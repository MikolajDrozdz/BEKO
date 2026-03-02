/**
 * @file radio_lora.c
 * @brief Implementacja backendu LoRa (SX1276/RFM95).
 */

#include "radio_lora.h"

#include "app_delay.h"
#include "main.h"
#include "../../common/sx1276/radio_sx1276.h"
#include "../../common/sx1276/radio_sx1276_regs.h"

#include <string.h>

#define RADIO_SX1276_FIFO_TX_BASE_ADDR        0x00U
#define RADIO_SX1276_FIFO_RX_BASE_ADDR        0x00U
#define RADIO_DEFAULT_RX_TIMEOUT_SYMBOLS      0x03FFU
#define RADIO_RESET_LOW_DELAY_MS              2U
#define RADIO_RESET_HIGH_DELAY_MS             10U

typedef struct
{
    bool initialized;
    sx1276_bus_t bus;
    radio_hw_cfg_t hw;
    radio_lora_cfg_t lora;
    radio_event_cb_t callback;
    void *callback_ctx;
    volatile uint8_t dio_pending_mask;
    volatile uint32_t event_flags;
    radio_state_t state;
    radio_state_t tx_resume_state;
    uint16_t rx_single_timeout_symbols;
    radio_packet_t last_packet;
} radio_context_t;

static radio_context_t s_radio;

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
    uint32_t key;

    key = radio_irq_save();
    s_radio.event_flags |= events;
    radio_irq_restore(key);
}

static bool radio_read_irq_flags_retry(uint8_t *irq_flags)
{
    if (sx1276_get_irq_flags(&s_radio.bus, irq_flags))
    {
        return true;
    }

    /* Retry once to handle occasional SPI transient errors. */
    return sx1276_get_irq_flags(&s_radio.bus, irq_flags);
}

static bool radio_cfg_valid(const radio_lora_cfg_t *cfg)
{
    if ((cfg == NULL) ||
        (cfg->frequency_hz < 137000000UL) || (cfg->frequency_hz > 1020000000UL) ||
        ((uint8_t)cfg->bandwidth > (uint8_t)RADIO_LORA_BW_500_KHZ) ||
        (cfg->spreading_factor < 6U) || (cfg->spreading_factor > 12U) ||
        (cfg->coding_rate < 5U) || (cfg->coding_rate > 8U) ||
        (cfg->preamble_len == 0U))
    {
        return false;
    }

    if (cfg->implicit_header && (cfg->payload_len == 0U))
    {
        return false;
    }

    if ((cfg->spreading_factor == 6U) && !cfg->implicit_header)
    {
        return false;
    }

    return true;
}

