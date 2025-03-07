/*********************************************************************
*                     SEGGER Microcontroller GmbH                    *
*                        The Embedded Experts                        *
**********************************************************************
*                                                                    *
*       (c) 2003 - 2022  SEGGER Microcontroller GmbH                 *
*                                                                    *
*       www.segger.com     Support: support_emfile@segger.com        *
*                                                                    *
**********************************************************************
*                                                                    *
*       emFile * File system for embedded applications               *
*                                                                    *
*                                                                    *
*       Please note:                                                 *
*                                                                    *
*       Knowledge of this file may under no circumstances            *
*       be used to write a similar product for in-house use.         *
*                                                                    *
*       Thank you for your fairness !                                *
*                                                                    *
**********************************************************************
*                                                                    *
*       emFile version: V5.20.0                                      *
*                                                                    *
**********************************************************************
----------------------------------------------------------------------
Licensing information
Licensor:                 SEGGER Microcontroller Systems LLC
Licensed to:              React Health Inc, 203 Avenue A NW, Suite 300, Winter Haven FL 33881, USA
Licensed SEGGER software: emFile
License number:           FS-00855
License model:            SSL [Single Developer Single Platform Source Code License]
Licensed product:         -
Licensed platform:        STM32F4, IAR
Licensed number of seats: 1
----------------------------------------------------------------------
Support and Update Agreement (SUA)
SUA period:               2022-05-19 - 2022-11-19
Contact to extend SUA:    sales@segger.com
----------------------------------------------------------------------
File        : FS_NOR_PHY_SFDP.c
Purpose     : Low level flash driver for NOR SPI flash which support
              the Serial Flash Discoverable Parameters JDEDEC standard.
Literature  :
  [1] JEDEC STANDARD Serial Flash Discoverable Parameters (SFDP) JESD216B
    (\\fileserver\Techinfo\Standard\Jedec\CFI\JESD216B_SerialFlashDiscoverableParameters.pdf)
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#include "FS_Int.h"
#include "FS_NOR_Int.h"

/*********************************************************************
*
*       Defines, fixed
*
**********************************************************************
*/

/*********************************************************************
*
*       ASSERT_SECTOR_INDEX_IS_IN_RANGE
*/
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL)
  #define ASSERT_SECTOR_INDEX_IS_IN_RANGE(pInst, SectorIndex)                  \
    if ((SectorIndex) >= (pInst)->NumSectors) {                                \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SFDP: Invalid sector index.")); \
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);                                     \
    }
#else
  #define ASSERT_SECTOR_INDEX_IS_IN_RANGE(pInst, SectorIndex)
#endif

/*********************************************************************
*
*       ASSERT_UNIT_NO_IS_IN_RANGE
*/
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL)
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)                                    \
    if ((Unit) >= (U8)FS_NOR_NUM_UNITS) {                                     \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SFDP: Invalid unit number.")); \
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);                                    \
    }
#else
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)
#endif

/*********************************************************************
*
*       ASSERT_HW_TYPE_IS_SET
*/
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL)
  #define ASSERT_HW_TYPE_IS_SET(pInst)                                      \
    if ((pInst)->pHWType == NULL) {                                         \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR_SFDP: HW layer not set.")); \
      FS_X_PANIC(FS_ERRCODE_HW_LAYER_NOT_SET);                              \
    }
#else
  #define ASSERT_HW_TYPE_IS_SET(pInst)
#endif

/*********************************************************************
*
*       ASSERT_VALID_BUS_WIDTH
*/
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL)
  #define ASSERT_VALID_BUS_WIDTH(BusWidth)                                   \
    if (((BusWidth) != 1u) && ((BusWidth) != 2u) && ((BusWidth) != 4u)) {    \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR_SFDP: Invalid bus width.")); \
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);                                   \
    }
#else
  #define ASSERT_VALID_BUS_WIDTH(BusWidth)
#endif

/*********************************************************************
*
*      Local data types
*
**********************************************************************
*/

/*********************************************************************
*
*      NOR_SFDP_INST
*/
typedef struct {
  U8                                  Unit;
  U8                                  IsInited;                // Set to 1 if the driver instance has been initialized
  U32                                 NumSectors;              // Number of sectors to be used as storage
  U32                                 StartAddrConf;           // Configured start address
  U32                                 StartAddrUsed;           // Start addr. actually used (aligned to start of a sector)
  U32                                 NumBytes;                // Number of bytes to be used as storage
  FS_NOR_SPI_POLL_PARA                PollParaSectorErase;     // Polling parameters for the sector erase operation.
  FS_NOR_SPI_POLL_PARA                PollParaPageWrite;       // Polling parameters for the page write operation.
  U32                                 Delay1ms;                // Number of software cycles to block the execution for about 1 ms.
  FS_NOR_SPI_DEVICE                   Device;                  // NOR device related information
  const FS_NOR_SPI_DEVICE_LIST      * pDeviceList;             // List of supported devices
  const FS_NOR_HW_TYPE_SPI          * pHWType;                 // HW access routines
  const FS_NOR_SPI_DEVICE_PARA_LIST * pDeviceParaList;         // List of device parameters specified by the application.
} NOR_SFDP_INST;

/*********************************************************************
*
*      Static data
*
**********************************************************************
*/
static NOR_SFDP_INST      * _apInst[FS_NOR_NUM_UNITS];
#if FS_SUPPORT_TEST
  static FS_NOR_TEST_HOOK_NOTIFICATION * _pfTestHookFailSafe;
#endif

/*********************************************************************
*
*      Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _ld
*/
static unsigned _ld(U32 Value) {
  unsigned i;

  for (i = 0; i < 32u; i++) {
    if ((1uL << i) == Value) {
      break;
    }
  }
  return i;
}

/*********************************************************************
*
*      _Write
*
*  Function description
*    Transfers data from MCU to NOR flash device via SPI.
*
*  Parameters
*    pInst      Physical layer instance.
*    pData      Data to be transferred.
*    NumBytes   Number of bytes to be transferred.
*    BusWidth   Number of data lines to be used for the data transfer.
*
*  Return value
*    ==0     OK, data transferred successfully.
*    !=0     An error occurred.
*/
static int _Write(const NOR_SFDP_INST * pInst, const U8 * pData, U32 NumBytes, U16 BusWidth) {
  U8  Unit;
  int r;

  ASSERT_VALID_BUS_WIDTH(BusWidth);
  r    = 0;
  Unit = pInst->Unit;
  if (pInst->pHWType->pfWriteEx != NULL) {
    //
    // Prefer calling the extended write function for improved error handling.
    //
    r = pInst->pHWType->pfWriteEx(Unit, pData, NumBytes, (U8)BusWidth);
  } else {
    switch (BusWidth) {
    case 1:
      pInst->pHWType->pfWrite(Unit, pData, (int)NumBytes);
      break;
    case 2:
      pInst->pHWType->pfWrite_x2(Unit, pData, (int)NumBytes);
      break;
    case 4:
      pInst->pHWType->pfWrite_x4(Unit, pData, (int)NumBytes);
      break;
    default:
      //
      // Error, invalid bus width.
      //
      r = 1;
      break;
    }
  }
  return r;
}

/*********************************************************************
*
*      _Read
*
*  Function description
*    Transfers data from NOR flash device to MCU via SPI.
*
*  Parameters
*    pInst      Physical layer instance.
*    pData      Transferred data.
*    NumBytes   Number of bytes to be transferred.
*    BusWidth   Number of data lines to be used for the data transfer.
*
*  Return value
*    ==0     OK, data transferred successfully.
*    !=0     An error occurred.
*/
static int _Read(const NOR_SFDP_INST * pInst, U8 * pData, U32 NumBytes, U16 BusWidth) {
  U8  Unit;
  int r;

  ASSERT_VALID_BUS_WIDTH(BusWidth);
  r    = 0;
  Unit = pInst->Unit;
  if (pInst->pHWType->pfReadEx != NULL) {
    //
    // Prefer calling the extended read function for improved error handling.
    //
    r = pInst->pHWType->pfReadEx(Unit, pData, NumBytes, (U8)BusWidth);
  } else {
    switch (BusWidth) {
    case 1:
      pInst->pHWType->pfRead(Unit, pData, (int)NumBytes);
      break;
    case 2:
      pInst->pHWType->pfRead_x2(Unit, pData, (int)NumBytes);
      break;
    case 4:
      pInst->pHWType->pfRead_x4(Unit, pData, (int)NumBytes);
      break;
    default:
      //
      // Error, invalid bus width.
      //
      r = 1;
      break;
    }
  }
  return r;
}

/*********************************************************************
*
*      _EnableCS
*/
static void _EnableCS(const NOR_SFDP_INST * pInst) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWType->pfEnableCS(Unit);
}

