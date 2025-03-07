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
File        : FS_FAT_FormatSD.c
Purpose     : Implementation of the SD FS Format spec. V2.00.
Literature  :
  [1] SD Specifications Part 2 File System Specification
    (\\FILESERVER\Techinfo\Company\SDCard_org\Copyrighted_0812\Part 02 File System\Part 2 File System Specification V2.00 Final 060509.pdf)
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

#if FS_SUPPORT_FAT

/*********************************************************************
*
*       Defines, fixed
*
**********************************************************************
*/
#define NUM_ROOT_DIR_ENTRIES      512

/*********************************************************************
*
*       Local data types
*
**********************************************************************
*/

/*********************************************************************
*
*       SIZE_INFO
*/
typedef struct {
  U32 NumSectors;
  U16 SectorsPerCluster;
  U32 BoundaryUnit;
} SIZE_INFO;

/*********************************************************************
*
*       Static const data
*
**********************************************************************
*/
static const SIZE_INFO _aSizeInfo[] = {
  {0x00003FFFuL, 16,   16},  // Up to     8 MBytes
  {0x0001FFFFuL, 32,   32},  // Up to    64 MBytes
  {0x0007FFFFuL, 32,   64},  // Up to   256 MBytes
  {0x001FFFFFuL, 32,  128},  // Up to  1024 MBytes
  {0x003FFFFFuL, 64,  128},  // Up to  2048 MBytes
  {0x03FFFFFFuL, 64, 8192}   // Up to 32768 MBytes
};

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _CalcFormatInfo
*
*  Return value
*    ==0      OK, format information calculated.
*    !=0      Error, invalid parameters or storage too small.
*/
static int _CalcFormatInfo(const FS_DEV_INFO * pDevInfo, FAT_FORMAT_INFO * pFormatInfo, FS_PARTITION_INFO_MBR * pPartInfo) {
  unsigned          i;
  U32               NumClusters;
  U32               NumSectorsAT;
  U32               NumSectorsSystemArea;
  U32               PartStartSector;
  U32               NumSectorsReserved;
  unsigned          FATType;
  const SIZE_INFO * pSizeInfo;
  U32               NumSectors;
  U32               BytesPerSector;
  U32               BoundaryUnit;
  unsigned          SectorsPerCluster;
  unsigned          NumRootDirEntries;
  U32               NumClustersCalc;
  U32               NumSectorsATCalc;
  int               r;
  unsigned          FATTypeCalc;

  r       = 0;
  FATType = FS_FAT_TYPE_UNKONWN;
  for (;;) {
    NumRootDirEntries = 0;
    NumSectors        = pDevInfo->NumSectors;
    BytesPerSector    = pDevInfo->BytesPerSector;
    pSizeInfo         = _aSizeInfo;
    for (i = 0; i < SEGGER_COUNTOF(_aSizeInfo); i++) {
      pSizeInfo = &_aSizeInfo[i];
      if (pSizeInfo->NumSectors > NumSectors) {
        break;
      }
    }
    BoundaryUnit      = pSizeInfo->BoundaryUnit;
    SectorsPerCluster = pSizeInfo->SectorsPerCluster;
    NumClusters       = NumSectors / SectorsPerCluster;
    if (FATType == FS_FAT_TYPE_UNKONWN) {
      FATType         = FS_FAT_GetFATType(NumClusters);
    }
    NumSectorsAT      = FS__DivideU32Up(NumClusters * FATType, BytesPerSector * 8u);
    //
    // Calculate the format parameters using the algorithms
    // presented in the sections "C.1.4 Format Parameter Computations"
    // and "C.2.4 Format Parameter Computations " of [1].
    //
    // Mapping of algorithm parameters:
    //   BoundaryUnit           BU
    //   BytesPerSector         SS
    //   NumClustersCalc        MAX - 1
    //   NumRootDirEntries      RDE
    //   NumSectors             TS
    //   NumSectorsSystemArea   SSA
    //   NumSectorsAT           SF
    //   NumSectorsATCalc       SF'
    //   NumSectorsReserved     RSC
    //   PartStartSector        NOM
    //   SectorsPerCluster      SC
    //
    if (FATType != FS_FAT_TYPE_FAT32) {
      NumSectorsReserved = 1;
      NumRootDirEntries  = NUM_ROOT_DIR_ENTRIES;
      for (;;) {
        NumSectorsSystemArea = NumSectorsReserved + FAT_NUM_ALLOC_TABLES * NumSectorsAT + FS__DivideU32Up((U32)NumRootDirEntries * 32uL, BytesPerSector);
        //
        // Find the correct multiplier for the boundary unit.
        //
        i = 1;
        for (;;) {
          if ((i * BoundaryUnit) > NumSectorsSystemArea) {
            break;
          }
          i++;
        }
        PartStartSector = i * BoundaryUnit - NumSectorsSystemArea;
        if (PartStartSector != BoundaryUnit) {
          PartStartSector += BoundaryUnit;
        }
        //
        // Recalculate format parameters if required.
        //
        for (;;) {
          if (NumSectors <= (PartStartSector + NumSectorsSystemArea)) {
            r = 1;                            // Error, storage device too small.
            goto Done;
          }
          NumClustersCalc  = (NumSectors - (PartStartSector + NumSectorsSystemArea)) / SectorsPerCluster;
          NumSectorsATCalc = FS__DivideU32Up((2u + NumClustersCalc) * FATType, BytesPerSector * 8u);
          if (NumSectorsATCalc == NumSectorsAT) {
            NumClusters = NumClustersCalc;
            goto Done;                        // Done, correct format parameters found.
          }
          if (NumSectorsATCalc > NumSectorsAT) {
            PartStartSector += BoundaryUnit;
          } else {
            NumSectorsAT = NumSectorsATCalc;
            break;                            // We have to recalculate parameters.
          }
        }
      }
    } else {
      PartStartSector = BoundaryUnit;
      for (;;) {
        NumSectorsReserved = FAT_NUM_ALLOC_TABLES * NumSectorsAT;
        //
        // Find the correct multiplier for the boundary unit.
        //
        i = 1;
        for (;;) {
          if ((i * BoundaryUnit) > NumSectorsReserved) {
            break;
          }
          i++;
        }
        NumSectorsReserved = (i * BoundaryUnit) - FAT_NUM_ALLOC_TABLES * NumSectorsAT;
        if (NumSectorsReserved < 9u) {
          NumSectorsReserved += BoundaryUnit;
        }
        NumSectorsSystemArea = NumSectorsReserved + FAT_NUM_ALLOC_TABLES * NumSectorsAT;
        //
        // Recalculate format parameters if required.
        //
        for (;;) {
          NumClustersCalc  = (NumSectors - (PartStartSector + NumSectorsSystemArea)) / SectorsPerCluster;
          NumSectorsATCalc = FS__DivideU32Up((2u + NumClustersCalc) * FATType, BytesPerSector * 8u);
          if (NumSectorsATCalc == NumSectorsAT) {
            NumClusters = NumClustersCalc;
            goto Done;                // Done, correct format parameters found.
          }
          if (NumSectorsATCalc > NumSectorsAT) {
            NumSectorsSystemArea += BoundaryUnit;
            NumSectorsReserved   += BoundaryUnit;
          } else {
            --NumSectorsAT;
            break;                    // We have to recalculate parameters.
          }
        }
      }
    }
Done:
    if (r == 1) {
      break;                          // Error, invalid parameters.
    }
    //
    // Check if the FAT type matches the initially calculated value.
    // If not, calculate the FAT type again based on the actual
    // number of clusters.
    //
    FATTypeCalc = FS_FAT_GetFATType(NumClusters);
    if (FATTypeCalc == FATType) {
      break;
    }
    if (FATTypeCalc > FATType) {    // Assume that the types are ordered in this way FA12 < FAT16 < FAT32
      r = 1;                        // Error, invalid parameters.
      break;
    }
    FATType = FATTypeCalc;
  }
  if (pFormatInfo != NULL) {
    //
    // Store the information about how the storage device has to be formatted.
    //
    pFormatInfo->SectorsPerCluster  = (U16)SectorsPerCluster;
    pFormatInfo->NumRootDirEntries  = (U16)NumRootDirEntries;
    pFormatInfo->NumSectorsReserved = NumSectorsReserved;
    pFormatInfo->NumClusters        = NumClusters;
    pFormatInfo->NumSectorsAT       = NumSectorsAT;
    pFormatInfo->FATType            = (U8)FATType;
  }
  if (pPartInfo != NULL) {
    //
    // Store the information about the partition location.
    //
    pPartInfo->NumSectors  = NumSectors - PartStartSector;
    pPartInfo->StartSector = PartStartSector;
  }
  return r;
}

