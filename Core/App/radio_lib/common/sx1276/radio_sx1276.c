/**
 * @file radio_sx1276.c
 * @brief Implementacja niskopoziomowego dostępu do rejestrów SX1276.
 */

#include "radio_sx1276.h"
#include "radio_sx1276_regs.h"

static bool sx1276_select(const sx1276_bus_t *bus)
{
    if ((bus == NULL) || (bus->hspi == NULL) || (bus->nss_port == NULL))
    {
        return false;
    }

    HAL_GPIO_WritePin(bus->nss_port, bus->nss_pin, GPIO_PIN_RESET);
    return true;
}

static void sx1276_deselect(const sx1276_bus_t *bus)
{
    HAL_GPIO_WritePin(bus->nss_port, bus->nss_pin, GPIO_PIN_SET);
}

bool sx1276_read_reg(const sx1276_bus_t *bus, uint8_t reg, uint8_t *value)
{
    uint8_t tx_buf[2];
    uint8_t rx_buf[2];

    if ((value == NULL) || !sx1276_select(bus))
    {
        return false;
    }

    tx_buf[0] = (uint8_t)(reg & SX1276_SPI_READ_MASK);
    tx_buf[1] = 0x00U;

    if (HAL_SPI_TransmitReceive(bus->hspi, tx_buf, rx_buf, 2U, bus->spi_timeout_ms) != HAL_OK)
    {
        sx1276_deselect(bus);
        return false;
    }

    sx1276_deselect(bus);
    *value = rx_buf[1];
    return true;
}

bool sx1276_write_reg(const sx1276_bus_t *bus, uint8_t reg, uint8_t value)
{
    uint8_t tx_buf[2];

    if (!sx1276_select(bus))
    {
        return false;
    }

    tx_buf[0] = (uint8_t)(reg | SX1276_SPI_WRITE_MASK);
    tx_buf[1] = value;

    if (HAL_SPI_Transmit(bus->hspi, tx_buf, 2U, bus->spi_timeout_ms) != HAL_OK)
    {
        sx1276_deselect(bus);
        return false;
    }

    sx1276_deselect(bus);
    return true;
}

bool sx1276_read_burst(const sx1276_bus_t *bus, uint8_t reg, uint8_t *data, uint8_t len)
{
    uint8_t addr;

    if ((data == NULL) || (len == 0U) || !sx1276_select(bus))
    {
        return false;
    }

    addr = (uint8_t)(reg & SX1276_SPI_READ_MASK);
    if (HAL_SPI_Transmit(bus->hspi, &addr, 1U, bus->spi_timeout_ms) != HAL_OK)
    {
        sx1276_deselect(bus);
        return false;
    }

    if (HAL_SPI_Receive(bus->hspi, data, len, bus->spi_timeout_ms) != HAL_OK)
    {
        sx1276_deselect(bus);
        return false;
    }

    sx1276_deselect(bus);
    return true;
}

bool sx1276_write_burst(const sx1276_bus_t *bus, uint8_t reg, const uint8_t *data, uint8_t len)
{
    uint8_t addr;

    if ((data == NULL) || (len == 0U) || !sx1276_select(bus))
    {
        return false;
    }

    addr = (uint8_t)(reg | SX1276_SPI_WRITE_MASK);
    if (HAL_SPI_Transmit(bus->hspi, &addr, 1U, bus->spi_timeout_ms) != HAL_OK)
    {
        sx1276_deselect(bus);
        return false;
    }

    if (HAL_SPI_Transmit(bus->hspi, (uint8_t *)data, len, bus->spi_timeout_ms) != HAL_OK)
    {
        sx1276_deselect(bus);
        return false;
    }

    sx1276_deselect(bus);
    return true;
}

bool sx1276_get_version(const sx1276_bus_t *bus, uint8_t *version)
{
    return sx1276_read_reg(bus, SX1276_REG_VERSION, version);
}

bool sx1276_set_op_mode(const sx1276_bus_t *bus, uint8_t op_mode)
{
    return sx1276_write_reg(bus, SX1276_REG_OP_MODE, op_mode);
}

bool sx1276_set_frequency(const sx1276_bus_t *bus, uint32_t frequency_hz)
{
    uint64_t frf;
    uint8_t frf_msb;
    uint8_t frf_mid;
    uint8_t frf_lsb;

    frf = (((uint64_t)frequency_hz) << 19) / 32000000ULL;
    frf_msb = (uint8_t)((frf >> 16) & 0xFFU);
    frf_mid = (uint8_t)((frf >> 8) & 0xFFU);
    frf_lsb = (uint8_t)(frf & 0xFFU);

    return sx1276_write_reg(bus, SX1276_REG_FRF_MSB, frf_msb) &&
           sx1276_write_reg(bus, SX1276_REG_FRF_MID, frf_mid) &&
           sx1276_write_reg(bus, SX1276_REG_FRF_LSB, frf_lsb);
}

