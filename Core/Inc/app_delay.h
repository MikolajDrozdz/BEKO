#ifndef APP_DELAY_H_
#define APP_DELAY_H_

#include "cmsis_os2.h"
#include "stm32u5xx_hal.h"

#include <stdint.h>

/*
 * Millisecond delay helper for mixed pre/post-scheduler contexts.
 * - If kernel is running: uses osDelay() with runtime tick-frequency conversion.
 * - If kernel is not running: uses a HAL_GetTick() spin wait.
 */
static inline uint32_t app_ms_to_os_ticks(uint32_t ms)
{
    uint32_t tick_hz;
    uint64_t ticks64;

    tick_hz = osKernelGetTickFreq();
    if (tick_hz == 0U)
    {
        /* User project note: RTOS tick can be configured to 10000 Hz. */
        tick_hz = 10000U;
    }

    ticks64 = (((uint64_t)ms * (uint64_t)tick_hz) + 999ULL) / 1000ULL;
    if (ticks64 == 0ULL)
    {
        ticks64 = 1ULL;
    }

    return (uint32_t)ticks64;
}

static inline void app_busy_wait_ms(uint32_t ms)
{
    uint32_t cycles_per_ms;
    uint32_t start_cycles;
    uint32_t i;

    if (ms == 0U)
    {
        return;
    }

    cycles_per_ms = SystemCoreClock / 1000U;
    if (cycles_per_ms == 0U)
    {
        cycles_per_ms = 1U;
    }

    /* Use DWT cycle counter: independent of HAL tick/TIM IRQ state. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    for (i = 0U; i < ms; i++)
    {
        start_cycles = DWT->CYCCNT;
        while ((uint32_t)(DWT->CYCCNT - start_cycles) < cycles_per_ms)
        {
            __NOP();
        }
    }
}

static inline void app_delay_ms(uint32_t ms)
{
    osKernelState_t state;

    if (ms == 0U)
    {
        return;
    }

    state = osKernelGetState();
    if (state == osKernelRunning)
    {
        (void)osDelay(app_ms_to_os_ticks(ms));
        return;
    }

    /* Kernel not running yet (e.g. osKernelReady): HAL tick can be paused. */
    app_busy_wait_ms(ms);
}

#endif /* APP_DELAY_H_ */
