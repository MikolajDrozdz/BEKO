#include "service.h"

#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "laviet_frame.h"
#include "main.h"
#include "radio_main.h"
#include "radio_lib/radio_lib.h"
#include "security_main.h"

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if SERVICE_UART

#define SERVICE_UART_TASK_STACK_SIZE    2048U
#define SERVICE_UART_TASK_STACK_WORDS   (SERVICE_UART_TASK_STACK_SIZE / sizeof(StackType_t))
#define SERVICE_UART_LINE_BUF_SIZE      (8U + (RADIO_LIB_MAX_PAYLOAD * 3U))
#define SERVICE_UART_RX_RING_SIZE       1024U
#define SERVICE_UART_POLL_DELAY_MS      1U
#define SERVICE_UART_IRQ_PRIORITY       5U
#define SERVICE_UART_TOKEN_MAX          24U

extern UART_HandleTypeDef huart1;

static osThreadId_t s_service_uart_task = NULL;
static StaticTask_t s_service_uart_task_cb;
static StackType_t s_service_uart_task_stack[SERVICE_UART_TASK_STACK_WORDS];
static char s_service_uart_line[SERVICE_UART_LINE_BUF_SIZE];
static uint8_t s_service_uart_tx_bytes[RADIO_LIB_MAX_PAYLOAD];
static uint8_t s_service_uart_rx_ring[SERVICE_UART_RX_RING_SIZE];
static volatile uint16_t s_service_uart_rx_head = 0U;
static volatile uint16_t s_service_uart_rx_tail = 0U;
static volatile bool s_service_uart_rx_overflow = false;
static uint8_t s_service_uart_rx_byte = 0U;
static bool s_service_uart_unlocked = false;

static const osThreadAttr_t s_service_uart_task_attr =
{
    .name = "uart_service",
    .priority = (osPriority_t)osPriorityLow,
    .stack_mem = s_service_uart_task_stack,
    .stack_size = sizeof(s_service_uart_task_stack),
    .cb_mem = &s_service_uart_task_cb,
    .cb_size = sizeof(s_service_uart_task_cb)
};

static void service_uart_task_fn(void *argument);
static void service_uart_rx_start(void);
static bool service_uart_rx_read_byte(uint8_t *out);
static bool service_uart_rx_take_overflow(void);
static void service_uart_rx_clear(void);
static void service_uart_rx_push_from_isr(uint8_t ch);
static void service_uart_handle_line(const char *line);
static void service_uart_print_locked_help(void);
static void service_uart_print_help(void);
static void service_uart_print_cfg(void);
static void service_uart_print_radio_result(bool ok);
static void service_uart_handle_login(const char *args);
static void service_uart_handle_send_hex(const char *payload);
static void service_uart_handle_text(const char *args);
static void service_uart_handle_mod(const char *args);
static void service_uart_handle_preset(const char *args);
static void service_uart_handle_freq(const char *args);
static void service_uart_handle_bw(const char *args);
static void service_uart_handle_option(const char *args);
static void service_uart_handle_bool_cmd(const char *args, bool (*setter)(bool));
static void service_uart_handle_u32_cmd(const char *args, bool (*setter)(uint32_t));
static void service_uart_handle_auto_ping_mode(const char *args);
static void service_uart_handle_save(void);
static bool service_uart_parse_hex_bytes(const char *text,
                                         uint8_t *data,
                                         uint8_t *len_out,
                                         const char **error_out);
static bool service_uart_parse_u32_token(const char *token, uint32_t *value_out);
static bool service_uart_parse_u32_arg(const char *args, uint32_t *value_out);
static bool service_uart_parse_bool_arg(const char *args, bool *value_out);
static bool service_uart_parse_mod_arg(const char *args, radio_main_modulation_t *mod_out);
static bool service_uart_next_token(const char **cursor, char *out, uint8_t out_size);
static const char *service_uart_skip_space(const char *text);
static bool service_uart_token_equals(const char *a, const char *b);
static bool service_uart_match_prefix(const char **cursor, const char *prefix);
static int service_uart_hex_nibble(char c);
static uint32_t service_uart_get_service_code(void);
static const char *service_uart_mod_text(radio_main_modulation_t modulation);
static const char *service_uart_ap_mode_text(radio_main_auto_ping_mode_t mode);

