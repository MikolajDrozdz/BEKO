/**
 * @file radio_main.h
 * @brief Application radio task, runtime profile and command API.
 */

#ifndef APP_RADIO_MAIN_H_
#define APP_RADIO_MAIN_H_

#include "radio_lib/radio_lib.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Rodziny modulacji zarządzane przez warstwę aplikacyjną.
 *
 * Wartości tych identyfikatorów są używane zarówno przez `radio_main`,
 * jak i przez menu LCD. Dzięki temu UI nie operuje na "gołych" liczbach.
 */
typedef enum
{
    RADIO_MAIN_MODULATION_LORA = 0U, /**< Profil LoRa. */
    RADIO_MAIN_MODULATION_FSK = 1U,  /**< Profil FSK/GFSK/MSK/GMSK. */
    RADIO_MAIN_MODULATION_OOK = 2U   /**< Profil OOK. */
} radio_main_modulation_t;

/**
 * @brief Encoding used for the periodic automatic ping feature.
 */
typedef enum
{
    RADIO_MAIN_AUTO_PING_FRAME = 0U, /**< Auto ping jako normalny `laviet_frame`. */
    RADIO_MAIN_AUTO_PING_RAW = 1U    /**< Auto ping jako surowy payload ASCII `PING`. */
} radio_main_auto_ping_mode_t;

/**
 * @brief Typ kształtowania sygnału dla rodziny FSK.
 */
typedef enum
{
    RADIO_MAIN_FSK_SHAPING_FSK = 0U,      /**< Czyste FSK bez filtra gaussowskiego. */
    RADIO_MAIN_FSK_SHAPING_GFSK = 1U,     /**< GFSK z filtrem gaussowskim. */
    RADIO_MAIN_FSK_SHAPING_MSK = 2U,      /**< Minimal shift keying. */
    RADIO_MAIN_FSK_SHAPING_GMSK = 3U      /**< GMSK z filtrem gaussowskim. */
} radio_main_fsk_shaping_t;

/**
 * @brief Typ filtra i współczynnik BT dla FSK/GFSK/GMSK.
 */
typedef enum
{
    RADIO_MAIN_FILTER_NONE = 0U,      /**< Brak dodatkowego filtrowania gaussowskiego. */
    RADIO_MAIN_FILTER_BT_10 = 1U,     /**< BT = 1.0. */
    RADIO_MAIN_FILTER_BT_07 = 2U,     /**< BT = 0.7. */
    RADIO_MAIN_FILTER_BT_05 = 3U,     /**< BT = 0.5. */
    RADIO_MAIN_FILTER_BT_03 = 4U      /**< BT = 0.3. */
} radio_main_filter_t;

/**
 * @brief Rodzaj CRC pakietowego zapisanego w profilu aplikacyjnym.
 *
 * LoRa na SX1276 wspiera tylko własny, wbudowany CRC payloadu ON/OFF.
 * Pozostałe typy są przechowywane w profilach FSK/OOK na potrzeby menu,
 * dokumentacji i przyszłego backendu FSK/OOK.
 */
typedef enum
{
    RADIO_MAIN_CRC_OFF = 0U,          /**< CRC wyłączone. */
    RADIO_MAIN_CRC_SX1276 = 1U,       /**< Wbudowane CRC payloadu SX1276/LoRa. */
    RADIO_MAIN_CRC_IBM = 2U,          /**< CRC-16 IBM/ANSI. */
    RADIO_MAIN_CRC_CCITT = 3U         /**< CRC-16 CCITT. */
} radio_main_crc_type_t;

/**
 * @brief Tryb nagłówka pakietu.
 */
typedef enum
{
    RADIO_MAIN_HEADER_EXPLICIT = 0U,  /**< Długość payloadu niesiona w nagłówku. */
    RADIO_MAIN_HEADER_IMPLICIT = 1U   /**< Stała długość payloadu po obu stronach. */
} radio_main_header_mode_t;

