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
File    : FS_NOR_PHY_CFI.c
Purpose : Low level flash driver for x16 bit CFI compliant flash chips
Literature:
[1] Intel's "Common Flash Interface (CFI) and Command Sets"
    Application Note 646, April 2000

[2] Spansion's "Common Flash Interface Version 1.4 Vendor Specific Extensions"
    Revision A, Amendment 0, March 22 - 2004

Additional information:
  Comments in this file refer to Intel's "Common Flash Interface
  (CFI) and Command Sets" document as the "CFI spec."

  Supported devices:
  Any CFI-compliant flash in 16-bit mode
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#include "FS_Int.h"
#include "FS_NOR_Int.h"

/*********************************************************************
*
*       Defines, fixed
*
**********************************************************************
*/

/*********************************************************************
*
*       Algo types.
*       These values are defined by hardware. DO NOT CHANGE!
*/
#define ALGO_TYPE_INTEL_EXT       0x0001u
#define ALGO_TYPE_AMD_STD         0x0002u
#define ALGO_TYPE_INTEL_STD       0x0003u
#define ALGO_TYPE_AMD_EXT         0x0004u
#define ALGO_TYPE_SST             0x0701u

/*********************************************************************
*
*       CFI_OFF_...: Offset of parameters in CFI string
*/
#define CFI_OFF_NUMBLOCKS         0x2Cu   // Offset to the number of erase block regions
#define CFI_OFF_SECTORINFO        0x2Du   // Offset to the first erase block region information

/*********************************************************************
*
*       ASSERT_UNIT_NO_IS_IN_RANGE
*/
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL)
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)                                                                                      \
    if ((Unit) >= (unsigned)FS_NOR_NUM_UNITS) {                                                                                 \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_CFI: Invalid unit number (%d not in [0, %d]).", (int)(Unit), FS_NOR_NUM_UNITS)); \
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);                                                                                      \
    }
#else
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)
#endif

