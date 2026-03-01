/**
 * @file radio_ook.c
 * @brief Implementacja szkieletowa backendu OOK.
 *
 * Ten plik zawiera placeholder backendu OOK. Funkcje publiczne zachowują
 * kontrakt API, ale zwracają `RADIO_ESTATE` do czasu pełnej implementacji.
 */

#include "radio_ook.h"

/**
 * @brief Uzupełnia domyślną konfigurację sprzętową backendu OOK.
 * @param cfg [out] Konfiguracja sprzętowa.
 * @param hspi Uchwyt SPI.
 */
void radio_ook_default_hw_cfg(radio_hw_cfg_t *cfg, SPI_HandleTypeDef *hspi)
{
    (void)cfg;
    (void)hspi;
}

/**
 * @brief Uzupełnia domyślną konfigurację parametrów backendu OOK.
 * @param cfg [out] Konfiguracja profilu.
 */
void radio_ook_default_lora_cfg(radio_lora_cfg_t *cfg)
{
    (void)cfg;
}

/**
 * @brief Inicjalizacja backendu OOK (placeholder).
 * @return Zawsze `RADIO_ESTATE`.
 */
radio_status_t radio_ook_init(const radio_hw_cfg_t *hw,
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

/** @brief Deinicjalizacja backendu OOK (placeholder). */
radio_status_t radio_ook_deinit(void) { return RADIO_ESTATE; }
/** @brief Uruchomienie RX ciągłego OOK (placeholder). */
radio_status_t radio_ook_start_rx_continuous(void) { return RADIO_ESTATE; }
/** @brief Uruchomienie RX pojedynczego OOK (placeholder). */
radio_status_t radio_ook_start_rx_single(uint16_t symbol_timeout) { (void)symbol_timeout; return RADIO_ESTATE; }
/** @brief Uruchomienie TX asynchronicznego OOK (placeholder). */
radio_status_t radio_ook_send_async(const uint8_t *data, uint8_t len) { (void)data; (void)len; return RADIO_ESTATE; }
/** @brief Przejście do standby backendu OOK (placeholder). */
radio_status_t radio_ook_standby(void) { return RADIO_ESTATE; }
/** @brief Przejście do sleep backendu OOK (placeholder). */
radio_status_t radio_ook_sleep(void) { return RADIO_ESTATE; }
/** @brief Przetwarzanie odroczone backendu OOK (placeholder). */
void radio_ook_process(void) {}
/** @brief Zwraca maskę zdarzeń backendu OOK (placeholder). */
uint32_t radio_ook_take_events(void) { return RADIO_EVENT_NONE; }
/** @brief Odczyt ostatniego pakietu OOK (placeholder). */
bool radio_ook_get_last_packet(radio_packet_t *pkt) { (void)pkt; return false; }
/** @brief Odczyt stanu backendu OOK (placeholder). */
radio_state_t radio_ook_get_state(void) { return RADIO_STATE_UNINIT; }
/** @brief Odczyt rejestru backendu OOK (placeholder). */
radio_status_t radio_ook_raw_read_reg(uint8_t reg, uint8_t *value) { (void)reg; (void)value; return RADIO_ESTATE; }
/** @brief Zapis rejestru backendu OOK (placeholder). */
radio_status_t radio_ook_raw_write_reg(uint8_t reg, uint8_t value) { (void)reg; (void)value; return RADIO_ESTATE; }
/** @brief Odczyt burst backendu OOK (placeholder). */
radio_status_t radio_ook_raw_read_burst(uint8_t reg, uint8_t *data, uint8_t len) { (void)reg; (void)data; (void)len; return RADIO_ESTATE; }
/** @brief Zapis burst backendu OOK (placeholder). */
radio_status_t radio_ook_raw_write_burst(uint8_t reg, const uint8_t *data, uint8_t len) { (void)reg; (void)data; (void)len; return RADIO_ESTATE; }
/** @brief Przekazanie IRQ EXTI do backendu OOK (placeholder). */
void radio_ook_on_exti(uint16_t gpio_pin) { (void)gpio_pin; }
