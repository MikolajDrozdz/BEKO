#include "security_main.h"

#include "beko_net_proto.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "i2c_mem_store_lib/i2c_mem_store.h"
#include "main.h"
#include "st33ktpm2x_lib/st33ktpm2x.h"
#include "task.h"

#include <stdio.h>
#include <string.h>

#define SECURITY_TASK_STACK_SIZE            6144U
#define SECURITY_TASK_STACK_WORDS           (SECURITY_TASK_STACK_SIZE / sizeof(StackType_t))
#define SECURITY_CMD_QUEUE_DEPTH            16U
#define SECURITY_CMD_WAIT_MS                3000U
#define SECURITY_CMD_POLL_MS                5U
#define SECURITY_TRUSTED_MAX                16U
#define SECURITY_STORE_SLOT                 0U
#define SECURITY_STORE_RADIO_SLOT           1U
#define SECURITY_TRUSTED_SLOT_BASE          2U
#define SECURITY_STORE_MAGIC                0xA5U
#define SECURITY_STORE_VERSION              3U
#define SECURITY_LEGACY_SETTINGS_SLOT       0U
#define SECURITY_LEGACY_KEY_SEED_SLOT       1U
#define SECURITY_SETTINGS_MAGIC             0x5345U
#define SECURITY_SETTINGS_VERSION           1U
#define SECURITY_KEY_MAGIC                  0x4B59U
#define SECURITY_KEY_SEED_BYTES             8U
#define SECURITY_CODE_MAX                   I2C_MEM_STORE_TRUSTED_CODE_MAX
#define SECURITY_TRUSTED_ID_LEN             4U

typedef enum
{
    SECURITY_CMD_NONE = 0,
    SECURITY_CMD_ADD_DEVICE,
    SECURITY_CMD_DELETE_DEVICE,
    SECURITY_CMD_GET_DEVICE,
    SECURITY_CMD_SET_CODING,
    SECURITY_CMD_SET_FH,
    SECURITY_CMD_SET_FH_PERIOD,
    SECURITY_CMD_ROTATE_KEY,
    SECURITY_CMD_SET_NOTIFY,
    SECURITY_CMD_SET_PRESET,
    SECURITY_CMD_SET_AUTO_PING,
    SECURITY_CMD_SET_RADIO_RUNTIME,
    SECURITY_CMD_GET_RUNTIME,
    SECURITY_CMD_LOG_MESSAGE
} security_cmd_id_t;

typedef struct
{
    volatile bool done;
    bool result;
    trusted_info_t trusted_out;
    security_runtime_cfg_t runtime_out;
} security_cmd_sync_t;

typedef struct
{
    security_cmd_id_t id;
    security_cmd_sync_t *sync;
    union
    {
        struct
        {
            uint32_t node_id;
            uint8_t code_len;
            uint8_t code[SECURITY_CODE_MAX];
        } add_device;
        struct
        {
            uint32_t node_id;
        } del_device;
        struct
        {
            uint8_t index;
        } get_device;
        struct
        {
            bool enabled;
        } set_bool;
        struct
        {
            uint32_t value;
        } set_u32;
        struct
        {
            security_notify_mode_t mode;
        } set_notify;
        struct
        {
            uint8_t preset_id;
        } set_preset;
        struct
        {
            radio_main_runtime_cfg_t cfg;
        } set_radio;
        struct
        {
            int16_t rssi_dbm;
            uint8_t len;
            uint8_t payload[I2C_MEM_STORE_LOG_PAYLOAD_MAX];
        } log_msg;
    } u;
} security_cmd_t;

typedef struct
{
    bool in_use;
    uint32_t node_id;
    uint8_t code_len;
    uint8_t code[SECURITY_CODE_MAX];
} security_trusted_entry_t;

typedef struct
{
    uint8_t magic;
    uint8_t version;
    uint8_t flags;
    uint8_t notify_mode;
    uint8_t lora_preset;
    uint8_t modulation_fh;
    uint8_t seed[SECURITY_KEY_SEED_BYTES];
} security_store_wire_t;

typedef struct
{
    uint8_t magic;
    uint8_t version;
    uint8_t packed[11];
} security_radio_store_wire_t;

typedef struct
{
    uint16_t magic;
    uint8_t version;
    uint8_t flags;
    uint8_t notify_mode;
    uint8_t lora_preset;
    uint8_t reserved0;
    uint8_t reserved1;
    uint16_t crc;
} security_settings_wire_t;

typedef struct
{
    uint16_t magic;
    uint8_t seed[SECURITY_KEY_SEED_BYTES];
    uint16_t crc;
} security_key_wire_t;

static osThreadId_t s_security_task = NULL;
static osMessageQueueId_t s_security_cmd_queue = NULL;
static osMutexId_t s_security_mutex = NULL;
static StaticTask_t s_security_task_cb;
static StackType_t s_security_task_stack[SECURITY_TASK_STACK_WORDS];

static bool s_security_initialized = false;
static bool s_tpm_ready = false;
static i2c_mem_store_t s_mem_store;
static bool s_mem_ready = false;
static st33ktpm2x_t s_tpm;
static security_trusted_entry_t s_trusted[SECURITY_TRUSTED_MAX];
static security_runtime_cfg_t s_runtime_cfg =
{
    .coding_enabled = true,
    .fh_enabled = false,
    .fh_period_ms = 2000UL,
    .auto_ping_enabled = false,
    .notify_mode = SECURITY_NOTIFY_POPUP,
    .lora_preset = 2U,
    .active_modulation = RADIO_MAIN_MODULATION_LORA,
    .radio_profiles_persisted = false
};
static uint8_t s_network_key[16] =
{
    0x31U, 0x42U, 0x53U, 0x64U,
    0x75U, 0x86U, 0x97U, 0xA8U,
    0x19U, 0x2AU, 0x3BU, 0x4CU,
    0x5DU, 0x6EU, 0x7FU, 0x80U
};
static uint8_t s_key_seed_cached[SECURITY_KEY_SEED_BYTES];

extern I2C_HandleTypeDef hi2c1;
extern I2C_HandleTypeDef hi2c3;

static const uint32_t s_freq_options_hz[] =
{
    868000000UL, 868100000UL, 868300000UL, 868500000UL, 868800000UL, 869050000UL, 869525000UL
};
static const uint32_t s_bitrate_options_bps[] =
{
    1200UL, 2400UL, 4800UL, 9600UL, 19200UL, 38400UL, 50000UL, 100000UL
};
static const uint16_t s_preamble_options[] =
{
    4U, 6U, 8U, 12U, 16U, 24U, 32U
};
static const int8_t s_power_options_dbm[] =
{
    2, 5, 8, 11, 14, 17, 20
};
static const uint32_t s_sync_word_options[] =
{
    0x12UL, 0x34UL, 0x56UL, 0xA5UL, 0x55AAUL, 0x2DD4UL, 0xA55AUL, 0x00C194C1UL, 0x1ACFFC1DUL
};
static const uint8_t s_sync_len_options[] =
{
    0U, 1U, 2U, 3U, 4U
};
static const uint8_t s_ook_threshold_options[] =
{
    4U, 8U, 12U, 16U, 24U, 32U, 48U, 64U
};
static const uint32_t s_fh_period_options_ms[] =
{
    1000UL, 2000UL, 5000UL, 10000UL
};

static void security_main_task_fn(void *argument);
static bool security_main_wait_sync(security_cmd_sync_t *sync, uint32_t timeout_ms);
static bool security_main_enqueue_sync(const security_cmd_t *cmd, security_cmd_sync_t *sync);

static uint16_t security_crc16(const uint8_t *data, uint16_t len);
static void security_key_seed_to_key(const uint8_t seed[SECURITY_KEY_SEED_BYTES], uint8_t key_out[16]);
static void security_peer_link_key_derive(uint32_t local_node_id,
                                          uint32_t peer_node_id,
                                          const uint8_t *code,
                                          uint8_t code_len,
                                          uint8_t key_out[16]);
