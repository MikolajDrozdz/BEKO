#include "radio_main.h"

#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "laviet_crypto.h"
#include "laviet_frame.h"
#include "lcd_main.h"
#include "menu_main.h"
#include "radio_lib/radio_lib.h"
#include "radio_lib/common/sx1276/radio_sx1276_regs.h"
#include "security_main.h"
#include "task.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define RADIO_TASK_STACK_SIZE                6144U
#define RADIO_TASK_STACK_WORDS               (RADIO_TASK_STACK_SIZE / sizeof(StackType_t))
#define RADIO_CMD_QUEUE_DEPTH                16U
#define RADIO_CMD_WAIT_MS                    1500U
#define RADIO_CMD_POLL_MS                    5U
#define RADIO_MSG_BUF_MAX                    255U
#define RADIO_HOP_PERIOD_DEFAULT_MS          10000UL
#define RADIO_PAIR_CODE_LEN                  LAVIET_PAIR_PAYLOAD_LEN
#define RADIO_RECOVERY_RESET_PULSE_MS        2U
#define RADIO_RECOVERY_RESET_BOOT_MS         10U
#define RADIO_RECOVERY_RETRY_COUNT           2U
#define RADIO_TX_GUARD_MIN_MS                200UL
#define RADIO_TX_GUARD_LORA_MS               20000UL
#define RADIO_TX_GUARD_MARGIN_MS             64UL
#define RADIO_ACK_TIMEOUT_MIN_MS             6000UL
#define RADIO_ACK_TIMEOUT_MARGIN_MS          1500UL
#define RADIO_ACK_ACTIVE_POLL_MS             1U
#define RADIO_ACK_TX_DELAY_MS                100UL
#define RADIO_IDLE_POLL_MS                   10U
#define RADIO_ACK_CLOSED_TTL_MS              10000UL
#define RADIO_ACK_RETRY_LIMIT                2U
#define RADIO_AUTO_PING_MIN_PERIOD_MS        250UL
#define RADIO_AUTO_PING_PERIOD_DEFAULT_MS    1000UL
#define RADIO_AUTO_PING_RAW_LEN              4U
#define RADIO_NETWORK_PAIR_RSSI_MIN_DBM      (-20)
#define RADIO_TRUSTED_DEVICE_SLOTS           16U
#define RADIO_BCAST_CTRL_MAGIC               0xB7U
#define RADIO_BCAST_CTRL_INSTALL_FRAGMENT    1U
#define RADIO_BCAST_CTRL_ACTIVATE            2U
#define RADIO_BCAST_GROUP_KEY_LEN            16U
#define RADIO_BCAST_GROUP_FRAGMENT_LEN       8U
#define RADIO_BCAST_GROUP_FRAGMENT_COUNT     2U
#define RADIO_LORA_BW_HZ_7_8                 7800UL
#define RADIO_LORA_BW_HZ_10_4                10400UL
#define RADIO_LORA_BW_HZ_15_6                15600UL
#define RADIO_LORA_BW_HZ_20_8                20800UL
#define RADIO_LORA_BW_HZ_31_25               31250UL
#define RADIO_LORA_BW_HZ_41_7                41700UL
#define RADIO_LORA_BW_HZ_62_5                62500UL
#define RADIO_LORA_BW_HZ_125                 125000UL
#define RADIO_LORA_BW_HZ_250                 250000UL
#define RADIO_LORA_BW_HZ_500                 500000UL

typedef enum
{
    RADIO_MAIN_CMD_NONE = 0,
    RADIO_MAIN_CMD_SEND_TEMPLATE,
    RADIO_MAIN_CMD_SEND_USER_TEXT,
    RADIO_MAIN_CMD_SEND_RAW,
    RADIO_MAIN_CMD_SET_PRESET,
    RADIO_MAIN_CMD_SET_MODULATION,
    RADIO_MAIN_CMD_SET_MOD_FREQ,
    RADIO_MAIN_CMD_SET_MOD_BW,
    RADIO_MAIN_CMD_SET_OPTION,
    RADIO_MAIN_CMD_SET_FH,
    RADIO_MAIN_CMD_SET_FH_PERIOD,
    RADIO_MAIN_CMD_SET_CODING,
    RADIO_MAIN_CMD_SET_AUTO_PING,
    RADIO_MAIN_CMD_SET_AUTO_PING_PERIOD,
    RADIO_MAIN_CMD_SET_AUTO_PING_MODE,
    RADIO_MAIN_CMD_RESET_MODULE,
    RADIO_MAIN_CMD_START_PAIRING,
    RADIO_MAIN_CMD_START_NETWORK_PAIRING,
    RADIO_MAIN_CMD_PAIRING_ACCEPT,
    RADIO_MAIN_CMD_SEND_PAIR_REQ,
    RADIO_MAIN_CMD_SEND_NETWORK_PAIR_REQ,
    RADIO_MAIN_CMD_SEND_PAIR_ERROR
} radio_main_cmd_id_t;

typedef struct
{
    volatile bool done;
    bool result;
    radio_main_runtime_cfg_t runtime_cfg;
} radio_main_cmd_sync_t;

typedef struct
{
    radio_main_cmd_id_t id;
    radio_main_cmd_sync_t *sync;
    union
    {
        struct
        {
            uint8_t group_id;
            uint8_t msg_id;
            uint32_t dst_id;
        } send_template;
        struct
        {
            uint32_t dst_id;
            uint8_t len;
            char text[21];
        } send_text;
        struct
        {
            const uint8_t *data;
            uint8_t len;
        } send_raw;
        struct
        {
            uint8_t value;
        } set_u8;
        struct
        {
            uint32_t value;
        } set_u32;
        struct
        {
            radio_main_option_t option;
            uint32_t value;
        } set_option;
        struct
        {
            bool enabled;
        } set_bool;
        struct
        {
            uint32_t timeout_ms;
            bool network_mode;
        } pairing;
        struct
        {
            uint32_t dst_id;
        } to_node;
    } u;
} radio_main_cmd_t;

typedef struct
{
    radio_hw_cfg_t hw;
    radio_lora_cfg_t backend_cfg;
    radio_lora_cfg_t lora_cfg;
    radio_main_modulation_t modulation_id;
    radio_main_modulation_t backend_modulation_id;
    bool initialized;
    bool fh_enabled;
    uint32_t fh_period_ms;
    bool coding_enabled;
    bool auto_ping_enabled;
    uint32_t auto_ping_period_ms;
    radio_main_auto_ping_mode_t auto_ping_mode;
    bool crypto_ready;
    uint8_t lora_preset;
    radio_main_fsk_cfg_t fsk_cfg;
    radio_main_ook_cfg_t ook_cfg;
    uint8_t hop_idx;
    uint32_t last_hop_ms;
    uint32_t last_ping_ms;
    uint16_t node_id;
    uint16_t next_msg_id;
    uint32_t gateway_rx_counter;
    uint32_t gateway_tx_counter;
    bool gateway_key_mode_known;
    security_frame_key_mode_t gateway_key_mode;
    bool gateway_pair_code_valid;
    uint8_t gateway_pair_code_len;
    uint8_t gateway_pair_code[8];
    bool broadcast_group_key_valid;
    uint32_t broadcast_group_epoch;
    uint8_t broadcast_group_aes_key[16];
    uint8_t broadcast_group_hmac_key[32];
    bool broadcast_group_pending_valid;
    uint32_t broadcast_group_pending_epoch;
    uint8_t broadcast_group_pending_mask;
    uint8_t broadcast_group_pending_key[RADIO_BCAST_GROUP_KEY_LEN];
    bool pairing_active;
    bool pairing_network_mode;
    uint32_t pairing_until_ms;
    bool pairing_pending;
    uint32_t pairing_pending_node;
    uint8_t pairing_pending_code[8];
    uint8_t pairing_pending_code_len;
    bool pairing_pending_network;
    bool pairing_outgoing_pending;
    uint8_t pairing_outgoing_code[8];
    uint8_t pairing_outgoing_code_len;
    bool pairing_outgoing_network;
    bool tx_in_progress;
    bool tx_silent;
    uint32_t tx_deadline_ms;
    char last_error_text[21];
    struct
    {
        bool active;
        uint16_t peer_id;
        uint16_t msg_id;
        uint32_t counter;
        uint8_t raw_len;
        uint8_t raw[LAVIET_FRAME_MAX_LEN];
        uint8_t retries_done;
        uint32_t deadline_ms;
        uint32_t wait_ms;
    } ack_pending;
    struct
    {
        bool valid;
        bool timed_out;
        uint16_t peer_id;
        uint16_t msg_id;
        uint32_t counter;
        uint32_t expires_ms;
    } ack_recent;
} radio_main_ctx_t;

static osThreadId_t s_radio_task = NULL;
static osMessageQueueId_t s_radio_cmd_queue = NULL;
static osMutexId_t s_radio_state_mutex = NULL;
static StaticTask_t s_radio_task_cb;
static StackType_t s_radio_task_stack[RADIO_TASK_STACK_WORDS];
static radio_main_ctx_t s_ctx;

extern SPI_HandleTypeDef hspi1;

static void radio_main_task_fn(void *argument);
static bool radio_main_enqueue_sync(const radio_main_cmd_t *cmd, radio_main_cmd_sync_t *sync);
static bool radio_main_wait_sync(radio_main_cmd_sync_t *sync, uint32_t timeout_ms);
static bool radio_main_radio_init_and_start(void);
static void radio_main_apply_preset_cfg(uint8_t preset_id, radio_lora_cfg_t *cfg);
static void radio_main_load_default_profiles(void);
static void radio_main_apply_modulation_cfg(void);
static bool radio_main_switch_backend(radio_main_modulation_t modulation);
static void radio_main_load_default_profile(radio_main_modulation_t modulation);
static void radio_main_sync_snapshot(radio_main_runtime_cfg_t *cfg);
static bool radio_main_apply_option(radio_main_option_t option, uint32_t value);
static radio_packet_crc_t radio_main_map_fsk_crc(radio_main_crc_type_t crc_type);
static radio_address_filter_t radio_main_map_fsk_address_filter(radio_main_address_filter_t filter);
static radio_ook_threshold_t radio_main_map_ook_threshold(radio_main_ook_threshold_t threshold);
static bool radio_main_push_backend_cfg(void);
static bool radio_main_validate_frequency(uint32_t frequency_hz);
static bool radio_main_validate_tx_power(int32_t tx_power_dbm);
static bool radio_main_validate_bitrate(uint32_t bitrate_bps);
static bool radio_main_validate_preamble(uint32_t preamble_len);
static bool radio_main_is_supported_bw(uint8_t bw_code);
static bool radio_main_validate_hop_period(uint32_t period_ms);
static bool radio_main_validate_auto_ping_period(uint32_t period_ms);
static bool radio_main_validate_auto_ping_mode(radio_main_auto_ping_mode_t mode);
static bool radio_main_reconfigure_radio(void);
static bool radio_main_reset_module_internal(void);
static bool radio_main_force_recover_radio(const char *reason);
static bool radio_main_send_system_frame(laviet_frame_type_t type,
                                         uint32_t dst_id,
                                         const uint8_t *payload,
                                         uint16_t payload_len);
static bool radio_main_send_system_frame_ex(laviet_frame_type_t type,
                                            uint32_t dst_id,
                                            const uint8_t *payload,
                                            uint16_t payload_len,
                                            bool request_ack,
                                            bool silent);
static bool radio_main_send_ack(const laviet_frame_t *frame);
static bool radio_main_send_current_backend_with_retry_ex(const uint8_t *data, uint8_t len, bool silent);
static bool radio_main_send_raw_with_retry(const uint8_t *data, uint8_t len);
static bool radio_main_send_raw_with_retry_ex(const uint8_t *data, uint8_t len, bool silent);
static bool radio_main_send_template_internal(uint8_t group_id, uint8_t msg_id, uint32_t dst_id);
static void radio_main_post_rx_notification(int16_t rssi_dbm,
                                            uint16_t src_id,
                                            uint16_t dst_id,
                                            const uint8_t *payload,
                                            uint8_t payload_len,
                                            laviet_frame_type_t frame_type);
static uint32_t radio_main_ack_timeout_ms(uint8_t raw_len);
static bool radio_main_ack_track_start(const laviet_frame_t *frame, const uint8_t *raw, uint8_t raw_len);
static void radio_main_ack_track_close(bool timed_out);
static void radio_main_ack_recent_expire(void);
static void radio_main_handle_ack_frame(uint16_t src_id, uint16_t acked_msg_id, uint32_t acked_counter);
static void radio_main_handle_ack_timeout(void);
static void radio_main_handle_events(void);
static void radio_main_handle_rx_packet(const radio_packet_t *pkt);
static bool radio_main_source_is_trusted(uint16_t src_id);
static bool radio_main_source_is_allowed_for_data(const laviet_frame_t *frame);
static void radio_main_handle_hopping(void);
static void radio_main_handle_auto_ping(void);
static void radio_main_ensure_rx_continuous(void);
static void radio_main_watchdog_tx(void);
static void radio_main_notify(menu_notification_type_t type, const char *text);
static void radio_main_clear_gateway_pair_code(void);
static void radio_main_set_gateway_pair_code(const uint8_t *code, uint8_t len);
static void radio_main_load_gateway_pair_code_from_security(void);
static bool radio_main_get_gateway_cached_frame_keys(security_frame_key_mode_t mode,
                                                     uint8_t enc_key_out[16],
                                                     uint8_t hmac_key_out[32]);
static void radio_main_clear_broadcast_group_state(void);
static uint32_t radio_main_be32_read(const uint8_t *src);
static bool radio_main_derive_broadcast_group_frame_keys(const uint8_t group_key[RADIO_BCAST_GROUP_KEY_LEN],
                                                         uint32_t epoch,
                                                         uint8_t enc_key_out[16],
                                                         uint8_t hmac_key_out[32]);
static bool radio_main_try_broadcast_group_rx(const laviet_frame_t *frame, uint8_t enc_key[16]);
static bool radio_main_handle_key_rotate_frame(const laviet_frame_t *frame);
static uint8_t radio_main_format_payload_text(const uint8_t *payload,
                                              uint8_t payload_len,
                                              char *out,
                                              uint8_t out_size);
static bool radio_main_push_payload_to_monitor(int16_t rssi_dbm,
                                               uint16_t src_id,
                                               const uint8_t *payload,
                                               uint8_t payload_len);
static void radio_main_log_gateway_rx_frame(const laviet_frame_t *raw_frame,
                                            const laviet_frame_t *decoded_frame,
                                            int16_t rssi_dbm,
                                            int8_t snr_db);
static const char *radio_main_frame_type_text(laviet_frame_type_t type);
static const char *radio_main_key_mode_text(security_frame_key_mode_t mode);
static void radio_main_print_rx_ascii(const uint8_t *data, uint8_t len);
static void radio_main_print_tx_ascii(const uint8_t *data, uint8_t len);
static void radio_main_print_hex_bytes(const char *label, const uint8_t *data, uint16_t len);
static void radio_main_log_hmac_debug(const char *prefix,
                                      const laviet_frame_t *frame,
                                      security_frame_key_mode_t mode,
                                      const uint8_t *pair_code,
                                      uint8_t pair_code_len,
                                      const uint8_t hmac_key[32],
                                      const uint8_t expected_mac[LAVIET_MAC_TAG_LEN]);
static void radio_main_print_generated_pattern(const char *label, uint8_t value, uint16_t len);
static void radio_main_print_fsk_sync_word(uint64_t sync_word, uint8_t sync_len, const char *label);
static void radio_main_print_ook_sync_word(uint32_t sync_word, uint8_t sync_len, const char *label);
static void radio_main_print_tx_frame(const uint8_t *data, uint8_t len, bool retry_attempt);
static uint32_t radio_main_frf_to_hz(uint8_t msb, uint8_t mid, uint8_t lsb);
static void radio_main_log_runtime_profile(const char *reason);
static void radio_main_log_hw_registers(const char *reason);
static bool radio_main_finish_pairing(bool accept);
static bool radio_main_send_pair_request_internal(void);
static bool radio_main_pair_payload_parse(const uint8_t *payload,
                                          uint8_t payload_len,
                                          const uint8_t **code_out,
                                          uint8_t *code_len_out);
static uint8_t radio_main_pair_payload_build(bool network_mode,
                                             const uint8_t *code,
                                             uint8_t code_len,
                                             uint8_t *payload_out,
                                             uint8_t payload_capacity);
static bool radio_main_laviet_verify_rx(const laviet_frame_t *frame, uint8_t enc_key[16]);
static void radio_main_make_pair_code(uint8_t *code_out, uint8_t len);
static void radio_main_pair_code_to_text(const uint8_t *code, uint8_t len, char *out, uint8_t out_size);
static uint32_t radio_main_now_ms(void);
static uint32_t radio_main_tx_timeout_ms(uint16_t payload_len);
static void radio_main_tx_mark_started_ex(uint16_t payload_len, bool silent);
static void radio_main_tx_clear(void);
static bool radio_main_tx_timed_out(void);
static void radio_main_set_last_error(const char *text);
static void radio_main_clear_last_error(void);
static uint32_t radio_main_current_frequency_hz(void);
static uint16_t radio_main_current_duty_cycle_permille(uint32_t frequency_hz);
static uint32_t radio_main_lora_bw_hz(radio_lora_bw_t bw);
static uint32_t radio_main_auto_ping_airtime_ms(void);
static uint32_t radio_main_estimate_lora_airtime_ms(uint16_t payload_len);
static uint32_t radio_main_estimate_fsk_ook_airtime_ms(uint16_t payload_len, bool ook_mode);
static uint32_t radio_main_auto_ping_period_ms(void);

static const uint32_t s_hop_channels_hz[3] =
{
    868100000UL,
    868300000UL,
    868500000UL
};

static const char *s_template_groups[3][3] =
{
    { "ALR:FIRE", "ALR:INTRUSION", "ALR:LOWBATT" },
    { "STS:OK", "STS:BUSY", "STS:IDLE" },
    { "SRV:PING", "SRV:RESET", "SRV:SYNC" }
};
static const uint8_t s_auto_ping_raw[RADIO_AUTO_PING_RAW_LEN] =
{
    (uint8_t)'P', (uint8_t)'I', (uint8_t)'N', (uint8_t)'G'
};

static const osThreadAttr_t s_radio_task_attr =
{
    .name = "radio_task",
    .priority = (osPriority_t)osPriorityNormal,
    .stack_mem = s_radio_task_stack,
    .stack_size = sizeof(s_radio_task_stack),
    .cb_mem = &s_radio_task_cb,
    .cb_size = sizeof(s_radio_task_cb)
};

void radio_main_create_task(void)
{
    if (s_radio_state_mutex == NULL)
    {
        s_radio_state_mutex = osMutexNew(NULL);
        if (s_radio_state_mutex == NULL)
        {
            printf("RADIO: state mutex create failed\r\n");
            return;
        }
    }

    if (s_radio_cmd_queue == NULL)
    {
        s_radio_cmd_queue = osMessageQueueNew(RADIO_CMD_QUEUE_DEPTH, sizeof(radio_main_cmd_t), NULL);
        if (s_radio_cmd_queue == NULL)
        {
            printf("RADIO: cmd queue create failed\r\n");
            return;
        }
    }

    if (s_radio_task == NULL)
    {
        s_radio_task = osThreadNew(radio_main_task_fn, NULL, &s_radio_task_attr);
        if (s_radio_task == NULL)
        {
            printf("RADIO: task create failed\r\n");
        }
    }
}

bool radio_main_cmd_send_template(uint8_t group_id, uint8_t msg_id, uint32_t dst_id)
{
    radio_main_cmd_t cmd;
    radio_main_cmd_sync_t sync;

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = RADIO_MAIN_CMD_SEND_TEMPLATE;
    cmd.u.send_template.group_id = group_id;
    cmd.u.send_template.msg_id = msg_id;
    cmd.u.send_template.dst_id = dst_id;

    return radio_main_enqueue_sync(&cmd, &sync);
}

bool radio_main_cmd_send_user_text(const char *text, uint32_t dst_id)
{
    radio_main_cmd_t cmd;
    radio_main_cmd_sync_t sync;
    size_t len;

    if (text == NULL)
    {
        return false;
    }

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = RADIO_MAIN_CMD_SEND_USER_TEXT;
    cmd.u.send_text.dst_id = dst_id;
    len = strlen(text);
    if ((len == 0U) || (len > LAVIET_MAX_PAYLOAD))
    {
        return false;
    }
    cmd.u.send_text.len = (uint8_t)len;
    memcpy(cmd.u.send_text.text, text, len);
    cmd.u.send_text.text[len] = '\0';

    return radio_main_enqueue_sync(&cmd, &sync);
}

bool radio_main_cmd_send_raw(const uint8_t *data, uint8_t len)
{
    radio_main_cmd_t cmd;
    radio_main_cmd_sync_t sync;

    if ((data == NULL) || (len == 0U))
    {
        return false;
    }

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = RADIO_MAIN_CMD_SEND_RAW;
    cmd.u.send_raw.data = data;
    cmd.u.send_raw.len = len;

    return radio_main_enqueue_sync(&cmd, &sync);
}

bool radio_main_cmd_set_lora_preset(uint8_t preset_id)
{
    radio_main_cmd_t cmd;
    radio_main_cmd_sync_t sync;

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = RADIO_MAIN_CMD_SET_PRESET;
    cmd.u.set_u8.value = preset_id;

    return radio_main_enqueue_sync(&cmd, &sync);
}

bool radio_main_cmd_set_modulation(uint8_t modulation_id)
{
    radio_main_cmd_t cmd;
    radio_main_cmd_sync_t sync;

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = RADIO_MAIN_CMD_SET_MODULATION;
    cmd.u.set_u8.value = modulation_id;

    return radio_main_enqueue_sync(&cmd, &sync);
}

bool radio_main_cmd_set_modulation_freq(uint32_t frequency_hz)
{
    radio_main_cmd_t cmd;
    radio_main_cmd_sync_t sync;

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = RADIO_MAIN_CMD_SET_MOD_FREQ;
    cmd.u.set_u32.value = frequency_hz;

    return radio_main_enqueue_sync(&cmd, &sync);
}

bool radio_main_cmd_set_modulation_bw(uint8_t bandwidth_code)
{
    radio_main_cmd_t cmd;
    radio_main_cmd_sync_t sync;

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = RADIO_MAIN_CMD_SET_MOD_BW;
    cmd.u.set_u8.value = bandwidth_code;

    return radio_main_enqueue_sync(&cmd, &sync);
}

bool radio_main_cmd_set_option(radio_main_option_t option, uint32_t value)
{
    radio_main_cmd_t cmd;
    radio_main_cmd_sync_t sync;

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = RADIO_MAIN_CMD_SET_OPTION;
    cmd.u.set_option.option = option;
    cmd.u.set_option.value = value;

    return radio_main_enqueue_sync(&cmd, &sync);
}

