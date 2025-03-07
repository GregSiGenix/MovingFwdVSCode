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
File        : FS_SectorSize.c
Purpose     : Converts between different sector sizes.
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
*       ASSERT_UNIT_NO_IS_IN_RANGE
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)                                    \
    if ((Unit) >= (U8)FS_SECSIZE_NUM_UNITS) {                                 \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "SECSIZE: Invalid unit number."));  \
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);                                    \
    }
#else
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)
#endif

/*********************************************************************
*
*       ASSERT_DEVICE_IS_SET
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_DEVICE_IS_SET(pInst)                                         \
  if ((pInst)->pDeviceType == NULL) {                                         \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "SECSIZE: Device is not set."));    \
      FS_X_PANIC(FS_ERRCODE_UNKNOWN_DEVICE);                                  \
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
*       SECSIZE_INST
*/
typedef struct {
  U8                     Unit;
  U8                     DeviceUnit;
  U16                    ldBytesPerSector;
  U16                    ldBytesPerSectorStorage;
  U32                    NumSectors;
  U32                  * pSectorBuffer;
  const FS_DEVICE_TYPE * pDeviceType;
#if FS_SECSIZE_ENABLE_ERROR_RECOVERY
  FS_READ_ERROR_DATA     ReadErrorData;
#endif
} SECSIZE_INST;

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static SECSIZE_INST * _apInst[FS_SECSIZE_NUM_UNITS];
static U8             _NumUnits = 0;

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
static int _InitMedium(const SECSIZE_INST * pInst) {
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
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "SECSIZE: Could not initialize storage."));
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
static int _GetDeviceInfo(const SECSIZE_INST * pInst, FS_DEV_INFO * pDeviceInfo) {
  int                    r;
  U8                     DeviceUnit;
  const FS_DEVICE_TYPE * pDeviceType;

  ASSERT_DEVICE_IS_SET(pInst);
  pDeviceType = pInst->pDeviceType;
  DeviceUnit  = pInst->DeviceUnit;
  r = pDeviceType->pfIoCtl(DeviceUnit, FS_CMD_GET_DEVINFO, 0, SEGGER_PTR2PTR(void, pDeviceInfo));
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "SECSIZE: Could not get info from storage."));
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
static int _IoCtl(const SECSIZE_INST * pInst, I32 Cmd, I32 Aux, void * pBuffer) {
  int                    r;
  U8                     DeviceUnit;
  const FS_DEVICE_TYPE * pDeviceType;

  ASSERT_DEVICE_IS_SET(pInst);
  pDeviceType  = pInst->pDeviceType;
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
static int _ReadSectors(const SECSIZE_INST * pInst, U32 SectorIndex, void * pBuffer, U32 NumSectors) {
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
*       _ReadApplyDeviceInfo
*
*  Function description
*    Reads information from the storage devices and computes the driver parameters.
*/
static int _ReadApplyDeviceInfo(SECSIZE_INST * pInst) {
  int           r;
  U16           ldBytesPerSector;
  U16           ldBytesPerSectorStorage;
  U16           BytesPerSectorStorage;
  U32         * pSectorBuffer;
  U32           NumSectors;
  U32           NumSectorsStorage;
  U16           ldSectorsPerSector;
  FS_DEV_INFO   DeviceInfo;

  //
  // Read the information about the storage device.
  //
  r = _GetDeviceInfo(pInst, &DeviceInfo);
  if (r == 0) {
    BytesPerSectorStorage   = DeviceInfo.BytesPerSector;
    NumSectorsStorage       = DeviceInfo.NumSectors;
    ldBytesPerSector        = pInst->ldBytesPerSector;
    ldBytesPerSectorStorage = (U16)_ld(BytesPerSectorStorage);
    pSectorBuffer           = pInst->pSectorBuffer;
    //
    // If the sector size of storage device is larger than the driver sector size
    // we have to allocate a buffer for the read-modify-write operations.
    //
    if (ldBytesPerSectorStorage > ldBytesPerSector) {
      FS_ALLOC_ZEROED_PTR(SEGGER_PTR2PTR(void *, &pSectorBuffer), (I32)BytesPerSectorStorage, "SECSIZE_BUFFER");
      if (pSectorBuffer == NULL) {
        r = 1;                        // Error, could not allocate memory for the sector buffer.
      }
      ldSectorsPerSector = ldBytesPerSectorStorage - ldBytesPerSector;
      NumSectors         = NumSectorsStorage << ldSectorsPerSector;
    } else {
      ldSectorsPerSector = ldBytesPerSector - ldBytesPerSectorStorage;
      NumSectors         = NumSectorsStorage >> ldSectorsPerSector;
    }
    pInst->ldBytesPerSectorStorage = ldBytesPerSectorStorage;
    pInst->pSectorBuffer           = pSectorBuffer;
    pInst->NumSectors              = NumSectors;
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
static int _ReadApplyDeviceInfoIfRequired(SECSIZE_INST * pInst) {
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
static int _GetStatus(const SECSIZE_INST * pInst) {
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
static int _WriteSectors(const SECSIZE_INST * pInst, U32 SectorIndex, const void * pBuffer, U32 NumSectors, U8 RepeatSame) {
  int                    r;
  U8                     DeviceUnit;
  const FS_DEVICE_TYPE * pDeviceType;

  ASSERT_DEVICE_IS_SET(pInst);
  pDeviceType = pInst->pDeviceType;
  DeviceUnit  = pInst->DeviceUnit;
  r = pDeviceType->pfWrite(DeviceUnit, SectorIndex, pBuffer, NumSectors, RepeatSame);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "SECSIZE: Could not write sectors to storage."));
  }
  return r;
}

#if FS_SECSIZE_ENABLE_ERROR_RECOVERY

/*********************************************************************
*
*       _FindInst
*
*  Function description
*    Searches for a driver instance by storage device.
*    Typ. called to associate a read error signaled via callback
*    to a driver instance.
*/
static SECSIZE_INST * _FindInst(const FS_DEVICE_TYPE * pDeviceType, U32 DeviceUnit) {
  unsigned       Unit;
  SECSIZE_INST * pInst;

  for (Unit = 0; Unit < _NumUnits; ++Unit) {
    pInst = _apInst[Unit];
    if ((pInst->pDeviceType == pDeviceType) &&
        (pInst->DeviceUnit  == DeviceUnit)) {
      return pInst;
    }
  }
  return NULL;       // No matching instance found.
}

#endif

/*********************************************************************
*
*       _DriverToStorageSectorIndex
*
*  Function description
*    Converts a driver sector index to a storage sector index.
*/
static U32 _DriverToStorageSectorIndex(const SECSIZE_INST * pInst, U32 SectorIndex) {
  U16 ldBytesPerSector;
  U16 ldBytesPerSectorStorage;
  U16 ldSectorsPerSector;

  ldBytesPerSector        = pInst->ldBytesPerSector;
  ldBytesPerSectorStorage = pInst->ldBytesPerSectorStorage;
  if (ldBytesPerSector >= ldBytesPerSectorStorage) {
    ldSectorsPerSector   = ldBytesPerSector - ldBytesPerSectorStorage;
    SectorIndex        <<= ldSectorsPerSector;
  } else {
    ldSectorsPerSector = ldBytesPerSectorStorage - ldBytesPerSector;
    SectorIndex        = SectorIndex >> ldSectorsPerSector;
  }
  return SectorIndex;
}

/*********************************************************************
*
*       _DriverToStorageSectorRange
*
*  Function description
*    Converts a driver sector range to a storage sector range.
*/
static void _DriverToStorageSectorRange(const SECSIZE_INST * pInst, U32 * pSectorIndex, U32 * pNumSectors) {
  U16 ldBytesPerSector;
  U16 ldBytesPerSectorStorage;
  U16 ldSectorsPerSector;
  U32 SectorsPerSector;
  U32 SectorIndex;
  U32 NumSectors;
  U32 SectorOff;

  ldBytesPerSector        = pInst->ldBytesPerSector;
  ldBytesPerSectorStorage = pInst->ldBytesPerSectorStorage;
  SectorIndex             = *pSectorIndex;
  NumSectors              = *pNumSectors;
  if (ldBytesPerSector >= ldBytesPerSectorStorage) {
    ldSectorsPerSector   = ldBytesPerSector - ldBytesPerSectorStorage;
    SectorIndex        <<= ldSectorsPerSector;
    NumSectors         <<= ldSectorsPerSector;
  } else {
    ldSectorsPerSector = ldBytesPerSectorStorage - ldBytesPerSector;
    SectorsPerSector   = 1uL << ldSectorsPerSector;
    SectorOff          = SectorIndex & (SectorsPerSector - 1u);
    SectorIndex        = (SectorIndex + SectorsPerSector - 1u) >> ldSectorsPerSector;  // Round up to next sector index.
    if (NumSectors < SectorsPerSector) {
      NumSectors = 0;
    } else {
      if (SectorOff != 0u) {
        NumSectors -= SectorsPerSector - SectorOff;
      }
      NumSectors >>= ldSectorsPerSector;
    }
  }
  *pSectorIndex = SectorIndex;
  *pNumSectors  = NumSectors;
}

#if FS_SECSIZE_ENABLE_ERROR_RECOVERY

/*********************************************************************
*
*       _cbOnReadError
*
*  Function description
*    Function to be called by the driver when a read error occurs.
*
*  Parameters
*    pDeviceType    Type of storage device which encountered the read error.
*    DeviceUnit     Unit number of the storage device where the read error occurred.
*    SectorIndex    Index of the sector where the read error occurred.
*    pBuffer        [OUT] Corrected sector data.
*    NumSectors     Number of sectors on which the read error occurred.
*
*  Return value
*    ==0    Data for the requested sector returned.
*    !=0    An error occurred.
*/
static int _cbOnReadError(const FS_DEVICE_TYPE * pDeviceType, U32 DeviceUnit, U32 SectorIndex, void * pBuffer, U32 NumSectors) {
  int            r;
  U8             Unit;
  U16            ldBytesPerSector;
  U16            ldBytesPerSectorStorage;
  U16            ldSectorsPerSector;
  U32            SectorsPerSector;
  U32            BytesPerSector;
  U8           * pSectorBuffer;
  U8           * pReadBuffer;
  U32            SectorIndexDriver;
  U32            NumSectorsDriver;
  U32            spsMask;
  U32            SectorOff;
  U32            ByteOff;
  U32            NumBytes;
  U32            NumSectorsAtOnce;
  SECSIZE_INST * pInst;

  pInst = _FindInst(pDeviceType, DeviceUnit);
  if (pInst == NULL) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "SECSIZE: No matching instance found."));
    return 1;               // Error, driver instance not found.
  }
  Unit                    = pInst->Unit;
  ldBytesPerSector        = pInst->ldBytesPerSector;
  ldBytesPerSectorStorage = pInst->ldBytesPerSectorStorage;
  //
  // Read directly to storage buffer if the sector size of the
  // storage is larger than the sector size of driver.
  //
  if (ldBytesPerSectorStorage >= ldBytesPerSector) {
    ldSectorsPerSector = ldBytesPerSectorStorage - ldBytesPerSector;
    SectorIndexDriver  = SectorIndex << ldSectorsPerSector;
    NumSectorsDriver   = NumSectors  << ldSectorsPerSector;
    r = pInst->ReadErrorData.pfCallback(&FS_SECSIZE_Driver, Unit, SectorIndexDriver, pBuffer, NumSectorsDriver);
    return r;
  }
  //
  // Sector size of driver is larger than the sector size of storage.
  // First, read the sector data into the internal buffer and then copy it to storage buffer.
  //
  ldSectorsPerSector = ldBytesPerSector - ldBytesPerSectorStorage;
  SectorsPerSector   = 1uL << ldSectorsPerSector;
  pSectorBuffer      = SEGGER_PTR2PTR(U8, pInst->pSectorBuffer);
  pReadBuffer        = SEGGER_PTR2PTR(U8, pBuffer);
  spsMask            = SectorsPerSector - 1u;
  SectorOff          = SectorIndex & spsMask;
  //
  // Check that the sector buffer is valid.
  //
  if (pSectorBuffer == NULL) {
    return 1;         // Error, invalid sector buffer.
  }
  if (SectorOff != 0u) {
    SectorIndexDriver = SectorIndex >> ldSectorsPerSector;
    r = pInst->ReadErrorData.pfCallback(&FS_SECSIZE_Driver, Unit, SectorIndexDriver, pSectorBuffer, 1);
    if (r != 0) {
      return 1;       // Error, could not read data.
    }
    NumSectorsAtOnce = SectorsPerSector - SectorOff;
    NumSectorsAtOnce = SEGGER_MIN(NumSectorsAtOnce, NumSectors);
    ByteOff          = SectorOff << ldBytesPerSector;
    NumBytes         = NumSectorsAtOnce << ldBytesPerSector;
    FS_MEMCPY(pReadBuffer, pSectorBuffer + ByteOff, NumBytes);
    NumSectors  -= NumSectorsAtOnce;
    SectorIndex += NumSectorsAtOnce;
    pReadBuffer += NumBytes;
  }
  if (NumSectors == 0u) {
    return 0;         // OK, data read.
  }
  //
  // Read whole driver sectors if possible.
  //
  NumSectorsDriver = NumSectors >> ldSectorsPerSector;
  if (NumSectorsDriver != 0u) {
    SectorIndexDriver = SectorIndex >> ldSectorsPerSector;
    BytesPerSector    = 1uL << ldBytesPerSector;
    do {
      r = pInst->ReadErrorData.pfCallback(&FS_SECSIZE_Driver, Unit, SectorIndexDriver, pReadBuffer, 1);
      if (r != 0) {
        return 1;     // Error, could not read data.
      }
      ++SectorIndexDriver;
      pReadBuffer += BytesPerSector;
      NumSectors  -= SectorsPerSector;
      SectorIndex += SectorsPerSector;
    } while (--NumSectorsDriver != 0u);
  }
  if (NumSectors == 0u) {
    return 0;         // OK, data read.
  }
  //
  // Read the remaining sectors which are not multiple of driver sector size.
  //
  SectorIndexDriver = SectorIndex >> ldSectorsPerSector;
  r = pInst->ReadErrorData.pfCallback(&FS_SECSIZE_Driver, Unit, SectorIndexDriver, pSectorBuffer, 1);
  if (r != 0) {
    return 1;         // Error, could not read data.
  }
  NumBytes = NumSectors << ldBytesPerSector;
  FS_MEMCPY(pReadBuffer, pSectorBuffer, NumBytes);
  return 0;
}

