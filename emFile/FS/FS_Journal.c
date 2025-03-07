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
File        : FS_Journal.c
Purpose     : Implementation of Journal for embedded file system
-------------------------- END-OF-HEADER -----------------------------
Additional information:

This is the layout of the data in the journal file:

  Sector range      Description
  -----------------------------
  0                 Status sector
  1 to N            Sector copy list
  N+1 to M-2        Sector copy data
  M-1               Info sector

  where:
    N is the number of sectors in the copy list.
    M is the total number of sectors in journal file.

This is the layout of the status sector:

  Byte range        Description
  -----------------------------
  0x00 to 0x0F      Signature ("Journal status\0")
  0x10 to 0x13      Number of sectors stored in journal file

This is the layout of the info sector:

  Byte range        Description
  -----------------------------
  0x00 to 0x0F      Signature ("Journal info\0\0\0")
  0x10 to 0x13      Version
  0x20 to 0x23      Size of the journal file in sectors
  0x30              Flag that indicates if the free sector operation is supported

This is the layout of an entry in the sector copy list:

  Byte range        Description
  -----------------------------
  0x00 to 0x03      Sector index
  0x04              Flag indicating if the sector has to be freed or not
  0x05 to 0x07      Reserved
  0x08 to 0x0B      Number of sectors in the range

  Each entry is 16 bytes long.
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#include <stdlib.h>
#include "FS_Int.h"

#if FS_SUPPORT_JOURNAL

/*********************************************************************
*
*       Defines, fixed
*
**********************************************************************
*/
#define VERSION                       10000uL
#define SIZEOF_SECTOR_LIST_ENTRY      16u
#define JOURNAL_INDEX_INVALID         0xFFFFFFFFUL
#define INFO_SECTOR_TAG               "Journal info\0\0\0"
#define MAN_SECTOR_TAG                "Journal status\0"
#define SIZEOF_INFO_SECTOR_TAG        16u
#define SIZEOF_MAN_SECTOR_TAG         16u
#define NUM_SECTORS_MIN               5u       // Minimum number of sectors required for journaling to properly operate.
#define OPEN_CNT_MAX                  0x7FFFu  // Maximum number of times the journal can be opened without closing it.

#if FS_JOURNAL_ENABLE_STATS
  #define IF_STATS(Exp) Exp
#else
  #define IF_STATS(Exp)
#endif

/*********************************************************************
*
*       Offsets in info sector
*/
#define OFF_INFO_VERSION              0x10
#define OFF_INFO_NUM_TOTAL_SECTORS    0x20
#define OFF_INFO_SUPPORT_FREE_SECTOR  0x30

/*********************************************************************
*
*       Offsets in status sector
*/
#define OFF_MAN_SECTOR_CNT            0x10

/*********************************************************************
*
*       Offsets in the entry of the sector list
*/
#define OFF_ENTRY_SECTOR_INDEX        0x00
#define OFF_ENTRY_SECTOR_NOT_USED     0x04
#if FS_JOURNAL_OPTIMIZE_SPACE_USAGE
  #define OFF_ENTRY_NUM_SECTORS       0x08
#endif // FS_JOURNAL_OPTIMIZE_SPACE_USAGE

/*********************************************************************
*
*       ASSERT_UNIT_NO_IS_IN_RANGE
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)                                    \
    if ((Unit) >= (unsigned)FS_NUM_VOLUMES) {                                 \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "JOURNAL: Invalid unit number.")); \
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);                                    \
    }
#else
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)
#endif

/*********************************************************************
*
*       FREE_DATA
*/
#if FS_JOURNAL_SUPPORT_FREE_SECTOR
  #define FREE_DATA(pInst)        (void)_FreeData(pInst)
#else
  #define FREE_DATA(pInst)
#endif

/*********************************************************************
*
*       MARK_SECTOR_AS_FREE
*/
#if FS_JOURNAL_SUPPORT_FREE_SECTOR
  #define MARK_SECTOR_AS_FREE(pInst, JournalIndex)    _MarkSectorAsFree(pInst, JournalIndex)
#else
  #define MARK_SECTOR_AS_FREE(pInst, JournalIndex)
#endif

/*********************************************************************
*
*       MARK_SECTOR_AS_USED
*/
#if FS_JOURNAL_SUPPORT_FREE_SECTOR
  #define MARK_SECTOR_AS_USED(pInst, JournalIndex)    _MarkSectorAsUsed(pInst, JournalIndex)
#else
  #define MARK_SECTOR_AS_USED(pInst, JournalIndex)
#endif

/*********************************************************************
*
*       Invokes the test hook function if the support for testing is enabled.
*/
#if FS_SUPPORT_TEST
  #define CALL_TEST_HOOK(Unit) \
    if (_pfTestHook != NULL) { \
      _pfTestHook(Unit);       \
    }
#else
  #define CALL_TEST_HOOK(Unit)
#endif

/*********************************************************************
*
*       Local types
*
**********************************************************************
*/

/*********************************************************************
*
*       JOURNAL_STATUS
*
*  Description
*    Operating status of one Journaling instance.
*
*  Additional information
*    SectorCntTotal is set to 0 when a transaction is opened
*    and increased by SectorCnt on each journal clean operation.
*/
typedef struct {
  U32                      NumSectorsData;          // Number of sectors available for data.
  U32                      BytesPerSector;          // Number of bytes per sector. Typically 512.
  U32                      PBIInfoSector;           // Physical sector index of last sector in the journal file. The contents of this sector never change.
  U32                      PBIStatusSector;         // Physical sector index of first sector in the journal file. Used to store status information.
  U32                      PBIStartSectorList;      // Physical sector index of first sector in the sector list.
  U32                      PBIFirstDataSector;      // Physical sector index of first sector used to store user data ("payload").
  U32                      SectorCnt;               // Number of sectors currently stored in the journal file.
#if FS_JOURNAL_ENABLE_STATS
  U32                      SectorCntTotal;          // Total number of sectors stored in the journal file during a transaction.
  FS_JOURNAL_STAT_COUNTERS StatCounters;            // Statistical counters used for debugging.
#endif // FS_JOURNAL_ENABLE_STATS
  U16                      OpenCnt;                 // Number of times the current transaction has been opened.
  I16                      Error;                   // Type of the error that occurred during the current transaction.
  U8                       IsPresent;               // Set to 1 if the journal file was found to be present on the storage.
  U8                       IsFreeSectorSupported;   // Set to 1 if the journal is configured to handle free sector operations.
} JOURNAL_STATUS;

/*********************************************************************
*
*       JOURNAL_INST
*
*  Description
*    Information related to one Journaling instance.
*/
typedef struct {
  JOURNAL_STATUS   Status;              // Information about the journal operation.
  U8             * pJ2P;                // Journal to physical table. Input: journal index (file system view). Output: Physical index (hardware/driver view)
  FS_VOLUME      * pVolume;             // Volume on which the journal file is located.
#if FS_JOURNAL_SUPPORT_FREE_SECTOR
  U8             * paIsSectorFree;      // A bit in this array is set to 1 to indicate if a logical sector has to be freed.
#endif // FS_JOURNAL_SUPPORT_FREE_SECTOR
#if FS_JOURNAL_OPTIMIZE_SPACE_USAGE
  U8             * pSC;                 // Table that stores the number of sectors contained in a journal entry.
#endif // FS_JOURNAL_OPTIMIZE_SPACE_USAGE
  U32              NumEntries;          // Number of journal entries for which memory was allocated.
#if (FS_JOURNAL_SUPPORT_FAST_SECTOR_SEARCH == 0)
  U8               NumBitsSectorIndex;  // Size in bits of an entry in the journal to physical table.
#endif // FS_JOURNAL_SUPPORT_FAST_SECTOR_SEARCH == 0
#if (FS_JOURNAL_OPTIMIZE_SPACE_USAGE != 0) && (FS_JOURNAL_SUPPORT_FAST_SECTOR_SEARCH == 0)
  U8               NumBitsSectorCnt;    // Size in bits of an entry in the number of sectors table.
#endif // FS_JOURNAL_OPTIMIZE_SPACE_USAGE != 0 && FS_JOURNAL_SUPPORT_FAST_SECTOR_SEARCH == 0
} JOURNAL_INST;

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static JOURNAL_INST                       * _apInst[FS_NUM_VOLUMES];
static FS_JOURNAL_ON_OVERFLOW_CALLBACK    * _pfOnOverflow;
static FS_JOURNAL_ON_OVERFLOW_EX_CALLBACK * _pfOnOverflowEx;
#if FS_SUPPORT_TEST
  static FS_JOURNAL_TEST_HOOK             * _pfTestHook;
#endif

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

//lint -esym(9070, _Write) Recursive function [MISRA 2012 Rule 17.2, required] N:100. Rationale: Verified manually as being false.
//lint -esym(9070, _AddFreeSectors) Recursive function [MISRA 2012 Rule 17.2, required] N:100. Rationale: Verified manually as being false.
//lint -esym(9070, _WriteOneSector) Recursive function [MISRA 2012 Rule 17.2, required] N:100. Rationale: Verified manually as being false.
//lint -esym(9070, _WriteOneSectorEx) Recursive function [MISRA 2012 Rule 17.2, required] N:100. Rationale: Verified manually as being false.
//lint -esym(9070, _CleanJournal) Recursive function [MISRA 2012 Rule 17.2, required] N:100. Rationale: Verified manually as being false.
//lint -esym(9070, _AddSector) Recursive function [MISRA 2012 Rule 17.2, required] N:100. Rationale: Rationale: Verified manually as being false.
//lint -esym(9070, FS__JOURNAL_Write) Recursive function [MISRA 2012 Rule 17.2, required] N:100. Rationale: Verified manually as being false.
//lint -efunc(818, _ReadOneDeviceSector) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory] N:104. Rationale: pInst cannot be declared as pointing to const because the statistical counters have to be updated in debug builds.
//lint -efunc(818, _ReadDeviceSectors) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory] N:104. Rationale: pInst cannot be declared as pointing to const because the statistical counters have to be updated in debug builds.
//lint -efunc(818, _WriteOneDeviceSector) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory] N:104. Rationale: pInst cannot be declared as pointing to const because the statistical counters have to be updated in debug builds.
//lint -efunc(818, _FreeDeviceSectors) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory] N:104. Rationale: pInst cannot be declared as pointing to const because the statistical counters have to be updated in debug builds.

/*********************************************************************
*
*       _OnOverflow
*
*  Function description
*    Invokes the registered callback function on a journal overflow.
*/
static int _OnOverflow(JOURNAL_INST * pInst) {
  int         r;
  FS_VOLUME * pVolume;

  r = 0;
  IF_STATS(pInst->Status.StatCounters.OverflowCnt++);
  FS_DEBUG_WARN((FS_MTYPE_JOURNAL, "JOURNAL: _OnOverflow: Journal file is full."));       // Note: Journal can no longer guarantee that operations are atomic!
  //
  // Call the standard callback if registered.
  //
  if (_pfOnOverflow != NULL) {
    char        acVolumeName[32];
    int         NumBytes;
    I32         NumBytesFree;
    char      * p;

    pVolume = pInst->pVolume;
    FS_MEMSET(acVolumeName, 0, sizeof(acVolumeName));
    NumBytes = FS__GetVolumeName(pVolume, acVolumeName, (int)sizeof(acVolumeName));
    if (NumBytes > (int)sizeof(acVolumeName)) {
      //
      // The buffer allocated on the stack for the volume name is not large enough.
      // Try to use the free memory from the memory pool as buffer.
      //
      p = SEGGER_PTR2PTR(char, FS_GetFreeMem(&NumBytesFree));                             // MISRA deviation D:100[d]
      if (p != NULL) {
        if (NumBytesFree > NumBytes) {
          (void)FS__GetVolumeName(pVolume, acVolumeName, (int)sizeof(acVolumeName));
        }
      }
    }
    _pfOnOverflow(acVolumeName);
  }
  //
  // Call the alternative callback if registered.
  //
  if (_pfOnOverflowEx != NULL) {
    FS_JOURNAL_OVERFLOW_INFO   OverflowInfo;
    U8                         VolumeIndex;
    unsigned                   NumVolumes;
    FS_VOLUME                * pVolumeToCheck;

    pVolume = pInst->pVolume;
    FS_MEMSET(&OverflowInfo, 0, sizeof(OverflowInfo));
    //
    // Calculate the index of the volume on which the overflow occurred.
    // The application can get the name of the volume using the FS_GetVolumeName() API function.
    //
    VolumeIndex = 0;
    pVolumeToCheck = &FS_Global.FirstVolume;
    NumVolumes     = FS_Global.NumVolumes;
    if (NumVolumes != 0u) {
      do {
        if (pVolumeToCheck == pVolume) {
          break;
        }
        pVolumeToCheck = pVolumeToCheck->pNext;
        ++VolumeIndex;
      } while (--NumVolumes != 0u);
    }
    OverflowInfo.VolumeIndex = VolumeIndex;
    r = _pfOnOverflowEx(&OverflowInfo);
  }
  if (r == 0) {
    //
    // Journal can no longer guarantee that operations are atomic until the end of the transaction.
    //
    FS_DEBUG_WARN((FS_MTYPE_JOURNAL, "JOURNAL: _OnOverflow: Cleaning journal file."));
  } else {
    pInst->Status.Error = FS_ERRCODE_TRANSACTION_ABORTED;
    FS_DEBUG_WARN((FS_MTYPE_JOURNAL, "JOURNAL: _OnOverflow: Aborting transaction."));
  }
  return r;
}

/*********************************************************************
*
*       _Volume2Inst
*
*  Function description
*    Maps a volume to a journal instance.
*/
static JOURNAL_INST * _Volume2Inst(FS_VOLUME * pVolume) {
  U8             Unit;
  JOURNAL_INST * pInst;
  FS_VOLUME    * pVolumeToCheck;

  pVolumeToCheck = &FS_Global.FirstVolume;
  for (Unit = 0; Unit < FS_Global.NumVolumes; Unit++) {
    if (pVolume == pVolumeToCheck) {
      break;
    }
    pVolumeToCheck = pVolumeToCheck->pNext;
  }
  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  pInst = NULL;
  if (Unit < (U8)FS_NUM_VOLUMES) {
    pInst = _apInst[Unit];
    if (pInst == NULL) {
      pInst = SEGGER_PTR2PTR(JOURNAL_INST, FS_ALLOC_ZEROED((I32)sizeof(JOURNAL_INST), "JOURNAL_INST"));   // MISRA deviation D:100[d]
      if (pInst != NULL) {
        _apInst[Unit] = pInst;
        pInst->pVolume = pVolume;
        pVolume->Partition.Device.Data.JournalData.Unit     = Unit;
        pVolume->Partition.Device.Data.JournalData.IsInited = 1;
      }
    }
  }
  return pInst;
}

/*********************************************************************
*
*       _ReadOneDeviceSector
*/
static int _ReadOneDeviceSector(JOURNAL_INST * pInst, U32 SectorIndex, U8 * pData) {
  FS_DEVICE * pDevice;
  int         r;

  pDevice = &pInst->pVolume->Partition.Device;
  r = FS_LB_ReadSectors(pDevice, SectorIndex, 1, pData);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_JOURNAL, "JOURNAL: _ReadOneDeviceSector: Operation failed."));
  } else {
    FS_DEBUG_LOG((FS_MTYPE_STORAGE, "JOURNAL:  READ_SECTOR   VN: \"%s:%d:\", ST: ---, SI: %lu\n", pDevice->pType->pfGetName(pDevice->Data.Unit), pDevice->Data.Unit, SectorIndex));
    //
    // OK, sector data read.
    //
    IF_STATS(pInst->Status.StatCounters.ReadSectorCntStorage++);
  }
  return r;
}

