#include "lcd_main.h"

#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"
#include "lcd_library/lcd.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define LCD_MAIN_ROWS          4U
#define LCD_MAIN_LINE_LEN      20U
#define LCD_MAIN_QUEUE_LENGTH  16U
#define LCD_TASK_STACK_SIZE    2048U
#define LCD_TASK_STACK_WORDS   (LCD_TASK_STACK_SIZE / sizeof(StackType_t))

typedef enum
{
    LCD_MAIN_MSG_SET_LINE = 0,
    LCD_MAIN_MSG_SET_LINES,
    LCD_MAIN_MSG_PUSH_MESSAGE,
    LCD_MAIN_MSG_CLEAR
} lcd_main_msg_type_t;

typedef struct
{
    lcd_main_msg_type_t type;
    uint8_t line_index;
    char text0[LCD_MAIN_LINE_LEN + 1U];
    char text1[LCD_MAIN_LINE_LEN + 1U];
} lcd_main_msg_t;

static osThreadId_t s_lcd_task = NULL;
static osMessageQueueId_t s_lcd_queue = NULL;
static char s_lcd_lines[LCD_MAIN_ROWS][LCD_MAIN_LINE_LEN + 1U];
static StaticTask_t s_lcd_task_cb;
static StackType_t s_lcd_task_stack[LCD_TASK_STACK_WORDS];

static void lcd_main_task_fn(void *argument);
static void lcd_main_fill_line(char *dst, const char *src);
static void lcd_main_fill_line_from_payload(char *dst, int16_t rssi_dbm, const uint8_t *data, uint32_t length);
static void lcd_main_clear_lines(void);
static void lcd_main_render_lines(void);
static void lcd_main_shift_up_and_append(const char *line);
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
            printf("LCD task: queue create failed\r\n");
            return;
        }
    }

    if (s_lcd_task == NULL)
    {
        s_lcd_task = osThreadNew(lcd_main_task_fn, NULL, &s_lcd_task_attr);
        if (s_lcd_task == NULL)
        {
            printf("LCD task: create failed\r\n");
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
    msg.type = LCD_MAIN_MSG_PUSH_MESSAGE;
    lcd_main_fill_line_from_payload(msg.text0, rssi_dbm, data, length);
    return lcd_main_post_message(&msg);
}

static void lcd_main_task_fn(void *argument)
{
    lcd_main_msg_t msg;

    (void)argument;

    lcd_init();
    lcd_backlight(1U);
    lcd_clear();

    lcd_main_clear_lines();
    lcd_main_fill_line(s_lcd_lines[0], "LCD task ready");
    lcd_main_fill_line(s_lcd_lines[1], "Waiting data...");
    lcd_main_render_lines();

    for (;;)
    {
        if (osMessageQueueGet(s_lcd_queue, &msg, NULL, osWaitForever) != osOK)
        {
            continue;
        }

        switch (msg.type)
        {
            case LCD_MAIN_MSG_SET_LINE:
                if (msg.line_index < LCD_MAIN_ROWS)
                {
                    lcd_main_fill_line(s_lcd_lines[msg.line_index], msg.text0);
                }
                break;

            case LCD_MAIN_MSG_SET_LINES:
                lcd_main_fill_line(s_lcd_lines[0], msg.text0);
                lcd_main_fill_line(s_lcd_lines[1], msg.text1);
                break;

            case LCD_MAIN_MSG_PUSH_MESSAGE:
                lcd_main_shift_up_and_append(msg.text0);
                break;

            case LCD_MAIN_MSG_CLEAR:
                lcd_main_clear_lines();
                break;

            default:
                break;
        }

        lcd_main_render_lines();
    }
}

static void lcd_main_fill_line(char *dst, const char *src)
{
    uint8_t i;

    if (dst == NULL)
    {
        return;
    }

    for (i = 0U; i < LCD_MAIN_LINE_LEN; i++)
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

    dst[LCD_MAIN_LINE_LEN] = '\0';
}

static void lcd_main_fill_line_from_payload(char *dst, int16_t rssi_dbm, const uint8_t *data, uint32_t length)
{
    uint32_t i;
    uint32_t msg_max_len = (LCD_MAIN_LINE_LEN - 5U);
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

    dst[LCD_MAIN_LINE_LEN] = '\0';
}

static void lcd_main_clear_lines(void)
{
    uint8_t row;

    for (row = 0U; row < LCD_MAIN_ROWS; row++)
    {
        lcd_main_fill_line(s_lcd_lines[row], NULL);
    }
}

static void lcd_main_render_lines(void)
{
    uint8_t row;

    for (row = 0U; row < LCD_MAIN_ROWS; row++)
    {
        lcd_set_cursor(row, 0U);
        lcd_write_string((uint8_t *)s_lcd_lines[row]);
    }
}

static void lcd_main_shift_up_and_append(const char *line)
{
    uint8_t row;

    for (row = 0U; row < (LCD_MAIN_ROWS - 1U); row++)
    {
        memcpy(s_lcd_lines[row], s_lcd_lines[row + 1U], (LCD_MAIN_LINE_LEN + 1U));
    }

    lcd_main_fill_line(s_lcd_lines[LCD_MAIN_ROWS - 1U], line);
}

static bool lcd_main_post_message(const lcd_main_msg_t *msg)
{
    lcd_main_msg_t dropped_msg;
    osStatus_t status;

    if ((msg == NULL) || (s_lcd_queue == NULL))
    {
        return false;
    }

    status = osMessageQueuePut(s_lcd_queue, msg, 0U, 0U);
    if (status == osOK)
    {
        return true;
    }

    if (status == osErrorResource)
    {
        if (osMessageQueueGet(s_lcd_queue, &dropped_msg, NULL, 0U) == osOK)
        {
            status = osMessageQueuePut(s_lcd_queue, msg, 0U, 0U);
            return (status == osOK);
        }
    }

    return false;
}
