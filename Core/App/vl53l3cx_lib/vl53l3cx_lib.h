/**
 * @file vl53l3cx_lib.h
 * @brief Application wrapper for the VL53L3CX time-of-flight sensor.
 */

#ifndef APP_VL53L3CX_LIB_VL53L3CX_LIB_H_
#define APP_VL53L3CX_LIB_VL53L3CX_LIB_H_

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Initialize the onboard VL53L3CX sensor.
 * @return `true` when the sensor was detected and configured.
 */
bool tof_init(void);

/**
 * @brief Read one distance sample in millimeters.
 *
 * @note Funkcja jest blokujaca: czeka na swieza probke z czujnika.
 * Czas blokowania zalezy od TimingBudget (aktualnie ~30 ms + narzut magistrali).
 *
 * @return Distance in millimeters, or a negative value on failure.
 */
int32_t tof_get_distance(void);

/**
 * @brief Diagnostic helper that reads and prints one VL53L3CX sample.
 */
void VL53L3CX_TestOnce(void);

#endif /* APP_VL53L3CX_LIB_VL53L3CX_LIB_H_ */