static bool security_load_runtime_and_seed_from_store(void);
static bool security_save_runtime_and_seed_to_store(void);
static bool security_commit_runtime_cfg_soft(void);
static void security_load_default_radio_profiles(security_runtime_cfg_t *cfg);
static uint8_t security_pack_bits(uint8_t *buf, uint8_t bit_pos, uint32_t value, uint8_t width);
static uint8_t security_unpack_bits(const uint8_t *buf, uint8_t bit_pos, uint8_t width, uint32_t *value_out);
static uint8_t security_index_from_u32(uint32_t value, const uint32_t *table, uint8_t count, uint8_t fallback);
static uint8_t security_index_from_u16(uint16_t value, const uint16_t *table, uint8_t count, uint8_t fallback);
static uint8_t security_index_from_i8(int8_t value, const int8_t *table, uint8_t count, uint8_t fallback);
static uint8_t security_index_from_u8(uint8_t value, const uint8_t *table, uint8_t count, uint8_t fallback);
static uint8_t security_pack_modulation_fh(radio_main_modulation_t modulation, uint8_t fh_period_idx);
static radio_main_modulation_t security_unpack_modulation(uint8_t modulation_fh);
static uint8_t security_unpack_fh_period_idx(uint8_t modulation_fh);
static uint32_t security_u32_from_index(uint8_t idx, const uint32_t *table, uint8_t count, uint32_t fallback);
static uint16_t security_u16_from_index(uint8_t idx, const uint16_t *table, uint8_t count, uint16_t fallback);
static int8_t security_i8_from_index(uint8_t idx, const int8_t *table, uint8_t count, int8_t fallback);
static uint8_t security_u8_from_index(uint8_t idx, const uint8_t *table, uint8_t count, uint8_t fallback);
static bool security_save_radio_profiles_to_store(void);
static bool security_load_radio_profiles_from_store(void);
static void security_migrate_trusted_slot_v1_to_v2(void);
static bool security_load_settings_legacy_from_store(void);
static bool security_load_key_seed_legacy_from_store(uint8_t seed[SECURITY_KEY_SEED_BYTES]);
static bool security_rotate_key_internal(void);
static bool security_add_device_internal(uint32_t node_id, const uint8_t *code, uint8_t len);
static bool security_delete_device_internal(uint32_t node_id);
static bool security_get_device_internal(uint8_t idx, trusted_info_t *out);
static uint8_t security_trusted_store_capacity(void);
static uint16_t security_trusted_store_slot(uint8_t idx);
static void security_node_id_to_bytes(uint32_t node_id, uint8_t out[SECURITY_TRUSTED_ID_LEN]);
static uint32_t security_node_id_from_bytes(const uint8_t in[SECURITY_TRUSTED_ID_LEN]);
static bool security_store_trusted_slot(uint8_t idx);
static bool security_erase_trusted_slot(uint8_t idx);
static void security_load_trusted_from_store(void);
static void security_bootstrap_tpm(void);
static void security_bootstrap_store(void);

static const osThreadAttr_t s_security_task_attr =
{
    .name = "security_task",
    .priority = (osPriority_t)osPriorityBelowNormal,
    .stack_mem = s_security_task_stack,
    .stack_size = sizeof(s_security_task_stack),
    .cb_mem = &s_security_task_cb,
    .cb_size = sizeof(s_security_task_cb)
};

void security_main_create_task(void)
{
    if (s_security_mutex == NULL)
    {
        s_security_mutex = osMutexNew(NULL);
        if (s_security_mutex == NULL)
        {
            printf("SEC: mutex create failed\r\n");
            return;
        }
    }

    if (s_security_cmd_queue == NULL)
    {
        s_security_cmd_queue = osMessageQueueNew(SECURITY_CMD_QUEUE_DEPTH, sizeof(security_cmd_t), NULL);
        if (s_security_cmd_queue == NULL)
        {
            printf("SEC: queue create failed\r\n");
            return;
        }
    }

    if (s_security_task == NULL)
    {
        s_security_task = osThreadNew(security_main_task_fn, NULL, &s_security_task_attr);
        if (s_security_task == NULL)
        {
            printf("SEC: task create failed\r\n");
        }
    }
}

bool security_main_cmd_add_device(uint32_t node_id, const uint8_t *code, uint8_t len)
{
    security_cmd_t cmd;
    security_cmd_sync_t sync;

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = SECURITY_CMD_ADD_DEVICE;
    cmd.u.add_device.node_id = node_id;
    cmd.u.add_device.code_len = (len > SECURITY_CODE_MAX) ? SECURITY_CODE_MAX : len;
    if ((cmd.u.add_device.code_len > 0U) && (code != NULL))
    {
        memcpy(cmd.u.add_device.code, code, cmd.u.add_device.code_len);
    }

    return security_main_enqueue_sync(&cmd, &sync);
}

bool security_main_cmd_delete_device(uint32_t node_id)
{
    security_cmd_t cmd;
    security_cmd_sync_t sync;

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = SECURITY_CMD_DELETE_DEVICE;
    cmd.u.del_device.node_id = node_id;

    return security_main_enqueue_sync(&cmd, &sync);
}

bool security_main_cmd_get_device(uint8_t idx, trusted_info_t *out)
{
    security_cmd_t cmd;
    security_cmd_sync_t sync;
    bool ok;

    if (out == NULL)
    {
        return false;
    }

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = SECURITY_CMD_GET_DEVICE;
    cmd.u.get_device.index = idx;

    ok = security_main_enqueue_sync(&cmd, &sync);
    if (ok)
    {
        *out = sync.trusted_out;
    }
    return ok;
}

bool security_main_cmd_set_coding(bool enabled)
{
    security_cmd_t cmd;
    security_cmd_sync_t sync;

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = SECURITY_CMD_SET_CODING;
    cmd.u.set_bool.enabled = enabled;
    return security_main_enqueue_sync(&cmd, &sync);
}

bool security_main_cmd_rotate_key(void)
{
    security_cmd_t cmd;
    security_cmd_sync_t sync;

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = SECURITY_CMD_ROTATE_KEY;
    return security_main_enqueue_sync(&cmd, &sync);
}

bool security_main_cmd_set_fh(bool enabled)
{
    security_cmd_t cmd;
    security_cmd_sync_t sync;

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = SECURITY_CMD_SET_FH;
    cmd.u.set_bool.enabled = enabled;
    return security_main_enqueue_sync(&cmd, &sync);
}

bool security_main_cmd_set_fh_period(uint32_t period_ms)
{
    security_cmd_t cmd;
    security_cmd_sync_t sync;

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = SECURITY_CMD_SET_FH_PERIOD;
    cmd.u.set_u32.value = period_ms;
    return security_main_enqueue_sync(&cmd, &sync);
}

bool security_main_cmd_set_notify_mode(security_notify_mode_t mode)
{
    security_cmd_t cmd;
    security_cmd_sync_t sync;

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = SECURITY_CMD_SET_NOTIFY;
    cmd.u.set_notify.mode = mode;
    return security_main_enqueue_sync(&cmd, &sync);
}

bool security_main_cmd_set_lora_preset(uint8_t preset_id)
{
    security_cmd_t cmd;
    security_cmd_sync_t sync;

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = SECURITY_CMD_SET_PRESET;
    cmd.u.set_preset.preset_id = preset_id;
    return security_main_enqueue_sync(&cmd, &sync);
}

bool security_main_cmd_set_auto_ping(bool enabled)
{
    security_cmd_t cmd;
    security_cmd_sync_t sync;

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = SECURITY_CMD_SET_AUTO_PING;
    cmd.u.set_bool.enabled = enabled;
    return security_main_enqueue_sync(&cmd, &sync);
}

bool security_main_cmd_set_radio_runtime_cfg(const radio_main_runtime_cfg_t *cfg)
{
    security_cmd_t cmd;
    security_cmd_sync_t sync;

    if (cfg == NULL)
    {
        return false;
    }

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = SECURITY_CMD_SET_RADIO_RUNTIME;
    cmd.u.set_radio.cfg = *cfg;
    return security_main_enqueue_sync(&cmd, &sync);
}

bool security_main_cmd_get_runtime_cfg(security_runtime_cfg_t *cfg_out)
{
    security_cmd_t cmd;
    security_cmd_sync_t sync;
    bool ok;

    if (cfg_out == NULL)
    {
        return false;
    }

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = SECURITY_CMD_GET_RUNTIME;

    ok = security_main_enqueue_sync(&cmd, &sync);
    if (ok)
    {
        *cfg_out = sync.runtime_out;
    }
    return ok;
}

bool security_main_get_network_key(uint8_t key_out[16])
{
    bool ok = false;

    if (key_out == NULL)
    {
        return false;
    }
    if (s_security_mutex == NULL)
    {
        return false;
    }

    if (osMutexAcquire(s_security_mutex, 100U) == osOK)
    {
        memcpy(key_out, s_network_key, 16U);
        ok = s_security_initialized;
        (void)osMutexRelease(s_security_mutex);
    }

    return ok;
}

