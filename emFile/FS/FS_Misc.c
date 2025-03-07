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
File        : FS_Misc.c
Purpose     : Misc. API functions
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*        #include Section
*
**********************************************************************
*/
#include "FS_Int.h"

/*********************************************************************
*
*       Static const data
*
**********************************************************************
*/
static const FS_ACCESS_MODE _aAccessMode[] = {
  //                                         DEL  OPEN  CREATE   READ  WRITE  APPEND  CREATE  BINARY
  { "r",    (U8)FS_FILE_ACCESS_FLAG_R,         0,    1,      0}, // 1     0        0       0       1
  { "rb",   (U8)FS_FILE_ACCESS_FLAGS_BR,       0,    1,      0}, // 1     0        0       0       1
  { "w",    (U8)FS_FILE_ACCESS_FLAGS_CW,       1,    0,      1}, // 0     1        0       1       0
  { "wb",   (U8)FS_FILE_ACCESS_FLAGS_BCW,      1,    0,      1}, // 0     1        0       1       1
  { "a",    (U8)FS_FILE_ACCESS_FLAGS_ACW,      0,    1,      1}, // 0     1        1       1       0
  { "ab",   (U8)FS_FILE_ACCESS_FLAGS_ABCW,     0,    1,      1}, // 0     1        1       1       1
  { "r+",   (U8)FS_FILE_ACCESS_FLAGS_RW,       0,    1,      0}, // 1     1        0       0       0
  { "r+b",  (U8)FS_FILE_ACCESS_FLAGS_BRW,      0,    1,      0}, // 1     1        0       0       1
  { "rb+",  (U8)FS_FILE_ACCESS_FLAGS_BRW,      0,    1,      0}, // 1     1        0       0       1
  { "w+",   (U8)FS_FILE_ACCESS_FLAGS_CRW,      1,    0,      1}, // 1     1        0       1       0
  { "w+b",  (U8)FS_FILE_ACCESS_FLAGS_BCRW,     1,    0,      1}, // 1     1        0       1       1
  { "wb+",  (U8)FS_FILE_ACCESS_FLAGS_BCRW,     1,    0,      1}, // 1     1        0       1       1
  { "a+",   (U8)FS_FILE_ACCESS_FLAGS_ACRW,     0,    1,      1}, // 1     1        1       1       0
  { "a+b",  (U8)FS_FILE_ACCESS_FLAGS_ABCRW,    0,    1,      1}, // 1     1        1       1       1
  { "ab+",  (U8)FS_FILE_ACCESS_FLAGS_ABCRW,    0,    1,      1}  // 1     1        1       1       1
};

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static FS_TIME_DATE_CALLBACK * _pfTimeDate = FS_X_GetTimeDate;
#if FS_SUPPRESS_EOF_ERROR
  static U8                    _IsEOFErrorSuppressed = 1;
#endif  // FS_SUPPRESS_EOF_ERROR

/*********************************************************************
*
*       Public data
*
**********************************************************************
*/
const FS_CHARSET_TYPE * FS_pCharSetType = &FS_CHARSET_CP437;
#if FS_SUPPORT_POSIX
  U8 FS_IsPOSIXSupported = 1;
#endif // FS_SUPPORT_POSIX
#if FS_VERIFY_WRITE
  U8 FS_IsWriteVerificationEnabled = 1;
#endif // FS_SUPPORT_POSIX

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

//lint -esym(9070, _SB_Clean) Recursive function [MISRA 2012 Rule 17.2, required] N:100. Rationale: Verified manually as being false.
//lint -esym(9070, FS__SB_Delete) Recursive function [MISRA 2012 Rule 17.2, required] N:100. Rationale: Verified manually as being false.
//lint -efunc(818, FS_FILE_GetSize) Pointer parameter 'pFile' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory] N:104. Rationale: pFile cannot be declared as pointing to const because it breaks the API.
//lint -efunc(818, FS_FTell) Pointer parameter 'pFile' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory] N:104. Rationale: pFile cannot be declared as pointing to const because it breaks the API.
//lint -efunc(818, FS_FEof) Pointer parameter 'pFile' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory] N:104. Rationale: pFile cannot be declared as pointing to const because it breaks the API.
//lint -efunc(818, FS_FError) Pointer parameter 'pFile' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory] N:104. Rationale: pFile cannot be declared as pointing to const because it breaks the API.
//lint -efunc(818, FS_SetFileBufferFlags) Pointer parameter 'pFile' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory] N:104. Rationale: pFile cannot be declared as pointing to const because it breaks the API.
//lint -efunc(818, FS_SetEncryptionObject) Pointer parameter 'pFile' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory] N:104. Rationale: pFile cannot be declared as pointing to const because it breaks the API.

/*********************************************************************
*
*       _CalcSizeInKB
*
*  Function description
*    Given the numbers of clusters, sectors per cluster and bytes per sector,
*    calculate the equivalent number of kilo bytes.
*
*  Parameters
*    NumClusters          Number of clusters.
*    SectorsPerCluster    Number of sectors in a cluster.
*    BytesPerSector       Number of bytes in a sector.
*
*  Return value
*    The number of kilo bytes (KB).
*/
static U32 _CalcSizeInKB(U32 NumClusters, U32 SectorsPerCluster, U32 BytesPerSector) {
  U32      BytesPerCluster;
  unsigned NumShifts;

  BytesPerCluster = SectorsPerCluster * BytesPerSector;
  NumShifts = 10;
  do {
    if (BytesPerCluster == 1u) {
      break;
    }
    BytesPerCluster >>= 1;
  } while (--NumShifts != 0u);
  return BytesPerCluster * (NumClusters >> NumShifts);
}

/*********************************************************************
*
*       _SB_Clean
*
*  Function description
*    Writes the sector buffer to storage.
*
*  Parameters
*    pSB        Instance of sector buffer.
*
*  Additional information
*    The data is written only if it is marked as dirty. In addition,
*    this function duplicates the sector data if a sector offset is
*    configured.
*/
static void _SB_Clean(FS_SB * pSB) {
  int            r;
  FS_PARTITION * pPart;

  if (pSB->Error != 0) {
    return;                 // Previous error, do not continue.
  }
  if (pSB->IsDirty != 0u) {
    pPart = &pSB->pVolume->Partition;
    r = FS_LB_WritePart(pPart, pSB->SectorIndex, pSB->pBuffer, pSB->Type, pSB->WriteToJournal);
    if (r != 0) {
      pSB->Error = FS_ERRCODE_WRITE_FAILURE;
    }
#if FS_MAINTAIN_FAT_COPY
    //
    // Handle the optional sector copy (typically used for the second FAT).
    //
    if (pSB->OffSectorCopy != 0u) {
      U32 SectorIndex;

      SectorIndex = pSB->SectorIndex + pSB->OffSectorCopy;
      r = FS_LB_WritePart(pPart, SectorIndex, pSB->pBuffer, pSB->Type, pSB->WriteToJournal);
      if (r != 0) {
        pSB->Error = FS_ERRCODE_WRITE_FAILURE;
      }
    }
#endif
#if FS_SUPPORT_SECTOR_BUFFER_CACHE
    FS__InvalidateSectorBuffer(pSB->pVolume, pSB->SectorIndex, 1);
#endif // FS_SUPPORT_SECTOR_BUFFER_CACHE
    pSB->IsDirty = 0;
  }
}

#if FS_SUPPORT_SECTOR_BUFFER_BURST

/*********************************************************************
*
*       _SB_CalcOffSector
*
*  Function description
*    Calculates the offset of the specified sector index
*    relative to the index of the first sector stored in
*    the sector buffer.
*
*  Parameters
*    pSB              Instance of the sector buffer.
*    SectorIndex      Index of the logical sector.
*
*  Return value
*    >=0      Offset of the specified sector.
*    < 0      The specified sector is not stored in the buffer.
*/
static I32 _SB_CalcOffSector(const FS_SB * pSB, U32 SectorIndex) {
  I32      OffSector;
  U32      SectorIndexStart;
  unsigned NumSectors;

  OffSector        = -1;              // Sector not in buffer.
  SectorIndexStart = pSB->SectorIndex;
  NumSectors       = pSB->NumSectors;
  if (   (SectorIndex >= SectorIndexStart)
      && (SectorIndex <  (SectorIndexStart + NumSectors))) {
    OffSector = (I32)SectorIndex - (I32)SectorIndexStart;
  }
  return OffSector;
}

/*********************************************************************
*
*       _SB_CalcNumSectors
*
*  Function description
*    Calculates the maximum number of sectors that can be stored in the buffer.
*
*  Parameters
*    pSB              Instance of the sector buffer.
*    SectorIndex      Index of the logical sector.
*
*  Return value
*    !=0      Maximum number of sectors that can be stored in the buffer.
*    ==0      Invalid sector index.
*/
static unsigned _SB_CalcNumSectors(FS_SB * pSB, U32 SectorIndex) {
  U32      NumSectorsTotal;
  unsigned NumSectorsBuffer;
  unsigned NumSectors;
  U32      SectorIndexRange;
  U32      NumSectorsRange;
  U32      NumSectorsDiff;

  NumSectors       = 0;               // Set to indicate error.
  NumSectorsTotal  = pSB->pVolume->FSInfo.Info.NumSectors;
  NumSectorsBuffer = pSB->NumSectorsBuffer;
  if (SectorIndex < NumSectorsTotal) {
    //
    // Make sure that we do not try to read past the end of storage device.
    //
    NumSectors = NumSectorsTotal - SectorIndex;
    NumSectors = SEGGER_MIN(NumSectorsBuffer, NumSectors);
    if (NumSectors != 0u) {
      //
      // Make sure that we do not read outside the sector boundary set by the file system.
      //
      SectorIndexRange = pSB->SectorIndexRange;
      NumSectorsRange  = pSB->NumSectorsRange;
      if (NumSectorsRange != 0u) {
        if (   (SectorIndex < SectorIndexRange)
            || (SectorIndex >= (SectorIndexRange + NumSectorsRange))) {
          FS_DEBUG_ERROROUT((FS_MTYPE_API, "FS__SB_SetSector: Sector not in range (%lu not in [%lu, %lu]).", SectorIndex, SectorIndexRange, SectorIndexRange + NumSectorsRange - 1u));
          pSB->Error = FS_ERRCODE_INVALID_PARA;
#if FS_SUPPORT_TEST
          FS_X_PANIC(FS_ERRCODE_INVALID_PARA);
#endif // FS_SUPPORT_TEST
          NumSectors = 0;
        } else {
          NumSectorsDiff   = SectorIndex - SectorIndexRange;
          NumSectorsRange -= NumSectorsDiff;
          NumSectors       = SEGGER_MIN(NumSectorsRange, NumSectors);
        }
      }
    }
  }
  return NumSectors;
}

#endif // FS_SUPPORT_SECTOR_BUFFER_BURST

#if FS_MULTI_HANDLE_SAFE

/*********************************************************************
*
*       _FindFileObj
*
*  Function description
*    Searches for an in use file object by the file name.
*
*  Parameters
*    sFileName        Name of the file to search for (fully qualified).
*    pFileObjToSkip   File object to skip during the search. Can be NULL.
*
*  Return value
*    !=NULL     Found file object.
*    ==NULL     File object not found.
*
*  Notes
*    We do not actually need the fully qualified name.
*    We can identify a file by its name (partially qualified)
*    and by the pointer to the volume instance that is stored
*    in FS_FILE_OBJ::pVolume.
*/
static FS_FILE_OBJ * _FindFileObj(const char * sFileName, const FS_FILE_OBJ * pFileObjToSkip) {
  FS_FILE_OBJ * pFileObj;
  FS_FILE_OBJ * pFileObjToCheck;

  pFileObj        = NULL;
  pFileObjToCheck = FS_Global.pFirstFileObj;
  FS_LOCK_SYS();
  while (pFileObjToCheck != NULL) {
    if (pFileObjToCheck != pFileObjToSkip) {
      if (pFileObjToCheck->UseCnt != 0u) {
        if (FS_STRCMP(sFileName, pFileObjToCheck->acFullFileName) == 0) {
          pFileObj = pFileObjToCheck;
          break;
        }
      }
    }
    pFileObjToCheck = pFileObjToCheck->pNext;
  }
  FS_UNLOCK_SYS();
  return pFileObj;
}

#endif // FS_MULTI_HANDLE_SAFE

/*********************************************************************
*
*       _SetFilePosNC
*
*  Function description
*    Sets current position of a file pointer.
*
*  Parameters
*    pFile      Handle to the opened file (cannot be NULL).
*    Off        Position in the file (byte offset).
*    Origin     Mode for positioning the file pointer.
*
*  Additional information
*    Internal version of FS__SetFilePos() without parameter checking.
*/
static void _SetFilePosNC(FS_FILE * pFile, FS_FILE_OFF Off, int Origin) {
  FS_FILE_SIZE FilePos;

  FilePos = (FS_FILE_SIZE)Off;
  switch (Origin) {
  case FS_SEEK_SET:
    break;
  case FS_SEEK_CUR:
    FilePos += pFile->FilePos;
    break;
  case FS_SEEK_END:
    {
      FS_FILE_SIZE FileSize;

#if FS_SUPPORT_FILE_BUFFER
      FileSize = FS__FB_GetFileSize(pFile);
#else
      FileSize = pFile->pFileObj->Size;
#endif
      FilePos += FileSize;
    }
    break;
  default:
    //
    // Invalid origin specification. This error is handled by the calling function.
    //
    break;
  }
  if (pFile->FilePos != FilePos) {
    pFile->FilePos = FilePos;
  }
}

#if FS_OS_LOCK_PER_DRIVER

/*********************************************************************
*
*       _SetFilePosDL
*
*  Function description
*    Internal version of the FS__SetFilePos() with driver locking.
*/
static int _SetFilePosDL(FS_FILE * pFile, FS_FILE_OFF Off, int Origin) {
  FS_VOLUME   * pVolume;
  int           r;
  FS_FILE_OBJ * pFileObj;
  FS_DEVICE   * pDevice;
  int           InUse;

  pVolume  = NULL;
  FS_LOCK_SYS();
  InUse    = (int)pFile->InUse;
  pFileObj = pFile->pFileObj;
  if (pFileObj != NULL) {
    pVolume = pFileObj->pVolume;
  }
  pDevice = NULL;
  if (pVolume != NULL) {
    pDevice = &pVolume->Partition.Device;
  }
  FS_UNLOCK_SYS();
  if (InUse == 0) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;    // Error, the file handle has been closed.
  }
  if (pVolume == NULL) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;    // Error, the file object has been closed.
  }
  FS_LOCK_DRIVER(pDevice);
  FS_LOCK_SYS();
  if (pFileObj != pFile->pFileObj) {
    InUse = 0;                                // Error, the file object has been invalidated by a forced unmount operation.
  }
  if (pFile->InUse == 0u) {                   // Error, the file handle has been closed by another task.
    InUse = 0;
  }
  FS_UNLOCK_SYS();
  if (InUse == 0) {
    r = FS_ERRCODE_INVALID_FILE_HANDLE;       // Error, invalid file handle.
  } else {
    _SetFilePosNC(pFile, Off, Origin);
    r = FS_ERRCODE_OK;
  }
  FS_UNLOCK_DRIVER(pDevice);
  return r;
}

#else

/*********************************************************************
*
*       _SetFilePosNL
*
*  Function description
*    Internal version of the FS__SetFilePos() without locking.
*/
static int _SetFilePosNL(FS_FILE * pFile, FS_FILE_OFF Off, int Origin) {
  FS_FILE_OBJ * pFileObj;

  if (pFile->InUse == 0u) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;          // Error, the file handle has been closed.
  }
  pFileObj = pFile->pFileObj;
  if (pFileObj == NULL) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;          // Error, the file object has been invalidated by a forced unmount.
  }
  _SetFilePosNC(pFile, Off, Origin);
  return FS_ERRCODE_OK;                             // OK, file pointer repositioned.
}

#endif // FS_OS_LOCK_PER_DRIVER

/*********************************************************************
*
*       _WipeFile
*
*  Function description
*    Internal version of FS_WipeFile().
*    Overwrites the contents of a file with random values.
*
*  Return value
*    ==0    OK, file contents overwritten
*    !=0    Error code indicating the failure reason
*
*  Notes
*    (1) This function allocates FS_BUFFER_SIZE_FILE_WIPE bytes on the stack.
*/
static int _WipeFile(const char * sFileName) {
  int            r;
  int            rClose;
  FS_FILE_SIZE   NumBytesInFile;
  U32            aBuffer[FS_BUFFER_SIZE_FILE_WIPE / 4];
  FS_FILE      * pFile;

  //
  // Open source file.
  //
  pFile = NULL;
  r = FS__OpenFileEx(sFileName, FS_FILE_ACCESS_FLAG_W, 0, 0, 1, &pFile);
  if (r != 0) {
    return r;                                                                             // Error, could not open file.
  }
  if (pFile == NULL) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;
  }
  r = FS_ERRCODE_OK;                                                                      // Set to indicate success.
  //
  // Get the number of bytes to be written.
  //
  NumBytesInFile = FS__GetFileSize(pFile);
  if (NumBytesInFile != 0u) {
    //
    // Overwrite the file contents with random data sector by sector.
    //
    do {
      U32   NumBytesToWrite;
      U32   NumBytesWritten;
      U16   v;
      U16 * p;
      U32   NumLoops;

      NumBytesToWrite = (U32)SEGGER_MIN(NumBytesInFile, sizeof(aBuffer));
      //
      // Fill the buffer with random values.
      //
      p        = SEGGER_PTR2PTR(U16, aBuffer);                                            // MISRA deviation D:100[e]
      NumLoops = (NumBytesToWrite + 1u) >> 1;                                             // PRNG returns 16-bit values.
      do {
        v  = FS_PRNG_Generate();
        *p = v;
        ++p;
      } while (--NumLoops != 0u);
      //
      // Write the random data to file.
      //
      NumBytesWritten  = FS_FILE_WRITE(pFile, aBuffer, NumBytesToWrite);
      if (NumBytesWritten != NumBytesToWrite) {
        r = pFile->Error;                                                                 // Error, could not write data to file.
        break;
      }
      NumBytesInFile  -= NumBytesToWrite;
    } while (NumBytesInFile != 0u);
  }
  rClose = FS__CloseFile(pFile);
  if (r == 0) {
    r = rClose;
  }
  return r;
}

/*********************************************************************
*
*       _GetFSType
*
*  Function description
*    Returns the type of file system.
*/
static int _GetFSType(const FS_VOLUME * pVolume) {
  int FSType;

#if FS_SUPPORT_MULTIPLE_FS
  FSType = FS_MAP_GetFSType(pVolume);
#elif FS_SUPPORT_FAT
  FS_USE_PARA(pVolume);
  FSType = FS_FAT;
#else
  FS_USE_PARA(pVolume);
  FSType = FS_EFS;
#endif
  return FSType;
}

