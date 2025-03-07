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
File        : FS_FAT_Format.c
Purpose     : Implementation format routines
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#include "FS_FAT.h"
#include "FS_FAT_Int.h"

/*********************************************************************
*
*       Defines, fixed
*
**********************************************************************
*/
#define FAT_SIGNATURE               0xAA55u

#define PARTENTRY_OFF_TYPE          0x04u

#define PART_TYPE_FAT12             0x01
#define PART_TYPE_FAT16             0x04
#define PART_TYPE_FAT16_HUGE        0x06
#define PART_TYPE_FAT32             0x0B

#define NUM_DEFAULT_DIR_ENTRIES     0x100
#define MEDIA_TYPE                  0xF8

/*********************************************************************
*
*       Local data types
*
**********************************************************************
*/
typedef struct {
  U32 NumSectors;
  U8  SectorsPerCluster;
  U16 NumRootDirEntries;
} FORMAT_INFO;

typedef struct {
  U8  FATType;
  I32 MinClusters;
} FAT_TYPE_INFO;

/*********************************************************************
*
*       Static const data
*
**********************************************************************
*/
static const FAT_TYPE_INFO _aFATTypeInfo[] = {
//             FATType, MinClusters
    {FS_FAT_TYPE_FAT12, 0x00000000}
  , {FS_FAT_TYPE_FAT16, 0x00000FF5}
#if (FS_FAT_SUPPORT_FAT32)
  , {FS_FAT_TYPE_FAT32, 0x0000FFF5}
#endif
};

//
// Default volume label. Format will use this string as default label.
//
static const char _acVolumeLabel[] = "NO NAME    ";  // 11 characters

