#ifndef BMP280_API_H
#define BMP280_API_H

#include "bmp280.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Cached measurement container.
 * - temperature_c : temperature in degree Celsius
 * - pressure_pa   : pressure in Pascal
 * - pressure_hpa  : pressure in hPa / mbar
 * - timestamp_ms  : HAL_GetTick() timestamp of the last successful measurement
 * - valid         : true after the first successful measurement
 */
typedef struct
{
    float temperature_c;
    float pressure_pa;
    float pressure_hpa;
    uint32_t timestamp_ms;
    bool valid;
} bmp280_api_data_t;

/*
 * 1) Initialize BMP280 for single-shot ultra-precision measurements.
 *    The device is left in sleep mode (lowest power mode) after init.
 *
 *    i2c_address must be BMP280_I2C_ADDRESS_0 (0x76) or BMP280_I2C_ADDRESS_1 (0x77).
 */
bool bmp280_api_init(I2C_HandleTypeDef *hi2c, uint8_t i2c_address);

/*
 * 2) Measure temperature using forced mode.
 *    timeout_ms is checked using HAL_GetTick() / SysTick.
 *    The device is returned to sleep mode before the function exits.
 */
bool bmp280_api_measure_temperature(float *temperature_c, uint32_t timeout_ms);

/*
 * 3) Measure pressure using forced mode.
 *    timeout_ms is checked using HAL_GetTick() / SysTick.
 *    The device is returned to sleep mode before the function exits.
 */
bool bmp280_api_measure_pressure(float *pressure_pa, uint32_t timeout_ms);

/*
 * 4) Measure temperature + pressure and save everything into the provided struct.
 *    timeout_ms is checked using HAL_GetTick() / SysTick.
 *    The device is returned to sleep mode before the function exits.
 */
bool bmp280_api_measure_all(bmp280_api_data_t *data, uint32_t timeout_ms);

/*
 * 5) Force the sensor into sleep mode (lowest power mode).
 */
bool bmp280_api_sleep(void);

/*
 * 6) Read back the last cached measurement data.
 *    The sensor is kept in sleep mode after the function exits.
 */
bool bmp280_api_get_last_data(bmp280_api_data_t *data);

#ifdef __cplusplus
}
#endif

#endif /* BMP280_API_H */
