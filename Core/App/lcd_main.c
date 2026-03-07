#include "lcd_main.h"

#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "lcd_library/lcd.h"
#include "task.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define LCD_MAIN_ROWS              4U
#define LCD_MAIN_COLS              20U
#define LCD_MAIN_QUEUE_LENGTH      24U
#define LCD_TASK_STACK_SIZE        3072U
#define LCD_TASK_STACK_WORDS       (LCD_TASK_STACK_SIZE / sizeof(StackType_t))

typedef enum
{
    LCD_MAIN_MSG_SET_LINE = 0,
    LCD_MAIN_MSG_SET_LINES,
    LCD_MAIN_MSG_PUSH_MONITOR,
    LCD_MAIN_MSG_SET_MODE,
    LCD_MAIN_MSG_SHOW_POPUP,
    LCD_MAIN_MSG_SHOW_BOOT,
    LCD_MAIN_MSG_CLEAR
} lcd_main_msg_type_t;

typedef struct
{
    lcd_main_msg_type_t type;
    uint8_t line_index;
    lcd_main_mode_t mode;
    char text0[LCD_MAIN_COLS + 1U];
    char text1[LCD_MAIN_COLS + 1U];
    char text2[LCD_MAIN_COLS + 1U];
    char text3[LCD_MAIN_COLS + 1U];
} lcd_main_msg_t;

static osThreadId_t s_lcd_task = NULL;
static osMessageQueueId_t s_lcd_queue = NULL;
static StaticTask_t s_lcd_task_cb;
static StackType_t s_lcd_task_stack[LCD_TASK_STACK_WORDS];

static lcd_main_mode_t s_mode = LCD_MODE_MONITOR;
static char s_monitor_lines[LCD_MAIN_ROWS][LCD_MAIN_COLS + 1U];
static char s_ui_lines[LCD_MAIN_ROWS][LCD_MAIN_COLS + 1U];
static char s_rendered_lines[LCD_MAIN_ROWS][LCD_MAIN_COLS + 1U];
static bool s_render_cache_valid = false;

static void lcd_main_task_fn(void *argument);
static void lcd_main_fill_line(char *dst, const char *src);
static void lcd_main_fill_line_from_payload(char *dst, int16_t rssi_dbm, const uint8_t *data, uint32_t length);
static void lcd_main_clear_lines(char lines[LCD_MAIN_ROWS][LCD_MAIN_COLS + 1U]);
static void lcd_main_shift_up_and_append(char lines[LCD_MAIN_ROWS][LCD_MAIN_COLS + 1U], const char *line);
static void lcd_main_render_mode(void);
static void lcd_main_render_lines(char lines[LCD_MAIN_ROWS][LCD_MAIN_COLS + 1U]);
static void lcd_main_invalidate_render_cache(void);
static bool lcd_main_post_message(const lcd_main_msg_t *msg);

static const osThreadAttr_t s_lcd_task_attr =
{
    .name = "lcd_task",
    .priority = (osPriority_t)osPriorityLow,
    .stack_mem = s_lcd_task_stack,
    .stack_size = sizeof(s_lcd_task_stack),
    .cb_mem = &s_lcd_task_cb,
    .cb_size = sizeof(s_lcd_task_cb)
};

void lcd_main_create_task(void)
{
    if (s_lcd_queue == NULL)
    {
        s_lcd_queue = osMessageQueueNew(LCD_MAIN_QUEUE_LENGTH, sizeof(lcd_main_msg_t), NULL);
        if (s_lcd_queue == NULL)
        {
            printf("LCD: queue create failed\r\n");
            return;
        }
    }

    if (s_lcd_task == NULL)
    {
        s_lcd_task = osThreadNew(lcd_main_task_fn, NULL, &s_lcd_task_attr);
        if (s_lcd_task == NULL)
        {
            printf("LCD: task create failed\r\n");
        }
    }
}

bool lcd_main_set_line(uint8_t line_index, const char *text)
{
    lcd_main_msg_t msg;

    if (line_index >= LCD_MAIN_ROWS)
    {
        return false;
    }

    memset(&msg, 0, sizeof(msg));
    msg.type = LCD_MAIN_MSG_SET_LINE;
    msg.line_index = line_index;
    lcd_main_fill_line(msg.text0, text);
    return lcd_main_post_message(&msg);
}

bool lcd_main_set_lines(const char *line0, const char *line1)
{
    lcd_main_msg_t msg;

    memset(&msg, 0, sizeof(msg));
    msg.type = LCD_MAIN_MSG_SET_LINES;
    lcd_main_fill_line(msg.text0, line0);
    lcd_main_fill_line(msg.text1, line1);
    return lcd_main_post_message(&msg);
}