#endif // FS_SECSIZE_ENABLE_ERROR_RECOVERY

/*********************************************************************
*
*       _GetInst
*/
static SECSIZE_INST * _GetInst(U8 Unit) {
  SECSIZE_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  pInst = NULL;
  if (Unit < (U8)FS_SECSIZE_NUM_UNITS) {
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
*       _SECSIZE_GetDriverName
*
*   Function description
*     FS driver function. Returns the driver name.
*/
static const char * _SECSIZE_GetDriverName(U8 Unit) {
  FS_USE_PARA(Unit);
  return "secsize";
}

/*********************************************************************
*
*       _SECSIZE_AddDevice
*
*   Function description
*     FS driver function. Creates a driver instance.
*/
static int _SECSIZE_AddDevice(void) {
  SECSIZE_INST * pInst;
  U8             Unit;

  if (_NumUnits >= (U8)FS_SECSIZE_NUM_UNITS) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "SECSIZE: _SECSIZE_AddDevice: Too many instances."));
    return -1;                      // Error, too many instances defined.
  }
  Unit  = (U8)_NumUnits;
  pInst = _apInst[Unit];
  if (pInst == NULL) {
    FS_ALLOC_ZEROED_PTR(SEGGER_PTR2PTR(void *, &pInst), (I32)sizeof(SECSIZE_INST), "SECSIZE_INST");
    if (pInst == NULL) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "SECSIZE: _SECSIZE_AddDevice: Not enough memory."));
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
*       _SECSIZE_Read
*
*   Function description
*     FS driver function. Reads a number of sectors from storage medium.
*/
static int _SECSIZE_Read(U8 Unit, U32 SectorIndex, void * pBuffer, U32 NumSectors) {
  int            r;
  U16            ldBytesPerSector;
  U16            ldBytesPerSectorStorage;
  U16            ldSectorsPerSector;
  U32            SectorsPerSector;
  U32            spsMask;
  U8           * pSectorBuffer;
  U8           * pReadBuffer;
  U32            SectorIndexStorage;
  U32            NumSectorsStorage;
  U32            NumSectorsAtOnce;
  U32            SectorOff;
  U32            ByteOff;
  U32            NumBytes;
  SECSIZE_INST * pInst;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;               // Error, instance not found.
  }
  ldBytesPerSector        = pInst->ldBytesPerSector;
  ldBytesPerSectorStorage = pInst->ldBytesPerSectorStorage;
  //
  // Read the sector data directly into the output buffer if the size of the driver sector
  // is greater than the sector size of storage device.
  //
  if (ldBytesPerSector >= ldBytesPerSectorStorage) {
    ldSectorsPerSector = ldBytesPerSector - ldBytesPerSectorStorage;
    SectorIndexStorage = SectorIndex << ldSectorsPerSector;
    NumSectorsStorage  = NumSectors  << ldSectorsPerSector;
    r = _ReadSectors(pInst, SectorIndexStorage, pBuffer, NumSectorsStorage);
    return r;
  }
  //
  // The driver sector size is smaller than the sector size of storage device.
  //
  pSectorBuffer      = SEGGER_PTR2PTR(U8, pInst->pSectorBuffer);
  pReadBuffer        = SEGGER_PTR2PTR(U8, pBuffer);
  ldSectorsPerSector = ldBytesPerSectorStorage - ldBytesPerSector;
  SectorsPerSector   = 1uL << ldSectorsPerSector;
  spsMask            = SectorsPerSector - 1u;
  //
  // Check that the sector buffer is valid.
  //
  if (pSectorBuffer == NULL) {
    return 1;               // Error, invalid sector buffer.
  }
  //
  // Read the first not aligned sectors to internal sector buffer
  // and then copy them to output buffer.
  //
  SectorOff = SectorIndex & spsMask;
  if (SectorOff != 0u) {
    SectorIndexStorage = SectorIndex >> ldSectorsPerSector;
    r = _ReadSectors(pInst, SectorIndexStorage, pSectorBuffer, 1);
    if (r != 0) {
      return 1;             // Error, could not read sectors.
    }
    NumSectorsAtOnce = SectorsPerSector - SectorOff;
    NumSectorsAtOnce = SEGGER_MIN(NumSectorsAtOnce, NumSectors);
    ByteOff          = SectorOff << ldBytesPerSector;
    NumBytes         = NumSectorsAtOnce << ldBytesPerSector;
    FS_MEMCPY(pReadBuffer, pSectorBuffer + ByteOff, NumBytes);
    NumSectors  -= NumSectorsAtOnce;
    SectorIndex += NumSectorsAtOnce;
    pReadBuffer += NumBytes;
  }
  if (NumSectors == 0u) {
    return 0;               // OK, all sectors read.
  }
  //
  // The sector index is aligned. Read the sector data directly into the output buffer.
  //
  NumSectorsAtOnce = NumSectors & ~spsMask;
  if (NumSectorsAtOnce != 0u) {
    SectorIndexStorage = SectorIndex      >> ldSectorsPerSector;
    NumSectorsStorage  = NumSectorsAtOnce >> ldSectorsPerSector;
    r = _ReadSectors(pInst, SectorIndexStorage, pReadBuffer, NumSectorsStorage);
    if (r != 0) {
      return 1;             // Error, could not read sectors.
    }
    NumSectors  -= NumSectorsAtOnce;
    SectorIndex += NumSectorsAtOnce;
    pReadBuffer += NumSectorsAtOnce << ldBytesPerSector;
  }
  if (NumSectors == 0u) {
    return 0;               // OK, all sectors read.
  }
  //
  // Read the remaining sectors to internal sector buffer first
  // and then copy them to output buffer.
  //
  SectorIndexStorage = SectorIndex >> ldSectorsPerSector;
  r = _ReadSectors(pInst, SectorIndexStorage, pSectorBuffer, 1);
  if (r != 0) {
    return 1;                // Error, could not read sectors.
  }
  NumBytes = NumSectors << ldBytesPerSector;
  FS_MEMCPY(pReadBuffer, pSectorBuffer, NumBytes);
  return 0;                 // OK, all sectors read.
}

