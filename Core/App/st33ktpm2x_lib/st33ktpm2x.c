/**
 * @file st33ktpm2x.c
 * @brief ST33KTPM2X driver implementation (TIS over I2C).
 */

#include "st33ktpm2x.h"

#include "app_delay.h"
#include "main.h"

#include <string.h>

/* TIS register map (locality 0 base). */
#define ST33_REG_ACCESS                 0x0000U
#define ST33_REG_STS                    0x0018U
#define ST33_REG_DATA_FIFO              0x0024U
#define ST33_REG_DID_VID                0x0F00U
#define ST33_REG_RID                    0x0F04U

/* ACCESS bits. */
#define ST33_ACCESS_VALID               0x80U
#define ST33_ACCESS_ACTIVE_LOCALITY     0x20U
#define ST33_ACCESS_REQUEST_USE         0x02U

/* STS bits. */
#define ST33_STS_VALID                  0x80U
#define ST33_STS_COMMAND_READY          0x40U
#define ST33_STS_TPM_GO                 0x20U
#define ST33_STS_DATA_AVAIL             0x10U
#define ST33_STS_EXPECT                 0x08U

#define ST33_TPM_HEADER_SIZE            10U

static uint16_t st33_min_u16(uint16_t a, uint16_t b)
{
    return (a < b) ? a : b;
}

static uint16_t st33_be16_read(const uint8_t *buf)
{
    return (uint16_t)(((uint16_t)buf[0] << 8) | buf[1]);
}

static uint32_t st33_be32_read(const uint8_t *buf)
{
    return ((uint32_t)buf[0] << 24) |
           ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8) |
           (uint32_t)buf[3];
}

static void st33_be16_write(uint8_t *buf, uint16_t v)
{
    buf[0] = (uint8_t)(v >> 8);
    buf[1] = (uint8_t)v;
}

static void st33_be32_write(uint8_t *buf, uint32_t v)
{
    buf[0] = (uint8_t)(v >> 24);
    buf[1] = (uint8_t)(v >> 16);
    buf[2] = (uint8_t)(v >> 8);
    buf[3] = (uint8_t)v;
}

static st33ktpm2x_status_t st33_i2c_read(st33ktpm2x_t *ctx,
                                         uint16_t reg,
                                         uint8_t *data,
                                         uint16_t len)
{
    HAL_StatusTypeDef hal_st;

    if ((ctx == NULL) || (data == NULL) || (len == 0U))
    {
        return ST33KTPM2X_EINVAL;
    }

    hal_st = HAL_I2C_Mem_Read(ctx->cfg.hi2c,
                              (uint16_t)(ctx->cfg.i2c_addr_7bit << 1),
                              reg,
                              I2C_MEMADD_SIZE_16BIT,
                              data,
                              len,
                              ctx->cfg.io_timeout_ms);
    if (hal_st == HAL_OK)
    {
        return ST33KTPM2X_OK;
    }
    if (hal_st == HAL_TIMEOUT)
    {
        return ST33KTPM2X_ETIMEOUT;
    }
    return ST33KTPM2X_EHAL;
}

static st33ktpm2x_status_t st33_i2c_write(st33ktpm2x_t *ctx,
                                          uint16_t reg,
                                          const uint8_t *data,
                                          uint16_t len)
{
    HAL_StatusTypeDef hal_st;

    if ((ctx == NULL) || (data == NULL) || (len == 0U))
    {
        return ST33KTPM2X_EINVAL;
    }

    hal_st = HAL_I2C_Mem_Write(ctx->cfg.hi2c,
                               (uint16_t)(ctx->cfg.i2c_addr_7bit << 1),
                               reg,
                               I2C_MEMADD_SIZE_16BIT,
                               (uint8_t *)data,
                               len,
                               ctx->cfg.io_timeout_ms);
    if (hal_st == HAL_OK)
    {
        return ST33KTPM2X_OK;
    }
    if (hal_st == HAL_TIMEOUT)
    {
        return ST33KTPM2X_ETIMEOUT;
    }
    return ST33KTPM2X_EHAL;
}

static st33ktpm2x_status_t st33_read_access(st33ktpm2x_t *ctx, uint8_t *access)
{
    return st33_i2c_read(ctx, ST33_REG_ACCESS, access, 1U);
}

static st33ktpm2x_status_t st33_write_access(st33ktpm2x_t *ctx, uint8_t value)
{
    return st33_i2c_write(ctx, ST33_REG_ACCESS, &value, 1U);
}

