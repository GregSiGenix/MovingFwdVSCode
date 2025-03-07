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
File        : FS_ReadAhead.c
Purpose     : Logical driver that reads in advance sectors from storage device.
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
#if FS_READAHEAD_ENABLE_STATS
  #define IF_STATS(Exp) Exp
#else
  #define IF_STATS(Exp)
#endif

/*********************************************************************
*
*       ASSERT_UNIT_NO_IS_IN_RANGE
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)                                     \
    if ((Unit) >= (U8)FS_READAHEAD_NUM_UNITS) {                                \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "READAHEAD: Invalid unit number.")); \
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);                                     \
    }
#else
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)
#endif

/*********************************************************************
*
*       ASSERT_SECTORS_ARE_IN_RANGE
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_SECTORS_ARE_IN_RANGE(pInst, SectorIndex, NumSectors)           \
  if (((SectorIndex) >= (pInst)->NumSectorsDevice) ||                           \
    (((SectorIndex) + (NumSectors)) > (pInst)->NumSectorsDevice)) {             \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "READAHEAD: Invalid sector range.")); \
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);                                      \
    }
#else
  #define ASSERT_SECTORS_ARE_IN_RANGE(pInst, SectorIndex, NumSectors)
#endif

/*********************************************************************
*
*       ASSERT_DEVICE_IS_SET
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_DEVICE_IS_SET(pInst)                                     \
    if ((pInst)->pDeviceType == NULL) {                                   \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "READAHEAD: Device not set.")); \
      FS_X_PANIC(FS_ERRCODE_UNKNOWN_DEVICE);                              \
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
typedef struct {
  U8                           Unit;
  U8                           DeviceUnit;
  U8                           IsActive;
  const FS_DEVICE_TYPE       * pDeviceType;
  U32                          NumSectorsDevice;  // Total number of sectors on the storage
  U16                          ldBytesPerSector;  // Size of a sector in bytes (as power of 2)
  U32                        * pReadBuffer;       // Pointer to read buffer assigned by the application
  U32                          NumBytesBuffer;    // Size of the read buffer in bytes
  U32                          StartSector;       // Index of the first sector in the buffer
  U32                          NumSectorsRead;    // Number of sectors stored in the buffer
#if FS_READAHEAD_ENABLE_STATS
  FS_READAHEAD_STAT_COUNTERS   StatCounters;      // Statistical counters used for debugging.
#endif // FS_READAHEAD_ENABLE_STATS
} READAHEAD_INST;

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static READAHEAD_INST * _apInst[FS_READAHEAD_NUM_UNITS];
static U8               _NumUnits = 0;

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
*    Initializes the underlying driver.
*/
static int _InitMedium(const READAHEAD_INST * pInst) {
  int                    r;
  U8                     DeviceUnit;
  const FS_DEVICE_TYPE * pDeviceType;

  ASSERT_DEVICE_IS_SET(pInst);
  r = 0;        // No error so far.
  pDeviceType = pInst->pDeviceType;
  DeviceUnit  = pInst->DeviceUnit;
  if (pDeviceType->pfInitMedium != NULL) {
    r = pDeviceType->pfInitMedium(DeviceUnit);
    if (r != 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "READAHEAD: Could not initialize storage medium."));
    }
  }
  return r;
}

/*********************************************************************
*
*       _GetDeviceInfo
*
*  Function description
*    Reads device information from the underlying driver.
*/
static int _GetDeviceInfo(const READAHEAD_INST * pInst, FS_DEV_INFO * pDevInfo) {
  int                    r;
  U8                     DeviceUnit;
  const FS_DEVICE_TYPE * pDeviceType;

  pDeviceType = pInst->pDeviceType;
  DeviceUnit  = pInst->DeviceUnit;
  r = pDeviceType->pfIoCtl(DeviceUnit, FS_CMD_GET_DEVINFO, 0, SEGGER_PTR2PTR(void, pDevInfo));
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "READAHEAD: Could not get storage info."));
  }
  return r;
}

/*********************************************************************
*
*       _ReadDeviceInfoIfRequired
*
*  Function description
*    Loads a number of sectors from the storage medium.
*/
static int _ReadDeviceInfoIfRequired(READAHEAD_INST * pInst) {
  int         r;
  FS_DEV_INFO DevInfo;

  r = 0;        // Set to indicate success.
  if (pInst->NumSectorsDevice == 0u) {
    r = _GetDeviceInfo(pInst, &DevInfo);
    if (r == 0) {
      pInst->NumSectorsDevice = DevInfo.NumSectors;
      pInst->ldBytesPerSector = (U16)_ld(DevInfo.BytesPerSector);
    }
  }
  return r;
}

