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
File        : FS_Storage.c
Purpose     : Implementation of Storage API functions
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include section
*
**********************************************************************
*/
#include "FS_Int.h"

/*********************************************************************
*
*       Public data
*
**********************************************************************
*/
FS_STORAGE_COUNTERS FS_STORAGE_Counters;

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _AllocVolumeHandle
*
*  Function description
*    Searches for a free volume instance.
*
*  Return value
*    pVolume    A valid free volume handle
*/
static FS_VOLUME * _AllocVolumeHandle(void) {
  FS_VOLUME * pVolume;

  FS_LOCK_SYS();
  pVolume = &FS_Global.FirstVolume;
  //
  // While no free entry found.
  //
  for (;;) {
    if (pVolume->InUse == 0u) {
      FS_VOLUME * pNext;
      //
      // Save the pNext pointer to restore it back.
      //
      pNext = pVolume->pNext;
      FS_MEMSET(pVolume, 0, sizeof(FS_VOLUME));
      pVolume->WriteMode  = FS_WRITEMODE_UNKNOWN;
      pVolume->InUse      = 1;
#if FS_SUPPORT_FREE_SECTOR
      pVolume->FreeSector = 1;
#endif
      pVolume->pNext      = pNext;
      break;
    }
    if (pVolume->pNext == NULL) {
      pVolume->pNext = SEGGER_PTR2PTR(FS_VOLUME, FS_TRY_ALLOC((I32)sizeof(FS_VOLUME), "FS_VOLUME"));
      if (pVolume->pNext != NULL) {
        FS_MEMSET(pVolume->pNext, 0, sizeof(FS_VOLUME));
      }
    }
    pVolume = pVolume->pNext;
    //
    // Neither a free volume handle found
    // nor enough space to allocate a new one.
    //
    if (pVolume == NULL) {
      break;
    }
  }
  FS_UNLOCK_SYS();
  return pVolume;
}

/*********************************************************************
*
*       Public code (internal)
*
**********************************************************************
*/

/*********************************************************************
*
*       FS__IoCtlNL
*
*  Function description
*    Executes a device command.
*
*  Parameters
*    pVolume    Instance of the volume on which the command is executed.
*    Cmd        Command to be executed.
*    Aux        Parameter depending on command.
*    pBuffer    Pointer to a buffer used for the command.
*
*  Return value
*    >=0    OK, command executed successfully.
*    < 0    Error code indicating the failure reason.
*
*  Additional information
*    This function performs the same operation as FS_IoCtl() with
*    the difference that it does not lock the device driver.
*/
int FS__IoCtlNL(FS_VOLUME * pVolume, I32 Cmd, I32 Aux, void * pBuffer) {
  FS_DEVICE * pDevice;
  int         r;
  int         Status;

  r = FS_ERRCODE_OK;
  pDevice = &pVolume->Partition.Device;
  switch (Cmd) {
  case FS_CMD_UNMOUNT:
  case FS_CMD_UNMOUNT_FORCED:
  case FS_CMD_SYNC:
  case FS_CMD_DEINIT:
    break;
  case FS_CMD_FORMAT_LOW_LEVEL:
  case FS_CMD_REQUIRES_FORMAT:
  case FS_CMD_FREE_SECTORS:
  case FS_CMD_GET_DEVINFO:
  case FS_CMD_SET_DELAY:
  default:
    r = FS_LB_InitMediumIfRequired(pDevice);
    if (r == 0) {
      Status = FS_LB_GetStatus(pDevice);
      if (Status == FS_MEDIA_NOT_PRESENT) {
        r = FS_ERRCODE_STORAGE_NOT_PRESENT;           // Error, the storage device is not inserted.
      }
    }
    break;
  }
  if (r == FS_ERRCODE_OK) {
    r = FS_LB_Ioctl(pDevice, Cmd, Aux, pBuffer);
    if (r < 0) {
      r = FS_ERRCODE_IOCTL_FAILURE;                   // Error, I/O control operation failed.
    }
  }
  return r;
}

/*********************************************************************
*
*       FS__IoCtl
*
*  Function description
*    Executes a device command.
*
*  Parameters
*    pVolume    Instance of the volume on which the command is executed.
*    Cmd        Command to be executed.
*    Aux        Parameter depending on command.
*    pBuffer    Pointer to a buffer used for the command.
*
*  Return value
*    >=0    OK, command executed successfully.
*    < 0    Error code indicating the failure reason.
*/
int FS__IoCtl(FS_VOLUME * pVolume, I32 Cmd, I32 Aux, void *pBuffer) {
  int r;

  r = FS_ERRCODE_INVALID_PARA;
  if (pVolume != NULL) {
    FS_LOCK_DRIVER(&pVolume->Partition.Device);
    r = FS__IoCtlNL(pVolume, Cmd, Aux, pBuffer);
    FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
  }
  return r;
}

/*********************************************************************
*
*       FS__FormatLowNL
*
*  Function description
*    Prepares the storage device for operation.
*
*  Parameters
*    pVolume      Instance of the volume to be formatted. Can not be NULL.
*
*  Return value
*    ==0      OK, low-level format was successful.
*    !=0      Error code indicating the failure reason.
*/
int FS__FormatLowNL(FS_VOLUME * pVolume) {
  int         r;
  int         Status;
  FS_DEVICE * pDevice;

  pDevice = &pVolume->Partition.Device;
  Status  = FS_LB_GetStatus(pDevice);
  if (Status == FS_MEDIA_NOT_PRESENT) {
    FS__UnmountForcedNL(pVolume);
  } else {
    FS__UnmountNL(pVolume);
  }
  r = FS__IoCtlNL(pVolume, FS_CMD_FORMAT_LOW_LEVEL, 0, NULL);    // Erase and low-level format the medium.
  return r;
}

/*********************************************************************
*
*       FS__WriteSectorNL
*
*  Function description
*    Writes a single logical sector to a volume.
*
*  Parameters
*    pVolume        Instance of the volume to write to. Can not be NULL.
*    pData          Sector data to write.
*    SectorIndex    Index of the sector to be written.
*
*  Return value
*    ==0      OK, sector data written to storage.
*    !=0      Error code indicating the failure reason.
*/
int FS__WriteSectorNL(FS_VOLUME * pVolume, const void * pData, U32 SectorIndex) {
  FS_DEVICE * pDevice;
  int         r;
  U8          SectorType;

  pDevice = &pVolume->Partition.Device;
  r = FS_LB_InitMediumIfRequired(pDevice);
  if (r != 0) {
    return r;                                     // Error, could not initialize the storage device,
  }
#if FS_SUPPORT_TEST
  SectorType = pVolume->SectorType;
#else
  SectorType = FS_SECTOR_TYPE_DATA;
#endif // FS_SUPPORT_TEST
  r = FS_LB_WriteDevice(pDevice, SectorIndex, pData, SectorType, 1);
  if (r != 0) {
    return FS_ERRCODE_WRITE_FAILURE;              // Error, could not write sector data.
  }
  return FS_ERRCODE_OK;                           // OK, sector data written.
}

/*********************************************************************
*
*       FS__ReadSectorNL
*
*  Function description
*    Reads a single logical sector from a volume.
*
*  Parameters
*    pVolume      Instance of the volume to read from. Can not be NULL.
*    pData        Sector data read from storage.
*    SectorIndex  Index of the sector to be read.
*
*  Return value
*    ==0      OK, sector data read from storage.
*    !=0      Error code indicating the failure reason.
*/
int FS__ReadSectorNL(FS_VOLUME * pVolume, void * pData, U32 SectorIndex) {
  FS_DEVICE * pDevice;
  int         r;
  U8          SectorType;

  pDevice = &pVolume->Partition.Device;
  r = FS_LB_InitMediumIfRequired(pDevice);
  if (r != 0) {
    return r;                                       // Error, could not initialize the storage device.
  }
#if FS_SUPPORT_TEST
  SectorType = pVolume->SectorType;
#else
  SectorType = FS_SECTOR_TYPE_DATA;
#endif // FS_SUPPORT_TEST
  r = FS_LB_ReadDevice(pDevice, SectorIndex, pData, SectorType);
  if (r != 0) {
    return FS_ERRCODE_READ_FAILURE;                 // Error, could not read sector data.
  }
  return FS_ERRCODE_OK;                             // OK, sector data read.
}

/*********************************************************************
*
*       FS__WriteSectorsNL
*
*  Function description
*    Writes multiple sectors to a volume.
*
*  Parameters
*    pVolume        Instance of the volume to write to. Can not be NULL.
*    pData          Sector data to write.
*    SectorIndex    Index of the first sector to be written.
*    NumSectors     Number of the sectors to be written.
*
*  Return value
*    ==0      OK, sector data written to storage.
*    !=0      Error code indicating the failure reason.
*
*  Additional information
*    This is the internal non-locking version of FS__WriteSectors().
*/
int FS__WriteSectorsNL(FS_VOLUME * pVolume, const void * pData, U32 SectorIndex, U32 NumSectors) {
  FS_DEVICE * pDevice;
  int         r;
  U8          SectorType;

  pDevice = &pVolume->Partition.Device;
  r = FS_LB_InitMediumIfRequired(pDevice);
  if (r != 0) {
    return r;                                         // Error, could initialize the storage device.
  }
#if FS_SUPPORT_TEST
  SectorType = pVolume->SectorType;
#else
  SectorType = FS_SECTOR_TYPE_DATA;
#endif // FS_SUPPORT_TEST
  r = FS_LB_WriteBurst(pDevice, SectorIndex, NumSectors, pData, SectorType, 1);
  if (r != 0) {
    return FS_ERRCODE_WRITE_FAILURE;                  // Error, could not write sector data.
  }
  return FS_ERRCODE_OK;                               // OK, sector data written.
}

/*********************************************************************
*
*       FS__ReadSectorsNL
*
*  Function description
*    Reads multiple sectors from a volume.
*
*  Parameters
*    pVolume      Instance of the volume to read from. Can not be NULL.
*    pData        Sector data read from storage.
*    SectorIndex  Index of the first sector to be read.
*    NumSectors   Number of sectors to be read.
*
*  Return value
*    ==0      OK, sector data read from storage.
*    !=0      Error code indicating the failure reason.
*/
int FS__ReadSectorsNL(FS_VOLUME * pVolume, void * pData, U32 SectorIndex, U32 NumSectors) {
  FS_DEVICE * pDevice;
  int         r;
  U8          SectorType;

  pDevice = &pVolume->Partition.Device;
  r = FS_LB_InitMediumIfRequired(pDevice);
  if (r != 0) {
    return r;                               // Error, could not initialize the storage device.
  }
#if FS_SUPPORT_TEST
  SectorType = pVolume->SectorType;
#else
  SectorType = FS_SECTOR_TYPE_DATA;
#endif // FS_SUPPORT_TEST
  r = FS_LB_ReadBurst(pDevice, SectorIndex, NumSectors, pData, SectorType);
  if (r != 0) {
    return FS_ERRCODE_READ_FAILURE;         // Error, could not read sector data.
  }
  return FS_ERRCODE_OK;                     // OK, sector data returned.
}

