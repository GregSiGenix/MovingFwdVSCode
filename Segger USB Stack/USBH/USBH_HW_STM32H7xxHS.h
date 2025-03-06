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
File        : USBH_HW_STM32H7xxHS.h
Purpose     : Header for the STM32H7 HighSpeed emUSB Host driver
-------------------------- END-OF-HEADER -----------------------------
*/

#ifndef USBH_HW_STM32H7XX_HS_H_
#define USBH_HW_STM32H7XX_HS_H_

#include "SEGGER.h"

#if defined(__cplusplus)
  extern "C" {                 // Make sure we have C-declarations in C++ programs
#endif

U32  USBH_STM32H7_HS_Add           (void * pBase);
U32  USBH_STM32H7_HS_AddEx         (void * pBase, U8 PhyType);
void USBH_STM32H7_HS_SetCacheConfig(const SEGGER_CACHE_CONFIG *pConfig, unsigned ConfSize);
void USBH_STM32H7_HS_SetCheckAddress(USBH_CHECK_ADDRESS_FUNC * pfCheckValidDMAAddress);

#if defined(__cplusplus)
  }
#endif

#endif // USBH_HW_STM32H7XX_HS_H_

/*************************** End of file ****************************/
