/**
 * @file radio_lib.c
 * @brief Fasada biblioteki radia delegująca wywołania do backendu wybranego w runtime.
 */

#include "radio_lib.h"

#include "modulations/fsk/radio_fsk.h"
#include "modulations/lora/radio_lora.h"
#include "modulations/ook/radio_ook.h"

#include <string.h>

static uint8_t s_active_backend = RADIO_LIB_ACTIVE_MODULATION;

static uint8_t radio_normalize_backend(uint8_t modulation)
{
    if ((modulation == RADIO_LIB_MODULATION_LORA) ||
        (modulation == RADIO_LIB_MODULATION_FSK) ||
        (modulation == RADIO_LIB_MODULATION_OOK))
    {
        return modulation;
    }

    return RADIO_LIB_MODULATION_LORA;
}

void radio_default_hw_cfg(radio_hw_cfg_t *cfg, SPI_HandleTypeDef *hspi)
{
    radio_lora_default_hw_cfg(cfg, hspi);
}

void radio_default_lora_cfg(radio_lora_cfg_t *cfg)
{
    radio_lora_default_lora_cfg(cfg);
}

void radio_default_fsk_cfg(radio_fsk_cfg_t *cfg)
{
    if (cfg == NULL)
    {
        return;
    }

    memset(cfg, 0, sizeof(*cfg));
    cfg->frequency_hz = 868300000UL;
    cfg->bitrate_bps = 4800UL;
    cfg->rx_bandwidth = RADIO_LORA_BW_125_KHZ;
    cfg->shaping = RADIO_FSK_SHAPING_GFSK;
    cfg->filter = RADIO_FSK_FILTER_BT_05;
    cfg->tx_power_dbm = 14;
    cfg->preamble_len = 8U;
    cfg->sync_word_len = 2U;
    cfg->sync_word = 0x00002DD4UL;
    cfg->address_filter = RADIO_ADDRESS_FILTER_OFF;
    cfg->crc_type = RADIO_PACKET_CRC_CCITT;
    cfg->data_whitening = true;
}

void radio_default_ook_cfg(radio_ook_cfg_t *cfg)
{
    if (cfg == NULL)
    {
        return;
    }

    memset(cfg, 0, sizeof(*cfg));
    cfg->frequency_hz = 868500000UL;
    cfg->bitrate_bps = 4800UL;
    cfg->rx_bandwidth = RADIO_LORA_BW_125_KHZ;
    cfg->tx_power_dbm = 10;
    cfg->preamble_len = 8U;
    cfg->sync_word_len = 2U;
    cfg->sync_word = 0x00002DD4UL;
    cfg->threshold = RADIO_OOK_THRESHOLD_PEAK;
    cfg->threshold_value = 12U;
}

void radio_select_backend(uint8_t modulation)
{
    s_active_backend = radio_normalize_backend(modulation);
}

uint8_t radio_get_backend(void)
{
    return s_active_backend;
}

radio_status_t radio_set_fsk_cfg(const radio_fsk_cfg_t *cfg)
{
    return radio_fsk_set_runtime_cfg(cfg);
}

radio_status_t radio_set_ook_cfg(const radio_ook_cfg_t *cfg)
{
    return radio_ook_set_runtime_cfg(cfg);
}

radio_status_t radio_init(const radio_hw_cfg_t *hw,
                          const radio_lora_cfg_t *cfg,
                          radio_event_cb_t cb,
                          void *user_ctx)
{
    switch (s_active_backend)
    {
        case RADIO_LIB_MODULATION_FSK:
            return radio_fsk_init(hw, cfg, cb, user_ctx);

        case RADIO_LIB_MODULATION_OOK:
            return radio_ook_init(hw, cfg, cb, user_ctx);

        case RADIO_LIB_MODULATION_LORA:
        default:
            return radio_lora_init(hw, cfg, cb, user_ctx);
    }
}

radio_status_t radio_deinit(void)
{
    switch (s_active_backend)
    {
        case RADIO_LIB_MODULATION_FSK:
            return radio_fsk_deinit();

        case RADIO_LIB_MODULATION_OOK:
            return radio_ook_deinit();

        case RADIO_LIB_MODULATION_LORA:
        default:
            return radio_lora_deinit();
    }
}

radio_status_t radio_start_rx_continuous(void)
{
    switch (s_active_backend)
    {
        case RADIO_LIB_MODULATION_FSK:
            return radio_fsk_start_rx_continuous();

        case RADIO_LIB_MODULATION_OOK:
            return radio_ook_start_rx_continuous();

        case RADIO_LIB_MODULATION_LORA:
        default:
            return radio_lora_start_rx_continuous();
    }
}

radio_status_t radio_start_rx_single(uint16_t symbol_timeout)
{
    switch (s_active_backend)
    {
        case RADIO_LIB_MODULATION_FSK:
            return radio_fsk_start_rx_single(symbol_timeout);

        case RADIO_LIB_MODULATION_OOK:
            return radio_ook_start_rx_single(symbol_timeout);

        case RADIO_LIB_MODULATION_LORA:
        default:
            return radio_lora_start_rx_single(symbol_timeout);
    }
}