/*********************************************************************
*
*       _ReadSectorsFromStorage
*
*  Function description
*    Loads a number of sectors from the storage medium.
*/
static int _ReadSectorsFromStorage(READAHEAD_INST * pInst, U32 SectorIndex, void * pBuffer, U32 NumSectors) {    //lint -efunc(818, _ReadSectorsFromStorage) Rationale: pInst cannot be declared as pointing to const because the statistical counters have to be updated in debug builds.
  int                    r;
  U8                     DeviceUnit;
  const FS_DEVICE_TYPE * pDeviceType;

  ASSERT_SECTORS_ARE_IN_RANGE(pInst, SectorIndex, NumSectors);
  ASSERT_DEVICE_IS_SET(pInst);
  pDeviceType  = pInst->pDeviceType;
  DeviceUnit   = pInst->DeviceUnit;
  r = pDeviceType->pfRead(DeviceUnit, SectorIndex, pBuffer, NumSectors);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "READAHEAD: Could not read sectors."));
  } else {
    //
    // OK, sector data read from storage.
    //
    IF_STATS(pInst->StatCounters.ReadSectorCnt += NumSectors);
  }
  return r;
}

/*********************************************************************
*
*       _ReadSectors
*
*  Function description
*    Reads sectors in advance from the storage medium.
*/
static int _ReadSectors(READAHEAD_INST * pInst, U32 SectorIndex, void * pBuffer, U32 NumSectors) {
  int   r;
  U32 * pReadBuffer;
  U32   NumSectorsBuffer;
  U16   ldBytesPerSector;
  U32   NumBytesBuffer;
  U32   NumBytesToCopy;
  U32   NumSectorsToRead;
  U32   NumSectorsRead;
  U32   FirstSector;
  U32   LastSector;
  U32   NumSectorsDevice;

  ldBytesPerSector = pInst->ldBytesPerSector;
  pReadBuffer      = pInst->pReadBuffer;
  NumBytesBuffer   = pInst->NumBytesBuffer;
  NumSectorsBuffer = 0;
  if (NumBytesBuffer != 0u) {
    NumSectorsBuffer = NumBytesBuffer >> ldBytesPerSector;
  }
  //
  // Determine whether the requested sectors are present in the buffer.
  //
  FirstSector    = pInst->StartSector;
  NumSectorsRead = pInst->NumSectorsRead;
  if (FirstSector != SECTOR_INDEX_INVALID) {
    LastSector  = FirstSector + NumSectorsRead - 1u;
    if ((SectorIndex >= FirstSector) && ((SectorIndex + NumSectors - 1u) <= LastSector)) {
      U32   SectorOff;
      U8  * pData;

      //
      // Sectors found in the internal buffer. Copy the contents to user buffer.
      //
      SectorOff      = (SectorIndex - FirstSector) << ldBytesPerSector;
      pData          = SEGGER_PTR2PTR(U8, pReadBuffer) + SectorOff;
      NumBytesToCopy = NumSectors << ldBytesPerSector;
      FS_MEMCPY(pBuffer, pData, NumBytesToCopy);
      IF_STATS(pInst->StatCounters.ReadSectorCachedCnt += NumSectors);
      return 0;         // OK, sectors read from internal buffer.
    }
  }
  //
  // Sectors not in buffer. Read from storage to internal or user buffer.
  // The buffer with the largest size is used.
  //
  if (NumSectorsBuffer > NumSectors) {
    //
    // Read sectors in advance to internal buffer and then copy the contents to user buffer.
    //
    NumSectorsDevice = pInst->NumSectorsDevice;
    NumSectorsToRead = NumSectorsBuffer;
    if ((SectorIndex + NumSectorsToRead) > NumSectorsDevice) {
      NumSectorsToRead = NumSectorsDevice - SectorIndex;        // Make sure that we do not read past the end of storage.
    }
    r = _ReadSectorsFromStorage(pInst, SectorIndex, pReadBuffer, NumSectorsToRead);
    if (r == 0) {
      //
      // Data read to internal buffer. Copy it to user buffer.
      //
      NumBytesToCopy = NumSectors << ldBytesPerSector;
      FS_MEMCPY(pBuffer, pReadBuffer, NumBytesToCopy);
      pInst->StartSector    = SectorIndex;
      pInst->NumSectorsRead = NumSectorsToRead;
    }
  } else {
    //
    // User buffer is greater than the internal buffer.
    // Read to user buffer and then copy the data to internal buffer.
    //
    r = _ReadSectorsFromStorage(pInst, SectorIndex, pBuffer, NumSectors);
    if (r == 0) {
      //
      // Data read to user buffer. Copy it to internal buffer.
      //
      NumBytesToCopy = NumSectorsBuffer << ldBytesPerSector;
      if (NumBytesToCopy != 0u) {
        FS_MEMCPY(pReadBuffer, pBuffer, NumBytesToCopy);
        pInst->StartSector    = SectorIndex;
        pInst->NumSectorsRead = NumSectors;
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _GetStatus
*
*  Function description
*    Returns whether the storage medium is present or not.
*/
static int _GetStatus(const READAHEAD_INST * pInst) {
  int                    Status;
  U8                     DeviceUnit;
  const FS_DEVICE_TYPE * pDeviceType;

  Status = FS_MEDIA_NOT_PRESENT;
  if (pInst->pDeviceType != NULL) {
    pDeviceType = pInst->pDeviceType;
    DeviceUnit  = pInst->DeviceUnit;
    Status      = pDeviceType->pfGetStatus(DeviceUnit);
  }
  return Status;
}

/*********************************************************************
*
*       _WriteSectorsToStorage
*
*  Function description
*    Stores a number of sectors to storage medium.
*/
static int _WriteSectorsToStorage(const READAHEAD_INST * pInst, U32 SectorIndex, const void * pBuffer, U32 NumSectors, U8 RepeatSame) {
  int                    r;
  U8                     DeviceUnit;
  const FS_DEVICE_TYPE * pDeviceType;

  ASSERT_SECTORS_ARE_IN_RANGE(pInst, SectorIndex, NumSectors);
  ASSERT_DEVICE_IS_SET(pInst);
  pDeviceType  = pInst->pDeviceType;
  DeviceUnit   = pInst->DeviceUnit;
  r = pDeviceType->pfWrite(DeviceUnit, SectorIndex, pBuffer, NumSectors, RepeatSame);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "READAHEAD: Could not write sectors."));
  }
  return r;
}

/*********************************************************************
*
*       _GetInst
*
*  Function description
*    Returns a driver instance by unit number.
*/
static READAHEAD_INST * _GetInst(U8 Unit) {
  READAHEAD_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  pInst = NULL;
  if (Unit < (U8)FS_READAHEAD_NUM_UNITS) {
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
*       _READAHEAD_GetDriverName
*
*  Function description
*    FS driver function. Returns the driver name.
*/
static const char * _READAHEAD_GetDriverName(U8 Unit) {
  FS_USE_PARA(Unit);
  return "rah";
}

/*********************************************************************
*
*       _READAHEAD_AddDevice
*
*  Function description
*    FS driver function. Creates a driver instance.
*/
static int _READAHEAD_AddDevice(void) {
  READAHEAD_INST * pInst;
  U8               Unit;

  if (_NumUnits >= (U8)FS_READAHEAD_NUM_UNITS) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "READAHEAD: Could not add device. Too many instances."));
    return -1;                      // Error, too many instances defined.
  }
  Unit  = _NumUnits;
  pInst = _apInst[Unit];
  if (pInst == NULL) {
    FS_ALLOC_ZEROED_PTR(SEGGER_PTR2PTR(void*, &pInst), (I32)sizeof(READAHEAD_INST), "READAHEAD_INST");
    if (pInst == NULL) {
      return -1;                    // Error, could not create instance.
    }
    pInst->Unit        = Unit;
    pInst->StartSector = SECTOR_INDEX_INVALID;
    _apInst[Unit] = pInst;
    ++_NumUnits;
  }
  return (int)Unit;                 // OK, instance created.
}

