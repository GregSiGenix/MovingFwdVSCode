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
File        : FS_NAND_Drv.c
Purpose     : File system generic NAND driver for Single level cell
              NAND flashes. Also works for Adesto / Atmel DataFlash devices.
              For more information on supported devices, refer to the
              user manual.
-------------------------- END-OF-HEADER -----------------------------

Literature:
[1] Samsung data sheet for K9XXG08UXM devices, specifically K9F2G08U0M, K9K4G08U1M. Similar information in other Samsung manuals.
    \\fileserver\techinfo\Company\Samsung\NAND_Flash\Device\K9F2GxxU0M_2KPageSLC_R12.pdf
[2] Micron data sheet for MT29F2G08AAD, MT29F2G16AAD, MT29F2G08ABD, MT29F2G16ABD devices
    \\fileserver\techinfo\Company\Micron\NANDFlash\MT29F2G0_8AAD_16AAD_08ABD_16ABD.pdf
[3] Micron presentations for Flash Memory Summit.
    \\fileserver\techinfo\Company\Micron\NANDFlash\flash_mem_summit_08_khurram_nand.pdf
[4] Micron application notes about bad block management, wear leveling and error correction codes.

General info on the inner workings of this high level flash driver:

ECC and Error correction
========================
  The driver uses an ECC error correction scheme. This error correction scheme allows finding and correcting 1-bit errors
  and detecting 2-bit errors. ECC is performed over blocks of 256 bytes. The reason why we chose this small block size
  (ECC block sizes of 512 or more are more common) is simple: It works more reliably and has a higher probability of actually
  detecting and fixing errors.
  Why are smaller ECC sizes better ?
  Simple. Let's take one example, where we consider 2 * 256 bytes of memory. If an error occurs in both blocks, it can still
  be fixed if every block has a separate ECC, but not if we only have one ECC for the entire 512 bytes.
  So the 256 byte ECC is always at least as good as the 512-byte ECC and better in some cases where we deal with 2 1-bit errors
  (which are correctable) versus a single 2-bit error, which is not.

  How does ECC work ?
    Fairly simple actually, but you should consult literature.
    Basically, you need 2 * n bits for an ECC of 2^n bits. This means: 256 bytes = 2048 bits = 2 ^ 11 bits => 22 bits required.

Data management
===============
  Data is stored in so called data blocks in the NAND flash. The assignment information (which physical block contains which data)
  is stored in the spare area of the block.
  Modifications of data are not done in the data blocks directly, but using a concept of work blocks. A work block contains
  modifications of a data block.
  The first block is used to store format information and written only once. All other blocks are used to store
  data. This means that in the driver, a valid physical block index is always > 0.

Capacity
========
  Not all available data blocks are used to store data.
  About 3% of the blocks are reserved in order to make sure that there are always blocks available,
  even if some develop bad blocks and can no longer be used.


Reading data
============
  So when data is read, the driver checks
  a) Is there a work block which contains this information ? If so, this is recent and used.
  b) Is there a data block which contains this information ? If so, this is recent and used.
  c) Otherwise, the sector has never been written. In this case, the driver delivers 0xFF data bytes.

Writing data to the NAND flash
==============================
  This is a complicated task.

Max. write accesses to a page without erasing it
================================================
  This depends on the sector size, but in general the following is done by the NAND driver when writing a block:

  Find work block which can be used:
  --> Work block found, but no more sectors are free, clean work block:
    --> Work block can be converted:  Up to sectors per page write accesses to one page (worst case: no sector in the page has been modified, all sectors in this page have to be copied from the old block)
                                     1 write access to the spare area of the first page in the block
    --> Work block can not be converted: Up to sectors per page write accesses to one page
                                        1 write access to spare area of first page in the block, to mark it as valid data block
                                        1 write access to the first page of the work block to mark it as invalid
                                        1 write access to the first page of the old data block to mark it as invalid
  --> No work block found or work block has been cleaned
    --> Write spare of first page in the work block to mark it as work block
  Write sector:
  --> 1 write access to write sector data + spare data (brsi info)
  --> 1 write access to write spare data in order to mark a previous version of the same sector as invalid

  Worst case:
    - First page in a work block is written in order to mark block as work block (1 WAcc)
    - Sector data + brsi is written into the first page in a work block (Up to sectors per page WAcc)
    - Previous version of the sector is invalidated. If we have 4 sectors in a page and the same sector is written 5 times, we have to invalidate the previous version of the sector 4 times
    - Work block is converted / copied: Mark as data block / invalid (1 WAcc)

    Worst case 1 + SectorsPerPage + SectorsPerPage + 1 Write accesses to the same page

Spare area usage
================

  Block information.

    Block info is stored in the first spare area of a block.

    Byte 0x00  - Block status: 0xFF means O.K. - Else Error!
    Byte 0x01  - Data Count & status
                 b[3..0]: DataCnt:      Increments every time a block is copied. part of the logical block.
                 b[7..4]: DataStat:     F: Unused (Empty)
                                        E: Work
                                        C: Data
                                        0: Dirty
    Byte 0x02  - EraseCnt b[31..24]
    Byte 0x03  - EraseCnt b[23..16]
    Byte 0x04  - EraseCnt b[15..8]
    Byte 0x05  - EraseCnt b[7..0]       -> For older NAND flashes, this byte is used as block info, in which case EraseCnt[7..0] moves to offset 0
    Byte 0x06  - LBI 15:8
    Byte 0x07  - LBI  7:0
    Byte 0x0B  - LBI 15:8 [Copy]
    Byte 0x0C  - LBI  7:0 [Copy]


  ECC information

    Spare are usage, ECC (512 byte pages)
      0x8 - 0xA     ECC for 0x100-0x1FF
      0xD - 0xF     ECC for 0x000-0x0FF

    Spare are usage, ECC (2048 byte pages)
      0x08 - 0x0A     ECC for 0x100-0x1FF
      0x0D - 0x0F     ECC for 0x000-0x0FF
      0x18 - 0x1A     ECC for 0x300-0x3FF
      0x1D - 0x1F     ECC for 0x200-0x2FF
      0x28 - 0x2A     ECC for 0x500-0x5FF
      0x2D - 0x2F     ECC for 0x400-0x4FF
      0x38 - 0x3A     ECC for 0x700-0x7FF
      0x3D - 0x3F     ECC for 0x600-0x6FF

  The sector usage information is only present in work blocks
  Moreover, this information is available for each sector.
  For example, if we have 4 sectors per page (512 byte sectors, 2048 byte page size, 64 bytes spare size),
  the brsi information is available 4 times in the spare area of one page (1 time for each sector)
  info for sector0: 0x06-0x07, 0x0B-0x0C
  info for sector1: 0x16-0x17, 0x1B-0x1C
  info for sector2: 0x26-0x27, 0x2B-0x2C
  info for sector3: 0x36-0x37, 0x3B-0x3C

  Sector usage (BRSI, block relative sector information) for sectors in a work block
  if BRSI = 0 - First sector in block
    Byte[0]   - Block status
    Byte[1]   - Data Count & Status
    Byte[5:2] - EraseCnt
  else        - BRSI > 0
    Byte[0]   - Unused (0xFF)
    Byte[1]   - FreeMarker for this sector. 0 if this sector has been freed using TRIM, else 0xFF
    Byte[2]   - FreeMarker for first sector if BRSI = 1, else unused (0xFF)
    Byte[5:3] - Unused (0xFF)
  endif
    Byte 0x06  - BRSI  15:8 ^ 0xff
    Byte 0x07  - BRSI  7:0  ^ 0xff
    Byte 0x0B  - BRSI 15:8  ^ 0xff [Copy]
    Byte 0x0C  - BRSI  7:0  ^ 0xff [Copy]
    Example:
      Let's look at a system with 16 sectors per block and a work block with BRSIs/LBIs as follows:
      SectorIndex  BRSI/LBI
                0    0x0001 -> Block 1  If ECC is valid, it contains BRSI 0 = Sector 16
                1    0x0000 -> Invalid (Same sector written multiple times into workblock)
                2    0xFFFE -> BSRI  1 -> Sector 17
                3    0xFFFF -> Unused
                4    0xFFFB -> BSRI  4 -> Sector 20
                5    0xFFFF -> Unused
                6    0xFFFF -> Unused
                ...
                15   0xFFFF -> Unused

Passive wear leveling
=====================
  BlockNoNextErase = pInst->BlockNoNextErase
  if (BlockNoNextErase == 0) {
    FindNextFree(pInst);
  }

Active wear leveling
====================
  If (pInst->MaxErase - pInst->MinErase > ACTIVE_WL_THRESHOLD) {
    CopyBlock(BlockNoLowestEraseCnt);
  }

 LBI: Logical block index   : Gives the data position of a block.
BRSI: Block relative sector index  : Index of sector relative to start of block, typ. 0..63 or 0..255

----------------------------------------------------------------------
Potential improvements

  Stability & data integrity
    - Optionally, mark blocks which have 1-bit errors as BAD (Maybe with special marker to show that they have been marked as bad by this driver and for what reason)

  Improve Speed
    - Optionally, keep erase count for every block in RAM (for systems with large amount of RAM) to speed up block search
    - Optionally, check before conversion if all the sectors of a work block are valid

  Reducing RAM size
    - Optionally, pFreeMap can be eliminated (free block is searched each time)

  Misc
    - Currently, at least 7 blocks are reserved, which is a lot for data flashes. This minimum could be eliminated. -> _ReadApplyDeviceParas()
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#include "FS_Int.h"
#include "FS_NAND_Int.h"

/*********************************************************************
*
*       Defines, fixed
*
**********************************************************************
*/
#define LLFORMAT_VERSION                  20001
#define MIN_BYTES_PER_PAGE                512u
#define BYTES_PER_ECC_BLOCK               256u
#define NUM_BLOCKS_RESERVED               2u    // Number of NAND blocks the driver reserves for internal use 1 for the low-level format information and one for the copy operation.

/*********************************************************************
*
*       Spare area usage
*
*  For details, see explanation in header
*/
#define SPARE_OFF_DATA_STATUS             0x01
#define SPARE_OFF_ERASE_CNT               0x02
#define SPARE_OFF_ADDR1                   0x06
#define SPARE_OFF_ADDR2                   0x0B
#define SPARE_OFF_ECC00                   0x0D
#define SPARE_OFF_ECC10                   0x08
#define SPARE_OFF_SECTOR_FREE             0x01
#define SPARE_OFF_SECTOR0_FREE            0x02

/*********************************************************************
*
*       Special values for "INVALID"
*/
#if FS_NAND_SUPPORT_FAST_WRITE
  #define LBI_INVALID                     0xFFFFu         // Invalid logical block index
#endif // FS_NAND_SUPPORT_FAST_WRITE
#define BRSI_INVALID                      0xFFFFu         // Invalid relative sector index
#define ERASE_CNT_INVALID                 0xFFFFFFFFuL    // Invalid erase count

/*********************************************************************
*
*       DATA STATUS NIBBLE
*/
#define DATA_STAT_EMPTY                   0xFu            // Block is empty
#define DATA_STAT_WORK                    0xEu            // Block is used as "work block"
#define DATA_STAT_VALID                   0xCu            // Block contains valid data
#define DATA_STAT_INVALID                 0x0u            // Block contains old, invalid data

/*********************************************************************
*
*       Block status marker
*/
#define BAD_BLOCK_MARKER                  0x00u
#define GOOD_BLOCK_MARKER                 0xFFu

/*********************************************************************
*
*       Status of read/write NAND operations
*/
#define RESULT_NO_ERROR                   0   // Everything OK
#define RESULT_1BIT_CORRECTED             1   // 1 bit error in the data corrected
#define RESULT_ERROR_IN_ECC               2   // Error detected in the ECC, data is OK
#define RESULT_UNCORRECTABLE_ERROR        3   // 2 or more bit errors detected, not recoverable
#define RESULT_READ_ERROR                 4   // Error while reading from NAND, not recoverable
#define RESULT_WRITE_ERROR                5   // Error while writing into NAND, recoverable
#define RESULT_OUT_OF_FREE_BLOCKS         6   // Tried to allocate a free block but no more were found
#define RESULT_ERASE_ERROR                7   // Error while erasing a NAND block, recoverable

/*********************************************************************
*
*       Index of physical sectors that store special data
*/
#define SECTOR_INDEX_FORMAT_INFO          0   // Format information
#define SECTOR_INDEX_ERROR_INFO           1   // Error information

/*********************************************************************
*
*       Number of work blocks
*/
#if FS_SUPPORT_JOURNAL
  #define NUM_WORK_BLOCKS_MIN             4u   // For performance reasons we need more work blocks when Journaling is enabled
#else
  #define NUM_WORK_BLOCKS_MIN             3u
#endif

#define NUM_WORK_BLOCKS_MAX               10u

#ifdef FS_NAND_MAX_WORK_BLOCKS
  #define NUM_WORK_BLOCKS_OLD             FS_NAND_MAX_WORK_BLOCKS
#else
  #define NUM_WORK_BLOCKS_OLD             3u
#endif

/*********************************************************************
*
*       Byte offsets in the sector that stores format information
*/
#define INFO_OFF_LLFORMAT_VERSION         0x10
#define INFO_OFF_SECTOR_SIZE              0x20
#define INFO_OFF_BAD_BLOCK_OFFSET         0x30
#define INFO_OFF_NUM_LOG_BLOCKS           0x40
#define INFO_OFF_NUM_WORK_BLOCKS          0x50

/*********************************************************************
*
*       The second sector of the first block in a NAND flash stores the fatal error information
*/
#define INFO_OFF_IS_WRITE_PROTECTED       0x00    // Inverted. 0xFFFF -> ~0xFFFF = 0 means normal (not write protected)
#define INFO_OFF_HAS_FATAL_ERROR          0x02    // Inverted. 0xFFFF -> ~0xFFFF = 0 means normal (no error)
#define INFO_OFF_FATAL_ERROR_TYPE         0x04    // Type of fatal error
#define INFO_OFF_FATAL_ERROR_SECTOR_INDEX 0x08    // Index of the sector where the error occurred

/*********************************************************************
*
*       ASSERT_UNIT_NO_IS_IN_RANGE
*/
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL)
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)                                 \
    if ((Unit) >= (U8)FS_NAND_NUM_UNITS) {                                 \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NAND: Invalid unit number.")); \
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);                                 \
    }
#else
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)
#endif

/*********************************************************************
*
*       ASSERT_PHY_TYPE_IS_SET
*/
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL)
  #define ASSERT_PHY_TYPE_IS_SET(pInst)                                            \
    if ((pInst)->pPhyType == NULL) {                                               \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NAND: Physical layer type not set.")); \
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);                                         \
    }
#else
  #define ASSERT_PHY_TYPE_IS_SET(pInst)
#endif

/*********************************************************************
*
*       IF_STATS
*/
#if FS_NAND_ENABLE_STATS
  #define IF_STATS(Expr)            Expr
#else
  #define IF_STATS(Expr)
#endif

/*********************************************************************
*
*       CALL_TEST_HOOK
*/
#if FS_SUPPORT_TEST
  #define CALL_TEST_HOOK(Unit)  \
    if (_pfTestHook != NULL) {  \
      _pfTestHook(Unit);        \
    }
#else
  #define CALL_TEST_HOOK(Unit)
#endif

/*********************************************************************
*
*       CHECK_CONSISTENCY
*/
#if FS_SUPPORT_TEST
  #define CHECK_CONSISTENCY(pInst)           \
    if (_CheckConsistency(pInst) != 0) {     \
      FS_X_PANIC(FS_ERRCODE_VERIFY_FAILURE); \
    }
#else
  #define CHECK_CONSISTENCY(pInst)
#endif

/*********************************************************************
*
*       Local types
*
**********************************************************************
*/

/*********************************************************************
*
*       NAND_WORK_BLOCK
*
*  Organization of a work block
*  ============================
*
*  The NAND_WORK_BLOCK structure has 6 elements, as can be seen below.
*  The first 2, pNext & pPrev, are used to keep it in a doubly linked list.
*  The next 2 elements are used to associate it with a data block and logical block index.
*  The last 2 elements contain the actual management data. The are pointers to arrays, allocated during initialization.
*  paIsWritten is a 1-bit array.
*    There is one bit = one entry for every sector in the block.
*  paAssign is a n bit array.
*    The number of bits is determined by the number of sectors per block.
*    The index is the logical position (BRSI, block relative sector index).
*/
typedef struct NAND_WORK_BLOCK {
  struct NAND_WORK_BLOCK * pNext;           // Pointer to next work buffer.     NULL if there is no next.
  struct NAND_WORK_BLOCK * pPrev;           // Pointer to previous work buffer. NULL if there is no previous.
  unsigned                 pbi;             // Physical Index of the destination block which data is written to. 0 means none is selected yet.
  unsigned                 lbi;             // Logical block index of the work block
  U8                     * paIsWritten;     // Pointer to IsWritten table, which indicates which sectors have already been transferred into the work buffer. This is a 1-bit array.
  void                   * paAssign;        // Pointer to assignment table, containing n bits per block. n depends on number of sectors per block.
} NAND_WORK_BLOCK;

/*********************************************************************
*
*       NAND_INST
*
*  Description
*    This is the central data structure for the entire driver.
*    It contains data items of one instance of the driver.
*/
typedef struct {
  U8                       Unit;
  U8                       IsLLMounted;
  U8                       LLMountFailed;
  U8                       IsWriteProtected;
  U8                       BadBlockOffset;          // Specifies where to find the bad block information in the spare area.
                                                    // Small page NAND flashes (Page size: 512 bytes) normally contain the information at Offset 5 in the spare area.
                                                    // Large page NAND flashes (Page size: 2048/4096 bytes)  normally contain the information at Offset 0 in the spare area.
  U8                       HasFatalError;
  U8                       ErrorType;               // Type of fatal error
  unsigned                 ErrorSectorIndex;        // Index of the sector where the fatal error occurred
  const FS_NAND_PHY_TYPE * pPhyType;                // Interface to physical layer
  U8                     * pFreeMap;                // Pointer to physical block usage map. Each bit represents one physical block. 0: Block is not assigned; 1: Assigned or bad block.
                                                    // Only purpose is to find a free block.
  U8                     * pLog2PhyTable;           // Pointer to Log2Phytable, which contains the logical to physical block translation (0xFFFF -> Not assigned)
  U32                      NumSectors;              // Number of logical sectors. This is redundant, but the value is used in a lot of places, so it is worth it!
  U32                      EraseCntMax;             // Worst (= highest) erase count of all blocks
  U32                      NumPhyBlocks;
  U32                      NumLogBlocks;
  U32                      EraseCntMin;             // Smallest erase count of all blocks. Used for active wear leveling.
  U32                      NumBlocksEraseCntMin;    // Number of erase counts with the smallest value
  U32                      NumWorkBlocks;           // Number of configured work blocks
  NAND_WORK_BLOCK        * pFirstWorkBlockInUse;    // Pointer to the first work block
  NAND_WORK_BLOCK        * pFirstWorkBlockFree;     // Pointer to the first free work block
  NAND_WORK_BLOCK        * paWorkBlock;             // WorkBlock management info
  U32                      MRUFreeBlock;            // Most recently used free block
  U16                      BytesPerSector;
  U16                      BytesPerPage;
  U8                       SPB_Shift;               // Sectors per Block shift: typ. a block contains 64 pages -> 8; in case of 2048 byte physical pages and 512 byte sectors: 256 -> 8
  U8                       PPB_Shift;               // Pages per block shift. Typ. 6 for 64 pages for block
  U8                       NumBitsPhyBlockIndex;
  //
  // Configuration items. 0 per default, which typically means: Use a reasonable default
  //
  U32                      FirstBlock;              // Allows sparing blocks at the beginning of the NAND flash
  U32                      MaxNumBlocks;            // Allows sparing blocks at the end of the NAND flash
  U32                      MaxEraseCntDiff;         // Threshold for active wear leveling
  U32                      NumWorkBlocksConf;       // Number of work blocks configured by application
  //
  // Additional info for debugging purposes
  //
#if FS_NAND_ENABLE_STATS
  FS_NAND_STAT_COUNTERS    StatCounters;
#endif
#if FS_NAND_VERIFY_ERASE
  U8                       VerifyErase;
#endif
#if FS_NAND_VERIFY_WRITE
  U8                       VerifyWrite;
#endif
#if FS_NAND_SUPPORT_FAST_WRITE
  U16                      NumBlocksFree;           // Number of work blocks reserved for fast write operations.
  U16                      NumSectorsFree;          // Number of sectors in a work block reserved for fast write operations.
  NAND_WORK_BLOCK        * pFirstWorkBlockErased;   // Pointer to the first free work block with an assigned erased block.
#endif
} NAND_INST;

/*********************************************************************
*
*       Static const data
*
**********************************************************************
*/

/*********************************************************************
*
*       The first sector/block in a NAND flash should have these values so the NAND
*       driver recognize the device as properly formatted.
*/
static const U8 _acInfo[16] = {
  0x53, 0x45, 0x47, 0x47, 0x45, 0x52, 0x00, 0x00,     // Id (Can be expanded in the future to include format / version information)
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static U32                              * _pSectorBuffer;        // We need a buffer for one sector for internal operations such as copying a block etc. Typ. 512 or 2048 bytes.
static U8                               * _pSpareAreaData;       // Buffer for spare area, Either 16 or 64 bytes.
static NAND_INST                        * _apInst[FS_NAND_NUM_UNITS];
static U8                                 _NumUnits = 0;
static FS_NAND_ON_FATAL_ERROR_CALLBACK  * _pfOnFatalError;
#if FS_SUPPORT_TEST
  static FS_NAND_TEST_HOOK_NOTIFICATION * _pfTestHook;
#endif

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _Count1Bits
*
*  Function description
*    Returns the number of bits set to 1.
*/
static U32 _Count1Bits(U32 Value) {
  Value = (Value & 0x55555555uL) + ((Value & 0xAAAAAAAAuL) >> 1);
  Value = (Value & 0x33333333uL) + ((Value & 0xCCCCCCCCuL) >> 2);
  Value = (Value & 0x0F0F0F0FuL) + ((Value & 0xF0F0F0F0uL) >> 4);
  Value = (Value & 0x00FF00FFuL) + ((Value & 0xFF00FF00uL) >> 8);
  Value = (Value & 0x0000FFFFuL) + ((Value & 0xFFFF0000uL) >> 16);
  return Value;
}

/*********************************************************************
*
*       _Find0BitInByte
*
*  Function description
*    Returns the position of the first 0-bit in a byte. The function checks only the bits
*    between the offsets FirstBit and LastBit inclusive.
*
*  Parameters
*    Data       Byte value to be searched.
*    FirstBit   Position of the first bit to check (0-based).
*    LastBit    Position of the last bit to check (0-based).
*    Off        Byte offset to be added to the found bit position.
*
*  Return value
*    >= 0   On Success, Bit position of first 0
*    -1     On Error, No 0-bit found
*/
static int _Find0BitInByte(U8 Data, unsigned FirstBit, unsigned LastBit, unsigned Off) {
  unsigned i;
  unsigned BitPos;

  for (i = FirstBit; i <= LastBit; i++) {
    if ((Data & (1u << i)) == 0u) {
      BitPos = i + (Off << 3);
      return (int)BitPos;
    }
  }
  return -1;
}

/*********************************************************************
*
*       _Find0BitInArray
*
*  Function description
*    Finds the first 0-bit in a byte array.
*
*  Return value
*    >= 0   On Success, Bit position of first 0.
*    -1     On Error, No 0-bit found.
*
*  Additional information
*    Bits are numbered LSB first as follows:
*      00000000 11111100
*      76543210 54321098
*    So the first byte contains bits 0..7, second byte contains bits 8..15, third byte contains 16..23
*/
static int _Find0BitInArray(const U8 * pData, unsigned FirstBit, unsigned LastBit) {
  unsigned FirstOff;
  unsigned LastOff;
  U8       Data;
  unsigned i;
  unsigned BitPos;
  int      r;

  FirstOff = FirstBit >> 3;
  LastOff  = LastBit  >> 3;
  pData   += FirstOff;

  //
  // Handle first byte
  //
  Data = *pData++;
  if (FirstOff == LastOff) {      // Special case where first and last byte are the same ?
    r = _Find0BitInByte(Data, FirstBit & 7u, LastBit & 7u, FirstOff);
    return r;
  }
  r = _Find0BitInByte(Data, FirstBit & 7u, 7, FirstOff);
  if (r >= 0) {
    BitPos = (unsigned)r + (FirstOff << 3);
    return (int)BitPos;
  }
  //
  // Handle complete bytes
  //
  for (i = FirstOff + 1u; i < LastOff; i++) {
    Data = *pData++;
    if (Data != 0xFFu) {
      r = _Find0BitInByte(Data, 0, 7, i);
      return r;
    }
  }
  //
  // Handle last byte
  //
  Data = *pData;
  r = _Find0BitInByte(Data, 0, LastBit & 7u, i);
  return r;
}

/*********************************************************************
*
*       _CalcNumWorkBlocksDefault
*
*   Function description
*     Computes the the default number of work blocks.
*     This is a percentage of number of NAND blocks.
*/
static U32 _CalcNumWorkBlocksDefault(U32 NumPhyBlocks) {
  U32 NumWorkBlocks;

#ifdef FS_NAND_MAX_WORK_BLOCKS
  FS_USE_PARA(NumPhyBlocks);
  NumWorkBlocks = FS_NAND_MAX_WORK_BLOCKS;
#else
  //
  // Allocate 10% of NAND capacity for work blocks
  //
  NumWorkBlocks = NumPhyBlocks >> 7;
  //
  // Limit the number of work blocks to reasonable values
  //
  if (NumWorkBlocks > NUM_WORK_BLOCKS_MAX) {
    NumWorkBlocks = NUM_WORK_BLOCKS_MAX;
  }
  if (NumWorkBlocks < NUM_WORK_BLOCKS_MIN) {
    NumWorkBlocks = NUM_WORK_BLOCKS_MIN;
  }
#endif
  return NumWorkBlocks;
}

/*********************************************************************
*
*       _CalcNumBlocksToUse
*
*   Function description
*     Computes the number of logical blocks available to file system.
*/
static int _CalcNumBlocksToUse(U32 NumPhyBlocks, U32 NumWorkBlocks) {
  int NumLogBlocks;
  U32 NumBlocksToUse;
  U32 Reserve;

  //
  // Compute the number of logical blocks. These are the blocks which are
  // actually available to the file system and therefor determines the capacity.
  // We reserve a small percentage (about 3%) for bad blocks
  // plus the number of work blocks + 1 info block (first block) + 1 block for copy operations
  //
  NumBlocksToUse = (NumPhyBlocks * 125u) >> 7;     // Reserve some blocks for blocks which are or can turn "bad" = unusable. We need at least one block.
  Reserve        = NumWorkBlocks + NUM_BLOCKS_RESERVED;
  NumLogBlocks   = (int)NumBlocksToUse - (int)Reserve;
  return NumLogBlocks;
}

/*********************************************************************
*
*       _CalcNumBlocksToUseOldFormat
*
*   Function description
*     Computes the number of logical blocks available to file system
*     like in the "old" versions of the driver.
*/
static int _CalcNumBlocksToUseOldFormat(U32 NumPhyBlocks, U32 NumWorkBlocks) {
  int NumLogBlocks;

  NumLogBlocks = _CalcNumBlocksToUse(NumPhyBlocks, NumWorkBlocks);
  NumLogBlocks++;                             // More logical blocks available to file system as in the current version
  return NumLogBlocks;
}

/*********************************************************************
*
*       _ComputeAndStoreECC
*
*  Function description
*    Computes the ECC values and writes them into the 64-byte buffer for the redundant area
*/
static void _ComputeAndStoreECC(const NAND_INST * pInst, const U32 * pData, U8 * pSpare) {
  unsigned i;
  U32      ecc;
  unsigned NumLoops;

  NumLoops = (unsigned)pInst->BytesPerSector >> 9;      // 512 bytes are taken care of in one loop.
  for (i = 0; i < NumLoops; i++) {
    ecc = FS__ECC256_Calc(pData);
    FS__ECC256_Store(pSpare + SPARE_OFF_ECC00, ecc);
    ecc = FS__ECC256_Calc(pData + 64);
    FS__ECC256_Store(pSpare + SPARE_OFF_ECC10, ecc);
    pData  += 128;
    pSpare += 16;
  }
}

/*********************************************************************
*
*       _ApplyECC
*
*  Function description
*    Uses the ECC values to correct the data if necessary
*    Works on an entire 2Kbyte page (which is divided into 8 parts of 256 bytes each)
*
*  Return value
*    ==-1     Data block is empty
*    == 0     O.K., data is valid. No error in data
*    == 1     1 bit error in data which has been corrected
*    == 2     Error in ECC
*    == 3     Uncorrectable error
*/
static int _ApplyECC(const NAND_INST * pInst, U32 * pData, const U8 * pSpare) {
  int      r;
  int      Result;
  unsigned i;
  U32      ecc;
  unsigned NumLoops;

  NumLoops = (unsigned)pInst->BytesPerSector >> 9;
  Result = 0;
  for (i = 0; i < NumLoops; i++) {
    ecc = FS__ECC256_Load(pSpare + SPARE_OFF_ECC00);
    if (FS__ECC256_IsValid(ecc) == 0) {
      return -1;       // Data block is empty
    }
    r = FS__ECC256_Apply(pData, ecc);
    if (r > Result) {
      Result = r;
    }
    ecc = FS__ECC256_Load(pSpare + SPARE_OFF_ECC10);
    r = FS__ECC256_Apply(pData + 64, ecc);
    if (r > Result) {
      Result = r;
    }
    pData  += 128;
    pSpare += 16;
  }
  return Result;
}

/*********************************************************************
*
*       _ReadApplyDeviceParas
*
*  Function description
*    Reads the device info and computes the parameters stored in the instance structure
*    such as number of blocks, number of sectors, sector size etc.
*
*  Return value
*    ==0    O.K., parameters read from NAND flash and driver instance configured.
*    ==1    Error, Could not apply device parameters.
*/
static int _ReadApplyDeviceParas(NAND_INST * pInst) {
  unsigned            BytesPerSector;
  unsigned            BytesPerPage;
  unsigned            SPB_Shift;
  unsigned            PPB_Shift;
  U32                 MaxNumBlocks;
  U32                 NumBlocks;
  FS_NAND_DEVICE_INFO DeviceInfo;
  U32                 NumWorkBlocks;
  int                 NumLogBlocks;
  U32                 FirstBlock;
  int                 r;

  FS_MEMSET(&DeviceInfo, 0, sizeof(DeviceInfo));
  r = pInst->pPhyType->pfInitGetDeviceInfo(pInst->Unit, &DeviceInfo);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND: Could not read device info."));
    return 1;
  }
  MaxNumBlocks = pInst->MaxNumBlocks;
  NumBlocks    = DeviceInfo.NumBlocks;
  FirstBlock   = pInst->FirstBlock;
  if (NumBlocks <= FirstBlock) {
    return 1;                         // Less blocks than configured
  }
  NumBlocks -= FirstBlock;
  if (MaxNumBlocks != 0u) {           // Is an upper limit configured ?
    if (NumBlocks > MaxNumBlocks) {
      NumBlocks = MaxNumBlocks;
    }
  }
  //
  // Compute a default number of work blocks if the application did not configured it yet.
  //
  if (pInst->NumWorkBlocksConf == 0u) {
    NumWorkBlocks = _CalcNumWorkBlocksDefault(NumBlocks);
  } else {
    NumWorkBlocks = pInst->NumWorkBlocksConf;
  }
  //
  // Compute the number of blocks available to file system
  //
  NumLogBlocks = _CalcNumBlocksToUse(NumBlocks, NumWorkBlocks);
  if (NumLogBlocks <= 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND: Insufficient logical blocks."));
    return 1;
  }
  pInst->NumPhyBlocks         = NumBlocks;
  pInst->NumBitsPhyBlockIndex = (U8)FS_BITFIELD_CalcNumBitsUsed(NumBlocks);
  pInst->NumLogBlocks         = (U32)NumLogBlocks;
  pInst->NumWorkBlocks        = NumWorkBlocks;
  BytesPerPage                = 1uL << DeviceInfo.BPP_Shift;
  PPB_Shift                   = DeviceInfo.PPB_Shift;
  if (BytesPerPage < MIN_BYTES_PER_PAGE) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND: Page size too small."));
    return 1;
  }
  //
  // Adjust BytesPerSector to be <= max. sector size
  //
  SPB_Shift      = PPB_Shift;
  BytesPerSector = BytesPerPage;
  while (BytesPerSector > FS_Global.MaxSectorSize) {
    BytesPerSector >>= 1;
    SPB_Shift++;
  }
  pInst->SPB_Shift      = (U8)SPB_Shift;
  pInst->BytesPerSector = (U16)BytesPerSector;
  pInst->BytesPerPage   = (U16)BytesPerPage;
  pInst->NumSectors     = (U32)pInst->NumLogBlocks << pInst->SPB_Shift;
  pInst->PPB_Shift      = (U8)PPB_Shift;
  return 0;                   // O.K., successfully identified
}

/*********************************************************************
*
*       _StoreEraseCnt
*
*  Function description
*    Stores the logical block index in the static spare area buffer.
*/
static void _StoreEraseCnt(const NAND_INST * pInst, U32 EraseCnt) {
  U8 * pBuffer;

  pBuffer    = (_pSpareAreaData + SPARE_OFF_ERASE_CNT);
  *pBuffer++ = (U8)(EraseCnt >> 24);
  *pBuffer++ = (U8)(EraseCnt >> 16);
  *pBuffer++ = (U8)(EraseCnt >>  8);
  //
  // Last byte is stored at offset 5 or 0 to avoid conflicts with the BAD block marker
  //
  pBuffer    = _pSpareAreaData + (5u - pInst->BadBlockOffset);
  *pBuffer   = (U8)EraseCnt;
}

/*********************************************************************
*
*       _LoadEraseCnt
*
*  Function description
*    Retrieves the erase count from the static spare area buffer.
*/
static U32 _LoadEraseCnt(const NAND_INST * pInst, const U8 * pSpare) {
  U32   r;
  const U8  * pBuffer;


  pBuffer = (pSpare + SPARE_OFF_ERASE_CNT);
  r = *pBuffer++;
  r = (r << 8) | *pBuffer++;
  r = (r << 8) | *pBuffer++;
  //
  // Last byte is stored at offset 5 or 0 to avoid conflicts with the BAD block marker
  //
  pBuffer    = pSpare + (5u - pInst->BadBlockOffset);
  r = (r << 8) | *pBuffer;
  return r;
}

/*********************************************************************
*
*       _StoreLBI
*
*  Function description
*    Stores the logical block index in the static spare area buffer.
*/
static void _StoreLBI(unsigned lbi) {
  FS_StoreU16BE(_pSpareAreaData + SPARE_OFF_ADDR1, lbi);
  FS_StoreU16BE(_pSpareAreaData + SPARE_OFF_ADDR2, lbi);
}

