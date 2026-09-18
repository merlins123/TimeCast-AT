/*
 * SPDX-FileCopyrightText: 2026 Xin He
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#include <string.h>

#include "packet.h"

static void _u32_to_le(uint8_t *dst, uint32_t v)
{
    dst[0] = (uint8_t)(v & 0xFFU);
    dst[1] = (uint8_t)((v >> 8) & 0xFFU);
    dst[2] = (uint8_t)((v >> 16) & 0xFFU);
    dst[3] = (uint8_t)((v >> 24) & 0xFFU);
}

static uint32_t _u32_from_le(const uint8_t *src)
{
    return ((uint32_t)src[0]) |
           ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) |
           ((uint32_t)src[3] << 24);
}

void encode_p1_sync(uint8_t *dst,
                    const p1_sync_frame_t *frame)
{
    dst[0] = frame->packet_type;
    dst[1] = (uint8_t)(frame->relay_cnt & PACKET_P1_RELAY_CNT_MASK);
    if ((frame->flags & 1U) != 0U) {
        dst[1] |= PACKET_P1_RUN_PRE_WIRE_BIT;
    }
    _u32_to_le(&dst[2], frame->epoch);
}

bool decode_p1_sync(const uint8_t *frame_buf,
                    p1_sync_frame_t *frame)
{
    frame->packet_type = frame_buf[0];
    if (frame->packet_type != PACKET_TYPE_P1_SYNC) {
        return false;
    }

    frame->relay_cnt = (uint8_t)(frame_buf[1] & PACKET_P1_RELAY_CNT_MASK);
    frame->flags = ((frame_buf[1] & PACKET_P1_RUN_PRE_WIRE_BIT) != 0U) ?
                   1U : 0U;
    frame->epoch = _u32_from_le(&frame_buf[2]);
    return true;
}

void encode_p2_data(uint8_t *dst,
                    const p2_data_frame_t *frame,
                    const uint8_t *data)
{
    dst[0] = frame->packet_type;
    dst[1] = (uint8_t)(frame->source_node_id & PACKET_P2_SOURCE_ID_MASK);
    if ((frame->flags & 1U) != 0U) {
        dst[1] |= PACKET_P2_UPDATE_WIRE_BIT;
    }
    dst[2] = frame->slot_idx;
    dst[3] = frame->subslot_idx;
    dst[4] = frame->data_len;
    _u32_to_le(&dst[5], frame->epoch);
    if (frame->data_len > 0U) {
        memcpy(&dst[PACKET_P2_DATA_FRAME_HDR_LEN], data, frame->data_len);
    }
}

bool decode_p2_data(const uint8_t *frame_buf,
                    p2_data_frame_t *frame,
                    uint8_t *data_out)
{
    frame->packet_type = frame_buf[0];
    if (frame->packet_type != PACKET_TYPE_P2_DATA) {
        return false;
    }
    frame->source_node_id = (uint8_t)(frame_buf[1] & PACKET_P2_SOURCE_ID_MASK);
    frame->slot_idx = frame_buf[2];
    frame->subslot_idx = frame_buf[3];
    frame->flags = ((frame_buf[1] & PACKET_P2_UPDATE_WIRE_BIT) != 0U) ?
                   1U : 0U;
    frame->data_len = frame_buf[4];
    if (frame->data_len > PACKET_P2_DATA_MAX_DATA_LEN) {
        return false;
    }
    frame->epoch = _u32_from_le(&frame_buf[5]);
    if (data_out && (frame->data_len > 0U)) {
        memcpy(data_out, &frame_buf[PACKET_P2_DATA_FRAME_HDR_LEN], frame->data_len);
    }
    return true;
}

void encode_pre_p2(uint8_t *dst, const pre_collect_frame_t *frame)
{
    dst[0] = frame->packet_type;
    dst[1] = frame->class_id;
}

bool decode_pre_p2(const uint8_t *frame_buf, pre_collect_frame_t *frame)
{
    frame->packet_type = frame_buf[0];
    if (frame->packet_type != PACKET_TYPE_PRE_COL) {
        return false;
    }
    frame->class_id = frame_buf[1];
    if (frame->class_id >= 16) {
        return false;
    }
    return true;
}

void encode_pre_commit(uint8_t *dst,
                       pre_commit_frame_t *frame,
                       size_t packed_len)
{
    dst[0] = frame->packet_type;
    memcpy(&dst[1], frame->packed_schedule, packed_len);
}

bool decode_pre_commit(const uint8_t *frame_buf,
                       uint8_t node_count,
                       pre_commit_frame_t *frame,
                       size_t *packed_len_out)
{
    size_t packed_len;

    frame->packet_type = frame_buf[0];
    if (frame->packet_type != PACKET_TYPE_PRE_COM) {
        return false;
    }
    packed_len = (size_t)((node_count + 1U) / 2U);

    memcpy(frame->packed_schedule, frame_buf + 1, packed_len);
    *packed_len_out = packed_len;

    return true;
}
