# led_array_lib

Driver for 5 LEDs connected to:

1. `PC7`
2. `PA8`
3. `PB10`
4. `PB4`
5. `PB5`

The library provides:

- init/deinit,
- ON/OFF control,
- brightness `0..100%`,
- one-shot fade-out ("fire and dim"),
- breath effect,
- circulating rainbow-like wave across all LEDs.

## Files

- `led_array_lib.h` - public API.
- `led_array_lib.c` - implementation.
- `led_array_lib_config.h` - compile-time options.

## Design

- Uses hardware PWM with high carrier frequency (~20 kHz).
- Timers used:
- `TIM8_CH2` -> `PC7`
- `TIM1_CH1` -> `PA8`
- `TIM2_CH3` -> `PB10`
- `TIM3_CH1` -> `PB4`
- `TIM3_CH2` -> `PB5`
- Effect timing (breath/rainbow/fade progression) is still updated by library tick.

## API Overview

- `led_array_init()`, `led_array_deinit()`
- `led_array_timer_init(tick_ms)`, `led_array_timer_deinit()`
- `led_array_timer_tick()` (for ISR scheduling)
- `led_array_process()` (polling scheduling via `HAL_GetTick`)
- `led_array_on(mask)`, `led_array_off(mask)`
- `led_array_set_brightness(mask, pct)`
- `led_array_fire_fade_out(mask, fade_ms)`
- `led_array_start_breath(mask, period_ms, min_pct, max_pct)`
- `led_array_start_rainbow(step_ms, min_pct, max_pct)`
- `led_array_stop_effect()`

## LED Masks

- `LED_ARRAY_LED_1` ... `LED_ARRAY_LED_5`
- `LED_ARRAY_LED_ALL`

Masks can be OR-ed, for example:

`LED_ARRAY_LED_1 | LED_ARRAY_LED_3`

## Usage (polling mode)

```c
#include "led_array_lib/led_array_lib.h"

void app_init(void)
{
    led_array_init();
    led_array_timer_init(1); /* 1 ms tick */
    led_array_start_rainbow(20, 5, 100);
}

void app_main(void)
{
    led_array_process();
}
```

## Usage (ISR mode)

Call `led_array_timer_tick()` from SysTick or a hardware timer ISR.

If you want the library to own `HAL_SYSTICK_Callback`, set:

`LED_ARRAY_LIB_OWNS_HAL_SYSTICK_CALLBACK = 1`

in `led_array_lib_config.h` (or compiler defines).

## Notes

- For smooth dimming, use `tick_ms = 1`.
- Effects are global (one active effect at a time).
- Manual brightness APIs (`on/off/set_brightness`) stop active effect.