/*********************************************************************
*
*       _ReadDeviceSectors
*/
static int _ReadDeviceSectors(JOURNAL_INST * pInst, U32 SectorIndex, U8 * pData, U32 NumSectors) {
  FS_DEVICE * pDevice;
  int         r;

  pDevice = &pInst->pVolume->Partition.Device;
  r = FS_LB_ReadSectors(pDevice, SectorIndex, NumSectors, pData);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_JOURNAL, "JOURNAL: _ReadDeviceSectors: Operation failed."));
  } else {
    FS_DEBUG_LOG((FS_MTYPE_STORAGE, "JOURNAL:  READ_SECTORS  VN: \"%s:%d:\", ST: ---, SI: %lu, NS: %d\n", pDevice->pType->pfGetName(pDevice->Data.Unit), pDevice->Data.Unit, SectorIndex, NumSectors));
    //
    // OK, sector data read.
    //
    IF_STATS(pInst->Status.StatCounters.ReadSectorCntStorage += NumSectors);
  }
  return r;
}

/*********************************************************************
*
*       _WriteOneDeviceSector
*/
static int _WriteOneDeviceSector(JOURNAL_INST * pInst, U32 SectorIndex, const U8 * pData) {
  FS_DEVICE * pDevice;
  int         r;

  pDevice = &pInst->pVolume->Partition.Device;
  r = FS_LB_WriteSectors(pDevice, SectorIndex, 1, pData, 0);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_JOURNAL, "JOURNAL: _WriteOneDeviceSector: Operation failed."));
  } else {
    FS_DEBUG_LOG((FS_MTYPE_STORAGE, "JOURNAL:  WRITE_SECTOR  VN: \"%s:%d:\", ST: ---, SI: %lu\n", pDevice->pType->pfGetName(pDevice->Data.Unit), pDevice->Data.Unit, SectorIndex));
    //
    // OK, sector data written.
    //
    IF_STATS(pInst->Status.StatCounters.WriteSectorCntStorage++);
  }
  return r;
}

#if FS_JOURNAL_SUPPORT_FREE_SECTOR

/*********************************************************************
*
*       _FreeDeviceSectors
*/
static int _FreeDeviceSectors(JOURNAL_INST * pInst, U32 SectorIndex, U32 NumSectors) {
  FS_DEVICE * pDevice;
  int         r;

  pDevice = &pInst->pVolume->Partition.Device;
  r = FS_LB_FreeSectors(pDevice, SectorIndex, NumSectors);
  if (r == 0) {
    FS_DEBUG_LOG((FS_MTYPE_STORAGE, "JOURNAL:  FREE_SECTORS  VN: \"%s:%d:\", ST: ---, SI: %lu, NS: %lu\n", pDevice->pType->pfGetName(pDevice->Data.Unit), pDevice->Data.Unit, SectorIndex, NumSectors));
    IF_STATS(pInst->Status.StatCounters.FreeOperationCntStorage++);
    IF_STATS(pInst->Status.StatCounters.FreeSectorCntStorage += NumSectors);
  }
  return r;
}

#endif // FS_JOURNAL_SUPPORT_FREE_SECTOR

/*********************************************************************
*
*       _J2P_Read
*
*  Function description
*    Returns the contents of the specified entry in the J2P table.
*
*  Parameters
*    pInst          Journal instance.
*    JournalIndex   Entry index (0-based)
*
*  Return value
*    Index of the logical sector stored in this entry.
*/
static U32 _J2P_Read(const JOURNAL_INST * pInst, U32 JournalIndex) {
  U32 v;

#if FS_JOURNAL_SUPPORT_FAST_SECTOR_SEARCH
  {
    U32 * p;

    p = SEGGER_PTR2PTR(U32, pInst->pJ2P);                                                 // MISRA deviation D:100[c]
    v = *(p + JournalIndex);
  }
#else
  v = FS_BITFIELD_ReadEntry(pInst->pJ2P, JournalIndex, pInst->NumBitsSectorIndex);
#endif
  return v;
}

/*********************************************************************
*
*       _J2P_Write
*
*  Function description
*    Updates the contents of the specified entry in the J2P table.
*
*  Parameters
*    pInst          Journal instance.
*    JournalIndex   Entry index (0-based)
*    SectorIndex    Index of the logical sector stored in this entry.
*/
static void _J2P_Write(const JOURNAL_INST * pInst, U32 JournalIndex, U32 SectorIndex) {
#if FS_JOURNAL_SUPPORT_FAST_SECTOR_SEARCH
  U32 * p;

  p = SEGGER_PTR2PTR(U32, pInst->pJ2P);                                                   // MISRA deviation D:100[c]
  *(p + JournalIndex) = SectorIndex;
#else
  FS_BITFIELD_WriteEntry(pInst->pJ2P, JournalIndex, pInst->NumBitsSectorIndex, SectorIndex);
#endif
}

/*********************************************************************
*
*       _J2P_GetSize
*
*  Function description
*    Calculates and returns the size of the J2P assignment table.
*
*  Parameters
*    pInst      Journal instance.
*
*  Return value
*    Size of the table in bytes.
*
*  Additional information
*    This function is called before allocation of the J2P assignment
*    table to find out how many bytes need to be allocated.
*/
static unsigned _J2P_GetSize(const JOURNAL_INST * pInst) {
  unsigned v;

#if FS_JOURNAL_SUPPORT_FAST_SECTOR_SEARCH
  v = pInst->NumEntries << 2;           // 4 bytes are allocated for each entry.
#else
  v = FS_BITFIELD_CalcSize(pInst->NumEntries, pInst->NumBitsSectorIndex);
#endif
  return v;
}

#if FS_JOURNAL_OPTIMIZE_SPACE_USAGE

/*********************************************************************
*
*       _SC_Read
*
*  Function description
*    Returns the contents of the specified entry in the sector count table.
*
*  Parameters
*    pInst          Journal instance.
*    JournalIndex   Entry index (0-based)
*
*  Return value
*    Number of sectors stored in the entry.
*/
static U32 _SC_Read(const JOURNAL_INST * pInst, U32 JournalIndex) {
  U32 v;

#if FS_JOURNAL_SUPPORT_FAST_SECTOR_SEARCH
  {
    U32 * p;

    p = SEGGER_PTR2PTR(U32, pInst->pSC);                                                  // MISRA deviation D:100[c]
    v = *(p + JournalIndex);
  }
#else
  v = FS_BITFIELD_ReadEntry(pInst->pSC, JournalIndex, pInst->NumBitsSectorCnt);
#endif
  return v;
}

/*********************************************************************
*
*       _SC_Write
*
*  Function description
*    Updates the contents of the specified entry in the sector count table.
*
*  Parameters
*    pInst          Journal instance.
*    JournalIndex   Entry index (0-based)
*    SectorCnt      Number of sectors in the journal entry.
*/
static void _SC_Write(const JOURNAL_INST * pInst, U32 JournalIndex, U32 SectorCnt) {
#if FS_JOURNAL_SUPPORT_FAST_SECTOR_SEARCH
  U32 * p;

  p = SEGGER_PTR2PTR(U32, pInst->pSC);                                                    // MISRA deviation D:100[c]
  *(p + JournalIndex) = SectorCnt;
#else
  FS_BITFIELD_WriteEntry(pInst->pSC, JournalIndex, pInst->NumBitsSectorCnt, SectorCnt);
#endif
}

/*********************************************************************
*
*       _SC_GetSize
*
*  Function description
*    Calculates and returns the size of the sector count table.
*
*  Parameters
*    pInst      Journal instance.
*
*  Return value
*    Size of the table in bytes.
*
*  Additional information
*    This function is called before allocation of the sector count
*    table to find out how many bytes need to be allocated.
*    The same number of entries are allocated as the for the
*    J2P assignment table.
*/
static unsigned _SC_GetSize(const JOURNAL_INST * pInst) {
  unsigned v;

#if FS_JOURNAL_SUPPORT_FAST_SECTOR_SEARCH
  v = pInst->NumEntries << 2;           // 4 bytes are allocated for each entry.
#else
  v = FS_BITFIELD_CalcSize(pInst->NumEntries, pInst->NumBitsSectorCnt);
#endif
  return v;
}

#endif // FS_JOURNAL_OPTIMIZE_SPACE_USAGE != 0

/*********************************************************************
*
*       _IsSectorFree
*
*  Function description
*    Checks if a sector is marked as free.
*
*  Parameters
*    pInst          Journal instance.
*    JournalIndex   Index of the sector in the journal.
*
*  Return value
*    !=0    Sector is marked as free.
*    ==0    Sector is not marked as free.
*/
static int _IsSectorFree(const JOURNAL_INST * pInst, U32 JournalIndex) {
#if FS_JOURNAL_SUPPORT_FREE_SECTOR
  U8 Data;

  if (pInst->Status.IsFreeSectorSupported == 0u) {
    return 0;
  }
  Data   = *(pInst->paIsSectorFree + (JournalIndex >> 3));
  Data >>= JournalIndex & 7u;
  if ((Data & 1u) != 0u) {
    return 1;
  }
  return 0;
#else
  FS_USE_PARA(pInst);
  FS_USE_PARA(JournalIndex);
  return 0;
#endif // FS_JOURNAL_SUPPORT_FREE_SECTOR
}

#if FS_JOURNAL_SUPPORT_FREE_SECTOR

/*********************************************************************
*
*       _MarkSectorAsUsed
*
*  Function description
*    Marks a sector as containing valid data.
*/
static void _MarkSectorAsUsed(const JOURNAL_INST * pInst, U32 JournalIndex) {
  unsigned Mask;
  unsigned Off;
  unsigned Data;

  if (pInst->Status.IsFreeSectorSupported != 0u) {
    Off   = JournalIndex >> 3;
    Mask  = 1uL << (JournalIndex & 7u);
    Data  = pInst->paIsSectorFree[Off];
    Data &= ~Mask;
    pInst->paIsSectorFree[Off] = (U8)Data;
  }
}

/*********************************************************************
*
*       _MarkSectorAsFree
*
*  Function description
*    Marks a sector as containing invalid data.
*/
static void _MarkSectorAsFree(const JOURNAL_INST * pInst, U32 JournalIndex) {
  unsigned Mask;
  unsigned Off;
  unsigned Data;

  if (pInst->Status.IsFreeSectorSupported != 0u) {
    Off   = JournalIndex >> 3;
    Mask  = 1uL << (JournalIndex & 7u);
    Data  = pInst->paIsSectorFree[Off];
    Data |= Mask;
    pInst->paIsSectorFree[Off] = (U8)Data;
  }
}

#endif // FS_JOURNAL_SUPPORT_FREE_SECTOR

/*********************************************************************
*
*       _InitStatus
*
*  Function description
*    Initialize the status data.
*    This makes sure the all routine that depend on this status
*    are working with correct status information.
*/
static void _InitStatus(JOURNAL_INST * pInst) {
  JOURNAL_STATUS * pStatus;

  pStatus = &pInst->Status;
  //
  // Invalidate all information by initializing it with 0.
  //
  FS_MEMSET(pStatus, 0, sizeof(JOURNAL_STATUS));
}

/*********************************************************************
*
*       _InitInst
*
*  Function description
*    Initializes a journal instance.
*
*  Parameters
*    pInst                  Journal instance.
*    FirstSector            Index of the first logical sector in the journal file.
*    NumSectors             Number of logical sectors in the journal file.
*    IsFreeSectorSupported  Enables or disables the handling of free sector request.
*
*  Return value
*    ==0      OK, instance initialized successfully.
*    !=0      Error code indicating the failure reason.
*/
static int _InitInst(JOURNAL_INST * pInst, U32 FirstSector, U32 NumSectors, U8 IsFreeSectorSupported) {
  U32 NumBytes;
  U32 NumSectorsData;
  U32 NumSectorsManagement;
  U32 FirstSectorAfterJournal;
  U32 BytesPerSector;

  _InitStatus(pInst);
  FirstSectorAfterJournal              = FirstSector + NumSectors;
  pInst->Status.PBIInfoSector          = FirstSectorAfterJournal - 1u;                          // Info sector. Contents never change
  pInst->Status.PBIStatusSector        = FirstSectorAfterJournal - NumSectors;                  // Status sector. First sector in journal.
  pInst->Status.PBIStartSectorList     = pInst->Status.PBIStatusSector + 1u;                    // Start of sector list
  pInst->Status.IsFreeSectorSupported  = IsFreeSectorSupported;
  //
  // Compute the number of sectors which can be used to store data.
  //
  BytesPerSector       = pInst->pVolume->FSInfo.Info.BytesPerSector;
  NumBytes             = (NumSectors - 3u) * BytesPerSector;                                    // Total number of bytes for data & management. 3 sectors subtract for info, status and head of sector list.
  NumSectorsData       = NumBytes / (BytesPerSector + SIZEOF_SECTOR_LIST_ENTRY);                // This computation is a bit simplified and may waste one sector in some cases.
  NumSectorsManagement = FS__DivideU32Up(NumSectorsData * SIZEOF_SECTOR_LIST_ENTRY, BytesPerSector);
  //
  // Store information in the instance structure.
  //
  pInst->Status.BytesPerSector     = BytesPerSector;
  pInst->Status.NumSectorsData     = NumSectorsData;
  pInst->Status.PBIFirstDataSector = pInst->Status.PBIStartSectorList + NumSectorsManagement;   // Data sectors follow sector list
  //
  // Initialize the tables. The memory is allocated at the first call to this function
  // after the file system initialization. We have to limit here the number of sectors
  // that can be stored in the journal to the number of entries allocated in order to
  // prevent that we write outside the tables. This case can occur when the a removable
  // storage device is mounted with journal file size larger than that of the storage
  // device mounted first after the file system initialization
  // or when the journal size is increased without reinitializing the file system.
  //
  if (pInst->NumEntries == 0u) {
    pInst->NumEntries = NumSectorsData;
  } else {
    if (pInst->NumEntries < NumSectorsData) {
      pInst->Status.NumSectorsData = pInst->NumEntries;
    }
  }
#if (FS_JOURNAL_SUPPORT_FAST_SECTOR_SEARCH == 0)
  if (pInst->NumBitsSectorIndex == 0u) {
    U32 MaxSectorIndex;

    MaxSectorIndex = pInst->Status.PBIStatusSector - 1u;
    pInst->NumBitsSectorIndex = (U8)FS_BITFIELD_CalcNumBitsUsed(MaxSectorIndex);
  }
#endif // FS_JOURNAL_SUPPORT_FAST_SECTOR_SEARCH == 0
  FS_ALLOC_ZEROED_PTR(SEGGER_PTR2PTR(void *, &pInst->pJ2P), (I32)_J2P_GetSize(pInst), "JOURNAL_SECTOR_MAP");        // MISRA deviation D:100[d]
  if (pInst->pJ2P == NULL) {
    return FS_ERRCODE_OUT_OF_MEMORY;
  }
#if FS_JOURNAL_SUPPORT_FREE_SECTOR
  if (IsFreeSectorSupported != 0u) {
    NumBytes = (pInst->NumEntries + 7u) >> 3;    // 1 bit per sector
    //
    // Allocate and initialize the bit array of free sectors.
    //
    FS_ALLOC_ZEROED_PTR(SEGGER_PTR2PTR(void *, &pInst->paIsSectorFree), (I32)NumBytes, "JOURNAL_FREE_SECTOR_MAP");  // MISRA deviation D:100[d]
    if (pInst->paIsSectorFree == NULL) {
      return FS_ERRCODE_OUT_OF_MEMORY;
    }
  }
#endif // FS_JOURNAL_SUPPORT_FREE_SECTOR
#if FS_JOURNAL_OPTIMIZE_SPACE_USAGE
#if (FS_JOURNAL_SUPPORT_FAST_SECTOR_SEARCH == 0)
  if (pInst->NumBitsSectorCnt == 0u) {
    U32 MaxSectorCnt;

    MaxSectorCnt = pInst->Status.PBIStatusSector;
    pInst->NumBitsSectorCnt = (U8)FS_BITFIELD_CalcNumBitsUsed(MaxSectorCnt);
  }
#endif // FS_JOURNAL_SUPPORT_FAST_SECTOR_SEARCH == 0
  FS_ALLOC_ZEROED_PTR(SEGGER_PTR2PTR(void *, &pInst->pSC), (I32)_SC_GetSize(pInst), "JOURNAL_SECTOR_CNT");          // MISRA deviation D:100[d]
  if (pInst->pSC == NULL) {
    return FS_ERRCODE_OUT_OF_MEMORY;
  }
#endif // FS_JOURNAL_OPTIMIZE_SPACE_USAGE
  return 0;
}

