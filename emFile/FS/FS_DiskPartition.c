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
File        : FS_DiskPartition.c
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
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)                                       \
    if ((Unit) >= (U8)FS_DISKPART_NUM_UNITS) {                                   \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "DISKPART: Invalid unit number."));    \
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);                                       \
    }
#else
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)
#endif

/*********************************************************************
*
*       ASSERT_PART_INDEX_IS_IN_RANGE
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_PART_INDEX_IS_IN_RANGE(PartIndex)                                \
  if ((PartIndex) >= 4u) {                                                        \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "DISKPART: Invalid partition index.")); \
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);                                        \
    }
#else
  #define ASSERT_PART_INDEX_IS_IN_RANGE(PartIndex)
#endif

/*********************************************************************
*
*       ASSERT_SECTORS_ARE_IN_RANGE
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_SECTORS_ARE_IN_RANGE(pInst, SectorIndex, NumSectors)             \
  if (((SectorIndex) >= (pInst)->NumSectors) ||                                   \
     (((SectorIndex) + (NumSectors)) > (pInst)->NumSectors)) {                    \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "DISKPART: Invalid sector index."));    \
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);                                        \
    }
#else
  #define ASSERT_SECTORS_ARE_IN_RANGE(pInst, SectorIndex, NumSectors)
#endif

/*********************************************************************
*
*       ASSERT_DEVICE_IS_SET
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_DEVICE_IS_SET(pInst)                                            \
    if ((pInst)->pDeviceType == NULL) {                                          \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "DISKPART: Device not set."));         \
      FS_X_PANIC(FS_ERRCODE_UNKNOWN_DEVICE);                                     \
    }
#else
  #define ASSERT_DEVICE_IS_SET(Unit)
#endif

/*********************************************************************
*
*       CALL_TEST_HOOK_SECTOR_READ_BEGIN
*/
#if FS_SUPPORT_TEST
  #define CALL_TEST_HOOK_SECTOR_READ_BEGIN(pDeviceType, DeviceUnit, pSectorIndex, pData, pNumSectors)                       _CallTestHookSectorReadBegin(pDeviceType, DeviceUnit, pSectorIndex, pData, pNumSectors)
#else
  #define CALL_TEST_HOOK_SECTOR_READ_BEGIN(pDeviceType, DeviceUnit, pSectorIndex, pData, pNumSectors)
#endif

/*********************************************************************
*
*       CALL_TEST_HOOK_SECTOR_READ_END
*/
#if FS_SUPPORT_TEST
  #define CALL_TEST_HOOK_SECTOR_READ_END(pDeviceType, DeviceUnit, SectorIndex, pData, NumSectors, pResult)                  _CallTestHookSectorReadEnd(pDeviceType, DeviceUnit, SectorIndex, pData, NumSectors, pResult)
#else
  #define CALL_TEST_HOOK_SECTOR_READ_END(pDeviceType, DeviceUnit, SectorIndex, pData, NumSectors, pResult)
#endif

/*********************************************************************
*
*       CALL_TEST_HOOK_SECTOR_WRITE_BEGIN
*/
#if FS_SUPPORT_TEST
  #define CALL_TEST_HOOK_SECTOR_WRITE_BEGIN(pDeviceType, DeviceUnit, pSectorIndex, ppData, pNumSectors, pRepeatSame)        _CallTestHookSectorWriteBegin(pDeviceType, DeviceUnit, pSectorIndex, ppData, pNumSectors, pRepeatSame)
#else
  #define CALL_TEST_HOOK_SECTOR_WRITE_BEGIN(pDeviceType, DeviceUnit, pSectorIndex, ppData, pNumSectors, pRepeatSame)
#endif

/*********************************************************************
*
*       CALL_TEST_HOOK_SECTOR_WRITE_END
*/
#if FS_SUPPORT_TEST
  #define CALL_TEST_HOOK_SECTOR_WRITE_END(pDeviceType, DeviceUnit, pSectorIndex, pData, NumSectors, RepeatSame, pResult)    _CallTestHookSectorWriteEnd(pDeviceType, DeviceUnit, SectorIndex, pData, NumSectors, RepeatSame, pResult)
#else
  #define CALL_TEST_HOOK_SECTOR_WRITE_END(pDeviceType, DeviceUnit, pSectorIndex, pData, NumSectors, RepeatSame, pResult)
#endif

/*********************************************************************
*
*       Local data types
*
**********************************************************************
*/
typedef struct {
  U8                     Unit;
  U8                     DeviceUnit;
  U8                     PartIndex;
  U8                     HasError;
  const FS_DEVICE_TYPE * pDeviceType;
  U32                    StartSector;
  U32                    NumSectors;
  U16                    BytesPerSector;
#if FS_DISKPART_SUPPORT_ERROR_RECOVERY
  FS_READ_ERROR_DATA     ReadErrorData;         // Function to be called when a bit error occurs to get corrected data.
#endif
} DISKPART_INST;

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static DISKPART_INST                             * _apInst[FS_DISKPART_NUM_UNITS];
static U8                                          _NumUnits = 0;
#if FS_SUPPORT_TEST
  static FS_STORAGE_TEST_HOOK_SECTOR_READ_BEGIN  * _pfTestHookSectorReadBegin;
  static FS_STORAGE_TEST_HOOK_SECTOR_READ_END    * _pfTestHookSectorReadEnd;
  static FS_STORAGE_TEST_HOOK_SECTOR_WRITE_BEGIN * _pfTestHookSectorWriteBegin;
  static FS_STORAGE_TEST_HOOK_SECTOR_WRITE_END   * _pfTestHookSectorWriteEnd;