static st33ktpm2x_status_t st33_read_sts(st33ktpm2x_t *ctx, uint8_t *sts, uint16_t *burst_count)
{
    uint8_t raw[3];
    st33ktpm2x_status_t rc;

    rc = st33_i2c_read(ctx, ST33_REG_STS, raw, sizeof(raw));
    if (rc != ST33KTPM2X_OK)
    {
        return rc;
    }

    if (sts != NULL)
    {
        *sts = raw[0];
    }
    if (burst_count != NULL)
    {
        *burst_count = (uint16_t)raw[1] | ((uint16_t)raw[2] << 8);
    }

    return ST33KTPM2X_OK;
}

static st33ktpm2x_status_t st33_write_sts(st33ktpm2x_t *ctx, uint8_t value)
{
    return st33_i2c_write(ctx, ST33_REG_STS, &value, 1U);
}

static st33ktpm2x_status_t st33_wait_access_bits(st33ktpm2x_t *ctx,
                                                 uint8_t mask,
                                                 uint8_t expected,
                                                 uint32_t timeout_ms)
{
    uint32_t start_ms;
    uint8_t access = 0U;
    st33ktpm2x_status_t rc;

    start_ms = HAL_GetTick();
    do
    {
        rc = st33_read_access(ctx, &access);
        if (rc == ST33KTPM2X_OK)
        {
            if ((access & mask) == expected)
            {
                return ST33KTPM2X_OK;
            }
        }
        app_delay_ms(1U);
    } while ((HAL_GetTick() - start_ms) < timeout_ms);

    return ST33KTPM2X_ETIMEOUT;
}

static st33ktpm2x_status_t st33_wait_sts_bits(st33ktpm2x_t *ctx,
                                              uint8_t mask,
                                              uint8_t expected,
                                              uint32_t timeout_ms)
{
    uint32_t start_ms;
    uint8_t sts = 0U;
    st33ktpm2x_status_t rc;

    start_ms = HAL_GetTick();
    do
    {
        rc = st33_read_sts(ctx, &sts, NULL);
        if (rc == ST33KTPM2X_OK)
        {
            if ((sts & mask) == expected)
            {
                return ST33KTPM2X_OK;
            }
        }
        app_delay_ms(1U);
    } while ((HAL_GetTick() - start_ms) < timeout_ms);

    return ST33KTPM2X_ETIMEOUT;
}

static st33ktpm2x_status_t st33_wait_burst(st33ktpm2x_t *ctx, uint16_t *burst_out, uint8_t require_data_avail)
{
    uint32_t start_ms;
    uint8_t sts = 0U;
    uint16_t burst = 0U;
    st33ktpm2x_status_t rc;

    if (burst_out == NULL)
    {
        return ST33KTPM2X_EINVAL;
    }

    start_ms = HAL_GetTick();
    do
    {
        rc = st33_read_sts(ctx, &sts, &burst);
        if (rc == ST33KTPM2X_OK)
        {
            if ((sts & ST33_STS_VALID) != 0U)
            {
                if ((burst > 0U) && (((sts & ST33_STS_DATA_AVAIL) != 0U) || (require_data_avail == 0U)))
                {
                    *burst_out = burst;
                    return ST33KTPM2X_OK;
                }
            }
        }
        app_delay_ms(1U);
    } while ((HAL_GetTick() - start_ms) < ctx->cfg.burst_timeout_ms);

    return ST33KTPM2X_ETIMEOUT;
}

static st33ktpm2x_status_t st33_fifo_write(st33ktpm2x_t *ctx, const uint8_t *data, uint16_t len)
{
    return st33_i2c_write(ctx, ST33_REG_DATA_FIFO, data, len);
}

static st33ktpm2x_status_t st33_fifo_read(st33ktpm2x_t *ctx, uint8_t *data, uint16_t len)
{
    return st33_i2c_read(ctx, ST33_REG_DATA_FIFO, data, len);
}

static st33ktpm2x_status_t st33_read_response_stream(st33ktpm2x_t *ctx, uint8_t *dst, uint16_t len)
{
    uint16_t got = 0U;
    uint16_t burst = 0U;
    uint16_t chunk = 0U;
    st33ktpm2x_status_t rc;

    while (got < len)
    {
        rc = st33_wait_burst(ctx, &burst, 1U);
        if (rc != ST33KTPM2X_OK)
        {
            return rc;
        }

        chunk = st33_min_u16((uint16_t)(len - got), burst);
        rc = st33_fifo_read(ctx, &dst[got], chunk);
        if (rc != ST33KTPM2X_OK)
        {
            return rc;
        }
        got = (uint16_t)(got + chunk);
    }

    return ST33KTPM2X_OK;
}

