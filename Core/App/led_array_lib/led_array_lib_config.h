/**
 * @file led_array_lib_config.h
 * @brief Compile-time configuration for led_array_lib.
 */

#ifndef APP_LED_ARRAY_LIB_LED_ARRAY_LIB_CONFIG_H_
#define APP_LED_ARRAY_LIB_LED_ARRAY_LIB_CONFIG_H_

/**
 * @brief Default scheduler tick period in milliseconds.
 *
 * For smooth dimming effects keep this at 1 ms.
 */
#ifndef LED_ARRAY_LIB_DEFAULT_TICK_MS
#define LED_ARRAY_LIB_DEFAULT_TICK_MS 1U
#endif

/**
 * @brief Own HAL_SYSTICK_Callback in the library.
 *
 * If set to 1, the library provides HAL_SYSTICK_Callback() and calls
 * led_array_timer_tick() every SysTick interrupt.
 *
 * Keep set to 0 if your project already defines HAL_SYSTICK_Callback.
 */
#ifndef LED_ARRAY_LIB_OWNS_HAL_SYSTICK_CALLBACK
#define LED_ARRAY_LIB_OWNS_HAL_SYSTICK_CALLBACK 0
#endif

#endif /* APP_LED_ARRAY_LIB_LED_ARRAY_LIB_CONFIG_H_ */

