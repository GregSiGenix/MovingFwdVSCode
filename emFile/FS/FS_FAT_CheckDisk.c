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
File        : FS_FAT_CheckDisk.c
Purpose     : Implementation of file system integrity check for FAT.
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#include "FS_Int.h"
#include "FS_FAT_Int.h"

/*********************************************************************
*
*       Defines, configurable
*
**********************************************************************
*/
#ifdef FS_CHECKDISK_TEST_SECTOR_USAGE
  #define FS_FAT_CHECK_SECTOR_USAGE       FS_CHECKDISK_TEST_SECTOR_USAGE
#endif
#ifndef   FS_FAT_CHECK_SECTOR_USAGE
  #define FS_FAT_CHECK_SECTOR_USAGE       0   // When set to 1 the usage of a sector (as reported by the NAND or NOR drivers)
                                              // is checked against the allocated clusters. False errors can be reported since
                                              // the file system can not differentiate which sectors inside a cluster
                                              // contain valid data. This configuration define is experimental.
#endif

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static int _FileIndex    = 0;
static int _LastDirIndex = -1;      // The first created directory should have the index 0.
static int _UseSameDir;
static int _AbortRequested;

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

//lint -esym(9070, _CheckDir) recursive function D:114. Reason: The most natural way to implement the checking due to the recursive structure of the file system.

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
*       _InitClusterMap
*
*  Function description
*    Frees all the entries in the cluster map.
*/
static void _InitClusterMap(const FS_CLUSTER_MAP * pClusterMap) {
  FS_MEMSET(pClusterMap->pData, 0, ((unsigned)pClusterMap->NumClusters + 7u) >> 3u);
}

/*********************************************************************
*
*       _MarkClusterAsAllocated
*
*  Function description
*    Mark a cluster as not in use.
*/
static void _MarkClusterAsAllocated(const FS_CLUSTER_MAP * pClusterMap, U32 iCluster) {
  unsigned   Mask;
  U8       * pData;
  unsigned   Data;

  Mask    = 1uL << (iCluster & 7u);
  pData   = pClusterMap->pData + (iCluster >> 3u);
  Data    = *pData;
  Data   |= Mask;
  *pData  = (U8)Data;
}

/*********************************************************************
*
*       _MarkClusterAsFree
*
*  Function description
*    Marks a cluster as in use.
*/
static void _MarkClusterAsFree(const FS_CLUSTER_MAP * pClusterMap, U32 iCluster) {
  unsigned   Mask;
  unsigned   Data;
  U8       * pData;

  Mask    = 1uL << (iCluster & 7u);
  pData   = pClusterMap->pData + (iCluster >> 3u);
  Data    = *pData;
  Data   &= ~Mask;
  *pData  = (U8)Data;
}

/*********************************************************************
*
*       _IsClusterFree
*
*  Function description
*    Checks if a cluster is not in use.
*/
static int _IsClusterFree(const FS_CLUSTER_MAP * pClusterMap, U32 iCluster) {
  unsigned   Mask;
  const U8 * pData;

  Mask  = 1uL << (iCluster & 7u);
  pData = pClusterMap->pData + (iCluster >> 3u);
  if ((*pData & Mask) != 0u) {
    return 0;
  }
  return 1;
}

/*********************************************************************
*
*       _AddToClusterMap
*
*   Return value
*     == 0    O.K. Cluster info has been updated
*     == 1    Entry already exists
*     ==-1    Entry not valid for this pClusterMap
*/
static int _AddToClusterMap(const FS_CLUSTER_MAP * pClusterMap, U32 ClusterId) {
  I32 Off;

  Off = (I32)ClusterId - (I32)pClusterMap->FirstClusterId;
  if ((Off >= 0) && (Off < pClusterMap->NumClusters)) {
    if (_IsClusterFree(pClusterMap, (U32)Off) != 0) {
      _MarkClusterAsAllocated(pClusterMap, (U32)Off);
    } else {
      return 1;
    }
  } else {
    return -1;
  }
  return 0;
}

#if FS_FAT_CHECK_SECTOR_USAGE

/*********************************************************************
*
*       _RefreshSector
*
*  Function description
*    Rewrites the sector data in order to make the sector data valid.
*
*  Return value
*    ==0        OK, sector refreshed
*    !=0        An error occurred
*/
static int _RefreshSector(U32 SectorIndex, unsigned SectorType, FS_SB * pSB) {
  int r;
  int Result;

  r = 0;
  FS__SB_SetSector(pSB, SectorIndex, SectorType, 1);
  Result = FS__SB_Read(pSB);
  if (Result != 0) {
    r = FS_ERRCODE_READ_FAILURE;
  } else {
    Result = FS__SB_Write(pSB);
    if (Result != 0) {
      r = FS_ERRCODE_WRITE_FAILURE;
    }
  }
  return r;
}

/*********************************************************************
*
*       _RefreshSectorIfRequired
*
*  Function description
*    Rewrites the sector data in order to make the sector data valid,
*    but only if the sector is marked in the driver as not used.
*
*  Return value
*    ==FS_CHECKDISK_RETVAL_OK     Sector not refreshed.
*    ==FS_CHECKDISK_RETVAL_RETRY  Sector refreshed, FS_CheckDisk() should be invoked again.
*    ==FS_CHECKDISK_RETVAL_ABORT  Sector not refreshed, operation aborted by user.
*    < 0                          Error code indicating the failure reason.
*/
static int _RefreshSectorIfRequired(FS_VOLUME * pVolume, U32 SectorIndex, unsigned SectorType, FS_SB * pSB, FS_CHECKDISK_ON_ERROR_CALLBACK * pfOnError) {
  int r;
  int SectorUsage;
  int Action;
  U32 SectorIndexAbs;

  r = FS_CHECKDISK_RETVAL_OK;
  SectorIndexAbs  = pVolume->Partition.StartSector;
  SectorIndexAbs += SectorIndex;
  SectorUsage = FS__STORAGE_GetSectorUsageNL(pVolume, SectorIndexAbs);
  if (SectorUsage == FS_SECTOR_NOT_USED) {
    Action = pfOnError(FS_CHECKDISK_ERRCODE_SECTOR_NOT_IN_USE, SectorIndex);
    switch (Action) {
    case FS_CHECKDISK_ACTION_DO_NOT_REPAIR:
      r = FS_CHECKDISK_RETVAL_OK;
      break;
    case FS_CHECKDISK_ACTION_SAVE_CLUSTERS:
      // through
    case FS_CHECKDISK_ACTION_DELETE_CLUSTERS:
      //
      // Refresh sector data.
      //
      r = _RefreshSector(SectorIndex, SectorType, pSB);
      if (r == 0) {
        r = FS_CHECKDISK_RETVAL_RETRY;
      }
      break;
    default:
      r = FS_CHECKDISK_RETVAL_ABORT;
      break;
    }
  }
  return r;
}

#endif  // FS_FAT_CHECK_SECTOR_USAGE

/*********************************************************************
*
*       _IsValidShortNameChar
*/
static int _IsValidShortNameChar(unsigned Byte) {
  if (Byte == (unsigned)' ') {              // A space is used to pad the name on the storage but not allowed in a name used in an application.
    return 1;
  }
  return FS_FAT_IsValidShortNameChar((U8)Byte);
}

#if FS_SUPPORT_MBCS

/*********************************************************************
*
*       _CheckShortNameMBPartial
*
*  Function description
*    Checks the validity of a part of a short file name.
*
*  Return value
*    ==0    Valid file name
*    ==1    File name is invalid
*
*  Additional information
*    This function works only for multi-byte character sets.
*/
static int _CheckShortNameMBPartial(const U8 * pName, unsigned NumBytes) {
  FS_WCHAR Char;
  unsigned NumBytesRead;

  for (;;) {
    if (NumBytes == 0u) {
      return 0;           // OK, the name is valid.
    }
    NumBytesRead = 0;
    Char = FS_pCharSetType->pfGetChar(pName, NumBytes, &NumBytesRead);
    if (Char == FS_WCHAR_INVALID) {
      return 1;           // Error, invalid character sequence.
    }
    if (NumBytesRead == 0u) {
      return 1;           // Error, invalid character sequence.
    }
    if (Char < 128u) {    // Only ASCII characters can be invalid.
      if (_IsValidShortNameChar((U8)Char) == 0) {
        return 1;         // Error, invalid character in a short file name.
      }
    }
    if (FS_pCharSetType->pfIsLower(Char) != 0) {
      return 1;           // Error, all letters have to be stored in uppercase.
    }
    if (NumBytesRead > NumBytes) {
      return 1;           // Error, invalid character sequence.
    }
    NumBytes -= NumBytesRead;
    pName    += NumBytesRead;
  }
}

/*********************************************************************
*
*       _CheckShortNameMB
*
*  Function description
*    Checks for validity the file name of a directory entry which
*    stores a short file name.
*
*  Return value
*    ==0    Valid file name
*    ==1    File name is invalid
*
*  Additional information
*    This function works only for multi-byte character sets.
*/
static int _CheckShortNameMB(const FS_FAT_DENTRY * pDirEntry) {
  int        r;
  const U8 * pName;
  unsigned   NumBytes;
  U8         abName[FAT_MAX_NUM_BYTES_BASE];

  //
  // Check the base name.
  //
  pName    = &pDirEntry->Data[0];
  NumBytes = FAT_MAX_NUM_BYTES_BASE;
  //
  // 0xE5 is a valid character in the character set used in Japan.
  // It is replaced with 0x05 to indicate that the file is not deleted.
  //
  if (*pName == 0x05u) {
    abName[0] = 0xE5;
    FS_MEMCPY(&abName[1], pName + 1, NumBytes - 1u);
    pName = abName;
  }
  r = _CheckShortNameMBPartial(pName, NumBytes);
  if (r == 0) {
    //
    // Check the extension.
    //
    pName    = &pDirEntry->Data[FAT_MAX_NUM_BYTES_BASE];
    NumBytes = FAT_MAX_NUM_BYTES_EXT;
    r = _CheckShortNameMBPartial(pName, NumBytes);
  }
  return r;
}

