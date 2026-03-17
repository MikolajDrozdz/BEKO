#include "radio_main.h"

#include "beko_net_proto.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "lcd_main.h"
#include "menu_main.h"
#include "radio_lib/radio_lib.h"
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
#define RADIO_AUTO_PING_PERIOD_MS            5000UL
#define RADIO_HOP_PERIOD_DEFAULT_MS          10000UL
#define RADIO_DEDUP_WINDOW_MS                60000UL
#define RADIO_PAIR_CODE_LEN                  6U
#define RADIO_AUTH_TAG_LEN                   4U
#define RADIO_RECOVERY_RESET_PULSE_MS        2U
#define RADIO_RECOVERY_RESET_BOOT_MS         10U
#define RADIO_RECOVERY_RETRY_COUNT           2U
#define RADIO_TX_GUARD_MIN_MS                200UL
#define RADIO_TX_GUARD_LORA_MS               20000UL
#define RADIO_TX_GUARD_MARGIN_MS             64UL

typedef enum
{
    RADIO_MAIN_CMD_NONE = 0,
    RADIO_MAIN_CMD_SEND_TEMPLATE,
    RADIO_MAIN_CMD_SET_PRESET,
    RADIO_MAIN_CMD_SET_MODULATION,
    RADIO_MAIN_CMD_SET_MOD_FREQ,
    RADIO_MAIN_CMD_SET_MOD_BW,
    RADIO_MAIN_CMD_SET_OPTION,
    RADIO_MAIN_CMD_SET_FH,
    RADIO_MAIN_CMD_SET_FH_PERIOD,
    RADIO_MAIN_CMD_SET_CODING,
    RADIO_MAIN_CMD_SET_AUTO_PING,
    RADIO_MAIN_CMD_RESET_MODULE,
    RADIO_MAIN_CMD_START_PAIRING,
    RADIO_MAIN_CMD_PAIRING_ACCEPT,
    RADIO_MAIN_CMD_SEND_JOIN_REQ,
    RADIO_MAIN_CMD_SEND_TRUST_REMOVED
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
    bool initialized;
    bool fh_enabled;
    uint32_t fh_period_ms;
    bool coding_enabled;
    bool auto_ping_enabled;
    uint8_t lora_preset;
    radio_main_fsk_cfg_t fsk_cfg;
    radio_main_ook_cfg_t ook_cfg;
    uint8_t hop_idx;
    uint32_t last_hop_ms;
    uint32_t last_ping_ms;
    uint32_t node_id;
    uint32_t next_msg_id;
    bool pairing_active;
    uint32_t pairing_until_ms;
    bool pairing_pending;
    uint32_t pairing_pending_node;
    uint8_t pairing_pending_code[8];
    uint8_t pairing_pending_code_len;
    bool pairing_outgoing_pending;
    uint8_t pairing_outgoing_code[8];
    uint8_t pairing_outgoing_code_len;
    bool tx_in_progress;
    uint32_t tx_deadline_ms;
    beko_net_dedup_cache_t dedup;
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
static void radio_main_load_default_fsk_profile(radio_main_fsk_cfg_t *cfg);
static void radio_main_load_default_ook_profile(radio_main_ook_cfg_t *cfg);
static void radio_main_apply_modulation_cfg(void);
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
static bool radio_main_reconfigure_radio(void);
static bool radio_main_reset_module_internal(void);
static bool radio_main_force_recover_radio(const char *reason);
static bool radio_main_send_system_frame(uint8_t type,
                                         uint32_t dst_id,
                                         const uint8_t *payload,
                                         uint16_t payload_len);
static bool radio_main_send_raw_with_retry(const uint8_t *data, uint8_t len);
static bool radio_main_send_template_internal(uint8_t group_id, uint8_t msg_id, uint32_t dst_id);
static void radio_main_handle_events(void);
static void radio_main_handle_rx_packet(const radio_packet_t *pkt);
static void radio_main_handle_hopping(void);
static void radio_main_handle_auto_ping(void);
static void radio_main_ensure_rx_continuous(void);
static void radio_main_watchdog_tx(void);
static void radio_main_notify(menu_notification_type_t type, const char *text);
static void radio_main_print_rx_ascii(const uint8_t *data, uint8_t len);
static bool radio_main_finish_pairing(bool accept);
static bool radio_main_send_join_request_internal(void);
static uint32_t radio_main_auth_tag_compute(const uint8_t key[16],
                                            const beko_net_frame_t *frame,
                                            const uint8_t *cipher_payload,
                                            uint16_t cipher_len);
static void radio_main_auth_tag_write_be(uint32_t tag, uint8_t out[RADIO_AUTH_TAG_LEN]);
static uint32_t radio_main_auth_tag_read_be(const uint8_t in[RADIO_AUTH_TAG_LEN]);
static void radio_main_make_pair_code(uint8_t *code_out, uint8_t len);
static void radio_main_pair_code_to_text(const uint8_t *code, uint8_t len, char *out, uint8_t out_size);
static uint32_t radio_main_now_ms(void);
static uint32_t radio_main_tx_timeout_ms(uint16_t payload_len);
static void radio_main_tx_mark_started(uint16_t payload_len);
static void radio_main_tx_clear(void);
static bool radio_main_tx_timed_out(void);

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

bool radio_main_cmd_send_join_req(void)
{
    radio_main_cmd_t cmd;
    radio_main_cmd_sync_t sync;

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = RADIO_MAIN_CMD_SEND_JOIN_REQ;
    return radio_main_enqueue_sync(&cmd, &sync);
}

bool radio_main_cmd_send_trust_removed(uint32_t dst_id)
{
    radio_main_cmd_t cmd;
    radio_main_cmd_sync_t sync;

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = RADIO_MAIN_CMD_SEND_TRUST_REMOVED;
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
    s_ctx.node_id = beko_net_local_node_id();
    s_ctx.next_msg_id = 1U;
    s_ctx.modulation_id = RADIO_MAIN_MODULATION_LORA;
    s_ctx.lora_preset = 2U;
    beko_net_dedup_init(&s_ctx.dedup, RADIO_DEDUP_WINDOW_MS);

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
        s_ctx.lora_preset = sec_cfg.lora_preset;
        if (sec_cfg.radio_profiles_persisted)
        {
            s_ctx.modulation_id = sec_cfg.active_modulation;
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
        s_ctx.lora_preset = 2U;
    }

    if (!radio_main_validate_hop_period(s_ctx.fh_period_ms))
    {
        s_ctx.fh_period_ms = RADIO_HOP_PERIOD_DEFAULT_MS;
    }

    if (!sec_cfg.radio_profiles_persisted)
    {
        radio_main_apply_preset_cfg(s_ctx.lora_preset, &s_ctx.lora_cfg);
    }
    radio_main_apply_modulation_cfg();
    s_ctx.last_hop_ms = radio_main_now_ms();
    s_ctx.last_ping_ms = radio_main_now_ms();
    s_ctx.hop_idx = 0U;

    if (radio_main_radio_init_and_start())
    {
        printf("RADIO: init OK node=0x%08lX\r\n", (unsigned long)s_ctx.node_id);
    }
    else
    {
        printf("RADIO: init failed\r\n");
    }

    for (;;)
    {
        while (osMessageQueueGet(s_radio_cmd_queue, &cmd, NULL, 0U) == osOK)
        {
            bool cmd_result = false;

            switch (cmd.id)
            {
                case RADIO_MAIN_CMD_SEND_TEMPLATE:
                    cmd_result = radio_main_send_template_internal(cmd.u.send_template.group_id,
                                                                   cmd.u.send_template.msg_id,
                                                                   cmd.u.send_template.dst_id);
                    break;

                case RADIO_MAIN_CMD_SET_PRESET:
                    if (cmd.u.set_u8.value <= 2U)
                    {
                        s_ctx.lora_preset = cmd.u.set_u8.value;
                        radio_main_apply_preset_cfg(s_ctx.lora_preset, &s_ctx.lora_cfg);
                        if (s_ctx.modulation_id == RADIO_MAIN_MODULATION_LORA)
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
                        s_ctx.modulation_id = (radio_main_modulation_t)cmd.u.set_u8.value;
                        radio_main_apply_modulation_cfg();
                        cmd_result = radio_main_reconfigure_radio();
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

                case RADIO_MAIN_CMD_RESET_MODULE:
                    cmd_result = radio_main_reset_module_internal();
                    break;

                case RADIO_MAIN_CMD_START_PAIRING:
                    s_ctx.pairing_active = true;
                    s_ctx.pairing_pending = false;
                    s_ctx.pairing_outgoing_pending = false;
                    s_ctx.pairing_until_ms = radio_main_now_ms() + cmd.u.pairing.timeout_ms;
                    if (s_ctx.initialized && (radio_get_state() != RADIO_STATE_TX))
                    {
                        (void)radio_start_rx_continuous();
                    }
                    radio_main_notify(MENU_NOTIFICATION_PAIRING, "Pairing listen ON");
                    cmd_result = true;
                    break;

                case RADIO_MAIN_CMD_PAIRING_ACCEPT:
                    if (s_ctx.pairing_pending)
                    {
                        cmd_result = radio_main_finish_pairing(cmd.u.set_bool.enabled);
                    }
                    break;

                case RADIO_MAIN_CMD_SEND_JOIN_REQ:
                    cmd_result = radio_main_send_join_request_internal();
                    break;

                case RADIO_MAIN_CMD_SEND_TRUST_REMOVED:
                    cmd_result = radio_main_send_system_frame(BEKO_NET_TYPE_TRUST_REMOVED,
                                                              cmd.u.to_node.dst_id,
                                                              (const uint8_t *)"REMOVED",
                                                              7U);
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
            radio_main_handle_hopping();
            radio_main_handle_auto_ping();
            radio_main_ensure_rx_continuous();
        }

        if (s_ctx.pairing_active && (radio_main_now_ms() >= s_ctx.pairing_until_ms))
        {
            s_ctx.pairing_active = false;
            s_ctx.pairing_pending = false;
            s_ctx.pairing_outgoing_pending = false;
            radio_main_notify(MENU_NOTIFICATION_PAIRING, "Pairing timeout");
        }

        osDelay(10U);
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

    if (s_ctx.modulation_id == RADIO_MAIN_MODULATION_FSK)
    {
        bitrate_bps = s_ctx.fsk_cfg.bitrate_bps;
        total_bytes++;
    }
    else if (s_ctx.modulation_id == RADIO_MAIN_MODULATION_OOK)
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
static void radio_main_tx_mark_started(uint16_t payload_len)
{
    s_ctx.tx_in_progress = true;
    s_ctx.tx_deadline_ms = radio_main_now_ms() + radio_main_tx_timeout_ms(payload_len);
}

static void radio_main_tx_clear(void)
{
    s_ctx.tx_in_progress = false;
    s_ctx.tx_deadline_ms = 0U;
}

static bool radio_main_tx_timed_out(void)
{
    return (s_ctx.tx_in_progress &&
            ((int32_t)(radio_main_now_ms() - s_ctx.tx_deadline_ms) >= 0));
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
    return true;
}

static void radio_main_apply_preset_cfg(uint8_t preset_id, radio_lora_cfg_t *cfg)
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

static void radio_main_load_default_fsk_profile(radio_main_fsk_cfg_t *cfg)
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

static void radio_main_load_default_ook_profile(radio_main_ook_cfg_t *cfg)
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

    if (s_ctx.modulation_id == RADIO_MAIN_MODULATION_LORA)
    {
        if (s_ctx.backend_cfg.implicit_header && (s_ctx.backend_cfg.payload_len == 0U))
        {
            /*
             * BEKO wysyła ramki o zmiennej długości, więc implicit header traktujemy
             * jako tryb eksperymentalny. Wypełniamy `payload_len`, aby backend LoRa
             * zaakceptował konfigurację przy starcie.
             */
            s_ctx.backend_cfg.payload_len = BEKO_NET_MAX_PAYLOAD;
        }
        return;
    }

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

    if (s_ctx.modulation_id == RADIO_MAIN_MODULATION_FSK)
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
        return (st == RADIO_OK);
    }

    if (s_ctx.modulation_id == RADIO_MAIN_MODULATION_OOK)
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
            reconfigure_now = (s_ctx.modulation_id == RADIO_MAIN_MODULATION_LORA);
            break;

        case RADIO_MAIN_OPTION_LORA_FREQ:
            if (!radio_main_validate_frequency(value))
            {
                return false;
            }
            s_ctx.lora_cfg.frequency_hz = value;
            reconfigure_now = (s_ctx.modulation_id == RADIO_MAIN_MODULATION_LORA);
            break;

        case RADIO_MAIN_OPTION_LORA_BW:
            if (!radio_main_is_supported_bw((uint8_t)value))
            {
                return false;
            }
            s_ctx.lora_cfg.bandwidth = (radio_lora_bw_t)value;
            reconfigure_now = (s_ctx.modulation_id == RADIO_MAIN_MODULATION_LORA);
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
                s_ctx.lora_cfg.payload_len = BEKO_NET_MAX_PAYLOAD;
            }
            reconfigure_now = (s_ctx.modulation_id == RADIO_MAIN_MODULATION_LORA);
            break;

        case RADIO_MAIN_OPTION_LORA_CR:
            if ((value < 5UL) || (value > 8UL))
            {
                return false;
            }
            s_ctx.lora_cfg.coding_rate = (uint8_t)value;
            reconfigure_now = (s_ctx.modulation_id == RADIO_MAIN_MODULATION_LORA);
            break;

        case RADIO_MAIN_OPTION_LORA_TX_POWER:
            if (!radio_main_validate_tx_power((int32_t)value))
            {
                return false;
            }
            s_ctx.lora_cfg.tx_power_dbm = (int8_t)value;
            reconfigure_now = (s_ctx.modulation_id == RADIO_MAIN_MODULATION_LORA);
            break;

        case RADIO_MAIN_OPTION_LORA_CRC:
            if ((value != (uint32_t)RADIO_MAIN_CRC_OFF) &&
                (value != (uint32_t)RADIO_MAIN_CRC_SX1276))
            {
                return false;
            }
            s_ctx.lora_cfg.crc_on = (value != (uint32_t)RADIO_MAIN_CRC_OFF);
            reconfigure_now = (s_ctx.modulation_id == RADIO_MAIN_MODULATION_LORA);
            break;

        case RADIO_MAIN_OPTION_LORA_PREAMBLE:
            if (!radio_main_validate_preamble(value))
            {
                return false;
            }
            s_ctx.lora_cfg.preamble_len = (uint16_t)value;
            reconfigure_now = (s_ctx.modulation_id == RADIO_MAIN_MODULATION_LORA);
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
                s_ctx.lora_cfg.payload_len = BEKO_NET_MAX_PAYLOAD;
            }
            else
            {
                s_ctx.lora_cfg.payload_len = 0U;
            }
            reconfigure_now = (s_ctx.modulation_id == RADIO_MAIN_MODULATION_LORA);
            break;

        case RADIO_MAIN_OPTION_LORA_IQ_INVERT:
            if (value > 1UL)
            {
                return false;
            }
            s_ctx.lora_cfg.invert_iq = (value != 0UL);
            reconfigure_now = (s_ctx.modulation_id == RADIO_MAIN_MODULATION_LORA);
            break;

        case RADIO_MAIN_OPTION_LORA_SYNC_WORD:
            s_ctx.lora_cfg.sync_word = (uint8_t)(value & 0xFFU);
            reconfigure_now = (s_ctx.modulation_id == RADIO_MAIN_MODULATION_LORA);
            break;

        case RADIO_MAIN_OPTION_LORA_RESET_DEFAULTS:
            s_ctx.lora_preset = 2U;
            radio_main_apply_preset_cfg(s_ctx.lora_preset, &s_ctx.lora_cfg);
            reconfigure_now = (s_ctx.modulation_id == RADIO_MAIN_MODULATION_LORA);
            break;

        case RADIO_MAIN_OPTION_FSK_SHAPING:
            if (value > (uint32_t)RADIO_MAIN_FSK_SHAPING_GMSK)
            {
                return false;
            }
            s_ctx.fsk_cfg.shaping = (radio_main_fsk_shaping_t)value;
            reconfigure_now = (s_ctx.modulation_id == RADIO_MAIN_MODULATION_FSK);
            break;

        case RADIO_MAIN_OPTION_FSK_FREQ:
            if (!radio_main_validate_frequency(value))
            {
                return false;
            }
            s_ctx.fsk_cfg.frequency_hz = value;
            reconfigure_now = (s_ctx.modulation_id == RADIO_MAIN_MODULATION_FSK);
            break;

        case RADIO_MAIN_OPTION_FSK_BITRATE:
            if (!radio_main_validate_bitrate(value))
            {
                return false;
            }
            s_ctx.fsk_cfg.bitrate_bps = value;
            reconfigure_now = (s_ctx.modulation_id == RADIO_MAIN_MODULATION_FSK);
            break;

        case RADIO_MAIN_OPTION_FSK_RX_BW:
            if (!radio_main_is_supported_bw((uint8_t)value))
            {
                return false;
            }
            s_ctx.fsk_cfg.rx_bandwidth = (radio_lora_bw_t)value;
            reconfigure_now = (s_ctx.modulation_id == RADIO_MAIN_MODULATION_FSK);
            break;

        case RADIO_MAIN_OPTION_FSK_FILTER:
            if (value > (uint32_t)RADIO_MAIN_FILTER_BT_03)
            {
                return false;
            }
            s_ctx.fsk_cfg.filter = (radio_main_filter_t)value;
            reconfigure_now = (s_ctx.modulation_id == RADIO_MAIN_MODULATION_FSK);
            break;

        case RADIO_MAIN_OPTION_FSK_TX_POWER:
            if (!radio_main_validate_tx_power((int32_t)value))
            {
                return false;
            }
            s_ctx.fsk_cfg.tx_power_dbm = (int8_t)value;
            reconfigure_now = (s_ctx.modulation_id == RADIO_MAIN_MODULATION_FSK);
            break;

        case RADIO_MAIN_OPTION_FSK_PREAMBLE:
            if (!radio_main_validate_preamble(value))
            {
                return false;
            }
            s_ctx.fsk_cfg.preamble_len = (uint16_t)value;
            reconfigure_now = (s_ctx.modulation_id == RADIO_MAIN_MODULATION_FSK);
            break;

        case RADIO_MAIN_OPTION_FSK_SYNC_LEN:
            if (value > 4UL)
            {
                return false;
            }
            s_ctx.fsk_cfg.sync_word_len = (uint8_t)value;
            reconfigure_now = (s_ctx.modulation_id == RADIO_MAIN_MODULATION_FSK);
            break;

        case RADIO_MAIN_OPTION_FSK_SYNC_WORD:
            s_ctx.fsk_cfg.sync_word = value;
            reconfigure_now = (s_ctx.modulation_id == RADIO_MAIN_MODULATION_FSK);
            break;

        case RADIO_MAIN_OPTION_FSK_ADDRESS_FILTER:
            if (value > (uint32_t)RADIO_MAIN_ADDRESS_FILTER_NODE_BROADCAST)
            {
                return false;
            }
            s_ctx.fsk_cfg.address_filter = (radio_main_address_filter_t)value;
            reconfigure_now = (s_ctx.modulation_id == RADIO_MAIN_MODULATION_FSK);
            break;

        case RADIO_MAIN_OPTION_FSK_CRC:
            if (value > (uint32_t)RADIO_MAIN_CRC_CCITT)
            {
                return false;
            }
            s_ctx.fsk_cfg.crc_type = (radio_main_crc_type_t)value;
            reconfigure_now = (s_ctx.modulation_id == RADIO_MAIN_MODULATION_FSK);
            break;

        case RADIO_MAIN_OPTION_FSK_WHITENING:
            if (value > 1UL)
            {
                return false;
            }
            s_ctx.fsk_cfg.data_whitening = (value != 0UL);
            reconfigure_now = (s_ctx.modulation_id == RADIO_MAIN_MODULATION_FSK);
            break;

        case RADIO_MAIN_OPTION_FSK_RESET_DEFAULTS:
            radio_main_load_default_fsk_profile(&s_ctx.fsk_cfg);
            reconfigure_now = (s_ctx.modulation_id == RADIO_MAIN_MODULATION_FSK);
            break;

        case RADIO_MAIN_OPTION_OOK_FREQ:
            if (!radio_main_validate_frequency(value))
            {
                return false;
            }
            s_ctx.ook_cfg.frequency_hz = value;
            reconfigure_now = (s_ctx.modulation_id == RADIO_MAIN_MODULATION_OOK);
            break;

        case RADIO_MAIN_OPTION_OOK_BITRATE:
            if (!radio_main_validate_bitrate(value))
            {
                return false;
            }
            s_ctx.ook_cfg.bitrate_bps = value;
            reconfigure_now = (s_ctx.modulation_id == RADIO_MAIN_MODULATION_OOK);
            break;

        case RADIO_MAIN_OPTION_OOK_TX_POWER:
            if (!radio_main_validate_tx_power((int32_t)value))
            {
                return false;
            }
            s_ctx.ook_cfg.tx_power_dbm = (int8_t)value;
            reconfigure_now = (s_ctx.modulation_id == RADIO_MAIN_MODULATION_OOK);
            break;

        case RADIO_MAIN_OPTION_OOK_RX_BW:
            if (!radio_main_is_supported_bw((uint8_t)value))
            {
                return false;
            }
            s_ctx.ook_cfg.rx_bandwidth = (radio_lora_bw_t)value;
            reconfigure_now = (s_ctx.modulation_id == RADIO_MAIN_MODULATION_OOK);
            break;

        case RADIO_MAIN_OPTION_OOK_PREAMBLE:
            if (!radio_main_validate_preamble(value))
            {
                return false;
            }
            s_ctx.ook_cfg.preamble_len = (uint16_t)value;
            reconfigure_now = (s_ctx.modulation_id == RADIO_MAIN_MODULATION_OOK);
            break;

        case RADIO_MAIN_OPTION_OOK_SYNC_LEN:
            if (value > 4UL)
            {
                return false;
            }
            s_ctx.ook_cfg.sync_word_len = (uint8_t)value;
            reconfigure_now = (s_ctx.modulation_id == RADIO_MAIN_MODULATION_OOK);
            break;

        case RADIO_MAIN_OPTION_OOK_SYNC_WORD:
            s_ctx.ook_cfg.sync_word = value;
            reconfigure_now = (s_ctx.modulation_id == RADIO_MAIN_MODULATION_OOK);
            break;

        case RADIO_MAIN_OPTION_OOK_THRESHOLD_TYPE:
            if (value > (uint32_t)RADIO_MAIN_OOK_THRESHOLD_AVERAGE)
            {
                return false;
            }
            s_ctx.ook_cfg.threshold = (radio_main_ook_threshold_t)value;
            reconfigure_now = (s_ctx.modulation_id == RADIO_MAIN_MODULATION_OOK);
            break;

        case RADIO_MAIN_OPTION_OOK_THRESHOLD_VALUE:
            if (value > 255UL)
            {
                return false;
            }
            s_ctx.ook_cfg.threshold_value = (uint8_t)value;
            reconfigure_now = (s_ctx.modulation_id == RADIO_MAIN_MODULATION_OOK);
            break;

        case RADIO_MAIN_OPTION_OOK_RESET_DEFAULTS:
            radio_main_load_default_ook_profile(&s_ctx.ook_cfg);
            reconfigure_now = (s_ctx.modulation_id == RADIO_MAIN_MODULATION_OOK);
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

static bool radio_main_send_system_frame(uint8_t type,
                                         uint32_t dst_id,
                                         const uint8_t *payload,
                                         uint16_t payload_len)
{
    beko_net_frame_t frame;
    uint8_t raw[RADIO_MSG_BUF_MAX];
    uint16_t raw_len = 0U;
    uint8_t key[16];

    if (!s_ctx.initialized)
    {
        return false;
    }
    if (payload_len > BEKO_NET_MAX_PAYLOAD)
    {
        return false;
    }

    memset(&frame, 0, sizeof(frame));
    frame.type = type;
    frame.flags = 0U;
    frame.ttl = BEKO_NET_DEFAULT_TTL;
    frame.src_id = s_ctx.node_id;
    frame.dst_id = dst_id;
    frame.msg_id = s_ctx.next_msg_id++;
    frame.payload_len = payload_len;
    if ((payload_len > 0U) && (payload != NULL))
    {
        memcpy(frame.payload, payload, payload_len);
    }

    if ((type == BEKO_NET_TYPE_USER) &&
        s_ctx.coding_enabled)
    {
        uint32_t tag;

        if ((uint16_t)(payload_len + RADIO_AUTH_TAG_LEN) > BEKO_NET_MAX_PAYLOAD)
        {
            return false;
        }
        if (!security_main_get_network_key(key))
        {
            return false;
        }
        beko_net_xtea_ctr_crypt(frame.payload, frame.payload_len, key, frame.msg_id);
        tag = radio_main_auth_tag_compute(key, &frame, frame.payload, frame.payload_len);
        memmove(&frame.payload[RADIO_AUTH_TAG_LEN], frame.payload, frame.payload_len);
        radio_main_auth_tag_write_be(tag, frame.payload);
        frame.payload_len = (uint16_t)(frame.payload_len + RADIO_AUTH_TAG_LEN);
        frame.flags |= BEKO_NET_FLAG_CODED;
        frame.flags |= BEKO_NET_FLAG_AUTH;
    }

    if (!beko_net_encode(&frame, raw, sizeof(raw), &raw_len))
    {
        return false;
    }

    return radio_main_send_raw_with_retry(raw, (uint8_t)raw_len);
}

/*
 * Starts TX and retries once after a forced radio recovery if the backend looks
 * wedged. This protects the user-facing send path from leaving SX1276 stuck in TX.
 */
static bool radio_main_send_raw_with_retry(const uint8_t *data, uint8_t len)
{
    radio_status_t st;

    if ((data == NULL) || (len == 0U))
    {
        return false;
    }

    if (!s_ctx.initialized && !radio_main_force_recover_radio("send while uninitialized"))
    {
        return false;
    }

    st = radio_send_async(data, len);
    if (st == RADIO_OK)
    {
        radio_main_tx_mark_started(len);
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
        radio_main_tx_mark_started(len);
        return true;
    }

    return false;
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

    return radio_main_send_system_frame(BEKO_NET_TYPE_USER, dst_id, (const uint8_t *)msg, len);
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
        printf("RADIO EVT: TX_DONE\r\n");
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

static void radio_main_handle_rx_packet(const radio_packet_t *pkt)
{
    beko_net_frame_t frame;
    beko_net_frame_t frame_decoded;
    char type_text[16];
    bool is_beko;
    bool duplicate;
    bool for_me;
    uint8_t key[16];
    uint8_t tx_buf[RADIO_MSG_BUF_MAX];
    uint16_t tx_len = 0U;
    menu_notification_t n;

    if ((pkt == NULL) || (pkt->length == 0U))
    {
        return;
    }

    printf("RADIO RX len=%u RSSI=%d SNR=%d\r\n", pkt->length, pkt->rssi_dbm, pkt->snr_db);
    radio_main_print_rx_ascii(pkt->data, pkt->length);
    (void)security_main_log_message(pkt->rssi_dbm, pkt->data, pkt->length);

    is_beko = beko_net_decode(pkt->data, pkt->length, &frame);
    if (is_beko)
    {
        frame_decoded = frame;
        if ((frame.flags & BEKO_NET_FLAG_CODED) != 0U)
        {
            if (frame_decoded.type == BEKO_NET_TYPE_USER)
            {
                uint16_t cipher_len;
                uint32_t rx_tag;
                uint32_t expected_tag;

                if (!security_main_get_network_key(key))
                {
                    if (!security_main_get_peer_link_key(s_ctx.node_id, frame_decoded.src_id, key))
                    {
                        printf("RADIO RX coded from unknown src=0x%08lX\r\n",
                               (unsigned long)frame_decoded.src_id);
                        return;
                    }
                }
                if (((frame.flags & BEKO_NET_FLAG_AUTH) == 0U) ||
                    (frame_decoded.payload_len < RADIO_AUTH_TAG_LEN))
                {
                    printf("RADIO RX coded without auth src=0x%08lX\r\n",
                           (unsigned long)frame_decoded.src_id);
                    return;
                }

                cipher_len = (uint16_t)(frame_decoded.payload_len - RADIO_AUTH_TAG_LEN);
                rx_tag = radio_main_auth_tag_read_be(frame_decoded.payload);
                expected_tag = radio_main_auth_tag_compute(key,
                                                           &frame_decoded,
                                                           &frame_decoded.payload[RADIO_AUTH_TAG_LEN],
                                                           cipher_len);
                if ((rx_tag ^ expected_tag) != 0UL)
                {
                    printf("RADIO RX auth mismatch src=0x%08lX\r\n",
                           (unsigned long)frame_decoded.src_id);
                    return;
                }

                if (cipher_len > 0U)
                {
                    memmove(frame_decoded.payload,
                            &frame_decoded.payload[RADIO_AUTH_TAG_LEN],
                            cipher_len);
                }
                frame_decoded.payload_len = cipher_len;
            }
            else if (!security_main_get_network_key(key))
            {
                return;
            }

            beko_net_xtea_ctr_crypt(frame_decoded.payload,
                                    frame_decoded.payload_len,
                                    key,
                                    frame_decoded.msg_id);
        }
        else if (s_ctx.coding_enabled && (frame_decoded.type == BEKO_NET_TYPE_USER))
        {
            /* In secure mode ignore uncoded user payloads. */
            return;
        }

        if (frame_decoded.payload_len > 0U)
        {
            uint16_t i;

            printf("RADIO RX DEC: ");
            for (i = 0U; i < frame_decoded.payload_len; i++)
            {
                char c = (char)frame_decoded.payload[i];
                printf("%c", isprint((unsigned char)c) ? c : '.');
            }
            printf("\r\n");

            (void)lcd_main_push_message(pkt->rssi_dbm, frame_decoded.payload, frame_decoded.payload_len);
        }
        else
        {
            snprintf(type_text, sizeof(type_text), "TYPE:%u", frame_decoded.type);
            (void)lcd_main_push_message(pkt->rssi_dbm, (const uint8_t *)type_text, strlen(type_text));
        }
    }
    else
    {
        (void)lcd_main_push_message(pkt->rssi_dbm, pkt->data, pkt->length);
        return;
    }

    duplicate = beko_net_dedup_seen_or_add(&s_ctx.dedup,
                                           frame.src_id,
                                           frame.msg_id,
                                           radio_main_now_ms());
    for_me = beko_net_is_for_node(&frame_decoded, s_ctx.node_id);

    if (for_me)
    {
        if (frame_decoded.type == BEKO_NET_TYPE_JOIN_REQ)
        {
            if (s_ctx.pairing_active && !s_ctx.pairing_pending)
            {
                char pair_note[21];
                char code_text[12];

                s_ctx.pairing_pending = true;
                s_ctx.pairing_pending_node = frame_decoded.src_id;
                s_ctx.pairing_pending_code_len = (frame_decoded.payload_len > sizeof(s_ctx.pairing_pending_code)) ?
                                                 sizeof(s_ctx.pairing_pending_code) : (uint8_t)frame_decoded.payload_len;
                memcpy(s_ctx.pairing_pending_code, frame_decoded.payload, s_ctx.pairing_pending_code_len);

                radio_main_pair_code_to_text(s_ctx.pairing_pending_code,
                                             s_ctx.pairing_pending_code_len,
                                             code_text,
                                             (uint8_t)sizeof(code_text));
                snprintf(pair_note, sizeof(pair_note), "JOIN_REQ %s", code_text);
                radio_main_notify(MENU_NOTIFICATION_PAIRING, pair_note);
            }
        }
        else if (frame_decoded.type == BEKO_NET_TYPE_JOIN_ACCEPT)
        {
            if (s_ctx.pairing_active && s_ctx.pairing_outgoing_pending)
            {
                bool code_match = false;
                char code_text[12];

                if ((frame_decoded.payload_len == s_ctx.pairing_outgoing_code_len) &&
                    (frame_decoded.payload_len > 0U) &&
                    (memcmp(frame_decoded.payload, s_ctx.pairing_outgoing_code, frame_decoded.payload_len) == 0))
                {
                    code_match = true;
                }

                radio_main_pair_code_to_text(s_ctx.pairing_outgoing_code,
                                             s_ctx.pairing_outgoing_code_len,
                                             code_text,
                                             (uint8_t)sizeof(code_text));

                if (code_match && security_main_cmd_add_device(frame_decoded.src_id,
                                                               s_ctx.pairing_outgoing_code,
                                                               s_ctx.pairing_outgoing_code_len))
                {
                    char pair_note[21];
                    snprintf(pair_note, sizeof(pair_note), "JOIN_OK %s", code_text);
                    radio_main_notify(MENU_NOTIFICATION_PAIRING, pair_note);
                    s_ctx.pairing_active = false;
                }
                else
                {
                    radio_main_notify(MENU_NOTIFICATION_ERROR, "JOIN code mismatch");
                }

                s_ctx.pairing_outgoing_pending = false;
            }
        }
        else if (frame_decoded.type == BEKO_NET_TYPE_JOIN_REJECT)
        {
            if (s_ctx.pairing_active && s_ctx.pairing_outgoing_pending)
            {
                radio_main_notify(MENU_NOTIFICATION_PAIRING, "JOIN_REJECT");
                s_ctx.pairing_outgoing_pending = false;
            }
        }
        else if (frame_decoded.type == BEKO_NET_TYPE_TRUST_REMOVED)
        {
            if (security_main_cmd_delete_device(frame_decoded.src_id))
            {
                radio_main_notify(MENU_NOTIFICATION_WARNING, "Removed by peer");
            }
            else
            {
                radio_main_notify(MENU_NOTIFICATION_WARNING, "Peer removed trust");
            }
        }
        else
        {
            memset(&n, 0, sizeof(n));
            n.type = MENU_NOTIFICATION_RX;
            n.rssi_dbm = pkt->rssi_dbm;
            n.device_code = frame_decoded.src_id;
            if (frame_decoded.payload_len > 0U)
            {
                uint8_t i;
                uint8_t copy_len = (frame_decoded.payload_len > 20U) ? 20U : (uint8_t)frame_decoded.payload_len;
                for (i = 0U; i < copy_len; i++)
                {
                    char c = (char)frame_decoded.payload[i];
                    n.text[i] = isprint((unsigned char)c) ? c : '.';
                }
                n.text[copy_len] = '\0';
            }
            else
            {
                snprintf(n.text, sizeof(n.text), "Type %u", frame_decoded.type);
            }
            (void)menu_main_post_notification(&n);
        }
    }

    if (!duplicate && beko_net_should_forward(&frame, s_ctx.node_id))
    {
        frame.ttl--;
        if (beko_net_encode(&frame, tx_buf, sizeof(tx_buf), &tx_len))
        {
            if (radio_main_send_raw_with_retry(tx_buf, (uint8_t)tx_len))
            {
                printf("RADIO relay src=0x%08lX msg=0x%08lX ttl=%u\r\n",
                       (unsigned long)frame.src_id,
                       (unsigned long)frame.msg_id,
                       frame.ttl);
            }
        }
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
    (void)radio_main_reconfigure_radio();
}

static void radio_main_handle_auto_ping(void)
{
    uint32_t now;

    if (!s_ctx.auto_ping_enabled)
    {
        return;
    }
    if (!s_ctx.initialized)
    {
        return;
    }

    now = radio_main_now_ms();
    if ((now - s_ctx.last_ping_ms) < RADIO_AUTO_PING_PERIOD_MS)
    {
        return;
    }
    s_ctx.last_ping_ms = now;

    if (radio_get_state() != RADIO_STATE_TX)
    {
        (void)radio_main_send_template_internal(2U, 0U, BEKO_NET_BROADCAST_ID);
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

static void radio_main_print_rx_ascii(const uint8_t *data, uint8_t len)
{
    uint8_t i;

    printf("RADIO RX TEXT: ");
    for (i = 0U; i < len; i++)
    {
        char c = (char)data[i];
        printf("%c", isprint((unsigned char)c) ? c : '.');
    }
    printf("\r\n");
}

static bool radio_main_finish_pairing(bool accept)
{
    bool ok;
    menu_notification_t n;
    uint32_t node_id;
    uint8_t code_len;
    const uint8_t *code;
    char code_text[12];

    if (!s_ctx.pairing_pending)
    {
        return false;
    }

    node_id = s_ctx.pairing_pending_node;
    code = s_ctx.pairing_pending_code;
    code_len = s_ctx.pairing_pending_code_len;

    if (!accept)
    {
        (void)radio_main_send_system_frame(BEKO_NET_TYPE_JOIN_REJECT,
                                           node_id,
                                           (const uint8_t *)"REJECT",
                                           6U);
        memset(&n, 0, sizeof(n));
        n.type = MENU_NOTIFICATION_PAIRING;
        snprintf(n.text, sizeof(n.text), "Device rejected");
        (void)menu_main_post_notification(&n);
        s_ctx.pairing_pending = false;
        return true;
    }

    ok = security_main_cmd_add_device(node_id, code, code_len);
    if (!ok)
    {
        radio_main_notify(MENU_NOTIFICATION_ERROR, "Pairing save failed");
        s_ctx.pairing_pending = false;
        return false;
    }

    (void)radio_main_send_system_frame(BEKO_NET_TYPE_JOIN_ACCEPT, node_id, code, code_len);

    radio_main_pair_code_to_text(code, code_len, code_text, (uint8_t)sizeof(code_text));

    memset(&n, 0, sizeof(n));
    n.type = MENU_NOTIFICATION_PAIRING;
    snprintf(n.text, sizeof(n.text), "JOIN_OK %s", code_text);
    (void)menu_main_post_notification(&n);
    s_ctx.pairing_pending = false;
    return true;
}

static bool radio_main_send_join_request_internal(void)
{
    char note[21];
    char code_text[12];

    if (!s_ctx.pairing_active)
    {
        return false;
    }

    s_ctx.pairing_outgoing_code_len = RADIO_PAIR_CODE_LEN;
    radio_main_make_pair_code(s_ctx.pairing_outgoing_code, s_ctx.pairing_outgoing_code_len);

    if (!radio_main_send_system_frame(BEKO_NET_TYPE_JOIN_REQ,
                                      BEKO_NET_BROADCAST_ID,
                                      s_ctx.pairing_outgoing_code,
                                      s_ctx.pairing_outgoing_code_len))
    {
        return false;
    }

    s_ctx.pairing_outgoing_pending = true;
    radio_main_pair_code_to_text(s_ctx.pairing_outgoing_code,
                                 s_ctx.pairing_outgoing_code_len,
                                 code_text,
                                 (uint8_t)sizeof(code_text));
    snprintf(note, sizeof(note), "JOIN_SENT %.10s", code_text);
    radio_main_notify(MENU_NOTIFICATION_PAIRING, note);
    return true;
}

static uint32_t radio_main_auth_tag_compute(const uint8_t key[16],
                                            const beko_net_frame_t *frame,
                                            const uint8_t *cipher_payload,
                                            uint16_t cipher_len)
{
    uint32_t h = 2166136261UL;
    uint8_t i;

    if ((key == NULL) || (frame == NULL))
    {
        return 0UL;
    }

#define RADIO_AUTH_FNV_MIX(x) \
    do                        \
    {                         \
        h ^= (uint8_t)(x);    \
        h *= 16777619UL;      \
    } while (0)

    for (i = 0U; i < 16U; i++)
    {
        RADIO_AUTH_FNV_MIX(key[i]);
    }

    RADIO_AUTH_FNV_MIX(frame->type);
    RADIO_AUTH_FNV_MIX((uint8_t)(frame->src_id >> 24));
    RADIO_AUTH_FNV_MIX((uint8_t)(frame->src_id >> 16));
    RADIO_AUTH_FNV_MIX((uint8_t)(frame->src_id >> 8));
    RADIO_AUTH_FNV_MIX((uint8_t)frame->src_id);
    RADIO_AUTH_FNV_MIX((uint8_t)(frame->dst_id >> 24));
    RADIO_AUTH_FNV_MIX((uint8_t)(frame->dst_id >> 16));
    RADIO_AUTH_FNV_MIX((uint8_t)(frame->dst_id >> 8));
    RADIO_AUTH_FNV_MIX((uint8_t)frame->dst_id);
    RADIO_AUTH_FNV_MIX((uint8_t)(frame->msg_id >> 24));
    RADIO_AUTH_FNV_MIX((uint8_t)(frame->msg_id >> 16));
    RADIO_AUTH_FNV_MIX((uint8_t)(frame->msg_id >> 8));
    RADIO_AUTH_FNV_MIX((uint8_t)frame->msg_id);
    RADIO_AUTH_FNV_MIX((uint8_t)(cipher_len >> 8));
    RADIO_AUTH_FNV_MIX((uint8_t)cipher_len);

    if ((cipher_payload != NULL) && (cipher_len > 0U))
    {
        for (i = 0U; i < cipher_len; i++)
        {
            RADIO_AUTH_FNV_MIX(cipher_payload[i]);
        }
    }

#undef RADIO_AUTH_FNV_MIX

    h ^= (h >> 13);
    h *= 0x9E3779B1UL;
    h ^= (h >> 16);
    return h;
}

static void radio_main_auth_tag_write_be(uint32_t tag, uint8_t out[RADIO_AUTH_TAG_LEN])
{
    if (out == NULL)
    {
        return;
    }

    out[0] = (uint8_t)(tag >> 24);
    out[1] = (uint8_t)(tag >> 16);
    out[2] = (uint8_t)(tag >> 8);
    out[3] = (uint8_t)tag;
}

static uint32_t radio_main_auth_tag_read_be(const uint8_t in[RADIO_AUTH_TAG_LEN])
{
    if (in == NULL)
    {
        return 0UL;
    }

    return ((uint32_t)in[0] << 24) |
           ((uint32_t)in[1] << 16) |
           ((uint32_t)in[2] << 8) |
           (uint32_t)in[3];
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