bool radio_main_cmd_set_fh(bool enabled)
{
    radio_main_cmd_t cmd;
    radio_main_cmd_sync_t sync;

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = RADIO_MAIN_CMD_SET_FH;
    cmd.u.set_bool.enabled = enabled;
    return radio_main_enqueue_sync(&cmd, &sync);
}

bool radio_main_cmd_set_fh_period(uint32_t period_ms)
{
    radio_main_cmd_t cmd;
    radio_main_cmd_sync_t sync;

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = RADIO_MAIN_CMD_SET_FH_PERIOD;
    cmd.u.set_u32.value = period_ms;
    return radio_main_enqueue_sync(&cmd, &sync);
}

bool radio_main_cmd_set_coding(bool enabled)
{
    radio_main_cmd_t cmd;
    radio_main_cmd_sync_t sync;

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = RADIO_MAIN_CMD_SET_CODING;
    cmd.u.set_bool.enabled = enabled;
    return radio_main_enqueue_sync(&cmd, &sync);
}

bool radio_main_cmd_set_auto_ping(bool enabled)
{
    radio_main_cmd_t cmd;
    radio_main_cmd_sync_t sync;

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = RADIO_MAIN_CMD_SET_AUTO_PING;
    cmd.u.set_bool.enabled = enabled;
    return radio_main_enqueue_sync(&cmd, &sync);
}

bool radio_main_cmd_set_auto_ping_period(uint32_t period_ms)
{
    radio_main_cmd_t cmd;
    radio_main_cmd_sync_t sync;

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = RADIO_MAIN_CMD_SET_AUTO_PING_PERIOD;
    cmd.u.set_u32.value = period_ms;
    return radio_main_enqueue_sync(&cmd, &sync);
}

bool radio_main_cmd_set_auto_ping_mode(radio_main_auto_ping_mode_t mode)
{
    radio_main_cmd_t cmd;
    radio_main_cmd_sync_t sync;

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = RADIO_MAIN_CMD_SET_AUTO_PING_MODE;
    cmd.u.set_u32.value = (uint32_t)mode;
    return radio_main_enqueue_sync(&cmd, &sync);
}

bool radio_main_cmd_reset_module(void)
{
    radio_main_cmd_t cmd;
    radio_main_cmd_sync_t sync;

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = RADIO_MAIN_CMD_RESET_MODULE;
    return radio_main_enqueue_sync(&cmd, &sync);
}

bool radio_main_cmd_start_pairing(uint32_t timeout_ms)
{
    radio_main_cmd_t cmd;
    radio_main_cmd_sync_t sync;

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = RADIO_MAIN_CMD_START_PAIRING;
    cmd.u.pairing.timeout_ms = timeout_ms;
    cmd.u.pairing.network_mode = false;
    return radio_main_enqueue_sync(&cmd, &sync);
}

bool radio_main_cmd_start_network_pairing(uint32_t timeout_ms)
{
    radio_main_cmd_t cmd;
    radio_main_cmd_sync_t sync;

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = RADIO_MAIN_CMD_START_NETWORK_PAIRING;
    cmd.u.pairing.timeout_ms = timeout_ms;
    cmd.u.pairing.network_mode = true;
    return radio_main_enqueue_sync(&cmd, &sync);
}

bool radio_main_cmd_pairing_accept(bool accept)
{
    radio_main_cmd_t cmd;
    radio_main_cmd_sync_t sync;

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = RADIO_MAIN_CMD_PAIRING_ACCEPT;
    cmd.u.set_bool.enabled = accept;
    return radio_main_enqueue_sync(&cmd, &sync);
}

bool radio_main_cmd_send_pair_req(void)
{
    radio_main_cmd_t cmd;
    radio_main_cmd_sync_t sync;

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = RADIO_MAIN_CMD_SEND_PAIR_REQ;
    return radio_main_enqueue_sync(&cmd, &sync);
}

bool radio_main_cmd_send_network_pair_req(void)
{
    printf("RADIO: node-originated network PAIR_REQ disabled\r\n");
    return false;
}

bool radio_main_cmd_send_pair_error(uint32_t dst_id)
{
    radio_main_cmd_t cmd;
    radio_main_cmd_sync_t sync;

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = RADIO_MAIN_CMD_SEND_PAIR_ERROR;
    cmd.u.to_node.dst_id = dst_id;
    return radio_main_enqueue_sync(&cmd, &sync);
}

bool radio_main_get_runtime_cfg(radio_main_runtime_cfg_t *cfg_out)
{
    bool ok = false;

    if ((cfg_out == NULL) || (s_radio_state_mutex == NULL))
    {
        return false;
    }

    if (osMutexAcquire(s_radio_state_mutex, 100U) == osOK)
    {
        radio_main_sync_snapshot(cfg_out);
        (void)osMutexRelease(s_radio_state_mutex);
        ok = true;
    }

    return ok;
}

bool radio_main_get_auto_ping_period_ms(uint32_t *period_ms_out)
{
    bool ok = false;

    if (period_ms_out == NULL)
    {
        return false;
    }

    if ((s_radio_state_mutex != NULL) &&
        (osMutexAcquire(s_radio_state_mutex, 100U) == osOK))
    {
        *period_ms_out = radio_main_auto_ping_period_ms();
        (void)osMutexRelease(s_radio_state_mutex);
        ok = true;
    }

    return ok;
}

bool radio_main_get_last_error_text(char *out, uint8_t out_size)
{
    bool ok = false;

    if ((out == NULL) || (out_size == 0U) || (s_radio_state_mutex == NULL))
    {
        return false;
    }

    if (osMutexAcquire(s_radio_state_mutex, 100U) == osOK)
    {
        strncpy(out, s_ctx.last_error_text, out_size - 1U);
        out[out_size - 1U] = '\0';
        (void)osMutexRelease(s_radio_state_mutex);
        ok = true;
    }

    return ok;
}

uint32_t radio_main_get_node_id(void)
{
    uint32_t node_id = 0U;

    if (s_radio_state_mutex == NULL)
    {
        return 0U;
    }
    if (osMutexAcquire(s_radio_state_mutex, 100U) != osOK)
    {
        return 0U;
    }

    node_id = s_ctx.node_id;
    (void)osMutexRelease(s_radio_state_mutex);
    return node_id;
}

static void radio_main_task_fn(void *argument)
{
    radio_main_cmd_t cmd;
    security_runtime_cfg_t sec_cfg;

    (void)argument;
    memset(&s_ctx, 0, sizeof(s_ctx));
    memset(&sec_cfg, 0, sizeof(sec_cfg));
    s_ctx.node_id = laviet_local_node_id();
    s_ctx.next_msg_id = 1U;
    s_ctx.crypto_ready = laviet_crypto_init();
    radio_main_clear_gateway_pair_code();
    radio_main_clear_broadcast_group_state();
    (void)security_main_get_gateway_counter(&s_ctx.gateway_rx_counter, &s_ctx.gateway_tx_counter);
    s_ctx.modulation_id = RADIO_MAIN_MODULATION_LORA;
    s_ctx.backend_modulation_id = s_ctx.modulation_id;
    s_ctx.lora_preset = 2U;
    radio_main_clear_last_error();

    radio_default_hw_cfg(&s_ctx.hw, &hspi1);
    radio_default_lora_cfg(&s_ctx.lora_cfg);
    s_ctx.backend_cfg = s_ctx.lora_cfg;
    radio_main_load_default_profiles();

    if (security_main_cmd_get_runtime_cfg(&sec_cfg))
    {
        s_ctx.coding_enabled = sec_cfg.coding_enabled;
        s_ctx.fh_enabled = sec_cfg.fh_enabled;
        s_ctx.fh_period_ms = sec_cfg.fh_period_ms;
        s_ctx.auto_ping_enabled = sec_cfg.auto_ping_enabled;
        s_ctx.auto_ping_period_ms = sec_cfg.auto_ping_period_ms;
        s_ctx.auto_ping_mode = sec_cfg.auto_ping_mode;
        s_ctx.lora_preset = sec_cfg.lora_preset;
        if (sec_cfg.radio_profiles_persisted)
        {
            s_ctx.lora_cfg = sec_cfg.lora;
            s_ctx.fsk_cfg = sec_cfg.fsk;
            s_ctx.ook_cfg = sec_cfg.ook;
        }
    }
    else
    {
        s_ctx.fh_enabled = false;
        s_ctx.fh_period_ms = RADIO_HOP_PERIOD_DEFAULT_MS;
        s_ctx.coding_enabled = false;
        s_ctx.auto_ping_enabled = false;
        s_ctx.auto_ping_period_ms = RADIO_AUTO_PING_PERIOD_DEFAULT_MS;
        s_ctx.auto_ping_mode = RADIO_MAIN_AUTO_PING_FRAME;
        s_ctx.lora_preset = 2U;
    }

    radio_main_load_gateway_pair_code_from_security();

    if (!radio_main_validate_hop_period(s_ctx.fh_period_ms))
    {
        s_ctx.fh_period_ms = RADIO_HOP_PERIOD_DEFAULT_MS;
    }
    if (!radio_main_validate_auto_ping_period(s_ctx.auto_ping_period_ms))
    {
        s_ctx.auto_ping_period_ms = RADIO_AUTO_PING_PERIOD_DEFAULT_MS;
    }
    if (!radio_main_validate_auto_ping_mode(s_ctx.auto_ping_mode))
    {
        s_ctx.auto_ping_mode = RADIO_MAIN_AUTO_PING_FRAME;
    }

    if (!sec_cfg.radio_profiles_persisted)
    {
        radio_main_apply_preset_cfg(s_ctx.lora_preset, &s_ctx.lora_cfg);
    }
    s_ctx.backend_modulation_id = s_ctx.modulation_id;
    radio_main_apply_modulation_cfg();
    s_ctx.last_hop_ms = radio_main_now_ms();
    s_ctx.last_ping_ms = radio_main_now_ms();
    s_ctx.hop_idx = 0U;

    if (radio_main_radio_init_and_start())
    {
        printf("RADIO: init OK laviet_node=0x%04X\r\n", (unsigned int)s_ctx.node_id);
    }
    else
    {
        printf("RADIO: init failed, fallback to LoRa defaults\r\n");
        s_ctx.modulation_id = RADIO_MAIN_MODULATION_LORA;
        s_ctx.backend_modulation_id = RADIO_MAIN_MODULATION_LORA;
        s_ctx.lora_preset = 2U;
        radio_main_apply_preset_cfg(s_ctx.lora_preset, &s_ctx.lora_cfg);
        radio_main_load_default_fsk_profile(&s_ctx.fsk_cfg);
        radio_main_load_default_ook_profile(&s_ctx.ook_cfg);
        radio_main_apply_modulation_cfg();

        if (radio_main_radio_init_and_start())
        {
            printf("RADIO: fallback init OK laviet_node=0x%04X\r\n", (unsigned int)s_ctx.node_id);
        }
        else
        {
            printf("RADIO: fallback init failed\r\n");
        }
    }

    for (;;)
    {
        while (osMessageQueueGet(s_radio_cmd_queue, &cmd, NULL, 0U) == osOK)
        {
            bool cmd_result = false;

            switch (cmd.id)
            {
                case RADIO_MAIN_CMD_SEND_TEMPLATE:
                    radio_main_clear_last_error();
                    cmd_result = radio_main_send_template_internal(cmd.u.send_template.group_id,
                                                                   cmd.u.send_template.msg_id,
                                                                   cmd.u.send_template.dst_id);
                    break;

                case RADIO_MAIN_CMD_SEND_USER_TEXT:
                    radio_main_clear_last_error();
                    cmd_result = radio_main_send_system_frame(LAVIET_TYPE_DATA,
                                                              cmd.u.send_text.dst_id,
                                                              (const uint8_t *)cmd.u.send_text.text,
                                                              cmd.u.send_text.len);
                    break;

                case RADIO_MAIN_CMD_SEND_RAW:
                    radio_main_clear_last_error();
                    cmd_result = radio_main_send_raw_with_retry(cmd.u.send_raw.data,
                                                                cmd.u.send_raw.len);
                    if (!cmd_result && (s_ctx.last_error_text[0] == '\0'))
                    {
                        radio_main_set_last_error("Raw TX failed");
                    }
                    break;

                case RADIO_MAIN_CMD_SET_PRESET:
                    if (cmd.u.set_u8.value <= 2U)
                    {
                        s_ctx.lora_preset = cmd.u.set_u8.value;
                        radio_main_apply_preset_cfg(s_ctx.lora_preset, &s_ctx.lora_cfg);
                        if (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_LORA)
                        {
                            cmd_result = radio_main_reconfigure_radio();
                        }
                        else
                        {
                            cmd_result = true;
                        }
                    }
                    break;

                case RADIO_MAIN_CMD_SET_MODULATION:
                    if (cmd.u.set_u8.value <= 2U)
                    {
                        radio_main_modulation_t previous_modulation = s_ctx.modulation_id;
                        radio_main_modulation_t requested = (radio_main_modulation_t)cmd.u.set_u8.value;

                        s_ctx.modulation_id = requested;
                        cmd_result = radio_main_switch_backend(requested);
                        if (!cmd_result)
                        {
                            radio_main_load_default_profile(requested);
                            cmd_result = radio_main_switch_backend(requested);
                        }
                        if (!cmd_result)
                        {
                            s_ctx.modulation_id = previous_modulation;
                            (void)radio_main_switch_backend(previous_modulation);
                        }
                    }
                    break;

                case RADIO_MAIN_CMD_SET_MOD_FREQ:
                    if (s_ctx.modulation_id == RADIO_MAIN_MODULATION_FSK)
                    {
                        cmd_result = radio_main_apply_option(RADIO_MAIN_OPTION_FSK_FREQ,
                                                             cmd.u.set_u32.value);
                    }
                    else if (s_ctx.modulation_id == RADIO_MAIN_MODULATION_OOK)
                    {
                        cmd_result = radio_main_apply_option(RADIO_MAIN_OPTION_OOK_FREQ,
                                                             cmd.u.set_u32.value);
                    }
                    else
                    {
                        cmd_result = radio_main_apply_option(RADIO_MAIN_OPTION_LORA_FREQ,
                                                             cmd.u.set_u32.value);
                    }
                    break;

                case RADIO_MAIN_CMD_SET_MOD_BW:
                    if (s_ctx.modulation_id == RADIO_MAIN_MODULATION_FSK)
                    {
                        cmd_result = radio_main_apply_option(RADIO_MAIN_OPTION_FSK_RX_BW,
                                                             cmd.u.set_u8.value);
                    }
                    else if (s_ctx.modulation_id == RADIO_MAIN_MODULATION_OOK)
                    {
                        cmd_result = radio_main_apply_option(RADIO_MAIN_OPTION_OOK_RX_BW,
                                                             cmd.u.set_u8.value);
                    }
                    else
                    {
                        cmd_result = radio_main_apply_option(RADIO_MAIN_OPTION_LORA_BW,
                                                             cmd.u.set_u8.value);
                    }
                    break;

                case RADIO_MAIN_CMD_SET_OPTION:
                    cmd_result = radio_main_apply_option(cmd.u.set_option.option,
                                                         cmd.u.set_option.value);
                    break;

                case RADIO_MAIN_CMD_SET_FH:
                    s_ctx.fh_enabled = cmd.u.set_bool.enabled;
                    s_ctx.last_hop_ms = radio_main_now_ms();
                    cmd_result = true;
                    break;

                case RADIO_MAIN_CMD_SET_FH_PERIOD:
                    if (radio_main_validate_hop_period(cmd.u.set_u32.value))
                    {
                        s_ctx.fh_period_ms = cmd.u.set_u32.value;
                        s_ctx.last_hop_ms = radio_main_now_ms();
                        cmd_result = true;
                    }
                    break;

                case RADIO_MAIN_CMD_SET_CODING:
                    s_ctx.coding_enabled = cmd.u.set_bool.enabled;
                    cmd_result = true;
                    break;

                case RADIO_MAIN_CMD_SET_AUTO_PING:
                    s_ctx.auto_ping_enabled = cmd.u.set_bool.enabled;
                    s_ctx.last_ping_ms = radio_main_now_ms();
                    cmd_result = true;
                    break;

                case RADIO_MAIN_CMD_SET_AUTO_PING_PERIOD:
                    if (radio_main_validate_auto_ping_period(cmd.u.set_u32.value))
                    {
                        s_ctx.auto_ping_period_ms = cmd.u.set_u32.value;
                        s_ctx.last_ping_ms = radio_main_now_ms();
                        cmd_result = true;
                    }
                    break;

                case RADIO_MAIN_CMD_SET_AUTO_PING_MODE:
                    if (radio_main_validate_auto_ping_mode((radio_main_auto_ping_mode_t)cmd.u.set_u32.value))
                    {
                        s_ctx.auto_ping_mode = (radio_main_auto_ping_mode_t)cmd.u.set_u32.value;
                        s_ctx.last_ping_ms = radio_main_now_ms();
                        cmd_result = true;
                    }
                    break;

                case RADIO_MAIN_CMD_RESET_MODULE:
                    cmd_result = radio_main_reset_module_internal();
                    break;

                case RADIO_MAIN_CMD_START_PAIRING:
                case RADIO_MAIN_CMD_START_NETWORK_PAIRING:
                    cmd_result = s_ctx.initialized || radio_main_force_recover_radio("start pairing");
                    if (cmd_result && cmd.u.pairing.network_mode)
                    {
                        trusted_info_t gateway_info;

                        memset(&gateway_info, 0, sizeof(gateway_info));
                        if (security_main_cmd_get_device(0U, &gateway_info) && gateway_info.in_use)
                        {
                            printf("RADIO: network pairing blocked, gateway slot already in use\r\n");
                            cmd_result = false;
                        }
                    }
                    if (cmd_result)
                    {
                        s_ctx.pairing_active = true;
                        s_ctx.pairing_network_mode = cmd.u.pairing.network_mode;
                        if (cmd.u.pairing.network_mode)
                        {
                            s_ctx.gateway_key_mode_known = false;
                        }
                        s_ctx.pairing_pending = false;
                        s_ctx.pairing_pending_network = false;
                        s_ctx.pairing_outgoing_pending = false;
                        s_ctx.pairing_outgoing_network = false;
                        s_ctx.pairing_until_ms = radio_main_now_ms() + cmd.u.pairing.timeout_ms;
                        if (s_ctx.initialized &&
                            (radio_get_state() != RADIO_STATE_TX))
                        {
                            (void)radio_start_rx_continuous();
                        }
                    }
                    break;

                case RADIO_MAIN_CMD_PAIRING_ACCEPT:
                    if (s_ctx.pairing_pending)
                    {
                        cmd_result = radio_main_finish_pairing(cmd.u.set_bool.enabled);
                    }
                    break;

                case RADIO_MAIN_CMD_SEND_PAIR_REQ:
                    s_ctx.pairing_network_mode = false;
                    cmd_result = radio_main_send_pair_request_internal();
                    break;

                case RADIO_MAIN_CMD_SEND_NETWORK_PAIR_REQ:
                    printf("RADIO: network PAIR_REQ command rejected on node\r\n");
                    cmd_result = false;
                    break;

                case RADIO_MAIN_CMD_SEND_PAIR_ERROR:
                    cmd_result = radio_main_send_system_frame(LAVIET_TYPE_ERROR,
                                                              cmd.u.to_node.dst_id,
                                                              (const uint8_t *)"\x04\x00\x00\x00\x00\x00\x00\x00",
                                                              LAVIET_ERROR_PAYLOAD_LEN);
                    break;

                default:
                    break;
            }

            if (cmd.sync != NULL)
            {
                cmd.sync->result = cmd_result;
                cmd.sync->done = true;
            }
        }

        if (s_ctx.initialized)
        {
            radio_process();
            radio_main_handle_events();
            radio_main_watchdog_tx();
            radio_main_handle_ack_timeout();
            radio_main_ack_recent_expire();
            radio_main_handle_hopping();
            radio_main_handle_auto_ping();
            radio_main_ensure_rx_continuous();
        }

        if (s_ctx.pairing_active && (radio_main_now_ms() >= s_ctx.pairing_until_ms))
        {
            s_ctx.pairing_active = false;
            s_ctx.pairing_network_mode = false;
            s_ctx.pairing_pending = false;
            s_ctx.pairing_pending_network = false;
            s_ctx.pairing_outgoing_pending = false;
            s_ctx.pairing_outgoing_network = false;
            radio_main_notify(MENU_NOTIFICATION_PAIRING, "Pairing timeout");
        }

        {
            uint32_t poll_ms = (s_ctx.tx_in_progress || s_ctx.ack_pending.active) ?
                               RADIO_ACK_ACTIVE_POLL_MS : RADIO_IDLE_POLL_MS;
            uint32_t auto_ping_period_ms = radio_main_auto_ping_period_ms();

            if (s_ctx.auto_ping_enabled && (auto_ping_period_ms < poll_ms))
            {
                poll_ms = auto_ping_period_ms;
            }
            if (poll_ms == 0UL)
            {
                poll_ms = 1UL;
            }
            osDelay(poll_ms);
        }
    }
}

static bool radio_main_enqueue_sync(const radio_main_cmd_t *cmd, radio_main_cmd_sync_t *sync)
{
    radio_main_cmd_t local;

    if ((cmd == NULL) || (sync == NULL) || (s_radio_cmd_queue == NULL))
    {
        return false;
    }

    memset(sync, 0, sizeof(*sync));
    local = *cmd;
    local.sync = sync;

    if (osMessageQueuePut(s_radio_cmd_queue, &local, 0U, 100U) != osOK)
    {
        return false;
    }

    return radio_main_wait_sync(sync, RADIO_CMD_WAIT_MS);
}

static bool radio_main_wait_sync(radio_main_cmd_sync_t *sync, uint32_t timeout_ms)
{
    uint32_t start = radio_main_now_ms();

    if (sync == NULL)
    {
        return false;
    }

    while (!sync->done)
    {
        if ((radio_main_now_ms() - start) > timeout_ms)
        {
            return false;
        }
        osDelay(RADIO_CMD_POLL_MS);
    }

    return sync->result;
}

/*
 * Computes a conservative TX watchdog window for the active modulation.
 * FSK/OOK use bitrate-derived timing, while LoRa uses a generous fixed window
 * because the airtime depends on SF/BW/CR/header mode and should not be
 * cut short by the safety watchdog.
 */
