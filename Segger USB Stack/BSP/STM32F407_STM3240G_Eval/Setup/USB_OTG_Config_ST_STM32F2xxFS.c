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
File    : USB_OTG_Config_ST_STM32F2xxFS.c
Purpose : Config file for ST STM32F2xx/4xx FullSpeed Controller
--------  END-OF-HEADER  ---------------------------------------------
*/

#include "USB_OTG.h"
#include "BSP.h"

/*********************************************************************
*
*       Defines
*
**********************************************************************
*/
//
// RCC
//
#define RCC_BASE_ADDR             ((unsigned int)(0x40023800))
#define RCC_AHB1RSTR              (*(volatile unsigned int*)(RCC_BASE_ADDR + 0x10))
#define RCC_AHB2RSTR              (*(volatile unsigned int*)(RCC_BASE_ADDR + 0x14))
#define RCC_AHB1ENR               (*(volatile unsigned int*)(RCC_BASE_ADDR + 0x30))
#define RCC_AHB2ENR               (*(volatile unsigned int*)(RCC_BASE_ADDR + 0x34))

//
// GPIO
//
#define GPIOA_BASE_ADDR           ((unsigned int)0x40020000)
#define GPIOA_MODER               (*(volatile unsigned int*)(GPIOA_BASE_ADDR + 0x00))
#define GPIOA_OTYPER              (*(volatile unsigned int*)(GPIOA_BASE_ADDR + 0x04))
#define GPIOA_OSPEEDR             (*(volatile unsigned int*)(GPIOA_BASE_ADDR + 0x08))
#define GPIOA_PUPDR               (*(volatile unsigned int*)(GPIOA_BASE_ADDR + 0x0C))
#define GPIOA_AFRL                (*(volatile unsigned int*)(GPIOA_BASE_ADDR + 0x20))
#define GPIOA_AFRH                (*(volatile unsigned int*)(GPIOA_BASE_ADDR + 0x24))


/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       Setup which target USB driver shall be used
*/
/*********************************************************************
*
*       USB_OTG_X_Config
*/
void USB_OTG_X_Config(void) {
  U32 v;

  RCC_AHB1ENR |= 0
               | (1 <<  0)  // GPIOAEN: IO port A clock enable
               ;
  RCC_AHB2ENR |= 0
              | (1 <<  7)  // OTGFSEN: Enable USB OTG FS clock enable
              ;

  RCC_AHB2RSTR |= (1 <<  7)  // OTGFSEN: Reset USB OTG FS clock enable
                ;
  //
  // Set PA9 (USB VBUS) as GPIO input
  //
  v           = GPIOA_MODER;
  v          &= ~(0x3uL << (2 *  9));
  GPIOA_MODER = v;
  v           = GPIOA_AFRH;
  v          &= ~(0xFuL << (4 * 1));
  GPIOA_AFRH  = v;
  //
  // Set PA10 (OTG_FS_ID) as alternate function
  //
  GPIOA_OTYPER  |= (1 << 10);
  GPIOA_OSPEEDR |= (3 << (2 * 10));
  v              = GPIOA_PUPDR;
  v             &= ~(0x3uL << (2 * 10));
  v             |=  (0x1uL << (2 * 10));
  GPIOA_PUPDR = v;
  v             = GPIOA_MODER;
  v            &= ~(0x3uL << (2 * 10));
  v            |=  (0x2uL << (2 * 10));
  GPIOA_MODER   = v;
  v             = GPIOA_AFRH;
  v            &= ~(0xFuL << (4 * 2));
  v            |=  (0xAuL << (4 * 2));
  GPIOA_AFRH    = v;
  //
  // Set PA11 (OTG_FS_DM) as alternate function
  //
  v             = GPIOA_MODER;
  v            &= ~(0x3uL << (2 * 11));
  v            |=  (0x2uL << (2 * 11));
  GPIOA_MODER   = v;
  v             = GPIOA_AFRH;
  v            &= ~(0xFuL << (4 * 3));
  v            |=  (0xAuL << (4 * 3));
  GPIOA_AFRH    = v;
  //
  // Set PA12 (OTG_FS_DP) as alternate function
  //
  v             = GPIOA_MODER;
  v            &= ~(0x3uL << (2 * 12));
  v            |=  (0x2uL << (2 * 12));
  GPIOA_MODER   = v;
  v             = GPIOA_AFRH;
  v            &= ~(0xFuL << (4 * 4));
  v            |=  (0xAuL << (4 * 4));
  GPIOA_AFRH    = v;
  RCC_AHB2RSTR &= ~(1 <<  7)  // OTGFSEN: Reset USB OTG FS clock enable
                ;

  USB_OTG_AddDriver(&USB_OTG_Driver_ST_STM32F2xxFS);
}

/*************************** End of file ****************************/
