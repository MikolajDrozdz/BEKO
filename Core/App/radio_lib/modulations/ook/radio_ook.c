/**
 * @file radio_ook.c
 * @brief Backend modulacji OOK dla SX1276.
 */

#include "radio_ook.h"

#include "app_delay.h"
#include "main.h"
#include "../../common/sx1276/radio_sx1276.h"
#include "../../common/sx1276/radio_sx1276_regs.h"
#include "../lora/radio_lora.h"

#include <string.h>

#define RADIO_OOK_FIFO_THRESH_LEVEL        31U
#define RADIO_OOK_RESET_LOW_DELAY_MS       2U
#define RADIO_OOK_RESET_HIGH_DELAY_MS      10U
#define RADIO_OOK_TX_GUARD_MS              40UL
#define RADIO_OOK_FIFO_MAX_BYTES           64U
typedef struct
{
    bool initialized;
    sx1276_bus_t bus;
    radio_hw_cfg_t hw;
    radio_ook_cfg_t cfg;
    radio_event_cb_t callback;
    void *callback_ctx;
    volatile uint8_t dio_pending_mask;
    volatile uint32_t event_flags;
    radio_state_t state;
    radio_state_t tx_resume_state;
    uint32_t rx_single_deadline_ms;
    uint32_t tx_deadline_ms;
    radio_packet_t last_packet;
} radio_ook_context_t;

static radio_ook_context_t s_radio;
static radio_ook_cfg_t s_runtime_cfg;
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

static bool radio_cfg_valid(const radio_ook_cfg_t *cfg)
{
    if ((cfg == NULL) ||
        (cfg->frequency_hz < 137000000UL) || (cfg->frequency_hz > 1020000000UL) ||
        (cfg->bitrate_bps < 600UL) || (cfg->bitrate_bps > 300000UL) ||
        ((uint8_t)cfg->rx_bandwidth > (uint8_t)RADIO_LORA_BW_500_KHZ) ||
        (cfg->preamble_len == 0U) ||
        (cfg->sync_word_len > 4U) ||
        ((uint8_t)cfg->threshold > (uint8_t)RADIO_OOK_THRESHOLD_AVERAGE))
    {
        return false;
    }

    return true;
}