static uint32_t radio_main_tx_timeout_ms(uint16_t payload_len)
{
    uint64_t timeout_ms = RADIO_TX_GUARD_LORA_MS;
    uint32_t bitrate_bps = 0U;
    uint32_t total_bytes = (payload_len == 0U) ? 1UL : (uint32_t)payload_len;

    if (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_FSK)
    {
        bitrate_bps = s_ctx.fsk_cfg.bitrate_bps;
        total_bytes++;
    }
    else if (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_OOK)
    {
        bitrate_bps = s_ctx.ook_cfg.bitrate_bps;
        total_bytes++;
    }

    if (bitrate_bps > 0UL)
    {
        timeout_ms = ((uint64_t)total_bytes * 8ULL * 1000ULL) / bitrate_bps;
        if ((((uint64_t)total_bytes * 8ULL * 1000ULL) % bitrate_bps) != 0ULL)
        {
            timeout_ms++;
        }
        timeout_ms += RADIO_TX_GUARD_MARGIN_MS;
        if (timeout_ms < RADIO_TX_GUARD_MIN_MS)
        {
            timeout_ms = RADIO_TX_GUARD_MIN_MS;
        }
    }

    if (timeout_ms > 0xFFFFFFFFULL)
    {
        return 0xFFFFFFFFUL;
    }

    return (uint32_t)timeout_ms;
}

/*
 * Marks a locally initiated TX so the application layer can recover even if the
 * backend reports HW_ERROR but keeps exposing RADIO_STATE_TX for a short time.
 */
static void radio_main_tx_mark_started_ex(uint16_t payload_len, bool silent)
{
    s_ctx.tx_in_progress = true;
    s_ctx.tx_silent = silent;
    s_ctx.tx_deadline_ms = radio_main_now_ms() + radio_main_tx_timeout_ms(payload_len);
}

static void radio_main_tx_clear(void)
{
    s_ctx.tx_in_progress = false;
    s_ctx.tx_silent = false;
    s_ctx.tx_deadline_ms = 0U;
}

static bool radio_main_tx_timed_out(void)
{
    return (s_ctx.tx_in_progress &&
            ((int32_t)(radio_main_now_ms() - s_ctx.tx_deadline_ms) >= 0));
}

static uint32_t radio_main_ack_timeout_ms(uint8_t raw_len)
{
    uint32_t tx_airtime_ms;
    uint32_t ack_airtime_ms;
    uint32_t wait_ms;
    uint8_t ack_raw_len = (uint8_t)(LAVIET_FRAME_MIN_LEN + LAVIET_ACK_PAYLOAD_LEN);

    if (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_FSK)
    {
        tx_airtime_ms = radio_main_estimate_fsk_ook_airtime_ms(raw_len, false);
        ack_airtime_ms = radio_main_estimate_fsk_ook_airtime_ms(ack_raw_len, false);
    }
    else if (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_OOK)
    {
        tx_airtime_ms = radio_main_estimate_fsk_ook_airtime_ms(raw_len, true);
        ack_airtime_ms = radio_main_estimate_fsk_ook_airtime_ms(ack_raw_len, true);
    }
    else
    {
        tx_airtime_ms = radio_main_estimate_lora_airtime_ms(raw_len);
        ack_airtime_ms = radio_main_estimate_lora_airtime_ms(ack_raw_len);
    }

    wait_ms = tx_airtime_ms + ack_airtime_ms + RADIO_ACK_TIMEOUT_MARGIN_MS;
    if (wait_ms < RADIO_ACK_TIMEOUT_MIN_MS)
    {
        wait_ms = RADIO_ACK_TIMEOUT_MIN_MS;
    }

    return wait_ms;
}

static bool radio_main_ack_track_start(const laviet_frame_t *frame, const uint8_t *raw, uint8_t raw_len)
{
    if ((frame == NULL) || (raw == NULL) || (raw_len == 0U))
    {
        return false;
    }
    if ((frame->dst_id == LAVIET_BROADCAST_ID) ||
        ((frame->flags & LAVIET_FLAG_ACK_REQUIRED) == 0U) ||
        (laviet_frame_type(frame) == LAVIET_TYPE_ACK))
    {
        return true;
    }
    if (s_ctx.ack_pending.active)
    {
        radio_main_set_last_error("ACK pending");
        return false;
    }

    memset(&s_ctx.ack_pending, 0, sizeof(s_ctx.ack_pending));
    s_ctx.ack_pending.active = true;
    s_ctx.ack_pending.peer_id = frame->dst_id;
    s_ctx.ack_pending.msg_id = frame->msg_id;
    s_ctx.ack_pending.counter = frame->counter;
    s_ctx.ack_pending.raw_len = raw_len;
    memcpy(s_ctx.ack_pending.raw, raw, raw_len);
    s_ctx.ack_pending.wait_ms = radio_main_ack_timeout_ms(raw_len);
    s_ctx.ack_pending.deadline_ms = radio_main_now_ms() + s_ctx.ack_pending.wait_ms;
    s_ctx.ack_pending.retries_done = 0U;
    printf("RADIO ACK wait dst=0x%04X msg=0x%04X counter=%lu timeout=%lu ms\r\n",
           (unsigned int)s_ctx.ack_pending.peer_id,
           (unsigned int)s_ctx.ack_pending.msg_id,
           (unsigned long)s_ctx.ack_pending.counter,
           (unsigned long)s_ctx.ack_pending.wait_ms);
    return true;
}

static void radio_main_ack_track_close(bool timed_out)
{
    if (!s_ctx.ack_pending.active)
    {
        return;
    }

    s_ctx.ack_recent.valid = true;
    s_ctx.ack_recent.timed_out = timed_out;
    s_ctx.ack_recent.peer_id = s_ctx.ack_pending.peer_id;
    s_ctx.ack_recent.msg_id = s_ctx.ack_pending.msg_id;
    s_ctx.ack_recent.counter = s_ctx.ack_pending.counter;
    s_ctx.ack_recent.expires_ms = radio_main_now_ms() + RADIO_ACK_CLOSED_TTL_MS;
    memset(&s_ctx.ack_pending, 0, sizeof(s_ctx.ack_pending));
}

static void radio_main_ack_recent_expire(void)
{
    if (s_ctx.ack_recent.valid &&
        ((int32_t)(radio_main_now_ms() - s_ctx.ack_recent.expires_ms) >= 0))
    {
        memset(&s_ctx.ack_recent, 0, sizeof(s_ctx.ack_recent));
    }
}

static void radio_main_handle_ack_frame(uint16_t src_id, uint16_t acked_msg_id, uint32_t acked_counter)
{
    if (s_ctx.ack_pending.active &&
        (src_id == s_ctx.ack_pending.peer_id) &&
        (acked_msg_id == s_ctx.ack_pending.msg_id) &&
        (acked_counter == s_ctx.ack_pending.counter))
    {
        printf("RADIO ACK delivered src=0x%04X msg=0x%04X counter=%lu retries=%u\r\n",
               (unsigned int)src_id,
               (unsigned int)acked_msg_id,
               (unsigned long)acked_counter,
               (unsigned int)s_ctx.ack_pending.retries_done);
        radio_main_notify(MENU_NOTIFICATION_DELIVERY, "Delivery ACK");
        radio_main_ack_track_close(false);
        return;
    }

    if (s_ctx.ack_pending.active && (src_id == s_ctx.ack_pending.peer_id))
    {
        printf("RADIO ACK bad src=0x%04X ack_msg=0x%04X ack_counter=%lu expected_msg=0x%04X expected_counter=%lu\r\n",
               (unsigned int)src_id,
               (unsigned int)acked_msg_id,
               (unsigned long)acked_counter,
               (unsigned int)s_ctx.ack_pending.msg_id,
               (unsigned long)s_ctx.ack_pending.counter);
        return;
    }

    if (s_ctx.ack_recent.valid &&
        (src_id == s_ctx.ack_recent.peer_id) &&
        (acked_msg_id == s_ctx.ack_recent.msg_id) &&
        (acked_counter == s_ctx.ack_recent.counter))
    {
        printf("RADIO ACK %s src=0x%04X ack_msg=0x%04X ack_counter=%lu\r\n",
               s_ctx.ack_recent.timed_out ? "late" : "duplicate",
               (unsigned int)src_id,
               (unsigned int)acked_msg_id,
               (unsigned long)acked_counter);
        return;
    }

    printf("RADIO ACK unexpected src=0x%04X ack_msg=0x%04X ack_counter=%lu\r\n",
           (unsigned int)src_id,
           (unsigned int)acked_msg_id,
           (unsigned long)acked_counter);
}

static void radio_main_handle_ack_timeout(void)
{
    if (!s_ctx.ack_pending.active || s_ctx.tx_in_progress)
    {
        return;
    }
    if ((int32_t)(radio_main_now_ms() - s_ctx.ack_pending.deadline_ms) < 0)
    {
        return;
    }

    if (s_ctx.ack_pending.retries_done < RADIO_ACK_RETRY_LIMIT)
    {
        printf("RADIO ACK retry %u/%u dst=0x%04X msg=0x%04X counter=%lu\r\n",
               (unsigned int)(s_ctx.ack_pending.retries_done + 1U),
               (unsigned int)RADIO_ACK_RETRY_LIMIT,
               (unsigned int)s_ctx.ack_pending.peer_id,
               (unsigned int)s_ctx.ack_pending.msg_id,
               (unsigned long)s_ctx.ack_pending.counter);

        if (radio_main_send_raw_with_retry(s_ctx.ack_pending.raw, s_ctx.ack_pending.raw_len))
        {
            s_ctx.ack_pending.retries_done++;
            s_ctx.ack_pending.deadline_ms = radio_main_now_ms() + s_ctx.ack_pending.wait_ms;
            return;
        }

        printf("RADIO ACK retry send failed dst=0x%04X msg=0x%04X\r\n",
               (unsigned int)s_ctx.ack_pending.peer_id,
               (unsigned int)s_ctx.ack_pending.msg_id);
        s_ctx.ack_pending.deadline_ms = radio_main_now_ms() + 50UL;
        return;
    }

    printf("RADIO ACK timeout dst=0x%04X msg=0x%04X counter=%lu retries=%u\r\n",
           (unsigned int)s_ctx.ack_pending.peer_id,
           (unsigned int)s_ctx.ack_pending.msg_id,
           (unsigned long)s_ctx.ack_pending.counter,
           (unsigned int)s_ctx.ack_pending.retries_done);
    radio_main_set_last_error("ACK timeout");
    radio_main_notify(MENU_NOTIFICATION_WARNING, "ACK timeout");
    radio_main_ack_track_close(true);
}

static void radio_main_set_last_error(const char *text)
{
    if (text == NULL)
    {
        s_ctx.last_error_text[0] = '\0';
        return;
    }

    strncpy(s_ctx.last_error_text, text, sizeof(s_ctx.last_error_text) - 1U);
    s_ctx.last_error_text[sizeof(s_ctx.last_error_text) - 1U] = '\0';
}

static void radio_main_clear_last_error(void)
{
    s_ctx.last_error_text[0] = '\0';
}

static uint32_t radio_main_current_frequency_hz(void)
{
    if (s_ctx.modulation_id == RADIO_MAIN_MODULATION_FSK)
    {
        return s_ctx.fsk_cfg.frequency_hz;
    }
    if (s_ctx.modulation_id == RADIO_MAIN_MODULATION_OOK)
    {
        return s_ctx.ook_cfg.frequency_hz;
    }

    return s_ctx.lora_cfg.frequency_hz;
}

/*
 * Returns the ETSI-style duty-cycle limit for the current ISM sub-band in
 * permille. The auto-ping scheduler later uses half of that duty cycle to keep
 * a safety margin instead of driving the channel at the legal maximum.
 */
static uint16_t radio_main_current_duty_cycle_permille(uint32_t frequency_hz)
{
    if ((frequency_hz >= 868700000UL) && (frequency_hz <= 869200000UL))
    {
        return 1U;   /* 0.1% */
    }
    if ((frequency_hz >= 869400000UL) && (frequency_hz <= 869650000UL))
    {
        return 100U; /* 10% */
    }
    if ((frequency_hz >= 863000000UL) && (frequency_hz < 868700000UL))
    {
        return 10U;  /* 1% */
    }
    if ((frequency_hz > 869650000UL) && (frequency_hz <= 870000000UL))
    {
        return 10U;  /* 1% */
    }

    return 10U;
}

static uint32_t radio_main_lora_bw_hz(radio_lora_bw_t bw)
{
    switch (bw)
    {
        case RADIO_LORA_BW_7_8_KHZ:
            return RADIO_LORA_BW_HZ_7_8;
        case RADIO_LORA_BW_10_4_KHZ:
            return RADIO_LORA_BW_HZ_10_4;
        case RADIO_LORA_BW_15_6_KHZ:
            return RADIO_LORA_BW_HZ_15_6;
        case RADIO_LORA_BW_20_8_KHZ:
            return RADIO_LORA_BW_HZ_20_8;
        case RADIO_LORA_BW_31_25_KHZ:
            return RADIO_LORA_BW_HZ_31_25;
        case RADIO_LORA_BW_41_7_KHZ:
            return RADIO_LORA_BW_HZ_41_7;
        case RADIO_LORA_BW_62_5_KHZ:
            return RADIO_LORA_BW_HZ_62_5;
        case RADIO_LORA_BW_250_KHZ:
            return RADIO_LORA_BW_HZ_250;
        case RADIO_LORA_BW_500_KHZ:
            return RADIO_LORA_BW_HZ_500;
        case RADIO_LORA_BW_125_KHZ:
        default:
            return RADIO_LORA_BW_HZ_125;
    }
}

/*
 * Estimates LoRa packet airtime using the standard SX127x packet formula.
 * The result is intentionally conservative, because auto-ping should stay
 * clearly below the band occupancy limit.
 */
static uint32_t radio_main_estimate_lora_airtime_ms(uint16_t payload_len)
{
    uint32_t bw_hz = radio_main_lora_bw_hz(s_ctx.lora_cfg.bandwidth);
    uint32_t sf = s_ctx.lora_cfg.spreading_factor;
    uint32_t cr = (uint32_t)(s_ctx.lora_cfg.coding_rate - 4U);
    uint32_t de = ((bw_hz <= RADIO_LORA_BW_HZ_125) && (sf >= 11U)) ? 1U : 0U;
    uint32_t ih = s_ctx.lora_cfg.implicit_header ? 1U : 0U;
    uint32_t crc = s_ctx.lora_cfg.crc_on ? 1U : 0U;
    uint64_t tsym_us;
    uint64_t preamble_us;
    int32_t numerator;
    int32_t denominator;
    int32_t ceil_term = 0;
    uint32_t payload_symbols;
    uint64_t payload_us;
    uint64_t total_ms;

    if ((bw_hz == 0UL) || (sf < 6U))
    {
        return RADIO_TX_GUARD_LORA_MS;
    }

    tsym_us = (((uint64_t)1ULL << sf) * 1000000ULL) / (uint64_t)bw_hz;
    if ((((uint64_t)1ULL << sf) * 1000000ULL) % (uint64_t)bw_hz)
    {
        tsym_us++;
    }

    preamble_us = ((uint64_t)s_ctx.lora_cfg.preamble_len * tsym_us) +
                  ((17ULL * tsym_us) / 4ULL);
    if (((17ULL * tsym_us) % 4ULL) != 0ULL)
    {
        preamble_us++;
    }

    numerator = (int32_t)(8UL * payload_len) - (int32_t)(4UL * sf) + 28 +
                (int32_t)(16UL * crc) - (int32_t)(20UL * ih);
    denominator = (int32_t)(4UL * (sf - (2UL * de)));
    if ((numerator > 0) && (denominator > 0))
    {
        ceil_term = (numerator + denominator - 1) / denominator;
    }

    payload_symbols = 8U;
    if (ceil_term > 0)
    {
        payload_symbols += (uint32_t)ceil_term * (cr + 4U);
    }

    payload_us = (uint64_t)payload_symbols * tsym_us;
    total_ms = (preamble_us + payload_us + 999ULL) / 1000ULL;
    if (total_ms == 0ULL)
    {
        total_ms = 1ULL;
    }
    if (total_ms > 0xFFFFFFFFULL)
    {
        return 0xFFFFFFFFUL;
    }

    return (uint32_t)total_ms;
}

/*
 * Estimates on-air time for FSK/OOK by accounting for preamble, sync word,
 * packet-length byte, optional CRC and a small extra guard for packet-engine
 * framing overhead. This is conservative on purpose for auto-ping throttling.
 */
static uint32_t radio_main_estimate_fsk_ook_airtime_ms(uint16_t payload_len, bool ook_mode)
{
    uint32_t bitrate_bps;
    uint32_t preamble_len;
    uint32_t sync_len;
    uint32_t crc_len = 0U;
    uint32_t total_bytes;
    uint64_t total_ms;

    if (ook_mode)
    {
        bitrate_bps = s_ctx.ook_cfg.bitrate_bps;
        preamble_len = s_ctx.ook_cfg.preamble_len;
        sync_len = s_ctx.ook_cfg.sync_word_len;
    }
    else
    {
        bitrate_bps = s_ctx.fsk_cfg.bitrate_bps;
        preamble_len = s_ctx.fsk_cfg.preamble_len;
        sync_len = s_ctx.fsk_cfg.sync_word_len;
        if (s_ctx.fsk_cfg.crc_type != RADIO_MAIN_CRC_OFF)
        {
            crc_len = 2U;
        }
    }

    if (bitrate_bps == 0UL)
    {
        return RADIO_TX_GUARD_MIN_MS;
    }

    total_bytes = preamble_len + sync_len + 1UL + payload_len + crc_len + 2UL;
    total_ms = ((uint64_t)total_bytes * 8ULL * 1000ULL) / bitrate_bps;
    if ((((uint64_t)total_bytes * 8ULL * 1000ULL) % bitrate_bps) != 0ULL)
    {
        total_ms++;
    }
    total_ms += RADIO_TX_GUARD_MARGIN_MS;

    if (total_ms < RADIO_TX_GUARD_MIN_MS)
    {
        total_ms = RADIO_TX_GUARD_MIN_MS;
    }
    if (total_ms > 0xFFFFFFFFULL)
    {
        return 0xFFFFFFFFUL;
    }

    return (uint32_t)total_ms;
}

static uint32_t radio_main_auto_ping_airtime_ms(void)
{
    const char *msg = s_template_groups[2][0];
    uint16_t payload_len = (uint16_t)strlen(msg);
    uint16_t raw_len;

    if (s_ctx.auto_ping_mode == RADIO_MAIN_AUTO_PING_RAW)
    {
        raw_len = RADIO_AUTO_PING_RAW_LEN;
    }
    else
    {
        if (payload_len > LAVIET_MAX_PAYLOAD)
        {
            payload_len = LAVIET_MAX_PAYLOAD;
        }
        raw_len = (uint16_t)(LAVIET_FRAME_HEADER_LEN + payload_len + LAVIET_MAC_TAG_LEN);
    }

    if (s_ctx.modulation_id == RADIO_MAIN_MODULATION_FSK)
    {
        return radio_main_estimate_fsk_ook_airtime_ms(raw_len, false);
    }
    if (s_ctx.modulation_id == RADIO_MAIN_MODULATION_OOK)
    {
        return radio_main_estimate_fsk_ook_airtime_ms(raw_len, true);
    }

    return radio_main_estimate_lora_airtime_ms(raw_len);
}

static uint32_t radio_main_auto_ping_period_ms(void)
{
    uint16_t duty_permille;
    uint32_t airtime_ms;
    uint64_t period_ms;

    if (radio_main_validate_auto_ping_period(s_ctx.auto_ping_period_ms))
    {
        return s_ctx.auto_ping_period_ms;
    }

    duty_permille = radio_main_current_duty_cycle_permille(radio_main_current_frequency_hz());
    airtime_ms = radio_main_auto_ping_airtime_ms();
    if (duty_permille == 0U)
    {
        return RADIO_AUTO_PING_MIN_PERIOD_MS;
    }

    period_ms = ((uint64_t)airtime_ms * 2000ULL) / duty_permille;
    if ((((uint64_t)airtime_ms * 2000ULL) % duty_permille) != 0ULL)
    {
        period_ms++;
    }

    if (period_ms < RADIO_AUTO_PING_MIN_PERIOD_MS)
    {
        period_ms = RADIO_AUTO_PING_MIN_PERIOD_MS;
    }
    if (period_ms > 0xFFFFFFFFULL)
    {
        return 0xFFFFFFFFUL;
    }

    return (uint32_t)period_ms;
}

static bool radio_main_radio_init_and_start(void)
{
    radio_status_t st;

    if (!radio_main_push_backend_cfg())
    {
        return false;
    }

    st = radio_init(&s_ctx.hw, &s_ctx.backend_cfg, NULL, NULL);
    if (st != RADIO_OK)
    {
        return false;
    }

    st = radio_start_rx_continuous();
    if (st != RADIO_OK)
    {
        (void)radio_deinit();
        return false;
    }

    s_ctx.initialized = true;
    radio_main_tx_clear();
    radio_main_log_runtime_profile("radio init");
    radio_main_log_hw_registers("radio init");
    return true;
}

void radio_main_load_default_lora_preset(uint8_t preset_id, radio_lora_cfg_t *cfg)
{
    if (cfg == NULL)
    {
        return;
    }

    switch (preset_id)
    {
        case 0U: /* STD */
            cfg->frequency_hz = 868100000UL;
            cfg->bandwidth = RADIO_LORA_BW_125_KHZ;
            cfg->spreading_factor = 7U;
            cfg->coding_rate = 5U;
            cfg->preamble_len = 8U;
            cfg->sync_word = 0x34U;
            cfg->crc_on = true;
            cfg->invert_iq = false;
            cfg->tx_power_dbm = 14;
            cfg->implicit_header = false;
            cfg->payload_len = 0U;
            break;

        case 1U: /* RANGE */
            cfg->frequency_hz = 868300000UL;
            cfg->bandwidth = RADIO_LORA_BW_125_KHZ;
            cfg->spreading_factor = 12U;
            cfg->coding_rate = 5U;
            cfg->preamble_len = 12U;
            cfg->sync_word = 0x34U;
            cfg->crc_on = true;
            cfg->invert_iq = false;
            cfg->tx_power_dbm = 14;
            cfg->implicit_header = false;
            cfg->payload_len = 0U;
            break;

        case 2U: /* FAST */
            cfg->frequency_hz = 868500000UL;
            cfg->bandwidth = RADIO_LORA_BW_500_KHZ;
            cfg->spreading_factor = 7U;
            cfg->coding_rate = 5U;
            cfg->preamble_len = 8U;
            cfg->sync_word = 0x34U;
            cfg->crc_on = true;
            cfg->invert_iq = false;
            cfg->tx_power_dbm = 17;
            cfg->implicit_header = false;
            cfg->payload_len = 0U;
            break;

        default:
            break;
    }
}

static void radio_main_apply_preset_cfg(uint8_t preset_id, radio_lora_cfg_t *cfg)
{
    radio_main_load_default_lora_preset(preset_id, cfg);
}

/**
 * @brief Wypełnia profile FSK i OOK wartościami domyślnymi.
 *
 * Te profile są przechowywane przez aplikację nawet wtedy, gdy aktywny backend
 * radiowy nie umie jeszcze ich odwzorować 1:1. Dzięki temu UI i dokumentacja
 * opisują już docelowy model konfiguracji.
 */
