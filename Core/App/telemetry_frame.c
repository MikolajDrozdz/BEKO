#include "telemetry_frame.h"

#include <stddef.h>

uint8_t telemetry_crc8_atm(const uint8_t *data, uint32_t length)
{
    uint8_t crc = 0U;
    uint32_t byte_index;
    uint8_t bit_index;

    if ((data == NULL) && (length > 0U))
    {
        return 0U;
    }

    for (byte_index = 0U; byte_index < length; byte_index++)
    {
        crc ^= data[byte_index];
        for (bit_index = 0U; bit_index < 8U; bit_index++)
        {
            if ((crc & 0x80U) != 0U)
            {
                crc = (uint8_t)((uint8_t)(crc << 1U) ^ 0x07U);
            }
            else
            {
                crc = (uint8_t)(crc << 1U);
            }
        }
    }

    return crc;
}

bool telemetry_frame_encode(const telemetry_sample_t *sample,
                            uint8_t frame[TELEMETRY_FRAME_SIZE])
{
    uint16_t temperature_raw;

    if ((sample == NULL) || (frame == NULL))
    {
        return false;
    }

    temperature_raw = (uint16_t)sample->temperature_centi_c;

    frame[0] = (uint8_t)(sample->network_id >> 8U);
    frame[1] = (uint8_t)sample->network_id;
    frame[2] = (uint8_t)(sample->device_id >> 8U);
    frame[3] = (uint8_t)sample->device_id;
    frame[4] = sample->soil_percent_1;
    frame[5] = sample->soil_percent_2;
    frame[6] = (uint8_t)(temperature_raw >> 8U);
    frame[7] = (uint8_t)temperature_raw;
    frame[8] = (uint8_t)(sample->pressure_deci_hpa >> 8U);
    frame[9] = (uint8_t)sample->pressure_deci_hpa;
    frame[10] = telemetry_crc8_atm(frame, TELEMETRY_FRAME_SIZE - 1U);

    return true;
}
