/*
 * SPDX-FileCopyrightText: 2026 Xin He
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "link_radio.h"
#include "radio_driver.h"
#include "packet.h"
#include "protocol.h"
#include "store.h"

#if (APP_DATA_LEN > (MAX_DATA_LEN))
#  error "APP_DATA_LEN exceeds TIMECAST_STORE_MAX_DATA_LEN budget"
#endif
#define LOCAL_DATA_LEN ((uint8_t)(APP_DATA_LEN))
#define LOCAL_P2_PAYLOAD_LEN \
        ((NRF_SF_RADIO_HDR_LEN + PACKET_P2_DATA_FRAME_HDR_LEN + LOCAL_DATA_LEN))
#define PACKET_AIR_TIME_US(payload_len) \
        (8U * ((uint32_t)(payload_len) + SLOT_PHY_OVERHEAD_BYTES))
#define P2_PAYLOAD_TO_SUBSLOT_US(payload_len) \
        (SLOT_PROCESSING_US + RADIO_RAMPUP_US + \
         PACKET_AIR_TIME_US(payload_len))
#if (NTX > 63U)
#  error "NTX exceeds 7-bit packed relay_cnt budget"
#endif

static uint8_t rx_buffer[258] = { 0 };

#define P1_SLOT_TICKS         NRF_SF_RADIO_US_TO_TIMER_TICKS(P1_SLOT_US)
#define P2_SUBSLOT_TICKS      NRF_SF_RADIO_US_TO_TIMER_TICKS(P2_SUBSLOT_US)
#define SLOT_PROCESSING_TICKS NRF_SF_RADIO_US_TO_TIMER_TICKS(SLOT_PROCESSING_US)

static timecast_store_t g_store;
static timecast_protocol_state_t g_proto;
static uint32_t g_round_count;
/* Per-round RX/TX enabled time estimate, in microseconds. */
static uint32_t g_round_radio_on_us;
static uint64_t g_total_radio_on_us;
static uint32_t g_round_duration_us;
static uint64_t g_total_round_duration_us;
static uint8_t g_local_desired_class;
static uint8_t g_local_scheduled_data_len;
static uint8_t g_local_scheduled_data[MAX_DATA_LEN];
static struct {
    uint32_t subslot_miss;
    uint32_t rx_attempts;
    uint32_t no_address;
    uint32_t end_timeout;
    uint32_t crc_error;
    uint32_t reject;
    uint32_t max_address_offset_ticks;
} g_p2_diag;
static struct {
    uint64_t total_present;
    uint32_t sample_rounds;
} g_p2_success;
static struct {
    bool enabled;
    /* Relative to tref: self is 0, missing sources stay UINT32_MAX. */
    uint32_t first_rx_us[TIMECAST_STORE_MAX_NODES];
} g_prr_trace;

static timecast_protocol_cfg_t g_proto_cfg = {
    .local_node_id = (uint8_t)LOCAL_NODE_ID,
    .ntx = NTX,
    .p2_ntx = P2_NTX,
    .p2_node_count = P2_NODE_COUNT,
    .glossy_slot_ticks = P1_SLOT_TICKS,
    .packed_schedule_ticks = NRF_SF_RADIO_US_TO_TIMER_TICKS(((P2_NODE_COUNT + 1U) / 2U) * 8U),
    .p1_rx_ts_to_slot_start_ticks = NRF_SF_RADIO_US_TO_TIMER_TICKS(P1_RX_TS_TO_SLOT_START_US),
    .p1_guard_ticks = NRF_SF_RADIO_US_TO_TIMER_TICKS(P2_START_GUARD_US),
    .p2_subslot_ticks = P2_SUBSLOT_TICKS,
    .p2_guard_ticks = NRF_SF_RADIO_US_TO_TIMER_TICKS(P2_SUBSLOT_GUARD_US),
    .p2_rx_window_ticks = NRF_SF_RADIO_US_TO_TIMER_TICKS(P2_RX_WINDOW_US),
    .p2_payload_base_ticks =
        NRF_SF_RADIO_US_TO_TIMER_TICKS(SLOT_PROCESSING_US + RADIO_RAMPUP_US +
                                       (8U * (SLOT_PHY_OVERHEAD_BYTES + NRF_SF_RADIO_HDR_LEN))),
    .p2_payload_byte_ticks = NRF_SF_RADIO_US_TO_TIMER_TICKS(8U),
};

