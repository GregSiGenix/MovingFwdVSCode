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
File        : FS_LogVolume.c
Purpose     : Logical volume driver
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

/*********************************************************************
*
*       ASSERT_UNIT_NO_IS_IN_RANGE
*/
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL)
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)                                \
  if ((Unit) >= (U8)FS_LOGVOL_NUM_UNITS) {                                \
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "LOGVOL: Invalid unit number.")); \
    FS_X_PANIC(FS_ERRCODE_INVALID_PARA);                                  \
  }
#else
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)
#endif  // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL

/*********************************************************************
*
*       Local data types
*
**********************************************************************
*/

/*********************************************************************
*
*       LOGVOL_DEVICE_INFO
*/
typedef struct LOGVOL_DEVICE_INFO LOGVOL_DEVICE_INFO;
struct LOGVOL_DEVICE_INFO {                             //lint -esym(9058, LOGVOL_DEVICE_INFO) tag unused outside of typedefs N:100. Reason: the typedef is used as forward declaration.
  LOGVOL_DEVICE_INFO   * pNext;
  const FS_DEVICE_TYPE * pDeviceType;
  U8                     DeviceUnit;
  U32                    StartSector;
  U32                    NumSectors;
  U32                    NumSectorsConf;
};

/*********************************************************************
*
*       LOGVOL_INST
*/
typedef struct {
#if (FS_LOGVOL_SUPPORT_DRIVER_MODE == 0)
  const char         * sVolumeName;
#endif
  LOGVOL_DEVICE_INFO * pDeviceInfo;
  U16                  BytesPerSector;
} LOGVOL_INST;

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static LOGVOL_INST * _apInst[FS_LOGVOL_NUM_UNITS];
static U8            _NumUnits = 0;

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

//lint -efunc(818, _LOGVOL_Read) Pointer parameter 'pBuffer' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory] N:104. Rationale: pBuffer cannot be declared as pointing to const because the function that performs the operation is used for reading as well as writing.

/*********************************************************************
*
*       _GetNumSectors
*/
static U32 _GetNumSectors(const LOGVOL_INST * pInst) {
  U32                  NumSectors;
  LOGVOL_DEVICE_INFO * pDeviceInfo;

  NumSectors = 0;
  for (pDeviceInfo = pInst->pDeviceInfo; pDeviceInfo != NULL; pDeviceInfo = pDeviceInfo->pNext) {
    NumSectors += pDeviceInfo->NumSectors;
  }
  return NumSectors;
}

/*********************************************************************
*
*       _ReadApplyDeviceParas
*/
static int _ReadApplyDeviceParas(LOGVOL_INST * pInst) {
  LOGVOL_DEVICE_INFO * pDeviceInfo;
  int                  r;
  FS_DEV_INFO          DevInfo;
  U32                  NumSectorsConf;
  U32                  NumSectorsDevice;
  U16                  BytesPerSector;
  U32                  StartSector;

  r = 0;                // Set to indicate success.
  BytesPerSector = 0;   // Number of bytes per sector not determined.
  for (pDeviceInfo = pInst->pDeviceInfo; pDeviceInfo != NULL; pDeviceInfo = pDeviceInfo->pNext) {
    const FS_DEVICE_TYPE * pDeviceType;
    U8 DeviceUnit;

    pDeviceType = pDeviceInfo->pDeviceType;
    DeviceUnit  = pDeviceInfo->DeviceUnit;
    //
    // Get info from device.
    //
    if (pDeviceType->pfIoCtl(DeviceUnit, FS_CMD_GET_DEVINFO, 0, SEGGER_PTR2PTR(void, &DevInfo)) != 0) {         // MISRA deviation D:100[f]
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "LOGVOL: Could not get information from device."));
      r = 1;
      break;
    }
    NumSectorsConf   = pDeviceInfo->NumSectorsConf;
    StartSector      = pDeviceInfo->StartSector;
    NumSectorsDevice = DevInfo.NumSectors;
    //
    // Validate the start sector.
    //
    if (StartSector > NumSectorsDevice) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "LOGVOL: Start sector exceeds device capacity."));
      r = 1;
      break;
    }
    //
    // When the number of configured sectors is 0 use the number of sectors reported by the device.
    //
    if (NumSectorsConf == 0u) {
      NumSectorsConf = NumSectorsDevice - StartSector;
    }
    //
    // Additional check to see if more sectors were configured as the number of sectors available.
    // In this case use the number of sectors reported by the device and issue a warning.
    //
    if ((NumSectorsConf + StartSector) > NumSectorsDevice) {
      FS_DEBUG_WARN((FS_MTYPE_DRIVER, "LOGVOL: Device has less sectors than requested. Using the number of sectors reported by device."));
      NumSectorsConf = NumSectorsDevice - StartSector;
    }
    //
    // For first device, set the number of bytes per sector. All add. devices added need to have the same sector size.
    //
    if (BytesPerSector == 0u) {
      BytesPerSector = DevInfo.BytesPerSector;
    }
    if (BytesPerSector != DevInfo.BytesPerSector) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "LOGVOL: Devices with different sector size can not be combined."));
      r = 1;
      break;
    }
    pDeviceInfo->NumSectors = NumSectorsConf;
  }
  pInst->BytesPerSector = BytesPerSector;
  return r;
}

