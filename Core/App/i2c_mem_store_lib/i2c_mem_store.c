/**
 * @file i2c_mem_store.c
 * @brief Implementation of external I2C memory storage helper.
 */

#include "i2c_mem_store.h"

#include "../app.h"
#include "app_delay.h"

#include <string.h>

#define I2C_MEM_STORE_META_PRIMARY_ADDR      0x0000UL
#define I2C_MEM_STORE_META_MIRROR_ADDR       0x0018UL
#define I2C_MEM_STORE_LOG_START_ADDR         0x0030UL

#define I2C_MEM_STORE_META_MAGIC             0x4D533331UL
#define I2C_MEM_STORE_META_VERSION           0x0001U
#define I2C_MEM_STORE_LOG_MAGIC              0xA55AU
#define I2C_MEM_STORE_SECRET_MAGIC           0xC0DEU

#define I2C_MEM_STORE_LOG_HEADER_SIZE        16U
#define I2C_MEM_STORE_LOG_CRC_OFFSET         (I2C_MEM_STORE_LOG_RECORD_SIZE - 2U)

#define I2C_MEM_STORE_SECRET_HEADER_SIZE     8U
#define I2C_MEM_STORE_SECRET_CRC_OFFSET      (I2C_MEM_STORE_SECRET_SLOT_SIZE - 2U)

#define I2C_MEM_STORE_META_WIRE_SIZE         24U

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t write_slot;
    uint16_t valid_count;
    uint16_t reserved;
    uint32_t next_sequence;
    uint32_t secret_counter;
    uint16_t crc16;
    uint16_t reserved2;
} i2c_mem_store_meta_wire_t;

static uint16_t i2c_mem_store_min_u16(uint16_t a, uint16_t b)
{
    return (a < b) ? a : b;
}

static i2c_mem_store_status_t i2c_mem_store_hal_to_status(HAL_StatusTypeDef st)
{
    if (st == HAL_OK)
    {
        return I2C_MEM_STORE_OK;
    }
    if (st == HAL_TIMEOUT)
    {
        return I2C_MEM_STORE_ETIMEOUT;
    }
    return I2C_MEM_STORE_EHAL;
}

static uint16_t i2c_mem_store_crc16_ccitt(const uint8_t *data, uint16_t len)
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

static void i2c_mem_store_le16_write(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFFU);
    dst[1] = (uint8_t)(value >> 8);
}

static void i2c_mem_store_le32_write(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xFFU);
    dst[1] = (uint8_t)((value >> 8) & 0xFFU);
    dst[2] = (uint8_t)((value >> 16) & 0xFFU);
    dst[3] = (uint8_t)((value >> 24) & 0xFFU);
}

static uint16_t i2c_mem_store_le16_read(const uint8_t *src)
{
    return (uint16_t)src[0] | ((uint16_t)src[1] << 8);
}

static uint32_t i2c_mem_store_le32_read(const uint8_t *src)
{
    return ((uint32_t)src[0]) |
           ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) |
           ((uint32_t)src[3] << 24);
}