#endif // FS_SUPPORT_MBCS

/*********************************************************************
*
*       _CheckShortNameSB
*
*  Function description
*    Checks for validity the file name of a directory entry which
*    stores a short file name.
*
*  Return value
*    ==0    Valid file name
*    ==1    File name is invalid
*
*  Additional information
*    This function works only for single-byte character sets.
*/
static int _CheckShortNameSB(const FS_FAT_DENTRY * pDirEntry) {
  int      i;
  unsigned Byte;

  for (i = 0; i < 11; i++) {
    //
    // 0xE5 is a valid character in the character set used in Japan.
    // It is replaced with 0x05 to indicate that the file is not deleted.
    //
    Byte = pDirEntry->Data[i];
    if (i == 0) {
      if (Byte == 0x05u) {
        Byte = 0xE5;
      }
    }
    if (_IsValidShortNameChar((U8)Byte) == 0) {
      return 1;     // Error, invalid character in a short file name.
    }
    if ((Byte >= (unsigned)'a') && (Byte <= (unsigned)'z')) {
      return 1;     // Error, ASCII letters are stored in upper case.
    }
  }
  return 0;         // OK, the short file name is valid.
}

/*********************************************************************
*
*       _CheckShortName
*
*  Function description
*    Checks for validity the file name of a directory entry which stores a short file name.
*
*  Return value
*    ==0    Valid file name
*    ==1    File name is invalid
*/
static int _CheckShortName(const FS_FAT_DENTRY * pDirEntry) {
  int r;

#if FS_SUPPORT_MBCS
  if (FS_pCharSetType->pfGetChar != NULL) {
    r = _CheckShortNameMB(pDirEntry);
  } else
#endif
  {
    r = _CheckShortNameSB(pDirEntry);
  }
  return r;
}

/*********************************************************************
*
*       _MarkDirEntryAsDeleted
*
*  Function description
*    Deletes a directory entry if the user agrees.
*
*  Return value
*    FS_CHECKDISK_RETVAL_OK     Directory entry not deleted
*    FS_CHECKDISK_RETVAL_RETRY  Directory entry delete, FS_CheckDisk() should be invoked again.
*    FS_CHECKDISK_RETVAL_ABORT  Directory entry not deleted, operation aborted by user
*/
static int _MarkDirEntryAsDeleted(FS_FAT_DENTRY * pDirEntry, FS_SB * pSB, FS_CHECKDISK_ON_ERROR_CALLBACK * pfOnError) {
  int Action;
  int r;

  Action = pfOnError(FS_CHECKDISK_ERRCODE_INVALID_DIRECTORY_ENTRY);
  switch (Action) {
  case FS_CHECKDISK_ACTION_DO_NOT_REPAIR:
    r = FS_CHECKDISK_RETVAL_OK;
    break;
  case FS_CHECKDISK_ACTION_SAVE_CLUSTERS:
    // through
  case FS_CHECKDISK_ACTION_DELETE_CLUSTERS:
    //
    // Delete the directory entry.
    //
    if (pDirEntry != NULL) {
      pDirEntry->Data[0] = 0xE5u;
      FS__SB_MarkDirty(pSB);
    }
    r = FS_CHECKDISK_RETVAL_RETRY;
    break;
  default:
    r = FS_CHECKDISK_RETVAL_ABORT;
    break;
  }
  return r;
}

/*********************************************************************
*
*       _CheckLongDirEntry
*
*  Function description
*    Checks the validity of a directory entry which belongs to a long file name.
*
*  Return value
*    FS_CHECKDISK_RETVAL_OK     No error found or user does not want to repair the error
*    FS_CHECKDISK_RETVAL_RETRY  Error found FS_CheckDisk() should be invoked again.
*    FS_CHECKDISK_RETVAL_ABORT  Operation aborted by user
*/
static int _CheckLongDirEntry(FS_VOLUME * pVolume, FS_DIR_POS * pDirPos, FS_FAT_DENTRY * pDirEntry, FS_SB * pSB, FS_CHECKDISK_ON_ERROR_CALLBACK * pfOnError) {
  FS_DIR_POS DirPos;
  unsigned   CurrentIndex;
  U32        NumEntries;
  unsigned   Checksum;
  unsigned   CalcedCheckSum;
  int        r;

  DirPos = *pDirPos; // Save old DirPos settings
  //
  // Ensure that this is the last long file name entry.
  //
  if ((pDirEntry->Data[0] & 0x40u) == 0u) {
    r = _MarkDirEntryAsDeleted(pDirEntry, pSB, pfOnError);
    return r;                     // Error, found a long file name entry with no last entry flag.
  }
  CurrentIndex = (unsigned)pDirEntry->Data[0] & 0x3Fu;
  NumEntries   = CurrentIndex;
  Checksum     = pDirEntry->Data[13];
  do {
    //
    // Check entry for validity
    //
    if (pDirEntry->Data[13] != Checksum) {
      r = _MarkDirEntryAsDeleted(pDirEntry, pSB, pfOnError);
      return r;       // Error, check sum does not match
    }
    if (pDirEntry->Data[11] != FS_FAT_ATTR_LONGNAME) {
      r = _MarkDirEntryAsDeleted(pDirEntry, pSB, pfOnError);
      return r;       // Error, attributes do not match.
    }
    if (FS_LoadU16LE(&pDirEntry->Data[26]) != 0u) {
      r = _MarkDirEntryAsDeleted(pDirEntry, pSB, pfOnError);
      return r;       // Error, First cluster information should be zero.
    }
    //
    // Get the next directory entry.
    //
    FS_FAT_IncDirPos(pDirPos);
    pDirEntry = FS_FAT_GetDirEntry(pVolume, pSB, pDirPos);
    //
    // No more entries available.
    //
    if (pDirEntry == NULL) {
      return FS_CHECKDISK_RETVAL_OK;
    }
    if (pDirEntry->Data[0] == 0u) {
      return FS_CHECKDISK_RETVAL_OK;      // OK, directory entry is valid.
    }
  } while (--CurrentIndex != 0u);
  //
  // Check if calculated check sum of short directory matches with the checksum stored in the LFN entry(ies)
  //
  CalcedCheckSum = FS_FAT_CalcCheckSum(pDirEntry->Data);
  if (CalcedCheckSum != Checksum) {
    r = FS_CHECKDISK_RETVAL_OK;
    *pDirPos = DirPos;
    for (;;) {
      pDirEntry = FS_FAT_GetDirEntry(pVolume, pSB, pDirPos);
      if (pDirEntry != NULL) {
        r = _MarkDirEntryAsDeleted(pDirEntry, pSB, pfOnError);
      }
      if (--NumEntries == 0u) {
        break;
      }
      FS_FAT_IncDirPos(pDirPos);
    }
    return r;
  }
  //
  // Move back to the last LFN entry. The short file name entry shall be checked in the other routine.
  //
  pDirPos->DirEntryIndex--;
  return 0;           // OK, directory entry is valid.
}

/*********************************************************************
*
*       _CheckDirEntry
*
*  Function description
*    Checks the validity of a directory entry.
*
*  Return value
*    ==0    Valid directory entry
*    ==1    Directory entry is not valid
*/
static int _CheckDirEntry(const FS_FAT_INFO * pFATInfo, const FS_FAT_DENTRY * pDirEntry) {
  unsigned Attr;
  U32      FirstCluster;
  U32      FileSize;
  U32      TotalBytesOnDisk;

  Attr = pDirEntry->Data[DIR_ENTRY_OFF_ATTRIBUTES];
  //
  // If any bits other than specified by FAT in
  // the attributes field are set, mark as invalid
  //
  if ((Attr & ~FS_FAT_ATTR_MASK) != 0u) {
    return 1;       // Error
  }
  //
  // Check the short directory entry
  //
  if (Attr != FS_FAT_ATTR_LONGNAME) {
    if ((Attr != FS_FAT_ATTR_DIRECTORY) && (pDirEntry->Data[0] != (U8)'.')) {
      if (_CheckShortName(pDirEntry) != 0) {
        return 1;   // Error
      }
    }
    FirstCluster = FS_FAT_GetFirstCluster(pDirEntry);
    if (FirstCluster >= (pFATInfo->NumClusters + (U32)FAT_FIRST_CLUSTER)) {
      return 1;     // Error
    }
    TotalBytesOnDisk = FS__CalcSizeInBytes(pFATInfo->NumClusters, pFATInfo->SectorsPerCluster, pFATInfo->BytesPerSector);
    FileSize = FS_LoadU32LE(&pDirEntry->Data[DIR_ENTRY_OFF_SIZE]);
    if (FileSize > TotalBytesOnDisk) {
      return 1;
    }
  }
  return 0;         // OK, directory entry is valid.
}

#if FS_FAT_CHECK_UNUSED_DIR_ENTRIES

/*********************************************************************
*
*       _MarkDirEntryAsEmpty
*
*  Function description
*    Fills all the bytes in a directory entry with 0s if the application agrees.
*
*  Parameters
*    pDirEntry        Directory entry to zeroed. Cannot be NULL.
*    pSB              Sector buffer to be used for the operation. Cannot be NULL.
*    pfOnError        Callback function for confirming the operation. Cannot be NULL.
*
*  Return value
*    FS_CHECKDISK_RETVAL_OK     Directory entry not zeroed.
*    FS_CHECKDISK_RETVAL_RETRY  Directory entry delete, FS_CheckDisk() should be invoked again.
*    FS_CHECKDISK_RETVAL_ABORT  Directory entry not deleted, operation aborted by user
*/
static int _MarkDirEntryAsEmpty(FS_FAT_DENTRY * pDirEntry, FS_SB * pSB, FS_CHECKDISK_ON_ERROR_CALLBACK * pfOnError) {
  int Action;
  int r;

  Action = pfOnError(FS_CHECKDISK_ERRCODE_INVALID_DIRECTORY_ENTRY);
  switch (Action) {
  case FS_CHECKDISK_ACTION_DO_NOT_REPAIR:
    r = FS_CHECKDISK_RETVAL_OK;
    break;
  case FS_CHECKDISK_ACTION_SAVE_CLUSTERS:
    // through
  case FS_CHECKDISK_ACTION_DELETE_CLUSTERS:
    //
    // Fill the directory entry with 0s.
    //
    FS_MEMSET(pDirEntry->Data, 0, sizeof(pDirEntry->Data));
    FS__SB_MarkDirty(pSB);
    r = FS_CHECKDISK_RETVAL_RETRY;
    break;
  default:
    r = FS_CHECKDISK_RETVAL_ABORT;
    break;
  }
  return r;
}

