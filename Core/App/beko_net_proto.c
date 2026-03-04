/**
 * @file beko_net_proto.c
 * @brief BEKO system message protocol (BEKO_NET_V1) helpers.
 */

#include "beko_net_proto.h"

#include "stm32u5xx_hal.h"

#include <string.h>

#define BEKO_NET_HEADER_SIZE  20U
#define BEKO_NET_MIN_FRAME    (BEKO_NET_HEADER_SIZE + 2U)

static void beko_net_be16_write(uint8_t *dst, uint16_t v)
{
    dst[0] = (uint8_t)(v >> 8);
    dst[1] = (uint8_t)v;
}

static void beko_net_be32_write(uint8_t *dst, uint32_t v)
{
    dst[0] = (uint8_t)(v >> 24);
    dst[1] = (uint8_t)(v >> 16);
    dst[2] = (uint8_t)(v >> 8);
    dst[3] = (uint8_t)v;
}

static uint16_t beko_net_be16_read(const uint8_t *src)
{
    return (uint16_t)(((uint16_t)src[0] << 8) | src[1]);
}

static uint32_t beko_net_be32_read(const uint8_t *src)
{
    return ((uint32_t)src[0] << 24) |
           ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8) |
           (uint32_t)src[3];
}

static void beko_net_xtea_encrypt_block(uint32_t v[2], const uint32_t key[4])
{
    uint32_t i;
    uint32_t sum = 0U;
    const uint32_t delta = 0x9E3779B9UL;
    uint32_t v0 = v[0];
    uint32_t v1 = v[1];

    for (i = 0U; i < 32U; i++)
    {
        v0 += ((((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + key[sum & 3U]));
        sum += delta;
        v1 += ((((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + key[(sum >> 11) & 3U]));
    }

    v[0] = v0;
    v[1] = v1;
}

uint16_t beko_net_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFU;
    uint16_t i;
    uint8_t j;

    if ((data == NULL) && (len > 0U))
    {
        return 0U;
    }

    for (i = 0U; i < len; i++)
    {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (j = 0U; j < 8U; j++)
        {
            if ((crc & 0x8000U) != 0U)
            {
                crc = (uint16_t)((crc << 1) ^ 0x1021U);
            }
            else
            {
                crc <<= 1;
            }
        }
    }

    return crc;
}

uint32_t beko_net_local_node_id(void)
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

    if (hash == 0U)
    {
        hash = 1U;
    }

    return hash;
}

bool beko_net_encode(const beko_net_frame_t *frame,
                     uint8_t *out,
                     uint16_t out_capacity,
                     uint16_t *out_len)
{
    uint16_t total_len;
    uint16_t crc;

    if ((frame == NULL) || (out == NULL) || (out_len == NULL))
    {
        return false;
    }
    if (frame->payload_len > BEKO_NET_MAX_PAYLOAD)
    {
        return false;
    }

    total_len = (uint16_t)(BEKO_NET_HEADER_SIZE + frame->payload_len + 2U);
    if (total_len > out_capacity)
    {
        return false;
    }

    out[0] = BEKO_NET_MAGIC0;
    out[1] = BEKO_NET_MAGIC1;
    out[2] = BEKO_NET_VERSION;
    out[3] = frame->type;
    out[4] = frame->flags;
    out[5] = frame->ttl;
    beko_net_be32_write(&out[6], frame->src_id);
    beko_net_be32_write(&out[10], frame->dst_id);
    beko_net_be32_write(&out[14], frame->msg_id);
    beko_net_be16_write(&out[18], frame->payload_len);
    if (frame->payload_len > 0U)
    {
        memcpy(&out[20], frame->payload, frame->payload_len);
    }

    crc = beko_net_crc16(out, (uint16_t)(BEKO_NET_HEADER_SIZE + frame->payload_len));
    beko_net_be16_write(&out[BEKO_NET_HEADER_SIZE + frame->payload_len], crc);
    *out_len = total_len;
    return true;
}

bool beko_net_decode(const uint8_t *in,
                     uint16_t in_len,
                     beko_net_frame_t *frame_out)
{
    uint16_t payload_len;
    uint16_t expected_len;
    uint16_t crc_stored;
    uint16_t crc_calc;

    if ((in == NULL) || (frame_out == NULL))
    {
        return false;
    }
    if (in_len < BEKO_NET_MIN_FRAME)
    {
        return false;
    }
    if ((in[0] != BEKO_NET_MAGIC0) || (in[1] != BEKO_NET_MAGIC1) || (in[2] != BEKO_NET_VERSION))
    {
        return false;
    }

    payload_len = beko_net_be16_read(&in[18]);
    if (payload_len > BEKO_NET_MAX_PAYLOAD)
    {
        return false;
    }

    expected_len = (uint16_t)(BEKO_NET_HEADER_SIZE + payload_len + 2U);
    if (expected_len != in_len)
    {
        return false;
    }

    crc_stored = beko_net_be16_read(&in[BEKO_NET_HEADER_SIZE + payload_len]);
    crc_calc = beko_net_crc16(in, (uint16_t)(BEKO_NET_HEADER_SIZE + payload_len));
    if (crc_stored != crc_calc)
    {
        return false;
    }

    memset(frame_out, 0, sizeof(*frame_out));
    frame_out->type = in[3];
    frame_out->flags = in[4];
    frame_out->ttl = in[5];
    frame_out->src_id = beko_net_be32_read(&in[6]);
    frame_out->dst_id = beko_net_be32_read(&in[10]);
    frame_out->msg_id = beko_net_be32_read(&in[14]);
    frame_out->payload_len = payload_len;
    if (payload_len > 0U)
    {
        memcpy(frame_out->payload, &in[20], payload_len);
    }

    return true;
}

void beko_net_xtea_ctr_crypt(uint8_t *data,
                             uint16_t len,
                             const uint8_t key[16],
                             uint32_t nonce)
{
    uint32_t key_words[4];
    uint16_t offset = 0U;
    uint32_t counter = 0U;

    if ((data == NULL) || (key == NULL))
    {
        return;
    }

    key_words[0] = beko_net_be32_read(&key[0]);
    key_words[1] = beko_net_be32_read(&key[4]);
    key_words[2] = beko_net_be32_read(&key[8]);
    key_words[3] = beko_net_be32_read(&key[12]);

    while (offset < len)
    {
        uint8_t ks[8];
        uint8_t i;
        uint32_t blk[2];

        blk[0] = nonce;
        blk[1] = counter;
        beko_net_xtea_encrypt_block(blk, key_words);
        beko_net_be32_write(&ks[0], blk[0]);
        beko_net_be32_write(&ks[4], blk[1]);

        for (i = 0U; (i < 8U) && (offset < len); i++)
        {
            data[offset] ^= ks[i];
            offset++;
        }
        counter++;
    }
}

bool beko_net_is_for_node(const beko_net_frame_t *frame, uint32_t self_node_id)
{
    if (frame == NULL)
    {
        return false;
    }

    return ((frame->dst_id == self_node_id) || (frame->dst_id == BEKO_NET_BROADCAST_ID));
}

bool beko_net_should_forward(const beko_net_frame_t *frame, uint32_t self_node_id)
{
    if (frame == NULL)
    {
        return false;
    }
    if (frame->src_id == self_node_id)
    {
        return false;
    }
    if (beko_net_is_for_node(frame, self_node_id))
    {
        return false;
    }
    if (frame->ttl <= 1U)
    {
        return false;
    }

    return true;
}

void beko_net_dedup_init(beko_net_dedup_cache_t *cache, uint32_t window_ms)
{
    if (cache == NULL)
    {
        return;
    }

    memset(cache, 0, sizeof(*cache));
    cache->window_ms = window_ms;
}

bool beko_net_dedup_seen_or_add(beko_net_dedup_cache_t *cache,
                                uint32_t src_id,
                                uint32_t msg_id,
                                uint32_t now_ms)
{
    uint8_t i;
    uint8_t free_idx = 0xFFU;
    uint8_t oldest_idx = 0U;
    uint32_t oldest_age = 0U;

    if (cache == NULL)
    {
        return false;
    }

    for (i = 0U; i < BEKO_NET_DEDUP_CAPACITY; i++)
    {
        if (!cache->entries[i].used)
        {
            if (free_idx == 0xFFU)
            {
                free_idx = i;
            }
            continue;
        }

        if ((cache->entries[i].src_id == src_id) && (cache->entries[i].msg_id == msg_id))
        {
            if ((now_ms - cache->entries[i].timestamp_ms) <= cache->window_ms)
            {
                return true;
            }
            cache->entries[i].timestamp_ms = now_ms;
            return false;
        }

        if ((now_ms - cache->entries[i].timestamp_ms) > oldest_age)
        {
            oldest_age = (now_ms - cache->entries[i].timestamp_ms);
            oldest_idx = i;
        }
    }

    if (free_idx == 0xFFU)
    {
        free_idx = oldest_idx;
    }

    cache->entries[free_idx].used = true;
    cache->entries[free_idx].src_id = src_id;
    cache->entries[free_idx].msg_id = msg_id;
    cache->entries[free_idx].timestamp_ms = now_ms;
    return false;
}
