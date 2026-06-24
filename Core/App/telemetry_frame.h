#ifndef APP_TELEMETRY_FRAME_H_
#define APP_TELEMETRY_FRAME_H_

#include <stdbool.h>
#include <stdint.h>

#define TELEMETRY_FRAME_SIZE              11U
#define TELEMETRY_SOIL_INVALID            0xFFU
#define TELEMETRY_TEMPERATURE_INVALID     ((int16_t)INT16_MIN)
#define TELEMETRY_PRESSURE_INVALID        0xFFFFU

typedef struct
{
    uint16_t network_id;
    uint16_t device_id;
    uint8_t soil_percent_1;
    uint8_t soil_percent_2;
    int16_t temperature_centi_c;
    uint16_t pressure_deci_hpa;
} telemetry_sample_t;

uint8_t telemetry_crc8_atm(const uint8_t *data, uint32_t length);
bool telemetry_frame_encode(const telemetry_sample_t *sample,
                            uint8_t frame[TELEMETRY_FRAME_SIZE]);

#endif /* APP_TELEMETRY_FRAME_H_ */
