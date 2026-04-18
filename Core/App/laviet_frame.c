/**
 * @file laviet_frame.c
 * @brief LAVIET_FRAME_V1 parser, serializer and cheap validation.
 */

#include "laviet_frame.h"

#include "stm32u5xx_hal.h"

#include <string.h>

static void laviet_be16_write(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value >> 8);
    dst[1] = (uint8_t)value;
}

static void laviet_be32_write(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value >> 24);
    dst[1] = (uint8_t)(value >> 16);
    dst[2] = (uint8_t)(value >> 8);
    dst[3] = (uint8_t)value;
}

static uint16_t laviet_be16_read(const uint8_t *src)
{
    return (uint16_t)(((uint16_t)src[0] << 8) | src[1]);
}

static uint32_t laviet_be32_read(const uint8_t *src)
{
    return ((uint32_t)src[0] << 24) |
           ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8) |
           (uint32_t)src[3];
}

static uint8_t laviet_frame_version_from_ver_type(uint8_t ver_type)
{
    return (uint8_t)((ver_type >> 4) & 0x0FU);
}

static bool laviet_frame_type_known(laviet_frame_type_t type)
{
    return (type >= LAVIET_TYPE_DATA) && (type <= LAVIET_TYPE_ERROR);
}

uint8_t laviet_frame_ver_type(laviet_frame_type_t type)
{
    return (uint8_t)(((uint8_t)LAVIET_FRAME_VERSION << 4) | ((uint8_t)type & 0x0FU));
}

laviet_frame_type_t laviet_frame_type(const laviet_frame_t *frame)
{
    if (frame == NULL)
    {
        return LAVIET_TYPE_NONE;
    }

    return (laviet_frame_type_t)(frame->ver_type & 0x0FU);
}

bool laviet_frame_is_broadcast(const laviet_frame_t *frame)
{
    if (frame == NULL)
    {
        return false;
    }

    return (frame->dst_id == LAVIET_BROADCAST_ID);
}

uint16_t laviet_local_node_id(void)
{
    uint32_t uid0 = HAL_GetUIDw0();
    uint32_t uid1 = HAL_GetUIDw1();
    uint32_t uid2 = HAL_GetUIDw2();
    uint32_t hash = 2166136261UL;
    uint8_t i;
    const uint8_t *uid_bytes = (const uint8_t *)&uid0;

    for (i = 0U; i < 4U; i++)
    {
        hash ^= uid_bytes[i];
        hash *= 16777619UL;
    }
    uid_bytes = (const uint8_t *)&uid1;
    for (i = 0U; i < 4U; i++)
    {
        hash ^= uid_bytes[i];
        hash *= 16777619UL;
    }
    uid_bytes = (const uint8_t *)&uid2;
    for (i = 0U; i < 4U; i++)
    {
        hash ^= uid_bytes[i];
        hash *= 16777619UL;
    }

    hash = (hash ^ (hash >> 16)) & 0xFFFFUL;
    if ((hash == 0U) || (hash == LAVIET_GATEWAY_ID) || (hash == LAVIET_BROADCAST_ID))
    {
        hash = (uint32_t)(0x1000U | (hash & 0x0FFFU));
        if ((hash == 0U) || (hash == LAVIET_GATEWAY_ID) || (hash == LAVIET_BROADCAST_ID))
        {
            hash = 0x1002U;
        }
    }

    return (uint16_t)hash;
}

