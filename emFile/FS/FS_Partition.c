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
File        : FS_Partition.c
Purpose     : Volume partition tools
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
*       Defines, fixed
*
**********************************************************************
*/

/*********************************************************************
*
*       ASSERT_PART_INDEX_IS_IN_RANGE
*/
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL)
  #define ASSERT_PART_INDEX_IS_IN_RANGE(PartIndex)                                           \
    if ((PartIndex) >= (unsigned)FS_MAX_NUM_PARTITIONS_MBR) {                                \
      FS_DEBUG_ERROROUT((FS_MTYPE_API, "PART_API: Invalid partition index %d.", PartIndex)); \
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);                                                   \
    }
#else
  #define ASSERT_PART_INDEX_IS_IN_RANGE(PartIndex)
#endif

#if FS_SUPPORT_GPT

/*********************************************************************
*
*       ASSERT_PART_INDEX_GPT_IS_IN_RANGE
*/
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL)
  #define ASSERT_PART_INDEX_GPT_IS_IN_RANGE(PartIndex)                                       \
    if ((PartIndex) >= (unsigned)FS_MAX_NUM_PARTITIONS_GPT) {                                \
      FS_DEBUG_ERROROUT((FS_MTYPE_API, "PART_API: Invalid partition index %d.", PartIndex)); \
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);                                                   \
    }
#else
  #define ASSERT_PART_INDEX_GPT_IS_IN_RANGE(PartIndex)
#endif

#endif // FS_SUPPORT_GPT

/*********************************************************************
*
*       Local data types
*
**********************************************************************
*/
typedef struct {
  U32 NumSectors;
  U8  NumHeads;
  U8  SectorsPerTrack;
} CHS_INFO;

/*********************************************************************
*
*       Static const data
*
**********************************************************************
*/
static const CHS_INFO _aCHSInfo[] = {
  {0x0000FFFuL,   2,   16},  // Up to     2 MBytes
  {0x0007FFFuL,   2,   32},  // Up to    16 MBytes
  {0x000FFFFuL,   4,   32},  // Up to    32 MBytes
  {0x003FFFFuL,   8,   32},  // Up to   128 MBytes
  {0x007FFFFuL,  16,   32},  // Up to   256 MBytes
  {0x00FBFFFuL,  16,   63},  // Up to   504 MBytes
  {0x01F7FFFuL,  32,   63},  // Up to  1008 MBytes
  {0x03EFFFFuL,  64,   63},  // Up to  2016 MBytes
  {0x07DFFFFuL, 128,   63},  // Up to  4032 MBytes
  {0x07DFFFFuL, 255,   63},  // Up to 32768 MBytes
};

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _LoadNumSectors
*
*  Function description
*    Returns the number of sectors of the specified partition.
*
*  Parameters
*    PartIndex    Partition index. Valid range is 0..3.
*    pData        [IN] MBR sector data.
*
*  Return value
*    Number of sectors in the partition.
*/
static U32 _LoadNumSectors(unsigned PartIndex, const U8 * pData) {
  unsigned Off;
  U32      NumSectors;

  ASSERT_PART_INDEX_IS_IN_RANGE(PartIndex);
  Off  = MBR_OFF_PARTITION0 + (PartIndex << PART_ENTRY_SIZE_SHIFT);
  Off += PART_ENTRY_OFF_NUM_SECTORS;
  NumSectors = FS_LoadU32LE(&pData[Off]);
  return NumSectors;
}

/*********************************************************************
*
*       _LoadStartSector
*
*  Function description
*    Returns the index of the start sector of the specified partition.
*
*  Parameters
*    PartIndex    Partition index. Valid range is 0..3.
*    pData        [IN] MBR sector data.
*
*  Return value
*    Index of the first sector in the partition.
*/
static U32 _LoadStartSector(unsigned PartIndex, const U8 * pData) {
  unsigned Off;
  U32      SectorIndex;

  ASSERT_PART_INDEX_IS_IN_RANGE(PartIndex);
  Off  = MBR_OFF_PARTITION0 + (PartIndex << PART_ENTRY_SIZE_SHIFT);
  Off += PART_ENTRY_OFF_START_SECTOR;
  SectorIndex = FS_LoadU32LE(&pData[Off]);
  return SectorIndex;
}

/*********************************************************************
*
*       _HasSignature
*
*  Function description
*    Verifies if the MBR signature is present.
*
*  Parameters
*    pData      [IN] Sector data.
*
*  Return value
*    ==0      MBR signature is not valid.
*    !=0      MBR signature is valid.
*/
static int _HasSignature(const U8 * pData) {
  U16 Data;

  Data = FS_LoadU16LE(pData + MBR_OFF_SIGNATURE);
  if (Data == 0xAA55u) {
    return 1;
  }
  return 0;
}

/*********************************************************************
*
*       _IsBPB
*
*  Function description
*    Checks if the specified buffer stores a Boot Parameter Block (BPB).
*
*  Parameters
*    pData      [IN] Sector data.
*
*  Return value
*    ==0      Not a valid BPB.
*    !=0      This is a valid BPB.
*
*  Additional information
*    This is indicated by an unconditional x86 jmp instruction stored at the beginning of the buffer.
*/
static int _IsBPB(const U8 * pData) {
  //
  // Check for the 1-byte relative jump with opcode 0xe9
  //
  if (pData[0] == 0xE9u) {
    return 1;
  }
  //
  // Check for the 2-byte relative jump with opcode 0xeb
  //
  if ((pData[0] == 0xEBu) && (pData[2] == 0x90u)) {
    return 1;
  }
  return 0;
}

/*********************************************************************
*
*       _WriteMBR
*
*  Function description
*    Writes the Master Boot Record to the first sector (sector index 0)
*    of the specified storage device.
*
*  Parameters
*    pVolume          [IN] Volume instance.
*    pPartInfo        [IN] Partition list.
*    NumPartitions    Number of partitions to create.
*    pBuffer          Sector buffer.
*    BytesPerSector   Number of bytes in a logical sector.
*
*  Return value
*    ==0    OK, MBR written successfully.
*    !=0    Error code indicating the failure reason.
*/
static int _WriteMBR(FS_VOLUME * pVolume, const FS_PARTITION_INFO_MBR * pPartInfo, int NumPartitions, U8 * pBuffer, unsigned BytesPerSector) {
  int         iPart;
  int         r;
  U8        * p;
  FS_DEVICE * pDevice;

  pDevice = &pVolume->Partition.Device;
  FS_MEMSET(pBuffer, 0, BytesPerSector);
  //
  // Store the partition entries.
  //
  for (iPart = 0; iPart < NumPartitions; ++iPart) {
    FS__StorePartitionInfoMBR((U8)iPart, pPartInfo, pBuffer);
    ++pPartInfo;
  }
  //
  // Store the signature. If the number of partitions is 0 the MBR is not created and the signature is not needed.
  //
  if (NumPartitions != 0) {
    p = pBuffer + MBR_OFF_SIGNATURE;
    FS_StoreU16LE(p, MBR_SIGNATURE);
  }
  //
  // Write the MBR sector to storage device.
  //
  r = FS_LB_WriteDevice(pDevice, MBR_SECTOR_INDEX, pBuffer, FS_SECTOR_TYPE_MAN, 0);
  if (r != 0) {
    r = FS_ERRCODE_WRITE_FAILURE;
  }
  return r;
}

#if FS_SUPPORT_GPT

/*********************************************************************
*
*       _ld
*
*  Function description
*    Calculates the power of two exponent of the specified value.
*
*  Parameters
*    Value    Power of two value.
*
*  Return value
*    Power of two exponent.
*/
static unsigned _ld(U32 Value) {
  unsigned i;

  for (i = 0; i < 16u; i++) {
    if ((1uL << i) == Value) {
      break;
    }
  }
  return i;
}

/*********************************************************************
*
*       _IsProtectiveMBR
*
*  Function description
*    Checks if the sector data stores a protective MBR.
*
*  Parameters
*    pData      [IN] Sector data.
*
*  Return value
*    !=0    This is a protective MBR.
*    ==0    This is not a protective MBR.
*/
static int _IsProtectiveMBR(const U8 * pData) {
  int                   r;
  unsigned              iPart;
  FS_PARTITION_INFO_MBR PartInfo;

  r = 0;            // Set to indicate that this is not a protective MBR.
  //
  // Load information about the first MBR partition from read buffer.
  // According to UEFI specification a protective MBR has a partition
  // entry with the OS type set to 0xEE.
  //
  for (iPart = 0; iPart < (unsigned)FS_MAX_NUM_PARTITIONS_MBR; ++iPart) {
    FS_MEMSET(&PartInfo, 0, sizeof(PartInfo));
    FS__LoadPartitionInfoMBR(iPart, &PartInfo, pData);
    if (   (PartInfo.Type        == GPT_OS_TYPE)
        && (PartInfo.StartSector == GPT_HEADER_MAIN_SECTOR)) {
      r = 1;        // OK, this is a protective MBR.
      break;
    }
  }
  return r;
}

