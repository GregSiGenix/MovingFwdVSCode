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
File        : FS_FAT_Misc.c
Purpose     : File system's FAT File System Layer misc routines
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
*       Public data
*
**********************************************************************
*/
const FAT_DIRENTRY_API * FAT_pDirEntryAPI     = &FAT_SFN_API;
#if FS_FAT_USE_FSINFO_SECTOR
  U8                     FAT_UseFSInfoSector  = 1;  // Allows the user to enable/disable at runtime the use of FSInfo sector to get the number of free clusters.
#endif
#if FS_MAINTAIN_FAT_COPY
  U8                     FAT_MaintainFATCopy  = 1;  // Allows the user to enable/disable at runtime the update of the second allocation table.
#endif
#if FS_FAT_PERMIT_RO_FILE_MOVE
  U8                     FAT_PermitROFileMove = 1;  // Allows the user to move (and rename) the files/directories with the read-only attribute set.
#endif
#if FS_FAT_UPDATE_DIRTY_FLAG
  U8                     FAT_UpdateDirtyFlag  = 1;  // Allows the user to enable/disable at runtime the update of the flag which indicates that a volume was unmounted correctly.
#endif

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

//lint -efunc(818, _IsValidBPB) pBuffer could be declared as pointing to const N:104. Rationale: pBuffer cannot be declared as pointing to const because it is used to read data from the FSInfo sector.
//lint -efunc(818, FS_FAT_FindFreeCluster) Pointer parameter pFile could be declared as pointing to const N:104. Rationale: not possible because the file handle is used when the support for free cluster cache is enabled.
//lint -efunc(818, FS_FAT_FreeSectors) pBuffer could be declared as pointing to const N:104. Rationale: pVolume cannot be declared as pointing to const because some of the called functions require a non-const parameter.

/*********************************************************************
*
*       _ld
*/
static unsigned _ld(U32 Value) {
  unsigned i;

  for (i = 0; i < 32u; i++) {
    if ((1uL << i) == Value) {
      break;
    }
  }
  return i;
}

/*********************************************************************
*
*       _ClusterId2FATOff
*
*  Function description
*    Converts a cluster id to a byte offset in the allocation table.
*/
static U32 _ClusterId2FATOff(unsigned FATType, U32 ClusterId) {
  U32 Off;

  switch (FATType) {
  case FS_FAT_TYPE_FAT12:
    Off = ClusterId + (ClusterId >> 1);
    break;
  case FS_FAT_TYPE_FAT16:
    Off = ClusterId << 1;
    break;
  default:
    Off = ClusterId << 2;
    break;
  }
  return Off;
}

/*********************************************************************
*
*       _SetFATSector
*
*  Function description
*    Assigns a FAT sector to a sector buffer
*/
static void _SetFATSector(FS_SB * pSB, U32 SectorIndex, FS_FAT_INFO * pFATInfo) {
  FS_USE_PARA(pFATInfo);
  FS__SB_SetSector(pSB, SectorIndex, FS_SECTOR_TYPE_MAN, 1);
#if FS_MAINTAIN_FAT_COPY
  {
    U32 OffSectorCopy;

    OffSectorCopy = 0;
    if (FAT_MaintainFATCopy != 0u) {
      OffSectorCopy = pFATInfo->FATSize;
    }
    FS__SB_SetOffSectorCopy(pSB, OffSectorCopy);
  }
#endif
}

/*********************************************************************
*
*       _WriteFATEntry
*
*  Function description
*    Modifies an entry in the allocation table.
*
*  Parameters
*    pVolume      Identifies the volume containing the allocation table.
*    pSB          Sector buffer for read and write operations.
*    ClusterId    Id of the cluster to write. If it is invalid ( == 0),
*                 the routine does nothing. This is permitted !
*    Value        This is the value to be stored to allocation table entry.
*
*  Return value
*    ==0   OK, cluster value stored.
*    !=0   Error code indicating the failure reason.
*/
static int _WriteFATEntry(FS_VOLUME * pVolume, FS_SB * pSB, U32 ClusterId, U32 Value) {
  U32           SectorIndex;
  U32           Off;          // Total offset in bytes
  unsigned      SectorOff;    // Offset within the sector
  U8          * pData;
  FS_FAT_INFO * pFATInfo;
  U32           ValueOld;     // Previous value of this entry.
  U32           LastClusterId;
  U32           NumSectors;
  U8          * pBuffer;

  //
  // Make sure that we do not write outside of the allocation table.
  //
  pFATInfo      = &pVolume->FSInfo.FATInfo;
  LastClusterId = (pFATInfo->NumClusters + FAT_FIRST_CLUSTER) - 1u;
  if ((ClusterId < FAT_FIRST_CLUSTER) || (ClusterId > LastClusterId)) {
    FS_DEBUG_ERROROUT((FS_MTYPE_FS, "FAT: _WriteFATEntry: Cluster id out of bounds (%lu not in [%lu, %lu]).", ClusterId, FAT_FIRST_CLUSTER, LastClusterId));
#if FS_SUPPORT_TEST
    FS_X_PANIC(FS_ERRCODE_INVALID_CLUSTER_CHAIN);
#else
    return FS_ERRCODE_INVALID_CLUSTER_CHAIN;     // Error, Invalid cluster id.
#endif
  }
  //
  // Make sure that we do not create a closed cluster chain.
  //
  if (ClusterId == Value) {
    FS_DEBUG_ERROROUT((FS_MTYPE_FS, "FAT: _WriteFATEntry: Cluster id references itself (%lu).", ClusterId));
#if FS_SUPPORT_TEST
    FS_X_PANIC(FS_ERRCODE_INVALID_CLUSTER_CHAIN);
#else
    return FS_ERRCODE_INVALID_CLUSTER_CHAIN;     // Error, Invalid cluster value.
#endif
  }
  Off        = _ClusterId2FATOff(pFATInfo->FATType, ClusterId);
  NumSectors = Off >> pFATInfo->ldBytesPerSector;
  if (NumSectors >= pFATInfo->FATSize) {
#if FS_SUPPORT_TEST
    FS_X_PANIC(FS_ERRCODE_INVALID_CLUSTER_CHAIN);
#else
    FS_DEBUG_ERROROUT((FS_MTYPE_FS, "FAT: _WriteFATEntry: AT sector out of bounds (%lu not in [0, %lu]).", NumSectors, pFATInfo->FATSize - 1u));
    return FS_ERRCODE_INVALID_CLUSTER_CHAIN;     // Error, out of FAT bounds.
#endif
  }
  //
  // Read the FAT sector.
  //
  SectorIndex = pFATInfo->RsvdSecCnt + NumSectors;
  _SetFATSector(pSB, SectorIndex, pFATInfo);
  (void)FS__SB_Read(pSB);
  if (FS__SB_GetError(pSB) != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_FS, "FAT: _WriteFATEntry: Could not read sector."));
    return FS_ERRCODE_READ_FAILURE;     // Error, sector read failed.
  }
  FS_DEBUG_LOG((FS_MTYPE_FS, "FAT: WRITE_AT ClusterId: %lu, Value: %lu.\n", ClusterId, Value));
  SectorOff = Off & ((U32)pFATInfo->BytesPerSector - 1u);
  pBuffer = FS__SB_GetBuffer(pSB);
  pData = pBuffer + SectorOff;
#if FS_SUPPORT_FREE_SECTOR
  //
  // Inform the lower layer about the unused cluster
  //
  if (pVolume->FreeSector != 0u) {
    if (Value == 0u) {
      U32            Sector;
      FS_PARTITION * pPart;

      pPart      = &pVolume->Partition;
      Sector     = FS_FAT_ClusterId2SectorNo(pFATInfo, ClusterId);
      NumSectors = pFATInfo->SectorsPerCluster;
      (void)FS_LB_FreeSectorsPart(pPart, Sector, NumSectors);
    }
  }
#endif // FS_SUPPORT_FREE_SECTOR
#if FS_SUPPORT_JOURNAL
  {
    FS_JOURNAL_DATA * pJournalData;

    pJournalData = &pVolume->Partition.Device.Data.JournalData;
    //
    // Per default, the file system writes new data to original destination on the storage device.
    // The journal is bypassed in this case which helps increase the write performance. This optimization
    // cannot be applied if a cluster is freed and allocated in the same journal transaction.
    // We keep track of the range of freed clusters and disable the write optimization as soon as
    // the file system tries to allocate a cluster from this range.
    //
    if (Value == 0u) {
      //
      // Cluster freed. Update the range of free clusters.
      //
      if (ClusterId < pJournalData->MinClusterId) {
        pJournalData->MinClusterId = ClusterId;
      }
      if (ClusterId > pJournalData->MaxClusterId) {
        pJournalData->MaxClusterId = ClusterId;
      }
    } else {
      //
      // Cluster allocated. Enable writing to journal if a cluster is allocated that has
      // been freed in the current journal transaction.
      //
      if ((ClusterId >= pJournalData->MinClusterId) && (ClusterId <= pJournalData->MaxClusterId)) {
        pJournalData->IsNewDataLogged = 1;
      }
    }
  }
#endif // FS_SUPPORT_JOURNAL
  //
  // Update the position of the next free cluster in the allocation table.
  //
  if (Value == 0u) {
    if (ClusterId < pFATInfo->NextFreeCluster) {
#if FS_SUPPORT_JOURNAL
      FS_JOURNAL_DATA * pJournalData;

      //
      // OPTIMIZATION: If a journal transaction is active, we do not update
      // the next free cluster in order to avoid that we allocate the same
      // cluster again. In this way we can bypass the journal when writing new data.
      //
      pJournalData = &pVolume->Partition.Device.Data.JournalData;
      if (pJournalData->IsTransactionNested == 0u) {
        pFATInfo->NextFreeCluster = ClusterId;
      }
#else
      pFATInfo->NextFreeCluster = ClusterId;
#endif
    }
  } else {
    pFATInfo->NextFreeCluster = ClusterId + 1u;
  }
  //
  // Perform the actual write operation
  //
  switch (pFATInfo->FATType) {
  case FS_FAT_TYPE_FAT32:
    ValueOld = FS_LoadU32LE(pData);
    if (ValueOld != Value) {
      //
      // According to FAT specification the 4 most significant bits must be left unchanged.
      //
      Value = (ValueOld & ~FAT32_CLUSTER_ID_MASK) | (Value & FAT32_CLUSTER_ID_MASK);
      FS_StoreU32LE(pData, Value);
      FS__SB_MarkDirty(pSB);
    }
    break;
  case FS_FAT_TYPE_FAT16:
    ValueOld = FS_LoadU16LE(pData);
    if (ValueOld != Value) {
      FS_StoreU16LE(pData, (U16)Value);
      FS__SB_MarkDirty(pSB);
    }
    break;
  default:
    FS__SB_MarkDirty(pSB);
    if ((ClusterId & 1u) != 0u) {
      ValueOld = (U32)*pData >> 4;
      *pData = (U8)(((U32)*pData & 0xFu) | ((Value & 0xFu) << 4));
      pData++;
      if (SectorOff == ((unsigned)pFATInfo->BytesPerSector - 1u)) {   // With FAT12, the next byte could be in the next sector.
        _SetFATSector(pSB, SectorIndex + 1u, pFATInfo);
        (void)FS__SB_Read(pSB);
        pData = FS__SB_GetBuffer(pSB);
      }
      ValueOld |= (U32)*pData << 4;
      *pData = (U8)(Value >> 4);
    } else {
      ValueOld = *pData;
      *pData = (U8)Value;
      pData++;
      if (SectorOff == ((unsigned)pFATInfo->BytesPerSector - 1u)) {   // With FAT12, the next byte could be in the next sector.
        _SetFATSector(pSB, SectorIndex + 1u, pFATInfo);
        (void)FS__SB_Read(pSB);
        pData = FS__SB_GetBuffer(pSB);
      }
      ValueOld |= ((U32)*pData & 0xFu) << 8;
      *pData = (U8)(((U32)*pData & 0xF0u) | ((Value >> 8) & 0xFu));
    }
    FS__SB_MarkDirty(pSB);
    break;
  }
#if FS_FAT_USE_FSINFO_SECTOR
  {
    FAT_FSINFO_SECTOR * pFSInfoSector;
    int                 r;

    pFSInfoSector = &pFATInfo->FSInfoSector;
    if (   (FAT_UseFSInfoSector != 0u)
        && (pFSInfoSector->IsPresent != 0u)
        && (pFSInfoSector->IsUpdateRequired == 0u)) {

      FS__SB_SetSector(pSB, pFSInfoSector->SectorIndex, FS_SECTOR_TYPE_MAN, 1);
      r = FS__SB_Read(pSB);
      if (r == 0) {
        //
        // The number of free clusters is invalidated here and set to the correct
        // value when the volume is either unmounted or synchronized.
        //
        pBuffer = FS__SB_GetBuffer(pSB);
        FS_StoreU32LE(&pBuffer[FSINFO_OFF_FREE_CLUSTERS], NUM_FREE_CLUSTERS_INVALID);
        FS__SB_MarkDirty(pSB);
        pFSInfoSector->IsUpdateRequired = 1;
      }
    }
  }
#endif
  //
  // Update FATInfo, NumFreeClusters
  //
  if (pFATInfo->NumFreeClusters != NUM_FREE_CLUSTERS_INVALID) {
    if (ValueOld != 0u) {
      pFATInfo->NumFreeClusters++;
    }
    if (Value != 0u) {
      pFATInfo->NumFreeClusters--;
    }
  }
  pFATInfo->WriteCntAT++;
  return 0;                   /* O.K. */
}

