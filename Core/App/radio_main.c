#include "radio_main.h"

#include "app_delay.h"
#include "lcd_main.h"
#include "radio_lib/radio_lib.h"

#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"

#include <stdio.h>
#include <string.h>

/*
 * Tryb energooszczędny TX-only:
 *
 * Radio SX1276 jest utrzymywane w trybie Sleep (0.2 µA) przez cały czas
 * między transmisjami. Zadanie radio_main blokuje na kolejce TX z timeoutem
 * osWaitForever — scheduler FreeRTOS może wówczas wejść w tickless idle
 * i wprowadzić MCU w tryb Stop (wybudzenie przez RTC/LPTIM skonfigurowane
 * w vPortSuppressTicksAndSleep w warstwie portowej CMSIS-OS2).
 *
 * Faza aktywna trwa ok. 100 ms raz na godzinę (czas TX LoRa SF7 BW125).
 * Polling 10 ms jest utrzymany tylko w tej krótkiej fazie.
 */

extern SPI_HandleTypeDef hspi1;

static osThreadId_t s_radio_task = NULL;
static osMessageQueueId_t s_radio_tx_queue = NULL;
static StaticTask_t s_radio_task_cb;

#define RADIO_TASK_STACK_SIZE   4096U
#define RADIO_TASK_STACK_WORDS  (RADIO_TASK_STACK_SIZE / sizeof(StackType_t))
#define RADIO_TX_QUEUE_LENGTH   1U
#define RADIO_PROCESS_PERIOD_MS 10U
#define RADIO_TX_WATCHDOG_MS    2000U
#define RADIO_INIT_RETRY_MS     5000U

static StackType_t s_radio_task_stack[RADIO_TASK_STACK_WORDS];

typedef struct
{
    uint8_t length;
    uint8_t data[RADIO_LIB_MAX_PAYLOAD];
} radio_main_tx_message_t;

static void radio_main_task_fn(void *argument);
static bool radio_main_init_radio(void);
static bool radio_main_wait_tx_done(uint32_t start_ms);
static void radio_main_print_tx(const radio_main_tx_message_t *message);
static uint32_t radio_main_now_ms(void);

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
    if (s_radio_tx_queue == NULL)
    {
        s_radio_tx_queue = osMessageQueueNew(RADIO_TX_QUEUE_LENGTH,
                                             sizeof(radio_main_tx_message_t),
                                             NULL);
        if (s_radio_tx_queue == NULL)
        {
            printf("RADIO task: queue create failed\r\n");
            return;
        }
    }

    if (s_radio_task == NULL)
    {
        s_radio_task = osThreadNew(radio_main_task_fn, NULL, &s_radio_task_attr);
        if (s_radio_task == NULL)
        {
            printf("RADIO task: create failed\r\n");
        }
    }
}

bool radio_main_send(const uint8_t *data, uint8_t length)
{
    radio_main_tx_message_t message;

    if ((s_radio_tx_queue == NULL) || (data == NULL) || (length == 0U))
    {
        return false;
    }

    memset(&message, 0, sizeof(message));
    message.length = length;
    memcpy(message.data, data, length);

    return (osMessageQueuePut(s_radio_tx_queue, &message, 0U, 0U) == osOK);
}

static void radio_main_task_fn(void *argument)
{
    radio_main_tx_message_t message;
    radio_status_t send_status;
    uint32_t tx_start_ms;

    (void)argument;

    while (!radio_main_init_radio())
    {
        app_delay_ms(RADIO_INIT_RETRY_MS);
    }

    for (;;)
    {
        /*
         * Blokuj bez timeoutu — scheduler może wejść w Stop przez tickless
         * idle. MCU budzi się przez RTC/LPTIM gdy upłynie 1 h i telemetry_main
         * wstawi ramkę do kolejki.
         */
        if (osMessageQueueGet(s_radio_tx_queue, &message, NULL, osWaitForever) != osOK)
        {
            continue;
        }

        /* radio_send_async() wchodzi w Standby przed TX, wbudzone ze Sleep */
        send_status = radio_send_async(message.data, message.length);
        if (send_status != RADIO_OK)
        {
            printf("RADIO TX failed: %d (frame dropped)\r\n", (int)send_status);
            /* radio mogło wyjść z Sleep tylko częściowo — przywróć Sleep */
            (void)radio_standby();
            (void)radio_sleep();
            continue;
        }

        tx_start_ms = radio_main_now_ms();
        radio_main_print_tx(&message);

        if (!radio_main_wait_tx_done(tx_start_ms))
        {
            printf("RADIO WARN: TX timeout, recovery\r\n");
            (void)radio_standby();
        }

        /* Radio jest teraz w Standby (radio_resume_after_tx) — uśpij je */
        if (radio_sleep() != RADIO_OK)
        {
            printf("RADIO WARN: sleep failed\r\n");
        }
        else
        {
            printf("RADIO: sleep mode\r\n");
        }
    }
}

static bool radio_main_init_radio(void)
{
    radio_hw_cfg_t radio_hw;
    radio_lora_cfg_t radio_cfg;
    radio_status_t status;

    radio_default_hw_cfg(&radio_hw, &hspi1);
    radio_default_lora_cfg(&radio_cfg);

    status = radio_init(&radio_hw, &radio_cfg, NULL, NULL);
    if (status != RADIO_OK)
    {
        printf("RADIO init failed: %d, retry in %u ms\r\n",
               (int)status,
               RADIO_INIT_RETRY_MS);
        return false;
    }

    /* Natychmiast uśpij radio — nie startujemy RX ciągłego */
    status = radio_sleep();
    if (status != RADIO_OK)
    {
        printf("RADIO sleep failed: %d, retry in %u ms\r\n",
               (int)status,
               RADIO_INIT_RETRY_MS);
        (void)radio_deinit();
        return false;
    }

    printf("RADIO ready: LoRa 868.1 MHz BW125 SF7 CR4/5 | sleep mode\r\n");
    return true;
}

/*
 * Polling na zdarzenie TX_DONE — działa tylko przez czas transmisji (~100 ms).
 * Zwraca true gdy TX_DONE, false gdy watchdog timeout.
 */
static bool radio_main_wait_tx_done(uint32_t start_ms)
{
    uint32_t events;

    for (;;)
    {
        radio_process();
        events = radio_take_events();

        if ((events & RADIO_EVENT_TX_DONE) != 0U)
        {
            printf("RADIO EVT: TX_DONE\r\n");
            return true;
        }

        if ((events & RADIO_EVENT_HW_ERROR) != 0U)
        {
            printf("RADIO EVT: HW_ERROR during TX\r\n");
            return false;
        }

        if ((uint32_t)(radio_main_now_ms() - start_ms) > RADIO_TX_WATCHDOG_MS)
        {
            return false;
        }

        app_delay_ms(RADIO_PROCESS_PERIOD_MS);
    }
}

static void radio_main_print_tx(const radio_main_tx_message_t *message)
{
    uint8_t index;

    if (message == NULL)
    {
        return;
    }

    printf("RADIO TX len=%u hex:", message->length);
    for (index = 0U; index < message->length; index++)
    {
        printf(" %02X", message->data[index]);
    }
    printf("\r\n");
}

static uint32_t radio_main_now_ms(void)
{
    uint32_t tick_hz = osKernelGetTickFreq();

    if (tick_hz == 0U)
    {
        return HAL_GetTick();
    }

    return (uint32_t)(((uint64_t)osKernelGetTickCount() * 1000ULL) / tick_hz);
}