/*********************************************************************
*
*       _CheckGPTHeader
*
*  Function description
*    Checks if the data in the GPT header is valid.
*
*  Parameters
*    pData            [IN] Sector data.
*    BytesPerSector   Number of bytes in a logical sector.
*    SectorIndex      Index of the logical sector that stores the header.
*    IsBackup         Set to 1 if the backup GPT is checked.
*
*  Return value
*    ==0    OK, the GPT header is valid.
*    !=0    Error, the GPT header is not valid.
*
*  Additional information
*    According the UEFI specification the following tests have
*    to be performed:
*    - Check the signature.
*    - Check the CRC of the GPT header.
*    - Check that the MyLBA field stores the index of the
*      logical sector that stores the GPT header.
*    In addition, this function checks if the size of the GPT
*    header (the value stored in the HeaderSize filed)
*    is greater than 92 and smaller than the size of the
*    logical sector.
*/
static int _CheckGPTHeader(U8 * pData, unsigned BytesPerSector, U32 SectorIndex, int IsBackup) {
  U64 Signature;
  U32 crcRead;
  U32 crcCalc;
  U32 SizeOfHeader;
  U64 SectorIndexMain;
  U32 NumEntries;
  U32 Revision;
  U64 SectorIndexFirstEntry;
  U64 SectorIndexFirstFS;
  U64 SectorIndexLastFS;
  U64 SectorIndexSelf;
  U32 SizeOfEntry;

  Signature = FS_LoadU64LE(pData + GPT_HEADER_OFF_SIGNATURE);
  if (Signature != GPT_HEADER_SIGNATURE) {
    FS_DEBUG_WARN((FS_MTYPE_DRIVER, "PART_API: _CheckGPTHeader: Invalid signature."));
    return 1;                             // Error, invalid signature.
  }
  Revision = FS_LoadU32LE(pData + GPT_HEADER_OFF_REVISION);
  if (Revision != GPT_HEADER_REVISION) {
    FS_DEBUG_WARN((FS_MTYPE_DRIVER, "PART_API: _CheckGPTHeader: Invalid revision (0x%08X).", Revision));
    return 1;                             // Error, invalid signature.
  }
  SizeOfHeader = FS_LoadU32LE(pData + GPT_HEADER_OFF_SIZE);
  if (   (SizeOfHeader < GPT_HEADER_MIN_SIZE)
      || (SizeOfHeader > BytesPerSector)) {
    FS_DEBUG_WARN((FS_MTYPE_DRIVER, "PART_API: _CheckGPTHeader: Invalid header size (%u bytes).", SizeOfHeader));
    return 1;                             // Error, invalid header size.
  }
  if (IsBackup == 0) {
    SectorIndexMain = FS_LoadU64LE(pData + GPT_HEADER_OFF_MY_SECTOR);
    SectorIndexSelf = SectorIndexMain;
  } else {
    SectorIndexMain = FS_LoadU64LE(pData + GPT_HEADER_OFF_BACKUP_SECTOR);
    SectorIndexSelf = FS_LoadU64LE(pData + GPT_HEADER_OFF_MY_SECTOR);
  }
  if (SectorIndexMain != GPT_HEADER_MAIN_SECTOR) {
    FS_DEBUG_WARN((FS_MTYPE_DRIVER, "PART_API: _CheckGPTHeader: Invalid main sector (%u).", (U32)SectorIndexMain));
    return 1;                             // Error, invalid sector index.
  }
  if (SectorIndexSelf != SectorIndex) {
    FS_DEBUG_WARN((FS_MTYPE_DRIVER, "PART_API: _CheckGPTHeader: Invalid main self (%u <> %u).", (U32)SectorIndexSelf, (U32)SectorIndex));
    return 1;                             // Error, invalid sector index.
  }
  SectorIndexFirstEntry = FS_LoadU64LE(pData + GPT_HEADER_OFF_FIRST_ENTRY_SECTOR);
  if (SectorIndexFirstEntry <= SectorIndexMain) {
    FS_DEBUG_WARN((FS_MTYPE_DRIVER, "PART_API: _CheckGPTHeader: Invalid first entry sector (%u).", (U32)SectorIndexFirstEntry));
    return 1;                             // Error, invalid sector index.
  }
  SectorIndexFirstFS = FS_LoadU64LE(pData + GPT_HEADER_OFF_FIRST_FS_SECTOR);
  SectorIndexLastFS  = FS_LoadU64LE(pData + GPT_HEADER_OFF_LAST_FS_SECTOR);
  if (SectorIndexLastFS < SectorIndexFirstFS) {
    FS_DEBUG_WARN((FS_MTYPE_DRIVER, "PART_API: _CheckGPTHeader: Invalid last FS sector (%u < %u).", (U32)SectorIndexLastFS, (U32)SectorIndexFirstFS));
    return 1;                             // Error, invalid sector index.
  }
  if (IsBackup == 0) {
    if (SectorIndexFirstFS <= SectorIndexFirstEntry) {
      FS_DEBUG_WARN((FS_MTYPE_DRIVER, "PART_API: _CheckGPTHeader: Invalid first FS sector (%u <= %u).", (U32)SectorIndexFirstFS, (U32)SectorIndexFirstEntry));
      return 1;                             // Error, invalid sector index.
    }
  } else {
    if (SectorIndexLastFS >= SectorIndexFirstEntry) {
      FS_DEBUG_WARN((FS_MTYPE_DRIVER, "PART_API: _CheckGPTHeader: Invalid first FS sector (%u => %u).", (U32)SectorIndexLastFS, (U32)SectorIndexFirstEntry));
      return 1;                             // Error, invalid sector index.
    }
  }
  NumEntries = FS_LoadU32LE(pData + GPT_HEADER_OFF_NUM_ENTRIES);
  if (NumEntries > (unsigned)FS_MAX_NUM_PARTITIONS_GPT) {
    FS_DEBUG_WARN((FS_MTYPE_DRIVER, "PART_API: _CheckGPTHeader: Invalid number of entries (%u).", NumEntries));
    return 1;
  }
  SizeOfEntry = FS_LoadU32LE(pData + GPT_HEADER_OFF_SIZE_OF_ENTRY);
  if (   (SizeOfEntry == 0u)
      || (SizeOfEntry > BytesPerSector)) {
    FS_DEBUG_WARN((FS_MTYPE_DRIVER, "PART_API: _CheckGPTHeader: Invalid entry size (%u bytes).", SizeOfEntry));
    return 1;
  }
  //
  // Verify the CRC.
  //
  crcRead = FS_LoadU32LE(pData + GPT_HEADER_OFF_CRC);
  FS_StoreU32LE(pData + GPT_HEADER_OFF_CRC, 0u);
  crcCalc  = FS_CRC32_Calc(pData, SizeOfHeader, GPT_CRC_INIT);
  crcCalc ^= GPT_CRC_INIT;
  if (crcCalc != crcRead) {
    FS_DEBUG_WARN((FS_MTYPE_DRIVER, "PART_API: _CheckGPTHeader: Invalid header CRC (crcRead: 0x%08X, crcCalc: 0x%08X).", crcCalc, crcRead));
    return 1;                             // Error, invalid CRC.
  }
  return 0;                               // OK, the GPT header is valid.
}

/*********************************************************************
*
*       _StorePartitionInfoGPT
*
*  Function description
*    Writes information about a GPT partition to a sector buffer.
*
*  Parameters
*    PartIndex            Index of the partition to store.
*    pPartInfo            [IN] Information about the partition.
*    pData                [IN] Sector data that stores the partition entry.
*    ldEntriesPerSector   Number of partition entries in a sector as power of 2 exponent.
*    ldSizeOfEntry        Size of a partition entry in bytes as power of 2 exponent.
*
*  Return value
*    ==0    OK, information loaded successfully.
*    !=0    An error occurred.
*/
static int _StorePartitionInfoGPT(unsigned PartIndex, const FS_PARTITION_INFO_GPT * pPartInfo, U8 * pData, unsigned ldEntriesPerSector, unsigned ldSizeOfEntry) {
  unsigned    Off;
  U16       * pUnicode;
  int         r;
  FS_WCHAR    UnicodeChar;
  const U8  * pUTF8;
  unsigned    NumBytes;
  unsigned    NumBytesRead;
  unsigned    NumCharsUnicode;
  unsigned    i;
  U64         StartSector;
  U64         EndSector;
  U64         NumSectors;

  ASSERT_PART_INDEX_GPT_IS_IN_RANGE(PartIndex);
  r = 0;            // Set to indicate success.
  //
  // Calculate the position of the partition entry in the sector.
  //
  Off     = PartIndex & ((1uL << ldEntriesPerSector) - 1u);
  Off   <<= ldSizeOfEntry;
  pData  += Off;
  StartSector = pPartInfo->StartSector;
  NumSectors  = pPartInfo->NumSectors;
  EndSector   = StartSector + NumSectors - 1u;
  FS_StoreU64LE(pData + GPT_ENTRY_OFF_START_SECTOR, StartSector);
  FS_StoreU64LE(pData + GPT_ENTRY_OFF_END_SECTOR, EndSector);
  FS_StoreU64LE(pData + GPT_ENTRY_OFF_ATTR, pPartInfo->Attributes);
  FS_MEMCPY(pData + GPT_ENTRY_OFF_PART_TYPE, pPartInfo->abType, FS_NUM_BYTES_GUID);
  FS_MEMCPY(pData + GPT_ENTRY_OFF_PART_ID,   pPartInfo->abId,   FS_NUM_BYTES_GUID);
  //
  // Encode the partition name as UTF-16.
  //
  pUnicode        = SEGGER_PTR2PTR(U16, pData + GPT_ENTRY_OFF_NAME);
  pUTF8           = SEGGER_CONSTPTR2PTR(const U8, pPartInfo->acName);
  NumBytes        = FS_STRLEN(pPartInfo->acName);
  NumCharsUnicode = (GPT_ENTRY_SIZE_OF_PART_NAME / 2u) - 1u;        // /2 because each Unicode character is stored as 2 bytes and -1 to reserve space for the 0-terminator.
  for (i = 0; i < NumCharsUnicode; ++i) {
    NumBytesRead = 0;
    UnicodeChar = FS_UNICODE_DecodeCharUTF8(pUTF8, NumBytes, &NumBytesRead);
    if (UnicodeChar == FS_WCHAR_INVALID) {
      FS_DEBUG_ERROROUT((FS_MTYPE_API, "PART_API: _StorePartitionInfoGPT: Invalid partition name (UTF8: 0x%02X).", *pUTF8));
      r = FS_ERRCODE_INVALID_CHAR;
      break;                                                        // Error, could not encode partition name.
    }
    FS_StoreU16LE(SEGGER_PTR2PTR(U8, pUnicode), UnicodeChar);
    NumBytes -= NumBytesRead;
    pUTF8    += NumBytesRead;
    ++pUnicode;
    if (NumBytes == 0u) {
      break;                                                        // End of partition name reached.
    }
  }
  *pUnicode = 0;                                                    // Add the 0-terminator.
  return r;
}