/*********************************************************************
*
*       _FindSector
*
*  Function description
*    Locates a logical sector (as seen by the file system) in the journal.
*
*  Parameters
*    pInst          Journal instance.
*    SectorIndex    Index of the sector to be searched.
*
*  Return value
*    !=JOURNAL_INDEX_INVALID    Index of the sector in journal.
*    ==JOURNAL_INDEX_INVALID    Sector not in journal.
*/
static U32 _FindSector(const JOURNAL_INST * pInst, U32 SectorIndex) {
  U32 JournalIndex;
  U32 NumSectors;

  NumSectors = pInst->Status.SectorCnt;
  for (JournalIndex = 0; JournalIndex < NumSectors; JournalIndex++) {
#if FS_JOURNAL_OPTIMIZE_SPACE_USAGE
    U32 NumSectorsRange;
    U32 SectorIndexRange;

    SectorIndexRange = _J2P_Read(pInst, JournalIndex);
    NumSectorsRange  = _SC_Read(pInst, JournalIndex);
    if (   (SectorIndex >= SectorIndexRange)
        && (SectorIndex < (SectorIndexRange + NumSectorsRange))) {
      return JournalIndex;              // Sector is present in journal.
    }
#else
    if (_J2P_Read(pInst, JournalIndex) == SectorIndex) {
      return JournalIndex;              // Sector is present in journal.
    }
#endif // FS_JOURNAL_OPTIMIZE_SPACE_USAGE
  }
  return JOURNAL_INDEX_INVALID;         // Sector is not present in journal.
}

#if (FS_JOURNAL_OPTIMIZE_SPACE_USAGE != 0) && (FS_JOURNAL_SUPPORT_FREE_SECTOR != 0)

/*********************************************************************
*
*       _FindSectorEx
*
*  Function description
*    Locates a logical sector (as seen by the file system) in the journal.
*
*  Parameters
*    pInst          Journal instance.
*    SectorIndex    Index of the sector to be searched.
*    IsSectorFree   Set to 1 if the sector has to be freed.
*
*  Return value
*    !=JOURNAL_INDEX_INVALID    Index of the sector in journal.
*    ==JOURNAL_INDEX_INVALID    Sector not in journal.
*
*  Additional information
*    This function performs the same operation as _FindSector().
*    In addition, it returns a valid journal index when it finds
*    a range of free sectors to which the sector may be added.
*/
static U32 _FindSectorEx(const JOURNAL_INST * pInst, U32 SectorIndex, int IsSectorFree) {
  U32 JournalIndex;
  U32 NumSectors;
  U32 NumSectorsRange;
  U32 SectorIndexRange;
  U32 JournalIndexRange;

  NumSectors        = pInst->Status.SectorCnt;
  JournalIndexRange = JOURNAL_INDEX_INVALID;        // Set to indicate that the sector is not present in journal.
  for (JournalIndex = 0; JournalIndex < NumSectors; JournalIndex++) {
    SectorIndexRange = _J2P_Read(pInst, JournalIndex);
    NumSectorsRange  = _SC_Read(pInst, JournalIndex);
    if (   (SectorIndex >= SectorIndexRange)
        && (SectorIndex < (SectorIndexRange + NumSectorsRange))) {
      return JournalIndex;                          // Sector is present in journal.
    }
    //
    // Check if the sector range may be extended to store the sector in it.
    // We can extend only sector ranges that perform the same operation that
    // is either write or free.
    //
    if (_IsSectorFree(pInst, JournalIndex) == IsSectorFree) {
      if ((SectorIndexRange > 0u) && (SectorIndex == (SectorIndexRange - 1u))) {
        JournalIndexRange = JournalIndex;
      } else {
        if ((SectorIndex == (SectorIndexRange + NumSectorsRange))) {
          JournalIndexRange = JournalIndex;
        }
      }
    }
  }
  return JournalIndexRange;
}

#endif // (FS_JOURNAL_OPTIMIZE_SPACE_USAGE != 0) && (FS_JOURNAL_SUPPORT_FREE_SECTOR != 0)

/*********************************************************************
*
*       _CopyData
*
*  Function description
*    Copies data from journal to original destination.
*
*  Parameters
*    pInst        Journal instance.
*    pData        Sector buffer to be used for the copy operation.
*
*  Return value
*    ==0        OK, sectors copied successfully.
*    !=0        An error occurred.
*/
static int _CopyData(JOURNAL_INST * pInst, U8 * pData) {
  U32 SectorCnt;
  U32 SectorIndex;
  U32 JournalIndex;
  int r;

  r         = 0;      // No error so far.
  SectorCnt = pInst->Status.SectorCnt;
  for (JournalIndex = 0; JournalIndex < SectorCnt; JournalIndex++) {
    if (_IsSectorFree(pInst, JournalIndex) == 0) {
      //
      // Read from journal.
      //
      SectorIndex = pInst->Status.PBIFirstDataSector + JournalIndex;
      r = _ReadOneDeviceSector(pInst, SectorIndex, pData);
      if (r != 0) {
        break;        // Error, could not read sector.
      }
      //
      // Write to storage device.
      //
      SectorIndex = _J2P_Read(pInst, JournalIndex);
      r = _WriteOneDeviceSector(pInst, SectorIndex, pData);
      if (r != 0) {
        break;        // Error, could not write sector.
      }
    }
  }
  return r;
}

#if FS_JOURNAL_SUPPORT_FREE_SECTOR

/*********************************************************************
*
*       _FreeData
*
*  Function description
*    Invalidates sector data.
*
*  Parameters
*    pInst        Journal instance.
*
*  Return value
*    ==0      OK, sector data invalidated.
*    !=0      An error occurred.
*/
static int _FreeData(JOURNAL_INST * pInst) {
  int r;
  U32 SectorCnt;
  U32 JournalIndex;
  U32 SectorIndex;
  U32 StartSector;
  U32 NumSectors;
#if FS_JOURNAL_OPTIMIZE_SPACE_USAGE
  U32 NumSectorsRange;
#endif // FS_JOURNAL_OPTIMIZE_SPACE_USAGE

  r = 0;      // Set to indicate success.
  if (pInst->Status.IsFreeSectorSupported != 0u) {
    SectorCnt = pInst->Status.SectorCnt;
    if (SectorCnt != 0u) {
      StartSector = SECTOR_INDEX_INVALID;
      NumSectors  = 0;
      for (JournalIndex = 0; JournalIndex < SectorCnt; ++JournalIndex) {
        if (_IsSectorFree(pInst, JournalIndex) != 0) {
          SectorIndex = _J2P_Read(pInst, JournalIndex);
          if (NumSectors == 0u) {
            StartSector = SectorIndex;
#if (FS_JOURNAL_OPTIMIZE_SPACE_USAGE == 0)
            ++NumSectors;
#else
            NumSectorsRange = _SC_Read(pInst, JournalIndex);
            NumSectors += NumSectorsRange;
#endif // FS_JOURNAL_OPTIMIZE_SPACE_USAGE == 0
          } else {
            if ((StartSector + NumSectors) == SectorIndex) {
#if (FS_JOURNAL_OPTIMIZE_SPACE_USAGE == 0)
              ++NumSectors;
#else
              NumSectorsRange = _SC_Read(pInst, JournalIndex);
              NumSectors += NumSectorsRange;
#endif // FS_JOURNAL_OPTIMIZE_SPACE_USAGE == 0
            } else {
              r = _FreeDeviceSectors(pInst, StartSector, NumSectors);
              if (r != 0) {
                return 1;     // Error, could not free sector.
              }
              StartSector = SectorIndex;
#if (FS_JOURNAL_OPTIMIZE_SPACE_USAGE == 0)
              NumSectors  = 1;
#else
              NumSectors  = _SC_Read(pInst, JournalIndex);
#endif // FS_JOURNAL_OPTIMIZE_SPACE_USAGE == 0
            }
          }
        }
      }
      if (NumSectors != 0u) {
        r = _FreeDeviceSectors(pInst, StartSector, NumSectors);
        if (r != 0) {
          return 1;     // Error, could not free sector.
        }
      }
    }
  }
  return r;
}

#endif // FS_JOURNAL_SUPPORT_FREE_SECTOR

/*********************************************************************
*
*       _ClearSectorList
*
*  Function description
*    Clears the list of sectors stored in memory.
*/
static void _ClearSectorList(JOURNAL_INST * pInst) {
  U32 NumBytes;

  NumBytes = _J2P_GetSize(pInst);
  FS_MEMSET(pInst->pJ2P, 0, NumBytes);
#if FS_JOURNAL_SUPPORT_FREE_SECTOR
  if (pInst->Status.IsFreeSectorSupported != 0u) {
    NumBytes = (pInst->NumEntries + 7u) >> 3;    // 1 bit per sector
    FS_MEMSET(pInst->paIsSectorFree, 0, NumBytes);
  }
#endif // FS_JOURNAL_SUPPORT_FREE_SECTOR
#if FS_JOURNAL_OPTIMIZE_SPACE_USAGE
  NumBytes = _SC_GetSize(pInst);
  FS_MEMSET(pInst->pSC, 0, NumBytes);
#endif // FS_JOURNAL_OPTIMIZE_SPACE_USAGE
  pInst->Status.SectorCnt = 0;
}

/*********************************************************************
*
*       _ResetJournal
*
*  Function description
*    Clear the journal. This means resetting the sector count and updating (= writing to device) management info.
*/
static int _ResetJournal(JOURNAL_INST * pInst, U8 * pData) {
  int r;

  //
  // Prepare and write status sector.
  //
  FS_MEMSET(pData, 0xFF, pInst->Status.BytesPerSector);
  FS_MEMCPY(pData, MAN_SECTOR_TAG, 16);                           //lint !e840 Use of nul character in a string literal N:100. Reason: This is the most efficient way to fill the unused characters with 0.
  FS_StoreU32LE(pData + OFF_MAN_SECTOR_CNT, 0);
  r = _WriteOneDeviceSector(pInst, pInst->Status.PBIStatusSector, pData);
  //
  // Clear the list of sectors stored in memory.
  //
  _ClearSectorList(pInst);
  return r;
}

/*********************************************************************
*
*       _CleanJournal
*
*  Function description
*    Copies data from the journal file to original destination.
*
*  Parameters
*    pInst      Journal instance.
*
*  Return value
*    ==0    OK, data copied.
*    !=0    An error occurred.
*
*  Additional information
*    This routines copies the data in the journal to the original destination
*    and cleans the journal in the following steps:
*    - Write journal management info
*    - Copy data
*    - Clear journal (rewriting management info)
*/
static int _CleanJournal(JOURNAL_INST * pInst) {
  U32     JournalIndex;
  U32     SectorCnt;
  U32     SectorIndex;
  U8    * pData;
  FS_SB   sb;
  U32     BytesPerSector;
  U32     Off;
  U32     BPSMask;
  int     r;
  int     rClear;
  U8      IsSectorFree;

  r = 0;
  SectorCnt = pInst->Status.SectorCnt;
  BytesPerSector = pInst->Status.BytesPerSector;
  //
  // Write out the journal only if there are any sectors written to it.
  //
  if (SectorCnt != 0u) {
    (void)FS__SB_Create(&sb, pInst->pVolume);
    pData = FS__SB_GetBuffer(&sb);
    FS_MEMSET(pData, 0xFF, BytesPerSector);
    if (SectorCnt > 1u) {
      //
      // Prepare the list of sectors to be copied.
      //
      BPSMask = BytesPerSector - 1u;
      for (JournalIndex = 0; JournalIndex < SectorCnt; JournalIndex++) {
        Off = JournalIndex * SIZEOF_SECTOR_LIST_ENTRY;
        SectorIndex = _J2P_Read(pInst, JournalIndex);
        IsSectorFree = (U8)_IsSectorFree(pInst, JournalIndex);
        FS_StoreU32LE(pData + (Off & BPSMask) + OFF_ENTRY_SECTOR_INDEX, SectorIndex);
        *(pData + (Off & BPSMask) + OFF_ENTRY_SECTOR_NOT_USED) = IsSectorFree;
#if FS_JOURNAL_OPTIMIZE_SPACE_USAGE
        {
          U32 NumSectors;

          NumSectors = _SC_Read(pInst, JournalIndex);
          FS_StoreU32LE(pData + (Off & BPSMask) + OFF_ENTRY_NUM_SECTORS, NumSectors);
        }
#endif // FS_JOURNAL_OPTIMIZE_SPACE_USAGE
        //
        // Write sector if it is either last entry of copy list or in last entry of this sector.
        //
        if ((JournalIndex == (SectorCnt - 1u)) || (((Off & BPSMask) + SIZEOF_SECTOR_LIST_ENTRY) == BytesPerSector)) {
          SectorIndex = Off / BytesPerSector + pInst->Status.PBIStartSectorList;
          if (SectorIndex == pInst->Status.PBIFirstDataSector) {
            FS_DEBUG_ERROROUT((FS_MTYPE_JOURNAL, "JOURNAL: Fatal error: Writing management information into the data area."));
            r = 1;          // Error, management data overwritten.
            goto CleanUp;
          }
          r = _WriteOneDeviceSector(pInst, SectorIndex, pData);
          if (r != 0) {
            goto CleanUp;   // Error, could not write sector list.
          }
          FS_MEMSET(pData, 0xFF, BytesPerSector);
        }
      }
      //
      // OK, copy-list created. Store to status sector the number of sectors to be copied.
      //
      FS_MEMSET(pData, 0xFF, BytesPerSector);
      FS_MEMCPY(pData, MAN_SECTOR_TAG, SIZEOF_MAN_SECTOR_TAG);          //lint !e840 Use of nul character in a string literal N:100. Reason: This is the most efficient way to fill the unused characters with 0.
      FS_StoreU32LE(pData + OFF_MAN_SECTOR_CNT, SectorCnt);
      r = _WriteOneDeviceSector(pInst, pInst->Status.PBIStatusSector, pData);
      if (r != 0) {
        goto CleanUp;       // Error, could not write status sector
      }
    }

    //
    // Fail-safe test point
    //
    // If a reset occurs at this point _Mount() should replay the data stored to journal.
    //
    CALL_TEST_HOOK(pInst->pVolume->Partition.Device.Data.JournalData.Unit);

    //
    // Copy data from journal to its real destination.
    //
    r = _CopyData(pInst, pData);
    //
    // Informs the storage driver which sectors no longer contain valid data.
    //
    FREE_DATA(pInst);
    if (SectorCnt > 1u) {
#if FS_JOURNAL_ENABLE_STATS
      pInst->Status.SectorCntTotal += pInst->Status.SectorCnt;
      if (pInst->Status.SectorCntTotal > pInst->Status.StatCounters.MaxWriteSectorCnt) {
        pInst->Status.StatCounters.MaxWriteSectorCnt = pInst->Status.SectorCntTotal;
      }
#endif // FS_JOURNAL_ENABLE_STATS
      //
      // Prevent another error by marking the data as copied even when the copy operation fails.
      //
      rClear = _ResetJournal(pInst, pData);
      if (rClear != 0) {
        r = 1;              // Error, could not clear journal.
      }
    } else {
      _ClearSectorList(pInst);
    }
    //
    // Cleanup.
    //
CleanUp:
    FS__SB_Delete(&sb);
    if (r == 0) {
      if (FS__SB_GetError(&sb) != 0) {
        r = 1;            // Error, could not write or read sector data.
      }
    }
    IF_STATS(pInst->Status.StatCounters.NumTransactions++);
  }
  return r;
}