/*********************************************************************
*
*       _ReadApplyDeviceParasIfRequired
*/
static int _ReadApplyDeviceParasIfRequired(LOGVOL_INST * pInst) {
  int r;

  r = 0;      // No error so far.
  if (pInst->BytesPerSector == 0u) {
    r = _ReadApplyDeviceParas(pInst);
  }
  return r;
}

/*********************************************************************
*
*       _ReadWrite
*/
static int _ReadWrite(const LOGVOL_INST * pInst, U32 FirstSectorReq, const void * pBuffer, U32 NumSectorsReq, U8 IsWrite, U8 RepeatSame) {
  LOGVOL_DEVICE_INFO * pDeviceInfo;
  int                  r;
  U32                  NumSectorsAtOnce;
  U32                  NumSectorsUsed;

  r = 1;        // Set to indicate an error.
  //
  // Iterate over devices until we have reached the last device or all data has been read.
  //
  pDeviceInfo = pInst->pDeviceInfo;
  while (pDeviceInfo != NULL) {
    NumSectorsUsed = pDeviceInfo->NumSectors;
    if (FirstSectorReq < NumSectorsUsed) {
      U32                    SectorIndex;
      U8                     DeviceUnit;
      const FS_DEVICE_TYPE * pDeviceType;

      NumSectorsAtOnce = SEGGER_MIN(NumSectorsUsed - FirstSectorReq, NumSectorsReq);
      pDeviceType = pDeviceInfo->pDeviceType;
      DeviceUnit  = pDeviceInfo->DeviceUnit;
      SectorIndex = FirstSectorReq + pDeviceInfo->StartSector;
      if (IsWrite != 0u) {
        r = pDeviceType->pfWrite(DeviceUnit, SectorIndex, pBuffer, NumSectorsAtOnce, RepeatSame);
      } else {
        r = pDeviceType->pfRead(DeviceUnit, SectorIndex, (void *)pBuffer, NumSectorsAtOnce);        //lint !e9005 attempt to cast away const/volatile from a pointer or reference N:100. Rationale: const is cast away here because _ReadWrite() is called when writing as well as when reading data.
      }
      if (r != 0) {
        break;            // Error, read or write operation failed.
      }
      NumSectorsReq  -= NumSectorsAtOnce;
      FirstSectorReq += NumSectorsAtOnce;
      {
        const U8 * p;

        p        = SEGGER_CONSTPTR2PTR(const U8, pBuffer);                                          // MISRA deviation D:100[e]
        p       += NumSectorsAtOnce * pInst->BytesPerSector;
        pBuffer  = p;
      }
    }
    FirstSectorReq -= NumSectorsUsed;
    if (NumSectorsReq == 0u) {
      r = 0;        // O.K., all sectors read
      break;
    }
    pDeviceInfo = pDeviceInfo->pNext;
  }
  return r;
}

