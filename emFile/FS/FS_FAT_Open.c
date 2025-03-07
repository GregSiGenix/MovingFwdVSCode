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
File        : FS_FAT_Open.c
Purpose     : FAT routines for open/delete files
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#include "FS_FAT_Int.h"

/*********************************************************************
*
*       Public code (internal)
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_FAT_IsFileOpen
*
*  Function description
*    Checks if a file is already opened.
*
*  Parameters
*    pVolume        Volume on which the file is located.
*    SectorIndex    Index of the sector that stores the directory entry of the file.
*    DirEntryIndex  Position of the directory entry in the parent directory.
*
*  Return value
*    ==0    The file is not opened.
*    !=0    The file is opened.
*/
int FS_FAT_IsFileOpen(const FS_VOLUME * pVolume, U32 SectorIndex, U32 DirEntryIndex) {
  FS_FILE                   * pFile;
  FS_FILE_OBJ               * pFileObj;
  int                         r;
  const FS_DIRENTRY_POS_FAT * pDirEntryPos;

  r = 0;                  // Set to indicate that the file is not opened.
  FS_LOCK_SYS();
  pFile = FS_Global.pFirstFileHandle;
  while (pFile != NULL) {
    pFileObj = pFile->pFileObj;
    if (pFileObj != NULL) {
      if (pFile->InUse != 0u) {
        if (pFileObj->pVolume == pVolume) {
          pDirEntryPos = &pFileObj->DirEntryPos.fat;
          if (pDirEntryPos->SectorIndex == SectorIndex) {
            if ((U32)pDirEntryPos->DirEntryIndex == DirEntryIndex) {
              r = 1;      // The file is already opened.
              break;
            }
          }
        }
      }
    }
    pFile = pFile->pNext;
  }
  FS_UNLOCK_SYS();
  return r;
}

/*********************************************************************
*
*       FS_FAT_StoreShortNamePartial
*
*  Function description
*    Stores the name of the file to a directory entry.
*
*  Parameters
*    pShortName     [OUT] Short file name in the format expected on the storage.
*    pName          [IN] File name to be stored.
*    NumBytes       Number of bytes to be initialized in the short name.
*    EndPos         Number of bytes to copy. Can be negative in which
*                   case no bytes are copied at all.
*
*  Additional information
*    Unused characters are set to the space character (0x20, ' ').
*    All the lower-case letters are converted to upper case.
*    This function works only for single-byte character sets.
*/
void FS_FAT_StoreShortNamePartial(U8 * pShortName, const U8 * pName, unsigned NumBytes, int EndPos) {
  int i;
  U8  Byte;

  FS_MEMSET(pShortName, (int)' ', NumBytes);      // Fill with spaces.
  for (i = 0; i < EndPos; i++) {
    Byte          = *pName++;
    Byte          = (U8)FS_pCharSetType->pfToUpper((FS_WCHAR)Byte);
    *pShortName++ = Byte;
  }
}

#if FS_SUPPORT_MBCS

/*********************************************************************
*
*       FS_FAT_StoreShortNamePartialMB
*
*  Function description
*    Stores the name of the file to a directory entry.
*
*  Parameters
*    pShortName     [OUT] Short file name in the format expected on the storage.
*    pName          [IN] File name to be stored.
*    NumBytes       Number of bytes to be initialized in the short name.
*    EndPos         Number of bytes to copy. Can be negative in which
*                   case no bytes are copied at all.
*
*  Additional information
*    Unused characters are set to the space character (0x20, ' ').
*    All the lower-case letters are converted to upper case.
*    This function works only for single-byte as well as multi-byte
*    character sets.
*
*  Notes
*    (2) The number of bytes to be available in pName is unknown
*        but we know that there are sufficiently enough bytes in
*        the character sequence.
*    (2) We do not have for check for errors because we know that
*        the character sequence is valid.
*/
void FS_FAT_StoreShortNamePartialMB(U8 * pShortName, const U8 * pName, unsigned NumBytes, int EndPos) {
  int      i;
  FS_WCHAR Char;
  unsigned NumBytesRead;

  FS_MEMSET(pShortName, (int)' ', NumBytes);      // Fill with spaces.
  i = 0;
  for (;;) {
    if (i >= EndPos) {
      break;
    }
    NumBytesRead = 0;
    Char = FS_pCharSetType->pfGetChar(pName, FS_WCHAR_MAX_SIZE, &NumBytesRead);   // Note 1, 2
    Char = FS_pCharSetType->pfToUpper(Char);
    if (NumBytesRead == 2u) {
      *pShortName++ = (U8)(Char >> 8);
      *pShortName++ = (U8)Char;
    } else {
      *pShortName++ = (U8)Char;
    }
    i     += (int)NumBytesRead;
    pName += NumBytesRead;
  }
}

