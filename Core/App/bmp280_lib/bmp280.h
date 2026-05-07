/**
 * @file bmp280.h
 * @brief Low-level BMP280/BME280 driver API.
 */
/**
 * Ciastkolog.pl (https://github.com/ciastkolog)
 * 
*/
/**
 * The MIT License (MIT)
 * Copyright (c) 2016 sheinz (https://github.com/sheinz)
 */
#ifndef __BMP280_H__
#define __BMP280_H__

#include "stm32u5xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * BMP280 or BME280 address is 0x77 if SDO pin is high, and is 0x76 if
 * SDO pin is low.
 */

/** @brief I2C address when SDO pin is low. */
#define BMP280_I2C_ADDRESS_0  0x76
/** @brief I2C address when SDO pin is high. */
#define BMP280_I2C_ADDRESS_1  0x77

/** @brief Expected BMP280 chip identifier. */
#define BMP280_CHIP_ID  0x58
/** @brief Expected BME280 chip identifier. */
#define BME280_CHIP_ID  0x60

/**
 * Mode of BMP280 module operation.
 * Forced - Measurement is initiated by user.
 * Normal - Continues measurement.
 */
typedef enum {
    BMP280_MODE_SLEEP = 0,
    BMP280_MODE_FORCED = 1,
    BMP280_MODE_NORMAL = 3
} BMP280_Mode;

typedef enum {
    BMP280_FILTER_OFF = 0, /**< IIR filter disabled. */
    BMP280_FILTER_2 = 1, /**< IIR filter coefficient 2. */
    BMP280_FILTER_4 = 2, /**< IIR filter coefficient 4. */
    BMP280_FILTER_8 = 3, /**< IIR filter coefficient 8. */
    BMP280_FILTER_16 = 4 /**< IIR filter coefficient 16. */
} BMP280_Filter;

/**
 * Pressure oversampling settings
 */
typedef enum {
    BMP280_SKIPPED = 0,          /* no measurement  */
    BMP280_ULTRA_LOW_POWER = 1,  /* oversampling x1 */
    BMP280_LOW_POWER = 2,        /* oversampling x2 */
    BMP280_STANDARD = 3,         /* oversampling x4 */
    BMP280_HIGH_RES = 4,         /* oversampling x8 */
    BMP280_ULTRA_HIGH_RES = 5    /* oversampling x16 */
} BMP280_Oversampling;

/**
 * Stand by time between measurements in normal mode
 */
typedef enum {
    BMP280_STANDBY_05 = 0,      /* stand by time 0.5ms */
    BMP280_STANDBY_62 = 1,      /* stand by time 62.5ms */
    BMP280_STANDBY_125 = 2,     /* stand by time 125ms */
    BMP280_STANDBY_250 = 3,     /* stand by time 250ms */
    BMP280_STANDBY_500 = 4,     /* stand by time 500ms */
    BMP280_STANDBY_1000 = 5,    /* stand by time 1s */
    BMP280_STANDBY_2000 = 6,    /* stand by time 2s BMP280, 10ms BME280 */
    BMP280_STANDBY_4000 = 7,    /* stand by time 4s BMP280, 20ms BME280 */
} BMP280_StandbyTime;

/**
 * Configuration parameters for BMP280 module.
 * Use function bmp280_init_default_params to use default configuration.
 */
typedef struct {
    BMP280_Mode mode; /**< Sensor operating mode. */
    BMP280_Filter filter; /**< IIR filter setting. */
    BMP280_Oversampling oversampling_pressure; /**< Pressure oversampling setting. */
    BMP280_Oversampling oversampling_temperature; /**< Temperature oversampling setting. */
    BMP280_Oversampling oversampling_humidity; /**< Humidity oversampling setting for BME280. */
    BMP280_StandbyTime standby; /**< Standby period in normal mode. */
} bmp280_params_t;


/**
 * @brief Driver handle with calibration constants and active configuration.
 */