/**
 * @brief Tryb filtrowania adresu dla trybów pakietowych FSK.
 */
typedef enum
{
    RADIO_MAIN_ADDRESS_FILTER_NONE = 0U,           /**< Brak filtrowania adresowego. */
    RADIO_MAIN_ADDRESS_FILTER_NODE = 1U,           /**< Akceptuj tylko adres lokalny. */
    RADIO_MAIN_ADDRESS_FILTER_NODE_BROADCAST = 2U  /**< Akceptuj adres lokalny i broadcast. */
} radio_main_address_filter_t;

/**
 * @brief Tryb detekcji progu OOK.
 */
typedef enum
{
    RADIO_MAIN_OOK_THRESHOLD_FIXED = 0U,   /**< Stały próg RSSI/OOK. */
    RADIO_MAIN_OOK_THRESHOLD_PEAK = 1U,    /**< Próg śledzący szczyt sygnału. */
    RADIO_MAIN_OOK_THRESHOLD_AVERAGE = 2U  /**< Próg wyznaczany z uśredniania. */
} radio_main_ook_threshold_t;

/**
 * @brief Identyfikator pojedynczej opcji konfiguracyjnej.
 *
 * Menu korzysta z tego enum-a do wysyłania jednej, spójnej komendy
 * `radio_main_cmd_set_option(...)`, a `radio_main` wykonuje walidację
 * i ewentualną rekonfigurację radia w jednym miejscu.
 */
typedef enum
{
    RADIO_MAIN_OPTION_LORA_PRESET = 0U, /**< Select one of the predefined LoRa presets. */
    RADIO_MAIN_OPTION_LORA_FREQ, /**< Set LoRa carrier frequency. */
    RADIO_MAIN_OPTION_LORA_BW, /**< Set LoRa bandwidth code. */
    RADIO_MAIN_OPTION_LORA_SF, /**< Set LoRa spreading factor. */
    RADIO_MAIN_OPTION_LORA_CR, /**< Set LoRa coding-rate denominator. */
    RADIO_MAIN_OPTION_LORA_TX_POWER, /**< Set LoRa TX power. */
    RADIO_MAIN_OPTION_LORA_CRC, /**< Enable or disable LoRa payload CRC. */
    RADIO_MAIN_OPTION_LORA_PREAMBLE, /**< Set LoRa preamble length. */
    RADIO_MAIN_OPTION_LORA_HEADER_MODE, /**< Select explicit or implicit LoRa header mode. */
    RADIO_MAIN_OPTION_LORA_IQ_INVERT, /**< Enable or disable LoRa IQ inversion. */
    RADIO_MAIN_OPTION_LORA_SYNC_WORD, /**< Set LoRa sync word. */
    RADIO_MAIN_OPTION_LORA_RESET_DEFAULTS, /**< Restore default LoRa profile. */
    RADIO_MAIN_OPTION_FSK_SHAPING, /**< Select FSK/GFSK/MSK/GMSK shaping. */
    RADIO_MAIN_OPTION_FSK_FREQ, /**< Set FSK carrier frequency. */
    RADIO_MAIN_OPTION_FSK_BITRATE, /**< Set FSK bitrate. */
    RADIO_MAIN_OPTION_FSK_RX_BW, /**< Set FSK RX bandwidth code. */
    RADIO_MAIN_OPTION_FSK_FILTER, /**< Select Gaussian filter BT. */
    RADIO_MAIN_OPTION_FSK_TX_POWER, /**< Set FSK TX power. */
    RADIO_MAIN_OPTION_FSK_PREAMBLE, /**< Set FSK preamble length. */
    RADIO_MAIN_OPTION_FSK_SYNC_LEN, /**< Set FSK sync word length. */
    RADIO_MAIN_OPTION_FSK_SYNC_WORD, /**< Set FSK sync word pattern. */
    RADIO_MAIN_OPTION_FSK_ADDRESS_FILTER, /**< Set FSK address filtering mode. */
    RADIO_MAIN_OPTION_FSK_CRC, /**< Set FSK packet CRC type. */
    RADIO_MAIN_OPTION_FSK_WHITENING, /**< Enable or disable FSK data whitening. */
    RADIO_MAIN_OPTION_FSK_RESET_DEFAULTS, /**< Restore default FSK profile. */
    RADIO_MAIN_OPTION_OOK_FREQ, /**< Set OOK carrier frequency. */
    RADIO_MAIN_OPTION_OOK_BITRATE, /**< Set OOK bitrate. */
    RADIO_MAIN_OPTION_OOK_TX_POWER, /**< Set OOK TX power. */
    RADIO_MAIN_OPTION_OOK_RX_BW, /**< Set OOK RX bandwidth code. */
    RADIO_MAIN_OPTION_OOK_PREAMBLE, /**< Set OOK preamble length. */
    RADIO_MAIN_OPTION_OOK_SYNC_LEN, /**< Set OOK sync word length. */
    RADIO_MAIN_OPTION_OOK_SYNC_WORD, /**< Set OOK sync word pattern. */
    RADIO_MAIN_OPTION_OOK_THRESHOLD_TYPE, /**< Select OOK threshold algorithm. */
    RADIO_MAIN_OPTION_OOK_THRESHOLD_VALUE, /**< Set OOK raw threshold value. */
    RADIO_MAIN_OPTION_OOK_RESET_DEFAULTS /**< Restore default OOK profile. */
} radio_main_option_t;

