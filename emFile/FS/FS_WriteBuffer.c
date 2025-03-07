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
File        : FS_WriteBuffer.c
Purpose     : FIFO for sector write operations.
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#include "FS_Int.h"

/*********************************************************************
*
*       Defines, fixed
*
**********************************************************************
*/
#if FS_WRBUF_ENABLE_STATS
  #define IF_STATS(Exp) Exp
#else
  #define IF_STATS(Exp)
#endif

/*********************************************************************
*
*       ASSERT_UNIT_NO_IS_IN_RANGE
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)                                 \
    if ((Unit) >= (U8)FS_WRBUF_NUM_UNITS) {                                \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "WRBUF: Invalid unit number.")); \
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);                                 \
    }
#else
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)
#endif

/*********************************************************************
*
*       ASSERT_DEVICE_IS_SET
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_DEVICE_IS_SET(pInst)                                    \
  if ((pInst)->pDeviceType == NULL) {                                    \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "WRBUF: Device is not set.")); \
      FS_X_PANIC(FS_ERRCODE_UNKNOWN_DEVICE);                             \
    }
#else
  #define ASSERT_DEVICE_IS_SET(Unit)
#endif

/*********************************************************************
*
*       Local data types
*
**********************************************************************
*/

/*********************************************************************
*
*       WRBUF_INST
*/
typedef struct {
  U8                       Unit;                  // Unit number of this driver
  U8                       DeviceUnit;            // Unit number of the storage driver below this one. pDeviceType indicates the device type.
  U16                      ldBytesPerSector;      // Sector size as power of 2
  U32                      NumSectors;            // Number of sectors on the storage device
  U32                      NumBytesBuffer;        // Number of bytes assigned for the sector buffer
  U32                      NumSectorsList;        // Maximum number of sectors which can be stored in the buffer
  U32                      SectorCnt;             // Number of sectors available in the list of sectors
  FS_WRBUF_SECTOR_INFO   * paSectorInfo;          // Pointer to the array of sector indices
  U8                     * paSectorData;          // Pointer to the array of sector data. Separated from sector info to allow burst read/write operations.
  const FS_DEVICE_TYPE   * pDeviceType;           // Device type of the actual storage below this one.
#if FS_WRBUF_ENABLE_STATS
  FS_WRBUF_STAT_COUNTERS   StatCounters;          // Statistical counters.
#endif
} WRBUF_INST;

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static WRBUF_INST * _apInst[FS_WRBUF_NUM_UNITS];
static U8           _NumUnits = 0;

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _ld
*/
static unsigned _ld(U32 Value) {
  unsigned i;

  for (i = 0; i < 16u; i++) {
    if ((1uL << i) == Value) {
      break;
    }
  }
  return i;
}

/*********************************************************************
*
*       _InitMedium
*
*  Function description
*    Initializes the storage driver.
*/
static int _InitMedium(const WRBUF_INST * pInst) {
  int                    r;
  U8                     DeviceUnit;
  const FS_DEVICE_TYPE * pDeviceType;

  ASSERT_DEVICE_IS_SET(pInst);
  pDeviceType = pInst->pDeviceType;
  DeviceUnit  = pInst->DeviceUnit;
  r = 0;
  if (pDeviceType->pfInitMedium != NULL) {
    r = pDeviceType->pfInitMedium(DeviceUnit);
    if (r != 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "WRBUF: Could not initialize storage."));
    }
  }
  return r;
}

/*********************************************************************
*
*       _GetDeviceInfo
*
*  Function description
*    Reads information about the storage device of primary volume.
*/
static int _GetDeviceInfo(const WRBUF_INST * pInst, FS_DEV_INFO * pDeviceInfo) {
  int                    r;
  U8                     DeviceUnit;
  const FS_DEVICE_TYPE * pDeviceType;

  ASSERT_DEVICE_IS_SET(pInst);
  pDeviceType = pInst->pDeviceType;
  DeviceUnit  = pInst->DeviceUnit;
  r = pDeviceType->pfIoCtl(DeviceUnit, FS_CMD_GET_DEVINFO, 0, SEGGER_PTR2PTR(void, pDeviceInfo));
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "WRBUF: Could not get info from storage."));
  }
  return r;
}

/*********************************************************************
*
*       _IoCtl
*
*  Function description
*    Executes an I/O control command on the storage device.
*/
static int _IoCtl(const WRBUF_INST * pInst, I32 Cmd, I32 Aux, void * pBuffer) {
  int                    r;
  U8                     DeviceUnit;
  const FS_DEVICE_TYPE * pDeviceType;

  // cppcheck-suppress nullPointer
  ASSERT_DEVICE_IS_SET(pInst);
  // cppcheck-suppress nullPointer
  pDeviceType  = pInst->pDeviceType;
  // cppcheck-suppress nullPointer
  DeviceUnit   = pInst->DeviceUnit;
  r = pDeviceType->pfIoCtl(DeviceUnit, Cmd, Aux, pBuffer);
  return r;
}