bool security_main_get_peer_link_key(uint32_t local_node_id, uint32_t peer_node_id, uint8_t key_out[16])
{
    bool ok = false;
    uint8_t code[SECURITY_CODE_MAX];
    uint8_t code_len = 0U;
    uint8_t i;

    if ((key_out == NULL) || (peer_node_id == 0U) || (s_security_mutex == NULL))
    {
        return false;
    }

    memset(code, 0, sizeof(code));
    if (osMutexAcquire(s_security_mutex, 100U) == osOK)
    {
        if (s_security_initialized)
        {
            for (i = 0U; i < SECURITY_TRUSTED_MAX; i++)
            {
                if (s_trusted[i].in_use && (s_trusted[i].node_id == peer_node_id))
                {
                    code_len = s_trusted[i].code_len;
                    if (code_len > SECURITY_CODE_MAX)
                    {
                        code_len = SECURITY_CODE_MAX;
                    }
                    if (code_len > 0U)
                    {
                        memcpy(code, s_trusted[i].code, code_len);
                    }
                    break;
                }
            }
        }
        (void)osMutexRelease(s_security_mutex);
    }

    if (code_len == 0U)
    {
        return false;
    }

    security_peer_link_key_derive(local_node_id, peer_node_id, code, code_len, key_out);
    ok = true;
    return ok;
}

bool security_main_log_message(int16_t rssi_dbm, const uint8_t *payload, uint8_t payload_len)
{
    security_cmd_t cmd;

    if ((payload == NULL) && (payload_len > 0U))
    {
        return false;
    }
    if (s_security_cmd_queue == NULL)
    {
        return false;
    }

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = SECURITY_CMD_LOG_MESSAGE;
    cmd.u.log_msg.rssi_dbm = rssi_dbm;
    cmd.u.log_msg.len = payload_len;
    if (cmd.u.log_msg.len > I2C_MEM_STORE_LOG_PAYLOAD_MAX)
    {
        cmd.u.log_msg.len = I2C_MEM_STORE_LOG_PAYLOAD_MAX;
    }
    if ((cmd.u.log_msg.len > 0U) && (payload != NULL))
    {
        memcpy(cmd.u.log_msg.payload, payload, cmd.u.log_msg.len);
    }

    return (osMessageQueuePut(s_security_cmd_queue, &cmd, 0U, 0U) == osOK);
}

bool security_main_get_tpm_ready(bool *ready_out)
{
    bool ok = false;

    if ((ready_out == NULL) || (s_security_mutex == NULL))
    {
        return false;
    }

    if (osMutexAcquire(s_security_mutex, 100U) == osOK)
    {
        *ready_out = s_tpm_ready;
        ok = true;
        (void)osMutexRelease(s_security_mutex);
    }

    return ok;
}

static void security_main_task_fn(void *argument)
{
    security_cmd_t cmd;

    (void)argument;
    memset(s_trusted, 0, sizeof(s_trusted));

    security_bootstrap_store();
    security_bootstrap_tpm();

    if (osMutexAcquire(s_security_mutex, 1000U) == osOK)
    {
        s_security_initialized = true;
        (void)osMutexRelease(s_security_mutex);
    }

    printf("SEC: task ready\r\n");

    for (;;)
    {
        if (osMessageQueueGet(s_security_cmd_queue, &cmd, NULL, osWaitForever) != osOK)
        {
            continue;
        }

        if (cmd.id == SECURITY_CMD_LOG_MESSAGE)
        {
            if (s_mem_ready)
            {
                (void)i2c_mem_store_append_message(&s_mem_store,
                                                   cmd.u.log_msg.rssi_dbm,
                                                   cmd.u.log_msg.payload,
                                                   cmd.u.log_msg.len);
            }
            continue;
        }

        if (cmd.sync != NULL)
        {
            cmd.sync->result = false;
            memset(&cmd.sync->trusted_out, 0, sizeof(cmd.sync->trusted_out));
            memset(&cmd.sync->runtime_out, 0, sizeof(cmd.sync->runtime_out));
        }

        if (osMutexAcquire(s_security_mutex, 1000U) == osOK)
        {
            switch (cmd.id)
            {
                case SECURITY_CMD_ADD_DEVICE:
                    if (s_security_initialized)
                    {
                        cmd.sync->result = security_add_device_internal(cmd.u.add_device.node_id,
                                                                        cmd.u.add_device.code,
                                                                        cmd.u.add_device.code_len);
                    }
                    break;

                case SECURITY_CMD_DELETE_DEVICE:
                    if (s_security_initialized)
                    {
                        cmd.sync->result = security_delete_device_internal(cmd.u.del_device.node_id);
                    }
                    break;

                case SECURITY_CMD_GET_DEVICE:
                    if (s_security_initialized)
                    {
                        cmd.sync->result = security_get_device_internal(cmd.u.get_device.index,
                                                                        &cmd.sync->trusted_out);
                    }
                    break;

                case SECURITY_CMD_SET_CODING:
                    s_runtime_cfg.coding_enabled = cmd.u.set_bool.enabled;
                    cmd.sync->result = security_commit_runtime_cfg_soft();
                    break;

                case SECURITY_CMD_SET_FH:
                    s_runtime_cfg.fh_enabled = cmd.u.set_bool.enabled;
                    cmd.sync->result = security_commit_runtime_cfg_soft();
                    break;

                case SECURITY_CMD_SET_FH_PERIOD:
                    s_runtime_cfg.fh_period_ms = cmd.u.set_u32.value;
                    cmd.sync->result = security_commit_runtime_cfg_soft();
                    break;

                case SECURITY_CMD_ROTATE_KEY:
                    cmd.sync->result = security_rotate_key_internal();
                    break;

                case SECURITY_CMD_SET_NOTIFY:
                    s_runtime_cfg.notify_mode = cmd.u.set_notify.mode;
                    cmd.sync->result = security_commit_runtime_cfg_soft();
                    break;

                case SECURITY_CMD_SET_PRESET:
                    s_runtime_cfg.lora_preset = cmd.u.set_preset.preset_id;
                    cmd.sync->result = security_commit_runtime_cfg_soft();
                    break;

                case SECURITY_CMD_SET_AUTO_PING:
                    s_runtime_cfg.auto_ping_enabled = cmd.u.set_bool.enabled;
                    cmd.sync->result = security_commit_runtime_cfg_soft();
                    break;

                case SECURITY_CMD_SET_RADIO_RUNTIME:
                    s_runtime_cfg.active_modulation = cmd.u.set_radio.cfg.active_modulation;
                    s_runtime_cfg.lora = cmd.u.set_radio.cfg.lora;
                    s_runtime_cfg.fsk = cmd.u.set_radio.cfg.fsk;
                    s_runtime_cfg.ook = cmd.u.set_radio.cfg.ook;
                    s_runtime_cfg.radio_profiles_persisted = true;
                    cmd.sync->result = security_commit_runtime_cfg_soft();
                    break;

                case SECURITY_CMD_GET_RUNTIME:
                    cmd.sync->runtime_out = s_runtime_cfg;
                    cmd.sync->result = true;
                    break;

                default:
                    break;
            }

            (void)osMutexRelease(s_security_mutex);
        }

        if (cmd.sync != NULL)
        {
            cmd.sync->done = true;
        }
    }
}

static bool security_main_enqueue_sync(const security_cmd_t *cmd, security_cmd_sync_t *sync)
{
    security_cmd_t local;

    if ((cmd == NULL) || (sync == NULL) || (s_security_cmd_queue == NULL))
    {
        return false;
    }

    memset(sync, 0, sizeof(*sync));
    local = *cmd;
    local.sync = sync;

    if (osMessageQueuePut(s_security_cmd_queue, &local, 0U, 100U) != osOK)
    {
        return false;
    }

    return security_main_wait_sync(sync, SECURITY_CMD_WAIT_MS);
}

static bool security_main_wait_sync(security_cmd_sync_t *sync, uint32_t timeout_ms)
{
    uint32_t start;

    if (sync == NULL)
    {
        return false;
    }

    start = HAL_GetTick();
    while (!sync->done)
    {
        if ((HAL_GetTick() - start) > timeout_ms)
        {
            return false;
        }
        osDelay(SECURITY_CMD_POLL_MS);
    }

    return sync->result;
}

static uint16_t security_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFU;
    uint16_t i;
    uint8_t j;

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