/*********************************************************************
*
*       _IsEmptyDirEntry
*
*  Function description
*    Checks if a directory entry is filled with 0s.
*
*  Parameters
*    pDirEntry        Directory entry to be checked. Cannot be NULL.
*
*  Return value
*    !=0      All the bytes in the directory entry are set to 0.
*    ==0      At least one byte in the directory entry is not set to 0.
*/
static int _IsEmptyDirEntry(const FS_FAT_DENTRY * pDirEntry) {
  unsigned    NumItems;
  const U32 * pData32;

  NumItems = sizeof(pDirEntry->Data) >> 2u;                   // We know that the directory entry is a multiple of 4 bytes.
  pData32  = SEGGER_CONSTPTR2PTR(const U32, pDirEntry->Data); // MISRA deviation D:100[c]
  do {
    if (*pData32++ != 0u) {
      return 0;
    }
  } while (--NumItems != 0u);
  return 1;
}

#endif // FS_FAT_CHECK_UNUSED_DIR_ENTRIES

/*********************************************************************
*
*       _CreateFixFileName
*
*  Function description
*    Generates a serially numbered file name.
*
*  Parameters
*    pFileName    [OUT] Generated file name.
*
*  Additional information
*    pFileName has to point to a memory region that is at least
*    13 characters large. The function is typ. used to create
*    a file name for the file that stores the contents of a lost
*    cluster.
*/
static void _CreateFixFileName(char * pFileName) {
  char * p;

  p = pFileName;
  *p++ = 'F';
  *p++ = 'I';
  *p++ = 'L';
  *p++ = 'E';
  *p++ = (char)((_FileIndex  / 1000)        + '0');
  *p++ = (char)(((_FileIndex % 1000) / 100) + '0');
  *p++ = (char)(((_FileIndex % 100)  /  10) + '0');
  *p++ = (char)(((_FileIndex % 10)        ) + '0');
  *p++ = '.';
  *p++ = 'C';
  *p++ = 'H';
  *p++ = 'K';
  *p   = '\0';
  _FileIndex++;
}

/*********************************************************************
*
*       _CreateFixDirName
*
*  Function description
*    Generates a serially numbered directory name.
*
*  Additional information
*    Typ. used to create a name for a directory to save the files that
*    store the contents of lost clusters. pDirName has to point to a
*    memory reagion that is at least 13 characters large.
*/
static void _CreateFixDirName(char * pDirName) {
  char * p;

  p = pDirName;
  *p++ = 'F';
  *p++ = 'O';
  *p++ = 'U';
  *p++ = 'N';
  *p++ = 'D';
  *p++ = '.';
  *p++ = (char)(((_LastDirIndex % 1000) / 100) + '0');
  *p++ = (char)(((_LastDirIndex % 100)  /  10) + '0');
  *p++ = (char)(((_LastDirIndex % 10)        ) + '0');
  *p   = '\0';
}

/*********************************************************************
*
*       _IsClusterEOC
*/
static int _IsClusterEOC(FS_VOLUME * pVolume, FS_SB * pSB, U32 Cluster) {
  int r;

  r = 0;
  Cluster = FS_FAT_ReadFATEntry(pVolume, pSB, Cluster);
  switch (pVolume->FSInfo.FATInfo.FATType) {
  case FS_FAT_TYPE_FAT12:
    if ((Cluster & 0xFFFu) == 0xFFFu) {
      r = 1;
    }
    break;
  case FS_FAT_TYPE_FAT16:
    if ((Cluster & 0xFFFFu) == 0xFFFFu) {
      r = 1;
    }
    break;
  case FS_FAT_TYPE_FAT32:
    // through
  default:
    if ((Cluster & 0xFFFFFFFuL) == 0xFFFFFFFuL) {
      r = 1;
    }
    break;
  }
  return r;
}

/*********************************************************************
*
*       _SetFileLen
*/
static void _SetFileLen(FS_VOLUME * pVolume, FS_DIR_POS * pDirPos, U32 Size, FS_SB * pSB) {
  FS_FAT_DENTRY  * pDirEntry;

  pDirEntry = FS_FAT_GetDirEntry(pVolume, pSB, pDirPos);
  if (pDirEntry != NULL) {
    FS_StoreU32LE(&pDirEntry->Data[DIR_ENTRY_OFF_SIZE], Size);
    if (Size == 0u) {
      FS_FAT_WriteDirEntryCluster(pDirEntry, 0);
    }
    FS__SB_MarkDirty(pSB);
  }
}

/*********************************************************************
*
*       _ConvertLostClusterChain2File
*
*  Function description
*    Creates a new file and assigns the chain of lost clusters to it.
*
*  Return value
*    ==FS_CHECKDISK_RETVAL_OK       Cluster chain converted.
*    < 0                            Error code indicating the failure reason.
*/
static int _ConvertLostClusterChain2File(FS_VOLUME * pVolume, U32 DirStart, U32 FirstCluster, char * sFileName, FS_SB * pSB) {
  U32             NumClusters;
  U32             FileSize;
  U32             LastCluster;
  FS_FAT_INFO   * pFATInfo;
  FS_FAT_DENTRY * pDirEntry;
  U32             DateTime;
  int             r;
  FS_DIR_POS      DirPos;

  r = FS_CHECKDISK_RETVAL_OK;
  pFATInfo = &pVolume->FSInfo.FATInfo;
  FS_MEMSET(&DirPos, 0, sizeof(DirPos));
  LastCluster = FS_FAT_FindLastCluster(pVolume, pSB, FirstCluster, &NumClusters);
  //
  // Check if the last cluster in the chain otherwise it will be corrected.
  //
  if (LastCluster == 0u) {
    (void)FS_FAT_MarkClusterEOC(pVolume, pSB, LastCluster);
  }
  if (NumClusters == 0u) {
    (void)FS_FAT_MarkClusterEOC(pVolume, pSB, LastCluster);
  }
  FileSize = NumClusters << pFATInfo->ldBytesPerCluster;
  for (;;) {
    pDirEntry = FAT_pDirEntryAPI->pfFindDirEntry(pVolume, pSB, sFileName, (int)FS_STRLEN(sFileName), DirStart, &DirPos, 0, NULL);
    if (pDirEntry == NULL) {
      break;
    }
    _CreateFixFileName(sFileName);
  }
  //
  // Create the directory entry for the lost cluster chain.
  //
  DateTime = FS__GetTimeDate();
  pDirEntry = FAT_pDirEntryAPI->pfCreateDirEntry(pVolume, pSB, sFileName, DirStart, FirstCluster, FS_FAT_ATTR_ARCHIVE, FileSize, DateTime & 0xFFFFu, DateTime >> 16);
  if (pDirEntry == NULL) {
    FS_DEBUG_ERROROUT((FS_MTYPE_FS, "Failed to create directory entry, no space available."));
    r = FS_ERRCODE_WRITE_FAILURE;
  }
  return r;
}

/*********************************************************************
*
*       _CheckFileName
*
*  Function description
*    Checks if the name of a file contains valid characters.
*
*  Return value
*    FS_CHECKDISK_RETVAL_OK       OK
*    FS_CHECKDISK_RETVAL_RETRY    An error has be found and repaired, retry is required.
*    FS_CHECKDISK_RETVAL_ABORT    User specified an abort of disk checking operation through callback.
*/
static int _CheckFileName(FS_VOLUME * pVolume, FS_SB * pSB, FS_DIR_POS * pDirPos, U32 DirCluster, FS_CHECKDISK_ON_ERROR_CALLBACK * pfOnError) {
  FS_FAT_DENTRY * pDirEntry;
  int             r;

  pDirEntry = FS_FAT_GetDirEntry(pVolume, pSB, pDirPos);
  if (pDirEntry == NULL) {
    return FS_ERRCODE_READ_FAILURE;
  }
  r = FS_CHECKDISK_RETVAL_OK;
  if (_CheckShortName(pDirEntry) != 0) {
    char       acFileName[13];
    FS_83NAME  Name;
    FS_SB      sb;
    int        Action;

    Action = pfOnError(FS_CHECKDISK_ERRCODE_INVALID_DIRECTORY_ENTRY);
    switch (Action) {
    case FS_CHECKDISK_ACTION_DO_NOT_REPAIR:
      break;
    case FS_CHECKDISK_ACTION_SAVE_CLUSTERS:
      // through
    case FS_CHECKDISK_ACTION_DELETE_CLUSTERS:
      //
      // Assigned the file a generated name.
      //
      (void)FS__SB_Create(&sb, pVolume);
      for (;;) {
        FS_FAT_DENTRY * pDirEntryNew;

        _CreateFixFileName(acFileName);
        pDirEntryNew = FS_FAT_FindDirEntry(pVolume, &sb, acFileName, (int)FS_STRLEN(acFileName), DirCluster, 0, NULL);
        if (pDirEntryNew == (FS_FAT_DENTRY *)NULL) {
          break;
        }
      }
      FS__SB_Delete(&sb);
      (void)FS_FAT_MakeShortName(&Name, acFileName, (int)FS_STRLEN(acFileName), 0);
      FS_MEMCPY(pDirEntry->Data, Name.ac, sizeof(Name.ac));
      FS__SB_MarkDirty(pSB);
      r = FS_CHECKDISK_RETVAL_RETRY;
      break;
    default:
      r = FS_CHECKDISK_RETVAL_ABORT;
      break;
    }
  }
  return r;
}