#endif // FS_SUPPORT_TEST

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

#if FS_SUPPORT_TEST

/*********************************************************************
*
*       _CallTestHookSectorReadBegin
*/
static void _CallTestHookSectorReadBegin(const FS_DEVICE_TYPE * pDeviceType, U8 DeviceUnit, U32 * pSectorIndex, void * pData, U32 * pNumSectors) {
  if (_pfTestHookSectorReadBegin != NULL) {
    _pfTestHookSectorReadBegin(pDeviceType, DeviceUnit, pSectorIndex, pData, pNumSectors);
  }
}

/*********************************************************************
*
*       _CallTestHookSectorReadEnd
*/
static void _CallTestHookSectorReadEnd(const FS_DEVICE_TYPE * pDeviceType, U8 DeviceUnit, U32 SectorIndex, void * pData, U32 NumSectors, int * pResult) {
  if (_pfTestHookSectorReadEnd != NULL) {
    _pfTestHookSectorReadEnd(pDeviceType, DeviceUnit, SectorIndex, pData, NumSectors, pResult);
  }
}

/*********************************************************************
*
*       _CallTestHookSectorWriteBegin
*/
static void _CallTestHookSectorWriteBegin(const FS_DEVICE_TYPE * pDeviceType, U8 DeviceUnit, U32 * pSectorIndex, const void ** ppData, U32 * pNumSectors, U8 * pRepeatSame) {
  if (_pfTestHookSectorWriteBegin != NULL) {
    _pfTestHookSectorWriteBegin(pDeviceType, DeviceUnit, pSectorIndex, ppData, pNumSectors, pRepeatSame);
  }
}

/*********************************************************************
*
*       _CallTestHookSectorWriteEnd
*/
static void _CallTestHookSectorWriteEnd(const FS_DEVICE_TYPE * pDeviceType, U8 DeviceUnit, U32 SectorIndex, const void * pData, U32 NumSectors, U8 RepeatSame, int * pResult) {
  if (_pfTestHookSectorWriteEnd != NULL) {
    _pfTestHookSectorWriteEnd(pDeviceType, DeviceUnit, SectorIndex, pData, NumSectors, RepeatSame, pResult);
  }
}

#endif // FS_SUPPORT_TEST

/*********************************************************************
*
*       _InitMedium
*
*  Function description
*    Initializes the underlying driver.
*/
static int _InitMedium(const DISKPART_INST * pInst) {
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
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "DISKPART: Could not initialize the storage device."));
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
static int _GetDeviceInfo(const DISKPART_INST * pInst, FS_DEV_INFO * pDevInfo) {
  int                    r;
  U8                     DeviceUnit;
  const FS_DEVICE_TYPE * pDeviceType;

  pDeviceType = pInst->pDeviceType;
  DeviceUnit  = pInst->DeviceUnit;
  r = pDeviceType->pfIoCtl(DeviceUnit, FS_CMD_GET_DEVINFO, 0, SEGGER_PTR2PTR(void, pDevInfo));            // MISRA deviation D:100[f]
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "DISKPART: Could not get storage info."));
  }
  return r;
}

/*********************************************************************
*
*       _ReadSectors
*
*  Function description
*    Reads one or more logical sectors from storage device.
*
*  Parameters
*    pInst          Driver instance.
*    SectorIndex    Index of the first logical sector to be read.
*    pData          [OUT] Read logical sector data.
*    NumSectors     Number of logical sectors to be read.
*
*  Return value
*    ==0      OK, logical sectors read successfully.
*    !=0      An error occurred.
*
*  Additional information
*    SectorIndex is relative to the beginning of the partition
*    accessed by this instance of the driver.
*/
static int _ReadSectors(const DISKPART_INST * pInst, U32 SectorIndex, void * pData, U32 NumSectors) {
  int                    r;
  U32                    StartSector;
  U8                     DeviceUnit;
  const FS_DEVICE_TYPE * pDeviceType;

  if (pInst->HasError != 0u) {
    return 1;
  }
  ASSERT_SECTORS_ARE_IN_RANGE(pInst, SectorIndex, NumSectors);
  ASSERT_DEVICE_IS_SET(pInst);
  pDeviceType  = pInst->pDeviceType;
  DeviceUnit   = pInst->DeviceUnit;
  StartSector  = pInst->StartSector;
  SectorIndex += StartSector;
  CALL_TEST_HOOK_SECTOR_READ_BEGIN(pDeviceType, DeviceUnit, &SectorIndex, pData, &NumSectors);
  r = pDeviceType->pfRead(DeviceUnit, SectorIndex, pData, NumSectors);
  CALL_TEST_HOOK_SECTOR_READ_END(pDeviceType, DeviceUnit, SectorIndex, pData, NumSectors, &r);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "DISKPART: Could not read sectors."));
  }
  return r;
}