/*********************************************************************
*
*       FS__GetVolumeStatusNL
*
*  Function description
*    Returns the status of a volume.
*
*  Parameters
*    pVolume    Instance of the volume to be queried. Can not be NULL.
*
*  Return value
*    ==FS_MEDIA_NOT_PRESENT     Volume is not present.
*    ==FS_MEDIA_IS_PRESENT      Volume is present.
*    ==FS_MEDIA_STATE_UNKNOWN   Volume state is unknown.
*/
int FS__GetVolumeStatusNL(FS_VOLUME * pVolume) {
  int         r;
  FS_DEVICE * pDevice;

  pDevice = &pVolume->Partition.Device;
  r = pDevice->pType->pfGetStatus(pDevice->Data.Unit);
  return r;
}

/*********************************************************************
*
*       FS__GetDeviceInfoNL
*
*  Function description
*    Returns information about of a volume.
*
*  Parameters
*    pVolume    Instance of the volume to be queried. Can not be NULL.
*    pDevInfo   [OUT] Information about the volume.
*
*  Return value
*    ==0    OK, information about the storage device returned.
*    !=0    Error code indicating the failure reason.
*/
int FS__GetDeviceInfoNL(FS_VOLUME * pVolume, FS_DEV_INFO * pDevInfo) {
  FS_DEVICE * pDevice;
  int         r;

  pDevice = &pVolume->Partition.Device;
  FS_MEMSET(pDevInfo, 0, sizeof(FS_DEV_INFO));
  r = FS_LB_InitMediumIfRequired(pDevice);
  if (r != 0) {
    return r;                                 // Error, could not initialize storage medium.
  }
  r = FS_LB_GetDeviceInfo(pDevice, pDevInfo);
  if (r != 0) {
    return FS_ERRCODE_STORAGE_NOT_READY;      // Error, device information can not be retrieved.
  }
  return FS_ERRCODE_OK;                       // OK, information about the storage returned.
}

/*********************************************************************
*
*       FS__AddPhysDevice
*
*  Function description
*    Adds a device driver to the file system.
*
*  Parameters
*    pDeviceType    Type of the device to be added.
*
*  Return value
*    >=0    Unit number of the added device.
*    < 0    An error occurred.
*/
int FS__AddPhysDevice(const FS_DEVICE_TYPE * pDeviceType) {
  int Unit;

  Unit = -1;
  if (pDeviceType->pfAddDevice != NULL) {
    Unit = pDeviceType->pfAddDevice();
    if (Unit < 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_API, "FS__AddPhysDevice: Could not add device."));
    }
  }
  return Unit;
}

/*********************************************************************
*
*       FS__AddDevice
*
*  Function description
*    Adds a device driver to the file system.
*
*  Parameters
*    pDeviceType    Type of the device to be added.
*
*  Return value
*    !=NULL   Volume instance assigned to the storage device.
*    ==NULL   An error occurred.
*
*  Additional information
*    Internal version of FS_AddDevice().
*/
FS_VOLUME * FS__AddDevice(const FS_DEVICE_TYPE * pDeviceType) {
  FS_VOLUME * pVolume;
  int         Unit;
  FS_DEVICE * pDevice;

  pVolume = _AllocVolumeHandle();
  if (pVolume != NULL) {
    Unit = FS__AddPhysDevice(pDeviceType);
    if (Unit >= 0) {
      FS_OS_ADD_DRIVER(pDeviceType);
      FS_Global.NumVolumes++;
      pDevice = &pVolume->Partition.Device;
      pDevice->pType          = pDeviceType;
      pDevice->Data.Unit      = (U8)Unit;
      pVolume->AllowAutoMount = (U8)FS_MOUNT_RW;
#if FS_SUPPORT_JOURNAL
      pDevice->Data.JournalData.IsActive = 1;   // The journal is enabled by default.
#if (FS_MAX_LEN_JOURNAL_FILE_NAME > 0)
      //
      // Initialize the name of the journal file.
      //
      (void)FS_STRNCPY(pVolume->acJournalFileName, FS_JOURNAL_FILE_NAME, sizeof(pVolume->acJournalFileName) - 1u);
      pVolume->acJournalFileName[sizeof(pVolume->acJournalFileName) - 1u] = '\0';
#endif  // FS_MAX_LEN_JOURNAL_FILE_NAME > 0
#endif  // FS_SUPPORT_JOURNAL
    } else {
      pVolume->InUse = 0;                       // De-allocate the volume structure.
      pVolume        = NULL;
    }
  } else {
    //
    // Error, could not allocate volume handle.
    //
    FS_DEBUG_ERROROUT((FS_MTYPE_API, "FS__AddDevice: Add. driver could not be added."));
  }
  return pVolume;
}

/*********************************************************************
*
*       FS__IsLLFormattedNL
*
*  Function description
*    Checks if a volume is ready for data access.
*
*  Parameters
*    pVolume    Instance of the volume to be queried. Can not be NULL.
*
*  Return value
*    ==1    Volume is low-level formatted
*    ==0    Volume is not low-level formatted
*    < 0    Low-level format not supported by volume or an error occurred
*/
int FS__IsLLFormattedNL(FS_VOLUME * pVolume) {
  int r;

  r = FS__IoCtlNL(pVolume, FS_CMD_REQUIRES_FORMAT, 0, NULL);
  if (r == 0) {
    r = 1;
  } else {
    if (r == 1) {
      r = 0;
    }
  }
  return r;
}

/*********************************************************************
*
*       FS__FindVolumeEx
*
*  Function description
*    Searches for a volume by the name of the file or directory.
*
*  Parameters
*    sName      Partially qualified name (0-terminated). Can be NULL.
*    psName     [OUT] Partially qualified name of the file or directory (0-terminated). Can be NULL.
*
*  Return value
*    ==NULL   No matching volume found.
*    !=NULL   Instance of the volume that stores the specified file
*             or directory.
*
*  Additional information
*    sName can be specified as follows:
*    - <Name>                                 e.g. "File.txt"                 (*psName -> "File.txt")
*    - <DeviceName>:<Name>                    e.g. "mmc:SubDir"               (*psName -> "SubDir")
*    - <DeviceName>:<UnitNo>:<Name>           e.g. "mmc:0:File.txt"           (*psName -> "File.txt")
*    - <DeviceName>:<UnitNo>:\<Path>\<Name>   e.g. "mmc:0:\\SubDir\\File.txt" (*psName -> "\\SubDir\\File.txt")
*
*    UnitNo is optional and if not specified it is considered 0.
*    *psName points to the start of the file name in sName.
*    If sName does not contain any volume (device) name then
*    *psName is equal to sName.
*/
FS_VOLUME * FS__FindVolumeEx(const char * sName, const char ** psName) {
  const char * s;
  FS_VOLUME  * pVolume;
  unsigned     NumBytes;
  U8           Unit;

  s       = NULL;
  pVolume = NULL;
  if (sName != NULL) {
    pVolume = &FS_Global.FirstVolume;   // Take the first defined volume as default.
    //
    // Search for the volume name delimiter.
    //
    s = FS_STRCHR(sName, (int)':');
    if (s != NULL) {
      NumBytes = (unsigned)SEGGER_PTR_DISTANCE(s, sName);   // Calculate length of specified device name.
      Unit     = 0;
      //
      // Convert the unit number.
      //
      s++;
      if ((*s != '\0') && (*(s + 1) == ':')) {
        Unit = (U8)*s - (U8)'0';
        s += 2;
      }
      //
      // Scan for device name.
      //
      do {
        const FS_DEVICE_TYPE * pDevice;
        FS_DEVICE_DATA       * pDeviceData;
        const char           * sVolumeName;

        if (pVolume->InUse != 0u) {
          pDevice     = pVolume->Partition.Device.pType;
          pDeviceData = &pVolume->Partition.Device.Data;
          sVolumeName = pDevice->pfGetName(pDeviceData->Unit);
          if (FS_STRLEN(sVolumeName) == NumBytes) {
            if (FS_STRNCMP(sVolumeName, sName, NumBytes) == 0) {
              if (Unit == pDeviceData->Unit) {
                break;
              }
            }
          }
        }
        pVolume = pVolume->pNext;
      } while (pVolume != NULL);
#if FS_SUPPORT_VOLUME_ALIAS
      if (pVolume == NULL) {
        const char * sVolumeAlias;

        pVolume = &FS_Global.FirstVolume;
        //
        // No match found for the volume name. Check if any of the configured volume alias matches.
        //
        do {
          if (pVolume->InUse != 0u) {
#if (FS_MAX_LEN_VOLUME_ALIAS > 0)
            sVolumeAlias = pVolume->acAlias;
#else
            sVolumeAlias = pVolume->sAlias;
            if (sVolumeAlias != NULL)
#endif // FS_MAX_LEN_VOLUME_ALIAS > 0
            {
              if (FS_STRLEN(sVolumeAlias) == NumBytes) {
                if (FS_STRNCMP(sVolumeAlias, sName, NumBytes) == 0) {
                  break;                // Found an alias that matches.
                }
              }
            }
          }
          pVolume = pVolume->pNext;
        } while (pVolume != NULL);
      }
#endif // FS_SUPPORT_VOLUME_ALIAS
    } else {
      //
      // Use the first volume if none is specified.
      //
      s = sName;
      if (pVolume->InUse == 0u) {       // Not initialized correctly?
        pVolume = NULL;
      }
    }
  }
  //
  // Return the partially name of the file or directory.
  //
  if (psName != NULL) {
    *psName = s;
  }
  return pVolume;
}

