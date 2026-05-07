/**
 * @file app.h
 * @brief Main application entry points and shared I2C bus helpers.
 */

#ifndef APP_APP_H_
#define APP_APP_H_

#include <stdbool.h>
#include <stdint.h>
#include "stm32u5xx_hal.h"

/**
 * @brief Initialize application-level peripherals and modules.
 */
void
app_init( void );

/**
 * @brief Run the bare-metal application loop before the RTOS scheduler starts.
 */
void
app_main( void );

/**
 * @brief Create application RTOS tasks and synchronization primitives.
 */
void
app_freertos_init( void );

/**
 * @brief Lock the shared application I2C bus.
 * @param timeout_ms Timeout passed to the RTOS mutex wait.
 * @return `true` when the bus lock was acquired.
 */
bool
app_i2c_lock( uint32_t timeout_ms );

/**
 * @brief Release the shared application I2C bus.
 */
void
app_i2c_unlock( void );

/**
 * @brief Thread-safe wrapper for `HAL_I2C_Master_Transmit`.
 * @param hi2c I2C peripheral handle.
 * @param device_address 8-bit HAL device address.
 * @param data Data buffer to transmit.
 * @param size Number of bytes to transmit.
 * @param timeout_ms Transfer timeout in milliseconds.
 * @return HAL transfer status.
 */
HAL_StatusTypeDef
app_i2c_master_transmit( I2C_HandleTypeDef *hi2c,
                         uint16_t device_address,
                         const uint8_t *data,
                         uint16_t size,
                         uint32_t timeout_ms );

/**
 * @brief Thread-safe wrapper for `HAL_I2C_Master_Receive`.
 * @param hi2c I2C peripheral handle.
 * @param device_address 8-bit HAL device address.
 * @param data Receive buffer.
 * @param size Number of bytes to receive.
 * @param timeout_ms Transfer timeout in milliseconds.
 * @return HAL transfer status.
 */
HAL_StatusTypeDef
app_i2c_master_receive( I2C_HandleTypeDef *hi2c,
                        uint16_t device_address,
                        uint8_t *data,
                        uint16_t size,
                        uint32_t timeout_ms );

/**
 * @brief Thread-safe wrapper for `HAL_I2C_Mem_Write`.
 * @param hi2c I2C peripheral handle.
 * @param device_address 8-bit HAL device address.
 * @param mem_address Memory/register address.
 * @param mem_address_size HAL memory address size selector.
 * @param data Data buffer to write.
 * @param size Number of bytes to write.
 * @param timeout_ms Transfer timeout in milliseconds.
 * @return HAL transfer status.
 */
HAL_StatusTypeDef
app_i2c_mem_write( I2C_HandleTypeDef *hi2c,
                   uint16_t device_address,
                   uint16_t mem_address,
                   uint16_t mem_address_size,
                   const uint8_t *data,
                   uint16_t size,
                   uint32_t timeout_ms );

/**
 * @brief Thread-safe wrapper for `HAL_I2C_Mem_Read`.
 * @param hi2c I2C peripheral handle.
 * @param device_address 8-bit HAL device address.
 * @param mem_address Memory/register address.
 * @param mem_address_size HAL memory address size selector.
 * @param data Receive buffer.
 * @param size Number of bytes to read.
 * @param timeout_ms Transfer timeout in milliseconds.
 * @return HAL transfer status.
 */
HAL_StatusTypeDef
app_i2c_mem_read( I2C_HandleTypeDef *hi2c,
                  uint16_t device_address,
                  uint16_t mem_address,
                  uint16_t mem_address_size,
                  uint8_t *data,
                  uint16_t size,
                  uint32_t timeout_ms );

#endif /* APP_APP_H_ */
