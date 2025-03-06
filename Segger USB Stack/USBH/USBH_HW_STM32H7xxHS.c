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
File        : USBH_HW_STM32H7xxHS.c
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
#include "USBH_HW_STM32H7xxHS.h"

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
#define DWC2_NUM_CHANNELS                         16u

#define DWC2_HOST_INIT_OVERRIDE                   1


#include "USBH_HW_DWC2_Int.h"

/*********************************************************************
*
*       _DWC2_HostInit
*
*  Function description
*    Reset and initialize the hardware.
*/
static void _DWC2_HostInit(const USBH_DWC2_INST * pInst) {
  U32 Channel;

  //
  // Remove any settings. Especially important because the controller may be in forced device mode.
  //
  if (pInst->PhyType == 1u) {
    pInst->pHWReg->GUSBCFG = (1uL << 6);     // Internal PHY clock must be enabled before a core reset can be executed.
  } else {
    pInst->pHWReg->GUSBCFG = 0;
  }
  pInst->pHWReg->PCGCCTL = 0;                // Restart the Phy Clock
  USBH_OS_Delay(100);
  while((pInst->pHWReg->GRSTCTL & (1uL << 31)) == 0u) {
  }
  pInst->pHWReg->GRSTCTL = (1uL << 0);       // Core reset
  USBH_OS_Delay(20);
  while((pInst->pHWReg->GRSTCTL & 1u) != 0u) {
  }
  USBH_OS_Delay(50);
  pInst->pHWReg->GUSBCFG |= (1uL << 29)       // Force the OTG controller into host mode.
                         |  (1uL << 24)       // Complement Output signal is not qualified with the Internal VBUS valid comparator
                         |  (1uL << 23)       // PHY inverts ExternalVbusIndicator signal
                         |  (1uL << 21)       // PHY uses an external V BUS valid comparator
                         |  (1uL << 20)       // PHY drives VBUS using external supply
                         ;
  USBH_OS_Delay(100);                        // Wait at least 25 ms after force to host mode (some controllers need more)
  if (pInst->PhyType == 1u) {
    U32 Cfg;
    Cfg = pInst->pHWReg->GUSBCFG;
    Cfg &= ~(0x0FuL << 10);                  // Delete Timeout for calibration set to max value from the change value
    Cfg |= (1uL    <<  6)                    // Enable the internal PHY clock.
        |  (0x07uL <<  0)                    // Timeout for calibration set to max value
        |  (0x0FuL << 10)                    // Set turnaround to max value
        ;
    pInst->pHWReg->GUSBCFG  = Cfg;
    pInst->pHWReg->GCCFG    = (1uL << 16)       // Power down deactivated ("Transceiver active")
                            | (1uL << 18)       // Enable the VBUS sensing "A" device
                            | (1uL << 19)       // Enable the VBUS sensing "B" device
                            | (1uL << 21);      // VBUS sensing disable option
    pInst->pHWReg->HCFG    = 1;
    USBH_OS_Delay(100);
  }
  //
  // Configure data FIFO sizes, if necessary
  //
  _DWC2_ConfigureFIFO(pInst);
  pInst->pHWReg->HFIR     = 48000u;
  pInst->pHWReg->GAHBCFG  = (1u << 5)        // Enable DMA
                          | (3u << 1)        // Set burst length
                          ;
  pInst->pHWReg->GINTMSK = 0;            // Disable all interrupts
  pInst->pHWReg->GINTSTS = 0xFFFFFFFFu;  // Clear any pending interrupts.
  /* Disable all channels interrupt Masks */
  for (Channel = 0; Channel < DWC2_NUM_CHANNELS; Channel++) {
    pInst->pHWReg->aHChannel[Channel].HCINTMSK = 0;
  }
}

/*********************************************************************
*
*  Include MainCode
*
*/
#define USBH_HW_DWC2_C_
//lint -e9019 D:113
#include "USBH_HW_DWC2_RootHub.c"
#include "USBH_HW_DWC2_EPControl_DMA.c"
#include "USBH_HW_DWC2_BulkIntIso_DMA.c"
#include "USBH_HW_DWC2.c"

/*********************************************************************
*
*       USBH_STM32H7_HS_Add
*
*  Function description
*    Adds a Synopsys DWC2 high speed controller of a STM32H7xx device to the stack.
*
*  Parameters
*    pBase:   Pointer to the base of the controllers register set.
*
*  Return value
*    Reference to the added host controller (0-based index).
*/
U32 USBH_STM32H7_HS_Add(void * pBase) {
  return _DWC2_Add(pBase, 0);
}

/*********************************************************************
*
*       USBH_STM32H7_HS_AddEx
*
*  Function description
*    Adds a Synopsys DWC2 high speed controller of a STM32H7xx or STM32H7xx device to the stack.
*
*  Parameters
*    pBase:   Pointer to the base of the controllers register set.
*    PhyType:  * 0 - use external PHY connected via ULPI interface.
*              * 1 - use internal full speed PHY.
*
*  Return value
*    Reference to the added host controller (0-based index).
*/
U32 USBH_STM32H7_HS_AddEx(void * pBase, U8 PhyType) {
  return _DWC2_Add(pBase, PhyType);
}

/*********************************************************************
*
*       USBH_STM32H7_HS_SetCacheConfig()
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
void USBH_STM32H7_HS_SetCacheConfig(const SEGGER_CACHE_CONFIG *pConfig, unsigned ConfSize) {
  USBH_SetCacheConfig(pConfig, ConfSize);
}

/*********************************************************************
*
*       USBH_STM32H7_HS_SetCheckAddress
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
void USBH_STM32H7_HS_SetCheckAddress(USBH_CHECK_ADDRESS_FUNC * pfCheckValidDMAAddress) {
  _pfCheckValidDMAAddress = pfCheckValidDMAAddress;
}

/********************************* EOF*******************************/
