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

#if (APP_DATA_LEN > (TIMECAST_STORE_MAX_DATA_LEN))
#  error "APP_DATA_LEN exceeds TIMECAST_STORE_MAX_DATA_LEN budget"
#endif
#define LOCAL_DATA_LEN ((uint8_t)(APP_DATA_LEN))
#define LOCAL_P2_PAYLOAD_LEN \
        ((uint8_t)(NRF_SF_RADIO_HDR_LEN + PACKET_P2_DATA_FRAME_HDR_LEN + LOCAL_DATA_LEN))
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
#define P1_SLOT_ACTIVE_US     (P1_SLOT_US - SLOT_PROCESSING_US)
#define P1_SLOT_ACTIVE_TICKS  NRF_SF_RADIO_US_TO_TIMER_TICKS(P1_SLOT_ACTIVE_US)
#define SLOT_PROCESSING_TICKS NRF_SF_RADIO_US_TO_TIMER_TICKS(SLOT_PROCESSING_US)

static timecast_store_t g_store;
static timecast_protocol_state_t g_proto;
static uint32_t g_round_count;
static uint32_t g_round_p2_start_ticks;
static uint8_t g_local_desired_class;
static uint8_t g_local_scheduled_data_len;
static uint8_t g_local_scheduled_data[TIMECAST_STORE_MAX_DATA_LEN];
static struct {
    uint32_t subslot_miss;
    uint32_t rx_attempts;
    uint32_t rx_arm_late;
    uint32_t no_address;
    uint32_t end_timeout;
    uint32_t crc_error;
    uint32_t reject;
    int32_t min_arm_slack_ticks;
    uint32_t max_address_offset_ticks;
} g_p2_diag;
static struct {
    uint64_t total_present;
    uint32_t sample_rounds;
} g_p2_success;

static bool _is_master(void);

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

static uint32_t _improved_round_period_ticks(bool run_pre)
{
    uint32_t p1_slot_ticks = g_proto_cfg.glossy_slot_ticks;

    if (run_pre) {
        p1_slot_ticks += g_proto_cfg.packed_schedule_ticks;
    }

    return 2U * NTX * p1_slot_ticks
        + g_proto_cfg.p1_guard_ticks
        + 2U * P2_NTX * p2_get_slot_ticks(&g_proto, &g_proto_cfg)
        + NRF_SF_RADIO_US_TO_TIMER_TICKS(ROUND_GAP_US);
}

static bool _is_master(void)
{
    return (LOCAL_NODE_ID == 0U);
}

static void _format_class_map(char *dst, size_t dst_len,
                              const uint8_t *classes, bool desired_local_map)
{
    static const char hex_digits[] = "0123456789ABCDEF";
    uint8_t source_id;

    if (!dst || (dst_len == 0U)) {
        return;
    }

    if ((dst_len <= (size_t)g_proto_cfg.p2_node_count) ||
        (!desired_local_map && !classes)) {
        dst[0] = '\0';
        return;
    }

    for (source_id = 0U; source_id < g_proto_cfg.p2_node_count; source_id++) {
        uint8_t class_id;

        if (desired_local_map) {
            if (source_id != (uint8_t)LOCAL_NODE_ID) {
                dst[source_id] = '.';
                continue;
            }
            class_id = g_local_desired_class;
        }
        else {
            class_id = classes[source_id];
        }
        dst[source_id] = (class_id <= CLASS_MAX_ID) ? hex_digits[class_id] : '?';
    }
    dst[g_proto_cfg.p2_node_count] = '\0';
}

