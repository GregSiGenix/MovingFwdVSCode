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
File        : FS_LogBlock.c
Purpose     : Logical Block Layer
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#include "FS_ConfDefaults.h"
#include "FS_Int.h"

/*********************************************************************
*
*       Defines, fixed
*
**********************************************************************
*/

/*********************************************************************
*
*       CALL_ON_DEVICE_ACTIVITY
*/
#if FS_STORAGE_SUPPORT_DEVICE_ACTIVITY
  #define CALL_ON_DEVICE_ACTIVITY(pDevice, Operation, StartSector, NumSectors, Sectortype)                    \
    {                                                                                                         \
      if ((pDevice)->Data.pfOnDeviceActivity != NULL) {                                                       \
        ((pDevice)->Data.pfOnDeviceActivity)(pDevice, Operation, StartSector, NumSectors, (int)(Sectortype)); \
      }                                                                                                       \
    }
#else
  #define CALL_ON_DEVICE_ACTIVITY(pDevice, Operation, StartSector, NumSectors, Sectortype)
#endif // FS_STORAGE_SUPPORT_DEVICE_ACTIVITY

/*********************************************************************
*
*       INC_READ_SECTOR_CNT
*/
#if FS_STORAGE_ENABLE_STAT_COUNTERS
  #define INC_READ_SECTOR_CNT(NumSectors, Type)               \
    {                                                         \
      FS_STORAGE_Counters.ReadOperationCnt++;                 \
      FS_STORAGE_Counters.ReadSectorCnt += (NumSectors);      \
      if ((Type) == FS_SECTOR_TYPE_MAN) {                     \
        FS_STORAGE_Counters.ReadSectorCntMan += (NumSectors); \
      }                                                       \
      if ((Type) == FS_SECTOR_TYPE_DIR) {                     \
        FS_STORAGE_Counters.ReadSectorCntDir += (NumSectors); \
      }                                                       \
    }
#else
  #define INC_READ_SECTOR_CNT(NumSectors, Type)
#endif // FS_STORAGE_ENABLE_STAT_COUNTERS

/*********************************************************************
*
*       INC_WRITE_SECTOR_CNT
*/
#if FS_STORAGE_ENABLE_STAT_COUNTERS
  #define INC_WRITE_SECTOR_CNT(NumSectors, Type)               \
    {                                                          \
      FS_STORAGE_Counters.WriteOperationCnt++;                 \
      FS_STORAGE_Counters.WriteSectorCnt += (NumSectors);      \
      if ((Type) == FS_SECTOR_TYPE_MAN) {                      \
        FS_STORAGE_Counters.WriteSectorCntMan += (NumSectors); \
      }                                                        \
      if ((Type) == FS_SECTOR_TYPE_DIR) {                      \
        FS_STORAGE_Counters.WriteSectorCntDir += (NumSectors); \
      }                                                        \
    }
#else
  #define INC_WRITE_SECTOR_CNT(NumSectors, Type)
#endif // FS_STORAGE_ENABLE_STAT_COUNTERS

#if FS_SUPPORT_CACHE

/*********************************************************************
*
*       INC_READ_CACHE_HIT_CNT
*/
#if FS_STORAGE_ENABLE_STAT_COUNTERS
  #define INC_READ_CACHE_HIT_CNT()                  {FS_STORAGE_Counters.ReadSectorCachedCnt++;}
#else
  #define INC_READ_CACHE_HIT_CNT()
#endif // FS_STORAGE_ENABLE_STAT_COUNTERS

#endif // FS_SUPPORT_CACHE

/*********************************************************************
*
*       INC_WRITE_CACHE_CLEAN_CNT
*/
#if FS_STORAGE_ENABLE_STAT_COUNTERS
  #define INC_WRITE_CACHE_CLEAN_CNT()               {FS_STORAGE_Counters.WriteSectorCntCleaned++;}
#else
  #define INC_WRITE_CACHE_CLEAN_CNT()
#endif // FS_STORAGE_ENABLE_STAT_COUNTERS

/*********************************************************************
*
*       CLR_BUSY_LED
*/
#if FS_SUPPORT_BUSY_LED
  #define CLR_BUSY_LED(pDevice) _ClrBusyLED(pDevice)
#else
  #define CLR_BUSY_LED(pDevice)
#endif // FS_SUPPORT_BUSY_LED

/*********************************************************************
*
*       SET_BUSY_LED
*/
#if FS_SUPPORT_BUSY_LED
  #define SET_BUSY_LED(pDevice) _SetBusyLED(pDevice)
#else
  #define SET_BUSY_LED(pDevice)
#endif // FS_SUPPORT_BUSY_LED

/*********************************************************************
*
*       CALL_TEST_HOOK_SECTOR_READ_BEGIN
*/
#if FS_SUPPORT_TEST
  #define CALL_TEST_HOOK_SECTOR_READ_BEGIN(pDeviceType, Unit, pSectorIndex, pData, pNumSectors)                       _CallTestHookSectorReadBegin(pDeviceType, Unit, pSectorIndex, pData, pNumSectors)
#else
  #define CALL_TEST_HOOK_SECTOR_READ_BEGIN(pDeviceType, Unit, pSectorIndex, pData, pNumSectors)
#endif

/*********************************************************************
*
*       CALL_TEST_HOOK_SECTOR_READ_END
*/
#if FS_SUPPORT_TEST
  #define CALL_TEST_HOOK_SECTOR_READ_END(pDeviceType, Unit, SectorIndex, pData, NumSectors, pResult)                  _CallTestHookSectorReadEnd(pDeviceType, Unit, SectorIndex, pData, NumSectors, pResult)
#else
  #define CALL_TEST_HOOK_SECTOR_READ_END(pDeviceType, Unit, SectorIndex, pData, NumSectors, pResult)
#endif

/*********************************************************************
*
*       CALL_TEST_HOOK_SECTOR_WRITE_BEGIN
*/
#if FS_SUPPORT_TEST
  #define CALL_TEST_HOOK_SECTOR_WRITE_BEGIN(pDeviceType, Unit, pSectorIndex, ppData, pNumSectors, pRepeatSame)        _CallTestHookSectorWriteBegin(pDeviceType, Unit, pSectorIndex, ppData, pNumSectors, pRepeatSame)
#else
  #define CALL_TEST_HOOK_SECTOR_WRITE_BEGIN(pDeviceType, Unit, pSectorIndex, ppData, pNumSectors, pRepeatSame)
#endif

/*********************************************************************
*
*       CALL_TEST_HOOK_SECTOR_WRITE_END
*/
#if FS_SUPPORT_TEST
  #define CALL_TEST_HOOK_SECTOR_WRITE_END(pDeviceType, Unit, pSectorIndex, pData, NumSectors, RepeatSame, pResult)    _CallTestHookSectorWriteEnd(pDeviceType, Unit, SectorIndex, pData, NumSectors, RepeatSame, pResult)
#else
  #define CALL_TEST_HOOK_SECTOR_WRITE_END(pDeviceType, Unit, pSectorIndex, pData, NumSectors, RepeatSame, pResult)
#endif

/*********************************************************************
*
*       Local data types
*
**********************************************************************
*/
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ALL)

typedef struct {
  U8           Type;
  const char * s;
} SECTOR_TYPE_DESC;

#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ALL

/*********************************************************************
*
*       Static const data
*
**********************************************************************
*/
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ALL)

static const SECTOR_TYPE_DESC _aDesc[] = {
  { FS_SECTOR_TYPE_DATA, "DAT" },
  { FS_SECTOR_TYPE_MAN,  "MAN" },
  { FS_SECTOR_TYPE_DIR,  "DIR" },
};

