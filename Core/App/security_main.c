#include "security_main.h"

#include "app_delay.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "i2c_mem_store_lib/i2c_mem_store.h"
#include "laviet_crypto.h"
#include "laviet_frame.h"
#include "main.h"
#include "st33ktpm2x_lib/st33ktpm2x.h"
#include "task.h"

#include <stdio.h>
#include <string.h>

#define SECURITY_TASK_STACK_SIZE            15360U
#define SECURITY_TASK_STACK_WORDS           (SECURITY_TASK_STACK_SIZE / sizeof(StackType_t))
#define SECURITY_CMD_QUEUE_DEPTH            16U
#define SECURITY_CMD_WAIT_MS                3000U
#define SECURITY_CMD_POLL_MS                5U
#define SECURITY_TRUSTED_MAX                16U
#define SECURITY_TRUSTED_SLOT_BASE          0U
#define SECURITY_GATEWAY_COUNTER_SLOT       2U
#define SECURITY_GATEWAY_COUNTER_MAGIC      0xC7U
#define SECURITY_GATEWAY_COUNTER_VERSION    1U
#define SECURITY_GATEWAY_COUNTER_STORE_LEN  10U
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
#define SECURITY_TPM_ROOT_NV_INDEX          0x01C10101UL
#define SECURITY_TPM_ROOT_LEGACY_NV_INDEX   0x01C10100UL
#define SECURITY_TPM_NV_PPWRITE             0x00000001UL
#define SECURITY_TPM_NV_OWNERWRITE          0x00000002UL
#define SECURITY_TPM_NV_AUTHWRITE           0x00000004UL
#define SECURITY_TPM_NV_OWNERREAD           0x00020000UL
#define SECURITY_TPM_NV_AUTHREAD            0x00040000UL
#define SECURITY_TPM_NV_NO_DA               0x02000000UL
#define SECURITY_TPM_ROOT_NV_ATTRS          (SECURITY_TPM_NV_PPWRITE | \
                                             SECURITY_TPM_NV_OWNERREAD | \
                                             SECURITY_TPM_NV_NO_DA)
#define SECURITY_TPM_REG_LOC_SEL            0x00U
#define SECURITY_TPM_REG_ACCESS             0x04U
#define SECURITY_TPM_REG_STS                0x18U
#define SECURITY_TPM_REG_DATA_FIFO          0x24U
#define SECURITY_TPM_REG_IF_CAP             0x30U
#define SECURITY_TPM_REG_DID_VID            0x48U
#define SECURITY_TPM_REG_RID                0x4CU
#define SECURITY_TPM_DIAG_TIMEOUT_MS        20U

#if defined(TPM_INIT_LOG)
#define SECURITY_TPM_INIT_LOG(...)          printf(__VA_ARGS__)
#else
#define SECURITY_TPM_INIT_LOG(...)          do { if (0) { printf(__VA_ARGS__); } } while (0)
#endif

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
            bool gateway_slot;
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
    bool is_master;
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
static bool s_runtime_shadow_valid = false;
static security_store_wire_t s_runtime_shadow_store;
static security_radio_store_wire_t s_runtime_shadow_radio;
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
static uint8_t s_network_key[16];
static bool s_key_seed_tpm_backed = false;
static bool s_key_seed_tpm_pp_backed = false;
static const uint8_t s_shared_frame_root_key[16] =
{
    (uint8_t)'L', (uint8_t)'A', (uint8_t)'V', (uint8_t)'I',
    (uint8_t)'E', (uint8_t)'T', (uint8_t)'_', (uint8_t)'S',
    (uint8_t)'H', (uint8_t)'A', (uint8_t)'R', (uint8_t)'E',
    (uint8_t)'D', (uint8_t)'_', (uint8_t)'V', (uint8_t)'1'
};
static uint8_t s_key_seed_cached[SECURITY_KEY_SEED_BYTES];
static uint32_t s_gateway_rx_counter = 0UL;
static uint32_t s_gateway_tx_counter = 0UL;

extern I2C_HandleTypeDef hi2c1;
extern I2C_HandleTypeDef hi2c3;
extern RNG_HandleTypeDef hrng;

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
static void security_log_stack_high_water(const char *tag);
static bool security_main_wait_sync(security_cmd_sync_t *sync, uint32_t timeout_ms);
static bool security_main_enqueue_sync(const security_cmd_t *cmd, security_cmd_sync_t *sync);

static void security_key_seed_to_key(const uint8_t seed[SECURITY_KEY_SEED_BYTES], uint8_t key_out[16]);
static void security_key_seed_to_store_key(const uint8_t seed[SECURITY_KEY_SEED_BYTES], uint8_t key_out[16]);
static bool security_seed_has_data(const uint8_t seed[SECURITY_KEY_SEED_BYTES]);
static void security_peer_link_key_derive(uint32_t local_node_id,
                                          uint32_t peer_node_id,
                                          const uint8_t *code,
                                          uint8_t code_len,
                                          uint8_t key_out[16]);
static bool security_load_runtime_and_seed_from_store(bool *seed_loaded_out);
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
static bool security_add_device_internal(uint32_t node_id, const uint8_t *code, uint8_t len, bool gateway_slot);
static bool security_delete_device_internal(uint32_t node_id);
static bool security_get_device_internal(uint8_t idx, trusted_info_t *out);
static bool security_lookup_peer_code(uint32_t peer_node_id,
                                      uint8_t code_out[SECURITY_CODE_MAX],
                                      uint8_t *code_len_out);
static uint8_t security_trusted_store_capacity(void);
static uint16_t security_trusted_store_slot(uint8_t idx);
static void security_node_id_to_bytes(uint32_t node_id, uint8_t out[SECURITY_TRUSTED_ID_LEN]);
static uint32_t security_node_id_from_bytes(const uint8_t in[SECURITY_TRUSTED_ID_LEN]);
static void security_be32_write(uint8_t *dst, uint32_t value);
static uint32_t security_be32_read(const uint8_t *src);
static bool security_store_trusted_slot(uint8_t idx);
static bool security_erase_trusted_slot(uint8_t idx);
static uint8_t security_load_trusted_from_store(void);
static bool security_migrate_secrets_from_default_store_key(const i2c_mem_store_cfg_t *tpm_cfg);
static bool security_load_gateway_counter_from_store(void);
static bool security_store_gateway_counter_to_store(uint32_t rx_counter, uint32_t tx_counter);
static bool security_tpm_load_root_seed(uint8_t seed[SECURITY_KEY_SEED_BYTES]);
static bool security_tpm_store_root_seed(const uint8_t seed[SECURITY_KEY_SEED_BYTES]);
static bool security_tpm_define_root_seed(void);
static bool security_get_entropy_bytes(uint8_t *out, uint8_t len);
static const char *security_tpm_status_text(st33ktpm2x_status_t rc);
static uint32_t security_le32_read(const uint8_t *src);
static void security_tpm_log_i2c_state(const char *tag, I2C_HandleTypeDef *hi2c);
static void security_tpm_log_lines(const char *tag, const st33ktpm2x_cfg_t *cfg);
static void security_tpm_scan_i2c3(const st33ktpm2x_cfg_t *cfg);
static void security_tpm_dump_reg(const st33ktpm2x_cfg_t *cfg,
                                  const char *name,
                                  uint16_t reg,
                                  uint16_t mem_addr_size,
                                  uint8_t len);