/*********************************************************************
*
*       _GetFixDir
*
*  Function description
*    Creates or opens a directory to store the contents of the saved clusters.
*
*  Return value
*    Id of the first cluster allocated for the directory or CLUSTER_ID_INVALID on error.
*/
static U32 _GetFixDir(FS_VOLUME * pVolume, U32 DirStart, FS_SB * pSB, int UseSameDir) {
  char            acDirName[13];
  FS_FAT_DENTRY * pDirEntry;
  U32             r;

  if (UseSameDir == 0) {
    for (;;) {
      _LastDirIndex++;
      _CreateFixDirName(acDirName);
      pDirEntry = FS_FAT_FindDirEntry(pVolume, pSB, acDirName, (int)FS_STRLEN(acDirName), DirStart, 0, NULL);
      if (pDirEntry == NULL) {
        if (FS_FAT_CreateDirEx(pVolume, acDirName, DirStart, pSB) != 0) {
          return CLUSTER_ID_INVALID;    // Error, could not create directory.
        }
        break;
      }
    }
  } else {
    _CreateFixDirName(acDirName);
  }
  pDirEntry = FS_FAT_FindDirEntry(pVolume, pSB, acDirName, (int)FS_STRLEN(acDirName), DirStart, FS_FAT_ATTR_DIRECTORY, NULL);
  if (pDirEntry != NULL) {
    r = FS_FAT_GetFirstCluster(pDirEntry);
  } else {
    r = CLUSTER_ID_INVALID;             // Error, could not open directory.
  }
  return r;
}

/*********************************************************************
*
*       _CalcNumClustersUsed
*/
static U32 _CalcNumClustersUsed(FS_VOLUME * pVolume, U32 FileSize) {
  U32           NumClustersUsed;
  U32           BytesPerCluster;
  unsigned      ldBytesPerCluster;
  FS_FAT_INFO * pFATInfo;

  pFATInfo = &pVolume->FSInfo.FATInfo;
  BytesPerCluster   = pFATInfo->BytesPerCluster;
  ldBytesPerCluster = _ld(BytesPerCluster);
  NumClustersUsed   = FileSize >> ldBytesPerCluster;
  if ((FileSize & (BytesPerCluster - 1u)) != 0u) {
    ++NumClustersUsed;
  }
  return NumClustersUsed;
}

/*********************************************************************
*
*       _CheckFile
*
*  Return value
*    FS_CHECKDISK_RETVAL_OK       O.K.
*    FS_CHECKDISK_RETVAL_RETRY    An error has be found and repaired, retry is required.
*    FS_CHECKDISK_RETVAL_ABORT    User specified an abort of disk checking operation through callback.
*/
static int _CheckFile(FS_VOLUME                      * pVolume,
                      U32                              FirstFileCluster,
                      U32                              FileSize,
                      const FS_CLUSTER_MAP           * pClusterMap,
                      FS_SB                          * pSB,
                      U32                              DirCluster,
                      FS_DIR_POS                     * pDirPos,
                      FS_CHECKDISK_ON_ERROR_CALLBACK * pfOnError) {
  FS_FAT_INFO * pFATInfo;
  FS_SB         SBFat;
  FS_SB       * pSBFat;
  U32           NumClustersUsed;
  U32           Cluster;
  U32           LastCluster;
  int           r;
  int           Action;
  U32           i;
  int           Result;

  r = _CheckFileName(pVolume, pSB, pDirPos, DirCluster, pfOnError);
  if (r != FS_CHECKDISK_RETVAL_OK) {
    return r;
  }
  if (FS__SB_Create(&SBFat, pVolume) == 0) {
    pSBFat = &SBFat;
  } else {
    pSBFat = pSB;
  }
  if (FileSize == 0u) {
    if (FirstFileCluster == 0u) {
      r = FS_CHECKDISK_RETVAL_OK;
      goto Done;
    }
    Action = pfOnError(FS_CHECKDISK_ERRCODE_0FILE, FirstFileCluster);
    switch (Action) {
    case FS_CHECKDISK_ACTION_DO_NOT_REPAIR:
      break;
    case FS_CHECKDISK_ACTION_SAVE_CLUSTERS:
      // through
    case FS_CHECKDISK_ACTION_DELETE_CLUSTERS:
      //
      // Correct the directory entry (FirstCluster set to 0)
      // and free the cluster chain allocated to file.
      //
      _SetFileLen(pVolume, pDirPos, 0, pSB);
      (void)FS_FAT_FreeClusterChain(pVolume, pSBFat, FirstFileCluster, 0xFFFFFFFFuL);
      r = FS_CHECKDISK_RETVAL_RETRY;
      break;
    default:
      r = FS_CHECKDISK_RETVAL_ABORT;
      break;
    }
    if (r != FS_CHECKDISK_RETVAL_OK) {
      goto Done;
    }
  }
  r               = FS_CHECKDISK_RETVAL_OK;
  LastCluster     = 0;
  pFATInfo        = &pVolume->FSInfo.FATInfo;
  NumClustersUsed = _CalcNumClustersUsed(pVolume, FileSize);
  Cluster         = FirstFileCluster;
  i = 0;
  while (Cluster != 0u) {
    //
    // Check if max. size has been exceeded.
    //
    if (i > (NumClustersUsed - 1u)) {
      Action = pfOnError(FS_CHECKDISK_ERRCODE_SHORTEN_CLUSTER, Cluster);
      switch (Action) {
      case FS_CHECKDISK_ACTION_DO_NOT_REPAIR:
        break;
      case FS_CHECKDISK_ACTION_SAVE_CLUSTERS:
        // through
      case FS_CHECKDISK_ACTION_DELETE_CLUSTERS:
        (void)FS_FAT_MarkClusterEOC(pVolume, pSBFat, LastCluster);
        (void)FS_FAT_FreeClusterChain(pVolume, pSBFat, Cluster, 0xFFFFFFFFuL);
        r = FS_CHECKDISK_RETVAL_RETRY;
        break;
      default:
        r = FS_CHECKDISK_RETVAL_ABORT;
        break;
      }
      if (r != FS_CHECKDISK_RETVAL_OK) {
        goto Done;
      }
    }
    //
    // Save the cluster information and check if the cluster is used by an other file.
    //
    Result = _AddToClusterMap(pClusterMap, Cluster);
    if (Result == 1) {
      //
      // The cluster is used by another file/directory.
      //
      Action = pfOnError(FS_CHECKDISK_ERRCODE_CROSSLINKED_CLUSTER, Cluster, pDirPos->FirstClusterId, pDirPos->DirEntryIndex);
      switch (Action) {
      case FS_CHECKDISK_ACTION_DO_NOT_REPAIR:
        break;
      case FS_CHECKDISK_ACTION_SAVE_CLUSTERS:
        // through
      case FS_CHECKDISK_ACTION_DELETE_CLUSTERS:
        //
        // Truncate the file to 0 size. FS_CheckDisk() must be run again to repair
        // the clusters marked as in use but not allocated to any file.
        //
        _SetFileLen(pVolume, pDirPos, i * pFATInfo->BytesPerCluster, pSB);
        r = FS_CHECKDISK_RETVAL_RETRY;
        break;
      default:
        r = FS_CHECKDISK_RETVAL_ABORT;
        break;
      }
      if (r != FS_CHECKDISK_RETVAL_OK) {
        goto Done;
      }
    }
    //
    // Check if cluster is beyond the number of clusters
    //
    if ((Cluster - (U32)FAT_FIRST_CLUSTER) >= pVolume->FSInfo.FATInfo.NumClusters) {
      Action = pfOnError(FS_CHECKDISK_ERRCODE_INVALID_CLUSTER, Cluster);
      switch (Action) {
      case FS_CHECKDISK_ACTION_DO_NOT_REPAIR:
        break;
      case FS_CHECKDISK_ACTION_SAVE_CLUSTERS:
        // through
      case FS_CHECKDISK_ACTION_DELETE_CLUSTERS:
        if (FS_FAT_MarkClusterEOC(pVolume, pSBFat, LastCluster) != 0) {
          _SetFileLen(pVolume, pDirPos, 0, pSB);
        }
        r = FS_CHECKDISK_RETVAL_RETRY;
        break;
      default:
        r = FS_CHECKDISK_RETVAL_ABORT;
        break;
      }
      if (r != FS_CHECKDISK_RETVAL_OK) {
        goto Done;
      }
    }
    LastCluster = Cluster;
    Cluster = FS_FAT_WalkCluster(pVolume, pSBFat, Cluster, 1);
    //
    // Check if the last cluster is marked as end-of-chain.
    //
    if (i == (NumClustersUsed - 1u)) {
      if ((_IsClusterEOC(pVolume, pSB, LastCluster) == 0)) {
        Action = pfOnError(FS_CHECKDISK_ERRCODE_CLUSTER_NOT_EOC, LastCluster);
        switch (Action) {
        case FS_CHECKDISK_ACTION_DO_NOT_REPAIR:
          break;
        case FS_CHECKDISK_ACTION_SAVE_CLUSTERS:
          // through
        case FS_CHECKDISK_ACTION_DELETE_CLUSTERS:
          if (FS_FAT_MarkClusterEOC(pVolume, pSBFat, LastCluster) != 0) {
            _SetFileLen(pVolume, pDirPos, 0, pSB);
          }
          r = FS_CHECKDISK_RETVAL_RETRY;
          break;
        default:
          r = FS_CHECKDISK_RETVAL_ABORT;
          break;
        }
        if (r != FS_CHECKDISK_RETVAL_OK) {
          goto Done;
        }
      }
    }
    ++i;
  }
  if (i != NumClustersUsed) {
    Action = pfOnError(FS_CHECKDISK_ERRCODE_FEW_CLUSTER);
    switch (Action) {
    case FS_CHECKDISK_ACTION_DO_NOT_REPAIR:
      break;
    case FS_CHECKDISK_ACTION_SAVE_CLUSTERS:
      // through
    case FS_CHECKDISK_ACTION_DELETE_CLUSTERS:
      _SetFileLen(pVolume, pDirPos, i * pFATInfo->BytesPerCluster, pSB);
      r = FS_CHECKDISK_RETVAL_RETRY;
      break;
    default:
      r = FS_CHECKDISK_RETVAL_ABORT;
      break;
    }
  }
Done:
  FS__SB_Delete(&SBFat);
  return r;
}

