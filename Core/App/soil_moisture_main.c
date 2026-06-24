#include "soil_moisture_main.h"

#include "main.h"

#include <string.h>

#define SOIL_ADC_SAMPLE_COUNT  16U
#define SOIL_ADC_TIMEOUT_MS    20U

extern ADC_HandleTypeDef hadc1;

static bool s_adc_calibrated = false;

static uint8_t soil_moisture_map_percent(uint16_t raw,
                                         uint16_t dry_raw,
                                         uint16_t wet_raw,
                                         bool *valid)
{
    uint32_t span;
    uint32_t position;

    if (valid != NULL)
    {
        *valid = false;
    }

    if (dry_raw == wet_raw)
    {
        return SOIL_MOISTURE_INVALID;
    }

    if (dry_raw > wet_raw)
    {
        if (raw >= dry_raw)
        {
            if (valid != NULL)
            {
                *valid = true;
            }
            return 0U;
        }
        if (raw <= wet_raw)
        {
            if (valid != NULL)
            {
                *valid = true;
            }
            return 100U;
        }

        span = (uint32_t)dry_raw - (uint32_t)wet_raw;
        position = (uint32_t)dry_raw - (uint32_t)raw;
    }
    else
    {
        if (raw <= dry_raw)
        {
            if (valid != NULL)
            {
                *valid = true;
            }
            return 0U;
        }
        if (raw >= wet_raw)
        {
            if (valid != NULL)
            {
                *valid = true;
            }
            return 100U;
        }

        span = (uint32_t)wet_raw - (uint32_t)dry_raw;
        position = (uint32_t)raw - (uint32_t)dry_raw;
    }

    if (valid != NULL)
    {
        *valid = true;
    }

    return (uint8_t)(((position * 100U) + (span / 2U)) / span);
}

static bool soil_moisture_calibrate_adc(void)
{
    if (s_adc_calibrated)
    {
        return true;
    }

    if (HAL_ADCEx_Calibration_Start(&hadc1,
                                    ADC_CALIB_OFFSET_LINEARITY,
                                    ADC_SINGLE_ENDED) != HAL_OK)
    {
        return false;
    }

    s_adc_calibrated = true;
    return true;
}

bool soil_moisture_read(soil_moisture_data_t *out_data)
{
    uint32_t sum[SOIL_MOISTURE_SENSOR_COUNT] = {0U, 0U};
    uint32_t sample_index;

    if (out_data == NULL)
    {
        return false;
    }

    memset(out_data, 0, sizeof(*out_data));
    out_data->percent[0] = SOIL_MOISTURE_INVALID;
    out_data->percent[1] = SOIL_MOISTURE_INVALID;

    if (!soil_moisture_calibrate_adc())
    {
        return false;
    }

    for (sample_index = 0U; sample_index < SOIL_ADC_SAMPLE_COUNT; sample_index++)
    {
        if (HAL_ADC_Start(&hadc1) != HAL_OK)
        {
            (void)HAL_ADC_Stop(&hadc1);
            return false;
        }

        if (HAL_ADC_PollForConversion(&hadc1, SOIL_ADC_TIMEOUT_MS) != HAL_OK)
        {
            (void)HAL_ADC_Stop(&hadc1);
            return false;
        }
        sum[0] += HAL_ADC_GetValue(&hadc1);

        if (HAL_ADC_PollForConversion(&hadc1, SOIL_ADC_TIMEOUT_MS) != HAL_OK)
        {
            (void)HAL_ADC_Stop(&hadc1);
            return false;
        }
        sum[1] += HAL_ADC_GetValue(&hadc1);

        if (HAL_ADC_Stop(&hadc1) != HAL_OK)
        {
            return false;
        }
    }

    out_data->raw[0] = (uint16_t)((sum[0] + (SOIL_ADC_SAMPLE_COUNT / 2U)) /
                                  SOIL_ADC_SAMPLE_COUNT);
    out_data->raw[1] = (uint16_t)((sum[1] + (SOIL_ADC_SAMPLE_COUNT / 2U)) /
                                  SOIL_ADC_SAMPLE_COUNT);

    out_data->percent[0] = soil_moisture_map_percent(out_data->raw[0],
                                                     SOIL_MOISTURE_1_DRY_RAW,
                                                     SOIL_MOISTURE_1_WET_RAW,
                                                     &out_data->valid[0]);
    out_data->percent[1] = soil_moisture_map_percent(out_data->raw[1],
                                                     SOIL_MOISTURE_2_DRY_RAW,
                                                     SOIL_MOISTURE_2_WET_RAW,
                                                     &out_data->valid[1]);

    return out_data->valid[0] && out_data->valid[1];
}