typedef struct {
    uint16_t dig_T1; /**< Temperature calibration coefficient T1. */
    int16_t  dig_T2; /**< Temperature calibration coefficient T2. */
    int16_t  dig_T3; /**< Temperature calibration coefficient T3. */
    uint16_t dig_P1; /**< Pressure calibration coefficient P1. */
    int16_t  dig_P2; /**< Pressure calibration coefficient P2. */
    int16_t  dig_P3; /**< Pressure calibration coefficient P3. */
    int16_t  dig_P4; /**< Pressure calibration coefficient P4. */
    int16_t  dig_P5; /**< Pressure calibration coefficient P5. */
    int16_t  dig_P6; /**< Pressure calibration coefficient P6. */
    int16_t  dig_P7; /**< Pressure calibration coefficient P7. */
    int16_t  dig_P8; /**< Pressure calibration coefficient P8. */
    int16_t  dig_P9; /**< Pressure calibration coefficient P9. */

    /* Humidity compensation for BME280 */
    uint8_t  dig_H1; /**< Humidity calibration coefficient H1. */
    int16_t  dig_H2; /**< Humidity calibration coefficient H2. */
    uint8_t  dig_H3; /**< Humidity calibration coefficient H3. */
    int16_t  dig_H4; /**< Humidity calibration coefficient H4. */
    int16_t  dig_H5; /**< Humidity calibration coefficient H5. */
    int8_t   dig_H6; /**< Humidity calibration coefficient H6. */

    uint16_t addr; /**< 7-bit I2C device address. */

    I2C_HandleTypeDef* i2c; /**< I2C peripheral handle. */

    bmp280_params_t params; /**< Active sensor configuration. */

    uint8_t  id; /**< Chip ID read from the sensor. */

} BMP280_HandleTypedef;

/**
 * @brief Initialize default BMP280 parameters.
 *
 * Default configuration:
 * - mode: NORMAL
 * - filter: OFF
 * - oversampling: x4
 * - standby time: 250ms
 *
 * @param params [out] Parameter structure to initialize.
 */
void bmp280_init_default_params(bmp280_params_t *params);

/**
 * @brief Initialize BMP280/BME280 module.
 *
 * Probes for the device, soft resets the device,
 * reads the calibration constants, and configures the device using the supplied
 * parameters.
 *
 * The I2C address is assumed to have been initialized in the dev, and
 * may be either BMP280_I2C_ADDRESS_0 or BMP280_I2C_ADDRESS_1. If the I2C
 * address is unknown then try initializing each in turn.
 *
 * This may be called again to soft reset the device and initialize it again.
 *
 * @param dev Driver handle with I2C handle and address already set.
 * @param params Sensor configuration parameters.
 * @return `true` on successful initialization.
 */
bool bmp280_init(BMP280_HandleTypedef *dev, bmp280_params_t *params);

/**
 * @brief Start one measurement in forced mode.
 *
 * The module remains in forced mode after this call.
 * Do not call this method in normal mode.
 *
 * @param dev Driver handle.
 * @return `true` when the measurement trigger was written.
 */
bool bmp280_force_measurement(BMP280_HandleTypedef *dev);

/**
 * @brief Check whether BMP280 is busy measuring temperature or pressure.
 * @param dev Driver handle.
 * @return `true` when the sensor is currently measuring.
 */
bool bmp280_is_measuring(BMP280_HandleTypedef *dev);

/**
 * @brief Read compensated fixed-point temperature, pressure and optional humidity.
 *
 *  Temperature in degrees Celsius times 100.
 *
 *  Pressure in Pascals in fixed point 24 bit integer 8 bit fraction format.
 *
 *  Humidity is optional and only read for the BME280, in percent relative
 *  humidity as a fixed point 22 bit integer and 10 bit fraction format.
 *
 * @param dev Driver handle.
 * @param temperature [out] Temperature in centi-degrees Celsius.
 * @param pressure [out] Pressure in Pa using 24.8 fixed-point format.
 * @param humidity [out] Optional humidity in BME280 fixed-point format.
 * @return `true` when data was read and compensated.
 */
bool bmp280_read_fixed(BMP280_HandleTypedef *dev, int32_t *temperature,
                       uint32_t *pressure, uint32_t *humidity);

/**
 * @brief Read compensated floating-point temperature, pressure and optional humidity.
 *
 *  Temperature in degrees Celsius.
 *  Pressure in Pascals.
 *  Humidity is optional and only read for the BME280, in percent relative
 *  humidity.
 *
 * @param dev Driver handle.
 * @param temperature [out] Temperature in degrees Celsius.
 * @param pressure [out] Pressure in Pascals.
 * @param humidity [out] Optional relative humidity in percent.
 * @return `true` when data was read and compensated.
 */
bool bmp280_read_float(BMP280_HandleTypedef *dev, float *temperature,
                       float *pressure, float *humidity);


#endif  // __BMP280_H__