void service_uart_create_task(void)
{
    if (s_service_uart_task != NULL)
    {
        return;
    }

    HAL_NVIC_SetPriority(USART1_IRQn, SERVICE_UART_IRQ_PRIORITY, 0U);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    service_uart_rx_start();

    s_service_uart_task = osThreadNew(service_uart_task_fn, NULL, &s_service_uart_task_attr);
    if (s_service_uart_task == NULL)
    {
        printf("SERVICE UART: task create failed\r\n");
    }
}

/** @brief Internal helper: `service_uart_task_fn`. */
static void service_uart_task_fn(void *argument)
{
    uint16_t line_len = 0U;
    bool line_overflow = false;

    (void)argument;
    printf("SERVICE UART locked. Use LOGIN <code>\r\n");

    for (;;)
    {
        uint8_t ch = 0U;
        bool did_work = false;

        if (service_uart_rx_take_overflow())
        {
            service_uart_rx_clear();
            line_len = 0U;
            line_overflow = false;
            printf("SERVICE ERR: rx overflow\r\n");
        }

        while (service_uart_rx_read_byte(&ch))
        {
            did_work = true;
            if ((ch == (uint8_t)'\r') || (ch == (uint8_t)'\n'))
            {
                if (line_overflow)
                {
                    printf("SERVICE ERR: line too long\r\n");
                }
                else if (line_len > 0U)
                {
                    s_service_uart_line[line_len] = '\0';
                    service_uart_handle_line(s_service_uart_line);
                }

                line_len = 0U;
                line_overflow = false;
            }
            else if ((ch == 0x08U) || (ch == 0x7FU))
            {
                if (!line_overflow && (line_len > 0U))
                {
                    line_len--;
                }
            }
            else if (line_overflow)
            {
                /* Drop input until end-of-line. */
            }
            else if (line_len >= (uint16_t)(sizeof(s_service_uart_line) - 1U))
            {
                line_overflow = true;
            }
            else if (isprint((unsigned char)ch) || (ch == (uint8_t)'\t'))
            {
                s_service_uart_line[line_len] = (char)ch;
                line_len++;
            }
        }

        if (!did_work)
        {
            osDelay(SERVICE_UART_POLL_DELAY_MS);
        }
    }
}

/** @brief Internal helper: `service_uart_rx_start`. */
static void service_uart_rx_start(void)
{
    (void)HAL_UART_Receive_IT(&huart1, &s_service_uart_rx_byte, 1U);
}

/** @brief Internal helper: `service_uart_rx_read_byte`. */
static bool service_uart_rx_read_byte(uint8_t *out)
{
    uint32_t primask;
    bool ok = false;

    if (out == NULL)
    {
        return false;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    if (s_service_uart_rx_tail != s_service_uart_rx_head)
    {
        *out = s_service_uart_rx_ring[s_service_uart_rx_tail];
        s_service_uart_rx_tail = (uint16_t)((s_service_uart_rx_tail + 1U) % SERVICE_UART_RX_RING_SIZE);
        ok = true;
    }
    __set_PRIMASK(primask);

    return ok;
}

/** @brief Internal helper: `service_uart_rx_take_overflow`. */
static bool service_uart_rx_take_overflow(void)
{
    uint32_t primask;
    bool overflow;

    primask = __get_PRIMASK();
    __disable_irq();
    overflow = s_service_uart_rx_overflow;
    s_service_uart_rx_overflow = false;
    __set_PRIMASK(primask);

    return overflow;
}

/** @brief Internal helper: `service_uart_rx_clear`. */
static void service_uart_rx_clear(void)
{
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();
    s_service_uart_rx_head = 0U;
    s_service_uart_rx_tail = 0U;
    __set_PRIMASK(primask);
}

/** @brief Internal helper: `service_uart_rx_push_from_isr`. */
static void service_uart_rx_push_from_isr(uint8_t ch)
{
    uint16_t next = (uint16_t)((s_service_uart_rx_head + 1U) % SERVICE_UART_RX_RING_SIZE);

    if (next == s_service_uart_rx_tail)
    {
        s_service_uart_rx_overflow = true;
        return;
    }

    s_service_uart_rx_ring[s_service_uart_rx_head] = ch;
    s_service_uart_rx_head = next;
}

void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart1);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if ((huart != NULL) && (huart->Instance == USART1))
    {
        service_uart_rx_push_from_isr(s_service_uart_rx_byte);
        service_uart_rx_start();
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if ((huart != NULL) && (huart->Instance == USART1))
    {
        s_service_uart_rx_overflow = true;
        service_uart_rx_start();
    }
}

