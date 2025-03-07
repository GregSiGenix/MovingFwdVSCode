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
File        : FS_Volume.c
Purpose     : API functions for handling volumes.
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#include "FS_Int.h"

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _GetJournalOpenCnt
*
*  Function description
*    Internal function. Returns the number of opened journal transactions.
*
*  Parameters
*    pVolume      Identifies the volume.
*
*  Return value
*    The number of journal transactions.
*/
static int _GetJournalOpenCnt(FS_VOLUME * pVolume) {
  int OpenCnt;

#if FS_SUPPORT_JOURNAL
  OpenCnt = FS__JOURNAL_GetOpenCnt(pVolume);
#else
  FS_USE_PARA(pVolume);
  OpenCnt = 0;
#endif
  return OpenCnt;
}

/*********************************************************************
*
*       _Mount
*
*  Function description
*    If volume is not yet mounted, try to mount it.
*
*  Parameters
*    pVolume       Volume to mount. Must be valid, may not be NULL.
*    MountType     Specifies how the volume should be mounted.
*
*  Return value
*    ==0                Volume is not mounted.
*    ==FS_MOUNT_RO      Volume is mounted read only.
*    ==FS_MOUNT_RW      Volume is mounted read/write.
*     <0                Error code indicating the failure reason.
*/
static int _Mount(FS_VOLUME * pVolume, U8 MountType) {
  FS_DEVICE * pDevice;
  int         r;

  pDevice = &pVolume->Partition.Device;
  //
  // Check if the media is accessible.
  //
  r = FS_LB_GetStatus(pDevice);
  if (r == FS_MEDIA_NOT_PRESENT) {
    FS_DEBUG_ERROROUT((FS_MTYPE_API, "_Mount: Could not mount volume. Storage medium not present."));
    return FS_ERRCODE_STORAGE_NOT_PRESENT;
  }
  r = FS_LB_InitMediumIfRequired(pDevice);
  if (r != 0) {
    return r;                             // Error, could not initialize the storage medium.
  }
  //
  // Check first if there is a partition on the volume.
  //
  r = FS__LocatePartition(pVolume);
  if (r != 0) {
    return r;                             // Error, could not locate partition.
  }
  //
  // Mount the file system.
  //
  r = FS_CHECK_INFOSECTOR(pVolume);
  if (r != 0) {
    return r;                             // Error, storage device not formatted.
  }
  //
  // The mount type is checked in FS__JOURNAL_Mount()
  // so we have to set it here to the correct value.
  //
  pVolume->MountType = MountType;
  //
  // Mount the journal if necessary.
  //
#if FS_SUPPORT_JOURNAL
  r = FS__JOURNAL_Mount(pVolume);
  if (r != 0) {
    return r;                             // Error, could not mount journal file.
  }
#endif // FS_SUPPORT_JOURNAL
  return (int)MountType;
}

/*********************************************************************
*
*       _MountSyncIfRequired
*
*  Function description
*    Mounts and synchronizes the volume.
*
*  Return value
*    ==0      OK, volume mounted and synchronized.
*    !=0      Error code indicating the failure reason.
*
*  Additional information
*    This function is called before a disk checking operation to make
*    sure that the all the data cached by the file system is updated
*    to storage device.
*/
static int _MountSyncIfRequired(FS_VOLUME * pVolume) {
  int      r;
  unsigned MountType;

  MountType = pVolume->MountType;
  switch (MountType) {
  case FS_MOUNT_RW:
    r = FS__Sync(pVolume);
    if (r != 0) {
      return r;
    }
    break;
  case FS_MOUNT_RO:
    break;
  default:
    MountType = pVolume->AllowAutoMount;
    if (MountType != 0u) {
      r = FS__Mount(pVolume, (U8)MountType);
      if (r < 0) {
        return r;                           // Error, could not mount volume.
      }
    }
    break;
  }
  return 0;                                 // OK, the volume has been mounted and synchronized.
}

/*********************************************************************
*
*       _SuspendJournal
*
*  Function description
*    Temporarily disables the journal.
*
*  Additional information
*    _SuspendJournal() is called before a disk checking operation
*    to temporarily disable the journal. This function has to be
*    called in pair with _ResumeJournal().
*/
static int _SuspendJournal(FS_VOLUME * pVolume) {
  int OpenCnt;

  //
  // We cannot check the volume if a journal transaction is in progress.
  //
  OpenCnt = _GetJournalOpenCnt(pVolume);
  if (OpenCnt != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_API, "_SuspendJournal: Journal transaction in progress."));
    return FS_ERRCODE_INVALID_USAGE;        // Error, journal transaction in progress.
  }
  //
  // Disable temporarily the journal so that the disk checking operation
  // can write directly to storage when repairing an error.
  //
  FS_JOURNAL_INVALIDATE(pVolume);
  return 0;
}

/*********************************************************************
*
*       _ResumeJournal
*
*  Function description
*    Enable the journal operation.
*
*  Additional information
*    This function has to be called in pair with _SuspendJournal().
*    It does nothing if the journal is not active.
*/
static int _ResumeJournal(FS_VOLUME * pVolume) {
  int r;

  FS_USE_PARA(pVolume);
  //
  // Re-enable the journal.
  //
  r = FS_JOURNAL_MOUNT(pVolume);
  return r;
}

#if FS_SUPPORT_JOURNAL

/*********************************************************************
*
*       _SetVolumeLabelFS
*/
static int _SetVolumeLabelFS(FS_VOLUME * pVolume, const char * sVolumeLabel) {
  int r;
  int Result;

  r = FS__JOURNAL_Begin(pVolume);
  if (r == 0) {
    r = FS_SET_VOLUME_LABEL(pVolume, sVolumeLabel);
    FS__JOURNAL_SetError(pVolume, r);
    Result = FS__JOURNAL_End(pVolume);
    if (Result != 0) {
      r = Result;
    }
  } else {
    (void)FS_SET_VOLUME_LABEL(pVolume, sVolumeLabel);       // Perform the operation without journaling.
  }
  return r;
}

#endif // FS_SUPPORT_JOURNAL

#if FS_SUPPORT_VOLUME_ALIAS

/*********************************************************************
*
*       _IsValidVolumeAliasChar
*
*  Function description
*    Verifies if the specified character is valid in a volume alias.
*/
static int _IsValidVolumeAliasChar(char c) {
  if (   ((c >= 'a') && (c <= 'z'))
      || ((c >= 'A') && (c <= 'Z'))
      || ((c >= '0') && (c <= '9'))
      || (c == '_')) {
    return 1;             // The character is valid.
  }
  return 0;               // The character is invalid.
}

#endif // FS_SUPPORT_VOLUME_ALIAS

/*********************************************************************
*
*       _SyncVolumeNL
*
*  Function description
*    Writes cached volume related information to storage.
*
*  Parameters
*    pVolume      Volume instance.
*
*  Return value
*    ==0      OK, volume synchronized successfully.
*    !=0      An error occurred.
*
*  Additional information
*    This function does not lock the file system.
*/
static int _SyncVolumeNL(FS_VOLUME * pVolume) {
  //
  // Update all relevant FS information to storage device.
  //
  FS_CLEAN_FS(pVolume);
  //
  // As last operation, tell the storage layer to sync. Typically, this operation flushes the sector cache (if active).
  //
  FS__STORAGE_SyncNL(pVolume);
  return 0;
}

#if FS_SUPPORT_JOURNAL

/*********************************************************************
*
*       _SyncVolumeFS
*
*  Function description
*    Writes cached volume related information to storage.
*
*  Parameters
*    pVolume      Volume instance.
*
*  Return value
*    ==0      OK, volume synchronized successfully.
*    !=0      An error occurred.
*
*  Additional information
*    This is the fail-safe version of _SyncVolumeNL().
*/
static int _SyncVolumeFS(FS_VOLUME * pVolume) {
  int r;
  int Result;

  r = FS__JOURNAL_Begin(pVolume);
  if (r == 0) {
    r = _SyncVolumeNL(pVolume);
    FS__JOURNAL_SetError(pVolume, r);
    Result = FS__JOURNAL_End(pVolume);
    if (Result != 0) {
      r = Result;
    }
  } else {
    (void)_SyncVolumeNL(pVolume);           // Perform the operation without journal.
  }
  return r;
}

