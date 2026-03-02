/*
 * app.c
 *
 * FreeRTOS-style application orchestrator (App-level "app_freertos").
 */

#include "app.h"

#include "bmp280_main.h"
#include "lcd_main.h"
#include "led_array_main.h"
#include "radio_main.h"
#include "tof_main.h"

#include "app_delay.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "main.h"

#include <stdio.h>
#include <string.h>

#define APP_I2C_DEFAULT_TIMEOUT_MS  100U
#define APP_I2C_RETRY_COUNT         3U
#define APP_I2C_RETRY_DELAY_MS      1U

static osMutexId_t s_app_i2c_mutex = NULL;
static StaticSemaphore_t s_app_i2c_mutex_cb;

static const osMutexAttr_t s_app_i2c_mutex_attr =
{
    .name = "app_i2c_mutex",
    .attr_bits = osMutexRecursive | osMutexPrioInherit,
    .cb_mem = &s_app_i2c_mutex_cb,
    .cb_size = sizeof(s_app_i2c_mutex_cb)
};

static bool app_i2c_ensure_mutex(void);
static uint32_t app_i2c_normalize_timeout(uint32_t timeout_ms);
static bool app_i2c_should_recover(I2C_HandleTypeDef *hi2c, HAL_StatusTypeDef status);
static bool app_i2c_recover_locked(I2C_HandleTypeDef *hi2c);

void app_init(void)
{
    printf("APP: FreeRTOS bootstrap\r\n");
}

void app_main(void)
{
    /* Fallback path; should not execute in normal RTOS flow. */
    app_delay_ms(100U);
}

void app_freertos_init(void)
{
    (void)app_i2c_ensure_mutex();

    lcd_main_create_task();
    bmp280_main_create_task();
    tof_main_create_task();
    radio_main_create_task();
    led_array_main_create_task();
}

bool app_i2c_lock(uint32_t timeout_ms)
{
    uint32_t timeout;
    osKernelState_t kernel_state;

    if (!app_i2c_ensure_mutex())
    {
        kernel_state = osKernelGetState();
        return (kernel_state == osKernelInactive);
    }

    timeout = (timeout_ms == 0U) ? osWaitForever : timeout_ms;
    return (osMutexAcquire(s_app_i2c_mutex, timeout) == osOK);
}

void app_i2c_unlock(void)
{
    if (s_app_i2c_mutex != NULL)
    {
        (void)osMutexRelease(s_app_i2c_mutex);
    }
}

HAL_StatusTypeDef app_i2c_master_transmit(I2C_HandleTypeDef *hi2c,
                                          uint16_t device_address,
                                          const uint8_t *data,
                                          uint16_t size,
                                          uint32_t timeout_ms)
{
    HAL_StatusTypeDef status;
    uint32_t attempt;
    uint8_t *tx_data;

    if ((hi2c == NULL) || ((data == NULL) && (size > 0U)))
    {
        return HAL_ERROR;
    }
    if (!app_i2c_lock(0U))
    {
        return HAL_TIMEOUT;
    }

    tx_data = (uint8_t *)data;
    status = HAL_ERROR;

    for (attempt = 0U; attempt < APP_I2C_RETRY_COUNT; attempt++)
    {
        status = HAL_I2C_Master_Transmit(hi2c,
                                         device_address,
                                         tx_data,
                                         size,
                                         app_i2c_normalize_timeout(timeout_ms));
        if (status == HAL_OK)
        {
            break;
        }

        if (app_i2c_should_recover(hi2c, status))
        {
            (void)app_i2c_recover_locked(hi2c);
        }

        if ((attempt + 1U) < APP_I2C_RETRY_COUNT)
        {
            app_delay_ms(APP_I2C_RETRY_DELAY_MS);
        }
    }

    app_i2c_unlock();
    return status;
}

HAL_StatusTypeDef app_i2c_master_receive(I2C_HandleTypeDef *hi2c,
                                         uint16_t device_address,
                                         uint8_t *data,
                                         uint16_t size,
                                         uint32_t timeout_ms)
{
    HAL_StatusTypeDef status;
    uint32_t attempt;

    if ((hi2c == NULL) || ((data == NULL) && (size > 0U)))
    {
        return HAL_ERROR;
    }
    if (!app_i2c_lock(0U))
    {
        return HAL_TIMEOUT;
    }

    status = HAL_ERROR;

    for (attempt = 0U; attempt < APP_I2C_RETRY_COUNT; attempt++)
    {
        status = HAL_I2C_Master_Receive(hi2c,
                                        device_address,
                                        data,
                                        size,
                                        app_i2c_normalize_timeout(timeout_ms));
        if (status == HAL_OK)
        {
            break;
        }

        if (app_i2c_should_recover(hi2c, status))
        {
            (void)app_i2c_recover_locked(hi2c);
        }

        if ((attempt + 1U) < APP_I2C_RETRY_COUNT)
        {
            app_delay_ms(APP_I2C_RETRY_DELAY_MS);
        }
    }

    app_i2c_unlock();
    return status;
}

