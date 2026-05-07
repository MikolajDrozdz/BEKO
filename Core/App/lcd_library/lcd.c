#include "lcd.h"

#include "../app.h"
#include "app_delay.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define LCD_I2C_ADDR_DEFAULT_7BIT        0x27U
#define LCD_I2C_ADDR_ALT_7BIT            0x3FU

#define LCD_RS_MASK                      0x01U
#define LCD_EN_MASK                      0x04U
#define LCD_BL_MASK                      0x08U

#define LCD_ROWS                         4U
#define LCD_COLS                         20U

#define LCD_I2C_TIMEOUT_MS               20U
#define LCD_INIT_RETRY_COUNT             3U

#define LCD_CMD_CLEAR                    0x01U
#define LCD_CMD_HOME                     0x02U
#define LCD_CMD_ENTRY_MODE               0x06U
#define LCD_CMD_DISPLAY_ON               0x0CU
#define LCD_CMD_DISPLAY_OFF              0x08U
#define LCD_CMD_FUNCTION_SET             0x28U
#define LCD_CMD_SET_DDRAM                0x80U

typedef struct
{
    bool initialized;
    uint8_t backlight;
    uint8_t addr_7bit;
} lcd_state_t;

extern I2C_HandleTypeDef hi2c1;

static lcd_state_t s_lcd_state =
{
    .initialized = false,
    .backlight = LCD_BL_MASK,
    .addr_7bit = LCD_I2C_ADDR_DEFAULT_7BIT
};

static bool s_lcd_ready_logged = false;
static bool s_lcd_failure_logged = false;

static bool lcd_i2c_tx_locked(const uint8_t *data, uint16_t length);
static bool lcd_write_nibble_locked(uint8_t nibble, bool rs);
static bool lcd_write_byte_locked(uint8_t value, bool rs);
static bool lcd_send_command_locked(uint8_t command);
static bool lcd_send_data_locked(uint8_t data);
static uint8_t lcd_ddram_base(uint8_t row);
static bool lcd_init_locked(void);
static bool lcd_ensure_ready(void);
static void lcd_animation_clear_frame(char frame[LCD_ROWS][LCD_COLS + 1U]);
static void lcd_animation_put_text(char frame[LCD_ROWS][LCD_COLS + 1U],
                                   uint8_t row,
                                   uint8_t col,
                                   const char *text);
static void lcd_animation_put_char(char frame[LCD_ROWS][LCD_COLS + 1U],
                                   int8_t row,
                                   int8_t col,
                                   char ch);
static void lcd_animation_render_frame(char frame[LCD_ROWS][LCD_COLS + 1U], uint32_t hold_ms);

/** @brief Internal helper: `lcd_i2c_tx_locked`. */
static bool lcd_i2c_tx_locked(const uint8_t *data, uint16_t length)
{
    if ((data == NULL) || (length == 0U))
    {
        return false;
    }

    return (app_i2c_master_transmit(&hi2c1,
                                    (uint16_t)(s_lcd_state.addr_7bit << 1),
                                    data,
                                    length,
                                    LCD_I2C_TIMEOUT_MS) == HAL_OK);
}

/**
 * @brief Send one 4-bit nibble through the PCF8574-style expander.
 *
 * The enable pulse is packed into a single I2C burst. This matches the behavior of common
 * backpack modules better than sending separate start/stop transactions for each edge.
 */
static bool lcd_write_nibble_locked(uint8_t nibble, bool rs)
{
    uint8_t bus;
    uint8_t frame[3];

    bus = (uint8_t)((nibble & 0x0FU) << 4);
    bus |= s_lcd_state.backlight;
    if (rs)
    {
        bus |= LCD_RS_MASK;
    }

    frame[0] = bus;
    frame[1] = (uint8_t)(bus | LCD_EN_MASK);
    frame[2] = bus;

    return lcd_i2c_tx_locked(frame, (uint16_t)sizeof(frame));
}

/** @brief Internal helper: `lcd_write_byte_locked`. */
static bool lcd_write_byte_locked(uint8_t value, bool rs)
{
    if (!lcd_write_nibble_locked((uint8_t)(value >> 4), rs))
    {
        return false;
    }

    if (!lcd_write_nibble_locked((uint8_t)(value & 0x0FU), rs))
    {
        return false;
    }

    return true;
}