/*********************************************************************
*
*       _LoadLBI
*
*  Function description
*    Retrieves the logical block index from the static spare area buffer.
*
*  Return value
*    Block is not assigned:  pInst->NumLogBlocks
*    else                    LogicalBlockIndex  (>= 0, <= pInst->NumLogBlocks)
*/
static unsigned _LoadLBI(const NAND_INST * pInst) {
  unsigned lbi1;
  unsigned lbi2;

  lbi1 = FS_LoadU16BE(_pSpareAreaData + SPARE_OFF_ADDR1);
  lbi2 = FS_LoadU16BE(_pSpareAreaData + SPARE_OFF_ADDR2);
  if (lbi1 == lbi2) {
    if (lbi1 < pInst->NumLogBlocks) {
      return lbi1;
    }
  }
  return pInst->NumLogBlocks;
}

/*********************************************************************
*
*       _LoadBRSI
*
*  Function description
*    Retrieves the block relative sector index from the static spare area buffer.
*
*  Return value
*    BRSI_INVALID      Sector not assigned
*    else              BRSI
*/
static unsigned _LoadBRSI(const NAND_INST * pInst) {
  unsigned i1;
  unsigned i2;

  i1 = FS_LoadU16BE(_pSpareAreaData + SPARE_OFF_ADDR1);
  i2 = FS_LoadU16BE(_pSpareAreaData + SPARE_OFF_ADDR2);
  if (i1 == i2) {
    i1 ^= 0x0FFFFu;           // Physical to logical conversion
    if (i1 < (1uL << pInst->SPB_Shift)) {
      return i1;
    }
  }
  return BRSI_INVALID;
}

/*********************************************************************
*
*       _StoreBRSI
*
*  Function description
*    Writes the block relative sector index into the static spare area buffer.
*/
static void _StoreBRSI(unsigned lbi) {
  lbi ^= 0x0FFFFu;           // Logical to physical conversion
  FS_StoreU16BE(_pSpareAreaData + SPARE_OFF_ADDR1, lbi);
  FS_StoreU16BE(_pSpareAreaData + SPARE_OFF_ADDR2, lbi);
}

/*********************************************************************
*
*       _BlockIndex2SectorIndex
*
*  Function description
*    Returns the sector index of the first sector in a block.
*    With 128KB blocks and 2048 byte sectors, this means multiplying with 64.
*    With 128KB blocks and  512 byte sectors, this means multiplying with 256.
*/
static U32 _BlockIndex2SectorIndex(const NAND_INST * pInst, unsigned BlockIndex) {
  U32 SectorIndex;

  SectorIndex = (U32)BlockIndex << pInst->SPB_Shift;
  return SectorIndex;
}

/*********************************************************************
*
*       _PhySectorIndex2PageIndex_Data
*
*  Function description
*    Converts Logical pages (which can be 512 / 1024 / 2048 bytes) into
*    physical pages with same or larger page size.
*
*  Return value
*    Index of the physical page.
*
*  Notes
*    (1) Mapping of sectors to pages
*        In general, sector data is stored before spare data as follows:
*
*        !-------------------Phy. Page-------------!---- Phy. Spare area -------------!
*        !SectorData0!  ...        !SectorData<n>  !SectorSpare0! ....  SectorSpare<n>!
*
*        This typically applies only if a 2K device is used with 512 byte sectors.
*/
static U32 _PhySectorIndex2PageIndex_Data(const NAND_INST * pInst, U32 PhySectorIndex, unsigned * pOff) {
  unsigned SPP_Shift;
  U32      PageIndex;
  unsigned Mask;

  SPP_Shift = (unsigned)pInst->SPB_Shift - (unsigned)pInst->PPB_Shift;
  PageIndex = PhySectorIndex;
  if (SPP_Shift != 0u) {
    PageIndex >>= SPP_Shift;
    Mask        = PhySectorIndex & ((1uL << SPP_Shift) - 1u);
    *pOff      += Mask * pInst->BytesPerSector;
  }
  PageIndex += (U32)pInst->FirstBlock << pInst->PPB_Shift;
  return PageIndex;
}

/*********************************************************************
*
*       _PhySectorIndex2PageIndex_Spare
*
*  Function description
*    Converts Logical pages (which can be 512 / 1024 / 2048 bytes) into
*    physical pages with same or larger page size.
*/
static U32 _PhySectorIndex2PageIndex_Spare(const NAND_INST * pInst, U32 PhySectorIndex, unsigned * pOff) {
  unsigned SPP_Shift;
  unsigned Off;
  U32      PageIndex;

  SPP_Shift = (unsigned)pInst->SPB_Shift - (unsigned)pInst->PPB_Shift;
  PageIndex = PhySectorIndex;
  Off = *pOff;
  Off += pInst->BytesPerPage;        // Move offset from data to spare area
  if (SPP_Shift != 0u) {
    PageIndex >>= SPP_Shift;
    Off += (PhySectorIndex & ((1uL << SPP_Shift) - 1u)) * ((unsigned)pInst->BytesPerSector >> 5);
  }
  *pOff = Off;
  PageIndex += (U32)pInst->FirstBlock << pInst->PPB_Shift;
  return PageIndex;
}

/*********************************************************************
*
*       _ReadDataSpare
*
*  Function description
*    Reads (a part or all of) the data area
*    as well as (a part or all of) the spare area
*/
static int _ReadDataSpare(NAND_INST * pInst, U32 SectorIndex, void *pData, unsigned Off, unsigned NumBytes, void *pSpare, unsigned OffSpare, unsigned NumBytesSpare) {    //lint -efunc(818, _ReadDataSpare) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory]. Rationale: the statistical counters stored in the driver instance are updated in debug builds.
  U32 PageIndex;

  IF_STATS(pInst->StatCounters.ReadDataCnt++);     // Increment statistics counter if enabled
  PageIndex = _PhySectorIndex2PageIndex_Data(pInst, SectorIndex, &Off);
  (void)_PhySectorIndex2PageIndex_Spare(pInst, SectorIndex, &OffSpare);
  return pInst->pPhyType->pfReadEx(pInst->Unit, PageIndex, pData, Off, NumBytes, pSpare, OffSpare, NumBytesSpare);
}

/*********************************************************************
*
*       _ReadSpare
*
*  Function description
*    Reads (a part or all of) the spare area for the given sector
*
*  Notes
*    (1)  Alignment
*         For 16-bit NAND flashes, half-word alignment is required
*         ==> pData needs to be 16-bit aligned
*         ==> Off, NumBytes need to be even
*/
static int _ReadSpare(NAND_INST * pInst, U32 SectorIndex, void *pData, unsigned Off, unsigned NumBytes) {     //lint -efunc(818, _ReadSpare) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory]. Rationale: the statistical counters stored in the driver instance are updated in debug builds.
  U32 PageIndex;

// TBD: Assert (See note (1))
  IF_STATS(pInst->StatCounters.ReadSpareCnt++);     // Increment statistics counter if enabled
  PageIndex = _PhySectorIndex2PageIndex_Spare(pInst, SectorIndex, &Off);
  return pInst->pPhyType->pfRead(pInst->Unit, PageIndex, pData, Off, NumBytes);
}

/*********************************************************************
*
*       _ReadSpareByte
*
*  Function description
*    Reads 1 byte of the spare area of the given sector
*/
static int _ReadSpareByte(NAND_INST * pInst, U32 SectorIndex, U8 *pData, unsigned Off) {
  U8  ab[2];
  int r;

  r = _ReadSpare(pInst, SectorIndex, ab, Off & 0xFEu, 2);
  *pData = ab[Off & 1u];
  return r;
}

/*********************************************************************
*
*       _ReadSpareIntoStaticBuffer
*
*  Function description
*    Reads the entire spare area of the given sector into the static buffer
*/
static int _ReadSpareIntoStaticBuffer(NAND_INST * pInst, U32 SectorIndex) {
  return _ReadSpare(pInst, SectorIndex, _pSpareAreaData, 0, (unsigned)pInst->BytesPerSector >> 5);
}

/*********************************************************************
*
*       _WriteSpare
*
*   Function description
*     Writes into the spare area of a sector.
*
*   Parameters
*     pInst         [IN]  Driver instance.
*     SectorIndex   Index of the sector to write.
*     pData         [IN]  Sector data.
*     Off           Byte offset inside the spare area.
*     NumBytes      Number of bytes to write.
*
*   Return value
*     ==0   OK, spare area written.
*     !=0   A write error occurred (recoverable).
*/
static int _WriteSpare(NAND_INST * pInst, U32 SectorIndex, const void * pData, unsigned Off, unsigned NumBytes) {     //lint -efunc(818, _WriteSpare) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory]. Rationale: the statistical counters stored in the driver instance are updated in debug builds.
  U32 PageIndex;

  IF_STATS(pInst->StatCounters.WriteSpareCnt++);     // Increment statistics counter if enabled
  PageIndex = _PhySectorIndex2PageIndex_Spare(pInst, SectorIndex, &Off);
  return pInst->pPhyType->pfWrite(pInst->Unit, PageIndex, pData, Off, NumBytes);
}

/*********************************************************************
*
*       _WriteDataSpare
*
*  Function description
*    Writes (a part or all of) the data area
*    as well as (a part or all of) the spare area
*    The important point here is this function performs both operations with a single call to the physical layer,
*    giving the physical layer a chance to perform the operation as one operation, which saves time.
*/
static int _WriteDataSpare(NAND_INST * pInst, U32 SectorIndex, const void * pData, unsigned Off, unsigned NumBytes, const void * pSpare, unsigned OffSpare, unsigned NumBytesSpare) {   //lint -efunc(818, _WriteDataSpare) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory]. Rationale: the statistical counters stored in the driver instance are updated in debug builds.
  U32 PageIndex;

  IF_STATS(pInst->StatCounters.WriteDataCnt++);     // Increment statistics counter if enabled
  PageIndex = _PhySectorIndex2PageIndex_Data(pInst, SectorIndex, &Off);
  (void)_PhySectorIndex2PageIndex_Spare(pInst, SectorIndex, &OffSpare);
  return pInst->pPhyType->pfWriteEx(pInst->Unit, PageIndex, pData, Off, NumBytes, pSpare, OffSpare, NumBytesSpare);
}


/*********************************************************************
*
*       _WriteSpareByte
*
*  Function description
*    Writes 1 byte of the spare area of the given sector
*    Since we need 2 byte alignment for 16-bit NAND flashes, we copy this into a 2 byte buffer.
*/
static int _WriteSpareByte(NAND_INST * pInst, U32 SectorIndex, U8 Data, unsigned Off) {
  U8 ab[2];

  ab[Off        & 1u] = Data;     // Write data byte into buffer
  ab[(Off + 1u) & 1u] = 0xFFu;    // Fill the other byte with 0xff which means "do not change"
  return _WriteSpare(pInst, SectorIndex, ab, Off & 0xFEu, 2);
}

/*********************************************************************
*
*       _WriteSpareAreaFromStaticBuffer
*/
static int _WriteSpareAreaFromStaticBuffer(NAND_INST * pInst, U32 SectorIndex) {
  return _WriteSpare(pInst, SectorIndex, _pSpareAreaData, 0, (unsigned)pInst->BytesPerSector >> 5);
}

/*********************************************************************
*
*       _ReadPhySpare
*
*   Function description
*     Reads (a part or all of) the spare area
*
*   Parameters
*     pInst       [IN]  Driver instance.
*     PageIndex   Physical index of the page to read from.
*     pData       [OUT] Data read from the spare area.
*     Off         Byte offset in the spare area to start reading.
*     NumBytes    Number of bytes to read.
*
*   Return value
*     == 0  OK, spare area read.
*     != 0  An error occurred.
*/
static int _ReadPhySpare(NAND_INST * pInst, U32 PageIndex, void * pData, unsigned Off, unsigned NumBytes) {       //lint -efunc(818, _ReadPhySpare) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory]. Rationale: the statistical counters stored in the driver instance are updated in debug builds.
  IF_STATS(pInst->StatCounters.ReadSpareCnt++);     // Increment statistics counter if enabled
  return pInst->pPhyType->pfRead(pInst->Unit, PageIndex, pData, Off + pInst->BytesPerPage, NumBytes);
}

/*********************************************************************
*
*       _ReadPhySpareByte
*
*  Function description
*    Reads 1 byte of the spare area of the given sector
*
*  Notes
*    (1) Usage of this function
*        This function should only be called for one reason: To read a physical page as defined
*        by the manufacturer.
*        This is required to find out which blocks are marked as bad (DO NOT USE).
*        All other code should use sub routines that read on a per sector basis in order to allow
*        formatting / accessing 2K NAND flashes with 512 byte sectors (saving a lot of RAM)
*/
static int _ReadPhySpareByte(NAND_INST * pInst, U32 PageIndex, U8 * pData, unsigned Off) {
  U8  ab[2];
  int r;

  PageIndex += pInst->FirstBlock << pInst->PPB_Shift;
  r = _ReadPhySpare(pInst, PageIndex, ab, Off & 0xFEu, 2);     // make sure we have 2-byte alignment required by 16-bit NAND flashes
  *pData = ab[Off & 1u];
  return r;
}

#if FS_NAND_VERIFY_ERASE

/*********************************************************************
*
*       _IsBlockBlank
*
*  Function description
*    Checks if all the bytes in a block are set to 0xFF.
*
*  Return value
*    ==1    Block is blank.
*    ==0    Block is not blank or an error occurred.
*/
static int _IsBlockBlank(NAND_INST * pInst, unsigned BlockIndex) {
  U32        SectorsPerBlock;
  U32        SectorIndex;
  U32      * pData32;
  unsigned   NumLoops;
  unsigned   BytesPerSector;
  unsigned   BytesPerSpare;
  int        r;

  SectorsPerBlock = 1uL << pInst->SPB_Shift;
  SectorIndex     = _BlockIndex2SectorIndex(pInst, BlockIndex);
  BytesPerSector  = pInst->BytesPerSector;
  BytesPerSpare   = BytesPerSector >> 5;
  //
  // For each sector in the block read the main and the spare data and verify them.
  //
  do {
    //
    // Read one sector at a time.
    //
    r = _ReadDataSpare(pInst, SectorIndex, _pSectorBuffer, 0, BytesPerSector, _pSpareAreaData, 0, BytesPerSpare);
    if (r != 0) {
      return 0;                             // Error, could not read data.
    }
    //
    // Verify the main area.
    //
    NumLoops = BytesPerSector >> 2;         // Compare 4 bytes at a time.
    pData32  = SEGGER_PTR2PTR(U32, _pSectorBuffer);
    do {
      if (*pData32 != 0xFFFFFFFFuL) {
        return 0;                           // Error, verification failed.
      }
      ++pData32;
    } while (--NumLoops != 0u);
    //
    // Verify the spare area.
    //
    NumLoops = BytesPerSpare >> 2;          // Compare 4 bytes at a time.
    pData32  = SEGGER_PTR2PTR(U32, _pSpareAreaData);
    do {
      if (*pData32 != 0xFFFFFFFFuL) {
        return 0;                           // Error, verification failed.
      }
      ++pData32;
    } while (--NumLoops != 0u);
    ++SectorIndex;
  } while (--SectorsPerBlock != 0u);
  return 1;                                   // All bytes in the block are set to 0xFF.
}

#endif  // FS_NAND_VERIFY_ERASE

/*********************************************************************
*
*       _EraseBlock
*/
static int _EraseBlock(NAND_INST * pInst, unsigned BlockIndex) {        //lint -efunc(818, _EraseBlock) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory]. Rationale: the statistical counters stored in the driver instance are updated in debug builds.
  U32 PageIndex;
  int r;

  IF_STATS(pInst->StatCounters.EraseCnt++);     // Increment statistics counter if enabled
  BlockIndex += pInst->FirstBlock;
  PageIndex = (U32)BlockIndex << pInst->PPB_Shift;
  r = pInst->pPhyType->pfEraseBlock(pInst->Unit, PageIndex);
#if FS_NAND_VERIFY_ERASE
  //
  // Verify if all the bytes in the block are set to 0xFF.
  //
  if (r == 0) {
    if (pInst->VerifyErase != 0u) {
      int IsBlank;

      IsBlank = _IsBlockBlank(pInst, BlockIndex);
      if (IsBlank == 0) {
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND: Verify failed at block %d.", BlockIndex));
        r = 1;                                  // Error, the block was not erased correctly.
      }
    }
  }
#endif // FS_NAND_VERIFY_ERASE
  return r;
}

/*********************************************************************
*
*       _PreEraseBlock
*
*  Function description
*    Pre-erasing means writing an value into the data status which indicates that
*    the data is invalid and the block needs to be erased.
*/
static int _PreEraseBlock(NAND_INST * pInst, unsigned PhyBlockIndex) {
  U32 SectorIndex;

  SectorIndex = _BlockIndex2SectorIndex(pInst, PhyBlockIndex);
  return _WriteSpareByte(pInst, SectorIndex, 0, SPARE_OFF_DATA_STATUS);
}

/*********************************************************************
*
*       _MarkBlockAsFree
*
*  Function description
*    Mark block as free in management data.
*/
static void _MarkBlockAsFree(NAND_INST * pInst, unsigned iBlock) {          //lint -efunc(818, _MarkBlockAsFree) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory]. Rationale: the statistical counters stored in the driver instance are updated in debug builds.
  unsigned   Mask;
  U8       * pData;
  unsigned   Data;

  if (iBlock < pInst->NumPhyBlocks) {
    Mask  = 1uL << (iBlock & 7u);
    pData = pInst->pFreeMap + (iBlock >> 3);
    Data  = *pData;
#if FS_NAND_ENABLE_STATS
    if ((Data & Mask) == 0u) {
      pInst->StatCounters.NumFreeBlocks++;
    }
#endif // FS_NAND_ENABLE_STATS
    Data   |= Mask;
    *pData  = (U8)Data;
  }
}

/*********************************************************************
*
*       _MarkBlockAsAllocated
*
*  Function description
*    Mark block as allocated in management data.
*/
static void _MarkBlockAsAllocated(NAND_INST * pInst, unsigned iBlock) {     //lint -efunc(818, _MarkBlockAsAllocated) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory]. Rationale: the statistical counters stored in the driver instance are updated in debug builds.
  unsigned   Mask;
  U8       * pData;
  unsigned   Data;

  if (iBlock < pInst->NumPhyBlocks) {
    Mask  = 1uL << (iBlock & 7u);
    pData = pInst->pFreeMap + (iBlock >> 3);
    Data  = *pData;
#if FS_NAND_ENABLE_STATS
    if ((Data & Mask) != 0u) {
      pInst->StatCounters.NumFreeBlocks--;
    }
#endif // FS_NAND_ENABLE_STATS
    Data   &= ~Mask;
    *pData  = (U8)Data;
  }
}

/*********************************************************************
*
*       _IsBlockFree
*
*  Function description
*    Return if block is free.
*/
static int _IsBlockFree(const NAND_INST * pInst, unsigned iBlock) {
  unsigned   Mask;
  U8       * pData;
  unsigned   Data;

  if (iBlock >= pInst->NumPhyBlocks) {
    return 0;         // Error, invalid block index.
  }
  Mask  = 1uL << (iBlock & 7u);
  pData = pInst->pFreeMap + (iBlock >> 3);
  Data  = *pData;
  if ((Data & Mask) != 0u) {
    return 1;
  }
  return 0;
}

/*********************************************************************
*
*       _ClearStaticSpareArea
*
*  Function description
*    Fills the static spare area with 0xFF.
*/
static void _ClearStaticSpareArea(unsigned SpareAreaSize) {
  FS_MEMSET(_pSpareAreaData, 0xFF, SpareAreaSize);
}

/*********************************************************************
*
*       _ClearStaticSpareAreaExceptECC
*
*  Function description
*    Fills the static spare area with 0xFF, except the bytes which store the ECC.
*    Used by the sector copy routine to speed up the write operation.
*/
static void _ClearStaticSpareAreaExceptECC(unsigned SpareAreaSize) {
  unsigned   i;
  unsigned   NumLoops;
  U8       * pSpare;

  NumLoops = SpareAreaSize >> 4;                 // 16 bytes are taken care of in one loop
  pSpare   = _pSpareAreaData;
  for (i = 0; i < NumLoops; i++) {
    FS_MEMSET(pSpare,                   0xFF, SPARE_OFF_ECC10);
    FS_MEMSET(&pSpare[SPARE_OFF_ADDR2], 0xFF, sizeof(U16));
    pSpare += 16;
  }
}

/*********************************************************************
*
*       _WriteSector
*
*  Function description
*    Writes the sector data with ECC.
*
*  Return value
*    ==0      O.K., page data has been successfully written.
*    !=0      Error, could not write data.
*
*  Additional information
*    This function performs the following:
*    - Computes ECC and stores it into static spare area
*    - Write entire sector & spare area into NAND flash (in one operations if possible)
*
*  Notes
*    (1) Before the function call, the static spare area needs to contain
*        information for the page (such as lbi, EraseCnt, etc.)
*/
static int _WriteSector(NAND_INST * pInst, const U32 * pBuffer, U32 SectorIndex) {
  _ComputeAndStoreECC(pInst, (const U32 *)pBuffer, _pSpareAreaData);
  return _WriteDataSpare(pInst, SectorIndex, pBuffer, 0, pInst->BytesPerSector, _pSpareAreaData, 0, (unsigned)pInst->BytesPerSector >> 5);
}

/*********************************************************************
*
*       _GetOffBlockStatus
*
*  Function description
*    Returns the byte offset relative to beginning of the spare area
*    of the byte that indicate if a block is defective.
*/
static unsigned _GetOffBlockStatus(const NAND_INST * pInst) {
  unsigned Off;

  Off = (pInst->BytesPerPage == 512u) ? 5uL : 0uL;
  return Off;
}

/*********************************************************************
*
*       _MarkBlockAsBad
*
*  Function description
*    The first byte of the spare area in each block is used to mark the block as bad.
*    If it is != 0xFF, then this block will not be used any more.
*
*  Parameters
*    pInst            [IN]  Driver instance.
*    BlockIndex       Index of the block to mark as defective.
*    ErrorType        Reason why the block is marked as defective.
*    ErrorBRSI        Index of the physical sector where the error occurred.
*/
static void _MarkBlockAsBad(NAND_INST * pInst, unsigned BlockIndex, int ErrorType, unsigned ErrorBRSI) {
  U32        SectorIndex;
  U8       * pSpare;
  const U8 * pInfo;
  U8         BlockStatus;
  unsigned   OffStatus;
  unsigned   SectorsPerPage;

  IF_STATS(pInst->StatCounters.NumBadBlocks++);
  SectorIndex    = _BlockIndex2SectorIndex(pInst, BlockIndex);
  SectorsPerPage = 1uL << (pInst->SPB_Shift - pInst->PPB_Shift);
  //
  // Store the bad block marker.
  //
  BlockStatus = BAD_BLOCK_MARKER;
  OffStatus   = _GetOffBlockStatus(pInst);
  (void)_WriteSpareByte(pInst, SectorIndex, BlockStatus, OffStatus);
  //
  // We write the "SEGGER" text on the spare area of the block to be able to distinguish from the other "bad" blocks marked by manufacturer.
  // Additional information about the reason why the block was marked as "bad" is saved on the spare area of the 3rd page in the block.
  // The information is stored as follows:
  // 2nd page:
  //   aSpare[0] = 'S'
  //   aSpare[1] = Not modified (used for sector status)
  //   aSpare[2] = Not modified (used for status of sector 0)
  //   aSpare[3] = 'E'
  //   aSpare[4] = 'G'
  //   aSpare[5] = 'G'
  // 3rd page:
  //   aSpare[0] = 'E'
  //   aSpare[1] = Not modified (used for sector status)
  //   aSpare[2] = 'R'
  //   aSpare[3] = Error type
  //   aSpare[4] = Error BRSI (MSB)
  //   aSpare[5] = Error BRSI (LSB)
  //
  SectorIndex += SectorsPerPage;        // 2nd page
  _ClearStaticSpareArea((unsigned)pInst->BytesPerSector >> 5);
  pSpare = _pSpareAreaData;
  pInfo  = _acInfo;
  *pSpare       = *pInfo++;
  *(pSpare + 3) = *pInfo++;
  *(pSpare + 4) = *pInfo++;
  *(pSpare + 5) = *pInfo++;
  (void)_WriteSpareAreaFromStaticBuffer(pInst, SectorIndex);
  SectorIndex += SectorsPerPage;        // 3rd page
  _ClearStaticSpareArea((unsigned)pInst->BytesPerSector >> 5);
  pSpare = _pSpareAreaData;
  *pSpare       = *pInfo++;
  *(pSpare + 2) = *pInfo;
  *(pSpare + 3) = (U8)ErrorType;
  *(pSpare + 4) = (U8)(ErrorBRSI >> 8);
  *(pSpare + 5) = (U8)ErrorBRSI;
  (void)_WriteSpareAreaFromStaticBuffer(pInst, SectorIndex);
}

/*********************************************************************
*
*       _IsBlockBad
*
*   Function description
*     Checks whether a block can be used to store data.
*     The good/bad status is read from the spare area of the first and second page.
*
*   Return value
*     ==0   Block can be used to store data
*     !=0   Block is defect
*/
static int _IsBlockBad(NAND_INST * pInst, unsigned BlockIndex) {
  U32      PageIndex;
  U8       BlockStatus;
  unsigned OffStatus;

  OffStatus   = _GetOffBlockStatus(pInst);
  PageIndex   = (U32)BlockIndex << pInst->PPB_Shift;
  BlockStatus = BAD_BLOCK_MARKER;
  (void)_ReadPhySpareByte(pInst, PageIndex, &BlockStatus, OffStatus);
  if (BlockStatus == GOOD_BLOCK_MARKER) {
    (void)_ReadPhySpareByte(pInst, PageIndex + 1u, &BlockStatus, OffStatus);
    if (BlockStatus == GOOD_BLOCK_MARKER) {
      return 0;       // OK, block can be used to store data.
    }
  }
  return 1;           // Block is defective.
}

/*********************************************************************
*
*       _IsBlockErasable
*
*  Function description
*    Checks whether the driver is allowed to erase the given block.
*    The blocks marked as bad by the manufacturer are never erased.
*    Erasing of the blocks marked as bad by the driver can be explicitly
*    enabled/disable via a compile time switch.
*/
static int _IsBlockErasable(NAND_INST * pInst, unsigned BlockIndex) {
  if (_IsBlockBad(pInst, BlockIndex) == 0) {
    return 1;             // Block is not bad and it can be erased.
  }
#if FS_NAND_RECLAIM_DRIVER_BAD_BLOCKS
  {
    const U8 * pInfo;
    U8       * pSpare;
    U8         aSpare[8]; // 6 bytes for "SEGGER", 1 byte for the "bad" block mark and 1 byte more for 16-bit alignment.
    unsigned   i;
    U32        PageIndex;
    unsigned   OffStatus;
    U32        SectorIndex;
    unsigned   NumBytesToCheck;
    unsigned   SectorsPerPage;

    //
    // Check whether the block was marked bad by the driver. If so tell the caller it can erase it.
    // This is the signature used up to and including the version 4.00b.
    //
    PageIndex  = (U32)BlockIndex << pInst->PPB_Shift;
    PageIndex += pInst->FirstBlock << pInst->PPB_Shift;
    OffStatus  = _GetOffBlockStatus(pInst);
    (void)_ReadPhySpare(pInst, PageIndex, aSpare, 0, sizeof(aSpare));
    pInfo           = _acInfo;
    pSpare          = aSpare;
    NumBytesToCheck = sizeof(aSpare) - 1u;  // -1 since only the first 7 bytes are checked.
    for (i = 0; i < NumBytesToCheck; ++i) {
      if (OffStatus != i) {
        if (*pSpare != *pInfo) {
          break;                            // Driver signature for bad block do not match. Block marked as bad by the manufacturer.
        }
        ++pInfo;
      }
      ++pSpare;
    }
    if (i == NumBytesToCheck) {
      return 1;                             // Driver marked the block as bad. This block can be erased.
    }
    //
    // Check if the block was marked as bad by an emFile version >= 4.02a
    //
    SectorIndex    = _BlockIndex2SectorIndex(pInst, BlockIndex);
    SectorsPerPage = 1uL << (pInst->SPB_Shift - pInst->PPB_Shift);
    SectorIndex += SectorsPerPage;          // 2nd page
    (void)_ReadSpareIntoStaticBuffer(pInst, SectorIndex);
    pSpare = _pSpareAreaData;
    if ((*pSpare       == *pInfo)       &&
        (*(pSpare + 3) == *(pInfo + 1)) &&
        (*(pSpare + 4) == *(pInfo + 2)) &&
        (*(pSpare + 5) == *(pInfo + 3))) {
      SectorIndex += SectorsPerPage;        // 3rd page
      (void)_ReadSpareIntoStaticBuffer(pInst, SectorIndex);
      if ((*pSpare       == *(pInfo + 4)) &&
          (*(pSpare + 2) == *(pInfo + 5))) {
        return 1;                           // Driver marked the block as bad. This block can be erased.
      }
    }
  }
#endif
  return 0;
}

/*********************************************************************
*
*       _ReadSectorWithECC
*
*  Function description
*    Reads the contents of a sector and check the ECC.
*
*   Parameters
*     pInst         [IN]  Driver instance.
*     pBuffer       [OUT] Contents of the read sector.
*     SectorIndex   Index of the sector to read.
*
*   Return value
*     -1                          OK, page is blank
*     RESULT_NO_ERROR             OK
*     RESULT_1BIT_CORRECTED       OK
*     RESULT_ERROR_IN_ECC         OK
*     RESULT_UNCORRECTABLE_ERROR  Error
*     RESULT_READ_ERROR           Error
*
*  Additional information
*    The function performs the following:
*    - Reads an page data into the specified buffer & spare area into local area
*    - Performs error correction on the data
*
*  Notes
*     (1) Before the function call, the static spare area needs to contain info for the page!
*/
static int _ReadSectorWithECC(NAND_INST * pInst, U32 * pBuffer, U32 SectorIndex) {
  int r;
  int NumRetries;

  NumRetries = FS_NAND_NUM_READ_RETRIES;
  for (;;) {
    //
    // Read data and the entire spare area
    //
    r = _ReadDataSpare(pInst, SectorIndex, pBuffer, 0, pInst->BytesPerSector, _pSpareAreaData, 0, (unsigned)pInst->BytesPerSector >> 5);
    if (r != 0) {
      r = RESULT_READ_ERROR;        // Error while reading, try again
      goto Retry;
    }
    //
    // Check the ECC of sector data and correct one bit errors
    //
    r = _ApplyECC(pInst, (U32 *)pBuffer, _pSpareAreaData);
    if (r < 0) {
      return r;                     // Sector has no data
    }
    if ((r == RESULT_NO_ERROR) || (r == RESULT_1BIT_CORRECTED)) {
      return r;                     // Data has no errors
    }
Retry:
    if (NumRetries-- == 0) {
      break;
    }
    IF_STATS(pInst->StatCounters.NumReadRetries++);
  }
  return r;
}

/*********************************************************************
*
*       _L2P_Read
*
*  Function description
*    Returns the contents of the given entry in the L2P table (physical block lookup table)
*/
static unsigned _L2P_Read(const NAND_INST * pInst, U32 LogIndex) {
  U32 v;

  v = FS_BITFIELD_ReadEntry(pInst->pLog2PhyTable, LogIndex, pInst->NumBitsPhyBlockIndex);
  return v;
}

/*********************************************************************
*
*       _L2P_Write
*
*  Function description
*    Updates the contents of the given entry in the L2P table (physical block lookup table)
*/
static void _L2P_Write(const NAND_INST * pInst, U32 LogIndex, unsigned v) {
  FS_BITFIELD_WriteEntry(pInst->pLog2PhyTable, LogIndex, pInst->NumBitsPhyBlockIndex, v);
}

/*********************************************************************
*
*       _L2P_GetSize
*
*  Function description
*    Computes & returns the size of the L2P assignment table of a work block.
*    Is used before allocation to find out how many bytes need to be allocated.
*/
static unsigned _L2P_GetSize(const NAND_INST * pInst) {
  unsigned v;

  v = FS_BITFIELD_CalcSize(pInst->NumLogBlocks, pInst->NumBitsPhyBlockIndex);
  return v;
}

/*********************************************************************
*
*       _WB_IsSectorWritten
*
*  Function description
*    Returns if a sector in a work block is used (written) by looking at the 1 bit paIsWritten-array.
*    A set bit indicates that the sector in question has been written.
*
*  Return value
*    ==0    Sector not written
*    !=0    Sector written
*/
static int _WB_IsSectorWritten(const NAND_WORK_BLOCK * pWorkBlock, unsigned brsi) {
  unsigned   Data;
  U8       * pData;

  pData  = pWorkBlock->paIsWritten + (brsi >> 3);
  Data   = *pData;
  Data >>= brsi & 7u;
  if ((Data & 1u) != 0u) {
    return 1;
  }
  return 0;
}

/*********************************************************************
*
*       _WB_MarkSectorAsUsed
*
*  Function description
*    Mark sector as used in work block
*/
static void _WB_MarkSectorAsUsed(const NAND_WORK_BLOCK * pWorkBlock, unsigned brsi) {
  unsigned   Mask;
  unsigned   Data;
  U8       * pData;

  Mask    = 1uL << (brsi & 7u);
  pData   = pWorkBlock->paIsWritten + (brsi >> 3);
  Data    = *pData;
  Data   |= Mask;
  *pData  = (U8)Data;
}

#if FS_NAND_SUPPORT_FAST_WRITE

/*********************************************************************
*
*       _WB_GetNumFreeSectors
*
*  Function description
*    Returns the number of sectors in the work block which were not written.
*/
static unsigned _WB_GetNumFreeSectors(const NAND_INST * pInst, const NAND_WORK_BLOCK * pWorkBlock) {
  unsigned SectorsPerBlock;
  unsigned iSector;
  unsigned NumSectors;

  NumSectors      = 0;
  SectorsPerBlock = 1uL << pInst->SPB_Shift;
  for (iSector = 0; iSector < SectorsPerBlock; ++iSector) {
    if (_WB_IsSectorWritten(pWorkBlock, iSector) == 0) {
      ++NumSectors;
    }
  }
  return NumSectors;
}

#endif // FS_NAND_SUPPORT_FAST_WRITE

/*********************************************************************
*
*       _WB_ReadAssignment
*
*  Function description
*    Reads an entry in the assignment table of a work block.
*    It is necessary to use a subroutine to do the job since the entries are stored in a bitfield.
*    Logically, the code does the following:
*      return pWorkBlock->aAssign[Index];
*/
static unsigned _WB_ReadAssignment(const NAND_INST * pInst, const NAND_WORK_BLOCK * pWorkBlock, unsigned Index) {
  unsigned r;

  r = FS_BITFIELD_ReadEntry(SEGGER_CONSTPTR2PTR(const U8, pWorkBlock->paAssign), Index, pInst->SPB_Shift);
  return r;
}

