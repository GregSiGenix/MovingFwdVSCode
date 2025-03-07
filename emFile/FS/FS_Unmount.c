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
File        : FS_Unmount.c
Purpose     : Volume unmount operations.
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
*       Public code (internal)
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_NONE_Clean
*
*  Function description
*    Handling function for the storage clean operation.
*
*  Parameters
*    pVolume      Instance of the volume to be cleaned.
*
*  Additional information
*    This function does not perform any operation.
*/
void FS_NONE_Clean(FS_VOLUME * pVolume) {
  FS_USE_PARA(pVolume);
}

/*********************************************************************
*
*       FS__UnmountNL
*
*  Function description
*    Synchronizes the data and marks the volume as not initialized.
*
*  Parameters
*    pVolume      Instance of the volume to be unmounted. Must be valid, may not be NULL.
*
*  Additional information
*    This function closes all opened file and directory handles located
*    on the volume and marks the volume as not being initialized.
*/
void FS__UnmountNL(FS_VOLUME * pVolume) {
  FS_DEVICE * pDevice;

  pDevice = &pVolume->Partition.Device;
  if (pVolume->MountType != 0u) {
#if ((FS_SUPPORT_FAT != 0) || (FS_SUPPORT_EFS != 0))
    FS_FILE * pFile;

    //
    // Close all open files on this volume
    //
    pFile = FS_Global.pFirstFileHandle;
    while (pFile != NULL){
      FS_FILE_OBJ * pFileObj;
      U8            FileIsOnThisVolume;

      //
      // Check if file is on this volume. SYS-Lock is required when going through the data structures.
      //
      FileIsOnThisVolume = 0;
      FS_LOCK_SYS();
      if (pFile->InUse != 0u) {
        pFileObj = pFile->pFileObj;
        if (pFileObj != NULL) {
          if (pFileObj->pVolume == pVolume) {
            FileIsOnThisVolume = 1;
          }
        } else {
          FS__FreeFileHandle(pFile);      // Just in case the file has been left open by a forced unmount.
        }
      }
      FS_UNLOCK_SYS();
      //
      // Close file if it is on this volume
      //
      if (FileIsOnThisVolume != 0u) {
        (void)FS__CloseFileNL(pFile);
      }
      pFile = pFile->pNext;
    }
#endif
#if FS_SUPPORT_CACHE
    //
    // Write data from the sector cache to storage medium.
    //
    (void)FS__CACHE_CommandDeviceNL(pDevice, FS_CMD_CACHE_CLEAN, NULL);
    //
    // Discard all the data from the sector cache.
    //
    (void)FS__CACHE_CommandDeviceNL(pDevice, FS_CMD_CACHE_INVALIDATE, pVolume->Partition.Device.Data.pCacheData);
#endif
    FS_JOURNAL_INVALIDATE(pVolume);                       // Note: If a transaction on the journal is running, data in journal is purposely discarded!
    FS_CLEAN_FS(pVolume);
#if FS_SUPPORT_SECTOR_BUFFER_CACHE
    FS__InvalidateSectorBuffer(pVolume, SECTOR_INDEX_INVALID, 0);
#endif // FS_SUPPORT_SECTOR_BUFFER_CACHE
  }
  (void)FS__IoCtlNL(pVolume, FS_CMD_UNMOUNT, 0, NULL);    // Send unmount command to driver
  FS_LOCK_SYS();
  pVolume->MountType             = 0;                     // Mark volume as unmounted
  pDevice->Data.IsInited         = 0;
  pVolume->Partition.StartSector = 0;
  pVolume->Partition.NumSectors  = 0;
  FS_UNLOCK_SYS();
}

/*********************************************************************
*
*       FS__UnmountForcedNL
*
*  Function description
*    Marks the volume as not initialized.
*
*  Parameters
*    pVolume      Instance of the volume to be unmounted. Must be valid, may not be NULL.
*
*  Additional information
*    This function invalidates all opened handles of files and directories
*    located on the volume and marks the volume as not initialized.
*    This function does not write any data to storage device.
*/
void FS__UnmountForcedNL(FS_VOLUME * pVolume) {
  FS_DEVICE * pDevice;

  pDevice = &pVolume->Partition.Device;
#if ((FS_SUPPORT_FAT != 0) || (FS_SUPPORT_EFS != 0))
  if (pVolume->MountType != 0u) {
    FS_FILE * pFile;

    FS_JOURNAL_INVALIDATE(pVolume);
    //
    // Mark all open handles on this volume as invalid.
    // The file handles must be freed by calling FS_FClose().
    // A system Lock is required when going through the data structures.
    //
    FS_LOCK_SYS();
    pFile = FS_Global.pFirstFileHandle;
    while (pFile != NULL) {
      FS_FILE_OBJ * pFileObj;

      //
      // Check if file is on this volume.
      //
      if (pFile->InUse != 0u) {
        pFileObj = pFile->pFileObj;
        if (pFileObj != NULL) {
          if (pFileObj->pVolume == pVolume) {
            //
            // Free and invalidate the file object.
            //
            FS__FreeFileObjNL(pFileObj);
            pFile->pFileObj = NULL;
          }
        }
      }
      pFile = pFile->pNext;
    }
    FS_UNLOCK_SYS();
  }
#endif
#if FS_SUPPORT_CACHE
  //
  // Discard all the data from the sector cache.
  //
  (void)FS__CACHE_CommandDeviceNL(pDevice, FS_CMD_CACHE_INVALIDATE, pVolume->Partition.Device.Data.pCacheData);
#endif
#if FS_SUPPORT_SECTOR_BUFFER_CACHE
  FS__InvalidateSectorBuffer(pVolume, SECTOR_INDEX_INVALID, 0);
#endif // FS_SUPPORT_SECTOR_BUFFER_CACHE
  (void)FS__IoCtlNL(pVolume, FS_CMD_UNMOUNT_FORCED, 0, NULL);   // Send unmount command to driver
  FS_LOCK_SYS();
  pVolume->MountType             = 0;                     // Mark volume as unmounted
  pDevice->Data.IsInited         = 0;
  pVolume->Partition.StartSector = 0;
  pVolume->Partition.NumSectors  = 0;
  FS_UNLOCK_SYS();
  FS_JOURNAL_INVALIDATE(pVolume);
}