bool sx1276_set_pa_output_power(const sx1276_bus_t *bus, int8_t tx_power_dbm)
{
    uint8_t pa_config;
    uint8_t pa_dac;
    int8_t out_power;

    if (tx_power_dbm < 2)
    {
        tx_power_dbm = 2;
    }
    else if (tx_power_dbm > 20)
    {
        tx_power_dbm = 20;
    }

    if (tx_power_dbm > 17)
    {
        pa_dac = SX1276_PA_DAC_ENABLE_20DBM;
        out_power = (int8_t)(tx_power_dbm - 5);
    }
    else
    {
        pa_dac = SX1276_PA_DAC_DISABLE_17DBM;
        out_power = (int8_t)(tx_power_dbm - 2);
    }

    pa_config = (uint8_t)(0x80U | ((uint8_t)out_power & 0x0FU));

    return sx1276_write_reg(bus, SX1276_REG_PA_DAC, pa_dac) &&
           sx1276_write_reg(bus, SX1276_REG_PA_CONFIG, pa_config);
}

bool sx1276_set_lora_modem(const sx1276_bus_t *bus,
                           uint8_t bandwidth_code,
                           uint8_t spreading_factor,
                           uint8_t coding_rate_denominator,
                           bool implicit_header,
                           bool crc_on,
                           bool low_datarate_optimize)
{
    uint8_t modem_config_1;
    uint8_t modem_config_2;
    uint8_t modem_config_3;
    uint8_t coding_rate_field;
    uint8_t detect_optimize;

    if ((bandwidth_code > 9U) ||
        (spreading_factor < 6U) || (spreading_factor > 12U) ||
        (coding_rate_denominator < 5U) || (coding_rate_denominator > 8U))
    {
        return false;
    }

    coding_rate_field = (uint8_t)(coding_rate_denominator - 4U);

    modem_config_1 = (uint8_t)((bandwidth_code << 4) |
                               (coding_rate_field << 1) |
                               (implicit_header ? 1U : 0U));

    modem_config_2 = (uint8_t)((spreading_factor << 4) |
                               (crc_on ? 0x04U : 0x00U));

    modem_config_3 = (uint8_t)((low_datarate_optimize ? 0x08U : 0x00U) | 0x04U);

    if (!sx1276_write_reg(bus, SX1276_REG_MODEM_CONFIG_1, modem_config_1) ||
        !sx1276_write_reg(bus, SX1276_REG_MODEM_CONFIG_2, modem_config_2) ||
        !sx1276_write_reg(bus, SX1276_REG_MODEM_CONFIG_3, modem_config_3))
    {
        return false;
    }

    if (!sx1276_read_reg(bus, SX1276_REG_DETECTION_OPTIMIZE, &detect_optimize))
    {
        return false;
    }

    detect_optimize = (uint8_t)(detect_optimize & 0xF8U);
    if (spreading_factor == 6U)
    {
        detect_optimize |= 0x05U;
        return sx1276_write_reg(bus, SX1276_REG_DETECTION_OPTIMIZE, detect_optimize) &&
               sx1276_write_reg(bus, SX1276_REG_DETECTION_THRESHOLD, 0x0CU);
    }

    detect_optimize |= 0x03U;
    return sx1276_write_reg(bus, SX1276_REG_DETECTION_OPTIMIZE, detect_optimize) &&
           sx1276_write_reg(bus, SX1276_REG_DETECTION_THRESHOLD, 0x0AU);
}

bool sx1276_set_symbol_timeout(const sx1276_bus_t *bus, uint16_t symbol_timeout)
{
    uint8_t modem_config_2;

    if (!sx1276_read_reg(bus, SX1276_REG_MODEM_CONFIG_2, &modem_config_2))
    {
        return false;
    }

    modem_config_2 &= (uint8_t)~0x03U;
    modem_config_2 |= (uint8_t)((symbol_timeout >> 8) & 0x03U);

    return sx1276_write_reg(bus, SX1276_REG_MODEM_CONFIG_2, modem_config_2) &&
           sx1276_write_reg(bus, SX1276_REG_SYMB_TIMEOUT_LSB, (uint8_t)(symbol_timeout & 0xFFU));
}

