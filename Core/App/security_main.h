/**
 * @file security_main.h
 * @brief Security domain task and command API.
 */

#ifndef APP_SECURITY_MAIN_H_
#define APP_SECURITY_MAIN_H_

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    bool in_use;
    uint8_t slot;
    uint32_t node_id;
    uint8_t code_len;
    uint8_t code[8];
} trusted_info_t;

typedef enum
{
    SECURITY_NOTIFY_POPUP = 0,
    SECURITY_NOTIFY_BADGE = 1
} security_notify_mode_t;

typedef struct
{
    bool coding_enabled;
    bool fh_enabled;
    bool auto_ping_enabled;
    security_notify_mode_t notify_mode;
    uint8_t lora_preset;
} security_runtime_cfg_t;

void security_main_create_task(void);

bool security_main_cmd_add_device(uint32_t node_id, const uint8_t *code, uint8_t len);
bool security_main_cmd_delete_device(uint32_t node_id);
bool security_main_cmd_get_device(uint8_t idx, trusted_info_t *out);
bool security_main_cmd_set_coding(bool enabled);
bool security_main_cmd_rotate_key(void);
bool security_main_cmd_set_fh(bool enabled);

bool security_main_cmd_set_notify_mode(security_notify_mode_t mode);
bool security_main_cmd_set_lora_preset(uint8_t preset_id);
bool security_main_cmd_set_auto_ping(bool enabled);
bool security_main_cmd_get_runtime_cfg(security_runtime_cfg_t *cfg_out);

bool security_main_get_network_key(uint8_t key_out[16]);
bool security_main_log_message(int16_t rssi_dbm, const uint8_t *payload, uint8_t payload_len);
bool security_main_get_tpm_ready(bool *ready_out);

#endif /* APP_SECURITY_MAIN_H_ */
