/**
 * @file st33ktpm2x.c
 * @brief ST33KTPM2X driver implementation (TIS over I2C).
 */

#include "st33ktpm2x.h"

#include "app_delay.h"
#include "main.h"

#include <stdio.h>
#include <string.h>

/* I2C TPM register map (locality 0). */
#define ST33_REG_LOC_SEL                0x00U
#define ST33_REG_ACCESS                 0x04U
#define ST33_REG_STS                    0x18U
#define ST33_REG_DATA_FIFO              0x24U
#define ST33_REG_INTERFACE_CAPABILITY   0x30U
#define ST33_REG_DID_VID                0x48U
#define ST33_REG_RID                    0x4CU

#define ST33_I2C_MAX_RETRIES            3U

/* ACCESS bits. */
#define ST33_ACCESS_VALID               0x80U
#define ST33_ACCESS_ACTIVE_LOCALITY     0x20U
#define ST33_ACCESS_REQUEST_USE         0x02U
#define ST33_ACCESS_READ_ZERO           0x48U

/* STS bits. */
#define ST33_STS_VALID                  0x80U
#define ST33_STS_COMMAND_READY          0x40U
#define ST33_STS_TPM_GO                 0x20U
#define ST33_STS_DATA_AVAIL             0x10U
#define ST33_STS_EXPECT                 0x08U
#define ST33_STS_READ_ZERO              0x23U

#define ST33_TPM_HEADER_SIZE            10U
#define ST33_TPM_RH_OWNER               0x40000001UL
#define ST33_TPM_RH_PLATFORM            0x4000000CUL
#define ST33_TPM_RS_PW                  0x40000009UL
#define ST33_TPM_PW_AUTH_SIZE           9U
#define ST33_TPM_RANDOM_MAX_BYTES       256U
#define ST33_TPM_NV_TRANSFER_MAX_BYTES  512U

#if defined(TPM_INIT_LOG)
#define ST33_TRACE(...)                 printf(__VA_ARGS__)
#else
#define ST33_TRACE(...)                 do { if (0) { printf(__VA_ARGS__); } } while (0)
#endif

static uint8_t s_st33_i2c_tx[1U + ST33KTPM2X_CMD_MAX_BYTES];

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

static uint32_t st33_le32_read(const uint8_t *buf)
{
    return ((uint32_t)buf[3] << 24) |
           ((uint32_t)buf[2] << 16) |
           ((uint32_t)buf[1] << 8) |
           (uint32_t)buf[0];
}

static bool st33_identity_is_valid(uint32_t did_vid, uint8_t rid)
{
    if ((did_vid == 0xFFFFFFFFUL) || (did_vid == 0x00000000UL))
    {
        return false;
    }
    if (rid == 0xFFU)
    {
        return false;
    }
    return true;
}

static const char *st33_status_text(st33ktpm2x_status_t rc)
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

static uint16_t st33_write_empty_password_auth(uint8_t *buf)
{
    st33_be32_write(&buf[0], ST33_TPM_RS_PW);
    st33_be16_write(&buf[4], 0U);
    buf[6] = 0U;
    st33_be16_write(&buf[7], 0U);
    return ST33_TPM_PW_AUTH_SIZE;
}

static st33ktpm2x_status_t st33_i2c_read(st33ktpm2x_t *ctx,
                                         uint8_t reg,
                                         uint8_t *data,
                                         uint16_t len)
{
    HAL_StatusTypeDef hal_st = HAL_ERROR;
    uint8_t attempt;

    if ((ctx == NULL) || (data == NULL) || (len == 0U))
    {
        return ST33KTPM2X_EINVAL;
    }

    for (attempt = 0U; attempt < ST33_I2C_MAX_RETRIES; attempt++)
    {
        hal_st = HAL_I2C_Master_Transmit(ctx->cfg.hi2c,
                                         (uint16_t)(ctx->cfg.i2c_addr_7bit << 1),
                                         &reg,
                                         1U,
                                         ctx->cfg.io_timeout_ms);
        if (hal_st == HAL_OK)
        {
            app_delay_ms(1U);
            hal_st = HAL_I2C_Master_Receive(ctx->cfg.hi2c,
                                            (uint16_t)(ctx->cfg.i2c_addr_7bit << 1),
                                            data,
                                            len,
                                            ctx->cfg.io_timeout_ms);
        }
        if (hal_st == HAL_OK)
        {
            app_delay_ms(1U);
            return ST33KTPM2X_OK;
        }
        app_delay_ms(1U);
    }

    if (hal_st == HAL_TIMEOUT)
    {
        return ST33KTPM2X_ETIMEOUT;
    }
    return ST33KTPM2X_EHAL;
}

