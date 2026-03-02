/**
 * @file led_array_lib.c
 * @brief LED array driver implementation.
 */

#include "led_array_lib.h"

#include "cmsis_os2.h"

#include <string.h>

#define LED_ARRAY_COUNT            5U
#define LED_ARRAY_VALID_MASK       LED_ARRAY_LED_ALL
#define LED_ARRAY_RAINBOW_PHASE_INC 8U
#define LED_ARRAY_PWM_FREQ_HZ      20000U
#define LED_ARRAY_PWM_STEPS        100U

typedef struct
{
    bool initialized;
    bool timer_running;
    uint32_t tick_period_ms;
    uint32_t last_tick_ms;
    led_array_effect_t effect;
    uint8_t manual_brightness[LED_ARRAY_COUNT];
    uint8_t active_brightness[LED_ARRAY_COUNT];
    struct
    {
        uint8_t mask;
        uint16_t duration_ms;
        uint32_t elapsed_ms;
    } fade;
    struct
    {
        uint8_t mask;
        uint16_t period_ms;
        uint8_t min_pct;
        uint8_t max_pct;
        uint32_t elapsed_ms;
    } breath;
    struct
    {
        uint16_t step_ms;
        uint8_t min_pct;
        uint8_t max_pct;
        uint16_t phase_accum_ms;
        uint8_t phase_base;
    } rainbow;
} led_array_ctx_t;

static led_array_ctx_t s_led;

static uint32_t led_array_now_ms(void)
{
    osKernelState_t state;
    uint32_t tick_hz;
    uint32_t tick_count;

    state = osKernelGetState();
    if ((state == osKernelRunning) || (state == osKernelLocked))
    {
        tick_hz = osKernelGetTickFreq();
        tick_count = osKernelGetTickCount();
        if (tick_hz == 0U)
        {
            return tick_count;
        }
        return (uint32_t)(((uint64_t)tick_count * 1000ULL) / (uint64_t)tick_hz);
    }

    return HAL_GetTick();
}

static uint32_t led_array_get_tim_clock(TIM_TypeDef *tim)
{
    uint32_t pclk;
    uint32_t timer_clock;
    RCC_ClkInitTypeDef clk_cfg;
    uint32_t flash_latency;

    HAL_RCC_GetClockConfig(&clk_cfg, &flash_latency);

    if ((tim == TIM2) || (tim == TIM3))
    {
        pclk = HAL_RCC_GetPCLK1Freq();
        if (clk_cfg.APB1CLKDivider == RCC_HCLK_DIV1)
        {
            timer_clock = pclk;
        }
        else
        {
            timer_clock = pclk * 2U;
        }
    }
    else
    {
        pclk = HAL_RCC_GetPCLK2Freq();
        if (clk_cfg.APB2CLKDivider == RCC_HCLK_DIV1)
        {
            timer_clock = pclk;
        }
        else
        {
            timer_clock = pclk * 2U;
        }
    }

    return timer_clock;
}

static void led_array_timer_base_init(TIM_TypeDef *tim)
{
    uint32_t tim_clk;
    uint32_t ticks_per_period;
    uint32_t psc;

    tim_clk = led_array_get_tim_clock(tim);
    ticks_per_period = LED_ARRAY_PWM_FREQ_HZ * LED_ARRAY_PWM_STEPS;
    if ((ticks_per_period == 0U) || (tim_clk == 0U))
    {
        return;
    }

    psc = tim_clk / ticks_per_period;
    if (psc == 0U)
    {
        psc = 1U;
    }

    tim->CR1 = 0U;
    tim->PSC = (uint16_t)(psc - 1U);
    tim->ARR = (uint32_t)(LED_ARRAY_PWM_STEPS - 1U);
    tim->EGR = TIM_EGR_UG;
    tim->CR1 |= TIM_CR1_ARPE;
}