/*********************************************************************
*
*       _FreeSectors
*/
static int _FreeSectors(const LOGVOL_INST * pInst, U32 FirstSectorReq, U32 NumSectorsReq) {
  int                  r;
  U32                  NumSectorsAtOnce;
  U32                  NumSectorsUsed;
  LOGVOL_DEVICE_INFO * pDeviceInfo;

  r = 1;      // Set to indicate an error.
  //
  // Iterate over all configured devices and call the "free sectors" function.
  //
  pDeviceInfo = pInst->pDeviceInfo;
  while (pDeviceInfo != NULL) {
    NumSectorsUsed = pDeviceInfo->NumSectors;
    if (FirstSectorReq < NumSectorsUsed) {
      U32                    SectorIndex;
      U8                     DeviceUnit;
      const FS_DEVICE_TYPE * pDeviceType;

      NumSectorsAtOnce = SEGGER_MIN(NumSectorsUsed - FirstSectorReq, NumSectorsReq);
      pDeviceType = pDeviceInfo->pDeviceType;
      DeviceUnit  = pDeviceInfo->DeviceUnit;
      SectorIndex = FirstSectorReq + pDeviceInfo->StartSector;
      r = pDeviceType->pfIoCtl(DeviceUnit, FS_CMD_FREE_SECTORS, (int)SectorIndex, SEGGER_PTR2PTR(void, &NumSectorsAtOnce));     // MISRA deviation D:100[f]
      if (r != 0) {
        break;      // Error, operation failed.
      }
      NumSectorsReq  -= NumSectorsAtOnce;
      FirstSectorReq += NumSectorsAtOnce;
    }
    FirstSectorReq -= NumSectorsUsed;
    if (NumSectorsReq == 0u) {
      r = 0;        // OK, all sectors freed.
      break;
    }
    pDeviceInfo = pDeviceInfo->pNext;
  }
  return r;
}

/*********************************************************************
*
*       _GetSectorUsage
*/
static int _GetSectorUsage(const LOGVOL_INST * pInst, U32 SectorIndex, int * pSectorUsage) {
  int                    r;
  U32                    NumSectors;
  U32                    StartSector;
  LOGVOL_DEVICE_INFO   * pDeviceInfo;
  const FS_DEVICE_TYPE * pDeviceType;
  U8                     DeviceUnit;

  r = 1;      // Set to indicate an error.
  //
  // Iterate over all configured devices and check the usage of the sector.
  //
  pDeviceInfo = pInst->pDeviceInfo;
  while (pDeviceInfo != NULL) {
    NumSectors  = pDeviceInfo->NumSectors;
    StartSector = pDeviceInfo->StartSector;
    if ((SectorIndex >= StartSector) && (SectorIndex < (StartSector + NumSectors))) {
      pDeviceType  = pDeviceInfo->pDeviceType;
      DeviceUnit   = pDeviceInfo->DeviceUnit;
      SectorIndex += StartSector;
      r = pDeviceType->pfIoCtl(DeviceUnit, FS_CMD_GET_SECTOR_USAGE, (int)SectorIndex, SEGGER_PTR2PTR(void, pSectorUsage));    // MISRA deviation D:100[f]
      break;
    }
    pDeviceInfo = pDeviceInfo->pNext;
  }
  return r;
}

