/**
 * @file bmp280_api.h
 * @brief Single-instance BMP280 measurement wrapper used by the application.
 */

#ifndef BMP280_API_H
#define BMP280_API_H

#include "bmp280.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Cached measurement container.
 */
typedef struct
{
    float temperature_c; /**< Temperature in degrees Celsius. */
    float pressure_pa; /**< Pressure in Pascals. */
    float pressure_hpa; /**< Pressure in hPa/mbar. */
    uint32_t timestamp_ms; /**< `HAL_GetTick()` timestamp of the last successful measurement. */
    bool valid; /**< Set after the first successful measurement. */
} bmp280_api_data_t;

/**
 * @brief Initialize BMP280 for single-shot measurements.
 *
 * The device is left in sleep mode after initialization.
 *
 * @param hi2c I2C peripheral handle.
 * @param i2c_address `BMP280_I2C_ADDRESS_0` (0x76) or `BMP280_I2C_ADDRESS_1` (0x77).
 * @return `true` when the sensor was initialized.
 */
bool bmp280_api_init(I2C_HandleTypeDef *hi2c, uint8_t i2c_address);

/**
 * @brief Measure temperature using forced mode.
 * @param temperature_c [out] Temperature in degrees Celsius.
 * @param timeout_ms Measurement timeout checked with `HAL_GetTick()`.
 * @return `true` when a fresh measurement was read.
 */
bool bmp280_api_measure_temperature(float *temperature_c, uint32_t timeout_ms);

/**
 * @brief Measure pressure using forced mode.
 * @param pressure_pa [out] Pressure in Pascals.
 * @param timeout_ms Measurement timeout checked with `HAL_GetTick()`.
 * @return `true` when a fresh measurement was read.
 */
bool bmp280_api_measure_pressure(float *pressure_pa, uint32_t timeout_ms);

/**
 * @brief Measure temperature and pressure using forced mode.
 * @param data [out] Measurement container.
 * @param timeout_ms Measurement timeout checked with `HAL_GetTick()`.
 * @return `true` when a fresh measurement was read.
 */
bool bmp280_api_measure_all(bmp280_api_data_t *data, uint32_t timeout_ms);

/**
 * @brief Force the sensor into sleep mode.
 * @return `true` when the sleep command was written.
 */
bool bmp280_api_sleep(void);

/**
 * @brief Read back the last cached measurement data.
 * @param data [out] Cached measurement container.
 * @return `true` when valid cached data was copied.
 */
bool bmp280_api_get_last_data(bmp280_api_data_t *data);

#ifdef __cplusplus
}
#endif

#endif /* BMP280_API_H */