/*********************************************************************
*
*       _IsDotFolderEntryValid
*
*  Return value
*   ==1   The entry is OK.
*   ==0   Invalid entry.
*/
static int _IsDotFolderEntryValid(const FS_FAT_DENTRY * pDirEntry, const char * sDirEntryName, U32 ClusterId) {
  if (!pDirEntry) {
    goto Done;
  }
  if (FS_MEMCMP(&pDirEntry->Data[0], sDirEntryName, 11) != 0) {
    goto Done;
  }
  if ((pDirEntry->Data[DIR_ENTRY_OFF_ATTRIBUTES] & FS_FAT_ATTR_DIRECTORY) == 0u) {
    goto Done;
  }
  if (FS_FAT_GetFirstCluster(pDirEntry) != ClusterId) {
    goto Done;
  }
  return 1;
Done:
  return 0;
}

/*********************************************************************
*
*       _CheckDir
*
*  Function description
*    Check for validity the contents of a directory.
*
*  Return value
*    FS_CHECKDISK_RETVAL_OK           O.K.
*    FS_CHECKDISK_RETVAL_RETRY        An error has be found and repaired, retry is required.
*    FS_CHECKDISK_RETVAL_MAX_RECURSE  User specified an abort of disk checking operation through callback.
*/
static int _CheckDir(FS_VOLUME                      * pVolume,
                     U32                              DirCluster,
                     FS_CLUSTER_MAP                 * pClusterMap,
                     int                              MaxRecursionLevel,
                     FS_SB                          * pSB,
                     FS_CHECKDISK_ON_ERROR_CALLBACK * pfOnError) {
  FS_DIR_POS      DirPos;
  FS_FAT_INFO   * pFATInfo;
  FS_FAT_DENTRY * pDirEntry;
  int             r;
  unsigned        Attributes;
  FS_DIR_POS      DirPos2Check;
  FS_FAT_DENTRY * pDirEntry2Check;
  U32             RootDirCluster;
  int             Action;
  int             IsRecursionError;
  U32             DirCluster2Check;
  int             CheckDir;
#if FS_FAT_CHECK_UNUSED_DIR_ENTRIES
  int             IsLastEntry;
#endif // FS_FAT_CHECK_UNUSED_DIR_ENTRIES

  pFATInfo         = &pVolume->FSInfo.FATInfo;
  IsRecursionError = 0;
#if FS_FAT_CHECK_UNUSED_DIR_ENTRIES
  IsLastEntry      = 0;
#endif // FS_FAT_CHECK_UNUSED_DIR_ENTRIES
  //
  // Iterate over directory entries.
  //
  RootDirCluster = DirCluster;
  FS_FAT_InitDirEntryScan(pFATInfo, &DirPos, DirCluster);
  r = FS_CHECKDISK_RETVAL_RETRY;
  for (;;) {
    pDirEntry = FS_FAT_GetDirEntry(pVolume, pSB, &DirPos);
    if (pDirEntry == NULL) {
      break;
    }
#if (FS_FAT_CHECK_UNUSED_DIR_ENTRIES == 0)
    if (pDirEntry->Data[0] == 0x00u) {                            // Last entry found?
      break;
    }
    CheckDir = 0;
    if (pDirEntry->Data[0] != DIR_ENTRY_INVALID_MARKER) {         // Not a deleted file?
      CheckDir = 1;
    }
#else
    CheckDir = 0;
    if (IsLastEntry != 0) {
      if (_IsEmptyDirEntry(pDirEntry) == 0) {
        r = _MarkDirEntryAsEmpty(pDirEntry, pSB, pfOnError);
        if (r != FS_CHECKDISK_RETVAL_OK) {
          return r;
        }
      }
    } else {
      if (pDirEntry->Data[0] == 0x00u) {                          // Last entry found?
        if (_IsEmptyDirEntry(pDirEntry) == 0) {
          r = _MarkDirEntryAsEmpty(pDirEntry, pSB, pfOnError);
          if (r != FS_CHECKDISK_RETVAL_OK) {
            return r;
          }
        }
        IsLastEntry = 1;
      } else {
        if (pDirEntry->Data[0] != DIR_ENTRY_INVALID_MARKER) {     // Not a deleted file?
          CheckDir = 1;
        }
      }
    }
#endif // FS_FAT_CHECK_UNUSED_DIR_ENTRIES
    if (CheckDir != 0) {
      //
      // Check the entry for validity.
      //
      if (_CheckDirEntry(pFATInfo, pDirEntry) != 0) {
        r = _MarkDirEntryAsDeleted(pDirEntry, pSB, pfOnError);
        if (r != FS_CHECKDISK_RETVAL_OK) {
          return r;
        }
      } else {
        //
        // Check if the directory entry has the directory attribute set.
        //
        Attributes = pDirEntry->Data[DIR_ENTRY_OFF_ATTRIBUTES];
        if ((Attributes & FS_FAT_ATTR_LONGNAME) != FS_FAT_ATTR_LONGNAME) {
          if ((Attributes & FS_FAT_ATTR_DIRECTORY) == FS_FAT_ATTR_DIRECTORY) {
            if (pDirEntry->Data[0] != (U8)'.') {
              DirCluster = FS_FAT_GetFirstCluster(pDirEntry);
              if (DirCluster != 0u) {           // Not the root directory?
                //
                // Check if cluster FAT entry is not empty.
                //
                if (FS_FAT_ReadFATEntry(pVolume, pSB, DirCluster) == 0u) {
                  FS_FAT_InitDirEntryScan(pFATInfo, &DirPos2Check, DirCluster);
                  pDirEntry2Check = FS_FAT_GetDirEntry(pVolume, pSB, &DirPos2Check);
                  if (pDirEntry2Check != NULL) {
                    //
                    // Check if the first directory entry in the child directory
                    // points to parent directory.
                    //
                    DirCluster2Check = FS_FAT_GetFirstCluster(pDirEntry2Check);
                    if ((pDirEntry2Check->Data[0] == (U8)'.') && (DirCluster2Check == DirCluster)) {
                      //
                      // Repair the FAT entry, since the child directory seems to be valid.
                      //
                      Action = pfOnError(FS_CHECKDISK_ERRCODE_INVALID_DIRECTORY_ENTRY);
                      switch (Action) {
                      case FS_CHECKDISK_ACTION_DO_NOT_REPAIR:
                        break;
                      case FS_CHECKDISK_ACTION_SAVE_CLUSTERS:
                        // through
                      case FS_CHECKDISK_ACTION_DELETE_CLUSTERS:
                        (void)FS_FAT_MarkClusterEOC(pVolume, pSB, DirCluster);
                        r = FS_CHECKDISK_RETVAL_RETRY;
                        break;
                      default:
                        r = FS_CHECKDISK_RETVAL_ABORT;
                        break;
                      }
                      if (r != FS_CHECKDISK_RETVAL_OK) {
                        return r;
                      }
                    } else {
                      //
                      // Retrieve the directory entry and mark it as deleted
                      // since the directory entry does not point to a valid directory.
                      //
                      pDirEntry = FS_FAT_GetDirEntry(pVolume, pSB, &DirPos);
                      r = _MarkDirEntryAsDeleted(pDirEntry, pSB, pfOnError);
                      return r;
                    }
                  }
                }
              }
              pDirEntry = FS_FAT_GetDirEntry(pVolume, pSB, &DirPos);
              if (pDirEntry != NULL) {
                if (FS_LoadU32LE(&pDirEntry->Data[DIR_ENTRY_OFF_SIZE]) != 0u) {
                  //
                  // Directory entry is not a valid directory. Convert it into a file.
                  //
                  Action = pfOnError(FS_CHECKDISK_ERRCODE_INVALID_DIRECTORY_ENTRY);
                  switch (Action) {
                  case FS_CHECKDISK_ACTION_DO_NOT_REPAIR:
                    break;
                  case FS_CHECKDISK_ACTION_SAVE_CLUSTERS:
                    // through
                  case FS_CHECKDISK_ACTION_DELETE_CLUSTERS:
                    Attributes = pDirEntry->Data[DIR_ENTRY_OFF_ATTRIBUTES];
                    Attributes &= ~FS_FAT_ATTR_DIRECTORY;
                    pDirEntry->Data[DIR_ENTRY_OFF_ATTRIBUTES] = (U8)Attributes;
                    FS__SB_MarkDirty(pSB);
                    r = FS_CHECKDISK_RETVAL_RETRY;
                    break;
                  default:
                    r = FS_CHECKDISK_RETVAL_ABORT;
                    break;
                  }
                  if (r != FS_CHECKDISK_RETVAL_OK) {
                    return r;
                  }
                }
              }
              //
              // If we have a directory and it is not a root directory.
              // We need to check whether there are '..' and '.' directory entries
              // otherwise we convert the directory into a file.
              //
              FS_FAT_InitDirEntryScan(pFATInfo, &DirPos2Check, DirCluster);
              pDirEntry2Check = FS_FAT_GetDirEntry(pVolume, pSB, &DirPos2Check);
              //
              // Check the "." entry.
              //
              r = _IsDotFolderEntryValid(pDirEntry2Check, ".          ", DirPos2Check.FirstClusterId);
              if (r == 0) {
                //
                // Invalid directory entry.
                //
                Action = pfOnError(FS_CHECKDISK_ERRCODE_INVALID_DIRECTORY_ENTRY);
                switch (Action) {
                case FS_CHECKDISK_ACTION_DO_NOT_REPAIR:
                  break;
                case FS_CHECKDISK_ACTION_SAVE_CLUSTERS:
                  // through
                case FS_CHECKDISK_ACTION_DELETE_CLUSTERS:
                  r = FS_ERRCODE_READ_FAILURE;
                  pDirEntry = FS_FAT_GetDirEntry(pVolume, pSB, &DirPos);
                  if (pDirEntry != NULL) {
                    Attributes = pDirEntry->Data[DIR_ENTRY_OFF_ATTRIBUTES];
                    Attributes &= ~FS_FAT_ATTR_DIRECTORY;
                    pDirEntry->Data[DIR_ENTRY_OFF_ATTRIBUTES] = (U8)Attributes;
                    FS__SB_MarkDirty(pSB);
                    r = FS_CHECKDISK_RETVAL_RETRY;
                  }
                  break;
                default:
                  r = FS_CHECKDISK_RETVAL_ABORT;
                  break;
                }
                if (r != FS_CHECKDISK_RETVAL_OK) {
                  return r;
                }
              }
              //
              // Check the ".." entry.
              //
              FS_FAT_IncDirPos(&DirPos2Check);
              pDirEntry2Check = FS_FAT_GetDirEntry(pVolume, pSB, &DirPos2Check);
              r = _IsDotFolderEntryValid(pDirEntry2Check, "..         ", RootDirCluster);
              if (r == 0) {
                //
                // Invalid directory entry.
                //
                Action = pfOnError(FS_CHECKDISK_ERRCODE_INVALID_DIRECTORY_ENTRY);
                switch (Action) {
                case FS_CHECKDISK_ACTION_DO_NOT_REPAIR:
                  break;
                case FS_CHECKDISK_ACTION_SAVE_CLUSTERS:
                  // through
                case FS_CHECKDISK_ACTION_DELETE_CLUSTERS:
                  r = FS_ERRCODE_READ_FAILURE;
                  pDirEntry = FS_FAT_GetDirEntry(pVolume, pSB, &DirPos);
                  if (pDirEntry != NULL) {
                    Attributes = pDirEntry->Data[DIR_ENTRY_OFF_ATTRIBUTES];
                    Attributes &= ~FS_FAT_ATTR_DIRECTORY;
                    pDirEntry->Data[DIR_ENTRY_OFF_ATTRIBUTES] = (U8)Attributes;
                    FS__SB_MarkDirty(pSB);
                    r = FS_CHECKDISK_RETVAL_RETRY;
                  }
                  break;
                default:
                  r = FS_CHECKDISK_RETVAL_ABORT;
                  break;
                }
                if (r != FS_CHECKDISK_RETVAL_OK) {
                  return r;
                }
              }
              //
              // Add it to the cluster info array.
              //
              (void)_AddToClusterMap(pClusterMap, DirCluster);
              if (MaxRecursionLevel != 0) {
                r = _CheckDir(pVolume, DirCluster, pClusterMap, MaxRecursionLevel - 1, pSB, pfOnError);
                if (r != FS_CHECKDISK_RETVAL_OK) {
                  return r;
                }
              } else {
                IsRecursionError = 1;                             // Maximum recursion level limit reached. Remember the error and continue the checking.
              }
            }
          } else {
            U32 FileSize;
            U32 FirstFileCluster;

            FirstFileCluster = FS_FAT_GetFirstCluster(pDirEntry);
            FileSize         = FS_LoadU32LE(&pDirEntry->Data[DIR_ENTRY_OFF_SIZE]);
            r = _CheckFile(pVolume, FirstFileCluster, FileSize, pClusterMap, pSB, DirCluster, &DirPos, pfOnError);
            if (r != FS_CHECKDISK_RETVAL_OK) {
              return r;
            }
          }
        } else {
          //
          // Check Long file name entry.
          //
          r = _CheckLongDirEntry(pVolume, &DirPos, pDirEntry, pSB, pfOnError);
          if (r != FS_CHECKDISK_RETVAL_OK) {
            return r;
          }
        }
      }
    }
    FS_FAT_IncDirPos(&DirPos);
    //
    // Moved to another cluster to check the directory entries ?
    //
    if (DirPos.ClusterId != 0u) {
      if (DirPos.ClusterId != DirCluster) {
        if (FS_FAT_ReadFATEntry(pVolume, pSB, DirPos.ClusterId) == 0u) {
          //
          // Repair the FAT entry, since the directory is valid.
          //
          Action = pfOnError(FS_CHECKDISK_ERRCODE_CLUSTER_NOT_EOC);
          switch (Action) {
          case FS_CHECKDISK_ACTION_DO_NOT_REPAIR:
            break;
          case FS_CHECKDISK_ACTION_SAVE_CLUSTERS:
            // through
          case FS_CHECKDISK_ACTION_DELETE_CLUSTERS:
            (void)FS_FAT_MarkClusterEOC(pVolume, pSB, DirCluster);
            r = FS_CHECKDISK_RETVAL_RETRY;
            break;
          default:
            r = FS_CHECKDISK_RETVAL_ABORT;
            break;
          }
          if (r != FS_CHECKDISK_RETVAL_OK) {
            return r;
          }
        }
        //
        // Add it to the cluster info array.
        //
        (void)_AddToClusterMap(pClusterMap, DirPos.ClusterId);
      }
    }
  }
  if (IsRecursionError != 0) {
    return FS_CHECKDISK_RETVAL_MAX_RECURSE;       // Deepest directory level reached.
  }
  return FS_CHECKDISK_RETVAL_OK;
}