static void radio_hw_reset(void)
{
    HAL_GPIO_WritePin(s_radio.hw.nss.port, s_radio.hw.nss.pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(s_radio.hw.reset.port, s_radio.hw.reset.pin, GPIO_PIN_RESET);
    app_delay_ms(RADIO_OOK_RESET_LOW_DELAY_MS);
    HAL_GPIO_WritePin(s_radio.hw.reset.port, s_radio.hw.reset.pin, GPIO_PIN_SET);
    app_delay_ms(RADIO_OOK_RESET_HIGH_DELAY_MS);
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

static bool radio_write_sync_word(const radio_ook_cfg_t *cfg)
{
    uint8_t sync_bytes[4];
    uint8_t i;

    if (cfg->sync_word_len == 0U)
    {
        return sx1276_write_reg(&s_radio.bus, SX1276_REG_SYNC_CONFIG, 0x00U);
    }

    for (i = 0U; i < cfg->sync_word_len; i++)
    {
        uint8_t shift = (uint8_t)(((cfg->sync_word_len - 1U - i) * 8U) & 0x1FU);
        sync_bytes[i] = (uint8_t)((cfg->sync_word >> shift) & 0xFFU);
    }

    return sx1276_write_reg(&s_radio.bus, SX1276_REG_SYNC_CONFIG,
                            (uint8_t)(SX1276_SYNC_ON |
                                      SX1276_SYNC_FIFOFILL_AUTO |
                                      ((cfg->sync_word_len - 1U) & 0x07U))) &&
           sx1276_write_burst(&s_radio.bus, SX1276_REG_SYNC_VALUE_1, sync_bytes, cfg->sync_word_len);
}

/*
 * Waits for ModeReady after OOK/FSK-mode transitions. This keeps the packet
 * engine from being reconfigured while the modem still exits RX.
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

static bool radio_apply_threshold(const radio_ook_cfg_t *cfg)
{
    switch (cfg->threshold)
    {
        case RADIO_OOK_THRESHOLD_FIXED:
            return sx1276_write_reg(&s_radio.bus, SX1276_REG_OOK_FIX, cfg->threshold_value);

        case RADIO_OOK_THRESHOLD_AVERAGE:
            return sx1276_write_reg(&s_radio.bus, SX1276_REG_OOK_AVG,
                                    (uint8_t)(0x80U | (cfg->threshold_value & 0x7FU)));

        case RADIO_OOK_THRESHOLD_PEAK:
        default:
            return sx1276_write_reg(&s_radio.bus, SX1276_REG_OOK_PEAK,
                                    (uint8_t)(0x40U | (cfg->threshold_value & 0x3FU)));
    }
}

static bool radio_apply_ook_config(const radio_ook_cfg_t *cfg)
{
    uint32_t bitrate_reg;
    uint8_t rx_bw;
    uint8_t op_mode;

    bitrate_reg = (32000000UL + (cfg->bitrate_bps / 2UL)) / cfg->bitrate_bps;
    rx_bw = radio_get_rx_bw_reg_value(cfg->rx_bandwidth);
    op_mode = (uint8_t)(SX1276_OPMODE_MODULATION_OOK | SX1276_OPMODE_SHAPING_NONE | SX1276_MODE_SLEEP);

    return radio_set_op_mode_ready(op_mode) &&
           sx1276_set_frequency(&s_radio.bus, cfg->frequency_hz) &&
           sx1276_set_pa_output_power(&s_radio.bus, cfg->tx_power_dbm) &&
           sx1276_write_reg(&s_radio.bus, SX1276_REG_BITRATE_MSB, (uint8_t)((bitrate_reg >> 8) & 0xFFU)) &&
           sx1276_write_reg(&s_radio.bus, SX1276_REG_BITRATE_LSB, (uint8_t)(bitrate_reg & 0xFFU)) &&
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
           radio_apply_threshold(cfg) &&
           sx1276_write_reg(&s_radio.bus, SX1276_REG_PACKET_CONFIG_1, SX1276_PACKET_FORMAT_VARIABLE) &&
           sx1276_write_reg(&s_radio.bus, SX1276_REG_PACKET_CONFIG_2, 0x00U) &&
           sx1276_write_reg(&s_radio.bus, SX1276_REG_PAYLOAD_LENGTH_FSK, RADIO_LIB_MAX_PAYLOAD) &&
           sx1276_write_reg(&s_radio.bus, SX1276_REG_FIFO_THRESH,
                            (uint8_t)(SX1276_FIFO_THRESH_TX_START_NOT_EMPTY |
                                      (RADIO_OOK_FIFO_THRESH_LEVEL & 0x3FU))) &&
           sx1276_write_reg(&s_radio.bus, SX1276_REG_DIO_MAPPING_1, 0x00U) &&
           sx1276_write_reg(&s_radio.bus, SX1276_REG_DIO_MAPPING_2, 0x00U) &&
           radio_set_op_mode_ready((uint8_t)(SX1276_OPMODE_MODULATION_OOK |
                                             SX1276_OPMODE_SHAPING_NONE |
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
                                 (uint8_t)(SX1276_OPMODE_MODULATION_OOK |
                                           SX1276_OPMODE_SHAPING_NONE |
                                           SX1276_MODE_RXCONTINUOUS));
        radio_set_state(RADIO_STATE_RX_CONT);
    }
    else if (s_radio.tx_resume_state == RADIO_STATE_RX_SINGLE)
    {
        (void)sx1276_set_op_mode(&s_radio.bus,
                                 (uint8_t)(SX1276_OPMODE_MODULATION_OOK |
                                           SX1276_OPMODE_SHAPING_NONE |
                                           SX1276_MODE_RXSINGLE));
        radio_set_state(RADIO_STATE_RX_SINGLE);
    }
    else
    {
        (void)sx1276_set_op_mode(&s_radio.bus,
                                 (uint8_t)(SX1276_OPMODE_MODULATION_OOK |
                                           SX1276_OPMODE_SHAPING_NONE |
                                           SX1276_MODE_STDBY));
        radio_set_state(RADIO_STATE_STANDBY);
    }
}

static uint32_t radio_tx_timeout_ms(uint8_t payload_len)
{
    uint64_t timeout_ms;
    uint32_t total_bytes;

    total_bytes = (uint32_t)s_radio.cfg.preamble_len +
                  (uint32_t)s_radio.cfg.sync_word_len +
                  (uint32_t)payload_len +
                  1UL + /* variable-length byte */
                  2UL;  /* guard for packet engine overhead */
    timeout_ms = ((uint64_t)total_bytes * 8ULL * 1000ULL) / s_radio.cfg.bitrate_bps;
    if ((((uint64_t)total_bytes * 8ULL * 1000ULL) % s_radio.cfg.bitrate_bps) != 0ULL)
    {
        timeout_ms++;
    }

    timeout_ms += RADIO_OOK_TX_GUARD_MS;
    if (timeout_ms < RADIO_OOK_TX_GUARD_MS)
    {
        timeout_ms = RADIO_OOK_TX_GUARD_MS;
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

void radio_ook_default_hw_cfg(radio_hw_cfg_t *cfg, SPI_HandleTypeDef *hspi)
{
    radio_lora_default_hw_cfg(cfg, hspi);
}

void radio_ook_default_lora_cfg(radio_lora_cfg_t *cfg)
{
    radio_lora_default_lora_cfg(cfg);
}

radio_status_t radio_ook_set_runtime_cfg(const radio_ook_cfg_t *cfg)
{
    if (!radio_cfg_valid(cfg))
    {
        return RADIO_EINVAL;
    }

    s_runtime_cfg = *cfg;
    s_runtime_cfg_ready = true;
    return RADIO_OK;
}

radio_status_t radio_ook_init(const radio_hw_cfg_t *hw,
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
        radio_default_ook_cfg(&s_runtime_cfg);
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

    if (!radio_set_op_mode_ready((uint8_t)(SX1276_OPMODE_MODULATION_OOK | SX1276_MODE_SLEEP)))
    {
        return RADIO_EHW;
    }

    app_delay_ms(1U);

    if (!sx1276_get_version(&s_radio.bus, &version) || (version != SX1276_VERSION_ID))
    {
        return RADIO_EHW;
    }

    if (!radio_apply_ook_config(&s_radio.cfg))
    {
        return RADIO_EHW;
    }

    s_radio.initialized = true;
    s_radio.state = RADIO_STATE_STANDBY;
    return RADIO_OK;
}

radio_status_t radio_ook_deinit(void)
{
    if (!s_radio.initialized)
    {
        return RADIO_ESTATE;
    }

    (void)radio_ook_sleep();
    memset(&s_radio, 0, sizeof(s_radio));
    s_radio.state = RADIO_STATE_UNINIT;
    return RADIO_OK;
}

radio_status_t radio_ook_start_rx_continuous(void)
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
                            (uint8_t)(SX1276_OPMODE_MODULATION_OOK |
                                      SX1276_OPMODE_SHAPING_NONE |
                                      SX1276_MODE_RXCONTINUOUS)))
    {
        return RADIO_EHW;
    }

    radio_set_state(RADIO_STATE_RX_CONT);
    return RADIO_OK;
}

