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
File        : FS_FAT_Dir.c
Purpose     : FSL Directory functions
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
*       Static const data
*
**********************************************************************
*/
static const FS_83NAME NameDirDot    = {{(U8)'.', 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20}};
static const FS_83NAME NameDirDotDot = {{(U8)'.', (U8)'.',  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20}};

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

//lint -esym(9070, _DeleteDirectoryTree) recursive function D:114. Reason: The most natural way to implement the operation due to the recursive structure of the file system.
//lint -efunc(818, FS_FAT_CloseDir) pDirObj could be declared as pointing to const N:104. Rationale: this is an API function and we want to be flexible about what we can do with pDirObj.

/*********************************************************************
*
*       _OpenDir
*
*  Function description
*    Open an existing directory for reading.
*
*  Parameters
*    pDirName             Name of the directory to be opened.
*    pDirObj              [OUT] Receives information about the opened directory.
*    pSB                  Sector buffer to be used for the read or write operations.
*
*  Return value
*    ==0      OK, directory opened.
*    !=0      Error code indicating the failure reason.
*/
static int _OpenDir(const char * pDirName, FS_DIR_OBJ * pDirObj, FS_SB * pSB) {
  U32                   Len;
  const char          * pFileName;
  FS_VOLUME           * pVolume;
  U32                   ClusterId;
  FS_FAT_DENTRY       * pDirEntry;
  unsigned              Attr;
  FS_DIR_POS            DirPos;
  U32                   SectorIndex;
  FS_DIRENTRY_POS_FAT * pDirEntryPos;
  I32                   DirEntryIndex;

  ClusterId     = 0;
  SectorIndex   = SECTOR_INDEX_INVALID;
  DirEntryIndex = 0;
  pVolume       = pDirObj->pVolume;
  FS_MEMSET(&DirPos, 0, sizeof(DirPos));
  //
  // Find parent directory on the media and return file name part of the complete path, as well as location and size info.
  //
  if (FS_FAT_FindPath(pVolume, pSB, pDirName, &pFileName, &ClusterId) == 0) {
    return FS_ERRCODE_PATH_NOT_FOUND;                   // Error, directory not found.
  }
  //
  // Parent directory found.
  //
  Len = (U32)FS_STRLEN(pFileName);
  if (Len != 0u) {
    //
    // There is a name in the complete path (it does not end with a '\')
    //
    pDirEntry = FS_FAT_FindDirEntryEx(pVolume, pSB, pFileName, 0, ClusterId, &DirPos, FS_FAT_ATTR_DIRECTORY, NULL);
    if (pDirEntry == NULL) {
      return FS_ERRCODE_FILE_DIR_NOT_FOUND;             // Error, directory not found.
    }
    //
    // Check if the directory entry has the directory attribute set
    //
    Attr = pDirEntry->Data[DIR_ENTRY_OFF_ATTRIBUTES];
    if ((Attr & FS_FAT_ATTR_DIRECTORY) != FS_FAT_ATTR_DIRECTORY) {
      return FS_ERRCODE_NOT_A_DIR;                      // Error, specified name is not a directory.
    }
    SectorIndex   = FS__SB_GetSectorIndex(pSB);
    DirEntryIndex = FS_FAT_CalcDirEntryIndex(pSB, pDirEntry);
    ClusterId     = FS_FAT_GetFirstCluster(pDirEntry);
  }
  pDirEntryPos = &pDirObj->ParentDirPos.fat;
  pDirEntryPos->SectorIndex      = SectorIndex;         // Index of the logical sector that stores the directory entry of the parent directory.
  pDirEntryPos->DirEntryIndex    = (U32)DirEntryIndex;  // Position of directory entry in the parent directory relative to logical sector.
  pDirObj->DirPos.FirstClusterId = ClusterId;           // Id of the first cluster assigned to the opened directory.
  pDirObj->DirPos.DirEntryIndex  = 0;                   // Set the position in the directory to first entry.
  return FS_ERRCODE_OK;                                 // OK, directory opened.
}

