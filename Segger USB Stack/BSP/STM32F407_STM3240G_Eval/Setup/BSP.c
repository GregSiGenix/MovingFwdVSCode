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
Purpose : BSP for the ST STM32F40G-Eval eval board
*/

#include "BSP.h"

/*********************************************************************
*
*       Defines
*
**********************************************************************
*/
#define GPIOG_BASE_ADDR           ((unsigned int)0x40021800)
//
// LED port assignement on MB786 eval board
//
#define GPIOG_MODER               (*(volatile unsigned int*)(GPIOG_BASE_ADDR + 0x00))
#define GPIOG_ODR                 (*(volatile unsigned int*)(GPIOG_BASE_ADDR + 0x14))
#define GPIOG_BSRR                (*(volatile unsigned int*)(GPIOG_BASE_ADDR + 0x18))
//
// SFRs used for LED-Port
//
#define RCC_BASE_ADDR             ((unsigned int)(0x40023800))
#define RCC_AHB1RSTR              (*(volatile unsigned int*)(RCC_BASE_ADDR + 0x10))
#define RCC_AHB1ENR               (*(volatile unsigned int*)(RCC_BASE_ADDR + 0x30))

#define RCC_LEDPORT_RSTR          RCC_AHB1RSTR
#define RCC_LEDPORT_ENR           RCC_AHB1ENR
#define RCC_LEDPORT_BIT           (6)
//
// Assign LEDs to Ports
//
#define LED_PORT_MODER            GPIOG_MODER
#define LED_PORT_ODR              GPIOG_ODR
#define LED_PORT_BSRR             GPIOG_BSRR

#define LED0_BIT                  (6)
#define LED1_BIT                  (8)

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
  RCC_LEDPORT_ENR  &= ~(1uL << RCC_LEDPORT_BIT);
  RCC_LEDPORT_RSTR &= ~(1uL << RCC_LEDPORT_BIT);
  RCC_LEDPORT_ENR  |=  (1uL << RCC_LEDPORT_BIT);

  LED_PORT_MODER   &= ~(3uL << (LED0_BIT * 2)) | (3uL << (LED1_BIT * 2));   // Reset mode; sets port to input
  LED_PORT_MODER   |=  (1uL << (LED0_BIT * 2)) | (1uL << (LED1_BIT * 2));   // Set to output mode
  LED_PORT_BSRR     =  (0x10000uL << LED0_BIT) | (0x10000uL << LED1_BIT);   // Initially clear LEDs
}

/*********************************************************************
*
*       BSP_SetLED()
*/
void BSP_SetLED(int Index) {
  if (Index == 0) {
    LED_PORT_BSRR = (1uL << LED0_BIT);       // Switch on LED0
  } else if (Index == 1) {
    LED_PORT_BSRR = (1uL << LED1_BIT);       // Switch on LED1
  }
}

/*********************************************************************
*
*       BSP_ClrLED()
*/
void BSP_ClrLED(int Index) {
  if (Index == 0) {
    LED_PORT_BSRR = (0x10000uL << LED0_BIT); // Switch off LED0
  } else if (Index == 1) {
    LED_PORT_BSRR = (0x10000uL << LED1_BIT); // Switch off LED1
  }
}

/*********************************************************************
*
*       BSP_ToggleLED()
*/
void BSP_ToggleLED(int Index) {
  if (Index == 0) {
    if ((LED_PORT_ODR & (1uL << LED0_BIT)) == 0) {  // LED is switched off
      LED_PORT_BSRR = (1uL << LED0_BIT);            // Switch on LED0
    } else {
      LED_PORT_BSRR = (0x10000uL << LED0_BIT);      // Switch off LED0
    }
  } else if (Index == 1) {
    if ((LED_PORT_ODR & (1uL << LED1_BIT)) == 0) {  // LED is switched off
      LED_PORT_BSRR = (1uL << LED1_BIT);            // Switch on LED1
    } else {
      LED_PORT_BSRR = (0x10000uL << LED1_BIT);      // Switch off LED1
    }
  }
}

/*************************** End of file ****************************/
