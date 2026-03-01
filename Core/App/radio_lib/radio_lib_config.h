/**
 * @file radio_lib_config.h
 * @brief Konfiguracja kompilacyjna biblioteki `radio_lib`.
 */

#ifndef APP_RADIO_LIB_RADIO_LIB_CONFIG_H_
#define APP_RADIO_LIB_RADIO_LIB_CONFIG_H_

#include <stdint.h>

/**
 * @name Identyfikatory backendów modulacji
 * @brief Stałe używane przez `RADIO_LIB_ACTIVE_MODULATION`.
 * @{
 */
#define RADIO_LIB_MODULATION_LORA 0U /**< Backend LoRa. */
#define RADIO_LIB_MODULATION_FSK  1U /**< Backend FSK. */
#define RADIO_LIB_MODULATION_OOK  2U /**< Backend OOK. */
/** @} */

/**
 * @brief Aktywny backend modulacji wybierany na etapie kompilacji.
 *
 * Domyślnie aktywny jest backend LoRa.
 */
#ifndef RADIO_LIB_ACTIVE_MODULATION
#define RADIO_LIB_ACTIVE_MODULATION RADIO_LIB_MODULATION_LORA
#endif

/**
 * @brief Własność callbacku EXTI.
 *
 * Gdy ustawione na `1`, biblioteka dostarcza `HAL_GPIO_EXTI_Callback`.
 * Gdy ustawione na `0`, użytkownik musi sam przekazać pin do `radio_on_exti`.
 */
#ifndef RADIO_LIB_OWNS_HAL_EXTI_CALLBACK
#define RADIO_LIB_OWNS_HAL_EXTI_CALLBACK 1
#endif

/**
 * @brief Timeout blokujących transferów SPI (ms).
 */
#ifndef RADIO_LIB_SPI_TIMEOUT_MS
#define RADIO_LIB_SPI_TIMEOUT_MS 100U
#endif

/**
 * @brief Maksymalna długość payloadu bufora pakietu.
 */
#ifndef RADIO_LIB_MAX_PAYLOAD
#define RADIO_LIB_MAX_PAYLOAD 255U
#endif

/**
 * @brief Domyślna częstotliwość LoRa (Hz).
 */
#ifndef RADIO_LIB_DEFAULT_FREQ_HZ
#define RADIO_LIB_DEFAULT_FREQ_HZ 868100000UL
#endif

/**
 * @brief Domyślny sync word LoRa.
 */
#ifndef RADIO_LIB_DEFAULT_SYNC_WORD
#define RADIO_LIB_DEFAULT_SYNC_WORD 0x34U
#endif

/**
 * @name Filtry EXTI DIO (LoRa)
 * @brief Pozwala wyłączyć obsługę mniej używanych linii DIO.
 * @{
 */
#ifndef RADIO_LIB_ENABLE_DIO2_EXTI
#define RADIO_LIB_ENABLE_DIO2_EXTI 0
#endif

#ifndef RADIO_LIB_ENABLE_DIO3_EXTI
#define RADIO_LIB_ENABLE_DIO3_EXTI 0
#endif

#ifndef RADIO_LIB_ENABLE_DIO4_EXTI
#define RADIO_LIB_ENABLE_DIO4_EXTI 0
#endif

#ifndef RADIO_LIB_ENABLE_DIO5_EXTI
#define RADIO_LIB_ENABLE_DIO5_EXTI 0
#endif
/** @} */

#endif /* APP_RADIO_LIB_RADIO_LIB_CONFIG_H_ */