/*********************************************************************
*
*       _LoadPartitionInfoGPT
*
*  Function description
*    Reads information about a GPT partition from a sector buffer.
*
*  Parameters
*    PartIndex            Index of the partition to load.
*    pPartInfo            [OUT] Information about the partition. Can be set to NULL.
*    pData                [IN] Sector data that stores the partition entry.
*    ldEntriesPerSector   Number of partition entries in a sector as power of 2 exponent.
*    ldSizeOfEntry        Size of a partition entry in bytes as power of 2 exponent.
*
*  Return value
*    ==0    OK, information loaded successfully.
*    !=0    An error occurred.
*/
static int _LoadPartitionInfoGPT(unsigned PartIndex, FS_PARTITION_INFO_GPT * pPartInfo, const U8 * pData, unsigned ldEntriesPerSector, unsigned ldSizeOfEntry) {
  unsigned    Off;
  const U16 * pUnicode;
  int         r;
  int         Result;
  FS_WCHAR    UnicodeChar;
  U8        * pUTF8;
  unsigned    MaxNumBytes;
  unsigned    NumBytesWritten;
  unsigned    NumCharsUnicode;
  unsigned    i;
  U64         StartSector;
  U64         EndSector;
  U64         NumSectors;

  ASSERT_PART_INDEX_GPT_IS_IN_RANGE(PartIndex);
  r = 0;            // Set to indicate success.
  //
  // Calculate the position of the partition entry in the sector.
  //
  Off     = PartIndex & ((1uL << ldEntriesPerSector) - 1u);
  Off   <<= ldSizeOfEntry;
  pData  += Off;
  StartSector = FS_LoadU64LE(pData + GPT_ENTRY_OFF_START_SECTOR);
  EndSector   = FS_LoadU64LE(pData + GPT_ENTRY_OFF_END_SECTOR);
  if (StartSector >= EndSector) {
    FS_DEBUG_WARN((FS_MTYPE_API, "PART_API: _LoadPartitionInfoGPT: Invalid GPT partition (StartSector: %u, EndSector: %u).", StartSector, EndSector));
    r = FS_ERRCODE_INVALID_GPT;                                       // Error, invalid sector range.
  } else {
    if (pPartInfo != NULL) {
      NumSectors = EndSector - StartSector + 1u;
      pPartInfo->StartSector = StartSector;
      pPartInfo->NumSectors  = NumSectors;
      pPartInfo->Attributes  = FS_LoadU64LE(pData + GPT_ENTRY_OFF_ATTR);
      FS_MEMCPY(pPartInfo->abType, pData + GPT_ENTRY_OFF_PART_TYPE, FS_NUM_BYTES_GUID);
      FS_MEMCPY(pPartInfo->abId,   pData + GPT_ENTRY_OFF_PART_ID,   FS_NUM_BYTES_GUID);
      //
      // Encode the partition name as UTF-8.
      //
      FS_MEMSET(pPartInfo->acName, 0, FS_MAX_NUM_BYTES_PART_NAME);
      pUnicode        = SEGGER_CONSTPTR2PTR(const U16, pData + GPT_ENTRY_OFF_NAME);
      pUTF8           = SEGGER_PTR2PTR(U8, pPartInfo->acName);
      MaxNumBytes     = (unsigned)FS_MAX_NUM_BYTES_PART_NAME - 1u;      // -1 to reserve space for the 0-terminator.
      NumCharsUnicode = GPT_ENTRY_SIZE_OF_PART_NAME / 2u;               // /2 because each Unicode character is stored as two bytes.
      for (i = 0; i < NumCharsUnicode; ++i) {
        UnicodeChar = FS_LoadU16LE(SEGGER_CONSTPTR2PTR(const U8, pUnicode));
        if (UnicodeChar == 0u) {                                        // The partition name is 0 terminated.
          *pUTF8 = 0;
        } else {
          Result = FS_UNICODE_EncodeCharUTF8(pUTF8, MaxNumBytes, UnicodeChar);
          if (Result < 0) {
            FS_DEBUG_ERROROUT((FS_MTYPE_API, "PART_API: _LoadPartitionInfoGPT: Invalid partition name (UnicodeChar: 0x%04X).", UnicodeChar));
            r = Result;
            break;                                                      // Error, could not encode partition name.
          }
          NumBytesWritten = (unsigned)Result;
          MaxNumBytes -= NumBytesWritten;
          pUTF8       += NumBytesWritten;
          if (MaxNumBytes == 0u) {
            break;
          }
          ++pUnicode;
        }
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _ReadPartitionInfoGPT
*
*  Function description
*    Returns information about a GPT partition.
*
*  Parameters
*    pVolume          [IN] Volume instance.
*    pGPTInfo         [OUT] Information about the GPT partitioning. Can be set to NULL.
*    pPartInfo        [OUT] Partition information. Can be set to NULL.
*    PartIndex        Index of the partition to query (0-based).
*    pBuffer          Sector buffer.
*    pDeviceInfo      [IN] Information about the storage device.
*
*  Return value
*    ==0    OK, partition information read.
*    !=0    Error code indicating the failure reason.
*
*  Additional information
*    This function is able to return information about the partitioning
*    via pGPTInfo. In this case, pPartInfo can be set to NULL.
*    It the partitioning information is not required then pGPTInfo
*    can be set to NULL.
*/
static int _ReadPartitionInfoGPT(FS_VOLUME * pVolume, FS_GPT_INFO * pGPTInfo, FS_PARTITION_INFO_GPT * pPartInfo, unsigned PartIndex, U8 * pBuffer, const FS_DEV_INFO * pDeviceInfo) {
  int         r;
  FS_DEVICE * pDevice;
  U32         NumEntries;
  U32         SectorIndexFirstEntry;
  U32         SectorIndexEntry;
  U32         SectorIndex;
  U32         SizeOfEntry;
  U32         crcRead;
  U32         crcCalc;
  U32         NumSectorsList;
  U32         NumBytesEntryList;
  U32         NumBytes;
  unsigned    ldSizeOfEntry;
  unsigned    ldBytesPerSector;
  unsigned    ldEntriesPerSector;
  U32         SectorIndexPart;
  U32         NumSectorsPart;
  U32         SectorIndexBackup;
  unsigned    BytesPerSector;
  U32         NumSectorsDevice;
  U64         StartSector;
  U64         EndSector;
  U64         NumSectors;
  U32         NumPartitions;
  int         IsValidMain;
  int         IsValidBackup;
  int         Result;

  ASSERT_PART_INDEX_GPT_IS_IN_RANGE(PartIndex);
  //
  // Initialize local variables.
  //
  Result           = 0;
  pDevice          = &pVolume->Partition.Device;
  BytesPerSector   = pDeviceInfo->BytesPerSector;
  NumSectorsDevice = pDeviceInfo->NumSectors;
  FS_MEMSET(pBuffer, 0, BytesPerSector);
  //
  // Read the first logical sector from storage.
  //
  r = FS_LB_ReadDevice(pDevice, MBR_SECTOR_INDEX, pBuffer, FS_SECTOR_TYPE_MAN);
  if (r != 0) {
    return FS_ERRCODE_READ_FAILURE;         // Error, could not read GPT header.
  }
  //
  // OK, data of the first logical sector read.
  // Check if the protective MBR is present.
  //
  if (_HasSignature(pBuffer) == 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "PART_API: _ReadPartitionInfoGPT: Invalid protective MBR signature."));
    return FS_ERRCODE_INVALID_GPT;          // Error, no protective MBR found.
  }
  if (_IsBPB(pBuffer) != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "PART_API: _ReadPartitionInfoGPT: Found BPB instead of protective MBR."));
    return FS_ERRCODE_INVALID_GPT;          // Error, no protective MBR found.
  }
  if (_IsProtectiveMBR(pBuffer) == 0){
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "PART_API: _ReadPartitionInfoGPT: No protective MBR found."));
    return FS_ERRCODE_INVALID_GPT;          // Error, no protective MBR found.
  }
  //
  // Get information from the protective MBR partition.
  //
  SectorIndexPart = _LoadStartSector(MBR_PROTECTIVE_INDEX, pBuffer);
  NumSectorsPart  = _LoadNumSectors(MBR_PROTECTIVE_INDEX, pBuffer);
  if (NumSectorsPart == 0xFFFFFFFFuL) {
    if (SectorIndexPart < NumSectorsDevice) {
      NumSectorsPart = NumSectorsDevice - SectorIndexPart;
    }
  }
  //
  // OK, the storage device is partitioned via GPT. Read the main GPT header.
  //
  IsValidMain   = 1;
  IsValidBackup = 1;
  r = FS_LB_ReadDevice(pDevice, GPT_HEADER_MAIN_SECTOR, pBuffer, FS_SECTOR_TYPE_MAN);
  if (r != 0) {
    IsValidMain = 0;                                                          // Error, could not read the main GPT header.
  } else {
    //
    // Check if the main GPT header contains valid data.
    //
    r = _CheckGPTHeader(pBuffer, BytesPerSector, GPT_HEADER_MAIN_SECTOR, 0);  // 0 means that this is the main GPT header.
    if (r != 0) {
      IsValidMain = 0;                                                        // Error, the main GPT header is not valid.
    } else {
      if (pGPTInfo != NULL) {
        //
        // OK, the main GPT header is valid. Read the information about the partition.
        //
        StartSector   = FS_LoadU64LE(pBuffer + GPT_HEADER_OFF_FIRST_FS_SECTOR);
        EndSector     = FS_LoadU64LE(pBuffer + GPT_HEADER_OFF_LAST_FS_SECTOR);
        NumPartitions = (U16)FS_LoadU32LE(pBuffer + GPT_HEADER_OFF_NUM_ENTRIES);
        NumSectors    = EndSector - StartSector + 1u;
        pGPTInfo->StartSector   = StartSector;
        pGPTInfo->NumSectors    = NumSectors;
        pGPTInfo->NumPartitions = (U16)NumPartitions;
        pGPTInfo->IsValidMain   = (U8)IsValidMain;
        pGPTInfo->IsValidBackup = (U8)IsValidBackup;
        FS_MEMCPY(pGPTInfo->abId, pBuffer + GPT_HEADER_OFF_DISK_ID, FS_NUM_BYTES_GUID);
      }
      //
      // OK, the main GPT header is valid. Read the information from the specified partition entry.
      //
      SectorIndexFirstEntry = (U32)FS_LoadU64LE(pBuffer + GPT_HEADER_OFF_FIRST_ENTRY_SECTOR);     // The cast is safe because the GPT is located at the beginning of the storage device.
      NumEntries            = FS_LoadU32LE(pBuffer + GPT_HEADER_OFF_NUM_ENTRIES);
      SizeOfEntry           = FS_LoadU32LE(pBuffer + GPT_HEADER_OFF_SIZE_OF_ENTRY);
      crcRead               = FS_LoadU32LE(pBuffer + GPT_HEADER_OFF_CRC_ENTRIES);
      if (PartIndex >= NumEntries) {
        FS_DEBUG_WARN((FS_MTYPE_DRIVER, "PART_API: _ReadPartitionInfoGPT: Invalid partition index (%u not in [0, %u]).", PartIndex, NumEntries - 1u));
        Result = FS_ERRCODE_INVALID_PARA;                                     // Error, invalid partition index.
      }
      ldSizeOfEntry      = _ld(SizeOfEntry);
      ldBytesPerSector   = _ld(BytesPerSector);
      ldEntriesPerSector = ldBytesPerSector - ldSizeOfEntry;
      NumBytesEntryList  = NumEntries << ldSizeOfEntry;
      NumSectorsList     = (NumBytesEntryList + (BytesPerSector - 1u)) >> ldBytesPerSector;
      if (NumSectorsList == 0u) {
        FS_DEBUG_WARN((FS_MTYPE_DRIVER, "PART_API: _ReadPartitionInfoGPT: Invalid entry list."));
        IsValidMain = 0;                                                      // Error, invalid entry list.
      } else {
        //
        // Read the data from all the sectors that store the entry list
        // and calculate the CRC.
        //
        SectorIndexEntry  = PartIndex >> ldEntriesPerSector;
        SectorIndexEntry += SectorIndexFirstEntry;
        SectorIndex       = SectorIndexFirstEntry;
        crcCalc = GPT_CRC_INIT;
        do {
          r = FS_LB_ReadDevice(pDevice, SectorIndex, pBuffer, FS_SECTOR_TYPE_MAN);
          if (r != 0) {
            IsValidMain = 0;                                                  // Error, could not read sector data.
            break;
          }
          if (SectorIndex == SectorIndexEntry) {
            r = _LoadPartitionInfoGPT(PartIndex, pPartInfo, pBuffer, ldEntriesPerSector, ldSizeOfEntry);
            if (r != 0) {
              IsValidMain = 0;                                                // Error, could not load entry information.
              break;
            }
          }
          NumBytes = SEGGER_MIN(BytesPerSector, NumBytesEntryList);
          crcCalc = FS_CRC32_Calc(pBuffer, NumBytes, crcCalc);
          NumBytesEntryList -= NumBytes;
          ++SectorIndex;
        } while (--NumSectorsList != 0u);
        if (IsValidMain != 0) {
          crcCalc ^= GPT_CRC_INIT;
          if (crcCalc != crcRead) {
            FS_DEBUG_WARN((FS_MTYPE_DRIVER, "PART_API: _ReadPartitionInfoGPT: Invalid entry list CRC (crcRead: 0x%08X, crcCalc: 0x%08X).", crcCalc, crcRead));
            IsValidMain = 0;                                                  // Error, invalid CRC.
          }
        }
      }
    }
  }
  //
  // Check the validity of the data in the backup GPT header.
  //
  SectorIndexBackup = (SectorIndexPart + NumSectorsPart) - 1u;
  r = FS_LB_ReadDevice(pDevice, SectorIndexBackup, pBuffer, FS_SECTOR_TYPE_MAN);
  if (r != 0) {
    IsValidBackup = 0;                                                        // Error, could not read the main GPT header.
  } else {
    r = _CheckGPTHeader(pBuffer, BytesPerSector, SectorIndexBackup, 1);       // 1 means that this is the backup GPT header.
    if (r != 0) {
      IsValidBackup = 0;                                                      // Error, the backup GPT header is not valid.
    } else {
      if (IsValidMain == 0) {
        if (pGPTInfo != NULL) {
          //
          // OK, the backup GPT header is valid. Read the information about the partition.
          //
          StartSector   = FS_LoadU64LE(pBuffer + GPT_HEADER_OFF_FIRST_FS_SECTOR);
          EndSector     = FS_LoadU64LE(pBuffer + GPT_HEADER_OFF_LAST_FS_SECTOR);
          NumPartitions = (U16)FS_LoadU32LE(pBuffer + GPT_HEADER_OFF_NUM_ENTRIES);
          NumSectors    = EndSector - StartSector + 1u;
          pGPTInfo->StartSector   = StartSector;
          pGPTInfo->NumSectors    = NumSectors;
          pGPTInfo->NumPartitions = (U16)NumPartitions;
          pGPTInfo->IsValidMain   = (U8)IsValidMain;
          pGPTInfo->IsValidBackup = (U8)IsValidBackup;
          FS_MEMCPY(pGPTInfo->abId, pBuffer + GPT_HEADER_OFF_DISK_ID, FS_NUM_BYTES_GUID);
        }
      }
      //
      // OK, the backup GPT header is valid. Read the information from the specified partition entry.
      //
      SectorIndexFirstEntry = (U32)FS_LoadU64LE(pBuffer + GPT_HEADER_OFF_FIRST_ENTRY_SECTOR);     // The cast is safe because the GPT is located at the beginning of the storage device.
      NumEntries            = FS_LoadU32LE(pBuffer + GPT_HEADER_OFF_NUM_ENTRIES);
      SizeOfEntry           = FS_LoadU32LE(pBuffer + GPT_HEADER_OFF_SIZE_OF_ENTRY);
      crcRead               = FS_LoadU32LE(pBuffer + GPT_HEADER_OFF_CRC_ENTRIES);
      if (PartIndex >= NumEntries) {
        FS_DEBUG_WARN((FS_MTYPE_DRIVER, "PART_API: _ReadPartitionInfoGPT: Invalid partition index (%u not in [0, %u]).", PartIndex, NumEntries - 1u));
        Result = FS_ERRCODE_INVALID_PARA;                                     // Error, invalid partition index.
      }
      ldSizeOfEntry      = _ld(SizeOfEntry);
      ldBytesPerSector   = _ld(BytesPerSector);
      ldEntriesPerSector = ldBytesPerSector - ldSizeOfEntry;
      NumBytesEntryList  = NumEntries << ldSizeOfEntry;
      NumSectorsList     = (NumBytesEntryList + (BytesPerSector - 1u)) >> ldBytesPerSector;
      if (NumSectorsList == 0u) {
        FS_DEBUG_WARN((FS_MTYPE_DRIVER, "PART_API: _ReadPartitionInfoGPT: Invalid entry list."));
        IsValidBackup = 0;                                                    // Error, invalid entry list.
      } else {
        //
        // Read the data from all the sectors that store the entry list
        // and calculate the CRC.
        //
        SectorIndexEntry  = PartIndex >> ldEntriesPerSector;
        SectorIndexEntry += SectorIndexFirstEntry;
        SectorIndex       = SectorIndexFirstEntry;
        crcCalc = GPT_CRC_INIT;
        do {
          r = FS_LB_ReadDevice(pDevice, SectorIndex, pBuffer, FS_SECTOR_TYPE_MAN);
          if (r != 0) {
            IsValidBackup = 0;                                                // Error, could not read sector data.
            break;
          }
          if (SectorIndex == SectorIndexEntry) {
            r = _LoadPartitionInfoGPT(PartIndex, pPartInfo, pBuffer, ldEntriesPerSector, ldSizeOfEntry);
            if (r != 0) {
              IsValidBackup = 0;                                              // Error, could not load entry information.
              break;
            }
          }
          NumBytes = SEGGER_MIN(BytesPerSector, NumBytesEntryList);
          crcCalc = FS_CRC32_Calc(pBuffer, NumBytes, crcCalc);
          NumBytesEntryList -= NumBytes;
          ++SectorIndex;
        } while (--NumSectorsList != 0u);
        if (IsValidBackup != 0) {
          crcCalc ^= GPT_CRC_INIT;
          if (crcCalc != crcRead) {
            FS_DEBUG_WARN((FS_MTYPE_DRIVER, "PART_API: _ReadPartitionInfoGPT: Invalid entry list CRC (crcRead: 0x%08X, crcCalc: 0x%08X).", crcCalc, crcRead));
            IsValidBackup = 0;                                                // Error, invalid CRC.
          }
        }
      }
      if (pGPTInfo != NULL) {
        pGPTInfo->IsValidBackup = (U8)IsValidBackup;
      }
    }
  }
  if ((IsValidMain == 0) && (IsValidBackup == 0)) {
    if (Result != 0) {
      return Result;
    }
    return FS_ERRCODE_INVALID_GPT;                                            // Error, no valid GPT header found.
  }
  return FS_ERRCODE_OK;                                                       // OK, information read.
}