#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ALL

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
#if FS_VERIFY_WRITE
  static U8                                      * _pVerifyBuffer;
#endif // FS_VERIFY_WRITE
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

//lint -esym(9070, FS_LB_WritePart) [MISRA 2012 Rule 17.2, required] N:100. Rationale: Verified manually as being false.
//lint -esym(9070, _WriteToStorage) [MISRA 2012 Rule 17.2, required] N:100. Rationale: Verified manually as being false.
//lint -esym(9070, _WriteThroughCache) [MISRA 2012 Rule 17.2, required] N:100. Rationale: Verified manually as being false.
//lint -esym(9070, FS_LB_WriteDevice) [MISRA 2012 Rule 17.2, required] N:100. Rationale: Verified manually as being false.
//lint -efunc(818, _WriteSectors) Pointer parameter pDevice could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory] N:104. Rationale: pDevice cannot be declared as pointing to const because it calls _Verify().
//lint -efunc(818, FS_LB_FreeSectorsDevice) Pointer parameter pDevice could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory] N:104. Rationale: pDevice cannot be declared as pointing to const because it calls FS__CACHE_CommandDeviceNL().

#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ALL)

/*********************************************************************
*
*       _Type2Name
*
*  Function description
*    Converts a sector data type to a human-readable text.
*
*  Parameters
*    Type     Sector type.
*
*  Return value
*    Name of the sector type in text format.
*/
static const char * _Type2Name(U8 Type) {
  unsigned i;

  for (i = 0; i < SEGGER_COUNTOF(_aDesc); i++) {
    if (_aDesc[i].Type == Type) {
      return _aDesc[i].s;
    }
  }
  return "Unknown Type";
}

#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ALL

#if FS_SUPPORT_BUSY_LED

/*********************************************************************
*
*       _ClrBusyLED
*
*  Function description
*    Turns the busy LED off by invoking the registered callback.
*
*  Parameters
*    pDevice    Instance of the storage device.
*/
static void _ClrBusyLED(const FS_DEVICE * pDevice) {
  if (pDevice->Data.pfSetBusyLED != NULL) {
    pDevice->Data.pfSetBusyLED(0);
  }
}

/*********************************************************************
*
*       _SetBusyLED
*
*  Function description
*    Turns the busy LED on by invoking the registered callback.
*
*  Parameters
*    pDevice    Instance of the storage device.
*/
static void _SetBusyLED(const FS_DEVICE * pDevice) {
  if (pDevice->Data.pfSetBusyLED != NULL) {
    pDevice->Data.pfSetBusyLED(1);
  }
}

#endif // FS_SUPPORT_BUSY_LED

#if FS_SUPPORT_TEST

/*********************************************************************
*
*       _CallTestHookSectorReadBegin
*/
static void _CallTestHookSectorReadBegin(const FS_DEVICE_TYPE * pDeviceType, U8 Unit, U32 * pSectorIndex, void * pData, U32 * pNumSectors) {
  if (_pfTestHookSectorReadBegin != NULL) {
    _pfTestHookSectorReadBegin(pDeviceType, Unit, pSectorIndex, pData, pNumSectors);
  }
}

/*********************************************************************
*
*       _CallTestHookSectorReadEnd
*/
static void _CallTestHookSectorReadEnd(const FS_DEVICE_TYPE * pDeviceType, U8 Unit, U32 SectorIndex, void * pData, U32 NumSectors, int * pResult) {
  if (_pfTestHookSectorReadEnd != NULL) {
    _pfTestHookSectorReadEnd(pDeviceType, Unit, SectorIndex, pData, NumSectors, pResult);
  }
}

/*********************************************************************
*
*       _CallTestHookSectorWriteBegin
*/
static void _CallTestHookSectorWriteBegin(const FS_DEVICE_TYPE * pDeviceType, U8 Unit, U32 * pSectorIndex, const void ** ppData, U32 * pNumSectors, U8 * pRepeatSame) {
  if (_pfTestHookSectorWriteBegin != NULL) {
    _pfTestHookSectorWriteBegin(pDeviceType, Unit, pSectorIndex, ppData, pNumSectors, pRepeatSame);
  }
}

/*********************************************************************
*
*       _CallTestHookSectorWriteEnd
*/
static void _CallTestHookSectorWriteEnd(const FS_DEVICE_TYPE * pDeviceType, U8 Unit, U32 SectorIndex, const void * pData, U32 NumSectors, U8 RepeatSame, int * pResult) {
  if (_pfTestHookSectorWriteEnd != NULL) {
    _pfTestHookSectorWriteEnd(pDeviceType, Unit, SectorIndex, pData, NumSectors, RepeatSame, pResult);
  }
}

#endif // FS_SUPPORT_TEST

/*********************************************************************
*
*       _ReadSectors
*
*  Function description
*    Reads logical sectors from storage device.
*
*  Parameters
*    pDevice        Instance of the storage device.
*    SectorIndex    Index of the first logical sector to read.
*    NumSectors     Number of logical sectors to read.
*    pData          [OUT] Read logical sector data.
*
*  Return value
*    ==0      OK, logical sector data read.
*    !=0      An error occurred.
*/
static int _ReadSectors(const FS_DEVICE * pDevice, U32 SectorIndex, U32 NumSectors, void * pData) {
  int                    r;
  U8                     Unit;
  const FS_DEVICE_TYPE * pDeviceType;

  pDeviceType = pDevice->pType;
  Unit        = pDevice->Data.Unit;
  CALL_TEST_HOOK_SECTOR_READ_BEGIN(pDeviceType, Unit, &SectorIndex, pData, &NumSectors);
  r = pDeviceType->pfRead(Unit, SectorIndex, pData, NumSectors);
  CALL_TEST_HOOK_SECTOR_READ_END(pDeviceType, Unit, SectorIndex, pData, NumSectors, &r);
  return r;
}

#if FS_VERIFY_WRITE

/*********************************************************************
*
*       _Verify
*
*  Function description
*    Checks the contents of logical sectors.
*
*  Parameters
*    pDevice        Instance of the storage device.
*    SectorIndex    Index of the first logical sector to be checked.
*    pData          [IN] Expected contents of the logical sectors.
*    NumSectors     Number of logical sectors to check.
*    RepeatSame     Set to 1 if the logical sectors are expected to
*                   be filled with the same contents.
*
*  Return value
*    ==0      OK, logical sectors are filled with the specified value.
*    !=0      The contents of the logical sectors does not match or any other error.
*/
static int _Verify(FS_DEVICE * pDevice, U32 SectorIndex, const void * pData, U32 NumSectors, U8 RepeatSame) {
  U16         SectorSize;
  int         r;
  const U8  * p;

  //
  // If required, allocate memory to store one sector.
  //
  SectorSize = FS_GetSectorSize(pDevice);
  FS_ALLOC_ZEROED_PTR(SEGGER_PTR2PTR(void *, &_pVerifyBuffer), (I32)FS_Global.MaxSectorSize, "LOGBLOCK_VERIFY_BUFFER");       // MISRA deviation D:100[d]
  if (_pVerifyBuffer == NULL) {
    return 1;                 // Error, could not allocate read buffer.
  }
  //
  // Take one sector at a time and check its contents.
  //
  p = SEGGER_CONSTPTR2PTR(const U8, pData);                                                                                   // MISRA deviation D:100[e]
  do {
    //
    // Read one sector.
    //
    r = _ReadSectors(pDevice, SectorIndex, 1, _pVerifyBuffer);
    if (r != 0) {
      return 1;               // Error, read failed.
    }
    r = FS_MEMCMP(p, SEGGER_CONSTPTR2PTR(const void, _pVerifyBuffer), SectorSize);                                            // MISRA deviation D:100[e]
    if (r != 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_STORAGE, "LOGBLOCK: Verify failed at sector %lu on \"%s:%d:\".", SectorIndex, pDevice->pType->pfGetName(pDevice->Data.Unit), pDevice->Data.Unit));
      return 1;               // Error, content of sectors differs.
    }
    if (RepeatSame == 0u) {
      p += SectorSize;
    }
    ++SectorIndex;
  } while (--NumSectors != 0u);
  return 0;                   // OK, data matches.
}