static st33ktpm2x_status_t st33_i2c_write(st33ktpm2x_t *ctx,
                                          uint8_t reg,
                                          const uint8_t *data,
                                          uint16_t len)
{
    HAL_StatusTypeDef hal_st = HAL_ERROR;
    uint8_t attempt;

    if ((ctx == NULL) || (data == NULL) || (len == 0U))
    {
        return ST33KTPM2X_EINVAL;
    }
    if (len > ST33KTPM2X_CMD_MAX_BYTES)
    {
        return ST33KTPM2X_EOVERFLOW;
    }

    s_st33_i2c_tx[0] = reg;
    memcpy(&s_st33_i2c_tx[1], data, len);

    for (attempt = 0U; attempt < ST33_I2C_MAX_RETRIES; attempt++)
    {
        hal_st = HAL_I2C_Master_Transmit(ctx->cfg.hi2c,
                                         (uint16_t)(ctx->cfg.i2c_addr_7bit << 1),
                                         s_st33_i2c_tx,
                                         (uint16_t)(len + 1U),
                                         ctx->cfg.io_timeout_ms);
        if (hal_st == HAL_OK)
        {
            app_delay_ms(1U);
            return ST33KTPM2X_OK;
        }
        app_delay_ms(1U);
    }

    if (hal_st == HAL_TIMEOUT)
    {
        return ST33KTPM2X_ETIMEOUT;
    }
    return ST33KTPM2X_EHAL;
}

static st33ktpm2x_status_t st33_read_access(st33ktpm2x_t *ctx, uint8_t *access)
{
    uint8_t value = 0U;
    st33ktpm2x_status_t rc;

    if (access == NULL)
    {
        return ST33KTPM2X_EINVAL;
    }

    rc = st33_i2c_read(ctx, ST33_REG_ACCESS, &value, 1U);
    *access = value;
    if (rc != ST33KTPM2X_OK)
    {
        return rc;
    }
    if ((value & ST33_ACCESS_READ_ZERO) != 0U)
    {
        return ST33KTPM2X_EPROTO;
    }
    return ST33KTPM2X_OK;
}

static st33ktpm2x_status_t st33_write_access(st33ktpm2x_t *ctx, uint8_t value)
{
    return st33_i2c_write(ctx, ST33_REG_ACCESS, &value, 1U);
}