/*********************************************************************
*
*       FS_FAT_StoreShortNameCompleteMB
*
*  Function description
*    Stores the name of the file to a directory entry.
*
*  Notes
*    (2) The number of bytes to be available in pName is unknown
*        but we know that there are sufficiently enough bytes in
*        the character sequence.
*/
void FS_FAT_StoreShortNameCompleteMB(FS_83NAME * pShortName, const U8 * pName, unsigned NumBytes, unsigned ExtPos) {
  unsigned NumBytesRead;
  FS_WCHAR Char;

  if (*pName == DIR_ENTRY_INVALID_MARKER) {
    //
    // Make sure that we do not mark an entry as deleted.
    // According to FAT specification if the first character
    // in the file name is 0xE5 character (that is the marker
    // that indicates that the entry is invalid)
    // has to be replaced by 0x05.
    // The conversion to an upper case character is not required
    // in this case since we know that no letter has the first
    // byte set to 0xE5.
    //
    pShortName->ac[0] = 0x05;
    NumBytesRead = 0;
    Char = FS_pCharSetType->pfGetChar(pName, FS_WCHAR_MAX_SIZE, &NumBytesRead);     // Note 1
    if (NumBytesRead == 2u) {
      //
      // Make sure that we copy the entire character in order to avoid
      // passing an invalid character sequence to FS_FAT_StoreShortNamePartialMB().
      //
      pShortName->ac[1] = (U8)Char;
      FS_FAT_StoreShortNamePartialMB(&pShortName->ac[2], pName + 2,      6, (int)ExtPos - 2);
    } else {
      FS_FAT_StoreShortNamePartialMB(&pShortName->ac[1], pName + 1,      7, (int)ExtPos - 1);
    }
  } else {
    FS_FAT_StoreShortNamePartialMB(&pShortName->ac[0], pName,            8, (int)ExtPos);
  }
  FS_FAT_StoreShortNamePartialMB(&pShortName->ac[8], pName + ExtPos + 1, 3, ((int)NumBytes - (int)ExtPos) - 1);
}

#endif // FS_SUPPORT_MBCS

/*********************************************************************
*
*       FS_FAT_WriteDirEntry83
*/
void FS_FAT_WriteDirEntry83(FS_FAT_DENTRY * pDirEntry, const FS_83NAME * pFileName, U32 ClusterId, unsigned Attributes, U32 Size, unsigned Time, unsigned Date, unsigned Flags) {
  FS_MEMSET(pDirEntry, 0, sizeof(FS_FAT_DENTRY));
  FS_MEMCPY((char*)pDirEntry->Data, pFileName, 11);
  pDirEntry->Data[DIR_ENTRY_OFF_ATTRIBUTES] = (U8)Attributes;
  FS_StoreU16LE(&pDirEntry->Data[DIR_ENTRY_OFF_CREATION_TIME], Time);
  FS_StoreU16LE(&pDirEntry->Data[DIR_ENTRY_OFF_CREATION_DATE], Date);
  FS_StoreU16LE(&pDirEntry->Data[DIR_ENTRY_OFF_WRITE_TIME],    Time);
  FS_StoreU16LE(&pDirEntry->Data[DIR_ENTRY_OFF_WRITE_DATE],    Date);
  FS_FAT_WriteDirEntryCluster(pDirEntry, ClusterId);
  FS_StoreU32LE(&pDirEntry->Data[DIR_ENTRY_OFF_SIZE],          Size);
  pDirEntry->Data[DIR_ENTRY_OFF_FLAGS] = (U8)Flags;
}

/*********************************************************************
*
*       FS_FAT_WriteDirEntryCluster
*/
void FS_FAT_WriteDirEntryCluster(FS_FAT_DENTRY * pDirEntry, U32 Cluster) {
  FS_StoreU16LE(&pDirEntry->Data[DIR_ENTRY_OFF_FIRSTCLUSTER_LOW],  Cluster);
  FS_StoreU16LE(&pDirEntry->Data[DIR_ENTRY_OFF_FIRSTCLUSTER_HIGH], Cluster >> 16);
}

/*********************************************************************
*
*       FS_FAT_GetFirstCluster
*/
U32 FS_FAT_GetFirstCluster(const FS_FAT_DENTRY * pDirEntry) {
  U32 r;

  r  = FS_LoadU16LE(&pDirEntry->Data[DIR_ENTRY_OFF_FIRSTCLUSTER_LOW]);
  r |= ((U32)FS_LoadU16LE(&pDirEntry->Data[DIR_ENTRY_OFF_FIRSTCLUSTER_HIGH])) << 16;
  return r;
}

/*********************************************************************
*
*       FS_FAT_InitDirEntryScan
*/
void FS_FAT_InitDirEntryScan(const FS_FAT_INFO * pFATInfo, FS_DIR_POS * pDirPos, U32 DirCluster) {
  pDirPos->DirEntryIndex = 0;
  pDirPos->ClusterIndex  = 0;
  if (pFATInfo->FATType == FS_FAT_TYPE_FAT32) {
    if (DirCluster == 0u) {
      DirCluster = pFATInfo->RootDirPos;
    }
  }
  pDirPos->FirstClusterId = DirCluster;
  pDirPos->ClusterId      = DirCluster;
}