#endif // FS_VERIFY_WRITE

/*********************************************************************
*
*       _WriteSectors
*
*  Function description
*    Writes logical sectors to storage device.
*
*  Parameters
*    pDevice        Instance of the storage device.
*    SectorIndex    Index of the first logical sector to be written.
*    NumSectors     Number of logical sectors to be written.
*    pData          [IN] Contents of logical sectors.
*    RepeatSame     Set to 1 if the logical sectors have to
*                   be filled with the same contents.
*
*  Return value
*    ==0      OK, logical sector data written.
*    !=0      An error occurred.
*/
static int _WriteSectors(FS_DEVICE * pDevice, U32 SectorIndex, U32 NumSectors, const void * pData, U8 RepeatSame) {
  int                    r;
  U8                     Unit;
  const FS_DEVICE_TYPE * pDeviceType;

  pDeviceType = pDevice->pType;
  Unit        = pDevice->Data.Unit;
  CALL_TEST_HOOK_SECTOR_WRITE_BEGIN(pDeviceType, Unit, &SectorIndex, &pData, &NumSectors, &RepeatSame);
  r = pDeviceType->pfWrite(Unit, SectorIndex, pData, NumSectors, RepeatSame);
  CALL_TEST_HOOK_SECTOR_WRITE_END(pDeviceType, Unit, pSectorIndex, pData, NumSectors, RepeatSame, &r);
#if FS_VERIFY_WRITE
  if (r == 0) {
    if (FS_IsWriteVerificationEnabled != 0u) {
      FS_LOCK_SYS();
      r = _Verify(pDevice, SectorIndex, pData, NumSectors, RepeatSame);
      FS_UNLOCK_SYS();
    }
  }
#endif // FS_VERIFY_WRITE
  return r;
}

/*********************************************************************
*
*       _FreeSectors
*
*  Function description
*    Marks logical sectors as not in use.
*
*  Parameters
*    pDevice        Instance of the storage device.
*    SectorIndex    Index of the first logical sector to be freed.
*    NumSectors     Number of logical sectors to be freed.
*
*  Return value
*    ==0      OK, logical sectors freed.
*    !=0      An error occurred.
*/
static int _FreeSectors(const FS_DEVICE * pDevice, U32 SectorIndex, U32 NumSectors) {
  int                    r;
  U8                     Unit;
  const FS_DEVICE_TYPE * pDeviceType;

  pDeviceType = pDevice->pType;
  Unit        = pDevice->Data.Unit;
  r = pDeviceType->pfIoCtl(Unit, FS_CMD_FREE_SECTORS, (I32)SectorIndex, &NumSectors);
  if (r != 0) {
    r = FS_ERRCODE_IOCTL_FAILURE;         // TBD: Return r directly here when all the drivers are able to return meaningful error codes.
  }
  return r;
}

/*********************************************************************
*
*       _ReadFromStorage
*
*  Function description
*    Reads logical sectors from storage device.
*
*  Parameters
*    pDevice        Instance of the storage device.
*    SectorIndex    Index of the first logical sector to read.
*    pData          [OUT] Read logical sector data.
*    NumSectors     Number of logical sectors to read.
*
*  Return value
*    ==0      OK, logical sector data read.
*    !=0      An error occurred.
*
*  Additional information
*    If the support for journaling is enabled then the sector data is read from journal.
*/
static int _ReadFromStorage(const FS_DEVICE * pDevice, U32 SectorIndex, void * pData, U32 NumSectors) {
  int r;

#if FS_SUPPORT_JOURNAL
  {
    U8 IsJournalActive;
    U8 IsJournalPresent;

    IsJournalActive  = pDevice->Data.JournalData.IsActive;
    IsJournalPresent = (U8)FS__JOURNAL_IsPresent(pDevice);
    if ((IsJournalPresent != 0u) && (IsJournalActive != 0u)) {
      r = FS__JOURNAL_Read(pDevice, SectorIndex, pData, NumSectors);
    } else {
      r = _ReadSectors(pDevice, SectorIndex, NumSectors, pData);
    }
  }
#else
  r = _ReadSectors(pDevice, SectorIndex, NumSectors, pData);
#endif
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_STORAGE, "LOGBLOCK: Failed to read sector(s): %lu-%lu from \"%s:%d:\".", SectorIndex, SectorIndex + NumSectors - 1u, pDevice->pType->pfGetName(pDevice->Data.Unit), pDevice->Data.Unit));
  }
  return r;
}

/*********************************************************************
*
*       _WriteToStorage
*
*  Function description
*    Writes logical sectors to storage device.
*
*  Parameters
*    pDevice          Instance of the storage device.
*    SectorIndex      Index of the first logical sector to be written.
*    pData            [IN] Contents of logical sectors.
*    NumSectors       Number of logical sectors to be written.
*    RepeatSame       Set to 1 if same data has to be written in all logical sectors.
*    WriteToJournal   Set to 1 if the sector data has to be written to journal.
*
*  Return value
*    ==0      OK, logical sector data written.
*    !=0      An error occurred.
*
*  Additional information
*    If the support for journaling is enabled and WriteToJournal is set to 1
*    then the sector data is written to journal.
*/
static int _WriteToStorage(FS_DEVICE * pDevice, U32 SectorIndex, const void * pData, U32 NumSectors, U8 RepeatSame, U8 WriteToJournal) {
  int r;

#if FS_SUPPORT_JOURNAL
  {
    U8 IsJournalActive;
    U8 IsJournalPresent;
    U8 IsJournalLoggingNewData;

    IsJournalActive  = pDevice->Data.JournalData.IsActive;
    IsJournalPresent = (U8)FS__JOURNAL_IsPresent(pDevice);
    if ((IsJournalActive != 0u) && (IsJournalPresent != 0u)) {
      //
      // Determine if the data can be written to original destination on the storage medium.
      //
      IsJournalLoggingNewData = pDevice->Data.JournalData.IsNewDataLogged;
      if (IsJournalLoggingNewData != 0u) {
        WriteToJournal = 1;
      }
    } else {
      WriteToJournal = 0;
    }
    if (WriteToJournal != 0u) {
      r = FS__JOURNAL_Write(pDevice, SectorIndex, pData, NumSectors, RepeatSame);
    } else {
      r = _WriteSectors(pDevice, SectorIndex, NumSectors, pData, RepeatSame);
    }
  }
#else
  FS_USE_PARA(WriteToJournal);
  r = _WriteSectors(pDevice, SectorIndex, NumSectors, pData, RepeatSame);
#endif
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_STORAGE, "LOGBLOCK: Failed to write sector(s): %lu-%lu to \"%s:%d:\".", SectorIndex, SectorIndex + NumSectors - 1u, pDevice->pType->pfGetName(pDevice->Data.Unit), pDevice->Data.Unit));
  }
  return r;
}

#if FS_SUPPORT_CACHE

