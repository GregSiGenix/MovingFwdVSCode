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
File        : FS_MAP.c
Purpose     : Wrapper to call the correct FS function.
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#include "FS_Int.h"

#if FS_SUPPORT_MULTIPLE_FS

/*********************************************************************
*
*       Defines, fixed
*
**********************************************************************
*/

/*********************************************************************
*
*       ASSERT_IS_VOLUME_VALID
*/
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL)
  #define ASSERT_IS_VOLUME_VALID(pVolume)                                  \
    if ((pVolume)->pFS_API == NULL) {                                      \
      FS_DEBUG_ERROROUT((FS_MTYPE_API, "MAP: Invalid file system type.")); \
    }
#else
  #define ASSERT_IS_VOLUME_VALID(pVolume)
#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL

/*********************************************************************
*
*       ASSERT_IS_FILE_VALID
*/
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL)
  #define ASSERT_IS_FILE_VALID(pFile)                                 \
    if ((pFile)->pFileObj == NULL) {                                  \
      FS_DEBUG_ERROROUT((FS_MTYPE_API, "MAP: Invalid file object.")); \
    }
#else
  #define ASSERT_IS_FILE_VALID(pFile)
#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL

/*********************************************************************
*
*       Local data types
*
**********************************************************************
*/
typedef struct {
  int               FSType;
  const FS_FS_API * pAPI;
} FS_API_TABLE;

/*********************************************************************
*
*       Static const data
*
**********************************************************************
*/
static const FS_API_TABLE _aAPI[] = {
  {FS_FAT, &FS_FAT_API},
  {FS_EFS, &FS_EFS_API}
};

/*********************************************************************
*
*       Public code (internal)
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_MAP_CloseFile
*/
int FS_MAP_CloseFile(FS_FILE * pFile) {
  FS_VOLUME   * pVolume;
  FS_FILE_OBJ * pFileObj;
  int           r;

  ASSERT_IS_FILE_VALID(pFile);
  pFileObj = pFile->pFileObj;
  if (pFileObj == NULL) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;
  }
  pVolume = pFile->pFileObj->pVolume;
  ASSERT_IS_VOLUME_VALID(pVolume);
  if (pVolume->pFS_API == NULL) {
    return FS_ERRCODE_INVALID_FS_TYPE;
  }
  r = pVolume->pFS_API->pfCloseFile(pFile);
  return r;
}

/*********************************************************************
*
*       FS_MAP_CheckFS_API
*/
int FS_MAP_CheckFS_API(FS_VOLUME * pVolume) {
  unsigned i;
  int      r;

  if (pVolume->pFS_API != NULL) {
    r = pVolume->pFS_API->pfCheckBootSector(pVolume);
    if (r == 0) {
      return FS_ERRCODE_OK;
    }
  } else {
    for (i = 0; i < SEGGER_COUNTOF(_aAPI); i++) {
      if (_aAPI[i].pAPI->pfCheckBootSector(pVolume) == 0) {
        pVolume->pFS_API = _aAPI[i].pAPI;
        return FS_ERRCODE_OK;
      }
    }
  }
  FS_DEBUG_ERROROUT((FS_MTYPE_API, "MAP: FS_MAP_CheckFS_API: Volume does not contain a recognizable file system."));
  return FS_ERRCODE_INVALID_FS_FORMAT;
}

/*********************************************************************
*
*       FS_MAP_Read
*/
U32 FS_MAP_Read(FS_FILE * pFile, void * pData, U32 NumBytes) {
  FS_VOLUME   * pVolume;
  FS_FILE_OBJ * pFileObj;
  U32           r;

  ASSERT_IS_FILE_VALID(pFile);
  pFileObj = pFile->pFileObj;
  if (pFileObj == NULL) {
    return 0;
  }
  pVolume = pFile->pFileObj->pVolume;
  ASSERT_IS_VOLUME_VALID(pVolume);
  if (pVolume->pFS_API == NULL) {
    return 0;
  }
  r = pVolume->pFS_API->pfRead(pFile, pData, NumBytes);
  return r;
}

