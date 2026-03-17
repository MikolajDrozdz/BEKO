/**
 * @file radio_fsk.c
 * @brief Backend modulacji FSK/GFSK/MSK/GMSK dla SX1276.
 */

#include "radio_fsk.h"

#include "app_delay.h"
#include "main.h"
#include "../../common/sx1276/radio_sx1276.h"
#include "../../common/sx1276/radio_sx1276_regs.h"
#include "../lora/radio_lora.h"

#include <string.h>

#define RADIO_FSK_FIFO_THRESH_LEVEL        31U
#define RADIO_FSK_RESET_LOW_DELAY_MS       2U
#define RADIO_FSK_RESET_HIGH_DELAY_MS      10U
#define RADIO_FSK_NODE_ADDRESS_DEFAULT     0x01U
#define RADIO_FSK_BROADCAST_ADDRESS        0xFFU
#define RADIO_FSK_TX_GUARD_MS              40UL
#define RADIO_FSK_FIFO_MAX_BYTES           64U

typedef struct
{
    bool initialized;
    sx1276_bus_t bus;
    radio_hw_cfg_t hw;
    radio_fsk_cfg_t cfg;
    radio_event_cb_t callback;
    void *callback_ctx;
    volatile uint8_t dio_pending_mask;
    volatile uint32_t event_flags;
    radio_state_t state;
    radio_state_t tx_resume_state;
    uint32_t rx_single_deadline_ms;
    uint32_t tx_deadline_ms;
    radio_packet_t last_packet;
} radio_fsk_context_t;

static radio_fsk_context_t s_radio;
static radio_fsk_cfg_t s_runtime_cfg;
static bool s_runtime_cfg_ready = false;