/*********************************************************************
*
*      _DisableCS
*/
static void _DisableCS(const NOR_SFDP_INST * pInst) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWType->pfDisableCS(Unit);
}

/*********************************************************************
*
*      _Delay
*/
static int _Delay(const NOR_SFDP_INST * pInst, U32 ms) {
  U8  Unit;
  int r;

  r    = -1;              // Set to indicate that the feature is not supported.
  Unit = pInst->Unit;
  if (pInst->pHWType->pfDelay != NULL) {
    r = pInst->pHWType->pfDelay(Unit, ms);
  }
  return r;
}

/*********************************************************************
*
*      _Lock
*
*  Function description
*    Requests exclusive access to SPI bus.
*/
static void _Lock(const NOR_SFDP_INST * pInst) {
  U8 Unit;

  Unit = pInst->Unit;
  if (pInst->pHWType->pfLock != NULL) {
    pInst->pHWType->pfLock(Unit);
  }
}

/*********************************************************************
*
*      _Unlock
*
*  Function description
*    Releases the exclusive access of SPI bus.
*/
static void _Unlock(const NOR_SFDP_INST * pInst) {
  U8 Unit;

  Unit = pInst->Unit;
  if (pInst->pHWType->pfUnlock != NULL) {
    pInst->pHWType->pfUnlock(Unit);
  }
}

/*********************************************************************
*
*      _CMD_Control
*/
static int _CMD_Control(void * pContext, U8 Cmd, unsigned BusWidth) {
  NOR_SFDP_INST * pInst;
  int             r;

  pInst = SEGGER_PTR2PTR(NOR_SFDP_INST, pContext);
  _EnableCS(pInst);
  r = _Write(pInst, &Cmd, sizeof(Cmd), (U16)FS_BUSWIDTH_GET_CMD(BusWidth));
  _DisableCS(pInst);
  return r;
}

/*********************************************************************
*
*      _CMD_WriteData
*/
static int _CMD_WriteData(void * pContext, U8 Cmd, const U8 * pData, unsigned NumBytes, unsigned BusWidth) {
  NOR_SFDP_INST * pInst;
  int             r;
  int             Result;

  r     = 0;
  pInst = SEGGER_PTR2PTR(NOR_SFDP_INST, pContext);
  _EnableCS(pInst);
  Result = _Write(pInst, &Cmd, sizeof(Cmd), (U16)FS_BUSWIDTH_GET_CMD(BusWidth));
  r = (Result != 0) ? Result : r;
  Result = _Write(pInst, pData, NumBytes, (U16)FS_BUSWIDTH_GET_DATA(BusWidth));
  r = (Result != 0) ? Result : r;
  _DisableCS(pInst);
  return r;
}

/*********************************************************************
*
*      _CMD_ReadData
*/
static int _CMD_ReadData(void * pContext, U8 Cmd, U8 * pData, unsigned NumBytes, unsigned BusWidth) {
  NOR_SFDP_INST * pInst;
  int             r;
  int             Result;

  r     = 0;
  pInst = SEGGER_PTR2PTR(NOR_SFDP_INST, pContext);
  _EnableCS(pInst);
  Result = _Write(pInst, &Cmd, sizeof(Cmd), (U16)FS_BUSWIDTH_GET_CMD(BusWidth));
  r = (Result != 0) ? Result : r;
  Result = _Read(pInst, pData, NumBytes, (U16)FS_BUSWIDTH_GET_DATA(BusWidth));
  r = (Result != 0) ? Result : r;
  _DisableCS(pInst);
  return r;
}

/*********************************************************************
*
*      _CMD_WriteDataWithAddr
*/
static int _CMD_WriteDataWithAddr(void * pContext, U8 Cmd, const U8 * pPara, unsigned NumBytesPara, unsigned NumBytesAddr, const U8 * pData, unsigned NumBytesData, unsigned BusWidth) {
  NOR_SFDP_INST * pInst;
  int             r;
  int             Result;

  FS_USE_PARA(NumBytesAddr);
  r     = 0;
  pInst = SEGGER_PTR2PTR(NOR_SFDP_INST, pContext);
  _EnableCS(pInst);
  Result = _Write(pInst, &Cmd, sizeof(Cmd), (U16)FS_BUSWIDTH_GET_CMD(BusWidth));
  r = (Result != 0) ? Result : r;
  Result = _Write(pInst, pPara, NumBytesPara, (U16)FS_BUSWIDTH_GET_ADDR(BusWidth));
  r = (Result != 0) ? Result : r;
#if FS_SUPPORT_TEST
  //
  // We want to test if the NOR driver recovers correctly from
  // an unexpected reset that occurs during a write to management
  // information. Typically, the number of bytes written at once
  // in this case is smaller than 32.
  //
  if (NumBytesData > 32u) {
   Result = _Write(pInst, pData, NumBytesData, (U16)FS_BUSWIDTH_GET_DATA(BusWidth));
   r = (Result != 0) ? Result : r;
  } else {
    if (NumBytesData != 0u) {
      do {
        Result = _Write(pInst, pData++, 1, (U16)FS_BUSWIDTH_GET_DATA(BusWidth));
        r = (Result != 0) ? Result : r;

        //
        // Fail-safe TP. If the power fails at this point the serial NOR flash writes incomplete data.
        //
        if (_pfTestHookFailSafe != NULL) {
          _pfTestHookFailSafe(pInst->Unit);
        }

      } while (--NumBytesData != 0u);
    }
  }
#else
  Result = _Write(pInst, pData, NumBytesData, (U16)FS_BUSWIDTH_GET_DATA(BusWidth));
  r = (Result != 0) ? Result : r;
#endif
  _DisableCS(pInst);
  return r;
}

/*********************************************************************
*
*      _CMD_ReadDataWithAddr
*/
static int _CMD_ReadDataWithAddr(void * pContext, U8 Cmd, const U8 * pPara, unsigned NumBytesPara, unsigned NumBytesAddr, U8 * pData, unsigned NumBytesData, unsigned BusWidth) {
  NOR_SFDP_INST * pInst;
  int             r;
  int             Result;

  FS_USE_PARA(NumBytesAddr);
  r     = 0;
  pInst = SEGGER_PTR2PTR(NOR_SFDP_INST, pContext);
  _EnableCS(pInst);
  Result = _Write(pInst, &Cmd, sizeof(Cmd), (U16)FS_BUSWIDTH_GET_CMD(BusWidth));
  r = (Result != 0) ? Result : r;
  Result = _Write(pInst, pPara, NumBytesPara, (U16)FS_BUSWIDTH_GET_ADDR(BusWidth));
  r = (Result != 0) ? Result : r;
  Result = _Read(pInst, pData, NumBytesData, (U16)FS_BUSWIDTH_GET_DATA(BusWidth));
  r = (Result != 0) ? Result : r;
  _DisableCS(pInst);
  return r;
}

/*********************************************************************
*
*      _CMD_Delay
*/
static int _CMD_Delay(void * pContext, unsigned ms) {
  NOR_SFDP_INST * pInst;
  U8              abDummy[2];     // Delay1ms is calculated for sending 2 bytes on one cycle.
  unsigned        NumCycles;
  int             r;

  pInst = SEGGER_PTR2PTR(NOR_SFDP_INST, pContext);
  //
  // Prefer the hardware implementation if available.
  //
  r = _Delay(pInst, ms);
  if (r < 0) {
    //
    // Create the delay in the software.
    //
    FS_MEMSET(abDummy, 0xFF, sizeof(abDummy));
    NumCycles = pInst->Delay1ms * ms;
    if (NumCycles != 0u) {
      do {
        (void)_Write(pInst, abDummy, sizeof(abDummy), 1);
      } while (--NumCycles != 0u);
    }
  }
  return r;
}

