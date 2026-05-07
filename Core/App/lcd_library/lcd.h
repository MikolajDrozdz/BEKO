/**
 * @file lcd.h
 * @brief HD44780-compatible 20x4 LCD driver over I2C expander.
 */

#ifndef INC_LCD_H_
#define INC_LCD_H_

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Run the original LCD demo routine.
 */
void lcd_demo(void);

/**
 * @brief Initialize the LCD driver and display controller.
 */
void lcd_init(void);

/**
 * @brief Write a null-terminated string at the current cursor position.
 * @param str String bytes to send to the LCD.
 */
void lcd_write_string(uint8_t *str);

/**
 * @brief Move the LCD cursor.
 * @param row Zero-based row index.
 * @param column Zero-based column index.
 */
void lcd_set_cursor(uint8_t row, uint8_t column);

/**
 * @brief Clear the LCD display.
 */
void lcd_clear(void);

/**
 * @brief Enable or disable LCD backlight.
 * @param state Non-zero to enable, zero to disable.
 */
void lcd_backlight(uint8_t state);

/**
 * @brief Write one padded/truncated text line.
 * @param row Zero-based row index.
 * @param text Text to render.
 * @param width Maximum display width.
 * @return `true` when the line was written.
 */
bool lcd_write_line(uint8_t row, const char *text, uint8_t width);

/**
 * @brief Play the boot "HELLO BEKO" LCD animation.
 */
void lcd_animation_hello_beko(void);


#endif /* INC_LCD_H_ */
