#include "lcd_main.h"

#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "laviet_frame.h"
#include "lcd_library/lcd.h"
#include "task.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define LCD_MAIN_ROWS              4U
#define LCD_MAIN_COLS              20U
#define LCD_MAIN_QUEUE_LENGTH      256U
#define LCD_MAIN_MONITOR_HISTORY   64U
#define LCD_TASK_STACK_SIZE        8192U
#define LCD_TASK_STACK_WORDS       (LCD_TASK_STACK_SIZE / sizeof(StackType_t))

typedef enum
{
    LCD_MAIN_MSG_SET_LINE = 0,
    LCD_MAIN_MSG_SET_LINES,
    LCD_MAIN_MSG_SHOW_MENU,
    LCD_MAIN_MSG_PUSH_MONITOR,
    LCD_MAIN_MSG_SET_MODE,
    LCD_MAIN_MSG_SHOW_POPUP,
    LCD_MAIN_MSG_SHOW_BOOT,
    LCD_MAIN_MSG_CLEAR,
    LCD_MAIN_MSG_MONITOR_SCROLL_UP,
    LCD_MAIN_MSG_MONITOR_SCROLL_DOWN
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
static char s_monitor_history[LCD_MAIN_MONITOR_HISTORY][LCD_MAIN_COLS + 1U];
static char s_ui_lines[LCD_MAIN_ROWS][LCD_MAIN_COLS + 1U];
static char s_rendered_lines[LCD_MAIN_ROWS][LCD_MAIN_COLS + 1U];
static uint8_t s_monitor_history_head = 0U;
static uint8_t s_monitor_history_count = 0U;
static uint8_t s_monitor_view_offset = 0U;
static bool s_render_cache_valid = false;

static void lcd_main_task_fn(void *argument);
static void lcd_main_fill_line(char *dst, const char *src);
static void lcd_main_write_source_field(char *dst, uint32_t source_id);
static void lcd_main_fill_line_from_payload(char *dst,
                                            int16_t rssi_dbm,
                                            uint32_t source_id,
                                            const uint8_t *data,
                                            uint32_t length);
static void lcd_main_clear_lines(char lines[LCD_MAIN_ROWS][LCD_MAIN_COLS + 1U]);
static void lcd_main_monitor_history_append(const char *line);
static void lcd_main_monitor_rebuild_lines(void);
static void lcd_main_render_mode(void);
static void lcd_main_render_lines(char lines[LCD_MAIN_ROWS][LCD_MAIN_COLS + 1U]);
static void lcd_main_invalidate_render_cache(void);
static bool lcd_main_is_monitor_message_type(lcd_main_msg_type_t type);
static void lcd_main_drop_pending_messages(void);
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

/**
 * @brief Update all four menu rows in one queue message.
 *
 * Menu navigation used to enqueue each line separately, which could leave stale rows on screen
 * when the LCD queue was busy. Sending the whole frame atomically keeps the UI coherent.
 */
bool lcd_main_show_menu(const char *l0, const char *l1, const char *l2, const char *l3)
{
    lcd_main_msg_t msg;

    memset(&msg, 0, sizeof(msg));
    msg.type = LCD_MAIN_MSG_SHOW_MENU;
    lcd_main_fill_line(msg.text0, l0);
    lcd_main_fill_line(msg.text1, l1);
    lcd_main_fill_line(msg.text2, l2);
    lcd_main_fill_line(msg.text3, l3);
    return lcd_main_post_message(&msg);
}

bool lcd_main_push_message(int16_t rssi_dbm, const uint8_t *data, uint32_t length)
{
    return lcd_main_push_message_from(rssi_dbm, 0U, data, length);
}

bool lcd_main_push_message_from(int16_t rssi_dbm, uint32_t source_id, const uint8_t *data, uint32_t length)
{
    lcd_main_msg_t msg;

    if ((data == NULL) || (length == 0U))
    {
        return false;
    }

    memset(&msg, 0, sizeof(msg));
    msg.type = LCD_MAIN_MSG_PUSH_MONITOR;
    lcd_main_fill_line_from_payload(msg.text0, rssi_dbm, source_id, data, length);
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

bool lcd_main_monitor_scroll_up(void)
{
    lcd_main_msg_t msg;

    memset(&msg, 0, sizeof(msg));
    msg.type = LCD_MAIN_MSG_MONITOR_SCROLL_UP;
    return lcd_main_post_message(&msg);
}

bool lcd_main_monitor_scroll_down(void)
{
    lcd_main_msg_t msg;

    memset(&msg, 0, sizeof(msg));
    msg.type = LCD_MAIN_MSG_MONITOR_SCROLL_DOWN;
    return lcd_main_post_message(&msg);
}

/** @brief Internal helper: `lcd_main_task_fn`. */
static void lcd_main_task_fn(void *argument)
{
    lcd_main_msg_t msg;
    bool render_required;

    (void)argument;

    lcd_init();
    lcd_backlight(1U);
    lcd_clear();

    lcd_main_clear_lines(s_monitor_lines);
    lcd_main_clear_lines(s_monitor_history);
    lcd_main_clear_lines(s_ui_lines);
    lcd_main_clear_lines(s_rendered_lines);
    s_monitor_history_head = 0U;
    s_monitor_history_count = 0U;
    s_monitor_view_offset = 0U;
    s_render_cache_valid = false;

    lcd_animation_hello_beko();
    lcd_main_clear_lines(s_monitor_lines);
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
                    lcd_main_invalidate_render_cache();
                    render_required = true;
                    break;

                case LCD_MAIN_MSG_SHOW_MENU:
                    lcd_main_fill_line(s_ui_lines[0], msg.text0);
                    lcd_main_fill_line(s_ui_lines[1], msg.text1);
                    lcd_main_fill_line(s_ui_lines[2], msg.text2);
                    lcd_main_fill_line(s_ui_lines[3], msg.text3);
                    s_mode = LCD_MODE_MENU;
                    lcd_main_invalidate_render_cache();
                    render_required = true;
                    break;

                case LCD_MAIN_MSG_PUSH_MONITOR:
                    lcd_main_monitor_history_append(msg.text0);
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
                    if (msg.mode == LCD_MODE_MONITOR)
                    {
                        s_monitor_view_offset = 0U;
                        lcd_main_monitor_rebuild_lines();
                    }
                    lcd_main_invalidate_render_cache();
                    render_required = true;
                    break;

                case LCD_MAIN_MSG_SHOW_POPUP:
                    lcd_main_clear_lines(s_ui_lines);
                    lcd_main_fill_line(s_ui_lines[0], msg.text0);
                    lcd_main_fill_line(s_ui_lines[1], msg.text1);
                    lcd_main_fill_line(s_ui_lines[2], msg.text2);
                    lcd_main_fill_line(s_ui_lines[3], msg.text3);
                    s_mode = LCD_MODE_POPUP;
                    lcd_main_invalidate_render_cache();
                    render_required = true;
                    break;

                case LCD_MAIN_MSG_SHOW_BOOT:
                    lcd_animation_hello_beko();
                    s_mode = LCD_MODE_MONITOR;
                    s_monitor_view_offset = 0U;
                    lcd_main_monitor_rebuild_lines();
                    lcd_main_invalidate_render_cache();
                    render_required = true;
                    break;

                case LCD_MAIN_MSG_CLEAR:
                    lcd_main_clear_lines(s_monitor_lines);
                    lcd_main_clear_lines(s_monitor_history);
                    lcd_main_clear_lines(s_ui_lines);
                    s_monitor_history_head = 0U;
                    s_monitor_history_count = 0U;
                    s_monitor_view_offset = 0U;
                    lcd_main_monitor_rebuild_lines();
                    lcd_main_invalidate_render_cache();
                    render_required = true;
                    break;

                case LCD_MAIN_MSG_MONITOR_SCROLL_UP:
                    if (s_monitor_history_count > LCD_MAIN_ROWS)
                    {
                        uint8_t max_offset = (uint8_t)(s_monitor_history_count - LCD_MAIN_ROWS);

                        if (s_monitor_view_offset < max_offset)
                        {
                            s_monitor_view_offset++;
                            lcd_main_monitor_rebuild_lines();
                            render_required = (s_mode == LCD_MODE_MONITOR);
                        }
                    }
                    break;

                case LCD_MAIN_MSG_MONITOR_SCROLL_DOWN:
                    if (s_monitor_view_offset > 0U)
                    {
                        s_monitor_view_offset--;
                        lcd_main_monitor_rebuild_lines();
                        render_required = (s_mode == LCD_MODE_MONITOR);
                    }
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

/** @brief Internal helper: `lcd_main_fill_line`. */
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

/** @brief Internal helper: `lcd_main_write_source_field`. */
static void lcd_main_write_source_field(char *dst, uint32_t source_id)
{
    static const char hex[] = "0123456789ABCDEF";
    uint16_t source16;

    if (dst == NULL)
    {
        return;
    }

    if (source_id == LAVIET_GATEWAY_ID)
    {
        dst[0] = 'G';
        dst[1] = 'A';
        dst[2] = 'T';
        dst[3] = 'E';
        return;
    }
    if (source_id == LAVIET_BROADCAST_ID)
    {
        dst[0] = 'B';
        dst[1] = 'C';
        dst[2] = 'S';
        dst[3] = 'T';
        return;
    }
    if ((source_id == 0UL) || (source_id > 0xFFFFUL))
    {
        dst[0] = '-';
        dst[1] = '-';
        dst[2] = '-';
        dst[3] = '-';
        return;
    }

    source16 = (uint16_t)source_id;
    dst[0] = hex[(source16 >> 12) & 0x0FU];
    dst[1] = hex[(source16 >> 8) & 0x0FU];
    dst[2] = hex[(source16 >> 4) & 0x0FU];
    dst[3] = hex[source16 & 0x0FU];
}

/** @brief Internal helper: `lcd_main_fill_line_from_payload`. */
static void lcd_main_fill_line_from_payload(char *dst,
                                            int16_t rssi_dbm,
                                            uint32_t source_id,
                                            const uint8_t *data,
                                            uint32_t length)
{
    uint32_t i;
    uint32_t msg_start = 5U;
    uint32_t msg_max_len = (LCD_MAIN_COLS - msg_start);

    if (dst == NULL)
    {
        return;
    }

    (void)rssi_dbm;
    lcd_main_fill_line(dst, NULL);
    lcd_main_write_source_field(dst, source_id);
    dst[4] = ':';

    for (i = 0U; i < msg_max_len; i++)
    {
        uint32_t src_idx = i;
        uint32_t dst_idx = i + msg_start;
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

/** @brief Internal helper: `lcd_main_clear_lines`. */
static void lcd_main_clear_lines(char lines[LCD_MAIN_ROWS][LCD_MAIN_COLS + 1U])
{
    uint8_t row;

    for (row = 0U; row < LCD_MAIN_ROWS; row++)
    {
        lcd_main_fill_line(lines[row], NULL);
    }
}

/** @brief Internal helper: `lcd_main_monitor_history_append`. */
static void lcd_main_monitor_history_append(const char *line)
{
    uint8_t old_count = s_monitor_history_count;
    uint8_t max_offset;
    uint8_t idx = s_monitor_history_head;

    lcd_main_fill_line(s_monitor_history[idx], line);
    s_monitor_history_head = (uint8_t)((s_monitor_history_head + 1U) % LCD_MAIN_MONITOR_HISTORY);
    if (s_monitor_history_count < LCD_MAIN_MONITOR_HISTORY)
    {
        s_monitor_history_count++;
    }

    if ((old_count >= LCD_MAIN_ROWS) && (s_monitor_view_offset > 0U))
    {
        max_offset = (s_monitor_history_count > LCD_MAIN_ROWS) ?
                     (uint8_t)(s_monitor_history_count - LCD_MAIN_ROWS) : 0U;
        if (s_monitor_view_offset < max_offset)
        {
            s_monitor_view_offset++;
        }
    }

    lcd_main_monitor_rebuild_lines();
}

/** @brief Internal helper: `lcd_main_monitor_rebuild_lines`. */
static void lcd_main_monitor_rebuild_lines(void)
{
    uint8_t oldest_idx;
    uint8_t start;
    uint8_t visible;
    uint8_t row_base;
    uint8_t i;
    uint8_t max_offset;

    lcd_main_clear_lines(s_monitor_lines);
    if (s_monitor_history_count == 0U)
    {
        return;
    }

    max_offset = (s_monitor_history_count > LCD_MAIN_ROWS) ?
                 (uint8_t)(s_monitor_history_count - LCD_MAIN_ROWS) : 0U;
    if (s_monitor_view_offset > max_offset)
    {
        s_monitor_view_offset = max_offset;
    }

    if (s_monitor_history_count > LCD_MAIN_ROWS)
    {
        start = (uint8_t)(s_monitor_history_count - LCD_MAIN_ROWS - s_monitor_view_offset);
        visible = LCD_MAIN_ROWS;
        row_base = 0U;
    }
    else
    {
        start = 0U;
        visible = s_monitor_history_count;
        row_base = (uint8_t)(LCD_MAIN_ROWS - visible);
    }

    oldest_idx = (uint8_t)((s_monitor_history_head + LCD_MAIN_MONITOR_HISTORY - s_monitor_history_count) %
                           LCD_MAIN_MONITOR_HISTORY);
    for (i = 0U; i < visible; i++)
    {
        uint8_t history_idx = (uint8_t)((oldest_idx + start + i) % LCD_MAIN_MONITOR_HISTORY);

        memcpy(s_monitor_lines[row_base + i],
               s_monitor_history[history_idx],
               (LCD_MAIN_COLS + 1U));
    }
}

/** @brief Internal helper: `lcd_main_render_mode`. */
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

/** @brief Internal helper: `lcd_main_render_lines`. */
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

/** @brief Internal helper: `lcd_main_invalidate_render_cache`. */
static void lcd_main_invalidate_render_cache(void)
{
    s_render_cache_valid = false;
}

/** @brief Internal helper: `lcd_main_is_monitor_message_type`. */
static bool lcd_main_is_monitor_message_type(lcd_main_msg_type_t type)
{
    return (type == LCD_MAIN_MSG_PUSH_MONITOR);
}

/*
 * Full-screen UI updates must win over any stale backlog. Dropping pending LCD messages here is
 * cheaper than letting old popups arrive late and overwrite the current menu state.
 */
static void lcd_main_drop_pending_messages(void)
{
    lcd_main_msg_t dropped;

    if (s_lcd_queue == NULL)
    {
        return;
    }

    while (osMessageQueueGet(s_lcd_queue, &dropped, NULL, 0U) == osOK)
    {
    }
}

/** @brief Internal helper: `lcd_main_post_message`. */
static bool lcd_main_post_message(const lcd_main_msg_t *msg)
{
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
        if (lcd_main_is_monitor_message_type(msg->type))
        {
            return false;
        }

        lcd_main_drop_pending_messages();
        st = osMessageQueuePut(s_lcd_queue, msg, 0U, 5U);
        if (st == osOK)
        {
            return true;
        }
    }

    return false;
}