/*********************************************************************
*
*       _ReadOneSectorPart
*
*  Function description
*    Reads one logical sector that contains partitioning information.
*
*  Parameters
*    pInst          Driver instance.
*    SectorIndex    Index of the logical sector to be read.
*    pData          [OUT] Sector data read.
*
*  Return value
*    ==0      OK, partitioning information read.
*    !=0      An error occurred.
*/
static int _ReadOneSectorPart(const DISKPART_INST * pInst, U32 SectorIndex, void * pData) {
  int                     r;
  U8                      DeviceUnit;
  const FS_DEVICE_TYPE  * pDeviceType;
  U32                     NumSectors;

  pDeviceType = pInst->pDeviceType;
  DeviceUnit  = pInst->DeviceUnit;
  NumSectors  = 1;
  CALL_TEST_HOOK_SECTOR_READ_BEGIN(pDeviceType, DeviceUnit, &SectorIndex, pData, &NumSectors);
  r = pDeviceType->pfRead(DeviceUnit, SectorIndex, pData, NumSectors);
  CALL_TEST_HOOK_SECTOR_READ_END(pDeviceType, DeviceUnit, SectorIndex, pData, NumSectors, &r);
  return r;
}

#if FS_SUPPORT_GPT

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
*       _ReadPartitionInfoGPT
*
*  Function description
*    Returns information about a GPT partition.
*
*  Parameters
*    pInst                [IN] Driver instance.
*    pPartInfo            [OUT] Partition information.
*    PartIndex            Index of the partition to query (0-based).
*    pBuffer              Sector buffer.
*    SectorIndexBackup    Index of the logical sector that stores the backup header.
*    BytesPerSector       Number of bytes in a logical sector.
*
*  Return value
*    ==0    OK, partition information read.
*    !=0    An error occurred.
*/
static int _ReadPartitionInfoGPT(const DISKPART_INST * pInst, FS_PARTITION_INFO_GPT * pPartInfo, unsigned PartIndex, U8 * pBuffer, U32 SectorIndexBackup, unsigned BytesPerSector) {
  int      r;
  U32      NumEntries;
  U32      SectorIndexFirstEntry;
  U32      SectorIndexEntry;
  U32      SectorIndex;
  U32      SizeOfEntry;
  U32      crcRead;
  U32      crcCalc;
  U32      NumSectors;
  U32      NumBytesEntryList;
  U32      NumBytes;
  unsigned ldSizeOfEntry;
  unsigned ldBytesPerSector;
  unsigned ldEntriesPerSector;
  int      IsValidMain;
  int      IsValidBackup;

  IsValidMain   = 1;
  IsValidBackup = 1;
  //
  // Read the main GPT header.
  //
  r = _ReadOneSectorPart(pInst, GPT_HEADER_MAIN_SECTOR, pBuffer);
  if (r != 0) {
    FS_DEBUG_WARN((FS_MTYPE_DRIVER, "DISKPART[%d]: _ReadPartitionInfoGPT: Could not read main GPT header.", (int)pInst->Unit));
    IsValidMain = 0;                                                                            // Error, could not read GPT header.
  } else {
    //
    // Check if the main GPT header contains valid data.
    //
    r = FS__CheckGPTHeader(pBuffer, BytesPerSector, GPT_HEADER_MAIN_SECTOR, 0);                 // 0 means that this is the main GPT header.
    if (r != 0) {
      IsValidMain = 0;                                                                          // Error, the main GPT header is not valid.
    } else {
      //
      // OK, the GPT header is valid. Read the information from the specified partition entry.
      //
      SectorIndexFirstEntry = (U32)FS_LoadU64LE(pBuffer + GPT_HEADER_OFF_FIRST_ENTRY_SECTOR);   // The cast is safe because the GPT is located at the beginning of the storage device.
      NumEntries            = FS_LoadU32LE(pBuffer + GPT_HEADER_OFF_NUM_ENTRIES);
      SizeOfEntry           = FS_LoadU32LE(pBuffer + GPT_HEADER_OFF_SIZE_OF_ENTRY);
      crcRead               = FS_LoadU32LE(pBuffer + GPT_HEADER_OFF_CRC_ENTRIES);
      if (PartIndex >= NumEntries) {
        FS_DEBUG_WARN((FS_MTYPE_DRIVER, "DISKPART[%d]: _ReadPartitionInfoGPT: Invalid partition index (%u not in [0, %u]).", (int)pInst->Unit, PartIndex, NumEntries - 1u));
        IsValidMain = 0;                                                                        // Error, invalid partition index.
      } else {
        ldSizeOfEntry      = _ld(SizeOfEntry);
        ldBytesPerSector   = _ld(BytesPerSector);
        ldEntriesPerSector = ldBytesPerSector - ldSizeOfEntry;
        NumBytesEntryList  = NumEntries << ldSizeOfEntry;
        NumSectors         = (NumBytesEntryList + (BytesPerSector - 1u)) >> ldBytesPerSector;
        if (NumSectors == 0u) {
          FS_DEBUG_WARN((FS_MTYPE_DRIVER, "DISKPART[%d]: _ReadPartitionInfoGPT: Invalid entry list.", (int)pInst->Unit));
          IsValidMain = 0;                                                                      // Error, invalid entry list.
        } else {
          //
          // Read the data from all the sectors that store the entry list
          // and calculate the CRC.
          //
          SectorIndexEntry  = PartIndex >> ldEntriesPerSector;
          SectorIndexEntry += SectorIndexFirstEntry;
          SectorIndex       = SectorIndexFirstEntry;
          crcCalc = GPT_CRC_INIT;
          do {
            r = _ReadOneSectorPart(pInst, SectorIndex, pBuffer);
            if (r != 0) {
              IsValidMain = 0;                                                                  // Error, could not read sector data.
              break;
            }
            if (SectorIndex == SectorIndexEntry) {
              r = FS__LoadPartitionInfoGPT(PartIndex, pPartInfo, pBuffer, ldEntriesPerSector, ldSizeOfEntry);
              if (r != 0) {
                IsValidMain = 0;                                                                // Error, could not load entry information.
                break;
              }
            }
            NumBytes = SEGGER_MIN(BytesPerSector, NumBytesEntryList);
            crcCalc = FS_CRC32_Calc(pBuffer, NumBytes, crcCalc);
            NumBytesEntryList -= NumBytes;
            ++SectorIndex;
          } while (--NumSectors != 0u);
          if (IsValidMain != 0) {
            crcCalc ^= GPT_CRC_INIT;
            if (crcCalc != crcRead) {
              FS_DEBUG_WARN((FS_MTYPE_DRIVER, "DISKPART[%d]: _ReadPartitionInfoGPT: Invalid entry list CRC (crcRead: 0x%08X, crcCalc: 0x%08X).", (int)pInst->Unit, crcCalc, crcRead));
              IsValidMain = 0;                                                                  // Error, invalid CRC.
            }
          }
        }
      }
    }
  }
  if (IsValidMain == 0) {
    //
    // The main GPT header is invalid. Try to read the data from the backup GPT header.
    //
    r = _ReadOneSectorPart(pInst, SectorIndexBackup, pBuffer);
    if (r != 0) {
      FS_DEBUG_WARN((FS_MTYPE_DRIVER, "DISKPART[%d]: _ReadPartitionInfoGPT: Could not read backup GPT header.", (int)pInst->Unit));
      IsValidBackup = 0;                                                                        // Error, could not read backup GPT header.
    } else {
      r = FS__CheckGPTHeader(pBuffer, BytesPerSector, SectorIndexBackup, 1);                    // 1 means that this is the backup GPT header.
      if (r != 0) {
        IsValidBackup = 0;                                                                      // Error, the backup GPT header is not valid.
      } else {
        //
        // OK, the GPT header is valid. Read the information from the specified partition entry.
        //
        SectorIndexFirstEntry = (U32)FS_LoadU64LE(pBuffer + GPT_HEADER_OFF_FIRST_ENTRY_SECTOR); // The cast is safe because the GPT is located at the beginning of the storage device.
        NumEntries            = FS_LoadU32LE(pBuffer + GPT_HEADER_OFF_NUM_ENTRIES);
        SizeOfEntry           = FS_LoadU32LE(pBuffer + GPT_HEADER_OFF_SIZE_OF_ENTRY);
        crcRead               = FS_LoadU32LE(pBuffer + GPT_HEADER_OFF_CRC_ENTRIES);
        if (PartIndex >= NumEntries) {
          FS_DEBUG_WARN((FS_MTYPE_DRIVER, "DISKPART[%d]: _ReadPartitionInfoGPT: Invalid partition index (%u not in [0, %u]).", (int)pInst->Unit, PartIndex, NumEntries - 1u));
          IsValidBackup = 0;                                                                    // Error, invalid partition index.
        } else {
          ldSizeOfEntry      = _ld(SizeOfEntry);
          ldBytesPerSector   = _ld(BytesPerSector);
          ldEntriesPerSector = ldBytesPerSector - ldSizeOfEntry;
          NumBytesEntryList  = NumEntries << ldSizeOfEntry;
          NumSectors         = (NumBytesEntryList + (BytesPerSector - 1u)) >> ldBytesPerSector;
          if (NumSectors == 0u) {
            FS_DEBUG_WARN((FS_MTYPE_DRIVER, "DISKPART[%d]: _ReadPartitionInfoGPT: Invalid entry list.", (int)pInst->Unit));
            IsValidBackup = 0;                                                                  // Error, invalid entry list.
          } else {
            //
            // Read the data from all the sectors that store the entry list
            // and calculate the CRC.
            //
            SectorIndexEntry  = PartIndex >> ldEntriesPerSector;
            SectorIndexEntry += SectorIndexFirstEntry;
            SectorIndex       = SectorIndexFirstEntry;
            crcCalc = GPT_CRC_INIT;
            do {
              r = _ReadOneSectorPart(pInst, SectorIndex, pBuffer);
              if (r != 0) {
                IsValidBackup = 0;                                                              // Error, could not read sector data.
                break;
              }
              if (SectorIndex == SectorIndexEntry) {
                r = FS__LoadPartitionInfoGPT(PartIndex, pPartInfo, pBuffer, ldEntriesPerSector, ldSizeOfEntry);
                if (r != 0) {
                  IsValidBackup = 0;                                                            // Error, could not load entry information.
                  break;
                }
              }
              NumBytes = SEGGER_MIN(BytesPerSector, NumBytesEntryList);
              crcCalc = FS_CRC32_Calc(pBuffer, NumBytes, crcCalc);
              NumBytesEntryList -= NumBytes;
              ++SectorIndex;
            } while (--NumSectors != 0u);
            if (IsValidBackup != 0) {
              crcCalc ^= GPT_CRC_INIT;
              if (crcCalc != crcRead) {
                FS_DEBUG_WARN((FS_MTYPE_DRIVER, "DISKPART[%d]: _ReadPartitionInfoGPT: Invalid entry list CRC (crcRead: 0x%08X, crcCalc: 0x%08X).", (int)pInst->Unit, crcCalc, crcRead));
                IsValidBackup = 0;                                                              // Error, invalid CRC.
              }
            }
          }
        }
      }
    }
  }
  if ((IsValidMain == 0) && (IsValidBackup == 0)) {
    return 1;                                                                                   // Error, GPT information not valid.
  }
  return 0;                                                                                     // OK, partition information read.
}