/*********************************************************************
*
*       ASSERT_SECTOR_INDEX_IS_IN_RANGE
*/
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL)
  #define ASSERT_SECTOR_INDEX_IS_IN_RANGE(pInst, SectorIndex)                                                                                   \
    if ((SectorIndex) >= (pInst)->NumSectorsTotal) {                                                                                            \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_CFI: Invalid sector index (%lu not in [0, %lu]).", SectorIndex, (pInst)->NumSectorsTotal - 1u)); \
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);                                                                                                      \
    }
#else
  #define ASSERT_SECTOR_INDEX_IS_IN_RANGE(pInst, SectorIndex)
#endif

/*********************************************************************
*
*       ASSERT_HW_TYPE_IS_SET
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_HW_TYPE_IS_SET(pInst)                                     \
    if ((pInst)->pProgramHW == NULL) {                                     \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR_CFI: HW layer not set.")); \
      FS_X_PANIC(FS_ERRCODE_HW_LAYER_NOT_SET);                             \
    }
#else
  #define ASSERT_HW_TYPE_IS_SET(pInst)
#endif

/*********************************************************************
*
*      Local types
*
**********************************************************************
*/

/*********************************************************************
*
*      SECTOR_BLOCK
*
*  A Sector block defines a number of adjacent sectors of the same size.
*  It therefor contains the size (of one sector) and number of sectors in the block.
*  The entire sectorization can be described with just a few sector blocks.
*/
typedef struct {
  U32 SectorSize;
  U32 NumSectors;
} SECTOR_BLOCK;

typedef struct {
  U8                         Unit;
  U32                        BaseAddr;
  U32                        StartAddrConf;        // Configured start address
  U32                        StartAddrUsed;        // Start addr. actually used (aligned to start of a sector)
  U32                        NumBytes;
  SECTOR_BLOCK               aSectorUsed[FS_NOR_MAX_SECTOR_BLOCKS];
  U16                        NumSectorBlocksUsed;
  U16                        NumSectorsTotal;
  U8                         IsInited;
  U8                         NumChips;
  FS_NOR_READ_CFI_CALLBACK * pReadCFI;
  const FS_NOR_PROGRAM_HW  * pProgramHW;
  U8                         MultiBytesAtOnce;
  U32                        GapStartAddr;
  U32                        GapNumBytes;
} NOR_CFI_INST;

/*********************************************************************
*
*      Static data
*
**********************************************************************
*/
static NOR_CFI_INST * _apInst[FS_NOR_NUM_UNITS];

/*********************************************************************
*
*      Static code
*
**********************************************************************
*/

/*********************************************************************
*
*      _CFILoad_U16
*
*  Function description
*    Loads a 16-bit value from a CFI string. The 16 bit value is
*    stored in little endian format.
*/
static U16 _CFILoad_U16(const U8 * pBuffer) {
  U16 r;

  r  = *pBuffer;
  r |= (U16)pBuffer[1] << 8;
  return r;
}

/*********************************************************************
*
*      _LoadSectorSize
*
*  Function description
*    Read the sector size from the given location.
*/
static U32 _LoadSectorSize(const U8* pData) {
  U32 Size;

  Size = _CFILoad_U16(pData);
  if (Size == 0u) {
    return 128;       /* CFI spec. p. 9 says a value of 0 means 128-byte block size. */
  } else {
    return Size << 8; /* Size != 0 means we have to multiply with 256 */
  }
}

/*********************************************************************
*
*      _Init
*
*  Function description
*    This routine checks if the device is a valid CFI device.
*    If device is CFI compliant, the routine sets and fills the
*    information required by the erase and write routines.
*    CFI information are dynamically read as necessary.
*
*  Notes
*    (1)      Acc. to [2], AMD   compatible flashes have the value 0x40 at off 0x15.
*             Acc. to [1], Intel compatible Flashes have the value 0x50.
*/
static void _Init(NOR_CFI_INST * pInst) {
  U8       Off;
  U8       NumBlocks;
  unsigned i;
  U8       ReverseSectorBlocks;
  U16      NumSectors;
  U32      SectorSize;
  U8       aInfo[16];
  unsigned NumBlocksUsed;
  I32      NumBytesToSkip;
  I32      NumBytesRem;
  U32      NumBytesSkipped;
  U16      NumSectorsTotal;
  U32      BaseAddr;
  U8       MultiByteWrite;
  U16      AlgoType;
  U8       Unit;

  Unit                = pInst->Unit;
  NumBlocksUsed       = 0;
  NumBytesSkipped     = 0;
  NumSectorsTotal     = 0;
  ReverseSectorBlocks = 0;
  BaseAddr            = pInst->BaseAddr;
  //
  // Read 0x10 - 0x16, containing ID "QRY" @10-12, Primary command set @13-14 and address of Primary Extended Table @15-16
  //
  pInst->pReadCFI(Unit, BaseAddr, 0x10, &aInfo[0] , 7);
  //
  // Check if it is a CFI compatible device
  //
  if (   (aInfo[0] == (U8)'Q')
      && (aInfo[1] == (U8)'R')
      && (aInfo[2] == (U8)'Y')) {
    //
    // Read Algo type of used flash
    //
    AlgoType = _CFILoad_U16(&aInfo[3]);      // 1: INTEL, 2: AMD --> [1],[2]
    //
    // Determine flash algorithm
    //
    if (pInst->NumChips == 1u) {
      if (AlgoType == ALGO_TYPE_INTEL_STD) {
        pInst->pProgramHW = &FS_NOR_Program_Intel_1x16;
      } else if (AlgoType == ALGO_TYPE_INTEL_EXT) {
        pInst->pProgramHW = &FS_NOR_Program_IntelFast_1x16;
      } else if (AlgoType == ALGO_TYPE_AMD_STD) {
        pInst->pProgramHW = &FS_NOR_Program_AMD_1x16;
      } else if (AlgoType == ALGO_TYPE_AMD_EXT) {
        pInst->pProgramHW = &FS_NOR_Program_AMD_1x16;
      } else if (AlgoType == ALGO_TYPE_SST) {
       //
       // SST flashes are not fully CFI compliant.
       // The algo type 0x0701 that is returned is not listed in the CFI spec.
       // The algo type would be according to the CFI specification vendor specific.
       // Datasheets of all SST39x say that there are AMD command compliant.
       //
        pInst->pProgramHW = &FS_NOR_Program_AMD_1x16;
      } else {
        pInst->pProgramHW = NULL;           // Unknown programming algorithm.
      }
    } else if (pInst->NumChips == 2u) {
      if ((AlgoType == ALGO_TYPE_INTEL_STD) || (AlgoType == ALGO_TYPE_INTEL_EXT)) {
        pInst->pProgramHW = &FS_NOR_Program_Intel_2x16;
      } else if ((AlgoType == ALGO_TYPE_AMD_STD) || (AlgoType == ALGO_TYPE_AMD_EXT)) {
        pInst->pProgramHW = &FS_NOR_Program_AMD_2x16;
      } else {
        pInst->pProgramHW = NULL;           // Unknown programming algorithm.
      }
    } else {
      pInst->pProgramHW = NULL;             // Unknown programming algorithm.
    }
    if (pInst->pProgramHW == NULL) {
      FS_DEBUG_WARN((FS_MTYPE_DRIVER, "NOR_CFI: Algorithm %d is either not supported or not allowed.", AlgoType));
    }
    //
    // AMD specific parameter table ? (Note 1)
    //
    if (_CFILoad_U16(&aInfo[5]) == 0x40u) {
      //
      // Reverse blocks if "Boot Block Flag" info tells us to do so.
      //
      pInst->pReadCFI(Unit, BaseAddr, 0x4F, &aInfo[0], 1);
      if (aInfo[0] == 3u) {
        ReverseSectorBlocks = 1;
      }
    }
    //
    // Check if write to buffer command is supported
    //
    pInst->pReadCFI(Unit, BaseAddr, 0x2A, &MultiByteWrite, 1);
    if (MultiByteWrite >= 5u) {
      if (pInst->pProgramHW == &FS_NOR_Program_AMD_1x16) {
        pInst->pProgramHW = &FS_NOR_Program_AMDFast_1x16;
        pInst->MultiBytesAtOnce = (U8)(1u << MultiByteWrite);
      } else {
        if (pInst->pProgramHW == &FS_NOR_Program_AMD_2x16) {
          pInst->pProgramHW = &FS_NOR_Program_AMDFast_2x16;
          pInst->MultiBytesAtOnce = (U8)(1u << MultiByteWrite);
        }
      }
    }
    //
    // Read number of sector blocks
    //
    pInst->pReadCFI(Unit, BaseAddr, CFI_OFF_NUMBLOCKS, &aInfo[0], 1);
    NumBlocks = aInfo[0];
    if (NumBlocks > (U8)FS_NOR_MAX_SECTOR_BLOCKS) {
      NumBlocks = FS_NOR_MAX_SECTOR_BLOCKS;
    }
    //
    // Read physical sector block information and add it to the list of used blocks
    //
    NumBytesToSkip = (I32)pInst->StartAddrConf - (I32)pInst->BaseAddr;
    NumBytesRem    = (I32)pInst->NumBytes;
    for (i = 0; i < NumBlocks; i++) {
      unsigned j;

      //
      // Swap sector blocks if device is a top boot device
      //
      if (ReverseSectorBlocks != 0u) {
        j = (NumBlocks - i) - 1u;
      } else {
        j = i;
      }
      Off        = (U8)(CFI_OFF_SECTORINFO + (j << 2));
      pInst->pReadCFI(Unit, BaseAddr, Off, &aInfo[0] , 4);
      NumSectors = _CFILoad_U16(&aInfo[0]) + 1u;
      SectorSize = _LoadSectorSize(&aInfo[2]) * pInst->NumChips;
      //
      // Take care of bytes to skip before data area
      //
      while ((NumSectors != 0u) && (NumBytesToSkip > 0)) {
        NumBytesToSkip  -= (I32)SectorSize;
        NumBytesSkipped += SectorSize;
        NumSectors--;
      }
      if (NumSectors != 0u) {
        U16 NumSectorsRem;

        NumSectorsRem = (U16) ((U32)NumBytesRem / SectorSize);
        if (NumSectors > NumSectorsRem) {
          NumSectors = NumSectorsRem;
          NumBytesRem = 0;      // No more sectors after this to make sure that sectors are adjacent!
        } else {
          NumBytesRem -= (I32)NumSectors * (I32)SectorSize;
        }
        //
        // Take care of bytes to skip after data area
        //
        if (NumSectors != 0u) {
          if (NumBlocksUsed == 0u) {
            pInst->StartAddrUsed = pInst->BaseAddr + NumBytesSkipped;       // Remember addr. of first sector used
          }
          pInst->aSectorUsed[NumBlocksUsed].SectorSize = SectorSize;
          pInst->aSectorUsed[NumBlocksUsed].NumSectors = NumSectors;
          NumBlocksUsed++;
          NumSectorsTotal += NumSectors;
        }
      }
    }
    pInst->NumSectorBlocksUsed = (U16)NumBlocksUsed;
    pInst->NumSectorsTotal     = NumSectorsTotal;
    pInst->IsInited  = 1;
    //
    // Perform a sanity check.
    //
    if (NumBlocksUsed == 0u) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_CFI: Flash size too small for the configuration."));
    }
  }
}

