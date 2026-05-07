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

/** @brief Wire format version encoded in the high nibble of `ver_type`. */
#define LAVIET_FRAME_VERSION            1U
/** @brief Maximum payload bytes carried by one LAVIET frame. */
#define LAVIET_MAX_PAYLOAD              16U
/** @brief Full HMAC-SHA256 tag length stored on the wire. */
#define LAVIET_MAC_TAG_LEN              32U
/** @brief Serialized header length before payload and MAC. */
#define LAVIET_FRAME_HEADER_LEN         13U
/** @brief Minimum serialized frame length. */
#define LAVIET_FRAME_MIN_LEN            (LAVIET_FRAME_HEADER_LEN + LAVIET_MAC_TAG_LEN)
/** @brief Maximum serialized frame length. */
#define LAVIET_FRAME_MAX_LEN            (LAVIET_FRAME_HEADER_LEN + LAVIET_MAX_PAYLOAD + LAVIET_MAC_TAG_LEN)

/** @brief Reserved node identifier of the gateway. */
#define LAVIET_GATEWAY_ID               0x0001U
/** @brief Broadcast destination identifier. */
#define LAVIET_BROADCAST_ID             0xFFFFU

/** @brief Payload is encrypted with the current frame key. */
#define LAVIET_FLAG_ENCRYPTED           (1U << 0)
/** @brief Sender requests an ACK frame. */
#define LAVIET_FLAG_ACK_REQUIRED        (1U << 1)
/** @brief Frame itself is an ACK. */
#define LAVIET_FLAG_IS_ACK              (1U << 2)
/** @brief Frame belongs to pairing flow. */
#define LAVIET_FLAG_PAIRING             (1U << 3)
/** @brief Frame contains configuration/control payload. */
#define LAVIET_FLAG_CONFIG_ACCESS       (1U << 4)
/** @brief Destination is the broadcast address. */
#define LAVIET_FLAG_BROADCAST           (1U << 5)
/** @brief Payload carries a counter synchronization override. */
#define LAVIET_FLAG_COUNTER_OVERRIDE    (1U << 6)
/** @brief Payload carries key rotation material. */
#define LAVIET_FLAG_KEY_UPDATE          (1U << 7)

/** @brief Fixed payload length of pair request/response frames. */
#define LAVIET_PAIR_PAYLOAD_LEN         8U
/** @brief Fixed payload length of ACK frames. */
#define LAVIET_ACK_PAYLOAD_LEN          6U
/** @brief Fixed payload length of counter synchronization frames. */
#define LAVIET_COUNTER_SYNC_PAYLOAD_LEN 4U
/** @brief Fixed payload length reserved for error frames. */
#define LAVIET_ERROR_PAYLOAD_LEN        8U

/**
 * @brief LAVIET frame type stored in the low nibble of `ver_type`.
 */
typedef enum
{
    LAVIET_TYPE_NONE = 0x00U, /**< Invalid/empty type sentinel. */
    LAVIET_TYPE_DATA = 0x01U, /**< User/application payload. */
    LAVIET_TYPE_ACK = 0x02U, /**< Acknowledgement payload. */
    LAVIET_TYPE_RESP = 0x03U, /**< Response/status payload. */
    LAVIET_TYPE_PAIR_REQ = 0x04U, /**< Pairing request. */
    LAVIET_TYPE_PAIR_RESP = 0x05U, /**< Pairing response. */
    LAVIET_TYPE_CFG = 0x06U, /**< Configuration/control payload. */
    LAVIET_TYPE_COUNTER_SYNC = 0x07U, /**< Gateway counter synchronization. */
    LAVIET_TYPE_KEY_ROTATE = 0x08U, /**< Key rotation payload. */
    LAVIET_TYPE_ERROR = 0x09U /**< Error/status payload. */
} laviet_frame_type_t;

/**
 * @brief Validation, encode and decode result codes.
 */
typedef enum
{
    LAVIET_STATUS_OK = 0, /**< Operation completed successfully. */
    LAVIET_STATUS_NULL, /**< Null pointer argument. */
    LAVIET_STATUS_LENGTH, /**< Invalid serialized length or buffer capacity. */
    LAVIET_STATUS_VERSION, /**< Unsupported frame version. */
    LAVIET_STATUS_TYPE, /**< Unknown or invalid frame type. */
    LAVIET_STATUS_ID, /**< Invalid source, destination or message identifier. */
    LAVIET_STATUS_FLAGS, /**< Flag combination is invalid for the frame type. */
    LAVIET_STATUS_PAYLOAD /**< Payload length or shape is invalid. */
} laviet_frame_status_t;