static uint32_t _round_period_ticks(void)
{
    return (uint32_t)(g_proto.p2.start_local_ticks -
                      g_proto.p1.tref_local_ticks)
           + 2U * g_proto_cfg.p2_ntx * g_proto.p2.slot_ticks
           + NRF_SF_RADIO_US_TO_TIMER_TICKS(ROUND_GAP_US);
}

static bool _is_master(void)
{
    return (LOCAL_NODE_ID == 0U);
}

static uint8_t _local_data_len_for_source(void)
{
#if (TEST_NODE2_CLASS_PERIOD > 0U)
    if (LOCAL_NODE_ID == 2U) {
        uint32_t round_idx = g_round_count;
        uint8_t class_id = (uint8_t)(
            (round_idx / (uint32_t)TEST_NODE2_CLASS_PERIOD) %
            (uint32_t)CLASS_COUNT);
        uint8_t p2_frame_len = class_to_frame_len(class_id);

        return (uint8_t)(p2_frame_len - PACKET_P2_DATA_FRAME_HDR_LEN);
    }
#endif

    return LOCAL_DATA_LEN;
}

static void _build_local_data_with_len(uint8_t *dst, uint8_t data_len)
{
    memset(dst, 'c', (size_t)data_len);
}

static void _apply_round_schedule(void)
{
    uint8_t local_source_id = (uint8_t)LOCAL_NODE_ID;
    uint8_t scheduled_p2_frame_len;
    uint8_t scheduled_data_len;
    uint8_t desired_data_len;
    uint8_t data_len;

    scheduled_p2_frame_len =
        class_to_frame_len(g_proto.scheduled_class[local_source_id]);
    scheduled_data_len = (uint8_t)(scheduled_p2_frame_len -
                                   PACKET_P2_DATA_FRAME_HDR_LEN);
    desired_data_len = _local_data_len_for_source();
    if ((desired_data_len <= scheduled_data_len) ) {
        data_len = desired_data_len;
        _build_local_data_with_len(g_local_scheduled_data, data_len);
        g_local_scheduled_data_len = data_len;
    } else if(g_local_scheduled_data_len > scheduled_data_len){
        g_local_scheduled_data_len = scheduled_data_len;
    }

}

static void _local_data_init(void)
{
    uint8_t node_id;
    uint8_t source_id = (uint8_t)LOCAL_NODE_ID;
    uint8_t data_len;
    uint8_t data_class;

    if (source_id >= g_proto_cfg.p2_node_count) {
        return;
    }

    for (node_id = 0U; node_id < g_proto_cfg.p2_node_count; node_id++) {
        g_proto.scheduled_class[node_id] = frame_len_to_class((uint8_t)PACKET_P2_DATA_FRAME_HDR_LEN + MAX_DATA_LEN);
    }
    g_proto.have_schedule = _is_master();
    data_len = _local_data_len_for_source();
    data_class = frame_len_to_class((uint8_t)(PACKET_P2_DATA_FRAME_HDR_LEN + data_len));

    _build_local_data_with_len(g_local_scheduled_data, data_len);
    g_local_scheduled_data_len = data_len;

    g_local_desired_class = data_class;
}

static void _load_local_desired_data_for_p2(void)
{
    uint8_t source_id = (uint8_t)LOCAL_NODE_ID;
    uint8_t data[MAX_DATA_LEN];
    uint8_t data_len;

    data_len = _local_data_len_for_source();
    _build_local_data_with_len(data, data_len);
    (void)store_import(&g_store, source_id, data, g_local_desired_class, data_len);
}