/*********************************************************************
*
*       _UpdateCache
*
*  Function description
*    Writes the contents of the logical sectors to sector cache.
*
*  Parameters
*    pDevice          Instance of storage device.
*    SectorIndex      Index of the first logical sector to be written.
*    pData            [IN] Contents of logical sectors.
*    NumSectors       Number of logical sectors to be written.
*    RepeatSame       Set to 1 if same data has to be written in all logical sectors.
*    Type             Type of data stored in the logical sectors.
*/
static void _UpdateCache(FS_DEVICE * pDevice, U32 SectorIndex, const void * pData, U32 NumSectors, U8 RepeatSame, U8 Type) {
  int                  r;
  const U8           * p;
  U16                  SectorSize;
  const FS_CACHE_API * pCacheAPI;

  pCacheAPI = pDevice->Data.pCacheAPI;
  if (pCacheAPI != NULL) {
    SectorSize = FS_GetSectorSize(pDevice);
    p          = SEGGER_CONSTPTR2PTR(const U8, pData);                                    // MISRA deviation D:100[e]
    do {
      r = pCacheAPI->pfUpdateCache(pDevice, SectorIndex, p, Type);
      if (r != 0) {
        FS_DEBUG_WARN((FS_MTYPE_STORAGE, "Could not update sector %lu in cache.", SectorIndex));
      }
      SectorIndex++;
      if (RepeatSame == 0u) {
        p += SectorSize;
      }
    } while (--NumSectors != 0u);
  }
}

/*********************************************************************
*
*       _ReadThroughCache
*
*  Function description
*    Reads logical sectors from storage device.
*
*  Parameters
*    pDevice        Instance of the storage device.
*    SectorIndex    Index of the first logical sector to read.
*    pData          [OUT] Read logical sector data.
*    NumSectors     Number of logical sectors to read.
*    Type           Type of data stored in the logical sectors.
*
*  Return value
*    ==0      OK, logical sector data read.
*    !=0      An error occurred.
*
*  Additional information
*    This function reads from storage device when the logical sectors
*    are not present in the sector cache. In this case, the sector cache
*    is updated with the read sector data.
*/
static int _ReadThroughCache(FS_DEVICE * pDevice, U32 SectorIndex, void * pData, U32 NumSectors, U8 Type) {
  int                  r;
  const FS_CACHE_API * pCacheAPI;
  U32                  NumSectorsToRead;
  U32                  FirstSector;
  U16                  SectorSize;
  int                  NeedReadBurst;
  void               * pReadBuffer;
  U8                 * p;

  NeedReadBurst    = 0;
  NumSectorsToRead = 0;
  FirstSector      = 0;
  pReadBuffer      = NULL;
  pCacheAPI        = pDevice->Data.pCacheAPI;
  SectorSize       = FS_GetSectorSize(pDevice);
  if (pCacheAPI != NULL) {
    p = SEGGER_PTR2PTR(U8, pData);                                                        // MISRA deviation D:100[e]
    do {
      r = pCacheAPI->pfReadFromCache(pDevice, SectorIndex, p, Type);
      if (r != 0) {
        //
        // Cache miss. We need to read from hardware. Since we try to use burst mode, we do not read immediately.
        //
        if (NeedReadBurst != 0) {
          NumSectorsToRead++;
        } else {
          FirstSector      = SectorIndex;
          pReadBuffer      = p;
          NumSectorsToRead = 1;
          NeedReadBurst    = 1;
        }
      } else {
        INC_READ_CACHE_HIT_CNT();       // For statistics / debugging only
        if (NeedReadBurst != 0) {
          NeedReadBurst = 0;
          r = _ReadFromStorage(pDevice, FirstSector, pReadBuffer, NumSectorsToRead);
          if (r != 0) {
            break;                      // Error, read failure. We end the operation here.
          }
          _UpdateCache(pDevice, FirstSector, pReadBuffer, NumSectorsToRead, 0, Type);
        }
      }
      p += SectorSize;
      SectorIndex++;
    } while(--NumSectors != 0u);
    //
    // End of read routine reached. There may be a hardware "read burst" operation pending, which needs to be executed in this case.
    //
    if (NeedReadBurst != 0) {
      r = _ReadFromStorage(pDevice, FirstSector, pReadBuffer, NumSectorsToRead);
      if (r == 0) {
        _UpdateCache(pDevice, FirstSector, pReadBuffer, NumSectorsToRead, 0, Type);
      }
    }
  } else {
    r = _ReadFromStorage(pDevice, SectorIndex, pData, NumSectors);
  }
  return r;
}

/*********************************************************************
*
*       _WriteThroughCache
*
*  Function description
*    Writes logical sectors to storage device.
*
*  Parameters
*    pDevice          Instance of the storage device.
*    SectorIndex      Index of the first logical sector to be written.
*    pData            [IN] Contents of logical sectors.
*    NumSectors       Number of logical sectors to be written.
*    RepeatSame       Set to 1 if same data has to be written in all logical sectors.
*    Type             Type of data stored in the logical sectors.
*    WriteToJournal   Set to 1 if the sector data has to be written to journal.
*
*  Return value
*    ==0      OK, logical sector data written.
*    !=0      An error occurred.
*
*  Additional information
*    This function updates the sector cache with the written sector data.
*/
static int _WriteThroughCache(FS_DEVICE * pDevice, U32 SectorIndex, const void * pData, U32 NumSectors, U8 RepeatSame, U8 Type, U8 WriteToJournal) {
  int                  r;
  const FS_CACHE_API * pCacheAPI;
  int                  WriteRequired;
  U16                  SectorSize;
  const U8           * p;
  int                  IsWritten;
  U32                  NumSectorsToCache;
  U32                  SectorIndexToCache;

  r             = 0;
  WriteRequired = 1;
  pCacheAPI     = pDevice->Data.pCacheAPI;
  SectorSize    = FS_GetSectorSize(pDevice);
  if (pCacheAPI != NULL) {        // Cache is configured ?
    p                  = SEGGER_CONSTPTR2PTR(const U8, pData);                            // MISRA deviation D:100[e]
    NumSectorsToCache  = NumSectors;
    SectorIndexToCache = SectorIndex;
    WriteRequired      = 0;
    do {
      IsWritten = pCacheAPI->pfWriteIntoCache(pDevice, SectorIndexToCache, p, Type);
      if (IsWritten == 0) {
        WriteRequired = 1;
      }
      if (RepeatSame == 0u) {
        p += SectorSize;
      }
      ++SectorIndexToCache;
    } while (--NumSectorsToCache != 0u);
  }
  //
  // Write to storage medium if required.
  //
  if (WriteRequired != 0) {
    r = _WriteToStorage(pDevice, SectorIndex, pData, NumSectors, RepeatSame, WriteToJournal);
  }
  return r;
}

#endif // FS_SUPPORT_CACHE

/*********************************************************************
*
*       Public code (internal, for testing only)
*
**********************************************************************
*/

#if FS_SUPPORT_TEST

/*********************************************************************
*
*       FS__LB_SetTestHookSectorReadBegin
*/
void FS__LB_SetTestHookSectorReadBegin(FS_STORAGE_TEST_HOOK_SECTOR_READ_BEGIN * pfTestHook) {
  _pfTestHookSectorReadBegin = pfTestHook;
}

/*********************************************************************
*
*       FS__LB_SetTestHookSectorReadEnd
*/
void FS__LB_SetTestHookSectorReadEnd(FS_STORAGE_TEST_HOOK_SECTOR_READ_END * pfTestHook) {
  _pfTestHookSectorReadEnd = pfTestHook;
}