/*********************************************************************
*
*       _ReadSectors
*
*  Function description
*    Reads the contents of the specified sectors from storage device.
*/
static int _ReadSectors(const WRBUF_INST * pInst, U32 SectorIndex, void * pBuffer, U32 NumSectors) {
  int                    r;
  U8                     DeviceUnit;
  const FS_DEVICE_TYPE * pDeviceType;

  ASSERT_DEVICE_IS_SET(pInst);
  pDeviceType = pInst->pDeviceType;
  DeviceUnit  = pInst->DeviceUnit;
  r = pDeviceType->pfRead(DeviceUnit, SectorIndex, pBuffer, NumSectors);
  return r;
}

/*********************************************************************
*
*       _FreeSectors
*
*  Function description
*    Informs the storage driver about unused sectors.
*/
static int _FreeSectors(WRBUF_INST * pInst, U32 SectorIndex, U32 NumSectors) {          //lint -efunc(818, _FreeSectors) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory]. Rationale: pInst cannot be declared as pointing to const because we need to be able to update the statistical counters in debug builds.
  IF_STATS(pInst->StatCounters.FreeOperationCnt++);
  IF_STATS(pInst->StatCounters.FreeSectorCnt += NumSectors);
  // TBD: Evaluate the return value after the device drivers have been updated to return meaningful error codes.
  (void)_IoCtl(pInst, FS_CMD_FREE_SECTORS, (I32)SectorIndex, SEGGER_PTR2PTR(void, &NumSectors));
  return 0;
}

/*********************************************************************
*
*       _ReadApplyDeviceInfo
*
*  Function description
*    Reads information from the storage devices and computes the driver parameters.
*/
static int _ReadApplyDeviceInfo(WRBUF_INST * pInst) {
  int         r;
  U16         BytesPerSector;
  U32         NumSectors;
  U32         NumSectorsList;
  U32         NumBytesBuffer;
  FS_DEV_INFO DeviceInfo;

  //
  // Read the information about the storage device of primary volume.
  //
  r = _GetDeviceInfo(pInst, &DeviceInfo);
  if (r == 0) {
    BytesPerSector   = DeviceInfo.BytesPerSector;
    NumSectors       = DeviceInfo.NumSectors;
    NumBytesBuffer   = pInst->NumBytesBuffer;
    //
    // Compute the maximum number of sectors which can be stored to buffer.
    //
    NumSectorsList = NumBytesBuffer / (sizeof(FS_WRBUF_SECTOR_INFO) + BytesPerSector);
    //
    // Save information to instance structure.
    //
    pInst->ldBytesPerSector = (U16)_ld(BytesPerSector);
    pInst->NumSectors       = NumSectors;
    pInst->NumSectorsList   = NumSectorsList;
    pInst->SectorCnt        = 0;
    pInst->paSectorData     = SEGGER_PTR2PTR(U8, &pInst->paSectorInfo[NumSectorsList]);
  }
  return r;
}

/*********************************************************************
*
*       _ReadApplyDeviceInfoIfRequired
*
*  Function description
*    Reads information from the storage devices and computes the
*    driver parameters if not already done.
*/
static int _ReadApplyDeviceInfoIfRequired(WRBUF_INST * pInst) {
  int r;

  r = 0;
  if (pInst->NumSectors == 0u) {
    r = _ReadApplyDeviceInfo(pInst);
  }
  return r;
}

/*********************************************************************
*
*       _GetStatus
*
*  Function description
*    Returns information about if the storage device is present.
*/
static int _GetStatus(const WRBUF_INST * pInst) {
  int                    Status;
  U8                     DeviceUnit;
  const FS_DEVICE_TYPE * pDeviceType;

  Status = FS_MEDIA_NOT_PRESENT;
  pDeviceType = pInst->pDeviceType;
  if (pDeviceType != NULL) {
    DeviceUnit = pInst->DeviceUnit;
    Status = pDeviceType->pfGetStatus(DeviceUnit);
  }
  return Status;
}

