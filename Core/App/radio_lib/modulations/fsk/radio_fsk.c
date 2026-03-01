/**
 * @file radio_fsk.c
 * @brief Implementacja szkieletowa backendu FSK.
 *
 * Ten plik zawiera placeholder backendu FSK. Funkcje publiczne zachowują
 * kontrakt API, ale zwracają `RADIO_ESTATE` do czasu pełnej implementacji.
 */

#include "radio_fsk.h"

/**
 * @brief Uzupełnia domyślną konfigurację sprzętową backendu FSK.
 * @param cfg [out] Konfiguracja sprzętowa.
 * @param hspi Uchwyt SPI.
 */
void radio_fsk_default_hw_cfg(radio_hw_cfg_t *cfg, SPI_HandleTypeDef *hspi)
{
    (void)cfg;
    (void)hspi;
}

/**
 * @brief Uzupełnia domyślną konfigurację parametrów backendu FSK.
 * @param cfg [out] Konfiguracja profilu.
 */
void radio_fsk_default_lora_cfg(radio_lora_cfg_t *cfg)
{
    (void)cfg;
}

/**
 * @brief Inicjalizacja backendu FSK (placeholder).
 * @return Zawsze `RADIO_ESTATE`.
 */
radio_status_t radio_fsk_init(const radio_hw_cfg_t *hw,
                              const radio_lora_cfg_t *cfg,
                              radio_event_cb_t cb,
                              void *user_ctx)
{
    (void)hw;
    (void)cfg;
    (void)cb;
    (void)user_ctx;
    return RADIO_ESTATE;
}

/** @brief Deinicjalizacja backendu FSK (placeholder). */
radio_status_t radio_fsk_deinit(void) { return RADIO_ESTATE; }
/** @brief Uruchomienie RX ciągłego FSK (placeholder). */
radio_status_t radio_fsk_start_rx_continuous(void) { return RADIO_ESTATE; }
/** @brief Uruchomienie RX pojedynczego FSK (placeholder). */
radio_status_t radio_fsk_start_rx_single(uint16_t symbol_timeout) { (void)symbol_timeout; return RADIO_ESTATE; }
/** @brief Uruchomienie TX asynchronicznego FSK (placeholder). */
radio_status_t radio_fsk_send_async(const uint8_t *data, uint8_t len) { (void)data; (void)len; return RADIO_ESTATE; }
/** @brief Przejście do standby backendu FSK (placeholder). */
radio_status_t radio_fsk_standby(void) { return RADIO_ESTATE; }
/** @brief Przejście do sleep backendu FSK (placeholder). */
radio_status_t radio_fsk_sleep(void) { return RADIO_ESTATE; }
/** @brief Przetwarzanie odroczone backendu FSK (placeholder). */
void radio_fsk_process(void) {}
/** @brief Zwraca maskę zdarzeń backendu FSK (placeholder). */
uint32_t radio_fsk_take_events(void) { return RADIO_EVENT_NONE; }
/** @brief Odczyt ostatniego pakietu FSK (placeholder). */
bool radio_fsk_get_last_packet(radio_packet_t *pkt) { (void)pkt; return false; }
/** @brief Odczyt stanu backendu FSK (placeholder). */
radio_state_t radio_fsk_get_state(void) { return RADIO_STATE_UNINIT; }
/** @brief Odczyt rejestru backendu FSK (placeholder). */
radio_status_t radio_fsk_raw_read_reg(uint8_t reg, uint8_t *value) { (void)reg; (void)value; return RADIO_ESTATE; }
/** @brief Zapis rejestru backendu FSK (placeholder). */
radio_status_t radio_fsk_raw_write_reg(uint8_t reg, uint8_t value) { (void)reg; (void)value; return RADIO_ESTATE; }
/** @brief Odczyt burst backendu FSK (placeholder). */
radio_status_t radio_fsk_raw_read_burst(uint8_t reg, uint8_t *data, uint8_t len) { (void)reg; (void)data; (void)len; return RADIO_ESTATE; }
/** @brief Zapis burst backendu FSK (placeholder). */
radio_status_t radio_fsk_raw_write_burst(uint8_t reg, const uint8_t *data, uint8_t len) { (void)reg; (void)data; (void)len; return RADIO_ESTATE; }
/** @brief Przekazanie IRQ EXTI do backendu FSK (placeholder). */
void radio_fsk_on_exti(uint16_t gpio_pin) { (void)gpio_pin; }