/*********************************************************************
*
*       _WB_WriteAssignment
*
*  Function description
*    Writes an entry in the assignment table of a work block.
*    It is necessary to use a subroutine to do the job since the entries are stored in a bitfield.
*    Logically, the code does the following:
*      pWorkBlock->aAssign[Index] = v;
*/
static void _WB_WriteAssignment(const NAND_INST * pInst, const NAND_WORK_BLOCK * pWorkBlock, unsigned Index, unsigned v) {
  FS_BITFIELD_WriteEntry(SEGGER_PTR2PTR(U8, pWorkBlock->paAssign), Index, pInst->SPB_Shift, v);
}

/*********************************************************************
*
*       _WB_GetAssignmentSize
*
*  Function description
*    Returns the size of the assignment table of a work block.
*/
static unsigned _WB_GetAssignmentSize(const NAND_INST * pInst) {
  unsigned v;

  v = FS_BITFIELD_CalcSize(1uL << pInst->SPB_Shift, pInst->SPB_Shift);
  return v;
}

/*********************************************************************
*
*       _FindFreeSectorInWorkBlock
*
*  Function description
*    Locate a free sector in a work block.
*    If available we try to locate the brsi at the "native" position, meaning physical brsi = logical brsi,
*    because this leaves the option to later convert the work block into a data block without copying the data.
*
*  Return value
*    != BRSI_INVALID  brsi    If free sector has been found
*    == BRSI_INVALID          No free sector
*/
static unsigned _FindFreeSectorInWorkBlock(const NAND_INST * pInst, const NAND_WORK_BLOCK * pWorkBlock, unsigned brsi) {
  unsigned SectorsPerBlock;
  int      i;

  SectorsPerBlock = 1uL << pInst->SPB_Shift;
#if FS_NAND_SUPPORT_FAST_WRITE
  {
    unsigned NumSectorsFree;
    unsigned NumSectorsFreeInWB;

    NumSectorsFree  = pInst->NumSectorsFree;
    if (NumSectorsFree != 0u) {
      //
      // Count the number of free sectors in the work block.
      //
      NumSectorsFreeInWB = _WB_GetNumFreeSectors(pInst, pWorkBlock);
      if (NumSectorsFreeInWB <= NumSectorsFree) {
        return BRSI_INVALID;      // Number of free sectors reached the threshold. Return that no other free sectors are available.
      }
    }
  }
#endif // FS_NAND_SUPPORT_FAST_WRITE
  //
  // Preferred position is the real position within the block. So we first check if it is available.
  //
  if (_WB_IsSectorWritten(pWorkBlock, brsi) == 0) {
    return brsi;
  }
  //
  // Preferred position is taken. Let's use first free position.
  //
  i = _Find0BitInArray(pWorkBlock->paIsWritten, 1, SectorsPerBlock - 1u);    // Returns bit position (1 ... SectorsPerBlock - 1) if a 0-bit has been found, else -1
  if (i > 0) {
    return (unsigned)i;
  }
  return BRSI_INVALID;     // No free Sector in this block
}

/*********************************************************************
*
*       _WB_RemoveFromList
*
*  Function description
*    Removes a given work block from list of work blocks.
*/
static void _WB_RemoveFromList(const NAND_WORK_BLOCK * pWorkBlock, NAND_WORK_BLOCK ** ppFirst) {
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  {
    NAND_WORK_BLOCK * pWorkBlockToCheck;

    //
    // Make sure that the work block is contained in the list.
    //
    pWorkBlockToCheck = *ppFirst;
    while (pWorkBlockToCheck != NULL) {
      if (pWorkBlockToCheck == pWorkBlock) {
        break;
      }
      pWorkBlockToCheck = pWorkBlockToCheck->pNext;
    }
    if (pWorkBlockToCheck == NULL) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NAND: Work block is not contained in the list."));
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);
    }
  }
#endif
  //
  // Unlink Front: From head or previous block
  //
  if (pWorkBlock == *ppFirst) {                             // This WB first in list ?
    *ppFirst = pWorkBlock->pNext;
  } else {
    pWorkBlock->pPrev->pNext = pWorkBlock->pNext;
  }
  //
  // Unlink next if pNext is valid
  //
  if (pWorkBlock->pNext != NULL) {
    pWorkBlock->pNext->pPrev = pWorkBlock->pPrev;
  }
}

/*********************************************************************
*
*       _WB_AddToList
*
*  Function description
*    Adds a given work block to the beginning of the list of work block descriptors.
*/
static void _WB_AddToList(NAND_WORK_BLOCK * pWorkBlock, NAND_WORK_BLOCK ** ppFirst) {
  NAND_WORK_BLOCK * pPrevFirst;

  #if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  {
    NAND_WORK_BLOCK * pWorkBlockToCheck;

    //
    // Make sure that the work block is not already contained in the list.
    //
    pWorkBlockToCheck = *ppFirst;
    while (pWorkBlockToCheck != NULL) {
      if (pWorkBlockToCheck == pWorkBlock) {
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NAND: Work block is already contained in the list."));
        FS_X_PANIC(FS_ERRCODE_INVALID_PARA);
      }
      pWorkBlockToCheck = pWorkBlockToCheck->pNext;
    }
  }
#endif
  pPrevFirst = *ppFirst;
  pWorkBlock->pPrev = NULL;    // First entry
  pWorkBlock->pNext = pPrevFirst;
  if (pPrevFirst != NULL) {
    pPrevFirst->pPrev = pWorkBlock;
  }
  *ppFirst = pWorkBlock;
}

/*********************************************************************
*
*       _WB_RemoveFromUsedList
*
*  Function description
*    Removes a given work block from list of used work blocks.
*/
static void _WB_RemoveFromUsedList(NAND_INST * pInst, const NAND_WORK_BLOCK * pWorkBlock) {
  _WB_RemoveFromList(pWorkBlock, &pInst->pFirstWorkBlockInUse);
}

/*********************************************************************
*
*       _WB_AddToUsedList
*
*  Function description
*    Adds a given work block to the list of used work blocks.
*/
static void _WB_AddToUsedList(NAND_INST * pInst, NAND_WORK_BLOCK * pWorkBlock) {
  _WB_AddToList(pWorkBlock, &pInst->pFirstWorkBlockInUse);
}

/*********************************************************************
*
*       _WB_RemoveFromFreeList
*
*  Function description
*    Removes a given work block from list of free work blocks.
*/
static void _WB_RemoveFromFreeList(NAND_INST * pInst, const NAND_WORK_BLOCK * pWorkBlock) {
  _WB_RemoveFromList(pWorkBlock, &pInst->pFirstWorkBlockFree);
}

/*********************************************************************
*
*       _WB_AddToFreeList
*
*  Function description
*    Adds a given work block to the list of free work blocks.
*/
static void _WB_AddToFreeList(NAND_INST * pInst, NAND_WORK_BLOCK * pWorkBlock) {
  pWorkBlock->pbi = 0;          // Required for the fast write feature so that we can differentiate between work block descriptors which have an erased block assigned to them.
  _WB_AddToList(pWorkBlock, &pInst->pFirstWorkBlockFree);
}

#if FS_NAND_SUPPORT_FAST_WRITE

/*********************************************************************
*
*       _WB_AddErasedToFreeList
*
*  Function description
*    Adds a given work block to the list of free work blocks.
*    Identical to _WB_AddToFreeList() except that it does not
*    invalidate the index of the physical sector.
*/
static void _WB_AddErasedToFreeList(NAND_INST * pInst, NAND_WORK_BLOCK * pWorkBlock) {
  _WB_AddToList(pWorkBlock, &pInst->pFirstWorkBlockFree);
}

/*********************************************************************
*
*       _WB_RemoveFromErasedList
*
*  Function description
*    Removes a given work block from list of used work blocks that are free and erased.
*    Typ. used by the fast write feature.
*/
static void _WB_RemoveFromErasedList(NAND_INST * pInst, const NAND_WORK_BLOCK * pWorkBlock) {
  _WB_RemoveFromList(pWorkBlock, &pInst->pFirstWorkBlockErased);
}

/*********************************************************************
*
*       _WB_AddToErasedList
*
*  Function description
*    Adds a given work block to the list of erased and free work blocks.
*    Typ. used by the fast write feature.
*/
static void _WB_AddToErasedList(NAND_INST * pInst, NAND_WORK_BLOCK * pWorkBlock) {
  _WB_AddToList(pWorkBlock, &pInst->pFirstWorkBlockErased);
}

#endif // FS_NAND_SUPPORT_FAST_WRITE

/*********************************************************************
*
*       _IsSectorDataWritten
*
*   Function description
*     Checks whether the data of a sector was changed at least one time.
*
*   Parameters
*     pInst         [IN]  Driver instance.
*     SectorIndex   Index of the sector to be checked.
*
*   Return value
*     ==1     Sector has been written.
*     ==0     Sector empty.
*/
static int _IsSectorDataWritten(NAND_INST * pInst, U32 SectorIndex) {
  U32 ecc;

  //
  // We check the ECC validity of the first 256 bytes to determine if the sector has been written.
  //
  (void)_ReadSpareIntoStaticBuffer(pInst, SectorIndex);
  ecc = FS__ECC256_Load(_pSpareAreaData + SPARE_OFF_ECC00);
  return FS__ECC256_IsValid(ecc);
}

/*********************************************************************
*
*       _IsSectorDataInvalidated
*
*   Function description
*     Checks whether the data of a sector has been invalidated by a "free sectors" command.
*
*   Parameters
*     pInst         [IN]  Driver instance.
*     SectorIndex   Index of the sector to be checked.
*
*   Return value
*     ==1     Sector data invalidated.
*     ==0     Sector data not invalidated.
*
*   Notes
*       (1) First sector in the block requires special treatment.
*           The spare area of this sector is used up with block related information.
*           We store the flag which indicates whether the sector data was invalidated
*           or not in the spare area of the second sector at the next available byte offset.
*/
static int _IsSectorDataInvalidated(NAND_INST * pInst, U32 SectorIndex) {
  U32      Mask;
  U8       Data8;
  unsigned Off;

  //
  // First check if the sector data has been invalidated by a "free sectors" command.
  //
  Off  = SPARE_OFF_SECTOR_FREE;
  Mask = (1uL << pInst->SPB_Shift) - 1u;
  if ((SectorIndex & Mask) == 0u) {     // First sector in the block ? (See note 1)
    Off = SPARE_OFF_SECTOR0_FREE;
    ++SectorIndex;
  }
  (void)_ReadSpareByte(pInst, SectorIndex, &Data8, Off);
  if (Data8 == 0u) {                    // Reversed logic: 0 means invalidated
    return 1;
  }
  return  0;
}

/*********************************************************************
*
*       _IsSectorDataInvalidatedFast
*
*   Function description
*     Same as _IsSectorDataInvalidated() above, but assumes that the spare area for the given sector has already been read into the static buffer
*/
static int _IsSectorDataInvalidatedFast(NAND_INST * pInst, U32 SectorIndex) {
  U32 Mask;
  U8  Data8;

  //
  // First check if the sector data has been invalidated by a "free sectors" command.
  //
  Mask = (1uL << pInst->SPB_Shift) - 1u;
  if ((SectorIndex & Mask) == 0u) {     // First sector in the block ?
    (void)_ReadSpareByte(pInst, SectorIndex + 1u, &Data8, SPARE_OFF_SECTOR0_FREE);
  } else {
    Data8 = *(_pSpareAreaData + SPARE_OFF_SECTOR_FREE);
  }
  if (Data8 == 0u) {                    // Reversed logic: 0 means invalidated
    return 1;
  }
  return 0;
}

#if FS_NAND_SUPPORT_TRIM

/*********************************************************************
*
*       _InvalidateSectorData
*
*  Function description
*    Sets the flag which indicates that the data of a sector is not valid anymore.
*    Typ. called by the "free sectors" operation.
*
*  Parameters
*    pInst          [IN]  Driver instance.
*    SectorIndex    Index of the sector to be marked as free.
*
*  Return value
*    ==0      OK, sector data has been invalidated.
*    !=0      An error occurred.
*/
static int _InvalidateSectorData(NAND_INST * pInst, U32 SectorIndex) {
  U32      Mask;
  U8       Data8;
  unsigned Off;
  int      r;

  //
  // First sector in the block requires special treatment (see Note 1 of _IsSectorDataInvalidated()).
  //
  Off   = SPARE_OFF_SECTOR_FREE;
  Mask  = (1uL << pInst->SPB_Shift) - 1u;
  Data8 = 0;
  if ((SectorIndex & Mask) == 0u) {     // First sector in the block ?
    Off = SPARE_OFF_SECTOR0_FREE;
    ++SectorIndex;
  }
  r = _WriteSpareByte(pInst, SectorIndex, Data8, Off);
  return r;
}

/*********************************************************************
*
*       _InvalidateSectorDataFast
*
*  Function description
*    Same as _InvalidateSectorData() above, but assumes that the caller
*    writes the spare data to medium from static buffer.
*
*  Parameters
*    pInst          [IN]  Driver instance.
*    SectorIndex    Index of the sector to be marked as free.
*
*  Return value
*    ==0      OK, sector data has been invalidated.
*    !=0      An error occurred.
*/
static int _InvalidateSectorDataFast(NAND_INST * pInst, U32 SectorIndex) {
  U32 Mask;
  U8  Data8;
  int r;

  r     = 0;          // Set to indicate success.
  Mask  = (1uL << pInst->SPB_Shift) - 1u;
  Data8 = 0;
  if ((SectorIndex & Mask) == 0u) {     // First sector in the block ?
    r = _WriteSpareByte(pInst, SectorIndex + 1u, Data8, SPARE_OFF_SECTOR0_FREE);
  } else {
    *(_pSpareAreaData + SPARE_OFF_SECTOR_FREE) = Data8;
  }
  return r;
}

#endif // FS_NAND_SUPPORT_TRIM

/*********************************************************************
*
*       _brsiLog2Phy
*
*  Function description
*    Converts a logical brsi (block relative sector index) into a physical brsi.
*
*  Return value
*    brsi             If free sector has been found.
*    BRSI_INVALID     On error: No free sector.
*/
static unsigned _brsiLog2Phy(NAND_INST * pInst, const NAND_WORK_BLOCK * pWorkBlock, unsigned LogBRSI) {     //lint -efunc(818, _brsiLog2Phy) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory]. Rationale: the statistical counters stored in the driver instance are updated in debug builds.
  unsigned PhyBRSI;

  //
  //  In case of logical sector index <> 0 we need to check the physical sector index.
  //  The physical sector index of such a logical sector index will never be zero,
  //  since we do not assign such a value to a logical sector index.
  //  (see function _FindFreeSectorInWorkBlock)
  //
  if (LogBRSI != 0u) {
    PhyBRSI = _WB_ReadAssignment(pInst, pWorkBlock, LogBRSI);
    if (PhyBRSI == 0u) {
      return BRSI_INVALID;
    }
    return PhyBRSI;
  }
  //
  // LogBRSI == 0 (First sector in block) requires special handling.
  //
  if (_WB_IsSectorWritten(pWorkBlock, 0) == 0) {
    return BRSI_INVALID;
  }
  PhyBRSI = _WB_ReadAssignment(pInst, pWorkBlock, 0);
#if FS_NAND_SUPPORT_TRIM
  //
  // PhyBRSI == 0 has 2 different meanings:
  //    1. Logical sector 0 is stored on the first physical sector
  //    2. Logical sector 0 has been invalidated
  // We have to differentiate between these two conditions and return the correct value.
  //
  if (PhyBRSI == 0u) {
    U32 PhySectorIndex;

    PhyBRSI        = BRSI_INVALID;
    PhySectorIndex = _BlockIndex2SectorIndex(pInst, pWorkBlock->pbi);
    if (_IsSectorDataInvalidated(pInst, PhySectorIndex) == 0) {
      PhyBRSI = 0;
    }
  }
#endif // FS_NAND_SUPPORT_TRIM
  return PhyBRSI;
}

#if FS_SUPPORT_TEST

/*********************************************************************
*
*       _IsPBIAssignedToWorkBlock
*
*  Function description
*    Checks if the specified physical block is used as work block.
*
*  Parameters
*    pbi          Index of the physical block to be checked.
*    pWorkBlock   First work block to be checked.
*
*  Return value
*    ==0      The block is not used as work block.
*    ==1      The block is used as work block.
*/
static int _IsPBIAssignedToWorkBlock(unsigned pbi, const NAND_WORK_BLOCK * pWorkBlock) {
  while (pWorkBlock != NULL) {
    if (pbi == pWorkBlock->pbi) {
      return 1;
    }
    pWorkBlock = pWorkBlock->pNext;
  }
  return 0;
}

/*********************************************************************
*
*       _IsPBIAssignedToDataBlock
*
*  Function description
*    Checks if the specified block is used as data block.
*
*  Parameters
*    pInst        Driver instance.
*    pbi          Index of the physical block to be checked.
*    lbiStart     Index of the first logical block to be checked.
*
*  Return value
*    ==0      The block is not used as work block.
*    ==1      The block is used as work block.
*/
static int _IsPBIAssignedToDataBlock(const NAND_INST * pInst, unsigned pbi, unsigned lbiStart) {
  unsigned lbi;
  unsigned pbiToCheck;

  for (lbi = lbiStart; lbi < pInst->NumLogBlocks; ++lbi) {
    pbiToCheck = _L2P_Read(pInst, lbi);
    if (pbiToCheck == pbi) {
      return 1;
    }
  }
  return 0;
}

/*********************************************************************
*
*       _CheckConsistency
*
*  Function description
*    Checks the consistency of internal data structures.
*
*  Parameters
*    pInst        Driver instance.
*
*  Return value
*    ==0      No problems found.
*    !=0      Inconsistencies found.
*/
static int _CheckConsistency(const NAND_INST * pInst) {
  unsigned           lbi;
  unsigned           pbi;
  NAND_WORK_BLOCK  * pWorkBlock;

  if (pInst->IsLLMounted == 0u) {
    return 0;                   // OK, NAND flash not mounted yet.
  }
  //
  // Check if all the pbi's of data blocks are marked as used.
  //
  for (lbi = 0; lbi < pInst->NumLogBlocks; ++lbi) {
    pbi = _L2P_Read(pInst, lbi);
    if (pbi != 0u) {
      if (_IsBlockFree(pInst, pbi) != 0) {
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND: _CheckConsistency: Data block marked a free (pbi: %u)", pbi));
        return 1;               // Error, data block is marked as free.
      }
      //
      // Check if the physical blocks that are assigned to work blocks are not assigned to a data block.
      //
      if (_IsPBIAssignedToWorkBlock(pbi, pInst->pFirstWorkBlockInUse) != 0) {
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND: _CheckConsistency: Work block used as data block (pbi: %u)", pbi));
        return 1;               // Error, work block is used as data block.
      }
      //
      // Check if the pbi's of data blocks are unique.
      //
      if (_IsPBIAssignedToDataBlock(pInst, pbi, lbi + 1u) != 0) {
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND: _CheckConsistency: Duplicated data block found (pbi: %u)\n", pbi));
        return 1;               // Error, same physical block assigned to 2 data blocks.
      }
    }
  }
  //
  // Check if the pbi's of work blocks are marked as used.
  //
  pWorkBlock = pInst->pFirstWorkBlockInUse;       // Start with the first block in use.
  while (pWorkBlock != NULL) {
    pbi = pWorkBlock->pbi;
    if (_IsBlockFree(pInst, pbi) != 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND: _CheckConsistency: Work block is marked as free (pbi: %u)\n", pbi));
      return 1;                 // Error, work block is marked as free.
    }
    pWorkBlock = pWorkBlock->pNext;
    //
    // Check if the pbi's of work blocks are unique.
    //
    if (_IsPBIAssignedToWorkBlock(pbi, pWorkBlock) != 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND: _CheckConsistency: Duplicated work block found (pbi: %u)\n", pbi));
      return 1;                 // Error, same physical block is assigned to 2 work blocks.
    }
  }
#if FS_NAND_SUPPORT_FAST_WRITE
  //
  // Check if the PBI's of the erased work blocks are marked as used.
  //
  pWorkBlock = pInst->pFirstWorkBlockErased;
  while (pWorkBlock != NULL) {
    pbi = pWorkBlock->pbi;
    if (_IsBlockFree(pInst, pbi) != 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND: _CheckConsistency: Erased work block is marked as free (pbi: %u)\n", pbi));
      return 1;                 // Error, erased work block is marked as free.
    }
    pWorkBlock = pWorkBlock->pNext;
    //
    // Check if the pbi's of work blocks are unique.
    //
    if (_IsPBIAssignedToWorkBlock(pbi, pWorkBlock) != 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND: _CheckConsistency: Duplicated erased work block found (pbi: %u)\n", pbi));
      return 1;                 // Error, same physical block is assigned to 2 work blocks.
    }
  }
#endif
  return 0;                     // OK, no errors found.
}

#endif // FS_SUPPORT_TEST

/*********************************************************************
*
*       _AllocWorkBlockDesc
*
*  Function description
*    Allocates a work block descriptor from the array in the pInst structure.
*/
static NAND_WORK_BLOCK * _AllocWorkBlockDesc(NAND_INST * pInst, unsigned lbi) {
  NAND_WORK_BLOCK * pWorkBlock;

#if FS_NAND_SUPPORT_FAST_WRITE
  {
    NAND_WORK_BLOCK * pWorkBlockErased;

    //
    // Prefer a pre-erased work block.
    // Newly erased work blocks are inserted at the beginning of the list of free work blocks.
    // Take the last one from the list to make sure that all the erased blocks are used.
    //
    pWorkBlockErased  = NULL;
    pWorkBlock        = pInst->pFirstWorkBlockFree;
    while (pWorkBlock != NULL) {
      if (pWorkBlock->pbi != 0u) {
        pWorkBlockErased = pWorkBlock;   // We found a pre-erased block.
      }
      pWorkBlock = pWorkBlock->pNext;
    }
    //
    // Take the first available work block if we could not find any pre-erased one.
    //
    if (pWorkBlockErased == NULL) {
      pWorkBlock = pInst->pFirstWorkBlockFree;
    } else {
      pWorkBlock = pWorkBlockErased;
    }
  }
#else
  //
  // Take the first free work block that is available.
  //
  pWorkBlock = pInst->pFirstWorkBlockFree;
#endif // FS_NAND_SUPPORT_FAST_WRITE
  if (pWorkBlock != NULL) {
    unsigned NumBytes;

    //
    // Initialize work block descriptor,
    // mark it as in use and add it to the list
    //
    _WB_RemoveFromFreeList(pInst, pWorkBlock);
    _WB_AddToUsedList(pInst, pWorkBlock);
    pWorkBlock->lbi = lbi;
    NumBytes = 1;
    if (pInst->SPB_Shift > 3u) {
      NumBytes = 1uL << (pInst->SPB_Shift - 3u);
    }
    FS_MEMSET(pWorkBlock->paIsWritten, 0, NumBytes);    // Mark all entries as unused: Work block does not yet contain any sectors (data)
    NumBytes = _WB_GetAssignmentSize(pInst);
    FS_MEMSET(pWorkBlock->paAssign, 0, NumBytes);       // Make sure that no old assignment info from previous descriptor is in the table
  }
  return pWorkBlock;
}

/*********************************************************************
*
*       _MarkBlock
*
*  Function description
*    Marks a block as block of given type
*    We write only the 16 bytes actually required, even though the spare area may be larger (64 bytes for large-page NANDS with 2K sectors)
*
*  Parameters
*    pInst        [IN]  Driver instance.
*    pbi          Index of physical block to write.
*    lbi          Index of logical block assigned to physical block.
*    EraseCnt     Number of times the block has been erased.
*    DataStat     Status of the data in the block (valid, invalid, bad).
*
*  Return values
*    ==0   OK
*    !=0   A write error occurred (recoverable).
*/
static int _MarkBlock(NAND_INST * pInst, unsigned pbi, unsigned lbi, U32 EraseCnt, unsigned DataStat) {
  unsigned NumBytes;

  NumBytes = 16;
  _ClearStaticSpareArea(NumBytes);
  _StoreEraseCnt(pInst, EraseCnt);
  _StoreLBI(lbi);
  *(_pSpareAreaData + SPARE_OFF_DATA_STATUS) = (U8)DataStat;
  return _WriteSpare(pInst, _BlockIndex2SectorIndex(pInst, pbi), _pSpareAreaData, 0, NumBytes);
}

/*********************************************************************
*
*       _MarkAsWorkBlock
*
*  Function description
*    Marks a block as work block.
*
*  Parameters
*    pInst        [IN]  Driver instance.
*    pbi          Index of physical block to write.
*    lbi          Index of logical block assigned to physical block.
*    EraseCnt     Number of times the block has been erased.
*
*  Return values
*    ==0    OK
*    !=0    A write error occurred (recoverable).
*/
static int _MarkAsWorkBlock(NAND_INST * pInst, unsigned pbi, unsigned lbi, U32 EraseCnt) {
  return _MarkBlock(pInst, pbi, lbi, EraseCnt, 0xFu | (DATA_STAT_WORK << 4));
}

/*********************************************************************
*
*       _MarkAsDataBlock
*
*  Function description
*    Marks a block as data block.
*
*  Parameters
*    pInst        [IN]  Driver instance.
*    pbi          Index of physical block to write.
*    lbi          Index of logical block assigned to physical block.
*    EraseCnt     Number of times the block has been erased.
*    DataCnt      Number of times the data block was copied.
*                 It is used to determine which of two data blocks with the same
*                 LBI is the most recent one (see _IsBlockDataMoreRecent).
*
*  Return values
*    ==0    OK
*    !=0    A write error occurred (recoverable).
*/
static int _MarkAsDataBlock(NAND_INST * pInst, unsigned pbi, unsigned lbi, U32 EraseCnt, unsigned DataCnt) {
  return _MarkBlock(pInst, pbi, lbi, EraseCnt, (DataCnt & 0xFu) | ((unsigned)DATA_STAT_VALID << 4));
}

/*********************************************************************
*
*       _OnFatalError
*
*  Function description
*    Called when a fatal error occurs. It switches to read-only mode
*    and sets the error flag.
*
*  Parameters
*    pInst              [IN]  Driver instance.
*    ErrorType          Identifies the error.
*    ErrorSectorIndex   Index of the physical sector where the error occurred.
*/
static void _OnFatalError(NAND_INST * pInst, int ErrorType, unsigned ErrorSectorIndex) {
  U8                       * pPageBuffer;
  FS_NAND_FATAL_ERROR_INFO   FatalErrorInfo;
  int                        Result;
  int                        MarkAsReadOnly;

  MarkAsReadOnly          = 0;              // Per default, leave the NAND flash writable.
  pInst->HasFatalError    = 1;
  pInst->ErrorType        = (U8)ErrorType;
  pInst->ErrorSectorIndex = ErrorSectorIndex;
  FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND: FATAL error: Error %d occurred on sector %u.", ErrorType, ErrorSectorIndex));
  if (_pfOnFatalError != NULL) {
    //
    // Invoke the callback if registered.
    //
    FS_MEMSET(&FatalErrorInfo, 0, sizeof(FatalErrorInfo));
    FatalErrorInfo.Unit             = pInst->Unit;
    FatalErrorInfo.ErrorType        = (U8)ErrorType;
    FatalErrorInfo.ErrorSectorIndex = ErrorSectorIndex;
    Result = _pfOnFatalError(&FatalErrorInfo);
    if (Result == 0) {                  // Did application request to mark the NAND flash as read-only?
      MarkAsReadOnly = 1;
    }
  }
  //
  // If requested, mark the NAND flash as read-only.
  //
  if (MarkAsReadOnly != 0) {
    //
    // Do not write to NAND flash if it is write protected.
    //
    if (pInst->IsWriteProtected == 0u) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND: Switching permanently to read-only mode."));
      pInst->IsWriteProtected = 1;
      //
      // Save the write protected status and the error information into the first block
      //
      pPageBuffer = (U8 *)_pSectorBuffer;
      FS_MEMSET(pPageBuffer, 0xFF, pInst->BytesPerSector);
      FS_StoreU16BE(pPageBuffer + INFO_OFF_IS_WRITE_PROTECTED, 0);      // Inverted, 0 means write protected
      FS_StoreU16BE(pPageBuffer + INFO_OFF_HAS_FATAL_ERROR,    0);      // Inverted, 0 means has fatal error
      FS_StoreU16BE(pPageBuffer + INFO_OFF_FATAL_ERROR_TYPE,         (unsigned)ErrorType);
      FS_StoreU32BE(pPageBuffer + INFO_OFF_FATAL_ERROR_SECTOR_INDEX, ErrorSectorIndex);
      _ClearStaticSpareArea((unsigned)pInst->BytesPerSector >> 5);
      (void)_WriteSector(pInst, _pSectorBuffer, SECTOR_INDEX_ERROR_INFO);
    }
  }
}

/*********************************************************************
*
*       _MakeBlockAvailable
*
*  Function description
*    Marks the data of a block as invalid and puts it in the list of free blocks.
*
*  Parameters
*    pInst        [IN]  Driver instance.
*    pbi          Index of the physical block to be marked as free.
*    EraseCnt     Number of times the block has been erased. This information
*                 is used to update the NumBlocksEraseCntMin.
*
*  Return value
*    ==0    OK
*    !=0    A write error occurred, recoverable.
*/
static int _MakeBlockAvailable(NAND_INST * pInst, unsigned pbi, U32 EraseCnt) {
  int r;
  U8  DataStat;
  U32 SectorIndex;

  r = 0;
  //
  // Block 0 stores only management information and can never be freed
  //
  if (pbi != 0u) {
    //
    // Mark block as invalid and put it to the free list.
    //
    SectorIndex = _BlockIndex2SectorIndex(pInst, pbi);
    DataStat    = DATA_STAT_INVALID << 4;
    r = _WriteSpareByte(pInst, SectorIndex, DataStat, SPARE_OFF_DATA_STATUS);
    _MarkBlockAsFree(pInst, pbi);
    if ((pInst->NumBlocksEraseCntMin != 0u) && (pInst->EraseCntMin == EraseCnt)) {
      pInst->NumBlocksEraseCntMin--;
    }
  }
  return r;
}

/*********************************************************************
*
*       _CopySectorWithECC
*
*  Function description
*    Copies the data of a sector into another sector. During copy
*    the ECC of the source data is checked.
*
*  Parameters
*    pInst              [IN]  Driver instance.
*    SectorIndexSrc     Source physical sector index.
*    SectorIndexDest    Destination physical sector index.
*    brsi               Block relative index of the copied sector.
*
*  Return value
*    RESULT_NO_ERROR              OK
*    RESULT_1BIT_CORRECTED        OK
*    RESULT_ERROR_IN_ECC          OK
*    RESULT_UNCORRECTABLE_ERROR   Error, fatal
*    RESULT_READ_ERROR            Error, fatal
*    RESULT_WRITE_ERROR           Error, recoverable
*/
static int _CopySectorWithECC(NAND_INST * pInst, U32 SectorIndexSrc, U32 SectorIndexDest, unsigned brsi) {
  int rRead;
  int rWrite;

  rRead = _ReadSectorWithECC(pInst, _pSectorBuffer, SectorIndexSrc);
  if ((rRead == RESULT_NO_ERROR)       ||
      (rRead == RESULT_1BIT_CORRECTED) ||
      (rRead == RESULT_ERROR_IN_ECC)) {
#if FS_NAND_SUPPORT_TRIM
    if (_IsSectorDataInvalidatedFast(pInst, SectorIndexSrc) != 0) {
      return RESULT_NO_ERROR;       // Data have been invalidated by a "free sectors" command
    }
#endif
    //
    // Leave the ECC in the spare area to avoid computing it again in the write function.
    //
    _ClearStaticSpareAreaExceptECC((unsigned)pInst->BytesPerSector >> 5);
    //
    // A bit error in ECC is not corrected by the ECC check routine.
    // It must be re-computed to avoid propagating the bit error to destination sector.
    //
    if (rRead == RESULT_ERROR_IN_ECC) {
      _ComputeAndStoreECC(pInst, _pSectorBuffer, _pSpareAreaData);
    }
    //
    // Important when we clean a work block "in place". In case of a power fail the sector is marked as used.
    //
    if (brsi != BRSI_INVALID) {
      _StoreBRSI(brsi);
    }
    rWrite = _WriteDataSpare(pInst, SectorIndexDest, _pSectorBuffer, 0, pInst->BytesPerSector, _pSpareAreaData, 0, (unsigned)pInst->BytesPerSector >> 5);
    if (rWrite != 0) {
      return RESULT_WRITE_ERROR;
    }
    IF_STATS(pInst->StatCounters.CopySectorCnt++);
    return rRead;
  }
  if (rRead < 0) {
    return RESULT_NO_ERROR;   // Sector blank, nothing to copy
  }
  _OnFatalError(pInst, rRead, SectorIndexSrc);
  return rRead;             // Unrecoverable error
}

/*********************************************************************
*
*       _CountDataBlocksWithEraseCntMin
*
*  Function description
*    Goes through all blocks and counts the data blocks with the
*    lowest erase cnt.
*
*  Parameters
*    pInst        [IN]  Driver instance.
*    pEraseCnt    [OUT] Minimum erase count.
*    pPBI         [OUT] Index of the first data block with the min erase count.
*
*  Return value
*    Number of data blocks found with a min erase count
*/
static U32 _CountDataBlocksWithEraseCntMin(NAND_INST * pInst, U32 * pEraseCnt, unsigned * pPBI) {
  unsigned iBlock;
  unsigned pbi;
  U32      EraseCntMin;
  U32      SectorIndex;
  U32      EraseCnt;
  U32      NumBlocks;

  pbi         = 0;
  EraseCntMin = ERASE_CNT_INVALID;
  NumBlocks   = 0;
  //
  // Read and compare the erase count of all data blocks except the first one
  // which stores only management information
  //
  for (iBlock = 1; iBlock < pInst->NumPhyBlocks; ++iBlock) {
    U8 DataStat;

    if (_IsBlockFree(pInst, iBlock) != 0) {
      continue;       // Block is not used.
    }
    if (_IsBlockBad(pInst, iBlock) != 0) {
      continue;       // Block is marked as defect.
    }
    SectorIndex = _BlockIndex2SectorIndex(pInst, iBlock);
    (void)_ReadSpareIntoStaticBuffer(pInst, SectorIndex);
    DataStat    = _pSpareAreaData[SPARE_OFF_DATA_STATUS];
    //
    // Compare the erase count of data blocks only.
    //
    if ((DataStat >> 4) == DATA_STAT_VALID) {
      EraseCnt = _LoadEraseCnt(pInst, _pSpareAreaData);
      if ((EraseCntMin == ERASE_CNT_INVALID) || (EraseCnt < EraseCntMin)) {
        pbi         = iBlock;
        EraseCntMin = EraseCnt;
        NumBlocks   = 1;
      } else {
        if (EraseCnt == EraseCntMin) {
          ++NumBlocks;
        }
      }
    }
  }
  *pEraseCnt = EraseCntMin;
  *pPBI      = pbi;
  return NumBlocks;
}

