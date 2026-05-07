/**
 * @file radio_lib.h
 * @brief Główne API biblioteki radiowej.
 *
 * Ten plik udostępnia jednolite API dla aplikacji. Domyślny backend nadal może
 * być wskazany kompilacyjnie przez `radio_lib_config.h`, ale warstwa aplikacji
 * może go również przełączyć w runtime przez `radio_select_backend(...)`.
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
 * @brief Kształtowanie widma dla rodziny FSK.
 */
typedef enum
{
    RADIO_FSK_SHAPING_FSK = 0U,   /**< Klasyczne FSK bez filtra gaussowskiego. */
    RADIO_FSK_SHAPING_GFSK,       /**< GFSK z filtrem gaussowskim. */
    RADIO_FSK_SHAPING_MSK,        /**< MSK. */
    RADIO_FSK_SHAPING_GMSK        /**< GMSK. */
} radio_fsk_shaping_t;

/**
 * @brief Ustawienie filtra BT dla wariantów gaussowskich FSK.
 */
typedef enum
{
    RADIO_FSK_FILTER_NONE = 0U,
    RADIO_FSK_FILTER_BT_10,
    RADIO_FSK_FILTER_BT_07,
    RADIO_FSK_FILTER_BT_05,
    RADIO_FSK_FILTER_BT_03
} radio_fsk_filter_t;

/**
 * @brief Typ CRC pakietowego dla backendów packet-mode.
 */
typedef enum
{
    RADIO_PACKET_CRC_OFF = 0U,
    RADIO_PACKET_CRC_CCITT,
    RADIO_PACKET_CRC_IBM
} radio_packet_crc_t;

/**
 * @brief Tryb filtrowania adresowego dla packet engine FSK/OOK.
 */
typedef enum
{
    RADIO_ADDRESS_FILTER_OFF = 0U,
    RADIO_ADDRESS_FILTER_NODE,
    RADIO_ADDRESS_FILTER_NODE_BROADCAST
} radio_address_filter_t;

/**
 * @brief Tryb progu detekcji OOK.
 */
typedef enum
{
    RADIO_OOK_THRESHOLD_FIXED = 0U,
    RADIO_OOK_THRESHOLD_PEAK,
    RADIO_OOK_THRESHOLD_AVERAGE
} radio_ook_threshold_t;

/**
 * @brief Konfiguracja backendu FSK/GFSK/MSK/GMSK.
 */
typedef struct
{
    uint32_t frequency_hz;                /**< Częstotliwość RF [Hz]. */
    uint32_t bitrate_bps;                 /**< Bitrate [bps]. */
    radio_lora_bw_t rx_bandwidth;         /**< Szerokość pasma RX zakodowana tym samym enumem co w LoRa. */
    radio_fsk_shaping_t shaping;          /**< Odmiana/kanał shapingu modulacji. */
    radio_fsk_filter_t filter;            /**< Typ filtra BT dla wariantów gaussowskich. */
    int8_t tx_power_dbm;                  /**< Moc TX [dBm]. */
    uint16_t preamble_len;                /**< Długość preambuły [B]. */
    uint8_t sync_word_len;                /**< Długość słowa synchronizacji [B]. */
    uint32_t sync_word;                   /**< Wzorzec sync word, używane najmłodsze `sync_word_len` bajtów. */
    radio_address_filter_t address_filter;/**< Filtrowanie adresowe packet engine. */
    radio_packet_crc_t crc_type;          /**< Typ CRC packet engine. */
    bool data_whitening;                  /**< Włączenie whitening-u danych. */
} radio_fsk_cfg_t;

/**
 * @brief Konfiguracja backendu OOK.
 */
typedef struct
{
    uint32_t frequency_hz;             /**< Częstotliwość nośna [Hz]. */
    uint32_t bitrate_bps;              /**< Bitrate [bps]. */
    radio_lora_bw_t rx_bandwidth;      /**< Szerokość pasma RX. */
    int8_t tx_power_dbm;               /**< Moc TX [dBm]. */
    uint16_t preamble_len;             /**< Długość preambuły [B]. */
    uint8_t sync_word_len;             /**< Długość słowa synchronizacji [B]. */
    uint32_t sync_word;                /**< Wzorzec sync word, używane najmłodsze `sync_word_len` bajtów. */
    radio_ook_threshold_t threshold;   /**< Tryb progu OOK. */
    uint8_t threshold_value;           /**< Surowa wartość progu zależna od trybu. */
} radio_ook_cfg_t;

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
 * @brief Wypełnia domyślny profil FSK/GFSK/MSK/GMSK.
 * @param cfg [out] Struktura konfiguracji FSK do uzupełnienia.
 */
void radio_default_fsk_cfg(radio_fsk_cfg_t *cfg);

/**
 * @brief Wypełnia domyślny profil OOK.
 * @param cfg [out] Struktura konfiguracji OOK do uzupełnienia.
 */
void radio_default_ook_cfg(radio_ook_cfg_t *cfg);

/**
 * @brief Wybiera backend modulacji używany przez wspólne API w runtime.
 * @param modulation Jedna ze stałych `RADIO_LIB_MODULATION_*`.
 */
void radio_select_backend(uint8_t modulation);

/**
 * @brief Zwraca backend aktualnie wybrany do dispatchu runtime.
 * @return Jedna ze stałych `RADIO_LIB_MODULATION_*`.
 */
uint8_t radio_get_backend(void);

/**
 * @brief Przekazuje konfigurację runtime dla backendu FSK.
 * @param cfg Konfiguracja FSK do zapamiętania przed `radio_init(...)`.
 * @return Kod statusu.
 */
radio_status_t radio_set_fsk_cfg(const radio_fsk_cfg_t *cfg);

/**
 * @brief Przekazuje konfigurację runtime dla backendu OOK.
 * @param cfg Konfiguracja OOK do zapamiętania przed `radio_init(...)`.
 * @return Kod statusu.
 */
radio_status_t radio_set_ook_cfg(const radio_ook_cfg_t *cfg);

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
