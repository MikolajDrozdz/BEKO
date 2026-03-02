#include "led_array_main.h"

#include "cmsis_os2.h"
#include "led_array_lib/led_array_lib.h"

#include <stdio.h>

static osThreadId_t s_led_array_task = NULL;

static void led_array_main_task_fn(void *argument);

static const osThreadAttr_t s_led_array_task_attr =
{
    .name = "led_task",
    .priority = (osPriority_t)osPriorityBelowNormal,
    .stack_size = 16864U
};

void led_array_main_create_task(void)
{
    if (s_led_array_task == NULL)
    {
        s_led_array_task = osThreadNew(led_array_main_task_fn, NULL, &s_led_array_task_attr);
        if (s_led_array_task == NULL)
        {
            printf("LED task: create failed (heap/stack)\r\n");
        }
        else
        {
            printf("LED task: created\r\n");
        }
    }
}

static void led_array_main_task_fn(void *argument)
{
    led_array_status_t st_init;
    led_array_status_t st_timer;
    led_array_status_t st_rainbow;

    (void)argument;

    printf("LED task: running\r\n");

    st_init = led_array_init();
    st_timer = led_array_timer_init(1U);
    st_rainbow = led_array_start_rainbow(15U, 5U, 100U);
    printf("LED task: init=%d timer=%d rainbow=%d\r\n", (int)st_init, (int)st_timer, (int)st_rainbow);

    for (;;)
    {
        led_array_process();
        osDelay(1U);
    }
}
