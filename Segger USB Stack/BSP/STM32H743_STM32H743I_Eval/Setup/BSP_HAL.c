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

File    : BSP_HAL.c
Purpose : Overwrite weak HAL functions
--------  END-OF-HEADER  ---------------------------------------------
*/

#include "RTOS.h"
#include <stdint.h>

/*********************************************************************
*
*       Global functions
*
**********************************************************************
*/
void HAL_Delay(volatile uint32_t Delay);
uint32_t HAL_GetTick(void);
/*********************************************************************
*
*       HAL_Delay()
*
*  Overwrite HAL function
*
*/
void HAL_Delay(volatile uint32_t Delay) {
  OS_Delay(Delay);
}
/*********************************************************************
*
*       HAL_GetTick()
*
*  Overwrite HAL function
*
*/
uint32_t HAL_GetTick(void) {
  return OS_GetTime();
}
