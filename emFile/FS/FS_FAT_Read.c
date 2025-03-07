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
File        : FS_FAT_Read.c
Purpose     : FAT read routines
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
*       _ReadBurst
*
*/
static int _ReadBurst(const BURST_INFO_R * pBurstInfo) {
  FS_PARTITION * pPart;
  FS_VOLUME    * pVolume;

  if (pBurstInfo->NumSectors != 0u) {
    pVolume = FS__SB_GetVolume(pBurstInfo->pSBData);
    pPart   = &pVolume->Partition;
    if (FS_LB_ReadBurstPart(pPart,
                            pBurstInfo->FirstSector,
                            pBurstInfo->NumSectors,
                            pBurstInfo->pData, FS_SECTOR_TYPE_DATA) != 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_FS, "FAT: _ReadBurst: Burst read error."));
      return 1;     // read error
    }
  }
  return 0;         // No problem !
}

/*********************************************************************
*
*       _ReadData
*
*  Function description
*    Read data from a file.
*    Does most of the actual work and contains optimizations such as file buffer & burst support.
*
*  Return value
*    Number of bytes read
*/
static U32 _ReadData(U8 * pData, U32 NumBytesReq, FS_FILE * pFile, FS_SB * pSBData, FS_SB * pSBfat, FS_SB * pSBCrypt) {
  U32               NumBytesRead;
  U32               BytesPerCluster;
  FS_VOLUME       * pVolume;
  U32               NumBytesCluster;
  U32               FirstByteAfterCluster;
  U32               SectorOff;
  U32               SectorIndex;
  FS_FILE_OBJ     * pFileObj;
  FS_FAT_INFO     * pFATInfo;
  int               ZeroCopyAllowed;
  int               r;
  U32               FilePos;
  BURST_INFO_R      BurstInfo;
  FS_INT_DATA_FAT * pFATData;
  U8              * pBuffer;
  unsigned          NumBytesSector;

  //
  // Initialize some values used throughout the routine.
  //
  pFileObj              = pFile->pFileObj;
  pFATData              = &pFileObj->Data.Fat;
  pVolume               = pFileObj->pVolume;
  pFATInfo              = &pVolume->FSInfo.FATInfo;
  BytesPerCluster       = pFATInfo->BytesPerCluster;
  NumBytesRead          = 0;
  BurstInfo.NumSectors  = 0;
  BurstInfo.FirstSector = SECTOR_INDEX_INVALID;
  BurstInfo.pSBData     = pSBData;
  BurstInfo.pData       = NULL;
  //
  // Check if "Zero copy" is possible.
  // Per default, it is, but some systems may not allow the driver in some situations to read data directly into the application buffer.
  // Possible reasons can be misaligned destination (DMA requires 4-byte alignment, but application buffer is not) or caching issues.
  // On most systems, this does not need to be considered since it is not an issue; ideally this is taken care of by the driver anyhow;
  // meaning that if copying is required, it is already done by the driver itself
  //
  ZeroCopyAllowed = 1;
#if FS_SUPPORT_CHECK_MEMORY
  {
    FS_MEM_CHECK_CALLBACK * pfMemCheck;

    pfMemCheck = pFileObj->pVolume->Partition.Device.Data.pfMemCheck;
    if (pfMemCheck != NULL) {
      if (pfMemCheck(pData, NumBytesReq) == 0) {
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
    if (FS_FAT_GotoCluster(pFile, pSBfat) != 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_FS, "FAT: _ReadData: Too few cluster allocated to file."));
      return NumBytesRead;           // File truncated (too few clusters).
    }
    FirstByteAfterCluster = BytesPerCluster * (pFATData->CurClusterIndex + 1u);
    NumBytesCluster       = FirstByteAfterCluster - (U32)pFile->FilePos;
    if (NumBytesCluster > NumBytesReq) {
      NumBytesCluster = NumBytesReq;
    }
    SectorOff = pFile->FilePos & ((unsigned)pFATInfo->BytesPerSector - 1u);
    SectorIndex  = FS_FAT_ClusterId2SectorNo(pFATInfo, pFATData->CurClusterId);
    SectorIndex += (pFile->FilePos >> pFATInfo->ldBytesPerSector) & ((unsigned)pFATInfo->SectorsPerCluster - 1u);
    //
    // Read data from the cluster, iterating over sectors
    //
    do {
      NumBytesSector = pFATInfo->BytesPerSector - SectorOff;
      if ((U32)NumBytesSector > NumBytesCluster) {
        NumBytesSector = (unsigned)NumBytesCluster;
      }
      //
      // Do we have to read one sector into intermediate buffer ?
      //
      if   ((ZeroCopyAllowed == 0)
#if (FS_DRIVER_ALIGNMENT > 1)                               // Not required, just to avoid warnings
        || ((SEGGER_PTR2ADDR(pData) & ((unsigned)FS_DRIVER_ALIGNMENT - 1u)) != 0u)                        // MISRA deviation D:103[b]
#endif
#if FS_SUPPORT_ENCRYPTION
        || (pFileObj->pCryptObj != NULL)                    // Encryption active?
#endif
        || (NumBytesSector != pFATInfo->BytesPerSector))    // Do we read the sector only partially ?
      {
        //
        // Safe, but slow: Read one sector using memory of a smart buffer and copy data to destination
        //
        FilePos = (U32)pFile->FilePos;
        r = FS_FAT_ReadDataSector(SectorIndex, FilePos, pFileObj, pSBData, pSBCrypt);
        if (r != 0) {
          return NumBytesRead;
        }
        pBuffer  = FS__SB_GetBuffer(pSBData);
        pBuffer += SectorOff;
        FS_MEMCPY(pData, pBuffer, NumBytesSector);
      } else {
        //
        // Zero copy variant. Check if we need to read the previous burst data
        //
        if (SectorIndex != (BurstInfo.FirstSector + BurstInfo.NumSectors)) {
          if (_ReadBurst(&BurstInfo) != 0) {
            return 0;               // We do not know how many bytes have been read o.k., so reporting 0 is on the safe side
          }
          BurstInfo.FirstSector = SectorIndex;
          BurstInfo.NumSectors  = 1;
          BurstInfo.pData       = pData;
        } else {
          BurstInfo.NumSectors++;
        }
      }
      //
      // Update management info
      //
      pData           += NumBytesSector;
      NumBytesCluster -= NumBytesSector;
      NumBytesReq     -= NumBytesSector;
      NumBytesRead    += NumBytesSector;
      pFile->FilePos  += NumBytesSector;
      SectorIndex++;
      SectorOff = 0;                // Next sector will be written from start
    } while (NumBytesCluster != 0u);
  } while (NumBytesReq != 0u);
  if (_ReadBurst(&BurstInfo) != 0) {
    NumBytesRead = 0;               // We do not know how many bytes have been read o.k., so reporting 0 is on the safe side
  }
  return NumBytesRead;
}

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_FAT_ReadDataSector
*
*  Function description
*    Reads the contents of a data sector from the storage medium.
*    If the encryption is active for the file decrypted data is returned.
*
*  Parameters
*    SectorIndex  Index of the sector to read relative to beginning of the volume.
*    FilePos      Read position in the file.
*    pFileObj     [IN]  File object assigned to opened file.
*    pSBData      [OUT] Contents of the data sector in decrypted form.
*    pSBCrypt     Temporary buffer used for data decryption.
*
*  Return value
*    ==0      Sector read.
*    !=0      An error occurred.
*/
int FS_FAT_ReadDataSector(U32 SectorIndex, U32 FilePos, const FS_FILE_OBJ * pFileObj, FS_SB * pSBData, FS_SB * pSBCrypt) {
  int r;

  FS_USE_PARA(pSBCrypt);
  FS_USE_PARA(FilePos);
  FS_USE_PARA(pFileObj);
#if FS_SUPPORT_ENCRYPTION
  {
    FS_CRYPT_OBJ * pCryptObj;

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
      U32           FileSize;
      FS_FAT_INFO * pFATInfo;

      FS__SB_SetSector(pSBCrypt, SectorIndex, FS_SECTOR_TYPE_DATA, 0);
      r = FS__SB_Read(pSBCrypt);
      if (r == 0) {
        pContext        = pCryptObj->pContext;
        ldBytesPerBlock = pCryptObj->ldBytesPerBlock;
        pFATInfo        = &pFileObj->pVolume->FSInfo.FATInfo;
        BytesPerSector  = pFATInfo->BytesPerSector;
        FileSize        = pFileObj->SizeEncrypted;
        BlockIndex      = (FilePos & ~(BytesPerSector - 1u)) >> ldBytesPerBlock;
        NumBytesRem     = BytesPerSector;
        NumBytes        = FileSize & ~(BytesPerSector - 1u);      // Round down to multiple of sector size.
        if (FilePos >= NumBytes) {                                // Reading from the last sector?
          //
          // Compute the number of bytes to be read from the last sector.
          //
          NumBytes = FileSize & (BytesPerSector - 1u);
          if (NumBytes != 0u) {
            NumBytesRem = NumBytes;
          }
        }
        NumBlocks = NumBytesRem >> ldBytesPerBlock;
        pDest     = FS__SB_GetBuffer(pSBData);
        pSrc      = FS__SB_GetBuffer(pSBCrypt);
        //
        // Decrypt complete blocks if possible.
        //
        if (NumBlocks != 0u) {
          NumBytesAtOnce = 1uL << ldBytesPerBlock;
          do {
            pCryptObj->pAlgoType->pfDecrypt(pContext, pDest, pSrc, NumBytesAtOnce, BlockIndex);
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
            pCryptObj->pAlgoType->pfDecrypt(pContext, pDest, pSrc, NumBytesAtOnce, BlockIndex);
            pDest       += NumBytesAtOnce;
            pSrc        += NumBytesAtOnce;
            NumBytesRem -= NumBytesAtOnce;
            ++BlockIndex;               // Required for compatibility with older versions.
          }
        }
        //
        // The last bytes written to a sector which are not multiple of encryption block size
        // are decrypted separately using a simple encryption algorithm.
        //
        if (NumBytesRem != 0u) {
          U8  * pFirstKey;
          U32   Off;
          U8  * pBuffer;

          pFirstKey = NULL;
          pBuffer   = FS__SB_GetBuffer(pSBData);
          Off       = (U32)SEGGER_PTR_DISTANCE(pDest, pBuffer);   // D:106
          if ((Off & ((1uL << ldBytesPerBlock) - 1u)) != 0u) {    // Not the beginning of an encryption block?
            pFirstKey = pDest - 1;
          }
          FS__CRYPT_DecryptBytes(pDest, pSrc, NumBytesRem, (U8)BlockIndex, pFirstKey);
        }
      }
    } else {
      FS__SB_SetSector(pSBData, SectorIndex, FS_SECTOR_TYPE_DATA, 0);
      r = FS__SB_Read(pSBData);
    }
  }