radio_status_t radio_send_async(const uint8_t *data, uint8_t len)
{
    switch (s_active_backend)
    {
        case RADIO_LIB_MODULATION_FSK:
            return radio_fsk_send_async(data, len);

        case RADIO_LIB_MODULATION_OOK:
            return radio_ook_send_async(data, len);

        case RADIO_LIB_MODULATION_LORA:
        default:
            return radio_lora_send_async(data, len);
    }
}

radio_status_t radio_standby(void)
{
    switch (s_active_backend)
    {
        case RADIO_LIB_MODULATION_FSK:
            return radio_fsk_standby();

        case RADIO_LIB_MODULATION_OOK:
            return radio_ook_standby();

        case RADIO_LIB_MODULATION_LORA:
        default:
            return radio_lora_standby();
    }
}

radio_status_t radio_sleep(void)
{
    switch (s_active_backend)
    {
        case RADIO_LIB_MODULATION_FSK:
            return radio_fsk_sleep();

        case RADIO_LIB_MODULATION_OOK:
            return radio_ook_sleep();

        case RADIO_LIB_MODULATION_LORA:
        default:
            return radio_lora_sleep();
    }
}

void radio_process(void)
{
    switch (s_active_backend)
    {
        case RADIO_LIB_MODULATION_FSK:
            radio_fsk_process();
            break;

        case RADIO_LIB_MODULATION_OOK:
            radio_ook_process();
            break;

        case RADIO_LIB_MODULATION_LORA:
        default:
            radio_lora_process();
            break;
    }
}

uint32_t radio_take_events(void)
{
    switch (s_active_backend)
    {
        case RADIO_LIB_MODULATION_FSK:
            return radio_fsk_take_events();

        case RADIO_LIB_MODULATION_OOK:
            return radio_ook_take_events();

        case RADIO_LIB_MODULATION_LORA:
        default:
            return radio_lora_take_events();
    }
}

bool radio_get_last_packet(radio_packet_t *pkt)
{
    switch (s_active_backend)
    {
        case RADIO_LIB_MODULATION_FSK:
            return radio_fsk_get_last_packet(pkt);

        case RADIO_LIB_MODULATION_OOK:
            return radio_ook_get_last_packet(pkt);

        case RADIO_LIB_MODULATION_LORA:
        default:
            return radio_lora_get_last_packet(pkt);
    }
}

radio_state_t radio_get_state(void)
{
    switch (s_active_backend)
    {
        case RADIO_LIB_MODULATION_FSK:
            return radio_fsk_get_state();

        case RADIO_LIB_MODULATION_OOK:
            return radio_ook_get_state();

        case RADIO_LIB_MODULATION_LORA:
        default:
            return radio_lora_get_state();
    }
}

radio_status_t radio_raw_read_reg(uint8_t reg, uint8_t *value)
{
    switch (s_active_backend)
    {
        case RADIO_LIB_MODULATION_FSK:
            return radio_fsk_raw_read_reg(reg, value);

        case RADIO_LIB_MODULATION_OOK:
            return radio_ook_raw_read_reg(reg, value);

        case RADIO_LIB_MODULATION_LORA:
        default:
            return radio_lora_raw_read_reg(reg, value);
    }
}

radio_status_t radio_raw_write_reg(uint8_t reg, uint8_t value)
{
    switch (s_active_backend)
    {
        case RADIO_LIB_MODULATION_FSK:
            return radio_fsk_raw_write_reg(reg, value);

        case RADIO_LIB_MODULATION_OOK:
            return radio_ook_raw_write_reg(reg, value);

        case RADIO_LIB_MODULATION_LORA:
        default:
            return radio_lora_raw_write_reg(reg, value);
    }
}

radio_status_t radio_raw_read_burst(uint8_t reg, uint8_t *data, uint8_t len)
{
    switch (s_active_backend)
    {
        case RADIO_LIB_MODULATION_FSK:
            return radio_fsk_raw_read_burst(reg, data, len);

        case RADIO_LIB_MODULATION_OOK:
            return radio_ook_raw_read_burst(reg, data, len);

        case RADIO_LIB_MODULATION_LORA:
        default:
            return radio_lora_raw_read_burst(reg, data, len);
    }
}

radio_status_t radio_raw_write_burst(uint8_t reg, const uint8_t *data, uint8_t len)
{
    switch (s_active_backend)
    {
        case RADIO_LIB_MODULATION_FSK:
            return radio_fsk_raw_write_burst(reg, data, len);

        case RADIO_LIB_MODULATION_OOK:
            return radio_ook_raw_write_burst(reg, data, len);

        case RADIO_LIB_MODULATION_LORA:
        default:
            return radio_lora_raw_write_burst(reg, data, len);
    }
}

void radio_on_exti(uint16_t gpio_pin)
{
    switch (s_active_backend)
    {
        case RADIO_LIB_MODULATION_FSK:
            radio_fsk_on_exti(gpio_pin);
            break;

        case RADIO_LIB_MODULATION_OOK:
            radio_ook_on_exti(gpio_pin);
            break;

        case RADIO_LIB_MODULATION_LORA:
        default:
            radio_lora_on_exti(gpio_pin);
            break;
    }
}

#if RADIO_LIB_OWNS_HAL_EXTI_CALLBACK
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    radio_on_exti(GPIO_Pin);
}
#endif