static void _load_local_scheduled_data_for_p2(void)
{
    uint8_t source_id = (uint8_t)LOCAL_NODE_ID;

    if (source_id < g_proto_cfg.p2_node_count) {
        (void)store_import(&g_store, source_id, g_local_scheduled_data, g_local_desired_class,
                           g_local_scheduled_data_len);
    }
}

static void _log_p2_success_rate(void)
{
    uint32_t average_rate_x100 = 0U;

    if (g_p2_success.sample_rounds > 0U) {
        average_rate_x100 = (uint32_t)(
            (g_p2_success.total_present * 10000U) /
            ((uint64_t)g_p2_success.sample_rounds *
             g_proto_cfg.p2_node_count));
    }

    printf("[tc:rate] R=%" PRIu32 " avg=%" PRIu32 ".%02" PRIu32
           " n=%" PRIu32 "\n",
           g_round_count,
           average_rate_x100 / 100U,
           average_rate_x100 % 100U,
           g_p2_success.sample_rounds);
}

static void _log_round_summary(void)
{
    uint32_t radio_on_avg_us = g_round_count ?
        (uint32_t)(g_total_radio_on_us / g_round_count) : 0U;
    uint32_t round_avg_us = g_round_count ?
        (uint32_t)(g_total_round_duration_us / g_round_count) : 0U;

    printf("[tc] R=%" PRIu32 " round_avg_us=%" PRIu32 "\n",
           g_round_count, round_avg_us);
    printf("[tc] R=%" PRIu32 " radio_on_avg_us=%" PRIu32 "\n",
           g_round_count, radio_on_avg_us);
    _log_p2_success_rate();
}

static void _log_prr_trace(void)
{
    printf("[tc:prr_time] E=%" PRIu32 " id=%u rx_us=",
           g_proto.current_epoch, (unsigned)g_proto_cfg.local_node_id);
    for (uint8_t source_id = 0U; source_id < g_proto_cfg.p2_node_count; source_id++) {
        printf("%s%" PRIu32, source_id ? "," : "",
               g_prr_trace.first_rx_us[source_id]);
    }
    putchar('\n');
}

static void _handle_p1_rx(const uint8_t *frame_buf, uint32_t rx_time)
{
    p1_sync_frame_t frame;
    const uint8_t *buf = &frame_buf[NRF_SF_RADIO_HDR_LEN + NRF_SF_RADIO_S1_LEN];
    if (!decode_p1_sync(buf, &frame)) {
        return;
    }
    p1_handle_rx(&g_proto, &frame, frame_buf, rx_time - NRF_SF_RADIO_RAMPUP_TIME_TICKS, &g_proto_cfg);
    g_proto.rx_valid++;
}

static void _handle_p2_rx(const uint8_t *frame_buf)
{
    p2_data_frame_t frame;
    uint8_t data_len;
    const uint8_t *buf = &frame_buf[NRF_SF_RADIO_HDR_LEN + NRF_SF_RADIO_S1_LEN];

    if (frame_buf[1] < NRF_SF_RADIO_ADV_ADDR_LEN + PACKET_P2_DATA_FRAME_HDR_LEN) {
        g_p2_diag.reject++;
        return;
    }
    data_len = frame_buf[1] - NRF_SF_RADIO_ADV_ADDR_LEN - PACKET_P2_DATA_FRAME_HDR_LEN;
    if (data_len > PACKET_P2_DATA_MAX_DATA_LEN) {
        g_p2_diag.reject++;
        return;
    }

    if (!decode_p2_data(buf, &frame)) {
        g_p2_diag.reject++;
        return;
    }
    
    uint16_t present_count = g_store.present_count;
    if (!p2_handle_rx(&g_proto, &g_store, &frame, &buf[PACKET_P2_DATA_FRAME_HDR_LEN], data_len)) {
        g_p2_diag.reject++;
        return;
    }

    if (g_prr_trace.enabled && (g_store.present_count > present_count)) {
        g_prr_trace.first_rx_us[frame.source_node_id] =
            NRF_SF_RADIO_TIMER_TICKS_TO_US(nrf_sf_radio_get_last_end_time_ticks() -
                                          g_proto.p1.tref_local_ticks);
    }
}

