/**
 * @file tof_main.h
 * @brief RTOS task facade for the VL53L3CX time-of-flight sensor.
 */

#ifndef APP_TOF_MAIN_H_
#define APP_TOF_MAIN_H_

#include <stdint.h>

/**
 * @brief Create the ToF periodic sampling task.
 */
void tof_main_create_task(void);

/**
 * @brief Return the last cached distance measurement.
 * @return Distance in millimeters, or a negative value when unavailable.
 */
int32_t tof_main_get_last_distance(void);

#endif /* APP_TOF_MAIN_H_ */