/**
 * @brief Profil rodziny FSK/GFSK/MSK/GMSK trzymany przez warstwę aplikacji.
 */
typedef struct
{
    radio_main_fsk_shaping_t shaping;           /**< Wybrana odmiana modulacji FSK. */
    uint32_t frequency_hz;                      /**< Częstotliwość nośna [Hz]. */
    uint32_t bitrate_bps;                       /**< Bitrate [bps]. */
    radio_lora_bw_t rx_bandwidth;               /**< Umowne kodowanie pasma RX na potrzeby UI/profili. */
    radio_main_filter_t filter;                 /**< Filtr i BT dla gaussowskiego shapingu. */
    int8_t tx_power_dbm;                        /**< Moc TX [dBm]. */
    uint16_t preamble_len;                      /**< Długość preambuły [bajty/symbole]. */
    uint8_t sync_word_len;                      /**< Długość słowa synchronizacji [B]. */
    uint64_t sync_word;                         /**< Wzorzec sync word, używane najmłodsze `sync_word_len` bajtów. */
    radio_main_address_filter_t address_filter; /**< Tryb adresowania pakietów. */
    radio_main_crc_type_t crc_type;             /**< Typ CRC pakietowego. */
    bool data_whitening;                        /**< Włączenie whitening-u danych. */
} radio_main_fsk_cfg_t;

/**
 * @brief Profil rodziny OOK przechowywany przez warstwę aplikacji.
 */
typedef struct
{
    uint32_t frequency_hz;                  /**< Częstotliwość nośna [Hz]. */
    uint32_t bitrate_bps;                   /**< Bitrate [bps]. */
    radio_lora_bw_t rx_bandwidth;           /**< Umowne kodowanie szerokości pasma RX. */
    int8_t tx_power_dbm;                    /**< Moc TX [dBm]. */
    uint16_t preamble_len;                  /**< Długość preambuły [B]. */
    uint8_t sync_word_len;                  /**< Długość słowa synchronizacji [B]. */
    uint32_t sync_word;                     /**< Wzorzec sync word. */
    radio_main_ook_threshold_t threshold;   /**< Tryb progu detekcji OOK. */
    uint8_t threshold_value;                /**< Surowa wartość progu dla trybu wybranego powyżej. */
} radio_main_ook_cfg_t;