/** @brief Internal helper: `service_uart_handle_line`. */
static void service_uart_handle_line(const char *line)
{
    const char *p;
    char cmd[SERVICE_UART_TOKEN_MAX];

    if (line == NULL)
    {
        return;
    }

    p = service_uart_skip_space(line);
    if (service_uart_match_prefix(&p, "SEND:"))
    {
        if (!s_service_uart_unlocked)
        {
            printf("SERVICE LOCKED: use LOGIN <code>\r\n");
            return;
        }
        service_uart_handle_send_hex(p);
        return;
    }

    p = line;
    if (!service_uart_next_token(&p, cmd, sizeof(cmd)))
    {
        return;
    }

    if (service_uart_token_equals(cmd, "LOGIN") ||
        service_uart_token_equals(cmd, "AUTH") ||
        service_uart_token_equals(cmd, "UNLOCK"))
    {
        service_uart_handle_login(p);
    }
    else if (service_uart_token_equals(cmd, "LOGOUT") ||
             service_uart_token_equals(cmd, "LOCK"))
    {
        s_service_uart_unlocked = false;
        printf("SERVICE LOCKED\r\n");
    }
    else if (!s_service_uart_unlocked)
    {
        if (service_uart_token_equals(cmd, "HELP") || service_uart_token_equals(cmd, "?"))
        {
            service_uart_print_locked_help();
        }
        else
        {
            printf("SERVICE LOCKED: use LOGIN <code>\r\n");
        }
    }
    else if (service_uart_token_equals(cmd, "HELP") || service_uart_token_equals(cmd, "?"))
    {
        service_uart_print_help();
    }
    else if (service_uart_token_equals(cmd, "CFG") ||
             service_uart_token_equals(cmd, "CFG?") ||
             service_uart_token_equals(cmd, "STATUS"))
    {
        service_uart_print_cfg();
    }
    else if (service_uart_token_equals(cmd, "SEND") || service_uart_token_equals(cmd, "TX"))
    {
        service_uart_handle_send_hex(p);
    }
    else if (service_uart_token_equals(cmd, "TEXT"))
    {
        service_uart_handle_text(p);
    }
    else if (service_uart_token_equals(cmd, "MOD"))
    {
        service_uart_handle_mod(p);
    }
    else if (service_uart_token_equals(cmd, "PRESET"))
    {
        service_uart_handle_preset(p);
    }
    else if (service_uart_token_equals(cmd, "FREQ"))
    {
        service_uart_handle_freq(p);
    }
    else if (service_uart_token_equals(cmd, "BW"))
    {
        service_uart_handle_bw(p);
    }
    else if (service_uart_token_equals(cmd, "OPTION") || service_uart_token_equals(cmd, "SET"))
    {
        service_uart_handle_option(p);
    }
    else if (service_uart_token_equals(cmd, "FH"))
    {
        service_uart_handle_bool_cmd(p, radio_main_cmd_set_fh);
    }
    else if (service_uart_token_equals(cmd, "FH_PERIOD"))
    {
        service_uart_handle_u32_cmd(p, radio_main_cmd_set_fh_period);
    }
    else if (service_uart_token_equals(cmd, "CODING"))
    {
        service_uart_handle_bool_cmd(p, radio_main_cmd_set_coding);
    }
    else if (service_uart_token_equals(cmd, "AUTOPING"))
    {
        service_uart_handle_bool_cmd(p, radio_main_cmd_set_auto_ping);
    }
    else if (service_uart_token_equals(cmd, "AUTOPING_PERIOD"))
    {
        service_uart_handle_u32_cmd(p, radio_main_cmd_set_auto_ping_period);
    }
    else if (service_uart_token_equals(cmd, "AUTOPING_MODE"))
    {
        service_uart_handle_auto_ping_mode(p);
    }
    else if (service_uart_token_equals(cmd, "RESET"))
    {
        service_uart_print_radio_result(radio_main_cmd_reset_module());
    }
    else if (service_uart_token_equals(cmd, "SAVE"))
    {
        service_uart_handle_save();
    }
    else
    {
        printf("SERVICE ERR: unknown command, type HELP\r\n");
    }
}