/*********************************************************************
*
*       _FindDataBlockByEraseCnt
*
*  Function description
*    Goes through all data blocks and returns the first one with
*    the given erase count.
*
*  Parameters
*    pInst      [IN]  Driver instance.
*    EraseCnt   Erase count to lookup for.
*
*  Return value
*    == 0   No data block found.
*    != 0   Index of the found data block.
*/
static unsigned _FindDataBlockByEraseCnt(NAND_INST * pInst, U32 EraseCnt) {
  unsigned iBlock;

  //
  // Read and compare the erase count of all data blocks except the first one
  // which stores only management information
  //
  for (iBlock = 1; iBlock < pInst->NumPhyBlocks; ++iBlock) {
    U32 SectorIndex;
    U8  DataStat;
    U32 DataEraseCnt;

    if (_IsBlockFree(pInst, iBlock) != 0) {
      continue;       // Block is not used.
    }
    if (_IsBlockBad(pInst, iBlock) != 0) {
      continue;       // Block is marked as defect.
    }
    SectorIndex = _BlockIndex2SectorIndex(pInst, iBlock);
    (void)_ReadSpareIntoStaticBuffer(pInst, SectorIndex);
    DataStat    = _pSpareAreaData[SPARE_OFF_DATA_STATUS];
    //
    // Search only the data blocks.
    //
    if ((DataStat >> 4) == DATA_STAT_VALID) {
      DataEraseCnt = _LoadEraseCnt(pInst, _pSpareAreaData);
      if (EraseCnt == DataEraseCnt) {
        return iBlock;
      }
    }
  }
  return 0;     // No data block found with the requested erase count
}

/*********************************************************************
*
*       _CheckActiveWearLeveling
*
*  Function description
*    Checks if it is time to perform active wear leveling by
*    comparing the given erase count to the lowest erase count.
*    If so (difference is too big), the index of the data block
*    with the lowest erase count is returned.
*
*  Parameters
*    pInst            [IN]  Driver instance.
*    EraseCnt         Erase count of block to be erased.
*    pDataEraseCnt    [OUT] Erase count of the data block.
*
*  Return value
*    == 0  No data block found.
*    != 0  Physical block index of the data block found.
*/
static unsigned _CheckActiveWearLeveling(NAND_INST * pInst, U32 EraseCnt, U32 * pDataEraseCnt) {
  unsigned pbi;
  I32      EraseCntDiff;
  U32      NumBlocks;
  U32      EraseCntMin;

  //
  // Update pInst->EraseCntMin if necessary
  //
  pbi         = 0;
  NumBlocks   = pInst->NumBlocksEraseCntMin;
  EraseCntMin = pInst->EraseCntMin;
  if (NumBlocks == 0u) {
    NumBlocks = _CountDataBlocksWithEraseCntMin(pInst, &EraseCntMin, &pbi);
    if (NumBlocks == 0u) {
      return 0;     // We don't have any data block yet, it can happen if the flash is empty
    }
    pInst->EraseCntMin          = EraseCntMin;
    pInst->NumBlocksEraseCntMin = NumBlocks;
  }
  //
  // Check if the threshold for active wear leveling is reached
  //
  EraseCntDiff = (I32)EraseCnt - (I32)EraseCntMin;
  if (EraseCntDiff < (I32)pInst->MaxEraseCntDiff) {
    return 0;       // Active wear leveling not necessary, EraseCntDiff is not big enough yes
  }
  if (pbi == 0u) {
    pbi = _FindDataBlockByEraseCnt(pInst, EraseCntMin);
  }
  *pDataEraseCnt = EraseCntMin;
  --pInst->NumBlocksEraseCntMin;
  return pbi;
}

/*********************************************************************
*
*       _PerformPassiveWearLeveling
*
*  Function description
*    Searches for the next free block and returns its index. The block
*    is marked as allocated in the internal list.
*
*  Parameters
*    pInst        [IN]  Driver instance.
*    pEraseCnt    [OUT] Erase count of the allocated block.
*
*  Return value
*    == 0   No more free blocks.
*    != 0   Physical block index of the allocated block.
*/
static unsigned _PerformPassiveWearLeveling(NAND_INST * pInst, U32 * pEraseCnt) {
  unsigned i;
  unsigned iBlock;
  U8       aSpareData[6];   // The erase count is always within the first 6 bytes
  U32      EraseCnt;

  iBlock = pInst->MRUFreeBlock;
  for (i = 0; i < pInst->NumPhyBlocks; i++) {
    if (++iBlock >= pInst->NumPhyBlocks) {
      iBlock = 1;           // Block 0 contains only management information, so we skip it
    }
    if (_IsBlockFree(pInst, iBlock) != 0) {
      (void)_ReadSpare(pInst, _BlockIndex2SectorIndex(pInst, iBlock), aSpareData, 0, sizeof(aSpareData));
      EraseCnt = _LoadEraseCnt(pInst, aSpareData);
      if (EraseCnt == ERASE_CNT_INVALID) {
        EraseCnt = pInst->EraseCntMax;
      }
      *pEraseCnt = EraseCnt;
      _MarkBlockAsAllocated(pInst, iBlock);
      pInst->MRUFreeBlock = iBlock;
      return iBlock;
    }
  }
  FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NAND: FATAL Error: No more free blocks."));
  return 0;               // Error, no more free blocks
}

/*********************************************************************
*
*       _MoveDataBlock
*
*  Function description
*    Copies the contents of a data block into another block.
*    The source data block is marked as free.
*
*  Parameters
*    pInst        [IN]  Driver instance.
*    pbiSrc       Index of the block to be copied.
*    pbiDest      Index of the block where to copy.
*    EraseCnt     Erase count of the source block.
*    pErrorBRSI   [OUT] Index of the sector where the error occurred.
*
*  Return value
*    RESULT_NO_ERROR              OK
*    RESULT_ERROR_IN_ECC          OK
*    RESULT_UNCORRECTABLE_ERROR   Error, fatal
*    RESULT_READ_ERROR            Error, fatal
*    RESULT_WRITE_ERROR           Error, recoverable
*/
static int _MoveDataBlock(NAND_INST * pInst, unsigned pbiSrc, unsigned pbiDest, U32 EraseCnt, unsigned * pErrorBRSI) {
  unsigned SectorsPerBlock;
  unsigned iSector;
  U32      SectorIndexSrc;
  U32      SectorIndexDest;
  int      r;
#if (FS_NAND_MAX_BIT_ERROR_CNT == 0)
  int      ErrorInECC;
#endif // FS_NAND_MAX_BIT_ERROR_CNT == 0
  int      FatalError;
  unsigned lbi;
  unsigned pbi;
  U8       DataStat;

  DataStat        = 0;
#if (FS_NAND_MAX_BIT_ERROR_CNT == 0)
  ErrorInECC      = 0;
#endif // FS_NAND_MAX_BIT_ERROR_CNT == 0
  FatalError      = 0;
  *pErrorBRSI     = BRSI_INVALID;
  SectorIndexSrc  = _BlockIndex2SectorIndex(pInst, pbiSrc);
  SectorIndexDest = _BlockIndex2SectorIndex(pInst, pbiDest);
  SectorsPerBlock = 1uL << pInst->SPB_Shift;
  for (iSector = 0; iSector < SectorsPerBlock; ++iSector) {
    r = _CopySectorWithECC(pInst, SectorIndexSrc + iSector, SectorIndexDest + iSector, BRSI_INVALID);
    switch (r) {
    case RESULT_NO_ERROR:
      // through
    case RESULT_1BIT_CORRECTED:
      break;
    case RESULT_UNCORRECTABLE_ERROR:
      // through
    case RESULT_READ_ERROR:
      // through
    case RESULT_WRITE_ERROR:
      FatalError  = r;      // Remember that we encountered a fatal error. Copy the rest of the sectors in order to recover as much data as possible.
      *pErrorBRSI = iSector;
      break;
    case RESULT_ERROR_IN_ECC:
#if (FS_NAND_MAX_BIT_ERROR_CNT == 0)
      *pErrorBRSI = iSector;
      ErrorInECC = 1;       // Remember that we encountered an error the in ECC.
#endif // FS_NAND_MAX_BIT_ERROR_CNT == 0
      break;
    default:
      //
      // Invalid error code returned.
      //
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS)
      FS_X_Panic(FS_ERRCODE_ASSERT_FAILURE);
#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS
      break;
    }
  }
  //
  // Get the logical block index of the copied block. We do this by searching the logical to physical block translation table.
  //
  for (lbi = 0; lbi < pInst->NumLogBlocks; ++lbi) {
    pbi = _L2P_Read(pInst, lbi);
    if (pbi == pbiSrc) {
      break;
    }
  }
  (void)_ReadSpareByte(pInst, SectorIndexSrc, &DataStat, SPARE_OFF_DATA_STATUS);    // Read the 4-bit data count
  DataStat++;
  (void)_MarkAsDataBlock(pInst, pbiDest, lbi, EraseCnt, DataStat);
  //
  // Update the mapping of physical to logical blocks
  //
  _L2P_Write(pInst, lbi, pbiDest);

  //
  // Fail-safe TP. At this point we have two data blocks with the same LBI
  //
  CALL_TEST_HOOK(pInst->Unit);

  r = RESULT_NO_ERROR;
#if (FS_NAND_MAX_BIT_ERROR_CNT == 0)
  if (ErrorInECC != 0) {
    r = RESULT_ERROR_IN_ECC;
  }
#endif // FS_NAND_MAX_BIT_ERROR_CNT == 0
  if (FatalError != 0) {
    r = FatalError;
  }
  if (r == RESULT_NO_ERROR) {
    (void)_MakeBlockAvailable(pInst, pbiSrc, EraseCnt);
  }
  return r;
}

/*********************************************************************
*
*       _AllocErasedBlock
*
*  Function description
*    Selects a block to write data into. The returned block is erased.
*
*  Parameters
*    pInst        [IN]  Driver instance.
*    pEraseCnt    [OUT] Erase count of the selected block.
*
*  Return value
*    ==0    An error occurred
*    !=0    Physical block index
*/
static unsigned _AllocErasedBlock(NAND_INST * pInst, U32 * pEraseCnt) {
  unsigned pbiAlloc;
  unsigned pbiData;
  U32      AllocEraseCnt;
  U32      DataEraseCnt;
  int      r;
  unsigned ErrorBRSI;

  AllocEraseCnt = pInst->EraseCntMax;   // Just to avoid compiler warning about variable used before being initialized.
  for (;;) {
    //
    // Passive wear leveling. Get the next free block in the row
    //
    pbiAlloc = _PerformPassiveWearLeveling(pInst, &AllocEraseCnt);
    if (pbiAlloc == 0u) {
      _OnFatalError(pInst, RESULT_OUT_OF_FREE_BLOCKS, 0);
      return 0;       // Fatal error, out of free blocks
    }
    r = _EraseBlock(pInst, pbiAlloc);
    if (r != 0) {
      _MarkBlockAsBad(pInst, pbiAlloc, RESULT_ERASE_ERROR, 0);
      continue;       // Error when erasing the block, get a new one
    }
    //
    // OK, we found a free block.
    // Now, let's check if the erase count is too high so we need to use active wear leveling
    //
    pbiData = _CheckActiveWearLeveling(pInst, AllocEraseCnt, &DataEraseCnt);
    ++AllocEraseCnt;
    if (pbiData == 0u) {
      *pEraseCnt = AllocEraseCnt; // No data block has an erase count low enough, keep the block allocated by passive wear leveling
      return pbiAlloc;
    }
    //
    // Perform active wear leveling:
    // A block containing data has a much lower erase count. This block is now moved, giving us a free block with low erase count.
    // This procedure makes sure that blocks which contain data that does not change still take part in the wear leveling scheme.
    //
    r = _MoveDataBlock(pInst, pbiData, pbiAlloc, AllocEraseCnt, &ErrorBRSI);
#if (FS_NAND_MAX_BIT_ERROR_CNT == 0)
    if (r == RESULT_NO_ERROR)
#else
    if ((r == RESULT_NO_ERROR) || (r == RESULT_ERROR_IN_ECC))
#endif
    {
      //
      // The data has been moved and the data block is now free to use
      //
      _MarkBlockAsAllocated(pInst, pbiData);
      r = _EraseBlock(pInst, pbiData);
      if (r != 0) {
        _MarkBlockAsBad(pInst, pbiData, RESULT_ERASE_ERROR, 0);
        continue;                 // Error when erasing the block, get a new one
      }
      ++DataEraseCnt;
      *pEraseCnt = DataEraseCnt;
      return pbiData;
    }
    if ((r == RESULT_UNCORRECTABLE_ERROR) || (r == RESULT_READ_ERROR)) {
      _MarkBlockAsBad(pInst, pbiData, r, ErrorBRSI);
      return 0;                   // Fatal error, no way to recover
    }
    if (r == RESULT_WRITE_ERROR) {
      _MarkBlockAsBad(pInst, pbiAlloc, r, ErrorBRSI);
      continue;                   // Error when writing into the allocated block
    }
#if (FS_NAND_MAX_BIT_ERROR_CNT == 0)
    if (r == RESULT_ERROR_IN_ECC) {
      _MarkBlockAsBad(pInst, pbiData, r, ErrorBRSI);
      continue;                   // Error in the ECC of the data block, get a new free block and a new data block.
    }
#endif
  }
}

/*********************************************************************
*
*       _RecoverDataBlock
*
*  Function description
*    Copies a data block into a free block. Called typ. when an error
*    is found in the ECC.
*
*  Parameters
*    pInst        [IN]  Driver instance.
*    pbiData      Index of the block to be copied.
*
*  Return value
*    ==0    Data block copied.
*    !=0    An error occurred
*/
static int _RecoverDataBlock(NAND_INST * pInst, unsigned pbiData) {
  unsigned pbiAlloc;
  U32      EraseCnt;
  int      r;
  unsigned ErrorBRSI;

  for (;;) {
    //
    // Need a free block where to move the data of the damaged block
    //
    pbiAlloc = _AllocErasedBlock(pInst, &EraseCnt);
    if (pbiAlloc == 0u) {
      return 1;               // Could not allocate an empty block, fatal error
    }
    if (pbiData == pbiAlloc) {
      return 0;               // Block has been already moved in _AllocErasedBlock()
    }
    r = _MoveDataBlock(pInst, pbiData, pbiAlloc, EraseCnt, &ErrorBRSI);
    if (r == RESULT_ERROR_IN_ECC) {
#if (FS_NAND_MAX_BIT_ERROR_CNT == 0)
      _MarkBlockAsBad(pInst, pbiData, r, ErrorBRSI);
#endif
      return 0;               // Data was recovered
    }
    if ((r == RESULT_UNCORRECTABLE_ERROR) || (r == RESULT_READ_ERROR)) {
      _MarkBlockAsBad(pInst, pbiData, r, ErrorBRSI);
      return 1;               // Fatal error, no way to recover
    }
    if (r == RESULT_WRITE_ERROR) {
      _MarkBlockAsBad(pInst, pbiAlloc, r, ErrorBRSI);
      continue;               // Error when writing into the allocated block
    }
    if (r == RESULT_NO_ERROR) {
      return 0;               // Data moved into the new block
    }
  }
}

#if FS_NAND_VERIFY_WRITE

/*********************************************************************
*
*       _VerifySector
*
*  Function description
*    Verifies the data stored to a sector.
*/
static int _VerifySector(NAND_INST * pInst, const U32 * pData, U32 SectorIndex) {
  int r;

  if (pInst->VerifyWrite == 0u) {
    return 0;               // OK, the write verification is disabled.
  }
  r = _ReadSectorWithECC(pInst, _pSectorBuffer, SectorIndex);
  if ((r == RESULT_NO_ERROR) || (r == RESULT_1BIT_CORRECTED) || (r == RESULT_ERROR_IN_ECC)) {
    U32   NumItems;
    U32 * p;

    NumItems = (U32)pInst->BytesPerSector >> 2;
    p        = _pSectorBuffer;
    do {
      if (*p != *pData) {
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND: Verify failed at sector %d.", SectorIndex));
        return 1;           // Verification failed.
      }
      ++p;
      ++pData;
    } while (--NumItems != 0u);
    return 0;               // OK, data is identical.
  }
  return 1;                 // Error, could not read sector.
}

#endif

/*********************************************************************
*
*       _ConvertWorkBlockViaCopy
*
*  Function description
*    Converts a work block into a data block.
*
*  Parameters
*    pInst          [IN]  Driver instance.
*    pWorkBlock     [IN]  Work block to convert.
*    SkipBRSI       BRSI of the sector to ignore when copying.
*    brsi           Index of the sector (relative to block) to be written (BRSI_INVALID means no sector data).
*    pData          [IN]  Sector data to be written (NULL means no sector data).
*
*  Return value
*    ==0   OK, work block converted.
*    !=0   An error occurred.
*
*  Additional information
*    The data of the work block is "merged" with the data of the source
*    block in another free block. The merging operation copies sector-wise
*    the data from work block into the free block. If the sector data is
*    invalid in the work block the sector data from the source block is
*    copied instead. The sectors in the work block doesn't have to be on their
*    native positions.
*/
static int _ConvertWorkBlockViaCopy(NAND_INST * pInst, NAND_WORK_BLOCK * pWorkBlock, unsigned SkipBRSI, unsigned brsi, const U32 * pData) {
  unsigned iSector;
  U16      pbiSrc;
  U16      pbiWork;
  U32      SectorIndexSrc;
  U32      SectorIndexWork;
  unsigned SectorsPerBlock;
  U32      pbiDest;
  U32      SectorIndexDest;
  U32      EraseCntDest;
  U32      EraseCntSrc;
  int      r;
  unsigned brsiSrc;
#if (FS_NAND_MAX_BIT_ERROR_CNT == 0)
  int      ErrorInEccSrc;
  int      ErrorInEccWork;
#endif // FS_NAND_MAX_BIT_ERROR_CNT == 0
  int      FatalErrorSrc;
  int      FatalErrorWork;
  unsigned ErrorBRSI;
  U8       DataStat;
  U8       aSpareData[6];
  int      NumRetries;
  int      IsMarkedAsBad;

  DataStat        = 0;
  EraseCntSrc     = ERASE_CNT_INVALID;
  EraseCntDest    = ERASE_CNT_INVALID;
  pbiWork         = (U16)pWorkBlock->pbi;
  SectorIndexWork = _BlockIndex2SectorIndex(pInst, pbiWork);
  SectorsPerBlock = 1uL << pInst->SPB_Shift;
  NumRetries      = 0;
  for (;;) {
#if (FS_NAND_MAX_BIT_ERROR_CNT == 0)
    ErrorInEccSrc  = 0;
    ErrorInEccWork = 0;
#endif // FS_NAND_MAX_BIT_ERROR_CNT == 0
    FatalErrorSrc  = 0;
    FatalErrorWork = 0;
    ErrorBRSI      = 0;
    //
    // We need to allocate a new block to copy data into
    //
    pbiDest = _AllocErasedBlock(pInst, &EraseCntDest);
    if (pbiDest == 0u) {
      return 1;             // Error, no more free blocks, not recoverable
    }
    //
    // OK, we have an empty block to copy our data into
    //
    pbiSrc          = (U16)_L2P_Read(pInst, pWorkBlock->lbi);
    SectorIndexSrc  = _BlockIndex2SectorIndex(pInst, pbiSrc);
    SectorIndexDest = _BlockIndex2SectorIndex(pInst, pbiDest);
    //
    // Copy the data sector by sector.
    //
    for (iSector = 0; iSector < SectorsPerBlock; iSector++) {
      //
      // The source of the sector data can be (in this order) one of following:
      //   - passed as parameter
      //   - work block
      //   - data block
      //
      brsiSrc = _brsiLog2Phy(pInst, pWorkBlock, iSector);
      if ((brsi == iSector) && (pData != NULL)) {     // Sector data passed as parameter?
        _ClearStaticSpareArea((unsigned)pInst->BytesPerSector >> 5);
        r = _WriteSector(pInst, pData, SectorIndexDest + iSector);
#if FS_NAND_VERIFY_WRITE
        if (r == 0) {
          r = _VerifySector(pInst, (const U32 *)pData, SectorIndexDest + iSector);
        }
#endif
        if (r != 0) {
          _MarkBlockAsBad(pInst, pbiDest, RESULT_WRITE_ERROR, iSector);
          goto Retry;             // Write error occurred, try to find another empty block.
        }
      } else if ((brsiSrc != BRSI_INVALID) && (brsiSrc != SkipBRSI)) {  // Sector data in work block ?
        r = _CopySectorWithECC(pInst, SectorIndexWork + brsiSrc, SectorIndexDest + iSector, BRSI_INVALID);
        if ((r == RESULT_NO_ERROR) || (r == RESULT_1BIT_CORRECTED)) {
          continue;
        }
        if ((r == RESULT_UNCORRECTABLE_ERROR) || (r == RESULT_READ_ERROR)) {
          ErrorBRSI      = iSector;
          FatalErrorWork = r;
          continue;               // Continue the copy operation and try to recover as much data as possible.
        }
        if (r == RESULT_ERROR_IN_ECC) {
#if (FS_NAND_MAX_BIT_ERROR_CNT == 0)
          ErrorBRSI      = iSector;
          ErrorInEccWork = 1;     // Remember we had an error in the ECC of the work block. We will mark it as "bad" later.
#endif
          continue;
        }
        if (r == RESULT_WRITE_ERROR) {
          _MarkBlockAsBad(pInst, pbiDest, r, iSector);
          goto Retry;             // Write error occurred, try to find another empty block
        }
      } else if (SectorIndexSrc != 0u) {          // Sector data in source block ?
        //
        // Copy if we have a data source.
        // Note that when closing a work block which did not yet have a source data block,
        // it can happen that some sector have no source and stay empty.
        //
        r = _CopySectorWithECC(pInst, SectorIndexSrc + iSector, SectorIndexDest + iSector, BRSI_INVALID);
        if ((r == RESULT_NO_ERROR) || (r == RESULT_1BIT_CORRECTED)) {
          continue;
        }
        if ((r == RESULT_UNCORRECTABLE_ERROR) || (r == RESULT_READ_ERROR)) {
          ErrorBRSI     = iSector;
          FatalErrorSrc = r;
          continue;               // Continue the copy operation and try to recover as much data as possible.
        }
        if (r == RESULT_ERROR_IN_ECC) {
#if (FS_NAND_MAX_BIT_ERROR_CNT == 0)
          ErrorBRSI     = iSector;
          ErrorInEccSrc = 1;    // Remember we had an error in the ECC of the data block. We will mark it as "bad" later.
#endif
          continue;
        }
        if (r == RESULT_WRITE_ERROR) {
          _MarkBlockAsBad(pInst, pbiDest, r, iSector);
          goto Retry;           // Write error occurred, try to find another empty block
        }
      } else {
        //
        // The sector data does not have to be copied.
        //
      }
    }
    break;                      // OK, the sectors have been copied.
Retry:
    //
    // An error occurred. Try to copy the data to another block.
    //
    if (NumRetries++ >= FS_NAND_NUM_WRITE_RETRIES) {
      return 1;                                 // Error, too many write retries.
    }
  }
  if (SectorIndexSrc != 0u) {
    (void)_ReadSpare(pInst, SectorIndexSrc, aSpareData, 0, sizeof(aSpareData));   // Read the 4-bit data count and the erase count
    DataStat    = aSpareData[SPARE_OFF_DATA_STATUS];
    EraseCntSrc = _LoadEraseCnt(pInst, aSpareData);
    DataStat++;
  }
  //
  // Mark the newly allocated block as data block
  //
  (void)_MarkAsDataBlock(pInst, pbiDest, pWorkBlock->lbi, EraseCntDest, DataStat);

  //
  // Fail-safe TP. At this point we have two data blocks with the same LBI
  //
  CALL_TEST_HOOK(pInst->Unit);

  //
  // Update the mapping of physical to logical blocks
  //
  _L2P_Write(pInst, pWorkBlock->lbi, pbiDest);
  //
  // Mark former work block as invalid and put it to the free list if there was no error in the ECC.
  //
  r             = 0;
  IsMarkedAsBad = 0;
  if (FatalErrorWork != 0) {
    r = FatalErrorWork;
    _MarkBlockAsBad(pInst, pbiWork, FatalErrorWork, ErrorBRSI);
    IsMarkedAsBad = 1;
  }
#if (FS_NAND_MAX_BIT_ERROR_CNT == 0)
  else {
    if (ErrorInEccWork != 0) {
      _MarkBlockAsBad(pInst, pbiWork, RESULT_ERROR_IN_ECC, ErrorBRSI);
      IsMarkedAsBad = 1;
    }
  }
#endif // FS_NAND_MAX_BIT_ERROR_CNT == 0
  if (IsMarkedAsBad == 0) {
    (void)_MakeBlockAvailable(pInst, pbiWork, ERASE_CNT_INVALID);
  }
  //
  // Change data status of block which contained the "old" data
  // as invalid and put it to the free list if there was no error in ECC
  //
  IsMarkedAsBad = 0;
  if (FatalErrorSrc != 0) {
    r = FatalErrorSrc;
    _MarkBlockAsBad(pInst, pbiSrc, FatalErrorSrc, ErrorBRSI);
    IsMarkedAsBad = 1;
  }
#if (FS_NAND_MAX_BIT_ERROR_CNT == 0)
  else {
    if (ErrorInEccSrc != 0) {
      _MarkBlockAsBad(pInst, pbiSrc, RESULT_ERROR_IN_ECC, ErrorBRSI);
      IsMarkedAsBad = 1;
    }
  }
#endif // FS_NAND_MAX_BIT_ERROR_CNT == 0
  if (IsMarkedAsBad == 0) {
    (void)_MakeBlockAvailable(pInst, pbiSrc, EraseCntSrc);
  }
  //
  // Remove the work block from the internal list.
  //
  _WB_RemoveFromUsedList(pInst, pWorkBlock);
  _WB_AddToFreeList(pInst, pWorkBlock);
  //
  // If required, update the information used for active wear leveling
  //
  {
    U32 EraseCntMin;
    U32 NumBlocksEraseCntMin;

    EraseCntMin          = pInst->EraseCntMin;
    NumBlocksEraseCntMin = pInst->NumBlocksEraseCntMin;
    if (EraseCntDest < EraseCntMin) {
      EraseCntMin          = EraseCntDest;
      NumBlocksEraseCntMin = 1;
    } else {
      if (EraseCntDest == EraseCntMin) {
        ++NumBlocksEraseCntMin;
      }
    }
    pInst->EraseCntMin          = EraseCntMin;
    pInst->NumBlocksEraseCntMin = NumBlocksEraseCntMin;
  }
  IF_STATS(pInst->StatCounters.ConvertViaCopyCnt++);
  return r;
}

/*********************************************************************
*
*       _ConvertWorkBlockInPlace
*
*  Function description
*    Converts a work block into a data block.
*
*  Parameters
*    pInst       [IN]  Driver instance.
*    pWorkBlock  [IN]  Work block to convert.
*    pErrorBRSI  [OUT] Index of the sector where the error occurred.
*                      The number of sectors per block is returned
*                      in case of a fatal error.
*
*  Return value
*    ==0    OK, work block converted.
*    !=0    An error occurred.
*
*  Additional information
*    The function assumes that the sectors are on their native positions.
*    The missing sectors are copied from the source block into the work block.
*/
static int _ConvertWorkBlockInPlace(NAND_INST * pInst, NAND_WORK_BLOCK * pWorkBlock, unsigned * pErrorBRSI) {
  unsigned iSector;
  U16      pbiSrc;
  U32      SectorIndexSrc;
  U32      SectorIndexWork;
  U8       aSpareData[6];   // The erase count is always within the first 6 bytes
  unsigned DataStat;
  unsigned SectorsPerBlock;
  int      r;
  unsigned brsi;
  U32      EraseCnt;

  *pErrorBRSI     = BRSI_INVALID;
  brsi            = BRSI_INVALID;
  DataStat        = 0;
  EraseCnt        = ERASE_CNT_INVALID;
  pbiSrc          = (U16)_L2P_Read(pInst, pWorkBlock->lbi);
  SectorIndexSrc  = _BlockIndex2SectorIndex(pInst, pbiSrc);
  SectorIndexWork = _BlockIndex2SectorIndex(pInst, pWorkBlock->pbi);
  SectorsPerBlock = 1uL << pInst->SPB_Shift;
  //
  // If there is a source block, then use it to "fill the gaps", reading sectors which are empty in the work block
  //
  if (SectorIndexSrc != 0u) {
    for (iSector = 0; iSector < SectorsPerBlock; iSector++) {
      if (_WB_IsSectorWritten(pWorkBlock, iSector) == 0) {
        if (iSector != 0u) {
          brsi = iSector;
        }
        r = _CopySectorWithECC(pInst, SectorIndexSrc + iSector, SectorIndexWork + iSector, brsi);
        if (r == RESULT_NO_ERROR) {
          continue;
        }
        if (r == RESULT_1BIT_CORRECTED) {
#if (FS_NAND_MAX_BIT_ERROR_CNT == 0)
          continue;
#else
          return 1;                         // Stop the conversion here and let the caller perform the conversion via copy.
#endif
        }
        if ((r == RESULT_UNCORRECTABLE_ERROR) || (r == RESULT_READ_ERROR)) {
          *pErrorBRSI = iSector;
          return 1;                         // Stop the conversion here and let the caller perform the conversion via copy.
        }
        if (r == RESULT_ERROR_IN_ECC) {
#if (FS_NAND_MAX_BIT_ERROR_CNT == 0)
          *pErrorBRSI = iSector;
#endif
          return 1;                         // Stop the conversion here and let the caller perform the conversion via copy.
        }
        if (r == RESULT_WRITE_ERROR) {
          *pErrorBRSI = iSector;
          return 1;                         // Stop the conversion here and let the caller perform the conversion via copy.
        }
      }
    }
    //
    // Convert work block into valid data block by changing the data status
    //
    (void)_ReadSpare(pInst, SectorIndexSrc, aSpareData, 0, sizeof(aSpareData));       // Read the 4-bit data count and the erase count
    DataStat = aSpareData[SPARE_OFF_DATA_STATUS];
    EraseCnt = _LoadEraseCnt(pInst, aSpareData);
    DataStat++;
  }
  DataStat = (DataStat & 0xFu) | ((unsigned)DATA_STAT_VALID << 4);                    // Mark data as "valid"
  (void)_WriteSpareByte(pInst, SectorIndexWork, (U8)DataStat, SPARE_OFF_DATA_STATUS);

  //
  // Fail-safe TP. At this point we have two data blocks with the same LBI
  //
  CALL_TEST_HOOK(pInst->Unit);

  //
  // Converted work block is now data block, update Log2Phy Table
  //
  _L2P_Write(pInst, pWorkBlock->lbi, pWorkBlock->pbi);
  //
  // Change data status of block which contained the "old" data
  // as invalid and put it to the free list
  //
  (void)_MakeBlockAvailable(pInst, pbiSrc, EraseCnt);
  //
  // Remove the work block from the internal list
  //
  _WB_RemoveFromUsedList(pInst, pWorkBlock);
  _WB_AddToFreeList(pInst, pWorkBlock);
  //
  // If required, update the information used for active wear leveling
  //
  {
    U32 EraseCntMin;
    U32 NumBlocksEraseCntMin;
    U32 EraseCntWork;

    EraseCntMin          = pInst->EraseCntMin;
    NumBlocksEraseCntMin = pInst->NumBlocksEraseCntMin;
    (void)_ReadSpare(pInst, SectorIndexWork, aSpareData, 0, sizeof(aSpareData));  // Read the EraseCnt
    EraseCntWork         = _LoadEraseCnt(pInst, aSpareData);
    if (EraseCntWork < EraseCntMin) {
      EraseCntMin          = EraseCntWork;
      NumBlocksEraseCntMin = 1;
    } else {
      if (EraseCntWork == EraseCntMin) {
        ++NumBlocksEraseCntMin;
      }
    }
    pInst->EraseCntMin          = EraseCntMin;
    pInst->NumBlocksEraseCntMin = NumBlocksEraseCntMin;
  }
  IF_STATS(pInst->StatCounters.ConvertInPlaceCnt++);
  return 0;
}

/*********************************************************************
*
*       _IsInPlaceConversionAllowed
*
*  Function description
*    Checks if a work block can be converted without a copy operation.
*    This is the case if all written sectors are in their native places.
*
*  Return value
*    ==0    Found sectors with valid data not on their native positions.
*    ==1    All the sectors having valid data found on their native positions.
*/
static int _IsInPlaceConversionAllowed(const NAND_INST * pInst, const NAND_WORK_BLOCK * pWorkBlock) {
  unsigned u;
  unsigned Pos;
  unsigned SectorsPerBlock;

  SectorsPerBlock = 1uL << pInst->SPB_Shift;
  for (u = 0; u < SectorsPerBlock; u++) {
    if (_WB_IsSectorWritten(pWorkBlock, u) != 0) {
      Pos = _WB_ReadAssignment(pInst, pWorkBlock, u);
      if (Pos != u) {
        return 0;
      }
    }
  }
  return 1;
}

/*********************************************************************
*
*       _CleanWorkBlock
*
*  Function description
*    Closes a work block.
*
*  Parameters
*    pInst          [IN]  Driver instance.
*    pWorkBlock     [IN]  Work block to be cleaned.
*    brsi           Index of the sector (relative to block) to be written
*                   (BRSI_INVALID means no sector data).
*    pData          [IN]  Sector data to be written (NULL means no sector data).
*
*  Return values
*     ==1     OK, sector data written
*     ==0     OK, sector data not written
*     < 0     An error occurred
*
*  Additional information
*    The function performs the following operations:
*    - Converts a work block into normal data buffer by copy all data into it and marking it as data block.
*    - Invalidates and marks as free the block which contained the same logical data area before.
*/
static int _CleanWorkBlock(NAND_INST * pInst, NAND_WORK_BLOCK * pWorkBlock, unsigned brsi, const U32 * pData) {
  int      r;
  unsigned ErrBRSI;
  unsigned SectorsPerBlock;

  ErrBRSI = BRSI_INVALID;
  r = _IsInPlaceConversionAllowed(pInst, pWorkBlock);
  if (r < 0) {              // No valid sectors in the work block ?
    (void)_MakeBlockAvailable(pInst, pWorkBlock->pbi, ERASE_CNT_INVALID);
    //
    // Remove the work block from the internal list
    //
    _WB_RemoveFromUsedList(pInst, pWorkBlock);
    _WB_AddToFreeList(pInst, pWorkBlock);
    return 0;
  }
  if (r != 0) {                  // Can work block be converted in-place ?
    r = _ConvertWorkBlockInPlace(pInst, pWorkBlock, &ErrBRSI);
    if (r == 0) {
      return 0;             // Block converted, we are done.
    }
    SectorsPerBlock = 1uL << pInst->SPB_Shift;
    if (ErrBRSI == SectorsPerBlock) {
      return -1;            // Fatal error, no recovery is possible.
    }
  }
  //
  // Work block could not be converted in place, try via copy
  //
  r = _ConvertWorkBlockViaCopy(pInst, pWorkBlock, ErrBRSI, brsi, pData);
  if (r != 0) {
    return -1;              // Error, could not convert work block.
  }
  if (brsi != BRSI_INVALID) {
    return 1;               // OK, sector data written.
  }
  return 0;                 // OK, sector data not written.
}

