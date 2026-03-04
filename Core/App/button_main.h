/**
 * @file button_main.h
 * @brief Polled button task (25 ms) with event queue.
 */

#ifndef APP_BUTTON_MAIN_H_
#define APP_BUTTON_MAIN_H_

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    BUTTON_EVENT_NONE = 0,
    BUTTON_EVENT_UP_SHORT,
    BUTTON_EVENT_DOWN_SHORT,
    BUTTON_EVENT_OK_SHORT,
    BUTTON_EVENT_OK_LONG
} button_event_t;

void button_main_create_task(void);
bool button_main_get_event(button_event_t *evt, uint32_t timeout_ms);

#endif /* APP_BUTTON_MAIN_H_ */