#endif // FS_SUPPORT_GPT

/*********************************************************************
*
*       _ReadPartInfo
*
*  Function description
*    Reads information about the location of the partition from MBR.
*
*  Parameters
*    pInst      Driver instance.
*
*  Return value
*    ==0      OK, partition information read.
*    !=0      An error occurred.
*/
static int _ReadPartInfo(DISKPART_INST * pInst) {
  int                     r;
  U8                      PartIndex;
  U32                     StartSector;
  U32                     NumSectors;
  U16                     BytesPerSector;
  U8                      HasError;
  U32                     NumSectorsDevice;
  U8                    * pBuffer;
  FS_PARTITION_INFO_MBR   PartInfoMBR;
  FS_DEV_INFO             DeviceInfo;
  int                     PartitioningScheme;

  //
  // Initialize local variables.
  //
  PartIndex      = pInst->PartIndex;
  StartSector    = 0;
  NumSectors     = 0;
  BytesPerSector = 0;
  HasError       = 1;               // Set to indicate error until the instance is initialized successfully.
  pBuffer        = NULL;
  //
  // Get information about the storage device.
  //
  r = _GetDeviceInfo(pInst, &DeviceInfo);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "DISKPART[%d]: _ReadPartInfo: Could not device info.", (int)pInst->Unit));
    goto Done;
  }
  BytesPerSector   = DeviceInfo.BytesPerSector;
  NumSectorsDevice = DeviceInfo.NumSectors;
  //
  // Allocate a work buffer.
  //
  pBuffer = FS__AllocSectorBuffer();
  if (pBuffer == NULL) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "DISKPART[%d]: _ReadPartInfo: Could not allocate buffer.", (int)pInst->Unit));
    goto Done;                    // Error, failed to allocate a work buffer.
  }
  //
  // Read the first logical sector of the storage device
  // that typically stores the MBR information.
  //
  r = _ReadOneSectorPart(pInst, MBR_SECTOR_INDEX, pBuffer);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "DISKPART[%d]: _ReadPartInfo: Could not read MBR.", (int)pInst->Unit));
    goto Done;                    // Error, failed to read MBR.
  }
  PartitioningScheme = FS__LoadPartitioningScheme(pBuffer);