/** @brief Internal helper: `service_uart_print_locked_help`. */
static void service_uart_print_locked_help(void)
{
    printf("SERVICE UART locked\r\n");
    printf("  LOGIN <device address>\r\n");
    printf("  Address may be decimal, hex, or 0x-prefixed hex\r\n");
}

/** @brief Internal helper: `service_uart_print_help`. */
static void service_uart_print_help(void)
{
    printf("SERVICE UART commands:\r\n");
    printf("  HELP | CFG? | LOGOUT\r\n");
    printf("  SEND: <hex bytes> | TX <hex bytes>\r\n");
    printf("  TEXT <dst> <ascii up to 16B>\r\n");
    printf("  MOD <LORA|FSK|OOK|0|1|2>\r\n");
    printf("  PRESET <0..2> | FREQ <Hz> | BW <0..9>\r\n");
    printf("  OPTION <id> <value>  (radio_main_option_t)\r\n");
    printf("  FH <ON|OFF> | FH_PERIOD <ms>\r\n");
    printf("  CODING <ON|OFF>\r\n");
    printf("  AUTOPING <ON|OFF> | AUTOPING_PERIOD <ms> | AUTOPING_MODE <FRAME|RAW>\r\n");
    printf("  RESET | SAVE\r\n");
}

/** @brief Internal helper: `service_uart_print_cfg`. */
static void service_uart_print_cfg(void)
{
    radio_main_runtime_cfg_t cfg;

    if (!radio_main_get_runtime_cfg(&cfg))
    {
        printf("SERVICE ERR: cfg unavailable\r\n");
        return;
    }

    printf("SERVICE CFG: node=0x%04lX mod=%s fh=%u coding=%u ap=%u ap_period=%lu ap_mode=%s\r\n",
           (unsigned long)radio_main_get_node_id(),
           service_uart_mod_text(cfg.active_modulation),
           cfg.fh_enabled ? 1U : 0U,
           cfg.coding_enabled ? 1U : 0U,
           cfg.auto_ping_enabled ? 1U : 0U,
           (unsigned long)cfg.auto_ping_period_ms,
           service_uart_ap_mode_text(cfg.auto_ping_mode));

    if (cfg.active_modulation == RADIO_MAIN_MODULATION_FSK)
    {
        printf("SERVICE FSK: freq=%lu bitrate=%lu bw=%u shape=%u filter=%u pwr=%d pre=%u sync_len=%u sync=0x%08lX addr=%u crc=%u white=%u\r\n",
               (unsigned long)cfg.fsk.frequency_hz,
               (unsigned long)cfg.fsk.bitrate_bps,
               (unsigned int)cfg.fsk.rx_bandwidth,
               (unsigned int)cfg.fsk.shaping,
               (unsigned int)cfg.fsk.filter,
               (int)cfg.fsk.tx_power_dbm,
               (unsigned int)cfg.fsk.preamble_len,
               (unsigned int)cfg.fsk.sync_word_len,
               (unsigned long)(cfg.fsk.sync_word & 0xFFFFFFFFUL),
               (unsigned int)cfg.fsk.address_filter,
               (unsigned int)cfg.fsk.crc_type,
               cfg.fsk.data_whitening ? 1U : 0U);
    }
    else if (cfg.active_modulation == RADIO_MAIN_MODULATION_OOK)
    {
        printf("SERVICE OOK: freq=%lu bitrate=%lu bw=%u pwr=%d pre=%u sync_len=%u sync=0x%08lX thr=%u/%u\r\n",
               (unsigned long)cfg.ook.frequency_hz,
               (unsigned long)cfg.ook.bitrate_bps,
               (unsigned int)cfg.ook.rx_bandwidth,
               (int)cfg.ook.tx_power_dbm,
               (unsigned int)cfg.ook.preamble_len,
               (unsigned int)cfg.ook.sync_word_len,
               (unsigned long)cfg.ook.sync_word,
               (unsigned int)cfg.ook.threshold,
               (unsigned int)cfg.ook.threshold_value);
    }
    else
    {
        printf("SERVICE LORA: freq=%lu bw=%u sf=%u cr=4/%u pwr=%d pre=%u sync=0x%02X crc=%u hdr=%s iq=%u\r\n",
               (unsigned long)cfg.lora.frequency_hz,
               (unsigned int)cfg.lora.bandwidth,
               (unsigned int)cfg.lora.spreading_factor,
               (unsigned int)cfg.lora.coding_rate,
               (int)cfg.lora.tx_power_dbm,
               (unsigned int)cfg.lora.preamble_len,
               (unsigned int)cfg.lora.sync_word,
               cfg.lora.crc_on ? 1U : 0U,
               cfg.lora.implicit_header ? "implicit" : "explicit",
               cfg.lora.invert_iq ? 1U : 0U);
    }
}