static void radio_main_load_default_profiles(void)
{
    radio_main_load_default_fsk_profile(&s_ctx.fsk_cfg);
    radio_main_load_default_ook_profile(&s_ctx.ook_cfg);
}

void radio_main_load_default_fsk_profile(radio_main_fsk_cfg_t *cfg)
{
    if (cfg == NULL)
    {
        return;
    }

    memset(cfg, 0, sizeof(*cfg));
    cfg->shaping = RADIO_MAIN_FSK_SHAPING_GFSK;
    cfg->frequency_hz = 868300000UL;
    cfg->bitrate_bps = 4800UL;
    cfg->rx_bandwidth = RADIO_LORA_BW_125_KHZ;
    cfg->filter = RADIO_MAIN_FILTER_BT_05;
    cfg->tx_power_dbm = 14;
    cfg->preamble_len = 8U;
    cfg->sync_word_len = 2U;
    cfg->sync_word = 0x00002DD4UL;
    cfg->address_filter = RADIO_MAIN_ADDRESS_FILTER_NONE;
    cfg->crc_type = RADIO_MAIN_CRC_CCITT;
    cfg->data_whitening = true;
}

void radio_main_load_default_ook_profile(radio_main_ook_cfg_t *cfg)
{
    if (cfg == NULL)
    {
        return;
    }

    memset(cfg, 0, sizeof(*cfg));
    cfg->frequency_hz = 868500000UL;
    cfg->bitrate_bps = 4800UL;
    cfg->rx_bandwidth = RADIO_LORA_BW_125_KHZ;
    cfg->tx_power_dbm = 10;
    cfg->preamble_len = 8U;
    cfg->sync_word_len = 2U;
    cfg->sync_word = 0x00002DD4UL;
    cfg->threshold = RADIO_MAIN_OOK_THRESHOLD_PEAK;
    cfg->threshold_value = 12U;
}

static void radio_main_sync_snapshot(radio_main_runtime_cfg_t *cfg)
{
    if (cfg == NULL)
    {
        return;
    }

    memset(cfg, 0, sizeof(*cfg));
    cfg->active_modulation = s_ctx.modulation_id;
    cfg->lora = s_ctx.lora_cfg;
    cfg->fsk = s_ctx.fsk_cfg;
    cfg->ook = s_ctx.ook_cfg;
    cfg->fh_enabled = s_ctx.fh_enabled;
    cfg->fh_period_ms = s_ctx.fh_period_ms;
    cfg->coding_enabled = s_ctx.coding_enabled;
    cfg->auto_ping_enabled = s_ctx.auto_ping_enabled;
    cfg->auto_ping_period_ms = radio_main_auto_ping_period_ms();
    cfg->auto_ping_mode = s_ctx.auto_ping_mode;
}

/**
 * @brief Przygotowuje konfigurację przekazywaną do `radio_init(...)`.
 *
 * Warstwa aplikacji przechowuje pełne profile LoRa/FSK/OOK, ale wspólne API
 * `radio_init(...)` nadal przyjmuje strukturę LoRa. Dlatego w tym miejscu
 * przygotowujemy tylko tę część, która jest nadal potrzebna dla ścieżki LoRa,
 * a właściwe profile FSK/OOK są przekazywane osobno przez `radio_set_*_cfg(...)`.
 */
static void radio_main_apply_modulation_cfg(void)
{
    s_ctx.backend_cfg = s_ctx.lora_cfg;

    if (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_LORA)
    {
        if (s_ctx.backend_cfg.implicit_header && (s_ctx.backend_cfg.payload_len == 0U))
        {
            /*
             * BEKO wysyła ramki o zmiennej długości, więc implicit header traktujemy
             * jako tryb eksperymentalny. Wypełniamy `payload_len`, aby backend LoRa
             * zaakceptował konfigurację przy starcie.
             */
            s_ctx.backend_cfg.payload_len = LAVIET_FRAME_MAX_LEN;
        }
        return;
    }

}

static bool radio_main_switch_backend(radio_main_modulation_t modulation)
{
    radio_main_modulation_t previous_modulation = s_ctx.backend_modulation_id;
    bool was_initialized = s_ctx.initialized;
    bool ok;

    s_ctx.backend_modulation_id = modulation;
    radio_main_apply_modulation_cfg();
    ok = s_ctx.initialized ? radio_main_reconfigure_radio() : radio_main_radio_init_and_start();
    if (ok)
    {
        return true;
    }

    printf("RADIO: backend switch to %u failed, restore %u\r\n",
           (unsigned int)modulation,
           (unsigned int)previous_modulation);
    s_ctx.backend_modulation_id = previous_modulation;
    radio_main_apply_modulation_cfg();
    if (was_initialized && !s_ctx.initialized)
    {
        (void)radio_main_radio_init_and_start();
    }
    return false;
}

static void radio_main_load_default_profile(radio_main_modulation_t modulation)
{
    switch (modulation)
    {
        case RADIO_MAIN_MODULATION_FSK:
            radio_main_load_default_fsk_profile(&s_ctx.fsk_cfg);
            break;

        case RADIO_MAIN_MODULATION_OOK:
            radio_main_load_default_ook_profile(&s_ctx.ook_cfg);
            break;

        case RADIO_MAIN_MODULATION_LORA:
        default:
            radio_main_apply_preset_cfg(s_ctx.lora_preset, &s_ctx.lora_cfg);
            break;
    }

    radio_main_apply_modulation_cfg();
}


static radio_packet_crc_t radio_main_map_fsk_crc(radio_main_crc_type_t crc_type)
{
    switch (crc_type)
    {
        case RADIO_MAIN_CRC_IBM:
            return RADIO_PACKET_CRC_IBM;

        case RADIO_MAIN_CRC_CCITT:
            return RADIO_PACKET_CRC_CCITT;

        case RADIO_MAIN_CRC_OFF:
        case RADIO_MAIN_CRC_SX1276:
        default:
            return RADIO_PACKET_CRC_OFF;
    }
}

static radio_address_filter_t radio_main_map_fsk_address_filter(radio_main_address_filter_t filter)
{
    switch (filter)
    {
        case RADIO_MAIN_ADDRESS_FILTER_NODE:
            return RADIO_ADDRESS_FILTER_NODE;

        case RADIO_MAIN_ADDRESS_FILTER_NODE_BROADCAST:
            return RADIO_ADDRESS_FILTER_NODE_BROADCAST;

        case RADIO_MAIN_ADDRESS_FILTER_NONE:
        default:
            return RADIO_ADDRESS_FILTER_OFF;
    }
}

static radio_ook_threshold_t radio_main_map_ook_threshold(radio_main_ook_threshold_t threshold)
{
    switch (threshold)
    {
        case RADIO_MAIN_OOK_THRESHOLD_FIXED:
            return RADIO_OOK_THRESHOLD_FIXED;

        case RADIO_MAIN_OOK_THRESHOLD_AVERAGE:
            return RADIO_OOK_THRESHOLD_AVERAGE;

        case RADIO_MAIN_OOK_THRESHOLD_PEAK:
        default:
            return RADIO_OOK_THRESHOLD_PEAK;
    }
}

/**
 * @brief Przekazuje aktywny profil modulacji do runtime backendu `radio_lib`.
 *
 * Funkcja wykonuje jawne mapowanie enum-ów warstwy aplikacji na enum-y
 * biblioteki radiowej. Dzięki temu menu i logika aplikacyjna mogą rozwijać się
 * niezależnie od szczegółów backendu SX1276.
 */
static bool radio_main_push_backend_cfg(void)
{
    radio_status_t st;

    if (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_FSK)
    {
        radio_fsk_cfg_t cfg;

        memset(&cfg, 0, sizeof(cfg));
        cfg.frequency_hz = s_ctx.fsk_cfg.frequency_hz;
        cfg.bitrate_bps = s_ctx.fsk_cfg.bitrate_bps;
        cfg.rx_bandwidth = s_ctx.fsk_cfg.rx_bandwidth;
        cfg.shaping = (radio_fsk_shaping_t)s_ctx.fsk_cfg.shaping;
        cfg.filter = (radio_fsk_filter_t)s_ctx.fsk_cfg.filter;
        cfg.tx_power_dbm = s_ctx.fsk_cfg.tx_power_dbm;
        cfg.preamble_len = s_ctx.fsk_cfg.preamble_len;
        cfg.sync_word_len = s_ctx.fsk_cfg.sync_word_len;
        cfg.sync_word = (uint32_t)(s_ctx.fsk_cfg.sync_word & 0xFFFFFFFFUL);
        cfg.address_filter = radio_main_map_fsk_address_filter(s_ctx.fsk_cfg.address_filter);
        cfg.crc_type = radio_main_map_fsk_crc(s_ctx.fsk_cfg.crc_type);
        cfg.data_whitening = s_ctx.fsk_cfg.data_whitening;

        radio_select_backend(RADIO_LIB_MODULATION_FSK);
        st = radio_set_fsk_cfg(&cfg);
        if (st != RADIO_OK)
        {
            printf("RADIO: FSK cfg reject st=%d\r\n", (int)st);
        }
        return (st == RADIO_OK);
    }

    if (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_OOK)
    {
        radio_ook_cfg_t cfg;

        memset(&cfg, 0, sizeof(cfg));
        cfg.frequency_hz = s_ctx.ook_cfg.frequency_hz;
        cfg.bitrate_bps = s_ctx.ook_cfg.bitrate_bps;
        cfg.rx_bandwidth = s_ctx.ook_cfg.rx_bandwidth;
        cfg.tx_power_dbm = s_ctx.ook_cfg.tx_power_dbm;
        cfg.preamble_len = s_ctx.ook_cfg.preamble_len;
        cfg.sync_word_len = s_ctx.ook_cfg.sync_word_len;
        cfg.sync_word = s_ctx.ook_cfg.sync_word;
        cfg.threshold = radio_main_map_ook_threshold(s_ctx.ook_cfg.threshold);
        cfg.threshold_value = s_ctx.ook_cfg.threshold_value;

        radio_select_backend(RADIO_LIB_MODULATION_OOK);
        st = radio_set_ook_cfg(&cfg);
        if (st != RADIO_OK)
        {
            printf("RADIO: OOK cfg reject st=%d\r\n", (int)st);
        }
        return (st == RADIO_OK);
    }

    radio_select_backend(RADIO_LIB_MODULATION_LORA);
    return true;
}

static bool radio_main_validate_frequency(uint32_t frequency_hz)
{
    return ((frequency_hz >= 863000000UL) &&
            (frequency_hz <= 870000000UL));
}

static bool radio_main_validate_tx_power(int32_t tx_power_dbm)
{
    return ((tx_power_dbm >= 2) && (tx_power_dbm <= 20));
}

static bool radio_main_validate_bitrate(uint32_t bitrate_bps)
{
    return ((bitrate_bps >= 600UL) &&
            (bitrate_bps <= 300000UL));
}

static bool radio_main_validate_preamble(uint32_t preamble_len)
{
    return ((preamble_len >= 1UL) &&
            (preamble_len <= 65535UL));
}

static bool radio_main_validate_hop_period(uint32_t period_ms)
{
    return ((period_ms >= 250UL) && (period_ms <= 60000UL));
}

static bool radio_main_validate_auto_ping_period(uint32_t period_ms)
{
    return ((period_ms == 1UL) ||
            (period_ms == 10UL) ||
            (period_ms == 100UL) ||
            (period_ms == 1000UL) ||
            (period_ms == 10000UL));
}

static bool radio_main_validate_auto_ping_mode(radio_main_auto_ping_mode_t mode)
{
    return ((mode == RADIO_MAIN_AUTO_PING_FRAME) ||
            (mode == RADIO_MAIN_AUTO_PING_RAW));
}

static bool radio_main_apply_option(radio_main_option_t option, uint32_t value)
{
    radio_main_ctx_t saved_ctx;
    bool reconfigure_now = false;
    bool was_initialized;

    saved_ctx = s_ctx;
    was_initialized = s_ctx.initialized;

    switch (option)
    {
        case RADIO_MAIN_OPTION_LORA_PRESET:
            if (value > 2UL)
            {
                return false;
            }
            s_ctx.lora_preset = (uint8_t)value;
            radio_main_apply_preset_cfg(s_ctx.lora_preset, &s_ctx.lora_cfg);
            reconfigure_now = (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_LORA);
            break;

        case RADIO_MAIN_OPTION_LORA_FREQ:
            if (!radio_main_validate_frequency(value))
            {
                return false;
            }
            s_ctx.lora_cfg.frequency_hz = value;
            reconfigure_now = (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_LORA);
            break;

        case RADIO_MAIN_OPTION_LORA_BW:
            if (!radio_main_is_supported_bw((uint8_t)value))
            {
                return false;
            }
            s_ctx.lora_cfg.bandwidth = (radio_lora_bw_t)value;
            reconfigure_now = (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_LORA);
            break;

        case RADIO_MAIN_OPTION_LORA_SF:
            if ((value < 6UL) || (value > 12UL))
            {
                return false;
            }
            s_ctx.lora_cfg.spreading_factor = (uint8_t)value;
            if (value == 6UL)
            {
                s_ctx.lora_cfg.implicit_header = true;
                s_ctx.lora_cfg.payload_len = LAVIET_FRAME_MAX_LEN;
            }
            reconfigure_now = (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_LORA);
            break;

        case RADIO_MAIN_OPTION_LORA_CR:
            if ((value < 5UL) || (value > 8UL))
            {
                return false;
            }
            s_ctx.lora_cfg.coding_rate = (uint8_t)value;
            reconfigure_now = (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_LORA);
            break;

        case RADIO_MAIN_OPTION_LORA_TX_POWER:
            if (!radio_main_validate_tx_power((int32_t)value))
            {
                return false;
            }
            s_ctx.lora_cfg.tx_power_dbm = (int8_t)value;
            reconfigure_now = (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_LORA);
            break;

        case RADIO_MAIN_OPTION_LORA_CRC:
            if ((value != (uint32_t)RADIO_MAIN_CRC_OFF) &&
                (value != (uint32_t)RADIO_MAIN_CRC_SX1276))
            {
                return false;
            }
            s_ctx.lora_cfg.crc_on = (value != (uint32_t)RADIO_MAIN_CRC_OFF);
            reconfigure_now = (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_LORA);
            break;

        case RADIO_MAIN_OPTION_LORA_PREAMBLE:
            if (!radio_main_validate_preamble(value))
            {
                return false;
            }
            s_ctx.lora_cfg.preamble_len = (uint16_t)value;
            reconfigure_now = (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_LORA);
            break;

        case RADIO_MAIN_OPTION_LORA_HEADER_MODE:
            if (value > (uint32_t)RADIO_MAIN_HEADER_IMPLICIT)
            {
                return false;
            }
            if ((value == (uint32_t)RADIO_MAIN_HEADER_EXPLICIT) &&
                (s_ctx.lora_cfg.spreading_factor == 6U))
            {
                return false;
            }
            s_ctx.lora_cfg.implicit_header = (value == (uint32_t)RADIO_MAIN_HEADER_IMPLICIT);
            if (s_ctx.lora_cfg.implicit_header)
            {
                s_ctx.lora_cfg.payload_len = LAVIET_FRAME_MAX_LEN;
            }
            else
            {
                s_ctx.lora_cfg.payload_len = 0U;
            }
            reconfigure_now = (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_LORA);
            break;

        case RADIO_MAIN_OPTION_LORA_IQ_INVERT:
            if (value > 1UL)
            {
                return false;
            }
            s_ctx.lora_cfg.invert_iq = (value != 0UL);
            reconfigure_now = (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_LORA);
            break;

        case RADIO_MAIN_OPTION_LORA_SYNC_WORD:
            s_ctx.lora_cfg.sync_word = (uint8_t)(value & 0xFFU);
            reconfigure_now = (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_LORA);
            break;

        case RADIO_MAIN_OPTION_LORA_RESET_DEFAULTS:
            s_ctx.lora_preset = 2U;
            radio_main_apply_preset_cfg(s_ctx.lora_preset, &s_ctx.lora_cfg);
            reconfigure_now = (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_LORA);
            break;

        case RADIO_MAIN_OPTION_FSK_SHAPING:
            if (value > (uint32_t)RADIO_MAIN_FSK_SHAPING_GMSK)
            {
                return false;
            }
            s_ctx.fsk_cfg.shaping = (radio_main_fsk_shaping_t)value;
            reconfigure_now = (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_FSK);
            break;

        case RADIO_MAIN_OPTION_FSK_FREQ:
            if (!radio_main_validate_frequency(value))
            {
                return false;
            }
            s_ctx.fsk_cfg.frequency_hz = value;
            reconfigure_now = (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_FSK);
            break;

        case RADIO_MAIN_OPTION_FSK_BITRATE:
            if (!radio_main_validate_bitrate(value))
            {
                return false;
            }
            s_ctx.fsk_cfg.bitrate_bps = value;
            reconfigure_now = (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_FSK);
            break;

        case RADIO_MAIN_OPTION_FSK_RX_BW:
            if (!radio_main_is_supported_bw((uint8_t)value))
            {
                return false;
            }
            s_ctx.fsk_cfg.rx_bandwidth = (radio_lora_bw_t)value;
            reconfigure_now = (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_FSK);
            break;

        case RADIO_MAIN_OPTION_FSK_FILTER:
            if (value > (uint32_t)RADIO_MAIN_FILTER_BT_03)
            {
                return false;
            }
            s_ctx.fsk_cfg.filter = (radio_main_filter_t)value;
            reconfigure_now = (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_FSK);
            break;

        case RADIO_MAIN_OPTION_FSK_TX_POWER:
            if (!radio_main_validate_tx_power((int32_t)value))
            {
                return false;
            }
            s_ctx.fsk_cfg.tx_power_dbm = (int8_t)value;
            reconfigure_now = (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_FSK);
            break;

        case RADIO_MAIN_OPTION_FSK_PREAMBLE:
            if (!radio_main_validate_preamble(value))
            {
                return false;
            }
            s_ctx.fsk_cfg.preamble_len = (uint16_t)value;
            reconfigure_now = (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_FSK);
            break;

        case RADIO_MAIN_OPTION_FSK_SYNC_LEN:
            if (value > 4UL)
            {
                return false;
            }
            s_ctx.fsk_cfg.sync_word_len = (uint8_t)value;
            reconfigure_now = (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_FSK);
            break;

        case RADIO_MAIN_OPTION_FSK_SYNC_WORD:
            s_ctx.fsk_cfg.sync_word = value;
            reconfigure_now = (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_FSK);
            break;

        case RADIO_MAIN_OPTION_FSK_ADDRESS_FILTER:
            if (value > (uint32_t)RADIO_MAIN_ADDRESS_FILTER_NODE_BROADCAST)
            {
                return false;
            }
            s_ctx.fsk_cfg.address_filter = (radio_main_address_filter_t)value;
            reconfigure_now = (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_FSK);
            break;

        case RADIO_MAIN_OPTION_FSK_CRC:
            if (value > (uint32_t)RADIO_MAIN_CRC_CCITT)
            {
                return false;
            }
            s_ctx.fsk_cfg.crc_type = (radio_main_crc_type_t)value;
            reconfigure_now = (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_FSK);
            break;

        case RADIO_MAIN_OPTION_FSK_WHITENING:
            if (value > 1UL)
            {
                return false;
            }
            s_ctx.fsk_cfg.data_whitening = (value != 0UL);
            reconfigure_now = (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_FSK);
            break;

        case RADIO_MAIN_OPTION_FSK_RESET_DEFAULTS:
            radio_main_load_default_fsk_profile(&s_ctx.fsk_cfg);
            reconfigure_now = (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_FSK);
            break;

        case RADIO_MAIN_OPTION_OOK_FREQ:
            if (!radio_main_validate_frequency(value))
            {
                return false;
            }
            s_ctx.ook_cfg.frequency_hz = value;
            reconfigure_now = (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_OOK);
            break;

        case RADIO_MAIN_OPTION_OOK_BITRATE:
            if (!radio_main_validate_bitrate(value))
            {
                return false;
            }
            s_ctx.ook_cfg.bitrate_bps = value;
            reconfigure_now = (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_OOK);
            break;

        case RADIO_MAIN_OPTION_OOK_TX_POWER:
            if (!radio_main_validate_tx_power((int32_t)value))
            {
                return false;
            }
            s_ctx.ook_cfg.tx_power_dbm = (int8_t)value;
            reconfigure_now = (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_OOK);
            break;

        case RADIO_MAIN_OPTION_OOK_RX_BW:
            if (!radio_main_is_supported_bw((uint8_t)value))
            {
                return false;
            }
            s_ctx.ook_cfg.rx_bandwidth = (radio_lora_bw_t)value;
            reconfigure_now = (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_OOK);
            break;

        case RADIO_MAIN_OPTION_OOK_PREAMBLE:
            if (!radio_main_validate_preamble(value))
            {
                return false;
            }
            s_ctx.ook_cfg.preamble_len = (uint16_t)value;
            reconfigure_now = (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_OOK);
            break;

        case RADIO_MAIN_OPTION_OOK_SYNC_LEN:
            if (value > 4UL)
            {
                return false;
            }
            s_ctx.ook_cfg.sync_word_len = (uint8_t)value;
            reconfigure_now = (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_OOK);
            break;

        case RADIO_MAIN_OPTION_OOK_SYNC_WORD:
            s_ctx.ook_cfg.sync_word = value;
            reconfigure_now = (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_OOK);
            break;

        case RADIO_MAIN_OPTION_OOK_THRESHOLD_TYPE:
            if (value > (uint32_t)RADIO_MAIN_OOK_THRESHOLD_AVERAGE)
            {
                return false;
            }
            s_ctx.ook_cfg.threshold = (radio_main_ook_threshold_t)value;
            reconfigure_now = (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_OOK);
            break;

        case RADIO_MAIN_OPTION_OOK_THRESHOLD_VALUE:
            if (value > 255UL)
            {
                return false;
            }
            s_ctx.ook_cfg.threshold_value = (uint8_t)value;
            reconfigure_now = (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_OOK);
            break;

        case RADIO_MAIN_OPTION_OOK_RESET_DEFAULTS:
            radio_main_load_default_ook_profile(&s_ctx.ook_cfg);
            reconfigure_now = (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_OOK);
            break;

        default:
            return false;
    }

    radio_main_apply_modulation_cfg();
    if (!was_initialized || !reconfigure_now)
    {
        return true;
    }

    if (radio_main_reconfigure_radio())
    {
        return true;
    }

    /*
     * Roll back the in-memory profile when the hardware rejects the new setting.
     * Without this, one bad combination leaves the app with a broken runtime config
     * and every later send/change keeps failing until a manual reset.
     */
    s_ctx = saved_ctx;
    radio_main_apply_modulation_cfg();
    s_ctx.initialized = false;
    (void)radio_main_radio_init_and_start();
    return false;
}