/**
 * @brief Snapshot pełnej konfiguracji radiowej widzianej przez aplikację.
 */
typedef struct
{
    radio_main_modulation_t active_modulation; /**< Aktualnie wybrana rodzina modulacji. */
    radio_lora_cfg_t lora;                     /**< Profil LoRa. */
    radio_main_fsk_cfg_t fsk;                  /**< Profil FSK/GFSK/MSK/GMSK. */
    radio_main_ook_cfg_t ook;                  /**< Profil OOK. */
    bool fh_enabled;                           /**< Flaga frequency hopping. */
    uint32_t fh_period_ms;                     /**< Okres przeskoku kanału [ms] dla FH w LoRa. */
    bool coding_enabled;                       /**< Flaga szyfrowania ruchu `OPERATOR`. */
    bool auto_ping_enabled;                    /**< Flaga automatycznego `PING`. */
    uint32_t auto_ping_period_ms;              /**< Wybrany okres automatycznego `PING` [ms]. */
    radio_main_auto_ping_mode_t auto_ping_mode; /**< Sposób kodowania automatycznego `PING`. */
} radio_main_runtime_cfg_t;

/**
 * @brief Load one predefined LoRa preset into a profile object.
 * @param preset_id Preset index.
 * @param cfg [out] Destination LoRa profile.
 */
void radio_main_load_default_lora_preset(uint8_t preset_id, radio_lora_cfg_t *cfg);

/**
 * @brief Load the default FSK/GFSK/MSK/GMSK profile.
 * @param cfg [out] Destination FSK profile.
 */
void radio_main_load_default_fsk_profile(radio_main_fsk_cfg_t *cfg);

/**
 * @brief Load the default OOK profile.
 * @param cfg [out] Destination OOK profile.
 */
void radio_main_load_default_ook_profile(radio_main_ook_cfg_t *cfg);

/**
 * @brief Create the radio task and command queue.
 */
void radio_main_create_task(void);

/**
 * @brief Queue transmission of a built-in message template.
 * @param group_id Template group identifier.
 * @param msg_id Template message identifier.
 * @param dst_id Destination node/device identifier.
 * @return `true` when the command was accepted.
 */
bool radio_main_cmd_send_template(uint8_t group_id, uint8_t msg_id, uint32_t dst_id);

/**
 * @brief Queue transmission of user-entered text.
 * @param text Null-terminated text payload.
 * @param dst_id Destination node/device identifier.
 * @return `true` when the command was accepted.
 */
bool radio_main_cmd_send_user_text(const char *text, uint32_t dst_id);

/**
 * @brief Queue transmission of raw payload bytes.
 * @param data Payload bytes.
 * @param len Payload length.
 * @return `true` when the command was accepted.
 */
bool radio_main_cmd_send_raw(const uint8_t *data, uint8_t len);

/**
 * @brief Select and apply a LoRa preset.
 * @param preset_id Preset index.
 * @return `true` when the command was accepted and applied.
 */
bool radio_main_cmd_set_lora_preset(uint8_t preset_id);

/**
 * @brief Switch active modulation family.
 * @param modulation_id One of `radio_main_modulation_t`.
 * @return `true` when the command was accepted and applied.
 */
bool radio_main_cmd_set_modulation(uint8_t modulation_id);

/**
 * @brief Set carrier frequency for the active modulation profile.
 * @param frequency_hz Frequency in Hz.
 * @return `true` when the command was accepted and applied.
 */
bool radio_main_cmd_set_modulation_freq(uint32_t frequency_hz);

/**
 * @brief Set bandwidth code for the active modulation profile.
 * @param bandwidth_code Bandwidth enum/code value.
 * @return `true` when the command was accepted and applied.
 */
bool radio_main_cmd_set_modulation_bw(uint8_t bandwidth_code);