/*********************************************************************
*
*       FS_MAP_Write
*/
U32 FS_MAP_Write(FS_FILE * pFile, const void  * pData, U32 NumBytes) {
  FS_VOLUME   * pVolume;
  FS_FILE_OBJ * pFileObj;
  U32           r;

  ASSERT_IS_FILE_VALID(pFile);
  pFileObj = pFile->pFileObj;
  if (pFileObj == NULL) {
    return 0;
  }
  pVolume = pFile->pFileObj->pVolume;
  ASSERT_IS_VOLUME_VALID(pVolume);
  if (pVolume->pFS_API == NULL) {
    return 0;
  }
  r = pVolume->pFS_API->pfWrite(pFile, pData, NumBytes);
  return r;
}

/*********************************************************************
*
*       FS_MAP_OpenFile
*/
int FS_MAP_OpenFile(const char * sFileName, FS_FILE * pFile, int DoDel, int DoOpen, int DoCreate) {
  FS_VOLUME   * pVolume;
  FS_FILE_OBJ * pFileObj;
  int           r;

  ASSERT_IS_FILE_VALID(pFile);
  pFileObj = pFile->pFileObj;
  if (pFileObj == NULL) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;
  }
  pVolume = pFile->pFileObj->pVolume;
  ASSERT_IS_VOLUME_VALID(pVolume);
  if (pVolume->pFS_API == NULL) {
    return FS_ERRCODE_INVALID_FS_TYPE;
  }
  r = pVolume->pFS_API->pfOpenFile(sFileName, pFile, DoDel, DoOpen, DoCreate);
  return r;
}

/*********************************************************************
*
*       FS_MAP_Format
*/
int FS_MAP_Format(FS_VOLUME * pVolume, const FS_FORMAT_INFO * pFormatInfo) {
  ASSERT_IS_VOLUME_VALID(pVolume);
  if (pVolume->pFS_API != NULL) {
    return pVolume->pFS_API->pfFormat(pVolume, pFormatInfo);
  }
  return FS_ERRCODE_INVALID_FS_TYPE;
}

/*********************************************************************
*
*       FS_MAP_OpenDir
*/
int FS_MAP_OpenDir(const char * pDirName,  FS_DIR_OBJ * pDirObj) {
  FS_VOLUME * pVolume;

  pVolume = pDirObj->pVolume;
  ASSERT_IS_VOLUME_VALID(pVolume);
  if (pVolume->pFS_API != NULL) {
    return pVolume->pFS_API->pfOpenDir(pDirName, pDirObj);
  }
  return FS_ERRCODE_INVALID_FS_TYPE;
}

/*********************************************************************
*
*       FS_MAP_CloseDir
*/
int FS_MAP_CloseDir(FS_DIR_OBJ * pDirObj) {
  FS_VOLUME * pVolume;

  pVolume = pDirObj->pVolume;
  ASSERT_IS_VOLUME_VALID(pVolume);
  if (pVolume->pFS_API != NULL) {
    return pVolume->pFS_API->pfCloseDir(pDirObj);
  }
  return FS_ERRCODE_INVALID_FS_TYPE;
}

/*********************************************************************
*
*       FS_MAP_ReadDir
*/
int FS_MAP_ReadDir(FS_DIR_OBJ * pDirObj, FS_DIRENTRY_INFO * pDirEntryInfo) {
  FS_VOLUME * pVolume;

  pVolume = pDirObj->pVolume;
  ASSERT_IS_VOLUME_VALID(pVolume);
  if (pVolume->pFS_API != NULL) {
    return pVolume->pFS_API->pfReadDir(pDirObj, pDirEntryInfo);
  }
  return FS_ERRCODE_INVALID_FS_TYPE;
}

/*********************************************************************
*
*       FS_MAP_RemoveDir
*/
int FS_MAP_RemoveDir(FS_VOLUME * pVolume, const char * sDirName) {
  ASSERT_IS_VOLUME_VALID(pVolume);
  if (pVolume->pFS_API != NULL) {
    return pVolume->pFS_API->pfRemoveDir(pVolume, sDirName);
  }
  return FS_ERRCODE_INVALID_FS_TYPE;
}

/*********************************************************************
*
*       FS_MAP_CreateDir
*/
int FS_MAP_CreateDir(FS_VOLUME * pVolume, const char * sDirName) {
  ASSERT_IS_VOLUME_VALID(pVolume);
  if (pVolume->pFS_API != NULL) {
    return pVolume->pFS_API->pfCreateDir(pVolume, sDirName);
  }
  return FS_ERRCODE_INVALID_FS_TYPE;
}