/*********************************************************************
*
*       _CheckAT
*/
static int _CheckAT(FS_VOLUME * pVolume, U32 DirStart, const FS_CLUSTER_MAP * pClusterMap, FS_SB * pSB, FS_CHECKDISK_ON_ERROR_CALLBACK * pfOnError) {
  I32             i;
  int             r;
  int             Action;
  U32             FATEntry;
  U32             FirstCluster;
  U32             LastCluster;
  U32             NumClusters;
  FS_DIR_POS      DirPos;
  char            acFileName[13];
  FS_FAT_DENTRY * pDirEntry;
  U32             FileSize;
  U32             FixDirStart;
  int             Len;

  r = FS_CHECKDISK_RETVAL_OK;
  //
  // Check the all the entries in the allocation table one by one.
  //
  for (i = 0; i < pClusterMap->NumClusters; i++) {
    if (_IsClusterFree(pClusterMap, (U32)i) != 0) {
      FATEntry = FS_FAT_ReadFATEntry(pVolume, pSB, (U32)i + pClusterMap->FirstClusterId);
      if (FATEntry != 0u) {
        Action = pfOnError(FS_CHECKDISK_ERRCODE_CLUSTER_UNUSED, (U32)i + pClusterMap->FirstClusterId);
        switch (Action) {
        case FS_CHECKDISK_ACTION_DO_NOT_REPAIR:
          break;
        case FS_CHECKDISK_ACTION_SAVE_CLUSTERS:
          FixDirStart = _GetFixDir(pVolume, 0, pSB, _UseSameDir);
          if (FixDirStart == CLUSTER_ID_INVALID) {
            return FS_ERRCODE_CLUSTER_NOT_FREE;   // Error, could not open directory.
          }
          _UseSameDir = 1;                        // The cluster contents should be saved in the same directory on the next call to FS_CheckDisk().
          _CreateFixFileName(acFileName);
          r = _ConvertLostClusterChain2File(pVolume, FixDirStart, (U32)i + pClusterMap->FirstClusterId, acFileName, pSB);
          if (r != FS_CHECKDISK_RETVAL_OK) {
            return r;
          }
          //
          // Check the newly created file for validity.
          //
          FS_FAT_InitDirEntryScan(&pVolume->FSInfo.FATInfo, &DirPos, FixDirStart);
          Len = (int)FS_STRLEN(acFileName);
          pDirEntry = FS_FAT_FindDirEntryShortEx(pVolume, pSB, acFileName, Len, &DirPos, 0);
          if (pDirEntry == NULL) {
            return FS_ERRCODE_FILE_DIR_NOT_FOUND;
          }
          FileSize     = FS_LoadU32LE(&pDirEntry->Data[DIR_ENTRY_OFF_SIZE]);
          FirstCluster = (U32)i + pClusterMap->FirstClusterId;
          //
          // Invalidate the entry since we have created a new file.
          //
          _MarkClusterAsFree(pClusterMap, (U32)i);
          r = _CheckFile(pVolume, FirstCluster, FileSize, pClusterMap, pSB, DirStart, &DirPos, pfOnError);
          if (r == FS_CHECKDISK_RETVAL_ABORT) {
            return r;
          }
          r = FS_CHECKDISK_RETVAL_RETRY;
          break;
        case FS_CHECKDISK_ACTION_DELETE_CLUSTERS:
          FirstCluster = (U32)i + pClusterMap->FirstClusterId;
          LastCluster  = FS_FAT_FindLastCluster(pVolume, pSB, FirstCluster, &NumClusters);
          //
          // Check if the last cluster in the chain otherwise it will be corrected.
          //
          if (LastCluster == 0u) {
            (void)FS_FAT_MarkClusterEOC(pVolume, pSB, LastCluster);
          }
          if (NumClusters == 0u) {
            (void)FS_FAT_MarkClusterEOC(pVolume, pSB, LastCluster);
          }
          if (_IsClusterEOC(pVolume, pSB, LastCluster) == 0) {
            (void)FS_FAT_MarkClusterEOC(pVolume, pSB, LastCluster);
          }
          (void)FS_FAT_FreeClusterChain(pVolume, pSB, FirstCluster, NumClusters);
          r = FS_CHECKDISK_RETVAL_RETRY;
          break;
        default:
          r = FS_CHECKDISK_RETVAL_ABORT;
          break;
        }
        if (r != FS_CHECKDISK_RETVAL_OK) {
          return r;
        }
      }
    }
  }
#if FS_FAT_CHECK_SECTOR_USAGE
  {
    unsigned      SectorsPerCluster;
    U32           iSector;
    U32           SectorIndex;
    U32           NumSectors;
    unsigned      FATType;
    U32           NumSectorsReserved;
    U32           NumSectorsAT;
    U32           NumRootDirEntries;
    unsigned      BytesPerSector;
    FS_FAT_INFO * pFATInfo;
    U32           ClusterId;

    pFATInfo           = &pVolume->FSInfo.FATInfo;
    SectorsPerCluster  = pFATInfo->SectorsPerCluster;
    FATType            = pFATInfo->FATType;
    NumSectorsReserved = pFATInfo->RsvdSecCnt;
    NumSectorsAT       = pFATInfo->FATSize * (U32)FAT_NUM_ALLOC_TABLES;
    NumRootDirEntries  = pFATInfo->RootEntCnt;
    BytesPerSector     = pFATInfo->BytesPerSector;
    //
    // Check the boot sector.
    //
    r = _RefreshSectorIfRequired(pVolume, SECTOR_INDEX_BPB, FS_SECTOR_TYPE_MAN, pSB, pfOnError);
    if (r != FS_CHECKDISK_RETVAL_OK) {
      return r;
    }
#if FS_FAT_SUPPORT_FAT32
    if(FATType == FS_FAT_TYPE_FAT32) {
      //
      // Check the backup boot sector.
      //
      r = _RefreshSectorIfRequired(pVolume, SECTOR_INDEX_BPB_BACKUP, FS_SECTOR_TYPE_MAN, pSB, pfOnError);
      if (r != FS_CHECKDISK_RETVAL_OK) {
        return r;
      }
      //
      // Check the FSInfo sector.
      //
      r = _RefreshSectorIfRequired(pVolume, SECTOR_INDEX_FSINFO, FS_SECTOR_TYPE_MAN, pSB, pfOnError);
      if (r != FS_CHECKDISK_RETVAL_OK) {
        return r;
      }
      //
      // Check the backup FSInfo sector.
      //
      r = _RefreshSectorIfRequired(pVolume, SECTOR_INDEX_FSINFO_BACKUP, FS_SECTOR_TYPE_MAN, pSB, pfOnError);
      if (r != FS_CHECKDISK_RETVAL_OK) {
        return r;
      }
    }
#endif  // FS_FAT_SUPPORT_FAT32
    //
    // Check the allocation table.
    //
    SectorIndex = NumSectorsReserved;
    for (iSector = 0; iSector < NumSectorsAT; ++iSector) {
      r = _RefreshSectorIfRequired(pVolume, SectorIndex++, FS_SECTOR_TYPE_MAN, pSB, pfOnError);
      if (r != FS_CHECKDISK_RETVAL_OK) {
        return r;
      }
    }
    //
    // Check the root directory for FAT12 and FAT16.
    // The root directory of FAT32 is checked below.
    //
    if (FATType != FS_FAT_TYPE_FAT32) {
      SectorIndex = NumSectorsReserved + NumSectorsAT;
      NumSectors  = (((U32)NumRootDirEntries * 32u + (U32)BytesPerSector - 1u) / (U32)BytesPerSector);
      for (iSector = 0; iSector < NumSectors; ++iSector) {
        r = _RefreshSectorIfRequired(pVolume, SectorIndex++, FS_SECTOR_TYPE_MAN, pSB, pfOnError);
        if (r != FS_CHECKDISK_RETVAL_OK) {
          return r;
        }
      }
    }
    //
    // Check the data area of the FAT partition.
    //
    for (i = 0; i < pClusterMap->NumClusters; i++) {
      if (_IsClusterFree(pClusterMap, (U32)i) == 0) {
        ClusterId   = (U32)i + pClusterMap->FirstClusterId;
        SectorIndex = FS_FAT_ClusterId2SectorNo(pFATInfo, ClusterId);
        for (iSector = 0; iSector < SectorsPerCluster; ++iSector) {
          //
          // The sector type is not necessarily correct here, but there is no easy
          // way to figure out whether the sector stores file data or directory entries.
          // The functionality is not affected by this, only the statistical counters.
          //
          r = _RefreshSectorIfRequired(pVolume, SectorIndex++, FS_SECTOR_TYPE_DATA, pSB, pfOnError);
          if (r != FS_CHECKDISK_RETVAL_OK) {
            return r;
          }
        }
      }
    }
  }
#endif    // FS_FAT_CHECK_SECTOR_USAGE
  return FS_CHECKDISK_RETVAL_OK;        // OK, no errors detected.
}