/*********************************************************************
*
*      _Cmd
*/
static const FS_NOR_SPI_CMD _Cmd = {
  _CMD_Control,
  _CMD_WriteData,
  _CMD_ReadData,
  _CMD_WriteDataWithAddr,
  _CMD_ReadDataWithAddr,
  NULL,
  _CMD_Delay,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL
};

/*********************************************************************
*
*      _CalcStorageArea
*
*  Function description
*    Determines which physical sectors are used as storage.
*/
static int _CalcStorageArea(NOR_SFDP_INST * pInst) {
  U32                       NumSectors;
  U32                       BytesPerSector;
  I32                       NumBytesToSkip;
  I32                       NumBytesRem;
  U32                       NumBytesSkipped;
  U32                       NumBytes;
  U32                       NumBytesBlock;
  U8                        ldBytesPerSector;
  unsigned                  NumSectorBlocksConf;
  unsigned                  NumSectorBlocksUsed;
  U32                       NumSectorsRem;
  U32                       NumSectorsTotal;
  FS_NOR_SPI_SECTOR_BLOCK * pSectorBlockConf;
  FS_NOR_SPI_SECTOR_BLOCK * pSectorBlockUsed;

  pSectorBlockConf    = pInst->Device.Inst.aSectorBlock;
  pSectorBlockUsed    = pInst->Device.Inst.aSectorBlock;
  NumSectorBlocksConf = pInst->Device.Inst.NumSectorBlocks;
  NumSectorBlocksUsed = 0;
  NumSectorsTotal     = 0;
  NumBytesToSkip      = (I32)pInst->StartAddrConf;
  NumBytesSkipped     = 0;
  NumBytesRem         = (I32)pInst->NumBytes;
  NumBytes            = 0;
  if (NumSectorBlocksConf == 0u) {
    return 1;                     // Error, invalid number of sectors.
  }
  do {
    ldBytesPerSector = pSectorBlockConf->ldBytesPerSector;
    NumSectors       = pSectorBlockConf->NumSectors;
    BytesPerSector   = 1uL << ldBytesPerSector;
    //
    // Take care of bytes to skip before storage area.
    //
    while ((NumSectors != 0u) && (NumBytesToSkip > 0)) {
      NumBytesToSkip  -= (I32)BytesPerSector;
      NumBytesSkipped += BytesPerSector;
      NumSectors--;
    }
    if (NumSectors != 0u) {
      NumSectorsRem = (U32)NumBytesRem >> ldBytesPerSector;
      if (NumSectors > NumSectorsRem) {
        NumSectors = NumSectorsRem;
      }
      NumBytesBlock  = NumSectors << ldBytesPerSector;
      NumBytesRem   -= (I32)NumBytesBlock;
      NumBytes      += NumBytesBlock;         // Update the actual number of bytes used as storage.
      //
      // Take care of bytes to skip after data area
      //
      if (NumSectors != 0u) {
        pSectorBlockUsed->ldBytesPerSector = ldBytesPerSector;
        pSectorBlockUsed->NumSectors       = NumSectors;
        ++NumSectorBlocksUsed;
        ++pSectorBlockUsed;
        NumSectorsTotal += NumSectors;
      }
    }
    ++pSectorBlockConf;
  } while (--NumSectorBlocksConf != 0u);
  if (NumSectorBlocksUsed == 0u) {
    return 1;                       // Error, Flash size to small for this configuration
  }
  pInst->Device.Inst.NumSectorBlocks = (U8)NumSectorBlocksUsed;
  pInst->NumSectors                  = NumSectorsTotal;
  pInst->StartAddrUsed               = NumBytesSkipped;
  pInst->NumBytes                    = NumBytes;
  return 0;                                   // OK, storage area determined.
}

/*********************************************************************
*
*      _InitDevice
*/
static void _InitDevice(NOR_SFDP_INST * pInst) {
  FS_NOR_SPI_DEVICE * pDevice;

  pDevice = &pInst->Device;
  pDevice->pType->pfInit(&pDevice->Inst);
}

/*********************************************************************
*
*      _SetBusWidth
*
*  Function description
*    Configures the number of data lines for the data transfer.
*
*  Parameters
*    pInst      Driver instance.
*
*  Return value
*    ==0      OK, configured successfully.
*    !=0      An error occurred.
*/
static int _SetBusWidth(NOR_SFDP_INST * pInst) {
  int                 r;
  FS_NOR_SPI_DEVICE * pDevice;

  r = 0;
  pDevice = &pInst->Device;
  if (pDevice->pType->pfSetBusWidth != NULL) {
    r = pDevice->pType->pfSetBusWidth(&pDevice->Inst);
  }
  return r;
}

/*********************************************************************
*
*      _SetNumBytesAddr
*
*  Function description
*    Configures the number of address bytes.
*
*  Parameters
*    pInst      Driver instance.
*
*  Return value
*    ==0    OK, successfully configured.
*    !=0    An error occurred.
*/
static int _SetNumBytesAddr(NOR_SFDP_INST * pInst) {
  int                 r;
  FS_NOR_SPI_DEVICE * pDevice;

  r = 0;
  pDevice = &pInst->Device;
  if (pDevice->pType->pfSetNumBytesAddr != NULL) {
    r = pDevice->pType->pfSetNumBytesAddr(&pDevice->Inst);
  }
  return r;
}

/*********************************************************************
*
*      _RemoveWriteProtection
*/
static int _RemoveWriteProtection(NOR_SFDP_INST * pInst, U32 StartAddr, U32 NumBytes) {
  int                 r;
  FS_NOR_SPI_DEVICE * pDevice;

  r = 0;
  pDevice = &pInst->Device;
  if (pDevice->pType->pfRemoveWriteProtection != NULL) {
    r = pDevice->pType->pfRemoveWriteProtection(&pDevice->Inst, StartAddr, NumBytes);
  }
  return r;
}

/*********************************************************************
*
*      _WritePage
*/
static int _WritePage(NOR_SFDP_INST * pInst, U32 Addr, const U8 * pData, U32 NumBytes) {
  int                 r;
  FS_NOR_SPI_DEVICE * pDevice;

  pDevice = &pInst->Device;
  r = pDevice->pType->pfWritePage(&pDevice->Inst, Addr, pData, NumBytes);
  return r;
}

/*********************************************************************
*
*      _WaitForEndOfOperation
*/
static int _WaitForEndOfOperation(NOR_SFDP_INST * pInst, const FS_NOR_SPI_POLL_PARA * pPollPara) {
  int                 r;
  FS_NOR_SPI_DEVICE * pDevice;

  pDevice = &pInst->Device;
  r = pDevice->pType->pfWaitForEndOfOperation(&pDevice->Inst, pPollPara);
  return r;
}

/*********************************************************************
*
*      _ReadApplyParaBySFDP
*/
static int _ReadApplyParaBySFDP(NOR_SFDP_INST * pInst) {
  int                 r;
  FS_NOR_SPI_DEVICE * pDevice;

  r = 0;
  pDevice = &pInst->Device;
  if (pDevice->pType->pfReadApplyPara != NULL) {
    r = pDevice->pType->pfReadApplyPara(&pDevice->Inst);
  }
  return r;
}