static void _scan_until_reference(void)
{
    while (g_proto.p1.active &&
           !g_proto.p1.has_tref) {
        uint32_t rx_start_ticks = nrf_sf_radio_now_ticks();
        uint32_t rx_ticks = nrf_sf_radio_rx_listen_until_packet(
            rx_buffer, P1_SLOT_TICKS + g_proto_cfg.packed_schedule_ticks, P1_SCAN_LOG_INTERVAL_US);
        g_round_radio_on_us += NRF_SF_RADIO_TIMER_TICKS_TO_US(
            (NRF_RADIO->EVENTS_END ? nrf_sf_radio_get_last_end_time_ticks() :
                                   nrf_sf_radio_now_ticks()) - rx_start_ticks);

        if (rx_ticks > 2U) {
            _handle_p1_rx(rx_buffer, rx_ticks);
            if (g_proto.p1.slot_idx >= (uint8_t)(g_proto_cfg.ntx * 2U)) {
                g_proto.p1.active = false;
            }
        }
    }
}

static void _run_p1_slot(void)
{
    bool do_tx = g_proto.p1.flag_tx;
    uint32_t slot_start_ticks = p1_get_slot_start_local_ticks(&g_proto, &g_proto_cfg);
    uint32_t slot_active_end_ticks = slot_start_ticks +
                                        p1_get_slot_ticks(&g_proto, &g_proto_cfg) -
                                        SLOT_PROCESSING_TICKS;
    uint32_t now_tick = nrf_sf_radio_now_ticks();

    if ((int32_t)(now_tick - slot_start_ticks) >= 0) {
        p1_finish_slot(&g_proto, &g_proto_cfg, do_tx);
        return;
    }
    if (do_tx) {
        p1_sync_frame_t frame;
        uint8_t frame_buf[PACKET_P1_SYNC_MAX_FRAME_LEN] = { 0 };
        p1_prepare_tx(&g_proto, &frame);
        size_t frame_len = encode_p1_sync(frame_buf, &frame,
                                          g_proto.scheduled_class,
                                          g_proto_cfg.p2_node_count);
        nrf_sf_radio_tx_start(frame_buf,
                                  slot_start_ticks,
                                  slot_active_end_ticks,
                                  frame_len);
        if (NRF_TIMER3->EVENTS_COMPARE[0] != 0U) {
            g_round_radio_on_us += NRF_SF_RADIO_TIMER_TICKS_TO_US(
                (NRF_RADIO->EVENTS_END ? nrf_sf_radio_get_last_end_time_ticks() :
                                       nrf_sf_radio_now_ticks()) - slot_start_ticks);
        }
        p1_finish_slot(&g_proto, &g_proto_cfg, do_tx);
        

        return;
    }

    nrf_sf_radio_wait_until_abs(NULL, slot_active_end_ticks);

    p1_finish_slot(&g_proto, &g_proto_cfg, do_tx);
}