bool sx1276_set_preamble(const sx1276_bus_t *bus, uint16_t preamble_len)
{
    return sx1276_write_reg(bus, SX1276_REG_PREAMBLE_MSB, (uint8_t)((preamble_len >> 8) & 0xFFU)) &&
           sx1276_write_reg(bus, SX1276_REG_PREAMBLE_LSB, (uint8_t)(preamble_len & 0xFFU));
}

bool sx1276_set_sync_word(const sx1276_bus_t *bus, uint8_t sync_word)
{
    return sx1276_write_reg(bus, SX1276_REG_SYNC_WORD, sync_word);
}

bool sx1276_set_invert_iq(const sx1276_bus_t *bus, bool invert_iq)
{
    uint8_t invert_iq_reg;
    uint8_t invert_iq2_reg;

    if (!sx1276_read_reg(bus, SX1276_REG_INVERT_IQ, &invert_iq_reg))
    {
        return false;
    }

    invert_iq_reg &= (uint8_t)~0x40U;
    if (invert_iq)
    {
        invert_iq_reg = (uint8_t)(invert_iq_reg | 0x40U);
        invert_iq2_reg = 0x19U;
    }
    else
    {
        invert_iq2_reg = 0x1DU;
    }

    return sx1276_write_reg(bus, SX1276_REG_INVERT_IQ, invert_iq_reg) &&
           sx1276_write_reg(bus, SX1276_REG_INVERT_IQ2, invert_iq2_reg);
}

bool sx1276_set_fifo_base_addrs(const sx1276_bus_t *bus, uint8_t tx_base, uint8_t rx_base)
{
    return sx1276_write_reg(bus, SX1276_REG_FIFO_TX_BASE_ADDR, tx_base) &&
           sx1276_write_reg(bus, SX1276_REG_FIFO_RX_BASE_ADDR, rx_base);
}

bool sx1276_set_fifo_addr_ptr(const sx1276_bus_t *bus, uint8_t addr)
{
    return sx1276_write_reg(bus, SX1276_REG_FIFO_ADDR_PTR, addr);
}

bool sx1276_write_fifo(const sx1276_bus_t *bus, const uint8_t *data, uint8_t len)
{
    return sx1276_write_burst(bus, SX1276_REG_FIFO, data, len);
}

bool sx1276_read_fifo(const sx1276_bus_t *bus, uint8_t *data, uint8_t len)
{
    return sx1276_read_burst(bus, SX1276_REG_FIFO, data, len);
}

bool sx1276_set_payload_length(const sx1276_bus_t *bus, uint8_t len)
{
    return sx1276_write_reg(bus, SX1276_REG_PAYLOAD_LENGTH, len);
}

bool sx1276_get_rx_current_addr(const sx1276_bus_t *bus, uint8_t *addr)
{
    return sx1276_read_reg(bus, SX1276_REG_FIFO_RX_CURRENT_ADDR, addr);
}

bool sx1276_get_rx_nb_bytes(const sx1276_bus_t *bus, uint8_t *len)
{
    return sx1276_read_reg(bus, SX1276_REG_RX_NB_BYTES, len);
}

bool sx1276_get_packet_snr_raw(const sx1276_bus_t *bus, int8_t *snr_raw)
{
    uint8_t value;

    if ((snr_raw == NULL) || !sx1276_read_reg(bus, SX1276_REG_PKT_SNR_VALUE, &value))
    {
        return false;
    }

    *snr_raw = (int8_t)value;
    return true;
}

bool sx1276_get_packet_rssi_raw(const sx1276_bus_t *bus, uint8_t *rssi_raw)
{
    return sx1276_read_reg(bus, SX1276_REG_PKT_RSSI_VALUE, rssi_raw);
}

bool sx1276_get_irq_flags(const sx1276_bus_t *bus, uint8_t *flags)
{
    return sx1276_read_reg(bus, SX1276_REG_IRQ_FLAGS, flags);
}

bool sx1276_clear_irq_flags(const sx1276_bus_t *bus, uint8_t flags_to_clear)
{
    return sx1276_write_reg(bus, SX1276_REG_IRQ_FLAGS, flags_to_clear);
}

bool sx1276_map_dio(const sx1276_bus_t *bus, uint8_t dio_mapping1, uint8_t dio_mapping2)
{
    return sx1276_write_reg(bus, SX1276_REG_DIO_MAPPING_1, dio_mapping1) &&
           sx1276_write_reg(bus, SX1276_REG_DIO_MAPPING_2, dio_mapping2);
}
