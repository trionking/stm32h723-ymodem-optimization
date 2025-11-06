/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
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
#include "stm32h7xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

extern MDMA_HandleTypeDef hmdma_mdma_channel0_sw_0;
extern MDMA_HandleTypeDef hmdma_mdma_channel1_sdmmc1_end_data_0;

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

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);
void MX_SDMMC1_SD_Init(void);

/* USER CODE BEGIN EFP */

void MX_SDMMC1_SD_Init(void);

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define IN_ROT2_Pin GPIO_PIN_2
#define IN_ROT2_GPIO_Port GPIOE
#define IN_ROT3_Pin GPIO_PIN_3
#define IN_ROT3_GPIO_Port GPIOE
#define IN_ROT4_Pin GPIO_PIN_4
#define IN_ROT4_GPIO_Port GPIOE
#define IN_ROT5_Pin GPIO_PIN_5
#define IN_ROT5_GPIO_Port GPIOE
#define OT_LD0_Pin GPIO_PIN_0
#define OT_LD0_GPIO_Port GPIOF
#define OT_LD1_Pin GPIO_PIN_1
#define OT_LD1_GPIO_Port GPIOF
#define OT_LD2_Pin GPIO_PIN_2
#define OT_LD2_GPIO_Port GPIOF
#define OT_LD3_Pin GPIO_PIN_3
#define OT_LD3_GPIO_Port GPIOF
#define OT_LD4_Pin GPIO_PIN_4
#define OT_LD4_GPIO_Port GPIOF
#define OT_LD5_Pin GPIO_PIN_5
#define OT_LD5_GPIO_Port GPIOF
#define OT_LD6_Pin GPIO_PIN_6
#define OT_LD6_GPIO_Port GPIOF
#define OT_LD7_Pin GPIO_PIN_7
#define OT_LD7_GPIO_Port GPIOF
#define OT_SPI1_NSS1_Pin GPIO_PIN_4
#define OT_SPI1_NSS1_GPIO_Port GPIOA
#define OT_MON_SEL0_Pin GPIO_PIN_0
#define OT_MON_SEL0_GPIO_Port GPIOB
#define OT_MON_SEL1_Pin GPIO_PIN_1
#define OT_MON_SEL1_GPIO_Port GPIOB
#define OT_MON_SEL2_Pin GPIO_PIN_2
#define OT_MON_SEL2_GPIO_Port GPIOB
#define OT_SPI1_NSS2_Pin GPIO_PIN_11
#define OT_SPI1_NSS2_GPIO_Port GPIOF
#define OT_SPI1_NSS3_Pin GPIO_PIN_12
#define OT_SPI1_NSS3_GPIO_Port GPIOF
#define IN_SPI1_RDY1_Pin GPIO_PIN_13
#define IN_SPI1_RDY1_GPIO_Port GPIOF
#define IN_SPI1_RDY2_Pin GPIO_PIN_14
#define IN_SPI1_RDY2_GPIO_Port GPIOF
#define IN_SPI1_RDY3_Pin GPIO_PIN_15
#define IN_SPI1_RDY3_GPIO_Port GPIOF
#define IN_DIP0_Pin GPIO_PIN_0
#define IN_DIP0_GPIO_Port GPIOG
#define IN_DIP1_Pin GPIO_PIN_1
#define IN_DIP1_GPIO_Port GPIOG
#define OT_ETH_RST_Pin GPIO_PIN_12
#define OT_ETH_RST_GPIO_Port GPIOE
#define IN_DIP2_Pin GPIO_PIN_2
#define IN_DIP2_GPIO_Port GPIOG
#define IN_DIP3_Pin GPIO_PIN_3
#define IN_DIP3_GPIO_Port GPIOG
#define IN_DIP4_Pin GPIO_PIN_4
#define IN_DIP4_GPIO_Port GPIOG
#define IN_DIP5_Pin GPIO_PIN_5
#define IN_DIP5_GPIO_Port GPIOG
#define IN_DIP6_Pin GPIO_PIN_6
#define IN_DIP6_GPIO_Port GPIOG
#define IN_DIP7_Pin GPIO_PIN_7
#define IN_DIP7_GPIO_Port GPIOG
#define IN_nSD_CD_Pin GPIO_PIN_7
#define IN_nSD_CD_GPIO_Port GPIOC
#define OT_nA_SD_Pin GPIO_PIN_3
#define OT_nA_SD_GPIO_Port GPIOD
#define OT_SD_EN_Pin GPIO_PIN_9
#define OT_SD_EN_GPIO_Port GPIOG
#define OT_REL0_Pin GPIO_PIN_10
#define OT_REL0_GPIO_Port GPIOG
#define OT_REL1_Pin GPIO_PIN_11
#define OT_REL1_GPIO_Port GPIOG
#define OT_REL2_Pin GPIO_PIN_12
#define OT_REL2_GPIO_Port GPIOG
#define OT_REL3_Pin GPIO_PIN_13
#define OT_REL3_GPIO_Port GPIOG
#define OT_REL4_Pin GPIO_PIN_14
#define OT_REL4_GPIO_Port GPIOG
#define OT_REL5_Pin GPIO_PIN_15
#define OT_REL5_GPIO_Port GPIOG
#define OT_SYS_Pin GPIO_PIN_6
#define OT_SYS_GPIO_Port GPIOB
#define OT_REV_Pin GPIO_PIN_7
#define OT_REV_GPIO_Port GPIOB
#define IN_ROT0_Pin GPIO_PIN_0
#define IN_ROT0_GPIO_Port GPIOE
#define IN_ROT1_Pin GPIO_PIN_1
#define IN_ROT1_GPIO_Port GPIOE

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
