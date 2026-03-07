/*
 * lcd.h
 *
 *  Created on: Feb 19, 2024
 *      Author: Przemysław Korpas
 */

#ifndef INC_LCD_H_
#define INC_LCD_H_

#include <stdbool.h>
#include <stdint.h>

/* Public interface */

void lcd_demo(void);
void lcd_init(void);
void lcd_write_string(uint8_t *str);
void lcd_set_cursor(uint8_t row, uint8_t column);
void lcd_clear(void);
void lcd_backlight(uint8_t state);
bool lcd_write_line(uint8_t row, const char *text, uint8_t width);


void lcd_animation_hello_beko(void);


#endif /* INC_LCD_H_ */