/*********************************************************************
*
*       Public code (internal)
*
**********************************************************************
*/

/*********************************************************************
*
*       FS__SD_Format
*
*  Function description
*    Internal version of FS_FAT_FormatSD().
*    Formats the medium as specified in the SD card specification.
*
*  Parameters
*    pVolume   [IN]  Volume to format. NULL is not permitted.
*
*  Return value
*    ==0   OK, file system has been formatted
*    !=0   Error code indicating the failure reason
*/
int FS__SD_Format(FS_VOLUME * pVolume) {
  int                     r;
  int                     Status;
  FS_DEVICE             * pDevice;
  FS_PARTITION_INFO_MBR   PartInfo;
  FAT_FORMAT_INFO         FormatInfo;
  FS_DEV_INFO             DevInfo;

  r = FS_ERRCODE_STORAGE_NOT_PRESENT;         // Set to indicate an error.
  FS_MEMSET(&PartInfo,   0, sizeof(PartInfo));
  FS_MEMSET(&FormatInfo, 0, sizeof(FormatInfo));
  FS_MEMSET(&DevInfo,    0, sizeof(DevInfo));
  pDevice = &pVolume->Partition.Device;
  FS_LOCK_DRIVER(pDevice);
  Status = FS_LB_GetStatus(pDevice);
  if (Status != FS_MEDIA_NOT_PRESENT) {
    FS__UnmountNL(pVolume);
    r = FS_LB_InitMediumIfRequired(pDevice);
    if (r == 0) {
      //
      // Retrieve the information from card
      //
      r = FS_LB_GetDeviceInfo(pDevice, &DevInfo);
      if (r != 0) {
        r = FS_ERRCODE_STORAGE_NOT_READY;       // Error, device information can not be retrieved.
      } else {
        r = _CalcFormatInfo(&DevInfo, &FormatInfo, &PartInfo);
        if (r != 0) {
          //
          // Use the default format function if the SD format cannot be applied
          // because for example the storage is not large enough.
          //
          r = FS_FAT_Format(pVolume, NULL);
        } else {
          FS__CalcPartitionInfoMBR(&PartInfo, DevInfo.NumSectors);
          FS__CalcDeviceInfo(&DevInfo);
          //
          // Create the partition with 1 entry on the sector 0 of the storage medium.
          //
          r = FS__WriteMBR(pVolume, &PartInfo, 1);
          if (r == FS_ERRCODE_OK) {
            //
            // Get the actual number of sectors in the partition
            // and update the partition information.
            //
            if (PartInfo.StartSector != 0u) {
              if (DevInfo.NumSectors > PartInfo.NumSectors) {
                DevInfo.NumSectors = PartInfo.NumSectors;
              }
              pVolume->Partition.NumSectors  = PartInfo.NumSectors;
              pVolume->Partition.StartSector = PartInfo.StartSector;
            }
            r = FS_FAT_FormatVolume(pVolume, &DevInfo, &FormatInfo, 0);       // 0 means that the partition information does not have to be updated.
          }
        }
      }
    }
  } else {
    //
    // Umounting the file system when the format operation is not
    // performed does not make too much sense. However, we have to
    // do this for backward compatibility.
    //
    FS__UnmountForcedNL(pVolume);
  }
  FS_UNLOCK_DRIVER(pDevice);
  return r;
}