/*********************************************************************
*
*       _FreeSectorsFS
*/
static int _FreeSectorsFS(FS_VOLUME * pVolume) {
  int r;
  int Result;

  r = FS__JOURNAL_Begin(pVolume);
  if (r == 0) {
    r = FS_FREE_SECTORS(pVolume);
    FS__JOURNAL_SetError(pVolume, r);
    Result = FS__JOURNAL_End(pVolume);
    if (Result != 0) {
      r = Result;
    }
  } else {
    (void)FS_FREE_SECTORS(pVolume);         // Perform the operation without journaling.
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
*       FS__GetVolumeInfoDL
*
*  Function description
*    Internal function. Returns information about the volume.
*
*  Parameters
*    pVolume      Identifies the volume. Cannot be NULL.
*    pInfo        [OUT] Information about the volume.
*    Flags        Identifies the type of information requested.
*
*  Return value
*    ==0      OK, information about volume returned.
*    !=0      Error code indicating the failure reason.
*/
int FS__GetVolumeInfoDL(FS_VOLUME * pVolume, FS_DISK_INFO * pInfo, int Flags) {
  int r;

  r = FS__AutoMount(pVolume);
  switch ((unsigned)r) {
  case FS_MOUNT_RO:
    // through
  case FS_MOUNT_RW:
    FS_LOCK_DRIVER(&pVolume->Partition.Device);
    r = FS_GET_DISKINFO(pVolume, pInfo, Flags);
    FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
    if (r == FS_ERRCODE_OK) {
      pInfo->IsSDFormatted = 0;
      pInfo->sAlias        = NULL;
#if FS_SUPPORT_FAT
      pInfo->IsSDFormatted = (U8)FS__IsSDFormatted(pVolume);
#endif // FS_SUPPORT_FAT
#if FS_SUPPORT_VOLUME_ALIAS
#if (FS_MAX_LEN_VOLUME_ALIAS > 0)
      pInfo->sAlias = pVolume->acAlias;
#else
      pInfo->sAlias = pVolume->sAlias;
#endif // FS_MAX_LEN_VOLUME_ALIAS > 0
#endif // FS_SUPPORT_VOLUME_ALIAS
    }
    break;
  case 0:
    r = FS_ERRCODE_VOLUME_NOT_MOUNTED;
    break;
  default:
    //
    // An error occurred during the mount operation.
    //
    break;
  }
  return r;
}

/*********************************************************************
*
*       FS__GetVolumeInfoEx
*
*  Function description
*    Internal function. Returns volume information.
*
*  Parameters
*    sVolumeName      Volume name.
*    pInfo            [OUT] Information about the volume.
*    Flags            Identifies the type of information requested.
*
*  Return value
*    ==0      OK, information about the volume returned.
*    !=0      Error code indicating the failure reason.
*/
int FS__GetVolumeInfoEx(const char * sVolumeName, FS_DISK_INFO * pInfo, int Flags) {
  FS_VOLUME * pVolume;
  int         r;

  //
  // Validate parameters.
  //
  if ((sVolumeName == NULL) || (pInfo == NULL)) {
    return FS_ERRCODE_INVALID_PARA;       // Error, invalid parameters.
  }
  if (((unsigned)Flags & (unsigned)FS_DISKINFO_FLAG_WORK_BUFFER) != 0u) {
    if ((pInfo->pBuffer != NULL) && (pInfo->SizeOfBuffer < 0)) {
      return FS_ERRCODE_INVALID_PARA;     // Error, invalid work buffer.
    }
  }
  //
  // Perform the operation.
  //
  r = FS_ERRCODE_VOLUME_NOT_FOUND;
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    r = FS__GetVolumeInfoDL(pVolume, pInfo, Flags);
  }
  return r;
}

/*********************************************************************
*
*       FS__GetVolumeInfo
*
*  Function description
*    Internal function. Returns volume information.
*
*  Parameters
*    sVolumeName  Volume name.
*    pInfo        [OUT] Information about volume.
*
*  Return value
*    ==0      OK, information about volume returned.
*    !=0      Error code indicating the failure reason.
*/
int FS__GetVolumeInfo(const char * sVolumeName, FS_DISK_INFO * pInfo) {
  int r;

  r = FS__GetVolumeInfoEx(sVolumeName, pInfo, FS_DISKINFO_FLAG_FREE_SPACE);
  return r;
}

/*********************************************************************
*
*       FS__MountNL
*
*  Function description
*    FS internal function.
*    If volume is not yet mounted, try to mount it.
*
*  Parameters
*    pVolume      Volume to mount. Must be valid, may not be NULL.
*    MountType    Specifies how the volume should be mounted.
*
*  Return value
*    ==0                Volume is not mounted
*    ==FS_MOUNT_RO      Volume is mounted read only
*    ==FS_MOUNT_RW      Volume is mounted read/write
*     <0                Error code indicating the failure reason
*/
int FS__MountNL(FS_VOLUME * pVolume, U8 MountType) {
  int r;

  if (pVolume->MountType == 0u) {
    //
    // Shall we auto mount?
    //
    if (MountType != 0u) {
      r = _Mount(pVolume, MountType);
      if (r <= 0) {
        return r;
      }
    }
  }
  return (int)pVolume->MountType;
}

/*********************************************************************
*
*       FS__Mount
*
*  Function description
*    FS internal function.
*    If volume is not yet mounted, try to mount it.
*
*  Parameters
*    pVolume       Volume to mount. Must be valid, may not be NULL.
*    MountType     FS_MOUNT_RO or FS_MOUNT_RW.
*
*  Return value
*    ==0                Volume is not mounted
*    ==FS_MOUNT_RO      Volume is mounted read only
*    ==FS_MOUNT_RW      Volume is mounted read/write
*    < 0                Error code indicating the failure reason
*/
int FS__Mount(FS_VOLUME * pVolume, U8 MountType) {
  int r;

  FS_LOCK_DRIVER(&pVolume->Partition.Device);
  r = FS__MountNL(pVolume, MountType);
  FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
  return r;
}

/*********************************************************************
*
*       FS__AutoMountNL
*
*  Function description
*    If volume is not yet mounted, try to mount it if allowed.
*    This function does not lock.
*
*  Parameters
*    pVolume    Volume to mount. Must be valid, may not be NULL.
*
*  Return value
*    ==0                Volume is not mounted
*    ==FS_MOUNT_RO      Volume is mounted read only
*    ==FS_MOUNT_RW      Volume is mounted read/write
*    < 0                Error code indicating the failure reason
*/
int FS__AutoMountNL(FS_VOLUME * pVolume) {
  int r;

  r = (int)pVolume->MountType;
  if (r != 0) {
    return r;
  }
  if (pVolume->AllowAutoMount == 0u) {
    return 0;
  }
  //
  // Not yet mounted, auto-mount allowed. Let's try to mount.
  //
  r = _Mount(pVolume, pVolume->AllowAutoMount);
  return r;
}

/*********************************************************************
*
*       FS__AutoMount
*
*  Function description
*    If volume is not yet mounted, try to mount it if allowed.
*
*  Parameters
*    pVolume      Volume to mount. Must be valid, may not be NULL.
*
*  Return value
*    ==0                Volume is not mounted
*    ==FS_MOUNT_RO      Volume is mounted read only
*    ==FS_MOUNT_RW      Volume is mounted read/write
*    < 0                Error code indicating the failure reason
*/
int FS__AutoMount(FS_VOLUME * pVolume) {
  int r;

  FS_LOCK_DRIVER(&pVolume->Partition.Device);
  r = FS__AutoMountNL(pVolume);
  FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
  return r;
}

/*********************************************************************
*
*       FS__SyncNL
*
*  Function description
*    Non-locking version of FS_Sync
*/
int FS__SyncNL(FS_VOLUME * pVolume) {
  FS_FILE     * pFile;
  int           r;
  int           Result;
  FS_FILE_OBJ * pFileObj;

  r = 0;              // Set to indicate success.
  if (pVolume->MountType != 0u) {
    //
    // For each file handle in use update the cached information to storage.
    //
    pFile = FS_Global.pFirstFileHandle;
    while (pFile != NULL) {
      //
      // Process only the file handles that are in use and located on the specified volume.
      //
      if (pFile->InUse != 0u) {
        pFileObj = pFile->pFileObj;
        if (pFileObj != NULL) {
          if (pFileObj->pVolume == pVolume) {
            Result = FS__SyncFileNL(pVolume, pFile);
            if (Result != 0) {
              r = Result;
            }
          }
        }
      }
      pFile = pFile->pNext;
    }
#if FS_SUPPORT_JOURNAL
    Result = _SyncVolumeFS(pVolume);
#else
    Result = _SyncVolumeNL(pVolume);
#endif // FS_SUPPORT_JOURNAL
    if (Result != 0) {
      r = Result;
    }
  }
  return r;
}

/*********************************************************************
*
*       FS__Sync
*
*  Function description
*    Internal version of FS_Sync
*/
int FS__Sync(FS_VOLUME * pVolume) {
  int r;

  FS_LOCK_DRIVER(&pVolume->Partition.Device);
  r = FS__SyncNL(pVolume);
  FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
  return r;
}

#if FS_SUPPORT_TEST

/*********************************************************************
*
*       FS__ReadATEntry
*
*  Function description
*    Returns the value stored in an entry of the allocation table.
*/
I32 FS__ReadATEntry(const char * sVolumeName, U32 ClusterId) {
  int         r;
  FS_VOLUME * pVolume;
  int         MountType;

  FS_LOCK();
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    //
    // Mount the volume if necessary.
    //
    MountType = FS__AutoMount(pVolume);
    if (((unsigned)MountType & FS_MOUNT_R) != 0u) {
      FS_LOCK_DRIVER(&pVolume->Partition.Device);
      if (pVolume->MountType != 0u) {
        r = FS_READ_AT_ENTRY(pVolume, ClusterId);
      } else {
        FS_DEBUG_ERROROUT((FS_MTYPE_API, "FS__ReadATEntry: Volume has been unmounted by another task."));
        r = FS_ERRCODE_VOLUME_NOT_MOUNTED;
      }
      FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
    } else {
      if (MountType < 0) {
        r = MountType;
      } else {
        r = FS_ERRCODE_VOLUME_NOT_MOUNTED;
      }
    }
  } else {
    r = FS_ERRCODE_VOLUME_NOT_FOUND;
  }
  FS_UNLOCK();
  return r;
}

