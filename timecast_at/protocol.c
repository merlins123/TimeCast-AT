/*
 * SPDX-FileCopyrightText: 2026 Xin He
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#include <string.h>

#include "protocol.h"

static bool _tx_first(const timecast_protocol_state_t *state)
{
    return state && ((state->p1.local_hop & 0x1U) == 0U);
}

static void _set_p2_phase_start(timecast_protocol_state_t *state,
                                  const timecast_protocol_cfg_t *cfg,
                                  uint32_t tref_local_ticks)
{   if(state->round_run_pre) {
        state->p1.p2_phase_start_local_ticks = tref_local_ticks +
                                             ((uint32_t)cfg->ntx * 2U) * (cfg->glossy_slot_ticks + cfg->packed_schedule_ticks) +
                                             cfg->p1_guard_ticks;
    } else {
        state->p1.p2_phase_start_local_ticks = tref_local_ticks +
                                             ((uint32_t)cfg->ntx * 2U) * cfg->glossy_slot_ticks +
                                             cfg->p1_guard_ticks;
    }
}

static void _p2_build_fixed_schedule(timecast_protocol_state_t *state,
                                     const timecast_protocol_cfg_t *cfg)
{
    uint8_t source_id;
    uint32_t slot_ticks = 0U;

    for (source_id = 0U; source_id < state->p2.node_count; source_id++) {
        state->p2.subslot_offset_ticks[source_id] = slot_ticks;
        state->p2.subslot_ticks[source_id] = cfg->p2_subslot_ticks;
        slot_ticks += cfg->p2_subslot_ticks + cfg->p2_guard_ticks;
    }

    state->p2.slot_ticks = slot_ticks;
}

static void _p2_build_adaptive_schedule(timecast_protocol_state_t *state,
                                        const timecast_protocol_cfg_t *cfg)
{
    uint8_t source_id;
    uint32_t slot_ticks = 0U;

    for (source_id = 0U; source_id < state->p2.node_count; source_id++) {
        uint8_t p2_frame_len = class_to_frame_len(state->scheduled_class[source_id]);
        uint32_t subslot_ticks;

        subslot_ticks = cfg->p2_payload_base_ticks +
                        ((uint32_t)p2_frame_len * cfg->p2_payload_byte_ticks);

        state->p2.subslot_offset_ticks[source_id] = slot_ticks;
        state->p2.subslot_ticks[source_id] = subslot_ticks;
        slot_ticks += subslot_ticks + cfg->p2_guard_ticks;
    }

    state->p2.slot_ticks = slot_ticks;
}

static uint8_t _class_span_bytes(void)
{
    return (uint8_t)(((PACKET_P2_DATA_MAX_DATA_LEN + 1U) +
                      CLASS_COUNT - 1U) / CLASS_COUNT);
}

void protocol_init(timecast_protocol_state_t *state, bool initiator)
{
    if (!state) {
        return;
    }

    memset(state, 0, sizeof(*state));
    state->p1.active = true;
    state->p1.flag_tx = initiator;
    state->p1.local_hop = initiator ? 0U : UINT8_MAX;
    state->joined = initiator;
    state->master_force_initial_pre = true;
}

void p1_start(timecast_protocol_state_t *state,
              uint32_t start_local_ticks,
              const timecast_protocol_cfg_t *cfg,
              bool initiator,
              uint32_t epoch)
{
    if (!state) {
        return;
    }

    memset(&state->p1, 0, sizeof(state->p1));

    state->joined = initiator;
    state->current_epoch = epoch;
    state->rx_valid = 0U;
    state->p1.active = true;
    state->p1.flag_tx = initiator;
    state->p1.local_hop = initiator ? 0U : UINT8_MAX;
    if (initiator) {
        state->p1.has_tref = true;
        state->p1.tref_local_ticks = start_local_ticks;
        state->p1.schedule_ver = state->local_schedule_ver;
        _set_p2_phase_start(state, cfg, start_local_ticks);
    }
}

uint32_t p1_get_slot_start_local_ticks(const timecast_protocol_state_t *state,
                                       const timecast_protocol_cfg_t *cfg)
{
    if (!state || !cfg || !state->p1.has_tref) {
        return 0U;
    }
    if(state->round_run_pre) {
        return state->p1.tref_local_ticks +
           ((uint32_t)state->p1.slot_idx * (cfg->glossy_slot_ticks + cfg->packed_schedule_ticks));
    }
    return state->p1.tref_local_ticks +
           ((uint32_t)state->p1.slot_idx * cfg->glossy_slot_ticks);
}

void p1_prepare_tx(timecast_protocol_state_t *state,
                   p1_sync_frame_t *frame)
{
    frame->packet_type = PACKET_TYPE_P1_SYNC;
    frame->relay_cnt = (uint8_t)state->p1.slot_idx;
    frame->schedule_ver = state->p1.schedule_ver;
    frame->epoch = state->current_epoch;
    frame->flags = state->round_run_pre ? 1U : 0U;
    state->p1.ntx_done++;
}

void p1_handle_rx(timecast_protocol_state_t *state,
                  const p1_sync_frame_t *frame,
                  const uint8_t *frame_buf,
                  uint32_t rx_local_ticks,
                  const timecast_protocol_cfg_t *cfg)
{
    uint32_t tref_local_ticks;
    bool first_reference;

    first_reference = !state->p1.has_tref;
    state->current_epoch = frame->epoch;

    if (!state->p1.has_tref || (frame->relay_cnt > state->p1.slot_idx)) {
        state->p1.slot_idx = frame->relay_cnt;
    }

    if((frame->flags & 1U) != 0U) {
        state->round_run_pre = true;
        state->have_schedule = true;
        state->local_schedule_ver = frame->schedule_ver;
        uint8_t source_id;

        for (source_id = 0U; source_id < cfg->p2_node_count; source_id++) {
            uint8_t packed_byte = frame_buf[PACKET_P1_SYNC_FRAME_LEN + source_id / 2U];

            if ((source_id & 0x1U) == 0U) {
                state->scheduled_class[source_id] = (uint8_t)(packed_byte & 0x0FU);
            }
            else {
                state->scheduled_class[source_id] = (uint8_t)((packed_byte >> 4) & 0x0FU);
            }
        }
    } else if(frame->schedule_ver != state->local_schedule_ver) {
        state->have_schedule = false;
    }

    if (!state->p1.has_tref) {
        uint32_t p1_slot_ticks = state->round_run_pre ? cfg->glossy_slot_ticks + cfg->packed_schedule_ticks : cfg->glossy_slot_ticks;
        tref_local_ticks = rx_local_ticks - cfg->p1_rx_ts_to_slot_start_ticks -
                           ((uint32_t)frame->relay_cnt * p1_slot_ticks);
        state->p1.has_tref = true;
        state->p1.local_hop = (uint8_t)(frame->relay_cnt + 1U);
        state->p1.tref_local_ticks = tref_local_ticks;
        state->p1.schedule_ver = frame->schedule_ver;
        _set_p2_phase_start(state, cfg, tref_local_ticks);
        state->joined = true;
    }

    if (first_reference) {
        state->p1.slot_idx = (uint8_t)(frame->relay_cnt + 1U);
        state->p1.flag_tx = (state->p1.ntx_done < cfg->ntx);
    }

}

void p1_finish_slot(timecast_protocol_state_t *state,
                    const timecast_protocol_cfg_t *cfg,
                    bool did_tx)
{
    if (!state || !cfg || !state->p1.active) {
        return;
    }

    if (!did_tx && state->p1.has_tref && (state->p1.ntx_done < cfg->ntx)) {
        state->p1.flag_tx = true;
    }
    else {
        state->p1.flag_tx = false;
    }

    state->p1.slot_idx++;
    if (state->p1.slot_idx >= (uint8_t)(cfg->ntx * 2U)) {
        state->p1.active = false;
    }
}

static bool _p2_start_common(timecast_protocol_state_t *state,
                             uint32_t start_local_ticks,
                             const timecast_protocol_cfg_t *cfg)
{
    memset(&state->p2, 0, sizeof(state->p2));
    state->p2.tx_slot = _tx_first(state);
    state->p2.node_count = cfg->p2_node_count;
    state->p2.start_local_ticks = start_local_ticks;
    state->p2.active = state->joined;

    return state->p2.active;
}

void p2_start_original(timecast_protocol_state_t *state,
                       uint32_t start_local_ticks,
                       const timecast_protocol_cfg_t *cfg)
{
    if (!_p2_start_common(state, start_local_ticks, cfg)) {
        return;
    }

    _p2_build_fixed_schedule(state, cfg);
}

void p2_start_pre_p2(timecast_protocol_state_t *state,
                     uint32_t start_local_ticks,
                     const timecast_protocol_cfg_t *cfg)
{
    if (!_p2_start_common(state, start_local_ticks, cfg)) {
        return;
    }

    if(state->have_schedule) {
        _p2_build_adaptive_schedule(state, cfg);
        state->p2.active = true;
    } else {
        state->p2.active = false;
    }
}

uint32_t p2_get_subslot_start_local_ticks(const timecast_protocol_state_t *state)
{
    return state->p2.start_local_ticks +
           ((uint32_t)state->p2.slot_idx * state->p2.slot_ticks) +
           state->p2.subslot_offset_ticks[state->p2.subslot_idx];
}

uint32_t p2_get_slot_ticks(const timecast_protocol_state_t *state,
                           const timecast_protocol_cfg_t *cfg)
{
    if (!state || !cfg) {
        return 0U;
    }

    if (state->p2.slot_ticks > 0U) {
        return state->p2.slot_ticks;
    }

    return (uint32_t)cfg->p2_node_count * (cfg->p2_subslot_ticks + cfg->p2_guard_ticks);
}

bool p2_prepare_tx(const timecast_protocol_state_t *state,
                   const timecast_store_t *store,
                   p2_data_frame_t *frame,
                   const uint8_t **data_ptr)
{
    const timecast_store_entry_t *entry;
    uint8_t owner_id;

    owner_id = state->p2.subslot_idx;
    entry = &store->entries[owner_id];
    if (!entry->present) {
        return false;
    }

    frame->packet_type = PACKET_TYPE_P2_DATA;
    frame->source_node_id = owner_id;
    frame->slot_idx = state->p2.slot_idx;
    frame->subslot_idx = state->p2.subslot_idx;
    frame->desired_class = entry->desired_class;
    frame->data_len = entry->len;
    frame->epoch = state->current_epoch;
    if (entry->len != 0) {
        *data_ptr = entry->data;
    }
    return true;
}

bool p2_handle_rx(timecast_protocol_state_t *state,
                  timecast_store_t *store,
                  const p2_data_frame_t *frame,
                  const uint8_t *data)
{
    if (frame->epoch != state->current_epoch) {
        return false;
    }
    if (frame->slot_idx != state->p2.slot_idx) {
        return false;
    }
    if (frame->subslot_idx != state->p2.subslot_idx) {
        return false;
    }

    state->p2.rx_valid++;
    if (store_import(store, frame->source_node_id, data, frame->desired_class , frame->data_len)) {
        state->p2.store_updates++;
    }
    if(LOCAL_NODE_ID == 0 && frame->desired_class != state->scheduled_class[frame->source_node_id]) {
        state->scheduled_class[frame->source_node_id] = frame->desired_class;
        state->master_run_pre_next_round = true;
    }
    return true;
}

void p2_finish_subslot(timecast_protocol_state_t *state,
                       const timecast_protocol_cfg_t *cfg)
{
    uint8_t node_count;

    node_count = state->p2.node_count;

    state->p2.subslot_idx++;
    if (state->p2.subslot_idx >= node_count) {
        state->p2.subslot_idx = 0U;
        state->p2.slot_idx++;
        state->p2.tx_slot = !state->p2.tx_slot;
        if (state->p2.slot_idx >= (uint8_t)(cfg->p2_ntx * 2U)) {
            state->p2.active = false;
        }
    }
}

void prepare_round(timecast_protocol_state_t *state, timecast_store_t *store,
                   const timecast_protocol_cfg_t *cfg, bool use_pre_p2)
{
    memset(store->entries, 0, sizeof(store->entries));
    store->present_count = 0;

    if (use_pre_p2) {
        if (cfg->local_node_id == 0U) {
            state->round_run_pre = state->master_force_initial_pre ||
                                   state->master_run_pre_next_round;
            if(state->round_run_pre) {
                state->local_schedule_ver++;
            }
            state->master_force_initial_pre = false;
            state->master_run_pre_next_round = false;
        }
        else {
            state->round_run_pre = false;
        }
    }
    else {
        state->round_run_pre = false;
    }
}

uint8_t frame_len_to_class(uint8_t frame_len)
{
    uint32_t offset;
    uint32_t class_id;

    if (frame_len <= PACKET_P2_DATA_FRAME_HDR_LEN) {
        return 0U;
    }
    if (frame_len == PACKET_P2_DATA_MAX_FRAME_LEN) {
        return CLASS_MAX_ID;
    }
    if (frame_len > PACKET_P2_DATA_MAX_FRAME_LEN) {
        return CLASS_INVALID_ID;
    }

    offset = (uint32_t)frame_len - (uint32_t)PACKET_P2_DATA_FRAME_HDR_LEN;
    class_id = offset / (uint32_t)_class_span_bytes();
    if (class_id > CLASS_MAX_ID) {
        return CLASS_INVALID_ID;
    }

    return (uint8_t)class_id;
}

uint8_t class_to_frame_len(uint8_t class_id)
{
    uint32_t payload_len;

    if (class_id > CLASS_MAX_ID) {
        return 0U;
    }
    if (class_id == CLASS_MAX_ID) {
        return PACKET_P2_DATA_MAX_FRAME_LEN;
    }

    payload_len = (uint32_t)PACKET_P2_DATA_FRAME_HDR_LEN +
                  ((uint32_t)(class_id + 1U) * (uint32_t)_class_span_bytes()) - 1U;

    return (uint8_t)payload_len;
}

void master_track_p2_completeness(timecast_protocol_state_t *state,
                                  const timecast_protocol_cfg_t *cfg,
                                  uint16_t present_count, bool use_pre_p2,
                                  uint32_t incomplete_threshold)
{
    if (!use_pre_p2 || (cfg->local_node_id != 0U) ||
        (incomplete_threshold == 0U)) {
        return;
    }

    if (present_count >= cfg->p2_node_count) {

        state->master_p2_incomplete_rounds = 0U;
        return;
    }

    state->master_p2_incomplete_rounds++;
    if (state->master_p2_incomplete_rounds >= incomplete_threshold) {
        state->master_run_pre_next_round = true;
        state->master_p2_incomplete_rounds = 0U;
    }
}
