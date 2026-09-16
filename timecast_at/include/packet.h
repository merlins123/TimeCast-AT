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
#define PACKET_TYPE_PRE_COL (0x02U)
#define PACKET_TYPE_PRE_COM (0x03U)
#define PACKET_TYPE_P2_DATA (0x04U)

#define PACKET_P1_RELAY_CNT_MASK    (0x7FU)
#define PACKET_P1_RUN_PRE_WIRE_BIT  (0x80U)
#define PACKET_P2_SOURCE_ID_MASK    (0x7FU)
#define PACKET_P2_UPDATE_WIRE_BIT   (0x80U)

#if (TIMECAST_STORE_MAX_NODES > PACKET_P2_SOURCE_ID_MASK)
#error "TIMECAST_STORE_MAX_NODES exceeds 7-bit packed source_node_id budget"
#endif

typedef struct __attribute__((packed)) {
    uint8_t packet_type;
    uint8_t relay_cnt;
    uint8_t flags;
    uint32_t epoch;
} p1_sync_frame_t;

typedef struct __attribute__((packed)) {
    uint8_t packet_type;
    uint8_t class_id;
} pre_collect_frame_t;

typedef struct __attribute__((packed)) {
    uint8_t packet_type;
    uint8_t packed_schedule[(TIMECAST_STORE_MAX_NODES + 1U) / 2U];
} pre_commit_frame_t;

typedef struct __attribute__((packed)) {
    uint8_t packet_type;
    uint8_t source_node_id;
    uint8_t slot_idx;
    uint8_t subslot_idx;
    uint8_t flags;
    uint8_t data_len;
    uint32_t epoch;
} p2_data_frame_t;

enum {
    PACKET_P1_SYNC_FRAME_LEN              = sizeof(p1_sync_frame_t) - 1U,
    PACKET_P2_DATA_FRAME_HDR_LEN          = sizeof(p2_data_frame_t) - 1U,
    PACKET_P2_DATA_MAX_DATA_LEN         = TIMECAST_STORE_MAX_DATA_LEN,
    PACKET_P2_DATA_MAX_FRAME_LEN      =
        PACKET_P2_DATA_FRAME_HDR_LEN + PACKET_P2_DATA_MAX_DATA_LEN,
    PACKET_PRE_P2_CTRL_FRAME_LEN          = sizeof(pre_collect_frame_t),
    PACKET_PRE_COMMIT_MAX_FRAME_LEN  = sizeof(pre_commit_frame_t)
};

void encode_p1_sync(uint8_t *dst,
                    const p1_sync_frame_t *frame);
bool decode_p1_sync(const uint8_t *frame_buf,
                    p1_sync_frame_t *frame);
void encode_p2_data(uint8_t *dst,
                    const p2_data_frame_t *frame,
                    const uint8_t *data);
bool decode_p2_data(const uint8_t *frame_buf,
                    p2_data_frame_t *frame,
                    uint8_t *data_out);
void encode_pre_p2(uint8_t *dst, const pre_collect_frame_t *frame);
bool decode_pre_p2(const uint8_t *frame_buf,
                   pre_collect_frame_t *frame);
void encode_pre_commit(uint8_t *dst,
                       pre_commit_frame_t *frame,
                       size_t packed_len);
bool decode_pre_commit(const uint8_t *frame_buf,
                       uint8_t node_count,
                       pre_commit_frame_t *frame,
                       size_t *packed_len_out);

#ifdef __cplusplus
}
#endif

#endif
