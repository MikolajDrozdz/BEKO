/**
 * @file security_main.h
 * @brief Security domain task and command API.
 */

#ifndef APP_SECURITY_MAIN_H_
#define APP_SECURITY_MAIN_H_

#include "radio_main.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Public descriptor of one trusted pairing entry.
 */
typedef struct
{
    bool in_use; /**< Slot contains a valid trusted entry. */
    bool is_master; /**< Entry represents the gateway/master slot. */
    uint8_t slot; /**< Logical trusted-entry slot index. */
    uint32_t node_id; /**< Trusted node identifier. */
    uint8_t code_len; /**< Number of valid bytes in `code`. */
    uint8_t code[8]; /**< Pairing/service code bytes. */
} trusted_info_t;

/**
 * @brief UI delivery mode for security notifications.
 */
typedef enum
{
    SECURITY_NOTIFY_POPUP = 0, /**< Show notifications as popups. */
    SECURITY_NOTIFY_BADGE = 1 /**< Prefer compact badge/status notifications. */
} security_notify_mode_t;

/**
 * @brief Key derivation mode used for LAVIET frame encryption and MAC keys.
 */
typedef enum
{
    SECURITY_FRAME_KEY_MODE_SHARED = 0, /**< Use the shared frame root key. */
    SECURITY_FRAME_KEY_MODE_PAIR_V1_32 = 1 /**< Derive keys from a 32-bit pair code. */
} security_frame_key_mode_t;

/**
 * @brief Persistent and runtime security settings snapshot.
 */
typedef struct
{
    bool coding_enabled; /**< Protected/coded traffic setting. */
    bool fh_enabled; /**< Frequency hopping setting. */
    uint32_t fh_period_ms; /**< Frequency hopping period in milliseconds. */
    bool auto_ping_enabled; /**< Automatic ping setting. */
    uint32_t auto_ping_period_ms; /**< Automatic ping period in milliseconds. */
    radio_main_auto_ping_mode_t auto_ping_mode; /**< Automatic ping payload mode. */
    security_notify_mode_t notify_mode; /**< Security notification display mode. */
    uint8_t lora_preset; /**< Selected LoRa preset. */
    radio_main_modulation_t active_modulation; /**< Active modulation family. */
    radio_lora_cfg_t lora; /**< Persisted LoRa profile. */
    radio_main_fsk_cfg_t fsk; /**< Persisted FSK profile. */
    radio_main_ook_cfg_t ook; /**< Persisted OOK profile. */
    bool radio_profiles_persisted; /**< `true` when radio profiles were loaded from store. */
} security_runtime_cfg_t;

/**
 * @brief Create the security task and command queue.
 */
void security_main_create_task(void);

/**
 * @brief Add or update a trusted device entry.
 * @param node_id Trusted node identifier.
 * @param code Pairing code bytes.
 * @param len Pairing code length.
 * @return `true` when the command completed successfully.
 */
bool security_main_cmd_add_device(uint32_t node_id, const uint8_t *code, uint8_t len);

/**
 * @brief Add or update the gateway/master trusted entry.
 * @param node_id Gateway node identifier.
 * @param code Pairing code bytes.
 * @param len Pairing code length.
 * @return `true` when the command completed successfully.
 */
bool security_main_cmd_add_gateway(uint32_t node_id, const uint8_t *code, uint8_t len);

/**
 * @brief Delete a trusted device entry by node identifier.
 * @param node_id Node identifier to remove.
 * @return `true` when the command completed successfully.
 */
bool security_main_cmd_delete_device(uint32_t node_id);

/**
 * @brief Read one trusted device entry.
 * @param idx Logical trusted-entry index.
 * @param out [out] Destination descriptor.
 * @return `true` when a descriptor was copied.
 */
bool security_main_cmd_get_device(uint8_t idx, trusted_info_t *out);

/**
 * @brief Persist and apply protected/coded traffic setting.
 * @param enabled Desired coding state.
 * @return `true` when the command completed successfully.
 */
bool security_main_cmd_set_coding(bool enabled);

/**
 * @brief Rotate the current network key seed.
 * @return `true` when key rotation completed successfully.
 */
bool security_main_cmd_rotate_key(void);

/**
 * @brief Persist and apply frequency hopping setting.
 * @param enabled Desired FH state.
 * @return `true` when the command completed successfully.
 */
bool security_main_cmd_set_fh(bool enabled);

/**
 * @brief Persist the frequency hopping period.
 * @param period_ms Period in milliseconds.
 * @return `true` when the command completed successfully.
 */
bool security_main_cmd_set_fh_period(uint32_t period_ms);

/**
 * @brief Persist the preferred security notification mode.
 * @param mode Notification display mode.
 * @return `true` when the command completed successfully.
 */
bool security_main_cmd_set_notify_mode(security_notify_mode_t mode);

/**
 * @brief Persist the selected LoRa preset.
 * @param preset_id Preset index.
 * @return `true` when the command completed successfully.
 */
bool security_main_cmd_set_lora_preset(uint8_t preset_id);

