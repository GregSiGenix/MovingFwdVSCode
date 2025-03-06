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
File        : BSP_USB.h
Purpose     : BSP (Board support package) for USB.
---------------------------END-OF-HEADER------------------------------
*/

#ifndef _BSP_USB_H_     // Avoid multiple/recursive inclusion.
#define _BSP_USB_H_  1

#if defined(__cplusplus)
extern "C" {  /* Make sure we have C-declarations in C++ programs */
#endif

/*********************************************************************
*
*       Defines
*
**********************************************************************
*/
//
// In order to avoid warnings for unused parameters.
//
#ifndef BSP_USE_PARA
  #if defined(NC30) || defined(NC308)
    #define BSP_USE_PARA(para)
  #else
    #define BSP_USE_PARA(para) (void)para;
  #endif
#endif

/*********************************************************************
*
*       USBD
*
* Functions for USB device controllers (as far as present).
*/
void BSP_USB_InstallISR      (void (*pfISR)(void));
void BSP_USB_InstallISR_Ex   (int ISRIndex, void (*pfISR)(void), int Prio);
void BSP_USB_ISR_Handler     (void);
void BSP_USB_Init            (void);
void BSP_USB_EnableInterrupt (int ISRIndex);
void BSP_USB_DisableInterrupt(int ISRIndex);

/*********************************************************************
*
*       USBH
*
* Functions for USB Host controllers (as far as present).
*/
void BSP_USBH_InstallISR   (void (*pfISR)(void));
void BSP_USBH_InstallISR_Ex(int ISRIndex, void (*pfISR)(void), int Prio);
void BSP_USBH_Init         (void);

/*********************************************************************
*
*       CACHE
*
* Functions for cache handling (as far as present).
*/
void BSP_CACHE_CleanInvalidateRange(void *p, unsigned NumBytes);
void BSP_CACHE_CleanRange          (void *p, unsigned NumBytes);
void BSP_CACHE_InvalidateRange     (void *p, unsigned NumBytes);


#if defined(__cplusplus)
  }     // Make sure we have C-declarations in C++ programs
#endif

#endif  // Avoid multiple/recursive inclusion

/****** End Of File *************************************************/
