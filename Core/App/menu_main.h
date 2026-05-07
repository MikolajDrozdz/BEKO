/**
 * @file menu_main.h
 * @brief LCD menu controller task.
 */

#ifndef APP_MENU_MAIN_H_
#define APP_MENU_MAIN_H_

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Notification classes accepted by the menu task.
 */
typedef enum
{
    MENU_NOTIFICATION_RX = 0, /**< Received user/radio payload. */
    MENU_NOTIFICATION_WARNING, /**< Non-fatal warning for the UI. */
    MENU_NOTIFICATION_ERROR, /**< Error popup or status message. */
    MENU_NOTIFICATION_PAIRING, /**< Pairing workflow update. */
    MENU_NOTIFICATION_SECURITY, /**< Security subsystem update. */
    MENU_NOTIFICATION_DELIVERY /**< Delivery/ACK status update. */
} menu_notification_type_t;

/**
 * @brief Message passed from application subsystems to the menu task.
 */
typedef struct
{
    menu_notification_type_t type; /**< Notification category. */
    int16_t rssi_dbm; /**< RSSI for RX notifications. */
    uint32_t device_code; /**< Source or related device identifier. */
    uint32_t reply_device_code; /**< Reply destination suggested by notification. */
    char text[21]; /**< LCD-width text payload, null-terminated when possible. */
} menu_notification_t;

/**
 * @brief Create the LCD menu task and notification queue.
 */
void menu_main_create_task(void);

/**
 * @brief Queue a notification for menu/UI handling.
 * @param n Notification object.
 * @return `true` when the notification was queued.
 */
bool menu_main_post_notification(const menu_notification_t *n);

#endif /* APP_MENU_MAIN_H_ */