/*********************************************************************
*
*       _WriteSectors
*
*  Function description
*    Writes the contents of the specified sectors to storage device.
*/
static int _WriteSectors(WRBUF_INST * pInst, U32 SectorIndex, const void * pBuffer, U32 NumSectors, U8 RepeatSame) {      //lint -efunc(818, _WriteSectors) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory]. Rationale: pInst cannot be declared as pointing to const because we need to be able to update the statistical counters in debug builds.
  int                    r;
  U8                     DeviceUnit;
  const FS_DEVICE_TYPE * pDeviceType;

  ASSERT_DEVICE_IS_SET(pInst);
  IF_STATS(pInst->StatCounters.WriteOperationCnt++);
  IF_STATS(pInst->StatCounters.WriteSectorCnt += NumSectors);
  pDeviceType = pInst->pDeviceType;
  DeviceUnit  = pInst->DeviceUnit;
  r = pDeviceType->pfWrite(DeviceUnit, SectorIndex, pBuffer, NumSectors, RepeatSame);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "WRBUF: Could not write sectors to storage."));
  }
  return r;
}

/*********************************************************************
*
*       _AddToSectorList
*
*  Function description
*    Stores a sector at the end of list.
*
*  Return value
*    ==0    Sector data added to end of list
*    !=0    List is full, sector not added
*/
static int _AddToSectorList(WRBUF_INST * pInst, const FS_WRBUF_SECTOR_INFO * pSectorInfo, const U8 * pSectorData) {
  U32                    NumSectorsList;
  U32                    SectorCnt;
  U16                    ldBytesPerSector;
  U32                    BytesPerSector;
  U32                    ByteOff;
  FS_WRBUF_SECTOR_INFO * pSectorInfoList;

  NumSectorsList   = pInst->NumSectorsList;
  SectorCnt        = pInst->SectorCnt;
  ldBytesPerSector = pInst->ldBytesPerSector;
  BytesPerSector   = 1uL << ldBytesPerSector;
  if (SectorCnt >= NumSectorsList) {
    return 1;         // Error, the list is full.
  }
  //
  // Store the sector index.
  //
  pSectorInfoList  = &pInst->paSectorInfo[SectorCnt];
  *pSectorInfoList = *pSectorInfo;        // struct copy
  //
  // Store the sector data.
  //
  if (pSectorData != NULL) {
    ByteOff = SectorCnt << ldBytesPerSector;
    if (pInst->paSectorData == NULL) {
      return 1;       // Error, sector buffer not initialized.
    }
    FS_MEMCPY(&pInst->paSectorData[ByteOff], pSectorData, BytesPerSector);
  }
  //
  // Update the number of sectors written.
  //
  ++SectorCnt;
  pInst->SectorCnt = SectorCnt;
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "WBUF: ADD SectorIndex: %lu, IsValid: %d, SectorCnt: %lu\n", pSectorInfo->SectorIndex, pSectorInfo->IsValid, SectorCnt));
  return 0;           // OK, sector added to end of list.
}

/*********************************************************************
*
*       _GetLastFromSectorList
*
*  Function description
*    Returns the index and the data of the last sector stored to sector list.
*
*  Return value
*    ==NULL   List is empty
*    !=NULL   Sector data of the last entry in the list
*/
static U8 * _GetLastFromSectorList(const WRBUF_INST * pInst, FS_WRBUF_SECTOR_INFO ** ppSectorInfo) {
  U32                    SectorCnt;
  U16                    ldBytesPerSector;
  U32                    ByteOff;
  FS_WRBUF_SECTOR_INFO * pSectorInfoLast;
  U8                   * pSectorData;

  SectorCnt        = pInst->SectorCnt;
  ldBytesPerSector = pInst->ldBytesPerSector;
  if (SectorCnt == 0u) {
    return NULL;                        // The sector list is empty.
  }
  pSectorInfoLast = &pInst->paSectorInfo[SectorCnt - 1u];
  //
  // Get the sector data.
  //
  ByteOff = (SectorCnt - 1u) << ldBytesPerSector;
  pSectorData = &pInst->paSectorData[ByteOff];
  //
  // Return the sector info.
  //
  if (ppSectorInfo != NULL) {
    *ppSectorInfo = pSectorInfoLast;
  }
  return pSectorData;                   // Last sector found.
}

/*********************************************************************
*
*       _FindInSectorList
*
*  Function description
*    Searches for a sector with a specified index. If more sectors
*    with the same index are found the data of the most recent sector
*    is returned.
*
*  Parameters
*    pInst          Driver instance.
*    SectorIndex    Index of the sector to search for.
*    ppSectorInfo   [OUT] Information about the found sector.
*
*  Return value
*    ==NULL   Sector not found.
*    !=NULL   Data of the found sector.
*/
static U8 * _FindInSectorList(const WRBUF_INST * pInst, U32 SectorIndex, FS_WRBUF_SECTOR_INFO ** ppSectorInfo) {
  U32                    SectorCnt;
  U32                    ByteOff;
  unsigned               ldBytesPerSector;
  FS_WRBUF_SECTOR_INFO * pSectorInfoIter;
  U8                   * pSectorData;
  U32                    BytesPerSector;

  SectorCnt        = pInst->SectorCnt;
  ldBytesPerSector = pInst->ldBytesPerSector;
  BytesPerSector   = 1uL << ldBytesPerSector;
  if (SectorCnt == 0u) {
    return NULL;                    // The sector list is empty.
  }
  --SectorCnt;
  pSectorInfoIter = &pInst->paSectorInfo[SectorCnt];
  ByteOff = SectorCnt << ldBytesPerSector;
  pSectorData = &pInst->paSectorData[ByteOff];
  do {
    if (pSectorInfoIter->SectorIndex == SectorIndex) {
      if (ppSectorInfo != NULL) {
        *ppSectorInfo = pSectorInfoIter;
      }
      return pSectorData;           // Sector found.
    }
    --pSectorInfoIter;
    pSectorData -= BytesPerSector;
  } while (SectorCnt-- != 0u);
  return NULL;                      // Sector not found.
}

