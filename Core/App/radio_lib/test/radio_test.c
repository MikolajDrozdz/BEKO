/**
 * @file radio_test.c
 * @brief Implementacja narzędzi testowych i diagnostycznych `radio_lib`.
 */

#include "radio_test.h"

#include "../common/sx1276/radio_sx1276_regs.h"

#include "cmsis_os2.h"

#include <ctype.h>
#include <stdio.h>

static volatile uint32_t s_radio_cb_events = 0U;
static uint32_t s_radio_last_ping_ms = 0U;
static uint32_t s_radio_last_tx_start_ms = 0U;
static bool s_demo_initialized = false;

static uint32_t radio_test_irq_save(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void radio_test_irq_restore(uint32_t primask)
{
    if (primask == 0U)
    {
        __enable_irq();
    }
}

static uint32_t radio_test_now_ms(void)
{
    osKernelState_t state = osKernelGetState();
    uint32_t tick_hz;
    uint32_t tick_count;

    if ((state == osKernelRunning) || (state == osKernelLocked))
    {
        tick_hz = osKernelGetTickFreq();
        tick_count = osKernelGetTickCount();
        if (tick_hz == 0U)
        {
            return tick_count;
        }
        return (uint32_t)(((uint64_t)tick_count * 1000ULL) / (uint64_t)tick_hz);
    }

    return HAL_GetTick();
}

/**
 * @brief Callback zdarzeń radiowych używany w scenariuszu demo.
 * @param events Maska zdarzeń z `radio_lib`.
 * @param user_ctx Nieużywany kontekst użytkownika.
 */
static void radio_test_event_cb(uint32_t events, void *user_ctx)
{
    uint32_t key;

    (void)user_ctx;

    key = radio_test_irq_save();
    s_radio_cb_events |= events;
    radio_test_irq_restore(key);
}

/**
 * @brief Drukuje szczegóły odebranego pakietu (HEX + ASCII).
 * @param pkt Wskaźnik na pakiet do wydruku.
 */
static void radio_test_print_packet(const radio_packet_t *pkt)
{
    uint8_t i;

    printf("RADIO RX len=%u RSSI=%d dBm SNR=%d dB hex:",
           pkt->length,
           pkt->rssi_dbm,
           pkt->snr_db);
    for (i = 0U; i < pkt->length; i++)
    {
        printf(" %02X", pkt->data[i]);
    }

    printf(" ascii:");
    for (i = 0U; i < pkt->length; i++)
    {
        char c = (char)pkt->data[i];
        printf("%c", isprint((unsigned char)c) ? c : '.');
    }
    printf("\r\n");
}

/**
 * @brief Drukuje payload odebranego pakietu jako tekst terminalowy.
 * @param pkt Wskaźnik na pakiet do wydruku.
 */
static void radio_test_print_packet_text(const radio_packet_t *pkt)
{
    uint8_t i;

    printf("RADIO RX TEXT: ");
    for (i = 0U; i < pkt->length; i++)
    {
        char c = (char)pkt->data[i];
        printf("%c", isprint((unsigned char)c) ? c : '.');
    }
    printf("\r\n");
}

/**
 * @brief Drukuje podstawowe rejestry diagnostyczne ścieżki TX.
 */
static void radio_test_dump_tx_debug(void)
{
    uint8_t irq_flags = 0U;
    uint8_t dio_map1 = 0U;
    uint8_t op_mode = 0U;

    if ((radio_raw_read_reg(SX1276_REG_IRQ_FLAGS, &irq_flags) != RADIO_OK) ||
        (radio_raw_read_reg(SX1276_REG_DIO_MAPPING_1, &dio_map1) != RADIO_OK) ||
        (radio_raw_read_reg(SX1276_REG_OP_MODE, &op_mode) != RADIO_OK))
    {
        printf("RADIO DBG: reg read failed\r\n");
        return;
    }

    printf("RADIO DBG: irq=0x%02X dio1=0x%02X op=0x%02X state=%d\r\n",
           irq_flags,
           dio_map1,
           op_mode,
           (int)radio_get_state());
}

/**
 * @brief Sprawdza identyfikator wersji układu SX1276.
 * @param version [out] Opcjonalny bufor na wartość `RegVersion`.
 * @return `true` jeśli wersja odpowiada `SX1276_VERSION_ID`.
 */
bool radio_test_probe(uint8_t *version)
{
    uint8_t reg_version = 0U;

    if (radio_raw_read_reg(SX1276_REG_VERSION, &reg_version) != RADIO_OK)
    {
        return false;
    }

    if (version != NULL)
    {
        *version = reg_version;
    }

    return (reg_version == SX1276_VERSION_ID);
}

/**
 * @brief Drukuje skrócony zrzut podstawowych rejestrów radiowych.
 */
void radio_test_dump_basic(void)
{
    uint8_t reg_version = 0U;
    uint8_t reg_op_mode = 0U;
    uint8_t reg_irq_flags = 0U;
    uint8_t reg_modem_cfg_1 = 0U;
    uint8_t reg_modem_cfg_2 = 0U;
    uint8_t reg_modem_cfg_3 = 0U;

    if ((radio_raw_read_reg(SX1276_REG_VERSION, &reg_version) != RADIO_OK) ||
        (radio_raw_read_reg(SX1276_REG_OP_MODE, &reg_op_mode) != RADIO_OK) ||
        (radio_raw_read_reg(SX1276_REG_IRQ_FLAGS, &reg_irq_flags) != RADIO_OK) ||
        (radio_raw_read_reg(SX1276_REG_MODEM_CONFIG_1, &reg_modem_cfg_1) != RADIO_OK) ||
        (radio_raw_read_reg(SX1276_REG_MODEM_CONFIG_2, &reg_modem_cfg_2) != RADIO_OK) ||
        (radio_raw_read_reg(SX1276_REG_MODEM_CONFIG_3, &reg_modem_cfg_3) != RADIO_OK))
    {
        printf("RADIO: dump read failed\r\n");
        return;
    }

    printf("RADIO REG version=0x%02X opmode=0x%02X irq=0x%02X mc1=0x%02X mc2=0x%02X mc3=0x%02X\r\n",
           reg_version,
           reg_op_mode,
           reg_irq_flags,
           reg_modem_cfg_1,
           reg_modem_cfg_2,
           reg_modem_cfg_3);
}

/**
 * @brief Wysyła testowy pakiet "PING".
 * @return Kod statusu z `radio_send_async`.
 */
radio_status_t radio_test_send_ping(void)
{
    static const uint8_t ping_payload[] = { 'P', 'I', 'N', 'G' };
    return radio_send_async(ping_payload, (uint8_t)sizeof(ping_payload));
}

/**
 * @brief Inicjalizuje demonstracyjną konfigurację i uruchamia RX ciągły.
 * @param hspi Uchwyt SPI używany przez moduł radiowy.
 */
void radio_test_demo_init(SPI_HandleTypeDef *hspi)
{
    radio_hw_cfg_t radio_hw;
    radio_lora_cfg_t radio_cfg;
    radio_status_t radio_status;
    uint8_t radio_version = 0U;

    if (hspi == NULL)
    {
        printf("RADIO init failed: null SPI\r\n");
        s_demo_initialized = false;
        return;
    }

    printf("RADIO init\r\n");

    radio_default_hw_cfg(&radio_hw, hspi);
    radio_default_lora_cfg(&radio_cfg);
    radio_status = radio_init(&radio_hw, &radio_cfg, radio_test_event_cb, NULL);
    if (radio_status != RADIO_OK)
    {
        printf("RADIO init failed: %d\r\n", (int)radio_status);
        s_demo_initialized = false;
        return;
    }

    if (radio_test_probe(&radio_version))
    {
        printf("RADIO probe OK, version=0x%02X\r\n", radio_version);
    }
    else
    {
        printf("RADIO probe failed\r\n");
    }

    radio_test_dump_basic();

    radio_status = radio_start_rx_continuous();
    printf("RADIO RX start: %s\r\n", (radio_status == RADIO_OK) ? "OK" : "FAIL");

    s_radio_cb_events = 0U;
    s_radio_last_ping_ms = radio_test_now_ms();
    s_radio_last_tx_start_ms = 0U;
    s_demo_initialized = (radio_status == RADIO_OK);
}

/**
 * @brief Obsługuje logikę demo: zdarzenia, watchdog TX i okresowe PING.
 */
void radio_test_demo_process(void)
{
    uint32_t events;
    uint32_t cb_events;
    uint32_t key;
    radio_packet_t pkt;
    uint32_t now;
    radio_status_t tx_status;

    if (!s_demo_initialized)
    {
        return;
    }

    radio_process();

    events = radio_take_events();
    key = radio_test_irq_save();
    cb_events = s_radio_cb_events;
    s_radio_cb_events = 0U;
    radio_test_irq_restore(key);
    events |= cb_events;

    if ((events & RADIO_EVENT_TX_DONE) != 0U)
    {
        printf("RADIO EVT: TX_DONE\r\n");
        s_radio_last_tx_start_ms = 0U;
    }
    if ((events & RADIO_EVENT_RX_DONE) != 0U)
    {
        printf("RADIO EVT: RX_DONE\r\n");
        if (radio_get_last_packet(&pkt))
        {
            radio_test_print_packet(&pkt);
            radio_test_print_packet_text(&pkt);
        }
        else
        {
            printf("RADIO RX: no packet data\r\n");
        }

        if (radio_get_state() != RADIO_STATE_RX_CONT)
        {
            (void)radio_start_rx_continuous();
        }
    }
    if ((events & RADIO_EVENT_RX_TIMEOUT) != 0U)
    {
        printf("RADIO EVT: RX_TIMEOUT\r\n");
    }
    if ((events & RADIO_EVENT_CRC_ERR) != 0U)
    {
        printf("RADIO EVT: CRC_ERR\r\n");
    }
    if ((events & RADIO_EVENT_FIFO_OVERRUN) != 0U)
    {
        printf("RADIO EVT: FIFO_OVERRUN\r\n");
    }
    if ((events & RADIO_EVENT_CAD_DONE) != 0U)
    {
        printf("RADIO EVT: CAD_DONE\r\n");
    }
    if ((events & RADIO_EVENT_CAD_DETECTED) != 0U)
    {
        printf("RADIO EVT: CAD_DETECTED\r\n");
    }
    if ((events & RADIO_EVENT_HW_ERROR) != 0U)
    {
        printf("RADIO EVT: HW_ERROR\r\n");
    }

    now = radio_test_now_ms();
    if ((radio_get_state() == RADIO_STATE_TX) &&
        (s_radio_last_tx_start_ms != 0U) &&
        ((now - s_radio_last_tx_start_ms) > 2000U))
    {
        printf("RADIO WARN: TX stuck, recovering\r\n");
        radio_test_dump_tx_debug();
        (void)radio_standby();
        (void)radio_start_rx_continuous();
        s_radio_last_tx_start_ms = 0U;
    }

    if ((now - s_radio_last_ping_ms) >= 5000U)
    {
        if (radio_get_state() == RADIO_STATE_TX)
        {
            printf("RADIO TX busy, skip ping\r\n");
            radio_test_dump_tx_debug();
        }
        else
        {
            tx_status = radio_test_send_ping();
            if (tx_status == RADIO_OK)
            {
                printf("RADIO TX: PING\r\n");
                s_radio_last_tx_start_ms = now;
            }
            else
            {
                printf("RADIO TX failed: %d\r\n", (int)tx_status);
                if (tx_status == RADIO_EHW)
                {
                    printf("RADIO TX recovery: standby+rx_cont\r\n");
                    (void)radio_standby();
                    (void)radio_start_rx_continuous();
                }
            }
        }
        s_radio_last_ping_ms = now;
    }
}
