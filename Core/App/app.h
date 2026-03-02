/*
 * app.h
 *
 *  Created on: Feb 28, 2026
 *      Author: mikol
 */

#ifndef APP_APP_H_
#define APP_APP_H_

#include <stdbool.h>
#include <stdint.h>

void
app_init( void );

void
app_main( void );

void
app_freertos_init( void );

bool
app_i2c_lock( uint32_t timeout_ms );

void
app_i2c_unlock( void );

#endif /* APP_APP_H_ */
