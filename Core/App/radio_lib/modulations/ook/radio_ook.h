/**
 * @file radio_ook.h
 * @brief Interfejs backendu modulacji OOK (szkielet).
 *
 * Plik definiuje docelowy kontrakt API dla OOK. Aktualna implementacja jest
 * placeholderem i zwraca błędy stanu (`RADIO_ESTATE`).
 */

#ifndef APP_RADIO_LIB_MODULATIONS_OOK_RADIO_OOK_H_
#define APP_RADIO_LIB_MODULATIONS_OOK_RADIO_OOK_H_

#include "../../radio_lib.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Domyślna konfiguracja sprzętowa backendu OOK. */
void radio_ook_default_hw_cfg(radio_hw_cfg_t *cfg, SPI_HandleTypeDef *hspi);
/** @brief Domyślna konfiguracja parametrów backendu OOK. */
void radio_ook_default_lora_cfg(radio_lora_cfg_t *cfg);
/** @brief Inicjalizacja backendu OOK. */
radio_status_t radio_ook_init(const radio_hw_cfg_t *hw,
                              const radio_lora_cfg_t *cfg,
                              radio_event_cb_t cb,
                              void *user_ctx);
/** @brief Deinicjalizacja backendu OOK. */
radio_status_t radio_ook_deinit(void);
/** @brief Uruchomienie odbioru ciągłego OOK. */
radio_status_t radio_ook_start_rx_continuous(void);
/** @brief Uruchomienie odbioru pojedynczego OOK. */
radio_status_t radio_ook_start_rx_single(uint16_t symbol_timeout);
/** @brief Uruchomienie nadawania asynchronicznego OOK. */
radio_status_t radio_ook_send_async(const uint8_t *data, uint8_t len);
/** @brief Przejście do standby backendu OOK. */
radio_status_t radio_ook_standby(void);
/** @brief Przejście do sleep backendu OOK. */
radio_status_t radio_ook_sleep(void);
/** @brief Przetwarzanie logiki odroczonej backendu OOK. */
void radio_ook_process(void);
/** @brief Zwrócenie i wyczyszczenie flag zdarzeń backendu OOK. */
uint32_t radio_ook_take_events(void);
/** @brief Odczyt ostatniego pakietu backendu OOK. */
bool radio_ook_get_last_packet(radio_packet_t *pkt);
/** @brief Odczyt stanu backendu OOK. */
radio_state_t radio_ook_get_state(void);
/** @brief Odczyt pojedynczego rejestru (backend OOK). */
radio_status_t radio_ook_raw_read_reg(uint8_t reg, uint8_t *value);
/** @brief Zapis pojedynczego rejestru (backend OOK). */
radio_status_t radio_ook_raw_write_reg(uint8_t reg, uint8_t value);
/** @brief Odczyt sekwencji rejestrów (backend OOK). */
radio_status_t radio_ook_raw_read_burst(uint8_t reg, uint8_t *data, uint8_t len);
/** @brief Zapis sekwencji rejestrów (backend OOK). */
radio_status_t radio_ook_raw_write_burst(uint8_t reg, const uint8_t *data, uint8_t len);
/** @brief Przekazanie pinu EXTI do backendu OOK. */
void radio_ook_on_exti(uint16_t gpio_pin);

#ifdef __cplusplus
}
#endif

#endif /* APP_RADIO_LIB_MODULATIONS_OOK_RADIO_OOK_H_ */
