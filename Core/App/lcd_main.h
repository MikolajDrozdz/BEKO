#ifndef APP_LCD_MAIN_H_
#define APP_LCD_MAIN_H_

#include <stdbool.h>
#include <stdint.h>

void lcd_main_create_task(void);
bool lcd_main_set_line(uint8_t line_index, const char *text);
bool lcd_main_set_lines(const char *line0, const char *line1);

#endif /* APP_LCD_MAIN_H_ */