static void led_array_timer_pwm_ch_init(TIM_TypeDef *tim, uint8_t channel)
{
    if (channel == 1U)
    {
        tim->CCMR1 &= ~(TIM_CCMR1_OC1M_Msk | TIM_CCMR1_CC1S_Msk);
        tim->CCMR1 |= (6U << TIM_CCMR1_OC1M_Pos) | TIM_CCMR1_OC1PE;
        tim->CCER |= TIM_CCER_CC1E;
        tim->CCR1 = 0U;
    }
    else if (channel == 2U)
    {
        tim->CCMR1 &= ~(TIM_CCMR1_OC2M_Msk | TIM_CCMR1_CC2S_Msk);
        tim->CCMR1 |= (6U << TIM_CCMR1_OC2M_Pos) | TIM_CCMR1_OC2PE;
        tim->CCER |= TIM_CCER_CC2E;
        tim->CCR2 = 0U;
    }
    else if (channel == 3U)
    {
        tim->CCMR2 &= ~(TIM_CCMR2_OC3M_Msk | TIM_CCMR2_CC3S_Msk);
        tim->CCMR2 |= (6U << TIM_CCMR2_OC3M_Pos) | TIM_CCMR2_OC3PE;
        tim->CCER |= TIM_CCER_CC3E;
        tim->CCR3 = 0U;
    }
}

static void led_array_hw_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_TIM1_CLK_ENABLE();
    __HAL_RCC_TIM2_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();
    __HAL_RCC_TIM8_CLK_ENABLE();

    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;

    gpio.Pin = GPIO_PIN_7;
    gpio.Alternate = GPIO_AF3_TIM8;
    HAL_GPIO_Init(GPIOC, &gpio);

    gpio.Pin = GPIO_PIN_8;
    gpio.Alternate = GPIO_AF1_TIM1;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = GPIO_PIN_10;
    gpio.Alternate = GPIO_AF1_TIM2;
    HAL_GPIO_Init(GPIOB, &gpio);

    gpio.Pin = GPIO_PIN_4 | GPIO_PIN_5;
    gpio.Alternate = GPIO_AF2_TIM3;
    HAL_GPIO_Init(GPIOB, &gpio);

    led_array_timer_base_init(TIM1);
    led_array_timer_base_init(TIM2);
    led_array_timer_base_init(TIM3);
    led_array_timer_base_init(TIM8);

    led_array_timer_pwm_ch_init(TIM8, 2U); /* PC7  */
    led_array_timer_pwm_ch_init(TIM1, 1U); /* PA8  */
    led_array_timer_pwm_ch_init(TIM2, 3U); /* PB10 */
    led_array_timer_pwm_ch_init(TIM3, 1U); /* PB4  */
    led_array_timer_pwm_ch_init(TIM3, 2U); /* PB5  */

    TIM1->BDTR |= TIM_BDTR_MOE;
    TIM8->BDTR |= TIM_BDTR_MOE;

    TIM1->CR1 |= TIM_CR1_CEN;
    TIM2->CR1 |= TIM_CR1_CEN;
    TIM3->CR1 |= TIM_CR1_CEN;
    TIM8->CR1 |= TIM_CR1_CEN;
}

static void led_array_set_hw_duty(uint8_t index, uint8_t pct)
{
    uint32_t ccr;

    if (pct > 100U)
    {
        pct = 100U;
    }
    ccr = ((uint32_t)pct * (LED_ARRAY_PWM_STEPS - 1U)) / 100U;

    switch (index)
    {
    case 0U: TIM8->CCR2 = ccr; break;
    case 1U: TIM1->CCR1 = ccr; break;
    case 2U: TIM2->CCR3 = ccr; break;
    case 3U: TIM3->CCR1 = ccr; break;
    case 4U: TIM3->CCR2 = ccr; break;
    default: break;
    }
}

static bool led_array_mask_valid(uint8_t mask)
{
    if ((mask == 0U) || ((mask & (uint8_t)(~LED_ARRAY_VALID_MASK)) != 0U))
    {
        return false;
    }
    return true;
}

static uint8_t led_array_clamp_pct(uint8_t value)
{
    return (value > 100U) ? 100U : value;
}

static void led_array_write_pin(uint8_t index, GPIO_PinState state)
{
    if (state == GPIO_PIN_SET)
    {
        led_array_set_hw_duty(index, 100U);
    }
    else
    {
        led_array_set_hw_duty(index, 0U);
    }
}

static void led_array_apply_outputs(void)
{
    uint8_t i;
    uint8_t b;

    for (i = 0U; i < LED_ARRAY_COUNT; i++)
    {
        b = s_led.active_brightness[i];
        led_array_set_hw_duty(i, b);
    }
}