bool lcd_main_push_message(int16_t rssi_dbm, const uint8_t *data, uint32_t length)
{
    lcd_main_msg_t msg;

    if ((data == NULL) || (length == 0U))
    {
        return false;
    }

    memset(&msg, 0, sizeof(msg));
    msg.type = LCD_MAIN_MSG_PUSH_MONITOR;
    lcd_main_fill_line_from_payload(msg.text0, rssi_dbm, data, length);
    return lcd_main_post_message(&msg);
}

bool lcd_main_set_mode(lcd_main_mode_t mode)
{
    lcd_main_msg_t msg;

    memset(&msg, 0, sizeof(msg));
    msg.type = LCD_MAIN_MSG_SET_MODE;
    msg.mode = mode;
    return lcd_main_post_message(&msg);
}

bool lcd_main_show_popup(const char *l0, const char *l1, const char *l2, const char *l3)
{
    lcd_main_msg_t msg;

    memset(&msg, 0, sizeof(msg));
    msg.type = LCD_MAIN_MSG_SHOW_POPUP;
    lcd_main_fill_line(msg.text0, l0);
    lcd_main_fill_line(msg.text1, l1);
    lcd_main_fill_line(msg.text2, l2);
    lcd_main_fill_line(msg.text3, l3);
    return lcd_main_post_message(&msg);
}

bool lcd_main_show_boot_hello(void)
{
    lcd_main_msg_t msg;

    memset(&msg, 0, sizeof(msg));
    msg.type = LCD_MAIN_MSG_SHOW_BOOT;
    return lcd_main_post_message(&msg);
}

static void lcd_main_task_fn(void *argument)
{
    lcd_main_msg_t msg;
    bool render_required;

    (void)argument;

    lcd_init();
    lcd_backlight(1U);
    lcd_clear();

    lcd_main_clear_lines(s_monitor_lines);
    lcd_main_clear_lines(s_ui_lines);
    lcd_main_clear_lines(s_rendered_lines);
    lcd_main_fill_line(s_monitor_lines[0], "HELLO BEKO");
    lcd_main_fill_line(s_monitor_lines[1], "RX monitor...");
    s_render_cache_valid = false;

    lcd_animation_hello_beko();
    s_mode = LCD_MODE_MONITOR;
    lcd_main_render_mode();

    for (;;)
    {
        if (osMessageQueueGet(s_lcd_queue, &msg, NULL, osWaitForever) != osOK)
        {
            continue;
        }

        render_required = false;

        do
        {
            switch (msg.type)
            {
                case LCD_MAIN_MSG_SET_LINE:
                    if (msg.line_index < LCD_MAIN_ROWS)
                    {
                        lcd_main_fill_line(s_ui_lines[msg.line_index], msg.text0);
                        render_required = true;
                    }
                    break;

                case LCD_MAIN_MSG_SET_LINES:
                    lcd_main_clear_lines(s_ui_lines);
                    lcd_main_fill_line(s_ui_lines[0], msg.text0);
                    lcd_main_fill_line(s_ui_lines[1], msg.text1);
                    render_required = true;
                    break;

                case LCD_MAIN_MSG_PUSH_MONITOR:
                    lcd_main_shift_up_and_append(s_monitor_lines, msg.text0);
                    if (s_mode == LCD_MODE_MONITOR)
                    {
                        render_required = true;
                    }
                    break;

                case LCD_MAIN_MSG_SET_MODE:
                    if ((msg.mode == LCD_MODE_MENU) || (msg.mode == LCD_MODE_POPUP))
                    {
                        lcd_main_clear_lines(s_ui_lines);
                    }
                    s_mode = msg.mode;
                    render_required = true;
                    break;

                case LCD_MAIN_MSG_SHOW_POPUP:
                    lcd_main_clear_lines(s_ui_lines);
                    lcd_main_fill_line(s_ui_lines[0], msg.text0);
                    lcd_main_fill_line(s_ui_lines[1], msg.text1);
                    lcd_main_fill_line(s_ui_lines[2], msg.text2);
                    lcd_main_fill_line(s_ui_lines[3], msg.text3);
                    s_mode = LCD_MODE_POPUP;
                    render_required = true;
                    break;

                case LCD_MAIN_MSG_SHOW_BOOT:
                    lcd_animation_hello_beko();
                    s_mode = LCD_MODE_MONITOR;
                    lcd_main_invalidate_render_cache();
                    render_required = true;
                    break;

                case LCD_MAIN_MSG_CLEAR:
                    lcd_main_clear_lines(s_monitor_lines);
                    lcd_main_clear_lines(s_ui_lines);
                    render_required = true;
                    break;

                default:
                    break;
            }
        } while (osMessageQueueGet(s_lcd_queue, &msg, NULL, 0U) == osOK);

        if (render_required)
        {
            lcd_main_render_mode();
        }
    }
}