/*********************************************************************
*
*       _GetFileId
*
*  Function description
*    Internal version of FS_GetFileId().
*
*  Return value
*    ==0    OK, file id returned
*    !=0    Error code indicating the failure reason
*/
static int _GetFileId(const char * sFileName, U8 * pId) {
  int         r;
  U32         DirPos;
  U32         FilePos;
  unsigned    Attr;
  FS_VOLUME * pVolume;
  int         Result;

  if (pId == NULL) {
    return FS_ERRCODE_INVALID_PARA;
  }
  Attr = FS__GetFileAttributes(sFileName);
  if (Attr == 0xFFu) {
    return FS_ERRCODE_FILE_DIR_NOT_FOUND;     // Error, could not get the file attributes.
  }
  DirPos  = 0;
  FilePos = 0;
  if ((Attr & FS_ATTR_DIRECTORY) != 0u) {
    FS_DIR_OBJ   DirObj;
    const char * s;

    pVolume = FS__FindVolumeEx(sFileName, &s);
    if (pVolume != NULL) {
      FS_LOCK_DRIVER(&pVolume->Partition.Device);
      if (pVolume->MountType == 0u) {
        FS_DEBUG_ERROROUT((FS_MTYPE_API, "_GetFileId: Volume has been unmounted by other task during wait."));
        r = FS_ERRCODE_VOLUME_NOT_MOUNTED;
      } else {
        FS_MEMSET(&DirObj, 0, sizeof(DirObj));
        DirObj.pVolume = pVolume;
        r = FS_OPENDIR(s, &DirObj);
        if (r == 0) {
          DirPos  = DirObj.DirPos.FirstClusterId;
          FilePos = 0;
          (void)FS_CLOSEDIR(&DirObj);
        }
      }
      FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
    } else {
      r = FS_ERRCODE_VOLUME_NOT_FOUND;
    }
  } else {
    FS_FILE     * pFile;
    FS_FILE_OBJ * pFileObj;
    int           FSType;

    //
    // Open source file.
    //
    pFile = NULL;
    r = FS__OpenFileEx(sFileName, FS_FILE_ACCESS_FLAG_R, 0, 0, 1, &pFile);
    if (r == FS_ERRCODE_OK) {
      if (pFile == NULL) {
        r = FS_ERRCODE_INVALID_FILE_HANDLE;
      } else {
        pFileObj = pFile->pFileObj;
        FSType = _GetFSType(pFileObj->pVolume);
        if (FSType < 0) {
          r = FSType;
        } else {
          if (FSType == FS_FAT) {
            const FS_DIRENTRY_POS_FAT * pDirEntryPos;

            //
            // We have to add 1 to FilePos since the parent directory shares the same DirPos and has FilePos set to 0.
            //
            pDirEntryPos = &pFileObj->DirEntryPos.fat;
            DirPos  = pDirEntryPos->SectorIndex;
            FilePos = (U32)pDirEntryPos->DirEntryIndex + 1u;
          } else {
            const FS_DIRENTRY_POS_EFS * pDirEntryPos;

            pDirEntryPos = &pFileObj->DirEntryPos.efs;
            DirPos  = pDirEntryPos->FirstClusterId;
            FilePos = pDirEntryPos->DirEntryPos + 1u;
          }
        }
        Result = FS__CloseFile(pFile);
        if (Result != 0) {
          r = Result;
        }
      }
    }
  }
  if (r == 0) {
    FS_MEMSET(pId, 0, 16);
    FS_StoreU32LE(pId, DirPos);
    pId += 4;
    FS_StoreU32LE(pId, FilePos);
  }
  return r;
}

/*********************************************************************
*
*       _IsDirectoryDelimiter
*/
static int _IsDirectoryDelimiter(char c) {
  int r;

  r = 0;        // Not a directory delimiter.
  if (c == FS_DIRECTORY_DELIMITER) {
    r = 1;
  }
  return r;
}

#if FS_OS_LOCK_PER_DRIVER

/*********************************************************************
*
*       _IsEndOfFileDL
*
*  Function description
*    Internal version of FS__GetFileSize() with driver locking.
*
*  Additional information
*    A driver locking is required in order to prevent a forced
*    unmount operation from invalidating the file object assigned
*    to the file handle.
*/
static int _IsEndOfFileDL(const FS_FILE * pFile) {
  int           r;
  FS_VOLUME   * pVolume;
  FS_FILE_OBJ * pFileObj;
  FS_DEVICE   * pDevice;
  int           InUse;

  pVolume  = NULL;
  FS_LOCK_SYS();
  InUse    = (int)pFile->InUse;
  pFileObj = pFile->pFileObj;
  if (pFileObj != NULL) {
    pVolume = pFileObj->pVolume;
  }
  pDevice = NULL;
  if (pVolume != NULL) {
    pDevice = &pVolume->Partition.Device;
  }
  FS_UNLOCK_SYS();
  if (InUse == 0) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;    // Error, the file handle has been closed.
  }
  if (pVolume == NULL) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;    // Error, the file object has been closed.
  }
  FS_LOCK_DRIVER(pDevice);
  FS_LOCK_SYS();
  if (pFileObj != pFile->pFileObj) {
    InUse = 0;                                // Error, the file object has been invalidated by a forced unmount operation.
  }
  if (pFile->InUse == 0u) {                   // Error, the file handle has been closed by another task.
    InUse = 0;
  }
  FS_UNLOCK_SYS();
  if (InUse == 0) {
    r = FS_ERRCODE_INVALID_FILE_HANDLE;       // Error, invalid file handle.
  } else {
    r = 0;
    if (pFile->FilePos >= pFile->pFileObj->Size) {
      r = 1;
    }
  }
  FS_UNLOCK_DRIVER(pDevice);
  return r;
}

#else

/*********************************************************************
*
*       _IsEndOfFileNL
*
*  Function description
*    Internal version of FS__FEof() without locking.
*/
static int _IsEndOfFileNL(const FS_FILE * pFile) {
  int           r;
  FS_FILE_OBJ * pFileObj;

  if (pFile->InUse == 0u) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;      // Error, the file handle has been invalidated.
  }
  pFileObj = pFile->pFileObj;
  if (pFileObj == NULL) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;      // Error, the file object has been invalidated by a forced unmount operation.
  }
  r = 0;
  if (pFile->FilePos >= pFile->pFileObj->Size) {
    r = 1;
  }
  return r;
}

#endif // FS_OS_LOCK_PER_DRIVER

#if FS_OS_LOCK_PER_DRIVER

/*********************************************************************
*
*       _GetFilePosDL
*
*  Function description
*    Internal version of FS__FTell() with driver locking.
*
*  Additional information
*    A driver locking is required in order to prevent a forced
*    unmount operation from invalidating the file object assigned
*    to the file handle.
*/
static I32 _GetFilePosDL(const FS_FILE * pFile) {
  I32           r;
  FS_VOLUME   * pVolume;
  FS_FILE_OBJ * pFileObj;
  FS_DEVICE   * pDevice;
  int           InUse;

  pVolume  = NULL;
  FS_LOCK_SYS();
  InUse    = (int)pFile->InUse;
  pFileObj = pFile->pFileObj;
  if (pFileObj != NULL) {
    pVolume = pFileObj->pVolume;
  }
  pDevice = NULL;
  if (pVolume != NULL) {
    pDevice = &pVolume->Partition.Device;
  }
  FS_UNLOCK_SYS();
  if (InUse == 0) {
    return -1;                                // Error, the file handle has been closed.
  }
  if (pVolume == NULL) {
    return -1;                                // Error, the file object has been closed.
  }
  FS_LOCK_DRIVER(pDevice);
  FS_LOCK_SYS();
  if (pFileObj != pFile->pFileObj) {
    InUse = 0;                                // Error, the file object has been invalidated by a forced unmount operation.
  }
  if (pFile->InUse == 0u) {                   // Error, the file handle has been closed by another task.
    InUse = 0;
  }
  FS_UNLOCK_SYS();
  if (InUse == 0) {
    r = -1;                                   // Error, invalid file handle.
  } else {
    r = (I32)pFile->FilePos;
  }
  FS_UNLOCK_DRIVER(pDevice);
  return r;
}

#else

/*********************************************************************
*
*       _GetFilePosNL
*
*  Function description
*    Internal version of FS__FTell() without locking.
*/
static I32 _GetFilePosNL(const FS_FILE * pFile) {
  I32           r;
  FS_FILE_OBJ * pFileObj;

  if (pFile->InUse == 0u) {
    return -1;                                  // Error, the file handle has been invalidated.
  }
  pFileObj = pFile->pFileObj;
  if (pFileObj == NULL) {
    return -1;                                  // Error, the file object has been invalidated by a forced unmount operation.
  }
  r = (I32)pFile->FilePos;
  return r;
}

#endif // FS_OS_LOCK_PER_DRIVER

#if FS_SUPPORT_ENCRYPTION

#if FS_OS_LOCK_PER_DRIVER

/*********************************************************************
*
*       _SetEncryptionObjectDL
*
*  Function description
*    Internal version of FS__SetEncryptionObject() with driver locking.
*/
static int _SetEncryptionObjectDL(const FS_FILE * pFile, FS_CRYPT_OBJ * pCryptObj) {
  FS_VOLUME   * pVolume;
  int           r;
  FS_FILE_OBJ * pFileObj;
  FS_DEVICE   * pDevice;
  int           InUse;
  unsigned      ldBytesPerSector;

  pVolume  = NULL;
  FS_LOCK_SYS();
  InUse    = (int)pFile->InUse;
  pFileObj = pFile->pFileObj;
  if (pFileObj != NULL) {
    pVolume = pFileObj->pVolume;
  }
  pDevice = NULL;
  if (pVolume != NULL) {
    pDevice = &pVolume->Partition.Device;
  }
  FS_UNLOCK_SYS();
  if (InUse == 0) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;    // Error, the file handle has been closed.
  }
  if (pVolume == NULL) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;    // Error, the file object has been closed.
  }
  FS_LOCK_DRIVER(pDevice);
  FS_LOCK_SYS();
  if (pFileObj != pFile->pFileObj) {
    InUse = 0;                                // Error, the file object has been invalidated by a forced unmount operation.
  }
  if (pFile->InUse == 0u) {                   // Error, the file handle has been closed by another task.
    InUse = 0;
  }
  FS_UNLOCK_SYS();
  if (InUse == 0) {
    r = FS_ERRCODE_INVALID_FILE_HANDLE;       // Error, invalid file handle.
  } else {
    r = 0;
    if (pCryptObj != NULL) {
      ldBytesPerSector = pFile->pFileObj->pVolume->FSInfo.Info.ldBytesPerSector;
      if (ldBytesPerSector < pCryptObj->ldBytesPerBlock) {
        r = FS_ERRCODE_INVALID_PARA;
      }
    }
    if (r == 0) {
      pFile->pFileObj->pCryptObj = pCryptObj;
    }
  }
  FS_UNLOCK_DRIVER(pDevice);
  return r;
}

#else

/*********************************************************************
*
*       _SetEncryptionObjectNL
*
*  Function description
*    Internal version of FS__SetEncryptionObject() without any locking.
*/
static int _SetEncryptionObjectNL(const FS_FILE * pFile, FS_CRYPT_OBJ * pCryptObj) {
  FS_FILE_OBJ * pFileObj;
  unsigned      ldBytesPerSector;

  if (pFile->InUse == 0u) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;      // Error, the file handle has been invalidated.
  }
  pFileObj = pFile->pFileObj;
  if (pFileObj == NULL) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;      // Error, the file object has been invalidated by a forced unmount operation.
  }
  if (pCryptObj != NULL) {
    ldBytesPerSector = pFileObj->pVolume->FSInfo.Info.ldBytesPerSector;
    if (ldBytesPerSector < pCryptObj->ldBytesPerBlock) {
      return FS_ERRCODE_INVALID_PARA;
    }
  }
  pFileObj->pCryptObj = pCryptObj;
  return 0;                                     // OK, file object set.
}

#endif // FS_OS_LOCK_PER_DRIVER

#endif // FS_SUPPORT_ENCRYPTION

/*********************************************************************
*
*       _UpdateFile
*
*  Function description
*    Synchronizes a file
*/
static int _UpdateFile(FS_FILE * pFile) {
  int r;
  int Result;

  r = 0;        // Set to indicate success.
#if FS_SUPPORT_FILE_BUFFER
  Result = FS__FB_Clean(pFile);
  if (Result != 0) {
    r = Result;
  }
#endif
  Result = FS_UPDATE_FILE(pFile);
  if (Result != 0) {
    r = Result;
  }
  return r;
}

#if FS_SUPPORT_JOURNAL

/*********************************************************************
*
*       _OpenFileFS
*
*  Function description
*    Opens a file (fail-safe variant).
*/
static int _OpenFileFS(const char * sFileName, FS_FILE * pFile, int DoDel, int DoOpen, int DoCreate) {
  int         r;
  int         Result;
  FS_VOLUME * pVolume;

  pVolume = pFile->pFileObj->pVolume;         // We do not have to check pFileObj for NULL here because the file system is locked.
  r = FS__JOURNAL_Begin(pVolume);
  if (r == 0) {
    r = FS_OPEN_FILE(sFileName, pFile, DoDel, DoOpen, DoCreate);
    FS__JOURNAL_SetError(pVolume, r);
    Result = FS__JOURNAL_End(pVolume);
    if (Result != 0) {
      r = Result;
    }
  } else {
    (void)FS_OPEN_FILE(sFileName, pFile, DoDel, DoOpen, DoCreate);        // Perform the operation without journal.
  }
  return r;
}

/*********************************************************************
*
*       _CloseFileFS
*
*  Function description
*    Closes a file (fail-safe variant).
*/
static int _CloseFileFS(FS_VOLUME * pVolume, FS_FILE * pFile) {
  int r;
  int Result;

  r = FS__JOURNAL_Begin(pVolume);
  if (r == 0) {
    r = FS__CloseFileNL(pFile);
    FS__JOURNAL_SetError(pVolume, r);
    Result = FS__JOURNAL_End(pVolume);
    if (Result != 0) {
      r = Result;
    }
  } else {
    (void)FS__CloseFileNL(pFile);         // Perform the operation without journal.
  }
  return r;
}

/*********************************************************************
*
*       _UpdateFileFS
*
*  Function description
*    Synchronizes a file (fail-safe variant).
*/
static int _UpdateFileFS(FS_VOLUME * pVolume, FS_FILE * pFile) {
  int r;
  int Result;

  r = FS__JOURNAL_Begin(pVolume);
  if (r == 0) {
    r = _UpdateFile(pFile);
    FS__JOURNAL_SetError(pVolume, r);
    Result = FS__JOURNAL_End(pVolume);
    if (Result != 0) {
      r = Result;
    }
  } else {
    (void)_UpdateFile(pFile);             // Perform the operation without journal.
  }
  return r;
}

#endif // FS_SUPPORT_JOURNAL

/*********************************************************************
*
*       Public code (internal)
*
**********************************************************************
*/

/*********************************************************************
*
*       FS__SB_Flush
*/
void FS__SB_Flush(FS_SB * pSB) {
  pSB->IsDirty = 1;
  _SB_Clean(pSB);
}

/*********************************************************************
*
*       FS__SB_Create
*
*  Function description
*    Initializes the instance of a sector buffer.
*
*  Parameters
*    pSB      Instance of the sector buffer.
*    pVolume  Volume that stores the logical sectors to be accessed.
*
*  Return value
*    ==0      OK, instance initialized.
*    !=       Error code indicating the failure reason.
*/
int FS__SB_Create(FS_SB * pSB, FS_VOLUME * pVolume) {
  U8  * pBuffer;
  int   r;

  r = 0;
  FS_MEMSET(pSB, 0, sizeof(FS_SB));
#if FS_SUPPORT_SECTOR_BUFFER_CACHE
  pBuffer = NULL;
#else
  pBuffer = FS__AllocSectorBuffer();
  if (pBuffer == NULL) {
    FS_DEBUG_ERROROUT((FS_MTYPE_API, "FS__SB_Create: No sector buffer available."));
    r = FS_ERRCODE_BUFFER_NOT_AVAILABLE;
    pSB->Error = (I8)r;
    FS_X_PANIC(r);      // Error, no sector buffer available.
  }
#endif // FS_SUPPORT_SECTOR_BUFFER_CACHE
  pSB->pBuffer          = pBuffer;
  pSB->pVolume          = pVolume;
  pSB->SectorIndex      = SECTOR_INDEX_INVALID;
  pSB->Type             = FS_SECTOR_TYPE_DATA;
  pSB->WriteToJournal   = 1;
#if FS_SUPPORT_SECTOR_BUFFER_BURST
  pSB->NumSectorsBuffer = 1;
  pSB->NumSectors       = 1;
#endif // FS_SUPPORT_SECTOR_BUFFER_BURST
  return r;
}

#if FS_SUPPORT_SECTOR_BUFFER_BURST

/*********************************************************************
*
*       FS__SB_CreateEx
*
*  Function description
*    Initializes the instance of a sector buffer.
*
*  Parameters
*    pSB            Instance of the sector buffer.
*    pVolume        Volume that stores the logical sectors to be accessed.
*    pBuffer        [IN] Data buffer.
*    SizeOfBuffer   Data buffer size in bytes.
*
*  Return value
*    ==0      OK, instance initialized.
*    !=0      Error code indicating the failure reason.
*
*  Additional information
*    This function performs the same operation as FS__SB_Create()
*    with the difference that it uses a data buffer provided
*    by the caller.
*/
int FS__SB_CreateEx(FS_SB * pSB, FS_VOLUME * pVolume, void * pBuffer, int SizeOfBuffer) {
  int      r;
  U32      NumSectorsBuffer;
  unsigned ldBytesPerSector;

  r = 0;
  FS_MEMSET(pSB, 0, sizeof(FS_SB));
  ldBytesPerSector = pVolume->FSInfo.Info.ldBytesPerSector;
  NumSectorsBuffer = (U32)SizeOfBuffer >> ldBytesPerSector;
  if ((NumSectorsBuffer == 0u) || (pBuffer == NULL)) {
    r = FS__SB_Create(pSB, pVolume);
  } else {
    if (NumSectorsBuffer > MAX_NUM_SECTORS_BUFFER) {
      NumSectorsBuffer = MAX_NUM_SECTORS_BUFFER;
    }
    pSB->pBuffer          = SEGGER_PTR2PTR(U8, pBuffer);              // MISRA deviation D:100e
    pSB->pVolume          = pVolume;
    pSB->SectorIndex      = SECTOR_INDEX_INVALID;
    pSB->Type             = FS_SECTOR_TYPE_DATA;
    pSB->WriteToJournal   = 1;
    pSB->NumSectorsBuffer = (U16)NumSectorsBuffer;
    pSB->NumSectors       = (U16)NumSectorsBuffer;
    pSB->IsExtBuffer      = 1;
  }
  return r;
}

#endif // FS_SUPPORT_SECTOR_BUFFER_BURST

/*********************************************************************
*
*       FS__SB_Delete
*
*  Function description
*    Frees the instance of a sector buffer.
*
*  Parameters
*    pSB      Instance of the sector buffer.
*/
void FS__SB_Delete(FS_SB * pSB) {
  _SB_Clean(pSB);
#if FS_SUPPORT_SECTOR_BUFFER_BURST
  if (pSB->IsExtBuffer == 0u)
#endif // FS_SUPPORT_SECTOR_BUFFER_BURST
  {
#if FS_SUPPORT_SECTOR_BUFFER_CACHE
    FS__FreeSectorBufferEx(pSB->pBuffer, pSB->pVolume, pSB->SectorIndex, (int)pSB->IsValid);
#else
    FS__FreeSectorBuffer(pSB->pBuffer);
#endif // FS_SUPPORT_SECTOR_BUFFER_CACHE
  }
}

/*********************************************************************
*
*       FS__SB_Clean
*
*  Function description
*    Writes the contents of the sector buffer to storage.
*
*  Parameters
*    pSB      Instance of the sector buffer.
*
*  Additional information
*    The sector buffer is written only if the data is marked as being dirty.
*/
void FS__SB_Clean(FS_SB * pSB) {
  _SB_Clean(pSB);
}
/*********************************************************************
*
*       FS__SB_MarkDirty
*
*  Function description
*    Specifies that the contents of the sector buffer was modified.
*
*  Parameters
*    pSB      Instance of the sector buffer.
*/
void FS__SB_MarkDirty(FS_SB * pSB) {
  pSB->IsDirty = 1;
}

#if FS_MAINTAIN_FAT_COPY

/*********************************************************************
*
*       FS__SB_SetOffSectorCopy
*
*  Function description
*    Sets the offset of the logical sector to be duplicated on write.
*
*  Parameters
*    pSB      Instance of the sector buffer.
*    Off      Offset of the duplicated sector.
*
*  Additional information
*    Typically, this function is used for FAT-formatted volumes only.
*/
void FS__SB_SetOffSectorCopy(FS_SB * pSB, U32 Off) {
  pSB->OffSectorCopy = Off;
}