/** @brief Internal helper: `lcd_send_command_locked`. */
static bool lcd_send_command_locked(uint8_t command)
{
    if (!lcd_write_byte_locked(command, false))
    {
        return false;
    }

    if ((command == LCD_CMD_CLEAR) || (command == LCD_CMD_HOME))
    {
        app_delay_ms(3U);
    }
    else
    {
        app_delay_ms(1U);
    }

    return true;
}

/** @brief Internal helper: `lcd_send_data_locked`. */
static bool lcd_send_data_locked(uint8_t data)
{
    if (!lcd_write_byte_locked(data, true))
    {
        return false;
    }

    app_delay_ms(1U);
    return true;
}

/** @brief Internal helper: `lcd_ddram_base`. */
static uint8_t lcd_ddram_base(uint8_t row)
{
    switch (row)
    {
        case 0U:
            return 0x00U;
        case 1U:
            return 0x40U;
        case 2U:
            return 0x14U;
        case 3U:
            return 0x54U;
        default:
            return 0x00U;
    }
}

/**
 * @brief Initialize a 20x4 HD44780 controller in 4-bit mode.
 *
 * The sequence is intentionally conservative. Many 20x4 modules need longer delays after
 * power-up and become unstable if the first mode-set is sent too early.
 */
static bool lcd_init_locked(void)
{
    uint8_t candidates[3];
    uint32_t candidate_idx;
    uint32_t attempt;

    candidates[0] = s_lcd_state.addr_7bit;
    candidates[1] = LCD_I2C_ADDR_DEFAULT_7BIT;
    candidates[2] = LCD_I2C_ADDR_ALT_7BIT;

    for (candidate_idx = 0U; candidate_idx < (sizeof(candidates) / sizeof(candidates[0])); candidate_idx++)
    {
        uint32_t previous_idx;
        bool duplicate = false;

        for (previous_idx = 0U; previous_idx < candidate_idx; previous_idx++)
        {
            if (candidates[previous_idx] == candidates[candidate_idx])
            {
                duplicate = true;
                break;
            }
        }
        if (duplicate)
        {
            continue;
        }

        s_lcd_state.addr_7bit = candidates[candidate_idx];

        for (attempt = 0U; attempt < LCD_INIT_RETRY_COUNT; attempt++)
        {
            app_delay_ms(60U);

            if (!lcd_i2c_tx_locked(&s_lcd_state.backlight, 1U))
            {
                continue;
            }

            app_delay_ms(5U);

            if (!lcd_write_nibble_locked(0x03U, false))
            {
                continue;
            }
            app_delay_ms(5U);

            if (!lcd_write_nibble_locked(0x03U, false))
            {
                continue;
            }
            app_delay_ms(5U);

            if (!lcd_write_nibble_locked(0x03U, false))
            {
                continue;
            }
            app_delay_ms(2U);

            if (!lcd_write_nibble_locked(0x02U, false))
            {
                continue;
            }
            app_delay_ms(2U);

            if (!lcd_send_command_locked(LCD_CMD_FUNCTION_SET))
            {
                continue;
            }

            if (!lcd_send_command_locked(LCD_CMD_FUNCTION_SET))
            {
                continue;
            }

            if (!lcd_send_command_locked(LCD_CMD_DISPLAY_OFF))
            {
                continue;
            }

            if (!lcd_send_command_locked(LCD_CMD_CLEAR))
            {
                continue;
            }

            if (!lcd_send_command_locked(LCD_CMD_ENTRY_MODE))
            {
                continue;
            }

            if (!lcd_send_command_locked(LCD_CMD_DISPLAY_ON))
            {
                continue;
            }

            if (!lcd_send_command_locked(LCD_CMD_HOME))
            {
                continue;
            }

            s_lcd_state.initialized = true;
            return true;
        }
    }

    s_lcd_state.initialized = false;
    return false;
}

/**
 * @brief Ensure that the LCD controller is initialized before any write operation.
 *
 * Re-initialization is attempted automatically because brown-out or backpack glitches can leave
 * the display in an undefined state while the MCU keeps running.
 */
