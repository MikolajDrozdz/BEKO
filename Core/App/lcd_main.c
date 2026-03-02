#include "lcd_main.h"

#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "lcd_library/lcd.h"

#include <string.h>

#define LCD_MAIN_LINE_LEN 20U
#define LCD_TASK_STACK_SIZE   2048U
#define LCD_TASK_STACK_WORDS  (LCD_TASK_STACK_SIZE / sizeof(StackType_t))

static osThreadId_t s_lcd_task = NULL;
static osMutexId_t s_lcd_mutex = NULL;
static bool s_lcd_ready = false;
static char s_lcd_buffer[4][LCD_MAIN_LINE_LEN + 1U];
static bool s_lcd_dirty = false;
static StaticTask_t s_lcd_task_cb;
static StackType_t s_lcd_task_stack[LCD_TASK_STACK_WORDS];
static StaticSemaphore_t s_lcd_mutex_cb;

static void lcd_main_task_fn(void *argument);
static void lcd_main_copy_to_line(char *dst, const char *src);

static const osMutexAttr_t s_lcd_mutex_attr =
{
    .name = "lcd_mutex",
    .cb_mem = &s_lcd_mutex_cb,
    .cb_size = sizeof(s_lcd_mutex_cb)
};

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
    if (s_lcd_mutex == NULL)
    {
        s_lcd_mutex = osMutexNew(&s_lcd_mutex_attr);
    }
    if (s_lcd_mutex == NULL)
    {
        return;
    }

    if (osMutexAcquire(s_lcd_mutex, 100U) == osOK)
    {
        lcd_main_copy_to_line(s_lcd_buffer[0], "LCD task ready");
        lcd_main_copy_to_line(s_lcd_buffer[1], "Waiting data...");
        s_lcd_dirty = true;
        (void)osMutexRelease(s_lcd_mutex);
    }

    if (s_lcd_task == NULL)
    {
        s_lcd_task = osThreadNew(lcd_main_task_fn, NULL, &s_lcd_task_attr);
    }
}

bool lcd_main_set_line(uint8_t line_index, const char *text)
{
    if ((line_index > 1U) || (s_lcd_mutex == NULL))
    {
        return false;
    }
    if (osMutexAcquire(s_lcd_mutex, 100U) != osOK)
    {
        return false;
    }

    lcd_main_copy_to_line(s_lcd_buffer[line_index], text);
    s_lcd_dirty = true;

    (void)osMutexRelease(s_lcd_mutex);
    return true;
}

bool lcd_main_set_lines(const char *line0, const char *line1)
{
    if (s_lcd_mutex == NULL)
    {
        return false;
    }
    if (osMutexAcquire(s_lcd_mutex, 100U) != osOK)
    {
        return false;
    }

    lcd_main_copy_to_line(s_lcd_buffer[0], line0);
    lcd_main_copy_to_line(s_lcd_buffer[1], line1);
    s_lcd_dirty = true;

    (void)osMutexRelease(s_lcd_mutex);
    return true;
}

static void lcd_main_task_fn(void *argument)
{
    char line0[LCD_MAIN_LINE_LEN + 1U];
    char line1[LCD_MAIN_LINE_LEN + 1U];
    bool should_update;

    (void)argument;

    lcd_clear();
    lcd_backlight(1U);
    s_lcd_ready = true;

    for (;;)
    {
        should_update = false;

        if (osMutexAcquire(s_lcd_mutex, 100U) == osOK)
        {
            if (s_lcd_dirty)
            {
            	// Copy lines to local buffer to minimize time spent in mutex.
                should_update = true;
            }
            (void)osMutexRelease(s_lcd_mutex);
        }

        if (should_update)
        {
            // Update LCD with new lines. This is done outside of the mutex to avoid blocking other tasks while the LCD is being updated.
        }

        osDelay(50U);
    }
}

static void lcd_main_copy_to_line(char *dst, const char *src)
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