laviet_frame_status_t laviet_frame_validate_plain(const laviet_frame_t *frame)
{
    laviet_frame_type_t type;
    bool broadcast;

    if (frame == NULL)
    {
        return LAVIET_STATUS_NULL;
    }

    if (laviet_frame_version_from_ver_type(frame->ver_type) != LAVIET_FRAME_VERSION)
    {
        return LAVIET_STATUS_VERSION;
    }

    type = laviet_frame_type(frame);
    if (!laviet_frame_type_known(type))
    {
        return LAVIET_STATUS_TYPE;
    }

    if ((frame->src_id == 0U) ||
        (frame->src_id == LAVIET_BROADCAST_ID) ||
        (frame->dst_id == 0U) ||
        (frame->msg_id == 0U))
    {
        return LAVIET_STATUS_ID;
    }

    if (frame->payload_len > LAVIET_MAX_PAYLOAD)
    {
        return LAVIET_STATUS_PAYLOAD;
    }

    broadcast = (frame->dst_id == LAVIET_BROADCAST_ID);
    if (broadcast)
    {
        if (((frame->flags & LAVIET_FLAG_BROADCAST) == 0U) ||
            ((frame->flags & LAVIET_FLAG_ACK_REQUIRED) != 0U))
        {
            return LAVIET_STATUS_FLAGS;
        }
        if ((frame->src_id != LAVIET_GATEWAY_ID) &&
            (type != LAVIET_TYPE_DATA) &&
            (type != LAVIET_TYPE_RESP) &&
            (type != LAVIET_TYPE_PAIR_REQ))
        {
            return LAVIET_STATUS_FLAGS;
        }
    }
    else if ((frame->flags & LAVIET_FLAG_BROADCAST) != 0U)
    {
        return LAVIET_STATUS_FLAGS;
    }

    switch (type)
    {
        case LAVIET_TYPE_DATA:
            if ((frame->payload_len == 0U) || (frame->payload_len > LAVIET_MAX_PAYLOAD))
            {
                return LAVIET_STATUS_PAYLOAD;
            }
            if (((frame->flags & LAVIET_FLAG_IS_ACK) != 0U) ||
                ((frame->flags & LAVIET_FLAG_PAIRING) != 0U) ||
                ((frame->flags & LAVIET_FLAG_CONFIG_ACCESS) != 0U) ||
                ((frame->flags & LAVIET_FLAG_COUNTER_OVERRIDE) != 0U) ||
                ((frame->flags & LAVIET_FLAG_KEY_UPDATE) != 0U))
            {
                return LAVIET_STATUS_FLAGS;
            }
            break;

        case LAVIET_TYPE_ACK:
            if (frame->payload_len != LAVIET_ACK_PAYLOAD_LEN)
            {
                return LAVIET_STATUS_PAYLOAD;
            }
            if (((frame->flags & LAVIET_FLAG_IS_ACK) == 0U) ||
                ((frame->flags & LAVIET_FLAG_ACK_REQUIRED) != 0U) ||
                ((frame->flags & LAVIET_FLAG_ENCRYPTED) != 0U))
            {
                return LAVIET_STATUS_FLAGS;
            }
            break;

        case LAVIET_TYPE_RESP:
            if ((frame->flags & (LAVIET_FLAG_IS_ACK |
                                 LAVIET_FLAG_PAIRING |
                                 LAVIET_FLAG_CONFIG_ACCESS |
                                 LAVIET_FLAG_COUNTER_OVERRIDE |
                                 LAVIET_FLAG_KEY_UPDATE)) != 0U)
            {
                return LAVIET_STATUS_FLAGS;
            }
            break;

        case LAVIET_TYPE_PAIR_REQ:
        case LAVIET_TYPE_PAIR_RESP:
            if (frame->payload_len != LAVIET_PAIR_PAYLOAD_LEN)
            {
                return LAVIET_STATUS_PAYLOAD;
            }
            if (((frame->flags & LAVIET_FLAG_PAIRING) == 0U) ||
                ((frame->flags & LAVIET_FLAG_CONFIG_ACCESS) != 0U) ||
                ((frame->flags & LAVIET_FLAG_COUNTER_OVERRIDE) != 0U) ||
                ((frame->flags & LAVIET_FLAG_KEY_UPDATE) != 0U))
            {
                return LAVIET_STATUS_FLAGS;
            }
            break;

        case LAVIET_TYPE_CFG:
            if ((frame->payload_len < 2U) || (frame->payload_len > LAVIET_MAX_PAYLOAD))
            {
                return LAVIET_STATUS_PAYLOAD;
            }
            if (((frame->flags & LAVIET_FLAG_CONFIG_ACCESS) == 0U) ||
                ((frame->flags & LAVIET_FLAG_IS_ACK) != 0U) ||
                ((frame->flags & LAVIET_FLAG_PAIRING) != 0U))
            {
                return LAVIET_STATUS_FLAGS;
            }
            break;

        case LAVIET_TYPE_COUNTER_SYNC:
            if (frame->payload_len != LAVIET_COUNTER_SYNC_PAYLOAD_LEN)
            {
                return LAVIET_STATUS_PAYLOAD;
            }
            if (((frame->flags & LAVIET_FLAG_COUNTER_OVERRIDE) == 0U) ||
                ((frame->flags & LAVIET_FLAG_IS_ACK) != 0U) ||
                (frame->src_id != LAVIET_GATEWAY_ID))
            {
                return LAVIET_STATUS_FLAGS;
            }
            break;

        case LAVIET_TYPE_KEY_ROTATE:
            if ((frame->payload_len == 0U) || (frame->payload_len > LAVIET_MAX_PAYLOAD))
            {
                return LAVIET_STATUS_PAYLOAD;
            }
            if (((frame->flags & LAVIET_FLAG_KEY_UPDATE) == 0U) ||
                ((frame->flags & LAVIET_FLAG_IS_ACK) != 0U))
            {
                return LAVIET_STATUS_FLAGS;
            }
            break;

        case LAVIET_TYPE_ERROR:
            if (frame->payload_len != LAVIET_ERROR_PAYLOAD_LEN)
            {
                return LAVIET_STATUS_PAYLOAD;
            }
            if ((frame->flags & LAVIET_FLAG_IS_ACK) != 0U)
            {
                return LAVIET_STATUS_FLAGS;
            }
            break;

        default:
            return LAVIET_STATUS_TYPE;
    }

    return LAVIET_STATUS_OK;
}

