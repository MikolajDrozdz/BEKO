// Code adapted from the example described here:
// https://embeddedthere.com/interfacing-stm32-with-i2c-lcd-with-hal-code-example/
// P. Korpas


/* Includes */
#include "lcd.h"

#include "../app.h"
#include "app_delay.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

#define I2C_ADDR 0x27 // I2C address of the PCF8574
#define RS_BIT 0 // Register select bit
#define EN_BIT 2 // Enable bit
#define BL_BIT 3 // Backlight bit
#define D4_BIT 4 // Data 4 bit
#define D5_BIT 5 // Data 5 bit
#define D6_BIT 6 // Data 6 bit
#define D7_BIT 7 // Data 7 bit

#define LCD_ROWS 4 // Number of rows on the LCD
#define LCD_COLS 20 // Number of columns on the LCD

// Define global variable for backlight state
uint8_t backlight_state = 1;

/* Private variables */
extern I2C_HandleTypeDef hi2c1;


/* Private function prototypes */
void lcd_write_nibble(uint8_t nibble, uint8_t rs);
void lcd_send_cmd(uint8_t cmd);
void lcd_send_data(uint8_t data);
void lcd_init();
static void lcd_write_nibble_raw(uint8_t nibble, uint8_t rs);

/**
  * @brief  The demo function (infinite loop)
  */
void lcd_demo(void)
{
  lcd_init();
  lcd_backlight(1); // Turn on backlight

  uint8_t *text = "Hello BEKO studs";
  char int_to_str[10];
  int count=0;

  while (1)
  {
	  sprintf(int_to_str, "%d", count);
	  lcd_clear();
	  lcd_set_cursor(0, 0);
	  lcd_write_string(text);
	  lcd_set_cursor(1, 0);
	  lcd_write_string(int_to_str);
	  count++;
	  memset(int_to_str, 0, sizeof(int_to_str));
	  app_delay_ms(100U);

  }
}

void lcd_write_nibble(uint8_t nibble, uint8_t rs) {
  if (!app_i2c_lock(0U))
  {
    return;
  }
  lcd_write_nibble_raw(nibble, rs);
  app_i2c_unlock();
}

static void lcd_write_nibble_raw(uint8_t nibble, uint8_t rs)
{
  uint8_t data = nibble << D4_BIT;
  data |= rs << RS_BIT;
  data |= backlight_state << BL_BIT; // Include backlight state in data
  data |= 1 << EN_BIT;
  (void)app_i2c_master_transmit(&hi2c1, I2C_ADDR << 1, &data, 1, 100U);
  app_delay_ms(1U);
  data &= ~(1 << EN_BIT);
  (void)app_i2c_master_transmit(&hi2c1, I2C_ADDR << 1, &data, 1, 100U);
}

void lcd_send_cmd(uint8_t cmd) {
  uint8_t upper_nibble = cmd >> 4;
  uint8_t lower_nibble = cmd & 0x0F;
  if (!app_i2c_lock(0U))
  {
    return;
  }
  lcd_write_nibble_raw(upper_nibble, 0);
  lcd_write_nibble_raw(lower_nibble, 0);
  app_i2c_unlock();
  if (cmd == 0x01 || cmd == 0x02) {
    app_delay_ms(2U);
  }
}

void lcd_send_data(uint8_t data) {
  uint8_t upper_nibble = data >> 4;
  uint8_t lower_nibble = data & 0x0F;
  if (!app_i2c_lock(0U))
  {
    return;
  }
  lcd_write_nibble_raw(upper_nibble, 1);
  lcd_write_nibble_raw(lower_nibble, 1);
  app_i2c_unlock();
}

void lcd_init() {
  app_delay_ms(50U);
  lcd_write_nibble(0x03, 0);
  app_delay_ms(5U);
  lcd_write_nibble(0x03, 0);
  app_delay_ms(1U);
  lcd_write_nibble(0x03, 0);
  app_delay_ms(1U);
  lcd_write_nibble(0x02, 0);
  lcd_send_cmd(0x28);
  lcd_send_cmd(0x0C);
  lcd_send_cmd(0x06);
  lcd_send_cmd(0x01);
  app_delay_ms(2U);
}

void lcd_write_string(uint8_t *str) {
  while (*str) {
    lcd_send_data(*str++);
  }
}

void lcd_set_cursor(uint8_t row, uint8_t column) {
    uint8_t address;
    switch (row) {
        case 0:
            address = 0x00; // line 1
            break;
        case 1:
            address = 0x40; // line 2
            break;
        case 2:
            address = 0x14; // line 3
            break;
        case 3:
            address = 0x54; // line 4
            break;
        default:
            address = 0x00;
    }
    address += column;              // 0..19
    lcd_send_cmd(0x80 | address);   // set DDRAM addr
}


void lcd_clear(void) {
	lcd_send_cmd(0x01);
    app_delay_ms(2U);
}

void lcd_backlight(uint8_t state) {
  if (state) {
    backlight_state = 1;
  } else {
    backlight_state = 0;
  }
}


void lcd_animation_hello_beko(void)
{
    lcd_clear();
    lcd_set_cursor(1, 5);
    lcd_write_string((uint8_t *)"HELLO BEKO");
    app_delay_ms(500U);

    lcd_clear();
    lcd_set_cursor(0, 4);
    lcd_write_string((uint8_t *)"  *      *  ");
    lcd_set_cursor(1, 3);
    lcd_write_string((uint8_t *)" * HELLO *  ");
    lcd_set_cursor(2, 4);
    lcd_write_string((uint8_t *)"  * BEKO *  ");
    lcd_set_cursor(3, 5);
    lcd_write_string((uint8_t *)"   *  *   ");
    app_delay_ms(500U);

    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_write_string((uint8_t *)"*   *   *   *   *");
    lcd_set_cursor(1, 2);
    lcd_write_string((uint8_t *)"*  HELLO  *");
    lcd_set_cursor(2, 3);
    lcd_write_string((uint8_t *)"*  BEKO  *");
    lcd_set_cursor(3, 0);
    lcd_write_string((uint8_t *)"*   *   *   *   *");
    app_delay_ms(500U);

    lcd_clear();
    lcd_set_cursor(1, 4);
    lcd_write_string((uint8_t *)" .   .   . ");
    lcd_set_cursor(2, 3);
    lcd_write_string((uint8_t *)".   .   .   .");
    app_delay_ms(500U);

    lcd_clear();
}