/*********************************************************************
*
*       _READAHEAD_Read
*
*  Function description
*    FS driver function. Reads a number of sectors from storage medium.
*/
static int _READAHEAD_Read(U8 Unit, U32 SectorIndex, void * pBuffer, U32 NumSectors) {
  READAHEAD_INST * pInst;
  int              r;
  U8               IsActive;
  U32            * pReadBuffer;

  r     = 1;                // Set to indicate an error.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    IsActive    = pInst->IsActive;
    pReadBuffer = pInst->pReadBuffer;
    if ((IsActive != 0u) && (pReadBuffer != NULL)) {
      r = _ReadSectors(pInst, SectorIndex, pBuffer, NumSectors);
    } else {
      r = _ReadSectorsFromStorage(pInst, SectorIndex, pBuffer, NumSectors);
    }
  }
  return r;
}

/*********************************************************************
*
*       _READAHEAD_Write
*
*  Function description
*    FS driver function. Writes a number of sectors to storage medium.
*/
static int _READAHEAD_Write(U8 Unit, U32 SectorIndex, const void * pBuffer, U32 NumSectors, U8 RepeatSame) {
  READAHEAD_INST * pInst;
  int             r;

  r     = 1;                // Set to indicate an error.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    pInst->StartSector = SECTOR_INDEX_INVALID;    // TBD: update the data in the buffer instead of invalidate it.
    r = _WriteSectorsToStorage(pInst, SectorIndex, pBuffer, NumSectors, RepeatSame);
  }
  return r;
}

