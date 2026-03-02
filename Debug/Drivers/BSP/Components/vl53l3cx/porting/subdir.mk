################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Drivers/BSP/Components/vl53l3cx/porting/vl53lx_platform.c \
../Drivers/BSP/Components/vl53l3cx/porting/vl53lx_platform_ipp.c \
../Drivers/BSP/Components/vl53l3cx/porting/vl53lx_platform_log.c 

OBJS += \
./Drivers/BSP/Components/vl53l3cx/porting/vl53lx_platform.o \
./Drivers/BSP/Components/vl53l3cx/porting/vl53lx_platform_ipp.o \
./Drivers/BSP/Components/vl53l3cx/porting/vl53lx_platform_log.o 

C_DEPS += \
./Drivers/BSP/Components/vl53l3cx/porting/vl53lx_platform.d \
./Drivers/BSP/Components/vl53l3cx/porting/vl53lx_platform_ipp.d \
./Drivers/BSP/Components/vl53l3cx/porting/vl53lx_platform_log.d 


# Each subdirectory must supply rules for building sources it contributes
Drivers/BSP/Components/vl53l3cx/porting/%.o Drivers/BSP/Components/vl53l3cx/porting/%.su Drivers/BSP/Components/vl53l3cx/porting/%.cyclo: ../Drivers/BSP/Components/vl53l3cx/porting/%.c Drivers/BSP/Components/vl53l3cx/porting/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m33 -std=gnu11 -g3 -DDEBUG -DUSE_NUCLEO_64 -DUSE_HAL_DRIVER -DSTM32U545xx -c -I../Core/Inc -I../Drivers/STM32U5xx_HAL_Driver/Inc -I../Drivers/STM32U5xx_HAL_Driver/Inc/Legacy -I../Drivers/BSP/STM32U5xx_Nucleo -I../Drivers/CMSIS/Device/ST/STM32U5xx/Include -I../Drivers/CMSIS/Include -I../Drivers/BSP/Components/vl53l3cx/modules -I../Drivers/BSP/Components/vl53l3cx/porting -I../Drivers/BSP/Components/vl53l3cx -I../Middlewares/Third_Party/FreeRTOS/Source/include/ -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM33_NTZ/non_secure/ -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2/ -I../Middlewares/Third_Party/CMSIS/RTOS2/Include/ -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Drivers-2f-BSP-2f-Components-2f-vl53l3cx-2f-porting

clean-Drivers-2f-BSP-2f-Components-2f-vl53l3cx-2f-porting:
	-$(RM) ./Drivers/BSP/Components/vl53l3cx/porting/vl53lx_platform.cyclo ./Drivers/BSP/Components/vl53l3cx/porting/vl53lx_platform.d ./Drivers/BSP/Components/vl53l3cx/porting/vl53lx_platform.o ./Drivers/BSP/Components/vl53l3cx/porting/vl53lx_platform.su ./Drivers/BSP/Components/vl53l3cx/porting/vl53lx_platform_ipp.cyclo ./Drivers/BSP/Components/vl53l3cx/porting/vl53lx_platform_ipp.d ./Drivers/BSP/Components/vl53l3cx/porting/vl53lx_platform_ipp.o ./Drivers/BSP/Components/vl53l3cx/porting/vl53lx_platform_ipp.su ./Drivers/BSP/Components/vl53l3cx/porting/vl53lx_platform_log.cyclo ./Drivers/BSP/Components/vl53l3cx/porting/vl53lx_platform_log.d ./Drivers/BSP/Components/vl53l3cx/porting/vl53lx_platform_log.o ./Drivers/BSP/Components/vl53l3cx/porting/vl53lx_platform_log.su

.PHONY: clean-Drivers-2f-BSP-2f-Components-2f-vl53l3cx-2f-porting

