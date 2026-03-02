#include "radio_main.h"

#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "radio_lib/test/radio_test.h"

#include <stdio.h>

extern SPI_HandleTypeDef hspi1;

static osThreadId_t s_radio_task = NULL;
static StaticTask_t s_radio_task_cb;
#define RADIO_TASK_STACK_SIZE   4096U
#define RADIO_TASK_STACK_WORDS  (RADIO_TASK_STACK_SIZE / sizeof(StackType_t))
static StackType_t s_radio_task_stack[RADIO_TASK_STACK_WORDS];

static void radio_main_task_fn(void *argument);

static const osThreadAttr_t s_radio_task_attr =
{
    .name = "radio_task",
    .priority = (osPriority_t)osPriorityNormal,
    .stack_mem = s_radio_task_stack,
    .stack_size = sizeof(s_radio_task_stack),
    .cb_mem = &s_radio_task_cb,
    .cb_size = sizeof(s_radio_task_cb)
};

void radio_main_create_task(void)
{
    if (s_radio_task == NULL)
    {
        s_radio_task = osThreadNew(radio_main_task_fn, NULL, &s_radio_task_attr);
        if (s_radio_task == NULL)
        {
            printf("RADIO task: create failed\r\n");
        }
    }
}

static void radio_main_task_fn(void *argument)
{
    (void)argument;

    radio_test_demo_init(&hspi1);

    for (;;)
    {
        radio_test_demo_process();
        osDelay(10U);
    }
}
