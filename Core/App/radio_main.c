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
static void radio_main_handle_events(uint32_t events, uint32_t *tx_start_ms);
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
    uint32_t events;
    uint32_t tx_start_ms = 0U;

    (void)argument;

    while (!radio_main_init_radio())
    {
        app_delay_ms(RADIO_INIT_RETRY_MS);
    }

    for (;;)
    {
        radio_process();
        events = radio_take_events();
        radio_main_handle_events(events, &tx_start_ms);

        if ((radio_get_state() == RADIO_STATE_TX) &&
            (tx_start_ms != 0U) &&
            ((uint32_t)(radio_main_now_ms() - tx_start_ms) > RADIO_TX_WATCHDOG_MS))
        {
            printf("RADIO WARN: TX timeout, recovery without retransmission\r\n");
            (void)radio_standby();
            (void)radio_start_rx_continuous();
            tx_start_ms = 0U;
        }

        if ((radio_get_state() != RADIO_STATE_TX) &&
            (osMessageQueueGet(s_radio_tx_queue, &message, NULL, 0U) == osOK))
        {
            send_status = radio_send_async(message.data, message.length);
            if (send_status == RADIO_OK)
            {
                tx_start_ms = radio_main_now_ms();
                radio_main_print_tx(&message);
            }
            else
            {
                printf("RADIO TX failed: %d (frame dropped)\r\n", (int)send_status);
                if (send_status == RADIO_EHW)
                {
                    (void)radio_standby();
                    (void)radio_start_rx_continuous();
                }
            }
        }

        app_delay_ms(RADIO_PROCESS_PERIOD_MS);
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

    status = radio_start_rx_continuous();
    if (status != RADIO_OK)
    {
        printf("RADIO RX start failed: %d, retry in %u ms\r\n",
               (int)status,
               RADIO_INIT_RETRY_MS);
        (void)radio_deinit();
        return false;
    }

    printf("RADIO ready: LoRa 868.1 MHz BW125 SF7 CR4/5\r\n");
    return true;
}

static void radio_main_handle_events(uint32_t events, uint32_t *tx_start_ms)
{
    radio_packet_t packet;

    if ((events & RADIO_EVENT_TX_DONE) != 0U)
    {
        printf("RADIO EVT: TX_DONE\r\n");
        if (tx_start_ms != NULL)
        {
            *tx_start_ms = 0U;
        }
    }

    if ((events & RADIO_EVENT_RX_DONE) != 0U)
    {
        if (radio_get_last_packet(&packet))
        {
            printf("RADIO RX len=%u RSSI=%d SNR=%d\r\n",
                   packet.length,
                   packet.rssi_dbm,
                   packet.snr_db);
            (void)lcd_main_push_message(packet.rssi_dbm, packet.data, packet.length);
        }
    }

    if ((events & RADIO_EVENT_CRC_ERR) != 0U)
    {
        printf("RADIO EVT: PHY CRC_ERR\r\n");
    }
    if ((events & RADIO_EVENT_FIFO_OVERRUN) != 0U)
    {
        printf("RADIO EVT: FIFO_OVERRUN\r\n");
    }
    if ((events & RADIO_EVENT_HW_ERROR) != 0U)
    {
        printf("RADIO EVT: HW_ERROR\r\n");
        (void)radio_standby();
        (void)radio_start_rx_continuous();
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