/*********************************************************************
*
*       _READAHEAD_IoCtl
*
*  Function description
*    FS driver function. Executes an I/O control command.
*/
static int _READAHEAD_IoCtl(U8 Unit, I32 Cmd, I32 Aux, void * pBuffer) {
  READAHEAD_INST       * pInst;
  int                    r;
  U8                     DeviceUnit;
  FS_DEV_INFO          * pDevInfo;
  int                    RelayCmd;
  const FS_DEVICE_TYPE * pDeviceType;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return -1;                // Instance not allocated
  }
  FS_USE_PARA(Aux);
  r           = -1;           // Set to indicate an error.
  RelayCmd    = 1;            // By default, pass the commands to the underlying driver
  DeviceUnit  = pInst->DeviceUnit;
  pDeviceType = pInst->pDeviceType;
  switch (Cmd) {
  case FS_CMD_GET_DEVINFO:
    if (pBuffer != NULL) {
      r = _ReadDeviceInfoIfRequired(pInst);
      if (r == 0) {
        pDevInfo = SEGGER_PTR2PTR(FS_DEV_INFO, pBuffer);
        pDevInfo->NumSectors     = pInst->NumSectorsDevice;
        pDevInfo->BytesPerSector = (U16)(1uL << pInst->ldBytesPerSector);
      }
    }
    RelayCmd = 0;             // Command is handled by this driver.
    break;
  case FS_CMD_ENABLE_READ_AHEAD:
    pInst->StartSector = SECTOR_INDEX_INVALID;
    pInst->IsActive    = 1;
    RelayCmd           = 0;   // Command is handled by this driver.
    break;
  case FS_CMD_DISABLE_READ_AHEAD:
    pInst->IsActive = 0;
    RelayCmd = 0;             // Command is handled by this driver.
    break;
#if FS_SUPPORT_DEINIT
  case FS_CMD_DEINIT:
    if (pDeviceType != NULL) {
      r = pDeviceType->pfIoCtl(DeviceUnit, Cmd, Aux, pBuffer);
    }
    RelayCmd = 0;             // Command is handled by this driver.
    FS_FREE(pInst);
    _apInst[Unit] = NULL;
    _NumUnits--;
    break;
#endif
  case FS_CMD_UNMOUNT:
    // through
  case FS_CMD_UNMOUNT_FORCED:
    pInst->NumSectorsDevice = 0;
    pInst->ldBytesPerSector = 0;
    pInst->StartSector      = SECTOR_INDEX_INVALID;
    break;
  default:
    //
    // All other commands are relayed to the underlying driver(s).
    //
    break;
  }
  if (RelayCmd != 0) {
    if (pDeviceType != NULL) {
      r = pDeviceType->pfIoCtl(DeviceUnit, Cmd, Aux, pBuffer);
    }
  }
  return r;
}

/*********************************************************************
*
*       _READAHEAD_InitMedium
*
*  Function description
*    FS driver function. Initializes the storage medium.
*/
static int _READAHEAD_InitMedium(U8 Unit) {
  int              r;
  READAHEAD_INST * pInst;

  r     = 1;                    // Set to indicate an error.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    r = _InitMedium(pInst);
  }
  return r;
}

/*********************************************************************
*
*       _READAHEAD_GetStatus
*
*  Function description
*    FS driver function. Returns whether the storage media is present or not.
*/
static int _READAHEAD_GetStatus(U8 Unit) {
  READAHEAD_INST * pInst;
  int              Status;

  Status = FS_MEDIA_NOT_PRESENT;    // Set to indicate an error.
  pInst  = _GetInst(Unit);
  if (pInst != NULL) {
    Status = _GetStatus(pInst);
  }
  return Status;
}

