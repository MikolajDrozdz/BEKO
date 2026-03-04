################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/App/app.c \
../Core/App/beko_net_proto.c \
../Core/App/bmp280_main.c \
../Core/App/button_main.c \
../Core/App/lcd_main.c \
../Core/App/led_array_main.c \
../Core/App/menu_main.c \
../Core/App/radio_main.c \
../Core/App/security_main.c \
../Core/App/tof_main.c 

OBJS += \
./Core/App/app.o \
./Core/App/beko_net_proto.o \
./Core/App/bmp280_main.o \
./Core/App/button_main.o \
./Core/App/lcd_main.o \
./Core/App/led_array_main.o \
./Core/App/menu_main.o \
./Core/App/radio_main.o \
./Core/App/security_main.o \
./Core/App/tof_main.o 

C_DEPS += \
./Core/App/app.d \
./Core/App/beko_net_proto.d \
./Core/App/bmp280_main.d \
./Core/App/button_main.d \
./Core/App/lcd_main.d \
./Core/App/led_array_main.d \
./Core/App/menu_main.d \
./Core/App/radio_main.d \
./Core/App/security_main.d \
./Core/App/tof_main.d 


# Each subdirectory must supply rules for building sources it contributes
Core/App/%.o Core/App/%.su Core/App/%.cyclo: ../Core/App/%.c Core/App/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m33 -std=gnu11 -g3 -DDEBUG -DUSE_NUCLEO_64 -DUSE_HAL_DRIVER -DSTM32U545xx -c -I../Core/Inc -I../Drivers/STM32U5xx_HAL_Driver/Inc -I../Drivers/STM32U5xx_HAL_Driver/Inc/Legacy -I../Drivers/BSP/STM32U5xx_Nucleo -I../Drivers/CMSIS/Device/ST/STM32U5xx/Include -I../Drivers/CMSIS/Include -I../Drivers/BSP/Components/vl53l3cx/modules -I../Drivers/BSP/Components/vl53l3cx/porting -I../Drivers/BSP/Components/vl53l3cx -I../Middlewares/Third_Party/FreeRTOS/Source/include/ -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM33_NTZ/non_secure/ -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2/ -I../Middlewares/Third_Party/CMSIS/RTOS2/Include/ -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-App

clean-Core-2f-App:
	-$(RM) ./Core/App/app.cyclo ./Core/App/app.d ./Core/App/app.o ./Core/App/app.su ./Core/App/beko_net_proto.cyclo ./Core/App/beko_net_proto.d ./Core/App/beko_net_proto.o ./Core/App/beko_net_proto.su ./Core/App/bmp280_main.cyclo ./Core/App/bmp280_main.d ./Core/App/bmp280_main.o ./Core/App/bmp280_main.su ./Core/App/button_main.cyclo ./Core/App/button_main.d ./Core/App/button_main.o ./Core/App/button_main.su ./Core/App/lcd_main.cyclo ./Core/App/lcd_main.d ./Core/App/lcd_main.o ./Core/App/lcd_main.su ./Core/App/led_array_main.cyclo ./Core/App/led_array_main.d ./Core/App/led_array_main.o ./Core/App/led_array_main.su ./Core/App/menu_main.cyclo ./Core/App/menu_main.d ./Core/App/menu_main.o ./Core/App/menu_main.su ./Core/App/radio_main.cyclo ./Core/App/radio_main.d ./Core/App/radio_main.o ./Core/App/radio_main.su ./Core/App/security_main.cyclo ./Core/App/security_main.d ./Core/App/security_main.o ./Core/App/security_main.su ./Core/App/tof_main.cyclo ./Core/App/tof_main.d ./Core/App/tof_main.o ./Core/App/tof_main.su

.PHONY: clean-Core-2f-App