static uint8_t _local_data_len_for_source(void)
{
#if (TEST_NODE2_CLASS_PERIOD > 0U)
    if (LOCAL_NODE_ID == 2U) {
        uint32_t round_idx = (g_round_count > 0U) ? (g_round_count - 1U) : 0U;
        uint32_t class_span = (uint32_t)TEST_NODE2_CLASS_MAX -
                              (uint32_t)TEST_NODE2_CLASS_MIN + 1U;
        uint8_t class_id = (uint8_t)((uint32_t)TEST_NODE2_CLASS_MIN +
                                     ((round_idx / (uint32_t)TEST_NODE2_CLASS_PERIOD) %
                                      class_span));
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
    uint8_t data[TIMECAST_STORE_MAX_DATA_LEN];
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
        _build_local_data_with_len(data, data_len);
        memcpy(g_local_scheduled_data, data, data_len);
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
    uint8_t data[TIMECAST_STORE_MAX_DATA_LEN];

    if (source_id >= g_proto_cfg.p2_node_count) {
        return;
    }

    for (node_id = 0U; node_id < g_proto_cfg.p2_node_count; node_id++) {
        g_proto.scheduled_class[node_id] = frame_len_to_class((uint8_t)PACKET_P2_DATA_FRAME_HDR_LEN + MAX_DATA_LEN);
    }
    g_proto.have_schedule = _is_master();
    data_len = _local_data_len_for_source();
    data_class = frame_len_to_class((uint8_t)(PACKET_P2_DATA_FRAME_HDR_LEN + data_len));

    _build_local_data_with_len(data, data_len);
    memcpy(g_local_scheduled_data, data, data_len);
    g_local_scheduled_data_len = data_len;

    g_local_desired_class = data_class;
}

static void _load_local_desired_data_for_p2(void)
{
    uint8_t source_id = (uint8_t)LOCAL_NODE_ID;
    uint8_t data[TIMECAST_STORE_MAX_DATA_LEN];
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

static uint32_t _elapsed_since_ticks(uint32_t start_ticks, uint32_t now_tick)
{
    if (start_ticks == 0U) {
        return 0U;
    }

    return now_tick - start_ticks;
}

static void _log_p2_success_rate(uint16_t present_count)
{
    uint32_t current_rate_x100 =
        ((uint32_t)present_count * 10000U) / g_proto_cfg.p2_node_count;
    uint32_t average_rate_x100 = 0U;

    if (g_p2_success.sample_rounds > 0U) {
        average_rate_x100 = (uint32_t)(
            (g_p2_success.total_present * 10000U) /
            ((uint64_t)g_p2_success.sample_rounds *
             g_proto_cfg.p2_node_count));
    }

    printf("[tc:rate] R=%" PRIu32 " cur=%" PRIu32 ".%02" PRIu32 "\n",
           g_round_count,
           current_rate_x100 / 100U,
           current_rate_x100 % 100U);
    printf("[tc:rate] R=%" PRIu32 " avg=%" PRIu32 ".%02" PRIu32
           " n=%" PRIu32 "\n",
           g_round_count,
           average_rate_x100 / 100U,
           average_rate_x100 % 100U,
           g_p2_success.sample_rounds);
}

static void _log_round_summary(void)
{
    uint8_t local_pending_updates = (USE_PRE_P2 &&
                                    (g_local_desired_class !=
                                     g_proto.scheduled_class[LOCAL_NODE_ID])) ? 1U : 0U;
    uint32_t now_tick = nrf_sf_radio_now_ticks();
    uint32_t p2_duration_ticks = _elapsed_since_ticks(g_round_p2_start_ticks, now_tick);
    uint16_t present_count = store_present_count(&g_store);
    uint32_t p2_slot_ticks = p2_get_slot_ticks(&g_proto, &g_proto_cfg);
    bool log_update_state = USE_PRE_P2 &&
                            (g_proto.round_run_pre ||
                             (local_pending_updates > 0U) ||
                             g_proto.master_run_pre_next_round ||
                             (g_proto.master_p2_incomplete_rounds > 0U));
    char missing_map[TIMECAST_STORE_MAX_NODES + 1U];

    printf("[tc] r=%" PRIu32 " id=%u role=%s\n",
           g_round_count,
           (unsigned)LOCAL_NODE_ID,
           _is_master() ? "master" : "follower");
    printf("[tc] r=%" PRIu32 " e=%" PRIu32 "\n",
           g_round_count,
           g_proto.current_epoch);
    printf("[tc] r=%" PRIu32 " j=%u h=%u\n",
           g_round_count,
           (unsigned)g_proto.joined,
           (unsigned)g_proto.p1.local_hop);
    printf("[tc] r=%" PRIu32 " ss=%u %s=%" PRIu32 "\n",
           g_round_count,
           (unsigned)g_proto.p1.slot_idx,
           USE_PRE_P2 ? "p1rx" : "rx",
           g_proto.rx_valid);
    if (USE_PRE_P2) {
        printf("[tc] r=%" PRIu32 " pre=%u \n",
               g_round_count,
               (unsigned)g_proto.round_run_pre);
    }
    printf("[tc] r=%" PRIu32 " p2rx=%" PRIu32 "\n",
           g_round_count,
           g_proto.p2.rx_valid);
    printf("[tc] r=%" PRIu32 " p2store=%" PRIu32 "\n",
           g_round_count,
           g_proto.p2.store_updates);
    _log_p2_success_rate(present_count);
    printf("[tc] r=%" PRIu32 " present=%u/%u\n",
           g_round_count,
           (unsigned)present_count,
           (unsigned)P2_NODE_COUNT);
    printf("[tc] r=%" PRIu32 " p1slot=%u\n",
           g_round_count,
           (unsigned)P1_SLOT_US);
    if (present_count < g_proto_cfg.p2_node_count) {
        for (uint8_t id = 0U; id < g_proto_cfg.p2_node_count; id++) {
            missing_map[id] = g_store.entries[id].present ? '.' : 'X';
        }
        missing_map[g_proto_cfg.p2_node_count] = '\0';
        for (uint8_t offset = 0U; offset < g_proto_cfg.p2_node_count;
             offset = (uint8_t)(offset + 16U)) {
            uint8_t map_len =
                (uint8_t)(g_proto_cfg.p2_node_count - offset);

            if (map_len > 16U) {
                map_len = 16U;
            }
            printf("[tc:miss] R=%" PRIu32 " o=%u %.*s\n",
                   g_round_count,
                   (unsigned)offset,
                   (int)map_len,
                   &missing_map[offset]);
        }
    }
    printf("[tc] r=%" PRIu32 " p2slot=%" PRIu32 "\n",
           g_round_count,
           NRF_SF_RADIO_TIMER_TICKS_TO_US(p2_slot_ticks));
    printf("[tc] r=%" PRIu32 " p2rdu=%" PRIu32 "\n",
           g_round_count,
           NRF_SF_RADIO_TIMER_TICKS_TO_US(p2_duration_ticks));
    printf("[tc] r=%" PRIu32 " p2start=%" PRIu32 "\n",
           g_round_count,
           NRF_SF_RADIO_TIMER_TICKS_TO_US(g_round_p2_start_ticks));
    printf("[tc:d1] R=%" PRIu32 " m=%" PRIu32 "\n",
           g_round_count,
           g_p2_diag.subslot_miss);
    printf("[tc:d1] R=%" PRIu32 " a=%" PRIu32 " l=%" PRIu32 "\n",
           g_round_count,
           g_p2_diag.rx_attempts,
           g_p2_diag.rx_arm_late);
    printf("[tc:d2] R=%" PRIu32 " n=%" PRIu32 "\n",
           g_round_count,
           g_p2_diag.no_address);
    printf("[tc:d2] R=%" PRIu32 " e=%" PRIu32 " c=%" PRIu32 "\n",
           g_round_count,
           g_p2_diag.end_timeout,
           g_p2_diag.crc_error);

    printf("[tc:d3] R=%" PRIu32 " r=%" PRIu32 "\n",
           g_round_count,
           g_p2_diag.reject);
    printf("[tc:d3] R=%" PRIu32 " s=%" PRId32 " o=%" PRIu32 "\n",
           g_round_count,
           g_p2_diag.min_arm_slack_ticks,
           g_p2_diag.max_address_offset_ticks);
    if (log_update_state) {
        uint8_t map_offset;
        char scheduled_class_map[TIMECAST_STORE_MAX_NODES + 1U];
        char desired_class_map[TIMECAST_STORE_MAX_NODES + 1U];

        _format_class_map(scheduled_class_map, sizeof(scheduled_class_map),
                          g_proto.scheduled_class, false);
        _format_class_map(desired_class_map, sizeof(desired_class_map),
                          NULL, true);
        printf("[tc:u1] R=%" PRIu32 " loc=%u next=%u\n",
               g_round_count,
               (unsigned)local_pending_updates,
               (unsigned)g_proto.master_run_pre_next_round);
        printf("[tc:u1] R=%" PRIu32 " inc=%" PRIu32 "\n",
               g_round_count,
               g_proto.master_p2_incomplete_rounds);
        printf("[tc:u2] R=%" PRIu32 " have=%u\n",
               g_round_count,
               (unsigned)g_proto.have_schedule);
        for (map_offset = 0U;
             map_offset < g_proto_cfg.p2_node_count;
             map_offset = (uint8_t)(map_offset + 16U)) {
            uint8_t map_len =
                (uint8_t)(g_proto_cfg.p2_node_count - map_offset);

            if (map_len > 16U) {
                map_len = 16U;
            }
            printf("[tc:cc] R=%" PRIu32 " o=%u %.*s\n",
                   g_round_count,
                   (unsigned)map_offset,
                   (int)map_len,
                   &scheduled_class_map[map_offset]);
            printf("[tc:dc] R=%" PRIu32 " o=%u %.*s\n",
                   g_round_count,
                   (unsigned)map_offset,
                   (int)map_len,
                   &desired_class_map[map_offset]);
        }
    }
}

static void _handle_p1_rx(const uint8_t *frame_buf, uint32_t rx_time)
{
    p1_sync_frame_t frame;

    if (!decode_p1_sync(frame_buf, &frame)) {
        return;
    }
    p1_handle_rx(&g_proto, &frame, frame_buf, rx_time - NRF_SF_RADIO_RAMPUP_TIME_TICKS, &g_proto_cfg);
    g_proto.rx_valid++;
}

static void _handle_p2_rx(const uint8_t *frame_buf)
{
    p2_data_frame_t frame;
    uint8_t data[PACKET_P2_DATA_MAX_DATA_LEN];

    if (!decode_p2_data(frame_buf, &frame, data)) {
        g_p2_diag.reject++;
        return;
    }
    if (!p2_handle_rx(&g_proto, &g_store, &frame, data)) {
        g_p2_diag.reject++;
        return;
    }

}

static void _scan_until_reference(void)
{
    while (g_proto.p1.active &&
           !g_proto.p1.has_tref) {
        uint8_t *rx_frame = rx_buffer;
        uint32_t rx_ticks = nrf_sf_radio_rx_listen_until_packet(
            &rx_frame, P1_SLOT_TICKS + g_proto_cfg.packed_schedule_ticks, P1_SCAN_LOG_INTERVAL_US);

        if (rx_ticks > 2U) {
            _handle_p1_rx(rx_frame, rx_ticks);
        }
    }
}

static void _run_p1_slot(void)
{
    bool do_tx = g_proto.p1.flag_tx;
    uint32_t slot_start_ticks = p1_get_slot_start_local_ticks(&g_proto, &g_proto_cfg);
    uint32_t slot_active_end_ticks = slot_start_ticks + P1_SLOT_ACTIVE_TICKS;
    uint32_t now_tick = nrf_sf_radio_now_ticks();

    if ((int32_t)(now_tick - slot_start_ticks) >= 0) {
        p1_finish_slot(&g_proto, &g_proto_cfg, do_tx);
        return;
    }
    if(g_proto.round_run_pre) {
        slot_active_end_ticks += g_proto_cfg.packed_schedule_ticks;
    }
    if (do_tx) {
        p1_sync_frame_t frame;
        uint8_t frame_buf[PACKET_P1_SYNC_MAX_FRAME_LEN] = { 0 };
        p1_prepare_tx(&g_proto, &frame);
        size_t frame_len = encode_p1_sync(frame_buf, &frame,
                                          g_proto.scheduled_class,
                                          g_proto_cfg.p2_node_count);
        if (nrf_sf_radio_tx_start(frame_buf,
                                  slot_start_ticks,
                                  slot_active_end_ticks,
                                  frame_len)) {
            p1_finish_slot(&g_proto, &g_proto_cfg, true);
        }
        else {
            p1_finish_slot(&g_proto, &g_proto_cfg, false);
        }

        return;
    }

    nrf_sf_radio_wait_until_abs(NULL, slot_active_end_ticks);

    p1_finish_slot(&g_proto, &g_proto_cfg, false);
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
        encode_p2_data(frame_buf, &frame, data_ptr);
        if (!nrf_sf_radio_tx_start(frame_buf, subslot_start_ticks,
                                   subslot_active_end_ticks,
                                   PACKET_P2_DATA_FRAME_HDR_LEN +
                                   frame.data_len)) {
            p2_finish_subslot(&g_proto, &g_proto_cfg);
            return;
        }

        p2_finish_subslot(&g_proto, &g_proto_cfg);
        return;
    }

    uint32_t rxen_ticks =
        subslot_start_ticks - NRF_SF_RADIO_US_TO_TIMER_TICKS(P2_RX_LEAD_US);

    if ((int32_t)(now_tick - rxen_ticks) >= 0) {
        g_p2_diag.subslot_miss++;
        g_p2_diag.rx_arm_late++;
        (void)p2_finish_subslot(&g_proto, &g_proto_cfg);
        return;
    }
    if (store_has_data(&g_store, owner_id)) {
        nrf_sf_radio_wait_until_abs(NULL, subslot_active_end_ticks);
        p2_finish_subslot(&g_proto, &g_proto_cfg);
        return;
    }

    uint8_t rx_status;
    uint8_t *rx_frame = rx_buffer;
    int32_t arm_slack_ticks =
        (int32_t)(rxen_ticks - nrf_sf_radio_now_ticks());

    g_p2_diag.rx_attempts++;
    if ((g_p2_diag.rx_attempts == 1U) ||
        (arm_slack_ticks < g_p2_diag.min_arm_slack_ticks)) {
        g_p2_diag.min_arm_slack_ticks = arm_slack_ticks;
    }
    if (arm_slack_ticks <= 0) {
        g_p2_diag.rx_arm_late++;
    }
    rx_status = nrf_sf_radio_rx_start(
        &rx_frame, rxen_ticks,
        rx_window_end_ticks + 10, subslot_active_end_ticks);
    if (rx_status == 0U) {
        uint32_t address_offset_ticks =
            nrf_sf_radio_get_last_address_time_ticks() - subslot_start_ticks;

        if (address_offset_ticks > g_p2_diag.max_address_offset_ticks) {
            g_p2_diag.max_address_offset_ticks = address_offset_ticks;
        }
        _handle_p2_rx(rx_frame);
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

    nrf_sf_radio_wait_until_abs(NULL, g_round_p2_start_ticks + p2_ticks);
}

static void _prepare_local_data(void)
{
    uint8_t data_len = _local_data_len_for_source();

    g_local_desired_class =
        frame_len_to_class((uint8_t)(PACKET_P2_DATA_FRAME_HDR_LEN + data_len));
    if(_is_master() && g_local_desired_class != g_proto.scheduled_class[0]) {
        g_proto.master_run_pre_next_round = true;
        g_proto.scheduled_class[0] = g_local_desired_class;
    }
}

static void _run_p1_phase(uint32_t next_master_round_start_ticks)
{
    if (_is_master()) {
        p1_start(&g_proto, next_master_round_start_ticks,
                 &g_proto_cfg, true, g_round_count);
    }
    else {
        p1_start(&g_proto, 0U, &g_proto_cfg, false, 0U);
        _scan_until_reference();
    }

    while (g_proto.p1.active &&
           g_proto.p1.has_tref) {
        _run_p1_slot();
    }
}

static void _run_round(uint32_t *next_master_round_start_ticks)
{
    uint32_t p2_start_ticks;

    _prepare_local_data();
    prepare_round(&g_proto, &g_store, &g_proto_cfg, USE_PRE_P2);
    g_round_count++;

    _run_p1_phase(*next_master_round_start_ticks);

    p2_start_ticks = g_proto.p1.p2_phase_start_local_ticks;

    if (g_proto.round_run_pre && g_proto.have_schedule) {
        _apply_round_schedule();
    }

    g_round_p2_start_ticks = p2_start_ticks;

    if(USE_PRE_P2) {
        _load_local_scheduled_data_for_p2();
        p2_start_pre_p2(&g_proto, p2_start_ticks, &g_proto_cfg);
        if (!g_proto.p2.active) {
            _wait_until_round_end();
        }
    } else {
        _load_local_desired_data_for_p2();
        p2_start_original(&g_proto, p2_start_ticks, &g_proto_cfg);
    }

    while (g_proto.p2.active) {
        _run_p2_subslot();
    }

    master_track_p2_completeness(&g_proto, &g_proto_cfg,
                                 g_store.present_count, USE_PRE_P2,
                                 MASTER_P2_INCOMPLETE_PRE_THRESHOLD);
    if (_is_master()) {
        *next_master_round_start_ticks += _improved_round_period_ticks(g_proto.round_run_pre);
    }
}

int main(void)
{
    uint32_t next_master_round_start_ticks;

    printf("TimeCast start. node_id=%u hop=p1 role=%s ntx=%u"
           " pre_p2=%u app_data=%u payload=%u\n",
           (unsigned)LOCAL_NODE_ID,
           _is_master() ? "master" : "follower",
           (unsigned)P2_NTX,
           (unsigned)USE_PRE_P2,
           (unsigned)APP_DATA_LEN,
           (unsigned)LOCAL_P2_PAYLOAD_LEN);
    nrf_sf_radio_start();

    store_init(&g_store);
    protocol_init(&g_proto, _is_master());

    _local_data_init();
    nrf_sf_radio_set_ble_channel(24);
    nrf_sf_radio_set_power(4);
    g_round_count = 0U;
    next_master_round_start_ticks = nrf_sf_radio_now_ticks() +
                                    NRF_SF_RADIO_US_TO_TIMER_TICKS(MASTER_START_DELAY_US);

    while (g_round_count < ROUND_TIME) {
            _run_round(&next_master_round_start_ticks);

        if (g_store.present_count > 1U) {
            g_p2_success.total_present += g_store.present_count;
            g_p2_success.sample_rounds++;
        }
        _log_round_summary();
    }

    return 0;
}
