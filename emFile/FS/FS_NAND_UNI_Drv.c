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
File        : FS_NAND_UNI_Drv.c
Purpose     : File system generic NAND driver for SLC and MLC NAND flashes.
              For more information on supported devices, refer to the user manual.
Literature  : [1] Samsung data sheet for K9XXG08UXM devices, specifically K9F2G08U0M, K9K4G08U1M. Similar information in other Samsung manuals.
                  \\fileserver\techinfo\Company\Samsung\NAND_Flash\Device\K9F2GxxU0M_2KPageSLC_R12.pdf
              [2] Micron data sheet for MT29F2G08AAD, MT29F2G16AAD, MT29F2G08ABD, MT29F2G16ABD devices
                  \\fileserver\techinfo\Company\Micron\NANDFlash\MT29F2G0_8AAD_16AAD_08ABD_16ABD.pdf
              [3] Micron presentations for Flash Memory Summit.
                  \\fileserver\techinfo\Company\Micron\NANDFlash\flash_mem_summit_08_khurram_nand.pdf
              [4] Micron application notes about bad block management, wear leveling and error correction codes.
              [5] \\fileserver\Techinfo\Company\Samsung\NAND_Flash\ecc_algorithm_for_web_512b.pdf
-------------------------- END-OF-HEADER -----------------------------

General info on the inner workings of this high level flash driver:

Supported NAND flashes
======================
  All the NAND flashes with a pages size >= 2KB with a spare area >= 16 byte/512 byte data.

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
  TBD

Spare area usage
================
  The spare area stores the management information of the driver and the ECC of user data
  and is subdivided into several equal sized regions. Each region corresponds to 512 bytes of user data.
  Example for a NAND flash with a page size of 2048 bytes and a spare area of 64 bytes each region
  is 64 / (2048 / 512) = 16 bytes large. The ECC protects 512 bytes of user data and 4 bytes (bytes 4 to 7)
  of spare area and is stored at the end of the region starting from offset 8. At least 8 bytes are reserved for the ECC.
  The data layout of the spare area looks like this:

  Byte
  offset Region   Description
  ---------------------------
  0-1    0        Bad block marker. Present only in the first page of a block. Not protected by ECC.
  2-3    0        Reserved
  4-7    0        Erase count. Present only in the spare area of the first and of the second page in a block. Protected by ECC0.
                    Byte 4 - EraseCnt b[31..24]
                    Byte 5 - EraseCnt b[23..16]
                    Byte 6 - EraseCnt b[15..8]
                    Byte 7 - EraseCnt b[7..0]
  8-15   0        ECC0. Protects the user bytes 0x000-0x1FF and the spare bytes 4-7 of region 0.
  0-3    1        Reserved
  4-5    1        Logical block index. Present only in the spare area of the second page in a block. Protected by ECC1.
                    Byte 4 - LBI  b[15:8]
                    Byte 5 - LBI  b[7:0]
  6      1        Data type and count. Present only in the spare area of the second page in a block. Protected by ECC1.
                    Bit 3-0 - BlockCnt:  Increments every time a block is copied.
                    Bit 7-4 - BlockType: 0xF: Unused (Empty)
                                         0xE: Work
                                         0xC: Data
  7      1        Sector status. Protected by ECC1.
                    Bit 3-0 - SectorStat: 0x0: Sector data valid
                                          0xF: Sector data invalid.
                                          Present in the spare area of all pages in a data block which store sector data.
                  Work block count. Protected by ECC1.
                    Bit 4-7 - MergeCnt:  Increments every time a data block and a work block are merged.
                                         Present in the spare area of the second page in a block.
  8-15   1        ECC1. Protects the user bytes 0x200-0x3FF and the spare bytes 4-7 of region 1.
  0-3    2        Reserved
  4-5    2        Block relative sector index of the data stored in the page. Valid only for work blocks. Protected by ECC2.
                    Byte 4 - BRSI Bit 15-8
                    Byte 5 - BRSI Bit 7-0
  6-7    2        Number of sectors stored in the work block. Valid only for work blocks. Protected by ECC2.
                    Byte 6 - NumSectors Bit 15-8
                    Byte 7 - NumSectors Bit 7-0
  8-15   2        ECC2. Protects the user bytes 0x400-0x5FF and the spare bytes 4-7 of region 2.
  0-3    3        Reserved
  4-7    3        Optional 32-bit CRC of data area. Protected by ECC3.
                    Byte 4 - DataCRC b[31..24]
                    Byte 5 - DataCRC b[23..16]
                    Byte 6 - DataCRC b[15..8]
                    Byte 7 - DataCRC b[7..0]
  8-15   3        ECC3. Protects the user bytes 0x600-0x7FF and the spare bytes 4-7 of region 3.
  ...    ...      ...
  ...    ...      ...
  ...    ...      ...

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

 LBI: Logical block index         : Gives the data position of a block.
BRSI: Block relative sector index : Index of sector relative to start of block, 0..255

----------------------------------------------------------------------
Potential improvements

  Reliability
    - Count bit errors in blocks.
----------------------------------------------------------------------
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
*       Defines, configurable
*
**********************************************************************
*/
#ifndef   FS_NAND_SUPPORT_DATA_CRC
  #define FS_NAND_SUPPORT_DATA_CRC            0       // If set to 1 an additional 32-bit CRC of the user data is stored in the spare area of each
                                                      // page to help in detecting bit errors that the ECC is not able to correct. This feature is experimental.
#endif

/*********************************************************************
*
*       Defines, fixed
*
**********************************************************************
*/
#define LLFORMAT_VERSION                  40001

/*********************************************************************
*
*       IF_STATS
*
**********************************************************************
*/
#if FS_NAND_ENABLE_STATS
  #define IF_STATS(Exp)                   Exp
#else
  #define IF_STATS(Exp)
#endif // FS_NAND_ENABLE_STATS

/*********************************************************************
*
*       IF_STATS_SECTOR_STATUS
*
**********************************************************************
*/
#if (FS_NAND_ENABLE_STATS != 0) && (FS_NAND_ENABLE_STATS_SECTOR_STATUS != 0)
  #define IF_STATS_SECTOR_STATUS(Exp)     Exp
#else
  #define IF_STATS_SECTOR_STATUS(Exp)
#endif // FS_NAND_ENABLE_STATS != 0 && FS_NAND_ENABLE_STATS_SECTOR_STATUS != 0

/*********************************************************************
*
*       Spare area usage
*
*  For details, see the explanation in header.
*/
#define SPARE_OFF_BLOCK_STAT              0x00u
#define SPARE_OFF_ERASE_CNT               0x04u
#define SPARE_OFF_LBI                     0x04u
#define SPARE_OFF_BLOCK_TYPE_CNT          0x06u
#define SPARE_OFF_SECTOR_STAT_MERGE_CNT   0x07u
#define SPARE_OFF_BRSI                    0x04u
#define SPARE_OFF_NUM_SECTORS             0x06u
#if FS_NAND_SUPPORT_DATA_CRC
  #define SPARE_OFF_DATA_CRC              0x04u
#endif

/*********************************************************************
*
*       Special values for "INVALID"
*/
#define ERASE_CNT_INVALID                 0xFFFFFFFFuL    // Invalid erase count
#define BRSI_INVALID                      0xFFFFu         // Invalid relative sector index
#define LBI_INVALID                       0xFFFFu         // Invalid logical block index
#if FS_NAND_SUPPORT_BLOCK_GROUPING
  #define NUM_SECTORS_INVALID             0xFFFFu
#endif

/*********************************************************************
*
*       Block data type nibble
*/
#define BLOCK_TYPE_EMPTY                  0xFu            // Block is empty
#define BLOCK_TYPE_WORK                   0xEu            // Block is used as "work block"
#define BLOCK_TYPE_DATA                   0xCu            // Block contains valid data

/*********************************************************************
*
*       Block status marker
*/
#define BLOCK_STAT_BAD                    0x00u
#define BLOCK_STAT_GOOD                   0xFFu

/*********************************************************************
*
*       Sector data status
*/
#define SECTOR_STAT_WRITTEN               0x0u
#define SECTOR_STAT_EMPTY                 0xFu

/*********************************************************************
*
*       Status of NAND flash operations
*/
#define RESULT_NO_ERROR                   0   // Everything OK
#define RESULT_BIT_ERRORS_CORRECTED       1   // One or more bit errors detected and corrected
#define RESULT_BIT_ERROR_IN_ECC           2   // Error in the ECC itself detected and corrected, data is OK
#define RESULT_UNCORRECTABLE_BIT_ERRORS   3   // Bit errors detected but not corrected, not recoverable
#define RESULT_READ_ERROR                 4   // Error while reading from NAND flash, not recoverable
#define RESULT_WRITE_ERROR                5   // Error while writing to NAND flash, recoverable
#define RESULT_OUT_OF_FREE_BLOCKS         6   // Tried to allocate a free block but no more were found
#define RESULT_ERASE_ERROR                7   // Error while erasing a NAND flash block, recoverable
#define RESULT_DATA_RECOVERED             8   // Uncorrectable bit error detected but data was recovered from mirror
#if FS_NAND_VERIFY_WRITE
  #define RESULT_VERIFY_ERROR             9   // Data verification failed
#endif

/*********************************************************************
*
*       Sector and block indexes with special meaning
*/
#define SECTOR_INDEX_FORMAT_INFO          0u
#define SECTOR_INDEX_ERROR_INFO           1u
#define BRSI_BLOCK_INFO                   1u    // Page position in a block where the block related information are stored. This is also the first page that stores data.
#define PBI_STORAGE_START                 1u    // Index of the first block used for data storage

/*********************************************************************
*
*       Number of work blocks
*/
#if FS_SUPPORT_JOURNAL
  #define NUM_WORK_BLOCKS_MIN             4     // For performance reasons we need more work blocks when Journaling is enabled
#else
  #define NUM_WORK_BLOCKS_MIN             3
#endif
#define MAX_NUM_WORK_BLOCKS               10    // Maximum number of work blocks the driver allocates by default

/*********************************************************************
*
*       Misc. defines
*/
#define MAX_PCT_OF_BLOCKS_RESERVED        25u   // Maximum number of blocks to reserve in percents.
#define NUM_BLOCKS_RESERVED               2     // Number of NAND blocks the driver reserves for internal use 1 for the low-level format information and one for the copy operation.
#define MIN_BYTES_PER_PAGE                2048u // This is the minimum page size required by the driver.
#define LD_BYTES_PER_ECC_BLOCK            9     // Default number of bytes protected by a single ECC specified as power of 2.
#define NUM_BYTES_BAD_BLOCK_SIGNATURE     4     // This is the number of bytes that are not protected by ECC in a stripe of the spare area.
#if FS_NAND_SUPPORT_DATA_CRC
  #define DATA_CRC_INIT                   0xFFFFFFFFuL
#endif

/*********************************************************************
*
*       Location of format information
*/
#define INFO_OFF_LLFORMAT_VERSION         0x10
#define INFO_OFF_NUM_LOG_BLOCKS           0x20
#define INFO_OFF_NUM_WORK_BLOCKS          0x30
#define INFO_OFF_NUM_BLOCKS               0x40
#define INFO_OFF_NUM_PAGES_PER_BLOCK      0x50

/*********************************************************************
*
*       Location of management data in the spare area
*/
#define OFF_SPARE_RANGE                   4u
#if FS_NAND_OPTIMIZE_SPARE_AREA_READ
  #define SPARE_RANGE_ERASE_CNT           (1u << 0)
  #define SPARE_RANGE_LBI                 (1u << 1)
  #define SPARE_RANGE_BRSI                (1u << 2)
  #define MAX_NUM_SPARE_RANGES            4u
  #define NUM_BYTES_SPARE_RANGE           4
#endif // FS_NAND_OPTIMIZE_SPARE_AREA_READ

/*********************************************************************
*
*       Location of bad block information in the spare area
*/
#define SPARE_STRIPE_INDEX_SIGNATURE_ALT  0u      // Position of the signature in the second and the following blocks of a block group.
#define SPARE_STRIPE_INDEX_SIGNATURE      1u
#define SPARE_STRIPE_INDEX_ERROR_TYPE     2u
#define SPARE_STRIPE_INDEX_ERROR_BRSI     3u

/*********************************************************************
*
*       The second sector of the first block in a NAND flash stores
*       the fatal error information
*/
#define INFO_OFF_IS_WRITE_PROTECTED       0x00    // Inverted. 0xFFFF -> ~0xFFFF = 0 means normal (not write protected)
#define INFO_OFF_HAS_FATAL_ERROR          0x02    // Inverted. 0xFFFF -> ~0xFFFF = 0 means normal (no error)
#define INFO_OFF_FATAL_ERROR_TYPE         0x04    // Type of fatal error
#define INFO_OFF_FATAL_ERROR_SECTOR_INDEX 0x08    // Index of the sector where the error occurred

/*********************************************************************
*
*       Working status of the active wear leveling operation
*/
#define ACTIVE_WL_ENABLED                 0u
#define ACTIVE_WL_DISABLED_TEMP           1u
#define ACTIVE_WL_DISABLED_PERM           2u

/*********************************************************************
*
*       Invokes the test hook function if the support for testing is enabled.
*/
#if FS_SUPPORT_TEST
  #define CALL_TEST_HOOK_FAIL_SAFE(Unit)  _CallTestHookFailSafe(Unit)
#else
  #define CALL_TEST_HOOK_FAIL_SAFE(Unit)
#endif

/*********************************************************************
*
*       FS_NOR_TEST_HOOK_DATA_READ_BEGIN
*/
#if FS_SUPPORT_TEST
  #define CALL_TEST_HOOK_DATA_READ_BEGIN(Unit, PageIndex, pData, pOff, pNumBytes) _CallTestHookDataReadBegin(Unit, PageIndex, pData, pOff, pNumBytes)
#else
  #define CALL_TEST_HOOK_DATA_READ_BEGIN(Unit, PageIndex, pData, pOff, pNumBytes)
#endif

/*********************************************************************
*
*       FS_NOR_TEST_HOOK_DATA_READ_END
*/
#if FS_SUPPORT_TEST
  #define CALL_TEST_HOOK_DATA_READ_END(Unit, PageIndex, pData, Off, NumBytes, pResult) _CallTestHookDataReadEnd(Unit, PageIndex, pData, Off, NumBytes, pResult)
#else
  #define CALL_TEST_HOOK_DATA_READ_END(Unit, PageIndex, pData, Off, NumBytes, pResult)
#endif

/*********************************************************************
*
*       FS_NOR_TEST_HOOK_DATA_READ_EX_BEGIN
*/
#if FS_SUPPORT_TEST
  #define CALL_TEST_HOOK_DATA_READ_EX_BEGIN(Unit, PageIndex, pData, pOff, pNumBytes, pSpare, pOffSpare, pNumBytesSpare) _CallTestHookDataReadExBegin(Unit, PageIndex, pData, pOff, pNumBytes, pSpare, pOffSpare, pNumBytesSpare)
#else
  #define CALL_TEST_HOOK_DATA_READ_EX_BEGIN(Unit, PageIndex, pData, pOff, pNumBytes, pSpare, pOffSpare, pNumBytesSpare)
#endif

/*********************************************************************
*
*       FS_NOR_TEST_HOOK_DATA_READ_EX_END
*/
#if FS_SUPPORT_TEST
  #define CALL_TEST_HOOK_DATA_READ_EX_END(Unit, PageIndex, pData, Off, NumBytes, pSpare, OffSpare, NumBytesSpare, pResult) _CallTestHookDataReadExEnd(Unit, PageIndex, pData, Off, NumBytes, pSpare, OffSpare, NumBytesSpare, pResult)
#else
  #define CALL_TEST_HOOK_DATA_READ_EX_END(Unit, PageIndex, pData, Off, NumBytes, pSpare, OffSpare, NumBytesSpare, pResult)
#endif

/*********************************************************************
*
*       FS_NOR_TEST_HOOK_DATA_WRITE_EX_BEGIN
*/
#if FS_SUPPORT_TEST
  #define CALL_TEST_HOOK_DATA_WRITE_EX_BEGIN(Unit, PageIndex, pData, pOff, pNumBytes, pSpare, pOffSpare, pNumBytesSpare) _CallTestHookDataWriteExBegin(Unit, PageIndex, pData, pOff, pNumBytes, pSpare, pOffSpare, pNumBytesSpare)
#else
  #define CALL_TEST_HOOK_DATA_WRITE_EX_BEGIN(Unit, PageIndex, pData, pOff, pNumBytes, pSpare, pOffSpare, pNumBytesSpare)
#endif

/*********************************************************************
*
*       FS_NOR_TEST_HOOK_DATA_WRITE_EX_END
*/
#if FS_SUPPORT_TEST
  #define CALL_TEST_HOOK_DATA_WRITE_EX_END(Unit, PageIndex, pData, Off, NumBytes, pSpare, OffSpare, NumBytesSpare, pResult) _CallTestHookDataWriteExEnd(Unit, PageIndex, pData, Off, NumBytes, pSpare, OffSpare, NumBytesSpare, pResult)
#else
  #define CALL_TEST_HOOK_DATA_WRITE_EX_END(Unit, PageIndex, pData, Off, NumBytes, pSpare, OffSpare, NumBytesSpare, pResult)
#endif

/*********************************************************************
*
*       FS_NOR_TEST_HOOK_BLOCK_ERASE
*/
#if FS_SUPPORT_TEST
  #define CALL_TEST_HOOK_BLOCK_ERASE(Unit, PageIndex, pResult) _CallTestHookBlockErase(Unit, PageIndex, pResult)
#else
  #define CALL_TEST_HOOK_BLOCK_ERASE(Unit, PageIndex, pResult)
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
*       ASSERT_UNIT_NO_IS_IN_RANGE
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)                                      \
    if ((Unit) >= (U8)FS_NAND_NUM_UNITS) {                                      \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NAND_UNI: Invalid unit number."));  \
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);                                      \
    }
#else
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)
#endif

/*********************************************************************
*
*       ASSERT_PHY_TYPE_IS_SET
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_PHY_TYPE_IS_SET(pInst)                                             \
    if ((pInst)->pPhyType == NULL) {                                                \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NAND_UNI: Phy. layer type not set."));  \
      FS_X_PANIC(FS_ERRCODE_UNKNOWN_DEVICE);                                        \
    }
#else
  #define ASSERT_PHY_TYPE_IS_SET(pInst)
#endif

/*********************************************************************
*
*       Local types
*
**********************************************************************
*/

typedef struct WRITE_API WRITE_API;

/*********************************************************************
*
*       NAND_UNI_WORK_BLOCK
*
*  Description
*    Information about a work block.
*
*  Additional information
*    The NAND_UNI_WORK_BLOCK structure has 6 elements, as can be seen below.
*    The first 2, pNext & pPrev, are used to keep it in a doubly linked list.
*    The next 2 elements are used to associate it with a data block and logical block index.
*    The last 2 elements contain the actual management data. The are pointers to arrays, allocated during initialization.
*    paAssign is a n bit array.
*      The number of bits is determined by the number of sectors per block.
*      The index is the logical position (BRSI, block relative sector index).
*/
typedef struct NAND_UNI_WORK_BLOCK {
  struct NAND_UNI_WORK_BLOCK * pNext;     // Pointer to next work buffer.     NULL if there is no next.
  struct NAND_UNI_WORK_BLOCK * pPrev;     // Pointer to previous work buffer. NULL if there is no previous.
  unsigned                     pbi;       // Physical Index of the destination block which data is written to. 0 means none is selected yet.
  unsigned                     lbi;       // Logical block index of the work block
  U16                          brsiFree;  // Position in block of the first sector we can write to.
  void                       * paAssign;  // Pointer to assignment table, containing n bits per block. n depends on number of sectors per block.
} NAND_UNI_WORK_BLOCK;

#if FS_NAND_SUPPORT_FAST_WRITE

/*********************************************************************
*
*       NAND_UNI_DATA_BLOCK
*
*  Description
*    Information about a data block.
*/
typedef struct NAND_UNI_DATA_BLOCK {
  struct NAND_UNI_DATA_BLOCK * pNext;     // Pointer to next structure. This member is NULL for the last structure in the list.
  struct NAND_UNI_DATA_BLOCK * pPrev;     // Pointer to previous structure. This member is NULL for the first structure in the list.
  unsigned                     pbi;       // Index of the physical block where the data is stored. 0 means physical block is assigned yet.
  U16                          brsiLast;  // Position in block of the last written sector.
} NAND_UNI_DATA_BLOCK;

#endif // FS_NAND_SUPPORT_FAST_WRITE

/*********************************************************************
*
*       NAND_UNI_INST
*
*  Description
*    This is the main data structure for the entire driver.
*    It contains data items of one instance of the driver.
*/
typedef struct {
  U8                         Unit;                        // Index of the driver instance
  U8                         IsInited;                    // Set to 1 if the instance has been successfully initialized.
  U8                         IsLLMounted;                 // Set to 1 if the NAND flash was successfully mounted
  U8                         LLMountFailed;               // Set to 1 if there was an error when mounting the NAND flash
  U8                         IsWriteProtected;            // Set to 1 if the NAND flash is write protected
  U8                         DataBusWidth;                // Number of lines used for exchanging the data with the NAND flash (0 - unknown, 1 - SPI, 8 - parallel 8-bit or 16 - parallel 16-bit)
  U8                         BadBlockMarkingType;         // Specified how the NAND blocks are marked as defective.
  U8                         HasFatalError;               // Set to 1 if the driver encountered a fatal error
  U8                         ErrorType;                   // Type of fatal error
  U32                        ErrorSectorIndex;            // Index of the sector where the fatal error occurred
  const FS_NAND_PHY_TYPE   * pPhyType;                    // Interface to physical layer
  const FS_NAND_ECC_HOOK   * pECCHook;                    // Functions to compute and check ECC
  U8                       * pFreeMap;                    // Pointer to physical block usage map. Each bit represents one physical block. 0: Block is not assigned; 1: Assigned or bad block.
                                                          // Only purpose is to find a free block.
  U8                       * pLog2PhyTable;               // Pointer to Log2Phytable, which contains the logical to physical block translation (0xFFFF -> Not assigned)
  U32                        NumSectors;                  // Number of logical sectors. This is redundant, but the value is used in a lot of places, so it is worth it!
  U32                        EraseCntMax;                 // Worst (= highest) erase count of all blocks
  U32                        NumBlocks;                   // Total number of blocks in the NAND partition
  U32                        NumLogBlocks;                // Number of blocks available for user data
  U32                        FirstBlock;                  // First physical block in the partition
  U32                        EraseCntMin;                 // Smallest erase count of all blocks. Used for active wear leveling.
  U32                        NumBlocksEraseCntMin;        // Number of erase counts with the smallest value
  U32                        NumWorkBlocks;               // Number of configured work blocks
  NAND_UNI_WORK_BLOCK      * pFirstWorkBlockInUse;        // Pointer to the first work block
  NAND_UNI_WORK_BLOCK      * pFirstWorkBlockFree;         // Pointer to the first free work block
  NAND_UNI_WORK_BLOCK      * paWorkBlock;                 // Work block management information
#if FS_NAND_SUPPORT_FAST_WRITE
  NAND_UNI_DATA_BLOCK      * pFirstDataBlockInUse;        // Pointer to the first data block in use
  NAND_UNI_DATA_BLOCK      * pFirstDataBlockFree;         // Pointer to the first free data block
  NAND_UNI_DATA_BLOCK      * paDataBlock;                 // Data block management information
#endif // FS_NAND_SUPPORT_FAST_WRITE
  U32                        MRUFreeBlock;                // Most recently used block that is free
  U16                        BytesPerPage;                // Number of bytes in the main area of a page
  U16                        BytesPerSpareArea;           // Number of bytes in the spare area of a page. Usually, this is BytesPerPage/32
  U8                         PPB_Shift;                   // Number of pages in a block as a power of 2 exponent. Typ. 6 for 64 pages for block.
  U8                         NumBitsPhyBlockIndex;        // Minimum number of bits required to store the index of a NAND block.
  U8                         IsHW_ECCUsed;                // Set to 1 if HW ECC is used to correct bit errors.
  U8                         IsSpareDataECCUsed;          // Set to 1 if the data in the spare area is protected by a separate ECC.
  U8                         NumBitsCorrectable;          // Number of bit errors the ECC is able to correct. It is used for the page blank check.
  U8                         AllowBlankUnusedSectors;     // Set to 1 if the sectors which do not contain valid information should be left empty (all bytes 0xFF) when copying a block.
  U8                         AllowReadErrorBadBlocks;     // Set to 1 if the driver has to mark a block as defective on a read or uncorrectable bit error.
#if FS_NAND_SUPPORT_BLOCK_GROUPING
  U8                         BPG_Shift;                   // Number of blocks in a group (virtual block) as a power of 2.
#endif // FS_NAND_SUPPORT_BLOCK_GROUPING
  U16                        NumBlocksFree;               // Number of work blocks reserved for fast write operations.
  U16                        NumSectorsFree;              // Number of sectors in a work block reserved for fast write operations.
  U8                         ActiveWLStatus;              // Working status of the active wear leveling procedure. Set temporarily to ACTIVE_WL_DISABLED_TEMP during the fast write operation to prevent block erase operations.
  U8                         ldBytesPerECCBlock;          // Number of bytes in a block covered by a single ECC specified as power of 2.
  U8                         PPO_Shift;                   // Number of operations performed in parallel by the physical layer as a power of 2.
  //
  // Position of management data in the spare area
  //
  U8                         OffBlockStat;
  U8                         OffEraseCnt;
  U8                         OffLBI;
  U8                         OffBlockTypeCnt;
  U8                         OffSectorStatMergeCnt;
  U8                         OffBRSI;
  U8                         OffNumSectors;
#if FS_NAND_SUPPORT_DATA_CRC
  U8                         OffDataCRC;
#endif // FS_NAND_SUPPORT_DATA_CRC
  //
  // Configuration items. 0 per default, which typically means: Use a reasonable default
  //
  U32                        FirstBlockConf;              // Allows sparing blocks at the beginning of the NAND flash
  U32                        MaxNumBlocks;                // Allows sparing blocks at the end of the NAND flash
  U32                        MaxEraseCntDiff;             // Threshold for active wear leveling
  U32                        NumWorkBlocksConf;           // Number of work blocks configured by the application
  U8                         pctOfBlocksReserved;         // Number of blocks to reserve in percentage of the total number of blocks
#if FS_NAND_ENABLE_ERROR_RECOVERY
  FS_READ_ERROR_DATA         ReadErrorData;               // Function to be called when a bit error occurs to get corrected data.
#endif // FS_NAND_ENABLE_ERROR_RECOVERY
  //
  // Additional info for debugging purposes
  //
#if FS_NAND_ENABLE_STATS
  FS_NAND_STAT_COUNTERS      StatCounters;
#endif // FS_NAND_ENABLE_STATS
#if FS_NAND_MAX_BIT_ERROR_CNT
  U8                         MaxBitErrorCnt;              // Number of bit errors in a page which trigger a relocation of a block
  U8                         HasHW_ECC;                   // Set to 1 if the NAND flash has HW ECC.
  U8                         HandleWriteDisturb;          // If set to 1 if the driver checks for bit errors all the pages in the block that contains the modified page.
#endif // FS_NAND_MAX_BIT_ERROR_CNT
#if FS_NAND_VERIFY_ERASE
  U8                         VerifyErase;
#endif // FS_NAND_VERIFY_ERASE
#if FS_NAND_VERIFY_WRITE
  U8                         VerifyWrite;
#endif // FS_NAND_VERIFY_WRITE
#if FS_NAND_OPTIMIZE_SPARE_AREA_READ
  U8                         ActiveSpareAreaRanges;
  U16                        BytesPerSpareStripe;
#endif // FS_NAND_OPTIMIZE_SPARE_AREA_READ
#if FS_NAND_RECLAIM_DRIVER_BAD_BLOCKS
  U8                         ReclaimDriverBadBlocks;
#endif // FS_NAND_RECLAIM_DRIVER_BAD_BLOCKS
  const WRITE_API          * pWriteAPI;
} NAND_UNI_INST;

/*********************************************************************
*
*       WRITE_API
*
*  Description
*    Functions called internally by the driver to perform operations
*    that modify the data on the NAND flash.
*/
struct WRITE_API {            //lint -esym(9058, WRITE_API) tag unused outside of typedefs. Rationale: the typedef is used as forward declaration.
  int (*pfClearBlock)      (NAND_UNI_INST * pInst, unsigned BlockIndex, U32 EraseCnt);
  int (*pfCleanWorkBlock)  (NAND_UNI_INST * pInst, NAND_UNI_WORK_BLOCK * pWorkBlock, unsigned brsi, const U32 * pData);
  int (*pfRecoverDataBlock)(NAND_UNI_INST * pInst, unsigned pbiData);
  int (*pfMarkAsReadOnly)  (NAND_UNI_INST * pInst, U16 ErrorType, U32 ErrorSectorIndex);
  int (*pfFreeBadBlock)    (NAND_UNI_INST * pInst, unsigned pbi, int ErrorType, U32 ErrorBRSI);
#if FS_NAND_SUPPORT_BLOCK_GROUPING
  int (*pfFreeWorkBlock)   (NAND_UNI_INST * pInst, NAND_UNI_WORK_BLOCK * pWorkBlock, U32 EraseCnt);
#endif // FS_NAND_SUPPORT_BLOCK_GROUPING
};

/*********************************************************************
*
*       Static const data
*
**********************************************************************
*/

/*********************************************************************
*
*       The first sector/block in a NAND flash should have these
*       values so the NAND driver recognize the device as properly formatted.
*/
static const U8 _acInfo[16] = {
  0x53, 0x45, 0x47, 0x47, 0x45, 0x52, 0x00, 0x00,           // Id (Can be expanded in the future to include format / version information)
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static U32                                     * _pSectorBuffer;                  // We need a buffer for one sector for internal operations such as copying a block etc.
static U8                                      * _pSpareAreaData;                 // Buffer for spare area.
static NAND_UNI_INST                           * _apInst[FS_NAND_NUM_UNITS];      // List of driver instances.
static U8                                        _NumUnits = 0;                   // Number of driver instances.
static FS_NAND_ON_FATAL_ERROR_CALLBACK         * _pfOnFatalError;                 // Callback for the fatal error condition.
#if FS_NAND_VERIFY_WRITE
  static U32                                   * _pVerifyBuffer;                  // Buffer for data verification.
#endif // FS_NAND_VERIFY_WRITE
#if FS_SUPPORT_TEST
  static FS_NAND_TEST_HOOK_NOTIFICATION        * _pfTestHookFailSafe;             // Test hook for the fail-safety operation.
  static FS_NAND_TEST_HOOK_DATA_READ_BEGIN     * _pfTestHookDataReadBegin;        // Test hook for the page read operation.
  static FS_NAND_TEST_HOOK_DATA_READ_END       * _pfTestHookDataReadEnd;          // Test hook for the page read operation.
  static FS_NAND_TEST_HOOK_DATA_READ_EX_BEGIN  * _pfTestHookDataReadExBegin;      // Test hook for the page read operation.
  static FS_NAND_TEST_HOOK_DATA_READ_EX_END    * _pfTestHookDataReadExEnd;        // Test hook for the page read operation.
  static FS_NAND_TEST_HOOK_DATA_WRITE_EX_BEGIN * _pfTestHookDataWriteExBegin;     // Test hook for the page write operation.
  static FS_NAND_TEST_HOOK_DATA_WRITE_EX_END   * _pfTestHookDataWriteExEnd;       // Test hook for the page write operation.
  static FS_NAND_TEST_HOOK_BLOCK_ERASE         * _pfTestHookBlockErase;           // Test hook for the block erase operation.
#endif // FS_SUPPORT_TEST
#if FS_NAND_ENABLE_ERROR_RECOVERY
  static U8                                      _IsERActive = 0;                 // Set to 1 during the call to error recovery function.
  static U8                                    * _pSpareAreaDataER;               // Buffer for spare area used during the error recovery.
#endif // FS_NAND_ENABLE_ERROR_RECOVERY
#if (FS_NAND_MAX_PAGE_SIZE != 0)
  static U8                                      _ldMaxPageSize = 0;              // Maximum page size (in bytes) supported by the driver (power of 2 exponent).
#endif // FS_NAND_MAX_PAGE_SIZE != 0
#if (FS_NAND_MAX_SPARE_AREA_SIZE != 0)
  static U16                                     _MaxSpareAreaSize = 0;           // Maximum size (in bytes) of the spare area supported by the driver.
#endif // FS_NAND_MAX_SPARE_AREA_SIZE != 0

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

#if FS_SUPPORT_TEST

/*********************************************************************
*
*       _CallTestHookFailSafe
*/
static void _CallTestHookFailSafe(U8 Unit) {
  if (_pfTestHookFailSafe != NULL) {
    _pfTestHookFailSafe(Unit);
  }
}

/*********************************************************************
*
*       _CallTestHookDataReadBegin
*/
static void _CallTestHookDataReadBegin(U8 Unit, U32 PageIndex, void * pData, unsigned * pOff, unsigned * pNumBytes) {
  if (_pfTestHookDataReadBegin != NULL) {
    _pfTestHookDataReadBegin(Unit, PageIndex, pData, pOff, pNumBytes);
  }
}

/*********************************************************************
*
*       _CallTestHookDataReadEnd
*/
static void _CallTestHookDataReadEnd(U8 Unit, U32 PageIndex, void * pData, unsigned Off, unsigned NumBytes, int * pResult) {
  if (_pfTestHookDataReadEnd != NULL) {
    _pfTestHookDataReadEnd(Unit, PageIndex, pData, Off, NumBytes, pResult);
  }
}

/*********************************************************************
*
*       _CallTestHookDataReadExBegin
*/
static void _CallTestHookDataReadExBegin(U8 Unit, U32 PageIndex, void * pData, unsigned * pOff, unsigned * pNumBytes, void * pSpare, unsigned * pOffSpare, unsigned * pNumBytesSpare) {
  if (_pfTestHookDataReadExBegin != NULL) {
    _pfTestHookDataReadExBegin(Unit, PageIndex, pData, pOff, pNumBytes, pSpare, pOffSpare, pNumBytesSpare);
  }
}

/*********************************************************************
*
*       _CallTestHookDataReadExEnd
*/
static void _CallTestHookDataReadExEnd(U8 Unit, U32 PageIndex, void * pData, unsigned Off, unsigned NumBytes, void * pSpare, unsigned OffSpare, unsigned NumBytesSpare, int * pResult) {
  if (_pfTestHookDataReadExEnd != NULL) {
    _pfTestHookDataReadExEnd(Unit, PageIndex, pData, Off, NumBytes, pSpare, OffSpare, NumBytesSpare, pResult);
  }
}

/*********************************************************************
*
*       _CallTestHookDataWriteExBegin
*/
static void _CallTestHookDataWriteExBegin(U8 Unit, U32 PageIndex, const void ** pData, unsigned * pOff, unsigned * pNumBytes, const void ** pSpare, unsigned * pOffSpare, unsigned * pNumBytesSpare) {
  if (_pfTestHookDataWriteExBegin != NULL) {
    _pfTestHookDataWriteExBegin(Unit, PageIndex, pData, pOff, pNumBytes, pSpare, pOffSpare, pNumBytesSpare);
  }
}

/*********************************************************************
*
*       _CallTestHookDataWriteExEnd
*/
static void _CallTestHookDataWriteExEnd(U8 Unit, U32 PageIndex, const void * pData, unsigned Off, unsigned NumBytes, const void * pSpare, unsigned OffSpare, unsigned NumBytesSpare, int * pResult) {
  if (_pfTestHookDataWriteExEnd != NULL) {
    _pfTestHookDataWriteExEnd(Unit, PageIndex, pData, Off, NumBytes, pSpare, OffSpare, NumBytesSpare, pResult);
  }
}

/*********************************************************************
*
*       _CallTestHookBlockErase
*/
static void _CallTestHookBlockErase(U8 Unit, U32 PageIndex, int * pResult) {
  if (_pfTestHookBlockErase != NULL) {
    _pfTestHookBlockErase(Unit, PageIndex, pResult);
  }
}

#endif // FS_SUPPORT_TEST

#if (FS_NAND_SUPPORT_BLOCK_GROUPING != 0) || (FS_NAND_MAX_PAGE_SIZE != 0)

/*********************************************************************
*
*       _ld
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

#endif // FS_NAND_SUPPORT_BLOCK_GROUPING != 0 || FS_NAND_MAX_PAGE_SIZE != 0

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
*       _Count0Bits
*
*   Function description
*     Returns the number of bits set to 0.
*/
static unsigned _Count0Bits(U32 Value) {
  unsigned NumBits;

  NumBits = 0;
  while (Value != 0xFFFFFFFFuL) {
    Value |= Value + 1u;          // Turn on the rightmost 0-bit.
    ++NumBits;
  }
  return NumBits;
}

/*********************************************************************
*
*       _CalcNumWorkBlocksDefault
*
*   Function description
*     Computes the default number of work blocks.
*     This is a percentage of number of NAND blocks.
*/
static U32 _CalcNumWorkBlocksDefault(U32 NumBlocks) {
  U32 NumWorkBlocks;

#ifdef FS_NAND_MAX_WORK_BLOCKS
  FS_USE_PARA(NumBlocks);
  NumWorkBlocks = FS_NAND_MAX_WORK_BLOCKS;
#else
  //
  // Allocate 10% of NAND capacity for work blocks
  //
  NumWorkBlocks = NumBlocks >> 7;
  //
  // Limit the number of work blocks to reasonable values
  //
  if (NumWorkBlocks > (U32)MAX_NUM_WORK_BLOCKS) {
    NumWorkBlocks = MAX_NUM_WORK_BLOCKS;
  }
  if (NumWorkBlocks < (U32)NUM_WORK_BLOCKS_MIN) {
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
static int _CalcNumBlocksToUse(const NAND_UNI_INST * pInst, U32 NumBlocks, U32 NumWorkBlocks) {
  int NumLogBlocks;
  int NumBlocksReserved;
  int pctOfBlocksReserved;
  U32 NumBlocksToUse;

  //
  // Compute the number of logical blocks. These are the blocks which are
  // actually available to the file system and therefor determines the capacity.
  // We reserve a small percentage (about 3%) for bad blocks + the number of work blocks
  // + 1 info block (first block) + 1 block for copy operations.
  //
  pctOfBlocksReserved = (int)pInst->pctOfBlocksReserved;
  //
  // Reserve some blocks for blocks which are or can turn "bad" that is unusable.
  // We need to reserve at least one block.
  //
  if (pctOfBlocksReserved == 0) {
    NumBlocksToUse = (NumBlocks * 125u) >> 7;
    NumLogBlocks   = (int)NumBlocksToUse;
  } else {
    NumBlocksReserved = (int)NumBlocks * pctOfBlocksReserved / 100;
    if (NumBlocksReserved == 0) {
      NumBlocksReserved = 1;
    }
    NumLogBlocks = (int)NumBlocks - NumBlocksReserved;
  }
  NumBlocksReserved  = (int)NumWorkBlocks + NUM_BLOCKS_RESERVED;
  NumLogBlocks      -= NumBlocksReserved;
  return NumLogBlocks;
}

/*********************************************************************
*
*       _CalcAndStoreECC
*
*   Function description
*     Computes the ECC values and writes them into the buffer for the spare area
*/
static void _CalcAndStoreECC(const NAND_UNI_INST * pInst, const U32 * pData, U8 * pSpare) {
  const FS_NAND_ECC_HOOK * pECCHook;
  unsigned                 ECCBlocksPerPage;
  unsigned                 ldBytesPerECCBlock;
  unsigned                 BytesPerSpareArea;
  unsigned                 NumBytesAtOnceSpare;
  unsigned                 NumWordsAtOnceData;

  pECCHook = pInst->pECCHook;
  if (pECCHook != NULL) {
    ldBytesPerECCBlock = pECCHook->ldBytesPerBlock;
    if (ldBytesPerECCBlock == 0u) {
      ldBytesPerECCBlock = LD_BYTES_PER_ECC_BLOCK;
    }
    ECCBlocksPerPage    = (unsigned)pInst->BytesPerPage >> ldBytesPerECCBlock;
    BytesPerSpareArea   = pInst->BytesPerSpareArea;
    NumBytesAtOnceSpare = BytesPerSpareArea / ECCBlocksPerPage;
    NumWordsAtOnceData  = 1uL << (ldBytesPerECCBlock - 2u);
    do {
      pECCHook->pfCalc(pData, pSpare);
      pData  += NumWordsAtOnceData;
      pSpare += NumBytesAtOnceSpare;
    } while (--ECCBlocksPerPage != 0u);
  }
}

/*********************************************************************
*
*       _ApplyECC
*
*  Function description
*    Uses the ECC values stored to spare area to correct the data if necessary.
*
*  Parameters
*    pInst                  Driver instance.
*    pData                  Data read from the main area of the page. Can be NULL.
*    pSpare                 Data read from the spare area of the page. Cannot be NULL.
*    pMaxNumBitsCorrected   [OUT] Maximum number of bit errors corrected in an ECC block.
*
*  Return value
*    ==RESULT_NO_ERROR                  OK, data is valid. No error in data
*    ==RESULT_BIT_ERRORS_CORRECTED      Bit error(s) in data which has been corrected
*    ==RESULT_BIT_ERROR_IN_ECC          OK, data is valid but there is a bit error in ECC
*    ==RESULT_UNCORRECTABLE_BIT_ERRORS  Uncorrectable bit error
*/
static int _ApplyECC(NAND_UNI_INST * pInst, U32 * pData, U8 * pSpare, unsigned * pMaxNumBitsCorrected) {    //lint -efunc(818, _ApplyECC) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory]. Rationale: the statistical counters stored in the driver instance are updated in debug builds.
  const FS_NAND_ECC_HOOK * pECCHook;
  int                      r;
  int                      Result;
  unsigned                 ECCBlocksPerPage;
  unsigned                 ldBytesPerECCBlock;
  unsigned                 NumBytesAtOnceSpare;
  unsigned                 NumWordsAtOnceData;
  unsigned                 BytesPerSpareArea;
  unsigned                 MaxNumBitsCorrected;
  int                      ConvertResult;

  Result              = 0;
  MaxNumBitsCorrected = 0;
  ConvertResult       = 0;
  pECCHook = pInst->pECCHook;
  if (pECCHook != NULL) {
    ldBytesPerECCBlock = pECCHook->ldBytesPerBlock;
    if (ldBytesPerECCBlock == 0u) {
      ldBytesPerECCBlock = LD_BYTES_PER_ECC_BLOCK;
    } else {
      ConvertResult = 1;            // pfApply() returns different values than in the initial version. Remember that we have to convert these values.
    }
    ECCBlocksPerPage    = (unsigned)pInst->BytesPerPage >> ldBytesPerECCBlock;
    BytesPerSpareArea   = pInst->BytesPerSpareArea;
    NumBytesAtOnceSpare = BytesPerSpareArea / ECCBlocksPerPage;
    NumWordsAtOnceData  = 1uL << (ldBytesPerECCBlock - 2u);
    do {
      r = pECCHook->pfApply(pData, pSpare);
      if (ConvertResult != 0) {     // Does the result need to be converted?
        if (r == ECC_CORR_FAILURE) {
          r = RESULT_UNCORRECTABLE_BIT_ERRORS;
        } else {
          //
          // Remember what was the maximum number of bit errors corrected.
          //
          if ((int)MaxNumBitsCorrected < r) {
            MaxNumBitsCorrected = (unsigned)r;
          }
          //
          // Update statistical counters related to bit error correction.
          //
          IF_STATS(pInst->StatCounters.BitErrorCnt += (U32)r);
#if FS_NAND_ENABLE_STATS
          if ((r > 0) && (r <= FS_NAND_STAT_MAX_BIT_ERRORS)) {
            pInst->StatCounters.aBitErrorCnt[r - 1]++;
          }
#endif // FS_NAND_ENABLE_STATS
          //
          // Convert the return value.
          //
          if (r == ECC_CORR_NOT_APPLIED) {
            r = RESULT_NO_ERROR;
          } else {
            r = RESULT_BIT_ERRORS_CORRECTED;
          }
        }
      }
      if (r > Result) {
        Result = r;
      }
      if (pData != NULL) {
        pData += NumWordsAtOnceData;
      }
      pSpare += NumBytesAtOnceSpare;
    } while (--ECCBlocksPerPage != 0u);
  }
  if (pMaxNumBitsCorrected != NULL) {
    *pMaxNumBitsCorrected = MaxNumBitsCorrected;
  }
  return Result;
}

/*********************************************************************
*
*       _EnableHW_ECC
*
*   Function description
*     Tells the NAND flash to correct the bit errors using the internal ECC engine.
*
*   Return value
*     0   OK
*     1   An error occurred
*/
static int _EnableHW_ECC(const NAND_UNI_INST * pInst) {
  int r;

  r = 0;
  if (pInst->pPhyType->pfEnableECC != NULL) {
    r = pInst->pPhyType->pfEnableECC(pInst->Unit);
  }
  return r;
}

/*********************************************************************
*
*       _DisableHW_ECC
*
*   Function description
*     Tells the NAND flash to deliver uncorrected data.
*
*   Return value
*     0   OK
*     1   An error occurred
*/
static int _DisableHW_ECC(const NAND_UNI_INST * pInst) {
  int r;

  r = 0;
  if (pInst->pPhyType->pfDisableECC != NULL) {
    r = pInst->pPhyType->pfDisableECC(pInst->Unit);
  }
  return r;
}

/*********************************************************************
*
*       _EnableHW_ECCIfRequired
*/
static int _EnableHW_ECCIfRequired(const NAND_UNI_INST * pInst) {
  int r;

  r = 0;
  if (pInst->IsHW_ECCUsed != 0u) {      // Bit error correction performed in the HW?
    r = _EnableHW_ECC(pInst);
  }
  return r;
}

/*********************************************************************
*
*       _DisableHW_ECCIfRequired
*/
static int _DisableHW_ECCIfRequired(const NAND_UNI_INST * pInst) {
  int r;

  r = 0;
  if (pInst->IsHW_ECCUsed != 0u) {      // Bit error correction performed in the HW?
    r = _DisableHW_ECC(pInst);
  }
  return r;
}

/*********************************************************************
*
*       _ConfigureHW_ECC
*
*   Function description
*     Sets the correction level for the HW ECC.
*
*   Return value
*     0   OK, correction level set
*     1   Correction level not supported
*/
static int _ConfigureHW_ECC(const NAND_UNI_INST * pInst, U8 NumBitsCorrectable, U16 BytesPerECCBlock) {
  int r;

  r = 0;
  if (pInst->pPhyType->pfConfigureECC != NULL) {
    r = pInst->pPhyType->pfConfigureECC(pInst->Unit, NumBitsCorrectable, BytesPerECCBlock);
  }
  return r;
}

/*********************************************************************
*
*       _GetHW_ECCResult
*
*  Function description
*    Returns the correction status of the HW ECC.
*
*  Return value
*    ==0    OK, correction status returned
*    ==1    Correction status not supported
*/
static int _GetHW_ECCResult(const NAND_UNI_INST * pInst, FS_NAND_ECC_RESULT * pResult) {
  int r;

  r = 1;          // Correction status not supported.
  if (pInst->pPhyType->pfGetECCResult != NULL) {
    r = pInst->pPhyType->pfGetECCResult(pInst->Unit, pResult);
  }
  return r;
}

/*********************************************************************
*
*       _EnterRawMode
*
*  Function description
*    Request the physical layer to maintain the layout of the data
*    written to and read from the NAND flash device.
*
*  Additional information
*    Most of the NAND flash controllers use a native layout for storing
*    the data in a NAND page that typically do not correspond to the data
*    layout used by the Universal NAND driver. For example the data from
*    the spare area is stored at the beginning of the page and the ECC is
*    interleaved with data. The Universal NAND driver calls _EnterRawMode()
*    to make sure that the data layout of the following data read and write
*    operations is not modified in any way by the NAND physical layer.
*/
static int _EnterRawMode(const NAND_UNI_INST * pInst) {
  int r;

  r = 0;
  if (pInst->pPhyType->pfSetRawMode != NULL) {
    r = pInst->pPhyType->pfSetRawMode(pInst->Unit, 1);
  }
  return r;
}

/*********************************************************************
*
*       _LeaveRawMode
*
*  Function description
*    Request the physical layer to use the native layout when writing
*    the data to or reading the data from the NAND flash device.
*/
static int _LeaveRawMode(const NAND_UNI_INST * pInst) {
  int r;

  r = 0;
  if (pInst->pPhyType->pfSetRawMode != NULL) {
    r = pInst->pPhyType->pfSetRawMode(pInst->Unit, 0);
  }
  return r;
}

/*********************************************************************
*
*       _GetNextFreeSector
*
*  Function description
*    Returns the BRSI of the sector we can write to.
*
*  Return value
*    !=0    OK, index of the sector (relative to block) which can be written.
*    ==0    All sectors written
*/
static unsigned _GetNextFreeSector(const NAND_UNI_INST * pInst, NAND_UNI_WORK_BLOCK * pWorkBlock) {
  unsigned SectorsPerBlock;
  unsigned brsiFree;
  unsigned NumSectorsFree;

  SectorsPerBlock = 1uL << pInst->PPB_Shift;
  NumSectorsFree  = pInst->NumSectorsFree;
  brsiFree        = pWorkBlock->brsiFree;
  if ((brsiFree < BRSI_BLOCK_INFO) ||
     (brsiFree >= SectorsPerBlock) ||
     (brsiFree >= (SectorsPerBlock - NumSectorsFree))) {
    return 0;                         // All sectors in the work block were written.
  }
  pWorkBlock->brsiFree++;
  return brsiFree;
}

/*********************************************************************
*
*       _GetBPG_Shift
*
*  Function description
*    Returns the configured number of blocks in the group as power of 2.
*/
static unsigned _GetBPG_Shift(const NAND_UNI_INST * pInst) {
  unsigned r;

#if FS_NAND_SUPPORT_BLOCK_GROUPING
  r = pInst->BPG_Shift;
#else
  FS_USE_PARA(pInst);
  r = 0;            // 1 block in the group.
#endif // FS_NAND_SUPPORT_BLOCK_GROUPING
  return r;
}

/*********************************************************************
*
*       _IsBlockGroupingEnabled
*
*  Function description
*    Checks if the block grouping feature is enabled.
*
*  Parameters
*    pInst      Driver instance.
*
*  Return value
*    ==1      Functionality enabled.
*    ==0      Functionality disabled.
*/
static int _IsBlockGroupingEnabled(const NAND_UNI_INST * pInst) {
  int r;

  r = 0;
#if FS_NAND_SUPPORT_BLOCK_GROUPING
  if (pInst->BPG_Shift != 0u) {
    r = 1;
  }
#else
  FS_USE_PARA(pInst);
#endif // FS_NAND_SUPPORT_BLOCK_GROUPING
  return r;
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
*    ==0    O.K.
*    ==1    Error, Could not apply device paras
*/
static int _ReadApplyDeviceParas(NAND_UNI_INST * pInst) {
  U32                 BytesPerPage;
  U32                 BytesPerPageConf;
  unsigned            PPB_Shift;
  U32                 MaxNumBlocks;
  U32                 NumBlocks;
  FS_NAND_DEVICE_INFO DeviceInfo;
  U32                 NumWorkBlocks;
  int                 NumLogBlocks;
  unsigned            NumBitsCorrectableReq;
  unsigned            NumBitsCorrectableSup;
  unsigned            SectorsPerBlock;
  int                 r;
  unsigned            BytesPerSpareArea;
  int                 IsHW_ECCUsed;
  unsigned            OffBlockStat;
  unsigned            OffEraseCnt;
  unsigned            OffLBI;
  unsigned            OffBlockTypeCnt;
  unsigned            OffSectorStatMergeCnt;
  unsigned            OffBRSI;
  unsigned            OffNumSectors;
  U32                 FirstBlock;
  unsigned            BPG_Shift;
  unsigned            ldBytesPerECCBlockReq;
  unsigned            ldBytesPerECCBlockSup;
#if FS_NAND_SUPPORT_DATA_CRC
  unsigned            OffDataCRC;
#endif
  unsigned            OffSpare;
  unsigned            BytesPerSpareStripe;
  unsigned            ECCBlocksPerPage;
  int                 IsSpareDataECCUsed;

  //
  // Get information about the NAND flash device.
  //
  FS_MEMSET(&DeviceInfo, 0, sizeof(DeviceInfo));
  r = pInst->pPhyType->pfInitGetDeviceInfo(pInst->Unit, &DeviceInfo);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_UNI: Could not read device info."));
    return 1;
  }
  //
  // Update the number of blocks and the number of pages in a block
  // according to the configured number of blocks in a group.
  //
  PPB_Shift = DeviceInfo.PPB_Shift;
  NumBlocks = DeviceInfo.NumBlocks;
  BPG_Shift = _GetBPG_Shift(pInst);
  if (BPG_Shift != 0u) {
    PPB_Shift  += BPG_Shift;
    NumBlocks >>= BPG_Shift;
  }
  //
  // Check the number of blocks configured for storage.
  //
  MaxNumBlocks = pInst->MaxNumBlocks;
  FirstBlock   = pInst->FirstBlockConf;
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
  NumLogBlocks = _CalcNumBlocksToUse(pInst, NumBlocks, NumWorkBlocks);
  if (NumLogBlocks <= 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_UNI: Insufficient logical blocks."));
    return 1;
  }
  BytesPerPage = 1uL << DeviceInfo.BPP_Shift;
  if (BytesPerPage < MIN_BYTES_PER_PAGE) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_UNI: Page size of device is too small. A minimum of %d bytes per page is required.", MIN_BYTES_PER_PAGE));
    return 1;                                     // Error
  }
  //
  // Check if the sector buffer is large enough.
  //
#if (FS_NAND_MAX_PAGE_SIZE > 0)
  BytesPerPageConf = FS_NAND_MAX_PAGE_SIZE;
#else
  BytesPerPageConf = FS_Global.MaxSectorSize;     // Use the maximum sector size if the page size is not configured.
#endif
  if (BytesPerPage > BytesPerPageConf) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_UNI: Page size is larger than the sector buffer."));
    return 1;                                     // Error
  }
  //
  // Determine the size of the spare area.
  //
  BytesPerSpareArea = DeviceInfo.BytesPerSpareArea;
  if (BytesPerSpareArea == 0u) {
    BytesPerSpareArea = BytesPerPage >> 5;        // If the physical layer does not provide a size for the spare area use the usual size of BytesPerPage / 32.
  }
#if (FS_NAND_MAX_SPARE_AREA_SIZE > 0)
  if (BytesPerSpareArea > (unsigned)FS_NAND_MAX_SPARE_AREA_SIZE) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_UNI: Buffer for spare area too small. Increase FS_NAND_MAX_SPARE_AREA_SIZE."));
    return 1;                                     // Error
  }
#endif
  //
  // Provide default ECC computation routines if none were configured.
  //
  if (pInst->pECCHook == NULL) {
    pInst->pECCHook = FS_NAND_ECC_HOOK_DEFAULT;
  }
  //
  // Determine whether HW ECC should be used and if the data in the spare
  // area is protected by a separate ECC.
  //
  IsHW_ECCUsed = 0;
  if ((pInst->pECCHook->pfApply == NULL) || (pInst->pECCHook->pfCalc == NULL)) {
    IsHW_ECCUsed = 1;
  }
  IsSpareDataECCUsed = 0;
  if (pInst->pECCHook->NumBitsCorrectableSpare != 0u) {
    IsSpareDataECCUsed = 1;
  }
  //
  // Get the number of bit correctable errors requested by the NAND flash and correctable by the ECC algorithm.
  //
  NumBitsCorrectableSup = pInst->pECCHook->NumBitsCorrectable;
  NumBitsCorrectableReq = DeviceInfo.ECC_Info.NumBitsCorrectable;
  if (NumBitsCorrectableSup == 0u) {
    NumBitsCorrectableSup = NumBitsCorrectableReq;
  }
  if (NumBitsCorrectableReq == 0u) {
    NumBitsCorrectableReq = NumBitsCorrectableSup;
  }
  //
  // Check if the correction level of ECC is suitable for the NAND flash.
  //
  if (NumBitsCorrectableReq > NumBitsCorrectableSup) {
    FS_DEBUG_WARN((FS_MTYPE_DRIVER, "NAND_UNI: Correction level of ECC is too low for the NAND flash."));
  }
  //
  // Calculate the number of bytes in an ECC block.
  //
  ldBytesPerECCBlockSup = pInst->pECCHook->ldBytesPerBlock;
  if (ldBytesPerECCBlockSup == 0u) {
    ldBytesPerECCBlockSup = LD_BYTES_PER_ECC_BLOCK;
  }
  ldBytesPerECCBlockReq = DeviceInfo.ECC_Info.ldBytesPerBlock;
  if (ldBytesPerECCBlockReq == 0u) {
    ldBytesPerECCBlockReq = ldBytesPerECCBlockSup;
  }
  if (ldBytesPerECCBlockReq != ldBytesPerECCBlockSup) {
    FS_DEBUG_WARN((FS_MTYPE_DRIVER, "NAND_UNI: The NAND flash requests a different ECC block size."));
  }
  //
  // Enable the HW ECC if required.
  //
  if (IsHW_ECCUsed != 0) {
    r = _ConfigureHW_ECC(pInst, (U8)NumBitsCorrectableReq, (U16)(1uL << ldBytesPerECCBlockReq));
    if (r != 0) {
      FS_DEBUG_WARN((FS_MTYPE_DRIVER, "NAND_UNI: Error correction level not supported by the HW ECC."));
    }
    //
    // Enable the HW ECC. It depends on the physical layer whether the HW ECC of the NAND flash device or the HW ECC of the NAND flash controller is used.
    //
    r = _EnableHW_ECC(pInst);
    if (r != 0) {
      FS_DEBUG_WARN((FS_MTYPE_DRIVER, "NAND_UNI: Could not enable the HW ECC."));
    }
  }
  //
  // Calculate the byte offset of management information in the spare area.
  //
  OffSpare               = 0;
  ECCBlocksPerPage       = BytesPerPage >> ldBytesPerECCBlockReq;
  BytesPerSpareStripe    = BytesPerSpareArea / ECCBlocksPerPage;
  OffBlockStat           = OffSpare + SPARE_OFF_BLOCK_STAT;
  OffEraseCnt            = OffSpare + SPARE_OFF_ERASE_CNT;
  OffSpare              += BytesPerSpareStripe;
  OffLBI                 = OffSpare + SPARE_OFF_LBI;
  OffBlockTypeCnt        = OffSpare + SPARE_OFF_BLOCK_TYPE_CNT;
  OffSectorStatMergeCnt  = OffSpare + SPARE_OFF_SECTOR_STAT_MERGE_CNT;
  OffSpare              += BytesPerSpareStripe;
  OffBRSI                = OffSpare + SPARE_OFF_BRSI;
  OffNumSectors          = OffSpare + SPARE_OFF_NUM_SECTORS;
#if FS_NAND_SUPPORT_DATA_CRC
  OffSpare              += BytesPerSpareStripe;
  OffDataCRC             = OffSpare + SPARE_OFF_DATA_CRC;
#endif // FS_NAND_SUPPORT_DATA_CRC
  //
  // Save the parameters to driver instance.
  //
  SectorsPerBlock              = (1uL << PPB_Shift) - 1u;       // First sector is reserved for erase count and bad block status.
  pInst->NumBlocks             = NumBlocks;
  pInst->NumBitsPhyBlockIndex  = (U8)FS_BITFIELD_CalcNumBitsUsed(NumBlocks);
  pInst->NumLogBlocks          = (U32)NumLogBlocks;
  pInst->NumWorkBlocks         = NumWorkBlocks;
  pInst->BytesPerPage          = (U16)BytesPerPage;
  pInst->NumSectors            = pInst->NumLogBlocks * SectorsPerBlock;
  pInst->PPB_Shift             = (U8)PPB_Shift;
  pInst->BytesPerSpareArea     = (U16)BytesPerSpareArea;
  pInst->IsHW_ECCUsed          = (U8)IsHW_ECCUsed;
  pInst->IsSpareDataECCUsed    = (U8)IsSpareDataECCUsed;
  pInst->NumBitsCorrectable    = (U8)NumBitsCorrectableReq;
  pInst->OffBlockStat          = (U8)OffBlockStat;
  pInst->OffEraseCnt           = (U8)OffEraseCnt;
  pInst->OffLBI                = (U8)OffLBI;
  pInst->OffBlockTypeCnt       = (U8)OffBlockTypeCnt;
  pInst->OffSectorStatMergeCnt = (U8)OffSectorStatMergeCnt;
  pInst->OffBRSI               = (U8)OffBRSI;
  pInst->OffNumSectors         = (U8)OffNumSectors;
#if FS_NAND_SUPPORT_DATA_CRC
  pInst->OffDataCRC            = (U8)OffDataCRC;
#endif // FS_NAND_SUPPORT_DATA_CRC
#if FS_NAND_OPTIMIZE_SPARE_AREA_READ
  pInst->BytesPerSpareStripe   = (U16)BytesPerSpareStripe;
#endif // FS_NAND_OPTIMIZE_SPARE_AREA_READ
  pInst->FirstBlock            = FirstBlock;
  pInst->ldBytesPerECCBlock    = (U8)ldBytesPerECCBlockReq;
#if FS_NAND_MAX_BIT_ERROR_CNT
  pInst->HasHW_ECC             = DeviceInfo.ECC_Info.HasHW_ECC;
#endif // FS_NAND_MAX_BIT_ERROR_CNT
  pInst->DataBusWidth          = DeviceInfo.DataBusWidth;
  pInst->BadBlockMarkingType   = DeviceInfo.BadBlockMarkingType;
  pInst->PPO_Shift             = (U8)DeviceInfo.PPO_Shift;
  return 0;                   // O.K., successfully identified
}

/*********************************************************************
*
*       _StoreBlockType
*
*   Function description
*     Writes the block type to static spare area buffer.
*/
static void _StoreBlockType(const NAND_UNI_INST * pInst, unsigned BlockType) {
  unsigned Data;
  unsigned Off;

  Off = pInst->OffBlockTypeCnt;
  Data  = _pSpareAreaData[Off];
  Data &= ~(0xFuL << 4);
  Data |= (BlockType & 0xFu) << 4;
  _pSpareAreaData[Off] = (U8)Data;
}

/*********************************************************************
*
*       _LoadBlockType
*
*   Function description
*     Reads the block type from static spare area buffer.
*/
static unsigned _LoadBlockType(const NAND_UNI_INST * pInst) {
  unsigned BlockType;
  unsigned Off;

  Off = pInst->OffBlockTypeCnt;
  BlockType = ((unsigned)_pSpareAreaData[Off] >> 4) & 0xFu;
  return BlockType;
}

/*********************************************************************
*
*       _StoreBlockCnt
*
*   Function description
*     Writes the block count to static spare area buffer.
*/
static void _StoreBlockCnt(const NAND_UNI_INST * pInst, unsigned BlockCnt) {
  unsigned Data;
  unsigned Off;

  Off = pInst->OffBlockTypeCnt;
  Data = _pSpareAreaData[Off];
  Data &= ~0xFu;
  Data |= BlockCnt & 0xFu;
  _pSpareAreaData[Off] = (U8)Data;
}

/*********************************************************************
*
*       _LoadBlockCnt
*
*   Function description
*     Reads the block count from the static spare area buffer.
*/
static unsigned _LoadBlockCnt(const NAND_UNI_INST * pInst) {
  unsigned BlockCnt;
  unsigned Off;

  Off = pInst->OffBlockTypeCnt;
  BlockCnt = (unsigned)_pSpareAreaData[Off] & 0xFu;
  return BlockCnt;
}

/*********************************************************************
*
*       _StoreEraseCnt
*
*  Function description
*    Stores the erase count in the static spare area buffer.
*/
static void _StoreEraseCnt(const NAND_UNI_INST * pInst, U32 EraseCnt) {
  unsigned Off;

  Off = pInst->OffEraseCnt;
  FS_StoreU32BE(_pSpareAreaData + Off, EraseCnt);
}

/*********************************************************************
*
*       _LoadEraseCnt
*
*  Function description
*    Reads the erase count from the static spare area buffer.
*/
static U32 _LoadEraseCnt(const NAND_UNI_INST * pInst) {
  U32      EraseCnt;
  unsigned Off;

  Off = pInst->OffEraseCnt;
  EraseCnt = FS_LoadU32BE(_pSpareAreaData + Off);
  return EraseCnt;
}

/*********************************************************************
*
*       _StoreLBI
*
*  Function description
*    Stores the logical block index in the static spare area buffer.
*/
static void _StoreLBI(const NAND_UNI_INST * pInst, unsigned lbi) {
  unsigned Off;

  Off = pInst->OffLBI;
  FS_StoreU16BE(_pSpareAreaData + Off, lbi);
}

/*********************************************************************
*
*       _LoadLBI
*
*  Function description
*    Reads the logical block index from the static spare area buffer.
*
*  Return value
*    Index of the logical block.
*/
static unsigned _LoadLBI(const NAND_UNI_INST * pInst) {
  unsigned Off;
  unsigned lbi;

  Off = pInst->OffLBI;
  lbi = FS_LoadU16BE(_pSpareAreaData + Off);
  return lbi;
}

/*********************************************************************
*
*       _StoreBRSI
*
*  Function description
*    Writes the block relative sector index into the static spare area buffer.
*/
static void _StoreBRSI(const NAND_UNI_INST * pInst, unsigned brsi) {
  unsigned Off;

  Off = pInst->OffBRSI;
  FS_StoreU16BE(_pSpareAreaData + Off, brsi);
}

/*********************************************************************
*
*       _LoadBRSI
*
*  Function description
*    Reads the block relative sector index from the static spare area buffer.
*/
static unsigned _LoadBRSI(const NAND_UNI_INST * pInst) {
  unsigned brsi;
  unsigned Off;

  Off  = pInst->OffBRSI;
  brsi = FS_LoadU16BE(_pSpareAreaData + Off);
  return brsi;
}

/*********************************************************************
*
*       _StoreBlockStat
*
*   Function description
*     Stores the block bad status in the static spare area buffer.
*/
static void _StoreBlockStat(const NAND_UNI_INST * pInst, unsigned BlockStat, unsigned OffInit) {
  unsigned Off;

  Off  = OffInit;
  Off += pInst->OffBlockStat;
  _pSpareAreaData[Off] = (U8)BlockStat;
}

#if FS_NAND_SUPPORT_DATA_CRC

/*********************************************************************
*
*       _StoreDataCRC
*
*  Function description
*    Stores the 32-bit CRC of the data in the static spare area buffer.
*/
static void _StoreDataCRC(const NAND_UNI_INST * pInst, U32 DataCRC) {
  unsigned Off;

  Off = pInst->OffDataCRC;
  FS_StoreU32BE(_pSpareAreaData + Off, DataCRC);
}

/*********************************************************************
*
*       _LoadDataCRC
*
*  Function description
*    Reads the 32-bit CRC of the data from the static spare area buffer.
*/
static U32 _LoadDataCRC(const NAND_UNI_INST * pInst) {
  U32      DataCRC;
  unsigned Off;

  Off = pInst->OffDataCRC;
  DataCRC = FS_LoadU32BE(_pSpareAreaData + Off);
  return DataCRC;
}

#endif // FS_NAND_SUPPORT_DATA_CRC

/*********************************************************************
*
*       _CorrectBlockStatIfRequired
*
*   Function description
*     Takes the bad/good status of a block and corrects its value if it has been affected by bit errors.
*
*/
static unsigned _CorrectBlockStatIfRequired(unsigned BlockStat) {
  unsigned NumBits;
  unsigned Data;
  unsigned i;

  //
  // If the block stat does not store one of the pre-defined values we will have to count bits to decide whether the block is good or bad.
  //
  if ((BlockStat != BLOCK_STAT_GOOD) && (BlockStat != BLOCK_STAT_BAD)) {
    //
    // Count the number of bits set to 1.
    //
    NumBits = 0;
    Data    = BlockStat;
    for (i = 0; i < 8u; ++i) {
      if ((Data & 1u) != 0u) {
        ++NumBits;
      }
      Data >>= 1;
    }
    //
    // The block good if more than half of the bits are set to 1.
    //
    if (NumBits > 4u) {
      BlockStat = BLOCK_STAT_GOOD;
    } else {
      BlockStat = BLOCK_STAT_BAD;
    }
  }
  return BlockStat;
}

/*********************************************************************
*
*       _StoreSectorStat
*
*   Function description
*     Writes to the static spare area buffer the information about whether the sector has been written or not.
*/
static void _StoreSectorStat(const NAND_UNI_INST * pInst, unsigned SectorStat) {
  unsigned Data;
  unsigned Off;

  Off = pInst->OffSectorStatMergeCnt;
  Data  = _pSpareAreaData[Off];
  Data &= ~0xFu;
  Data |= SectorStat & 0xFu;
  _pSpareAreaData[Off] = (U8)Data;
}

/*********************************************************************
*
*       _LoadSectorStat
*
*   Function description
*     Reads from the static spare area buffer the information about whether the sector has been written or not.
*/
static unsigned _LoadSectorStat(const NAND_UNI_INST * pInst) {
  unsigned SectorStat;
  unsigned Off;

  Off = pInst->OffSectorStatMergeCnt;
  SectorStat = (unsigned)_pSpareAreaData[Off] & 0xFu;
  return SectorStat;
}

/*********************************************************************
*
*       _StoreMergeCnt
*
*   Function description
*     Writes the merge count to static spare area buffer.
*/
static void _StoreMergeCnt(const NAND_UNI_INST * pInst, unsigned MergeCnt) {
  unsigned Data;
  unsigned Off;

  Off = pInst->OffSectorStatMergeCnt;
  Data  = _pSpareAreaData[Off];
  Data &= ~(0xFuL << 4);
  Data |= (MergeCnt & 0xFu) << 4;
  _pSpareAreaData[Off] = (U8)Data;
}

/*********************************************************************
*
*       _LoadMergeCnt
*
*   Function description
*     Reads the merge count from static spare area buffer.
*/
static unsigned _LoadMergeCnt(const NAND_UNI_INST * pInst) {
  unsigned MergeCnt;
  unsigned Off;

  Off = pInst->OffSectorStatMergeCnt;
  MergeCnt = ((unsigned)_pSpareAreaData[Off] >> 4) & 0xFu;
  return MergeCnt;
}

/*********************************************************************
*
*       _StoreNumSectors
*
*  Function description
*    Writes the number of sectors stored in the block into the static spare area buffer.
*/
static void _StoreNumSectors(const NAND_UNI_INST * pInst, unsigned NumSectors) {
  unsigned Off;

  Off = pInst->OffNumSectors;
  FS_StoreU16BE(_pSpareAreaData + Off, NumSectors);
}

#if FS_NAND_SUPPORT_BLOCK_GROUPING

/*********************************************************************
*
*       _LoadNumSectors
*
*  Function description
*    Reads the number of sectors stored in a block from the static spare area buffer.
*/
static unsigned _LoadNumSectors(const NAND_UNI_INST * pInst) {
  unsigned brsi;
  unsigned Off;

  Off  = pInst->OffNumSectors;
  brsi = FS_LoadU16BE(_pSpareAreaData + Off);
  return brsi;
}

#endif // FS_NAND_SUPPORT_BLOCK_GROUPING

/*********************************************************************
*
*       _BlockIndex2SectorIndex0
*
*  Function description
*    Returns the sector index of the first sector in a block.
*    With 128KB blocks and 2048 byte sectors, this means multiplying with 64.
*    With 128KB blocks and  512 byte sectors, this means multiplying with 256.
*/
static U32 _BlockIndex2SectorIndex0(const NAND_UNI_INST * pInst, unsigned BlockIndex) {
  U32 SectorIndex;

  SectorIndex = (U32)BlockIndex << pInst->PPB_Shift;
  return SectorIndex;
}

/*********************************************************************
*
*       _PhySectorIndex2PageIndex
*
*   Function description
*     Converts logical sectors (which can be 512 / 1024 / 2048 bytes) into
*     physical pages with same or larger page size.
*/
static U32 _PhySectorIndex2PageIndex(const NAND_UNI_INST * pInst, U32 PhySectorIndex, unsigned * pOff) {
  unsigned Off;
  U32      PageIndex;

  PageIndex  = PhySectorIndex;
  Off        = *pOff;
  Off       += pInst->BytesPerPage;        // Move offset from data to spare area
  *pOff      = Off;
  PageIndex += (U32)pInst->FirstBlock << pInst->PPB_Shift;
  return PageIndex;
}

/*********************************************************************
*
*       _LogSectorIndex2LogBlockIndex
*
*  Function description
*    Calculates the logical block index and the position in the block
*    of a specified logical sector.
*
*  Parameters
*    pInst          Driver instance.
*    SectorIndex    Index of logical sector.
*    pBRSI          [OUT] Position of the logical sector in the block.
*
*  Return value
*    Calculated LBI value.
*/
static unsigned _LogSectorIndex2LogBlockIndex(const NAND_UNI_INST * pInst, U32 SectorIndex, unsigned * pBRSI) {
  U32 lbi;
  U32 brsi;
  U32 SectorsPerBlock;

  brsi            = 0;
  SectorsPerBlock = 1uL << pInst->PPB_Shift;
  --SectorsPerBlock;      // Sector 0 is used only to store the erase count of the block.
  lbi = FS__DivModU32(SectorIndex, SectorsPerBlock, &brsi);
  if (pBRSI != NULL) {
    ++brsi;               // One sector more to take into account; the sector 0 is reserved for the erase count.
    *pBRSI = brsi;
  }
  return lbi;
}

/*********************************************************************
*
*       _L2P_Read
*
*  Function description
*    Returns the contents of the given entry in the L2P table (physical block lookup table)
*/
static unsigned _L2P_Read(const NAND_UNI_INST * pInst, U32 LogIndex) {
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
static void _L2P_Write(const NAND_UNI_INST * pInst, U32 LogIndex, unsigned v) {
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
static unsigned _L2P_GetSize(const NAND_UNI_INST * pInst) {
  unsigned v;

  v = FS_BITFIELD_CalcSize(pInst->NumLogBlocks, pInst->NumBitsPhyBlockIndex);
  return v;
}

/*********************************************************************
*
*       _ReadDataSpare
*
*   Function description
*     Reads all (or part of) the data area as well as all (or part of) the spare area.
*/
static int _ReadDataSpare(NAND_UNI_INST * pInst, U32 SectorIndex, void * pData, unsigned NumBytes, void * pSpare, unsigned NumBytesSpare) {   //lint -efunc(818, _ReadDataSpare) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory]. Rationale: the statistical counters stored in the driver instance are updated in debug builds.
  U32      PageIndex;
  unsigned OffData;
  unsigned OffSpare;
  int      r;
  U8       Unit;

  //
  // Increment statistics counter if enabled.
  //
  IF_STATS(pInst->StatCounters.ReadDataCnt++);
  IF_STATS(pInst->StatCounters.ReadSpareCnt++);
  Unit      = pInst->Unit;
  OffData   = 0;
  OffSpare  = 0;
  PageIndex = _PhySectorIndex2PageIndex(pInst, SectorIndex, &OffSpare);
  CALL_TEST_HOOK_DATA_READ_EX_BEGIN(Unit, PageIndex, pData, &OffData, &NumBytes, pSpare, &OffSpare, &NumBytesSpare);
  r = pInst->pPhyType->pfReadEx(Unit, PageIndex, pData, OffData, NumBytes, pSpare, OffSpare, NumBytesSpare);
  CALL_TEST_HOOK_DATA_READ_EX_END(Unit, PageIndex, pData, OffData, NumBytes, pSpare, OffSpare, NumBytesSpare, &r);
  return r;
}

/*********************************************************************
*
*       _ReadDataSpareEx
*
*   Function description
*     Reads all (or part of) the data area as well as all (or part of) the spare area.
*/
static int _ReadDataSpareEx(NAND_UNI_INST * pInst, U32 SectorIndex, void * pData, unsigned OffData, unsigned NumBytes, void * pSpare, unsigned OffSpare, unsigned NumBytesSpare) {    //lint -efunc(818, _ReadDataSpareEx) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory]. Rationale: the statistical counters stored in the driver instance are updated in debug builds.
  U32 PageIndex;
  int r;
  U8  Unit;

  //
  // Increment statistics counter if enabled.
  //
  IF_STATS(pInst->StatCounters.ReadDataCnt++);
  IF_STATS(pInst->StatCounters.ReadSpareCnt++);
  IF_STATS(pInst->StatCounters.ReadByteCnt += NumBytes);
  IF_STATS(pInst->StatCounters.ReadByteCnt += NumBytesSpare);
  Unit      = pInst->Unit;
  PageIndex = _PhySectorIndex2PageIndex(pInst, SectorIndex, &OffSpare);
  CALL_TEST_HOOK_DATA_READ_EX_BEGIN(Unit, PageIndex, pData, &OffData, &NumBytes, pSpare, &OffSpare, &NumBytesSpare);
  r = pInst->pPhyType->pfReadEx(Unit, PageIndex, pData, OffData, NumBytes, pSpare, OffSpare, NumBytesSpare);
  CALL_TEST_HOOK_DATA_READ_EX_END(Unit, PageIndex, pData, OffData, NumBytes, pSpare, OffSpare, NumBytesSpare, &r);
  return r;
}

/*********************************************************************
*
*       _ReadSpare
*
*   Function description
*     Reads all (or part of) the spare area.
*/
static int _ReadSpare(NAND_UNI_INST * pInst, U32 SectorIndex, void * pData, unsigned NumBytes) {      //lint -efunc(818, _ReadSpare) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory]. Rationale: the statistical counters stored in the driver instance are updated in debug builds.
  U32      PageIndex;
  unsigned Off;
  int      r;
  U8       Unit;

  Unit      = pInst->Unit;
  Off       = 0;
  PageIndex = _PhySectorIndex2PageIndex(pInst, SectorIndex, &Off);
#if (FS_NAND_OPTIMIZE_SPARE_AREA_READ == 0)
  IF_STATS(pInst->StatCounters.ReadSpareCnt++);   // Increment statistics counter if enabled
  IF_STATS(pInst->StatCounters.ReadByteCnt += NumBytes);
  CALL_TEST_HOOK_DATA_READ_BEGIN(Unit, PageIndex, pData, &Off, &NumBytes);
  r = pInst->pPhyType->pfRead(Unit, PageIndex, pData, Off, NumBytes);
  CALL_TEST_HOOK_DATA_READ_END(Unit, PageIndex, pData, Off, NumBytes, &r);
#else
  {
    int        ReadEntireSpareArea;
    unsigned   ActiveSpareAreaRanges;
    unsigned   BytesPerSpareArea;
    unsigned   BytesPerSpareStripe;
    U8       * pData8;
    unsigned   i;
    int        Result;

    BytesPerSpareArea     = pInst->BytesPerSpareArea;
    ReadEntireSpareArea   = 1;
    ActiveSpareAreaRanges = pInst->ActiveSpareAreaRanges;
    if (NumBytes == BytesPerSpareArea) {
      if (ActiveSpareAreaRanges != 0u) {
        ReadEntireSpareArea = 0;
      }
    }
    if (ReadEntireSpareArea != 0) {
      IF_STATS(pInst->StatCounters.ReadSpareCnt++);   // Increment statistics counter if enabled
      IF_STATS(pInst->StatCounters.ReadByteCnt += NumBytes);
      CALL_TEST_HOOK_DATA_READ_BEGIN(Unit, PageIndex, pData, &Off, &NumBytes);
      r = pInst->pPhyType->pfRead(Unit, PageIndex, pData, Off, NumBytes);
      CALL_TEST_HOOK_DATA_READ_END(Unit, PageIndex, pData, Off, NumBytes, &r);
    } else {
      FS_MEMSET(pData, 0xFF, NumBytes);
      r                   = 0;
      pData8              = SEGGER_PTR2PTR(U8, pData);
      NumBytes            = NUM_BYTES_SPARE_RANGE;
      BytesPerSpareStripe = pInst->BytesPerSpareStripe;
      Off    += OFF_SPARE_RANGE;
      pData8 += OFF_SPARE_RANGE;
      for (i = 0; i < MAX_NUM_SPARE_RANGES; ++i) {
        if ((ActiveSpareAreaRanges & (1uL << i)) != 0u) {
          IF_STATS(pInst->StatCounters.ReadSpareCnt++);   // Increment statistics counter if enabled
          IF_STATS(pInst->StatCounters.ReadByteCnt += NumBytes);
          CALL_TEST_HOOK_DATA_READ_BEGIN(Unit, PageIndex, pData8, &Off, &NumBytes);
          Result = pInst->pPhyType->pfRead(Unit, PageIndex, pData8, Off, NumBytes);
          CALL_TEST_HOOK_DATA_READ_END(Unit, PageIndex, pData, Off, NumBytes, &Result);
          if (Result > r) {
            r = Result;
          }
        }
        pData8 += BytesPerSpareStripe;
        Off    += BytesPerSpareStripe;
      }
    }
  }
#endif
  return r;
}

/*********************************************************************
*
*       _WriteDataSpare
*
*   Function description
*     Writes all of the data area as well as all of the spare area.
*     The important point here is this function performs both operations with a single call to the physical layer,
*     giving the physical layer a chance to perform the operation as one operation, which saves time.
*/
static int _WriteDataSpare(NAND_UNI_INST * pInst, U32 SectorIndex, const void * pData, unsigned NumBytes, const void * pSpare, unsigned NumBytesSpare) {    //lint -efunc(818, _WriteDataSpare) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory]. Rationale: the statistical counters stored in the driver instance are updated in debug builds.
  U32      PageIndex;
  unsigned OffData;
  unsigned OffSpare;
  int      r;
  U8       Unit;

  IF_STATS(pInst->StatCounters.WriteDataCnt++);                 // Increment statistics counter if enabled.
  IF_STATS(pInst->StatCounters.WriteByteCnt += NumBytes);
  IF_STATS(pInst->StatCounters.WriteByteCnt += NumBytesSpare);
  Unit      = pInst->Unit;
  OffData   = 0;
  OffSpare  = 0;
  PageIndex = _PhySectorIndex2PageIndex(pInst, SectorIndex, &OffSpare);
  CALL_TEST_HOOK_DATA_WRITE_EX_BEGIN(Unit, PageIndex, &pData, &OffData, &NumBytes, &pSpare, &OffSpare, &NumBytesSpare);
  r = pInst->pPhyType->pfWriteEx(Unit, PageIndex, pData, OffData, NumBytes, pSpare, OffSpare, NumBytesSpare);
  CALL_TEST_HOOK_DATA_WRITE_EX_END(Unit, PageIndex, pData, OffData, NumBytes, pSpare, OffSpare, NumBytesSpare, &r);
  return r;
}

/*********************************************************************
*
*       _CopyPage
*
*  Function description
*    Copies the contents of a page to an other page without transferring the page data to host.
*    Works only for NAND flashes with HW ECC.
*/
static int _CopyPage(const NAND_UNI_INST * pInst, U32 PageIndexSrc, U32 PageIndexDest) {
  U32 PageIndexFirst;
  int r;

  PageIndexFirst  = (U32)pInst->FirstBlock << pInst->PPB_Shift;
  PageIndexSrc   += PageIndexFirst;
  PageIndexDest  += PageIndexFirst;
  r = pInst->pPhyType->pfCopyPage(pInst->Unit, PageIndexSrc, PageIndexDest);
  return r;
}

/*********************************************************************
*
*       _ReadSpareEx
*
*  Function description
*    Reads (a part or all of) the spare area.
*
*  Parameters
*    pInst        [IN]  Driver instance.
*    SectorIndex  Physical index of the sector to read from.
*    pData        [OUT] Data read from the spare area.
*    Off          Byte offset in the spare area to start reading.
*    NumBytes     Number of bytes to read.
*
*  Return value
*    ==0    OK, data has been read.
*    !=0    An error occurred
*/
static int _ReadSpareEx(NAND_UNI_INST * pInst, U32 SectorIndex, void * pData, unsigned Off, unsigned NumBytes) {      //lint -efunc(818, _ReadSpareEx) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory]. Rationale: the statistical counters stored in the driver instance are updated in debug builds.
  U32 PageIndex;
  int r;
  U8  Unit;

  IF_STATS(pInst->StatCounters.ReadSpareCnt++);                 // Increment statistics counter if enabled.
  IF_STATS(pInst->StatCounters.ReadByteCnt += NumBytes);
  Unit      = pInst->Unit;
  PageIndex = _PhySectorIndex2PageIndex(pInst, SectorIndex, &Off);
  CALL_TEST_HOOK_DATA_READ_BEGIN(Unit, PageIndex, pData, &Off, &NumBytes);
  r = pInst->pPhyType->pfRead(Unit, PageIndex, pData, Off, NumBytes);
  CALL_TEST_HOOK_DATA_READ_END(Unit, PageIndex, pData, Off, NumBytes, &r);
  return r;
}

/*********************************************************************
*
*       _IsHW_ECCError
*
*  Function description
*    Checks if an uncorrectable error occurred during the last read operation.
*
*  Return values
*    ==0   No bit error correction occurred.
*    ==1   A bit error correction occurred.
*/
static int _IsHW_ECCError(const NAND_UNI_INST * pInst) {
  int                r;
  FS_NAND_ECC_RESULT eccResult;
  int                Result;

  r = 0;          // Set to indicate that no error occurred.
  FS_MEMSET(&eccResult, 0, sizeof(eccResult));
  Result = _GetHW_ECCResult(pInst, &eccResult);
  if (Result == 0) {
    if (eccResult.CorrectionStatus == FS_NAND_CORR_FAILURE) {
      r = 1;      // An uncorrectable error occurred.
    }
  }
  return r;
}

/*********************************************************************
*
*       _ReadSpareByte
*
*   Function description
*     Reads 1 byte of the spare area of the given page.
*/
static int _ReadSpareByte(NAND_UNI_INST * pInst, U32 SectorIndex, U8 * pData, unsigned Off) {
  U8  ab[2];
  int r;

  if ((pInst->DataBusWidth == 1u) || (pInst->DataBusWidth == 8u)) {
    r = _ReadSpareEx(pInst, SectorIndex, pData, Off, 1);
  } else {
    r = _ReadSpareEx(pInst, SectorIndex, ab, Off & 0xFEu, 2);       // Make sure we have 2-byte alignment required by 16-bit NAND flashes.
    *pData = ab[Off & 1u];
  }
  return r;
}

/*********************************************************************
*
*       _ReadSpareByteWithRetry
*
*   Function description
*     Reads 1 byte of the spare area of the given page.
*/
static int _ReadSpareByteWithRetry(NAND_UNI_INST * pInst, U32 SectorIndex, U8 * pData, unsigned Off) {
  int NumRetries;
  int r;

  NumRetries = FS_NAND_NUM_READ_RETRIES;
  for (;;) {
    r = _ReadSpareByte(pInst, SectorIndex, pData, Off);
    if (r == 0) {
      break;                          // OK, block status read successfully.
    }
    if (_IsHW_ECCError(pInst) != 0) {
      r = 0;                          // OK, false error. Typ. occurs when the HW ECC of the NAND flash cannot be disabled.
      break;
    }
    if (NumRetries-- == 0) {
      break;                          // Error, no more retries.
    }
  }
  return r;
}

/*********************************************************************
*
*       _MarkBlockAsFree
*
*  Function description
*    Mark block as free in management data.
*/
static void _MarkBlockAsFree(NAND_UNI_INST * pInst, unsigned iBlock) {        //lint -efunc(818, _MarkBlockAsFree) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory]. Rationale: the statistical counters stored in the driver instance are updated in debug builds.
  unsigned   Mask;
  U8       * pData;
  unsigned   Data;

  if (iBlock < pInst->NumBlocks) {
    Mask = 1uL << (iBlock & 7u);
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
static void _MarkBlockAsAllocated(NAND_UNI_INST * pInst, unsigned iBlock) {   //lint -efunc(818, _MarkBlockAsAllocated) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory]. Rationale: the statistical counters stored in the driver instance are updated in debug builds.
  unsigned   Mask;
  U8       * pData;
  unsigned   Data;

  if (iBlock < pInst->NumBlocks) {
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
static int _IsBlockFree(const NAND_UNI_INST * pInst, unsigned iBlock) {
  unsigned   Mask;
  U8       * pData;
  unsigned   Data;

  if (iBlock >= pInst->NumBlocks) {
    return 0;
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
*    Fills the entire static spare area with 0xFF.
*/
static void _ClearStaticSpareArea(const NAND_UNI_INST * pInst) {
  FS_MEMSET(_pSpareAreaData, 0xFF, pInst->BytesPerSpareArea);
}

#if ((FS_NAND_ENABLE_ERROR_RECOVERY != 0) && (FS_NAND_FILL_READ_BUFFER != 0))

/*********************************************************************
*
*       _SwapSpareAreaBufferIfRequired
*/
static void _SwapSpareAreaBufferIfRequired(void) {
  if (_IsERActive != 0u) {
    U8 * pData;

    //
    // Restore the buffers for the spare area.
    //
    pData             = _pSpareAreaData;
    _pSpareAreaData   = _pSpareAreaDataER;
    _pSpareAreaDataER = pData;
  }
}

#endif

#if FS_NAND_SUPPORT_DATA_CRC

/*********************************************************************
*
*       _CalcDataCRC
*
*  Function description
*    Calculates the 32-bit CRC of the data and stores it to
*    static spare area buffer.
*/
static void _CalcDataCRC(const NAND_UNI_INST * pInst, const U32 * pData) {
  U32      crc;
  unsigned NumBytes;

  NumBytes = pInst->BytesPerPage;
  crc = FS_CRC32_Calc(SEGGER_CONSTPTR2PTR(const U8, pData), NumBytes, DATA_CRC_INIT);
  _StoreDataCRC(pInst, crc);
}

/*********************************************************************
*
*       _VerifyDataCRC
*
*  Function description
*    Verifies the 32-bit CRC of the data. The original CRC is read
*    from the static spare area buffer.
*/
static int _VerifyDataCRC(const NAND_UNI_INST * pInst, const U32 * pData) {
  U32      crcRead;
  U32      crcCalc;
  unsigned NumBytes;

  crcRead  = _LoadDataCRC(pInst);
  NumBytes = pInst->BytesPerPage;
  crcCalc = FS_CRC32_Calc(SEGGER_CONSTPTR2PTR(const U8, pData), NumBytes, DATA_CRC_INIT);
  if (crcRead != crcCalc) {
    return 1;                 // CRC verification failed.
  }
  return 0;                   // CRC verification OK.
}

#endif // FS_NAND_SUPPORT_DATA_CRC

/*********************************************************************
*
*       _GetDataFillPattern
*
*  Function description
*    Returns the pattern that is used to fill unused sector data.
*
*  Notes
*    (1) Some NAND flash devices will wear out quicker than normal
*        if too many 0's are written to main or spare area.
*        Alternatively, witting 0's reduces the chance of bit errors.
*/
static U8 _GetDataFillPattern(const NAND_UNI_INST * pInst) {
  U8 Pattern;

  Pattern = 0x00;
  if (pInst->AllowBlankUnusedSectors != 0u) {
    Pattern = 0xFF;
  }
  return Pattern;
}

/*********************************************************************
*
*       _WriteSectorWithECC
*
*  Function description
*    Writes the contents of a sector and the ECC.
*
*  Return value
*    ==0      O.K., page data has been successfully written
*    !=0      Error
*
*  Additional information
*    The function performs the following:
*    - Computes ECC and stores it into static spare area
*    - Write entire sector & spare area into NAND flash (in one operations if possible)
*
*  Notes
*    (1) Before the function call, the static spare area needs to contain
*        info for the page (such as lbi, EraseCnt, etc)
*/
static int _WriteSectorWithECC(NAND_UNI_INST * pInst, const U32 * pBuffer, U32 SectorIndex) {
  int r;

#if FS_NAND_SUPPORT_DATA_CRC
  _CalcDataCRC(pInst, pBuffer);
#endif
  if (pInst->IsHW_ECCUsed == 0u) {            // Bit error correction performed in SW by the host?
    _CalcAndStoreECC(pInst, pBuffer, _pSpareAreaData);
  }
  r = _WriteDataSpare(pInst, SectorIndex, pBuffer, pInst->BytesPerPage, _pSpareAreaData, pInst->BytesPerSpareArea);
  return r;
}

/*********************************************************************
*
*       _MarkAsReadOnly
*
*  Function description
*    Marks the NAND flash device as read-only.
*/
static int _MarkAsReadOnly(NAND_UNI_INST * pInst, U16 ErrorType, U32 ErrorSectorIndex) {
  int   r;
  U8  * pPageBuffer;

  r = 0;          // Set to indicate success.
  //
  // Do not attempt to write to NAND flash if it is write protected.
  //
  if (pInst->IsWriteProtected == 0u) {
    FS_DEBUG_WARN((FS_MTYPE_DRIVER, "NAND_UNI: _MarkAsReadOnly: Moving permanently to read-only mode."));
    pInst->IsWriteProtected = 1;
    //
    // Save the write protected status and the error information into the first block
    //
    pPageBuffer = (U8 *)_pSectorBuffer;
    FS_MEMSET(pPageBuffer, 0xFF, pInst->BytesPerPage);
    FS_StoreU16BE(pPageBuffer + INFO_OFF_IS_WRITE_PROTECTED,       0);      // Inverted, 0 means write protected
    FS_StoreU16BE(pPageBuffer + INFO_OFF_HAS_FATAL_ERROR,          0);      // Inverted, 0 means has fatal error
    FS_StoreU16BE(pPageBuffer + INFO_OFF_FATAL_ERROR_TYPE,         ErrorType);
    FS_StoreU32BE(pPageBuffer + INFO_OFF_FATAL_ERROR_SECTOR_INDEX, ErrorSectorIndex);
    _ClearStaticSpareArea(pInst);
    r = _WriteSectorWithECC(pInst, _pSectorBuffer, SECTOR_INDEX_ERROR_INFO);
  }
  return r;
}

/*********************************************************************
*
*       _MarkAsReadOnlyIfAllowed
*
*  Function description
*    Performs the same operations as _MarkAsReadOnly() indirectly,
*    via a function pointer.
*/
static int _MarkAsReadOnlyIfAllowed(NAND_UNI_INST * pInst, U16 ErrorType, U32 ErrorSectorIndex) {
  int r;

  r = 1;                // Set to indicate failure.
  if (pInst->pWriteAPI != NULL) {
    r = pInst->pWriteAPI->pfMarkAsReadOnly(pInst, ErrorType, ErrorSectorIndex);
  }
  return r;
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
static void _OnFatalError(NAND_UNI_INST * pInst, int ErrorType, unsigned ErrorSectorIndex) {
  FS_NAND_FATAL_ERROR_INFO FatalErrorInfo;
  int                      MarkAsReadOnly;
  int                      Result;

  MarkAsReadOnly          = 0;        // Per default, leave the NAND flash writable.
  pInst->HasFatalError    = 1;
  pInst->ErrorType        = (U8)ErrorType;
  pInst->ErrorSectorIndex = ErrorSectorIndex;
  FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_UNI: _OnFatalError: Error %d occurred on sector %u.", ErrorType, ErrorSectorIndex));
  if (_pfOnFatalError != NULL) {
    //
    // Invoke the callback if registered.
    //
    FS_MEMSET(&FatalErrorInfo, 0, sizeof(FatalErrorInfo));
    FatalErrorInfo.Unit             = pInst->Unit;
    FatalErrorInfo.ErrorType        = (U8)ErrorType;
    FatalErrorInfo.ErrorSectorIndex = ErrorSectorIndex;
    Result = _pfOnFatalError(&FatalErrorInfo);
    if (Result == 0) {                // Did application request to mark the NAND flash as read-only?
      MarkAsReadOnly = 1;
    }
  }
  //
  // If requested, mark the NAND flash as read-only.
  //
  if (MarkAsReadOnly != 0) {
    (void)_MarkAsReadOnlyIfAllowed(pInst, (U16)ErrorType, ErrorSectorIndex);
  }
}

/*********************************************************************
*
*       _IsRelocationRequired
*
*  Function description
*    Checks if too many bit errors have accumulated in the last read page.
*    If so, the data of the entire block has to be relocated.
*/
static int _IsRelocationRequired(const NAND_UNI_INST * pInst, unsigned MaxNumBitsCorrected) {
  int r;

  r = 0;
#if FS_NAND_MAX_BIT_ERROR_CNT
  {
    unsigned           MaxBitErrorCnt;
    FS_NAND_ECC_RESULT eccResult;
    int                Result;
    int                IsHW_ECCUsed;

    IsHW_ECCUsed   = (int)pInst->IsHW_ECCUsed;
    MaxBitErrorCnt = pInst->MaxBitErrorCnt;
    if (MaxBitErrorCnt != 0u) {           // Feature enabled?
      if (MaxNumBitsCorrected == 0u) {    // Do we have to determine here the maximum number of bit errors corrected?
        if (IsHW_ECCUsed != 0) {          // This information is available only when HW ECC is used.
          //
          // Read from device the number of error bits corrected
          // during the last read operation.
          //
          FS_MEMSET(&eccResult, 0, sizeof(eccResult));
          Result = _GetHW_ECCResult(pInst, &eccResult);
          if (Result == 0) {
            if (eccResult.CorrectionStatus == FS_NAND_CORR_APPLIED) {   // Do we have corrected bit errors?
              if (eccResult.MaxNumBitsCorrected != 0u) {
                MaxNumBitsCorrected = eccResult.MaxNumBitsCorrected;
              } else {
                //
                // Some NAND flash devices are not able to report the actual number of bits corrected.
                // Force a relocation by setting the number of bit errors corrected to the actual threshold.
                //
                MaxNumBitsCorrected = MaxBitErrorCnt;
              }
              FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NAND_UNI: ECC_CORR_APPLIED MaxNumBitsCorrected: %d\n", eccResult.MaxNumBitsCorrected));
            }
          }
        }
      }
      if (MaxNumBitsCorrected >= MaxBitErrorCnt) {
        r = 1;                            // Bit errors where corrected which means that the block containing the page has to be relocated.
      }
    }
  }
#else
  FS_USE_PARA(pInst);
  FS_USE_PARA(MaxNumBitsCorrected);
#endif // FS_NAND_MAX_BIT_ERROR_CNT
  return r;
}

/*********************************************************************
*
*       _IsDataSpareBlankEx
*/
static int _IsDataSpareBlankEx(const NAND_UNI_INST * pInst, const U32 * pData, const U8 * pSpare, unsigned NumBitsCorrectable) {
  unsigned    BytesPerPage;
  unsigned    BytesPerSpareArea;
  const U32 * p;
  unsigned    NumItems;
  unsigned    NumBits0;
  U32         Data32;
  int         IsBlank;

  IsBlank           = 0;            // Assume the page not blank.
  BytesPerPage      = pInst->BytesPerPage;
  BytesPerSpareArea = pInst->BytesPerSpareArea;
  //
  // Check if the data area is blank.
  //
  p        = pData;
  NumItems = BytesPerPage >> 2;
  NumBits0 = 0;
  do {
    Data32 = *p++;
    if (Data32 != 0xFFFFFFFFuL) {
      NumBits0 += _Count0Bits(Data32);
    }
  } while (--NumItems != 0u);
  //
  // Check if the spare area is blank.
  //
  p        = SEGGER_CONSTPTR2PTR(const U32, pSpare);
  NumItems = BytesPerSpareArea >> 2;
  do {
    Data32 = *p++;
    if (Data32 != 0xFFFFFFFFuL) {
      NumBits0 += _Count0Bits(Data32);
    }
  } while (--NumItems != 0u);
  if (NumBits0 <= NumBitsCorrectable) {
    IsBlank = 1;                  // Data and spare area are blank.
  }
  return IsBlank;
}

/*********************************************************************
*
*       _IsDataSpareBlank
*/
static int _IsDataSpareBlank(const NAND_UNI_INST * pInst, const U32 * pData, const U8 * pSpare) {
  int      IsBlank;
  unsigned NumBitsCorrectable;

  NumBitsCorrectable = pInst->NumBitsCorrectable;
  IsBlank = _IsDataSpareBlankEx(pInst, pData, pSpare, NumBitsCorrectable);
  return IsBlank;
}

/*********************************************************************
*
*       _pbi2lbi
*
*  Function description
*    Finds out which logical block index is assigned to a data block.
*
*  Parameters
*    pInst    Driver instance.
*    pbi      Index of the data block.
*
*  Return value
*    ==LBI_INVALID    The logical block index is not assigned to any data block.
*    !=LBI_INVALID    The index of the logical block.
*/
static unsigned _pbi2lbi(const NAND_UNI_INST * pInst, unsigned pbi) {
  unsigned lbi;
  unsigned pbiToCheck;

  for (lbi = 0; lbi < pInst->NumLogBlocks; ++lbi) {
    pbiToCheck = _L2P_Read(pInst, lbi);
    if (pbi == pbiToCheck) {
      return lbi;
    }
  }
  return LBI_INVALID;
}

/*********************************************************************
*
*       _ReadSectorWithECCEx
*
*  Function description
*    Reads the contents of a sector and corrects bit errors.
*
*   Parameters
*     pInst         [IN]  Driver instance.
*     pBuffer       [OUT] Contents of the read sector.
*     SectorIndex   Index of the sector to read.
*     Off           Byte offset to read from.
*     NumBytes      Number of bytes to read from sector.
*                   If 0 the function reads BytesPerPage bytes.
*
*  Additional information
*    The function performs the following operations:
*    - Reads the data of a page into the specified buffer and spare area into the static buffer
*    - Performs error correction on the data
*
*  Return value
*    RESULT_NO_ERROR                    OK, no bit errors detected
*    RESULT_BIT_ERRORS_CORRECTED        OK, bit errors were corrected but the data is OK
*    RESULT_BIT_ERROR_IN_ECC            OK, a single bit error was detected in the ECC
*    RESULT_UNCORRECTABLE_BIT_ERRORS    Error
*    RESULT_READ_ERROR                  Error
*/
static int _ReadSectorWithECCEx(NAND_UNI_INST * pInst, U32 * pBuffer, U32 SectorIndex, unsigned Off, unsigned NumBytes) {
  int      r;
  int      NumRetries;
  unsigned BytesPerPage;
  unsigned BytesPerSpareArea;
  int      IsHW_ECCUsed;
  unsigned MaxNumBitsCorrected;

  IsHW_ECCUsed      = (int)pInst->IsHW_ECCUsed;
  BytesPerPage      = pInst->BytesPerPage;
  BytesPerSpareArea = pInst->BytesPerSpareArea;
  NumRetries        = FS_NAND_NUM_READ_RETRIES;
  if (NumBytes == 0u) {
    Off      = 0;
    NumBytes = BytesPerPage;
  }
  for (;;) {
    //
    // Read data and the entire spare area.
    //
    r = _ReadDataSpareEx(pInst, SectorIndex, pBuffer, Off, NumBytes, _pSpareAreaData, 0, BytesPerSpareArea);
    if (r != 0) {
      r = RESULT_READ_ERROR;                          // Re-read the sector in case of an error.
    } else {
      //
      // OK, read operation succeeded. If the NAND flash has an ECC engine
      // and it is active no data correction has to be performed in the software.
      //
      if (IsHW_ECCUsed != 0) {                        // Bit correction performed by the NAND flash?
        r = RESULT_NO_ERROR;
        if (_IsRelocationRequired(pInst, 0) != 0) {
          FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NAND_UNI: RELOCATION_REQUIRED SectorIndex: %lu, BlockIndex: %lu\n", SectorIndex, SectorIndex >> pInst->PPB_Shift));
          r = RESULT_BIT_ERRORS_CORRECTED;            // Bit errors where corrected and the block containing the page has to be relocated.
        }
#if FS_NAND_SUPPORT_DATA_CRC
        {
          int rCRC;
          int IsBlank;

          IsBlank = _IsDataSpareBlank(pInst, pBuffer, _pSpareAreaData);
          if (IsBlank == 0) {
            rCRC = _VerifyDataCRC(pInst, pBuffer);         // Check the CRC of the data.
            if (rCRC != 0) {
              FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NAND_UNI: CRC_FAILURE SectorIndex: %lu\n", SectorIndex));
              r = RESULT_UNCORRECTABLE_BIT_ERRORS;
              goto Retry;
            }
          }
        }
#endif // FS_NAND_SUPPORT_DATA_CRC
        return r;
      }
      //
      // Check and correct bit errors of data and spare area.
      //
      MaxNumBitsCorrected = 0;
      r = _ApplyECC(pInst, pBuffer, _pSpareAreaData, &MaxNumBitsCorrected);
#if FS_NAND_SUPPORT_DATA_CRC
      if (r != RESULT_UNCORRECTABLE_BIT_ERRORS) {
        int rCRC;
        int IsBlank;

        IsBlank = _IsDataSpareBlank(pInst, pBuffer, _pSpareAreaData);
        if (IsBlank == 0) {
          rCRC = _VerifyDataCRC(pInst, pBuffer);         // Check the CRC of the data.
          if (rCRC != 0) {
            FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NAND_UNI: CRC_FAILURE SectorIndex: %lu\n", SectorIndex));
            r = RESULT_UNCORRECTABLE_BIT_ERRORS;
            goto Retry;
          }
        }
      }
#endif // FS_NAND_SUPPORT_DATA_CRC
      if (r == RESULT_NO_ERROR) {
        return r;                                     // OK, data read.
      }
      if (r == RESULT_BIT_ERRORS_CORRECTED) {
        if (_IsRelocationRequired(pInst, MaxNumBitsCorrected) == 0) {
          r = RESULT_NO_ERROR;                        // The number of bit errors corrected is below the threshold. No relocation is necessary.
        } else {
          //
          // OK, data relocated.
          //
          FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NAND_UNI: RELOCATION_REQUIRED SectorIndex: %lu, BlockIndex: %lu\n", SectorIndex, SectorIndex >> pInst->PPB_Shift));
        }
        return r;                                     // OK, data read.
      }
    }
#if FS_NAND_SUPPORT_DATA_CRC
Retry:
#endif
    if (NumRetries-- == 0) {
      break;                                          // No more retries.
    }
    FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NAND_UNI: READ_DATA_WITH_ECC SectorIndex: %lu, Retries: %d/%d, r: %d\n", SectorIndex, NumRetries, FS_NAND_NUM_READ_RETRIES, r));
    IF_STATS(pInst->StatCounters.NumReadRetries++);
  }
  return r;
}

/*********************************************************************
*
*       _ReadSectorWithECC
*
*  Function description
*    Reads the contents of a sector and corrects bit errors.
*
*  Parameters
*    pInst          [IN]  Driver instance.
*    pBuffer        [OUT] Contents of the read sector.
*    SectorIndex    Index of the sector to read.
*
*  Additional information
*    The function performs the following operations:
*    - Reads the data of a page into the specified buffer and spare area into the static buffer
*    - Performs error correction on the data
*
*  Return value
*    RESULT_NO_ERROR                    OK, no bit errors detected
*    RESULT_BIT_ERRORS_CORRECTED        OK, bit errors were corrected but the data is OK
*    RESULT_BIT_ERROR_IN_ECC            OK, a single bit error was detected in the ECC
*    RESULT_UNCORRECTABLE_BIT_ERRORS    Error
*    RESULT_READ_ERROR                  Error
*/
static int _ReadSectorWithECC(NAND_UNI_INST * pInst, U32 * pBuffer, U32 SectorIndex) {
  int r;

  r = _ReadSectorWithECCEx(pInst, pBuffer, SectorIndex, 0, 0);
  return r;
}

/*********************************************************************
*
*       _ReadSpareAreaWithECC
*
*  Function description
*    Reads the data of the spare area into the static buffer and performs
*    error correction on the data. The page data is also read when the ECC
*    is computed on MCU side.
*
*  Parameters
*    pInst          [IN]  Driver instance.
*    SectorIndex    Index of the sector to read.
*
*  Return value
*    RESULT_NO_ERROR                    OK
*    RESULT_BIT_ERRORS_CORRECTED        OK
*    RESULT_BIT_ERROR_IN_ECC            OK
*    RESULT_UNCORRECTABLE_BIT_ERRORS    Error
*    RESULT_READ_ERROR                  Error
*/
static int _ReadSpareAreaWithECC(NAND_UNI_INST * pInst, U32 SectorIndex) {
  int        r;
  int        NumRetries;
  unsigned   BytesPerPage;
  unsigned   BytesPerSpareArea;
  int        IsHW_ECCUsed;
  unsigned   MaxNumBitsCorrected;
  int        IsSpareDataECCUsed;
  U32      * pSectorBuffer;
  U8       * pSpareAreaData;

  IsHW_ECCUsed       = (int)pInst->IsHW_ECCUsed;
  IsSpareDataECCUsed = (int)pInst->IsSpareDataECCUsed;
  BytesPerSpareArea  = pInst->BytesPerSpareArea;
  BytesPerPage       = pInst->BytesPerPage;
  NumRetries         = FS_NAND_NUM_READ_RETRIES;
  for (;;) {
    if (IsHW_ECCUsed != 0) {
      r = _ReadSpare(pInst, SectorIndex, _pSpareAreaData, BytesPerSpareArea);
      if (r == 0) {
        r = RESULT_NO_ERROR;
        if (_IsRelocationRequired(pInst, 0) != 0) {
          FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NAND_UNI: RELOCATION_REQUIRED SectorIndex: %lu, BlockIndex: %lu\n", SectorIndex, SectorIndex >> pInst->PPB_Shift));
          r = RESULT_BIT_ERRORS_CORRECTED;              // Bit errors where corrected and the block containing the page has to be relocated.
        }
        return r;
      }
      r = RESULT_READ_ERROR;                            // Re-read the spare area in case of an error.
    } else {
      pSpareAreaData = _pSpareAreaData;
      if (IsSpareDataECCUsed != 0) {
        //
        // Read only the spare area.
        //
        pSectorBuffer = NULL;
        r = _ReadSpare(pInst, SectorIndex, pSpareAreaData, BytesPerSpareArea);
      } else {
        //
        // Read data and the entire spare area.
        //
        pSectorBuffer = _pSectorBuffer;
        r = _ReadDataSpare(pInst, SectorIndex, pSectorBuffer, BytesPerPage, pSpareAreaData, BytesPerSpareArea);
      }
      if (r != 0) {
        r = RESULT_READ_ERROR;                          // Re-read the sector in case of an error.
      } else {
        //
        // Check and correct bit errors of data and spare area.
        //
        MaxNumBitsCorrected = 0;
        r = _ApplyECC(pInst, pSectorBuffer, pSpareAreaData, &MaxNumBitsCorrected);
        if (r == RESULT_NO_ERROR) {
          return r;                                     // OK, data read.
        }
        if (r == RESULT_BIT_ERRORS_CORRECTED) {
          if (_IsRelocationRequired(pInst, MaxNumBitsCorrected) == 0) {
            r = RESULT_NO_ERROR;                        // The number of bit errors corrected is below the threshold. No relocation is necessary.
          } else {
            //
            // OK, data relocated.
            //
            FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NAND_UNI: RELOCATION_REQUIRED SectorIndex: %lu, BlockIndex: %lu\n", SectorIndex, SectorIndex >> pInst->PPB_Shift));
          }
          return r;                                     // OK, data read.
        }
      }
    }
    if (NumRetries-- == 0) {
      break;                                            // No more retries.
    }
    FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NAND_UNI: READ_SPARE_WITH_ECC SectorIndex: %lu, Retries: %d/%d, r: %d\n", SectorIndex, NumRetries, FS_NAND_NUM_READ_RETRIES, r));
    IF_STATS(pInst->StatCounters.NumReadRetries++);
  }
  return r;
}

/*********************************************************************
*
*       _ReadSectorWithECCAndErrorHandling
*
*   Function description
*     Similar to _ReadSectorWithECC(). In addition, in case of a bit
*     error, the NAND flash is put permanently in read-only mode.
*/
static int _ReadSectorWithECCAndErrorHandling(NAND_UNI_INST * pInst, U32 * pBuffer, U32 SectorIndex) {
  int r;

  r = _ReadSectorWithECC(pInst, pBuffer, SectorIndex);
  if ((r == RESULT_READ_ERROR) || (r == RESULT_UNCORRECTABLE_BIT_ERRORS)) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_UNI: FATAL error: Could not read sector %lu with ECC.", SectorIndex));
    _OnFatalError(pInst, r, SectorIndex);
  }
  return r;
}

/*********************************************************************
*
*       _ReadSectorWithECCAndER_Ex
*
*   Function description
*     Similar to _ReadSectorWithECC(). In addition, it performs error
*     recovery. In case of a bit error the function tries to get
*     corrected sector data via the read error callback. If no callback
*     is registered the NAND flash is put permanently in read-only mode.
*
*   Parameters
*     pInst         [IN]  Driver instance.
*     pBuffer       [OUT] Contents of the read sector.
*     SectorIndex   Index of the sector to read.
*     brsi          Block relative sector index of the logical sector to be read.
*                   It is required when performing recovery in case of a read error.
*     Off           Byte offset to read from.
*     NumBytes      Number of bytes to read from sector.
*
*   Return value
*     RESULT_NO_ERROR                   OK
*     RESULT_BIT_ERRORS_CORRECTED       OK
*     RESULT_BIT_ERROR_IN_ECC           OK
*     RESULT_DATA_RECOVERED             OK
*     RESULT_UNCORRECTABLE_BIT_ERRORS   Error
*     RESULT_READ_ERROR                 Error
*
*   Notes
*     The calling function has to make sure the static spare area contains the information for the processed page.
*/
static int _ReadSectorWithECCAndER_Ex(NAND_UNI_INST * pInst, U32 * pBuffer, U32 SectorIndex, unsigned brsi, unsigned Off, unsigned NumBytes) {
  int r;

#if FS_NAND_ENABLE_ERROR_RECOVERY
  if (_IsERActive != 0u) {
    U8 * pData;

    //
    // Swap the buffers for the spare area to avoid overwriting data.
    //
    pData             = _pSpareAreaData;
    _pSpareAreaData   = _pSpareAreaDataER;
    _pSpareAreaDataER = pData;
  }
#endif
  r = _ReadSectorWithECCEx(pInst, pBuffer, SectorIndex, Off, NumBytes);
#if FS_NAND_ENABLE_ERROR_RECOVERY
  if (_IsERActive != 0u) {
    U8 * pData;

    //
    // Restore the buffers for the spare area.
    //
    pData             = _pSpareAreaData;
    _pSpareAreaData   = _pSpareAreaDataER;
    _pSpareAreaDataER = pData;
    if (r == RESULT_BIT_ERRORS_CORRECTED) {
      r = RESULT_NO_ERROR;              // Do not relocate the data during error recovery.
    }
  } else {
    //
    // Do not preform error recovery during the mount operation to prevent
    // that the NAND flash device is mounted twice if an read error occurs.
    //
    if (pInst->IsLLMounted != 0u) {
      if ((r == RESULT_READ_ERROR) || (r == RESULT_UNCORRECTABLE_BIT_ERRORS)) {
        int                   Result;
        FS_READ_ERROR_DATA  * pReadErrorData;
        U32                   LogSectorIndex;
        unsigned              pbi;
        unsigned              SectorsPerBlock;
        unsigned              lbi;
        U8                    Unit;
        int                   lbiFound;
        NAND_UNI_WORK_BLOCK * pWorkBlock;
        U32                 * pBufferRead;
        int                   IsPartialRead;
        U8                  * pData8;

        //
        // Read operation failed. Try to get corrected sector data from file system.
        //
        pReadErrorData = &pInst->ReadErrorData;
        if (pReadErrorData->pfCallback != NULL) {
          if (brsi != 0u) {         // The first page in a block does not store any sector data, so we skip it.
            //
            // Find the index of the logical block which stores the sector.
            //
            lbiFound = 0;
            pbi      = SectorIndex >> pInst->PPB_Shift;
            lbi      = 0;
            //
            // First, search in the list of work blocks.
            //
            pWorkBlock = pInst->pFirstWorkBlockInUse;
            while (pWorkBlock != NULL) {
              if (pWorkBlock->pbi == pbi) {
                lbi      = pWorkBlock->lbi;
                lbiFound = 1;
                break;
              }
              pWorkBlock = pWorkBlock->pNext;
            }
            if (lbiFound == 0) {
              //
              // Now, search in the list which maps a logical to a physical block.
              // This operation is potentially slow but it is performed only in
              // the rare event when a bit error occurs.
              //
              lbi = _pbi2lbi(pInst, pbi);
              if (lbi != LBI_INVALID) {
                lbiFound = 1;
              }
            }
            if (lbiFound != 0) {
              SectorsPerBlock = 1uL << pInst->PPB_Shift;
              Unit            = pInst->Unit;
              //
              // The first page in a block does not store any sector data, so we skip it.
              //
              --brsi;
              --SectorsPerBlock;
              //
              // Calculate the index of the logical sector to be read.
              //
              LogSectorIndex  = lbi * SectorsPerBlock;
              LogSectorIndex += brsi;
              //
              // Use the internal sector buffer if a partial read operation is performed.
              //
              pBufferRead   = pBuffer;
              IsPartialRead = 0;
              if ((NumBytes != 0u) && (NumBytes != pInst->BytesPerPage)) {
                IsPartialRead = 1;
              }
              if (IsPartialRead != 0) {
                pBufferRead = _pSectorBuffer;
              }
              //
              // Read the sector data from the file system.
              //
              _IsERActive = 1;
              Result = pReadErrorData->pfCallback(&FS_NAND_UNI_Driver, Unit, LogSectorIndex, pBufferRead, 1);
              _IsERActive = 0;
              if (Result == 0) {
                //
                // In case of a partial read operation, copy the data from the read buffer to the original buffer.
                //
                if (IsPartialRead != 0) {
                  pData8  = (U8 *)pBufferRead;
                  pData8 += Off;
                  FS_MEMCPY(pBuffer, pData8, NumBytes);
                }
                r = RESULT_DATA_RECOVERED;    // Received corrected sector data from file system.
              }
            }
          }
        }
      }
    }
  }
#else
  FS_USE_PARA(brsi);
#endif
  if ((r == RESULT_READ_ERROR) || (r == RESULT_UNCORRECTABLE_BIT_ERRORS)) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_UNI: FATAL error: Could not read sector %lu with ECC.", SectorIndex));
    _OnFatalError(pInst, r, SectorIndex);
  }
  return r;
}

/*********************************************************************
*
*       _ReadSectorWithECCAndER
*
*  Function description
*    Similar to _ReadSectorWithECC(). In addition, it performs error
*    recovery. In case of a bit error the function tries to get
*    corrected sector data via the read error callback. If no callback
*    is registered the NAND flash is put permanently in read-only mode.
*
*  Parameters
*    pInst          [IN]  Driver instance.
*    pBuffer        [OUT] Contents of the read sector.
*    SectorIndex    Index of the sector to read.
*    brsi           Block relative sector index of the logical sector to be read.
*                   It is required when performing recovery in case of a read error.
*
*  Return value
*    RESULT_NO_ERROR                    OK
*    RESULT_BIT_ERRORS_CORRECTED        OK
*    RESULT_BIT_ERROR_IN_ECC            OK
*    RESULT_DATA_RECOVERED              OK
*    RESULT_UNCORRECTABLE_BIT_ERRORS    Error
*    RESULT_READ_ERROR                  Error
*
*  Notes
*    (1) Before the function call, the static spare area needs to contain info for the page!
*/
static int _ReadSectorWithECCAndER(NAND_UNI_INST * pInst, U32 * pBuffer, U32 SectorIndex, unsigned brsi) {
  int r;

  r = _ReadSectorWithECCAndER_Ex(pInst, pBuffer, SectorIndex, brsi, 0, 0);
  return r;
}

#if FS_NAND_VERIFY_WRITE

/*********************************************************************
*
*       _VerifySector
*
*  Function description
*    Verifies the data stored to a sector.
*/
static int _VerifySector(NAND_UNI_INST * pInst, const U32 * pData, U32 SectorIndex) {
  int r;

  r = 0;                                // Set to indicate success.
  if (pInst->VerifyWrite != 0u) {
    r = _ReadSectorWithECC(pInst, _pVerifyBuffer, SectorIndex);
    if ((r == RESULT_NO_ERROR) || (r == RESULT_BIT_ERRORS_CORRECTED) || (r == RESULT_BIT_ERROR_IN_ECC)) {
      U32   NumItems;
      U32 * p;

      r        = 0;                     // Set to indicate success.
      NumItems = (unsigned)pInst->BytesPerPage / 4u;
      p        = _pVerifyBuffer;
      do {
        if (*p != *pData) {
          FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_UNI: Verify failed at sector %d.", SectorIndex));
          r = RESULT_VERIFY_ERROR;      // Verification failed.
          break;
        }
        ++p;
        ++pData;
      } while (--NumItems != 0u);
    }
  }
  return r;
}

#endif // FS_NAND_VERIFY_WRITE

/*********************************************************************
*
*       _ReadEraseCnt
*
*   Function description
*     Reads the erase count of a block from the spare area of the first page.
*
*   Return values
*     ==0   OK
*     !=0   An error occurred
*/
static int _ReadEraseCnt(NAND_UNI_INST * pInst, unsigned BlockIndex, U32 * pEraseCnt) {
  int r;
  U32 SectorIndex0;
  U32 EraseCnt;
  int Result;

  //
  // The erase count is stored in the spare area of the first page in the block.
  //
  Result       = 1;
  EraseCnt     = ERASE_CNT_INVALID;
  SectorIndex0 = _BlockIndex2SectorIndex0(pInst, BlockIndex);
  r = _ReadSpareAreaWithECC(pInst, SectorIndex0);
  if ((r == RESULT_NO_ERROR) || (r == RESULT_BIT_ERRORS_CORRECTED) || (r == RESULT_BIT_ERROR_IN_ECC)) {
    EraseCnt = _LoadEraseCnt(pInst);
    Result = 0;
  }
  *pEraseCnt = EraseCnt;
  return Result;
}

/*********************************************************************
*
*       _WriteEraseCnt
*
*  Function description
*    Writes the erase count of a block to the spare area of the first page.
*
*  Return values
*    ==0    OK
*    !=0    An error occurred.
*
*  Notes
*    (1) The function overwrites the contents of the sector buffer and of the static spare area.
*/
static int _WriteEraseCnt(NAND_UNI_INST * pInst, unsigned BlockIndex, U32 EraseCnt) {
  U32 SectorIndex0;
  int r;
  int Pattern;

  SectorIndex0 = _BlockIndex2SectorIndex0(pInst, BlockIndex);
  Pattern = (int)_GetDataFillPattern(pInst);
  FS_MEMSET(_pSectorBuffer, Pattern, pInst->BytesPerPage);
  _ClearStaticSpareArea(pInst);
  _StoreEraseCnt(pInst, EraseCnt);
  r = _WriteSectorWithECC(pInst, _pSectorBuffer, SectorIndex0);
#if FS_NAND_VERIFY_WRITE
  if (r == 0) {
    r = _VerifySector(pInst, _pSectorBuffer, SectorIndex0);
  }
#endif
  return r;
}

/*********************************************************************
*
*       _ReadBlockCnt
*
*   Function description
*     Reads the data count of a block from the spare area of the second page.
*
*   Return values
*     ==0   OK
*     !=0   An error occurred
*/
static int _ReadBlockCnt(NAND_UNI_INST * pInst, unsigned BlockIndex, unsigned * pBlockCnt) {
  int      r;
  U32      SectorIndex;
  unsigned BlockCnt;
  int      Result;

  Result      = 1;
  BlockCnt    = 0;
  SectorIndex = _BlockIndex2SectorIndex0(pInst, BlockIndex);
  //
  // The block count is stored in the spare area of the second sector in the block.
  //
  ++SectorIndex;
  r = _ReadSpareAreaWithECC(pInst, SectorIndex);
  if ((r == RESULT_NO_ERROR) || (r == RESULT_BIT_ERRORS_CORRECTED) || (r == RESULT_BIT_ERROR_IN_ECC)) {
    BlockCnt = _LoadBlockCnt(pInst);
    Result   = 0;
  }
  *pBlockCnt = BlockCnt;
  return Result;
}

/*********************************************************************
*
*       _ReadSectorStat
*
*   Function description
*     Reads the status of the data in the sector.
*
*   Return values
*     ==0   OK
*     !=0   An error occurred
*/
static int _ReadSectorStat(NAND_UNI_INST * pInst, U32 SectorIndex, unsigned * pSectorStat) {
  int      r;
  unsigned SectorStat;

  SectorStat  = SECTOR_STAT_EMPTY;
  r = _ReadSpareAreaWithECC(pInst, SectorIndex);
  if ((r == RESULT_NO_ERROR) || (r == RESULT_BIT_ERRORS_CORRECTED) || (r == RESULT_BIT_ERROR_IN_ECC)) {
    SectorStat = _LoadSectorStat(pInst);
  }
  *pSectorStat = SectorStat;
  return r;
}

#if (FS_NAND_SUPPORT_BLOCK_GROUPING != 0) || (FS_NAND_SUPPORT_FAST_WRITE != 0)

/*********************************************************************
*
*       _ReadMergeCnt
*
*   Function description
*     Reads the merge count from the spare are of a page.
*
*   Return values
*     ==0   OK
*     !=0   An error occurred
*/
static int _ReadMergeCnt(NAND_UNI_INST * pInst, U32 SectorIndex, unsigned * pMergeCnt) {
  int      r;
  unsigned MergeCnt;

  MergeCnt  = 0;
  r = _ReadSpareAreaWithECC(pInst, SectorIndex);
  if ((r == RESULT_NO_ERROR) || (r == RESULT_BIT_ERRORS_CORRECTED) || (r == RESULT_BIT_ERROR_IN_ECC)) {
    MergeCnt = _LoadMergeCnt(pInst);
  }
  *pMergeCnt = MergeCnt;
  return r;
}

#endif // (FS_NAND_SUPPORT_BLOCK_GROUPING != 0) || (FS_NAND_SUPPORT_FAST_WRITE != 0)

#if FS_NAND_SUPPORT_BLOCK_GROUPING

/*********************************************************************
*
*       _ReadBRSI
*
*  Function description
*    Reads the index of a sector relative to the beginning of the physical block.
*
*  Return values
*    ==0   OK
*    !=0   An error occurred
*
*  Notes
*    (1) This function overwrites the contents of the spare area buffer.
*/
static int _ReadBRSI(NAND_UNI_INST * pInst, U32 SectorIndex, unsigned * pBRSI) {
  int      r;
  unsigned brsi;
  int      Result;

  Result = 1;
  brsi   = BRSI_INVALID;
  r = _ReadSpareAreaWithECC(pInst, SectorIndex);
  if ((r == RESULT_NO_ERROR) || (r == RESULT_BIT_ERRORS_CORRECTED) || (r == RESULT_BIT_ERROR_IN_ECC)) {
    brsi   = _LoadBRSI(pInst);
    Result = 0;
  }
  *pBRSI = brsi;
  return Result;
}

/*********************************************************************
*
*       _ReadNumSectors
*
*  Function description
*    Reads the number of valid sectors from the second page of a work block.
*
*  Return values
*    ==0   OK
*    !=0   An error occurred
*
*  Notes
*    (1) This function overwrites the contents of the spare area buffer.
*/
static int _ReadNumSectors(NAND_UNI_INST * pInst, U32 SectorIndex, unsigned * pNumSectors) {
  int      r;
  unsigned NumSectors;
  int      Result;

  Result     = 1;
  NumSectors = NUM_SECTORS_INVALID;
  r = _ReadSpareAreaWithECC(pInst, SectorIndex);
  if ((r == RESULT_NO_ERROR) || (r == RESULT_BIT_ERRORS_CORRECTED) || (r == RESULT_BIT_ERROR_IN_ECC)) {
    NumSectors = _LoadNumSectors(pInst);
    Result     = 0;
  }
  *pNumSectors = NumSectors;
  return Result;
}

#endif // FS_NAND_SUPPORT_BLOCK_GROUPING

/*********************************************************************
*
*       _WB_ReadAssignment
*
*  Function description
*    Reads an entry in the assignment table of a work block.
*    It is necessary to use a subroutine to do the job since the entries are stored in a bit field.
*    Logically, the code does the following:
*      return pWorkBlock->aAssign[Index];
*/
static unsigned _WB_ReadAssignment(const NAND_UNI_INST * pInst, const NAND_UNI_WORK_BLOCK * pWorkBlock, unsigned Index) {
  unsigned r;

  r = FS_BITFIELD_ReadEntry(SEGGER_CONSTPTR2PTR(const U8, pWorkBlock->paAssign), Index, pInst->PPB_Shift);
  return r;
}

/*********************************************************************
*
*       _WB_WriteAssignment
*
*   Function description
*     Writes an entry in the assignment table of a work block.
*     It is necessary to use a subroutine to do the job since the entries are stored in a bit field.
*     Logically, the code does the following:
*       pWorkBlock->aAssign[Index] = v;
*/
static void _WB_WriteAssignment(const NAND_UNI_INST * pInst, const NAND_UNI_WORK_BLOCK * pWorkBlock, unsigned Index, unsigned v) {
  FS_BITFIELD_WriteEntry(SEGGER_PTR2PTR(U8, pWorkBlock->paAssign), Index, pInst->PPB_Shift, v);
}

/*********************************************************************
*
*       _WB_GetAssignmentSize
*
*  Function description
*    Returns the size of the assignment table of a work block.
*/
static unsigned _WB_GetAssignmentSize(const NAND_UNI_INST * pInst) {
  unsigned v;

  v = FS_BITFIELD_CalcSize(1uL << pInst->PPB_Shift, pInst->PPB_Shift);
  return v;
}

/*********************************************************************
*
*       _WB_RemoveFromList
*
*  Function description
*    Removes a given work block from list of work blocks.
*/
static void _WB_RemoveFromList(const NAND_UNI_WORK_BLOCK * pWorkBlock, NAND_UNI_WORK_BLOCK ** ppFirst) {
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL)
  {
    NAND_UNI_WORK_BLOCK * pWorkBlockToCheck;

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
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NAND_UNI: Work block is not contained in the list."));
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);
    }
  }
#endif
  //
  // Unlink Front: From head or previous block
  //
  if (pWorkBlock == *ppFirst) {           // This WB first in list ?
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
static void _WB_AddToList(NAND_UNI_WORK_BLOCK * pWorkBlock, NAND_UNI_WORK_BLOCK ** ppFirst) {
  NAND_UNI_WORK_BLOCK * pPrevFirst;

#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL)
  {
    NAND_UNI_WORK_BLOCK * pWorkBlockToCheck;

    //
    // Make sure that the work block is not already contained in the list.
    //
    pWorkBlockToCheck = *ppFirst;
    while (pWorkBlockToCheck != NULL) {
      if (pWorkBlockToCheck == pWorkBlock) {
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NAND_UNI: Work block is already contained in the list."));
        FS_X_PANIC(FS_ERRCODE_INVALID_PARA);
      }
      pWorkBlockToCheck = pWorkBlockToCheck->pNext;
    }
  }
#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
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
static void _WB_RemoveFromUsedList(NAND_UNI_INST * pInst, const NAND_UNI_WORK_BLOCK * pWorkBlock) {
  _WB_RemoveFromList(pWorkBlock, &pInst->pFirstWorkBlockInUse);
}

/*********************************************************************
*
*       _WB_AddToUsedList
*
*  Function description
*    Adds a given work block to the list of used work blocks.
*/
static void _WB_AddToUsedList(NAND_UNI_INST * pInst, NAND_UNI_WORK_BLOCK * pWorkBlock) {
  _WB_AddToList(pWorkBlock, &pInst->pFirstWorkBlockInUse);
}

/*********************************************************************
*
*       _WB_RemoveFromFreeList
*
*  Function description
*    Removes a given work block from list of free work blocks.
*/
static void _WB_RemoveFromFreeList(NAND_UNI_INST * pInst, const NAND_UNI_WORK_BLOCK * pWorkBlock) {
  _WB_RemoveFromList(pWorkBlock, &pInst->pFirstWorkBlockFree);
}

/*********************************************************************
*
*       _WB_AddToFreeList
*
*  Function description
*    Adds a given work block to the list of free work blocks.
*/
static void _WB_AddToFreeList(NAND_UNI_INST * pInst, NAND_UNI_WORK_BLOCK * pWorkBlock) {
  _WB_AddToList(pWorkBlock, &pInst->pFirstWorkBlockFree);
}

/*********************************************************************
*
*       _WB_HasValidSectors
*
*   Function description
*     Checks whether the work block contains at least one sector with valid data in it.
*/
static int _WB_HasValidSectors(const NAND_UNI_INST * pInst, const NAND_UNI_WORK_BLOCK * pWorkBlock) {
  unsigned SectorsPerBlock;
  unsigned iSector;
  unsigned brsiPhy;

  SectorsPerBlock = 1uL << pInst->PPB_Shift;
  //
  // Check the sectors in the work block one by one.
  //
  for (iSector = BRSI_BLOCK_INFO; iSector < SectorsPerBlock; ++iSector) {
    //
    // The assignment table tell us if a sector has valid data or not.
    //
    brsiPhy = _WB_ReadAssignment(pInst, pWorkBlock, iSector);
    if (brsiPhy != 0u) {
      return 1;     // Found one sector with valid data. Done.
    }
  }
  return 0;         // No sector with valid data found in the work block.
}

#if FS_NAND_SUPPORT_FAST_WRITE

/*********************************************************************
*
*       _DB_RemoveFromList
*
*  Function description
*    Removes a given data block from list of data blocks.
*/
static void _DB_RemoveFromList(const NAND_UNI_DATA_BLOCK * pDataBlock, NAND_UNI_DATA_BLOCK ** ppFirst) {
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL)
  {
    NAND_UNI_DATA_BLOCK * pDataBlockToCheck;

    //
    // Make sure that the data block is contained in the list.
    //
    pDataBlockToCheck = *ppFirst;
    while (pDataBlockToCheck != NULL) {
      if (pDataBlockToCheck == pDataBlock) {
        break;
      }
      pDataBlockToCheck = pDataBlockToCheck->pNext;
    }
    if (pDataBlockToCheck == NULL) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NAND_UNI: Data block is not contained in the list."));
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);
    }
  }
#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  //
  // Unlink Front: From head or previous block
  //
  if (pDataBlock == *ppFirst) {           // This DB first in list ?
    *ppFirst = pDataBlock->pNext;
  } else {
    pDataBlock->pPrev->pNext = pDataBlock->pNext;
  }
  //
  // Unlink next if pNext is valid
  //
  if (pDataBlock->pNext != NULL) {
    pDataBlock->pNext->pPrev = pDataBlock->pPrev;
  }
}

/*********************************************************************
*
*       _DB_AddToList
*
*  Function description
*    Adds a given data block to the beginning of the list of data block descriptors.
*/
static void _DB_AddToList(NAND_UNI_DATA_BLOCK * pDataBlock, NAND_UNI_DATA_BLOCK ** ppFirst) {
  NAND_UNI_DATA_BLOCK * pPrevFirst;

#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL)
  {
    NAND_UNI_DATA_BLOCK * pDataBlockToCheck;

    //
    // Make sure that the data block is not already contained in the list.
    //
    pDataBlockToCheck = *ppFirst;
    while (pDataBlockToCheck != NULL) {
      if (pDataBlockToCheck == pDataBlock) {
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NAND_UNI: Data block is already contained in the list."));
        FS_X_PANIC(FS_ERRCODE_INVALID_PARA);
      }
      pDataBlockToCheck = pDataBlockToCheck->pNext;
    }
  }
#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  pPrevFirst = *ppFirst;
  pDataBlock->pPrev = NULL;    // First entry
  pDataBlock->pNext = pPrevFirst;
  if (pPrevFirst != NULL) {
    pPrevFirst->pPrev = pDataBlock;
  }
  *ppFirst = pDataBlock;
}

/*********************************************************************
*
*       _DB_RemoveFromUsedList
*
*  Function description
*    Removes a given data block from list of used data blocks.
*/
static void _DB_RemoveFromUsedList(NAND_UNI_INST * pInst, const NAND_UNI_DATA_BLOCK * pDataBlock) {
  _DB_RemoveFromList(pDataBlock, &pInst->pFirstDataBlockInUse);
}

/*********************************************************************
*
*       _DB_AddToUsedList
*
*  Function description
*    Adds a given data block to the list of used data blocks.
*/
static void _DB_AddToUsedList(NAND_UNI_INST * pInst, NAND_UNI_DATA_BLOCK * pDataBlock) {
  _DB_AddToList(pDataBlock, &pInst->pFirstDataBlockInUse);
}

/*********************************************************************
*
*       _DB_RemoveFromFreeList
*
*  Function description
*    Removes a given data block from list of free data blocks.
*/
static void _DB_RemoveFromFreeList(NAND_UNI_INST * pInst, const NAND_UNI_DATA_BLOCK * pDataBlock) {
  _DB_RemoveFromList(pDataBlock, &pInst->pFirstDataBlockFree);
}

/*********************************************************************
*
*       _DB_AddToFreeList
*
*  Function description
*    Adds a given data block to the list of free data blocks.
*/
static void _DB_AddToFreeList(NAND_UNI_INST * pInst, NAND_UNI_DATA_BLOCK * pDataBlock) {
  _DB_AddToList(pDataBlock, &pInst->pFirstDataBlockFree);
}

#endif // FS_NAND_SUPPORT_FAST_WRITE

/*********************************************************************
*
*       _ReadBlockStatEx
*
*  Function description
*    Reads the status of a NAND block from a specified offset.
*
*  Parameters
*    pInst          Driver instance.
*    SectorIndex    Index of the physical page (sector) to read from.
*    pBlockStat     [OUT] Block status.
*    OffBlockStat   Location of the block status as byte offset relative
*                   to the beginning of the spare area.
*
*  Return values
*    ==0   No bit error correction occurred.
*    ==1   A bit error correction occurred.
*/
static int _ReadBlockStatEx(NAND_UNI_INST * pInst, unsigned SectorIndex, U8 * pBlockStat, unsigned OffBlockStat) {
  int      r;
  int      Result;
  unsigned PlanesPerOperation;
  unsigned BytesPerSpareArea;
  unsigned OffBlockStatNext;
  U8       BlockStat;
  U8       BlockStatRead;

  r                  = 0;                 // Set to indicate success.
  BlockStat          = BLOCK_STAT_GOOD;
  BlockStatRead      = BLOCK_STAT_GOOD;
  BytesPerSpareArea  = pInst->BytesPerSpareArea;
  PlanesPerOperation = 1uL << pInst->PPO_Shift;
  OffBlockStatNext   = BytesPerSpareArea >> pInst->PPO_Shift;
  //
  // Read the bad block marker from each physical page.
  //
  do {
    Result = _ReadSpareByteWithRetry(pInst, SectorIndex, &BlockStatRead, OffBlockStat);
    if (Result != 0) {
      r = Result;
    }
    BlockStat    &= BlockStatRead;       // We assume here that the good block marker has all the bits set to 1.
    OffBlockStat += OffBlockStatNext;
  } while (--PlanesPerOperation != 0u);
  if (pBlockStat != NULL) {
    *pBlockStat = BlockStat;
  }
  return r;
}

/*********************************************************************
*
*       _ReadBlockStat
*
*  Function description
*    Reads the status of a NAND block from the default location.
*
*  Parameters
*    pInst          Driver instance.
*    SectorIndex    Index of the physical page (sector) to read from.
*    pBlockStat     [OUT] Block status.
*
*  Return values
*    ==0   No bit error correction occurred.
*    ==1   A bit error correction occurred.
*/
static int _ReadBlockStat(NAND_UNI_INST * pInst, unsigned SectorIndex, U8 * pBlockStat) {
  unsigned OffBlockStat;
  int      r;

  OffBlockStat = pInst->OffBlockStat;
  r = _ReadBlockStatEx(pInst, SectorIndex, pBlockStat, OffBlockStat);
  return r;
}

/*********************************************************************
*
*       _IsPhyBlockBad
*
*  Function description
*    Checks if a physical block can be used to store data.
*
*  Parameters
*    pInst            Driver instance.
*    PhyBlockIndex    Index of the NAND block to be checked.
*
*  Return values
*    ==0   Block is good
*    !=0   Block is defective
*
*  Additional information
*    Since the block status is not protected by ECC we disable the HW ECC
*    when reading it to prevent that the read routine reports bit errors.
*/
static int _IsPhyBlockBad(NAND_UNI_INST * pInst, unsigned PhyBlockIndex) {
  int      r;
  U8       Data8;
  U32      SectorIndex;
  unsigned BlockStat0;
  unsigned BlockStat1;
  unsigned BlockStat2;
  unsigned BlockStat3;
  unsigned BPG_Shift;
  unsigned PPB_Shift;
  int      BadBlockMarkingType;
  U32      SectorsPerBlock;
  unsigned OffBlockStat;

  BlockStat0      = BLOCK_STAT_BAD;
  BlockStat1      = BLOCK_STAT_BAD;
  BlockStat2      = BLOCK_STAT_BAD;
  BlockStat3      = BLOCK_STAT_BAD;
  BPG_Shift       = _GetBPG_Shift(pInst);
  PPB_Shift       = pInst->PPB_Shift;
  SectorIndex     = (U32)PhyBlockIndex << (PPB_Shift - BPG_Shift);
  SectorsPerBlock = 1uL << (PPB_Shift - BPG_Shift);
  //
  // All bad block marking schemes set to 0 (or to a value different than 0xFF)
  // the first byte of the spare area in the first page of the block.
  //
  r = _ReadBlockStat(pInst, SectorIndex, &Data8);
  if (r == 0) {
    BlockStat0 = _CorrectBlockStatIfRequired(Data8);
  }
  BadBlockMarkingType = (int)pInst->BadBlockMarkingType;
  if (BadBlockMarkingType == FS_NAND_BAD_BLOCK_MARKING_TYPE_FPS) {
    //
    // This is the bad block marking scheme used by the majority of NAND flash devices.
    //
    BlockStat1 = BLOCK_STAT_GOOD;
    BlockStat2 = BLOCK_STAT_GOOD;
    BlockStat3 = BLOCK_STAT_GOOD;
  } else {
    if (BadBlockMarkingType == FS_NAND_BAD_BLOCK_MARKING_TYPE_FLPS) {
      //
      // The ONFI specification requires that a block is marked as defective
      // by setting to 0 the first byte in the first and last page of that block.
      //
      BlockStat2 = BLOCK_STAT_GOOD;
      BlockStat3 = BLOCK_STAT_GOOD;
      if (BlockStat0 == BLOCK_STAT_GOOD) {
        r = _ReadBlockStat(pInst, SectorIndex + (SectorsPerBlock - 1u), &Data8);
        if (r == 0) {
          BlockStat1 = _CorrectBlockStatIfRequired(Data8);
        }
      }
    } else {
      if (BadBlockMarkingType == FS_NAND_BAD_BLOCK_MARKING_TYPE_FSLPS) {
        //
        // The SkyHigh/Cypress/Spansion NAND flash devices mark a block as defective
        // in the first byte of the spare are of either the first, second or last
        // page in a block. Therefore, we have to check all these locations here.
        //
        BlockStat3 = BLOCK_STAT_GOOD;
        if (BlockStat0 == BLOCK_STAT_GOOD) {
          r = _ReadBlockStat(pInst, SectorIndex + 1u, &Data8);
          if (r == 0) {
            BlockStat1 = _CorrectBlockStatIfRequired(Data8);
          }
          if (BlockStat1 == BLOCK_STAT_GOOD) {
            r = _ReadBlockStat(pInst, SectorIndex + (SectorsPerBlock - 1u), &Data8);
            if (r == 0) {
              BlockStat2 = _CorrectBlockStatIfRequired(Data8);
            }
          }
        }
      } else {
        if (BadBlockMarkingType == FS_NAND_BAD_BLOCK_MARKING_TYPE_FLPMS) {
          //
          // GigaDevice marks a block as defective by setting to 0 the first byte
          // in the main and spare area of the first and last page in the block.
          // Because the first byte in the main area is reserved for the bad block
          // marker the physical layer is expected to physically store the first
          // data byte as the second byte in the spare area.
          //
          if (BlockStat0 == BLOCK_STAT_GOOD) {
            OffBlockStat = (unsigned)pInst->OffBlockStat + 1u;                // Following byte after the actual block status.
            r = _ReadBlockStatEx(pInst, SectorIndex, &Data8, OffBlockStat);
            if (r == 0) {
              BlockStat1 = _CorrectBlockStatIfRequired(Data8);
            }
            if (BlockStat1 == BLOCK_STAT_GOOD) {
              r = _ReadBlockStat(pInst, SectorIndex + (SectorsPerBlock - 1u), &Data8);
              if (r == 0) {
                BlockStat2 = _CorrectBlockStatIfRequired(Data8);
              }
              if (BlockStat2 == BLOCK_STAT_GOOD) {
                r = _ReadBlockStatEx(pInst, SectorIndex + (SectorsPerBlock - 1u), &Data8, OffBlockStat);
                if (r == 0) {
                  BlockStat3 = _CorrectBlockStatIfRequired(Data8);
                }
              }
            }
          }
        } else {
          //
          // Samsung marks a block as defective by setting to 0 the first byte/half-word
          // of the spare area of the first or second page. We check here the second page
          // to make sure that we detect a defective block. This is the behavior of the emFile
          // versions prior to and including 4.04e
          //
          BlockStat2 = BLOCK_STAT_GOOD;
          BlockStat3 = BLOCK_STAT_GOOD;
          if (BlockStat0 == BLOCK_STAT_GOOD) {
            r = _ReadBlockStat(pInst, SectorIndex + 1u, &Data8);
            if (r == 0) {
              BlockStat1 = _CorrectBlockStatIfRequired(Data8);
            }
          }
        }
      }
    }
  }
  if (   (BlockStat0 == BLOCK_STAT_GOOD)
      && (BlockStat1 == BLOCK_STAT_GOOD)
      && (BlockStat2 == BLOCK_STAT_GOOD)
      && (BlockStat3 == BLOCK_STAT_GOOD)) {
    return 0;                               // Block can be used to store data.
  }
  return 1;                                 // Block is bad.
}

/*********************************************************************
*
*       _IsBlockBad
*
*  Function description
*    Checks if a group of blocks can be used to store data.
*    The group can not be used for data storage when at least one block
*    in the group is marked as defective.
*
*  Parameters
*    pInst        Driver instance.
*    BlockIndex   Index of the block to be checked.
*
*  Return values
*    ==0    Block is good.
*    !=0    Block is defective.
*/
static int _IsBlockBad(NAND_UNI_INST * pInst, unsigned BlockIndex) {
  int      r;
  unsigned NumBlocks;
  unsigned PhyBlockIndex;
  unsigned BPG_Shift;
  int      IsBad;

  IsBad         = 0;
  BPG_Shift     = _GetBPG_Shift(pInst);
  PhyBlockIndex = (U32)BlockIndex << BPG_Shift;
  NumBlocks     = 1uL << BPG_Shift;         // Calculate the number of blocks in a group.
  (void)_DisableHW_ECCIfRequired(pInst);    // Temporarily disable the HW ECC during the data transfer.
  //
  // Check all the physical blocks in the group.
  //
  do {
    r = _IsPhyBlockBad(pInst, PhyBlockIndex);
    if (r != 0) {
      IsBad = 1;                            // The block is defective. The group of blocks can not be used for data storage.
      break;
    }
    ++PhyBlockIndex;
  } while (--NumBlocks != 0u);
  (void)_EnableHW_ECCIfRequired(pInst);     // Re-enable HW ECC if needed.
  return IsBad;
}

/*********************************************************************
*
*       _CanBlockBeMarkedAsBad
*
*  Function description
*    Checks if the error code allows a block to be marked as defective.
*
*  Additional information
*    Erase and write errors are always marked as defective by the driver
*    as all the NAND flash manufacturers request this. This feature is
*    configurable for the read and uncorrectable bit errors to accommodate
*    the requirements of each NAND flash manufacturer. The default is
*    to mark the block as defective on read or uncorrectable bit errors.
*    The feature can be disabled via the FS_NAND_UNI_AllowReadErrorBadBlocks()
*    API function of the driver.
*/
static int _CanBlockBeMarkedAsBad(const NAND_UNI_INST * pInst, int ErrorType) {
  int r;

  r = 0;                // Per default, do not mark a block as defective.
  if ((ErrorType == RESULT_WRITE_ERROR) || (ErrorType == RESULT_ERASE_ERROR)) {
    r = 1;              // Mark the block as defective in case of an erase or write error. All the NAND flash manufacturers request this.
  } else {
    if ((ErrorType == RESULT_UNCORRECTABLE_BIT_ERRORS) || (ErrorType == RESULT_READ_ERROR)) {
      r = 1;            // Set to mark the block as defective.
      if (pInst->AllowReadErrorBadBlocks == 0u) {
        r = 0;          // Do not mark the block as defective.
      }
    }
  }
  return r;
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
*    BlockIndex       Index of the group of blocks to be marked as defective.
*    ErrorType        Reason why the block is marked as defective.
*    ErrorBRSI        Index of the physical sector where the error occurred.
*/
static int _MarkBlockAsBad(NAND_UNI_INST * pInst, unsigned BlockIndex, int ErrorType, unsigned ErrorBRSI) {
  U32        SectorIndex;
  U8       * pSpare;
  unsigned   BytesPerPage;
  unsigned   BytesPerSpareArea;
  int        r;
  unsigned   ECCBlocksPerPage;
  unsigned   BytesPerSpareStripe;
  unsigned   SignatureOff;
  unsigned   ErrorTypeOff;
  unsigned   ErrorBRSIOff;
  unsigned   StatOff;
  unsigned   PPB_Shift;
  unsigned   BPG_Shift;
  int        Pattern;
  unsigned   PlanesPerOperation;
  unsigned   OffNext;

  IF_STATS(pInst->StatCounters.NumBadBlocks++);
  PPB_Shift           = pInst->PPB_Shift;
  BPG_Shift           = _GetBPG_Shift(pInst);
  BytesPerPage        = pInst->BytesPerPage;
  BytesPerSpareArea   = pInst->BytesPerSpareArea;
  ECCBlocksPerPage    = BytesPerPage >> pInst->ldBytesPerECCBlock;
  BytesPerSpareStripe = BytesPerSpareArea / ECCBlocksPerPage;
  //
  // Calculate the index of the first page in the defective physical block.
  //
  ErrorBRSI   &= (1uL << PPB_Shift) - 1u;                   // Limit the value of the relative sector index.
  SectorIndex  = _BlockIndex2SectorIndex0(pInst, BlockIndex);
  SectorIndex += ErrorBRSI;
  SectorIndex &= ~((1uL << (PPB_Shift - BPG_Shift)) - 1u);  // Get the index of the first sector in the phy. block.
  //
  // Save the information starting from offset 0 of the region 1, 2 and 3
  // of spare area which is not used for any management data.
  //
  SignatureOff = BytesPerSpareStripe * SPARE_STRIPE_INDEX_SIGNATURE  + OFF_SPARE_RANGE;
  ErrorTypeOff = BytesPerSpareStripe * SPARE_STRIPE_INDEX_ERROR_TYPE + OFF_SPARE_RANGE;
  ErrorBRSIOff = BytesPerSpareStripe * SPARE_STRIPE_INDEX_ERROR_BRSI + OFF_SPARE_RANGE;
  //
  // The signature is saved to a different stripe for the second and the following blocks
  // in a block group to make sure that we do not overwrite the sector status.
  //
  if ((SectorIndex & ((1uL << PPB_Shift) - 1u)) != 0u) {
    SignatureOff = BytesPerSpareStripe * SPARE_STRIPE_INDEX_SIGNATURE_ALT + OFF_SPARE_RANGE;
  }
  //
  // We write "SEGGER" on the spare area of the block
  // to be able to distinguish it from the "bad" blocks
  // marked by manufacturer.
  //
  (void)_ReadSpare(pInst, SectorIndex, _pSpareAreaData, BytesPerSpareArea);
  Pattern = (int)_GetDataFillPattern(pInst);
  FS_MEMSET(_pSectorBuffer, Pattern, BytesPerPage);
  PlanesPerOperation = 1uL << pInst->PPO_Shift;
  OffNext            = BytesPerSpareArea >> pInst->PPO_Shift;
  StatOff            = 0;
  //
  // Make sure that we write the signature to each physical page.
  //
  do {
    _StoreBlockStat(pInst, BLOCK_STAT_BAD, StatOff);
    pSpare = _pSpareAreaData;
    FS_MEMCPY(pSpare + SignatureOff, _acInfo, NUM_BYTES_BAD_BLOCK_SIGNATURE);
    FS_StoreU16BE(pSpare + ErrorTypeOff, (unsigned)ErrorType);
    FS_StoreU16BE(pSpare + ErrorBRSIOff, ErrorBRSI);
    SignatureOff += OffNext;
    ErrorTypeOff += OffNext;
    ErrorBRSIOff += OffNext;
    StatOff      += OffNext;
  } while (--PlanesPerOperation != 0u);
  //
  // It is not required to write the data with ECC since the information
  // whether a block is defective or not is always read with ECC disabled.
  //
  (void)_DisableHW_ECCIfRequired(pInst);
  r = _WriteDataSpare(pInst, SectorIndex, _pSectorBuffer, BytesPerPage, _pSpareAreaData, BytesPerSpareArea);
  (void)_EnableHW_ECCIfRequired(pInst);
  return r;
}

#if FS_SUPPORT_TEST

/*********************************************************************
*
*       _IsPBIAssignedToWorkBlockDesc
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
static int _IsPBIAssignedToWorkBlockDesc(unsigned pbi, const NAND_UNI_WORK_BLOCK * pWorkBlock) {
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
static int _IsPBIAssignedToDataBlock(const NAND_UNI_INST * pInst, unsigned pbi, unsigned lbiStart) {
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

#if FS_NAND_SUPPORT_FAST_WRITE

/*********************************************************************
*
*       _IsPBIAssignedToDataBlockDesc
*
*  Function description
*    Checks if the specified physical block is used as data block.
*
*  Parameters
*    pbi          Index of the physical block to be checked.
*    pDataBlock   First data block to be checked.
*
*  Return value
*    ==0      The block is not used as data block.
*    ==1      The block is used as data block.
*/
static int _IsPBIAssignedToDataBlockDesc(unsigned pbi, const NAND_UNI_DATA_BLOCK * pDataBlock) {
  while (pDataBlock != NULL) {
    if (pbi == pDataBlock->pbi) {
      return 1;
    }
    pDataBlock = pDataBlock->pNext;
  }
  return 0;
}

#endif // FS_NAND_SUPPORT_FAST_WRITE

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
static int _CheckConsistency(NAND_UNI_INST * pInst) {
  unsigned              lbi;
  unsigned              pbi;
  NAND_UNI_WORK_BLOCK * pWorkBlock;

  if (pInst->IsLLMounted == 0u) {
    return 0;                   // OK, NAND flash not mounted yet.
  }
  //
  // Check if all the PBIs of data blocks are marked as used.
  //
  for (lbi = 0; lbi < pInst->NumLogBlocks; ++lbi) {
    pbi = _L2P_Read(pInst, lbi);
    if (pbi != 0u) {
      if (_IsBlockFree(pInst, pbi) != 0) {
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_UNI: _CheckConsistency: Data block marked as free (pbi: %u)", pbi));
        return 1;               // Error, data block is marked as free.
      }
      if (_IsBlockBad(pInst, pbi) != 0) {
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_UNI: _CheckConsistency: Data block is marked as defective (pbi: %u)\n", pbi));
        return 1;               // Error, data block is marked as defective.
      }
      //
      // Check if the physical blocks that are assigned to work blocks are not assigned to a data block.
      //
      if (_IsPBIAssignedToWorkBlockDesc(pbi, pInst->pFirstWorkBlockInUse) != 0) {
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_UNI: _CheckConsistency: Work block used as data block (pbi: %u)", pbi));
        return 1;               // Error, work block is used as data block.
      }
      //
      // Check if the PBIs of data blocks are unique.
      //
      if (_IsPBIAssignedToDataBlock(pInst, pbi, lbi + 1u) != 0) {
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_UNI: _CheckConsistency: Duplicated data block found (pbi: %u)\n", pbi));
        return 1;               // Error, same physical block assigned to 2 data blocks.
      }
    }
  }
  //
  // Check if the PBIs of work blocks are marked as used.
  //
  pWorkBlock = pInst->pFirstWorkBlockInUse;       // Start with the first block in use.
  while (pWorkBlock != NULL) {
    pbi = pWorkBlock->pbi;
    if (_IsBlockFree(pInst, pbi) != 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_UNI: _CheckConsistency: Work block is marked as free (pbi: %u)\n", pbi));
      return 1;                 // Error, work block is marked as free.
    }
    if (_IsBlockBad(pInst, pbi) != 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_UNI: _CheckConsistency: Work block is marked as defective (pbi: %u)\n", pbi));
      return 1;                 // Error, work block is marked as defective.
    }
    pWorkBlock = pWorkBlock->pNext;
    //
    // Check if the PBIs of work blocks are unique.
    //
    if (_IsPBIAssignedToWorkBlockDesc(pbi, pWorkBlock) != 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_UNI: _CheckConsistency: Duplicated work block found (pbi: %u)\n", pbi));
      return 1;                 // Error, same physical block is assigned to 2 work blocks.
    }
  }
#if FS_NAND_SUPPORT_FAST_WRITE
  {
    NAND_UNI_DATA_BLOCK * pDataBlock;
    //
    // Check if the PBIs of "special" data blocks are marked as used.
    //
    pDataBlock = pInst->pFirstDataBlockInUse;       // Start with the first block in use.
    while (pDataBlock != NULL) {
      pbi = pDataBlock->pbi;
      if (_IsBlockFree(pInst, pbi) != 0) {
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_UNI: _CheckConsistency: Data block is marked as free (pbi: %u)\n", pbi));
        return 1;                 // Error, data block is marked as free.
      }
      if (_IsBlockBad(pInst, pbi) != 0) {
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_UNI: _CheckConsistency: Data block is marked as defective (pbi: %u)\n", pbi));
        return 1;                 // Error, data block is marked as defective.
      }
      pDataBlock = pDataBlock->pNext;
      //
      // Check if the PBIs of data blocks are unique.
      //
      if (_IsPBIAssignedToDataBlockDesc(pbi, pDataBlock) != 0) {
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_UNI: _CheckConsistency: Duplicated data block found (pbi: %u)\n", pbi));
        return 1;                 // Error, same physical block is assigned to 2 data blocks.
      }
    }
  }
#endif // FS_NAND_SUPPORT_FAST_WRITE
  return 0;                     // OK, no errors found.
}

#endif // FS_SUPPORT_TEST

/*********************************************************************
*
*       _IsPageBlank
*
*   Function description
*     Checks whether all bytes in page including the spare area are set to 0xFF.
*     The data is read without ECC since the read routine will switch the NAND flash
*     to read-only mode if an uncorrectable bit error is encountered.
*     It is possible that bit errors are present on the page (that is bits are set to 0).
*     The routine counts them and if the number is smaller than the number of bit errors
*     the ECC can correct the page is considered blank.
*/
static int _IsPageBlank(NAND_UNI_INST * pInst, U32 SectorIndex) {
  int      r;
  unsigned BytesPerPage;
  unsigned BytesPerSpareArea;
  int      NumRetries;
  int      IsBlank;

  IsBlank           = 0;                    // Consider the page not blank.
  BytesPerPage      = pInst->BytesPerPage;
  BytesPerSpareArea = pInst->BytesPerSpareArea;
  NumRetries        = FS_NAND_NUM_READ_RETRIES;
  (void)_DisableHW_ECCIfRequired(pInst);    // Temporarily disable the HW ECC during the data transfer.
  for (;;) {
    r = _ReadDataSpare(pInst, SectorIndex, _pSectorBuffer, BytesPerPage, _pSpareAreaData, BytesPerSpareArea);
    if (r != 0) {
      if (_IsHW_ECCError(pInst) != 0) {
        r = 0;                              // Ignore the read error generated by the HW ECC. This condition occurs when the HW ECC cannot be disabled.
      }
    }
    if (r == 0) {
      r = _IsDataSpareBlank(pInst, _pSectorBuffer, _pSpareAreaData);
      if (r != 0) {
        IsBlank = 1;
        break;
      }
    }
    if (NumRetries-- == 0) {
      break;                        // No more retries.
    }
    IF_STATS(pInst->StatCounters.NumReadRetries++);
  }
  (void)_EnableHW_ECCIfRequired(pInst);
  return IsBlank;
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
*    ==1    Block is blank
*    ==0    Block is not blank or an error occurred
*/
static int _IsBlockBlank(NAND_UNI_INST * pInst, unsigned BlockIndex) {
  U32 PagesPerBlock;
  U32 PageIndex;
  int r;

  PagesPerBlock = 1uL << pInst->PPB_Shift;
  PageIndex     = _BlockIndex2SectorIndex0(pInst, BlockIndex);
  //
  // Check each page in the block.
  //
  do {
    r = _IsPageBlank(pInst, PageIndex);
    if (r == 0) {
      return 0;                             // The block is not blank.
    }
    ++PageIndex;
  } while (--PagesPerBlock != 0u);
  return 1;                                 // All bytes in the block are set to 0xFF.
}

#endif  // FS_NAND_VERIFY_ERASE

/*********************************************************************
*
*       _EraseBlock
*
*  Function description
*    Erases a group of blocks (virtual block).
*/
static int _EraseBlock(NAND_UNI_INST * pInst, unsigned BlockIndex) {    //lint -efunc(818, _EraseBlock) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory]. Rationale: the statistical counters stored in the driver instance are updated in debug builds.
  U32      PageIndex;
  int      r;
  int      Result;
  U8       Unit;
  unsigned PPB_Shift;
  unsigned BPG_Shift;
  unsigned NumPhyBlocks;
  unsigned FirstBlock;
  unsigned PagesPerPhyBlock;
  unsigned LastPhyBlock;
  unsigned PhyBlockIndex;

  IF_STATS(pInst->StatCounters.EraseCnt++);     // Increment statistics counter if enabled.
  //
  // Prepare the erase operation.
  //
  PPB_Shift         = pInst->PPB_Shift;
  BPG_Shift         = _GetBPG_Shift(pInst);
  FirstBlock        = pInst->FirstBlock;
  Unit              = pInst->Unit;
  r                 = 0;
  BlockIndex       += FirstBlock;
  PhyBlockIndex     = BlockIndex << BPG_Shift;
  NumPhyBlocks      = 1uL << BPG_Shift;
  PagesPerPhyBlock  = 1uL << (PPB_Shift - BPG_Shift);
  LastPhyBlock      = PhyBlockIndex + NumPhyBlocks - 1u;
  PageIndex         = (U32)LastPhyBlock << (PPB_Shift - BPG_Shift);
  //
  // Erase all the blocks in the group. For fail-safety reasons, start erasing with the last block.
  // If the erase operation is interrupted by a power loss, the first block will not be erased.
  // The low-level mount operation will be able then to identify the block type and take the appropriate action.
  //
  do {
    Result = pInst->pPhyType->pfEraseBlock(Unit, PageIndex);
    CALL_TEST_HOOK_BLOCK_ERASE(Unit, PageIndex, &r);
    if (Result != 0) {
      r = 1;
    }

    //
    // Fail-safe TP
    //
    // At this point the remaining blocks in a group are not erased.
    // The low-level mount operation should detect this and erase them.
    //
    CALL_TEST_HOOK_FAIL_SAFE(pInst->Unit);

    PageIndex -= PagesPerPhyBlock;
  } while (--NumPhyBlocks != 0u);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NAND_UNI: ERASE_BLOCK BlockIndex: %lu, r: %d\n", BlockIndex - FirstBlock, r));
#if FS_NAND_VERIFY_ERASE
  //
  // Verify if all the bytes in the block are set to 0xFF.
  //
  if (r == 0) {
    if (pInst->VerifyErase != 0u) {
      int IsBlank;

      BlockIndex -= FirstBlock;
      IsBlank = _IsBlockBlank(pInst, BlockIndex);
      if (IsBlank == 0) {
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_UNI: Verify failed at block %d.", BlockIndex - FirstBlock));
        r = 1;                          // Error, the block was not erased correctly.
      }
    }
  }
#endif // FS_NAND_VERIFY_ERASE
  return r;
}

/*********************************************************************
*
*       _AllocWorkBlockDesc
*
*  Function description
*    Allocates a work block descriptor from the array in the pInst structure.
*
*  Return value
*    ==0    No more descriptors available. Work blocks must be converted.
*    !=0    Pointer to the allocated work block.
*/
static NAND_UNI_WORK_BLOCK * _AllocWorkBlockDesc(NAND_UNI_INST * pInst, unsigned lbi) {
  NAND_UNI_WORK_BLOCK * pWorkBlock;
  unsigned              NumWorkBlocksFree;
  unsigned              NumBlocksFree;
  unsigned              NumBytes;

  //
  // Count the number of free work blocks.
  //
  NumWorkBlocksFree = 0;
  NumBlocksFree     = pInst->NumBlocksFree;
  pWorkBlock = pInst->pFirstWorkBlockFree;
  while (pWorkBlock != NULL) {
    ++NumWorkBlocksFree;
    pWorkBlock = pWorkBlock->pNext;
  }
  if ((NumWorkBlocksFree == 0u) ||
      (NumWorkBlocksFree <= NumBlocksFree)) {
    return NULL;
  }
  //
  // Check if a free block is available.
  //
  pWorkBlock = pInst->pFirstWorkBlockFree;
  //
  // Initialize work block descriptor, mark it as in use and add it to the list.
  //
  NumBytes = _WB_GetAssignmentSize(pInst);
  _WB_RemoveFromFreeList(pInst, pWorkBlock);
  _WB_AddToUsedList(pInst, pWorkBlock);
  pWorkBlock->lbi      = lbi;
  pWorkBlock->brsiFree = BRSI_BLOCK_INFO;
  pWorkBlock->pbi      = 0;
  FS_MEMSET(pWorkBlock->paAssign, 0, NumBytes);   // Make sure that no old assignment info from previous descriptor is in the table.
  return pWorkBlock;
}

/*********************************************************************
*
*       _ClearBlock
*
*  Function description
*    Erases a block and writes the erase count to first sector.
*
*  Parameters
*    pInst        [IN]  Driver instance.
*    BlockIndex   Number of the block to clear.
*    EraseCnt     Actual erase count of the block. This value is incremented and written to sector 0.
*
*  Return value
*    ==0    OK, block can be used to write data to it
*    !=0    An error occurred
*
*  Additional information
*    The block is not marked as free.
*/
static int _ClearBlock(NAND_UNI_INST * pInst, unsigned BlockIndex, U32 EraseCnt) {
  int r;
  int Result;

  r = 0;                                                    // Set to indicate success.
  if (BlockIndex == 0u) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_UNI: _ClearBlock: Invalid block index."));
    r = 1;
    goto Done;
  }
  if (pInst->ActiveWLStatus == ACTIVE_WL_DISABLED_TEMP) {
    pInst->ActiveWLStatus = ACTIVE_WL_ENABLED;              // Re-enable the active wear leveling.
  }
  Result = _EraseBlock(pInst, BlockIndex);
  if (Result != 0) {
    (void)_MarkBlockAsBad(pInst, BlockIndex, RESULT_ERASE_ERROR, 0);
    r = 1;                                                  // Error, erase operation failed.
    goto Done;
  }
  ++EraseCnt;
  Result = _WriteEraseCnt(pInst, BlockIndex, EraseCnt);
  if (Result != 0) {
    (void)_MarkBlockAsBad(pInst, BlockIndex, RESULT_WRITE_ERROR, 0);
    r = 1;                                                  // Error, write operation failed.
    goto Done;
  }
  _MarkBlockAsFree(pInst, BlockIndex);
Done:
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NAND_UNI: CLEAR_BLOCK BlockIndex: %d, EraseCnt: 0x%08x, r: %d\n", BlockIndex, EraseCnt, r));
  return r;                               // OK, block prepared for write.
}

/*********************************************************************
*
*       _ClearBlockIfAllowed
*
*  Function description
*    Performs the same operation as _ClearBlock() indirectly,
*    via a function pointer.
*/
static int _ClearBlockIfAllowed(NAND_UNI_INST * pInst, unsigned BlockIndex, U32 EraseCnt) {
  int r;

  r = 1;            // Set to indicate failure.
  if (pInst->pWriteAPI != NULL) {
    r = pInst->pWriteAPI->pfClearBlock(pInst, BlockIndex, EraseCnt);
  }
  return r;
}

/*********************************************************************
*
*       _FreeBlock
*
*  Function description
*    - Erases a block and stores the erase count to first sector
*    - Marks the block as free in the pFreeMap
*    - If required, updates management info of wear leveling
*
*  Parameters
*    pInst        [IN]  Driver instance.
*    BlockIndex   Index of the physical block.
*    EraseCnt     Number of times the phy. block has been erased.
*
*  Return value
*    ==0    Success, the phy. block is free.
*    !=0    An error occurred.
*/
static int _FreeBlock(NAND_UNI_INST * pInst, unsigned BlockIndex, U32 EraseCnt) {
  int r;

  if (EraseCnt == ERASE_CNT_INVALID) {
    r = _ReadEraseCnt(pInst, BlockIndex, &EraseCnt);
    if (r != 0) {
      EraseCnt = pInst->EraseCntMax;
    }
  }
  r = _ClearBlock(pInst, BlockIndex, EraseCnt);
  //
  // Wear leveling keeps track of the number of data and work blocks with a minimum EraseCnt.
  // We update this information here if such a block has been erased.
  //
  if ((pInst->NumBlocksEraseCntMin != 0u) && (pInst->EraseCntMin == EraseCnt)) {
    pInst->NumBlocksEraseCntMin--;
  }
  return r;
}

/*********************************************************************
*
*       _CopySectorWithECC
*
*  Function description
*    Copies the data of a sector into another sector.
*    The ECC of the source data is checked during the copy operation.
*
*  Parameters
*    pInst              [IN]  Driver instance.
*    SectorIndexSrc     Source physical sector index.
*    SectorIndexDest    Destination physical sector index.
*    brsi               Block relative index of logical sector. It is used
*                       for error recovery and for finding out which duplicated
*                       data block should be erased at low-level mount.
*
*  Return value
*    RESULT_NO_ERROR                    OK
*    RESULT_BIT_ERRORS_CORRECTED        OK
*    RESULT_BIT_ERROR_IN_ECC            OK
*    RESULT_DATA_RECOVERED              OK
*    RESULT_UNCORRECTABLE_BIT_ERRORS    Error, fatal
*    RESULT_READ_ERROR                  Error, fatal
*    RESULT_WRITE_ERROR                 Error, recoverable
*/
static int _CopySectorWithECC(NAND_UNI_INST * pInst, U32 SectorIndexSrc, U32 SectorIndexDest, unsigned brsi) {
  int      r;
  int      Result;
  unsigned BytesPerPage;
  unsigned BytesPerSpareArea;
  int      StoreBRSI;
  int      WriteWithECC;
  int      Pattern;

  Result    = RESULT_READ_ERROR;
  StoreBRSI = 0;
  //
  // When the block grouping is enabled we write the BRSI to the last sector in the destination data block.
  // The low-level mount checks this value in order to determine if the data block copy operation
  // completed successfully.
  //
  if (_IsBlockGroupingEnabled(pInst) != 0) {
    unsigned SectorsPerBlock;

    SectorsPerBlock = 1uL << pInst->PPB_Shift;
    if (brsi == (SectorsPerBlock - 1u)) {       // Last sector in a block?
      StoreBRSI = 1;
    }
  }
  if (StoreBRSI == 0) {
    //
    // Try to copy the page contents without reading it to host and writing it back.
    //
    if (pInst->pPhyType->pfCopyPage != NULL) {
      if (pInst->AllowBlankUnusedSectors != 0u) {
        //
        // Check if the file system marked the data of this sector as invalid. In this case the sector is not copied.
        //
        Result = _ReadSpareAreaWithECC(pInst, SectorIndexSrc);
        if ((Result == RESULT_NO_ERROR) || (Result == RESULT_BIT_ERRORS_CORRECTED) || (Result == RESULT_BIT_ERROR_IN_ECC)) {
          if (_LoadSectorStat(pInst) == SECTOR_STAT_EMPTY) {
            Result = RESULT_NO_ERROR;         // OK, sector data is not valid. Nothing to copy.
            goto Done;
          }
        }
      }
      Result = RESULT_READ_ERROR;
      r = _CopyPage(pInst, SectorIndexSrc, SectorIndexDest);
      if (r == 0) {
        Result = RESULT_NO_ERROR;
        IF_STATS(pInst->StatCounters.CopySectorCnt++);
      }
    }
  }
  //
  // Internal page copy operation is not supported or it failed.
  // Try to copy by reading the sector data to MCU and then writing it back.
  //
  if (Result != RESULT_NO_ERROR) {
    //
    // Read the data and the spare area of the source sector and correct the bit errors.
    //
    Result = _ReadSectorWithECCAndER(pInst, _pSectorBuffer, SectorIndexSrc, brsi);
    if ((Result == RESULT_NO_ERROR) || (Result == RESULT_BIT_ERRORS_CORRECTED) || (Result == RESULT_BIT_ERROR_IN_ECC) || (Result == RESULT_DATA_RECOVERED)) {
      //
      // Check if the file system marked the data of this sector as invalid. In this case the sector is not copied.
      //
      if (StoreBRSI == 0) {
        if (pInst->AllowBlankUnusedSectors != 0u) {
          if (_LoadSectorStat(pInst) == SECTOR_STAT_EMPTY) {
            Result = RESULT_NO_ERROR;       // OK, sector data not valid. Nothing to copy.
            goto Done;
          }
        }
      }
      //
      // Write the sector data to the new location.
      //
      WriteWithECC = 0;
      //
      // Recalculate the ECC if the BRSI has to be stored to spare area.
      //
      if (StoreBRSI != 0) {
        _StoreBRSI(pInst, brsi);
        WriteWithECC = 1;
      }
      //
      // A bit error that occurs in ECC is not corrected by the ECC check routine.
      // It must be recalculated to avoid propagating the bit error to destination sector.
      // We also have to recalculate ECC if the data has been recovered via RAID.
      //
      if ((Result == RESULT_BIT_ERROR_IN_ECC) || (Result == RESULT_DATA_RECOVERED)) {
        WriteWithECC = 1;
      }
      //
      // Do not copy empty sectors.
      //
      r = _IsDataSpareBlankEx(pInst, _pSectorBuffer, _pSpareAreaData, 0);
      if (r != 0) {
        if (pInst->AllowBlankUnusedSectors != 0u) {
          goto Done;
        }
        //
        // Fill the data with 0s to minimize the risk of bit errors.
        //
        Pattern = (int)_GetDataFillPattern(pInst);
        FS_MEMSET(_pSectorBuffer, Pattern, pInst->BytesPerPage);
        _ClearStaticSpareArea(pInst);
        WriteWithECC = 1;                 // We have to calculate the ECC of the written data.
      }
      //
      // Write the sector data either with ECC or directly.
      //
      if (WriteWithECC != 0) {
        r = _WriteSectorWithECC(pInst, _pSectorBuffer, SectorIndexDest);
      } else {
        BytesPerPage      = pInst->BytesPerPage;
        BytesPerSpareArea = pInst->BytesPerSpareArea;
        r = _WriteDataSpare(pInst, SectorIndexDest, _pSectorBuffer, BytesPerPage, _pSpareAreaData, BytesPerSpareArea);
      }
#if FS_NAND_VERIFY_WRITE
      if (r == 0) {
        r = _VerifySector(pInst, _pSectorBuffer, SectorIndexDest);
      }
#endif
      if (r != 0) {
        Result = RESULT_WRITE_ERROR;      // Error, data could not be written.
        goto Done;
      }
      IF_STATS(pInst->StatCounters.CopySectorCnt++);
    }
  }
Done:
  return Result;
}

/*********************************************************************
*
*       _CountBlocksWithEraseCntMin
*
*  Function description
*    Goes through all blocks and counts the data and work blocks
*    with the lowest erase count.
*
*  Parameters
*    pInst        [IN]  Driver instance.
*    pEraseCnt    [OUT] Minimum erase count.
*    pPBI         [OUT] Index of the first data block with the min erase count.
*
*  Return value
*    Number of data blocks found with a minimum erase count.
*/
static U32 _CountBlocksWithEraseCntMin(NAND_UNI_INST * pInst, U32 * pEraseCnt, unsigned * pPBI) {
  unsigned iBlock;
  unsigned pbi;
  U32      EraseCntMin;
  U32      SectorIndex;
  U32      EraseCnt;
  U32      NumBlocks;
  unsigned BlockType;
  int      r;

  pbi         = 0;
  EraseCntMin = ERASE_CNT_INVALID;
  NumBlocks   = 0;
  //
  // Check all allocated blocks.
  //
  for (iBlock = PBI_STORAGE_START; iBlock < pInst->NumBlocks; ++iBlock) {
    //
    // Consider only blocks that contain valid data.
    //
    if (_IsBlockFree(pInst, iBlock) != 0) {
      continue;
    }
    //
    // Skip blocks marked as defective.
    //
    if (_IsBlockBad(pInst, iBlock) != 0) {
      continue;
    }
    //
    // Found a block which is in use. Get the erase count and the block type from the second page of the block.
    //
    SectorIndex = _BlockIndex2SectorIndex0(pInst, iBlock);
    ++SectorIndex;
    r = _ReadSpareAreaWithECC(pInst, SectorIndex);
    if ((r == RESULT_NO_ERROR) || (r == RESULT_BIT_ERRORS_CORRECTED) || (r == RESULT_BIT_ERROR_IN_ECC)) {
      BlockType = _LoadBlockType(pInst);
      //
      // OK, block information has been read form the spare area.
      //
      if ((BlockType == BLOCK_TYPE_DATA) || (BlockType == BLOCK_TYPE_WORK)) {
        EraseCnt = _LoadEraseCnt(pInst);
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
  }
  *pEraseCnt = EraseCntMin;
  *pPBI      = pbi;
  return NumBlocks;
}

/*********************************************************************
*
*       _FindBlockByEraseCnt
*
*  Function description
*    Goes through all data and work blocks and returns the first one
*    with the specified erase count.
*
*  Parameters
*    pInst        [IN]  Driver instance.
*    EraseCnt     Erase count to search for.
*
*  Return value
*    ==0    No data block found.
*    !=0    Index of the found data block.
*/
static unsigned _FindBlockByEraseCnt(NAND_UNI_INST * pInst, U32 EraseCnt) {
  unsigned iBlock;
  U32      SectorIndex;
  unsigned BlockType;
  int      r;
  U32      EraseCntFound;

  //
  // Check all allocated blocks.
  //
  for (iBlock = PBI_STORAGE_START; iBlock < pInst->NumBlocks; ++iBlock) {
    //
    // Consider only blocks which contain valid data.
    //
    if (_IsBlockFree(pInst, iBlock) != 0) {
      continue;
    }
    //
    // Skip blocks marked as defective.
    //
    if (_IsBlockBad(pInst, iBlock) != 0) {
      continue;
    }
    //
    // Found a block which is in use. Get the erase count and the block type from the second page of the block.
    //
    SectorIndex = _BlockIndex2SectorIndex0(pInst, iBlock);
    ++SectorIndex;
    r = _ReadSpareAreaWithECC(pInst, SectorIndex);
    if ((r == RESULT_NO_ERROR) || (r == RESULT_BIT_ERRORS_CORRECTED) || (r == RESULT_BIT_ERROR_IN_ECC)) {
      BlockType = _LoadBlockType(pInst);
      //
      // OK, block information has been read form the spare area.
      //
      if ((BlockType == BLOCK_TYPE_DATA) || (BlockType == BLOCK_TYPE_WORK)) {
        EraseCntFound = _LoadEraseCnt(pInst);
        if (EraseCnt == EraseCntFound) {
          return iBlock;
        }
      }
    }
  }
  return 0;     // No data block found with the given erase count.
}

/*********************************************************************
*
*       _CheckActiveWearLeveling
*
*  Function description
*    Checks if it is time to perform the active wear leveling.
*    The specified erase count is compared to the lowest erase count.
*    If the difference is too big, the index of the block with the
*    lowest erase count is returned.
*
*  Parameters
*    pInst            [IN]  Driver instance.
*    EraseCntAlloc    Erase count of block to be erased.
*    pEraseCnt        [OUT] Erase count of the data or work block.
*
*  Return value
*    ==0    No data block found
*    !=0    Physical block index of the data block found
*/
static unsigned _CheckActiveWearLeveling(NAND_UNI_INST * pInst, U32 EraseCntAlloc, U32 * pEraseCnt) {
  unsigned pbi;
  I32      EraseCntDiff;
  U32      NumBlocks;
  U32      EraseCntMin;

  //
  // Update pInst->EraseCntMin if necessary.
  //
  pbi         = 0;
  NumBlocks   = pInst->NumBlocksEraseCntMin;
  EraseCntMin = pInst->EraseCntMin;
  if (NumBlocks == 0u) {
    NumBlocks = _CountBlocksWithEraseCntMin(pInst, &EraseCntMin, &pbi);
    if (NumBlocks == 0u) {
      return 0;     // We don't have any data block yet. It can happen if the flash is empty.
    }
    pInst->EraseCntMin          = EraseCntMin;
    pInst->NumBlocksEraseCntMin = NumBlocks;
  }
  //
  // Check if the threshold for active wear leveling has been reached.
  //
  EraseCntDiff = (I32)EraseCntAlloc -  (I32)EraseCntMin;
  if (EraseCntDiff < (I32)pInst->MaxEraseCntDiff) {
    return 0;       // Active wear leveling not necessary, EraseCntDiff is not large enough yet.
  }
  if (pbi == 0u) {
    pbi = _FindBlockByEraseCnt(pInst, EraseCntMin);
    if (pbi == 0u) {
      //
      // We did not find any block with the given erase count.
      // It can happen if at low-level mount a data or a work
      // block with a minimum erase count was discarded.
      // Search for a new block with a minimum erase count.
      //
      NumBlocks = _CountBlocksWithEraseCntMin(pInst, &EraseCntMin, &pbi);
      pInst->NumBlocksEraseCntMin = NumBlocks;
      pInst->EraseCntMin          = EraseCntMin;
    }
  }
  //
  // Return the minimum erase count and update the number of blocks available
  // with this minimum erase count.
  //
  *pEraseCnt = EraseCntMin;
  if (pInst->NumBlocksEraseCntMin != 0u) {
    pInst->NumBlocksEraseCntMin--;
  }
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
*    ==0    No more free blocks.
*    !=0    Physical block index of the allocated block.
*/
static unsigned _PerformPassiveWearLeveling(NAND_UNI_INST * pInst, U32 * pEraseCnt) {
  unsigned i;
  unsigned iBlock;
  U32      EraseCnt;
  int      r;
  U32      SectorIndex0;
  U32      NumBlocks;

  //
  // Search for a block we can write to.
  //
  iBlock    = pInst->MRUFreeBlock;
  NumBlocks = pInst->NumBlocks;
  for (i = 0; i < NumBlocks; i++) {
    if (++iBlock >= NumBlocks) {
      iBlock = PBI_STORAGE_START;
    }
    if (_IsBlockFree(pInst, iBlock) != 0) {
      //
      // The function must return the erase count of the block, so we read it here.
      //
      EraseCnt = ERASE_CNT_INVALID;
      (void)_ReadEraseCnt(pInst, iBlock, &EraseCnt);
      //
      // An invalid erase count indicates that the block information is invalid.
      // There are 2 reasons for this:
      //   - Block is blank
      //   - Bit errors where detected and the ECC could not correct them
      //
      if (EraseCnt == ERASE_CNT_INVALID) {
        EraseCnt = pInst->EraseCntMax;
        //
        // Erase the block only if needed by checking if the first page including spare area is empty.
        //
        SectorIndex0 = _BlockIndex2SectorIndex0(pInst, iBlock);
        if (_IsPageBlank(pInst, SectorIndex0) != 0) {
          //
          // Store the erase count so that the block is recognized as free at low-level mount.
          //
          r = _WriteEraseCnt(pInst, iBlock, EraseCnt);
          if (r != 0) {
            (void)_MarkBlockAsBad(pInst, iBlock, RESULT_WRITE_ERROR, 0);
            _MarkBlockAsAllocated(pInst, iBlock);       // This block can not be used anymore for data storage.
            continue;
          }
        } else {
          //
          // Erase and store the erase count.
          //
          r = _ClearBlock(pInst, iBlock, EraseCnt);
          if (r != 0) {
            continue;                                   // Error, could not write erase count.
          }
        }
      }
      *pEraseCnt = EraseCnt;
      _MarkBlockAsAllocated(pInst, iBlock);
      pInst->MRUFreeBlock = iBlock;
      return iBlock;
    }
  }
  return 0;               // Error, no more free blocks
}

/*********************************************************************
*
*       _RemoveDataBlockByLBI
*
*  Function description
*    Removes the data block from internal list and from the mapping table.
*
*   Parameters
*     pInst         [IN]  Driver instance.
*     lbi           Index of the logical block assigned to data block.
*
*  Return value
*    !=0    Index of the physical block that stored the data block.
*    ==0    Data block not found.
*/
static unsigned _RemoveDataBlockByLBI(NAND_UNI_INST * pInst, unsigned lbi) {        //lint -efunc(818, _RemoveDataBlockByLBI) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory]. Rationale: the data block list is modified when the fast write mode is active.
  unsigned pbi;

  //
  // Free the data block if one is assigned to the logical block.
  //
  pbi = _L2P_Read(pInst, lbi);
  if (pbi != 0u) {
    _L2P_Write(pInst, lbi, 0);    // Remove the logical block from the mapping table.
#if FS_NAND_SUPPORT_FAST_WRITE
    {
      NAND_UNI_DATA_BLOCK * pDataBlock;

      //
      // Remove the data block from the list.
      //
      pDataBlock = pInst->pFirstDataBlockInUse;
      while (pDataBlock != NULL) {
        if (pDataBlock->pbi == pbi) {
          _DB_RemoveFromUsedList(pInst, pDataBlock);
          _DB_AddToFreeList(pInst, pDataBlock);
          break;
        }
        pDataBlock = pDataBlock->pNext;
      }
    }
#endif // FS_NAND_SUPPORT_FAST_WRITE
  }
  return pbi;
}

/*********************************************************************
*
*       _RemoveDataBlock
*
*  Function description
*    Removes the data block from internal list and from the mapping table.
*
*   Parameters
*     pInst         [IN]  Driver instance.
*     BlockIndex    Index of the data block to be removed.
*
*  Return value
*    ==0        Data block found and removed.
*    !=0        Data block not found.
*/
static int _RemoveDataBlock(NAND_UNI_INST * pInst, unsigned BlockIndex) {
  unsigned lbi;
  int      r;

  r   = 1;            // Set to indicate that the data block was not found.
  lbi = _pbi2lbi(pInst, BlockIndex);
  if (lbi != LBI_INVALID) {
    (void)_RemoveDataBlockByLBI(pInst, lbi);
  }
  return r;
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
*    pInst          [IN]  Driver instance.
*    pbiSrc         Index of the block to be moved.
*    pbiDest        Index of the block where sector data should be moved.
*    EraseCntDest   Erase count of the destination block.
*                   It will be stored to the second page of the block
*                   together with other block information.
*    pErrorBRSI     [OUT] Block relative index of the sector where the error occurred.
*
*  Return value
*    RESULT_NO_ERROR                  OK
*    RESULT_BIT_ERROR_IN_ECC          OK
*    RESULT_DATA_RECOVERED            OK
*    RESULT_UNCORRECTABLE_BIT_ERRORS  Error, fatal
*    RESULT_READ_ERROR                Error, fatal
*    RESULT_WRITE_ERROR               Error, recoverable
*/
static int _MoveDataBlock(NAND_UNI_INST * pInst, unsigned pbiSrc, unsigned pbiDest, U32 EraseCntDest, unsigned * pErrorBRSI) {
  unsigned SectorsPerBlock;
  unsigned iSector;
  U32      SectorIndexSrc0;
  U32      SectorIndexDest0;
  int      r;
  unsigned BlockCntSrc;
  unsigned lbi;
  U32      EraseCntSrc;
  int      DataRecovered;
  int      ErrorReported;
  unsigned ErrorBRSI;
  int      Result;
  unsigned MergeCntSrc;

  DataRecovered    = 0;
  ErrorReported    = 0;
  SectorIndexSrc0  = _BlockIndex2SectorIndex0(pInst, pbiSrc);
  SectorIndexDest0 = _BlockIndex2SectorIndex0(pInst, pbiDest);
  SectorsPerBlock  = 1uL << pInst->PPB_Shift;
  iSector          = BRSI_BLOCK_INFO;
  ErrorBRSI        = BRSI_INVALID;
  //
  // Read the block related info from the second page of source block.
  // LBI, BlockCnt, and MergeCnt are required for the destination block.
  //
  Result = _ReadSectorWithECCAndER(pInst, _pSectorBuffer, SectorIndexSrc0 + iSector, iSector);
  if ((Result == RESULT_NO_ERROR) || (Result == RESULT_BIT_ERRORS_CORRECTED) || (Result == RESULT_BIT_ERROR_IN_ECC) || (Result == RESULT_DATA_RECOVERED)) {
    BlockCntSrc = _LoadBlockCnt(pInst);
    lbi         = _LoadLBI(pInst);
    EraseCntSrc = _LoadEraseCnt(pInst);
    MergeCntSrc = _LoadMergeCnt(pInst);
    _StoreBlockCnt(pInst, BlockCntSrc + 1u);  // BlockCnt helps low-level mount to detect the most recent version of a duplicated data block.
    _StoreEraseCnt(pInst, EraseCntDest);
    _StoreMergeCnt(pInst, MergeCntSrc);       // MergeCnt helps low-level mount to detect if the data block contains invalid data.
    if ((Result == RESULT_BIT_ERROR_IN_ECC) || (Result == RESULT_DATA_RECOVERED)) {
      ErrorBRSI     = iSector;
      DataRecovered = 1;                      // Remember that we encountered a bit error but the data has been recovered.
    } else {
      Result = RESULT_NO_ERROR;
    }
    //
    // Write data and spare area to second sector.
    //
    r = _WriteSectorWithECC(pInst, _pSectorBuffer, SectorIndexDest0 + iSector);
#if FS_NAND_VERIFY_WRITE
    if (r == 0) {
      r = _VerifySector(pInst, _pSectorBuffer, SectorIndexDest0 + iSector);
    }
#endif
    if (r != 0) {
      ErrorBRSI = iSector;
      Result    = RESULT_WRITE_ERROR;           // Error, could not write data and spare area
      goto Done;
    }
    IF_STATS(pInst->StatCounters.CopySectorCnt++);

    //
    // Fail-safe TP
    //
    // At this point we have 2 data blocks with the same LBI. The copy operation is not complete.
    // Low-level mount should throw away the latest version of the data block.
    //
    CALL_TEST_HOOK_FAIL_SAFE(pInst->Unit);

    //
    // Now, copy the rest of the sectors.
    //
    ++iSector;
    for (; iSector < SectorsPerBlock; ++iSector) {
      r = _CopySectorWithECC(pInst, SectorIndexSrc0 + iSector, SectorIndexDest0 + iSector, iSector);
      if ((r == RESULT_UNCORRECTABLE_BIT_ERRORS) || (r == RESULT_READ_ERROR) || (r == RESULT_WRITE_ERROR)) {
        if (ErrorReported == 0) {
          ErrorBRSI     = iSector;
          Result        = r;
          ErrorReported = 1;                  // Remember that we encountered a fatal error.
        }
      }
      if ((r == RESULT_BIT_ERROR_IN_ECC) || (r == RESULT_DATA_RECOVERED)) {
        if ((ErrorReported == 0) && (DataRecovered == 0)) {
          ErrorBRSI     = iSector;
          Result        = r;
          DataRecovered = 1;                  // Remember that we encountered a bit error but the data has been recovered.
        }
      }
    }
    //
    // Update the mapping of logical to physical blocks if the data has been
    // written successfully to destination block.
    //
    if ((ErrorReported == 0) || (Result != RESULT_WRITE_ERROR)) {
      (void)_RemoveDataBlock(pInst, pbiSrc);
      _L2P_Write(pInst, lbi, pbiDest);
    }
    //
    // Erase the block and put it in the free list if no bit errors were encountered.
    // In case of bit errors the block stays allocated and will be later marked as defective.
    //
    if (ErrorReported == 0) {
      if (DataRecovered == 0) {
        (void)_FreeBlock(pInst, pbiSrc, EraseCntSrc);
      }
    }
  } else {
    //
    // Unrecoverable error occurred. All data in this block is lost.
    // Remove the logical block from the mapping table.
    //
    ErrorBRSI = iSector;
    (void)_RemoveDataBlock(pInst, pbiSrc);
  }
Done:
  *pErrorBRSI = ErrorBRSI;
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NAND_UNI: MOVE_DATA_BLOCK pbiSrc: %u, pbiDest: %u, EraseCntDest: 0x%08x, r: %d\n", pbiSrc, pbiDest, EraseCntDest, Result));
  return Result;
}

/*********************************************************************
*
*       _MoveWorkBlock
*
*  Function description
*    Copies the contents of a work block into another block.
*    The source data block is marked as free.
*
*  Parameters
*    pInst          [IN]  Driver instance.
*    pWorkBlock     [IN]  Work block to be moved.
*    pbiDest        Index of the block where to copy.
*    EraseCntDest   Erase count of the destination block. Will be stored
*                   to second page of the block together with other block
*                   information.
*    pErrorBRSI     [OUT] Block relative index of the sector where the error occurred.
*
*  Return value
*    RESULT_NO_ERROR                  OK
*    RESULT_BIT_ERROR_IN_ECC          OK
*    RESULT_DATA_RECOVERED            OK
*    RESULT_UNCORRECTABLE_BIT_ERRORS  Error, fatal
*    RESULT_READ_ERROR                Error, fatal
*    RESULT_WRITE_ERROR               Error, recoverable
*/
static int _MoveWorkBlock(NAND_UNI_INST * pInst, NAND_UNI_WORK_BLOCK * pWorkBlock, unsigned pbiDest, U32 EraseCntDest, unsigned * pErrorBRSI) {
  unsigned SectorsPerBlock;
  unsigned iSector;
  U32      SectorIndexSrc0;
  U32      SectorIndexDest0;
  int      r;
  unsigned BlockCntSrc;
  unsigned lbi;
  U32      EraseCntSrc;
  int      DataRecovered;
  int      ErrorReported;
  unsigned ErrorBRSI;
  int      Result;
  unsigned MergeCntSrc;
  unsigned pbiSrc;
  unsigned brsiPhySrc;
  unsigned brsiPhyDest;
  unsigned BytesPerPage;
  unsigned BytesPerSpareArea;
  unsigned NumSectorsValid;

  DataRecovered    = 0;
  ErrorReported    = 0;
  pbiSrc           = pWorkBlock->pbi;
  SectorIndexSrc0  = _BlockIndex2SectorIndex0(pInst, pbiSrc);
  SectorIndexDest0 = _BlockIndex2SectorIndex0(pInst, pbiDest);
  SectorsPerBlock  = 1uL << pInst->PPB_Shift;
  brsiPhyDest      = BRSI_BLOCK_INFO;
  ErrorBRSI        = BRSI_INVALID;
  EraseCntSrc      = ERASE_CNT_INVALID;
  Result           = 0;
  //
  // Count the number of sectors that contain valid data.
  //
  NumSectorsValid = 0;
  for (iSector = BRSI_BLOCK_INFO; iSector < SectorsPerBlock; ++iSector) {
    brsiPhySrc = _WB_ReadAssignment(pInst, pWorkBlock, iSector);
    if (brsiPhySrc != 0u) {
      ++NumSectorsValid;
    }
  }
  //
  // Copy the data of all written sectors from the source work block to the destination block.
  //
  for (iSector = BRSI_BLOCK_INFO; iSector < SectorsPerBlock; ++iSector) {
    brsiPhySrc = _WB_ReadAssignment(pInst, pWorkBlock, iSector);
    if (brsiPhySrc == 0u) {
      continue;             // The sector has not been written yet.
    }
    //
    // We have found a sector which needs to be copied.
    // The second page in the work block contains block related
    // information in the spare are and needs to be handled separately.
    //
    if (brsiPhyDest == BRSI_BLOCK_INFO) {
      //
      // Read the block related information from the second page of source block.
      // This information is required to be stored to the destination block.
      //
      Result = _ReadSectorWithECCAndER(pInst, _pSectorBuffer, SectorIndexSrc0 + BRSI_BLOCK_INFO, BRSI_BLOCK_INFO);
      if ((Result == RESULT_NO_ERROR) || (Result == RESULT_BIT_ERRORS_CORRECTED) || (Result == RESULT_BIT_ERROR_IN_ECC) || (Result == RESULT_DATA_RECOVERED)) {
        EraseCntSrc = _LoadEraseCnt(pInst);
        lbi         = _LoadLBI(pInst);
        BlockCntSrc = _LoadBlockCnt(pInst);
        MergeCntSrc = _LoadMergeCnt(pInst);
        if ((Result == RESULT_BIT_ERROR_IN_ECC) || (Result == RESULT_DATA_RECOVERED)) {
          ErrorBRSI     = iSector;
          DataRecovered = 1;                        // Remember that we encountered a bit error but the data has been recovered.
        } else {
          Result = RESULT_NO_ERROR;
        }
      } else {
        ErrorBRSI = iSector;
        goto Done;                                  // Error, could not read sector data.
      }
      //
      // Read the sector data if the source sector is not located on the second page of the source work block.
      //
      if (brsiPhySrc != BRSI_BLOCK_INFO) {
        Result = _ReadSectorWithECCAndER(pInst, _pSectorBuffer, SectorIndexSrc0 + brsiPhySrc, brsiPhySrc);
        if ((Result == RESULT_NO_ERROR) || (Result == RESULT_BIT_ERRORS_CORRECTED) || (Result == RESULT_BIT_ERROR_IN_ECC) || (Result == RESULT_DATA_RECOVERED)) {
          if ((Result == RESULT_BIT_ERROR_IN_ECC) || (Result == RESULT_DATA_RECOVERED)) {
            if (DataRecovered == 0) {
              ErrorBRSI     = iSector;
              DataRecovered = 1;                    // Remember that we encountered a bit error but the data has been recovered.
            }
          } else {
            Result = RESULT_NO_ERROR;
          }
        } else {
          ErrorBRSI = iSector;
          goto Done;                                // Error, could not read sector data.
        }
      }
      //
      // Store the block related information to spare area buffer.
      //
      _StoreEraseCnt(pInst, EraseCntDest);
      _StoreLBI(pInst, lbi);
      _StoreBlockCnt(pInst, BlockCntSrc + 1u);      // BlockCnt helps low-level mount to detect the most recent version of a duplicated data block.
      _StoreBlockType(pInst, BLOCK_TYPE_WORK);
      _StoreMergeCnt(pInst, MergeCntSrc);           // MergeCnt helps low-level mount to detect if the data block contains invalid data.
      _StoreNumSectors(pInst, NumSectorsValid);     // NumSectors helps low-level mount to detect if the copy operation completed before an unexpected reset.
      //
      // Write data and spare area to second sector.
      //
      r = _WriteSectorWithECC(pInst, _pSectorBuffer, SectorIndexDest0 + brsiPhyDest);
#if FS_NAND_VERIFY_WRITE
      if (r == 0) {
        r = _VerifySector(pInst, _pSectorBuffer, SectorIndexDest0 + brsiPhyDest);
      }
#endif
      if (r != 0) {
        ErrorBRSI = iSector;
        Result    = RESULT_WRITE_ERROR;               // Error, could not write data and spare area
        goto Done;
      }
      IF_STATS(pInst->StatCounters.CopySectorCnt++);

      //
      // Fail-safe TP
      //
      // At this point we have 2 work blocks with the same LBI. The copy operation is not complete.
      // Low-level mount should throw away the most recent version of the work block.
      //
      CALL_TEST_HOOK_FAIL_SAFE(pInst->Unit);

    } else {
      //
      // Try to copy the page contents without reading it to host and writing it back.
      //
      Result = RESULT_READ_ERROR;
      if (pInst->pPhyType->pfCopyPage != NULL) {
        r = _CopyPage(pInst, SectorIndexSrc0 + brsiPhySrc, SectorIndexDest0 + brsiPhyDest);
        if (r == 0) {
          Result = RESULT_NO_ERROR;
          IF_STATS(pInst->StatCounters.CopySectorCnt++);
        }
      }
      //
      // Internal page copy operation is not supported or it failed.
      // Try to copy by reading the sector data to MCU and then writing it back.
      //
      if (Result != RESULT_NO_ERROR) {
        //
        // Read the data and the spare area of the source sector and correct the bit errors.
        //
        Result = _ReadSectorWithECCAndER(pInst, _pSectorBuffer, SectorIndexSrc0 + brsiPhySrc, brsiPhySrc);
        if ((Result == RESULT_NO_ERROR) || (Result == RESULT_BIT_ERRORS_CORRECTED) || (Result == RESULT_BIT_ERROR_IN_ECC) || (Result == RESULT_DATA_RECOVERED)) {
          //
          // A bit error in ECC is not corrected by the ECC check routine.
          // The ECC must be re-computed in order to avoid propagating the bit
          // error to destination sector.
          //
          if (Result == RESULT_BIT_ERROR_IN_ECC) {
            r = _WriteSectorWithECC(pInst, _pSectorBuffer, SectorIndexDest0 + brsiPhyDest);
          } else {
            BytesPerPage      = pInst->BytesPerPage;
            BytesPerSpareArea = pInst->BytesPerSpareArea;
            r = _WriteDataSpare(pInst, SectorIndexDest0 + brsiPhyDest, _pSectorBuffer, BytesPerPage, _pSpareAreaData, BytesPerSpareArea);
          }
#if FS_NAND_VERIFY_WRITE
          if (r == 0) {
            r = _VerifySector(pInst, _pSectorBuffer, SectorIndexDest0 + brsiPhyDest);
          }
#endif
          if (r != 0) {
            Result = RESULT_WRITE_ERROR;          // Error, data could not be written.
          }
        }
        if ((Result == RESULT_UNCORRECTABLE_BIT_ERRORS) || (Result == RESULT_READ_ERROR) || (Result == RESULT_WRITE_ERROR)) {
          if (ErrorReported == 0) {
            ErrorBRSI     = iSector;
            ErrorReported = 1;                  // Remember that we encountered a fatal error.
          }
        } else if ((Result == RESULT_BIT_ERROR_IN_ECC) || (Result == RESULT_DATA_RECOVERED)) {
          if ((ErrorReported == 0) && (DataRecovered == 0)) {
            ErrorBRSI     = iSector;
            DataRecovered = 1;                  // Remember that we encountered a bit error but the data has been recovered.
          }
        } else {
          //
          // OK, sector data copied.
          //
          IF_STATS(pInst->StatCounters.CopySectorCnt++);
        }
      }
    }
    //
    // Update the logical to physical sector mapping.
    //
    _WB_WriteAssignment(pInst, pWorkBlock, iSector, brsiPhyDest);
    ++brsiPhyDest;
  }
  //
  // Update the work block information if the data has been
  // written successfully to destination block.
  //
  if ((ErrorReported == 0) || (Result != RESULT_WRITE_ERROR)) {
    pWorkBlock->pbi      = pbiDest;
    pWorkBlock->brsiFree = (U16)brsiPhyDest;
  }
  if (ErrorReported == 0) {
    //
    // Erase the block and put it in the free list if no bit errors were encountered.
    // In case of bit errors the block stays allocated and will be later marked as defective.
    //
    if (DataRecovered == 0) {
      (void)_FreeBlock(pInst, pbiSrc, EraseCntSrc);
    }
  }
Done:
  *pErrorBRSI = ErrorBRSI;
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NAND_UNI: MOVE_WORK_BLOCK pbiSrc: %u, pbiDest: %u, EraseCntDest: 0x%08x, r: %d\n", pbiSrc, pbiDest, EraseCntDest, Result));
  return Result;
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
*    ==0    An error occurred.
*    !=0    Physical block index.
*/
static unsigned _AllocErasedBlock(NAND_UNI_INST * pInst, U32 * pEraseCnt) {
  unsigned              pbiAlloc;
  unsigned              pbi;
  U32                   EraseCntAlloc;
  U32                   EraseCnt;
  int                   r;
  unsigned              ErrorBRSI;
  NAND_UNI_WORK_BLOCK * pWorkBlock;

  for (;;) {
    //
    // Passive wear leveling. Get the next free block in the row.
    //
    pbiAlloc = _PerformPassiveWearLeveling(pInst, &EraseCntAlloc);
    if (pbiAlloc == 0u) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_UNI: FATAL error: No more free blocks."));
      _OnFatalError(pInst, RESULT_OUT_OF_FREE_BLOCKS, 0);
      return 0;                   // Fatal error, out of free blocks
    }
    //
    // OK, we found a free block.
    // Now, let's check if the erase count is too high so we need to use active wear leveling.
    //
    pbi      = 0;
    EraseCnt = 0;
    if (pInst->ActiveWLStatus == ACTIVE_WL_ENABLED) {
      pbi = _CheckActiveWearLeveling(pInst, EraseCntAlloc, &EraseCnt);
    }
    if (pbi == 0u) {
      *pEraseCnt = EraseCntAlloc;       // No other data or work block has an erase count low enough. Keep the block allocated by the passive wear leveling.
      FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NAND_UNI: ALLOC_ERASED_BLOCK BlockIndex: %u, EraseCnt: 0x%08x\n", pbiAlloc, EraseCntAlloc));
      return pbiAlloc;
    }
    //
    // Check if the PBI is assigned to a work block.
    //
    pWorkBlock = pInst->pFirstWorkBlockInUse;
    for (;;) {
      if (pWorkBlock == NULL) {
        break;                          // No match
      }
      if (pWorkBlock->pbi == pbi) {
        break;                          // Found it
      }
      pWorkBlock = pWorkBlock->pNext;
    }
    //
    // Perform active wear leveling:
    // A block containing data has a much lower erase count. The data of this block is now moved to the free block,
    // giving us an other free block with a low erase count. This procedure makes sure that blocks which contain data
    // that does not change are equally erased.
    //
    if (pWorkBlock != NULL) {
      r = _MoveWorkBlock(pInst, pWorkBlock, pbiAlloc, EraseCntAlloc, &ErrorBRSI);
    } else {
      r = _MoveDataBlock(pInst, pbi, pbiAlloc, EraseCntAlloc, &ErrorBRSI);
    }
    if ((r == RESULT_NO_ERROR) || (r == RESULT_BIT_ERROR_IN_ECC) || (r == RESULT_DATA_RECOVERED)) {
      //
      // The data has been moved and the data block is now free to use.
      // We have to mark the block as allocated here since _MoveDataBlock()/_MoveWorkBlock() marked it as free.
      //
      _MarkBlockAsAllocated(pInst, pbi);
      //
      // The block has been erased one more time at the end of move operation.
      //
      ++EraseCnt;
      *pEraseCnt = EraseCnt;
      FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NAND_UNI: ALLOC_ERASED_BLOCK BlockIndex: %u, EraseCnt: 0x%08x\n", pbi, EraseCnt));
      return pbi;
    }
    if ((r == RESULT_UNCORRECTABLE_BIT_ERRORS) || (r == RESULT_READ_ERROR)) {
      if (_CanBlockBeMarkedAsBad(pInst, r) != 0) {
        (void)_MarkBlockAsBad(pInst, pbi, r, ErrorBRSI);
      } else {
        (void)_FreeBlock(pInst, pbi, EraseCnt);
      }
      return 0;                   // Fatal error, no way to recover
    }
    if (r == RESULT_WRITE_ERROR) {
      (void)_MarkBlockAsBad(pInst, pbiAlloc, r, ErrorBRSI);
      continue;                   // Error when writing into the allocated block
    }
  }
}

/*********************************************************************
*
*       _RecoverDataBlock
*
*  Function description
*    Copies a data block into a free block. Typ. called when an error is found in the ECC.
*
*  Parameters
*    pInst        [IN]  Driver instance.
*    pbiData      Index of the block to recover.
*
*  Return value
*    ==0    Data block saved.
*    !=0    An error occurred.
*/
static int _RecoverDataBlock(NAND_UNI_INST * pInst, unsigned pbiData) {
  unsigned pbiAlloc;
  U32      EraseCnt;
  int      r;
  unsigned ErrorBRSI;
  int      NumRetries;

  NumRetries = 0;
  for (;;) {
    //
    // Quit the write loop if the number of operations retried
    // has exceeded the configured limit.
    //
    if (NumRetries++ > FS_NAND_NUM_WRITE_RETRIES) {
      return 1;               // Error, cannot write sector data.
    }
    //
    // Need a free block where to move the data of the damaged block
    //
    pbiAlloc = _AllocErasedBlock(pInst, &EraseCnt);
    if (pbiAlloc == 0u) {
      return 1;               // Could not allocate an empty block, fatal error
    }
    r = _MoveDataBlock(pInst, pbiData, pbiAlloc, EraseCnt, &ErrorBRSI);
    if ((r == RESULT_UNCORRECTABLE_BIT_ERRORS) || (r == RESULT_READ_ERROR)) {
      if (_CanBlockBeMarkedAsBad(pInst, r) != 0) {
        (void)_MarkBlockAsBad(pInst, pbiData, r, ErrorBRSI);
      } else {
        (void)_FreeBlock(pInst, pbiData, ERASE_CNT_INVALID);
      }
      return 1;               // Fatal error, no way to recover
    }
    if (r == RESULT_WRITE_ERROR) {
      (void)_MarkBlockAsBad(pInst, pbiAlloc, r, ErrorBRSI);
      IF_STATS(pInst->StatCounters.NumWriteRetries++);
      continue;               // Error when writing into the allocated block
    }
    if ((r == RESULT_NO_ERROR) || (r == RESULT_BIT_ERROR_IN_ECC) || (r == RESULT_DATA_RECOVERED)) {
      return 0;               // Data moved into the new block.
    }
    IF_STATS(pInst->StatCounters.NumWriteRetries++);
  }
}

#if FS_NAND_MAX_BIT_ERROR_CNT

/*********************************************************************
*
*       _CheckSector
*
*  Function description
*    Checks the sector for any bit errors.
*
*   Parameters
*     pInst         [IN]  Driver instance.
*     SectorIndex   Index of the sector to read.
*     pErrorBRSI    [IN] Block-relative sector index of the on which
*                   an error occurred (if any).
*
*  Return value
*    RESULT_NO_ERROR                    OK, no bit errors detected
*    RESULT_BIT_ERRORS_CORRECTED        OK, bit errors were corrected but the data is OK
*    RESULT_BIT_ERROR_IN_ECC            OK, a single bit error was detected in the ECC
*    RESULT_UNCORRECTABLE_BIT_ERRORS    Error
*    RESULT_READ_ERROR                  Error
*/
static int _CheckSector(NAND_UNI_INST * pInst, U32 SectorIndex) {
  int        r;
  int        NumRetries;
  int        IsHW_ECCUsed;
  int        HasHW_ECC;
  unsigned   MaxNumBitsCorrected;
  U32      * pData;
  U8       * pDataSpare;
  unsigned   NumBytes;
  unsigned   NumBytesSpare;

  IsHW_ECCUsed  = (int)pInst->IsHW_ECCUsed;
  HasHW_ECC     = (int)pInst->HasHW_ECC;
  NumBytes      = pInst->BytesPerPage;
  NumBytesSpare = pInst->BytesPerSpareArea;
  pData         = _pSectorBuffer;
  pDataSpare    = _pSpareAreaData;
  if (IsHW_ECCUsed != 0) {
    if (HasHW_ECC != 0) {
      //
      // Do not transfer any data to host if the HW ECC of the NAND flash is used.
      //
      NumBytes      = 0;
      NumBytesSpare = 0;
    }
  }
  NumRetries = FS_NAND_NUM_READ_RETRIES;
  for (;;) {
    //
    // Read data and the entire spare area.
    //
    r = _ReadDataSpareEx(pInst, SectorIndex, pData, 0, NumBytes, pDataSpare, 0, NumBytesSpare);
    if (r != 0) {
      r = RESULT_READ_ERROR;                          // Re-read the sector in case of an error.
    } else {
      //
      // OK, read operation succeeded. If the NAND flash has HW ECC
      // and it is active no data correction has to be performed in the software.
      //
      if (IsHW_ECCUsed != 0) {                        // Is bit correction performed by the NAND flash or by a NAND flash controller?
        r = RESULT_NO_ERROR;
        if (_IsRelocationRequired(pInst, 0) != 0) {
          FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NAND_UNI: RELOCATION_REQUIRED SectorIndex: %lu, BlockIndex: %lu\n", SectorIndex, SectorIndex >> pInst->PPB_Shift));
          r = RESULT_BIT_ERRORS_CORRECTED;            // Bit errors where corrected and the block containing the page has to be relocated.
        }
        return r;
      }
      //
      // Check and correct bit errors of data and spare area.
      //
      MaxNumBitsCorrected = 0;
      r = _ApplyECC(pInst, pData, pDataSpare, &MaxNumBitsCorrected);
      if (r == RESULT_NO_ERROR) {
        return r;                                     // OK, data read.
      }
      if (r == RESULT_BIT_ERRORS_CORRECTED) {
        if (_IsRelocationRequired(pInst, MaxNumBitsCorrected) == 0) {
          r = RESULT_NO_ERROR;                        // The number of bit errors corrected is below the threshold. No relocation is necessary.
        } else {
          //
          // OK, data relocated.
          //
          FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NAND_UNI: RELOCATION_REQUIRED SectorIndex: %lu, BlockIndex: %lu\n", SectorIndex, SectorIndex >> pInst->PPB_Shift));
        }
        return r;                                     // OK, data read.
      }
    }
    if (NumRetries-- == 0) {
      break;                                          // No more retries.
    }
    FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NAND_UNI: CHECK_SECTOR SectorIndex: %lu, Retries: %d/%d, r: %d\n", SectorIndex, NumRetries, FS_NAND_NUM_READ_RETRIES, r));
    IF_STATS(pInst->StatCounters.NumReadRetries++);
  }
  return r;
}

/*********************************************************************
*
*       _CheckDataBlock
*
*  Function description
*    Checks a data block for any bit errors.
*
*   Parameters
*     pInst         [IN]  Driver instance.
*     BlockIndex    Index of the data block to be checked.
*     pErrorBRSI    [OUT] Block-relative sector index of the on which
*                   an error occurred (if any).
*
*  Return value
*    RESULT_NO_ERROR                    OK, no bit errors detected
*    RESULT_BIT_ERRORS_CORRECTED        OK, bit errors were corrected but the data is OK
*    RESULT_BIT_ERROR_IN_ECC            OK, a single bit error was detected in the ECC
*    RESULT_UNCORRECTABLE_BIT_ERRORS    Error
*    RESULT_READ_ERROR                  Error
*/
static int _CheckDataBlock(NAND_UNI_INST * pInst, unsigned BlockIndex, unsigned * pErrorBRSI) {
  int      r;
  U32      SectorIndex;
  unsigned NumSectors;
  unsigned iSector;

  r           = RESULT_NO_ERROR;
  if (BlockIndex >= PBI_STORAGE_START) {
    NumSectors  = 1uL << pInst->PPB_Shift;
    SectorIndex = _BlockIndex2SectorIndex0(pInst, BlockIndex);
    for (iSector = 0; iSector < NumSectors; ++iSector) {
      r = _CheckSector(pInst, SectorIndex);
      if (r != RESULT_NO_ERROR) {
        if (pErrorBRSI != NULL) {
          *pErrorBRSI = iSector;
        }
        FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NAND_UNI: CHECK_DATA_BLOCK SectorIndex: %lu, BlockIndex: %lu\n", SectorIndex, BlockIndex));
        break;
      }
      ++SectorIndex;
    }
  }
  return r;
}

/*********************************************************************
*
*       _CheckDataBlockWithER
*
*  Function description
*    Checks a data block for any bit errors.
*
*   Parameters
*     pInst         [IN]  Driver instance.
*     BlockIndex    Index of the data block to be checked.
*     EraseCnt      Erase count of the data block.
*
*  Return value
*    RESULT_NO_ERROR                    OK, no bit errors detected
*    RESULT_BIT_ERRORS_CORRECTED        OK, bit errors were corrected but the data is OK
*    RESULT_BIT_ERROR_IN_ECC            OK, a single bit error was detected in the ECC
*    RESULT_UNCORRECTABLE_BIT_ERRORS    Error
*    RESULT_READ_ERROR                  Error
*/
static int _CheckDataBlockWithER(NAND_UNI_INST * pInst, U32 BlockIndex, U32 EraseCnt) {
  int      r;
  unsigned ErrorBRSI;

  r = RESULT_NO_ERROR;
  if (pInst->HandleWriteDisturb != 0u) {
    if (pInst->MaxBitErrorCnt != 0u) {
      ErrorBRSI = 0;
      r = _CheckDataBlock(pInst, BlockIndex, &ErrorBRSI);
      if (r != RESULT_NO_ERROR) {
        (void)_RemoveDataBlock(pInst, BlockIndex);
        if ((r == RESULT_BIT_ERRORS_CORRECTED) || (r == RESULT_BIT_ERROR_IN_ECC) || (r == RESULT_DATA_RECOVERED)) {
          (void)_FreeBlock(pInst, BlockIndex, EraseCnt);
        } else {
          if (_CanBlockBeMarkedAsBad(pInst, r) != 0) {
            (void)_MarkBlockAsBad(pInst, BlockIndex, r, ErrorBRSI);
          } else {
            (void)_FreeBlock(pInst, BlockIndex, EraseCnt);
          }
        }
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _CheckWorkBlock
*
*  Function description
*    Checks a work block for any bit errors.
*
*   Parameters
*     pInst         [IN]  Driver instance.
*     pWorkBlock    [IN]  Work block to be checked.
*
*  Return value
*    RESULT_NO_ERROR                    OK, no bit errors detected
*    RESULT_BIT_ERRORS_CORRECTED        OK, bit errors were corrected but the data is OK
*    RESULT_BIT_ERROR_IN_ECC            OK, a single bit error was detected in the ECC
*    RESULT_UNCORRECTABLE_BIT_ERRORS    Error
*    RESULT_READ_ERROR                  Error
*/
static int _CheckWorkBlock(NAND_UNI_INST * pInst, const NAND_UNI_WORK_BLOCK * pWorkBlock) {
  int      r;
  U32      SectorIndex;
  unsigned NumSectors;
  unsigned iSector;
  unsigned BlockIndex;

  r = RESULT_NO_ERROR;
  if (pInst->HandleWriteDisturb != 0u) {
    if (pInst->MaxBitErrorCnt != 0u) {
      BlockIndex  = pWorkBlock->pbi;
      NumSectors  = pWorkBlock->brsiFree;
      SectorIndex = _BlockIndex2SectorIndex0(pInst, BlockIndex);
      for (iSector = 0; iSector < NumSectors; ++iSector) {
        r = _CheckSector(pInst, SectorIndex);
        if (r != RESULT_NO_ERROR) {
          FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NAND_UNI: CHECK_WORK_BLOCK SectorIndex: %lu, BlockIndex: %lu\n", SectorIndex, BlockIndex));
          break;
        }
        ++SectorIndex;
      }
    }
  }
  return r;
}

#endif // FS_NAND_MAX_BIT_ERROR_CNT

/*********************************************************************
*
*       _ConvertWorkBlock
*
*  Function description
*    Converts a work block into a data block. The data of the work block
*    is "merged" with the data of the source block in another free block.
*    The merging operation copies sector-wise the data from work block
*    into the free block. If the sector data is invalid in the work block
*    the sector data from the source block is copied instead. The sectors
*    in the work block doesn't have to be on their native positions.
*
*  Parameters
*    pInst        [IN]  Driver instance.
*    pWorkBlock   [IN]  Work block to convert.
*    brsiToSkip   Index of the sector to ignore when copying.
*    brsi         Sector index (relative to block) to be written (BRSI_INVALID means no sector data).
*    pData        [IN]  Sector data to be written (NULL means no sector data).
*
*  Return value
*    ==0    OK
*    !=0    An error occurred
*/
static int _ConvertWorkBlock(NAND_UNI_INST * pInst, NAND_UNI_WORK_BLOCK * pWorkBlock, unsigned brsiToSkip, unsigned brsi, const U32 * pData) {
  unsigned    iSector;
  unsigned    pbiSrc;
  unsigned    pbiWork;
  U32         SectorIndexSrc0;
  U32         SectorIndexWork0;
  U32         SectorIndexDest0;
  unsigned    SectorsPerBlock;
  U32         pbiDest;
  U32         EraseCntDest;
  U32         EraseCntSrc;
  int         r;
  unsigned    brsiSrc;
  unsigned    BlockCntSrc;
  unsigned    SectorStat;
  int         ErrorCodeSrc;
  int         ErrorCodeWork;
  unsigned    ErrorBRSI;
  const U32 * pSectorData;
  unsigned    lbi;
  int         CopyInvalidSector;
  unsigned    MergeCntSrc;
  int         ErrorReported;
  int         NumRetries;
  int         Pattern;

  EraseCntSrc     = ERASE_CNT_INVALID;
  EraseCntDest    = ERASE_CNT_INVALID;
  SectorsPerBlock = 1uL << pInst->PPB_Shift;
  ErrorBRSI       = 0;
  ErrorReported   = 0;
  NumRetries      = 0;
  for (;;) {
    ErrorCodeSrc  = RESULT_NO_ERROR;
    ErrorCodeWork = RESULT_NO_ERROR;
#if FS_NAND_SUPPORT_BLOCK_GROUPING
    //
    // If the block grouping is enabled we have to mark the block as "completed"
    // by writing the BRSI to the spare area of the last page in the block.
    // Otherwise the mount operation thinks that this is an incomplete
    // data block and discards it.
    //
    if (_IsBlockGroupingEnabled(pInst) != 0) {
      lbi              = pWorkBlock->lbi;
      pbiSrc           = (U16)_L2P_Read(pInst, lbi);
      SectorIndexSrc0  = _BlockIndex2SectorIndex0(pInst, pbiSrc);
      if (SectorIndexSrc0 != 0u) {              // Does a data block exist?
        _ClearStaticSpareArea(pInst);
        iSector = SectorsPerBlock - 1u;
        r = _ReadSectorWithECCAndER(pInst, _pSectorBuffer, SectorIndexSrc0 + iSector, iSector);
        if ((r == RESULT_UNCORRECTABLE_BIT_ERRORS) || (r == RESULT_READ_ERROR)) {
          //
          // Remember that a fatal error occurred. The data has been lost.
          //
          ErrorBRSI     = iSector;
          ErrorCodeSrc  = r;
          ErrorReported = 1;
        }
        if (ErrorReported == 0) {
          //
          // Sector data read successfully. Store the BRSI value if not already set.
          //
          brsiSrc = _LoadBRSI(pInst);
          if (brsiSrc == BRSI_INVALID) {
            //
            // Fill the sector data with a defined value.
            //
            FS_MEMSET(_pSectorBuffer, 0xFF, pInst->BytesPerPage);
            SectorStat = SECTOR_STAT_EMPTY;
            brsiSrc    = iSector;
            _ClearStaticSpareArea(pInst);
            _StoreBRSI(pInst, brsiSrc);
            _StoreSectorStat(pInst, SectorStat);
            //
            // Write data and spare area to destination sector.
            //
            r = _WriteSectorWithECC(pInst, _pSectorBuffer, SectorIndexSrc0 + iSector);
#if FS_NAND_VERIFY_WRITE
            if (r == 0) {
              r = _VerifySector(pInst, _pSectorBuffer, SectorIndexSrc0 + iSector);
            }
#endif // FS_NAND_VERIFY_WRITE
            if (r != 0) {
              r = _RecoverDataBlock(pInst, pbiSrc);
              if (r != 0) {
                return r;                       // Error, could not recover data block.
              }
              goto Retry;                       // Try again to store BRSI.
            }
          }
        }
      }
    }
#endif // FS_NAND_SUPPORT_BLOCK_GROUPING
    //
    // Allocate a new empty block to store the data from the work block.
    //
    pbiDest = _AllocErasedBlock(pInst, &EraseCntDest);
    if (pbiDest == 0u) {
      return 1;                                 // Error, no more free blocks, not recoverable.
    }
    //
    // OK, we have an empty block. Compute the sector index of the source and destination blocks.
    //
    lbi              = pWorkBlock->lbi;
    pbiSrc           = (U16)_L2P_Read(pInst, lbi);
    SectorIndexSrc0  = _BlockIndex2SectorIndex0(pInst, pbiSrc);
    SectorIndexDest0 = _BlockIndex2SectorIndex0(pInst, pbiDest);
    pbiWork          = (U16)pWorkBlock->pbi;
    SectorIndexWork0 = _BlockIndex2SectorIndex0(pInst, pbiWork);
    //
    // The second sector of a block has to be handled separately.
    // This sector stores block related information which must be written back to spare area.
    //
    iSector     = BRSI_BLOCK_INFO;
    BlockCntSrc = 0;
    MergeCntSrc = 0;
    brsiSrc     = _WB_ReadAssignment(pInst, pWorkBlock, iSector);
    //
    // Read data and spare area from the source sector.
    // The source sector can be located in a data block or in a work block.
    //
    if ((brsiSrc != 0u) && (brsiSrc != brsiToSkip)) {   // Data in work block?
      //
      // Read here the block count and the merge count of the data block.
      // It is required below when we write the data to destination block.
      //
      if (SectorIndexSrc0 != 0u) {                      // Does a data block exist?
        _ClearStaticSpareArea(pInst);
        r = _ReadSectorWithECCAndER(pInst, _pSectorBuffer, SectorIndexSrc0 + iSector, iSector);
        if ((r == RESULT_UNCORRECTABLE_BIT_ERRORS) || (r == RESULT_READ_ERROR)) {
          //
          // Remember that a fatal error occurred. The data has been lost.
          //
          ErrorBRSI     = iSector;
          ErrorCodeSrc  = r;
          ErrorReported = 1;
        }
        BlockCntSrc = _LoadBlockCnt(pInst);
        MergeCntSrc = _LoadMergeCnt(pInst);
      }
      //
      // Read the sector data from the work block.
      //
      _ClearStaticSpareArea(pInst);
      r = _ReadSectorWithECCAndER(pInst, _pSectorBuffer, SectorIndexWork0 + brsiSrc, iSector);
      if ((r == RESULT_UNCORRECTABLE_BIT_ERRORS) || (r == RESULT_READ_ERROR)) {
        //
        // Remember that a fatal error occurred. The data has been lost.
        //
        ErrorBRSI     = iSector;
        ErrorCodeWork = r;
        ErrorReported = 1;
      }
      SectorStat = _LoadSectorStat(pInst);
    } else if (SectorIndexSrc0 != 0u) {                 // Data in source block?
      //
      // Read the sector data from the data block.
      //
      _ClearStaticSpareArea(pInst);
      r = _ReadSectorWithECCAndER(pInst, _pSectorBuffer, SectorIndexSrc0 + iSector, iSector);
      if ((r == RESULT_UNCORRECTABLE_BIT_ERRORS) || (r == RESULT_READ_ERROR)) {
        //
        // Remember that a fatal error occurred. The data has been lost.
        //
        ErrorBRSI     = iSector;
        ErrorCodeSrc  = r;
        ErrorReported = 1;
      }
      BlockCntSrc = _LoadBlockCnt(pInst);
      SectorStat  = _LoadSectorStat(pInst);
      MergeCntSrc = _LoadMergeCnt(pInst);
    } else {
      //
      // Fill the sector data with a defined value.
      //
      FS_MEMSET(_pSectorBuffer, 0xFF, pInst->BytesPerPage);
      SectorStat = SECTOR_STAT_EMPTY;
    }
    //
    // If new data is written to a sector, then we set the correct sector status
    // and use the sector data passed as parameter. If no sector data is specified
    // then this means the sector has to be invalidated.
    //
    pSectorData = _pSectorBuffer;
    if (brsi == iSector) {
      if (pData == NULL) {                                // Does the sector have to be invalidated?
        Pattern = 0xFF;
        if (pInst->AllowBlankUnusedSectors == 0u) {       // Does sector data have to be set to 0?
          Pattern = 0x00;
        }
        FS_MEMSET(_pSectorBuffer, Pattern, pInst->BytesPerPage);
        SectorStat  = SECTOR_STAT_EMPTY;
      } else {
        SectorStat  = SECTOR_STAT_WRITTEN;
        pSectorData = pData;
      }
    }
    //
    // Store the block related information to spare area buffer.
    //
    _ClearStaticSpareArea(pInst);
    _StoreEraseCnt(pInst, EraseCntDest);
    _StoreBlockType(pInst, BLOCK_TYPE_DATA);
    _StoreBlockCnt(pInst, BlockCntSrc + 1u);
    _StoreSectorStat(pInst, SectorStat);
    _StoreLBI(pInst, lbi);
    _StoreMergeCnt(pInst, MergeCntSrc + 1u);
    //
    // Write data and spare area to destination sector.
    //
    r = _WriteSectorWithECC(pInst, pSectorData, SectorIndexDest0 + iSector);
#if FS_NAND_VERIFY_WRITE
    if (r == 0) {
      r = _VerifySector(pInst, pSectorData, SectorIndexDest0 + iSector);
    }
#endif // FS_NAND_VERIFY_WRITE
    if (r != 0) {
      (void)_MarkBlockAsBad(pInst, pbiDest, RESULT_WRITE_ERROR, iSector);
      goto Retry;               // Write error occurred, try to find another empty block.
    }

    //
    // Fail-safe TP
    //
    // At this point we have 2 data blocks with the same LBI. The copy operation is not complete.
    // Low-level mount should throw away the latest version of the data block.
    //
    CALL_TEST_HOOK_FAIL_SAFE(pInst->Unit);

    //
    // Copy the data of the sectors left.
    //
    ++iSector;
    for (; iSector < SectorsPerBlock; iSector++) {
      //
      // Sector data can be:
      //   - valid and passed as parameter
      //   - valid and stored in work block
      //   - valid and stored in source block
      //   - invalid
      //
      brsiSrc = _WB_ReadAssignment(pInst, pWorkBlock, iSector);
      if (brsi == iSector) {
        if (pData == NULL) {                                    // Does the sector have to be invalidated?
          Pattern = 0xFF;
          if (pInst->AllowBlankUnusedSectors == 0u) {           // Does sector data have to be set to 0?
            Pattern = 0x00;
          }
          FS_MEMSET(_pSectorBuffer, Pattern, pInst->BytesPerPage);
          SectorStat  = SECTOR_STAT_EMPTY;
          pSectorData = _pSectorBuffer;
        } else {
          SectorStat  = SECTOR_STAT_WRITTEN;
          pSectorData = pData;
        }
        _ClearStaticSpareArea(pInst);
        _StoreSectorStat(pInst, SectorStat);
#if FS_NAND_SUPPORT_BLOCK_GROUPING
        //
        // If the block grouping is enabled we write the BRSI to the last sector in the block
        // in order to indicate that the merge operation is completed. The value is used
        // at low-level mount to determine which block has to be discarded when duplicated
        // data blocks are found.
        //
        if (_IsBlockGroupingEnabled(pInst) != 0) {
          if (iSector == (SectorsPerBlock - 1u)) {
            _StoreBRSI(pInst, iSector);
          }
        }
#endif // FS_NAND_SUPPORT_BLOCK_GROUPING
        r = _WriteSectorWithECC(pInst, pSectorData, SectorIndexDest0 + iSector);
#if FS_NAND_VERIFY_WRITE
        if (r == 0) {
          r = _VerifySector(pInst, pSectorData, SectorIndexDest0 + iSector);
        }
#endif // FS_NAND_VERIFY_WRITE
        if (r != 0) {
          (void)_MarkBlockAsBad(pInst, pbiDest, RESULT_WRITE_ERROR, iSector);
          goto Retry;                                           // Write error occurred, try to find another empty block.
        }
      } else if ((brsiSrc != 0u) && (brsiSrc != brsiToSkip)) {  // Sector data in work block ?
        r = _CopySectorWithECC(pInst, SectorIndexWork0 + brsiSrc, SectorIndexDest0 + iSector, iSector);
        if ((r == RESULT_NO_ERROR) || (r == RESULT_BIT_ERRORS_CORRECTED)) {
          continue;
        }
        if ((r == RESULT_UNCORRECTABLE_BIT_ERRORS) || (r == RESULT_READ_ERROR)) {
          //
          // Remember that a fatal error occurred. The data has been lost.
          //
          ErrorBRSI     = iSector;
          ErrorCodeWork = r;
          ErrorReported = 1;
        }
        if (r == RESULT_WRITE_ERROR) {
          (void)_MarkBlockAsBad(pInst, pbiDest, r, iSector);
          goto Retry;                                           // Write error occurred, try to find another empty block
        }
      } else if (SectorIndexSrc0 != 0u) {                       // In source block ?
        //
        // Copy if we have a data source.
        // Note that when closing a work block which did not yet have a source data block,
        // it can happen that some sector have no source and stay empty.
        //
        r = _CopySectorWithECC(pInst, SectorIndexSrc0 + iSector, SectorIndexDest0 + iSector, iSector);
        if ((r == RESULT_NO_ERROR) || (r == RESULT_BIT_ERRORS_CORRECTED)) {
          continue;
        }
        if ((r == RESULT_UNCORRECTABLE_BIT_ERRORS) || (r == RESULT_READ_ERROR)) {
          //
          // Remember that a fatal error occurred. The data has been lost.
          //
          ErrorBRSI     = iSector;
          ErrorCodeSrc  = r;
          ErrorReported = 1;
          continue;
        }
        if ((r == RESULT_BIT_ERROR_IN_ECC) || (r == RESULT_DATA_RECOVERED)) {
          //
          // Do not mark the sector as defective for these types of errors.
          //
          continue;
        }
        if (r == RESULT_WRITE_ERROR) {
          (void)_MarkBlockAsBad(pInst, pbiDest, r, iSector);
          goto Retry;                                     // Write error occurred, try to find another empty block.
        }
      } else {
        CopyInvalidSector = 0;
        if (pInst->AllowBlankUnusedSectors == 0u) {       // Should sector data be set to 0?
          CopyInvalidSector = 1;
        } else {
#if FS_NAND_SUPPORT_BLOCK_GROUPING
          if (_IsBlockGroupingEnabled(pInst) != 0) {
            if (iSector == (SectorsPerBlock - 1u)) {      // Is the merge operation is completed?
              CopyInvalidSector = 1;
            }
          }
#endif // FS_NAND_SUPPORT_BLOCK_GROUPING
        }
        if (CopyInvalidSector != 0) {
          //
          // Initialize the sector data with 0's to reduce the chance of bit errors.
          //
          FS_MEMSET(_pSectorBuffer, 0x00, pInst->BytesPerPage);
          _ClearStaticSpareArea(pInst);
          //
          // If the block grouping is enabled write the LBI to the last sector in the block
          // in order to indicate that the merge operation is completed. The value is used
          // at low-level mount to determine which block has to be discarded when duplicated
          // data blocks are found.
          //
          if (iSector == (SectorsPerBlock - 1u)) {
            _StoreBRSI(pInst, iSector);
          }
          r = _WriteSectorWithECC(pInst, _pSectorBuffer, SectorIndexDest0 + iSector);
#if FS_NAND_VERIFY_WRITE
          if (r == 0) {
            r = _VerifySector(pInst, _pSectorBuffer, SectorIndexDest0 + iSector);
          }
#endif // FS_NAND_VERIFY_WRITE
          if (r != 0) {
            (void)_MarkBlockAsBad(pInst, pbiDest, RESULT_WRITE_ERROR, iSector);
            goto Retry;                           // Write error occurred, try to find another empty block.
          }
        }
      }
    }
#if FS_NAND_MAX_BIT_ERROR_CNT
    //
    // Check if the created data block contains any bit errors and if so handle them.
    //
    {
      r = _CheckDataBlockWithER(pInst, pbiDest, EraseCntDest);
      if (r != RESULT_NO_ERROR) {
        goto Retry;                               // Convert again the work block.
      }
    }
#endif // FS_NAND_MAX_BIT_ERROR_CNT
    break;                      // OK, the sectors have been copied.
Retry:
    if (NumRetries++ > FS_NAND_NUM_WRITE_RETRIES) {
      return 1;                                 // Error, too many write retries.
    }
  }
  //
  // Copy operation done. Validate the new data and then invalidate the old data.
  //
  if (pbiSrc != 0u) {
    //
    // This operation has to be done before assigning the new block index to LBI via _L2P_Write()
    // since RemoveDataBlock() also modifies the mapping table.
    //
    (void)_RemoveDataBlock(pInst, pbiSrc);
  }
  //
  // Update the mapping of physical to logical blocks.
  //
  _L2P_Write(pInst, lbi, pbiDest);
  //
  // Free the old data block and the work block. Note that the order of the operations are important.
  // The data block must be freed first to ensure the data is not lost after an unexpected reset.
  //
  if (pbiSrc != 0u) {
    if (_CanBlockBeMarkedAsBad(pInst, ErrorCodeSrc) != 0) {
      (void)_MarkBlockAsBad(pInst, pbiSrc, ErrorCodeSrc, ErrorBRSI);    // There was a bit error in one of the sectors in the data block. The block is no more usable.
    } else {
      (void)_FreeBlock(pInst, pbiSrc, EraseCntSrc);                     // Free the old data block (if there was one).
    }
  }
  if (_CanBlockBeMarkedAsBad(pInst, ErrorCodeWork) != 0) {
    (void)_MarkBlockAsBad(pInst, pbiWork, ErrorCodeWork, ErrorBRSI);    // There was a bit error in one of the sectors in the work block. The block is no more usable.
  } else {
    (void)_FreeBlock(pInst, pbiWork, ERASE_CNT_INVALID);                // Free work block.
  }
  //
  // Remove the work block from the internal list.
  //
  _WB_RemoveFromUsedList(pInst, pWorkBlock);
  _WB_AddToFreeList(pInst, pWorkBlock);
  //
  // If required, update the information used for active wear leveling.
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
  return ErrorReported;
}

/*********************************************************************
*
*       _CleanWorkBlock
*
*  Function description
*    Closes the work buffer.
*    - Converts work block into normal data buffer by copy all data into it and marking it as data block
*    - Invalidates and mark as free the block which contained the same logical data area before
*
*  Parameters
*    pInst        [IN]  Driver instance.
*    pWorkBlock   [IN]  Work block to be cleaned.
*    brsi         Sector index (relative to block) to be written (BRSI_INVALID means no sector data).
*    pData        [IN]  Sector data to be written (NULL means no sector data).
*
*  Return value
*    ==0    OK
*    !=0    An error occurred.
*/
static int _CleanWorkBlock(NAND_UNI_INST * pInst, NAND_UNI_WORK_BLOCK * pWorkBlock, unsigned brsi, const U32 * pData) {
  int r;

  if (_WB_HasValidSectors(pInst, pWorkBlock) == 0) {    // No valid sectors in the work block ?
    (void)_FreeBlock(pInst, pWorkBlock->pbi, ERASE_CNT_INVALID);
    //
    // Remove the work block from the internal list
    //
    _WB_RemoveFromUsedList(pInst, pWorkBlock);
    _WB_AddToFreeList(pInst, pWorkBlock);
    return 0;
  }
  //
  // Convert the work block by merging it with the corresponding source block.
  //
  r = _ConvertWorkBlock(pInst, pWorkBlock, 0, brsi, pData);
  return r;
}

/*********************************************************************
*
*       _CleanWorkBlockIfAllowed
*
*  Function description
*    Performs the same operation as _CleanWorkBlock() indirectly,
*    via a function pointer.
*/
static int _CleanWorkBlockIfAllowed(NAND_UNI_INST * pInst, NAND_UNI_WORK_BLOCK * pWorkBlock, unsigned brsi, const U32 * pData) {
  int r;

  r = 1;            // Set to indicate failure.
  if (pInst->pWriteAPI != NULL) {
    r = pInst->pWriteAPI->pfCleanWorkBlock(pInst, pWorkBlock, brsi, pData);
  }
  return r;
}

/*********************************************************************
*
*       _CleanLastWorkBlock
*
*  Function description
*    Removes the least recently used work block from list of work blocks and converts it into data block
*/
static int _CleanLastWorkBlock(NAND_UNI_INST * pInst) {
  NAND_UNI_WORK_BLOCK * pWorkBlock;
  int                   r;
  //
  // Find last work block in list
  //
  pWorkBlock = pInst->pFirstWorkBlockInUse;
  if (pWorkBlock == NULL) {
    return 0;
  }
  while (pWorkBlock->pNext != NULL) {
    pWorkBlock = pWorkBlock->pNext;
  }
  r = _CleanWorkBlockIfAllowed(pInst, pWorkBlock, BRSI_INVALID, NULL);
  return r;
}

#if FS_NAND_SUPPORT_CLEAN

/*********************************************************************
*
*       _CleanAllWorkBlocks
*
*  Function description
*    Closes all work blocks.
*/
static int _CleanAllWorkBlocks(NAND_UNI_INST * pInst) {
  int r;

  r = 0;
  while (pInst->pFirstWorkBlockInUse != NULL) {
    r = _CleanWorkBlock(pInst, pInst->pFirstWorkBlockInUse, BRSI_INVALID, NULL);
    if (r != 0) {
      break;
    }
  }
  return r;
}

#endif  // FS_NAND_SUPPORT_CLEAN

/*********************************************************************
*
*       _CleanWorkBlockIfPossible
*
*  Function description
*    Converts a work block to a data block if the NAND flash is not write protected.
*/
static int _CleanWorkBlockIfPossible(NAND_UNI_INST * pInst, NAND_UNI_WORK_BLOCK * pWorkBlock) {
  int r;

  r = 0;                  // Set to indicate success.
  if (pInst->IsWriteProtected == 0u) {
    r = _CleanWorkBlockIfAllowed(pInst, pWorkBlock, BRSI_INVALID, NULL);
  }
  return r;
}

/*********************************************************************
*
*       _RecoverDataBlockIfAllowed
*
*  Function description
*    Performs the same operation as _RecoverDataBlock() indirectly,
*    via a function pointer.
*/
static int _RecoverDataBlockIfAllowed(NAND_UNI_INST * pInst, unsigned pbiData) {
  int r;

  r = 1;            // Set to indicate failure.
  if (pInst->pWriteAPI != NULL) {
    r = pInst->pWriteAPI->pfRecoverDataBlock(pInst, pbiData);
  }
  return r;
}

/*********************************************************************
*
*       _RelocateDataBlock
*
*  Function description
*    Copies the contents of a data block to an other free NAND flash block.
*/
static int _RelocateDataBlock(NAND_UNI_INST * pInst, unsigned pbi) {
  int r;

  r = _RecoverDataBlockIfAllowed(pInst, pbi);
  IF_STATS(pInst->StatCounters.BlockRelocationCnt++);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NAND_UNI: RELOCATE_DATA BlockIndex: %lu\n", pbi));
  return r;
}

/*********************************************************************
*
*       _RelocateWorkBlock
*
*  Function description
*    Copies the contents of a work block to an other free NAND flash block.
*/
static int _RelocateWorkBlock(NAND_UNI_INST * pInst, NAND_UNI_WORK_BLOCK * pWorkBlock) {
  int r;

  r = _CleanWorkBlockIfPossible(pInst, pWorkBlock);
  IF_STATS(pInst->StatCounters.BlockRelocationCnt++);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NAND_UNI: RELOCATE_WORK BlockIndex: %lu\n", pWorkBlock->pbi));
  return r;
}

/*********************************************************************
*
*       _AllocWorkBlock
*
*  Function description
*   - Allocates a NAND_UNI_WORK_BLOCK management entry from the array in the pInst structure
*   - Finds a free block and assigns it to the NAND_UNI_WORK_BLOCK
*
*  Parameters
*    pInst        [IN]  Driver instance.
*    lbi          Logical block index assigned to work block.
*    pEraseCnt    [OUT] Number of times the allocated block has been erased.
*
*  Return values
*    !=NULL   Pointer to allocated work block.
*    ==NULL   An error occurred, typ. a fatal error.
*/
static NAND_UNI_WORK_BLOCK * _AllocWorkBlock(NAND_UNI_INST * pInst, unsigned lbi, U32 * pEraseCnt) {
  NAND_UNI_WORK_BLOCK * pWorkBlock;
  U32                   EraseCnt;
  unsigned              pbi;
  int                   r;
  U32                   EraseCntMin;
  U32                   NumBlocksEraseCntMin;

  pWorkBlock = _AllocWorkBlockDesc(pInst, lbi);
  if (pWorkBlock == NULL) {
    r = _CleanLastWorkBlock(pInst);
    if (r != 0) {
      return NULL;                  // Error, could not clean the last work block in use. This is a fatal error.
    }
    pWorkBlock = _AllocWorkBlockDesc(pInst, lbi);
    if (pWorkBlock == NULL) {
      return NULL;                  // Error, could not allocate a new work block. This is a fatal error.
    }
  }
  //
  // Find an empty block to write to.
  //
  pbi = _AllocErasedBlock(pInst, &EraseCnt);
  if (pbi == 0u) {
    //
    // _AllocWorkBlockDesc() moved the work block descriptor to the list of used blocks.
    // We have to move the work block descriptor back to the list of free work blocks
    // since the index of the physical block stored in it is not valid. That is the index
    // of the physical block is set to 0 which means that if the work block is used the
    // Universal NAND driver will write to the first physical block used as storage.
    // But the first phy. block is reserved for format and error information and it cannot
    // be used for data storage.
    //
    _WB_RemoveFromUsedList(pInst, pWorkBlock);
    _WB_AddToFreeList(pInst, pWorkBlock);
    return NULL;                    // Error, could not find a free phy. block. This is a fatal error.
  }
  //
  // OK, new work block allocated.
  //
  pWorkBlock->pbi = pbi;
  *pEraseCnt      = EraseCnt;
  //
  // Update the wear leveling information.
  //
  EraseCntMin          = pInst->EraseCntMin;
  NumBlocksEraseCntMin = pInst->NumBlocksEraseCntMin;
  if (EraseCnt < EraseCntMin) {
    EraseCntMin          = EraseCnt;
    NumBlocksEraseCntMin = 1;
  } else {
    if (EraseCnt == EraseCntMin) {
      ++NumBlocksEraseCntMin;
    }
  }
  pInst->EraseCntMin          = EraseCntMin;
  pInst->NumBlocksEraseCntMin = NumBlocksEraseCntMin;
  return pWorkBlock;
}

/*********************************************************************
*
*       _FindWorkBlock
*
*  Function description
*    Tries to locate a work block for a given logical block.
*/
static NAND_UNI_WORK_BLOCK * _FindWorkBlock(const NAND_UNI_INST * pInst, unsigned lbi) {
  NAND_UNI_WORK_BLOCK * pWorkBlock;

  //
  // Iterate over used-list
  //
  pWorkBlock = pInst->pFirstWorkBlockInUse;
  for (;;) {
    if (pWorkBlock == NULL) {
      break;                          // No match
    }
    if (pWorkBlock->lbi == lbi) {
      break;                          // Found it
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
static void _MarkWorkBlockAsMRU(NAND_UNI_INST * pInst, NAND_UNI_WORK_BLOCK * pWorkBlock) {
  if (pWorkBlock != pInst->pFirstWorkBlockInUse) {
    _WB_RemoveFromUsedList(pInst, pWorkBlock);
    _WB_AddToUsedList(pInst, pWorkBlock);
  }
}

#if FS_NAND_SUPPORT_FAST_WRITE

/*********************************************************************
*
*       _FindDataBlock
*
*  Function description
*    Tries to locate a data block descriptor for the specified physical block index.
*/
static NAND_UNI_DATA_BLOCK * _FindDataBlock(const NAND_UNI_INST * pInst, unsigned pbi) {
  NAND_UNI_DATA_BLOCK * pDataBlock;

  //
  // Iterate over used-list
  //
  pDataBlock = pInst->pFirstDataBlockInUse;
  for (;;) {
    if (pDataBlock == NULL) {
      break;                          // No match
    }
    if (pDataBlock->pbi == pbi) {
      break;                          // Found it
    }
    pDataBlock = pDataBlock->pNext;
  }
  return pDataBlock;
}

#endif // FS_NAND_SUPPORT_FAST_WRITE

/*********************************************************************
*
*       _ReadBlockInfo
*
*  Function description
*    Reads the block related information from the spare area.
*
*  Return value
*    RESULT_NO_ERROR                    OK
*    RESULT_BIT_ERRORS_CORRECTED        OK
*    RESULT_BIT_ERROR_IN_ECC            OK
*    RESULT_UNCORRECTABLE_BIT_ERRORS    Error
*    RESULT_READ_ERROR                  Error
*
*  Additional information
*    The data is stored to static spare area buffer _pSpareAreaData.
*    If the read optimization for the spare area is active the
*    function reads only the ranges of the spare area that store
*    the erase count, the LBI and the block type. All other bytes int
*    the static spare area buffer are set to 0xFF.
*/
static int _ReadBlockInfo(NAND_UNI_INST * pInst, U32 SectorIndex) {
  int r;

#if (FS_NAND_OPTIMIZE_SPARE_AREA_READ == 0)
  r = _ReadSpareAreaWithECC(pInst, SectorIndex);
#else
  pInst->ActiveSpareAreaRanges = (U8)(SPARE_RANGE_ERASE_CNT | SPARE_RANGE_LBI);
  r = _ReadSpareAreaWithECC(pInst, SectorIndex);
  pInst->ActiveSpareAreaRanges = 0;
#endif
  return r;
}

/*********************************************************************
*
*       _ReadSectorInfo
*
*  Function description
*    Reads management data of a sector stored in a work block.
*
*  Return value
*    RESULT_NO_ERROR                    OK
*    RESULT_BIT_ERRORS_CORRECTED        OK
*    RESULT_BIT_ERROR_IN_ECC            OK
*    RESULT_UNCORRECTABLE_BIT_ERRORS    Error
*    RESULT_READ_ERROR                  Error
*/
static int _ReadSectorInfo(NAND_UNI_INST * pInst, U32 SectorIndex) {
  int r;

#if (FS_NAND_OPTIMIZE_SPARE_AREA_READ == 0)
  r = _ReadSpareAreaWithECC(pInst, SectorIndex);
#else
  {
    unsigned ActiveSpareAreaRanges;

    ActiveSpareAreaRanges = SPARE_RANGE_BRSI;
#if FS_NAND_SUPPORT_BLOCK_GROUPING
    if (_IsBlockGroupingEnabled(pInst) != 0) {
      unsigned brsi;
      unsigned SectorsPerBlock;

      SectorsPerBlock = 1uL << pInst->PPB_Shift;
      brsi            = SectorIndex & (SectorsPerBlock - 1u);
      if (brsi == BRSI_BLOCK_INFO) {
        ActiveSpareAreaRanges |= SPARE_RANGE_ERASE_CNT | SPARE_RANGE_LBI;
      }
    }
#endif // FS_NAND_SUPPORT_BLOCK_GROUPING
    pInst->ActiveSpareAreaRanges = (U8)ActiveSpareAreaRanges;
    r = _ReadSpareAreaWithECC(pInst, SectorIndex);
    pInst->ActiveSpareAreaRanges = 0;
  }
#endif
  return r;
}

#if FS_NAND_SUPPORT_BLOCK_GROUPING

/*********************************************************************
*
*       _FreeWorkBlock
*
*  Function description
*    Puts the block in the free list and erases it.
*/
static int _FreeWorkBlock(NAND_UNI_INST * pInst, NAND_UNI_WORK_BLOCK * pWorkBlock, U32 EraseCnt) {
  int      r;
  unsigned pbi;

  pbi = pWorkBlock->pbi;
  _WB_RemoveFromUsedList(pInst, pWorkBlock);
  _WB_AddToFreeList(pInst, pWorkBlock);
  r = _FreeBlock(pInst, pbi, EraseCnt);
  return r;
}

/*********************************************************************
*
*       _FreeWorkBlockIfAllowed
*
*  Function description
*    Performs the same operation as _FreeWorkBlock() indirectly,
*    via a function pointer.
*/
static int _FreeWorkBlockIfAllowed(NAND_UNI_INST * pInst, NAND_UNI_WORK_BLOCK * pWorkBlock, U32 EraseCnt) {
  int r;

  r = 1;              // Set to indicate failure.
  if (pInst->pWriteAPI != NULL) {
    r = pInst->pWriteAPI->pfFreeWorkBlock(pInst, pWorkBlock, EraseCnt);
  }
  return r;
}

#endif // FS_NAND_SUPPORT_BLOCK_GROUPING

/*********************************************************************
*
*       _FreeBadBlock
*
*  Function description
*    Marks block as defective and frees it.
*/
static int _FreeBadBlock(NAND_UNI_INST * pInst, unsigned pbi, int ErrorType, U32 ErrorBRSI) {
  int r;

  if (_CanBlockBeMarkedAsBad(pInst, ErrorType) != 0) {
    r = _MarkBlockAsBad(pInst, pbi, ErrorType, ErrorBRSI);
  } else {
    r = _FreeBlock(pInst, pbi, ERASE_CNT_INVALID);
  }
  return r;
}

/*********************************************************************
*
*       _FreeBadBlockIfAllowed
*
*  Function description
*    Performs the same operation as _FreeBadBlock() indirectly,
*    via a function pointer.
*/
static int _FreeBadBlockIfAllowed(NAND_UNI_INST * pInst, unsigned pbi, int ErrorType, U32 ErrorBRSI) {
  int r;

  r = 1;
  if (pInst->pWriteAPI != NULL) {
    r = pInst->pWriteAPI->pfFreeBadBlock(pInst, pbi, ErrorType, ErrorBRSI);
  }
  return r;
}

/*********************************************************************
*
*       _LoadWorkBlock
*
*  Function description
*    Reads management data of work block.
*
*  Parameters
*    pInst        Driver instance.
*    pWorkBlock   Work block descriptor.
*
*  Return value
*    ==0    OK, work block information loaded.
*    !=0    An error occurred.
*
*  Additional information
*    This function is used during low-level mount only,
*    since at all other times, the work block descriptors
*    are up to date.
*/
static int _LoadWorkBlock(NAND_UNI_INST * pInst, NAND_UNI_WORK_BLOCK * pWorkBlock) {
  unsigned SectorsPerBlock;
  unsigned iSector;
  U32      SectorIndex0;
  unsigned brsi;
  U32      pbiWork;
  unsigned brsiFree;
  U32      SectorIndexSrc;
  int      r;
  int      IsRelocationRequired;
  int      ErrorCode;
  unsigned ErrorBRSI;

  r                    = 0;
  pbiWork              = pWorkBlock->pbi;
  SectorsPerBlock      = 1uL << pInst->PPB_Shift;
  SectorIndex0         = _BlockIndex2SectorIndex0(pInst, pbiWork);
  iSector              = 0;
  IsRelocationRequired = 0;
  ErrorCode            = RESULT_NO_ERROR;
  ErrorBRSI            = 0;
  //
  // Iterate over all sectors, reading spare info in order to find out if sector contains data and if so which data.
  //
  ++iSector;            // First writable sector in an empty work block.
  brsiFree = iSector;
  for (; iSector < SectorsPerBlock; iSector++) {
    SectorIndexSrc = SectorIndex0 + iSector;
    //
    // Check if this sector of the work block contains valid data.
    //
    r = _ReadSectorInfo(pInst, SectorIndexSrc);
    if ((r == RESULT_NO_ERROR) || (r == RESULT_BIT_ERRORS_CORRECTED) || (r == RESULT_BIT_ERROR_IN_ECC)) {
      brsi = _LoadBRSI(pInst);
      if (r == RESULT_BIT_ERRORS_CORRECTED) {
        IsRelocationRequired = 1;
      }
#if FS_NAND_SUPPORT_BLOCK_GROUPING
      //
      // If the block grouping is enabled, check if the work block has been properly erased.
      // We do this by comparing the merge counts of the work block and of the corresponding data block.
      // If they are not equal it means that the work block was not properly erased.
      // If this is the case we erase the work block here and inform the caller about this.
      //
      if (_IsBlockGroupingEnabled(pInst) != 0) {
        if (iSector == BRSI_BLOCK_INFO) {
          unsigned lbi;
          unsigned pbiSrc;
          unsigned MergeCntWork;
          unsigned MergeCntSrc;
          U32      EraseCnt;

          lbi = pWorkBlock->lbi;
          pbiSrc = _L2P_Read(pInst, lbi);
          if (pbiSrc != 0u) {
            MergeCntWork    = _LoadMergeCnt(pInst);
            MergeCntSrc     = MergeCntWork;
            EraseCnt        = _LoadEraseCnt(pInst);
            SectorIndexSrc  = _BlockIndex2SectorIndex0(pInst, pbiSrc);
            SectorIndexSrc |= BRSI_BLOCK_INFO;
            r = _ReadMergeCnt(pInst, SectorIndexSrc, &MergeCntSrc);   // Note: the static buffer of the spare area will be overwritten during this function call.
            if (r == RESULT_BIT_ERRORS_CORRECTED) {
              r = _RelocateDataBlock(pInst, pbiSrc);
              if (r != 0) {
                goto Done;                                            // Error could not relocate data block.
              }
            }
            if (MergeCntWork != MergeCntSrc) {
              (void)_FreeWorkBlockIfAllowed(pInst, pWorkBlock, EraseCnt);
              r = 0;                                                  // OK, the work block has been erased and removed from the used list.
              goto Done;
            }
          }
        }
      }
#endif // FS_NAND_SUPPORT_BLOCK_GROUPING
      //
      // Check if this is the last page in the block.
      //
      if (brsi > SectorsPerBlock) {
        if (_IsPageBlank(pInst, SectorIndexSrc) != 0) {
          break;              // OK, found an empty sector. This is the first sector we can write to.
        }
        //
        // The page has an invalid sector index and it is not blank. Something went wrong.
        //
        FS_DEBUG_WARN((FS_MTYPE_DRIVER, "NAND_UNI: First free sector in work block is not blank."));
        r = _CleanWorkBlockIfPossible(pInst, pWorkBlock);
        goto Done;
      }
      //
      // Sector 0 does not store any data and it is invalid as BRSI.
      //
      if (brsi == 0u) {
        FS_DEBUG_WARN((FS_MTYPE_DRIVER, "NAND_UNI: Invalid sector index 0 found in work block."));
      } else {
        _WB_WriteAssignment(pInst, pWorkBlock, brsi, iSector);
      }
    } else {
      //
      // Remember that a block relocation has to be performed
      // and that the block has to be marked as defective.
      //
      IsRelocationRequired = 1;
      ErrorCode            = r;
      ErrorBRSI            = iSector;
    }
    ++brsiFree;
    pWorkBlock->brsiFree = (U16)brsiFree;
  }
  //
  // Convert the work block to a data block if the number of corrected
  // error passed the configured threshold.
  //
  if (IsRelocationRequired != 0) {
    r = _RelocateWorkBlock(pInst, pWorkBlock);
    if (ErrorCode != RESULT_NO_ERROR) {
      if (_IsBlockBad(pInst, pbiWork) == 0) {
        (void)_FreeBadBlockIfAllowed(pInst, pbiWork, ErrorCode, ErrorBRSI);
        r = 1;                // Error, the data in work block is corrupted.
      }
    }
    goto Done;
  }
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  //
  // On higher debug levels check whether the free sectors in the work block are blank.
  //
  for (; iSector < SectorsPerBlock; ++iSector) {
    SectorIndexSrc = SectorIndex0 + iSector;
    if (_IsPageBlank(pInst, SectorIndexSrc) == 0) {
      FS_DEBUG_WARN((FS_MTYPE_DRIVER, "NAND_UNI: Found free sector in work block which is not blank."));
      r = _CleanWorkBlockIfPossible(pInst, pWorkBlock);
      goto Done;
    }
  }
#endif  // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
Done:
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NAND_UNI: LOAD_WB BI: %d, LBI: %d, FreeBRSI: %d, IsRR: %d, r: %d\n", pWorkBlock->pbi, pWorkBlock->lbi, pWorkBlock->brsiFree, IsRelocationRequired, r));
  return r;
}

/*********************************************************************
*
*       _IsBlockMoreRecent
*
*  Function description
*    Used during low-level mount only to determine which version of a data block is newer.
*
*  Return value
*    ==0    pbiPrev is older.
*    ==1    pbiPrev is newer.
*
*  Notes
*    (1) Overwrites the contents of the static spare area.
*/
static int _IsBlockMoreRecent(NAND_UNI_INST * pInst, U32 pbiPrev) {
  unsigned BlockCnt;
  unsigned BlockCntPrev;
  unsigned BlockCntDiff;
  int      r;

  //
  // Get the data count of the current block from the static spare area.
  //
  BlockCnt = _LoadBlockCnt(pInst);
  //
  // Read the data count of the previous block form storage.
  //
  r = _ReadBlockCnt(pInst, pbiPrev, &BlockCntPrev);
  if (r != 0) {
    return 0;       // psiPrev is older.
  }
  BlockCntDiff = (BlockCntPrev - BlockCnt) & 0xFu;
  if (BlockCntDiff == 1u) {
    return 1;       // pbiPrev is newer.
  }
  return 0;         // pbiPrev is older.
}

/*********************************************************************
*
*       _SkipLeadingBadBlocks
*
*  Function description
*    Move the start of NAND partition (FirstBlock)
*    to skip over eventual leading bad blocks.
*
*  Return value
*    ==0    OK, found a block which is not bad
*    !=0    Error, all the blocks in the partition are bad
*/
static int _SkipLeadingBadBlocks(NAND_UNI_INST * pInst) {
  //
  // Do nothing if the NAND partition starts on physical block 0.
  // It is guaranteed that this block is not bad.
  //
  if (pInst->FirstBlock == 0u) {
    return 0;
  }
  //
  // Search for the first non-bad block.
  //
  for (;;) {
    if (pInst->NumLogBlocks == 0u) {
      return 1;                     // Error, all blocks in the partition are bad.
    }
    if (_IsBlockBad(pInst, 0) == 0) {
      return 0;                     // OK, found a block which is not bad.
    }
    pInst->FirstBlock++;
    pInst->NumBlocks--;
    pInst->NumLogBlocks--;
  }
}

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
static int _CleanLimited(NAND_UNI_INST * pInst, U32 NumBlocksFree, U32 NumSectorsFree) {
  unsigned              PPB_Shift;
  unsigned              SectorsPerBlock;
  NAND_UNI_WORK_BLOCK * pWorkBlock;
  unsigned              NumWorkBlocksFree;
  unsigned              brsiFree;
  int                   r;
  unsigned              NumSectorsFreeInWB;

  PPB_Shift       = pInst->PPB_Shift;
  SectorsPerBlock = 1uL << PPB_Shift;
  //
  // Count the number of available free work blocks.
  //
  NumWorkBlocksFree = 0;
  pWorkBlock        = pInst->pFirstWorkBlockFree;
  while (pWorkBlock != NULL) {
    ++NumWorkBlocksFree;
    pWorkBlock = pWorkBlock->pNext;
  }
  //
  // Clean some work blocks if the number of free work blocks
  // is smaller than the number of work blocks required to be free.
  //
  if (NumBlocksFree > NumWorkBlocksFree) {
    NumBlocksFree -= NumWorkBlocksFree;
    do {
      r = _CleanLastWorkBlock(pInst);
      if (r != 0) {
        return 1;       // Error, could not clean work block.
      }
    } while (--NumBlocksFree != 0u);
  }
  //
  // For each work block in use check if there are enough free sectors available.
  // Convert each work block that does not meet this requirement to a data block.
  //
  r = 0;
  do {
    pWorkBlock = pInst->pFirstWorkBlockInUse;
    while (pWorkBlock != NULL) {
      brsiFree = pWorkBlock->brsiFree;
      NumSectorsFreeInWB = SectorsPerBlock - brsiFree;
      if (NumSectorsFree > NumSectorsFreeInWB) {
        r = _CleanWorkBlockIfAllowed(pInst, pWorkBlock, BRSI_INVALID, NULL);
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
*    !=0    An error occurred.
*/
static int _ApplyCleanThreshold(NAND_UNI_INST * pInst) {
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
    FS_DEBUG_WARN((FS_MTYPE_DRIVER, "NAND_UNI: Invalid number of free blocks. It will be set to 0."));
    NumBlocksFree = 0;
  }
  if (NumSectorsFree >= (SectorsPerBlock - 1u)) {    // -1 because the first page in the block is reserved for erase count and bad block status.
    FS_DEBUG_WARN((FS_MTYPE_DRIVER, "NAND_UNI: Invalid number of free sectors in block. It will be set to 0."));
    NumSectorsFree = 0;
  }
  pInst->NumBlocksFree  = (U16)NumBlocksFree;
  pInst->NumSectorsFree = (U16)NumSectorsFree;
  r = _CleanLimited(pInst, NumBlocksFree, NumSectorsFree);
  return r;
}

#if FS_NAND_SUPPORT_BLOCK_GROUPING

/*********************************************************************
*
*       _IsDataBlockValid
*
*  Function description
*    Called when the block grouping is enabled to determine if a data block
*    has been merged successfully.
*
*  Return value
*    ==0    The data block is valid.
*    !=0    The data block is not valid. The merge operation has been probably
*           interrupted by an unexpected reset interrupted.
*/
static int _IsDataBlockValid(NAND_UNI_INST * pInst, unsigned BlockIndex) {
  unsigned SectorsPerBlock;
  U32      SectorIndexSrc0;
  U32      SectorIndexLast;
  unsigned brsi;

  SectorsPerBlock = 1uL << pInst->PPB_Shift;
  SectorIndexSrc0 = _BlockIndex2SectorIndex0(pInst, BlockIndex);
  SectorIndexLast = SectorIndexSrc0 + (SectorsPerBlock - 1u); // -1 since the first sector (page) in the block does not store any data.
  //
  // Read the BRSI of the last sector in the block. If the value is valid,
  // it means the data block has been merged successfully. BRSI is written
  // to the last sector in the block when the source data block and the
  // work block are merged into this one (see _ConvertWorkBlock() for details.)
  //
  brsi = BRSI_INVALID;
  (void)_ReadBRSI(pInst, SectorIndexLast, &brsi);
  if (brsi == BRSI_INVALID) {
    return 0;                   // The data block in not valid.
  }
  return 1;                     // The data block is valid.
}

/*********************************************************************
*
*       _IsWorkBlockValid
*
*  Function description
*    Called when the block grouping is enabled to determine if a work block
*    has been moved successfully.
*
*  Return value
*    ==0    The work block is valid.
*    !=0    The work block is not valid. The move operation has been probably
*           interrupted by an unexpected reset interrupted.
*/
static int _IsWorkBlockValid(NAND_UNI_INST * pInst, unsigned BlockIndex) {
  unsigned NumSectors;
  unsigned SectorIndex0;
  unsigned brsi;
  unsigned SectorsPerBlock;
  int      r;

  SectorsPerBlock = 1uL << pInst->PPB_Shift;
  SectorIndex0    = _BlockIndex2SectorIndex0(pInst, BlockIndex);
  NumSectors      = NUM_SECTORS_INVALID;
  (void)_ReadNumSectors(pInst, SectorIndex0 + BRSI_BLOCK_INFO, &NumSectors);
  if ((NumSectors == NUM_SECTORS_INVALID) ||
      (NumSectors == 0u)                  ||
      (NumSectors > (SectorsPerBlock - 1u))) {        // -1 since the first sector (page) in the block does not store any data.
    return 0;                   // Invalid number of sectors. The data in the work block is not valid.
  }
  //
  // Check the data of the last written sector.
  // If it is valid we consider the work block as valid.
  //
  r = _ReadSectorWithECCAndErrorHandling(pInst, _pSectorBuffer, SectorIndex0 + NumSectors - 1u);
  if ((r == RESULT_NO_ERROR) || (r == RESULT_BIT_ERRORS_CORRECTED) || (r == RESULT_BIT_ERROR_IN_ECC)) {
    brsi = _LoadBRSI(pInst);
    if ((brsi != BRSI_INVALID)    &&
        (brsi >= BRSI_BLOCK_INFO) &&
        (brsi <  SectorsPerBlock)) {
      return 1;                     // The data in the work block is valid.
    }
  }
  return 0;                         // The data in the work block is not valid.
}

#endif // FS_NAND_SUPPORT_BLOCK_GROUPING

/*********************************************************************
*
*       _LowLevelMount
*
*  Function description
*    Reads and analyzes management information read from NAND flash device.
*
*  Parameters
*    pInst      Driver instance.
*
*  Return value
*    ==0    OK, NAND flash device successfully mounted.
*    !=0    An error occurred.
*/
static int _LowLevelMount(NAND_UNI_INST * pInst) {
  unsigned              iBlock;
  unsigned              lbi;
  unsigned              pbiPrev;
  U32                   EraseCntMax;               // Highest erase count on any sector
  U32                   EraseCnt;
  U32                   EraseCntMin;
  U32                   NumBlocksEraseCntMin;
  const U8            * pPageBuffer;
  unsigned              u;
  int                   r;
  U32                   Version;
  U32                   NumBlocksToFileSystem;
  int                   NumBlocksToUse;
  NAND_UNI_WORK_BLOCK * pWorkBlock;
  NAND_UNI_WORK_BLOCK * pWorkBlockNext;
  unsigned              NumWorkBlocks;
  unsigned              NumWorkBlocksLLFormat;
  unsigned              NumWorkBlocksToAllocate;
  U32                   NumBlocks;
  unsigned              SectorsPerBlock;
  unsigned              NumPagesPerBlock;
  U32                   NumBlocksLLFormat;
  U32                   NumPagesPerBlockLLFormat;
  int                   DiscardPrevBlock;
  unsigned              MRUFreeBlock;
  unsigned              NumBadBlocks;
  unsigned              NumBytes;
  U8                  * pAssign;

  NumPagesPerBlock = 1uL << pInst->PPB_Shift;
  //
  // Move the start of NAND partition (FirstBlock)
  // to skip over eventual bad blocks.
  //
  r = _SkipLeadingBadBlocks(pInst);
  if (r != 0) {
    return 1;                   // Error
  }
  NumBlocks = pInst->NumBlocks;
  //
  // Check info block first (First block in the system)
  //
  r = _ReadSectorWithECCAndErrorHandling(pInst, _pSectorBuffer, SECTOR_INDEX_FORMAT_INFO);
  if ((r != RESULT_NO_ERROR) && (r != RESULT_BIT_ERRORS_CORRECTED) && (r != RESULT_BIT_ERROR_IN_ECC)) {
    return 1;                   // Error
  }
  pPageBuffer = (const U8 *)_pSectorBuffer;
  if (FS_MEMCMP(_acInfo, pPageBuffer , sizeof(_acInfo)) != 0) {
    FS_DEBUG_WARN((FS_MTYPE_DRIVER, "NAND_UNI: _LowLevelMount: Invalid low-level signature."));
    return 1;                   // Error
  }
  Version = FS_LoadU32BE(pPageBuffer + INFO_OFF_LLFORMAT_VERSION);
  if (Version != (U32)LLFORMAT_VERSION) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_UNI: _LowLevelMount: Invalid low-level format version."));
    return 1;                   // Error
  }
  NumWorkBlocksLLFormat = FS_LoadU32BE(pPageBuffer + INFO_OFF_NUM_WORK_BLOCKS);
  if (NumWorkBlocksLLFormat >= NumBlocks) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_UNI: _LowLevelMount: Invalid number of work blocks."));
    return 1;                   // Error
  }
  NumBlocksLLFormat = FS_LoadU32BE(pPageBuffer + INFO_OFF_NUM_BLOCKS);
  if (NumBlocksLLFormat != 0xFFFFFFFFuL) {
    if (NumBlocksLLFormat != NumBlocks) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_UNI: _LowLevelMount: Invalid number of blocks."));
      return 1;                 // Error
    }
  }
  NumPagesPerBlockLLFormat = FS_LoadU32BE(pPageBuffer + INFO_OFF_NUM_PAGES_PER_BLOCK);
  if (NumPagesPerBlockLLFormat != 0xFFFFFFFFuL) {
    if (NumPagesPerBlockLLFormat != NumPagesPerBlock) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_UNI: _LowLevelMount: Invalid number of pages per block."));
      return 1;                 // Error
    }
  }
  //
  // Find out how many work blocks are required to be allocated.
  // We take the maximum between the number of work blocks read from device
  // and the number of work blocks configured. The reason is to prevent
  // an overflow in the paWorkBlock array when the application increases
  // the number of work blocks and does a low-level format.
  //
  NumWorkBlocks           = pInst->NumWorkBlocks;
  NumWorkBlocksToAllocate = SEGGER_MAX(NumWorkBlocksLLFormat, NumWorkBlocks);
  NumWorkBlocks           = NumWorkBlocksLLFormat;
  //
  // Compute the number of logical blocks available to the file system.
  // We have to take into account that this version of the driver
  // reserves one block more for internal use. To stay compatible we have
  // to use 2 algorithms: one for the "old" version and one for the "new" one.
  // We tell the 2 versions apart by checking the INFO_OFF_NUM_LOG_BLOCKS.
  // The "old" version does not set this entry and its value will always read 0xFFFFFFFF.
  //
  NumBlocksToFileSystem = FS_LoadU32BE(pPageBuffer + INFO_OFF_NUM_LOG_BLOCKS);
  NumBlocksToUse        = _CalcNumBlocksToUse(pInst, NumBlocks, NumWorkBlocks);
  if ((NumBlocksToUse <= 0) || (NumBlocksToFileSystem > (U32)NumBlocksToUse)) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NAND_UNI: _LowLevelMount: Number of logical blocks has shrunk. Low-level format required."));
    return 1;
  }
  SectorsPerBlock      = (1uL << pInst->PPB_Shift) - 1u;
  pInst->NumLogBlocks  = (U32)NumBlocksToUse;
  pInst->NumSectors    = pInst->NumLogBlocks * SectorsPerBlock;
  pInst->NumWorkBlocks = NumWorkBlocksToAllocate;
  //
  // Load the information stored when a fatal error occurs
  //
  pInst->IsWriteProtected = 0;
  pInst->HasFatalError    = 0;
  pInst->ErrorType        = RESULT_NO_ERROR;
  pInst->ErrorSectorIndex = 0;
  r = _ReadSectorWithECC(pInst, _pSectorBuffer, SECTOR_INDEX_ERROR_INFO);
  if ((r == RESULT_NO_ERROR) || (r == RESULT_BIT_ERRORS_CORRECTED) || (r == RESULT_BIT_ERROR_IN_ECC)) {
    pPageBuffer = SEGGER_CONSTPTR2PTR(const U8, _pSectorBuffer);
    pInst->IsWriteProtected = FS_LoadU16BE(pPageBuffer + INFO_OFF_IS_WRITE_PROTECTED) != 0xFFFFu ? 1u : 0u;   // Inverted, 0xFFFF is not write protected
    pInst->HasFatalError    = FS_LoadU16BE(pPageBuffer + INFO_OFF_HAS_FATAL_ERROR)    != 0xFFFFu ? 1u : 0u;   // Inverted, 0xFFFF doesn't have fatal error
    if (pInst->HasFatalError != 0u) {
      pInst->ErrorType        = (U8)FS_LoadU16BE(pPageBuffer + INFO_OFF_FATAL_ERROR_TYPE);
      pInst->ErrorSectorIndex = FS_LoadU32BE(pPageBuffer + INFO_OFF_FATAL_ERROR_SECTOR_INDEX);
    }
  }
  //
  // Assign reasonable defaults for configurable parameters.
  //
  if (pInst->MaxEraseCntDiff == 0u) {
    pInst->MaxEraseCntDiff = FS_NAND_MAX_ERASE_CNT_DIFF;
  }
  //
  // Allocate/Zero memory for tables
  //
  FS_ALLOC_ZEROED_PTR(SEGGER_PTR2PTR(void *, &pInst->pLog2PhyTable), (I32)_L2P_GetSize(pInst), "NAND_UNI_SECTOR_MAP");
  if (pInst->pLog2PhyTable == NULL) {
    return 1;                 // Error, could not allocate memory.
  }
  FS_ALLOC_ZEROED_PTR(SEGGER_PTR2PTR(void *, &pInst->pFreeMap),      ((I32)NumBlocks + 7) / 8, "NAND_UNI_FREE_MAP");
  if (pInst->pFreeMap == NULL) {
    return 1;                 // Error, could not allocate memory.
  }
  //
  //  Initialize work block descriptors: Allocate memory and add them to free list
  //
  NumBytes = sizeof(NAND_UNI_WORK_BLOCK) * NumWorkBlocksToAllocate;
  //
  // This is equivalent to FS_AllocZeroedPtr() but it avoids filling the array with 0
  // when the memory block is already allocated.
  //
  if (pInst->paWorkBlock == NULL) {
    pInst->paWorkBlock = SEGGER_PTR2PTR(NAND_UNI_WORK_BLOCK, FS_ALLOC_ZEROED((I32)NumBytes, "NAND_UNI_WORK_BLOCK"));
    if (pInst->paWorkBlock == NULL) {
      return 1;               // Error, could not allocate memory.
    }
    FS_MEMSET(pInst->paWorkBlock, 0, NumBytes);
  }
  NumBytes   = _WB_GetAssignmentSize(pInst);
  pWorkBlock = pInst->paWorkBlock;
  u          = NumWorkBlocksToAllocate;
  //
  // The memory for the assign array is allocated here at once for all the work blocks.
  // The address of the allocated memory is stored to the first work block.
  //
  FS_ALLOC_ZEROED_PTR(SEGGER_PTR2PTR(void *, &pWorkBlock->paAssign), (I32)NumBytes * (I32)u, "NAND_UNI_WB_ASSIGN");
  if (pWorkBlock->paAssign == NULL) {
    return 1;                 // Error, could not allocate memory.
  }
  pAssign = SEGGER_PTR2PTR(U8, pWorkBlock->paAssign);
  do {
    pWorkBlock->paAssign = SEGGER_PTR2PTR(void, pAssign);
    //
    // Not all the work block descriptors are available if the number of work blocks
    // specified in the device is smaller than the number of work blocks configured.
    //
    if (NumWorkBlocks != 0u) {
      _WB_AddToFreeList(pInst, pWorkBlock);
      NumWorkBlocks--;
    }
    pWorkBlock++;
    pAssign += NumBytes;
  } while (--u != 0u);
#if FS_NAND_SUPPORT_FAST_WRITE
  {
    NAND_UNI_DATA_BLOCK * pDataBlock;

    //
    // We allocate the same number of data blocks as the number of configured work blocks
    // since this is typically the number of different blocks the file system has to write
    // to during an operation.
    //
    NumBytes = sizeof(NAND_UNI_DATA_BLOCK) * NumWorkBlocksToAllocate;
    if (pInst->paDataBlock == NULL) {
      pInst->paDataBlock = SEGGER_PTR2PTR(NAND_UNI_DATA_BLOCK, FS_ALLOC_ZEROED((I32)NumBytes, "NAND_UNI_DATA_BLOCK"));
      if (pInst->paDataBlock == NULL) {
        return 1;             // Error, could not allocate memory.
      }
      FS_MEMSET(pInst->paDataBlock, 0, NumBytes);
    }
    //
    // Put all the data blocks in the free list.
    //
    pDataBlock = pInst->paDataBlock;
    u          = NumWorkBlocksToAllocate;
    do {
      _DB_AddToFreeList(pInst, pDataBlock);
      pDataBlock++;
    } while (--u != 0u);
  }
#endif // FS_NAND_SUPPORT_FAST_WRITE
  //
  // Read the spare areas and fill the tables.
  //
  EraseCntMax          = 0;
  EraseCntMin          = ERASE_CNT_INVALID;
  NumBlocksEraseCntMin = 0;
  MRUFreeBlock         = 0;
  NumBadBlocks         = 0;
  IF_STATS(pInst->StatCounters.NumBadBlocks = 0);
  for (iBlock = PBI_STORAGE_START; iBlock < NumBlocks; iBlock++) {
    unsigned BlockType;
    U32      SectorIndex;

    if (_IsBlockBad(pInst, iBlock) != 0) {
      FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NAND_UNI: LL_MOUNT BT:  BAD, BI: %u\n", iBlock));
      ++NumBadBlocks;
      IF_STATS(pInst->StatCounters.NumBadBlocks++);
      continue;                               // Ignore bad blocks.
    }
    //
    // Read the block information from the spare area of the second page.
    //
    SectorIndex = _BlockIndex2SectorIndex0(pInst, iBlock);
    ++SectorIndex;
    r = _ReadBlockInfo(pInst, SectorIndex);
    if ((r != RESULT_NO_ERROR) && (r != RESULT_BIT_ERRORS_CORRECTED) && (r != RESULT_BIT_ERROR_IN_ECC)) {
      (void)_ClearBlockIfAllowed(pInst, iBlock, ERASE_CNT_INVALID);     // Block information is corrupted. Erase the block now and put it in the free list.
      continue;
    }
    //
    // OK, block information has no bit errors or the bit errors were corrected.
    //
    BlockType = _LoadBlockType(pInst);
    lbi       = (U16)_LoadLBI(pInst);
    EraseCnt  = _LoadEraseCnt(pInst);
    //
    // Check if the block information makes sense. If not, erase the block and put it in the free list.
    //
    if ((EraseCnt == ERASE_CNT_INVALID) || (lbi >= pInst->NumLogBlocks)) {
      _MarkBlockAsFree(pInst, iBlock);        // Block seems not to be empty. Consider it free to use.
      continue;
    }
    //
    // Handle work blocks.
    //
    if (BlockType == BLOCK_TYPE_WORK) {
      FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NAND_UNI: LL_MOUNT BT: WORK, BI: %u, LBI: %u\n", iBlock, lbi));
      //
      // Check if we already have a block with this lbi.
      // If we do, then we erase it and add it to the free list.
      //
      pWorkBlock = _FindWorkBlock(pInst, lbi);
      if (pWorkBlock == NULL) {
        pWorkBlock = _AllocWorkBlockDesc(pInst, lbi);
        if (pWorkBlock != NULL) {
          pWorkBlock->pbi = iBlock;
        } else {
          FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_UNI: _LowLevelMount: Found more work blocks than can be handled."));
          (void)_ClearBlockIfAllowed(pInst, iBlock, EraseCnt);
          EraseCnt = ERASE_CNT_INVALID;
        }
      } else {
        pbiPrev = pWorkBlock->pbi;
        //
        // We have found a duplicated work block. Per default we erase the most
        // recent data block since we can not say for sure if the block
        // merge operation completed before the unexpected reset.
        //
        DiscardPrevBlock = _IsBlockMoreRecent(pInst, pbiPrev);
#if FS_NAND_SUPPORT_BLOCK_GROUPING
        //
        // If the block grouping is enabled, we discard the data from the old work block
        // if the data in the new work block is valid. We are doing this in order to avoid
        // using a work block that might not have been erased completely.
        //
        if (_IsBlockGroupingEnabled(pInst) != 0) {
          if (DiscardPrevBlock != 0) {  // Is pbiPrev the more recent data block?
            if (_IsWorkBlockValid(pInst, pbiPrev) != 0) {
              DiscardPrevBlock = 0;     // Keep pbiPrev and discard iBlock.
            }
          } else {
            if (_IsWorkBlockValid(pInst, iBlock) != 0) {
              DiscardPrevBlock = 1;     // Keep iBlock and discard pbiPrev.
            }
          }
        }
#endif // FS_NAND_SUPPORT_BLOCK_GROUPING
        if (DiscardPrevBlock != 0) {
          pWorkBlock->pbi = iBlock;                           // Replace old block index with the new one.
          (void)_ClearBlockIfAllowed(pInst, pbiPrev, ERASE_CNT_INVALID);
        } else {
          (void)_ClearBlockIfAllowed(pInst, iBlock, EraseCnt);               // Erase the current block since the previous in newer.
          EraseCnt = ERASE_CNT_INVALID;
        }
      }
      //
      // Update the information about the maximum erase count.
      //
      if (EraseCnt != ERASE_CNT_INVALID) {
        if (EraseCnt > EraseCntMax) {
          EraseCntMax = EraseCnt;
        }
      }
      //
      // Update information for the active wear leveling.
      //
      if ((EraseCntMin == ERASE_CNT_INVALID) ||
          (EraseCnt    <  EraseCntMin)) {
        EraseCntMin          = EraseCnt;
        NumBlocksEraseCntMin = 1;
        MRUFreeBlock         = iBlock;
      } else {
        if (EraseCnt == EraseCntMin) {
          ++NumBlocksEraseCntMin;
        }
      }
    //
    // Handle data blocks.
    //
    } else if (BlockType == BLOCK_TYPE_DATA) {
      FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NAND_UNI: LL_MOUNT BT: DATA, BI: %u, LBI: %u\n", iBlock, lbi));
      pbiPrev = (U16)_L2P_Read(pInst, lbi);
      if (pbiPrev == 0u) {                                          // Has this lbi already been assigned?
        _L2P_Write(pInst, lbi, iBlock);                             // Add block to the translation table
        if (EraseCnt > EraseCntMax) {
          EraseCntMax = EraseCnt;
        }
      } else {
        //
        // We have found a duplicated data block. Per default we erase the most
        // recent data block since we can not say for sure if the block
        // merge operation completed before the unexpected reset.
        //
        DiscardPrevBlock = _IsBlockMoreRecent(pInst, pbiPrev);
#if FS_NAND_SUPPORT_BLOCK_GROUPING
        //
        // If the block grouping is enabled, we discard the data from the old block
        // if the data in the most recent block is valid. We are doing this in
        // order to avoid using a data block that might not have been erased completely.
        //
        if (_IsBlockGroupingEnabled(pInst) != 0) {
          if (DiscardPrevBlock != 0) {                              // Is pbiPrev the more recent data block?
            if (_IsDataBlockValid(pInst, pbiPrev) != 0) {
              DiscardPrevBlock = 0;                                 // Keep pbiPrev and discard iBlock.
            }
          } else {
            if (_IsDataBlockValid(pInst, iBlock) != 0) {
              DiscardPrevBlock = 1;                                 // Keep iBlock and discard pbiPrev.
            }
          }
        }
#endif // FS_NAND_SUPPORT_BLOCK_GROUPING
        if (DiscardPrevBlock != 0) {
          (void)_ClearBlockIfAllowed(pInst, pbiPrev, ERASE_CNT_INVALID);
          _L2P_Write(pInst, lbi, iBlock);                           // Add block to the translation table
        } else {
          (void)_ClearBlockIfAllowed(pInst, iBlock, EraseCnt);      // Erase the current block since the previous in newer.
          EraseCnt = ERASE_CNT_INVALID;
        }
      }
      //
      // Update the information about the maximum erase count.
      //
      if (EraseCnt != ERASE_CNT_INVALID) {
        if (EraseCnt > EraseCntMax) {
          EraseCntMax = EraseCnt;
        }
      }
      //
      // Update information for the active wear leveling.
      //
      if ((EraseCntMin == ERASE_CNT_INVALID) ||
          (EraseCnt    <  EraseCntMin)) {
        EraseCntMin          = EraseCnt;
        NumBlocksEraseCntMin = 1;
        MRUFreeBlock         = iBlock;
      } else {
        if (EraseCnt == EraseCntMin) {
          ++NumBlocksEraseCntMin;
        }
      }
    } else {
      //
      // Any other blocks are interpreted as free blocks.
      //
      _MarkBlockAsFree(pInst, iBlock);
    }
  }
  if ((NumBlocks - NumBadBlocks) < (unsigned)NumBlocksToUse) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_UNI: _LowLevelMount: Too many blocks marked as defective."));
    return 1;           // Error, too many bad blocks detected.
  }
  pInst->EraseCntMax          = EraseCntMax;
  pInst->EraseCntMin          = EraseCntMin;
  pInst->NumBlocksEraseCntMin = NumBlocksEraseCntMin;
  //
  // The most recently used free block will be the next
  // free block after the data or work block with the smallest
  // erase count. We are doing this in order to prevent that
  // the first blocks in the NAND flash are erased too much
  // when the NAND flash is mounted frequently. Reading the
  // erase count of the free blocks is not an option here since we
  // have to also read from the first page of the block where the
  // erase count of the free blocks is stored which will increase
  // the mount time.
  //
  pInst->MRUFreeBlock = MRUFreeBlock;
  //
  // Load information from the work blocks we found.
  // We have to disable the active wear leveling to
  // prevent that a work block is relocated before
  // is loaded.
  //
  r = 0;
  pInst->ActiveWLStatus = ACTIVE_WL_DISABLED_PERM;
  pWorkBlock = pInst->pFirstWorkBlockInUse;
  while (pWorkBlock != NULL) {
    //
    // Save the pointer to next work block for the case
    // that _LoadWorkBlock() removes the current work
    // block from the list of work blocks in use.
    //
    pWorkBlockNext = pWorkBlock->pNext;
    r = _LoadWorkBlock(pInst, pWorkBlock);
    if (r != 0) {
      break;       // Error, failed to load work block.
    }
    pWorkBlock = pWorkBlockNext;
#if FS_SUPPORT_TEST
    if (pInst->ActiveWLStatus == ACTIVE_WL_ENABLED) {
      FS_X_PANIC(FS_ERRCODE_INVALID_USAGE);
    }
#endif // FS_SUPPORT_TEST
  }
  pInst->ActiveWLStatus = ACTIVE_WL_ENABLED;
  //
  // Reserve space in work blocks for fast write operations.
  //
  if (r == 0) {
    r = _ApplyCleanThreshold(pInst);
  }
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NAND_UNI: LL_MOUNT LogSectorSize: %u, NumLogBlocks: %u, NumWorkBlocks: %u, r: %d\n", pInst->BytesPerPage, NumBlocksToUse, pInst->NumWorkBlocks, r));
  return r;
}

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
static int _GetSectorUsage(NAND_UNI_INST * pInst, U32 LogSectorIndex) {
  unsigned              lbi;
  unsigned              pbiSrc;
  unsigned              pbiWork;
  unsigned              brsiLog;
  unsigned              brsiPhy;
  NAND_UNI_WORK_BLOCK * pWorkBlock;
  int                   r;
  unsigned              SectorStat;
  U32                   SectorIndexSrc;
  U32                   SectorIndexWork;

  if (LogSectorIndex >= pInst->NumSectors) {
    return FS_SECTOR_USAGE_UNKNOWN;
  }
  lbi        = _LogSectorIndex2LogBlockIndex(pInst, LogSectorIndex, &brsiLog);
  pbiSrc     = _L2P_Read(pInst, lbi);
  pWorkBlock = _FindWorkBlock(pInst, lbi);
  //
  // Sector data can be in data block or work block.
  //
  if (pWorkBlock != NULL) {
    pbiWork = pWorkBlock->pbi;
    brsiPhy = _WB_ReadAssignment(pInst, pWorkBlock, brsiLog);
    if (brsiPhy != 0u) {        // Sector in a work block ?
      SectorIndexWork  = _BlockIndex2SectorIndex0(pInst, pbiWork);
      SectorIndexWork |= brsiPhy;
      r = _ReadSectorStat(pInst, SectorIndexWork, &SectorStat);
      if ((r == RESULT_NO_ERROR) || (r == RESULT_BIT_ERRORS_CORRECTED) || (r == RESULT_BIT_ERROR_IN_ECC)) {
        if (SectorStat == SECTOR_STAT_WRITTEN) {
          return 0;             // Sector contains valid data.
        }
      }
      return 1;                 // Sector has been invalidated.
    }
  }
  if (pbiSrc != 0u) {           // Sector in a data block ?
    SectorIndexSrc  = _BlockIndex2SectorIndex0(pInst, pbiSrc);
    SectorIndexSrc |= brsiLog;
    r = _ReadSectorStat(pInst, SectorIndexSrc, &SectorStat);
    if ((r == RESULT_NO_ERROR) || (r == RESULT_BIT_ERRORS_CORRECTED) || (r == RESULT_BIT_ERROR_IN_ECC)) {
      if (SectorStat == SECTOR_STAT_WRITTEN) {
        return 0;               // Sector contains valid data.
      }
    }
  }
  return 1;                     // Sector contains invalid data.
}

/*********************************************************************
*
*       _LowLevelMountIfRequired
*
*  Function description
*    Mounts the NAND flash device if not already mounted.
*
*  Parameters
*    pInst      Driver instance.
*
*  Return value
*    ==0    O.K., device has been successfully mounted and is accessible
*    !=0    An error occurred.
*/
static int _LowLevelMountIfRequired(NAND_UNI_INST * pInst) {
  int r;

  if (pInst->IsLLMounted != 0u) {
    return 0;                   // O.K., is mounted
  }
  if (pInst->LLMountFailed != 0u) {
    return 1;                   // Error, we could not mount it and do not want to try again
  }
  r = _LowLevelMount(pInst);
  if (r == 0) {
    pInst->IsLLMounted = 1;
  } else {
    pInst->LLMountFailed = 1;
  }
  //
  // On debug builds we count here the number of valid sectors.
  //
#if (FS_NAND_ENABLE_STATS != 0) && (FS_NAND_ENABLE_STATS_SECTOR_STATUS != 0)
  if (pInst->IsLLMounted != 0u) {
    U32 iSector;
    U32 NumLogSectors;
    int SectorUsage;
    U32 NumValidSectors;

    NumLogSectors   = pInst->NumSectors;
    NumValidSectors = 0;
    for (iSector = 0; iSector < NumLogSectors; ++iSector) {
      SectorUsage = _GetSectorUsage(pInst, iSector);
      if (SectorUsage == 0) {                       // Sector contains valid data ?
        ++NumValidSectors;
      }
    }
    pInst->StatCounters.NumValidSectors = NumValidSectors;
  }
#endif // FS_NAND_ENABLE_STATS != 0 && FS_NAND_ENABLE_STATS_SECTOR_STATUS != 0
  return r;
}

/*********************************************************************
*
*       _ReadOneSectorEx
*
*  Function description
*    Reads one logical sectors from storage device.
*
*  Return value
*    ==0   Data successfully read.
*    !=0   An error has occurred.
*
*  Additional information
*    There are 3 possibilities:
*      a) Data is located in a work block
*      b) There is a physical block assigned to this logical block which
*         means that we have to read from hardware
*      c) There is a no physical block assigned to this logical block.
*         This means data has never been written to storage.
*         We fill the data with a known value.
*/
static int _ReadOneSectorEx(NAND_UNI_INST * pInst, U32 LogSectorIndex, U8 * pBuffer, unsigned Off, unsigned NumBytes) {
  int                   r;
  unsigned              lbi;
  unsigned              pbi;
  unsigned              brsiPhy;
  unsigned              brsiLog;
  U32                   SectorIndex;
  NAND_UNI_WORK_BLOCK * pWorkBlock;
  unsigned              BytesPerPage;
  int                   IsRelocationRequired;
  int                   IsWorkBlock;

  IsWorkBlock  = 0;
  lbi          = _LogSectorIndex2LogBlockIndex(pInst, LogSectorIndex, &brsiLog);
  brsiPhy      = brsiLog;
  BytesPerPage = pInst->BytesPerPage;
  //
  // Physical block index is taken from Log2Phy table or is work block.
  //
  pbi          = _L2P_Read(pInst, lbi);
  pWorkBlock   = _FindWorkBlock(pInst, lbi);
  if (pWorkBlock != NULL) {
    unsigned u;

    u = _WB_ReadAssignment(pInst, pWorkBlock, brsiLog);
    if (u != 0u) {
      pbi         = pWorkBlock->pbi;
      brsiPhy     = u;
      IsWorkBlock = 1;
    }
  }
  //
  // Copy data to user buffer.
  //
  if (pbi == 0u) {
    //
    // Fill buffer with a known value if the sector has never been written before.
    //
    if (NumBytes == 0u) {
      NumBytes = BytesPerPage;
    }
    FS_MEMSET(pBuffer, FS_NAND_READ_BUFFER_FILL_PATTERN, NumBytes);
    r = 0;
  } else {
    IsRelocationRequired = 1;
    //
    // Read from hardware.
    //
    SectorIndex  = _BlockIndex2SectorIndex0(pInst, pbi);
    SectorIndex += brsiPhy;
    r = _ReadSectorWithECCAndER_Ex(pInst, SEGGER_PTR2PTR(U32, pBuffer), SectorIndex, brsiLog, Off, NumBytes);
    //
    // If the number of corrected bits is higher than the configured threshold
    // _ReadSectorWithECCAndER_Ex() returns RESULT_BIT_ERRORS_CORRECTED.
    // In this case the block has to be relocated to prevent the accumulation
    // of bit errors that might lead to data loss.
    //
    if ((r == RESULT_NO_ERROR) || (r == RESULT_BIT_ERRORS_CORRECTED) || (r == RESULT_BIT_ERROR_IN_ECC)) {
      if (r == RESULT_NO_ERROR) {
        IsRelocationRequired = 0;
      }
#if FS_NAND_FILL_READ_BUFFER
      if ((pInst->AllowBlankUnusedSectors == 0u) || (_IsBlockGroupingEnabled(pInst) != 0)) {
        unsigned SectorStat;

        //
        // The data of the spare area is stored to the buffer used during the error recovery.
        // Swap the buffers here to make sure that the correct sector status is read.
        //
#if FS_NAND_ENABLE_ERROR_RECOVERY
        _SwapSpareAreaBufferIfRequired();
#endif // FS_NAND_ENABLE_ERROR_RECOVERY
        SectorStat = _LoadSectorStat(pInst);
#if FS_NAND_ENABLE_ERROR_RECOVERY
        _SwapSpareAreaBufferIfRequired();
#endif // FS_NAND_ENABLE_ERROR_RECOVERY
        if (SectorStat == SECTOR_STAT_EMPTY) {
          //
          // Make sure that invalid sectors are filled with a known value.
          //
          if (NumBytes == 0u) {
            NumBytes = BytesPerPage;
          }
          FS_MEMSET(pBuffer, FS_NAND_READ_BUFFER_FILL_PATTERN, NumBytes);
        }
      }
#endif // FS_NAND_FILL_READ_BUFFER
      r = 0;
    } else {
#if FS_NAND_ENABLE_ERROR_RECOVERY
      //
      // Do not perform any block relocation if we are recovering from
      // an error via RAID to prevent a change in the assignment of
      // logical blocks.
      //
      if (_IsERActive != 0u) {
        IsRelocationRequired = 0;
      }
#endif // FS_NAND_ENABLE_ERROR_RECOVERY
    }
    if (IsRelocationRequired != 0) {
      //
      // Try to recover the data from the block.
      //
      if (IsWorkBlock != 0) {
        r = _RelocateWorkBlock(pInst, pWorkBlock);
      } else {
        r = _RelocateDataBlock(pInst, pbi);
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _ReadOneSector
*
*  Function description
*    Reads one logical sectors from storage device.
*/
static int _ReadOneSector(NAND_UNI_INST * pInst, U32 LogSectorIndex, U8 * pBuffer) {
  int r;

  r = _ReadOneSectorEx(pInst, LogSectorIndex, pBuffer, 0, 0);
  return r;
}

/*********************************************************************
*
*       _CalcSectorStat
*
*  Function description
*    Checks if a logical sector contains valid data.
*
*  Return value
*    ==0      OK, sector status returned
*    !=0      An error occurred.
*/
static int _CalcSectorStat(NAND_UNI_INST * pInst, U32 LogSectorIndex, unsigned * pSectorStat)  {
  unsigned              lbi;
  NAND_UNI_WORK_BLOCK * pWorkBlock;
  unsigned              brsiSrc;
  int                   r;
  unsigned              SectorStat;
  U32                   SectorIndex;
  unsigned              pbiSrc;
  unsigned              brsiPhy;

  //
  // Initialize the local variables.
  //
  r          = 0;                                 // Set to indicate success.
  SectorStat = SECTOR_STAT_EMPTY;                 // Assume that the sector data is not valid.
  lbi        = (U16)_LogSectorIndex2LogBlockIndex(pInst, LogSectorIndex, &brsiSrc);
  //
  // Source sector contains invalid data. Check if the sector has been modified i.e. is stored in a work block.
  //
  pWorkBlock = _FindWorkBlock(pInst, lbi);
  if (pWorkBlock != NULL) {
    brsiPhy = _WB_ReadAssignment(pInst, pWorkBlock, brsiSrc);
    if (brsiPhy != 0u) {
      SectorStat = SECTOR_STAT_WRITTEN;           // OK, a copy of the sector is present in the work block.
    }
  }
  if (SectorStat == SECTOR_STAT_EMPTY) {
    //
    // Check if the sector is present in a data block.
    //
    pbiSrc = _L2P_Read(pInst, lbi);
    if (pbiSrc != 0u) {                           // Is a data block assigned to LBI?
      SectorIndex  = _BlockIndex2SectorIndex0(pInst, pbiSrc);
      SectorIndex |= brsiSrc;
      r = _ReadSectorStat(pInst, SectorIndex, &SectorStat);
      if ((r == RESULT_NO_ERROR) || (r == RESULT_BIT_ERRORS_CORRECTED) || (r == RESULT_BIT_ERROR_IN_ECC)) {
        if (r == RESULT_BIT_ERRORS_CORRECTED) {   // Bit error threshold reached?
          r = _RelocateDataBlock(pInst, pbiSrc);  // Move the data block to an other location.
        } else {
          r = 0;                                  // Status read correctly.
        }
      }
    }
  }
  //
  // Return the sector status.
  //
  if (pSectorStat != NULL) {
    *pSectorStat = SectorStat;
  }
  return r;
}

/*********************************************************************
*
*       _WriteLogSectorToWorkBlock
*
*  Function description
*    Writes the data of one logical sector to a work block.
*
*  Parameters
*    pInst            Driver instance.
*    LogSectorIndex   Index of the logical sector to be written.
*    pData            [IN] Sector data to be written. If NULL the data
*                     of the logical sector is marked as invalid.
*
*  Return value
*    ==0    OK, data successfully written.
*    !=0    An error has occurred.
*/
static int _WriteLogSectorToWorkBlock(NAND_UNI_INST * pInst, U32 LogSectorIndex, const U32 * pData) {
  U16                   lbi;
  NAND_UNI_WORK_BLOCK * pWorkBlock;
  unsigned              brsiSrc;
  unsigned              brsiDest;
  int                   r;
  U32                   SectorIndex;
  U32                   EraseCnt;
  unsigned              MergeCnt;
  int                   NumRetries;

  //
  // Initialize local variables.
  //
  lbi        = (U16)_LogSectorIndex2LogBlockIndex(pInst, LogSectorIndex, &brsiSrc);
  pWorkBlock = NULL;
  NumRetries = 0;
  //
  // Perform the write operation until it succeeds or the number of retries expires.
  //
  for (;;) {
    if (NumRetries++ > FS_NAND_NUM_WRITE_RETRIES) {
      return 1;                           // Error could not write data.
    }
    EraseCnt = ERASE_CNT_INVALID;
    //
    // Find (or create) a work block.
    //
    pWorkBlock = _FindWorkBlock(pInst, lbi);
    if (pWorkBlock != NULL) {
      //
      // Make sure that the sector to be written is in work block and that it has not been written already.
      //
      brsiDest = _GetNextFreeSector(pInst, pWorkBlock);
      if (brsiDest == 0u) {
        //
        // Work block is full. Clean it and at the same time write the sector data.
        //
        r = _CleanWorkBlock(pInst, pWorkBlock, brsiSrc, pData);
        return r;
      }
    } else {
      //
      // No work block found. Create a new work block for the specified LBI.
      //
      pWorkBlock = _AllocWorkBlock(pInst, lbi, &EraseCnt);
      if (pWorkBlock == NULL) {
        return 1;
      }
      brsiDest = _GetNextFreeSector(pInst, pWorkBlock);
    }
    MergeCnt = 0xF;
#if FS_NAND_SUPPORT_BLOCK_GROUPING
    //
    // If the block grouping is enabled we get the merge count of the corresponding data block
    // (if available) and store it later to work block. When the NAND flash is mounted,
    // the merge counts of the work and of the corresponding data block are compared.
    // If they are not equal, it means that the work block erase operation has been interrupted
    // and that the work block has to be erased again. We read the merge count here since _ReadMergeCnt()
    // overwrites the static buffer where the contents of the spare area is loaded.
    //
    if (_IsBlockGroupingEnabled(pInst) != 0) {
      unsigned pbiSrc;

      if (brsiDest == BRSI_BLOCK_INFO) {
        pbiSrc = _L2P_Read(pInst, lbi);
        if (pbiSrc != 0u) {
          SectorIndex  = _BlockIndex2SectorIndex0(pInst, pbiSrc);
          SectorIndex += brsiDest;
          r = _ReadMergeCnt(pInst, SectorIndex, &MergeCnt);
          if (r == RESULT_BIT_ERRORS_CORRECTED) {   // Bit error threshold reached?
            r = _RelocateDataBlock(pInst, pbiSrc);  // Move the data block to an other location.
            if (r != 0) {
              return 1;                             // Error, could not move the data block.
            }
            //
            // The wear leveling performed in _RelocateDataBlock() can also relocate work blocks.
            // Go to beginning of the loop and get a new pointer the work block descriptor.
            //
            IF_STATS(pInst->StatCounters.NumWriteRetries++);
            continue;
          }
        }
      }
    }
#endif // FS_NAND_SUPPORT_BLOCK_GROUPING
    //
    // Compute the physical sector index sector to write to.
    //
    SectorIndex  = _BlockIndex2SectorIndex0(pInst, pWorkBlock->pbi);
    SectorIndex += brsiDest;
    //
    // Write data to a free sector in the work block.
    //
    _ClearStaticSpareArea(pInst);
    _StoreBRSI(pInst, brsiSrc);
    if (pData != NULL) {                        // Mark the sector data as valid if data is written to it.
      _StoreSectorStat(pInst, SECTOR_STAT_WRITTEN);
    }
    //
    // If the sector stores block related information we encode them to spare area buffer.
    //
    if (brsiDest == BRSI_BLOCK_INFO) {
      _StoreEraseCnt(pInst, EraseCnt);
      _StoreLBI(pInst, lbi);
      _StoreBlockType(pInst, BLOCK_TYPE_WORK);   // Mark as work block.
      _StoreMergeCnt(pInst, MergeCnt);
    }
    //
    // In case the write operation invalidates the data of a sector
    // use the internal buffer to write 0s to it to reduce the chance of a bit error.
    //
    if (pData == NULL) {
      FS_MEMSET(_pSectorBuffer, 0, pInst->BytesPerPage);
      pData = _pSectorBuffer;
    }
    r = _WriteSectorWithECC(pInst, pData, SectorIndex);
#if FS_NAND_VERIFY_WRITE
    if (r == 0) {
      r = _VerifySector(pInst, pData, SectorIndex);
    }
#endif // FS_NAND_VERIFY_WRITE
#if FS_NAND_MAX_BIT_ERROR_CNT
    if (r == 0) {
      r = _CheckWorkBlock(pInst, pWorkBlock);
    }
#endif // FS_NAND_MAX_BIT_ERROR_CNT
    if (r == 0) {
      break;                            // OK, data has been written.
    }
    //
    // Could not write to work block. Save the data of this work block into a data block
    // and allocate another work block to write to.
    //
    r = _ConvertWorkBlock(pInst, pWorkBlock, brsiDest, BRSI_INVALID, NULL);
    if (r != 0) {
      return 1;
    }
    IF_STATS(pInst->StatCounters.NumWriteRetries++);
  }
  //
  // Update work block management info.
  //
  _MarkWorkBlockAsMRU(pInst, pWorkBlock);
  _WB_WriteAssignment(pInst, pWorkBlock, brsiSrc, brsiDest);
  return 0;
}

#if FS_NAND_SUPPORT_FAST_WRITE

/*********************************************************************
*
*       _CreateDataBlock
*
*  Function description
*    Creates a new data block.
*
*  Parameters
*    pInst      Driver instance.
*    lbi        Index of the logical block assigned to the created data block.
*    MergeCnt   Counter used to detect if a work and a data block have been merged correctly.
*    pData      [IN] Sector data to be written to first sector in the block.
*               If NULL the sector data is filled with a known pattern.
*
*  Return value
*    !=0    Index of the physical block assigned to the created data block.
*    ==0    An error occurred.
*/
static unsigned _CreateDataBlock(NAND_UNI_INST * pInst, unsigned lbi, unsigned MergeCnt, const U32 * pData) {
  int                   r;
  U32                   SectorIndex;
  U32                   EraseCnt;
  unsigned              pbi;
  unsigned              BytesPerPage;
  int                   Pattern;
  NAND_UNI_DATA_BLOCK * pDataBlock;

  //
  // Initialize local variables.
  //
  BytesPerPage = pInst->BytesPerPage;
  //
  // Allocate a new block.
  //
  EraseCnt = ERASE_CNT_INVALID;
  pbi = _AllocErasedBlock(pInst, &EraseCnt);
  if (pbi != 0u) {
    //
    // Prepare the management data.
    //
    _ClearStaticSpareArea(pInst);
    _StoreEraseCnt(pInst, EraseCnt);
    _StoreLBI(pInst, lbi);
    _StoreBlockType(pInst, BLOCK_TYPE_DATA);
    _StoreMergeCnt(pInst, MergeCnt);
    if (pData != NULL) {
      //
      // Mark the sector data as valid.
      //
      _StoreSectorStat(pInst, SECTOR_STAT_WRITTEN);
    } else {
      //
      // No data has to be written to first sector.
      // Fill the sector data with a known pattern.
      //
      Pattern = (int)_GetDataFillPattern(pInst);
      FS_MEMSET(_pSectorBuffer, Pattern, BytesPerPage);
      pData = _pSectorBuffer;
    }
    //
    // Calculate the physical sector index of the first sector in the block
    // and write the data to it.
    //
    SectorIndex  = _BlockIndex2SectorIndex0(pInst, pbi);
    SectorIndex += BRSI_BLOCK_INFO;
    r = _WriteSectorWithECC(pInst, pData, SectorIndex);
#if FS_NAND_VERIFY_WRITE
    if (r == 0) {
      r = _VerifySector(pInst, pData, SectorIndex);
    }
#endif // FS_NAND_VERIFY_WRITE
#if FS_NAND_MAX_BIT_ERROR_CNT
    if (r == 0) {
      r = _CheckDataBlockWithER(pInst, pbi, EraseCnt);
      if (r != RESULT_NO_ERROR) {
        pbi = 0;
        r = 1;                            // Errors detected in the created block.
      }
    }
#endif // FS_NAND_MAX_BIT_ERROR_CNT
    if (r == 0) {
      //
      // Store the information about the last written sector.
      //
      pDataBlock = pInst->pFirstDataBlockFree;
      if (pDataBlock == NULL) {
        //
        // Remove the least recently used block from the list.
        //
        pDataBlock = pInst->pFirstDataBlockInUse;
        if (pDataBlock != NULL) {
          while (pDataBlock->pNext != NULL) {
            pDataBlock = pDataBlock->pNext;
          }
          _DB_RemoveFromUsedList(pInst, pDataBlock);
          _DB_AddToFreeList(pInst, pDataBlock);
        }
      }
      pDataBlock = pInst->pFirstDataBlockFree;
      if (pDataBlock != NULL) {
        pDataBlock->pbi      = pbi;
        pDataBlock->brsiLast = BRSI_BLOCK_INFO;
        _DB_RemoveFromFreeList(pInst, pDataBlock);
        _DB_AddToUsedList(pInst, pDataBlock);
      }
      //
      // Update the logical to physical mapping table.
      //
      _L2P_Write(pInst, lbi, pbi);
    } else {
#if FS_NAND_MAX_BIT_ERROR_CNT
      if (pbi != 0u)
#endif // FS_NAND_MAX_BIT_ERROR_CNT
      {
        (void)_RemoveDataBlock(pInst, pbi);
        (void)_FreeBlock(pInst, pbi, EraseCnt);
        pbi = 0;                          // Error, could not write to created data block.
      }
    }
  }
  return pbi;
}

/*********************************************************************
*
*       _GetLastSectorInUse
*
*  Function description
*    Returns the block-relative index of the last sector in the block
*    that is not blank.
*
*  Parameters
*    pInst        Driver instance.
*    pbi          Index of the physical data block.
*    brsi         Index of the last physical sector to check.
*
*  Return value
*    !=BRSI_INVALID   Index of the sector relative to the beginning of the block.
*    ==BRSI_INVALID   An error has occurred or nor sector free in the block.
*
*  Additional information
*    We start searching from the end of the block since in this way
*    the checking runs faster with the block grouping feature enabled.
*
*    The function performs error recovery and it might relocate
*    the data block if a read error occurs while calculating
*    the location of the last written sector on the block.
*/
static unsigned _GetLastSectorInUse(NAND_UNI_INST * pInst, unsigned pbi, unsigned brsi) {
  int      r;
  unsigned brsiLast;
  unsigned SectorsPerBlock;
  U32      SectorIndex0;
  U32      SectorIndex;
  unsigned SectorStat;
  unsigned i;

  brsiLast        = BRSI_INVALID;                       // Set to indicate an error.
  SectorsPerBlock = 1uL << pInst->PPB_Shift;
  if (brsi < SectorsPerBlock) {
    brsiLast     = SectorsPerBlock - 1u;
    SectorIndex0 = _BlockIndex2SectorIndex0(pInst, pbi);
#if FS_NAND_SUPPORT_BLOCK_GROUPING
    //
    // If the block grouping feature is enabled then we have to check the if the BRSI of the last sector has been set.
    // The driver does this when it converts a data block to a work block to indicate that the copy operation completed
    // successfully. If this is the case, then we are not allowed to write to this data block.
    //
    if (_IsBlockGroupingEnabled(pInst) != 0) {
      unsigned brsiToCheck;

      brsiToCheck = BRSI_INVALID;
      r = _ReadBRSI(pInst, SectorIndex0 + brsiLast, &brsiToCheck);
      if ((r != 0) || (brsiToCheck != BRSI_INVALID)) {
        return BRSI_INVALID;                            // We cannot write to this data block.
      }
    }
#endif // FS_NAND_SUPPORT_BLOCK_GROUPING
    if (pInst->AllowBlankUnusedSectors == 0u) {
      //
      // If invalid sector data is filled with 0's, then we have to perform a blank check of the sector data.
      // TBD: We can avoid reading the entire page and the blank checking if we store in the spare area some
      //      flag telling us that the bytes in the page are not set to 0xFF.
      //
      for (i = 0; i < (SectorsPerBlock - 1u); ++i) {    // -1 since the first sector (page) in the block is not used for data storage.
        SectorIndex = SectorIndex0 + brsiLast;
        SectorStat  = SECTOR_STAT_WRITTEN;
        r = _ReadSectorWithECCAndER(pInst, _pSectorBuffer, SectorIndex, brsiLast);
        if ((r == RESULT_NO_ERROR) || (r == RESULT_BIT_ERRORS_CORRECTED) || (r == RESULT_BIT_ERROR_IN_ECC)) {
          if (r == RESULT_BIT_ERRORS_CORRECTED) {       // Bit error threshold reached?
            (void)_RelocateDataBlock(pInst, pbi);       // Move the data block to an other location.
            brsiLast = BRSI_INVALID;
            break;                                      // Stop searching since the data block has been relocated.
          }
          SectorStat = _LoadSectorStat(pInst);
          if (SectorStat == SECTOR_STAT_WRITTEN) {
            break;                                      // Found a sector that contains valid data.
          }
          if (_IsDataSpareBlank(pInst, _pSectorBuffer, _pSpareAreaData) == 0) {
            break;                                      // The sector is not blank. Stop searching.
          }
        } else {
          brsiLast = BRSI_INVALID;
          break;                                        // Stop searching since we cannot determine if the sector is written or not.
        }
        if (brsiLast <= brsi) {
          brsiLast = 0;
          break;                                        // Could not find any sector that contains valid data.
        }
        --brsiLast;                                     // Check the next sector.
      }
    } else {
      for (i = 0; i < (SectorsPerBlock - 1u); ++i) {    // -1 since the first sector (page) in the block is not used for data storage.
        SectorIndex = SectorIndex0 + brsiLast;
        SectorStat  = SECTOR_STAT_WRITTEN;
        r = _ReadSectorStat(pInst, SectorIndex, &SectorStat);
        if ((r == RESULT_NO_ERROR) || (r == RESULT_BIT_ERRORS_CORRECTED) || (r == RESULT_BIT_ERROR_IN_ECC)) {
          if (r == RESULT_BIT_ERRORS_CORRECTED) {       // Bit error threshold reached?
            (void)_RelocateDataBlock(pInst, pbi);       // Move the data block to an other location.
            brsiLast = BRSI_INVALID;
            break;                                      // Stop searching since the data block has been relocated.
          }
          if (SectorStat == SECTOR_STAT_WRITTEN) {
            break;                                      // Found a sector that contains valid data.
          }
        } else {
          brsiLast = BRSI_INVALID;
          break;                                        // Stop searching since we cannot determine if the sector is written or not.
        }
        if (brsiLast <= brsi) {
          brsiLast = 0;
          break;                                        // Could not find any sector that contains valid data.
        }
        --brsiLast;                                     // Check the next sector.
      }
    }
  }
  return brsiLast;
}

/*********************************************************************
*
*       _IsWriteToDataBlockAllowed
*
*  Function description
*    Checks if data can be written to the specified position
*    on a data block.
*
*  Parameters
*    pInst        Driver instance.
*    pbi          Index of the physical data block.
*    brsi         Index of the physical sector to write to.
*
*  Return value
*    !=0    Data can be written to data block.
*    ==0    Data cannot be written to data block.
*
*  Additional information
*    We can write data directly to a data block if the physical sector
*    on which we are about to write and the all following physical
*    sectors to the end of the block are empty. That is we are not
*    allowed to write out of order to meet the requirements of some
*    NAND flash devices.
*
*    The _GetLastSectorInUse() function performs error recovery
*    and it might relocate the data block if a read error occurs
*    while calculating the location of the last written sector on the block.
*/
static int _IsWriteToDataBlockAllowed(NAND_UNI_INST * pInst, unsigned pbi, unsigned brsi) {
  int                   r;
  unsigned              brsiLast;
  NAND_UNI_DATA_BLOCK * pDataBlock;

  r = 0;                        // Set to indicate that we cannot write to data block.
  //
  // Try to get the information about the last written sector from the internal list.
  //
  pDataBlock = _FindDataBlock(pInst, pbi);
  if (pDataBlock == NULL) {
    //
    // The information is not cached and has to be calculated.
    //
    brsiLast = _GetLastSectorInUse(pInst, pbi, brsi);
    if (brsiLast != BRSI_INVALID) {
      //
      // Remember the position of the last written sector.
      //
      pDataBlock = pInst->pFirstDataBlockFree;
      if (pDataBlock == NULL) {
        //
        // Remove the least recently used block from the list.
        //
        pDataBlock = pInst->pFirstDataBlockInUse;
        if (pDataBlock != NULL) {
          while (pDataBlock->pNext != NULL) {
            pDataBlock = pDataBlock->pNext;
          }
          _DB_RemoveFromUsedList(pInst, pDataBlock);
          _DB_AddToFreeList(pInst, pDataBlock);
        }
      }
      pDataBlock = pInst->pFirstDataBlockFree;
      if (pDataBlock != NULL) {
        pDataBlock->pbi      = pbi;
        pDataBlock->brsiLast = (U16)brsiLast;
        _DB_RemoveFromFreeList(pInst, pDataBlock);
        _DB_AddToUsedList(pInst, pDataBlock);
      }
    }
  } else {
    brsiLast = pDataBlock->brsiLast;
  }
  if (brsiLast < brsi) {
    r = 1;                          // It is possible to write to data block.
  }
  return r;
}

/*********************************************************************
*
*       _WriteSectorToDataBlock
*
*  Function description
*    Writes the data of a physical sector to a data block.
*
*  Parameters
*    pInst        Driver instance.
*    pbi          Index of the physical data block.
*    brsi         Index of the physical sector to write to.
*    pData        [IN] Sector data to be written. Cannot be NULL.
*
*  Return value
*    > 0    A correctable error occurred. The write operation has to be retried.
*    ==0    OK, data successfully written.
*    < 0    An uncorrectable error has occurred.
*/
static int _WriteSectorToDataBlock(NAND_UNI_INST * pInst, unsigned pbi, unsigned brsi, const U32 * pData) {
  int                   r;
  U32                   SectorIndex;
  NAND_UNI_DATA_BLOCK * pDataBlock;
  int                   Result;

  //
  // Initialize local variables.
  //
  r = -1;                                 // Set to indicate failure.
  if (brsi > BRSI_BLOCK_INFO) {           // The sector that contains block-related information in the spare area is written using _CreateDataBlock().
    //
    // Prepare the management data.
    //
    _ClearStaticSpareArea(pInst);
#if FS_NAND_SUPPORT_BLOCK_GROUPING
    //
    // If the block grouping is enabled and we write to the last page in the block then we have to set
    // the BRSI field to prevent that the mount operation discards this data block.
    //
    if (_IsBlockGroupingEnabled(pInst) != 0) {
      unsigned SectorsPerBlock;

      SectorsPerBlock = 1uL << pInst->PPB_Shift;
      if (brsi == (SectorsPerBlock - 1u)) {
        _StoreBRSI(pInst, brsi);
      }
    }
#endif // FS_NAND_SUPPORT_BLOCK_GROUPING
    _StoreSectorStat(pInst, SECTOR_STAT_WRITTEN);
    //
    // Calculate the index of the physical sector and write the data to it.
    //
    SectorIndex  = _BlockIndex2SectorIndex0(pInst, pbi);
    SectorIndex += brsi;
    r = _WriteSectorWithECC(pInst, pData, SectorIndex);
#if FS_NAND_VERIFY_WRITE
    if (r == 0) {
      //
      // Read back the written data.
      //
      Result = _VerifySector(pInst, pData, SectorIndex);
      if (Result != 0) {
        r = 1;                                            // Error, data read back does not match the data that has been written.
      }
    }
#endif // FS_NAND_VERIFY_WRITE
    if (r != 0) {
      //
      // Could not write to data block. Try to recover the already existing data.
      //
      Result = _RecoverDataBlock(pInst, pbi);
      if (Result != 0) {
        r = -1;                           // Error, could not recover data block.
      }
    }
#if FS_NAND_MAX_BIT_ERROR_CNT
    if (r == 0) {
      //
      // Check for uncorrectable bit errors and try to recover the data if necessary.
      //
      Result = _CheckDataBlockWithER(pInst, pbi, ERASE_CNT_INVALID);
      if (Result != 0) {
        r = 1;
      }
    }
#endif // FS_NAND_MAX_BIT_ERROR_CNT
    if (r == 0) {
      //
      // Remember on which sector we wrote the data.
      //
      pDataBlock = _FindDataBlock(pInst, pbi);
      if (pDataBlock != NULL) {
        pDataBlock->brsiLast = (U16)brsi;
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _TryWriteSectorToDataBlock
*
*  Function description
*    Tries to write the data of one logical sector to a data block.
*
*  Parameters
*    pInst            Driver instance.
*    LogSectorIndex   Index of the logical sector to be written.
*    pData            [IN] Sector data to be written. Cannot be NULL.
*
*  Return value
*    ==0    OK, data successfully written.
*    !=0    An error has occurred or the data cannot be written to data block.
*/
static int _TryWriteSectorToDataBlock(NAND_UNI_INST * pInst, U32 LogSectorIndex, const U32 * pData) {
  unsigned              lbi;
  unsigned              brsi;
  int                   r;
  unsigned              pbi;
  unsigned              MergeCnt;
  U32                   SectorIndex;
  NAND_UNI_WORK_BLOCK * pWorkBlock;
  unsigned              pbiWork;
  int                   Result;
  int                   NumRetries;

  //
  // Initialize the local variables.
  //
  r          = 1;               // Set to indicate failure.
  NumRetries = 0;
  brsi       = BRSI_INVALID;
  lbi        = _LogSectorIndex2LogBlockIndex(pInst, LogSectorIndex, &brsi);
  //
  // We stay in this loop until we have successfully written
  // the sector data or an unrecoverable error occurs.
  //
  for (;;) {
    //
    // Quit the write loop if the number of operations retried
    // has exceeded the configured limit.
    //
    if (NumRetries++ > FS_NAND_NUM_WRITE_RETRIES) {
      r = 1;
      break;                                                      // Error, cannot write sector data.
    }
    //
    // Get the index of the physical block assigned to the data block.
    //
    pbi = _L2P_Read(pInst, lbi);
    if (pbi == 0u) {
      //
      // No data block found. Check if there is a work block assigned
      // to the logical block we have to write to. If yes then read MergeCnt
      // from it and use it later.
      //
      MergeCnt = 0xFu;
      pWorkBlock = _FindWorkBlock(pInst, lbi);
      if (pWorkBlock != NULL) {
        pbiWork = pWorkBlock->pbi;
        SectorIndex  = _BlockIndex2SectorIndex0(pInst, pbiWork);
        SectorIndex |= BRSI_BLOCK_INFO;
        Result = _ReadMergeCnt(pInst, SectorIndex, &MergeCnt);
        if (Result == RESULT_BIT_ERRORS_CORRECTED) {
          //
          // Perform error recovery.
          //
          Result = _RelocateWorkBlock(pInst, pWorkBlock);
          if (Result != 0) {
            break;                                                // Error, could not relocate work block.
          }
        }
      }
      //
      // The data block does not exist therefore we have to create a new one.
      //
      if (brsi == BRSI_BLOCK_INFO) {
        //
        // Create the data block and write the data to first sector.
        //
        pbi = _CreateDataBlock(pInst, lbi, MergeCnt, pData);
        if (pbi != 0u) {
          r = 0;                                                  // OK, data block created.
          break;
        }
      } else {
        //
        // Create the data block and write the data.
        //
        pbi = _CreateDataBlock(pInst, lbi, MergeCnt, NULL);
        if (pbi != 0u) {
          Result = _WriteSectorToDataBlock(pInst, pbi, brsi, pData);
          if (Result == 0) {
            r = 0;
            break;                                                // OK, data written.
          }
        }
      }
      //
      // The write operation failed. Create a new data block and try again.
      //
    } else {
      //
      // Data block exists. Write the sector data to it if the sector on which we have to write
      // and the following sectors are empty.
      //
      if (_IsWriteToDataBlockAllowed(pInst, pbi, brsi) == 0) {
        break;
      }
      Result = _WriteSectorToDataBlock(pInst, pbi, brsi, pData);
      if (Result < 0) {
        break;                                                    // Unrecoverable error.
      }
      if (Result == 0) {
        r = 0;
        break;                                                    // OK, data written.
      }
      //
      // Write operation failed. The data stored in the data block has been recovered.
      // Try again to write the data.
      //
    }
    IF_STATS(pInst->StatCounters.NumWriteRetries++);
  }
  return r;
}

#endif // FS_NAND_SUPPORT_FAST_WRITE

/*********************************************************************
*
*       _WriteOneSector
*
*  Function description
*    Writes one logical sector to storage device. Can be used to invalidate
*    the data of a sector by setting pData to NULL.
*
*  Return value
*    ==0    Data successfully written.
*    !=0    An error has occurred.
*/
static int _WriteOneSector(NAND_UNI_INST * pInst, U32 LogSectorIndex, const void * pData) {
  int      r;
  unsigned SectorStat;

  //
  // Get the validity status of the sector data.
  //
  SectorStat = SECTOR_STAT_WRITTEN;
  r = _CalcSectorStat(pInst, LogSectorIndex, &SectorStat);
  if (r != 0) {
    return 1;                   // Error, could not read sector status.
  }
  //
  // Check if the sector has to be invalidated.
  //
  if (pData == NULL) {
    if (SectorStat == SECTOR_STAT_EMPTY) {
      return 0;                 // OK, the logical sector does not have to be invalidated.
    }
  }
#if FS_NAND_SUPPORT_FAST_WRITE
  //
  // Check if we can write directly to a data block.
  //
  if (SectorStat == SECTOR_STAT_EMPTY) {
    if (pData != NULL) {        // The sector data is invalidated by writing to a work block.
      r = _TryWriteSectorToDataBlock(pInst, LogSectorIndex, SEGGER_CONSTPTR2PTR(const U32, pData));
      if (r == 0) {
        return 0;               // OK, sector data written to data block.
      }
      //
      // Error recovery: Writing to data block has failed or not possible. Write the data to a work block.
      //
    }
  }
#endif // FS_NAND_SUPPORT_FAST_WRITE
  //
  // Write the logical sector to work block.
  //
  r = _WriteLogSectorToWorkBlock(pInst, LogSectorIndex, SEGGER_CONSTPTR2PTR(const U32, pData));
  return r;
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
static int _FreeOneSector(NAND_UNI_INST * pInst, U32 LogSectorIndex) {
  int SectorUsage;
  int r;

  r = 0;                    // No sector freed.
  SectorUsage = _GetSectorUsage(pInst, LogSectorIndex);
  if (SectorUsage == 0) {   // Sector in use?
    r = _WriteOneSector(pInst, LogSectorIndex, NULL);
  }
  return r;
}

/*********************************************************************
*
*        _FreeBlockByLBI
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
static int _FreeBlockByLBI(NAND_UNI_INST * pInst, unsigned lbi) {
  int                   r;
  int                   Result;
  unsigned              pbi;
  NAND_UNI_WORK_BLOCK * pWorkBlock;

  r = 0;                          // Set to indicate success.
  //
  // First, free the work block if one is assigned to logical block.
  //
  pWorkBlock = _FindWorkBlock(pInst, lbi);
  if (pWorkBlock != NULL) {
    pbi = pWorkBlock->pbi;
    _WB_RemoveFromUsedList(pInst, pWorkBlock);
    _WB_AddToFreeList(pInst, pWorkBlock);
    Result = _FreeBlock(pInst, pbi, ERASE_CNT_INVALID);
    if (Result != 0) {
      r = Result;                 // Error, could not free phy. block.
    }
  }
  //
  // Free the data block if one is assigned to the logical block.
  //
  pbi = _RemoveDataBlockByLBI(pInst, lbi);
  if (pbi != 0u) {
    Result = _FreeBlock(pInst, pbi, ERASE_CNT_INVALID);
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
*    Marks a logical sector as free. This routine is called from the
*    higher layer file system to help the driver to manage the data.
*    This way sectors which are no longer in use by the higher
*    layer file system do not need to be copied.
*/
static int _FreeSectors(NAND_UNI_INST * pInst, U32 SectorIndex, U32 NumSectors) {
  int      r;
  unsigned NumBlocks;
  unsigned lbi;
  int      Result;
  unsigned brsi;
  U32      NumSectorsAtOnce;
  unsigned SectorsPerBlock;
  U32      NumSectorsTotal;

  r = 0;                          // Set to indicate success.
  if (NumSectors == 0u) {
    return 0;                     // Nothing to do.
  }
  NumSectorsTotal = pInst->NumSectors;
  if ((SectorIndex >= NumSectorsTotal) || ((SectorIndex + NumSectors - 1u) >= NumSectorsTotal)) {
    return 1;                     // Error, invalid sector range.
  }
  //
  // Free sectors until we reach a block boundary.
  //
  do {
    (void)_LogSectorIndex2LogBlockIndex(pInst, SectorIndex, &brsi);
    if (brsi == 1u) {             // The first sector in the block is reserved for the erase count.
      break;                      // OK, we reached the next block boundary.
    }
    Result = _FreeOneSector(pInst, SectorIndex);
    if (Result < 0) {
      r = 1;                      // Error, could not free sector.
    } else {
      if (Result != 0) {
        IF_STATS_SECTOR_STATUS(pInst->StatCounters.NumValidSectors--);
      }
    }
    SectorIndex++;
  } while (--NumSectors != 0u);
  //
  // Free entire NAND blocks. Calculate the number of blocks we have to free.
  //
  NumBlocks = _LogSectorIndex2LogBlockIndex(pInst, NumSectors, NULL);
  if (NumBlocks != 0u) {
    SectorsPerBlock  = (1uL << pInst->PPB_Shift) - 1u;  // The first sector (page) in the block is not used for data storage.
    NumSectorsAtOnce = (U32)NumBlocks * SectorsPerBlock;
    lbi              = _LogSectorIndex2LogBlockIndex(pInst, SectorIndex, NULL);
    do {
      Result = _FreeBlockByLBI(pInst, lbi);
      if (Result < 0) {
        r = 1;                    // Error, could not free block.
      } else {
        if (Result != 0) {
          IF_STATS_SECTOR_STATUS(pInst->StatCounters.NumValidSectors -= NumSectorsAtOnce);
        }
      }
      ++lbi;
    } while (--NumBlocks != 0u);
    SectorIndex += NumSectorsAtOnce;
    NumSectors  -= NumSectorsAtOnce;
  }
  //
  // Free the remaining sectors.
  //
  if (NumSectors != 0u) {
    do {
      Result = _FreeOneSector(pInst, SectorIndex);
      if (Result < 0) {
        r = 1;                    // Error, could not free sector.
      } else {
        if (Result != 0) {
          IF_STATS_SECTOR_STATUS(pInst->StatCounters.NumValidSectors--);
        }
      }
      SectorIndex++;
    } while (--NumSectors != 0u);
  }
  return r;
}

#endif // FS_NAND_SUPPORT_TRIM

#if FS_NAND_SUPPORT_CLEAN

/*********************************************************************
*
*       _CleanOne
*
*  Function description
*    Executes a single "clean" operation.
*
*  Parameters
*    pInst          Driver instance.
*    pMoreToClean   [OUT] Set to 1 if more operations are required
*                         to clean the entire NAND flash device.
*
*  Return value
*    ==0    OK, clean operation successful.
*    !=0    An error occurred.
*/
static int _CleanOne(NAND_UNI_INST * pInst, int * pMoreToClean) {
  int r;

  r = 0;          // Set to indicate success.
  //
  // Clean the work block in use.
  //
  if (pInst->pFirstWorkBlockInUse != NULL) {
    r = _CleanWorkBlock(pInst, pInst->pFirstWorkBlockInUse, BRSI_INVALID, NULL);
  }
  //
  // Now check if there is more work to do.
  //
  if (pMoreToClean != NULL) {
    *pMoreToClean = 0;
    if (pInst->pFirstWorkBlockInUse != NULL) {
      *pMoreToClean = 1;    // At least one more work block has to be cleaned.
    }
  }
  return r;
}

/*********************************************************************
*
*       _Clean
*
*  Function description
*    Converts all work blocks into data blocks and erases all free blocks.
*/
static int _Clean(NAND_UNI_INST * pInst) {
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
static U32 _GetCleanCnt(const NAND_UNI_INST * pInst) {
  U32                   CleanCnt;
  NAND_UNI_WORK_BLOCK * pWorkBlock;

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
*       _CheckBadBlockSignature
*
*  Function description
*    Checks if the SEGGER bad block signature is present in the
*    spare area of the last read page.
*
*  Return value
*    Byte offset of the signature or 0 if the signature was not found.
*/
static unsigned _CheckBadBlockSignature(const NAND_UNI_INST * pInst) {
  unsigned   BytesPerSpareArea;
  unsigned   PlanesPerOperation;
  U8       * pSpare;
  const U8 * pInfo;
  unsigned   NumBytesToCompare;
  int        DoCompare;
  unsigned   OffSignature;
  unsigned   Off;
  unsigned   PPO_Shift;

  PPO_Shift          = pInst->PPO_Shift;
  PlanesPerOperation = 1uL << PPO_Shift;
  pSpare             = _pSpareAreaData;
  OffSignature       = 0;
  Off                = 0;
  do {
    BytesPerSpareArea   = pInst->BytesPerSpareArea;
    BytesPerSpareArea >>= PPO_Shift;
    pInfo               = _acInfo;
    DoCompare           = 0;
    NumBytesToCompare   = NUM_BYTES_BAD_BLOCK_SIGNATURE;
    //
    // Compare the signature byte-by-byte.
    //
    do {
      if (DoCompare == 0) {
        if (*pSpare == *pInfo) {
          ++pInfo;
          --NumBytesToCompare;
          if (OffSignature == 0u) {
            OffSignature = Off;
          }
          DoCompare    = 1;                 // First byte of the signature found.
        }
      } else {
        if (NumBytesToCompare != 0u) {
          if (*pSpare != *pInfo) {
            DoCompare         = 0;          // Driver signature for bad block do not match. Continue searching.
            pInfo             = _acInfo;
            NumBytesToCompare = NUM_BYTES_BAD_BLOCK_SIGNATURE;
          } else {
            ++pInfo;
            --NumBytesToCompare;
          }
        }
      }
      ++pSpare;
      ++Off;
    } while (--BytesPerSpareArea != 0u);
    if (NumBytesToCompare != 0u) {
      OffSignature = 0;
      break;
    }
  } while (--PlanesPerOperation != 0u);
  return OffSignature;
}

/*********************************************************************
*
*       _IsDriverBadBlockMarking
*
*  Function description
*    Tests if the driver marked the block as defective.
*    A signature is stored to spare area in order to indicate that
*    the driver marked the physical block as defective.
*
*  Parameters
*    pInst          [IN]  Driver instance.
*    PhyBlockIndex  Index of the physical block to be tested.
*    pErrorType     [OUT] Type of error that caused the block to
*                   be marked as defective.
*    pErrorBRSI     [OUT] Index of the sector index inside the NAND
*                   block where the error occurred.
*
*  Return value
*    ==0      Block was marked as defective by manufacturer.
*    ==1      Block was marked as defective by the driver.
*/
static int _IsDriverBadBlockMarking(NAND_UNI_INST * pInst, unsigned PhyBlockIndex, int * pErrorType, unsigned * pErrorBRSI) {
  U8       * pSpare;
  U32        PageIndex;
  unsigned   BytesPerSpareArea;
  unsigned   PPB_Shift;
  unsigned   BPG_Shift;
  unsigned   BytesPerSpareStripe;
  unsigned   ECCBlocksPerPage;
  unsigned   BytesPerPage;
  unsigned   ErrorTypeOff;
  unsigned   ErrorBRSIOff;
  int        ErrorType;
  unsigned   ErrorBRSI;
  unsigned   OffSignature;
  unsigned   Off;

  PPB_Shift         = pInst->PPB_Shift;
  BPG_Shift         = _GetBPG_Shift(pInst);
  PageIndex         = (U32)PhyBlockIndex << (PPB_Shift - BPG_Shift);
  BytesPerSpareArea = pInst->BytesPerSpareArea;
  (void)_DisableHW_ECCIfRequired(pInst);
  (void)_ReadSpareEx(pInst, PageIndex, _pSpareAreaData, 0, BytesPerSpareArea);
  (void)_EnableHW_ECCIfRequired(pInst);
  OffSignature = _CheckBadBlockSignature(pInst);
  if (OffSignature != 0u) {
    //
    // Load information about the error that caused
    // the block to be marked as defective.
    //
    BytesPerPage        = pInst->BytesPerPage;
    ECCBlocksPerPage    = BytesPerPage >> pInst->ldBytesPerECCBlock;
    BytesPerSpareStripe = BytesPerSpareArea / ECCBlocksPerPage;
    Off                 = OffSignature & (BytesPerSpareStripe - 1u);
    ErrorTypeOff        = BytesPerSpareStripe * SPARE_STRIPE_INDEX_ERROR_TYPE + Off;
    ErrorBRSIOff        = BytesPerSpareStripe * SPARE_STRIPE_INDEX_ERROR_BRSI + Off;
    pSpare              = _pSpareAreaData;
    ErrorType = (int)FS_LoadU16BE(pSpare + ErrorTypeOff);
    ErrorBRSI = (unsigned)FS_LoadU16BE(pSpare + ErrorBRSIOff);
    if (pErrorType != NULL) {
      *pErrorType = ErrorType;
    }
    if (pErrorBRSI != NULL) {
      *pErrorBRSI = ErrorBRSI;
    }
    return 1;                             // Driver marked the block as bad. This block can be erased.
  }
  return 0;
}

/*********************************************************************
*
*       _IsBlockErasable
*
*  Function description
*    Checks whether the driver is allowed to erase the given group of blocks.
*    The groups containing physical blocks marked as defective by the manufacturer
*    are never erased. The groups containing physical blocks marked as defective
*    by the driver can be erased if the FS_NAND_RECLAIM_DRIVER_BAD_BLOCKS
*    compile time switch is set to 1 (which is the default).
*
*  Parameter
*    pInst        [IN]  Driver instance.
*    BlockIndex   Index of the group of blocks to be erased.
*
*  Return value
*    ==1    The group of blocks can be erased.
*    ==0    The group of blocks can not be erased.
*/
static int _IsBlockErasable(NAND_UNI_INST * pInst, unsigned BlockIndex) {
  int      r;
  unsigned PhyBlockIndex;
  unsigned NumBlocks;
  unsigned BPG_Shift;

  r             = 1;                      // Set to indicate an erasable block.
  BPG_Shift     = _GetBPG_Shift(pInst);
  PhyBlockIndex = BlockIndex << BPG_Shift;
  NumBlocks     = 1uL << BPG_Shift;
  (void)_DisableHW_ECCIfRequired(pInst);  // Temporarily disable the HW ECC during the data transfer to avoid ECC errors.
  do {
    if (_IsPhyBlockBad(pInst, PhyBlockIndex) != 0) {
#if FS_NAND_RECLAIM_DRIVER_BAD_BLOCKS
      if (pInst->ReclaimDriverBadBlocks == 0u) {
        r = 0;
        break;                            // Block can not be erased.
      }
      if (_IsDriverBadBlockMarking(pInst, PhyBlockIndex, NULL, NULL) == 0) {
        r = 0;                            // Block can not be erased.
        break;
      }
#else
      r = 0;                              // Block can not be erased.
      break;
#endif // FS_NAND_RECLAIM_DRIVER_BAD_BLOCKS
    }
    ++PhyBlockIndex;
  } while (--NumBlocks != 0u);
  (void)_EnableHW_ECCIfRequired(pInst);   // Re-enable the HW ECC.
  return r;
}

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
static int _LowLevelFormat(NAND_UNI_INST * pInst) {
  int        r;
  U32        NumBlocks;
  unsigned   iBlock;
  U8       * pPageBuffer;
  unsigned   NumPagesPerBlock;

  pInst->LLMountFailed = 0;
  pInst->IsLLMounted   = 0;
  pPageBuffer          = (U8 *)_pSectorBuffer;
  //
  // Move the start of NAND partition (FirstBlock)
  // to skip over eventual bad blocks in order to
  // prevent a format error.
  //
  r = _SkipLeadingBadBlocks(pInst);
  if (r != 0) {
    return 1;             // Error
  }
  //
  // Erase the first sector/phy. block. This block is guaranteed to be valid.
  //
  r = _EraseBlock(pInst, SECTOR_INDEX_FORMAT_INFO);
  if (r != 0) {
    return 1;             // Error
  }
  //
  // Erase NAND flash blocks which are valid. Valid NAND flash blocks are blocks that
  // contain a 0xff in the first byte of the spare area of the first two pages of the block
  //
  NumBlocks = pInst->NumBlocks;
  for (iBlock = 1; iBlock < NumBlocks; iBlock++) {
    if (_IsBlockErasable(pInst, iBlock) != 0) {
      r = _EraseBlock(pInst, iBlock);
      if (r != 0) {
        return 1;         // Error, could not erase block.
      }
    } else {
      //
      // The block is defective.
      //
      IF_STATS(pInst->StatCounters.NumBadBlocks++);
    }
  }
  IF_STATS_SECTOR_STATUS(pInst->StatCounters.NumValidSectors = 0);
  //
  // Write the format information to first sector of the first block.
  //
  NumPagesPerBlock = 1uL << pInst->PPB_Shift;
  FS_MEMSET(pPageBuffer, 0xFF, pInst->BytesPerPage);
  FS_MEMCPY(pPageBuffer, _acInfo, sizeof(_acInfo));
  FS_StoreU32BE(pPageBuffer + INFO_OFF_LLFORMAT_VERSION,    LLFORMAT_VERSION);
  FS_StoreU32BE(pPageBuffer + INFO_OFF_NUM_LOG_BLOCKS,      pInst->NumLogBlocks);
  FS_StoreU32BE(pPageBuffer + INFO_OFF_NUM_WORK_BLOCKS,     pInst->NumWorkBlocks);
  FS_StoreU32BE(pPageBuffer + INFO_OFF_NUM_BLOCKS,          NumBlocks);
  FS_StoreU32BE(pPageBuffer + INFO_OFF_NUM_PAGES_PER_BLOCK, NumPagesPerBlock);
  _ClearStaticSpareArea(pInst);
  r = _WriteSectorWithECC(pInst, _pSectorBuffer, SECTOR_INDEX_FORMAT_INFO);
#if FS_NAND_VERIFY_WRITE
  if (r == 0) {
    r = _VerifySector(pInst, _pSectorBuffer, SECTOR_INDEX_FORMAT_INFO);
  }
#endif
  return r;
}

/*********************************************************************
*
*       _AllocInstIfRequired
*
*  Function description
*    Allocate memory for the specified unit if required.
*/
static NAND_UNI_INST * _AllocInstIfRequired(U8 Unit) {
  NAND_UNI_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  pInst = NULL;
  if (Unit < (U8)FS_NAND_NUM_UNITS) {
    pInst = _apInst[Unit];
    if (pInst == NULL) {
      FS_ALLOC_ZEROED_PTR(SEGGER_PTR2PTR(void *, &pInst), (I32)sizeof(NAND_UNI_INST), "NAND_UNI_INST");
      if (pInst != NULL) {
        _apInst[Unit] = pInst;
        pInst->Unit = Unit;
        //
        // Initialize parameters to default values.
        //
#if FS_NAND_MAX_BIT_ERROR_CNT
        pInst->MaxBitErrorCnt          = FS_NAND_MAX_BIT_ERROR_CNT;
#endif
#if FS_NAND_RECLAIM_DRIVER_BAD_BLOCKS
        pInst->ReclaimDriverBadBlocks  = 1;     // By default, we erase the blocks marked as defective by the driver.
#endif
        pInst->AllowReadErrorBadBlocks = 1;     // By default, we mark a block as defective in case of an uncorrectable bit error.
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
*    Returns the instance of a driver by its unit number.
*/
static NAND_UNI_INST * _GetInst(U8 Unit) {
  NAND_UNI_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  pInst = NULL;
  if (Unit < (U8)FS_NAND_NUM_UNITS) {
    pInst = _apInst[Unit];
  }
  return pInst;
}

/*********************************************************************
*
*       _Init
*
*  Function description
*    Identifies and initializes the NAND flash and stores its parameters
*    to the driver instance.
*/
static int _Init(NAND_UNI_INST * pInst) {
  int r;
  U32 BytesPerSector;
  U32 BytesPerSpareArea;

  ASSERT_PHY_TYPE_IS_SET(pInst);
  //
  // Allocate the sector buffer and the spare area buffer which are common to all driver instances.
  //
#if (FS_NAND_MAX_PAGE_SIZE > 0)
  BytesPerSector = FS_NAND_MAX_PAGE_SIZE;
  if (_ldMaxPageSize != 0u) {
    BytesPerSector = 1uL << _ldMaxPageSize;
  }
#else
  BytesPerSector = FS_Global.MaxSectorSize;
#endif
#if (FS_NAND_MAX_SPARE_AREA_SIZE > 0)
  BytesPerSpareArea = FS_NAND_MAX_SPARE_AREA_SIZE;
  if (_MaxSpareAreaSize != 0u) {
    BytesPerSpareArea = _MaxSpareAreaSize;
  }
#else
  BytesPerSpareArea = BytesPerSector >> 5;
#endif
  FS_ALLOC_ZEROED_PTR(SEGGER_PTR2PTR(void *, &_pSectorBuffer),  (I32)BytesPerSector, "NAND_UNI_SECTOR_BUFFER");
  if (_pSectorBuffer == NULL) {
    return 1;                      // Error, failed to allocate memory for the sector buffer.
  }
  FS_ALLOC_ZEROED_PTR(SEGGER_PTR2PTR(void *, &_pSpareAreaData), (I32)BytesPerSpareArea, "NAND_UNI_SPARE_BUFFER");
  if (_pSpareAreaData == NULL) {
    return 1;                      // Error, failed to allocate memory for the spare area buffer.
  }
#if FS_NAND_VERIFY_WRITE
  FS_ALLOC_ZEROED_PTR(SEGGER_PTR2PTR(void *, &_pVerifyBuffer),  (I32)BytesPerSector, "NAND_UNI_VERIFY_BUFFER");
  if (_pVerifyBuffer == NULL) {
    return 1;                      // Error, failed to allocate memory for the verification buffer.
  }
#endif
#if FS_NAND_ENABLE_ERROR_RECOVERY
  FS_ALLOC_ZEROED_PTR(SEGGER_PTR2PTR(void *, &_pSpareAreaDataER), (I32)BytesPerSpareArea, "NAND_UNI_SPARE_BUFFER_ER");
  if (_pSpareAreaDataER == NULL) {
    return 1;                      // Error, failed to allocate memory for the error recovery.
  }
#endif
  r = _ReadApplyDeviceParas(pInst);
  if (r != 0) {
    return 1;                      // Failed to identify NAND Flash or unsupported type
  }
  //
  // Check if the allocated buffers for the sector and for the spare area are large enough.
  //
  if (BytesPerSector < pInst->BytesPerPage) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_UNI: _Init: The sector buffer is too small."));
    return 1;
  }
  if (BytesPerSpareArea < pInst->BytesPerSpareArea) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_UNI: _Init: The spare area buffer is too small."));
    return 1;
  }
  //
  // Get the write protection status.
  //
  if (pInst->pPhyType->pfIsWP(pInst->Unit) != 0) {
    pInst->IsWriteProtected = 1;
  }
  //
  // Initialization done.
  //
  pInst->IsInited = 1;
  return 0;
}

/*********************************************************************
*
*       _WriteAPI
*/
static const WRITE_API _WriteAPI = {
  _ClearBlock,
  _CleanWorkBlock,
  _RecoverDataBlock,
  _MarkAsReadOnly,
  _FreeBadBlock
#if FS_NAND_SUPPORT_BLOCK_GROUPING
  , _FreeWorkBlock
#endif // FS_NAND_SUPPORT_BLOCK_GROUPING
};

/*********************************************************************
*
*       _InitIfRequired
*/
static int _InitIfRequired(NAND_UNI_INST * pInst) {
  int r;

  r = 0;          // Set to indicate success.
  if (pInst->IsInited == 0u) {
    r = _Init(pInst);
  }
  return r;
}

/*********************************************************************
*
*       _Unmount
*
*  Function description
*    Unmount the NAND flash device.
*/
static void _Unmount(NAND_UNI_INST * pInst) {
  pInst->IsInited             = 0;
  pInst->IsLLMounted          = 0;
  pInst->LLMountFailed        = 0;
  pInst->MRUFreeBlock         = 0;
  pInst->pFirstWorkBlockFree  = NULL;
  pInst->pFirstWorkBlockInUse = NULL;
#if FS_NAND_SUPPORT_FAST_WRITE
  pInst->pFirstDataBlockFree  = NULL;
  pInst->pFirstDataBlockInUse = NULL;
#endif // FS_NAND_SUPPORT_FAST_WRITE
#if FS_NAND_ENABLE_STATS
  FS_MEMSET(&pInst->StatCounters, 0, sizeof(pInst->StatCounters));
#endif
}

/*********************************************************************
*
*       _ExecCmdGetDevInfo
*/
static int _ExecCmdGetDevInfo(NAND_UNI_INST * pInst, void * pBuffer) {
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
      pDevInfo->BytesPerSector = pInst->BytesPerPage;
      r = 0;
    }
  }
  return r;
}

/*********************************************************************
*
*       _ExecCmdRequiresFormat
*/
static int _ExecCmdRequiresFormat(NAND_UNI_INST * pInst) {
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
static int _ExecCmdUnmount(NAND_UNI_INST * pInst) {
  _Unmount(pInst);
  return 0;
}

/*********************************************************************
*
*       _ExecCmdGetSectorUsage
*/
static int _ExecCmdGetSectorUsage(NAND_UNI_INST * pInst, int Aux, void * pBuffer) {
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

#if FS_SUPPORT_DEINIT

/*********************************************************************
*
*       _ExecCmdDeInit
*/
static int _ExecCmdDeInit(NAND_UNI_INST * pInst) {
  U8                    Unit;
  NAND_UNI_WORK_BLOCK * pWorkBlock;

  Unit = pInst->Unit;
  if (pInst->pPhyType != NULL) {
    if (pInst->pPhyType->pfDeInit != NULL) {
      pInst->pPhyType->pfDeInit(Unit);
    }
  }
  FS_FREE(pInst->pLog2PhyTable);
  FS_FREE(pInst->pFreeMap);
  if (pInst->paWorkBlock != NULL) {     // The array is allocated only when the volume is mounted.
    pWorkBlock = &pInst->paWorkBlock[0];
    FS_FREE(pWorkBlock->paAssign);      // This array is allocated at once for all the work blocks. The address of the allocated memory is stored to the first work block.
    FS_FREE(pInst->paWorkBlock);
  }
#if FS_NAND_SUPPORT_FAST_WRITE
  if (pInst->paDataBlock != NULL) {     // The array is allocated only when the volume is mounted.
    FS_FREE(pInst->paDataBlock);
  }
#endif // FS_NAND_SUPPORT_FAST_WRITE
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
#if FS_NAND_VERIFY_WRITE
    FS_FREE(_pVerifyBuffer);
    _pVerifyBuffer = NULL;
#endif // FS_NAND_VERIFY_WRITE
#if FS_NAND_ENABLE_ERROR_RECOVERY
    FS_FREE(_pSpareAreaDataER);
    _pSpareAreaDataER = NULL;
#endif // FS_NAND_ENABLE_ERROR_RECOVERY
  }
  return 0;
}

#endif // FS_SUPPORT_DEINIT

/*********************************************************************
*
*       _ExecCmdFormatLowLevel
*/
static int _ExecCmdFormatLowLevel(NAND_UNI_INST * pInst) {
  int r;
  int Result;

  r = -1;         // Set to indicate failure.
  Result = _LowLevelFormat(pInst);
  if (Result == 0) {
    r = 0;
  }
  return r;
}

#if FS_NAND_SUPPORT_CLEAN

/*********************************************************************
*
*       _ExecCmdCleanOne
*/
static int _ExecCmdCleanOne(NAND_UNI_INST * pInst, void * pBuffer) {
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
static int _ExecCmdClean(NAND_UNI_INST * pInst) {
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
static int _ExecCmdGetCleanCnt(NAND_UNI_INST * pInst, void * pBuffer) {
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
static int _ExecCmdFreeSectors(NAND_UNI_INST * pInst, int Aux, const void * pBuffer) {
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

#if FS_NAND_ENABLE_ERROR_RECOVERY

/*********************************************************************
*
*       _ExecCmdSetReadErrorCallback
*/
static int _ExecCmdSetReadErrorCallback(NAND_UNI_INST * pInst, void * pBuffer) {
  int                  r;
  FS_READ_ERROR_DATA * pReadErrorData;

  r = -1;         // Set to indicate a failure.
  if (pBuffer != NULL) {
    pReadErrorData       = SEGGER_PTR2PTR(FS_READ_ERROR_DATA, pBuffer);
    pInst->ReadErrorData = *pReadErrorData;       // struct copy
    r = 0;
  }
  return r;
}

#endif // FS_NAND_ENABLE_ERROR_RECOVERY

/*********************************************************************
*
*       _GetBlockInfo
*
*   Function description
*     Returns information about the specified NAND block.
*
*  Parameters
*    pInst         Driver instance.
*    BlockIndex    Index of the physical block to get information about.
*    pBlockInfo    [OUT] Information about the NAND block.
*    Flags         Specifies the information to be returned.
*
*  Return value
*    ==0 - OK, information returned.
*    !=0 - An error occurred.
*/
static int _GetBlockInfo(NAND_UNI_INST * pInst, U32 BlockIndex, FS_NAND_BLOCK_INFO * pBlockInfo, unsigned Flags) {
  unsigned        iSector;
  U32             SectorIndexSrc0;
  U32             EraseCnt;
  U32             lbi;
  unsigned        NumSectorsBlank;               // Sectors are not used yet.
  unsigned        NumSectorsValid;               // Sectors contain correct data.
  unsigned        NumSectorsInvalid;             // Sectors have been invalidated.
  unsigned        NumSectorsECCError;            // Sectors have incorrect ECC.
  unsigned        NumSectorsECCCorrectable;      // Sectors have correctable ECC error.
  unsigned        NumSectorsErrorInECC;
  unsigned        SectorsPerBlock;
  unsigned        BlockType;
  int             r;
  unsigned        Type;
  unsigned        SectorStat;
  unsigned        brsi;
  int             IsDriverBadBlock;
  int             ErrorType;
  unsigned        ErrorBRSI;
  unsigned        BPG_Shift;
  U32             PhyBlockIndex;
  unsigned        BlocksPerGroup;

  NumSectorsBlank          = 0;
  NumSectorsValid          = 0;
  NumSectorsInvalid        = 0;
  NumSectorsECCError       = 0;
  NumSectorsECCCorrectable = 0;
  NumSectorsErrorInECC     = 0;
  lbi                      = 0;
  IsDriverBadBlock         = 0;
  SectorsPerBlock          = 1uL << pInst->PPB_Shift;
  SectorIndexSrc0          = _BlockIndex2SectorIndex0(pInst, BlockIndex);
  EraseCnt                 = 0;
  ErrorType                = 0;
  ErrorBRSI                = 0;
  FS_MEMSET(pBlockInfo, 0, sizeof(FS_NAND_BLOCK_INFO));
  if ((Flags & FS_NAND_BLOCK_INFO_FLAG_BAD_STATUS) != 0u) {
    if (_IsBlockBad(pInst, BlockIndex) != 0) {
      BPG_Shift      = _GetBPG_Shift(pInst);
      PhyBlockIndex  = BlockIndex << BPG_Shift;
      Type           = NAND_BLOCK_TYPE_BAD;
      BlocksPerGroup = 1uL << BPG_Shift;
      do {
        IsDriverBadBlock = _IsDriverBadBlockMarking(pInst, PhyBlockIndex++, &ErrorType, &ErrorBRSI);
        if (IsDriverBadBlock != 0) {
          break;
        }
      } while (--BlocksPerGroup != 0u);
      goto Done;
    }
  }
  //
  // Read the second sector of the block to get the erase count and the LBI.
  //
  Type = FS_NAND_BLOCK_TYPE_UNKNOWN;
  for (iSector = BRSI_BLOCK_INFO; iSector < SectorsPerBlock; ++iSector) {
    r = _ReadSpareAreaWithECC(pInst, SectorIndexSrc0 + iSector);
    if ((r == RESULT_NO_ERROR) || (r == RESULT_BIT_ERRORS_CORRECTED) || (r == RESULT_BIT_ERROR_IN_ECC)) {
      if (iSector == BRSI_BLOCK_INFO) {
        BlockType = _LoadBlockType(pInst);
        EraseCnt  = _LoadEraseCnt(pInst);
        lbi       = _LoadLBI(pInst);
        switch(BlockType) {
        case BLOCK_TYPE_EMPTY:
          Type = FS_NAND_BLOCK_TYPE_EMPTY;
          break;
        case BLOCK_TYPE_WORK:
          Type = FS_NAND_BLOCK_TYPE_WORK;
          break;
        case BLOCK_TYPE_DATA:
          Type = FS_NAND_BLOCK_TYPE_DATA;
          break;
        default:
          Type = FS_NAND_BLOCK_TYPE_UNKNOWN;
          break;
        }
        if ((Flags & FS_NAND_BLOCK_INFO_FLAG_SECTOR_STATUS) == 0u) {
          break;
        }
      }
      SectorStat = _LoadSectorStat(pInst);
      if (SectorStat == SECTOR_STAT_WRITTEN) {
        brsi = _LoadBRSI(pInst);
        if (brsi == 0u) {
          ++NumSectorsInvalid;                // Error: invalid BRSI.
        }
        if (brsi > SectorsPerBlock) {
          if (_IsPageBlank(pInst, SectorIndexSrc0 + iSector) != 0) {
            ++NumSectorsBlank;                // OK, found an empty sector.
          } else {
            ++NumSectorsInvalid;              // Error, invalid BRSI.
          }
        }
        if (r == RESULT_BIT_ERRORS_CORRECTED) {
          ++NumSectorsECCCorrectable;         // Data have been corrected
        } else if (r == RESULT_BIT_ERROR_IN_ECC) {
          ++NumSectorsErrorInECC;
        } else {
          ++NumSectorsValid;
        }
      } else {
        if (_IsPageBlank(pInst, SectorIndexSrc0 + iSector) != 0) {
          ++NumSectorsBlank;              // OK, found an empty sector.
        } else {
          ++NumSectorsInvalid;            // Error, invalid BRSI.
        }
      }
    } else {
      ++NumSectorsECCError;               // Error not correctable by ECC or NAND read error
    }
  }
  //
  // Try to read the erase count from the spare area of the first page in the block.
  //
  if (Type == FS_NAND_BLOCK_TYPE_EMPTY) {
    r = _ReadSpareAreaWithECC(pInst, SectorIndexSrc0);
    if ((r == RESULT_NO_ERROR) || (r == RESULT_BIT_ERRORS_CORRECTED) || (r == RESULT_BIT_ERROR_IN_ECC)) {
      EraseCnt = _LoadEraseCnt(pInst);
    }
  }
Done:
  pBlockInfo->Type                     = (U8)Type;
  pBlockInfo->EraseCnt                 = EraseCnt;
  pBlockInfo->lbi                      = lbi;
  pBlockInfo->NumSectorsBlank          = (U16)NumSectorsBlank;
  pBlockInfo->NumSectorsECCCorrectable = (U16)NumSectorsECCCorrectable;
  pBlockInfo->NumSectorsErrorInECC     = (U16)NumSectorsErrorInECC;
  pBlockInfo->NumSectorsECCError       = (U16)NumSectorsECCError;
  pBlockInfo->NumSectorsInvalid        = (U16)NumSectorsInvalid;
  pBlockInfo->NumSectorsValid          = (U16)NumSectorsValid;
  pBlockInfo->IsDriverBadBlock         = (U8)IsDriverBadBlock;
  pBlockInfo->BadBlockErrorType        = (U8)ErrorType;
  pBlockInfo->BadBlockErrorBRSI        = (U16)ErrorBRSI;
  return 0;
}

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
*       _NAND_WriteRO
*
*   Function description
*     FS driver function. Does nothing and returns with an error.
*
*   Return value
*     !=0   An error has occurred.
*/
static int _NAND_WriteRO(U8 Unit, U32 SectorIndex, const void * pData, U32 NumSectors, U8 RepeatSame) {
  FS_USE_PARA(Unit);
  FS_USE_PARA(SectorIndex);
  FS_USE_PARA(pData);
  FS_USE_PARA(NumSectors);
  FS_USE_PARA(RepeatSame);
  FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_UNI: _NAND_WriteRO: Operation not supported."));
  return 1;
}

/*********************************************************************
*
*       _NAND_Write
*
*   Function description
*     FS driver function. Writes one or more logical sectors to storage device.
*
*   Return value
*     ==0   Data successfully written.
*     !=0   An error has occurred.
*/
static int _NAND_Write(U8 Unit, U32 SectorIndex, const void * pData, U32 NumSectors, U8 RepeatSame) {
  const U8      * pData8;
  NAND_UNI_INST * pInst;
  int             r;
  int             HasFatalError;
  U32             NumSectorsTotal;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;                       // Error, could not get driver instance.
  }
  r = _LowLevelMountIfRequired(pInst);
  if (r != 0) {
    return 1;                       // Error, could not mount the NAND flash.
  }
  if (pInst->IsWriteProtected != 0u) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_UNI: _NAND_Write: NAND flash is write protected."));
    return 1;
  }
  NumSectorsTotal = pInst->NumSectors;
  if ((SectorIndex >= NumSectorsTotal) || ((SectorIndex + NumSectors - 1u) >= NumSectorsTotal)) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NAND_UNI: _NAND_Write: Invalid sector block."));
    return 1;
  }
  //
  // Do not perform any operation if no sector data is provided
  // or no sectors are written.
  //
  if ((pData == NULL) || (NumSectors == 0u)) {
    return 0;
  }
  //
  // Write the data one sector at a time.
  //
  pData8 = SEGGER_CONSTPTR2PTR(const U8, pData);
  HasFatalError = (int)pInst->HasFatalError;
  for (;;) {
#if (FS_NAND_ENABLE_STATS != 0) && (FS_NAND_ENABLE_STATS_SECTOR_STATUS != 0)
    int SectorUsage;
#endif // FS_NAND_ENABLE_STATS != 0 && FS_NAND_ENABLE_STATS_SECTOR_STATUS != 0

    //
    // The usage of a sector is useful only in debug builds
    // to update the number of valid sectors.
    //
#if (FS_NAND_ENABLE_STATS != 0) && (FS_NAND_ENABLE_STATS_SECTOR_STATUS != 0)
    SectorUsage = _GetSectorUsage(pInst, SectorIndex);
#endif // FS_NAND_ENABLE_STATS != 0 && FS_NAND_ENABLE_STATS_SECTOR_STATUS != 0
    r = _WriteOneSector(pInst, SectorIndex, pData8);
    if (r != 0) {
      CHECK_CONSISTENCY(pInst);
      return 1;                     // Error, could not write sector.
    }
    if ((HasFatalError == 0) && (pInst->HasFatalError != 0u)) {
      CHECK_CONSISTENCY(pInst);
      return 1;
    }
#if FS_NAND_ENABLE_STATS
#if FS_NAND_ENABLE_STATS_SECTOR_STATUS
    //
    // Increment the sector valid count only if the sector is written for the first time.
    //
    if (SectorUsage != 0) {
      pInst->StatCounters.NumValidSectors++;
    }
#endif // FS_NAND_ENABLE_STATS_SECTOR_STATUS
    pInst->StatCounters.WriteSectorCnt++;
#endif  // FS_NAND_ENABLE_STATS
    if (--NumSectors == 0u) {
      break;
    }
    if (RepeatSame == 0u) {
      pData8 += pInst->BytesPerPage;
    }
    SectorIndex++;
  }
  CHECK_CONSISTENCY(pInst);
  return 0;                         // OK, data written.
}

/*********************************************************************
*
*       _NAND_Read
*
*  Function description
*    Driver callback function.
*    Reads one or more logical sectors from storage device.
*
*  Return value
*      0    Data successfully written.
*    !=0    An error has occurred.
*/
static int _NAND_Read(U8 Unit, U32 SectorIndex, void * pData, U32 NumSectors) {
  U8            * pData8;
  NAND_UNI_INST * pInst;
  int             r;
  U32             NumSectorsTotal;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;                       // Error, could not get driver instance.
  }
  //
  // Make sure device is low-level mounted. If it is not, there is nothing we can do.
  //
  r = _LowLevelMountIfRequired(pInst);
  if (r != 0) {
    return 1;                       // Error, could not mount the NAND flash.
  }
  NumSectorsTotal = pInst->NumSectors;
  if ((SectorIndex >= NumSectorsTotal) || ((SectorIndex + NumSectors - 1u) >= NumSectorsTotal)) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NAND_UNI: _NAND_Read: Invalid sector block."));
    return 1;
  }
  pData8 = SEGGER_PTR2PTR(U8, pData);
  //
  // Read the data one sector at a time.
  //
  do {
    r = _ReadOneSector(pInst, SectorIndex, pData8);
    if (r != 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NAND_UNI: _NAND_Read: Failed to read sector."));
      CHECK_CONSISTENCY(pInst);
      return 1;                               // Error, could not read data.
    }
    pData8 += pInst->BytesPerPage;
    SectorIndex++;
    IF_STATS(pInst->StatCounters.ReadSectorCnt++);
  } while (--NumSectors != 0u);
  CHECK_CONSISTENCY(pInst);
  return 0;                                    // OK, data read.
}

/*********************************************************************
*
*       _NAND_IoCtlRO
*/
static int _NAND_IoCtlRO(U8 Unit, I32 Cmd, I32 Aux, void * pBuffer) {
  NAND_UNI_INST * pInst;
  int             r;
  int             IsLLMounted;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return -1;                // Error, could not get driver instance.
  }
  r = -1;
  IsLLMounted = (int)pInst->IsLLMounted;
  switch (Cmd) {
  case FS_CMD_GET_DEVINFO:
    r = _ExecCmdGetDevInfo(pInst, pBuffer);
    break;
  case FS_CMD_REQUIRES_FORMAT:
    r = _ExecCmdRequiresFormat(pInst);
    break;
  case FS_CMD_UNMOUNT:
    //lint through
  case FS_CMD_UNMOUNT_FORCED:
    r = _ExecCmdUnmount(pInst);
    break;
  case FS_CMD_GET_SECTOR_USAGE:
    r = _ExecCmdGetSectorUsage(pInst, Aux, pBuffer);
    break;
#if FS_SUPPORT_DEINIT
  case FS_CMD_DEINIT:
    r = _ExecCmdDeInit(pInst);
    break;
#endif
  default:
    //
    // Command not supported.
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
*       _NAND_IoCtl
*/
static int _NAND_IoCtl(U8 Unit, I32 Cmd, I32 Aux, void * pBuffer) {
  NAND_UNI_INST * pInst;
  int             r;
  int             CheckConsistency;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return -1;                // Error, could not get driver instance.
  }
  CheckConsistency = 1;
  switch (Cmd) {
  case FS_CMD_FORMAT_LOW_LEVEL:
    r = _ExecCmdFormatLowLevel(pInst);
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
#if FS_NAND_ENABLE_ERROR_RECOVERY
  case FS_CMD_SET_READ_ERROR_CALLBACK:
    r = _ExecCmdSetReadErrorCallback(pInst, pBuffer);
    break;
#endif
  default:
    r = _NAND_IoCtlRO(Unit, Cmd, Aux, pBuffer);
    CheckConsistency = 0;                                 // The consistency is checked in _NAND_IoCtlRO().
    break;
  }
  if (CheckConsistency != 0) {
    CHECK_CONSISTENCY(pInst);
  }
  return r;
}

/*********************************************************************
*
*       _NAND_AddDevice
*
*   Function description
*     Allocates memory for a new driver instance.
*
*   Return value
*     >=0   Command successfully executed, Unit no.
*     < 0   Error, could not add device
*/
static int _NAND_AddDevice(void) {
  NAND_UNI_INST * pInst;

  if (_NumUnits >= (U8)FS_NAND_NUM_UNITS) {
    return -1;                      // Error, too many driver instances.
  }
  pInst = _AllocInstIfRequired((U8)_NumUnits);
  if (pInst == NULL) {
    return -1;                      // Error, could not create driver instance.
  }
  pInst->pWriteAPI = &_WriteAPI;
  return (int)_NumUnits++;
}

/*********************************************************************
*
*       _NAND_AddDeviceRO
*
*   Function description
*     Allocates memory for a new driver instance.
*
*   Return value
*     >=0   Command successfully executed, Unit no.
*     < 0   Error, could not add device
*/
static int _NAND_AddDeviceRO(void) {
  NAND_UNI_INST * pInst;

  if (_NumUnits >= (U8)FS_NAND_NUM_UNITS) {
    return -1;                      // Error, too many driver instances.
  }
  pInst = _AllocInstIfRequired((U8)_NumUnits);
  if (pInst == NULL) {
    return -1;                      // Error, could not create driver instance.
  }
  pInst->pWriteAPI = NULL;
  return (int)_NumUnits++;
}

/*********************************************************************
*
*       _NAND_Init
*
*   Function description
*     Initializes the instance and identifies the storage device.
*
*   Return value
*     ==0   Device OK and ready for operation.
*     < 0   An error has occurred.
*/
static int _NAND_Init(U8 Unit) {
  NAND_UNI_INST * pInst;
  int             r;

  r = 1;                  // Set to indicate an error.
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
*       Driver API tables
*/
const FS_DEVICE_TYPE FS_NAND_UNI_Driver = {
  _NAND_GetDriverName,
  _NAND_AddDevice,
  _NAND_Read,
  _NAND_Write,
  _NAND_IoCtl,
  _NAND_Init,
  _NAND_GetStatus,
  _NAND_GetNumUnits
};

const FS_DEVICE_TYPE FS_NAND_UNI_RO_Driver = {
  _NAND_GetDriverName,
  _NAND_AddDeviceRO,
  _NAND_Read,
  _NAND_WriteRO,
  _NAND_IoCtlRO,
  _NAND_Init,
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
*       FS__NAND_UNI_SetTestHookFailSafe
*/
void FS__NAND_UNI_SetTestHookFailSafe(FS_NAND_TEST_HOOK_NOTIFICATION * pfTestHook) {
  _pfTestHookFailSafe = pfTestHook;
}

/*********************************************************************
*
*       FS__NAND_UNI_SetTestHookDataReadBegin
*/
void FS__NAND_UNI_SetTestHookDataReadBegin(FS_NAND_TEST_HOOK_DATA_READ_BEGIN * pfTestHook) {
  _pfTestHookDataReadBegin = pfTestHook;
}

/*********************************************************************
*
*       FS__NAND_UNI_SetTestHookDataReadEnd
*/
void FS__NAND_UNI_SetTestHookDataReadEnd(FS_NAND_TEST_HOOK_DATA_READ_END * pfTestHook) {
  _pfTestHookDataReadEnd = pfTestHook;
}

/*********************************************************************
*
*       FS__NAND_UNI_SetTestHookDataReadExBegin
*/
void FS__NAND_UNI_SetTestHookDataReadExBegin(FS_NAND_TEST_HOOK_DATA_READ_EX_BEGIN * pfTestHook) {
  _pfTestHookDataReadExBegin = pfTestHook;
}

/*********************************************************************
*
*       FS__NAND_UNI_SetTestHookDataReadExEnd
*/
void FS__NAND_UNI_SetTestHookDataReadExEnd(FS_NAND_TEST_HOOK_DATA_READ_EX_END * pfTestHook) {
  _pfTestHookDataReadExEnd = pfTestHook;
}

/*********************************************************************
*
*       FS__NAND_UNI_SetTestHookDataWriteExBegin
*/
void FS__NAND_UNI_SetTestHookDataWriteExBegin(FS_NAND_TEST_HOOK_DATA_WRITE_EX_BEGIN * pfTestHook) {
  _pfTestHookDataWriteExBegin = pfTestHook;
}

/*********************************************************************
*
*       FS__NAND_UNI_SetTestHookDataWriteExEnd
*/
void FS__NAND_UNI_SetTestHookDataWriteExEnd(FS_NAND_TEST_HOOK_DATA_WRITE_EX_END * pfTestHook) {
  _pfTestHookDataWriteExEnd = pfTestHook;
}

/*********************************************************************
*
*       FS__NAND_UNI_SetTestHookBlockErase
*/
void FS__NAND_UNI_SetTestHookBlockErase(FS_NAND_TEST_HOOK_BLOCK_ERASE * pfTestHook) {
  _pfTestHookBlockErase = pfTestHook;
}

#endif // FS_SUPPORT_TEST

/*********************************************************************
*
*       FS__NAND_UNI_GetPhyType
*/
const FS_NAND_PHY_TYPE * FS__NAND_UNI_GetPhyType(U8 Unit) {
  NAND_UNI_INST          * pInst;
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
*       FS__NAND_UNI_MarkBlockAsBad
*/
int FS__NAND_UNI_MarkBlockAsBad(U8 Unit, unsigned BlockIndex) {
  NAND_UNI_INST * pInst;
  int             r;

  r     = 1;          // Set to indicate error.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    r = _InitIfRequired(pInst);
    if (r == 0) {
      r = _MarkBlockAsBad(pInst, BlockIndex, 0, 0);
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

#if FS_NAND_ENABLE_STATS

/*********************************************************************
*
*       FS_NAND_UNI_GetStatCounters
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
*    The statistical counters can be cleared via FS_NAND_UNI_ResetStatCounters().
*/
void FS_NAND_UNI_GetStatCounters(U8 Unit, FS_NAND_STAT_COUNTERS * pStat) {
  NAND_UNI_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    if (pStat != NULL) {
      *pStat = pInst->StatCounters;
    }
  }
}

/*********************************************************************
*
*       FS_NAND_UNI_ResetStatCounters
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
*    The statistical counters can be queried via FS_NAND_UNI_GetStatCounters().
*/
void FS_NAND_UNI_ResetStatCounters(U8 Unit) {
  NAND_UNI_INST         * pInst;
  FS_NAND_STAT_COUNTERS * pStat;
  U32                     NumFreeBlocks;
  U32                     NumBadBlocks;
  U32                     NumValidSectors;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pStat = &pInst->StatCounters;
    //
    // Preserve the following counter values since these values
    // are calculated when the NAND flash is mounted.
    //
    NumFreeBlocks   = pStat->NumFreeBlocks;
    NumBadBlocks    = pStat->NumBadBlocks;
    NumValidSectors = pStat->NumValidSectors;
    FS_MEMSET(pStat, 0, sizeof(FS_NAND_STAT_COUNTERS));
    pStat->NumFreeBlocks   = NumFreeBlocks;
    pStat->NumBadBlocks    = NumBadBlocks;
    pStat->NumValidSectors = NumValidSectors;
  }
}

#endif // FS_NAND_ENABLE_STATS

/*********************************************************************
*
*       FS_NAND_UNI_SetPhyType
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
*    once for each instance of the Universal NAND driver. The driver instance
*    is identified by the Unit parameter. First Universal NAND driver instance
*    added to the file system via a FS_AddDevice(&FS_NAND_UNI_Driver) call
*    has the unit number 0, the Universal NAND driver added by a second call
*    to FS_AddDevice() has the unit number 1 and so on.
*/
void FS_NAND_UNI_SetPhyType(U8 Unit, const FS_NAND_PHY_TYPE * pPhyType) {
  NAND_UNI_INST * pInst;

  pInst  = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    if (pPhyType != NULL) {
      pInst->pPhyType = pPhyType;
    }
  }
}

/*********************************************************************
*
*       FS_NAND_UNI_SetECCHook
*
*  Function description
*    Configures the ECC algorithm to be used for the correction of bit errors.
*
*  Parameters
*    Unit     Index of the driver instance (0-based)
*    pECCHook [IN] ECC algorithm.
*
*  Additional information
*    This function is optional. By default, the Universal NAND driver
*    uses an software algorithm that is capable of correcting 1 bit
*    errors and of detecting 2 bit errors. If the NAND flash device
*    requires a better error correction, the application has to specify
*    an different ECC algorithm via FS_NAND_UNI_SetECCHook().
*
*    The ECC algorithms can be either implemented in the software or
*    the calculation routines can take advantage of any dedicated ECC
*    HW available on the target system.
*
*    The following ECC algorithms are supported:
*
*    +---------------------+-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
*    | Type                | Description                                                                                                                                                                                                       |
*    +---------------------+-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
*    | FS_NAND_ECC_HW_NULL | This is a pseudo ECC algorithm that requests the Universal NAND driver to use the internal HW ECC of a NAND flash device.                                                                                         |
*    +---------------------+-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
*    | FS_NAND_ECC_HW_4BIT | This is a pseudo ECC algorithm that requests the Universal NAND driver to use HW 4 bit error correction. It can be used for example with dedicated a NAND flash controller that comes with a configurable HW ECC. |
*    +---------------------+-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
*    | FS_NAND_ECC_HW_8BIT | This is a pseudo ECC algorithm that requests the Universal NAND driver to use HW 8 bit error correction. It can be used for example with dedicated a NAND flash controller that comes with a configurable HW ECC. |
*    +---------------------+-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
*    | FS_NAND_ECC_SW_1BIT | This is an software algorithm that is able to correct 1 bit errors and detect 2 bit errors. More specifically it can correct a 1 bit error per 256 bytes of data and a 1 bit error per 4 bytes of spare area.     |
*    +---------------------+-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
*
*    Additional software algorithms with error correction capabilities
*    greater than 1 are supported via the SEGGER emLib ECC component
*    (www.segger.com/products/security-iot/emlib/variations/ecc/).
*/
void FS_NAND_UNI_SetECCHook(U8 Unit, const FS_NAND_ECC_HOOK * pECCHook) {
  NAND_UNI_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->pECCHook = pECCHook;
  }
}

/*********************************************************************
*
*       FS_NAND_UNI_SetBlockRange
*
*  Function description
*    Specifies which NAND blocks the driver can use to store the data.
*
*  Parameters
*    Unit             Index of the driver instance (0-based).
*    FirstBlock       Index of the first NAND flash block to
*                     be used as storage (0-based).
*    MaxNumBlocks     Maximum number of NAND flash blocks
*                     to be used as storage.
*
*  Additional information
*    This function is optional. By default, the Universal NAND driver uses
*    all blocks of the NAND flash as data storage. FS_UNI_NAND_SetBlockRange()
*    is useful when a part of the NAND flash has to be used for another
*    purpose, for example to store the application program used by a boot loader,
*    and therefore it cannot be managed by the Universal NAND driver. Limiting
*    the number of blocks used by the Universal NAND driver can also help reduce the RAM usage.
*
*    FirstBlock is the index of the first physical NAND block were
*    0 is the index of the first block of the NAND flash device.
*    MaxNumBlocks can be larger that the actual number of available
*    NAND blocks in which case the Universal NAND driver silently truncates
*    the value to reflect the actual number of NAND blocks available.
*
*    The Universal NAND driver uses the first NAND block in the range to store
*    management information at low-level format. If the first NAND block happens
*    to be marked as defective, then the next usable NAND block is used.
*
*    If the FS_UNI_NAND_SetBlockRange() is used to subdivide the same
*    physical NAND flash device into two or more partitions than
*    the application has to make sure that the created partitions do not overlap.
*
*    The read optimization of the FS_NAND_PHY_2048x8 physical layer has to be
*    disabled when this function is used to partition the NAND flash device
*    in order to ensure data consistency. The read cache can be disabled at
*    runtime via FS_NAND_2048x8_DisableReadCache().
*/
void FS_NAND_UNI_SetBlockRange(U8 Unit, U16 FirstBlock, U16 MaxNumBlocks) {
  NAND_UNI_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->FirstBlockConf = FirstBlock;
    pInst->MaxNumBlocks   = MaxNumBlocks;
  }
}

/*********************************************************************
*
*       FS_NAND_UNI_SetMaxEraseCntDiff
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
*    the Universal NAND driver performs the wear leveling.
*    The wear leveling procedure makes sure that the NAND blocks
*    are equally erased to meet the life expectancy of the storage
*    device by keeping track of the number of times a NAND block
*    has been erased (erase count). The Universal NAND driver executes
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
void FS_NAND_UNI_SetMaxEraseCntDiff(U8 Unit, U32 EraseCntDiff) {
  NAND_UNI_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->MaxEraseCntDiff = EraseCntDiff;
  }
}

/*********************************************************************
*
*       FS_NAND_UNI_SetNumWorkBlocks
*
*  Function description
*    Sets number of work blocks the Universal NAND driver uses for write operations.
*
*  Parameters
*    Unit           Index of the driver instance (0-based).
*    NumWorkBlocks  Number of work blocks.
*
*  Additional information
*    This function is optional. It can be used to change the default
*    number of work blocks according to the requirements of the application.
*    Work blocks are physical NAND blocks that the Universal NAND driver
*    uses to temporarily store the data written to NAND flash device.
*    The Universal NAND driver calculates at low-level format the number of
*    work blocks based on the total number of blocks available on the
*    NAND flash device.
*
*    By default, the NAND driver allocates 10% of the total number of
*    NAND blocks used as storage, but no more than 10 NAND blocks.
*    The minimum number of work blocks allocated by default depends
*    on whether journaling is used or not. If the journal is active
*    4 work blocks are allocated, else Universal NAND driver allocates 3
*    work blocks. The currently allocated number of work blocks can
*    be checked via FS_NAND_UNI_GetDiskInfo(). The value is returned in the
*    NumWorkBlocks member of the FS_NAND_DISK_INFO structure.
*
*    Increasing the number of work blocks can help increase the write
*    performance of the Universal NAND driver. At the same time the RAM
*    usage of the Universal NAND driver increases since each configured
*    work block requires a certain amount of RAM for the data management.
*    This is a trade-off between write performance and RAM usage.
*
*    The new value take effect after the NAND flash device is low-level
*    formatted via the FS_FormatLow() API function.
*/
void FS_NAND_UNI_SetNumWorkBlocks(U8 Unit, U32 NumWorkBlocks) {
  NAND_UNI_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->NumWorkBlocksConf = NumWorkBlocks;
  }
}

/*********************************************************************
*
*       FS_NAND_UNI_AllowBlankUnusedSectors
*
*  Function description
*    Configures if the data of unused sectors has to be initialized.
*
*  Parameters
*    Unit     Index of driver instance (0-based).
*    OnOff    Specifies if the feature has to be enabled or disabled
*             * ==0   Sector data is filled with 0s.
*             * !=0   Sector data is not initialized (all bytes remain
*                     set to 0xFF).
*
*  Additional information
*    This function is optional.
*    The default behavior of the Universal NAND driver is to fill
*    the data of unused logical sectors with 0s. This done in order
*    to reduce the chance of a bit error caused by an unwanted transition
*    from 1 to 0 of the value stored to these memory cells.
*
*    Some NAND flash devices may wear out faster than expected if
*    excessive number of bytes are set to 0. This limitation is typically
*    documented in the data sheet of the corresponding NAND flash.
*    For such devices it is recommended configure the Universal NAND
*    driver to do not initialize the unused sectors with 0s. This can
*    be realized by calling FS_NAND_UNI_AllowBlankUnusedSectors() with
*    the OnOff parameter set to 1 in FS_X_AddDevices().
*/
void FS_NAND_UNI_AllowBlankUnusedSectors(U8 Unit, U8 OnOff) {
  NAND_UNI_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->AllowBlankUnusedSectors = OnOff;
  }
}

/*********************************************************************
*
*       FS_NAND_UNI_AllowReadErrorBadBlocks
*
*  Function description
*    Configures if a block is marked as defective on read fatal error.
*
*  Parameters
*    Unit     Index of driver instance (0-based).
*    OnOff    Specifies if the feature has to be enabled or disabled
*             * ==0   The block is not marked as defective.
*             * !=0   The block is marked as defective.
*
*  Additional information
*    This function is optional.
*    The default behavior of the Universal NAND driver is to mark a
*    block as defective if a read error or and uncorrectable bit error
*    occurs while reading data from a page of that block.
*/
void FS_NAND_UNI_AllowReadErrorBadBlocks(U8 Unit, U8 OnOff) {
  NAND_UNI_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->AllowReadErrorBadBlocks = OnOff;
  }
}

#if FS_NAND_MAX_BIT_ERROR_CNT

/*********************************************************************
*
*       FS_NAND_UNI_SetMaxBitErrorCnt
*
*  Function description
*    Configures the number of bit errors that trigger the relocation
*    of the data stored in a NAND block.
*
*  Parameters
*    Unit           Index of the driver instance (0-based).
*    BitErrorCnt    Number of physical bit errors.
*
*  Additional information
*    This function is optional and is active only when the
*    file system is compiled with FS_NAND_MAX_BIT_ERROR_CNT set to
*    a value greater than 0.
*
*    FS_NAND_UNI_SetMaxBitErrorCnt() can be used to configure when
*    the Universal NAND Driver has to copy the contents of a NAND block
*    to another location in order to prevent the accumulation of bit errors.
*    BitErrorCnt has to be smaller than or equal to the bit error
*    correction requirement of the NAND flash device. The feature can
*    be disabled by calling FS_NAND_UNI_SetMaxBitErrorCnt() with
*    BitErrorCnt set to 0.
*
*    The lifetime of the NAND flash device can be negatively affected
*    if this feature is enabled due to the increased number of block
*    erase operations.
*/
void FS_NAND_UNI_SetMaxBitErrorCnt(U8 Unit, unsigned BitErrorCnt) {
  NAND_UNI_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->MaxBitErrorCnt = (U8)BitErrorCnt;
  }
}

/*********************************************************************
*
*       FS_NAND_UNI_SetWriteDisturbHandling
*
*  Function description
*    Configures if the bit errors caused by write operations are
*    handled or not.
*
*  Parameters
*    Unit     Index of the driver instance (0-based).
*    OnOff    Activation status of the feature.
*             * ==0   Write disturb errors are not handled (default).
*             * !=0   Write disturb errors are handled.
*
*  Additional information
*    This function is optional and is active only when the
*    file system is compiled with FS_NAND_MAX_BIT_ERROR_CNT
*    set to a value greater than 0.
*
*    A write operation can cause bit errors in the pages located on
*    the same NAND block with the page being currently written.
*    Normally, these bit errors are corrected later when the data of the
*    NAND block is copied internally by the Universal NAND driver to
*    another location on the NAND flash device during a wear leveling,
*    a garbage collection or a write operation. This is the default
*    behavior of the Universal NAND driver.
*
*    The Universal NAND driver is also able to check for and correct
*    any bit errors right a after the write operation in order to reduce
*    the accumulation of bit errors that can lead to a data loss.
*    This error handling mode can be enabled by calling
*    FS_NAND_UNI_SetWriteDisturbHandling() with the OnOff parameter
*    set to 1. In this error handling mode if the number of bit errors
*    in any page in the checked NAND block is greater than or equal
*    to the value configured via FS_NAND_UNI_SetMaxBitErrorCnt() then
*    the NAND block is copied to another location on the NAND flash
*    device in order to correct the bit errors.
*
*    The lifetime of the NAND flash device can be negatively affected
*    if this feature is enabled due to the increased number of block
*    erase operations. The write performance can be negatively affected
*    as well.
*/
void FS_NAND_UNI_SetWriteDisturbHandling(U8 Unit, U8 OnOff) {
  NAND_UNI_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->HandleWriteDisturb = OnOff;
  }
}

#endif // FS_NAND_MAX_BIT_ERROR_CNT

#if FS_NAND_SUPPORT_BLOCK_GROUPING

/*********************************************************************
*
*       FS_NAND_UNI_SetNumBlocksPerGroup
*
*  Function description
*    Specifies the number of physical NAND blocks in a virtual block.
*
*  Parameters
*    Unit           Index of the driver instance (0-based).
*    BlocksPerGroup Number of physical blocks in a virtual block.
*                   The value must be a power of 2.
*
*  Return value
*     ==0 - OK, value configured.
*     !=0 - Error code indicating the failure reason.
*
*  Additional information
*    This function is optional.
*    It can be used to specify how many physical NAND blocks
*    are grouped together to form a virtual block. Grouping physical blocks
*    helps reduce the RAM usage of the driver when the NAND flash device
*    contains a large number of physical blocks. For example, the
*    dynamic RAM usage of the Universal NAND driver using a NAND flash
*    device with 8196 blocks and a page size of 2048 bytes is about 20 KB.
*    The dynamic RAM usage is reduced to about 7 KB if 4 physical blocks
*    are grouped together.
*
*    The FS_NAND_SUPPORT_BLOCK_GROUPING configuration define has to be set
*    to 1 in order to enable this function. The FS_NAND_SUPPORT_BLOCK_GROUPING
*    configuration define is set to 1 by default. When set to 0 the function
*    returns an error if called in the application.
*
*    FS_NAND_UNI_SetNumBlocksPerGroup() is optional and may be called
*    only from FS_X_AddDevices(). Changing the block grouping requires
*    a low-level format of the NAND flash device.
*/
int FS_NAND_UNI_SetNumBlocksPerGroup(U8 Unit, unsigned BlocksPerGroup) {
  int             r;
  NAND_UNI_INST * pInst;

  r = FS_ERRCODE_INVALID_PARA;
  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->BPG_Shift = (U8)_ld(BlocksPerGroup);
    r = FS_ERRCODE_OK;
  }
  return r;
}

#endif // FS_NAND_SUPPORT_BLOCK_GROUPING

/*********************************************************************
*
*       FS_NAND_UNI_SetCleanThreshold
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
*    ==0 - OK, threshold has been set.
*    !=0 - An error occurred.
*
*  Additional information
*    This function is optional.
*    It can be used by the application to prepare the NAND flash device to
*    write data as fast as possible once when an event occurs such an
*    unexpected reset. At the startup, the application reserves free space,
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
int FS_NAND_UNI_SetCleanThreshold(U8 Unit, unsigned NumBlocksFree, unsigned NumSectorsFree) {
  NAND_UNI_INST * pInst;
  unsigned        NumBlocksFreeOld;
  unsigned        NumSectorsFreeOld;
  int             r;

  r = FS_ERRCODE_INVALID_PARA;        // Set to indicate error.
  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    r = FS_ERRCODE_OK;                // Set to indicate success.
    NumBlocksFreeOld  = pInst->NumBlocksFree;
    NumSectorsFreeOld = pInst->NumSectorsFree;
    pInst->NumBlocksFree  = (U16)NumBlocksFree;
    pInst->NumSectorsFree = (U16)NumSectorsFree;
    if ((NumBlocksFree  > NumBlocksFreeOld) ||
        (NumSectorsFree > NumSectorsFreeOld)) {
      if (pInst->IsLLMounted != 0u) {
        r = _ApplyCleanThreshold(pInst);
      }
    }
    if ((NumBlocksFree  < NumBlocksFreeOld) ||
        (NumSectorsFree < NumSectorsFreeOld)) {
      //
      // Temporarily disable the active wear leveling to prevent a potential block erase operation.
      // The active wear leveling is enabled as soon as the first NAND block is about to be erased.
      //
      pInst->ActiveWLStatus = ACTIVE_WL_DISABLED_TEMP;
    }
  }
  return r;
}

/*********************************************************************
*
*       FS_NAND_UNI_Clean
*
*  Function description
*    Makes storage space available for fast write operations.
*
*  Parameters
*    Unit             Index of the driver instance (0-based).
*    NumBlocksFree    Number of blocks to be kept free.
*    NumSectorsFree   Number of sectors to be kept free on each block.
*
*  Return value
*    ==0 - OK, space has been made available.
*    !=0 - An error occurred.
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
int FS_NAND_UNI_Clean(U8 Unit, unsigned NumBlocksFree, unsigned NumSectorsFree) {
  NAND_UNI_INST * pInst;
  int             r;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;         // Error, could not allocate driver instance.
  }
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return 1;         // Error, device initialization failed.
  }
  r = _LowLevelMountIfRequired(pInst);
  if (r != 0) {
    return 1;         // Error, could not mount NAND device.
  }
  r = _CleanLimited(pInst, NumBlocksFree, NumSectorsFree);
  return r;
}

/*********************************************************************
*
*       FS_NAND_UNI_ReadPhySector
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
*    >=0 - OK, sector data read.
*    < 0 - An error occurred.
*
*  Additional information
*    This function is optional.
*/
int FS_NAND_UNI_ReadPhySector(U8 Unit, U32 PhySectorIndex, void * pData, unsigned * pNumBytesData, void * pSpare, unsigned * pNumBytesSpare) {
  NAND_UNI_INST * pInst;
  U32             NumPhySectors;
  int             r;
  U32             NumBytes2Copy;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return FS_ERRCODE_OUT_OF_MEMORY;        // Error, could not allocate driver instance.
  }
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return -1;
  }
  NumPhySectors = (U32)pInst->NumBlocks * (1uL << pInst->PPB_Shift);
  if (PhySectorIndex >= NumPhySectors) {
    return -1;        // Error, invalid sector number.
  }
  r = _ReadSectorWithECC(pInst, _pSectorBuffer, PhySectorIndex);
  if (pNumBytesData != NULL) {
    NumBytes2Copy   = SEGGER_MIN(pInst->BytesPerPage, *pNumBytesData);
    *pNumBytesData  = NumBytes2Copy;
    FS_MEMCPY(pData, _pSectorBuffer, NumBytes2Copy);
  }
  if (pNumBytesSpare != NULL) {
    NumBytes2Copy   = SEGGER_MIN((unsigned)pInst->BytesPerSpareArea, *pNumBytesSpare);
    *pNumBytesSpare = NumBytes2Copy;
    FS_MEMCPY(pSpare, _pSpareAreaData, NumBytes2Copy);
  }
  return r;           // OK, data read.
}

/*********************************************************************
*
*       FS_NAND_UNI_EraseFlash
*
*  Function description
*    Erases the entire NAND partition.
*
*  Return value
*    >= 0 . Number of blocks which failed to erase.
*    <  0 - An error occurred.
*
*  Parameters
*    Unit   Index of the driver instance (0-based).
*
*  Additional information
*    This function is optional. After the call to this function all the
*    bytes in the NAND partition are set to 0xFF.
*
*    This function has to be used with care, since it also erases
*    blocks marked as defective and therefore the information about
*    the block status will be lost. FS_NAND_EraseFlash() can be used
*    without this side effect on storage devices that are guaranteed
*    to not have any bad blocks, such as DataFlash devices.
*/
int FS_NAND_UNI_EraseFlash(U8 Unit) {
  NAND_UNI_INST * pInst;
  U32             NumBlocks;
  unsigned        iBlock;
  int             NumErrors;
  int             r;

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
    return FS_ERRCODE_INIT_FAILURE;
  }
  //
  // Erase all the NAND flash blocks including the ones marked as defective.
  //
  NumErrors = 0;
  NumBlocks = pInst->NumBlocks;
  for (iBlock = 0; iBlock < NumBlocks; iBlock++) {
    r = _EraseBlock(pInst, iBlock);
    if (r != 0) {
      FS_DEBUG_WARN((FS_MTYPE_DRIVER, "NAND_UNI: Could not erase block %d.", iBlock));
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
*       FS_NAND_UNI_GetDiskInfo
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
*    FS_NAND_UNI_GetDiskInfo() can be used to get information about
*    the NAND flash device and about the instance of the Universal
*    NAND driver that is used to access it. If the NAND flash device
*    is formatted then FS_NAND_UNI_GetDiskInfo() also returns statistical
*    information about the usage of NAND blocks. If this information
*    is not relevant to the application FS_NAND_UNI_Mount() can be
*    called instead which typically requires less time to complete.
*
*    FS_NAND_UNI_GetDiskInfo() mounts the NAND flash device if required
*    and leaves it in this state upon return.
*
*    This function is not required for the functionality of the
*    Universal NAND driver and is typically not linked in production
*    builds.
*/
int FS_NAND_UNI_GetDiskInfo(U8 Unit, FS_NAND_DISK_INFO * pDiskInfo) {
  NAND_UNI_INST * pInst;
  unsigned        iBlock;
  U32             NumBlocks;
  U32             NumUsedPhyBlocks;
  U32             NumBadPhyBlocks;
  U32             EraseCntMax;
  U32             EraseCntMin;
  U32             EraseCntAvg;
  U32             EraseCnt;
  U32             EraseCntTotal;
  U32             NumEraseCnt;
  int             r;
  int             IsFormatted;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;         // Error, could not allocate driver instance.
  }
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return 1;         // Error, device initialization failed.
  }
  //
  // Initialize the counters.
  //
  NumUsedPhyBlocks = 0;
  NumBadPhyBlocks  = 0;
  NumBlocks        = pInst->NumBlocks;
  EraseCntMax      = 0;
  EraseCntMin      = 0xFFFFFFFFuL;
  EraseCntAvg      = 0;
  NumEraseCnt      = 0;
  EraseCntTotal    = 0;
  IsFormatted      = 0;
  //
  // Some of the information can be collected only when the NAND flash is low-level mounted.
  //
  (void)_LowLevelMountIfRequired(pInst);
  if (pInst->IsLLMounted != 0u) {
    IsFormatted = 1;                // OK, the NAND flash device is formatted.
  }
  if (IsFormatted != 0) {
    //
    // Check each block of the NAND flash and collect the information into counters.
    //
    for (iBlock = 0; iBlock < NumBlocks; iBlock++) {
      U32 SectorIndex;

      //
      // Count allocated blocks.
      //
      if (_IsBlockFree(pInst, iBlock) == 0) {
        NumUsedPhyBlocks++;
      }
      //
      // Count bad blocks.
      //
      if (_IsBlockBad(pInst, iBlock) != 0) {
        NumBadPhyBlocks++;
        continue;
      }
      //
      // The spare area of the first page in a block stores the erase count.
      //
      SectorIndex = _BlockIndex2SectorIndex0(pInst, iBlock);
      r = _ReadSpareAreaWithECC(pInst, SectorIndex);
      if ((r == RESULT_NO_ERROR) || (r == RESULT_BIT_ERRORS_CORRECTED) || (r == RESULT_BIT_ERROR_IN_ECC)) {
        EraseCnt = _LoadEraseCnt(pInst);
        //
        // Update the min and max erase counts.
        //
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
    //
    // Compute the average erase count of all blocks.
    //
    if (NumEraseCnt != 0u) {
      EraseCntAvg = EraseCntTotal / NumEraseCnt;
    } else {
      EraseCntAvg = 0;
    }
  }
  //
  // Store the collected information to user structure.
  //
  FS_MEMSET(pDiskInfo, 0, sizeof(FS_NAND_DISK_INFO));
  pDiskInfo->NumPhyBlocks        = NumBlocks;
  pDiskInfo->NumLogBlocks        = pInst->NumLogBlocks;
  pDiskInfo->NumPagesPerBlock    = 1uL << pInst->PPB_Shift;
  pDiskInfo->NumSectorsPerBlock  = (1uL << pInst->PPB_Shift) - 1u;   // -1 since the first sector in the block is used only for management data.
  pDiskInfo->BytesPerPage        = pInst->BytesPerPage;
  pDiskInfo->BytesPerSpareArea   = pInst->BytesPerSpareArea;
  pDiskInfo->BytesPerSector      = pInst->BytesPerPage;
  pDiskInfo->NumUsedPhyBlocks    = NumUsedPhyBlocks;
  pDiskInfo->NumBadPhyBlocks     = NumBadPhyBlocks;
  pDiskInfo->EraseCntMax         = EraseCntMax;
  pDiskInfo->EraseCntMin         = EraseCntMin;
  pDiskInfo->EraseCntAvg         = EraseCntAvg;
  pDiskInfo->IsWriteProtected    = pInst->IsWriteProtected;
  pDiskInfo->HasFatalError       = pInst->HasFatalError;
  pDiskInfo->ErrorSectorIndex    = pInst->ErrorSectorIndex;
  pDiskInfo->ErrorType           = pInst->ErrorType;
  pDiskInfo->BlocksPerGroup      = (U16)(1uL << _GetBPG_Shift(pInst));
  pDiskInfo->NumWorkBlocks       = pInst->NumWorkBlocks;
  pDiskInfo->BadBlockMarkingType = pInst->BadBlockMarkingType;
  pDiskInfo->IsFormatted         = (U8)IsFormatted;
  return r;
}

/*********************************************************************
*
*       FS_NAND_UNI_GetBlockInfo
*
*   Function description
*     Returns information about the specified NAND block.
*
*  Parameters
*    Unit          Index of the driver instance (0-based).
*    BlockIndex    Index of the physical block to get information about.
*    pBlockInfo    [OUT] Information about the NAND block.
*
*  Return value
*    ==0 - OK, information returned.
*    !=0 - An error occurred.
*
*  Additional information
*    This function is not required for the functionality of the driver
*    and will typically not be linked in production builds.
*
*    FS_NAND_UNI_GetBlockInfo() has to read the contents of the entire
*    NAND block in order to collect all the information which may take
*    a relatively long time to complete. If the application does not
*    require the information about the block status or about the
*    status of the logical sectors stored in the NAND block it can
*    call FS_NAND_UNI_GetBlockInfoEx() instead.
*/
int FS_NAND_UNI_GetBlockInfo(U8 Unit, U32 BlockIndex, FS_NAND_BLOCK_INFO * pBlockInfo) {
  NAND_UNI_INST * pInst;
  int             r;
  unsigned        Flags;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;       // Error, could not allocate driver instance.
  }
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return 1;       // Error, could not initialize the NAND flash.
  }
  r = _LowLevelMountIfRequired(pInst);
  if (r != 0) {
    return 1;       // Error, could not mount the NAND flash.
  }
  if (pBlockInfo == NULL) {
    return 1;       // Error, invalid parameter.
  }
  Flags = 0u
        | FS_NAND_BLOCK_INFO_FLAG_BAD_STATUS
        | FS_NAND_BLOCK_INFO_FLAG_SECTOR_STATUS
        ;
  r = _GetBlockInfo(pInst, BlockIndex, pBlockInfo, Flags);
  return r;
}

/*********************************************************************
*
*       FS_NAND_UNI_GetBlockInfoEx
*
*   Function description
*     Returns information about the specified NAND block.
*
*  Parameters
*    Unit          Index of the driver instance (0-based).
*    BlockIndex    Index of the physical block to get information about.
*    pBlockInfo    [OUT] Information about the NAND block.
*    Flags         Specifies the information to be returned.
*
*  Return value
*    ==0 - OK, information returned.
*    !=0 - An error occurred.
*
*  Additional information
*    This function is not required for the functionality of the driver
*    and will typically not be linked in production builds.
*
*    Flags is a bitwise OR-combination of \ref{Block info flags}.
*
*    The FS_NAND_BLOCK_INFO_FLAG_BAD_STATUS flag specifies if the
*    information about the block status has to be returned via
*    the members IsDriverBadBlock, BadBlockErrorType, BadBlockErrorBRSI
*    of FS_NAND_BLOCK_INFO. If FS_NAND_UNI_GetBlockInfoEx() is called
*    on a NAND block marked as defective with the FS_NAND_BLOCK_INFO_FLAG_BAD_STATUS
*    flag cleared than the member Type of FS_NAND_BLOCK_INFO is set
*    to NAND_BLOCK_TYPE_UNKNOWN.
*
*    The FS_NAND_BLOCK_INFO_FLAG_SECTOR_STATUS flag specifies if
*    FS_NAND_UNI_GetBlockInfoEx() has to return information about
*    the logical sectors stored in the NAND block. This information
*    is returned via the members NumSectorsBlank, NumSectorsValid,
*    NumSectorsInvalid, NumSectorsECCError, NumSectorsECCCorrectable,
*    NumSectorsErrorInECC of FS_NAND_BLOCK_INFO
*/
int FS_NAND_UNI_GetBlockInfoEx(U8 Unit, U32 BlockIndex, FS_NAND_BLOCK_INFO * pBlockInfo, unsigned Flags) {
  NAND_UNI_INST * pInst;
  int             r;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;       // Error, could not allocate driver instance.
  }
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return 1;       // Error, could not initialize the NAND flash.
  }
  r = _LowLevelMountIfRequired(pInst);
  if (r != 0) {
    return 1;       // Error, could not mount the NAND flash.
  }
  if (pBlockInfo == NULL) {
    return 1;       // Error, invalid parameter.
  }
  r = _GetBlockInfo(pInst, BlockIndex, pBlockInfo, Flags);
  return r;
}

/*********************************************************************
*
*       FS_NAND_UNI_SetOnFatalErrorCallback
*
*  Function description
*    Registers a function to be called by the driver when a fatal error occurs.
*
*  Parameters
*    pfOnFatalError   Function to be called when a fatal error occurs.
*
*  Additional information
*    This function is optional. The application can use this function
*    to register a routine to be called by the file system when a fatal
*    error occurs. Typically, a fatal error occurs when the ECC is not able
*    to correct all the bit errors in a page and is an indication that some
*    data was lost. A data loss leads in most of the cases to a damage of
*    the file system structure.
*/
void FS_NAND_UNI_SetOnFatalErrorCallback(FS_NAND_ON_FATAL_ERROR_CALLBACK * pfOnFatalError) {
  _pfOnFatalError = pfOnFatalError;
}

/*********************************************************************
*
*       FS_NAND_UNI_TestBlock
*
*  Function description
*    Fills all the pages in a block (including the spare area) with the
*    specified pattern and verifies if the data was written correctly.
*
*  Parameters
*    Unit         Index of the driver instance (0-based).
*    BlockIndex   Index of the NAND block to be tested.
*    Pattern      Data pattern to be written during the test.
*    pInfo        Additional parameters and information about the test.
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
int FS_NAND_UNI_TestBlock(U8 Unit, unsigned BlockIndex, U32 Pattern, FS_NAND_TEST_INFO * pInfo) {
  int             r;
  U32             Data32;
  unsigned        NumLoops;
  unsigned        BitErrorCnt;
  unsigned        BitErrorCntPage;
  unsigned        NumBits;
  U32           * pData32;
  U32           * pSpare32;
  unsigned        PageIndex;
  unsigned        PageIndex0;
  unsigned        NumPages;
  unsigned        BytesPerPage;
  unsigned        BytesPerSpare;
  unsigned        NumReadRetries;
  unsigned        NumBitsCorrectable;
  unsigned        NumBlocksECC;
  unsigned        BytesPerSparePart;
  NAND_UNI_INST * pInst;
  unsigned        Off;
  unsigned        OffSpareECCProt;
  unsigned        NumBytesSpareECCProt;
  unsigned        NumBitsNotECCProt;
  unsigned        NumBitsInWord;
  unsigned        ldBytesPerECCBlock;

  if (pInfo == NULL) {
    return FS_NAND_TEST_RETVAL_INTERNAL_ERROR;            // Error, invalid parameter.
  }
  FS_DEBUG_ASSERT(FS_MTYPE_DRIVER, (pInfo->OffSpareECCProt & ~3u) == 0u);       // The byte offset in the spare area must be a multiple of 4.
  FS_DEBUG_ASSERT(FS_MTYPE_DRIVER, (pInfo->NumBytesSpareECCProt & ~3u) == 0u);  // The number of bytes in the spare area must be a multiple of 4.
  BitErrorCnt = 0;
  PageIndex   = 0;
  //
  // Allocate memory for the driver instance if necessary.
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
  if (BlockIndex > pInst->NumBlocks) {
    return FS_NAND_TEST_RETVAL_INTERNAL_ERROR;            // Error, invalid block index.
  }
  PageIndex0 = BlockIndex << pInst->PPB_Shift;            // Calc. the index of the first page in the block.
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
    (void)_MarkBlockAsBad(pInst, BlockIndex, RESULT_ERASE_ERROR, 0);
    r = FS_NAND_TEST_RETVAL_ERASE_FAILURE;                // OK, erase failed and the block has been marked as defective.
    goto Done;
  }
  //
  // Fill local variables.
  //
  BytesPerPage         = pInst->BytesPerPage;
  BytesPerSpare        = pInfo->BytesPerSpare;
  if (BytesPerSpare == 0u) {
    BytesPerSpare      = BytesPerPage >> 5;               // Typ. the spare area is 1/32 of a page
  }
  BitErrorCnt          = 0;
  NumBitsCorrectable   = pInfo->NumBitsCorrectable;
  OffSpareECCProt      = pInfo->OffSpareECCProt;
  NumBytesSpareECCProt = pInfo->NumBytesSpareECCProt;
  //
  // Fill the internal buffers with the pattern.
  //
  NumLoops = BytesPerPage >> 2;                           // The pattern in 4 bytes large.
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
  PageIndex = PageIndex0;
  NumPages  = 1uL << pInst->PPB_Shift;
  //
  // Fill the page and the spare area.
  //
  do {
    //
    // Write the page and the spare area. The ECC is disabled since we test the entire spare area.
    //
    (void)_DisableHW_ECCIfRequired(pInst);
    r = _WriteDataSpare(pInst, PageIndex, _pSectorBuffer, BytesPerPage, _pSpareAreaData, BytesPerSpare);
    (void)_EnableHW_ECCIfRequired(pInst);
    if (r != 0) {
      (void)_EraseBlock(pInst, BlockIndex);
      (void)_MarkBlockAsBad(pInst, BlockIndex, RESULT_UNCORRECTABLE_BIT_ERRORS, PageIndex);
      r = FS_NAND_TEST_RETVAL_WRITE_FAILURE;
      goto Done;
    }
    ++PageIndex;
  } while (--NumPages != 0u);
  //
  // Read back and verify the written data.
  //
  PageIndex       = PageIndex0;
  NumPages        = 1uL << pInst->PPB_Shift;
  NumReadRetries  = FS_NAND_NUM_READ_RETRIES;
  BitErrorCntPage = 0;
  //
  // Loop over all pages in the block.
  //
  for (;;) {
    //
    // Read the page and the spare area.
    //
    for (;;) {
      (void)_DisableHW_ECCIfRequired(pInst);
      r = _ReadDataSpare(pInst, PageIndex, _pSectorBuffer, BytesPerPage, _pSpareAreaData, BytesPerSpare);
      (void)_EnableHW_ECCIfRequired(pInst);
      if (r == 0) {
        break;
      }
      if (NumReadRetries != 0u) {
        --NumReadRetries;
        continue;
      }
      r = FS_NAND_TEST_RETVAL_READ_FAILURE;
      goto Done;
    }
    //
    // Count the number of bit errors on each ECC block.
    // The ECC block includes a part of the data and of the spare area.
    //
    pData32            = _pSectorBuffer;
    pSpare32           = SEGGER_PTR2PTR(U32, _pSpareAreaData);
    ldBytesPerECCBlock = pInst->ldBytesPerECCBlock;
    NumBlocksECC       = BytesPerPage >> ldBytesPerECCBlock;
    BytesPerSparePart  = BytesPerSpare / NumBlocksECC;
    do {
      //
      // Count the number of bit errors in the data area of the ECC block.
      //
      NumBits           = 0;
      NumBitsNotECCProt = 0;
      NumLoops          = 1uL << (ldBytesPerECCBlock - 2u);   // -2 since the pattern is 4 bytes large.
      do {
        Data32   = *pData32++ ^ Pattern;
        NumBits += _Count1Bits(Data32);
      } while (--NumLoops != 0u);
      //
      // Count the number of bits in the spare are of the ECC block.
      // For some NAND flash devices the HW ECC covers only a part of the spare area.
      // Take this into account here.
      //
      NumLoops = BytesPerSparePart >> 2;                      // 2 since the pattern is 4 bytes large.
      Off      = 0;
      do {
        Data32        = *pSpare32++ ^ Pattern;
        NumBitsInWord = _Count1Bits(Data32);
        if (NumBytesSpareECCProt != 0u) {
          if ((Off >= OffSpareECCProt) && (Off < (OffSpareECCProt + NumBytesSpareECCProt))) {
            NumBits += NumBitsInWord;
          } else {
            NumBitsNotECCProt += NumBitsInWord;
          }
          Off += 4u;                                          // 4 since we process 4 bytes at a time.
        }
      } while (--NumLoops != 0u);
      //
      // Check if the number of bit errors can be corrected by ECC.
      //
      if ((NumBits > NumBitsCorrectable) ||
          (NumBitsNotECCProt != 0u)) {                        // No bit errors are allowed in the areas not protected by ECC.
        if (NumReadRetries != 0u) {
          --NumReadRetries;
          goto Retry;                                         // This could be a transient error. Read again the data.
        }
        BitErrorCntPage += NumBits + NumBitsNotECCProt;
        (void)_EraseBlock(pInst, BlockIndex);
        (void)_MarkBlockAsBad(pInst, BlockIndex, RESULT_UNCORRECTABLE_BIT_ERRORS, PageIndex);
        BitErrorCnt += BitErrorCntPage;
        r = FS_NAND_TEST_RETVAL_FATAL_ERROR;                  // Uncorrectable bit error detected.
        goto Done;
      }
      BitErrorCntPage += NumBits;
    } while (--NumBlocksECC != 0u);
    if (--NumPages == 0u) {
      break;
    }
    ++PageIndex;
    NumReadRetries  = FS_NAND_NUM_READ_RETRIES;
Retry:
    BitErrorCntPage = 0;
  }
  if (BitErrorCnt != 0u) {
    r = FS_NAND_TEST_RETVAL_CORRECTABLE_ERROR;
  }
Done:
  if (r == FS_NAND_TEST_RETVAL_OK) {
    PageIndex = PageIndex0;
  }
  //
  // Leave the contents of the block in a known state.
  //
  if ((r != FS_NAND_TEST_RETVAL_BAD_BLOCK    ) &&
      (r != FS_NAND_TEST_RETVAL_ERASE_FAILURE) &&
      (r != FS_NAND_TEST_RETVAL_FATAL_ERROR  )) {
    (void)_EraseBlock(pInst, BlockIndex);
  }
  //
  // Return additional information.
  //
  pInfo->BitErrorCnt = BitErrorCnt;
  pInfo->PageIndex   = PageIndex;
  return r;
}

/*********************************************************************
*
*       FS_NAND_UNI_IsBlockBad
*
*  Function description
*    Checks if a NAND block is marked as defective.
*
*  Parameters
*    Unit           Index of the driver instance (0-based).
*    BlockIndex     Index of the NAND flash block to be checked.
*
*  Return value
*    ==1 - Block is defective
*    ==0 - Block is not defective
*
*  Additional information
*    This function is optional.
*/
int FS_NAND_UNI_IsBlockBad(U8 Unit, unsigned BlockIndex) {
  int             r;
  NAND_UNI_INST * pInst;
  int             IsBad;

  IsBad = 1;          // Set to indicate that the block is defective.
  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;         // Error, could not allocate driver instance.
  }
  r = _InitIfRequired(pInst);
  if (r == 0) {
    r = _IsBlockErasable(pInst, BlockIndex);
    if (r != 0) {
      IsBad = 0;
    }
  }
  return IsBad;
}

/*********************************************************************
*
*       FS_NAND_UNI_EraseBlock
*
*  Function description
*    Sets all the bytes in a NAND block to 0xFF.
*
*  Parameters
*    Unit           Index of the driver instance (0-based).
*    BlockIndex     Index of the NAND flash block to be erased.
*
*  Return value
*    ==0 - OK, block erased
*    !=0 - An error occurred
*
*  Additional information
*    This function is optional. FS_NAND_UNI_EraseBlock() function does
*    not check if the block is marked as defective before erasing it.
*/
int FS_NAND_UNI_EraseBlock(U8 Unit, unsigned BlockIndex) {
  int             r;
  NAND_UNI_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;                     // Error, could not allocate driver instance.
  }
  r = _InitIfRequired(pInst);
  if (r == 0) {
    r = _EraseBlock(pInst, BlockIndex);
  }
  return r;
}

/*********************************************************************
*
*       FS_NAND_UNI_WritePage
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
int FS_NAND_UNI_WritePage(U8 Unit, U32 PageIndex, const void * pData, unsigned NumBytes) {
  int             r;
  NAND_UNI_INST * pInst;
  unsigned        BytesPerPage;
  unsigned        NumBytesAtOnce;
  unsigned        BytesPerSpareArea;
  U32             NumPages;
  unsigned        PPB_Shift;    // Pages per block
  const U8      * pSpare;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;         // Error, could not allocate driver instance.
  }
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return 1;         // Error, could not initialize device.
  }
  BytesPerPage      = pInst->BytesPerPage;
  PPB_Shift         = pInst->PPB_Shift;
  BytesPerSpareArea = pInst->BytesPerSpareArea;
  NumPages          = pInst->NumBlocks << PPB_Shift;
  NumBytes          = SEGGER_MIN(NumBytes, BytesPerPage + BytesPerSpareArea);
  if (PageIndex >= NumPages) {
    return 1;         // Error, invalid page index.
  }
  //
  // Copy the data to internal sector buffer and write it from there.
  // There are 2 reasons why we are doing this:
  // 1) The _WriteSectorWithECC() function requires a 32-bit aligned buffer.
  //    This is necessary so that the SW ECC routine (if used) can process 32-bits at a time.
  // 2) The _WriteSectorWithECC() function can write only entire sectors.
  //
  FS_MEMSET(_pSectorBuffer, 0xFF, BytesPerPage);
  NumBytesAtOnce = SEGGER_MIN(NumBytes, BytesPerPage);
  FS_MEMCPY(_pSectorBuffer, pData, NumBytesAtOnce);
  NumBytes -= NumBytesAtOnce;
  pSpare    = SEGGER_CONSTPTR2PTR(const U8, pData);
  pSpare   += NumBytesAtOnce;
  //
  // Copy the data in the spare if present.
  //
  _ClearStaticSpareArea(pInst);
  if (NumBytes > 0u) {
    FS_MEMCPY(_pSpareAreaData, pSpare, NumBytes);
  }
  //
  // The Universal NAND driver uses a sector size equal to page size.
  // The page index is the same as the sector index.
  //
  r = _WriteSectorWithECC(pInst, _pSectorBuffer, PageIndex);
  return r;
}

/*********************************************************************
*
*       FS_NAND_UNI_WritePageRaw
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
*    typ. 2 Kbytes + 64 bytes, the excess bytes are ignored.
*
*    FS_NAND_UNI_WritePageRaw() does not work correctly on NAND flash
*    devices with HW ECC that cannot be disabled.
*/
int FS_NAND_UNI_WritePageRaw(U8 Unit, U32 PageIndex, const void * pData, unsigned NumBytes) {
  int             r;
  NAND_UNI_INST * pInst;
  unsigned        BytesPerPage;
  U32             NumPages;
  unsigned        PPB_Shift;    // Pages per block
  unsigned        NumBytesData;
  unsigned        NumBytesSpare;
  unsigned        BytesPerSpareArea;
  const U8      * pSpare;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;         // Error, could not allocate driver instance.
  }
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return 1;         // Error, could not initialize device.
  }
  pSpare             = NULL;
  NumBytesSpare      = 0;
  BytesPerPage       = pInst->BytesPerPage;
  BytesPerSpareArea  = pInst->BytesPerSpareArea;
  PPB_Shift          = pInst->PPB_Shift;
  NumPages           = pInst->NumBlocks << PPB_Shift;
  if (PageIndex >= NumPages) {
    return 1;         // Error, invalid page index.
  }
  NumBytesData     = SEGGER_MIN(NumBytes, BytesPerPage);
  NumBytes        -= NumBytesData;
  if (NumBytes != 0u) {
    NumBytesSpare  = SEGGER_MIN(NumBytes, BytesPerSpareArea);
    pSpare         = SEGGER_CONSTPTR2PTR(const U8, pData) + NumBytesData;
  }
  //
  // Write the data without ECC. Disable the HW ECC here and enable it again
  // after the read operation.
  //
  (void)_DisableHW_ECCIfRequired(pInst);
  //
  // In addition, the physical layer is put in the raw mode in which the data
  // is written to the page exactly as passed to _WriteDataSpare(). The normal
  // operation mode of the physical layer is restored after the write operation
  // completes.
  //
  (void)_EnterRawMode(pInst);
  //
  // The Universal NAND driver uses a sector size equal to page size.
  // The page index is the same as the sector index.
  //
  r = _WriteDataSpare(pInst, PageIndex, pData, NumBytesData, pSpare, NumBytesSpare);
  (void)_LeaveRawMode(pInst);
  (void)_EnableHW_ECCIfRequired(pInst);
  return r;
}

/*********************************************************************
*
*       FS_NAND_UNI_ReadPageRaw
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
*
*    FS_NAND_UNI_ReadPageRaw() does not work correctly on NAND flash
*    devices with HW ECC that cannot be disabled.
*/
int FS_NAND_UNI_ReadPageRaw(U8 Unit, U32 PageIndex, void * pData, unsigned NumBytes) {
  int             r;
  NAND_UNI_INST * pInst;
  unsigned        BytesPerPage;
  unsigned        NumBytesData;
  U32             NumPages;
  unsigned        NumBytesSpare;
  unsigned        PPB_Shift;    // Pages per block
  unsigned        BytesPerSpareArea;
  U8            * pSpare;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;         // Error, could not allocate driver instance.
  }
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return 1;         // Error, could not initialize device.
  }
  pSpare             = NULL;
  NumBytesSpare      = 0;
  BytesPerPage       = pInst->BytesPerPage;
  BytesPerSpareArea  = pInst->BytesPerSpareArea;
  PPB_Shift          = pInst->PPB_Shift;
  NumPages           = pInst->NumBlocks << PPB_Shift;
  if (PageIndex >= NumPages) {
    return 1;         // Error, invalid page index.
  }
  NumBytesData     = SEGGER_MIN(NumBytes, BytesPerPage);
  NumBytes        -= NumBytesData;
  if (NumBytes != 0u) {
    NumBytesSpare  = SEGGER_MIN(NumBytes, BytesPerSpareArea);
    pSpare         = SEGGER_PTR2PTR(U8, pData) + NumBytesData;
  }
  //
  // Read the data without ECC. Disable the HW ECC here and enable it again
  // after the read operation.
  //
  (void)_DisableHW_ECCIfRequired(pInst);
  //
  // In addition, the physical layer is put in the raw mode in which the data
  // read from the page via _ReaDataSpare() has the same layout as on the physical
  // storage. The normal operation mode of the physical layer is restored after
  // the read operation completes.
  //
  (void)_EnterRawMode(pInst);
  //
  // The Universal NAND driver uses a sector size equal to page size.
  // The page index is the same as the sector index.
  //
  r = _ReadDataSpare(pInst, PageIndex, pData, NumBytesData, pSpare, NumBytesSpare);
  (void)_LeaveRawMode(pInst);
  (void)_EnableHW_ECCIfRequired(pInst);
  return r;
}

#if FS_NAND_VERIFY_ERASE

/*********************************************************************
*
*       FS_NAND_UNI_SetEraseVerification
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
*    NAND flash device in a internal status register. FS_NAND_UNI_SetEraseVerification()
*    can be used to enable additional verification of the block erase
*    operation that is realized by reading back the contents of the entire
*    erased physical block and by checking that all the bytes in it are
*    set to 0xFF. Enabling this feature can negatively impact the write
*    performance of Universal NAND driver.
*
*    The block erase verification feature is active only when the Universal
*    NAND driver is compiled with the FS_NAND_VERIFY_ERASE configuration
*    define is set to 1 (default is 0) or when the FS_DEBUG_LEVEL
*    configuration define is set to a value greater than or equal to
*    FS_DEBUG_LEVEL_CHECK_ALL.
*/
void FS_NAND_UNI_SetEraseVerification(U8 Unit, U8 OnOff) {
  NAND_UNI_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->VerifyErase = OnOff;
  }
}

#endif // FS_NAND_VERIFY_ERASE

#if FS_NAND_VERIFY_WRITE

/*********************************************************************
*
*       FS_NAND_UNI_SetWriteVerification
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
*    NAND flash device in a internal status register. FS_NAND_UNI_SetWriteVerification()
*    can be used to enable additional verification of the page write
*    operation that is realized by reading back the contents of the written
*    page and by checking that all the bytes are matching the data
*    requested to be written. Enabling this feature can negatively
*    impact the write performance of Universal NAND driver.
*
*    The page write verification feature is active only when the Universal
*    NAND driver is compiled with the FS_NAND_VERIFY_WRITE configuration
*    define is set to 1 (default is 0) or when the FS_DEBUG_LEVEL
*    configuration define is set to a value greater than or equal to
*    FS_DEBUG_LEVEL_CHECK_ALL.
*/
void FS_NAND_UNI_SetWriteVerification(U8 Unit, U8 OnOff) {
  NAND_UNI_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->VerifyWrite = OnOff;
  }
}

#endif // FS_NAND_VERIFY_WRITE

/*********************************************************************
*
*       FS_NAND_UNI_ReadLogSectorPartial
*
*  Function description
*    Reads a specified number of bytes from a logical sector.
*
*  Parameters
*    Unit             Index of the driver instance (0-based).
*    LogSectorIndex   Index of the logical sector to read from.
*    pData            [OUT] Data read from NAND flash.
*    Off              Byte offset to read from (relative to beginning of the sector).
*    NumBytes         Number of bytes to be read.
*
*  Return value
*    !=0 - Number of bytes read.
*    ==0 - An error occurred.
*
*  Additional information
*    This function is optional.
*
*    For NAND flash devices with internal HW ECC only the specified
*    number of bytes is transferred and not the entire sector.
*    Typ. used by the applications that access the NAND flash
*    directly (that is without a file system) to increase
*    the read performance.
*/
int FS_NAND_UNI_ReadLogSectorPartial(U8 Unit, U32 LogSectorIndex, void * pData, unsigned Off, unsigned NumBytes) {
  NAND_UNI_INST * pInst;
  int             r;

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
  if ((Off >= pInst->BytesPerPage) || ((Off + NumBytes) > pInst->BytesPerPage)) {
    return 1;         // Error, invalid data block range.
  }
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return 1;         // Error, could not initialize device.
  }
  r = _LowLevelMountIfRequired(pInst);
  if (r != 0) {
    return 1;         // Error, could not mount the NAND flash.
  }
  r = _ReadOneSectorEx(pInst, LogSectorIndex, SEGGER_PTR2PTR(U8, pData), Off, NumBytes);
  if (r != 0) {
    return 1;         // Error, could not read sector data.
  }
  return 0;           // OK, sector data read.
}

/*********************************************************************
*
*       FS_NAND_UNI_SetBlockReserve
*
*  Function description
*    Configures the number of NAND flash blocks to be reserved
*    as replacement for the NAND flash blocks that become defective.
*
*  Parameters
*    Unit             Index of the driver instance (0-based).
*    pctOfBlocks      Number of NAND flash blocks to be reserved
*                     as percentage of the total number of NAND flash blocks.
*
*  Additional information
*    This function is optional. The Universal NAND driver reserves
*    by default about 3% of the total number of NAND flash blocks
*    which is sufficient for most applications. Reserving more NAND flash
*    blocks can increase the lifetime of the NAND flash device.
*    The NAND flash device has to be low-level formatted after
*    changing the number of reserved blocks.
*/
void FS_NAND_UNI_SetBlockReserve(U8 Unit, unsigned pctOfBlocks) {
  NAND_UNI_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    if (pctOfBlocks > MAX_PCT_OF_BLOCKS_RESERVED) {
      pctOfBlocks = MAX_PCT_OF_BLOCKS_RESERVED;
    }
    pInst->pctOfBlocksReserved = (U8)pctOfBlocks;
  }
}

#if FS_NAND_RECLAIM_DRIVER_BAD_BLOCKS

/*********************************************************************
*
*       FS_NAND_UNI_SetDriverBadBlockReclamation
*
*  Function description
*    Configures if the bad blocks marked as defective by the driver
*    have to be erased at low-level format or not.
*
*  Parameters
*    Unit         Index of the driver instance (0-based).
*    OnOff        Configures if the feature has to be enable or disabled.
*                 * ==1   Defective blocks marked as such by the driver are erased at low-level format.
*                 * ==0   Defective blocks marked as such by the driver are not erased at low-level format.
*
*  Additional information
*    This function is active only when the option
*    FS_NAND_RECLAIM_DRIVER_BAD_BLOCKS is set to 1.
*    The default behavior is to erase the blocks marked as
*    defective by the driver.
*/
void FS_NAND_UNI_SetDriverBadBlockReclamation(U8 Unit, U8 OnOff) {
  NAND_UNI_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->ReclaimDriverBadBlocks = OnOff;
  }
}

#endif // FS_NAND_RECLAIM_DRIVER_BAD_BLOCKS

/*********************************************************************
*
*       FS_NAND_UNI_Mount
*
*  Function description
*    Mounts the NAND flash device.
*
*  Parameters
*    Unit         Index of the driver instance (0-based).
*    pMountInfo   [OUT] Information about the mounted NAND flash device. Can be set to NULL.
*
*  Return value
*    ==0    OK, NAND flash device successfully mounted.
*    !=0    Error, could not mount NAND flash device.
*
*  Additional information
*    FS_NAND_UNI_Mount() can be used to explicitly mount the NAND flash
*    device and to get information about it. This function returns a subset
*    of the information returned by FS_NAND_UNI_GetDiskInfo() and therefore
*    can be used instead of it if the application does not require statistical
*    information about the usage of the NAND blocks. Typically, FS_NAND_UNI_Mount()
*    requires less time to complete than FS_NAND_UNI_GetDiskInfo().
*
*    This function is not required for the functionality of the
*    Universal NAND driver and is typically not linked in production
*    builds.
*/
int FS_NAND_UNI_Mount(U8 Unit, FS_NAND_MOUNT_INFO * pMountInfo) {
  NAND_UNI_INST * pInst;
  int             r;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;         // Error, could not allocate driver instance.
  }
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return 1;         // Error, device initialization failed.
  }
  //
  // Some of the information can be collected only when the NAND flash is low-level mounted.
  //
  r = _LowLevelMountIfRequired(pInst);
  if (r != 0) {
    return 1;         // Error, could not mount NAND flash device.
  }
  //
  // Return information about the mounted NAND flash device to the caller.
  //
  if (pMountInfo != NULL) {
    FS_MEMSET(pMountInfo, 0, sizeof(FS_NAND_MOUNT_INFO));
    pMountInfo->NumPhyBlocks        = pInst->NumBlocks;
    pMountInfo->NumLogBlocks        = pInst->NumLogBlocks;
    pMountInfo->NumPagesPerBlock    = 1uL << pInst->PPB_Shift;
    pMountInfo->NumSectorsPerBlock  = (1uL << pInst->PPB_Shift) - 1u;   // -1 because the first sector in the block is used only for management data.
    pMountInfo->BytesPerPage        = pInst->BytesPerPage;
    pMountInfo->BytesPerSpareArea   = pInst->BytesPerSpareArea;
    pMountInfo->BytesPerSector      = pInst->BytesPerPage;
    pMountInfo->IsWriteProtected    = pInst->IsWriteProtected;
    pMountInfo->HasFatalError       = pInst->HasFatalError;
    pMountInfo->ErrorSectorIndex    = pInst->ErrorSectorIndex;
    pMountInfo->ErrorType           = pInst->ErrorType;
    pMountInfo->BlocksPerGroup      = (U16)(1uL << _GetBPG_Shift(pInst));
    pMountInfo->NumWorkBlocks       = pInst->NumWorkBlocks;
    pMountInfo->BadBlockMarkingType = pInst->BadBlockMarkingType;
  }
  return 0;           // OK, NAND flash device successfully mounted.
}

#if (FS_NAND_MAX_PAGE_SIZE != 0)

/*********************************************************************
*
*       FS_NAND_UNI_SetMaxPageSize
*
*  Function description
*    Configures the maximum handled page size.
*
*  Parameters
*    NumBytes     NAND page size in bytes.
*
*  Additional information
*    This function can be used at runtime to configure the size of
*    the internal buffer used by the Universal NAND driver to read
*    the data from a NAND page. This buffer is shared by all instances
*    of the Universal NAND driver. If the application does not call
*    FS_NAND_UNI_SetMaxPageSize() then the size of the internal page
*    buffer is set to FS_NAND_MAX_PAGE_SIZE if FS_NAND_MAX_PAGE_SIZE
*    is different than 0 or to the logical sector size of the file
*    system if FS_NAND_MAX_PAGE_SIZE is set to 0.
*
*    FS_NAND_UNI_SetMaxPageSize() is for example useful when
*    the application is using two NAND flash devices with different
*    page sizes. In this case, the application has to call
*    FS_NAND_UNI_SetMaxPageSize() with the NumBytes parameter set to
*    the largest of the two page sizes.
*
*    FS_NAND_UNI_SetMaxPageSize() is available only if the file system
*    is built with FS_NAND_MAX_PAGE_SIZE set to a value different
*    than 0. The function has to be called in FS_X_AddDevices()
*    before any instance of the Universal NAND driver is created.
*
*    NumBytes has to be a power of 2 value.
*/
void FS_NAND_UNI_SetMaxPageSize(unsigned NumBytes) {
  _ldMaxPageSize = (U8)_ld(NumBytes);
}

#endif // FS_NAND_MAX_PAGE_SIZE != 0

#if (FS_NAND_MAX_SPARE_AREA_SIZE != 0)

/*********************************************************************
*
*       FS_NAND_UNI_SetMaxSpareAreaSize
*
*  Function description
*    Configures the maximum handled spare area size.
*
*  Parameters
*    NumBytes     Spare area size of the NAND page in bytes.
*
*  Additional information
*    This function can be used at runtime to configure the size of
*    the internal buffer used by the Universal NAND driver to read
*    the data from a spare area of a NAND page. This buffer is shared
*    by all instances of the Universal NAND driver. If the application
*    does not call FS_NAND_UNI_SetMaxSpareAreaSize() then the size of
*    the internal spare area buffer is set to FS_NAND_MAX_SPARE_AREA_SIZE
*    if FS_NAND_MAX_SPARE_AREA_SIZE is different than 0 or to 1/32 of
*    the logical sector size of the file system if FS_NAND_MAX_SPARE_AREA_SIZE
*    is set to 0.
*
*    FS_NAND_UNI_SetMaxSpareAreaSize() is for example useful when NAND
*    flash device has a spare area that is larger than 1/32 of the page size.
*    In this case, the application has to call FS_NAND_UNI_SetMaxSpareAreaSize()
*    with the NumBytes parameter set to the spare area size specified
*    in the data sheet of the NAND flash device.
*
*    FS_NAND_UNI_SetMaxSpareAreaSize() is available only if the file system
*    is built with FS_NAND_MAX_SPARE_AREA_SIZE set to a value different
*    than 0. The function has to be called in FS_X_AddDevices()
*    before any instance of the Universal NAND driver is created.
*/
void FS_NAND_UNI_SetMaxSpareAreaSize(unsigned NumBytes) {
  _MaxSpareAreaSize = (U16)NumBytes;
}

#endif // FS_NAND_MAX_SPARE_AREA_SIZE != 0

/*************************** End of file ****************************/