/*********************************************************************
*
*       _CleanSectorList
*
*  Function description
*    Removes from the beginning of list the specified number of
*    sectors and writes them to storage.
*
*  Parameters
*    pInst        Driver instance.
*
*  Return value
*    ==0      OK, sectors written to storage.
*    !=0      Error, could not write to storage.
*/
static int _CleanSectorList(WRBUF_INST * pInst) {
  U32                    SectorCnt;
  U32                    SectorOff;
  U32                    NumSectorsAtOnce;
  U32                    ByteOff;
  U32                    StartSector;
  U32                    SectorIndex;
  U32                    SectorIndexPrev;
  U16                    ldBytesPerSector;
  int                    r;
  unsigned               IsValid;
  unsigned               IsValidPrev;
  U8                   * pDataStart;
  FS_WRBUF_SECTOR_INFO * pSectorInfo;

  r = 0;
  //
  // Do nothing if the list is empty.
  //
  SectorCnt = pInst->SectorCnt;
  if (SectorCnt == 0u) {
    return 0;
  }
  //
  // Prepare local variables.
  //
  ldBytesPerSector  = pInst->ldBytesPerSector;
  pSectorInfo       = &pInst->paSectorInfo[0];
  SectorIndex       = pSectorInfo->SectorIndex;
  IsValid           = pSectorInfo->IsValid;
  SectorIndexPrev   = SECTOR_INDEX_INVALID;
  IsValidPrev       = 0;
  StartSector       = SectorIndex;
  ByteOff           = 0;
  pDataStart        = &pInst->paSectorData[ByteOff];
  NumSectorsAtOnce  = 1;
  SectorOff         = 0;
  //
  // Read sectors from list and store them to storage.
  //
  for (;;) {
    if (SectorIndexPrev != SECTOR_INDEX_INVALID) {
      if (   (SectorIndex == (SectorIndexPrev + 1u))
          && (IsValid     == IsValidPrev)) {
        //
        // Consecutive sector index. Count it.
        //
        if (NumSectorsAtOnce == 0u) {
          //
          // First sector in burst. Save the sector index and a pointer to the beginning of sector data.
          //
          StartSector = SectorIndex;
          ByteOff     = SectorOff << ldBytesPerSector;
          pDataStart  = &pInst->paSectorData[ByteOff];
        }
        ++NumSectorsAtOnce;
      } else {
        //
        // Non-consecutive sector index. Write to storage.
        //
        if (NumSectorsAtOnce != 0u) {
          if (IsValidPrev != 0u) {
            r = _WriteSectors(pInst, StartSector, pDataStart, NumSectorsAtOnce, 0);
          } else {
            r = _FreeSectors(pInst, StartSector, NumSectorsAtOnce);
          }
          if (r != 0) {
            break;
          }
          NumSectorsAtOnce = 0;
        }
        //
        // First sector in burst. Save the sector index and a pointer to the beginning of sector data.
        //
        StartSector = SectorIndex;
        ByteOff     = SectorOff << ldBytesPerSector;
        pDataStart  = &pInst->paSectorData[ByteOff];
        ++NumSectorsAtOnce;
      }
    }
    ++SectorOff;
    if (SectorOff >= SectorCnt) {
      break;
    }
    SectorIndexPrev = SectorIndex;
    IsValidPrev     = IsValid;
    //
    // Get the info of the next sector in the list.
    //
    pSectorInfo = &pInst->paSectorInfo[SectorOff];
    SectorIndex = pSectorInfo->SectorIndex;
    IsValid     = pSectorInfo->IsValid;
  }
  //
  // Update the number of sectors left in the list.
  //
  pInst->SectorCnt = 0;
  if (r == 0) {
    //
    // Write the remaining sectors to storage.
    //
    if (NumSectorsAtOnce != 0u) {
      if (IsValid != 0u) {
        r = _WriteSectors(pInst, StartSector, pDataStart, NumSectorsAtOnce, 0);
      } else {
        r = _FreeSectors(pInst, StartSector, NumSectorsAtOnce);
      }
    }
  }
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "WBUF: CLEAN\n"));
  return r;
}