/*********************************************************************
*
*      _GetSectorOff
*/
static U32 _GetSectorOff(const NOR_CFI_INST * pInst, unsigned SectorIndex) {
  unsigned i;
  unsigned NumBlocks;
  unsigned NumSectors;
  U32      Off;

  NumBlocks = pInst->NumSectorBlocksUsed;
  Off = 0;
  for (i = 0; i < NumBlocks; i++) {
    NumSectors = pInst->aSectorUsed[i].NumSectors;
    if (SectorIndex < NumSectors) {
      NumSectors = SectorIndex;
    }
    Off += NumSectors * pInst->aSectorUsed[i].SectorSize;
    SectorIndex -= NumSectors;   // Number of remaining sectors
  }
  return Off;
}

/*********************************************************************
*
*      _GetSectorSize
*/
static U32 _GetSectorSize(const NOR_CFI_INST * pInst, unsigned SectorIndex) {
  unsigned i;
  unsigned NumBlocks;
  unsigned NumSectors;

  NumBlocks = pInst->NumSectorBlocksUsed;
  for (i = 0; i < NumBlocks; i++) {
    NumSectors = pInst->aSectorUsed[i].NumSectors;
    if (SectorIndex < NumSectors) {
      return pInst->aSectorUsed[i].SectorSize;
    }
    SectorIndex -= NumSectors;   // Number of remaining sectors
  }
  return 0;                      // SectorIndex was out of bounds
}

