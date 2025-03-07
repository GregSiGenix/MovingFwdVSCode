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
File        : FS_FAT_Write.c
Purpose     : FAT file write routines
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
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _WriteBurst
*/
static int _WriteBurst(const BURST_INFO_W * pBurstInfo) {
  FS_PARTITION * pPart;
  FS_VOLUME    * pVolume;

  if (pBurstInfo->NumSectors != 0u) {
    pVolume = FS__SB_GetVolume(pBurstInfo->pSBData);
    pPart   = &pVolume->Partition;
    if (FS_LB_WriteBurstPart(pPart,
                             pBurstInfo->FirstSector,
                             pBurstInfo->NumSectors,
                             pBurstInfo->pData,
                             FS_SECTOR_TYPE_DATA,
                             pBurstInfo->WriteToJournal) != 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_FS, "FAT: _WriteBurst: Burst write error."));
      return 1;     // Write error
    }
  }
  return 0;         // No problem !
}

/*********************************************************************
*
*       _WriteData
*
*  Return value
*    Number of bytes written
*/
static U32 _WriteData(const U8 * pData, U32 NumBytes2Write, FS_FILE * pFile, FS_SB * pSBData, FS_SB * pSBfat, FS_SB * pSBCrypt) {
  U32               NumBytesWritten;
  U32               NumBytesCluster;
  U32               BytesPerCluster;
  unsigned          SectorOff;
  U32               SectorIndex;
  int               IsDirUpdateRequired;
  int               ZeroCopyAllowed;
  U32               LastByteInCluster;
  int               r;
  U32               FilePos;
  U32               FileSize;
  FS_VOLUME       * pVolume;
  FS_WRITEMODE      WriteMode;
  FS_FILE_OBJ     * pFileObj;
  FS_FAT_INFO     * pFATInfo;
  BURST_INFO_W      BurstInfo;
  U32               BytesPerSector;
  U32               ClusterIndex;
#if FS_SUPPORT_JOURNAL
  U32               ClusterIndexFirst;
  int               IsDataInJournal;
#endif // FS_SUPPORT_JOURNAL
  int               WriteToJournal;
  FS_INT_DATA_FAT * pFATData;

  //
  // Initialize and calculate some values used throughout the routine.
  //
  IsDirUpdateRequired      = 0;
  pFileObj                 = pFile->pFileObj;
  pFATData                 = &pFileObj->Data.Fat;
  pFATInfo                 = &pFileObj->pVolume->FSInfo.FATInfo;
  BytesPerCluster          = pFATInfo->BytesPerCluster;
  NumBytesWritten          = 0;
  ZeroCopyAllowed          = 1;
  pVolume                  = pFileObj->pVolume;
  WriteMode                = FS__GetFileWriteModeEx(pVolume);
  BurstInfo.NumSectors     = 0;
  BurstInfo.FirstSector    = 0xFFFFFFFFuL;
  BurstInfo.pSBData        = pSBData;
  BurstInfo.pData          = NULL;
  BurstInfo.WriteToJournal = 1;
  BytesPerSector           = pFATInfo->BytesPerSector;
#if FS_SUPPORT_JOURNAL
  ClusterIndexFirst        = pFATData->CurClusterIndex;
  IsDataInJournal          = 0;
#endif // FS_SUPPORT_JOURNAL
#if FS_SUPPORT_CHECK_MEMORY
  {
    FS_MEM_CHECK_CALLBACK * pfMemCheck;

    pfMemCheck = pFileObj->pVolume->Partition.Device.Data.pfMemCheck;
    if (pfMemCheck != NULL) {
      if (pfMemCheck(SEGGER_PTR2PTR(void, pData), NumBytes2Write) == 0) {       //lint !e9005 attempt to cast away const/volatile from a pointer or reference D:105. Reason: We cannot change the API without breaking existing applications.
        ZeroCopyAllowed = 0;
      }
    }
  }
#endif // FS_SUPPORT_CHECK_MEMORY
  //
  // Main loop
  // We determine the cluster (allocate as necessary using the FAT buffer)
  // and write data into the cluster
  //
  do {
    //
    // Locate current cluster.
    //
    if (FS_FAT_GotoClusterAllocIfReq(pFile, pSBfat) != 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_FS, "FAT: _WriteData: Could not allocate cluster to file."));
      if (_WriteBurst(&BurstInfo) != 0) {
        NumBytesWritten = 0;            // We do not know how many bytes have been written o.k., so reporting 0 is on the safe side
      }
      (void)FS_FAT_UpdateDirEntry(pFileObj, pSBData, 1);
      return NumBytesWritten;           // File truncated (too few clusters)
    }
    ClusterIndex      = pFATData->CurClusterIndex;
    LastByteInCluster = BytesPerCluster * (ClusterIndex + 1u);
    NumBytesCluster   = LastByteInCluster - (U32)pFile->FilePos;
    SectorOff         = pFile->FilePos & (BytesPerSector - 1u);
    if (NumBytesCluster > NumBytes2Write) {
      NumBytesCluster = NumBytes2Write;
    }
    SectorIndex  = FS_FAT_ClusterId2SectorNo(pFATInfo, pFATData->CurClusterId);
    SectorIndex += (pFile->FilePos >> pFATInfo->ldBytesPerSector) & ((unsigned)pFATInfo->SectorsPerCluster - 1u);
    //
    // Write data into the cluster, iterating over sectors
    //
    do {
      unsigned   NumBytesSector;
      U8       * pBuffer;
      int        IsRead;

      NumBytesSector = BytesPerSector - SectorOff;
      if ((U32)NumBytesSector > NumBytesCluster) {
        NumBytesSector = (unsigned)NumBytesCluster;
      }
      //
      // We can write directly to device without using the journaling
      // when we append the data to file
      //
      WriteToJournal = 1;
#if FS_SUPPORT_JOURNAL
      if (pFile->FilePos == pFileObj->Size) {
        WriteToJournal = 0;
      }
      if (IsDataInJournal == 0) {
        if (WriteToJournal != 0) {
          IsDataInJournal = 1;
        }
      }
#endif // FS_SUPPORT_JOURNAL
      //
      // Check if we can write an entire sector.
      //
      if   ((ZeroCopyAllowed == 0)
#if (FS_DRIVER_ALIGNMENT > 1)                   // Not required, just to avoid warnings
        || ((SEGGER_PTR2ADDR(pData) & ((unsigned)FS_DRIVER_ALIGNMENT - 1u)) != 0u)                    // MISRA deviation D:103[b]
#endif
#if FS_SUPPORT_ENCRYPTION
        || (pFileObj->pCryptObj != NULL)        // Encryption active?
#endif
        || (NumBytesSector != BytesPerSector)) {
        //
        // Read the sector if we need to modify an existing one
        //
        IsRead   = 0;
        FilePos  = (U32)pFile->FilePos;
        FileSize = (U32)pFileObj->Size;
        if (   (SectorOff != 0u)                                                                      // Are we writing somewhere inside the sector?
            || (   (FilePos < FileSize)                                                               // Do we overwrite old data?
                && (   ((FilePos & ~(BytesPerSector - 1u)) != (FileSize & ~(BytesPerSector - 1u)))    // Are size and position not on the same sector?
                    || (NumBytesSector < (FileSize & (BytesPerSector - 1u)))))) {                     // Do we overwrite only a part of the data located at the end of file?
          r = FS_FAT_ReadDataSector(SectorIndex, FilePos, pFileObj, pSBData, pSBCrypt);
          if (r != 0) {
            pFile->Error = FS_ERRCODE_READ_FAILURE;         // Error, could not read data sector.
            return NumBytesWritten;
          }
          IsRead = 1;
        }
        //
        // Merge the written data into the sector
        //
        pBuffer = FS__SB_GetBuffer(pSBData);
        FS_MEMCPY(pBuffer + SectorOff, pData, NumBytesSector);
        //
        // Initialize the rest of the sector with a known value.
        //
        if (IsRead == 0) {
          SectorOff += NumBytesSector;
          if (SectorOff < BytesPerSector) {
            FS_MEMSET(pBuffer + SectorOff, FS_FILL_PATTERN_UNUSED_DATA, BytesPerSector - SectorOff);
          }
        }
        //
        // Write the sector data.
        //
#if FS_SUPPORT_ENCRYPTION
        r = FS_FAT_WriteDataSectorEncrypted(SectorIndex, FilePos, NumBytesSector, FileSize, WriteToJournal, pFileObj, pSBData, pSBCrypt);
#else
        r = FS_FAT_WriteDataSector(SectorIndex, WriteToJournal, pSBData);
#endif // FS_SUPPORT_ENCRYPTION
        if (r != 0) {
          pFile->Error = FS_ERRCODE_WRITE_FAILURE;
          return NumBytesWritten;                         // Error, could not write data sector.
        }
      } else {
        //
        // Write the sector with "Zero-copy"
        //
        if (   (SectorIndex != (BurstInfo.FirstSector + BurstInfo.NumSectors))
            || (WriteToJournal != (int)BurstInfo.WriteToJournal)) {
          if (_WriteBurst(&BurstInfo) != 0) {
            pFile->Error = FS_ERRCODE_WRITE_FAILURE;
            return NumBytesWritten;
          }
          BurstInfo.FirstSector    = SectorIndex;
          BurstInfo.NumSectors     = 1;
          BurstInfo.pData          = pData;
          BurstInfo.WriteToJournal = (U8)WriteToJournal;
        } else {
          BurstInfo.NumSectors++;
        }
      }
      //
      // Update management info
      //
      pData           += NumBytesSector;
      NumBytesCluster -= NumBytesSector;
      NumBytes2Write  -= NumBytesSector;
      NumBytesWritten += NumBytesSector;
      pFile->FilePos  += NumBytesSector;
      SectorIndex++;
      SectorOff = 0;                /* Next sector will be written from start */
      //
      // Update File size
      //
      if (pFile->FilePos > pFileObj->Size) {
        switch (WriteMode) {
        case FS_WRITEMODE_MEDIUM:
          if (pFileObj->Size == 0u) {       // Update the directory entry only on the first write to file.
            IsDirUpdateRequired = 1;
          }
          break;
        case FS_WRITEMODE_FAST:
          break;                            // The directory entry is updated when the file is closed.
        case FS_WRITEMODE_SAFE:
          //through
        case FS_WRITEMODE_UNKNOWN:
          //through
        default:
          IsDirUpdateRequired = 1;          // Always update the directory entry in SAFE mode.
          break;
        }
        pFileObj->Size = pFile->FilePos;
      }
    } while (NumBytesCluster != 0u);
  } while (NumBytes2Write != 0u);
  //
  // Flush Burst
  //
  if (_WriteBurst(&BurstInfo) != 0) {
    pFile->Error = FS_ERRCODE_WRITE_FAILURE;
    NumBytesWritten = 0;                    // We do not know how many bytes have been written o.k., so reporting 0 is on the safe side.
  }
  //
  // Update directory entry if required.
  //
  if (IsDirUpdateRequired != 0) {
    WriteToJournal = 1;
#if FS_SUPPORT_JOURNAL
    //
    // The directory entry does not have to be written to journal
    // if no new cluster was allocated during the write operation.
    // This optimization can be applied only for not nested journal
    // transactions. The optimization cannot be applied when the
    // application overwrites the last data in the file and it also
    // appends new data to file. This condition is detected via IsDataInJournal.
    //
    if (ClusterIndexFirst == ClusterIndex) {
      FS_JOURNAL_DATA * pJournalData;

      pJournalData = &pVolume->Partition.Device.Data.JournalData;
      if (IsDataInJournal == 0) {
        if (pJournalData->IsTransactionNested == 0u) {
          WriteToJournal = 0;
        }
      }
    }
#endif // FS_SUPPORT_JOURNAL
    r = FS_FAT_UpdateDirEntry(pFileObj, pSBData, WriteToJournal);
    if (r != 0) {
      NumBytesWritten = 0;
    }
  } else {
    //
    // Remember that the file data has been modified. This flag is checked when the file is closed
    // in order to decide whether the directory entry should be updated or not.
    //
    IsDirUpdateRequired = 1;
    if (WriteMode == FS_WRITEMODE_SAFE) {
      U32 TimeStamp;

      TimeStamp = FS__GetTimeDate();
      if (TimeStamp == TIME_DATE_DEFAULT) {
        IsDirUpdateRequired = 0;                    // Do not update the directory entry if the application does not provide a time base for the timestamp.
      }
    }
    if (IsDirUpdateRequired != 0) {
      pFile->IsDirUpdateRequired = 1;
    }
  }
  return NumBytesWritten;
}