/*********************************************************************
*
*       _CleanLastWorkBlock
*
*  Function description
*    Removes the least recently used work block from list of work blocks and converts it into data block
*/
static int _CleanLastWorkBlock(NAND_INST * pInst) {
  NAND_WORK_BLOCK * pWorkBlock;

  if (pInst->pFirstWorkBlockInUse == NULL) {
    return 1;           // Error, no work block in use.
  }
  //
  // Find last work block in list
  //
  pWorkBlock = pInst->pFirstWorkBlockInUse;
  while (pWorkBlock->pNext != NULL) {
    pWorkBlock = pWorkBlock->pNext;
  }
  return _CleanWorkBlock(pInst, pWorkBlock, BRSI_INVALID, NULL);
}

#if FS_NAND_SUPPORT_CLEAN

/*********************************************************************
*
*       _CleanAllWorkBlocks
*
*  Function description
*    Closes all work blocks.
*/
static int _CleanAllWorkBlocks(NAND_INST * pInst) {
  int r;
  int Result;

  r = 0;
  while (pInst->pFirstWorkBlockInUse != NULL) {
    Result = _CleanWorkBlock(pInst, pInst->pFirstWorkBlockInUse, BRSI_INVALID, NULL);
    if (Result != 0) {
      r = Result;
    }
  }
  return r;
}

#endif // FS_NAND_SUPPORT_CLEAN

/*********************************************************************
*
*       _AllocWorkBlock
*
*  Function description
*    Allocates resources for a new work block.
*
*  Parameters
*    pInst      [IN]  Driver instance.
*    lbi        Logical block index assigned to work block.
*
*  Return values
*    !=NULL   Pointer to allocated work block.
*    ==NULL   An error occurred, typ. a fatal error.
*
*  Additional information
*    The function performs the following operations:
*    - Allocates a work block descriptor from the array in the pInst structure.
*    - Finds a free block and assigns it to the work block descriptor.
*    - Writes info such as EraseCnt, LBI and the work block marker to the spare area of the first sector.
*/
static NAND_WORK_BLOCK * _AllocWorkBlock(NAND_INST * pInst, unsigned lbi) {
  NAND_WORK_BLOCK * pWorkBlock;
  U32               EraseCnt;
  unsigned          pbi;
  int               r;

  pWorkBlock = _AllocWorkBlockDesc(pInst, lbi);
  if (pWorkBlock == NULL) {
    //
    // No free work block found.
    //
    r = _CleanLastWorkBlock(pInst);
    if (r != 0) {
#if FS_NAND_SUPPORT_FAST_WRITE
      //
      // No work blocks are free or in use. Take an erased work block if available.
      //
      pWorkBlock = pInst->pFirstWorkBlockErased;
      if (pWorkBlock != NULL) {
        _WB_RemoveFromErasedList(pInst, pWorkBlock);
        _WB_AddErasedToFreeList(pInst, pWorkBlock);
      }
#else
      return NULL;                  // Error, could not convert work block to data block.
#endif
    }
    pWorkBlock = _AllocWorkBlockDesc(pInst, lbi);
    if (pWorkBlock == NULL) {
      return NULL;                  // Error, no more work block descriptors.
    }
  }
  //
  // A work block descriptor has been allocated. Check if we need to assign an erased block to it.
  //
  pbi = pWorkBlock->pbi;
  if (pbi == 0u) {
    //
    // Get an empty block to write on.
    //
    pbi = _AllocErasedBlock(pInst, &EraseCnt);
    if (pbi == 0u) {
      return NULL;                  // Error, could not allocate a new block.
    }
    //
    // New work block allocated.
    //
    pWorkBlock->pbi = pbi;
    r = _MarkAsWorkBlock(pInst, pbi, lbi, EraseCnt);
    if (r != 0) {
      return NULL;                  // Error, could not mark as work block.
    }
  } else {
    //
    // Work block already erased. Store only the LBI.
    //
    unsigned NumBytes;
    U32      SectorIndex;

    NumBytes    = (unsigned)pInst->BytesPerSector >> 5;
    SectorIndex = _BlockIndex2SectorIndex(pInst, pbi);
    _ClearStaticSpareArea(NumBytes);
    _StoreLBI(lbi);
    r = _WriteSpare(pInst, SectorIndex, _pSpareAreaData, 0, NumBytes);
    if (r != 0) {
      return NULL;                  // Error, could not store LBI to spare area.
    }
  }
  return pWorkBlock;
}

/*********************************************************************
*
*       _FindWorkBlock
*
*  Function description
*    Tries to locate a work block for a given logical block.
*/
static NAND_WORK_BLOCK * _FindWorkBlock(const NAND_INST * pInst, unsigned lbi) {
  NAND_WORK_BLOCK * pWorkBlock;

  //
  // Iterate over used-list
  //
  pWorkBlock = pInst->pFirstWorkBlockInUse;
  for (;;) {
    if (pWorkBlock == NULL) {
      break;                         // No match
    }
    if (pWorkBlock->lbi == lbi) {
      break;                         // Found it
    }
    pWorkBlock = pWorkBlock->pNext;
  }
  return pWorkBlock;
}

/*********************************************************************
*
*       _MarkWorkBlockAsMRU
*
*  Function description
*    Marks the given work block as most-recently used.
*    This is important so the least recently used one can be "kicked out" if a new one is needed.
*/
static void _MarkWorkBlockAsMRU(NAND_INST * pInst, NAND_WORK_BLOCK * pWorkBlock) {
  if (pWorkBlock != pInst->pFirstWorkBlockInUse) {
    _WB_RemoveFromUsedList(pInst, pWorkBlock);
    _WB_AddToUsedList(pInst, pWorkBlock);
  }
}

/*********************************************************************
*
*       _LoadWorkBlock
*
*  Function description
*    Reads management data of work block.
*    Used during low-level mount only, since at all other times, the work block descriptors are up to date.
*
*  Notes
*    (1) Finding out if page data has been written to work block
*        There are 2 criteria to find out if the sector in a work block has been written:
*        a) LBI entry correct
*        b) ECC written. A valid ECC has bits 16/17 == 0.
*        For the first page, criterion a) can not be used since this info is written even without data.
*/
static void _LoadWorkBlock(NAND_INST * pInst, const NAND_WORK_BLOCK * pWorkBlock) {
  unsigned NumSectors;
  unsigned iSector;
  U32      ecc;
  U32      SectorIndex0;
  unsigned brsi;
  U32      pbi;

  pbi          = pWorkBlock->pbi;
  NumSectors   = 1uL << pInst->SPB_Shift;
  SectorIndex0 = _BlockIndex2SectorIndex(pInst, pbi);
  //
  // Iterate over all sectors, reading spare info in order to find out if sector contains data and if so which data
  //
  for (iSector = 0; iSector < NumSectors; iSector++) {
    U32 SectorIndexSrc;

    SectorIndexSrc = SectorIndex0 + iSector;
    //
    // Check if this sector of the work block contains valid data.
    //
    (void)_ReadSpareIntoStaticBuffer(pInst, SectorIndexSrc);
    //
    // For first page, we need to check if data has been written. Note (1).
    //
    ecc = FS__ECC256_Load(_pSpareAreaData + SPARE_OFF_ECC00);
    if (FS__ECC256_IsValid(ecc) != 0) {
      if (iSector == 0u) {
        brsi = 0;
      } else {
        brsi = _LoadBRSI(pInst);
      }
      _WB_MarkSectorAsUsed(pWorkBlock, iSector);
      if (brsi != BRSI_INVALID) {
        _WB_WriteAssignment(pInst, pWorkBlock, brsi, iSector);
      }
    }
  }
}

/*********************************************************************
*
*       _IsBlockDataMoreRecent
*
*  Function description
*    Used during low-level mount only.
*/
static int _IsBlockDataMoreRecent(NAND_INST * pInst, U32 BlockIndex) {
  U8  Data;
  U8  Data8;
  U32 SectorIndex;

  SectorIndex = _BlockIndex2SectorIndex(pInst, BlockIndex);
  (void)_ReadSpareByte(pInst, SectorIndex, &Data8, SPARE_OFF_DATA_STATUS);
  Data = Data8 - *(_pSpareAreaData + SPARE_OFF_DATA_STATUS);
  if (Data == 1u) {
    return 1;        // Newer!
  }
  return 0;          // Older!
}

#if FS_NAND_ENABLE_STATS

/*********************************************************************
*
*       _GetNumValidSectors
*
*  Function description
*    Counts how many sectors in a block contain valid data.
*
*  Parameters
*    pInst    [IN]  Driver instance.
*    lbi      Logical index of the block to process.
*
*  Return value
*    Number of valid sectors.
*
*  Notes
*    (1) A sector with valid data has also a valid BRSI assigned to it.
*/
static U32 _GetNumValidSectors(NAND_INST * pInst, unsigned lbi) {
  unsigned          SectorsPerBlock;
  unsigned          iSector;
  U32               SectorIndexSrc;
  U32               NumSectors;
  unsigned          pbiSrc;
  NAND_WORK_BLOCK * pWorkBlock;

  pbiSrc          = _L2P_Read(pInst, lbi);
  pWorkBlock      = _FindWorkBlock(pInst, lbi);
  SectorsPerBlock = 1uL << pInst->SPB_Shift;
  NumSectors      = 0;
  //
  // 1st case: a data block is assigned to logical block
  //
  if ((pbiSrc != 0u) && (pWorkBlock == NULL)) {
    SectorIndexSrc  = _BlockIndex2SectorIndex(pInst, pbiSrc);
    do {
      if (_IsSectorDataInvalidated(pInst, SectorIndexSrc) == 0) {
        if (_IsSectorDataWritten(pInst, SectorIndexSrc) != 0) {
          ++NumSectors;
        }
      }
      ++SectorIndexSrc;
    } while (--SectorsPerBlock != 0u);
  }
  //
  // 2nd case: a work block is assigned to logical block
  //
  if ((pbiSrc == 0u) && (pWorkBlock != NULL)) {
    for (iSector = 0; iSector < SectorsPerBlock; ++iSector) {
      if (_brsiLog2Phy(pInst, pWorkBlock, iSector) != BRSI_INVALID) {     // Note 1
        ++NumSectors;
      }
    }
  }
  //
  // 3rd case: a data block and a work block are assigned to logical block
  //
  if ((pbiSrc != 0u) && (pWorkBlock != NULL)) {
    SectorIndexSrc  = _BlockIndex2SectorIndex(pInst, pbiSrc);
    for (iSector = 0; iSector < SectorsPerBlock; ++iSector) {
      if (_IsSectorDataInvalidated(pInst, SectorIndexSrc) == 0) {
        //
        // The sector was not invalidated by a "free sectors" command.
        // Check if it contains valid data. Else check if there is
        // valid data in the work block for this sector.
        //
        if (_IsSectorDataWritten(pInst, SectorIndexSrc) != 0) {
          ++NumSectors;
        } else {
          if (_brsiLog2Phy(pInst, pWorkBlock, iSector) != BRSI_INVALID) {   // Note 1
            ++NumSectors;
          }
        }
      }
      ++SectorIndexSrc;
    }
  }
  return NumSectors;
}

#endif  // FS_NAND_ENABLE_STATS

#if FS_NAND_SUPPORT_FAST_WRITE

/*********************************************************************
*
*       _CleanLimited
*
*  Function description
*    The clean function performs some house keeping as follows:
*      - Convert all work blocks that have less free sectors than
*        NumSectorsFree into data blocks.
*      - If there are less free blocks available than NumBlocksFree,
*        convert work blocks until at least NumBlocksFree are available
*
*  Return value
*    ==0    OK
*    !=0    An error occurred
*/
static int _CleanLimited(NAND_INST * pInst, U32 NumBlocksFree, U32 NumSectorsFree) {
  NAND_WORK_BLOCK * pWorkBlock;
  unsigned          NumWorkBlocksErased;
  int               r;
  unsigned          NumSectorsFreeInWB;
  unsigned          pbi;
  U32               EraseCnt;

  //
  // Count the number of available erased work blocks.
  //
  NumWorkBlocksErased = 0;
  pWorkBlock          = pInst->pFirstWorkBlockErased;
  while (pWorkBlock != NULL) {
    ++NumWorkBlocksErased;
    pWorkBlock = pWorkBlock->pNext;
  }
  pWorkBlock = pInst->pFirstWorkBlockFree;
  while (pWorkBlock != NULL) {
    if (pWorkBlock->pbi != 0u) {
      ++NumWorkBlocksErased;
    }
    pWorkBlock = pWorkBlock->pNext;
  }
  //
  // Clean and erase work blocks if the number of free work blocks
  // is smaller than the number of free work blocks required.
  //
  if (NumBlocksFree > NumWorkBlocksErased) {
    NumBlocksFree -= NumWorkBlocksErased;
    do {
      //
      // If there are no more free work blocks create ones by converting used work blocks.
      //
      if (pInst->pFirstWorkBlockFree == NULL) {
        r = _CleanLastWorkBlock(pInst);
        if (r != 0) {
          return 1;                   // Error, could not clean work block.
        }
      }
      pWorkBlock = pInst->pFirstWorkBlockFree;
      while (pWorkBlock != NULL) {
        if (pWorkBlock->pbi == 0u) {
          break;                      // We found a work block that is not erased.
        }
        pWorkBlock = pWorkBlock->pNext;
      }
      if (pWorkBlock == NULL) {
        break;                        // No more free blocks.
      }
      if (pWorkBlock->pbi != 0u) {
        break;                        // All work blocks are erased.
      }
      pbi = _AllocErasedBlock(pInst, &EraseCnt);
      if (pbi == 0u) {
        return 1;                     // Error, could not get an erased block.
      }
      _WB_RemoveFromFreeList(pInst, pWorkBlock);
      pWorkBlock->pbi = pbi;
      //
      // Mark as work block so that it is recognized as such at low-level mount and not erased again.
      // LBI will be set to the correct value when the work block is allocated.
      //
      r = _MarkAsWorkBlock(pInst, pbi, LBI_INVALID, EraseCnt);
      if (r != 0) {
        _WB_AddToFreeList(pInst, pWorkBlock);
        return 1;                     // Error, could not set as work block.
      }
      _WB_AddToErasedList(pInst, pWorkBlock);
    } while (--NumBlocksFree != 0u);
  }
  //
  // For each work block in use check if there are enough free sectors available.
  // Convert each work block that does not meet this requirement.
  //
  r = 0;
  do {
    pWorkBlock = pInst->pFirstWorkBlockInUse;
    while (pWorkBlock != NULL) {
      NumSectorsFreeInWB = _WB_GetNumFreeSectors(pInst, pWorkBlock);
      if (NumSectorsFree > NumSectorsFreeInWB) {
        r = _CleanWorkBlock(pInst, pWorkBlock, BRSI_INVALID, NULL);
        break;          // Restart the loop since the linked list has been modified.
      }
      pWorkBlock = pWorkBlock->pNext;
    }
  } while ((pWorkBlock != NULL) && (r == 0));
  return r;
}

/*********************************************************************
*
*       _ApplyCleanThreshold
*
*  Function description
*    Makes sure that the configured free space on work blocks is available.
*    The function converts work blocks to data blocks if necessary.
*
*  Parameters
*    pInst        Driver instance.
*
*  Return value
*    ==0    OK
*    !=0    An error occurred
*/
static int _ApplyCleanThreshold(NAND_INST * pInst) {
  unsigned NumBlocksFree;
  unsigned NumSectorsFree;
  unsigned PPB_Shift;
  unsigned SectorsPerBlock;
  unsigned NumWorkBlocks;
  int      r;

  PPB_Shift       = pInst->PPB_Shift;
  SectorsPerBlock = 1uL << PPB_Shift;
  NumWorkBlocks   = pInst->NumWorkBlocks;
  //
  // Validate the configuration parameters.
  //
  NumBlocksFree  = pInst->NumBlocksFree;
  NumSectorsFree = pInst->NumSectorsFree;
  if (NumBlocksFree >= NumWorkBlocks) {
    FS_DEBUG_WARN((FS_MTYPE_DRIVER, "NAND: Invalid number of free blocks. It will be set to 0."));
    NumBlocksFree = 0;
  }
  if (NumSectorsFree >= (SectorsPerBlock - 1u)) {    // -1 because the first sector in the block can store only the sector with the BRSI 0.
    FS_DEBUG_WARN((FS_MTYPE_DRIVER, "NAND: Invalid number of free sectors in block. It will be set to 0."));
    NumSectorsFree = 0;
  }
  pInst->NumBlocksFree  = (U16)NumBlocksFree;
  pInst->NumSectorsFree = (U16)NumSectorsFree;
  r = _CleanLimited(pInst, NumBlocksFree, NumSectorsFree);
  return r;
}

#endif // FS_NAND_SUPPORT_FAST_WRITE

/*********************************************************************
*
*       _LowLevelMount
*
*  Function description
*    Initializes the access to the NAND flash device.
*
*  Parameters
*    pInst        Driver instance.
*
*  Return value
*    ==0    OK, the NAND flash device was successfully mounted and is accessible.
*    !=0    An error occurred.
*/
static int _LowLevelMount(NAND_INST * pInst) {
  U16               iBlock;
  U16               lbi;
  U16               PBIPrev;
  U32               EraseCntMax;               // Highest erase count on any sector
  U32               EraseCnt;
  U32               EraseCntMin;
  U32               NumBlocksEraseCntMin;
  const U8        * pPageBuffer;
  unsigned          u;
  U32               BadBlockOff;
  int               r;
  U32               Version;
  U32               SectorSize;
  U32               NumBlocksToFileSystem;
  int               NumBlocksToUse;
  NAND_WORK_BLOCK * pWorkBlock;
  unsigned          NumWorkBlocks;
  U32               NumWorkBlocksLLFormat;
  unsigned          NumWorkBlocksToAllocate;
  U32               NumPhyBlocks;

  //
  // Check info block first (First block in the system)
  //
  r = _ReadSectorWithECC(pInst, _pSectorBuffer, SECTOR_INDEX_FORMAT_INFO);
  if ((r != RESULT_NO_ERROR) && (r != RESULT_1BIT_CORRECTED)) {
    if (r > 0) {
      _OnFatalError(pInst, r, SECTOR_INDEX_FORMAT_INFO);
    }
    return 1;                   // Error
  }
  pPageBuffer = (const U8 *)_pSectorBuffer;
  if (FS_MEMCMP(_acInfo, pPageBuffer , sizeof(_acInfo)) != 0) {
    FS_DEBUG_WARN((FS_MTYPE_DRIVER, "NAND: Invalid low-level signature."));
    return 1;                   // Error
  }
  Version = FS_LoadU32BE(pPageBuffer + INFO_OFF_LLFORMAT_VERSION);
  if (Version != (U32)LLFORMAT_VERSION) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND: Invalid low-level format version."));
    return 1;                   // Error
  }
  SectorSize = FS_LoadU32BE(pPageBuffer + INFO_OFF_SECTOR_SIZE);
  if (SectorSize > (U32)FS_Global.MaxSectorSize) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND: Sector size specified in drive is higher than the sector size that can be stored by the FS."));
    return 1;                   // Error
  }
  //
  // Load the number of work blocks from device.
  //
  NumWorkBlocks         = pInst->NumWorkBlocks;
  NumWorkBlocksLLFormat = FS_LoadU32BE(pPageBuffer + INFO_OFF_NUM_WORK_BLOCKS);
  if (NumWorkBlocksLLFormat == 0xFFFFFFFFuL) {  // "Old" driver versions do not set this field but use FS_NAND_MAX_WORK_BLOCKS.
    NumWorkBlocksLLFormat = NUM_WORK_BLOCKS_OLD;
  }
  //
  // Find out how many work blocks are required to be allocated.
  // We take the maximum between the number of work blocks read from device
  // and the number of work blocks configured. The reason is to prevent
  // an overflow in the paWorkBlock array when the application increases
  // the number of work blocks and does a low-level format.
  //
  NumWorkBlocksToAllocate = SEGGER_MAX(NumWorkBlocksLLFormat, NumWorkBlocks);
  NumWorkBlocks           = NumWorkBlocksLLFormat;
  //
  // Compute the number of logical blocks available for the file system.
  // We have to take into account that this version of the driver
  // reserves one block more for internal use. To stay compatible we have
  // to use 2 algorithms: one for the "old" version and one for the "new" one.
  // We tell the 2 versions apart by checking the INFO_OFF_NUM_LOG_BLOCKS.
  // The "old" version does not set this entry and its value will always be 0xFFFFFFFF.
  //
  NumPhyBlocks          = pInst->NumPhyBlocks;
  NumBlocksToFileSystem = FS_LoadU32BE(pPageBuffer + INFO_OFF_NUM_LOG_BLOCKS);
  NumBlocksToUse        = _CalcNumBlocksToUse(NumPhyBlocks, NumWorkBlocks);
  if (NumBlocksToFileSystem == 0xFFFFFFFFuL) {            // Old Low-Level Format ?
    NumBlocksToUse        = _CalcNumBlocksToUseOldFormat(NumPhyBlocks, NumWorkBlocks);
    NumBlocksToFileSystem = (U32)NumBlocksToUse;
  }
  if ((NumBlocksToUse <= 0) || (NumBlocksToFileSystem > (U32)NumBlocksToUse)) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NAND: Number of logical blocks has shrunk. Low-level format required."));
    return 1;
  }
  pInst->NumLogBlocks  = (U32)NumBlocksToUse;
  pInst->NumSectors    = (U32)pInst->NumLogBlocks << pInst->SPB_Shift;
  pInst->NumWorkBlocks = NumWorkBlocks;
  //
  // 3 different values for the BadBlockOff are permitted:
  // 0:          Used by large page flashes (2KB)
  // 5:          Used by small page flashes (512 bytes)
  // 0xFFFFFFFF: Formatted by older version of the driver, => We need to use 0 to stay compatible.
  //
  r           = 0;
  BadBlockOff = FS_LoadU32BE(pPageBuffer + INFO_OFF_BAD_BLOCK_OFFSET);
  switch (BadBlockOff) {
  case 0:                   // Large page flash ?
    break;
  case 5:                   // Small page flash ?
    break;
  case 0xFFFFFFFFu:         // Unknown type, but LL-formatted with bad block info at offset 0
    BadBlockOff = 0;
    break;
  default:                  // Illegal value!
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND: Invalid bad block offset %d.", BadBlockOff));
    r = 1;
    break;
  }
  if (r != 0) {
    return 1;
  }
  pInst->BadBlockOffset = (U8)BadBlockOff;
  //
  // Load the information stored when a fatal error occurs
  //
  pInst->IsWriteProtected = 0;
  pInst->HasFatalError    = 0;
  pInst->ErrorType        = RESULT_NO_ERROR;
  pInst->ErrorSectorIndex = 0;
  r = _ReadSectorWithECC(pInst, _pSectorBuffer, SECTOR_INDEX_ERROR_INFO);
  if ((r == RESULT_NO_ERROR) || (r == RESULT_1BIT_CORRECTED)) {
    pPageBuffer = (const U8 *)_pSectorBuffer;
    pInst->IsWriteProtected = FS_LoadU16BE(pPageBuffer + INFO_OFF_IS_WRITE_PROTECTED) != 0xFFFFu ? 1u : 0u;   // Inverted, 0xFFFF is not write protected
    pInst->HasFatalError    = FS_LoadU16BE(pPageBuffer + INFO_OFF_HAS_FATAL_ERROR)    != 0xFFFFu ? 1u : 0u;   // Inverted, 0xFFFF doesn't have fatal error
    if (pInst->HasFatalError != 0u) {
      pInst->ErrorType        = (U8)FS_LoadU16BE(pPageBuffer + INFO_OFF_FATAL_ERROR_TYPE);
      pInst->ErrorSectorIndex = FS_LoadU32BE(pPageBuffer + INFO_OFF_FATAL_ERROR_SECTOR_INDEX);
    }
  }
  //
  // Assign reasonable default for configuration values
  //
  if (pInst->MaxEraseCntDiff == 0u) {
    pInst->MaxEraseCntDiff = FS_NAND_MAX_ERASE_CNT_DIFF;
  }
  //
  // Allocate/Zero memory for tables
  //
  FS_ALLOC_ZEROED_PTR(SEGGER_PTR2PTR(void *, &pInst->pLog2PhyTable), (I32)_L2P_GetSize(pInst), "NAND_SECTOR_MAP");
  if (pInst->pLog2PhyTable == NULL) {
    return 1;                 // Error, could not allocate memory.
  }
  FS_ALLOC_ZEROED_PTR(SEGGER_PTR2PTR(void *, &pInst->pFreeMap),      ((I32)pInst->NumPhyBlocks + 7) / 8, "NAND_FREE_MAP");
  if (pInst->pFreeMap == NULL) {
    return 1;                 // Error, could not allocate memory.
  }
  //
  //  Initialize work block descriptors: Allocate memory & add them to free list.
  //
  {
    unsigned   spb;     // Sectors per block
    unsigned   NumBytes;
    unsigned   NumBytesStatus;
    U8       * pStatus;
    U8       * pAssign;

    NumBytes = sizeof(NAND_WORK_BLOCK) * NumWorkBlocksToAllocate;
    //
    // This is equivalent to FS_AllocZeroedPtr() but it avoids filling the array with 0
    // when the memory block is already allocated.
    //
    if (pInst->paWorkBlock == NULL) {
      pInst->paWorkBlock = SEGGER_PTR2PTR(NAND_WORK_BLOCK, FS_ALLOC_ZEROED((I32)NumBytes, "NAND_WORK_BLOCK"));
      if (pInst->paWorkBlock == NULL) {
        return 1;               // Error, could not allocate memory.
      }
      FS_MEMSET(pInst->paWorkBlock, 0, NumBytes);
    }
    NumBytes       = _WB_GetAssignmentSize(pInst);
    spb            = 1uL << pInst->SPB_Shift;
    pWorkBlock     = pInst->paWorkBlock;
    u              = NumWorkBlocksToAllocate;
    NumBytesStatus = (spb + 7u) >> 3;
    //
    // The memory for the assign and status arrays are allocated here at once for all the work blocks.
    // The address of the allocated memory is stored to the first work block.
    //
    FS_ALLOC_ZEROED_PTR(SEGGER_PTR2PTR(void *, &pWorkBlock->paIsWritten), (I32)NumBytesStatus * (I32)NumWorkBlocksToAllocate, "NAND_WB_IS_WRITTEN");
    FS_ALLOC_ZEROED_PTR(SEGGER_PTR2PTR(void *, &pWorkBlock->paAssign),    (I32)NumBytes       * (I32)NumWorkBlocksToAllocate, "NAND_WB_ASSIGN");
    if ((pWorkBlock->paIsWritten == NULL) || (pWorkBlock->paAssign == NULL)) {
      return 1;                     // Error, could not allocate memory for the work blocks.
    }
    pStatus = SEGGER_PTR2PTR(U8, pWorkBlock->paIsWritten);
    pAssign = SEGGER_PTR2PTR(U8, pWorkBlock->paAssign);
    do {
      pWorkBlock->paIsWritten = pStatus;
      pWorkBlock->paAssign    = SEGGER_PTR2PTR(void, pAssign);
      //
      // Not all the work block descriptors are available if the number of work blocks
      // specified in the device is smaller than the number of work blocks configured.
      //
      if (NumWorkBlocks != 0u) {
        _WB_AddToFreeList(pInst, pWorkBlock);
        NumWorkBlocks--;
      }
      pWorkBlock++;
      pStatus += NumBytesStatus;
      pAssign += NumBytes;
    } while (--u != 0u);
  }
  //
  // O.K., we read the spare areas and fill the tables
  //
  EraseCntMax          = 0;
  EraseCntMin          = ERASE_CNT_INVALID;
  NumBlocksEraseCntMin = 0;
  IF_STATS(pInst->StatCounters.NumBadBlocks = 0);
  for (iBlock = 1; iBlock < pInst->NumPhyBlocks; iBlock++) {
    U8   Data;

    (void)_ReadSpareIntoStaticBuffer(pInst, _BlockIndex2SectorIndex(pInst, iBlock));
    if (*(_pSpareAreaData + pInst->BadBlockOffset) != 0xFFu) {
      IF_STATS(pInst->StatCounters.NumBadBlocks++);
      continue;                                                     // This block is invalid and may not be used for anything !
    }
    Data     = *(_pSpareAreaData + SPARE_OFF_DATA_STATUS);
    lbi      = (U16)_LoadLBI(pInst);
    EraseCnt = _LoadEraseCnt(pInst, _pSpareAreaData);
    //
    // Is this a block containing valid data ?
    //
    if (EraseCnt != ERASE_CNT_INVALID) {
      //
      // Has this block been used as work block ?
      //
      if ((Data >> 4) == DATA_STAT_WORK) {
        //
        // If the work block is an invalid one do a pre-erase.
        //
        if (lbi >= pInst->NumLogBlocks) {
#if FS_NAND_SUPPORT_FAST_WRITE
          //
          // We have found an erased work block. Add it to the list.
          //
          pWorkBlock = pInst->pFirstWorkBlockFree;
          if (pWorkBlock != NULL) {
            _WB_RemoveFromFreeList(pInst, pWorkBlock);
            pWorkBlock->pbi = iBlock;
            _WB_AddToErasedList(pInst, pWorkBlock);
            goto NextBlock;
          }
#endif // FS_NAND_SUPPORT_FAST_WRITE
          (void)_PreEraseBlock(pInst, iBlock);
          _MarkBlockAsFree(pInst, iBlock);
          goto NextBlock;
        }
#if FS_NAND_SUPPORT_FAST_WRITE
        //
        // If all the free blocks are allocated try to free an erased block
        // in order to prevent a data loss.
        //
        pWorkBlock = pInst->pFirstWorkBlockFree;
        if (pWorkBlock == NULL) {
          pWorkBlock = pInst->pFirstWorkBlockErased;
          if (pWorkBlock != NULL) {
            unsigned pbi;

            pbi = pWorkBlock->pbi;
            (void)_PreEraseBlock(pInst, pbi);
            _MarkBlockAsFree(pInst, pbi);
            _WB_RemoveFromErasedList(pInst, pWorkBlock);
            _WB_AddToFreeList(pInst, pWorkBlock);
          }
        }
#endif // FS_NAND_SUPPORT_FAST_WRITE
        pWorkBlock = pInst->pFirstWorkBlockFree;
        if (pWorkBlock != NULL) {
          //
          // Check if we already have a block with this LBI.
          // If we do, then we erase it and add it to the free list.
          //
          pWorkBlock = _FindWorkBlock(pInst, lbi);
          if (pWorkBlock != NULL) {
            FS_DEBUG_WARN((FS_MTYPE_DRIVER, "NAND: Found a work block with the same LBI."));
            (void)_PreEraseBlock(pInst, iBlock);
            _MarkBlockAsFree(pInst, iBlock);
            goto NextBlock;
          }
          pWorkBlock      = _AllocWorkBlockDesc(pInst, lbi);
          pWorkBlock->pbi = iBlock;
        } else {
          FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NAND: Found more work blocks than can be handled. Configuration changed?"));
          (void)_PreEraseBlock(pInst, iBlock);
          _MarkBlockAsFree(pInst, iBlock);
        }
NextBlock:
        continue;
      }
      //
      // Is this a block containing valid data ?
      //
      if ((Data >> 4) == DATA_STAT_VALID) {
        if (lbi >= pInst->NumLogBlocks) {
         _MarkBlockAsFree(pInst, iBlock);
          continue;
        }
        PBIPrev = (U16)_L2P_Read(pInst, lbi);
        if (PBIPrev == 0u) {                                  // Has this lbi already been assigned ?
          _L2P_Write(pInst, lbi, iBlock);                     // Add block to the translation table
          if (EraseCnt > EraseCntMax) {
            EraseCntMax = EraseCnt;
          }
          continue;
        }
        if (_IsBlockDataMoreRecent(pInst, PBIPrev) != 0) {
          _MarkBlockAsFree(pInst, iBlock);
          (void)_PreEraseBlock(pInst, iBlock);
        } else {
          _MarkBlockAsFree(pInst, PBIPrev);
          (void)_PreEraseBlock(pInst, PBIPrev);
          _L2P_Write(pInst, lbi, iBlock);                     // Add block to the translation table
        }
        if ((EraseCntMin == ERASE_CNT_INVALID) ||
            (EraseCnt < EraseCntMin)) {                       // Collect information for the active wear leveling
          EraseCntMin          = EraseCnt;
          NumBlocksEraseCntMin = 1;
        } else {
          if (EraseCnt == EraseCntMin) {
            ++NumBlocksEraseCntMin;
          }
        }
        continue;
      }
    }
    //
    // Any other blocks are interpreted as free blocks.
    //
    _MarkBlockAsFree(pInst, iBlock);
  }
  pInst->EraseCntMax          = EraseCntMax;
  pInst->EraseCntMin          = EraseCntMin;
  pInst->NumBlocksEraseCntMin = NumBlocksEraseCntMin;
  //
  // Handle the work blocks we found
  //
  pWorkBlock = pInst->pFirstWorkBlockInUse;
  while (pWorkBlock != NULL) {
    _LoadWorkBlock(pInst, pWorkBlock);
    pWorkBlock = pWorkBlock->pNext;
  }
#if FS_NAND_SUPPORT_FAST_WRITE
  //
  // Reserve space in work blocks for fast write operations.
  //
  r = _ApplyCleanThreshold(pInst);
#else
  r = 0;
#endif // FS_NAND_SUPPORT_FAST_WRITE
  //
  // On debug builds we count here the number of valid sectors
  //
#if FS_NAND_ENABLE_STATS
  {
    U32 NumSectors;
    U32 NumSectorsInBlock;

    NumSectors = 0;
    for (iBlock = 0; iBlock < pInst->NumLogBlocks; ++iBlock) {
      NumSectorsInBlock  = _GetNumValidSectors(pInst, iBlock);
      NumSectors        += NumSectorsInBlock;
    }
    pInst->StatCounters.NumValidSectors = NumSectors;
  }
