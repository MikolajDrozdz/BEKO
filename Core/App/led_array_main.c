#include "led_array_main.h"

#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"
#include "led_array_lib/led_array_lib.h"

#include <stdio.h>

static osThreadId_t s_led_array_task = NULL;
static StaticTask_t s_led_array_task_cb;
#define LED_TASK_STACK_SIZE   2048U
#define LED_TASK_STACK_WORDS  (LED_TASK_STACK_SIZE / sizeof(StackType_t))
static StackType_t s_led_array_task_stack[LED_TASK_STACK_WORDS];

static void led_array_main_task_fn(void *argument);

static const osThreadAttr_t s_led_array_task_attr =
{
    .name = "led_task",
    .priority = (osPriority_t)osPriorityBelowNormal,
    .stack_mem = s_led_array_task_stack,
    .stack_size = sizeof(s_led_array_task_stack),
    .cb_mem = &s_led_array_task_cb,
    .cb_size = sizeof(s_led_array_task_cb)
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
    (void)argument;

    led_array_init();
    led_array_timer_init(10U);
    led_array_start_rainbow(15U, 5U, 100U);
    printf("LED task: running rainbow\r\n");

    for (;;)
    {
        led_array_process();
        osDelay(5U);
    }
}