/*********************************************************************
*
*       _SECSIZE_Write
*
*   Function description
*     FS driver function. Writes a number of sectors to storage medium.
*/
static int _SECSIZE_Write(U8 Unit, U32 SectorIndex, const void * pBuffer, U32 NumSectors, U8 RepeatSame) {
  int            r;
  U16            ldBytesPerSector;
  U16            ldBytesPerSectorStorage;
  U16            ldSectorsPerSector;
  U16            BytesPerSector;
  U32            SectorsPerSector;
  U32            spsMask;
  U8           * pSectorBuffer;
  const U8     * pWriteBuffer;
  U32            SectorIndexStorage;
  U32            NumSectorsStorage;
  U32            NumSectorsAtOnce;
  U32            SectorOff;
  U32            ByteOff;
  U32            NumBytes;
  U8           * p;
  const U8     * pData;
  SECSIZE_INST * pInst;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;               // Error, instance not found.
  }
  ldBytesPerSector        = pInst->ldBytesPerSector;
  ldBytesPerSectorStorage = pInst->ldBytesPerSectorStorage;
  BytesPerSector          = (U16)(1uL << ldBytesPerSector);
  //
  // Write the sector data directly to storage device if the size of the driver sector
  // is greater than the sector size of storage device.
  //
  if (ldBytesPerSector >= ldBytesPerSectorStorage) {
    ldSectorsPerSector = ldBytesPerSector - ldBytesPerSectorStorage;
    SectorIndexStorage = SectorIndex << ldSectorsPerSector;
    NumSectorsStorage  = NumSectors  << ldSectorsPerSector;
    r = _WriteSectors(pInst, SectorIndexStorage, pBuffer, NumSectorsStorage, RepeatSame);
    return r;
  }
  //
  // The driver sector size is smaller than the sector size of storage device.
  //
  pSectorBuffer      = SEGGER_PTR2PTR(U8, pInst->pSectorBuffer);
  pWriteBuffer       = SEGGER_CONSTPTR2PTR(const U8, pBuffer);
  ldSectorsPerSector = ldBytesPerSectorStorage - ldBytesPerSector;
  SectorsPerSector   = 1uL << ldSectorsPerSector;
  spsMask            = SectorsPerSector - 1u;
  //
  // Check that the sector buffer is valid.
  //
  if (pSectorBuffer == NULL) {
    return 1;               // Error, invalid sector buffer.
  }
  //
  // Read the first not aligned sectors to internal sector buffer,
  // modify the sector data and then write the sector data back to storage device.
  //
  SectorOff = SectorIndex & spsMask;
  if (SectorOff != 0u) {
    SectorIndexStorage = SectorIndex >> ldSectorsPerSector;
    r = _ReadSectors(pInst, SectorIndexStorage, pSectorBuffer, 1);
    if (r != 0) {
      return 1;             // Error, could not read sectors.
    }
    NumSectorsAtOnce = SectorsPerSector - SectorOff;
    NumSectorsAtOnce = SEGGER_MIN(NumSectorsAtOnce, NumSectors);
    ByteOff          = SectorOff << ldBytesPerSector;
    NumBytes         = NumSectorsAtOnce << ldBytesPerSector;
    p                = pSectorBuffer + ByteOff;
    if (RepeatSame != 0u) {
      do {
        FS_MEMCPY(p, pBuffer, BytesPerSector);
        p        += BytesPerSector;
        NumBytes -= BytesPerSector;
      } while (NumBytes != 0u);
    } else {
      FS_MEMCPY(p, pWriteBuffer, NumBytes);
      pWriteBuffer += NumBytes;
    }
    NumSectors  -= NumSectorsAtOnce;
    SectorIndex += NumSectorsAtOnce;
    r = _WriteSectors(pInst, SectorIndexStorage, pSectorBuffer, 1, 0);
    if (r != 0) {
      return 1;             // Error, could not write sectors.
    }
  }
  if (NumSectors == 0u) {
    return 0;               // OK, all sectors written.
  }
  //
  // The sector index is aligned. Write the sector data directly to storage device.
  //
  NumSectorsAtOnce = NumSectors & ~spsMask;
  if (NumSectorsAtOnce != 0u) {
    SectorIndexStorage = SectorIndex      >> ldSectorsPerSector;
    NumSectorsStorage  = NumSectorsAtOnce >> ldSectorsPerSector;
    if (RepeatSame != 0u) {
      p        = pSectorBuffer;
      NumBytes = (U32)SectorsPerSector << ldBytesPerSector;
      do {
        FS_MEMCPY(p, pBuffer, BytesPerSector);
        p        += BytesPerSector;
        NumBytes -= BytesPerSector;
      } while (NumBytes != 0u);
      pData = pSectorBuffer;
    } else {
      pData = pWriteBuffer;
    }
    r = _WriteSectors(pInst, SectorIndexStorage, pData, NumSectorsStorage, RepeatSame);
    if (r != 0) {
      return 1;             // Error, could not write sectors.
    }
    NumSectors  -= NumSectorsAtOnce;
    SectorIndex += NumSectorsAtOnce;
    pWriteBuffer += NumSectorsAtOnce << ldBytesPerSector;
  }
  if (NumSectors == 0u) {
    return 0;               // OK, all sectors read.
  }
  //
  // Read the remaining sectors to internal sector buffer first,
  // modify them and then write them to storage device.
  //
  SectorIndexStorage = SectorIndex >> ldSectorsPerSector;
  r = _ReadSectors(pInst, SectorIndexStorage, pSectorBuffer, 1);
  if (r != 0) {
    return 1;                // Error, could not read sectors.
  }
  NumBytes = NumSectors << ldBytesPerSector;
  if (RepeatSame != 0u) {
    p = pSectorBuffer;
    do {
      FS_MEMCPY(p, pBuffer, BytesPerSector);
      p        += BytesPerSector;
      NumBytes -= BytesPerSector;
    } while (NumBytes != 0u);
  } else {
    FS_MEMCPY(pSectorBuffer, pWriteBuffer, NumBytes);
  }
  r = _WriteSectors(pInst, SectorIndexStorage, pSectorBuffer, 1, 0);
  return r;
}