/*********************************************************************
*
*       _GetPartitionInfoGPT_NL
*
*  Function description
*    Returns information about a GPT partition.
*
*  Parameters
*    pVolume        [IN] Volume instance.
*    pGPTInfo       [OUT] Information about the GPT partitioning. Can be set to NULL.
*    pPartInfo      [OUT] Partition information.
*    PartIndex      Index of the partition to query (0-based).
*
*  Return value
*    ==0    OK, partition information read.
*    !=0    Error code indicating the failure reason.
*
*  Additional information
*    This function performs the same operation as _GetPartitionInfoGPT()
*    with the difference that it does not lock the file system.
*/
static int _GetPartitionInfoGPT_NL(FS_VOLUME * pVolume, FS_GPT_INFO * pGPTInfo, FS_PARTITION_INFO_GPT * pPartInfo, unsigned PartIndex) {
  U8          * pBuffer;
  int           r;
  FS_DEVICE   * pDevice;
  int           Status;
  FS_DEV_INFO   DeviceInfo;

  r = FS_ERRCODE_STORAGE_NOT_PRESENT;             // Set to indicate error.
  pDevice = &pVolume->Partition.Device;
  Status = FS_LB_GetStatus(pDevice);
  //
  // Try to get the information only if the storage device is present.
  //
  if (Status != FS_MEDIA_NOT_PRESENT) {
    FS_MEMSET(&DeviceInfo, 0, sizeof(DeviceInfo));
    r = FS_LB_GetDeviceInfo(pDevice, &DeviceInfo);
    if (r != 0) {
      r = FS_ERRCODE_STORAGE_NOT_READY;           // Error, could not get device info.
    } else {
      //
      // Allocate a sector buffer and perform the operation.
      //
      pBuffer = FS__AllocSectorBuffer();
      if (pBuffer == NULL) {
        r = FS_ERRCODE_BUFFER_NOT_AVAILABLE;      // Error, could not allocate sector buffer.
      } else {
        r = _ReadPartitionInfoGPT(pVolume, pGPTInfo, pPartInfo, PartIndex, pBuffer, &DeviceInfo);
        FS__FreeSectorBuffer(pBuffer);
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _GetPartitionInfoGPT
*
*  Function description
*    Returns information about a GPT partition.
*
*  Parameters
*    pVolume        [IN] Volume instance.
*    pPartInfo      [OUT] Partition information.
*    PartIndex      Index of the partition to query (0-based).
*
*  Return value
*    ==0    OK, partition information read.
*    !=0    Error code indicating the failure reason.
*/
static int _GetPartitionInfoGPT(FS_VOLUME * pVolume, FS_PARTITION_INFO_GPT * pPartInfo, unsigned PartIndex) {
  int r;

  FS_LOCK_DRIVER(&pVolume->Partition.Device);
  r = _GetPartitionInfoGPT_NL(pVolume, NULL, pPartInfo, PartIndex);
  FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
  return r;
}

/*********************************************************************
*
*       _GetGPTInfo
*
*  Function description
*    Returns information about a partitioning via GPT.
*
*  Parameters
*    pVolume        [IN] Volume instance.
*    pGPTInfo       [OUT] Information about the GPT partitioning.
*
*  Return value
*    ==0    OK, partition information read.
*    !=0    Error code indicating the failure reason.
*/
static int _GetGPTInfo(FS_VOLUME * pVolume, FS_GPT_INFO * pGPTInfo) {
  int r;

  FS_LOCK_DRIVER(&pVolume->Partition.Device);
  r = _GetPartitionInfoGPT_NL(pVolume, pGPTInfo, NULL, 0);
  FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
  return r;
}

/*********************************************************************
*
*       _CalcNumSectorsGPT
*
*  Function description
*    Calculates the number of logical sectors required to store
*    a GPT with specified parameters.
*
*  Parameters
*    NumPartitions    Number of entries in the partition table.
*    BytesPerSector   Logical sector size in bytes.
*
*  Return value
*    Number of logical sectors.
*
*  Additional information
*    The place required for the protective MBR sector is not included.
*/
static U32 _CalcNumSectorsGPT(unsigned NumPartitions, unsigned BytesPerSector) {
  U32      NumSectors;
  U32      NumBytes;
  unsigned ldBytesPerSector;

  ldBytesPerSector  = _ld(BytesPerSector);
  NumBytes          = NumPartitions << GPT_ENTRY_SIZE_SHIFT;
  NumSectors        = (NumBytes + (BytesPerSector - 1u)) >> ldBytesPerSector;
  NumSectors       += 1u;           // +1 for GPT header.
  return NumSectors;
}

/*********************************************************************
*
*       _WriteGPT
*
*  Function description
*    Writes the GPT partitioning information to storage device.
*
*  Parameters
*    pVolume          [IN] Volume instance.
*    pGPTInfo         [IN] Information about the GPT partitioning.
*    pPartInfo        [IN] Partition list.
*    NumPartitions    Number of partitions to create.
*    pBuffer          Sector buffer.
*    pDeviceInfo      [IN] Information about the storage device.
*
*  Return value
*    ==0    OK, partition created successfully.
*    !=0    Error code indicating the failure reason.
*/
static int _WriteGPT(FS_VOLUME * pVolume, const FS_GPT_INFO * pGPTInfo, const FS_PARTITION_INFO_GPT * pPartInfo, int NumPartitions, U8 * pBuffer, const FS_DEV_INFO * pDeviceInfo) {
  FS_PARTITION_INFO_MBR   PartInfoMBR;
  U32                     NumSectorsDevice;
  int                     r;
  unsigned                ldBytesPerSector;
  unsigned                NumPartitionsTotal;
  U32                     SectorIndex;
  U32                     crc;
  unsigned                iPart;
  unsigned                ldEntriesPerSector;
  FS_DEVICE             * pDevice;
  unsigned                NumBytesWritten;
  U32                     SectorIndexFirstEntry;
  U32                     SectorIndexFirstEntryBackup;
  U32                     SectorIndexEntry;
  U64                     SectorIndexBackup;
  U64                     SectorIndexFirstFS;
  U64                     SectorIndexLastFS;
  unsigned                NumSectorsGPT;
  unsigned                NumBytesEntry;
  unsigned                iSector;
  unsigned                BytesPerSector;

  //
  // Initialize local variables.
  //
  pDevice          = &pVolume->Partition.Device;
  BytesPerSector   = pDeviceInfo->BytesPerSector;
  NumSectorsDevice = pDeviceInfo->NumSectors;
  //
  // Write the protective MBR.
  //
  FS_MEMSET(&PartInfoMBR, 0, sizeof(PartInfoMBR));
  PartInfoMBR.StartSector = GPT_HEADER_MAIN_SECTOR;
  PartInfoMBR.NumSectors  = (U32)(NumSectorsDevice - GPT_HEADER_MAIN_SECTOR);
  FS__CalcPartitionInfoMBR(&PartInfoMBR, (U32)NumSectorsDevice);
  PartInfoMBR.Type = GPT_OS_TYPE;
  r = _WriteMBR(pVolume, &PartInfoMBR, 1, pBuffer, BytesPerSector);
  if (r != 0) {
    return r;                                             // Error, could not write protective MBR.
  }
  //
  // Write the partition entries. We have to write them before the GPT header
  // because a CRC of all partition entries is stored in the GPT header.
  //
  ldBytesPerSector   = _ld(BytesPerSector);
  NumPartitionsTotal = pGPTInfo->NumPartitions;
  ldEntriesPerSector = ldBytesPerSector - GPT_ENTRY_SIZE_SHIFT;
  NumBytesEntry      = 1uL << GPT_ENTRY_SIZE_SHIFT;
  //
  // Write the partition entries and calculate the CRC.
  //
  FS_MEMSET(pBuffer, 0, BytesPerSector);
  NumBytesWritten       = 0;
  crc                   = GPT_CRC_INIT;
  SectorIndexFirstEntry = GPT_HEADER_MAIN_SECTOR + 1u;    // +1 because the entry list starts immediately after the GPT header.
  SectorIndex           = SectorIndexFirstEntry;
  for (iPart = 0; iPart < NumPartitionsTotal; ++iPart) {
    SectorIndexEntry = (iPart >> ldEntriesPerSector) + SectorIndexFirstEntry;
    if (SectorIndex != SectorIndexEntry) {
      r = FS_LB_WriteDevice(pDevice, SectorIndex, pBuffer, FS_SECTOR_TYPE_MAN, 0);
      if (r != 0) {
        return FS_ERRCODE_WRITE_FAILURE;                  // Error, could not write sector data.
      }
      SectorIndex = SectorIndexEntry;
      crc = FS_CRC32_Calc(pBuffer, NumBytesWritten, crc);
      FS_MEMSET(pBuffer, 0, BytesPerSector);
      NumBytesWritten = 0;
    }
    if (iPart < (unsigned)NumPartitions) {
      r = _StorePartitionInfoGPT(iPart, pPartInfo, pBuffer, ldEntriesPerSector, GPT_ENTRY_SIZE_SHIFT);
      if (r != 0) {
        return r;                                         // Error, could not write partition entry.
      }
      ++pPartInfo;
    }
    NumBytesWritten += NumBytesEntry;
  }
  if (NumBytesWritten != 0u) {
    r = FS_LB_WriteDevice(pDevice, SectorIndex, pBuffer, FS_SECTOR_TYPE_MAN, 0);
    if (r != 0) {
      return FS_ERRCODE_WRITE_FAILURE;                    // Error, could not write sector data.
    }
    crc = FS_CRC32_Calc(pBuffer, NumBytesWritten, crc);
  }
  crc ^= GPT_CRC_INIT;
  //
  // Write the main GPT header.
  //
  FS_MEMSET(pBuffer, 0, BytesPerSector);
  SectorIndexFirstFS = pGPTInfo->StartSector;
  SectorIndexLastFS  = SectorIndexFirstFS + pGPTInfo->NumSectors - 1u;
  NumSectorsGPT      = _CalcNumSectorsGPT(NumPartitionsTotal, BytesPerSector);
  SectorIndexBackup  = SectorIndexLastFS + NumSectorsGPT;
  FS_StoreU64LE(pBuffer + GPT_HEADER_OFF_SIGNATURE, GPT_HEADER_SIGNATURE);
  FS_StoreU32LE(pBuffer + GPT_HEADER_OFF_REVISION, GPT_HEADER_REVISION);
  FS_StoreU32LE(pBuffer + GPT_HEADER_OFF_SIZE, GPT_HEADER_SIZE);
  FS_StoreU64LE(pBuffer + GPT_HEADER_OFF_MY_SECTOR, (U64)GPT_HEADER_MAIN_SECTOR);
  FS_StoreU64LE(pBuffer + GPT_HEADER_OFF_BACKUP_SECTOR, SectorIndexBackup);
  FS_StoreU64LE(pBuffer + GPT_HEADER_OFF_FIRST_FS_SECTOR, SectorIndexFirstFS);
  FS_StoreU64LE(pBuffer + GPT_HEADER_OFF_LAST_FS_SECTOR, SectorIndexLastFS);
  FS_MEMCPY(pBuffer + GPT_HEADER_OFF_DISK_ID, pGPTInfo->abId, FS_NUM_BYTES_GUID);
  FS_StoreU64LE(pBuffer + GPT_HEADER_OFF_FIRST_ENTRY_SECTOR, (U64)SectorIndexFirstEntry);
  FS_StoreU32LE(pBuffer + GPT_HEADER_OFF_NUM_ENTRIES, NumPartitionsTotal);
  FS_StoreU32LE(pBuffer + GPT_HEADER_OFF_SIZE_OF_ENTRY, NumBytesEntry);
  FS_StoreU32LE(pBuffer + GPT_HEADER_OFF_CRC_ENTRIES, crc);
  crc = GPT_CRC_INIT;
  crc = FS_CRC32_Calc(pBuffer, GPT_HEADER_SIZE, crc);
  crc ^= GPT_CRC_INIT;
  FS_StoreU32LE(pBuffer + GPT_HEADER_OFF_CRC, crc);
  r = FS_LB_WriteDevice(pDevice, GPT_HEADER_MAIN_SECTOR, pBuffer, FS_SECTOR_TYPE_MAN, 0);
  if (r != 0) {
    return FS_ERRCODE_WRITE_FAILURE;                      // Error, could not write sector data.
  }
  //
  // Create the backup partition table. The data in the backup header
  // is identical with that of the main header with the exception
  // of the MyLBA, AlternateLBA and PartitionEntryLBA fields.
  // In addition, the CRC of the backup GPT is different.
  //
  SectorIndexFirstEntryBackup = (U32)SectorIndexLastFS + 1u;
  FS_StoreU64LE(pBuffer + GPT_HEADER_OFF_MY_SECTOR, SectorIndexBackup);
  FS_StoreU64LE(pBuffer + GPT_HEADER_OFF_BACKUP_SECTOR, (U64)GPT_HEADER_MAIN_SECTOR);
  FS_StoreU64LE(pBuffer + GPT_HEADER_OFF_FIRST_ENTRY_SECTOR, (U64)SectorIndexFirstEntryBackup);
  FS_StoreU32LE(pBuffer + GPT_HEADER_OFF_CRC, 0);
  crc = GPT_CRC_INIT;
  crc = FS_CRC32_Calc(pBuffer, GPT_HEADER_SIZE, crc);
  crc ^= GPT_CRC_INIT;
  FS_StoreU32LE(pBuffer + GPT_HEADER_OFF_CRC, crc);
  r = FS_LB_WriteDevice(pDevice, (U32)SectorIndexBackup, pBuffer, FS_SECTOR_TYPE_MAN, 0);
  if (r != 0) {
    return FS_ERRCODE_WRITE_FAILURE;                      // Error, could not write sector data.
  }
  --NumSectorsGPT;                                        // -- because we have already written the GPT header.
  SectorIndex       = SectorIndexFirstEntry;
  SectorIndexBackup = SectorIndexFirstEntryBackup;
  for (iSector = 0; iSector < NumSectorsGPT; ++iSector) {
    r = FS_LB_ReadDevice(pDevice, (U32)SectorIndex, pBuffer, FS_SECTOR_TYPE_MAN);
    if (r != 0) {
      return FS_ERRCODE_READ_FAILURE;                     // Error, could not read sector data.
    }
    r = FS_LB_WriteDevice(pDevice, (U32)SectorIndexBackup, pBuffer, FS_SECTOR_TYPE_MAN, 0);
    if (r != 0) {
      return FS_ERRCODE_WRITE_FAILURE;                    // Error, could not write sector data.
    }
    ++SectorIndex;
    ++SectorIndexBackup;
  }
  return 0;
}

/*********************************************************************
*
*       _CreateGPT
*
*  Function description
*    Partitions the specified volume using a GPT (GUID Partition Table) scheme.
*
*  Parameters
*    pVolume        [IN] Volume instance.
*    pGPTInfo       [IN] Information about the GPT partitioning.
*    pPartInfo      [IN] Partition list.
*    NumPartitions  Number of partitions to create.
*
*  Return value
*    ==0    OK, partition created successfully.
*    !=0    Error code indicating the failure reason.
*/
static int _CreateGPT(FS_VOLUME * pVolume, FS_GPT_INFO * pGPTInfo, FS_PARTITION_INFO_GPT * pPartInfo, int NumPartitions) {
  int                     r;
  int                     iPart;
  U32                     NumSectorsDevice;
  int                     Status;
  FS_PARTITION_INFO_GPT * pPartInfoToCheck;
  FS_DEVICE             * pDevice;
  FS_DEV_INFO             DeviceInfo;
  unsigned                NumSectorsGPT;
  U32                     StartSector;
  U32                     StartSectorPart;
  U32                     NumSectors;
  U32                     NumSectorsAvail;
  U32                     NumSectorsPart;
  U32                     FreeSector;
  unsigned                NumPartitionsTotal;
  unsigned                BytesPerSector;
  U8                    * pBuffer;

  r       = 0;                                // Set to indicate success.
  pDevice = &pVolume->Partition.Device;
  FS_LOCK_DRIVER(pDevice);
  Status = FS_LB_GetStatus(pDevice);
  //
  // Create GPT only if the storage device is present.
  //
  if (Status == FS_MEDIA_NOT_PRESENT) {
    FS_DEBUG_ERROROUT((FS_MTYPE_API, "PART_API: _CreateGPT: Storage device not present."));
    r = FS_ERRCODE_STORAGE_NOT_READY;         // Error, storage device not present.
    goto Done;
  }
  //
  // Get information about the storage device.
  //
  FS_MEMSET(&DeviceInfo, 0, sizeof(DeviceInfo));
  r = FS_LB_GetDeviceInfo(pDevice, &DeviceInfo);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_API, "PART_API: _CreateGPT: Could not get device info."));
    goto Done;                                // Error, cannot get device information.
  }
  NumSectorsDevice = DeviceInfo.NumSectors;
  BytesPerSector   = DeviceInfo.BytesPerSector;
  //
  // Set the correct number of partitions.
  //
  NumPartitionsTotal = pGPTInfo->NumPartitions;
  NumPartitionsTotal = SEGGER_MAX(NumPartitionsTotal, (unsigned)NumPartitions);
  pGPTInfo->NumPartitions = (U16)NumPartitionsTotal;
  //
  // Calculate the number of sectors required to store the GPT information
  // and verify that the information specified by the application is valid.
  //
  NumSectorsGPT = _CalcNumSectorsGPT(NumPartitionsTotal, BytesPerSector);
  StartSector   = (U32)pGPTInfo->StartSector;
  NumSectors    = (U32)pGPTInfo->NumSectors;
  if (StartSector == 0u) {
    StartSector = NumSectorsGPT + 1u;                 // +1 for the protective MBR sector.
  } else {
    if (   (StartSector < (NumSectorsGPT + 1u))       // +1 for the protective MBR sector.
        || (StartSector >= NumSectorsDevice)) {
      FS_DEBUG_ERROROUT((FS_MTYPE_API, "PART_API: _CreateGPT: Invalid start sector (%u).", StartSector));
      r = FS_ERRCODE_INVALID_PARA;
      goto Done;
    }
  }
  NumSectorsAvail = NumSectorsDevice - StartSector;
  if (NumSectorsAvail <= NumSectorsGPT) {             // Check that there is place for the backup copy of GPT.
    FS_DEBUG_ERROROUT((FS_MTYPE_API, "PART_API: _CreateGPT: Device too small (%u).", NumSectorsDevice));
    r = FS_ERRCODE_INVALID_PARA;
    goto Done;
  }
  NumSectorsAvail -= NumSectorsGPT;                   // Reserve space for the backup copy of GPT.
  if (NumSectors == 0u) {
    NumSectors = NumSectorsAvail;                     // Use the entire available storage space.
  } else {
    if (NumSectors > NumSectorsAvail) {
      FS_DEBUG_ERROROUT((FS_MTYPE_API, "PART_API: _CreateGPT: Invalid number of sectors (%u).", NumSectors));
      r = FS_ERRCODE_INVALID_PARA;
      goto Done;
    }
  }
  pGPTInfo->StartSector = StartSector;
  pGPTInfo->NumSectors  = NumSectors;
  //
  // For all configured partitions fill in the missing parameters.
  //
  FreeSector       = StartSector;
  pPartInfoToCheck = pPartInfo;
  for (iPart = 0; iPart < NumPartitions; ++iPart) {
    StartSectorPart = (U32)pPartInfoToCheck->StartSector;
    NumSectorsPart  = (U32)pPartInfoToCheck->NumSectors;
    //
    // If a start sector is not specified, then use the first free sector.
    //
    if (StartSectorPart == 0u) {
      StartSectorPart = FreeSector;
    } else {
      if (StartSectorPart < FreeSector) {
        FS_DEBUG_ERROROUT((FS_MTYPE_API, "PART_API: _CreateGPT: Invalid start sector of partition %d.", iPart));
        r = FS_ERRCODE_INVALID_PARA;
        goto Done;
      }
    }
    if ((StartSectorPart + NumSectorsPart) > NumSectors) {
      FS_DEBUG_ERROROUT((FS_MTYPE_API, "PART_API: _CreateGPT: Overflow of partition %d.", iPart));
      r = FS_ERRCODE_INVALID_PARA;
      goto Done;
    }
    if (NumSectorsPart == 0u) {
      if ((iPart + 1) != NumPartitions) {
        FS_DEBUG_ERROROUT((FS_MTYPE_API, "PART_API: _CreateGPT: Invalid number of sectors in partition %d.", iPart));
        r = FS_ERRCODE_INVALID_PARA;
        goto Done;
      }
      //
      // Assign the remaining storage space to last partition.
      //
      NumSectorsPart = NumSectors - StartSectorPart;
    }
    pPartInfoToCheck->StartSector = StartSectorPart;
    pPartInfoToCheck->NumSectors  = NumSectorsPart;
    FreeSector = StartSectorPart + NumSectorsPart;
    ++pPartInfoToCheck;
  }
  //
  // Store GPT information to the storage device.
  //
  pBuffer = FS__AllocSectorBuffer();
  if (pBuffer == NULL) {
    r = FS_ERRCODE_BUFFER_NOT_AVAILABLE;      // Error, could not allocate sector buffer.
    goto Done;
  }
  r = _WriteGPT(pVolume, pGPTInfo, pPartInfo, NumPartitions, pBuffer, &DeviceInfo);
  FS__FreeSectorBuffer(pBuffer);
Done:
  FS_UNLOCK_DRIVER(pDevice);
  return r;
}

#endif // FS_SUPPORT_GPT

/*********************************************************************
*
*       _GetFirstPartitionInfo
*
*  Function description
*    Returns the start sector and the number of sectors in the first partition.
*
*  Parameters
*    pVolume        [IN] Volume instance.
*    pStartSector   [OUT] Index of the first sector in the partition.
*    pNumSectors    [OUT] Number of sectors in the first partition.
*    pBuffer        [IN] Buffer to read the contents of the MBR sector.
*
*  Return value
*    ==0    OK, partition information returned.
*    !=0    Error code indicating the failure reason.
*
*  Notes
*    (1) The failure to read the MBR sector is not reported as an error
*        because in some cases we try to read invalid sector data. The access
*        to an invalid sector is reported as error by some of the device drivers
*        such as the Block Map NOR driver which is expected if the access is performed
*        after the storage device was low-level formatted.
*/
static int _GetFirstPartitionInfo(FS_VOLUME * pVolume, U32 * pStartSector, U32 * pNumSectors, U8 * pBuffer) {
  U32         StartSector;
  U32         NumSectors;
  FS_DEVICE * pDevice;
  int         r;
  FS_DEV_INFO DeviceInfo;
  U32         NumSectorsInPart;

  pDevice = &pVolume->Partition.Device;
  FS_MEMSET(&DeviceInfo, 0, sizeof(DeviceInfo));
  r = FS_LB_GetDeviceInfo(pDevice, &DeviceInfo);
  if (r != 0) {
    return r;                                                           // Error, could not get device info.
  }
  StartSector = 0;
  NumSectors  = DeviceInfo.NumSectors;
  r = FS_LB_ReadDevice(pDevice, 0uL, pBuffer, FS_SECTOR_TYPE_MAN);
  if (r == 0) {                                                         // Note 1
    if (_HasSignature(pBuffer) != 0) {
      if (_IsBPB(pBuffer) == 0) {
        //
        // The sector seems to contain a valid partition table.
        //
#if FS_SUPPORT_GPT
        if (_IsProtectiveMBR(pBuffer) != 0) {                           // Is this a GPT partition?
          FS_PARTITION_INFO_GPT PartInfoGPT;

          FS_MEMSET(&PartInfoGPT, 0, sizeof(PartInfoGPT));
          r = _GetPartitionInfoGPT_NL(pVolume, NULL, &PartInfoGPT, 0u);
          if (r != 0) {
            return r;
          }
          StartSector = (U32)PartInfoGPT.StartSector;
          NumSectors  = (U32)PartInfoGPT.NumSectors;
        } else
#endif // FS_SUPPORT_GPT
        {
          StartSector = _LoadStartSector(0, pBuffer);
          NumSectors  = _LoadNumSectors(0, pBuffer);
          if ((NumSectors == 0u) || (StartSector == 0u)) {
            FS_DEBUG_ERROROUT((FS_MTYPE_API, "PART_API: _GetFirstPartitionInfo: Invalid MBR partition (StartSector: %u, NumSectors: %u).", StartSector, NumSectors));
            return FS_ERRCODE_INVALID_MBR;                              // Error, partition table entry 0 is not valid.
          }
        }
        //
        // Allow a tolerance of 0.4% in order of having a larger partition
        // than the total capacity reported by the storage device.
        //
        NumSectorsInPart = ((StartSector + NumSectors) * 255u) >> 8;
        if (NumSectorsInPart > DeviceInfo.NumSectors) {
          FS_DEBUG_WARN((FS_MTYPE_API, "PART_API: _GetFirstPartitionInfo: Invalid partition size (PartSize: %lu, DeviceSize: %lu).", NumSectorsInPart, DeviceInfo.NumSectors));
          return FS_ERRCODE_INVALID_MBR;                                // Error, partition table entry 0 is out of bounds.
        }
      }
    }
  }
  if (pStartSector != NULL) {
    *pStartSector = StartSector;
  }
  if (pNumSectors != NULL) {
    *pNumSectors = NumSectors;
  }
  return 0;
}

/*********************************************************************
*
*       _LocatePartition
*
*  Function description
*    Determines the location of the first MBR partition.
*
*  Parameters
*    pVolume      Volume instance.
*    pBuffer      Sector buffer.
*
*  Return value
*    ==0    OK, first MBR partition successfully located.
*    !=0    Error code indicating the failure reason.
*/
static int _LocatePartition(FS_VOLUME * pVolume, U8 * pBuffer) {
  U32 StartSector;
  U32 NumSectors;
  int r;

  NumSectors  = 0;
  StartSector = 0;
  //
  // Calculate the layout of the first MBR partition.
  //
  r = _GetFirstPartitionInfo(pVolume, &StartSector, &NumSectors, pBuffer);
  if (r == 0) {
    pVolume->Partition.StartSector = StartSector;
    pVolume->Partition.NumSectors  = NumSectors;
  }
  return r;
}

/*********************************************************************
*
*       _GetPartitioningScheme
*
*  Function description
*    Returns information about how the storage device is partitioned.
*
*  Parameters
*    pVolume        [IN] Volume instance.
*
*  Return value
*    >=0    OK, partitioning type. Can be one of the \ref{Partitioning schemes} values.
*    !=0    Error code indicating the failure reason.
*/
static int _GetPartitioningScheme(FS_VOLUME * pVolume) {
  U8        * pBuffer;
  int         r;
  FS_DEVICE * pDevice;
  int         Status;

  r = FS_ERRCODE_STORAGE_NOT_PRESENT;       // Set to indicate error.
  pDevice = &pVolume->Partition.Device;
  FS_LOCK_DRIVER(pDevice);
  Status = FS_LB_GetStatus(pDevice);
  //
  // Try to get the information only if the storage device is present.
  //
  if (Status != FS_MEDIA_NOT_PRESENT) {
    pBuffer = FS__AllocSectorBuffer();
    if (pBuffer != NULL) {
      FS_MEMSET(pBuffer, 0, FS_Global.MaxSectorSize);
      //
      // Read MBR from storage.
      //
      r = FS_LB_ReadDevice(pDevice, 0uL, pBuffer, FS_SECTOR_TYPE_MAN);
      if (r == 0) {
        //
        // First sector read successfully. Determine the way the storage device is partitioned.
        //
        r = FS__LoadPartitioningScheme(pBuffer);
      }
      FS__FreeSectorBuffer(pBuffer);
    } else {
      r = FS_ERRCODE_BUFFER_NOT_AVAILABLE;
    }
  }
  FS_UNLOCK_DRIVER(pDevice);
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
*       FS__LocatePartition
*
*  Function description
*    Determines the location of the first MBR partition.
*
*  Parameters
*    pVolume      Volume instance.
*
*  Return value
*    ==0    OK, partition located.
*    !=0    Error indicating the failure reason.
*/
int FS__LocatePartition(FS_VOLUME * pVolume) {
  int   r;
  U8  * pBuffer;
  U16   BytesPerSector;

  r              = FS_ERRCODE_BUFFER_NOT_AVAILABLE;   // Set to indicate an error.
  BytesPerSector = FS_GetSectorSize(&pVolume->Partition.Device);
  pBuffer        = FS__AllocSectorBuffer();
  if (pBuffer != NULL) {
    //
    // Check if the a sector fits into the sector buffer.
    //
    if ((BytesPerSector > FS_Global.MaxSectorSize) || (BytesPerSector == 0u)) {
      FS_DEBUG_ERROROUT((FS_MTYPE_API, "PART_API: FS__LocatePartition: Invalid sector size: %d.", BytesPerSector));
      r = FS_ERRCODE_STORAGE_NOT_READY;               // Error, could not get sector size.
    } else {
      r = _LocatePartition(pVolume, pBuffer);
    }
    FS__FreeSectorBuffer(pBuffer);
  }
  return r;
}

/*********************************************************************
*
*       FS__LoadPartitionInfoMBR
*
*  Function description
*    Returns information about a MBR partition.
*
*  Parameters
*    PartIndex      Index of the partition to query.
*    pPartInfo      [OUT] Information about the partition.
*    pData          [IN] MBR sector data.
*/
void FS__LoadPartitionInfoMBR(unsigned PartIndex, FS_PARTITION_INFO_MBR * pPartInfo, const U8 * pData) {
  unsigned   Off;
  const U8 * p;
  int        IsActive;
  U8         Status;

  ASSERT_PART_INDEX_IS_IN_RANGE(PartIndex);
  Off    = MBR_OFF_PARTITION0 + (PartIndex << PART_ENTRY_SIZE_SHIFT);
  p      = pData + Off;
  Status = *p++;
  if ((Status & PART_ENTRY_STATUS_ACTIVE) != 0u) {    // Bootable?
    IsActive = 1;
  } else {                                            // Non-bootable?
    IsActive = 0;
  }
  pPartInfo->IsActive            = (U8)IsActive;
  pPartInfo->StartAddr.Head      = (U8)*p++;
  pPartInfo->StartAddr.Sector    = (U8)(*p & 0x3Fu);
  pPartInfo->StartAddr.Cylinder  = (U16)(((unsigned)*p++ & 0xC0u) << 2);
  pPartInfo->StartAddr.Cylinder += (U16)*p++;
  pPartInfo->Type                = (U8)*p++;
  pPartInfo->EndAddr.Head        = (U8)*p++;
  pPartInfo->EndAddr.Sector      = (U8)(*p & 0x3Fu);
  pPartInfo->EndAddr.Cylinder    = (U16)(((unsigned)*p++ & 0xC0u) << 2);
  pPartInfo->EndAddr.Cylinder   += (U16)*p++;
  pPartInfo->StartSector         = FS_LoadU32LE(p);
  p += 4;
  pPartInfo->NumSectors          = FS_LoadU32LE(p);
}

/*********************************************************************
*
*       FS__StorePartitionInfoMBR
*
*  Function description
*    Modifies a MBR partition.
*
*  Parameters
*    PartIndex      Index of the partition to modify.
*    pPartInfo      [IN] Partition information.
*    pData          [OUT] MBR sector data.
*/
void FS__StorePartitionInfoMBR(unsigned PartIndex, const FS_PARTITION_INFO_MBR * pPartInfo, U8 * pData) {
  unsigned   Off;
  U8       * p;
  U8         Status;

  ASSERT_PART_INDEX_IS_IN_RANGE(PartIndex);
  Off  = MBR_OFF_PARTITION0 + (PartIndex << PART_ENTRY_SIZE_SHIFT);
  p    = pData + Off;
  if (pPartInfo->IsActive != 0u) {
    Status = PART_ENTRY_STATUS_ACTIVE;
  } else {
    Status = PART_ENTRY_STATUS_INACTIVE;
  }
  *p++ = Status;
  *p++ = pPartInfo->StartAddr.Head;
  *p++ = (U8)(((unsigned)pPartInfo->StartAddr.Sector & 0x003Fu)
       | (((unsigned)pPartInfo->StartAddr.Cylinder & 0x0300u) >> 2));
  *p++ = (U8)(pPartInfo->StartAddr.Cylinder & 0x00FFu);
  *p++ = pPartInfo->Type;
  *p++ = pPartInfo->EndAddr.Head;
  *p++ = (U8)(((unsigned)pPartInfo->EndAddr.Sector & 0x003Fu)
       | (((unsigned)pPartInfo->EndAddr.Cylinder & 0x0300u) >> 2));
  *p++ = (U8)(pPartInfo->EndAddr.Cylinder & 0x00FFu);
  FS_StoreU32LE(p, pPartInfo->StartSector);
  p += 4;
  FS_StoreU32LE(p, pPartInfo->NumSectors);
}

/*********************************************************************
*
*       FS__CalcPartitionInfoMBR
*
*  Function description
*    Calculates the location of a partition in CHS (Cylinder/Head/Sector)
*    units and the type of partition.
*
*  Parameters
*    pPartInfo          [IN]  Partition location in sectors.
*                       [OUT] CHS location of the partition and the partition type.
*    NumSectorsDevice   Total number of sectors on the storage device.
*/
void FS__CalcPartitionInfoMBR(FS_PARTITION_INFO_MBR * pPartInfo, U32 NumSectorsDevice) {
  U32              PartFirstSector;
  U32              PartLastSector;
  U32              Data;
  U32              NumSectorsInPart;
  U8               PartType;
  unsigned         i;
  const CHS_INFO * pCHSInfo;

  //
  // Get CHS info from the table based on the number of sectors on the storage device.
  //
  pCHSInfo = _aCHSInfo;
  for (i = 0; i < SEGGER_COUNTOF(_aCHSInfo); i++) {
    pCHSInfo = &_aCHSInfo[i];
    if (pCHSInfo->NumSectors > NumSectorsDevice) {
      break;
    }
  }
  NumSectorsInPart = pPartInfo->NumSectors;
  PartFirstSector  = pPartInfo->StartSector;
  PartLastSector   = PartFirstSector + NumSectorsInPart - 1u;
  //
  // Compute the start of partition.
  //
  Data                           = PartFirstSector % ((U32)pCHSInfo->NumHeads * pCHSInfo->SectorsPerTrack);
  Data                          /= pCHSInfo->SectorsPerTrack;
  pPartInfo->StartAddr.Head      = (U8)Data;
  Data                           = (PartFirstSector % pCHSInfo->SectorsPerTrack) + 1u;
  pPartInfo->StartAddr.Sector    = (U8)Data;
  Data                           = PartFirstSector / ((U32)pCHSInfo->NumHeads * pCHSInfo->SectorsPerTrack);
  pPartInfo->StartAddr.Cylinder  = (U16)Data;
  //
  // Compute the end of partition.
  //
  Data                           = PartLastSector % ((U32)pCHSInfo->NumHeads * pCHSInfo->SectorsPerTrack);
  Data                          /= pCHSInfo->SectorsPerTrack;
  pPartInfo->EndAddr.Head        = (U8)Data;
  Data                           = (PartLastSector % pCHSInfo->SectorsPerTrack) + 1u;
  pPartInfo->EndAddr.Sector      = (U8)Data;
  Data                           = PartLastSector / ((U32)pCHSInfo->NumHeads * pCHSInfo->SectorsPerTrack);
  pPartInfo->EndAddr.Cylinder    = (U16)Data;
  //
  // Determine the partition type.
  //
  if        (NumSectorsInPart < 0x7FA8uL) {
    PartType = 0x01;
  } else if (NumSectorsInPart < 0x010000uL) {
    PartType = 0x04;
  } else if (NumSectorsInPart < 0x400000uL) {
    PartType = 0x06;
  } else if (NumSectorsInPart < 0xFB0400uL) {
    PartType = 0x0B;
  } else {
    PartType = 0x0C;
  }
  pPartInfo->Type = PartType;
}

/*********************************************************************
*
*       FS__CalcDeviceInfo
*
*  Function description
*    Calculates the number of sectors per track and the number of heads
*    of a the specified storage device.
*
*  Parameters
*    pDevInfo     [IN]  Number of sectors on the device.
*                 [OUT] Number of sectors per track and the number of heads.
*/
void FS__CalcDeviceInfo(FS_DEV_INFO * pDevInfo) {
  unsigned         i;
  const CHS_INFO * pCHSInfo;

  pCHSInfo = _aCHSInfo;
  for (i = 0; i < SEGGER_COUNTOF(_aCHSInfo); i++) {
    pCHSInfo = &_aCHSInfo[i];
    if (pCHSInfo->NumSectors > pDevInfo->NumSectors) {
      break;
    }
  }
  pDevInfo->SectorsPerTrack = pCHSInfo->SectorsPerTrack;
  pDevInfo->NumHeads        = pCHSInfo->NumHeads;
}

/*********************************************************************
*
*       FS__WriteMBR
*
*  Function description
*    Writes the Master Boot Record to the first sector (sector index 0)
*    of the specified storage device.
*
*  Parameters
*    pVolume        [IN] Volume instance.
*    pPartInfo      [IN] Partition list.
*    NumPartitions  Number of partitions to create.
*
*  Return value
*    ==0    OK, MBR written successfully.
*    !=0    Error code indicating the failure reason.
*
*  Additional information
*    NumPartitions can be 0 in which case the no MBR is created
*    and the MBR sector is filled with 0s.
*/
int FS__WriteMBR(FS_VOLUME * pVolume, const FS_PARTITION_INFO_MBR * pPartInfo, int NumPartitions) {
  U8  * pBuffer;
  int   r;

  r       = FS_ERRCODE_BUFFER_NOT_AVAILABLE;      // Set to indicate error.
  pBuffer = FS__AllocSectorBuffer();
  if (pBuffer != NULL) {
    r = _WriteMBR(pVolume, pPartInfo, NumPartitions, pBuffer, FS_Global.MaxSectorSize);
    FS__FreeSectorBuffer(pBuffer);
  }
  return r;
}

/*********************************************************************
*
*       FS__CreateMBR
*
*  Function description
*    Partitions the specified volume using a MBR (Master Boot Record) scheme.
*
*  Parameters
*    pVolume        [IN] Volume instance.
*    pPartInfo      [IN] Partition list.
*    NumPartitions  Number of partitions to create.
*
*  Return value
*    ==0    OK, Master Boot Record created successfully.
*    !=0    Error code indicating the failure reason.
*/
int FS__CreateMBR(FS_VOLUME * pVolume, FS_PARTITION_INFO_MBR * pPartInfo, int NumPartitions) {
  int                     r;
  int                     iPart;
  U32                     NumSectorsDevice;
  int                     Status;
  FS_PARTITION_INFO_MBR * pPartInfoToCheck;
  FS_DEVICE             * pDevice;
  FS_DEV_INFO             DeviceInfo;

  r                = FS_ERRCODE_STORAGE_NOT_READY;  // Set to indicate error.
  NumSectorsDevice = 0;
  pDevice          = &pVolume->Partition.Device;
  pPartInfoToCheck = pPartInfo;
  FS_LOCK_DRIVER(pDevice);
  Status = FS_LB_GetStatus(pDevice);
  //
  // Create MBR only if the storage device is present.
  //
  if (Status != FS_MEDIA_NOT_PRESENT) {
    //
    // For all created partitions fill in the missing parameters.
    //
    for (iPart = 0; iPart < NumPartitions; ++iPart) {
      //
      // If not specified, calculate the type of partition and the CHS parameters.
      //
      if (pPartInfoToCheck->Type == 0u) {
        //
        // Get the number of sectors on the storage medium if required.
        //
        if (NumSectorsDevice == 0u) {
          //
          // Get the number of sectors from the driver.
          //
          FS_MEMSET(&DeviceInfo, 0, sizeof(DeviceInfo));
          r = FS_LB_GetDeviceInfo(pDevice, &DeviceInfo);
          if (r == 0) {
            NumSectorsDevice = DeviceInfo.NumSectors;
          }
        }
        FS__CalcPartitionInfoMBR(pPartInfoToCheck, NumSectorsDevice);
      }
      ++pPartInfoToCheck;
    }
    //
    // Store the MBR on the device.
    //
    r = FS__WriteMBR(pVolume, pPartInfo, NumPartitions);
  }
  FS_UNLOCK_DRIVER(pDevice);
  return r;
}

/*********************************************************************
*
*       FS__GetPartitionInfoMBR
*
*  Function description
*    Returns information about a MBR partition.
*
*  Parameters
*    pVolume        [IN] Volume instance.
*    pPartInfo      [OUT] Partition information.
*    PartIndex      Index of the partition to query (0-based).
*
*  Return value
*    ==0    OK, partition information read.
*    !=0    Error code indicating the failure reason.
*/
int FS__GetPartitionInfoMBR(FS_VOLUME * pVolume, FS_PARTITION_INFO_MBR * pPartInfo, unsigned PartIndex) {
  U8        * pBuffer;
  int         r;
  FS_DEVICE * pDevice;
  int         Status;

  ASSERT_PART_INDEX_IS_IN_RANGE(PartIndex);
  r = FS_ERRCODE_STORAGE_NOT_PRESENT;       // Set to indicate error.
  pDevice = &pVolume->Partition.Device;
  FS_LOCK_DRIVER(pDevice);
  Status = FS_LB_GetStatus(pDevice);
  //
  // Try to get the information only if the storage device is present.
  //
  if (Status != FS_MEDIA_NOT_PRESENT) {
    pBuffer = FS__AllocSectorBuffer();
    if (pBuffer != NULL) {
      FS_MEMSET(pBuffer, 0, FS_Global.MaxSectorSize);
      //
      // Read MBR from storage.
      //
      r = FS_LB_ReadDevice(pDevice, 0uL, pBuffer, FS_SECTOR_TYPE_MAN);
      if (r == 0) {
        //
        // MBR read, check whether the signature is correct.
        //
        if (_HasSignature(pBuffer) == 0) {
          FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "PART_API: FS__GetPartitionInfoMBR: Invalid MBR signature."));
          r = FS_ERRCODE_INVALID_MBR;       // Error, no MBR found.
        } else if (_IsBPB(pBuffer) != 0) {
          FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "PART_API: FS__GetPartitionInfoMBR: Found BPB instead of MBR."));
          r = FS_ERRCODE_INVALID_MBR;       // Error, no MBR found.
        } else {
          //
          // Load partition information from read buffer.
          //
          FS__LoadPartitionInfoMBR(PartIndex, pPartInfo, pBuffer);
          r = FS_ERRCODE_OK;                // OK, partition information read.
        }
      }
      FS__FreeSectorBuffer(pBuffer);
    } else {
      r = FS_ERRCODE_BUFFER_NOT_AVAILABLE;
    }
  }
  FS_UNLOCK_DRIVER(pDevice);
  return r;
}