/*********************************************************************
*
*       FS__LB_SetTestHookSectorWriteBegin
*/
void FS__LB_SetTestHookSectorWriteBegin(FS_STORAGE_TEST_HOOK_SECTOR_WRITE_BEGIN * pfTestHook) {
  _pfTestHookSectorWriteBegin = pfTestHook;
}

/*********************************************************************
*
*       FS__LB_SetTestHookSectorWriteEnd
*/
void FS__LB_SetTestHookSectorWriteEnd(FS_STORAGE_TEST_HOOK_SECTOR_WRITE_END * pfTestHook) {
  _pfTestHookSectorWriteEnd = pfTestHook;
}

#endif // FS_SUPPORT_TEST

/*********************************************************************
*
*       Public code (internal)
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_LB_GetStatus
*
*  Function description
*    Queries the presence status of the storage device.
*
*  Parameters
*    pDevice      Instance of the storage device.
*
*  Return value
*    FS_MEDIA_STATE_UNKNOWN   Presence status is not known.
*    FS_MEDIA_NOT_PRESENT     The storage device is not present.
*    FS_MEDIA_IS_PRESENT      The storage device is present.
*/
int FS_LB_GetStatus(const FS_DEVICE * pDevice) {
  int                    r;
  const FS_DEVICE_TYPE * pDeviceType;

  FS_PROFILE_CALL_U32x2(FS_EVTID_LB_GETSTATUS, SEGGER_PTR2ADDR(pDevice->pType), pDevice->Data.Unit);    // MISRA deviation D:100[b]
  r = FS_ERRCODE_UNKNOWN_DEVICE;
  pDeviceType = pDevice->pType;
  if (pDeviceType != NULL) {
    r = pDeviceType->pfGetStatus(pDevice->Data.Unit);
  }
  FS_PROFILE_END_CALL_U32(FS_EVTID_LB_GETSTATUS, r);
  return r;
}

/*********************************************************************
*
*       FS_LB_InitMedium
*
*  Function description
*    Initializes the storage device.
*
*  Parameters
*    pDevice    Instance of the storage device.
*
*  Return value
*    ==0        OK, storage device initialized.
*    !=0        Error code indicating the failure reason.
*
*  Additional information
*    This function calls the initialization routine of the device driver, if one exists.
*    If there if no initialization routine available, we assume the driver is
*    handling this automatically.
*/
int FS_LB_InitMedium(FS_DEVICE * pDevice) {
  int                    IsInited;
  const FS_DEVICE_TYPE * pDeviceType;
  int                    r;

  FS_PROFILE_CALL_U32x2(FS_EVTID_LB_INITMEDIUM, SEGGER_PTR2ADDR(pDevice->pType), pDevice->Data.Unit);   // MISRA deviation D:100[b]
  r = FS_ERRCODE_UNKNOWN_DEVICE;
  pDeviceType = pDevice->pType;
  if (pDeviceType != NULL) {
    r = 0;                          // Set to indicate success because the implementation of pfInitMedium() is optional.
    if (pDeviceType->pfInitMedium != NULL) {
      r = pDeviceType->pfInitMedium(pDevice->Data.Unit);
      if (r != 0) {
        r = FS_ERRCODE_INIT_FAILURE;
      }
    }
  }
  IsInited = 0;
  if (r == 0) {
    IsInited = 1;
  }
  pDevice->Data.IsInited = (U8)IsInited;
  FS_PROFILE_END_CALL_U32(FS_EVTID_LB_INITMEDIUM, IsInited);
  return r;
}

/*********************************************************************
*
*       FS_LB_InitMediumIfRequired
*
*  Function description
*    Initializes the storage device.
*
*  Parameters
*    pDevice    Instance of the storage device.
*
*  Return value
*    ==0      OK, storage device initialized.
*    !=0      Error code indicating the failure reason.
*
*  Additional information
*    This function does not perform any operation if the storage
*    device is already initialized.
*/
int FS_LB_InitMediumIfRequired(FS_DEVICE * pDevice) {
  int r;

  r = 0;              // Set to indicate success.
  if (pDevice->Data.IsInited == 0u) {
    r = FS_LB_InitMedium(pDevice);
  }
  return r;
}

/*********************************************************************
*
*       FS_LB_ReadDevice
*
*  Function description
*    Reads a single logical sector from the storage device.
*
*  Parameters
*    pDevice        Instance of the storage device.
*    SectorIndex    Index of the logical sector to read.
*    pData          [OUT] Read logical sector data.
*    Type           Type of data stored in the logical sector.
*
*  Return value
*    ==0            OK, logical sector read.
*    !=0            An error occurred.
*
*  Additional information
*    This function checks if the logical sector is present in the
*    sector cache and if yes, it returns the cached data instead
*    of reading id from the storage device.
*/
int FS_LB_ReadDevice(FS_DEVICE * pDevice, U32 SectorIndex, void * pData, U8 Type) {
  int r;

  FS_PROFILE_CALL_U32x5(FS_EVTID_LB_READDEVICE, SEGGER_PTR2ADDR(pDevice->pType), pDevice->Data.Unit, SectorIndex, SEGGER_PTR2ADDR(pData), Type);    // MISRA deviation D:100[b]
  r = FS_LB_InitMediumIfRequired(pDevice);
  if (r == 0) {
    INC_READ_SECTOR_CNT(1u, Type);
    FS_DEBUG_LOG((FS_MTYPE_STORAGE, "LOGBLOCK: READ_DEVICE   VN: \"%s:%d:\", ST: %s, SI: %lu\n", pDevice->pType->pfGetName(pDevice->Data.Unit), pDevice->Data.Unit, _Type2Name(Type), SectorIndex));
    CALL_ON_DEVICE_ACTIVITY(pDevice, FS_OPERATION_READ, SectorIndex, 1, Type);
    SET_BUSY_LED(pDevice);
#if FS_SUPPORT_CACHE
    r = _ReadThroughCache(pDevice, SectorIndex, pData, 1, Type);
#else
    FS_USE_PARA(Type);
    r = _ReadFromStorage(pDevice, SectorIndex, pData, 1);
#endif
    CLR_BUSY_LED(pDevice);
  }
  FS_PROFILE_END_CALL_U32(FS_EVTID_LB_READDEVICE, r);
  return r;
}

/*********************************************************************
*
*       FS_LB_ReadPart
*
*  Function description
*    Reads a single logical sector from a partition.
*
*  Parameters
*    pPart          Partition instance.
*    SectorIndex    Index of the logical sector to read.
*    pData          [OUT] Read logical sector data.
*    Type           Type of data stored in the logical sector.
*
*  Return value
*    ==0            OK, logical sector read.
*    !=0            An error occurred.
*
*  Additional information
*    SectorIndex is relative to the beginning of the MBR partition.
*/
int FS_LB_ReadPart(FS_PARTITION * pPart, U32 SectorIndex, void * pData, U8 Type) {
  FS_DEVICE * pDevice;
  int         r;

  pDevice      = &pPart->Device;
  SectorIndex += pPart->StartSector;      // Convert to absolute sector index.
  r = FS_LB_ReadDevice(pDevice, SectorIndex, pData, Type);
  return r;
}