static bool radio_main_is_supported_bw(uint8_t bw_code)
{
    return ((bw_code == (uint8_t)RADIO_LORA_BW_7_8_KHZ) ||
            (bw_code == (uint8_t)RADIO_LORA_BW_10_4_KHZ) ||
            (bw_code == (uint8_t)RADIO_LORA_BW_15_6_KHZ) ||
            (bw_code == (uint8_t)RADIO_LORA_BW_20_8_KHZ) ||
            (bw_code == (uint8_t)RADIO_LORA_BW_31_25_KHZ) ||
            (bw_code == (uint8_t)RADIO_LORA_BW_41_7_KHZ) ||
            (bw_code == (uint8_t)RADIO_LORA_BW_62_5_KHZ) ||
            (bw_code == (uint8_t)RADIO_LORA_BW_125_KHZ) ||
            (bw_code == (uint8_t)RADIO_LORA_BW_250_KHZ) ||
            (bw_code == (uint8_t)RADIO_LORA_BW_500_KHZ));
}

static bool radio_main_reconfigure_radio(void)
{
    if (!s_ctx.initialized)
    {
        return false;
    }
    if (radio_get_state() == RADIO_STATE_TX)
    {
        if (!radio_main_tx_timed_out())
        {
            return false;
        }
        return radio_main_force_recover_radio("reconfigure while TX stuck");
    }

    radio_main_tx_clear();
    (void)radio_standby();
    (void)radio_deinit();
    s_ctx.initialized = false;
    return radio_main_radio_init_and_start();
}

/*
 * Forces a full backend teardown and optional hardware reset of SX1276.
 * This path is used when TX appears stuck, so it deliberately ignores the
 * normal "don't touch radio during TX" guard and rebuilds the runtime state
 * from the currently selected profile.
 */
static bool radio_main_force_recover_radio(const char *reason)
{
    uint8_t attempt;

    printf("RADIO: recovery start (%s) state=%d backend=%u\r\n",
           (reason != NULL) ? reason : "unknown",
           (int)radio_get_state(),
           (unsigned int)radio_get_backend());

    radio_main_tx_clear();

    for (attempt = 0U; attempt < RADIO_RECOVERY_RETRY_COUNT; attempt++)
    {
        (void)radio_sleep();
        (void)radio_standby();
        (void)radio_deinit();
        s_ctx.initialized = false;

        if ((s_ctx.hw.reset.port != NULL) && (s_ctx.hw.reset.pin != 0U))
        {
            HAL_GPIO_WritePin(s_ctx.hw.reset.port, s_ctx.hw.reset.pin, GPIO_PIN_RESET);
            osDelay(RADIO_RECOVERY_RESET_PULSE_MS);
            HAL_GPIO_WritePin(s_ctx.hw.reset.port, s_ctx.hw.reset.pin, GPIO_PIN_SET);
            osDelay(RADIO_RECOVERY_RESET_BOOT_MS);
        }

        radio_main_apply_modulation_cfg();
        if (radio_main_radio_init_and_start())
        {
            printf("RADIO: recovery OK attempt=%u\r\n", (unsigned int)(attempt + 1U));
            return true;
        }
    }

    printf("RADIO: recovery FAILED\r\n");
    return false;
}

/*
 * Performs an explicit SX1276 hardware reset through the RESET pin and then restores
 * the current runtime profile. This gives the hardware menu a true module reboot path.
 */
static bool radio_main_reset_module_internal(void)
{
    return radio_main_force_recover_radio("manual reset");
}

static bool radio_main_send_system_frame(laviet_frame_type_t type,
                                         uint32_t dst_id,
                                         const uint8_t *payload,
                                         uint16_t payload_len)
{
    return radio_main_send_system_frame_ex(type, dst_id, payload, payload_len, true, false);
}

static bool radio_main_send_system_frame_ex(laviet_frame_type_t type,
                                            uint32_t dst_id,
                                            const uint8_t *payload,
                                            uint16_t payload_len,
                                            bool request_ack,
                                            bool silent)
{
    laviet_frame_t frame;
    uint8_t raw[LAVIET_FRAME_MAX_LEN];
    uint8_t raw_len = 0U;
    uint8_t enc_key[16];
    uint8_t hmac_key[32];
    uint16_t dst16;
    uint16_t peer_id;
    uint32_t rx_counter;
    uint32_t tx_counter;
    bool encrypted = false;
    bool use_pair_link;
    security_frame_key_mode_t key_mode = SECURITY_FRAME_KEY_MODE_SHARED;
    bool keys_ok = false;

    if (!s_ctx.initialized)
    {
        return false;
    }
    if (!s_ctx.crypto_ready)
    {
        radio_main_set_last_error("Crypto not ready");
        return false;
    }
    if (((payload == NULL) && (payload_len > 0U)) || (payload_len > LAVIET_MAX_PAYLOAD))
    {
        radio_main_set_last_error("Payload >16B");
        return false;
    }
    if (dst_id == 0xFFFFFFFFUL)
    {
        dst16 = LAVIET_BROADCAST_ID;
    }
    else if (dst_id > 0xFFFFUL)
    {
        radio_main_set_last_error("Bad dst_id");
        return false;
    }
    else
    {
        dst16 = (uint16_t)dst_id;
    }
    if (dst16 == 0U)
    {
        radio_main_set_last_error("Bad dst_id");
        return false;
    }
    if (((type == LAVIET_TYPE_DATA) || (type == LAVIET_TYPE_RESP)) &&
        (dst16 == LAVIET_BROADCAST_ID))
    {
        radio_main_set_last_error("Broadcast disabled");
        return false;
    }

    if (request_ack &&
        s_ctx.ack_pending.active &&
        (dst_id != 0xFFFFFFFFUL) &&
        ((type == LAVIET_TYPE_DATA) || (type == LAVIET_TYPE_RESP)))
    {
        radio_main_set_last_error("ACK pending");
        return false;
    }

    memset(&frame, 0, sizeof(frame));
    frame.ver_type = laviet_frame_ver_type(type);
    frame.flags = 0U;
    frame.src_id = s_ctx.node_id;
    frame.dst_id = dst16;
    frame.msg_id = s_ctx.next_msg_id;
    s_ctx.next_msg_id++;
    if (s_ctx.next_msg_id == 0U)
    {
        s_ctx.next_msg_id = 1U;
    }

    if (!security_main_get_gateway_counter(&rx_counter, &tx_counter))
    {
        rx_counter = s_ctx.gateway_rx_counter;
        tx_counter = s_ctx.gateway_tx_counter;
    }
    tx_counter++;
    if (tx_counter == 0UL)
    {
        tx_counter = 1UL;
    }
    frame.counter = tx_counter;
    frame.payload_len = (uint8_t)payload_len;
    if ((payload_len > 0U) && (payload != NULL))
    {
        memcpy(frame.payload, payload, payload_len);
    }

    if (dst16 == LAVIET_BROADCAST_ID)
    {
        frame.flags |= LAVIET_FLAG_BROADCAST;
    }
    else if (request_ack &&
             ((type == LAVIET_TYPE_DATA) || (type == LAVIET_TYPE_RESP)))
    {
        frame.flags |= LAVIET_FLAG_ACK_REQUIRED;
    }

    switch (type)
    {
        case LAVIET_TYPE_DATA:
        case LAVIET_TYPE_RESP:
            encrypted = s_ctx.coding_enabled && (dst16 != LAVIET_BROADCAST_ID);
            break;
        case LAVIET_TYPE_CFG:
            frame.flags |= LAVIET_FLAG_CONFIG_ACCESS;
            encrypted = true;
            break;
        case LAVIET_TYPE_COUNTER_SYNC:
            frame.flags |= LAVIET_FLAG_COUNTER_OVERRIDE;
            encrypted = true;
            break;
        case LAVIET_TYPE_KEY_ROTATE:
            frame.flags |= LAVIET_FLAG_KEY_UPDATE;
            encrypted = true;
            break;
        case LAVIET_TYPE_ACK:
            frame.flags |= LAVIET_FLAG_IS_ACK;
            break;
        case LAVIET_TYPE_PAIR_REQ:
        case LAVIET_TYPE_PAIR_RESP:
            frame.flags |= LAVIET_FLAG_PAIRING;
            break;
        case LAVIET_TYPE_ERROR:
        default:
            break;
    }
    if (encrypted)
    {
        frame.flags |= LAVIET_FLAG_ENCRYPTED;
    }

    peer_id = (dst16 == LAVIET_BROADCAST_ID) ? LAVIET_BROADCAST_ID : dst16;
    use_pair_link = ((type != LAVIET_TYPE_PAIR_REQ) &&
                     (type != LAVIET_TYPE_PAIR_RESP) &&
                     (type != LAVIET_TYPE_ERROR) &&
                     (dst16 != LAVIET_BROADCAST_ID));
    if (use_pair_link)
    {
        key_mode = SECURITY_FRAME_KEY_MODE_PAIR_V1_32;
    }
    if ((dst16 == LAVIET_GATEWAY_ID) &&
        use_pair_link &&
        radio_main_get_gateway_cached_frame_keys(key_mode, enc_key, hmac_key))
    {
        keys_ok = true;
    }
    else
    {
        keys_ok = security_main_get_frame_keys_mode(s_ctx.node_id, peer_id, key_mode, enc_key, hmac_key);
    }
    if (!keys_ok)
    {
        laviet_secure_zero(enc_key, sizeof(enc_key));
        laviet_secure_zero(hmac_key, sizeof(hmac_key));
        radio_main_set_last_error("No frame key");
        return false;
    }
    if (encrypted && !laviet_aes_ctr_crypt(frame.payload, frame.payload_len, enc_key, &frame))
    {
        laviet_secure_zero(enc_key, sizeof(enc_key));
        laviet_secure_zero(hmac_key, sizeof(hmac_key));
        radio_main_set_last_error("AES failed");
        return false;
    }
    if (!laviet_frame_hmac_sha256(&frame, hmac_key, frame.mac_tag))
    {
        laviet_secure_zero(enc_key, sizeof(enc_key));
        laviet_secure_zero(hmac_key, sizeof(hmac_key));
        radio_main_set_last_error("HMAC failed");
        return false;
    }
    if (!silent && (dst16 == LAVIET_GATEWAY_ID) && use_pair_link)
    {
        radio_main_log_hmac_debug("RADIO TX HMAC DBG",
                                  &frame,
                                  key_mode,
                                  s_ctx.gateway_pair_code_valid ? s_ctx.gateway_pair_code : NULL,
                                  s_ctx.gateway_pair_code_valid ? s_ctx.gateway_pair_code_len : 0U,
                                  hmac_key,
                                  frame.mac_tag);
    }
    if (laviet_frame_encode(&frame, raw, sizeof(raw), &raw_len) != LAVIET_STATUS_OK)
    {
        laviet_secure_zero(enc_key, sizeof(enc_key));
        laviet_secure_zero(hmac_key, sizeof(hmac_key));
        radio_main_set_last_error("Frame encode failed");
        return false;
    }

    if (!radio_main_send_raw_with_retry_ex(raw, raw_len, silent))
    {
        laviet_secure_zero(enc_key, sizeof(enc_key));
        laviet_secure_zero(hmac_key, sizeof(hmac_key));
        return false;
    }
    if (request_ack && !radio_main_ack_track_start(&frame, raw, raw_len))
    {
        laviet_secure_zero(enc_key, sizeof(enc_key));
        laviet_secure_zero(hmac_key, sizeof(hmac_key));
        return false;
    }

    s_ctx.gateway_rx_counter = rx_counter;
    s_ctx.gateway_tx_counter = tx_counter;
    (void)security_main_commit_gateway_counter(rx_counter, tx_counter);
    laviet_secure_zero(enc_key, sizeof(enc_key));
    laviet_secure_zero(hmac_key, sizeof(hmac_key));
    return true;
}

static bool radio_main_send_ack(const laviet_frame_t *frame)
{
    uint8_t payload[LAVIET_ACK_PAYLOAD_LEN];
    bool sent;

    if (frame == NULL)
    {
        printf("RADIO ACK skip reason=null-frame\r\n");
        return false;
    }

    if (frame->dst_id != s_ctx.node_id)
    {
        printf("RADIO ACK skip reason=dst src=0x%04X dst=0x%04X node=0x%04X msg=0x%04X flags=0x%02X\r\n",
               (unsigned int)frame->src_id,
               (unsigned int)frame->dst_id,
               (unsigned int)s_ctx.node_id,
               (unsigned int)frame->msg_id,
               (unsigned int)frame->flags);
        return false;
    }

    if (frame->dst_id == LAVIET_BROADCAST_ID)
    {
        printf("RADIO ACK skip reason=broadcast src=0x%04X msg=0x%04X flags=0x%02X\r\n",
               (unsigned int)frame->src_id,
               (unsigned int)frame->msg_id,
               (unsigned int)frame->flags);
        return false;
    }

    if ((frame->flags & LAVIET_FLAG_ACK_REQUIRED) == 0U)
    {
        printf("RADIO ACK skip reason=no-ack-required src=0x%04X dst=0x%04X msg=0x%04X flags=0x%02X\r\n",
               (unsigned int)frame->src_id,
               (unsigned int)frame->dst_id,
               (unsigned int)frame->msg_id,
               (unsigned int)frame->flags);
        return false;
    }

    if (laviet_frame_type(frame) == LAVIET_TYPE_ACK)
    {
        printf("RADIO ACK skip reason=ack-frame src=0x%04X msg=0x%04X flags=0x%02X\r\n",
               (unsigned int)frame->src_id,
               (unsigned int)frame->msg_id,
               (unsigned int)frame->flags);
        return false;
    }

    if (!laviet_frame_write_ack_payload(frame->msg_id, frame->counter, payload))
    {
        printf("RADIO ACK build failed src=0x%04X msg=0x%04X counter=%lu\r\n",
               (unsigned int)frame->src_id,
               (unsigned int)frame->msg_id,
               (unsigned long)frame->counter);
        return false;
    }

    printf("RADIO ACK tx delay=%lu ms dst=0x%04X ack_msg=0x%04X ack_counter=%lu\r\n",
           (unsigned long)RADIO_ACK_TX_DELAY_MS,
           (unsigned int)frame->src_id,
           (unsigned int)frame->msg_id,
           (unsigned long)frame->counter);
    osDelay(RADIO_ACK_TX_DELAY_MS);
    sent = radio_main_send_system_frame(LAVIET_TYPE_ACK, frame->src_id, payload, sizeof(payload));
    printf("RADIO ACK tx %s dst=0x%04X ack_msg=0x%04X ack_counter=%lu\r\n",
           sent ? "ok" : "failed",
           (unsigned int)frame->src_id,
           (unsigned int)frame->msg_id,
           (unsigned long)frame->counter);

    return sent;
}

/*
 * Starts TX and retries once after a forced radio recovery if the backend looks
 * wedged. This protects the user-facing send path from leaving SX1276 stuck in TX.
 */
static bool radio_main_send_current_backend_with_retry_ex(const uint8_t *data, uint8_t len, bool silent)
{
    radio_status_t st;

    if ((data == NULL) || (len == 0U))
    {
        return false;
    }

    if ((s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_FSK) &&
        (((uint16_t)len + 1U) > 64U))
    {
        printf("RADIO FSK TX ERROR: message too long (%u B raw, max 63 B payload FIFO path)\r\n",
               (unsigned int)len);
        radio_main_set_last_error("FSK msg too long");
        radio_main_notify(MENU_NOTIFICATION_ERROR, "FSK msg too long");
        return false;
    }

    if ((s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_OOK) &&
        (((uint16_t)len + 1U) > 64U))
    {
        printf("RADIO OOK TX ERROR: message too long (%u B raw, max 63 B payload FIFO path)\r\n",
               (unsigned int)len);
        radio_main_set_last_error("OOK msg too long");
        radio_main_notify(MENU_NOTIFICATION_ERROR, "OOK msg too long");
        return false;
    }

    if (!s_ctx.initialized && !radio_main_force_recover_radio("send while uninitialized"))
    {
        return false;
    }

    st = radio_send_async(data, len);
    if (st == RADIO_OK)
    {
        if (!silent)
        {
            radio_main_print_tx_frame(data, len, false);
        }
        radio_main_tx_mark_started_ex(len, silent);
        return true;
    }

    if ((st == RADIO_EBUS) &&
        (radio_get_state() == RADIO_STATE_TX) &&
        !radio_main_tx_timed_out())
    {
        return false;
    }

    if ((st != RADIO_EBUS) && (st != RADIO_EHW) && (st != RADIO_ESTATE))
    {
        return false;
    }

    if (!radio_main_force_recover_radio("tx start failed"))
    {
        return false;
    }

    st = radio_send_async(data, len);
    if (st == RADIO_OK)
    {
        if (!silent)
        {
            radio_main_print_tx_frame(data, len, true);
        }
        radio_main_tx_mark_started_ex(len, silent);
        return true;
    }

    return false;
}

static bool radio_main_send_raw_with_retry(const uint8_t *data, uint8_t len)
{
    return radio_main_send_raw_with_retry_ex(data, len, false);
}

static bool radio_main_send_raw_with_retry_ex(const uint8_t *data, uint8_t len, bool silent)
{
    if ((data == NULL) || (len == 0U))
    {
        return false;
    }
    if (s_ctx.tx_in_progress)
    {
        return false;
    }
    if (!radio_main_switch_backend(s_ctx.modulation_id))
    {
        return false;
    }

    return radio_main_send_current_backend_with_retry_ex(data, len, silent);
}

static bool radio_main_send_template_internal(uint8_t group_id, uint8_t msg_id, uint32_t dst_id)
{
    const char *msg;
    uint16_t len;

    if ((group_id >= 3U) || (msg_id >= 3U))
    {
        return false;
    }

    msg = s_template_groups[group_id][msg_id];
    len = (uint16_t)strlen(msg);
    if ((len == 0U) || (len > LAVIET_MAX_PAYLOAD))
    {
        radio_main_set_last_error("Payload >16B");
        return false;
    }

    return radio_main_send_system_frame(LAVIET_TYPE_DATA, dst_id, (const uint8_t *)msg, len);
}

static void radio_main_post_rx_notification(int16_t rssi_dbm,
                                            uint16_t src_id,
                                            uint16_t dst_id,
                                            const uint8_t *payload,
                                            uint8_t payload_len,
                                            laviet_frame_type_t frame_type)
{
    char text_buf[21];
    uint8_t text_len;
    menu_notification_t n;

    memset(&n, 0, sizeof(n));
    n.type = MENU_NOTIFICATION_RX;
    n.rssi_dbm = rssi_dbm;
    n.device_code = (dst_id == LAVIET_BROADCAST_ID) ? LAVIET_BROADCAST_ID : src_id;
    n.reply_device_code = src_id;

    text_len = radio_main_format_payload_text(payload, payload_len, text_buf, sizeof(text_buf));
    if (text_len > 0U)
    {
        memcpy(n.text, text_buf, text_len);
        n.text[text_len] = '\0';
    }
    else
    {
        (void)snprintf(n.text, sizeof(n.text), "TYPE:%u", (unsigned int)frame_type);
    }

    printf("RADIO RX NOTIFY type=%s src=0x%04X dst=0x%04X rssi=%d text=\"%s\"\r\n",
           radio_main_frame_type_text(frame_type),
           (unsigned int)src_id,
           (unsigned int)dst_id,
           (int)rssi_dbm,
           n.text);
    (void)menu_main_post_notification(&n);
}