/*********************************************************************
*
*       FS__UnmountForced
*
*  Function description
*    Marks the volume as not initialized.
*
*  Parameters
*    pVolume      Instance of the volume to be unmounted. Must be valid, may not be NULL.
*
*  Additional information
*    This function invalidates all opened handles of files and directories
*    located on the volume and marks the volume as not initialized.
*    This function does not write any data to storage device.
*/
void FS__UnmountForced(FS_VOLUME * pVolume) {
  FS_LOCK_DRIVER(&pVolume->Partition.Device);
  FS__UnmountForcedNL(pVolume);
  FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
}

/*********************************************************************
*
*       FS__Unmount
*
*  Function description
*    Synchronizes the data and marks the volume as not initialized.
*
*  Parameters
*    pVolume      Instance of the volume to be unmounted. Must be valid, may not be NULL.
*
*  Additional information
*    This function closes all opened file and directory handles located
*    on the volume and marks the volume as not being initialized.
*/
void FS__Unmount(FS_VOLUME * pVolume) {
  int Status;

  FS_LOCK_DRIVER(&pVolume->Partition.Device);
  Status = FS_LB_GetStatus(&pVolume->Partition.Device);
  if (Status == FS_MEDIA_NOT_PRESENT) {
    FS__UnmountForcedNL(pVolume);
  } else {
    FS__UnmountNL(pVolume);
  }
  FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
}

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_Unmount
*
*  Function description
*    Synchronizes the data and marks the volume as not initialized.
*
*  Parameters
*    sVolumeName  Pointer to a string containing the name of the volume
*                 to be unmounted. If the empty string is specified,
*                 the first device in the volume table is used.
*                 Can not be NULL.
*
*  Additional information
*    This function closes all open files and synchronizes the volume,
*    that is writes all cached data to storage device.
*    FS_Unmount() hast to be called before a storage device is removed
*    to make sure that all the information cached by the file system
*    is updated to storage device. This function is also useful when
*    shutting down a system.
*
*    The volume is initialized again at the next call to any other
*    file system API function that requires access to storage device.
*    The application can also explicitly initialize the volume via
*    FS_Mount() or FS_MountEx().
*/
void FS_Unmount(const char * sVolumeName) {
  FS_VOLUME * pVolume;

  FS_LOCK();
  FS_PROFILE_CALL_STRING(FS_EVTID_UNMOUNT, sVolumeName);
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    FS__Unmount(pVolume);
  }
  FS_PROFILE_END_CALL(FS_EVTID_UNMOUNT);
  FS_UNLOCK();
}

/*********************************************************************
*
*       FS_UnmountForced
*
*  Function description
*    Marks the volume as not initialized.
*
*  Parameters
*    sVolumeName    Pointer to a string containing the name of the volume
*                   to be unmounted. If the empty string is specified,
*                   the first device in the volume table is used.
*
*  Additional information
*    This function performs the same operations as FS_Unmount().
*    FS_UnmountForced() has to be called if a storage device has been
*    removed before it could be regularly unmounted. When using
*    FS_UnmountForced() there is no guarantee that the information
*    cached by the file system is updated to storage.
*
*    The volume is initialized again at the next call to any other
*    file system API function that requires access to storage device.
*    The application can also explicitly initialize the volume via
*    FS_Mount() or FS_MountEx().
*
*    Opened file handles are only marked as invalid but they are not
*    closed. The application has to close them explicitly by calling
*    FS_FClose().
*/
void FS_UnmountForced(const char * sVolumeName) {
  FS_VOLUME * pVolume;

  FS_LOCK();
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    FS__UnmountForced(pVolume);
  }
  FS_UNLOCK();
}

/*************************** End of file ****************************/