#if FS_SUPPORT_GPT
  if (PartitioningScheme == FS_PARTITIONING_SCHEME_GPT) {
    FS_PARTITION_INFO_GPT PartInfoGPT;
    U32                   SectorIndexPart;
    U32                   NumSectorsPart;
    U32                   SectorIndexBackup;

    //
    // Get information from the protective MBR partition.
    //
    FS_MEMSET(&PartInfoMBR, 0, sizeof(PartInfoMBR));
    FS__LoadPartitionInfoMBR(MBR_PROTECTIVE_INDEX, &PartInfoMBR, pBuffer);
    SectorIndexPart = PartInfoMBR.StartSector;
    NumSectorsPart  = PartInfoMBR.NumSectors;
    if (NumSectorsPart == 0xFFFFFFFFuL) {
      if (SectorIndexPart < NumSectorsDevice) {
        NumSectorsPart = NumSectorsDevice - SectorIndexPart;
      }
    }
    SectorIndexBackup = (SectorIndexPart + NumSectorsPart) - 1u;
    //
    // Load information about the configured GPT partition.
    //
    FS_MEMSET(&PartInfoGPT, 0, sizeof(PartInfoGPT));
    r = _ReadPartitionInfoGPT(pInst, &PartInfoGPT, PartIndex, pBuffer, SectorIndexBackup, BytesPerSector);
    if (r != 0) {
      goto Done;
    }
    StartSector = (U32)PartInfoGPT.StartSector;
    NumSectors  = (U32)PartInfoGPT.NumSectors;
  } else
#endif // FS_SUPPORT_GPT
  {
    if (PartitioningScheme == FS_PARTITIONING_SCHEME_MBR) {
      if (PartIndex >= (unsigned)FS_MAX_NUM_PARTITIONS_MBR) {
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "DISKPART[%d]: _ReadPartInfo: Invalid MBR partition index (%u).", (int)pInst->Unit, PartIndex));
        goto Done;                  // Error, invalid MBR partition index.
      }
      //
      // Load information about the MBR configured partition.
      //
      FS_MEMSET(&PartInfoMBR, 0, sizeof(PartInfoMBR));
      FS__LoadPartitionInfoMBR(PartIndex, &PartInfoMBR, pBuffer);
      StartSector = PartInfoMBR.StartSector;
      NumSectors  = PartInfoMBR.NumSectors;
    } else {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "DISKPART[%d]: _ReadPartInfo: Invalid partitioning scheme.", (int)pInst->Unit));
      goto Done;                    // Error, storage device not partitioned or partitioning scheme not supported.
    }
  }
  //
  // Validate the information stored in the partition entry.
  //
  if (NumSectors == 0u) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "DISKPART[%d]: _ReadPartInfo: Invalid number of sectors.", (int)pInst->Unit));
    goto Done;
  }
  if (   (StartSector >= NumSectorsDevice)
      || ((StartSector + NumSectors) > NumSectorsDevice)) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "DISKPART[%d]: _ReadPartInfo: Partition exceeds device size.", (int)pInst->Unit));
    goto Done;
  }
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "DISKPART[%d]: PART Index: %d, StartSector: %u, NumSectors: %u\n", (int)pInst->Unit, (int)PartIndex, StartSector, NumSectors));
  HasError = 0;