/*********************************************************************
*
*       FS__FindVolume
*
*  Function description
*    Searches for a volume by volume name.
*
*  Parameters
*    sVolumeName    Volume name (0-terminated). Can be NULL.
*
*  Return value
*    ==NULL   No matching volume found.
*    !=NULL   Instance of the found volume.
*
*  Additional information
*    The volume name has to be specified as <DeviceName>:<UnitNo>"
*    with the UnitNo being optional. If not specified the unit number
*    is considered to be 0. Sample volume names: "nand:0" or "nor:"
*/
FS_VOLUME * FS__FindVolume(const char * sVolumeName) {
  const char * s;
  const char * sDevice;
  FS_VOLUME  * pVolume;
  unsigned     NumBytes;
  U8           Unit;

  pVolume = NULL;
  if (sVolumeName != NULL) {
    pVolume  = &FS_Global.FirstVolume;
    NumBytes = (unsigned)FS_STRLEN(sVolumeName);
    if (NumBytes != 0u) {
      s = FS_STRCHR(sVolumeName, (int)':');
      if (s != NULL) {
        NumBytes = (unsigned)SEGGER_PTR_DISTANCE(s, sVolumeName);
        Unit = 0;
        //
        // Find the correct unit number.
        //
        s++;
        if ((*s != '\0') && (*(s + 1) == ':')) {
          Unit = (U8)*s - (U8)'0';
        }
        for (;;) {
          const FS_DEVICE_TYPE * pDevice;
          FS_DEVICE_DATA       * pDeviceData;

          if (pVolume == NULL) {
            break;                                            // No matching device found.
          }
          pDevice  = pVolume->Partition.Device.pType;
          pDeviceData = &pVolume->Partition.Device.Data;
          if (pDeviceData->Unit == Unit) {
            sDevice = pDevice->pfGetName(pDeviceData->Unit);
            if (FS_STRLEN(sDevice) == NumBytes) {
              if (FS_MEMCMP(sDevice, sVolumeName, NumBytes) == 0) {
                break;                                        // Found the volume.
              }
            }
          }
          pVolume = pVolume->pNext;
        }
      } else {
        //
        // Error, invalid volume name (no volume separator find and the volume name is not the empty string.)
        //
        pVolume = NULL;
      }
#if FS_SUPPORT_VOLUME_ALIAS
      if (pVolume == NULL) {
        const char * sVolumeAlias;

        pVolume = &FS_Global.FirstVolume;
        //
        // No match found for the volume name. Check if any of the configured volume alias matches.
        //
        do {
          if (pVolume->InUse != 0u) {
#if (FS_MAX_LEN_VOLUME_ALIAS > 0)
            sVolumeAlias = pVolume->acAlias;
#else
            sVolumeAlias = pVolume->sAlias;
            if (sVolumeAlias != NULL)
#endif // FS_MAX_LEN_VOLUME_ALIAS > 0
            {
              if (FS_STRLEN(sVolumeAlias) == NumBytes) {
                if (FS_STRNCMP(sVolumeAlias, sVolumeName, NumBytes) == 0) {
                  break;                // Found an alias that matches.
                }
              }
            }
          }
          pVolume = pVolume->pNext;
        } while (pVolume != NULL);
      }
#endif // FS_SUPPORT_VOLUME_ALIAS
    }
  }
  return pVolume;
}

/*********************************************************************
*
*       FS__GetNumVolumes
*
*  Function description
*    Returns the number of available volumes.
*
*  Return value
*    Number of available volumes.
*/
int FS__GetNumVolumes(void) {
  return (int)FS_Global.NumVolumes;
}

/*********************************************************************
*
*       FS__UnmountLL_NL
*
*  Function description
*    Frees the resources allocated to a volume instance.
*
*  Parameters
*    pVolume       Instance of the volume to be unmounted. Can not be NULL.
*
*  Additional information
*    This function sends an unmount command to the device driver
*    and marks the volume as unmounted and uninitialized.
*/
void FS__UnmountLL_NL(FS_VOLUME * pVolume) {
  FS_DEVICE * pDevice;

  pDevice = &pVolume->Partition.Device;
  //
  // Check if we need to low-level-unmount
  //
  if (pDevice->Data.IsInited == 0u) {
    if (pVolume->MountType == 0u) {
      return;
    }
  }
  (void)FS__IoCtlNL(pVolume, FS_CMD_UNMOUNT, 0, NULL);          // Send unmount command to device driver.
  FS_LOCK_SYS();
  pDevice->Data.IsInited = 0;
  FS_UNLOCK_SYS();
}

/*********************************************************************
*
*       FS__UnmountForcedLL_NL
*
*  Function description
*    Frees the resources allocated to a volume instance.
*
*  Parameters
*    pVolume       Instance of the volume to be unmounted. Can not be NULL.
*
*  Additional information
*    This function performs the same operation as FS__UnmountLL_NL()
*    with the difference that it does not write any data to storage device.
*/
void FS__UnmountForcedLL_NL(FS_VOLUME * pVolume) {
  FS_DEVICE * pDevice;

  pDevice = &pVolume->Partition.Device;
  //
  // Check if we need to low-level-unmount the storage device.
  //
  if ((pDevice->Data.IsInited) == 0u) {
    if (pVolume->MountType == 0u) {
      return;
    }
  }
  (void)FS__IoCtlNL(pVolume, FS_CMD_UNMOUNT_FORCED, 0, NULL);   // Send forced unmount command to device driver.
  FS_LOCK_SYS();
  pDevice->Data.IsInited = 0;
  FS_UNLOCK_SYS();
}

/*********************************************************************
*
*       FS__STORAGE_GetSectorUsage
*
*  Function description
*    Returns information about the usage of a logical sector.
*
*  Parameters
*    pVolume        Instance of the volume on which the logical sector is stored.
*    SectorIndex    Index of the logical sector to be queried.
*
*  Return value
*    ==FS_SECTOR_IN_USE         Sector in use
*    ==FS_SECTOR_NOT_USED       Sector not in use
*    ==FS_SECTOR_USAGE_UNKNOWN  The usage of the sector is unknown or not supported
*    < 0                        An error occurred
*/
int FS__STORAGE_GetSectorUsage(FS_VOLUME * pVolume, U32 SectorIndex) {
  int r;
  int Usage;

  r = FS__IoCtl(pVolume, FS_CMD_GET_SECTOR_USAGE, (I32)SectorIndex, SEGGER_PTR2PTR(void, &Usage));
  if (r == 0) {
    r = Usage;
  }
  return r;
}

/*********************************************************************
*
*       FS__STORAGE_GetSectorUsageNL
*
*  Function description
*    Returns information about the usage of a logical sector.
*
*  Parameters
*    pVolume        Instance of the volume on which the logical sector is stored.
*    SectorIndex    Index of the logical sector to be queried.
*
*  Return value
*    ==FS_SECTOR_IN_USE         Sector in use
*    ==FS_SECTOR_NOT_USED       Sector not in use
*    ==FS_SECTOR_USAGE_UNKNOWN  The usage of the sector is unknown or not supported
*    < 0                        An error occurred
*
*  Additional information
*    This function perform the same operation as FS__STORAGE_GetSectorUsage()
*    with the difference that it does not lock the device driver.
*/
int FS__STORAGE_GetSectorUsageNL(FS_VOLUME * pVolume, U32 SectorIndex) {
  int r;
  int Usage;

  r = FS__IoCtlNL(pVolume, FS_CMD_GET_SECTOR_USAGE, (I32)SectorIndex, SEGGER_PTR2PTR(void, &Usage));
  if (r == 0) {
    r = Usage;
  }
  return r;
}

/*********************************************************************
*
*       FS__STORAGE_Sync
*
*  Function description
*    Writes all the cached data to storage device.
*
*  Parameters
*    pVolume      Instance of the volume to be synchronized.
*
*  Additional information
*    This function cleans the sector cache attached to the volume
*    if any and sends a synchronization command to the device driver.
*/
void FS__STORAGE_Sync(FS_VOLUME * pVolume) {
#if FS_SUPPORT_CACHE
  (void)FS__CACHE_CommandVolume(pVolume, FS_CMD_CACHE_CLEAN, NULL);
#endif
  (void)FS__IoCtl(pVolume, FS_CMD_SYNC, 0, NULL);
}

/*********************************************************************
*
*       FS__STORAGE_SyncNL
*
*  Function description
*    Writes all the cached data to storage device.
*
*  Parameters
*    pVolume      Instance of the volume to be synchronized.
*
*  Additional information
*    This function performs the same operation as FS__STORAGE_Sync()
*    with the difference that it does not lock the device driver.
*/
void FS__STORAGE_SyncNL(FS_VOLUME * pVolume) {
#if FS_SUPPORT_CACHE
  (void)FS__CACHE_CommandVolumeNL(pVolume, FS_CMD_CACHE_CLEAN, NULL);
#endif
  (void)FS__IoCtlNL(pVolume, FS_CMD_SYNC, 0, NULL);
}

/*********************************************************************
*
*       FS__STORAGE_RefreshSectors
*
*   Function description
*     Rewrites the contents of logical sector with same data.
*
*  Parameters
*    pVolume              Instance of the volume on which the logical sectors are located.
*                         It cannot be NULL.
*    FirstSector          Index of the first sector to refresh (0-based).
*    NumSectors           Number of sectors to refresh starting from FirstSector.
*    pBuffer              Temporary storage for the sector data.
*                         Must be at least one sector large.
*                         It cannot be NULL.
*    NumSectorsInBuffer   Number of logical sectors that can be stored in pBuffer.
*
*  Return value
*    ==0    OK, sectors refreshed.
*    !=0    Error code indicating the failure reason
*
*  Additional information
*    Refer to FS_STORAGE_RefreshSectors() for more information.
*/
int FS__STORAGE_RefreshSectors(FS_VOLUME * pVolume, U32 FirstSector, U32 NumSectors, void * pBuffer, unsigned NumSectorsInBuffer) {
  int r;
  U32 NumSectorsAtOnce;
  U32 SectorIndex;
  U32 SectorIndexToCheck;
  int Usage;

  r = 0;
  SectorIndexToCheck = FirstSector;
  do {
    //
    // Skip over unused sectors.
    //
    for (;;) {
      Usage = FS__STORAGE_GetSectorUsage(pVolume, SectorIndexToCheck);
      if (Usage == 0) {     // Is sector in use?
        break;
      }
      ++SectorIndexToCheck;
      --NumSectors;
      if (NumSectors == 0u) {
        break;
      }
    }
    if (NumSectors == 0u) {
      break;          // No sectors to refresh.
    }
    SectorIndex      = SectorIndexToCheck;
    NumSectorsAtOnce = 0;
    //
    // Count the number of consecutive sectors in use.
    //
    for (;;) {
      Usage = FS__STORAGE_GetSectorUsage(pVolume, SectorIndexToCheck);
      if (Usage != 0) {     // Is sector not in use?
        break;
      }
      ++SectorIndexToCheck;
      --NumSectors;
      ++NumSectorsAtOnce;
      if (NumSectors == 0u) {
        break;
      }
      if (NumSectorsAtOnce >= NumSectorsInBuffer) {
        break;
      }
    }
    if (NumSectorsAtOnce == 0u) {
      break;            // No more sectors to refresh.
    }
    FS_LOCK_DRIVER(&pVolume->Partition.Device);
    r = FS__ReadSectorsNL(pVolume, pBuffer, SectorIndex, NumSectorsAtOnce);
    FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
    if (r != 0) {
      break;
    }
    FS_LOCK_DRIVER(&pVolume->Partition.Device);
    r = FS__WriteSectorsNL(pVolume, pBuffer, SectorIndex, NumSectorsAtOnce);
    FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
    if (r != 0) {
      break;
    }
  } while (NumSectors != 0u);
  return r;
}

