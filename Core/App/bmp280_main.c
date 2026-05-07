#include "bmp280_main.h"

#include "pager_config.h"

#include <stdio.h>

#if PAGER_CONFIG_BMP280_MOUNTED

#include "app.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "main.h"

#include <string.h>

extern I2C_HandleTypeDef hi2c1;

#define BMP280_TASK_STACK_SIZE   3072U
#define BMP280_TASK_STACK_WORDS  (BMP280_TASK_STACK_SIZE / sizeof(StackType_t))

static osThreadId_t s_bmp280_task = NULL;
static osMutexId_t s_bmp280_data_mutex = NULL;
static bmp280_api_data_t s_bmp280_last;
static bool s_bmp280_has_data = false;
static StaticTask_t s_bmp280_task_cb;
static StackType_t s_bmp280_task_stack[BMP280_TASK_STACK_WORDS];
static StaticSemaphore_t s_bmp280_data_mutex_cb;

static void bmp280_main_task_fn(void *argument);

static const osMutexAttr_t s_bmp280_data_mutex_attr =
{
    .name = "bmp280_data_mutex",
    .cb_mem = &s_bmp280_data_mutex_cb,
    .cb_size = sizeof(s_bmp280_data_mutex_cb)
};

static const osThreadAttr_t s_bmp280_task_attr =
{
    .name = "bmp280_task",
    .priority = (osPriority_t)osPriorityBelowNormal,
    .stack_mem = s_bmp280_task_stack,
    .stack_size = sizeof(s_bmp280_task_stack),
    .cb_mem = &s_bmp280_task_cb,
    .cb_size = sizeof(s_bmp280_task_cb)
};

#else

static bool s_bmp280_not_mounted_logged = false;

#endif

void bmp280_main_create_task(void)
{
#if PAGER_CONFIG_BMP280_MOUNTED
    if (s_bmp280_data_mutex == NULL)
    {
        s_bmp280_data_mutex = osMutexNew(&s_bmp280_data_mutex_attr);
    }

    if (s_bmp280_task == NULL)
    {
        s_bmp280_task = osThreadNew(bmp280_main_task_fn, NULL, &s_bmp280_task_attr);
    }
#else
    if (!s_bmp280_not_mounted_logged)
    {
        printf("BMP280: not mounted, task disabled\r\n");
        s_bmp280_not_mounted_logged = true;
    }
#endif
}

bool bmp280_main_get_last(bmp280_api_data_t *out_data)
{
#if PAGER_CONFIG_BMP280_MOUNTED
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
#else
    (void)out_data;
    return false;
#endif
}

#if PAGER_CONFIG_BMP280_MOUNTED
/** @brief Internal helper: `bmp280_main_task_fn`. */
static void bmp280_main_task_fn(void *argument)
{
    bmp280_api_data_t sample;
    bool sensor_ready;

    (void)argument;

    if (app_i2c_lock(0U))
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
        if (sensor_ready && app_i2c_lock(0U))
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
#endif