/*********************************************************************
*
*       _UpdateSector
*
*  Function description
*    Updates the data of a sector stored in the journal.
*
*  Parameters
*    pInst          Journal instance.
*    JournalIndex   Index in the journal of the sector to be updated.
*    pData          [IN] Sector data.
*
*  Return value
*    ==0      OK, sector successfully updated.
*    !=0      An error occurred.
*/
static int _UpdateSector(JOURNAL_INST * pInst, U32 JournalIndex, const U8 * pData) {
  int r;
  U32 SectorIndex;

  SectorIndex = JournalIndex + pInst->Status.PBIFirstDataSector;
  //
  // Write the sector data to journal.
  //
  r = _WriteOneDeviceSector(pInst, SectorIndex, pData);
  if (r != 0) {
    pInst->Status.SectorCnt = 0;                // Cancel the current transaction.
    return 1;                                   // Error, could not write.
  }
  MARK_SECTOR_AS_USED(pInst, JournalIndex);
#if FS_JOURNAL_OPTIMIZE_SPACE_USAGE
  _SC_Write(pInst, JournalIndex, 1);            // We always update one sector at a time.
#endif // FS_JOURNAL_OPTIMIZE_SPACE_USAGE
  return 0;                                     // OK, sector data stored successfully.
}

/*********************************************************************
*
*       _AddSector
*
*  Function description
*    Adds a new sector to the mapping table.
*
*  Parameters
*    pInst          Journal instance.
*    SectorIndex    Index of the sector be added.
*    pData          [IN] Sector data.
*
*  Return value
*    ==0      OK, index in journal of the added sector.
*    !=0      An error occurred.
*/
static int _AddSector(JOURNAL_INST * pInst, U32 SectorIndex, const U8 * pData) {
  int r;
  U32 SectorCnt;
  U32 JournalIndex;

  SectorCnt = pInst->Status.SectorCnt;
  if (SectorCnt == pInst->Status.NumSectorsData) {
    r = _OnOverflow(pInst);
    if (r == 0) {
      r = _CleanJournal(pInst);
    }
    if (r != 0) {
      pInst->Status.SectorCnt = 0;            // Cancel the current transaction.
      return 1;                               // Error, Could not clean journal.
    }
    SectorCnt = 0;
  }
  JournalIndex = SectorCnt;
  _J2P_Write(pInst, JournalIndex, SectorIndex);
  ++SectorCnt;
  pInst->Status.SectorCnt = SectorCnt;
  //
  // Write the sector data to journal.
  //
  r = _UpdateSector(pInst, JournalIndex, pData);
  return r;
}

#if FS_JOURNAL_SUPPORT_FREE_SECTOR

/*********************************************************************
*
*       _AddFreeSectors
*
*  Function description
*    Adds a range of invalid sectors to the mapping table.
*
*  Parameters
*    pInst        Journal instance.
*    SectorIndex  Index of the first sector to invalidate.
*    NumSectors   Number of sectors to invalidate.
*
*  Return value
*    ==0    OK, sectors marked as free.
*    !=0    An error occurred.
*/
static int _AddFreeSectors(JOURNAL_INST * pInst, U32 SectorIndex, U32 NumSectors) {
  U32 JournalIndex;
  U32 SectorCnt;
  int r;

  SectorCnt = pInst->Status.SectorCnt;
  if (SectorCnt == pInst->Status.NumSectorsData) {
    r = _OnOverflow(pInst);
    if (r == 0) {
      r = _CleanJournal(pInst);
    }
    if (r != 0) {
      pInst->Status.SectorCnt = 0;                // Cancel the current transaction.
      return 1;                                   // Error, could not clean journal.
    }
    SectorCnt = 0;
  }
  JournalIndex = SectorCnt;
  _J2P_Write(pInst, JournalIndex, SectorIndex);
#if FS_JOURNAL_OPTIMIZE_SPACE_USAGE
  _SC_Write(pInst, JournalIndex, NumSectors);
#else
  FS_USE_PARA(NumSectors);
#endif // FS_JOURNAL_OPTIMIZE_SPACE_USAGE
  ++SectorCnt;
  pInst->Status.SectorCnt = SectorCnt;
  //
  // Remember that the sector must be marked as free.
  //
  MARK_SECTOR_AS_FREE(pInst, JournalIndex);
  return 0;                                       // OK, free sectors added successfully.
}

#endif // FS_JOURNAL_SUPPORT_FREE_SECTOR

#if (FS_JOURNAL_OPTIMIZE_SPACE_USAGE == 0) || (FS_JOURNAL_SUPPORT_FREE_SECTOR == 0)

/*********************************************************************
*
*       _WriteOneSector
*
*  Function description
*    Writes one logical sector to journal.
*
*  Parameters
*    pInst          Journal instance.
*    SectorIndex    Index of the first logical sector to be written.
*    pData          Sector data to be written.
*
*  Return value
*    ==0    OK, sector successfully written.
*    !=0    An error occurred.
*/
static int _WriteOneSector(JOURNAL_INST * pInst, U32 SectorIndex, const U8 * pData) {
  int r;
  U32 JournalIndex;

  //
  // Try to locate the sector in the journal
  //
  JournalIndex = _FindSector(pInst, SectorIndex);
  if (JournalIndex != JOURNAL_INDEX_INVALID) {
    r = _UpdateSector(pInst, JournalIndex, pData);
  } else {
    r = _AddSector(pInst, SectorIndex, pData);
  }
  return r;
}

#else

/*********************************************************************
*
*       _WriteOneSectorEx
*
*  Function description
*    Writes one logical sector to journal.
*
*  Parameters
*    pInst          Journal instance.
*    SectorIndex    Index of the first logical sector to be written.
*    pData          Sector data to be written.
*
*  Return value
*    ==0    OK, sector successfully written.
*    !=0    An error occurred.
*/
static int _WriteOneSectorEx(JOURNAL_INST * pInst, U32 SectorIndex, const U8 * pData) {
  int r;
  U32 JournalIndex;
  U32 SectorIndexRange;
  U32 NumSectorsRange;
  U32 NumSectorsSplit;
  U32 SectorIndexSplit;

  JournalIndex = _FindSector(pInst, SectorIndex);
  if (JournalIndex == JOURNAL_INDEX_INVALID) {
    r = _AddSector(pInst, SectorIndex, pData);
    return r;
  }
  if (_IsSectorFree(pInst, JournalIndex) == 0) {
    //
    // Update the sector data if the sector is marked as in use.
    // Note that we know that there is only one sector in the list
    // because valid sectors are currently added one by one.
    //
    r = _UpdateSector(pInst, JournalIndex, pData);
    return r;
  }
  SectorIndexRange = _J2P_Read(pInst, JournalIndex);
  NumSectorsRange  = _SC_Read(pInst, JournalIndex);
  if (NumSectorsRange <= 1u) {
    //
    // The sector range contains only one sector.
    // In this case we store the sector index to
    // the journal entry and update the sector data.
    // Note that _UpdateSector() clears the free sector flag.
    //
    _J2P_Write(pInst, JournalIndex, SectorIndex);
    r = _UpdateSector(pInst, JournalIndex, pData);
    return r;
  }
  if (SectorIndex == SectorIndexRange) {
    //
    // Remove the sector from the range of free sectors
    // and add it separately to the list.
    //
    ++SectorIndexRange;
    _J2P_Write(pInst, JournalIndex, SectorIndexRange);
    --NumSectorsRange;
    _SC_Write(pInst, JournalIndex, NumSectorsRange);
    r = _AddSector(pInst, SectorIndex, pData);
    return r;
  }
  if (SectorIndex == (SectorIndexRange + NumSectorsRange - 1u)) {
    //
    // Remove the sector from the range of free sectors
    // and add it separately to the list.
    //
    --NumSectorsRange;
    _SC_Write(pInst, JournalIndex, NumSectorsRange);
    r = _AddSector(pInst, SectorIndex, pData);
    return r;
  }
  //
  // The sector index is located somewhere inside a free sector range.
  // Split the sector range in two and add the sector separately to the list.
  //
  NumSectorsSplit = SectorIndex - SectorIndexRange;
  _SC_Write(pInst, JournalIndex, NumSectorsSplit);
  SectorIndexSplit = SectorIndex + 1u;
  NumSectorsSplit = NumSectorsRange - (SectorIndex - SectorIndexRange + 1u);
  r = _AddFreeSectors(pInst, SectorIndexSplit, NumSectorsSplit);
  if (r == 0) {
    r = _AddSector(pInst, SectorIndex, pData);
  }
  return r;
}

#endif // FS_JOURNAL_OPTIMIZE_SPACE_USAGE

/*********************************************************************
*
*       _Write
*
*  Function description
*    Writes one or more logical sectors to journal.
*
*  Parameters
*    pInst          Journal instance.
*    SectorIndex    Index of the first logical sector to be written.
*    pData          Sector data to be written.
*    NumSectors     Number of logical sectors to be written.
*    RepeatSame     Set to 1 if the same data has to be written to all logical sectors.
*
*  Return value
*    ==0    OK, sector data successfully written.
*    !=0    An error occurred.
*/
static int _Write(JOURNAL_INST * pInst, U32 SectorIndex, const void * pData, U32 NumSectors, U8 RepeatSame) {
  int        r;
  unsigned   BytesPerSector;
  const U8 * pData8;

  if (pInst->Status.Error != 0) {
    return 1;                                             // Reject any write operation if an error has been reported during the transaction.
  }
  r              = 0;                                     // Set to indicate success.
  BytesPerSector = pInst->Status.BytesPerSector;
  pData8         = SEGGER_CONSTPTR2PTR(const U8, pData);  // MISRA deviation D:100[e]
  while (NumSectors != 0u) {
#if (FS_JOURNAL_OPTIMIZE_SPACE_USAGE == 0) || (FS_JOURNAL_SUPPORT_FREE_SECTOR == 0)
    r = _WriteOneSector(pInst, SectorIndex, pData8);
#else
    r = _WriteOneSectorEx(pInst, SectorIndex, pData8);
#endif // FS_JOURNAL_OPTIMIZE_SPACE_USAGE
    if (r != 0) {
      break;                                              // Error, could not update sector.
    }
    NumSectors--;
    SectorIndex++;
    if (RepeatSame == 0u) {
      pData8 += BytesPerSector;
    }
    IF_STATS(pInst->Status.StatCounters.WriteSectorCnt++);
  }
  return r;
}

#if FS_JOURNAL_SUPPORT_FREE_SECTOR

#if (FS_JOURNAL_OPTIMIZE_SPACE_USAGE == 0)

/*********************************************************************
*
*       _FreeOneSector
*
*  Function description
*    Marks one logical sector as containing invalid data.
*
*  Parameters
*    pInst        Journal instance.
*    SectorIndex  Index of the sector to be invalidated.
*
*  Return value
*    ==0    OK, sectors marked as free.
*    !=0    An error occurred.
*/
static int _FreeOneSector(JOURNAL_INST * pInst, U32 SectorIndex) {
  int r;
  U32 JournalIndex;

  //
  // Try to locate sector in journal.
  //
  JournalIndex = _FindSector(pInst, SectorIndex);
  if (JournalIndex == JOURNAL_INDEX_INVALID) {
    r = _AddFreeSectors(pInst, SectorIndex, 1);
  } else {
    //
    // Remember that the sector must be marked as free.
    //
    MARK_SECTOR_AS_FREE(pInst, JournalIndex);
    r = 0;
  }
  return r;
}

#else

/*********************************************************************
*
*       _FreeOneSectorEx
*
*  Function description
*    Marks one logical sector as containing invalid data.
*
*  Parameters
*    pInst        Journal instance.
*    SectorIndex  Index of the sector to be invalidated.
*
*  Return value
*    ==0    OK, sectors marked as free.
*    !=0    An error occurred.
*/
static int _FreeOneSectorEx(JOURNAL_INST * pInst, U32 SectorIndex) {
  int r;
  U32 JournalIndex;
  U32 SectorIndexRange;
  U32 NumSectorsRange;

  //
  // Try to locate sector in journal.
  //
  JournalIndex = _FindSectorEx(pInst, SectorIndex, 1);      // 1 means that the sector has to be freed.
  if (JournalIndex == JOURNAL_INDEX_INVALID) {
    //
    // Sector not found. Add it to the list.
    //
    r = _AddFreeSectors(pInst, SectorIndex, 1);
    return r;
  }
  if (_IsSectorFree(pInst, JournalIndex) == 0) {
    //
    // If the sector is in use then mark it as free.
    // Note that we know that there is only one sector
    // in the list because valid sectors are currently
    // added one by one.
    //
    MARK_SECTOR_AS_FREE(pInst, JournalIndex);
    return 0;
  }
  SectorIndexRange = _J2P_Read(pInst, JournalIndex);
  NumSectorsRange  = _SC_Read(pInst, JournalIndex);
  if (SectorIndex == (SectorIndexRange + NumSectorsRange)) {
    //
    // The sector index is one greater than the index of the last in the sector range.
    // Update the sector range by incrementing the number of sectors.
    //
    ++NumSectorsRange;
    _SC_Write(pInst, JournalIndex, NumSectorsRange);
    return 0;
  }
  if ((SectorIndexRange > 0u) && (SectorIndex == (SectorIndexRange - 1u))) {
    //
    // The sector index is one smaller than the index of the first sector in range.
    // Update the sector range by setting the new sector index and by incrementing
    // the number of sectors.
    //
    _J2P_Write(pInst, JournalIndex, SectorIndex);
    ++NumSectorsRange;
    _SC_Write(pInst, JournalIndex, NumSectorsRange);
  }
  return 0;
}

#endif // FS_JOURNAL_OPTIMIZE_SPACE_USAGE == 0

#endif // FS_JOURNAL_SUPPORT_FREE_SECTOR