/*********************************************************************
*
*       _HandleFreeSectors
*/
static int _HandleFreeSectors(WRBUF_INST * pInst, U32 SectorIndex, U32 NumSectors) {
  U32                    NumSectorsList;
  U32                    SectorCnt;
  U32                    NumSectorsFree;
  U32                    NumSectorsAtOnce;
  int                    r;
  U8                   * pSectorDataLast;
  FS_WRBUF_SECTOR_INFO * pSectorInfoLast;
  FS_WRBUF_SECTOR_INFO   SectorInfo;

  NumSectorsList   = pInst->NumSectorsList;
  SectorCnt        = pInst->SectorCnt;
  //
  // Overwrite the last sector in the list if possible.
  //
  pSectorDataLast = _GetLastFromSectorList(pInst, &pSectorInfoLast);
  if (pSectorDataLast != NULL) {
    if (pSectorInfoLast->SectorIndex == SectorIndex) {
      pSectorInfoLast->IsValid = 0;
      ++SectorIndex;
      --NumSectors;
    }
  }
  //
  // If the number of sectors written is greater than the capacity of sector list.
  // store only the last NumSectorsList to sector list. The rest of the sectors is
  // directly freed.
  //
  if (NumSectors > NumSectorsList) {
    NumSectorsAtOnce = NumSectors - NumSectorsList;
    r = _FreeSectors(pInst, SectorIndex, NumSectorsAtOnce);
    if (r != 0) {
      return r;             // Error, could not free sectors.
    }
    SectorIndex += NumSectorsAtOnce;
    NumSectors  -= NumSectorsAtOnce;
  }
  //
  // Make room in the sector list for the new sectors.
  //
  NumSectorsFree = NumSectorsList - SectorCnt;
  if (NumSectorsFree < NumSectors) {
    //
    // Free up entries in the list.
    //
    r = _CleanSectorList(pInst);
    if (r != 0) {
      return r;             // Error, could not clean sector list.
    }
  }
  if (NumSectors != 0u) {
    //
    // Store the sector info to sector list.
    //
    do {
      SectorInfo.IsValid     = 0;
      SectorInfo.SectorIndex = SectorIndex;
      r = _AddToSectorList(pInst, &SectorInfo, NULL);
      if (r != 0) {
        return r;             // Error, could not add to sector list. Should not happen.
      }
      ++SectorIndex;
    } while (--NumSectors != 0u);
  }
  return 0;
}

/*********************************************************************
*
*       _GetInst
*/
static WRBUF_INST * _GetInst(U8 Unit) {
  WRBUF_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  pInst = NULL;
  if (Unit < (U8)FS_WRBUF_NUM_UNITS) {
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
*       _WRBUF_GetDriverName
*
*  Function description
*    FS driver function. Returns the driver name.
*/
static const char * _WRBUF_GetDriverName(U8 Unit) {
  FS_USE_PARA(Unit);
  return "wrbuf";
}

/*********************************************************************
*
*       _WRBUF_AddDevice
*
*  Function description
*    FS driver function. Creates a driver instance.
*/
static int _WRBUF_AddDevice(void) {
  WRBUF_INST * pInst;
  U8           Unit;

  if (_NumUnits >= (U8)FS_WRBUF_NUM_UNITS) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "WRBUF: _WRBUF_AddDevice: Too many instances."));
    return -1;                      // Error, too many instances defined.
  }
  Unit  = _NumUnits;
  pInst = _apInst[Unit];
  if (pInst == NULL) {
    FS_ALLOC_ZEROED_PTR(SEGGER_PTR2PTR(void *, &pInst), (I32)sizeof(WRBUF_INST), "WRBUF_INST");
    if (pInst == NULL) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "WRBUF: _WRBUF_AddDevice: Not enough memory."));
      return -1;                    // Error, could not allocate memory for the instance.
    }
    _apInst[Unit] = pInst;
    pInst->Unit = Unit;
    ++_NumUnits;
  }
  return (int)Unit;                 // OK, instance created.
}

