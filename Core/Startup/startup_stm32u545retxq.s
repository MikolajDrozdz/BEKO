/**
  ******************************************************************************
  * @file      startup_stm32u545retxq.s
  * @brief     STM32U545RETxQ GCC startup file.
  ******************************************************************************
  */

  .syntax unified
  .cpu cortex-m33
  .fpu fpv5-sp-d16
  .thumb

  .global g_pfnVectors
  .global Default_Handler
  .global Reset_Handler

  .extern main
  .extern SystemInit
  .extern __libc_init_array
  .extern _estack
  .extern _sidata
  .extern _sdata
  .extern _edata
  .extern _sbss
  .extern _ebss

  .section .text.Reset_Handler
  .weak Reset_Handler
  .type Reset_Handler, %function
Reset_Handler:
  ldr r0, =_sdata
  ldr r1, =_edata
  ldr r2, =_sidata
  movs r3, #0
  b CopyDataLoop

CopyData:
  ldr r4, [r2, r3]
  str r4, [r0, r3]
  adds r3, r3, #4

CopyDataLoop:
  adds r4, r0, r3
  cmp r4, r1
  bcc CopyData

  ldr r2, =_sbss
  ldr r4, =_ebss
  movs r3, #0
  b ZeroBssLoop

ZeroBss:
  str r3, [r2]
  adds r2, r2, #4

ZeroBssLoop:
  cmp r2, r4
  bcc ZeroBss

  bl SystemInit
  bl __libc_init_array
  bl main

LoopForever:
  b LoopForever
  .size Reset_Handler, .-Reset_Handler

  .section .text.Default_Handler
  .type Default_Handler, %function