Done:
  if (pBuffer != NULL) {
    FS__FreeSectorBuffer(pBuffer);
  }
  pInst->HasError       = HasError;
  pInst->StartSector    = StartSector;
  pInst->NumSectors     = NumSectors;
  pInst->BytesPerSector = BytesPerSector;
  return r;
}

/*********************************************************************
*
*       _ReadPartInfoIfRequired
*
*  Function description
*    Reads information about the location of the partition from MBR only if it is not already present.
*/
static int _ReadPartInfoIfRequired(DISKPART_INST * pInst) {
  int r;

  if (pInst->HasError != 0u) {
    return 1;               // Error
  }
  if (pInst->NumSectors != 0u) {
    return 0;               // OK, information already read;
  }
  r = _ReadPartInfo(pInst);
  return r;
}

/*********************************************************************
*
*       _GetStatus
*
*  Function description
*    Returns whether the storage medium is present or not.
*/
static int _GetStatus(const DISKPART_INST * pInst) {
  int                    Status;
  U8                     DeviceUnit;
  const FS_DEVICE_TYPE * pDeviceType;

  Status = FS_MEDIA_NOT_PRESENT;
  if ((pInst->HasError == 0u) && (pInst->pDeviceType != NULL)) {
    pDeviceType = pInst->pDeviceType;
    DeviceUnit  = pInst->DeviceUnit;
    Status      = pDeviceType->pfGetStatus(DeviceUnit);
  }
  return Status;
}

/*********************************************************************
*
*       _WriteSectors
*
*  Function description
*    Writes one or more logical sectors to storage device.
*
*  Parameters
*    pInst          Driver instance.
*    SectorIndex    Index of the first logical sector to be written.
*    pData          [IN] Logical sector data.
*    NumSectors     Number of logical sectors to be written.
*    RepeatSame     Specifies if the same sector data has to be written.
*
*  Return value
*    ==0      OK, logical sectors written successfully.
*    !=0      An error occurred.
*
*  Additional information
*    SectorIndex is relative to the beginning of the partition
*    accessed by this instance of the driver.
*/
static int _WriteSectors(const DISKPART_INST * pInst, U32 SectorIndex, const void * pData, U32 NumSectors, U8 RepeatSame) {
  int                    r;
  U32                    StartSector;
  U8                     DeviceUnit;
  const FS_DEVICE_TYPE * pDeviceType;

  if (pInst->HasError != 0u) {
    return 1;
  }
  ASSERT_SECTORS_ARE_IN_RANGE(pInst, SectorIndex, NumSectors);
  ASSERT_DEVICE_IS_SET(pInst);
  pDeviceType  = pInst->pDeviceType;
  DeviceUnit   = pInst->DeviceUnit;
  StartSector  = pInst->StartSector;
  SectorIndex += StartSector;
  CALL_TEST_HOOK_SECTOR_WRITE_BEGIN(pDeviceType, DeviceUnit, &SectorIndex, &pData, &NumSectors, &RepeatSame);
  r = pDeviceType->pfWrite(DeviceUnit, SectorIndex, pData, NumSectors, RepeatSame);
  CALL_TEST_HOOK_SECTOR_WRITE_END(pDeviceType, DeviceUnit, &SectorIndex, pData, NumSectors, RepeatSame, &r);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "DISKPART: Could not write sectors."));
  }
  return r;
}

#if FS_DISKPART_SUPPORT_ERROR_RECOVERY

/*********************************************************************
*
*       _FindInst
*
*  Function description
*    Searches for a driver instance by storage device.
*/
static DISKPART_INST * _FindInst(const FS_DEVICE_TYPE * pDeviceType, U32 DeviceUnit) {
  unsigned        Unit;
  DISKPART_INST * pInst;

  for (Unit = 0; Unit < _NumUnits; ++Unit) {
    pInst = _apInst[Unit];
    if (pInst->pDeviceType == pDeviceType) {
      if (pInst->DeviceUnit == DeviceUnit) {
        return pInst;
      }
    }
  }
  return NULL;       // No matching instance found.
}

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
  DISKPART_INST      * pInst;
  U8                   Unit;
  FS_READ_ERROR_DATA * pReadErrorData;
  int                  r;
  U32                  StartSectorPart;
  U32                  NumSectorsPart;

  pInst = _FindInst(pDeviceType, DeviceUnit);
  if (pInst == NULL) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "DISKPART: _cbOnReadError: No matching instance found (VN: \"%s:%d:\")", pDeviceType->pfGetName((U8)DeviceUnit), (int)DeviceUnit));
    return 1;
  }
  pReadErrorData = &pInst->ReadErrorData;
  if (pReadErrorData->pfCallback == NULL) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "DISKPART: _cbOnReadError: No callback registered."));
    return 1;
  }
  StartSectorPart = pInst->StartSector;
  NumSectorsPart  = pInst->NumSectors;
  if ((SectorIndex < StartSectorPart) || (SectorIndex >= (StartSectorPart + NumSectorsPart))) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "DISKPART: _cbOnReadError: Invalid sector index."));
    return 1;
  }
  Unit         = pInst->Unit;
  SectorIndex -= StartSectorPart;
  r = pReadErrorData->pfCallback(&FS_DISKPART_Driver, Unit, SectorIndex, pBuffer, NumSectors);
  return r;
}