/*********************************************************************
*
*      _ShiftAddrIfRequired
*/
static U32 _ShiftAddrIfRequired(const NOR_CFI_INST * pInst, U32 Addr) {
  U32 GapStartAddr;
  U32 GapNumBytes;
  U32 BaseAddr;

  BaseAddr     = pInst->BaseAddr;
  GapNumBytes  = pInst->GapNumBytes;
  GapStartAddr = pInst->GapStartAddr;
  if (GapNumBytes != 0u) {
    if ((BaseAddr < GapStartAddr) && (Addr >= GapStartAddr)) {
      Addr += GapNumBytes;
    }
  }
  return Addr;
}

/*********************************************************************
*
*      _WriteData
*/
static int _WriteData(const NOR_CFI_INST * pInst, U32 SectorAddr, U32 DestAddr, const void FS_NOR_FAR * pSrc, unsigned NumItems) {
  int r;
  U32 BaseAddr;
  U8  Unit;

  ASSERT_HW_TYPE_IS_SET(pInst);
  Unit = pInst->Unit;
  BaseAddr   = pInst->BaseAddr;
  SectorAddr = _ShiftAddrIfRequired(pInst, SectorAddr);
  DestAddr   = _ShiftAddrIfRequired(pInst, DestAddr);
  r = pInst->pProgramHW->pfWrite(Unit, BaseAddr, SectorAddr, DestAddr, SEGGER_CONSTPTR2PTR(const U16 FS_NOR_FAR, pSrc), NumItems);
  return r;
}

/*********************************************************************
*
*      _ReadData
*/
static int _ReadData(const NOR_CFI_INST * pInst, void * pDest, U32 SrcAddr, U32 NumBytes) {
  int r;
  U8  Unit;

  ASSERT_HW_TYPE_IS_SET(pInst);
  Unit = pInst->Unit;
  SrcAddr = _ShiftAddrIfRequired(pInst, SrcAddr);
  r = pInst->pProgramHW->pfRead(Unit, pDest, SrcAddr, NumBytes);
  return r;
}

/*********************************************************************
*
*      _AllocInstIfRequired
*/
static NOR_CFI_INST * _AllocInstIfRequired(U8 Unit) {
  NOR_CFI_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  pInst = NULL;
  if (Unit < (U8)FS_NOR_NUM_UNITS) {
    pInst = _apInst[Unit];
    if (pInst == NULL) {
      FS_ALLOC_ZEROED_PTR(SEGGER_PTR2PTR(void *, &pInst), (I32)sizeof(NOR_CFI_INST), "NOR_CFI_INST");
      if (pInst != NULL) {
        pInst->Unit = Unit;
        _apInst[Unit] = pInst;
      }
    }
  }
  return pInst;
}

/*********************************************************************
*
*      _GetInst
*/
static NOR_CFI_INST * _GetInst(U8 Unit) {
  NOR_CFI_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  pInst = NULL;
  if (Unit < (U8)FS_NOR_NUM_UNITS) {
    pInst = _apInst[Unit];
  }
  return pInst;
}

/*********************************************************************
*
*      Static code (public via callback)
*
**********************************************************************
*/

/*********************************************************************
*
*       _PHY_GetNumSectors
*
*  Function description
*    Returns the number of flash sectors.
*/
static int _PHY_GetNumSectors(U8 Unit) {
  int            r;
  NOR_CFI_INST * pInst;

  r     = 0;
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    r = (int)pInst->NumSectorsTotal;
  }
  return r;
}