static void security_key_seed_to_key(const uint8_t seed[SECURITY_KEY_SEED_BYTES], uint8_t key_out[16])
{
    uint8_t i;

    for (i = 0U; i < 16U; i++)
    {
        uint8_t a = seed[i % SECURITY_KEY_SEED_BYTES];
        uint8_t b = (uint8_t)(0x5AU + (17U * i));
        key_out[i] = (uint8_t)(a ^ b);
    }
}

static void security_load_default_radio_profiles(security_runtime_cfg_t *cfg)
{
    if (cfg == NULL)
    {
        return;
    }

    radio_main_load_default_lora_preset(2U, &cfg->lora);
    radio_main_load_default_fsk_profile(&cfg->fsk);
    radio_main_load_default_ook_profile(&cfg->ook);
    cfg->fh_period_ms = 2000UL;
    cfg->active_modulation = RADIO_MAIN_MODULATION_LORA;
    cfg->radio_profiles_persisted = false;
}

static uint8_t security_pack_bits(uint8_t *buf, uint8_t bit_pos, uint32_t value, uint8_t width)
{
    uint8_t bit;

    for (bit = 0U; bit < width; bit++)
    {
        uint8_t dst_bit = (uint8_t)(bit_pos + bit);
        uint8_t dst_byte = (uint8_t)(dst_bit / 8U);
        uint8_t dst_mask = (uint8_t)(1U << (dst_bit % 8U));

        if (((value >> bit) & 0x01UL) != 0UL)
        {
            buf[dst_byte] |= dst_mask;
        }
        else
        {
            buf[dst_byte] &= (uint8_t)~dst_mask;
        }
    }

    return (uint8_t)(bit_pos + width);
}

static uint8_t security_unpack_bits(const uint8_t *buf, uint8_t bit_pos, uint8_t width, uint32_t *value_out)
{
    uint32_t value = 0UL;
    uint8_t bit;

    if (value_out == NULL)
    {
        return bit_pos;
    }

    for (bit = 0U; bit < width; bit++)
    {
        uint8_t src_bit = (uint8_t)(bit_pos + bit);
        uint8_t src_byte = (uint8_t)(src_bit / 8U);
        uint8_t src_mask = (uint8_t)(1U << (src_bit % 8U));

        if ((buf[src_byte] & src_mask) != 0U)
        {
            value |= (1UL << bit);
        }
    }

    *value_out = value;
    return (uint8_t)(bit_pos + width);
}

static uint8_t security_index_from_u32(uint32_t value, const uint32_t *table, uint8_t count, uint8_t fallback)
{
    uint8_t i;

    for (i = 0U; i < count; i++)
    {
        if (table[i] == value)
        {
            return i;
        }
    }

    return fallback;
}

static uint8_t security_index_from_u16(uint16_t value, const uint16_t *table, uint8_t count, uint8_t fallback)
{
    uint8_t i;

    for (i = 0U; i < count; i++)
    {
        if (table[i] == value)
        {
            return i;
        }
    }

    return fallback;
}

static uint8_t security_index_from_i8(int8_t value, const int8_t *table, uint8_t count, uint8_t fallback)
{
    uint8_t i;

    for (i = 0U; i < count; i++)
    {
        if (table[i] == value)
        {
            return i;
        }
    }

    return fallback;
}

static uint8_t security_index_from_u8(uint8_t value, const uint8_t *table, uint8_t count, uint8_t fallback)
{
    uint8_t i;

    for (i = 0U; i < count; i++)
    {
        if (table[i] == value)
        {
            return i;
        }
    }

    return fallback;
}

static uint8_t security_pack_modulation_fh(radio_main_modulation_t modulation, uint8_t fh_period_idx)
{
    return (uint8_t)((((uint8_t)modulation) & 0x03U) |
                     ((fh_period_idx & 0x03U) << 2));
}

static radio_main_modulation_t security_unpack_modulation(uint8_t modulation_fh)
{
    uint8_t modulation = (uint8_t)(modulation_fh & 0x03U);

    if (modulation > (uint8_t)RADIO_MAIN_MODULATION_OOK)
    {
        modulation = (uint8_t)RADIO_MAIN_MODULATION_LORA;
    }

    return (radio_main_modulation_t)modulation;
}

static uint8_t security_unpack_fh_period_idx(uint8_t modulation_fh)
{
    return (uint8_t)((modulation_fh >> 2) & 0x03U);
}

static uint32_t security_u32_from_index(uint8_t idx, const uint32_t *table, uint8_t count, uint32_t fallback)
{
    return (idx < count) ? table[idx] : fallback;
}

static uint16_t security_u16_from_index(uint8_t idx, const uint16_t *table, uint8_t count, uint16_t fallback)
{
    return (idx < count) ? table[idx] : fallback;
}

static int8_t security_i8_from_index(uint8_t idx, const int8_t *table, uint8_t count, int8_t fallback)
{
    return (idx < count) ? table[idx] : fallback;
}

static uint8_t security_u8_from_index(uint8_t idx, const uint8_t *table, uint8_t count, uint8_t fallback)
{
    return (idx < count) ? table[idx] : fallback;
}

static void security_peer_link_key_derive(uint32_t local_node_id,
                                          uint32_t peer_node_id,
                                          const uint8_t *code,
                                          uint8_t code_len,
                                          uint8_t key_out[16])
{
    uint32_t lo;
    uint32_t hi;
    /**
     * @brief FNV-1a hash algorithm initial offset basis constant
     * @details This is the standard 32-bit FNV offset basis value used as the
     *          starting hash value in the FNV-1a (Fowler-Noll-Vo) hash function.
     *          The value 2166136261 (0x811c9dc5) is the recommended prime for
     *          32-bit FNV hashing.
     */
    uint32_t h = 2166136261UL;
    uint8_t i;

    if ((key_out == NULL) || (code == NULL) || (code_len == 0U))
    {
        return;
    }

    lo = (local_node_id < peer_node_id) ? local_node_id : peer_node_id;
    hi = (local_node_id < peer_node_id) ? peer_node_id : local_node_id;

#define SECURITY_FNV_MIX(_v)             \
    do                                   \
    {                                    \
        h ^= (uint8_t)(_v);              \
        h *= 16777619UL;                 \
    } while (0)

    SECURITY_FNV_MIX('B');
    SECURITY_FNV_MIX('K');
    SECURITY_FNV_MIX('L');
    SECURITY_FNV_MIX('1');

    for (i = 0U; i < 4U; i++)
    {
        SECURITY_FNV_MIX((lo >> (24U - (8U * i))) & 0xFFU);
    }
    for (i = 0U; i < 4U; i++)
    {
        SECURITY_FNV_MIX((hi >> (24U - (8U * i))) & 0xFFU);
    }
    SECURITY_FNV_MIX(code_len);
    for (i = 0U; i < code_len; i++)
    {
        SECURITY_FNV_MIX(code[i]);
    }

    for (i = 0U; i < 16U; i++)
    {
        h ^= (h << 13);
        h ^= (h >> 17);
        h ^= (h << 5);
        h += (uint32_t)code[i % code_len] + ((uint32_t)i * 41UL);
        key_out[i] = (uint8_t)((h >> ((i % 3U) * 8U)) & 0xFFU);
    }

#undef SECURITY_FNV_MIX
}