/*********************************************************************
*
*       _SECSIZE_IoCtl
*
*   Function description
*     FS driver function. Executes an I/O control command.
*/
static int _SECSIZE_IoCtl(U8 Unit, I32 Cmd, I32 Aux, void * pBuffer) {
  SECSIZE_INST       * pInst;
  int                  r;
  FS_DEV_INFO        * pDevInfo;
  int                  RelayCmd;
#if FS_SECSIZE_ENABLE_ERROR_RECOVERY
  FS_READ_ERROR_DATA   ReadErrorData;
#endif
  U32                  NumSectors;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return -1;                // Error, instance not found.
  }
  FS_USE_PARA(Aux);
  r        = -1;              // Set to indicate an error.
  RelayCmd = 1;               // By default, pass the commands to the underlying driver
  switch (Cmd) {
  case FS_CMD_GET_DEVINFO:
    r = _ReadApplyDeviceInfoIfRequired(pInst);
    if (r == 0) {
      if (pBuffer != NULL) {
        U16 BytesPerSector;

        BytesPerSector = (U16)(1uL << pInst->ldBytesPerSector);
        NumSectors     = pInst->NumSectors;
        if (NumSectors == 0u) {
          BytesPerSector = 0;
        }
        pDevInfo = SEGGER_PTR2PTR(FS_DEV_INFO, pBuffer);
        pDevInfo->NumSectors     = NumSectors;
        pDevInfo->BytesPerSector = BytesPerSector;
      }
    }
    RelayCmd = 0;             // Command is handled by the driver.
    break;
#if FS_SUPPORT_DEINIT
  case FS_CMD_DEINIT:
    {
      U32 * p;

      r = _IoCtl(pInst, Cmd, Aux, pBuffer);
      RelayCmd = 0;           // Command is handled by the driver.
      p = pInst->pSectorBuffer;
      if (p != NULL) {
        FS_FREE(p);
      }
      FS_FREE(pInst);
      pInst = NULL;
      _apInst[Unit] = NULL;
      _NumUnits--;
    }
    break;
#endif
  case FS_CMD_GET_SECTOR_USAGE:
    {
      U32 SectorIndex;

      SectorIndex = (U32)Aux;
      SectorIndex = _DriverToStorageSectorIndex(pInst, SectorIndex);
      Aux         = (I32)SectorIndex;
    }
    break;
  case FS_CMD_FREE_SECTORS:
    {
      U32 SectorIndex;

      SectorIndex = (U32)Aux;
      NumSectors  = *SEGGER_PTR2PTR(U32, pBuffer);
      _DriverToStorageSectorRange(pInst, &SectorIndex, &NumSectors);
      if (NumSectors != 0u) {
        Aux     = (I32)SectorIndex;
        pBuffer = SEGGER_PTR2PTR(void, &NumSectors);
      } else {
        RelayCmd = 0;
        r = 0;
      }
    }
    break;
  case FS_CMD_UNMOUNT:
    // through
  case FS_CMD_UNMOUNT_FORCED:
    pInst->NumSectors = 0;
    break;
#if FS_SECSIZE_ENABLE_ERROR_RECOVERY
  case FS_CMD_SET_READ_ERROR_CALLBACK:
    if (pBuffer != NULL) {
      FS_READ_ERROR_DATA * pReadErrorData;
      U16                  ldBytesPerSector;
      U16                  ldBytesPerSectorStorage;

      //
      // Register our callback function instead of the received one
      // in order to be able to convert the sector index.
      //
      pReadErrorData           = SEGGER_PTR2PTR(FS_READ_ERROR_DATA, pBuffer);
      pInst->ReadErrorData     = *pReadErrorData;       // struct copy
      ReadErrorData.pfCallback = _cbOnReadError;
      pBuffer                  = SEGGER_PTR2PTR(void, &ReadErrorData);
      //
      // Allocate a read buffer for the callback function.
      //
      ldBytesPerSector        = pInst->ldBytesPerSector;
      ldBytesPerSectorStorage = pInst->ldBytesPerSectorStorage;
      if (ldBytesPerSector > ldBytesPerSectorStorage) {
        U32    NumBytes;
        void * p;

        NumBytes = 1uL << ldBytesPerSector;
        p        = NULL;
        FS_ALLOC_ZEROED_PTR(&p, (I32)NumBytes, "SECSIZE_BUFFER_ER");
        if (p == NULL) {
          r = -1;             // Error, could not allocate memory for the sector buffer.
        }
        pInst->pSectorBuffer = SEGGER_PTR2PTR(U32, p);
      }
    }
    break;
#endif // FS_SECSIZE_ENABLE_ERROR_RECOVERY
  default:
    //
    // All other commands are relayed to the underlying driver(s).
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
*       _SECSIZE_InitMedium
*
*   Function description
*     FS driver function. Initializes the storage medium.
*/
static int _SECSIZE_InitMedium(U8 Unit) {
  int            r;
  SECSIZE_INST * pInst;

  r     = 1;                  // Set to indicate an error.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    r = _InitMedium(pInst);
  }
  return r;
}