/*********************************************************************
*
*       _AddDevice
*
*  Function description
*    Adds a new device to the list.
*
*  Parameters
*    pInst          Driver instance.
*    pDeviceType    Type of device to be added.
*    DeviceUnit     Index of device to be added.
*    StartSector    Index of the first logical sector in the added
*                   device to be used a storage.
*    NumSectors     Number of sectors from the added device to be
*                   used as storage.
*
*  Return value
*    ==0      OK, device added successfully.
*    !=0      An error occurred.
*/
static int _AddDevice(LOGVOL_INST * pInst, const FS_DEVICE_TYPE * pDeviceType, U8 DeviceUnit, U32 StartSector, U32 NumSectors) {
  LOGVOL_DEVICE_INFO *  pDeviceInfo;
  LOGVOL_DEVICE_INFO ** ppPrevNext;
  int                   r;

  r = 1;            // Set to indicate error.
  //
  // A new item is inserted an the end of the list.
  //
  ppPrevNext = &pInst->pDeviceInfo;
  for (;;) {
    pDeviceInfo = *ppPrevNext;
    if (pDeviceInfo == NULL) {
      break;
    }
    ppPrevNext = &pDeviceInfo->pNext;
  }
  //
  // Allocate memory for new device, fill it in and add it to the linked list.
  //
  pDeviceInfo = SEGGER_PTR2PTR(LOGVOL_DEVICE_INFO, FS_ALLOC_ZEROED((I32)sizeof(LOGVOL_DEVICE_INFO), "LOGVOL_DEVICE_INFO"));     // MISRA deviation D:100[d]
  if (pDeviceInfo != NULL) {
    pDeviceInfo->NumSectorsConf = NumSectors;
    pDeviceInfo->StartSector    = StartSector;
    pDeviceInfo->pDeviceType    = pDeviceType;
    pDeviceInfo->DeviceUnit     = DeviceUnit;
    *ppPrevNext = pDeviceInfo;
    r = 0;        // OK, device added.
  }
  return r;
}

#if FS_SUPPORT_DEINIT

/*********************************************************************
*
*       _DeInit
*/
static void _DeInit(LOGVOL_INST * pInst) {
  LOGVOL_DEVICE_INFO   * pDeviceInfo;
  LOGVOL_DEVICE_INFO   * pNext;
  U8                     DeviceUnit;
  const FS_DEVICE_TYPE * pDeviceType;

  pDeviceInfo = pInst->pDeviceInfo;
  while (pDeviceInfo != NULL) {
    pDeviceType  = pDeviceInfo->pDeviceType;
    DeviceUnit   = pDeviceInfo->DeviceUnit;
    (void)pDeviceType->pfIoCtl(DeviceUnit, FS_CMD_DEINIT, 0, NULL);
    pNext = pDeviceInfo->pNext;
    FS_Free(pDeviceInfo);
    pDeviceInfo = pNext;
  }
  FS_FREE(pInst);
  _NumUnits--;
}

#endif // FS_SUPPORT_DEINIT

/*********************************************************************
*
*       _Clean
*/
static int _Clean(const LOGVOL_INST * pInst) {
  int                    r;
  int                    Result;
  LOGVOL_DEVICE_INFO   * pDeviceInfo;
  const FS_DEVICE_TYPE * pDeviceType;
  U8                     DeviceUnit;

  r = 0;      // Set to indicate success.
  //
  // Iterate over all configured devices and perform the clean operation on each of it.
  //
  pDeviceInfo = pInst->pDeviceInfo;
  while (pDeviceInfo != NULL) {
    pDeviceType = pDeviceInfo->pDeviceType;
    DeviceUnit  = pDeviceInfo->DeviceUnit;
    Result = pDeviceType->pfIoCtl(DeviceUnit, FS_CMD_CLEAN, 0, NULL);
    if (Result != 0) {
      r = Result;
    }
    pDeviceInfo = pDeviceInfo->pNext;
  }
  return r;
}

/*********************************************************************
*
*       _CleanOne
*/
static int _CleanOne(const LOGVOL_INST * pInst, int * pMoreToClean) {
  int                    r;
  int                    Result;
  LOGVOL_DEVICE_INFO   * pDeviceInfo;
  const FS_DEVICE_TYPE * pDeviceType;
  U8                     DeviceUnit;

  r = 0;      // Set to indicate success.
  //
  // Iterate over all configured devices and perform the clean operation on each of it.
  //
  pDeviceInfo = pInst->pDeviceInfo;
  while (pDeviceInfo != NULL) {
    pDeviceType = pDeviceInfo->pDeviceType;
    DeviceUnit  = pDeviceInfo->DeviceUnit;
    Result = pDeviceType->pfIoCtl(DeviceUnit, FS_CMD_CLEAN_ONE, 0, pMoreToClean);
    if (Result != 0) {
      r = Result;
    }
    if (*pMoreToClean != 0) {
      break;
    }
    pDeviceInfo = pDeviceInfo->pNext;
  }
  return r;
}

