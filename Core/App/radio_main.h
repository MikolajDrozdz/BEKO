#ifndef APP_RADIO_MAIN_H_
#define APP_RADIO_MAIN_H_

#include <stdbool.h>
#include <stdint.h>

void radio_main_create_task(void);
bool radio_main_send(const uint8_t *data, uint8_t length);

#endif /* APP_RADIO_MAIN_H_ */
