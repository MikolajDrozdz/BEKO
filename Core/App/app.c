/*
 * app.c
 *
 *  Created on: Feb 28, 2026
 *      Author: mikol
 */

#include "app.h"
#include "lcd_library/lcd.h"
#include "main.h"

void
app_init( void )
{

	lcd_clear();
	lcd_backlight(1);
	lcd_set_cursor(0,0);

	lcd_animation_hello_beko();


	return;
}

void
app_main( void )
{



	return;
}