/*********************************************************************
*
*       FS__GetVolumeName
*
*  Function description
*    Generates the name of a volume name.
*
*  Parameters
*    pVolume          Instance of the volume for which to generate the name.
*    sVolumeName      [OUT] Volume name as 0-terminated string.
*    VolumeNameSize   Number of bytes in sVolumeName.
*
*  Return value
*    Number of bytes written to sVolumeName without the 0-terminator.
*
*  Additional information
*    The name is composed of the device name and the unit number
*/
int FS__GetVolumeName(FS_VOLUME * pVolume, char * sVolumeName, int VolumeNameSize) {
  const FS_DEVICE_TYPE * pType;
  FS_DEVICE_DATA       * pDeviceData;
  const char           * sDeviceName;
  int                    r;

  pType       = pVolume->Partition.Device.pType;
  pDeviceData = &pVolume->Partition.Device.Data;
  sDeviceName = pType->pfGetName(pDeviceData->Unit);
  //
  // Check if we have space in the output buffer.
  //
  r = (int)FS_STRLEN(sDeviceName) + 4;      // 1 volume separator + 1 device number + 1 device separator + 1 string terminator.
  if (r <= VolumeNameSize) {
    if (sVolumeName != NULL)  {
      r = 0;
      //
      // Copy the device name
      //
      do {
        *sVolumeName++ = *sDeviceName++;
        r++;
      } while (*sDeviceName != '\0');
      //
      // Add ':' separator.
      //
      *sVolumeName++ = ':';
      //
      // Add unit number.
      //
      *sVolumeName++ = (char)('0' + pDeviceData->Unit);
      //
      // Add ':' separator.
      //
      *sVolumeName++ = ':';
      r += 3;
      //
      // Add '\0' terminator.
      //
      *sVolumeName = '\0';
    }
  }
  return r;
}

#if FS_SUPPORT_TEST

/*********************************************************************
*
*       FS__STORAGE_SetSectorType
*
*  Function description
*    Configures the type of logical sector passed to the logical block
*    read and write functions.
*
*  Parameters
*    sVolumeName      Name of the volume to be configured.
*    SectorType       Type of data stored in the sector (FS_SECTOR_TYPE_...)
*
*  Return value
*    ==0      OK, sector type set successfully.
*    !=0      Error code indicating the failure reason.
*
*  Additional information
*    This function is used only during the testing of the Storage layer.
*/
int FS__STORAGE_SetSectorType(const char * sVolumeName, U8 SectorType) {
  int         r;
  FS_VOLUME * pVolume;

  r = FS_ERRCODE_VOLUME_NOT_FOUND;
  FS_LOCK();
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    FS_LOCK_DRIVER(&pVolume->Partition.Device);
    pVolume->SectorType = SectorType;
    FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
    r = 0;
  }
  FS_UNLOCK();
  return r;
}

