/**
 * @file laviet_frame.h
 * @brief LAVIET_FRAME_V1 wire protocol helpers.
 */

#ifndef APP_LAVIET_FRAME_H_
#define APP_LAVIET_FRAME_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LAVIET_FRAME_VERSION            1U
#define LAVIET_MAX_PAYLOAD              16U
#define LAVIET_MAC_TAG_LEN              32U
#define LAVIET_FRAME_HEADER_LEN         13U
#define LAVIET_FRAME_MIN_LEN            (LAVIET_FRAME_HEADER_LEN + LAVIET_MAC_TAG_LEN)
#define LAVIET_FRAME_MAX_LEN            (LAVIET_FRAME_HEADER_LEN + LAVIET_MAX_PAYLOAD + LAVIET_MAC_TAG_LEN)

#define LAVIET_GATEWAY_ID               0x0001U
#define LAVIET_BROADCAST_ID             0xFFFFU

#define LAVIET_FLAG_ENCRYPTED           (1U << 0)
#define LAVIET_FLAG_ACK_REQUIRED        (1U << 1)
#define LAVIET_FLAG_IS_ACK              (1U << 2)
#define LAVIET_FLAG_PAIRING             (1U << 3)
#define LAVIET_FLAG_CONFIG_ACCESS       (1U << 4)
#define LAVIET_FLAG_BROADCAST           (1U << 5)
#define LAVIET_FLAG_COUNTER_OVERRIDE    (1U << 6)
#define LAVIET_FLAG_KEY_UPDATE          (1U << 7)

#define LAVIET_PAIR_PAYLOAD_LEN         8U
#define LAVIET_ACK_PAYLOAD_LEN          6U
#define LAVIET_COUNTER_SYNC_PAYLOAD_LEN 4U
#define LAVIET_ERROR_PAYLOAD_LEN        8U

typedef enum
{
    LAVIET_TYPE_NONE = 0x00U,
    LAVIET_TYPE_DATA = 0x01U,
    LAVIET_TYPE_ACK = 0x02U,
    LAVIET_TYPE_RESP = 0x03U,
    LAVIET_TYPE_PAIR_REQ = 0x04U,
    LAVIET_TYPE_PAIR_RESP = 0x05U,
    LAVIET_TYPE_CFG = 0x06U,
    LAVIET_TYPE_COUNTER_SYNC = 0x07U,
    LAVIET_TYPE_KEY_ROTATE = 0x08U,
    LAVIET_TYPE_ERROR = 0x09U
} laviet_frame_type_t;

typedef enum
{
    LAVIET_STATUS_OK = 0,
    LAVIET_STATUS_NULL,
    LAVIET_STATUS_LENGTH,
    LAVIET_STATUS_VERSION,
    LAVIET_STATUS_TYPE,
    LAVIET_STATUS_ID,
    LAVIET_STATUS_FLAGS,
    LAVIET_STATUS_PAYLOAD
} laviet_frame_status_t;

typedef struct
{
    uint8_t ver_type;
    uint8_t flags;
    uint16_t src_id;
    uint16_t dst_id;
    uint16_t msg_id;
    uint32_t counter;
    uint8_t payload_len;
    uint8_t payload[LAVIET_MAX_PAYLOAD];
    uint8_t mac_tag[LAVIET_MAC_TAG_LEN];
} laviet_frame_t;

uint8_t laviet_frame_ver_type(laviet_frame_type_t type);
laviet_frame_type_t laviet_frame_type(const laviet_frame_t *frame);
bool laviet_frame_is_broadcast(const laviet_frame_t *frame);

uint16_t laviet_local_node_id(void);

laviet_frame_status_t laviet_frame_validate_plain(const laviet_frame_t *frame);
laviet_frame_status_t laviet_frame_encode(const laviet_frame_t *frame,
                                          uint8_t *out,
                                          uint8_t out_capacity,
                                          uint8_t *out_len);
laviet_frame_status_t laviet_frame_decode(const uint8_t *in,
                                          uint8_t in_len,
                                          laviet_frame_t *frame_out);

bool laviet_frame_build_mac_input(const laviet_frame_t *frame,
                                  uint8_t *out,
                                  uint8_t out_capacity,
                                  uint8_t *out_len);

bool laviet_frame_write_ack_payload(uint16_t acked_msg_id,
                                    uint32_t acked_counter,
                                    uint8_t out[LAVIET_ACK_PAYLOAD_LEN]);
bool laviet_frame_read_ack_payload(const uint8_t payload[LAVIET_ACK_PAYLOAD_LEN],
                                   uint16_t *acked_msg_id,
                                   uint32_t *acked_counter);

#ifdef __cplusplus
}
#endif

#endif /* APP_LAVIET_FRAME_H_ */