/*********************************************************************
*
*       FS_FAT_GetDirEntry
*
*  Function description
*    Returns a directory entry by relative position.
*
*  Return value
*    !=NULL       OK, directory entry.
*    ==NULL       An error occurred.
*/
FS_FAT_DENTRY * FS_FAT_GetDirEntry(FS_VOLUME * pVolume, FS_SB * pSB, FS_DIR_POS * pDirPos) {
  U32             Cluster;
  U32             DirSector;
  U32             DirEntryIndex;
  U32             DirSectorIndex;
  U32             CurClusterIndex;
  FS_FAT_INFO   * pFATInfo;
  unsigned        ShiftPerEntry;
  U8            * pBuffer;
  FS_FAT_DENTRY * pDirEntry;
  int             r;

  pFATInfo        = &pVolume->FSInfo.FATInfo;
  Cluster         = pDirPos->ClusterId;
  DirEntryIndex   = pDirPos->DirEntryIndex;
  CurClusterIndex = (DirEntryIndex << DIR_ENTRY_SHIFT) >> pFATInfo->ldBytesPerCluster;
  if (CurClusterIndex < pDirPos->ClusterIndex) {
    pDirPos->ClusterIndex = 0;
  }
  //
  // Walk to the right cluster starting from the last known cluster position.
  //
  ShiftPerEntry = (unsigned)pFATInfo->ldBytesPerSector - DIR_ENTRY_SHIFT;
  if (pDirPos->ClusterIndex == 0u) {
    Cluster = pDirPos->FirstClusterId;
  }
  DirSectorIndex = DirEntryIndex >> ShiftPerEntry;
  if (Cluster != 0u) {
    U32 SectorMask;
    U32 NumClustersToWalk;

    SectorMask        = (U32)pFATInfo->SectorsPerCluster - 1u;
    NumClustersToWalk = CurClusterIndex - pDirPos->ClusterIndex;
    //
    // Go to next cluster.
    //
    if (NumClustersToWalk != 0u) {
      U32 LastCluster;

      LastCluster = Cluster;
      Cluster = FS_FAT_WalkCluster(pVolume, pSB, Cluster, NumClustersToWalk);
      //
      // Check if we get somehow further (either forwards or backwards), if not,
      // the file system is corrupt and may be checked with checkdisk.
      // In order to avoid any endless loop in functions that use this function
      // we will ignore this and will return that we have reached the end of the cluster chain.
      //
      if (Cluster == LastCluster) {
        FS_DEBUG_ERROROUT((FS_MTYPE_FS, "FAT: FS_FAT_GetDirEntry: Invalid cluster chain found."));
        Cluster = 0;
      }
    }
    if (Cluster == 0u) {
      return NULL;            // Error, no more clusters.
    }
    DirSector = FS_FAT_ClusterId2SectorNo(pFATInfo, Cluster) + (DirSectorIndex & SectorMask);
  } else {
    U32 MaxDirSectorIndex;

    MaxDirSectorIndex = (U32)pFATInfo->RootEntCnt >> ShiftPerEntry;
    if (DirSectorIndex  < MaxDirSectorIndex) {
      DirSector = pFATInfo->RootDirPos + DirSectorIndex;
    } else {
      return NULL;            // Error, no more directory entries.
    }
  }
  pDirEntry             = NULL;
  pDirPos->ClusterId    = Cluster;
  pDirPos->ClusterIndex = CurClusterIndex;
  FS__SB_SetSector(pSB, DirSector, FS_SECTOR_TYPE_DIR, 1);
  r = FS__SB_Read(pSB);
  if (r == 0) {
    pBuffer   = FS__SB_GetBuffer(pSB);
    pDirEntry = SEGGER_PTR2PTR(FS_FAT_DENTRY, pBuffer) + (DirEntryIndex & ((1uL << ShiftPerEntry) - 1u));           // MISRA deviation D:100[e]
  }
  return pDirEntry;
}

/*********************************************************************
*
*       FS_FAT_GetDirEntryEx
*
*  Function description
*    Returns a directory index by absolute position.
*
*  Return value
*    !=NULL       OK, directory entry.
*    ==NULL       An error occurred.
*/
FS_FAT_DENTRY * FS_FAT_GetDirEntryEx(const FS_VOLUME * pVolume, FS_SB * pSB, const FS_DIRENTRY_POS * pDirEntryPos) {
  FS_FAT_DENTRY * pDirEntry;
  U32             SectorIndex;
  U32             DirEntryIndex;
  unsigned        BytesPerSector;
  unsigned        SectorOff;
  int             r;
  U8            * pBuffer;

  pDirEntry      = NULL;
  BytesPerSector = pVolume->FSInfo.FATInfo.BytesPerSector;
  SectorIndex    = pDirEntryPos->fat.SectorIndex;
  DirEntryIndex  = pDirEntryPos->fat.DirEntryIndex;
  SectorOff      = (DirEntryIndex * sizeof(FS_FAT_DENTRY)) & (BytesPerSector - 1u);
  FS__SB_SetSector(pSB, SectorIndex, FS_SECTOR_TYPE_DIR, 1);
  r = FS__SB_Read(pSB);
  if (r == 0) {
    pBuffer  = FS__SB_GetBuffer(pSB);
    pBuffer += SectorOff;
    pDirEntry = SEGGER_PTR2PTR(FS_FAT_DENTRY, pBuffer);                                                             // MISRA deviation D:100[e]
  }
  return pDirEntry;
}

/*********************************************************************
*
*       FS_FAT_IncDirPos
*
*  Function description
*    Increments the position in the directory.
*/
void FS_FAT_IncDirPos(FS_DIR_POS * pDirPos) {
  pDirPos->DirEntryIndex++;
}

/*********************************************************************
*
*       FS_FAT_InvalidateDirPos
*
*  Function description
*    Invalidates the position in the directory.
*/
void FS_FAT_InvalidateDirPos(FS_DIR_POS * pDirPos) {
  if (pDirPos != NULL) {
    FS_MEMSET(pDirPos, 0, sizeof(FS_DIR_POS));
    pDirPos->ClusterId = CLUSTER_ID_INVALID;
  }
}

/*********************************************************************
*
*       FS_FAT_IsValidDirPos
*
*  Function description
*    Checks if a directory position is valid.
*
*  Return value
*    ==1    Position is valid.
*    ==0    Position is not valid.
*/
int FS_FAT_IsValidDirPos(const FS_DIR_POS * pDirPos) {
  int r;

  r = 1;          // Position is valid.
  if (pDirPos->ClusterId == CLUSTER_ID_INVALID) {
    r = 0;        // Position is not valid.
  }
  return r;
}

