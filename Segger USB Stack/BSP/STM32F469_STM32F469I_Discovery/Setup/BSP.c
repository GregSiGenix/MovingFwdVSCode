/*********************************************************************
*                   (c) SEGGER Microcontroller GmbH                  *
*                        The Embedded Experts                        *
**********************************************************************
*                                                                    *
*       (c) 2003 - 2022     SEGGER Microcontroller GmbH              *
*                                                                    *
*       www.segger.com     Support: www.segger.com/ticket            *
*                                                                    *
**********************************************************************
*                                                                    *
*       emUSB-Host * USB Host stack for embedded applications        *
*                                                                    *
*       Please note: Knowledge of this file may under no             *
*       circumstances be used to write a similar product.            *
*       Thank you for your fairness !                                *
*                                                                    *
**********************************************************************
*                                                                    *
*       emUSB-Host version: V2.36.1                                  *
*                                                                    *
**********************************************************************
----------------------------------------------------------------------
Licensing information
Licensor:                 SEGGER Microcontroller Systems LLC
Licensed to:              React Health, Inc., 203 Avenue A NW, Suite 300, Winter Haven FL 33881, USA
Licensed SEGGER software: emUSB-Host
License number:           USBH-00304
License model:            SSL [Single Developer Single Platform Source Code License]
Licensed product:         -
Licensed platform:        STM32F4, IAR
Licensed number of seats: 1
----------------------------------------------------------------------
Support and Update Agreement (SUA)
SUA period:               2022-05-19 - 2022-11-19
Contact to extend SUA:    sales@segger.com
----------------------------------------------------------------------
File    : BSP.c
Purpose : BSP for the ST STM32F469 Discovery eval board
--------  END-OF-HEADER  ---------------------------------------------
*/

#include "BSP.h"

/*********************************************************************
*
*       Defines
*
**********************************************************************
*/
#define RCC_BASE_ADDR     (0x40023800u)
#define RCC_AHB1RSTR      (*(volatile unsigned int*)(RCC_BASE_ADDR + 0x10u))
#define RCC_AHBENR        (*(volatile unsigned int*)(RCC_BASE_ADDR + 0x30u))
#define RCC_LEDPORT_MASK  ((1u << 3u) | (1u << 6u) | (1u << 10u))  // GPIO_D, GPIO_G, GPIO_K

#define GPIOD_BASE_ADDR   (0x40020C00u)
#define GPIOD_MODER       (*(volatile unsigned int*)(GPIOD_BASE_ADDR        ))
#define GPIOD_ODR         (*(volatile unsigned int*)(GPIOD_BASE_ADDR + 0x14u))

#define GPIOG_BASE_ADDR   (0x40021800u)
#define GPIOG_MODER       (*(volatile unsigned int*)(GPIOG_BASE_ADDR        ))
#define GPIOG_ODR         (*(volatile unsigned int*)(GPIOG_BASE_ADDR + 0x14u))

#define GPIOK_BASE_ADDR   (0x40022800u)
#define GPIOK_MODER       (*(volatile unsigned int*)(GPIOK_BASE_ADDR        ))
#define GPIOK_ODR         (*(volatile unsigned int*)(GPIOK_BASE_ADDR + 0x14u))

#define LED0_BIT          (6)  // LD1 (green)  - PG6
#define LED1_BIT          (4)  // LD2 (orange) - PD4
#define LED2_BIT          (5)  // LD3 (red)    - PD5
#define LED3_BIT          (3)  // LD4 (blue)   - PK3

/*********************************************************************
*
*       Global functions
*
**********************************************************************
*/

/*********************************************************************
*
*       BSP_Init()
*/
void BSP_Init(void) {
  //
  // Initialize port for LEDs (sample application)
  //
  RCC_AHBENR   &= ~RCC_LEDPORT_MASK;
  RCC_AHB1RSTR &= ~RCC_LEDPORT_MASK;
  RCC_AHBENR   |=  RCC_LEDPORT_MASK;

  GPIOG_MODER &= ~(3u << (LED0_BIT * 2));  // Reset mode; sets port to input
  GPIOG_MODER |=  (1u << (LED0_BIT * 2));  // Set port to output
  GPIOG_ODR   |=  (1u << LED0_BIT);        // Initially clear LED

  GPIOD_MODER &= ~(3u << (LED1_BIT * 2));  // Reset mode; sets port to input
  GPIOD_MODER |=  (1u << (LED1_BIT * 2));  // Set port to output
  GPIOD_ODR   |=  (1u << LED1_BIT);        // Initially clear LED

  GPIOD_MODER &= ~(3u << (LED2_BIT * 2));  // Reset mode; sets port to input
  GPIOD_MODER |=  (1u << (LED2_BIT * 2));  // Set port to output
  GPIOD_ODR   |=  (1u << LED2_BIT);        // Initially clear LED

  GPIOK_MODER &= ~(3u << (LED3_BIT * 2));  // Reset mode; sets port to input
  GPIOK_MODER |=  (1u << (LED3_BIT * 2));  // Set port to output
  GPIOK_ODR   |=  (1u << LED3_BIT);        // Initially clear LED
}

/*********************************************************************
*
*       BSP_SetLED()
*/
void BSP_SetLED(int Index) {
  if        (Index == 0) {
    GPIOG_ODR &= ~(1u << LED0_BIT);        // Switch on LD1
  } else if (Index == 1) {
    GPIOD_ODR &= ~(1u << LED1_BIT);        // Switch on LD2
  } else if (Index == 2) {
    GPIOD_ODR &= ~(1u << LED2_BIT);        // Switch on LD3
  } else if (Index == 3) {
    GPIOK_ODR &= ~(1u << LED3_BIT);        // Switch on LD4
  }
}

/*********************************************************************
*
*       BSP_ClrLED()
*/
void BSP_ClrLED(int Index) {
  if        (Index == 0) {
    GPIOG_ODR |= (1u << LED0_BIT);         // Switch off LD1
  } else if (Index == 1) {
    GPIOD_ODR |= (1u << LED1_BIT);         // Switch off LD2
  } else if (Index == 2) {
    GPIOD_ODR |= (1u << LED2_BIT);         // Switch off LD3
  } else if (Index == 3) {
    GPIOK_ODR |= (1u << LED3_BIT);         // Switch off LD4
  }
}

/*********************************************************************
*
*       BSP_ToggleLED()
*/
void BSP_ToggleLED(int Index) {
  if        (Index == 0) {
    GPIOG_ODR ^= (1u << LED0_BIT);         // Toggle LD1
  } else if (Index == 1) {
    GPIOD_ODR ^= (1u << LED1_BIT);         // Toggle LD2
  } else if (Index == 2) {
    GPIOD_ODR ^= (1u << LED2_BIT);         // Toggle LD3
  } else if (Index == 3) {
    GPIOK_ODR ^= (1u << LED3_BIT);         // Toggle LD4
  }
}

/****** End Of File *************************************************/
