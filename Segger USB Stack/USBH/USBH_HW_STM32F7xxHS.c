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
File        : USBH_HW_STM32F7xxHS.c
Purpose     : USB host implementation
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/

#include <stdlib.h>
#include "USBH_Int.h"
#include "USBH_HW_STM32F7xxHS.h"

/*********************************************************************
*
*       Defines
*
**********************************************************************
*/
//
// FIFO sizes. This controller features 4 Kbytes FIFO RAM.
// All sizes are in 32bit words.
// Because of the broken FIFO implementation of the USB controller in the STM32 MCUs,
// most FIFO configurations will not work and result in FIFO stuck.
//
#define USBH_DWC2_RECEIVE_FIFO_SIZE               0x200uL
#define USBH_DWC2_NON_PERIODIC_TRANSMIT_FIFO_SIZE 0x100uL
#define USBH_DWC2_PERIODIC_TRANSMIT_FIFO_SIZE     0x80uL

#define USBH_DWC2_MAX_TRANSFER_SIZE               (1023u * 512u)
#define USBH_DWC2_DEFAULT_TRANSFER_BUFF_SIZE       0x4000u

#define USBH_DWC2_HIGH_SPEED                      1
#define USBH_DWC2_SUPPORT_SPLIT_TRANSACTIONS      1
#define USBH_DWC2_USE_DMA                         1
#define USBH_DWC2_CACHE_LINE_SIZE                 32u
#define DWC2_NUM_CHANNELS                         12u


/*********************************************************************
*
*       Static data
*
**********************************************************************
*/

//
// Include MainCode
//
#include "USBH_HW_DWC2_Int.h"

#define USBH_HW_DWC2_C_
#include "USBH_HW_DWC2_RootHub.c"
#include "USBH_HW_DWC2_EPControl_DMA.c"
#include "USBH_HW_DWC2_BulkIntIso_DMA.c"
#include "USBH_HW_DWC2.c"

/*********************************************************************
*
*       USBH_STM32F7_HS_Add
*
*  Function description
*    Adds a Synopsys DWC2 high speed controller of a STM32F7xx device to the stack.
*
*  Parameters
*    pBase:   Pointer to the base of the controllers register set.
*
*  Return value
*    Reference to the added host controller (0-based index).
*/
U32 USBH_STM32F7_HS_Add(void * pBase) {
  return _DWC2_Add(pBase, 0);
}

/*********************************************************************
*
*       USBH_STM32F7_HS_AddEx
*
*  Function description
*    Adds a Synopsys DWC2 high speed controller of a STM32F7xx or STM32F7xx device to the stack.
*
*  Parameters
*    pBase:   Pointer to the base of the controllers register set.
*    PhyType:  * 0 - use external PHY connected via ULPI interface.
*              * 1 - use internal full-speed PHY.
*
*  Return value
*    Reference to the added host controller (0-based index).
*/
U32 USBH_STM32F7_HS_AddEx(void * pBase, U8 PhyType) {
  return _DWC2_Add(pBase, PhyType);
}

/*********************************************************************
*
*       USBH_STM32F7_HS_SetCacheConfig()
*
*  Function description
*    Configures cache related functionality that might be required by
*    the stack for several purposes such as cache handling in drivers.
*
*  Parameters
*    pConfig : Pointer to an element of SEGGER_CACHE_CONFIG .
*    ConfSize: Size of the passed structure in case library and
*              header size of the structure differs.
*
*  Additional information
*    This function has to called in USBH_X_Config().
*/
void USBH_STM32F7_HS_SetCacheConfig(const SEGGER_CACHE_CONFIG *pConfig, unsigned ConfSize) {
  USBH_SetCacheConfig(pConfig, ConfSize);
}

/*********************************************************************
*
*       USBH_STM32F7_HS_SetCheckAddress
*
*  Function description
*    Installs a function that checks if an address can be used for DMA transfers.
*    Installed function must return 0, if DMA access is allowed for the given address,
*    1 otherwise.
*
*  Parameters
*    pfCheckValidDMAAddress:    Pointer to the function.
*
*  Additional information
*    If the function reports a memory region not valid for DMA, the driver uses a temporary
*    transfer buffer to copy data to and from this area.
*/
void USBH_STM32F7_HS_SetCheckAddress(USBH_CHECK_ADDRESS_FUNC * pfCheckValidDMAAddress) {
  _pfCheckValidDMAAddress = pfCheckValidDMAAddress;
}

/********************************* EOF*******************************/