/*********************************************************************
*
*       _PHY_GetSectorInfo
*
*  Function description
*    Returns the offset and length of the given sector.
*/
static void _PHY_GetSectorInfo(U8 Unit, unsigned SectorIndex, U32 * pOff, U32 * pLen) {
  U32            SectorOff;
  U32            SectorSize;
  NOR_CFI_INST * pInst;

  SectorOff  = 0;
  SectorSize = 0;
  pInst      = _GetInst(Unit);
  if (pInst != NULL) {
    //
    // Make sure SectorIndex is in range
    //
    ASSERT_SECTOR_INDEX_IS_IN_RANGE(pInst, SectorIndex);
    //
    // Compute result.
    //
    SectorOff  = _GetSectorOff(pInst, SectorIndex);
    SectorSize = _GetSectorSize(pInst, SectorIndex);
  }
  if (pOff != NULL) {
    *pOff = SectorOff;
  }
  if (pLen != NULL) {
    *pLen = SectorSize;
  }
}

/*********************************************************************
*
*      _PHY_WriteOff1x16
*
*  Function description
*    This routine writes data into any section of the flash of one
*    parallel NOR flash device connected via a 16-bit bus.
*
*  Return value
*    ==0    O.K., data has been written
*    !=0    An error occurred
*
*  Additional information
*    This routine does not check if this section has been previously
*    erased. It is in the responsibility of the NOR driver to
*    make sure that the section is erased. Data written into multiple
*    physical sectors at a time can be handled by this routine.
*
*  Notes
*    (1) The 3rd parameter should actually be the sector address.
*        This parameter is required only for AMD compliant CFI
*        NOR flash devices which support the "fast write" mode.
*        Computing the sector address is time consuming for NOR
*        flash devices with a large number of physical sectors.
*        To improve the performance we pass here the address of
*        the first byte to be written, which should be OK according
*        to AMD specification.
*/
static int _PHY_WriteOff1x16(U8 Unit, U32 Off, const void * pData, U32 NumBytes) {
  unsigned       NumItems;
  U32            Addr;
  NOR_CFI_INST * pInst;
  U16            DataRead;
  U16            DataToWrite;
  U8           * pDataToWrite;
  const U8     * pData8;
  unsigned       NumBytesAtOnce;
  int            r;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;                                                 // Error, invalid instance.
  }
  if (NumBytes == 0u) {
    return 0;                                                 // OK, nothing to do.
  }
  Addr   = pInst->StartAddrUsed + Off;
  pData8 = SEGGER_CONSTPTR2PTR(const U8, pData);
  //
  // Write leading not aligned bytes.
  //
  if ((Addr & 1u) != 0u) {
    Addr &= ~1u;
    r = _ReadData(pInst, &DataRead, Addr, 2u);
    if (r == 0) {
      pDataToWrite = SEGGER_PTR2PTR(U8, &DataToWrite);
      *pDataToWrite++  = 0xFFu;
      *pDataToWrite    = *pData8++;
      DataToWrite     &= DataRead;                            // Turn bits to 0.
      r = _WriteData(pInst, Addr, Addr, &DataToWrite, 1u);    // Note 1
      if (r != 0) {
        return 1;                                             // Error, write operation failed.
      }
    }
    Addr += 2u;                                               // We wrote 2 bytes.
    --NumBytes;
  }
  //
  // Write 16-bit items at a time if possible.
  //
  NumItems = NumBytes >> 1;                                   // We write 2 bytes at a time.
  if (NumItems != 0u) {
    if ((SEGGER_PTR2ADDR(pData8) & 1u) == 0u) {               // Is the source address 32-bit aligned?
      NumBytesAtOnce = NumItems << 1;                         // We write 2 bytes at a time.
      r = _WriteData(pInst, Addr, Addr, pData8, NumItems);
      if (r != 0) {
        return 1;                                             // Error, write operation failed.
      }
      NumBytes -= NumBytesAtOnce;
      Addr     += NumBytesAtOnce;
      pData8   += NumBytesAtOnce;
    } else {
      NumBytesAtOnce = 2;
      do {
        pDataToWrite = SEGGER_PTR2PTR(U8, &DataToWrite);
        *pDataToWrite++ = *pData8++;
        *pDataToWrite++ = *pData8++;
        r = _WriteData(pInst, Addr, Addr, &DataToWrite, 1u);  // Note 1
        if (r != 0) {
          return 1;                                           // Error, write operation failed.
        }
        NumBytes -= NumBytesAtOnce;
        Addr     += NumBytesAtOnce;
      } while (--NumItems != 0u);
    }
  }
  //
  // Write trailing not aligned byte.
  //
  if (NumBytes != 0u) {
    r = _ReadData(pInst, &DataRead, Addr, 2u);
    if (r == 0) {
      pDataToWrite = SEGGER_PTR2PTR(U8, &DataToWrite);
      *pDataToWrite++  = *pData8++;
      *pDataToWrite    = 0xFFu;
      DataToWrite     &= DataRead;                            // Turn bits to 0.
      r = _WriteData(pInst, Addr, Addr, &DataToWrite, 1u);    // Note 1
      if (r != 0) {
        return 1;                                             // Error, write operation failed.
      }
    }
  }
  return 0;                                                   // OK, data written.
}