Default_Handler:
InfiniteLoop:
  b InfiniteLoop
  .size Default_Handler, .-Default_Handler

  .macro weak_alias name
    .weak \name
    .thumb_set \name, Default_Handler
  .endm

  weak_alias NMI_Handler
  weak_alias HardFault_Handler
  weak_alias MemManage_Handler
  weak_alias BusFault_Handler
  weak_alias UsageFault_Handler
  weak_alias SecureFault_Handler
  weak_alias SVC_Handler
  weak_alias DebugMon_Handler
  weak_alias PendSV_Handler
  weak_alias SysTick_Handler
  weak_alias WWDG_IRQHandler
  weak_alias PVD_PVM_IRQHandler
  weak_alias RTC_IRQHandler
  weak_alias RTC_S_IRQHandler
  weak_alias TAMP_IRQHandler
  weak_alias RAMCFG_IRQHandler
  weak_alias FLASH_IRQHandler
  weak_alias FLASH_S_IRQHandler
  weak_alias GTZC_IRQHandler
  weak_alias RCC_IRQHandler
  weak_alias RCC_S_IRQHandler
  weak_alias EXTI0_IRQHandler
  weak_alias EXTI1_IRQHandler
  weak_alias EXTI2_IRQHandler
  weak_alias EXTI3_IRQHandler
  weak_alias EXTI4_IRQHandler
  weak_alias EXTI5_IRQHandler
  weak_alias EXTI6_IRQHandler
  weak_alias EXTI7_IRQHandler
  weak_alias EXTI8_IRQHandler
  weak_alias EXTI9_IRQHandler
  weak_alias EXTI10_IRQHandler
  weak_alias EXTI11_IRQHandler
  weak_alias EXTI12_IRQHandler
  weak_alias EXTI13_IRQHandler
  weak_alias EXTI14_IRQHandler
  weak_alias EXTI15_IRQHandler
  weak_alias IWDG_IRQHandler
  weak_alias SAES_IRQHandler
  weak_alias GPDMA1_Channel0_IRQHandler
  weak_alias GPDMA1_Channel1_IRQHandler
  weak_alias GPDMA1_Channel2_IRQHandler
  weak_alias GPDMA1_Channel3_IRQHandler
  weak_alias GPDMA1_Channel4_IRQHandler
  weak_alias GPDMA1_Channel5_IRQHandler
  weak_alias GPDMA1_Channel6_IRQHandler
  weak_alias GPDMA1_Channel7_IRQHandler
  weak_alias ADC1_IRQHandler
  weak_alias DAC1_IRQHandler
  weak_alias FDCAN1_IT0_IRQHandler
  weak_alias FDCAN1_IT1_IRQHandler
  weak_alias TIM1_BRK_IRQHandler
  weak_alias TIM1_UP_IRQHandler
  weak_alias TIM1_TRG_COM_IRQHandler
  weak_alias TIM1_CC_IRQHandler
  weak_alias TIM2_IRQHandler
  weak_alias TIM3_IRQHandler
  weak_alias TIM4_IRQHandler
  weak_alias TIM5_IRQHandler
  weak_alias TIM6_IRQHandler
  weak_alias TIM7_IRQHandler
  weak_alias TIM8_BRK_IRQHandler
  weak_alias TIM8_UP_IRQHandler
  weak_alias TIM8_TRG_COM_IRQHandler
  weak_alias TIM8_CC_IRQHandler
  weak_alias I2C1_EV_IRQHandler
  weak_alias I2C1_ER_IRQHandler
  weak_alias I2C2_EV_IRQHandler
  weak_alias I2C2_ER_IRQHandler
  weak_alias SPI1_IRQHandler
  weak_alias SPI2_IRQHandler
  weak_alias USART1_IRQHandler
  weak_alias USART3_IRQHandler
  weak_alias UART4_IRQHandler
  weak_alias UART5_IRQHandler
  weak_alias LPUART1_IRQHandler
  weak_alias LPTIM1_IRQHandler
  weak_alias LPTIM2_IRQHandler
  weak_alias TIM15_IRQHandler
  weak_alias TIM16_IRQHandler
  weak_alias TIM17_IRQHandler
  weak_alias COMP_IRQHandler
  weak_alias USB_IRQHandler
  weak_alias CRS_IRQHandler
  weak_alias OCTOSPI1_IRQHandler
  weak_alias PWR_S3WU_IRQHandler
  weak_alias SDMMC1_IRQHandler
  weak_alias GPDMA1_Channel8_IRQHandler
  weak_alias GPDMA1_Channel9_IRQHandler
  weak_alias GPDMA1_Channel10_IRQHandler
  weak_alias GPDMA1_Channel11_IRQHandler
  weak_alias GPDMA1_Channel12_IRQHandler
  weak_alias GPDMA1_Channel13_IRQHandler
  weak_alias GPDMA1_Channel14_IRQHandler
  weak_alias GPDMA1_Channel15_IRQHandler
  weak_alias I2C3_EV_IRQHandler
  weak_alias I2C3_ER_IRQHandler
  weak_alias SAI1_IRQHandler
  weak_alias TSC_IRQHandler
  weak_alias AES_IRQHandler
  weak_alias RNG_IRQHandler
  weak_alias FPU_IRQHandler
  weak_alias HASH_IRQHandler
  weak_alias PKA_IRQHandler
  weak_alias LPTIM3_IRQHandler
  weak_alias SPI3_IRQHandler
  weak_alias I2C4_ER_IRQHandler
  weak_alias I2C4_EV_IRQHandler
  weak_alias MDF1_FLT0_IRQHandler
  weak_alias MDF1_FLT1_IRQHandler
  weak_alias ICACHE_IRQHandler
  weak_alias OTFDEC1_IRQHandler
  weak_alias LPTIM4_IRQHandler
  weak_alias DCACHE1_IRQHandler
  weak_alias ADF1_IRQHandler
  weak_alias ADC4_IRQHandler
  weak_alias LPDMA1_Channel0_IRQHandler
  weak_alias LPDMA1_Channel1_IRQHandler
  weak_alias LPDMA1_Channel2_IRQHandler
  weak_alias LPDMA1_Channel3_IRQHandler
  weak_alias DCMI_PSSI_IRQHandler
  weak_alias CORDIC_IRQHandler
  weak_alias FMAC_IRQHandler
  weak_alias LSECSSD_IRQHandler

  .section .isr_vector,"a",%progbits
  .type g_pfnVectors, %object
