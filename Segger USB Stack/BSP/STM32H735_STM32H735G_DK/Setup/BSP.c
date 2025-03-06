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
-------------------------- END-OF-HEADER -----------------------------
File    : BSP.c
Purpose : BSP for STM32H735G-DK board
*/

#include "BSP.h"

/*********************************************************************
*
*       Defines
*
**********************************************************************
*/
#define LED0_PIN         (2)  // PC2, LD2,  red, low active
#define LED1_PIN         (3)  // PC3, LD1, green, low active

#define GPIOC_BASE_ADDR  (0x58020800u)
#define GPIOC_MODER      (*(volatile unsigned int*)(GPIOC_BASE_ADDR + 0x00u))
#define GPIOC_OTYPER     (*(volatile unsigned int*)(GPIOC_BASE_ADDR + 0x04u))
#define GPIOC_OSPEEDR    (*(volatile unsigned int*)(GPIOC_BASE_ADDR + 0x08u))
#define GPIOC_PUPDR      (*(volatile unsigned int*)(GPIOC_BASE_ADDR + 0x0Cu))
#define GPIOC_ODR        (*(volatile unsigned int*)(GPIOC_BASE_ADDR + 0x14u))
#define GPIOC_BSRR       (*(volatile unsigned int*)(GPIOC_BASE_ADDR + 0x18u))

#define RCC_BASE_ADDR    (0x58024400u)
#define RCC_AHB4ENR      (*(volatile unsigned int*)(RCC_BASE_ADDR + 0xE0u))

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
  // Enable GPIO clocks
  //
  RCC_AHB4ENR   |= (0x1u << 2);                // Enable the GPIOC clock
  //
  // Initialize LED0
  //
  GPIOC_MODER   &= ~(0x3u << (LED0_PIN * 2));  // Clear mode register
  GPIOC_MODER   |=  (0x1u << (LED0_PIN * 2));  // Set IO direction to output mode
  GPIOC_OSPEEDR |=  (0x3u << (LED0_PIN * 2));  // Set speed to high frequency
  GPIOC_OTYPER  &= ~(0x1u << (LED0_PIN * 1));  // Set output to push-pull
  GPIOC_PUPDR   &= ~(0x3u << (LED0_PIN * 2));  // Clear the pull-up/pull-down register
  GPIOC_PUPDR   |=  (0x1u << (LED0_PIN * 2));  // Set push-pull to pull-up
  GPIOC_BSRR     =  (0x1u << LED0_PIN);        // Turn LED off
  //
  // Initialize LED1
  //
  GPIOC_MODER   &= ~(0x3u << (LED1_PIN * 2));  // Clear mode register
  GPIOC_MODER   |=  (0x1u << (LED1_PIN * 2));  // Set IO direction to output mode
  GPIOC_OSPEEDR |=  (0x3u << (LED1_PIN * 2));  // Set speed to high frequency
  GPIOC_OTYPER  &= ~(0x1u << (LED1_PIN * 1));  // Set output to push-pull
  GPIOC_PUPDR   &= ~(0x3u << (LED1_PIN * 2));  // Clear the pull-up/pull-down register
  GPIOC_PUPDR   |=  (0x1u << (LED1_PIN * 2));  // Set push-pull to pull-up
  GPIOC_BSRR     =  (0x1u << LED1_PIN);        // Turn LED off
}

/*********************************************************************
*
*       BSP_SetLED()
*/
void BSP_SetLED(int Index) {
  if (Index < 2) {
    GPIOC_BSRR |= ((0x1u << 16) << (LED0_PIN + Index));  // Turn LED on
  }
}

/*********************************************************************
*
*       BSP_ClrLED()
*/
void BSP_ClrLED(int Index) {
  if (Index < 2) {
    GPIOC_BSRR |= (0x1u << (LED0_PIN + Index));          // Turn LED off
  }
}

/*********************************************************************
*
*       BSP_ToggleLED()
*/
void BSP_ToggleLED(int Index) {
  if (Index < 2) {
    GPIOC_ODR ^= (0x1u << (LED0_PIN + Index));           // Toggle LED
  }
}

/*************************** End of file ****************************/
