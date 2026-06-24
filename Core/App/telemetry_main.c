#include "telemetry_main.h"

#include "app_delay.h"
#include "bmp280_main.h"
#include "radio_main.h"
#include "soil_moisture_main.h"
#include "telemetry_frame.h"

#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef TELEMETRY_NETWORK_ID
#define TELEMETRY_NETWORK_ID              0x0001U
#endif

#ifndef TELEMETRY_DEVICE_ID
#define TELEMETRY_DEVICE_ID               0x0001U
#endif

#ifndef TELEMETRY_INTERVAL_MS
#define TELEMETRY_INTERVAL_MS             3600000U
#endif
#define TELEMETRY_BME_STARTUP_WAIT_MS     5000U
#define TELEMETRY_BME_POLL_MS             100U
#define TELEMETRY_BME_MAX_AGE_MS          5000U
#define TELEMETRY_TASK_STACK_SIZE         2048U
#define TELEMETRY_TASK_STACK_WORDS        (TELEMETRY_TASK_STACK_SIZE / sizeof(StackType_t))

static osThreadId_t s_telemetry_task = NULL;
static StaticTask_t s_telemetry_task_cb;
static StackType_t s_telemetry_task_stack[TELEMETRY_TASK_STACK_WORDS];

static void telemetry_main_task_fn(void *argument);
static void telemetry_main_send_sample(void);
static bool telemetry_main_get_fresh_bme(bmp280_api_data_t *data);
static int16_t telemetry_main_temperature_to_wire(float temperature_c);
static uint16_t telemetry_main_pressure_to_wire(float pressure_pa);

static const osThreadAttr_t s_telemetry_task_attr =
{
    .name = "telemetry_task",
    .priority = (osPriority_t)osPriorityBelowNormal,
    .stack_mem = s_telemetry_task_stack,
    .stack_size = sizeof(s_telemetry_task_stack),
    .cb_mem = &s_telemetry_task_cb,
    .cb_size = sizeof(s_telemetry_task_cb)
};

void telemetry_main_create_task(void)
{
    if (s_telemetry_task == NULL)
    {
        s_telemetry_task = osThreadNew(telemetry_main_task_fn, NULL, &s_telemetry_task_attr);
        if (s_telemetry_task == NULL)
        {
            printf("TELEMETRY task: create failed\r\n");
        }
    }
}

static void telemetry_main_task_fn(void *argument)
{
    uint32_t waited_ms = 0U;
    uint32_t next_wakeup;
    uint32_t interval_ticks;
    bmp280_api_data_t bme_data;

    (void)argument;

    memset(&bme_data, 0, sizeof(bme_data));
    while ((waited_ms < TELEMETRY_BME_STARTUP_WAIT_MS) &&
           !telemetry_main_get_fresh_bme(&bme_data))
    {
        app_delay_ms(TELEMETRY_BME_POLL_MS);
        waited_ms += TELEMETRY_BME_POLL_MS;
    }

    telemetry_main_send_sample();

    interval_ticks = app_ms_to_os_ticks(TELEMETRY_INTERVAL_MS);
    next_wakeup = osKernelGetTickCount() + interval_ticks;

    for (;;)
    {
        (void)osDelayUntil(next_wakeup);
        next_wakeup += interval_ticks;
        telemetry_main_send_sample();
    }
}

static void telemetry_main_send_sample(void)
{
    telemetry_sample_t sample;
    soil_moisture_data_t soil;
    bmp280_api_data_t bme;
    uint8_t frame[TELEMETRY_FRAME_SIZE];
    bool soil_ok;
    bool bme_ok;

    memset(&sample, 0, sizeof(sample));
    memset(&soil, 0, sizeof(soil));
    memset(&bme, 0, sizeof(bme));

    sample.network_id = TELEMETRY_NETWORK_ID;
    sample.device_id = TELEMETRY_DEVICE_ID;
    sample.soil_percent_1 = TELEMETRY_SOIL_INVALID;
    sample.soil_percent_2 = TELEMETRY_SOIL_INVALID;
    sample.temperature_centi_c = TELEMETRY_TEMPERATURE_INVALID;
    sample.pressure_deci_hpa = TELEMETRY_PRESSURE_INVALID;

    soil_ok = soil_moisture_read(&soil);
    if (soil.valid[0])
    {
        sample.soil_percent_1 = soil.percent[0];
    }
    if (soil.valid[1])
    {
        sample.soil_percent_2 = soil.percent[1];
    }

    bme_ok = telemetry_main_get_fresh_bme(&bme);
    if (bme_ok)
    {
        sample.temperature_centi_c = telemetry_main_temperature_to_wire(bme.temperature_c);
        sample.pressure_deci_hpa = telemetry_main_pressure_to_wire(bme.pressure_pa);
    }

    if (!telemetry_frame_encode(&sample, frame))
    {
        printf("TELEMETRY: frame encode failed\r\n");
        return;
    }

    printf("TELEMETRY: soil=%u/%u raw=%u/%u temp=%d pressure=%u status=%s/%s\r\n",
           sample.soil_percent_1,
           sample.soil_percent_2,
           soil.raw[0],
           soil.raw[1],
           sample.temperature_centi_c,
           sample.pressure_deci_hpa,
           soil_ok ? "OK" : "ERR",
           bme_ok ? "OK" : "ERR");

    if (!radio_main_send(frame, TELEMETRY_FRAME_SIZE))
    {
        printf("TELEMETRY: radio queue unavailable/full\r\n");
    }
}

static bool telemetry_main_get_fresh_bme(bmp280_api_data_t *data)
{
    if ((data == NULL) || !bmp280_main_get_last(data) || !data->valid)
    {
        return false;
    }

    return ((uint32_t)(HAL_GetTick() - data->timestamp_ms) <= TELEMETRY_BME_MAX_AGE_MS);
}

static int16_t telemetry_main_temperature_to_wire(float temperature_c)
{
    float scaled;

    if (!isfinite(temperature_c))
    {
        return TELEMETRY_TEMPERATURE_INVALID;
    }

    scaled = temperature_c * 100.0f;
    if ((scaled <= (float)INT16_MIN) || (scaled > (float)INT16_MAX))
    {
        return TELEMETRY_TEMPERATURE_INVALID;
    }

    scaled += (scaled >= 0.0f) ? 0.5f : -0.5f;
    return (int16_t)scaled;
}

static uint16_t telemetry_main_pressure_to_wire(float pressure_pa)
{
    float scaled;

    if (!isfinite(pressure_pa) || (pressure_pa < 0.0f))
    {
        return TELEMETRY_PRESSURE_INVALID;
    }

    scaled = (pressure_pa / 10.0f) + 0.5f;
    if (scaled >= (float)TELEMETRY_PRESSURE_INVALID)
    {
        return TELEMETRY_PRESSURE_INVALID;
    }

    return (uint16_t)scaled;
}