static bool security_load_runtime_and_seed_from_store(void)
{
    uint8_t buf[sizeof(security_store_wire_t)];
    uint8_t len = 0U;
    security_store_wire_t w;
    i2c_mem_store_status_t rc;

    if (!s_mem_ready)
    {
        return false;
    }

    rc = i2c_mem_store_secret_read(&s_mem_store, SECURITY_STORE_SLOT, buf, sizeof(buf), &len);
    if ((rc != I2C_MEM_STORE_OK) ||
        ((len != 13U) && (len != 14U) && (len != sizeof(w))))
    {
        return false;
    }

    memset(&w, 0, sizeof(w));
    memcpy(&w, buf, len);
    if ((w.magic != SECURITY_STORE_MAGIC) ||
        ((w.version != 1U) && (w.version != 2U) && (w.version != SECURITY_STORE_VERSION)))
    {
        return false;
    }

    s_runtime_cfg.coding_enabled = ((w.flags & 0x01U) != 0U);
    s_runtime_cfg.fh_enabled = ((w.flags & 0x02U) != 0U);
    s_runtime_cfg.auto_ping_enabled = ((w.flags & 0x04U) != 0U);
    s_runtime_cfg.notify_mode = (w.notify_mode == (uint8_t)SECURITY_NOTIFY_BADGE) ?
                                SECURITY_NOTIFY_BADGE : SECURITY_NOTIFY_POPUP;
    s_runtime_cfg.lora_preset = w.lora_preset;
    s_runtime_cfg.active_modulation = (w.version >= SECURITY_STORE_VERSION) ?
                                      security_unpack_modulation(w.modulation_fh) :
                                      ((w.version >= 2U) ?
                                       (radio_main_modulation_t)w.modulation_fh :
                                       RADIO_MAIN_MODULATION_LORA);
    s_runtime_cfg.fh_period_ms = (w.version >= SECURITY_STORE_VERSION) ?
                                 security_u32_from_index(security_unpack_fh_period_idx(w.modulation_fh),
                                                         s_fh_period_options_ms,
                                                         (uint8_t)(sizeof(s_fh_period_options_ms) /
                                                                   sizeof(s_fh_period_options_ms[0])),
                                                         2000UL) :
                                 2000UL;
    s_runtime_cfg.radio_profiles_persisted = false;
    memcpy(s_key_seed_cached, w.seed, SECURITY_KEY_SEED_BYTES);

    if (w.version >= 2U)
    {
        s_runtime_cfg.radio_profiles_persisted = security_load_radio_profiles_from_store();
    }

    return true;
}

static bool security_save_runtime_and_seed_to_store(void)
{
    security_store_wire_t w;

    if (!s_mem_ready)
    {
        return true;
    }

    memset(&w, 0, sizeof(w));
    w.magic = SECURITY_STORE_MAGIC;
    w.version = SECURITY_STORE_VERSION;
    w.flags = 0U;
    if (s_runtime_cfg.coding_enabled)
    {
        w.flags |= 0x01U;
    }
    if (s_runtime_cfg.fh_enabled)
    {
        w.flags |= 0x02U;
    }
    if (s_runtime_cfg.auto_ping_enabled)
    {
        w.flags |= 0x04U;
    }
    w.notify_mode = (uint8_t)s_runtime_cfg.notify_mode;
    w.lora_preset = s_runtime_cfg.lora_preset;
    w.modulation_fh = security_pack_modulation_fh(s_runtime_cfg.active_modulation,
                                                  security_index_from_u32(s_runtime_cfg.fh_period_ms,
                                                                          s_fh_period_options_ms,
                                                                          (uint8_t)(sizeof(s_fh_period_options_ms) /
                                                                                    sizeof(s_fh_period_options_ms[0])),
                                                                          1U));
    memcpy(w.seed, s_key_seed_cached, SECURITY_KEY_SEED_BYTES);

    if (i2c_mem_store_secret_write(&s_mem_store,
                                   SECURITY_STORE_SLOT,
                                   (const uint8_t *)&w,
                                   sizeof(w)) != I2C_MEM_STORE_OK)
    {
        return false;
    }

    if (!security_save_radio_profiles_to_store())
    {
        return false;
    }

    return true;
}

static bool security_commit_runtime_cfg_soft(void)
{
    if (!security_save_runtime_and_seed_to_store())
    {
        printf("SEC: runtime cfg applied but not persisted\r\n");
    }

    return true;
}

static bool security_save_radio_profiles_to_store(void)
{
    security_radio_store_wire_t w;
    uint8_t bit_pos = 0U;

    memset(&w, 0, sizeof(w));
    w.magic = SECURITY_STORE_MAGIC;
    w.version = SECURITY_STORE_VERSION;

    bit_pos = security_pack_bits(w.packed, bit_pos,
                                 security_index_from_u32(s_runtime_cfg.lora.frequency_hz,
                                                         s_freq_options_hz,
                                                         (uint8_t)(sizeof(s_freq_options_hz) / sizeof(s_freq_options_hz[0])),
                                                         1U), 3U);
    bit_pos = security_pack_bits(w.packed, bit_pos, (uint32_t)s_runtime_cfg.lora.bandwidth, 4U);
    bit_pos = security_pack_bits(w.packed, bit_pos, (uint32_t)(s_runtime_cfg.lora.spreading_factor - 6U), 3U);
    bit_pos = security_pack_bits(w.packed, bit_pos, (uint32_t)(s_runtime_cfg.lora.coding_rate - 5U), 2U);
    bit_pos = security_pack_bits(w.packed, bit_pos,
                                 security_index_from_i8(s_runtime_cfg.lora.tx_power_dbm,
                                                        s_power_options_dbm,
                                                        (uint8_t)(sizeof(s_power_options_dbm) / sizeof(s_power_options_dbm[0])),
                                                        4U), 3U);
    bit_pos = security_pack_bits(w.packed, bit_pos, s_runtime_cfg.lora.crc_on ? 1UL : 0UL, 1U);
    bit_pos = security_pack_bits(w.packed, bit_pos,
                                 security_index_from_u16(s_runtime_cfg.lora.preamble_len,
                                                         &s_preamble_options[1],
                                                         6U, 1U), 3U);
    bit_pos = security_pack_bits(w.packed, bit_pos, s_runtime_cfg.lora.implicit_header ? 1UL : 0UL, 1U);
    bit_pos = security_pack_bits(w.packed, bit_pos, s_runtime_cfg.lora.invert_iq ? 1UL : 0UL, 1U);
    bit_pos = security_pack_bits(w.packed, bit_pos,
                                 security_index_from_u32(s_runtime_cfg.lora.sync_word,
                                                         s_sync_word_options,
                                                         4U, 1U), 2U);

    bit_pos = security_pack_bits(w.packed, bit_pos, (uint32_t)s_runtime_cfg.fsk.shaping, 2U);
    bit_pos = security_pack_bits(w.packed, bit_pos,
                                 security_index_from_u32(s_runtime_cfg.fsk.frequency_hz,
                                                         s_freq_options_hz,
                                                         (uint8_t)(sizeof(s_freq_options_hz) / sizeof(s_freq_options_hz[0])),
                                                         2U), 3U);
    bit_pos = security_pack_bits(w.packed, bit_pos,
                                 security_index_from_u32(s_runtime_cfg.fsk.bitrate_bps,
                                                         s_bitrate_options_bps,
                                                         (uint8_t)(sizeof(s_bitrate_options_bps) / sizeof(s_bitrate_options_bps[0])),
                                                         2U), 3U);
    bit_pos = security_pack_bits(w.packed, bit_pos, (uint32_t)s_runtime_cfg.fsk.rx_bandwidth - 4UL, 3U);
    bit_pos = security_pack_bits(w.packed, bit_pos, (uint32_t)s_runtime_cfg.fsk.filter, 3U);
    bit_pos = security_pack_bits(w.packed, bit_pos,
                                 security_index_from_i8(s_runtime_cfg.fsk.tx_power_dbm,
                                                        s_power_options_dbm,
                                                        (uint8_t)(sizeof(s_power_options_dbm) / sizeof(s_power_options_dbm[0])),
                                                        4U), 3U);
    bit_pos = security_pack_bits(w.packed, bit_pos,
                                 security_index_from_u16(s_runtime_cfg.fsk.preamble_len,
                                                         s_preamble_options,
                                                         (uint8_t)(sizeof(s_preamble_options) / sizeof(s_preamble_options[0])),
                                                         2U), 3U);
    bit_pos = security_pack_bits(w.packed, bit_pos,
                                 security_index_from_u8(s_runtime_cfg.fsk.sync_word_len,
                                                        s_sync_len_options,
                                                        5U, 2U), 3U);
    bit_pos = security_pack_bits(w.packed, bit_pos,
                                 security_index_from_u32((uint32_t)s_runtime_cfg.fsk.sync_word,
                                                         &s_sync_word_options[4],
                                                         5U, 1U), 3U);
    bit_pos = security_pack_bits(w.packed, bit_pos, (uint32_t)s_runtime_cfg.fsk.address_filter, 2U);
    bit_pos = security_pack_bits(w.packed, bit_pos,
                                 (s_runtime_cfg.fsk.crc_type == RADIO_MAIN_CRC_IBM) ? 1UL :
                                 ((s_runtime_cfg.fsk.crc_type == RADIO_MAIN_CRC_CCITT) ? 2UL : 0UL), 2U);
    bit_pos = security_pack_bits(w.packed, bit_pos, s_runtime_cfg.fsk.data_whitening ? 1UL : 0UL, 1U);

    bit_pos = security_pack_bits(w.packed, bit_pos,
                                 security_index_from_u32(s_runtime_cfg.ook.frequency_hz,
                                                         s_freq_options_hz,
                                                         (uint8_t)(sizeof(s_freq_options_hz) / sizeof(s_freq_options_hz[0])),
                                                         3U), 3U);
    bit_pos = security_pack_bits(w.packed, bit_pos,
                                 security_index_from_u32(s_runtime_cfg.ook.bitrate_bps,
                                                         s_bitrate_options_bps,
                                                         (uint8_t)(sizeof(s_bitrate_options_bps) / sizeof(s_bitrate_options_bps[0])),
                                                         2U), 3U);
    bit_pos = security_pack_bits(w.packed, bit_pos,
                                 security_index_from_i8(s_runtime_cfg.ook.tx_power_dbm,
                                                        s_power_options_dbm,
                                                        (uint8_t)(sizeof(s_power_options_dbm) / sizeof(s_power_options_dbm[0])),
                                                        2U), 3U);
    bit_pos = security_pack_bits(w.packed, bit_pos, (uint32_t)s_runtime_cfg.ook.rx_bandwidth - 4UL, 3U);
    bit_pos = security_pack_bits(w.packed, bit_pos,
                                 security_index_from_u16(s_runtime_cfg.ook.preamble_len,
                                                         s_preamble_options,
                                                         (uint8_t)(sizeof(s_preamble_options) / sizeof(s_preamble_options[0])),
                                                         2U), 3U);
    bit_pos = security_pack_bits(w.packed, bit_pos,
                                 security_index_from_u8(s_runtime_cfg.ook.sync_word_len,
                                                        s_sync_len_options,
                                                        5U, 2U), 3U);
    bit_pos = security_pack_bits(w.packed, bit_pos,
                                 security_index_from_u32(s_runtime_cfg.ook.sync_word,
                                                         &s_sync_word_options[4],
                                                         5U, 1U), 3U);
    bit_pos = security_pack_bits(w.packed, bit_pos, (uint32_t)s_runtime_cfg.ook.threshold, 2U);
    (void)security_pack_bits(w.packed, bit_pos,
                             security_index_from_u8(s_runtime_cfg.ook.threshold_value,
                                                    s_ook_threshold_options,
                                                    8U, 2U), 3U);

    return (i2c_mem_store_secret_write(&s_mem_store,
                                       SECURITY_STORE_RADIO_SLOT,
                                       (const uint8_t *)&w,
                                       sizeof(w)) == I2C_MEM_STORE_OK);
}