static const char *radio_main_frame_type_text(laviet_frame_type_t type)
{
    switch (type)
    {
        case LAVIET_TYPE_DATA:
            return "DATA";
        case LAVIET_TYPE_ACK:
            return "ACK";
        case LAVIET_TYPE_RESP:
            return "RESP";
        case LAVIET_TYPE_PAIR_REQ:
            return "PAIR_REQ";
        case LAVIET_TYPE_PAIR_RESP:
            return "PAIR_RESP";
        case LAVIET_TYPE_CFG:
            return "CFG";
        case LAVIET_TYPE_COUNTER_SYNC:
            return "COUNTER_SYNC";
        case LAVIET_TYPE_KEY_ROTATE:
            return "KEY_ROTATE";
        case LAVIET_TYPE_ERROR:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
}

static const char *radio_main_key_mode_text(security_frame_key_mode_t mode)
{
    switch (mode)
    {
        case SECURITY_FRAME_KEY_MODE_PAIR_V1_32:
            return "PAIR32";
        case SECURITY_FRAME_KEY_MODE_SHARED:
        default:
            return "SHARED";
    }
}

static bool radio_main_laviet_verify_rx(const laviet_frame_t *frame, uint8_t enc_key[16])
{
    security_frame_key_mode_t candidates[2];
    security_frame_key_mode_t matched_mode = SECURITY_FRAME_KEY_MODE_SHARED;
    uint8_t trial_enc[16];
    uint8_t hmac_key[32];
    uint8_t expected[LAVIET_MAC_TAG_LEN];
    laviet_frame_type_t frame_type;
    uint16_t peer_id;
    bool use_pair_link;
    bool ok = false;
    uint8_t candidate_count = 0U;
    uint8_t idx;

    if ((frame == NULL) || (enc_key == NULL) || !s_ctx.crypto_ready)
    {
        return false;
    }

    memset(enc_key, 0, 16U);
    memset(trial_enc, 0, sizeof(trial_enc));
    memset(hmac_key, 0, sizeof(hmac_key));
    memset(expected, 0, sizeof(expected));
    frame_type = laviet_frame_type(frame);
    peer_id = laviet_frame_is_broadcast(frame) ? LAVIET_BROADCAST_ID : frame->src_id;
    use_pair_link = ((frame_type != LAVIET_TYPE_PAIR_REQ) &&
                     (frame_type != LAVIET_TYPE_PAIR_RESP) &&
                     (frame_type != LAVIET_TYPE_ERROR) &&
                     (peer_id != LAVIET_BROADCAST_ID));

    if (radio_main_try_broadcast_group_rx(frame, enc_key))
    {
        laviet_secure_zero(trial_enc, sizeof(trial_enc));
        laviet_secure_zero(hmac_key, sizeof(hmac_key));
        laviet_secure_zero(expected, sizeof(expected));
        return true;
    }

    if (!use_pair_link)
    {
        candidates[candidate_count++] = SECURITY_FRAME_KEY_MODE_SHARED;
    }
    else
    {
        candidates[candidate_count++] = SECURITY_FRAME_KEY_MODE_PAIR_V1_32;
    }

    for (idx = 0U; idx < candidate_count; idx++)
    {
        bool candidate_ok = false;
        bool mac_match = false;

        memset(trial_enc, 0, sizeof(trial_enc));
        memset(hmac_key, 0, sizeof(hmac_key));
        memset(expected, 0, sizeof(expected));

        if ((peer_id == LAVIET_GATEWAY_ID) &&
            use_pair_link &&
            radio_main_get_gateway_cached_frame_keys(candidates[idx], trial_enc, hmac_key))
        {
            candidate_ok = true;
        }
        else if (security_main_get_frame_keys_mode(s_ctx.node_id, peer_id, candidates[idx], trial_enc, hmac_key))
        {
            candidate_ok = true;
        }
        else if (frame->src_id == LAVIET_GATEWAY_ID)
        {
            trusted_info_t gateway_info;

            memset(&gateway_info, 0, sizeof(gateway_info));
            if (security_main_cmd_get_device(0U, &gateway_info) && gateway_info.in_use)
            {
                printf("RADIO RX HMAC no-key mode=%s src=0x%04X dst=0x%04X peer=0x%04X slot0=0x%04lX code_len=%u\r\n",
                       radio_main_key_mode_text(candidates[idx]),
                       (unsigned int)frame->src_id,
                       (unsigned int)frame->dst_id,
                       (unsigned int)peer_id,
                       (unsigned long)gateway_info.node_id,
                       (unsigned int)gateway_info.code_len);
            }
            else
            {
                printf("RADIO RX HMAC no-key mode=%s src=0x%04X dst=0x%04X peer=0x%04X slot0=<empty>\r\n",
                       radio_main_key_mode_text(candidates[idx]),
                       (unsigned int)frame->src_id,
                       (unsigned int)frame->dst_id,
                       (unsigned int)peer_id);
            }
        }

        if (candidate_ok &&
            laviet_frame_hmac_sha256(frame, hmac_key, expected))
        {
            if (frame->src_id == LAVIET_GATEWAY_ID)
            {
                radio_main_log_hmac_debug("RADIO RX HMAC DBG",
                                          frame,
                                          candidates[idx],
                                          ((peer_id == LAVIET_GATEWAY_ID) && s_ctx.gateway_pair_code_valid) ?
                                              s_ctx.gateway_pair_code : NULL,
                                          ((peer_id == LAVIET_GATEWAY_ID) && s_ctx.gateway_pair_code_valid) ?
                                              s_ctx.gateway_pair_code_len : 0U,
                                          hmac_key,
                                          expected);
            }
            mac_match = laviet_mac_equal(expected, frame->mac_tag);
        }

        if (candidate_ok && mac_match)
        {
            memcpy(enc_key, trial_enc, sizeof(trial_enc));
            matched_mode = candidates[idx];
            ok = true;
            break;
        }
    }

    if (ok && use_pair_link && (peer_id == LAVIET_GATEWAY_ID))
    {
        if (!s_ctx.gateway_key_mode_known || (s_ctx.gateway_key_mode != matched_mode))
        {
            printf("RADIO RX gateway key mode=%s\r\n", radio_main_key_mode_text(matched_mode));
        }
        s_ctx.gateway_key_mode_known = true;
        s_ctx.gateway_key_mode = matched_mode;
    }

    laviet_secure_zero(trial_enc, sizeof(trial_enc));
    laviet_secure_zero(hmac_key, sizeof(hmac_key));
    laviet_secure_zero(expected, sizeof(expected));
    if (!ok)
    {
        laviet_secure_zero(enc_key, 16U);
    }
    return ok;
}

static void radio_main_handle_events(void)
{
    uint32_t events = radio_take_events();
    radio_packet_t pkt;

    if ((events & RADIO_EVENT_RX_DONE) != 0U)
    {
        if (radio_get_last_packet(&pkt))
        {
            radio_main_handle_rx_packet(&pkt);
        }
    }

    if ((events & RADIO_EVENT_TX_DONE) != 0U)
    {
        if (!s_ctx.tx_silent)
        {
            printf("RADIO EVT: TX_DONE\r\n");
        }
        radio_main_tx_clear();
        radio_main_ensure_rx_continuous();
    }
    if ((events & RADIO_EVENT_CRC_ERR) != 0U)
    {
        printf("RADIO EVT: CRC_ERR\r\n");
    }
    if ((events & RADIO_EVENT_RX_TIMEOUT) != 0U)
    {
        printf("RADIO EVT: RX_TIMEOUT\r\n");
    }
    if ((events & RADIO_EVENT_HW_ERROR) != 0U)
    {
        printf("RADIO EVT: HW_ERROR\r\n");
        if (!radio_main_force_recover_radio("backend HW error"))
        {
            radio_main_notify(MENU_NOTIFICATION_ERROR, "Radio recovery failed");
        }
    }
}

static bool radio_main_source_is_trusted(uint16_t src_id)
{
    trusted_info_t info;
    uint8_t idx;

    if ((src_id == 0U) || (src_id == LAVIET_BROADCAST_ID))
    {
        return false;
    }

    for (idx = 0U; idx < RADIO_TRUSTED_DEVICE_SLOTS; idx++)
    {
        memset(&info, 0, sizeof(info));
        if (security_main_cmd_get_device(idx, &info) &&
            info.in_use &&
            (info.node_id == src_id))
        {
            return true;
        }
    }

    return false;
}

static bool radio_main_source_is_allowed_for_data(const laviet_frame_t *frame)
{
    if (frame == NULL)
    {
        return false;
    }

    if ((frame->src_id == LAVIET_GATEWAY_ID) &&
        (frame->dst_id == LAVIET_BROADCAST_ID))
    {
        return true;
    }

    return radio_main_source_is_trusted(frame->src_id);
}

static void radio_main_handle_rx_packet(const radio_packet_t *pkt)
{
    laviet_frame_t frame;
    laviet_frame_t frame_decoded;
    laviet_frame_status_t frame_status;
    bool for_me;
    uint8_t enc_key[16];
    uint32_t rx_counter;
    uint32_t tx_counter;
    laviet_frame_type_t frame_type;

    if ((pkt == NULL) || (pkt->length == 0U))
    {
        return;
    }

    printf("RADIO RX len=%u RSSI=%d SNR=%d\r\n", pkt->length, pkt->rssi_dbm, pkt->snr_db);
    radio_main_print_rx_ascii(pkt->data, pkt->length);
    radio_main_print_hex_bytes("RADIO RX HEX: ", pkt->data, pkt->length);
    (void)security_main_log_message(pkt->rssi_dbm, pkt->data, pkt->length);

    frame_status = laviet_frame_decode(pkt->data, pkt->length, &frame);
    if (frame_status != LAVIET_STATUS_OK)
    {
        printf("RADIO RX non-LAVIET drop status=%d\r\n", (int)frame_status);
        return;
    }

    if (!radio_main_laviet_verify_rx(&frame, enc_key))
    {
        if (frame.src_id == LAVIET_GATEWAY_ID)
        {
            radio_main_log_gateway_rx_frame(&frame, &frame, pkt->rssi_dbm, pkt->snr_db);
            printf("RADIO RX GATEWAY NOTE HMAC failed; PAYLOAD DEC fields below are still raw ciphertext\r\n");
        }
        printf("RADIO RX HMAC drop src=0x%04X dst=0x%04X msg=0x%04X\r\n",
               (unsigned int)frame.src_id,
               (unsigned int)frame.dst_id,
               (unsigned int)frame.msg_id);
        return;
    }

    if (!security_main_get_gateway_counter(&rx_counter, &tx_counter))
    {
        rx_counter = s_ctx.gateway_rx_counter;
        tx_counter = s_ctx.gateway_tx_counter;
    }

    frame_type = laviet_frame_type(&frame);
    if ((frame.src_id == LAVIET_GATEWAY_ID) &&
        (frame_type != LAVIET_TYPE_COUNTER_SYNC))
    {
        if ((frame.counter == 0UL) && (rx_counter == 0UL))
        {
            printf("RADIO RX compat allow src=0x%04X counter=0 while last=0\r\n",
                   (unsigned int)frame.src_id);
        }
        else if (frame.counter <= rx_counter)
        {
            printf("RADIO RX REPLAY bypass src=0x%04X counter=%lu last=%lu\r\n",
                   (unsigned int)frame.src_id,
                   (unsigned long)frame.counter,
                   (unsigned long)rx_counter);
        }
    }

    frame_decoded = frame;
    if ((frame_decoded.flags & LAVIET_FLAG_ENCRYPTED) != 0U)
    {
        if (!laviet_aes_ctr_crypt(frame_decoded.payload,
                                  frame_decoded.payload_len,
                                  enc_key,
                                  &frame_decoded))
        {
            printf("RADIO RX AES drop src=0x%04X\r\n", (unsigned int)frame_decoded.src_id);
            laviet_secure_zero(enc_key, 16U);
            return;
        }
    }
    laviet_secure_zero(enc_key, 16U);

    for_me = (frame_decoded.dst_id == s_ctx.node_id) ||
             (frame_decoded.dst_id == LAVIET_BROADCAST_ID);
    if (!for_me)
    {
        printf("RADIO RX not-for-me src=0x%04X dst=0x%04X\r\n",
               (unsigned int)frame_decoded.src_id,
               (unsigned int)frame_decoded.dst_id);
        return;
    }
    if (frame_decoded.src_id == LAVIET_GATEWAY_ID)
    {
        radio_main_log_gateway_rx_frame(&frame, &frame_decoded, pkt->rssi_dbm, pkt->snr_db);
    }
    if (((frame_type == LAVIET_TYPE_DATA) || (frame_type == LAVIET_TYPE_RESP)) &&
        !radio_main_source_is_allowed_for_data(&frame_decoded))
    {
        printf("RADIO RX outside-network drop src=0x%04X dst=0x%04X type=%s\r\n",
               (unsigned int)frame_decoded.src_id,
               (unsigned int)frame_decoded.dst_id,
               radio_main_frame_type_text(frame_type));
        return;
    }

    printf("RADIO RX OK type=%s src=0x%04X dst=0x%04X msg=0x%04X counter=%lu flags=0x%02X len=%u rssi=%d snr=%d\r\n",
           radio_main_frame_type_text(frame_type),
           (unsigned int)frame_decoded.src_id,
           (unsigned int)frame_decoded.dst_id,
           (unsigned int)frame_decoded.msg_id,
           (unsigned long)frame_decoded.counter,
           (unsigned int)frame_decoded.flags,
           (unsigned int)frame_decoded.payload_len,
           (int)pkt->rssi_dbm,
           (int)pkt->snr_db);

    if (frame_decoded.payload_len > 0U)
    {
        char text_buf[21];

        radio_main_print_hex_bytes("RADIO RX DEC HEX: ", frame_decoded.payload, frame_decoded.payload_len);
        if (radio_main_format_payload_text(frame_decoded.payload,
                                           frame_decoded.payload_len,
                                           text_buf,
                                           sizeof(text_buf)) > 0U)
        {
            printf("RADIO RX DEC TXT: \"%s\"\r\n", text_buf);
            if (((frame_type == LAVIET_TYPE_DATA) || (frame_type == LAVIET_TYPE_RESP)) &&
                ((frame_decoded.flags & LAVIET_FLAG_ENCRYPTED) != 0U) &&
                (strncmp(text_buf, "HEX:", 4U) == 0))
            {
                printf("RADIO RX WARN non-printable payload after decrypt; check gateway nonce fields src/dst/msg/counter\r\n");
            }
        }
    }

    if (for_me)
    {
        if (frame_type == LAVIET_TYPE_DATA)
        {
            if (frame_decoded.dst_id == s_ctx.node_id)
            {
                bool ack_ok = radio_main_send_ack(&frame_decoded);

                if (((frame_decoded.flags & LAVIET_FLAG_ACK_REQUIRED) != 0U) && !ack_ok)
                {
                    printf("RADIO RX DATA ACK failed src=0x%04X msg=0x%04X counter=%lu flags=0x%02X\r\n",
                           (unsigned int)frame_decoded.src_id,
                           (unsigned int)frame_decoded.msg_id,
                           (unsigned long)frame_decoded.counter,
                           (unsigned int)frame_decoded.flags);
                }
                (void)security_main_get_gateway_counter(&rx_counter, &tx_counter);
            }
            if (frame_decoded.payload_len > 0U)
            {
                bool stored = radio_main_push_payload_to_monitor(pkt->rssi_dbm,
                                                                 frame_decoded.src_id,
                                                                 frame_decoded.payload,
                                                                 frame_decoded.payload_len);

                printf("RADIO RX MONITOR type=DATA stored=%u src=0x%04X len=%u\r\n",
                       stored ? 1U : 0U,
                       (unsigned int)frame_decoded.src_id,
                       (unsigned int)frame_decoded.payload_len);
            }
            radio_main_post_rx_notification(pkt->rssi_dbm,
                                            frame_decoded.src_id,
                                            frame_decoded.dst_id,
                                            frame_decoded.payload,
                                            frame_decoded.payload_len,
                                            frame_type);
        }
        else if (frame_type == LAVIET_TYPE_RESP)
        {
            if ((frame_decoded.dst_id == s_ctx.node_id) &&
                ((frame_decoded.flags & LAVIET_FLAG_ACK_REQUIRED) != 0U))
            {
                bool ack_ok = radio_main_send_ack(&frame_decoded);

                if (!ack_ok)
                {
                    printf("RADIO RX RESP ACK failed src=0x%04X msg=0x%04X counter=%lu flags=0x%02X\r\n",
                           (unsigned int)frame_decoded.src_id,
                           (unsigned int)frame_decoded.msg_id,
                           (unsigned long)frame_decoded.counter,
                           (unsigned int)frame_decoded.flags);
                }
            }
            if (frame_decoded.payload_len > 0U)
            {
                bool stored = radio_main_push_payload_to_monitor(pkt->rssi_dbm,
                                                                 frame_decoded.src_id,
                                                                 frame_decoded.payload,
                                                                 frame_decoded.payload_len);

                printf("RADIO RX MONITOR type=RESP stored=%u src=0x%04X len=%u\r\n",
                       stored ? 1U : 0U,
                       (unsigned int)frame_decoded.src_id,
                       (unsigned int)frame_decoded.payload_len);
            }
            radio_main_post_rx_notification(pkt->rssi_dbm,
                                            frame_decoded.src_id,
                                            frame_decoded.dst_id,
                                            frame_decoded.payload,
                                            frame_decoded.payload_len,
                                            frame_type);
        }
        else if (frame_type == LAVIET_TYPE_COUNTER_SYNC)
        {
            uint32_t new_counter;

            if ((frame_decoded.src_id != LAVIET_GATEWAY_ID) ||
                (frame_decoded.payload_len != LAVIET_COUNTER_SYNC_PAYLOAD_LEN))
            {
                printf("RADIO RX bad COUNTER_SYNC\r\n");
                return;
            }
            new_counter = ((uint32_t)frame_decoded.payload[0] << 24) |
                          ((uint32_t)frame_decoded.payload[1] << 16) |
                          ((uint32_t)frame_decoded.payload[2] << 8) |
                          (uint32_t)frame_decoded.payload[3];
            s_ctx.gateway_rx_counter = new_counter;
            s_ctx.gateway_tx_counter = tx_counter;
            (void)security_main_commit_gateway_counter(s_ctx.gateway_rx_counter,
                                                       s_ctx.gateway_tx_counter);
            (void)radio_main_send_ack(&frame_decoded);
            radio_main_notify(MENU_NOTIFICATION_WARNING, "Counter sync");
        }
        else if (frame_type == LAVIET_TYPE_PAIR_REQ)
        {
            if (s_ctx.pairing_active && !s_ctx.pairing_pending)
            {
                char pair_note[21];
                char code_text[12];
                const uint8_t *pair_code = NULL;
                uint8_t pair_code_len = 0U;
                trusted_info_t gateway_info;

                if (!radio_main_pair_payload_parse(frame_decoded.payload,
                                                   (uint8_t)frame_decoded.payload_len,
                                                   &pair_code,
                                                   &pair_code_len))
                {
                    return;
                }

                if (s_ctx.pairing_network_mode && (frame_decoded.src_id != LAVIET_GATEWAY_ID))
                {
                    printf("RADIO RX non-gateway PAIR_REQ drop src=0x%04X\r\n",
                           (unsigned int)frame_decoded.src_id);
                    return;
                }

                memset(&gateway_info, 0, sizeof(gateway_info));
                if (s_ctx.pairing_network_mode &&
                    security_main_cmd_get_device(0U, &gateway_info) &&
                    gateway_info.in_use)
                {
                    printf("RADIO RX network PAIR_REQ drop, gateway slot already in use\r\n");
                    return;
                }

                if (s_ctx.pairing_network_mode &&
                    (pkt->rssi_dbm < RADIO_NETWORK_PAIR_RSSI_MIN_DBM))
                {
                    printf("RADIO RX network PAIR_REQ drop, gateway RSSI too low rssi=%d min=%d\r\n",
                           (int)pkt->rssi_dbm,
                           (int)RADIO_NETWORK_PAIR_RSSI_MIN_DBM);
                    radio_main_notify(MENU_NOTIFICATION_WARNING, "Gateway RSSI low");
                    return;
                }

                s_ctx.pairing_pending = true;
                s_ctx.pairing_pending_network = s_ctx.pairing_network_mode;
                s_ctx.pairing_pending_node = frame_decoded.src_id;
                s_ctx.pairing_pending_code_len = (pair_code_len > sizeof(s_ctx.pairing_pending_code)) ?
                                                 sizeof(s_ctx.pairing_pending_code) : pair_code_len;
                memcpy(s_ctx.pairing_pending_code, pair_code, s_ctx.pairing_pending_code_len);

                radio_main_pair_code_to_text(s_ctx.pairing_pending_code,
                                              s_ctx.pairing_pending_code_len,
                                              code_text,
                                              (uint8_t)sizeof(code_text));
                snprintf(pair_note, sizeof(pair_note), "PAIR_REQ %s", code_text);
                radio_main_notify(MENU_NOTIFICATION_PAIRING, pair_note);
            }
        }
        else if (frame_type == LAVIET_TYPE_PAIR_RESP)
        {
            if (s_ctx.pairing_active && s_ctx.pairing_outgoing_pending)
            {
                bool code_match = false;
                char code_text[12];
                const uint8_t *pair_code = NULL;
                uint8_t pair_code_len = 0U;

                if (!radio_main_pair_payload_parse(frame_decoded.payload,
                                                   (uint8_t)frame_decoded.payload_len,
                                                   &pair_code,
                                                   &pair_code_len))
                {
                    return;
                }

                if (s_ctx.pairing_outgoing_network &&
                    (pkt->rssi_dbm < RADIO_NETWORK_PAIR_RSSI_MIN_DBM))
                {
                    printf("RADIO RX network PAIR_RESP drop, gateway RSSI too low rssi=%d min=%d\r\n",
                           (int)pkt->rssi_dbm,
                           (int)RADIO_NETWORK_PAIR_RSSI_MIN_DBM);
                    radio_main_notify(MENU_NOTIFICATION_WARNING, "Gateway RSSI low");
                    s_ctx.pairing_outgoing_pending = false;
                    s_ctx.pairing_outgoing_network = false;
                    return;
                }

                if ((pair_code_len == s_ctx.pairing_outgoing_code_len) &&
                    (pair_code_len > 0U) &&
                    (memcmp(pair_code, s_ctx.pairing_outgoing_code, pair_code_len) == 0))
                {
                    code_match = true;
                }

                radio_main_pair_code_to_text(s_ctx.pairing_outgoing_code,
                                             s_ctx.pairing_outgoing_code_len,
                                             code_text,
                                             (uint8_t)sizeof(code_text));

                if (code_match)
                {
                    bool saved;

                    if (s_ctx.pairing_outgoing_network)
                    {
                        saved = security_main_cmd_add_gateway(frame_decoded.src_id,
                                                              s_ctx.pairing_outgoing_code,
                                                              s_ctx.pairing_outgoing_code_len);
                    }
                    else
                    {
                        saved = security_main_cmd_add_device(frame_decoded.src_id,
                                                            s_ctx.pairing_outgoing_code,
                                                            s_ctx.pairing_outgoing_code_len);
                    }

                    if (saved)
                    {
                        char pair_note[21];
                        if (s_ctx.pairing_outgoing_network)
                        {
                            radio_main_set_gateway_pair_code(s_ctx.pairing_outgoing_code,
                                                             s_ctx.pairing_outgoing_code_len);
                        }
                        snprintf(pair_note, sizeof(pair_note), "PAIR_OK %s", code_text);
                        radio_main_notify(MENU_NOTIFICATION_PAIRING, pair_note);
                        s_ctx.pairing_active = false;
                    }
                    else
                    {
                        radio_main_notify(MENU_NOTIFICATION_ERROR, "Pairing save failed");
                    }
                }
                else
                {
                    radio_main_notify(MENU_NOTIFICATION_ERROR, "PAIR code mismatch");
                }

                s_ctx.pairing_outgoing_pending = false;
                s_ctx.pairing_outgoing_network = false;
            }
        }
        else if (frame_type == LAVIET_TYPE_KEY_ROTATE)
        {
            bool key_ok = radio_main_handle_key_rotate_frame(&frame_decoded);

            if (!key_ok)
            {
                printf("RADIO RX KEY_ROTATE ignored\r\n");
            }
            if ((frame_decoded.dst_id == s_ctx.node_id) &&
                ((frame_decoded.flags & LAVIET_FLAG_ACK_REQUIRED) != 0U))
            {
                (void)radio_main_send_ack(&frame_decoded);
            }
        }
        else if (frame_type == LAVIET_TYPE_ERROR)
        {
            if (s_ctx.pairing_active && s_ctx.pairing_outgoing_pending)
            {
                radio_main_notify(MENU_NOTIFICATION_PAIRING, "PAIR_ERROR");
                s_ctx.pairing_outgoing_pending = false;
            }
            else
            {
                radio_main_notify(MENU_NOTIFICATION_WARNING, "LAVIET error");
            }
            if (frame_decoded.dst_id == s_ctx.node_id)
            {
                (void)radio_main_send_ack(&frame_decoded);
            }
        }
        else if (frame_type == LAVIET_TYPE_ACK)
        {
            uint16_t acked_msg_id = 0U;
            uint32_t acked_counter = 0UL;

            if (laviet_frame_read_ack_payload(frame_decoded.payload,
                                              &acked_msg_id,
                                              &acked_counter))
            {
                printf("RADIO RX ACK src=0x%04X ack_msg=0x%04X ack_counter=%lu\r\n",
                       (unsigned int)frame_decoded.src_id,
                       (unsigned int)acked_msg_id,
                       (unsigned long)acked_counter);
                radio_main_handle_ack_frame(frame_decoded.src_id, acked_msg_id, acked_counter);
            }
            else
            {
                printf("RADIO RX ACK bad payload src=0x%04X\r\n",
                       (unsigned int)frame_decoded.src_id);
            }
        }
        else
        {
            radio_main_post_rx_notification(pkt->rssi_dbm,
                                            frame_decoded.src_id,
                                            frame_decoded.dst_id,
                                            frame_decoded.payload,
                                            frame_decoded.payload_len,
                                            frame_type);
            if ((frame_decoded.dst_id == s_ctx.node_id) &&
                ((frame_decoded.flags & LAVIET_FLAG_ACK_REQUIRED) != 0U) &&
                (frame_type != LAVIET_TYPE_ACK))
            {
                (void)radio_main_send_ack(&frame_decoded);
            }
        }
    }

    if ((frame_decoded.src_id == LAVIET_GATEWAY_ID) &&
        (frame_type != LAVIET_TYPE_COUNTER_SYNC) &&
        (frame_decoded.counter > s_ctx.gateway_rx_counter))
    {
        uint32_t latest_rx;
        uint32_t latest_tx;

        latest_rx = s_ctx.gateway_rx_counter;
        latest_tx = s_ctx.gateway_tx_counter;
        if (security_main_get_gateway_counter(&latest_rx, &latest_tx))
        {
            s_ctx.gateway_tx_counter = latest_tx;
        }
        s_ctx.gateway_rx_counter = frame_decoded.counter;
        (void)security_main_commit_gateway_counter(s_ctx.gateway_rx_counter,
                                                   s_ctx.gateway_tx_counter);
    }
}

static void radio_main_handle_hopping(void)
{
    uint32_t now;

    if (!s_ctx.fh_enabled)
    {
        return;
    }
    if (!s_ctx.initialized)
    {
        return;
    }
    if (s_ctx.ack_pending.active)
    {
        return;
    }

    now = radio_main_now_ms();
    if ((now - s_ctx.last_hop_ms) < s_ctx.fh_period_ms)
    {
        return;
    }
    if (radio_get_state() == RADIO_STATE_TX)
    {
        return;
    }

    s_ctx.last_hop_ms = now;
    s_ctx.hop_idx = (uint8_t)((s_ctx.hop_idx + 1U) % 3U);
    s_ctx.lora_cfg.frequency_hz = s_hop_channels_hz[s_ctx.hop_idx];
    if (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_LORA)
    {
        (void)radio_main_reconfigure_radio();
    }
}

static void radio_main_handle_auto_ping(void)
{
    uint32_t now;
    uint32_t period_ms;

    if (!s_ctx.auto_ping_enabled)
    {
        return;
    }
    if (!s_ctx.initialized)
    {
        return;
    }

    now = radio_main_now_ms();
    period_ms = radio_main_auto_ping_period_ms();
    if ((now - s_ctx.last_ping_ms) < period_ms)
    {
        return;
    }
    s_ctx.last_ping_ms = now;

    if (radio_get_state() != RADIO_STATE_TX)
    {
        if (s_ctx.auto_ping_mode == RADIO_MAIN_AUTO_PING_RAW)
        {
            (void)radio_main_send_raw_with_retry_ex(s_auto_ping_raw, RADIO_AUTO_PING_RAW_LEN, true);
        }
        else
        {
            const char *msg = s_template_groups[2][0];
            (void)radio_main_send_system_frame_ex(LAVIET_TYPE_DATA,
                                                  LAVIET_GATEWAY_ID,
                                                  (const uint8_t *)msg,
                                                  (uint16_t)strlen(msg),
                                                  false,
                                                  true);
        }
    }
}

/*
 * Secondary application-layer watchdog. Backend watchdogs should catch TX stalls
 * first, but this guard keeps the app recoverable if the backend state machine
 * or IRQ path still leaves the radio reported as TX.
 */
static void radio_main_watchdog_tx(void)
{
    if (!s_ctx.initialized)
    {
        return;
    }
    if (!radio_main_tx_timed_out())
    {
        return;
    }

    if (!radio_main_force_recover_radio("application TX watchdog"))
    {
        radio_main_notify(MENU_NOTIFICATION_ERROR, "TX watchdog failed");
    }
}

static void radio_main_ensure_rx_continuous(void)
{
    if (!s_ctx.initialized)
    {
        return;
    }
    if (radio_get_state() == RADIO_STATE_TX)
    {
        if (radio_main_tx_timed_out() &&
            !radio_main_force_recover_radio("ensure RX while TX stuck"))
        {
            radio_main_notify(MENU_NOTIFICATION_ERROR, "Radio stuck in TX");
        }
        return;
    }
    if (radio_get_state() == RADIO_STATE_RX_CONT)
    {
        return;
    }

    if (radio_start_rx_continuous() != RADIO_OK)
    {
        if (!radio_main_force_recover_radio("restart RX failed"))
        {
            radio_main_notify(MENU_NOTIFICATION_ERROR, "RX restart failed");
        }
    }
}

static void radio_main_notify(menu_notification_type_t type, const char *text)
{
    menu_notification_t n;

    memset(&n, 0, sizeof(n));
    n.type = type;
    if (text != NULL)
    {
        snprintf(n.text, sizeof(n.text), "%s", text);
    }
    (void)menu_main_post_notification(&n);
}

static void radio_main_clear_gateway_pair_code(void)
{
    memset(s_ctx.gateway_pair_code, 0, sizeof(s_ctx.gateway_pair_code));
    s_ctx.gateway_pair_code_len = 0U;
    s_ctx.gateway_pair_code_valid = false;
}

static void radio_main_set_gateway_pair_code(const uint8_t *code, uint8_t len)
{
    radio_main_clear_gateway_pair_code();
    if ((code == NULL) || (len == 0U))
    {
        return;
    }

    s_ctx.gateway_pair_code_len = (len > sizeof(s_ctx.gateway_pair_code)) ?
                                  (uint8_t)sizeof(s_ctx.gateway_pair_code) :
                                  len;
    memcpy(s_ctx.gateway_pair_code, code, s_ctx.gateway_pair_code_len);
    s_ctx.gateway_pair_code_valid = true;
    printf("RADIO: gateway pair code cache updated len=%u\r\n",
           (unsigned int)s_ctx.gateway_pair_code_len);
    radio_main_print_hex_bytes("RADIO: gateway pair code cache=", s_ctx.gateway_pair_code, s_ctx.gateway_pair_code_len);
}

static void radio_main_load_gateway_pair_code_from_security(void)
{
    trusted_info_t gateway_info;

    memset(&gateway_info, 0, sizeof(gateway_info));
    if (security_main_cmd_get_device(0U, &gateway_info) &&
        gateway_info.in_use &&
        (gateway_info.node_id == LAVIET_GATEWAY_ID) &&
        (gateway_info.code_len > 0U))
    {
        radio_main_set_gateway_pair_code(gateway_info.code, gateway_info.code_len);
    }
}

static bool radio_main_get_gateway_cached_frame_keys(security_frame_key_mode_t mode,
                                                     uint8_t enc_key_out[16],
                                                     uint8_t hmac_key_out[32])
{
    if (!s_ctx.gateway_pair_code_valid)
    {
        radio_main_load_gateway_pair_code_from_security();
    }

    if (!s_ctx.gateway_pair_code_valid)
    {
        return false;
    }

    return security_main_get_frame_keys_for_code(s_ctx.node_id,
                                                 LAVIET_GATEWAY_ID,
                                                 mode,
                                                 s_ctx.gateway_pair_code,
                                                 s_ctx.gateway_pair_code_len,
                                                 enc_key_out,
                                                 hmac_key_out);
}

static void radio_main_clear_broadcast_group_state(void)
{
    s_ctx.broadcast_group_key_valid = false;
    s_ctx.broadcast_group_epoch = 0UL;
    laviet_secure_zero(s_ctx.broadcast_group_aes_key, sizeof(s_ctx.broadcast_group_aes_key));
    laviet_secure_zero(s_ctx.broadcast_group_hmac_key, sizeof(s_ctx.broadcast_group_hmac_key));
    s_ctx.broadcast_group_pending_valid = false;
    s_ctx.broadcast_group_pending_epoch = 0UL;
    s_ctx.broadcast_group_pending_mask = 0U;
    laviet_secure_zero(s_ctx.broadcast_group_pending_key, sizeof(s_ctx.broadcast_group_pending_key));
}

static uint32_t radio_main_be32_read(const uint8_t *src)
{
    if (src == NULL)
    {
        return 0UL;
    }

    return ((uint32_t)src[0] << 24) |
           ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8) |
           (uint32_t)src[3];
}

