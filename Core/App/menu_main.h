/**
 * @file menu_main.h
 * @brief LCD menu controller task.
 */

#ifndef APP_MENU_MAIN_H_
#define APP_MENU_MAIN_H_

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    MENU_NOTIFICATION_RX = 0,
    MENU_NOTIFICATION_WARNING,
    MENU_NOTIFICATION_ERROR,
    MENU_NOTIFICATION_PAIRING,
    MENU_NOTIFICATION_SECURITY
} menu_notification_type_t;

typedef struct
{
    menu_notification_type_t type;
    int16_t rssi_dbm;
    uint32_t device_code;
    char text[21];
} menu_notification_t;

void menu_main_create_task(void);
bool menu_main_post_notification(const menu_notification_t *n);

#endif /* APP_MENU_MAIN_H_ */