/*********************************************************************
*
*       _WRBUF_Read
*
*  Function description
*    FS driver function. Reads a number of sectors from storage medium.
*/
static int _WRBUF_Read(U8 Unit, U32 SectorIndex, void * pBuffer, U32 NumSectors) {
  int                    r;
  U32                    NumSectorsAtOnce;
  U32                    StartSector;
  U16                    BytesPerSector;
  U8                   * pData8;
  U8                   * pDataStart;
  void                 * pSectorData;
  WRBUF_INST           * pInst;
  FS_WRBUF_SECTOR_INFO * pSectorInfo;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;                 // Error, instance not found.
  }
  NumSectorsAtOnce = 0;
  pData8           = SEGGER_PTR2PTR(U8, pBuffer);
  BytesPerSector   = (U16)(1uL << pInst->ldBytesPerSector);
  StartSector      = 0;
  pDataStart       = NULL;
  do {
    pSectorData = _FindInSectorList(pInst, SectorIndex, &pSectorInfo);
    if (pSectorData == NULL) {
      if (NumSectorsAtOnce == 0u) {
        StartSector = SectorIndex;
        pDataStart  = pData8;
      }
      ++NumSectorsAtOnce;
    } else {
      if (NumSectorsAtOnce != 0u) {
        r = _ReadSectors(pInst, StartSector, pDataStart, NumSectorsAtOnce);
        if (r != 0) {
          return 1;           // Error, could not read sectors.
        }
        NumSectorsAtOnce = 0;
      }
      if (pSectorInfo->IsValid != 0u) {
        //
        // Copy sector data from list.
        //
        FS_MEMCPY(pData8, pSectorData, BytesPerSector);
      } else {
        //
        // Fill the sector data with a known value if the sector is not valid.
        //
        FS_MEMSET(pData8, 0xFF, BytesPerSector);
      }
    }
    ++SectorIndex;
    pData8 += BytesPerSector;
  } while (--NumSectors != 0u);
  if (NumSectorsAtOnce != 0u) {
    r = _ReadSectors(pInst, StartSector, pDataStart, NumSectorsAtOnce);
    if (r != 0) {
      return 1;               // Error, could not read sectors.
    }
  }
  return 0;                   // OK, all sectors read.
}

/*********************************************************************
*
*       _WRBUF_Write
*
*  Function description
*    FS driver function. Writes a number of sectors to storage medium.
*/
static int _WRBUF_Write(U8 Unit, U32 SectorIndex, const void * pBuffer, U32 NumSectors, U8 RepeatSame) {
  U32                    NumSectorsList;
  U32                    SectorCnt;
  U32                    NumSectorsFree;
  U32                    NumSectorsAtOnce;
  unsigned               ldBytesPerSector;
  U32                    BytesPerSector;
  int                    r;
  WRBUF_INST           * pInst;
  const U8             * pSectorData;
  U8                   * pSectorDataLast;
  FS_WRBUF_SECTOR_INFO * pSectorInfoLast;
  FS_WRBUF_SECTOR_INFO   SectorInfo;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;               // Error, instance not found.
  }
  ldBytesPerSector = pInst->ldBytesPerSector;
  BytesPerSector   = 1uL << ldBytesPerSector;
  NumSectorsList   = pInst->NumSectorsList;
  SectorCnt        = pInst->SectorCnt;
  pSectorData      = SEGGER_CONSTPTR2PTR(const U8, pBuffer);
  //
  // Overwrite the last sector in the list if possible.
  //
  pSectorDataLast = _GetLastFromSectorList(pInst, &pSectorInfoLast);
  if (pSectorDataLast != NULL) {
    if (pSectorInfoLast->SectorIndex == SectorIndex) {
      FS_MEMCPY(pSectorDataLast, pSectorData, BytesPerSector);
      ++SectorIndex;
      --NumSectors;
      if (RepeatSame == 0u) {
        pSectorData += BytesPerSector;
      }
    }
  }
  if (NumSectors == 0u) {
    return 0;
  }
  //
  // Make room in the sector list for the new sector data.
  //
  NumSectorsFree = NumSectorsList - SectorCnt;
  if (NumSectorsFree < NumSectors) {
    //
    // Write sectors to storage to free up entries in the list.
    //
    r = _CleanSectorList(pInst);
    if (r != 0) {
      return r;             // Error, could not clean sector list.
    }
  }
  //
  // If the number of sectors written is greater than the capacity of sector list.
  // store only the last NumSectorsList to sector list. The rest of the sectors is
  // written directly to storage.
  //
  if (NumSectors > NumSectorsList) {
    NumSectorsAtOnce = NumSectors - NumSectorsList;
    r = _WriteSectors(pInst, SectorIndex, pSectorData, NumSectorsAtOnce, RepeatSame);
    if (r != 0) {
      return r;             // Error, could not write sectors.
    }
    SectorIndex += NumSectorsAtOnce;
    NumSectors  -= NumSectorsAtOnce;
    if (RepeatSame == 0u) {
      pSectorData += NumSectorsAtOnce << ldBytesPerSector;
    }
  }
  if (NumSectors == 0u) {
    return 0;
  }
  //
  // Store the sector data to sector list.
  //
  do {
    SectorInfo.IsValid     = 1;
    SectorInfo.SectorIndex = SectorIndex;
    r = _AddToSectorList(pInst, &SectorInfo, pSectorData);
    if (r != 0) {
      return r;             // Error, could not add to sector list. Should not happen.
    }
    ++SectorIndex;
    if (RepeatSame == 0u) {
      pSectorData += BytesPerSector;
    }
  } while (--NumSectors != 0u);
  return 0;
}

