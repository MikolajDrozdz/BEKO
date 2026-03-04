#ifndef APP_LCD_MAIN_H_
#define APP_LCD_MAIN_H_

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    LCD_MODE_MONITOR = 0,
    LCD_MODE_MENU,
    LCD_MODE_POPUP
} lcd_main_mode_t;

void lcd_main_create_task(void);
bool lcd_main_set_line(uint8_t line_index, const char *text);
bool lcd_main_set_lines(const char *line0, const char *line1);
bool lcd_main_push_message(int16_t rssi_dbm, const uint8_t *data, uint32_t length);
bool lcd_main_set_mode(lcd_main_mode_t mode);
bool lcd_main_show_popup(const char *l0, const char *l1, const char *l2, const char *l3);
bool lcd_main_show_boot_hello(void);

#endif /* APP_LCD_MAIN_H_ */

