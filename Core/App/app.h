/*
 * app.h
 *
 *  Created on: Feb 28, 2026
 *      Author: mikol
 */

#ifndef APP_APP_H_
#define APP_APP_H_

#include <stdbool.h>
#include <stdint.h>
#include "stm32u5xx_hal.h"

void
app_init( void );

void
app_main( void );

void
app_freertos_init( void );

bool
app_i2c_lock( uint32_t timeout_ms );

void
app_i2c_unlock( void );

HAL_StatusTypeDef
app_i2c_master_transmit( I2C_HandleTypeDef *hi2c,
                         uint16_t device_address,
                         const uint8_t *data,
                         uint16_t size,
                         uint32_t timeout_ms );

HAL_StatusTypeDef
app_i2c_master_receive( I2C_HandleTypeDef *hi2c,
                        uint16_t device_address,
                        uint8_t *data,
                        uint16_t size,
                        uint32_t timeout_ms );

HAL_StatusTypeDef
app_i2c_mem_write( I2C_HandleTypeDef *hi2c,
                   uint16_t device_address,
                   uint16_t mem_address,
                   uint16_t mem_address_size,
                   const uint8_t *data,
                   uint16_t size,
                   uint32_t timeout_ms );

HAL_StatusTypeDef
app_i2c_mem_read( I2C_HandleTypeDef *hi2c,
                  uint16_t device_address,
                  uint16_t mem_address,
                  uint16_t mem_address_size,
                  uint8_t *data,
                  uint16_t size,
                  uint32_t timeout_ms );

#endif /* APP_APP_H_ */