static void radio_hw_reset(void)
{
    HAL_GPIO_WritePin(s_radio.hw.nss.port, s_radio.hw.nss.pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(s_radio.hw.reset.port, s_radio.hw.reset.pin, GPIO_PIN_RESET);
    app_delay_ms(RADIO_RESET_LOW_DELAY_MS);
    HAL_GPIO_WritePin(s_radio.hw.reset.port, s_radio.hw.reset.pin, GPIO_PIN_SET);
    app_delay_ms(RADIO_RESET_HIGH_DELAY_MS);
}

static bool radio_low_data_rate_optimize_required(const radio_lora_cfg_t *cfg)
{
    if ((cfg->bandwidth <= RADIO_LORA_BW_125_KHZ) &&
        (cfg->spreading_factor >= 11U))
    {
        return true;
    }

    return false;
}

static bool radio_apply_lora_config(const radio_lora_cfg_t *cfg)
{
    bool ldo_required;

    ldo_required = radio_low_data_rate_optimize_required(cfg);

    return sx1276_set_frequency(&s_radio.bus, cfg->frequency_hz) &&
           sx1276_set_pa_output_power(&s_radio.bus, cfg->tx_power_dbm) &&
           sx1276_set_lora_modem(&s_radio.bus,
                                 (uint8_t)cfg->bandwidth,
                                 cfg->spreading_factor,
                                 cfg->coding_rate,
                                 cfg->implicit_header,
                                 cfg->crc_on,
                                 ldo_required) &&
           sx1276_set_symbol_timeout(&s_radio.bus, RADIO_DEFAULT_RX_TIMEOUT_SYMBOLS) &&
           sx1276_set_preamble(&s_radio.bus, cfg->preamble_len) &&
           sx1276_set_sync_word(&s_radio.bus, cfg->sync_word) &&
           sx1276_set_invert_iq(&s_radio.bus, cfg->invert_iq) &&
           sx1276_set_fifo_base_addrs(&s_radio.bus,
                                      RADIO_SX1276_FIFO_TX_BASE_ADDR,
                                      RADIO_SX1276_FIFO_RX_BASE_ADDR) &&
           sx1276_set_payload_length(&s_radio.bus, cfg->payload_len) &&
           sx1276_write_reg(&s_radio.bus, SX1276_REG_LNA, 0x23U) &&
           sx1276_write_reg(&s_radio.bus, SX1276_REG_IRQ_FLAGS_MASK, 0x00U);
}

static bool radio_config_dio_for_rx(void)
{
    const uint8_t dio_mapping1 = (uint8_t)(SX1276_DIO0_MAP_RX_DONE |
                                           SX1276_DIO1_MAP_RX_TIMEOUT);

    return sx1276_map_dio(&s_radio.bus, dio_mapping1, 0x00U);
}

static bool radio_config_dio_for_tx(void)
{
    const uint8_t dio_mapping1 = (uint8_t)(SX1276_DIO0_MAP_TX_DONE |
                                           SX1276_DIO1_MAP_RX_TIMEOUT);

    return sx1276_map_dio(&s_radio.bus, dio_mapping1, 0x00U);
}

static void radio_set_state(radio_state_t state)
{
    s_radio.state = state;
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
        if ((i == 2U) && (RADIO_LIB_ENABLE_DIO2_EXTI == 0))
        {
            continue;
        }
        if ((i == 3U) && (RADIO_LIB_ENABLE_DIO3_EXTI == 0))
        {
            continue;
        }
        if ((i == 4U) && (RADIO_LIB_ENABLE_DIO4_EXTI == 0))
        {
            continue;
        }
        if ((i == 5U) && (RADIO_LIB_ENABLE_DIO5_EXTI == 0))
        {
            continue;
        }

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
    uint8_t current_addr;
    uint8_t rx_len;
    int8_t snr_raw;
    uint8_t rssi_raw;
    bool rf_ok;

    rf_ok = sx1276_get_rx_current_addr(&s_radio.bus, &current_addr) &&
            sx1276_get_rx_nb_bytes(&s_radio.bus, &rx_len) &&
            sx1276_set_fifo_addr_ptr(&s_radio.bus, current_addr);
    if (!rf_ok)
    {
        return RADIO_EHW;
    }

    if (rx_len > RADIO_LIB_MAX_PAYLOAD)
    {
        s_radio.last_packet.valid = false;
        return RADIO_EBUS;
    }

    if (!sx1276_read_fifo(&s_radio.bus, s_radio.last_packet.data, rx_len))
    {
        return RADIO_EHW;
    }

    if (!sx1276_get_packet_snr_raw(&s_radio.bus, &snr_raw) ||
        !sx1276_get_packet_rssi_raw(&s_radio.bus, &rssi_raw))
    {
        return RADIO_EHW;
    }

    s_radio.last_packet.length = rx_len;
    s_radio.last_packet.snr_db = (int8_t)(snr_raw / 4);
    s_radio.last_packet.rssi_dbm = (int16_t)((int16_t)rssi_raw - 157);
    s_radio.last_packet.timestamp_ms = HAL_GetTick();
    s_radio.last_packet.valid = true;

    return RADIO_OK;
}

static void radio_resume_after_tx(void)
{
    /* Po wejściu tutaj TX jest już zakończony. */
    radio_set_state(RADIO_STATE_STANDBY);

    if (s_radio.tx_resume_state == RADIO_STATE_RX_CONT)
    {
        (void)radio_lora_start_rx_continuous();
    }
    else if (s_radio.tx_resume_state == RADIO_STATE_RX_SINGLE)
    {
        (void)radio_lora_start_rx_single(s_radio.rx_single_timeout_symbols);
    }
    else
    {
        (void)radio_lora_standby();
    }
}

void radio_lora_default_hw_cfg(radio_hw_cfg_t *cfg, SPI_HandleTypeDef *hspi)
{
    if (cfg == NULL)
    {
        return;
    }

    memset(cfg, 0, sizeof(*cfg));

    cfg->hspi = hspi;
    cfg->nss.port = SPI_CS_GPIO_Port;
    cfg->nss.pin = SPI_CS_Pin;
    cfg->reset.port = RMF_RST_GPIO_Port;
    cfg->reset.pin = RMF_RST_Pin;

    cfg->dio[0].port = RFM_DIO0_EXIT_2_GPIO_Port;
    cfg->dio[0].pin = RFM_DIO0_EXIT_2_Pin;
    cfg->dio[1].port = RFM_DIO1_EXIT_1_GPIO_Port;
    cfg->dio[1].pin = RFM_DIO1_EXIT_1_Pin;
    cfg->dio[2].port = RFM_DIO2_EXIT_15_GPIO_Port;
    cfg->dio[2].pin = RFM_DIO2_EXIT_15_Pin;
    cfg->dio[3].port = RFM_DIO3_EXIT_14_GPIO_Port;
    cfg->dio[3].pin = RFM_DIO3_EXIT_14_Pin;
    cfg->dio[4].port = RFM_DIO4_EXIT_13_GPIO_Port;
    cfg->dio[4].pin = RFM_DIO4_EXIT_13_Pin;
    cfg->dio[5].port = RFM_DIO5_EXIT_8_GPIO_Port;
    cfg->dio[5].pin = RFM_DIO5_EXIT_8_Pin;
    cfg->spi_timeout_ms = RADIO_LIB_SPI_TIMEOUT_MS;
}

void radio_lora_default_lora_cfg(radio_lora_cfg_t *cfg)
{
    if (cfg == NULL)
    {
        return;
    }

    memset(cfg, 0, sizeof(*cfg));
    cfg->frequency_hz = RADIO_LIB_DEFAULT_FREQ_HZ;
    cfg->bandwidth = RADIO_LORA_BW_125_KHZ;
    cfg->spreading_factor = 7U;
    cfg->coding_rate = 5U;
    cfg->preamble_len = 8U;
    cfg->sync_word = RADIO_LIB_DEFAULT_SYNC_WORD;
    cfg->crc_on = true;
    cfg->invert_iq = false;
    cfg->tx_power_dbm = 14;
    cfg->implicit_header = false;
    cfg->payload_len = 0U;
}

radio_status_t radio_lora_init(const radio_hw_cfg_t *hw,
                          const radio_lora_cfg_t *cfg,
                          radio_event_cb_t cb,
                          void *user_ctx)
{
    uint8_t version;

    if ((hw == NULL) || (hw->hspi == NULL) ||
        (hw->nss.port == NULL) || (hw->reset.port == NULL) ||
        !radio_cfg_valid(cfg))
    {
        return RADIO_EINVAL;
    }

    memset(&s_radio, 0, sizeof(s_radio));

    s_radio.hw = *hw;
    s_radio.lora = *cfg;
    s_radio.callback = cb;
    s_radio.callback_ctx = user_ctx;
    s_radio.bus.hspi = hw->hspi;
    s_radio.bus.nss_port = hw->nss.port;
    s_radio.bus.nss_pin = hw->nss.pin;
    s_radio.bus.spi_timeout_ms = hw->spi_timeout_ms;
    s_radio.rx_single_timeout_symbols = RADIO_DEFAULT_RX_TIMEOUT_SYMBOLS;
    s_radio.state = RADIO_STATE_UNINIT;

    radio_hw_reset();

    if (!sx1276_set_op_mode(&s_radio.bus, (uint8_t)(SX1276_OPMODE_LONG_RANGE_MODE | SX1276_MODE_SLEEP)))
    {
        return RADIO_EHW;
    }

    app_delay_ms(1U);

    if (!sx1276_get_version(&s_radio.bus, &version) || (version != SX1276_VERSION_ID))
    {
        return RADIO_EHW;
    }

    if (!radio_apply_lora_config(&s_radio.lora) ||
        !sx1276_set_fifo_addr_ptr(&s_radio.bus, RADIO_SX1276_FIFO_TX_BASE_ADDR) ||
        !sx1276_clear_irq_flags(&s_radio.bus, 0xFFU) ||
        !sx1276_set_op_mode(&s_radio.bus, (uint8_t)(SX1276_OPMODE_LONG_RANGE_MODE | SX1276_MODE_STDBY)))
    {
        return RADIO_EHW;
    }

#if RADIO_LIB_ENABLE_DIO5_EXTI == 0
    __HAL_GPIO_EXTI_CLEAR_IT(RFM_DIO5_EXIT_8_Pin);
    HAL_NVIC_DisableIRQ(RFM_DIO5_EXIT_8_EXTI_IRQn);
#endif
#if RADIO_LIB_ENABLE_DIO4_EXTI == 0
    __HAL_GPIO_EXTI_CLEAR_IT(RFM_DIO4_EXIT_13_Pin);
    HAL_NVIC_DisableIRQ(RFM_DIO4_EXIT_13_EXTI_IRQn);
#endif
#if RADIO_LIB_ENABLE_DIO3_EXTI == 0
    __HAL_GPIO_EXTI_CLEAR_IT(RFM_DIO3_EXIT_14_Pin);
    HAL_NVIC_DisableIRQ(RFM_DIO3_EXIT_14_EXTI_IRQn);
#endif
#if RADIO_LIB_ENABLE_DIO2_EXTI == 0
    __HAL_GPIO_EXTI_CLEAR_IT(RFM_DIO2_EXIT_15_Pin);
    HAL_NVIC_DisableIRQ(RFM_DIO2_EXIT_15_EXTI_IRQn);
#endif

    s_radio.initialized = true;
    s_radio.state = RADIO_STATE_STANDBY;
    return RADIO_OK;
}

radio_status_t radio_lora_deinit(void)
{
    if (!s_radio.initialized)
    {
        return RADIO_ESTATE;
    }

    (void)radio_lora_sleep();
    memset(&s_radio, 0, sizeof(s_radio));
    s_radio.state = RADIO_STATE_UNINIT;
    return RADIO_OK;
}

radio_status_t radio_lora_start_rx_continuous(void)
{
    if (!s_radio.initialized)
    {
        return RADIO_ESTATE;
    }

    if (s_radio.state == RADIO_STATE_TX)
    {
        return RADIO_EBUS;
    }

    if (!radio_config_dio_for_rx() ||
        !sx1276_set_symbol_timeout(&s_radio.bus, RADIO_DEFAULT_RX_TIMEOUT_SYMBOLS) ||
        !sx1276_clear_irq_flags(&s_radio.bus, 0xFFU) ||
        !sx1276_set_op_mode(&s_radio.bus, (uint8_t)(SX1276_OPMODE_LONG_RANGE_MODE | SX1276_MODE_RXCONTINUOUS)))
    {
        return RADIO_EHW;
    }

    radio_set_state(RADIO_STATE_RX_CONT);
    return RADIO_OK;
}

radio_status_t radio_lora_start_rx_single(uint16_t symbol_timeout)
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

    s_radio.rx_single_timeout_symbols = symbol_timeout;

    if (!radio_config_dio_for_rx() ||
        !sx1276_set_symbol_timeout(&s_radio.bus, symbol_timeout) ||
        !sx1276_clear_irq_flags(&s_radio.bus, 0xFFU) ||
        !sx1276_set_op_mode(&s_radio.bus, (uint8_t)(SX1276_OPMODE_LONG_RANGE_MODE | SX1276_MODE_RXSINGLE)))
    {
        return RADIO_EHW;
    }

    radio_set_state(RADIO_STATE_RX_SINGLE);
    return RADIO_OK;
}

