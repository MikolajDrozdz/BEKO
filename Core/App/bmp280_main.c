#include "bmp280_main.h"

#include "app.h"
#include "cmsis_os2.h"
#include "main.h"

#include <stdio.h>
#include <string.h>

extern I2C_HandleTypeDef hi2c1;

static osThreadId_t s_bmp280_task = NULL;
static osMutexId_t s_bmp280_data_mutex = NULL;
static bmp280_api_data_t s_bmp280_last;
static bool s_bmp280_has_data = false;

static void bmp280_main_task_fn(void *argument);

static const osMutexAttr_t s_bmp280_data_mutex_attr =
{
    .name = "bmp280_data_mutex"
};

static const osThreadAttr_t s_bmp280_task_attr =
{
    .name = "bmp280_task",
    .priority = (osPriority_t)osPriorityBelowNormal,
    .stack_size = 768U
};

void bmp280_main_create_task(void)
{
    if (s_bmp280_data_mutex == NULL)
    {
        s_bmp280_data_mutex = osMutexNew(&s_bmp280_data_mutex_attr);
    }

    if (s_bmp280_task == NULL)
    {
        s_bmp280_task = osThreadNew(bmp280_main_task_fn, NULL, &s_bmp280_task_attr);
    }
}

bool bmp280_main_get_last(bmp280_api_data_t *out_data)
{
    bool has_data;

    if ((out_data == NULL) || (s_bmp280_data_mutex == NULL))
    {
        return false;
    }

    if (osMutexAcquire(s_bmp280_data_mutex, 50U) != osOK)
    {
        return false;
    }

    has_data = s_bmp280_has_data;
    if (has_data)
    {
        *out_data = s_bmp280_last;
    }

    (void)osMutexRelease(s_bmp280_data_mutex);
    return has_data;
}

static void bmp280_main_task_fn(void *argument)
{
    bmp280_api_data_t sample;
    bool sensor_ready;

    (void)argument;

    if (app_i2c_lock(500U))
    {
        sensor_ready = bmp280_api_init(&hi2c1, BMP280_I2C_ADDRESS_1);
        app_i2c_unlock();
    }
    else
    {
        sensor_ready = false;
    }

    if (!sensor_ready)
    {
        printf("BMP280 task: init failed\r\n");
    }
    else
    {
        printf("BMP280 task: init OK\r\n");
    }

    memset(&sample, 0, sizeof(sample));

    for (;;)
    {
        if (sensor_ready && app_i2c_lock(500U))
        {
            if (bmp280_api_measure_all(&sample, 80U))
            {
                if (osMutexAcquire(s_bmp280_data_mutex, 50U) == osOK)
                {
                    s_bmp280_last = sample;
                    s_bmp280_has_data = true;
                    (void)osMutexRelease(s_bmp280_data_mutex);
                }
            }
            app_i2c_unlock();
        }

        osDelay(1000U);
    }
}
