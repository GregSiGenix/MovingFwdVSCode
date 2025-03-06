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
File    : USB_OTG_HW_ST_STM32L4xx.c
Purpose : Target OTG USB driver for ST STM32L4xx OTG FullSpeed Controller
--------  END-OF-HEADER  ---------------------------------------------
*/

#include "USB_OTG.h"

/*********************************************************************
*
*       Defines, sfrs
*
**********************************************************************
*/
#define OTG_FS_BASE_ADDR   0x50000000u
//lint -emacro((923,9033,9078), OTG_FS_G*) D:103[a]
//lint -esym(750, OTG_FS_G*)          N:999
#define OTG_FS_GOTGCTL     *((volatile U32 *)(OTG_FS_BASE_ADDR + 0x00u))
#define OTG_FS_GOTGINT     *((volatile U32 *)(OTG_FS_BASE_ADDR + 0x04u))
#define OTG_FS_GAHBCFG     *((volatile U32 *)(OTG_FS_BASE_ADDR + 0x08u))
#define OTG_FS_GUSBCFG     *((volatile U32 *)(OTG_FS_BASE_ADDR + 0x0Cu))
#define OTG_FS_GRSTCTL     *((volatile U32 *)(OTG_FS_BASE_ADDR + 0x10u))
#define OTG_FS_GINTSTS     *((volatile U32 *)(OTG_FS_BASE_ADDR + 0x14u))
#define OTG_FS_GINTMSK     *((volatile U32 *)(OTG_FS_BASE_ADDR + 0x18u))
#define OTG_FS_GRXSTSR     *((volatile U32 *)(OTG_FS_BASE_ADDR + 0x1Cu))
#define OTG_FS_GRXSTSP     *((volatile U32 *)(OTG_FS_BASE_ADDR + 0x20u))
#define OTG_FS_GRXFSIZ     *((volatile U32 *)(OTG_FS_BASE_ADDR + 0x24u))
#define OTG_FS_GNPTXFSIZ   *((volatile U32 *)(OTG_FS_BASE_ADDR + 0x28u))
#define OTG_FS_GNPTXSTS    *((volatile U32 *)(OTG_FS_BASE_ADDR + 0x2Cu))
#define OTG_FS_GCCFG       *((volatile U32 *)(OTG_FS_BASE_ADDR + 0x38u))

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/

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
*/
static void _Init(void) {
  OTG_FS_GAHBCFG &= ~(1uL << 0);   // Disable interrupts
  OTG_FS_GRSTCTL =   (1uL << 0);   // Perform a complete reset of the USB controller
  USBH_OS_Delay(10);
  //
  // Wait until controller is ready
  //
  while ((OTG_FS_GRSTCTL & (1uL << 31)) == 0u) {
  }
//  OTG_FS_GUSBCFG  |= (1uL << 8);   // Enable SRPCAP
  OTG_FS_GINTSTS  = 0xFFFFFFFFu;
  OTG_FS_GOTGINT  = 0xFFFFFFFFu;
  OTG_FS_GCCFG    = (1uL << 16)    // Enable transceiver
                  | (1uL << 21)    // VBUS sensing enable
                  | (1uL << 19)    // Primary detection
                  ;
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
  OTG_FS_GUSBCFG &= ~(1uL << 8);
  OTG_FS_GCCFG   &= ~((1uL << 18) | (1uL << 19) | (1uL << 20));
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
  U32 OTGState;

  OTGState = OTG_FS_GOTGCTL;
  if ((OTGState & ((1uL << 21) | (1uL << 16))) == (1uL << 21)) {
    return USB_OTG_ID_PIN_STATE_IS_HOST;
  }
  if ((OTGState & ((1uL << 21) | (1uL << 19) | (1uL << 16))) == ((1uL << 19) | (1uL << 16))) {
    return USB_OTG_ID_PIN_STATE_IS_DEVICE;
  }
  return USB_OTG_ID_PIN_STATE_IS_INVALID;
}

/*********************************************************************
*
*       Public const
*
**********************************************************************
*/
const USB_OTG_HW_DRIVER USB_OTG_Driver_ST_STM32L4xx = {
  _Init,
  _GetSessionState,
  _DeInit
};

/*************************** End of file ****************************/