/**
 * @brief Set one runtime radio option.
 * @param option Option identifier.
 * @param value Raw option value.
 * @return `true` when the command was accepted and applied.
 */
bool radio_main_cmd_set_option(radio_main_option_t option, uint32_t value);

/**
 * @brief Enable or disable frequency hopping.
 * @param enabled Desired FH state.
 * @return `true` when the command was accepted.
 */
bool radio_main_cmd_set_fh(bool enabled);

/**
 * @brief Set frequency-hopping period.
 * @param period_ms Hop period in milliseconds.
 * @return `true` when the command was accepted.
 */
bool radio_main_cmd_set_fh_period(uint32_t period_ms);

/**
 * @brief Enable or disable protected/coded traffic mode.
 * @param enabled Desired coding state.
 * @return `true` when the command was accepted.
 */
bool radio_main_cmd_set_coding(bool enabled);

/**
 * @brief Enable or disable automatic ping.
 * @param enabled Desired auto-ping state.
 * @return `true` when the command was accepted.
 */
bool radio_main_cmd_set_auto_ping(bool enabled);

/**
 * @brief Set automatic ping period.
 * @param period_ms Period in milliseconds.
 * @return `true` when the command was accepted.
 */
bool radio_main_cmd_set_auto_ping_period(uint32_t period_ms);

/**
 * @brief Select automatic ping encoding mode.
 * @param mode Auto-ping encoding mode.
 * @return `true` when the command was accepted.
 */
bool radio_main_cmd_set_auto_ping_mode(radio_main_auto_ping_mode_t mode);

/**
 * @brief Reset and reinitialize the radio module.
 * @return `true` when the command was accepted.
 */
bool radio_main_cmd_reset_module(void);

/**
 * @brief Start local device pairing window.
 * @param timeout_ms Pairing timeout in milliseconds.
 * @return `true` when the command was accepted.
 */
bool radio_main_cmd_start_pairing(uint32_t timeout_ms);

/**
 * @brief Start gateway/network pairing window.
 * @param timeout_ms Pairing timeout in milliseconds.
 * @return `true` when the command was accepted.
 */
bool radio_main_cmd_start_network_pairing(uint32_t timeout_ms);

/**
 * @brief Accept or reject the current pairing candidate.
 * @param accept `true` to accept, `false` to reject.
 * @return `true` when the command was accepted.
 */
bool radio_main_cmd_pairing_accept(bool accept);

/**
 * @brief Send a device pair request using the current pairing code.
 * @return `true` when the command was accepted.
 */
bool radio_main_cmd_send_pair_req(void);

/**
 * @brief Send a network/gateway pair request.
 * @return `true` when the command was accepted.
 */
bool radio_main_cmd_send_network_pair_req(void);

/**
 * @brief Send a pairing error frame to a destination.
 * @param dst_id Destination node/device identifier.
 * @return `true` when the command was accepted.
 */
bool radio_main_cmd_send_pair_error(uint32_t dst_id);

/**
 * @brief Read a snapshot of the radio runtime configuration.
 * @param cfg_out [out] Destination configuration snapshot.
 * @return `true` when a snapshot was copied.
 */
bool radio_main_get_runtime_cfg(radio_main_runtime_cfg_t *cfg_out);

/**
 * @brief Read the effective automatic ping period.
 * @param period_ms_out [out] Period in milliseconds.
 * @return `true` when the value was copied.
 */
bool radio_main_get_auto_ping_period_ms(uint32_t *period_ms_out);

/**
 * @brief Read the last short radio error message.
 * @param out Destination text buffer.
 * @param out_size Destination buffer size.
 * @return `true` when an error string was copied.
 */
bool radio_main_get_last_error_text(char *out, uint8_t out_size);

/**
 * @brief Return this node's application-level radio identifier.
 * @return Node identifier.
 */
uint32_t radio_main_get_node_id(void);

#endif /* APP_RADIO_MAIN_H_ */
