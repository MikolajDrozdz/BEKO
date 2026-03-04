/**
 * @file beko_net_proto.h
 * @brief BEKO system message protocol (BEKO_NET_V1) helpers.
 */

#ifndef APP_BEKO_NET_PROTO_H_
#define APP_BEKO_NET_PROTO_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BEKO_NET_MAGIC0                 ((uint8_t)'B')
#define BEKO_NET_MAGIC1                 ((uint8_t)'K')
#define BEKO_NET_VERSION                1U

#define BEKO_NET_DEFAULT_TTL            3U
#define BEKO_NET_BROADCAST_ID           0xFFFFFFFFUL
#define BEKO_NET_MAX_PAYLOAD            200U

#define BEKO_NET_FLAG_CODED             (1U << 0)

#define BEKO_NET_DEDUP_CAPACITY         32U

typedef enum
{
    BEKO_NET_TYPE_USER = 0x01U,
    BEKO_NET_TYPE_JOIN_REQ = 0x10U,
    BEKO_NET_TYPE_JOIN_ACCEPT = 0x11U,
    BEKO_NET_TYPE_JOIN_REJECT = 0x12U,
    BEKO_NET_TYPE_ACK = 0x20U
} beko_net_type_t;

typedef struct
{
    uint8_t type;
    uint8_t flags;
    uint8_t ttl;
    uint32_t src_id;
    uint32_t dst_id;
    uint32_t msg_id;
    uint16_t payload_len;
    uint8_t payload[BEKO_NET_MAX_PAYLOAD];
} beko_net_frame_t;

typedef struct
{
    bool used;
    uint32_t src_id;
    uint32_t msg_id;
    uint32_t timestamp_ms;
} beko_net_dedup_entry_t;

typedef struct
{
    beko_net_dedup_entry_t entries[BEKO_NET_DEDUP_CAPACITY];
    uint32_t window_ms;
} beko_net_dedup_cache_t;

uint16_t beko_net_crc16(const uint8_t *data, uint16_t len);

uint32_t beko_net_local_node_id(void);

bool beko_net_encode(const beko_net_frame_t *frame,
                     uint8_t *out,
                     uint16_t out_capacity,
                     uint16_t *out_len);

bool beko_net_decode(const uint8_t *in,
                     uint16_t in_len,
                     beko_net_frame_t *frame_out);

void beko_net_xtea_ctr_crypt(uint8_t *data,
                             uint16_t len,
                             const uint8_t key[16],
                             uint32_t nonce);

bool beko_net_is_for_node(const beko_net_frame_t *frame, uint32_t self_node_id);

bool beko_net_should_forward(const beko_net_frame_t *frame, uint32_t self_node_id);

void beko_net_dedup_init(beko_net_dedup_cache_t *cache, uint32_t window_ms);

bool beko_net_dedup_seen_or_add(beko_net_dedup_cache_t *cache,
                                uint32_t src_id,
                                uint32_t msg_id,
                                uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* APP_BEKO_NET_PROTO_H_ */
