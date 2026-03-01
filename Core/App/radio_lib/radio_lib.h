/**
 * @file radio_lib.h
 * @brief Główne API biblioteki radiowej.
 *
 * Ten plik udostępnia jednolite API dla aplikacji. Konkretna implementacja
 * jest wybierana kompilacyjnie (LoRa/FSK/OOK) przez `radio_lib_config.h`.
 */

#ifndef APP_RADIO_LIB_RADIO_LIB_H_
#define APP_RADIO_LIB_RADIO_LIB_H_

#include "radio_lib_config.h"

#include "stm32u5xx_hal.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Kody statusu zwracane przez API radia.
 */
typedef enum
{
    RADIO_OK = 0,       /**< Operacja zakończona poprawnie. */
    RADIO_EINVAL = -1,  /**< Niepoprawny argument wejściowy. */
    RADIO_EBUS = -2,    /**< Zasób zajęty lub niepoprawny stan wykonania. */
    RADIO_ETIMEOUT = -3,/**< Przekroczono limit czasu operacji. */
    RADIO_ESTATE = -4,  /**< Niepoprawny stan cyklu życia (np. brak init). */
    RADIO_EHW = -5      /**< Błąd warstwy sprzętowej (SPI/GPIO/rejestry). */
} radio_status_t;

/**
 * @brief Aktualny stan pracy backendu radiowego.
 */
typedef enum
{
    RADIO_STATE_UNINIT = 0, /**< Backend nie został zainicjalizowany. */
    RADIO_STATE_STANDBY,    /**< Tryb gotowości. */
    RADIO_STATE_RX_CONT,    /**< Odbiór ciągły aktywny. */
    RADIO_STATE_RX_SINGLE,  /**< Odbiór pojedynczy aktywny. */
    RADIO_STATE_TX          /**< Nadawanie aktywne. */
} radio_state_t;

/**
 * @brief Maska zdarzeń generowanych przez backend.
 */
typedef enum
{
    RADIO_EVENT_NONE = 0U,             /**< Brak zdarzeń. */
    RADIO_EVENT_TX_DONE = (1UL << 0),  /**< Zakończono nadawanie. */
    RADIO_EVENT_RX_DONE = (1UL << 1),  /**< Odebrano ramkę. */
    RADIO_EVENT_RX_TIMEOUT = (1UL << 2), /**< Timeout odbioru. */
    RADIO_EVENT_CRC_ERR = (1UL << 3),  /**< Błąd CRC ramki. */
    RADIO_EVENT_CAD_DONE = (1UL << 4), /**< Zakończono CAD. */
    RADIO_EVENT_CAD_DETECTED = (1UL << 5), /**< CAD wykrył aktywność. */
    RADIO_EVENT_FIFO_OVERRUN = (1UL << 6), /**< Przepełnienie/niepoprawna długość FIFO. */
    RADIO_EVENT_HW_ERROR = (1UL << 7)  /**< Błąd sprzętowy backendu. */
} radio_event_mask_t;

/**
 * @brief Kody szerokości pasma LoRa.
 */
typedef enum
{
    RADIO_LORA_BW_7_8_KHZ = 0U,
    RADIO_LORA_BW_10_4_KHZ = 1U,
    RADIO_LORA_BW_15_6_KHZ = 2U,
    RADIO_LORA_BW_20_8_KHZ = 3U,
    RADIO_LORA_BW_31_25_KHZ = 4U,
    RADIO_LORA_BW_41_7_KHZ = 5U,
    RADIO_LORA_BW_62_5_KHZ = 6U,
    RADIO_LORA_BW_125_KHZ = 7U,
    RADIO_LORA_BW_250_KHZ = 8U,
    RADIO_LORA_BW_500_KHZ = 9U
} radio_lora_bw_t;

/**
 * @brief Opis pojedynczego pinu GPIO.
 */
typedef struct
{
    GPIO_TypeDef *port; /**< Port GPIO. */
    uint16_t pin;       /**< Maska pinu GPIO. */
} radio_gpio_t;

/**
 * @brief Konfiguracja sprzętowa modułu radiowego.
 */
typedef struct
{
    SPI_HandleTypeDef *hspi; /**< Uchwyt SPI używany przez transceiver. */
    radio_gpio_t nss;        /**< Pin NSS/CS. */
    radio_gpio_t reset;      /**< Pin RESET transceivera. */
    radio_gpio_t dio[6];     /**< Piny DIO0..DIO5 (IRQ/status). */
    uint32_t spi_timeout_ms; /**< Timeout blokujących transferów SPI. */
} radio_hw_cfg_t;

/**
 * @brief Konfiguracja profilu LoRa.
 */
typedef struct
{
    uint32_t frequency_hz;      /**< Częstotliwość RF [Hz]. */
    radio_lora_bw_t bandwidth;  /**< Szerokość pasma. */
    uint8_t spreading_factor;   /**< SF6..SF12. */
    uint8_t coding_rate;        /**< Mianownik CR: 4/x, x=5..8. */
    uint16_t preamble_len;      /**< Długość preambuły w symbolach. */
    uint8_t sync_word;          /**< Sync word LoRa. */
    bool crc_on;                /**< Włączenie CRC payloadu. */
    bool invert_iq;             /**< Włączenie inwersji IQ. */
    int8_t tx_power_dbm;        /**< Moc TX [dBm]. */
    bool implicit_header;       /**< Tryb nagłówka niejawnego. */
    uint8_t payload_len;        /**< Stała długość payloadu (implicit header). */
} radio_lora_cfg_t;

/**
 * @brief Bufor ostatnio odebranej ramki.
 */