/*********************************************************************
*
*       FS_FAT_FindEmptyDirEntry
*
*  Function description
*    Tries to find an empty directory entry in the specified directory.
*    If there is no free entry, try to increase directory size.
*
*  Parameters
*    pVolume      Volume information.
*    pSB          Sector buffer to be used for the read operations.
*    DirStart     Start of directory, where to create pDirName.
*
*  Return value
*    != NULL    Free entry found.
*    ==NULL     An error has occurred.
*/
FS_FAT_DENTRY * FS_FAT_FindEmptyDirEntry(FS_VOLUME * pVolume, FS_SB * pSB, U32 DirStart) {
  FS_FAT_DENTRY * pDirEntry;
  U32             SectorIndex;
  FS_FAT_INFO   * pFATInfo;
  FS_DIR_POS      DirPos;
  U32             NewCluster;
  U32             LastCluster;
  unsigned        c;
  int             r;
  U8            * pBuffer;
  U32             NumSectors;
  FS_PARTITION  * pPart;

  pFATInfo = &pVolume->FSInfo.FATInfo;
  //
  // Read directory, trying to find an empty slot.
  //
  FS_FAT_InitDirEntryScan(pFATInfo, &DirPos, DirStart);
  for (;;) {
    pDirEntry = FS_FAT_GetDirEntry(pVolume, pSB, &DirPos);
    FS_FAT_IncDirPos(&DirPos);
    if (pDirEntry == NULL) {
      if ((DirStart == 0u) && (pFATInfo->RootEntCnt != 0u)) {
        //
        // Root directory of FAT12/16 medium can not be increased.
        //
        FS_DEBUG_ERROROUT((FS_MTYPE_FS, "FAT: FS_FAT_FindEmptyDirEntry: Root directory too small."));
        return NULL;                      // Error, no more free entries in the root directory.
      }
      LastCluster = FS_FAT_FindLastCluster(pVolume, pSB, DirPos.ClusterId, NULL);
      NewCluster  = FS_FAT_AllocCluster(pVolume, pSB, LastCluster);
      FS__SB_Flush(pSB);
      if (NewCluster == 0u) {
        FS_DEBUG_ERROROUT((FS_MTYPE_FS, "FAT: FS_FAT_FindEmptyDirEntry: Disk is full."));
        return NULL;
      }
      if (FS__SB_GetError(pSB) != 0) {
        FS_DEBUG_ERROROUT((FS_MTYPE_FS, "FAT: FS_FAT_FindEmptyDirEntry: Could not update AT."));
        return NULL;
      }
      //
      // Clean new directory cluster (fill it with 0s).
      //
      pBuffer = FS__SB_GetBuffer(pSB);
      pPart   = &pVolume->Partition;
      FS_MEMSET(pBuffer, 0x00, pFATInfo->BytesPerSector);
      SectorIndex = FS_FAT_ClusterId2SectorNo(pFATInfo, NewCluster);
      NumSectors  = pFATInfo->SectorsPerCluster;
      r = FS_LB_WriteMultiplePart(pPart, SectorIndex, NumSectors, pBuffer, FS_SECTOR_TYPE_DIR, 1);
      FS__SB_MarkNotValid(pSB);       // Invalidate in sector buffer that this sector has been read.
#if FS_SUPPORT_SECTOR_BUFFER_CACHE
      FS__InvalidateSectorBuffer(pVolume, SectorIndex, NumSectors);
#endif // FS_SUPPORT_SECTOR_BUFFER_CACHE
      if (r != 0) {
        FS_DEBUG_ERROROUT((FS_MTYPE_FS, "FAT: FS_FAT_FindEmptyDirEntry: Cannot initialize directory."));
        return NULL;                  // Error, cannot initialize directory.
      }
      FS__SB_MarkValid(pSB, SectorIndex, FS_SECTOR_TYPE_DIR, 1);
      pDirEntry = SEGGER_PTR2PTR(FS_FAT_DENTRY, pBuffer);                                                           // MISRA deviation D:100[e]
      break;
    }
    c = pDirEntry->Data[0];
    if ((c == 0x00u) || (c == 0xE5u)) {   // A free entry has either 0 or 0xe5 as first byte.
      //
      // Free entry found.
      //
      break;
    }
  }
  return pDirEntry;
}

/*********************************************************************
*
*       FS_FAT_DeleteFileOrDir
*
*  Function description
*    Deletes a directory entry and frees all clusters allocated to it.
*
*  Parameters
*    pVolume            Volume information.
*    pSB                Sector buffer to be used for the read/write operations.
*    pDirEntry          Pointer to directory entry to be deleted (in sector buffer).
*    DirEntryIndex      Index of the directory entry that has to be deleted.
*    pDirPosLFN         Position of the first directory entry that stores the long file name.
*
*  Return value
*    ==0    OK, file or directory deleted.
*    !=0    An error occurred.
*/
int FS_FAT_DeleteFileOrDir(FS_VOLUME * pVolume, FS_SB * pSB, FS_FAT_DENTRY * pDirEntry, U32 DirEntryIndex, FS_DIR_POS * pDirPosLFN) {
  U32           FirstCluster;
  U32           NumClusters;
  FS_FAT_INFO * pFATInfo;
  int           r;
  int           Result;
  U32           SectorIndex;
  unsigned      Attr;
  int           IsFile;
  U32           FileSize;

  pFATInfo = &pVolume->FSInfo.FATInfo;
  //
  // Check if we have to delete a file or directory.
  //
  IsFile = 0;
  Attr = pDirEntry->Data[DIR_ENTRY_OFF_ATTRIBUTES];
  if ((Attr & FS_FAT_ATTR_DIRECTORY) != FS_FAT_ATTR_DIRECTORY) {
    IsFile = 1;
  }
  //
  // Determine how many clusters have to be freed.
  // For normal files, the number of clusters can be calculated from the file size
  // (assuming that the volume is not corrupted). This is the safest method since
  // it avoids the corruption of the volume in case there is no end-of-cluster mark
  // is found. If the function has to free the clusters allocated to a directory,
  // file size if always 0 and cannot be used for that purpose. To avoid running into
  // endless loop, NumClusters is set to a reasonable (configurable) limit.
  //
  if (IsFile != 0) {
    //
    // Deleting an opened file may cause a file system corruption when
    // the directory entry is updated while closing the file. Therefore,
    // we do not delete opened files.
    //
    SectorIndex = FS__SB_GetSectorIndex(pSB);
    if (FS_FAT_IsFileOpen(pVolume, SectorIndex, DirEntryIndex) != 0) {
      return FS_ERRCODE_FILE_IS_OPEN;
    }
    FileSize    = FS_LoadU32LE(&pDirEntry->Data[DIR_ENTRY_OFF_SIZE]);
    NumClusters = (FileSize + (U32)pFATInfo->BytesPerCluster - 1u) >> pFATInfo->ldBytesPerCluster;
  } else {
    NumClusters = FAT_MAX_NUM_CLUSTERS_DIR;
  }
  FirstCluster = FS_FAT_GetFirstCluster(pDirEntry);
  //
  // Mark the volume as dirty.
  //
  FS_FAT_UpdateDirtyFlagIfRequired(pVolume, 1);
  //
  // Delete directory entry containing the short file name.
  //
  pDirEntry->Data[0] = DIR_ENTRY_INVALID_MARKER;
  FS__SB_MarkDirty(pSB);
  //
  // If support for long file names is enabled, delete the directory entries containing the long file name.
  //
  r = FS_FAT_DelLongDirEntry(pVolume, pSB, pDirPosLFN);
  //
  // Free clusters in the allocation table.
  //
  Result = FS_FAT_FreeClusterChain(pVolume, pSB, FirstCluster, NumClusters);
  //
  // We have to ignore the error about invalid cluster chain of directory
  // because the actual number of clusters allocated to a directory is not known.
  //
  if (IsFile == 0) {
    if (Result == FS_ERRCODE_INVALID_CLUSTER_CHAIN) {
      Result = 0;
    }
  }
  if (Result != 0) {
    r = Result;
  }
  return r;
}