static bool radio_main_derive_broadcast_group_frame_keys(const uint8_t group_key[RADIO_BCAST_GROUP_KEY_LEN],
                                                         uint32_t epoch,
                                                         uint8_t enc_key_out[16],
                                                         uint8_t hmac_key_out[32])
{
    static const uint8_t group_info_label[] = "LAVIET:BCAST:GROUP:V1";
    uint8_t group_info[(sizeof(group_info_label) - 1U) + 6U];
    uint8_t digest[32];
    uint8_t base_key[16];
    uint8_t key_info[8];
    uint8_t off = 0U;
    bool ok;

    if ((group_key == NULL) || (enc_key_out == NULL) || (hmac_key_out == NULL) || (epoch == 0UL))
    {
        return false;
    }

    memset(group_info, 0, sizeof(group_info));
    memset(digest, 0, sizeof(digest));
    memset(base_key, 0, sizeof(base_key));
    memcpy(&group_info[off], group_info_label, sizeof(group_info_label) - 1U);
    off = (uint8_t)(off + (uint8_t)(sizeof(group_info_label) - 1U));
    group_info[off++] = (uint8_t)(epoch >> 24);
    group_info[off++] = (uint8_t)(epoch >> 16);
    group_info[off++] = (uint8_t)(epoch >> 8);
    group_info[off++] = (uint8_t)epoch;
    group_info[off++] = (uint8_t)(LAVIET_BROADCAST_ID >> 8);
    group_info[off++] = (uint8_t)LAVIET_BROADCAST_ID;

    ok = laviet_hmac_sha256(group_key,
                            RADIO_BCAST_GROUP_KEY_LEN,
                            group_info,
                            sizeof(group_info),
                            digest);
    if (ok)
    {
        memcpy(base_key, digest, sizeof(base_key));
        key_info[0] = (uint8_t)'L';
        key_info[1] = (uint8_t)'V';
        key_info[2] = (uint8_t)'1';
        key_info[3] = (uint8_t)'K';
        key_info[4] = (uint8_t)(LAVIET_BROADCAST_ID >> 8);
        key_info[5] = (uint8_t)LAVIET_BROADCAST_ID;
        key_info[6] = 0U;
        key_info[7] = 1U;
        ok = laviet_hmac_sha256(base_key, sizeof(base_key), key_info, sizeof(key_info), digest);
        if (ok)
        {
            memcpy(enc_key_out, digest, 16U);
            key_info[7] = 2U;
            ok = laviet_hmac_sha256(base_key, sizeof(base_key), key_info, sizeof(key_info), hmac_key_out);
        }
    }

    laviet_secure_zero(group_info, sizeof(group_info));
    laviet_secure_zero(digest, sizeof(digest));
    laviet_secure_zero(base_key, sizeof(base_key));
    laviet_secure_zero(key_info, sizeof(key_info));
    if (!ok)
    {
        laviet_secure_zero(enc_key_out, 16U);
        laviet_secure_zero(hmac_key_out, 32U);
    }
    return ok;
}

static bool radio_main_try_broadcast_group_rx(const laviet_frame_t *frame, uint8_t enc_key[16])
{
    uint8_t expected[LAVIET_MAC_TAG_LEN];
    bool mac_match = false;
    bool ok = false;

    if ((frame == NULL) || (enc_key == NULL) ||
        !s_ctx.broadcast_group_key_valid ||
        (frame->src_id != LAVIET_GATEWAY_ID) ||
        (frame->dst_id != LAVIET_BROADCAST_ID) ||
        ((frame->flags & LAVIET_FLAG_ENCRYPTED) == 0U))
    {
        return false;
    }

    memset(expected, 0, sizeof(expected));
    if (laviet_frame_hmac_sha256(frame, s_ctx.broadcast_group_hmac_key, expected))
    {
        printf("RADIO RX HMAC DBG mode=BCAST_GROUP epoch=%lu src=0x%04X dst=0x%04X msg=0x%04X counter=%lu flags=0x%02X payload_len=%u\r\n",
               (unsigned long)s_ctx.broadcast_group_epoch,
               (unsigned int)frame->src_id,
               (unsigned int)frame->dst_id,
               (unsigned int)frame->msg_id,
               (unsigned long)frame->counter,
               (unsigned int)frame->flags,
               (unsigned int)frame->payload_len);
        mac_match = laviet_mac_equal(expected, frame->mac_tag);
    }
    if (mac_match)
    {
        memcpy(enc_key, s_ctx.broadcast_group_aes_key, 16U);
        ok = true;
    }

    laviet_secure_zero(expected, sizeof(expected));
    return ok;
}

static bool radio_main_handle_key_rotate_frame(const laviet_frame_t *frame)
{
    uint32_t epoch;
    uint8_t command;

    if ((frame == NULL) ||
        (frame->src_id != LAVIET_GATEWAY_ID) ||
        (frame->dst_id != s_ctx.node_id) ||
        (frame->payload_len != LAVIET_MAX_PAYLOAD) ||
        (frame->payload[0] != RADIO_BCAST_CTRL_MAGIC))
    {
        printf("RADIO RX KEY_ROTATE unsupported\r\n");
        return false;
    }

    command = frame->payload[1];
    epoch = radio_main_be32_read(&frame->payload[2]);
    if (epoch == 0UL)
    {
        printf("RADIO RX BCAST KEY bad epoch\r\n");
        return false;
    }

    if (command == RADIO_BCAST_CTRL_INSTALL_FRAGMENT)
    {
        uint8_t fragment_index = frame->payload[6];
        uint8_t fragment_count = frame->payload[7];

        if ((fragment_count != RADIO_BCAST_GROUP_FRAGMENT_COUNT) ||
            (fragment_index >= RADIO_BCAST_GROUP_FRAGMENT_COUNT))
        {
            printf("RADIO RX BCAST KEY bad fragment idx=%u count=%u\r\n",
                   (unsigned int)fragment_index,
                   (unsigned int)fragment_count);
            return false;
        }

        if ((!s_ctx.broadcast_group_pending_valid) ||
            (s_ctx.broadcast_group_pending_epoch != epoch))
        {
            s_ctx.broadcast_group_pending_valid = true;
            s_ctx.broadcast_group_pending_epoch = epoch;
            s_ctx.broadcast_group_pending_mask = 0U;
            laviet_secure_zero(s_ctx.broadcast_group_pending_key,
                               sizeof(s_ctx.broadcast_group_pending_key));
        }

        memcpy(&s_ctx.broadcast_group_pending_key[fragment_index * RADIO_BCAST_GROUP_FRAGMENT_LEN],
               &frame->payload[8],
               RADIO_BCAST_GROUP_FRAGMENT_LEN);
        s_ctx.broadcast_group_pending_mask |= (uint8_t)(1U << fragment_index);
        printf("RADIO RX BCAST KEY fragment epoch=%lu idx=%u mask=0x%02X\r\n",
               (unsigned long)epoch,
               (unsigned int)fragment_index,
               (unsigned int)s_ctx.broadcast_group_pending_mask);
        return true;
    }

    if (command == RADIO_BCAST_CTRL_ACTIVATE)
    {
        uint8_t enc_key[16];
        uint8_t hmac_key[32];
        uint8_t full_mask = (uint8_t)((1U << RADIO_BCAST_GROUP_FRAGMENT_COUNT) - 1U);

        memset(enc_key, 0, sizeof(enc_key));
        memset(hmac_key, 0, sizeof(hmac_key));
        if ((!s_ctx.broadcast_group_pending_valid) ||
            (s_ctx.broadcast_group_pending_epoch != epoch) ||
            ((s_ctx.broadcast_group_pending_mask & full_mask) != full_mask))
        {
            printf("RADIO RX BCAST KEY activate missing epoch=%lu pending_epoch=%lu mask=0x%02X\r\n",
                   (unsigned long)epoch,
                   (unsigned long)s_ctx.broadcast_group_pending_epoch,
                   (unsigned int)s_ctx.broadcast_group_pending_mask);
            return false;
        }

        if (!radio_main_derive_broadcast_group_frame_keys(s_ctx.broadcast_group_pending_key,
                                                          epoch,
                                                          enc_key,
                                                          hmac_key))
        {
            printf("RADIO RX BCAST KEY derive failed epoch=%lu\r\n", (unsigned long)epoch);
            return false;
        }

        memcpy(s_ctx.broadcast_group_aes_key, enc_key, sizeof(s_ctx.broadcast_group_aes_key));
        memcpy(s_ctx.broadcast_group_hmac_key, hmac_key, sizeof(s_ctx.broadcast_group_hmac_key));
        s_ctx.broadcast_group_epoch = epoch;
        s_ctx.broadcast_group_key_valid = true;
        s_ctx.broadcast_group_pending_valid = false;
        s_ctx.broadcast_group_pending_epoch = 0UL;
        s_ctx.broadcast_group_pending_mask = 0U;
        laviet_secure_zero(s_ctx.broadcast_group_pending_key,
                           sizeof(s_ctx.broadcast_group_pending_key));
        laviet_secure_zero(enc_key, sizeof(enc_key));
        laviet_secure_zero(hmac_key, sizeof(hmac_key));
        printf("RADIO RX BCAST KEY active epoch=%lu\r\n", (unsigned long)epoch);
        return true;
    }

    printf("RADIO RX BCAST KEY unknown cmd=%u epoch=%lu\r\n",
           (unsigned int)command,
           (unsigned long)epoch);
    return false;
}

static uint8_t radio_main_format_payload_text(const uint8_t *payload,
                                              uint8_t payload_len,
                                              char *out,
                                              uint8_t out_size)
{
    static const char hex_table[] = "0123456789ABCDEF";
    uint8_t i;
    uint8_t out_idx = 0U;
    bool all_printable = true;

    if ((out == NULL) || (out_size == 0U))
    {
        return 0U;
    }

    out[0] = '\0';
    if ((payload == NULL) || (payload_len == 0U))
    {
        return 0U;
    }

    for (i = 0U; i < payload_len; i++)
    {
        if (!isprint((unsigned char)payload[i]))
        {
            all_printable = false;
            break;
        }
    }

    if (all_printable)
    {
        uint8_t copy_len = (payload_len >= (out_size - 1U)) ? (uint8_t)(out_size - 1U) : payload_len;

        for (i = 0U; i < copy_len; i++)
        {
            out[i] = (char)payload[i];
        }
        out[copy_len] = '\0';
        return copy_len;
    }

    if (out_size <= 5U)
    {
        return 0U;
    }

    out[out_idx++] = 'H';
    out[out_idx++] = 'E';
    out[out_idx++] = 'X';
    out[out_idx++] = ':';

    for (i = 0U; i < payload_len; i++)
    {
        if ((out_idx + 2U) >= out_size)
        {
            break;
        }
        out[out_idx++] = hex_table[payload[i] >> 4];
        out[out_idx++] = hex_table[payload[i] & 0x0F];
    }

    out[out_idx] = '\0';
    return out_idx;
}

static bool radio_main_push_payload_to_monitor(int16_t rssi_dbm,
                                               uint16_t src_id,
                                               const uint8_t *payload,
                                               uint8_t payload_len)
{
    char text_buf[21];
    uint8_t text_len;

    text_len = radio_main_format_payload_text(payload, payload_len, text_buf, sizeof(text_buf));
    if (text_len == 0U)
    {
        return false;
    }

    return lcd_main_push_message_from(rssi_dbm, src_id, (const uint8_t *)text_buf, text_len);
}

static void radio_main_log_gateway_rx_frame(const laviet_frame_t *raw_frame,
                                            const laviet_frame_t *decoded_frame,
                                            int16_t rssi_dbm,
                                            int8_t snr_db)
{
    char raw_text[21];
    char dec_text[21];
    laviet_frame_type_t type;
    uint8_t version;

    if ((raw_frame == NULL) || (decoded_frame == NULL) || (decoded_frame->src_id != LAVIET_GATEWAY_ID))
    {
        return;
    }

    type = laviet_frame_type(decoded_frame);
    version = (uint8_t)((decoded_frame->ver_type >> 4) & 0x0FU);

    printf("RADIO RX GATEWAY version=%u ver_type=0x%02X type=%s flags=0x%02X src=0x%04X dst=0x%04X msg=0x%04X counter=%lu payload_len=%u rssi=%d snr=%d\r\n",
           (unsigned int)version,
           (unsigned int)decoded_frame->ver_type,
           radio_main_frame_type_text(type),
           (unsigned int)decoded_frame->flags,
           (unsigned int)decoded_frame->src_id,
           (unsigned int)decoded_frame->dst_id,
           (unsigned int)decoded_frame->msg_id,
           (unsigned long)decoded_frame->counter,
           (unsigned int)decoded_frame->payload_len,
           (int)rssi_dbm,
           (int)snr_db);

    printf("RADIO RX GATEWAY FLAGS enc=%u ack_req=%u is_ack=%u pairing=%u cfg=%u bcast=%u ctr_override=%u key_update=%u\r\n",
           ((decoded_frame->flags & LAVIET_FLAG_ENCRYPTED) != 0U) ? 1U : 0U,
           ((decoded_frame->flags & LAVIET_FLAG_ACK_REQUIRED) != 0U) ? 1U : 0U,
           ((decoded_frame->flags & LAVIET_FLAG_IS_ACK) != 0U) ? 1U : 0U,
           ((decoded_frame->flags & LAVIET_FLAG_PAIRING) != 0U) ? 1U : 0U,
           ((decoded_frame->flags & LAVIET_FLAG_CONFIG_ACCESS) != 0U) ? 1U : 0U,
           ((decoded_frame->flags & LAVIET_FLAG_BROADCAST) != 0U) ? 1U : 0U,
           ((decoded_frame->flags & LAVIET_FLAG_COUNTER_OVERRIDE) != 0U) ? 1U : 0U,
           ((decoded_frame->flags & LAVIET_FLAG_KEY_UPDATE) != 0U) ? 1U : 0U);

    radio_main_print_hex_bytes("RADIO RX GATEWAY MAC: ", decoded_frame->mac_tag, LAVIET_MAC_TAG_LEN);
    radio_main_print_hex_bytes("RADIO RX GATEWAY PAYLOAD RAW HEX: ", raw_frame->payload, raw_frame->payload_len);
    if (radio_main_format_payload_text(raw_frame->payload, raw_frame->payload_len, raw_text, sizeof(raw_text)) > 0U)
    {
        printf("RADIO RX GATEWAY PAYLOAD RAW TXT: \"%s\"\r\n", raw_text);
    }

    radio_main_print_hex_bytes("RADIO RX GATEWAY PAYLOAD DEC HEX: ", decoded_frame->payload, decoded_frame->payload_len);
    if (radio_main_format_payload_text(decoded_frame->payload, decoded_frame->payload_len, dec_text, sizeof(dec_text)) > 0U)
    {
        printf("RADIO RX GATEWAY PAYLOAD DEC TXT: \"%s\"\r\n", dec_text);
    }
}

static void radio_main_print_hex_bytes(const char *label, const uint8_t *data, uint16_t len)
{
    uint16_t i;

    printf("%s", (label != NULL) ? label : "");
    for (i = 0U; i < len; i++)
    {
        printf("%02X", data[i]);
        if ((uint16_t)(i + 1U) < len)
        {
            printf(" ");
        }
    }
    printf("\r\n");
}