static void lcd_main_fill_line(char *dst, const char *src)
{
    uint8_t i;

    if (dst == NULL)
    {
        return;
    }

    for (i = 0U; i < LCD_MAIN_COLS; i++)
    {
        if ((src != NULL) && (src[i] != '\0'))
        {
            dst[i] = src[i];
        }
        else
        {
            dst[i] = ' ';
        }
    }
    dst[LCD_MAIN_COLS] = '\0';
}

static void lcd_main_fill_line_from_payload(char *dst, int16_t rssi_dbm, const uint8_t *data, uint32_t length)
{
    uint32_t i;
    uint32_t msg_max_len = (LCD_MAIN_COLS - 5U);
    int32_t rssi_display;

    if (dst == NULL)
    {
        return;
    }

    if (rssi_dbm > 999)
    {
        rssi_display = 999;
    }
    else if (rssi_dbm < -99)
    {
        rssi_display = -99;
    }
    else
    {
        rssi_display = rssi_dbm;
    }

    dst[0] = ' ';
    dst[1] = ' ';
    dst[2] = ' ';
    dst[3] = ' ';
    if (rssi_display < 0)
    {
        int32_t mag = -rssi_display;
        dst[0] = '-';
        dst[1] = (char)('0' + ((mag / 10) % 10));
        dst[2] = (char)('0' + (mag % 10));
    }
    else
    {
        dst[1] = (char)('0' + ((rssi_display / 100) % 10));
        dst[2] = (char)('0' + ((rssi_display / 10) % 10));
        dst[3] = (char)('0' + (rssi_display % 10));
    }
    dst[4] = ':';

    for (i = 0U; i < msg_max_len; i++)
    {
        uint32_t src_idx = i;
        uint32_t dst_idx = i + 5U;
        if ((src_idx < length) && (data != NULL))
        {
            char c = (char)data[src_idx];
            dst[dst_idx] = isprint((unsigned char)c) ? c : '.';
        }
        else
        {
            dst[dst_idx] = ' ';
        }
    }

    dst[LCD_MAIN_COLS] = '\0';
}

static void lcd_main_clear_lines(char lines[LCD_MAIN_ROWS][LCD_MAIN_COLS + 1U])
{
    uint8_t row;

    for (row = 0U; row < LCD_MAIN_ROWS; row++)
    {
        lcd_main_fill_line(lines[row], NULL);
    }
}

static void lcd_main_shift_up_and_append(char lines[LCD_MAIN_ROWS][LCD_MAIN_COLS + 1U], const char *line)
{
    uint8_t row;

    for (row = 0U; row < (LCD_MAIN_ROWS - 1U); row++)
    {
        memcpy(lines[row], lines[row + 1U], (LCD_MAIN_COLS + 1U));
    }
    lcd_main_fill_line(lines[LCD_MAIN_ROWS - 1U], line);
}

static void lcd_main_render_mode(void)
{
    if (s_mode == LCD_MODE_MONITOR)
    {
        lcd_main_render_lines(s_monitor_lines);
    }
    else
    {
        lcd_main_render_lines(s_ui_lines);
    }
}

static void lcd_main_render_lines(char lines[LCD_MAIN_ROWS][LCD_MAIN_COLS + 1U])
{
    uint8_t row;
    bool all_ok = true;

    for (row = 0U; row < LCD_MAIN_ROWS; row++)
    {
        if ((!s_render_cache_valid) ||
            (memcmp(lines[row], s_rendered_lines[row], (LCD_MAIN_COLS + 1U)) != 0))
        {
            if (lcd_write_line(row, lines[row], LCD_MAIN_COLS))
            {
                memcpy(s_rendered_lines[row], lines[row], (LCD_MAIN_COLS + 1U));
            }
            else
            {
                all_ok = false;
            }
        }
    }

    s_render_cache_valid = all_ok;
}

static void lcd_main_invalidate_render_cache(void)
{
    s_render_cache_valid = false;
}

static bool lcd_main_post_message(const lcd_main_msg_t *msg)
{
    lcd_main_msg_t dropped;
    osStatus_t st;

    if ((msg == NULL) || (s_lcd_queue == NULL))
    {
        return false;
    }

    st = osMessageQueuePut(s_lcd_queue, msg, 0U, 0U);
    if (st == osOK)
    {
        return true;
    }

    if (st == osErrorResource)
    {
        if (msg->type != LCD_MAIN_MSG_PUSH_MONITOR)
        {
            return (osMessageQueuePut(s_lcd_queue, msg, 0U, 5U) == osOK);
        }

        if (osMessageQueueGet(s_lcd_queue, &dropped, NULL, 0U) == osOK)
        {
            return (osMessageQueuePut(s_lcd_queue, msg, 0U, 0U) == osOK);
        }
    }

    return false;
}