static void security_tpm_dump_raw_regs(const st33ktpm2x_cfg_t *cfg);
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
        memset(&s_security_task_cb, 0, sizeof(s_security_task_cb));
        memset(s_security_task_stack, 0, sizeof(s_security_task_stack));
        printf("SEC: task memory cb=%p stack=%p size=%lu\r\n",
               (void *)&s_security_task_cb,
               (void *)s_security_task_stack,
               (unsigned long)sizeof(s_security_task_stack));
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
    cmd.u.add_device.gateway_slot = false;
    if ((cmd.u.add_device.code_len > 0U) && (code != NULL))
    {
        memcpy(cmd.u.add_device.code, code, cmd.u.add_device.code_len);
    }

    return security_main_enqueue_sync(&cmd, &sync);
}

bool security_main_cmd_add_gateway(uint32_t node_id, const uint8_t *code, uint8_t len)
{
    security_cmd_t cmd;
    security_cmd_sync_t sync;

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = SECURITY_CMD_ADD_DEVICE;
    cmd.u.add_device.node_id = node_id;
    cmd.u.add_device.code_len = (len > SECURITY_CODE_MAX) ? SECURITY_CODE_MAX : len;
    cmd.u.add_device.gateway_slot = true;
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

static bool security_lookup_peer_code(uint32_t peer_node_id,
                                      uint8_t code_out[SECURITY_CODE_MAX],
                                      uint8_t *code_len_out)
{
    uint8_t i;

    if ((code_out == NULL) || (code_len_out == NULL) || (peer_node_id == 0U) || (s_security_mutex == NULL))
    {
        return false;
    }

    memset(code_out, 0, SECURITY_CODE_MAX);
    *code_len_out = 0U;
    if (osMutexAcquire(s_security_mutex, 100U) == osOK)
    {
        if (s_security_initialized)
        {
            for (i = 0U; i < SECURITY_TRUSTED_MAX; i++)
            {
                if (s_trusted[i].in_use && (s_trusted[i].node_id == peer_node_id))
                {
                    *code_len_out = s_trusted[i].code_len;
                    if (*code_len_out > SECURITY_CODE_MAX)
                    {
                        *code_len_out = SECURITY_CODE_MAX;
                    }
                    if (*code_len_out > 0U)
                    {
                        memcpy(code_out, s_trusted[i].code, *code_len_out);
                    }
                    break;
                }
            }
        }
        (void)osMutexRelease(s_security_mutex);
    }

    return (*code_len_out > 0U);
}

bool security_main_get_peer_link_key(uint32_t local_node_id, uint32_t peer_node_id, uint8_t key_out[16])
{
    bool ok = false;
    uint8_t code[SECURITY_CODE_MAX];
    uint8_t code_len = 0U;

    if ((key_out == NULL) || (peer_node_id == 0U))
    {
        return false;
    }

    if (!security_lookup_peer_code(peer_node_id, code, &code_len))
    {
        return false;
    }

    security_peer_link_key_derive(local_node_id, peer_node_id, code, code_len, key_out);
    laviet_secure_zero(code, sizeof(code));
    ok = true;
    return ok;
}

static bool security_get_frame_keys_mode_internal(uint16_t local_id,
                                                  uint16_t peer_id,
                                                  security_frame_key_mode_t mode,
                                                  const uint8_t *code_in,
                                                  uint8_t code_in_len,
                                                  bool use_explicit_code,
                                                  uint8_t enc_key_out[16],
                                                  uint8_t hmac_key_out[32])
{
    uint8_t base_key[16];
    uint8_t digest[32];
    uint8_t info[8];
    uint8_t code[SECURITY_CODE_MAX];
    uint8_t code_len = 0U;
    uint16_t domain_id;
    bool ok = false;

    if ((local_id == 0U) || (peer_id == 0U) || (enc_key_out == NULL) || (hmac_key_out == NULL))
    {
        return false;
    }

    memset(base_key, 0, sizeof(base_key));
    memset(code, 0, sizeof(code));
    switch (mode)
    {
        case SECURITY_FRAME_KEY_MODE_PAIR_V1_32:
            if (use_explicit_code)
            {
                if ((code_in == NULL) || (code_in_len == 0U))
                {
                    return false;
                }
                code_len = (code_in_len > SECURITY_CODE_MAX) ? SECURITY_CODE_MAX : code_in_len;
                memcpy(code, code_in, code_len);
            }
            else if (!security_lookup_peer_code(peer_id, code, &code_len))
            {
                return false;
            }
            security_peer_link_key_derive(local_id, peer_id, code, code_len, base_key);
            ok = true;
            break;

        case SECURITY_FRAME_KEY_MODE_SHARED:
        default:
            memcpy(base_key, s_shared_frame_root_key, sizeof(base_key));
            ok = true;
            break;
    }

    if (!ok)
    {
        laviet_secure_zero(code, sizeof(code));
        return false;
    }

    domain_id = (peer_id == LAVIET_BROADCAST_ID) ?
                LAVIET_BROADCAST_ID :
                ((local_id < peer_id) ? local_id : peer_id);
    info[0] = (uint8_t)'L';
    info[1] = (uint8_t)'V';
    info[2] = (uint8_t)'1';
    info[3] = (uint8_t)'K';
    info[4] = (uint8_t)(domain_id >> 8);
    info[5] = (uint8_t)domain_id;
    info[6] = 0U;
    info[7] = 1U;
    ok = laviet_hmac_sha256(base_key, sizeof(base_key), info, sizeof(info), digest);
    if (ok)
    {
        memcpy(enc_key_out, digest, 16U);
        info[7] = 2U;
        ok = laviet_hmac_sha256(base_key, sizeof(base_key), info, sizeof(info), hmac_key_out);
    }

    laviet_secure_zero(base_key, sizeof(base_key));
    laviet_secure_zero(digest, sizeof(digest));
    laviet_secure_zero(info, sizeof(info));
    laviet_secure_zero(code, sizeof(code));
    return ok;
}

bool security_main_get_frame_keys_mode(uint16_t local_id,
                                       uint16_t peer_id,
                                       security_frame_key_mode_t mode,
                                       uint8_t enc_key_out[16],
                                       uint8_t hmac_key_out[32])
{
    return security_get_frame_keys_mode_internal(local_id,
                                                 peer_id,
                                                 mode,
                                                 NULL,
                                                 0U,
                                                 false,
                                                 enc_key_out,
                                                 hmac_key_out);
}

bool security_main_get_frame_keys_for_code(uint16_t local_id,
                                           uint16_t peer_id,
                                           security_frame_key_mode_t mode,
                                           const uint8_t *code,
                                           uint8_t code_len,
                                           uint8_t enc_key_out[16],
                                           uint8_t hmac_key_out[32])
{
    return security_get_frame_keys_mode_internal(local_id,
                                                 peer_id,
                                                 mode,
                                                 code,
                                                 code_len,
                                                 true,
                                                 enc_key_out,
                                                 hmac_key_out);
}

bool security_main_get_frame_keys(uint16_t local_id,
                                  uint16_t peer_id,
                                  bool use_pair_link,
                                  uint8_t enc_key_out[16],
                                  uint8_t hmac_key_out[32])
{
    security_frame_key_mode_t mode = SECURITY_FRAME_KEY_MODE_SHARED;

    if (use_pair_link && (peer_id != LAVIET_BROADCAST_ID))
    {
        mode = SECURITY_FRAME_KEY_MODE_PAIR_V1_32;
    }

    return security_main_get_frame_keys_mode(local_id, peer_id, mode, enc_key_out, hmac_key_out);
}

bool security_main_get_gateway_counter(uint32_t *rx_counter_out, uint32_t *tx_counter_out)
{
    bool ok = false;

    if ((rx_counter_out == NULL) || (tx_counter_out == NULL) || (s_security_mutex == NULL))
    {
        return false;
    }

    if (osMutexAcquire(s_security_mutex, 100U) == osOK)
    {
        *rx_counter_out = s_gateway_rx_counter;
        *tx_counter_out = s_gateway_tx_counter;
        ok = s_security_initialized;
        (void)osMutexRelease(s_security_mutex);
    }

    return ok;
}

bool security_main_commit_gateway_counter(uint32_t rx_counter, uint32_t tx_counter)
{
    bool ok = false;

    if (s_security_mutex == NULL)
    {
        return false;
    }

    if (osMutexAcquire(s_security_mutex, 100U) == osOK)
    {
        if (s_security_initialized)
        {
            s_gateway_rx_counter = rx_counter;
            s_gateway_tx_counter = tx_counter;
            if (!security_store_gateway_counter_to_store(rx_counter, tx_counter))
            {
                printf("SEC: gateway counter persist failed\r\n");
            }
            ok = true;
        }
        (void)osMutexRelease(s_security_mutex);
    }

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

    security_bootstrap_tpm();
    security_log_stack_high_water("after_tpm");
    security_bootstrap_store();
    security_log_stack_high_water("after_store");

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
                                                                        cmd.u.add_device.code_len,
                                                                        cmd.u.add_device.gateway_slot);
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
                    s_runtime_cfg.radio_profiles_persisted = false;
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

static void security_log_stack_high_water(const char *tag)
{
    UBaseType_t words;

    words = uxTaskGetStackHighWaterMark(NULL);
    printf("SEC: stack %s high-water=%lu words (%lu B)\r\n",
           (tag != NULL) ? tag : "now",
           (unsigned long)words,
           (unsigned long)(words * sizeof(StackType_t)));
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

static void security_key_seed_to_key(const uint8_t seed[SECURITY_KEY_SEED_BYTES], uint8_t key_out[16])
{
    static const uint8_t label[] = "SEC:NET:ROOT";
    uint8_t digest[LAVIET_SHA256_LEN];

    if ((seed == NULL) || (key_out == NULL))
    {
        return;
    }

    memset(digest, 0, sizeof(digest));
    if (laviet_hmac_sha256(seed,
                           SECURITY_KEY_SEED_BYTES,
                           label,
                           (uint16_t)(sizeof(label) - 1U),
                           digest))
    {
        memcpy(key_out, digest, 16U);
    }
    else
    {
        memset(key_out, 0, 16U);
    }
    laviet_secure_zero(digest, sizeof(digest));
}

static void security_key_seed_to_store_key(const uint8_t seed[SECURITY_KEY_SEED_BYTES], uint8_t key_out[16])
{
    static const uint8_t label[] = "SEC:EEPROM:KEY";
    uint8_t digest[LAVIET_SHA256_LEN];

    if ((seed == NULL) || (key_out == NULL))
    {
        return;
    }

    memset(digest, 0, sizeof(digest));
    if (laviet_hmac_sha256(seed,
                           SECURITY_KEY_SEED_BYTES,
                           label,
                           (uint16_t)(sizeof(label) - 1U),
                           digest))
    {
        memcpy(key_out, digest, 16U);
    }
    laviet_secure_zero(digest, sizeof(digest));
}

static bool security_seed_has_data(const uint8_t seed[SECURITY_KEY_SEED_BYTES])
{
    uint8_t i;

    if (seed == NULL)
    {
        return false;
    }

    for (i = 0U; i < SECURITY_KEY_SEED_BYTES; i++)
    {
        if (seed[i] != 0U)
        {
            return true;
        }
    }

    return false;
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
    static const uint8_t label[] = "SEC:PAIR:V1";
    uint8_t info[sizeof(label) - 1U + 8U];
    uint8_t digest[LAVIET_SHA256_LEN];
    uint32_t lo;
    uint32_t hi;

    if ((key_out == NULL) || (code == NULL) || (code_len == 0U))
    {
        return;
    }

    lo = (local_node_id < peer_node_id) ? local_node_id : peer_node_id;
    hi = (local_node_id < peer_node_id) ? peer_node_id : local_node_id;
    memset(info, 0, sizeof(info));
    memset(digest, 0, sizeof(digest));
    memcpy(info, label, sizeof(label) - 1U);
    security_be32_write(&info[sizeof(label) - 1U], lo);
    security_be32_write(&info[sizeof(label) - 1U + 4U], hi);

    if (laviet_hmac_sha256(code, code_len, info, (uint16_t)sizeof(info), digest))
    {
        memcpy(key_out, digest, 16U);
    }
    else
    {
        memset(key_out, 0, 16U);
    }

    laviet_secure_zero(info, sizeof(info));
    laviet_secure_zero(digest, sizeof(digest));
}

static bool security_load_runtime_and_seed_from_store(bool *seed_loaded_out)
{
    security_store_wire_t w;

    if (seed_loaded_out != NULL)
    {
        *seed_loaded_out = false;
    }

    if (!s_runtime_shadow_valid)
    {
        return false;
    }

    w = s_runtime_shadow_store;
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
    if (security_seed_has_data(w.seed))
    {
        memcpy(s_key_seed_cached, w.seed, SECURITY_KEY_SEED_BYTES);
        if (seed_loaded_out != NULL)
        {
            *seed_loaded_out = true;
        }
    }

    if (w.version >= 2U)
    {
        s_runtime_cfg.radio_profiles_persisted = security_load_radio_profiles_from_store();
    }

    return true;
}

static bool security_save_runtime_and_seed_to_store(void)
{
    security_store_wire_t w;

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
    if (!s_key_seed_tpm_backed)
    {
        memcpy(w.seed, s_key_seed_cached, SECURITY_KEY_SEED_BYTES);
    }
    s_runtime_shadow_store = w;
    s_runtime_shadow_valid = true;

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
    s_runtime_shadow_radio = w;
    s_runtime_shadow_valid = true;
    return true;
}

static bool security_load_radio_profiles_from_store(void)
{
    security_radio_store_wire_t w;
    uint8_t bit_pos = 0U;
    uint32_t value = 0UL;

    if (!s_runtime_shadow_valid)
    {
        return false;
    }
    w = s_runtime_shadow_radio;
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
    s_runtime_cfg.lora.payload_len = s_runtime_cfg.lora.implicit_header ? LAVIET_FRAME_MAX_LEN : 0U;
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
    return false;
}

static bool security_load_key_seed_legacy_from_store(uint8_t seed[SECURITY_KEY_SEED_BYTES])
{
    (void)seed;
    return false;
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

static bool security_tpm_load_root_seed(uint8_t seed[SECURITY_KEY_SEED_BYTES])
{
    uint16_t out_len = 0U;
    uint32_t tpm_rc = 0UL;
    uint32_t nv_index;
    st33ktpm2x_status_t rc;
    uint8_t attempt;

    if ((seed == NULL) || !s_tpm_ready)
    {
        return false;
    }

    for (attempt = 0U; attempt < 2U; attempt++)
    {
        nv_index = (attempt == 0U) ? SECURITY_TPM_ROOT_NV_INDEX : SECURITY_TPM_ROOT_LEGACY_NV_INDEX;
        memset(seed, 0, SECURITY_KEY_SEED_BYTES);
        out_len = 0U;
        tpm_rc = 0UL;

        rc = st33ktpm2x_tpm2_nv_read(&s_tpm,
                                     nv_index,
                                     0U,
                                     seed,
                                     SECURITY_KEY_SEED_BYTES,
                                     &out_len,
                                     &tpm_rc);
        printf("SEC: TPM NV read idx=0x%08lX rc=%d(%s) tpm_rc=0x%08lX len=%u nonzero=%u\r\n",
               (unsigned long)nv_index,
               (int)rc,
               security_tpm_status_text(rc),
               (unsigned long)tpm_rc,
               (unsigned int)out_len,
               security_seed_has_data(seed) ? 1U : 0U);
        if ((rc == ST33KTPM2X_OK) &&
            (out_len == SECURITY_KEY_SEED_BYTES) &&
            security_seed_has_data(seed))
        {
            s_key_seed_tpm_pp_backed = (nv_index == SECURITY_TPM_ROOT_NV_INDEX);
            if (nv_index == SECURITY_TPM_ROOT_LEGACY_NV_INDEX)
            {
                printf("SEC: legacy TPM root seed loaded; hold PP during key rotation to migrate\r\n");
            }
            return true;
        }
    }

    if ((rc != ST33KTPM2X_ENOTSUP) && (rc != ST33KTPM2X_ETPM_RC))
    {
        printf("SEC: TPM root seed read failed rc=%d(%s) tpm_rc=0x%08lX\r\n",
               (int)rc,
               security_tpm_status_text(rc),
               (unsigned long)tpm_rc);
    }

    return false;
}

static bool security_tpm_define_root_seed(void)
{
    uint32_t tpm_rc = 0UL;
    st33ktpm2x_status_t rc;

    if (!s_tpm_ready)
    {
        return false;
    }

    rc = st33ktpm2x_tpm2_nv_define(&s_tpm,
                                   SECURITY_TPM_ROOT_NV_INDEX,
                                   SECURITY_KEY_SEED_BYTES,
                                   SECURITY_TPM_ROOT_NV_ATTRS,
                                   &tpm_rc);
    printf("SEC: TPM NV define idx=0x%08lX attrs=0x%08lX rc=%d(%s) tpm_rc=0x%08lX\r\n",
           (unsigned long)SECURITY_TPM_ROOT_NV_INDEX,
           (unsigned long)SECURITY_TPM_ROOT_NV_ATTRS,
           (int)rc,
           security_tpm_status_text(rc),
           (unsigned long)tpm_rc);
    if (rc == ST33KTPM2X_OK)
    {
        return true;
    }

    if ((rc != ST33KTPM2X_ENOTSUP) && (rc != ST33KTPM2X_ETPM_RC))
    {
        printf("SEC: TPM root seed define failed rc=%d(%s) tpm_rc=0x%08lX\r\n",
               (int)rc,
               security_tpm_status_text(rc),
               (unsigned long)tpm_rc);
    }

    return false;
}

static bool security_tpm_store_root_seed(const uint8_t seed[SECURITY_KEY_SEED_BYTES])
{
    uint32_t tpm_rc = 0UL;
    st33ktpm2x_status_t rc;

    if ((seed == NULL) || !s_tpm_ready || !security_seed_has_data(seed))
    {
        return false;
    }

    rc = st33ktpm2x_tpm2_nv_write_platform_pp(&s_tpm,
                                              SECURITY_TPM_ROOT_NV_INDEX,
                                              0U,
                                              seed,
                                              SECURITY_KEY_SEED_BYTES,
                                              &tpm_rc);
    printf("SEC: TPM NV write PP idx=0x%08lX rc=%d(%s) tpm_rc=0x%08lX\r\n",
           (unsigned long)SECURITY_TPM_ROOT_NV_INDEX,
           (int)rc,
           security_tpm_status_text(rc),
           (unsigned long)tpm_rc);
    if (rc == ST33KTPM2X_OK)
    {
        s_key_seed_tpm_pp_backed = true;
        return true;
    }

    if ((rc == ST33KTPM2X_ETPM_RC) && security_tpm_define_root_seed())
    {
        tpm_rc = 0UL;
        rc = st33ktpm2x_tpm2_nv_write_platform_pp(&s_tpm,
                                                  SECURITY_TPM_ROOT_NV_INDEX,
                                                  0U,
                                                  seed,
                                                  SECURITY_KEY_SEED_BYTES,
                                                  &tpm_rc);
        printf("SEC: TPM NV write PP retry idx=0x%08lX rc=%d(%s) tpm_rc=0x%08lX\r\n",
               (unsigned long)SECURITY_TPM_ROOT_NV_INDEX,
               (int)rc,
               security_tpm_status_text(rc),
               (unsigned long)tpm_rc);
        if (rc == ST33KTPM2X_OK)
        {
            s_key_seed_tpm_pp_backed = true;
            return true;
        }
    }

    if ((rc != ST33KTPM2X_ENOTSUP) && (rc != ST33KTPM2X_ETPM_RC))
    {
        printf("SEC: TPM root seed write failed rc=%d(%s) tpm_rc=0x%08lX\r\n",
               (int)rc,
               security_tpm_status_text(rc),
               (unsigned long)tpm_rc);
    }

    return false;
}

static bool security_get_entropy_bytes(uint8_t *out, uint8_t len)
{
    uint16_t out_len = 0U;
    uint32_t tpm_rc = 0UL;
    st33ktpm2x_status_t tpm_st;
    uint8_t i;

    if ((out == NULL) || (len == 0U))
    {
        return false;
    }

    if (s_tpm_ready)
    {
        tpm_st = st33ktpm2x_tpm2_get_random(&s_tpm,
                                            len,
                                            out,
                                            len,
                                            &out_len,
                                            &tpm_rc);
        if ((tpm_st == ST33KTPM2X_OK) && (out_len >= len))
        {
            return true;
        }

        printf("SEC: TPM random unavailable rc=%d(%s) tpm_rc=0x%08lX len=%u\r\n",
               (int)tpm_st,
               security_tpm_status_text(tpm_st),
               (unsigned long)tpm_rc,
               (unsigned int)out_len);
        s_tpm_ready = false;
    }

    for (i = 0U; i < len; i += 4U)
    {
        uint32_t random_word = 0UL;
        uint8_t chunk = ((uint8_t)(len - i) >= 4U) ? 4U : (uint8_t)(len - i);
        uint8_t j;

        if ((HAL_RNG_GetState(&hrng) != HAL_RNG_STATE_READY) ||
            (HAL_RNG_GenerateRandomNumber(&hrng, &random_word) != HAL_OK))
        {
            printf("SEC: HAL RNG unavailable\r\n");
            return false;
        }

        for (j = 0U; j < chunk; j++)
        {
            out[i + j] = (uint8_t)(random_word >> (24U - (j * 8U)));
        }
    }

    return true;
}

static bool security_rotate_key_internal(void)
{
    uint8_t seed[SECURITY_KEY_SEED_BYTES];

    memset(seed, 0, sizeof(seed));
    if (!security_get_entropy_bytes(seed, sizeof(seed)))
    {
        laviet_secure_zero(seed, sizeof(seed));
        return false;
    }

    if (s_tpm_ready)
    {
        if (!security_tpm_store_root_seed(seed))
        {
            printf("SEC: root seed write requires TPM PP button\r\n");
            laviet_secure_zero(seed, sizeof(seed));
            return false;
        }
        s_key_seed_tpm_backed = true;
        s_key_seed_tpm_pp_backed = true;
    }
    else
    {
        s_key_seed_tpm_backed = false;
        s_key_seed_tpm_pp_backed = false;
    }

    memcpy(s_key_seed_cached, seed, SECURITY_KEY_SEED_BYTES);
    security_key_seed_to_key(seed, s_network_key);
    if (s_mem_ready)
    {
        (void)security_save_runtime_and_seed_to_store();
    }

    laviet_secure_zero(seed, sizeof(seed));
    return true;
}

static uint8_t security_trusted_store_capacity(void)
{
    uint16_t available;
    uint16_t reserved_offset;

    if (!s_mem_ready)
    {
        return 0U;
    }
    if (s_mem_store.secret_slot_count <= SECURITY_TRUSTED_SLOT_BASE)
    {
        return 0U;
    }

    available = (uint16_t)(s_mem_store.secret_slot_count - SECURITY_TRUSTED_SLOT_BASE);
    if (SECURITY_GATEWAY_COUNTER_SLOT >= SECURITY_TRUSTED_SLOT_BASE)
    {
        reserved_offset = (uint16_t)(SECURITY_GATEWAY_COUNTER_SLOT - SECURITY_TRUSTED_SLOT_BASE);
        if (available > reserved_offset)
        {
            available = reserved_offset;
        }
    }
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

static void security_be32_write(uint8_t *dst, uint32_t value)
{
    if (dst == NULL)
    {
        return;
    }

    dst[0] = (uint8_t)(value >> 24);
    dst[1] = (uint8_t)(value >> 16);
    dst[2] = (uint8_t)(value >> 8);
    dst[3] = (uint8_t)value;
}

static uint32_t security_be32_read(const uint8_t *src)
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

static bool security_load_gateway_counter_from_store(void)
{
    uint8_t data[I2C_MEM_STORE_SECRET_PAYLOAD_MAX];
    uint8_t len = 0U;
    i2c_mem_store_status_t rc;

    if (!s_mem_ready || (s_mem_store.secret_slot_count <= SECURITY_GATEWAY_COUNTER_SLOT))
    {
        return false;
    }

    memset(data, 0, sizeof(data));
    rc = i2c_mem_store_secret_read(&s_mem_store,
                                   SECURITY_GATEWAY_COUNTER_SLOT,
                                   data,
                                   sizeof(data),
                                   &len);
    if ((rc != I2C_MEM_STORE_OK) ||
        (len != SECURITY_GATEWAY_COUNTER_STORE_LEN) ||
        (data[0] != SECURITY_GATEWAY_COUNTER_MAGIC) ||
        (data[1] != SECURITY_GATEWAY_COUNTER_VERSION))
    {
        laviet_secure_zero(data, sizeof(data));
        return false;
    }

    s_gateway_rx_counter = security_be32_read(&data[2]);
    s_gateway_tx_counter = security_be32_read(&data[6]);
    laviet_secure_zero(data, sizeof(data));
    return true;
}

static bool security_store_gateway_counter_to_store(uint32_t rx_counter, uint32_t tx_counter)
{
    uint8_t data[SECURITY_GATEWAY_COUNTER_STORE_LEN];
    i2c_mem_store_status_t rc;

    if (!s_mem_ready)
    {
        return true;
    }
    if (s_mem_store.secret_slot_count <= SECURITY_GATEWAY_COUNTER_SLOT)
    {
        return false;
    }

    memset(data, 0, sizeof(data));
    data[0] = SECURITY_GATEWAY_COUNTER_MAGIC;
    data[1] = SECURITY_GATEWAY_COUNTER_VERSION;
    security_be32_write(&data[2], rx_counter);
    security_be32_write(&data[6], tx_counter);

    rc = i2c_mem_store_secret_write(&s_mem_store,
                                    SECURITY_GATEWAY_COUNTER_SLOT,
                                    data,
                                    sizeof(data));
    laviet_secure_zero(data, sizeof(data));
    return (rc == I2C_MEM_STORE_OK);
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
    if (rc != I2C_MEM_STORE_OK)
    {
        printf("SEC: trusted store write failed idx=%u slot=%u rc=%d cap=%u secret_slots=%u code_len=%u\r\n",
               (unsigned int)idx,
               (unsigned int)security_trusted_store_slot(idx),
               (int)rc,
               (unsigned int)capacity,
               (unsigned int)s_mem_store.secret_slot_count,
               (unsigned int)rec.code_len);
    }
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

static uint8_t security_load_trusted_from_store(void)
{
    uint8_t idx;
    uint8_t capacity = security_trusted_store_capacity();
    uint8_t loaded = 0U;

    if (!s_mem_ready)
    {
        return 0U;
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
        s_trusted[idx].is_master = (idx == 0U);
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
        loaded++;
    }

    printf("SEC: trusted slots persisted=%u loaded=%u\r\n", capacity, loaded);
    return loaded;
}

static bool security_migrate_secrets_from_default_store_key(const i2c_mem_store_cfg_t *tpm_cfg)
{
    i2c_mem_store_cfg_t legacy_cfg;
    uint8_t loaded;
    uint8_t idx;
    bool counter_loaded;

    if ((tpm_cfg == NULL) || !s_key_seed_tpm_backed)
    {
        return false;
    }

    i2c_mem_store_default_cfg_m24c01r(&legacy_cfg, &hi2c1);
    legacy_cfg.secret_area_bytes = tpm_cfg->secret_area_bytes;

    if (i2c_mem_store_init(&s_mem_store, &legacy_cfg, false) != I2C_MEM_STORE_OK)
    {
        (void)i2c_mem_store_init(&s_mem_store, tpm_cfg, false);
        return false;
    }

    memset(s_trusted, 0, sizeof(s_trusted));
    counter_loaded = security_load_gateway_counter_from_store();
    loaded = security_load_trusted_from_store();
    if ((loaded == 0U) && !counter_loaded)
    {
        (void)i2c_mem_store_init(&s_mem_store, tpm_cfg, false);
        return false;
    }

    if (i2c_mem_store_init(&s_mem_store, tpm_cfg, false) != I2C_MEM_STORE_OK)
    {
        printf("SEC: trusted TPM-key reinit failed after legacy load\r\n");
        memset(s_trusted, 0, sizeof(s_trusted));
        return false;
    }

    if (counter_loaded && !security_store_gateway_counter_to_store(s_gateway_rx_counter, s_gateway_tx_counter))
    {
        printf("SEC: gateway counter TPM-key migration failed\r\n");
        return false;
    }

    for (idx = 0U; idx < SECURITY_TRUSTED_MAX; idx++)
    {
        if (s_trusted[idx].in_use && !security_store_trusted_slot(idx))
        {
            printf("SEC: trusted TPM-key migration failed idx=%u\r\n", idx);
            return false;
        }
    }

    printf("SEC: EEPROM secrets migrated to TPM-derived key\r\n");
    return true;
}

static bool security_add_device_internal(uint32_t node_id, const uint8_t *code, uint8_t len, bool gateway_slot)
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

    if (gateway_slot)
    {
        bool persist_ok;

        if (max_slots == 0U)
        {
            return false;
        }

        if (s_trusted[0].in_use &&
            (s_trusted[0].node_id != 0U) &&
            (s_trusted[0].node_id != node_id))
        {
            printf("SEC: replacing slot0 0x%08lX with gateway 0x%08lX\r\n",
                   (unsigned long)s_trusted[0].node_id,
                   (unsigned long)node_id);
        }

        memset(&s_trusted[0], 0, sizeof(s_trusted[0]));
        s_trusted[0].in_use = true;
        s_trusted[0].is_master = true;
        s_trusted[0].node_id = node_id;
        s_trusted[0].code_len = len;
        if ((len > 0U) && (code != NULL))
        {
            memcpy(s_trusted[0].code, code, len);
        }

        persist_ok = security_store_trusted_slot(0U);
        if (!persist_ok)
        {
            printf("SEC: trusted gateway persist failed idx=0; continuing with volatile pairing\r\n");
        }

        for (i = 1U; i < max_slots; i++)
        {
            if (s_trusted[i].in_use && (s_trusted[i].node_id == node_id))
            {
                memset(&s_trusted[i], 0, sizeof(s_trusted[i]));
                if (!security_erase_trusted_slot(i))
                {
                    printf("SEC: trusted duplicate erase failed idx=%u\r\n", i);
                }
            }
        }

        return true;
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
    }

    for (i = 1U; i < max_slots; i++)
    {
        if (!s_trusted[i].in_use)
        {
            free_idx = i;
            break;
        }
    }

    if (free_idx == 0xFFU)
    {
        return false;
    }

    s_trusted[free_idx].in_use = true;
    s_trusted[free_idx].is_master = gateway_slot;
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
        out->is_master = (idx == 0U);
        out->in_use = false;
        return true;
    }

    out->in_use = true;
    out->is_master = s_trusted[idx].is_master;
    out->slot = idx;
    out->node_id = s_trusted[idx].node_id;
    out->code_len = s_trusted[idx].code_len;
    memcpy(out->code, s_trusted[idx].code, out->code_len);
    return true;
}

static const char *security_tpm_status_text(st33ktpm2x_status_t rc)
{
    switch (rc)
    {
        case ST33KTPM2X_OK:
            return "OK";
        case ST33KTPM2X_EINVAL:
            return "EINVAL";
        case ST33KTPM2X_ESTATE:
            return "ESTATE";
        case ST33KTPM2X_EHAL:
            return "EHAL";
        case ST33KTPM2X_ETIMEOUT:
            return "ETIMEOUT";
        case ST33KTPM2X_EOVERFLOW:
            return "EOVERFLOW";
        case ST33KTPM2X_EPROTO:
            return "EPROTO";
        case ST33KTPM2X_ETPM_RC:
            return "ETPM_RC";
        case ST33KTPM2X_ENOTSUP:
            return "ENOTSUP";
        default:
            return "?";
    }
}

static uint32_t security_le32_read(const uint8_t *src)
{
    return ((uint32_t)src[3] << 24) |
           ((uint32_t)src[2] << 16) |
           ((uint32_t)src[1] << 8) |
           (uint32_t)src[0];
}

static void security_tpm_log_i2c_state(const char *tag, I2C_HandleTypeDef *hi2c)
{
    if (hi2c == NULL)
    {
        SECURITY_TPM_INIT_LOG("SEC: TPM I2C %s handle=NULL\r\n", (tag != NULL) ? tag : "state");
        return;
    }

    SECURITY_TPM_INIT_LOG("SEC: TPM I2C %s inst=%p state=%d mode=%d err=0x%08lX timing=0x%08lX\r\n",
                          (tag != NULL) ? tag : "state",
                          (void *)hi2c->Instance,
                          (int)HAL_I2C_GetState(hi2c),
                          (int)HAL_I2C_GetMode(hi2c),
                          (unsigned long)HAL_I2C_GetError(hi2c),
                          (unsigned long)hi2c->Init.Timing);
}

static void security_tpm_log_lines(const char *tag, const st33ktpm2x_cfg_t *cfg)
{
    GPIO_PinState reset_state = GPIO_PIN_RESET;
    GPIO_PinState davint_state = GPIO_PIN_RESET;
    GPIO_PinState scl_state;
    GPIO_PinState sda_state;

    scl_state = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_0);
    sda_state = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_1);
    if ((cfg != NULL) && (cfg->reset_port != NULL) && (cfg->reset_pin != 0U))
    {
        reset_state = HAL_GPIO_ReadPin(cfg->reset_port, cfg->reset_pin);
    }
    if ((cfg != NULL) && (cfg->davint_port != NULL) && (cfg->davint_pin != 0U))
    {
        davint_state = HAL_GPIO_ReadPin(cfg->davint_port, cfg->davint_pin);
    }

    SECURITY_TPM_INIT_LOG("SEC: TPM lines %s SCL_PC0=%u SDA_PC1=%u RESET#=%u DAVINT#=%u\r\n",
                          (tag != NULL) ? tag : "now",
                          (unsigned int)scl_state,
                          (unsigned int)sda_state,
                          (unsigned int)reset_state,
                          (unsigned int)davint_state);
}

static void security_tpm_scan_i2c3(const st33ktpm2x_cfg_t *cfg)
{
    uint8_t addr;
    uint8_t hits = 0U;

    if ((cfg == NULL) || (cfg->hi2c == NULL))
    {
        return;
    }

    for (addr = 0x08U; addr <= 0x77U; addr++)
    {
        HAL_StatusTypeDef st = HAL_I2C_IsDeviceReady(cfg->hi2c, (uint16_t)(addr << 1), 1U, 2U);
        if (st == HAL_OK)
        {
            hits++;
            SECURITY_TPM_INIT_LOG("SEC: TPM I2C3 scan hit addr=0x%02X\r\n", (unsigned int)addr);
        }
    }

    if (hits == 0U)
    {
        SECURITY_TPM_INIT_LOG("SEC: TPM I2C3 scan no devices\r\n");
    }
    security_tpm_log_i2c_state("after_scan", cfg->hi2c);
}

static void security_tpm_dump_reg(const st33ktpm2x_cfg_t *cfg,
                                  const char *name,
                                  uint16_t reg,
                                  uint16_t mem_addr_size,
                                  uint8_t len)
{
    uint8_t buf[8];
    HAL_StatusTypeDef st;
    uint8_t i;

    if ((cfg == NULL) || (cfg->hi2c == NULL) || (len == 0U) || (len > sizeof(buf)))
    {
        return;
    }

    memset(buf, 0xEE, sizeof(buf));
    if (mem_addr_size == I2C_MEMADD_SIZE_8BIT)
    {
        uint8_t reg8 = (uint8_t)reg;

        st = HAL_I2C_Master_Transmit(cfg->hi2c,
                                     (uint16_t)(cfg->i2c_addr_7bit << 1),
                                     &reg8,
                                     1U,
                                     SECURITY_TPM_DIAG_TIMEOUT_MS);
        if (st == HAL_OK)
        {
            app_delay_ms(1U);
            st = HAL_I2C_Master_Receive(cfg->hi2c,
                                        (uint16_t)(cfg->i2c_addr_7bit << 1),
                                        buf,
                                        len,
                                        SECURITY_TPM_DIAG_TIMEOUT_MS);
        }
    }
    else
    {
        st = HAL_I2C_Mem_Read(cfg->hi2c,
                              (uint16_t)(cfg->i2c_addr_7bit << 1),
                              reg,
                              mem_addr_size,
                              buf,
                              len,
                              SECURITY_TPM_DIAG_TIMEOUT_MS);
    }

    SECURITY_TPM_INIT_LOG("SEC: TPM reg%s %s addr=0x%02X reg=0x%04X len=%u st=%d err=0x%08lX data=",
                          (mem_addr_size == I2C_MEMADD_SIZE_8BIT) ? "8" : "16",
                          (name != NULL) ? name : "?",
                          (unsigned int)cfg->i2c_addr_7bit,
                          (unsigned int)reg,
                          (unsigned int)len,
                          (int)st,
                          (unsigned long)HAL_I2C_GetError(cfg->hi2c));
    for (i = 0U; i < len; i++)
    {
        SECURITY_TPM_INIT_LOG("%02X", (unsigned int)buf[i]);
    }
    if ((st == HAL_OK) && (len == 4U))
    {
        SECURITY_TPM_INIT_LOG(" le=0x%08lX be=0x%08lX",
                              (unsigned long)security_le32_read(buf),
                              (unsigned long)security_be32_read(buf));
    }
    SECURITY_TPM_INIT_LOG("\r\n");
}

static void security_tpm_dump_raw_regs(const st33ktpm2x_cfg_t *cfg)
{
    security_tpm_dump_reg(cfg, "LOCSEL", SECURITY_TPM_REG_LOC_SEL, I2C_MEMADD_SIZE_8BIT, 1U);
    security_tpm_dump_reg(cfg, "ACCESS", SECURITY_TPM_REG_ACCESS, I2C_MEMADD_SIZE_8BIT, 1U);
    security_tpm_dump_reg(cfg, "STS", SECURITY_TPM_REG_STS, I2C_MEMADD_SIZE_8BIT, 3U);
    security_tpm_dump_reg(cfg, "IFCAP", SECURITY_TPM_REG_IF_CAP, I2C_MEMADD_SIZE_8BIT, 4U);
    security_tpm_dump_reg(cfg, "DIDVID", SECURITY_TPM_REG_DID_VID, I2C_MEMADD_SIZE_8BIT, 4U);
    security_tpm_dump_reg(cfg, "RID", SECURITY_TPM_REG_RID, I2C_MEMADD_SIZE_8BIT, 1U);
    security_tpm_dump_reg(cfg, "DIDVID", SECURITY_TPM_REG_DID_VID, I2C_MEMADD_SIZE_16BIT, 4U);
    security_tpm_dump_reg(cfg, "RID", SECURITY_TPM_REG_RID, I2C_MEMADD_SIZE_16BIT, 1U);
}

static void security_bootstrap_tpm(void)
{
    st33ktpm2x_cfg_t cfg;
    st33ktpm2x_status_t rc;
    HAL_StatusTypeDef i2c_probe;
    uint32_t tpm_rc = 0UL;
    uint32_t did_vid = 0UL;
    uint8_t rid = 0U;
    bool davint_asserted = false;

    st33ktpm2x_default_cfg(&cfg, &hi2c3);
    SECURITY_TPM_INIT_LOG("SEC: TPM cfg hi2c=%p inst=%p addr7=0x%02X io=%lu locality=%lu burst=%lu reset_port=%p reset_pin=0x%04X davint_port=%p davint_pin=0x%04X\r\n",
                          (void *)cfg.hi2c,
                          (cfg.hi2c != NULL) ? (void *)cfg.hi2c->Instance : NULL,
                          (unsigned int)cfg.i2c_addr_7bit,
                          (unsigned long)cfg.io_timeout_ms,
                          (unsigned long)cfg.locality_timeout_ms,
                          (unsigned long)cfg.burst_timeout_ms,
                          (void *)cfg.reset_port,
                          (unsigned int)cfg.reset_pin,
                          (void *)cfg.davint_port,
                          (unsigned int)cfg.davint_pin);
    security_tpm_log_i2c_state("before_init", cfg.hi2c);
    security_tpm_log_lines("before_reset", &cfg);

    rc = st33ktpm2x_init(&s_tpm, &cfg);
    if (rc != ST33KTPM2X_OK)
    {
        printf("SEC: TPM init failed rc=%d(%s)\r\n",
               (int)rc,
               security_tpm_status_text(rc));
        s_tpm_ready = false;
        return;
    }

    SECURITY_TPM_INIT_LOG("SEC: TPM hard reset pulse=%lu recovery=%lu\r\n",
                          (unsigned long)cfg.reset_pulse_ms,
                          (unsigned long)cfg.reset_recovery_ms);
    rc = st33ktpm2x_hard_reset(&s_tpm);
    SECURITY_TPM_INIT_LOG("SEC: TPM hard reset rc=%d(%s)\r\n",
                          (int)rc,
                          security_tpm_status_text(rc));
    security_tpm_log_lines("after_reset", &cfg);
    security_tpm_log_i2c_state("after_reset", cfg.hi2c);
    if (st33ktpm2x_davint_is_asserted(&s_tpm, &davint_asserted) == ST33KTPM2X_OK)
    {
        SECURITY_TPM_INIT_LOG("SEC: TPM DAVINT# %s\r\n", davint_asserted ? "asserted" : "idle");
    }

    security_tpm_scan_i2c3(&cfg);

    i2c_probe = HAL_I2C_IsDeviceReady(cfg.hi2c,
                                      (uint16_t)(cfg.i2c_addr_7bit << 1),
                                      3U,
                                      cfg.io_timeout_ms);
    SECURITY_TPM_INIT_LOG("SEC: TPM I2C probe addr=0x%02X status=%d err=0x%08lX\r\n",
                          (unsigned int)cfg.i2c_addr_7bit,
                          (int)i2c_probe,
                          (unsigned long)HAL_I2C_GetError(cfg.hi2c));
    security_tpm_dump_raw_regs(&cfg);

    rc = st33ktpm2x_read_identity(&s_tpm, &did_vid, &rid);
    if (rc != ST33KTPM2X_OK)
    {
        s_tpm_ready = false;
        printf("SEC: TPM identity invalid rc=%d(%s) DIDVID=0x%08lX RID=0x%02X\r\n",
               (int)rc,
               security_tpm_status_text(rc),
               (unsigned long)did_vid,
               rid);
        security_tpm_log_i2c_state("identity_failed", cfg.hi2c);
        return;
    }
    SECURITY_TPM_INIT_LOG("SEC: TPM identity OK DIDVID=0x%08lX RID=0x%02X\r\n",
                          (unsigned long)did_vid,
                          rid);

    rc = st33ktpm2x_tpm2_startup(&s_tpm, ST33KTPM2X_TPM2_SU_CLEAR, &tpm_rc);
    SECURITY_TPM_INIT_LOG("SEC: TPM startup rc=%d(%s) tpm_rc=0x%08lX\r\n",
                          (int)rc,
                          security_tpm_status_text(rc),
                          (unsigned long)tpm_rc);
    if ((rc != ST33KTPM2X_OK) && (rc != ST33KTPM2X_ETPM_RC))
    {
        s_tpm_ready = false;
        printf("SEC: TPM startup failed rc=%d(%s) tpm_rc=0x%08lX; define TPM_INIT_LOG for bus trace\r\n",
               (int)rc,
               security_tpm_status_text(rc),
               (unsigned long)tpm_rc);
        security_tpm_log_i2c_state("startup_failed", cfg.hi2c);
        security_tpm_dump_raw_regs(&cfg);
        return;
    }

    rc = st33ktpm2x_tpm2_self_test(&s_tpm, false, &tpm_rc);
    SECURITY_TPM_INIT_LOG("SEC: TPM self-test rc=%d(%s) tpm_rc=0x%08lX\r\n",
                          (int)rc,
                          security_tpm_status_text(rc),
                          (unsigned long)tpm_rc);
    if ((rc != ST33KTPM2X_OK) && (rc != ST33KTPM2X_ETPM_RC))
    {
        s_tpm_ready = false;
        printf("SEC: TPM self-test failed rc=%d(%s) tpm_rc=0x%08lX; define TPM_INIT_LOG for bus trace\r\n",
               (int)rc,
               security_tpm_status_text(rc),
               (unsigned long)tpm_rc);
        security_tpm_log_i2c_state("self_test_failed", cfg.hi2c);
        security_tpm_dump_raw_regs(&cfg);
        return;
    }

    s_tpm_ready = true;
    if ((rc == ST33KTPM2X_ETPM_RC) && (tpm_rc != ST33KTPM2X_TPM2_RC_SUCCESS))
    {
        printf("SEC: TPM ready DIDVID=0x%08lX RID=0x%02X self_test_tpm_rc=0x%08lX\r\n",
               (unsigned long)did_vid,
               rid,
               (unsigned long)tpm_rc);
    }
    else
    {
        printf("SEC: TPM ready DIDVID=0x%08lX RID=0x%02X\r\n", (unsigned long)did_vid, rid);
    }
}

static void security_bootstrap_store(void)
{
    i2c_mem_store_cfg_t mem_cfg;
    bool loaded = false;
    bool loaded_v1 = false;
    bool runtime_loaded = false;
    bool store_seed_loaded = false;
    bool counter_loaded = false;
    uint8_t trusted_loaded = 0U;

    i2c_mem_store_default_cfg_m24c01r(&mem_cfg, &hi2c1);
    /* M24C01-R has only 128 B, so keep EEPROM only for trusted devices. */
    mem_cfg.secret_area_bytes = 72U;
    memset(s_key_seed_cached, 0, sizeof(s_key_seed_cached));
    s_key_seed_tpm_backed = false;
    s_key_seed_tpm_pp_backed = false;
    security_load_default_radio_profiles(&s_runtime_cfg);

    if (security_tpm_load_root_seed(s_key_seed_cached))
    {
        s_key_seed_tpm_backed = true;
        loaded = true;
        printf("SEC: root seed loaded from TPM\r\n");
        if (!s_key_seed_tpm_pp_backed)
        {
            if (security_tpm_store_root_seed(s_key_seed_cached))
            {
                s_key_seed_tpm_pp_backed = true;
                printf("SEC: root seed migrated to TPM PP-protected NV index\r\n");
            }
            else
            {
                printf("SEC: root seed PP migration pending; hold TPM PP during next boot or key rotation\r\n");
            }
        }
    }
    else if (s_tpm_ready)
    {
        loaded = security_rotate_key_internal();
        if (loaded && s_key_seed_tpm_backed)
        {
            printf("SEC: root seed generated and stored in TPM\r\n");
        }
    }

    if (s_key_seed_tpm_backed && security_seed_has_data(s_key_seed_cached))
    {
        security_key_seed_to_store_key(s_key_seed_cached, mem_cfg.crypto_key);
        printf("SEC: MEM secret key derived from TPM root seed\r\n");
    }

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
        runtime_loaded = security_load_runtime_and_seed_from_store(&store_seed_loaded);
        loaded_v1 = runtime_loaded && !s_runtime_cfg.radio_profiles_persisted;
        if (!runtime_loaded)
        {
            if (security_load_settings_legacy_from_store() &&
                security_load_key_seed_legacy_from_store(s_key_seed_cached))
            {
                runtime_loaded = true;
                store_seed_loaded = security_seed_has_data(s_key_seed_cached);
            }
        }
        if (!loaded && runtime_loaded && store_seed_loaded)
        {
            if (s_tpm_ready)
            {
                if (security_tpm_store_root_seed(s_key_seed_cached))
                {
                    s_key_seed_tpm_backed = true;
                    s_key_seed_tpm_pp_backed = true;
                    loaded = true;
                    security_key_seed_to_store_key(s_key_seed_cached, mem_cfg.crypto_key);
                    (void)i2c_mem_store_init(&s_mem_store, &mem_cfg, false);
                    printf("SEC: legacy root seed migrated to TPM; EEPROM key updated\r\n");
                }
                else
                {
                    printf("SEC: legacy root seed ignored until TPM PP button is held\r\n");
                }
            }
            else
            {
                loaded = true;
            }
        }
    }

    if (loaded && security_seed_has_data(s_key_seed_cached))
    {
        security_key_seed_to_key(s_key_seed_cached, s_network_key);
    }
    else
    {
        if (!security_rotate_key_internal())
        {
            printf("SEC: root key init failed\r\n");
            memset(s_network_key, 0, sizeof(s_network_key));
        }
    }

    if (s_mem_ready)
    {
        if (loaded_v1)
        {
            security_migrate_trusted_slot_v1_to_v2();
        }
        (void)security_save_runtime_and_seed_to_store();
        counter_loaded = security_load_gateway_counter_from_store();
        if (counter_loaded)
        {
            printf("SEC: gateway counters rx=%lu tx=%lu\r\n",
                   (unsigned long)s_gateway_rx_counter,
                   (unsigned long)s_gateway_tx_counter);
        }
        trusted_loaded = security_load_trusted_from_store();
        if (s_key_seed_tpm_backed && (trusted_loaded == 0U) && !counter_loaded)
        {
            (void)security_migrate_secrets_from_default_store_key(&mem_cfg);
        }
    }
}