/*********************************************************************
*
*       _GetInst
*/
static LOGVOL_INST * _GetInst(U8 Unit) {
  LOGVOL_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  pInst = NULL;
  if (Unit < (U8)FS_LOGVOL_NUM_UNITS) {
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
*       _LOGVOL_GetDriverName
*/
static const char * _LOGVOL_GetDriverName(U8 Unit) {
#if (FS_LOGVOL_SUPPORT_DRIVER_MODE == 0)
  const char * sVolumeName;

  if (_apInst[Unit] != NULL) {
    sVolumeName = _apInst[Unit]->sVolumeName;
  } else {
    sVolumeName = "";
  }
  return sVolumeName;
#else
  FS_USE_PARA(Unit);
  return "lvol";
#endif
}

/*********************************************************************
*
*       _LOGVOL_AddDevice
*/
static int _LOGVOL_AddDevice(void) {
#if (FS_LOGVOL_SUPPORT_DRIVER_MODE == 0)
  return (int)_NumUnits;
#else
  LOGVOL_INST * pInst;
  U8            Unit;

  if (_NumUnits >= (U8)FS_LOGVOL_NUM_UNITS) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "LOGVOL: Could not add device. Too many instances."));
    return FS_ERRCODE_TOO_MANY_INSTANCES;                                                               // Error, too many instances defined.
  }
  Unit  = _NumUnits;
  pInst = _apInst[Unit];
  if (pInst == NULL) {
    FS_ALLOC_ZEROED_PTR(SEGGER_PTR2PTR(void *, &pInst), (I32)sizeof(LOGVOL_INST), "LOGVOL_INST");       // MISRA deviation D:100[d]
    if (pInst == NULL) {
      return FS_ERRCODE_OUT_OF_MEMORY;
    }
    _apInst[Unit] = pInst;
    ++_NumUnits;
  }
  return (int)Unit;                                                                                     // OK, instance created.
#endif // FS_LOGVOL_SUPPORT_DRIVER_MODE == 0
}

/*********************************************************************
*
*       _LOGVOL_Read
*/
static int _LOGVOL_Read(U8 Unit, U32 SectorIndex, void * pBuffer, U32 NumSectors) {
  int           r;
  LOGVOL_INST * pInst;

  r     = 1;                      // Set to indicate an error.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    r = _ReadWrite(pInst, SectorIndex, pBuffer, NumSectors, 0, 0);              // Perform a read operation.
  }
  return r;
}

/*********************************************************************
*
*       _LOGVOL_Write
*/
static int _LOGVOL_Write(U8 Unit, U32 SectorIndex, const void * pBuffer, U32 NumSectors, U8 RepeatSame) {
  int           r;
  LOGVOL_INST * pInst;

  r     = 1;                      // Set to indicate an error.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    r = _ReadWrite(pInst, SectorIndex, pBuffer, NumSectors, 1, RepeatSame);     // Perform a write operation.
  }
  return r;
}