/*********************************************************************
*
*       FS_MAP_DeleteDir
*/
int FS_MAP_DeleteDir(FS_VOLUME * pVolume, const char * sDirName, int MaxRecursionLevel) {
  ASSERT_IS_VOLUME_VALID(pVolume);
  if (pVolume->pFS_API != NULL) {
    return pVolume->pFS_API->pfDeleteDir(pVolume, sDirName, MaxRecursionLevel);
  }
  return FS_ERRCODE_INVALID_FS_TYPE;
}

/*********************************************************************
*
*       FS_MAP_Rename
*/
int FS_MAP_Rename(FS_VOLUME * pVolume, const char * sOldName, const char * sNewName) {
  ASSERT_IS_VOLUME_VALID(pVolume);
  if (pVolume->pFS_API != NULL) {
    return pVolume->pFS_API->pfRename(pVolume, sOldName, sNewName);
  }
  return FS_ERRCODE_INVALID_FS_TYPE;
}

/*********************************************************************
*
*       FS_MAP_Move
*/
int FS_MAP_Move(FS_VOLUME * pVolume, const char * sOldName, const char * sNewName) {
  ASSERT_IS_VOLUME_VALID(pVolume);
  if (pVolume->pFS_API != NULL) {
    return pVolume->pFS_API->pfMove(pVolume, sOldName, sNewName);
  }
  return FS_ERRCODE_INVALID_FS_TYPE;
}

/*********************************************************************
*
*       FS_MAP_SetDirEntryInfo
*/
int FS_MAP_SetDirEntryInfo(FS_VOLUME * pVolume, const char * sName, const void * p, int Mask) {
  ASSERT_IS_VOLUME_VALID(pVolume);
  if (pVolume->pFS_API != NULL) {
    return pVolume->pFS_API->pfSetDirEntryInfo(pVolume, sName, p, Mask);
  }
  return FS_ERRCODE_INVALID_FS_TYPE;
}

/*********************************************************************
*
*       FS_MAP_GetDirEntryInfo
*/
int FS_MAP_GetDirEntryInfo(FS_VOLUME * pVolume, const char * sName, void * p, int Mask) {
  ASSERT_IS_VOLUME_VALID(pVolume);
  if (pVolume->pFS_API != NULL) {
    return pVolume->pFS_API->pfGetDirEntryInfo(pVolume, sName, p, Mask);
  }
  return FS_ERRCODE_INVALID_FS_TYPE;
}

/*********************************************************************
*
*       FS_MAP_SetEndOfFile
*/
int FS_MAP_SetEndOfFile(FS_FILE * pFile) {
  FS_VOLUME   * pVolume;
  FS_FILE_OBJ * pFileObj;
  int           r;

  ASSERT_IS_FILE_VALID(pFile);
  pFileObj = pFile->pFileObj;
  if (pFileObj == NULL) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;        // Error, invalid file object in the file handle.
  }
  pVolume = pFileObj->pVolume;
  ASSERT_IS_VOLUME_VALID(pVolume);
  if (pVolume->pFS_API == NULL) {
    return FS_ERRCODE_INVALID_FS_TYPE;
  }
  r = pVolume->pFS_API->pfSetEndOfFile(pFile);
  return r;
}

/*********************************************************************
*
*       FS_MAP_Clean
*/
void FS_MAP_Clean(FS_VOLUME * pVolume) {
  ASSERT_IS_VOLUME_VALID(pVolume);
  if (pVolume->pFS_API != NULL) {
    pVolume->pFS_API->pfUnmount(pVolume);
  }
}

/*********************************************************************
*
*       FS_MAP_GetDiskInfo
*/
int FS_MAP_GetDiskInfo(FS_VOLUME * pVolume, FS_DISK_INFO * pDiskData, int Flags) {
  ASSERT_IS_VOLUME_VALID(pVolume);
  if (pVolume->pFS_API != NULL) {
    return pVolume->pFS_API->pfGetDiskInfo(pVolume, pDiskData, Flags);
  }
  return FS_ERRCODE_INVALID_FS_TYPE;
}

