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
File        : FS_FAT_SetEndOfFile.c
Purpose     : Routines for modifying the size of a file
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include section
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
*       _ShortenClusterChain
*
*  Function description
*    Makes a cluster chain shorter.
*
*  Parameters
*    pVolume            Volume on which the cluster chain is stored.
*    pSB                Buffer to be used for the read and write operations.
*    FirstCluster       Id of the first cluster in the chain.
*    NumClustersAct     Actual number of clusters in the chain.
*    NumClustersNew     Number of clusters in the resulting chain.
*    pLastCluster       [OUT] Id of the last cluster in the chain.
*
*  Return value
*    ==0    OK, cluster chain shortened.
*    !=0    Error code indicating the failure reason.
*
*  Additional information
*    This function removes clusters from the end of a cluster chain.
*    The removed clusters are marked as free. The last cluster in
*    the resulting cluster chain is marked as end-of-chain.
*/
static int _ShortenClusterChain(FS_VOLUME * pVolume, FS_SB * pSB, U32 FirstCluster, U32 NumClustersAct, U32 NumClustersNew, U32 * pLastCluster) {
  int r;
  U32 LastCluster;
  U32 FirstClusterToDelete;
  U32 NumClustersToDelete;

  NumClustersToDelete = NumClustersAct - NumClustersNew;
  //
  // Get the id of the last cluster allocated to file. It is marked below as end-of-chain.
  //
  LastCluster = FS_FAT_WalkCluster(pVolume, pSB, FirstCluster, NumClustersNew - 1u);
  if (LastCluster == 0u) {
    return FS_ERRCODE_INVALID_CLUSTER_CHAIN;  // Error, cluster chain too short or read error.
  }
  //
  // Get the id of the first cluster to be deleted. This is the next cluster after the last one.
  //
  FirstClusterToDelete = FS_FAT_WalkCluster(pVolume, pSB, LastCluster, 1);
  if (FirstClusterToDelete == 0u) {
    return FS_ERRCODE_INVALID_CLUSTER_CHAIN;  // Error, cluster chain too short or read error.
  }
  //
  // Mark the last cluster as end-of-chain.
  //
  r = FS_FAT_MarkClusterEOC(pVolume, pSB, LastCluster);
  if (r != 0) {
    return FS_ERRCODE_WRITE_FAILURE;          // Error, could not write EOC marker.
  }
  //
  // Free the remaining cluster chain.
  //
  r = FS_FAT_FreeClusterChain(pVolume, pSB, FirstClusterToDelete, NumClustersToDelete);
  if (r != 0) {
    return r;                                 // Error, could not free cluster chain.
  }
  *pLastCluster = LastCluster;
  return FS_ERRCODE_OK;                       // OK, cluster shortened.
}