/** @brief Internal helper: `service_uart_print_radio_result`. */
static void service_uart_print_radio_result(bool ok)
{
    char last_error[21];

    if (ok)
    {
        printf("SERVICE OK\r\n");
        return;
    }

    if (radio_main_get_last_error_text(last_error, sizeof(last_error)) && (last_error[0] != '\0'))
    {
        printf("SERVICE ERR: %s\r\n", last_error);
    }
    else
    {
        printf("SERVICE ERR: command failed\r\n");
    }
}

/** @brief Internal helper: `service_uart_handle_login`. */
static void service_uart_handle_login(const char *args)
{
    uint32_t code;
    uint32_t expected;

    if (!service_uart_parse_u32_arg(args, &code))
    {
        printf("SERVICE ERR: use LOGIN <device address>\r\n");
        return;
    }

    expected = service_uart_get_service_code();
    if ((expected != 0U) && (code == expected))
    {
        s_service_uart_unlocked = true;
        printf("SERVICE OK: panel enabled\r\n");
        service_uart_print_help();
    }
    else
    {
        printf("SERVICE ERR: bad code\r\n");
    }
}

/** @brief Internal helper: `service_uart_handle_send_hex`. */
static void service_uart_handle_send_hex(const char *payload)
{
    const char *error = NULL;
    uint8_t len = 0U;

    if (!service_uart_parse_hex_bytes(payload, s_service_uart_tx_bytes, &len, &error))
    {
        printf("SERVICE ERR: %s\r\n", (error != NULL) ? error : "bad hex");
        return;
    }

    if (radio_main_cmd_send_raw(s_service_uart_tx_bytes, len))
    {
        printf("SERVICE SEND OK: %u B\r\n", (unsigned int)len);
        return;
    }

    service_uart_print_radio_result(false);
}

/** @brief Internal helper: `service_uart_handle_text`. */
static void service_uart_handle_text(const char *args)
{
    const char *text;
    uint32_t dst;
    char token[SERVICE_UART_TOKEN_MAX];

    if (!service_uart_next_token(&args, token, sizeof(token)) ||
        !service_uart_parse_u32_arg(token, &dst))
    {
        printf("SERVICE ERR: use TEXT <dst> <ascii>\r\n");
        return;
    }

    text = service_uart_skip_space(args);
    if ((text == NULL) || (*text == '\0'))
    {
        printf("SERVICE ERR: empty text\r\n");
        return;
    }

    service_uart_print_radio_result(radio_main_cmd_send_user_text(text, dst));
}

/** @brief Internal helper: `service_uart_handle_mod`. */
static void service_uart_handle_mod(const char *args)
{
    radio_main_modulation_t mod;

    if (!service_uart_parse_mod_arg(args, &mod))
    {
        printf("SERVICE ERR: use MOD <LORA|FSK|OOK|0|1|2>\r\n");
        return;
    }

    service_uart_print_radio_result(radio_main_cmd_set_modulation((uint8_t)mod));
}

/** @brief Internal helper: `service_uart_handle_preset`. */
static void service_uart_handle_preset(const char *args)
{
    uint32_t preset;

    if (!service_uart_parse_u32_arg(args, &preset) || (preset > 2UL))
    {
        printf("SERVICE ERR: use PRESET <0..2>\r\n");
        return;
    }

    service_uart_print_radio_result(radio_main_cmd_set_lora_preset((uint8_t)preset));
}

/** @brief Internal helper: `service_uart_handle_freq`. */
static void service_uart_handle_freq(const char *args)
{
    uint32_t freq;

    if (!service_uart_parse_u32_arg(args, &freq))
    {
        printf("SERVICE ERR: use FREQ <Hz>\r\n");
        return;
    }

    service_uart_print_radio_result(radio_main_cmd_set_modulation_freq(freq));
}