static void _run_p2_subslot(void)
{
    bool tx_slot = g_proto.p2.tx_slot;
    uint32_t subslot_ticks = g_proto.p2.subslot_ticks[g_proto.p2.subslot_idx];
    uint32_t subslot_start_ticks = p2_get_subslot_start_local_ticks(&g_proto);
    uint32_t subslot_end_ticks = subslot_start_ticks + subslot_ticks;
    uint32_t subslot_active_end_ticks = subslot_end_ticks - SLOT_PROCESSING_TICKS;
    uint32_t rx_window_end_ticks = subslot_start_ticks + g_proto_cfg.p2_rx_window_ticks;
    uint32_t now_tick = nrf_sf_radio_now_ticks();
    uint8_t owner_id = g_proto.p2.subslot_idx;

    if (tx_slot) {
        if ((int32_t)(now_tick - subslot_start_ticks) >= 0) {
            g_p2_diag.subslot_miss++;
            (void)p2_finish_subslot(&g_proto, &g_proto_cfg);
            return;
        }
        p2_data_frame_t frame;
        const uint8_t *data_ptr = NULL;
        uint8_t frame_buf[PACKET_P2_DATA_MAX_FRAME_LEN] = { 0 };
        if (!p2_prepare_tx(&g_proto, &g_store, &frame, &data_ptr)) {
            nrf_sf_radio_wait_until_abs(NULL, subslot_active_end_ticks);
            p2_finish_subslot(&g_proto, &g_proto_cfg);
            return;
        }
        encode_p2_data(frame_buf, &frame, data_ptr, g_store.entries[owner_id].len);
        nrf_sf_radio_tx_start(frame_buf, subslot_start_ticks,
                             subslot_active_end_ticks,
                             PACKET_P2_DATA_FRAME_HDR_LEN +
                             g_store.entries[owner_id].len);
        if (NRF_TIMER3->EVENTS_COMPARE[0] != 0U) {
            g_round_radio_on_us += NRF_SF_RADIO_TIMER_TICKS_TO_US(
                (NRF_RADIO->EVENTS_END ? nrf_sf_radio_get_last_end_time_ticks() :
                                       nrf_sf_radio_now_ticks()) - subslot_start_ticks);
        }

        p2_finish_subslot(&g_proto, &g_proto_cfg);
        return;
    }

    uint32_t rxen_ticks =
        subslot_start_ticks - NRF_SF_RADIO_US_TO_TIMER_TICKS(P2_RX_LEAD_US);

    if ((int32_t)(now_tick - rxen_ticks) >= 0) {
        g_p2_diag.subslot_miss++;
        (void)p2_finish_subslot(&g_proto, &g_proto_cfg);
        return;
    }
    if (store_has_data(&g_store, owner_id)) {
        nrf_sf_radio_wait_until_abs(NULL, subslot_active_end_ticks);
        p2_finish_subslot(&g_proto, &g_proto_cfg);
        return;
    }

    uint8_t rx_status;

    g_p2_diag.rx_attempts++;
    rx_status = nrf_sf_radio_rx_start(
        rx_buffer, rxen_ticks,
        rx_window_end_ticks + 10, subslot_active_end_ticks);
    if (NRF_TIMER3->EVENTS_COMPARE[0] != 0U) {
        g_round_radio_on_us += NRF_SF_RADIO_TIMER_TICKS_TO_US(
            (NRF_RADIO->EVENTS_END ? nrf_sf_radio_get_last_end_time_ticks() :
                                   nrf_sf_radio_now_ticks()) - rxen_ticks);
    }
    if (rx_status == 0U) {
        uint32_t address_offset_ticks =
            nrf_sf_radio_get_last_address_time_ticks() - subslot_start_ticks;

        if (address_offset_ticks > g_p2_diag.max_address_offset_ticks) {
            g_p2_diag.max_address_offset_ticks = address_offset_ticks;
        }
        _handle_p2_rx(rx_buffer);
    }
    else if (rx_status == 1U) {
        g_p2_diag.no_address++;
    }
    else if (rx_status == 2U) {
        g_p2_diag.end_timeout++;
    }
    else if (rx_status == 3U) {
        g_p2_diag.crc_error++;
    }

    p2_finish_subslot(&g_proto, &g_proto_cfg);
}

static void _wait_until_round_end(void)
{
    uint32_t p2_ticks = 2U * P2_NTX * g_proto.p2.slot_ticks;

    nrf_sf_radio_wait_until_abs(NULL, g_proto.p2.start_local_ticks + p2_ticks);
}

static void _prepare_local_data(void)
{
    uint8_t data_len = _local_data_len_for_source();

    g_local_desired_class =
        frame_len_to_class((uint8_t)(PACKET_P2_DATA_FRAME_HDR_LEN + data_len));
    if(_is_master() && g_local_desired_class != g_proto.scheduled_class[0]) {
        g_proto.master_with_table_next_round = true;
        g_proto.scheduled_class[0] = g_local_desired_class;
    }
}