/*********************************************************************
*
*      _PHY_WriteOff2x16
*
*  Function description
*    This routine writes data into any section of the flash
*    for a configuration with 2 NOR flash devices connected
*    in parallel via 2 16-bit data buses.
*
*  Return value
*    ==0    O.K., data has been written
*    !=0    An error occurred
*
*  Additional information
*    This routine does not check if this section has been previously
*    erased. It is in the responsibility of the NOR driver to
*    make sure that the section is erased. Data written into multiple
*    physical sectors at a time can be handled by this routine.
*
*  Notes
*    (1) The 3rd parameter should actually be the sector address.
*        This parameter is required only for AMD compliant CFI
*        NOR flash devices which support the "fast write" mode.
*        Computing the sector address is time consuming for NOR
*        flash devices with a large number of physical sectors.
*        To improve the performance we pass here the address of
*        the first byte to be written, which should be OK according
*        to AMD specification.
*    (2) The last argument has to be the number of 16-bit items.
*/
static int _PHY_WriteOff2x16(U8 Unit, U32 Off, const void * pData, U32 NumBytes) {
  unsigned       NumItems;
  U32            Addr;
  NOR_CFI_INST * pInst;
  U32            DataRead;
  U32            DataToWrite;
  U8           * pDataToWrite;
  const U8     * pData8;
  unsigned       NumBytesAtOnce;
  int            r;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;                                                 // Error, invalid instance.
  }
  if (NumBytes == 0u) {
    return 0;                                                 // OK, nothing to do.
  }
  Addr   = pInst->StartAddrUsed + Off;
  pData8 = SEGGER_CONSTPTR2PTR(const U8, pData);
  //
  // Write leading not aligned bytes.
  //
  if ((Addr & 3u) != 0u) {
    NumBytesAtOnce = 4u - (Addr & 3u);
    Addr &= ~3u;
    r = _ReadData(pInst, &DataRead, Addr, 4u);
    if (r == 0) {
      DataToWrite = 0xFFFFFFFFuL;
      pDataToWrite = SEGGER_PTR2PTR(U8, &DataToWrite);
      if (NumBytesAtOnce == 1u) {
        *pDataToWrite++   = 0xFFu;
        *pDataToWrite++   = 0xFFu;
        *pDataToWrite++   = 0xFFu;
        *pDataToWrite     = *pData8++;
      } else if (NumBytesAtOnce == 2u) {
        *pDataToWrite++   = 0xFFu;
        *pDataToWrite++   = 0xFFu;
        *pDataToWrite++   = *pData8++;
        if (NumBytes > 1u) {
          *pDataToWrite   = *pData8++;
        }
      } else if (NumBytesAtOnce == 3u) {
        *pDataToWrite++   = 0xFFu;
        *pDataToWrite++   = *pData8++;
        if (NumBytes > 1u) {
          *pDataToWrite++ = *pData8++;
          if (NumBytes > 2u) {
            *pDataToWrite = *pData8++;
          }
        }
      } else {
        //
        // Not reachable.
        //
      }
      DataToWrite &= DataRead;                                // Turn bits to 0.
      r = _WriteData(pInst, Addr, Addr, &DataToWrite, 2u);    // Note 1, 2
      if (r != 0) {
        return 1;                                             // Error, write operation failed.
      }
    }
    Addr           += 4u;                                     // We wrote 4 bytes.
    NumBytesAtOnce  = SEGGER_MIN(NumBytesAtOnce, NumBytes);
    NumBytes       -= NumBytesAtOnce;
  }
  //
  // Write 16-bit items at a time if possible.
  //
  NumItems = NumBytes >> 2;                                   // We write 4 bytes at a time.
  if (NumItems != 0u) {
    if ((SEGGER_PTR2ADDR(pData8) & 3u) == 0u) {               // Is the source address 32-bit aligned?
      NumBytesAtOnce = NumItems << 2;                         // We write 4 bytes at a time.
      NumItems <<= 1;                                         // _WriteData() expects the number of 16-bit items.
      r = _WriteData(pInst, Addr, Addr, pData8, NumItems);
      if (r != 0) {
        return 1;                                             // Error, write operation failed.
      }
      NumBytes -= NumBytesAtOnce;
      Addr     += NumBytesAtOnce;
      pData8   += NumBytesAtOnce;
    } else {
      NumBytesAtOnce = 4;
      do {
        pDataToWrite = SEGGER_PTR2PTR(U8, &DataToWrite);
        *pDataToWrite++ = *pData8++;
        *pDataToWrite++ = *pData8++;
        *pDataToWrite++ = *pData8++;
        *pDataToWrite   = *pData8++;
        r = _WriteData(pInst, Addr, Addr, &DataToWrite, 2u);  // Note 1, 2
        if (r != 0) {
          return 1;                                           // Error, write operation failed.
        }
        NumBytes -= NumBytesAtOnce;
        Addr     += NumBytesAtOnce;
      } while (--NumItems != 0u);
    }
  }
  //
  // Write trailing not aligned byte.
  //
  if (NumBytes != 0u) {
    r = _ReadData(pInst, &DataRead, Addr, 4u);
    if (r == 0) {
      pDataToWrite = SEGGER_PTR2PTR(U8, &DataToWrite);
      if (NumBytes == 1u) {
        *pDataToWrite++ = *pData8++;
        *pDataToWrite++ = 0xFFu;
        *pDataToWrite++ = 0xFFu;
        *pDataToWrite   = 0xFFu;
      } else if (NumBytes == 2u) {
        *pDataToWrite++ = *pData8++;
        *pDataToWrite++ = *pData8++;
        *pDataToWrite++ = 0xFFu;
        *pDataToWrite   = 0xFFu;
      } else if (NumBytes == 3u) {
        *pDataToWrite++ = *pData8++;
        *pDataToWrite++ = *pData8++;
        *pDataToWrite++ = *pData8++;
        *pDataToWrite   = 0xFFu;
      } else {
        DataToWrite = 0xFFFFFFFFuL;                           // Not reachable.
      }
      DataToWrite &= DataRead;                                // Turn bits to 0.
      r = _WriteData(pInst, Addr, Addr, &DataToWrite, 2u);    // Note 1, 2
      if (r != 0) {
        return 1;                                             // Error, write operation failed.
      }
    }
  }
  return 0;                                                   // OK, data written.
}