/*********************************************************************
*
*       FS__LoadPartitioningScheme
*
*  Function description
*    Determines how the storage device is partitioned.
*
*  Parameters
*    pData      [IN] Data of the first sector.
*
*  Return value
*    Partitioning type. Can be one of the \ref{Partitioning schemes} values.
*/
int FS__LoadPartitioningScheme(const U8 * pData) {
  int r;

  //
  // Check a valid signature is present. If not, then the storage device
  // is neither formatted nor partitioned.
  //
  if (_HasSignature(pData) == 0) {
    r = FS_PARTITIONING_SCHEME_NONE;      // The storage device is not partitioned.
  } else {
    //
    // Check if a volume format information is present.
    //
    if (_IsBPB(pData) != 0) {
      r = FS_PARTITIONING_SCHEME_NONE;    // The storage device is not partitioned.
    } else {
#if FS_SUPPORT_GPT
      //
      // Check if a protective MBR is present.
      //
      if (_IsProtectiveMBR(pData) == 0){
        r = FS_PARTITIONING_SCHEME_MBR;   // The storage device is partitioned as MBR.
      } else {
        r = FS_PARTITIONING_SCHEME_GPT;   // The storage device is partitioned as GPT.
      }
#else
      r = FS_PARTITIONING_SCHEME_MBR;     // The storage device is partitioned as MBR.
#endif // FS_SUPPORT_GPT
    }
  }
  return r;
}