#endif  // FS_MAINTAIN_FAT_COPY

/*********************************************************************
*
*       FS__SB_SetSector
*
*  Function description
*    Sets the index of the logical sector assigned to sector buffer.
*
*  Parameters
*    pSB              Instance of the sector buffer.
*    SectorIndex      Index of the logical sector.
*    Type             Type of data that is stored in the logical sector.
*    WriteToJournal   Specifies if the sector data has to be written to journal on not.
*/
void FS__SB_SetSector(FS_SB * pSB, U32 SectorIndex, unsigned Type, int WriteToJournal) {
#if FS_SUPPORT_SECTOR_BUFFER_CACHE
  if (pSB->pBuffer == NULL) {
    U8  * pBuffer;
    int   IsMatching;

    IsMatching = 0;
    pBuffer = FS__AllocSectorBufferEx(pSB->pVolume, SectorIndex, &IsMatching);
    if (pBuffer == NULL) {
      FS_DEBUG_ERROROUT((FS_MTYPE_API, "FS__SB_SetSector: No sector buffer available."));
      pSB->Error = FS_ERRCODE_BUFFER_NOT_AVAILABLE;
      FS_X_PANIC(FS_ERRCODE_BUFFER_NOT_AVAILABLE);
    }
    pSB->pBuffer       = pBuffer;
    pSB->SectorIndex   = SectorIndex;
    pSB->Type          = (U8)Type;
    pSB->IsValid       = 0;
#if FS_MAINTAIN_FAT_COPY
    pSB->OffSectorCopy = 0;
#endif // FS_MAINTAIN_FAT_COPY
    if (IsMatching != 0) {
      pSB->IsValid = 1;
    }
  } else
#endif // FS_SUPPORT_SECTOR_BUFFER_CACHE
  {
#if FS_SUPPORT_SECTOR_BUFFER_BURST
    I32      OffSector;
    unsigned NumSectors;

    OffSector = _SB_CalcOffSector(pSB, SectorIndex);
    if (OffSector < 0) {
      NumSectors = _SB_CalcNumSectors(pSB, SectorIndex);
      if (NumSectors != 0u) {
        if (pSB->IsDirty != 0u) {
          _SB_Clean(pSB);
        }
        pSB->SectorIndex   = SectorIndex;
        pSB->Type          = (U8)Type;
        pSB->IsValid       = 0;
#if FS_MAINTAIN_FAT_COPY
        pSB->OffSectorCopy = 0;
#endif // FS_MAINTAIN_FAT_COPY
        pSB->OffSector     = 0;
        pSB->NumSectors    = (U16)NumSectors;
      }
    } else {
      pSB->OffSector = (U16)OffSector;
    }
#else
    if (SectorIndex != pSB->SectorIndex) {
      if (pSB->IsDirty != 0u) {
        _SB_Clean(pSB);
      }
      pSB->SectorIndex   = SectorIndex;
      pSB->Type          = (U8)Type;
      pSB->IsValid       = 0;
#if FS_MAINTAIN_FAT_COPY
      pSB->OffSectorCopy = 0;
#endif // FS_MAINTAIN_FAT_COPY
    }
#endif // FS_SUPPORT_SECTOR_BUFFER_BURST
  }
  pSB->WriteToJournal = (U8)WriteToJournal;
}

/*********************************************************************
*
*       FS__SB_MarkValid
*
*  Function description
*    Specifies that the sector buffer contains valid data.
*
*  Parameters
*    pSB              Instance of the sector buffer.
*    SectorIndex      Index of the logical sector.
*    Type             Type of data that is stored in the logical sector.
*    WriteToJournal   Specifies if the sector data has to be written to journal on not.
*
*  Additional information
*    This function is useful if the sector buffer is filled with data
*    and needs to be written later on.
*/
void FS__SB_MarkValid(FS_SB * pSB, U32 SectorIndex, unsigned Type, int WriteToJournal) {
  FS__SB_SetSector(pSB, SectorIndex, Type, WriteToJournal);
  pSB->IsValid = 1;
  pSB->IsDirty = 1;
}

/*********************************************************************
*
*       FS__SB_MarkNotDirty
*
*  Function description
*    Specifies that the sector buffer does not contain modified data.
*
*  Parameters
*    pSB              Instance of the sector buffer.
*
*  Additional information
*    This function is useful if the sector buffer is filled with data
*    and needs to be written later on.
*/
void FS__SB_MarkNotDirty(FS_SB * pSB) {
  pSB->IsDirty  = 0;
}

/*********************************************************************
*
*       FS__SB_MarkNotValid
*
*  Function description
*    Invalidates the data in a sector buffer.
*
*  Parameters
*    pSB              Instance of the sector buffer.
*/
void FS__SB_MarkNotValid(FS_SB * pSB) {
  pSB->IsValid = 0;
}

/*********************************************************************
*
*       FS__SB_Read
*
*  Function description
*    Reads sector data from storage to sector buffer.
*
*  Parameters
*    pSB              Instance of the sector buffer.
*
*  Return value
*    ==0      OK, sector data read successfully.
*    !=0      Error code indicating the failure reason.
*/
int FS__SB_Read(FS_SB * pSB) {
  int            r;
  FS_PARTITION * pPart;

  if (pSB->Error != 0) {
    return pSB->Error;      // Previous error, do not continue.
  }
  if (pSB->IsValid == 0u) {
    pPart = &pSB->pVolume->Partition;
#if FS_SUPPORT_SECTOR_BUFFER_BURST
    r = FS_LB_ReadBurstPart(pPart, pSB->SectorIndex, pSB->NumSectors, pSB->pBuffer, pSB->Type);
#else
    r = FS_LB_ReadPart(pPart, pSB->SectorIndex, pSB->pBuffer, pSB->Type);
#endif
    if (r != 0) {
      pSB->Error = FS_ERRCODE_READ_FAILURE;
      return pSB->Error;    // Error, the read operation failed.
    }
    pSB->IsValid = 1;
  }
  return 0;                 // OK, sector data read successfully.
}

/*********************************************************************
*
*       FS__SB_Write
*
*  Function description
*    Writes sector data from sector buffer to storage.
*
*  Parameters
*    pSB              Instance of the sector buffer.
*
*  Return value
*    ==0      OK, sector data written successfully.
*    !=0      Error code indicating the failure reason.
*
*  Additional information
*    This function does not check if the sector data was modified
*    before writing it to storage.
*/
int FS__SB_Write(FS_SB * pSB) {
  int            r;
  FS_PARTITION * pPart;

  FS_DEBUG_ASSERT(FS_MTYPE_FS, pSB->SectorIndex != SECTOR_INDEX_INVALID);
  if (pSB->Error != 0) {
    return pSB->Error;                      // Previous error, do not continue.
  }
  pPart = &pSB->pVolume->Partition;
  r = FS_LB_WritePart(pPart, pSB->SectorIndex, pSB->pBuffer, pSB->Type, pSB->WriteToJournal);
  if (r != 0) {
    pSB->Error = FS_ERRCODE_WRITE_FAILURE;  // Error, the write operation failed.
  } else {
    pSB->IsDirty = 0;
  }
#if FS_SUPPORT_SECTOR_BUFFER_CACHE
  FS__InvalidateSectorBuffer(pSB->pVolume, pSB->SectorIndex, 1);
#endif // FS_SUPPORT_SECTOR_BUFFER_CACHE
  return pSB->Error;        // OK, sector data written successfully.
}

/*********************************************************************
*
*       FS__SB_GetError
*
*  Function description
*    Returns the error code indicating the success or failure
*    of the last operation.
*
*  Parameters
*    pSB              Instance of the sector buffer.
*
*  Return value
*    ==0      OK, the last operation was successful.
*    !=0      Error code indicating the failure reason.
*/
int FS__SB_GetError(const FS_SB * pSB) {
  return (int)pSB->Error;
}

/*********************************************************************
*
*       FS__SB_GetBuffer
*
*  Function description
*    Returns the data buffer assigned to the sector buffer.
*
*  Parameters
*    pSB              Instance of the sector buffer.
*
*  Return value
*    Pointer to the data buffer.
*/
U8 * FS__SB_GetBuffer(FS_SB * pSB) {
  U8 * pBuffer;

  pBuffer = pSB->pBuffer;
#if FS_SUPPORT_SECTOR_BUFFER_CACHE
  if (pBuffer == NULL) {
    pBuffer = FS__AllocSectorBufferEx(pSB->pVolume, SECTOR_INDEX_INVALID, NULL);
    if (pBuffer == NULL) {
      FS_DEBUG_ERROROUT((FS_MTYPE_API, "FS__SB_GetBuffer: No sector buffer available."));
      pSB->Error = FS_ERRCODE_BUFFER_NOT_AVAILABLE;
      FS_X_PANIC(FS_ERRCODE_BUFFER_NOT_AVAILABLE);
    }
  }
#endif // FS_SUPPORT_SECTOR_BUFFER_CACHE
  pSB->pBuffer = pBuffer;
#if FS_SUPPORT_SECTOR_BUFFER_BURST
  if (pBuffer != NULL) {
    U32      Off;
    U32      OffSector;
    unsigned ldBytesPerSector;

    ldBytesPerSector  = pSB->pVolume->FSInfo.Info.ldBytesPerSector;
    OffSector         = (U32)pSB->OffSector;
    Off               = OffSector << ldBytesPerSector;
    pBuffer          += Off;
  }
#endif
  return pBuffer;
}

/*********************************************************************
*
*       FS__SB_GetSectorIndex
*
*  Function description
*    Returns the index of the sector assigned to sector buffer.
*
*  Parameters
*    pSB              Instance of the sector buffer.
*
*  Return value
*    Index of the sector stored in the sector buffer.
*/
U32 FS__SB_GetSectorIndex(const FS_SB * pSB) {
  U32 SectorIndex;

  SectorIndex  = pSB->SectorIndex;
#if FS_SUPPORT_SECTOR_BUFFER_BURST
  SectorIndex += pSB->OffSector;
#endif // FS_SUPPORT_SECTOR_BUFFER_BURST
  return SectorIndex;
}

/*********************************************************************
*
*       FS__SB_GetVolume
*
*  Function description
*    Returns the instance of the volume assigned to the sector buffer.
*
*  Parameters
*    pSB              Instance of the sector buffer.
*
*  Return value
*    Volume instance.
*/
FS_VOLUME * FS__SB_GetVolume(const FS_SB * pSB) {
  return pSB->pVolume;
}

/*********************************************************************
*
*       FS__SB_SetWriteToJournal
*
*  Function description
*    Configures if the sector data has to be written via journal or not.
*
*  Parameters
*    pSB              Instance of the sector buffer.
*    WriteToJournal   Set to 1 if the sector data has to be written to journal.
*/
void FS__SB_SetWriteToJournal(FS_SB * pSB, int WriteToJournal) {
  pSB->WriteToJournal = (U8)WriteToJournal;
}

#if FS_SUPPORT_SECTOR_BUFFER_BURST

/*********************************************************************
*
*       FS__SB_SetSectorRange
*
*  Function description
*    Specifies a range of sector that can be accessed.
*
*  Parameters
*    pSB              Instance of the sector buffer.
*    SectorIndex      Index of the first sector in the range.
*    NumSectors       Number of sectors in the range.
*
*  Additional information
*    This function can be used to prevent that the sector buffer
*    accesses invalid sectors when it performs burst operations.
*    If the burst operation tries to access sectors outside
*    the specified range then the number of sector accessed
*    is reduced to prevent this.
*/
void FS__SB_SetSectorRange(FS_SB * pSB, U32 SectorIndex, U32 NumSectors) {
  pSB->SectorIndexRange = SectorIndex;
  pSB->NumSectorsRange  = NumSectors;
}

#endif // FS_SUPPORT_SECTOR_BUFFER_BURST

/*********************************************************************
*
*       FS__SB_GetBufferSize
*
*  Function description
*    Returns the size of the buffer.
*
*  Parameters
*    pSB              Instance of the sector buffer.
*
*  Return value
*    Buffer size in bytes.
*/
U32 FS__SB_GetBufferSize(const FS_SB * pSB) {
  U32      NumBytes;
  U32      NumSectors;
  unsigned ldBytesPerSector;

  ldBytesPerSector = pSB->pVolume->FSInfo.Info.ldBytesPerSector;
#if FS_SUPPORT_SECTOR_BUFFER_BURST
  NumSectors = pSB->NumSectors;
#else
  NumSectors = 1;
#endif // FS_SUPPORT_SECTOR_BUFFER_BURST
  NumBytes = NumSectors << ldBytesPerSector;
  return NumBytes;
}

/*********************************************************************
*
*       FS__AllocFileHandle
*
*  Function description
*    Returns a free file handle.
*
*  Return value
*    pFile    A valid free file handle.
*/
FS_FILE * FS__AllocFileHandle(void) {
  FS_FILE * pFile;

  pFile = FS_Global.pFirstFileHandle;
  //
  // While no free entry found.
  //
  for (;;) {
    if (pFile->InUse == 0u) {
      FS_FILE        * pNext;
#if FS_SUPPORT_FILE_BUFFER
      FS_FILE_BUFFER * pBuffer;
#endif

#if FS_SUPPORT_FILE_BUFFER
      //
      // Save the pointer to file buffer if it has been allocated globally.
      //
      pBuffer = NULL;
      if (FS_Global.FileBufferSize != 0u) {
        pBuffer = pFile->pBuffer;
      }
#endif
      //
      // Save the pNext pointer so that we can restore it back.
      //
      pNext = pFile->pNext;
      FS_MEMSET(pFile, 0, sizeof(FS_FILE));
      pFile->InUse   = 1;
      pFile->pNext   = pNext;
#if FS_SUPPORT_FILE_BUFFER
      pFile->pBuffer = pBuffer;
#endif
      break;
    }
    if (pFile->pNext == NULL) {
      pFile->pNext = SEGGER_PTR2PTR(FS_FILE, FS_TRY_ALLOC((I32)sizeof(FS_FILE), "FS_FILE"));        // MISRA deviation D:100[d]
      //
      // Check if we got a valid pointer.
      //
      if (pFile->pNext != NULL) {
        FS_MEMSET(pFile->pNext, 0, sizeof(FS_FILE));
      }
    }
    pFile = pFile->pNext;
    //
    // Neither a free file handle found
    // nor enough space to allocate a new one.
    //
    if (pFile == NULL) {
      break;
    }
  }
  return pFile;
}

/*********************************************************************
*
*       FS__FreeFileHandle
*
*  Function description
*    Closes the file handle and mark it as free.
*
*  Parameters
*    pFile    Opened file handle.
*
*  Additional information
*    This function sets the file object to NULL without to free it.
*/
void FS__FreeFileHandle(FS_FILE * pFile) {
  if (pFile != NULL) {
    FS_LOCK_SYS();
    pFile->InUse = 0;
    pFile->pFileObj = (FS_FILE_OBJ*)NULL;
    FS_UNLOCK_SYS();
  }
}

/*********************************************************************
*
*       FS__AllocFileObj
*
*  Function description
*    Returns a free file object.
*
*  Return value
*    !=NULL         A valid free file object.
*    ==NULL         An error occurred.
*/
FS_FILE_OBJ * FS__AllocFileObj(void) {
  FS_FILE_OBJ * pFileObj;

  //
  // Find next free entry.
  //
  FS_LOCK_SYS();
  pFileObj = FS_Global.pFirstFileObj;
  for (;;) {
    //
    // Neither a free file handle found
    // nor enough space to allocate a new one.
    //
    if (pFileObj == NULL) {
      break;
    }
    if (pFileObj->UseCnt == 0u) {
      FS_FILE_OBJ * pNext;

      //
      // Save the pNext pointer to be able to restore it back.
      //
      pNext = pFileObj->pNext;
      //
      // Initialize the file object.
      //
      FS_MEMSET(pFileObj, 0, sizeof(FS_FILE_OBJ));
      pFileObj->UseCnt = 1;
      pFileObj->pNext  = pNext;
      break;
    }
    if (pFileObj->pNext == NULL) {
      pFileObj->pNext = SEGGER_PTR2PTR(FS_FILE_OBJ, FS_TRY_ALLOC((I32)sizeof(FS_FILE_OBJ), "FS_FILE_OBJ"));     // MISRA deviation D:100[d]
      //
      // Check if we got a valid pointer.
      //
      if (pFileObj->pNext != NULL) {
        FS_MEMSET(pFileObj->pNext, 0, sizeof(FS_FILE_OBJ));
      }
    }
    pFileObj = pFileObj->pNext;
  }
  FS_UNLOCK_SYS();
  return pFileObj;
}

/*********************************************************************
*
*       FS__FreeFileObjNL
*
*  Function description
*    Closes the file object (non-locking version).
*
*  Parameters
*    pFileObj     Pointer to the file object to be freed. Can be NULL.
*/
void FS__FreeFileObjNL(FS_FILE_OBJ * pFileObj) {
  if (pFileObj != NULL) {
    if (pFileObj->UseCnt != 0u) {
      pFileObj->UseCnt--;
    }
#if FS_MULTI_HANDLE_SAFE
    if (pFileObj->UseCnt == 0u) {
      pFileObj->pVolume = NULL;             // Invalidate the volume instance to make sure that the file object is not accidentally closed by an unmount operation.
      pFileObj->acFullFileName[0] = '\0';   // Clear the file name when this file object is not used anymore.
    }
#endif
  }
}

/*********************************************************************
*
*       FS__FreeFileObj
*
*  Function description
*    Closes the file object.
*
*  Parameters
*    pFileObj     Pointer to the file object to be freed. Can be NULL.
*/
void FS__FreeFileObj(FS_FILE_OBJ * pFileObj) {
  FS_LOCK_SYS();
  FS__FreeFileObjNL(pFileObj);
  FS_UNLOCK_SYS();
}

/*********************************************************************
*
*       FS__FTell
*
*  Function description
*    Returns position of a file pointer.
*
*  Parameters
*    pFile      Handle to opened file. Cannot be NULL.
*
*  Return value
*    Current position of the file pointer.
*
*  Additional information
*    Internal version of FS_FTell().
*/
I32 FS__FTell(const FS_FILE * pFile) {
  I32 r;

  if (pFile == NULL) {
    return -1;                                  // Error, invalid file handle.
  }
#if FS_OS_LOCK_PER_DRIVER
  r = _GetFilePosDL(pFile);
#else
  r = _GetFilePosNL(pFile);
#endif // FS_OS_LOCK_PER_DRIVER
  return r;
}

/*********************************************************************
*
*       FS__CloseFileNL
*
*  Function description
*    Internal version of FS_FClose without driver locking.
*    Close a file referred by pFile.
*
*  Parameters
*    pFile      Handle to the file to be closed.
*
*  Return value
*    ==0        File handle has been closed.
*    !=0        Error, File handle can not be closed.
*/
int FS__CloseFileNL(FS_FILE * pFile) {
  int           r;
  int           Result;
  FS_FILE_OBJ * pFileObj;

  r = 0;    // Set to indicate success.
  if (pFile->InUse != 0u) {
    pFileObj = pFile->pFileObj;
#if FS_SUPPORT_FILE_BUFFER
    Result = FS__FB_Clean(pFile);
    if (Result != 0) {
      r = Result;
    }
#endif
    Result = FS_CLOSE_FILE(pFile);
    if (Result != 0) {
      r = Result;
    }
    FS__FreeFileObj(pFileObj);
    FS__FreeFileHandle(pFile);
  }
  return r;
}