/*********************************************************************
*
*       _SetReadErrorCallback
*
*  Function description
*    Function to be called by the driver when a read error occurs.
*/
static int _SetReadErrorCallback(const DISKPART_INST * pInst) {
  int                    r;
  FS_READ_ERROR_DATA     ReadErrorData;
  const FS_DEVICE_TYPE * pDeviceType;
  U8                     DeviceUnit;

  ASSERT_DEVICE_IS_SET(pInst);
  FS_MEMSET(&ReadErrorData, 0, sizeof(ReadErrorData));
  ReadErrorData.pfCallback = _cbOnReadError;
  pDeviceType = pInst->pDeviceType;
  DeviceUnit  = pInst->DeviceUnit;
  r = pDeviceType->pfIoCtl(DeviceUnit, FS_CMD_SET_READ_ERROR_CALLBACK, 0 /* not used */, &ReadErrorData);
  return r;
}

#endif // FS_DISKPART_SUPPORT_ERROR_RECOVERY

/*********************************************************************
*
*       _GetInst
*/
static DISKPART_INST * _GetInst(U8 Unit) {
  DISKPART_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  pInst = NULL;
  if (Unit < (U8)FS_DISKPART_NUM_UNITS) {
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
*       _DISKPART_GetDriverName
*
*   Function description
*     FS driver function. Returns the driver name.
*/
static const char * _DISKPART_GetDriverName(U8 Unit) {
  FS_USE_PARA(Unit);
  return "diskpart";
}

/*********************************************************************
*
*       _DISKPART_AddDevice
*
*   Function description
*     FS driver function. Creates a driver instance.
*/
static int _DISKPART_AddDevice(void) {
  DISKPART_INST * pInst;
  U8              Unit;

  if (_NumUnits >= (U8)FS_DISKPART_NUM_UNITS) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "DISKPART: Could not add device. Too many instances."));
    return -1;                      // Error, too many instances defined.
  }
  Unit  = _NumUnits;
  pInst = _apInst[Unit];
  if (pInst == NULL) {
    FS_ALLOC_ZEROED_PTR(SEGGER_PTR2PTR(void*, &pInst), (int)sizeof(DISKPART_INST), "DISKPART_INST");      // MISRA deviation D:100[d]
    if (pInst == NULL) {
      return -1;                    // Error, could not allocate memory.
    }
    _apInst[Unit] = pInst;
    pInst->Unit = Unit;
    ++_NumUnits;
  }
  return (int)Unit;                 // OK, instance created.
}

/*********************************************************************
*
*       _DISKPART_Read
*
*   Function description
*     FS driver function. Reads a number of sectors from storage medium.
*/
static int _DISKPART_Read(U8 Unit, U32 SectorIndex, void * pBuffer, U32 NumSectors) {
  DISKPART_INST * pInst;
  int             r;

  r     = 1;                // Set to indicate an error.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    r = _ReadSectors(pInst, SectorIndex, pBuffer, NumSectors);
  }
  return r;
}

/*********************************************************************
*
*       _DISKPART_Write
*
*   Function description
*     FS driver function. Writes a number of sectors to storage medium.
*/
static int _DISKPART_Write(U8 Unit, U32 SectorIndex, const void * pBuffer, U32 NumSectors, U8 RepeatSame) {
  DISKPART_INST * pInst;
  int             r;

  r     = 1;                // Set to indicate an error.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    r = _WriteSectors(pInst, SectorIndex, pBuffer, NumSectors, RepeatSame);
  }
  return r;
}

/*********************************************************************
*
*       _DISKPART_IoCtl
*
*   Function description
*     FS driver function. Executes an I/O control command.
*/
static int _DISKPART_IoCtl(U8 Unit, I32 Cmd, I32 Aux, void * pBuffer) {
  DISKPART_INST        * pInst;
  int                    r;
  U8                     DeviceUnit;
  FS_DEV_INFO          * pDevInfo;
  int                    RelayCmd;
  U32                    SectorIndex;
  const FS_DEVICE_TYPE * pDeviceType;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return -1;                    // Instance not allocated
  }
  FS_USE_PARA(Aux);
  r            = -1;              // Set to indicate an error.
  RelayCmd     = 1;               // By default, pass the commands to the underlying driver
  DeviceUnit  = pInst->DeviceUnit;
  pDeviceType = pInst->pDeviceType;
  switch (Cmd) {
  case FS_CMD_GET_DEVINFO:
    if (pBuffer != NULL) {
      r = _ReadPartInfoIfRequired(pInst);
      if (r == 0) {
        pDevInfo = SEGGER_PTR2PTR(FS_DEV_INFO, pBuffer);                                                  // MISRA deviation D:100[f]
        pDevInfo->NumSectors     = pInst->NumSectors;
        pDevInfo->BytesPerSector = pInst->BytesPerSector;
      }
    }
    RelayCmd = 0;                 // Command is handled by this driver.
    break;
#if FS_SUPPORT_DEINIT
  case FS_CMD_DEINIT:
    if (pDeviceType != NULL) {
      r = pDeviceType->pfIoCtl(DeviceUnit, Cmd, Aux, pBuffer);
    }
    RelayCmd = 0;               // Command is handled by this driver.
    FS_FREE(pInst);
    _apInst[Unit] = NULL;
    _NumUnits--;
    break;
#endif
  case FS_CMD_UNMOUNT:
    // through
  case FS_CMD_UNMOUNT_FORCED:
    pInst->HasError       = 0;
    pInst->NumSectors     = 0;
    pInst->StartSector    = 0;
    pInst->BytesPerSector = 0;
    break;
  case FS_CMD_FREE_SECTORS:
    //
    // SectorIndex is relative to the beginning of partition but the driver
    // expects the absolute logical sector index. This command is relayed.
    //
    SectorIndex  = (U32)Aux;
    SectorIndex += pInst->StartSector;
    Aux          = (I32)SectorIndex;
    break;
#if FS_DISKPART_SUPPORT_ERROR_RECOVERY
  case FS_CMD_SET_READ_ERROR_CALLBACK:
    if (pBuffer != NULL) {
      FS_READ_ERROR_DATA * pReadErrorData;

      pReadErrorData       = SEGGER_PTR2PTR(FS_READ_ERROR_DATA, pBuffer);                                 // MISRA deviation D:100[f]
      pInst->ReadErrorData = *pReadErrorData;     // struct copy
      r = _SetReadErrorCallback(pInst);
    }
    RelayCmd = 0;                 // Command is handled by this driver.
    break;
#endif
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
*       _DISKPART_InitMedium
*
*   Function description
*     FS driver function. Initializes the storage medium.
*/
static int _DISKPART_InitMedium(U8 Unit) {
  int             r;
  DISKPART_INST * pInst;

  r     = 1;                // Set to indicate an error.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    r = _InitMedium(pInst);
  }
  return r;
}

