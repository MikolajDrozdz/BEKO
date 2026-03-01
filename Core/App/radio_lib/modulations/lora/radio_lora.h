/**
 * @file radio_lora.h
 * @brief Backend modulacji LoRa dla SX1276/RFM95.
 */

#ifndef APP_RADIO_LIB_MODULATIONS_LORA_RADIO_LORA_H_
#define APP_RADIO_LIB_MODULATIONS_LORA_RADIO_LORA_H_

#include "../../radio_lib.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Uzupełnia domyślną konfigurację sprzętową LoRa.
 * @param cfg [out] Konfiguracja sprzętowa.
 * @param hspi Uchwyt SPI.
 */
void radio_lora_default_hw_cfg(radio_hw_cfg_t *cfg, SPI_HandleTypeDef *hspi);

/**
 * @brief Uzupełnia domyślną konfigurację parametrów LoRa.
 * @param cfg [out] Konfiguracja LoRa.
 */
void radio_lora_default_lora_cfg(radio_lora_cfg_t *cfg);

/**
 * @brief Inicjalizuje backend LoRa.
 * @param hw Konfiguracja sprzętowa.
 * @param cfg Konfiguracja LoRa.
 * @param cb Callback zdarzeń (opcjonalny).
 * @param user_ctx Kontekst użytkownika callbacku.
 * @return Kod statusu.
 */
radio_status_t radio_lora_init(const radio_hw_cfg_t *hw,
                               const radio_lora_cfg_t *cfg,
                               radio_event_cb_t cb,
                               void *user_ctx);

/**
 * @brief Deinicjalizuje backend LoRa.
 * @return Kod statusu.
 */
radio_status_t radio_lora_deinit(void);

/**
 * @brief Uruchamia odbiór ciągły LoRa.
 * @return Kod statusu.
 */
radio_status_t radio_lora_start_rx_continuous(void);

/**
 * @brief Uruchamia odbiór pojedynczy LoRa.
 * @param symbol_timeout Timeout w symbolach.
 * @return Kod statusu.
 */
radio_status_t radio_lora_start_rx_single(uint16_t symbol_timeout);

/**
 * @brief Rozpoczyna asynchroniczne nadawanie LoRa.
 * @param data Payload.
 * @param len Długość payloadu.
 * @return Kod statusu.
 */
radio_status_t radio_lora_send_async(const uint8_t *data, uint8_t len);

/**
 * @brief Przełącza radio LoRa do standby.
 * @return Kod statusu.
 */
radio_status_t radio_lora_standby(void);

/**
 * @brief Przełącza radio LoRa do sleep.
 * @return Kod statusu.
 */
radio_status_t radio_lora_sleep(void);

/**
 * @brief Obsługuje logikę odroczoną po IRQ (wywoływać w pętli głównej).
 */
void radio_lora_process(void);

/**
 * @brief Zwraca i czyści maskę zdarzeń LoRa.
 * @return Maska zdarzeń.
 */
uint32_t radio_lora_take_events(void);

/**
 * @brief Odczytuje ostatnio odebrany pakiet LoRa.
 * @param pkt [out] Struktura pakietu.
 * @return `true` jeśli pakiet jest poprawny.
 */
bool radio_lora_get_last_packet(radio_packet_t *pkt);

/**
 * @brief Zwraca stan backendu LoRa.
 * @return Stan pracy.
 */
radio_state_t radio_lora_get_state(void);

/**
 * @brief Odczyt pojedynczego rejestru transceivera.
 * @param reg Adres rejestru.
 * @param value [out] Odczytana wartość.
 * @return Kod statusu.
 */
radio_status_t radio_lora_raw_read_reg(uint8_t reg, uint8_t *value);

/**
 * @brief Zapis pojedynczego rejestru transceivera.
 * @param reg Adres rejestru.
 * @param value Wartość do zapisu.
 * @return Kod statusu.
 */
radio_status_t radio_lora_raw_write_reg(uint8_t reg, uint8_t value);

/**
 * @brief Odczyt sekwencji rejestrów.
 * @param reg Adres startowy.
 * @param data [out] Bufor danych.
 * @param len Długość transferu.
 * @return Kod statusu.
 */
radio_status_t radio_lora_raw_read_burst(uint8_t reg, uint8_t *data, uint8_t len);

/**
 * @brief Zapis sekwencji rejestrów.
 * @param reg Adres startowy.
 * @param data Bufor danych.
 * @param len Długość transferu.
 * @return Kod statusu.
 */
radio_status_t radio_lora_raw_write_burst(uint8_t reg, const uint8_t *data, uint8_t len);

/**
 * @brief Przekazuje pin EXTI do backendu LoRa.
 * @param gpio_pin Pin zgłoszony przez HAL.
 */
void radio_lora_on_exti(uint16_t gpio_pin);

#ifdef __cplusplus
}
#endif

#endif /* APP_RADIO_LIB_MODULATIONS_LORA_RADIO_LORA_H_ */