static uint8_t led_array_tri8(uint8_t phase)
{
    if (phase < 128U)
    {
        return (uint8_t)(phase << 1);
    }
    return (uint8_t)((255U - phase) << 1);
}

static void led_array_effect_none(void)
{
    memcpy(s_led.active_brightness, s_led.manual_brightness, sizeof(s_led.active_brightness));
}

static void led_array_effect_fade_update(uint32_t delta_ms)
{
    uint8_t i;
    uint32_t remain;
    uint32_t level;

    if (s_led.fade.duration_ms == 0U)
    {
        s_led.effect = LED_ARRAY_EFFECT_NONE;
        led_array_effect_none();
        return;
    }

    s_led.fade.elapsed_ms += delta_ms;
    if (s_led.fade.elapsed_ms >= s_led.fade.duration_ms)
    {
        for (i = 0U; i < LED_ARRAY_COUNT; i++)
        {
            if ((s_led.fade.mask & (uint8_t)(1U << i)) != 0U)
            {
                s_led.manual_brightness[i] = 0U;
                s_led.active_brightness[i] = 0U;
            }
            else
            {
                s_led.active_brightness[i] = s_led.manual_brightness[i];
            }
        }

        s_led.effect = LED_ARRAY_EFFECT_NONE;
        return;
    }

    remain = (uint32_t)s_led.fade.duration_ms - s_led.fade.elapsed_ms;

    for (i = 0U; i < LED_ARRAY_COUNT; i++)
    {
        if ((s_led.fade.mask & (uint8_t)(1U << i)) != 0U)
        {
            level = (100U * remain) / (uint32_t)s_led.fade.duration_ms;
            s_led.active_brightness[i] = (uint8_t)level;
        }
        else
        {
            s_led.active_brightness[i] = s_led.manual_brightness[i];
        }
    }
}

static void led_array_effect_breath_update(uint32_t delta_ms)
{
    uint8_t i;
    uint32_t half;
    uint32_t phase;
    uint32_t tri;
    uint32_t range;
    uint8_t level;

    if (s_led.breath.period_ms < 2U)
    {
        s_led.breath.period_ms = 2U;
    }

    s_led.breath.elapsed_ms = (s_led.breath.elapsed_ms + delta_ms) % s_led.breath.period_ms;
    half = s_led.breath.period_ms / 2U;
    if (half == 0U)
    {
        half = 1U;
    }

    if (s_led.breath.elapsed_ms < half)
    {
        phase = s_led.breath.elapsed_ms;
    }
    else
    {
        phase = (uint32_t)s_led.breath.period_ms - s_led.breath.elapsed_ms;
    }

    tri = (phase * 100U) / half;
    if (tri > 100U)
    {
        tri = 100U;
    }

    range = (uint32_t)s_led.breath.max_pct - s_led.breath.min_pct;
    level = (uint8_t)(s_led.breath.min_pct + ((range * tri) / 100U));

    for (i = 0U; i < LED_ARRAY_COUNT; i++)
    {
        if ((s_led.breath.mask & (uint8_t)(1U << i)) != 0U)
        {
            s_led.active_brightness[i] = level;
        }
        else
        {
            s_led.active_brightness[i] = s_led.manual_brightness[i];
        }
    }
}

static void led_array_effect_rainbow_update(uint32_t delta_ms)
{
    uint8_t i;
    uint8_t phase;
    uint8_t tri;
    uint32_t range;
    uint8_t level;
    const uint8_t phase_step = (uint8_t)(256U / LED_ARRAY_COUNT);

    if (s_led.rainbow.step_ms == 0U)
    {
        s_led.rainbow.step_ms = 1U;
    }

    s_led.rainbow.phase_accum_ms = (uint16_t)(s_led.rainbow.phase_accum_ms + delta_ms);
    while (s_led.rainbow.phase_accum_ms >= s_led.rainbow.step_ms)
    {
        s_led.rainbow.phase_accum_ms = (uint16_t)(s_led.rainbow.phase_accum_ms - s_led.rainbow.step_ms);
        s_led.rainbow.phase_base = (uint8_t)(s_led.rainbow.phase_base + LED_ARRAY_RAINBOW_PHASE_INC);
    }

    range = (uint32_t)s_led.rainbow.max_pct - s_led.rainbow.min_pct;
    for (i = 0U; i < LED_ARRAY_COUNT; i++)
    {
        phase = (uint8_t)(s_led.rainbow.phase_base + (uint8_t)(phase_step * i));
        tri = led_array_tri8(phase);
        level = (uint8_t)(s_led.rainbow.min_pct + ((range * tri) / 254U));
        s_led.active_brightness[i] = level;
    }
}