/**
 * @brief Parsed LAVIET frame representation.
 */
typedef struct
{
    uint8_t ver_type; /**< Version/type byte: high nibble version, low nibble type. */
    uint8_t flags; /**< Bitmask of `LAVIET_FLAG_*` values. */
    uint16_t src_id; /**< Source node identifier. */
    uint16_t dst_id; /**< Destination node identifier. */
    uint16_t msg_id; /**< Message identifier used by ACK tracking. */
    uint32_t counter; /**< Monotonic frame counter. */
    uint8_t payload_len; /**< Number of valid bytes in `payload`. */
    uint8_t payload[LAVIET_MAX_PAYLOAD]; /**< Frame payload buffer. */
    uint8_t mac_tag[LAVIET_MAC_TAG_LEN]; /**< Authentication tag stored on the wire. */
} laviet_frame_t;

/**
 * @brief Compose the version/type byte for a frame type.
 * @param type LAVIET frame type.
 * @return Encoded `ver_type` byte.
 */
uint8_t laviet_frame_ver_type(laviet_frame_type_t type);

/**
 * @brief Extract the low-nibble frame type from a parsed frame.
 * @param frame Frame object.
 * @return Frame type or `LAVIET_TYPE_NONE` for a null frame.
 */
laviet_frame_type_t laviet_frame_type(const laviet_frame_t *frame);

/**
 * @brief Check whether a frame targets the broadcast address.
 * @param frame Frame object.
 * @return `true` when `dst_id` equals `LAVIET_BROADCAST_ID`.
 */
bool laviet_frame_is_broadcast(const laviet_frame_t *frame);

/**
 * @brief Derive a stable local node identifier from STM32 unique ID words.
 * @return Non-reserved 16-bit node identifier.
 */
uint16_t laviet_local_node_id(void);

/**
 * @brief Validate parsed frame fields before encryption/MAC checks.
 * @param frame Frame object.
 * @return Validation status.
 */
laviet_frame_status_t laviet_frame_validate_plain(const laviet_frame_t *frame);

/**
 * @brief Serialize a parsed frame into wire format.
 * @param frame Frame to serialize.
 * @param out Output buffer.
 * @param out_capacity Output buffer capacity.
 * @param out_len [out] Serialized frame length.
 * @return Encode status.
 */
laviet_frame_status_t laviet_frame_encode(const laviet_frame_t *frame,
                                          uint8_t *out,
                                          uint8_t out_capacity,
                                          uint8_t *out_len);

/**
 * @brief Parse a serialized wire frame into a frame structure.
 * @param in Serialized frame bytes.
 * @param in_len Serialized frame length.
 * @param frame_out [out] Parsed frame.
 * @return Decode status.
 */
laviet_frame_status_t laviet_frame_decode(const uint8_t *in,
                                          uint8_t in_len,
                                          laviet_frame_t *frame_out);

/**
 * @brief Build the canonical byte sequence covered by the frame MAC.
 * @param frame Frame whose header and payload are used.
 * @param out Output buffer.
 * @param out_capacity Output buffer capacity.
 * @param out_len [out] MAC input length.
 * @return `true` when the MAC input was built.
 */
bool laviet_frame_build_mac_input(const laviet_frame_t *frame,
                                  uint8_t *out,
                                  uint8_t out_capacity,
                                  uint8_t *out_len);

/**
 * @brief Encode an ACK payload.
 * @param acked_msg_id Message identifier being acknowledged.
 * @param acked_counter Counter value being acknowledged.
 * @param out [out] Fixed-size ACK payload buffer.
 * @return `true` when the payload was written.
 */
bool laviet_frame_write_ack_payload(uint16_t acked_msg_id,
                                    uint32_t acked_counter,
                                    uint8_t out[LAVIET_ACK_PAYLOAD_LEN]);

/**
 * @brief Decode an ACK payload.
 * @param payload ACK payload bytes.
 * @param acked_msg_id [out] Decoded message identifier.
 * @param acked_counter [out] Decoded counter value.
 * @return `true` when the payload was decoded.
 */
bool laviet_frame_read_ack_payload(const uint8_t payload[LAVIET_ACK_PAYLOAD_LEN],
                                   uint16_t *acked_msg_id,
                                   uint32_t *acked_counter);

#ifdef __cplusplus
}
#endif

#endif /* APP_LAVIET_FRAME_H_ */
