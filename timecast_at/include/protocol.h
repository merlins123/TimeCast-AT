/*
 * SPDX-FileCopyrightText: 2026 Xin He
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#ifndef TIMECAST_PROTOCOL_H
#define TIMECAST_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#include "packet.h"
#include "store.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CLASS_COUNT      (16U)
#define CLASS_MAX_ID     (CLASS_COUNT - 1U)
#define CLASS_INVALID_ID (CLASS_COUNT)
#if (TEST_NODE2_CLASS_MAX > CLASS_MAX_ID)
#  error "TEST_NODE2_CLASS_MAX exceeds class range"
#endif
#if (TEST_NODE2_CLASS_MIN > TEST_NODE2_CLASS_MAX)
#  error "TEST_NODE2_CLASS_MIN exceeds TEST_NODE2_CLASS_MAX"
#endif

typedef struct {
    bool active;
    bool flag_tx;
    bool has_tref;
    uint8_t slot_idx;
    uint8_t ntx_done;
    uint8_t local_hop;
    uint8_t schedule_ver;
    uint32_t tref_local_ticks;
    uint32_t p2_phase_start_local_ticks;
} timecast_protocol_p1_t;

typedef struct {
    bool active;
    bool tx_slot;
    uint8_t node_count;
    uint8_t slot_idx;
    uint8_t subslot_idx;
    uint32_t start_local_ticks;
    uint32_t slot_ticks;
    uint32_t rx_valid;
    uint32_t store_updates;
    uint32_t subslot_ticks[TIMECAST_STORE_MAX_NODES];
    uint32_t subslot_offset_ticks[TIMECAST_STORE_MAX_NODES];
} timecast_protocol_p2_t;

typedef struct {
    bool joined;
    bool have_schedule;
    uint32_t current_epoch;
    uint32_t rx_valid;
    timecast_protocol_p1_t p1;
    timecast_protocol_p2_t p2;
    uint8_t local_schedule_ver;
    uint8_t scheduled_class[TIMECAST_STORE_MAX_NODES];
    bool round_run_pre;
    bool master_run_pre_next_round;
    bool master_force_initial_pre;
    uint32_t master_p2_incomplete_rounds;
} timecast_protocol_state_t;

typedef struct {
    uint8_t local_node_id;
    uint8_t ntx;
    uint8_t p2_ntx;
    uint8_t p2_node_count;
    uint32_t glossy_slot_ticks;
    uint32_t packed_schedule_ticks;
    uint32_t p1_rx_ts_to_slot_start_ticks;
    uint32_t p1_guard_ticks;
    uint32_t p2_subslot_ticks;
    uint32_t p2_guard_ticks;
    uint32_t p2_rx_window_ticks;
    uint32_t p2_payload_base_ticks;
    uint32_t p2_payload_byte_ticks;
} timecast_protocol_cfg_t;

void protocol_init(timecast_protocol_state_t *state, bool initiator);
void p1_start(timecast_protocol_state_t *state,
              uint32_t start_local_ticks,
              const timecast_protocol_cfg_t *cfg,
              bool initiator,
              uint32_t epoch);
uint32_t p1_get_slot_start_local_ticks(const timecast_protocol_state_t *state,
                                       const timecast_protocol_cfg_t *cfg);
void p1_prepare_tx(timecast_protocol_state_t *state,
                   p1_sync_frame_t *frame);
void p1_handle_rx(timecast_protocol_state_t *state,
                  const p1_sync_frame_t *frame,
                  const uint8_t *frame_buf,
                  uint32_t rx_local_ticks,
                  const timecast_protocol_cfg_t *cfg);
void p1_finish_slot(timecast_protocol_state_t *state,
                    const timecast_protocol_cfg_t *cfg,
                    bool did_tx);
void p2_start_original(timecast_protocol_state_t *state,
                       uint32_t start_local_ticks,
                       const timecast_protocol_cfg_t *cfg);
void p2_start_pre_p2(timecast_protocol_state_t *state,
                     uint32_t start_local_ticks,
                     const timecast_protocol_cfg_t *cfg);
uint32_t p2_get_subslot_start_local_ticks(const timecast_protocol_state_t *state);
uint32_t p2_get_slot_ticks(const timecast_protocol_state_t *state,
                           const timecast_protocol_cfg_t *cfg);
bool p2_prepare_tx(const timecast_protocol_state_t *state,
                   const timecast_store_t *store,
                   p2_data_frame_t *frame,
                   const uint8_t **data_ptr);
bool p2_handle_rx(timecast_protocol_state_t *state,
                  timecast_store_t *store,
                  const p2_data_frame_t *frame,
                  const uint8_t *data);
void p2_finish_subslot(timecast_protocol_state_t *state,
                       const timecast_protocol_cfg_t *cfg);
void prepare_round(timecast_protocol_state_t *state, timecast_store_t *store,
                   const timecast_protocol_cfg_t *cfg, bool use_pre_p2);
uint8_t frame_len_to_class(uint8_t payload_len);
uint8_t class_to_frame_len(uint8_t class_id);
void master_track_p2_completeness(timecast_protocol_state_t *state,
                                  const timecast_protocol_cfg_t *cfg,
                                  uint16_t present_count, bool use_pre_p2,
                                  uint32_t incomplete_threshold);
#ifdef __cplusplus
}
#endif

#endif /* TIMECAST_PROTOCOL_H */