/*********************************************************************
*
*       FS_FAT_StoreShortName
*
*  Function description
*    Converts a file or directory name to the format expected on the storage.
*
*  Parameters
*    pShortName           [OUT] Encoded file or directory name.
*    pName                [IN] Name of the file or directory to be stored.
*    NumBytes             Number of bytes in the name of the file or directory.
*                         0 means that the length is unknown.
*    AcceptMultipleDots   Set to 1 if the short file name is allowed to contain
*                         more than one period character.
*
*  Return value
*    ==0   OK, name could be converted.
*    !=0   Error, name did not comply with 8.3 criteria.
*
*  Additional information
*    See FS_FAT_MakeShortName() for more information about AcceptMultipleDots.
*/
int FS_FAT_StoreShortName(FS_83NAME * pShortName, const U8 * pName, unsigned NumBytes, int AcceptMultipleDots) {
  unsigned i;
  int      ExtPos;
  U8       Byte;

  ExtPos = -1;
  i = 0;
  for (;;) {
    if (i == 13u) {
      return 1;                         // Error, file name too long.
    }
    Byte = *(pName + i);
    if (FS_FAT_IsValidShortNameChar(Byte) == 0) {
      return 1;                         // Invalid character used in string.
    }
    if (Byte == (U8)'.') {
      if (AcceptMultipleDots == 0) {
        if (ExtPos >= 0) {
          return 1;                     // Only one period character is allowed in an short file name.
        }
      }
      ExtPos = (int)i;
    }
    ++i;
    if (i >= NumBytes) {                // End of name ?
      if (ExtPos == -1) {
        ExtPos = (int)i;
      }
      break;
    }
  }
  //
  // Perform some checks.
  //
  if (ExtPos == 0) {
    return 1;                         // Error, no file name.
  }
  if (ExtPos > 8) {
    return 1;                         // Error, file name too long.
  }
  if (((int)i - ExtPos) > 4) {
    return 1;                         // Error, extension too long.
  }
  //
  // All checks passed, copy filename and extension.
  //
  FS_FAT_StoreShortNamePartial(&pShortName->ac[0], pName,              8, ExtPos);
  FS_FAT_StoreShortNamePartial(&pShortName->ac[8], pName + ExtPos + 1, 3, (((int)i - ExtPos) - 1));
  return 0;                           // O.K., file name successfully converted.
}

#if FS_SUPPORT_MBCS

/*********************************************************************
*
*       FS_FAT_StoreShortNameMB
*
*  Function description
*    Converts a file or directory name that use multi-byte characters
*    to the format expected on the storage.
*
*  Parameters
*    pShortName           [OUT] Encoded file or directory name.
*    pName                [IN] Name of the file or directory to be stored.
*    NumBytes             Number of bytes in the name of the file or directory.
*    AcceptMultipleDots   Set to 1 if the short file name is allowed to contain
*                         more than one period character.
*
*  Return value
*    ==0   OK, name could be converted.
*    !=0   Error, name did not comply with 8.3 criteria.
*
*  Additional information
*    See FS_FAT_MakeShortName() for more information about AcceptMultipleDots.
*/
int FS_FAT_StoreShortNameMB(FS_83NAME * pShortName, const U8 * pName, unsigned NumBytes, int AcceptMultipleDots) {
  unsigned i;
  int      ExtPos;
  FS_WCHAR Char;
  unsigned NumBytesRead;

  ExtPos = -1;
  i = 0;
  for (;;) {
    if (i >= 13u) {
      return 1;                         // Error, file name too long.
    }
    NumBytesRead = 0;
    Char = FS_pCharSetType->pfGetChar(pName + i, NumBytes - i, &NumBytesRead);
    if (Char == FS_WCHAR_INVALID) {
      return 1;                         // Invalid character encoding.
    }
    if (Char < 128u) {                  // Only the ASCII characters have to be checked.
      if (FS_FAT_IsValidShortNameChar((U8)Char) == 0) {
        return 1;                       // Invalid character used in string.
      }
    }
    if (Char == (FS_WCHAR)'.') {
      if (AcceptMultipleDots == 0) {
        if (ExtPos >= 0) {
          return 1;                     // Only one period character is allowed in an 8.3 file name.
        }
      }
      ExtPos = (int)i;
    }
    i += NumBytesRead;
    if (i >= NumBytes) {                // End of name ?
      if (ExtPos == -1) {
        ExtPos = (int)i;
      }
      break;
    }
  }
  //
  // Perform some checks.
  //
  if (ExtPos == 0) {
    return 1;                           // Error, no file name.
  }
  if (ExtPos > 8) {
    return 1;                           // Error, file name too long.
  }
  if (((int)i - ExtPos) > 4) {
    return 1;                           // Error, extension too long.
  }
  //
  // All checks passed, copy filename and extension.
  //
  FS_FAT_StoreShortNameCompleteMB(pShortName, pName, i, (unsigned)ExtPos);
  return 0;                           // O.K., file name successfully converted.
}