/*********************************************************************
*
*       FS__CloseFile
*
*  Function description
*    Closes the specified file.
*
*  Parameters
*    pFile    Handle to file to be closed.
*
*  Return value
*    ==0    OK, file handle has been closed
*    !=0    Error code indicating the failure reason
*/
int FS__CloseFile(FS_FILE * pFile) {
  FS_FILE_OBJ * pFileObj;
#if FS_OS_LOCK_PER_DRIVER
  FS_DEVICE   * pDevice;
#endif // FS_OS_LOCK_PER_DRIVER
  FS_VOLUME   * pVolume;
  int           InUse;
  int           r;
  int           Result;

  pVolume  = NULL;
  FS_LOCK_SYS();
  InUse    = (int)pFile->InUse;
  pFileObj = pFile->pFileObj;
  if (pFileObj != NULL) {
    pVolume = pFileObj->pVolume;
  }
#if FS_OS_LOCK_PER_DRIVER
  pDevice = NULL;
  if (pVolume != NULL) {
    pDevice = &pVolume->Partition.Device;
  }
#endif // FS_OS_LOCK_PER_DRIVER
  FS_UNLOCK_SYS();
  if (InUse == 0) {           // File handle already freed?
    return FS_ERRCODE_INVALID_FILE_HANDLE;
  }
  if (pVolume == NULL) {      // File handle invalidated?
    FS__FreeFileHandle(pFile);
    return FS_ERRCODE_INVALID_FILE_HANDLE;
  }
  r = FS_ERRCODE_OK;          // Set to indicate success.
  FS_USE_PARA(pVolume);
  FS_LOCK_DRIVER(pDevice);
#if FS_OS_LOCK_PER_DRIVER
  FS_LOCK_SYS();
  if (pFileObj != pFile->pFileObj) {
    InUse = 0;
  }
  if (pFile->InUse == 0u) {
    InUse = 0;
  }
  FS_UNLOCK_SYS();
  if (InUse == 0) {           // Make sure the file is still valid
    FS_DEBUG_ERROROUT((FS_MTYPE_API, "Application error: File handle has been invalidated by other task during wait."));
    r = FS_ERRCODE_INVALID_FILE_HANDLE;
  } else
#endif // FS_OS_LOCK_PER_DRIVER
  {
#if FS_SUPPORT_JOURNAL
    Result = _CloseFileFS(pVolume, pFile);
#else
    Result = FS__CloseFileNL(pFile);
#endif // FS_SUPPORT_JOURNAL
    if (Result != 0) {
      r = FS_ERRCODE_WRITE_FAILURE;
    }
  }
  FS_UNLOCK_DRIVER(pDevice);
  return r;
}

/*********************************************************************
*
*       FS__FClose
*
*  Function description
*    Internal version of FS_FClose.
*    Closes the file referred by pFile.
*
*  Parameters
*    pFile    Handle to file to be closed.
*
*  Return value
*    ==0    OK, file handle has been closed
*    !=0    Error code indicating the failure reason
*/
int FS__FClose(FS_FILE * pFile) {
  int r;

  r = FS_ERRCODE_INVALID_PARA;
  if (pFile != NULL) {
    r = FS__CloseFile(pFile);
  }
  return r;
}

/*********************************************************************
*
*       FS__SyncFileNL
*
*  Function description
*    Internal version of FS__SyncFile.
*    Cleans the write buffer and updates the management information to storage medium.
*
*  Parameters
*    pVolume  Volume on which the file handle is located.
*    pFile    Handle to the file to be synchronized.
*
*  Return value
*    ==0      OK, file has been synchronized
*    !=0      Error code indicating the failure reason
*/
int FS__SyncFileNL(FS_VOLUME * pVolume, FS_FILE * pFile) {
  int r;

#if FS_SUPPORT_JOURNAL
  r = _UpdateFileFS(pVolume, pFile);
#else
  FS_USE_PARA(pVolume);
  r = _UpdateFile(pFile);
#endif // FS_SUPPORT_JOURNAL
  return r;
}

/*********************************************************************
*
*       FS__SyncFile
*
*  Function description
*    Internal version of FS_SyncFile.
*    Cleans the write buffer and updates the management information to storage medium.
*
*  Parameters
*    pFile    Handle to the file to be synchronized.
*
*  Return value
*    ==0      OK, file has been synchronized
*    !=0      Error code indicating the failure reason
*/
int FS__SyncFile(FS_FILE * pFile) {
  FS_FILE_OBJ * pFileObj;
#if FS_OS_LOCK_PER_DRIVER
  FS_DEVICE   * pDevice;
#endif
  FS_VOLUME   * pVolume;
  int           InUse;
  int           r;

  pVolume = NULL;
  FS_LOCK_SYS();
  InUse    = (int)pFile->InUse;
  pFileObj = pFile->pFileObj;
  if (pFileObj != NULL) {
    pVolume = pFileObj->pVolume;
  }
#if FS_OS_LOCK_PER_DRIVER
  pDevice = NULL;
  if (pVolume != NULL) {
    pDevice = &pVolume->Partition.Device;
  }
#endif
  FS_UNLOCK_SYS();
  if ((InUse == 0) || (pVolume == NULL)) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;      // Error, file handle not in use or invalidated.
  }
  FS_LOCK_DRIVER(pDevice);
#if FS_OS_LOCK_PER_DRIVER
  FS_LOCK_SYS();
  if (pFileObj != pFile->pFileObj) {
    InUse = 0;
  }
  if (pFile->InUse == 0u) {
    InUse = 0;
  }
  FS_UNLOCK_SYS();
  if (InUse == 0) {                             // Let's make sure the file is still valid.
    FS_DEBUG_ERROROUT((FS_MTYPE_API, "Application error: File handle has been invalidated by other task during wait."));
    FS_UNLOCK_DRIVER(pDevice);
    return FS_ERRCODE_INVALID_FILE_HANDLE;
  } else
#endif
  {
    r = FS__SyncFileNL(pVolume, pFile);
  }
  FS_UNLOCK_DRIVER(pDevice);
  return r;
}

/*********************************************************************
*
*       FS__SetFilePos
*
*  Function description
*    Internal version of FS_FSeek
*    Set current position of a file pointer.
*
*  Parameters
*    pFile      Handle to the opened file.
*    Off        New position of the file pointer (byte offset).
*    Origin     Mode for positioning the file pointer.
*/
void FS__SetFilePos(FS_FILE * pFile, FS_FILE_OFF Off, int Origin) {
  FS_LOCK_SYS();
  _SetFilePosNC(pFile, Off, Origin);
  FS_UNLOCK_SYS();
}

/*********************************************************************
*
*       FS__FSeek
*
*  Function description
*    Internal version of FS_FSeek for large files.
*    Set current position of a file pointer.
*
*  Parameters
*    pFile      Handle to the opened file.
*    Off        New position of the file pointer (byte offset).
*    Origin     Mode for positioning the file pointer.
*
*  Return value
*    ==0      OK, file pointer has been positioned according to the parameters.
*    !=0      Error code indicating the failure reason.
*/
int FS__FSeek(FS_FILE * pFile, FS_FILE_OFF Off, int Origin) {
  int r;

  r = FS_FILE_SET_POS(pFile, Off, Origin);
  return r;
}

/*********************************************************************
*
*       FS__CalcSizeInBytes
*
*  Function description
*    Given the numbers of clusters, sectors per cluster and bytes per sector,
*    calculate the equivalent number of bytes.
*
*  Parameters
*    NumClusters          Number of clusters.
*    SectorsPerCluster    Number of sectors in a cluster.
*    BytesPerSector       Number of bytes in a sector.
*
*  Return value
*    The number of bytes, or if the number would exceed the range U32
*    can represent, return 0xFFFFFFFF.
*/
U32 FS__CalcSizeInBytes(U32 NumClusters, U32 SectorsPerCluster, U32 BytesPerSector) {
  if (_CalcSizeInKB(NumClusters, SectorsPerCluster, BytesPerSector) < 0x400000uL) {
    return NumClusters * SectorsPerCluster * BytesPerSector;
  }
  return 0xFFFFFFFFuL;    // Max. value of U32. The size in bytes does not fit into a U32.
}

/*********************************************************************
*
*       FS__CalcSizeInKB
*
*  Function description
*    Given the numbers of clusters, sectors per cluster and bytes per sector,
*    calculate the equivalent number of KBytes.
*
*  Parameters
*    NumClusters          Number of clusters.
*    SectorsPerCluster    Number of sectors in a cluster.
*    BytesPerSector       Number of bytes in a sector.
*
*  Return value
*    The value in kilo bytes.
*/
U32 FS__CalcSizeInKB(U32 NumClusters, U32 SectorsPerCluster, U32 BytesPerSector) {
  U32 v;

  v = _CalcSizeInKB(NumClusters, SectorsPerCluster, BytesPerSector);
  return v;
}

/*********************************************************************
*
*       FS__RemoveEx
*
*  Function description
*    Removes a file.
*
*  Parameters
*    pVolume      Volume on which the file is located.
*    sFileName    Fully qualified name of the file to be deleted.
*
*  Return value
*    ==0      File has been removed
*    !=0      Error code indicating the failure reason
*/
int FS__RemoveEx(FS_VOLUME * pVolume, const char * sFileName) {
  int r;

  //
  // Delete the file by calling the file open function with the delete flag set.
  //
  r = FS__OpenFileDL(pVolume, sFileName, FS_FILE_ACCESS_FLAG_W, 0, 1, 0, NULL);
  return r;
}

/*********************************************************************
*
*       FS__Remove
*
*  Function description
*    Internal version of FS_Remove. Removes a file.
*    There is no real 'delete' function in the FSL, but the FSL's 'open'
*    function can delete a file.
*
*  Parameters
*    sFileName    Fully qualified name of the file to be deleted.
*
*  Return value
*    ==0      File has been removed
*    !=0      Error code indicating the failure reason
*/
int FS__Remove(const char * sFileName) {
  int r;

  //
  // Delete the file by calling the file open function with the delete flag set.
  //
  r = FS__OpenFileEx(sFileName, FS_FILE_ACCESS_FLAG_W, 0, 1, 0, NULL);
  return r;
}

/*********************************************************************
*
*       FS__CreateFileHandle
*
*  Function description
*    Allocates a file handle and initializes its file buffer.
*/
int FS__CreateFileHandle(const FS_VOLUME * pVolume, unsigned AccessFlags, FS_FILE_OBJ * pFileObj, FS_FILE ** ppFile) {
  FS_FILE * pFile;
  int       r;

  r = FS_ERRCODE_OK;          // Set to indicate success.
  //
  // Allocate file handle.
  //
  FS_LOCK_SYS();
  pFile = FS__AllocFileHandle();
  if (pFile == NULL) {
    FS_DEBUG_ERROROUT((FS_MTYPE_API, "FS__CreateFileHandle: No file handle available."));
    r = FS_ERRCODE_TOO_MANY_FILES_OPEN;
  } else {
    pFile->AccessFlags = (U8)AccessFlags;
    //
    // Allocate or invalidate the file buffer.
    //
#if FS_SUPPORT_FILE_BUFFER
    {
      FS_FILE_BUFFER * pBuffer;
      unsigned         FileBufferSize;
      I32              NumBytes;
      unsigned         FileBufferFlags;
      unsigned         FileBufferFlagsVolume;

      //
      // Determine the operating mode of the file buffer.
      //
      FileBufferFlags       = FS_Global.FileBufferFlags;
      FileBufferFlagsVolume = pVolume->FileBufferFlags;
      if ((FileBufferFlagsVolume & FILE_BUFFER_FLAGS_VALID) != 0u) {
        FileBufferFlags = FileBufferFlagsVolume & ~FILE_BUFFER_FLAGS_VALID;
      }
      pBuffer = pFile->pBuffer;
      if (pBuffer != NULL) {            // File buffer already allocated?
        pBuffer->FilePos          = 0;
        pBuffer->Flags            = (U8)FileBufferFlags;
        pBuffer->IsDirty          = 0;
        pBuffer->NumBytesInBuffer = 0;
      } else {
        FileBufferSize = FS_Global.FileBufferSize;
        if (FileBufferSize != 0u) {
          //
          // Allocate the buffer structure and the data buffer.
          //
          NumBytes = (I32)sizeof(FS_FILE_BUFFER) + (I32)FileBufferSize;
          pBuffer = SEGGER_PTR2PTR(FS_FILE_BUFFER, FS_TRY_ALLOC((I32)NumBytes, "FS_FILE_BUFFER"));        // MISRA deviation D:100[d]
          if (pBuffer != NULL) {
            FS_MEMSET(pBuffer, 0, sizeof(FS_FILE_BUFFER));
            pBuffer->pData      = (U8 *)(pBuffer + 1);
            pBuffer->BufferSize = FileBufferSize;
            pBuffer->Flags      = (U8)FileBufferFlags;
            pFile->pBuffer      = pBuffer;
          }
        }
      }
    }
#else
    FS_USE_PARA(pVolume);
#endif // FS_SUPPORT_FILE_BUFFER
  }
  if (ppFile != NULL) {
    if (pFile != NULL) {
      pFile->pFileObj = pFileObj;
    }
    *ppFile = pFile;
  }
  FS_UNLOCK_SYS();
  if (ppFile == NULL) {
    //
    // Prevent a file handle leak.
    //
    FS__FreeFileHandle(pFile);
  }
  return r;
}

/*********************************************************************
*
*       FS__CreateFileObj
*
*  Function description
*    Allocates a file object and assigns it to a file handle.
*/
int FS__CreateFileObj(FS_VOLUME * pVolume, const char * sFileName, FS_FILE_OBJ ** ppFileObj) {
  int           r;
  FS_FILE_OBJ * pFileObj;

  pFileObj = FS__AllocFileObj();
  if (pFileObj == NULL) {
    FS_DEBUG_ERROROUT((FS_MTYPE_API, "FS__CreateFileObject: No file object available."));
    r = FS_ERRCODE_TOO_MANY_FILES_OPEN;
  } else {
    FS_LOCK_SYS();
    pFileObj->pVolume = pVolume;
    FS_UNLOCK_SYS();
#if FS_MULTI_HANDLE_SAFE
    {
      FS_FILE_OBJ  * pFileObjShared;
      unsigned       NumBytesFileNameFQ;
      char         * sFileNameFQ;
      FS_FILE      * pFileToCheck;
      FS_FILE      * pFileToUpdate;
      FS_WRITEMODE   WriteMode;
      int            Result;

      //
      // Check if a file object with the same file name is already in use.
      // If so, free the original file object and use the already existing one instead.
      //
      sFileNameFQ        = pFileObj->acFullFileName;
      NumBytesFileNameFQ = sizeof(pFileObj->acFullFileName);
      Result = FS__BuildFileNameFQ(pVolume, sFileName, sFileNameFQ, NumBytesFileNameFQ);
      if (Result < 0) {
        r = Result;
      } else {
        r = 0;
        pFileObjShared = _FindFileObj(sFileNameFQ, pFileObj);
        if (pFileObjShared != NULL) {
          FS__FreeFileObj(pFileObj);
          pFileObjShared->UseCnt++;
          pFileObj = pFileObjShared;
        }
        //
        // Make sure that the information in the directory entry of the shared file
        // is up to date before we open the file.
        //
        if (pFileObjShared != NULL) {
          pFileToUpdate = NULL;
          WriteMode = FS__GetFileWriteModeEx(pVolume);
          //
          // In the FS_WRITEMODE_SAFE the directory entry is updated on each write,
          // therefore we do not have to do it here.
          //
          if ((WriteMode == FS_WRITEMODE_FAST) || (WriteMode == FS_WRITEMODE_MEDIUM)) {
            //
            // Check which other opened file handle is using the same file object.
            //
            FS_LOCK_SYS();
            pFileToCheck = FS_Global.pFirstFileHandle;
            while (pFileToCheck != NULL) {
              if (pFileToCheck->InUse != 0u) {
                if (pFileToCheck->pFileObj == pFileObjShared) {
                  //
                  // We have found an opened file handle that is using the same file object.
                  // Check if this is the file handle that is used to write to file and if so update the directory entry.
                  //
                  if ((pFileToCheck->AccessFlags & FS_FILE_ACCESS_FLAGS_AW) != 0u) {
                    pFileToUpdate = pFileToCheck;
                    break;
                  }
                }
              }
              pFileToCheck = pFileToCheck->pNext;
            }
            FS_UNLOCK_SYS();
            if (pFileToUpdate != NULL) {
              (void)FS_CLOSE_FILE(pFileToUpdate);
            }
          }
        }
      }
    }
#else
    FS_USE_PARA(sFileName);
    r = FS_ERRCODE_OK;                  // Set to indicate success.
#endif // FS_MULTI_HANDLE_SAFE
  }
  if (ppFileObj != NULL) {
    *ppFileObj = pFileObj;
  } else {
    //
    // Prevent a file object leak.
    //
    FS__FreeFileObj(pFileObj);
  }
  return r;
}

/*********************************************************************
*
*       FS__OpenFile
*
*  Function description
*    Opens a file at file system level.
*
*  Parameters
*    sFileName      Name of the file to open (partially qualified).
*    pFile          File handle that is used to access the file.
*    DoDel          Set to 1 if the file has to be deleted if it exists.
*    DoOpen         Set to 1 if the file has to be opened for data access.
*    DoCreate       Set to 1 if the file has to be created if it does not exists.
*
*  Return value
*    ==0    OK, file opened.
*    !=0    Error code indicating the failure reason.
*/
int FS__OpenFile(const char * sFileName, FS_FILE * pFile, int DoDel, int DoOpen, int DoCreate) {
  int r;

#if FS_SUPPORT_JOURNAL
  r = _OpenFileFS(sFileName, pFile, DoDel, DoOpen, DoCreate);
#else
  r = FS_OPEN_FILE(sFileName, pFile, DoDel, DoOpen, DoCreate);
#endif // FS_SUPPORT_JOURNAL
  return r;
}

/*********************************************************************
*
*       FS_FILE_Open
*
*  Function description
*    Opens a handle to a file.
*
*  Parameters
*    pVolume      Pointer to a volume structure.
*    sFileName    Name of the file to open (partially qualified).
*    AccessFlags  Type of file access.
*    DoCreate     Shall the file be created.
*    DoDel        Shall the existing file be deleted.
*    DoOpen       Shall the file be opened.
*    ppFile       [OUT] Pointer to a file handle pointer that
*                 receives the handle to the opened file.
*
*  Return value
*    ==0    OK, file opened.
*    !=0    Error code indicating the failure reason.
*/
int FS_FILE_Open(FS_VOLUME * pVolume, const char * sFileName, unsigned AccessFlags, int DoCreate, int DoDel, int DoOpen, FS_FILE ** ppFile) {
  FS_FILE     * pFile;
  FS_FILE_OBJ * pFileObj;
  int           r;

  pFile    = NULL;
  pFileObj = NULL;
  //
  // Create a file object and a file handle and then perform the operation in the file system layer.
  // The creation order is important because the unmount function frees any file handles that
  // do not have an associated file object.
  //
  r = FS__CreateFileObj(pVolume, sFileName, &pFileObj);
  if (r == 0) {
    r = FS__CreateFileHandle(pVolume, AccessFlags, pFileObj, &pFile);
    if (r == 0) {
      r = FS__OpenFile(sFileName, pFile, DoDel, DoOpen, DoCreate);
    }
  }
  //
  // Perform clean up in case of an error or if the caller
  // does not need the file handle.
  //
  if ((r != 0) || (ppFile == NULL)) {
    if (pFileObj != NULL) {
      FS__FreeFileObj(pFileObj);
    }
    if (pFile != NULL) {
      FS__FreeFileHandle(pFile);
    }
    pFile = NULL;
  }
  //
  // Return the file handle.
  //
  if (ppFile != NULL) {
    *ppFile = pFile;
  }
  return r;
}

/*********************************************************************
*
*       FS_FILE_Read
*
*  Function description
*    Internal version of FS_Read.
*    Reads data from a file.
*
*  Parameters
*    pFile        Pointer to an opened file handle.
*    pData        Pointer to a buffer to receive the data.
*    NumBytes     Number of bytes to be read.
*
*  Return value
*    Number of bytes read.
*/
U32 FS_FILE_Read(FS_FILE * pFile, void * pData, U32 NumBytes) {
  U32 r;

  r = FS_FREAD(pFile, pData, NumBytes);
  return r;
}