void st33ktpm2x_default_cfg(st33ktpm2x_cfg_t *cfg, I2C_HandleTypeDef *hi2c)
{
    if (cfg == NULL)
    {
        return;
    }

    memset(cfg, 0, sizeof(*cfg));
    cfg->hi2c = hi2c;
    cfg->i2c_addr_7bit = ST33KTPM2X_I2C_ADDR_DEFAULT;

    cfg->reset_port = TMP_RESET_GPIO_Port;
    cfg->reset_pin = TMP_RESET_Pin;
    cfg->reset_pulse_ms = 5U;
    cfg->reset_recovery_ms = 50U;

    cfg->pp_port = NULL;
    cfg->pp_pin = 0U;
    cfg->pp_active_state = GPIO_PIN_SET;

    cfg->io_timeout_ms = 100U;
    cfg->locality_timeout_ms = 100U;
    cfg->burst_timeout_ms = 100U;
}

st33ktpm2x_status_t st33ktpm2x_init(st33ktpm2x_t *ctx, const st33ktpm2x_cfg_t *cfg)
{
    if ((ctx == NULL) || (cfg == NULL) || (cfg->hi2c == NULL))
    {
        return ST33KTPM2X_EINVAL;
    }

    if ((cfg->reset_port == NULL) || (cfg->reset_pin == 0U))
    {
        return ST33KTPM2X_EINVAL;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->cfg = *cfg;
    ctx->initialized = true;
    return ST33KTPM2X_OK;
}

st33ktpm2x_status_t st33ktpm2x_deinit(st33ktpm2x_t *ctx)
{
    if ((ctx == NULL) || (!ctx->initialized))
    {
        return ST33KTPM2X_ESTATE;
    }

    (void)st33ktpm2x_release_locality0(ctx);
    memset(ctx, 0, sizeof(*ctx));
    return ST33KTPM2X_OK;
}

st33ktpm2x_status_t st33ktpm2x_hard_reset(st33ktpm2x_t *ctx)
{
    if ((ctx == NULL) || (!ctx->initialized))
    {
        return ST33KTPM2X_ESTATE;
    }

    HAL_GPIO_WritePin(ctx->cfg.reset_port, ctx->cfg.reset_pin, GPIO_PIN_RESET);
    app_delay_ms(ctx->cfg.reset_pulse_ms);
    HAL_GPIO_WritePin(ctx->cfg.reset_port, ctx->cfg.reset_pin, GPIO_PIN_SET);
    app_delay_ms(ctx->cfg.reset_recovery_ms);
    ctx->locality0_acquired = false;
    return ST33KTPM2X_OK;
}

st33ktpm2x_status_t st33ktpm2x_pp_is_pressed(st33ktpm2x_t *ctx, bool *pressed)
{
    GPIO_PinState state;

    if ((ctx == NULL) || (!ctx->initialized) || (pressed == NULL))
    {
        return ST33KTPM2X_EINVAL;
    }

    if ((ctx->cfg.pp_port == NULL) || (ctx->cfg.pp_pin == 0U))
    {
        return ST33KTPM2X_ENOTSUP;
    }

    state = HAL_GPIO_ReadPin(ctx->cfg.pp_port, ctx->cfg.pp_pin);
    *pressed = (state == ctx->cfg.pp_active_state);
    return ST33KTPM2X_OK;
}

st33ktpm2x_status_t st33ktpm2x_pp_wait_pressed(st33ktpm2x_t *ctx, uint32_t timeout_ms)
{
    uint32_t start_ms;
    bool pressed = false;
    st33ktpm2x_status_t rc;

    if ((ctx == NULL) || (!ctx->initialized))
    {
        return ST33KTPM2X_ESTATE;
    }

    start_ms = HAL_GetTick();
    do
    {
        rc = st33ktpm2x_pp_is_pressed(ctx, &pressed);
        if (rc != ST33KTPM2X_OK)
        {
            return rc;
        }
        if (pressed)
        {
            return ST33KTPM2X_OK;
        }
        app_delay_ms(1U);
    } while ((HAL_GetTick() - start_ms) < timeout_ms);

    return ST33KTPM2X_ETIMEOUT;
}

st33ktpm2x_status_t st33ktpm2x_request_locality0(st33ktpm2x_t *ctx)
{
    uint8_t access = 0U;
    st33ktpm2x_status_t rc;

    if ((ctx == NULL) || (!ctx->initialized))
    {
        return ST33KTPM2X_ESTATE;
    }

    if (ctx->locality0_acquired)
    {
        return ST33KTPM2X_OK;
    }

    rc = st33_read_access(ctx, &access);
    if (rc != ST33KTPM2X_OK)
    {
        return rc;
    }

    if ((access & ST33_ACCESS_ACTIVE_LOCALITY) == 0U)
    {
        rc = st33_write_access(ctx, ST33_ACCESS_REQUEST_USE);
        if (rc != ST33KTPM2X_OK)
        {
            return rc;
        }
    }

    rc = st33_wait_access_bits(ctx,
                               (uint8_t)(ST33_ACCESS_VALID | ST33_ACCESS_ACTIVE_LOCALITY),
                               (uint8_t)(ST33_ACCESS_VALID | ST33_ACCESS_ACTIVE_LOCALITY),
                               ctx->cfg.locality_timeout_ms);
    if (rc == ST33KTPM2X_OK)
    {
        ctx->locality0_acquired = true;
    }
    return rc;
}

st33ktpm2x_status_t st33ktpm2x_release_locality0(st33ktpm2x_t *ctx)
{
    st33ktpm2x_status_t rc;

    if ((ctx == NULL) || (!ctx->initialized))
    {
        return ST33KTPM2X_ESTATE;
    }

    if (!ctx->locality0_acquired)
    {
        return ST33KTPM2X_OK;
    }

    rc = st33_write_access(ctx, ST33_ACCESS_ACTIVE_LOCALITY);
    if (rc == ST33KTPM2X_OK)
    {
        ctx->locality0_acquired = false;
    }
    return rc;
}

st33ktpm2x_status_t st33ktpm2x_read_identity(st33ktpm2x_t *ctx, uint32_t *did_vid, uint8_t *rid)
{
    uint8_t id_raw[4];
    st33ktpm2x_status_t rc;

    if ((ctx == NULL) || (!ctx->initialized) || (did_vid == NULL) || (rid == NULL))
    {
        return ST33KTPM2X_EINVAL;
    }

    rc = st33_i2c_read(ctx, ST33_REG_DID_VID, id_raw, sizeof(id_raw));
    if (rc != ST33KTPM2X_OK)
    {
        return rc;
    }

    *did_vid = st33_be32_read(id_raw);
    rc = st33_i2c_read(ctx, ST33_REG_RID, rid, 1U);
    return rc;
}

st33ktpm2x_status_t st33ktpm2x_transceive(st33ktpm2x_t *ctx,
                                          const uint8_t *command,
                                          uint16_t command_len,
                                          uint8_t *response,
                                          uint16_t response_capacity,
                                          uint16_t *response_len,
                                          uint32_t *tpm_rc)
{
    uint8_t sts = 0U;
    uint16_t burst = 0U;
    uint16_t sent = 0U;
    uint16_t chunk = 0U;
    uint16_t rsp_total = 0U;
    st33ktpm2x_status_t rc;

    if ((ctx == NULL) || (!ctx->initialized) || (command == NULL) || (response == NULL) || (response_len == NULL))
    {
        return ST33KTPM2X_EINVAL;
    }
    if ((command_len < ST33_TPM_HEADER_SIZE) || (command_len > ST33KTPM2X_CMD_MAX_BYTES))
    {
        return ST33KTPM2X_EINVAL;
    }

    *response_len = 0U;
    if (tpm_rc != NULL)
    {
        *tpm_rc = 0UL;
    }

    rc = st33ktpm2x_request_locality0(ctx);
    if (rc != ST33KTPM2X_OK)
    {
        return rc;
    }

    rc = st33_write_sts(ctx, ST33_STS_COMMAND_READY);
    if (rc != ST33KTPM2X_OK)
    {
        (void)st33ktpm2x_release_locality0(ctx);
        return rc;
    }

    rc = st33_wait_sts_bits(ctx, ST33_STS_COMMAND_READY, ST33_STS_COMMAND_READY, ctx->cfg.burst_timeout_ms);
    if (rc != ST33KTPM2X_OK)
    {
        (void)st33ktpm2x_release_locality0(ctx);
        return rc;
    }

    while (sent < command_len)
    {
        rc = st33_wait_burst(ctx, &burst, 0U);
        if (rc != ST33KTPM2X_OK)
        {
            (void)st33ktpm2x_release_locality0(ctx);
            return rc;
        }

        chunk = st33_min_u16((uint16_t)(command_len - sent), burst);
        rc = st33_fifo_write(ctx, &command[sent], chunk);
        if (rc != ST33KTPM2X_OK)
        {
            (void)st33ktpm2x_release_locality0(ctx);
            return rc;
        }

        sent = (uint16_t)(sent + chunk);
        rc = st33_read_sts(ctx, &sts, NULL);
        if (rc != ST33KTPM2X_OK)
        {
            (void)st33ktpm2x_release_locality0(ctx);
            return rc;
        }
        if ((sent < command_len) && ((sts & ST33_STS_EXPECT) == 0U))
        {
            (void)st33ktpm2x_release_locality0(ctx);
            return ST33KTPM2X_EPROTO;
        }
    }

    rc = st33_write_sts(ctx, ST33_STS_TPM_GO);
    if (rc != ST33KTPM2X_OK)
    {
        (void)st33ktpm2x_release_locality0(ctx);
        return rc;
    }

    rc = st33_wait_sts_bits(ctx, ST33_STS_DATA_AVAIL, ST33_STS_DATA_AVAIL, ctx->cfg.burst_timeout_ms);
    if (rc != ST33KTPM2X_OK)
    {
        (void)st33ktpm2x_release_locality0(ctx);
        return rc;
    }

    rc = st33_read_response_stream(ctx, response, ST33_TPM_HEADER_SIZE);
    if (rc != ST33KTPM2X_OK)
    {
        (void)st33ktpm2x_release_locality0(ctx);
        return rc;
    }

    rsp_total = (uint16_t)st33_be32_read(&response[2]);
    if ((rsp_total < ST33_TPM_HEADER_SIZE) || (rsp_total > ST33KTPM2X_RSP_MAX_BYTES))
    {
        (void)st33_write_sts(ctx, ST33_STS_COMMAND_READY);
        (void)st33ktpm2x_release_locality0(ctx);
        return ST33KTPM2X_EPROTO;
    }
    if (rsp_total > response_capacity)
    {
        (void)st33_write_sts(ctx, ST33_STS_COMMAND_READY);
        (void)st33ktpm2x_release_locality0(ctx);
        return ST33KTPM2X_EOVERFLOW;
    }

    if (rsp_total > ST33_TPM_HEADER_SIZE)
    {
        rc = st33_read_response_stream(ctx,
                                       &response[ST33_TPM_HEADER_SIZE],
                                       (uint16_t)(rsp_total - ST33_TPM_HEADER_SIZE));
        if (rc != ST33KTPM2X_OK)
        {
            (void)st33_write_sts(ctx, ST33_STS_COMMAND_READY);
            (void)st33ktpm2x_release_locality0(ctx);
            return rc;
        }
    }

    *response_len = rsp_total;
    if (tpm_rc != NULL)
    {
        *tpm_rc = st33_be32_read(&response[6]);
    }

    (void)st33_write_sts(ctx, ST33_STS_COMMAND_READY);
    (void)st33ktpm2x_release_locality0(ctx);

    if ((tpm_rc != NULL) && (*tpm_rc != ST33KTPM2X_TPM2_RC_SUCCESS))
    {
        return ST33KTPM2X_ETPM_RC;
    }

    return ST33KTPM2X_OK;
}

st33ktpm2x_status_t st33ktpm2x_tpm2_startup(st33ktpm2x_t *ctx,
                                            uint16_t startup_type,
                                            uint32_t *tpm_rc)
{
    uint8_t cmd[12];
    uint8_t rsp[ST33_TPM_HEADER_SIZE];
    uint16_t rsp_len = 0U;

    st33_be16_write(&cmd[0], ST33KTPM2X_TPM2_ST_NO_SESSIONS);
    st33_be32_write(&cmd[2], (uint32_t)sizeof(cmd));
    st33_be32_write(&cmd[6], ST33KTPM2X_TPM2_CC_STARTUP);
    st33_be16_write(&cmd[10], startup_type);

    return st33ktpm2x_transceive(ctx, cmd, (uint16_t)sizeof(cmd), rsp, (uint16_t)sizeof(rsp), &rsp_len, tpm_rc);
}

st33ktpm2x_status_t st33ktpm2x_tpm2_self_test(st33ktpm2x_t *ctx,
                                              bool full_test,
                                              uint32_t *tpm_rc)
{
    uint8_t cmd[11];
    uint8_t rsp[ST33_TPM_HEADER_SIZE];
    uint16_t rsp_len = 0U;

    st33_be16_write(&cmd[0], ST33KTPM2X_TPM2_ST_NO_SESSIONS);
    st33_be32_write(&cmd[2], (uint32_t)sizeof(cmd));
    st33_be32_write(&cmd[6], ST33KTPM2X_TPM2_CC_SELF_TEST);
    cmd[10] = full_test ? 1U : 0U;

    return st33ktpm2x_transceive(ctx, cmd, (uint16_t)sizeof(cmd), rsp, (uint16_t)sizeof(rsp), &rsp_len, tpm_rc);
}

st33ktpm2x_status_t st33ktpm2x_tpm2_get_random(st33ktpm2x_t *ctx,
                                               uint16_t requested_bytes,
                                               uint8_t *random_out,
                                               uint16_t random_capacity,
                                               uint16_t *random_len,
                                               uint32_t *tpm_rc)
{
    uint8_t cmd[12];
    uint8_t rsp[ST33KTPM2X_RSP_MAX_BYTES];
    uint16_t rsp_len = 0U;
    uint16_t rnd_len = 0U;
    st33ktpm2x_status_t rc;

    if ((random_out == NULL) || (random_len == NULL))
    {
        return ST33KTPM2X_EINVAL;
    }

    *random_len = 0U;

    st33_be16_write(&cmd[0], ST33KTPM2X_TPM2_ST_NO_SESSIONS);
    st33_be32_write(&cmd[2], (uint32_t)sizeof(cmd));
    st33_be32_write(&cmd[6], ST33KTPM2X_TPM2_CC_GET_RANDOM);
    st33_be16_write(&cmd[10], requested_bytes);

    rc = st33ktpm2x_transceive(ctx, cmd, (uint16_t)sizeof(cmd), rsp, (uint16_t)sizeof(rsp), &rsp_len, tpm_rc);
    if (rc != ST33KTPM2X_OK)
    {
        return rc;
    }

    if (rsp_len < (ST33_TPM_HEADER_SIZE + 2U))
    {
        return ST33KTPM2X_EPROTO;
    }

    rnd_len = st33_be16_read(&rsp[ST33_TPM_HEADER_SIZE]);
    if ((uint16_t)(ST33_TPM_HEADER_SIZE + 2U + rnd_len) > rsp_len)
    {
        return ST33KTPM2X_EPROTO;
    }
    if (rnd_len > random_capacity)
    {
        return ST33KTPM2X_EOVERFLOW;
    }

    memcpy(random_out, &rsp[ST33_TPM_HEADER_SIZE + 2U], rnd_len);
    *random_len = rnd_len;
    return ST33KTPM2X_OK;
}

st33ktpm2x_status_t st33ktpm2x_tpm2_get_capability(st33ktpm2x_t *ctx,
                                                   uint32_t capability,
                                                   uint32_t property,
                                                   uint32_t property_count,
                                                   uint8_t *payload_out,
                                                   uint16_t payload_capacity,
                                                   uint16_t *payload_len,
                                                   bool *more_data,
                                                   uint32_t *tpm_rc)
{
    uint8_t cmd[22];
    uint8_t rsp[ST33KTPM2X_RSP_MAX_BYTES];
    uint16_t rsp_len = 0U;
    uint16_t capability_data_len = 0U;
    st33ktpm2x_status_t rc;

    if ((payload_out == NULL) || (payload_len == NULL) || (more_data == NULL))
    {
        return ST33KTPM2X_EINVAL;
    }
    *payload_len = 0U;
    *more_data = false;

    st33_be16_write(&cmd[0], ST33KTPM2X_TPM2_ST_NO_SESSIONS);
    st33_be32_write(&cmd[2], (uint32_t)sizeof(cmd));
    st33_be32_write(&cmd[6], ST33KTPM2X_TPM2_CC_GET_CAPABILITY);
    st33_be32_write(&cmd[10], capability);
    st33_be32_write(&cmd[14], property);
    st33_be32_write(&cmd[18], property_count);

    rc = st33ktpm2x_transceive(ctx, cmd, (uint16_t)sizeof(cmd), rsp, (uint16_t)sizeof(rsp), &rsp_len, tpm_rc);
    if (rc != ST33KTPM2X_OK)
    {
        return rc;
    }

    if (rsp_len < (ST33_TPM_HEADER_SIZE + 1U))
    {
        return ST33KTPM2X_EPROTO;
    }

    *more_data = (rsp[ST33_TPM_HEADER_SIZE] != 0U);
    capability_data_len = (uint16_t)(rsp_len - ST33_TPM_HEADER_SIZE - 1U);
    if (capability_data_len > payload_capacity)
    {
        return ST33KTPM2X_EOVERFLOW;
    }
    memcpy(payload_out, &rsp[ST33_TPM_HEADER_SIZE + 1U], capability_data_len);
    *payload_len = capability_data_len;
    return ST33KTPM2X_OK;
}

st33ktpm2x_status_t st33ktpm2x_tpm2_pcr_read_sha256(st33ktpm2x_t *ctx,
                                                    uint8_t pcr_index,
                                                    uint8_t digest_sha256_out[32],
                                                    uint32_t *update_counter_out,
                                                    uint32_t *tpm_rc)
{
    uint8_t cmd[20];
    uint8_t rsp[ST33KTPM2X_RSP_MAX_BYTES];
    uint16_t rsp_len = 0U;
    uint16_t off = ST33_TPM_HEADER_SIZE;
    uint32_t sel_count;
    uint32_t dig_count;
    uint8_t sizeof_select;
    uint16_t dig_len;
    uint32_t i;
    st33ktpm2x_status_t rc;

    if ((digest_sha256_out == NULL) || (update_counter_out == NULL) || (pcr_index > 23U))
    {
        return ST33KTPM2X_EINVAL;
    }

    memset(digest_sha256_out, 0, 32U);
    *update_counter_out = 0UL;

    st33_be16_write(&cmd[0], ST33KTPM2X_TPM2_ST_NO_SESSIONS);
    st33_be32_write(&cmd[2], (uint32_t)sizeof(cmd));
    st33_be32_write(&cmd[6], ST33KTPM2X_TPM2_CC_PCR_READ);

    st33_be32_write(&cmd[10], 1UL); /* TPML_PCR_SELECTION.count */
    st33_be16_write(&cmd[14], ST33KTPM2X_TPM2_ALG_SHA256);
    cmd[16] = 3U; /* sizeofSelect */
    cmd[17] = 0U;
    cmd[18] = 0U;
    cmd[19] = 0U;
    cmd[17U + (uint8_t)(pcr_index / 8U)] = (uint8_t)(1U << (pcr_index % 8U));

    rc = st33ktpm2x_transceive(ctx, cmd, (uint16_t)sizeof(cmd), rsp, (uint16_t)sizeof(rsp), &rsp_len, tpm_rc);
    if (rc != ST33KTPM2X_OK)
    {
        return rc;
    }

    if (rsp_len < (ST33_TPM_HEADER_SIZE + 4U + 4U + 4U + 2U))
    {
        return ST33KTPM2X_EPROTO;
    }

    *update_counter_out = st33_be32_read(&rsp[off]);
    off = (uint16_t)(off + 4U);

    sel_count = st33_be32_read(&rsp[off]);
    off = (uint16_t)(off + 4U);
    for (i = 0UL; i < sel_count; i++)
    {
        if ((uint16_t)(off + 3U) > rsp_len)
        {
            return ST33KTPM2X_EPROTO;
        }
        off = (uint16_t)(off + 2U);      /* hash */
        sizeof_select = rsp[off];
        off = (uint16_t)(off + 1U);      /* sizeofSelect */
        if ((uint16_t)(off + sizeof_select) > rsp_len)
        {
            return ST33KTPM2X_EPROTO;
        }
        off = (uint16_t)(off + sizeof_select);
    }

    if ((uint16_t)(off + 4U + 2U) > rsp_len)
    {
        return ST33KTPM2X_EPROTO;
    }

    dig_count = st33_be32_read(&rsp[off]);
    off = (uint16_t)(off + 4U);
    if (dig_count == 0UL)
    {
        return ST33KTPM2X_EPROTO;
    }

    dig_len = st33_be16_read(&rsp[off]);
    off = (uint16_t)(off + 2U);
    if (dig_len != 32U)
    {
        return ST33KTPM2X_EPROTO;
    }
    if ((uint16_t)(off + dig_len) > rsp_len)
    {
        return ST33KTPM2X_EPROTO;
    }

    memcpy(digest_sha256_out, &rsp[off], 32U);
    return ST33KTPM2X_OK;
}

st33ktpm2x_status_t st33ktpm2x_tpm2_start_auth_session(st33ktpm2x_t *ctx,
                                                        uint32_t *session_handle_out,
                                                        uint32_t *tpm_rc)
{
    (void)ctx;
    if (session_handle_out != NULL)
    {
        *session_handle_out = 0UL;
    }
    if (tpm_rc != NULL)
    {
        *tpm_rc = 0UL;
    }
    return ST33KTPM2X_ENOTSUP;
}

st33ktpm2x_status_t st33ktpm2x_tpm2_policy_pcr(st33ktpm2x_t *ctx,
                                                uint32_t session_handle,
                                                uint8_t pcr_index,
                                                uint32_t *tpm_rc)
{
    (void)ctx;
    (void)session_handle;
    (void)pcr_index;
    if (tpm_rc != NULL)
    {
        *tpm_rc = 0UL;
    }
    return ST33KTPM2X_ENOTSUP;
}

st33ktpm2x_status_t st33ktpm2x_tpm2_policy_physical_presence(st33ktpm2x_t *ctx,
                                                              uint32_t session_handle,
                                                              uint32_t *tpm_rc)
{
    (void)ctx;
    (void)session_handle;
    if (tpm_rc != NULL)
    {
        *tpm_rc = 0UL;
    }
    return ST33KTPM2X_ENOTSUP;
}

st33ktpm2x_status_t st33ktpm2x_tpm2_policy_command_code(st33ktpm2x_t *ctx,
                                                         uint32_t session_handle,
                                                         uint32_t command_code,
                                                         uint32_t *tpm_rc)
{
    (void)ctx;
    (void)session_handle;
    (void)command_code;
    if (tpm_rc != NULL)
    {
        *tpm_rc = 0UL;
    }
    return ST33KTPM2X_ENOTSUP;
}

st33ktpm2x_status_t st33ktpm2x_tpm2_policy_or(st33ktpm2x_t *ctx,
                                               uint32_t session_handle,
                                               const uint8_t *digest_list,
                                               uint8_t digest_count,
                                               uint8_t digest_size,
                                               uint32_t *tpm_rc)
{
    (void)ctx;
    (void)session_handle;
    (void)digest_list;
    (void)digest_count;
    (void)digest_size;
    if (tpm_rc != NULL)
    {
        *tpm_rc = 0UL;
    }
    return ST33KTPM2X_ENOTSUP;
}

st33ktpm2x_status_t st33ktpm2x_tpm2_nv_define(st33ktpm2x_t *ctx,
                                              uint32_t nv_index,
                                              uint16_t data_size,
                                              uint32_t attributes,
                                              uint32_t *tpm_rc)
{
    (void)ctx;
    (void)nv_index;
    (void)data_size;
    (void)attributes;
    if (tpm_rc != NULL)
    {
        *tpm_rc = 0UL;
    }
    return ST33KTPM2X_ENOTSUP;
}

st33ktpm2x_status_t st33ktpm2x_tpm2_nv_read(st33ktpm2x_t *ctx,
                                            uint32_t nv_index,
                                            uint16_t offset,
                                            uint8_t *out_data,
                                            uint16_t out_capacity,
                                            uint16_t *out_len,
                                            uint32_t *tpm_rc)
{
    (void)ctx;
    (void)nv_index;
    (void)offset;
    (void)out_data;
    (void)out_capacity;
    if (out_len != NULL)
    {
        *out_len = 0U;
    }
    if (tpm_rc != NULL)
    {
        *tpm_rc = 0UL;
    }
    return ST33KTPM2X_ENOTSUP;
}

st33ktpm2x_status_t st33ktpm2x_tpm2_nv_write(st33ktpm2x_t *ctx,
                                             uint32_t nv_index,
                                             uint16_t offset,
                                             const uint8_t *data,
                                             uint16_t data_len,
                                             uint32_t *tpm_rc)
{
    (void)ctx;
    (void)nv_index;
    (void)offset;
    (void)data;
    (void)data_len;
    if (tpm_rc != NULL)
    {
        *tpm_rc = 0UL;
    }
    return ST33KTPM2X_ENOTSUP;
}

st33ktpm2x_status_t st33ktpm2x_tpm2_flush_context(st33ktpm2x_t *ctx,
                                                  uint32_t handle,
                                                  uint32_t *tpm_rc)
{
    (void)ctx;
    (void)handle;
    if (tpm_rc != NULL)
    {
        *tpm_rc = 0UL;
    }
    return ST33KTPM2X_ENOTSUP;
}
