/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32u5xx_hal.h"

#include "stm32u5xx_nucleo.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define VL53L3CX_INT_Pin GPIO_PIN_2
#define VL53L3CX_INT_GPIO_Port GPIOC
#define VL53L3CX_xshout_Pin GPIO_PIN_3
#define VL53L3CX_xshout_GPIO_Port GPIOC
#define RFM_DIO1_EXIT_1_Pin GPIO_PIN_1
#define RFM_DIO1_EXIT_1_GPIO_Port GPIOB
#define RFM_DIO1_EXIT_1_EXTI_IRQn EXTI1_IRQn
#define RFM_DIO0_EXIT_2_Pin GPIO_PIN_2
#define RFM_DIO0_EXIT_2_GPIO_Port GPIOB
#define RFM_DIO0_EXIT_2_EXTI_IRQn EXTI2_IRQn
#define RFM_DIO4_EXIT_13_Pin GPIO_PIN_13
#define RFM_DIO4_EXIT_13_GPIO_Port GPIOB
#define RFM_DIO4_EXIT_13_EXTI_IRQn EXTI13_IRQn
#define RFM_DIO3_EXIT_14_Pin GPIO_PIN_14
#define RFM_DIO3_EXIT_14_GPIO_Port GPIOB
#define RFM_DIO3_EXIT_14_EXTI_IRQn EXTI14_IRQn
#define RFM_DIO2_EXIT_15_Pin GPIO_PIN_15
#define RFM_DIO2_EXIT_15_GPIO_Port GPIOB
#define RFM_DIO2_EXIT_15_EXTI_IRQn EXTI15_IRQn
#define SPI_CS_Pin GPIO_PIN_9
#define SPI_CS_GPIO_Port GPIOC
#define RMF_RST_Pin GPIO_PIN_11
#define RMF_RST_GPIO_Port GPIOA
#define RFM_DIO5_EXIT_8_Pin GPIO_PIN_8
#define RFM_DIO5_EXIT_8_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

#define RFM_DIO5_EXIT_8_EXTI_IRQn EXTI8_IRQn

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
