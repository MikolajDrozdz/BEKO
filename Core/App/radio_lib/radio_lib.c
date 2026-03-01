/**
 * @file radio_lib.c
 * @brief Fasada biblioteki radia delegująca wywołania do aktywnego backendu.
 *
 * Implementacja w tym pliku nie zawiera logiki modulacji. Jej rolą jest
 * przekierowanie funkcji API do backendu wybranego przez
 * `RADIO_LIB_ACTIVE_MODULATION`.
 */

#include "radio_lib.h"

#include "modulations/fsk/radio_fsk.h"
#include "modulations/lora/radio_lora.h"
#include "modulations/ook/radio_ook.h"

/**
 * @note Szczegółowe opisy parametrów i kodów zwrotnych funkcji publicznych
 * znajdują się w `radio_lib.h`.
 */
void radio_default_hw_cfg(radio_hw_cfg_t *cfg, SPI_HandleTypeDef *hspi)
{
#if (RADIO_LIB_ACTIVE_MODULATION == RADIO_LIB_MODULATION_LORA)
    radio_lora_default_hw_cfg(cfg, hspi);
#elif (RADIO_LIB_ACTIVE_MODULATION == RADIO_LIB_MODULATION_FSK)
    radio_fsk_default_hw_cfg(cfg, hspi);
#else
    radio_ook_default_hw_cfg(cfg, hspi);
#endif
}

void radio_default_lora_cfg(radio_lora_cfg_t *cfg)
{
#if (RADIO_LIB_ACTIVE_MODULATION == RADIO_LIB_MODULATION_LORA)
    radio_lora_default_lora_cfg(cfg);
#elif (RADIO_LIB_ACTIVE_MODULATION == RADIO_LIB_MODULATION_FSK)
    radio_fsk_default_lora_cfg(cfg);
#else
    radio_ook_default_lora_cfg(cfg);
#endif
}

radio_status_t radio_init(const radio_hw_cfg_t *hw,
                          const radio_lora_cfg_t *cfg,
                          radio_event_cb_t cb,
                          void *user_ctx)
{
#if (RADIO_LIB_ACTIVE_MODULATION == RADIO_LIB_MODULATION_LORA)
    return radio_lora_init(hw, cfg, cb, user_ctx);
#elif (RADIO_LIB_ACTIVE_MODULATION == RADIO_LIB_MODULATION_FSK)
    return radio_fsk_init(hw, cfg, cb, user_ctx);
#else
    return radio_ook_init(hw, cfg, cb, user_ctx);
#endif
}

radio_status_t radio_deinit(void)
{
#if (RADIO_LIB_ACTIVE_MODULATION == RADIO_LIB_MODULATION_LORA)
    return radio_lora_deinit();
#elif (RADIO_LIB_ACTIVE_MODULATION == RADIO_LIB_MODULATION_FSK)
    return radio_fsk_deinit();
#else
    return radio_ook_deinit();
#endif
}

radio_status_t radio_start_rx_continuous(void)
{
#if (RADIO_LIB_ACTIVE_MODULATION == RADIO_LIB_MODULATION_LORA)
    return radio_lora_start_rx_continuous();
#elif (RADIO_LIB_ACTIVE_MODULATION == RADIO_LIB_MODULATION_FSK)
    return radio_fsk_start_rx_continuous();
#else
    return radio_ook_start_rx_continuous();
#endif
}

radio_status_t radio_start_rx_single(uint16_t symbol_timeout)
{
#if (RADIO_LIB_ACTIVE_MODULATION == RADIO_LIB_MODULATION_LORA)
    return radio_lora_start_rx_single(symbol_timeout);
#elif (RADIO_LIB_ACTIVE_MODULATION == RADIO_LIB_MODULATION_FSK)
    return radio_fsk_start_rx_single(symbol_timeout);
#else
    return radio_ook_start_rx_single(symbol_timeout);
#endif
}

radio_status_t radio_send_async(const uint8_t *data, uint8_t len)
{
#if (RADIO_LIB_ACTIVE_MODULATION == RADIO_LIB_MODULATION_LORA)
    return radio_lora_send_async(data, len);
#elif (RADIO_LIB_ACTIVE_MODULATION == RADIO_LIB_MODULATION_FSK)
    return radio_fsk_send_async(data, len);
#else
    return radio_ook_send_async(data, len);
#endif
}

radio_status_t radio_standby(void)
{
#if (RADIO_LIB_ACTIVE_MODULATION == RADIO_LIB_MODULATION_LORA)
    return radio_lora_standby();
#elif (RADIO_LIB_ACTIVE_MODULATION == RADIO_LIB_MODULATION_FSK)
    return radio_fsk_standby();
#else
    return radio_ook_standby();
#endif
}