/*********************************************************************
*
*       FS_MAP_GetVolumeLabel
*/
int FS_MAP_GetVolumeLabel(FS_VOLUME * pVolume, char * pVolumeLabel, unsigned VolumeLabelSize) {
  ASSERT_IS_VOLUME_VALID(pVolume);
  if (pVolume->pFS_API != NULL) {
    return pVolume->pFS_API->pfGetVolumeLabel(pVolume, pVolumeLabel, VolumeLabelSize);
  }
  return FS_ERRCODE_INVALID_FS_TYPE;
}

/*********************************************************************
*
*       FS_MAP_SetVolumeLabel
*/
int FS_MAP_SetVolumeLabel(FS_VOLUME * pVolume, const char * pVolumeLabel) {
  ASSERT_IS_VOLUME_VALID(pVolume);
  if (pVolume->pFS_API != NULL) {
    return pVolume->pFS_API->pfSetVolumeLabel(pVolume, pVolumeLabel);
  }
  return FS_ERRCODE_INVALID_FS_TYPE;
}

/*********************************************************************
*
*       FS_MAP_CreateJournalFile
*/
int FS_MAP_CreateJournalFile(FS_VOLUME * pVolume, U32 NumBytes, U32 * pFirstSector, U32 * pNumSectors) {
  ASSERT_IS_VOLUME_VALID(pVolume);
  if (pVolume->pFS_API != NULL) {
    return pVolume->pFS_API->pfCreateJournalFile(pVolume, NumBytes, pFirstSector, pNumSectors);
  }
  return FS_ERRCODE_INVALID_FS_TYPE;
}

/*********************************************************************
*
*       FS_MAP_OpenJournalFile
*/
int FS_MAP_OpenJournalFile(FS_VOLUME * pVolume) {
  ASSERT_IS_VOLUME_VALID(pVolume);
  if (pVolume->pFS_API != NULL) {
    return pVolume->pFS_API->pfOpenJournalFile(pVolume);
  }
  return FS_ERRCODE_INVALID_FS_TYPE;
}

/*********************************************************************
*
*       FS_MAP_GetIndexOfLastSector
*/
U32 FS_MAP_GetIndexOfLastSector(FS_VOLUME * pVolume) {
  ASSERT_IS_VOLUME_VALID(pVolume);
  if (pVolume->pFS_API != NULL) {
    return pVolume->pFS_API->pfGetIndexOfLastSector(pVolume);
  }
  return 0;
}

/*********************************************************************
*
*       FS_MAP_CheckVolume
*/
int FS_MAP_CheckVolume(FS_VOLUME * pVolume, void * pBuffer, U32 BufferSize, int MaxRecursionLevel, FS_CHECKDISK_ON_ERROR_CALLBACK * pfOnError) {
  ASSERT_IS_VOLUME_VALID(pVolume);
  if (pVolume->pFS_API != NULL) {
    return pVolume->pFS_API->pfCheckVolume(pVolume, pBuffer, BufferSize, MaxRecursionLevel, pfOnError);
  }
  return FS_ERRCODE_INVALID_FS_TYPE;
}

/*********************************************************************
*
*       FS_MAP_UpdateFile
*/
int FS_MAP_UpdateFile(FS_FILE * pFile) {
  FS_VOLUME   * pVolume;
  FS_FILE_OBJ * pFileObj;
  int           r;

  ASSERT_IS_FILE_VALID(pFile);
  pFileObj = pFile->pFileObj;
  if (pFileObj == NULL) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;
  }
  pVolume = pFileObj->pVolume;
  ASSERT_IS_VOLUME_VALID(pVolume);
  if (pVolume->pFS_API == NULL) {
    return FS_ERRCODE_INVALID_FS_TYPE;
  }
  r = pVolume->pFS_API->pfUpdateFile(pFile);
  return r;
}

/*********************************************************************
*
*       FS_MAP_SetFileSize
*/
int FS_MAP_SetFileSize(FS_FILE * pFile, U32 NumBytes) {
  FS_VOLUME   * pVolume;
  FS_FILE_OBJ * pFileObj;
  int           r;

  ASSERT_IS_FILE_VALID(pFile);
  pFileObj = pFile->pFileObj;
  if (pFileObj == NULL) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;        // Error, invalid file object in the file handle.
  }
  pVolume = pFileObj->pVolume;
  ASSERT_IS_VOLUME_VALID(pVolume);
  if (pVolume->pFS_API == NULL) {
    return FS_ERRCODE_INVALID_FS_TYPE;            // Error, API not set.
  }
  r = pVolume->pFS_API->pfSetFileSize(pFile, NumBytes);
  return r;
}

