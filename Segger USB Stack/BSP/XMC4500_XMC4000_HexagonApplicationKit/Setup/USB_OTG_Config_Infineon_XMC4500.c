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
File    : USB_OTG_Config_Infineon_XMC4500.c
Purpose : Config file for ST STM32F2xx/4xx FullSpeed Controller
--------  END-OF-HEADER  ---------------------------------------------
*/

#include "USB_OTG.h"
#include "BSP.h"
#include "XMC4500.h"

/*********************************************************************
*
*       Defines
*
**********************************************************************
*/
/*********************************************************************
*
*       Defines, sfrs
*
**********************************************************************
*/
#define SCU_BASE_ADDR  0x50004000

#define SCU_PCU_PWRSTAT      *((volatile U32 *)(SCU_BASE_ADDR + 0x200)) // PCU Status Register
#define SCU_PCU_PWRSET       *((volatile U32 *)(SCU_BASE_ADDR + 0x204)) // PCU Set Control Register
#define SCU_PCU_PWRCLR       *((volatile U32 *)(SCU_BASE_ADDR + 0x208)) // PCU Clear Control Register
#define SCU_PCU_EVRSTAT      *((volatile U32 *)(SCU_BASE_ADDR + 0x210)) // EVR Status Register
#define SCU_PCU_EVRVADCSTAT  *((volatile U32 *)(SCU_BASE_ADDR + 0x214)) // EVR VADC Status Register
#define SCU_PCU_PWRMON       *((volatile U32 *)(SCU_BASE_ADDR + 0x22C)) // Power Monitor Control

#define SCU_RCU_RSTSTAT      *((volatile U32 *)(SCU_BASE_ADDR + 0x400)) // RCU Reset Status
#define SCU_RCU_RSTSET       *((volatile U32 *)(SCU_BASE_ADDR + 0x404)) // RCU Reset Set Register
#define SCU_RCU_RSTCLR       *((volatile U32 *)(SCU_BASE_ADDR + 0x408)) // RCU Reset Clear Register
#define SCU_RCU_PRSTAT0      *((volatile U32 *)(SCU_BASE_ADDR + 0x40C)) // RCU Peripheral 0 Reset Status
#define SCU_RCU_PRSET0       *((volatile U32 *)(SCU_BASE_ADDR + 0x410)) // RCU Peripheral 0 Reset Set
#define SCU_RCU_PRCLR0       *((volatile U32 *)(SCU_BASE_ADDR + 0x414)) // RCU Peripheral 0 Reset Clear
#define SCU_RCU_PRSTAT1      *((volatile U32 *)(SCU_BASE_ADDR + 0x418)) // RCU Peripheral 1 Reset Status
#define SCU_RCU_PRSET1       *((volatile U32 *)(SCU_BASE_ADDR + 0x41C)) // RCU Peripheral 1 Reset Set
#define SCU_RCU_PRCLR1       *((volatile U32 *)(SCU_BASE_ADDR + 0x420)) // RCU Peripheral 1 Reset Clear
#define SCU_RCU_PRSTAT2      *((volatile U32 *)(SCU_BASE_ADDR + 0x424)) // RCU Peripheral 2 Reset Status
#define SCU_RCU_PRSET2       *((volatile U32 *)(SCU_BASE_ADDR + 0x428)) // RCU Peripheral 2 Reset Set
#define SCU_RCU_PRCLR2       *((volatile U32 *)(SCU_BASE_ADDR + 0x42C)) // RCU Peripheral 2 Reset Clear
#define SCU_RCU_PRSTAT3      *((volatile U32 *)(SCU_BASE_ADDR + 0x430)) // RCU Peripheral 3 Reset Status
#define SCU_RCU_PRSET3       *((volatile U32 *)(SCU_BASE_ADDR + 0x434)) // RCU Peripheral 3 Reset Set
#define SCU_RCU_PRCLR3       *((volatile U32 *)(SCU_BASE_ADDR + 0x438)) // RCU Peripheral 3 Reset Clear

#define SCU_CCU_CLKSTAT      *((volatile U32 *)(SCU_BASE_ADDR + 0x600)) // CCU Clock Status Register
#define SCU_CCU_CLKSET       *((volatile U32 *)(SCU_BASE_ADDR + 0x604)) // CCU Clock Set Control Register
#define SCU_CCU_CLKCLR       *((volatile U32 *)(SCU_BASE_ADDR + 0x608)) // CCU Clock clear Control Register
#define SCU_CCU_SYSCLKCR     *((volatile U32 *)(SCU_BASE_ADDR + 0x60C)) // CCU System Clock Control
#define SCU_CCU_CPUCLKCR     *((volatile U32 *)(SCU_BASE_ADDR + 0x610)) // CCU CPU Clock Control
#define SCU_CCU_PBCLKCR      *((volatile U32 *)(SCU_BASE_ADDR + 0x614)) // CCU Peripheral Bus Clock Control
#define SCU_CCU_USBCLKCR     *((volatile U32 *)(SCU_BASE_ADDR + 0x618)) // CCU USB Clock Control
#define SCU_CCU_EBUCLKCR     *((volatile U32 *)(SCU_BASE_ADDR + 0x61C)) // CCU EBU Clock Control
#define SCU_CCU_CCUCLKCR     *((volatile U32 *)(SCU_BASE_ADDR + 0x620)) // CCU CCU Clock Control
#define SCU_CCU_WDTCLKCR     *((volatile U32 *)(SCU_BASE_ADDR + 0x624)) // CCU WDT Clock Control
#define SCU_CCU_EXTCLKCR     *((volatile U32 *)(SCU_BASE_ADDR + 0x628)) // CCU External clock Control Register
#define SCU_CCU_SLEEPCR      *((volatile U32 *)(SCU_BASE_ADDR + 0x62C)) // CCU Sleep Control Register
#define SCU_CCU_DSLEEPCR     *((volatile U32 *)(SCU_BASE_ADDR + 0x630)) // CCU Deep Sleep Control Register

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
*       USB_X_AddDriver
*/
void USB_OTG_X_Config(void) {
  SCU_CCU_CLKSET = (1 << 0);  // Enable USB clock
  SCU_PCU_PWRSET = (1 << 16)  // Enable USB PHY transceiver
                 | (1 << 17)  // Enable USB OTG state
                 ;
  SCU_RCU_PRCLR2 = (1 << 7);  // De-assert Reset from USB controller
  USB_OTG_AddDriver(&USB_OTG_Driver_ST_STM32F2xxFS);
  USB_OTG_DRIVER_STM32F2xxFS_ConfigAddr(0x50040000);
}

/*************************** End of file ****************************/