static bool security_load_radio_profiles_from_store(void)
{
    uint8_t buf[sizeof(security_radio_store_wire_t)];
    uint8_t len = 0U;
    security_radio_store_wire_t w;
    uint8_t bit_pos = 0U;
    uint32_t value = 0UL;

    if (!s_mem_ready)
    {
        return false;
    }

    if (i2c_mem_store_secret_read(&s_mem_store,
                                  SECURITY_STORE_RADIO_SLOT,
                                  buf,
                                  sizeof(buf),
                                  &len) != I2C_MEM_STORE_OK)
    {
        return false;
    }
    if (len != sizeof(w))
    {
        return false;
    }

    memcpy(&w, buf, sizeof(w));
    if ((w.magic != SECURITY_STORE_MAGIC) || (w.version != SECURITY_STORE_VERSION))
    {
        return false;
    }

    bit_pos = security_unpack_bits(w.packed, bit_pos, 3U, &value);
    s_runtime_cfg.lora.frequency_hz = security_u32_from_index((uint8_t)value, s_freq_options_hz, 7U, 868500000UL);
    bit_pos = security_unpack_bits(w.packed, bit_pos, 4U, &value);
    s_runtime_cfg.lora.bandwidth = (radio_lora_bw_t)value;
    bit_pos = security_unpack_bits(w.packed, bit_pos, 3U, &value);
    s_runtime_cfg.lora.spreading_factor = (uint8_t)(value + 6U);
    bit_pos = security_unpack_bits(w.packed, bit_pos, 2U, &value);
    s_runtime_cfg.lora.coding_rate = (uint8_t)(value + 5U);
    bit_pos = security_unpack_bits(w.packed, bit_pos, 3U, &value);
    s_runtime_cfg.lora.tx_power_dbm = security_i8_from_index((uint8_t)value, s_power_options_dbm, 7U, 17);
    bit_pos = security_unpack_bits(w.packed, bit_pos, 1U, &value);
    s_runtime_cfg.lora.crc_on = (value != 0UL);
    bit_pos = security_unpack_bits(w.packed, bit_pos, 3U, &value);
    s_runtime_cfg.lora.preamble_len = security_u16_from_index((uint8_t)value, &s_preamble_options[1], 6U, 8U);
    bit_pos = security_unpack_bits(w.packed, bit_pos, 1U, &value);
    s_runtime_cfg.lora.implicit_header = (value != 0UL);
    s_runtime_cfg.lora.payload_len = s_runtime_cfg.lora.implicit_header ? BEKO_NET_MAX_PAYLOAD : 0U;
    bit_pos = security_unpack_bits(w.packed, bit_pos, 1U, &value);
    s_runtime_cfg.lora.invert_iq = (value != 0UL);
    bit_pos = security_unpack_bits(w.packed, bit_pos, 2U, &value);
    s_runtime_cfg.lora.sync_word = (uint8_t)security_u32_from_index((uint8_t)value, s_sync_word_options, 4U, 0x34U);

    bit_pos = security_unpack_bits(w.packed, bit_pos, 2U, &value);
    s_runtime_cfg.fsk.shaping = (radio_main_fsk_shaping_t)value;
    bit_pos = security_unpack_bits(w.packed, bit_pos, 3U, &value);
    s_runtime_cfg.fsk.frequency_hz = security_u32_from_index((uint8_t)value, s_freq_options_hz, 7U, 868300000UL);
    bit_pos = security_unpack_bits(w.packed, bit_pos, 3U, &value);
    s_runtime_cfg.fsk.bitrate_bps = security_u32_from_index((uint8_t)value, s_bitrate_options_bps, 8U, 4800UL);
    bit_pos = security_unpack_bits(w.packed, bit_pos, 3U, &value);
    s_runtime_cfg.fsk.rx_bandwidth = (radio_lora_bw_t)(value + 4U);
    bit_pos = security_unpack_bits(w.packed, bit_pos, 3U, &value);
    s_runtime_cfg.fsk.filter = (radio_main_filter_t)value;
    bit_pos = security_unpack_bits(w.packed, bit_pos, 3U, &value);
    s_runtime_cfg.fsk.tx_power_dbm = security_i8_from_index((uint8_t)value, s_power_options_dbm, 7U, 14);
    bit_pos = security_unpack_bits(w.packed, bit_pos, 3U, &value);
    s_runtime_cfg.fsk.preamble_len = security_u16_from_index((uint8_t)value, s_preamble_options, 7U, 8U);
    bit_pos = security_unpack_bits(w.packed, bit_pos, 3U, &value);
    s_runtime_cfg.fsk.sync_word_len = security_u8_from_index((uint8_t)value, s_sync_len_options, 5U, 2U);
    bit_pos = security_unpack_bits(w.packed, bit_pos, 3U, &value);
    s_runtime_cfg.fsk.sync_word = security_u32_from_index((uint8_t)value, &s_sync_word_options[4], 5U, 0x2DD4UL);
    bit_pos = security_unpack_bits(w.packed, bit_pos, 2U, &value);
    s_runtime_cfg.fsk.address_filter = (radio_main_address_filter_t)value;
    bit_pos = security_unpack_bits(w.packed, bit_pos, 2U, &value);
    s_runtime_cfg.fsk.crc_type = (value == 1UL) ? RADIO_MAIN_CRC_IBM :
                                 ((value == 2UL) ? RADIO_MAIN_CRC_CCITT : RADIO_MAIN_CRC_OFF);
    bit_pos = security_unpack_bits(w.packed, bit_pos, 1U, &value);
    s_runtime_cfg.fsk.data_whitening = (value != 0UL);

    bit_pos = security_unpack_bits(w.packed, bit_pos, 3U, &value);
    s_runtime_cfg.ook.frequency_hz = security_u32_from_index((uint8_t)value, s_freq_options_hz, 7U, 868500000UL);
    bit_pos = security_unpack_bits(w.packed, bit_pos, 3U, &value);
    s_runtime_cfg.ook.bitrate_bps = security_u32_from_index((uint8_t)value, s_bitrate_options_bps, 8U, 4800UL);
    bit_pos = security_unpack_bits(w.packed, bit_pos, 3U, &value);
    s_runtime_cfg.ook.tx_power_dbm = security_i8_from_index((uint8_t)value, s_power_options_dbm, 7U, 10);
    bit_pos = security_unpack_bits(w.packed, bit_pos, 3U, &value);
    s_runtime_cfg.ook.rx_bandwidth = (radio_lora_bw_t)(value + 4U);
    bit_pos = security_unpack_bits(w.packed, bit_pos, 3U, &value);
    s_runtime_cfg.ook.preamble_len = security_u16_from_index((uint8_t)value, s_preamble_options, 7U, 8U);
    bit_pos = security_unpack_bits(w.packed, bit_pos, 3U, &value);
    s_runtime_cfg.ook.sync_word_len = security_u8_from_index((uint8_t)value, s_sync_len_options, 5U, 2U);
    bit_pos = security_unpack_bits(w.packed, bit_pos, 3U, &value);
    s_runtime_cfg.ook.sync_word = security_u32_from_index((uint8_t)value, &s_sync_word_options[4], 5U, 0x2DD4UL);
    bit_pos = security_unpack_bits(w.packed, bit_pos, 2U, &value);
    s_runtime_cfg.ook.threshold = (radio_main_ook_threshold_t)value;
    bit_pos = security_unpack_bits(w.packed, bit_pos, 3U, &value);
    s_runtime_cfg.ook.threshold_value = security_u8_from_index((uint8_t)value, s_ook_threshold_options, 8U, 12U);

    return true;
}

