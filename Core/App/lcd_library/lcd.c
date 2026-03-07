// Code adapted from the example described here:
// https://embeddedthere.com/interfacing-stm32-with-i2c-lcd-with-hal-code-example/
// P. Korpas

/* Includes */
#include "lcd.h"

#include "../app.h"
#include "app_delay.h"
#include "main.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define I2C_ADDR                 0x27U
#define LCD_I2C_ADDR_8BIT        (I2C_ADDR << 1)
#define RS_BIT                   0U
#define EN_BIT                   2U
#define BL_BIT                   3U
#define D4_BIT                   4U

#define LCD_ROWS                 4U
#define LCD_COLS                 20U

#define LCD_TX_TIMEOUT_MS        20U
#define LCD_DMA_TIMEOUT_MS       20U
#define LCD_USE_I2C_DMA          0U

/* Private variables */
extern I2C_HandleTypeDef hi2c1;

static uint8_t backlight_state = 1U;

/* Private function prototypes */
static bool lcd_i2c_tx_locked(const uint8_t *data, uint16_t len);
static bool lcd_write_nibble_locked(uint8_t nibble, uint8_t rs);
static bool lcd_send_cmd_locked(uint8_t cmd);
static bool lcd_send_data_locked(uint8_t data);
static uint8_t lcd_ddram_base(uint8_t row);

/**
  * @brief  The demo function (infinite loop)
  */
void lcd_demo(void)
{
  char int_to_str[12];
  int count = 0;

  lcd_init();
  lcd_backlight(1U);

  while (1)
  {
    snprintf(int_to_str, sizeof(int_to_str), "%d", count);
    lcd_clear();
    lcd_write_line(0U, "Hello BEKO studs", LCD_COLS);
    lcd_write_line(1U, int_to_str, LCD_COLS);
    count++;
    app_delay_ms(100U);
  }
}

void lcd_write_nibble(uint8_t nibble, uint8_t rs)
{
  if (!app_i2c_lock(0U))
  {
    return;
  }

  (void)lcd_write_nibble_locked(nibble, rs);

  app_i2c_unlock();
}

static bool lcd_i2c_tx_locked(const uint8_t *data, uint16_t len)
{
  HAL_StatusTypeDef st;

  if ((data == NULL) || (len == 0U))
  {
    return false;
  }

#if (LCD_USE_I2C_DMA == 1)
  if (hi2c1.hdmatx != NULL)
  {
    st = HAL_I2C_Master_Transmit_DMA(&hi2c1, LCD_I2C_ADDR_8BIT, (uint8_t *)data, len);
    if (st == HAL_OK)
    {
      uint32_t start = HAL_GetTick();

      while (HAL_I2C_GetState(&hi2c1) != HAL_I2C_STATE_READY)
      {
        if ((HAL_GetTick() - start) >= LCD_DMA_TIMEOUT_MS)
        {
          return false;
        }
      }

      return true;
    }
  }
#endif

  st = app_i2c_master_transmit(&hi2c1, LCD_I2C_ADDR_8BIT, data, len, LCD_TX_TIMEOUT_MS);
  return (st == HAL_OK);
}

static bool lcd_write_nibble_locked(uint8_t nibble, uint8_t rs)
{
  uint8_t bus = (uint8_t)((nibble & 0x0FU) << D4_BIT);
  uint8_t frame[2];

  bus |= (uint8_t)((rs & 0x01U) << RS_BIT);
  bus |= (uint8_t)((backlight_state & 0x01U) << BL_BIT);

  frame[0] = (uint8_t)(bus | (1U << EN_BIT));
  frame[1] = (uint8_t)(bus & (uint8_t)~(1U << EN_BIT));

  return lcd_i2c_tx_locked(frame, (uint16_t)sizeof(frame));
}

static bool lcd_send_cmd_locked(uint8_t cmd)
{
  if (!lcd_write_nibble_locked((uint8_t)(cmd >> 4), 0U))
  {
    return false;
  }

  if (!lcd_write_nibble_locked((uint8_t)(cmd & 0x0FU), 0U))
  {
    return false;
  }

  if ((cmd == 0x01U) || (cmd == 0x02U))
  {
    app_delay_ms(2U);
  }

  return true;
}

static bool lcd_send_data_locked(uint8_t data)
{
  if (!lcd_write_nibble_locked((uint8_t)(data >> 4), 1U))
  {
    return false;
  }

  return lcd_write_nibble_locked((uint8_t)(data & 0x0FU), 1U);
}

void lcd_send_cmd(uint8_t cmd)
{
  if (!app_i2c_lock(0U))
  {
    return;
  }

  (void)lcd_send_cmd_locked(cmd);

  app_i2c_unlock();
}

void lcd_send_data(uint8_t data)
{
  if (!app_i2c_lock(0U))
  {
    return;
  }

  (void)lcd_send_data_locked(data);

  app_i2c_unlock();
}

void lcd_init(void)
{
  app_delay_ms(50U);
  lcd_write_nibble(0x03U, 0U);
  app_delay_ms(5U);
  lcd_write_nibble(0x03U, 0U);
  app_delay_ms(1U);
  lcd_write_nibble(0x03U, 0U);
  app_delay_ms(1U);
  lcd_write_nibble(0x02U, 0U);

  lcd_send_cmd(0x28U);
  lcd_send_cmd(0x0CU);
  lcd_send_cmd(0x06U);
  lcd_send_cmd(0x01U);
  app_delay_ms(2U);
}

void lcd_write_string(uint8_t *str)
{
  if (str == NULL)
  {
    return;
  }

  while (*str != '\0')
  {
    lcd_send_data(*str++);
  }
}

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

void lcd_set_cursor(uint8_t row, uint8_t column)
{
  uint8_t address;

  if ((row >= LCD_ROWS) || (column >= LCD_COLS))
  {
    return;
  }

  address = (uint8_t)(lcd_ddram_base(row) + column);
  lcd_send_cmd((uint8_t)(0x80U | address));
}

bool lcd_write_line(uint8_t row, const char *text, uint8_t width)
{
  uint8_t i;
  bool ok = true;

  if (row >= LCD_ROWS)
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

  if (!lcd_send_cmd_locked((uint8_t)(0x80U | lcd_ddram_base(row))))
  {
    app_i2c_unlock();
    return false;
  }

  for (i = 0U; i < width; i++)
  {
    uint8_t c = ' ';

    if ((text != NULL) && (text[i] != '\0'))
    {
      c = (uint8_t)text[i];
    }

    if (!lcd_send_data_locked(c))
    {
      ok = false;
      break;
    }
  }

  app_i2c_unlock();
  return ok;
}

void lcd_clear(void)
{
  lcd_send_cmd(0x01U);
  app_delay_ms(2U);
}

void lcd_backlight(uint8_t state)
{
  backlight_state = (state != 0U) ? 1U : 0U;
}

void lcd_animation_hello_beko(void)
{
  lcd_clear();
  lcd_write_line(0U, "HELLO BEKO", LCD_COLS);
  lcd_write_line(1U, "RX monitor...", LCD_COLS);
  lcd_write_line(2U, "", LCD_COLS);
  lcd_write_line(3U, "", LCD_COLS);
  app_delay_ms(250U);
}