radio_status_t radio_ook_start_rx_single(uint16_t symbol_timeout)
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
                            (uint8_t)(SX1276_OPMODE_MODULATION_OOK |
                                      SX1276_OPMODE_SHAPING_NONE |
                                      SX1276_MODE_RXSINGLE)))
    {
        return RADIO_EHW;
    }

    s_radio.rx_single_deadline_ms = HAL_GetTick() + radio_rx_single_timeout_ms(symbol_timeout);
    radio_set_state(RADIO_STATE_RX_SINGLE);
    return RADIO_OK;
}

radio_status_t radio_ook_send_async(const uint8_t *data, uint8_t len)
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
    if (((uint16_t)len + 1U) > RADIO_OOK_FIFO_MAX_BYTES)
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

    if (!radio_set_op_mode_ready((uint8_t)(SX1276_OPMODE_MODULATION_OOK |
                                           SX1276_OPMODE_SHAPING_NONE |
                                           SX1276_MODE_STDBY)) ||
        !sx1276_write_reg(&s_radio.bus, SX1276_REG_DIO_MAPPING_1, 0x00U) ||
        !sx1276_write_burst(&s_radio.bus, SX1276_REG_FIFO, fifo_buf, (uint8_t)(len + 1U)) ||
        !sx1276_set_op_mode(&s_radio.bus,
                            (uint8_t)(SX1276_OPMODE_MODULATION_OOK |
                                      SX1276_OPMODE_SHAPING_NONE |
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

radio_status_t radio_ook_standby(void)
{
    if (!s_radio.initialized)
    {
        return RADIO_ESTATE;
    }

    if (!radio_set_op_mode_ready((uint8_t)(SX1276_OPMODE_MODULATION_OOK |
                                           SX1276_OPMODE_SHAPING_NONE |
                                           SX1276_MODE_STDBY)))
    {
        return RADIO_EHW;
    }

    radio_set_state(RADIO_STATE_STANDBY);
    return RADIO_OK;
}

radio_status_t radio_ook_sleep(void)
{
    if (!s_radio.initialized)
    {
        return RADIO_ESTATE;
    }

    if (!radio_set_op_mode_ready((uint8_t)(SX1276_OPMODE_MODULATION_OOK |
                                           SX1276_OPMODE_SHAPING_NONE |
                                           SX1276_MODE_SLEEP)))
    {
        return RADIO_EHW;
    }

    radio_set_state(RADIO_STATE_STANDBY);
    return RADIO_OK;
}

void radio_ook_process(void)
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
            (void)radio_ook_standby();
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
        (void)radio_ook_standby();
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
            (void)radio_ook_standby();
        }
    }

    if ((irq2 & SX1276_IRQ2_PAYLOAD_READY) != 0U)
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

        if (s_radio.state == RADIO_STATE_RX_SINGLE)
        {
            (void)radio_ook_standby();
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

uint32_t radio_ook_take_events(void)
{
    uint32_t events;
    uint32_t key = radio_irq_save();

    events = s_radio.event_flags;
    s_radio.event_flags = 0U;
    radio_irq_restore(key);
    return events;
}

bool radio_ook_get_last_packet(radio_packet_t *pkt)
{
    if ((pkt == NULL) || !s_radio.last_packet.valid)
    {
        return false;
    }

    *pkt = s_radio.last_packet;
    return true;
}

radio_state_t radio_ook_get_state(void)
{
    return s_radio.state;
}

radio_status_t radio_ook_raw_read_reg(uint8_t reg, uint8_t *value)
{
    if (!s_radio.initialized)
    {
        return RADIO_ESTATE;
    }

    return sx1276_read_reg(&s_radio.bus, reg, value) ? RADIO_OK : RADIO_EHW;
}

radio_status_t radio_ook_raw_write_reg(uint8_t reg, uint8_t value)
{
    if (!s_radio.initialized)
    {
        return RADIO_ESTATE;
    }

    return sx1276_write_reg(&s_radio.bus, reg, value) ? RADIO_OK : RADIO_EHW;
}

radio_status_t radio_ook_raw_read_burst(uint8_t reg, uint8_t *data, uint8_t len)
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

radio_status_t radio_ook_raw_write_burst(uint8_t reg, const uint8_t *data, uint8_t len)
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

void radio_ook_on_exti(uint16_t gpio_pin)
{
    radio_handle_exti_pin(gpio_pin);
}
