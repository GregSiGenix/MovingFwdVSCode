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
Purpose : BSP for STM32746G Discovery eval board
--------  END-OF-HEADER  ---------------------------------------------
*/

#include "BSP.h"
#include "stm32f7xx.h"  // Device specific header file, contains CMSIS

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
  RCC->AHB1ENR   |= (RCC_AHB1ENR_GPIOIEN);  // GPIOIEN: IO port I clock enable
  GPIOI->MODER    = (GPIOI->MODER & ~(3u << 2)) | (1u << 2);
  GPIOI->OTYPER  &= ~(1u << 1);
  GPIOI->OSPEEDR |=  (3u << 2);
  GPIOI->PUPDR    = (GPIOI->PUPDR & ~(3u << 2)) | (1u << 2);
}

/*********************************************************************
*
*       BSP_SetLED()
*/
void BSP_SetLED(int Index) {
  if (Index == 0) {
    GPIOI->BSRR = (0x00001u << 1);    // Switch on LED
  }
}

/*********************************************************************
*
*       BSP_ClrLED()
*/
void BSP_ClrLED(int Index) {
  if (Index == 0) {
    GPIOI->BSRR = (0x10000u << 1);    // Switch off LED
  }
}

/*********************************************************************
*
*       BSP_ToggleLED()
*/
void BSP_ToggleLED(int Index) {
  if (Index == 0) {
    if (GPIOI->ODR & (1u << 1)) {     // LED is switched off
      GPIOI->BSRR = (0x10000u << 1);  // Switch off LED
    } else {
      GPIOI->BSRR = (0x00001u << 1);  // Switch on LED
    }
  }
}

/****** End Of File *************************************************/