/*********************************************************************
*
*       FS_LB_ReadBurst
*
*  Function description
*    Reads multiple logical sectors from the storage device.
*
*  Parameters
*    pDevice        Instance of the storage device.
*    SectorIndex    Index of the first logical sector to read.
*    NumSectors     Number of logical sectors to read.
*    pData          [OUT] Read logical sector data.
*    Type           Type of data stored in the logical sector.
*
*  Return value
*    ==0            OK, logical sectors read.
*    !=0            An error occurred.
*/
int FS_LB_ReadBurst(FS_DEVICE * pDevice, U32 SectorIndex, U32 NumSectors, void * pData, U8 Type) {
  int r;

  FS_PROFILE_CALL_U32x6(FS_EVTID_LB_READBURST, SEGGER_PTR2ADDR(pDevice->pType), pDevice->Data.Unit, SectorIndex, NumSectors, SEGGER_PTR2ADDR(pData), Type);     // MISRA deviation D:100[b]
  r = FS_LB_InitMediumIfRequired(pDevice);
  if (r == 0) {
    INC_READ_SECTOR_CNT(NumSectors, Type);
    FS_DEBUG_LOG((FS_MTYPE_STORAGE, "LOGBLOCK: READ_BURST    VN: \"%s:%d:\", ST: %s, SI: %lu, NS: %d\n", pDevice->pType->pfGetName(pDevice->Data.Unit), pDevice->Data.Unit, _Type2Name(Type), SectorIndex, NumSectors));
    CALL_ON_DEVICE_ACTIVITY(pDevice, FS_OPERATION_READ, SectorIndex, NumSectors, Type);
    SET_BUSY_LED(pDevice);
#if FS_SUPPORT_CACHE
    r = _ReadThroughCache(pDevice, SectorIndex, pData, NumSectors, Type);
#else
    FS_USE_PARA(Type);
    r = _ReadFromStorage(pDevice, SectorIndex, pData, NumSectors);
#endif
    CLR_BUSY_LED(pDevice);
  }
  FS_PROFILE_END_CALL_U32(FS_EVTID_LB_READBURST, r);
  return r;
}

/*********************************************************************
*
*       FS_LB_ReadBurstPart
*
*  Function description
*    Reads multiple logical sectors from a partition.
*
*  Parameters
*    pPart          Partition instance.
*    SectorIndex    Index of the first logical sector to read.
*    NumSectors     Number of logical sectors to read.
*    pData          [OUT] Read logical sector data.
*    Type           Type of data stored in the logical sector.
*
*  Return value
*    ==0            OK, logical sectors read.
*    !=0            An error occurred.
*
*  Additional information
*    SectorIndex is relative to the beginning of the MBR partition.
*/
int FS_LB_ReadBurstPart(FS_PARTITION * pPart, U32 SectorIndex, U32 NumSectors, void * pData, U8 Type) {
  FS_DEVICE * pDevice;
  int         r;

  pDevice      = &pPart->Device;
  SectorIndex += pPart->StartSector;      // Convert to absolute sector index.
  r = FS_LB_ReadBurst(pDevice, SectorIndex, NumSectors, pData, Type);
  return r;
}

/*********************************************************************
*
*       FS_LB_WriteBurst
*
*  Function description
*    Writes multiple logical sectors to the storage device.
*
*  Parameters
*    pDevice          Instance of the storage device.
*    SectorIndex      Index of the first logical sector to be written.
*    NumSectors       Number of logical sectors to be written.
*    pData            [IN] Contents of logical sectors.
*    Type             Type of data stored in the logical sectors.
*    WriteToJournal   Set to 1 if the sector data has to be written to journal.
*
*  Return value
*    ==0      OK, logical sector data written.
*    !=0      An error occurred.
*/
int FS_LB_WriteBurst(FS_DEVICE * pDevice, U32 SectorIndex, U32 NumSectors, const void * pData, U8 Type, U8 WriteToJournal) {
  int r;

  FS_PROFILE_CALL_U32x7(FS_EVTID_LB_WRITEBURST, SEGGER_PTR2ADDR(pDevice->pType), pDevice->Data.Unit, SectorIndex, NumSectors, SEGGER_PTR2ADDR(pData), Type, WriteToJournal);    // MISRA deviation D:100[b]
  r = FS_LB_InitMediumIfRequired(pDevice);
  if (r == 0) {
    INC_WRITE_SECTOR_CNT(NumSectors, Type);
    FS_DEBUG_LOG((FS_MTYPE_STORAGE, "LOGBLOCK: WRITE_BURST   VN: \"%s:%d:\", ST: %s, SI: %lu, NS: %d\n", pDevice->pType->pfGetName(pDevice->Data.Unit), pDevice->Data.Unit, _Type2Name(Type), SectorIndex, NumSectors));
    CALL_ON_DEVICE_ACTIVITY(pDevice, FS_OPERATION_WRITE, SectorIndex, NumSectors, Type);
    SET_BUSY_LED(pDevice);
#if FS_SUPPORT_CACHE
    r = _WriteThroughCache(pDevice, SectorIndex, pData, NumSectors, 0, Type, WriteToJournal);
#else
    FS_USE_PARA(Type);
    r = _WriteToStorage(pDevice, SectorIndex, pData, NumSectors, 0, WriteToJournal);
#endif
    CLR_BUSY_LED(pDevice);
  }
  FS_PROFILE_END_CALL_U32(FS_EVTID_LB_WRITEBURST, r);
  return r;
}

/*********************************************************************
*
*       FS_LB_WriteBurstPart
*
*  Function description
*    Writes multiple logical sectors to a partition.
*
*  Parameters
*    pPart            Partition instance.
*    SectorIndex      Index of the first logical sector to be written.
*    NumSectors       Number of logical sectors to be written.
*    pData            [IN] Contents of logical sectors.
*    Type             Type of data stored in the logical sectors.
*    WriteToJournal   Set to 1 if the sector data has to be written to journal.
*
*  Return value
*    ==0      OK, logical sector data written.
*    !=0      An error occurred.
*
*  Additional information
*    SectorIndex is relative to the beginning of the MBR partition.
*/
int FS_LB_WriteBurstPart(FS_PARTITION * pPart, U32 SectorIndex, U32 NumSectors, const void * pData, U8 Type, U8 WriteToJournal) {
  FS_DEVICE * pDevice;
  int         r;

  pDevice      = &pPart->Device;
  SectorIndex += pPart->StartSector;      // Convert to absolute sector index.
  r = FS_LB_WriteBurst(pDevice, SectorIndex, NumSectors, pData, Type, WriteToJournal);
  return r;
}

/*********************************************************************
*
*       FS_LB_WriteMultiple
*
*  Function description
*    Writes multiple logical sectors to the storage device.
*
*  Parameters
*    pDevice          Instance of the storage device.
*    SectorIndex      Index of the first logical sector to be written.
*    NumSectors       Number of logical sectors to be written.
*    pData            [IN] Contents of logical sectors.
*    Type             Type of data stored in the logical sectors.
*    WriteToJournal   Set to 1 if the sector data has to be written to journal.
*
*  Return value
*    ==0      OK, logical sector data written.
*    !=0      An error occurred.
*
*  Additional information
*    The contents of all the written logical sectors is identical.
*    This means that pData points to a buffer with a size of one
*    logical sector.
*/
int FS_LB_WriteMultiple(FS_DEVICE * pDevice, U32 SectorIndex, U32 NumSectors, const void * pData, U8 Type, U8 WriteToJournal) {
  int r;

  FS_PROFILE_CALL_U32x7(FS_EVTID_LB_WRITEMULTIPLE, SEGGER_PTR2ADDR(pDevice->pType), pDevice->Data.Unit, SectorIndex, NumSectors, SEGGER_PTR2ADDR(pData), Type, WriteToJournal);     // MISRA deviation D:100[b]
  r = FS_LB_InitMediumIfRequired(pDevice);
  if (r == 0) {
    INC_WRITE_SECTOR_CNT(NumSectors, Type);
    FS_DEBUG_LOG((FS_MTYPE_STORAGE, "LOGBLOCK: WRITE_MULTI   VN: \"%s:%d:\", ST: %s, SI: %lu, NS: %d\n", pDevice->pType->pfGetName(pDevice->Data.Unit), pDevice->Data.Unit, _Type2Name(Type), SectorIndex, NumSectors));
    CALL_ON_DEVICE_ACTIVITY(pDevice, FS_OPERATION_WRITE, SectorIndex, NumSectors, Type);
    SET_BUSY_LED(pDevice);
#if FS_SUPPORT_CACHE
    r = _WriteThroughCache(pDevice, SectorIndex, pData, NumSectors, 1, Type, WriteToJournal);
#else
    FS_USE_PARA(Type);
    r = _WriteToStorage(pDevice, SectorIndex, pData, NumSectors, 1, WriteToJournal);
#endif
    CLR_BUSY_LED(pDevice);
  }
  FS_PROFILE_END_CALL_U32(FS_EVTID_LB_WRITEMULTIPLE, r);
  return r;
}