static st33ktpm2x_status_t st33_write_loc_sel(st33ktpm2x_t *ctx, uint8_t locality)
{
    return st33_i2c_write(ctx, ST33_REG_LOC_SEL, &locality, 1U);
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
    if ((raw[0] & ST33_STS_READ_ZERO) != 0U)
    {
        return ST33KTPM2X_EPROTO;
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
    uint8_t last_access = 0U;
    st33ktpm2x_status_t rc;
    st33ktpm2x_status_t last_rc = ST33KTPM2X_ETIMEOUT;

    start_ms = HAL_GetTick();
    do
    {
        rc = st33_read_access(ctx, &access);
        last_rc = rc;
        last_access = access;
        if (rc == ST33KTPM2X_OK)
        {
            if ((access & mask) == expected)
            {
                return ST33KTPM2X_OK;
            }
        }
        app_delay_ms(1U);
    } while ((HAL_GetTick() - start_ms) < timeout_ms);

    ST33_TRACE("TPM TIS wait ACCESS timeout mask=0x%02X expected=0x%02X last_rc=%d(%s) last=0x%02X timeout=%lu\r\n",
               (unsigned int)mask,
               (unsigned int)expected,
               (int)last_rc,
               st33_status_text(last_rc),
               (unsigned int)last_access,
               (unsigned long)timeout_ms);
    return ST33KTPM2X_ETIMEOUT;
}

static st33ktpm2x_status_t st33_wait_sts_bits(st33ktpm2x_t *ctx,
                                              uint8_t mask,
                                              uint8_t expected,
                                              uint32_t timeout_ms)
{
    uint32_t start_ms;
    uint8_t sts = 0U;
    uint8_t last_sts = 0U;
    uint16_t last_burst = 0U;
    st33ktpm2x_status_t rc;
    st33ktpm2x_status_t last_rc = ST33KTPM2X_ETIMEOUT;

    start_ms = HAL_GetTick();
    do
    {
        rc = st33_read_sts(ctx, &sts, &last_burst);
        last_rc = rc;
        last_sts = sts;
        if (rc == ST33KTPM2X_OK)
        {
            if ((sts & mask) == expected)
            {
                return ST33KTPM2X_OK;
            }
        }
        app_delay_ms(1U);
    } while ((HAL_GetTick() - start_ms) < timeout_ms);

    ST33_TRACE("TPM TIS wait STS timeout mask=0x%02X expected=0x%02X last_rc=%d(%s) sts=0x%02X burst=%u timeout=%lu\r\n",
               (unsigned int)mask,
               (unsigned int)expected,
               (int)last_rc,
               st33_status_text(last_rc),
               (unsigned int)last_sts,
               (unsigned int)last_burst,
               (unsigned long)timeout_ms);
    return ST33KTPM2X_ETIMEOUT;
}

static st33ktpm2x_status_t st33_wait_burst(st33ktpm2x_t *ctx, uint16_t *burst_out, uint8_t require_data_avail)
{
    uint32_t start_ms;
    uint8_t sts = 0U;
    uint16_t burst = 0U;
    uint8_t last_sts = 0U;
    uint16_t last_burst = 0U;
    st33ktpm2x_status_t rc;
    st33ktpm2x_status_t last_rc = ST33KTPM2X_ETIMEOUT;

    if (burst_out == NULL)
    {
        return ST33KTPM2X_EINVAL;
    }

    start_ms = HAL_GetTick();
    do
    {
        rc = st33_read_sts(ctx, &sts, &burst);
        last_rc = rc;
        last_sts = sts;
        last_burst = burst;
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

    ST33_TRACE("TPM TIS wait burst timeout data_avail=%u last_rc=%d(%s) sts=0x%02X burst=%u timeout=%lu\r\n",
               (unsigned int)require_data_avail,
               (int)last_rc,
               st33_status_text(last_rc),
               (unsigned int)last_sts,
               (unsigned int)last_burst,
               (unsigned long)ctx->cfg.burst_timeout_ms);
    return ST33KTPM2X_ETIMEOUT;
}

static void st33_trace_tis(st33ktpm2x_t *ctx, const char *tag)
{
    uint8_t access = 0U;
    uint8_t sts = 0U;
    uint16_t burst = 0U;
    st33ktpm2x_status_t access_rc;
    st33ktpm2x_status_t sts_rc;

    access_rc = st33_read_access(ctx, &access);
    sts_rc = st33_read_sts(ctx, &sts, &burst);
    ST33_TRACE("TPM TIS %s access_rc=%d(%s) access=0x%02X sts_rc=%d(%s) sts=0x%02X burst=%u loc=%u hal_err=0x%08lX\r\n",
               (tag != NULL) ? tag : "state",
               (int)access_rc,
               st33_status_text(access_rc),
               (unsigned int)access,
               (int)sts_rc,
               st33_status_text(sts_rc),
               (unsigned int)sts,
               (unsigned int)burst,
               ctx->locality0_acquired ? 1U : 0U,
               (unsigned long)HAL_I2C_GetError(ctx->cfg.hi2c));
}

static st33ktpm2x_status_t st33_trace_fail(st33ktpm2x_t *ctx,
                                           const char *stage,
                                           st33ktpm2x_status_t rc,
                                           bool release_locality)
{
    ST33_TRACE("TPM TIS fail stage=%s rc=%d(%s)\r\n",
               (stage != NULL) ? stage : "?",
               (int)rc,
               st33_status_text(rc));
    st33_trace_tis(ctx, stage);
    if (release_locality)
    {
        (void)st33ktpm2x_release_locality0(ctx);
    }
    return rc;
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

    cfg->reset_port = TPM_RESET__GPIO_Port;
    cfg->reset_pin = TPM_RESET__Pin;
    cfg->reset_pulse_ms = 5U;
    cfg->reset_recovery_ms = 100U;

    cfg->davint_port = TPM_DAVINT__GPIO_Port;
    cfg->davint_pin = TPM_DAVINT__Pin;
    cfg->davint_active_state = GPIO_PIN_RESET;

    cfg->pp_port = NULL;
    cfg->pp_pin = 0U;
    cfg->pp_active_state = GPIO_PIN_SET;

    cfg->io_timeout_ms = 100U;
    cfg->locality_timeout_ms = 1000U;
    cfg->burst_timeout_ms = 1000U;
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
    st33ktpm2x_status_t rc;
    uint8_t locality = 0U;

    if ((ctx == NULL) || (!ctx->initialized))
    {
        return ST33KTPM2X_ESTATE;
    }

    HAL_GPIO_WritePin(ctx->cfg.reset_port, ctx->cfg.reset_pin, GPIO_PIN_RESET);
    app_delay_ms(ctx->cfg.reset_pulse_ms);
    HAL_GPIO_WritePin(ctx->cfg.reset_port, ctx->cfg.reset_pin, GPIO_PIN_SET);
    app_delay_ms(ctx->cfg.reset_recovery_ms);
    ctx->locality0_acquired = false;

    rc = st33_write_loc_sel(ctx, locality);
    ST33_TRACE("TPM TIS LOC_SEL after reset rc=%d(%s) locality=%u\r\n",
               (int)rc,
               st33_status_text(rc),
               (unsigned int)locality);
    if (rc == ST33KTPM2X_OK)
    {
        st33_trace_tis(ctx, "after_loc_sel");
    }
    return rc;
}

st33ktpm2x_status_t st33ktpm2x_davint_is_asserted(st33ktpm2x_t *ctx, bool *asserted)
{
    GPIO_PinState state;

    if ((ctx == NULL) || (!ctx->initialized) || (asserted == NULL))
    {
        return ST33KTPM2X_EINVAL;
    }

    if ((ctx->cfg.davint_port == NULL) || (ctx->cfg.davint_pin == 0U))
    {
        return ST33KTPM2X_ENOTSUP;
    }

    state = HAL_GPIO_ReadPin(ctx->cfg.davint_port, ctx->cfg.davint_pin);
    *asserted = (state == ctx->cfg.davint_active_state);
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
    uint8_t locality = 0U;
    st33ktpm2x_status_t rc;

    if ((ctx == NULL) || (!ctx->initialized))
    {
        return ST33KTPM2X_ESTATE;
    }

    if (ctx->locality0_acquired)
    {
        st33_trace_tis(ctx, "request_locality_cached");
        return ST33KTPM2X_OK;
    }

    rc = st33_write_loc_sel(ctx, locality);
    if (rc != ST33KTPM2X_OK)
    {
        return st33_trace_fail(ctx, "write_loc_sel", rc, false);
    }

    rc = st33_read_access(ctx, &access);
    if (rc != ST33KTPM2X_OK)
    {
        return st33_trace_fail(ctx, "read_access_initial", rc, false);
    }
    ST33_TRACE("TPM TIS request locality initial access=0x%02X timeout=%lu\r\n",
               (unsigned int)access,
               (unsigned long)ctx->cfg.locality_timeout_ms);

    if ((access & ST33_ACCESS_ACTIVE_LOCALITY) == 0U)
    {
        rc = st33_write_access(ctx, ST33_ACCESS_REQUEST_USE);
        if (rc != ST33KTPM2X_OK)
        {
            return st33_trace_fail(ctx, "write_request_use", rc, false);
        }
        st33_trace_tis(ctx, "after_request_use");
    }

    rc = st33_wait_access_bits(ctx,
                               (uint8_t)(ST33_ACCESS_VALID | ST33_ACCESS_ACTIVE_LOCALITY),
                               (uint8_t)(ST33_ACCESS_VALID | ST33_ACCESS_ACTIVE_LOCALITY),
                               ctx->cfg.locality_timeout_ms);
    if (rc == ST33KTPM2X_OK)
    {
        ctx->locality0_acquired = true;
        st33_trace_tis(ctx, "locality_acquired");
    }
    else
    {
        (void)st33_trace_fail(ctx, "locality_timeout", rc, false);
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
    ST33_TRACE("TPM TIS release locality rc=%d(%s)\r\n",
               (int)rc,
               st33_status_text(rc));
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

    *did_vid = st33_le32_read(id_raw);
    rc = st33_i2c_read(ctx, ST33_REG_RID, rid, 1U);
    if (rc != ST33KTPM2X_OK)
    {
        return rc;
    }

    if (!st33_identity_is_valid(*did_vid, *rid))
    {
        return ST33KTPM2X_EPROTO;
    }

    return ST33KTPM2X_OK;
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
    uint32_t command_code = 0UL;
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

    command_code = st33_be32_read(&command[6]);
    ST33_TRACE("TPM TIS transceive start cc=0x%08lX cmd_len=%u rsp_cap=%u\r\n",
               (unsigned long)command_code,
               (unsigned int)command_len,
               (unsigned int)response_capacity);

    rc = st33ktpm2x_request_locality0(ctx);
    if (rc != ST33KTPM2X_OK)
    {
        return st33_trace_fail(ctx, "request_locality", rc, false);
    }
    st33_trace_tis(ctx, "transceive_after_locality");

    rc = st33_write_sts(ctx, ST33_STS_COMMAND_READY);
    if (rc != ST33KTPM2X_OK)
    {
        return st33_trace_fail(ctx, "write_command_ready", rc, true);
    }
    st33_trace_tis(ctx, "after_write_command_ready");

    rc = st33_wait_sts_bits(ctx,
                            (uint8_t)(ST33_STS_VALID | ST33_STS_COMMAND_READY),
                            (uint8_t)(ST33_STS_VALID | ST33_STS_COMMAND_READY),
                            ctx->cfg.burst_timeout_ms);
    if (rc != ST33KTPM2X_OK)
    {
        return st33_trace_fail(ctx, "wait_command_ready", rc, true);
    }
    st33_trace_tis(ctx, "command_ready");

    while (sent < command_len)
    {
        rc = st33_wait_burst(ctx, &burst, 0U);
        if (rc != ST33KTPM2X_OK)
        {
            return st33_trace_fail(ctx, "wait_write_burst", rc, true);
        }

        chunk = st33_min_u16((uint16_t)(command_len - sent), burst);
        ST33_TRACE("TPM TIS write FIFO chunk=%u sent_before=%u burst=%u\r\n",
                   (unsigned int)chunk,
                   (unsigned int)sent,
                   (unsigned int)burst);
        rc = st33_fifo_write(ctx, &command[sent], chunk);
        if (rc != ST33KTPM2X_OK)
        {
            return st33_trace_fail(ctx, "fifo_write", rc, true);
        }

        sent = (uint16_t)(sent + chunk);
        rc = st33_read_sts(ctx, &sts, NULL);
        if (rc != ST33KTPM2X_OK)
        {
            return st33_trace_fail(ctx, "read_sts_after_fifo_write", rc, true);
        }
        ST33_TRACE("TPM TIS after FIFO sent=%u/%u sts=0x%02X expect=%u\r\n",
                   (unsigned int)sent,
                   (unsigned int)command_len,
                   (unsigned int)sts,
                   ((sts & ST33_STS_EXPECT) != 0U) ? 1U : 0U);
        if ((sent < command_len) && ((sts & ST33_STS_EXPECT) == 0U))
        {
            return st33_trace_fail(ctx, "expect_cleared_before_command_end", ST33KTPM2X_EPROTO, true);
        }
    }

    rc = st33_write_sts(ctx, ST33_STS_TPM_GO);
    if (rc != ST33KTPM2X_OK)
    {
        return st33_trace_fail(ctx, "write_tpm_go", rc, true);
    }
    st33_trace_tis(ctx, "after_tpm_go");

    rc = st33_wait_sts_bits(ctx,
                            (uint8_t)(ST33_STS_VALID | ST33_STS_DATA_AVAIL),
                            (uint8_t)(ST33_STS_VALID | ST33_STS_DATA_AVAIL),
                            ctx->cfg.burst_timeout_ms);
    if (rc != ST33KTPM2X_OK)
    {
        return st33_trace_fail(ctx, "wait_data_avail", rc, true);
    }
    st33_trace_tis(ctx, "data_avail");

    rc = st33_read_response_stream(ctx, response, ST33_TPM_HEADER_SIZE);
    if (rc != ST33KTPM2X_OK)
    {
        return st33_trace_fail(ctx, "read_response_header", rc, true);
    }

    rsp_total = (uint16_t)st33_be32_read(&response[2]);
    ST33_TRACE("TPM TIS response header cc=0x%08lX total=%u tpm_rc=0x%08lX\r\n",
               (unsigned long)command_code,
               (unsigned int)rsp_total,
               (unsigned long)st33_be32_read(&response[6]));
    if ((rsp_total < ST33_TPM_HEADER_SIZE) || (rsp_total > ST33KTPM2X_RSP_MAX_BYTES))
    {
        (void)st33_write_sts(ctx, ST33_STS_COMMAND_READY);
        return st33_trace_fail(ctx, "response_size_invalid", ST33KTPM2X_EPROTO, true);
    }
    if (rsp_total > response_capacity)
    {
        (void)st33_write_sts(ctx, ST33_STS_COMMAND_READY);
        return st33_trace_fail(ctx, "response_capacity", ST33KTPM2X_EOVERFLOW, true);
    }

    if (rsp_total > ST33_TPM_HEADER_SIZE)
    {
        rc = st33_read_response_stream(ctx,
                                       &response[ST33_TPM_HEADER_SIZE],
                                       (uint16_t)(rsp_total - ST33_TPM_HEADER_SIZE));
        if (rc != ST33KTPM2X_OK)
        {
            (void)st33_write_sts(ctx, ST33_STS_COMMAND_READY);
            return st33_trace_fail(ctx, "read_response_body", rc, true);
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
        ST33_TRACE("TPM TIS command TPM_RC cc=0x%08lX tpm_rc=0x%08lX\r\n",
                   (unsigned long)command_code,
                   (unsigned long)*tpm_rc);
        return ST33KTPM2X_ETPM_RC;
    }

    ST33_TRACE("TPM TIS transceive OK cc=0x%08lX rsp_len=%u\r\n",
               (unsigned long)command_code,
               (unsigned int)*response_len);
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
    uint8_t rsp[ST33_TPM_HEADER_SIZE + 2U + ST33_TPM_RANDOM_MAX_BYTES];
    uint16_t rsp_len = 0U;
    uint16_t rnd_len = 0U;
    st33ktpm2x_status_t rc;

    if ((random_out == NULL) ||
        (random_len == NULL) ||
        (requested_bytes == 0U) ||
        (requested_bytes > ST33_TPM_RANDOM_MAX_BYTES) ||
        (random_capacity < requested_bytes))
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
    uint8_t cmd[45];
    uint8_t rsp[ST33_TPM_HEADER_SIZE + 4U + ST33_TPM_PW_AUTH_SIZE];
    uint16_t off = 0U;
    uint16_t rsp_len = 0U;

    if ((ctx == NULL) || (!ctx->initialized) || (data_size == 0U))
    {
        return ST33KTPM2X_EINVAL;
    }

    st33_be16_write(&cmd[off], ST33KTPM2X_TPM2_ST_SESSIONS);
    off = (uint16_t)(off + 2U);
    st33_be32_write(&cmd[off], (uint32_t)sizeof(cmd));
    off = (uint16_t)(off + 4U);
    st33_be32_write(&cmd[off], ST33KTPM2X_TPM2_CC_NV_DEFINE_SPACE);
    off = (uint16_t)(off + 4U);

    st33_be32_write(&cmd[off], ST33_TPM_RH_OWNER);
    off = (uint16_t)(off + 4U);

    st33_be32_write(&cmd[off], ST33_TPM_PW_AUTH_SIZE);
    off = (uint16_t)(off + 4U);
    off = (uint16_t)(off + st33_write_empty_password_auth(&cmd[off]));

    st33_be16_write(&cmd[off], 0U); /* TPM2B_AUTH userAuth */
    off = (uint16_t)(off + 2U);

    st33_be16_write(&cmd[off], 14U); /* TPM2B_NV_PUBLIC.size */
    off = (uint16_t)(off + 2U);
    st33_be32_write(&cmd[off], nv_index);
    off = (uint16_t)(off + 4U);
    st33_be16_write(&cmd[off], ST33KTPM2X_TPM2_ALG_SHA256);
    off = (uint16_t)(off + 2U);
    st33_be32_write(&cmd[off], attributes);
    off = (uint16_t)(off + 4U);
    st33_be16_write(&cmd[off], 0U); /* TPM2B_DIGEST authPolicy */
    off = (uint16_t)(off + 2U);
    st33_be16_write(&cmd[off], data_size);
    off = (uint16_t)(off + 2U);

    if (off != sizeof(cmd))
    {
        return ST33KTPM2X_EPROTO;
    }

    return st33ktpm2x_transceive(ctx,
                                 cmd,
                                 (uint16_t)sizeof(cmd),
                                 rsp,
                                 (uint16_t)sizeof(rsp),
                                 &rsp_len,
                                 tpm_rc);
}

st33ktpm2x_status_t st33ktpm2x_tpm2_nv_read(st33ktpm2x_t *ctx,
                                            uint32_t nv_index,
                                            uint16_t offset,
                                            uint8_t *out_data,
                                            uint16_t out_capacity,
                                            uint16_t *out_len,
                                            uint32_t *tpm_rc)
{
    uint8_t cmd[35];
    uint8_t rsp[ST33_TPM_HEADER_SIZE + 4U + 2U + ST33_TPM_NV_TRANSFER_MAX_BYTES + ST33_TPM_PW_AUTH_SIZE];
    uint16_t cmd_off = 0U;
    uint16_t rsp_off = ST33_TPM_HEADER_SIZE;
    uint16_t rsp_len = 0U;
    uint16_t data_len = 0U;
    st33ktpm2x_status_t rc;

    if ((ctx == NULL) ||
        (!ctx->initialized) ||
        (out_data == NULL) ||
        (out_len == NULL) ||
        (out_capacity == 0U) ||
        (out_capacity > ST33_TPM_NV_TRANSFER_MAX_BYTES))
    {
        return ST33KTPM2X_EINVAL;
    }

    *out_len = 0U;

    st33_be16_write(&cmd[cmd_off], ST33KTPM2X_TPM2_ST_SESSIONS);
    cmd_off = (uint16_t)(cmd_off + 2U);
    st33_be32_write(&cmd[cmd_off], (uint32_t)sizeof(cmd));
    cmd_off = (uint16_t)(cmd_off + 4U);
    st33_be32_write(&cmd[cmd_off], ST33KTPM2X_TPM2_CC_NV_READ);
    cmd_off = (uint16_t)(cmd_off + 4U);

    st33_be32_write(&cmd[cmd_off], ST33_TPM_RH_OWNER);
    cmd_off = (uint16_t)(cmd_off + 4U);
    st33_be32_write(&cmd[cmd_off], nv_index);
    cmd_off = (uint16_t)(cmd_off + 4U);

    st33_be32_write(&cmd[cmd_off], ST33_TPM_PW_AUTH_SIZE);
    cmd_off = (uint16_t)(cmd_off + 4U);
    cmd_off = (uint16_t)(cmd_off + st33_write_empty_password_auth(&cmd[cmd_off]));

    st33_be16_write(&cmd[cmd_off], out_capacity);
    cmd_off = (uint16_t)(cmd_off + 2U);
    st33_be16_write(&cmd[cmd_off], offset);
    cmd_off = (uint16_t)(cmd_off + 2U);

    if (cmd_off != sizeof(cmd))
    {
        return ST33KTPM2X_EPROTO;
    }

    rc = st33ktpm2x_transceive(ctx,
                               cmd,
                               (uint16_t)sizeof(cmd),
                               rsp,
                               (uint16_t)sizeof(rsp),
                               &rsp_len,
                               tpm_rc);
    if (rc != ST33KTPM2X_OK)
    {
        return rc;
    }

    if (rsp_len < (ST33_TPM_HEADER_SIZE + 4U + 2U))
    {
        return ST33KTPM2X_EPROTO;
    }

    rsp_off = (uint16_t)(rsp_off + 4U); /* parameterSize */
    data_len = st33_be16_read(&rsp[rsp_off]);
    rsp_off = (uint16_t)(rsp_off + 2U);
    if ((uint16_t)(rsp_off + data_len) > rsp_len)
    {
        return ST33KTPM2X_EPROTO;
    }
    if (data_len > out_capacity)
    {
        return ST33KTPM2X_EOVERFLOW;
    }

    memcpy(out_data, &rsp[rsp_off], data_len);
    *out_len = data_len;
    return ST33KTPM2X_OK;
}

static st33ktpm2x_status_t st33_tpm2_nv_write_auth(st33ktpm2x_t *ctx,
                                                   uint32_t auth_handle,
                                                   uint32_t nv_index,
                                                   uint16_t offset,
                                                   const uint8_t *data,
                                                   uint16_t data_len,
                                                   uint32_t *tpm_rc)
{
    uint8_t cmd[35U + ST33_TPM_NV_TRANSFER_MAX_BYTES];
    uint8_t rsp[ST33_TPM_HEADER_SIZE + 4U + ST33_TPM_PW_AUTH_SIZE];
    uint16_t off = 0U;
    uint16_t rsp_len = 0U;
    uint16_t cmd_len;

    if ((ctx == NULL) ||
        (!ctx->initialized) ||
        ((data == NULL) && (data_len > 0U)) ||
        (data_len > ST33_TPM_NV_TRANSFER_MAX_BYTES))
    {
        return ST33KTPM2X_EINVAL;
    }

    cmd_len = (uint16_t)(35U + data_len);

    st33_be16_write(&cmd[off], ST33KTPM2X_TPM2_ST_SESSIONS);
    off = (uint16_t)(off + 2U);
    st33_be32_write(&cmd[off], cmd_len);
    off = (uint16_t)(off + 4U);
    st33_be32_write(&cmd[off], ST33KTPM2X_TPM2_CC_NV_WRITE);
    off = (uint16_t)(off + 4U);

    st33_be32_write(&cmd[off], auth_handle);
    off = (uint16_t)(off + 4U);
    st33_be32_write(&cmd[off], nv_index);
    off = (uint16_t)(off + 4U);

    st33_be32_write(&cmd[off], ST33_TPM_PW_AUTH_SIZE);
    off = (uint16_t)(off + 4U);
    off = (uint16_t)(off + st33_write_empty_password_auth(&cmd[off]));

    st33_be16_write(&cmd[off], data_len);
    off = (uint16_t)(off + 2U);
    if (data_len > 0U)
    {
        memcpy(&cmd[off], data, data_len);
        off = (uint16_t)(off + data_len);
    }
    st33_be16_write(&cmd[off], offset);
    off = (uint16_t)(off + 2U);

    if (off != cmd_len)
    {
        return ST33KTPM2X_EPROTO;
    }

    return st33ktpm2x_transceive(ctx,
                                 cmd,
                                 cmd_len,
                                 rsp,
                                 (uint16_t)sizeof(rsp),
                                 &rsp_len,
                                 tpm_rc);
}

st33ktpm2x_status_t st33ktpm2x_tpm2_nv_write(st33ktpm2x_t *ctx,
                                             uint32_t nv_index,
                                             uint16_t offset,
                                             const uint8_t *data,
                                             uint16_t data_len,
                                             uint32_t *tpm_rc)
{
    return st33_tpm2_nv_write_auth(ctx,
                                   ST33_TPM_RH_OWNER,
                                   nv_index,
                                   offset,
                                   data,
                                   data_len,
                                   tpm_rc);
}

st33ktpm2x_status_t st33ktpm2x_tpm2_nv_write_platform_pp(st33ktpm2x_t *ctx,
                                                         uint32_t nv_index,
                                                         uint16_t offset,
                                                         const uint8_t *data,
                                                         uint16_t data_len,
                                                         uint32_t *tpm_rc)
{
    return st33_tpm2_nv_write_auth(ctx,
                                   ST33_TPM_RH_PLATFORM,
                                   nv_index,
                                   offset,
                                   data,
                                   data_len,
                                   tpm_rc);
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