/*********************************************************************
*
*       _FreeClusterChain
*
*  Function description
*    Frees all clusters allocated to a file or directory.
*
*  Parameters
*    pVolume        Volume information.
*    pDirEntry      Pointer to directory entry to be freed (in sector buffer).
*    pSB            Sector buffer to be used for the read/write operations.
*
*  Return value
*    ==0    OK, file or directory deleted.
*    !=0    An error occurred.
*/
static int _FreeClusterChain(FS_VOLUME * pVolume, const FS_FAT_DENTRY * pDirEntry, FS_SB * pSB) {
  U32           FirstCluster;
  U32           NumClusters;
  FS_FAT_INFO * pFATInfo;
  int           r;
  U32           FileSize;
  unsigned      Attr;
  int           IsFile;

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
    FileSize    = FS_LoadU32LE(&pDirEntry->Data[DIR_ENTRY_OFF_SIZE]);
    NumClusters = (FileSize + pFATInfo->BytesPerCluster - 1u) >> pFATInfo->ldBytesPerCluster;
  } else {
    NumClusters = FAT_MAX_NUM_CLUSTERS_DIR;
  }
  FirstCluster = FS_FAT_GetFirstCluster(pDirEntry);
  //
  // Mark the volume as dirty.
  //
  FS_FAT_UpdateDirtyFlagIfRequired(pVolume, 1);
  //
  // Free clusters in the allocation table.
  //
  r = FS_FAT_FreeClusterChain(pVolume, pSB, FirstCluster, NumClusters);
  //
  // We have to ignore the error about invalid cluster chain of directory
  // because the actual number of clusters allocated to a directory is not known.
  //
  if (IsFile == 0) {
    if (r == FS_ERRCODE_INVALID_CLUSTER_CHAIN) {
      r = 0;
    }
  }
  return r;
}