/*********************************************************************
*
*       _AddRootDirClusters
*
*  Function description
*    Marks as in use the clusters assigned to the root directory.
*/
static int _AddRootDirClusters(FS_VOLUME * pVolume, const FS_CLUSTER_MAP * pClusterMap, FS_SB * pSB, FS_CHECKDISK_ON_ERROR_CALLBACK * pfOnError) {
  int             r;
  int             Result;
  U32             ClusterIdPrev;
  U32             NumClusters;
  FS_FAT_INFO   * pFATInfo;
  FS_DIR_POS      DirPos;
  FS_FAT_DENTRY * pDirEntry;
  int             AddCluster;
  U32             ClusterId;
  int             Action;

  r             = FS_CHECKDISK_RETVAL_OK;       // Set to indicate success.
  ClusterIdPrev = 0;                            // Set to an invalid value.
  NumClusters   = 0;
  pFATInfo      = &pVolume->FSInfo.FATInfo;
  ClusterId     = pFATInfo->RootDirPos;         // Get the first cluster assigned to root directory.
  //
  // Add the clusters one by one.
  //
  while (ClusterId != 0u) {
    AddCluster = 0;
    if (NumClusters == 0u) {
      AddCluster = 1;             // The first cluster is always added since _CheckDir() does not do it for the root cluster.
    } else {
      //
      // Add only clusters that are empty, i.e. clusters never stored a directory entry.
      // This situation can occur if the root directory has been enlarged via FS_FAT_GrowRootDir().
      //
      FS_MEMSET(&DirPos, 0, sizeof(DirPos));
      FS_FAT_InitDirEntryScan(pFATInfo, &DirPos, ClusterId);
      pDirEntry = FS_FAT_GetDirEntry(pVolume, pSB, &DirPos);
      if (pDirEntry != NULL) {
        if (pDirEntry->Data[0] == 0x00u) {   // Is this the last entry?
          AddCluster = 1;
        }
      }
    }
    //
    // Mark the cluster as in use.
    //
    Result = 0;
    if (AddCluster != 0) {
      Result = _AddToClusterMap(pClusterMap, ClusterId);
    }
    if (Result == 1) {
      //
      // The cluster is used by another file/directory.
      //
      Action = pfOnError(FS_CHECKDISK_ERRCODE_CROSSLINKED_CLUSTER, ClusterId, 0, 0);
      switch (Action) {
      case FS_CHECKDISK_ACTION_DO_NOT_REPAIR:
        break;
      case FS_CHECKDISK_ACTION_SAVE_CLUSTERS:
        // through
      case FS_CHECKDISK_ACTION_DELETE_CLUSTERS:
        //
        // Cut the cluster chain. FS_CheckDisk() has to be run again to repair
        // the clusters marked as in use but not allocated to any file.
        //
        if (ClusterIdPrev != 0u) {
          (void)FS_FAT_MarkClusterEOC(pVolume, pSB, ClusterIdPrev);
        }
        r = FS_CHECKDISK_RETVAL_RETRY;
        break;
      default:
        r = FS_CHECKDISK_RETVAL_ABORT;
        break;
      }
    }
    if (r != FS_CHECKDISK_RETVAL_OK) {
      break;
    }
    //
    // Advance to next cluster.
    //
    ClusterIdPrev = ClusterId;
    ClusterId     = FS_FAT_WalkCluster(pVolume, pSB, ClusterId, 1);
    //
    // Safety check to make sure that we exit the processing loop
    // in case of an cyclic cluster chain.
    //
    ++NumClusters;
    if (NumClusters >= pFATInfo->NumClusters) {
      r = FS_ERRCODE_INVALID_CLUSTER_CHAIN;     // Error, too many clusters in the chain.
      break;
    }
  }
  return r;
}

/*********************************************************************
*
*       _CheckVolume
*
*  Function description
*    Checks the entire volume for errors.
*
*  Return value
*    ==FS_CHECKDISK_RETVAL_OK             O.K.
*    ==FS_CHECKDISK_RETVAL_RETRY          An error has be found and repaired, retry is required.
*    ==FS_CHECKDISK_RETVAL_MAX_RECURSE    User specified an abort of disk checking operation through callback.
*    0 <                                  Error code indicating the failure reason.
*/
static int _CheckVolume(FS_VOLUME * pVolume, FS_CLUSTER_MAP * pClusterMap, int MaxRecursionLevel, FS_SB * pSB, FS_CHECKDISK_ON_ERROR_CALLBACK * pfOnError) {
  U32            DirStart;
  const char   * sFileName;
  FS_FAT_INFO  * pFATInfo;
  int            r;

  //
  // Search for the root directory.
  //
  r = FS_FAT_FindPath(pVolume, pSB, "", &sFileName, &DirStart);
  if (r == 0) {
    return FS_ERRCODE_PATH_NOT_FOUND;     // Root directory not found.
  }
  _InitClusterMap(pClusterMap);
  pFATInfo = &pVolume->FSInfo.FATInfo;
  if (pFATInfo->FATType == FS_FAT_TYPE_FAT32) {
    if (DirStart == 0u) {
      r = _AddRootDirClusters(pVolume, pClusterMap, pSB, pfOnError);
      if (r != FS_CHECKDISK_RETVAL_OK) {
        return r;                         // Error wile adding the clusters of the root directory.
      }
    }
  }
  //
  // Check all the files and directories.
  //
  r = _CheckDir(pVolume, DirStart, pClusterMap, MaxRecursionLevel, pSB, pfOnError);
  if (r != 0) {
    return r;
  }
  //
  // Check the allocation table.
  //
  r = _CheckAT(pVolume, DirStart, pClusterMap, pSB, pfOnError);
  return r;
}

/*********************************************************************
*
*       Public code (internal)
*
**********************************************************************
*/

#if FS_SUPPORT_FAT

/*********************************************************************
*
*       FS_FAT_CHECKDISK_Init
*/
void FS_FAT_CHECKDISK_Init(void) {
  _FileIndex      = 0;
  _LastDirIndex   = -1;
  _UseSameDir     = 0;
  _AbortRequested = 0;
}