/**
 * @brief Persist and apply automatic ping enable state.
 * @param enabled Desired auto-ping state.
 * @return `true` when the command completed successfully.
 */
bool security_main_cmd_set_auto_ping(bool enabled);

/**
 * @brief Persist the automatic ping period.
 * @param period_ms Period in milliseconds.
 * @return `true` when the command completed successfully.
 */
bool security_main_cmd_set_auto_ping_period(uint32_t period_ms);

/**
 * @brief Persist the automatic ping payload mode.
 * @param mode Payload mode.
 * @return `true` when the command completed successfully.
 */
bool security_main_cmd_set_auto_ping_mode(radio_main_auto_ping_mode_t mode);

/**
 * @brief Persist a full radio runtime configuration snapshot.
 * @param cfg Runtime radio snapshot.
 * @return `true` when the command completed successfully.
 */
bool security_main_cmd_set_radio_runtime_cfg(const radio_main_runtime_cfg_t *cfg);

/**
 * @brief Read the current security runtime configuration.
 * @param cfg_out [out] Destination configuration snapshot.
 * @return `true` when the snapshot was copied.
 */
bool security_main_cmd_get_runtime_cfg(security_runtime_cfg_t *cfg_out);

/**
 * @brief Copy the active 128-bit network key.
 * @param key_out [out] Destination key buffer.
 * @return `true` when the key was copied.
 */
bool security_main_get_network_key(uint8_t key_out[16]);

/**
 * @brief Derive a peer link key for two node identifiers.
 * @param local_node_id Local node identifier.
 * @param peer_node_id Peer node identifier.
 * @param key_out [out] Destination 128-bit key.
 * @return `true` when the key was derived.
 */
bool security_main_get_peer_link_key(uint32_t local_node_id, uint32_t peer_node_id, uint8_t key_out[16]);

/**
 * @brief Derive frame encryption and HMAC keys using a selected mode.
 * @param local_id Local frame node identifier.
 * @param peer_id Peer frame node identifier.
 * @param mode Key derivation mode.
 * @param enc_key_out [out] 128-bit encryption key.
 * @param hmac_key_out [out] 256-bit HMAC key.
 * @return `true` when both keys were derived.
 */
bool security_main_get_frame_keys_mode(uint16_t local_id,
                                       uint16_t peer_id,
                                       security_frame_key_mode_t mode,
                                       uint8_t enc_key_out[16],
                                       uint8_t hmac_key_out[32]);

/**
 * @brief Derive frame keys from an explicit pairing code.
 * @param local_id Local frame node identifier.
 * @param peer_id Peer frame node identifier.
 * @param mode Key derivation mode.
 * @param code Pairing code bytes.
 * @param code_len Pairing code length.
 * @param enc_key_out [out] 128-bit encryption key.
 * @param hmac_key_out [out] 256-bit HMAC key.
 * @return `true` when both keys were derived.
 */
bool security_main_get_frame_keys_for_code(uint16_t local_id,
                                           uint16_t peer_id,
                                           security_frame_key_mode_t mode,
                                           const uint8_t *code,
                                           uint8_t code_len,
                                           uint8_t enc_key_out[16],
                                           uint8_t hmac_key_out[32]);

/**
 * @brief Derive frame keys using either shared or pair-link material.
 * @param local_id Local frame node identifier.
 * @param peer_id Peer frame node identifier.
 * @param use_pair_link Use peer link material instead of the shared root mode.
 * @param enc_key_out [out] 128-bit encryption key.
 * @param hmac_key_out [out] 256-bit HMAC key.
 * @return `true` when both keys were derived.
 */
bool security_main_get_frame_keys(uint16_t local_id,
                                  uint16_t peer_id,
                                  bool use_pair_link,
                                  uint8_t enc_key_out[16],
                                  uint8_t hmac_key_out[32]);

/**
 * @brief Read persisted gateway RX/TX counters.
 * @param rx_counter_out [out] Last RX counter.
 * @param tx_counter_out [out] Last TX counter.
 * @return `true` when counters were copied.
 */
bool security_main_get_gateway_counter(uint32_t *rx_counter_out, uint32_t *tx_counter_out);

/**
 * @brief Persist gateway RX/TX counters after successful frame processing.
 * @param rx_counter RX counter to store.
 * @param tx_counter TX counter to store.
 * @return `true` when counters were stored.
 */
bool security_main_commit_gateway_counter(uint32_t rx_counter, uint32_t tx_counter);

/**
 * @brief Append a received message to the protected message log.
 * @param rssi_dbm RSSI attached to the payload.
 * @param payload Payload bytes.
 * @param payload_len Payload length.
 * @return `true` when the log operation succeeded.
 */
bool security_main_log_message(int16_t rssi_dbm, const uint8_t *payload, uint8_t payload_len);

/**
 * @brief Read whether TPM-backed services are ready.
 * @param ready_out [out] TPM readiness flag.
 * @return `true` when the flag was copied.
 */
bool security_main_get_tpm_ready(bool *ready_out);

#endif /* APP_SECURITY_MAIN_H_ */