static void led_array_tick_core(uint32_t delta_ms)
{
    switch (s_led.effect)
    {
    case LED_ARRAY_EFFECT_FADE_OUT:
        led_array_effect_fade_update(delta_ms);
        break;
    case LED_ARRAY_EFFECT_BREATH:
        led_array_effect_breath_update(delta_ms);
        break;
    case LED_ARRAY_EFFECT_RAINBOW:
        led_array_effect_rainbow_update(delta_ms);
        break;
    case LED_ARRAY_EFFECT_NONE:
    default:
        led_array_effect_none();
        break;
    }

    led_array_apply_outputs();
}

led_array_status_t led_array_init(void)
{
    led_array_hw_init();

    memset(&s_led, 0, sizeof(s_led));
    s_led.tick_period_ms = LED_ARRAY_LIB_DEFAULT_TICK_MS;
    s_led.last_tick_ms = led_array_now_ms();
    s_led.timer_running = true;
    s_led.initialized = true;
    s_led.effect = LED_ARRAY_EFFECT_NONE;

    return LED_ARRAY_OK;
}

led_array_status_t led_array_deinit(void)
{
    uint8_t i;

    if (!s_led.initialized)
    {
        return LED_ARRAY_ESTATE;
    }

    for (i = 0U; i < LED_ARRAY_COUNT; i++)
    {
        led_array_write_pin(i, GPIO_PIN_RESET);
    }

    memset(&s_led, 0, sizeof(s_led));
    return LED_ARRAY_OK;
}

led_array_status_t led_array_timer_init(uint32_t tick_period_ms)
{
    if (!s_led.initialized)
    {
        return LED_ARRAY_ESTATE;
    }
    if (tick_period_ms == 0U)
    {
        return LED_ARRAY_EINVAL;
    }

    s_led.tick_period_ms = tick_period_ms;
    s_led.last_tick_ms = led_array_now_ms();
    s_led.timer_running = true;
    return LED_ARRAY_OK;
}

led_array_status_t led_array_timer_deinit(void)
{
    if (!s_led.initialized)
    {
        return LED_ARRAY_ESTATE;
    }

    s_led.timer_running = false;
    return LED_ARRAY_OK;
}

void led_array_timer_tick(void)
{
    if (!s_led.initialized || !s_led.timer_running)
    {
        return;
    }

    led_array_tick_core(s_led.tick_period_ms);
}

void led_array_process(void)
{
    uint32_t now;

    if (!s_led.initialized || !s_led.timer_running)
    {
        return;
    }

    now = led_array_now_ms();
    while ((now - s_led.last_tick_ms) >= s_led.tick_period_ms)
    {
        s_led.last_tick_ms += s_led.tick_period_ms;
        led_array_tick_core(s_led.tick_period_ms);
    }
}

led_array_status_t led_array_on(uint8_t mask)
{
    uint8_t i;

    if (!s_led.initialized)
    {
        return LED_ARRAY_ESTATE;
    }
    if (!led_array_mask_valid(mask))
    {
        return LED_ARRAY_EINVAL;
    }

    s_led.effect = LED_ARRAY_EFFECT_NONE;
    for (i = 0U; i < LED_ARRAY_COUNT; i++)
    {
        if ((mask & (uint8_t)(1U << i)) != 0U)
        {
            s_led.manual_brightness[i] = 100U;
        }
    }
    return LED_ARRAY_OK;
}

led_array_status_t led_array_off(uint8_t mask)
{
    uint8_t i;

    if (!s_led.initialized)
    {
        return LED_ARRAY_ESTATE;
    }
    if (!led_array_mask_valid(mask))
    {
        return LED_ARRAY_EINVAL;
    }

    s_led.effect = LED_ARRAY_EFFECT_NONE;
    for (i = 0U; i < LED_ARRAY_COUNT; i++)
    {
        if ((mask & (uint8_t)(1U << i)) != 0U)
        {
            s_led.manual_brightness[i] = 0U;
        }
    }
    return LED_ARRAY_OK;
}

