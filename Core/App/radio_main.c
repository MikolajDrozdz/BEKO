#include "radio_main.h"

#include "cmsis_os2.h"
#include "main.h"
#include "radio_lib/test/radio_test.h"

extern SPI_HandleTypeDef hspi1;

static osThreadId_t s_radio_task = NULL;

static void radio_main_task_fn(void *argument);

static const osThreadAttr_t s_radio_task_attr =
{
    .name = "radio_task",
    .priority = (osPriority_t)osPriorityNormal,
    .stack_size = 16864U
};

void radio_main_create_task(void)
{
    if (s_radio_task == NULL)
    {
        s_radio_task = osThreadNew(radio_main_task_fn, NULL, &s_radio_task_attr);
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
