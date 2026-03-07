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
#define RADIO_HOP_PERIOD_MS                  2000UL
#define RADIO_DEDUP_WINDOW_MS                60000UL
#define RADIO_PAIR_CODE_LEN                  6U
#define RADIO_AUTH_TAG_LEN                   4U

typedef enum
{
    RADIO_MAIN_CMD_NONE = 0,
    RADIO_MAIN_CMD_SEND_TEMPLATE,
    RADIO_MAIN_CMD_SET_PRESET,
    RADIO_MAIN_CMD_SET_MODULATION,
    RADIO_MAIN_CMD_SET_FH,
    RADIO_MAIN_CMD_SET_CODING,
    RADIO_MAIN_CMD_SET_AUTO_PING,
    RADIO_MAIN_CMD_START_PAIRING,
    RADIO_MAIN_CMD_PAIRING_ACCEPT,
    RADIO_MAIN_CMD_SEND_JOIN_REQ,
    RADIO_MAIN_CMD_SEND_TRUST_REMOVED
} radio_main_cmd_id_t;

typedef struct
{
    volatile bool done;
    bool result;
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
    radio_lora_cfg_t lora_cfg;
    uint8_t modulation_id;
    bool initialized;
    bool fh_enabled;
    bool coding_enabled;
    bool auto_ping_enabled;
    uint8_t lora_preset;
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
static bool radio_main_reconfigure_radio(void);
static bool radio_main_send_system_frame(uint8_t type,
                                         uint32_t dst_id,
                                         const uint8_t *payload,
                                         uint16_t payload_len);
static bool radio_main_send_template_internal(uint8_t group_id, uint8_t msg_id, uint32_t dst_id);
static void radio_main_handle_events(void);
static void radio_main_handle_rx_packet(const radio_packet_t *pkt);
static void radio_main_handle_hopping(void);
static void radio_main_handle_auto_ping(void);
static void radio_main_ensure_rx_continuous(void);
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

bool radio_main_cmd_set_fh(bool enabled)
{
    radio_main_cmd_t cmd;
    radio_main_cmd_sync_t sync;

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = RADIO_MAIN_CMD_SET_FH;
    cmd.u.set_bool.enabled = enabled;
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
    s_ctx.node_id = beko_net_local_node_id();
    s_ctx.next_msg_id = 1U;
    s_ctx.modulation_id = 0U;
    s_ctx.lora_preset = 0U;
    beko_net_dedup_init(&s_ctx.dedup, RADIO_DEDUP_WINDOW_MS);

    radio_default_hw_cfg(&s_ctx.hw, &hspi1);
    radio_default_lora_cfg(&s_ctx.lora_cfg);

    if (security_main_cmd_get_runtime_cfg(&sec_cfg))
    {
        s_ctx.coding_enabled = sec_cfg.coding_enabled;
        s_ctx.fh_enabled = false;
        s_ctx.auto_ping_enabled = false;
        s_ctx.lora_preset = 0U;
    }
    else
    {
        s_ctx.fh_enabled = false;
        s_ctx.coding_enabled = false;
        s_ctx.auto_ping_enabled = false;
        s_ctx.lora_preset = 0U;
    }

    radio_main_apply_preset_cfg(s_ctx.lora_preset, &s_ctx.lora_cfg);
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
                        cmd_result = radio_main_reconfigure_radio();
                    }
                    break;

                case RADIO_MAIN_CMD_SET_MODULATION:
                    s_ctx.modulation_id = cmd.u.set_u8.value;
                    if (s_ctx.modulation_id == 0U)
                    {
                        cmd_result = true;
                    }
                    else
                    {
                        radio_main_notify(MENU_NOTIFICATION_ERROR,
                                          (s_ctx.modulation_id == 1U) ? "FSK not implemented" : "OOK not implemented");
                        cmd_result = false;
                    }
                    break;

                case RADIO_MAIN_CMD_SET_FH:
                    s_ctx.fh_enabled = cmd.u.set_bool.enabled;
                    s_ctx.last_hop_ms = radio_main_now_ms();
                    cmd_result = true;
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

static bool radio_main_radio_init_and_start(void)
{
    radio_status_t st;

    st = radio_init(&s_ctx.hw, &s_ctx.lora_cfg, NULL, NULL);
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
            break;

        case 1U: /* RANGE */
            cfg->frequency_hz = 868300000UL;
            cfg->bandwidth = RADIO_LORA_BW_125_KHZ;
            cfg->spreading_factor = 12U;
            cfg->coding_rate = 5U;
            break;

        case 2U: /* FAST */
            cfg->frequency_hz = 868500000UL;
            cfg->bandwidth = RADIO_LORA_BW_500_KHZ;
            cfg->spreading_factor = 7U;
            cfg->coding_rate = 5U;
            break;

        default:
            break;
    }
}

static bool radio_main_reconfigure_radio(void)
{
    if (!s_ctx.initialized)
    {
        return false;
    }
    if (radio_get_state() == RADIO_STATE_TX)
    {
        return false;
    }

    (void)radio_standby();
    (void)radio_deinit();
    s_ctx.initialized = false;
    return radio_main_radio_init_and_start();
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

        if (dst_id == BEKO_NET_BROADCAST_ID)
        {
            return false;
        }
        if ((uint16_t)(payload_len + RADIO_AUTH_TAG_LEN) > BEKO_NET_MAX_PAYLOAD)
        {
            return false;
        }
        if (!security_main_get_peer_link_key(s_ctx.node_id, dst_id, key))
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

    return (radio_send_async(raw, (uint8_t)raw_len) == RADIO_OK);
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

    if (s_ctx.coding_enabled && (dst_id == BEKO_NET_BROADCAST_ID))
    {
        trusted_info_t info;
        uint8_t idx;
        bool sent_any = false;

        for (idx = 0U; idx < 16U; idx++)
        {
            if (security_main_cmd_get_device(idx, &info) && info.in_use)
            {
                uint32_t wait_start = radio_main_now_ms();

                while (radio_get_state() == RADIO_STATE_TX)
                {
                    radio_process();
                    if ((radio_main_now_ms() - wait_start) > 250U)
                    {
                        break;
                    }
                    osDelay(2U);
                }

                if (radio_main_send_system_frame(BEKO_NET_TYPE_USER,
                                                 info.node_id,
                                                 (const uint8_t *)msg,
                                                 len))
                {
                    sent_any = true;
                }
            }
        }
        return sent_any;
    }

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
        radio_main_ensure_rx_continuous();
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

                if (!security_main_get_peer_link_key(s_ctx.node_id, frame_decoded.src_id, key))
                {
                    printf("RADIO RX coded from unknown src=0x%08lX\r\n",
                           (unsigned long)frame_decoded.src_id);
                    return;
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
            if (radio_send_async(tx_buf, (uint8_t)tx_len) == RADIO_OK)
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
    if ((now - s_ctx.last_hop_ms) < RADIO_HOP_PERIOD_MS)
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

static void radio_main_ensure_rx_continuous(void)
{
    if (!s_ctx.initialized)
    {
        return;
    }
    if (radio_get_state() == RADIO_STATE_TX)
    {
        return;
    }
    if (radio_get_state() == RADIO_STATE_RX_CONT)
    {
        return;
    }

    (void)radio_start_rx_continuous();
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