/*********************************************************************
*
*       FS_FAT_CHECKDISK_Save
*/
void FS_FAT_CHECKDISK_Save(FS_CONTEXT * pContext) {
  pContext->FAT_CHECKDISK_FileIndex      = _FileIndex;
  pContext->FAT_CHECKDISK_LastDirIndex   = _LastDirIndex;
  pContext->FAT_CHECKDISK_UseSameDir     = _UseSameDir;
  pContext->FAT_CHECKDISK_AbortRequested = _AbortRequested;
}

/*********************************************************************
*
*       FS_FAT_CHECKDISK_Restore
*/
void FS_FAT_CHECKDISK_Restore(const FS_CONTEXT * pContext) {
  _FileIndex      = pContext->FAT_CHECKDISK_FileIndex;
  _LastDirIndex   = pContext->FAT_CHECKDISK_LastDirIndex;
  _UseSameDir     = pContext->FAT_CHECKDISK_UseSameDir;
  _AbortRequested = pContext->FAT_CHECKDISK_AbortRequested;
}

#endif // FS_SUPPORT_FAT

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_FAT_CheckVolume
*
*  Function description
*    Checks the consistency of an entire volume.
*
*  Parameters
*    pVolume            Volume instance.
*    pBuffer            Working buffer.
*    BufferSize         Size of pBuffer in bytes.
*    MaxRecursionLevel  Deepest directory level to be checked.
*    pfOnError          Callback function to be invoked on error. NULL is not permitted.
*
*  Return value
*    ==FS_CHECKDISK_RETVAL_OK           OK, no errors detected.
*    ==FS_CHECKDISK_RETVAL_RETRY        An error has be found and repaired, retry is required.
*    ==FS_CHECKDISK_RETVAL_ABORT        User specified an abort of disk checking operation.
*    ==FS_CHECKDISK_RETVAL_MAX_RECURSE  Max recursion level reached, operation aborted.
*    < 0                                Error code indicating the failure reason.
*/
int FS_FAT_CheckVolume(FS_VOLUME * pVolume, void * pBuffer, U32 BufferSize, int MaxRecursionLevel, FS_CHECKDISK_ON_ERROR_CALLBACK * pfOnError) {
  U32              NumClusters;
  U32              iCluster;
  U32              NumClustersAtOnce;
  FS_SB            sb;
  int              r;
  FS_CLUSTER_MAP   ClusterMap;
  FS_FAT_INFO    * pFATInfo;

  r = FS_CHECKDISK_RETVAL_OK;
  pFATInfo = &pVolume->FSInfo.FATInfo;
  FS_MEMSET(&ClusterMap, 0, sizeof(ClusterMap));
  (void)FS__SB_Create(&sb, pVolume);
  NumClusters       = pFATInfo->NumClusters;
  NumClustersAtOnce = BufferSize << 3;
  if (NumClustersAtOnce > NumClusters) {
    NumClustersAtOnce = NumClusters;
  }
  if (NumClustersAtOnce == 0u) {
    r = FS_ERRCODE_BUFFER_TOO_SMALL;                                                // Error, buffer not large enough.
  } else {
    _AbortRequested = 0;
    for (iCluster = FAT_FIRST_CLUSTER; NumClusters != 0u;) {
      ClusterMap.pData          = SEGGER_PTR2PTR(U8, pBuffer);                      // MISRA deviation D:100[d]
      ClusterMap.FirstClusterId = iCluster;
      ClusterMap.NumClusters    = (I32)NumClustersAtOnce;
      r = _CheckVolume(pVolume, &ClusterMap, MaxRecursionLevel, &sb, pfOnError);
      if (_AbortRequested != 0) {
        _AbortRequested = 0;                                                        // Mark the request as processed.
        r = FS_CHECKDISK_RETVAL_ABORT;                                              // Abort the operation.
      }
      if (r != FS_CHECKDISK_RETVAL_OK) {
        break;
      }
      iCluster          += NumClustersAtOnce;
      NumClusters       -= NumClustersAtOnce;
      NumClustersAtOnce  = SEGGER_MIN(NumClustersAtOnce, NumClusters);
    }
  }
  FS__SB_Delete(&sb);
  if (FS__SB_GetError(&sb) != 0) {
    r = FS_ERRCODE_WRITE_FAILURE;         // Error, write failed.
  }
  if (r == FS_CHECKDISK_RETVAL_OK) {
    _UseSameDir = 0;
  }
  return r;
}

/*********************************************************************
*
*       FS_FAT_AbortCheckDisk
*
*  Function description
*    Requests FS_CheckDisk() to abort operation. It has to be called
*    from a different task than the task that called FS_CheckDisk().
*/
void FS_FAT_AbortCheckDisk(void) {
  _AbortRequested = 1;
}

/*********************************************************************
*
*       FS_FAT_CheckDir
*
*  Function description
*    Checks the consistency of one directory.
*
*  Parameters
*    pVolume            Volume instance.
*    sPath              Path to the directory to be checked.
*    pClusterMap        Information about the usage of clusters.
*    pfOnError          Callback function to be invoked on error. NULL is not permitted.
*
*  Return value
*    ==FS_CHECKDISK_RETVAL_OK       OK, directory is not corrupted.
*    ==FS_CHECKDISK_RETVAL_RETRY    An error has be found and repaired, retry is required.
*    ==FS_CHECKDISK_RETVAL_ABORT    User requested the operation to be stopped.
*    < 0                            Error code indicating the failure reason.
*/
int FS_FAT_CheckDir(FS_VOLUME * pVolume, const char * sPath, FS_CLUSTER_MAP * pClusterMap, FS_CHECKDISK_ON_ERROR_CALLBACK * pfOnError){
  int             r;
  FS_SB           sb;
  U32             DirStart;
  const char    * sFileName;
  FS_FAT_INFO   * pFATInfo;
  FS_FAT_DENTRY * pDirEntry;
  int             IsDirCheckAllowed;
  int             Result;

  r                 = FS_ERRCODE_PATH_NOT_FOUND;    // Set to indicate an error.
  IsDirCheckAllowed = 1;
  //
  // Allocate a sector buffer.
  //
  (void)FS__SB_Create(&sb, pVolume);
  //
  // Search for the directory.
  //
  pDirEntry = NULL;
  Result = FS_FAT_FindPathEx(pVolume, &sb, sPath, &sFileName, &DirStart, &pDirEntry, CLUSTER_ID_INVALID);
  if (Result != 0) {
    //
    // Mark as in-use the clusters assigned to root directory.
    //
    pFATInfo = &pVolume->FSInfo.FATInfo;
    if (Result == 1) {                              // Is the root directory?
      if (pFATInfo->FATType == FS_FAT_TYPE_FAT32) {
        r = _AddRootDirClusters(pVolume, pClusterMap, &sb, pfOnError);
        if (r != FS_CHECKDISK_RETVAL_OK) {
          goto Done;                                // Error wile adding the clusters of the root directory.
        }
      }
    }
    //
    // Do not descent into invalid directory entries.
    // The directory entry is normally checked for validity
    // during the check of the parent directory but
    // since we do not know if the parent directory has
    // already been checked we have to do it here again.
    //
    if ((Result > 1) && pDirEntry) {                // Is a subdirectory?
      if (_CheckDirEntry(pFATInfo, pDirEntry) != 0) {
        r = _MarkDirEntryAsDeleted(pDirEntry, &sb, pfOnError);
        if (r == FS_CHECKDISK_RETVAL_OK) {
          r = FS_CHECKDISK_RETVAL_SKIP;
        }
        IsDirCheckAllowed = 0;                      // Do not recurse in this directory since it is invalid.
      }
    }
    if (IsDirCheckAllowed != 0) {
      //
      // OK, directory found. Perform the checking.
      //
      r = _CheckDir(pVolume, DirStart, pClusterMap, 0, &sb, pfOnError);     // 0 since we do not want to check subdirectories.
      if (r == FS_CHECKDISK_RETVAL_MAX_RECURSE) {                           // This is not and error but just an indication that the function found a subdirectory.
        r = FS_CHECKDISK_RETVAL_OK;
      }
    }
  }
Done:
  //
  // Free the sector buffer.
  //
  FS__SB_Delete(&sb);
  if (FS__SB_GetError(&sb) != 0) {
    r = FS_ERRCODE_WRITE_FAILURE;                   // Error, could not access the storage device.
  }
  return r;
}

/*********************************************************************
*
*       FS_FAT_CheckAT
*
*  Function description
*    Checks the consistency of the allocation table.
*
*  Parameters
*    pVolume            Volume instance.
*    pClusterMap        Information about the usage of clusters.
*    pfOnError          Callback function to be invoked on error. NULL is not permitted.
*
*  Return value
*    ==FS_CHECKDISK_RETVAL_OK         OK, the allocation table is not corrupted.
*    ==FS_CHECKDISK_RETVAL_RETRY      An error has be found and repaired, retry is required.
*    ==FS_CHECKDISK_RETVAL_ABORT      User requested the operation to be stopped.
*    < 0                              Error code indicating the failure reason.
*/
int FS_FAT_CheckAT(FS_VOLUME * pVolume, const FS_CLUSTER_MAP * pClusterMap, FS_CHECKDISK_ON_ERROR_CALLBACK * pfOnError){
  int          r;
  FS_SB        sb;
  U32          DirStart;
  const char * sFileName;
  int          Result;

  r = FS_ERRCODE_PATH_NOT_FOUND;                                          // Set to indicate an error.
  //
  // Allocate a sector buffer.
  //
  (void)FS__SB_Create(&sb, pVolume);
  //
  // Search for the root directory and add the used clusters to the cluster map.
  //
  Result = FS_FAT_FindPath(pVolume, &sb, "", &sFileName, &DirStart);
  if (Result != 0) {
    r = _CheckAT(pVolume, DirStart, pClusterMap, &sb, pfOnError);
  }
  //
  // Free the sector buffer.
  //
  FS__SB_Delete(&sb);
  if (FS__SB_GetError(&sb) != 0) {
    r = FS_ERRCODE_WRITE_FAILURE;                                         // Error, could not access the storage device.
  }
  if (r == FS_CHECKDISK_RETVAL_OK) {
    _UseSameDir = 0;
  }
  return r;
}

/*************************** End of file ****************************/