led_array_status_t led_array_set_brightness(uint8_t mask, uint8_t brightness_pct)
{
    uint8_t i;

    if (!s_led.initialized)
    {
        return LED_ARRAY_ESTATE;
    }
    if (!led_array_mask_valid(mask))
    {
        return LED_ARRAY_EINVAL;
    }

    brightness_pct = led_array_clamp_pct(brightness_pct);
    s_led.effect = LED_ARRAY_EFFECT_NONE;
    for (i = 0U; i < LED_ARRAY_COUNT; i++)
    {
        if ((mask & (uint8_t)(1U << i)) != 0U)
        {
            s_led.manual_brightness[i] = brightness_pct;
        }
    }
    return LED_ARRAY_OK;
}

led_array_status_t led_array_fire_fade_out(uint8_t mask, uint16_t fade_ms)
{
    uint8_t i;

    if (!s_led.initialized)
    {
        return LED_ARRAY_ESTATE;
    }
    if (!led_array_mask_valid(mask) || (fade_ms == 0U))
    {
        return LED_ARRAY_EINVAL;
    }

    for (i = 0U; i < LED_ARRAY_COUNT; i++)
    {
        if ((mask & (uint8_t)(1U << i)) != 0U)
        {
            s_led.manual_brightness[i] = 100U;
            s_led.active_brightness[i] = 100U;
        }
    }

    s_led.effect = LED_ARRAY_EFFECT_FADE_OUT;
    s_led.fade.mask = (uint8_t)(mask & LED_ARRAY_VALID_MASK);
    s_led.fade.duration_ms = fade_ms;
    s_led.fade.elapsed_ms = 0U;
    return LED_ARRAY_OK;
}

led_array_status_t led_array_start_breath(uint8_t mask,
                                          uint16_t period_ms,
                                          uint8_t min_pct,
                                          uint8_t max_pct)
{
    if (!s_led.initialized)
    {
        return LED_ARRAY_ESTATE;
    }
    if (!led_array_mask_valid(mask) || (period_ms < 2U))
    {
        return LED_ARRAY_EINVAL;
    }

    min_pct = led_array_clamp_pct(min_pct);
    max_pct = led_array_clamp_pct(max_pct);
    if (max_pct < min_pct)
    {
        return LED_ARRAY_EINVAL;
    }

    s_led.effect = LED_ARRAY_EFFECT_BREATH;
    s_led.breath.mask = (uint8_t)(mask & LED_ARRAY_VALID_MASK);
    s_led.breath.period_ms = period_ms;
    s_led.breath.min_pct = min_pct;
    s_led.breath.max_pct = max_pct;
    s_led.breath.elapsed_ms = 0U;
    return LED_ARRAY_OK;
}

led_array_status_t led_array_start_rainbow(uint16_t step_ms,
                                           uint8_t min_pct,
                                           uint8_t max_pct)
{
    if (!s_led.initialized)
    {
        return LED_ARRAY_ESTATE;
    }
    if (step_ms == 0U)
    {
        return LED_ARRAY_EINVAL;
    }

    min_pct = led_array_clamp_pct(min_pct);
    max_pct = led_array_clamp_pct(max_pct);
    if (max_pct < min_pct)
    {
        return LED_ARRAY_EINVAL;
    }

    s_led.effect = LED_ARRAY_EFFECT_RAINBOW;
    s_led.rainbow.step_ms = step_ms;
    s_led.rainbow.min_pct = min_pct;
    s_led.rainbow.max_pct = max_pct;
    s_led.rainbow.phase_accum_ms = 0U;
    s_led.rainbow.phase_base = 0U;
    return LED_ARRAY_OK;
}

led_array_status_t led_array_stop_effect(void)
{
    if (!s_led.initialized)
    {
        return LED_ARRAY_ESTATE;
    }

    s_led.effect = LED_ARRAY_EFFECT_NONE;
    return LED_ARRAY_OK;
}

led_array_effect_t led_array_get_effect(void)
{
    if (!s_led.initialized)
    {
        return LED_ARRAY_EFFECT_NONE;
    }
    return s_led.effect;
}

bool led_array_is_initialized(void)
{
    return s_led.initialized;
}

#if LED_ARRAY_LIB_OWNS_HAL_SYSTICK_CALLBACK
void HAL_SYSTICK_Callback(void)
{
    led_array_timer_tick();
}
#endif