radio_status_t radio_lora_send_async(const uint8_t *data, uint8_t len)
{
    uint8_t op_mode = 0U;

    if (!s_radio.initialized)
    {
        return RADIO_ESTATE;
    }

    if ((data == NULL) || (len == 0U) || (len > RADIO_LIB_MAX_PAYLOAD))
    {
        return RADIO_EINVAL;
    }

    if (s_radio.state == RADIO_STATE_TX)
    {
        return RADIO_EBUS;
    }

    s_radio.tx_resume_state = s_radio.state;

    if (!sx1276_set_op_mode(&s_radio.bus, (uint8_t)(SX1276_OPMODE_LONG_RANGE_MODE | SX1276_MODE_STDBY)) ||
        !radio_config_dio_for_tx() ||
        !sx1276_clear_irq_flags(&s_radio.bus, 0xFFU) ||
        !sx1276_set_fifo_addr_ptr(&s_radio.bus, RADIO_SX1276_FIFO_TX_BASE_ADDR) ||
        !sx1276_set_payload_length(&s_radio.bus, len) ||
        !sx1276_write_fifo(&s_radio.bus, data, len) ||
        !sx1276_set_op_mode(&s_radio.bus, (uint8_t)(SX1276_OPMODE_LONG_RANGE_MODE | SX1276_MODE_TX)))
    {
        return RADIO_EHW;
    }

    if (!sx1276_read_reg(&s_radio.bus, SX1276_REG_OP_MODE, &op_mode))
    {
        return RADIO_EHW;
    }

    op_mode &= SX1276_OPMODE_MODE_MASK;
    if ((op_mode != SX1276_MODE_TX) && (op_mode != SX1276_MODE_FSTX))
    {
        return RADIO_EHW;
    }

    radio_set_state(RADIO_STATE_TX);
    return RADIO_OK;
}