/** @brief Internal helper: `service_uart_handle_bw`. */
static void service_uart_handle_bw(const char *args)
{
    uint32_t bw;

    if (!service_uart_parse_u32_arg(args, &bw) || (bw > 9UL))
    {
        printf("SERVICE ERR: use BW <0..9>\r\n");
        return;
    }

    service_uart_print_radio_result(radio_main_cmd_set_modulation_bw((uint8_t)bw));
}

/** @brief Internal helper: `service_uart_handle_option`. */
static void service_uart_handle_option(const char *args)
{
    uint32_t option;
    uint32_t value;
    char token[SERVICE_UART_TOKEN_MAX];

    if (!service_uart_next_token(&args, token, sizeof(token)) ||
        !service_uart_parse_u32_arg(token, &option) ||
        (option > (uint32_t)RADIO_MAIN_OPTION_OOK_RESET_DEFAULTS) ||
        !service_uart_parse_u32_arg(args, &value))
    {
        printf("SERVICE ERR: use OPTION <id> <value>\r\n");
        return;
    }

    service_uart_print_radio_result(radio_main_cmd_set_option((radio_main_option_t)option, value));
}

/** @brief Internal helper: `service_uart_handle_bool_cmd`. */
static void service_uart_handle_bool_cmd(const char *args, bool (*setter)(bool))
{
    bool value;

    if ((setter == NULL) || !service_uart_parse_bool_arg(args, &value))
    {
        printf("SERVICE ERR: use <cmd> <ON|OFF>\r\n");
        return;
    }

    service_uart_print_radio_result(setter(value));
}

/** @brief Internal helper: `service_uart_handle_u32_cmd`. */
static void service_uart_handle_u32_cmd(const char *args, bool (*setter)(uint32_t))
{
    uint32_t value;

    if ((setter == NULL) || !service_uart_parse_u32_arg(args, &value))
    {
        printf("SERVICE ERR: use <cmd> <value>\r\n");
        return;
    }

    service_uart_print_radio_result(setter(value));
}

/** @brief Internal helper: `service_uart_handle_auto_ping_mode`. */
static void service_uart_handle_auto_ping_mode(const char *args)
{
    char token[SERVICE_UART_TOKEN_MAX];
    radio_main_auto_ping_mode_t mode;

    if (!service_uart_next_token(&args, token, sizeof(token)))
    {
        printf("SERVICE ERR: use AUTOPING_MODE <FRAME|RAW>\r\n");
        return;
    }

    if (service_uart_token_equals(token, "FRAME") || service_uart_token_equals(token, "0"))
    {
        mode = RADIO_MAIN_AUTO_PING_FRAME;
    }
    else if (service_uart_token_equals(token, "RAW") || service_uart_token_equals(token, "1"))
    {
        mode = RADIO_MAIN_AUTO_PING_RAW;
    }
    else
    {
        printf("SERVICE ERR: use AUTOPING_MODE <FRAME|RAW>\r\n");
        return;
    }

    service_uart_print_radio_result(radio_main_cmd_set_auto_ping_mode(mode));
}

/** @brief Internal helper: `service_uart_handle_save`. */
static void service_uart_handle_save(void)
{
    radio_main_runtime_cfg_t cfg;

    if (!radio_main_get_runtime_cfg(&cfg))
    {
        printf("SERVICE ERR: cfg unavailable\r\n");
        return;
    }

    if (security_main_cmd_set_radio_runtime_cfg(&cfg))
    {
        printf("SERVICE OK: saved\r\n");
    }
    else
    {
        printf("SERVICE ERR: save failed\r\n");
    }
}