#endif // FS_SUPPORT_MBCS

/*********************************************************************
*
*       FS_FAT_MakeShortName
*
*  Function description
*    FS internal function. Convert a given name to the format, which is
*    used in the FAT directory.
*
*  Parameters
*    pOutName             Pointer to a buffer for storing the real name used
*                         in a directory.
*    pOrgName             Pointer to name to be translated.
*    Len                  Length of the name. 0 means no defined length.
*    AcceptMultipleDots   Set to 1 if the short file name is allowed to contain
*                         more than one period character.
*
*  Return value
*    ==0   OK, name could be converted.
*    !=0   Error, name did not comply with 8.3 criteria.
*
*  Additional information
*    According to the FAT specification only one period character is allowed
*    in a short file name but emFile versions older than 4.04a did not explicitly
*    check for this condition. AcceptMultipleDots is used to specify if more than
*    one period character is accepted so that the file system can access the files
*    with invalid names.
*
*  Notes
*    (1) Allowed file names
*        The filename must conform to 8.3 standards.
*        The extension is optional, the name may be 8 characters at most.
*/
int FS_FAT_MakeShortName(FS_83NAME * pOutName, const char * pOrgName, int Len, int AcceptMultipleDots) {
  int      r;
  unsigned NumBytes;

  NumBytes = (unsigned)Len;
  if (NumBytes == 0u) {
    NumBytes = (unsigned)FS_STRLEN(pOrgName);
  }
#if FS_SUPPORT_MBCS
  if (FS_pCharSetType->pfGetChar != NULL) {     // Is a multi-byte character set?
    r = FS_FAT_StoreShortNameMB(pOutName, SEGGER_CONSTPTR2PTR(const U8, pOrgName), NumBytes, AcceptMultipleDots);   // MISRA deviation D:100[e]
  } else
#endif
  {
    r = FS_FAT_StoreShortName(pOutName, SEGGER_CONSTPTR2PTR(const U8, pOrgName), NumBytes, AcceptMultipleDots);     // MISRA deviation D:100[e]
  }
  return r;
}

/*********************************************************************
*
*       FS_FAT_FindDirEntryShortEx
*
*  Function description
*    Tries to locate the short directory entry in the specified directory.
*
*  Return value
*    != NULL    Pointer to directory entry (in the smart buffer)
*    ==NULL     Entry not found
*/
FS_FAT_DENTRY * FS_FAT_FindDirEntryShortEx(FS_VOLUME * pVolume, FS_SB * pSB, const char *pEntryName, int Len, FS_DIR_POS * pDirPos, unsigned AttributeReq) {
  FS_FAT_DENTRY * pDirEntry;
  FS_83NAME       FATEntryName;

  if (FS_FAT_MakeShortName(&FATEntryName, pEntryName, Len, 1) != 0) {     // 1 specifies that file names that contain more than one period characters have to be accepted.
    return NULL;  // Error, entry name could not be converted.
  }
  //
  // Read directory.
  //
  for (;;) {
    pDirEntry = FS_FAT_GetDirEntry(pVolume, pSB, pDirPos);
    if (pDirEntry == NULL) {
      break;
    }
    if (pDirEntry->Data[0] == 0u) {
      pDirEntry = (FS_FAT_DENTRY*)NULL;
      break;  // No more entries. Not found.
    }
    if (FS_MEMCMP(pDirEntry->Data, &FATEntryName, 11) == 0) {   // Name does match.
      unsigned  Attrib;

      //
      // Do the attribute match ?
      //
      Attrib = pDirEntry->Data[DIR_ENTRY_OFF_ATTRIBUTES];
      if (((Attrib & AttributeReq) == AttributeReq) && (Attrib != FS_FAT_ATTR_VOLUME_ID)) {
        break;
      }
    }
    FS_FAT_IncDirPos(pDirPos);
  }
  return pDirEntry;
}

/*********************************************************************
*
*       FS_FAT_FindDirEntryShort
*
*  Function description
*    Tries to locate the short directory entry in the specified directory
*
*  Parameters
*    pVolume        Volume information.
*    pSB            Sector buffer to be used for the read operations.
*    pEntryName     Directory entry name.
*    Len            Maximum number of characters in EntryName
*                   (everything after that is ignored). E.g.: "Dir\File", 3: -> "Dir".
*    DirStart       Id of the cluster allocated to the directory.
*    pDirPos        Position in the parent directory.
*    AttributeReq   Directory attributes that should match.
*
*  Return value
*    != NULL    Pointer to directory entry (in the sector buffer).
*    ==NULL     Entry not found.
*/
FS_FAT_DENTRY * FS_FAT_FindDirEntryShort(FS_VOLUME * pVolume, FS_SB * pSB, const char * pEntryName, int Len, U32 DirStart, FS_DIR_POS * pDirPos, unsigned AttributeReq) {
  FS_FAT_INFO   * pFATInfo;
  FS_FAT_DENTRY * pDirEntry;

  pFATInfo = &pVolume->FSInfo.FATInfo;
  FS_FAT_InitDirEntryScan(pFATInfo, pDirPos, DirStart);
  pDirEntry = FS_FAT_FindDirEntryShortEx(pVolume, pSB, pEntryName, Len, pDirPos, AttributeReq);
  return pDirEntry;
}