/*********************************************************************
*
*      _IdentifyDevice
*
*  Function description
*    Tries to identify manufacturer specific features by using the
*    id information. This includes error reporting flags and settings that
*    are required to work with the NOR flash in quad mode.
*
*  Return value
*    ==0    OK, device identified
*    !=0    Could not identify device
*/
static int _IdentifyDevice(NOR_SFDP_INST * pInst, U8 * pDeviceId, unsigned SizeOfDeviceId) {
  const FS_NOR_SPI_TYPE        *  pDevice;
  const FS_NOR_SPI_TYPE        ** ppDevice;
  const FS_NOR_SPI_DEVICE_LIST *  pDeviceList;
  int                             r;
  unsigned                        NumDevices;
  unsigned                        iDevice;

  pDevice     = NULL;
  pDeviceList = pInst->pDeviceList;
  NumDevices  = pDeviceList->NumDevices;
  ppDevice    = pDeviceList->ppDevice;
  //
  // The information about error flags is not present in the SFDP data.
  // We use the id bytes to determine if the type of the NOR flash connected.
  //
  FS_MEMSET(pDeviceId, 0, SizeOfDeviceId);
  FS_NOR_SPI_ReadId(&pInst->Device.Inst, pDeviceId, SizeOfDeviceId);
  //
  // A value of 0xFF or 0x00 is not a valid manufacturer id and it typically indicates
  // that the device did not respond to read id command.
  //
  if ((*pDeviceId == 0xFFu) || (*pDeviceId == 0x00u)) {
    return 1;                         // Error, could not identify device.
  }
  for (iDevice = 0; iDevice < NumDevices; ++iDevice) {
    pDevice = *ppDevice;
    if (pDevice->pfIdentify == NULL) {
      break;                          // OK, device found.
    }
    r = pDevice->pfIdentify(&pInst->Device.Inst, pDeviceId);
    if (r == 0) {
      break;                          // OK, device found.
    }
    ++ppDevice;
  }
  if (iDevice == NumDevices) {
    return 1;                         // Error, could not identify device.
  }
  pInst->Device.pType = pDevice;
  return 0;
}

/*********************************************************************
*
*      _ApplyParaConf
*
*  Function description
*    Configures the operation according to the user-provided device parameters.
*/
static void _ApplyParaConf(NOR_SFDP_INST * pInst, const U8 * pDeviceId) {
  U8                                  NumParas;
  U8                                  iPara;
  U8                                  MfgId;
  U8                                  Density;
  U8                                  CmdWrite;
  const FS_NOR_SPI_DEVICE_PARA_LIST * pDeviceParaList;
  const FS_NOR_SPI_DEVICE_PARA      * pPara;

  pDeviceParaList = pInst->pDeviceParaList;
  if (pDeviceParaList != NULL) {
    MfgId    = *pDeviceId;
    Density  = *(pDeviceId + 2);
    NumParas = pDeviceParaList->NumParas;
    pPara    = pDeviceParaList->pPara;
    for (iPara = 0; iPara < NumParas; ++iPara) {
      if ((pPara->MfgId == MfgId) && (pPara->Id == Density)) {
        if (pInst->Device.Inst.Allow2bitMode != 0u) {
          CmdWrite = pPara->CmdWrite112;
          if (CmdWrite != 0u) {
            pInst->Device.Inst.CmdWrite      = CmdWrite;
            pInst->Device.Inst.BusWidthWrite = (U16)FS_BUSWIDTH_MAKE(1uL, 1uL, 2uL);
          }
          CmdWrite = pPara->CmdWrite122;
          if (CmdWrite != 0u) {
            pInst->Device.Inst.CmdWrite      = CmdWrite;
            pInst->Device.Inst.BusWidthWrite = (U16)FS_BUSWIDTH_MAKE(1uL, 2uL, 2uL);
          }
        }
        if (pInst->Device.Inst.Allow4bitMode != 0u) {
          CmdWrite = pPara->CmdWrite114;
          if (CmdWrite != 0u) {
            pInst->Device.Inst.CmdWrite      = CmdWrite;
            pInst->Device.Inst.BusWidthWrite = (U16)FS_BUSWIDTH_MAKE(1uL, 1uL, 4uL);
          }
          CmdWrite = pPara->CmdWrite144;
          if (CmdWrite != 0u) {
            pInst->Device.Inst.CmdWrite      = CmdWrite;
            pInst->Device.Inst.BusWidthWrite = (U16)FS_BUSWIDTH_MAKE(1uL, 4uL, 4uL);
          }
        }
        break;
      }
      ++pPara;
    }
  }
}

/*********************************************************************
*
*      _ReleaseFromPowerDown
*
*  Function description
*    Tries to release the NOR device from power down.
*/
static void _ReleaseFromPowerDown(NOR_SFDP_INST * pInst) {
  const FS_NOR_SPI_TYPE        *  pDevice;
  const FS_NOR_SPI_TYPE        ** ppDevice;
  const FS_NOR_SPI_DEVICE_LIST *  pDeviceList;
  unsigned                        NumDevices;
  unsigned                        iDevice;

  pDevice     = NULL;
  pDeviceList = pInst->pDeviceList;
  NumDevices  = pDeviceList->NumDevices;
  ppDevice    = pDeviceList->ppDevice;
  for (iDevice = 0; iDevice < NumDevices; ++iDevice) {
    pDevice = *ppDevice;
    if (pDevice->pfInit != NULL) {
      pDevice->pfInit(&pInst->Device.Inst);
    }
    ++ppDevice;
  }
}

/*********************************************************************
*
*      _Init
*
*  Function description
*    Initializes the HW layer and reads from device the operating parameters.
*/
static int _Init(NOR_SFDP_INST * pInst) {
  U32                    Freq_kHz;
  U32                    NumBytes;
  U32                    StartAddr;
  U8                     Unit;
  int                    r;
  U32                    TimeOutSectorErase;
  U32                    TimeOutPageWrite;
  U32                    DelaySectorErase;
  U32                    Delay1ms;
  U32                    srpms;
  FS_NOR_SPI_POLL_PARA * pPollPara;
  U8                     abDeviceId[3];

  FS_MEMSET(abDeviceId, 0, sizeof(abDeviceId));
  Unit = pInst->Unit;
  ASSERT_HW_TYPE_IS_SET(pInst);
  //
  // Initialize the HW.
  //
  Freq_kHz = (U32)pInst->pHWType->pfInit(Unit);
  if (Freq_kHz == 0u) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SFDP: _Init: Could not initialize HW."));
    return 1;                         // Error, could not identify device
  }
  //
  // The emFile manual wrongly documented that the initialization function of the HW layer
  // has to return the SPI frequency in Hz but all the emFile HW samples returned the frequency in kHz.
  // To stay compatible with these HW layers we try to detect here if the frequency is returned in Hz
  // or kHz by checking if converting the value to Hz will overflow a 32-bit integer. If it overflows
  // we consider the value as being returned in Hz and we converted it to kHz.
  //
  if (Freq_kHz > (0xFFFFFFFFuL / 1000u)) {
    Freq_kHz /= 1000u;
  }
  //
  // Calculate the number of status requests that can be executed in 1 millisecond.
  // At least 16-bits are exchanged on each NOR device status request.
  //
  srpms = ((Freq_kHz * 1000u) >> 4) / 1000u;
  TimeOutSectorErase = srpms * (U32)FS_NOR_TIMEOUT_SECTOR_ERASE;
  TimeOutPageWrite   = srpms * (U32)FS_NOR_TIMEOUT_PAGE_WRITE;
  DelaySectorErase   = srpms * (U32)FS_NOR_DELAY_SECTOR_ERASE;
  Delay1ms           = srpms;
  //
  // Save parameters to phy. layer instance.
  //
  pPollPara = &pInst->PollParaSectorErase;
  FS_MEMSET(pPollPara, 0, sizeof(FS_NOR_SPI_POLL_PARA));
  pPollPara->TimeOut    = TimeOutSectorErase;
  pPollPara->TimeOut_ms = FS_NOR_TIMEOUT_SECTOR_ERASE;
  pPollPara->Delay      = DelaySectorErase;
  pPollPara->Delay_ms   = FS_NOR_DELAY_SECTOR_ERASE;
  pPollPara = &pInst->PollParaPageWrite;
  FS_MEMSET(pPollPara, 0, sizeof(FS_NOR_SPI_POLL_PARA));
  pPollPara->TimeOut    = TimeOutPageWrite;
  pPollPara->TimeOut_ms = FS_NOR_TIMEOUT_PAGE_WRITE;
  pPollPara = &pInst->Device.Inst.PollParaRegWrite;
  FS_MEMSET(pPollPara, 0, sizeof(FS_NOR_SPI_POLL_PARA));
  pPollPara->TimeOut    = TimeOutPageWrite;
  pPollPara->TimeOut_ms = FS_NOR_TIMEOUT_PAGE_WRITE;
  pInst->Delay1ms = Delay1ms;
  //
  // Identify features that are not standardized such as
  // error flags, special settings that have to be
  // performed in order to enable the quad mode, etc.
  //
  r = _IdentifyDevice(pInst, abDeviceId, sizeof(abDeviceId));
  if (r != 0) {
    //
    // Try to release the device from power down.
    //
    _ReleaseFromPowerDown(pInst);
    r = _IdentifyDevice(pInst, abDeviceId, sizeof(abDeviceId));
    if (r != 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SFDP: _Init: Could not identify device."));
      return 1;                       // Error, could not identify device
    }
  }
  //
  // Wait for the last operation before reset to complete.
  //
  (void)_WaitForEndOfOperation(pInst, &pInst->PollParaSectorErase);
  //
  // Wake-up NOR flash if required, clear the write mode flag, etc.
  //
  _InitDevice(pInst);
  //
  // Identify the device parameters. First, try to see if it supports SFDP.
  // If it does not support SFDP we try to identify the parameters by id.
  //
  r = _ReadApplyParaBySFDP(pInst);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SFDP: _Init: Device does not support SFDP."));
    return 1;                         // Error, could not identify device
  }
  //
  // OK, the device is identified. Determine which physical sectors are used as storage.
  //
  r = _CalcStorageArea(pInst);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SFDP: _Init: Could not determine the storage area."));
    return 1;                         // Error
  }
  //
  // Switch to 4-byte address if required.
  //
  r = _SetNumBytesAddr(pInst);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPIFI: _Init: Could not set address mode."));
    return 1;                         // Error
  }
  //
  // Remove the write protection of all physical sectors.
  //
  StartAddr = pInst->StartAddrUsed;
  NumBytes  = pInst->NumBytes;
  r = _RemoveWriteProtection(pInst, StartAddr, NumBytes);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPIFI: _Init: Could not remove write protection."));
    return 1;                         // Error
  }
  //
  // Switch to single, quad or dual mode.
  //
  r = _SetBusWidth(pInst);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPIFI: _Init: Could not configure bus width."));
    return 1;                         // Error
  }
  //
  // Determine the command code and the bus width for the write operation.
  //
  _ApplyParaConf(pInst, abDeviceId);
  pInst->IsInited = 1;
  return 0;                           // OK, device initialized.
}