/*********************************************************************
*
*       FS_FILE_Write
*
*  Function description
*    Write data to a file.
*
*  Parameters
*    pFile        Pointer to an opened file handle.
*    pData        Pointer to the data to be written.
*    NumBytes     Number of bytes to be written.
*
*  Return value
*    Number of bytes written.
*/
U32 FS_FILE_Write(FS_FILE * pFile, const void * pData, U32 NumBytes) {
  U32 r;

  r = FS_FWRITE(pFile, pData, NumBytes);
  return r;
}

/*********************************************************************
*
*       FS_FILE_SetPos
*/
int FS_FILE_SetPos(FS_FILE * pFile, FS_FILE_OFF Off, int Origin) {
  int r;

  if (pFile == NULL) {
    return FS_ERRCODE_INVALID_PARA;       // Error, the file handle is invalid.
  }
  if ((Origin != FS_SEEK_SET) && (Origin != FS_SEEK_CUR) && (Origin != FS_SEEK_END)) {
    FS_LOCK_SYS();
    pFile->Error = FS_ERRCODE_INVALID_PARA;
    FS_UNLOCK_SYS();
    return FS_ERRCODE_INVALID_PARA;       // Error, invalid parameter.
  }
#if FS_OS_LOCK_PER_DRIVER
  r = _SetFilePosDL(pFile, Off, Origin);
#else
  r = _SetFilePosNL(pFile, Off, Origin);
#endif // FS_OS_LOCK_PER_DRIVER
  return r;
}

/*********************************************************************
*
*       FS_FILE_SetEnd
*/
int FS_FILE_SetEnd(FS_FILE * pFile) {
  int r;

  r = FS_SET_END_OF_FILE(pFile);
  return r;
}

/*********************************************************************
*
*       FS_FILE_SetSize
*/
int FS_FILE_SetSize(FS_FILE * pFile, FS_FILE_SIZE NumBytes) {
  int r;

  r = FS_SET_FILE_SIZE(pFile, NumBytes);
  return r;
}

/*********************************************************************
*
*       FS_FILE_GetSize
*/
FS_FILE_SIZE FS_FILE_GetSize(FS_FILE * pFile) {
  FS_FILE_SIZE r;

  FS_LOCK_SYS();
  r = (FS_FILE_SIZE)pFile->pFileObj->Size;
  FS_UNLOCK_SYS();
  return r;
}

/*********************************************************************
*
*       FS__OpenFileDL
*
*  Function description
*    Opens a handle to a file.
*
*  Parameters
*    pVolume      Pointer to a volume structure.
*    sFileName    Name of the file to open (partially qualified).
*    AccessFlags  Type of file access.
*    DoCreate     Shall the file be created.
*    DoDel        Shall the existing file be deleted.
*    DoOpen       Shall the file be opened.
*    ppFile       [OUT] Pointer to a file handle pointer that
*                 receives the handle to the opened file.
*
*  Return value
*    ==0    OK, file opened.
*    !=0    Error code indicating the failure reason.
*/
int FS__OpenFileDL(FS_VOLUME * pVolume, const char * sFileName, unsigned AccessFlags, int DoCreate, int DoDel, int DoOpen, FS_FILE ** ppFile) {
  int r;

  //
  // Lock the driver and perform the operation.
  //
  FS_LOCK_DRIVER(&pVolume->Partition.Device);
#if FS_OS_LOCK_PER_DRIVER
  if (pVolume->MountType == 0u) {
    FS_DEBUG_ERROROUT((FS_MTYPE_API, "FS__OpenFileDL: Volume has been unmounted by other task during wait."));
    r = FS_ERRCODE_VOLUME_NOT_MOUNTED;
  } else
#endif // FS_OS_LOCK_PER_DRIVER
  {
    r = FS_FILE_OPEN(pVolume, sFileName, AccessFlags, DoCreate, DoDel, DoOpen, ppFile);
  }
  FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
  return r;
}

/*********************************************************************
*
*       FS__OpenFileEx
*
*  Function description
*    Opens a file and returns a handle to it.
*
*  Parameters
*    sFileName      File name (partially qualified).
*    AccessFlags    Type of file access.
*    DoCreate       Set to 1 if the file has to be created if it does not exists.
*    DoDel          Set to 1 if the file has to be deleted if it exists.
*    DoOpen         Set to 1 if the file has to be opened for data access.
*    ppFile         [OUT] Returns a pointer to the opened file handle.
*
*  Return value
*    ==0    OK, file opened.
*    !=0    Error code indicating the failure reason.
*
*  Additional information
*    Depending on the DoCreate, DoDel or DoOpen parameters, the function
*    is able to perform the following operations on a file:
*    - open an existing file
*    - create a new file
*    - delete a file
*    Not all the value combinations of DoCreate, DoDel and DoOpen are valid. The open function
*    of the file system layer reports an error if the value combination is not valid.
*/
int FS__OpenFileEx(const char * sFileName, unsigned AccessFlags, int DoCreate, int DoDel, int DoOpen, FS_FILE ** ppFile) {
  FS_VOLUME * pVolume;
  int         r;

  pVolume = FS__FindVolumeEx(sFileName, &sFileName);
  if (pVolume == NULL) {
    return FS_ERRCODE_VOLUME_NOT_FOUND;
  }
  //
  // Volume found. Check that the file name is valid.
  //
  if (sFileName == NULL) {
    return FS_ERRCODE_INVALID_PARA;
  }
  if (*sFileName == '\0') {
    return FS_ERRCODE_INVALID_PARA;
  }
  //
  // OK we have a valid file name. Try to mount the volume if necessary.
  //
  r = FS__AutoMount(pVolume);
  switch ((unsigned)r) {
  case FS_MOUNT_RW:
    r = 0;
    break;
  case FS_MOUNT_RO:
    r = 0;
    //
    // Report an error if the application tries to open a file
    // in write mode on a read-only mounted volume.
    //
    if ((AccessFlags & FS_FILE_ACCESS_FLAGS_ACW) != 0u) {
      r = FS_ERRCODE_READ_ONLY_VOLUME;
    }
    break;
  case 0:
    r = FS_ERRCODE_VOLUME_NOT_MOUNTED;
    break;
  default:
    //
    // Error, could not mount the volume.
    //
    break;
  }
  if (r != 0) {
    return r;         // Error while mounting the volume.
  }
  r = FS__OpenFileDL(pVolume, sFileName, AccessFlags, DoCreate, DoDel, DoOpen, ppFile);
  return r;
}

/*********************************************************************
*
*       FS__GetFileWriteModeEx
*
*  Function description
*    Returns the write mode of a volume.
*/
FS_WRITEMODE FS__GetFileWriteModeEx(const FS_VOLUME * pVolume) {
  FS_WRITEMODE WriteMode;

  WriteMode = pVolume->WriteMode;
  if (WriteMode == FS_WRITEMODE_UNKNOWN) {
    WriteMode = FS_Global.WriteMode;
  }
  return WriteMode;
}

/*********************************************************************
*
*       FS__GetTimeDate
*
*  Function description
*    Returns the current date and time.
*
*  Additional information
*    The returned value is encoded as described in the definition
*    of FS_TIME_DATE_CALLBACK.
*/
U32 FS__GetTimeDate(void) {
  U32 TimeDate;

  TimeDate = TIME_DATE_DEFAULT;   // Second: 0, Minute: 0, Hour: 0, Day: 1, Month: 1, Year: 0 (1980)
  if (_pfTimeDate != NULL) {
    TimeDate = _pfTimeDate();
  }
  return TimeDate;
}

/*********************************************************************
*
*       FS__GetJournalFileName
*
*  Function description
*    Returns the name of the journal file.
*/
const char * FS__GetJournalFileName(const FS_VOLUME * pVolume) {
  const char * sFileName;

#if (FS_SUPPORT_JOURNAL != 0) && (FS_MAX_LEN_JOURNAL_FILE_NAME > 0)
  sFileName = pVolume->acJournalFileName;
#else
  FS_USE_PARA(pVolume);
  sFileName = FS_JOURNAL_FILE_NAME;
#endif
  return sFileName;
}

/*********************************************************************
*
*       FS__FindDirDelimiter
*
*  Function description
*    Returns the position of the first directory delimiter in the file name.
*
*  Parameters
*    sFileName    [IN] File name to be searched (0-terminated string).
*                 It cannot be NULL.
*
*  Return value
*    !=NULL     Pointer in sFileName to the first directory delimiter.
*    ==NULL     Directory delimiter not found.
*
*  Additional information
*    The function accepts either slash or backslash as directory delimiter.
*    The value of FS_DIRECTORY_DELIMITER is ignored.
*/
const char * FS__FindDirDelimiter(const char * sFileName) {
  char         c;
  const char * s;

  s = NULL;
#if FS_SUPPORT_MBCS
  if (FS_pCharSetType->pfGetChar != NULL) {
    FS_WCHAR Char;
    unsigned NumBytes;
    unsigned NumBytesRead;

    NumBytes = FS_STRLEN(sFileName);
    for (;;) {
      if (NumBytes == 0u) {
        break;                // End of file name reached.
      }
      NumBytesRead = 0;
      Char = FS_pCharSetType->pfGetChar(SEGGER_CONSTPTR2PTR(const U8, sFileName), NumBytes, &NumBytesRead);       // MISRA deviation D:100[e]
      if (Char == FS_WCHAR_INVALID) {
        break;                // Error, invalid character sequence.
      }
      if (Char < 128u) {      // The directory delimiter is always an ASCII character.
        if (_IsDirectoryDelimiter((char)Char) != 0) {
          s = sFileName;
          break;
        }
      }
      NumBytes  -= NumBytesRead;
      sFileName += NumBytesRead;
    }
  } else
#endif // FS_SUPPORT_MBCS
  {
    for (;;) {
      c = *sFileName;
      if (c == '\0') {
        break;
      }
      if (_IsDirectoryDelimiter(c) != 0) {
        s = sFileName;
        break;
      }
      ++sFileName;
    }
  }
  return s;
}

/*********************************************************************
*
*       FS__FOpenEx
*
*  Function description
*    Internal version of FS_FOpen.
*    Open an existing file or create a new one.
*
*  Parameters
*    sFileName  Fully qualified file name (0-terminated).
*    sMode      Mode for opening the file (0-terminated).
*    ppFile     Pointer to a file handle pointer that receives
*               the opened file handle.
*
*  Return value
*    ==0    OK, file opened.
*    !=0    Error code indicating the failure reason.
*/
int FS__FOpenEx(const char * sFileName, const char * sMode, FS_FILE ** ppFile) {
  unsigned               AccessFlags;
  int                    DoCreate;
  int                    DoDel;
  int                    DoOpen;
  int                    r;
  const FS_ACCESS_MODE * pAccessMode;

  //
  // Convert and check the open mode.
  //
  pAccessMode = FS__GetAccessMode(sMode);
  if (pAccessMode == NULL) {
    FS_DEBUG_ERROROUT((FS_MTYPE_API, "FS__FOpenEx: Invalid access flags (sMode: %s).", sMode));
    return FS_ERRCODE_INVALID_PARA;
  }
  if (ppFile == NULL) {
    FS_DEBUG_ERROROUT((FS_MTYPE_API, "FS__FOpenEx: Invalid file handle."));
    return FS_ERRCODE_INVALID_PARA;
  }
  //
  // All checks have been performed, lets do the work.
  //
  AccessFlags = pAccessMode->AccessFlags;
  DoDel       = (int)pAccessMode->DoDel;
  DoOpen      = (int)pAccessMode->DoOpen;
  DoCreate    = (int)pAccessMode->DoCreate;
  r = FS__OpenFileEx(sFileName, AccessFlags, DoCreate, DoDel, DoOpen, ppFile);
  return r;
}

/*********************************************************************
*
*       FS__FError
*
*  Function description
*    Internal version of FS_FError().
*/
I16 FS__FError(const FS_FILE * pFile) {
  I16 r;

  r = FS_ERRCODE_INVALID_PARA;
  if (pFile != NULL) {
    FS_LOCK_SYS();
    r = pFile->Error;
    FS_UNLOCK_SYS();
#if FS_SUPPRESS_EOF_ERROR
    if (r == FS_ERRCODE_EOF) {
      if (_IsEOFErrorSuppressed != 0u) {
        r = FS_ERRCODE_OK;
      }
    }
#endif // FS_SUPPRESS_EOF_ERROR
  }
  return r;
}

/*********************************************************************
*
*       FS__FEof
*
*  Function description
*    Internal version of FS_FEof() without global locking.
*/
int FS__FEof(const FS_FILE * pFile) {
  int r;

  if (pFile == NULL) {
    return FS_ERRCODE_INVALID_PARA;
  }
#if FS_OS_LOCK_PER_DRIVER
  r = _IsEndOfFileDL(pFile);
#else
  r = _IsEndOfFileNL(pFile);
#endif // FS_OS_LOCK_PER_DRIVER
  return r;
}

#if FS_SUPPORT_FILE_BUFFER

/*********************************************************************
*
*       FS__SetFileBufferFlags
*
*  Function description
*    Internal version of FS_SetFileBufferFlags() without global locking.
*/
int FS__SetFileBufferFlags(const FS_FILE * pFile, int Flags) {
  FS_FILE_BUFFER * pFileBuffer;

  if (pFile == NULL) {
    return FS_ERRCODE_INVALID_PARA;                 // Error, invalid file handle.
  }
  if (pFile->InUse == 0u) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;          // Error, file handle has been closed.
  }
  FS_LOCK_SYS();
  pFileBuffer = pFile->pBuffer;
  FS_UNLOCK_SYS();
  if (pFileBuffer == NULL) {
    return FS_ERRCODE_INVALID_USAGE;                // Error, file buffer not configured.
  }
  FS_LOCK_SYS();
  pFileBuffer->Flags = (U8)Flags;
  FS_UNLOCK_SYS();
  return 0;                                         // OK, file buffer flags modified.
}

#endif // FS_SUPPORT_FILE_BUFFER

#if FS_SUPPORT_ENCRYPTION

/*********************************************************************
*
*       FS__SetEncryptionObject
*
*  Function description
*    Internal version of FS_SetEncryptionObject() without global locking.
*/
int FS__SetEncryptionObject(const FS_FILE * pFile, FS_CRYPT_OBJ * pCryptObj) {
  int r;

  if (pFile == NULL) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;         // Error, the file handle is not valid.
  }
#if FS_OS_LOCK_PER_DRIVER
  r = _SetEncryptionObjectDL(pFile, pCryptObj);
#else
  r = _SetEncryptionObjectNL(pFile, pCryptObj);
#endif // FS_OS_LOCK_PER_DRIVER
  return r;
}

#endif // FS_SUPPORT_ENCRYPTION

/*********************************************************************
*
*       FS__GetAccessMode
*
*  Function description
*    Converts the "open-mode-string" into flags using a table.
*/
const FS_ACCESS_MODE * FS__GetAccessMode(const char * sMode) {
  int                    i;
  const FS_ACCESS_MODE * pAccessMode;

  pAccessMode = _aAccessMode;
  for (i = 0; i < (int)SEGGER_COUNTOF(_aAccessMode); i++) {
    if (FS_STRCMP(sMode, pAccessMode->sMode) == 0) {
      return pAccessMode;     // OK, valid access mode.
    }
    ++pAccessMode;
  }
  return NULL;                // Error, not a valid access mode.
}

#if FS_MULTI_HANDLE_SAFE

/*********************************************************************
*
*       FS__BuildFileNameFQ
*
*  Function description
*    Stores the fully qualified filename (including volume and path)
*    into the destination buffer.
*
*  Parameters
*    pVolume    Instance of the volume on which the file is stored.
*    sFileName  [IN] Partially qualified file name.
*    sDest      [OUT] Fully qualified file name.
*    DestSize   Maximum number of bytes that can be stored to sDest.
*
*  Return value
*    >=0    OK, number of bytes stored to the specified buffer.
*    !=0    Error code indicating the failure reason.
*
*  Additional information
*    This function can also be used to check that the file name
*    fits into the specified buffer by setting sDest to NULL.
*    An error is returned if the file name is too long.
*    If the file name fits, than the number of bytes required
*    to store the fully qualified file name including the 0-terminator
*    is returned.
*/
int FS__BuildFileNameFQ(FS_VOLUME * pVolume, const char * sFileName, char * sDest, unsigned DestSize) {
  unsigned     NumBytes;
  unsigned     NumBytesFileName;
  unsigned     NumBytesTotal;
  const char * sDriverName;
  FS_DEVICE  * pDevice;

  pDevice = &pVolume->Partition.Device;
  sDriverName = pDevice->pType->pfGetName(pDevice->Data.Unit);
  NumBytesFileName = (unsigned)FS_STRLEN(sFileName);
  NumBytes         = (unsigned)FS_STRLEN(sDriverName);
  NumBytesTotal    = NumBytes + NumBytesFileName + 3u;    // We need 3 characters for the device unit.
  if (*sFileName != FS_DIRECTORY_DELIMITER) {
    ++NumBytesTotal;                                      // One character for the directory delimiter.
  }
  ++NumBytesTotal;                                        // One character for the string terminator.
  if (NumBytesTotal > DestSize) {
    return FS_ERRCODE_FILENAME_TOO_LONG;                  // Error, file name is too long to store in sDest.
  }
  if (sDest != NULL) {
    (void)FS_STRNCPY(sDest, sDriverName, DestSize);
    *(sDest + NumBytes++) = ':';
    *(sDest + NumBytes++) = (char)('0' + pDevice->Data.Unit);
    *(sDest + NumBytes++) = ':';
    if (*sFileName != FS_DIRECTORY_DELIMITER) {
      *(sDest + NumBytes++) = FS_DIRECTORY_DELIMITER;
    }
    (void)FS_STRNCPY(sDest + NumBytes, sFileName, DestSize - NumBytes);
    NumBytes += NumBytesFileName;
    *(sDest + NumBytes) = '\0';
  }
  return (int)NumBytesTotal;
}

#endif // FS_MULTI_HANDLE_SAFE

#if (FS_SUPPORT_FAT == 0) && (FS_SUPPORT_EFS == 0)

/*********************************************************************
*
*       FS_NONE_CloseFile
*/
int FS_NONE_CloseFile(FS_FILE * pFile) {
  FS_USE_PARA(pFile);
  return 0;
}

/*********************************************************************
*
*       FS_NONE_CheckFS_API
*/
int FS_NONE_CheckFS_API(FS_VOLUME * pVolume) {
  FS_USE_PARA(pVolume);
  return 0;
}

/*********************************************************************
*
*       FS_NONE_Read
*/
U32 FS_NONE_Read(FS_FILE * pFile, void * pData, U32 NumBytes) {
  FS_USE_PARA(pFile);
  FS_USE_PARA(pData);
  FS_USE_PARA(NumBytes);
  return NumBytes;
}

/*********************************************************************
*
*       FS_NONE_Write
*/
U32 FS_NONE_Write(FS_FILE * pFile, const void * pData, U32 NumBytes) {
  FS_USE_PARA(pFile);
  FS_USE_PARA(pData);
  FS_USE_PARA(NumBytes);
  return NumBytes;
}

/*********************************************************************
*
*       FS_NONE_OpenFile
*/
int FS_NONE_OpenFile(const char * sFileName, FS_FILE * pFile, int DoDel, int DoOpen, int DoCreate) {
  FS_USE_PARA(sFileName);
  FS_USE_PARA(pFile);
  FS_USE_PARA(DoDel);
  FS_USE_PARA(DoOpen);
  FS_USE_PARA(DoCreate);
  return 0;
}

/*********************************************************************
*
*       FS_NONE_Format
*/
int FS_NONE_Format(FS_VOLUME * pVolume, const FS_FORMAT_INFO * pFormatInfo) {
  FS_USE_PARA(pVolume);
  FS_USE_PARA(pFormatInfo);
  return 0;
}