#endif // FS_SUPPORT_TEST

/*********************************************************************
*
*       FS__CheckDisk
*
*  Function description
*    Checks the consistency of the file system structure.
*
*  Parameters
*    sVolumeName        Name of the volume to be checked.
*    pBuffer            Work buffer to be used for checking the
*                       allocation table.
*    BufferSize         Size of the work buffer in bytes.
*                       It cannot be NULL.
*    MaxRecursionLevel  The maximum directory depth the function
*                       is allowed to checks.
*    pfOnError          Function that has to be called when an error
*                       is found. It cannot be NULL.
*
*  Return value
*    ==FS_CHECKDISK_RETVAL_OK           No errors found or the callback returned
*                                       FS_CHECKDISK_ACTION_DO_NOT_REPAIR.
*    ==FS_CHECKDISK_RETVAL_RETRY        An error has been found. The error has been corrected since
*                                       the callback function returned
*                                       FS_CHECKDISK_ACTION_SAVE_CLUSTERS or
*                                       FS_CHECKDISK_ACTION_DELETE_CLUSTERS.
*                                       FS_CheckDisk() has to be called again to check for the next error.
*   ==FS_CHECKDISK_RETVAL_ABORT         The application requested the abort of disk checking operation
*                                       through the callback returning FS_CHECKDISK_ACTION_ABORT.
*   ==FS_CHECKDISK_RETVAL_MAX_RECURSE   Maximum recursion level reached. The disk checking
*                                       operation has been aborted.
*   < 0                                 Error code indicating the failure reason.
*
*  Additional information
*    This is the internal version of FS_CheckDisk() that does not lock
*    the file system globally.
*/
int FS__CheckDisk(const char * sVolumeName, void * pBuffer, U32 BufferSize, int MaxRecursionLevel, FS_CHECKDISK_ON_ERROR_CALLBACK * pfOnError) {
  FS_VOLUME * pVolume;
  int         r;
  unsigned    MountType;
  unsigned    MountTypeSaved;

  r = FS_CHECKDISK_RETVAL_ABORT;
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    //
    // Save the mount type and restore it after the end of operation.
    //
    MountTypeSaved = pVolume->MountType;
    if (MountTypeSaved == 0u) {
      MountTypeSaved = pVolume->AllowAutoMount;
    }
    //
    // Determine how the volume has to be mounted for the checking operation.
    //
    MountType = pVolume->AllowAutoMount;
    if ((MountType == 0u) || (MountType == FS_MOUNT_RO)) {
      MountType = pVolume->MountType;
      if (MountType == 0u) {
        MountType = FS_MOUNT_RW;                    // Mount in write mode so that the errors can be corrected.
      }
    }
    //
    // Close all opened files and clear the caches.
    //
    FS__Unmount(pVolume);
    //
    // Explicitly mount the volume again, just in case the auto-mount feature is disabled.
    //
    (void)FS__Mount(pVolume, (U8)MountType);
    FS_LOCK_DRIVER(&pVolume->Partition.Device);
    //
    // No opened journal transactions are allowed during the disk checking operation
    // in order to prevent a possible damage of the file system.
    //
    r = _SuspendJournal(pVolume);
    if (r == 0) {
      //
      // Check the information on the storage medium.
      //
      r = FS_CHECK_VOLUME(pVolume, pBuffer, BufferSize, MaxRecursionLevel, pfOnError);
      //
      // Re-enable the journal.
      //
      (void)_ResumeJournal(pVolume);
    } else {
      //
      // Error, a disk checking operation cannot be started while a journal transaction is in progress.
      //
      FS_DEBUG_ERROROUT((FS_MTYPE_API, "FS_CheckDisk: Journal transaction in progress."));
    }
    FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
    //
    // Restore the mount type.
    //
    if (MountTypeSaved == 0u) {
      FS__Unmount(pVolume);
    } else {
      if (MountTypeSaved == FS_MOUNT_RO) {
        FS__Unmount(pVolume);
        (void)FS__Mount(pVolume, (U8)FS_MOUNT_RO);
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
*       FS_IsVolumeMounted
*
*  Function description
*    Checks if a volume is mounted.
*
*  Parameters
*    sVolumeName    Name of the volume to be queried.
*
*  Return value
*    ==1    Volume is mounted.
*    ==0    Volume is not mounted or does not exist.
*
*  Additional information
*    The function returns 1 if the volume is mounted either in
*    read-only mode or in read / write mode.
*/
int FS_IsVolumeMounted(const char * sVolumeName) {
  int         r;
  FS_VOLUME * pVolume;

  FS_LOCK();
  r = 0;
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    r = 1;
    if (pVolume->MountType == 0u) {
      r = 0;
    }
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_GetVolumeInfo
*
*  Function description
*    Returns information about a volume.
*
*  Parameters
*    sVolumeName  Name of the volume to query. It cannot be NULL.
*    pInfo        [OUT] Volume information. It cannot be NULL.
*
*  Return value
*    ==0      OK, information returned.
*    !=0      Error code indicating the failure reason.
*
*  Additional information
*    This function collects the information about the volume
*    such as volume size, available free space, format type, etc.
*/
int FS_GetVolumeInfo(const char * sVolumeName, FS_DISK_INFO * pInfo) {
  int r;

  FS_LOCK();
  r = FS__GetVolumeInfo(sVolumeName, pInfo);
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_GetVolumeInfoEx
*
*  Function description
*    Returns information about a volume. Identical to FS_GetVolumeInfo
*    except it gives the application more control about which type of
*    information should be returned.
*
*  Parameters
*    sVolumeName  The volume name.
*    pInfo        [OUT] Volume information.
*    Flags        Bit mask that controls what type of information the
*                 function returns. It is an or-combination of
*                 FS_DISKINFO_FLAG_... defines.
*
*  Return value
*    ==0      OK, information returned.
*    !=0      Error code indicating the failure reason.
*
*  Additional information
*    This function collects the information about the volume
*    such as volume size, available free space, format type, etc.
*    The kind of information is actually returned can be controlled
*    via the Flags parameter.
*/
int FS_GetVolumeInfoEx(const char * sVolumeName, FS_DISK_INFO * pInfo, int Flags) {
  int r;

  FS_LOCK();
  r = FS__GetVolumeInfoEx(sVolumeName, pInfo, Flags);
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_GetVolumeFreeSpace
*
*  Function description
*    Returns the free space available on a volume.
*
*  Parameters
*    sVolumeName    Name of the volume to be checked.
*
*  Return value
*    !=0    Number of bytes available on the volume.
*    ==0    An error occurred.
*
*  Additional information
*    This function returns the free space available to application
*    to store files and directories.
*
*    A free space larger than four Gbytes is reported as 0xFFFFFFFF
*    because this is the maximum value that can be represented in
*    an unsigned 32-bit integer. The function
*    FS_GetVolumeFreeSpaceKB() can be used instead if the available
*    free space is larger than four Gbytes.
*
*    FS_GetVolumeFreeSpace() can sill be reliably used if the
*    application does not need to know if there is more than four
*    GBytes of free space available.
*/
U32 FS_GetVolumeFreeSpace(const char * sVolumeName) {
  FS_DISK_INFO Info;
  U32       r;

  FS_LOCK();
  FS_PROFILE_CALL_STRING(FS_EVTID_GETVOLUMEFREESPACE, sVolumeName);
  r = 0;
  if (sVolumeName != NULL) {
    if (FS__GetVolumeInfo(sVolumeName, &Info) == 0) {
      r = FS__CalcSizeInBytes(Info.NumFreeClusters, Info.SectorsPerCluster, Info.BytesPerSector);
    }
  }
  FS_PROFILE_END_CALL_U32(FS_EVTID_GETVOLUMEFREESPACE, r);
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_GetVolumeFreeSpaceKB
*
*  Function description
*    Returns the free space available on a volume.
*
*  Parameters
*    sVolumeName    Name of the volume to be checked.
*
*  Return value
*    !=0    The space available on the volume in Kbytes.
*    ==0    An error occurred.
*
*  Additional information
*    This function returns the free space available to application
*    to store files and directories.
*/
U32 FS_GetVolumeFreeSpaceKB(const char * sVolumeName) {
  FS_DISK_INFO Info;
  U32       r;

  FS_LOCK();
  r = 0;
  if (sVolumeName != NULL) {
    if (FS__GetVolumeInfo(sVolumeName, &Info) == 0) {
      r = FS__CalcSizeInKB(Info.NumFreeClusters, Info.SectorsPerCluster, Info.BytesPerSector);
    }
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_GetVolumeSize
*
*  Function description
*    Returns the size of a volume.
*
*  Parameters
*    sVolumeName    Name of the volume to be queried.
*
*  Return value
*    !=0    Total number of bytes available to file system.
*    ==0    An error occurred.
*
*  Additional information
*    This function returns the total number of bytes available to
*    file system as data storage. The actual number of bytes available
*    to application for files and directories is smaller than
*    the value returned by FS_GetVolumeSize() since the file system
*    requires some space for the boot sector and the allocation table.
*
*    A size larger than four Gbytes is reported as 0xFFFFFFFF
*    because this is the maximum value that can be represented in
*    an unsigned 32-bit integer. The function
*    FS_GetVolumeSizeKB() can be used instead if the volume size is
*    larger than four Gbytes.
*/
U32 FS_GetVolumeSize(const char * sVolumeName) {
  FS_DISK_INFO Info;
  U32          r;

  r = 0;            // Error, failed to get volume information.
  FS_LOCK();
  if (sVolumeName != NULL) {
    if (FS__GetVolumeInfoEx(sVolumeName, &Info, 0) == 0) {
      r = FS__CalcSizeInBytes(Info.NumTotalClusters, Info.SectorsPerCluster, Info.BytesPerSector);
    }
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_GetVolumeSizeKB
*
*  Function description
*    Returns the size of a volume.
*
*  Parameters
*    sVolumeName    Name of the volume to be queried.
*
*  Return value
*    !=0    Storage available to file system in Kbytes.
*    ==0    An error occurred.
*
*  Additional information
*    This function returns the total number of bytes available to
*    file system as data storage. The actual number of bytes available
*    to application for files and directories is smaller than
*    the value returned by FS_GetVolumeSize() since the file system
*    requires some space for the boot sector and the allocation table.
*/
U32 FS_GetVolumeSizeKB(const char * sVolumeName) {
  FS_DISK_INFO Info;
  U32          r;

  r = 0;            // Error, failed to get volume information.
  FS_LOCK();
  if (sVolumeName != NULL) {
    if (FS__GetVolumeInfoEx(sVolumeName, &Info, 0) == 0) {
      r = FS__CalcSizeInKB(Info.NumTotalClusters, Info.SectorsPerCluster, Info.BytesPerSector);
    }
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_GetVolumeLabel
*
*  Function description
*    Returns the label of the volume.
*
*  Parameters
*    sVolumeName        Identifies the volume to be queried.
*                       It cannot be NULL.
*    sVolumeLabel       Buffer that receives the volume label.
*                       It cannot be NULL.
*    SizeOfVolumeLabel  Number of characters in pVolumeName buffer.
*
*  Return value
*    ==0     OK, volume label read.
*    !=0     Error code indicating the failure reason.
*
*  Additional information
*    The function stores at most SizeOfVolumeLabel - 1 bytes
*    to pVolumeLabel buffer. The returned volume label is 0-terminated.
*    The volume label is truncated if it contains more characters
*    than the number of characters that can be stored to pVolumeLabel.
*
*    EFS does not have a volume label. FS_GetVolumeLabel() returns
*    with an error if the application tries to read the volume
*    label of a volume formatted as EFS.
*/
int FS_GetVolumeLabel(const char * sVolumeName, char * sVolumeLabel, unsigned SizeOfVolumeLabel) {
  int         r;
  FS_VOLUME * pVolume;

  r = FS_ERRCODE_INVALID_PARA;
  FS_LOCK();
  if (sVolumeName != NULL) {
    if (sVolumeLabel != NULL) {
      pVolume = FS__FindVolume(sVolumeName);
      if (pVolume != NULL) {
        r = FS__AutoMount(pVolume);
        switch ((unsigned)r) {
        case FS_MOUNT_RO:
          // through
        case FS_MOUNT_RW:
          FS_LOCK_DRIVER(&pVolume->Partition.Device);
          r = FS_GET_VOLUME_LABEL(pVolume, sVolumeLabel, SizeOfVolumeLabel);
          FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
          break;
        case 0:
          r = FS_ERRCODE_VOLUME_NOT_MOUNTED;
          break;
        default:
          //
          // An error occurred during the mount operation.
          //
          break;
        }
      } else {
        r = FS_ERRCODE_VOLUME_NOT_FOUND;
      }
    }
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_SetVolumeLabel
*
*  Function description
*    Modifies the label of a volume.
*
*  Parameters
*    sVolumeName      Name of the volume for which the label has to
*                     be modified. It cannot be NULL.
*    sVolumeLabel     Volume label as 0-terminated ASCII string.
*                     The volume label is deleted if set to NULL.
*
*  Return value
*    ==0     OK, volume label set.
*    !=0     Error code indicating the failure reason.
*
*  Additional information
*    The volume label is a ASCII string that can be assigned to a
*    volume. It is not evaluated by the file system and it cannot
*    be used as volume name. A Windows PC shows the volume label
*    of a removable storage device in the "Computer" window when it
*    is mounted via USB MSD. The volume label can be read via
*    FS_GetVolumeLabel().
*
*    The volume label of a FAT-formatted volume can contain at
*    most 11 characters. The following characters are not allowed
*    in a volume name: '"', '&', '*', '+', '-', ',', '.', '/',
*    ':', ';', '<', '=', '>', '?', '[', ']', '\\'.
*
*    EFS does not have a volume label. FS_GetVolumeLabel() returns
*    with an error if the application tries to read the volume
*    label of a volume formatted as EFS.
*/
int FS_SetVolumeLabel(const char * sVolumeName, const char * sVolumeLabel) {
  int         r;
  FS_VOLUME * pVolume;

  FS_LOCK();
  if (sVolumeName != NULL) {
    pVolume = FS__FindVolume(sVolumeName);
    if (pVolume != NULL) {
      r = FS__AutoMount(pVolume);
      switch ((unsigned)r) {
      case FS_MOUNT_RW:
        FS_LOCK_DRIVER(&pVolume->Partition.Device);
#if FS_SUPPORT_JOURNAL
        r = _SetVolumeLabelFS(pVolume, sVolumeLabel);
#else
        r = FS_SET_VOLUME_LABEL(pVolume, sVolumeLabel);
#endif // FS_SUPPORT_JOURNAL
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
        // An error occurred during the mount operation.
        //
        break;
      }
    } else {
      r = FS_ERRCODE_VOLUME_NOT_FOUND;
    }
  } else {
    r = FS_ERRCODE_INVALID_PARA;
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_Mount
*
*  Function description
*    Initializes a volume in default access mode.
*
*  Parameters
*    sVolumeName    The name of a volume. If the empty string is specified,
*                   the first device in the volume table is used.
*
*  Return value
*    ==0            Volume is not mounted.
*    ==FS_MOUNT_RO  Volume is mounted read only.
*    ==FS_MOUNT_RW  Volume is mounted read/write.
*    < 0            Error code indicating the failure reason.
*                   Refer to FS_ErrorNo2Text().
*
*  Additional information
*    The storage device has to be mounted before being accessed for the
*    first time after file system initialization. The file system is
*    configured by default to automatically mount the storage device
*    at the first access in read / write mode. This function can be used to
*    explicitly mount the storage device if the automatic mount behavior
*    has been disabled via FS_SetAutoMount(). Refer to FS_SetAutoMount()
*    for an overview of the different automatic mount types.
*/
int FS_Mount(const char * sVolumeName) {
  FS_VOLUME * pVolume;
  int         r;

  FS_LOCK();
  r       = FS_ERRCODE_VOLUME_NOT_FOUND;    // Set as error so far.
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    r = FS__Mount(pVolume, (U8)FS_MOUNT_RW);
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_MountEx
*
*  Function description
*    Initializes a volume in a specified access mode.
*
*  Parameters
*    sVolumeName    The name of the volume. If the empty string is specified,
*                   the first device in the volume table is used.
*    MountType      Indicates how the volume has to be mounted.
*                   * FS_MOUNT_RO   Read only access.
*                   * FS_MOUNT_RW   Read / write access.
*
*  Return value
*    ==0            Volume is not mounted.
*    ==FS_MOUNT_RO  Volume is mounted read only.
*    ==FS_MOUNT_RW  Volume is mounted read/write.
*     <0            Error code indicating the failure reason.
*                   Refer to FS_ErrorNo2Text().
*
*  Additional information
*    Performs the same operation as FS_Mount() while it allows the
*    application to specify how the storage device has to be mounted.
*/
int FS_MountEx(const char * sVolumeName, U8 MountType) {
  FS_VOLUME * pVolume;
  int         r;

  FS_LOCK();
  r       = FS_ERRCODE_VOLUME_NOT_FOUND;    // Set as error so far.
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    r = FS__Mount(pVolume, MountType);
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_IsHLFormatted
*
*  Function description
*    Checks if a volume is high-level formatted or not.
*
*  Parameters
*    sVolumeName    Name of the volume to be checked.
*
*  Return value
*    ==1      Volume is formatted.
*    ==0      Volume is not formatted.
*    < 0      Error code indicating the failure reason.
*
*  Additional information
*    This function can be use to determine if the format of a volume
*    is supported by the file system. If the volume format is unknown
*    the function returns 0.
*/
int FS_IsHLFormatted(const char * sVolumeName) {
  FS_VOLUME * pVolume;
  int         r;

  FS_LOCK();
  r = FS_ERRCODE_VOLUME_NOT_FOUND;          // Set as error so far.
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    r = FS__AutoMount(pVolume);
    if (r > 0) {
      r = 1;                                // Volume formatted.
    } else {
      if (r == FS_ERRCODE_INVALID_FS_FORMAT) {
        r = 0;                              // Volume not formatted.
      }
    }
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_CheckDisk
*
*  Function description
*    Checks the consistency of the file system structure.
*
*  Parameters
*    sVolumeName        Name of the volume to be checked.
*    pBuffer            Work buffer to be used for checking the
*                       allocation table.
*    BufferSize         Size of the work buffer in bytes.
*                       It cannot be NULL.
*    MaxRecursionLevel  The maximum directory depth the function
*                       is allowed to check.
*    pfOnError          Function that has to be called when an error
*                       is found. It cannot be NULL.
*
*  Return value
*    ==FS_CHECKDISK_RETVAL_OK           No errors found or the callback returned
*                                       FS_CHECKDISK_ACTION_DO_NOT_REPAIR.
*    ==FS_CHECKDISK_RETVAL_RETRY        An error has been found. The error has been corrected since
*                                       the callback function returned
*                                       FS_CHECKDISK_ACTION_SAVE_CLUSTERS or
*                                       FS_CHECKDISK_ACTION_DELETE_CLUSTERS.
*                                       FS_CheckDisk() has to be called again to check for the next error.
*   ==FS_CHECKDISK_RETVAL_ABORT         The application requested the abort of disk checking operation
*                                       through the callback returning FS_CHECKDISK_ACTION_ABORT.
*   ==FS_CHECKDISK_RETVAL_MAX_RECURSE   Maximum recursion level reached. The disk checking
*                                       operation has been aborted.
*   < 0                                 Error code indicating the failure reason.
*
*  Additional information
*    This function can be used to check if any errors are present
*    on a specific volume and, if necessary, to repair these errors.
*    Ideally, the work buffer has to be large enough to store the
*    usage information of all the clusters in the allocation table.
*    FS_CheckDisk() uses one bit to store the usage state of a cluster.
*    The typical size of the work buffer is about 2 KBytes.
*    Additional iterations are performed if the work buffer
*    is not large enough to check the whole allocation table
*    in one step.
*
*    FS_CheckDisk() can detect and correct the following file system
*    errors:
*      * Invalid directory entries.
*      * Lost clusters or cluster chains.
*      * Cross-linked clusters.
*      * Clusters are associated to a file with size of 0.
*      * Too few clusters are allocated to a file.
*      * Cluster is not marked as end-of-chain, although it should be.
*
*    The contents of a lost cluster chain is saved during the repair
*    operation to files named \file{FILE<FileIndex>.CHK} that are stored
*    in directories named \file{FOUND.<DirIndex>}. \file FileIndex is
*    a 0-based 4-digit decimal number that is incremented by one for
*    each cluster chain saved. \file DirIndex is a 0-based 3-digit
*    decimal number that is incremented by one each time FS_CheckDisk()
*    is called. For example the first created directory has the
*    name \file{FOUND.000}, the second \file{FOUND.001}, and so on
*    while the first file in the directory has the name \file{FILE0000.CHK},
*    the second \file{FILE0001.CHK}, and so on.
*
*    The callback function is used to notify the application about
*    the errors found by FS_CheckDisk() during the disk checking
*    operation. FS_CheckDisk() uses the return value of the callback
*    function to decide if the error hast to be repaired or not.
*    For more information refer to FS_CHECKDISK_ON_ERROR_CALLBACK.
*
*    FS_CheckDisk() closes all opened files before it starts the
*    disk checking operation. The application is not allowed to
*    access the storage device from a different task as long as
*    the operation is in progress.
*/
int FS_CheckDisk(const char * sVolumeName, void * pBuffer, U32 BufferSize, int MaxRecursionLevel, FS_CHECKDISK_ON_ERROR_CALLBACK * pfOnError) {
  int r;

  //
  // Validate parameters.
  //
  if ((sVolumeName == NULL) || (pBuffer == NULL) || (pfOnError == NULL) || (MaxRecursionLevel < 0)) {
    return FS_ERRCODE_INVALID_PARA;
  }
  //
  // Perform the operation.
  //
  FS_LOCK();
  r = FS__CheckDisk(sVolumeName, pBuffer, BufferSize, MaxRecursionLevel, pfOnError);
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_SetAutoMount
*
*  Function description
*    Sets the automatic mount behavior.
*
*  Parameters
*    sVolumeName  Pointer to a string containing the name of the volume.
*                 If the empty string is specified, the first device in
*                 the volume table is used.
*    MountType    Indicates how the volume has to be mounted.
*                 * FS_MOUNT_RO   Allows to automatically mount the
*                                 volume in read only mode.
*                 * FS_MOUNT_RW   Allows to automatically mount the
*                                 volume in read / write mode.
*                 * 0             Disables the automatic mount operation
*                                 for the volume.
*
*  Additional information
*    By default, the file system is configured to automatically mount
*    all volumes in read / write mode and this function can be used to
*    change the default automatic mount type or to disable the automatic
*    mounting.
*/
void FS_SetAutoMount(const char * sVolumeName, U8 MountType) {
  FS_VOLUME * pVolume;

  FS_LOCK();
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    FS_LOCK_SYS();
    pVolume->AllowAutoMount = MountType;
    FS_UNLOCK_SYS();
  }
  FS_UNLOCK();
}

/*********************************************************************
*
*       FS_Sync
*
*  Function description
*    Saves cached information to storage.
*
*  Parameters
*    sVolumeName  Pointer to a string containing the name of the volume
*                 to be synchronized. If the empty string is specified,
*                 the first configured volume is used.
*
*  Return value
*    ==0      OK, volume synchronized
*    !=0      Error code indicating the failure reason.
*             Refer to FS_ErrorNo2Text() for more information.
*
*  Additional information
*    The function write the contents of write buffers and updates
*    the management information of all opened file handles to storage
*    device. All the file handles are left open. If configured,
*    FS_Sync() also writes to storage the changes present in the write
*    cache and in the journal. FS_Sync() can be called from the same
*    task as the one writing data or from a different task.
*/
int FS_Sync(const char * sVolumeName) {
  FS_VOLUME * pVolume;
  int         r;

  FS_LOCK();
  if (sVolumeName != NULL) {
    pVolume = FS__FindVolume(sVolumeName);
    if (pVolume != NULL) {
      r = FS__Sync(pVolume);
    } else {
      r = FS_ERRCODE_VOLUME_NOT_FOUND;    // Error, volume was not found.
    }
  } else {
    r = FS_ERRCODE_INVALID_PARA;          // Error, volume not specified.
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_FreeSectors
*
*  Function description
*    Informs the device driver about unused sectors.
*
*  Parameters
*    sVolumeName    Name of the volume on which to perform the operation.
*
*  Return value
*    ==0    OK, sectors have been freed.
*    !=0    Error code indicating the failure reason.
*
*  Additional information
*    The function visits each entry of the allocation table and checks
*    if the cluster is used to store data. If the cluster is free,
*    informs the storage layer that the sectors assigned to the cluster
*    do not store valid data. This information is used by the NAND and
*    NOR device drivers to optimize the internal wear-leveling process.
*
*    To use FS_FreeSectors() the support for free sector operation
*    has to be enabled in the file system, that is FS_SUPPORT_FREE_SECTOR
*    has to be set to 1. The function does nothing if
*    FS_SUPPORT_FREE_SECTOR is set to 0.
*
*    This function is optional. The file system informs automatically
*    the device drivers about unused sectors.
*/
int FS_FreeSectors(const char * sVolumeName) {
  FS_VOLUME * pVolume;
  int         r;

  r = FS_ERRCODE_INVALID_PARA;
  FS_LOCK();
  if (sVolumeName != NULL) {
    pVolume = FS__FindVolume(sVolumeName);
    if (pVolume != NULL) {
      r = FS__AutoMount(pVolume);
      switch ((unsigned)r) {
      case FS_MOUNT_RW:
        FS_LOCK_DRIVER(&pVolume->Partition.Device);
#if FS_SUPPORT_JOURNAL
        r = _FreeSectorsFS(pVolume);
#else
        r = FS_FREE_SECTORS(pVolume);
#endif // FS_SUPPORT_JOURNAL
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
        // An error occurred during the mount operation.
        //
        break;
      }
    } else {
      r = FS_ERRCODE_VOLUME_NOT_FOUND;
    }
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_GetVolumeFreeSpaceFirst
*
*  Function description
*    Initiates the search for free space.
*
*  Parameters
*    pFSD           Context of the free space search.
*    sVolumeName    Name of the volume to search on.
*    pBuffer        Work buffer.
*    SizeOfBuffer   Size of the work buffer in bytes.
*
*  Return value
*    ==1     OK, the entire allocation table has been searched.
*    ==0     OK, search is not completed.
*    < 0     Error code indicating the failure reason.
*
*  Additional information
*    FS_GetVolumeFreeSpaceFirst() together with FS_GetVolumeFreeSpaceNext()
*    can be used to calculate the amount of available free space on a volume.
*    This pair of functions implement the same functionality as
*    FS_GetVolumeFreeSpace() with the difference that they block the access
*    to the file system for a very short time.
*
*    Typically, FS_GetVolumeFreeSpace() uses the information stored to
*    the FSInfo sector of a FAT file system or to the Status sector of
*    an EFS file system to get the number of free clusters on the volume.
*    If this information is not available then FS_GetVolumeFreeSpace()
*    calculates the free space by reading and evaluating the entire
*    allocation table. The size of the allocation table depends on the
*    size of the volume with sizes of a few tens of Mbytes for a volume
*    pf 4 Gbytes or larger. Reading such amount of data can take a
*    relatively long time to complete during which the application
*    is not able to access the file system. FS_GetVolumeFreeSpaceFirst()
*    and FS_GetVolumeFreeSpaceNext() can be used in such cases by
*    calculating the available free space while the application is
*    performing other file system accesses.
*
*    FS_GetVolumeFreeSpaceFirst() is used by the application to initiate
*    the search process followed by one or more calls to
*    FS_GetVolumeFreeSpaceNext(). The free space is returned in the
*    NumClustersFree member of pFSD. The cluster size can be determined
*    by calling FS_GetVolumeInfoEx() with the Flags parameter set to 0.
*
*    The specified work buffer is used by the file system for the read
*    operations only when FS_SUPPORT_SECTOR_BUFFER_BURST is set to 1.
*    The file system is able to use the work buffer for burst operations
*    only if pBuffer is aligned according to the value configured via
*    FS_DRIVER_ALIGNMENT.
*/
int FS_GetVolumeFreeSpaceFirst(FS_FREE_SPACE_DATA * pFSD, const char * sVolumeName, void * pBuffer, int SizeOfBuffer) {
  FS_VOLUME  * pVolume;
  int          r;
  int          MountType;
  U32          NumClustersFree;
  U32          NumClustersChecked;
  U32          FirstClusterId;
  FS_AT_INFO   atInfo;

  if ((pBuffer != NULL) && (SizeOfBuffer < 0)) {
    return FS_ERRCODE_INVALID_PARA;                     // Invalid work buffer.
  }
  r = FS_ERRCODE_INVALID_PARA;
  FS_LOCK();
  if ((pFSD != NULL) && (sVolumeName != NULL)) {
    pVolume = FS__FindVolume(sVolumeName);
    if (pVolume != NULL) {
      //
      // Mount the volume if necessary.
      //
      MountType = FS__AutoMount(pVolume);
      if (((unsigned)MountType & FS_MOUNT_R) != 0u) {
        FS_MEMSET(pFSD, 0, sizeof(FS_FREE_SPACE_DATA));
        pFSD->SizeOfBuffer = SizeOfBuffer;
        pFSD->pBuffer      = pBuffer;
        pFSD->pVolume      = pVolume;
        FS_LOCK_DRIVER(&pVolume->Partition.Device);
        if (pVolume->MountType != 0u) {
          FS_MEMSET(&atInfo, 0, sizeof(atInfo));
          r = FS_GET_AT_INFO(pVolume, &atInfo);         // Get information about the allocation table.
          if (r == 0) {
            NumClustersFree    = 0;
            NumClustersChecked = 0;
            FirstClusterId     = atInfo.FirstClusterId;
            r = FS_GET_FREE_SPACE(pVolume, pBuffer, SizeOfBuffer, FirstClusterId, &NumClustersFree, &NumClustersChecked);
            if (r >= 0) {
              FirstClusterId += NumClustersChecked;
              pFSD->FirstClusterId  = FirstClusterId;
              pFSD->NumClustersFree = NumClustersFree;
            }
          } else {
            r = FS_ERRCODE_NOT_SUPPORTED;
          }
        } else {
          FS_DEBUG_ERROROUT((FS_MTYPE_API, "FS_GetVolumeFreeSpaceFirst: Volume has been unmounted by another task."));
          r = FS_ERRCODE_VOLUME_NOT_MOUNTED;
        }
        FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
      } else {
        if (MountType < 0) {
          r = MountType;
        } else {
          r = FS_ERRCODE_VOLUME_NOT_MOUNTED;
        }
      }
    } else {
      r = FS_ERRCODE_VOLUME_NOT_FOUND;
    }
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_GetVolumeFreeSpaceNext
*
*  Function description
*    Continues the search for free space.
*
*  Parameters
*    pFSD           Context of the free space search.
*
*  Return value
*    ==1     OK, the entire allocation table has been searched.
*    ==0     OK, search is not completed.
*    < 0     Error code indicating the failure reason.
*
*  Additional information
*    pFSD has to be initialized via a call to FS_GetVolumeFreeSpaceFirst().
*    One or more calls to FS_GetVolumeFreeSpaceNext() are required
*    in order to determine the available free space. For more information
*    refer to FS_GetVolumeFreeSpaceFirst().
*/
int FS_GetVolumeFreeSpaceNext(FS_FREE_SPACE_DATA * pFSD) {
  FS_VOLUME * pVolume;
  int         r;
  U32         NumClustersFree;
  U32         NumClustersChecked;
  U32         FirstClusterId;
  void      * pBuffer;
  int         SizeOfBuffer;

  r = FS_ERRCODE_INVALID_PARA;
  FS_LOCK();
  if (pFSD != NULL) {
    pVolume = pFSD->pVolume;
    if (pVolume != NULL) {
      FS_LOCK_DRIVER(&pVolume->Partition.Device);
      if (pVolume->MountType != 0u) {
        NumClustersChecked = 0;
        FirstClusterId  = pFSD->FirstClusterId;
        pBuffer         = pFSD->pBuffer;
        SizeOfBuffer    = pFSD->SizeOfBuffer;
        NumClustersFree = pFSD->NumClustersFree;
        r = FS_GET_FREE_SPACE(pVolume, pBuffer, SizeOfBuffer, FirstClusterId, &NumClustersFree, &NumClustersChecked);
        if (r >= 0) {
          FirstClusterId += NumClustersChecked;
          pFSD->FirstClusterId  = FirstClusterId;
          pFSD->NumClustersFree = NumClustersFree;
        }
      } else {
        FS_DEBUG_ERROROUT((FS_MTYPE_API, "FS_GetVolumeFreeSpaceNext: Volume has been unmounted by another task."));
        r = FS_ERRCODE_VOLUME_NOT_MOUNTED;
      }
      FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
    }
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_InitCheck
*
*  Function description
*    Initializes a non-blocking disk checking operation.
*
*  Parameters
*    pCheckData     [OUT] Checking context. It cannot be NULL.
*    sVolumeName    Name of the volume on which the checking is performed.
*                   It cannot be NULL.
*    pBuffer        [IN] Working buffer. It cannot be NULL.
*    BufferSize     Number of bytes in pBuffer.
*    pfOnError      Callback function to be invoked in case of a file
*                   system damage. It cannot be NULL.
*
*  Return value
*    ==FS_CHECKDISK_RETVAL_OK     OK, disk checking has been initialized.
*    < 0                          Error code indicating the failure reason.
*
*  Additional information
*    FS_InitCheck() has to be called in combination with FS_CheckDir()
*    and FS_CheckAT() to check the consistency of the file system.
*    These functions perform the same operation as FS_CheckDisk()
*    with the difference that the access to the file system is
*    blocked only for a limited period of time.
*
*    It is mandatory to call FS_InitCheck() before any call to
*    FS_CheckDir() or FS_CheckAT(). The purpose of FS_InitCheck() is to
*    prepare the checking context pCheckData for the operation.
*    This context must be passed to FS_CheckDir() and FS_CheckAT().
*
*    The parameters sVolumeName, pBuffer, BufferSize and pfOnError
*    have the same meaning as the corresponding parameters of FS_CheckDisk().
*    Refer to FS_CheckDisk() for more information about the usage of these
*    parameters.
*
*    In a typical usage the application calls FS_InitCheck() once followed
*    by multiple calls to FS_CheckDir() and FS_CheckAT(). In addition,
*    each time FS_CheckDir() and FS_CheckAT() find and correct an error
*    FS_InitCheck() has to be called again to re-initialize the operation.
*/
int FS_InitCheck(FS_CHECK_DATA * pCheckData, const char * sVolumeName, void * pBuffer, U32 BufferSize, FS_CHECKDISK_ON_ERROR_CALLBACK * pfOnError) {
  int              r;
  FS_VOLUME      * pVolume;
  FS_CLUSTER_MAP * pClusterMap;
  U32              NumClusters;
  U32              NumClustersAtOnce;
  FS_AT_INFO       atInfo;

  //
  // Validate parameters.
  //
  if ((pCheckData == NULL) || (sVolumeName == NULL) || (pBuffer == NULL) || (pfOnError == NULL)) {
    FS_DEBUG_ERROROUT((FS_MTYPE_API, "FS_InitCheck: Invalid parameter(s)."));
    return FS_ERRCODE_INVALID_PARA;
  }
  //
  // Get information from the file system layer and initialize the context.
  //
  r = FS_ERRCODE_VOLUME_NOT_FOUND;
  FS_LOCK();
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    r = _MountSyncIfRequired(pVolume);
    if (r == 0) {
      FS_LOCK_DRIVER(&pVolume->Partition.Device);
      if (pVolume->MountType != 0u) {
        FS_MEMSET(&atInfo, 0, sizeof(atInfo));
        r = FS_GET_AT_INFO(pVolume, &atInfo);         // Get information about the allocation table.
        if (r == 0) {
          NumClusters       = atInfo.NumClusters;
          NumClustersAtOnce = BufferSize << 3;        // We can store information about 8 clusters in a byte.
          if (NumClustersAtOnce > NumClusters) {
            NumClustersAtOnce = NumClusters;
          }
          pCheckData->pVolume    = pVolume;
          pCheckData->pfOnError  = pfOnError;
          pCheckData->WriteCntAT = atInfo.WriteCnt;
          FS_MEMSET(pBuffer, 0, BufferSize);
          pClusterMap = &pCheckData->ClusterMap;
          pClusterMap->FirstClusterId = atInfo.FirstClusterId;
          pClusterMap->pData          = SEGGER_PTR2PTR(U8, pBuffer);
          pClusterMap->NumClusters    = (I32)NumClustersAtOnce;
        }
      } else {
        FS_DEBUG_ERROROUT((FS_MTYPE_API, "FS_InitCheck: Volume has been unmounted by another task."));
        r = FS_ERRCODE_VOLUME_NOT_MOUNTED;
      }
      FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
    }
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_CheckDir
*
*  Function description
*    Verifies the consistency of a single directory.
*
*  Parameters
*    pCheckData     [IN] Checking context. It cannot be NULL.
*    sPath          Path to the directory to be checked. It cannot be NULL.
*
*  Return value
*    ==FS_CHECKDISK_RETVAL_OK           No errors found or the callback returned
*                                       FS_CHECKDISK_ACTION_DO_NOT_REPAIR.
*    ==FS_CHECKDISK_RETVAL_RETRY        An error has been found. The error has been corrected since
*                                       the callback function returned
*                                       FS_CHECKDISK_ACTION_SAVE_CLUSTERS or
*                                       FS_CHECKDISK_ACTION_DELETE_CLUSTERS.
*                                       FS_CheckDir() has to be called again to check for the next error.
*   ==FS_CHECKDISK_RETVAL_ABORT         The application requested the abort of disk checking operation
*                                       through the callback returning FS_CHECKDISK_ACTION_ABORT.
*   ==FS_CHECKDISK_RETVAL_MAX_RECURSE   Maximum recursion level reached. The disk checking
*                                       operation has been aborted.
*   < 0                                 Error code indicating the failure reason.
*
*  Additional information
*    FS_CheckDir() has to be called in combination with FS_InitCheck()
*    and FS_CheckAT() to check the consistency of the file system.
*
*    pCheckData has to be initialized via a call to FS_InitCheck().
*    In order to check the entire file system structure, the application
*    will have to call FS_CheckDir() with every different directory path
*    present on the file system.
*/
int FS_CheckDir(FS_CHECK_DATA * pCheckData, const char * sPath) {
  int                              r;
  FS_VOLUME                      * pVolume;
  FS_AT_INFO                       atInfo;
  FS_CLUSTER_MAP                 * pClusterMap;
  FS_CHECKDISK_ON_ERROR_CALLBACK * pfOnError;
  int                              Result;

  //
  // Validate parameters.
  //
  if ((pCheckData == NULL) || (sPath == NULL)) {
    FS_DEBUG_ERROROUT((FS_MTYPE_API, "FS_CheckDir: Invalid parameter(s)."));
    return FS_ERRCODE_INVALID_PARA;
  }
  r = FS_ERRCODE_VOLUME_NOT_FOUND;
  pVolume = pCheckData->pVolume;
  FS_LOCK();
  if (pVolume != NULL) {
    //
    // Make sure that the cached data is written to storage.
    //
    r = _MountSyncIfRequired(pVolume);
    if (r == 0) {
      FS_LOCK_DRIVER(&pVolume->Partition.Device);
      //
      // Make sure that no other task unmounted the volume.
      //
      if (pVolume->MountType != 0u) {
        //
        // Get information about the allocation table.
        //
        FS_MEMSET(&atInfo, 0, sizeof(atInfo));
        r = FS_GET_AT_INFO(pVolume, &atInfo);
        if (r == 0) {
          //
          // Make sure that the journal is disabled during the checking.
          //
          r = _SuspendJournal(pVolume);
          if (r == 0) {
            //
            // Restart the checking if the allocation table has
            // been modified since the start of the operation
            // that is last call to FS_InitCheck().
            //
            r = FS_CHECKDISK_RETVAL_RETRY;
            if (atInfo.WriteCnt == pCheckData->WriteCntAT) {
              pClusterMap = &pCheckData->ClusterMap;
              pfOnError   = pCheckData->pfOnError;
              r = FS_CHECK_DIR(pVolume, sPath, pClusterMap, pfOnError);
              FS_MEMSET(&atInfo, 0, sizeof(atInfo));
              Result = FS_GET_AT_INFO(pVolume, &atInfo);
              if (Result == 0) {
                //
                // Update the number of write operations performed
                // to the allocation table for the case the checking
                // operation modified the allocation table.
                //
                pCheckData->WriteCntAT = atInfo.WriteCnt;
              }
            }
            (void)_ResumeJournal(pVolume);
          } else {
            if (r == FS_ERRCODE_INVALID_USAGE) {
              //
              // Retry the disk checking operation if a journal transaction is active.
              //
              r = FS_CHECKDISK_RETVAL_RETRY;
            }
          }
        }
      } else {
        FS_DEBUG_ERROROUT((FS_MTYPE_API, "FS_CheckDir: Volume has been unmounted by another task."));
        r = FS_ERRCODE_VOLUME_NOT_MOUNTED;
      }
      FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
    }
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_CheckAT
*
*  Function description
*    Verifies the consistency of the allocation table.
*
*  Parameters
*    pCheckData     [IN] Checking context. It cannot be NULL.
*
*  Return value
*    ==FS_CHECKDISK_RETVAL_OK           No errors found or the callback returned
*                                       FS_CHECKDISK_ACTION_DO_NOT_REPAIR.
*    ==FS_CHECKDISK_RETVAL_RETRY        An error has been found. The error has been corrected since
*                                       the callback function returned
*                                       FS_CHECKDISK_ACTION_SAVE_CLUSTERS or
*                                       FS_CHECKDISK_ACTION_DELETE_CLUSTERS.
*                                       FS_CheckDir() has to be called again to check for the next error.
*   ==FS_CHECKDISK_RETVAL_ABORT         The application requested the abort of disk checking operation
*                                       through the callback returning FS_CHECKDISK_ACTION_ABORT.
*   ==FS_CHECKDISK_RETVAL_CONTINUE      Indicates that not all the allocation table has been checked.
*                                       The entire volume has to be checked again using FS_CheckDir().
*   < 0                                 Error code indicating the failure reason.
*
*  Additional information
*    FS_CheckAT() has to be called in combination with FS_InitCheck()
*    and FS_CheckDir() to check the consistency of the file system.
*
*    pCheckData has to be initialized via a call to FS_InitCheck().
*/
int FS_CheckAT(FS_CHECK_DATA * pCheckData) {
  int                              r;
  FS_VOLUME                      * pVolume;
  FS_AT_INFO                       atInfo;
  FS_CLUSTER_MAP                 * pClusterMap;
  FS_CHECKDISK_ON_ERROR_CALLBACK * pfOnError;
  U32                              NumClustersToCheck;
  U32                              NumClustersChecked;
  U32                              LastClusterIdChecked;
  U32                              LastClusterId;
  U32                              NumBytes;
  int                              Result;

  //
  // Validate parameters.
  //
  if (pCheckData == NULL) {
    FS_DEBUG_ERROROUT((FS_MTYPE_API, "FS_CheckAT: Invalid parameter."));
    return FS_ERRCODE_INVALID_PARA;
  }
  r = FS_ERRCODE_VOLUME_NOT_FOUND;
  pVolume = pCheckData->pVolume;
  FS_LOCK();
  if (pVolume != NULL) {
    //
    // Make sure that the cached data is written to storage.
    //
    r = _MountSyncIfRequired(pVolume);
    if (r == 0) {
      FS_LOCK_DRIVER(&pVolume->Partition.Device);
      //
      // Make sure that no other task unmounted the volume.
      //
      if (pVolume->MountType != 0u) {
        //
        // Get information about the allocation table.
        //
        FS_MEMSET(&atInfo, 0, sizeof(atInfo));
        r = FS_GET_AT_INFO(pVolume, &atInfo);
        if (r == 0) {
          //
          // Make sure that the journal is disabled during the checking.
          //
          r = _SuspendJournal(pVolume);
          if (r == 0) {
            //
            // Restart the checking if the allocation table has
            // been modified since the start of the operation
            // that is last call to FS_InitCheck().
            //
            r = FS_CHECKDISK_RETVAL_RETRY;
            if (atInfo.WriteCnt == pCheckData->WriteCntAT) {
              pClusterMap = &pCheckData->ClusterMap;
              pfOnError   = pCheckData->pfOnError;
              r = FS_CHECK_AT(pVolume, pClusterMap, pfOnError);
              if (r == 0) {
                //
                // Check if the entire allocation table has been checked.
                // If not, request a new check for the next range of clusters.
                //
                NumClustersChecked   = (U32)pClusterMap->NumClusters;
                LastClusterIdChecked = (pClusterMap->FirstClusterId + NumClustersChecked) - 1u;
                LastClusterId        = (atInfo.FirstClusterId + atInfo.NumClusters) - 1u;
                if (LastClusterIdChecked < LastClusterId) {
                  NumClustersToCheck = LastClusterId - LastClusterIdChecked;
                  NumClustersToCheck = SEGGER_MIN(NumClustersToCheck, NumClustersChecked);
                  pClusterMap->NumClusters    = (I32)NumClustersToCheck;
                  pClusterMap->FirstClusterId = LastClusterIdChecked + 1u;
                  NumBytes = (NumClustersToCheck + 8u - 1u) >> 3;       // The status of 8 clusters is stored in a byte.
                  FS_MEMSET(pClusterMap->pData, 0, NumBytes);
                  r = FS_CHECKDISK_RETVAL_CONTINUE;
                }
                Result = FS_GET_AT_INFO(pVolume, &atInfo);
                if (Result == 0) {
                  //
                  // Update the number of write operations performed
                  // to the allocation table for the case the checking
                  // operation modified the allocation table.
                  //
                  pCheckData->WriteCntAT = atInfo.WriteCnt;
                }
              }
            }
            (void)_ResumeJournal(pVolume);
          } else {
            if (r == FS_ERRCODE_INVALID_USAGE) {
              //
              // Retry the disk checking operation if a journal transaction is active.
              //
              r = FS_CHECKDISK_RETVAL_RETRY;
            }
          }
        }
      } else {
        FS_DEBUG_ERROROUT((FS_MTYPE_API, "FS_CheckAT: Volume has been unmounted by another task."));
        r = FS_ERRCODE_VOLUME_NOT_MOUNTED;
      }
      FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
    }
  }
  FS_UNLOCK();
  return r;
}

#if FS_SUPPORT_VOLUME_ALIAS

/*********************************************************************
*
*       FS_SetVolumeAlias
*
*  Function description
*    Assigns an alternative name for a volume.
*
*  Parameters
*    sVolumeName      [IN] Name of the volume to which the alternative
*                     name has to be assigned. It cannot be set to NULL.
*    sVolumeAlias     [IN] Alternative name as 0-terminated string.
*                     Can be set to NULL.
*
*  Return value
*    ==0     OK, the alternative name has been assigned.
*    < 0     Error code indicating the failure reason.
*
*  Additional information
*    The assigned alias can be used as replacement in any path
*    to a file or directory that contains a volume name and
*    it can be passed instead of a volume name to any API function
*    that requires one. The alias replaces the volume and the unit number.
*    When used as a volume name the volume separator character (':')
*    has to be added to the end of the alias otherwise it is not recognized
*    as a valid volume name by the file system.
*
*    Valid characters in an alias are ASCII capital and small letters,
*    digits and the underscore character. The comparison applied
*    to alias is case sensitive and is performed after the file system
*    checks the real volume names.
*
*    The alias name is copied to the internal instance of the volume.
*    The function fails with an error if the alias is longer that the
*    space available in the internal buffer. The size of the internal
*    buffer for the alias can be configured at compile time via
*    FS_MAX_LEN_VOLUME_ALIAS. The alias can be removed by either
*    passing a NULL or an empty string as sVolumeAlias parameter.
*    If FS_MAX_LEN_VOLUME_ALIAS is set to 0 then only the pointer to
*    the volume alias is stored. The application has to make sure that
*    the memory region that stores the volume alias remains valid
*    until the file system is deinitialized via FS_DeInit().
*
*    This function is available only when FS_SUPPORT_VOLUME_ALIAS is set
*    to 1.
*
*    The alias of a volume can be queried either via FS_GetVolumeInfo()
*    or FS_GetVolumeInfoEx(). The sAlias member of the FS_DISK_INFO structure
*    stores the configured alias. In addition, the volume alias can be obtained
*    via FS_GetVolumeAlias().
*/
int FS_SetVolumeAlias(const char * sVolumeName, const char * sVolumeAlias) {
  FS_VOLUME * pVolume;
  int         r;
  unsigned    NumBytes;
  unsigned    i;
  char        c;
  U8          IsValid;

  FS_LOCK();
  if (sVolumeName != NULL) {
    pVolume = FS__FindVolume(sVolumeName);
    if (pVolume != NULL) {
      FS_LOCK_SYS();
      r = FS_ERRCODE_OK;                                    // OK, alternative name assigned.
#if (FS_MAX_LEN_VOLUME_ALIAS > 0)
      NumBytes = 0;
      if (sVolumeAlias != NULL) {
        NumBytes = FS_STRLEN(sVolumeAlias);
      }
      if (NumBytes == 0u) {
        pVolume->acAlias[0] = '\0';
      } else {
        if (NumBytes > (sizeof (pVolume->acAlias) - 1u)) {  // -1 because we have to reserve one character for the 0-terminator.
          r = FS_ERRCODE_INVALID_PARA;                      // Error, alias too long.
        } else {
          //
          // Make a copy of the alias and at the same time
          // check the characters for validity.
          //
          if (sVolumeAlias != NULL) {
            //
            // Verify that the alias is valid.
            //
            IsValid = 1;
            for (i = 0; i < NumBytes; ++i) {
              c = sVolumeAlias[i];
              if (_IsValidVolumeAliasChar(c) == 0) {
                IsValid = 0;
                break;
              }
            }
            if (IsValid == 0u) {
              r = FS_ERRCODE_INVALID_CHAR;                  // Error, invalid character in alias.
            } else {
              (void)FS_STRNCPY(pVolume->acAlias, sVolumeAlias, NumBytes);
              pVolume->acAlias[NumBytes] = '\0';
            }
          }
        }
      }
#else
      IsValid = 1;
      if (sVolumeAlias != NULL) {
        NumBytes = (unsigned)FS_STRLEN(sVolumeAlias);
        //
        // Verify that the alias is valid.
        //
        IsValid = 1;
        for (i = 0; i < NumBytes; ++i) {
          c = sVolumeAlias[i];
          if (_IsValidVolumeAliasChar(c) == 0) {
            IsValid = 0;
            break;
          }
        }
      }
      if (IsValid == 0u) {
        r = FS_ERRCODE_INVALID_CHAR;                  // Error, invalid character in alias.
      } else {
        pVolume->sAlias = sVolumeAlias;
      }
#endif // FS_MAX_LEN_VOLUME_ALIAS > 0
      FS_UNLOCK_SYS();
    } else {
      r = FS_ERRCODE_VOLUME_NOT_FOUND;                      // Error, volume was not found.
    }
  } else {
    r = FS_ERRCODE_INVALID_PARA;                            // Error, volume not specified.
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_GetVolumeAlias
*
*  Function description
*    Returns the alternative name of a volume.
*
*  Parameters
*    sVolumeName      [IN] Name of the volume to be queried.
*                     It cannot be set to NULL.
*
*  Return value
*    ==NULL   No volume alias configure or an error occurred.
*    !=NULL   Configured volume alias.
*
*  Additional information
*    This function is optional. It can be used by an application
*    to obtain the alternative name of a volume if configured.
*    For more information about the volume alias refer to FS_SetVolumeAlias().
*
*    This function is available only when FS_SUPPORT_VOLUME_ALIAS is set
*    to 1.
*
*    The alias of a volume can also be queried either via FS_GetVolumeInfo()
*    or FS_GetVolumeInfoEx(). The sAlias member of the FS_DISK_INFO structure
*    stores the configured alias.
*/
const char * FS_GetVolumeAlias(const char * sVolumeName) {
  FS_VOLUME  * pVolume;
  const char * sAlias;

  FS_LOCK();
  sAlias = NULL;
  if (sVolumeName != NULL) {
    pVolume = FS__FindVolume(sVolumeName);
    if (pVolume != NULL) {
      FS_LOCK_SYS();
#if (FS_MAX_LEN_VOLUME_ALIAS > 0)
      sAlias = pVolume->acAlias;
#else
      sAlias = pVolume->sAlias;
#endif // FS_MAX_LEN_VOLUME_ALIAS > 0
      FS_UNLOCK_SYS();
    }
  }
  FS_UNLOCK();
  return sAlias;
}

#endif // FS_SUPPORT_VOLUME_ALIAS

/*********************************************************************
*
*       FS_GetMountType
*
*  Function description
*    Returns information about how a volume is mounted.
*
*  Parameters
*    sVolumeName    Name of the volume to be queried.
*
*  Return value
*    ==0            Volume is not mounted.
*    ==FS_MOUNT_RO  Volume is mounted read only.
*    ==FS_MOUNT_RW  Volume is mounted read/write.
*     <0            Error code indicating the failure reason.
*                   Refer to FS_ErrorNo2Text().
*
*  Additional information
*    sVolumeName is the name of a volume that already exists. If the
*    volume name is not known to file system then an error is returned.
*    Alternatively, the application can call FS_IsVolumeMounted()
*    if the information about how the volume is actually mounted
*    is not important. After the file system initialization all
*    volumes are in unmounted state.
*/
int FS_GetMountType(const char * sVolumeName) {
  int         r;
  FS_VOLUME * pVolume;

  FS_LOCK();
  r = FS_ERRCODE_VOLUME_NOT_FOUND;
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    FS_LOCK_SYS();
    r = (int)pVolume->MountType;
    FS_UNLOCK_SYS();
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_GetAutoMount
*
*  Function description
*    Returns information about how a volume is automatically mounted.
*
*  Parameters
*    sVolumeName    Name of the volume to be queried.
*
*  Return value
*    ==0            Volume is not mounted automatically.
*    ==FS_MOUNT_RO  Volume is mounted automatically in read only mode.
*    ==FS_MOUNT_RW  Volume is mounted automatically in read/write mode.
*     <0            Error code indicating the failure reason.
*                   Refer to FS_ErrorNo2Text().
*
*  Additional information
*    After the initialization of the file system all the volumes
*    are configured to be automatically mounted as read/write
*    at the first access to the file system. The type of mount
*    operation can be configured via FS_SetAutoMount().
*/
int FS_GetAutoMount(const char * sVolumeName) {
  int         r;
  FS_VOLUME * pVolume;

  FS_LOCK();
  r = FS_ERRCODE_VOLUME_NOT_FOUND;
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    FS_LOCK_SYS();
    r = (int)pVolume->AllowAutoMount;
    FS_UNLOCK_SYS();
  }
  FS_UNLOCK();
  return r;
}

/*************************** End of file ****************************/