#if FS_SUPPORT_GPT

/*********************************************************************
*
*       FS__CheckGPTHeader
*
*  Function description
*    Checks if the data in the GPT header is valid.
*
*  Parameters
*    pData            [IN] Sector data.
*    BytesPerSector   Number of bytes in a logical sector.
*    SectorIndex      Index of the sector that stores the GPT header.
*    IsBackup         Set to 1 if the data to be checked is the backup GPT header.
*
*  Return value
*    ==0    OK, the GPT header is valid.
*    !=0    Error, the GPT header is not valid.
*/
int FS__CheckGPTHeader(U8 * pData, unsigned BytesPerSector, U32 SectorIndex, int IsBackup) {
  int r;

  r = _CheckGPTHeader(pData, BytesPerSector, SectorIndex, IsBackup);
  return r;
}

/*********************************************************************
*
*       FS__LoadPartitionInfoGPT
*
*  Function description
*    Returns information about a GPT partition.
*
*  Parameters
*    PartIndex            Index of the partition to query.
*    pPartInfo            [OUT] Information about the partition.
*    pData                [IN] Sector data that stores the partition entry.
*    ldEntriesPerSector   Number of partition entries in a sector as power of 2 exponent.
*    ldSizeOfEntry        Size of a partition entry in bytes as power of 2 exponent.
*
*  Return value
*    ==0    OK, information loaded successfully.
*    !=0    An error occurred.
*/
int FS__LoadPartitionInfoGPT(unsigned PartIndex, FS_PARTITION_INFO_GPT * pPartInfo, const U8 * pData, unsigned ldEntriesPerSector, unsigned ldSizeOfEntry) {
  int r;

  r = _LoadPartitionInfoGPT(PartIndex, pPartInfo, pData, ldEntriesPerSector, ldSizeOfEntry);
  return r;
}