/*********************************************************************
*
*       _PHY_ReadOff
*
*  Function description
*    Reads data from the given offset of the flash.
*/
static int _PHY_ReadOff(U8 Unit, void * pDest, U32 Off, U32 NumBytes) {
  int            r;
  U32            SrcAddr;
  NOR_CFI_INST * pInst;

  r     = 1;    // Set to indicate an error,
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    SrcAddr = pInst->StartAddrUsed + Off;
    r = _ReadData(pInst, pDest, SrcAddr, NumBytes);
  }
  return r;
}

/*********************************************************************
*
*      _PHY_EraseSector
*
*  Function description
*    Erases one sector.
*
*  Return value
*    ==0    O.K., sector is erased
*    !=0    Error, sector may not be erased
*/
static int _PHY_EraseSector(U8 Unit, unsigned SectorIndex) {
  int            r;
  U32            Off;
  U32            SectorAddr;
  U32            BaseAddr;
  NOR_CFI_INST * pInst;

  r     = 1;                // Set to indicate an error.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    ASSERT_HW_TYPE_IS_SET(pInst);
    ASSERT_SECTOR_INDEX_IS_IN_RANGE(pInst, SectorIndex);
    Off        = _GetSectorOff(pInst, SectorIndex);
    SectorAddr = pInst->StartAddrUsed + Off;
    BaseAddr   = pInst->BaseAddr;
    SectorAddr = _ShiftAddrIfRequired(pInst, SectorAddr);
    r = pInst->pProgramHW->pfEraseSector(Unit, BaseAddr, SectorAddr);
  }
  return r;
}

/*********************************************************************
*
*       _PHY_Configure
*
*  Function description
*    Configures a single instance of the driver
*/
static void _PHY_Configure(U8 Unit, U32 BaseAddr, U32 StartAddr, U32 NumBytes) {
  NOR_CFI_INST * pInst;

  FS_DEBUG_ASSERT(FS_MTYPE_DRIVER, StartAddr >= BaseAddr);
  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->BaseAddr      = BaseAddr;
    pInst->StartAddrConf = StartAddr;
    pInst->NumBytes      = NumBytes;
    _Init(pInst);
  }
}