/*********************************************************************
*
*       _aFormatInfo
*
* Purpose
*   Table with format info
*
* Notes
*   (1) Why this table ?
*       It is not necessary to put information on how to format media of
*       a certain size into a table, but it sure is a lot easier and
*       also a lot more compact in terms of code size than to put this
*       into the source code and also proves to be the most flexible
*       method
*   (2) How is the table searched ?
*       Simple ... The first entry, number of sectors is compared.
*       The first entry with less more or equal sectors as available
*       on the medium (partition) is used
*   (3) Does this table work the same way on any medium ?
*       Yes. It is used by all format code for any medium.
*/
static const FORMAT_INFO _aFormatInfo[] = {
//    NumSectors, SectorsPerCluster, NumRootDirEntries
  {        256uL, 0x0001,            0x0020},     // <= 128kB
  {        512uL, 0x0001,            0x0040},     // <= 256kB
  {   0x000800uL, 0x0001,            0x0080},     // <=   1MB
  {   0x001000uL, 0x0001,            0x0100},     // <=   2MB
  {   0x004000uL, 0x0002,            0x0100},     // <=  16MB
  {   0x008000uL, 0x0002,            0x0100},     // <=  32MB
  {   0x040000uL, 0x0004,            0x0200},     // <= 128MB
  {   0x080000uL, 0x0008,            0x0200},     // <= 256MB
  {   0x100000uL, 0x0010,            0x0200},     // <= 512MB
#if FS_FAT_SUPPORT_FAT32
  {  0x1000000uL, 0x0008,                 0},     // <=   8GB
  {   33554432uL, 0x0010,                 0},     // <=  16GB
  {   67108864uL, 0x0020,                 0},     // <=  32GB
  { 0xFFFFFFFFuL, 0x0040,                 0}
#else
  {   0x1FFEA0uL, 0x0020,            0x0200},     // <=   1GB
  {   0x3FFD40uL, 0x0040,            0x0200},     // <=   2GB
  {   0xFFF500uL, 0x0080,            0x0200},     // <=   8GB
#endif // FS_FAT_SUPPORT_FAT32
};

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _UpdatePartTable
*
*  Function description
*    Updates the partition table. This is necessary because different FAT types
*    have different Ids in the partition table.
*    In other words: If a medium was FAT32 and is now formatted as FAT16 (or the other way round),
*    the partition type in the partition table needs to be changed.
*
*  Return value
*    ==0    OK, partition table updated
*    !=0    Error code indicating the failure reason
*/
static int _UpdatePartTable(FS_VOLUME * pVolume, U32 NumSectors, unsigned FATType, U8 * pBuffer) {
  unsigned Off;
  U8       CurrentPartType;
  unsigned PartType;
  int      r;

  r = FS_LB_ReadDevice(&pVolume->Partition.Device, SECTOR_INDEX_BPB, pBuffer, FS_SECTOR_TYPE_MAN);
  if (r != 0) {
    return FS_ERRCODE_READ_FAILURE;       // Error, could not read sector.
  }
  Off = MBR_OFF_PARTITION0 + PARTENTRY_OFF_TYPE;
  CurrentPartType = *(pBuffer + Off);
#if FS_SUPPORT_GPT
  if (CurrentPartType == GPT_OS_TYPE) {
    return 0;                             // Do not update the partition table if the storage device is partitioned as GPT.
  }
#endif // FS_SUPPORT_GPT
  switch (FATType) {
  case FS_FAT_TYPE_FAT12:
    PartType = PART_TYPE_FAT12;
    break;
  case FS_FAT_TYPE_FAT16:
    if (NumSectors < 65536uL) {
      PartType = PART_TYPE_FAT16;
    } else {
      PartType = PART_TYPE_FAT16_HUGE;
    }
    break;
  case FS_FAT_TYPE_FAT32:
    PartType = PART_TYPE_FAT32;
    break;
  default:
    PartType = 0;
    FS_DEBUG_ERROROUT((FS_MTYPE_FS, "_UpdatePartTable: Unknown FAT type %d.", FATType));
    r = FS_ERRCODE_INVALID_PARA;          // Error, invalid FAT type.
    break;
  }
  if (r == 0) {
    if ((unsigned)CurrentPartType != PartType) {
      *(pBuffer + Off) = (U8)PartType;
      r = FS_LB_WriteDevice(&pVolume->Partition.Device, SECTOR_INDEX_BPB, pBuffer, FS_SECTOR_TYPE_MAN, 1);
      if (r != 0) {
        r = FS_ERRCODE_WRITE_FAILURE;     // Error, could not write sector.
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _AutoFormat
*
*  Function description
*    FS internal function. Get information about the media from the
*    device driver. Based on that information, calculate parameters for
*    formatting that media and call the format routine.
*
*  Return value
*    ==0    OK, storage medium has been formatted.
*    !=0    Error code indicating the failure reason.
*/
static int _AutoFormat(FS_VOLUME * pVolume) {
  FS_DEV_INFO     DevInfo;
  int             r;
  int             i;
  FAT_FORMAT_INFO FormatInfo;

  FS_MEMSET(&DevInfo,    0, sizeof(FS_DEV_INFO));
  FS_MEMSET(&FormatInfo, 0, sizeof(FormatInfo));
  //
  // Check if there is a partition table.
  //
  r = FS__LocatePartition(pVolume);
  if (r != 0) {
    return r;                                 // Error, could not locate partition.
  }
  //
  // Get info about storage device.
  //
  (void)FS_LB_GetDeviceInfo(&pVolume->Partition.Device, &DevInfo);
  if (pVolume->Partition.StartSector != 0u) {
    DevInfo.NumSectors = pVolume->Partition.NumSectors;
  }
  //
  // Check if we have gotten the information.
  //
  if (DevInfo.NumSectors == 0u) {
    return FS_ERRCODE_STORAGE_NOT_READY;                  // Error, we could not get the required info or device is not ready.
  }
  //
  // Format media using calculated values.
  //
  for (i = 0; DevInfo.NumSectors > _aFormatInfo[i].NumSectors; i++) {
    ;
  }
  FormatInfo.SectorsPerCluster  = _aFormatInfo[i].SectorsPerCluster;
  FormatInfo.NumRootDirEntries  = _aFormatInfo[i].NumRootDirEntries;
  r = FS_FAT_FormatVolume(pVolume, &DevInfo, &FormatInfo, 1);    // 1 means that the partition information has to be updated.
  return r;
}

/*********************************************************************
*
*       _Format
*/
static int _Format(FS_VOLUME * pVolume, const FS_FORMAT_INFO * pFormatInfo) {
  int               r;
  int               Status;
  FS_DEV_INFO       DevInfo;
  FS_DEVICE       * pDevice;
  FAT_FORMAT_INFO   FormatInfo;

  FS_MEMSET(&DevInfo,    0, sizeof(DevInfo));
  FS_MEMSET(&FormatInfo, 0, sizeof(FormatInfo));
  pDevice = &pVolume->Partition.Device;
  Status = FS_LB_GetStatus(pDevice);
  if (Status == FS_MEDIA_NOT_PRESENT) {
    return FS_ERRCODE_STORAGE_NOT_PRESENT;          // Error, the storage medium is not present.
  }
  r = FS__LocatePartition(pVolume);                 // Check if there is a partition table.
  if (r != 0) {
    return r;                                       // Error, could not locate partition.
  }
  if (pFormatInfo->pDevInfo == NULL) {
    (void)FS_LB_GetDeviceInfo(pDevice, &DevInfo);   // Get info from device if required.
  } else {
    DevInfo = *pFormatInfo->pDevInfo;
  }
  //
  // If there is a partition table, then use this information to clip the size (NumSectors).
  //
  if (pVolume->Partition.StartSector != 0u) {
    if (DevInfo.NumSectors > pVolume->Partition.NumSectors) {
      DevInfo.NumSectors = pVolume->Partition.NumSectors;
    }
  }
  FormatInfo.SectorsPerCluster = pFormatInfo->SectorsPerCluster;
  FormatInfo.NumRootDirEntries = pFormatInfo->NumRootDirEntries;
  r = FS_FAT_FormatVolume(pVolume, &DevInfo, &FormatInfo, 1);
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
*       FS_FAT_FormatVolume
*
*  Function description
*    Format the storage medium as FAT using the specified parameters.
*
*  Return value
*    ==0    OK, storage medium has been formatted
*    !=0    Error code indicating the failure reason
*/
int FS_FAT_FormatVolume(FS_VOLUME * pVolume, const FS_DEV_INFO * pDevInfo, const FAT_FORMAT_INFO * pFormatInfo, int UpdatePartition) {
  int            r;
  int            Status;
  int            i;
  I32            NumClusters;
  U32            NumSectorsAT;
  unsigned       FATType;
  U32            NumSectorsRootDir;
  I32            NumDataSectors;
  unsigned       NumRootDirEntries;
  unsigned       NumRootDirEntriesProp;
  U32            NumReservedSectorsTemp;
  FS_PARTITION * pPart;
  FS_DEVICE    * pDevice;
  U8           * pBuffer;
  U32            BytesPerSector;
  U32            NumSectorsReserved;
  unsigned       NumHeads;
  unsigned       SectorsPerTrack;
  U32            NumSectors;
  unsigned       SectorsPerCluster;
  U32            NumSectorsCalc;

  pPart                 = &pVolume->Partition;
  pDevice               = &pPart->Device;
  NumSectorsRootDir     = 0;
  FATType               = pFormatInfo->FATType;
  NumRootDirEntries     = pFormatInfo->NumRootDirEntries;
  NumSectorsAT          = pFormatInfo->NumSectorsAT;
  NumClusters           = (I32)pFormatInfo->NumClusters;
  SectorsPerCluster     = pFormatInfo->SectorsPerCluster;
  NumSectorsReserved    = pFormatInfo->NumSectorsReserved;
  BytesPerSector        = pDevInfo->BytesPerSector;
  NumSectors            = pDevInfo->NumSectors;
  NumHeads              = pDevInfo->NumHeads;
  SectorsPerTrack       = pDevInfo->SectorsPerTrack;
  NumRootDirEntriesProp = NumRootDirEntries;
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_PARA
  NumRootDirEntriesProp &= 0xFFF0u;           // Make sure it is a multiple of 16.
#endif
  //
  // Is device ready ?
  //
  Status = FS_LB_GetStatus(pDevice);
  if (Status == FS_MEDIA_NOT_PRESENT) {
    return FS_ERRCODE_STORAGE_NOT_PRESENT;    // Error, the storage medium is not present.
  }
  //
  // Unmount the volume. Note that all handles should have been closed !
  //
  pVolume->MountType = 0;
  pBuffer = FS__AllocSectorBuffer();
  if (pBuffer == NULL) {
    return FS_ERRCODE_BUFFER_NOT_AVAILABLE;   // Error, no more sector buffers available.
  }
  //
  // Check if the a sector fits into the sector buffer.
  //
  if ((BytesPerSector > FS_Global.MaxSectorSize) || (BytesPerSector == 0u)) {
    FS_DEBUG_ERROROUT((FS_MTYPE_FS, "FS_FAT_FormatVolume: Invalid sector size: %d.", BytesPerSector));
    r = FS_ERRCODE_INVALID_PARA;              // Error, invalid sector size.
    goto Done;
  }
  NumReservedSectorsTemp = NumSectorsReserved;
  //
  // Set NumHeads and SectorPerTrack to default value, if necessary.
  //
  if (NumHeads == 0u) {
    NumHeads = 0xFF;
  }
  if (SectorsPerTrack == 0u) {
    SectorsPerTrack = 0x3F;
  }
  if (FATType == 0u) {
    //
    // Calculate the number of data sectors, number of clusters.
    // We calculate preliminary values first (Values may be slightly too big),
    // Use these to calculate NumSectorsAT and then compute the correct values in a second step.
    //
    for (i = (int)SEGGER_COUNTOF(_aFATTypeInfo) - 1; i >= 0; i--) {
      I32 MinClusters;
      U32 BitsPerSector;
      U32 BitsInFAT;

      MinClusters = _aFATTypeInfo[i].MinClusters;
      FATType     = _aFATTypeInfo[i].FATType;
      if ((NumReservedSectorsTemp != NumSectorsReserved) || (NumReservedSectorsTemp == 0u)) {
        NumReservedSectorsTemp = (FATType == FS_FAT_TYPE_FAT32) ? 32uL : 1uL;
      }
      if (NumRootDirEntriesProp == 0u) {
        if (FATType != FS_FAT_TYPE_FAT32) {
          FS_DEBUG_WARN((FS_MTYPE_FS, "FS_FAT_FormatVolume: NumRootDirEntries (%d) is invalid. Defaulting to %d.", NumRootDirEntriesProp, NUM_DEFAULT_DIR_ENTRIES));
          NumRootDirEntriesProp = NUM_DEFAULT_DIR_ENTRIES;
        }
      }
      NumRootDirEntries  = (FATType == FS_FAT_TYPE_FAT32) ?  0u : NumRootDirEntriesProp;
      NumSectorsRootDir  = FS__DivideU32Up((U32)NumRootDirEntries * 32uL, BytesPerSector);
      NumDataSectors     = ((I32)NumSectors - (I32)NumReservedSectorsTemp) - (I32)NumSectorsRootDir;
      NumClusters        = NumDataSectors / (I32)SectorsPerCluster;
      BitsPerSector      = BytesPerSector << 3;
      BitsInFAT          = ((U32)NumClusters + FAT_FIRST_CLUSTER) * FATType;
      NumSectorsAT       = FS__DivideU32Up(BitsInFAT, BitsPerSector);
      NumDataSectors    -= (I32)FAT_NUM_ALLOC_TABLES * (I32)NumSectorsAT;       // Calculate the precise number of avail. sectors
      NumClusters        = NumDataSectors / (I32)SectorsPerCluster;             // Calculate the precise number of avail. clusters
      BitsInFAT          = ((U32)NumClusters + FAT_FIRST_CLUSTER) * FATType;
      NumSectorsAT       = FS__DivideU32Up(BitsInFAT, BitsPerSector);
      //
      // Now check if the max. number of clusters is exceeded.
      //
      if (i == 0) {
        if (NumClusters <= 4084) {
          break;
        }
        NumClusters = 4084;
      } else {
        if (NumClusters >= MinClusters) {
          break;                                                            // O.K., this FATType can be used
        }
      }
    }
    NumSectorsReserved = NumReservedSectorsTemp;
    //
    // Now that the type of FAT has been determined, we can perform add. checks.
    //
    if (NumClusters <= 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_FS, "FS_FAT_FormatVolume: The number of sectors on the medium is too small."));
      r = FS_ERRCODE_STORAGE_TOO_SMALL;
      goto Done;
    }
  } else {
    //
    // All the format information is provided by the caller.
    // Calculate only the number of sectors to be reserved for the root directory on FAT12/16.
    //
    if (FATType != FS_FAT_TYPE_FAT32) {
      NumSectorsRootDir = FS__DivideU32Up((U32)NumRootDirEntries * 32uL, BytesPerSector);
    }
  }
  NumSectorsCalc = NumSectorsReserved + NumSectorsAT * FAT_NUM_ALLOC_TABLES + NumSectorsRootDir + (U32)NumClusters * SectorsPerCluster;
  if (NumSectorsCalc > NumSectors) {
    FS_DEBUG_ERROROUT((FS_MTYPE_FS, "FS_FAT_FormatVolume: Invalid format parameters."));
    r = FS_ERRCODE_INVALID_PARA;
    goto Done;
  }
  NumSectors = NumSectorsCalc;
  //
  // Invalidate the old BPB sector.
  //
  FS_MEMSET(pBuffer, 0x00, BytesPerSector);
  r = FS_LB_WritePart(pPart, 0, SEGGER_PTR2PTR(void, pBuffer), FS_SECTOR_TYPE_MAN, 1);                                                // MISRA deviation D:100[e]
  if (r != 0) {
    r = FS_ERRCODE_WRITE_FAILURE;       // Error, could not invalidate BPB.
    goto Done;
  }
  //
  // Initialize FAT 1 & 2. Start by filling all FAT sectors except the first one with 0.
  //
  FS_MEMSET(pBuffer, 0x00, BytesPerSector);
  r = FS_LB_WriteMultiplePart(pPart, NumSectorsReserved, FAT_NUM_ALLOC_TABLES * NumSectorsAT, pBuffer, FS_SECTOR_TYPE_MAN, 1);
  if (r != 0) {
    r = FS_ERRCODE_WRITE_FAILURE;       // Error, could not initialize allocation table.
    goto Done;
  }
  //
  // Initialize the first FAT sector.
  //
  pBuffer[0] = (U8)MEDIA_TYPE;
  pBuffer[1] = (U8)0xFF;
  pBuffer[2] = (U8)0xFF;
  if (FATType != FS_FAT_TYPE_FAT12) {
    pBuffer[3] = (U8)0xFF;
  }
#if FS_FAT_SUPPORT_FAT32
  if (FATType == FS_FAT_TYPE_FAT32) {
    FS_StoreU32LE(&pBuffer[4], 0x0FFFFFFFuL);
    FS_StoreU32LE(&pBuffer[8], 0x0FFFFFFFuL);
  }
#endif // FS_FAT_SUPPORT_FAT32
  for (i = 0; (unsigned)i < FAT_NUM_ALLOC_TABLES; i++) {
    r = FS_LB_WritePart(pPart, NumSectorsReserved + (U32)i * NumSectorsAT, SEGGER_PTR2PTR(void, pBuffer), FS_SECTOR_TYPE_MAN, 1);     // MISRA deviation D:100[e]
    if (r != 0) {
      r = FS_ERRCODE_WRITE_FAILURE;     // Error, could not initialize allocation table.
      goto Done;
    }
  }
  //
  // Initialize root directory area.
  //
  FS_MEMSET(pBuffer, 0x00, BytesPerSector);
  if (NumRootDirEntries != 0u) {
    //
    // FAT12/FAT16
    //
    r = FS_LB_WriteMultiplePart(pPart, NumSectorsReserved + FAT_NUM_ALLOC_TABLES * NumSectorsAT, NumSectorsRootDir, pBuffer, FS_SECTOR_TYPE_DIR, 1);
    if (r != 0) {
      r = FS_ERRCODE_WRITE_FAILURE;     // Error, could not initialize root directory.
      goto Done;
    }
  }
#if (FS_FAT_SUPPORT_FAT32)
  else {
    //
    // FAT32
    //
    NumSectorsRootDir = SectorsPerCluster;
    r = FS_LB_WriteMultiplePart(pPart, NumSectorsReserved + FAT_NUM_ALLOC_TABLES * NumSectorsAT, NumSectorsRootDir, pBuffer, FS_SECTOR_TYPE_DIR, 1);
    if (r != 0) {
      r = FS_ERRCODE_WRITE_FAILURE;     // Error, could not initialize root directory.
      goto Done;
    }
  }
#endif // FS_FAT_SUPPORT_FAT32
#if FS_FAT_SUPPORT_FAT32
  if (FATType == FS_FAT_TYPE_FAT32) {
    //
    // Initialize FSInfo sector.
    //
    FS_MEMSET(pBuffer, 0x00, BytesPerSector);
    FS_StoreU32LE(&pBuffer[FSINFO_OFF_SIGNATURE_1],       0x41615252uL);            // LeadSig = 0x41615252
    FS_StoreU32LE(&pBuffer[FSINFO_OFF_SIGNATURE_2],       0x61417272uL);            // StructSig = 0x61417272
    FS_StoreU32LE(&pBuffer[FSINFO_OFF_FREE_CLUSTERS],     (U32)NumClusters - 1uL);  // Invalidate last known free cluster count. One cluster is allocated for the root directory.
    FS_StoreU32LE(&pBuffer[FSINFO_OFF_NEXT_FREE_CLUSTER], 0x00000003uL);            // Give hint for free cluster search
    FS_StoreU16LE(&pBuffer[510], FAT_SIGNATURE);                                    // Signature = 0xAA55
    r = FS_LB_WritePart(pPart, SECTOR_INDEX_FSINFO, SEGGER_PTR2PTR(void, pBuffer), FS_SECTOR_TYPE_MAN, 1);                            // MISRA deviation D:100[e]
    if (r == 0) {
      //
      // Write Backup of FSInfo sector
      //
      r = FS_LB_WritePart(pPart, SECTOR_INDEX_FSINFO_BACKUP, SEGGER_PTR2PTR(void, pBuffer), FS_SECTOR_TYPE_MAN, 1);                   // MISRA deviation D:100[e]
    }
    if (r != 0) {
      r = FS_ERRCODE_WRITE_FAILURE;     // Error, could not store FSInfo sector.
      goto Done;
    }
  }
#endif // FS_FAT_SUPPORT_FAT32
  //
  // Prepare buffer. Offset 0 - 35 is same for FAT12/FAT16 and FAT32.
  //
  FS_MEMSET(pBuffer, 0x00, BytesPerSector);                     // MS specified. Most frequently used format: 0xEB 0x3C 0x90.
  pBuffer[0]  = 0xE9;                                           // jmpBoot
  (void)FS_MEMCPY((char*)&pBuffer[3], "MSWIN4.1", 8);           // OEMName = 'MSWIN4.1'
  FS_StoreU16LE(&pBuffer[11], BytesPerSector);                  // Sector size
  pBuffer[13] = (U8)SectorsPerCluster;                          // Sectors in each allocation unit
  FS_StoreU16LE(&pBuffer[14], NumSectorsReserved);
  pBuffer[16] = FAT_NUM_ALLOC_TABLES;                           // Number of allocation tables
  FS_StoreU16LE(&pBuffer[17], NumRootDirEntries);
  //
  // Number of total sectors (512 byte units) of the media.
  // This is independent of FAT type (FAT12/FAT16/FAT32).
  //
  if (NumSectors < 0x10000uL) {
    FS_StoreU16LE(&pBuffer[BPB_OFF_NUMSECTORS_16BIT], NumSectors);    // Total number of sectors as 16-bit value
  } else {
    FS_StoreU32LE(&pBuffer[BPB_OFF_NUMSECTORS_32BIT], NumSectors);    // Total number of sectors as 32-bit value
  }
  pBuffer[21]  = MEDIA_TYPE;                                    // Media type
  FS_StoreU16LE(&pBuffer[24], SectorsPerTrack);                 // Number of sectors per track
  FS_StoreU16LE(&pBuffer[26], NumHeads);                        // Number of heads
  FS_StoreU32LE(&pBuffer[28], pVolume->Partition.StartSector);  // Number of hidden sectors
  //
  // Offset 36 and above have different meanings for FAT12/FAT16 and FAT32
  //
  if (FATType != FS_FAT_TYPE_FAT32) {
    pBuffer[36] = 0x80;                                         // Physical drive number
    pBuffer[38] = 0x29;                                         // Extended Boot Signature
    FS_StoreU32LE(&pBuffer[39], 0x01234567uL);                  // 32 Bit Volume ID
    FS_StoreU16LE(&pBuffer[22], NumSectorsAT);                  // FATSz16
    (void)FS_MEMCPY((char*)&pBuffer[43], _acVolumeLabel, 11);   // VolLab = ' '
    if (FATType == FS_FAT_TYPE_FAT12) {
      (void)FS_MEMCPY((char*)&pBuffer[54], "FAT12   ", 8);
    } else {
      (void)FS_MEMCPY((char*)&pBuffer[54], "FAT16   ", 8);
    }
  }
#if FS_FAT_SUPPORT_FAT32
  else {
    //
    // FAT32
    //
    pBuffer[64] = 0x80;                                         // Physical drive number
    pBuffer[66] = 0x29;                                         // Extended Boot Signature
    FS_StoreU32LE(&pBuffer[36], NumSectorsAT);                  // FATSize32
    FS_StoreU32LE(&pBuffer[44], 2);                             // RootClus
    FS_StoreU16LE(&pBuffer[48], 1);                             // FSInfo
    pBuffer[50]  = 0x06;                                        // BkBootSec = 0x0006
    FS_StoreU32LE(&pBuffer[67], 0x01234567uL);                  // 32 Bit Volume ID
    (void)FS_MEMCPY((char*)&pBuffer[71], _acVolumeLabel, 11);   // VolLab = ' '
    (void)FS_MEMCPY((char*)&pBuffer[82], "FAT32   ", 8);        // FilSysType = 'FAT32'
  }
#endif // FS_FAT_SUPPORT_FAT32
  FS_StoreU16LE(&pBuffer[510], FAT_SIGNATURE);                  // Signature = 0xAA55
  //
  // Write BPB to media.
  //
  r = FS_LB_WritePart(pPart, SECTOR_INDEX_BPB, SEGGER_PTR2PTR(void, pBuffer), FS_SECTOR_TYPE_MAN, 1);                                 // MISRA deviation D:100[e]
  if (r != 0) {
    r = FS_ERRCODE_WRITE_FAILURE;               // Error, could not write BPB to storage.
    goto Done;
  }
  if (FATType == FS_FAT_TYPE_FAT32) {
    //
    // Write backup BPB.
    //
    r = FS_LB_WritePart(pPart, SECTOR_INDEX_BPB_BACKUP, SEGGER_PTR2PTR(void, pBuffer), FS_SECTOR_TYPE_MAN, 1);                        // MISRA deviation D:100[e]
    if (r != 0) {
      r = FS_ERRCODE_WRITE_FAILURE;             // Error, could not write backup BPB to storage.
      goto Done;
    }
  }
  r = FS_ERRCODE_OK;
  //
  // Update partition table if necessary
  //
  if (pVolume->Partition.StartSector != 0u) {
    if (UpdatePartition != 0) {
      r = _UpdatePartTable(pVolume, NumSectors, FATType, pBuffer);
    }
  }
  //
  // Inform the driver layer about the unused sectors.
  //
#if FS_SUPPORT_FREE_SECTOR
  if (pVolume->FreeSector != 0u) {
    U32 FirstDataSector;
    U32 FirstSectorAfterAT;

    FirstSectorAfterAT = NumSectorsReserved + FAT_NUM_ALLOC_TABLES * NumSectorsAT;
    FirstDataSector    = FirstSectorAfterAT + NumSectorsRootDir;                       // Add number of sectors in the root directory.
    (void)FS_LB_FreeSectorsPart(pPart, FirstDataSector, NumSectors - FirstDataSector);
  }
#endif // FS_SUPPORT_FREE_SECTOR
Done:
  FS__FreeSectorBuffer(pBuffer);
  return r;
}

/*********************************************************************
*
*       FS_FAT_Format
*
*  Function description
*    This functions formats the volume as FAT.
*
*  Return value
*    ==0    OK, volume formatted
*    !=0    Error code indicating the failure reason
*/
int FS_FAT_Format(FS_VOLUME  * pVolume, const FS_FORMAT_INFO * pFormatInfo) {
  int r;

  if (pFormatInfo != NULL) {
    r = _Format(pVolume, pFormatInfo);
  } else {
    r = _AutoFormat(pVolume);
  }
  return r;
}

/*************************** End of file ****************************/