/*********************************************************************
*
*       FS_MAP_FreeSectors
*/
int FS_MAP_FreeSectors(FS_VOLUME * pVolume) {
  ASSERT_IS_VOLUME_VALID(pVolume);
  if (pVolume->pFS_API != NULL) {
    return pVolume->pFS_API->pfFreeSectors(pVolume);
  }
  return FS_ERRCODE_INVALID_FS_TYPE;      // Error, API not set.
}

/*********************************************************************
*
*       FS_MAP_GetFreeSpace
*/
int FS_MAP_GetFreeSpace(FS_VOLUME * pVolume, void * pBuffer, int SizeOfBuffer, U32 FirstClusterId, U32 * pNumClustersFree, U32 * pNumClustersChecked) {
  ASSERT_IS_VOLUME_VALID(pVolume);
  if (pVolume->pFS_API != NULL) {
    return pVolume->pFS_API->pfGetFreeSpace(pVolume, pBuffer, SizeOfBuffer, FirstClusterId, pNumClustersFree, pNumClustersChecked);
  }
  return FS_ERRCODE_INVALID_FS_TYPE;      // Error, API not set.
}

/*********************************************************************
*
*       FS_MAP_GetATInfo
*/
int FS_MAP_GetATInfo(FS_VOLUME * pVolume, FS_AT_INFO * pATInfo) {
  ASSERT_IS_VOLUME_VALID(pVolume);
  if (pVolume->pFS_API != NULL) {
    return pVolume->pFS_API->pfGetATInfo(pVolume, pATInfo);
  }
  return FS_ERRCODE_INVALID_FS_TYPE;      // Error, API not set.
}

/*********************************************************************
*
*       FS_MAP_CheckDir
*/
int FS_MAP_CheckDir(FS_VOLUME * pVolume, const char * sPath, FS_CLUSTER_MAP * pClusterMap, FS_CHECKDISK_ON_ERROR_CALLBACK * pfOnError) {
  ASSERT_IS_VOLUME_VALID(pVolume);
  if (pVolume->pFS_API != NULL) {
    return pVolume->pFS_API->pfCheckDir(pVolume, sPath, pClusterMap, pfOnError);
  }
  return FS_ERRCODE_INVALID_FS_TYPE;      // Error, API not set.
}

/*********************************************************************
*
*       FS_MAP_CheckAT
*/
int FS_MAP_CheckAT(FS_VOLUME * pVolume, const FS_CLUSTER_MAP * pClusterMap, FS_CHECKDISK_ON_ERROR_CALLBACK * pfOnError) {
  ASSERT_IS_VOLUME_VALID(pVolume);
  if (pVolume->pFS_API != NULL) {
    return pVolume->pFS_API->pfCheckAT(pVolume, pClusterMap, pfOnError);
  }
  return FS_ERRCODE_INVALID_FS_TYPE;      // Error, API not set.
}

/*********************************************************************
*
*       FS_MAP_ReadATEntry
*/
I32 FS_MAP_ReadATEntry(FS_VOLUME * pVolume, U32 ClusterId) {
  ASSERT_IS_VOLUME_VALID(pVolume);
  if (pVolume->pFS_API != NULL) {
    return pVolume->pFS_API->pfReadATEntry(pVolume, ClusterId);
  }
  return FS_ERRCODE_INVALID_FS_TYPE;      // Error, API not set.
}

/*********************************************************************
*
*       FS_MAP_SetDirEntryInfoEx
*/
int FS_MAP_SetDirEntryInfoEx(FS_VOLUME * pVolume, const FS_DIRENTRY_POS * pDirEntryPos, const void * p, int Mask) {
  int r;

  ASSERT_IS_VOLUME_VALID(pVolume);
  if (pVolume->pFS_API == NULL) {
    return FS_ERRCODE_INVALID_FS_TYPE;            // Error, API not set.
  }
  r = pVolume->pFS_API->pfSetDirEntryInfoEx(pVolume, pDirEntryPos, p, Mask);
  return r;
}