static bool security_load_settings_legacy_from_store(void)
{
    uint8_t buf[sizeof(security_settings_wire_t)];
    uint8_t len = 0U;
    security_settings_wire_t w;
    i2c_mem_store_status_t rc;

    if (!s_mem_ready)
    {
        return false;
    }

    rc = i2c_mem_store_secret_read(&s_mem_store, SECURITY_LEGACY_SETTINGS_SLOT, buf, sizeof(buf), &len);
    if ((rc != I2C_MEM_STORE_OK) || (len != sizeof(w)))
    {
        return false;
    }

    memcpy(&w, buf, sizeof(w));
    if ((w.magic != SECURITY_SETTINGS_MAGIC) || (w.version != SECURITY_SETTINGS_VERSION))
    {
        return false;
    }
    if (w.crc != security_crc16((const uint8_t *)&w, (uint16_t)(sizeof(w) - sizeof(w.crc))))
    {
        return false;
    }

    s_runtime_cfg.coding_enabled = ((w.flags & 0x01U) != 0U);
    s_runtime_cfg.fh_enabled = ((w.flags & 0x02U) != 0U);
    s_runtime_cfg.auto_ping_enabled = ((w.flags & 0x04U) != 0U);
    s_runtime_cfg.notify_mode = (w.notify_mode == (uint8_t)SECURITY_NOTIFY_BADGE) ?
                                SECURITY_NOTIFY_BADGE : SECURITY_NOTIFY_POPUP;
    s_runtime_cfg.lora_preset = w.lora_preset;
    s_runtime_cfg.fh_period_ms = 2000UL;
    return true;
}

static bool security_load_key_seed_legacy_from_store(uint8_t seed[SECURITY_KEY_SEED_BYTES])
{
    uint8_t buf[sizeof(security_key_wire_t)];
    uint8_t len = 0U;
    security_key_wire_t w;
    i2c_mem_store_status_t rc;

    if ((!s_mem_ready) || (seed == NULL))
    {
        return false;
    }

    rc = i2c_mem_store_secret_read(&s_mem_store, SECURITY_LEGACY_KEY_SEED_SLOT, buf, sizeof(buf), &len);
    if ((rc != I2C_MEM_STORE_OK) || (len != sizeof(w)))
    {
        return false;
    }

    memcpy(&w, buf, sizeof(w));
    if (w.magic != SECURITY_KEY_MAGIC)
    {
        return false;
    }
    if (w.crc != security_crc16((const uint8_t *)&w, (uint16_t)(sizeof(w) - sizeof(w.crc))))
    {
        return false;
    }

    memcpy(seed, w.seed, SECURITY_KEY_SEED_BYTES);
    return true;
}

static void security_migrate_trusted_slot_v1_to_v2(void)
{
    i2c_mem_store_trusted_device_t trusted;

    if (!s_mem_ready)
    {
        return;
    }

    if (i2c_mem_store_trusted_device_read(&s_mem_store, 1U, &trusted) != I2C_MEM_STORE_OK)
    {
        return;
    }

    if (i2c_mem_store_trusted_device_write(&s_mem_store, SECURITY_TRUSTED_SLOT_BASE, &trusted) == I2C_MEM_STORE_OK)
    {
        (void)i2c_mem_store_secret_erase(&s_mem_store, 1U);
    }
}

static bool security_rotate_key_internal(void)
{
    uint8_t seed[SECURITY_KEY_SEED_BYTES];
    uint16_t out_len = 0U;
    uint32_t tpm_rc = 0UL;
    st33ktpm2x_status_t tpm_st;
    uint8_t i;

    memset(seed, 0, sizeof(seed));

    if (s_tpm_ready)
    {
        tpm_st = st33ktpm2x_tpm2_get_random(&s_tpm,
                                            SECURITY_KEY_SEED_BYTES,
                                            seed,
                                            sizeof(seed),
                                            &out_len,
                                            &tpm_rc);
        if ((tpm_st != ST33KTPM2X_OK) || (out_len < SECURITY_KEY_SEED_BYTES))
        {
            s_tpm_ready = false;
        }
    }

    if (!s_tpm_ready)
    {
        uint32_t tick = HAL_GetTick();
        for (i = 0U; i < SECURITY_KEY_SEED_BYTES; i++)
        {
            seed[i] = (uint8_t)((tick >> ((i % 4U) * 8U)) ^ (uint32_t)(0x37U + (i * 13U)));
        }
    }

    memcpy(s_key_seed_cached, seed, SECURITY_KEY_SEED_BYTES);
    security_key_seed_to_key(seed, s_network_key);
    if (s_mem_ready)
    {
        (void)security_save_runtime_and_seed_to_store();
    }

    return true;
}

static uint8_t security_trusted_store_capacity(void)
{
    uint16_t available;

    if (!s_mem_ready)
    {
        return 0U;
    }
    if (s_mem_store.secret_slot_count <= SECURITY_TRUSTED_SLOT_BASE)
    {
        return 0U;
    }

    available = (uint16_t)(s_mem_store.secret_slot_count - SECURITY_TRUSTED_SLOT_BASE);
    if (available > SECURITY_TRUSTED_MAX)
    {
        available = SECURITY_TRUSTED_MAX;
    }

    return (uint8_t)available;
}

static uint16_t security_trusted_store_slot(uint8_t idx)
{
    return (uint16_t)(SECURITY_TRUSTED_SLOT_BASE + idx);
}

static void security_node_id_to_bytes(uint32_t node_id, uint8_t out[SECURITY_TRUSTED_ID_LEN])
{
    if (out == NULL)
    {
        return;
    }

    out[0] = (uint8_t)((node_id >> 24) & 0xFFU);
    out[1] = (uint8_t)((node_id >> 16) & 0xFFU);
    out[2] = (uint8_t)((node_id >> 8) & 0xFFU);
    out[3] = (uint8_t)(node_id & 0xFFU);
}

static uint32_t security_node_id_from_bytes(const uint8_t in[SECURITY_TRUSTED_ID_LEN])
{
    if (in == NULL)
    {
        return 0U;
    }

    return ((uint32_t)in[0] << 24) |
           ((uint32_t)in[1] << 16) |
           ((uint32_t)in[2] << 8) |
           (uint32_t)in[3];
}

static bool security_store_trusted_slot(uint8_t idx)
{
    i2c_mem_store_trusted_device_t rec;
    i2c_mem_store_status_t rc;
    uint8_t capacity = security_trusted_store_capacity();

    if (!s_mem_ready)
    {
        return true;
    }
    if (idx >= capacity)
    {
        return true;
    }
    if (!s_trusted[idx].in_use)
    {
        return security_erase_trusted_slot(idx);
    }

    memset(&rec, 0, sizeof(rec));
    rec.id_len = SECURITY_TRUSTED_ID_LEN;
    rec.code_len = s_trusted[idx].code_len;
    if (rec.code_len > I2C_MEM_STORE_TRUSTED_CODE_MAX)
    {
        rec.code_len = I2C_MEM_STORE_TRUSTED_CODE_MAX;
    }

    security_node_id_to_bytes(s_trusted[idx].node_id, rec.id);
    if (rec.code_len > 0U)
    {
        memcpy(rec.code, s_trusted[idx].code, rec.code_len);
    }

    rc = i2c_mem_store_trusted_device_write(&s_mem_store, security_trusted_store_slot(idx), &rec);
    return (rc == I2C_MEM_STORE_OK);
}