/*********************************************************************
*
*       _SECSIZE_GetStatus
*
*   Function description
*     FS driver function. Returns whether the storage media is present or not.
*/
static int _SECSIZE_GetStatus(U8 Unit) {
  SECSIZE_INST * pInst;
  int            Status;

  Status = FS_MEDIA_NOT_PRESENT;    // Set to indicate an error.
  pInst  = _GetInst(Unit);
  if (pInst != NULL) {
    Status = _GetStatus(pInst);
  }
  return Status;
}

/*********************************************************************
*
*       _SECSIZE_GetNumUnits
*
*   Function description
*     FS driver function. Returns the number of driver instances.
*/
static int _SECSIZE_GetNumUnits(void) {
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
*       FS_SECSIZE_Driver
*/
const FS_DEVICE_TYPE FS_SECSIZE_Driver = {
  _SECSIZE_GetDriverName,
  _SECSIZE_AddDevice,
  _SECSIZE_Read,
  _SECSIZE_Write,
  _SECSIZE_IoCtl,
  _SECSIZE_InitMedium,
  _SECSIZE_GetStatus,
  _SECSIZE_GetNumUnits
};

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_SECSIZE_Configure
*
*  Function description
*    Sets the parameters of a driver instance.
*
*  Parameters
*    Unit            Index of the driver instance (0-based).
*    pDeviceType     [IN]  Storage device.
*    DeviceUnit      Unit number of storage device.
*    BytesPerSector  Sector size in bytes presented to upper layer.
*
*  Additional information
*    This function is mandatory and it has to be called once for each
*    instance of the driver. BytesPerSector has to be a power of 2 value.
*/
void FS_SECSIZE_Configure(U8 Unit, const FS_DEVICE_TYPE * pDeviceType, U8 DeviceUnit, U16 BytesPerSector) {
  SECSIZE_INST * pInst;

  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    pInst->DeviceUnit       = DeviceUnit;
    pInst->pDeviceType      = pDeviceType;
    pInst->ldBytesPerSector = (U16)_ld(BytesPerSector);
  }
}

/*************************** End of file ****************************/