/*********************************************************************
*
*       _TruncateFile
*
*  Function description
*    Reduces the size of a file.
*
*  Parameters
*    pFile      Handle to the file to operate on.
*    pSB        Buffer to be used for the read and write operations.
*
*  Return value
*    ==0      OK, file size reduced.
*    !=0      Error code indicating the failure reason.
*/
static int _TruncateFile(const FS_FILE * pFile, FS_SB * pSB) {
  FS_FAT_INFO     * pFATInfo;
  FS_FILE_OBJ     * pFileObj;
  U32               NumClustersAct;
  U32               FirstCluster;
  U32               LastCluster;
  U32               NumClustersNew;
  U32               NumBytesAct;
  U32               NumBytesNew;
  U32               BytesPerCluster;
  int               r;
  FS_VOLUME       * pVolume;
  FS_INT_DATA_FAT * pFATData;

  r        = 0;
  pFileObj = pFile->pFileObj;
  pFATData = &pFileObj->Data.Fat;
  pVolume  = pFileObj->pVolume;
  pFATInfo = &pVolume->FSInfo.FATInfo;
  NumBytesNew     = (U32)pFile->FilePos;
  NumBytesAct     = (U32)pFileObj->Size;
  BytesPerCluster = pFATInfo->BytesPerCluster;
  FirstCluster    = pFileObj->FirstCluster;
  LastCluster     = 0;
  //
  // Calculate the number of clusters allocated to file.
  //
  NumClustersAct = (NumBytesAct + BytesPerCluster - 1u) / BytesPerCluster;
  //
  // Calculate the number of cluster for the new file size.
  //
  NumClustersNew = (NumBytesNew + BytesPerCluster - 1u) / BytesPerCluster;
  if (NumClustersAct == NumClustersNew) {
    goto Done;          // OK, the number of clusters have not been changed.
  }
  //
  // Check whether the file has been truncated to 0.
  //
  if (NumClustersNew == 0u) {
    pFileObj->FirstCluster    = 0;    // An empty file has no cluster associated with it.
    pFATData->CurClusterId    = 0;
    pFATData->CurClusterIndex = 0;
#if FS_FAT_OPTIMIZE_LINEAR_ACCESS
    pFATData->NumAdjClusters  = 0;
#endif // FS_FAT_OPTIMIZE_LINEAR_ACCESS
    r = FS_FAT_FreeClusterChain(pVolume, pSB, FirstCluster, NumClustersAct);
    goto Done;
  }
  LastCluster = 0;
  r = _ShortenClusterChain(pVolume, pSB, FirstCluster, NumClustersAct, NumClustersNew, &LastCluster);
  if (r != 0) {
    goto Done;          // Error, could not truncate cluster chain.
  }
  //
  // Update the cluster information in the file object.
  //
  pFATData->CurClusterId    = LastCluster;
  pFATData->CurClusterIndex = NumClustersNew - 1u;
#if FS_FAT_OPTIMIZE_LINEAR_ACCESS
  pFATData->NumAdjClusters  = 0;
#endif // FS_FAT_OPTIMIZE_LINEAR_ACCESS
Done:
#if FS_SUPPORT_ENCRYPTION
  if (r == 0) {
    FS_CRYPT_OBJ * pCryptObj;

    //
    // If the size of the new file is not a multiple of encryption block size
    // we need to re-encrypt the last sector since a different encryption
    // algorithm is used for the remaining bytes.
    //
    pCryptObj = pFileObj->pCryptObj;
    if (pCryptObj != NULL) {
      unsigned BytesPerBlock;
      U32      NumBytesRem;
      FS_SB    sbCrypt;
      FS_SB    sbData;
      unsigned ldBytesPerSector;
      U32      LastSector;
      unsigned SectorsPerCluster;

      BytesPerBlock = (unsigned)pCryptObj->pAlgoType->BitsPerBlock >> 3;
      NumBytesRem   = NumBytesNew & ((U32)BytesPerBlock - 1uL);
      if (NumBytesRem != 0u) {
        if (LastCluster == 0u) {
          LastCluster = FS_FAT_WalkCluster(pVolume, pSB, FirstCluster, NumClustersNew - 1u);
        }
        if (LastCluster == 0u) {
          r = FS_ERRCODE_INVALID_CLUSTER_CHAIN;   // Error, last cluster not found.
        } else {
          ldBytesPerSector   = pFATInfo->ldBytesPerSector;
          SectorsPerCluster  = pFATInfo->SectorsPerCluster;
          LastSector         = FS_FAT_ClusterId2SectorNo(pFATInfo, LastCluster);
          LastSector        += (NumBytesNew >> ldBytesPerSector) & ((U32)SectorsPerCluster - 1uL);
          (void)FS__SB_Create(&sbData, pVolume);
          (void)FS__SB_Create(&sbCrypt, pVolume);
          //
          // Simulate a write to last byte in the file.
          //
          r = FS_FAT_ReadDataSector(LastSector, NumBytesNew - 1u, pFileObj, &sbData, &sbCrypt);
          if (r == 0) {
            r = FS_FAT_WriteDataSectorEncrypted(LastSector, NumBytesNew - 1u, 1, NumBytesNew, 1, pFileObj, &sbData, &sbCrypt);
            if (r != 0) {
              r = FS_ERRCODE_WRITE_FAILURE;       // Error, could not write sector data.
            }
          } else {
            r = FS_ERRCODE_READ_FAILURE;          // Error, could not read sector data.
          }
          FS__SB_Delete(&sbData);
          FS__SB_Delete(&sbCrypt);
        }
      }
    }
  }
#endif
  return r;
}

