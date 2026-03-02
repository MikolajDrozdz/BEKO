#include "tof_main.h"

#include "app.h"
#include "cmsis_os2.h"
#include "vl53l3cx_lib/vl53l3cx_lib.h"

#include <stdio.h>

static osThreadId_t s_tof_task = NULL;
static volatile int32_t s_tof_last_distance_mm = -1;

static void tof_main_task_fn(void *argument);

static const osThreadAttr_t s_tof_task_attr =
{
    .name = "tof_task",
    .priority = (osPriority_t)osPriorityBelowNormal,
    .stack_size = 768U
};

void tof_main_create_task(void)
{
    if (s_tof_task == NULL)
    {
        s_tof_task = osThreadNew(tof_main_task_fn, NULL, &s_tof_task_attr);
    }
}

int32_t tof_main_get_last_distance(void)
{
    return s_tof_last_distance_mm;
}

static void tof_main_task_fn(void *argument)
{
    bool init_ok;

    (void)argument;

    if (app_i2c_lock(1000U))
    {
        init_ok = tof_init();
        app_i2c_unlock();
    }
    else
    {
        init_ok = false;
    }

    if (!init_ok)
    {
        printf("ToF task: init failed\r\n");
    }
    else
    {
        printf("ToF task: init OK\r\n");
    }

    for (;;)
    {
        if (init_ok && app_i2c_lock(1000U))
        {
            s_tof_last_distance_mm = tof_get_distance();
            app_i2c_unlock();
        }
        else
        {
            s_tof_last_distance_mm = -1;
        }

        osDelay(300U);
    }
}
