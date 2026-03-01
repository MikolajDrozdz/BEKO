/**
 * @file radio_sx1276.h
 * @brief Niskopoziomowe API dostępu do rejestrów SX1276.
 */

#ifndef APP_RADIO_LIB_COMMON_SX1276_RADIO_SX1276_H_
#define APP_RADIO_LIB_COMMON_SX1276_RADIO_SX1276_H_

#include "stm32u5xx_hal.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Kontekst magistrali SPI dla układu SX1276.
 */
typedef struct
{
    SPI_HandleTypeDef *hspi; /**< Uchwyt SPI. */
    GPIO_TypeDef *nss_port;  /**< Port pinu NSS/CS. */
    uint16_t nss_pin;        /**< Pin NSS/CS. */
    uint32_t spi_timeout_ms; /**< Timeout transferu SPI. */
} sx1276_bus_t;

/**
 * @name Dostęp bazowy do rejestrów
 * @{
 */
bool sx1276_read_reg(const sx1276_bus_t *bus, uint8_t reg, uint8_t *value);
bool sx1276_write_reg(const sx1276_bus_t *bus, uint8_t reg, uint8_t value);
bool sx1276_read_burst(const sx1276_bus_t *bus, uint8_t reg, uint8_t *data, uint8_t len);
bool sx1276_write_burst(const sx1276_bus_t *bus, uint8_t reg, const uint8_t *data, uint8_t len);
/** @} */

/**
 * @name Pomocnicze operacje konfiguracyjne
 * @{
 */
bool sx1276_get_version(const sx1276_bus_t *bus, uint8_t *version);
bool sx1276_set_op_mode(const sx1276_bus_t *bus, uint8_t op_mode);
bool sx1276_set_frequency(const sx1276_bus_t *bus, uint32_t frequency_hz);
bool sx1276_set_pa_output_power(const sx1276_bus_t *bus, int8_t tx_power_dbm);
bool sx1276_set_lora_modem(const sx1276_bus_t *bus,
                           uint8_t bandwidth_code,
                           uint8_t spreading_factor,
                           uint8_t coding_rate_denominator,
                           bool implicit_header,
                           bool crc_on,
                           bool low_datarate_optimize);
bool sx1276_set_symbol_timeout(const sx1276_bus_t *bus, uint16_t symbol_timeout);
bool sx1276_set_preamble(const sx1276_bus_t *bus, uint16_t preamble_len);
bool sx1276_set_sync_word(const sx1276_bus_t *bus, uint8_t sync_word);
bool sx1276_set_invert_iq(const sx1276_bus_t *bus, bool invert_iq);
/** @} */

/**
 * @name FIFO i pakiet
 * @{
 */
bool sx1276_set_fifo_base_addrs(const sx1276_bus_t *bus, uint8_t tx_base, uint8_t rx_base);
bool sx1276_set_fifo_addr_ptr(const sx1276_bus_t *bus, uint8_t addr);
bool sx1276_write_fifo(const sx1276_bus_t *bus, const uint8_t *data, uint8_t len);
bool sx1276_read_fifo(const sx1276_bus_t *bus, uint8_t *data, uint8_t len);
bool sx1276_set_payload_length(const sx1276_bus_t *bus, uint8_t len);
bool sx1276_get_rx_current_addr(const sx1276_bus_t *bus, uint8_t *addr);
bool sx1276_get_rx_nb_bytes(const sx1276_bus_t *bus, uint8_t *len);
bool sx1276_get_packet_snr_raw(const sx1276_bus_t *bus, int8_t *snr_raw);
bool sx1276_get_packet_rssi_raw(const sx1276_bus_t *bus, uint8_t *rssi_raw);
/** @} */

/**
 * @name IRQ i mapowanie DIO
 * @{
 */
bool sx1276_get_irq_flags(const sx1276_bus_t *bus, uint8_t *flags);
bool sx1276_clear_irq_flags(const sx1276_bus_t *bus, uint8_t flags_to_clear);
bool sx1276_map_dio(const sx1276_bus_t *bus, uint8_t dio_mapping1, uint8_t dio_mapping2);
/** @} */

#endif /* APP_RADIO_LIB_COMMON_SX1276_RADIO_SX1276_H_ */