#endif // FS_SUPPORT_GPT

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_CreateMBR
*
*  Function description
*    Partitions the specified volume using a MBR (Master Boot Record)
*    partition scheme.
*
*  Parameters
*    sVolumeName    Name of the volume to be partitioned.
*    pPartInfo      [IN] List of the partitions to be created.
*    NumPartitions  Number of partitions to be created.
*
*  Return value
*    ==0    OK, volume partitioned successfully.
*    !=0    Error code indicating the failure reason.
*
*  Additional information
*    The Master Boot Record is a special logical sector that contains
*    information about how the storage device is partitioned.
*    This partitioning information is located on the first logical
*    sector of a storage device (sector index 0). The MBR information can
*    be queried via FS_GetPartitionInfoMBR().
*    FS_CreateMBR() overwrites any information present in the first
*    logical sector of the specified volume.
*
*    The partition entries are stored in the order specified in
*    the pPartInfo array: the information found in \tt{pPartInfo[0]}
*    is stored to first partition entry, the information found in
*    \tt{pPartInfo[1]} is stored to the second partition entry, and so on.
*
*    If the \tt Type member of the FS_PARTITION_INFO_MBR structure
*    is set to 0 then FS_CreateMBR() automatically calculates the partition
*    type and the CHS (Cylinder/Head/Sector) addresses (\tt Type,
*    \tt StartAddr and \tt EndAddr) based on the values stored
*    in the \tt StartSector and \tt NumSector members.
*
*    The data of the created partitions can be accessed using the DISKPART
*    logical driver.
*/
int FS_CreateMBR(const char * sVolumeName, FS_PARTITION_INFO_MBR * pPartInfo, int NumPartitions) {
  int         r;
  FS_VOLUME * pVolume;

  //
  // Validate parameters.
  //
  if (pPartInfo == NULL) {
    return FS_ERRCODE_INVALID_PARA;         // Error, invalid partition table.
  }
  if ((NumPartitions <= 0) || (NumPartitions > FS_MAX_NUM_PARTITIONS_MBR)) {
    return FS_ERRCODE_INVALID_PARA;         // Error, invalid number of partitions.
  }
  //
  // Perform the operation.
  //
  FS_LOCK();
  r = FS_ERRCODE_VOLUME_NOT_FOUND;          // Error, volume not found.
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    r = FS__CreateMBR(pVolume, pPartInfo, NumPartitions);
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_GetPartitionInfoMBR
*
*  Function description
*    Returns information about a MBR partition.
*
*  Parameters
*    sVolumeName    Name of the volume on which the partition is located.
*    pPartInfo      [OUT] Information about the partition.
*    PartIndex      Index of the partition to query.
*
*  Return value
*    ==0    OK, partition information read.
*    !=0    Error code indicating the failure reason.
*
*  Additional information
*    The function reads the information from the Master Boot Record
*    (MBR) that is stored on the first sector (the sector with the
*    index 0) of the specified volume. An error is returned if no MBR
*    information is present on the volume. If the \tt Type member
*    of the FS_PARTITION_INFO_MBR structure is 0, the partition entry
*    is not valid.
*
*    Permitted values for PartIndex are 0 to 3.
*/
int FS_GetPartitionInfoMBR(const char * sVolumeName, FS_PARTITION_INFO_MBR * pPartInfo, U8 PartIndex) {
  int         r;
  FS_VOLUME * pVolume;

  //
  // Validate parameters.
  //
  if (PartIndex >= (unsigned)FS_MAX_NUM_PARTITIONS_MBR) {
    return FS_ERRCODE_INVALID_PARA;           // Error, invalid partition index.
  }
  if (pPartInfo == NULL) {
    return FS_ERRCODE_INVALID_PARA;         // Error, invalid partition table.
  }
  //
  // Perform the operation.
  //
  FS_LOCK();
  r = FS_ERRCODE_VOLUME_NOT_FOUND;            // Error, the specified volume was not found.
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    r = FS__GetPartitionInfoMBR(pVolume, pPartInfo, PartIndex);
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_GetPartitioningScheme
*
*  Function description
*    Returns information about how a storage device is partitioned.
*
*  Parameters
*    sVolumeName    Name of the volume on which the partition is located.
*
*  Return value
*    >=0    OK, partitioning type. Can be one of the \ref{Partitioning schemes} values.
*    !=0    Error code indicating the failure reason.
*/
int FS_GetPartitioningScheme(const char * sVolumeName) {
  int         r;
  FS_VOLUME * pVolume;

  FS_LOCK();
  r = FS_ERRCODE_VOLUME_NOT_FOUND;            // Error, the specified volume was not found.
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    r = _GetPartitioningScheme(pVolume);
  }
  FS_UNLOCK();
  return r;
}

#if FS_SUPPORT_GPT

/*********************************************************************
*
*       FS_CreateGPT
*
*  Function description
*    Partitions the specified volume using a GPT (GUID Partition Table)
*    partition scheme.
*
*  Parameters
*    sVolumeName    Name of the volume to be partitioned.
*    pGPTInfo       [IN] Information about the GPT partitioning. It cannot be NULL.
*    pPartInfo      [IN] List of the partitions to be created. It cannot be NULL.
*    NumPartitions  Number of partitions to be created. It cannot be 0 or negative.
*
*  Return value
*    ==0    OK, volume partitioned successfully.
*    !=0    Error code indicating the failure reason.
*
*  Additional information
*    The partition information is stored starting with the first
*    logical sector of the storage device. The number of logical sectors
*    occupied by the partitioning information depends on the number
*    of partitions created. The partitioning information requires
*    at least three logical sectors that is one logical sector
*    for the protective MBR, one logical sector for the GPT header
*    and one logical sector for the partition table. In addition,
*    a copy of the GPT header and of the partition table is stored
*    for redundancy purposes at the end of the storage device.
*    For this reason, FS_CreateGPT() overwrites any information
*    present in these logical sectors of the specified volume.
*
*    The partition entries are stored in the order specified
*    in the pPartInfo array: the information found in \tt{pPartInfo[0]}
*    is stored to first partition entry, the information found
*    in \tt{pPartInfo[1]} is stored to the second partition entry,
*    and so on.
*
*    The actual number of created partition entries is calculated
*    as the maximum of NumPartitions and \tt{pGPTInfo::NumPartitions}
*    If \tt{pGPTInfo::NumPartitions} is greater than NumPartitions
*    then FS_CreateGPT() creates empty partitions for partition indexes
*    greater than or equal to NumPartitions. 
*
*    FS_CreateGPT() calculates the values of \tt{pGPTInfo::StartSector} 
*    and \tt{pGPTInfo::NumSectors} based on the capacity of the storage 
*    device if these members are set to 0.
*    If \tt{pPartInfo::StartSector} is set to 0 then FS_CreateGPT()
*    set is to the next available sector that immediately follows
*    the previous partition entry. \tt{pPartInfo::NumSectors} can
*    be set to 0 only for the last partition in the list in which
*    case the last partition occupies the remaining free space
*    on the storage device. For any other partitions
*    \tt{pPartInfo::NumSectors} must be different than 0.
*
*    FS_CreateGPT() checks the validity of values in pGPTInfo and pPartInfo.
*    Any misconfiguration such as overlapping partitions is reported as an 
*    error and the partition table is not created.
*
*    The partitioning information such as the disk id and the
*    number of partitions can be queried via FS_GetGPTInfo().
*    The information about individual partitions can be obtained
*    via FS_GetPartitionInfoGPT().
*
*    The DISKPART logical driver can be used to access
*    the data of the created partitions.
*/
int FS_CreateGPT(const char * sVolumeName, FS_GPT_INFO * pGPTInfo, FS_PARTITION_INFO_GPT * pPartInfo, int NumPartitions) {
  int         r;
  FS_VOLUME * pVolume;

  //
  // Validate parameters.
  //
  if (pGPTInfo == NULL) {
    return FS_ERRCODE_INVALID_PARA;         // Error, invalid partitioning information.
  }
  if (pPartInfo == NULL) {
    return FS_ERRCODE_INVALID_PARA;         // Error, invalid partition table.
  }
  if ((NumPartitions <= 0) || (NumPartitions > FS_MAX_NUM_PARTITIONS_GPT)) {
    return FS_ERRCODE_INVALID_PARA;         // Error, invalid number of partitions.
  }
  //
  // Perform the operation.
  //
  FS_LOCK();
  r = FS_ERRCODE_VOLUME_NOT_FOUND;          // Error, volume not found.
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    r = _CreateGPT(pVolume, pGPTInfo, pPartInfo, NumPartitions);
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_GetPartitionInfoGPT
*
*  Function description
*    Returns information about a GPT partition.
*
*  Parameters
*    sVolumeName    Name of the volume on which the partition is located.
*    pPartInfo      [OUT] Information about the partition.
*    PartIndex      Index of the partition to query.
*
*  Return value
*    ==0    OK, partition information read.
*    !=0    Error code indicating the failure reason.
*/
int FS_GetPartitionInfoGPT(const char * sVolumeName, FS_PARTITION_INFO_GPT * pPartInfo, unsigned PartIndex) {
  int         r;
  FS_VOLUME * pVolume;

  //
  // Validate parameters.
  //
  if (pPartInfo == NULL) {
    return FS_ERRCODE_INVALID_PARA;           // Error, invalid partition table.
  }
  if (PartIndex >= (unsigned)FS_MAX_NUM_PARTITIONS_GPT) {
    return FS_ERRCODE_INVALID_PARA;           // Error, invalid partition index.
  }
  //
  // Perform the operation.
  //
  FS_LOCK();
  r = FS_ERRCODE_VOLUME_NOT_FOUND;            // Error, the specified volume was not found.
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    r = _GetPartitionInfoGPT(pVolume, pPartInfo, PartIndex);
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_GetGPTInfo
*
*  Function description
*    Returns information about the GPT partitioning.
*
*  Parameters
*    sVolumeName    Name of the volume on which the partition is located.
*    pGPTInfo       [OUT] Information about the GPT partitioning.
*
*  Return value
*    ==0    OK, partition information read.
*    !=0    Error code indicating the failure reason.
*/
int FS_GetGPTInfo(const char * sVolumeName, FS_GPT_INFO * pGPTInfo) {
  int         r;
  FS_VOLUME * pVolume;

  //
  // Validate parameters.
  //
  if (pGPTInfo == NULL) {
    return FS_ERRCODE_INVALID_PARA;           // Error, invalid partitioning information.
  }
  //
  // Perform the operation.
  //
  FS_LOCK();
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    r = _GetGPTInfo(pVolume, pGPTInfo);
  } else {
    r = FS_ERRCODE_VOLUME_NOT_FOUND;          // Error, the specified volume was not found.
  }
  FS_UNLOCK();
  return r;
}

#endif // FS_SUPPORT_GPT

/*************************** End of file ****************************/