/*********************************************************************
*
*       FS_MAP_GetDirEntryInfoEx
*/
int FS_MAP_GetDirEntryInfoEx(FS_VOLUME * pVolume, const FS_DIRENTRY_POS * pDirEntryPos, void * p, int Mask) {
  int r;

  ASSERT_IS_VOLUME_VALID(pVolume);
  if (pVolume->pFS_API == NULL) {
    return FS_ERRCODE_INVALID_FS_TYPE;            // Error, API not set.
  }
  r = pVolume->pFS_API->pfGetDirEntryInfoEx(pVolume, pDirEntryPos, p, Mask);
  return r;
}

/*********************************************************************
*
*       FS_MAP_GetFSType
*/
int FS_MAP_GetFSType(const FS_VOLUME * pVolume) {
  int                  FSType;
  int                  i;
  const FS_API_TABLE * pAPI;

  FSType = FS_ERRCODE_INVALID_FS_TYPE;
  pAPI   = _aAPI;
  for (i = 0; i < (int)SEGGER_COUNTOF(_aAPI); ++i) {
    if (pVolume->pFS_API == pAPI->pAPI) {
      FSType = pAPI->FSType;
      break;
    }
    ++pAPI;
  }
  return FSType;
}

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_SetFSType
*
*  Function description
*    Sets the type of file system a volume.
*
*  Parameters
*    sVolumeName      Name of the volume for which the type of the
*                     file system has to be set. It cannot be NULL.
*    FSType           Type of file system. It take be one these
*                     values:
*                     * FS_FAT    FAT file system.
*                     * FS_EFS    SEGGER's Embedded File System.
*
*  Return value
*    ==0    OK, file system type set.
*    !=0    Error code indicating the failure reason.
*
*  Additional information
*    This function is optional and available only when both the FAT
*    and the EFS file system are enabled in the file system, that is
*    the FS_SUPPORT_FAT and FS_SUPPORT_EFS configuration defines
*    are both set to 1 (multiple-volume configuration).
*
*    In a multiple-volume configuration the application has to call
*    FS_SetFSType() before formatting a volume that has not been
*    formatted before or when the volume has been formatted using a
*    different file system type.
*/
int FS_SetFSType(const char * sVolumeName, int FSType) {
  int                  r;
  int                  i;
  FS_VOLUME          * pVolume;
  const FS_API_TABLE * pAPI;

  FS_LOCK();
  r = FS_ERRCODE_VOLUME_NOT_FOUND;
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_WARNINGS)
    if (pVolume->pFS_API != NULL) {
      FS_DEVICE * pDevice;

      pDevice = &pVolume->Partition.Device;
      FS_DEBUG_WARN((FS_MTYPE_FS, "FS_SetFSType: A file system is already assigned to volume \"%s:%d:\".", pDevice->pType->pfGetName(pDevice->Data.Unit), pDevice->Data.Unit));
    }
#endif
    pAPI = _aAPI;
    for (i = 0; i < (int)SEGGER_COUNTOF(_aAPI); ++i) {
      if (pAPI->FSType == FSType) {
        pVolume->pFS_API = pAPI->pAPI;
        r = FS_ERRCODE_OK;
        break;
      }
      ++pAPI;
    }
    if (i >= (int)SEGGER_COUNTOF(_aAPI)) {
      FS_DEBUG_ERROROUT((FS_MTYPE_FS, "FS_SetFSType: Invalid file system type %d.", FSType));
      r = FS_ERRCODE_INVALID_FS_TYPE;
    }
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_GetFSType
*
*  Function description
*    Returns the type of file system assigned to volume.
*
*  Parameters
*    sVolumeName      Name of the volume to be queried.
*                     It cannot be NULL.
*
*  Return value
*    >=0    File system type (FS_FAT or FS_EFS)
*    < 0    Error code indicating the failure reason.
*
*  Additional information
*    This function is optional and available only when both the FAT
*    and the EFS file system are enabled in the file system, that is
*    the FS_SUPPORT_FAT and FS_SUPPORT_EFS configuration defines
*    are both set to 1 (multiple-volume configuration).
*/
int FS_GetFSType(const char * sVolumeName) {
  FS_VOLUME * pVolume;
  int         r;

  FS_LOCK();
  r = FS_ERRCODE_VOLUME_NOT_FOUND;            // Set to indicate an error.
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    r = FS_MAP_GetFSType(pVolume);
  }
  FS_UNLOCK();
  return r;
}

#endif

/*************************** End of file ****************************/