static bool lcd_ensure_ready(void)
{
    bool ok;

    if (s_lcd_state.initialized)
    {
        return true;
    }

    ok = false;

    if (app_i2c_lock(0U))
    {
        ok = lcd_init_locked();
        app_i2c_unlock();
    }

    return ok;
}

/** @brief Internal helper: `lcd_animation_clear_frame`. */
static void lcd_animation_clear_frame(char frame[LCD_ROWS][LCD_COLS + 1U])
{
    uint8_t row;
    uint8_t col;

    for (row = 0U; row < LCD_ROWS; row++)
    {
        for (col = 0U; col < LCD_COLS; col++)
        {
            frame[row][col] = ' ';
        }
        frame[row][LCD_COLS] = '\0';
    }
}

/** @brief Internal helper: `lcd_animation_put_text`. */
static void lcd_animation_put_text(char frame[LCD_ROWS][LCD_COLS + 1U],
                                   uint8_t row,
                                   uint8_t col,
                                   const char *text)
{
    uint8_t idx;

    if ((text == NULL) || (row >= LCD_ROWS) || (col >= LCD_COLS))
    {
        return;
    }

    idx = 0U;
    while ((text[idx] != '\0') && ((uint8_t)(col + idx) < LCD_COLS))
    {
        frame[row][col + idx] = text[idx];
        idx++;
    }
}

/** @brief Internal helper: `lcd_animation_put_char`. */
static void lcd_animation_put_char(char frame[LCD_ROWS][LCD_COLS + 1U],
                                   int8_t row,
                                   int8_t col,
                                   char ch)
{
    if ((row < 0) || (col < 0))
    {
        return;
    }

    if (((uint8_t)row >= LCD_ROWS) || ((uint8_t)col >= LCD_COLS))
    {
        return;
    }

    frame[(uint8_t)row][(uint8_t)col] = ch;
}

/** @brief Internal helper: `lcd_animation_render_frame`. */
static void lcd_animation_render_frame(char frame[LCD_ROWS][LCD_COLS + 1U], uint32_t hold_ms)
{
    uint8_t row;

    lcd_clear();
    for (row = 0U; row < LCD_ROWS; row++)
    {
        (void)lcd_write_line(row, frame[row], LCD_COLS);
    }
    app_delay_ms(hold_ms);
}

void lcd_demo(void)
{
    char number[12];
    int count;

    lcd_init();
    lcd_backlight(1U);

    count = 0;
    while (1)
    {
        (void)snprintf(number, sizeof(number), "%d", count);
        lcd_clear();
        (void)lcd_write_line(0U, "Hello BEKO studs", LCD_COLS);
        (void)lcd_write_line(1U, number, LCD_COLS);
        count++;
        app_delay_ms(100U);
    }
}

void lcd_init(void)
{
    bool ok;

    s_lcd_state.initialized = false;
    ok = lcd_ensure_ready();
    if (ok)
    {
        if (!s_lcd_ready_logged)
        {
            printf("LCD: init OK addr=0x%02X\r\n", (unsigned int)s_lcd_state.addr_7bit);
            s_lcd_ready_logged = true;
        }
        s_lcd_failure_logged = false;
    }
    else if (!s_lcd_failure_logged)
    {
        printf("LCD: init failed addr=0x%02X err=0x%08lX\r\n",
               (unsigned int)s_lcd_state.addr_7bit,
               (unsigned long)HAL_I2C_GetError(&hi2c1));
        s_lcd_failure_logged = true;
    }
}

void lcd_write_string(uint8_t *str)
{
    if ((str == NULL) || (!lcd_ensure_ready()))
    {
        return;
    }

    if (!app_i2c_lock(0U))
    {
        return;
    }

    while (*str != '\0')
    {
        if (!lcd_send_data_locked(*str))
        {
            s_lcd_state.initialized = false;
            break;
        }

        str++;
    }

    app_i2c_unlock();
}

void lcd_set_cursor(uint8_t row, uint8_t column)
{
    uint8_t address;

    if ((row >= LCD_ROWS) || (column >= LCD_COLS) || (!lcd_ensure_ready()))
    {
        return;
    }

    address = (uint8_t)(lcd_ddram_base(row) + column);

    if (!app_i2c_lock(0U))
    {
        return;
    }

    if (!lcd_send_command_locked((uint8_t)(LCD_CMD_SET_DDRAM | address)))
    {
        s_lcd_state.initialized = false;
    }

    app_i2c_unlock();
}