/*********************************************************************
*
*       _DeleteDirectoryTree
*
*  Function description
*    FS internal function. Deletes a directory and its contents recursively.
*
*  Return value
*    ==0    Directory has been deleted
*    !=0    Error code indicating the failure reason
*
*  Notes
*    (1) This function calls itself recursively.
*/
static int _DeleteDirectoryTree(FS_VOLUME * pVolume, U32 FirstClusterId, FS_DIRENTRY_INFO * pDirEntryInfo, int MaxRecursionLevel, FS_SB * pSB) {
  int             r;
  FS_DIR_OBJ      DirObj;
  unsigned        Attr;
  int             IsDotDirEntry;
  U32             FirstClusterIdSubDir;
  int             DoDelete;
  int             Result;
  FS_FAT_DENTRY * pDirEntry;
  FS_DIR_POS      DirPos;
  FS_DIR_POS      DirPosLFN;
  U32             NumDirEntries;
  int             OneLevel;
  U32             DirEntryIndex;

  r             = 0;                  // Set to indicate success.
  NumDirEntries = 0;
  FS_MEMSET(&DirObj, 0, sizeof(DirObj));
  DirObj.DirPos.FirstClusterId = FirstClusterId;
  DirObj.pVolume               = pVolume;
  OneLevel = 1;
  if (MaxRecursionLevel < 0) {
    OneLevel = -1;
  }
  for (;;) {
    FS_FAT_InvalidateDirPos(&DirPosLFN);
    Result = FAT_pDirEntryAPI->pfReadDirEntryInfo(&DirObj, pDirEntryInfo, &DirPosLFN, pSB);
    if (Result != 0) {
      if (Result < 0) {
        r = Result;
      }
      break;                          // End of directory reached or an error occurred.
    }
    ++NumDirEntries;
    if (MaxRecursionLevel == 0) {
      //
      // Check only that the directory is empty. A subdirectory always contains at least 2 entries: "." and ".."
      //
      if (NumDirEntries > 2u) {
        r = FS_ERRCODE_DIR_NOT_EMPTY; // Error, deepest recursion level reached and the directory is not empty.
        break;
      }
    } else {
      Attr     = pDirEntryInfo->Attributes;
      DoDelete = 1;
      if ((Attr & FS_FAT_ATTR_DIRECTORY) != 0u) {
        //
        // Do not recurse into nor delete "." and ".." directory entries.
        //
        IsDotDirEntry = FS__IsSystemDirName(pDirEntryInfo->sFileName);
        if (IsDotDirEntry != 0) {
          DoDelete = 0;
        } else {
          FirstClusterIdSubDir = pDirEntryInfo->FirstClusterId;
          r = _DeleteDirectoryTree(pVolume, FirstClusterIdSubDir, pDirEntryInfo, MaxRecursionLevel - OneLevel, pSB);
          if (r != 0) {
            break;                      // Error, could not delete directory tree.
          }
        }
      }
      if (DoDelete != 0) {
        //
        // Delete the directory entry.
        //
        r = FS_ERRCODE_FILE_DIR_NOT_FOUND;
        DirPos = DirObj.DirPos;         // struct copy.
        if (DirPos.DirEntryIndex != 0u) {
          DirPos.DirEntryIndex--;       // Go back one position to the current entry.
        }
        DirEntryIndex = DirPos.DirEntryIndex;
        pDirEntry = FS_FAT_GetDirEntry(pVolume, pSB, &DirPos);
        if (pDirEntry != NULL) {
          if (MaxRecursionLevel > 0) {
            r = FS_FAT_DeleteFileOrDir(pVolume, pSB, pDirEntry, DirEntryIndex, &DirPosLFN);
          } else {
            r = _FreeClusterChain(pVolume, pDirEntry, pSB);
          }
        }
        if (r != 0) {
          break;                        // Error, could not delete the directory entry.
        }
      }
    }
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
*       FS_FAT_CreateDirEx
*
*  Function description
*    FS internal function. Create a directory in the directory specified
*    with DirStart. Do not call, if you have not checked before for
*    existing directory with name pDirName.
*
*  Parameters
*    pVolume    Identifies the volume on which the directory has to be created.
*    pDirName   Name of the directory as a 0-terminated string.
*    DirStart   Start of directory, where to create pDirName.
*    pSB        Sector buffer to work with.
*
*  Return value
*    ==0      Directory has been created.
*    !=0      Error code indicating the failure reason.
*/
int FS_FAT_CreateDirEx(FS_VOLUME * pVolume, const char * pDirName, U32 DirStart, FS_SB * pSB) {
  U32             DirSector;
  U32             Cluster;
  FS_FAT_DENTRY * pDirEntry;
  U32             TimeDate;
  U16             Date;
  U16             Time;
  FS_FAT_INFO   * pFATInfo;
  U32             NumSectors;
  int             r;
  U8            * pBuffer;
  FS_PARTITION  * pPart;

  pFATInfo = &pVolume->FSInfo.FATInfo;
  TimeDate = FS__GetTimeDate();
  //
  // First, mark the volume as dirty.
  //
  FS_FAT_UpdateDirtyFlagIfRequired(pVolume, 1);
  //
  // Allocate a new cluster.
  //
  Cluster = FS_FAT_AllocCluster(pVolume, pSB, 0);
  if (Cluster == 0u) {
    FS_DEBUG_ERROROUT((FS_MTYPE_FS, "FS_FAT_CreateDirEx: No free cluster found."));
    return FS_ERRCODE_VOLUME_FULL;       // Error, could not allocate cluster.
  }
  Time = (U16)(TimeDate & 0xFFFFu);
  Date = (U16)(TimeDate >> 16);
  pDirEntry = FAT_pDirEntryAPI->pfCreateDirEntry(pVolume, pSB, pDirName, DirStart, Cluster, FS_FAT_ATTR_DIRECTORY, 0, Time, Date);
  if (pDirEntry != NULL) {
    //
    // Free entry found. Store the directory entry.
    //
    FS__SB_MarkDirty(pSB);
    FS__SB_Clean(pSB);
    //
    // Make the "." and ".." entries.
    //
    pBuffer = FS__SB_GetBuffer(pSB);
    FS_MEMSET(pBuffer, 0x00, pFATInfo->BytesPerSector);
    DirSector = FS_FAT_ClusterId2SectorNo(pFATInfo, Cluster);   // Find 1st absolute sector of the new directory.
    FS__SB_MarkValid(pSB, DirSector, FS_SECTOR_TYPE_DIR, 1);
    pDirEntry = SEGGER_PTR2PTR(FS_FAT_DENTRY, pBuffer);         // MISRA deviation D:100[e]
    FS_FAT_WriteDirEntry83(pDirEntry++, &NameDirDot,    Cluster,  FS_FAT_ATTR_DIRECTORY, 0, Time, Date, 0);
    FS_FAT_WriteDirEntry83(pDirEntry,   &NameDirDotDot, DirStart, FS_FAT_ATTR_DIRECTORY, 0, Time, Date, 0);
    FS__SB_Clean(pSB);
    //
    // Clear rest of the directory cluster if necessary.
    //
    NumSectors = (U32)pFATInfo->SectorsPerCluster - 1u;         // -1 since the first sector in the cluster is already initialized.
    if (NumSectors == 0u) {
      return 0;           // OK, directory initialized.
    }
    //
    // Initialize the remaining of directory entries.
    //
    ++DirSector;                                                // ++ since the first sector in the cluster is already initialized.
    pPart = &pVolume->Partition;
    FS_MEMSET(pBuffer, 0x00, pFATInfo->BytesPerSector);
    r = FS_LB_WriteMultiplePart(pPart, DirSector, NumSectors, pBuffer, FS_SECTOR_TYPE_DIR, 1);
    FS__SB_MarkNotValid(pSB);
#if FS_SUPPORT_SECTOR_BUFFER_CACHE
    FS__InvalidateSectorBuffer(pVolume, DirSector, NumSectors);
#endif // FS_SUPPORT_SECTOR_BUFFER_CACHE
    if (r == 0) {
      return 0;           // It worked O.K. !
    }
  }
  (void)FS_FAT_FreeClusterChain(pVolume, pSB, Cluster, 1);
  FS_DEBUG_ERROROUT((FS_MTYPE_FS, "FS_FAT_CreateDirEx: No free directory entry found."));
  return FS_ERRCODE_READ_FAILURE;                               // Error, directory full and can not be increased.
}

/*********************************************************************
*
*       FS_FAT_OpenDir
*
*  Function description
*    Open an existing directory for reading.
*
*  Parameters
*    sDirName     Name of the directory to be opened.
*    pDirObj      Receives information about the opened directory.
*
*  Return value
*    ==0      OK, directory opened.
*    !=0      Error code indicating the failure reason.
*/
int FS_FAT_OpenDir(const char * sDirName, FS_DIR_OBJ * pDirObj) {
  int   r;
  FS_SB sb;

  if (pDirObj == NULL) {
    return FS_ERRCODE_INVALID_PARA;         // Error, no valid pointer to a FS_DIR structure.
  }
  (void)FS__SB_Create(&sb, pDirObj->pVolume);
  r = _OpenDir(sDirName, pDirObj, &sb);
  FS__SB_Delete(&sb);
  if (r == 0) {
    r = FS__SB_GetError(&sb);
  }
  return r;
}

/*********************************************************************
*
*       FS_FAT_CloseDir
*
*  Function description
*    FS internal function. Close a directory referred by pDir.
*
*  Parameters
*    pDirObj  Specified the directory to be closed.
*
*  Return value
*    ==0      Directory has been closed.
*    !=0      Error code indicating the failure reason.
*/
int FS_FAT_CloseDir(FS_DIR_OBJ * pDirObj) {
  if (pDirObj == NULL) {
    return FS_ERRCODE_INVALID_PARA;
  }
  return FS_ERRCODE_OK;
}

/*********************************************************************
*
*       FS_FAT_ReadDir
*
*  Function description
*    Read next directory entry in directory specified by pDir.
*
*  Parameters
*    pDirObj        Specifies the directory to read from.
*    pDirEntryInfo  Receives the information about the directory entry.
*
*  Return value
*    ==1      Last directory entry reached.
*    ==0      OK, directory entry read.
*    < 0      Error code indicating the failure reason.
*/
int FS_FAT_ReadDir(FS_DIR_OBJ * pDirObj, FS_DIRENTRY_INFO * pDirEntryInfo) {
  FS_VOLUME * pVolume;
  int         r;
  FS_SB       sb;

  if (pDirObj == NULL) {
    return FS_ERRCODE_INVALID_PARA;   // Error, no valid pointer to a FS_DIR structure.
  }
  pVolume  = pDirObj->pVolume;
  (void)FS__SB_Create(&sb, pVolume);
  r = FAT_pDirEntryAPI->pfReadDirEntryInfo(pDirObj, pDirEntryInfo, NULL, &sb);
  FS__SB_Delete(&sb);
  return r;
}

/*********************************************************************
*
*       FS_FAT_RemoveDir
*
*  Function description
*    Remove a directory.
*    If you call this function to remove a directory, you must make sure, that
*    it is already empty.
*
*  Parameters
*    pVolume            Volume on which the directory is located.
*    sDirName           Name of the directory to be deleted.
*
*  Return value
*    ==0        OK, Directory has been removed.
*    !=0        Error code indicating the failure reason
*/
int FS_FAT_RemoveDir(FS_VOLUME * pVolume, const char * sDirName) {
  U32             Len;
  U32             DirStart;
  const char    * pFileName;
  int             r;
  FS_SB           sb;
  FS_FAT_DENTRY * pDirEntry;
  FS_DIR_POS      DirPosLFN;

  FS_FAT_InvalidateDirPos(&DirPosLFN);
  (void)FS__SB_Create(&sb, pVolume);
  if (FS_FAT_FindPath(pVolume, &sb, sDirName, &pFileName, &DirStart) != 0) {
    Len = (U32)FS_STRLEN(pFileName);
    if (Len != 0u) {
      pDirEntry = FS_FAT_FindDirEntry(pVolume, &sb, pFileName, 0, DirStart, FS_FAT_ATTR_DIRECTORY, &DirPosLFN);
      if (pDirEntry != NULL) {
        r = FS_ERRCODE_OK;
        if (FS_FAT_DeleteFileOrDir(pVolume, &sb, pDirEntry, 0, &DirPosLFN) != 0) {     // 0 means unspecified directory entry index.
          r = FS_ERRCODE_WRITE_FAILURE;
        }
      } else {
        r = FS_ERRCODE_FILE_DIR_NOT_FOUND;  // Error, directory name not found.
      }
    } else {
      r = FS_ERRCODE_INVALID_PARA;          // Error, directory name not specified.
    }
  } else {
    r = FS_ERRCODE_PATH_NOT_FOUND;          // Error, path to directory not found.
  }
  FS__SB_Delete(&sb);
  if (r == 0) {
    r = FS__SB_GetError(&sb);
  }
  return r;
}

/*********************************************************************
*
*       FS_FAT_CreateDir
*
*  Function description
*    Creates a directory.
*
*  Parameters
*    pVolume      Volume on which to create the directory.
*    sDirName     Directory name.
*
*  Return value
*    ==0      Directory has been created.
*    !=0      Error code indicating the failure reason.
*/
int FS_FAT_CreateDir(FS_VOLUME * pVolume, const char * sDirName) {
  U32             Len;
  U32             DirStart;
  const char    * pFileName;
  int             r;
  FS_SB           sb;
  FS_FAT_DENTRY * pDirEntry;

  (void)FS__SB_Create(&sb, pVolume);
  if (FS_FAT_FindPath(pVolume, &sb, sDirName, &pFileName, &DirStart) != 0) {
    Len = (U32)FS_STRLEN(pFileName);
    if (Len != 0u) {
      pDirEntry = FS_FAT_FindDirEntry(pVolume, &sb, pFileName, 0, DirStart, 0, NULL);
      if (pDirEntry == NULL) {
        r = FS_FAT_CreateDirEx(pVolume, pFileName, DirStart, &sb);  // Create the directory.
      } else {
        r = FS_ERRCODE_FILE_DIR_EXISTS; // Error, a file or a directory with the same name already exists.
      }
    } else {
      r = FS_ERRCODE_INVALID_PARA;      // Error, directory name not specified.
    }
  } else {
    r = FS_ERRCODE_PATH_NOT_FOUND;      // Error, path to directory not found.
  }
  FS__SB_Delete(&sb);
  if (r == 0) {
    r = FS__SB_GetError(&sb);
  }
  return r;
}

/*********************************************************************
*
*       FS_FAT_DeleteDir
*
*  Function description
*    Remove a directory including its contents.
*
*  Parameters
*    pVolume            Volume on which the directory is located.
*    sDirName           Name of the directory to be deleted.
*    MaxRecursionLevel  Maximum depth of the directory tree.
*
*  Return value
*    ==0        OK, directory has been removed
*    !=0        Error code indicating the failure reason
*/
int FS_FAT_DeleteDir(FS_VOLUME * pVolume, const char * sDirName, int MaxRecursionLevel) {
  U32                FileNameLen;
  U32                FirstClusterId;
  U32                FirstClusterIdDirTree;
  const char       * pFileName;
  int                r;
  FS_SB              sb;
  FS_FAT_DENTRY    * pDirEntry;
  FS_DIR_POS         DirPosLFN;
  FS_DIRENTRY_INFO   DirEntryInfo;
  char               acFileName[4];   // At least 3 characters to check for "." and ".." special entries and 1 character for the 0-terminator.

  FirstClusterId    = 0;
  FS_FAT_InvalidateDirPos(&DirPosLFN);
  (void)FS__SB_Create(&sb, pVolume);
  //
  // Search for the parent directory.
  //
  if (FS_FAT_FindPath(pVolume, &sb, sDirName, &pFileName, &FirstClusterId) != 0) {
    //
    // OK, parent directory found. Now check if a directory name is specified.
    //
    FileNameLen = (U32)FS_STRLEN(pFileName);
    if (FileNameLen != 0u) {
      //
      // OK, directory name specified. Search for the directory name in the parent directory.
      //
      pDirEntry = FS_FAT_FindDirEntry(pVolume, &sb, pFileName, 0, FirstClusterId, FS_FAT_ATTR_DIRECTORY, NULL);
      if (pDirEntry != NULL) {
        //
        // OK, directory name found in the parent directory.
        // Delete the directory tree recursively.
        //
        FS_MEMSET(&DirEntryInfo, 0, sizeof(DirEntryInfo));
        FS_MEMSET(acFileName, 0, sizeof(acFileName));
        DirEntryInfo.sFileName      = acFileName;
        DirEntryInfo.SizeofFileName = (int)sizeof(acFileName);
        FirstClusterIdDirTree = FS_FAT_GetFirstCluster(pDirEntry);
        r = _DeleteDirectoryTree(pVolume, FirstClusterIdDirTree, &DirEntryInfo, MaxRecursionLevel, &sb);
        if (r == 0) {
          //
          // Remove the top directory entry if no errors occurred during the removal of directory tree.
          //
          r = FS_ERRCODE_FILE_DIR_NOT_FOUND;  // Error, directory name not found.
          pDirEntry = FS_FAT_FindDirEntry(pVolume, &sb, pFileName, 0, FirstClusterId, FS_FAT_ATTR_DIRECTORY, &DirPosLFN);
          if (pDirEntry != NULL) {
            r = FS_FAT_DeleteFileOrDir(pVolume, &sb, pDirEntry, 0, &DirPosLFN);     // 0 means unspecified directory entry index.
          }
        }
      } else {
        r = FS_ERRCODE_FILE_DIR_NOT_FOUND;  // Error, directory name not found.
      }
    } else {
      r = FS_ERRCODE_INVALID_PARA;          // Error, directory name not specified.
    }
  } else {
    r = FS_ERRCODE_PATH_NOT_FOUND;          // Error, path to directory not found.
  }
  FS__SB_Delete(&sb);
  if (r == 0) {
    r = FS__SB_GetError(&sb);
  }
  return r;
}
/*************************** End of file ****************************/