/** @brief Internal helper: `service_uart_parse_hex_bytes`. */
static bool service_uart_parse_hex_bytes(const char *text,
                                         uint8_t *data,
                                         uint8_t *len_out,
                                         const char **error_out)
{
    const char *p = service_uart_skip_space(text);
    uint16_t count = 0U;

    if ((data == NULL) || (len_out == NULL))
    {
        if (error_out != NULL)
        {
            *error_out = "internal error";
        }
        return false;
    }

    while ((p != NULL) && (*p != '\0'))
    {
        int high;
        int low;

        while (isspace((unsigned char)*p) || (*p == ',') || (*p == ';'))
        {
            p++;
        }
        if (*p == '\0')
        {
            break;
        }
        if ((*p == '0') && ((p[1] == 'x') || (p[1] == 'X')))
        {
            p += 2;
        }

        high = service_uart_hex_nibble(*p);
        if (high < 0)
        {
            if (error_out != NULL)
            {
                *error_out = "bad hex byte";
            }
            return false;
        }
        p++;

        low = service_uart_hex_nibble(*p);
        if (low < 0)
        {
            if (error_out != NULL)
            {
                *error_out = "hex byte needs 2 digits";
            }
            return false;
        }
        p++;

        if (isxdigit((unsigned char)*p))
        {
            if (error_out != NULL)
            {
                *error_out = "hex byte too long";
            }
            return false;
        }
        if (count >= RADIO_LIB_MAX_PAYLOAD)
        {
            if (error_out != NULL)
            {
                *error_out = "too many bytes";
            }
            return false;
        }

        data[count] = (uint8_t)(((uint8_t)high << 4) | (uint8_t)low);
        count++;

        if ((*p != '\0') &&
            !isspace((unsigned char)*p) &&
            (*p != ',') &&
            (*p != ';'))
        {
            if (error_out != NULL)
            {
                *error_out = "missing separator";
            }
            return false;
        }
    }

    if (count == 0U)
    {
        if (error_out != NULL)
        {
            *error_out = "no bytes";
        }
        return false;
    }

    *len_out = (uint8_t)count;
    return true;
}

/** @brief Internal helper: `service_uart_parse_u32_token`. */
static bool service_uart_parse_u32_token(const char *token, uint32_t *value_out)
{
    const char *p;
    const char *scan;
    uint32_t value = 0UL;
    uint32_t base = 10UL;

    if ((token == NULL) || (value_out == NULL))
    {
        return false;
    }

    p = token;
    if ((p[0] == '0') && ((p[1] == 'x') || (p[1] == 'X')))
    {
        base = 16UL;
        p += 2;
    }
    else
    {
        scan = p;
        while (*scan != '\0')
        {
            if (((*scan >= 'A') && (*scan <= 'F')) ||
                ((*scan >= 'a') && (*scan <= 'f')))
            {
                base = 16UL;
                break;
            }
            scan++;
        }
    }
    if (*p == '\0')
    {
        return false;
    }

    while (*p != '\0')
    {
        int digit;

        if ((*p >= '0') && (*p <= '9'))
        {
            digit = (int)(*p - '0');
        }
        else if (base == 16UL)
        {
            digit = service_uart_hex_nibble(*p);
        }
        else
        {
            return false;
        }

        if ((digit < 0) || ((uint32_t)digit >= base) ||
            (value > ((UINT32_MAX - (uint32_t)digit) / base)))
        {
            return false;
        }

        value = (value * base) + (uint32_t)digit;
        p++;
    }

    *value_out = value;
    return true;
}

/** @brief Internal helper: `service_uart_parse_u32_arg`. */
static bool service_uart_parse_u32_arg(const char *args, uint32_t *value_out)
{
    char token[SERVICE_UART_TOKEN_MAX];

    if ((args == NULL) || (value_out == NULL) ||
        !service_uart_next_token(&args, token, sizeof(token)))
    {
        return false;
    }

    return service_uart_parse_u32_token(token, value_out);
}

/** @brief Internal helper: `service_uart_parse_bool_arg`. */
static bool service_uart_parse_bool_arg(const char *args, bool *value_out)
{
    char token[SERVICE_UART_TOKEN_MAX];

    if ((value_out == NULL) ||
        !service_uart_next_token(&args, token, sizeof(token)))
    {
        return false;
    }

    if (service_uart_token_equals(token, "ON") ||
        service_uart_token_equals(token, "1") ||
        service_uart_token_equals(token, "TRUE") ||
        service_uart_token_equals(token, "ENABLE"))
    {
        *value_out = true;
        return true;
    }

    if (service_uart_token_equals(token, "OFF") ||
        service_uart_token_equals(token, "0") ||
        service_uart_token_equals(token, "FALSE") ||
        service_uart_token_equals(token, "DISABLE"))
    {
        *value_out = false;
        return true;
    }

    return false;
}