/*********************************************************************
*
*       FS_LB_WriteMultiplePart
*
*  Function description
*    Writes multiple logical sectors to the storage device.
*
*  Parameters
*    pPart            Partition instance.
*    SectorIndex      Index of the first logical sector to be written.
*    NumSectors       Number of logical sectors to be written.
*    pData            [IN] Contents of logical sectors.
*    Type             Type of data stored in the logical sectors.
*    WriteToJournal   Set to 1 if the sector data has to be written to journal.
*
*  Return value
*    ==0      OK, logical sector data written.
*    !=0      An error occurred.
*
*  Additional information
*    SectorIndex is relative to the beginning of the MBR partition.
*    The contents of all the written logical sectors is identical.
*    This means that pData points to a buffer with a size of one
*    logical sector.
*/
int FS_LB_WriteMultiplePart(FS_PARTITION * pPart, U32 SectorIndex, U32 NumSectors, const void * pData, U8 Type, U8 WriteToJournal) {
  FS_DEVICE * pDevice;
  int         r;

  pDevice      = &pPart->Device;
  SectorIndex += pPart->StartSector;      // Convert to absolute sector index.
  r = FS_LB_WriteMultiple(pDevice, SectorIndex, NumSectors, pData, Type, WriteToJournal);
  return r;
}

/*********************************************************************
*
*       FS_LB_WriteDevice
*
*  Function description
*    Writes a single logical sector to the storage device.
*
*  Parameters
*    pDevice          Instance of the storage device.
*    SectorIndex      Index of the first logical sector to be written.
*    pData            [IN] Contents of logical sectors.
*    Type             Type of data stored in the logical sectors.
*    WriteToJournal   Set to 1 if the sector data has to be written to journal.
*
*  Return value
*    ==0      OK, logical sector data written.
*    !=0      An error occurred.
*/
int FS_LB_WriteDevice(FS_DEVICE * pDevice, U32 SectorIndex, const void * pData, U8 Type, U8 WriteToJournal) {
  int r;

  FS_PROFILE_CALL_U32x6(FS_EVTID_LB_WRITEDEVICE, SEGGER_PTR2ADDR(pDevice->pType), pDevice->Data.Unit, SectorIndex, SEGGER_PTR2ADDR(pData), Type, WriteToJournal);     // MISRA deviation D:100[b]
  r = FS_LB_InitMediumIfRequired(pDevice);
  if (r == 0) {
    INC_WRITE_SECTOR_CNT(1u, Type);
    FS_DEBUG_LOG((FS_MTYPE_STORAGE, "LOGBLOCK: WRITE_DEVICE  VN: \"%s:%d:\", ST: %s, SI: %lu\n", pDevice->pType->pfGetName(pDevice->Data.Unit), pDevice->Data.Unit, _Type2Name(Type), SectorIndex));
    CALL_ON_DEVICE_ACTIVITY(pDevice, FS_OPERATION_WRITE, SectorIndex, 1, Type);
    SET_BUSY_LED(pDevice);
#if FS_SUPPORT_CACHE
    r = _WriteThroughCache(pDevice, SectorIndex, pData, 1, 0, Type, WriteToJournal);
#else
    FS_USE_PARA(Type);
    r = _WriteToStorage(pDevice, SectorIndex, pData, 1, 0, WriteToJournal);
#endif
    CLR_BUSY_LED(pDevice);
  }
  FS_PROFILE_END_CALL_U32(FS_EVTID_LB_WRITEDEVICE, r);
  return r;
}

/*********************************************************************
*
*         FS_LB_WritePart
*
*  Function description
*    Writes a single logical sector to a partition.
*
*  Parameters
*    pPart            Partition instance.
*    SectorIndex      Index of the first logical sector to be written.
*    pData            [IN] Contents of logical sectors.
*    Type             Type of data stored in the logical sectors.
*    WriteToJournal   Set to 1 if the sector data has to be written to journal.
*
*  Return value
*    ==0      OK, logical sector data written.
*    !=0      An error occurred.
*/
int FS_LB_WritePart(FS_PARTITION * pPart, U32 SectorIndex, const void * pData, U8 Type, U8 WriteToJournal) {
  FS_DEVICE * pDevice;
  int         r;

  pDevice      = &pPart->Device;
  SectorIndex += pPart->StartSector;      // Convert to absolute sector index.
  r = FS_LB_WriteDevice(pDevice, SectorIndex, pData, Type, WriteToJournal);
  return r;
}

/*********************************************************************
*
*       FS_LB_Ioctl
*
*  Function description
*    Requests a storage device to execute a command.
*
*  Parameters
*    pDevice    Instance of the storage device.
*    Cmd        Command to be executed.
*    Aux        Additional command parameter.
*    pData      Additional command parameter.
*
*  Return value
*    Command specific. In general, 0 means that the operation was successful
*    while a negative value means that an error occurred.
*/
int FS_LB_Ioctl(FS_DEVICE * pDevice, I32 Cmd, I32 Aux, void * pData) {
  int                    r;
  const FS_DEVICE_TYPE * pDeviceType;

  pDeviceType = pDevice->pType;
  if (pDeviceType == NULL) {
    return FS_ERRCODE_UNKNOWN_DEVICE;
  }
  r = 0;                    // No error so far.
  switch (Cmd) {
  case FS_CMD_UNMOUNT:
    // through
  case FS_CMD_UNMOUNT_FORCED:
    break;
  case FS_CMD_DEINIT:
#if ((FS_VERIFY_WRITE != 0) && (FS_SUPPORT_DEINIT != 0))
    if (_pVerifyBuffer != NULL) {
      FS_Free(_pVerifyBuffer);
      _pVerifyBuffer = NULL;
    }
#endif // (FS_VERIFY_WRITE != 0) && (FS_SUPPORT_DEINIT != 0)
    break;
  default:
    r = FS_LB_InitMediumIfRequired(pDevice);
    break;
  }
  if (r == 0) {
    r = pDeviceType->pfIoCtl(pDevice->Data.Unit, Cmd, Aux, pData);
  }
  return r;
}

/*********************************************************************
*
*       FS_GetSectorSize
*
*  Function description
*    Queries the size of the logical sector.
*
*  Parameters
*    pDevice      Instance of the storage device.
*
*  Return value
*    !=0      Size of the logical sector in bytes.
*    ==0      An error occurred.
*/
U16 FS_GetSectorSize(FS_DEVICE * pDevice) {
  U16         BytesPerSector;
  int         r;
  FS_DEV_INFO DevInfo;

  BytesPerSector = 0;       // Set to indicate an error.
  r = FS_LB_InitMediumIfRequired(pDevice);
  if (r == 0) {
    r = pDevice->pType->pfIoCtl(pDevice->Data.Unit, FS_CMD_GET_DEVINFO, 0, &DevInfo);
    if (r == 0) {
      BytesPerSector = DevInfo.BytesPerSector;
    }
  }
  return BytesPerSector;
}

