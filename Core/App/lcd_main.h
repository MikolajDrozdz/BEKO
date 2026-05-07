/**
 * @file lcd_main.h
 * @brief RTOS-facing LCD rendering task API.
 */

#ifndef APP_LCD_MAIN_H_
#define APP_LCD_MAIN_H_

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief High-level ownership mode for the LCD task.
 */
typedef enum
{
    LCD_MODE_MONITOR = 0, /**< Show radio monitor history. */
    LCD_MODE_MENU, /**< Show menu-provided 4-line screen. */
    LCD_MODE_POPUP /**< Show modal/popup content. */
} lcd_main_mode_t;

/**
 * @brief Create the LCD task and message queue.
 */
void lcd_main_create_task(void);

/**
 * @brief Update one monitor line.
 * @param line_index Zero-based LCD line index.
 * @param text Text copied and padded to display width.
 * @return `true` when the update request was queued.
 */
bool lcd_main_set_line(uint8_t line_index, const char *text);

/**
 * @brief Update the first two monitor lines.
 * @param line0 First line text.
 * @param line1 Second line text.
 * @return `true` when the update request was queued.
 */
bool lcd_main_set_lines(const char *line0, const char *line1);

/**
 * @brief Replace the menu screen with four LCD lines.
 * @param l0 First line.
 * @param l1 Second line.
 * @param l2 Third line.
 * @param l3 Fourth line.
 * @return `true` when the screen was queued.
 */
bool lcd_main_show_menu(const char *l0, const char *l1, const char *l2, const char *l3);

/**
 * @brief Append an RX message to the monitor history.
 * @param rssi_dbm RSSI associated with the payload.
 * @param data Payload bytes.
 * @param length Payload length.
 * @return `true` when the message was queued.
 */
bool lcd_main_push_message(int16_t rssi_dbm, const uint8_t *data, uint32_t length);

/**
 * @brief Append an RX message with source identifier to the monitor history.
 * @param rssi_dbm RSSI associated with the payload.
 * @param source_id Source node/device identifier.
 * @param data Payload bytes.
 * @param length Payload length.
 * @return `true` when the message was queued.
 */
bool lcd_main_push_message_from(int16_t rssi_dbm, uint32_t source_id, const uint8_t *data, uint32_t length);

/**
 * @brief Switch LCD ownership mode.
 * @param mode New LCD mode.
 * @return `true` when the mode change was queued.
 */
bool lcd_main_set_mode(lcd_main_mode_t mode);

/**
 * @brief Show a four-line popup screen.
 * @param l0 First line.
 * @param l1 Second line.
 * @param l2 Third line.
 * @param l3 Fourth line.
 * @return `true` when the popup was queued.
 */
bool lcd_main_show_popup(const char *l0, const char *l1, const char *l2, const char *l3);

/**
 * @brief Queue the boot welcome animation.
 * @return `true` when the request was queued.
 */
bool lcd_main_show_boot_hello(void);

/**
 * @brief Scroll monitor history one line toward older entries.
 * @return `true` when the request was queued.
 */
bool lcd_main_monitor_scroll_up(void);

/**
 * @brief Scroll monitor history one line toward newer entries.
 * @return `true` when the request was queued.
 */
bool lcd_main_monitor_scroll_down(void);

#endif /* APP_LCD_MAIN_H_ */