/*********************************************************************
*
*       FS__IsSDFormatted
*
*  Function description
*    Checks if the volume has been formatted acc. to SD specification.
*
*  Parameters
*    pVolume   [IN]  Volume to format. NULL not is permitted. Must be mounted.
*
*  Return value
*    ==0   Not formatted acc. to SD specification or an error occurred
*    !=0   Formatted acc. to SD specification
*/
int FS__IsSDFormatted(FS_VOLUME * pVolume) {
  int               Status;
  FS_DEVICE       * pDevice;
  FS_DEV_INFO       DevInfo;
  const SIZE_INFO * pSizeInfo;
  int               r;
  unsigned          i;
  int               IsSDFormatted;

  IsSDFormatted = 0;
  pDevice = &pVolume->Partition.Device;
  FS_LOCK_DRIVER(pDevice);
  Status = FS_LB_GetStatus(pDevice);
  if (Status != FS_MEDIA_NOT_PRESENT) {
    r = FS_LB_InitMediumIfRequired(pDevice);
    if (r == 0) {
      //
      // Retrieve the information from storage medium.
      //
      r = FS_LB_GetDeviceInfo(pDevice, &DevInfo);
      if (r == 0) {
        //
        // Get the format information.
        //
        pSizeInfo = _aSizeInfo;
        for (i = 0; i < SEGGER_COUNTOF(_aSizeInfo); i++) {
          pSizeInfo = &_aSizeInfo[i];
          if (pSizeInfo->NumSectors > DevInfo.NumSectors) {
            break;
          }
        }
        if (pSizeInfo->SectorsPerCluster == pVolume->FSInfo.FATInfo.SectorsPerCluster) {
          IsSDFormatted = 1;      // Volume is formatted acc. to SD specification.
        }
      }
    }
  }
  FS_UNLOCK_DRIVER(pDevice);
  return IsSDFormatted;
}

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_FAT_FormatSD
*
*  Function description
*    Formats the volume according to specification of SD Association.
*
*  Parameters
*    sVolumeName   Name of the volume to be formatted.
*
*  Return value
*    ==0   O.K., format successful.
*    !=0   Error code indicating the failure reason.
*
*  Additional information
*    The SD Association defines the layout of the information that
*    has to be stored to an SD, SDHC or SDXC card during the
*    FAT format operation to ensure the best read and write
*    performance by taking advantage of the physical structure of the
*    storage device. FS_FAT_FormatSD() implements this recommended
*    layout and it shall be used to format SD and MMC storage devices
*    but it can be used for other storage devices as well.
*    It typically reserves more space for the file system as
*    FS_Format() and as a consequence less space is available for
*    the application to store files and directories.
*
*    FS_FAT_FormatSD() performs the following steps:
*    * Writes partition entry into the MBR.
*    * Formats the storage device as FAT.
*    The function is available only if the file system
*    sources are compiled with the FS_SUPPORT_FAT option define
*    set to 1.
*/
int FS_FAT_FormatSD(const char * sVolumeName) {
  int          r;
  FS_VOLUME  * pVolume;

  FS_LOCK();
  FS_PROFILE_CALL_STRING(FS_EVTID_FORMATSD, sVolumeName);
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    r = FS__SD_Format(pVolume);
  } else {
    r = FS_ERRCODE_VOLUME_NOT_FOUND;        // Error, invalid volume specified.
  }
  FS_PROFILE_END_CALL_U32(FS_EVTID_FORMATSD, r);
  FS_UNLOCK();
  return r;
}

#endif

/*************************** End of file ****************************/
