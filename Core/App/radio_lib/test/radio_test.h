/**
 * @file radio_test.h
 * @brief Narzędzia testowe i diagnostyczne dla `radio_lib`.
 */

#ifndef APP_RADIO_LIB_TEST_RADIO_TEST_H_
#define APP_RADIO_LIB_TEST_RADIO_TEST_H_

#include "../radio_lib.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Sprawdza wersję układu i porównuje z oczekiwanym ID SX1276.
 * @param version [out] Opcjonalny odczyt surowego rejestru wersji.
 * @return `true` gdy układ odpowiada poprawnym identyfikatorem.
 */
bool radio_test_probe(uint8_t *version);

/**
 * @brief Drukuje podstawowy zrzut rejestrów przez `printf`.
 */
void radio_test_dump_basic(void);

/**
 * @brief Wysyła krótką ramkę testową "PING".
 * @return Kod statusu.
 */
radio_status_t radio_test_send_ping(void);

/**
 * @brief Inicjalizuje demonstracyjny scenariusz pracy radia.
 * @param hspi Uchwyt SPI przekazany z aplikacji.
 */
void radio_test_demo_init(SPI_HandleTypeDef *hspi);

/**
 * @brief Obsługuje pętlę demona testowego (zdarzenia, logi, cykliczny ping).
 */
void radio_test_demo_process(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_RADIO_LIB_TEST_RADIO_TEST_H_ */