#endif // FS_NAND_ENABLE_STATS
  return r;
}

/*********************************************************************
*
*       _LowLevelMountIfRequired
*
*  Function description
*    Mounts the NAND flash device if it is not already mounted.
*
*  Parameters
*    pInst      Driver instance.
*
*  Return value
*    ==0      OK, operation completed successfully.
*    !=0      An error occurred.
*/
static int _LowLevelMountIfRequired(NAND_INST * pInst) {
  int r;

  if (pInst->IsLLMounted != 0u) {
    return 0;                   // OK, the NAND flash device is already mounted.
  }
  if (pInst->LLMountFailed != 0u) {
    return 1;                   // Error, we were not able to mount the NAND flash device and do not want to try again.
  }
  r = _LowLevelMount(pInst);
  if (r == 0) {
    pInst->IsLLMounted = 1;
  } else {
    pInst->LLMountFailed = 1;
  }
  return r;
}

/*********************************************************************
*
*       _ReadSector
*
*  Function description
*    Reads one logical sectors from storage device.
*    There are 3 possibilities:
*    a) Data is in WorkBlock
*    b) There is a physical block assigned to this logical block -> Read from Hardware
*    c) There is a no physical block assigned to this logical block. This means data has never been written to storage. Fill data with 0.
*
*  Return value
*    ==0    Data successfully read.
*    !=0    An error has occurred.
*/
static int _ReadSector(NAND_INST * pInst, U32 LogSectorIndex, U8 * pBuffer) {
  int               r;
  unsigned          lbi;
  unsigned          pbi;
  unsigned          Mask;
  unsigned          brsiPhy;
  unsigned          brsiLog;
  U32               PhySectorIndex;
  NAND_WORK_BLOCK * pWorkBlock;
  int               IsRelocationRequired;

  lbi        = LogSectorIndex >> pInst->SPB_Shift;             // Log.  block index
  Mask       = (1uL << pInst->SPB_Shift) - 1u;
  //
  // Physical block index is taken from Log2Phy table or is work block
  //
  pbi        = _L2P_Read(pInst, lbi);          // Phys. block index
  brsiLog    = LogSectorIndex & Mask;
  brsiPhy    = brsiLog;
  pWorkBlock = _FindWorkBlock(pInst, lbi);
  if (pWorkBlock != NULL) {
    unsigned u;

    u = _brsiLog2Phy(pInst, pWorkBlock, brsiLog);
    if (u != BRSI_INVALID) {
      pbi = pWorkBlock->pbi;
      brsiPhy = u;
    }
  }
  //
  // Get data
  //
  if (pbi == 0u) {
    //
    // Find physical page and fill buffer if it is not assigned
    //
    FS_MEMSET(pBuffer, 0xFF, pInst->BytesPerSector);
    r = 0;                                                  // O.K., we filled the buffer with a known value since this sector has never been written.
  } else {
    IsRelocationRequired = 0;
    //
    // Read from hardware
    //
    PhySectorIndex = _BlockIndex2SectorIndex(pInst, pbi) | brsiPhy;
    r = _ReadSectorWithECC(pInst, SEGGER_PTR2PTR(U32, pBuffer), PhySectorIndex);
    if (r == RESULT_NO_ERROR) {
      r = 0;
#if (FS_NAND_MAX_BIT_ERROR_CNT == 0)
    } else if (r == RESULT_1BIT_CORRECTED) {
      r = 0;
#endif
    } else if (r < 0) {   // Sector is blank
      FS_MEMSET(pBuffer, 0xFF, pInst->BytesPerSector);      // Data should be blank. This one bit in NAND flash may be incorrect, we fill with 0xFF to be safe
      r = 0;
#if (FS_NAND_MAX_BIT_ERROR_CNT == 0)
    } else if (r == RESULT_ERROR_IN_ECC) {
#else
    } else if ((r == RESULT_ERROR_IN_ECC) || (r == RESULT_1BIT_CORRECTED)) {
#endif
      //
      // We found an error in the ECC or in the data but the data is OK. Copy the data into another block.
      //
      IsRelocationRequired = 1;
    } else {
      //
      // An fatal error occurred. Try to recover as much data as possible by copying the data to an other block.
      //
      IsRelocationRequired = 1;
    }
    //
    // Copy the data to an other block.
    //
    if (IsRelocationRequired != 0) {
      if (pWorkBlock != NULL) {
        r = _ConvertWorkBlockViaCopy(pInst, pWorkBlock, BRSI_INVALID, BRSI_INVALID, NULL);
      } else {
        r = _RecoverDataBlock(pInst, pbi);
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _WriteLogSector
*
*  Function description
*    Writes one logical sector to storage device.
*
*  Return value
*    ==0      Data successfully written.
*    !=0      An error occurred.
*/
static int _WriteLogSector(NAND_INST * pInst, U32 LogSectorIndex, const void * pBuffer) {
  U16               lbi;
  U32               Mask;
  NAND_WORK_BLOCK * pWorkBlock;
  unsigned          brsiSrc;
  unsigned          brsiDest;
  int               r;
  U32               SectorIndex;
  unsigned          brsiPhy;

  Mask     = (1uL << pInst->SPB_Shift) - 1u;
  lbi      = (U16)(LogSectorIndex >> pInst->SPB_Shift);
  brsiSrc  = LogSectorIndex & Mask;
  brsiDest = ~0u;
  for (;;) {
    //
    // Find (or create) a work block and the sector to be used in it.
    //
    pWorkBlock = _FindWorkBlock(pInst, lbi);
    if (pWorkBlock != NULL) {
      //
      // Make sure that the sector to write is in work block and that it has not already been written.
      //
      brsiDest = _FindFreeSectorInWorkBlock(pInst, pWorkBlock, brsiSrc);
      if (brsiDest == BRSI_INVALID) {
        r = _CleanWorkBlock(pInst, pWorkBlock, brsiSrc, SEGGER_CONSTPTR2PTR(const U32, pBuffer));
        if (r < 0) {
          return 1;             // Error, could not clean work block.
        }
        if (r == 1) {
          return 0;             // OK, sector data successfully written. We are done.
        }
        pWorkBlock = NULL;      // Request a new work block.
      }
    }
    //
    // No work block found. Allocate a new one.
    //
    if (pWorkBlock == NULL) {
      pWorkBlock = _AllocWorkBlock(pInst, lbi);
      if (pWorkBlock == NULL) {
        return 1;
      }
      brsiDest = brsiSrc;       // Preferred position is free, so let's use it.
    }
    //
    // Write data into sector of work block.
    //
    _ClearStaticSpareArea((unsigned)pInst->BytesPerSector >> 5);
    if (brsiDest != 0u) {       // BRSI is stored in the same place as LBI info, but only for the first sector in a block.
      _StoreBRSI(brsiSrc);
    }
    SectorIndex = _BlockIndex2SectorIndex(pInst, pWorkBlock->pbi) | brsiDest;
    r = _WriteSector(pInst, SEGGER_CONSTPTR2PTR(const U32, pBuffer), SectorIndex);
#if FS_NAND_VERIFY_WRITE
    if (r == 0) {
      r = _VerifySector(pInst, SEGGER_CONSTPTR2PTR(const U32, pBuffer), SectorIndex);
    }
#endif // FS_NAND_VERIFY_WRITE
    if (r == 0) {
      break;                    // Data written
    }
    //
    // Could not write into work block. Save the data of this work block into data block
    // and try to find another work block to write into
    //
    r = _ConvertWorkBlockViaCopy(pInst, pWorkBlock, brsiDest, BRSI_INVALID, NULL);
    if (r != 0) {
      return 1;                 // Error, could not convert work block.
    }
  }
#if FS_NAND_ENABLE_STATS
  //
  // For debug builds only. Keep the number of valid sectors up to date
  //
  {
    unsigned pbiSrc;

    //
    // The number of valid sectors is increased only if the sector
    // is written for the first time since the last low-level format
    // or it is re-written after its value has been invalidated.
    //
    pbiSrc  = _L2P_Read(pInst, lbi);
    brsiPhy = _brsiLog2Phy(pInst, pWorkBlock, brsiSrc);
    if (brsiPhy == BRSI_INVALID) {  // Sector not written yet ?
      if (pbiSrc != 0u) {           // Sector in data block ?
        U32 SectorIndexSrc;
        int IsWritten;
        int IsInvalidated;

        SectorIndexSrc  = _BlockIndex2SectorIndex(pInst, pbiSrc);
        SectorIndexSrc |= brsiSrc;
        IsWritten     = _IsSectorDataWritten(pInst, SectorIndexSrc);
        IsInvalidated = _IsSectorDataInvalidated(pInst, SectorIndexSrc);
        if ((IsWritten == 0) || (IsInvalidated != 0)) {
          pInst->StatCounters.NumValidSectors++;
        }
      } else {
        pInst->StatCounters.NumValidSectors++;
      }
    }
  }
#endif // FS_NAND_ENABLE_STATS
  //
  // Invalidate data previously used for the same brsi (if necessary).
  //
#if FS_NAND_SUPPORT_TRIM
  brsiPhy      = _WB_ReadAssignment(pInst, pWorkBlock, brsiSrc);
  SectorIndex  = _BlockIndex2SectorIndex(pInst, pWorkBlock->pbi);
  SectorIndex |= brsiPhy;
  if (brsiPhy != 0u) {
    _ClearStaticSpareArea((unsigned)pInst->BytesPerSector >> 5);
    _StoreBRSI(BRSI_INVALID);
    (void)_InvalidateSectorDataFast(pInst, SectorIndex);
    (void)_WriteSpareAreaFromStaticBuffer(pInst, SectorIndex);
  } else {
    if (brsiSrc == 0u) {
      //
      // It is necessary to invalidate the data of the PhyBRSI 0
      // to be able to tell if the LogBRSI 0 contains valid data or not.
      //
      if (_WB_IsSectorWritten(pWorkBlock, brsiPhy) != 0) {
        (void)_InvalidateSectorData(pInst, SectorIndex);
      }
    }
  }
#else
  if (brsiSrc != 0u) {
    brsiPhy = _WB_ReadAssignment(pInst, pWorkBlock, brsiSrc);
    if (brsiPhy != 0u) {
      SectorIndex  = _BlockIndex2SectorIndex(pInst, pWorkBlock->pbi);
      SectorIndex |= brsiPhy;
      _ClearStaticSpareArea((unsigned)pInst->BytesPerSector >> 5);
      _StoreBRSI(BRSI_INVALID);
      (void)_WriteSpareAreaFromStaticBuffer(pInst, SectorIndex);
    }
  }
#endif // FS_NAND_SUPPORT_TRIM
  //
  // Update work block management info.
  //
  _MarkWorkBlockAsMRU(pInst, pWorkBlock);
  _WB_MarkSectorAsUsed(pWorkBlock, brsiDest);                 // Mark sector as used.
  _WB_WriteAssignment(pInst, pWorkBlock, brsiSrc, brsiDest);  // Update the look-up table.
  return 0;
}

#if FS_NAND_SUPPORT_TRIM

/*********************************************************************
*
*        _FreeOneSector
*
*  Return value
*    ==1    Sector freed
*    ==0    No sector freed
*    < 0    An error occurred
*/
static int _FreeOneSector(NAND_INST * pInst, U32 LogSectorIndex) {
  unsigned          lbi;
  unsigned          pbi;
  U32               PhySectorIndex;
  unsigned          brsiLog;
  unsigned          brsiPhy;
  U32               Mask;
  NAND_WORK_BLOCK * pWorkBlock;
  int               r;
  int               Result;

  r       = 0;    // No sector freed yet
  lbi     = LogSectorIndex >> pInst->SPB_Shift;
  pbi     = _L2P_Read(pInst, lbi);
  Mask    = (1uL << pInst->SPB_Shift) - 1u;
  brsiLog = LogSectorIndex & Mask;
  //
  // If necessary, mark the sector as free in the data block
  //
  if (pbi != 0u) {                    // Sector in a data block ?
    PhySectorIndex  = _BlockIndex2SectorIndex(pInst, pbi);
    PhySectorIndex |= brsiLog;
    //
    // Invalidate only sectors which contain valid data and are not already invalidated
    //
    if (_IsSectorDataWritten(pInst, PhySectorIndex) != 0) {
      if (_IsSectorDataInvalidated(pInst, PhySectorIndex) == 0) {
        r = 1;                        // Sector has been freed
        Result = _InvalidateSectorData(pInst, PhySectorIndex);
        if (Result != 0) {
          r = -1;                     // An error occurred.
        }
      }
    }
  }
  //
  // If necessary, mark the sector as free in the work block
  //
  pWorkBlock = _FindWorkBlock(pInst, lbi);
  if (pWorkBlock != NULL) {
    brsiPhy = _brsiLog2Phy(pInst, pWorkBlock, brsiLog);
    if (brsiPhy != BRSI_INVALID) {    // Sector in a work block ?
      PhySectorIndex  = _BlockIndex2SectorIndex(pInst, pWorkBlock->pbi);
      PhySectorIndex |= brsiPhy;
      r = 1;                          // Sector has been freed.
      if (brsiPhy != 0u) {
        //
        // Mark on the medium the sector data as invalid
        //
        _ClearStaticSpareArea((unsigned)pInst->BytesPerSector >> 5);
        _StoreBRSI(BRSI_INVALID);
        Result = _InvalidateSectorDataFast(pInst, PhySectorIndex);
        if (Result == 0) {
          Result = _WriteSpareAreaFromStaticBuffer(pInst, PhySectorIndex);
        }
        //
        // Remove the assignment of the logical sector
        //
        _WB_WriteAssignment(pInst, pWorkBlock, brsiLog, 0);
      } else {
        Result = _InvalidateSectorData(pInst, PhySectorIndex);
      }
      if (Result != 0) {
        r = -1;
      }
    }
  }
  return r;
}

/*********************************************************************
*
*        _FreeOneBlock
*
*  Function description
*    Marks all logical sectors in a logical block as free.
*
*  Parameters
*    pInst      Driver instance.
*    lbi        Index of the logical block to be freed.
*
*  Return value
*    ==0      Sectors have been freed.
*    !=0      An error occurred.
*/
static int _FreeOneBlock(NAND_INST * pInst, unsigned lbi) {
  int               r;
  int               Result;
  unsigned          pbi;
  NAND_WORK_BLOCK * pWorkBlock;
  U32               EraseCnt;
  U32               PhySectorIndex;

  r = 0;                          // Set to indicate success.
  //
  // First, free the work block if one is assigned to logical block.
  //
  pWorkBlock = _FindWorkBlock(pInst, lbi);
  if (pWorkBlock != NULL) {
    pbi = pWorkBlock->pbi;
    _WB_RemoveFromUsedList(pInst, pWorkBlock);
    _WB_AddToFreeList(pInst, pWorkBlock);
    PhySectorIndex = _BlockIndex2SectorIndex(pInst, pbi);
    (void)_ReadSpareIntoStaticBuffer(pInst, PhySectorIndex);
    EraseCnt = _LoadEraseCnt(pInst, _pSpareAreaData);
    Result = _MakeBlockAvailable(pInst, pbi, EraseCnt);
    if (Result != 0) {
      r = Result;                 // Error, could not free phy. block.
    }
  }
  //
  // Free the data block if one is assigned to the logical block.
  //
  pbi = _L2P_Read(pInst, lbi);
  if (pbi != 0u) {
    _L2P_Write(pInst, lbi, 0);    // Remove the logical block from the mapping table.
    PhySectorIndex = _BlockIndex2SectorIndex(pInst, pbi);
    (void)_ReadSpareIntoStaticBuffer(pInst, PhySectorIndex);
    EraseCnt = _LoadEraseCnt(pInst, _pSpareAreaData);
    Result = _MakeBlockAvailable(pInst, pbi, EraseCnt);
    if (Result != 0) {
      r = Result;                 // Error, could not free phy. block.
    }
  }
  return r;
}

/*********************************************************************
*
*        _FreeSectors
*
*  Function description
*     Marks a logical sector as free.
*
*  Parameters
*    pInst            Driver instance.
*    LogSectorIndex   Index of the first logical sector to be freed.
*    NumSectors       Number of sectors to be freed.
*
*  Return value
*    ==0    Sectors have been freed.
*    !=0    An error occurred.
*
*  Additional information
*    This routine is called from the higher layer file system to help
*    the driver to manage the data. In this way sectors which are no
*    longer in use by the higher layer file system do not need to be copied.
*/
static int _FreeSectors(NAND_INST * pInst, U32 LogSectorIndex, U32 NumSectors) {
  int      r;
  unsigned NumBlocks;
  unsigned SPB_Shift;
  unsigned lbi;
  int      Result;
  U32      NumSectorsAtOnce;

  r = 0;            // Set to indicate success.
  if (NumSectors != 0u) {
    U32 FirstSector;
    U32 LastSector;
    U32 NumSectorsTotal;

    FirstSector     = LogSectorIndex;
    LastSector      = LogSectorIndex + NumSectors - 1u;
    NumSectorsTotal = pInst->NumSectors;
    if ((FirstSector >= NumSectorsTotal) || (LastSector >= NumSectorsTotal)) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NAND: _FreeSectors: Invalid sector range."));
      return 1;       // Error, trying to free sectors which do not exist.
    }
  }
  SPB_Shift = pInst->SPB_Shift;
  //
  // Free single sectors until we reach a NAND block boundary.
  //
  do {
    if ((LogSectorIndex & ((1uL << SPB_Shift) - 1u)) == 0u) {
      break;
    }
    Result = _FreeOneSector(pInst, LogSectorIndex);
    if (Result < 0) {
      r = 1;                      // Error, could not free sector.
    } else {
      if (Result != 0) {
        IF_STATS(pInst->StatCounters.NumValidSectors--);
      }
    }
    LogSectorIndex++;
  } while (--NumSectors != 0u);
  //
  // Free entire NAND blocks.
  //
  NumBlocks = NumSectors >> SPB_Shift;
  if (NumBlocks != 0u) {
    NumSectorsAtOnce = (U32)NumBlocks << SPB_Shift;
    lbi              = LogSectorIndex >> SPB_Shift;
    do {
      Result = _FreeOneBlock(pInst, lbi);
      if (Result < 0) {
        r = 1;                    // Error, could not free block.
      } else {
        if (Result != 0) {
          IF_STATS(pInst->StatCounters.NumValidSectors -= NumSectorsAtOnce);
        }
      }
      ++lbi;
    } while (--NumBlocks != 0u);
    LogSectorIndex += NumSectorsAtOnce;
    NumSectors     -= NumSectorsAtOnce;
  }
  //
  // Free the remaining sectors one at a time.
  //
  if (NumSectors != 0u) {
    do {
      Result = _FreeOneSector(pInst, LogSectorIndex);
      if (Result < 0) {
        r = 1;                    // Error, could not free sector.
      } else {
        if (Result != 0) {
          IF_STATS(pInst->StatCounters.NumValidSectors--);
        }
      }
      LogSectorIndex++;
    } while (--NumSectors != 0u);
  }
  return r;
}

#endif // FS_NAND_SUPPORT_TRIM

/*********************************************************************
*
*        _GetSectorUsage
*
*   Function description
*     Checks if a logical sector contains valid data.
*
*   Return values
*     ==0     Sector in use (contains valid data)
*     ==1     Sector not in use (was not written nor was invalidated)
*     ==2     Usage unknown
*/
static int _GetSectorUsage(NAND_INST * pInst, U32 LogSectorIndex) {
  unsigned          lbi;
  unsigned          pbi;
  U32               PhySectorIndex;
  unsigned          brsiLog;
  unsigned          brsiPhy;
  U32               Mask;
  NAND_WORK_BLOCK * pWorkBlock;
  int               r;
  U32               NumSectorsTotal;

  NumSectorsTotal = pInst->NumSectors;
  if (LogSectorIndex >= NumSectorsTotal) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NAND: _GetSectorUsage: Invalid sector index."));
    return 2;               // Error, trying to get the usage of a non-existent sector.
  }
  r       = 1;                // Sector not in use
  lbi     = LogSectorIndex >> pInst->SPB_Shift;
  pbi     = _L2P_Read(pInst, lbi);
  Mask    = (1uL << pInst->SPB_Shift) - 1u;
  brsiLog = LogSectorIndex & Mask;
  //
  // First check if the sector is in a data block
  //
  if (pbi != 0u) {            // Sector in a data block ?
    int IsWritten;
    int IsInvalidated;

    PhySectorIndex  = _BlockIndex2SectorIndex(pInst, pbi);
    PhySectorIndex |= brsiLog;
    IsWritten     = _IsSectorDataWritten(pInst, PhySectorIndex);
    IsInvalidated = _IsSectorDataInvalidated(pInst, PhySectorIndex);
    if ((IsWritten != 0) && (IsInvalidated == 0)) {
      r = 0;                                // Sector contains valid data.
    }
  }
  //
  // Now check if the sector data is located in a data block.
  //
  pWorkBlock = _FindWorkBlock(pInst, lbi);
  if (pWorkBlock != NULL) {
    brsiPhy = _brsiLog2Phy(pInst, pWorkBlock, brsiLog);
    if (brsiPhy != BRSI_INVALID) {          // Sector in a work block ?
      r = 0;                                // Sector contains valid data.
    }
  }
  return r;
}

#if FS_NAND_SUPPORT_CLEAN

/*********************************************************************
*
*       _CleanOne
*
*  Function description
*    Executes a single clean operation.
*
*  Parameters
*    pInst      Driver instance.
*    pMore      [OUT] Indicates if all sectors have been cleaned.
*               * ==0   No other clean operations are required.
*               * ==1   At least one more clean operation is required
*                       to completely clean the storage.
*
*  Return value
*    ==0    OK, operation completed successfully.
*    !=0    An error occurred.
*
*  Additional information
*    The clean operation converts a work block to data block.
*/
static int _CleanOne(NAND_INST * pInst, int * pMore) {
  int More;
  int r;

  More = 0;
  r    = 0;
  //
  // Clean the first work block in the list.
  //
  if (pInst->pFirstWorkBlockInUse != NULL) {
    r = _CleanWorkBlock(pInst, pInst->pFirstWorkBlockInUse, BRSI_INVALID, NULL);
  }
  //
  // Now check if there is more work to do.
  //
  if (pInst->pFirstWorkBlockInUse != NULL) {
    More = 1;       // At least one more work block to clean.
  }
  if (pMore != NULL) {
    *pMore = More;
  }
  return r;
}

/*********************************************************************
*
*       _Clean
*
*  Function description
*    Performs a complete clean of the storage.
*
*  Parameters
*    pInst     Driver instance.
*
*  Additional information
*    Converts all work blocks into data blocks.
*/
static int _Clean(NAND_INST * pInst) {
  int r;

  r = _CleanAllWorkBlocks(pInst);
  return r;
}

/*********************************************************************
*
*       _GetCleanCnt
*
*  Function description
*    Returns the number of operations required to completely clean the storage.
*
*  Parameters
*    pInst      Driver instance.
*
*  Return value
*    Number of clean operations required.
*/
static U32 _GetCleanCnt(const NAND_INST * pInst) {
  U32               CleanCnt;
  NAND_WORK_BLOCK * pWorkBlock;

  CleanCnt = 0;
  pWorkBlock = pInst->pFirstWorkBlockInUse;
  while (pWorkBlock != NULL) {
    ++CleanCnt;
    pWorkBlock = pWorkBlock->pNext;
  }
  return CleanCnt;
}

#endif // FS_NAND_SUPPORT_CLEAN

/*********************************************************************
*
*       _LowLevelFormat
*
*  Function description
*    Erases all blocks and writes the format information to the first one.
*
*  Return value
*    ==0    O.K.
*    !=0    Error
*/
static int _LowLevelFormat(NAND_INST * pInst) {
  U32        NumPhyBlocks;
  unsigned   BlockIndex;
  U8       * pPageBuffer;
  unsigned   OffStatus;
  int        r;

  pInst->LLMountFailed  = 0;
  pInst->IsLLMounted    = 0;
  pPageBuffer           = (U8 *)_pSectorBuffer;

  OffStatus = _GetOffBlockStatus(pInst);
  //
  // Erase the first sector/phy. block. This block is guaranteed to be valid.
  //
  r  = _EraseBlock(pInst, 0);
  if (r != 0) {
    return 1;     // Error, first block in the range is defective.
  }
  //
  // Erase NAND flash blocks which are valid.
  // Valid NAND flash blocks are blocks that
  // contain a 0xff in the first byte of the spare area of the first two pages of the block
  //
  NumPhyBlocks = pInst->NumPhyBlocks;
  for (BlockIndex = 1; BlockIndex < NumPhyBlocks; BlockIndex++) {
    if (_IsBlockErasable(pInst, BlockIndex) != 0) {
      r = _EraseBlock(pInst, BlockIndex);
      if (r != 0) {
        _MarkBlockAsBad(pInst, BlockIndex, RESULT_ERASE_ERROR, 0);
        IF_STATS(pInst->StatCounters.NumBadBlocks++);
      }
    } else {
      //
      // The block is marked as defective and cannot be erased.
      //
      IF_STATS(pInst->StatCounters.NumBadBlocks++);
    }
  }
  IF_STATS(pInst->StatCounters.NumValidSectors = 0);
  //
  // Write the Format information to first sector of the first block
  //
  FS_MEMSET(pPageBuffer, 0xFF, pInst->BytesPerSector);
  FS_MEMCPY(pPageBuffer, _acInfo, sizeof(_acInfo));
  FS_StoreU32BE(pPageBuffer + INFO_OFF_LLFORMAT_VERSION, LLFORMAT_VERSION);
  FS_StoreU32BE(pPageBuffer + INFO_OFF_SECTOR_SIZE,      FS_Global.MaxSectorSize);
  FS_StoreU32BE(pPageBuffer + INFO_OFF_BAD_BLOCK_OFFSET, OffStatus);
  FS_StoreU32BE(pPageBuffer + INFO_OFF_NUM_LOG_BLOCKS,   pInst->NumLogBlocks);
  FS_StoreU32BE(pPageBuffer + INFO_OFF_NUM_WORK_BLOCKS,  pInst->NumWorkBlocks);
  _ClearStaticSpareArea((unsigned)pInst->BytesPerSector >> 5);
  r = _WriteSector(pInst, _pSectorBuffer, SECTOR_INDEX_FORMAT_INFO);
#if FS_NAND_VERIFY_WRITE
  if (r == 0) {
    r = _VerifySector(pInst, _pSectorBuffer, SECTOR_INDEX_FORMAT_INFO);
  }
#endif
  return r;
}

/*********************************************************************
*
*       _InitIfRequired
*
*  Function description
*    Initialize and identifies the storage device.
*
*  Return value
*    ==0    Device ok and ready for operation.
*    < 0    An error has occurred.
*/
static int _InitIfRequired(NAND_INST * pInst) {
  U8       Unit;
  int      r;
  unsigned NumBytes;

  ASSERT_PHY_TYPE_IS_SET(pInst);
  if (pInst->IsLLMounted != 0u) {
    return 0;                     // OK, NAND device already initialized.
  }
  NumBytes = (unsigned)FS_Global.MaxSectorSize >> 5;
  FS_ALLOC_ZEROED_PTR(SEGGER_PTR2PTR(void *, &_pSectorBuffer),  (I32)FS_Global.MaxSectorSize, "NAND_SECTOR_BUFFER");
  if (_pSectorBuffer == NULL) {
    return 1;                      // Error, failed to allocate memory for the sector buffer.
  }
  FS_ALLOC_ZEROED_PTR(SEGGER_PTR2PTR(void *, &_pSpareAreaData), (I32)NumBytes, "NAND_SPARE_BUFFER");
  if (_pSpareAreaData == NULL) {
    return 1;                      // Error, failed to allocate memory for the spare area buffer.
  }
  r = _ReadApplyDeviceParas(pInst);
  if (r != 0) {
    return 1;                      // Error, failed to identify NAND flash or unsupported type
  }
  Unit = pInst->Unit;
  if (pInst->pPhyType->pfIsWP(Unit) != 0) {
    pInst->IsWriteProtected = 1;
  }
  return 0;                        // OK, Device is accessible.
}

/*********************************************************************
*
*       _AllocInstIfRequired
*
*  Function description
*    Allocate memory for the specified unit if required.
*/
static NAND_INST * _AllocInstIfRequired(U8 Unit) {
  NAND_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  pInst = NULL;
  if (Unit < (U8)FS_NAND_NUM_UNITS) {
    pInst = _apInst[Unit];
    if (pInst == NULL) {
      FS_ALLOC_ZEROED_PTR(SEGGER_PTR2PTR(void *, &pInst), (I32)sizeof(NAND_INST), "NAND_INST");
      if (pInst != NULL) {
       _apInst[Unit] = pInst;
        pInst->Unit        = Unit;
#if FS_NAND_VERIFY_WRITE
        pInst->VerifyWrite = 1;
#endif
#if FS_NAND_VERIFY_ERASE
        pInst->VerifyErase = 1;
#endif
      }
    }
  }
  return pInst;
}

/*********************************************************************
*
*       _GetInst
*
*  Function description
*    Returns a driver instance by by unit number.
*/
static NAND_INST * _GetInst(U8 Unit) {
  NAND_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  pInst = NULL;
  if (Unit < (U8)FS_NAND_NUM_UNITS) {
    pInst = _apInst[Unit];
  }
  return pInst;
}

/*********************************************************************
*
*       _Unmount
*/
static void _Unmount(NAND_INST * pInst) {
  pInst->IsLLMounted           = 0;
  pInst->MRUFreeBlock          = 0;
  pInst->pFirstWorkBlockFree   = NULL;
  pInst->pFirstWorkBlockInUse  = NULL;
#if FS_NAND_SUPPORT_FAST_WRITE
  pInst->pFirstWorkBlockErased = NULL;
#endif
#if FS_NAND_ENABLE_STATS
  FS_MEMSET(&pInst->StatCounters, 0, sizeof(pInst->StatCounters));
#endif
}

/*********************************************************************
*
*       _ExecCmdGetDevInfo
*/
static int _ExecCmdGetDevInfo(NAND_INST * pInst, void * pBuffer) {
  int           r;
  int           Result;
  FS_DEV_INFO * pDevInfo;

  r = -1;           // Set to indicate failure.
  if (pBuffer != NULL) {
    //
    // This low-level mount is required in oder to calculate
    // the correct number of sectors available to the file system
    //
    Result = _LowLevelMountIfRequired(pInst);
    if (Result == 0) {
      pDevInfo = SEGGER_PTR2PTR(FS_DEV_INFO, pBuffer);
      pDevInfo->NumSectors     = pInst->NumSectors;
      pDevInfo->BytesPerSector = pInst->BytesPerSector;
      r = 0;
    }
  }
  return r;
}

/*********************************************************************
*
*       _ExecCmdFormatLowLevel
*/
static int _ExecCmdFormatLowLevel(NAND_INST * pInst) {
  int r;
  int Result;

  r = -1;         // Set to indicate failure.
  Result = _LowLevelFormat(pInst);
  if (Result == 0) {
    r = 0;
  }
  return r;
}

/*********************************************************************
*
*       _ExecCmdRequiresFormat
*/
static int _ExecCmdRequiresFormat(NAND_INST * pInst) {
  int r;
  int Result;

  r = 1;          // Set to indicate that the NAND flash device is not formatted.
  Result = _LowLevelMountIfRequired(pInst);
  if (Result == 0) {
    r = 0;        // NAND flash is formatted.
  }
  return r;
}

/*********************************************************************
*
*       _ExecCmdUnmount
*/
static int _ExecCmdUnmount(NAND_INST * pInst) {
  _Unmount(pInst);
  return 0;
}

/*********************************************************************
*
*       _ExecCmdGetSectorUsage
*/
static int _ExecCmdGetSectorUsage(NAND_INST * pInst, int Aux, void * pBuffer) {
  int   r;
  int   Result;
  int * pSectorUsage;

  r = -1;         // Set to indicate failure.
  if (pBuffer != NULL) {
    Result = _LowLevelMountIfRequired(pInst);
    if (Result == 0) {
      pSectorUsage  = SEGGER_PTR2PTR(int, pBuffer);
      *pSectorUsage = _GetSectorUsage(pInst, (U32)Aux);
      r = 0;
    }
  }
  return r;
}

#if FS_NAND_SUPPORT_CLEAN

/*********************************************************************
*
*       _ExecCmdCleanOne
*/
static int _ExecCmdCleanOne(NAND_INST * pInst, void * pBuffer) {
  int   r;
  int   Result;
  int   More;
  int * pMore;

  r = -1;           // Set to indicate failure.
  Result = _LowLevelMountIfRequired(pInst);
  if (Result == 0) {
    More = 0;
    Result = _CleanOne(pInst, &More);
    pMore = SEGGER_PTR2PTR(int, pBuffer);
    if (pMore != NULL) {
      *pMore = More;
    }
    if (Result == 0) {
      r = 0;
    }
  }
  return r;
}

/*********************************************************************
*
*       _ExecCmdClean
*/
static int _ExecCmdClean(NAND_INST * pInst) {
  int r;
  int Result;

  r = -1;           // Set to indicate failure.
  Result = _LowLevelMountIfRequired(pInst);
  if (Result == 0) {
    Result = _Clean(pInst);
    if (Result == 0) {
      r = 0;
    }
  }
  return r;
}

/*********************************************************************
*
*       _ExecCmdGetCleanCnt
*/
static int _ExecCmdGetCleanCnt(NAND_INST * pInst, void * pBuffer) {
  int   r;
  int   Result;
  U32   Cnt;
  U32 * pCnt;

  r = -1;           // Set to indicate failure.
  Result = _LowLevelMountIfRequired(pInst);
  if (Result == 0) {
    Cnt = _GetCleanCnt(pInst);
    pCnt = SEGGER_PTR2PTR(U32, pBuffer);
    if (pCnt != NULL) {
      *pCnt = Cnt;
      r = 0;
    }
  }
  return r;
}

#endif // FS_NAND_SUPPORT_CLEAN

#if FS_NAND_SUPPORT_TRIM

/*********************************************************************
*
*       _ExecCmdFreeSectors
*/
static int _ExecCmdFreeSectors(NAND_INST * pInst, int Aux, const void * pBuffer) {
  int r;
  int Result;
  U32 SectorIndex;
  U32 NumSectors;

  r = -1;         // Set to indicate failure.
  if (pBuffer != NULL) {
    Result = _LowLevelMountIfRequired(pInst);
    if (Result == 0) {
      SectorIndex = (U32)Aux;
      NumSectors  = *SEGGER_CONSTPTR2PTR(const U32, pBuffer);
      Result = _FreeSectors(pInst, SectorIndex, NumSectors);
      if (Result == 0) {
        r = 0;
      }
    }
  }
  return r;
}

#endif // FS_NAND_SUPPORT_TRIM

#if FS_SUPPORT_DEINIT

/*********************************************************************
*
*       _ExecCmdDeInit
*/
static int _ExecCmdDeInit(NAND_INST * pInst) {
  U8 Unit;

  Unit = pInst->Unit;
  if (pInst->pPhyType != NULL) {
    if (pInst->pPhyType->pfDeInit != NULL) {
      pInst->pPhyType->pfDeInit(Unit);
    }
  }
  FS_FREE(pInst->pLog2PhyTable);
  FS_FREE(pInst->pFreeMap);
  if (pInst->paWorkBlock != NULL) {       // The array is allocated only when the volume is mounted.
    NAND_WORK_BLOCK * pWorkBlock;

    pWorkBlock = pInst->paWorkBlock;
    FS_FREE(pWorkBlock->paIsWritten);
    FS_FREE(pWorkBlock->paAssign);
    FS_FREE(pInst->paWorkBlock);
  }
  FS_FREE(pInst);
  _apInst[Unit] = NULL;
  //
  // If all instances have been removed, remove the sector buffers.
  //
  if (--_NumUnits == 0u) {
    FS_FREE(_pSectorBuffer);
    _pSectorBuffer = NULL;
    FS_FREE(_pSpareAreaData);
    _pSpareAreaData = NULL;
  }
  return 0;
}

#endif // FS_SUPPORT_DEINIT

/*********************************************************************
*
*       Static code (public via callback)
*
**********************************************************************
*/

/*********************************************************************
*
*       _NAND_GetStatus
*/
static int _NAND_GetStatus(U8 Unit) {
  FS_USE_PARA(Unit);
  return FS_MEDIA_IS_PRESENT;
}

/*********************************************************************
*
*       _NAND_Write
*
*  Function description
*    FS driver function. Writes one or more logical sectors to storage device.
*
*  Parameters
*    Unit           Driver unit number.
*    SectorIndex    Index of the first logical sector to be modified.
*    pData          [IN]  Data to be written.
*    NumSectors     Number of sectors to write.
*    RepeatSame     Set to 1 if the same data should be written to all sectors.
*
*  Return value
*    ==0    Data successfully written.
*    !=0    An error has occurred.
*/
static int _NAND_Write(U8 Unit, U32 SectorIndex, const void * pData, U32 NumSectors, U8 RepeatSame) {
  const U8  * pData8;
  NAND_INST * pInst;
  int         r;
  int         HasFatalError;
  U32         FirstSector;
  U32         LastSector;
  U32         NumSectorsTotal;

  if (NumSectors == 0u) {
    return 0;                   // OK, nothing to do.
  }
  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;                   // Error, could not get driver instance.
  }
  r = _LowLevelMountIfRequired(pInst);
  if (r != 0) {
    return r;                   // Error, could not mount the NAND flash device.
  }
  if (pInst->IsWriteProtected != 0u) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND: _NAND_Write: NAND flash is write protected."));
    return 1;
  }
  //
  // Check that the sectors to be written are in range.
  //
  FirstSector     = SectorIndex;
  LastSector      = SectorIndex + NumSectors - 1u;
  NumSectorsTotal = pInst->NumSectors;
  if ((FirstSector >= NumSectorsTotal) || (LastSector >= NumSectorsTotal)) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NAND: _NAND_Write: Invalid sector range."));
    return 1;       // Error, trying to write sectors which do not exist.
  }
  //
  // Write data one sector at a time
  //
  HasFatalError = (int)pInst->HasFatalError;
  pData8        = SEGGER_CONSTPTR2PTR(const U8, pData);
  for (;;) {
    r = _WriteLogSector(pInst, SectorIndex, pData8);
    if (r != 0) {
      CHECK_CONSISTENCY(pInst);
      return 1;       // Error, could not write data.
    }
    if ((HasFatalError == 0) && (pInst->HasFatalError != 0u)) {
      CHECK_CONSISTENCY(pInst);
      return 1;       // Error, a fatal error occurred while writing the data.
    }
    CHECK_CONSISTENCY(pInst);
    IF_STATS(pInst->StatCounters.WriteSectorCnt++);
    if (--NumSectors == 0u) {
      break;
    }
    if (RepeatSame == 0u) {
      pData8 += pInst->BytesPerSector;
    }
    SectorIndex++;
  }
  return 0;           // O.K., sector data written.
}