g_pfnVectors:
  .word _estack
  .word Reset_Handler
  .word NMI_Handler
  .word HardFault_Handler
  .word MemManage_Handler
  .word BusFault_Handler
  .word UsageFault_Handler
  .word SecureFault_Handler
  .word 0
  .word 0
  .word 0
  .word SVC_Handler
  .word DebugMon_Handler
  .word 0
  .word PendSV_Handler
  .word SysTick_Handler
  .word WWDG_IRQHandler
  .word PVD_PVM_IRQHandler
  .word RTC_IRQHandler
  .word RTC_S_IRQHandler
  .word TAMP_IRQHandler
  .word RAMCFG_IRQHandler
  .word FLASH_IRQHandler
  .word FLASH_S_IRQHandler
  .word GTZC_IRQHandler
  .word RCC_IRQHandler
  .word RCC_S_IRQHandler
  .word EXTI0_IRQHandler
  .word EXTI1_IRQHandler
  .word EXTI2_IRQHandler
  .word EXTI3_IRQHandler
  .word EXTI4_IRQHandler
  .word EXTI5_IRQHandler
  .word EXTI6_IRQHandler
  .word EXTI7_IRQHandler
  .word EXTI8_IRQHandler
  .word EXTI9_IRQHandler
  .word EXTI10_IRQHandler
  .word EXTI11_IRQHandler
  .word EXTI12_IRQHandler
  .word EXTI13_IRQHandler
  .word EXTI14_IRQHandler
  .word EXTI15_IRQHandler
  .word IWDG_IRQHandler
  .word SAES_IRQHandler
  .word GPDMA1_Channel0_IRQHandler
  .word GPDMA1_Channel1_IRQHandler
  .word GPDMA1_Channel2_IRQHandler
  .word GPDMA1_Channel3_IRQHandler
  .word GPDMA1_Channel4_IRQHandler
  .word GPDMA1_Channel5_IRQHandler
  .word GPDMA1_Channel6_IRQHandler
  .word GPDMA1_Channel7_IRQHandler
  .word ADC1_IRQHandler
  .word DAC1_IRQHandler
  .word FDCAN1_IT0_IRQHandler
  .word FDCAN1_IT1_IRQHandler
  .word TIM1_BRK_IRQHandler
  .word TIM1_UP_IRQHandler
  .word TIM1_TRG_COM_IRQHandler
  .word TIM1_CC_IRQHandler
  .word TIM2_IRQHandler
  .word TIM3_IRQHandler
  .word TIM4_IRQHandler
  .word TIM5_IRQHandler
  .word TIM6_IRQHandler
  .word TIM7_IRQHandler
  .word TIM8_BRK_IRQHandler
  .word TIM8_UP_IRQHandler
  .word TIM8_TRG_COM_IRQHandler
  .word TIM8_CC_IRQHandler
  .word I2C1_EV_IRQHandler
  .word I2C1_ER_IRQHandler
  .word I2C2_EV_IRQHandler
  .word I2C2_ER_IRQHandler
  .word SPI1_IRQHandler
  .word SPI2_IRQHandler
  .word USART1_IRQHandler
  .word 0
  .word USART3_IRQHandler
  .word UART4_IRQHandler
  .word UART5_IRQHandler
  .word LPUART1_IRQHandler
  .word LPTIM1_IRQHandler
  .word LPTIM2_IRQHandler
  .word TIM15_IRQHandler
  .word TIM16_IRQHandler
  .word TIM17_IRQHandler
  .word COMP_IRQHandler
  .word USB_IRQHandler
  .word CRS_IRQHandler
  .word 0
  .word OCTOSPI1_IRQHandler
  .word PWR_S3WU_IRQHandler
  .word SDMMC1_IRQHandler
  .word 0
  .word GPDMA1_Channel8_IRQHandler
  .word GPDMA1_Channel9_IRQHandler
  .word GPDMA1_Channel10_IRQHandler
  .word GPDMA1_Channel11_IRQHandler
  .word GPDMA1_Channel12_IRQHandler
  .word GPDMA1_Channel13_IRQHandler
  .word GPDMA1_Channel14_IRQHandler
  .word GPDMA1_Channel15_IRQHandler
  .word I2C3_EV_IRQHandler
  .word I2C3_ER_IRQHandler
  .word SAI1_IRQHandler
  .word 0
  .word TSC_IRQHandler
  .word AES_IRQHandler
  .word RNG_IRQHandler
  .word FPU_IRQHandler
  .word HASH_IRQHandler
  .word PKA_IRQHandler
  .word LPTIM3_IRQHandler
  .word SPI3_IRQHandler
  .word I2C4_ER_IRQHandler
  .word I2C4_EV_IRQHandler
  .word MDF1_FLT0_IRQHandler
  .word MDF1_FLT1_IRQHandler
  .word 0
  .word 0
  .word 0
  .word ICACHE_IRQHandler
  .word OTFDEC1_IRQHandler
  .word 0
  .word LPTIM4_IRQHandler
  .word DCACHE1_IRQHandler
  .word ADF1_IRQHandler
  .word ADC4_IRQHandler
  .word LPDMA1_Channel0_IRQHandler
  .word LPDMA1_Channel1_IRQHandler
  .word LPDMA1_Channel2_IRQHandler
  .word LPDMA1_Channel3_IRQHandler
  .word 0
  .word DCMI_PSSI_IRQHandler
  .word 0
  .word 0
  .word 0
  .word CORDIC_IRQHandler
  .word FMAC_IRQHandler
  .word LSECSSD_IRQHandler
  .size g_pfnVectors, .-g_pfnVectors