/*********************************************************************
*
*       FS_NONE_OpenDir
*/
int FS_NONE_OpenDir(const char * pDirName,  FS_DIR_OBJ * pDirObj) {
  FS_USE_PARA(pDirName);
  FS_USE_PARA(pDirObj);
  return 0;
}

/*********************************************************************
*
*       FS_NONE_CloseDir
*/
int FS_NONE_CloseDir(FS_DIR_OBJ * pDirObj) {
  FS_USE_PARA(pDirObj);
  return 0;
}

/*********************************************************************
*
*       FS_NONE_ReadDir
*/
int FS_NONE_ReadDir(FS_DIR_OBJ * pDirObj, FS_DIRENTRY_INFO * pDirEntryInfo) {
  FS_USE_PARA(pDirObj);
  FS_USE_PARA(pDirEntryInfo);
  return 0;
}

/*********************************************************************
*
*       FS_NONE_RemoveDir
*/
int FS_NONE_RemoveDir(FS_VOLUME * pVolume, const char * sDirName) {
  FS_USE_PARA(pVolume);
  FS_USE_PARA(sDirName);
  return 0;
}

/*********************************************************************
*
*       FS_NONE_CreateDir
*/
int FS_NONE_CreateDir(FS_VOLUME * pVolume, const char * sDirName) {
  FS_USE_PARA(pVolume);
  FS_USE_PARA(sDirName);
  return 0;
}

/*********************************************************************
*
*       FS_NONE_Rename
*/
int FS_NONE_Rename(FS_VOLUME * pVolume, const char * sOldName, const char * sNewName) {
  FS_USE_PARA(sOldName);
  FS_USE_PARA(sNewName);
  FS_USE_PARA(pVolume);
  return 0;
}

/*********************************************************************
*
*       FS_NONE_DeleteDir
*/
int FS_NONE_DeleteDir(FS_VOLUME * pVolume, const char * sDirName, int MaxRecursionLevel) {
  FS_USE_PARA(pVolume);
  FS_USE_PARA(sDirName);
  FS_USE_PARA(MaxRecursionLevel);
  return 0;
}

/*********************************************************************
*
*       FS_NONE_Move
*/
int FS_NONE_Move(FS_VOLUME * pVolume, const char * sOldName,  const char * sNewName) {
  FS_USE_PARA(sOldName);
  FS_USE_PARA(sNewName);
  FS_USE_PARA(pVolume);
  return 0;
}

/*********************************************************************
*
*       FS_NONE_SetDirEntryInfo
*/
int FS_NONE_SetDirEntryInfo(FS_VOLUME * pVolume, const char * sName, const void * p, int Mask) {
  FS_USE_PARA(pVolume);
  FS_USE_PARA(sName);
  FS_USE_PARA(p);
  FS_USE_PARA(Mask);
  return 0;
}

/*********************************************************************
*
*       FS_NONE_GetDirEntryInfo
*/
int FS_NONE_GetDirEntryInfo(FS_VOLUME * pVolume, const char * sName, void * p, int Mask) {
  FS_USE_PARA(pVolume);
  FS_USE_PARA(sName);
  FS_USE_PARA(p);
  FS_USE_PARA(Mask);
  return 0;
}

/*********************************************************************
*
*       FS_NONE_SetEndOfFile
*/
int FS_NONE_SetEndOfFile(FS_FILE * pFile) {
  FS_USE_PARA(pFile);
  return 0;
}

/*********************************************************************
*
*       FS_NONE_GetDiskInfo
*/
int FS_NONE_GetDiskInfo(FS_VOLUME * pVolume, FS_DISK_INFO * pDiskData, int Flags) {
  FS_USE_PARA(pVolume);
  FS_USE_PARA(pDiskData);
  FS_USE_PARA(Flags);
  return 0;
}

/*********************************************************************
*
*       FS_NONE_GetVolumeLabel
*/
int FS_NONE_GetVolumeLabel(FS_VOLUME * pVolume, char * pVolumeLabel, unsigned VolumeLabelSize) {
  FS_USE_PARA(pVolume);
  FS_USE_PARA(pVolumeLabel);
  FS_USE_PARA(VolumeLabelSize);
  return 0;
}

/*********************************************************************
*
*       FS_NONE_SetVolumeLabel
*/
int FS_NONE_SetVolumeLabel(FS_VOLUME * pVolume, const char * pVolumeLabel) {
  FS_USE_PARA(pVolume);
  FS_USE_PARA(pVolumeLabel);
  return 0;
}

/*********************************************************************
*
*       FS_NONE_CreateJournalFile
*/
int FS_NONE_CreateJournalFile(FS_VOLUME * pVolume, U32 NumBytes, U32 * pFirstSector, U32 * pNumSectors) {
  FS_USE_PARA(pVolume);
  FS_USE_PARA(NumBytes);
  FS_USE_PARA(pFirstSector);
  FS_USE_PARA(pNumSectors);
  return 0;
}

/*********************************************************************
*
*       FS_NONE_OpenJournalFile
*/
int FS_NONE_OpenJournalFile(FS_VOLUME * pVolume) {
  FS_USE_PARA(pVolume);
  return 0;
}

/*********************************************************************
*
*       FS_NONE_GetIndexOfLastSector
*/
U32 FS_NONE_GetIndexOfLastSector(FS_VOLUME * pVolume) {
  FS_USE_PARA(pVolume);
  return 0;
}

/*********************************************************************
*
*       FS_NONE_CheckVolume
*/
int FS_NONE_CheckVolume(FS_VOLUME * pVolume, void * pBuffer, U32 BufferSize, int MaxRecursionLevel, FS_CHECKDISK_ON_ERROR_CALLBACK * pfOnError) {
  FS_USE_PARA(pVolume);
  FS_USE_PARA(pBuffer);
  FS_USE_PARA(BufferSize);
  FS_USE_PARA(MaxRecursionLevel);
  FS_USE_PARA(pfOnError);
  return 0;
}

/*********************************************************************
*
*       FS_NONE_UpdateFile
*/
int FS_NONE_UpdateFile(FS_FILE * pFile) {
  FS_USE_PARA(pFile);
  return 0;
}

/*********************************************************************
*
*       FS_NONE_SetFileSize
*/
int FS_NONE_SetFileSize(FS_FILE * pFile, U32 NumBytes) {
  FS_USE_PARA(pFile);
  FS_USE_PARA(NumBytes);
  return 0;
}

/*********************************************************************
*
*       FS_NONE_FreeSectors
*/
int FS_NONE_FreeSectors(FS_VOLUME * pVolume) {
  FS_USE_PARA(pVolume);
  return 0;
}

/*********************************************************************
*
*       FS_NONE_GetFreeSpace
*/
int FS_NONE_GetFreeSpace(FS_VOLUME * pVolume, void * pBuffer, int SizeOfBuffer, U32 FirstClusterId, U32 * pNumClustersFree, U32 * pNumClustersChecked) {
  FS_USE_PARA(pVolume);
  FS_USE_PARA(pBuffer);
  FS_USE_PARA(SizeOfBuffer);
  FS_USE_PARA(FirstClusterId);
  FS_USE_PARA(pNumClustersFree);
  FS_USE_PARA(pNumClustersChecked);
  return 0;
}

/*********************************************************************
*
*       FS_NONE_GetATInfo
*/
int FS_NONE_GetATInfo(FS_VOLUME * pVolume, FS_AT_INFO * pATInfo) {
  FS_USE_PARA(pVolume);
  FS_USE_PARA(pATInfo);
  return 0;
}

/*********************************************************************
*
*       FS_NONE_CheckDir
*/
int FS_NONE_CheckDir(FS_VOLUME * pVolume, const char * sPath, FS_CLUSTER_MAP * pClusterMap, FS_CHECKDISK_ON_ERROR_CALLBACK * pfOnError) {
  FS_USE_PARA(pVolume);
  FS_USE_PARA(sPath);
  FS_USE_PARA(pClusterMap);
  FS_USE_PARA(pfOnError);
  return 0;
}

/*********************************************************************
*
*       FS_NONE_CheckAT
*/
int FS_NONE_CheckAT(FS_VOLUME * pVolume, const FS_CLUSTER_MAP * pClusterMap, FS_CHECKDISK_ON_ERROR_CALLBACK * pfOnError) {
  FS_USE_PARA(pVolume);
  FS_USE_PARA(pClusterMap);
  FS_USE_PARA(pfOnError);
  return 0;
}

/*********************************************************************
*
*       FS_NONE_ReadATEntry
*/
I32 FS_NONE_ReadATEntry(FS_VOLUME * pVolume, U32 ClusterId) {
  FS_USE_PARA(pVolume);
  FS_USE_PARA(ClusterId);
  return 0;
}

/*********************************************************************
*
*       FS_NONE_GetFSType
*/
int FS_NONE_GetFSType(const FS_VOLUME * pVolume) {
  FS_USE_PARA(pVolume);
  return 0;
}

/*********************************************************************
*
*       FS_NONE_SetDirEntryInfoEx
*/
int FS_NONE_SetDirEntryInfoEx(FS_VOLUME * pVolume, const FS_DIRENTRY_POS * pDirEntryPos, const void * p, int Mask) {
  FS_USE_PARA(pVolume);
  FS_USE_PARA(pDirEntryPos);
  FS_USE_PARA(p);
  FS_USE_PARA(Mask);
  return 0;
}

/*********************************************************************
*
*       FS_NONE_GetDirEntryInfoEx
*/
int FS_NONE_GetDirEntryInfoEx(FS_VOLUME * pVolume, const FS_DIRENTRY_POS * pDirEntryPos, void * p, int Mask) {
  FS_USE_PARA(pVolume);
  FS_USE_PARA(pDirEntryPos);
  FS_USE_PARA(p);
  FS_USE_PARA(Mask);
  return 0;
}

#endif // (FS_SUPPORT_FAT == 0) && (FS_SUPPORT_EFS == 0)

#if FS_SUPPORT_TEST

/*********************************************************************
*
*       FS__SetDiskAllocMode
*
*  Function description
*    Configures the free cluster search strategy.
*
*  Parameters
*    AllocMode    Specifies how to search for a free cluster:
*                 * DISK_ALLOC_MODE_FIRST_FREE  Start searching from
*                                               the last cluster allocated
*                                               to file or directory.
*                 * DISK_ALLOC_MODE_NEXT_FREE   Start searching from
*                                               the last cluster freed.
*                 * DISK_ALLOC_MODE_BEST_FREE   Start searching from
*                                               the last cluster allocated
*                                               and if the next cluster
*                                               in the row is not free
*                                               continue with the last
*                                               cluster freed.
*
*  Additional information
*    The default allocation mode is DISK_ALLOC_MODE_FIRST_FREE.
*    This allocation mode is designed to reduce the file fragmentation
*    by trying to allocate continuous ranges of clusters. In this mode,
*    the file system stars searching for a free cluster beginning from
*    the last cluster allocated to the file by checking in ascending
*    cluster order. Typically, an application is not required to use
*    a different file allocation type than DISK_ALLOC_MODE_FIRST_FREE.
*
*    In the DISK_ALLOC_MODE_NEXT_FREE allocation mode the file system
*    starts searching for a free cluster beginning with the next free
*    cluster known to the file system. This allocation mode can help
*    improve the write performance when files that grow larger with
*    the time such as in an application that collects data.
*/
void FS__SetDiskAllocMode(int AllocMode) {
  FS_LOCK();
  FS_LOCK_SYS();
  FS_Global.AllocMode = (U8)AllocMode;
  FS_UNLOCK_SYS();
  FS_UNLOCK();
}

/*********************************************************************
*
*       FS__GetDiskAllocMode
*
*  Function description
*    Returns the configured free cluster search strategy.
*
*  Return value
*    == DISK_ALLOC_MODE_FIRST_FREE  Start searching from
*                                   the last cluster allocated
*                                   to file or directory.
*    == DISK_ALLOC_MODE_NEXT_FREE   Start searching from
*                                   the last cluster freed.
*    == DISK_ALLOC_MODE_BEST_FREE   Start searching from
*                                   the last cluster allocated
*                                   and if the next cluster
*                                   in the row is not free
*                                   continue with the last
*                                   cluster freed.
*
*  Additional information
*    For more information see FS__SetDiskAllocMode().
*/
int FS__GetDiskAllocMode(void) {
  int r;

  FS_LOCK();
  FS_LOCK_SYS();
  r = (int)FS_Global.AllocMode;
  FS_UNLOCK_SYS();
  FS_UNLOCK();
  return r;
}

#endif // FS_SUPPORT_TEST

/*********************************************************************
*
*       FS__SaveContext
*/
void FS__SaveContext(FS_CONTEXT * pContext) {
  pContext->pfTimeDate                 = _pfTimeDate;
  pContext->pCharSetType               = FS_pCharSetType;
  pContext->STORAGE_Counters           = FS_STORAGE_Counters;
#if FS_SUPPRESS_EOF_ERROR
  pContext->IsEOFErrorSuppressed       = _IsEOFErrorSuppressed;
#endif  // FS_SUPPRESS_EOF_ERROR
#if FS_SUPPORT_POSIX
  pContext->IsPOSIXSupported           = FS_IsPOSIXSupported;
#endif // FS_SUPPORT_POSIX
#if FS_VERIFY_WRITE
  pContext->IsWriteVerificationEnabled = FS_IsWriteVerificationEnabled;
#endif // FS_SUPPORT_POSIX
#if FS_SUPPORT_EFS
  FS_EFS_Save(pContext);
#endif // FS_SUPPORT_EFS
#if FS_SUPPORT_FAT
  FS_FAT_Save(pContext);
#endif // FS_SUPPORT_FAT
#if FS_SUPPORT_JOURNAL
  FS__JOURNAL_Save(pContext);
#endif // FS_SUPPORT_JOURNAL
  FS_PRNG_Save(pContext);
}

/*********************************************************************
*
*       FS__RestoreContext
*/
void FS__RestoreContext(const FS_CONTEXT * pContext) {
  _pfTimeDate                   = pContext->pfTimeDate;
  FS_pCharSetType               = pContext->pCharSetType;
  FS_STORAGE_Counters           = pContext->STORAGE_Counters;
#if FS_SUPPRESS_EOF_ERROR
  _IsEOFErrorSuppressed         = pContext->IsEOFErrorSuppressed;
#endif  // FS_SUPPRESS_EOF_ERROR
#if FS_SUPPORT_POSIX
  FS_IsPOSIXSupported           = pContext->IsPOSIXSupported;
#endif // FS_SUPPORT_POSIX
#if FS_VERIFY_WRITE
  FS_IsWriteVerificationEnabled = pContext->IsWriteVerificationEnabled;
#endif // FS_SUPPORT_POSIX
#if FS_SUPPORT_EFS
  FS_EFS_Restore(pContext);
#endif // FS_SUPPORT_EFS
#if FS_SUPPORT_FAT
  FS_FAT_Restore(pContext);
#endif // FS_SUPPORT_FAT
#if FS_SUPPORT_JOURNAL
  FS__JOURNAL_Restore(pContext);
#endif // FS_SUPPORT_JOURNAL
  FS_PRNG_Restore(pContext);
}

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_FOpen
*
*  Function description
*    Opens an existing file or creates a new one.
*
*  Parameters
*    sFileName    Name of the file to be opened or created.
*                 It is a 0-terminated string that cannot be NULL.
*    sMode        Indicates how the file should be opened.
*                 It is a 0-terminated string that cannot be NULL.
*
*  Return value
*    !=0    OK, pointer to a file handle that identifies
*           the opened file.
*    ==0    Error, unable to open the file.
*
*  Additional information
*    The sMode parameter can take one of the following values:
*    +----------------------+-------------------------------------------------------------------+
*    | sMode                | Description                                                       |
*    +----------------------+-------------------------------------------------------------------+
*    | "r" or "rb"          | Open files for reading.                                           |
*    +----------------------+-------------------------------------------------------------------+
*    | "w" or "wb"          | Truncates to zero length or creates file for writing.             |
*    +----------------------+-------------------------------------------------------------------+
*    | "a" or "ab"          | Appends; opens / creates file for writing at end of file.         |
*    +----------------------+-------------------------------------------------------------------+
*    | "r+", "r+b" or "rb+" | Opens file for update (reading and writing).                      |
*    +----------------------+-------------------------------------------------------------------+
*    | "w+", "w+b" or "wb+" | Truncates to zero length or creates file for update.              |
*    +----------------------+-------------------------------------------------------------------+
*    | "a+", "a+b" or "ab+" | Appends; opens / creates file for update, writing at end of file. |
*    +----------------------+-------------------------------------------------------------------+
*
*    For more details about FS_FOpen(), refer to the ANSI C
*    documentation of the fopen() function.
*
*    The file system does not distinguish between binary and text mode;
*    the files are always accessed in binary mode.
*
*    In order to use long file names with FAT, the FS_FAT_SupportLFN()
*    has to be called before after the file system is initialized.
*
*    FS_FOpen() accepts file names encoded in UTF-8 format. This feature
*    is disabled by default. To enable it the file system has to be compiled
*    with the FS_FAT_SUPPORT_UTF8 configuration define to 1. Additionally,
*    the support for long file names has to be enabled for volumes
*    formatted as FAT.
*/
FS_FILE * FS_FOpen(const char * sFileName, const char * sMode) {
  FS_FILE * pFile;

  pFile = NULL;
  FS_LOCK();
  FS_PROFILE_CALL_STRINGx2(FS_EVTID_FOPEN, sFileName, sMode);
  (void)FS__FOpenEx(sFileName, sMode, &pFile);
  FS_PROFILE_END_CALL_U32(FS_EVTID_FOPEN, SEGGER_PTR2ADDR(pFile));              // MISRA deviation D:100[b]
  FS_UNLOCK();
  return pFile;
}