/*********************************************************************
*
*       FS_FAT_FindPathEx
*
*  Function description
*    FS internal function. Return start cluster and size of the directory
*    of the file name in pFileName.
*
*  Parameters
*    pVolume        Volume information. Cannot be NULL.
*    pSB            Sector buffer to be used for the read operations. Cannot be NULL.
*    pFullName      Fully qualified file name w/o device name. Cannot be NULL.
*    ppFileName     [OUT] Pointer to a pointer, which is modified to point to the
*                         file name part of pFullName. Cannot be NULL.
*    pFirstCluster  [OUT] Pointer to an U32 for returning the start cluster of the directory. Cannot be NULL.
*    ppDirEntry     [OUT] Pointer in pSB->pBuffer to the first byte of directory entry. Can be NULL.
*    ClusterId      Id of the cluster to be checked.
*
*  Return value
*    > 0      OK, path exists. Directory level where 1 is the root directory.
*    ==0      An error occurred.
*
*  Additional information
*    The function opens the path of the highest level directory.
*    subdir               -> Opens \
*    subdir\              -> Opens \subdir\
*    subdir\subdir1       -> Opens \subdir\
*    subdir\subdir1\      -> Opens \subdir\subdir1\
*
*    The function returns an error if the cluster id passed via ClusterId
*    matches the cluster id of any visited directories in the path.
*    This functionality is used for detecting if an attempt is
*    made to move a directory in any of its subdirectories.
*    The checking is not performed if ClusterId is set to CLUSTER_ID_INVALID.
*/
int FS_FAT_FindPathEx(FS_VOLUME * pVolume, FS_SB * pSB, const char * pFullName, const char ** ppFileName, U32 * pFirstCluster, FS_FAT_DENTRY ** ppDirEntry, U32 ClusterId) {
  const char    * pDirNameStart;
  const char    * pDirNameStop;
  I32             i;
  U32             FirstCluster;
  FS_FAT_DENTRY * pDirEntry;
  int             DirLevel;

  //
  // Initialize local variables.
  //
  DirLevel     = 1;                                       // Set to indicate the root directory.
  *ppFileName  = pFullName;
  FirstCluster = 0;
  pDirEntry    = NULL;
  //
  // Descend into subdirectory for every directory delimiter found.
  //
  pDirNameStart = pFullName;
  pDirNameStop  = FS__FindDirDelimiter(*ppFileName);
  do {
    if (pDirNameStart == pDirNameStop) {
      pDirNameStart++;
      *ppFileName  = pDirNameStart;
      pDirNameStop  = FS__FindDirDelimiter(*ppFileName);
    }
    if (pDirNameStop != NULL) {
      i = (I32)(pDirNameStop - pDirNameStart);            //lint !e946 !e947 D:106
      if (i > 0) {
        pDirEntry = FS_FAT_FindDirEntry(pVolume, pSB, pDirNameStart, i, FirstCluster, FS_FAT_ATTR_DIRECTORY, NULL);
        if (pDirEntry == NULL) {
          DirLevel = 0;                                   // Error, directory entry not found.
          break;
        }
        FirstCluster = FS_FAT_GetFirstCluster(pDirEntry);
        if (ClusterId != CLUSTER_ID_INVALID) {
          if (ClusterId == FirstCluster) {
            DirLevel = 0;                                 // Error, cluster id is in the path.
            break;
          }
        }
        ++DirLevel;
      }
    }
    pDirNameStart = pDirNameStop;
    pDirNameStop  = FS__FindDirDelimiter(*ppFileName);
  } while (pDirNameStop != NULL);
  *pFirstCluster = FirstCluster;
  if (ppDirEntry != NULL) {
    *ppDirEntry = pDirEntry;
  }
  return DirLevel;
}

