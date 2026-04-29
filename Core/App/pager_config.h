#ifndef APP_PAGER_CONFIG_H_
#define APP_PAGER_CONFIG_H_

/*
 * When set to 1, secure/control payloads may be printed verbatim on UART for
 * diagnostics. Keep this disabled in production because those logs can expose
 * pairing data and protected LAVIET_FRAME_V1 payloads.
 */
#define PAGER_CONFIG_UART_SENSITIVE_LOGS 0

/*
 * UART service console. Set to 0 to remove the whole UART command service from
 * the firmware build while leaving normal printf diagnostics intact.
 */
#ifndef SERVICE_UART
#define SERVICE_UART 1
#endif

/*
 * Current board variant has no BMP280/BME280 mounted. Keep the driver code in
 * tree for future variants, but do not start the RTOS task on this hardware.
 */
#define PAGER_CONFIG_BMP280_MOUNTED 0

#endif /* APP_PAGER_CONFIG_H_ */