/*********************************************************************
*
*       _LOGVOL_IoCtl
*/
static int _LOGVOL_IoCtl(U8 Unit, I32 Cmd, I32 Aux, void * pBuffer) {
  LOGVOL_INST        * pInst;
  FS_DEV_INFO        * pInfo;
  LOGVOL_DEVICE_INFO * pDeviceInfo;
  int                  r;
  U32                  NumSectors;
  U32                  SectorIndex;
  int                * pSectorUsage;
  int                  Result;

  r     = -1;             // Set to indicate an error,
  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return r;
  }
  switch (Cmd) {
  case FS_CMD_GET_DEVINFO:
    r = _ReadApplyDeviceParasIfRequired(pInst);
    if ((pBuffer != NULL) && (r == 0)) {
      pInfo = SEGGER_PTR2PTR(FS_DEV_INFO, pBuffer);                             // MISRA deviation D:100[f]
      NumSectors = _GetNumSectors(pInst);
      if (NumSectors != 0u) {
        pInfo->NumSectors      = NumSectors;
        pInfo->BytesPerSector  = pInst->BytesPerSector;
        r = 0;
      }
    }
    break;
#if FS_SUPPORT_DEINIT
  case FS_CMD_DEINIT:
    _DeInit(pInst);
    _apInst[Unit] = NULL;
    r = 0;
    break;
#endif // FS_SUPPORT_DEINIT
  case FS_CMD_FREE_SECTORS:
    if (pBuffer != NULL) {
      SectorIndex = (U32)Aux;
      NumSectors  = *SEGGER_PTR2PTR(U32, pBuffer);                              // MISRA deviation D:100[f]
      r = _FreeSectors(pInst, SectorIndex, NumSectors);
    }
    break;
  case FS_CMD_CLEAN_ONE:
    {
      int   MoreToClean;
      int * pMoreToClean;

      MoreToClean = 0;
      Result = _CleanOne(pInst, &MoreToClean);
      pMoreToClean = SEGGER_PTR2PTR(int, pBuffer);                              // MISRA deviation D:100[f]
      if (pMoreToClean != NULL) {
        *pMoreToClean = MoreToClean;
      }
      if (Result == 0) {
        r = 0;
      }
    }
    break;
  case FS_CMD_CLEAN:
    r = _Clean(pInst);
    break;
  case FS_CMD_GET_SECTOR_USAGE:
    SectorIndex  = (U32)Aux;
    pSectorUsage = SEGGER_PTR2PTR(int, pBuffer);                                // MISRA deviation D:100[f]
    if (pSectorUsage != NULL) {
      r = _GetSectorUsage(pInst, SectorIndex, pSectorUsage);
    }
    break;
  case FS_CMD_UNMOUNT:
    //lint -fallthrough
  case FS_CMD_UNMOUNT_FORCED:
    pInst->BytesPerSector = 0;                                                  // At next mount, force a read/apply of parameters from device.
    //lint -fallthrough
  default:                                                                      //lint !e9090 unconditional break missing from switch case N:100. Rationale: The command has to be passed to device driver.
    r           = 0;
    pDeviceInfo = pInst->pDeviceInfo;
    while (pDeviceInfo != NULL) {
      int                    DriverReturn;
      U8                     DeviceUnit;
      const FS_DEVICE_TYPE * pDeviceType;

      pDeviceType  = pDeviceInfo->pDeviceType;
      DeviceUnit   = pDeviceInfo->DeviceUnit;
      DriverReturn = pDeviceType->pfIoCtl(DeviceUnit, Cmd, Aux, pBuffer);
      if (DriverReturn != 0) {
        r = DriverReturn;
      }
      pDeviceInfo = pDeviceInfo->pNext;
    }
    break;
  }
  return r;
}

/*********************************************************************
*
*       _LOGVOL_InitDevice
*/
static int _LOGVOL_InitDevice(U8 Unit) {
  LOGVOL_DEVICE_INFO * pDeviceInfo;
  int                  r;
  LOGVOL_INST        * pInst;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;                     // Error, could not get driver instance.
  }
  for (pDeviceInfo = pInst->pDeviceInfo; pDeviceInfo != NULL; pDeviceInfo = pDeviceInfo->pNext) {
    const FS_DEVICE_TYPE * pDeviceType;
    U8                     DeviceUnit;

    pDeviceType = pDeviceInfo->pDeviceType;
    DeviceUnit  = pDeviceInfo->DeviceUnit;
    if (pDeviceType->pfInitMedium != NULL) {
      r = pDeviceType->pfInitMedium(DeviceUnit);
      if (r != 0) {
        return 1;                 // Error, could not initialize storage device.
      }
    }
  }
  return 0;
}