#endif // FS_SUPPORT_TEST

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_STORAGE_WriteSector
*
*  Function description
*    Modifies the data of a logical sector.
*
*  Parameters
*    sVolumeName      Name of the volume to write to.
*                     It cannot be NULL.
*    pData            [IN] Buffer that contains the sector data to
*                     be written. It cannot be NULL.
*    SectorIndex      Index of the sector to write to.
*
*  Return value
*    ==0      OK, sector data modified.
*    !=0      Error code indicating the failure reason.
*
*  Additional information
*    pData has to point to a memory area that stores the contents
*    of one logical sector. The size of the logical sector is
*    driver-dependent and typically 512 bytes in size. SectorIndex
*    is a 0-based index that specifies the logical sector to be written.
*    The size of the logical sector and the number of logical sectors
*    in a storage device can be determined via FS_STORAGE_GetDeviceInfo().
*
*    FS_STORAGE_WriteSector() reports an error and does modify the
*    contents of the logical sector if SectorIndex is out of bounds.
*
*    The application can call FS_STORAGE_WriteSectors() instead of
*    calling FS_STORAGE_WriteSector() multiple times if it has to
*    write consecutive logical sectors at once.
*/
int FS_STORAGE_WriteSector(const char * sVolumeName, const void * pData, U32 SectorIndex) {
  int           r;
  FS_VOLUME   * pVolume;
  FS_DEV_INFO   DevInfo;

  if (pData == NULL) {
    return FS_ERRCODE_INVALID_PARA;             // Error, invalid buffer.
  }
  r = FS_ERRCODE_VOLUME_NOT_FOUND;
  FS_LOCK();
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    FS_LOCK_DRIVER(&pVolume->Partition.Device);
    r = FS__GetDeviceInfoNL(pVolume, &DevInfo);
    if (r == 0) {
      r = FS_ERRCODE_INVALID_PARA;
      if (SectorIndex < DevInfo.NumSectors) {
        r = FS__WriteSectorNL(pVolume, pData, SectorIndex);
      }
    }
    FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_STORAGE_ReadSector
*
*  Function description
*    Reads the data of one logical sector.
*
*  Parameters
*    sVolumeName      Name of the volume to read from.
*                     It cannot be NULL.
*    pData            [OUT] Buffer that receives the read sector data.
*                     It cannot be NULL.
*    SectorIndex      Index of the sector to read from.
*
*  Return value
*    ==0      OK, sector data read.
*    !=0      Error code indicating the failure reason.
*
*  Additional information
*    pData has to point to a memory area large enough to store the
*    contents of one logical sector. The size of the logical sector
*    is driver-dependent and typically 512 bytes in size. SectorIndex
*    is a 0-based index that specifies the logical sector to be read.
*    The size of the logical sector and the number of logical sectors
*    in a storage device can be determined via FS_STORAGE_GetDeviceInfo().
*
*    FS_STORAGE_ReadSector() reports an error and does not store any
*    data to pData if SectorIndex is out of bounds.
*
*    The application can call FS_STORAGE_ReadSectors() instead of
*    calling FS_STORAGE_ReadSector() multiple times if it has to
*    read consecutive logical sectors at once.
*/
int FS_STORAGE_ReadSector(const char * sVolumeName, void * pData, U32 SectorIndex) {
  int           r;
  FS_VOLUME   * pVolume;
  FS_DEV_INFO   DevInfo;

  if (pData == NULL) {
    return FS_ERRCODE_INVALID_PARA;             // Error, invalid buffer.
  }
  r = FS_ERRCODE_VOLUME_NOT_FOUND;
  FS_LOCK();
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    FS_LOCK_DRIVER(&pVolume->Partition.Device);
    r = FS__GetDeviceInfoNL(pVolume, &DevInfo);
    if (r == 0) {
      r = FS_ERRCODE_INVALID_PARA;
      if (SectorIndex < DevInfo.NumSectors) {
        r = FS__ReadSectorNL(pVolume, pData, SectorIndex);
      }
    }
    FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_STORAGE_WriteSectors
*
*  Function description
*    Modifies the data of one or more logical sector.
*
*  Parameters
*    sVolumeName      Name of the volume to write to.
*                     It cannot be NULL.
*    pData            [IN] Buffer that contains the sector data to
*                     be written. It cannot be NULL.
*    FirstSector      Index of the first sector to read from.
*    NumSectors       Number of sectors to be read.
*
*  Return value
*    ==0      OK, sector data modified.
*    !=0      Error code indicating the failure reason.
*
*  Additional information
*    This function can be used to write the contents of multiple
*    consecutive logical sectors. pData has to point to a memory
*    area that store the contents of all the logical sectors to
*    be written. The size of the logical sector is driver-dependent
*    and typically 512 bytes in size. FirstSector is a 0-based index
*    that specifies the index of the first logical sector to be written.
*    The size of the logical sector and the number of logical sectors
*    in a storage device can be determined via FS_STORAGE_GetDeviceInfo().
*
*    FS_STORAGE_WriteSectors() reports an error and does not modify
*    the contents of the logical sectors if any of the indexes of
*    the specified logical sectors is out of bounds.
*/
int FS_STORAGE_WriteSectors(const char * sVolumeName, const void * pData, U32 FirstSector, U32 NumSectors) {
  int           r;
  FS_VOLUME   * pVolume;
  FS_DEV_INFO   DevInfo;

  if (pData == NULL) {
    return FS_ERRCODE_INVALID_PARA;             // Error, invalid buffer.
  }
  r = FS_ERRCODE_VOLUME_NOT_FOUND;
  FS_LOCK();
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    FS_LOCK_DRIVER(&pVolume->Partition.Device);
    r = FS__GetDeviceInfoNL(pVolume, &DevInfo);
    if (r == 0) {
      r = FS_ERRCODE_INVALID_PARA;
      if ((FirstSector <   DevInfo.NumSectors) &&
          (NumSectors  <= (DevInfo.NumSectors - FirstSector))) {
        r = FS__WriteSectorsNL(pVolume, pData, FirstSector, NumSectors);
      }
    }
    FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_STORAGE_ReadSectors
*
*  Function description
*    Reads the data of one or more logical sectors.
*
*  Parameters
*    sVolumeName      Name of the volume to read from.
*                     It cannot be NULL.
*    pData            [OUT] Buffer that receives the read sector data.
*                     It cannot be NULL.
*    FirstSector      Index of the first sector to read from.
*    NumSectors       Number of sectors to be read.
*
*  Return value
*    ==0      OK, sector data read.
*    !=0      Error code indicating the failure reason.
*
*  Additional information
*    This function can be used to read the contents of multiple
*    consecutive logical sectors. pData has to point to a memory
*    area large enough to store the contents of all the logical
*    sectors read. The size of the logical sector is driver-dependent
*    typically 512 bytes in size. FirstSector is a 0-based index that
*    specifies the index of the first logical sector to be read.
*    The size of the logical sector and the number of logical sectors
*    in a storage device can be determined via FS_STORAGE_GetDeviceInfo().
*
*    FS_STORAGE_ReadSectors() reports an error and does not store
*    any data to pData if any of the indexes of the specified logical
*    sectors is out of bounds.
*/
int FS_STORAGE_ReadSectors(const char * sVolumeName, void * pData, U32 FirstSector, U32 NumSectors) {
  int           r;
  FS_VOLUME   * pVolume;
  FS_DEV_INFO   DevInfo;

  if (pData == NULL) {
    return FS_ERRCODE_INVALID_PARA;             // Error, invalid buffer.
  }
  r = FS_ERRCODE_VOLUME_NOT_FOUND;
  FS_LOCK();
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    FS_LOCK_DRIVER(&pVolume->Partition.Device);
    r = FS__GetDeviceInfoNL(pVolume, &DevInfo);
    if (r == 0) {
      r = FS_ERRCODE_INVALID_PARA;
      if ((FirstSector <   DevInfo.NumSectors) &&
          (NumSectors  <= (DevInfo.NumSectors - FirstSector))) {
        r = FS__ReadSectorsNL(pVolume, pData, FirstSector, NumSectors);
      }
    }
    FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_STORAGE_Unmount
*
*  Function description
*    Synchronizes a volume and marks it as not initialized.
*
*  Parameters
*    sVolumeName   Name of the volume to be unmounted.
*                  It cannot be NULL.
*
*  Additional information
*    The function sends an unmount command to the driver and marks
*    the volume as unmounted and uninitialized. If a write sector
*    cache is enabled, FS_STORAGE_Unmount() also stores any modified
*    data from sector cache to storage device.
*    This function has to be called before the device is shutdown
*    to prevent a data loss.
*
*    The file system mounts automatically the volume at the call to
*    an API function of the storage layer.
*/
void FS_STORAGE_Unmount(const char * sVolumeName) {
  FS_VOLUME * pVolume;

  FS_LOCK();
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    FS_LOCK_DRIVER(&pVolume->Partition.Device);
    FS__UnmountLL_NL(pVolume);
    FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
  }
  FS_UNLOCK();
}

/*********************************************************************
*
*       FS_STORAGE_UnmountForced
*
*  Function description
*    Marks a volume it as not initialized.
*
*  Parameters
*    sVolumeName   Name of the volume to be unmounted.
*                  It cannot be NULL.
*
*  Additional information
*    This function performs the same operations as FS_STORAGE_Unmount().
*    FS_STORAGE_UnmountForced() has to be called if a storage device
*    has been removed before being regularly unmounted. When using
*    FS_STORAGE_UnmountForced() there is no guarantee that the
*    information cached by the file system is updated to storage.
*
*    The file system mounts automatically the volume at the call to
*    an API function of the storage layer.
*/
void FS_STORAGE_UnmountForced(const char * sVolumeName) {
  FS_VOLUME * pVolume;

  FS_LOCK();
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    FS_LOCK_DRIVER(&pVolume->Partition.Device);
    FS__UnmountForcedLL_NL(pVolume);
    FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
  }
  FS_UNLOCK();
}

/*********************************************************************
*
*       FS_STORAGE_Sync
*
*  Function description
*    Writes cached information to volume.
*
*  Parameters
*    sVolumeName    Name of the volume to be synchronized.
*
*  Additional information
*    This function updates all the information present only in sector
*    cache (if enabled) to storage device. It is also requests the
*    driver to perform a synchronization operation. The operations
*    performed during the synchronization are driver-dependent.
*
*    Typically, FS_STORAGE_Sync() has to be called if a write-back
*    sector cache is configured for the volume to reduce the chance
*    of a data loss in case of an unexpected reset.
*/
void FS_STORAGE_Sync(const char * sVolumeName) {
  FS_VOLUME * pVolume;

  FS_LOCK();
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    FS__STORAGE_Sync(pVolume);
  }
  FS_UNLOCK();
}

/*********************************************************************
*
*       FS_STORAGE_GetDeviceInfo
*
*  Function description
*    Returns information about the storage device.
*
*  Parameters
*    sVolumeName    Name of the volume to be queried.
*                   It cannot be NULL.
*    pDeviceInfo    [OUT] Information about the storage device.
*                   It cannot be NULL.
*
*  Return value
*    ==0      OK, information returned.
*    !=0      Error code indicating the failure reason.
*
*  Additional information
*    This function returns information about the logical organization
*    of the storage device such as the number of logical sectors
*    and the size of the logical sector supported.
*    FS_STORAGE_GetDeviceInfo() requests the information directly
*    from the device driver.
*/
int FS_STORAGE_GetDeviceInfo(const char * sVolumeName, FS_DEV_INFO * pDeviceInfo) {
  FS_VOLUME * pVolume;
  int         r;

  if (pDeviceInfo == NULL) {
    return FS_ERRCODE_INVALID_PARA;         // Error, invalid parameter.
  }
  r = FS_ERRCODE_VOLUME_NOT_FOUND;
  FS_LOCK();
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    FS_LOCK_DRIVER(&pVolume->Partition.Device);
    r = FS__GetDeviceInfoNL(pVolume, pDeviceInfo);
    FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_IoCtl
*
*  Function description
*    Execute device command.
*
*  Parameters
*    pVolumeName    Fully qualified volume name.
*    Cmd            Command to be executed.
*    Aux            Parameter depending on command.
*    pBuffer        Pointer to a buffer used for the command.
*
*  Return value
*    Command specific. In general a negative value means an error.
*/
int FS_IoCtl(const char * pVolumeName, I32 Cmd, I32 Aux, void * pBuffer) {
  int          r;
  FS_VOLUME  * pVolume;
  FS_DEVICE  * pDevice;

  FS_LOCK();
  r = FS_ERRCODE_VOLUME_NOT_FOUND;
  pVolume = FS__FindVolume(pVolumeName);
  if (pVolume != NULL) {
    pDevice = &pVolume->Partition.Device;
    FS_LOCK_DRIVER(pDevice);
    r = FS_LB_Ioctl(pDevice, Cmd, Aux, pBuffer);
    FS_UNLOCK_DRIVER(pDevice);
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_FormatLow
*
*  Function description
*    Performs a low-level format.
*
*  Parameters
*    sVolumeName    Name of the volume to be formatted.
*
*  Return value
*    ==0 -  OK, low-level format successful.
*    !=0 -  Error code indicating the failure reason.
*
*  Additional information
*    This function prepares a storage device for the data access.
*    The file system reports an error if an attempt is made to access
*    a storage device that is not low-level formatted. All the data
*    present on the storage device is lost after a low-level format.
*
*    The low-level format operation has to be performed only for the
*    storage devices that are managed by the file system such as NAND
*    and NOR flash devices. SD cards and e.MMC devices do not require
*    a low-level format.
*/
int FS_FormatLow(const char * sVolumeName) {
  int         r;
  FS_VOLUME * pVolume;

  FS_LOCK();
  FS_PROFILE_CALL_STRING(FS_EVTID_FORMATLOW, sVolumeName);
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    FS_LOCK_DRIVER(&pVolume->Partition.Device);
    r = FS__FormatLowNL(pVolume);
    FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
  } else {
    r = FS_ERRCODE_VOLUME_NOT_FOUND;          // Error, volume not found.
  }
  FS_PROFILE_END_CALL_U32(FS_EVTID_FORMATLOW, r);
  FS_DEBUG_LOG((FS_MTYPE_API, "API: LLFORMAT VolumeName: \"%s\", r: %s.\n", sVolumeName, FS_ErrorNo2Text(r)));
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_FormatLLIfRequired
*
*  Function description
*    Performs a low-level format.
*
*  Parameters
*    sVolumeName    Name of the volume to be formatted.
*
*  Return value
*    ==0 -    OK, low-level format successful.
*    ==1 -    Low-level format not required.
*    < 0 -    Error code indicating the failure reason.
*
*  Additional information
*    This function performs a low-level format of a storage device
*    if it is not already low-level formatted. FS_FormatLLIfRequired()
*    does nothing if the storage device is already low-level formatted.
*    A storage device has to be low-level formatted once before
*    the file system can perform any data access.  All the data
*    present on the storage device is lost after a low-level format.
*
*    The low-level format operation is required only for the storage
*    devices that are managed by the file system such as NAND and NOR
*    flash devices. SD cards and e.MMC devices do not require
*    a low-level format.
*/
int FS_FormatLLIfRequired(const char * sVolumeName) {
  int r;

  r = FS_IsLLFormatted(sVolumeName);
  if (r == 0) {
    r = FS_FormatLow(sVolumeName);
  }
  return r;
}

/*********************************************************************
*
*       FS_AddDevice
*
*  Function description
*    Adds a driver to file system.
*
*  Parameters
*    pDevType   [IN] Pointer to a function table identifying the
*               driver that has to be added.
*
*  Return value
*    !=0    OK, driver added.
*    ==0    An error occurred.
*
*  Additional information
*    This function can be used to add a device or a logical driver
*    to file system. The application has to add at least one driver
*    to file system.
*
*    The function performs the following operations:
*    * Adds a physical device. This initializes the driver, allowing
*      the driver to identify the storage device if required
*      and to allocate memory for driver level management of
*      the storage device. This makes sector operations possible.
*    * Assigns a logical volume to physical device. This makes it
*      possible to mount the storage device, making it accessible
*      for the file system and allowing file operations to be performed
*      on it.
*/
FS_VOLUME * FS_AddDevice(const FS_DEVICE_TYPE * pDevType) {
  FS_VOLUME * pVolume;

  pVolume = FS__AddDevice(pDevType);
  return pVolume;
}

/*********************************************************************
*
*       FS_AddPhysDevice
*
*  Function description
*    Adds a device to file system without assigning a volume to it.
*
*  Parameters
*    pDevType   [IN] Pointer to a function table identifying the
*               driver that has to be added.
*
*  Return value
*    ==0    OK, storage device added.
*    !=0    An error occurred.
*
*  Additional information
*    This function can be used to add a device or a logical driver
*    to file system. It works similarly to FS_AddDevice() with the
*    difference that it does not assign a logical volume to storage
*    device. This means that the storage device is not directly
*    accessible by the application via the API functions of the file
*    system. An additional logical driver is required to be added
*    via FS_AddDevice() to make the storage device visible
*    to application.
*
*    FS_AddPhysDevice() initializes the driver, allowing the driver
*    to identify the storage device as far as required and allocate
*    memory required for driver level management of the device.
*    This makes sector operations possible.
*/
int FS_AddPhysDevice(const FS_DEVICE_TYPE * pDevType) {
  int r;

  r = FS__AddPhysDevice(pDevType);
  return r;
}

/*********************************************************************
*
*       FS_GetNumVolumes
*
*  Function description
*    Queries the number of configured volumes.
*
*  Return value
*    Number of volumes.
*
*  Additional information
*    This function can be used to check how many volumes are
*    configured in the file system. Each call to FS_AddDevice()
*    creates a separate volume. Calling FS_AddPhysDevice() does
*    not create a volume. The maximum number of volumes is limited
*    only by available memory.
*
*    FS_GetNumVolumes() can be used together with FS_GetVolumeName()
*    to list the names of all configured volumes.
*/
int FS_GetNumVolumes(void) {
  int r;

  FS_LOCK();
  r = (int)FS_Global.NumVolumes;
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_IsLLFormatted
*
*  Function description
*    Returns whether a volume is low-level formatted or not.
*
*  Parameters
*    sVolumeName    Name of the volume to be checked. It cannot be NULL.
*
*  Return value
*    ==1 - Volume is low-level formatted
*    ==0 - Volume is not low-level formatted
*    < 0 - Error code indicating the failure reason
*/
int FS_IsLLFormatted(const char * sVolumeName) {
  FS_VOLUME * pVolume;
  int         r;

  r = FS_ERRCODE_VOLUME_NOT_FOUND;        // Set as error so far.
  FS_LOCK();
  FS_PROFILE_CALL_STRING(FS_EVTID_ISLLFORMATTED, sVolumeName);
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    FS_LOCK_DRIVER(&pVolume->Partition.Device);
    r = FS__IsLLFormattedNL(pVolume);
    FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
  }
  FS_PROFILE_END_CALL_U32(FS_EVTID_ISLLFORMATTED, r);
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_GetVolumeName
*
*  Function description
*    Returns the name of a volume.
*
*  Parameters
*    VolumeIndex      0-based index of the volume to be queried.
*    sVolumeName      [OUT] Receives the name of the volume as
*                     0-terminated string. It cannot be NULL.
*    VolumeNameSize   Number of bytes in sVolumeName.
*
*  Return value
*    > 0    Number of bytes required to store the volume name.
*    < 0    An error occurred.
*
*  Additional information
*    If the function succeeds, the return value is the length of the
*    string copied to sVolumeName in bytes, excluding the 0-terminating
*    character. FS_GetVolumeName() stores at most VolumeNameSize - 1
*    characters to sVolumeName and it terminates the string by a 0.
*
*    VolumeIndex specifies the position of the volume in the internal
*    volume list of the file system. The first volume added to file
*    system in FS_X_AddDevices() via FS_AddDevice() is stored at
*    index 0 in the volume list, the second volume added via
*    FS_AddDevice() is store at the index 1 in the volume list and
*    so on. The total number of volumes can be queried via
*    FS_GetNumVolumes().
*
*    If the sVolumeName is too small to hold the entire volume
*    name, the return value is the size of the buffer required to
*    hold the volume name plus the terminating 0 character. Therefore,
*    if the return value is greater than VolumeNameSize,
*    the application has to call FS_GetVolumeName() again with a buffer
*    that is large enough to hold the volume name.
*/
int FS_GetVolumeName(int VolumeIndex, char * sVolumeName, int VolumeNameSize) {
  FS_VOLUME * pVolume;
  int         r;
  int         iVolume;

  FS_LOCK();
  r = FS_ERRCODE_INVALID_PARA;      // Error, invalid volume index.
  pVolume = &FS_Global.FirstVolume;
  FS_LOCK_SYS();
  for (iVolume = 0; iVolume < VolumeIndex; iVolume++) {
    pVolume = pVolume->pNext;
    if (pVolume == NULL) {
      break;                        // Quit the loop when we reach the end of volume list.
    }
  }
  FS_UNLOCK_SYS();
  if (pVolume != NULL) {
    r = FS__GetVolumeName(pVolume, sVolumeName, VolumeNameSize);
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_GetVolumeStatus
*
*  Function description
*    Returns the presence status of a volume.
*
*  Parameters
*    sVolumeName    Name of the volume. It cannot be NULL.
*
*  Return value
*    ==FS_MEDIA_NOT_PRESENT     Storage device is not present.
*    ==FS_MEDIA_IS_PRESENT      Storage device is present.
*    ==FS_MEDIA_STATE_UNKNOWN   Presence status is unknown.
*
*  Additional information
*    This function can be used to check if a removable storage device
*    that is assigned to a volume is present or not.
*    FS_GetVolumeStatus() is typically called periodically from a
*    separate task to handle the insertion and removal of a
*    removable storage device.
*/
int FS_GetVolumeStatus(const char * sVolumeName) {
  FS_VOLUME * pVolume;
  int         r;

  r = FS_MEDIA_STATE_UNKNOWN;
  FS_LOCK();
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    FS_LOCK_DRIVER(&pVolume->Partition.Device);
    r = FS__GetVolumeStatusNL(pVolume);
    FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
  }
  FS_UNLOCK();
  return r;
}

#if FS_STORAGE_SUPPORT_DEVICE_ACTIVITY

/*********************************************************************
*
*       FS_STORAGE_SetOnDeviceActivityCallback
*
*  Function description
*    Registers a function to be called on any logical sector read
*    or write operation.
*
*  Parameters
*    sVolumeName            Name of the volume for which the callback
*                           function is registered. It cannot be NULL.
*    pfOnDeviceActivity     Function to be invoked.
*
*  Additional information
*    This function is optional. It is available only when the file
*    system is built with FS_DEBUG_LEVEL set to a value grater than
*    or equal to FS_DEBUG_LEVEL_CHECK_PARA or with
*    FS_STORAGE_SUPPORT_DEVICE_ACTIVITY set to 1.
*/
void FS_STORAGE_SetOnDeviceActivityCallback(const char * sVolumeName, FS_ON_DEVICE_ACTIVITY_CALLBACK * pfOnDeviceActivity) {
  FS_VOLUME * pVolume;

  FS_LOCK();
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    FS_LOCK_SYS();
    pVolume->Partition.Device.Data.pfOnDeviceActivity = pfOnDeviceActivity;
    FS_UNLOCK_SYS();
  }
  FS_UNLOCK();
}

#endif // FS_STORAGE_SUPPORT_DEVICE_ACTIVITY

#if FS_STORAGE_ENABLE_STAT_COUNTERS

/*********************************************************************
*
*       FS_STORAGE_GetCounters
*
*  Function description
*    Returns the values of statistical counters.
*
*  Parameters
*    pStat      [OUT] Current values of statistical counters.
*
*  Additional information
*    This function returns the values of the counters that indicate
*    how many operations the storage layer executed since the file
*    system initialization or the last call to
*    FS_STORAGE_ResetCounters().
*
*    The statistical counters are updated only on debug builds if the
*    file system sources are compiled with FS_DEBUG_LEVEL greater then
*    or equal to FS_DEBUG_LEVEL_CHECK_PARA or FS_STORAGE_ENABLE_STAT_COUNTERS
*    set to 1.
*/
void FS_STORAGE_GetCounters(FS_STORAGE_COUNTERS * pStat) {
  if (pStat != NULL) {
    FS_LOCK();
    FS_LOCK_SYS();
    *pStat = FS_STORAGE_Counters;
    FS_UNLOCK_SYS();
    FS_UNLOCK();
  }
}

/*********************************************************************
*
*       FS_STORAGE_ResetCounters
*
*  Function description
*    Sets all statistical counters to 0.
*
*  Additional information
*    This function can be used to set to 0 all the statistical
*    counters maintained by the storage layer. This can be useful
*    for example in finding out how many sector operations are
*    performed during a specific file system operation.
*    The application calls FS_STORAGE_ResetCounters() before the
*    file system operation and then FS_STORAGE_GetCounters()
*    at the end.
*
*    The statistical counters are available only on debug builds
*    if the file system sources are compiled with FS_DEBUG_LEVEL
*    greater then or equal to FS_DEBUG_LEVEL_CHECK_PARA or
*    FS_STORAGE_ENABLE_STAT_COUNTERS set to 1.
*/
void FS_STORAGE_ResetCounters(void) {
  FS_LOCK();
  FS_LOCK_SYS();
  FS_MEMSET(&FS_STORAGE_Counters, 0, sizeof(FS_STORAGE_COUNTERS));
  FS_UNLOCK_SYS();
  FS_UNLOCK();
}

#endif // FS_STORAGE_ENABLE_STAT_COUNTERS

/*********************************************************************
*
*       FS_STORAGE_Clean
*
*  Function description
*    Performs garbage collection on a volume.
*
*  Parameters
*    sVolumeName      Name of the volume on which to perform
*                     the garbage collection.
*
*  Return value
*    ==0   OK, volume cleaned.
*    !=0   Error code indicating the failure reason.
*
*  Additional information
*    The application can call this function to convert storage blocks
*    that contain invalid data to free space that can be used to
*    store new data. This operation is supported only by storage devices
*    that are managed by the file system such as NAND and NOR flash.
*
*    FS_STORAGE_Clean() is optional since the device drivers perform
*    the garbage collection operation automatically. The function
*    can be used to increase the write performance by preparing the
*    storage device in advance of the write operation.
*
*    The actions executed by FS_STORAGE_Clean() are
*    device-driver-dependent. For example, the sector map NOR driver
*    converts all logical blocks that contain invalid data into free
*    (writable) logical blocks. As a consequence, the following write
*    operation will run faster since no physical sector is required
*    to be erased.
*
*    FS_STORAGE_Clean() can potentially take a long time to complete,
*    preventing the access of other tasks to the file system. How long
*    the execution takes depends on the type of storage device and on
*    the number of storage blocks that contain invalid data.
*    The file system provides an alternative function
*    FS_STORAGE_CleanOne() that completes in a shorter period of time
*    than FS_STORAGE_Clean(). This is realized by executing only one
*    sub-operation of the entire garbage collection operation
*    at a time. For more information refer to FS_STORAGE_CleanOne().
*
*    Additional information about the garbage collection operation
*    can be found in the "Garbage collection" section of the NAND
*    and NOR drivers of the emFile manual.
*/
int FS_STORAGE_Clean(const char * sVolumeName) {
  int         r;
  FS_VOLUME * pVolume;

  FS_LOCK();
  r = FS_ERRCODE_VOLUME_NOT_FOUND;
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    r = FS__IoCtl(pVolume, FS_CMD_CLEAN, 0, NULL);
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_STORAGE_CleanOne
*
*  Function description
*    Performs garbage collection on a volume.
*
*  Parameters
*    sVolumeName    Name of the storage volume on which to perform
*                   the garbage collection.
*    pMoreToClean   [OUT] Indicates if the storage device has been
*                   cleaned completely or not. It can be NULL.
*                   * !=0   Not cleaned completely.
*                   * ==0   Completely clean.
*
*  Return value
*    ==0   OK, clean operation executed.
*    !=0   Error code indicating the failure reason.
*
*  Additional information
*    This function performs the same operation as FS_STORAGE_Clean()
*    with the difference that it executes only one sub-operation of
*    the garbage collection operation at a time. This is practical
*    if other tasks of the application require access to the file
*    system before the garbage collection operation is completed.
*    The completion of the garbage collection operation is indicated
*    via pMoreToClean. FS_STORAGE_CleanOne() returns a value different
*    than 0 via pMoreToClean if the operation is not completed.
*
*    Additional information about the garbage collection operation
*    can be found in the "Garbage collection" section of the NAND
*    and NOR drivers.
*/
int FS_STORAGE_CleanOne(const char * sVolumeName, int * pMoreToClean) {
  int         r;
  FS_VOLUME * pVolume;

  FS_LOCK();
  r = FS_ERRCODE_VOLUME_NOT_FOUND;
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    r = FS__IoCtl(pVolume, FS_CMD_CLEAN_ONE, 0, pMoreToClean);
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_STORAGE_GetCleanCnt
*
*  Function description
*    Calculates the number of garbage collection sub-operations.
*
*  Parameters
*    sVolumeName  Name of the volume to be queried.
*    pCleanCnt    [OUT] Number of sub-operations left.
*
*  Return value
*    ==0   OK, clean count returned.
*    !=0   Error code indicating the failure reason.
*
*  Additional information
*    FS_STORAGE_GetCleanCnt() calculates and returns the number
*    of sub-operations that the application has to perform
*    until the garbage collection operation is completed.
*    The value returned via pCleanCnt is the number of times
*    FS_STORAGE_CleanOne() has to be called to complete
*    the garbage collection. FS_STORAGE_GetCleanCnt() is supported
*    only for volumes mounted on a storage device managed by emFile
*    such as NAND or NOR flash.
*/
int FS_STORAGE_GetCleanCnt(const char * sVolumeName, U32 * pCleanCnt) {
  int         r;
  FS_VOLUME * pVolume;

  FS_LOCK();
  r = FS_ERRCODE_VOLUME_NOT_FOUND;
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    r = FS__IoCtl(pVolume, FS_CMD_GET_CLEAN_CNT, 0, pCleanCnt);
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_STORAGE_RefreshSectors
*
*  Function description
*    Reads the contents of a logical sector and writes it back.
*
*  Parameters
*    sVolumeName   Name of the volume on which to perform the
*                  operation. It cannot be NULL.
*    FirstSector   Index of the first sector to refresh (0-based).
*    NumSectors    Number of sectors to refresh starting from FirstSector.
*    pBuffer       Temporary storage for the sector data.
*                  Must be at least one sector large.
*                  It cannot be NULL.
*    NumBytes      Number of bytes in pBuffer.
*
*  Return value
*    ==0    OK, sectors refreshed.
*    !=0    Error code indicating the failure reason
*
*  Additional information
*    This function reads the contents of each specified logical
*    sector to pBuffer and then it writes the same data to it.
*    FS_STORAGE_RefreshSectors() can read and write more than one
*    logical sector at once if the size of pBuffer allows it.
*    The larger pBuffer the faster runs the refresh operation.
*
*    FS_STORAGE_RefreshSectors() function can be used on volumes
*    mounted on a NAND flash device to prevent the accumulation
*    of bit errors due to excessive read operations (read disturb
*    effect). The function can also be used to prevent data loses
*    caused by the data reaching the retention limit.
*    Reading and then writing back the contents of a logical
*    sector causes the NAND driver to relocate the data on the NAND
*    flash device that in turn eliminates bit errors.
*    Typically, the refresh operation has to be performed periodically
*    at large time intervals (weeks). The NAND flash may wear out
*    too soon if the refresh operation is performed too often.
*/
int FS_STORAGE_RefreshSectors(const char * sVolumeName, U32 FirstSector, U32 NumSectors, void * pBuffer, U32 NumBytes) {
  int           r;
  FS_VOLUME   * pVolume;
  FS_DEV_INFO   DevInfo;
  unsigned      NumSectorsInBuffer;
  U32           NumSectorsDiff;

  if (NumSectors == 0u) {
    return FS_ERRCODE_INVALID_PARA;         // Invalid number of sectors
  }
  FS_LOCK();
  r = FS_ERRCODE_VOLUME_NOT_FOUND;
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    FS_LOCK_DRIVER(&pVolume->Partition.Device);
    r = FS__GetDeviceInfoNL(pVolume, &DevInfo);
    FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
    if (r == 0) {
      r = FS_ERRCODE_INVALID_PARA;
      NumSectorsInBuffer = NumBytes / DevInfo.BytesPerSector;
      if ((FirstSector < DevInfo.NumSectors) && (NumSectorsInBuffer != 0u)) {
        //
        // Limit the number of sectors to the number of sectors available on device.
        //
        NumSectorsDiff = DevInfo.NumSectors - FirstSector;
        NumSectors     = SEGGER_MIN(NumSectorsDiff, NumSectors);
        r = FS__STORAGE_RefreshSectors(pVolume, FirstSector, NumSectors, pBuffer, NumSectorsInBuffer);
      }
    }
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_STORAGE_FreeSectors
*
*  Function description
*    Informs the driver about unused sectors.
*
*  Parameters
*    sVolumeName   Name of the volume on which to perform the
*                  operation.
*    FirstSector   Index of the first logical sector to be marked
*                  as invalid (0-based).
*    NumSectors    Number of sectors to be marked as invalid starting
*                  from FirstSector.
*
*  Return value
*    ==0    OK, sectors freed.
*    !=0    Error code indicating the failure reason
*
*  Additional information
*    Typically, this function is called by the application to inform
*    the driver which logical sectors are no longer used for data
*    storage. The NAND and NOR drivers can use this information to
*    optimize the internal relocation of data blocks during the
*    wear-leveling operation. The data of the logical sectors marked
*    as not in use is not copied anymore that can typically lead to
*    an improvement of the write performance.
*
*    FS_STORAGE_FreeSectors() performs a similar operation as the trim
*    command of SSDs (Solid-State Drives). The data stored to a
*    logical sector is no longer available to an application after
*    the logical sector has been freed via FS_STORAGE_FreeSectors(),
*/
int FS_STORAGE_FreeSectors(const char * sVolumeName, U32 FirstSector, U32 NumSectors) {
  int           r;
  FS_VOLUME   * pVolume;
  FS_DEV_INFO   DevInfo;
  FS_DEVICE   * pDevice;

  FS_LOCK();
  r = FS_ERRCODE_VOLUME_NOT_FOUND;
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    pDevice = &pVolume->Partition.Device;
    FS_LOCK_DRIVER(pDevice);
    r = FS__GetDeviceInfoNL(pVolume, &DevInfo);
    if (r == 0) {
      r = FS_ERRCODE_INVALID_PARA;
      if ((FirstSector <   DevInfo.NumSectors) &&
          (NumSectors  <= (DevInfo.NumSectors - FirstSector))) {
        r = FS_LB_FreeSectorsDevice(pDevice, FirstSector, NumSectors);
      }
    }
    FS_UNLOCK_DRIVER(pDevice);
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_STORAGE_SyncSectors
*
*  Function description
*    Synchronize the contents of one or more logical sectors.
*
*  Parameters
*    sVolumeName   Name of the volume.
*    FirstSector   Index of the first sector to be synchronized (0-based).
*    NumSectors    Number of sectors to be synchronized starting from FirstSector.
*
*  Return value
*    ==0    OK, sectors synchronized.
*    !=0    Error code indicating the failure reason.
*
*  Additional information
*    This operation is driver-dependent and is currently supported
*    only by the RAID1 logical driver. The operation updates the
*    contents of the specified logical sectors that are located on
*    the secondary storage (mirrored) with the contents of the
*    corresponding logical sectors from the primary storage (master).
*    RAID1 logical driver updates the logical sectors only if
*    the contents is different.
*/
int FS_STORAGE_SyncSectors(const char * sVolumeName, U32 FirstSector, U32 NumSectors) {
  int           r;
  FS_VOLUME   * pVolume;
  FS_DEV_INFO   DevInfo;

  FS_LOCK();
  r = FS_ERRCODE_VOLUME_NOT_FOUND;
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    FS_LOCK_DRIVER(&pVolume->Partition.Device);
    r = FS__GetDeviceInfoNL(pVolume, &DevInfo);
    if (r == 0) {
      r = FS_ERRCODE_INVALID_PARA;
      if ((FirstSector <   DevInfo.NumSectors) &&
          (NumSectors  <= (DevInfo.NumSectors - FirstSector))) {
        r = FS__IoCtlNL(pVolume, FS_CMD_SYNC_SECTORS, (I32)FirstSector, &NumSectors);
      }
    }
    FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_STORAGE_GetSectorUsage
*
*  Function description
*    Returns information about the usage of a logical sector.
*
*  Parameters
*    sVolumeName   Name of the volume.
*    SectorIndex   Index of the sector to be queried.
*
*  Return value
*    ==FS_SECTOR_IN_USE         The sector contains valid data.
*    ==FS_SECTOR_NOT_USED       The sector contains invalid data.
*    ==FS_SECTOR_USAGE_UNKNOWN  The usage of the sector is unknown
*                               or the operation is not supported.
*    < 0                        Error code indicating the failure reason.
*
*  Additional information
*    After a low-level format all the logical sectors contain invalid
*    information. The data of a logical sector becomes valid after
*    the application performs a write operation to that sector.
*    The sector data can be explicitly invalidated by calling
*    FS_STORAGE_FreeSectors().
*/
int FS_STORAGE_GetSectorUsage(const char * sVolumeName, U32 SectorIndex) {
  int           r;
  FS_VOLUME   * pVolume;
  FS_DEV_INFO   DevInfo;

  FS_LOCK();
  r = FS_ERRCODE_VOLUME_NOT_FOUND;
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    FS_LOCK_DRIVER(&pVolume->Partition.Device);
    r = FS__GetDeviceInfoNL(pVolume, &DevInfo);
    FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
    if (r == 0) {
      r = FS_ERRCODE_INVALID_PARA;
      if (SectorIndex < DevInfo.NumSectors) {
        r = FS__STORAGE_GetSectorUsage(pVolume, SectorIndex);
      }
    }
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_STORAGE_FindVolume
*
*  Function description
*    Searches for a volume instance by name.
*
*  Parameters
*    sVolumeName  Name of the volume.
*
*  Return value
*    !=NULL       Pointer to the volume instance.
*    ==NULL       Volume not found.
*
*  Additional information
*    This function returns the volume instance assigned to the
*    specified volume name. The return value can be passed as
*    parameter to the API functions of the Storage layer that
*    identify a volume by instance and not by name.
*
*    The returned volume instance is no longer valid after
*    the file system is deinitialized via a call to FS_DeInit().
*    The application must call FS_STORAGE_FindVolume() to
*    get a new volume instance after the file system is
*    reinitialized.
*/
FS_VOLUME * FS_STORAGE_FindVolume(const char * sVolumeName) {
  FS_VOLUME * pVolume;

  FS_LOCK();
  pVolume = FS__FindVolume(sVolumeName);
  FS_UNLOCK();
  return pVolume;
}

/*********************************************************************
*
*       FS_STORAGE_FormatLowEx
*
*  Function description
*    Formats the storage device.
*
*  Parameters
*    pVolume    Instance of the volume to be formatted.
*
*  Return value
*    ==0      OK, low-level format was successful.
*    !=0      Error code indicating the failure reason.
*
*  Additional information
*    This function performs the same operation as FS_FormatLow()
*    with the difference that the volume to be queried is identified
*    by a volume instance instead a volume name. The application
*    can get a volume instance via FS_STORAGE_FindVolume().
*/
int FS_STORAGE_FormatLowEx(FS_VOLUME * pVolume) {
  int r;

  r = FS_ERRCODE_INVALID_PARA;
  FS_LOCK();
  if (pVolume != NULL) {
    FS_LOCK_DRIVER(&pVolume->Partition.Device);
    r = FS__FormatLowNL(pVolume);
    FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_STORAGE_GetDeviceInfoEx
*
*  Function description
*    Returns information about the storage device.
*
*  Parameters
*    pVolume      Instance of the volume to be queried.
*    pDeviceInfo  [OUT] Information about the storage device.
*
*  Return value
*    ==0      OK, volume information returned.
*    !=0      Error code indicating the failure reason.
*
*  Additional information
*    This function performs the same operation as FS_STORAGE_GetDeviceInfo()
*    with the difference that the volume to be queried is identified
*    by a volume instance instead a volume name. The application
*    can get a volume instance via FS_STORAGE_FindVolume().
*/
int FS_STORAGE_GetDeviceInfoEx(FS_VOLUME * pVolume, FS_DEV_INFO * pDeviceInfo) {
  int r;

  r = FS_ERRCODE_INVALID_PARA;    // Set as error so far.
  FS_LOCK();
  if (pVolume != NULL) {
    FS_LOCK_DRIVER(&pVolume->Partition.Device);
    r = FS__GetDeviceInfoNL(pVolume, pDeviceInfo);
    FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_STORAGE_GetVolumeStatusEx
*
*  Function description
*    Returns the presence status of a volume.
*
*  Parameters
*    pVolume    Instance of the volume to be queried.
*
*  Return value
*    ==FS_MEDIA_NOT_PRESENT     Volume is not present.
*    ==FS_MEDIA_IS_PRESENT      Volume is present.
*    ==FS_MEDIA_STATE_UNKNOWN   Volume state is unknown.
*
*  Additional information
*    This function performs the same operation as FS_GetVolumeStatus()
*    with the difference that the volume to be queried is identified
*    by a volume instance instead a volume name. The application
*    can get a volume instance via FS_STORAGE_FindVolume().
*/
int FS_STORAGE_GetVolumeStatusEx(FS_VOLUME * pVolume) {
  int r;

  r = FS_MEDIA_STATE_UNKNOWN;
  FS_LOCK();
  if (pVolume != NULL) {
    FS_LOCK_DRIVER(&pVolume->Partition.Device);
    r = FS__GetVolumeStatusNL(pVolume);
    FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_STORAGE_IsLLFormattedEx
*
*  Function description
*    Checks if a storage device is low-level formatted.
*
*  Parameters
*    pVolume    Instance of the volume to be queried.
*
*  Return value
*    ==1      Volume is low-level formatted.
*    ==0      Volume is not low-level formatted.
*    < 0      Low-level format not supported by volume or an error occurred.
*
*  Additional information
*    This function performs the same operation as FS_IsLLFormatted()
*    with the difference that the volume to be queried is identified
*    by a volume instance instead a volume name. The application
*    can get a volume instance via FS_STORAGE_FindVolume().
*/
int FS_STORAGE_IsLLFormattedEx(FS_VOLUME * pVolume) {
  int r;

  r = FS_ERRCODE_INVALID_PARA;        // Set to indicate error.
  FS_LOCK();
  if (pVolume != NULL) {
    FS_LOCK_DRIVER(&pVolume->Partition.Device);
    r = FS__IsLLFormattedNL(pVolume);
    FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_STORAGE_ReadSectorEx
*
*  Function description
*    Reads the contents of one logical sector from the storage device.
*
*  Parameters
*    pVolume      Instance of the volume to read from.
*    pData        [OUT] Contents of the logical sector.
*    SectorIndex  Index of the logical sector to be read.
*
*  Return value
*    ==0      OK, logical sector read successfully.
*    !=0      Error code indicating the failure reason.
*
*  Additional information
*    This function performs the same operation as FS_STORAGE_ReadSector()
*    with the difference that the volume is identified
*    by a volume instance instead a volume name. The application
*    can get a volume instance via FS_STORAGE_FindVolume().
*/
int FS_STORAGE_ReadSectorEx(FS_VOLUME * pVolume, void * pData, U32 SectorIndex) {
  int r;

  r = FS_ERRCODE_INVALID_PARA;
  FS_LOCK();
  if (pVolume != NULL) {
    FS_LOCK_DRIVER(&pVolume->Partition.Device);
    r = FS__ReadSectorNL(pVolume, pData, SectorIndex);
    FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_STORAGE_ReadSectorsEx
*
*  Function description
*    Reads the contents of multiple logical sectors from a storage device.
*
*  Parameters
*    pVolume      Instance of the volume to read from.
*    pData        [OUT] Contents of the logical sector.
*    SectorIndex  Index of the logical sector to be read.
*    NumSectors   Number of logical sectors to be read.
*
*  Return value
*    ==0      OK, logical sectors read successfully.
*    !=0      Error code indicating the failure reason
*
*  Additional information
*    This function performs the same operation as FS_STORAGE_ReadSectors()
*    with the difference that the volume is identified
*    by a volume instance instead a volume name. The application
*    can get a volume instance via FS_STORAGE_FindVolume().
*/
int FS_STORAGE_ReadSectorsEx(FS_VOLUME * pVolume, void * pData, U32 SectorIndex, U32 NumSectors) {
  int r;

  r = FS_ERRCODE_INVALID_PARA;
  FS_LOCK();
  if (pVolume != NULL) {
    FS_LOCK_DRIVER(&pVolume->Partition.Device);
    r = FS__ReadSectorsNL(pVolume, pData, SectorIndex, NumSectors);
    FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_STORAGE_UnmountEx
*
*  Function description
*    Synchronizes the volume and marks it as not initialized.
*
*  Parameters
*    pVolume      Instance of the volume to be unmounted.
*
*  Additional information
*    This function performs the same operation as FS_STORAGE_Unmount()
*    with the difference that the volume is identified
*    by a volume instance instead a volume name. The application
*    can get a volume instance via FS_STORAGE_FindVolume().
*/
void FS_STORAGE_UnmountEx(FS_VOLUME * pVolume) {
  FS_LOCK();
  if (pVolume != NULL) {
    FS_LOCK_DRIVER(&pVolume->Partition.Device);
    FS__UnmountLL_NL(pVolume);
    FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
  }
  FS_UNLOCK();
}

/*********************************************************************
*
*       FS_STORAGE_UnmountForcedEx
*
*  Function description
*    Marks the volume as not initialized without synchronizing it.
*
*  Parameters
*    pVolume      Instance of the volume to be unmounted.
*
*  Additional information
*    This function performs the same operation as FS_STORAGE_UnmountForced()
*    with the difference that the volume is identified
*    by a volume instance instead a volume name. The application
*    can get a volume instance via FS_STORAGE_FindVolume().
*/
void FS_STORAGE_UnmountForcedEx(FS_VOLUME * pVolume) {
  FS_LOCK();
  if (pVolume != NULL) {
    FS_LOCK_DRIVER(&pVolume->Partition.Device);
    FS__UnmountForcedLL_NL(pVolume);
    FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
  }
  FS_UNLOCK();
}

/*********************************************************************
*
*       FS_STORAGE_WriteSectorEx
*
*  Function description
*    Writes a logical sector to the storage device.
*
*  Parameters
*    pVolume      Instance of the volume to write to.
*    pData        [IN] Contents of the logical sector.
*    SectorIndex  Index of the logical sector to be written.
*
*  Return value
*    ==0      OK, logical sector written successfully.
*    !=0      Error code indicating the failure reason.
*
*  Additional information
*    This function performs the same operation as FS_STORAGE_WriteSector()
*    with the difference that the volume is identified
*    by a volume instance instead a volume name. The application
*    can get a volume instance via FS_STORAGE_FindVolume().
*/
int FS_STORAGE_WriteSectorEx(FS_VOLUME * pVolume, const void * pData, U32 SectorIndex) {
  int r;

  r = FS_ERRCODE_INVALID_PARA;
  FS_LOCK();
  if (pVolume != NULL) {
    FS_LOCK_DRIVER(&pVolume->Partition.Device);
    r = FS__WriteSectorNL(pVolume, pData, SectorIndex);
    FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_STORAGE_WriteSectorsEx
*
*  Function description
*    Writes multiple logical sectors to the storage device.
*
*  Parameters
*    pVolume      Instance of the volume to write to.
*    pData        [IN] Contents of the logical sector.
*    SectorIndex  Index of the logical sector to be written.
*    NumSectors   Number of logical sectors to be written.
*
*  Return value
*    ==0      OK, logical sectors written successfully.
*    !=0      Error code indicating the failure reason.
*
*  Additional information
*    This function performs the same operation as FS_STORAGE_WriteSectors()
*    with the difference that the volume is identified
*    by a volume instance instead a volume name. The application
*    can get a volume instance via FS_STORAGE_FindVolume().
*/
int FS_STORAGE_WriteSectorsEx(FS_VOLUME * pVolume, const void * pData, U32 SectorIndex, U32 NumSectors) {
  int r;

  r = FS_ERRCODE_INVALID_PARA;
  FS_LOCK();
  if (pVolume != NULL) {
    FS_LOCK_DRIVER(&pVolume->Partition.Device);
    r = FS__WriteSectorsNL(pVolume, pData, SectorIndex, NumSectors);
    FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
  }
  FS_UNLOCK();
  return r;
}

/*************************** End of file ****************************/