/*********************************************************************
*
*       _WRBUF_IoCtl
*
*  Function description
*    FS driver function. Executes an I/O control command.
*/
static int _WRBUF_IoCtl(U8 Unit, I32 Cmd, I32 Aux, void * pBuffer) {
  int           r;
  int           Result;
  int           RelayCmd;
  U32           SectorIndex;
  U32           NumSectors;
  WRBUF_INST  * pInst;
  FS_DEV_INFO * pDevInfo;
  int           SectorUsage;
  U32         * pNumSectors;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return -1;                // Error, instance not found.
  }
  FS_USE_PARA(Aux);
  r        = -1;              // Set to indicate error.
  RelayCmd = 1;               // By default, pass the commands to the underlying driver
  switch (Cmd) {
  case FS_CMD_GET_DEVINFO:
    Result = _ReadApplyDeviceInfoIfRequired(pInst);
    if (Result == 0) {
      if (pBuffer != NULL) {
        pDevInfo = SEGGER_PTR2PTR(FS_DEV_INFO, pBuffer);
        pDevInfo->NumSectors     = pInst->NumSectors;
        pDevInfo->BytesPerSector = (U16)(1uL << pInst->ldBytesPerSector);
        r = 0;
      }
    }
    RelayCmd = 0;             // Command is handled by this driver.
    break;
#if FS_SUPPORT_DEINIT
  case FS_CMD_DEINIT:
    r = _IoCtl(pInst, Cmd, Aux, pBuffer);
    FS_FREE(pInst);
    pInst = NULL;
    _apInst[Unit] = NULL;
    _NumUnits--;
    RelayCmd = 0;             // Command is handled by this driver.
    break;
#endif
  case FS_CMD_GET_SECTOR_USAGE:
    {
      U8                   * pSectorData;
      int                  * pSectorUsage;
      FS_WRBUF_SECTOR_INFO * pSectorInfo;

      SectorIndex = (U32)Aux;
      pSectorInfo = NULL;
      pSectorData = _FindInSectorList(pInst, SectorIndex, &pSectorInfo);
      if (pSectorData != NULL) {
        if (pBuffer != NULL) {
          if (pSectorInfo->IsValid != 0u) {
            SectorUsage = FS_SECTOR_IN_USE;
          } else {
            SectorUsage = FS_SECTOR_NOT_USED;
          }
          pSectorUsage  = SEGGER_PTR2PTR(int, pBuffer);
          *pSectorUsage = SectorUsage;
          RelayCmd = 0;       // Command is handled by this driver.
          r = 0;              // OK, sector usage determined.
        }
      }
    }
    break;
  case FS_CMD_FREE_SECTORS:
    pNumSectors = SEGGER_PTR2PTR(U32, pBuffer);
    if (pNumSectors != NULL) {
      SectorIndex = (U32)Aux;
      NumSectors  = *pNumSectors;
      Result = _HandleFreeSectors(pInst, SectorIndex, NumSectors);
      if (Result == 0) {
        r = 0;
      }
    }
    RelayCmd = 0;             // Command is handled by this driver.
    break;
  case FS_CMD_UNMOUNT:
    Result = _CleanSectorList(pInst);
    if (Result == 0) {
      r = 0;
    }
    pInst->NumSectors       = 0;
    pInst->ldBytesPerSector = 0;
    pInst->SectorCnt        = 0;
    break;
  case FS_CMD_UNMOUNT_FORCED:
    pInst->NumSectors       = 0;
    pInst->ldBytesPerSector = 0;
    pInst->SectorCnt        = 0;
    r = 0;
    break;
  case FS_CMD_SYNC:
    Result = _CleanSectorList(pInst);
    if (Result == 0) {
      r = 0;
    }
    break;
  default:
    //
    // All other commands are relayed to the underlying driver.
    //
    break;
  }
  if (RelayCmd != 0) {
    r = _IoCtl(pInst, Cmd, Aux, pBuffer);
  }
  return r;
}

/*********************************************************************
*
*       _WRBUF_InitMedium
*
*  Function description
*    FS driver function. Initializes the storage medium.
*/
static int _WRBUF_InitMedium(U8 Unit) {
  int          r;
  WRBUF_INST * pInst;

  r     = 1;             // Set to indicate an error.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    r = _InitMedium(pInst);
  }
  return r;
}

