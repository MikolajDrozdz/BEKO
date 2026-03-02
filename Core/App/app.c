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
#include "main.h"

#include <stdio.h>
#include <string.h>

static osMutexId_t s_app_i2c_mutex = NULL;
static osThreadId_t s_app_display_task = NULL;

static void app_display_task_fn(void *argument);

static const osMutexAttr_t s_app_i2c_mutex_attr =
{
    .name = "app_i2c_mutex"
};

static const osThreadAttr_t s_app_display_task_attr =
{
    .name = "app_display",
    .priority = (osPriority_t)osPriorityLow,
    .stack_size = 1024U
};

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
    if (s_app_i2c_mutex == NULL)
    {
        s_app_i2c_mutex = osMutexNew(&s_app_i2c_mutex_attr);
    }

    lcd_main_create_task();
    bmp280_main_create_task();
    tof_main_create_task();
    radio_main_create_task();
    led_array_main_create_task();

    if (s_app_display_task == NULL)
    {
        s_app_display_task = osThreadNew(app_display_task_fn, NULL, &s_app_display_task_attr);
    }
}

bool app_i2c_lock(uint32_t timeout_ms)
{
    uint32_t timeout;

    if (s_app_i2c_mutex == NULL)
    {
        return false;
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

static void app_display_task_fn(void *argument)
{
    char lcd_line0[17];
    char lcd_line1[17];
    bmp280_api_data_t bmp_data;
    int32_t tof_distance_mm;
    bool has_bmp;

    (void)argument;

    lcd_main_set_lines("System booting...", "RTOS tasks start");
    osDelay(500U);

    for (;;)
    {
        memset(lcd_line0, 0, sizeof(lcd_line0));
        memset(lcd_line1, 0, sizeof(lcd_line1));

        has_bmp = bmp280_main_get_last(&bmp_data);
        tof_distance_mm = tof_main_get_last_distance();

        if (has_bmp)
        {
            (void)snprintf(lcd_line0,
                           sizeof(lcd_line0),
                           "T:%4.1fC P:%4.0f",
                           bmp_data.temperature_c,
                           bmp_data.pressure_hpa);
        }
        else
        {
            (void)snprintf(lcd_line0, sizeof(lcd_line0), "BMP280: waiting");
        }

        if (tof_distance_mm >= 0)
        {
            (void)snprintf(lcd_line1,
                           sizeof(lcd_line1),
                           "ToF:%5ld mm\n",
                           (long)tof_distance_mm);
        }
        else
        {
            (void)snprintf(lcd_line1, sizeof(lcd_line1), "ToF: no target");
        }

        lcd_main_set_lines(lcd_line0, lcd_line1);
        osDelay(500U);
    }
}