static bool security_erase_trusted_slot(uint8_t idx)
{
    uint8_t capacity = security_trusted_store_capacity();
    i2c_mem_store_status_t rc;

    if (!s_mem_ready)
    {
        return true;
    }
    if (idx >= capacity)
    {
        return true;
    }

    rc = i2c_mem_store_secret_erase(&s_mem_store, security_trusted_store_slot(idx));
    return (rc == I2C_MEM_STORE_OK);
}

static void security_load_trusted_from_store(void)
{
    uint8_t idx;
    uint8_t capacity = security_trusted_store_capacity();

    if (!s_mem_ready)
    {
        return;
    }

    for (idx = 0U; idx < capacity; idx++)
    {
        i2c_mem_store_trusted_device_t rec;
        i2c_mem_store_status_t rc;
        uint32_t node_id;

        memset(&rec, 0, sizeof(rec));
        rc = i2c_mem_store_trusted_device_read(&s_mem_store, security_trusted_store_slot(idx), &rec);
        if (rc != I2C_MEM_STORE_OK)
        {
            continue;
        }
        if (rec.id_len != SECURITY_TRUSTED_ID_LEN)
        {
            continue;
        }

        node_id = security_node_id_from_bytes(rec.id);
        if (node_id == 0U)
        {
            continue;
        }

        s_trusted[idx].in_use = true;
        s_trusted[idx].node_id = node_id;
        s_trusted[idx].code_len = rec.code_len;
        if (s_trusted[idx].code_len > SECURITY_CODE_MAX)
        {
            s_trusted[idx].code_len = SECURITY_CODE_MAX;
        }
        if (s_trusted[idx].code_len > 0U)
        {
            memcpy(s_trusted[idx].code, rec.code, s_trusted[idx].code_len);
        }
    }

    printf("SEC: trusted slots persisted=%u\r\n", capacity);
}

static bool security_add_device_internal(uint32_t node_id, const uint8_t *code, uint8_t len)
{
    uint8_t i;
    uint8_t free_idx = 0xFFU;
    uint8_t max_slots = SECURITY_TRUSTED_MAX;

    if (node_id == 0U)
    {
        return false;
    }
    if (len > SECURITY_CODE_MAX)
    {
        len = SECURITY_CODE_MAX;
    }
    if (s_mem_ready)
    {
        max_slots = security_trusted_store_capacity();
        if (max_slots == 0U)
        {
            return false;
        }
    }

    for (i = 0U; i < max_slots; i++)
    {
        if (s_trusted[i].in_use && (s_trusted[i].node_id == node_id))
        {
            s_trusted[i].code_len = len;
            memset(s_trusted[i].code, 0, sizeof(s_trusted[i].code));
            if ((len > 0U) && (code != NULL))
            {
                memcpy(s_trusted[i].code, code, len);
            }
            if (!security_store_trusted_slot(i))
            {
                printf("SEC: trusted update persist failed idx=%u\r\n", i);
            }
            return true;
        }

        if ((!s_trusted[i].in_use) && (free_idx == 0xFFU))
        {
            free_idx = i;
        }
    }

    if (free_idx == 0xFFU)
    {
        return false;
    }

    s_trusted[free_idx].in_use = true;
    s_trusted[free_idx].node_id = node_id;
    s_trusted[free_idx].code_len = len;
    memset(s_trusted[free_idx].code, 0, sizeof(s_trusted[free_idx].code));
    if ((len > 0U) && (code != NULL))
    {
        memcpy(s_trusted[free_idx].code, code, len);
    }

    if (!security_store_trusted_slot(free_idx))
    {
        printf("SEC: trusted add persist failed idx=%u\r\n", free_idx);
    }
    return true;
}

static bool security_delete_device_internal(uint32_t node_id)
{
    uint8_t i;

    for (i = 0U; i < SECURITY_TRUSTED_MAX; i++)
    {
        if (s_trusted[i].in_use && (s_trusted[i].node_id == node_id))
        {
            memset(&s_trusted[i], 0, sizeof(s_trusted[i]));
            if (!security_erase_trusted_slot(i))
            {
                printf("SEC: trusted erase persist failed idx=%u\r\n", i);
            }
            return true;
        }
    }

    return false;
}

static bool security_get_device_internal(uint8_t idx, trusted_info_t *out)
{
    if ((idx >= SECURITY_TRUSTED_MAX) || (out == NULL))
    {
        return false;
    }

    memset(out, 0, sizeof(*out));
    if (!s_trusted[idx].in_use)
    {
        out->slot = idx;
        out->in_use = false;
        return true;
    }

    out->in_use = true;
    out->slot = idx;
    out->node_id = s_trusted[idx].node_id;
    out->code_len = s_trusted[idx].code_len;
    memcpy(out->code, s_trusted[idx].code, out->code_len);
    return true;
}

static void security_bootstrap_tpm(void)
{
    st33ktpm2x_cfg_t cfg;
    uint32_t tpm_rc = 0UL;
    uint32_t did_vid = 0UL;
    uint8_t rid = 0U;

    st33ktpm2x_default_cfg(&cfg, &hi2c3);
    if (st33ktpm2x_init(&s_tpm, &cfg) != ST33KTPM2X_OK)
    {
        printf("SEC: TPM init failed\r\n");
        s_tpm_ready = false;
        return;
    }

    (void)st33ktpm2x_hard_reset(&s_tpm);
    (void)st33ktpm2x_tpm2_startup(&s_tpm, ST33KTPM2X_TPM2_SU_CLEAR, &tpm_rc);
    (void)st33ktpm2x_tpm2_self_test(&s_tpm, false, &tpm_rc);

    if (st33ktpm2x_read_identity(&s_tpm, &did_vid, &rid) == ST33KTPM2X_OK)
    {
        s_tpm_ready = true;
        printf("SEC: TPM ready DIDVID=0x%08lX RID=0x%02X\r\n", (unsigned long)did_vid, rid);
    }
    else
    {
        s_tpm_ready = false;
        printf("SEC: TPM not ready\r\n");
    }
}

static void security_bootstrap_store(void)
{
    i2c_mem_store_cfg_t mem_cfg;
    bool loaded = false;
    bool loaded_v1 = false;

    i2c_mem_store_default_cfg_m24c01r(&mem_cfg, &hi2c1);
    /* M24C01-R has only 128 B, so prioritize settings + radio profiles + one trusted slot. */
    mem_cfg.secret_area_bytes = 72U;
    memset(s_key_seed_cached, 0, sizeof(s_key_seed_cached));
    security_load_default_radio_profiles(&s_runtime_cfg);

    if (i2c_mem_store_init(&s_mem_store, &mem_cfg, true) == I2C_MEM_STORE_OK)
    {
        s_mem_ready = true;
        printf("SEC: MEM store ready log_slots=%u secret_slots=%u\r\n",
               (unsigned int)s_mem_store.slot_count,
               (unsigned int)s_mem_store.secret_slot_count);
        if (s_mem_store.slot_count == 0U)
        {
            printf("SEC: MEM log disabled, storage reserved for trusted devices\r\n");
        }
    }
    else
    {
        s_mem_ready = false;
        printf("SEC: MEM store unavailable\r\n");
    }

    if (s_mem_ready)
    {
        loaded = security_load_runtime_and_seed_from_store();
        loaded_v1 = loaded && !s_runtime_cfg.radio_profiles_persisted;
        if (!loaded)
        {
            if (security_load_settings_legacy_from_store() &&
                security_load_key_seed_legacy_from_store(s_key_seed_cached))
            {
                loaded = true;
            }
        }
    }

    if (loaded)
    {
        security_key_seed_to_key(s_key_seed_cached, s_network_key);
    }
    else
    {
        (void)security_rotate_key_internal();
    }

    if (s_mem_ready)
    {
        if (loaded_v1)
        {
            security_migrate_trusted_slot_v1_to_v2();
        }
        (void)security_save_runtime_and_seed_to_store();
        security_load_trusted_from_store();
    }
}
