#include "bmp280_api.h"

#include "../app.h"

#include <string.h>

#define BMP280_API_REG_CTRL                0xF4U
#define BMP280_API_REG_STATUS              0xF3U
#define BMP280_API_STATUS_MEASURING_MASK   (1U << 3)
#define BMP280_API_I2C_TIMEOUT_MS          100U
#define BMP280_API_DEFAULT_MEAS_TIMEOUT_MS 100U

/*
 * Single-instance wrapper built on top of the user's existing bmp280.c/.h driver.
 * This keeps the public API small and uses one public data struct only.
 */
static BMP280_HandleTypedef s_bmp280_dev;
static bmp280_api_data_t s_last_data;
static bool s_initialized = false;

static bool bmp280_api_write_reg(uint8_t reg, uint8_t value)
{
    uint16_t device_addr;

    if (!s_initialized || (s_bmp280_dev.i2c == NULL))
    {
        return false;
    }

    device_addr = (uint16_t)(s_bmp280_dev.addr << 1);

    return (app_i2c_mem_write(s_bmp280_dev.i2c,
                              device_addr,
                              reg,
                              I2C_MEMADD_SIZE_8BIT,
                              &value,
                              1,
                              BMP280_API_I2C_TIMEOUT_MS) == HAL_OK);
}

static bool bmp280_api_read_reg(uint8_t reg, uint8_t *value)
{
    uint16_t device_addr;

    if (!s_initialized || (s_bmp280_dev.i2c == NULL) || (value == NULL))
    {
        return false;
    }

    device_addr = (uint16_t)(s_bmp280_dev.addr << 1);

    return (app_i2c_mem_read(s_bmp280_dev.i2c,
                             device_addr,
                             reg,
                             I2C_MEMADD_SIZE_8BIT,
                             value,
                             1,
                             BMP280_API_I2C_TIMEOUT_MS) == HAL_OK);
}

static bool bmp280_api_wait_for_measurement_done(uint32_t timeout_ms)
{
    uint32_t start_tick;
    uint8_t status;

    if (!s_initialized)
    {
        return false;
    }

    if (timeout_ms == 0U)
    {
        timeout_ms = BMP280_API_DEFAULT_MEAS_TIMEOUT_MS;
    }

    start_tick = HAL_GetTick();

    while (1)
    {
        if (!bmp280_api_read_reg(BMP280_API_REG_STATUS, &status))
        {
            return false;
        }

        if ((status & BMP280_API_STATUS_MEASURING_MASK) == 0U)
        {
            return true;
        }

        if ((HAL_GetTick() - start_tick) >= timeout_ms)
        {
            return false;
        }
    }
}

static bool bmp280_api_measure_internal(float *temperature_c, float *pressure_pa, uint32_t timeout_ms)
{
    bool success = false;
    float local_temperature = 0.0f;
    float local_pressure = 0.0f;

    if (!s_initialized)
    {
        return false;
    }

    if (!bmp280_force_measurement(&s_bmp280_dev))
    {
        (void)bmp280_api_sleep();
        return false;
    }

    if (!bmp280_api_wait_for_measurement_done(timeout_ms))
    {
        (void)bmp280_api_sleep();
        return false;
    }

    if (!bmp280_read_float(&s_bmp280_dev, &local_temperature, &local_pressure, NULL))
    {
        (void)bmp280_api_sleep();
        return false;
    }

    s_last_data.temperature_c = local_temperature;
    s_last_data.pressure_pa = local_pressure;
    s_last_data.pressure_hpa = local_pressure / 100.0f;
    s_last_data.timestamp_ms = HAL_GetTick();
    s_last_data.valid = true;

    if (temperature_c != NULL)
    {
        *temperature_c = local_temperature;
    }

    if (pressure_pa != NULL)
    {
        *pressure_pa = local_pressure;
    }

    success = true;

    (void)bmp280_api_sleep();
    return success;
}

bool bmp280_api_init(I2C_HandleTypeDef *hi2c, uint8_t i2c_address)
{
    bmp280_params_t params;

    if ((hi2c == NULL) ||
        ((i2c_address != BMP280_I2C_ADDRESS_0) && (i2c_address != BMP280_I2C_ADDRESS_1)))
    {
        return false;
    }

    s_initialized = false;
    memset(&s_bmp280_dev, 0, sizeof(s_bmp280_dev));
    memset(&s_last_data, 0, sizeof(s_last_data));

    s_bmp280_dev.i2c = hi2c;
    s_bmp280_dev.addr = i2c_address;

    bmp280_init_default_params(&params);

    /*
     * Ultra-precision single-shot profile:
     * - forced mode for explicit one-shot measurements
     * - x1 pressure oversampling
     * - x1 temperature oversampling
     * - min IIR filter
     * - shortest standby (not relevant in forced mode, but kept deterministic)
     */
    params.mode = BMP280_MODE_FORCED;
    params.filter = BMP280_FILTER_OFF;
    params.oversampling_pressure = BMP280_ULTRA_LOW_POWER;
    params.oversampling_temperature = BMP280_ULTRA_LOW_POWER;
    params.standby = BMP280_STANDBY_05;

    if (!bmp280_init(&s_bmp280_dev, &params))
    {
        memset(&s_bmp280_dev, 0, sizeof(s_bmp280_dev));
        memset(&s_last_data, 0, sizeof(s_last_data));
        s_initialized = false;
        return false;
    }

    /* bmp280_init() changes FORCED to SLEEP internally. Cache the final configuration. */
    s_bmp280_dev.params = params;
    s_initialized = true;

    bmp280_api_data_t dummy;
	bmp280_api_measure_all(&dummy, 50);

    /* Explicitly request lowest-power state before returning. */
    (void)bmp280_api_sleep();
    return true;
}

bool bmp280_api_measure_temperature(float *temperature_c, uint32_t timeout_ms)
{
    bool result;

    if (temperature_c == NULL)
    {
        (void)bmp280_api_sleep();
        return false;
    }

    result = bmp280_api_measure_internal(temperature_c, NULL, timeout_ms);
    (void)bmp280_api_sleep();
    return result;
}

bool bmp280_api_measure_pressure(float *pressure_pa, uint32_t timeout_ms)
{
    bool result;

    if (pressure_pa == NULL)
    {
        (void)bmp280_api_sleep();
        return false;
    }

    result = bmp280_api_measure_internal(NULL, pressure_pa, timeout_ms);
    (void)bmp280_api_sleep();
    return result;
}

bool bmp280_api_measure_all(bmp280_api_data_t *data, uint32_t timeout_ms)
{
    bool result;

    if (data == NULL)
    {
        (void)bmp280_api_sleep();
        return false;
    }

    result = bmp280_api_measure_internal(NULL, NULL, timeout_ms);

    if (result)
    {
        *data = s_last_data;
    }

    (void)bmp280_api_sleep();
    return result;
}

bool bmp280_api_sleep(void)
{
    uint8_t ctrl;

    if (!s_initialized)
    {
        return false;
    }

    ctrl = (uint8_t)(((uint8_t)s_bmp280_dev.params.oversampling_temperature << 5) |
                     ((uint8_t)s_bmp280_dev.params.oversampling_pressure << 2) |
                     (uint8_t)BMP280_MODE_SLEEP);

    return bmp280_api_write_reg(BMP280_API_REG_CTRL, ctrl);
}

bool bmp280_api_get_last_data(bmp280_api_data_t *data)
{
    bool result = false;

    if ((data != NULL) && s_last_data.valid)
    {
        *data = s_last_data;
        result = true;
    }

    (void)bmp280_api_sleep();
    return result;
}
