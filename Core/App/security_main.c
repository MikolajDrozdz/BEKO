#include "security_main.h"

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
#define SECURITY_TRUSTED_SLOT_BASE          1U
#define SECURITY_STORE_MAGIC                0xA5U
#define SECURITY_STORE_VERSION              1U
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
    SECURITY_CMD_ROTATE_KEY,
    SECURITY_CMD_SET_NOTIFY,
    SECURITY_CMD_SET_PRESET,
    SECURITY_CMD_SET_AUTO_PING,
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
            security_notify_mode_t mode;
        } set_notify;
        struct
        {
            uint8_t preset_id;
        } set_preset;
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
    uint8_t seed[SECURITY_KEY_SEED_BYTES];
} security_store_wire_t;

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
    .auto_ping_enabled = false,
    .notify_mode = SECURITY_NOTIFY_POPUP,
    .lora_preset = 0U
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
                    cmd.sync->result = security_save_runtime_and_seed_to_store();
                    break;

                case SECURITY_CMD_SET_FH:
                    s_runtime_cfg.fh_enabled = cmd.u.set_bool.enabled;
                    cmd.sync->result = security_save_runtime_and_seed_to_store();
                    break;

                case SECURITY_CMD_ROTATE_KEY:
                    cmd.sync->result = security_rotate_key_internal();
                    break;

                case SECURITY_CMD_SET_NOTIFY:
                    s_runtime_cfg.notify_mode = cmd.u.set_notify.mode;
                    cmd.sync->result = security_save_runtime_and_seed_to_store();
                    break;

                case SECURITY_CMD_SET_PRESET:
                    s_runtime_cfg.lora_preset = cmd.u.set_preset.preset_id;
                    cmd.sync->result = security_save_runtime_and_seed_to_store();
                    break;

                case SECURITY_CMD_SET_AUTO_PING:
                    s_runtime_cfg.auto_ping_enabled = cmd.u.set_bool.enabled;
                    cmd.sync->result = security_save_runtime_and_seed_to_store();
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
    if ((rc != I2C_MEM_STORE_OK) || (len != sizeof(w)))
    {
        return false;
    }

    memcpy(&w, buf, sizeof(w));
    if ((w.magic != SECURITY_STORE_MAGIC) || (w.version != SECURITY_STORE_VERSION))
    {
        return false;
    }

    s_runtime_cfg.coding_enabled = ((w.flags & 0x01U) != 0U);
    s_runtime_cfg.fh_enabled = ((w.flags & 0x02U) != 0U);
    s_runtime_cfg.auto_ping_enabled = ((w.flags & 0x04U) != 0U);
    s_runtime_cfg.notify_mode = (w.notify_mode == (uint8_t)SECURITY_NOTIFY_BADGE) ?
                                SECURITY_NOTIFY_BADGE : SECURITY_NOTIFY_POPUP;
    s_runtime_cfg.lora_preset = w.lora_preset;
    memcpy(s_key_seed_cached, w.seed, SECURITY_KEY_SEED_BYTES);
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
    memcpy(w.seed, s_key_seed_cached, SECURITY_KEY_SEED_BYTES);

    return (i2c_mem_store_secret_write(&s_mem_store,
                                       SECURITY_STORE_SLOT,
                                       (const uint8_t *)&w,
                                       sizeof(w)) == I2C_MEM_STORE_OK);
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

    if (node_id == 0U)
    {
        return false;
    }
    if (len > SECURITY_CODE_MAX)
    {
        len = SECURITY_CODE_MAX;
    }

    for (i = 0U; i < SECURITY_TRUSTED_MAX; i++)
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
        printf("SEC: TPM ready DIDVID=0x%08lX RID=0x%02X\r\n", did_vid, rid);
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

    i2c_mem_store_default_cfg_m24c01r(&mem_cfg, &hi2c1);
    mem_cfg.secret_area_bytes = 48U;
    memset(s_key_seed_cached, 0, sizeof(s_key_seed_cached));

    if (i2c_mem_store_init(&s_mem_store, &mem_cfg, true) == I2C_MEM_STORE_OK)
    {
        s_mem_ready = true;
        printf("SEC: MEM store ready\r\n");
    }
    else
    {
        s_mem_ready = false;
        printf("SEC: MEM store unavailable\r\n");
    }

    if (s_mem_ready)
    {
        loaded = security_load_runtime_and_seed_from_store();
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

    /* Keep secure defaults after restart. */
    s_runtime_cfg.coding_enabled = true;
    s_runtime_cfg.fh_enabled = false;
    s_runtime_cfg.auto_ping_enabled = false;
    s_runtime_cfg.lora_preset = 0U;

    if (s_mem_ready)
    {
        (void)security_save_runtime_and_seed_to_store();
        security_load_trusted_from_store();
    }
}
