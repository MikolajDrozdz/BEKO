/**
 * @file service.h
 * @brief Optional UART service console task entry point.
 */

#ifndef APP_SERVICE_H_
#define APP_SERVICE_H_

#include "pager_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create the UART service task when `SERVICE_UART` is enabled.
 */
void service_uart_create_task(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_SERVICE_H_ */