HAL_StatusTypeDef app_i2c_mem_write(I2C_HandleTypeDef *hi2c,
                                    uint16_t device_address,
                                    uint16_t mem_address,
                                    uint16_t mem_address_size,
                                    const uint8_t *data,
                                    uint16_t size,
                                    uint32_t timeout_ms)
{
    HAL_StatusTypeDef status;
    uint32_t attempt;
    uint8_t *tx_data;

    if ((hi2c == NULL) || ((data == NULL) && (size > 0U)))
    {
        return HAL_ERROR;
    }
    if (!app_i2c_lock(0U))
    {
        return HAL_TIMEOUT;
    }

    tx_data = (uint8_t *)data;
    status = HAL_ERROR;

    for (attempt = 0U; attempt < APP_I2C_RETRY_COUNT; attempt++)
    {
        status = HAL_I2C_Mem_Write(hi2c,
                                   device_address,
                                   mem_address,
                                   mem_address_size,
                                   tx_data,
                                   size,
                                   app_i2c_normalize_timeout(timeout_ms));
        if (status == HAL_OK)
        {
            break;
        }

        if (app_i2c_should_recover(hi2c, status))
        {
            (void)app_i2c_recover_locked(hi2c);
        }

        if ((attempt + 1U) < APP_I2C_RETRY_COUNT)
        {
            app_delay_ms(APP_I2C_RETRY_DELAY_MS);
        }
    }

    app_i2c_unlock();
    return status;
}

HAL_StatusTypeDef app_i2c_mem_read(I2C_HandleTypeDef *hi2c,
                                   uint16_t device_address,
                                   uint16_t mem_address,
                                   uint16_t mem_address_size,
                                   uint8_t *data,
                                   uint16_t size,
                                   uint32_t timeout_ms)
{
    HAL_StatusTypeDef status;
    uint32_t attempt;

    if ((hi2c == NULL) || ((data == NULL) && (size > 0U)))
    {
        return HAL_ERROR;
    }
    if (!app_i2c_lock(0U))
    {
        return HAL_TIMEOUT;
    }

    status = HAL_ERROR;

    for (attempt = 0U; attempt < APP_I2C_RETRY_COUNT; attempt++)
    {
        status = HAL_I2C_Mem_Read(hi2c,
                                  device_address,
                                  mem_address,
                                  mem_address_size,
                                  data,
                                  size,
                                  app_i2c_normalize_timeout(timeout_ms));
        if (status == HAL_OK)
        {
            break;
        }

        if (app_i2c_should_recover(hi2c, status))
        {
            (void)app_i2c_recover_locked(hi2c);
        }

        if ((attempt + 1U) < APP_I2C_RETRY_COUNT)
        {
            app_delay_ms(APP_I2C_RETRY_DELAY_MS);
        }
    }

    app_i2c_unlock();
    return status;
}

static bool app_i2c_ensure_mutex(void)
{
    osKernelState_t kernel_state;

    if (s_app_i2c_mutex != NULL)
    {
        return true;
    }

    kernel_state = osKernelGetState();
    if ((kernel_state == osKernelReady) || (kernel_state == osKernelRunning))
    {
        s_app_i2c_mutex = osMutexNew(&s_app_i2c_mutex_attr);
    }

    return (s_app_i2c_mutex != NULL);
}

static uint32_t app_i2c_normalize_timeout(uint32_t timeout_ms)
{
    if (timeout_ms == 0U)
    {
        return APP_I2C_DEFAULT_TIMEOUT_MS;
    }

    return timeout_ms;
}

static bool app_i2c_should_recover(I2C_HandleTypeDef *hi2c, HAL_StatusTypeDef status)
{
    uint32_t error_flags;

    if ((hi2c == NULL) || (status == HAL_OK))
    {
        return false;
    }

    if ((status == HAL_BUSY) || (status == HAL_TIMEOUT))
    {
        return true;
    }

    error_flags = HAL_I2C_GetError(hi2c);

    return ((error_flags & (HAL_I2C_ERROR_BERR |
                            HAL_I2C_ERROR_ARLO |
                            HAL_I2C_ERROR_OVR |
                            HAL_I2C_ERROR_DMA |
                            HAL_I2C_ERROR_TIMEOUT |
                            HAL_I2C_ERROR_SIZE |
                            HAL_I2C_ERROR_DMA_PARAM)) != 0U);
}

static bool app_i2c_recover_locked(I2C_HandleTypeDef *hi2c)
{
    if (hi2c == NULL)
    {
        return false;
    }

    (void)HAL_I2C_DeInit(hi2c);

    if (HAL_I2C_Init(hi2c) != HAL_OK)
    {
        return false;
    }
    if (HAL_I2CEx_ConfigAnalogFilter(hi2c, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
    {
        return false;
    }
    if (HAL_I2CEx_ConfigDigitalFilter(hi2c, 0U) != HAL_OK)
    {
        return false;
    }

    return true;
}