/*********************************************************************
*
*       _READAHEAD_GetNumUnits
*
*  Function description
*    FS driver function. Returns the number of driver instances.
*/
static int _READAHEAD_GetNumUnits(void) {
  return (int)_NumUnits;
}

/*********************************************************************
*
*       Public data
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_READAHEAD_Driver
*/
const FS_DEVICE_TYPE FS_READAHEAD_Driver = {
  _READAHEAD_GetDriverName,
  _READAHEAD_AddDevice,
  _READAHEAD_Read,
  _READAHEAD_Write,
  _READAHEAD_IoCtl,
  _READAHEAD_InitMedium,
  _READAHEAD_GetStatus,
  _READAHEAD_GetNumUnits
};

/*********************************************************************
*
*       Public code (internal)
*
**********************************************************************
*/

/*********************************************************************
*
*       FS__READAHEAD_SetBuffer
*
*  Function description
*    Configures a new working buffer.
*/
int FS__READAHEAD_SetBuffer(U8 Unit, U32 * pData, U32 NumBytes) {
  READAHEAD_INST * pInst;
  int              r;

  r = 1;                    // Set to indicate an error.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    pInst->pReadBuffer    = pData;
    pInst->NumBytesBuffer = NumBytes;
    pInst->StartSector    = SECTOR_INDEX_INVALID;
    r = 0;
  }
  return r;
}

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_READAHEAD_Configure
*
*  Function description
*    Sets the parameters which allows the driver instance to access the storage medium.
*
*  Parameters
*    Unit           Index of the driver instance (0-based)
*    pDeviceType    [IN] Device driver used to access the storage device.
*    DeviceUnit     Index of the storage device (0-based)
*    pData          [IN] Buffer to store the sector data read from storage device.
*    NumBytes       Number of bytes in the read buffer.
*
*  Additional information
*    This function is mandatory and it has to be called once for each
*    instance of the driver. The read buffer has to be sufficiently large
*    to store at least one logical sector.
*/
void FS_READAHEAD_Configure(U8 Unit, const FS_DEVICE_TYPE * pDeviceType, U8 DeviceUnit, U32 * pData, U32 NumBytes) {
  READAHEAD_INST * pInst;

  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    pInst->pDeviceType    = pDeviceType;
    pInst->DeviceUnit     = DeviceUnit;
    pInst->pReadBuffer    = pData;
    pInst->NumBytesBuffer = NumBytes;
    pInst->StartSector    = SECTOR_INDEX_INVALID;
  }
}

#if FS_READAHEAD_ENABLE_STATS

/*********************************************************************
*
*       FS_READAHEAD_GetStatCounters
*
*  Function description
*    Returns the values of the statistical counters.
*
*  Parameters
*    Unit         Driver index (0-based)
*    pStat        [OUT] Values of statistical counters.
*
*  Additional information
*    This function is optional. The statistical counters are updated
*    only when the file system is compiled with FS_READAHEAD_ENABLE_STATS
*    set to 1 or with FS_DEBUG_LEVEL set to a value greater than or equal
*    to FS_DEBUG_LEVEL_CHECK_ALL.
*/
void FS_READAHEAD_GetStatCounters(U8 Unit, FS_READAHEAD_STAT_COUNTERS * pStat) {
  READAHEAD_INST * pInst;

  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    if (pStat != NULL) {
      *pStat = pInst->StatCounters;
    }
  }
}

/*********************************************************************
*
*       FS_READAHEAD_ResetStatCounters
*
*  Function description
*    Sets to 0 the values of all statistical counters.
*
*  Parameters
*    Unit         Driver index (0-based)
*
*  Additional information
*    This function is optional. It is available only when the file system
*    is compiled with FS_READAHEAD_ENABLE_STATS set to 1 or with FS_DEBUG_LEVEL
*    set to a value greater than or equal to FS_DEBUG_LEVEL_CHECK_ALL.
*/
void FS_READAHEAD_ResetStatCounters(U8 Unit) {
  READAHEAD_INST * pInst;

  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    FS_MEMSET(&pInst->StatCounters, 0, sizeof(FS_READAHEAD_STAT_COUNTERS));
  }
}

#endif // FS_READAHEAD_ENABLE_STATS

/*************************** End of file ****************************/
