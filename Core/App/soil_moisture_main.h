#ifndef APP_SOIL_MOISTURE_MAIN_H_
#define APP_SOIL_MOISTURE_MAIN_H_

#include <stdbool.h>
#include <stdint.h>

#define SOIL_MOISTURE_SENSOR_COUNT  2U
#define SOIL_MOISTURE_INVALID       0xFFU

/*
 * Initial calibration for a typical capacitive probe powered from 3.3 V.
 * Replace these values with measurements taken in dry and saturated soil.
 * Separate constants are provided because individual probes differ.
 */
#ifndef SOIL_MOISTURE_1_DRY_RAW
#define SOIL_MOISTURE_1_DRY_RAW  12400U
#endif

#ifndef SOIL_MOISTURE_1_WET_RAW
#define SOIL_MOISTURE_1_WET_RAW   6300U
#endif

#ifndef SOIL_MOISTURE_2_DRY_RAW
#define SOIL_MOISTURE_2_DRY_RAW  12400U
#endif

#ifndef SOIL_MOISTURE_2_WET_RAW
#define SOIL_MOISTURE_2_WET_RAW   6300U
#endif

typedef struct
{
    uint16_t raw[SOIL_MOISTURE_SENSOR_COUNT];
    uint8_t percent[SOIL_MOISTURE_SENSOR_COUNT];
    bool valid[SOIL_MOISTURE_SENSOR_COUNT];
} soil_moisture_data_t;

/* Performs ADC calibration lazily, averages both channels and maps to 0..100%. */
bool soil_moisture_read(soil_moisture_data_t *out_data);

#endif /* APP_SOIL_MOISTURE_MAIN_H_ */