/*********************************************************************
*
*       _FreeSectors
*
*  Function description
*    Marks one or more logical sectors as containing invalid data.
*
*  Parameters
*    pInst        Journal instance.
*    SectorIndex  Index of the first sector to invalidate.
*    NumSectors   Number of sectors to invalidate.
*
*  Return value
*    ==0    OK, sectors marked as free.
*    !=0    An error occurred.
*/
static int _FreeSectors(JOURNAL_INST * pInst, U32 SectorIndex, U32 NumSectors) {
  int r;

  r = 0;                                                // Set to indicate success.
#if FS_JOURNAL_SUPPORT_FREE_SECTOR
  if (pInst->Status.IsFreeSectorSupported != 0u) {
    if (pInst->Status.Error != 0) {
      r = 1;                                            // Reject any operation if an error was reported during the transaction.
    } else {
      while (NumSectors != 0u) {
#if (FS_JOURNAL_OPTIMIZE_SPACE_USAGE == 0)
        r = _FreeOneSector(pInst, SectorIndex);
#else
        r = _FreeOneSectorEx(pInst, SectorIndex);
#endif // FS_JOURNAL_OPTIMIZE_SPACE_USAGE
        if (r != 0) {
          break;                                          // Error, could not free sector.
        }
        NumSectors--;
        SectorIndex++;
        IF_STATS(pInst->Status.StatCounters.FreeSectorCnt++);
      }
    }
  }
#else
  FS_USE_PARA(pInst);
  FS_USE_PARA(SectorIndex);
  FS_USE_PARA(NumSectors);
#endif
  return r;
}

/*********************************************************************
*
*       _Read
*
*  Function description
*    Reads one or more logical sectors from journal.
*
*  Parameters
*    pInst          Journal instance.
*    SectorIndex    Index of the first logical sector to be read.
*    pData          Read sector data.
*    NumSectors     Number of logical sectors to be read.
*
*  Return value
*    ==0    OK, sector data successfully read.
*    !=0    An error occurred.
*/
static int _Read(JOURNAL_INST * pInst, U32 SectorIndex, void * pData, U32 NumSectors) {
  U32   SectorIndexJournal;
  U32   JournalIndex;
  int   r;
  U32   NumSectorsAtOnce;
  U8  * pDataStart;
  U32   StartSector;

  NumSectorsAtOnce = 0;
  StartSector      = 0;
  pDataStart       = NULL;
  while (NumSectors != 0u) {
    //
    // Try to locate sector in journal.
    //
    JournalIndex = _FindSector(pInst, SectorIndex);
    if (JournalIndex == JOURNAL_INDEX_INVALID) {
      //
      // Sector not in the journal. Remember it and read it later.
      //
      if (NumSectorsAtOnce == 0u) {
        StartSector = SectorIndex;
        pDataStart  = SEGGER_PTR2PTR(U8, pData);                                                // MISRA deviation D:100[e]
      }
      ++NumSectorsAtOnce;
    } else {
      if (NumSectorsAtOnce != 0u) {
        r = _ReadDeviceSectors(pInst, StartSector, pDataStart, NumSectorsAtOnce);
        if (r != 0) {
          pInst->Status.SectorCnt = 0;                                                          // Cancel the current transaction.
          return 1;                                                                             // Error, could not read sectors.
        }
        NumSectorsAtOnce = 0;
      }
      SectorIndexJournal = JournalIndex + pInst->Status.PBIFirstDataSector;
      //
      // Read one sector from journal.
      //
      r = _ReadOneDeviceSector(pInst, SectorIndexJournal, SEGGER_PTR2PTR(U8, pData));           // MISRA deviation D:100[e]
      if (r != 0) {
        pInst->Status.SectorCnt = 0;                                                            // Cancel the current transaction.
        return 1;                                                                               // Error, could not read sector.
      }
    }
    NumSectors--;
    SectorIndex++;
    pData = SEGGER_PTR2PTR(void, (pInst->Status.BytesPerSector + SEGGER_PTR2PTR(U8, pData)));   // MISRA deviation D:100[e] pData += BytesPerSector
  }
  if (NumSectorsAtOnce != 0u) {
    r = _ReadDeviceSectors(pInst, StartSector, pDataStart, NumSectorsAtOnce);
    if (r != 0) {
      pInst->Status.SectorCnt = 0;                  // Cancel the current transaction.
      return 1;                                     // Error, could not read sectors.
    }
  }
  return 0;
}

/*********************************************************************
*
*       _Mount
*
*  Function description
*    Initializes the journal instance with the information read from journal file.
*
*  Parameters
*    pVolume          Volume instance on which the journal is located.
*    LastSectorInFS   Index of the journal info sector.
*
*  Return value
*    ==0    OK, journal successfully mounted.
*    !=0    An error occurred.
*
*  Additional information
*    This function copies the data from journal file to original
*    destination if it detects that the operation was interrupted
*    by an unexpected reset.
*/
static int _Mount(FS_VOLUME * pVolume, U32 LastSectorInFS) {
  FS_SB          sb;
  int            r;
  int            rClear;
  U32            FirstSector;
  U32            NumSectors;
  U32            SectorCnt;
  U32            JournalIndex;
  U32            Off;
  U32            BytesPerSector;
  U32            SectorIndex;
  U32            StartSector;
  U8             IsFreeSectorSupported;
  U8             IsSectorFree;
  U8           * pData;
  JOURNAL_INST * pInst;

  pInst = _Volume2Inst(pVolume);
  if (pInst == NULL) {
    return 1;                                                                     // Error, instance not found.
  }
  (void)FS__SB_Create(&sb, pVolume);
  pData = FS__SB_GetBuffer(&sb);
  //
  // Compute the physical sector index of the last sector.
  //
  StartSector     = pVolume->Partition.StartSector;
  LastSectorInFS += StartSector;
  //
  // Read info sector (last sector of the partition)
  //
  r = _ReadOneDeviceSector(pInst, LastSectorInFS, pData);
  if (r != 0) {
    FS_DEBUG_WARN((FS_MTYPE_JOURNAL, "JOURNAL: _Mount: Could not read of info sector."));
    goto CleanUp;
  }
  //
  // Check sector for validity
  //
  if (FS_MEMCMP(pData, INFO_SECTOR_TAG, SIZEOF_INFO_SECTOR_TAG) != 0) {           //lint !e840 Use of nul character in a string literal N:100. Reason: This is the most efficient way to fill the unused characters with 0.
    FS_DEBUG_WARN((FS_MTYPE_JOURNAL, "JOURNAL: _Mount: Invalid info sector."));
    goto CleanUp;
  }
  if (FS_LoadU32LE(pData + OFF_INFO_VERSION) != VERSION) {
    FS_DEBUG_WARN((FS_MTYPE_JOURNAL, "JOURNAL: _Mount: File version does not match."));
    goto CleanUp;
  }
  //
  // Retrieve static information from info sector. This info is written when the journal is created and never changes.
  //
  NumSectors  = FS_LoadU32LE(pData + OFF_INFO_NUM_TOTAL_SECTORS);
  FirstSector = (LastSectorInFS - NumSectors) + 1u;
  //
  // Check if the "free sector" feature is supported.
  //
  IsFreeSectorSupported = 0;
  if (pData[OFF_INFO_SUPPORT_FREE_SECTOR] == 0u) {                                // Reversed logic: ==0 -> supported, !=0 -> not supported
    IsFreeSectorSupported = 1;
  }
  r = _InitInst(pInst, FirstSector, NumSectors, IsFreeSectorSupported);
  if (r != 0) {
    FS_DEBUG_WARN((FS_MTYPE_JOURNAL, "JOURNAL: _Mount: Could not initialize instance."));
    goto CleanUp;
  }
  BytesPerSector = pInst->Status.BytesPerSector;
  //
  // Read status sector, check for validity.
  //
  r = _ReadOneDeviceSector(pInst, pInst->Status.PBIStatusSector, pData);
  if (r != 0) {
    FS_DEBUG_WARN((FS_MTYPE_JOURNAL, "JOURNAL: _Mount: Could not read status sector."));
    goto CleanUp;
  }
  if (FS_MEMCMP(pData, MAN_SECTOR_TAG, SIZEOF_MAN_SECTOR_TAG) != 0) {             //lint !e840 Use of nul character in a string literal N:100. Reason: This is the most efficient way to fill the unused characters with 0.
    FS_DEBUG_WARN((FS_MTYPE_JOURNAL, "JOURNAL: _Mount: Invalid status sector."));
    goto CleanUp;
  }
  r = 0;  // No error so far.
  //
  // Check if any entries are in the journal.
  //
  SectorCnt = FS_LoadU32LE(pData + OFF_MAN_SECTOR_CNT);
  if (SectorCnt != 0u) {
    //
    // Load the list of sectors stored in the journal file.
    //
    for (JournalIndex = 0; JournalIndex < SectorCnt; JournalIndex++) {
      Off = JournalIndex * SIZEOF_SECTOR_LIST_ENTRY;
      if ((Off & (BytesPerSector - 1u)) == 0u) {
        SectorIndex = Off / BytesPerSector + pInst->Status.PBIStartSectorList;
        r = _ReadOneDeviceSector(pInst, SectorIndex, pData);
        if (r != 0) {
          FS_DEBUG_WARN((FS_MTYPE_JOURNAL, "JOURNAL: _Mount: Could not read sector list."));
          goto CleanUp;
        }
      }
      Off         &= BytesPerSector - 1u;
      SectorIndex  = FS_LoadU32LE(pData + Off + OFF_ENTRY_SECTOR_INDEX);
      _J2P_Write(pInst, JournalIndex, SectorIndex);
      IsSectorFree = *(pData + Off + OFF_ENTRY_SECTOR_NOT_USED);
      if (IsSectorFree != 0u) {
        MARK_SECTOR_AS_FREE(pInst, JournalIndex);
      }
#if FS_JOURNAL_OPTIMIZE_SPACE_USAGE
      NumSectors = FS_LoadU32LE(pData + Off + OFF_ENTRY_NUM_SECTORS);
      if ((NumSectors == 0u) || (NumSectors == SECTOR_INDEX_INVALID)) {
        NumSectors = 1;                                                           // Each entry stores at least one sector.
      }
      _SC_Write(pInst, JournalIndex, NumSectors);
#endif // FS_JOURNAL_OPTIMIZE_SPACE_USAGE
    }
    pInst->Status.SectorCnt = SectorCnt;
    //
    // Copy data from journal to its real destination.
    //
    r = _CopyData(pInst, pData);
    //
    // Inform the storage device which sectors no longer contain valid data.
    //
    FREE_DATA(pInst);
#if FS_JOURNAL_ENABLE_STATS
    //
    // Update statistical counters.
    //
    pInst->Status.SectorCntTotal += pInst->Status.SectorCnt;
    if (pInst->Status.SectorCntTotal > pInst->Status.StatCounters.MaxWriteSectorCnt) {
      pInst->Status.StatCounters.MaxWriteSectorCnt = pInst->Status.SectorCntTotal;
    }
#endif // FS_JOURNAL_ENABLE_STATS
    //
    // Mark the data as copied even when the copy operation failed to prevent yet another error.
    //
    rClear = _ResetJournal(pInst, pData);
    if (rClear != 0) {
      r = 1;
    }
    IF_STATS(pInst->Status.StatCounters.NumTransactions++);
  }
  if (r == 0) {
    pInst->Status.IsPresent = 1;                                                  // OK, journal mounted successfully.
  }
CleanUp:
  //
  // Cleanup
  //
  FS__SB_Delete(&sb);
  if (r == 0) {
    if (FS__SB_GetError(&sb) != 0) {
      r = 1;
    }
  }
  return r;
}

/*********************************************************************
*
*       _CreateJournal
*
*  Function description
*    Creates the journal
*
*  Return value
*    ==0    O.K.
*    !=0    Error code indicating the failure reason
*/
static int _CreateJournal(FS_VOLUME * pVolume, U32 FirstSector, U32 NumSectors, U8 IsFreeSectorSupported) {
  U32            BytesPerSector;
  U32            StartSector;
  int            r;
  FS_SB          sb;
  U8           * pData;
  JOURNAL_INST * pInst;

  if (NumSectors < NUM_SECTORS_MIN) {
    FS_DEBUG_ERROROUT((FS_MTYPE_JOURNAL, "JOURNAL: The number of configured sectors is too small. A minimum of %d sectors is required.", NUM_SECTORS_MIN));
    return FS_ERRCODE_INVALID_PARA;                                             // Error, invalid number of sectors.
  }
  pInst = _Volume2Inst(pVolume);
  if (pInst == NULL) {
    return FS_ERRCODE_INVALID_PARA;                                             // Error, cannot get instance.
  }
  StartSector  = pVolume->Partition.StartSector;
  FirstSector += StartSector;
#if (FS_JOURNAL_SUPPORT_FREE_SECTOR == 0)
  IsFreeSectorSupported = 0;
#endif
  r = _InitInst(pInst, FirstSector, NumSectors, IsFreeSectorSupported);
  if (r != 0) {
    return r;                                                                   // Error, could not initialize instance.
  }
  BytesPerSector = pInst->Status.BytesPerSector;
  //
  // Prepare and write info sector.
  //
  (void)FS__SB_Create(&sb, pVolume);
  pData = FS__SB_GetBuffer(&sb);
  FS_MEMSET(pData, 0xFF, BytesPerSector);
  FS_MEMCPY(pData, INFO_SECTOR_TAG, SIZEOF_INFO_SECTOR_TAG);                    //lint !e840 Use of nul character in a string literal N:100. Reason: This is the most efficient way to fill the unused characters with 0.
  FS_StoreU32LE(pData + OFF_INFO_VERSION,           VERSION);
  FS_StoreU32LE(pData + OFF_INFO_NUM_TOTAL_SECTORS, NumSectors);
#if FS_JOURNAL_SUPPORT_FREE_SECTOR
  pData[OFF_INFO_SUPPORT_FREE_SECTOR] = IsFreeSectorSupported == 0u ? 1u : 0u;  // Reversed logic: ==0 -> enabled, !=0 -> disabled since all the unused bytes are filled with the value 1.
#endif
  r = _WriteOneDeviceSector(pInst, pInst->Status.PBIInfoSector, pData);
  if (r == 0) {
    //
    // Remove all the data from journal.
    //
    r = _ResetJournal(pInst, pData);
    if (r == 0) {
      pInst->Status.IsPresent = 1;
    }
  }
  if (r != 0) {
    r = FS_ERRCODE_WRITE_FAILURE;
  }
  //
  // Cleanup
  //
  FS__SB_Delete(&sb);
  if (r == 0) {
    r = FS__SB_GetError(&sb);
  }
  return r;
}

/*********************************************************************
*
*       _CreateJournalIfRequired
*
*  Function description
*    Creates the journal if it does not exists already.
*
*  Return value
*    ==0    O.K.
*    !=0    Error code indicating the failure reason
*/
static int _CreateJournalIfRequired(FS_VOLUME * pVolume, U32 NumBytes, U8 IsFreeSectorSupported) {
  int         r;
  U32         FirstSector;
  U32         NumSectors;
  FS_DEVICE * pDevice;

  r = FS__AutoMount(pVolume);
  switch ((unsigned)r) {
  case FS_MOUNT_RW:
    pDevice = &pVolume->Partition.Device;
    r = FS__JOURNAL_IsPresent(pDevice);
    if (r == 0) {
      //
      // Create the journal file if not present.
      //
      FirstSector = 0;
      NumSectors  = 0;
      FS_LOCK_DRIVER(pDevice);
      r = FS_CREATE_JOURNAL_FILE(pVolume, NumBytes, &FirstSector, &NumSectors);
      if (r == 0) {
        r = _CreateJournal(pVolume, FirstSector, NumSectors, IsFreeSectorSupported);
      }
      FS_UNLOCK_DRIVER(pDevice);
    }
    break;
  case FS_MOUNT_RO:
    r = FS_ERRCODE_READ_ONLY_VOLUME;
    break;
  case 0:
    r = FS_ERRCODE_VOLUME_NOT_MOUNTED;
    break;
  default:
    //
    // An error occurred while mounting the volume.
    //
    break;
  }
  return r;
}