/*********************************************************************
*
*       _NAND_Read
*
*  Function description
*    FS driver function. Reads one or more logical sectors from storage device.
*
*  Parameters
*    Unit           Driver unit number.
*    SectorIndex    Index of the first logical sector to be read.
*    pData          [OUT] Sector data read from device.
*    NumSectors     Number of logical sectors to read.
*
*  Return value
*    ==0    Data successfully read.
*    !=0    An error has occurred.
*/
static int _NAND_Read(U8 Unit, U32 SectorIndex, void * pData, U32 NumSectors) {
  U8        * pData8;
  NAND_INST * pInst;
  int         r;
  U32         FirstSector;
  U32         LastSector;
  U32         NumSectorsTotal;

  if (NumSectors == 0u) {
    return 0;                   // OK, nothing to do.
  }
  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;                   // Error, could not get driver instance.
  }
  //
  // Make sure device is low-level mounted. If it is not, there is nothing we can do.
  //
  r = _LowLevelMountIfRequired(pInst);
  if (r != 0) {
    return 1;                   // Error, could not mount NAND flash device.
  }
  //
  // Check that the sectors to be read are in range.
  //
  FirstSector     = SectorIndex;
  LastSector      = SectorIndex + NumSectors - 1u;
  NumSectorsTotal = pInst->NumSectors;
  if ((FirstSector >= NumSectorsTotal) || (LastSector >= NumSectorsTotal)) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NAND: _NAND_Read: Invalid sector range."));
    return 1;       // Error, trying to read sectors which do not exist.
  }
  //
  // Read data one sector at a time.
  //
  pData8 = SEGGER_PTR2PTR(U8, pData);
  do {
    r = _ReadSector(pInst, SectorIndex, pData8);
    if (r != 0) {
      CHECK_CONSISTENCY(pInst);
      return 1;       // Error, could not read sector data.
    }
    CHECK_CONSISTENCY(pInst);
    pData8 += pInst->BytesPerSector;
    SectorIndex++;
    IF_STATS(pInst->StatCounters.ReadSectorCnt++);
  } while (--NumSectors != 0u);
  return 0;           // O.K.
}

/*********************************************************************
*
*       _NAND_IoCtl
*/
static int _NAND_IoCtl(U8 Unit, I32 Cmd, I32 Aux, void * pBuffer) {
  NAND_INST * pInst;
  int         r;
  int         IsLLMounted;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return -1;                  // Error, could not get driver instance.
  }
  r = -1;                       // Set to indicate an error.
  IsLLMounted = (int)pInst->IsLLMounted;
  switch (Cmd) {
  case FS_CMD_GET_DEVINFO:
    r = _ExecCmdGetDevInfo(pInst, pBuffer);
    break;
  case FS_CMD_FORMAT_LOW_LEVEL:
    r = _ExecCmdFormatLowLevel(pInst);
    break;
  case FS_CMD_REQUIRES_FORMAT:
    r = _ExecCmdRequiresFormat(pInst);
    break;
  case FS_CMD_UNMOUNT:
    //lint through
  case FS_CMD_UNMOUNT_FORCED:
    r = _ExecCmdUnmount(pInst);
    break;
#if FS_NAND_SUPPORT_CLEAN
  case FS_CMD_CLEAN_ONE:
    r = _ExecCmdCleanOne(pInst, pBuffer);
    break;
  case FS_CMD_CLEAN:
    r = _ExecCmdClean(pInst);
    break;
  case FS_CMD_GET_CLEAN_CNT:
    r = _ExecCmdGetCleanCnt(pInst, pBuffer);
    break;
#endif // FS_NAND_SUPPORT_CLEAN
  case FS_CMD_GET_SECTOR_USAGE:
    r = _ExecCmdGetSectorUsage(pInst, Aux, pBuffer);
    break;
  case FS_CMD_FREE_SECTORS:
#if FS_NAND_SUPPORT_TRIM
    r = _ExecCmdFreeSectors(pInst, Aux, pBuffer);
#else
    //
    // Return OK even if we do nothing here in order to
    // prevent that the file system reports an error.
    //
    r = 0;
#endif // FS_NAND_SUPPORT_TRIM
    break;
#if FS_SUPPORT_DEINIT
  case FS_CMD_DEINIT:
    r = _ExecCmdDeInit(pInst);
    break;
#endif // FS_SUPPORT_DEINIT
  default:
    //
    // Error, command not supported.
    //
    break;
  }
  //
  // Check the consistency only if the NAND flash device
  // is mounted by the current I/O control operation.
  //
  if (IsLLMounted == 0) {
    CHECK_CONSISTENCY(pInst);
  }
  return r;
}

/*********************************************************************
*
*       _NAND_AddDevice
*
*  Function description
*    Initializes the low-level driver object.
*
*  Return value
*    >=0    Unit number.
*    < 0    Error, could not add device.
*/
static int _NAND_AddDevice(void) {
  NAND_INST * pInst;

  if (_NumUnits >= (U8)FS_NAND_NUM_UNITS) {
    return -1;                  // Error, too many driver instances.
  }
  pInst = _AllocInstIfRequired((U8)_NumUnits);
  if (pInst == NULL) {
    return -1;                  // Error, could not allocate driver instance.
  }
  return (int)_NumUnits++;
}

/*********************************************************************
*
*       _NAND_InitMedium
*
*  Function description
*    Initialize and identifies the storage device.
*
*  Return value
*    ==0    Device is ready for operation.
*    < 0    An error has occurred.
*/
static int _NAND_InitMedium(U8 Unit) {
  int         r;
  NAND_INST * pInst;

  r = 1;                      // Set to indicate an error.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    r = _InitIfRequired(pInst);
  }
  return r;
}

/*********************************************************************
*
*       _NAND_GetNumUnits
*/
static int _NAND_GetNumUnits(void) {
  return (int)_NumUnits;
}

/*********************************************************************
*
*       _NAND_GetDriverName
*/
static const char * _NAND_GetDriverName(U8 Unit) {
  FS_USE_PARA(Unit);
  return "nand";
}

/*********************************************************************
*
*       Driver API Table
*/
const FS_DEVICE_TYPE FS_NAND_Driver = {
  _NAND_GetDriverName,
  _NAND_AddDevice,
  _NAND_Read,
  _NAND_Write,
  _NAND_IoCtl,
  _NAND_InitMedium,
  _NAND_GetStatus,
  _NAND_GetNumUnits
};

/*********************************************************************
*
*       Public code (internal)
*
**********************************************************************
*/

#if FS_SUPPORT_TEST

/*********************************************************************
*
*       FS__NAND_SetTestHookFailSafe
*/
void FS__NAND_SetTestHookFailSafe(FS_NAND_TEST_HOOK_NOTIFICATION * pfTestHook) {
  _pfTestHook = pfTestHook;
}

#endif // FS_SUPPORT_TEST

/*********************************************************************
*
*       FS_NAND_Validate
*
*  Function description
*    Performs unit tests on some of the internal routines
*
*  Return value
*    ==0    OK
*    !=0    An error occurred
*/
int FS_NAND_Validate(void) {
  int r;
  // 00000000 11111100 22221111 33222222 33333333 44444444 55555544 66665555 77666666
  // 76543210 54321098 32109876 10987654 98765432 76543210 54321098 32109876 10987654
  //
  // 01010011 01000101 01000111 01000111 01000101 01010010 11111111 11111111 01111111
  U8  aData[] = {0x53, 0x45, 0x47, 0x47, 0x45, 0x52, 0xFF, 0xFF, 0x7F};

  r = _Find0BitInByte(0xFF, 0, 7, 0);
  if (r != -1) {
    return 1;
  }
  r = _Find0BitInByte(0xFE, 0, 0, 0);
  if (r != 0) {
    return 1;
  }
  r = _Find0BitInByte(0x7F, 7, 7, 0);
  if (r != 7) {
    return 1;
  }
  r = _Find0BitInByte(0xEF, 2, 4, 0);
  if (r != 4) {
    return 1;
  }
  r = _Find0BitInByte(0xF7, 3, 4, 0);
  if (r != 3) {
    return 1;
  }
  r = _Find0BitInByte(0xF1, 0, 1, 0);
  if (r != 1) {
    return 1;
  }
  r = _Find0BitInByte(0xF3, 1, 6, 0);
  if (r != 2) {
    return 1;
  }
  r = _Find0BitInByte(0xF7, 3 ,3 ,5);
  if (r != (3 + (5 * 8))) {
    return 1;
  }
  r = _Find0BitInArray(aData, 3, 3);
  if (r != 3) {
    return 1;
  }
  r = _Find0BitInArray(aData, 7, 16);
  if (r != 7) {
    return 1;
  }
  r = _Find0BitInArray(aData, 16, 18);
  if (r != -1) {
    return 1;
  }
  r = _Find0BitInArray(aData, 44, 47);
  if (r != 45) {
    return 1;
  }
  r = _Find0BitInArray(aData, 5, 47);
  if (r != 5) {
    return 1;
  }
  r = _Find0BitInArray(aData, 55, 71);
  if (r != 71) {
    return 1;
  }
  r = (int)_Count1Bits(0xFFFFFFFFuL);
  if (r != 32) {
    return 1;
  }
  r = (int)_Count1Bits(0);
  if (r != 0) {
    return 1;
  }
  r = (int)_Count1Bits(0xAAAAAAAAuL);
  if (r != 16) {
    return 1;
  }
  r = (int)_Count1Bits(0x55555555uL);
  if (r != 16) {
    return 1;
  }
  r = (int)_Count1Bits(0x33333333uL);
  if (r != 16) {
    return 1;
  }
  r = (int)_Count1Bits(0xCCCCCCCCuL);
  if (r != 16) {
    return 1;
  }
  r = (int)_Count1Bits(0x0F0F0F0FuL);
  if (r != 16) {
    return 1;
  }
  r = (int)_Count1Bits(0xF0F0F0F0uL);
  if (r != 16) {
    return 1;
  }
  r = (int)_Count1Bits(0x00FF00FFuL);
  if (r != 16) {
    return 1;
  }
  r = (int)_Count1Bits(0xFF00FF00uL);
  if (r != 16) {
    return 1;
  }
  r = (int)_Count1Bits(0x0000FFFFuL);
  if (r != 16) {
    return 1;
  }
  r = (int)_Count1Bits(0xFFFF0000uL);
  if (r != 16) {
    return 1;
  }
  r = (int)_Count1Bits(0x12345678uL);
  if (r != 13) {
    return 1;
  }
  return 0;
}

/*********************************************************************
*
*       FS__NAND_GetPhyType
*/
const FS_NAND_PHY_TYPE * FS__NAND_GetPhyType(U8 Unit) {
  NAND_INST              * pInst;
  const FS_NAND_PHY_TYPE * pPhyType;

  pPhyType = NULL;
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    pPhyType = pInst->pPhyType;
  }
  return pPhyType;
}

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

#if FS_NAND_ENABLE_STATS

/*********************************************************************
*
*       FS_NAND_GetStatCounters
*
*  Function description
*    Returns the actual values of statistical counters.
*
*  Parameters
*    Unit     Index of the driver instance (0-based)
*    pStat    [OUT] Values of statistical counters.
*
*  Additional information
*    This function is optional. It is active only when the file system
*    is compiled with FS_DEBUG_LEVEL set to a value greater than or
*    equal to FS_DEBUG_LEVEL_CHECK_ALL or FS_NAND_ENABLE_STATS set to 1.
*
*    The statistical counters can be cleared via FS_NAND_ResetStatCounters().
*/
void FS_NAND_GetStatCounters(U8 Unit, FS_NAND_STAT_COUNTERS * pStat) {
  NAND_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    if (pStat != NULL) {
      *pStat = pInst->StatCounters;
    }
  }
}

/*********************************************************************
*
*       FS_NAND_ResetStatCounters
*
*  Function description
*    Sets the values of statistical counters to 0.
*
*  Parameters
*    Unit     Index of the driver instance (0-based)
*
*  Additional information
*    This function is optional. It is active only when the file system
*    is compiled with FS_DEBUG_LEVEL set to a value greater than or
*    equal to FS_DEBUG_LEVEL_CHECK_ALL or FS_NAND_ENABLE_STATS set to 1.
*
*    The statistical counters can be queried via FS_NAND_GetStatCounters().
*/
void FS_NAND_ResetStatCounters(U8 Unit) {
  NAND_INST             * pInst;
  FS_NAND_STAT_COUNTERS * pStat;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pStat = &pInst->StatCounters;
    pStat->ConvertInPlaceCnt = 0;
    pStat->ConvertViaCopyCnt = 0;
    pStat->CopySectorCnt     = 0;
    pStat->EraseCnt          = 0;
    pStat->NumReadRetries    = 0;
    pStat->ReadDataCnt       = 0;
    pStat->ReadSectorCnt     = 0;
    pStat->ReadSpareCnt      = 0;
    pStat->WriteDataCnt      = 0;
    pStat->WriteSectorCnt    = 0;
    pStat->WriteSpareCnt     = 0;
  }
}

#endif // FS_NAND_ENABLE_STATS

/*********************************************************************
*
*       FS_NAND_SetPhyType
*
*  Function description
*    Configures NAND flash access functions.
*
*  Parameters
*    Unit     Index of the driver instance (0-based)
*    pPhyType [IN] Physical layer to be used to access the NAND flash device.
*
*  Additional information
*    This function is mandatory and it has to be called in FS_X_AddDevices()
*    once for each instance of the SLC1 NAND driver. The driver instance
*    is identified by the Unit parameter. First SLC1 NAND driver instance
*    added to the file system via a FS_AddDevice(&FS_NAND_Driver) call
*    has the unit number 0, the SLC1 NAND driver added by a second call
*    to FS_AddDevice() has the unit number 1 and so on.
*/
void FS_NAND_SetPhyType(U8 Unit, const FS_NAND_PHY_TYPE * pPhyType) {
  NAND_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    if (pPhyType != NULL) {
      pInst->pPhyType = pPhyType;
    }
  }
}

/*********************************************************************
*
*       FS_NAND_SetBlockRange
*
*  Function description
*    Specifies which NAND flash blocks can used as data storage.
*
*  Parameters
*    Unit             Index of the driver instance (0-based).
*    FirstBlock       Index of the first NAND flash block to
*                     be used as storage (0-based).
*    MaxNumBlocks     Maximum number of NAND flash blocks
*                     to be used as storage.
*
*  Additional information
*    This function is optional. By default, the SLC1 NAND driver uses
*    all blocks of the NAND flash as data storage. FS_NAND_SetBlockRange()
*    is useful when a part of the NAND flash has to be used for another
*    purpose, for example to store the application program used by a boot loader,
*    and therefore it cannot be managed by the SLC1 NAND driver. Limiting
*    the number of blocks used by the SLC1 NAND driver can also help reduce the RAM usage.
*
*    FirstBlock is the index of the first physical NAND block were
*    0 is the index of the first block of the NAND flash device.
*    MaxNumBlocks can be larger that the actual number of available
*    NAND blocks in which case the SCL1 NAND driver silently truncates
*    the value to reflect the actual number of NAND blocks available.
*
*    The SLC1 NAND driver uses the first NAND block in the range to store
*    management information at low-level format. If the first NAND block happens
*    to be marked as defective, then the next usable NAND block is used.
*
*    The read optimization of the FS_NAND_PHY_2048x8 physical layer has to be
*    disabled when this function is used to partition the NAND flash device
*    in order to ensure data consistency. The read cache can be disabled at
*    runtime using FS_NAND_2048x8_DisableReadCache().
*
*    If the FS_NAND_SetBlockRange() is used to subdivide the same
*    physical NAND flash device into two or more partitions than
*    the application has to make sure that they do not overlap.
*/
void FS_NAND_SetBlockRange(U8 Unit, U16 FirstBlock, U16 MaxNumBlocks) {
  NAND_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->FirstBlock   = FirstBlock;
    pInst->MaxNumBlocks = MaxNumBlocks;
  }
}

/*********************************************************************
*
*       FS_NAND_IsLLFormatted
*
*  Function description
*    Checks if the NAND flash is low-level formatted.
*
*  Parameters
*    Unit             Index of the driver instance (0-based).
*
*  Return value
*    ==1 - NAND flash device is low-level formatted.
*    ==0 -  NAND flash device is not low-level formatted.
*    < 0 - An error occurred.
*
*  Additional information
*    This function is optional.
*/
int FS_NAND_IsLLFormatted(U8 Unit) {
  NAND_INST * pInst;
  int         r;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 0;           // Error, could not allocate driver instance.
  }
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return 0;           // Error, could not initialize NAND flash device.
  }
  r = _LowLevelMountIfRequired(pInst);
  if (r != 0) {
    return 0;           // OK, NAND flash device is not formatted.
  }
  return 1;             // OK, NAND flash device is formatted.
}

/*********************************************************************
*
*       FS_NAND_SetMaxEraseCntDiff
*
*  Function description
*    Configures the threshold of the wear leveling procedure.
*
*  Parameters
*    Unit           Index of the driver instance (0-based).
*    EraseCntDiff   Maximum allowed difference between the erase
*                   counts of any two NAND blocks.
*
*  Additional information
*    This function is optional. It can be used to control how
*    the SLC1 NAND driver performs the wear leveling.
*    The wear leveling procedure makes sure that the NAND blocks
*    are equally erased to meet the life expectancy of the storage
*    device by keeping track of the number of times a NAND block
*    has been erased (erase count). The SLC1 NAND driver executes
*    this procedure when a new empty NAND block is required for
*    data storage. The wear leveling procedure works by first
*    choosing the next available NAND block. Then the difference
*    between the erase count of the chosen block and the NAND block
*    with lowest erase count is computed. If this value is greater
*    than EraseCntDiff the NAND block with the lowest erase count
*    is freed and made available for use.
*
*    The same threshold can also be configured at compile time
*    via the FS_NAND_MAX_ERASE_CNT_DIFF configuration define.
*/
void FS_NAND_SetMaxEraseCntDiff(U8 Unit, U32 EraseCntDiff) {
  NAND_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->MaxEraseCntDiff = EraseCntDiff;
  }
}

/*********************************************************************
*
*       FS_NAND_SetNumWorkBlocks
*
*  Function description
*    Sets number of work blocks the SLC1 NAND driver uses for write operations.
*
*  Parameters
*    Unit           Index of the driver instance (0-based).
*    NumWorkBlocks  Number of work blocks.
*
*  Additional information
*    This function is optional. It can be used to change the default
*    number of work blocks according to the requirements of the application.
*    Work blocks are physical NAND blocks that the SLC1 NAND driver
*    uses to temporarily store the data written to NAND flash device.
*    The SLC1 NAND driver calculates at low-level format the number of
*    work blocks based on the total number of blocks available on the
*    NAND flash device.
*
*    By default, the NAND driver allocates 10% of the total number of
*    NAND blocks used as storage, but no more than 10 NAND blocks.
*    The minimum number of work blocks allocated by default depends
*    on whether journaling is used or not. If the journal is active
*    4 work blocks are allocated, else SLC1 NAND driver allocates 3
*    work blocks. The currently allocated number of work blocks can
*    be checked via FS_NAND_GetDiskInfo(). The value is returned in the
*    NumWorkBlocks member of the FS_NAND_DISK_INFO structure.
*
*    Increasing the number of work blocks can help increase the write
*    performance of the SLC1 NAND driver. At the same time the RAM
*    usage of the SLC1 NAND driver increases since each configured
*    work block requires a certain amount of RAM for the data management.
*    This is a trade-off between write performance and RAM usage.
*
*    The new value take effect after the NAND flash device is low-level
*    formatted via FS_FormatLow() or FS_NAND_FormatLow() API functions.
*/
void FS_NAND_SetNumWorkBlocks(U8 Unit, U32 NumWorkBlocks) {
  NAND_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->NumWorkBlocksConf = NumWorkBlocks;
  }
}

/*********************************************************************
*
*       FS_NAND_FormatLow
*
*  Function description
*    Performs a low-level format of the NAND flash device.
*
*  Parameters
*    Unit           Index of the driver instance (0-based).
*
*  Return value
*    ==0 - OK, NAND flash device is low-level formatted.
*    !=0 - An error occurred.
*
*  Additional information
*    This function is optional. It is recommended that application
*    use FS_FormatLow() to initialize the NAND flash device instead
*    of FS_NAND_FormatLow().
*
*    After the low-level format operation all data that was stored on
*    the NAND flash device is lost. A low-level format has to be performed
*    only once before using the NAND flash device for the first time.
*    The application can check if the NAND flash device is low-level
*    formatted by calling FS_IsLLFormated() or alternatively
*    FS_NAND_IsLLFormated().
*/
int FS_NAND_FormatLow(U8 Unit) {
  NAND_INST * pInst;
  int         r;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;       // Error, could allocate driver instance.
  }
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return 1;       // Error, could not initialize NAND flash device.
  }
  r = _LowLevelFormat(pInst);
  return r;
}

#if FS_NAND_SUPPORT_FAST_WRITE

/*********************************************************************
*
*       FS_NAND_SetCleanThreshold
*
*  Function description
*    Specifies the minimum number sectors that the driver should keep
*    available for fast write operations.
*
*  Parameters
*    Unit             Index of the driver instance (0-based).
*    NumBlocksFree    Number of blocks to be kept free.
*    NumSectorsFree   Number of sectors to be kept free on each block.
*
*  Return value
*    ==0 - OK, threshold set.
*    !=0 - An error occurred.
*
*  Additional information
*    Typically, used for allowing the NAND flash to write data fast to a file
*    on a sudden reset. At the startup, the application reserves free space,
*    by calling the function with NumBlocksFree and NumSectorsFree set to a
*    value different than 0. The number of free sectors depends on the number
*    of bytes written and on how the file system is configured.
*    When the unexpected reset occurs, the application tells the driver that
*    it can write to the free sectors, by calling the function with
*    NumBlocksFree and NumSectorsFree set to 0. Then, the application writes
*    the data to file and the NAND driver stores it to the free space.
*    Since no erase or copy operation is required, the data is written as
*    fastest as the NAND flash device permits it.
*
*    The NAND flash device will wear out faster than normal if sectors
*    are reserved in a work block (NumSectors > 0).
*/
int FS_NAND_SetCleanThreshold(U8 Unit, unsigned NumBlocksFree, unsigned NumSectorsFree) {
  NAND_INST * pInst;
  unsigned    NumBlocksFreeOld;
  unsigned    NumSectorsFreeOld;
  int         r;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;                 // Error, could not allocate driver instance.
  }
  r = 0;                      // Set to indicate success.
  NumBlocksFreeOld  = pInst->NumBlocksFree;
  NumSectorsFreeOld = pInst->NumSectorsFree;
  pInst->NumBlocksFree  = (U16)NumBlocksFree;
  pInst->NumSectorsFree = (U16)NumSectorsFree;
  //
  // Free work blocks if required.
  //
  if ((NumBlocksFree  > NumBlocksFreeOld) ||
      (NumSectorsFree > NumSectorsFreeOld)) {
    if (pInst->IsLLMounted != 0u) {
      r = _ApplyCleanThreshold(pInst);
    }
  }
  //
  // Put work block descriptors with assigned erased blocks
  // in front of the free work block list so that _AllocWorkBlock()
  // can return them on the next call.
  //
  if (NumBlocksFree < NumBlocksFreeOld) {
    NAND_WORK_BLOCK * pWorkBlock;
    NAND_WORK_BLOCK * pWorkBlockNext;

    NumBlocksFree = NumBlocksFreeOld - NumBlocksFree;
    pWorkBlock = pInst->pFirstWorkBlockErased;
    while (pWorkBlock != NULL) {
      pWorkBlockNext = pWorkBlock->pNext;
      _WB_RemoveFromErasedList(pInst, pWorkBlock);
      _WB_AddErasedToFreeList(pInst, pWorkBlock);
      if (--NumBlocksFree == 0u) {
        break;
      }
      pWorkBlock = pWorkBlockNext;
    }
  }
  return r;
}

/*********************************************************************
*
*       FS_NAND_Clean
*
*  Function description
*    Makes sectors available for fast write operations.
*
*  Parameters
*    Unit             Index of the driver instance (0-based).
*    NumBlocksFree    Number of blocks to be kept free.
*    NumSectorsFree   Number of sectors to be kept free on each block.
*
*  Return value
*    ==0 - OK
*    !=0 - An error occurred
*
*  Additional information
*    This function is optional. It can be used to free space on the
*    NAND flash device for data that the application has to write
*    as fast as possible. FS_NAND_UNI_Clean() performs two internal
*    operations:
*    (1) Converts all work blocks that have less free sectors than
*        NumSectorsFree into data blocks.
*    (2) If required, convert work blocks until at least NumBlocksFree
*        are available.
*/
int FS_NAND_Clean(U8 Unit, unsigned NumBlocksFree, unsigned NumSectorsFree) {
  NAND_INST * pInst;
  int         r;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;         // Error, could not allocate driver instance.
  }
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return 1;         // Error, could not initialize NAND flash device.
  }
  r = _LowLevelMountIfRequired(pInst);
  if (r != 0) {
    return 1;         // Error, could not mount NAND flash device.
  }
  r = _CleanLimited(pInst, NumBlocksFree, NumSectorsFree);
  return r;
}

#endif // FS_NAND_SUPPORT_FAST_WRITE

/*********************************************************************
*
*       FS_NAND_ReadPhySector
*
*  Function description
*    This function reads a physical sector from NAND flash.
*
*  Parameters
*    Unit             Index of the driver instance (0-based).
*    PhySectorIndex   Physical sector index.
*    pData            Pointer to a buffer to store read data.
*    pNumBytesData    [IN]  Pointer to variable storing the size of the data buffer.
*                     [OUT] The number of bytes that were stored in the data buffer.
*    pSpare           Pointer to a buffer to store read spare data.
*    pNumBytesSpare   [IN]  Pointer to variable storing the size of the spare data buffer.
*                     [OUT] The number of bytes that were stored in the spare data buffer.
*
*  Return value
*    < 0 - OK, all bytes are set to 0xFF in the physical sector.
*    ==0 - OK, no bit errors.
*    ==1 - OK, one bit error found and corrected.
*    ==2 - OK, one bit error found in the ECC.
*    ==3 - Error, more than 1 bit errors occurred.
*    ==4 - Error, could not read form NAND flash device.
*    ==5 - Error, internal operation.
*
*  Additional information
*    This function is optional.
*/
int FS_NAND_ReadPhySector(U8 Unit, U32 PhySectorIndex, void * pData, unsigned * pNumBytesData, void * pSpare, unsigned * pNumBytesSpare) {
  NAND_INST * pInst;
  U32         NumPhySectors;
  int         r;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 5;         // Error, could not allocate driver instance.
  }
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return 5;         // Error, could not initialize NAND flash.
  }
  (void)_LowLevelMountIfRequired(pInst);    // Mount the NAND flash if necessary
  NumPhySectors = (U32)pInst->NumPhyBlocks * (1uL << pInst->SPB_Shift);
  if (PhySectorIndex < NumPhySectors) {
    U32 NumBytes2Copy;

    r = _ReadSectorWithECC(pInst, _pSectorBuffer, PhySectorIndex);
    NumBytes2Copy = SEGGER_MIN(pInst->BytesPerSector, *pNumBytesData);
    *pNumBytesData = NumBytes2Copy;
    FS_MEMCPY(pData, _pSectorBuffer, NumBytes2Copy);
    NumBytes2Copy = SEGGER_MIN((unsigned)pInst->BytesPerSector >> 5, *pNumBytesSpare);
    *pNumBytesSpare = NumBytes2Copy;
    FS_MEMCPY(pSpare, _pSpareAreaData, NumBytes2Copy);
  }
  return r;
}

/*********************************************************************
*
*       FS_NAND_EraseFlash
*
*  Function description
*    Erases the entire NAND partition.
*
*  Parameters
*    Unit   Index of the driver instance (0-based).
*
*  Return value
*    >=0 - Number of blocks which failed to erase.
*    < 0 - An error occurred.
*
*  Additional information
*    This function is optional. After the call to FS_NAND_EraseFlash()
*    all the bytes in the NAND partition are set to 0xFF.
*
*    This function has to be used with care, since it also erases
*    blocks marked as defective and therefore the information about
*    the block status will be lost. FS_NAND_EraseFlash() can be used
*    without this side effect on storage devices that are guaranteed
*    to not have any bad blocks, such as DataFlash devices.
*/
int FS_NAND_EraseFlash(U8 Unit) {
  NAND_INST * pInst;
  U32         NumBlocks;
  unsigned    iBlock;
  int         r;
  int         NumErrors;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return FS_ERRCODE_OUT_OF_MEMORY;        // Error, could not allocate driver instance.
  }
  //
  // Initialize the NAND flash, so that all necessary information
  // about the organization of the NAND flash are available.
  //
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return FS_ERRCODE_INIT_FAILURE;         // Error, could not initialize NAND flash device.
  }
  //
  // Erase all the NAND flash blocks including the ones marked as defective.
  //
  NumErrors = 0;
  NumBlocks = pInst->NumPhyBlocks;
  for (iBlock = 0; iBlock < NumBlocks; iBlock++) {
    r = _EraseBlock(pInst, iBlock);
    if (r != 0) {
      FS_DEBUG_WARN((FS_MTYPE_DRIVER, "NAND: FS_NAND_EraseFlash: Failed to erase block %d.", iBlock));
      ++NumErrors;
    }
  }
  //
  // Force a remount of the NAND flash.
  //
  _Unmount(pInst);
  return NumErrors;
}

