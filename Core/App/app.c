/*
 * app.c
 *
 *  Created on: Feb 28, 2026
 *      Author: mikol
 */

#include "app.h"
#include "lcd_library/lcd.h"
#include "bmp280_lib/bmp280_api.h"
#include "main.h"
#include <stdio.h>

extern I2C_HandleTypeDef hi2c1;

bmp280_api_data_t bmp;

void
app_init( void )
{
	printf("HELLO BEKO!\n\r");

	printf("LCD init\r\n");
	lcd_clear();
	lcd_backlight(1);
	lcd_set_cursor(0,0);
	lcd_animation_hello_beko();


	printf("Initializing BMP280 (if connected)");
	bool tmp = bmp280_api_init(&hi2c1, BMP280_I2C_ADDRESS_1);
	bmp280_api_measure_all(&bmp, 50);

	if(bmp.valid != true)
	{
		printf("BMP280 init fail!\r\n");
	}
	else if(tmp == true)
	{
		printf("BMP280 not connected\n\r");
	}
	else
	{
		printf("Measurements: temp:%.2f, pres:%.2f\r\n", bmp.temperature_c, bmp.pressure_hpa);
	}


	return;
}

void
app_main( void )
{

}