/** @brief Internal helper: `service_uart_parse_mod_arg`. */
static bool service_uart_parse_mod_arg(const char *args, radio_main_modulation_t *mod_out)
{
    char token[SERVICE_UART_TOKEN_MAX];

    if ((mod_out == NULL) ||
        !service_uart_next_token(&args, token, sizeof(token)))
    {
        return false;
    }

    if (service_uart_token_equals(token, "LORA") || service_uart_token_equals(token, "0"))
    {
        *mod_out = RADIO_MAIN_MODULATION_LORA;
        return true;
    }
    if (service_uart_token_equals(token, "FSK") ||
        service_uart_token_equals(token, "GFSK") ||
        service_uart_token_equals(token, "1"))
    {
        *mod_out = RADIO_MAIN_MODULATION_FSK;
        return true;
    }
    if (service_uart_token_equals(token, "OOK") || service_uart_token_equals(token, "2"))
    {
        *mod_out = RADIO_MAIN_MODULATION_OOK;
        return true;
    }

    return false;
}

/** @brief Internal helper: `service_uart_next_token`. */
static bool service_uart_next_token(const char **cursor, char *out, uint8_t out_size)
{
    const char *p;
    uint8_t len = 0U;

    if ((cursor == NULL) || (*cursor == NULL) || (out == NULL) || (out_size == 0U))
    {
        return false;
    }

    p = service_uart_skip_space(*cursor);
    if ((p == NULL) || (*p == '\0'))
    {
        return false;
    }

    while ((*p != '\0') && !isspace((unsigned char)*p))
    {
        if (len < (uint8_t)(out_size - 1U))
        {
            out[len] = *p;
            len++;
        }
        p++;
    }

    out[len] = '\0';
    *cursor = p;
    return (len > 0U);
}

/** @brief Internal helper: `service_uart_skip_space`. */
static const char *service_uart_skip_space(const char *text)
{
    while ((text != NULL) && isspace((unsigned char)*text))
    {
        text++;
    }

    return text;
}

/** @brief Internal helper: `service_uart_token_equals`. */
static bool service_uart_token_equals(const char *a, const char *b)
{
    if ((a == NULL) || (b == NULL))
    {
        return false;
    }

    while ((*a != '\0') && (*b != '\0'))
    {
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b))
        {
            return false;
        }
        a++;
        b++;
    }

    return ((*a == '\0') && (*b == '\0'));
}

/** @brief Internal helper: `service_uart_match_prefix`. */
static bool service_uart_match_prefix(const char **cursor, const char *prefix)
{
    const char *p;

    if ((cursor == NULL) || (*cursor == NULL) || (prefix == NULL))
    {
        return false;
    }

    p = service_uart_skip_space(*cursor);
    while (*prefix != '\0')
    {
        if ((*p == '\0') ||
            (toupper((unsigned char)*p) != toupper((unsigned char)*prefix)))
        {
            return false;
        }
        p++;
        prefix++;
    }

    *cursor = p;
    return true;
}

/** @brief Internal helper: `service_uart_hex_nibble`. */
static int service_uart_hex_nibble(char c)
{
    if ((c >= '0') && (c <= '9'))
    {
        return (int)(c - '0');
    }
    if ((c >= 'A') && (c <= 'F'))
    {
        return (int)(c - 'A' + 10);
    }
    if ((c >= 'a') && (c <= 'f'))
    {
        return (int)(c - 'a' + 10);
    }

    return -1;
}

/** @brief Internal helper: `service_uart_get_service_code`. */
static uint32_t service_uart_get_service_code(void)
{
    uint32_t node_id = radio_main_get_node_id();

    if (node_id == 0U)
    {
        node_id = laviet_local_node_id();
    }

    return node_id;
}

/** @brief Internal helper: `service_uart_mod_text`. */
static const char *service_uart_mod_text(radio_main_modulation_t modulation)
{
    switch (modulation)
    {
        case RADIO_MAIN_MODULATION_FSK:
            return "FSK";
        case RADIO_MAIN_MODULATION_OOK:
            return "OOK";
        case RADIO_MAIN_MODULATION_LORA:
        default:
            return "LORA";
    }
}

/** @brief Internal helper: `service_uart_ap_mode_text`. */
static const char *service_uart_ap_mode_text(radio_main_auto_ping_mode_t mode)
{
    switch (mode)
    {
        case RADIO_MAIN_AUTO_PING_RAW:
            return "RAW";
        case RADIO_MAIN_AUTO_PING_FRAME:
        default:
            return "FRAME";
    }
}

#else

void service_uart_create_task(void)
{
}

#endif /* SERVICE_UART */