static void radio_main_log_hmac_debug(const char *prefix,
                                      const laviet_frame_t *frame,
                                      security_frame_key_mode_t mode,
                                      const uint8_t *pair_code,
                                      uint8_t pair_code_len,
                                      const uint8_t hmac_key[32],
                                      const uint8_t expected_mac[LAVIET_MAC_TAG_LEN])
{
    uint8_t mac_input[LAVIET_FRAME_HEADER_LEN + LAVIET_MAX_PAYLOAD];
    uint8_t mac_input_len = 0U;

    if ((prefix == NULL) || (frame == NULL) || (hmac_key == NULL) || (expected_mac == NULL))
    {
        return;
    }

    printf("%s mode=%s src=0x%04X dst=0x%04X msg=0x%04X counter=%lu flags=0x%02X payload_len=%u\r\n",
           prefix,
           radio_main_key_mode_text(mode),
           (unsigned int)frame->src_id,
           (unsigned int)frame->dst_id,
           (unsigned int)frame->msg_id,
           (unsigned long)frame->counter,
           (unsigned int)frame->flags,
           (unsigned int)frame->payload_len);
    if ((pair_code != NULL) && (pair_code_len > 0U))
    {
        radio_main_print_hex_bytes("  pair_code=", pair_code, pair_code_len);
    }
    else
    {
        printf("  pair_code=<none>\r\n");
    }
    radio_main_print_hex_bytes("  hmac_key=", hmac_key, LAVIET_HMAC_KEY_LEN);
    if (laviet_frame_build_mac_input(frame, mac_input, sizeof(mac_input), &mac_input_len))
    {
        radio_main_print_hex_bytes("  mac_input=", mac_input, mac_input_len);
    }
    else
    {
        printf("  mac_input=<build_failed>\r\n");
    }
    radio_main_print_hex_bytes("  expected_mac=", expected_mac, LAVIET_MAC_TAG_LEN);
    radio_main_print_hex_bytes("  frame_mac=", frame->mac_tag, LAVIET_MAC_TAG_LEN);
}

static void radio_main_print_generated_pattern(const char *label, uint8_t value, uint16_t len)
{
    uint16_t i;

    printf("%s", (label != NULL) ? label : "");
    for (i = 0U; i < len; i++)
    {
        printf("%02X", value);
        if ((uint16_t)(i + 1U) < len)
        {
            printf(" ");
        }
    }
    printf("\r\n");
}

static void radio_main_print_fsk_sync_word(uint64_t sync_word, uint8_t sync_len, const char *label)
{
    uint8_t bytes[8];
    uint8_t i;

    if (sync_len == 0U)
    {
        printf("%sOFF\r\n", (label != NULL) ? label : "");
        return;
    }

    for (i = 0U; i < sync_len; i++)
    {
        uint8_t shift = (uint8_t)(((sync_len - 1U - i) * 8U) & 0x3FU);
        bytes[i] = (uint8_t)((sync_word >> shift) & 0xFFU);
    }
    radio_main_print_hex_bytes(label, bytes, sync_len);
}

static void radio_main_print_ook_sync_word(uint32_t sync_word, uint8_t sync_len, const char *label)
{
    uint8_t bytes[4];
    uint8_t i;

    if (sync_len == 0U)
    {
        printf("%sOFF\r\n", (label != NULL) ? label : "");
        return;
    }

    for (i = 0U; i < sync_len; i++)
    {
        uint8_t shift = (uint8_t)(((sync_len - 1U - i) * 8U) & 0x1FU);
        bytes[i] = (uint8_t)((sync_word >> shift) & 0xFFU);
    }
    radio_main_print_hex_bytes(label, bytes, sync_len);
}

static void radio_main_print_rx_ascii(const uint8_t *data, uint8_t len)
{
    uint8_t i;

    printf("RADIO RX RAW: ");
    for (i = 0U; i < len; i++)
    {
        char c = (char)data[i];
        printf("%c", isprint((unsigned char)c) ? c : '.');
    }
    printf("\r\n");
}

static void radio_main_print_tx_ascii(const uint8_t *data, uint8_t len)
{
    uint8_t i;

    printf("RADIO TX TEXT: ");
    for (i = 0U; i < len; i++)
    {
        char c = (char)data[i];
        printf("%c", isprint((unsigned char)c) ? c : '.');
    }
    printf("\r\n");
}

/*
 * Prints a terminal-side view of the radio packet as it is emitted by the
 * current backend. Hardware-generated elements such as preamble, sync or CRC
 * are logged explicitly from the active configuration so diagnostics include
 * more than just the BEKO payload bytes.
 */
static void radio_main_print_tx_frame(const uint8_t *data, uint8_t len, bool retry_attempt)
{
    const char *prefix = retry_attempt ? "RADIO TX RETRY" : "RADIO TX";

    if ((data == NULL) || (len == 0U))
    {
        return;
    }

    if (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_FSK)
    {
        uint8_t length_byte = len;

        printf("%s FSK freq=%luHz bitrate=%lubps power=%ddBm bw=%u\r\n",
               prefix,
               (unsigned long)s_ctx.fsk_cfg.frequency_hz,
               (unsigned long)s_ctx.fsk_cfg.bitrate_bps,
               (int)s_ctx.fsk_cfg.tx_power_dbm,
               (unsigned int)s_ctx.fsk_cfg.rx_bandwidth);
        printf("%s FSK PREAMBLE(%uB HW): ",
               prefix,
               (unsigned int)s_ctx.fsk_cfg.preamble_len);
        radio_main_print_generated_pattern("", 0xAAU, s_ctx.fsk_cfg.preamble_len);
        printf("%s FSK SYNC(%uB HW): ", prefix, (unsigned int)s_ctx.fsk_cfg.sync_word_len);
        radio_main_print_fsk_sync_word(s_ctx.fsk_cfg.sync_word, s_ctx.fsk_cfg.sync_word_len, "");
        printf("%s FSK LEN(HW): %02X\r\n", prefix, length_byte);
        radio_main_print_hex_bytes("RADIO TX FSK PAYLOAD: ", data, len);
        printf("%s FSK CRC(HW): %s\r\n",
               prefix,
               (s_ctx.fsk_cfg.crc_type == RADIO_MAIN_CRC_OFF) ? "OFF" :
               ((s_ctx.fsk_cfg.crc_type == RADIO_MAIN_CRC_IBM) ? "IBM" : "CCITT"));
        radio_main_print_tx_ascii(data, len);
        return;
    }

    if (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_OOK)
    {
        uint8_t length_byte = len;

        printf("%s OOK freq=%luHz bitrate=%lubps power=%ddBm bw=%u thr=%u/%u\r\n",
               prefix,
               (unsigned long)s_ctx.ook_cfg.frequency_hz,
               (unsigned long)s_ctx.ook_cfg.bitrate_bps,
               (int)s_ctx.ook_cfg.tx_power_dbm,
               (unsigned int)s_ctx.ook_cfg.rx_bandwidth,
               (unsigned int)s_ctx.ook_cfg.threshold,
               (unsigned int)s_ctx.ook_cfg.threshold_value);
        printf("%s OOK PREAMBLE(%uB HW): ",
               prefix,
               (unsigned int)s_ctx.ook_cfg.preamble_len);
        radio_main_print_generated_pattern("", 0xAAU, s_ctx.ook_cfg.preamble_len);
        printf("%s OOK SYNC(%uB HW): ", prefix, (unsigned int)s_ctx.ook_cfg.sync_word_len);
        radio_main_print_ook_sync_word(s_ctx.ook_cfg.sync_word, s_ctx.ook_cfg.sync_word_len, "");
        printf("%s OOK LEN(HW): %02X\r\n", prefix, length_byte);
        radio_main_print_hex_bytes("RADIO TX OOK PAYLOAD: ", data, len);
        printf("%s OOK CRC(HW): OFF\r\n", prefix);
        radio_main_print_tx_ascii(data, len);
        return;
    }

    printf("%s LORA freq=%luHz bw=%u sf=%u cr=4/%u preamble=%u sym sync=%02X crc=%s hdr=%s iq=%s\r\n",
           prefix,
           (unsigned long)s_ctx.lora_cfg.frequency_hz,
           (unsigned int)s_ctx.lora_cfg.bandwidth,
           (unsigned int)s_ctx.lora_cfg.spreading_factor,
           (unsigned int)s_ctx.lora_cfg.coding_rate,
           (unsigned int)s_ctx.lora_cfg.preamble_len,
           (unsigned int)s_ctx.lora_cfg.sync_word,
           s_ctx.lora_cfg.crc_on ? "ON" : "OFF",
           s_ctx.lora_cfg.implicit_header ? "implicit" : "explicit",
           s_ctx.lora_cfg.invert_iq ? "invert" : "normal");
    radio_main_print_hex_bytes("RADIO TX LORA PAYLOAD: ", data, len);
    radio_main_print_tx_ascii(data, len);
}

static uint32_t radio_main_frf_to_hz(uint8_t msb, uint8_t mid, uint8_t lsb)
{
    uint32_t frf = ((uint32_t)msb << 16) |
                   ((uint32_t)mid << 8) |
                   (uint32_t)lsb;

    return (uint32_t)((((uint64_t)frf) * 32000000ULL) >> 19);
}

static void radio_main_log_runtime_profile(const char *reason)
{
    const char *tag = (reason != NULL) ? reason : "state";

    printf("RADIO CFG [%s]: sel=%u backend=%u lib=%u state=%d init=%u\r\n",
           tag,
           (unsigned int)s_ctx.modulation_id,
           (unsigned int)s_ctx.backend_modulation_id,
           (unsigned int)radio_get_backend(),
           (int)radio_get_state(),
           s_ctx.initialized ? 1U : 0U);

    if (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_FSK)
    {
        printf("RADIO CFG [%s] FSK: freq=%luHz br=%lubps bw=%u shape=%u filter=%u pre=%u sync_len=%u crc=%u white=%u addr=%u\r\n",
               tag,
               (unsigned long)s_ctx.fsk_cfg.frequency_hz,
               (unsigned long)s_ctx.fsk_cfg.bitrate_bps,
               (unsigned int)s_ctx.fsk_cfg.rx_bandwidth,
               (unsigned int)s_ctx.fsk_cfg.shaping,
               (unsigned int)s_ctx.fsk_cfg.filter,
               (unsigned int)s_ctx.fsk_cfg.preamble_len,
               (unsigned int)s_ctx.fsk_cfg.sync_word_len,
               (unsigned int)s_ctx.fsk_cfg.crc_type,
               s_ctx.fsk_cfg.data_whitening ? 1U : 0U,
               (unsigned int)s_ctx.fsk_cfg.address_filter);
        radio_main_print_fsk_sync_word(s_ctx.fsk_cfg.sync_word,
                                       s_ctx.fsk_cfg.sync_word_len,
                                       "RADIO CFG FSK SYNC: ");
        return;
    }

    if (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_OOK)
    {
        printf("RADIO CFG [%s] OOK: freq=%luHz br=%lubps bw=%u pre=%u sync_len=%u thr=%u thr_v=%u\r\n",
               tag,
               (unsigned long)s_ctx.ook_cfg.frequency_hz,
               (unsigned long)s_ctx.ook_cfg.bitrate_bps,
               (unsigned int)s_ctx.ook_cfg.rx_bandwidth,
               (unsigned int)s_ctx.ook_cfg.preamble_len,
               (unsigned int)s_ctx.ook_cfg.sync_word_len,
               (unsigned int)s_ctx.ook_cfg.threshold,
               (unsigned int)s_ctx.ook_cfg.threshold_value);
        radio_main_print_ook_sync_word(s_ctx.ook_cfg.sync_word,
                                       s_ctx.ook_cfg.sync_word_len,
                                       "RADIO CFG OOK SYNC: ");
        return;
    }

    printf("RADIO CFG [%s] LORA: freq=%luHz bw=%u sf=%u cr=4/%u pre=%u sync=%02X crc=%u iq=%u hdr=%u\r\n",
           tag,
           (unsigned long)s_ctx.lora_cfg.frequency_hz,
           (unsigned int)s_ctx.lora_cfg.bandwidth,
           (unsigned int)s_ctx.lora_cfg.spreading_factor,
           (unsigned int)s_ctx.lora_cfg.coding_rate,
           (unsigned int)s_ctx.lora_cfg.preamble_len,
           (unsigned int)s_ctx.lora_cfg.sync_word,
           s_ctx.lora_cfg.crc_on ? 1U : 0U,
           s_ctx.lora_cfg.invert_iq ? 1U : 0U,
           s_ctx.lora_cfg.implicit_header ? 1U : 0U);
}

static void radio_main_log_hw_registers(const char *reason)
{
    uint8_t op_mode = 0U;
    uint8_t frf_msb = 0U;
    uint8_t frf_mid = 0U;
    uint8_t frf_lsb = 0U;
    uint8_t rx_bw = 0U;
    uint8_t bitrate_msb = 0U;
    uint8_t bitrate_lsb = 0U;
    uint8_t sync_cfg = 0U;
    uint8_t packet_cfg2 = 0U;
    const char *tag = (reason != NULL) ? reason : "state";
    bool ok;

    if (!s_ctx.initialized)
    {
        return;
    }

    ok = (radio_raw_read_reg(SX1276_REG_OP_MODE, &op_mode) == RADIO_OK) &&
         (radio_raw_read_reg(SX1276_REG_FRF_MSB, &frf_msb) == RADIO_OK) &&
         (radio_raw_read_reg(SX1276_REG_FRF_MID, &frf_mid) == RADIO_OK) &&
         (radio_raw_read_reg(SX1276_REG_FRF_LSB, &frf_lsb) == RADIO_OK) &&
         (radio_raw_read_reg(SX1276_REG_RX_BW, &rx_bw) == RADIO_OK);
    if (!ok)
    {
        printf("RADIO HW [%s]: reg read failed\r\n", tag);
        return;
    }

    printf("RADIO HW [%s]: opmode=0x%02X mode=%u mod=0x%02X frf=0x%02X%02X%02X -> %luHz rx_bw=0x%02X\r\n",
           tag,
           (unsigned int)op_mode,
           (unsigned int)(op_mode & SX1276_OPMODE_MODE_MASK),
           (unsigned int)(op_mode & SX1276_OPMODE_MODULATION_MASK),
           (unsigned int)frf_msb,
           (unsigned int)frf_mid,
           (unsigned int)frf_lsb,
           (unsigned long)radio_main_frf_to_hz(frf_msb, frf_mid, frf_lsb),
           (unsigned int)rx_bw);

    if ((s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_FSK) ||
        (s_ctx.backend_modulation_id == RADIO_MAIN_MODULATION_OOK))
    {
        ok = (radio_raw_read_reg(SX1276_REG_BITRATE_MSB, &bitrate_msb) == RADIO_OK) &&
             (radio_raw_read_reg(SX1276_REG_BITRATE_LSB, &bitrate_lsb) == RADIO_OK) &&
             (radio_raw_read_reg(SX1276_REG_SYNC_CONFIG, &sync_cfg) == RADIO_OK) &&
             (radio_raw_read_reg(SX1276_REG_PACKET_CONFIG_2, &packet_cfg2) == RADIO_OK);
        if (ok)
        {
            uint16_t bitrate_reg = ((uint16_t)bitrate_msb << 8) | bitrate_lsb;
            uint32_t bitrate_hz = (bitrate_reg != 0U) ? (32000000UL / (uint32_t)bitrate_reg) : 0UL;

            printf("RADIO HW [%s]: bitrate_reg=0x%04X -> %lubps sync_cfg=0x%02X packet_cfg2=0x%02X\r\n",
                   tag,
                   (unsigned int)bitrate_reg,
                   (unsigned long)bitrate_hz,
                   (unsigned int)sync_cfg,
                   (unsigned int)packet_cfg2);
        }
    }
}

static bool radio_main_finish_pairing(bool accept)
{
    bool ok;
    menu_notification_t n;
    uint32_t node_id;
    uint8_t code_len;
    const uint8_t *code;
    char code_text[12];
    uint8_t payload[16];
    uint8_t payload_len;

    if (!s_ctx.pairing_pending)
    {
        return false;
    }

    node_id = s_ctx.pairing_pending_node;
    code = s_ctx.pairing_pending_code;
    code_len = s_ctx.pairing_pending_code_len;

    if (!accept)
    {
        uint8_t err_payload[LAVIET_ERROR_PAYLOAD_LEN] = { 1U, LAVIET_TYPE_PAIR_REQ, 0U, 0U, 0U, 0U, 0U, 0U };

        (void)radio_main_send_system_frame(LAVIET_TYPE_ERROR,
                                           node_id,
                                           err_payload,
                                           sizeof(err_payload));
        memset(&n, 0, sizeof(n));
        n.type = MENU_NOTIFICATION_PAIRING;
        snprintf(n.text, sizeof(n.text), "Device rejected");
        (void)menu_main_post_notification(&n);
        s_ctx.pairing_pending = false;
        s_ctx.pairing_pending_network = false;
        return true;
    }

    if (s_ctx.pairing_pending_network)
    {
        ok = security_main_cmd_add_gateway(node_id, code, code_len);
    }
    else
    {
        ok = security_main_cmd_add_device(node_id, code, code_len);
    }
    if (!ok)
    {
        radio_main_notify(MENU_NOTIFICATION_ERROR, "Pairing save failed");
        s_ctx.pairing_pending = false;
        s_ctx.pairing_pending_network = false;
        return false;
    }
    if (s_ctx.pairing_pending_network)
    {
        radio_main_set_gateway_pair_code(code, code_len);
    }

    payload_len = radio_main_pair_payload_build(s_ctx.pairing_pending_network,
                                                code,
                                                code_len,
                                                payload,
                                                (uint8_t)sizeof(payload));
    if (payload_len == 0U)
    {
        s_ctx.pairing_pending = false;
        s_ctx.pairing_pending_network = false;
        return false;
    }

    (void)radio_main_send_system_frame(LAVIET_TYPE_PAIR_RESP, node_id, payload, payload_len);

    radio_main_pair_code_to_text(code, code_len, code_text, (uint8_t)sizeof(code_text));

    memset(&n, 0, sizeof(n));
    n.type = MENU_NOTIFICATION_PAIRING;
    snprintf(n.text, sizeof(n.text), "PAIR_OK %s", code_text);
    (void)menu_main_post_notification(&n);
    if (s_ctx.pairing_pending_network)
    {
        s_ctx.gateway_key_mode_known = false;
    }
    s_ctx.pairing_pending = false;
    s_ctx.pairing_pending_network = false;
    s_ctx.pairing_active = false;
    s_ctx.pairing_network_mode = false;
    return true;
}

static bool radio_main_send_pair_request_internal(void)
{
    char note[21];
    char code_text[12];
    uint8_t payload[16];
    uint8_t payload_len;
    uint32_t dst_id;

    if (!s_ctx.pairing_active)
    {
        return false;
    }
    s_ctx.pairing_outgoing_code_len = RADIO_PAIR_CODE_LEN;
    radio_main_make_pair_code(s_ctx.pairing_outgoing_code, s_ctx.pairing_outgoing_code_len);
    payload_len = radio_main_pair_payload_build(s_ctx.pairing_network_mode,
                                                s_ctx.pairing_outgoing_code,
                                                s_ctx.pairing_outgoing_code_len,
                                                payload,
                                                (uint8_t)sizeof(payload));
    if (payload_len == 0U)
    {
        return false;
    }

    dst_id = s_ctx.pairing_network_mode ? LAVIET_GATEWAY_ID : LAVIET_BROADCAST_ID;
    if (!radio_main_send_system_frame(LAVIET_TYPE_PAIR_REQ,
                                      dst_id,
                                      payload,
                                      payload_len))
    {
        return false;
    }

    s_ctx.pairing_outgoing_pending = true;
    s_ctx.pairing_outgoing_network = s_ctx.pairing_network_mode;
    radio_main_pair_code_to_text(s_ctx.pairing_outgoing_code,
                                 s_ctx.pairing_outgoing_code_len,
                                 code_text,
                                 (uint8_t)sizeof(code_text));
    snprintf(note, sizeof(note), "PAIR_SENT %.10s", code_text);
    radio_main_notify(MENU_NOTIFICATION_PAIRING, note);
    return true;
}

static void radio_main_make_pair_code(uint8_t *code_out, uint8_t len)
{
    uint8_t i;
    uint32_t x;

    if ((code_out == NULL) || (len == 0U))
    {
        return;
    }

    x = radio_main_now_ms() ^ s_ctx.node_id ^ (s_ctx.next_msg_id * 2654435761UL);
    for (i = 0U; i < len; i++)
    {
        x = (1103515245UL * x) + 12345UL;
        code_out[i] = (uint8_t)('0' + ((x >> 16) % 10UL));
    }
}

static void radio_main_pair_code_to_text(const uint8_t *code, uint8_t len, char *out, uint8_t out_size)
{
    uint8_t i;
    uint8_t max_copy;

    if ((out == NULL) || (out_size == 0U))
    {
        return;
    }

    out[0] = '\0';
    if ((code == NULL) || (len == 0U))
    {
        return;
    }

    max_copy = (len >= (out_size - 1U)) ? (uint8_t)(out_size - 1U) : len;
    for (i = 0U; i < max_copy; i++)
    {
        char c = (char)code[i];
        out[i] = isprint((unsigned char)c) ? c : '.';
    }
    out[max_copy] = '\0';
}

static bool radio_main_pair_payload_parse(const uint8_t *payload,
                                          uint8_t payload_len,
                                          const uint8_t **code_out,
                                          uint8_t *code_len_out)
{
    if ((code_out == NULL) || (code_len_out == NULL))
    {
        return false;
    }

    *code_out = NULL;
    *code_len_out = 0U;

    if ((payload == NULL) || (payload_len != RADIO_PAIR_CODE_LEN))
    {
        return false;
    }

    *code_out = payload;
    *code_len_out = payload_len;
    return true;
}

static uint8_t radio_main_pair_payload_build(bool network_mode,
                                             const uint8_t *code,
                                             uint8_t code_len,
                                             uint8_t *payload_out,
                                             uint8_t payload_capacity)
{
    (void)network_mode;

    if ((payload_out == NULL) || (code == NULL) || (code_len == 0U))
    {
        return 0U;
    }

    if ((code_len != RADIO_PAIR_CODE_LEN) || (payload_capacity < RADIO_PAIR_CODE_LEN))
    {
        return 0U;
    }

    memcpy(payload_out, code, code_len);
    return code_len;
}

static uint32_t radio_main_now_ms(void)
{
    osKernelState_t state = osKernelGetState();
    uint32_t tick_hz;
    uint32_t ticks;

    if ((state == osKernelRunning) || (state == osKernelLocked))
    {
        ticks = osKernelGetTickCount();
        tick_hz = osKernelGetTickFreq();
        if (tick_hz == 0U)
        {
            return ticks;
        }
        return (uint32_t)(((uint64_t)ticks * 1000ULL) / tick_hz);
    }

    return HAL_GetTick();
}