/*********************************************************************
*
*       FS_FAT_FindPath
*
*  Function description
*    FS internal function. Variant of FS_FAT_FindPathEx().
*/
int FS_FAT_FindPath(FS_VOLUME * pVolume, FS_SB * pSB, const char * pFullName, const char ** ppFileName, U32 * pFirstCluster) {
  int r;

  r = FS_FAT_FindPathEx(pVolume, pSB, pFullName, ppFileName, pFirstCluster, NULL, CLUSTER_ID_INVALID);
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
*       FS_FAT_OpenFile
*
*  Function description
*    FS internal function. Open an existing file or create a new one.
*
*  Parameters
*    sFileName    File name.
*    pFile        Handle to opened file.
*    DoDel        Set to 1 if the file has to be deleted.
*    DoOpen       Set to 1 if the file has to be opened.
*    DoCreate     Set to 1 if the file has to be created.
*
*  Return value
*    ==0       O.K., file opened.
*    !=0       Error code indicating the failure reason.
*/
int FS_FAT_OpenFile(const char * sFileName, FS_FILE * pFile, int DoDel, int DoOpen, int DoCreate) {
  const char    * pFName;
  U32             DirStart;
  unsigned        AccessFlags;
  I32             DirEntryIndex;
  U32             SectorIndex;
  U32             FirstCluster;
  U32             FileSize;
  int             r;
  FS_FILE_OBJ   * pFileObj;
  FS_VOLUME     * pVolume;
  FS_SB           sb;
  FS_FAT_DENTRY * pDirEntry;
  FS_DIR_POS      DirPosLFN;
  U8            * pBuffer;

  //
  // Search directory
  //
  pFileObj      = pFile->pFileObj;
  pVolume       = pFileObj->pVolume;
  DirEntryIndex = 0;
  SectorIndex   = 0;
  FirstCluster  = 0;
  FileSize      = 0;
  FS_FAT_InvalidateDirPos(&DirPosLFN);
  (void)FS__SB_Create(&sb, pVolume);
  pDirEntry = NULL;
  if (FS_FAT_FindPath(pVolume, &sb, sFileName, &pFName, &DirStart) == 0) {
    r = FS_ERRCODE_PATH_NOT_FOUND;                // Error, path to file not found.
    goto Done;
  }
  r = 0;       // No error so far
  AccessFlags = pFile->AccessFlags;
  pDirEntry = FS_FAT_FindDirEntry(pVolume, &sb, pFName, 0, DirStart, 0, &DirPosLFN);
  if (pDirEntry != NULL) {
    unsigned Attrib;

    //
    // Check that the directory entry is not a directory.
    //
    Attrib = pDirEntry->Data[DIR_ENTRY_OFF_ATTRIBUTES];
    if ((Attrib & FS_FAT_ATTR_DIRECTORY) != FS_FAT_ATTR_DIRECTORY) {
      pBuffer       = FS__SB_GetBuffer(&sb);
      DirEntryIndex = (I32)(pDirEntry - SEGGER_PTR2PTR(FS_FAT_DENTRY, pBuffer));     //lint !e946 !e947 D:106
      SectorIndex   = FS__SB_GetSectorIndex(&sb);
    } else {
      r = FS_ERRCODE_NOT_A_FILE;                  // Error, the specified file name is a directory.
      goto Done;
    }
  }
  //
  // Delete the file if requested
  //
  if (DoDel != 0) {                               // Do we need to delete the file ?
    if (pDirEntry != NULL) {                      // Does file exist ?
      r = FS_FAT_DeleteFileOrDir(pVolume, &sb, pDirEntry, (U32)DirEntryIndex, &DirPosLFN);
      if (r != 0) {
        goto Done;
      }
      pDirEntry = NULL;                           // File does not exist any more
    } else {
      if ((DoOpen == 0) && (DoCreate == 0)) {
        r = FS_ERRCODE_FILE_DIR_NOT_FOUND;        // This is an error unless some other command is executed
        goto Done;
      }
    }
  }
  //
  // Open file if requested.
  //
  if (DoOpen != 0) {
    if (pDirEntry != NULL) {                      // Does file exist ?
      //
      // Check if the file is read-only and we try to create, write or append.
      //
      if ((((pDirEntry->Data[DIR_ENTRY_OFF_ATTRIBUTES] & FS_FAT_ATTR_READ_ONLY) != 0u)) &&
            ((AccessFlags & (FS_FILE_ACCESS_FLAG_W | FS_FILE_ACCESS_FLAG_A | FS_FILE_ACCESS_FLAG_C)) != 0u)) {
        r = FS_ERRCODE_READ_ONLY_FILE;            // Error, writing to a read-only file is not allowed.
        goto Done;
      }
      FirstCluster = FS_FAT_GetFirstCluster(pDirEntry);
      FileSize     = FS_LoadU32LE(&pDirEntry->Data[DIR_ENTRY_OFF_SIZE]);
      DoCreate     = 0;                           // Do not create, since it could be opened.
    } else {
      if (DoCreate == 0) {
        r = FS_ERRCODE_FILE_DIR_NOT_FOUND;        // This is an error unless some other command is executed.
        goto Done;
      }
    }
  }
  //
  // Do we need to create the file ?
  //
  if (DoCreate != 0) {
    if (pDirEntry == NULL) {
      U32 TimeDate;

      TimeDate = FS__GetTimeDate();
      //
      // Mark the volume as dirty.
      //
      FS_FAT_UpdateDirtyFlagIfRequired(pVolume, 1);
      //
      // Create a new file.
      //
      pDirEntry = FAT_pDirEntryAPI->pfCreateDirEntry(pVolume, &sb, pFName, DirStart, 0, FS_FAT_ATTR_ARCHIVE, 0, TimeDate & 0xFFFFu, TimeDate >> 16);
      if (pDirEntry != NULL) {
        //
        // Free entry found.
        //
        pBuffer = FS__SB_GetBuffer(&sb);
        DirEntryIndex = (I32)(pDirEntry - SEGGER_PTR2PTR(FS_FAT_DENTRY, pBuffer));       //lint !e946 !e947 D:106
        SectorIndex   = FS__SB_GetSectorIndex(&sb);
      } else {
        r = FS_ERRCODE_WRITE_FAILURE;             // Error, could not create file.
        goto Done;
      }
      FileSize     = 0;
      FirstCluster = 0;
    } else {
      r = FS_ERRCODE_FILE_DIR_EXISTS;             // Error, file already exists, we can recreate an additional directory entry.
      goto Done;
    }
  }
  pFileObj->DirEntryPos.fat.SectorIndex   = SectorIndex;
  pFileObj->DirEntryPos.fat.DirEntryIndex = (U16)DirEntryIndex;
  pFileObj->Data.Fat.CurClusterIndex      = CLUSTER_INDEX_INVALID;
  pFileObj->FirstCluster                  = FirstCluster;
  pFileObj->Size                          = FileSize;
#if FS_SUPPORT_ENCRYPTION
  pFileObj->SizeEncrypted                 = FileSize;
#endif
  pFile->FilePos                          = ((AccessFlags & FS_FILE_ACCESS_FLAG_A)) != 0u ? pFileObj->Size : 0u;
Done:
  FS__SB_Delete(&sb);
  if (r == 0) {
    r = FS__SB_GetError(&sb);
  }
  return r;
}

/*************************** End of file ****************************/

