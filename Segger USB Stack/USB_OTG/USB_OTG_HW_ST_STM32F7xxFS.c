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
File    : USB_OTG_HW_ST_STM32F7xxFS.c
Purpose : Target OTG USB driver for ST STM32F7xx OTG FullSpeed Controller
--------  END-OF-HEADER  ---------------------------------------------
*/

#include "USB_OTG.h"

/*********************************************************************
*
*       Defines, sfrs
*
**********************************************************************
*/

/*********************************************************************
*
*       Types / structures
*/
//lint -esym(754,GPIO_REGS::*)
typedef struct {
  volatile U32 MODER;
  volatile U32 OTYPER;
  volatile U32 OSPEEDR;
  volatile U32 PUPDR;
  volatile U32 IDR;
} GPIO_REGS;

/*********************************************************************
*
*       static data
*
**********************************************************************
*/
static GPIO_REGS * _Regs = (GPIO_REGS *)0x40020000;        //lint !e923 !e9078  D:103[a]

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _Init
*
*  Function description:
*    Initialize everything on hardware which is necessary in order to
*    to detect a USB session on the OTG controller.
*
*/
static void _Init(void) {
  U32 v;

  v = _Regs->PUPDR;
  //
  // Set PA9 (OTG_FS_VBUS) as GPIN
  //
  _Regs->MODER &= ~(0x3uL << (2 * 9));
  v &= ~(0x03uL << (2 * 9));
  v |=  (0x02uL << (2 * 9));
  //
  // Set PA10 (OTG_FS_ID) as GPIN
  //
  _Regs->MODER &= ~(0x3uL << (2 * 10));
  v &= ~(0x03uL << (2 * 10));
  v |=  (0x01uL << (2 * 10));
  _Regs->PUPDR = v;
}

/*********************************************************************
*
*       _DeInit
*
*  Function description:
*     De-initialize the USB OTG-controller in order to initialize either
*     the Host or Device controller part.
*/
static void _DeInit(void) {
  U32 v;

  v = _Regs->PUPDR;
  v &= ~(0x03uL << (2 * 9));
  v &= ~(0x03uL << (2 * 10));
  _Regs->PUPDR = v;
}

/*********************************************************************
*
*       _GetSessionState
*
*  Function description:
*    Returns whether a valid USB session was detected.
*
*  Return value:
*     == USB_OTG_ID_PIN_STATE_IS_HOST   :  Host session detected
*     == USB_OTG_ID_PIN_STATE_IS_DEVICE :  Device session detected
*     == USB_OTG_ID_PIN_STATE_IS_INVALID:  No valid session
*/
static int _GetSessionState(void) {
  int Ret;

  switch ((_Regs->IDR >> 9) & 3u) {
  case 3:
    Ret = USB_OTG_ID_PIN_STATE_IS_DEVICE;
    break;
  case 0:
    Ret = USB_OTG_ID_PIN_STATE_IS_HOST;
    break;
  default:
    Ret = USB_OTG_ID_PIN_STATE_IS_INVALID;
    break;
  }
  return Ret;
}

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       Public const
*
**********************************************************************
*/
const USB_OTG_HW_DRIVER USB_OTG_Driver_ST_STM32F7xxFS = {
  _Init,
  _GetSessionState,
  _DeInit
};

/*************************** End of file ****************************/