static uint32_t radio_irq_save(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void radio_irq_restore(uint32_t primask)
{
    if (primask == 0U)
    {
        __enable_irq();
    }
}

static void radio_set_event_flags(uint32_t events)
{
    uint32_t key = radio_irq_save();
    s_radio.event_flags |= events;
    radio_irq_restore(key);
}

static bool radio_cfg_valid(const radio_fsk_cfg_t *cfg)
{
    if ((cfg == NULL) ||
        (cfg->frequency_hz < 137000000UL) || (cfg->frequency_hz > 1020000000UL) ||
        (cfg->bitrate_bps < 600UL) || (cfg->bitrate_bps > 300000UL) ||
        ((uint8_t)cfg->rx_bandwidth > (uint8_t)RADIO_LORA_BW_500_KHZ) ||
        ((uint8_t)cfg->shaping > (uint8_t)RADIO_FSK_SHAPING_GMSK) ||
        ((uint8_t)cfg->filter > (uint8_t)RADIO_FSK_FILTER_BT_03) ||
        (cfg->preamble_len == 0U) ||
        (cfg->sync_word_len == 0U) ||
        (cfg->sync_word_len > 4U) ||
        ((uint8_t)cfg->address_filter > (uint8_t)RADIO_ADDRESS_FILTER_NODE_BROADCAST) ||
        ((uint8_t)cfg->crc_type > (uint8_t)RADIO_PACKET_CRC_IBM))
    {
        return false;
    }

    return true;
}

static void radio_hw_reset(void)
{
    HAL_GPIO_WritePin(s_radio.hw.nss.port, s_radio.hw.nss.pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(s_radio.hw.reset.port, s_radio.hw.reset.pin, GPIO_PIN_RESET);
    app_delay_ms(RADIO_FSK_RESET_LOW_DELAY_MS);
    HAL_GPIO_WritePin(s_radio.hw.reset.port, s_radio.hw.reset.pin, GPIO_PIN_SET);
    app_delay_ms(RADIO_FSK_RESET_HIGH_DELAY_MS);
}

static uint8_t radio_get_rx_bw_reg_value(radio_lora_bw_t bw)
{
    switch (bw)
    {
        case RADIO_LORA_BW_7_8_KHZ:
            return 0x06U;
        case RADIO_LORA_BW_10_4_KHZ:
            return 0x15U;
        case RADIO_LORA_BW_15_6_KHZ:
            return 0x05U;
        case RADIO_LORA_BW_20_8_KHZ:
            return 0x14U;
        case RADIO_LORA_BW_31_25_KHZ:
            return 0x04U;
        case RADIO_LORA_BW_41_7_KHZ:
            return 0x13U;
        case RADIO_LORA_BW_62_5_KHZ:
            return 0x03U;
        case RADIO_LORA_BW_125_KHZ:
            return 0x02U;
        case RADIO_LORA_BW_250_KHZ:
            return 0x01U;
        case RADIO_LORA_BW_500_KHZ:
        default:
            return 0x00U;
    }
}

static uint8_t radio_get_shaping_bits(const radio_fsk_cfg_t *cfg)
{
    if (cfg->shaping == RADIO_FSK_SHAPING_FSK)
    {
        return SX1276_OPMODE_SHAPING_NONE;
    }

    if (cfg->shaping == RADIO_FSK_SHAPING_MSK)
    {
        return SX1276_OPMODE_SHAPING_FSK_BT10;
    }

    switch (cfg->filter)
    {
        case RADIO_FSK_FILTER_BT_10:
            return SX1276_OPMODE_SHAPING_FSK_BT10;

        case RADIO_FSK_FILTER_BT_07:
        case RADIO_FSK_FILTER_BT_05:
            return SX1276_OPMODE_SHAPING_FSK_BT05;

        case RADIO_FSK_FILTER_BT_03:
            return SX1276_OPMODE_SHAPING_FSK_BT03;

        case RADIO_FSK_FILTER_NONE:
        default:
            return SX1276_OPMODE_SHAPING_NONE;
    }
}

static uint32_t radio_get_frequency_deviation(const radio_fsk_cfg_t *cfg)
{
    if ((cfg->shaping == RADIO_FSK_SHAPING_MSK) ||
        (cfg->shaping == RADIO_FSK_SHAPING_GMSK))
    {
        return (cfg->bitrate_bps / 4UL);
    }

    return (cfg->bitrate_bps / 2UL);
}

/*
 * Waits until the FSK modem reports ModeReady after leaving RX.
 * Semtech fixed a known SX127x issue around FSK transmissions started while the
 * radio was still in RX, so this helper makes the RX->STDBY transition explicit.
 */
static bool radio_set_op_mode_ready(uint8_t op_mode)
{
    uint8_t irq1;
    uint8_t retry;

    if (!sx1276_set_op_mode(&s_radio.bus, op_mode))
    {
        return false;
    }

    if ((op_mode & SX1276_OPMODE_MODE_MASK) == SX1276_MODE_SLEEP)
    {
        app_delay_ms(1U);
        return true;
    }

    for (retry = 0U; retry < 5U; retry++)
    {
        if (sx1276_read_reg(&s_radio.bus, SX1276_REG_IRQ_FLAGS_1, &irq1) &&
            ((irq1 & SX1276_IRQ1_MODE_READY) != 0U))
        {
            return true;
        }
        app_delay_ms(1U);
    }

    return false;
}

static uint8_t radio_get_packet_config_1(const radio_fsk_cfg_t *cfg)
{
    uint8_t value = SX1276_PACKET_FORMAT_VARIABLE;

    if (cfg->data_whitening)
    {
        value |= SX1276_PACKET_DC_FREE_WHITENING;
    }

    if (cfg->crc_type != RADIO_PACKET_CRC_OFF)
    {
        value |= SX1276_PACKET_CRC_ON;
        if (cfg->crc_type == RADIO_PACKET_CRC_IBM)
        {
            value |= SX1276_PACKET_CRC_WHITENING_IBM;
        }
        else
        {
            value |= SX1276_PACKET_CRC_WHITENING_CCITT;
        }
    }

    switch (cfg->address_filter)
    {
        case RADIO_ADDRESS_FILTER_NODE:
            value |= SX1276_PACKET_ADDR_FILTER_NODE;
            break;

        case RADIO_ADDRESS_FILTER_NODE_BROADCAST:
            value |= SX1276_PACKET_ADDR_FILTER_NODE_BC;
            break;

        case RADIO_ADDRESS_FILTER_OFF:
        default:
            value |= SX1276_PACKET_ADDR_FILTER_OFF;
            break;
    }

    return value;
}

static bool radio_write_sync_word(const radio_fsk_cfg_t *cfg)
{
    uint8_t sync_config;
    uint8_t sync_bytes[4];
    uint8_t i;

    sync_config = (uint8_t)(SX1276_SYNC_AUTO_RESTART_PLL |
                            SX1276_SYNC_ON |
                            SX1276_SYNC_FIFOFILL_AUTO |
                            ((cfg->sync_word_len - 1U) & 0x07U));
    for (i = 0U; i < cfg->sync_word_len; i++)
    {
        uint8_t shift = (uint8_t)(((cfg->sync_word_len - 1U - i) * 8U) & 0x1FU);
        sync_bytes[i] = (uint8_t)((cfg->sync_word >> shift) & 0xFFU);
    }

    return sx1276_write_reg(&s_radio.bus, SX1276_REG_SYNC_CONFIG, sync_config) &&
           sx1276_write_burst(&s_radio.bus, SX1276_REG_SYNC_VALUE_1, sync_bytes, cfg->sync_word_len);
}

static bool radio_apply_fsk_config(const radio_fsk_cfg_t *cfg)
{
    uint32_t bitrate_reg;
    uint32_t fdev_reg;
    uint8_t op_mode;
    uint8_t rx_bw;

    bitrate_reg = (32000000UL + (cfg->bitrate_bps / 2UL)) / cfg->bitrate_bps;
    fdev_reg = (uint32_t)((((uint64_t)radio_get_frequency_deviation(cfg)) << 19) / 32000000ULL);
    rx_bw = radio_get_rx_bw_reg_value(cfg->rx_bandwidth);
    op_mode = (uint8_t)(SX1276_OPMODE_MODULATION_FSK |
                        radio_get_shaping_bits(cfg) |
                        SX1276_MODE_SLEEP);

    return radio_set_op_mode_ready(op_mode) &&
           sx1276_set_frequency(&s_radio.bus, cfg->frequency_hz) &&
           sx1276_set_pa_output_power(&s_radio.bus, cfg->tx_power_dbm) &&
           sx1276_write_reg(&s_radio.bus, SX1276_REG_BITRATE_MSB, (uint8_t)((bitrate_reg >> 8) & 0xFFU)) &&
           sx1276_write_reg(&s_radio.bus, SX1276_REG_BITRATE_LSB, (uint8_t)(bitrate_reg & 0xFFU)) &&
           sx1276_write_reg(&s_radio.bus, SX1276_REG_FDEV_MSB, (uint8_t)((fdev_reg >> 8) & 0x3FU)) &&
           sx1276_write_reg(&s_radio.bus, SX1276_REG_FDEV_LSB, (uint8_t)(fdev_reg & 0xFFU)) &&
           sx1276_write_reg(&s_radio.bus, SX1276_REG_RX_BW, rx_bw) &&
           sx1276_write_reg(&s_radio.bus, SX1276_REG_AFC_BW, rx_bw) &&
           sx1276_write_reg(&s_radio.bus, SX1276_REG_PREAMBLE_DETECT, 0xAAU) &&
           sx1276_write_reg(&s_radio.bus, SX1276_REG_RX_CONFIG,
                            (uint8_t)(SX1276_RXCONFIG_AFC_AUTO_ON |
                                      SX1276_RXCONFIG_AGC_AUTO_ON |
                                      SX1276_RXCONFIG_TRIGGER_RSSI)) &&
           sx1276_write_reg(&s_radio.bus, SX1276_REG_PREAMBLE_MSB_FSK, (uint8_t)((cfg->preamble_len >> 8) & 0xFFU)) &&
           sx1276_write_reg(&s_radio.bus, SX1276_REG_PREAMBLE_LSB_FSK, (uint8_t)(cfg->preamble_len & 0xFFU)) &&
           radio_write_sync_word(cfg) &&
           sx1276_write_reg(&s_radio.bus, SX1276_REG_PACKET_CONFIG_1, radio_get_packet_config_1(cfg)) &&
           sx1276_write_reg(&s_radio.bus, SX1276_REG_PACKET_CONFIG_2, SX1276_PACKET_DATA_MODE_PACKET) &&
           sx1276_write_reg(&s_radio.bus, SX1276_REG_PAYLOAD_LENGTH_FSK, RADIO_LIB_MAX_PAYLOAD) &&
           sx1276_write_reg(&s_radio.bus, SX1276_REG_NODE_ADDRESS, RADIO_FSK_NODE_ADDRESS_DEFAULT) &&
           sx1276_write_reg(&s_radio.bus, SX1276_REG_BROADCAST_ADDRESS, RADIO_FSK_BROADCAST_ADDRESS) &&
           sx1276_write_reg(&s_radio.bus, SX1276_REG_FIFO_THRESH,
                            (uint8_t)(SX1276_FIFO_THRESH_TX_START_NOT_EMPTY |
                                      (RADIO_FSK_FIFO_THRESH_LEVEL & 0x3FU))) &&
           sx1276_write_reg(&s_radio.bus, SX1276_REG_DIO_MAPPING_1, 0x00U) &&
           sx1276_write_reg(&s_radio.bus, SX1276_REG_DIO_MAPPING_2, 0x00U) &&
           radio_set_op_mode_ready((uint8_t)(SX1276_OPMODE_MODULATION_FSK |
                                             radio_get_shaping_bits(cfg) |
                                             SX1276_MODE_STDBY));
}

static void radio_set_state(radio_state_t state)
{
    s_radio.state = state;
}

static void radio_resume_after_tx(void)
{
    if (s_radio.tx_resume_state == RADIO_STATE_RX_CONT)
    {
        (void)sx1276_set_op_mode(&s_radio.bus,
                                 (uint8_t)(SX1276_OPMODE_MODULATION_FSK |
                                           radio_get_shaping_bits(&s_radio.cfg) |
                                           SX1276_MODE_RXCONTINUOUS));
        radio_set_state(RADIO_STATE_RX_CONT);
    }
    else if (s_radio.tx_resume_state == RADIO_STATE_RX_SINGLE)
    {
        (void)sx1276_set_op_mode(&s_radio.bus,
                                 (uint8_t)(SX1276_OPMODE_MODULATION_FSK |
                                           radio_get_shaping_bits(&s_radio.cfg) |
                                           SX1276_MODE_RXSINGLE));
        radio_set_state(RADIO_STATE_RX_SINGLE);
    }
    else
    {
        (void)sx1276_set_op_mode(&s_radio.bus,
                                 (uint8_t)(SX1276_OPMODE_MODULATION_FSK |
                                           radio_get_shaping_bits(&s_radio.cfg) |
                                           SX1276_MODE_STDBY));
        radio_set_state(RADIO_STATE_STANDBY);
    }
}

static uint32_t radio_tx_timeout_ms(uint8_t payload_len)
{
    uint64_t timeout_ms;
    uint32_t total_bytes;
    uint32_t crc_len = (s_radio.cfg.crc_type == RADIO_PACKET_CRC_OFF) ? 0UL : 2UL;

    total_bytes = (uint32_t)s_radio.cfg.preamble_len +
                  (uint32_t)s_radio.cfg.sync_word_len +
                  (uint32_t)payload_len +
                  crc_len +
                  1UL + /* variable-length byte */
                  2UL;  /* guard for packet engine overhead */
    timeout_ms = ((uint64_t)total_bytes * 8ULL * 1000ULL) / s_radio.cfg.bitrate_bps;
    if ((((uint64_t)total_bytes * 8ULL * 1000ULL) % s_radio.cfg.bitrate_bps) != 0ULL)
    {
        timeout_ms++;
    }

    timeout_ms += RADIO_FSK_TX_GUARD_MS;
    if (timeout_ms < RADIO_FSK_TX_GUARD_MS)
    {
        timeout_ms = RADIO_FSK_TX_GUARD_MS;
    }
    if (timeout_ms > 0xFFFFFFFFULL)
    {
        return 0xFFFFFFFFUL;
    }

    return (uint32_t)timeout_ms;
}

static uint32_t radio_rx_single_timeout_ms(uint32_t symbol_timeout)
{
    uint64_t timeout_ms;

    timeout_ms = ((uint64_t)symbol_timeout * 1000ULL) / s_radio.cfg.bitrate_bps;
    if ((((uint64_t)symbol_timeout * 1000ULL) % s_radio.cfg.bitrate_bps) != 0ULL)
    {
        timeout_ms++;
    }

    timeout_ms += 2ULL;
    if (timeout_ms == 0ULL)
    {
        timeout_ms = 1ULL;
    }

    if (timeout_ms > 0xFFFFFFFFULL)
    {
        return 0xFFFFFFFFUL;
    }

    return (uint32_t)timeout_ms;
}

static void radio_handle_exti_pin(uint16_t pin)
{
    uint8_t i;

    if (!s_radio.initialized)
    {
        return;
    }

    for (i = 0U; i < 6U; i++)
    {
        if (s_radio.hw.dio[i].pin == pin)
        {
            uint32_t key = radio_irq_save();
            s_radio.dio_pending_mask |= (uint8_t)(1U << i);
            radio_irq_restore(key);
            break;
        }
    }
}

static radio_status_t radio_read_rx_packet(void)
{
    uint8_t packet_len;
    uint8_t rssi_raw;

    if (!sx1276_read_burst(&s_radio.bus, SX1276_REG_FIFO, &packet_len, 1U))
    {
        return RADIO_EHW;
    }

    if (packet_len > RADIO_LIB_MAX_PAYLOAD)
    {
        s_radio.last_packet.valid = false;
        return RADIO_EBUS;
    }

    if ((packet_len > 0U) &&
        !sx1276_read_burst(&s_radio.bus, SX1276_REG_FIFO, s_radio.last_packet.data, packet_len))
    {
        return RADIO_EHW;
    }

    if (!sx1276_read_reg(&s_radio.bus, SX1276_REG_RSSI_VALUE_FSK, &rssi_raw))
    {
        return RADIO_EHW;
    }

    s_radio.last_packet.length = packet_len;
    s_radio.last_packet.rssi_dbm = (int16_t)(-(int16_t)rssi_raw / 2);
    s_radio.last_packet.snr_db = 0;
    s_radio.last_packet.timestamp_ms = HAL_GetTick();
    s_radio.last_packet.valid = true;
    return RADIO_OK;
}

void radio_fsk_default_hw_cfg(radio_hw_cfg_t *cfg, SPI_HandleTypeDef *hspi)
{
    radio_lora_default_hw_cfg(cfg, hspi);
}

void radio_fsk_default_lora_cfg(radio_lora_cfg_t *cfg)
{
    radio_lora_default_lora_cfg(cfg);
}

radio_status_t radio_fsk_set_runtime_cfg(const radio_fsk_cfg_t *cfg)
{
    if (!radio_cfg_valid(cfg))
    {
        return RADIO_EINVAL;
    }

    s_runtime_cfg = *cfg;
    s_runtime_cfg_ready = true;
    return RADIO_OK;
}

radio_status_t radio_fsk_init(const radio_hw_cfg_t *hw,
                              const radio_lora_cfg_t *cfg,
                              radio_event_cb_t cb,
                              void *user_ctx)
{
    uint8_t version;

    (void)cfg;

    if ((hw == NULL) || (hw->hspi == NULL) ||
        (hw->nss.port == NULL) || (hw->reset.port == NULL))
    {
        return RADIO_EINVAL;
    }

    if (!s_runtime_cfg_ready)
    {
        radio_default_fsk_cfg(&s_runtime_cfg);
        s_runtime_cfg_ready = true;
    }

    if (!radio_cfg_valid(&s_runtime_cfg))
    {
        return RADIO_EINVAL;
    }

    memset(&s_radio, 0, sizeof(s_radio));
    s_radio.hw = *hw;
    s_radio.cfg = s_runtime_cfg;
    s_radio.callback = cb;
    s_radio.callback_ctx = user_ctx;
    s_radio.bus.hspi = hw->hspi;
    s_radio.bus.nss_port = hw->nss.port;
    s_radio.bus.nss_pin = hw->nss.pin;
    s_radio.bus.spi_timeout_ms = hw->spi_timeout_ms;
    s_radio.state = RADIO_STATE_UNINIT;

    radio_hw_reset();

    if (!radio_set_op_mode_ready((uint8_t)(SX1276_OPMODE_MODULATION_FSK | SX1276_MODE_SLEEP)))
    {
        return RADIO_EHW;
    }

    app_delay_ms(1U);

    if (!sx1276_get_version(&s_radio.bus, &version) || (version != SX1276_VERSION_ID))
    {
        return RADIO_EHW;
    }

    if (!radio_apply_fsk_config(&s_radio.cfg))
    {
        return RADIO_EHW;
    }

    s_radio.initialized = true;
    s_radio.state = RADIO_STATE_STANDBY;
    return RADIO_OK;
}

radio_status_t radio_fsk_deinit(void)
{
    if (!s_radio.initialized)
    {
        return RADIO_ESTATE;
    }

    (void)radio_fsk_sleep();
    memset(&s_radio, 0, sizeof(s_radio));
    s_radio.state = RADIO_STATE_UNINIT;
    return RADIO_OK;
}

radio_status_t radio_fsk_start_rx_continuous(void)
{
    if (!s_radio.initialized)
    {
        return RADIO_ESTATE;
    }

    if (s_radio.state == RADIO_STATE_TX)
    {
        return RADIO_EBUS;
    }

    if (!sx1276_write_reg(&s_radio.bus, SX1276_REG_DIO_MAPPING_1, 0x00U) ||
        !sx1276_set_op_mode(&s_radio.bus,
                            (uint8_t)(SX1276_OPMODE_MODULATION_FSK |
                                      radio_get_shaping_bits(&s_radio.cfg) |
                                      SX1276_MODE_RXCONTINUOUS)))
    {
        return RADIO_EHW;
    }

    radio_set_state(RADIO_STATE_RX_CONT);
    return RADIO_OK;
}

radio_status_t radio_fsk_start_rx_single(uint16_t symbol_timeout)
{
    if (!s_radio.initialized)
    {
        return RADIO_ESTATE;
    }

    if (s_radio.state == RADIO_STATE_TX)
    {
        return RADIO_EBUS;
    }
    if (symbol_timeout == 0U)
    {
        return RADIO_EINVAL;
    }

    if (!sx1276_write_reg(&s_radio.bus, SX1276_REG_DIO_MAPPING_1, 0x00U) ||
        !sx1276_set_op_mode(&s_radio.bus,
                            (uint8_t)(SX1276_OPMODE_MODULATION_FSK |
                                      radio_get_shaping_bits(&s_radio.cfg) |
                                      SX1276_MODE_RXSINGLE)))
    {
        return RADIO_EHW;
    }

    s_radio.rx_single_deadline_ms = HAL_GetTick() + radio_rx_single_timeout_ms(symbol_timeout);
    radio_set_state(RADIO_STATE_RX_SINGLE);
    return RADIO_OK;
}

radio_status_t radio_fsk_send_async(const uint8_t *data, uint8_t len)
{
    uint8_t fifo_buf[RADIO_LIB_MAX_PAYLOAD + 1U];

    if (!s_radio.initialized)
    {
        return RADIO_ESTATE;
    }

    if ((data == NULL) || (len == 0U) || (len > RADIO_LIB_MAX_PAYLOAD))
    {
        return RADIO_EINVAL;
    }
    if (((uint16_t)len + 1U) > RADIO_FSK_FIFO_MAX_BYTES)
    {
        /* Chunked FIFO TX is not implemented in this backend yet. */
        return RADIO_EINVAL;
    }

    if (s_radio.state == RADIO_STATE_TX)
    {
        return RADIO_EBUS;
    }

    fifo_buf[0] = len;
    memcpy(&fifo_buf[1], data, len);
    s_radio.tx_resume_state = s_radio.state;

    if (!radio_set_op_mode_ready((uint8_t)(SX1276_OPMODE_MODULATION_FSK |
                                           radio_get_shaping_bits(&s_radio.cfg) |
                                           SX1276_MODE_STDBY)) ||
        !sx1276_write_reg(&s_radio.bus, SX1276_REG_DIO_MAPPING_1, 0x00U) ||
        !sx1276_write_burst(&s_radio.bus, SX1276_REG_FIFO, fifo_buf, (uint8_t)(len + 1U)) ||
        !sx1276_set_op_mode(&s_radio.bus,
                            (uint8_t)(SX1276_OPMODE_MODULATION_FSK |
                                      radio_get_shaping_bits(&s_radio.cfg) |
                                      SX1276_MODE_TX)))
    {
        return RADIO_EHW;
    }

    {
        uint32_t key = radio_irq_save();
        s_radio.dio_pending_mask = 0U;
        radio_irq_restore(key);
    }
    radio_set_state(RADIO_STATE_TX);
    s_radio.tx_deadline_ms = HAL_GetTick() + radio_tx_timeout_ms(len);
    return RADIO_OK;
}

radio_status_t radio_fsk_standby(void)
{
    if (!s_radio.initialized)
    {
        return RADIO_ESTATE;
    }

    if (!radio_set_op_mode_ready((uint8_t)(SX1276_OPMODE_MODULATION_FSK |
                                           radio_get_shaping_bits(&s_radio.cfg) |
                                           SX1276_MODE_STDBY)))
    {
        return RADIO_EHW;
    }

    radio_set_state(RADIO_STATE_STANDBY);
    return RADIO_OK;
}

radio_status_t radio_fsk_sleep(void)
{
    if (!s_radio.initialized)
    {
        return RADIO_ESTATE;
    }

    if (!radio_set_op_mode_ready((uint8_t)(SX1276_OPMODE_MODULATION_FSK |
                                           radio_get_shaping_bits(&s_radio.cfg) |
                                           SX1276_MODE_SLEEP)))
    {
        return RADIO_EHW;
    }

    radio_set_state(RADIO_STATE_STANDBY);
    return RADIO_OK;
}

void radio_fsk_process(void)
{
    uint8_t pending;
    uint8_t irq1 = 0U;
    uint8_t irq2 = 0U;
    uint32_t events = RADIO_EVENT_NONE;
    uint32_t key;

    if (!s_radio.initialized)
    {
        return;
    }

    key = radio_irq_save();
    pending = s_radio.dio_pending_mask;
    s_radio.dio_pending_mask = 0U;
    radio_irq_restore(key);

    if ((pending == 0U) &&
        (s_radio.hw.dio[0].port != NULL) &&
        (HAL_GPIO_ReadPin(s_radio.hw.dio[0].port, s_radio.hw.dio[0].pin) == GPIO_PIN_SET))
    {
        pending |= 0x01U;
    }

    if ((pending == 0U) && (s_radio.state != RADIO_STATE_TX))
    {
        if ((s_radio.state == RADIO_STATE_RX_SINGLE) &&
            ((int32_t)(HAL_GetTick() - s_radio.rx_single_deadline_ms) >= 0))
        {
            events |= RADIO_EVENT_RX_TIMEOUT;
            (void)radio_fsk_standby();
        }
        else
        {
            return;
        }
    }

    if ((pending == 0U) &&
        (s_radio.state == RADIO_STATE_TX) &&
        ((int32_t)(HAL_GetTick() - s_radio.tx_deadline_ms) >= 0))
    {
        (void)radio_fsk_standby();
        radio_resume_after_tx();
        events |= RADIO_EVENT_HW_ERROR;
        goto done;
    }

    if (!sx1276_read_reg(&s_radio.bus, SX1276_REG_IRQ_FLAGS_1, &irq1) ||
        !sx1276_read_reg(&s_radio.bus, SX1276_REG_IRQ_FLAGS_2, &irq2))
    {
        events |= RADIO_EVENT_HW_ERROR;
        goto done;
    }

    if ((irq2 & SX1276_IRQ2_FIFO_OVERRUN) != 0U)
    {
        events |= RADIO_EVENT_FIFO_OVERRUN;
    }

    if ((irq1 & SX1276_IRQ1_TIMEOUT) != 0U)
    {
        events |= RADIO_EVENT_RX_TIMEOUT;
        if (s_radio.state == RADIO_STATE_RX_SINGLE)
        {
            (void)radio_fsk_standby();
        }
    }

    if ((irq2 & SX1276_IRQ2_PAYLOAD_READY) != 0U)
    {
        if ((s_radio.cfg.crc_type != RADIO_PACKET_CRC_OFF) &&
            ((irq2 & SX1276_IRQ2_CRC_OK) == 0U))
        {
            events |= RADIO_EVENT_CRC_ERR;
        }
        else
        {
            radio_status_t status = radio_read_rx_packet();
            if (status == RADIO_OK)
            {
                events |= RADIO_EVENT_RX_DONE;
            }
            else if (status == RADIO_EBUS)
            {
                events |= RADIO_EVENT_FIFO_OVERRUN;
            }
            else
            {
                events |= RADIO_EVENT_HW_ERROR;
            }
        }

        if (s_radio.state == RADIO_STATE_RX_SINGLE)
        {
            (void)radio_fsk_standby();
        }
    }

    if ((irq2 & SX1276_IRQ2_PACKET_SENT) != 0U)
    {
        events |= RADIO_EVENT_TX_DONE;
        radio_resume_after_tx();
        s_radio.tx_deadline_ms = 0U;
    }

done:
    if (events != RADIO_EVENT_NONE)
    {
        radio_set_event_flags(events);
        if (s_radio.callback != NULL)
        {
            s_radio.callback(events, s_radio.callback_ctx);
        }
    }
}

uint32_t radio_fsk_take_events(void)
{
    uint32_t events;
    uint32_t key = radio_irq_save();

    events = s_radio.event_flags;
    s_radio.event_flags = 0U;
    radio_irq_restore(key);
    return events;
}

bool radio_fsk_get_last_packet(radio_packet_t *pkt)
{
    if ((pkt == NULL) || !s_radio.last_packet.valid)
    {
        return false;
    }

    *pkt = s_radio.last_packet;
    return true;
}

radio_state_t radio_fsk_get_state(void)
{
    return s_radio.state;
}

radio_status_t radio_fsk_raw_read_reg(uint8_t reg, uint8_t *value)
{
    if (!s_radio.initialized)
    {
        return RADIO_ESTATE;
    }

    return sx1276_read_reg(&s_radio.bus, reg, value) ? RADIO_OK : RADIO_EHW;
}

radio_status_t radio_fsk_raw_write_reg(uint8_t reg, uint8_t value)
{
    if (!s_radio.initialized)
    {
        return RADIO_ESTATE;
    }

    return sx1276_write_reg(&s_radio.bus, reg, value) ? RADIO_OK : RADIO_EHW;
}

radio_status_t radio_fsk_raw_read_burst(uint8_t reg, uint8_t *data, uint8_t len)
{
    if (!s_radio.initialized)
    {
        return RADIO_ESTATE;
    }

    if ((data == NULL) || (len == 0U))
    {
        return RADIO_EINVAL;
    }

    return sx1276_read_burst(&s_radio.bus, reg, data, len) ? RADIO_OK : RADIO_EHW;
}

radio_status_t radio_fsk_raw_write_burst(uint8_t reg, const uint8_t *data, uint8_t len)
{
    if (!s_radio.initialized)
    {
        return RADIO_ESTATE;
    }

    if ((data == NULL) || (len == 0U))
    {
        return RADIO_EINVAL;
    }

    return sx1276_write_burst(&s_radio.bus, reg, data, len) ? RADIO_OK : RADIO_EHW;
}

void radio_fsk_on_exti(uint16_t gpio_pin)
{
    radio_handle_exti_pin(gpio_pin);
}