radio_status_t radio_lora_standby(void)
{
    if (!s_radio.initialized)
    {
        return RADIO_ESTATE;
    }

    if (!sx1276_set_op_mode(&s_radio.bus, (uint8_t)(SX1276_OPMODE_LONG_RANGE_MODE | SX1276_MODE_STDBY)))
    {
        return RADIO_EHW;
    }

    radio_set_state(RADIO_STATE_STANDBY);
    return RADIO_OK;
}

radio_status_t radio_lora_sleep(void)
{
    if (!s_radio.initialized)
    {
        return RADIO_ESTATE;
    }

    if (!sx1276_set_op_mode(&s_radio.bus, (uint8_t)(SX1276_OPMODE_LONG_RANGE_MODE | SX1276_MODE_SLEEP)))
    {
        return RADIO_EHW;
    }

    radio_set_state(RADIO_STATE_STANDBY);
    return RADIO_OK;
}

void radio_lora_process(void)
{
    uint8_t pending;
    uint8_t irq_flags;
    uint8_t op_mode;
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

    /*
     * Główna ścieżka: zdarzenia z EXTI (pending != 0).
     * Fallback: podczas TX sprawdzaj flagi IRQ pollingiem, jeśli zbocze DIO0
     * zostało pominięte.
     */
    if ((pending == 0U) &&
        (s_radio.hw.dio[0].port != NULL) &&
        (HAL_GPIO_ReadPin(s_radio.hw.dio[0].port, s_radio.hw.dio[0].pin) == GPIO_PIN_SET))
    {
        pending |= 0x01U;
    }

    if ((pending == 0U) && (s_radio.state != RADIO_STATE_TX))
    {
        return;
    }

    if (!radio_read_irq_flags_retry(&irq_flags))
    {
        events |= RADIO_EVENT_HW_ERROR;
        goto done;
    }

    if (irq_flags == 0U)
    {
        /*
         * Ścieżka recovery: jeśli zbocze TX_DONE zostało pominięte, układ może
         * być już poza TX, mimo że stan software nadal wskazuje TX.
         */
        if ((s_radio.state == RADIO_STATE_TX) &&
            sx1276_read_reg(&s_radio.bus, SX1276_REG_OP_MODE, &op_mode))
        {
            op_mode &= SX1276_OPMODE_MODE_MASK;
            if ((op_mode != SX1276_MODE_TX) && (op_mode != SX1276_MODE_FSTX))
            {
                events |= RADIO_EVENT_TX_DONE;
                radio_resume_after_tx();
            }
        }
        goto done;
    }

    if (!sx1276_clear_irq_flags(&s_radio.bus, irq_flags))
    {
        events |= RADIO_EVENT_HW_ERROR;
    }

    if ((irq_flags & SX1276_IRQ_PAYLOAD_CRC_ERROR) != 0U)
    {
        events |= RADIO_EVENT_CRC_ERR;
    }

    if ((irq_flags & SX1276_IRQ_RX_TIMEOUT) != 0U)
    {
        events |= RADIO_EVENT_RX_TIMEOUT;
        if (s_radio.state == RADIO_STATE_RX_SINGLE)
        {
            (void)radio_lora_standby();
        }
    }

    if ((irq_flags & SX1276_IRQ_RX_DONE) != 0U)
    {
        if ((irq_flags & SX1276_IRQ_PAYLOAD_CRC_ERROR) == 0U)
        {
            radio_status_t read_status = radio_read_rx_packet();
            if (read_status == RADIO_OK)
            {
                events |= RADIO_EVENT_RX_DONE;
            }
            else if (read_status == RADIO_EBUS)
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
            (void)radio_lora_standby();
        }
    }

    if ((irq_flags & SX1276_IRQ_TX_DONE) != 0U)
    {
        events |= RADIO_EVENT_TX_DONE;
        radio_resume_after_tx();
    }

    if ((irq_flags & SX1276_IRQ_CAD_DONE) != 0U)
    {
        events |= RADIO_EVENT_CAD_DONE;
    }

    if ((irq_flags & SX1276_IRQ_CAD_DETECTED) != 0U)
    {
        events |= RADIO_EVENT_CAD_DETECTED;
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

uint32_t radio_lora_take_events(void)
{
    uint32_t events;
    uint32_t key;

    key = radio_irq_save();
    events = s_radio.event_flags;
    s_radio.event_flags = 0U;
    radio_irq_restore(key);

    return events;
}

bool radio_lora_get_last_packet(radio_packet_t *pkt)
{
    if ((pkt == NULL) || !s_radio.last_packet.valid)
    {
        return false;
    }

    *pkt = s_radio.last_packet;
    return true;
}

radio_state_t radio_lora_get_state(void)
{
    return s_radio.state;
}

radio_status_t radio_lora_raw_read_reg(uint8_t reg, uint8_t *value)
{
    if (!s_radio.initialized)
    {
        return RADIO_ESTATE;
    }

    return sx1276_read_reg(&s_radio.bus, reg, value) ? RADIO_OK : RADIO_EHW;
}

radio_status_t radio_lora_raw_write_reg(uint8_t reg, uint8_t value)
{
    if (!s_radio.initialized)
    {
        return RADIO_ESTATE;
    }

    return sx1276_write_reg(&s_radio.bus, reg, value) ? RADIO_OK : RADIO_EHW;
}

radio_status_t radio_lora_raw_read_burst(uint8_t reg, uint8_t *data, uint8_t len)
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

radio_status_t radio_lora_raw_write_burst(uint8_t reg, const uint8_t *data, uint8_t len)
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

void radio_lora_on_exti(uint16_t gpio_pin)
{
    radio_handle_exti_pin(gpio_pin);
}