/*********************************************************************
*
*      _InitIfRequired
*
*/
static int _InitIfRequired(NOR_SFDP_INST * pInst) {
  int r;

  r = 0;        // Set to indicate success.
  if (pInst->IsInited == 0u) {
    r = _Init(pInst);
  }
  return r;
}

/*********************************************************************
*
*      _WritePageAligned
*
*  Function description
*    Writes data to SPI flash and waits for the operation to complete.
*
*  Parameters
*    pInst      Physical layer instance.
*    Addr       NOR flash address to write to.
*    pData      [IN] Data to be written.
*    NumBytes   Number of bytes to be written.
*
*  Return value
*    ==0    OK, data was written.
*    !=0    An error occurred.
*
*  Additional information
*    This function performs more than one write operation is the data
*    is not aligned to and is not a multiple of the minimum number
*    of bytes that can be written to NOR flash device.
*/
static int _WritePageAligned(NOR_SFDP_INST * pInst, U32 Addr, const U8 * pData, U32 NumBytes) {
  int                    r;
  int                    IsDualDeviceMode;
  FS_NOR_SPI_POLL_PARA * pPollPara;
  U8                     abData[2];
  U32                    NumBytesToWrite;

  pPollPara        = &pInst->PollParaPageWrite;
  IsDualDeviceMode = (int)pInst->Device.Inst.IsDualDeviceMode;
  if (IsDualDeviceMode == 0) {
    r = _WritePage(pInst, Addr, pData, NumBytes);
    if (r == 0) {
      r = _WaitForEndOfOperation(pInst, pPollPara);
    }
  } else {
    //
    // Handle leading unaligned bytes.
    //
    r = 0;
    if ((Addr & 1u) != 0u) {
      //
      // Align the data to be written.
      //
      abData[0] = 0xFF;         // Do not modify already existing data.
      abData[1] = *pData;
      r = _WritePage(pInst, Addr - 1u, abData, sizeof(abData));
      if (r == 0) {
        r = _WaitForEndOfOperation(pInst, pPollPara);
      }
      ++Addr;
      --NumBytes;
      ++pData;
    }
    //
    // Handle aligned bytes.
    //
    if (r == 0) {
      NumBytesToWrite  = NumBytes;
      NumBytesToWrite &= ~1u;
      if (NumBytesToWrite != 0u) {
        r = _WritePage(pInst, Addr, pData, NumBytesToWrite);
        if (r == 0) {
          r = _WaitForEndOfOperation(pInst, pPollPara);
        }
        Addr     += NumBytesToWrite;
        NumBytes -= NumBytesToWrite;
        pData    += NumBytesToWrite;
      }
      //
      // Handle unaligned trailing bytes.
      //
      if (r == 0) {
        if (NumBytes != 0u) {
          abData[0] = *pData;
          abData[1] = 0xFF;         // Do not modify already existing data.
          r = _WritePage(pInst, Addr, abData, sizeof(abData));
          if (r == 0) {
            r = _WaitForEndOfOperation(pInst, pPollPara);
          }
        }
      }
    }
  }
  return r;
}

/*********************************************************************
*
*      _WriteOff
*
*  Function description
*    Writes data to SPI flash.
*
*  Parameters
*    pInst      Physical layer instance.
*    Off        Bytes offset of the written data.
*    pData      [IN] Data to be written.
*    NumBytes   Number of bytes to be written.
*
*  Return value
*    ==0    OK, data was written.
*    !=0    An error occurred.
*
*  Additional information
*    This function performs more than one write operation is the data
*    crosses a NOR page boundary.
*/
static int _WriteOff(NOR_SFDP_INST * pInst, U32 Off, const U8 * pData, U32 NumBytes) {
  U32 NumBytesToWrite;
  int r;
  U32 BytesPerPage;
  U32 Addr;

  Addr      = pInst->StartAddrUsed + Off;
  BytesPerPage = FS_NOR_BYTES_PER_PAGE;
  if ((Addr & (BytesPerPage - 1u)) != 0u) {
    NumBytesToWrite = BytesPerPage - (Addr & (BytesPerPage - 1u));
    NumBytesToWrite = SEGGER_MIN(NumBytesToWrite, NumBytes);
    r = _WritePageAligned(pInst, Addr, pData, NumBytesToWrite);
    if (r != 0) {
      return 1;           // Error, write failed.
    }
    pData    += NumBytesToWrite;
    NumBytes -= NumBytesToWrite;
    Addr     += (U32)NumBytesToWrite;
  }
  if (NumBytes > 0u) {
    do {
      NumBytesToWrite = SEGGER_MIN(NumBytes, BytesPerPage);
      r = _WritePageAligned(pInst, Addr, pData, NumBytesToWrite);
      if (r != 0) {
        return 1;         // Error, write failed.
      }
      pData    += NumBytesToWrite;
      NumBytes -= NumBytesToWrite;
      Addr     += (U32)NumBytesToWrite;
    } while (NumBytes != 0u);
  }
  return 0;               // OK, data written successfully.
}