void lcd_clear(void)
{
    if (!lcd_ensure_ready())
    {
        return;
    }

    if (!app_i2c_lock(0U))
    {
        return;
    }

    if (!lcd_send_command_locked(LCD_CMD_CLEAR) ||
        !lcd_send_command_locked(LCD_CMD_HOME))
    {
        s_lcd_state.initialized = false;
    }

    app_i2c_unlock();
}

void lcd_backlight(uint8_t state)
{
    s_lcd_state.backlight = (state != 0U) ? LCD_BL_MASK : 0U;

    if (!app_i2c_lock(0U))
    {
        return;
    }

    if (!lcd_i2c_tx_locked(&s_lcd_state.backlight, 1U))
    {
        s_lcd_state.initialized = false;
    }

    app_i2c_unlock();
}

/**
 * @brief Write one logical LCD row and blank the remaining characters.
 *
 * Writing the whole row every time avoids leftover characters when a shorter string replaces a
 * longer one, which is especially visible in menu screens and popup overlays.
 */
bool lcd_write_line(uint8_t row, const char *text, uint8_t width)
{
    uint8_t i;

    if ((row >= LCD_ROWS) || (!lcd_ensure_ready()))
    {
        return false;
    }

    if (width > LCD_COLS)
    {
        width = LCD_COLS;
    }

    if (!app_i2c_lock(0U))
    {
        return false;
    }

    if (!lcd_send_command_locked((uint8_t)(LCD_CMD_SET_DDRAM | lcd_ddram_base(row))))
    {
        s_lcd_state.initialized = false;
        app_i2c_unlock();
        return false;
    }

    for (i = 0U; i < width; i++)
    {
        uint8_t ch;

        ch = ' ';
        if ((text != NULL) && (text[i] != '\0'))
        {
            ch = (uint8_t)text[i];
        }

        if (!lcd_send_data_locked(ch))
        {
            s_lcd_state.initialized = false;
            app_i2c_unlock();
            return false;
        }
    }

    app_i2c_unlock();
    return true;
}

void lcd_animation_hello_beko(void)
{
    char frame[LCD_ROWS][LCD_COLS + 1U];
    static const char title[] = "HELLO BEKO";
    static const char subtitle[] = "RX monitor...";
    static const int8_t blast_offsets[][2] =
    {
        {  0,  0 }, { -1,  0 }, {  1,  0 }, {  0, -3 }, {  0,  3 },
        { -1, -5 }, { -1,  5 }, {  1, -5 }, {  1,  5 }, { -2,  0 },
        {  2,  0 }, { -2, -7 }, { -2,  7 }, {  2, -7 }, {  2,  7 }
    };
    uint8_t frame_idx;
    int8_t center_row = 1;
    int8_t center_col = 10;

    /* 8 x 250 ms gives a simple 2 second boot animation without blocking too long. */
    for (frame_idx = 0U; frame_idx < 8U; frame_idx++)
    {
        uint8_t i;
        uint8_t active_count = (uint8_t)((frame_idx + 1U) * 2U);
        char spark = '.';

        if (active_count > (sizeof(blast_offsets) / sizeof(blast_offsets[0])))
        {
            active_count = (uint8_t)(sizeof(blast_offsets) / sizeof(blast_offsets[0]));
        }

        if (frame_idx >= 2U)
        {
            spark = '*';
        }
        if (frame_idx >= 5U)
        {
            spark = '+';
        }

        lcd_animation_clear_frame(frame);

        for (i = 0U; i < active_count; i++)
        {
            lcd_animation_put_char(frame,
                                   (int8_t)(center_row + blast_offsets[i][0]),
                                   (int8_t)(center_col + blast_offsets[i][1]),
                                   spark);
        }

        if (frame_idx >= 1U)
        {
            lcd_animation_put_text(frame, 1U, 5U, title);
        }

        if (frame_idx >= 6U)
        {
            lcd_animation_put_text(frame, 2U, 3U, subtitle);
        }

        lcd_animation_render_frame(frame, 250U);
    }
}