/*********************************************************************
*
*       _WRBUF_GetStatus
*
*  Function description
*    FS driver function. Returns whether the storage media is present or not.
*/
static int _WRBUF_GetStatus(U8 Unit) {
  WRBUF_INST * pInst;
  int          Status;

  Status = FS_MEDIA_NOT_PRESENT;    // Set to indicate an error.
  pInst  = _GetInst(Unit);
  if (pInst != NULL) {
    Status = _GetStatus(pInst);
  }
  return Status;
}

/*********************************************************************
*
*       _WRBUF_GetNumUnits
*
*  Function description
*    FS driver function. Returns the number of driver instances.
*/
static int _WRBUF_GetNumUnits(void) {
  return (int)_NumUnits;
}

/*********************************************************************
*
*       FS_WRBUF_Driver
*/
const FS_DEVICE_TYPE FS_WRBUF_Driver = {
  _WRBUF_GetDriverName,
  _WRBUF_AddDevice,
  _WRBUF_Read,
  _WRBUF_Write,
  _WRBUF_IoCtl,
  _WRBUF_InitMedium,
  _WRBUF_GetStatus,
  _WRBUF_GetNumUnits
};

/*********************************************************************
*
*       Public code (internal)
*
**********************************************************************
*/

/*********************************************************************
*
*       FS__WRBUF_GetNumSectors
*
*  Function description
*    Returns the number of sectors which can be stored to internal buffer.
*/
U32 FS__WRBUF_GetNumSectors(U8 Unit) {
  U32          NumSectors;
  WRBUF_INST * pInst;

  NumSectors = 0;
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    NumSectors = pInst->NumSectorsList;
  }
  return NumSectors;
}

/*********************************************************************
*
*       FS__WRBUF_GetStatCounters
*/
void FS__WRBUF_GetStatCounters(U8 Unit, FS_WRBUF_STAT_COUNTERS * pStat) {
#if FS_WRBUF_ENABLE_STATS
  WRBUF_INST * pInst;

  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    *pStat = pInst->StatCounters;
  } else {
    FS_MEMSET(pStat, 0, sizeof(FS_WRBUF_STAT_COUNTERS));
  }
#else
  FS_USE_PARA(Unit);
  FS_MEMSET(pStat, 0, sizeof(FS_WRBUF_STAT_COUNTERS));
#endif
}

/*********************************************************************
*
*       FS__WRBUF_ResetStatCounters
*/
void FS__WRBUF_ResetStatCounters(U8 Unit) {
#if FS_WRBUF_ENABLE_STATS
  WRBUF_INST * pInst;

  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    FS_MEMSET(&pInst->StatCounters, 0, sizeof(FS_WRBUF_STAT_COUNTERS));
  }
#else
  FS_USE_PARA(Unit);
#endif
}

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_WRBUF_Configure
*
*  Function description
*    Sets the parameters of a driver instance.
*
*  Parameters
*    Unit             Index of the driver instance (0-based).
*    pDeviceType      [IN]  Storage device.
*    DeviceUnit       Unit number of storage device.
*    pBuffer          [IN]  Storage for sector data.
*    NumBytes         Number of bytes in pBuffer.
*
*  Additional information
*    This function is mandatory and it has to be called once for each
*    instance of the driver. FS_SIZEOF_WRBUF() can be used to calculate
*    the number of bytes required to be allocated in order to store a
*    specified number of logical sectors.
*/
void FS_WRBUF_Configure(U8 Unit, const FS_DEVICE_TYPE * pDeviceType, U8 DeviceUnit, void * pBuffer, U32 NumBytes) {
  WRBUF_INST * pInst;
  U8         * pData8;

  //
  // Sanity checks.
  //
  FS_DEBUG_ASSERT(FS_MTYPE_API, sizeof(FS_WRBUF_SECTOR_INFO) == FS_SIZEOF_WRBUF_SECTOR_INFO);   //lint !e506 !e774 Constant value Boolean and Boolean within 'if' always evaluates to False
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    pInst->DeviceUnit  = DeviceUnit;
    pInst->pDeviceType = pDeviceType;
    //
    // Align pointer to a 32-bit boundary.
    //
    pData8 = SEGGER_PTR2PTR(U8, pBuffer);
    if ((SEGGER_PTR2ADDR(pData8) & 3u) != 0u) {
      NumBytes -= 4u - (SEGGER_PTR2ADDR(pData8) & 3u);
      pData8   += 4u - (SEGGER_PTR2ADDR(pData8) & 3u);
    }
    pInst->NumBytesBuffer = NumBytes;
    pInst->paSectorInfo   = SEGGER_PTR2PTR(FS_WRBUF_SECTOR_INFO, pData8);
  }
}

/*************************** End of file ****************************/