/*********************************************************************
*
*       Public code (internal)
*
**********************************************************************
*/

/*********************************************************************
*
*       FS__JOURNAL_Mount
*
*  Function description
*    Mounts the journal layer, replaying the journal.
*
*  Return value
*    ==0    O.K.
*    !=0    Error code indicating the failure reason
*/
int FS__JOURNAL_Mount(FS_VOLUME * pVolume) {
  int r;
  U32 LastSector;

  r = 0;              // Set to indicate success.
  if (pVolume->MountType == FS_MOUNT_RW) {
    r = FS_OPEN_JOURNAL_FILE(pVolume);
    if (r == 0) {
      LastSector = FS_GET_INDEX_OF_LAST_SECTOR(pVolume);
      r = _Mount(pVolume, LastSector);
    } else {
      //
      // Return success so that the application can mount the file system
      // with the journaling disabled. If the journal file is not present
      // the call to FS_JOURNAL_Create() or FS_JOURNAL_CreateEx() will
      // create it again.
      //
      r = 0;
    }
  }
  return r;
}

/*********************************************************************
*
*       FS__JOURNAL_Begin
*
*  Function description
*    Opens a journal transaction.
*
*  Parameters
*    pVolume      Instance of the volume on which the journal file is located.
*
*  Return value
*    ==0  OK, journal transaction opened.
*    !=0  An error occurred.
*
*  Additional information
*    After the call to this function all the data modified by the file
*    system is stored to the journal file instead of the original destination.
*/
int FS__JOURNAL_Begin(FS_VOLUME * pVolume) {
  JOURNAL_INST    * pInst;
  unsigned          OpenCnt;
  FS_JOURNAL_DATA * pJournalData;

  pInst = _Volume2Inst(pVolume);
  if (pInst == NULL) {
    return FS_ERRCODE_INVALID_PARA;                   // Error, cannot get instance.
  }
  pJournalData = &pVolume->Partition.Device.Data.JournalData;
  OpenCnt = pInst->Status.OpenCnt;
  if (OpenCnt == OPEN_CNT_MAX) {
    FS_DEBUG_WARN((FS_MTYPE_JOURNAL, "JOURNAL: Could not open. Too many opened transactions."));
    return FS_ERRCODE_TOO_MANY_TRANSACTIONS_OPEN;     // Error, journal has been opened to many times.
  }
  if (OpenCnt == 0u) {
    pJournalData->MaxClusterId        = 0;
    pJournalData->MinClusterId        = 0xFFFFFFFFuL;
    pJournalData->IsTransactionNested = 0;
    pInst->Status.Error               = 0;
    IF_STATS(pInst->Status.SectorCntTotal = 0);
  }
  ++OpenCnt;
  if (OpenCnt > 1u) {
    pJournalData->IsTransactionNested = 1;
  }
  pInst->Status.OpenCnt = (U16)OpenCnt;
  return 0;           // OK, journal opened.
}

/*********************************************************************
*
*       FS__JOURNAL_End
*
*  Function description
*    Closes the journal.
*    This means all relevant data is written to the journal,
*    instead of the "real destination".
*
*  Return value
*    ==0    OK, journal closed
*    !=0    An error occurred
*/
int FS__JOURNAL_End(FS_VOLUME * pVolume) {
  JOURNAL_INST    * pInst;
  int               r;
  unsigned          OpenCnt;
  FS_JOURNAL_DATA * pJournalData;

  pInst = _Volume2Inst(pVolume);
  if (pInst == NULL) {
    return FS_ERRCODE_INVALID_PARA;                   // Error, cannot get instance.
  }
  pJournalData = &pVolume->Partition.Device.Data.JournalData;
  OpenCnt = pInst->Status.OpenCnt;
  if (OpenCnt == 0u) {
    FS_DEBUG_WARN((FS_MTYPE_JOURNAL, "JOURNAL: Could not close. No open transaction."));
    return FS_ERRCODE_NO_OPEN_TRANSACTION;    // Error, journal not opened.
  }
  r = pInst->Status.Error;
  --OpenCnt;
  //
  // Close the transaction on the last nested call.
  //
  if (OpenCnt == 0u) {
    if (pInst->Status.IsPresent != 0u) {
      if (r == 0) {
        //
        // Replay the journal.
        //
        r = _CleanJournal(pInst);
        if (r != 0) {
          r = FS_ERRCODE_WRITE_FAILURE;
        }
      }
      if (r != 0) {
        //
        // Cancel the current transaction in case of an error.
        //
        pInst->Status.SectorCnt = 0;
      }
    }
    pInst->Status.Error = 0;
    //
    // Data appended at the end of a file (i.e. new data) can be written directly to "real" destination.
    //
    pJournalData->IsNewDataLogged = 0;
  }
  if (OpenCnt <= 1u) {
    pJournalData->IsTransactionNested = 0;
  }
  pInst->Status.OpenCnt = (U16)OpenCnt;
  return r;
}

/*********************************************************************
*
*       FS__JOURNAL_Clean
*
*  Function description
*    Closes the journal.
*    This means all relevant data is written to the journal,
*    instead of the "real destination".
*
*  Return value
*    ==0    O.K.
*    !=0    An error occurred
*/
int FS__JOURNAL_Clean(FS_VOLUME * pVolume) {
  JOURNAL_INST * pInst;
  int            r;

  r = 1;                  // Set to indicate an error.
  pInst = _Volume2Inst(pVolume);
  if (pInst != NULL) {
    r = 0;                // Set to indicate OK.
    pInst->Status.OpenCnt = 0;
    if (pInst->Status.IsPresent != 0u) {
      //
      // Copy data from journal to actual position on the storage medium.
      //
      r = _CleanJournal(pInst);
      //
      // Data appended at the end of a file (i.e. new data) can be written directly to "real" destination.
      //
      pVolume->Partition.Device.Data.JournalData.IsNewDataLogged = 0;
    }
  }
  return r;
}

/*********************************************************************
*
*       FS__JOURNAL_Invalidate
*
*  Function description
*    Invalidates the journal.
*    Typically called when formatting a medium to avoid replaying of the journal
*/
void FS__JOURNAL_Invalidate(FS_VOLUME * pVolume) {
  JOURNAL_INST * pInst;

  pInst = _Volume2Inst(pVolume);
  if (pInst != NULL) {
    //
    // Invalidate all status information in the instance structure.
    //
    _InitStatus(pInst);
  }
}

/*********************************************************************
*
*       FS__JOURNAL_IsPresent
*
*  Function description
*    Returns if a journal is present and active.
*/
int FS__JOURNAL_IsPresent(const FS_DEVICE * pDevice) {
  JOURNAL_INST          * pInst;
  U8                      Unit;
  int                     IsPresent;
  const FS_JOURNAL_DATA * pJournalData;

  IsPresent = 0;
  pJournalData = &pDevice->Data.JournalData;
  if (pJournalData->IsInited != 0u) {
    Unit = pJournalData->Unit;
    if (Unit < SEGGER_COUNTOF(_apInst)) {
      pInst = _apInst[Unit];
      if (pInst != NULL) {
        IsPresent = (int)pInst->Status.IsPresent;
      }
    }
  }
  return IsPresent;
}

/*********************************************************************
*
*       FS__JOURNAL_GetNumFreeSectors
*
*  Function description
*    Returns the number of sectors which can be written to journal.
*/
int FS__JOURNAL_GetNumFreeSectors(FS_VOLUME * pVolume) {
  JOURNAL_INST * pInst;
  int            r;

  r = 0;
  pInst = _Volume2Inst(pVolume);
  if (pInst != NULL) {
    r = (int)pInst->Status.NumSectorsData - (int)pInst->Status.SectorCnt;
  }
  return r;
}

/*********************************************************************
*
*       FS__JOURNAL_Read
*
*  Function description
*    Reads one or multiple sectors from journal
*/
int FS__JOURNAL_Read(const FS_DEVICE * pDevice, U32 SectorIndex, void * pBuffer, U32 NumSectors) {
  JOURNAL_INST * pInst;
  U8             Unit;
  int            r;

  Unit  = pDevice->Data.JournalData.Unit;
  pInst = _apInst[Unit];
  r = _Read(pInst, SectorIndex, pBuffer, NumSectors);
  return r;
}

/*********************************************************************
*
*       FS__JOURNAL_Write
*
*  Function description
*    Write one or multiple sectors to journal
*/
int FS__JOURNAL_Write(const FS_DEVICE * pDevice, U32 SectorIndex, const void * pBuffer, U32 NumSectors, U8 RepeatSame) {
  JOURNAL_INST * pInst;
  U8             Unit;
  int            r;

  Unit  = pDevice->Data.JournalData.Unit;
  pInst = _apInst[Unit];
  r = _Write(pInst, SectorIndex, pBuffer, NumSectors, RepeatSame);
  return r;
}

/*********************************************************************
*
*       FS__JOURNAL_FreeSectors
*
*  Function description
*    Marks one or more sectors as not in used.
*/
int FS__JOURNAL_FreeSectors(const FS_DEVICE * pDevice, U32 SectorIndex, U32 NumSectors) {
  JOURNAL_INST * pInst;
  U8             Unit;
  int            r;

  Unit  = pDevice->Data.JournalData.Unit;
  pInst = _apInst[Unit];
  r = _FreeSectors(pInst, SectorIndex, NumSectors);
  return r;
}

/*********************************************************************
*
*       FS__JOURNAL_GetOpenCnt
*
*  Function description
*    Returns the number of opened journal transactions.
*/
int FS__JOURNAL_GetOpenCnt(FS_VOLUME * pVolume) {
  JOURNAL_INST * pInst;
  int            OpenCnt;

  OpenCnt = 0;
  pInst = _Volume2Inst(pVolume);
  if (pInst != NULL) {
    OpenCnt = (int)pInst->Status.OpenCnt;
  }
  return OpenCnt;
}

#if FS_SUPPORT_DEINIT

/*********************************************************************
*
*       FS__JOURNAL_DeInit
*
*  Function description
*    Deinitializes the Journal module.
*/
void FS__JOURNAL_DeInit(const FS_VOLUME * pVolume) {
  JOURNAL_INST * pInst;
  U8             Unit;
  FS_VOLUME    * pVolumeIter;

  pVolumeIter = &FS_Global.FirstVolume;
  for (Unit = 0; Unit < FS_Global.NumVolumes; Unit++) {
    if (pVolume == pVolumeIter) {
      break;
    }
    pVolumeIter = pVolumeIter->pNext;
  }
  if (Unit < (U8)FS_NUM_VOLUMES) {
    pInst = _apInst[Unit];
    if (pInst != NULL) {
      if (pInst->pJ2P != NULL) {
        FS_Free(pInst->pJ2P);
        pInst->pJ2P = NULL;
      }
#if FS_JOURNAL_SUPPORT_FREE_SECTOR
      if (pInst->paIsSectorFree != NULL) {
        FS_Free(pInst->paIsSectorFree);
        pInst->paIsSectorFree = NULL;
      }
#endif // FS_JOURNAL_SUPPORT_FREE_SECTOR
#if FS_JOURNAL_OPTIMIZE_SPACE_USAGE
      if (pInst->pSC != NULL) {
        FS_Free(pInst->pSC);
        pInst->pSC = NULL;
      }
#endif // FS_JOURNAL_OPTIMIZE_SPACE_USAGE
      FS_Free(pInst);
      _apInst[Unit] = NULL;
    }
  }
}

#endif // FS_SUPPORT_DEINIT

#if FS_SUPPORT_TEST

/*********************************************************************
*
*       FS__JOURNAL_SetTestHook
*
*  Function description
*    Registers a callback function for testing purposes.
*
*  Parameters
*    pfTestHook     Callback function to be registered.
*
*  Additional information
*    The registered callback function is called by the Journaling
*    component at critical points during the data update procedure.
*    The typical usage of the callback function is to perform
*    a target reset to check if the file system recovers correctly
*    from such events.
*/
void FS__JOURNAL_SetTestHook(FS_JOURNAL_TEST_HOOK * pfTestHook) {
  _pfTestHook = pfTestHook;
}

/*********************************************************************
*
*       FS__JOURNAL_GetLayout
*
*  Function description
*    Returns information about the layout of the data in the journal file.
*
*  Parameters
*    sVolumeName    Name of the volume on which the journal file is located.
*    pLayout        [OUT] Information about the journal file layout.
*
*  Return value
*    ==0      OK, information returned successfully.
*    !=0      Error code indicating the failure reason.
*/
int FS__JOURNAL_GetLayout(const char * sVolumeName, FS_JOURNAL_LAYOUT * pLayout) {
  FS_VOLUME    * pVolume;
  int            r;
  JOURNAL_INST * pInst;

  r = FS_ERRCODE_VOLUME_NOT_FOUND;    // Set to indicate error.
  FS_LOCK();
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    r = FS__AutoMount(pVolume);
    switch ((unsigned)r) {
    case FS_MOUNT_RW:
      pInst = _Volume2Inst(pVolume);
      if (pInst != NULL) {
        FS_LOCK_DRIVER(&pVolume->Partition.Device);
        pLayout->SectorIndexData   = pInst->Status.PBIFirstDataSector;
        pLayout->SectorIndexInfo   = pInst->Status.PBIInfoSector;
        pLayout->SectorIndexList   = pInst->Status.PBIStartSectorList;
        pLayout->SectorIndexStatus = pInst->Status.PBIStatusSector;
        FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
        r = 0;
      }
      break;
    case FS_MOUNT_RO:
      r = FS_ERRCODE_READ_ONLY_VOLUME;
      break;
    case 0:
      r = FS_ERRCODE_VOLUME_NOT_MOUNTED;
      break;
    default:
      //
      // An error occurred while mounting the volume.
      //
      break;
    }
  }
  FS_UNLOCK();
  return r;
}

#endif  // FS_SUPPORT_TEST

/*********************************************************************
*
*       FS__JOURNAL_Save
*/
void FS__JOURNAL_Save(FS_CONTEXT * pContext) {
  pContext->JOURNAL_pfOnOverflow   = _pfOnOverflow;
  pContext->JOURNAL_pfOnOverflowEx = _pfOnOverflowEx;
}

/*********************************************************************
*
*       FS__JOURNAL_Restore
*/
void FS__JOURNAL_Restore(const FS_CONTEXT * pContext) {
  _pfOnOverflow   = pContext->JOURNAL_pfOnOverflow;
  _pfOnOverflowEx = pContext->JOURNAL_pfOnOverflowEx;
}

