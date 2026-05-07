/**
 * @file bmp280_main.h
 * @brief RTOS task facade for periodic BMP280 measurements.
 */

#ifndef APP_BMP280_MAIN_H_
#define APP_BMP280_MAIN_H_

#include "bmp280_lib/bmp280_api.h"

#include <stdbool.h>

/**
 * @brief Create the BMP280 sampling task when the sensor is enabled for the board.
 */
void bmp280_main_create_task(void);

/**
 * @brief Read the last cached BMP280 measurement from the task layer.
 * @param out_data [out] Destination for the cached measurement.
 * @return `true` when valid cached data was copied.
 */
bool bmp280_main_get_last(bmp280_api_data_t *out_data);

#endif /* APP_BMP280_MAIN_H_ */
