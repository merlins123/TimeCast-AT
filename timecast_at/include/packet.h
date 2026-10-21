/*
 * SPDX-FileCopyrightText: 2026 Xin He
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#ifndef PACKET_H
#define PACKET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "store.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PACKET_TYPE_P1_SYNC (0x01U)
#define PACKET_TYPE_P2_DATA (0x04U)

#define PACKET_P1_RELAY_CNT_MASK    (0x7FU)
#define PACKET_P1_RUN_PRE_WIRE_BIT  (0x80U)

typedef struct __attribute__((packed)) {
    uint8_t packet_type;
    uint8_t relay_cnt;
    uint8_t flags;
    uint8_t schedule_ver;
    uint32_t epoch;
} p1_sync_frame_t;

typedef struct __attribute__((packed)) {
    uint8_t packet_type;
    uint8_t source_node_id;
    uint8_t slot_idx;
    uint8_t subslot_idx;
    uint8_t desired_class;
    uint8_t data_len;
    uint32_t epoch;
} p2_data_frame_t;

enum {
    PACKET_P1_SYNC_FRAME_LEN        = sizeof(p1_sync_frame_t) - 1U,
    PACKET_P1_SYNC_MAX_FRAME_LEN    =
        PACKET_P1_SYNC_FRAME_LEN + (TIMECAST_STORE_MAX_NODES + 1U) / 2U,
    PACKET_P2_DATA_FRAME_HDR_LEN    = sizeof(p2_data_frame_t),
    PACKET_P2_DATA_MAX_DATA_LEN     = TIMECAST_STORE_MAX_DATA_LEN,
    PACKET_P2_DATA_MAX_FRAME_LEN    =
        PACKET_P2_DATA_FRAME_HDR_LEN + PACKET_P2_DATA_MAX_DATA_LEN
};

size_t encode_p1_sync(uint8_t *dst,
                      const p1_sync_frame_t *frame,
                      const uint8_t *scheduled_class,
                      uint8_t node_count);
bool decode_p1_sync(const uint8_t *frame_buf,
                    p1_sync_frame_t *frame);
void encode_p2_data(uint8_t *dst,
                    const p2_data_frame_t *frame,
                    const uint8_t *data);
bool decode_p2_data(const uint8_t *frame_buf,
                    p2_data_frame_t *frame,
                    uint8_t *data_out);

#ifdef __cplusplus
}
#endif

#endif /* PACKET_H */