typedef struct
{
    uint8_t data[RADIO_LIB_MAX_PAYLOAD]; /**< Payload ramki. */
    uint8_t length;                      /**< Długość payloadu. */
    int16_t rssi_dbm;                    /**< RSSI pakietu [dBm]. */
    int8_t snr_db;                       /**< SNR pakietu [dB]. */
    uint32_t timestamp_ms;               /**< Znacznik czasu (`HAL_GetTick`). */
    bool valid;                          /**< Flaga poprawności danych bufora. */
} radio_packet_t;

/**
 * @brief Typ funkcji callback dla zdarzeń radiowych.
 * @param events Maska zdarzeń (`radio_event_mask_t`).
 * @param user_ctx Wskaźnik kontekstu użytkownika przekazany w `radio_init`.
 */
typedef void (*radio_event_cb_t)(uint32_t events, void *user_ctx);

/**
 * @brief Wypełnia domyślną konfigurację sprzętową dla aktywnej modulacji.
 * @param cfg [out] Struktura konfiguracji do uzupełnienia.
 * @param hspi Uchwyt SPI używany przez radio.
 */
void radio_default_hw_cfg(radio_hw_cfg_t *cfg, SPI_HandleTypeDef *hspi);

/**
 * @brief Wypełnia domyślny profil LoRa.
 * @param cfg [out] Struktura konfiguracji LoRa do uzupełnienia.
 */
void radio_default_lora_cfg(radio_lora_cfg_t *cfg);

/**
 * @brief Inicjalizuje backend radiowy.
 * @param hw Konfiguracja sprzętowa.
 * @param cfg Konfiguracja profilu LoRa.
 * @param cb Callback zdarzeń (opcjonalny, może być `NULL`).
 * @param user_ctx Kontekst użytkownika przekazywany do callbacku.
 * @return Kod statusu.
 */
radio_status_t radio_init(const radio_hw_cfg_t *hw,
                          const radio_lora_cfg_t *cfg,
                          radio_event_cb_t cb,
                          void *user_ctx);

/**
 * @brief Deinicjalizuje backend radiowy.
 * @return Kod statusu.
 */
radio_status_t radio_deinit(void);

/**
 * @brief Uruchamia odbiór ciągły.
 * @return Kod statusu.
 */
radio_status_t radio_start_rx_continuous(void);

/**
 * @brief Uruchamia odbiór pojedynczy z timeoutem symbolowym.
 * @param symbol_timeout Timeout w symbolach.
 * @return Kod statusu.
 */
radio_status_t radio_start_rx_single(uint16_t symbol_timeout);

/**
 * @brief Rozpoczyna nadawanie asynchroniczne.
 * @param data Wskaźnik na payload.
 * @param len Długość payloadu.
 * @return Kod statusu.
 */
radio_status_t radio_send_async(const uint8_t *data, uint8_t len);

/**
 * @brief Przełącza radio do trybu standby.
 * @return Kod statusu.
 */
radio_status_t radio_standby(void);

/**
 * @brief Przełącza radio do trybu sleep.
 * @return Kod statusu.
 */
radio_status_t radio_sleep(void);

/**
 * @brief Obsługuje odroczoną logikę IRQ w kontekście pętli głównej.
 */
void radio_process(void);

/**
 * @brief Zwraca i czyści aktualną maskę zdarzeń.
 * @return Maska zdarzeń.
 */
uint32_t radio_take_events(void);

/**
 * @brief Odczytuje ostatnio odebraną ramkę.
 * @param pkt [out] Struktura wyjściowa pakietu.
 * @return `true` jeśli bufor zawiera poprawne dane.
 */
bool radio_get_last_packet(radio_packet_t *pkt);

/**
 * @brief Zwraca aktualny stan backendu.
 * @return Stan pracy.
 */
radio_state_t radio_get_state(void);

/**
 * @brief Odczytuje pojedynczy rejestr transceivera.
 * @param reg Adres rejestru.
 * @param value [out] Odczytana wartość.
 * @return Kod statusu.
 */
radio_status_t radio_raw_read_reg(uint8_t reg, uint8_t *value);

/**
 * @brief Zapisuje pojedynczy rejestr transceivera.
 * @param reg Adres rejestru.
 * @param value Wartość do zapisu.
 * @return Kod statusu.
 */
radio_status_t radio_raw_write_reg(uint8_t reg, uint8_t value);

/**
 * @brief Odczytuje ciąg bajtów z rejestrów transceivera.
 * @param reg Adres startowy.
 * @param data [out] Bufor danych.
 * @param len Liczba bajtów.
 * @return Kod statusu.
 */
radio_status_t radio_raw_read_burst(uint8_t reg, uint8_t *data, uint8_t len);

/**
 * @brief Zapisuje ciąg bajtów do rejestrów transceivera.
 * @param reg Adres startowy.
 * @param data Bufor danych.
 * @param len Liczba bajtów.
 * @return Kod statusu.
 */
radio_status_t radio_raw_write_burst(uint8_t reg, const uint8_t *data, uint8_t len);

/**
 * @brief Przekazuje numer pinu EXTI do backendu radiowego.
 *
 * Użyj tej funkcji, gdy `RADIO_LIB_OWNS_HAL_EXTI_CALLBACK == 0` i callback
 * `HAL_GPIO_EXTI_Callback` jest implementowany poza biblioteką.
 *
 * @param gpio_pin Numer/maska pinu przekazana przez HAL.
 */
void radio_on_exti(uint16_t gpio_pin);

#ifdef __cplusplus
}
#endif

#endif /* APP_RADIO_LIB_RADIO_LIB_H_ */
