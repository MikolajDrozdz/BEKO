/**
 * @file pager_config.h
 * @brief Application-level build and board configuration switches.
 */

#ifndef APP_PAGER_CONFIG_H_
#define APP_PAGER_CONFIG_H_

/**
 * @brief Enable verbose UART logs for sensitive secure/control payloads.
 *
 * Keep this disabled in production because these logs can expose pairing data
 * and protected LAVIET_FRAME_V1 payloads.
 */
#define PAGER_CONFIG_UART_SENSITIVE_LOGS 0

/**
 * @brief Build the UART service console task.
 *
 * Set to 0 to remove the command service while leaving normal printf
 * diagnostics intact.
 */
#ifndef SERVICE_UART
#define SERVICE_UART 1
#endif

/**
 * @brief Board-level presence flag for BMP280/BME280.
 *
 * Current board variant has no sensor mounted. Keep the driver code in tree for
 * future variants, but do not start the RTOS task on this hardware.
 */
#define PAGER_CONFIG_BMP280_MOUNTED 0

#endif /* APP_PAGER_CONFIG_H_ */