/*********************************************************************
*
*       FS_NAND_GetDiskInfo
*
*  Function description
*    Returns information about the NAND partition.
*
*  Parameters
*    Unit       Index of the driver instance (0-based).
*    pDiskInfo  [OUT] Information about the NAND partition.
*
*  Return value
*    ==0 - OK, information returned.
*    !=0 - An error occurred.
*
*  Additional information
*    This function is not required for the functionality of the
*    SLC1 NAND driver and is typically not linked in production
*    builds.
*/
int FS_NAND_GetDiskInfo(U8 Unit, FS_NAND_DISK_INFO * pDiskInfo) {
  NAND_INST * pInst;
  unsigned    iBlock;
  U32         NumPhyBlocks;
  U32         NumUsedPhyBlocks;
  U32         NumBadPhyBlocks;
  U32         EraseCntMax;
  U32         EraseCntMin;
  U32         EraseCntAvg;
  U32         EraseCnt;
  U32         EraseCntTotal;
  U32         NumEraseCnt;
  int         IsFormatted;
  int         r;

  if (pDiskInfo == NULL) {
    return FS_ERRCODE_INVALID_PARA;         // Error, invalid parameter.
  }
  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return FS_ERRCODE_OUT_OF_MEMORY;        // Error, could not allocate driver instance.
  }
  //
  // Initialize the NAND flash, so that all necessary information
  // about the organization of the NAND flash are available.
  //
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return FS_ERRCODE_INIT_FAILURE;         // Error, could not initialize NAND flash device.
  }
  FS_MEMSET(pDiskInfo, 0, sizeof(FS_NAND_DISK_INFO));
  //
  // Retrieve the number of used physical blocks and block status
  //
  NumUsedPhyBlocks = 0;
  NumBadPhyBlocks  = 0;
  NumPhyBlocks     = pInst->NumPhyBlocks;
  EraseCntMax      = 0;
  EraseCntMin      = 0xFFFFFFFFuL;
  NumEraseCnt      = 0;
  EraseCntTotal    = 0;
  EraseCntAvg      = 0;
  IsFormatted      = 0;
  //
  // Low level mount the drive if not already done
  //
  (void)_LowLevelMountIfRequired(pInst);
  if (pInst->IsLLMounted != 0u) {
    IsFormatted = 1;                // OK, the NAND flash device is formatted.
  }
  if (IsFormatted != 0) {
    for (iBlock = 0; iBlock < NumPhyBlocks; iBlock++) {
      U32 PageIndex;
      U32 aSpare[2];     // Large enough to hold the bad block status and the erase count

      FS_MEMSET(aSpare, 0xFF, sizeof(aSpare));
      //
      // Check if block is free
      //
      if (_IsBlockFree(pInst, iBlock) == 0) {
        NumUsedPhyBlocks++;
      }
      //
      // The spare area of the first page in a block stores the bad block status
      // information and the erase count.
      //
      PageIndex  = (U32)iBlock << pInst->PPB_Shift;
      PageIndex += pInst->FirstBlock << pInst->PPB_Shift;
      if (_IsBlockBad(pInst, iBlock) != 0) {
        NumBadPhyBlocks++;
      } else {
        (void)_ReadPhySpare(pInst, PageIndex, aSpare, 0, sizeof(aSpare));
        EraseCnt = _LoadEraseCnt(pInst, SEGGER_PTR2PTR(U8, aSpare));
        if (EraseCnt != ERASE_CNT_INVALID) {
          if (EraseCnt > EraseCntMax) {
            EraseCntMax = EraseCnt;
          }
          if (EraseCnt < EraseCntMin) {
            EraseCntMin = EraseCnt;
          }
          EraseCntTotal += EraseCnt;
          ++NumEraseCnt;
        }
      }
    }
    if (NumEraseCnt != 0u) {
      EraseCntAvg = EraseCntTotal / NumEraseCnt;
    } else {
      EraseCntAvg = 0;
    }
  }
  pDiskInfo->NumPhyBlocks       = NumPhyBlocks;
  pDiskInfo->NumLogBlocks       = pInst->NumLogBlocks;
  pDiskInfo->NumPagesPerBlock   = 1uL << pInst->PPB_Shift;
  pDiskInfo->NumSectorsPerBlock = 1uL << pInst->SPB_Shift;
  pDiskInfo->BytesPerPage       = pInst->BytesPerPage;
  pDiskInfo->BytesPerSector     = pInst->BytesPerSector;
  pDiskInfo->NumUsedPhyBlocks   = NumUsedPhyBlocks;
  pDiskInfo->NumBadPhyBlocks    = NumBadPhyBlocks;
  pDiskInfo->EraseCntMax        = EraseCntMax;
  pDiskInfo->EraseCntMin        = EraseCntMin;
  pDiskInfo->EraseCntAvg        = EraseCntAvg;
  pDiskInfo->IsWriteProtected   = pInst->IsWriteProtected;
  pDiskInfo->HasFatalError      = pInst->HasFatalError;
  pDiskInfo->ErrorSectorIndex   = pInst->ErrorSectorIndex;
  pDiskInfo->ErrorType          = pInst->ErrorType;
  pDiskInfo->BlocksPerGroup     = 1;
  pDiskInfo->NumWorkBlocks      = pInst->NumWorkBlocks;
  pDiskInfo->IsFormatted        = (U8)IsFormatted;
  return 0;
}

/*********************************************************************
*
*       FS_NAND_GetBlockInfo
*
*   Function description
*     Returns information about a specified NAND block.
*
*  Parameters
*    Unit             Index of the driver instance (0-based).
*    PhyBlockIndex    Index of the physical block to get information about.
*    pBlockInfo       [OUT] Information about the NAND block.
*
*  Return value
*    ==0 - OK, information returned.
*    !=0 - An error occurred.
*
*  Additional information
*    This function is not required for the functionality of the SLC1 NAND driver
*    and is typically not linked in in production builds.
*/
int FS_NAND_GetBlockInfo(U8 Unit, U32 PhyBlockIndex, FS_NAND_BLOCK_INFO * pBlockInfo) {
  NAND_INST  * pInst;
  int          r;
  unsigned     iSector;
  U32          SectorIndexSrc;
  const char * sType;
  U32          EraseCnt;
  U32          lbi;
  unsigned     NumSectorsBlank;           // Sectors are not used yet.
  unsigned     NumSectorsValid;           // Sectors contain correct data.
  unsigned     NumSectorsInvalid;         // Sectors have been invalidated.
  unsigned     NumSectorsECCError;        // Sectors have incorrect ECC.
  unsigned     NumSectorsECCCorrectable;  // Sectors have correctable ECC error.
  unsigned     NumSectorsErrorInECC;
  unsigned     SectorsPerBlock;
  unsigned     BlockType;
  int          Result;
  unsigned     Type;
  unsigned     brsi;
  int          IsInvalidated;

  if (pBlockInfo == NULL) {
    return FS_ERRCODE_INVALID_PARA;         // Error, invalid parameter.
  }
  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return FS_ERRCODE_OUT_OF_MEMORY;        // Error, could not allocate driver instance.
  }
  //
  // Initialize the NAND flash, so that all necessary information
  // about the organization of the NAND flash are available.
  //
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return FS_ERRCODE_INIT_FAILURE;         // Error, could not initialize NAND flash device.
  }
  //
  // Low level mount the drive if not already done
  //
  (void)_LowLevelMountIfRequired(pInst);
  if (pInst->IsLLMounted == 0u) {
    return FS_ERRCODE_VOLUME_NOT_MOUNTED;   // Error, the NAND flash device is not formatted.
  }
  FS_MEMSET(pBlockInfo, 0, sizeof(FS_NAND_BLOCK_INFO));
  _ClearStaticSpareArea((unsigned)pInst->BytesPerSector >> 5);
  NumSectorsBlank          = 0;
  NumSectorsValid          = 0;
  NumSectorsInvalid        = 0;
  NumSectorsECCError       = 0;
  NumSectorsECCCorrectable = 0;
  NumSectorsErrorInECC     = 0;
  iSector                  = 0;
  SectorsPerBlock          = 1uL << pInst->SPB_Shift;
  SectorIndexSrc  = _BlockIndex2SectorIndex(pInst, PhyBlockIndex);
  Type  = FS_NAND_BLOCK_TYPE_UNKNOWN;
  sType = "Unknown";
  if (_IsBlockBad(pInst, PhyBlockIndex) != 0) {
    EraseCnt = 0;
    lbi      = 0;
    sType    = "Bad block";
    Type     = NAND_BLOCK_TYPE_BAD;
  } else {
    (void)_ReadSpareIntoStaticBuffer(pInst, SectorIndexSrc);
    BlockType = (unsigned)_pSpareAreaData[SPARE_OFF_DATA_STATUS] >> 4;
    EraseCnt  = _LoadEraseCnt(pInst, _pSpareAreaData);
    lbi       = _LoadLBI(pInst);
    switch(BlockType) {
    case DATA_STAT_EMPTY:
      sType = "Empty block";
      Type  = FS_NAND_BLOCK_TYPE_EMPTY;
      break;
    case DATA_STAT_WORK:
      sType = "Work block";
      Type  = FS_NAND_BLOCK_TYPE_WORK;
      break;
    case DATA_STAT_VALID:
      sType = "Data block";
      Type  = FS_NAND_BLOCK_TYPE_DATA;
      break;
    case DATA_STAT_INVALID:
      sType = "Block not in use";
      Type  = FS_NAND_BLOCK_TYPE_EMPTY;
      break;
    default:
      //
      // Unknown block type.
      //
      break;
    }
    //
    // The validity of the first sector in a work block should be checked differently.
    //
    if (BlockType == DATA_STAT_WORK) {
      NAND_WORK_BLOCK * pWorkBlock;

      //
      // Get the descriptor associated with the work block
      //
      pWorkBlock = _FindWorkBlock(pInst, lbi);
      if (pWorkBlock != NULL) {
        //
        // The sector is empty if it has never been written
        //
        if (_WB_IsSectorWritten(pWorkBlock, 0) == 0) {
          NumSectorsBlank++;
        } else {
          //
          // Check if it contains valid data
          //
          brsi = _WB_ReadAssignment(pInst, pWorkBlock, 0);
          IsInvalidated = _IsSectorDataInvalidated(pInst, SectorIndexSrc);
          if ((brsi == 0u) || (IsInvalidated != 0)) {
            NumSectorsInvalid++;
          } else {
            //
            // Now read the sector and verify the ECC
            //
            Result = _ReadSectorWithECC(pInst, _pSectorBuffer, SectorIndexSrc);
            if (Result == RESULT_NO_ERROR) {   // Data is valid
              NumSectorsValid++;
            } else if (Result == RESULT_1BIT_CORRECTED) {
              NumSectorsECCCorrectable++;
            } else if (Result == RESULT_ERROR_IN_ECC) {
              NumSectorsErrorInECC++;
            } else if ((Result == RESULT_UNCORRECTABLE_ERROR) || (Result == RESULT_READ_ERROR)) {
              NumSectorsECCError++;
            } else {
               //
               // Unknown state.
               //
            }
          }
        }
      }
      iSector = 1;
    }
    for (; iSector < SectorsPerBlock; iSector++) {
      //
      // Check sector data against ECC in spare area.
      //
      Result = _ReadSectorWithECC(pInst, _pSectorBuffer, iSector + SectorIndexSrc);
      if (Result == RESULT_NO_ERROR) {
        if (_IsSectorDataInvalidatedFast(pInst, SectorIndexSrc) != 0) {
          NumSectorsInvalid++;
        } else if (iSector != 0u) { // For sectors other than 0 we check the block relative sector index.
          if (_LoadBRSI(pInst) != BRSI_INVALID) {
            NumSectorsValid++;
          } else {
            NumSectorsInvalid++;
          }
        } else {
          NumSectorsValid++;        // On Data sectors we assume the sector is valid.
        }
      } else if (Result == RESULT_1BIT_CORRECTED) {
        NumSectorsECCCorrectable++; // Data have been corrected
      } else if (Result == RESULT_ERROR_IN_ECC) {
        NumSectorsErrorInECC++;     // Error in ECC, data still OK
      } else if ((Result == RESULT_UNCORRECTABLE_ERROR) || (Result == RESULT_READ_ERROR)) {
        NumSectorsECCError++;       // Error not correctable by ECC or NAND read error
      } else if (Result < 0) {
        NumSectorsBlank++;          // Sector not used
      } else {
         //
         // Unknown state.
         //
      }
    }
  }
  pBlockInfo->sType                    = sType;
  pBlockInfo->Type                     = (U8)Type;
  pBlockInfo->EraseCnt                 = EraseCnt;
  pBlockInfo->lbi                      = lbi;
  pBlockInfo->NumSectorsBlank          = (U16)NumSectorsBlank;
  pBlockInfo->NumSectorsECCCorrectable = (U16)NumSectorsECCCorrectable;
  pBlockInfo->NumSectorsErrorInECC     = (U16)NumSectorsErrorInECC;
  pBlockInfo->NumSectorsECCError       = (U16)NumSectorsECCError;
  pBlockInfo->NumSectorsInvalid        = (U16)NumSectorsInvalid;
  pBlockInfo->NumSectorsValid          = (U16)NumSectorsValid;
  return 0;                         // OK, information returned.
}

/*********************************************************************
*
*       FS_NAND_SetOnFatalErrorCallback
*
*  Function description
*    Registers a function to be called by the driver when a fatal error occurs.
*
*  Parameters
*    pfOnFatalError     Pointer to callback function.
*
*  Additional information
*    This function is optional. If no callback function is registered
*    the SLC1 NAND driver behaves as if the callback function returned 1.
*    This means that the NAND flash remains writable after the occurrence
*    of the fatal error. emFile versions previous to 4.04b behave differently
*    and mark the NAND flash as read-only in this case. For additional
*    information refer to FS_NAND_ON_FATAL_ERROR_CALLBACK.
*
*    Typically, the SLC1 NAND driver reports a fatal error when an
*    uncorrectable bit error occurs, that is when the ECC is not able
*    to correct the bit errors. A fatal error can also be reported
*    on other events such the failure to erase a NAND block. The type of
*    error is indicated to the callback function via the ErrorType member
*    of the FS_NAND_FATAL_ERROR_INFO structure.
*
*    All instances of the SLC1 NAND driver share the same callback function.
*    The Unit member of the FS_NAND_FATAL_ERROR_INFO structure passed
*    as parameter to the pfOnFatalError callback function can be used to
*    determine which driver instance triggered the fatal error.
*/
void FS_NAND_SetOnFatalErrorCallback(FS_NAND_ON_FATAL_ERROR_CALLBACK * pfOnFatalError) {
  _pfOnFatalError = pfOnFatalError;
}

/*********************************************************************
*
*       FS_NAND_TestBlock
*
*  Function description
*    Fills all the pages in a block (including the spare area) with the
*    specified pattern and verifies if the data was written correctly.
*
*  Parameters
*    Unit         Index of the driver instance (0-based).
*    BlockIndex   Index of the NAND block to be tested.
*    Pattern      Data pattern to be written during the test.
*    pInfo        [OUT] Optional information about the test result.
*
*  Return value
*    ==FS_NAND_TEST_RETVAL_OK                - OK, no bit errors.
*    ==FS_NAND_TEST_RETVAL_CORRECTABLE_ERROR - OK, correctable bit errors found. The number of bit errors is returned in NumErrorsCorrectable of pResult.
*    ==FS_NAND_TEST_RETVAL_FATAL_ERROR       - Fatal error, uncorrectable bit error found. The page index is returned in PageIndexFatalError of pResult.
*    ==FS_NAND_TEST_RETVAL_BAD_BLOCK         - Bad block, skipped.
*    ==FS_NAND_TEST_RETVAL_ERASE_FAILURE     - Erase operation failed. The block has been marked as defective.
*    ==FS_NAND_TEST_RETVAL_WRITE_FAILURE     - Write operation failed. The block has been marked as defective.
*    ==FS_NAND_TEST_RETVAL_READ_FAILURE      - Read operation failed.
*    ==FS_NAND_TEST_RETVAL_INTERNAL_ERROR    - NAND flash access error.
*
*  Additional information
*    This function is optional. It can be used by the application to
*    test the data reliability of a NAND block. BlockIndex is relative
*    to the beginning of the NAND partition where the first block has
*    the index 0.
*/
int FS_NAND_TestBlock(U8 Unit, unsigned BlockIndex, U32 Pattern, FS_NAND_TEST_INFO * pInfo) {
  int         r;
  U32         Data32;
  unsigned    NumLoops;
  unsigned    BitErrorCnt;
  unsigned    BitErrorCntPage;
  unsigned    NumBits;
  U32       * pData32;
  unsigned    PageIndex;
  unsigned    PageIndex0;
  U32         SectorIndex;
  unsigned    NumSectors;
  unsigned    BytesPerSector;
  unsigned    BytesPerSpare;
  unsigned    NumReadRetries;
  unsigned    SPB_Shift;
  unsigned    PPB_Shift;
  NAND_INST * pInst;

  BitErrorCnt = 0;
  PageIndex   = 0;
  //
  // Allocate memory if necessary
  //
  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return FS_NAND_TEST_RETVAL_INTERNAL_ERROR;            // Error, could not allocate instance.
  }
  //
  // Initialize the NAND flash, so that all necessary information
  // about the NAND flash are available.
  //
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return FS_NAND_TEST_RETVAL_INTERNAL_ERROR;            // Error, could not initialize device.
  }
  //
  // Validate the block index.
  //
  if (BlockIndex > pInst->NumPhyBlocks) {
    return FS_NAND_TEST_RETVAL_INTERNAL_ERROR;            // Error, invalid block index.
  }
  //
  // Erase only blocks that are not defective.
  //
  if (_IsBlockErasable(pInst, BlockIndex) == 0) {
    r = FS_NAND_TEST_RETVAL_BAD_BLOCK;                    // OK, the block is marked as defective.
    goto Done;
  }
  //
  // Erase the block.
  //
  r = _EraseBlock(pInst, BlockIndex);
  if (r != 0) {
    _MarkBlockAsBad(pInst, BlockIndex, RESULT_ERASE_ERROR, 0);
    r = FS_NAND_TEST_RETVAL_ERASE_FAILURE;                // OK, erase failed and the block has been marked as defective.
    goto Done;
  }
  //
  // Fill local variables.
  //
  SPB_Shift      = pInst->SPB_Shift;
  PPB_Shift      = pInst->PPB_Shift;
  BytesPerSector = pInst->BytesPerSector;
  BytesPerSpare  = BytesPerSector >> 5;                   // The spare area is 1/32 of a sector
  BitErrorCnt    = 0;
  NumSectors     = 1uL << SPB_Shift;
  PageIndex0     = BlockIndex << PPB_Shift;
  PageIndex      = PageIndex0;
  SectorIndex    = _BlockIndex2SectorIndex(pInst, BlockIndex);
  //
  // Fill the internal buffers with the pattern.
  //
  NumLoops = BytesPerSector >> 2;                         // The pattern in 4 bytes large.
  pData32  = _pSectorBuffer;
  do {
    *pData32++ = Pattern;
  } while (--NumLoops != 0u);
  NumLoops = BytesPerSpare >> 2;                          // The pattern in 4 bytes large.
  pData32  = SEGGER_PTR2PTR(U32, _pSpareAreaData);
  do {
    *pData32++ = Pattern;
  } while (--NumLoops != 0u);
  //
  // For each page in the block:
  //   - fill with the pattern
  //   - read back and verify the pattern
  //
  //
  // Fill the page and the spare area.
  //
  do {
    //
    // Write the page and the spare area.
    //
    r = _WriteDataSpare(pInst, SectorIndex, _pSectorBuffer, 0, BytesPerSector, _pSpareAreaData, 0, BytesPerSpare);
    if (r != 0) {
      PageIndex = SectorIndex >> (SPB_Shift - PPB_Shift);
      (void)_EraseBlock(pInst, BlockIndex);
      _MarkBlockAsBad(pInst, BlockIndex, RESULT_UNCORRECTABLE_ERROR, PageIndex - PageIndex0);
      r = FS_NAND_TEST_RETVAL_WRITE_FAILURE;
      goto Done;
    }
    ++SectorIndex;
  } while (--NumSectors != 0u);
  //
  // Read back and verify the written data.
  //
  SectorIndex     = _BlockIndex2SectorIndex(pInst, BlockIndex);
  NumSectors      = 1uL << SPB_Shift;
  NumReadRetries  = FS_NAND_NUM_READ_RETRIES;
  BitErrorCntPage = 0;
  for (;;) {
    //
    // Read the page and the spare area.
    //
    for (;;) {
      r = _ReadDataSpare(pInst, SectorIndex, _pSectorBuffer, 0, BytesPerSector, _pSpareAreaData, 0, BytesPerSpare);
      if (r == 0) {
        break;
      }
      if (NumReadRetries != 0u) {
        --NumReadRetries;
        continue;
      }
      PageIndex = SectorIndex >> (SPB_Shift - PPB_Shift);
      r = FS_NAND_TEST_RETVAL_READ_FAILURE;
      goto Done;
    }
    //
    // Verify the page data.
    //
    NumLoops = BytesPerSector >> 2;                       // The pattern in 4 bytes large.
    pData32  = _pSectorBuffer;
    NumBits  = 0;
    do {
      Data32 = *pData32++ ^ Pattern;
      NumBits += _Count1Bits(Data32);
      --NumLoops;
      //
      // Check for the number of error bits at ECC block boundary.
      //
      if ((NumLoops & ((BYTES_PER_ECC_BLOCK >> 2) - 1u)) == 0u) {
        if (NumBits > 1u) {                               // The ECC can correct only 1 bit in an ECC block.
          if (NumReadRetries != 0u) {
            --NumReadRetries;
            goto Retry;                                   // This could be a transient error. Read again the data.
          }
          PageIndex  = SectorIndex >> (SPB_Shift - PPB_Shift);
          (void)_EraseBlock(pInst, BlockIndex);
          _MarkBlockAsBad(pInst, BlockIndex, RESULT_UNCORRECTABLE_ERROR, PageIndex - PageIndex0);
          BitErrorCnt += BitErrorCntPage;
          r = FS_NAND_TEST_RETVAL_FATAL_ERROR;            // Uncorrectable bit error detected.
          goto Done;
        }
        BitErrorCntPage += NumBits;
        NumBits          = 0;
      }
    } while (NumLoops != 0u);
    //
    // Verify the data of the spare area.
    //
    NumLoops = BytesPerSpare >> 2;                        // The pattern in 4 bytes large.
    pData32  = SEGGER_PTR2PTR(U32, _pSpareAreaData);
    do {
      Data32 = *pData32++ ^ Pattern;
      //
      // The data in the spare are is not protected by ECC.
      // We report each bit error in this area as fatal error.
      //
      NumBits = _Count1Bits(Data32);
      if (NumBits != 0u) {
        if (NumReadRetries != 0u) {
          --NumReadRetries;
          goto Retry;                                     // This could be a transient error. Read again the data.
        }
        PageIndex = SectorIndex >> (SPB_Shift - PPB_Shift);
        (void)_EraseBlock(pInst, BlockIndex);
        _MarkBlockAsBad(pInst, BlockIndex, RESULT_UNCORRECTABLE_ERROR, PageIndex - PageIndex0);
        BitErrorCnt += BitErrorCntPage;
        r = FS_NAND_TEST_RETVAL_FATAL_ERROR;              // Uncorrectable bit error detected.
        goto Done;
      }
    } while (--NumLoops != 0u);
    BitErrorCnt += BitErrorCntPage;
    if (--NumSectors == 0u) {
      break;
    }
    ++SectorIndex;
    NumReadRetries  = FS_NAND_NUM_READ_RETRIES;
Retry:
    BitErrorCntPage = 0;
  }
  if (BitErrorCnt != 0u) {
    r = FS_NAND_TEST_RETVAL_CORRECTABLE_ERROR;
  }
Done:
  //
  // Leave the contents of the block in a known state.
  //
  if ((r != FS_NAND_TEST_RETVAL_BAD_BLOCK    ) &&
      (r != FS_NAND_TEST_RETVAL_ERASE_FAILURE)) {
    (void)_EraseBlock(pInst, BlockIndex);
  }
  //
  // Return additional information.
  //
  if (pInfo != NULL) {
    pInfo->BitErrorCnt = BitErrorCnt;
    pInfo->PageIndex   = PageIndex;
  }
  return r;
}

#if FS_NAND_VERIFY_ERASE

/*********************************************************************
*
*       FS_NAND_SetEraseVerification
*
*  Function description
*    Enables or disables the checking of the block erase operation.
*
*  Parameters
*    Unit           Index of the driver instance (0-based).
*    OnOff          Specifies if the feature has to be enabled or disabled
*                   * ==0   The erase operation is not checked.
*                   * !=0   The erase operation is checked.
*
*  Additional information
*    This function is optional. The result of a block erase operation
*    is normally checked by evaluating the error bits maintained by the
*    NAND flash device in a internal status register. FS_NAND_SetEraseVerification()
*    can be used to enable additional verification of the block erase
*    operation that is realized by reading back the contents of the entire
*    erased physical block and by checking that all the bytes in it are
*    set to 0xFF. Enabling this feature can negatively impact the write
*    performance of SLC1 NAND driver.
*
*    The block erase verification feature is active only when the SLC1
*    NAND driver is compiled with the FS_NAND_VERIFY_ERASE configuration
*    define is set to 1 (default is 0) or when the FS_DEBUG_LEVEL
*    configuration define is set to a value greater than or equal to
*    FS_DEBUG_LEVEL_CHECK_ALL.
*/
void FS_NAND_SetEraseVerification(U8 Unit, U8 OnOff) {
  NAND_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->VerifyErase = OnOff;
  }
}

#endif // FS_NAND_VERIFY_ERASE

#if FS_NAND_VERIFY_WRITE

/*********************************************************************
*
*       FS_NAND_SetWriteVerification
*
*  Function description
*    Enables or disables the checking of each page write operation.
*
*  Parameters
*    Unit         Index of the driver instance (0-based).
*    OnOff        Specifies if the feature has to be enabled or disabled
*                 * ==0   The write operation is not checked.
*                 * !=0   The write operation is checked.
*
*  Additional information
*    This function is optional. The result of a page write operation
*    is normally checked by evaluating the error bits maintained by the
*    NAND flash device in a internal status register. FS_NAND_SetWriteVerification()
*    can be used to enable additional verification of the page write
*    operation that is realized by reading back the contents of the written
*    page and by checking that all the bytes are matching the data
*    requested to be written. Enabling this feature can negatively
*    impact the write performance of SLC1 NAND driver.
*
*    The page write verification feature is active only when the SLC1
*    NAND driver is compiled with the FS_NAND_VERIFY_WRITE configuration
*    define is set to 1 (default is 0) or when the FS_DEBUG_LEVEL
*    configuration define is set to a value greater than or equal to
*    FS_DEBUG_LEVEL_CHECK_ALL.
*/
void FS_NAND_SetWriteVerification(U8 Unit, U8 OnOff) {
  NAND_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->VerifyWrite = OnOff;
  }
}

#endif // FS_NAND_VERIFY_WRITE

/*********************************************************************
*
*       FS_NAND_IsBlockBad
*
*  Function description
*    Checks if a NAND block is marked as defective.
*
*  Parameters
*    Unit           Index of the driver instance (0-based).
*    BlockIndex     Index of the NAND flash block to be checked.
*
*  Return value
*    ==1    Block is defective
*    ==0    Block is not defective
*
*  Additional information
*    This function is optional.
*/
int FS_NAND_IsBlockBad(U8 Unit, unsigned BlockIndex) {
  int         r;
  NAND_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;             // Error, could not allocate driver instance.
  }
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return 1;             // Error, could not initialize NAND flash device.
  }
  r = _IsBlockErasable(pInst, BlockIndex);
  return r;
}

/*********************************************************************
*
*       FS_NAND_EraseBlock
*
*  Function description
*    Sets all the bytes in a NAND block to 0xFF.
*
*  Parameters
*    Unit           Index of the driver instance (0-based).
*    BlockIndex     Index of the NAND flash block to be erased.
*
*  Return value
*    ==0 - OK, block erased.
*    !=0 - An error occurred.
*
*  Additional information
*    This function is optional. FS_NAND_EraseBlock() function does
*    not check if the block is marked as defective before erasing it.
*/
int FS_NAND_EraseBlock(U8 Unit, unsigned BlockIndex) {
  int         r;
  NAND_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;             // Error, could not allocate driver instance.
  }
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return 1;             // Error, could not initialize NAND flash device.
  }
  r = _EraseBlock(pInst, BlockIndex);
  return r;
}

/*********************************************************************
*
*       FS_NAND_WritePage
*
*  Function description
*    Stores data to a page of a NAND flash with ECC.
*
*  Parameters
*    Unit         Index of the driver instance (0-based).
*    PageIndex    Index of the page to be written.
*    pData        [IN] Data to be written.
*    NumBytes     Number of bytes to be written.
*
*  Return value
*    ==0 - OK, data written.
*    !=0 - An error occurred.
*
*  Additional information
*    This function is optional.
*
*    The data is written beginning with the byte offset 0 in the page.
*    If more data is written than the size of the page, typically 2 KB + 64 bytes,
*    the excess bytes are discarded. Data in the area reserved for ECC cannot
*    be written using this function and it will be overwritten.
*/
int FS_NAND_WritePage(U8 Unit, U32 PageIndex, const void * pData, unsigned NumBytes) {
  int         r;
  NAND_INST * pInst;
  unsigned    BytesPerPage;
  unsigned    BytesPerSector;
  unsigned    NumBytesAtOnce;
  U32         SectorIndex;
  U32         NumPages;
  unsigned    PPB_Shift;    // Pages per block
  unsigned    SPB_Shift;    // Sectors per block
  unsigned    SPP_Shift;    // Sectors per page
  const U8  * pData8;

  if (NumBytes == 0u) {
    return 0;         // OK, nothing to do.
  }
  if (pData == NULL) {
    return 1;         // Error, invalid parameter.
  }
  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;         // Error, could not get driver instance.
  }
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return 1;         // Error, could not initialize device.
  }
  BytesPerPage   = pInst->BytesPerPage;
  BytesPerSector = pInst->BytesPerSector;
  PPB_Shift      = pInst->PPB_Shift;
  SPB_Shift      = pInst->SPB_Shift;
  SPP_Shift      = SPB_Shift - PPB_Shift;
  NumPages       = pInst->NumPhyBlocks << PPB_Shift;
  NumBytes       = SEGGER_MIN(NumBytes, BytesPerPage);
  if (PageIndex >= NumPages) {
    return 1;         // Error, invalid page index.
  }
  SectorIndex = PageIndex << SPP_Shift;
  pData8      = SEGGER_CONSTPTR2PTR(const U8, pData);
  do {
    //
    // Copy the data to internal sector buffer and write it from there.
    // There are 2 reasons why we are doing this:
    // 1) The _WriteSector() function requires a 32-bit aligned buffer.
    //    This is necessary so that the SW ECC routine (if used) can process 32-bits at a time.
    // 2) The _WriteSector() function can write only whole sectors.
    //
    FS_MEMSET(_pSectorBuffer, 0xFF, BytesPerSector);
    NumBytesAtOnce = SEGGER_MIN(NumBytes, BytesPerSector);
    FS_MEMCPY(_pSectorBuffer, pData8, NumBytesAtOnce);
    r = _WriteSector(pInst, _pSectorBuffer, SectorIndex);
    if (r != 0) {
      break;          // Error, could not write to page.
    }
    ++SectorIndex;
    NumBytes -= NumBytesAtOnce;
    pData8   += NumBytesAtOnce;
  } while (NumBytes != 0u);
  return r;
}

/*********************************************************************
*
*       FS_NAND_WritePageRaw
*
*  Function description
*    Stores data to a page of a NAND flash without ECC.
*
*  Parameters
*    Unit         Index of the driver instance (0-based).
*    PageIndex    Index of the page to be written.
*    pData        [IN] Data to be written.
*    NumBytes     Number of bytes to be written.
*
*  Return value
*    ==0 - OK, data written.
*    !=0 - An error occurred.
*
*  Additional information
*    This function is optional.
*
*    The data is written beginning at the byte offset 0 in the page.
*    If more data is written than the size of the page + spare area,
*    typically 2 Kbytes + 64 bytes, the excess bytes are ignored.
*/
int FS_NAND_WritePageRaw(U8 Unit, U32 PageIndex, const void * pData, unsigned NumBytes) {
  int         r;
  NAND_INST * pInst;
  unsigned    BytesPerPage;
  unsigned    BytesPerSector;
  unsigned    NumBytesAtOnce;
  U32         NumPages;
  unsigned    PPB_Shift;    // Pages per block
  unsigned    SPB_Shift;    // Sectors per page
  unsigned    SPP_Shift;    // Sectors per page
  U32         SectorIndex;
  const U8  * pData8;

  if (NumBytes == 0u) {
    return 0;         // OK, nothing to do.
  }
  if (pData == NULL) {
    return 1;         // Error, invalid parameter.
  }
  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;         // Error, could not get driver instance.
  }
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return 1;         // Error, could not initialize device.
  }
  BytesPerPage   = pInst->BytesPerPage;
  BytesPerSector = pInst->BytesPerSector;
  PPB_Shift      = pInst->PPB_Shift;
  SPB_Shift      = pInst->SPB_Shift;
  SPP_Shift      = SPB_Shift - PPB_Shift;;
  NumPages       = pInst->NumPhyBlocks << PPB_Shift;
  NumBytes       = SEGGER_MIN(NumBytes, BytesPerPage);
  if (PageIndex >= NumPages) {
    return 1;         // Error, invalid page index.
  }
  SectorIndex = PageIndex << SPP_Shift;
  pData8      = SEGGER_CONSTPTR2PTR(const U8, pData);
  do {
    NumBytesAtOnce = SEGGER_MIN(NumBytes, BytesPerSector);
    r = _WriteDataSpare(pInst, SectorIndex, pData8, 0, NumBytes, NULL, 0, 0);
    if (r != 0) {
      break;
    }
    ++SectorIndex;
    NumBytes -= NumBytesAtOnce;
    pData8   += NumBytesAtOnce;
  } while (NumBytes != 0u);
  return r;
}

/*********************************************************************
*
*       FS_NAND_ReadPageRaw
*
*  Function description
*    Reads data from a page without ECC.
*
*  Parameters
*    Unit         Index of the driver instance (0-based).
*    PageIndex    Index of the page to be read.
*    pData        [OUT] Data to be written.
*    NumBytes     Number of bytes to be read.
*
*  Return value
*    ==0 - OK, data read.
*    !=0 - An error occurred.
*
*  Additional information
*    This function is optional.
*
*    The data is read beginning from byte offset 0 in the page.
*    If more data is requested than the page + spare area size,
*    typ. 2 Kbytes + 64 bytes, the function does not modify the
*    remaining bytes in pData.
*/
int FS_NAND_ReadPageRaw(U8 Unit, U32 PageIndex, void * pData, unsigned NumBytes) {
  int         r;
  NAND_INST * pInst;
  unsigned    BytesPerPage;
  U32         SectorIndex;
  U32         NumPages;
  unsigned    PPB_Shift;    // Pages per block
  unsigned    SPB_Shift;    // Sectors per block
  unsigned    SPP_Shift;    // Sectors per page

  if (NumBytes == 0u) {
    return 0;         // OK, nothing to do.
  }
  if (pData == NULL) {
    return 1;         // Error, invalid parameter.
  }
  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;         // Error, could not allocate driver instance.
  }
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return 1;         // Error, could not initialize NAND flash device.
  }
  BytesPerPage = pInst->BytesPerPage;
  PPB_Shift    = pInst->PPB_Shift;
  SPB_Shift    = pInst->SPB_Shift;
  SPP_Shift    = SPB_Shift - PPB_Shift;;
  NumPages     = pInst->NumPhyBlocks << PPB_Shift;
  NumBytes     = SEGGER_MIN(NumBytes, BytesPerPage);
  if (PageIndex >= NumPages) {
    return 1;         // Error, invalid page index.
  }
  SectorIndex = PageIndex << SPP_Shift;
  r = _ReadDataSpare(pInst, SectorIndex, pData, 0, NumBytes, NULL, 0, 0);
  return r;
}

/*************************** End of file ****************************/