laviet_frame_status_t laviet_frame_encode(const laviet_frame_t *frame,
                                          uint8_t *out,
                                          uint8_t out_capacity,
                                          uint8_t *out_len)
{
    laviet_frame_status_t status;
    uint8_t total_len;

    if ((frame == NULL) || (out == NULL) || (out_len == NULL))
    {
        return LAVIET_STATUS_NULL;
    }

    status = laviet_frame_validate_plain(frame);
    if (status != LAVIET_STATUS_OK)
    {
        return status;
    }

    total_len = (uint8_t)(LAVIET_FRAME_HEADER_LEN + frame->payload_len + LAVIET_MAC_TAG_LEN);
    if (out_capacity < total_len)
    {
        return LAVIET_STATUS_LENGTH;
    }

    out[0] = frame->ver_type;
    out[1] = frame->flags;
    laviet_be16_write(&out[2], frame->src_id);
    laviet_be16_write(&out[4], frame->dst_id);
    laviet_be16_write(&out[6], frame->msg_id);
    laviet_be32_write(&out[8], frame->counter);
    out[12] = frame->payload_len;
    if (frame->payload_len > 0U)
    {
        memcpy(&out[LAVIET_FRAME_HEADER_LEN], frame->payload, frame->payload_len);
    }
    memcpy(&out[LAVIET_FRAME_HEADER_LEN + frame->payload_len], frame->mac_tag, LAVIET_MAC_TAG_LEN);
    *out_len = total_len;
    return LAVIET_STATUS_OK;
}

laviet_frame_status_t laviet_frame_decode(const uint8_t *in,
                                          uint8_t in_len,
                                          laviet_frame_t *frame_out)
{
    uint8_t payload_len;
    uint8_t expected_len;
    laviet_frame_status_t status;

    if ((in == NULL) || (frame_out == NULL))
    {
        return LAVIET_STATUS_NULL;
    }
    if ((in_len < LAVIET_FRAME_MIN_LEN) || (in_len > LAVIET_FRAME_MAX_LEN))
    {
        return LAVIET_STATUS_LENGTH;
    }

    payload_len = in[12];
    if (payload_len > LAVIET_MAX_PAYLOAD)
    {
        return LAVIET_STATUS_PAYLOAD;
    }
    expected_len = (uint8_t)(LAVIET_FRAME_HEADER_LEN + payload_len + LAVIET_MAC_TAG_LEN);
    if (expected_len != in_len)
    {
        return LAVIET_STATUS_LENGTH;
    }

    memset(frame_out, 0, sizeof(*frame_out));
    frame_out->ver_type = in[0];
    frame_out->flags = in[1];
    frame_out->src_id = laviet_be16_read(&in[2]);
    frame_out->dst_id = laviet_be16_read(&in[4]);
    frame_out->msg_id = laviet_be16_read(&in[6]);
    frame_out->counter = laviet_be32_read(&in[8]);
    frame_out->payload_len = payload_len;
    if (payload_len > 0U)
    {
        memcpy(frame_out->payload, &in[LAVIET_FRAME_HEADER_LEN], payload_len);
    }
    memcpy(frame_out->mac_tag, &in[LAVIET_FRAME_HEADER_LEN + payload_len], LAVIET_MAC_TAG_LEN);

    status = laviet_frame_validate_plain(frame_out);
    if (status != LAVIET_STATUS_OK)
    {
        memset(frame_out, 0, sizeof(*frame_out));
    }
    return status;
}

bool laviet_frame_build_mac_input(const laviet_frame_t *frame,
                                  uint8_t *out,
                                  uint8_t out_capacity,
                                  uint8_t *out_len)
{
    uint8_t len;

    if ((frame == NULL) || (out == NULL) || (out_len == NULL))
    {
        return false;
    }
    if (frame->payload_len > LAVIET_MAX_PAYLOAD)
    {
        return false;
    }

    len = (uint8_t)(LAVIET_FRAME_HEADER_LEN + frame->payload_len);
    if (out_capacity < len)
    {
        return false;
    }

    out[0] = frame->ver_type;
    out[1] = frame->flags;
    laviet_be16_write(&out[2], frame->src_id);
    laviet_be16_write(&out[4], frame->dst_id);
    laviet_be16_write(&out[6], frame->msg_id);
    laviet_be32_write(&out[8], frame->counter);
    out[12] = frame->payload_len;
    if (frame->payload_len > 0U)
    {
        memcpy(&out[LAVIET_FRAME_HEADER_LEN], frame->payload, frame->payload_len);
    }
    *out_len = len;
    return true;
}

bool laviet_frame_write_ack_payload(uint16_t acked_msg_id,
                                    uint32_t acked_counter,
                                    uint8_t out[LAVIET_ACK_PAYLOAD_LEN])
{
    if ((out == NULL) || (acked_msg_id == 0U))
    {
        return false;
    }

    laviet_be16_write(&out[0], acked_msg_id);
    laviet_be32_write(&out[2], acked_counter);
    return true;
}

bool laviet_frame_read_ack_payload(const uint8_t payload[LAVIET_ACK_PAYLOAD_LEN],
                                   uint16_t *acked_msg_id,
                                   uint32_t *acked_counter)
{
    if ((payload == NULL) || (acked_msg_id == NULL) || (acked_counter == NULL))
    {
        return false;
    }

    *acked_msg_id = laviet_be16_read(&payload[0]);
    *acked_counter = laviet_be32_read(&payload[2]);
    return (*acked_msg_id != 0U);
}