/*********************************************************************
*
*      _EraseSector
*
*  Function description
*    Sets all the bytes of a physical sector to 0xFF.
*
*  Return value
*    ==0    O.K., sector erased
*    !=0    An error occurred
*/
static int _EraseSector(NOR_SFDP_INST * pInst, U32 SectorIndex) {
  int                    r;
  U32                    SectorOff;
  FS_NOR_SPI_DEVICE    * pDevice;
  U8                     Cmd;
  FS_NOR_SPI_POLL_PARA * pPollPara;

  //
  // Fill local variables.
  //
  pDevice   = &pInst->Device;
  pPollPara = &pInst->PollParaSectorErase;
  //
  // Compute the start address of the physical sector.
  //
  SectorOff  = FS_NOR_SPI_GetSectorOff(&pInst->Device.Inst, SectorIndex);
  Cmd        = FS_NOR_SPI_GetSectorEraseCmd(&pInst->Device.Inst, SectorIndex);
  SectorOff += pInst->StartAddrUsed;
  //
  // Erase the physical sector.
  //
  r = pDevice->pType->pfEraseSector(&pDevice->Inst, Cmd, SectorOff);
  if (r == 0) {
    r = _WaitForEndOfOperation(pInst, pPollPara);
  }
  return r;
}

/*********************************************************************
*
*      _AllocInstIfRequired
*
*  Function description
*    Allocates memory for the instance of a physical layer.
*/
static NOR_SFDP_INST * _AllocInstIfRequired(U8 Unit) {
  NOR_SFDP_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  pInst = NULL;
  if (Unit < (U8)FS_NOR_NUM_UNITS) {
    pInst = _apInst[Unit];
    if (pInst == NULL) {
      pInst = SEGGER_PTR2PTR(NOR_SFDP_INST, FS_ALLOC_ZEROED((I32)sizeof(NOR_SFDP_INST), "NOR_SFDP_INST"));
      if (pInst != NULL) {
        pInst->Unit                 = Unit;
        pInst->Device.Inst.pCmd     = &_Cmd;
        pInst->Device.Inst.pContext = SEGGER_PTR2PTR(void, pInst);
        pInst->pDeviceList          = FS_NOR_DEVICE_LIST_DEFAULT;
        pInst->Device.Inst.BusWidth = (U16)FS_BUSWIDTH_MAKE(1uL, 1uL, 1uL);     // By default, all the operations are executed in single SPI  mode.
        _apInst[Unit]               = pInst;
      }
    }
  }
  return pInst;
}

/*********************************************************************
*
*      _GetInst
*/
static NOR_SFDP_INST * _GetInst(U8 Unit) {
  NOR_SFDP_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  pInst = NULL;
  if (Unit < (U8)FS_NOR_NUM_UNITS) {
    pInst = _apInst[Unit];
  }
  return pInst;
}

/*********************************************************************
*
*       Static code (public via callback)
*
**********************************************************************
*/

/*********************************************************************
*
*       _PHY_WriteOff
*
*  Function description
*    Physical layer function.
*    This routine writes data into any section of the flash. It does not
*    check if this section has been previously erased; this is in the responsibility of the user program.
*    Data written into multiple sectors at a time can be handled by this routine.
*
*  Return value
*    ==0    O.K., data has been written
*    !=0    An error occurred
*/
static int _PHY_WriteOff(U8 Unit, U32 Off, const void * pData, U32 NumBytes) {
  int             r;
  NOR_SFDP_INST * pInst;
  const U8      * pData8;

  r     = 1;                  // Set to indicate an error.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    _Lock(pInst);
    r = _InitIfRequired(pInst);
    if (r == 0) {
      if (NumBytes != 0u) {
        pData8 = SEGGER_CONSTPTR2PTR(const U8, pData);
        //
        // Skip leading bytes set to 0xFF.
        //
        for (;;) {
          if (*pData8 != 0xFFu) {
            break;
          }
          ++pData8;
          ++Off;
          if (--NumBytes == 0u) {
            break;
          }
        }
        if (NumBytes != 0u) {
          //
          // Skip trailing bytes set to 0xFF.
          //
          for (;;) {
            if (*(pData8 + (NumBytes - 1u)) != 0xFFu) {
              break;
            }
            if (--NumBytes == 0u) {
              break;
            }
          }
          if (NumBytes != 0u) {
            r = _WriteOff(pInst, Off, pData8, NumBytes);
          }
        }
      }
    }
    _Unlock(pInst);
  }
  return r;
}

/*********************************************************************
*
*       _PHY_ReadOff
*
*  Function description
*    Physical layer function. Reads data from the given offset of the flash.
*/
static int _PHY_ReadOff(U8 Unit, void * pData, U32 Off, U32 NumBytes) {
  U32             Addr;
  int             r;
  U32             NumBytesAddr;
  U32             NumBytesDummy;
  U8              Cmd;
  U8              Dummy;
  U8              aAddr[4];
  U8              BusWidthCmd;
  U8              BusWidthAddr;
  U8              BusWidthData;
  NOR_SFDP_INST * pInst;
  int             Result;

  r     = 1;                  // Set to indicate an error.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    _Lock(pInst);
    r = _InitIfRequired(pInst);
    if (r == 0) {
      Addr = pInst->StartAddrUsed + Off;
      NumBytesAddr  = 0;
      Dummy         = 0xFF;
      NumBytesDummy = pInst->Device.Inst.NumBytesReadDummy;
      BusWidthCmd   = (U8)FS_BUSWIDTH_GET_CMD(pInst->Device.Inst.BusWidthRead);
      BusWidthAddr  = (U8)FS_BUSWIDTH_GET_ADDR(pInst->Device.Inst.BusWidthRead);
      BusWidthData  = (U8)FS_BUSWIDTH_GET_DATA(pInst->Device.Inst.BusWidthRead);
      Cmd           = pInst->Device.Inst.CmdRead;
      if (pInst->Device.Inst.NumBytesAddr == 4u) {
        aAddr[NumBytesAddr++] = (U8)(Addr >> 24);
      }
      aAddr[NumBytesAddr++]   = (U8)(Addr >> 16);
      aAddr[NumBytesAddr++]   = (U8)(Addr >>  8);
      aAddr[NumBytesAddr++]   = (U8)Addr;
      _EnableCS(pInst);
      Result = _Write(pInst, &Cmd, sizeof(Cmd), BusWidthCmd);
      r = (Result != 0) ? Result : r;
      Result = _Write(pInst, aAddr, NumBytesAddr, BusWidthAddr);
      r = (Result != 0) ? Result : r;
      if (NumBytesDummy != 0u) {
        do {
          Result = _Write(pInst, &Dummy, 1, BusWidthAddr);
          r = (Result != 0) ? Result : r;
        } while (--NumBytesDummy != 0u);
      }
      Result = _Read(pInst, SEGGER_PTR2PTR(U8, pData), NumBytes, BusWidthData);
      r = (Result != 0) ? Result : r;
      _DisableCS(pInst);
    }
    _Unlock(pInst);
  }
  return r;
}

/*********************************************************************
*
*       _PHY_EraseSector
*
*  Function description
*    Physical layer function. Erases one physical sector.
*
*  Return value
*    ==0    O.K., sector has been erased
*    !=0    An error occurred
*/
static int _PHY_EraseSector(U8 Unit, unsigned SectorIndex) {
  int             r;
  NOR_SFDP_INST * pInst;

  r     = 1;                    // Set to indicate an error.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    _Lock(pInst);
    r = _InitIfRequired(pInst);
    if (r == 0) {
      //
      // Erase the physical sector.
      //
      ASSERT_SECTOR_INDEX_IS_IN_RANGE(pInst, SectorIndex);
      r = _EraseSector(pInst, SectorIndex);
    }
    _Unlock(pInst);
  }
  return r;
}

/*********************************************************************
*
*       _PHY_GetSectorInfo
*
*  Function description
*    Physical layer function. Returns the offset and length of the given physical sector.
*/
static void _PHY_GetSectorInfo(U8 Unit, unsigned SectorIndex, U32 * pOff, U32 * pLen) {
  int             r;
  U32             SectorOff;
  U32             BytesPerSector;
  NOR_SFDP_INST * pInst;

  SectorOff      = 0;
  BytesPerSector = 0;
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    _Lock(pInst);
    r = _InitIfRequired(pInst);
    if (r == 0) {
      ASSERT_SECTOR_INDEX_IS_IN_RANGE(pInst, SectorIndex);
      SectorOff      = FS_NOR_SPI_GetSectorOff(&pInst->Device.Inst, SectorIndex);
      BytesPerSector = FS_NOR_SPI_GetSectorSize(&pInst->Device.Inst, SectorIndex);
    }
    _Unlock(pInst);
  }
  if (pOff != NULL) {
    *pOff = SectorOff;
  }
  if (pLen != NULL) {
    *pLen = BytesPerSector;
  }
}