#else
  FS__SB_SetSector(pSBData, SectorIndex, FS_SECTOR_TYPE_DATA, 0);
  r = FS__SB_Read(pSBData);
#endif
  return r;
}

/*********************************************************************
*
*       FS_FAT_Read
*
*  Function description
*    Reads data from a file.
*
*  Parameters
*    pFile        Handle to opened file.
*    pData        [OUT] Data read from file.
*    NumBytesReq  Number of bytes to be read.
*
*  Return value
*    Number of bytes read.
*/
U32 FS_FAT_Read(FS_FILE * pFile, void * pData, U32 NumBytesReq) {
  U32           NumBytesRead;
  FS_SB         sbData;          // Sector buffer for data
  FS_SB         sbfat;           // Sector buffer for FAT handling
#if FS_SUPPORT_ENCRYPTION
  FS_SB         sbCrypt;         // Sector buffer for decryption
#endif
  FS_SB       * pSBCrypt;
  FS_FILE_OBJ * pFileObj;
  FS_VOLUME   * pVolume;

  pFileObj = pFile->pFileObj;
  pVolume  = pFileObj->pVolume;
  //
  // Perform the operation if either there is no error
  // or the error code indicates the end of file.
  //
  if ((pFile->Error != FS_ERRCODE_EOF) && (pFile->Error != FS_ERRCODE_OK)) {
    return 0;                           // Error, cannot perform operation.
  }
  //
  // Set the error flag if the application reads more data than available.
  //
  if (pFile->FilePos >= pFileObj->Size) {
    pFile->Error = FS_ERRCODE_EOF;
    return 0;                           // Error, attempting to read past the end of file.
  }
  //
  // Make sure we do not try to read past the end of the file
  //
  {
    U32 NumBytesAvail;

    NumBytesAvail = (U32)pFileObj->Size - (U32)pFile->FilePos;
    if (NumBytesReq > NumBytesAvail) {
      NumBytesReq = NumBytesAvail;
      pFile->Error = FS_ERRCODE_EOF;    // An attempt was made to read past the end of file.
    }
  }
  if (NumBytesReq == 0u) {
    pFile->Error = FS_ERRCODE_EOF;
    return 0;
  }

  if (pFileObj->FirstCluster == 0u) {
    FS_DEBUG_ERROROUT((FS_MTYPE_FS, "FAT: FS_FAT_Read: No cluster in directory entry. Read failed."));
    return 0;
  }
  //
  // Allocate sector buffers.
  //
  (void)FS__SB_Create(&sbfat,  pVolume);
  (void)FS__SB_Create(&sbData, pVolume);
#if FS_SUPPORT_ENCRYPTION
  (void)FS__SB_Create(&sbCrypt, pVolume);
  pSBCrypt = &sbCrypt;
#else
  pSBCrypt = NULL;
#endif
  //
  // Do the work in a static subroutine
  //
  NumBytesRead = _ReadData(SEGGER_PTR2PTR(U8, pData), NumBytesReq, pFile, &sbData, &sbfat, pSBCrypt);     // MISRA deviation D:103[b]
  //
  // If less bytes have been read than intended
  //   - Set error code in file structure (unless already set)
  //   - Invalidate the Current cluster Id to make sure we read allocation list from start next time we read
  //
  if (NumBytesRead != NumBytesReq) {
    if (pFile->Error == 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_FS, "FAT: FS_FAT_Read: General read error."));
      pFile->Error = FS_ERRCODE_READ_FAILURE;
    }
  }
  //
  // Cleanup
  //
  FS__SB_Delete(&sbfat);
  FS__SB_Delete(&sbData);
#if FS_SUPPORT_ENCRYPTION
  FS__SB_Delete(&sbCrypt);
#endif
  return NumBytesRead;
}

/*************************** End of file ****************************/
