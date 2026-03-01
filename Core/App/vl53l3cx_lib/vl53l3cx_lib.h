#ifndef APP_VL53L3CX_LIB_VL53L3CX_LIB_H_
#define APP_VL53L3CX_LIB_VL53L3CX_LIB_H_

#include <stdbool.h>
#include <stdint.h>

/**
 * @fn bool tof_init(void)
 * @brief
 * 		Initialize tof VL53L3CX on board
 * @return
 * 		- true: works
 * 		- flase: does not work
 */
bool tof_init(void);

/**
 * @fn int32_t tof_get_distance(void)
 * @brief get distance in mm
 * @note Funkcja jest blokująca: czeka na świeżą próbkę z czujnika.
 * Czas blokowania zależy od TimingBudget (aktualnie ~30 ms + narzut magistrali).
 * @return
 * 		uint32_t distance in mm
 */
int32_t tof_get_distance(void);


void VL53L3CX_TestOnce(void);

#endif /* APP_VL53L3CX_LIB_VL53L3CX_LIB_H_ */