/*********************************************************************
*
*       _SetEndOfFile
*
*  Function description
*    Modifies the size of a file.
*
*  Parameters
*    pFile      Handle to the file to operate on.
*    pSB        Buffer to be used for the read and write operations.
*
*  Return value
*    ==0      OK, file size modified.
*    !=0      Error code indicating the failure reason.
*
*  Additional information
*    The new file size is specified via the file position.
*/
static int _SetEndOfFile(FS_FILE * pFile, FS_SB * pSB) {
  int           r;
  FS_FILE_OBJ * pFileObj;
  U32           FilePos;
  U32           FileSize;
  FS_VOLUME   * pVolume;
  U32           FirstCluster;
  U32           NumClusters;
  U32           FileSizeAct;
  unsigned      ldBytesPerCluster;

  r        = FS_ERRCODE_OK;
  pFileObj = pFile->pFileObj;
  pVolume  = pFileObj->pVolume;
  FilePos  = (U32)pFile->FilePos;
  FileSize = (U32)pFileObj->Size;
  //
  // Mark the volume as dirty.
  //
  if (FilePos != FileSize) {
    FS_FAT_UpdateDirtyFlagIfRequired(pVolume, 1);
  }
  //
  // Execute the operation.
  //
  if (FilePos < FileSize) {
    r = _TruncateFile(pFile, pSB);
  } else {
    if (FilePos > FileSize) {
      //
      // We temporarily subtract 1 byte from the file position
      // to avoid allocating one cluster more than required.
      //
      pFile->FilePos--;
      r = FS_FAT_GotoClusterAllocIfReq(pFile, pSB);
      pFile->FilePos++;
      if (r != 0) {
        //
        // Calculate the actual file size using the number of clusters successfully allocated.
        //
        NumClusters       = 0;
        FirstCluster      = pFileObj->FirstCluster;
        ldBytesPerCluster = pVolume->FSInfo.FATInfo.ldBytesPerCluster;
        (void)FS_FAT_FindLastCluster(pVolume, pSB, FirstCluster, &NumClusters);     // We discard the return value because we are not interested in the id of the last cluster in the cluster chain.
        FileSizeAct = NumClusters << ldBytesPerCluster;
        if (FileSizeAct > FileSize) {                                               // Preserve the original file size if FS_FAT_FindLastCluster() fails.
          pFileObj->Size = FileSizeAct;
        }
      }
    }
  }
  if (r == 0) {
    pFileObj->Size = FilePos;
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
*       FS_FAT_SetEndOfFile
*
*  Function description
*    Modifies the size of a file.
*
*  Parameters
*    pFile    Handle to an opened file.
*
*  Return value
*    ==0    OK, file size modified.
*    !=0    Error code indicating the failure reason.
*
*  Additional information
*    The file position is set to the end of file.
*/
int FS_FAT_SetEndOfFile(FS_FILE * pFile) {
  FS_SB         sb;
  FS_VOLUME   * pVolume;
  FS_FILE_OBJ * pFileObj;
  int           r;
  int           Result;

  pFileObj = pFile->pFileObj;
  pVolume  = pFileObj->pVolume;
  (void)FS__SB_Create(&sb, pVolume);
  r = _SetEndOfFile(pFile, &sb);
  Result = FS_FAT_UpdateDirEntry(pFileObj, &sb, 1);       // 1 means that the data has to be written to journal.
  if (Result != 0) {
    r = Result;
  }
  FS__SB_Delete(&sb);
  if (r == 0) {
    r = FS__SB_GetError(&sb);
  }
  return r;
}

/*********************************************************************
*
*       FS_FAT_SetFileSize
*
*  Function description
*    Modifies the size of a file.
*
*  Parameters
*    pFile      Handle to an opened file.
*    NumBytes   New file size in bytes.
*
*  Return value
*    ==0    OK, file size modified.
*    !=0    Error code indicating the failure reason.
*
*  Additional information
*    The file position is not modified.
*/
int FS_FAT_SetFileSize(FS_FILE * pFile, U32 NumBytes) {
  int               r;
  int               Result;
  FS_SB             sb;
  FS_VOLUME       * pVolume;
  FS_FILE_OBJ     * pFileObj;
  U32               FilePos;
  FS_INT_DATA_FAT   FSIntData;
  int               PreserveFilePos;

  pFileObj = pFile->pFileObj;
  pVolume  = pFileObj->pVolume;
  FilePos  = (U32)pFile->FilePos;
  FS_MEMSET(&FSIntData, 0, sizeof(FSIntData));
  (void)FS__SB_Create(&sb, pVolume);
  PreserveFilePos = 0;
  //
  // Preserve the file position only if the new
  // file size is larger than the actual file position.
  //
  if (NumBytes > FilePos) {
    PreserveFilePos = 1;
  }
  //
  // Save the file position including the cluster information.
  //
  if (PreserveFilePos != 0) {
    FilePos   = (U32)pFile->FilePos;
    FSIntData = pFileObj->Data.Fat;   // struct copy
  }
  //
  // Truncate the file.
  //
  pFile->FilePos = NumBytes;
  r = _SetEndOfFile(pFile, &sb);
  //
  // Restore the file position.
  //
  if (PreserveFilePos != 0) {
    pFile->FilePos     = FilePos;
    pFileObj->Data.Fat = FSIntData;   // struct copy
  }
  Result = FS_FAT_UpdateDirEntry(pFileObj, &sb, 1);
  if (Result != 0) {
    r = Result;
  }
  FS__SB_Delete(&sb);
  if (r == 0) {
    r = FS__SB_GetError(&sb);
  }
  return r;
}

/*************************** End of file ****************************/
