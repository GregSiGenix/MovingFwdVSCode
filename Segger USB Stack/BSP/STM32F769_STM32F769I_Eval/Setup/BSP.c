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
Purpose : BSP for the ST STM32F767 EVAL board
--------  END-OF-HEADER  ---------------------------------------------
*/

#include "BSP.h"

/*********************************************************************
*
*       Defines
*
**********************************************************************
*/
#define RCC_BASE_ADDR             (0x40023800u)
#define RCC_AHBENR                (*(volatile unsigned int*)(RCC_BASE_ADDR + 0x30u))
#define RCC_LEDPORT_BIT           (9)

#define GPIOJ_BASE_ADDR           (0x40022400u)
#define GPIOJ_MODER               (*(volatile unsigned int*)(GPIOJ_BASE_ADDR + 0x00u))
#define GPIOJ_ODR                 (*(volatile unsigned int*)(GPIOJ_BASE_ADDR + 0x14u))

#define LED0_BIT                  (0)  // LED1 (orange) - PJ0
#define LED1_BIT                  (1)  // LED2 (red) - PJ1

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
  // Initialize clock and port pins for LEDs
  //
  RCC_AHBENR   |= (1u << RCC_LEDPORT_BIT);

  GPIOJ_MODER &= ~(3u << (LED0_BIT * 2));   // Reset mode; sets port to input
  GPIOJ_MODER |=  (1u << (LED0_BIT * 2));   // Set to output mode
  GPIOJ_ODR   |=  (1u << LED0_BIT);         // Initially clear LEDs, low active

  GPIOJ_MODER &= ~(3u << (LED1_BIT * 2));   // Reset mode; sets port to input
  GPIOJ_MODER |=  (1u << (LED1_BIT * 2));   // Set to output mode
  GPIOJ_ODR   |=  (1u << LED1_BIT);         // Initially clear LEDs, low active
}

/*********************************************************************
*
*       BSP_SetLED()
*/
void BSP_SetLED(int Index) {
  if (Index == 0) {
    GPIOJ_ODR &= ~(1u << LED0_BIT);         // Switch on LED0
  } else if (Index == 1) {
    GPIOJ_ODR &= ~(1u << LED1_BIT);         // Switch on LED1
  }
}

/*********************************************************************
*
*       BSP_ClrLED()
*/
void BSP_ClrLED(int Index) {
  if (Index == 0) {
    GPIOJ_ODR |= (1u << LED0_BIT);          // Switch off LED0
  } else if (Index == 1) {
    GPIOJ_ODR |= (1u << LED1_BIT);          // Switch off LED1
  }
}

/*********************************************************************
*
*       BSP_ToggleLED()
*/
void BSP_ToggleLED(int Index) {
  if (Index == 0) {
    GPIOJ_ODR ^= (1u << LED0_BIT);          // Toggle LED0
  } else if (Index == 1) {
    GPIOJ_ODR ^= (1u << LED1_BIT);          // Toggle LED1
  }
}

/****** End Of File *************************************************/