/*********************************************************************
*
*       FS_LB_GetDeviceInfo
*
*  Function description
*    Returns information about the storage device.
*
*  Parameters
*    pDevice      Instance of the storage device.
*    pDevInfo     [OUT] Storage device information.
*
*  Return value
*    ==0      OK, information returned successfully.
*    !=0      Error code indicating the failure reason.
*/
int FS_LB_GetDeviceInfo(FS_DEVICE * pDevice, FS_DEV_INFO * pDevInfo) {
  int r;

  r = FS_LB_InitMediumIfRequired(pDevice);
  if (r == 0) {
    r = pDevice->pType->pfIoCtl(pDevice->Data.Unit, FS_CMD_GET_DEVINFO, 0, SEGGER_PTR2PTR(void, pDevInfo));       // MISRA deviation D:100[f]
    if (r != 0) {
      r = FS_ERRCODE_IOCTL_FAILURE;         // TBD: Return r directly here when all the drivers are able to return meaningful error codes.
    }
  }
  return r;
}

/*********************************************************************
*
*       FS_LB_FreeSectorsDevice
*
*  Function description
*    Marks logical sectors as not in use.
*
*  Parameters
*    pDevice        Storage device on which the logical sectors are located.
*    SectorIndex    Index of the first logical sector to be freed.
*    NumSectors     Number of logical sectors to be freed.
*
*  Return value
*    ==0      OK, logical sector successfully freed.
*    !=0      An error occurred.
*
*  Additional information
*    SectorIndex is relative to the beginning of the storage device.
*    This function also frees any logical sectors that are located
*    in the sector cache or journal.
*/
int FS_LB_FreeSectorsDevice(FS_DEVICE * pDevice, U32 SectorIndex, U32 NumSectors) {
  int r;

  FS_DEBUG_LOG((FS_MTYPE_STORAGE, "LOGBLOCK: FREE_SECTORS  VN: \"%s:%d:\", SI: %lu, NS: %u\n", pDevice->pType->pfGetName(pDevice->Data.Unit), pDevice->Data.Unit, SectorIndex, NumSectors));
#if FS_SUPPORT_JOURNAL
  {
    U8 IsJournalActive;
    U8 IsJournalPresent;

    IsJournalActive  = pDevice->Data.JournalData.IsActive;
    IsJournalPresent = (U8)FS__JOURNAL_IsPresent(pDevice);
    if ((IsJournalPresent != 0u) && (IsJournalActive != 0u)) {
      r = FS__JOURNAL_FreeSectors(pDevice, SectorIndex, NumSectors);
    } else {
      r = _FreeSectors(pDevice, SectorIndex, NumSectors);
    }
  }
#else
  r = _FreeSectors(pDevice, SectorIndex, NumSectors);
#endif
#if FS_SUPPORT_CACHE
  {
    CACHE_FREE CacheFree;
    int        Result;

    CacheFree.FirstSector = SectorIndex;
    CacheFree.NumSectors  = NumSectors;
    Result = FS__CACHE_CommandDeviceNL(pDevice, FS_CMD_CACHE_FREE_SECTORS, &CacheFree);
    if (Result != 0) {
      r = FS_ERRCODE_IOCTL_FAILURE;
    }
  }
#endif
  return r;
}

/*********************************************************************
*
*       FS_LB_FreeSectorsPart
*
*  Function description
*    Marks logical sectors as not in use.
*
*  Parameters
*    pPart          Partition on which the logical sectors are located.
*    SectorIndex    Index of the first logical sector to be freed.
*    NumSectors     Number of logical sectors to be freed.
*
*  Return value
*    ==0      OK, logical sector successfully freed.
*    !=0      An error occurred.
*
*  Additional information
*    SectorIndex is relative to the beginning of the MBR partition.
*    This function also frees any logical sectors that are located
*    in the sector cache or journal.
*/
int FS_LB_FreeSectorsPart(FS_PARTITION * pPart, U32 SectorIndex, U32 NumSectors) {
  FS_DEVICE * pDevice;
  int         r;

  pDevice      = &pPart->Device;
  SectorIndex += pPart->StartSector;      // Convert to absolute sector index.
  r = FS_LB_FreeSectorsDevice(pDevice, SectorIndex, NumSectors);
  return r;
}

/*********************************************************************
*
*       FS_LB_WriteBack
*
*  Function description
*    Writes a single logical sector to the storage device.
*
*  Parameters
*    pDevice          Instance of the storage device.
*    SectorIndex      Index of the first logical sector to be written.
*    pData            [IN] Contents of logical sectors.
*
*  Return value
*    ==0      OK, logical sector data written.
*    !=0      An error occurred.
*
*  Additional information
*    This function is typically called by the sector cache when a
*    dirty logical sector must be evicted (write back operation).
*/
int FS_LB_WriteBack(FS_DEVICE * pDevice, U32 SectorIndex, const void * pData) {
  int r;

  INC_WRITE_CACHE_CLEAN_CNT();
  r = _WriteToStorage(pDevice, SectorIndex, pData, 1, 0, 1);
  return r;
}

/*********************************************************************
*
*       FS_LB_ReadSectors
*
*  Function description
*    Reads logical sectors from storage device.
*
*  Parameters
*    pDevice        Instance of the storage device.
*    SectorIndex    Index of the first logical sector to read.
*    NumSectors     Number of logical sectors to read.
*    pData          [OUT] Read logical sector data.
*
*  Return value
*    ==0      OK, logical sector data read.
*    !=0      An error occurred.
*/
int FS_LB_ReadSectors(const FS_DEVICE * pDevice, U32 SectorIndex, U32 NumSectors, void * pData) {
  int r;

  r = _ReadSectors(pDevice, SectorIndex, NumSectors, pData);
  return r;
}

/*********************************************************************
*
*       FS_LB_WriteSectors
*
*  Function description
*    Writes logical sectors to storage device.
*
*  Parameters
*    pDevice        Instance of the storage device.
*    SectorIndex    Index of the first logical sector to be written.
*    NumSectors     Number of logical sectors to be written.
*    pData          [IN] Contents of logical sectors.
*    RepeatSame     Set to 1 if the logical sectors have to
*                   be filled with the same contents.
*
*  Return value
*    ==0      OK, logical sector data written.
*    !=0      An error occurred.
*/
int FS_LB_WriteSectors(FS_DEVICE * pDevice, U32 SectorIndex, U32 NumSectors, const void * pData, U8 RepeatSame) {
  int r;

  r = _WriteSectors(pDevice, SectorIndex, NumSectors, pData, RepeatSame);
  return r;
}

/*********************************************************************
*
*       FS_LB_FreeSectors
*
*  Function description
*    Marks logical sectors as not in use.
*
*  Parameters
*    pDevice        Instance of the storage device.
*    SectorIndex    Index of the first logical sector to be freed.
*    NumSectors     Number of logical sectors to be freed.
*
*  Return value
*    ==0      OK, logical sectors freed.
*    !=0      An error occurred.
*/
int FS_LB_FreeSectors(const FS_DEVICE * pDevice, U32 SectorIndex, U32 NumSectors) {
  int r;

  r = _FreeSectors(pDevice, SectorIndex, NumSectors);
  return r;
}

/*************************** End of file ****************************/