/*********************************************************************
*
*       _PHY_GetNumSectors
*
*  Function description
*    Physical layer function. Returns the number total number of physical sectors in the SPI flash.
*/
static int _PHY_GetNumSectors(U8 Unit) {
  int             r;
  int             NumSectors;
  NOR_SFDP_INST * pInst;

  NumSectors = 0;               // Set to indicate an error.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    _Lock(pInst);
    r = _InitIfRequired(pInst);
    if (r == 0) {
      NumSectors = (int)pInst->NumSectors;
    }
    _Unlock(pInst);
  }
  return NumSectors;
}

/*********************************************************************
*
*       _PHY_Configure
*
*  Function description
*    Physical layer function. Configures a single instance of the driver.
*/
static void _PHY_Configure(U8 Unit, U32 BaseAddr, U32 StartAddr, U32 NumBytes) {
  NOR_SFDP_INST * pInst;

  FS_DEBUG_ASSERT(FS_MTYPE_DRIVER, StartAddr >= BaseAddr);
  FS_USE_PARA(BaseAddr);
  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->StartAddrConf = StartAddr;
    pInst->NumBytes      = NumBytes;
    pInst->IsInited      = 0;             // The layer needs to be re-initialized.
  }
}

/*********************************************************************
*
*       _PHY_OnSelectPhy
*
*  Function description
*    Physical layer function. Called right after selection of the physical layer.
*/
static void _PHY_OnSelectPhy(U8 Unit) {
  (void)_AllocInstIfRequired(Unit);
}

/*********************************************************************
*
*       _PHY_DeInit
*
*  Function description
*    Physical layer function.
*    This function frees up memory resources allocated for the instance of a physical layer.
*
*  Parameters
*    Unit  Physical layer number.
*/
static void _PHY_DeInit(U8 Unit) {
#if FS_SUPPORT_DEINIT
  NOR_SFDP_INST * pInst;

  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    FS_Free(SEGGER_PTR2PTR(void, pInst));
    _apInst[Unit] = NULL;
  }
#else
  FS_USE_PARA(Unit);
#endif
}

/*********************************************************************
*
*       _PHY_Init
*
*  Function description
*    Initializes the physical layer.
*
*  Parameters
*    Unit         Index of the physical layer.
*
*  Return value
*    ==0      OK, physical layer initialized.
*    !=0      An error occurred.
*/
static int _PHY_Init(U8 Unit) {
  int             r;
  NOR_SFDP_INST * pInst;

  r     = 1;        // Set to indicate failure.
  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    _Lock(pInst);
    r = _Init(pInst);
    _Unlock(pInst);
  }
  return r;
}

/*********************************************************************
*
*       Public data
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_NOR_PHY_SFDP
*/
const FS_NOR_PHY_TYPE FS_NOR_PHY_SFDP = {
  _PHY_WriteOff,
  _PHY_ReadOff,
  _PHY_EraseSector,
  _PHY_GetSectorInfo,
  _PHY_GetNumSectors,
  _PHY_Configure,
  _PHY_OnSelectPhy,
  _PHY_DeInit,
  NULL,
  _PHY_Init
};

/*********************************************************************
*
*       Public code (for internal use only)
*
**********************************************************************
*/

#if FS_SUPPORT_TEST

/*********************************************************************
*
*       FS__NOR_PHY_SFDP_SetTestHookFailSafe
*/
void FS__NOR_PHY_SFDP_SetTestHookFailSafe(FS_NOR_TEST_HOOK_NOTIFICATION * pfTestHook) {
  _pfTestHookFailSafe = pfTestHook;
}

#endif

/*********************************************************************
*
*      Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_NOR_SFDP_Allow2bitMode
*
*  Function description
*    Specifies if the physical layer is permitted to exchange data
*    via two data lines.
*
*  Parameters
*    Unit       Index of the physical layer (0-based).
*    OnOff      Activation status of the option.
*               * 0   Disable the option.
*               * 1   Enable the option.
*
*  Additional information
*    This function is optional. By default the data is exchanged via one
*    data line (standard SPI). The data transfer via two data lines is
*    used only if this type of data transfer is supported by the serial
*    NOR flash device. In dual mode two bits of data are transferred with
*    each clock period which helps improve the performance. If the serial
*    NOR flash device does not support the dual mode then the data is
*    transferred in standard mode (one data bit per clock period).
*
*    The application is permitted to call this function only at the file
*    system initialization in FS_X_AddDevices().
*/
void FS_NOR_SFDP_Allow2bitMode(U8 Unit, U8 OnOff) {
  NOR_SFDP_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    //
    // Check that the hardware layer implements the function required
    // for the data transfer in dual mode.
    //
    if (OnOff != 0u) {
      if (pInst->pHWType != NULL) {
        if (pInst->pHWType->pfRead_x2 == NULL) {
          OnOff = 0;                // Dual mode is not allowed.
        }
      }
    }
    pInst->Device.Inst.Allow2bitMode = OnOff;
  }
}

/*********************************************************************
*
*       FS_NOR_SFDP_Allow4bitMode
*
*  Function description
*    Specifies if the physical layer is permitted to exchange data
*    via four data lines.
*
*  Parameters
*    Unit       Index of the physical layer (0-based).
*    OnOff      Activation status of the option.
*               * 0   Disable the option.
*               * 1   Enable the option.
*
*  Additional information
*    This function is optional. By default the data is exchanged via one
*    data line (standard SPI). The data transfer via four data lines is
*    used only if this type of data transfer is supported by the serial
*    NOR flash device. In quad mode four bits of data are transferred on
*    each clock period which helps improve the performance. If the serial
*    NOR flash device does not support the quad mode then the data is
*    transferred in dual mode if enabled and supported or in standard
*    mode (one bit per clock period).
*
*    The application is permitted to call this function only at the file
*    system initialization in FS_X_AddDevices().
*/
void FS_NOR_SFDP_Allow4bitMode(U8 Unit, U8 OnOff) {
  NOR_SFDP_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    //
    // Check that the hardware layer implements the function required
    // for the data transfer in quad mode.
    //
    if (OnOff != 0u) {
      if (pInst->pHWType != NULL) {
        if (pInst->pHWType->pfRead_x4 == NULL) {
          OnOff = 0;                // Quad mode is not allowed.
        }
      }
    }
    pInst->Device.Inst.Allow4bitMode = OnOff;
  }
}

/*********************************************************************
*
*       FS_NOR_SFDP_SetHWType
*
*  Function description
*    Configures the HW access routines.
*
*  Parameters
*    Unit           Index of the physical layer (0-based).
*    pHWType        Hardware access routines. Cannot be NULL.
*
*  Additional information
*    It is mandatory to call this function during the file system
*    initialization in FS_X_AddDevices() once for each instance
*    of a physical layer.
*/
void FS_NOR_SFDP_SetHWType(U8 Unit, const FS_NOR_HW_TYPE_SPI * pHWType) {
  NOR_SFDP_INST * pInst;
  U8              OnOff;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->pHWType = pHWType;
    //
    // Check if the data transfer functions for dual mode are defined.
    //
    OnOff = pInst->Device.Inst.Allow2bitMode;
    if (OnOff != 0u) {
      if (pInst->pHWType->pfRead_x2 == NULL) {
        OnOff = 0;                  // Dual mode is not allowed.
      }
    }
    pInst->Device.Inst.Allow2bitMode = OnOff;
    //
    // Check if the data transfer functions for quad mode are defined.
    //
    OnOff = pInst->Device.Inst.Allow4bitMode;
    if (OnOff != 0u) {
      if (pInst->pHWType->pfRead_x4 == NULL) {
        OnOff = 0;                  // Quad mode is not allowed.
      }
    }
    pInst->Device.Inst.Allow4bitMode = OnOff;
  }
}