/*********************************************************************
*
*       _DISKPART_GetStatus
*
*   Function description
*     FS driver function. Returns whether the storage media is present or not.
*/
static int _DISKPART_GetStatus(U8 Unit) {
  DISKPART_INST * pInst;
  int             Status;

  Status = FS_MEDIA_NOT_PRESENT;    // Set to indicate an error.
  pInst  = _GetInst(Unit);
  if (pInst != NULL) {
    Status = _GetStatus(pInst);
  }
  return Status;
}

/*********************************************************************
*
*       _DISKPART_GetNumUnits
*
*   Function description
*     FS driver function. Returns the number of driver instances.
*/
static int _DISKPART_GetNumUnits(void) {
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
*       FS_DISKPART_Driver
*/
const FS_DEVICE_TYPE FS_DISKPART_Driver = {
  _DISKPART_GetDriverName,
  _DISKPART_AddDevice,
  _DISKPART_Read,
  _DISKPART_Write,
  _DISKPART_IoCtl,
  _DISKPART_InitMedium,
  _DISKPART_GetStatus,
  _DISKPART_GetNumUnits
};

/*********************************************************************
*
*       Public code (internal, for testing only)
*
**********************************************************************
*/

#if FS_SUPPORT_TEST

/*********************************************************************
*
*       FS__DISKPART_SetTestHookSectorReadBegin
*/
void FS__DISKPART_SetTestHookSectorReadBegin(FS_STORAGE_TEST_HOOK_SECTOR_READ_BEGIN * pfTestHook) {
  _pfTestHookSectorReadBegin = pfTestHook;
}

/*********************************************************************
*
*       FS__DISKPART_SetTestHookSectorReadEnd
*/
void FS__DISKPART_SetTestHookSectorReadEnd(FS_STORAGE_TEST_HOOK_SECTOR_READ_END * pfTestHook) {
  _pfTestHookSectorReadEnd = pfTestHook;
}

/*********************************************************************
*
*       FS__DISKPART_SetTestHookSectorWriteBegin
*/
void FS__DISKPART_SetTestHookSectorWriteBegin(FS_STORAGE_TEST_HOOK_SECTOR_WRITE_BEGIN * pfTestHook) {
  _pfTestHookSectorWriteBegin = pfTestHook;
}

/*********************************************************************
*
*       FS__DISKPART_SetTestHookSectorWriteEnd
*/
void FS__DISKPART_SetTestHookSectorWriteEnd(FS_STORAGE_TEST_HOOK_SECTOR_WRITE_END * pfTestHook) {
  _pfTestHookSectorWriteEnd = pfTestHook;
}

#endif // FS_SUPPORT_TEST

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_DISKPART_Configure
*
*  Function description
*    Configures the parameters of a driver instance.
*
*  Parameters
*    Unit           Index of the DISKPART instance to configure.
*    pDeviceType    Type of device driver that is used to access the storage device.
*    DeviceUnit     Index of the device driver instance that is used to access the storage device (0-based).
*    PartIndex      Index of the partition in the partition table stored in MBR.
*
*  Additional information
*    This function has to be called once for each instance of the driver.
*    The application can use FS_DISKPART_Configure() to set the parameters
*    that allows the driver to access the partition table stored in
*    Master Boot Record (MBR). The size and the position of the partition
*    are read from MBR on the first access to storage device.
*/
void FS_DISKPART_Configure(U8 Unit, const FS_DEVICE_TYPE * pDeviceType, U8 DeviceUnit, U8 PartIndex) {
  DISKPART_INST * pInst;

  ASSERT_PART_INDEX_IS_IN_RANGE(PartIndex);
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    pInst->pDeviceType = pDeviceType;
    pInst->DeviceUnit  = DeviceUnit;
    pInst->PartIndex   = PartIndex;
  }
}

/*************************** End of file ****************************/