/*********************************************************************
*
*       FS_FOpenEx
*
*  Function description
*    Opens an existing file or creates a new one.
*
*  Parameters
*    sFileName    Name of the file to be opened or created.
*                 It is a 0-terminated string that cannot be NULL.
*    sMode        Indicates how the file should be opened.
*                 It is a 0-terminated string that cannot be NULL.
*    ppFile       Pointer to a file handle pointer that receives the
*                 opened file handle.
*
*  Return value
*    ==0      OK, file opened.
*    !=0      Error code indicating the failure reason.
*
*  Additional information
*    For additional information about the sMode parameter refer
*    to FS_FOpen().
*/
int FS_FOpenEx(const char * sFileName, const char * sMode, FS_FILE ** ppFile) {
  int r;

  FS_LOCK();
  FS_PROFILE_CALL_STRINGx2(FS_EVTID_FOPEN, sFileName, sMode);
  r = FS__FOpenEx(sFileName, sMode, ppFile);
#if (FS_SUPPORT_PROFILE != 0) && (FS_SUPPORT_PROFILE_END_CALL != 0)
  {
    U32 File;

    File = 0;
    if (ppFile != NULL) {
      File = (U32)SEGGER_PTR2ADDR(*ppFile);                                     // MISRA deviation D:100[b]
    }
    FS_PROFILE_END_CALL_U32(FS_EVTID_FOPEN, File);
  }
#endif // (FS_SUPPORT_PROFILE != 0) && (FS_SUPPORT_PROFILE_END_CALL != 0)
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_Remove
*
*  Function description
*    Removes a file.
*
*  Parameters
*    sFileName    Name of the file to be removed.
*
*  Return value
*    ==0      OK, file has been removed.
*    !=0      Error code indicating the failure reason.
*
*  Additional information
*    The function removes also files that have the FS_ATTR_READ_ONLY
*    attribute set. The remove operation fails if the file to be
*    deleted is open.
*/
int FS_Remove(const char * sFileName) {
  int r;

  FS_LOCK();
  r = FS__Remove(sFileName);
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_FClose
*
*  Function description
*    Closes an opened file.
*
*  Parameters
*    pFile      Handle to the file to be closed.
*
*  Return value
*    ==0      OK, file handle has been successfully closed.
*    !=0      Error code indicating the failure reason.
*             Refer to FS_ErrorNo2Text().
*/
int FS_FClose(FS_FILE * pFile) {
  int r;

  FS_LOCK();
  FS_PROFILE_CALL_U32(FS_EVTID_FCLOSE, SEGGER_PTR2ADDR(pFile));                 // MISRA deviation D:100[e]
  r = FS__FClose(pFile);
  FS_PROFILE_END_CALL(FS_EVTID_FCLOSE);
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_SyncFile
*
*  Function description
*    Synchronizes file to storage device.
*
*  Parameters
*    pFile    File pointer identifying the opened file.
*             It can be NULL.
*
*  Return value
*    ==0      OK, file(s) synchronized.
*    !=0      Error code indicating the failure reason.
*
*  Additional information
*    The function synchronizes all the opened files if a NULL is
*    passed as pFile parameter.
*
*    FS_SyncFile() cleans the write buffer if configured and updates
*    the management information to storage device. The function
*    performs basically the same operations as FS_FClose() with the
*    only difference that it leaves the file open. FS_SyncFile() is
*    used typically with fast or medium file write modes to make
*    sure that the data cached by the file system for the file is
*    written to storage medium.
*
*    The function can also be called from a different task than the
*    one that writes to file if the support for multi-tasking is
*    enabled in the file system. The support for multi-tasking can
*    be enabled by compiling the file system sources with the
*    FS_OS_LOCKING define is set to value different than 0.
*/
int FS_SyncFile(FS_FILE * pFile) {
  int       r;
  int       SyncResult;
  FS_FILE * pFileToSync;
  int       InUse;

  FS_LOCK();
  if (pFile != NULL) {
    r = FS__SyncFile(pFile);
  } else {
    r = FS_ERRCODE_OK;        // No error so far.
    pFile = FS_Global.pFirstFileHandle;
    //
    // Loop through all opened files an synchronize them.
    //
    while (pFile != NULL) {
      //
      // System lock is required to make sure that the file handle is properly initialized.
      //
      InUse = 0;
      FS_LOCK_SYS();
      if (pFile->InUse != 0u) {
        InUse = 1;
      }
      pFileToSync = pFile;
      pFile       = pFile->pNext;
      FS_UNLOCK_SYS();
      if (InUse != 0) {
        SyncResult = FS__SyncFile(pFileToSync);
        if (SyncResult != 0) {
          r = SyncResult;
        }
      }
    }
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_FSeek
*
*  Function description
*    Sets the current position in file.
*
*  Parameters
*    pFile      Handle to opened file.
*    Offset     Byte offset for setting the file pointer position.
*    Origin     Indicates how the file pointer has to be moved.
*
*  Return value
*    ==0      OK, file pointer has been positioned according
*             to the specified parameters.
*    !=0      Error code indicating the failure reason.
*
*  Additional information
*    FS_FSeek() moves the file pointer to a new location by a number
*    of bytes relative to the position specified by the Origin
*    parameter. The Origin parameter can take the values specified
*    by the \tt FS_SEEK_... defines
*
*    The file pointer can be repositioned anywhere in the file.
*    It is also possible to reposition the file pointer beyond
*    the end of the file. This feature is used together with
*    FS_SetEndOfFile() to reserve space for a file
*    (preallocate the file).
*/
int FS_FSeek(FS_FILE * pFile, FS_FILE_OFF Offset, int Origin) {
  int r;

  FS_LOCK();
  FS_PROFILE_CALL_U32x3(FS_EVTID_FSEEK, SEGGER_PTR2ADDR(pFile), Offset, Origin);      // MISRA deviation D:100[b]
  r = FS__FSeek(pFile, Offset, Origin);
  FS_PROFILE_END_CALL_U32(FS_EVTID_FSEEK, r);
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_FTell
*
*  Function description
*    Returns current position in file.
*
*  Parameters
*    pFile      Handle to opened file.
*
*  Return value
*    != 0xFFFFFFFF  OK, current position of the file pointer.
*    == 0xFFFFFFFF  An error occurred.
*
*  Additional information
*    The function returns the file position as a signed value
*    for compatibility reasons. The return value has to be
*    treated as a 32-bit unsigned with the value 0xFFFFFFFF
*    indicating an error.
*
*    This function simply returns the file pointer member of the
*    file handle structure pointed by pFile. Nevertheless, you should
*    not access the FS_FILE structure yourself, because that data
*    structure may change in the future.
*
*    In conjunction with FS_FSeek(), this function can also be used
*    to examine the file size. By setting the file pointer to the end
*    of the file using FS_SEEK_END, the length of the file can now be
*    retrieved by calling FS_FTell(). Alternatively the FS_GetFileSize()
*    function can be used.
*/
FS_FILE_OFF FS_FTell(FS_FILE * pFile) {
  FS_FILE_OFF r;

  FS_LOCK();
  r = FS__FTell(pFile);
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_FEof
*
*  Function description
*    Returns if end of file has been reached.
*
*  Parameters
*    pFile      Handle to opened file.
*
*  Return value
*    ==1        An attempt was made to read beyond the end of the file.
*    ==0        The end of file has not been reached.
*    < 0        An error occurred.
*
*  Additional information
*    The end-of-file flag of the file handle is set by the file system
*    when the application tries to read more bytes than available in
*    the file. This is not an error condition but just an indication
*    that the file pointer is positioned beyond the last byte in the
*    file and that by trying to read from this position no bytes are
*    returned.
*/
int FS_FEof(FS_FILE * pFile) {
  int r;

  FS_LOCK();
  r = FS__FEof(pFile);
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_FError
*
*  Function description
*    Return error status of a file handle.
*
*  Parameters
*    pFile      Handle to opened file.
*
*  Return value
*    ==FS_ERRCODE_OK    No error present.
*    !=FS_ERRCODE_OK    An error has occurred.
*
*  Additional information
*    The application can use this function to check for example
*    what kind of error caused the call to FS_Read(), FS_FRead(),
*    FS_Write() or FS_FWrite() to fail. These functions do not
*    return an error code but the number of byte read or written.
*    The error status remains set until the application calls
*    FS_ClearErr().
*
*    FS_ErrorNo2Text() can be used to return a human-readable
*    description of the error as a 0-terminated string.
*/
I16 FS_FError(FS_FILE * pFile) {
  I16 r;

  FS_LOCK();
  r = FS__FError(pFile);
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_ClearErr
*
*  Function description
*    Clears error status of a file handle.
*
*  Parameters
*    pFile    Handle to opened file.
*
*  Additional information
*    The function sets the error status of a file handle to
*    FS_ERRCODE_OK. FS_ClearErr() is the only function that clears
*    the error status of a file handle. The other API functions modify
*    the error status of a file handle only if it is cleared.
*    The application has to call this function after it detects that
*    an error occurred during a file system operation on an opened
*    file handle and before it starts a new file system operation
*    if it wants to get the correct error status in case of a failure.
*/
void FS_ClearErr(FS_FILE * pFile) {
  FS_LOCK();
  if (pFile != NULL) {
    FS_LOCK_SYS();
    pFile->Error = FS_ERRCODE_OK;
    FS_UNLOCK_SYS();
  }
  FS_UNLOCK();
}

/*********************************************************************
*
*       FS_Init
*
*  Function description
*    Starts the file system.
*
*  Additional information
*    FS_Init() initializes the file system and creates resources
*    required for the access of the storage device in
*    a multi-tasking environment. This function has to be called
*    before any other file system API function.
*/
void FS_Init(void) {
  unsigned         NumDriverLocks;
  U8             * pBuffer;
  FS_DATA_BUFFER * pDesc;
  FS_DATA_BUFFER * paDesc;
  unsigned         i;
  unsigned         NumDesc;
  unsigned         NumBytesBuffer;

  //
  // Check whether the file system has been already initialized. If so, generate a warning.
  //
  if (FS_Global.IsInited != 0u) {
    FS_DEBUG_WARN((FS_MTYPE_API, "FS_Init: File system already initialized."));
  }
  //
  // Allocate memory for sector buffers.
  //
  NumDriverLocks = FS_STORAGE_Init();
  if (NumDriverLocks != 0u) {
    NumBytesBuffer = FS_Global.MaxSectorSize;
    NumDesc        = FS_NUM_SECTOR_BUFFERS_PER_OPERATION * NumDriverLocks;
    //
    // Allocate memory for the internal sector buffers.
    //
    paDesc  = SEGGER_PTR2PTR(FS_DATA_BUFFER, FS_ALLOC_ZEROED((I32)NumDesc * (I32)sizeof(FS_DATA_BUFFER), "SECTOR_BUFFER"));               // MISRA deviation D:100[d]
    pBuffer = SEGGER_PTR2PTR(U8, FS_ALLOC_ZEROED((I32)NumDesc * (I32)NumBytesBuffer, "SECTOR_BUFFER_DATA"));                              // MISRA deviation D:100[d]
    if ((paDesc != NULL) && (pBuffer != NULL)) {
      pDesc = paDesc;
      for (i = 0; i < NumDesc; i++) {
        pDesc->pBuffer  = SEGGER_PTR2PTR(U32, pBuffer);                                                                                   // MISRA deviation D:100[e]
        pBuffer        += NumBytesBuffer;
        pDesc++;
      }
    }
    FS_Global.NumSectorBuffers = (U8)NumDesc;
    FS_Global.paSectorBuffer   = paDesc;
    FS_Global.pFirstFileHandle = SEGGER_PTR2PTR(FS_FILE, FS_ALLOC_ZEROED((I32)sizeof(FS_FILE), "FS_FILE"));                               // MISRA deviation D:100[d]
    FS_Global.pFirstFileObj    = SEGGER_PTR2PTR(FS_FILE_OBJ, FS_ALLOC_ZEROED((I32)sizeof(FS_FILE_OBJ), "FS_FILE_OBJ"));                   // MISRA deviation D:100[d]
#if ((FS_SUPPORT_EFS != 0) && (FS_EFS_SUPPORT_DIRENTRY_BUFFERS != 0))
    {
      //
      // Allocate memory for the buffers used to store directory entries.
      //
      NumBytesBuffer = (unsigned)FS_EFS_MAX_DIR_ENTRY_SIZE + 1u;          // One additional byte is required to store the end of directory marker.
      NumDesc        = (unsigned)FS_EFS_NUM_DIRENTRY_BUFFERS * NumDriverLocks;
      paDesc         = SEGGER_PTR2PTR(FS_DATA_BUFFER, FS_ALLOC_ZEROED((I32)NumDesc * (I32)sizeof(FS_DATA_BUFFER), "DIR_ENTRY_BUFFER"));   // MISRA deviation D:100[d]
      pBuffer        = SEGGER_PTR2PTR(U8, FS_ALLOC_ZEROED((I32)NumDesc * (I32)NumBytesBuffer, "DIR_ENTRY_BUFFER_DATA"));                  // MISRA deviation D:100[d]
      if ((paDesc != NULL) && (pBuffer != NULL)) {
        pDesc = paDesc;
        for (i = 0; i < NumDesc; i++) {
          pDesc->pBuffer  = SEGGER_PTR2PTR(U32, pBuffer);                                                                                 // MISRA deviation D:100[e]
          pBuffer        += NumBytesBuffer;
          pDesc++;
        }
      }
      FS_Global.NumDirEntryBuffers = (U8)NumDesc;
      FS_Global.paDirEntryBuffer   = paDesc;
    }
#endif
    FS_Global.IsInited = 1;
  }
  FS_PROFILE_END_CALL(FS_EVTID_INIT);
}

/*********************************************************************
*
*       FS_ConfigOnWriteDirUpdate
*
*  Function description
*    Configures if the directory entry has be updated after writing
*    to file.
*
*  Parameters
*    OnOff    Specifies if the feature has to be enabled or disabled.
*             * 1   Enable update directory after write.
*             * 0   Do not update directory. FS_FClose() updates
*                   the directory entry.
*/
void FS_ConfigOnWriteDirUpdate(char OnOff) {
  FS_LOCK();
  FS_LOCK_SYS();
  FS_PROFILE_CALL_U32(FS_EVTID_CONFIGONWRITEDIRUPDATE, (U8)OnOff);
  if (OnOff != '\0') {
    FS_Global.WriteMode = FS_WRITEMODE_SAFE;
  } else {
    FS_Global.WriteMode = FS_WRITEMODE_MEDIUM;
  }
  FS_PROFILE_END_CALL(FS_EVTID_CONFIGONWRITEDIRUPDATE);
  FS_UNLOCK_SYS();
  FS_UNLOCK();
}

#if FS_SUPPORT_FILE_BUFFER

/*********************************************************************
*
*       FS_ConfigFileBufferDefault
*
*  Function description
*    Configures the size and flags for the file buffer.
*
*  Parameters
*    BufferSize     Size of the file buffer in bytes.
*    Flags          File buffer operating mode.
*                   * 0                       Read file buffer.
*                   * FS_FILE_BUFFER_WRITE    Read / write file buffer.
*                   * FS_FILE_BUFFER_ALIGNED  Logical sector boundary alignment.
*
*  Return value
*    ==0      OK, the file buffer has been configured.
*    !=0      Error code indicating the failure reason.
*
*  Additional information
*    The function has to be called only once, in FS_X_AddDevices().
*    If called after FS_Init() the function does nothing and generates
*    a warning.
*
*    The file system allocates a file buffer of BufferSize bytes for
*    each file the application opens. The operating mode of the file
*    buffer can be changed at runtime via FS_SetFileBufferFlags().
*    If file buffers of different sizes are required
*    FS_SetFileBuffer() should be used instead.
*
*    For best performance it is recommended to set the size of the
*    file buffer to be equal to the size of the logical sector.
*    Smaller file buffer sizes can also be used to reduce the RAM
*    usage.
*
*    FS_SetFileBuffer() is available if the emFile sources are
*    compiled with the FS_SUPPORT_FILE_BUFFER configuration
*    define set to 1.
*/
int FS_ConfigFileBufferDefault(int BufferSize, int Flags) {
  if (FS_Global.IsInited != 0u) {
    FS_DEBUG_WARN((FS_MTYPE_API, "FS_ConfigFileBufferDefault: Can be called only before FS_Init() or in FS_X_AddDevices()."));
    return FS_ERRCODE_INVALID_USAGE;
  }
  FS_LOCK();
  FS_LOCK_SYS();
  FS_Global.FileBufferSize  = (U32)BufferSize;
  FS_Global.FileBufferFlags = (U8)Flags;
  FS_UNLOCK_SYS();
  FS_UNLOCK();
  return 0;
}

/*********************************************************************
*
*       FS_SetFileBufferFlags
*
*  Function description
*    Changes the operating mode of the file buffer.
*
*  Parameters
*    pFile          Handle to opened file.
*    Flags          File buffer operating mode.
*                   * 0                       Read file buffer.
*                   * FS_FILE_BUFFER_WRITE    Read / write file buffer.
*                   * FS_FILE_BUFFER_ALIGNED  Logical sector boundary alignment.
*
*  Return value
*    ==0    OK, file buffer flags changed.
*    !=0    Error code indicating the failure reason.
*
*  Additional information
*    This function can only be called immediately after FS_FOpen(),
*    to change the operating mode of the file buffer (read or
*    read / write).
*
*    FS_SetFileBufferFlags() is available if the emFile sources are
*    compiled with the FS_SUPPORT_FILE_BUFFER configuration define set to 1.
*/
int FS_SetFileBufferFlags(FS_FILE * pFile, int Flags) {
  int r;

  FS_LOCK();
  r = FS__SetFileBufferFlags(pFile, Flags);
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_SetFileBufferFlagsEx
*
*  Function description
*    Changes the operating mode of the file buffer.
*
*  Parameters
*    sVolumeName    Name of the volume for which to set the flags.
*    Flags          File buffer operating mode.
*                   * 0                       Read file buffer.
*                   * FS_FILE_BUFFER_WRITE    Read / write file buffer.
*                   * FS_FILE_BUFFER_ALIGNED  Logical sector boundary alignment.
*
*  Additional information
*    This function can be used to change the operating mode of
*    the file buffer for the files that are located on a specific
*    volume.
*
*    FS_SetFileBufferFlagsEx() is available if the emFile sources are
*    compiled with the FS_SUPPORT_FILE_BUFFER configuration define
*    set to 1.
*
*  Return value
*    ==0    OK, operating mode changed.
*    !=0    Error code indicating the failure reason.
*/
int FS_SetFileBufferFlagsEx(const char * sVolumeName, int Flags) {
  FS_VOLUME * pVolume;
  int         r;

  r = FS_ERRCODE_INVALID_PARA;
  FS_LOCK();
  if (sVolumeName != NULL) {
    pVolume = FS__FindVolume(sVolumeName);
    if (pVolume != NULL) {
      FS_LOCK_DRIVER(&pVolume->Partition.Device);
      pVolume->FileBufferFlags = (U8)Flags | (U8)FILE_BUFFER_FLAGS_VALID;
      FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
      r = FS_ERRCODE_OK;
    } else {
      r = FS_ERRCODE_VOLUME_NOT_FOUND;
    }
  }
  FS_UNLOCK();
  return r;
}

#endif  // FS_SUPPORT_FILE_BUFFER

/*********************************************************************
*
*       FS_SetFileWriteMode
*
*  Function description
*    Configures the file write mode.
*
*  Parameters
*    WriteMode    Specifies how to write to file:
*                 * FS_WRITEMODE_SAFE     Updates the allocation table
*                                         and the directory entry
*                                         at each write to file operation.
*                 * FS_WRITEMODE_MEDIUM   Updates the allocation table
*                                         at each write to file operation.
*                 * FS_WRITEMODE_FAST     The allocation table and
*                                         directory entry are updated
*                                         when the file is closed.
*
*  Additional information
*   This function can be called to configure which mode the file system
*   has to use when writing to a file. The file system uses by default
*   FS_WRITEMODE_SAFE which allows the maximum fail-safe behavior,
*   since the allocation table and the directory entry is updated on
*   every write operation to file.
*
*   If FS_WRITEMODE_FAST is set, the update of the allocation table
*   is performed using a special algorithm. When writing to the file
*   for the first time, the file system checks how many clusters in series
*   are empty starting with the first one occupied by the file.
*   This cluster chain is remembered, so that if the file grows and
*   needs an additional cluster, the allocation doesn't have to be
*   read again in order to find the next free cluster. The allocation table
*   is only modified if necessary, which is the case when:
*   * All clusters of the cached free-cluster-chain are occupied.
*   * The volume or the file is synchronized that is when FS_Sync(), FS_FClose(),
*     or FS_SyncFile() is called.
*   * A different file is written.
*   Especially when writing large amounts of data, FS_WRITEMODE_FAST allows
*   maximum performance, since usually the file system has to search for
*   a free cluster in the allocation table and link it with the last one
*   occupied by the file. In worst case, multiple sectors of the allocation
*   table have to be read in order to find a free cluster.
*/
void FS_SetFileWriteMode(FS_WRITEMODE WriteMode) {
  FS_LOCK();
  FS_LOCK_SYS();
  FS_Global.WriteMode = WriteMode;
  FS_UNLOCK_SYS();
  FS_UNLOCK();
}

/*********************************************************************
*
*       FS_SetFileWriteModeEx
*
*  Function description
*    Configures the write mode of a specified volume.
*
*  Parameters
*    WriteMode    Specifies how to write to file:
*                 * FS_WRITEMODE_SAFE     Updates the allocation table
*                                         and the directory entry
*                                         at each write to file operation.
*                 * FS_WRITEMODE_MEDIUM   Updates the allocation table
*                                         at each write to file operation.
*                 * FS_WRITEMODE_FAST     The allocation table and
*                                         directory entry are updated
*                                         when the file is closed.
*    sVolumeName  Identifies the volume for which the write mode has
*                 to be changed.
*
*  Return value
*    ==0      OK, write mode set.
*    !=0      Error code inidicating the failure reason.
*
*  Additional information
*    When not explicitly set using this function the write mode of a
*    volume is the write mode set via FS_SetFileWriteMode() or the
*    default write mode (FS_WRITEMODE_SAFE). FS_SetFileWriteModeEx()
*    is typically on file system configurations using multiple volumes
*    that require different write modes. For example on a file system
*    configured to use two volumes where one volume has to be configured
*    for maximum write performance (FS_WRITEMODE_FAST) while on the other
*    volume the write operation has to be fail-safe (FS_WRITEMODE_SAFE).
*
*    Refer to FS_SetFileWriteMode() for detailed information about
*    the different write modes.
*/
int FS_SetFileWriteModeEx(FS_WRITEMODE WriteMode, const char * sVolumeName) {
  FS_VOLUME * pVolume;
  int         r;

  r = FS_ERRCODE_VOLUME_NOT_FOUND;
  FS_LOCK();
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    FS_LOCK_SYS();
    pVolume->WriteMode = WriteMode;
    FS_UNLOCK_SYS();
    r = 0;
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_GetFileWriteMode
*
*  Function description
*    Returns the write mode.
*
*  Return value
*    WriteMode    Specifies how to write to file:
*                 * FS_WRITEMODE_SAFE     Updates the allocation table
*                                         and the directory entry
*                                         at each write to file operation.
*                 * FS_WRITEMODE_MEDIUM   Updates the allocation table
*                                         at each write to file operation.
*                 * FS_WRITEMODE_FAST     The allocation table and
*                                         directory entry are updated
*                                         when the file is closed.
*                 * FS_WRITEMODE_UNKNOWN  An error occurred.
*
*  Additional information
*    This function can be used to query the write mode configured
*    for the entire file system. The write mode for the entire file
*    system can be configured via FS_SetFileWriteMode().
*    The write mode is set by default to FS_WRITEMODE_SAFE when
*    the file system is initialized.
*
*    Refer to FS_SetFileWriteMode() for detailed information about
*    the different write modes.
*/
FS_WRITEMODE FS_GetFileWriteMode(void) {
  FS_WRITEMODE WriteMode;

  FS_LOCK();
  FS_LOCK_SYS();
  WriteMode = FS_Global.WriteMode;
  FS_UNLOCK_SYS();
  FS_UNLOCK();
  return WriteMode;
}

/*********************************************************************
*
*       FS_GetFileWriteModeEx
*
*  Function description
*    Returns the write mode configured for a specified volume.
*
*  Parameters
*    sVolumeName  Identifies the volume that have to be queried.
*
*  Return value
*    WriteMode    Specifies how to write to file:
*                 * FS_WRITEMODE_SAFE     Updates the allocation table
*                                         and the directory entry
*                                         at each write to file operation.
*                 * FS_WRITEMODE_MEDIUM   Updates the allocation table
*                                         at each write to file operation.
*                 * FS_WRITEMODE_FAST     The allocation table and
*                                         directory entry are updated
*                                         when the file is closed.
*                 * FS_WRITEMODE_UNKNOWN  An error occurred.
*
*  Additional information
*    This function can be used to query the write mode configured
*    for the specified volume. The write mode of the volume can be
*    configured via FS_SetFileWriteModeEx(). If the write mode is not
*    explicitly configured by the application for the volume
*    via FS_SetFileWriteModeEx() then the write mode configured
*    for the entire file system is returned. The write mode for
*    the entire file system can be configured via FS_SetFileWriteMode().
*    The write mode is set by default to FS_WRITEMODE_SAFE when
*    the file system is initialized.
*
*    Refer to FS_SetFileWriteMode() for detailed information about
*    the different write modes.
*/
FS_WRITEMODE FS_GetFileWriteModeEx(const char * sVolumeName) {
  FS_VOLUME    * pVolume;
  FS_WRITEMODE   WriteMode;

  WriteMode = FS_WRITEMODE_UNKNOWN;
  FS_LOCK();
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    FS_LOCK_SYS();
    WriteMode = pVolume->WriteMode;
    if (WriteMode == FS_WRITEMODE_UNKNOWN)  {   // Not explicitly set for the volume?
      WriteMode = FS_Global.WriteMode;
    }
    FS_UNLOCK_SYS();
  }
  FS_UNLOCK();
  return WriteMode;
}

#if FS_SUPPORT_ENCRYPTION

/*********************************************************************
*
*       FS_SetEncryptionObject
*
*  Function description
*    Assigns an encryption object to a file handle.
*
*  Parameters
*    pFile        Handle to opened file.
*    pCryptObj    Instance of the object to be initialized.
*
*  Return value
*    ==0    OK, configured the object to be used for file encryption.
*    !=0    Error code indicating the failure reason.
*
*  Additional information
*    This function has to be called once immediately after the file
*    has been opened and before any other operation on that file.
*    The pointer to encryption object is saved internally by the file
*    system to the file handle. This means that the memory it points
*    to has to remain valid until the file is closed or
*    until the FS_SetEncryptionObject() is called for the same file
*    handle with pCryptObj set to NULL.
*
*    The encryption object can be initialized using FS_CRYPT_Prepare().
*/
int FS_SetEncryptionObject(FS_FILE * pFile, FS_CRYPT_OBJ * pCryptObj) {
  int r;

  FS_LOCK();
  r = FS__SetEncryptionObject(pFile, pCryptObj);
  FS_UNLOCK();
  return r;
}

#endif // FS_SUPPORT_ENCRYPTION

/*********************************************************************
*
*       FS_WipeFile
*
*  Function description
*    Overwrites the contents of a file with random data.
*
*  Parameters
*    sFileName    Name of the file to overwrite.
*
*  Return value
*    ==0    Contents of the file overwritten.
*    !=0    Error code indicating the failure reason.
*
*  Additional information
*    When a file is removed, the file system marks the corresponding
*    directory entry and the clusters in the allocation table as free.
*    The contents of the file is not modified and it can be in theory
*    restored by using a disk recovery tool. This can be a problem if
*    the file stores sensitive data. Calling FS_WipeFile() before
*    the file is removed makes the recovery of data impossible.
*
*    This function allocates FS_BUFFER_SIZE_FILE_WIPE bytes on the stack.
*/
int FS_WipeFile(const char * sFileName) {
  int r;

  FS_LOCK();
  r = _WipeFile(sFileName);
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_GetFileId
*
*  Function description
*    Calculates a value that uniquely identifies a file.
*
*  Parameters
*    sFileName    Name of the file for which to calculate the id.
*    pId          A 16-byte array where the id is stored.
*
*  Return value
*    ==0    Id returned OK.
*    !=0    Error code indicating the failure reason.
*
*  Additional information
*    The calculated value is a combination of the sector number that
*    stores the directory entry assigned to file combined with the
*    index of the directory index. Typically used by USB MTP component
*    of SEGGER emUSB-Device to create an unique object id to the file.
*/
int FS_GetFileId(const char * sFileName, U8 * pId) {
  int r;

  FS_LOCK();
  r = _GetFileId(sFileName, pId);
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_GetVersion
*
*  Function description
*    Returns the version number of the file system.
*
*  Return value
*    Version number.
*
*  Additional information
*    The version is formatted as follows:
*      \tt Mmmrr
*
*    where:
*      * \tt M    the major version number
*      * \tt mm   the minor version number
*      * \tt rr   the revision number
*
*    For example 40201 represents the version 4.02a (major version: 4,
*    minor version: 2, revision: a)
*/
U32 FS_GetVersion(void) {
  return FS_VERSION;
}

/*********************************************************************
*
*       FS_CONF_GetMaxPath
*
*  Function description
*    Returns the configured maximum number of characters in a path to a file or directory.
*
*  Return value
*    Value of FS_MAX_PATH configuration define.
*/
int FS_CONF_GetMaxPath(void) {
  return FS_MAX_PATH;
}

/*********************************************************************
*
*       FS_CONF_IsFATSupported
*
*  Function description
*    Checks if the file system is configured to support the FAT file system.
*
*  Return value
*    Value of FS_SUPPORT_FAT configuration define.
*/
int FS_CONF_IsFATSupported(void) {
  return FS_SUPPORT_FAT;
}

/*********************************************************************
*
*       FS_CONF_IsEFSSupported
*
*  Function description
*    Checks if the file system is configured to support the EFS file system.
*
*  Return value
*    Value of FS_SUPPORT_EFS configuration define.
*/
int FS_CONF_IsEFSSupported(void) {
  return FS_SUPPORT_EFS;
}

/*********************************************************************
*
*       FS_CONF_IsFreeSectorSupported
*
*  Function description
*    Checks if the file system is configured to support the "free sector" command.
*
*  Return value
*    Value of FS_SUPPORT_FREE_SECTOR configuration define.
*/
int FS_CONF_IsFreeSectorSupported(void) {
  return FS_SUPPORT_FREE_SECTOR;
}

/*********************************************************************
*
*       FS_CONF_IsCacheSupported
*
*  Function description
*    Checks if the file system is configured to support the sector cache.
*
*  Return value
*    Value of FS_SUPPORT_CACHE configuration define.
*
*  Additional information
*    This function does not check if the sector cache is actually active.
*    It only indicates if the file system has been compiled with support
*    for sector cache. The sector cache has to be activated via
*    FS_AssignCache().
*/
int FS_CONF_IsCacheSupported(void) {
  return FS_SUPPORT_CACHE;
}

/*********************************************************************
*
*       FS_CONF_IsEncryptionSupported
*
*  Function description
*    Checks if the file system is configured to support encryption.
*
*  Return value
*    Value of FS_SUPPORT_ENCRYPTION configuration define.
*/
int FS_CONF_IsEncryptionSupported(void) {
  return FS_SUPPORT_ENCRYPTION;
}

/*********************************************************************
*
*       FS_CONF_IsJournalSupported
*
*  Function description
*    Checks if the file system is configured to support journaling.
*
*  Return value
*    Value of FS_SUPPORT_JOURNAL configuration define.
*/
int FS_CONF_IsJournalSupported(void) {
  return FS_SUPPORT_JOURNAL;
}

/*********************************************************************
*
*       FS_CONF_GetDirectoryDelimiter
*
*  Function description
*    Returns the character that is configured as delimiter between
*    the directory names in a file path.
*
*  Return value
*    Value of FS_DIRECTORY_DELIMITER configuration define.
*/
char FS_CONF_GetDirectoryDelimiter(void) {
  return FS_DIRECTORY_DELIMITER;
}

/*********************************************************************
*
*       FS_CONF_IsDeInitSupported
*
*  Function description
*    Checks if the file system is configured to support deinitialization.
*
*  Return value
*    Value of FS_SUPPORT_DEINIT configuration define.
*/
int FS_CONF_IsDeInitSupported(void) {
  return FS_SUPPORT_DEINIT;
}

/*********************************************************************
*
*       FS_CONF_GetOSLocking
*
*  Function description
*    Returns the type of task locking configured for the file system.
*
*  Return value
*    Value of FS_OS_LOCKING configuration define.
*/
int FS_CONF_GetOSLocking(void) {
  return FS_OS_LOCKING;
}

/*********************************************************************
*
*       FS_CONF_GetNumVolumes
*
*  Function description
*    Returns the maximum number of volumes configured for the file system.
*
*  Return value
*    Returns the value of FS_NUM_VOLUMES configuration define.
*/
int FS_CONF_GetNumVolumes(void) {
  return FS_NUM_VOLUMES;
}

/*********************************************************************
*
*       FS_CONF_IsTrialVersion
*
*  Function description
*    Checks if the file system has been configured as a trial (limited) version.
*
*  Return value
*    ==0    Full version.
*    !=0    Trial version.
*/
int FS_CONF_IsTrialVersion(void) {
  return 0;
}

/*********************************************************************
*
*       FS_CONF_GetDebugLevel
*
*  Function description
*    Returns the level of debug information configured for the file system.
*
*  Return value
*    Value of FS_DEBUG_LEVEL configuration define.
*/
int FS_CONF_GetDebugLevel(void) {
  return FS_DEBUG_LEVEL;
}

#if FS_SUPPRESS_EOF_ERROR

/*********************************************************************
*
*       FS_ConfigEOFErrorSuppression
*
*  Function description
*    Enables / disables the reporting of end-of-file condition as error.
*
*  Parameters
*    OnOff      Activation status of the option.
*               * 0   EOF is reported as error.
*               * 1   EOF is not reported as error.
*
*  Additional information
*    The end-of-file indicator of a file handle is set to 1 as soon
*    as the application tries to read more bytes than available in
*    the file. This function controls if an error is reported
*    via FS_FError() when the end-of-file indicator is set for
*    a file handle. The default is to report the end-of-file
*    condition as error. The configuration has effect on all the
*    opened file handles.
*/
void FS_ConfigEOFErrorSuppression(int OnOff) {
  FS_LOCK();
  FS_LOCK_SYS();
  _IsEOFErrorSuppressed = (U8)OnOff;
  FS_UNLOCK_SYS();
  FS_UNLOCK();
}

#endif // FS_SUPPRESS_EOF_ERROR

#if FS_SUPPORT_POSIX

/*********************************************************************
*
*       FS_ConfigPOSIXSupport
*
*  Function description
*    Enables / disables support for the POSIX-like behavior.
*
*  Parameters
*    OnOff      Activation status of the option.
*               * 0   POSIX behavior is disabled.
*               * 1   POSIX behavior is enabled.
*
*  Additional information
*    The end-of-file indicator of a file handle is set to 1 as soon
*    as the application tries to read more bytes than available in
*    the file. This function controls if an error is reported
*    via FS_FError() when the end-of-file indicator is set for
*    a file handle. The default is to report the end-of-file
*    condition as error. The configuration has effect on all the
*    opened file handles.
*/
void FS_ConfigPOSIXSupport(int OnOff) {
  FS_LOCK();
  FS_LOCK_SYS();
  FS_IsPOSIXSupported = (U8)OnOff;
  FS_UNLOCK_SYS();
  FS_UNLOCK();
}

#endif // FS_SUPPORT_POSIX

#if FS_VERIFY_WRITE

/*********************************************************************
*
*       FS_ConfigWriteVerification
*
*  Function description
*    Enables / disables the verification of the written data.
*
*  Parameters
*    OnOff      Activation status of the option.
*               * 0   Verification is disabled.
*               * 1   Verification is enabled.
*
*  Notes
*    (1) Enabling the write verification can negatively affect the
*        write performance of the file system.
*
*  Additional information
*    If enabled, this feature requests the file system to check that
*    the data has been written correctly to storage device.
*    This operation is performed by reading back and comparing
*    the read with the written data. The verification is performed
*    one logical sector at a time. A sector buffer is allocated
*    from the memory pool for this operation.
*/
void FS_ConfigWriteVerification(int OnOff) {
  FS_LOCK();
  FS_LOCK_SYS();
  FS_IsWriteVerificationEnabled = (U8)OnOff;
  FS_UNLOCK_SYS();
  FS_UNLOCK();
}

#endif // FS_VERIFY_WRITE

/*********************************************************************
*
*       FS_SetTimeDateCallback
*
*  Function description
*    Configures a function that the file system can use to get
*    the current time and date.
*
*  Parameters
*    pfTimeDate     Function to be invoked.
*
*  Additional information
*    During the initialization of the file system via FS_Init()
*    the callback function is initialized to point to FS_X_GetTimeDate().
*/
void FS_SetTimeDateCallback(FS_TIME_DATE_CALLBACK * pfTimeDate) {
  FS_LOCK();
  FS_LOCK_SYS();
  _pfTimeDate = pfTimeDate;
  FS_UNLOCK_SYS();
  FS_UNLOCK();
}

/*********************************************************************
*
*       FS_SetCharSetType
*
*  Function description
*    Configures the character set that is used for
*    the file and directory names.
*
*  Parameters
*    pCharSetType     Type of character set used.
*
*  Additional information
*    Permitted values of pCharSetType are:
*    +------------------+----------------------------------------------+
*    | Identifier       | Description                                  |
*    +------------------+----------------------------------------------+
*    | FS_CHARSET_CP437 | Latin characters                             |
*    +------------------+----------------------------------------------+
*    | FS_CHARSET_CP932 | Japanese characters encoded as Shift JIS     |
*    +------------------+----------------------------------------------+
*    | FS_CHARSET_CP936 | Simplified Chinese characters encoded as GBK |
*    +------------------+----------------------------------------------+
*    | FS_CHARSET_CP949 | Korean characters encoded as UHC             |
*    +------------------+----------------------------------------------+
*    | FS_CHARSET_CP950 | Chinese characters encoded as Big5           |
*    +------------------+----------------------------------------------+
*/
void FS_SetCharSetType(const FS_CHARSET_TYPE * pCharSetType) {
  FS_LOCK();
  FS_LOCK_SYS();
  FS_pCharSetType = pCharSetType;
  FS_UNLOCK_SYS();
  FS_UNLOCK();
}

/*********************************************************************
*
*       FS_GetNumFilesOpen
*
*  Function description
*    Queries the number of opened file handles.
*
*  Return value
*    Total number of file handles that are open.
*
*  Additional information
*    This function counts the number of file handles that were opened
*    by the application via FS_FOpen() or FS_FOpenEx() and that were
*    not closed yet via FS_FClose().
*
*    The returned value is not the actual number of files opened
*    because the same file can be opened by an application more
*    than one time. For example, FS_GetNumFilesOpen() returns 2
*    if the same file is opened by the application twice.
*/
int FS_GetNumFilesOpen(void) {
  int       r;
  FS_FILE * pFile;

  FS_LOCK();
  r = 0;
  FS_LOCK_SYS();
  pFile = FS_Global.pFirstFileHandle;
  while (pFile != NULL) {       // While no free entry found.
    if (pFile->InUse != 0u) {
      r++;
    }
    pFile = pFile->pNext;
  }
  FS_UNLOCK_SYS();
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_GetNumFilesOpenEx
*
*  Function description
*    Queries the number of opened file handles on a volume.
*
*  Parameters
*    sVolumeName    Name of the volume (0-terminated string).
*                   NULL is permitted.
*
*  Return value
*    >=0    Total number of file handles that are open on the volume.
*    < 0    Error code indicating the failure reason.
*
*  Additional information
*    This function counts the number of file handles that were opened
*    by the application via FS_FOpen() or FS_FOpenEx() and that were
*    not closed yet via FS_FClose() on a specified volume.
*
*    The returned value is not the actual number of files opened
*    because the same file can be opened by an application more
*    than one time. For example, FS_GetNumFilesOpenEx() returns 2
*    if the same file is opened by the application twice.
*
*    FS_GetNumFilesOpenEx() works in the same way as
*    FS_GetNumFilesOpen() if sVolumeName is set to NULL.
*/
int FS_GetNumFilesOpenEx(const char * sVolumeName) {
  int           r;
  FS_FILE     * pFile;
  FS_VOLUME   * pVolume;
  FS_FILE_OBJ * pFileObj;

  FS_LOCK();
  r       = 0;
  pVolume = NULL;
  if (sVolumeName != NULL) {
    pVolume = FS__FindVolume(sVolumeName);
    if (pVolume == NULL) {
      r = FS_ERRCODE_VOLUME_NOT_FOUND;      // Error, invalid volume name.
    }
  }
  if (r == 0) {
    FS_LOCK_SYS();
    pFile = FS_Global.pFirstFileHandle;
    while (pFile != NULL) {
      if (pFile->InUse != 0u) {
        if (pVolume == NULL) {
          r++;
        } else {
          pFileObj = pFile->pFileObj;
          if (pFileObj != NULL) {
            if (pVolume == pFileObj->pVolume) {
              r++;
            }
          }
        }
      }
      pFile = pFile->pNext;
    }
    FS_UNLOCK_SYS();
  }
  FS_UNLOCK();
  return r;
}

/*************************** End of file ****************************/