/*********************************************************************
*
*       _LOGVOL_GetStatus
*/
static int _LOGVOL_GetStatus(U8 Unit) {
  LOGVOL_DEVICE_INFO * pDeviceInfo;
  LOGVOL_INST        * pInst;
  int                  Status;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;                     // Error, could not get driver instance.
  }
  Status = FS_MEDIA_IS_PRESENT;
  for (pDeviceInfo = pInst->pDeviceInfo; pDeviceInfo != NULL; pDeviceInfo = pDeviceInfo->pNext) {
    const FS_DEVICE_TYPE * pDeviceType;
    U8                     DeviceUnit;
    int                    DeviceStatus;

    pDeviceType  = pDeviceInfo->pDeviceType;
    DeviceUnit   = pDeviceInfo->DeviceUnit;
    DeviceStatus = pDeviceType->pfGetStatus(DeviceUnit);
    if (DeviceStatus == FS_MEDIA_NOT_PRESENT) {
      Status = DeviceStatus;
      break;          // Error, the device is not available.
    }
  }
  return Status;
}

/*********************************************************************
*
*       _LOGVOL_GetNumUnits
*/
static int _LOGVOL_GetNumUnits(void) {
  return (int)_NumUnits;
}

/*********************************************************************
*
*       Public data
*
**********************************************************************
*/

#if (FS_LOGVOL_SUPPORT_DRIVER_MODE == 0)

/*********************************************************************
*
*       _LOGVOL_Driver
*/
static const FS_DEVICE_TYPE _LOGVOL_Driver = {
  _LOGVOL_GetDriverName,
  _LOGVOL_AddDevice,
  _LOGVOL_Read,
  _LOGVOL_Write,
  _LOGVOL_IoCtl,
  _LOGVOL_InitDevice,
  _LOGVOL_GetStatus,
  _LOGVOL_GetNumUnits
};

#else

/*********************************************************************
*
*       FS_LOGVOL_Driver
*/
const FS_DEVICE_TYPE FS_LOGVOL_Driver = {
  _LOGVOL_GetDriverName,
  _LOGVOL_AddDevice,
  _LOGVOL_Read,
  _LOGVOL_Write,
  _LOGVOL_IoCtl,
  _LOGVOL_InitDevice,
  _LOGVOL_GetStatus,
  _LOGVOL_GetNumUnits
};

#endif

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

#if (FS_LOGVOL_SUPPORT_DRIVER_MODE == 0)