static void _run_p1_phase(uint32_t next_master_round_start_ticks)
{
    if (_is_master()) {
        p1_start(&g_proto, next_master_round_start_ticks,
                 true, g_round_count);
    }
    else {
        p1_start(&g_proto, 0U, false, 0U);
        _scan_until_reference();
    }

    while (g_proto.p1.active &&
           g_proto.p1.has_tref) {
        _run_p1_slot();
    }
}

static void _run_round(uint32_t *next_master_round_start_ticks)
{
    uint32_t p1_slot_ticks;
    uint32_t p2_start_ticks;

    g_round_radio_on_us = 0U;
    _prepare_local_data();
    prepare_round(&g_proto, &g_store, &g_proto_cfg, USE_AT);

    _run_p1_phase(*next_master_round_start_ticks);

    g_prr_trace.enabled = false;
#if (PRR_TRACE_ROUNDS > 0)
    const uint32_t trace_start_epoch = PRR_TRACE_START_EPOCH;
    g_prr_trace.enabled =
        (g_proto.current_epoch >= trace_start_epoch) &&
        ((g_proto.current_epoch - trace_start_epoch) < PRR_TRACE_ROUNDS);
#endif
    if (g_prr_trace.enabled) {
        memset(g_prr_trace.first_rx_us, 0xff, sizeof(g_prr_trace.first_rx_us));
    }

    p1_slot_ticks = p1_get_slot_ticks(&g_proto, &g_proto_cfg);
    p2_start_ticks = g_proto.p1.tref_local_ticks +
                     ((uint32_t)g_proto_cfg.ntx * 2U) * p1_slot_ticks +
                     g_proto_cfg.p1_guard_ticks;

    if (g_proto.round_with_table && g_proto.have_schedule) {
        _apply_round_schedule();
    }

    if (USE_AT) {
        _load_local_scheduled_data_for_p2();
    }
    else {
        _load_local_desired_data_for_p2();
    }

    p2_start(&g_proto, p2_start_ticks, &g_proto_cfg, USE_AT);
    if (g_prr_trace.enabled) {
        if (store_has_data(&g_store, g_proto_cfg.local_node_id)) {
            g_prr_trace.first_rx_us[g_proto_cfg.local_node_id] = 0U;
        }
    }
    if (USE_AT && !g_proto.p2.active) {
        _wait_until_round_end();
    }

    while (g_proto.p2.active) {
        _run_p2_subslot();
    }

    /* Full P2 schedule end relative to tref, excluding the inter-round gap. */
    g_round_duration_us = NRF_SF_RADIO_TIMER_TICKS_TO_US(
        _round_period_ticks() - NRF_SF_RADIO_US_TO_TIMER_TICKS(ROUND_GAP_US));
    g_total_round_duration_us += g_round_duration_us;

    master_track_p2_completeness(&g_proto, &g_proto_cfg,
                                 g_store.present_count, USE_AT,
                                 MASTER_P2_INCOMPLETE_THRESHOLD);
    if (_is_master()) {
        *next_master_round_start_ticks += _round_period_ticks();
    }
    g_total_radio_on_us += g_round_radio_on_us;
    g_round_count++;
}

int main(void)
{
    uint32_t next_master_round_start_ticks;

    nrf_sf_radio_start();

    protocol_init(&g_proto, _is_master());

    _local_data_init();
    nrf_sf_radio_set_ble_channel(24);
    nrf_sf_radio_set_power(4);
    g_round_count = 0U;
    next_master_round_start_ticks = nrf_sf_radio_now_ticks() +
                                    NRF_SF_RADIO_US_TO_TIMER_TICKS(MASTER_START_DELAY_US);

    while (g_round_count < ROUND_TIME) {
        _run_round(&next_master_round_start_ticks);
        g_p2_success.total_present += g_store.present_count;
        g_p2_success.sample_rounds++;

        if(g_round_count%100 == 0)
        _log_round_summary();
        if (g_prr_trace.enabled) {
            _log_prr_trace();
        }
    }

    return 0;
}
