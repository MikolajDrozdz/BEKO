################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/App/bmp280_lib/bmp280.c \
../Core/App/bmp280_lib/bmp280_api.c 

OBJS += \
./Core/App/bmp280_lib/bmp280.o \
./Core/App/bmp280_lib/bmp280_api.o 

C_DEPS += \
./Core/App/bmp280_lib/bmp280.d \
./Core/App/bmp280_lib/bmp280_api.d 


# Each subdirectory must supply rules for building sources it contributes
Core/App/bmp280_lib/%.o Core/App/bmp280_lib/%.su Core/App/bmp280_lib/%.cyclo: ../Core/App/bmp280_lib/%.c Core/App/bmp280_lib/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m33 -std=gnu11 -g3 -DDEBUG -DUSE_NUCLEO_64 -DUSE_HAL_DRIVER -DSTM32U545xx -c -I../Core/Inc -I../Drivers/STM32U5xx_HAL_Driver/Inc -I../Drivers/STM32U5xx_HAL_Driver/Inc/Legacy -I../Drivers/BSP/STM32U5xx_Nucleo -I../Drivers/CMSIS/Device/ST/STM32U5xx/Include -I../Drivers/CMSIS/Include -I../Drivers/BSP/Components/vl53l3cx/modules -I../Drivers/BSP/Components/vl53l3cx/porting -I../Drivers/BSP/Components/vl53l3cx -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-App-2f-bmp280_lib

clean-Core-2f-App-2f-bmp280_lib:
	-$(RM) ./Core/App/bmp280_lib/bmp280.cyclo ./Core/App/bmp280_lib/bmp280.d ./Core/App/bmp280_lib/bmp280.o ./Core/App/bmp280_lib/bmp280.su ./Core/App/bmp280_lib/bmp280_api.cyclo ./Core/App/bmp280_lib/bmp280_api.d ./Core/App/bmp280_lib/bmp280_api.o ./Core/App/bmp280_lib/bmp280_api.su

.PHONY: clean-Core-2f-App-2f-bmp280_lib

