#include "button_main.h"

#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "main.h"
#include "task.h"

#include <stdio.h>
#include <string.h>

#define BUTTON_TASK_STACK_SIZE          2048U
#define BUTTON_TASK_STACK_WORDS         (BUTTON_TASK_STACK_SIZE / sizeof(StackType_t))
#define BUTTON_POLL_MS                  25U
#define BUTTON_DEBOUNCE_SAMPLES         2U
#define BUTTON_OK_LONG_PRESS_MS         800U
#define BUTTON_EVENT_QUEUE_DEPTH        16U

typedef enum
{
    BUTTON_ID_UP = 0,
    BUTTON_ID_DOWN,
    BUTTON_ID_OK,
    BUTTON_ID_COUNT
} button_id_t;

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
    bool active_low;
} button_hw_t;

typedef struct
{
    bool raw_pressed;
    bool stable_pressed;
    uint8_t stable_count;
    uint32_t pressed_since_ms;
    bool long_reported;
} button_state_t;

static osThreadId_t s_button_task = NULL;
static osMessageQueueId_t s_button_evt_queue = NULL;
static StaticTask_t s_button_task_cb;
static StackType_t s_button_task_stack[BUTTON_TASK_STACK_WORDS];

static const button_hw_t s_button_hw[BUTTON_ID_COUNT] =
{
    [BUTTON_ID_UP] = { DIG_B1_GPIO_Port, DIG_B1_Pin, true },
    [BUTTON_ID_DOWN] = { DIG_B2_GPIO_Port, DIG_B2_Pin, true },
    [BUTTON_ID_OK] = { DIG_B3_GPIO_Port, DIG_B3_Pin, true }
};

static void button_main_task_fn(void *argument);
static bool button_hw_is_pressed(button_id_t id);
static void button_try_emit(button_event_t evt);

static const osThreadAttr_t s_button_task_attr =
{
    .name = "button_task",
    .priority = (osPriority_t)osPriorityBelowNormal,
    .stack_mem = s_button_task_stack,
    .stack_size = sizeof(s_button_task_stack),
    .cb_mem = &s_button_task_cb,
    .cb_size = sizeof(s_button_task_cb)
};

void button_main_create_task(void)
{
    if (s_button_evt_queue == NULL)
    {
        s_button_evt_queue = osMessageQueueNew(BUTTON_EVENT_QUEUE_DEPTH, sizeof(button_event_t), NULL);
        if (s_button_evt_queue == NULL)
        {
            printf("BUTTON: queue create failed\r\n");
            return;
        }
    }

    if (s_button_task == NULL)
    {
        s_button_task = osThreadNew(button_main_task_fn, NULL, &s_button_task_attr);
        if (s_button_task == NULL)
        {
            printf("BUTTON: task create failed\r\n");
        }
    }
}

bool button_main_get_event(button_event_t *evt, uint32_t timeout_ms)
{
    if ((evt == NULL) || (s_button_evt_queue == NULL))
    {
        return false;
    }

    return (osMessageQueueGet(s_button_evt_queue, evt, NULL, timeout_ms) == osOK);
}

static void button_main_task_fn(void *argument)
{
    button_state_t st[BUTTON_ID_COUNT];
    uint32_t i;

    (void)argument;
    memset(st, 0, sizeof(st));

    for (i = 0U; i < BUTTON_ID_COUNT; i++)
    {
        st[i].raw_pressed = button_hw_is_pressed((button_id_t)i);
        st[i].stable_pressed = st[i].raw_pressed;
        st[i].stable_count = BUTTON_DEBOUNCE_SAMPLES;
    }

    for (;;)
    {
        uint32_t now_ms = HAL_GetTick();

        for (i = 0U; i < BUTTON_ID_COUNT; i++)
        {
            bool current_pressed = button_hw_is_pressed((button_id_t)i);
            button_state_t *s = &st[i];

            if (current_pressed == s->raw_pressed)
            {
                if (s->stable_count < BUTTON_DEBOUNCE_SAMPLES)
                {
                    s->stable_count++;
                }
            }
            else
            {
                s->raw_pressed = current_pressed;
                s->stable_count = 0U;
            }

            if ((s->stable_count >= BUTTON_DEBOUNCE_SAMPLES) && (s->stable_pressed != s->raw_pressed))
            {
                s->stable_pressed = s->raw_pressed;

                if (s->stable_pressed)
                {
                    s->pressed_since_ms = now_ms;
                    s->long_reported = false;
                }
                else
                {
                    uint32_t press_time = now_ms - s->pressed_since_ms;

                    if ((i == BUTTON_ID_OK) && s->long_reported)
                    {
                        /* Long event was already emitted while holding. */
                    }
                    else
                    {
                        switch ((button_id_t)i)
                        {
                            case BUTTON_ID_UP:
                                button_try_emit(BUTTON_EVENT_UP_SHORT);
                                break;
                            case BUTTON_ID_DOWN:
                                button_try_emit(BUTTON_EVENT_DOWN_SHORT);
                                break;
                            case BUTTON_ID_OK:
                                if (press_time >= BUTTON_OK_LONG_PRESS_MS)
                                {
                                    button_try_emit(BUTTON_EVENT_OK_LONG);
                                }
                                else
                                {
                                    button_try_emit(BUTTON_EVENT_OK_SHORT);
                                }
                                break;
                            default:
                                break;
                        }
                    }
                }
            }
            else if ((i == BUTTON_ID_OK) && s->stable_pressed && (!s->long_reported))
            {
                if ((now_ms - s->pressed_since_ms) >= BUTTON_OK_LONG_PRESS_MS)
                {
                    s->long_reported = true;
                    button_try_emit(BUTTON_EVENT_OK_LONG);
                }
            }
        }

        osDelay(BUTTON_POLL_MS);
    }
}

static bool button_hw_is_pressed(button_id_t id)
{
    GPIO_PinState raw;
    bool pressed;

    if (id >= BUTTON_ID_COUNT)
    {
        return false;
    }

    raw = HAL_GPIO_ReadPin(s_button_hw[id].port, s_button_hw[id].pin);
    pressed = (raw == GPIO_PIN_SET);
    if (s_button_hw[id].active_low)
    {
        pressed = !pressed;
    }

    return pressed;
}

static void button_try_emit(button_event_t evt)
{
    if (s_button_evt_queue == NULL)
    {
        return;
    }

    if (osMessageQueuePut(s_button_evt_queue, &evt, 0U, 0U) != osOK)
    {
        button_event_t dropped;
        if (osMessageQueueGet(s_button_evt_queue, &dropped, NULL, 0U) == osOK)
        {
            (void)osMessageQueuePut(s_button_evt_queue, &evt, 0U, 0U);
        }
    }
}