static void i2c_mem_store_xtea_encrypt_block(uint32_t v[2], const uint32_t key[4])
{
    uint32_t i;
    uint32_t sum = 0U;
    uint32_t delta = 0x9E3779B9UL;
    uint32_t v0 = v[0];
    uint32_t v1 = v[1];

    for (i = 0U; i < 32U; i++)
    {
        v0 += ((((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + key[sum & 3U]));
        sum += delta;
        v1 += ((((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + key[(sum >> 11) & 3U]));
    }

    v[0] = v0;
    v[1] = v1;
}

static void i2c_mem_store_xtea_ctr_crypt(uint8_t *data,
                                         uint16_t len,
                                         const uint8_t key_bytes[16],
                                         uint32_t nonce)
{
    uint32_t key[4];
    uint32_t ctr = 0U;
    uint16_t offset = 0U;
    uint16_t i;
    uint8_t keystream[8];

    key[0] = i2c_mem_store_le32_read(&key_bytes[0]);
    key[1] = i2c_mem_store_le32_read(&key_bytes[4]);
    key[2] = i2c_mem_store_le32_read(&key_bytes[8]);
    key[3] = i2c_mem_store_le32_read(&key_bytes[12]);

    while (offset < len)
    {
        uint32_t block[2];
        block[0] = nonce;
        block[1] = ctr;
        i2c_mem_store_xtea_encrypt_block(block, key);
        i2c_mem_store_le32_write(&keystream[0], block[0]);
        i2c_mem_store_le32_write(&keystream[4], block[1]);

        for (i = 0U; (i < 8U) && (offset < len); i++)
        {
            data[offset] ^= keystream[i];
            offset++;
        }
        ctr++;
    }
}

static uint32_t i2c_mem_store_log_base_addr(const i2c_mem_store_t *ctx)
{
    return I2C_MEM_STORE_LOG_START_ADDR;
}

static uint32_t i2c_mem_store_secret_base_addr(const i2c_mem_store_t *ctx)
{
    return ctx->cfg.total_bytes - ctx->cfg.secret_area_bytes;
}

static uint32_t i2c_mem_store_log_slot_addr(const i2c_mem_store_t *ctx, uint16_t slot)
{
    return i2c_mem_store_log_base_addr(ctx) + ((uint32_t)slot * I2C_MEM_STORE_LOG_RECORD_SIZE);
}

static uint32_t i2c_mem_store_secret_slot_addr(const i2c_mem_store_t *ctx, uint16_t slot)
{
    return i2c_mem_store_secret_base_addr(ctx) + ((uint32_t)slot * I2C_MEM_STORE_SECRET_SLOT_SIZE);
}

static i2c_mem_store_status_t i2c_mem_store_read_raw(i2c_mem_store_t *ctx,
                                                      uint32_t addr,
                                                      uint8_t *dst,
                                                      uint16_t len)
{
    HAL_StatusTypeDef st;

    if ((ctx == NULL) || (dst == NULL) || (len == 0U))
    {
        return I2C_MEM_STORE_EINVAL;
    }

    st = app_i2c_mem_read(ctx->cfg.hi2c,
                          (uint16_t)(ctx->cfg.i2c_addr_7bit << 1),
                          (uint16_t)addr,
                          ctx->cfg.mem_addr_size,
                          dst,
                          len,
                          100U);
    return i2c_mem_store_hal_to_status(st);
}

static i2c_mem_store_status_t i2c_mem_store_write_raw(i2c_mem_store_t *ctx,
                                                       uint32_t addr,
                                                       const uint8_t *src,
                                                       uint16_t len)
{
    uint32_t current_addr = addr;
    uint16_t remaining = len;
    uint16_t chunk;
    uint16_t page_off;
    HAL_StatusTypeDef st;

    if ((ctx == NULL) || (src == NULL) || (len == 0U))
    {
        return I2C_MEM_STORE_EINVAL;
    }

    while (remaining > 0U)
    {
        page_off = (uint16_t)(current_addr % ctx->cfg.page_bytes);
        chunk = i2c_mem_store_min_u16(remaining, (uint16_t)(ctx->cfg.page_bytes - page_off));

        st = app_i2c_mem_write(ctx->cfg.hi2c,
                               (uint16_t)(ctx->cfg.i2c_addr_7bit << 1),
                               (uint16_t)current_addr,
                               ctx->cfg.mem_addr_size,
                               src,
                               chunk,
                               100U);
        if (st != HAL_OK)
        {
            return i2c_mem_store_hal_to_status(st);
        }

        app_delay_ms(ctx->cfg.write_cycle_ms);
        current_addr += chunk;
        src += chunk;
        remaining = (uint16_t)(remaining - chunk);
    }

    return I2C_MEM_STORE_OK;
}

static void i2c_mem_store_meta_encode(const i2c_mem_store_t *ctx, uint8_t out[I2C_MEM_STORE_META_WIRE_SIZE])
{
    i2c_mem_store_meta_wire_t w;

    memset(&w, 0, sizeof(w));
    w.magic = I2C_MEM_STORE_META_MAGIC;
    w.version = I2C_MEM_STORE_META_VERSION;
    w.write_slot = ctx->write_slot;
    w.valid_count = ctx->valid_count;
    w.next_sequence = ctx->next_sequence;
    w.secret_counter = ctx->secret_counter;

    i2c_mem_store_le32_write(&out[0], w.magic);
    i2c_mem_store_le16_write(&out[4], w.version);
    i2c_mem_store_le16_write(&out[6], w.write_slot);
    i2c_mem_store_le16_write(&out[8], w.valid_count);
    i2c_mem_store_le16_write(&out[10], 0U);
    i2c_mem_store_le32_write(&out[12], w.next_sequence);
    i2c_mem_store_le32_write(&out[16], w.secret_counter);
    i2c_mem_store_le16_write(&out[20], 0U);
    i2c_mem_store_le16_write(&out[22], 0U);

    w.crc16 = i2c_mem_store_crc16_ccitt(out, 20U);
    i2c_mem_store_le16_write(&out[20], w.crc16);
}

static bool i2c_mem_store_meta_decode(i2c_mem_store_t *ctx, const uint8_t in[I2C_MEM_STORE_META_WIRE_SIZE])
{
    uint16_t crc_stored;
    uint16_t crc_calc;
    uint16_t write_slot;
    uint16_t valid_count;

    crc_stored = i2c_mem_store_le16_read(&in[20]);
    crc_calc = i2c_mem_store_crc16_ccitt(in, 20U);
    if (crc_calc != crc_stored)
    {
        return false;
    }

    if (i2c_mem_store_le32_read(&in[0]) != I2C_MEM_STORE_META_MAGIC)
    {
        return false;
    }
    if (i2c_mem_store_le16_read(&in[4]) != I2C_MEM_STORE_META_VERSION)
    {
        return false;
    }

    write_slot = i2c_mem_store_le16_read(&in[6]);
    valid_count = i2c_mem_store_le16_read(&in[8]);
    if ((write_slot >= ctx->slot_count) || (valid_count > ctx->slot_count))
    {
        return false;
    }

    ctx->write_slot = write_slot;
    ctx->valid_count = valid_count;
    ctx->next_sequence = i2c_mem_store_le32_read(&in[12]);
    ctx->secret_counter = i2c_mem_store_le32_read(&in[16]);
    return true;
}

static i2c_mem_store_status_t i2c_mem_store_meta_save(i2c_mem_store_t *ctx)
{
    uint8_t raw[I2C_MEM_STORE_META_WIRE_SIZE];
    i2c_mem_store_status_t rc;

    i2c_mem_store_meta_encode(ctx, raw);

    rc = i2c_mem_store_write_raw(ctx, I2C_MEM_STORE_META_PRIMARY_ADDR, raw, sizeof(raw));
    if (rc != I2C_MEM_STORE_OK)
    {
        return rc;
    }
    rc = i2c_mem_store_write_raw(ctx, I2C_MEM_STORE_META_MIRROR_ADDR, raw, sizeof(raw));
    return rc;
}

static i2c_mem_store_status_t i2c_mem_store_meta_load(i2c_mem_store_t *ctx)
{
    uint8_t raw[I2C_MEM_STORE_META_WIRE_SIZE];
    i2c_mem_store_status_t rc;

    rc = i2c_mem_store_read_raw(ctx, I2C_MEM_STORE_META_PRIMARY_ADDR, raw, sizeof(raw));
    if ((rc == I2C_MEM_STORE_OK) && i2c_mem_store_meta_decode(ctx, raw))
    {
        return I2C_MEM_STORE_OK;
    }

    rc = i2c_mem_store_read_raw(ctx, I2C_MEM_STORE_META_MIRROR_ADDR, raw, sizeof(raw));
    if ((rc == I2C_MEM_STORE_OK) && i2c_mem_store_meta_decode(ctx, raw))
    {
        return I2C_MEM_STORE_OK;
    }

    return I2C_MEM_STORE_ENOTFOUND;
}

static bool i2c_mem_store_layout_valid(const i2c_mem_store_cfg_t *cfg,
                                       uint16_t *out_slot_count,
                                       uint16_t *out_secret_slots)
{
    uint32_t secret_base;
    uint32_t log_bytes;
    uint16_t slot_count;
    uint16_t secret_slots;

    if ((cfg == NULL) || (cfg->hi2c == NULL))
    {
        return false;
    }
    if ((cfg->total_bytes < 128U) || (cfg->page_bytes == 0U) || (cfg->secret_area_bytes < I2C_MEM_STORE_SECRET_SLOT_SIZE))
    {
        return false;
    }

    secret_base = cfg->total_bytes - cfg->secret_area_bytes;
    if (secret_base <= I2C_MEM_STORE_LOG_START_ADDR)
    {
        return false;
    }

    log_bytes = secret_base - I2C_MEM_STORE_LOG_START_ADDR;
    slot_count = (uint16_t)(log_bytes / I2C_MEM_STORE_LOG_RECORD_SIZE);
    secret_slots = (uint16_t)(cfg->secret_area_bytes / I2C_MEM_STORE_SECRET_SLOT_SIZE);

    if ((slot_count == 0U) || (secret_slots == 0U))
    {
        return false;
    }

    *out_slot_count = slot_count;
    *out_secret_slots = secret_slots;
    return true;
}

void i2c_mem_store_default_cfg(i2c_mem_store_cfg_t *cfg, I2C_HandleTypeDef *hi2c)
{
    uint8_t i;
    static const uint8_t default_key[16] =
    {
        0x2AU, 0x19U, 0x84U, 0xC7U,
        0x3DU, 0x55U, 0xABU, 0x10U,
        0xE1U, 0x7CU, 0x06U, 0x93U,
        0x44U, 0x5FU, 0xB2U, 0x0DU
    };

    if (cfg == NULL)
    {
        return;
    }

    memset(cfg, 0, sizeof(*cfg));
    cfg->hi2c = hi2c;
    cfg->i2c_addr_7bit = I2C_MEM_STORE_I2C_ADDR_DEFAULT;
    cfg->mem_addr_size = I2C_MEMADD_SIZE_8BIT;
    cfg->total_bytes = I2C_MEM_STORE_TOTAL_BYTES_DEFAULT;
    cfg->page_bytes = I2C_MEM_STORE_PAGE_BYTES_DEFAULT;
    cfg->write_cycle_ms = I2C_MEM_STORE_WRITE_CYCLE_MS_DEFAULT;
    cfg->secret_area_bytes = I2C_MEM_STORE_SECRET_AREA_BYTES_DEFAULT;

    for (i = 0U; i < 16U; i++)
    {
        cfg->crypto_key[i] = default_key[i];
    }
}

void i2c_mem_store_default_cfg_m24c01r(i2c_mem_store_cfg_t *cfg, I2C_HandleTypeDef *hi2c)
{
    i2c_mem_store_default_cfg(cfg, hi2c);

    if (cfg == NULL)
    {
        return;
    }

    cfg->i2c_addr_7bit = I2C_MEM_STORE_I2C_ADDR_DEFAULT;
    cfg->mem_addr_size = I2C_MEMADD_SIZE_8BIT;
    cfg->total_bytes = 128U;
    cfg->page_bytes = 8U;
    cfg->write_cycle_ms = 5U;
    cfg->secret_area_bytes = I2C_MEM_STORE_SECRET_AREA_BYTES_DEFAULT;
}

i2c_mem_store_status_t i2c_mem_store_init(i2c_mem_store_t *ctx,
                                          const i2c_mem_store_cfg_t *cfg,
                                          bool format_if_needed)
{
    i2c_mem_store_status_t rc;
    uint16_t slot_count = 0U;
    uint16_t secret_slots = 0U;

    if ((ctx == NULL) || (cfg == NULL))
    {
        return I2C_MEM_STORE_EINVAL;
    }

    if (!i2c_mem_store_layout_valid(cfg, &slot_count, &secret_slots))
    {
        return I2C_MEM_STORE_EINVAL;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->cfg = *cfg;
    ctx->slot_count = slot_count;
    ctx->secret_slot_count = secret_slots;
    ctx->initialized = true;

    rc = i2c_mem_store_meta_load(ctx);
    if (rc == I2C_MEM_STORE_OK)
    {
        return I2C_MEM_STORE_OK;
    }

    if (!format_if_needed)
    {
        ctx->initialized = false;
        return rc;
    }

    return i2c_mem_store_format(ctx);
}

i2c_mem_store_status_t i2c_mem_store_format(i2c_mem_store_t *ctx)
{
    if ((ctx == NULL) || (!ctx->initialized))
    {
        return I2C_MEM_STORE_ESTATE;
    }

    ctx->write_slot = 0U;
    ctx->valid_count = 0U;
    ctx->next_sequence = 1UL;
    ctx->secret_counter = 1UL;
    return i2c_mem_store_meta_save(ctx);
}

i2c_mem_store_status_t i2c_mem_store_append_message(i2c_mem_store_t *ctx,
                                                    int16_t rssi_dbm,
                                                    const uint8_t *payload,
                                                    uint8_t payload_len)
{
    uint8_t rec[I2C_MEM_STORE_LOG_RECORD_SIZE];
    uint8_t effective_len;
    uint16_t crc;
    uint32_t addr;
    i2c_mem_store_status_t rc;

    if ((ctx == NULL) || (!ctx->initialized) || ((payload == NULL) && (payload_len > 0U)))
    {
        return I2C_MEM_STORE_EINVAL;
    }

    effective_len = payload_len;
    if (effective_len > I2C_MEM_STORE_LOG_PAYLOAD_MAX)
    {
        effective_len = I2C_MEM_STORE_LOG_PAYLOAD_MAX;
    }

    memset(rec, 0xFF, sizeof(rec));
    i2c_mem_store_le16_write(&rec[0], I2C_MEM_STORE_LOG_MAGIC);
    rec[2] = effective_len;
    rec[3] = 0U;
    i2c_mem_store_le32_write(&rec[4], ctx->next_sequence);
    i2c_mem_store_le32_write(&rec[8], HAL_GetTick());
    i2c_mem_store_le16_write(&rec[12], (uint16_t)rssi_dbm);

    if (effective_len > 0U)
    {
        memcpy(&rec[I2C_MEM_STORE_LOG_HEADER_SIZE], payload, effective_len);
    }

    crc = i2c_mem_store_crc16_ccitt(rec, I2C_MEM_STORE_LOG_CRC_OFFSET);
    i2c_mem_store_le16_write(&rec[I2C_MEM_STORE_LOG_CRC_OFFSET], crc);

    addr = i2c_mem_store_log_slot_addr(ctx, ctx->write_slot);
    rc = i2c_mem_store_write_raw(ctx, addr, rec, sizeof(rec));
    if (rc != I2C_MEM_STORE_OK)
    {
        return rc;
    }

    ctx->write_slot = (uint16_t)((ctx->write_slot + 1U) % ctx->slot_count);
    if (ctx->valid_count < ctx->slot_count)
    {
        ctx->valid_count++;
    }
    ctx->next_sequence++;

    return i2c_mem_store_meta_save(ctx);
}

i2c_mem_store_status_t i2c_mem_store_read_message(i2c_mem_store_t *ctx,
                                                  uint16_t index_from_latest,
                                                  i2c_mem_store_log_record_t *out_record)
{
    uint16_t slot;
    uint8_t rec[I2C_MEM_STORE_LOG_RECORD_SIZE];
    uint16_t crc_stored;
    uint16_t crc_calc;
    i2c_mem_store_status_t rc;

    if ((ctx == NULL) || (!ctx->initialized) || (out_record == NULL))
    {
        return I2C_MEM_STORE_EINVAL;
    }
    if (index_from_latest >= ctx->valid_count)
    {
        return I2C_MEM_STORE_ENOTFOUND;
    }

    slot = (uint16_t)((ctx->write_slot + ctx->slot_count - 1U - index_from_latest) % ctx->slot_count);
    rc = i2c_mem_store_read_raw(ctx, i2c_mem_store_log_slot_addr(ctx, slot), rec, sizeof(rec));
    if (rc != I2C_MEM_STORE_OK)
    {
        return rc;
    }

    if (i2c_mem_store_le16_read(&rec[0]) != I2C_MEM_STORE_LOG_MAGIC)
    {
        return I2C_MEM_STORE_ENOTFOUND;
    }

    crc_stored = i2c_mem_store_le16_read(&rec[I2C_MEM_STORE_LOG_CRC_OFFSET]);
    crc_calc = i2c_mem_store_crc16_ccitt(rec, I2C_MEM_STORE_LOG_CRC_OFFSET);
    if (crc_stored != crc_calc)
    {
        return I2C_MEM_STORE_ECRC;
    }

    memset(out_record, 0, sizeof(*out_record));
    out_record->sequence = i2c_mem_store_le32_read(&rec[4]);
    out_record->timestamp_ms = i2c_mem_store_le32_read(&rec[8]);
    out_record->rssi_dbm = (int16_t)i2c_mem_store_le16_read(&rec[12]);
    out_record->length = rec[2];
    if (out_record->length > I2C_MEM_STORE_LOG_PAYLOAD_MAX)
    {
        out_record->length = I2C_MEM_STORE_LOG_PAYLOAD_MAX;
    }
    memcpy(out_record->payload, &rec[I2C_MEM_STORE_LOG_HEADER_SIZE], out_record->length);
    return I2C_MEM_STORE_OK;
}

i2c_mem_store_status_t i2c_mem_store_get_log_count(i2c_mem_store_t *ctx, uint16_t *out_count)
{
    if ((ctx == NULL) || (!ctx->initialized) || (out_count == NULL))
    {
        return I2C_MEM_STORE_EINVAL;
    }

    *out_count = ctx->valid_count;
    return I2C_MEM_STORE_OK;
}

i2c_mem_store_status_t i2c_mem_store_secret_write(i2c_mem_store_t *ctx,
                                                  uint16_t slot_id,
                                                  const uint8_t *data,
                                                  uint8_t data_len)
{
    uint8_t rec[I2C_MEM_STORE_SECRET_SLOT_SIZE];
    uint8_t plain[I2C_MEM_STORE_SECRET_PAYLOAD_MAX];
    uint32_t nonce;
    uint16_t crc;
    i2c_mem_store_status_t rc;

    if ((ctx == NULL) || (!ctx->initialized) || ((data == NULL) && (data_len > 0U)))
    {
        return I2C_MEM_STORE_EINVAL;
    }
    if ((slot_id >= ctx->secret_slot_count) || (data_len > I2C_MEM_STORE_SECRET_PAYLOAD_MAX))
    {
        return I2C_MEM_STORE_EOVERFLOW;
    }

    memset(rec, 0xFF, sizeof(rec));
    memset(plain, 0, sizeof(plain));
    if (data_len > 0U)
    {
        memcpy(plain, data, data_len);
    }

    nonce = ctx->secret_counter++;
    i2c_mem_store_xtea_ctr_crypt(plain, sizeof(plain), ctx->cfg.crypto_key, nonce);

    i2c_mem_store_le16_write(&rec[0], I2C_MEM_STORE_SECRET_MAGIC);
    rec[2] = data_len;
    rec[3] = (uint8_t)slot_id;
    i2c_mem_store_le32_write(&rec[4], nonce);
    memcpy(&rec[I2C_MEM_STORE_SECRET_HEADER_SIZE], plain, sizeof(plain));

    crc = i2c_mem_store_crc16_ccitt(rec, I2C_MEM_STORE_SECRET_CRC_OFFSET);
    i2c_mem_store_le16_write(&rec[I2C_MEM_STORE_SECRET_CRC_OFFSET], crc);

    rc = i2c_mem_store_write_raw(ctx, i2c_mem_store_secret_slot_addr(ctx, slot_id), rec, sizeof(rec));
    if (rc != I2C_MEM_STORE_OK)
    {
        return rc;
    }

    return i2c_mem_store_meta_save(ctx);
}

i2c_mem_store_status_t i2c_mem_store_secret_read(i2c_mem_store_t *ctx,
                                                 uint16_t slot_id,
                                                 uint8_t *data_out,
                                                 uint8_t data_capacity,
                                                 uint8_t *data_len)
{
    uint8_t rec[I2C_MEM_STORE_SECRET_SLOT_SIZE];
    uint8_t plain[I2C_MEM_STORE_SECRET_PAYLOAD_MAX];
    uint16_t crc_stored;
    uint16_t crc_calc;
    uint8_t len;
    uint32_t nonce;
    i2c_mem_store_status_t rc;

    if ((ctx == NULL) || (!ctx->initialized) || (data_out == NULL) || (data_len == NULL))
    {
        return I2C_MEM_STORE_EINVAL;
    }
    if (slot_id >= ctx->secret_slot_count)
    {
        return I2C_MEM_STORE_EINVAL;
    }

    rc = i2c_mem_store_read_raw(ctx, i2c_mem_store_secret_slot_addr(ctx, slot_id), rec, sizeof(rec));
    if (rc != I2C_MEM_STORE_OK)
    {
        return rc;
    }

    if (i2c_mem_store_le16_read(&rec[0]) != I2C_MEM_STORE_SECRET_MAGIC)
    {
        return I2C_MEM_STORE_ENOTFOUND;
    }

    crc_stored = i2c_mem_store_le16_read(&rec[I2C_MEM_STORE_SECRET_CRC_OFFSET]);
    crc_calc = i2c_mem_store_crc16_ccitt(rec, I2C_MEM_STORE_SECRET_CRC_OFFSET);
    if (crc_stored != crc_calc)
    {
        return I2C_MEM_STORE_ECRC;
    }

    len = rec[2];
    if (len > I2C_MEM_STORE_SECRET_PAYLOAD_MAX)
    {
        return I2C_MEM_STORE_EPROTO;
    }
    if (len > data_capacity)
    {
        return I2C_MEM_STORE_EOVERFLOW;
    }

    memcpy(plain, &rec[I2C_MEM_STORE_SECRET_HEADER_SIZE], sizeof(plain));
    nonce = i2c_mem_store_le32_read(&rec[4]);
    i2c_mem_store_xtea_ctr_crypt(plain, sizeof(plain), ctx->cfg.crypto_key, nonce);

    if (len > 0U)
    {
        memcpy(data_out, plain, len);
    }
    *data_len = len;
    return I2C_MEM_STORE_OK;
}

i2c_mem_store_status_t i2c_mem_store_secret_erase(i2c_mem_store_t *ctx, uint16_t slot_id)
{
    uint8_t marker[2] = { 0xFFU, 0xFFU };

    if ((ctx == NULL) || (!ctx->initialized) || (slot_id >= ctx->secret_slot_count))
    {
        return I2C_MEM_STORE_EINVAL;
    }

    return i2c_mem_store_write_raw(ctx, i2c_mem_store_secret_slot_addr(ctx, slot_id), marker, sizeof(marker));
}

i2c_mem_store_status_t i2c_mem_store_trusted_device_write(i2c_mem_store_t *ctx,
                                                          uint16_t slot_id,
                                                          const i2c_mem_store_trusted_device_t *trusted)
{
    uint8_t buf[I2C_MEM_STORE_SECRET_PAYLOAD_MAX];
    uint8_t total_len;

    if ((ctx == NULL) || (!ctx->initialized) || (trusted == NULL))
    {
        return I2C_MEM_STORE_EINVAL;
    }
    if ((trusted->id_len > I2C_MEM_STORE_TRUSTED_ID_MAX) || (trusted->code_len > I2C_MEM_STORE_TRUSTED_CODE_MAX))
    {
        return I2C_MEM_STORE_EOVERFLOW;
    }

    total_len = (uint8_t)(2U + trusted->id_len + trusted->code_len);
    if (total_len > I2C_MEM_STORE_SECRET_PAYLOAD_MAX)
    {
        return I2C_MEM_STORE_EOVERFLOW;
    }

    buf[0] = trusted->id_len;
    buf[1] = trusted->code_len;
    memcpy(&buf[2], trusted->id, trusted->id_len);
    memcpy(&buf[2U + trusted->id_len], trusted->code, trusted->code_len);
    return i2c_mem_store_secret_write(ctx, slot_id, buf, total_len);
}

i2c_mem_store_status_t i2c_mem_store_trusted_device_read(i2c_mem_store_t *ctx,
                                                         uint16_t slot_id,
                                                         i2c_mem_store_trusted_device_t *trusted)
{
    uint8_t buf[I2C_MEM_STORE_SECRET_PAYLOAD_MAX];
    uint8_t len = 0U;
    i2c_mem_store_status_t rc;
    uint8_t id_len;
    uint8_t code_len;

    if ((ctx == NULL) || (!ctx->initialized) || (trusted == NULL))
    {
        return I2C_MEM_STORE_EINVAL;
    }

    rc = i2c_mem_store_secret_read(ctx, slot_id, buf, sizeof(buf), &len);
    if (rc != I2C_MEM_STORE_OK)
    {
        return rc;
    }
    if (len < 2U)
    {
        return I2C_MEM_STORE_EPROTO;
    }

    id_len = buf[0];
    code_len = buf[1];
    if ((id_len > I2C_MEM_STORE_TRUSTED_ID_MAX) || (code_len > I2C_MEM_STORE_TRUSTED_CODE_MAX))
    {
        return I2C_MEM_STORE_EPROTO;
    }
    if ((uint8_t)(2U + id_len + code_len) != len)
    {
        return I2C_MEM_STORE_EPROTO;
    }

    memset(trusted, 0, sizeof(*trusted));
    trusted->id_len = id_len;
    trusted->code_len = code_len;
    memcpy(trusted->id, &buf[2], id_len);
    memcpy(trusted->code, &buf[2U + id_len], code_len);
    return I2C_MEM_STORE_OK;
}