/*********************************************************************
*
*       _OnSelectPhy
*
*  Function description
*    Called right after selection of the physical layer
*/
static void _OnSelectPhy(U8 Unit, int NumChips, FS_NOR_READ_CFI_CALLBACK * pReadCFI) {
  NOR_CFI_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->NumChips = (U8)NumChips;
    if (pInst->pReadCFI == NULL) {
      pInst->pReadCFI   = pReadCFI;
    }
  }
}

/*********************************************************************
*
*       _PHY_OnSelectPhy1x16
*
*  Function description
*    Called right after selection of the single chip, 16-bit mode (1x16) physical layer
*/
static void _PHY_OnSelectPhy1x16(U8 Unit) {
  _OnSelectPhy(Unit, 1, FS_NOR_CFI_ReadCFI_1x16);
}

/*********************************************************************
*
*       _PHY_OnSelectPhy2x16
*
*  Function description
*    Called right after selection of the dual chip, 16-bit mode (2x16) physical layer
*/
static void _PHY_OnSelectPhy2x16(U8 Unit) {
  _OnSelectPhy(Unit, 2, FS_NOR_CFI_ReadCFI_2x16);
}

/*********************************************************************
*
*       _PHY_DeInit
*
*  Function description
*    This function deinitialize or frees up memory resources that
*    are no longer needed.
*
*  Parameters
*    Unit   Physical unit.
*/
static void _PHY_DeInit(U8 Unit) {
  FS_USE_PARA(Unit);
#if FS_SUPPORT_DEINIT
  FS_FREE(_apInst[Unit]);
  _apInst[Unit] = NULL;
#endif
}

/*********************************************************************
*
*      Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_NOR_CFI_SetReadCFICallback
*
*  Function description
*    Registers a read function for the CFI parameters.
*
*  Parameters
*    Unit       Index of the physical layer (0-based)
*    pReadCFI   Function to be registered.
*
*  Additional information
*    This function is optional. It can be used to specify a different
*    function for the reading of CFI parameters than the default function
*    used by the physical layer. This is typically required when the CFI
*    parameters do not fully comply with the CFI specification.
*
*    The application is permitted to call FS_NOR_CFI_SetReadCFICallback()
*    only during the file system initialization in FS_X_AddDevices().
*/
void FS_NOR_CFI_SetReadCFICallback(U8 Unit, FS_NOR_READ_CFI_CALLBACK * pReadCFI) {
  NOR_CFI_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->pReadCFI = pReadCFI;
  }
}

/*********************************************************************
*
*       FS_NOR_CFI_SetAddrGap
*
*  Function description
*    Configures a memory access gap.
*
*  Parameters
*    Unit       Index of the physical layer (0-based)
*    StartAddr  Address of the first byte in the gap.
*    NumBytes   Number of bytes in the gap.
*
*  Additional information
*    This function is optional. The application can use FS_NOR_CFI_SetAddrGap()
*    to specify a range in the memory region where the contents of the
*    NOR flash device is mapped that is not assigned to the NOR flash device.
*    Any access to an address equal to or greater than StartAddr is translated
*    by NumBytes. StartAddr and NumBytes have to be aligned to a physical sector
*    boundary of the used NOR flash device.
*
*    The application is permitted to call FS_NOR_CFI_SetAddrGap()
*    only during the file system initialization in FS_X_AddDevices().
*/
void FS_NOR_CFI_SetAddrGap(U8 Unit, U32 StartAddr, U32 NumBytes) {
  NOR_CFI_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->GapStartAddr = StartAddr;
    pInst->GapNumBytes  = NumBytes;
  }
}

/*********************************************************************
*
*       Public const data
*
**********************************************************************
*/

/*********************************************************************
*
*       1 x 16-bit CFI compliant NOR flash
*/
const FS_NOR_PHY_TYPE FS_NOR_PHY_CFI_1x16 = {
  _PHY_WriteOff1x16,
  _PHY_ReadOff,
  _PHY_EraseSector,
  _PHY_GetSectorInfo,
  _PHY_GetNumSectors,
  _PHY_Configure,
  _PHY_OnSelectPhy1x16,
  _PHY_DeInit,
  NULL,
  NULL
};

/*********************************************************************
*
*       2 x 16-bit CFI compliant NOR flash
*/
const FS_NOR_PHY_TYPE FS_NOR_PHY_CFI_2x16 = {
  _PHY_WriteOff2x16,
  _PHY_ReadOff,
  _PHY_EraseSector,
  _PHY_GetSectorInfo,
  _PHY_GetNumSectors,
  _PHY_Configure,
  _PHY_OnSelectPhy2x16,
  _PHY_DeInit,
  NULL,
  NULL
};

/*************************** End of file ****************************/