/*********************************************************************
*
*       FS_NOR_SFDP_SetDeviceList
*
*  Function description
*    Configures the type of handled serial NOR flash devices.
*
*  Parameters
*    Unit         Index of the physical layer (0-based).
*    pDeviceList  List of NOR flash devices the physical layer can handle.
*
*  Additional information
*    This function is optional. This function can be used to enable handling
*    for vendor specific features of serial NOR flash device such as error
*    handling and data protection. By default the physical layer is configured
*    to handle only Micron serial NOR flash devices. Handling for serial NOR
*    flash devices from other manufacturers has to be explicitly enabled via
*    this function.
*
*    Permitted values for the pDeviceList parameter are:
*    +----------------------------------+-------------------------------------------------------------------------------------+
*    | Identifier                       | Description                                                                         |
*    +----------------------------------+-------------------------------------------------------------------------------------+
*    | FS_NOR_SPI_DeviceListAdesto      | Enables handling of Adesto serial NOR flash devices.                                |
*    +----------------------------------+-------------------------------------------------------------------------------------+
*    | FS_NOR_SPI_DeviceListAll         | Enables handling of serial NOR flash devices from all manufacturers.                |
*    +----------------------------------+-------------------------------------------------------------------------------------+
*    | FS_NOR_SPI_DeviceListCypress     | Enables handling of Cypress serial NOR flash devices.                               |
*    +----------------------------------+-------------------------------------------------------------------------------------+
*    | FS_NOR_SPI_DeviceListDefault     | Enables handling of Micron and of SFDP compatible serial NOR flash devices.         |
*    +----------------------------------+-------------------------------------------------------------------------------------+
*    | FS_NOR_SPI_DeviceListEon         | Enables handling of Eon serial NOR flash devices.                                   |
*    +----------------------------------+-------------------------------------------------------------------------------------+
*    | FS_NOR_SPI_DeviceListGigaDevice  | Enables handling of GigaDevice serial NOR flash devices.                            |
*    +----------------------------------+-------------------------------------------------------------------------------------+
*    | FS_NOR_SPI_DeviceListISSI        | Enables handling of ISSI serial NOR flash devices.                                  |
*    +----------------------------------+-------------------------------------------------------------------------------------+
*    | FS_NOR_SPI_DeviceListMacronix    | Enables handling of Macronix serial NOR flash devices.                              |
*    +----------------------------------+-------------------------------------------------------------------------------------+
*    | FS_NOR_SPI_DeviceListMicron      | Enables handling of Micron serial NOR flash devices.                                |
*    +----------------------------------+-------------------------------------------------------------------------------------+
*    | FS_NOR_SPI_DeviceListMicron_x    | Enables handling of Micron serial NOR flash devices in single and dual chip setups. |
*    +----------------------------------+-------------------------------------------------------------------------------------+
*    | FS_NOR_SPI_DeviceListMicron_x2   | Enables handling of Micron serial NOR flash devices in dual chip setups.            |
*    +----------------------------------+-------------------------------------------------------------------------------------+
*    | FS_NOR_SPI_DeviceListMicrochip   | Enables handling of Microchip serial NOR flash devices.                             |
*    +----------------------------------+-------------------------------------------------------------------------------------+
*    | FS_NOR_SPI_DeviceListSpansion    | Enables handling of Spansion serial NOR flash devices.                              |
*    +----------------------------------+-------------------------------------------------------------------------------------+
*    | FS_NOR_SPI_DeviceListWinbond     | Enables handling of Winbond serial NOR flash devices.                               |
*    +----------------------------------+-------------------------------------------------------------------------------------+
*
*    The application can save ROM space by setting FS_NOR_DEVICE_LIST_DEFAULT
*    to NULL at compile time and by calling at runtime FS_NOR_SFDP_SetDeviceList()
*    with the actual list of serial NOR flash devices that have to be
*    handled.
*
*    The application is permitted to call this function only at the file
*    system initialization in FS_X_AddDevices().
*/
void FS_NOR_SFDP_SetDeviceList(U8 Unit, const FS_NOR_SPI_DEVICE_LIST * pDeviceList) {
  NOR_SFDP_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->pDeviceList = pDeviceList;
  }
}

/*********************************************************************
*
*       FS_NOR_SFDP_SetSectorSize
*
*  Function description
*    Configures the size of the physical sector to be used by the driver.
*
*  Parameters
*    Unit             Index of the physical layer (0-based).
*    BytesPerSector   Sector size to be used.
*
*  Additional information
*    Typically, a serial NOR flash device supports erase commands
*    that can be used to erase sectors of different sizes (4 KB, 32 KB, etc.)
*    For performance reasons the physical layer chooses always the
*    erase command corresponding to the largest physical sector.
*    This function can be used to request the physical layer to use
*    a different (smaller) physical sector size. The mount operation fails
*    if the serial NOR flash device does not support the specified
*    physical sector size.
*
*    The application is permitted to call this function only at the file
*    system initialization in FS_X_AddDevices().
*/
void FS_NOR_SFDP_SetSectorSize(U8 Unit, U32 BytesPerSector) {
  NOR_SFDP_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->Device.Inst.ldBytesPerSector = (U8)_ld(BytesPerSector);
  }
}

/*********************************************************************
*
*       FS_NOR_SFDP_SetDeviceParaList
*
*  Function description
*    Configures parameters of serial NOR flash devices.
*
*  Parameters
*    Unit             Index of the physical layer (0-based).
*    pDeviceParaList  List of device parameters.
*
*  Additional information
*    This function is optional. By default, the parameters of the used
*    serial NOR flash device are determined by evaluating the SFDP
*    tables stored in it. However, the information about the commands
*    that can be used to write the data via two and four data lines
*    is not stored to this parameters. FS_NOR_SFDP_SetDeviceParaList()
*    can be used to specify this information. The parameters are matched
*    by comparing the first byte (manufacturer id) and the third byte
*    (device id) of the information returned by the READ ID (0x9F) function
*    with the MfgId and Id members of the FS_NOR_SPI_DEVICE_PARA structure.
*
*    The application is permitted to call this function only at the file
*    system initialization in FS_X_AddDevices().
*/
void FS_NOR_SFDP_SetDeviceParaList(U8 Unit, const FS_NOR_SPI_DEVICE_PARA_LIST * pDeviceParaList) {
  NOR_SFDP_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->pDeviceParaList = pDeviceParaList;
  }
}

/*********************************************************************
*
*       FS_NOR_SFDP_Configure
*
*  Function description
*    Configures an instance of the physical layer.
*
*  Parameters
*    Unit           Index of the SFDP physical layer (0-based).
*    StartAddr      Address of the first byte the SFDP physical layer
*                   is permitted to use as storage.
*    NumBytes       Number of bytes starting from StartAddr available to
*                   be used by the SFDP physical layer as storage.
*
*  Additional information
*    This function is optional. It can be called instead of the FS_NOR_BM_Configure()
*    or FS_NOR_Configure(). Different instances of the SFDP physical layer
*    are identified by the Unit parameter.
*
*    BaseAddr is used only for NOR flash devices that are memory mapped.
*    For serial NOR flash devices that are not memory mapped BaseAddr
*    has to be set to 0.
*
*    StartAddr has to be greater than or equal to BaseAddr and smaller
*    than the total number of bytes in the NOR flash device. The SFDP physical
*    layer rounds up StartAddr to the start address of the next physical
*    sector in the NOR flash device.
*
*    NumBytes is rounded up to a physical sector boundary if the memory
*    range defined by StartAddr and NumBytes is smaller than the capacity
*    of the NOR flash device. If the memory range defined by StartAddr
*    and NumBytes is larger than the capacity of the NOR flash device
*    than NumBytes is rounded down so that the memory range fits into
*    the NOR flash device.
*/
void FS_NOR_SFDP_Configure(U8 Unit, U32 StartAddr, U32 NumBytes) {
  NOR_SFDP_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->StartAddrConf = StartAddr;
    pInst->NumBytes      = NumBytes;
  }
}

/*************************** End of file ****************************/