/*********************************************************************
*
*       FS__JOURNAL_SetError
*/
void FS__JOURNAL_SetError(FS_VOLUME * pVolume, int Error) {
  JOURNAL_INST * pInst;

  pInst = _Volume2Inst(pVolume);
  if (pInst != NULL) {
    //
    // Do not set any error on nested transactions because we cannot
    // keep track of which sub-transactions actually failed.
    // By not doing this the file system reports that the entire
    // transaction failed even when some of the sub-transactions
    // were actually successful.
    //
    if (pInst->Status.OpenCnt == 1u) {
      if (pInst->Status.Error == 0) {
        pInst->Status.Error = (I16)Error;
      }
    }
  }
}

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_JOURNAL_Create
*
*  Function description
*    Creates the journal file.
*
*  Parameters
*    sVolumeName        Journal instance identified by volume name (0-terminated string).
*    NumBytes           Size of journal file in bytes.
*
*  Return value
*   ==1    OK, journal already exists.
*   ==0    OK, journal successfully created.
*   < 0    Error code indicating the failure reason.
*
*  Additional information
*    This function is mandatory. It has to be called after the file
*    system initialization to create the journal file. FS_JOURNAL_Create()
*    does nothing if the journal file already exists. The name of the
*    journal file can be configured at compile time via FS_JOURNAL_FILE_NAME
*    or at runtime via FS_JOURNAL_SetFileName().
*
*    The size of the journal file can be calculated by using the following formula:
*
*    JournalSize = 3 * BytesPerSector + (16 + BytesPerSector) * NumSectors
*
*    +----------------+-------------------------------------------------------------------------------------------------------------------------------------+
*    | Parameter      | Description                                                                                                                         |
*    +----------------+-------------------------------------------------------------------------------------------------------------------------------------+
*    | JournalSize    | Size of the journal file in bytes. This value has to be passed as second parameter to FS_JOURNAL_Create() or FS_JOURNAL_CreateEx(). |
*    +----------------+-------------------------------------------------------------------------------------------------------------------------------------+
*    | BytesPerSector | Size of the file system logical sector in bytes.                                                                                    |
*    +----------------+-------------------------------------------------------------------------------------------------------------------------------------+
*    | NumSectors     | Number of logical sectors the journal has to be able to store.                                                                      |
*    +----------------+-------------------------------------------------------------------------------------------------------------------------------------+
*
*    The number of sectors the journal file is able to store on a transaction depends
*    on the file system operations performed by the application. The table  below can
*    be used to calculate the approximate number of sectors that are stored during
*    a specific file system operation.
*
*    +----------------------------+-------------------------------------------------------------------+
*    | API function               | Number of logical sectors                                         |
*    +----------------------------+-------------------------------------------------------------------+
*    | FS_CreateDir()             | The number of sectors modified by FS_MkDir() times the number of directories that have to be created. |
*    +----------------------------+-------------------------------------------------------------------+
*    | FS_DeleteDir()             | The number of sectors modified by FS_RmDir() times the number of directories that have to be deleted plus the number of sectors modified by FS_Remove() times the number of files that have to be deleted. |
*    +----------------------------+-------------------------------------------------------------------+
*    | FS_FClose()                | One sector if the file has been modified else no sectors.         |
*    +----------------------------+-------------------------------------------------------------------+
*    | FS_FOpen()                 | One sector when creating the file else no sectors. If the file exists and is truncated to 0 then total number of sectors in the allocation table that have to be modified. |
*    +----------------------------+-------------------------------------------------------------------+
*    | FS_FWrite()                | The same number of sectors as FS_Write()                          |
*    +----------------------------+-------------------------------------------------------------------+
*    | FS_MkDir()                 | Two sectors plus the number of sectors in cluster.                |
*    +----------------------------+-------------------------------------------------------------------+
*    | FS_ModifyFileAttributes()  | One sector.                                                       |
*    +----------------------------+-------------------------------------------------------------------+
*    | FS_Move()                  | Two sectors if the destination and source files or directories are located on the same volume else the number of sectors modified by FS_CopyFile().                                                         |
*    +----------------------------+-------------------------------------------------------------------+
*    | FS_Remove()                | One sector plus total number of sectors in the allocation table that have to be modified. |
*    +----------------------------+-------------------------------------------------------------------+
*    | FS_Rename()                | One sector.                                                       |
*    +----------------------------+-------------------------------------------------------------------+
*    | FS_RmDir()                 | Two sectors.                                                      |
*    +----------------------------+-------------------------------------------------------------------+
*    | FS_SetEndOfFile()          | One sector plus the total number of sectors in the allocation table that have to be modified. |
*    +----------------------------+-------------------------------------------------------------------+
*    | FS_SetFileAttributes()     | One sector.                                                       |
*    +----------------------------+-------------------------------------------------------------------+
*    | FS_SetFileSize()           | The same number of sectors as FS_SetEndOfFile()                   |
*    +----------------------------+-------------------------------------------------------------------+
*    | FS_SetFileTime()           | One sector.                                                       |
*    +----------------------------+-------------------------------------------------------------------+
*    | FS_SetFileTimeEx()         | One sector.                                                       |
*    +----------------------------+-------------------------------------------------------------------+
*    | FS_SetVolumeLabel()        | One sector.                                                       |
*    +----------------------------+-------------------------------------------------------------------+
*    | FS_SyncFile()              | One sector if the file has been modified else no sectors.         |
*    +----------------------------+-------------------------------------------------------------------+
*    | FS_SetVolumeLabel()        | One sector.                                                       |
*    +----------------------------+-------------------------------------------------------------------+
*    | FS_Write()                 | Uses the remaining free space in the journal file at the start of the transaction. Two sectors and about 9 percent of the free space available in the journal file (rounded up to a multiple of sector size) are reserved for allocation table and directory entry updates. The remaining sectors are used to store the actual data. If more data is written than free space is available in the journal file, the operation is split into multiple journal transactions. |
*    +----------------------------+-------------------------------------------------------------------+
*
*    The values in the table above are for orientation only. The recommended
*    procedure for determining the size of the journal file is as follows:
*    +------+---------------------------------------------------------------------+
*    | Step | Action                                                              |
*    +------+---------------------------------------------------------------------+
*    | 1    | Set the journal file to an arbitrary value (for example 200 Kbytes) |
*    +------+---------------------------------------------------------------------+
*    | 2    | Let the application perform typical file system operations.         |
*    +------+---------------------------------------------------------------------+
*    | 3    | Verify if any journal overflow events occurred. If yes, then increase the journal file by a multiple of the logical sector size of the volume on which the journal file is stored and go to step 2. |
*    +------+---------------------------------------------------------------------+
*    | 4    | Done                                                                |
*    +------+---------------------------------------------------------------------+
*
*    An overflow event is reported by the Journaling component by invoking
*    the callback function registered via either FS_JOURNAL_SetOnOverflowExCallback()
*    or FS_JOURNAL_SetOnOverflowCallback(). In addition, the size of the journal file
*    can be fine tuned by evaluating the value of the MaxWriteSectorCnt member of
*    the FS_JOURNAL_STAT_COUNTERS returned via FS_JOURNAL_GetStatCounters().
*
*    If a journal is created using FS_JOURNAL_Create() the information
*    about unused logical sectors is not forwarded to the device driver.
*    FS_JOURNAL_CreateEx() can be used instead to specify how this
*    information has to be handled.
*
*    The journal operation remains disabled after the journal creation
*    if the application previously disable it via FS_JOURNAL_Disable().
*    In this case the journal operation has to be explicitly enabled
*    by the application after the journal creation via FS_JOURNAL_Enable().
*/
int FS_JOURNAL_Create(const char * sVolumeName, U32 NumBytes) {
  int         r;
  FS_VOLUME * pVolume;

  if (NumBytes == 0u) {
    return FS_ERRCODE_INVALID_PARA;   // Error, volume name not specified or invalid number of bytes.
  }
  FS_LOCK();
  r = FS_ERRCODE_VOLUME_NOT_FOUND;    // Error, volume does not exist.
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    r = _CreateJournalIfRequired(pVolume, NumBytes, 0);
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_JOURNAL_CreateEx
*
*  Function description
*    Creates the journal file.
*
*  Parameters
*    sVolumeName            Journal instance identified by volume name (0-terminated string).
*    NumBytes               Size of journal file in bytes.
*    IsFreeSectorSupported  Handling of the information about unused sectors.
*                           * 1   Forwarded to the device driver
*                           * 0   Not forwarded to device driver.
*
*  Return value
*   ==1    OK, journal already exists.
*   ==0    OK, journal successfully created.
*   < 0    Error code indicating the failure reason.
*
*  Additional information
*    This function is mandatory. It performs the same operations as FS_JOURNAL_Create().
*    In addition, IsFreeSectorSupported can be used to specify if the information
*    about the logical sectors that are no longer in use has to be passed
*    to the device driver. The NOR and NAND drivers as well as the SD/MMC
*    driver with eMMC as storage device can use this information to improve
*    the write performance.
*/
int FS_JOURNAL_CreateEx(const char * sVolumeName, U32 NumBytes, U8 IsFreeSectorSupported) {
  int         r;
  FS_VOLUME * pVolume;

  if (NumBytes == 0u) {
    return FS_ERRCODE_INVALID_PARA;   // Error, volume name not specified or invalid number of bytes.
  }
  FS_LOCK();
  r = FS_ERRCODE_VOLUME_NOT_FOUND;    // Error, volume does not exist.
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    r = _CreateJournalIfRequired(pVolume, NumBytes, IsFreeSectorSupported);
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_JOURNAL_Begin
*
*  Function description
*    Opens a journal transaction.
*
*  Parameters
*    sVolumeName        Journal instance identified by volume name (0-terminated string).
*
*  Return value
*   ==0    OK, journal transaction opened.
*   !=0    Error code indicating the failure reason.
*
*  Additional information
*    This function is optional. The file system opens and closes
*    journal transactions automatically as required. The application
*    can use this function together with FS_JOURNAL_End() to create
*    journal transactions that extend over multiple file system operations.
*    A journal transaction can be opened more than once using
*    FS_JOURNAL_Begin() and it has to be closed by calling FS_JOURNAL_End()
*    by the same number of times.
*
*    Following the call to FS_JOURNAL_Begin() all the data written
*    by the application is stored to the journal file until either
*    the application calls FS_JOURNAL_End() or the journal becomes full.
*    An application can get informed about a journal full event by
*    registering a callback function via FS_JOURNAL_SetOnOverflowCallback()
*    or FS_JOURNAL_SetOnOverflowExCallback()
*
*    It is mandatory that FS_JOURNAL_Begin() and FS_JOURNAL_End()
*    are called in pairs. The calls to these functions can be nested.
*    The current nesting level can be queried via FS_JOURNAL_GetOpenCnt().
*/
int FS_JOURNAL_Begin(const char * sVolumeName) {
  FS_VOLUME * pVolume;
  int         r;

  r = FS_ERRCODE_VOLUME_NOT_FOUND;    // Set to indicate error.
  FS_LOCK();
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    r = FS__AutoMount(pVolume);
    switch ((unsigned)r) {
    case FS_MOUNT_RW:
      FS_LOCK_DRIVER(&pVolume->Partition.Device);
      r = FS__JOURNAL_Begin(pVolume);
      FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
      break;
    case FS_MOUNT_RO:
      r = FS_ERRCODE_READ_ONLY_VOLUME;
      break;
    case 0:
      r = FS_ERRCODE_VOLUME_NOT_MOUNTED;
      break;
    default:
      //
      // An error occurred while mounting the volume.
      //
      break;
    }
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_JOURNAL_End
*
*  Function description
*    Closes a journal transaction.
*
*  Parameters
*    sVolumeName        Journal instance identified by volume name (0-terminated string).
*
*  Return value
*   ==0    OK, journal transaction closed.
*   !=0    Error code indicating the failure reason.
*
*  Additional information
*    This function is optional. The file system opens and closes
*    journal transactions automatically as required. The application
*    can use this function together with FS_JOURNAL_Begin() to create
*    journal transactions that extend over multiple file system operations.
*
*    Following the outermost call to FS_JOURNAL_End() the sector data
*    stored to journal file is copied to actual destination on the storage
*    device. The other nested calls to FS_JOURNAL_End() simply close
*    the transaction at that nesting level but do not copy any data.
*
*    It is mandatory that FS_JOURNAL_Begin() and FS_JOURNAL_End()
*    are called in pair. The calls to these functions can be nested.
*    The current nesting level can be queried via FS_JOURNAL_GetOpenCnt().
*/
int FS_JOURNAL_End(const char * sVolumeName) {
  FS_VOLUME * pVolume;
  int         r;

  r = FS_ERRCODE_VOLUME_NOT_FOUND;    // Set to indicate error.
  FS_LOCK();
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    FS_LOCK_DRIVER(&pVolume->Partition.Device);
    r = FS__JOURNAL_End(pVolume);
    FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_JOURNAL_Enable
*
*  Function description
*    Activates the journal.
*
*  Parameters
*    sVolumeName        Journal instance identified by volume name (0-terminated string).
*
*  Return value
*   ==0    OK, the journal operation is enabled.
*   !=0    Error code indicating the failure reason.
*
*  Additional information
*    This function is optional. The journal is enabled automatically
*    when the file system is mounted if a valid journal file is found.
*    FS_JOURNAL_Enable() can be used to re-enable the journal after
*    the application disabled it via FS_JOURNAL_Disable().
*
*    After the call to FS_JOURNAL_Enable() all file system operations
*    are protected against unexpected resets.
*
*    The operational status of the journal can be queried using FS_JOURNAL_IsEnabled().
*/
int FS_JOURNAL_Enable(const char * sVolumeName) {
  FS_VOLUME * pVolume;
  FS_DEVICE * pDevice;
  int         r;

  r = FS_ERRCODE_VOLUME_NOT_FOUND;    // Set to indicate error.
  FS_LOCK();
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    pDevice = &pVolume->Partition.Device;
    FS_LOCK_DRIVER(pDevice);
    pDevice->Data.JournalData.IsActive = 1;
    FS_UNLOCK_DRIVER(pDevice);
    r = 0;      // OK, journal enabled.
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_JOURNAL_Disable
*
*  Function description
*    Deactivates the journal.
*
*  Parameters
*    sVolumeName        Journal instance identified by volume name (0-terminated string).
*
*  Return value
*   ==0    OK, the journal operation is disabled.
*   !=0    Error code indicating the failure reason.
*
*  Additional information
*    This function is optional. FS_JOURNAL_Disable() can be used
*    to disable the journal if the next file system operations do
*    not have to be protected against unexpected resets.
*    After the call to this function the integrity of the file system
*    is no more guaranteed. The journal operation can be re-enabled
*    by calling FS_JOURNAL_Enable().
*
*    The operational status of the journal can be queried using FS_JOURNAL_IsEnabled().
*/
int FS_JOURNAL_Disable(const char * sVolumeName) {
  FS_VOLUME * pVolume;
  FS_DEVICE * pDevice;
  int         r;
  int         Result;

  r = FS_ERRCODE_VOLUME_NOT_FOUND;      // Set to indicate error.
  FS_LOCK();
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    pDevice = &pVolume->Partition.Device;
    FS_LOCK_DRIVER(pDevice);
    r = FS_ERRCODE_OK;
    Result = FS__JOURNAL_Clean(pVolume);
    if (Result != 0) {
      r = FS_ERRCODE_WRITE_FAILURE;
    }
    pDevice->Data.JournalData.IsActive = 0;
    FS_UNLOCK_DRIVER(pDevice);
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_JOURNAL_GetStatCounters
*
*  Function description
*    Returns statistical information about the operation.
*
*  Parameters
*    sVolumeName        Journal instance identified by volume name (0-terminated string).
*    pStat              [OUT] Statistical information.
*
*  Return value
*   ==0    OK, information returned.
*   !=0    Error code indicating the failure reason.
*
*  Additional information
*    This function is optional. It can be used to get information
*    about the number of operations performed by the journal since
*    the last file system mount operation or since the last call
*    to FS_JOURNAL_ResetStatCounters().
*
*    FS_JOURNAL_GetStatCounters() is available only when the file
*    system is compiled with either FS_JOURNAL_ENABLE_STATS set to 1
*    or with FS_DEBUG_LEVEL set to a value equal to or larger than
*    FS_DEBUG_LEVEL_CHECK_ALL.
*/
int FS_JOURNAL_GetStatCounters(const char * sVolumeName, FS_JOURNAL_STAT_COUNTERS * pStat) {
  int r;

  FS_USE_PARA(sVolumeName);
  if (pStat == NULL) {
    return FS_ERRCODE_INVALID_PARA;
  }
  FS_MEMSET(pStat, 0, sizeof(FS_JOURNAL_STAT_COUNTERS));
#if FS_JOURNAL_ENABLE_STATS
  {
    FS_VOLUME    * pVolume;
    JOURNAL_INST * pInst;

    FS_LOCK();
    r = FS_ERRCODE_VOLUME_NOT_FOUND;    // Set to indicate error.
    pVolume = FS__FindVolume(sVolumeName);
    if (pVolume != NULL) {
      pInst = _Volume2Inst(pVolume);
      if (pInst != NULL) {
        FS_LOCK_DRIVER(&pVolume->Partition.Device);
        *pStat = pInst->Status.StatCounters;
        FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
        r = FS_ERRCODE_OK;              // OK, statistical counters read.
      }
    }
    FS_UNLOCK();
  }
#else
  r = FS_ERRCODE_NOT_SUPPORTED;
#endif // FS_JOURNAL_ENABLE_STATS
  return r;
}

/*********************************************************************
*
*       FS_JOURNAL_ResetStatCounters
*
*  Function description
*    Sets to 0 all statistical counters.
*
*  Parameters
*    sVolumeName        Journal instance identified by volume name (0-terminated string).
*
*  Return value
*   ==0    OK, statistical counters cleared.
*   !=0    Error code indicating the failure reason.
*
*  Additional information
*    This function is optional. The statistical counters are cleared
*    each time the volume is mounted. An application can use
*    FS_JOURNAL_ResetStatCounters() to explicitly clear the statistical
*    counters at runtime for example for testing purposes.
*    The statistical counters can be queried via FS_JOURNAL_GetStatCounters()
*
*    FS_JOURNAL_ResetStatCounters() is available only when the file
*    system is compiled with either FS_JOURNAL_ENABLE_STATS set to 1
*    or with FS_DEBUG_LEVEL set to a value equal to or larger than
*    FS_DEBUG_LEVEL_CHECK_ALL.
*/
int FS_JOURNAL_ResetStatCounters(const char * sVolumeName) {
  int r;

  FS_USE_PARA(sVolumeName);
#if FS_JOURNAL_ENABLE_STATS
  {
    FS_VOLUME    * pVolume;
    JOURNAL_INST * pInst;

    FS_LOCK();
    r = FS_ERRCODE_VOLUME_NOT_FOUND;    // Set to indicate error
    pVolume = FS__FindVolume(sVolumeName);
    if (pVolume != NULL) {
      pInst = _Volume2Inst(pVolume);
      if (pInst != NULL) {
        FS_LOCK_DRIVER(&pVolume->Partition.Device);
        FS_MEMSET(&pInst->Status.StatCounters, 0, sizeof(FS_JOURNAL_STAT_COUNTERS));
        FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
        r = FS_ERRCODE_OK;              // OK, statistical counters cleared.
      }
    }
    FS_UNLOCK();
  }
#else
  r = FS_ERRCODE_NOT_SUPPORTED;
#endif
  return r;
}

/*********************************************************************
*
*       FS_JOURNAL_GetOpenCnt
*
*  Function description
*    Returns the number times the current journal transaction has been opened.
*
*  Parameters
*    sVolumeName        Journal instance identified by volume name (0-terminated string).
*
*  Return value
*   >=0    OK, number of nested calls.
*   !=0    Error code indicating the failure reason.
*
*  Additional information
*    This function is optional. The application can use FS_JOURNAL_GetOpenCnt()
*    to check how many times FS_JOURNAL_Begin() has been called in a row
*    without a call to FS_JOURNAL_End() in between.
*/
int FS_JOURNAL_GetOpenCnt(const char * sVolumeName) {
  FS_VOLUME    * pVolume;
  JOURNAL_INST * pInst;
  int            r;
  FS_DEVICE    * pDevice;

  r = FS_ERRCODE_VOLUME_NOT_FOUND;              // Set to indicate error.
  FS_LOCK();
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    pDevice = &pVolume->Partition.Device;
    FS_LOCK_DRIVER(pDevice);
    r = FS_ERRCODE_FILE_DIR_NOT_FOUND;          // Indicate that the journal file has not been found.
    if (FS__JOURNAL_IsPresent(pDevice) != 0) {
      r = FS_ERRCODE_VOLUME_NOT_FOUND;          // TBD: Find a more suitable error code.
      pInst = _Volume2Inst(pVolume);
      if (pInst != NULL) {
        r = (int)pInst->Status.OpenCnt;
      }
    }
    FS_UNLOCK_DRIVER(pDevice);
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_JOURNAL_Invalidate
*
*  Function description
*    Cancels the pending journal transaction.
*
*  Parameters
*    sVolumeName        Journal instance identified by volume name (0-terminated string).
*
*  Return value
*   >=0    OK, journal data has been discarded.
*   !=0    Error code indicating the failure reason.
*
*  Additional information
*    This function is optional. It can be used to discard all the
*    modifications stored in the journal during a journal transaction
*    opened via FS_JOURNAL_Begin(). After the call to FS_JOURNAL_Invalidate()
*    the current journal transaction is closed. In case of a journal
*    transaction opened multiple times it is not necessary to call
*    FS_JOURNAL_Invalidate() for the number of times the journal transaction
*    has been opened.
*
*    A read sector cache has to be invalidated after canceling a
*    journal transaction via FS_JOURNAL_Invalidate(). The application
*    can configure a read sector cache via FS_AssignCache().
*/
int FS_JOURNAL_Invalidate(const char * sVolumeName) {
  FS_VOLUME * pVolume;
  int         r;

  r = FS_ERRCODE_VOLUME_NOT_FOUND;
  FS_LOCK();
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    FS_LOCK_DRIVER(&pVolume->Partition.Device);
    FS__JOURNAL_Invalidate(pVolume);
    r = FS__JOURNAL_Mount(pVolume);
    FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_JOURNAL_SetOnOverflowCallback
*
*  Function description
*    Registers a callback function for the journal full event.
*
*  Parameters
*    pfOnOverflow     Function to be invoked when the journal full event occurs.
*
*  Additional information
*    This function is optional. A journal full event occurs when there
*    is no more free space in the journal file to store the modifications
*    requested by the file system layer. When this event occurs, the data currently
*    stored in the journal is copied to the actual destination on the storage
*    device to make free space for the new data.
*
*    The file system is no longer fail safe in the time interval from
*    the occurrence of the journal  full event to the end of current
*    journal transaction.
*/
void FS_JOURNAL_SetOnOverflowCallback(FS_JOURNAL_ON_OVERFLOW_CALLBACK * pfOnOverflow) {
  _pfOnOverflow = pfOnOverflow;
}

/*********************************************************************
*
*       FS_JOURNAL_SetOnOverflowExCallback
*
*  Function description
*    Registers a callback function for the journal full event.
*
*  Parameters
*    pfOnOverflow     Function to be invoked when the journal full event occurs.
*
*  Additional information
*    This function is optional. A journal full event occurs when there
*    is no more free space in the journal file to store the modifications
*    requested by the file system layer. When this event occurs, the data currently
*    stored in the journal is copied to the actual destination on the storage
*    device to make free space for the new data. This behavior can be changed
*    via the return value of the callback function. Refer to
*    FS_JOURNAL_ON_OVERFLOW_EX_CALLBACK for more information.
*
*    The file system is no longer fail safe in the time interval from
*    the occurrence of the journal  full event to the end of current
*    journal transaction.
*/
void FS_JOURNAL_SetOnOverflowExCallback(FS_JOURNAL_ON_OVERFLOW_EX_CALLBACK * pfOnOverflow) {
  _pfOnOverflowEx = pfOnOverflow;
}

/*********************************************************************
*
*       FS_JOURNAL_SetFileName
*
*  Function description
*    Configures the name of the journal file.
*
*  Parameters
*    sVolumeName        Journal instance identified by volume name (0-terminated string).
*    sFileName          Name of the journal file (0-terminated string)
*
*  Return value
*    ==0      OK, file name set.
*    !=0      Error code indicating the failure reason.
*
*  Additional information
*    This function is optional. It can be used by an application
*    to specify at runtime a name for the journal file. FS_JOURNAL_SetFileName()
*    has to be called before the creation of the journal file via
*    FS_JOURNAL_Create() or FS_JOURNAL_CreateEx().
*
*    FS_JOURNAL_SetFileName() is available only when the file system
*    is compiled with the FS_MAX_LEN_JOURNAL_FILE_NAME configuration
*    define set to a value greater than 0.
*/
int FS_JOURNAL_SetFileName(const char * sVolumeName, const char * sFileName) {
  int r;

#if (FS_MAX_LEN_JOURNAL_FILE_NAME > 0)
  FS_VOLUME * pVolume;

  r = FS_ERRCODE_VOLUME_NOT_FOUND;
  FS_LOCK();
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    r = FS_ERRCODE_INVALID_PARA;
    if (sFileName != NULL) {
      FS_LOCK_DRIVER(&pVolume->Partition.Device);
      (void)FS_STRNCPY(pVolume->acJournalFileName, sFileName, sizeof(pVolume->acJournalFileName) - 1u);
      pVolume->acJournalFileName[sizeof(pVolume->acJournalFileName) - 1u] = '\0';
      FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
      r = 0;
    }
  }
  FS_UNLOCK();
#else
  FS_USE_PARA(sVolumeName);
  FS_USE_PARA(sFileName);
  r = FS_ERRCODE_NOT_SUPPORTED;
#endif
  return r;
}

/*********************************************************************
*
*       FS_JOURNAL_IsEnabled
*
*  Function description
*    Checks the journal operational status.
*
*  Parameters
*    sVolumeName        Journal instance identified by volume name (0-terminated string).
*
*  Return value
*    ==1      The journal is active. All the file system operations are fail safe.
*    ==0      The journal is not active. The file system operations are not fail safe.
*    < 0      Error code indicating the failure reason.
*
*  Additional information
*    This function is optional. The journal is automatically activated
*    at file system mount if a valid journal file is present. The journal
*    file can be created using FS_JOURNAL_Create() or FS_JOURNAL_CreateEx().
*    The journal can be enabled and disabled at runtime using FS_JOURNAL_Enable()
*    and FS_JOURNAL_Disable() respectively.
*/
int FS_JOURNAL_IsEnabled(const char * sVolumeName) {
  FS_VOLUME * pVolume;
  FS_DEVICE * pDevice;
  int         r;

  r = FS_ERRCODE_VOLUME_NOT_FOUND;        // Set to indicate error.
  FS_LOCK();
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    pDevice = &pVolume->Partition.Device;
    FS_LOCK_DRIVER(pDevice);
    r = FS_ERRCODE_FILE_DIR_NOT_FOUND;    // Indicate that the journal file has not been found.
    if (FS__JOURNAL_IsPresent(pDevice) != 0) {
      r = (int)pDevice->Data.JournalData.IsActive;
    }
    FS_UNLOCK_DRIVER(pDevice);
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_JOURNAL_GetInfo
*
*  Function description
*    Returns information about the journal.
*
*  Parameters
*    sVolumeName        Journal instance identified by volume name (0-terminated string).
*    pInfo              [OUT] Journal information.
*
*  Return value
*    ==0      OK, information returned.
*    < 0      Error code indicating the failure reason.
*
*  Additional information
*    This function is optional. The application can call it to get
*    information about the journal such as if the journal is enabled,
*    the number of free sectors in the journal and so on.
*
*    FS_JOURNAL_GetInfo() mounts the specified volume if the auto mount
*    feature is enabled for that volume and the volume is not mounted a
*    the time of the call.
*/
int FS_JOURNAL_GetInfo(const char * sVolumeName, FS_JOURNAL_INFO * pInfo) {
  JOURNAL_INST * pInst;
  FS_VOLUME    * pVolume;
  FS_DEVICE    * pDevice;
  int            r;
  int            IsPresent;

  if (pInfo == NULL) {
    return FS_ERRCODE_INVALID_PARA;
  }
  r = FS_ERRCODE_VOLUME_NOT_FOUND;                // Set to indicate error.
  FS_LOCK();
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    r = FS__AutoMount(pVolume);
    switch ((unsigned)r) {
    case FS_MOUNT_RW:
      pDevice = &pVolume->Partition.Device;
      FS_LOCK_DRIVER(pDevice);
      r = FS_ERRCODE_VOLUME_NOT_MOUNTED;
      if (pVolume->MountType == FS_MOUNT_RW) {    // Make sure that another task did not unmount the volume.
        r = FS_ERRCODE_FILE_DIR_NOT_FOUND;        // Indicate that the journal file has not been found.
        IsPresent = FS__JOURNAL_IsPresent(pDevice);
        if (IsPresent != 0) {
          r = FS_ERRCODE_VOLUME_NOT_FOUND;        // TBD: Find a more suitable error code.
          pInst = _Volume2Inst(pVolume);
          if (pInst != NULL) {
            pInfo->IsEnabled             = pDevice->Data.JournalData.IsActive;
            pInfo->IsFreeSectorSupported = pInst->Status.IsFreeSectorSupported;
            pInfo->OpenCnt               = pInst->Status.OpenCnt;
            pInfo->NumSectors            = pInst->Status.NumSectorsData;
            pInfo->NumSectorsFree        = pInst->Status.NumSectorsData - pInst->Status.SectorCnt;
            r = FS_ERRCODE_OK;
          }
        }
      }
      FS_UNLOCK_DRIVER(pDevice);
      break;
    case FS_MOUNT_RO:
      r = FS_ERRCODE_READ_ONLY_VOLUME;
      break;
    case 0:
      r = FS_ERRCODE_VOLUME_NOT_MOUNTED;
      break;
    default:
      //
      // An error occurred while mounting the volume.
      //
      break;
    }
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_JOURNAL_IsPresent
*
*  Function description
*    Checks the presence of journal file.
*
*  Parameters
*    sVolumeName        Journal instance identified by volume name (0-terminated string).
*
*  Return value
*    ==1      OK, journal file is present and valid.
*    ==0      OK, journal file is not present.
*    < 0      Error code indicating the failure reason.
*
*  Additional information
*    This function is optional. The application can call it to check
*    if a journal file is present on the specified volume and that
*    the file is also valid.
*
*    FS_JOURNAL_IsPresent() mounts the specified volume if the auto mount
*    feature is enabled for that volume and the volume is not mounted a
*    the time of the call.
*/
int FS_JOURNAL_IsPresent(const char * sVolumeName) {
  FS_VOLUME * pVolume;
  FS_DEVICE * pDevice;
  int         r;

  r = FS_ERRCODE_VOLUME_NOT_FOUND;                // Set to indicate error.
  FS_LOCK();
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    r = FS__AutoMount(pVolume);
    switch ((unsigned)r) {
    case FS_MOUNT_RW:
      pDevice = &pVolume->Partition.Device;
      FS_LOCK_DRIVER(pDevice);
      r = FS_ERRCODE_VOLUME_NOT_MOUNTED;
      if (pVolume->MountType == FS_MOUNT_RW) {    // Make sure that another task did not unmount the volume.
        r = FS__JOURNAL_IsPresent(pDevice);
      }
      FS_UNLOCK_DRIVER(pDevice);
      break;
    case FS_MOUNT_RO:
      r = FS_ERRCODE_READ_ONLY_VOLUME;
      break;
    case 0:
      r = FS_ERRCODE_VOLUME_NOT_MOUNTED;
      break;
    default:
      //
      // An error occurred while mounting the volume.
      //
      break;
    }
  }
  FS_UNLOCK();
  return r;
}

#endif // FS_SUPPORT_JOURNAL

/*************************** End of file ****************************/
