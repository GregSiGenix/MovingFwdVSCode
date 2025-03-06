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
File    : BSP.h
Purpose : BSP (Board support package)
*/

#ifndef BSP_H
#define BSP_H

/*********************************************************************
*
*       Defines
*
**********************************************************************
*/

//
// In order to avoid warnings for unused parameters
//
#ifndef BSP_USE_PARA
  #define BSP_USE_PARA(para)  (void) (para)
#endif

#if   (defined(__ICCARM__) && (__CPU_MODE__ == 1))  // If IAR and THUMB mode
  #define INTERWORK  __interwork
#elif (defined(__ICC430__))
  #define INTERWORK  __intrinsic
#else
  #define INTERWORK
#endif

/*********************************************************************
*
*       Prototypes
*
**********************************************************************
*/

#ifdef __cplusplus
extern "C" {
#endif

void          BSP_Init        (void);
void          BSP_SetLED      (int Index);
void          BSP_ClrLED      (int Index);
void          BSP_ToggleLED   (int Index);
int           BSP_GetLEDState (int Index);
int           BSP_FPGA_Init   (void);

void          MemoryInit      (void);
INTERWORK int __low_level_init(void);

#ifdef __cplusplus
}
#endif

#endif  // BSP_H

/*************************** End of file ****************************/