/*********************************************************************
*
*       _SFN_ReadDirEntryInfo
*
*  Function description
*    Searches for short directory entry and returns information about it.
*
*  Return value
*    ==1   End of directory reached.
*    ==0   OK, information about the directory entry returned.
*    < 0   Error code indicating the failure reason.
*/
static int _SFN_ReadDirEntryInfo(FS_DIR_OBJ * pDir, FS_DIRENTRY_INFO * pDirEntryInfo, FS_DIR_POS * pDirPosLFN, FS_SB * pSB) {
  FS_FAT_DENTRY * pDirEntry;
  U32             DirIndex;
  FS_FAT_INFO   * pFATInfo;
  FS_VOLUME     * pVolume;
  int             r;
  unsigned        Attr;

  pVolume    = pDir->pVolume;
  pFATInfo   = &pVolume->FSInfo.FATInfo;
  DirIndex   = pDir->DirPos.DirEntryIndex;
  FS_FAT_InvalidateDirPos(pDirPosLFN);
  if (DirIndex == 0u) {
    FS_FAT_InitDirEntryScan(pFATInfo, &pDir->DirPos, pDir->DirPos.FirstClusterId);
  }
  for (;;) {
    pDirEntry = FS_FAT_GetDirEntry(pVolume, pSB, &pDir->DirPos);
    FS_FAT_IncDirPos(&pDir->DirPos);
    if (pDirEntry == NULL) {
      r = FS__SB_GetError(pSB);
      if (r == 0) {
        r = FS_ERRCODE_READ_FAILURE;
      }
      break;
    }
    if (pDirEntry->Data[0] == 0x00u) {      // Last entry found?
      r = 1;
      break;
    }
    if (pDirEntry->Data[0] != (U8)0xE5) {   // Not a deleted file?
      Attr = pDirEntry->Data[DIR_ENTRY_OFF_ATTRIBUTES];
      if ((Attr != FS_FAT_ATTR_LONGNAME) && ((Attr & FS_FAT_ATTR_VOLUME_ID) != FS_FAT_ATTR_VOLUME_ID)) { /* Also not a long entry nor a volume id, so it is a valid entry */
        FS_FAT_LoadShortName(pDirEntryInfo->sFileName, (unsigned)pDirEntryInfo->SizeofFileName, &pDirEntry->Data[0]);
        FS_FAT_CopyDirEntryInfo(pDirEntry, pDirEntryInfo);
        r = 0;
        break;
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _SFN_FindDirEntry
*
*  Function description
*    Tries to locate the directory entry in the specified directory.
*    The short name space is searched first;
*    if LFN support is activated, the long name space is search
*    if no short name match was found.
*
*  Parameters
*    pVolume              Volume information.
*    pSB                  Sector buffer for read and write operations.
*    pEntryName           Directory entry name.
*    Len                  Maximum number of characters in pEntryName to consider.
*    DirStart             Id of the first cluster allocated to directory.
*    pDirPos              [OUT] Position of the directory entry in the parent directory.
*    AttrRequired         Directory attributes required to match the directory entry.
*    pDirPosLFN           [OUT] Position of the first directory entry that stores the long file name (not used).
*
*  Return value
*    !=NULL       Pointer to directory entry (in the smart buffer).
*    ==NULL       Entry not found.
*/
static FS_FAT_DENTRY * _SFN_FindDirEntry(FS_VOLUME * pVolume, FS_SB * pSB, const char * pEntryName, int Len, U32 DirStart, FS_DIR_POS * pDirPos, unsigned AttrRequired, FS_DIR_POS * pDirPosLFN) {
  FS_FAT_DENTRY * pDirEntry;

  FS_FAT_InvalidateDirPos(pDirPosLFN);
  pDirEntry = FS_FAT_FindDirEntryShort(pVolume, pSB, pEntryName, Len, DirStart, pDirPos, AttrRequired);
  return pDirEntry;
}

/*********************************************************************
*
*       _SFN_CreateDirEntry
*/
static FS_FAT_DENTRY * _SFN_CreateDirEntry(FS_VOLUME * pVolume, FS_SB * pSB, const char * pFileName, U32 DirStart, U32 ClusterId, unsigned Attributes, U32 Size, unsigned Time, unsigned Date) {
  FS_FAT_DENTRY * pDirEntry;
  FS_83NAME       FATEntryName;

  pDirEntry = NULL;
  if (FS_FAT_MakeShortName(&FATEntryName, pFileName, 0, 0) == 0) {
    pDirEntry = FS_FAT_FindEmptyDirEntry(pVolume, pSB, DirStart);
    if (pDirEntry != NULL) {
      FS_FAT_WriteDirEntry83(pDirEntry, &FATEntryName, ClusterId, Attributes, Size, Time, Date, 0);
      //
      // Update the directory entry to storage.
      //
      FS__SB_MarkDirty(pSB);
    }
  } else {
    //
    // Error, could not create directory entry.
    //
    FS_DEBUG_ERROROUT((FS_MTYPE_FS, "FAT: _SFN_CreateDirEntry: File or directory name is not a legal 8.3 name (Either too long or invalid characters)."));
  }
  return pDirEntry;
}

/*********************************************************************
*
*       _IsValidBPB
*
*  Function description
*    Checks the BIOS Parameter Block (BPB) of the volume
*    and initialize the FS specific information in the volume structure.
*
*  Parameters
*    pVolume    Pointer to volume structure.
*    pBuffer    Pointer to buffer with read BPB.
*
*  Return value
*    1    OK, BPB contains valid information.
*    0    Error, BPB contains invalid information.
*/
static int _IsValidBPB(FS_VOLUME * pVolume, U8 * pBuffer) {
  FS_FAT_INFO * pFATInfo;
  unsigned      BytesPerSector;
  U32           FirstDataSector;
  U32           NumSectors;
  U32           NumClusters;
  U32           FATSize;
  U32           FirstSectorAfterFAT;
  unsigned      SectorsPerCluster;
  unsigned      Signature;
  unsigned      FATType;
  unsigned      i;
  unsigned      NumFATs;

  //
  // Check if this a valid BPB
  //
  Signature = FS_LoadU16LE(&pBuffer[BPB_OFF_SIGNATURE]);
  if (Signature != 0xAA55u) {
    FS_DEBUG_WARN((FS_MTYPE_FS, "FAT: _IsValidBPB: Signature invalid or no signature. High-level format required."));
    return 0;                         // Error, not a valid BPB
  }
  BytesPerSector = FS_LoadU16LE(&pBuffer[BPB_OFF_BYTES_PER_SECTOR]);
  if (((BytesPerSector & 0xFE00u) == 0u) || (BytesPerSector > FS_Global.MaxSectorSize)) {  // The sector size must a multiple of 512 bytes.
    FS_DEBUG_ERROROUT((FS_MTYPE_FS, "FAT: _IsValidBPB: BytesPerSector (%d) is not valid.", BytesPerSector));
    return 0;                         // Error, not a valid BPB
  }
  NumFATs = pBuffer[BPB_OFF_NUM_FATS];
  if ((NumFATs != 1u) && (NumFATs != 2u)) {
    FS_DEBUG_ERROROUT((FS_MTYPE_FS, "FAT: _IsValidBPB: Only 1 or 2 FATs supported."));
    return 0;                         // Error, not a valid BPB
  }
  SectorsPerCluster = pBuffer[BPB_OFF_SECTOR_PER_CLUSTER];
  if (SectorsPerCluster == 0u) {
    FS_DEBUG_ERROROUT((FS_MTYPE_FS, "FAT: _IsValidBPB: SectorsPerCluster == 0."));
    return 0;                         // Error, not a valid BPB
  }
  //
  // Analyze BPB and fill the FATInfo structure in pVolume.
  //
  pFATInfo = &pVolume->FSInfo.FATInfo;
  FS_MEMSET(pFATInfo, 0, sizeof(FS_FAT_INFO));
  NumSectors = FS_LoadU16LE(&pBuffer[BPB_OFF_NUMSECTORS_16BIT]);      // RSVD + FAT + ROOT + FATA (<64k)
  if (NumSectors == 0u) {
    NumSectors = FS_LoadU32LE(&pBuffer[BPB_OFF_NUMSECTORS_32BIT]);    // RSVD + FAT + ROOT + FATA (>=64k)
  }
  FATSize = FS_LoadU16LE(&pBuffer[BPB_OFF_FATSIZE_16BIT]);            // Number of FAT sectors
  if (FATSize == 0u) {
    unsigned ExtFlags;

    ExtFlags = FS_LoadU16LE(&pBuffer[BPB_OFF_FAT32_EXTFLAGS]);        // Mirroring info
    //
    // Check FAT mirroring flags for FAT32 volumes
    //
    if ((ExtFlags & 0x008Fu) != 0u) {
      FS_DEBUG_ERROROUT((FS_MTYPE_FS, "FAT: _IsValidBPB: FAT32 feature \"FAT mirroring\" not supported."));
      return 0;                       // Error, not a valid BPB
    }
    //
    // FAT32
    //
    FATSize                 = FS_LoadU32LE(&pBuffer[BPB_OFF_FATSIZE_32BIT]);    // Number of FAT sectors
    pFATInfo->RootDirPos    = FS_LoadU32LE(&pBuffer[BPB_OFF_ROOTDIR_CLUSTER]);  // Root directory cluster for FAT32. Typically cluster 2.
  }
  pFATInfo->NumSectors        = NumSectors;
  pFATInfo->FATSize           = FATSize;
  pFATInfo->NumFATs           = (U8)NumFATs;
  pFATInfo->SectorsPerCluster = (U8)SectorsPerCluster;
  pFATInfo->RsvdSecCnt        = FS_LoadU16LE(&pBuffer[14]);                // 1 for FAT12 and FAT16
  pFATInfo->RootEntCnt        = FS_LoadU16LE(&pBuffer[17]);                // Number of root directory entries
  pFATInfo->BytesPerSector    = (U16)BytesPerSector;
  FirstSectorAfterFAT         = pFATInfo->RsvdSecCnt + pFATInfo->NumFATs * FATSize;
  FirstDataSector             = FirstSectorAfterFAT  + (U32)pFATInfo->RootEntCnt / ((U32)BytesPerSector >> DIR_ENTRY_SHIFT);  // Add number of sectors of root directory
  pFATInfo->FirstDataSector   = FirstDataSector;
  //
  // Compute the shift for bytes per sector.
  //
  for (i = 9; i < 16u; i++) {
    if ((1uL << i) == BytesPerSector) {
      pFATInfo->ldBytesPerSector = (U16)i;
      break;
    }
  }
  //
  // Compute the number of clusters.
  //
  NumClusters = (NumSectors - FirstDataSector) / SectorsPerCluster;
  pFATInfo->NumClusters       = NumClusters;
  pFATInfo->BytesPerCluster   = (U32)SectorsPerCluster * (U32)BytesPerSector;
  pFATInfo->ldBytesPerCluster = (U16)_ld(pFATInfo->BytesPerCluster);
  //
  // Determine the type of FAT (12/16/32), based on the number of clusters. (acc. MS spec)
  //
  FATType = FS_FAT_GetFATType(NumClusters);
  pFATInfo->FATType = (U8)FATType;
  if(FATType != FS_FAT_TYPE_FAT32) {
    pFATInfo->RootDirPos = FirstSectorAfterFAT;     // For FAT12 / FAT16
  }
  pFATInfo->NumFreeClusters = NUM_FREE_CLUSTERS_INVALID;
#if FS_FAT_USE_FSINFO_SECTOR
  if ((FAT_UseFSInfoSector != 0u) && (FATType == FS_FAT_TYPE_FAT32)) {
    unsigned            SectorIndex;
    U32                 FAT32Signature1;
    U32                 FAT32Signature2;
    U32                 FAT32Signature3;
    U32                 NumFreeClusters;
    U32                 NextFreeCluster;
    unsigned            Flags;
    int                 r;
    FAT_FSINFO_SECTOR * pFSInfoSector;
    int                 IsDirty;

    //
    // Load from the boot sector the index of the FSInfo sector and
    // the flag that indicates if the volume was correctly unmounted.
    //
    SectorIndex = FS_LoadU16LE(&pBuffer[BPB_OFF_FAT32_FSINFO_SECTOR]);
    IsDirty = 0;
    Flags = pBuffer[BPB_OFF_FAT32_RESERVED1];
    if ((Flags & FAT_WRITE_IN_PROGRESS) != 0u) {
      IsDirty = 1;
    }
    //
    // The FSInfo sector is located in the reserved area after the first sector
    // on the partition that stores format information.
    //
    if ((SectorIndex > 0u) && (SectorIndex <= pFATInfo->RsvdSecCnt)) {
      pFSInfoSector = &pFATInfo->FSInfoSector;
      pFSInfoSector->SectorIndex = (U16)SectorIndex;
      //
      // Read the information from the FSInfo sector.
      //
      r = FS_LB_ReadPart(&pVolume->Partition, SectorIndex, pBuffer, FS_SECTOR_TYPE_MAN);
      if (r == 0) {
        //
        // Use FSInfo sector only if the signatures are correct.
        //
        FAT32Signature1 = FS_LoadU32LE(&pBuffer[FSINFO_OFF_SIGNATURE_1]);
        FAT32Signature2 = FS_LoadU32LE(&pBuffer[FSINFO_OFF_SIGNATURE_2]);
        FAT32Signature3 = FS_LoadU32LE(&pBuffer[FSINFO_OFF_SIGNATURE_3]);
        if (   (FAT32Signature1 == FSINFO_SIGNATURE_1)
            && (FAT32Signature2 == FSINFO_SIGNATURE_2)
            && (FAT32Signature3 == FSINFO_SIGNATURE_3)) {
          pFSInfoSector->IsPresent = 1;
          //
          // Use the information from FSInfo sector only if the volume was correctly unmounted.
          //
          if (IsDirty == 0) {
            NextFreeCluster = FS_LoadU32LE(&pBuffer[FSINFO_OFF_NEXT_FREE_CLUSTER]);
            NumFreeClusters = FS_LoadU32LE(&pBuffer[FSINFO_OFF_FREE_CLUSTERS]);
            //
            // Use the values stored in the FSInfo sector only if they make sense.
            //
            if (NumFreeClusters <= NumClusters) {
              pFATInfo->NumFreeClusters = NumFreeClusters;
            }
            if (   (NextFreeCluster >= FAT_FIRST_CLUSTER)
                && (NextFreeCluster <= ((NumClusters + FAT_FIRST_CLUSTER) - 1u))) {
              pFATInfo->NextFreeCluster = NextFreeCluster;
            }
          }
        }
      }
    }
  }
#endif // FS_FAT_USE_FSINFO_SECTOR

#if FS_FAT_UPDATE_DIRTY_FLAG
  {
    int      Off;
    unsigned Flags;
    int      IsDirty;

    if (FATType == FS_FAT_TYPE_FAT32) {
      Off = BPB_OFF_FAT32_RESERVED1;
    } else {
      Off = BPB_OFF_FAT16_RESERVED1;
    }
    IsDirty = 0;
    Flags   = pBuffer[Off];
    if ((Flags & FAT_WRITE_IN_PROGRESS) != 0u) {
      IsDirty = 1;
    }
    pFATInfo->IsDirty = (U8)IsDirty;
  }
#endif // FS_FAT_UPDATE_DIRTY_FLAG
  return 1;                   // OK, valid BPB found.
}

/*********************************************************************
*
*       _FilePosToClusterIndex
*
*  Function description
*    Calculates the index of the cluster relative to the beginning of the file.
*
*  Parameters
*    pFile      Opened file handle.
*
*  Return value
*    Cluster index.
*
*  Additional information
*    The calculated value represents the index of the cluster corresponding
*    to the position in the file.
*/
static U32 _FilePosToClusterIndex(const FS_FILE * pFile) {
  FS_FAT_INFO * pFATInfo;

  pFATInfo = &pFile->pFileObj->pVolume->FSInfo.FATInfo;
  return (U32)pFile->FilePos >> pFATInfo->ldBytesPerCluster;
}

/*********************************************************************
*
*       _WalkAdjClusters
*
*  Function description
*    Selects the cluster that corresponds to the file position.
*
*  Parameters
*    pFile      Opened file handle.
*
*  Return value
*    Number of clusters that are missing from the chain.
*
*  Additional information
*    The cluster id and index are calculated using the information
*    about adjacent clusters stored in the file object assigned
*    to the specified file handle. In doing so, this function does
*    not perform any access to the storage device.
*
*    The calculated values are stored to the file object
*    assigned to the file handle.
*/
static U32 _WalkAdjClusters(const FS_FILE * pFile) {
  U32               CurClusterIndex;
  U32               NumClustersToWalk;
  FS_FILE_OBJ     * pFileObj;
  FS_INT_DATA_FAT * pFATData;

  pFileObj = pFile->pFileObj;
  pFATData = &pFileObj->Data.Fat;
  //
  // Calculate the cluster index corresponding to the file position.
  //
  CurClusterIndex = _FilePosToClusterIndex(pFile);
  //
  // Check if the cluster is in the cache of adjacent clusters.
  // If no, then we invalidate the cluster index stored in the
  // file object assigned to file handle.
  //
  if (CurClusterIndex < pFATData->CurClusterIndex) {
    pFATData->CurClusterIndex = CLUSTER_INDEX_INVALID;    // Invalidate the cluster index because the file position was moved back.
  }
  //
  // If necessary, initialize the cache of adjacent clusters.
  //
  if (pFATData->CurClusterIndex == CLUSTER_INDEX_INVALID) {
    pFATData->CurClusterIndex = 0;
    pFATData->CurClusterId    = pFileObj->FirstCluster;
#if FS_FAT_OPTIMIZE_LINEAR_ACCESS
    pFATData->NumAdjClusters  = 0;
#endif // FS_FAT_OPTIMIZE_LINEAR_ACCESS
  }
  //
  // Calculate the number of clusters we have to advance from
  // the old file position to the new one.
  //
  NumClustersToWalk = CurClusterIndex - pFATData->CurClusterIndex;
#if FS_FAT_OPTIMIZE_LINEAR_ACCESS
  {
    U32 NumClusters;

    NumClusters = NumClustersToWalk;
    if (NumClustersToWalk > pFATData->NumAdjClusters) {
      NumClusters = pFATData->NumAdjClusters;
    }
    //
    // If necessary, update the cache of adjacent clusters.
    //
    if (NumClusters != 0u) {
      pFATData->CurClusterId    += NumClusters;
      pFATData->CurClusterIndex += NumClusters;
      pFATData->NumAdjClusters  -= (U16)NumClusters;
      NumClustersToWalk         -= NumClusters;
    }
  }
#endif // FS_FAT_OPTIMIZE_LINEAR_ACCESS
  return NumClustersToWalk;
}

#if FS_SUPPORT_FAT

/*********************************************************************
*
*       _GrowRootDir
*
*  Function description
*    Let the root directory of a FAT32 volume grow.
*    This function shall be called after formatting the volume.
*    If the function is not called after format or called for
*    a FAT12/16 volume the function will fail.
*
*  Parameters
*    sVolumeName    Pointer to a string that specifies the volume name.
*    NumAddEntries  Number of entries to be added.
*
*  Return value
*    > 0            Number of entries added.
*    ==0            Clusters after root directory are not free.
*    ==0xFFFFFFFF   Failed (Invalid volume, volume not mountable, volume is not FAT32).
*/
static U32 _GrowRootDir(const char * sVolumeName, U32 NumAddEntries) {
  FS_VOLUME   * pVolume;
  FS_FAT_INFO * pFATInfo;
  U32           r;
  int           Result;

  r = 0xFFFFFFFFuL;       // Set to indicate an error.
  //
  // Find correct volume
  //
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    //
    // Mount the volume if necessary
    //
    if (FS__AutoMount(pVolume) == (int)FS_MOUNT_RW) {
      //
      // Check whether volume is a FAT32 volume.
      //
      pFATInfo = &pVolume->FSInfo.FATInfo;
      if (pFATInfo->FATType == FS_FAT_TYPE_FAT32) {
        U32            NumClustersReq;
        U32            NumSectors;
        U32            StartCluster;
        U32            StartSector;
        FS_SB          sb;
        U8           * pBuffer;
        FS_PARTITION * pPart;

        (void)FS__SB_Create(&sb, pVolume);
        //
        // Calculate how many clusters are necessary.
        //
        NumClustersReq = FS__DivideU32Up(NumAddEntries << DIR_ENTRY_SHIFT, pFATInfo->BytesPerCluster);
        //
        // Check whether the adjacent cluster after the root directory are available.
        //
        StartCluster = FS_FAT_FindLastCluster(pVolume, &sb, pFATInfo->RootDirPos, NULL) + 1u;
        if (FS_FAT_AllocClusterBlock(pVolume, StartCluster, NumClustersReq, &sb) == FS_ERRCODE_CLUSTER_NOT_FREE) {
          r = 0;                              // Error, could not allocate the requested number of clusters.
        } else {
          //
          // Update the FAT entry for the root directory.
          //
          Result = _WriteFATEntry(pVolume, &sb, pFATInfo->RootDirPos, StartCluster);
          if (Result == 0) {
            //
            // Let the smart buffer write its contents to storage.
            // We need the buffer to zero the new directory sectors
            //
            FS__SB_Clean(&sb);
            if (FS__SB_GetError(&sb) == 0) {
              pPart = &pVolume->Partition;
              pBuffer = FS__SB_GetBuffer(&sb);
              FS_MEMSET(pBuffer, 0x00, pFATInfo->BytesPerSector);
              StartSector = FS_FAT_ClusterId2SectorNo(pFATInfo, StartCluster);
              NumSectors  = NumClustersReq * pFATInfo->SectorsPerCluster;
              Result = FS_LB_WriteMultiplePart(pPart, StartSector, NumSectors, pBuffer, FS_SECTOR_TYPE_DIR, 1);
              FS__SB_MarkNotValid(&sb);
#if FS_SUPPORT_SECTOR_BUFFER_CACHE
              FS__InvalidateSectorBuffer(pVolume, StartSector, NumSectors);
#endif // FS_SUPPORT_SECTOR_BUFFER_CACHE
              if (Result == 0) {
                FS__SB_MarkValid(&sb, StartSector, FS_SECTOR_TYPE_DIR, 1);
                r = NumAddEntries;
              }
            }
          }
        }
        FS__SB_Delete(&sb);
      }
    }
  }
  return r;
}

#endif // FS_SUPPORT_FAT

#if FS_FAT_OPTIMIZE_DELETE

/*********************************************************************
*
*       _WriteEmptyFATSectors
*
*  Function description
*    Fills with 0s sectors of the allocation table.
*
*  Parameters
*    pSB                  Sector buffer to be used for the operation.
*    FirstFreeFATSector   Index of the first logical sector to fill.
*    LastFreeFATSector    Index of the last logical sector to fill.
*    FATSize              Number of sectors in the allocation table.
*
*  Return value
*    ==0      OK, sectors filled.
*    !=0      An error occurred.
*
*  Additional information
*    FATSize is used only when the copy of the allocation table
*    has to be updated too.
*/
static int _WriteEmptyFATSectors(FS_SB * pSB, U32 FirstFreeFATSector, U32 LastFreeFATSector, U32 FATSize) {
  int            r;
  U32            NumSectors;
  U8           * pBuffer;
  FS_PARTITION * pPart;
  FS_VOLUME    * pVolume;

  r = 0;        // Set to indicate success.
  if (LastFreeFATSector != 0xFFFFFFFFuL) {
    NumSectors = (LastFreeFATSector - FirstFreeFATSector) + 1u;
    FS__SB_Clean(pSB);
    r = FS__SB_GetError(pSB);
    if (r == 0) {
      pBuffer = FS__SB_GetBuffer(pSB);
      pVolume = FS__SB_GetVolume(pSB);
      pPart   = &pVolume->Partition;
      FS_MEMSET(pBuffer, 0, FS_Global.MaxSectorSize);
      r = FS_LB_WriteMultiplePart(pPart, FirstFreeFATSector, NumSectors, pBuffer, FS_SECTOR_TYPE_MAN, 1);
#if FS_MAINTAIN_FAT_COPY
      if (r == 0) {
        //
        // Update the copy of the allocation table if required.
        //
        if (FAT_MaintainFATCopy != 0u) {
          FirstFreeFATSector += FATSize;
          r = FS_LB_WriteMultiplePart(pPart, FirstFreeFATSector, NumSectors, pBuffer, FS_SECTOR_TYPE_MAN, 1);
        }
      }
      FS__SB_MarkNotValid(pSB);       // Invalidate in sector buffer that this sector has been read.
#if FS_SUPPORT_SECTOR_BUFFER_CACHE
      FS__InvalidateSectorBuffer(pVolume, FirstFreeFATSector, NumSectors);
#endif // FS_SUPPORT_SECTOR_BUFFER_CACHE
#else
      FS_USE_PARA(FATSize);
#endif // FS_MAINTAIN_FAT_COPY
    }
  }
  return r;
}

/*********************************************************************
*
*       _IsSectorBlank
*
*  Function description
*    Checks if all the bytes in a sector are set to 0.
*/
static int _IsSectorBlank(const U32 * pSectorBuffer, unsigned SectorSizeU32) {
  do {
    if (*pSectorBuffer++ != 0u) {
      return 0;                  // Not blank
    }
  } while (--SectorSizeU32 != 0u);
  return 1;                      // Blank
}

#endif // FS_FAT_OPTIMIZE_DELETE

#if FS_FAT_SUPPORT_FREE_CLUSTER_CACHE

/*********************************************************************
*
*       _FillFreeClusterCache
*
*  Function description
*    Calculates the number of free clusters and adds them to cache.
*
*  Parameters
*    pVolume        Volume that stores the allocation table.
*    pSB            Work sector buffer.
*    StartCluster   Id of the first free cluster.
*    pFile          Opened file that is used with the cache.
*
*  Return value
*    ==0    OK, cache filled.
*    !=0    Error code indicating the failure reason.
*
*  Additional information
*    This function is used only if file write mode is set to FS_WRITEMODE_FAST.
*
*    StartCluster has to indicate a free cluster. This cluster is added
*    to the cache without checking if it is actually free.
*/
static int _FillFreeClusterCache(FS_VOLUME * pVolume, FS_SB * pSB, U32 StartCluster, FS_FILE * pFile) {
  U32                     LastCluster;
  U32                     FirstCluster;
  U32                     Off;
  U32                     LastOff;
  U32                     iCluster;
  FS_FAT_INFO           * pFATInfo;
  FS_WRITEMODE            WriteMode;
  FS_FREE_CLUSTER_CACHE * pFreeClusterCache;
  unsigned                FATType;
  U32                     BytesPerSector;
  unsigned                ldBytesPerSector;
  U32                     SectorIndex;
  U32                     Rem;
  int                     r;
  U32                     ATEntry;
  int                     Result;

  r = 0;                  // Set to indicate success.
  if (pFile != NULL) {
    WriteMode = FS__GetFileWriteModeEx(pVolume);
    if (WriteMode == FS_WRITEMODE_FAST) {
      pFATInfo          = &pVolume->FSInfo.FATInfo;
      pFreeClusterCache = &pFATInfo->FreeClusterCache;
      FATType           = pFATInfo->FATType;
      BytesPerSector    = (U32)pFATInfo->BytesPerSector;
      ldBytesPerSector  = pFATInfo->ldBytesPerSector;
      //
      // Add StartCluster to the cache.
      //
      pFreeClusterCache->StartCluster = StartCluster;
      pFreeClusterCache->NumClustersTotal++;
      FirstCluster = StartCluster + 1u;
      //
      // Calculate the id of the last cluster to be checked.
      //
      Off     = _ClusterId2FATOff(FATType, FirstCluster);
      LastOff = BytesPerSector - 1u;
      if (FATType == FS_TYPE_FAT12) {
        //
        // The update of last AT entry in the sector requires two sector
        // write operations when it crosses a sector boundary which happens
        // at every third sector boundary (e.g. between the sectors with these
        // relative indexes: 0->1, 1->2, 3->4, 4->5, ...). We have to make sure that this
        // AT entry is not the last entry in the cache in order to reduce the number of sector
        // write operations to a minimum. Typically, the last AT entry in the cache
        // is updated twice: (1) to mark it as end of chain and (2) to link it to the
        // next cluster in the chain.
        //
        SectorIndex = Off >> ldBytesPerSector;
        (void)FS__DivModU32(SectorIndex, 3, &Rem);
        if (Rem != 2u) {
          LastOff = BytesPerSector - 2u;
        }
      }
      Off &= BytesPerSector - 1u;
      if (LastOff <= Off) {
        LastOff += BytesPerSector;
      }
      LastCluster = ((LastOff - Off) << 3) / FATType + FirstCluster;
      LastCluster = SEGGER_MIN(LastCluster, (pFATInfo->NumClusters + FAT_FIRST_CLUSTER) - 1u);
      //
      // Scan the allocation table for free clusters.
      //
      for (iCluster = FirstCluster; iCluster <= LastCluster; iCluster++) {
        ATEntry = FS_FAT_ReadFATEntry(pVolume, pSB, iCluster);
        if (ATEntry == CLUSTER_ID_INVALID) {
          r = FS_ERRCODE_READ_FAILURE;                              // Error, could not read from allocation table.
          break;
        }
        if (ATEntry != 0u) {
          break;                                                    // First used AT entry found.
        }
        pFreeClusterCache->NumClustersTotal++;
      }
      //
      // Link the first free cluster to the cluster chain of the opened file used with the cache.
      //
      if (pFreeClusterCache->NumClustersTotal != 0u) {
        pFreeClusterCache->pFile = pFile;
        LastCluster = pFile->pFileObj->Data.Fat.CurClusterId;
        if (LastCluster != 0u) {
          Result = _WriteFATEntry(pVolume, pSB, LastCluster, StartCluster);
          if (Result != 0) {
            r = Result;
          }
        }
        pFreeClusterCache->NumClustersInUse++;
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _Value2FatEntry
*/
static U32 _Value2FatEntry(unsigned FATType, U32 Cluster) {
  U32 r;

  switch (FATType) {
  case FS_FAT_TYPE_FAT12:
    r = Cluster & 0xFFFu;
    break;
  case FS_FAT_TYPE_FAT16:
    r = Cluster & 0xFFFFu;
    break;
  default:
    r = Cluster & 0xFFFFFFFuL;
    break;
  }
  return r;
}

/*********************************************************************
*
*       _ReadFromFreeClusterCache
*/
static int _ReadFromFreeClusterCache(FS_VOLUME * pVolume, U32 ClusterId, U32 * pFATEntry) {
  FS_FAT_INFO * pFATInfo;
  U32           FreeClusterStart;
  U32           NumFreeClustersInUse;
  U32           NumFreeClustersTotal;
  int           SkipRead;
  U32           FATEntry;
  U32           LastClusterId;

  SkipRead             = 0;
  pFATInfo             = &pVolume->FSInfo.FATInfo;
  FreeClusterStart     = pFATInfo->FreeClusterCache.StartCluster;
  NumFreeClustersInUse = pFATInfo->FreeClusterCache.NumClustersInUse;
  NumFreeClustersTotal = pFATInfo->FreeClusterCache.NumClustersTotal;
  //
  // Do nothing if the free cluster cache is empty.
  //
  if (NumFreeClustersTotal != 0u) {
    //
    // Check if the cluster is located in cache.
    //
    if ((ClusterId >= FreeClusterStart) && (ClusterId < (FreeClusterStart + NumFreeClustersTotal))) {
      //
      // Calculate the FAT entry value.
      //
      if (NumFreeClustersInUse == 0u) {
        FATEntry = 0;                         // Cluster is free.
      } else {
        LastClusterId = FreeClusterStart + NumFreeClustersInUse - 1u;
        if (ClusterId == LastClusterId) {
          FATEntry = 0xFFFFFFFFuL;            // End of chain
        } else {
          if (ClusterId < LastClusterId) {
            FATEntry = ClusterId + 1u;        // Cluster in use.
          } else {
            FATEntry = 0;                     // Cluster is free.
          }
        }
      }
      //
      // Return the correct AT entry value.
      //
      if (pFATEntry != NULL) {
        *pFATEntry  = _Value2FatEntry(pFATInfo->FATType, FATEntry);
      }
      SkipRead  = 1;   // No need to read the entry from real FAT table
    }
  }
  return SkipRead;
}

/*********************************************************************
*
*       _ReadFATEntry
*
*  Function description
*    Reads the value of an allocation table entry.
*
*  Additional information
*    All the errors are reported via the error member of the sector buffer.
*/
static U32 _ReadFATEntry(FS_VOLUME * pVolume, FS_SB * pSB, U32 ClusterId) {
  int          SkipRead;
  int          ReadFromCache;
  U32          FATEntry;
  FS_WRITEMODE WriteMode;

  FATEntry      = 0;
  SkipRead      = 0;
  ReadFromCache = 0;
  WriteMode     = FS__GetFileWriteModeEx(pVolume);
  //
  // If fast write mode is configure then we have to try get the value from our internal FAT free cluster cache first.
  //
  if (WriteMode == FS_WRITEMODE_FAST) {
    ReadFromCache = 1;
  }
  if (ReadFromCache != 0) {
    SkipRead = _ReadFromFreeClusterCache(pVolume, ClusterId, &FATEntry);
  }
  if (SkipRead == 0) {
    FATEntry = FS_FAT_ReadFATEntry(pVolume, pSB, ClusterId);
  }
  return FATEntry;
}

/*********************************************************************
*
*       _FreeClusterChainFromFreeClusterCache
*
*  Function description
*    Frees clusters that were allocated form the free cluster cache.
*
*  Return value
*    Number of clusters that have been freed.
*/
static U32 _FreeClusterChainFromFreeClusterCache(FS_VOLUME * pVolume, U32 ClusterId) {
  FS_FAT_INFO           * pFATInfo;
  U32                     NumClustersInUse;
  U32                     ClusterIdFirst;
  U32                     ClusterIdLast;
  U32                     NumClustersToFree;
  FS_WRITEMODE            WriteMode;
  FS_FREE_CLUSTER_CACHE * pFreeClusterCache;

  WriteMode = FS__GetFileWriteModeEx(pVolume);
  if (WriteMode != FS_WRITEMODE_FAST) {
    return 0;                               // This feature is active only when the FAST write mode is active.
  }
  pFATInfo          = &pVolume->FSInfo.FATInfo;
  pFreeClusterCache = &pFATInfo->FreeClusterCache;
  if (pFreeClusterCache->pFile == NULL) {
    return 0;                               // The free cluster cache is empty.
  }
  NumClustersInUse = pFreeClusterCache->NumClustersInUse;
  if (NumClustersInUse == 0u) {             // No clusters were allocated from the free cluster cache.
    return 0;
  }
  NumClustersToFree = 0;
  ClusterIdFirst    = pFreeClusterCache->StartCluster;
  ClusterIdLast     = ClusterIdFirst + NumClustersInUse - 1u;
  if ((ClusterId >= ClusterIdFirst) && (ClusterId <= ClusterIdLast)) {
    NumClustersToFree  = ClusterIdLast - ClusterId + 1u;
    NumClustersInUse  -= NumClustersToFree;
    if (NumClustersInUse > 0u) {
      pFreeClusterCache->NumClustersInUse = NumClustersInUse;
    } else {
      pFreeClusterCache->StartCluster     = 0;
      pFreeClusterCache->NumClustersInUse = 0;
      pFreeClusterCache->NumClustersTotal = 0;
      pFreeClusterCache->pFile            = NULL;
    }
  }
  return NumClustersToFree;
}

#endif // FS_FAT_SUPPORT_FREE_CLUSTER_CACHE

#if FS_SUPPORT_FREE_SECTOR

/*********************************************************************
*
*       _FreeClusters
*
*  Function description
*    Informs the device driver about sectors that were assigned to
*    clusters and that are no longer in use.
*/
static int _FreeClusters(FS_VOLUME * pVolume, U32 FirstCluster, U32 NumClusters) {
  FS_FAT_INFO  * pFATInfo;
  U32            SectorIndex;
  U32            NumSectors;
  FS_PARTITION * pPart;
  unsigned       ldSectorsPerCluster;
  int            r;

  pPart = &pVolume->Partition;
  pFATInfo = &pVolume->FSInfo.FATInfo;
  ldSectorsPerCluster = _ld(pFATInfo->SectorsPerCluster);
  SectorIndex         = FS_FAT_ClusterId2SectorNo(pFATInfo, FirstCluster);
  NumSectors          = NumClusters << ldSectorsPerCluster;
  r = FS_LB_FreeSectorsPart(pPart, SectorIndex, NumSectors);
  return r;
}

#endif // FS_SUPPORT_FREE_SECTOR

#if FS_FAT_OPTIMIZE_LINEAR_ACCESS

/*********************************************************************
*
*       _GetNumAdjClusters
*
*  Function description
*    Returns the number of adjacent clusters in the specified cluster chain.
*
*  Parameters
*    pVolume      Volume instance.
*    pSB          Sector buffer.
*    ClusterId    Id of the first cluster in the chain.
*
*  Return value
*    Number of adjacent clusters that are linked with the specified cluster.
*
*  Additional information
*    This function is part of an optimization that allows the read and write
*    operations to reduce the number of accesses to the allocation table.
*    The function scans only one sector of the allocation table at the time
*    therefore the real number of adjacent clusters may be greater than the
*    value returned
*/
static unsigned _GetNumAdjClusters(FS_VOLUME * pVolume, FS_SB * pSB, U32 ClusterId) {
  FS_FAT_INFO * pFATInfo;
  U32           BitOff;
  U32           NumRemEntries;
  unsigned      NumClusters;
  unsigned      FATType;
  U32           ClusterIdNext;

  NumClusters    = 0;
  pFATInfo       = &pVolume->FSInfo.FATInfo;
  FATType        = pFATInfo->FATType;
  BitOff         = ClusterId * FATType;                         // Bit number in FAT.
  BitOff        &= ((U32)pFATInfo->BytesPerSector << 3) - 1u;   // Bit number in sector
  NumRemEntries  = (((U32)pFATInfo->BytesPerSector << 3) - BitOff) / FATType;
  for (; NumRemEntries > 0u; NumRemEntries--) {
#if FS_FAT_SUPPORT_FREE_CLUSTER_CACHE
    ClusterIdNext = _ReadFATEntry(pVolume, pSB, ClusterId);
#else
    ClusterIdNext = FS_FAT_ReadFATEntry(pVolume, pSB, ClusterId);
#endif // FS_FAT_SUPPORT_FREE_CLUSTER_CACHE
    if (ClusterIdNext != ++ClusterId) {
      break;                                                    // This one is not adjacent so quit the loop.
    }
    NumClusters++;
  }
  return NumClusters;
}

#endif // FS_FAT_OPTIMIZE_LINEAR_ACCESS

#if FS_SUPPORT_FAT

/*********************************************************************
*
*       _GetConfig
*
*  Function description
*    Returns information about how the FAT component is configured to operate.
*
*  Parameters
*    pConfig    [OUT] Configuration information.
*
*  Additional information
*    This is the internal version of FS_FAT_GetConfig().
*/
static void _GetConfig(FS_FAT_CONFIG * pConfig) {
  U8 IsLFNSupported;
  U8 IsFSInfoSectorUsed;
  U8 IsATCopyMaintained;
  U8 IsROFileMovePermitted;
  U8 IsDirtyFlagUpdated;
  U8 IsFAT32Supported;
  U8 IsDeleteOptimized;
  U8 LinearAccessOptimizationLevel;
  U8 IsFreeClusterCacheSupported;
  U8 IsLowerCaseSFNSupported;

  IsLFNSupported = 0;
  if (FAT_pDirEntryAPI != &FAT_SFN_API) {
    IsLFNSupported = 1;
  }
#if FS_FAT_USE_FSINFO_SECTOR
  IsFSInfoSectorUsed = FAT_UseFSInfoSector;
#else
  IsFSInfoSectorUsed = 0;
#endif // FS_FAT_USE_FSINFO_SECTOR
#if FS_MAINTAIN_FAT_COPY
  IsATCopyMaintained = FAT_MaintainFATCopy;
#else
  IsATCopyMaintained = 0;
#endif // FS_MAINTAIN_FAT_COPY
#if FS_FAT_PERMIT_RO_FILE_MOVE
  IsROFileMovePermitted = FAT_PermitROFileMove;
#else
  IsROFileMovePermitted = 0;
#endif // FS_FAT_PERMIT_RO_FILE_MOVE
#if FS_FAT_UPDATE_DIRTY_FLAG
  IsDirtyFlagUpdated = FAT_UpdateDirtyFlag;
#else
  IsDirtyFlagUpdated = 0;
#endif // FS_FAT_UPDATE_DIRTY_FLAG
  IsFAT32Supported              = FS_FAT_SUPPORT_FAT32;
  IsDeleteOptimized             = FS_FAT_OPTIMIZE_DELETE;
  LinearAccessOptimizationLevel = FS_FAT_OPTIMIZE_LINEAR_ACCESS;
  IsFreeClusterCacheSupported   = FS_FAT_SUPPORT_FREE_CLUSTER_CACHE;
  IsLowerCaseSFNSupported       = FS_FAT_LFN_LOWER_CASE_SHORT_NAMES;
  //
  // Return the calculated values.
  //
  pConfig->IsLFNSupported                = IsLFNSupported;
  pConfig->IsFSInfoSectorUsed            = IsFSInfoSectorUsed;
  pConfig->IsATCopyMaintained            = IsATCopyMaintained;
  pConfig->IsROFileMovePermitted         = IsROFileMovePermitted;
  pConfig->IsDirtyFlagUpdated            = IsDirtyFlagUpdated;
  pConfig->IsFAT32Supported              = IsFAT32Supported;
  pConfig->IsDeleteOptimized             = IsDeleteOptimized;
  pConfig->LinearAccessOptimizationLevel = LinearAccessOptimizationLevel;
  pConfig->IsFreeClusterCacheSupported   = IsFreeClusterCacheSupported;
  pConfig->IsLowerCaseSFNSupported       = IsLowerCaseSFNSupported;
}

#endif // FS_SUPPORT_FAT

/*********************************************************************
*
*       Public code (internal)
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_FAT_CheckBPB
*
*  Function description
*    Reads the BPB (BIOS Parameter Block) from a device and analyzes it.
*
*  Parameters
*    pVolume    Pointer to volume the BPB should be read.
*
*  Return value
*    ==0      BPB successfully read and contains valid information
*    !=0      Error code indicating the failure reason
*/
int FS_FAT_CheckBPB(FS_VOLUME * pVolume) {
  int        r;
  U8       * pBuffer;
  unsigned   BytesPerSector;

  pBuffer = FS__AllocSectorBuffer();
  if (pBuffer == NULL) {
    return FS_ERRCODE_BUFFER_NOT_AVAILABLE; // Error, no buffer available.
  }
  BytesPerSector = FS_GetSectorSize(&pVolume->Partition.Device);
  //
  // Check if the a sector fits into the sector buffer.
  //
  if (BytesPerSector == 0u) {
    r = FS_ERRCODE_STORAGE_NOT_READY;       // Error, storage device not ready.
    goto Done;
  }
  if (BytesPerSector > FS_Global.MaxSectorSize) {
    FS_DEBUG_ERROROUT((FS_MTYPE_FS, "FAT: FS_FAT_CheckBPB: Sector buffer smaller than device sector."));
    r = FS_ERRCODE_BUFFER_TOO_SMALL;        // Error, sector size of device larger than sector buffer.
    goto Done;
  }
  r = FS_LB_ReadPart(&pVolume->Partition, SECTOR_INDEX_BPB, pBuffer, FS_SECTOR_TYPE_MAN);
  if (r != 0) {
    r = FS_ERRCODE_READ_FAILURE;            // Error, could not read sector.
    goto Done;
  }
  r = _IsValidBPB(pVolume, pBuffer);
  if (r == 0) {
    r = FS_ERRCODE_INVALID_FS_FORMAT;       // Error, volume is not properly formatted.
    goto Done;
  }
  r = FS_ERRCODE_OK;
  //
  // Check if the number of sectors has shrunk since the last high-level format.
  // If so, the medium must be re-formatted.
  //
  {
    U32 NumSectorsFormat;
    U32 NumSectorsDevice;

    NumSectorsFormat = pVolume->FSInfo.FATInfo.NumSectors;
    NumSectorsDevice = pVolume->Partition.NumSectors;
    if (NumSectorsFormat > NumSectorsDevice) {
      FS_DEBUG_ERROROUT((FS_MTYPE_FS, "FAT: FS_FAT_CheckBPB: Number of sectors on device has shrunk. High-level format required."));
      r = FS_ERRCODE_INVALID_FS_FORMAT;     // Error, number of sectors on the storage medium has shrunk.
    }
  }
Done:
  FS__FreeSectorBuffer(pBuffer);
  return r;
}

/*********************************************************************
*
*       FS_FAT_GetFATType
*
*  Function description
*    Returns the FAT type based on the total number of clusters.
*
*  Parameters
*    NumClusters    Number of available clusters.
*
*  Return value
*    FAT type.
*/
unsigned FS_FAT_GetFATType(U32 NumClusters) {
  unsigned FATType;

  if (NumClusters < 4085u) {
    FATType = FS_FAT_TYPE_FAT12;
  } else if (NumClusters < 65525uL) {
    FATType = FS_FAT_TYPE_FAT16;
  } else {
    FATType = FS_FAT_TYPE_FAT32;
  }
  return FATType;
}

/*********************************************************************
*
*       FS_FAT_ClusterId2SectorNo
*
*  Function description
*    Calculates the index of the logical sector that stores
*    the specified cluster id.
*
*  Return value
*    Calculated sector index.
*/
U32 FS_FAT_ClusterId2SectorNo(const FS_FAT_INFO * pFATInfo, U32 ClusterId) {
  return pFATInfo->FirstDataSector + (ClusterId - FAT_FIRST_CLUSTER) * pFATInfo->SectorsPerCluster;
}

/*********************************************************************
*
*       FS_FAT_ReadFATEntry
*
*  Function description
*    Returns the value of a single FAT entry.
*
*  Parameters
*    pVolume      [IN]  Volume to read from.
*    pSB          [IN]  Smart buffer to read data from medium.
*    ClusterId    Index of the FAT entry to read.
*
*  Return value
*    The value of the FAT entry as defined in the FAT spec.
*    0xFFFFFFFF is invalid and used on error.
*
*  Notes
*    (1) Pointer incrementing
*        The  pointer is pre-incremented before accesses for the
*        FAT32 entries. This is so because it does in fact allow the
*        compiler to generate better code.
*    (2) This cast is necessary, because the value is promoted to a
*        signed int. On 16-bit target this value could be negative and
*        would result in using the wrong FAT entry value.
*/
U32 FS_FAT_ReadFATEntry(FS_VOLUME * pVolume, FS_SB * pSB, U32 ClusterId) {
  U32           FATEntry;
  U32           SectorIndex;
  U32           Off;          // Total offset in bytes
  unsigned      SectorOff;    // Offset within the sector
  unsigned      FATType;
  U8          * pData;
  FS_FAT_INFO * pFATInfo;
  int           r;
  U32           LastCluster;
  U8          * pBuffer;

  pFATInfo    = &pVolume->FSInfo.FATInfo;
  LastCluster = pFATInfo->NumClusters + FAT_FIRST_CLUSTER - 1u;
  if ((ClusterId < FAT_FIRST_CLUSTER) || (ClusterId > LastCluster)) {
    FS_DEBUG_ERROROUT((FS_MTYPE_FS, "FAT: FS_FAT_ReadFATEntry: Cluster id out of bounds (%lu not in [%lu, %lu]).", ClusterId, FAT_FIRST_CLUSTER, LastCluster));
#if FS_SUPPORT_TEST
    FS_X_PANIC(FS_ERRCODE_INVALID_CLUSTER_CHAIN);
#else
    return CLUSTER_ID_INVALID;                  // Error, out of allocation table bounds.
#endif // FS_SUPPORT_TEST
  }
  FATType  = pFATInfo->FATType;
  Off      = _ClusterId2FATOff(FATType, ClusterId);
  if ((Off >> pFATInfo->ldBytesPerSector) >= pFATInfo->FATSize) {
    FS_DEBUG_ERROROUT((FS_MTYPE_FS, "FAT: FS_FAT_ReadFATEntry: AT sector out of bounds (%lu not in [0, %lu]).", Off >> pFATInfo->ldBytesPerSector, pFATInfo->FATSize - 1u));
#if FS_SUPPORT_TEST
    FS_X_PANIC(FS_ERRCODE_INVALID_CLUSTER_CHAIN);
#else
    return CLUSTER_ID_INVALID;                  // Error, out of allocation table bounds.
#endif // FS_SUPPORT_TEST
  }
  SectorIndex = pFATInfo->RsvdSecCnt + (Off >> pFATInfo->ldBytesPerSector);
  _SetFATSector(pSB, SectorIndex, pFATInfo);
  r = FS__SB_Read(pSB);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_FS, "FAT: FS_FAT_ReadFATEntry: Could not read sector."));
    return CLUSTER_ID_INVALID;                  // Error, could not read from storage.
  }
  SectorOff = Off & ((unsigned)pFATInfo->BytesPerSector - 1u);
  pBuffer   = FS__SB_GetBuffer(pSB);
  pData     = pBuffer + SectorOff;
  FATEntry  = *pData++;
  if (SectorOff == ((unsigned)pFATInfo->BytesPerSector - 1u)) {   // With FAT12, the next byte could be in the next sector
    _SetFATSector(pSB, SectorIndex + 1u, pFATInfo);
    (void)FS__SB_Read(pSB);
    pData = FS__SB_GetBuffer(pSB);
  }
  FATEntry |= ((U32)*pData) << 8;               // Note 1, Note 2
  switch (FATType) {
  case FS_FAT_TYPE_FAT32:                       // We have to read 2 more bytes
    FATEntry |= ((U32)*++pData) << 16;          // Note 1
    FATEntry |= ((U32)*++pData) << 24;          // Note 1
    FATEntry &= FAT32_CLUSTER_ID_MASK;          // The 4 most significant bits are not used. See FAT specification.
    break;
  case FS_FAT_TYPE_FAT12:
    if ((ClusterId & 1u) != 0u) {
      FATEntry >>= 4;
    } else {
      FATEntry &= 0xFFFu;
    }
    break;
  case FS_FAT_TYPE_FAT16:
    break;
  default:
    FATEntry = CLUSTER_ID_INVALID;              // Error invalid FAT type
    break;
  }
  FS_DEBUG_LOG((FS_MTYPE_FS, "FAT: READ_AT ClusterId: %lu, Value: %lu.\n", ClusterId, FATEntry));
  return FATEntry;
}

/*********************************************************************
*
*       FS_FAT_FindFreeCluster
*
*  Function description
*    Finds the first available sector in the FAT.
*    Search starts at the specified cluster number, which makes it
*    possible to allocate consecutive sectors (if available)
*
*  Parameters
*    pVolume        Specifies the volume to search on.
*    pSB            Sector buffer to be used for the read operations.
*    FirstCluster   Index of the first free cluster to look at.
*                   Can be 0 or out of range, in which case the first cluster assumed to be free is used.
*    pFile          Handle to an opened file for which a new cluster must be allocated. NULL if the cluster is allocated for a directory.
*
*  Return value
*    ClusterId   if free cluster has been found
*    0           if no free cluster is available
*/
U32 FS_FAT_FindFreeCluster(FS_VOLUME * pVolume, FS_SB * pSB, U32 FirstCluster, FS_FILE * pFile) {
  U32           ClusterId;
  U32           LastCluster;
  FS_FAT_INFO * pFATInfo;
  U32           ATEntry;

  pFATInfo    = &pVolume->FSInfo.FATInfo;
  LastCluster = pFATInfo->NumClusters + FAT_FIRST_CLUSTER - 1u;   // NumCluster stores the number of cluster that are used for data storage. The first 2 clusters are typically reserved.
#if FS_FAT_SUPPORT_FREE_CLUSTER_CACHE
  {
    FS_WRITEMODE            WriteMode;
    FS_FREE_CLUSTER_CACHE * pFreeClusterCache;

    pFreeClusterCache = &pFATInfo->FreeClusterCache;
    WriteMode         = FS__GetFileWriteModeEx(pVolume);
    if (WriteMode == FS_WRITEMODE_FAST) {
      //
      // Check if the file is changed. If so then we need to sync.
      //
      if ((pFile != pFreeClusterCache->pFile) && (pFreeClusterCache->NumClustersTotal != 0u)) {
        (void)FS_FAT_SyncAT(pVolume, pSB);
      }
      //
      // Try to get a free cluster from the cache.
      //
      if (pFile != NULL) {
        if (pFreeClusterCache->pFile == pFile) {
          ClusterId = pFreeClusterCache->StartCluster + pFreeClusterCache->NumClustersInUse;
          if (pFreeClusterCache->NumClustersTotal > pFreeClusterCache->NumClustersInUse) {
            pFreeClusterCache->NumClustersInUse++;
            return ClusterId;
          }
          FirstCluster = ClusterId;
          (void)FS_FAT_SyncAT(pVolume, pSB);
        }
      }
    }
  }
#else
  FS_USE_PARA(pFile);
#endif // FS_FAT_SUPPORT_FREE_CLUSTER_CACHE
  //
  // Compute the first cluster to look at. If no valid cluster is specified, try the next one which should be free.
  //
  if ((FirstCluster < FAT_FIRST_CLUSTER) || (FirstCluster > LastCluster)) {
    FirstCluster = pFATInfo->NextFreeCluster;
  }
  if ((FirstCluster < FAT_FIRST_CLUSTER) || (FirstCluster > LastCluster)) {
    FirstCluster = FAT_FIRST_CLUSTER;
  }
  ClusterId = FirstCluster;
  //
  // Start searching with the specified cluster.
  //
  FS_ENABLE_READ_AHEAD(pVolume);
  do {
    ATEntry = FS_FAT_ReadFATEntry(pVolume, pSB, ClusterId);
    if (ATEntry == 0u) {
#if FS_FAT_SUPPORT_FREE_CLUSTER_CACHE
      if (_FillFreeClusterCache(pVolume, pSB, ClusterId, pFile) != 0) {
        ClusterId = 0;  // Error, could not fill the cache.
      }
#endif
      goto Done;        // We found a free cluster
    }
    if (ATEntry == CLUSTER_ID_INVALID) {
      ClusterId = 0;    // Error FAT could not be read
      goto Done;
    }
  } while (++ClusterId <= LastCluster);
  //
  // If we did not find any free cluster from the given cluster to the last cluster of the storage device.
  // Continue searching from first cluster of the storage device to the given cluster.
  //
  for (ClusterId = FAT_FIRST_CLUSTER; ClusterId < FirstCluster; ClusterId++) {
    ATEntry = FS_FAT_ReadFATEntry(pVolume, pSB, ClusterId);
    if (ATEntry == 0u) {
#if FS_FAT_SUPPORT_FREE_CLUSTER_CACHE
      if (_FillFreeClusterCache(pVolume, pSB, ClusterId, pFile) != 0) {
        ClusterId = 0;  // Error, could not fill the cache.
      }
#endif
      goto Done;      // We found a free cluster
    }
  }
  ClusterId = 0;              // Error, no free cluster
Done:
  FS_DISABLE_READ_AHEAD(pVolume);
  return ClusterId;
}

/*********************************************************************
*
*       FS_FAT_IsClusterFree
*
*  Function description
*    Verifies if the specified cluster is free.
*
*  Return value
*    ==1   The cluster is free and can be used to store data.
*    ==0   The cluster is already in use.
*
*  Additional information
*    This function does not return accurate information about the
*    usage of a cluster if the free cluster cache is enabled. If the
*    information cannot be determined by consulting the free cluster
*    cache then we simply return that the cluster is in use which
*    it may not be true. This is not a problem because the caller
*    is supposed to check the actual usage of the cluster. We do this
*    in order to keep the number of sector read operations to a minimum.
*/
int FS_FAT_IsClusterFree(FS_VOLUME * pVolume, FS_SB * pSB, U32 ClusterId) {
  int r;
  U32 ATEntry;

  r = 0;          // Set to indicate that the cluster is not free.
#if FS_FAT_SUPPORT_FREE_CLUSTER_CACHE
  {
    int          SkipRead;
    FS_WRITEMODE WriteMode;

    FS_USE_PARA(pSB);
    ATEntry   = 0xFFFFFFFFuL;       // Assume that the cluster is in use.
    WriteMode = FS__GetFileWriteModeEx(pVolume);
    if (WriteMode == FS_WRITEMODE_FAST) {
      SkipRead = _ReadFromFreeClusterCache(pVolume, ClusterId, &ATEntry);
      if (SkipRead == 0) {
        ATEntry = 0xFFFFFFFFuL;     // Assume that the cluster is in use.
      }
    }
    if (ATEntry == 0u) {
      r = 1;                        // Cluster is not in use.
    }
  }
#else
  ATEntry = FS_FAT_ReadFATEntry(pVolume, pSB, ClusterId);
  if (FS__SB_GetError(pSB) == 0) {
    if (ATEntry == 0u) {
      r = 1;                        // Cluster is not in use.
    }
  }
#endif // FS_FAT_SUPPORT_FREE_CLUSTER_CACHE
  return r;
}

/*********************************************************************
*
*       FS_FAT_MarkClusterEOC
*
*  Function description
*    Marks the given cluster as the last in the cluster chain.
*
*  Return value
*    ==0   O.K.
*    ==1   Error
*/
int FS_FAT_MarkClusterEOC(FS_VOLUME * pVolume, FS_SB * pSB, U32 Cluster) {
  int r;

  r = _WriteFATEntry(pVolume, pSB, Cluster, 0xFFFFFFFuL);
  return r;
}

/*********************************************************************
*
*       FS_FAT_LinkCluster
*/
int FS_FAT_LinkCluster(FS_VOLUME * pVolume, FS_SB * pSB, U32 LastCluster, U32 NewCluster) {
  int r;
  int Result;

  r = 0;                                        // Set to indicate success.
  if (LastCluster != 0u) {
    if (LastCluster != NewCluster) {            // Do not link a cluster to itself.
      r = _WriteFATEntry(pVolume, pSB, LastCluster, NewCluster);
    }
  }
  Result = FS_FAT_MarkClusterEOC(pVolume, pSB, NewCluster);
  if (Result != 0) {
    r = Result;
  }
  return r;
}

/*********************************************************************
*
*       FS_FAT_FindLastCluster
*
*  Function description
*    Returns the id of the last cluster in a cluster chain.
*
*  Parameters
*    pVolume        Specifies the volume to search on.
*    pSB            Sector buffer to be used for the read operations.
*    ClusterId      Id of any cluster in the cluster chain.
*    pNumClusters   [OUT] Number of clusters in the cluster chain.
*
*  Return value
*     Id of the last cluster in the cluster chain.
*/
U32 FS_FAT_FindLastCluster(FS_VOLUME * pVolume, FS_SB * pSB, U32 ClusterId, U32 * pNumClusters) {
  U32 NumClusters;
  U32 NextCluster;

  NumClusters = 1;            // There always is at least one cluster in the cluster chain.
  for (;;) {
    NextCluster = FS_FAT_WalkCluster(pVolume, pSB, ClusterId, 1);
    if (NextCluster == 0u) {
      break;
    }
    NumClusters++;
    if (NumClusters > pVolume->FSInfo.FATInfo.NumClusters) {
      FS_DEBUG_ERROROUT((FS_MTYPE_FS, "FAT: FS_FAT_FindLastCluster: Too many clusters in the chain."));
      NumClusters = 0;
      break;
    }
    ClusterId = NextCluster;
  }
  if (pNumClusters != NULL) {
    *pNumClusters = NumClusters;
  }
  return ClusterId;
}

/*********************************************************************
*
*       FS_FAT_AllocCluster
*
*  Function description
*    Allocates a cluster and (optionally) links it to an existing cluster
*    chain, specified by the last cluster.
*
*  Return value
*    0             On error (No new cluster).
*    ClusterId >0  If new cluster has been allocated (and possibly added to the chain).
*/
U32 FS_FAT_AllocCluster(FS_VOLUME * pVolume, FS_SB * pSB, U32 LastCluster) {
  U32 NewCluster;
  int r;

  NewCluster = FS_FAT_FindFreeCluster(pVolume, pSB, LastCluster, NULL);
  if (NewCluster != 0u) {
    r = FS_FAT_LinkCluster(pVolume, pSB, LastCluster, NewCluster);
    if (r != 0) {
      NewCluster = 0;           // Error, could not link cluster.
    }
  }
  return NewCluster;
}

/*********************************************************************
*
*       FS_FAT_WalkClusterEx
*
*  Function description
*    Walks a chain of clusters and returns the cluster Id of the
*    cluster found.
*
*  Parameters
*    pVolume        Identifies the volume to search on.
*    pSB            Sector buffer to be used for the read operations.
*    ClusterId      Id of the cluster to start with.
*    pNumClusters   [IN]  Number of clusters to walk.
*                   [OUT] Number of clusters which could not be walked.
*
*  Return value
*    if (ClusterChain long enough) {
*      ClusterId of destination cluster
*    } else {
*      last cluster in chain
*    }
*/
U32 FS_FAT_WalkClusterEx(FS_VOLUME * pVolume, FS_SB * pSB, U32 ClusterId, U32 * pNumClusters) {
  U32  NumClusters;

  NumClusters = *pNumClusters;
  for (; NumClusters != 0u; NumClusters--) {
    U32 FATEntry;

#if FS_FAT_SUPPORT_FREE_CLUSTER_CACHE
    FATEntry = _ReadFATEntry(pVolume, pSB, ClusterId);
#else
    FATEntry = FS_FAT_ReadFATEntry(pVolume, pSB, ClusterId);
#endif // FS_FAT_SUPPORT_FREE_CLUSTER_CACHE
    if (FS__SB_GetError(pSB) != 0) {
      break;                      // A read error occurred.
    }
    //
    // Check validity of the FAT entry.
    //
    if (FATEntry > (pVolume->FSInfo.FATInfo.NumClusters + 1u)) {
      break;
    }
    if (FATEntry == 0u) {
      FS_DEBUG_ERROROUT((FS_MTYPE_FS, "FAT: FS_FAT_WalkClusterEx: Cluster id out of bounds (%lu of %lu not in [%lu, %lu]).", FATEntry, ClusterId, FAT_FIRST_CLUSTER, pVolume->FSInfo.FATInfo.NumClusters + 1u));
      break;
    }
    ClusterId = FATEntry;
  }
  *pNumClusters = NumClusters;
  return ClusterId;
}

/*********************************************************************
*
*       FS_FAT_WalkCluster
*
*  Function description
*    Walks a chain of clusters and returns the cluster Id of the
*    cluster found
*
*  Return value
*    ClusterId   if cluster is in chain
*    0           if cluster is not valid
*/
U32 FS_FAT_WalkCluster(FS_VOLUME * pVolume, FS_SB * pSB, U32 ClusterId, U32 NumClusters) {
  ClusterId = FS_FAT_WalkClusterEx(pVolume, pSB, ClusterId, &NumClusters);
  if (NumClusters != 0u) {
    ClusterId = 0;     // Could not go all the way
  }
  return ClusterId;
}

/*********************************************************************
*
*       FS_FAT_GotoCluster
*
*  Function description
*    Selects the cluster that matches the current file position.
*
*  Parameters
*    pFile      Opened file handle.
*    pSB        Sector buffer.
*
*  Return value
*    Number of clusters that are missing from the cluster chain.
*
*  Additional information
*    This routine is called both when reading and writing a file.
*
*    When reading, a non-zero return value indicates an error meaning that file
*    position is larger than the file size.
*
*    When writing, a non-zero return value indicates the number of clusters that
*    that have to be additionally allocated.
*
*    The id of the cluster that corresponds with the file position
*    is stored to the file object assigned to the file handle.
*/
int FS_FAT_GotoCluster(const FS_FILE * pFile, FS_SB * pSB) {
  U32               NumClustersToWalk;
  U32               NumClustersRem;
  U32               CurClusterId;
  FS_FILE_OBJ     * pFileObj;
  FS_VOLUME       * pVolume;
  FS_INT_DATA_FAT * pFATData;

  pFileObj = pFile->pFileObj;
  pFATData = &pFileObj->Data.Fat;
  pVolume  = pFileObj->pVolume;
  //
  // If no cluster is allocated to the file then return the
  // number of clusters that have to be allocated for the
  // current file position.
  //
  if (pFileObj->FirstCluster == 0u) {
    NumClustersRem = _FilePosToClusterIndex(pFile) + 1u;                    // +1 because we need the number of clusters.
    return (int)NumClustersRem;                                             // OK, this is the number of clusters that have to be allocated.
  }
  //
  // There is at least one cluster that is allocated to the file.
  // Get the id of the cluster corresponding to the file position
  // by checking the cache of adjacent clusters.
  //
  NumClustersToWalk = _WalkAdjClusters(pFile);
  if (NumClustersToWalk == 0u) {
    return 0;                                                               // OK, all clusters are present.
  }
  NumClustersRem = NumClustersToWalk;
  CurClusterId   = FS_FAT_WalkClusterEx(pVolume, pSB, pFATData->CurClusterId, &NumClustersRem);
  //
  // Update values in pFile.
  //
  pFATData->CurClusterId     = CurClusterId;
  pFATData->CurClusterIndex += NumClustersToWalk - NumClustersRem;          // Advance cluster index by number of clusters walked.
#if FS_FAT_OPTIMIZE_LINEAR_ACCESS
  if (NumClustersRem == 0u) {
    pFATData->NumAdjClusters = (U16)_GetNumAdjClusters(pVolume, pSB, CurClusterId);
  }
#endif // FS_FAT_OPTIMIZE_LINEAR_ACCESS
  return (int)NumClustersRem;
}

/*********************************************************************
*
*       FS_FAT_FreeClusterChain
*
*  Function description
*    Marks all clusters in a cluster chain as free.
*
*  Return value
*    ==0    OK, cluster chain freed.
*    !=0    Error code indicating the failure reason.
*/
int FS_FAT_FreeClusterChain(FS_VOLUME * pVolume, FS_SB * pSB, U32 ClusterId, U32 NumClusters) {
  U32   NextCluster;
  U32   NumClustersRem;
  int   r;
  int   Result;
#if FS_SUPPORT_FREE_SECTOR
  U32   FirstFreeCluster;
  U32   NumFreeClusters;
#endif // FS_SUPPORT_FREE_SECTOR

  r                = 0;                 // Set to indicate success.
  NumClustersRem   = NumClusters;
#if FS_SUPPORT_FREE_SECTOR
  FirstFreeCluster = ClusterId;
  NumFreeClusters  = 1;
#endif // FS_SUPPORT_FREE_SECTOR
#if FS_FAT_OPTIMIZE_DELETE
  if (pVolume->FSInfo.FATInfo.FATType != FS_FAT_TYPE_FAT12) {
    FS_FAT_INFO * pFATInfo;
    U32           FirstFreeFATSector;
    U32           LastFreeFATSector;

    pFATInfo = &pVolume->FSInfo.FATInfo;
    FirstFreeFATSector = 0xFFFFFFFFuL;
    LastFreeFATSector  = 0xFFFFFFFFuL;
    for (; NumClusters != 0u; NumClusters--) {
      U32 Off;
      U32 SectorIndex;

#if FS_FAT_SUPPORT_FREE_CLUSTER_CACHE
      {
        U32 NumClustersFreed;

        //
        // Take care of the clusters that are present in the free cluster cache.
        //
        NumClustersFreed = _FreeClusterChainFromFreeClusterCache(pVolume, ClusterId);
        if (NumClustersFreed != 0u) {
          NumClustersRem = 0;
          FS_DEBUG_ASSERT(FS_MTYPE_FS, NumClustersFreed == NumClusters);    // Clusters present in the free cluster cache are always the last in the chain.
          if (NumClustersFreed != NumClusters) {
            r = FS_ERRCODE_INVALID_CLUSTER_CHAIN;
          }
          break;
        }
      }
#endif // FS_FAT_SUPPORT_FREE_CLUSTER_CACHE
      Off  = _ClusterId2FATOff(pFATInfo->FATType, ClusterId);
      Off &= (U32)pFATInfo->BytesPerSector - 1u;
      if (Off == 0u) {
        if (NumClustersRem < NumClusters) {
          U8 * pBuffer;

          SectorIndex = FS__SB_GetSectorIndex(pSB);
          pBuffer     = FS__SB_GetBuffer(pSB);
          if (_IsSectorBlank(SEGGER_PTR2PTR(U32, pBuffer), (U32)pFATInfo->BytesPerSector >> 2) != 0) {          // MISRA deviation D:100[e]
            if (SectorIndex == (LastFreeFATSector + 1u)) {
              LastFreeFATSector++;
            } else {
              Result = _WriteEmptyFATSectors(pSB, FirstFreeFATSector, LastFreeFATSector, pFATInfo->FATSize);
              if (Result != 0) {
                r = FS_ERRCODE_WRITE_FAILURE;
              }
              FirstFreeFATSector = SectorIndex;
              LastFreeFATSector  = SectorIndex;
            }
            FS__SB_MarkNotDirty(pSB);
          }
        }
      }
      NextCluster = FS_FAT_WalkCluster(pVolume, pSB, ClusterId, 1);
#if FS_SUPPORT_FREE_SECTOR
      if (pVolume->FreeSector != 0u) {
        //
        // Free sectors if the clusters are not continuous.
        //
        if (NextCluster != (FirstFreeCluster + NumFreeClusters)) {
          Result = _FreeClusters(pVolume, FirstFreeCluster, NumFreeClusters);
          if (Result != 0) {
            r = FS_ERRCODE_WRITE_FAILURE;
          }
          FirstFreeCluster = NextCluster;
          NumFreeClusters  = 1;
        } else {
          ++NumFreeClusters;
        }
        //
        // Store the cluster id but without performing a sector free operation.
        //
        pVolume->FreeSector = 0;
        Result = _WriteFATEntry(pVolume, pSB, ClusterId, 0);
        if (Result != 0) {
          r = Result;
        }
        pVolume->FreeSector = 1;
      }
#else
      Result = _WriteFATEntry(pVolume, pSB, ClusterId, 0);
      if (Result != 0) {
        r = Result;
      }
#endif // FS_SUPPORT_FREE_SECTOR
      --NumClustersRem;
      if (NextCluster == 0u) {
        break;
      }
      ClusterId = NextCluster;
    }
    Result = _WriteEmptyFATSectors(pSB, FirstFreeFATSector, LastFreeFATSector, pFATInfo->FATSize);
    if (Result != 0) {
      r = FS_ERRCODE_WRITE_FAILURE;
    }
  } else
#endif // FS_FAT_OPTIMIZE_DELETE
  {
    for (; NumClusters != 0u; NumClusters--) {
#if FS_FAT_SUPPORT_FREE_CLUSTER_CACHE
      {
        U32 NumClustersFreed;

        //
        // Take care of the clusters that are present in the free cluster cache.
        //
        NumClustersFreed = _FreeClusterChainFromFreeClusterCache(pVolume, ClusterId);
        if (NumClustersFreed != 0u) {
          NumClustersRem = 0;
          FS_DEBUG_ASSERT(FS_MTYPE_FS, NumClustersFreed == NumClusters);    // Clusters present in the free cluster cache are always the last in the chain.
          if (NumClustersFreed != NumClusters) {
            r = FS_ERRCODE_INVALID_CLUSTER_CHAIN;
          }
          break;
        }
      }
#endif // FS_FAT_SUPPORT_FREE_CLUSTER_CACHE
      NextCluster = FS_FAT_WalkCluster(pVolume, pSB, ClusterId, 1);
#if FS_SUPPORT_FREE_SECTOR
      if (pVolume->FreeSector != 0u) {
        //
        // Free sectors if the clusters are not continuous.
        //
        if (NextCluster != (FirstFreeCluster + NumFreeClusters)) {
          Result = _FreeClusters(pVolume, FirstFreeCluster, NumFreeClusters);
          if (Result != 0) {
            r = FS_ERRCODE_WRITE_FAILURE;
          }
          FirstFreeCluster = NextCluster;
          NumFreeClusters  = 1;
        } else {
          ++NumFreeClusters;
        }
        //
        // Store the cluster id but without performing a sector free operation.
        //
        pVolume->FreeSector = 0;
        Result = _WriteFATEntry(pVolume, pSB, ClusterId, 0);
        if (Result != 0) {
          r = Result;
        }
        pVolume->FreeSector = 1;
      }
#else
      Result = _WriteFATEntry(pVolume, pSB, ClusterId, 0);
      if (Result != 0) {
        r = Result;
      }
#endif // FS_SUPPORT_FREE_SECTOR
      --NumClustersRem;
      if (NextCluster == 0u) {
        break;
      }
      ClusterId = NextCluster;
    }
  }
  if (r == 0) {
    if (NumClustersRem != 0u) {
      r = FS_ERRCODE_INVALID_CLUSTER_CHAIN;           // Error, cluster chain too short.
    }
  }
  return r;
}

/*********************************************************************
*
*       FS_FAT_AllocClusterBlock
*
*  Function description
*    Allocates a cluster block.
*
*  Parameters
*    pVolume        Pointer to a volume.
*    FirstCluster   First cluster that shall be used for allocation.
*    NumClusters    Number of cluster to allocate.
*    pSB            Buffer to use for the operation.
*
*  Return value
*    ==0    O.K., number of allocated clusters.
*    !=0    Error code indicating the failure reason.
*/
int FS_FAT_AllocClusterBlock(FS_VOLUME * pVolume, U32 FirstCluster, U32 NumClusters, FS_SB * pSB) {
  U32 i;
  U32 ClusterId;
  U32 LastClusterId;
  int r;

  //
  // Check if parameters are valid.
  //
  if (((FirstCluster + NumClusters) - FAT_FIRST_CLUSTER) > pVolume->FSInfo.FATInfo.NumClusters) {
    return FS_ERRCODE_INVALID_PARA;       // Error, trying to allocate out of bounds clusters.
  }
  //
  // Check if all requested clusters are available.
  //
  for (i = 0; i < NumClusters; i++) {
    ClusterId = FS_FAT_ReadFATEntry(pVolume, pSB, i + FirstCluster);
    if (ClusterId != 0u) {
      if (ClusterId == CLUSTER_ID_INVALID) {
        return FS_ERRCODE_READ_FAILURE;     // Error, could not read from allocation table.
      }
      return FS_ERRCODE_CLUSTER_NOT_FREE;   // Error, cluster is already used.
    }
  }
  //
  // Mark all clusters as used. The first one is the head of the cluster chain.
  //
  LastClusterId = FirstCluster;
  for (i = 0; i < NumClusters; i++) {
    LastClusterId = FS_FAT_AllocCluster(pVolume, pSB, LastClusterId);
  }
  r = FS_FAT_MarkClusterEOC(pVolume, pSB, LastClusterId);
  if (r != 0) {
    return FS_ERRCODE_WRITE_FAILURE;
  }
  return FS_ERRCODE_OK;                   // OK, cluster block allocated.
}

/*********************************************************************
*
*       FS_FAT_IsValidShortNameChar
*
*  Function description
*    Checks if a character is allowed in a 8.3 file name.
*
*  Return value
*    1    Valid character
*    0    Not a valid character
*/
int FS_FAT_IsValidShortNameChar(U8 c) {
  int r;

  if (c <= 0x20u) {
    return 0;               // Error, control characters are not allowed in a short file name.
  }
  switch ((char)c) {
  case '"':
  case '*':
  case '+':
  case ',':
  case '/':
  case ':':
  case ';':
  case '<':
  case '=':
  case '>':
  case '?':
  case '[':
  case ']':
  case '\\':
  case '|':
  case '\x7F':              // A DEL character is not allowed in a short file name.
    r = 0;                  // Error, invalid character.
    break;
  default:
    r = 1;                  // O.K., valid character.
    break;
  }
  return r;
}

/*********************************************************************
*
*       FS_FAT_LoadShortName
*
*  Function description
*    Converts a file name from the storage format to a text string.
*
*  Parameters
*    sName          [OUT] File or directory name (0-terminated string)
*    MaxNumBytes    Maximum number of bytes to store to sName.
*    pShortName     [IN] Name of the file or directory as read from storage.
*/
void FS_FAT_LoadShortName(char * sName, unsigned MaxNumBytes, const U8 * pShortName) {
  unsigned i;
  unsigned NumBytesToCopy;

  if ((sName != NULL) && (MaxNumBytes != 0u)) {
    NumBytesToCopy = MaxNumBytes - 1u;              // Reserve one byte for the 0-terminator.
    if (NumBytesToCopy != 0u) {
      for (i = 0; i < FAT_MAX_NUM_BYTES_SFN; ++i) {
        //
        // Start of extension. If we have no space, then append the dot.
        //
        if ((i == 8u) && (*pShortName != (U8)' ')) {
          *sName++ = '.';
          --NumBytesToCopy;
          if (NumBytesToCopy == 0u) {
            break;
          }
        }
        //
        // If the first character of the directory entry is 0x05,
        // it is changed to 0xe5. FAT spec V1.03: FAT directories
        //
        if ((i == 0u) && (*pShortName == 0x05u)) {
          pShortName++;
          *sName++ = (char)0xE5;
          --NumBytesToCopy;
        } else if (*pShortName == (U8)' ') {    // Copy everything except spaces.
           pShortName++;
        } else {
          *sName++ = (char)*pShortName++;
          --NumBytesToCopy;
        }
        if (NumBytesToCopy == 0u) {
          break;
        }
      }
    }
    *sName = '\0';
  }
}

/*********************************************************************
*
*       FS_FAT_CalcCheckSum
*
*  Function description
*    Calculates the checksum of a short file name.
*
*  Parameters
*    pShortName     [IN] Short file name (11 bytes)
*
*  Return value
*    Calculated checksum.
*/
unsigned FS_FAT_CalcCheckSum(const U8 * pShortName) {
  unsigned Sum;
  int      i;

  Sum = 0;
  for (i = 0; i < 11; i++) {
    if ((Sum & 1u) != 0u) {
      Sum = (Sum >> 1) | 0x80u;
    } else {
      Sum >>= 1;
    }
    Sum += *pShortName++;
    Sum &= 0xFFu;
  }
  return Sum;
}

/*********************************************************************
*
*       FS_FAT_FindDirEntry
*
*  Function description
*    Tries to locate the directory entry in the specified directory.
*    The short name space is searched first;
*    if LFN support is activated, the long name space is search
*    if no short name match was found.
*
*  Parameters
*    pVolume          [IN] Volume information.
*    pSB              Sector buffer for the operation.
*    pEntryName       [IN]  Directory entry name.
*    Len              Maximum number of characters in pEntryName to consider.
*    DirStart         Id of the first cluster assigned to directory.
*    AttrRequired     Directory attributes to match.
*    pDirPosLFN       [OUT] Position of the first directory entry that stores the long file name.
*
*  Return value
*    != NULL    Pointer to directory entry in pSB.
*    ==NULL     Entry not found.
*/
FS_FAT_DENTRY * FS_FAT_FindDirEntry(FS_VOLUME * pVolume, FS_SB * pSB, const char * pEntryName, int Len, U32 DirStart, unsigned AttrRequired, FS_DIR_POS * pDirPosLFN) {
  FS_FAT_DENTRY * pDirEntry;
  FS_DIR_POS      DirPos;

  FS_MEMSET(&DirPos, 0, sizeof(DirPos));
  pDirEntry = FAT_pDirEntryAPI->pfFindDirEntry(pVolume, pSB, pEntryName, Len, DirStart, &DirPos, AttrRequired, pDirPosLFN);
  return pDirEntry;
}

/*********************************************************************
*
*       FS_FAT_FindDirEntryEx
*
*  Function description
*    Tries to locate the directory entry in the specified directory.
*    The short name space is searched first;
*    if LFN support is activated, the long name space is search
*    if no short name match was found.
*
*  Parameters
*    pVolume          [IN] Volume information.
*    pSB              Sector buffer for the operation.
*    pEntryName       [IN]  Directory entry name.
*    Len              Maximum number of characters in pEntryName to consider.
*    DirStart         Id of the first cluster assigned to directory.
*    pDirPos          [OUT] Position of the directory entry in the parent directory.
*    AttrRequired     Directory attributes to match.
*    pDirPosLFN       [OUT] Position of the first directory entry that stores the long file name.
*
*  Return value
*    != NULL    Pointer to directory entry in pSB.
*    ==NULL     Entry not found.
*/
FS_FAT_DENTRY * FS_FAT_FindDirEntryEx(FS_VOLUME * pVolume, FS_SB * pSB, const char * pEntryName, int Len, U32 DirStart, FS_DIR_POS * pDirPos, unsigned AttrRequired, FS_DIR_POS * pDirPosLFN) {
  FS_FAT_DENTRY * pDirEntry;

  pDirEntry = FAT_pDirEntryAPI->pfFindDirEntry(pVolume, pSB, pEntryName, Len, DirStart, pDirPos, AttrRequired, pDirPosLFN);
  return pDirEntry;
}

/*********************************************************************
*
*       FS_FAT_DelLongDirEntry
*
*  Function description
*    Marks as deleted the directory entries belonging to a long file name.
*
*  Parameters
*    pVolume      [IN] Volume information.
*    pSB          Sector buffer for the operation.
*    pDirPos      [IN] Position of the first directory entry.
*
*  Return value
*    ==0        Success, long directory entry deleted.
*    !=0        An error occurred.
*/
int FS_FAT_DelLongDirEntry(FS_VOLUME * pVolume, FS_SB * pSB, FS_DIR_POS * pDirPos) {
  int r;

  r = 0;          // Set to indicate success.
  if (FAT_pDirEntryAPI->pfDelLongEntry != NULL) {
    r = FAT_pDirEntryAPI->pfDelLongEntry(pVolume, pSB, pDirPos);
  }
  return r;
}
/*********************************************************************
*
*       FS_FAT_CopyDirEntryInfo
*/
void FS_FAT_CopyDirEntryInfo(const FS_FAT_DENTRY * pDirEntry, FS_DIRENTRY_INFO * pDirEntryInfo) {
  pDirEntryInfo->Attributes      = pDirEntry->Data[DIR_ENTRY_OFF_ATTRIBUTES];
  pDirEntryInfo->CreationTime    = (U32)FS_LoadU16LE(&pDirEntry->Data[DIR_ENTRY_OFF_CREATION_DATE]) << 16;
  pDirEntryInfo->CreationTime   |= (U32)FS_LoadU16LE(&pDirEntry->Data[DIR_ENTRY_OFF_CREATION_TIME]);
  pDirEntryInfo->LastAccessTime  = (U32)FS_LoadU16LE(&pDirEntry->Data[DIR_ENTRY_OFF_LAST_ACCESS_DATE]) << 16;
  pDirEntryInfo->LastWriteTime   = (U32)FS_LoadU16LE(&pDirEntry->Data[DIR_ENTRY_OFF_WRITE_DATE]) << 16;
  pDirEntryInfo->LastWriteTime  |= (U32)FS_LoadU16LE(&pDirEntry->Data[DIR_ENTRY_OFF_WRITE_TIME]);
  pDirEntryInfo->FileSize        = FS_LoadU32LE(&pDirEntry->Data[DIR_ENTRY_OFF_SIZE]);
  pDirEntryInfo->FirstClusterId  = FS_FAT_GetFirstCluster(pDirEntry);
}

/*********************************************************************
*
*       FS_FAT_CreateJournalFile
*
*  Function description
*    Creates the file where the Journal saves the data.
*
*  Parameters
*    pVolume        Pointer to a mounted volume.
*    NumBytes       Size of the file in bytes.
*    pFirstSector   [OUT] Index of the fist sector used to store the file.
*    pNumSectors    [OUT] Number of sectors allocated for the file.
*
*  Return value
*   ==0     O.K., Successfully created.
*   ==1     Journal file already present.
*   <=0     Error code indicating the failure reason.
*/
int FS_FAT_CreateJournalFile(FS_VOLUME * pVolume, U32 NumBytes, U32 * pFirstSector, U32 * pNumSectors) {
  FS_FILE       FileHandle;
  FS_FILE_OBJ   FileObj;
  FS_SB         sb;
  FS_FAT_INFO * pFATInfo;
  int           r;
  U32           FirstCluster;
  U32           NumClusters;
  U8            Attributes;
  const char  * sFileName;

  pFATInfo  = &pVolume->FSInfo.FATInfo;
  sFileName = FS__GetJournalFileName(pVolume);
  NumClusters = FS__DivideU32Up(NumBytes, pFATInfo->BytesPerCluster);
  if ((NumClusters == 0u) || (NumClusters >= pFATInfo->NumClusters)) {
    return FS_ERRCODE_INVALID_PARA;            // Error, invalid journal file size.
  }
  (void)FS__SB_Create(&sb, pVolume);
  FS_MEMSET(&FileHandle, 0, sizeof(FS_FILE));
  FS_MEMSET(&FileObj, 0, sizeof(FS_FILE_OBJ));
  //
  // Create journal file.
  //
  FileHandle.AccessFlags         = (U8)FS_FILE_ACCESS_FLAGS_CW;
  FileHandle.pFileObj            = &FileObj;
  FileHandle.IsDirUpdateRequired = 1;          // To force the update of directory entry when the file is closed.
  FileObj.pVolume                = pVolume;
  r = FS_FAT_OpenFile(sFileName, &FileHandle, 1, 0, 1);
  if (r != 0) {
    goto Done;
  }
  //
  // Add clusters to journal file.
  //
  FirstCluster = (pFATInfo->NumClusters - NumClusters) + FAT_FIRST_CLUSTER;
  r = FS_FAT_AllocClusterBlock(pVolume, FirstCluster, NumClusters, &sb);
  if (r != 0) {
    //
    // The cluster block could not be allocated therefore we remove the file.
    //
    (void)FS_FAT_OpenFile(sFileName, &FileHandle, 1, 0, 0);
    goto Done;
  }
  //
  // Update file object information
  //
  FileObj.FirstCluster = FirstCluster;
  FileObj.Size         = (FS_FILE_SIZE)NumClusters << pFATInfo->ldBytesPerCluster;
  //
  // Update directory entry
  //
  r = FS_FAT_CloseFile(&FileHandle);
  if (r != 0) {
    goto Done;
  }
  //
  // Set the file's attribute to SYSTEM and HIDDEN to make it harder
  // to delete the journal file on a host PC.
  //
  Attributes = (U8)(FS_ATTR_HIDDEN | FS_ATTR_SYSTEM);
  r = FS_FAT_SetDirEntryInfo(pVolume, sFileName, &Attributes, FS_DIRENTRY_SET_ATTRIBUTES);
  //
  // Set return value and out parameters
  //
  *pFirstSector = FS_FAT_ClusterId2SectorNo(pFATInfo, FirstCluster);
  *pNumSectors  = NumClusters * pFATInfo->SectorsPerCluster;
Done:
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
*       FS_FAT_OpenJournalFile
*
*  Function description
*    Opens the file where the Journal saves the data.
*
*  Parameters
*    pVolume  Pointer to a mounted volume.
*
*  Return value
*   ==0    O.K., journal file exists
*   !=0    Error code indicating the failure reason
*/
int FS_FAT_OpenJournalFile(FS_VOLUME * pVolume) {
  FS_FILE       FileHandle;
  FS_FILE_OBJ   FileObj;
  int           r;
  const char  * sFileName;

  FS_MEMSET(&FileHandle, 0, sizeof(FS_FILE));
  FS_MEMSET(&FileObj,    0, sizeof(FS_FILE_OBJ));
  //
  // Open journal file
  //
  FileHandle.AccessFlags = (U8)FS_FILE_ACCESS_FLAG_R;
  FileHandle.pFileObj    = &FileObj;
  FileObj.pVolume        = pVolume;
  sFileName              = FS__GetJournalFileName(pVolume);
  r = FS_FAT_OpenFile(sFileName, &FileHandle, 0, 1, 0);
  return r;
}

/*********************************************************************
*
*       FS_FAT_GetIndexOfLastSector
*
*  Function description
*    Returns the last sector that is used by the FS.
*
*  Parameters
*    pVolume      Pointer to a mounted volume.
*/
U32  FS_FAT_GetIndexOfLastSector(FS_VOLUME * pVolume) {
  FS_FAT_INFO * pFATInfo;

  pFATInfo = &pVolume->FSInfo.FATInfo;
  return FS_FAT_ClusterId2SectorNo(pFATInfo, pFATInfo->NumClusters + FAT_FIRST_CLUSTER - 1u) + pFATInfo->SectorsPerCluster - 1u;
}

#if FS_FAT_SUPPORT_FREE_CLUSTER_CACHE

/*********************************************************************
*
*       FS_FAT_SyncAT
*
*  Function description
*    Typ. called in fast file write mode to update the allocation table.
*
*  Parameters
*    pVolume    Pointer to the volume.
*    pSB        Pointer to a sector buffer used to handle read/write operation.
*/
int FS_FAT_SyncAT(FS_VOLUME * pVolume, FS_SB * pSB) {
  FS_FAT_INFO           * pFATInfo;
  U32                     iCluster;
  FS_FREE_CLUSTER_CACHE * pFreeClusterCache;
  U32                     ClusterId;
  int                     r;
  int                     Result;

  r                 = 0;          // Set to indicate success.
  pFATInfo          = &pVolume->FSInfo.FATInfo;
  pFreeClusterCache = &pFATInfo->FreeClusterCache;
  //
  // Mark all clusters as used. The first one is the head of the cluster chain
  //
  if (pFreeClusterCache->NumClustersInUse != 0u) {
    for (iCluster = 0; iCluster < (pFreeClusterCache->NumClustersInUse - 1u); iCluster++) {
      ClusterId = iCluster + pFreeClusterCache->StartCluster;
      Result = _WriteFATEntry(pVolume, pSB, ClusterId, ClusterId + 1u);
      if (Result != 0) {
        r = Result;
      }
    }
    ClusterId = iCluster + pFreeClusterCache->StartCluster;
    Result = FS_FAT_MarkClusterEOC(pVolume, pSB, ClusterId);
    if (Result != 0) {
      r = Result;
    }
  }
  pFreeClusterCache->StartCluster     = 0;
  pFreeClusterCache->NumClustersInUse = 0;
  pFreeClusterCache->NumClustersTotal = 0;
  pFreeClusterCache->pFile            = NULL;
  return r;
}

#endif // FS_FAT_SUPPORT_FREE_CLUSTER_CACHE

/*********************************************************************
*
*       FS_FAT_FreeSectors
*
*  Function description
*    Informs the storage driver about the sectors which are not used for storing data.
*
*  Parameters
*    pVolume    Pointer to a mounted volume.
*
*  Return value
*    ==0      OK, sectors marked as free
*    !=0      Error code indicating the failure reason
*/
int FS_FAT_FreeSectors(FS_VOLUME * pVolume) {
#if FS_SUPPORT_FREE_SECTOR
  FS_FAT_INFO * pFATInfo;
  U32           ClusterId;
  U32           ClusterIdLast;
  FS_SB         sb;
  int           r;
  U32           Value;
  int           Result;
  U32           ClusterIdFirstFree;
  U32           NumClustersFree;

  r = FS_ERRCODE_OK;                // Set to indicate success.
  pFATInfo = &pVolume->FSInfo.FATInfo;
  (void)FS__SB_Create(&sb, pVolume);
  ClusterIdFirstFree = CLUSTER_ID_INVALID;
  NumClustersFree    = 0;
  ClusterIdLast      = pFATInfo->NumClusters + 1u;
  for (ClusterId = FAT_FIRST_CLUSTER; ClusterId <= ClusterIdLast; ClusterId++) {
    Value = FS_FAT_ReadFATEntry(pVolume, &sb, ClusterId);
    if (FS__SB_GetError(&sb) != 0) {
      r = FS_ERRCODE_READ_FAILURE;  // Error, could not read from allocation table.
      break;
    }
    if (Value == CLUSTER_ID_INVALID) {
      r = FS_ERRCODE_READ_FAILURE;  // Error, could not read from allocation table.
      break;
    }
    if (Value == 0u) {              // Cluster not used?
      if (NumClustersFree == 0u) {
        ClusterIdFirstFree = ClusterId;
      }
      ++NumClustersFree;
    } else {
      if (NumClustersFree != 0u) {
        //
        // Inform the driver layer about the unused sector(s).
        //
        Result = _FreeClusters(pVolume, ClusterIdFirstFree, NumClustersFree);
        if (Result != 0) {
          r = FS_ERRCODE_WRITE_FAILURE;
        }
        ClusterIdFirstFree = CLUSTER_ID_INVALID;
        NumClustersFree    = 0;
      }
    }
  }
  if (NumClustersFree != 0u) {
    //
    // Inform the driver layer about the unused sector(s).
    //
    Result = _FreeClusters(pVolume, ClusterIdFirstFree, NumClustersFree);
    if (Result != 0) {
      r = FS_ERRCODE_WRITE_FAILURE;
    }
  }
  FS__SB_Delete(&sb);
  return r;
#else
  FS_USE_PARA(pVolume);
  return FS_ERRCODE_NOT_SUPPORTED;
#endif // FS_SUPPORT_FREE_SECTOR
}

/*********************************************************************
*
*       FS_FAT_Validate
*/
int FS_FAT_Validate(void) {
  const U8 aShortName[11] = {(U8)'J', (U8)'O', (U8)'U', (U8)'R', (U8)'N', (U8)'A', (U8)'L', (U8)' ', (U8)'B', (U8)'I', (U8)'N'};
  unsigned CheckSum;

  CheckSum = FS_FAT_CalcCheckSum(aShortName);
  if (CheckSum != 0x81u) {
    return 1;                 // Error, checksum is not correct.
  }
  return 0;                   // OK, checksum is correct.
}

/*********************************************************************
*
*       FS_FAT_GetFreeSpace
*
*  Function description
*    Calculates the amount of available free space.
*
*  Parameters
*    pVolume              Volume information.
*    pBuffer              Work buffer.
*    SizeOfBuffer         Number of bytes in the work buffer.
*    FirstClusterId       Id of the first cluster to be checked.
*    pNumClustersFree     [OUT] Number of free clusters found.
*    pNumClustersChecked  [OUT] Number of clusters checked during operation.
*
*  Return value
*    ==1     OK, the entire allocation table has been searched.
*    ==0     OK, search is not completed.
*    < 0     Error code indicating the failure reason.
*/
int FS_FAT_GetFreeSpace(FS_VOLUME * pVolume, void * pBuffer, int SizeOfBuffer, U32 FirstClusterId, U32 * pNumClustersFree, U32 * pNumClustersChecked) {
  FS_SB         sb;
  U32           NumClusters;
  FS_FAT_INFO * pFATInfo;
  U32           LastClusterId;
  U32           LastClusterIdCalc;
  U32           iCluster;
  U32           NumClustersFree;
  int           r;
  U32           ClusterId;
  U8            fatType;
  U32           NumBytes;

  pFATInfo = &pVolume->FSInfo.FATInfo;
#if FS_SUPPORT_SECTOR_BUFFER_BURST
  (void)FS__SB_CreateEx(&sb, pVolume, pBuffer, SizeOfBuffer);
#else
  FS_USE_PARA(pBuffer);
  FS_USE_PARA(SizeOfBuffer);
  (void)FS__SB_Create(&sb, pVolume);
#endif
  NumBytes = FS__SB_GetBufferSize(&sb);
  //
  // Use the information stored in the volume instance if available.
  //
  if ((pFATInfo->NumFreeClusters != NUM_FREE_CLUSTERS_INVALID) && (pFATInfo->NumFreeClusters <= pFATInfo->NumClusters)) {
    NumClustersFree   = pFATInfo->NumFreeClusters;
    FirstClusterId    = FAT_FIRST_CLUSTER;
    LastClusterIdCalc = pFATInfo->NumClusters + FAT_FIRST_CLUSTER - 1u;
    r = 1;                                            // End of allocation table reached.
    goto Done;
  }
  //
  // The information about the available free space is not known.
  // Collect the information by scanning the allocation table.
  //
  LastClusterIdCalc = 0;
  NumClustersFree   = *pNumClustersFree;
  fatType = pFATInfo->FATType;
  switch (fatType) {
  case FS_FAT_TYPE_FAT12:
    //
    // OPTIMIZATION: Make sure that we check all the cluster ids present in the buffer.
    //
    NumClusters = (NumBytes << 3) / 12u;
    break;
  case FS_FAT_TYPE_FAT16:
    NumClusters = NumBytes >> 1;
    break;
  default:
    NumClusters = NumBytes >> 2;
    break;
  }
  LastClusterId = pFATInfo->NumClusters + FAT_FIRST_CLUSTER - 1u;
  if (FirstClusterId > LastClusterId) {
    FirstClusterId = LastClusterId;
    r = 1;                                            // End of allocation table reached.
    goto Done;
  }
  r = 0;
  LastClusterIdCalc = FirstClusterId + NumClusters - 1u;
  if (LastClusterIdCalc > LastClusterId) {
    LastClusterIdCalc = LastClusterId;
  }
#if FS_SUPPORT_SECTOR_BUFFER_BURST
  {
    U32 SectorIndex;
    U32 NumSectors;

    //
    // Limit the access of the sector buffer only to the allocation table.
    // This is needed to prevent that the device driver reports an error
    // when the file system tries to access an invalid sector.
    //
    SectorIndex = pFATInfo->RsvdSecCnt;
    NumSectors  = pFATInfo->FATSize;
    FS__SB_SetSectorRange(&sb, SectorIndex, NumSectors);
  }
#endif // FS_SUPPORT_SECTOR_BUFFER_BURST
  for (iCluster = FirstClusterId; iCluster <= LastClusterIdCalc; iCluster++) {
    ClusterId = FS_FAT_ReadFATEntry(pVolume, &sb, iCluster);
    if (FS__SB_GetError(&sb) != 0) {
      r = FS_ERRCODE_READ_FAILURE;                    // Error, could not read from allocation table.
      break;
    }
    if (ClusterId == CLUSTER_ID_INVALID) {
      r = FS_ERRCODE_READ_FAILURE;                    // Error, could not read from allocation table.
      break;
    }
    if (ClusterId == 0u) {                            // Cluster not used?
      ++NumClustersFree;
    }
  }
#if FS_SUPPORT_SECTOR_BUFFER_BURST
  FS__SB_SetSectorRange(&sb, 0, 0);                   // Remove any access limitation of the sector buffer.
#endif // FS_SUPPORT_SECTOR_BUFFER_BURST
  if (r == 0) {
    if (LastClusterIdCalc == LastClusterId) {
      pFATInfo->NumFreeClusters = NumClustersFree;    // Remember the number of free clusters so that we can use it again.
#if FS_FAT_USE_FSINFO_SECTOR
      {
        FAT_FSINFO_SECTOR * pFSInfoSector;

        pFSInfoSector = &pFATInfo->FSInfoSector;
        if (   (FAT_UseFSInfoSector != 0u)
            && (pFSInfoSector->IsPresent != 0u)
            && (pFSInfoSector->IsUpdateRequired == 0u)) {
          pFSInfoSector->IsUpdateRequired = 1;        // Request that the FSInfo sector is updated either at unmount or at synchronization.
        }
      }
#endif // FS_FAT_USE_FSINFO_SECTOR
      r = 1;                                          // End of allocation table reached.
    }
  }
Done:
  FS__SB_Delete(&sb);
  *pNumClustersChecked = (LastClusterIdCalc - FirstClusterId) + 1u;
  *pNumClustersFree    = NumClustersFree;
  return r;
}

/*********************************************************************
*
*       FS_FAT_GetATInfo
*
*  Function description
*    Returns information about the allocation table.
*
*  Parameters
*    pVolume      Volume information.
*    pATInfo      [OUT] Information about the allocation table.
*
*  Return value
*    ==0     OK, information returned.
*    < 0     Error code indicating the failure reason.
*/
int FS_FAT_GetATInfo(FS_VOLUME * pVolume, FS_AT_INFO * pATInfo) {
  FS_FAT_INFO * pFATInfo;

  pFATInfo = &pVolume->FSInfo.FATInfo;
  pATInfo->FirstClusterId = FAT_FIRST_CLUSTER;
  pATInfo->NumClusters    = pFATInfo->NumClusters;
  pATInfo->WriteCnt       = pFATInfo->WriteCntAT;
  return 0;
}

/*********************************************************************
*
*       FS_FAT_ReadATEntry
*
*  Function description
*    Returns the value stored in a specified allocation table entry.
*
*  Parameters
*    pVolume              Volume information.
*    ClusterId            Id of the cluster to read from.
*
*  Return value
*    > 0     OK, value stored in the allocation table entry.
*    ==0     OK, end of cluster chain.
*    < 0     Error code indicating the failure reason.
*/
I32 FS_FAT_ReadATEntry(FS_VOLUME * pVolume, U32 ClusterId) {
  FS_SB sb;
  I32   r;

  (void)FS__SB_Create(&sb, pVolume);
  ClusterId = FS_FAT_ReadFATEntry(pVolume, &sb, ClusterId);
  if (FS__SB_GetError(&sb) != 0) {
    r = FS_ERRCODE_READ_FAILURE;                    // Error, could not read from allocation table.
  } else {
    if (ClusterId == CLUSTER_ID_INVALID) {
      r = FS_ERRCODE_READ_FAILURE;                  // Error, could not read from allocation table.
    } else {
      r = (I32)ClusterId;
      switch (pVolume->FSInfo.FATInfo.FATType) {
      case FS_FAT_TYPE_FAT12:
        if ((ClusterId & ~0xFFFu) != 0u) {
          r = FS_ERRCODE_INVALID_CLUSTER_CHAIN;     // Error, invalid value in allocation table entry.
        } else {
          if ((ClusterId & 0xFFFu) == 0xFFFu) {
            r = 0;                                  // OK, end of chain marker.
          }
        }
        break;
      case FS_FAT_TYPE_FAT16:
        if ((ClusterId & ~0xFFFFuL) != 0u) {
          r = FS_ERRCODE_INVALID_CLUSTER_CHAIN;     // Error, invalid value in allocation table entry.
        } else {
          if ((ClusterId & 0xFFFFuL) == 0xFFFFuL) {
            r = 0;                                  // OK, end of chain marker.
          }
        }
        break;
      case FS_FAT_TYPE_FAT32:
        if ((ClusterId & ~0xFFFFFFFuL) != 0u) {
          r = FS_ERRCODE_INVALID_CLUSTER_CHAIN;     // Error, invalid value in allocation table entry.
        } else {
          if ((ClusterId & 0xFFFFFFFuL) == 0xFFFFFFFuL) {
            r = 0;                                  // OK, end of chain marker.
          }
        }
        break;
      default:
        r = FS_ERRCODE_INVALID_FS_TYPE;
        break;
      }
    }
  }
  FS__SB_Delete(&sb);
  return r;
}

/*********************************************************************
*
*       FS_FAT_CalcDirEntryIndex
*
*  Function description
*    Calculates the index of the directory entry relative to
*    the beginning of the logical sector that stores that entry.
*/
I32 FS_FAT_CalcDirEntryIndex(FS_SB * pSB, const FS_FAT_DENTRY * pDirEntry) {
  I32   DirEntryIndex;
  U8  * pBuffer;

  pBuffer = FS__SB_GetBuffer(pSB);
  DirEntryIndex = (I32)(pDirEntry - SEGGER_PTR2PTR(FS_FAT_DENTRY, pBuffer));        //lint !e946 !e947 D:106
  return DirEntryIndex;
}

#if FS_SUPPORT_FAT

/*********************************************************************
*
*       FS_FAT_Save
*
*  Function description
*    Saves the global and static variables used by the FAT implementation.
*/
void FS_FAT_Save(FS_CONTEXT * pContext) {
  pContext->FAT_pDirEntryAPI     = SEGGER_CONSTPTR2PTR(const void, FAT_pDirEntryAPI);               // MISRA deviation N:107
#if FS_FAT_USE_FSINFO_SECTOR
  pContext->FAT_UseFSInfoSector  = FAT_UseFSInfoSector;
#endif
#if FS_MAINTAIN_FAT_COPY
  pContext->FAT_MaintainFATCopy  = FAT_MaintainFATCopy;
#endif
#if FS_FAT_PERMIT_RO_FILE_MOVE
  pContext->FAT_PermitROFileMove = FAT_PermitROFileMove;
#endif
#if FS_FAT_UPDATE_DIRTY_FLAG
  pContext->FAT_UpdateDirtyFlag  = FAT_UpdateDirtyFlag;
#endif
  FS_FAT_CHECKDISK_Save(pContext);
#if FS_SUPPORT_FILE_NAME_ENCODING
  FS_FAT_LFN_Save(pContext);
#endif // FS_SUPPORT_FILE_NAME_ENCODING
}

/*********************************************************************
*
*       FS_FAT_Restore
*
*  Function description
*    Restores the global and static variables used by the FAT implementation.
*/
void FS_FAT_Restore(const FS_CONTEXT * pContext) {
  FAT_pDirEntryAPI     = SEGGER_CONSTPTR2PTR(const FAT_DIRENTRY_API, pContext->FAT_pDirEntryAPI);   // MISRA deviation N:107
#if FS_FAT_USE_FSINFO_SECTOR
  FAT_UseFSInfoSector  = pContext->FAT_UseFSInfoSector;
#endif
#if FS_MAINTAIN_FAT_COPY
  FAT_MaintainFATCopy  = pContext->FAT_MaintainFATCopy;
#endif
#if FS_FAT_PERMIT_RO_FILE_MOVE
  FAT_PermitROFileMove = pContext->FAT_PermitROFileMove;
#endif
#if FS_FAT_UPDATE_DIRTY_FLAG
  FAT_UpdateDirtyFlag  = pContext->FAT_UpdateDirtyFlag;
#endif
  FS_FAT_CHECKDISK_Restore(pContext);
#if FS_SUPPORT_FILE_NAME_ENCODING
  FS_FAT_LFN_Restore(pContext);
#endif // FS_SUPPORT_FILE_NAME_ENCODING
}

#endif // FS_SUPPORT_FAT

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

#if FS_SUPPORT_FAT

/*********************************************************************
*
*       FS_FAT_GrowRootDir
*
*  Function description
*    Increases the size of the root directory.
*
*  Parameters
*    sVolumeName    Name of the volume for which to increase the root
*                   directory.
*    NumAddEntries  Number of directory entries to be added.
*
*  Return value
*    >  0           Number of entries added.
*    == 0           Clusters after root directory are not free.
*                   The number of entries in the root directory
*                   has not been changed.
*    == 0xFFFFFFFF  An error occurred.
*
*  Additional information
*    The formatting function allocates per default one cluster
*    for the root directory of a FAT32 formatted volume. The file
*    system increases automatically the size of the root directory
*    as more files are added to it. This operation has a certain
*    overhead that depends on the size of the allocation table
*    and on the available free space. This overhead can be eliminated
*    by calling FS_FAT_GrowRootDir() to increase the size of the
*    root directory to the number of files and directories the
*    application is expected to store in it.
*
*    This function increases the size of the root directory on a
*    FAT32 formatted volume by the number of entries specified in
*    NumAddEntries. The file system allocates one directory entry
*    for each file or directory if the support for long file names
*    is not enabled. With the support for long file names enabled
*    the number of directory entries allocated to a file or directory
*    depends on the number of characters in the name of the created
*    file or directory.
*
*    This function shall be called after formatting the volume.
*    The function fails with an error if:
*    * is not called after format operation.
*    * the specified volume is formatted as FAT12 or FAT16.
*    * the required number of clusters cannot be allocated
*      immediately after the cluster already allocated to the
*      root directory.
*    FS_FAT_GrowRootDir() is available only if the compile-time option
*    FS_SUPPORT_FAT set to 1.
*/
U32 FS_FAT_GrowRootDir(const char * sVolumeName, U32 NumAddEntries) {
  U32 r;

  FS_LOCK();
  r = _GrowRootDir(sVolumeName, NumAddEntries);
  FS_UNLOCK();
  return r;
}

#if FS_FAT_USE_FSINFO_SECTOR

/*********************************************************************
*
*       FS_FAT_ConfigFSInfoSectorUse
*
*  Function description
*    Enables / disables the usage of information from the FSInfo sector.
*
*  Parameters
*    OnOff      Activation status of the option.
*               * 0   Disable the option.
*               * 1   Enable the option.
*
*  Additional information
*    FSInfo sector is a management sector present on FAT32-formatted
*    volumes that stores information about the number of free clusters
*    and the id of the first free cluster. This information, when
*    available and valid, can be used to increase the performance of
*    the operation that calculates the available free space on a volume
*    such as FS_GetVolumeFreeSpace(), FS_GetVolumeFreeSpaceKB(),
*    FS_GetVolumeInfo(), or FS_GetVolumeInfoEx(). If the information
*    in the FSInfo sector is missing or invalid, the file system has
*    to scan the entire allocation to calculate the available free
*    space an operation that can take a long time to complete on
*    storage devices with a large capacity (few Gbytes.)
*
*    The file system invalidates the information in the FSInfo sector
*    on the first operation that allocates or frees a cluster. The
*    FSInfo sector is updated when the volume is unmounted via
*    FS_Unmount() or synchronized via FS_Sync().
*
*    FSInfo sector is evaluated and updated by default if the
*    compile-time option FS_FAT_USE_FSINFO_SECTOR is set to 1.
*    FS_FAT_ConfigFSInfoSectorUse() is available only if
*    the compile-time option FS_FAT_USE_FSINFO_SECTOR is set to 1.
*/
void FS_FAT_ConfigFSInfoSectorUse(int OnOff) {
  FS_LOCK();
  FS_LOCK_SYS();
  FAT_UseFSInfoSector = (U8)OnOff;
  FS_UNLOCK_SYS();
  FS_UNLOCK();
}

#endif // FS_FAT_USE_FSINFO_SECTOR

#if FS_MAINTAIN_FAT_COPY

/*********************************************************************
*
*       FS_FAT_ConfigFATCopyMaintenance
*
*  Function description
*    Enables / disables the update of the second allocation table.
*
*  Parameters
*    OnOff      Activation status of the option.
*               * 0   Disable the option.
*               * 1   Enable the option.
*
*  Additional information
*    The FAT file system has support for a second (redundant)
*    allocation table. FS_FAT_ConfigFATCopyMaintenance() can be used
*    to enable or disable the update of the second allocation table.
*    The data in the second allocation table is not used by the file
*    system but it may be required to be present by some PC file
*    system checking utilities. Enabling this option can possible
*    reduce the write performance of the file system.
*
*    The update of the second allocation table is enabled by default
*    if the compile-time option FS_MAINTAIN_FAT_COPY is set to 1.
*    FS_FAT_ConfigFATCopyMaintenance() is available only if the
*    compile-time option FS_MAINTAIN_FAT_COPY is set to 1.
*/
void FS_FAT_ConfigFATCopyMaintenance(int OnOff) {
  FS_LOCK();
  FS_LOCK_SYS();
  FAT_MaintainFATCopy = (U8)OnOff;
  FS_UNLOCK_SYS();
  FS_UNLOCK();
}

#endif // FS_MAINTAIN_FAT_COPY

#if FS_FAT_PERMIT_RO_FILE_MOVE

/*********************************************************************
*
*       FS_FAT_ConfigROFileMovePermission
*
*  Function description
*    Enables / disables the permission to move (and rename)
*    files and directories with the read-only file attribute set.
*
*  Parameters
*    OnOff      Activation status of the option.
*               * 0   Disable the option.
*               * 1   Enable the option.
*
*  Additional information
*    The application is per default allowed to move or rename via
*    FS_Move() and FS_Rename() respectively files and directories
*    that have the read-only file attribute (FS_ATTR_READ_ONLY) set.
*    FS_FAT_ConfigROFileMovePermission() can be used to disable this
*    option and thus to prevent an application to perform move
*    or rename operations on files and directories marked as
*    read-only.
*
*    FS_FAT_ConfigROFileMovePermission() is available only if the
*    compile-time option FS_FAT_PERMIT_RO_FILE_MOVE is set to 1.
*/
void FS_FAT_ConfigROFileMovePermission(int OnOff) {
  FS_LOCK();
  FS_LOCK_SYS();
  FAT_PermitROFileMove = (U8)OnOff;
  FS_UNLOCK_SYS();
  FS_UNLOCK();
}

#endif // FS_FAT_PERMIT_RO_FILE_MOVE

#if FS_FAT_UPDATE_DIRTY_FLAG

/*********************************************************************
*
*       FS_FAT_ConfigDirtyFlagUpdate
*
*  Function description
*    Enables / disables the update of the flag that indicates if
*    the volume has been unmounted correctly.
*
*  Parameters
*    OnOff      Activation status of the option.
*               * 0   Disable the option.
*               * 1   Enable the option.
*
*  Additional information
*    If enabled, the file system updates an internal dirty flag that
*    is set to 1 each time data is written to storage device. The dirty
*    flag is set to 0 when the application unmounts the file system.
*    The value of the dirty flag is updated to storage device and
*    can be used to check if the storage device has been properly
*    unmounted before the system reset. FS_GetVolumeInfo() can
*    be used to get the value of this dirty flag (IsDirty member
*    of FS_DISK_INFO).
*
*    The update of the dirty flag is enabled by default if the
*    compile-time option FS_FAT_UPDATE_DIRTY_FLAG is set to 1.
*    FS_FAT_ConfigDirtyFlagUpdate() is available only if
*    the compile-time option FS_FAT_UPDATE_DIRTY_FLAG is set to 1.
*/
void FS_FAT_ConfigDirtyFlagUpdate(int OnOff) {
  FS_LOCK();
  FS_LOCK_SYS();
  FAT_UpdateDirtyFlag = (U8)OnOff;
  FS_UNLOCK_SYS();
  FS_UNLOCK();
}

#endif // FS_FAT_UPDATE_DIRTY_FLAG

/*********************************************************************
*
*       FS_FAT_GetConfig
*
*  Function description
*    Returns information about how the FAT component is configured to operate.
*
*  Parameters
*    pConfig    [OUT] Configuration information.
*
*  Return value
*    ==0      OK, information returned.
*    !=0      Error code indicating the failure reason.
*/
int FS_FAT_GetConfig(FS_FAT_CONFIG * pConfig) {
  int r;

  FS_LOCK();
  FS_LOCK_SYS();
  r = FS_ERRCODE_INVALID_PARA;
  if (pConfig != NULL) {
    _GetConfig(pConfig);
    r = FS_ERRCODE_OK;
  }
  FS_UNLOCK_SYS();
  FS_UNLOCK();
  return r;
}

#endif // FS_SUPPORT_FAT

/*********************************************************************
*
*       Public const data
*
**********************************************************************
*/
const FAT_DIRENTRY_API FAT_SFN_API = {
  _SFN_ReadDirEntryInfo,
  _SFN_FindDirEntry,
  _SFN_CreateDirEntry,
  NULL
};

/*************************** End of file ****************************/
