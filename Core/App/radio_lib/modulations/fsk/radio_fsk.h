/**
 * @file radio_fsk.h
 * @brief Interfejs backendu modulacji FSK (szkielet).
 *
 * Plik definiuje docelowy kontrakt API dla FSK. Aktualna implementacja jest
 * placeholderem i zwraca błędy stanu (`RADIO_ESTATE`).
 */

#ifndef APP_RADIO_LIB_MODULATIONS_FSK_RADIO_FSK_H_
#define APP_RADIO_LIB_MODULATIONS_FSK_RADIO_FSK_H_

#include "../../radio_lib.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Domyślna konfiguracja sprzętowa backendu FSK. */
void radio_fsk_default_hw_cfg(radio_hw_cfg_t *cfg, SPI_HandleTypeDef *hspi);
/** @brief Domyślna konfiguracja parametrów backendu FSK. */
void radio_fsk_default_lora_cfg(radio_lora_cfg_t *cfg);
/** @brief Inicjalizacja backendu FSK. */
radio_status_t radio_fsk_init(const radio_hw_cfg_t *hw,
                              const radio_lora_cfg_t *cfg,
                              radio_event_cb_t cb,
                              void *user_ctx);
/** @brief Deinicjalizacja backendu FSK. */
radio_status_t radio_fsk_deinit(void);
/** @brief Uruchomienie odbioru ciągłego FSK. */
radio_status_t radio_fsk_start_rx_continuous(void);
/** @brief Uruchomienie odbioru pojedynczego FSK. */
radio_status_t radio_fsk_start_rx_single(uint16_t symbol_timeout);
/** @brief Uruchomienie nadawania asynchronicznego FSK. */
radio_status_t radio_fsk_send_async(const uint8_t *data, uint8_t len);
/** @brief Przejście do standby backendu FSK. */
radio_status_t radio_fsk_standby(void);
/** @brief Przejście do sleep backendu FSK. */
radio_status_t radio_fsk_sleep(void);
/** @brief Przetwarzanie logiki odroczonej backendu FSK. */
void radio_fsk_process(void);
/** @brief Zwrócenie i wyczyszczenie flag zdarzeń backendu FSK. */
uint32_t radio_fsk_take_events(void);
/** @brief Odczyt ostatniego pakietu backendu FSK. */
bool radio_fsk_get_last_packet(radio_packet_t *pkt);
/** @brief Odczyt stanu backendu FSK. */
radio_state_t radio_fsk_get_state(void);
/** @brief Odczyt pojedynczego rejestru (backend FSK). */
radio_status_t radio_fsk_raw_read_reg(uint8_t reg, uint8_t *value);
/** @brief Zapis pojedynczego rejestru (backend FSK). */
radio_status_t radio_fsk_raw_write_reg(uint8_t reg, uint8_t value);
/** @brief Odczyt sekwencji rejestrów (backend FSK). */
radio_status_t radio_fsk_raw_read_burst(uint8_t reg, uint8_t *data, uint8_t len);
/** @brief Zapis sekwencji rejestrów (backend FSK). */
radio_status_t radio_fsk_raw_write_burst(uint8_t reg, const uint8_t *data, uint8_t len);
/** @brief Przekazanie pinu EXTI do backendu FSK. */
void radio_fsk_on_exti(uint16_t gpio_pin);

#ifdef __cplusplus
}
#endif

#endif /* APP_RADIO_LIB_MODULATIONS_FSK_RADIO_FSK_H_ */