/*********************************************************************
*
*       FS_LOGVOL_Create
*
*  Function description
*    Creates a driver instance.
*
*  Parameters
*    sVolumeName    Name of the volume to create.
*
*  Return value
*    ==0      OK, volume has been created.
*    !=0      An error occurred.
*
*  Additional information
*    This function creates an instance of a logical volume.
*    A logical volume is the representation of one or more physical devices
*    as a single device. It allows treating multiple physical devices as one
*    larger device. The file system takes care of selecting the correct
*    location on the correct physical device when reading from or writing to
*    the logical volume. Logical volumes are typically used if multiple flash
*    devices (NOR or NAND) are present, but they should be presented
*    to the application in the same way as a single device with the combined
*    capacity of both.
*
*    sVolumeName is the name that has to be assigned to the logical
*    volume. This is the volume name that is passed to some of
*    the FS API functions and that has to be used in a file path.
*
*    FS_LOGVOL_Create() does nothing if the module is configured
*    to work in driver mode by setting the FS_LOGVOL_SUPPORT_DRIVER_MODE
*    define option to 1. In this case a logical driver is created
*    by adding it via FS_AddDevice() to file system.
*
*    Normally, all devices are added individually using FS_AddDevice().
*    This function adds the devices physically as well as logically to the file system.
*    In contrast to adding all devices individually, all devices can be combined
*    in a logical volume with a total size of all combined devices.
*    To create a logical volume the following steps have to be performed:
*    1. The storage device has to be physically added to the file system using
*       FS_AddPhysDevice().
*    2. A logical volume has to be created using FS_LOGVOL_Create().
*    3. The devices which are physically added to the file system have to be
*       added to the logical volume using FS_LOGVOL_AddDevice().
*/
int FS_LOGVOL_Create(const char * sVolumeName) {
  int           r;
  LOGVOL_INST * pLogVol;
  FS_VOLUME   * pVolume;

  r = FS_ERRCODE_TOO_MANY_INSTANCES;
  FS_LOCK();
  if (_NumUnits < (U8)FS_LOGVOL_NUM_UNITS) {
    r = FS_ERRCODE_OUT_OF_MEMORY;
    pVolume = FS__AddDevice(&_LOGVOL_Driver);
    if (pVolume != NULL) {
      pLogVol = SEGGER_PTR2PTR(LOGVOL_INST, FS_ALLOC_ZEROED((I32)sizeof(LOGVOL_INST), "LOGVOL_INST"));    // MISRA deviation D:100[d]
      if (pLogVol != NULL) {
        pLogVol->sVolumeName = sVolumeName;
        _apInst[_NumUnits++] = pLogVol;
        r = 0;
      }
    }
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_LOGVOL_AddDevice
*
*  Function description
*    Adds a storage device to a logical volume.
*
*  Parameters
*    sVolumeName  Name of the logical volume.
*    pDeviceType  Type of the storage device that has to be added.
*    DeviceUnit   Index of the storage device that has to be added (0-based).
*    StartSector  Index of the first sector that has to be used as storage (0-based).
*    NumSectors   Number of sectors that have to be used as storage.
*
*  Return value
*    ==0      OK, storage device added.
*    !=0      An error occurred.
*
*  Additional information
*    Only devices with an identical sector size can be combined to a logical volume.
*    All additionally added devices need to have the same sector size as the first
*    physical device of the logical volume.
*
*    This function does nothing if FS_LOGVOL_SUPPORT_DRIVER_MODE is set to 1.
*/
int FS_LOGVOL_AddDevice(const char * sVolumeName, const FS_DEVICE_TYPE * pDeviceType, U8 DeviceUnit, U32 StartSector, U32 NumSectors) {
  unsigned      i;
  int           r;
  LOGVOL_INST * pInst;

  FS_LOCK();
  r = FS_ERRCODE_VOLUME_NOT_FOUND;
  //
  // Find log volume and store the parameters.
  //
  for (i = 0; i < _NumUnits; i++) {
    pInst = _apInst[i];
    if (FS_STRCMP(sVolumeName, pInst->sVolumeName) == 0) {
      r = _AddDevice(pInst, pDeviceType, DeviceUnit, StartSector, NumSectors);
      break;
    }
  }
  FS_UNLOCK();
  return r;
}

#else

/*********************************************************************
*
*       FS_LOGVOL_AddDeviceEx
*
*  Function description
*    Adds a storage device to a logical volume.
*
*  Parameters
*    Unit         Index of the driver instance (0-based).
*    pDeviceType  Type of the storage device that has to be added.
*    DeviceUnit   Index of the storage device that has to be added (0-based).
*    StartSector  Index of the first sector that has to be used as storage (0-based).
*    NumSectors   Number of sectors that have to be used as storage.
*
*  Return value
*    ==0      OK, storage device added.
*    !=0      An error occurred.
*
*  Additional information
*    This function has to be called at least once for each instance
*    of a LOGVOL driver. Each call to FS_LOGVOL_AddDeviceEx() defines a
*    range of sectors to be used a storage from a volume attached to
*    a device or logical driver. The volume to be used as storage is
*    identified by pDeviceType and DeviceUnit. If the defined logical
*    volume extends over two or more existing sectors then all of
*    these volumes need to have the same logical sector size.
*
*    If NumSectors is set to 0 then all the sectors of the specified volume
*    are used as storage.
*
*    FS_LOGVOL_AddDeviceEx does nothing if FS_LOGVOL_SUPPORT_DRIVER_MODE is set to 0.
*/
int FS_LOGVOL_AddDeviceEx(U8 Unit, const FS_DEVICE_TYPE * pDeviceType, U8 DeviceUnit, U32 StartSector, U32 NumSectors) {
  LOGVOL_INST * pInst;
  int           r;

  r = FS_ERRCODE_INVALID_PARA;
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    r = _AddDevice(pInst, pDeviceType, DeviceUnit, StartSector, NumSectors);
  }
  return r;
}

#endif // FS_LOGVOL_SUPPORT_DRIVER_MODE == 0

/*************************** End of file ****************************/