/*********************************************************************
*
*       _UpdateFSInfoSectorIfRequired
*
*  Function description
*    Updates the information in the FSInfoSector for FAT32 volumes.
*/
static void _UpdateFSInfoSectorIfRequired(FS_VOLUME * pVolume) {
#if FS_FAT_USE_FSINFO_SECTOR
  FS_FAT_INFO       * pFATInfo;
  FAT_FSINFO_SECTOR * pFSInfoSector;
  FS_SB               sb;
  U8                * pBuffer;

  pFATInfo      = &pVolume->FSInfo.FATInfo;
  pFSInfoSector = &pFATInfo->FSInfoSector;
  if (   (FAT_UseFSInfoSector != 0u)
      && (pFSInfoSector->IsPresent != 0u)
      && (pFSInfoSector->IsUpdateRequired != 0u)) {
    //
    // Update the information to the FSInfo sector.
    //
    (void)FS__SB_Create(&sb, pVolume);
    FS__SB_SetSector(&sb, pFSInfoSector->SectorIndex, FS_SECTOR_TYPE_MAN, 1);
    if (FS__SB_Read(&sb) == 0) {
      pBuffer = FS__SB_GetBuffer(&sb);
      FS_StoreU32LE(&pBuffer[FSINFO_OFF_FREE_CLUSTERS],     pFATInfo->NumFreeClusters);
      FS_StoreU32LE(&pBuffer[FSINFO_OFF_NEXT_FREE_CLUSTER], pFATInfo->NextFreeCluster);
      FS__SB_MarkDirty(&sb);
    }
    FS__SB_Delete(&sb);
    if (FS__SB_GetError(&sb) == 0) {
      pFSInfoSector->IsUpdateRequired = 0;
    }
  }
#else
  FS_USE_PARA(pVolume);
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
*       FS_FAT_UpdateDirEntry
*/
int FS_FAT_UpdateDirEntry(const FS_FILE_OBJ * pFileObj, FS_SB * pSB, int WriteToJournal) {
  FS_FAT_DENTRY         * pDirEntry;
  U32                     TimeDate;
  FS_VOLUME             * pVolume;
  const FS_DIRENTRY_POS * pDirEntryPos;
  int                     r;

  r            = 1;           // Set to indicate an error.
  pVolume      = pFileObj->pVolume;
  pDirEntryPos = &pFileObj->DirEntryPos;
  pDirEntry = FS_FAT_GetDirEntryEx(pVolume, pSB, pDirEntryPos);
  if (pDirEntry != NULL) {
    //
    // Modify directory entry.
    //
    FS_StoreU32LE(&pDirEntry->Data[DIR_ENTRY_OFF_SIZE], (U32)pFileObj->Size);
    FS_FAT_WriteDirEntryCluster(pDirEntry, pFileObj->FirstCluster);
    TimeDate = FS__GetTimeDate();
    FS_StoreU16LE(&pDirEntry->Data[DIR_ENTRY_OFF_WRITE_TIME], (unsigned)(TimeDate & 0xFFFFu));
    FS_StoreU16LE(&pDirEntry->Data[DIR_ENTRY_OFF_WRITE_DATE], (unsigned)(TimeDate >> 16));
    FS__SB_SetWriteToJournal(pSB, WriteToJournal);
    FS__SB_Flush(pSB);                   // Write the modified directory entry.
    r = FS__SB_GetError(pSB);
  }
  return r;
}

/*********************************************************************
*
*       FS_FAT_GotoClusterAllocIfReq
*
*  Function description
*    Selects the current cluster and allocates new clusters if required.
*
*  Parameters
*    pFile    Handle to opened file.
*    pSB      Sector buffer to work with.
*
*  Return value
*    ==0    OK, current cluster selected or allocated.
*    !=0    Error code indicating the failure reason.
*
*  Additional information
*    The current cluster is the id of the cluster that
*    stores the data at the current file position.
*/
int FS_FAT_GotoClusterAllocIfReq(FS_FILE * pFile, FS_SB * pSB) {
  FS_FILE_OBJ     * pFileObj;
  FS_VOLUME       * pVolume;
  int               NumClustersToGo;
  int               r;
#if FS_FAT_SUPPORT_FREE_CLUSTER_CACHE
  FS_WRITEMODE      WriteMode;
#endif // FS_FAT_SUPPORT_FREE_CLUSTER_CACHE
  int               Result;
  FS_INT_DATA_FAT * pFATData;

  r               = 0;
  pFileObj        = pFile->pFileObj;
  pFATData        = &pFileObj->Data.Fat;
  pVolume         = pFileObj->pVolume;
#if FS_FAT_SUPPORT_FREE_CLUSTER_CACHE
  WriteMode       = FS__GetFileWriteModeEx(pVolume);
#endif // FS_FAT_SUPPORT_FREE_CLUSTER_CACHE
  NumClustersToGo = FS_FAT_GotoCluster(pFile, pSB);
  if (NumClustersToGo > 0) {
    //
    // Make sure at least one cluster is allocated, so that FirstCluster is valid.
    // If no cluster has yet been allocated, allocate one
    //
    if (pFileObj->FirstCluster == 0u) {
      U32 CurClusterId;                       // FAT id of the current cluster.

      CurClusterId = FS_FAT_FindFreeCluster(pVolume, pSB, 0, pFile);
      if (CurClusterId == 0u) {
        r = FS_ERRCODE_VOLUME_FULL;
        pFile->Error = (I16)r;
        return r;                             // Error, no free cluster found.
      }
      NumClustersToGo--;
      pFileObj->FirstCluster    = CurClusterId;
      pFATData->CurClusterId    = CurClusterId;
      pFATData->CurClusterIndex = 0;
#if FS_FAT_SUPPORT_FREE_CLUSTER_CACHE
      if (WriteMode != FS_WRITEMODE_FAST)
#endif // FS_FAT_SUPPORT_FREE_CLUSTER_CACHE
      {
        Result = FS_FAT_MarkClusterEOC(pVolume, pSB, pFATData->CurClusterId);
        if (Result != 0) {
          pFile->Error = (I16)Result;
          return Result;                      // Error, could not mark cluster as end of chain.
        }
      }
    }
    if (NumClustersToGo != 0) {
      do {
        U32           NewCluster;
        U32           StartCluster;
        U32           LastCluster;
        U32           NextCluster;
        FS_FAT_INFO * pFATInfo;

        pFATInfo     = &pVolume->FSInfo.FATInfo;
        StartCluster = pFATData->CurClusterId;
#if FS_SUPPORT_TEST
        if (FS_Global.AllocMode == DISK_ALLOC_MODE_NEXT_FREE) {
          StartCluster = pFATInfo->NextFreeCluster;
        } else {
          if (FS_Global.AllocMode == DISK_ALLOC_MODE_BEST_FREE)
#endif // FS_SUPPORT_TEST
          {
            NextCluster = StartCluster + 1u;
            LastCluster = pFATInfo->NumClusters + FAT_FIRST_CLUSTER - 1u;
            if (NextCluster > LastCluster) {
              NextCluster = FAT_FIRST_CLUSTER;
            }
            if (FS_FAT_IsClusterFree(pVolume, pSB, NextCluster) == 0) {
              StartCluster = pFATInfo->NextFreeCluster;
            }
          }
#if FS_SUPPORT_TEST
        }
#endif // FS_SUPPORT_TEST
        //
        // Check if we have an other cluster in the chain or if we need to allocate an other one.
        //
        NewCluster = FS_FAT_FindFreeCluster(pVolume, pSB, StartCluster, pFile);
        if (NewCluster == 0u) {
          r = FS_ERRCODE_VOLUME_FULL;
          pFile->Error = (I16)r;              // Error, no more free space on storage.
          break;
        }
#if FS_FAT_SUPPORT_FREE_CLUSTER_CACHE
        if (WriteMode != FS_WRITEMODE_FAST)
#endif // FS_FAT_SUPPORT_FREE_CLUSTER_CACHE
        {
          Result = FS_FAT_LinkCluster(pVolume, pSB, pFATData->CurClusterId, NewCluster);
          if (Result != 0) {
            pFile->Error = (I16)Result;
            r = Result;                       // Error, could not write to storage.
            break;
          }
        }
        pFATData->CurClusterId = NewCluster;
        pFATData->CurClusterIndex++;
      } while (--NumClustersToGo != 0);
    }
  }
  return r;
}

/*********************************************************************
*
*       FS_FAT_WriteDataSector
*
*  Function description
*    Writes the contents of a data sector to the storage device.
*
*  Parameters
*    SectorIndex     Index of the sector to write relative to beginning of the volume.
*    WriteToJournal  Set to 1 if data should be written to journal.
*    pSB             [IN] Contents of the data sector.
*
*  Return value
*    ==0    Sector written.
*    !=0    An error occurred.
*/
int FS_FAT_WriteDataSector(U32 SectorIndex, int WriteToJournal, FS_SB * pSB) {
  int r;

  FS__SB_SetSector(pSB, SectorIndex, FS_SECTOR_TYPE_DATA, WriteToJournal);
  r = FS__SB_Write(pSB);
  return r;
}

#if FS_SUPPORT_ENCRYPTION

/*********************************************************************
*
*       FS_FAT_WriteDataSectorEncrypted
*
*  Function description
*    Writes the encrypted contents of a data sector to the storage device.
*
*  Parameters
*    SectorIndex     Index of the sector to write relative to beginning of the volume.
*    FilePos         Actual write position in bytes.
*    NumBytesToWrite Number of bytes to be written a the given position.
*    FileSize        Actual size of the file in bytes. Typ. used when the file is truncated.
*                    In other cases it has the same value as pFileObj->Size.
*    WriteToJournal  Set to 1 if data should be written to journal.
*    pFileObj        [IN]  File object assigned to opened file.
*    pSBData         [IN]  Contents of the data sector in decrypted form.
*    pSBCrypt        Temporary buffer used for data encryption.
*
*  Return value
*    ==0    Sector written.
*    !=0    An error occurred.
*/
int FS_FAT_WriteDataSectorEncrypted(U32 SectorIndex, U32 FilePos, U32 NumBytesToWrite, U32 FileSize, int WriteToJournal, FS_FILE_OBJ * pFileObj, FS_SB * pSBData, FS_SB * pSBCrypt) {
  int            r;
  FS_SB        * pSB;
  FS_CRYPT_OBJ * pCryptObj;

  pSB = pSBData;
  pCryptObj = pFileObj->pCryptObj;
  if (pCryptObj != NULL) {
    U8          * pDest;
    U8          * pSrc;
    void        * pContext;
    U32           BlockIndex;
    U32           BytesPerSector;
    unsigned      ldBytesPerBlock;
    U32           BitsPerBlock;
    U32           NumBlocks;
    U32           NumBytesRem;
    U32           NumBytesAtOnce;
    U32           NumBytes;
    FS_FAT_INFO * pFATInfo;
    U32           NumBytesToFill;

    pContext        = pCryptObj->pContext;
    ldBytesPerBlock = pCryptObj->ldBytesPerBlock;
    pFATInfo        = &pFileObj->pVolume->FSInfo.FATInfo;
    BytesPerSector  = pFATInfo->BytesPerSector;
    BlockIndex      = (FilePos & ~(BytesPerSector - 1u)) >> ldBytesPerBlock;
    NumBytesRem     = BytesPerSector;
    NumBytes        = FileSize & ~(BytesPerSector - 1u);        // Round down to multiple of sector size.
    if (FilePos >= NumBytes) {                                  // Writing to last sector?
      //
      // Compute the number of bytes to be written in the last sector.
      //
      FileSize = SEGGER_MAX(FileSize, FilePos + NumBytesToWrite);
      NumBytes = FileSize & (BytesPerSector - 1u);
      if (NumBytes != 0u) {
        NumBytesRem = NumBytes;
      }
    }
    NumBytesToFill = BytesPerSector - NumBytesRem;
    pFileObj->SizeEncrypted = FileSize;
    NumBlocks = NumBytesRem >> ldBytesPerBlock;
    pDest     = FS__SB_GetBuffer(pSBCrypt);
    pSrc      = FS__SB_GetBuffer(pSBData);
    //
    // Encrypt complete blocks if possible.
    //
    if (NumBlocks != 0u) {
      NumBytesAtOnce = 1uL << ldBytesPerBlock;
      do {
        pCryptObj->pAlgoType->pfEncrypt(pContext, pDest, pSrc, NumBytesAtOnce, BlockIndex);
        pDest       += NumBytesAtOnce;
        pSrc        += NumBytesAtOnce;
        NumBytesRem -= NumBytesAtOnce;
        ++BlockIndex;
      } while (--NumBlocks != 0u);
    }
    //
    // Encrypt the last incomplete block.
    //
    if (NumBytesRem != 0u) {
      //
      // The number of bytes remaining to be encrypted is rounded down to a multiple of encryption block size.
      //
      BitsPerBlock   = pCryptObj->pAlgoType->BitsPerBlock;
      NumBytesAtOnce = NumBytesRem & ~((BitsPerBlock >> 3) - 1u);
      if (NumBytesAtOnce != 0u) {
        pCryptObj->pAlgoType->pfEncrypt(pContext, pDest, pSrc, NumBytesAtOnce, BlockIndex);
        pDest       += NumBytesAtOnce;
        pSrc        += NumBytesAtOnce;
        NumBytesRem -= NumBytesAtOnce;
        ++BlockIndex;               // Required for compatibility with older versions.
      }
    }
    //
    // The last bytes written to a sector which are not multiple of encryption block size
    // are encrypted separately using a simple encryption algorithm.
    //
    if (NumBytesRem != 0u) {
      const U8 * pFirstKey;
      U32        Off;
      U8       * pBuffer;

      pFirstKey = NULL;
      pBuffer   = FS__SB_GetBuffer(pSBData);
      Off       = (U32)SEGGER_PTR_DISTANCE(pSrc, pBuffer);      // MISRA deviation D:106
      if ((Off & ((1uL << ldBytesPerBlock) - 1u)) != 0u) {      // Not the beginning of an encryption block?
        pFirstKey = pSrc - 1;
      }
      FS__CRYPT_EncryptBytes(pDest, pSrc, NumBytesRem, (U8)BlockIndex, pFirstKey);
      pDest += NumBytesRem;
    }
    if (NumBytesToFill != 0u) {
      FS_MEMSET(pDest, FS_FILL_PATTERN_UNUSED_DATA, NumBytesToFill);
    }
    pSB = pSBCrypt;
  }
  r = FS_FAT_WriteDataSector(SectorIndex, WriteToJournal, pSB);
  return r;
}

#endif // FS_SUPPORT_ENCRYPTION

/*********************************************************************
*
*       FS_FAT_UpdateDirtyFlagIfRequired
*
*  Function description
*    Changes the value of a flag in the boot sector which is used
*    by Windows to determine if the volume has been mounted correctly.
*/
void FS_FAT_UpdateDirtyFlagIfRequired(FS_VOLUME * pVolume, int IsDirty) {
#if FS_FAT_UPDATE_DIRTY_FLAG
  {
    FS_FAT_INFO * pFATInfo;
    unsigned      FATType;
    int           IsDirtyOld;

    pFATInfo   = &pVolume->FSInfo.FATInfo;
    FATType    = pFATInfo->FATType;
    IsDirtyOld = (int)pFATInfo->IsDirty;
    if ((FAT_UpdateDirtyFlag != 0u) && (IsDirtyOld != IsDirty)) {
      FS_SB   sb;
      U8    * pBuffer;

      (void)FS__SB_Create(&sb, pVolume);
      //
      // Update the dirty flag on the storage medium.
      //
      FS__SB_SetSector(&sb, SECTOR_INDEX_BPB, FS_SECTOR_TYPE_MAN, 1);
      if (FS__SB_Read(&sb) == 0) {
        unsigned Flags;
        int      Off;

        //
        // The flags are located at different offsets on FAT16 and FAT32.
        //
        if (FATType == FS_FAT_TYPE_FAT32) {
          Off = BPB_OFF_FAT32_RESERVED1;
        } else {
          Off = BPB_OFF_FAT16_RESERVED1;
        }
        //
        // Store the flag value.
        //
        pBuffer = FS__SB_GetBuffer(&sb);
        Flags = pBuffer[Off];
        if (IsDirty != 0) {
          Flags |=  FAT_WRITE_IN_PROGRESS;
        } else {
          Flags &= ~FAT_WRITE_IN_PROGRESS;
        }
        pBuffer[Off] = (U8)Flags;
        FS__SB_MarkDirty(&sb);
        pFATInfo->IsDirty = (U8)IsDirty;
      }
      FS__SB_Delete(&sb);
    }
  }
#else
  FS_USE_PARA(pVolume);
  FS_USE_PARA(IsDirty);
#endif // FS_FAT_UPDATE_DIRTY_FLAG
}

/*********************************************************************
*
*       FS_FAT_Write
*
*  Function description
*    FS internal function. Write data to a file.
*
*  Parameters
*    pFile        Handle to opened file.
*    pData        Pointer to data, which will be written to the file.
*    NumBytes     Number of bytes to write to file.
*
*  Return value
*    Number of bytes written.
*
*  Notes
*    (1) pFile is not checked for validity.
*/
U32 FS_FAT_Write(FS_FILE * pFile, const void * pData, U32 NumBytes) {
  U32           NumBytesWritten;
  FS_SB         sbData;          // Sector buffer for data
  FS_SB         sbFAT;           // Sector buffer for FAT handling
#if FS_SUPPORT_ENCRYPTION
  FS_SB         sbCrypt;         // Sector buffer for encryption
#endif
  FS_SB       * pSBCrypt;
  FS_FILE_OBJ * pFileObj;
  FS_VOLUME   * pVolume;
  U32           FilePos;
  U32           NumBytesAvail;
  int           r;

  pFileObj = pFile->pFileObj;
  pVolume  = pFileObj->pVolume;
  //
  // Check if file status is O.K..
  // If not, return.
  //
  if ((pFile->Error != FS_ERRCODE_EOF) && (pFile->Error != FS_ERRCODE_OK)) {
    return 0;                 // Error
  }
  //
  // Check if the application tries to write beyond the 4GB file size limit.
  //
  FilePos = (U32)pFile->FilePos;
  NumBytesAvail = FAT_MAX_FILE_SIZE - FilePos;
  if (NumBytes > NumBytesAvail) {
    NumBytes = NumBytesAvail;
    if (pFile->Error == 0) {
      pFile->Error = FS_ERRCODE_FILE_TOO_LARGE;
    }
    if (NumBytes == 0u) {
      return 0;               // Error, could not write any data. Maximum file size exceeded.
    }
  }
  //
  // Allocate sector buffers.
  //
  (void)FS__SB_Create(&sbFAT,   pVolume);
  (void)FS__SB_Create(&sbData,  pVolume);
#if FS_SUPPORT_ENCRYPTION
  (void)FS__SB_Create(&sbCrypt, pVolume);
  pSBCrypt = &sbCrypt;
#else
  pSBCrypt = NULL;
#endif
  //
  // Mark the volume as dirty.
  //
  FS_FAT_UpdateDirtyFlagIfRequired(pVolume, 1);
  //
  // Do the work in a static subroutine
  //
  NumBytesWritten = _WriteData(SEGGER_CONSTPTR2PTR(const U8, pData), NumBytes, pFile, &sbData, &sbFAT, pSBCrypt);     // MISRA deviation D:103[b]
  //
  // If less bytes have been written than intended
  //   - Set error code in file structure (unless already set)
  //   - Invalidate the Current cluster Id to make sure we read allocation list from start next time we read
  //
  if (NumBytesWritten != NumBytes) {
    if (pFile->Error == 0) {
      pFile->Error = FS_ERRCODE_WRITE_FAILURE;
    }
  }
  //
  // Cleanup
  //
  FS__SB_Delete(&sbFAT);
  if (pFile->Error == 0) {
    r = FS__SB_GetError(&sbFAT);
    if (r != 0) {
      pFile->Error    = (I16)r;
      NumBytesWritten = 0;
    }
  }
  FS__SB_Delete(&sbData);
  if (pFile->Error == 0) {
    r = FS__SB_GetError(&sbData);
    if (r != 0) {
      pFile->Error    = (I16)r;
      NumBytesWritten = 0;
    }
  }
#if FS_SUPPORT_ENCRYPTION
  FS__SB_Delete(&sbCrypt);
  if (pFile->Error == 0) {
    r = FS__SB_GetError(&sbCrypt);
    if (r != 0) {
      pFile->Error    = (I16)r;
      NumBytesWritten = 0;
    }
  }
#endif
  return NumBytesWritten;
}

/*********************************************************************
*
*       FS_FAT_CloseFile
*
*  Function description
*    FS internal function. Close a file referred by a file pointer.
*
*  Parameters
*    pFile    File handle to close.
*/
int FS_FAT_CloseFile(FS_FILE * pFile) {
  FS_FILE_OBJ  * pFileObj;
  FS_VOLUME    * pVolume;
  FS_WRITEMODE   WriteMode;
  FS_SB          sb;
  int            r;
  int            Result;

  r         = 0;            // Set to indicate success.
  pFileObj  = pFile->pFileObj;
  pVolume   = pFileObj->pVolume;
  WriteMode = FS__GetFileWriteModeEx(pVolume);
  if ((pFile->Error == FS_ERRCODE_OK) || (pFile->Error == FS_ERRCODE_EOF)) {
    //
    // Update directory if the file data has been modified.
    //
    if ((pFile->AccessFlags & FS_FILE_ACCESS_FLAGS_AW) != 0u) {
      (void)FS__SB_Create(&sb, pVolume);
      if (pFile->IsDirUpdateRequired != 0u) {
        //
        // Mark the volume as dirty.
        //
        FS_FAT_UpdateDirtyFlagIfRequired(pVolume, 1);
        //
        // Write the directory entry to storage.
        //
        Result = FS_FAT_UpdateDirEntry(pFileObj, &sb, 1);
        if (Result != 0) {
          r = Result;
        } else {
          pFile->IsDirUpdateRequired = 0;
        }
      }
      if (WriteMode == FS_WRITEMODE_FAST) {
        //
        // Mark the volume as dirty.
        //
        FS_FAT_UpdateDirtyFlagIfRequired(pVolume, 1);
        //
        // Write the cached allocation table entries to storage.
        //
#if FS_FAT_SUPPORT_FREE_CLUSTER_CACHE
        Result = FS_FAT_SyncAT(pVolume, &sb);
        if (Result != 0) {
          r = Result;
        }
#endif // FS_FAT_SUPPORT_FREE_CLUSTER_CACHE
      }
      FS__SB_Delete(&sb);
      Result = FS__SB_GetError(&sb);
      if (Result != 0) {
        r = Result;
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       FS_FAT_Clean
*
*  Function description
*    Cleans the file system of a volume. If any pending operations need to be done to
*    the file system (eg. Updating the FSInfo on FAT32 media), this is done
*    in this function.
*
*  Parameters
*    pVolume      Pointer to a mounted volume.
*/
void FS_FAT_Clean(FS_VOLUME * pVolume) {
  _UpdateFSInfoSectorIfRequired(pVolume);
  FS_FAT_UpdateDirtyFlagIfRequired(pVolume, 0);
}

/*************************** End of file ****************************/

