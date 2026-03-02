#ifndef APP_BMP280_MAIN_H_
#define APP_BMP280_MAIN_H_

#include "bmp280_lib/bmp280_api.h"

#include <stdbool.h>

void bmp280_main_create_task(void);
bool bmp280_main_get_last(bmp280_api_data_t *out_data);

#endif /* APP_BMP280_MAIN_H_ */

