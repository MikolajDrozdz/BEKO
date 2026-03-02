/**
 * @file led_array_lib.h
 * @brief LED array driver for 5 LEDs (PC7, PA8, PB10, PB4, PB5).
 */

#ifndef APP_LED_ARRAY_LIB_LED_ARRAY_LIB_H_
#define APP_LED_ARRAY_LIB_LED_ARRAY_LIB_H_

#include "led_array_lib_config.h"
#include "stm32u5xx_hal.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Driver status codes. */
typedef enum
{
    LED_ARRAY_OK = 0,
    LED_ARRAY_EINVAL = -1,
    LED_ARRAY_ESTATE = -2
} led_array_status_t;

/** @brief LED mask bits. */
typedef enum
{
    LED_ARRAY_LED_1 = (1U << 0),  /**< PC7  */
    LED_ARRAY_LED_2 = (1U << 1),  /**< PA8  */
    LED_ARRAY_LED_3 = (1U << 2),  /**< PB10 */
    LED_ARRAY_LED_4 = (1U << 3),  /**< PB4  */
    LED_ARRAY_LED_5 = (1U << 4),  /**< PB5  */
    LED_ARRAY_LED_ALL = 0x1FU
} led_array_led_mask_t;

/** @brief Global effect mode. */
typedef enum
{
    LED_ARRAY_EFFECT_NONE = 0,
    LED_ARRAY_EFFECT_FADE_OUT,
    LED_ARRAY_EFFECT_BREATH,
    LED_ARRAY_EFFECT_RAINBOW
} led_array_effect_t;

/**
 * @brief Initialize the LED array driver.
 * @return Driver status.
 */
led_array_status_t led_array_init(void);

/**
 * @brief Deinitialize the LED array driver and turn off all LEDs.
 * @return Driver status.
 */
led_array_status_t led_array_deinit(void);

/**
 * @brief Configure internal scheduler tick period.
 *
 * This controls effect update and brightness modulation step.
 *
 * @param tick_period_ms Tick period in milliseconds (>= 1).
 * @return Driver status.
 */
led_array_status_t led_array_timer_init(uint32_t tick_period_ms);

/**
 * @brief Disable the scheduler updates.
 * @return Driver status.
 */
led_array_status_t led_array_timer_deinit(void);

/**
 * @brief Tick function for ISR-driven scheduling.
 *
 * Call this from SysTick or a hardware timer ISR if you want deterministic
 * timing independent of the main loop.
 */
void led_array_timer_tick(void);

/**
 * @brief Polling scheduler update (HAL_GetTick based).
 *
 * Call this from the main loop when not using ISR-driven ticks.
 */
void led_array_process(void);

/**
 * @brief Turn LEDs on at 100% brightness.
 * @param mask LED mask.
 * @return Driver status.
 */
led_array_status_t led_array_on(uint8_t mask);

/**
 * @brief Turn LEDs off.
 * @param mask LED mask.
 * @return Driver status.
 */
led_array_status_t led_array_off(uint8_t mask);

/**
 * @brief Set LED brightness in range 0..100%.
 * @param mask LED mask.
 * @param brightness_pct Brightness in percent.
 * @return Driver status.
 */
led_array_status_t led_array_set_brightness(uint8_t mask, uint8_t brightness_pct);

/**
 * @brief Trigger one-shot fade-out from 100% to 0%.
 * @param mask LED mask.
 * @param fade_ms Fade duration in milliseconds.
 * @return Driver status.
 */
led_array_status_t led_array_fire_fade_out(uint8_t mask, uint16_t fade_ms);

/**
 * @brief Start breathing effect on selected LEDs.
 * @param mask LED mask.
 * @param period_ms Full breath period (up + down) in milliseconds.
 * @param min_pct Minimum brightness (0..100).
 * @param max_pct Maximum brightness (0..100, must be >= min_pct).
 * @return Driver status.
 */
led_array_status_t led_array_start_breath(uint8_t mask,
                                          uint16_t period_ms,
                                          uint8_t min_pct,
                                          uint8_t max_pct);

/**
 * @brief Start circulating rainbow-like wave on all LEDs.
 *
 * For a monochrome LED strip this effect is implemented as a moving brightness
 * wave across LED1..LED5.
 *
 * @param step_ms Phase step period in milliseconds.
 * @param min_pct Minimum brightness (0..100).
 * @param max_pct Maximum brightness (0..100, must be >= min_pct).
 * @return Driver status.
 */
led_array_status_t led_array_start_rainbow(uint16_t step_ms,
                                           uint8_t min_pct,
                                           uint8_t max_pct);

/**
 * @brief Stop the active effect and keep current manual brightness values.
 * @return Driver status.
 */
led_array_status_t led_array_stop_effect(void);

/**
 * @brief Read currently active global effect.
 * @return Active effect.
 */
led_array_effect_t led_array_get_effect(void);

/**
 * @brief Read initialization state.
 * @return true if initialized.
 */
bool led_array_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_LED_ARRAY_LIB_LED_ARRAY_LIB_H_ */