radio_status_t radio_sleep(void)
{
#if (RADIO_LIB_ACTIVE_MODULATION == RADIO_LIB_MODULATION_LORA)
    return radio_lora_sleep();
#elif (RADIO_LIB_ACTIVE_MODULATION == RADIO_LIB_MODULATION_FSK)
    return radio_fsk_sleep();
#else
    return radio_ook_sleep();
#endif
}

void radio_process(void)
{
#if (RADIO_LIB_ACTIVE_MODULATION == RADIO_LIB_MODULATION_LORA)
    radio_lora_process();
#elif (RADIO_LIB_ACTIVE_MODULATION == RADIO_LIB_MODULATION_FSK)
    radio_fsk_process();
#else
    radio_ook_process();
#endif
}

uint32_t radio_take_events(void)
{
#if (RADIO_LIB_ACTIVE_MODULATION == RADIO_LIB_MODULATION_LORA)
    return radio_lora_take_events();
#elif (RADIO_LIB_ACTIVE_MODULATION == RADIO_LIB_MODULATION_FSK)
    return radio_fsk_take_events();
#else
    return radio_ook_take_events();
#endif
}

bool radio_get_last_packet(radio_packet_t *pkt)
{
#if (RADIO_LIB_ACTIVE_MODULATION == RADIO_LIB_MODULATION_LORA)
    return radio_lora_get_last_packet(pkt);
#elif (RADIO_LIB_ACTIVE_MODULATION == RADIO_LIB_MODULATION_FSK)
    return radio_fsk_get_last_packet(pkt);
#else
    return radio_ook_get_last_packet(pkt);
#endif
}

radio_state_t radio_get_state(void)
{
#if (RADIO_LIB_ACTIVE_MODULATION == RADIO_LIB_MODULATION_LORA)
    return radio_lora_get_state();
#elif (RADIO_LIB_ACTIVE_MODULATION == RADIO_LIB_MODULATION_FSK)
    return radio_fsk_get_state();
#else
    return radio_ook_get_state();
#endif
}

radio_status_t radio_raw_read_reg(uint8_t reg, uint8_t *value)
{
#if (RADIO_LIB_ACTIVE_MODULATION == RADIO_LIB_MODULATION_LORA)
    return radio_lora_raw_read_reg(reg, value);
#elif (RADIO_LIB_ACTIVE_MODULATION == RADIO_LIB_MODULATION_FSK)
    return radio_fsk_raw_read_reg(reg, value);
#else
    return radio_ook_raw_read_reg(reg, value);
#endif
}

radio_status_t radio_raw_write_reg(uint8_t reg, uint8_t value)
{
#if (RADIO_LIB_ACTIVE_MODULATION == RADIO_LIB_MODULATION_LORA)
    return radio_lora_raw_write_reg(reg, value);
#elif (RADIO_LIB_ACTIVE_MODULATION == RADIO_LIB_MODULATION_FSK)
    return radio_fsk_raw_write_reg(reg, value);
#else
    return radio_ook_raw_write_reg(reg, value);
#endif
}

radio_status_t radio_raw_read_burst(uint8_t reg, uint8_t *data, uint8_t len)
{
#if (RADIO_LIB_ACTIVE_MODULATION == RADIO_LIB_MODULATION_LORA)
    return radio_lora_raw_read_burst(reg, data, len);
#elif (RADIO_LIB_ACTIVE_MODULATION == RADIO_LIB_MODULATION_FSK)
    return radio_fsk_raw_read_burst(reg, data, len);
#else
    return radio_ook_raw_read_burst(reg, data, len);
#endif
}

radio_status_t radio_raw_write_burst(uint8_t reg, const uint8_t *data, uint8_t len)
{
#if (RADIO_LIB_ACTIVE_MODULATION == RADIO_LIB_MODULATION_LORA)
    return radio_lora_raw_write_burst(reg, data, len);
#elif (RADIO_LIB_ACTIVE_MODULATION == RADIO_LIB_MODULATION_FSK)
    return radio_fsk_raw_write_burst(reg, data, len);
#else
    return radio_ook_raw_write_burst(reg, data, len);
#endif
}

void radio_on_exti(uint16_t gpio_pin)
{
#if (RADIO_LIB_ACTIVE_MODULATION == RADIO_LIB_MODULATION_LORA)
    radio_lora_on_exti(gpio_pin);
#elif (RADIO_LIB_ACTIVE_MODULATION == RADIO_LIB_MODULATION_FSK)
    radio_fsk_on_exti(gpio_pin);
#else
    radio_ook_on_exti(gpio_pin);
#endif
}

#if RADIO_LIB_OWNS_HAL_EXTI_CALLBACK
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    radio_on_exti(GPIO_Pin);
}
#endif
