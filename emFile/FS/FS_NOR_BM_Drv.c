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
File        : FS_NOR_BM_Drv.c
Purpose     : File system high level NOR flash driver with reduced RAM usage.
-------------------------- END-OF-HEADER -----------------------------

General info on the inner workings of this high level flash driver.

Layered approach
================
All read, write and erase operations are performed by the low level
flash driver. The low level flash driver is also responsible for
returning information about the organization of the NOR flash device.
This driver assumes the following:
- The flash is organized in physical sectors
- The phys. sectors are at least 1kb in size
- Erasing a phys. sector fills all bytes with FF
- Writing is permitted in arbitrary units (bytes)
- Writing can change bits from 1 to 0, even if the byte already
  had a value other than FF

Data management
===============
Data is stored in so called data blocks in the NOR flash. The assignment
information (which physical sector contains which data) is stored in the
sector header. Modifications of data are not done in the data blocks directly,
but using the concept of work blocks. A work block contains modifications of a data block.
The first physical sector is used to store format information and written only once.
All other physical sectors are used to store data. This means that in the driver,
a valid physical sector index is always > 0.

Reading data from the NOR flash
===============================
The following actions are performed when the data is read:
a) Is there a work block which contains this information ? If so, this is recent and used.
b) Is there a data block which contains this information ? If so, this is recent and used.
c) Otherwise, the sector has never been written. In this case, the driver delivers 0xFF data bytes.

Data integrity via CRC
======================
CRC can be used to ensure the integrity of the management and the user data.
This feature is optional and has to be explicitly enabled via the FS_NOR_SUPPORT_CRC configuration
define at compile time and then at runtime via FS_NOR_BM_EnableCRC().
The CRC of management data is typically calculated and stored each time the NOR driver writes
to a physical or logical sector header. There are some exceptions where data is stored to a header
without calculating the CRC for example when a logical sector is copied to an other location.
In this case the BRSI is stored to the header of the destination logical sector (without updating the CRC)
to indicate that the write operation is in progress. If the operation is interrupted by an unexpected reset,
the low-level mount operation can detect that the logical sector data is not valid by looking
a the header at the logical sector. If the CRC verification fails then the data of the logical sector is discarded.
The CRC is verified at each operation that reads data from the physical or logical sector header.
The check returns success if all the bytes in the header are set to 0xFF. In this case the CRC
verification is skipped.
The physical and logical sector headers contain more than one CRC member because the driver
modifies the data more than once (that is incrementally) between two physical sector erase operations.
This is true for NOR flash devices that are able to modify the same data more than once
while keeping the bits set to 0 (FS_NOR_CAN_REWRITE configuration define set to 1).
A CRC status member indicates in this case which CRC member contains the current value.
For NOR flash devices that do not support this feature (FS_NOR_CAN_REWRITE configuration define set to 0)
the CRC is updated only once between two physical sector erase operations when the driver writes
for the first time to the physical or logical sector header. The other write operations
performed by the driver to the same physical or logical header modify a byte value from 0xFF to 0x00
that is not covered by CRC but is checked each time the CRC is verified to be either 0xFF or 0x00.
Any other value is reported as a CRC verification error to the file system layer.
The driver uses an 8-bit CRC to protect the integrity of management data (physical and logical sector header)
while for the user data (logical sector) a 16-bit CRC is used.

Abbreviations
=============
LBI
    Logical Block Index: Index of a block of logical sectors assigned to physical sector.
BRSI
    Block Relative Sector Index: Index of a logical sector relative to start of logical block.
PSI
    Physical Sector Index: Gives the position of a NOR physical sector.
SRSI
    Sector Relative Sector Index: Index of logical sector relative to start of physical sector.
CRC
    Cyclic Redundancy Check: A type of check sum that is used to protect the data integrity.
ECC
    Error Control and Correction: A type of parity check that is used to detect and correct bit errors.

Potential improvements
======================
- Improving speed: keep the erase count of every physical sector in RAM (for systems with large amount of RAM)
  to speed up the block search operation.
- Improving speed: skip a work block conversion if all the logical sectors in that work block are in valid.
- Reducing RAM size: pFreeMap can be eliminated by searching on the storage for a free block each time one is needed.
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
#define LLFORMAT_VERSION              10001u

#if FS_NOR_ENABLE_STATS
  #define IF_STATS(Expr) Expr
#else
  #define IF_STATS(Expr)
#endif

/*********************************************************************
*
*       Special values for "INVALID"
*/
#define BRSI_INVALID                  0xFFFFu        // Invalid relative sector index
#define ERASE_CNT_INVALID             0xFFFFFFFFuL   // Invalid erase count
#define LBI_INVALID                   0xFFFFu
#define DATA_CNT_INVALID              0xFFu
#if FS_NOR_SUPPORT_FAIL_SAFE_ERASE
  #define FAIL_SAFE_ERASE_INVALID     0xFFu
#endif

/*********************************************************************
*
*       Status of data in a physical sector
*/
#define DATA_STAT_EMPTY               0xFFu     // Block is empty
#define DATA_STAT_WORK                0xFEu     // Block is used as "work block"
#define DATA_STAT_VALID               0xFCu     // Block contains valid data
#define DATA_STAT_INVALID             0x00u     // Block contains old, invalid data

/*********************************************************************
*
*       Format information
*/
#if (FS_NOR_LINE_SIZE > 16)
  #define INFO_NUM_BYTES_STRIPE       FS_NOR_LINE_SIZE
#else
  #define INFO_NUM_BYTES_STRIPE       16
#endif // (FS_NOR_LINE_SIZE > 16)

#define INFO_OFF_FORMAT_SIGNATURE     0
#define INFO_OFF_FORMAT_VERSION       INFO_NUM_BYTES_STRIPE
#define INFO_OFF_BYTES_PER_SECTOR     (INFO_NUM_BYTES_STRIPE * 2)
#define INFO_OFF_NUM_LOG_BLOCKS       (INFO_NUM_BYTES_STRIPE * 3)
#define INFO_OFF_NUM_WORK_BLOCKS      (INFO_NUM_BYTES_STRIPE * 4)
#define INFO_OFF_FAIL_SAFE_ERASE      (INFO_NUM_BYTES_STRIPE * 5)

/*********************************************************************
*
*       Fatal error information
*/
#define INFO_OFF_IS_WRITE_PROTECTED   0
#define INFO_OFF_HAS_FATAL_ERROR      INFO_NUM_BYTES_STRIPE
#define INFO_OFF_ERROR_TYPE           (INFO_NUM_BYTES_STRIPE * 2)
#define INFO_OFF_ERROR_PSI            (INFO_NUM_BYTES_STRIPE * 3)

/*********************************************************************
*
*       Status of NOR flash operations
*/
#define RESULT_NO_ERROR               0   // OK, no error occurred.
#define RESULT_WRITE_ERROR            1   // Error while writing to NOR flash
#define RESULT_ERASE_ERROR            2   // Error while erasing a physical sector of the NOR flash
#define RESULT_OUT_OF_FREE_SECTORS    3   // Tried to allocate a free physical sector but no more were found
#define RESULT_READ_ERROR             4   // Error while reading from NOR flash
#if FS_NOR_SUPPORT_CRC
  #define RESULT_CRC_ERROR            5   // Data integrity check failed
#endif
#if (FS_NOR_SUPPORT_CRC != 0) || (FS_NOR_SUPPORT_ECC != 0)
  #define RESULT_INCONSISTENT_DATA    6   // Data of physical or logical sector is inconsistent
#endif // FS_NOR_SUPPORT_CRC != 0 || FS_NOR_SUPPORT_ECC != 0
#define RESULT_OUT_OF_WORK_BLOCKS     7   // Tried to allocate a work block but no more were found
#if FS_NOR_SUPPORT_ECC
  #define RESULT_ECC_ERROR            8   // Uncorrectable bit error.
#endif // FS_NOR_SUPPORT_ECC

/*********************************************************************
*
*       Indices of special physical and logical sectors
*/
#define PSI_INFO_BLOCK                0u
#define PSI_FIRST_STORAGE_BLOCK       1u
#define SRSI_INFO_FORMAT              0u
#define SRSI_INFO_ERROR               1u

/*********************************************************************
*
*       Limits for the number of work blocks
*/
#if FS_SUPPORT_JOURNAL
  #define NUM_WORK_BLOCKS_MIN         4u  // For performance reasons we need more work blocks when Journaling is enabled
#else
  #define NUM_WORK_BLOCKS_MIN         3u
#endif
#define NUM_WORK_BLOCKS_MAX           10u

/*********************************************************************
*
*       Phy. sector erase status
*/
#if FS_NOR_SUPPORT_FAIL_SAFE_ERASE
  #define ERASE_SIGNATURE_VALID       0x45525344uL    // "ERSD"
  #define ERASE_SIGNATURE_INVALID     0uL
#endif // FS_NOR_SUPPORT_FAIL_SAFE_ERASE

/*********************************************************************
*
*       FAIL_SAFE_ERASE_NO_REWRITE
*
*  Description
*    Enables the support for a fail-safe erase operation for the
*    NOR flash devices that cannot perform incremental write operations.
*    The fail-safe erase operation has to be enabled at runtime via
*    the FS_NOR_BM_SetFailSafeErase() because by default it is disabled
*    in order to maintain the compatibility with older emFile versions.
*/
#if (FS_NOR_LINE_SIZE < 5)
  #define FAIL_SAFE_ERASE_NO_REWRITE  0
#elif FS_NOR_CAN_REWRITE
  #define FAIL_SAFE_ERASE_NO_REWRITE  0
#elif FS_NOR_SUPPORT_ECC
  #define FAIL_SAFE_ERASE_NO_REWRITE  1
#elif (FS_NOR_SUPPORT_FAIL_SAFE_ERASE != 0) && (FS_NOR_SUPPORT_CLEAN != 0)
  #define FAIL_SAFE_ERASE_NO_REWRITE  1
#else
  #define FAIL_SAFE_ERASE_NO_REWRITE  0
#endif

/*********************************************************************
*
*       CRC related defines
*/
#if FS_NOR_SUPPORT_CRC
  #define CRC_DRIVER_DATA_INIT        0x00
  #define CRC_SECTOR_DATA_INIT        0x0000
  #define CRC_STAT_INVALID            0xFFu
  #define CRC_STAT_VALID0             0xFEu
  #define CRC_STAT_VALID1             0xFCu
  #define CRC_STAT_VALID2             0xF8u
#endif // FS_NOR_SUPPORT_CRC
#if FAIL_SAFE_ERASE_NO_REWRITE
  #define CRC_DRIVER_DATA_INVALID     0xFFu
#endif // FAIL_SAFE_ERASE_NO_REWRITE

/*********************************************************************
*
*       ECC related defines
*/
#if FS_NOR_SUPPORT_ECC
  #define ECC_STAT_EMPTY              0xFFu
  #define ECC_STAT_VALID              0xF0u
  #define ECC_STAT_INVALID            0x00u
  #define ECC_DRIVER_DATA_INVALID     0xFFu
#endif // FS_NOR_SUPPORT_ECC
#if FAIL_SAFE_ERASE_NO_REWRITE
  #define ECC_STAT_VALID_EX           0x00u
#endif // FAIL_SAFE_ERASE_NO_REWRITE

/*********************************************************************
*
*       Header member sizes
*/
#define SIZEOF_ERASE_SIGNATURE        4
#define SIZEOF_BASE_PSH               8
#define SIZEOF_CRC_PSH                4
#define SIZEOF_BASE_LSH               4
#define SIZEOF_CRC_LSH                4
#define SIZEOF_ECC_PSH                (2 * FS_NOR_MAX_NUM_BYTES_ECC_MAN + 2)
#define SIZEOF_ECC_LSH                (2 * FS_NOR_MAX_NUM_BYTES_ECC_MAN + 2 + FS_NOR_MAX_NUM_BYTES_ECC_DATA * FS_NOR_MAX_NUM_BLOCKS_ECC_DATA)
#if ((SIZEOF_ECC_PSH % 4) != 0)
  #define SIZEOF_ECC_PSH_ALIGNED      (SIZEOF_ECC_PSH + (4 - (SIZEOF_ECC_PSH % 4)))
#else
  #define SIZEOF_ECC_PSH_ALIGNED      SIZEOF_ECC_PSH
#endif
#if ((SIZEOF_ECC_LSH % 4) != 0)
  #define SIZEOF_ECC_LSH_ALIGNED      (SIZEOF_ECC_LSH + (4 - (SIZEOF_ECC_LSH % 4)))
#else
  #define SIZEOF_ECC_LSH_ALIGNED      SIZEOF_ECC_LSH
#endif
#if (FS_NOR_CAN_REWRITE == 0) && (FS_NOR_LINE_SIZE > 1)
  #define SIZEOF_CRC_PSH_EX           4
  #define SIZEOF_ECC_PSH_EX           (FS_NOR_MAX_NUM_BYTES_ECC_MAN + 1)
  #if ((SIZEOF_ECC_PSH_EX % 4) != 0)
    #define SIZEOF_ECC_PSH_ALIGNED_EX (SIZEOF_ECC_PSH_EX + (4 - (SIZEOF_ECC_PSH_EX % 4)))
  #else
    #define SIZEOF_ECC_PSH_ALIGNED_EX SIZEOF_ECC_PSH_EX
  #endif
  #define SIZEOF_BASE_PSH_EX          4
  #define SIZEOF_ECC_LSH_EX           (FS_NOR_MAX_NUM_BYTES_ECC_MAN + 1)
  #if ((SIZEOF_ECC_LSH_EX % 4) != 0)
    #define SIZEOF_ECC_LSH_ALIGNED_EX (SIZEOF_ECC_LSH_EX + (4 - (SIZEOF_ECC_LSH_EX % 4)))
  #else
    #define SIZEOF_ECC_LSH_ALIGNED_EX SIZEOF_ECC_LSH_EX
  #endif
  #define SIZEOF_BASE_LSH_EX          4
#endif // FS_NOR_CAN_REWRITE == 0

/*********************************************************************
*
*       ASSERT_UNIT_NO_IS_IN_RANGE
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)                                   \
    if ((Unit) >= (U8)FS_NOR_NUM_UNITS) {                                    \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR_BM: Invalid unit number.")); \
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);                                   \
    }
#else
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)
#endif

/*********************************************************************
*
*       ASSERT_PHY_TYPE_IS_SET
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_PHY_TYPE_IS_SET(pInst)                                   \
    if ((pInst)->pPhyType == NULL)  {                                     \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR_BM: PHY type not set.")); \
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);                                \
    }
#else
  #define ASSERT_PHY_TYPE_IS_SET(pInst)
#endif

/*********************************************************************
*
*       INIT_PSH_DATA_RANGE
*/
#if FS_NOR_OPTIMIZE_HEADER_WRITE
  #define INIT_PSH_DATA_RANGE()                                 _InitDataRange(&_pshDataRange)
#else
  #define INIT_PSH_DATA_RANGE()
#endif

/*********************************************************************
*
*       UPDATE_PSH_DATA_RANGE
*/
#if FS_NOR_OPTIMIZE_HEADER_WRITE
  #define UPDATE_PSH_DATA_RANGE(Off, NumBytes)                  _UpdateDataRange(&_pshDataRange, Off, NumBytes)
#else
  #define UPDATE_PSH_DATA_RANGE(Off, NumBytes)
#endif

/*********************************************************************
*
*       CALC_PSH_DATA_RANGE
*/
#if FS_NOR_OPTIMIZE_HEADER_WRITE
  #define CALC_PSH_DATA_RANGE(pInst, ppData, pOff, pNumBytes)    _CalcDataRange(pInst, &_pshDataRange, ppData, pOff, pNumBytes)
#else
  #define CALC_PSH_DATA_RANGE(pInst, ppData, pOff, pNumBytes)
#endif

/*********************************************************************
*
*       INIT_LSH_DATA_RANGE
*/
#if FS_NOR_OPTIMIZE_HEADER_WRITE
  #define INIT_LSH_DATA_RANGE()                                 _InitDataRange(&_lshDataRange)
#else
  #define INIT_LSH_DATA_RANGE()
#endif

/*********************************************************************
*
*       UPDATE_LSH_DATA_RANGE
*/
#if FS_NOR_OPTIMIZE_HEADER_WRITE
  #define UPDATE_LSH_DATA_RANGE(Off, NumBytes)                  _UpdateDataRange(&_lshDataRange, Off, NumBytes)
#else
  #define UPDATE_LSH_DATA_RANGE(Off, NumBytes)
#endif

/*********************************************************************
*
*       CALC_LSH_DATA_RANGE
*/
#if FS_NOR_OPTIMIZE_HEADER_WRITE
  #define CALC_LSH_DATA_RANGE(pInst, ppData, pOff, pNumBytes)   _CalcDataRange(pInst, &_lshDataRange, ppData, pOff, pNumBytes)
#else
  #define CALC_LSH_DATA_RANGE(pInst, ppData, pOff, pNumBytes)
#endif

/*********************************************************************
*
*       INIT_VERIFY
*/
#if (FS_SUPPORT_TEST != 0) && (FS_NOR_OPTIMIZE_HEADER_WRITE != 0)
  #define INIT_VERIFY(pData, Off, NumBytes)               _InitVerify(pData, Off, NumBytes)
#else
  #define INIT_VERIFY(pData, Off, NumBytes)
#endif

/*********************************************************************
*
*       VERIFY_WRITE
*/
#if (FS_SUPPORT_TEST != 0) && (FS_NOR_OPTIMIZE_HEADER_WRITE != 0)
  #define VERIFY_WRITE(pInst)                                                       \
    if (_VerifyWrite(pInst, _pVerifyData, _VerifyOff, (U32)_VerifyNumBytes) != 0) { \
      FS_X_PANIC(FS_ERRCODE_VERIFY_FAILURE);                                        \
    }

#else
  #define VERIFY_WRITE(pInst)
#endif

/*********************************************************************
*
*       CALL_TEST_HOOK_FAIL_SAFE
*/
#if FS_SUPPORT_TEST
  #define CALL_TEST_HOOK_FAIL_SAFE(Unit) _CallTestHookFailSafe(Unit)
#else
  #define CALL_TEST_HOOK_FAIL_SAFE(Unit)
#endif

/*********************************************************************
*
*       CALL_TEST_HOOK_DATA_READ_BEGIN
*/
#if FS_SUPPORT_TEST
  #define CALL_TEST_HOOK_DATA_READ_BEGIN(Unit, pData, pOff, pNumBytes) _CallTestHookDataReadBegin(Unit, pData, pOff, pNumBytes)
#else
  #define CALL_TEST_HOOK_DATA_READ_BEGIN(Unit, pData, pOff, pNumBytes)
#endif

/*********************************************************************
*
*       CALL_TEST_HOOK_DATA_READ_END
*/
#if FS_SUPPORT_TEST
  #define CALL_TEST_HOOK_DATA_READ_END(Unit, pData, Off, NumBytes, pResult) _CallTestHookDataReadEnd(Unit, pData, Off, NumBytes, pResult)
#else
  #define CALL_TEST_HOOK_DATA_READ_END(Unit, pData, Off, NumBytes, pResult)
#endif

/*********************************************************************
*
*       CALL_TEST_HOOK_DATA_WRITE_BEGIN
*/
#if FS_SUPPORT_TEST
  #define CALL_TEST_HOOK_DATA_WRITE_BEGIN(Unit, pData, pOff, pNumBytes) _CallTestHookDataWriteBegin(Unit, pData, pOff, pNumBytes)
#else
  #define CALL_TEST_HOOK_DATA_WRITE_BEGIN(Unit, pData, pOff, pNumBytes)
#endif

/*********************************************************************
*
*       CALL_TEST_HOOK_DATA_WRITE_END
*/
#if FS_SUPPORT_TEST
  #define CALL_TEST_HOOK_DATA_WRITE_END(Unit, pData, Off, NumBytes, pResult) _CallTestHookDataWriteEnd(Unit, pData, Off, NumBytes, pResult)
#else
  #define CALL_TEST_HOOK_DATA_WRITE_END(Unit, pData, Off, NumBytes, pResult)
#endif

/*********************************************************************
*
*       CALL_TEST_HOOK_SECTOR_ERASE
*/
#if FS_SUPPORT_TEST
  #define CALL_TEST_HOOK_SECTOR_ERASE(Unit, PhySectorIndex, pResult) _CallTestHookSectorErase(Unit, PhySectorIndex, pResult)
#else
  #define CALL_TEST_HOOK_SECTOR_ERASE(Unit, PhySectorIndex, pResult)
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
*       ERASE_PHY_SECTOR
*/
#if FS_NOR_SUPPORT_FAIL_SAFE_ERASE
  #define ERASE_PHY_SECTOR(pInst, PhySectorIndex, pEraseCnt) _ErasePhySectorFailSafe(pInst, PhySectorIndex, pEraseCnt)
#else
  #define ERASE_PHY_SECTOR(pInst, PhySectorIndex, pEraseCnt) _ErasePhySector(pInst, PhySectorIndex, pEraseCnt)
#endif // FS_NOR_SUPPORT_FAIL_SAFE_ERASE

#if FS_NOR_SUPPORT_ECC

/*********************************************************************
*
*       UPDATE_NUM_BIT_ERRORS
*/
#if FS_NOR_ENABLE_STATS
  #define UPDATE_NUM_BIT_ERRORS(pInst, NumBitErrors) _UpdateNumBitErrors(pInst, NumBitErrors)
#else
  #define UPDATE_NUM_BIT_ERRORS(pInst, NumBitErrors)
#endif

#endif // FS_NOR_SUPPORT_ECC

//lint -emacro((413), OFFSET_OF_MEMBER, SIZE_OF_MEMBER) Likely use of null pointer N:100. Rationale: This is the only way we can calculate the offset or the size of the structure member.

#define OFFSET_OF_MEMBER(Type, Member)        SEGGER_PTR2ADDR(&(((Type *)0)->Member))             // MISRA deviation D:100e
#if (FS_NOR_SUPPORT_VARIABLE_LINE_SIZE != 0) || (FS_NOR_SUPPORT_CRC != 0) || (FAIL_SAFE_ERASE_NO_REWRITE != 0)  || (FS_NOR_SUPPORT_ECC != 0)
  #define SIZE_OF_MEMBER(Type, Member)        (sizeof(((Type *)0)->Member))
#endif
#if (FS_NOR_SUPPORT_VARIABLE_LINE_SIZE != 0) || (FS_NOR_OPTIMIZE_HEADER_WRITE != 0)
  #define ALIGN_TO_BOUNDARY(Value, Boundary)  (((Value) + (Boundary) - 1u) & ~((Boundary) - 1u))
#endif // FS_NOR_SUPPORT_VARIABLE_LINE_SIZE

/*********************************************************************
*
*       Sanity checks
*/
#if (FS_NOR_SUPPORT_CRC != 0) && (FS_NOR_PHY_SECTOR_RESERVE < (SIZEOF_ERASE_SIGNATURE + SIZEOF_CRC_PSH))
  #error FS_NOR_PHY_SECTOR_RESERVE has to be set to a larger value with FS_NOR_SUPPORT_CRC != 0.
#endif

#if (FS_NOR_SUPPORT_CRC != 0) && (FS_NOR_LOG_SECTOR_RESERVE < SIZEOF_CRC_LSH)
  #error FS_NOR_LOG_SECTOR_RESERVE has to be set to a larger value with FS_NOR_SUPPORT_CRC != 0.
#endif

#if (FS_NOR_SUPPORT_ECC != 0) && (FS_NOR_PHY_SECTOR_RESERVE < (SIZEOF_ERASE_SIGNATURE + SIZEOF_CRC_PSH + SIZEOF_ECC_PSH))
  #error FS_NOR_PHY_SECTOR_RESERVE has to be set to a larger value with FS_NOR_SUPPORT_ECC != 0.
#endif

#if (FS_NOR_SUPPORT_ECC != 0) && (FS_NOR_LOG_SECTOR_RESERVE < (SIZEOF_CRC_LSH + SIZEOF_ECC_LSH))
  #error FS_NOR_LOG_SECTOR_RESERVE has to be set to a larger value with FS_NOR_SUPPORT_ECC != 0.
#endif

/*********************************************************************
*
*       Local types
*
**********************************************************************
*/

#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ALL)

/*********************************************************************
*
*       DATA_TYPE_DESC
*/
typedef struct {
  U8           Type;
  const char * s;
} DATA_TYPE_DESC;

#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ALL

#if FS_NOR_OPTIMIZE_HEADER_WRITE

/*********************************************************************
*
*       NOR_BM_DATA_RANGE
*
*  Notes
*    (1) We assume that the header of a logical or physical sector is not larger than 65536 bytes.
*/
typedef struct {
  U16 OffStart;   // Note 1
  U16 OffEnd;     // Note 1
} NOR_BM_DATA_RANGE;

#endif // FS_NOR_OPTIMIZE_HEADER_WRITE

typedef struct NOR_BM_INST NOR_BM_INST;       //lint -esym(9058, NOR_BM_INST) tag unused outside of typedefs N:100. Rationale: the typedef is used as forward declaration.

//lint -esym(754, NOR_BM_PSH::abPaddingPSH, NOR_BM_PSH::abPaddingWork, NOR_BM_PSH::abPaddingData, NOR_BM_PSH::abPaddingInvalid) local structure member not referenced. Rationale: these members are used only for padding of other members to a required byte boundary.
//lint -esym(754, NOR_BM_PSH::abReservedECC, NOR_BM_PSH::abReservedPSH, NOR_BM_PSH::abReservedWorkCRC, NOR_BM_PSH::abReservedWorkECC, NOR_BM_PSH::abReservedWork, NOR_BM_PSH::abReservedDataCRC, NOR_BM_PSH::abReservedDataECC, NOR_BM_PSH::abReservedData, NOR_BM_PSH::abReservedInvalidBase, NOR_BM_PSH::abReservedInvalidECC, NOR_BM_PSH::abReservedInvalid) local structure member not referenced. Rationale: these members are used only for padding of other members to a required byte boundary.
//lint -esym(754, NOR_BM_PSH::crc0, NOR_BM_PSH::crc1, NOR_BM_PSH::crc2, NOR_BM_PSH::crcStat, NOR_BM_PSH::crcWork, NOR_BM_PSH::crcData) local structure member not referenced. Rationale: these members are used only if the CRC verification feature is enabled.
//lint -esym(754, NOR_BM_PSH::abECCWork, NOR_BM_PSH::abECCData, NOR_BM_PSH::eccStatWork, , NOR_BM_PSH::eccStatData, NOR_BM_PSH::abECCInvalid, NOR_BM_PSH::eccStatInvalid) local structure member not referenced. Rationale: these members are used only if the ECC verification feature is enabled.
//lint -esym(754, NOR_BM_PSH::EraseSignature, NOR_BM_PSH::DataCntWork, NOR_BM_PSH::lbiWork, NOR_BM_PSH::DataCntData, NOR_BM_PSH::lbiData) local structure member not referenced. Rationale: these members are used only if the support for NOR flash devices that cannot rewrite is enabled.

/*********************************************************************
*
*       NOR_BM_PSH
*
*  Description
*    Management data of a physical sector.
*
*  Additional information
*    This structure holds information about how a physical sector is used by the driver such
*    as the type of data stored in it and the number of times the physical sector was erased.
*    The data of this structure is stored at the beginning of every physical sector, with the
*    exception of the first one, that the driver uses as storage. More exactly at byte offset
*    0 relative to the beginning of that physical sector. The layout of the data stored to
*    the NOR flash device is identical with the layout of this structure. In addition, this structure
*    is used to keep in RAM the information about the physical sector that was read from
*    the NOR flash device.
*
*    The availability of some members of this structure varies with the driver configuration.
*    However, the first four members DataStat, DataCnt, lbi and EraseCnt are always present.
*    The structure is always aligned to at least a four byte boundary. The actual alignment depends
*    on the configured NOR flash line size. Some of the structure members do not store any data.
*    They are used only to make sure that the structure itself and its members are aligned.
*    Typically, these members have the word "reserved" or "padding" in their names.
*    Making sure that the members are aligned in all possible configurations makes
*    the declaration of the structure relatively complicated.
*
*    For optimization purposes, the structure is organized in sections with each
*    section being aligned to a NOR flash line boundary so that the driver can store
*    only one section at the time instead of the entire structure to the NOR flash device.
*    This is done in order to increase the performance. The structure has a maximum of four
*    sections. This is the case for NOR flash devices that do not support incremental
*    write operations.
*
*  Notes
*    (1) The implementation of _CalcSectionSizePSH() has to be updated if new structure members
*        are added.
*/
typedef struct {
  //
  // First section (SectionIndex: 0)
  //
  U8  DataStat;                                           // Type of data stored by the physical sector.
  U8  DataCnt;                                            // Number of times this physical sector was copied to another location since creation.
  U16 lbi;                                                // Index of the logical block stored in this physical sector.
  U32 EraseCnt;                                           // Number of times the sector was erased since the last low-level format operation.
#if (FS_NOR_PHY_SECTOR_RESERVE >= SIZEOF_ERASE_SIGNATURE)
  U32 EraseSignature;                                     // Set to indicate that a physical sector has been successfully erased.
#endif
#if (FS_NOR_PHY_SECTOR_RESERVE >= (SIZEOF_ERASE_SIGNATURE + SIZEOF_CRC_PSH))
  U8  crc0;                                               // 8-bit check sum of all the structure members up to and not including the crc0 member.
  U8  crc1;                                               // 8-bit check sum of all the structure members up to and not including the crc0 member.
  U8  crc2;                                               // 8-bit check sum of all the structure members up to and not including the crc0 member.
  U8  crcStat;                                            // Indicates which CRC item contains the current check sum.
#endif
#if (FS_NOR_PHY_SECTOR_RESERVE >= (SIZEOF_ERASE_SIGNATURE + SIZEOF_CRC_PSH + SIZEOF_ECC_PSH))
  U8  abECC0[FS_NOR_MAX_NUM_BYTES_ECC_MAN];               // ECC of the structure members DataStat, DataCnt and lbi.
  U8  abECC1[FS_NOR_MAX_NUM_BYTES_ECC_MAN];               // ECC of the structure members DataStat, DataCnt and lbi.
  U8  ecc0Stat;                                           // Indicates the status of abECC0[] (not set, valid, or invalid).
  U8  ecc1Stat;                                           // Indicates the status of abECC1[] (not set, valid, or invalid).
#if ((SIZEOF_ECC_PSH % 4) != 0)
  U8  abReservedECC[4 - (SIZEOF_ECC_PSH % 4)];            // Pad to a 4 byte boundary.
#endif
#endif
#if (FS_NOR_PHY_SECTOR_RESERVE >= (SIZEOF_ERASE_SIGNATURE + SIZEOF_CRC_PSH + SIZEOF_ECC_PSH_ALIGNED + 4))
  U8  abReservedPSH[FS_NOR_PHY_SECTOR_RESERVE - (SIZEOF_ERASE_SIGNATURE + SIZEOF_CRC_PSH + SIZEOF_ECC_PSH_ALIGNED)];
#endif
#if (((SIZEOF_BASE_PSH + FS_NOR_PHY_SECTOR_RESERVE) % FS_NOR_LINE_SIZE) != 0)
  U8  abPaddingPSH[FS_NOR_LINE_SIZE - ((SIZEOF_BASE_PSH + FS_NOR_PHY_SECTOR_RESERVE) % FS_NOR_LINE_SIZE)];   // Pad to line size
#endif
  //
  // The following structure members are required only
  // for NOR flash devices that do not support incremental
  // write operation.
  //
#if (FS_NOR_CAN_REWRITE == 0)
  //
  // Second section (SectionIndex: 1)
  //
  U8  IsWork;                                             // Set to 0 if the physical sector stores or stored a work block.
#if (FS_NOR_LINE_SIZE > 1)
  U8  DataCntWork;
  U16 lbiWork;
#if (FS_NOR_PHY_SECTOR_RESERVE_EX >= SIZEOF_CRC_PSH_EX)
  U8  crcWork;                                            // 8-bit checksum of IsWork, DataCntWork and lbiWork.
  U8  abReservedWorkCRC[4 - 1];
#endif
#if (FS_NOR_PHY_SECTOR_RESERVE_EX >= (SIZEOF_CRC_PSH_EX + SIZEOF_ECC_PSH_EX))
  U8  abECCWork[FS_NOR_MAX_NUM_BYTES_ECC_MAN];            // ECC of IsWork, DataCntWork and lbiWork.
  U8  eccStatWork;
#if ((SIZEOF_ECC_PSH_EX % 4) != 0)
  U8  abReservedWorkECC[4 - (SIZEOF_ECC_PSH_EX % 4)];     // Pad to a 4 byte boundary.
#endif
#endif
#if (FS_NOR_PHY_SECTOR_RESERVE_EX >= (SIZEOF_CRC_PSH_EX + SIZEOF_ECC_PSH_ALIGNED_EX + 4))
  U8  abReservedWork[FS_NOR_PHY_SECTOR_RESERVE_EX - (SIZEOF_CRC_PSH_EX + SIZEOF_ECC_PSH_ALIGNED_EX)];
#endif
#if (((SIZEOF_BASE_PSH_EX + FS_NOR_PHY_SECTOR_RESERVE_EX) % FS_NOR_LINE_SIZE) != 0)
  U8  abPaddingWork[FS_NOR_LINE_SIZE - ((SIZEOF_BASE_PSH_EX + FS_NOR_PHY_SECTOR_RESERVE_EX) % FS_NOR_LINE_SIZE)];
#endif
#endif // FS_NOR_LINE_SIZE > 1
  //
  // Third section (SectionIndex: 2)
  //
  U8  IsValid;                                            // Set to 0 if the physical sector stores or stored a data block.
#if (FS_NOR_LINE_SIZE > 1)
  U8  DataCntData;
  U16 lbiData;
#if (FS_NOR_PHY_SECTOR_RESERVE_EX >= SIZEOF_CRC_PSH_EX)
  U8  crcData;                                            // 8-bit checksum of IsValid, DataCntData and lbiData.
  U8  abReservedDataCRC[4 - 1];
#endif
#if (FS_NOR_PHY_SECTOR_RESERVE_EX >= (SIZEOF_CRC_PSH_EX + SIZEOF_ECC_PSH_EX))
  U8  abECCData[FS_NOR_MAX_NUM_BYTES_ECC_MAN];
  U8  eccStatData;
#if ((SIZEOF_ECC_PSH_EX % 4) != 0)
  U8  abReservedDataECC[4 - (SIZEOF_ECC_PSH_EX % 4)];     // Pad to a 4 byte boundary.
#endif
#endif
#if (FS_NOR_PHY_SECTOR_RESERVE_EX >= (SIZEOF_CRC_PSH_EX + SIZEOF_ECC_PSH_ALIGNED_EX + 4))
  U8  abReservedData[FS_NOR_PHY_SECTOR_RESERVE_EX - (SIZEOF_CRC_PSH_EX + SIZEOF_ECC_PSH_ALIGNED_EX)];
#endif
#if (((SIZEOF_BASE_PSH_EX + FS_NOR_PHY_SECTOR_RESERVE_EX) % FS_NOR_LINE_SIZE) != 0)
  U8  abPaddingData[FS_NOR_LINE_SIZE - ((SIZEOF_BASE_PSH_EX + FS_NOR_PHY_SECTOR_RESERVE_EX) % FS_NOR_LINE_SIZE)];
#endif
#endif // FS_NOR_LINE_SIZE > 1
  //
  // Fourth section (SectionIndex: 3)
  //
  U8  IsInvalid;                                          // Set to 0 if the data of the physical sector is invalid.
#if (FS_NOR_LINE_SIZE > 1)
  U8  abReservedInvalidBase[4 - 1];
#if (FS_NOR_PHY_SECTOR_RESERVE_EX >= SIZEOF_ECC_PSH_EX)
  U8  abECCInvalid[FS_NOR_MAX_NUM_BYTES_ECC_MAN];
  U8  eccStatInvalid;
#if ((SIZEOF_ECC_PSH_EX % 4) != 0)
  U8  abReservedInvalidECC[4 - (SIZEOF_ECC_PSH_EX % 4)];  // Pad to a 4 byte boundary.
#endif
#endif
#if (FS_NOR_PHY_SECTOR_RESERVE_EX >= (SIZEOF_ECC_PSH_ALIGNED_EX + 4))
  U8  abReservedInvalid[FS_NOR_PHY_SECTOR_RESERVE_EX - SIZEOF_ECC_PSH_ALIGNED_EX];
#endif
#if (((SIZEOF_BASE_PSH_EX + FS_NOR_PHY_SECTOR_RESERVE_EX) % FS_NOR_LINE_SIZE) != 0)
  U8  abPaddingInvalid[FS_NOR_LINE_SIZE - ((SIZEOF_BASE_PSH_EX + FS_NOR_PHY_SECTOR_RESERVE_EX) % FS_NOR_LINE_SIZE)];
#endif
#endif // FS_NOR_LINE_SIZE > 1
#endif // FS_NOR_CAN_REWRITE == 0
} NOR_BM_PSH;

//lint -esym(754, NOR_BM_LSH::crc0, NOR_BM_LSH::crc1, NOR_BM_LSH::crcSectorData, NOR_BM_LSH::crcStat, NOR_BM_LSH::abECCData, NOR_BM_LSH::eccStatData, NOR_BM_LSH::abECCInvalid, NOR_BM_LSH::eccStatInvalid) local structure member not referenced. Rationale: these members are used only for padding of other members to a required byte boundary.
//lint -esym(754, NOR_BM_LSH::abReservedECC, NOR_BM_LSH::abReservedLSH, NOR_BM_LSH::abReservedDataBase, NOR_BM_LSH::abReservedDataECC, NOR_BM_LSH::abReservedData, NOR_BM_LSH::abReservedInvalidBase, NOR_BM_LSH::abReservedInvalidECC, NOR_BM_LSH::abReservedInvalid) local structure member not referenced. Rationale: these members are used only for padding of other members to a required byte boundary.
//lint -esym(754, NOR_BM_LSH::abPaddingLSH, NOR_BM_LSH::abPaddingData, NOR_BM_LSH::abPaddingInvalid) local structure member not referenced. Rationale: these members are used only for padding of other members to a required byte boundary.

/*********************************************************************
*
*       NOR_BM_LSH
*
*  Description
*    Management data of a logical sector.
*
*  Additional information
*    This structure holds information about how a logical sector is used by the driver such
*    as if the data is valid or not. The data of this structure is stored at the beginning
*    of every logical sector. The payload of the logical sector is stored right after this structure.
*    The layout of the data stored to the NOR flash device is identical with the layout of this
*    structure. In addition, this structure is used to keep in RAM the information about the logical sector
*    that was read from the NOR flash device.
*
*    The availability of some members of this structure varies with the driver configuration.
*    However, the members DataStat and brsi are always present.
*    The structure is always aligned to at least a four byte boundary. The actual alignment depends
*    on the configured NOR flash line size. Some of the structure members do not store any data.
*    They are used only to make sure that the structure itself and its members are aligned.
*    Typically, these members have the word "reserved" or "padding" in their names.
*    Making sure that the members are aligned in all possible configurations makes
*    the declaration of the structure relatively complicated.
*
*    For optimization purposes, the structure is organized in sections with each
*    section being aligned to a NOR flash line boundary so that the driver can store
*    only one section at the time instead of the entire structure to the NOR flash device.
*    This is done in order to increase the performance. The structure has a maximum of three
*    sections. This is the case for NOR flash devices that do not support incremental
*    write operations.
*
*  Notes
*    (1) The implementation of _CalcSectionSizeLSH() has to be updated if new structure members
*        are added.
*/
typedef struct {
  //
  // First section (SectionIndex: 0)
  //
  U8  DataStat;                                         // Status of data stored in the logical sector (empty, valid, invalid)
  U8  crc0;                                             // Stores the 8-bit check sum of DataStat and brsi structure members when the logical sector header is written for the first time after the erase operation.
  U16 brsi;                                             // Index of the logical sector relative to begin of logical block
#if (FS_NOR_LOG_SECTOR_RESERVE >= SIZEOF_CRC_LSH)
  U16 crcSectorData;                                    // 16-bit check sum of the sector data.
  U8  crc1;                                             // Stores the 8-bit check sum of DataStat and brsi structure members when the logical sector header is written for the second time after the erase operation.
  U8  crcStat;                                          // Indicates if crc0 or crc1 contains the current check sum.
#endif
#if (FS_NOR_LOG_SECTOR_RESERVE >= (SIZEOF_CRC_LSH + SIZEOF_ECC_LSH))
  U8  aaECCSectorData[FS_NOR_MAX_NUM_BLOCKS_ECC_DATA][FS_NOR_MAX_NUM_BYTES_ECC_DATA];   // ECC of the sector data.
  U8  abECC0[FS_NOR_MAX_NUM_BYTES_ECC_MAN];             // Stores the ECC of DataStat and brsi structure members when the logical sector header is written for the first time after the erase operation.
  U8  abECC1[FS_NOR_MAX_NUM_BYTES_ECC_MAN];             // Stores the ECC of DataStat and brsi structure members when the logical sector header is written for the second time after the erase operation.
  U8  ecc0Stat;                                         // Indicates the status of abECC0[].
  U8  ecc1Stat;                                         // Indicates the status of abECC1[].
#if ((SIZEOF_ECC_LSH % 4) != 0)
  U8  abReservedECC[4 - (SIZEOF_ECC_LSH % 4)];          // Align to 4 byte boundary.
#endif
#endif
#if (FS_NOR_LOG_SECTOR_RESERVE >= (SIZEOF_CRC_LSH + SIZEOF_ECC_LSH_ALIGNED + 4))
  U8  abReservedLSH[FS_NOR_LOG_SECTOR_RESERVE - (SIZEOF_CRC_LSH + SIZEOF_ECC_LSH_ALIGNED)];
#endif
#if (((SIZEOF_BASE_LSH + FS_NOR_LOG_SECTOR_RESERVE) % FS_NOR_LINE_SIZE) != 0)
  U8  abPaddingLSH[FS_NOR_LINE_SIZE - ((SIZEOF_BASE_LSH + FS_NOR_LOG_SECTOR_RESERVE) % FS_NOR_LINE_SIZE)];   // Pad to line size
#endif
  //
  // The following structure members are required only
  // for NOR flash devices that do not support incremental
  // write operation.
  //
#if (FS_NOR_CAN_REWRITE == 0)
  //
  // Second section (SectionIndex: 1)
  //
  U8  IsValid;
#if (FS_NOR_LINE_SIZE > 1)
  U8  abReservedDataBase[4 - 1];
#if (FS_NOR_LOG_SECTOR_RESERVE_EX >= SIZEOF_ECC_LSH_EX)
  U8  abECCData[FS_NOR_MAX_NUM_BYTES_ECC_MAN];
  U8  eccStatData;
#if ((SIZEOF_ECC_LSH_EX % 4) != 0)
  U8  abReservedDataECC[4 - (SIZEOF_ECC_LSH_EX % 4)];  // Pad to a 4 byte boundary.
#endif
#endif
#if (FS_NOR_LOG_SECTOR_RESERVE_EX >= (SIZEOF_ECC_LSH_ALIGNED_EX + 4))
  U8  abReservedData[FS_NOR_LOG_SECTOR_RESERVE_EX - (SIZEOF_ECC_LSH_ALIGNED_EX)];
#endif
#if (((SIZEOF_BASE_LSH_EX + FS_NOR_LOG_SECTOR_RESERVE_EX) % FS_NOR_LINE_SIZE) != 0)
  U8  abPaddingData[FS_NOR_LINE_SIZE - ((SIZEOF_BASE_LSH_EX + FS_NOR_LOG_SECTOR_RESERVE_EX) % FS_NOR_LINE_SIZE)];
#endif
#endif // FS_NOR_LINE_SIZE > 1
  //
  // Third section (SectionIndex: 2)
  //
  U8  IsInvalid;
#if (FS_NOR_LINE_SIZE > 1)
  U8  abReservedInvalidBase[4 - 1];
#if (FS_NOR_LOG_SECTOR_RESERVE_EX >= SIZEOF_ECC_LSH_EX)
  U8  abECCInvalid[FS_NOR_MAX_NUM_BYTES_ECC_MAN];
  U8  eccStatInvalid;
#if ((SIZEOF_ECC_LSH_EX % 4) != 0)
  U8  abReservedInvalidECC[4 - (SIZEOF_ECC_LSH_EX % 4)];  // Pad to a 4 byte boundary.
#endif
#endif
#if (FS_NOR_LOG_SECTOR_RESERVE_EX >= (SIZEOF_ECC_LSH_ALIGNED_EX + 4))
  U8  abReservedInvalid[FS_NOR_LOG_SECTOR_RESERVE_EX - (SIZEOF_ECC_LSH_ALIGNED_EX)];
#endif
#if (((SIZEOF_BASE_LSH_EX + FS_NOR_LOG_SECTOR_RESERVE_EX) % FS_NOR_LINE_SIZE) != 0)
  U8  abPaddingInvalid[FS_NOR_LINE_SIZE - ((SIZEOF_BASE_LSH_EX + FS_NOR_LOG_SECTOR_RESERVE_EX) % FS_NOR_LINE_SIZE)];
#endif
#endif // FS_NOR_LINE_SIZE > 1
#endif // FS_NOR_CAN_REWRITE == 0
} NOR_BM_LSH;

#if FS_NOR_SUPPORT_CRC

/*********************************************************************
*
*       NOR_BM_CRC_API
*/
typedef struct {
  int (*pfCalcStorePSH)      (const NOR_BM_INST * pInst, NOR_BM_PSH * pPSH);
  int (*pfLoadVerifyPSH)     (const NOR_BM_INST * pInst, const NOR_BM_PSH * pPSH);
  int (*pfCalcStoreLSH)      (      NOR_BM_LSH  * pLSH);
  int (*pfLoadVerifyLSH)     (const NOR_BM_INST * pInst, NOR_BM_LSH * pLSH);
  U16 (*pfCalcData)          (const U8 * pData, unsigned NumBytes, U16 crc);
#if FAIL_SAFE_ERASE_NO_REWRITE
  int (*pfCalcStorePSH_Data) (const NOR_BM_INST * pInst, NOR_BM_PSH * pPSH);
  int (*pfCalcStorePSH_Work) (const NOR_BM_INST * pInst, NOR_BM_PSH * pPSH);
  int (*pfLoadVerifyPSH_Data)(const NOR_BM_INST * pInst, const NOR_BM_PSH * pPSH);
  int (*pfLoadVerifyPSH_Work)(const NOR_BM_INST * pInst, const NOR_BM_PSH * pPSH);
#endif // FAIL_SAFE_ERASE_NO_REWRITE
} NOR_BM_CRC_API;

#endif

#if FS_NOR_SUPPORT_ECC

/*********************************************************************
*
*       NOR_BM_ECC_API
*/
typedef struct {
  int  (*pfCalcStorePSH)        (const NOR_BM_INST * pInst, NOR_BM_PSH * pPSH, int * pNumBitsCorrected);
  int  (*pfLoadApplyPSH)        (const NOR_BM_INST * pInst, NOR_BM_PSH * pPSH, int * pNumBitsCorrected);
  int  (*pfCalcStoreLSH)        (const NOR_BM_INST * pInst, NOR_BM_LSH * pLSH, int * pNumBitsCorrected);
  int  (*pfLoadApplyLSH)        (const NOR_BM_INST * pInst, NOR_BM_LSH * pLSH, int * pNumBitsCorrected);
  void (*pfCalcData)            (const NOR_BM_INST * pInst, const U32 * pData, U8 * pECC);
  int  (*pfApplyData)           (const NOR_BM_INST * pInst,       U32 * pData, U8 * pECC);
#if FAIL_SAFE_ERASE_NO_REWRITE
  int  (*pfCalcStorePSH_Data)   (const NOR_BM_INST * pInst, NOR_BM_PSH * pPSH);
  int  (*pfCalcStorePSH_Work)   (const NOR_BM_INST * pInst, NOR_BM_PSH * pPSH);
  int  (*pfCalcStorePSH_Invalid)(const NOR_BM_INST * pInst, NOR_BM_PSH * pPSH);
  int  (*pfLoadApplyPSH_Data)   (const NOR_BM_INST * pInst, NOR_BM_PSH * pPSH, int * pNumBitsCorrected);
  int  (*pfLoadApplyPSH_Work)   (const NOR_BM_INST * pInst, NOR_BM_PSH * pPSH, int * pNumBitsCorrected);
  int  (*pfLoadApplyPSH_Invalid)(const NOR_BM_INST * pInst, NOR_BM_PSH * pPSH, int * pNumBitsCorrected);
  int  (*pfCalcStoreLSH_Data)   (const NOR_BM_INST * pInst, NOR_BM_LSH * pLSH);
  int  (*pfCalcStoreLSH_Invalid)(const NOR_BM_INST * pInst, NOR_BM_LSH * pLSH);
#endif // FAIL_SAFE_ERASE_NO_REWRITE
} NOR_BM_ECC_API;

#endif // FS_NOR_SUPPORT_ECC

//lint -esym(754, DATA_CHECK::Dummy) local structure member not referenced. Rationale: this member is used only to prevent the compiler from generating an error.

/*********************************************************************
*
*       DATA_CHECK
*/
typedef struct {
#if (FS_NOR_SUPPORT_CRC != 0) && (FS_NOR_SUPPORT_ECC != 0)
  U16 crc;
  U8  aaECC[FS_NOR_MAX_NUM_BLOCKS_ECC_DATA][FS_NOR_MAX_NUM_BYTES_ECC_DATA];
#elif (FS_NOR_SUPPORT_CRC != 0) && (FS_NOR_SUPPORT_ECC == 0)
  U16 crc;
#elif(FS_NOR_SUPPORT_CRC == 0) && (FS_NOR_SUPPORT_ECC != 0)
  U8  aaECC[FS_NOR_MAX_NUM_BLOCKS_ECC_DATA][FS_NOR_MAX_NUM_BYTES_ECC_DATA];
#else
  U8  Dummy;
#endif
} DATA_CHECK;

#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER

/*********************************************************************
*
*       MULTI_BYTE_API
*/
typedef struct {
  U32  (*pfLoadU32) (const U8 * pBuffer);
  void (*pfStoreU32)(      U8 * pBuffer, U32 v);
  U16  (*pfLoadU16) (const U8 * pBuffer);
  void (*pfStoreU16)(      U8 * pBuffer, unsigned v);
} MULTI_BYTE_API;

#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER

/*********************************************************************
*
*       NOR_BM_WORK_BLOCK
*
*  Description
*    Work block information.
*
*  Additional information
*    The pNext and pPrev members are used for keeping the work block
*    in a doubly linked list.
*
*    A bit set to 1 in paIsWritten indicates that the driver already
*    stored some data to the logical sector corresponding to the bit
*    position in the array.
*
*    The size of an element in paAssign is calculated based on the total
*    number of logical sectors that the stored in a logical block.
*/
typedef struct NOR_BM_WORK_BLOCK {
  struct NOR_BM_WORK_BLOCK * pNext;           // Pointer to next work block in the list. Set to NULL if the work block is last in the list.
  struct NOR_BM_WORK_BLOCK * pPrev;           // Pointer to previous work block in the list. Set to NULL it the work block is the first in the list.
  U8                       * paIsWritten;     // 1-bit array that stores the data status of the logical sectors stored in the work block.
  void                     * paAssign;        // Array containing the block relative index of the logical sectors stored in the work block.
  unsigned                   psi;             // Physical index of the destination sector which data is written to. 0 means none is selected yet.
  unsigned                   lbi;             // Logical block index of the work block
} NOR_BM_WORK_BLOCK;

#if FS_NOR_OPTIMIZE_DATA_WRITE

/*********************************************************************
*
*       NOR_BM_DATA_BLOCK
*
*  Description
*    Data block information.
*
*  Additional information
*    This structure is used for keeping information about a data
*    block in RAM. Typically, this information is used to check
*    if the data can be written directly to a data block.
*
*    A bit set to 1 in paIsWritten indicates that the driver already
*    stored some data to the logical sector corresponding to the bit
*    position in the array.
*/
typedef struct NOR_BM_DATA_BLOCK {
  struct NOR_BM_DATA_BLOCK * pNext;           // Pointer to next data block in the list. Set to NULL if the data block is last in the list.
  struct NOR_BM_DATA_BLOCK * pPrev;           // Pointer to previous data block in the list. Set to NULL if the data block is the first in the list.
  U8                       * paIsWritten;     // 1-bit array that stores the data status of the logical sectors stored in the data block.
  unsigned                   psi;             // Index of the physical sector that stores the data block.
} NOR_BM_DATA_BLOCK;

#endif // FS_NOR_OPTIMIZE_DATA_WRITE

/*********************************************************************
*
*       NOR_BM_INST
*
*  Description
*    Driver instance.
*
*  Additional information
*    This is the central data structure for the entire driver.
*/
struct NOR_BM_INST {
  const FS_NOR_PHY_TYPE   * pPhyType;               // Interface to physical layer
  U8                      * pFreeMap;               // Pointer to physical sector usage map. Each bit represents one physical sector. 0: Block is not assigned; 1: Assigned.
                                                    // The only purpose of the member is to find a free physical sector.
  U8                      * pLog2PhyTable;          // Pointer to Log2Phytable, which contains the logical block to physical sector translation (0: Not assigned)
  NOR_BM_WORK_BLOCK       * pFirstWorkBlockInUse;   // Pointer to the first work block
  NOR_BM_WORK_BLOCK       * pFirstWorkBlockFree;    // Pointer to the first free work block
  NOR_BM_WORK_BLOCK       * paWorkBlock;            // Work block management info
#if FS_NOR_SUPPORT_ECC
  const NOR_BM_ECC_API    * pECC_API;               // Functions to be called for ECC handling.
  const FS_NOR_ECC_HOOK   * pECCHookMan;            // Functions to be called for ECC calculation and verification of the management data.
  const FS_NOR_ECC_HOOK   * pECCHookData;           // Functions to be called for ECC calculation and verification of the sector data.
#endif // FS_NOR_SUPPORT_ECC
#if FS_NOR_OPTIMIZE_DATA_WRITE
  NOR_BM_DATA_BLOCK       * pFirstDataBlockInUse;   // Pointer to the first data block in use.
  NOR_BM_DATA_BLOCK       * pFirstDataBlockFree;    // Pointer to the first free data block.
  NOR_BM_DATA_BLOCK       * paDataBlock;            // Data block management information.
#endif // FS_NOR_OPTIMIZE_DATA_WRITE
  U32                       ErrorPSI;               // Index of the physical sector where the fatal error occurred
  U32                       NumLogSectors;          // Number of logical sectors. This is redundant, but the value is used in a lot of places, so it is worth it!
  U32                       PhySectorSize;          // Size of a physical sector in bytes
  U32                       EraseCntMax;            // Worst (= highest) erase count of all physical sectors
  U32                       EraseCntMin;            // Smallest erase count of all physical sectors. Used for active wear leveling.
  U32                       NumBlocksEraseCntMin;   // Number of erase counts with the smallest value
  U32                       MRUFreeBlock;           // Most recently used free physical sector
  U32                       MaxEraseCntDiff;        // Threshold for active wear leveling
#if FS_NOR_ENABLE_STATS
  FS_NOR_BM_STAT_COUNTERS   StatCounters;           // Statistical counters.
#endif // FS_NOR_ENABLE_STATS
  U16                       NumPhySectors;          // Number of physical sectors in the NOR partition.
  U16                       NumLogBlocks;           // NUmber of logical blocks in the NOR partition.
  U16                       ldBytesPerSector;       // Number of user data bytes in a logical sector (as power of 2)
  U16                       LSectorsPerPSector;     // Number of logical sectors that fit in a physical sector
  U16                       FirstPhySector;         // Index of the first physical sector used by the driver
  U16                       BytesPerSectorConf;     // Number of bytes available to file system in a logical sector
  U8                        Unit;                   // Instance index (0-based)
  U8                        IsInited;               // Set to 1 if the driver was successfully initialized. Set to 0 when the NOR flash is unmounted.
  U8                        IsLLMounted;            // Set to 1 if the NOR flash device was successfully mounted.
  U8                        LLMountFailed;          // Set to 1 if the low-level mount operation failed.
  U8                        HasFatalError;          // Set to 1 if the driver encountered a fatal error.
  U8                        ErrorType;              // Type of fatal error
  U8                        NumWorkBlocks;          // Number of configured work blocks
  U8                        NumBitsPhySectorIndex;  // Minimum number of bits required to represent a physical sector index.
  U8                        NumBitsSRSI;            // Minimum number of bits required to represent a sector-relative sector index.
  U8                        NumBytesIsWritten;      // Number of bytes in the paIsWritten member of NOR_BM_WORK_BLOCK.
  U8                        IsWLSuspended;          // Set to 1 if the application has temporarily disabled the wear leveling to reduce the write latency.
  U8                        NumWorkBlocksConf;      // Number of work blocks configured by application
#if FS_NOR_SUPPORT_FAIL_SAFE_ERASE
  U8                        FailSafeErase;          // Indicates if a phy. sector should be marked as successfully erased
  U8                        FailSafeEraseConf;      // Indicates if the fail-safe sector erase feature is enabled or not
#endif // FS_NOR_SUPPORT_FAIL_SAFE_ERASE
#if FS_NOR_VERIFY_ERASE
  U8                        VerifyErase;            // If set to 1 after the erase operation the driver checks if all bytes in the physical sector are set to 0xFF.
#endif // FS_NOR_VERIFY_ERASE
#if FS_NOR_VERIFY_WRITE
  U8                        VerifyWrite;            // If set to 1 after the write operation the driver reads back and compares the data.
#endif // FS_NOR_VERIFY_WRITE
#if FS_NOR_SKIP_BLANK_SECTORS
  U8                        SkipBlankSectors;       // If set to 1 the low-level format operation will not erase physical sectors which are already blank.
#endif // FS_NOR_SKIP_BLANK_SECTORS
  U8                        EraseUsedSectors;       // If set to 1 the low-level format operation has to erase all physical sectors. Else the physical sectors are invalidated.
  U8                        IsWriteProtected;       // Set to 1 if the NOR flash is write protected
#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  U8                        ldBytesPerLine;         // Indicates the number of bytes in a block that can be written only once (power of 2 exponent).
  U8                        IsRewriteSupported;     // If set to 1 the same data can be rewritten as long as 0 bits are preserved.
  U8                        SizeOfLSH;              // Number of bytes in the header of a logical sector.
  U8                        SizeOfPSH;              // Number of bytes in the header of a physical sector.
#endif // FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  U8                        InvalidSectorError;     // Set to 1 if an error must be reported when an invalid sector is read.
#if FS_NOR_SUPPORT_CLEAN
  U8                        IsCleanPhySector;       // Set to 1 if all the invalid physical sectors are erased.
  U8                        IsCleanWorkBlock;       // Set to 1 if all the work blocks are converted to data blocks or a conversion via copy is not required.
#endif // FS_NOR_SUPPORT_CLEAN
#if FS_NOR_SUPPORT_ECC
  U8                        NumBlocksECC;           // Number of parity checks that have to be calculated to cover a logical sector.
#endif // FS_NOR_SUPPORT_ECC
};

/*********************************************************************
*
*       Static const data
*
**********************************************************************
*/
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ALL)

static const DATA_TYPE_DESC _aDesc[] = {
  { DATA_STAT_EMPTY,   "EMPTY"   },
  { DATA_STAT_WORK,    "WORK"    },
  { DATA_STAT_VALID,   "VALID"   },
  { DATA_STAT_INVALID, "INVALID" }
};

#endif

/*********************************************************************
*
*       First physical sector in a NOR flash should have these values
*       for the NOR driver to recognize the device as properly formatted.
*/
static const U8 _acInfo[16] = {
  0x53, 0x45, 0x47, 0x47, 0x45, 0x52, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER

/*********************************************************************
*
*       _MultiByteAPI_LE
*/
static const MULTI_BYTE_API _MultiByteAPI_LE = {
  FS_LoadU32LE,
  FS_StoreU32LE,
  FS_LoadU16LE,
  FS_StoreU16LE
};

/*********************************************************************
*
*       _MultiByteAPI_BE
*/
static const MULTI_BYTE_API _MultiByteAPI_BE = {
  FS_LoadU32BE,
  FS_StoreU32BE,
  FS_LoadU16BE,
  FS_StoreU16BE
};

#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER

#if FS_NOR_SUPPORT_CRC

/*********************************************************************
*
*       FS_NOR_CRC_SW
*/
const FS_NOR_CRC_HOOK FS_NOR_CRC_SW = {
  FS_CRC8_Calc,
  FS_CRC16_Calc
};

#endif // FS_NOR_SUPPORT_CRC

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static NOR_BM_INST                         * _apInst[FS_NOR_NUM_UNITS];
static U8                                    _NumUnits = 0;
static FS_NOR_ON_FATAL_ERROR_CALLBACK      * _pfOnFatalError;
static U8                                    _IsFreeMemInUse = 0;
#if FS_NOR_SUPPORT_CRC
  static const NOR_BM_CRC_API              * _pCRC_API;
  static const FS_NOR_CRC_HOOK             * _pCRCHook = FS_NOR_CRC_HOOK_DEFAULT;
#endif // FS_NOR_SUPPORT_CRC
#if FS_NOR_SUPPORT_ECC
  static U32                               * _pECCBuffer;   // Buffer for ECC the calculation and verification.
#endif // FS_NOR_SUPPORT_ECC
#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  static const MULTI_BYTE_API              * _pMultiByteAPI = &_MultiByteAPI_LE;
#endif
#if FS_SUPPORT_TEST
  static FS_NOR_TEST_HOOK_NOTIFICATION     * _pfTestHookFailSafe;
  static FS_NOR_TEST_HOOK_DATA_READ_BEGIN  * _pfTestHookDataReadBegin;
  static FS_NOR_TEST_HOOK_DATA_READ_END    * _pfTestHookDataReadEnd;
  static FS_NOR_TEST_HOOK_DATA_WRITE_BEGIN * _pfTestHookDataWriteBegin;
  static FS_NOR_TEST_HOOK_DATA_WRITE_END   * _pfTestHookDataWriteEnd;
  static FS_NOR_TEST_HOOK_SECTOR_ERASE     * _pfTestHookSectorErase;
#endif
#if FS_NOR_OPTIMIZE_HEADER_WRITE
  static NOR_BM_DATA_RANGE                   _pshDataRange;
  static NOR_BM_DATA_RANGE                   _lshDataRange;
#if FS_SUPPORT_TEST
  static const U8                          * _pVerifyData;
  static U32                                 _VerifyOff;
  static U16                                 _VerifyNumBytes;
#endif // FS_SUPPORT_TEST
#endif // FS_NOR_OPTIMIZE_HEADER_WRITE

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

//lint -efunc(818, _ReadOff) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory] N:104. Rationale: the statistical counters stored in the driver instance are updated in debug builds.
//lint -efunc(818, _WriteOff) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory] N:104. Rationale: the statistical counters stored in the driver instance are updated in debug builds.
//lint -efunc(818, _ErasePhySector) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory] N:104. Rationale: the statistical counters stored in the driver instance are updated in debug builds.
//lint -efunc(818, _GetPhySectorDataStatNR) Pointer parameter 'pPSH' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory] N:104. Rationale: the PSH is modified if the support for ECC is enabled.
//lint -efunc(818, _CalcPSH_ECC) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory] N:104. Rationale: the PSH is modified if the support for the variable byte order is enabled.
//lint -efunc(818, _CalcStoreLSHWithCRCAndECC) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory] N:104. Rationale: the statistical counters stored in the driver instance are updated in debug builds.
//lint -efunc(818, _MarkPhySectorAsFree) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory] N:104. Rationale: the statistical counters stored in the driver instance are updated in debug builds.
//lint -efunc(818, _MarkPhySectorAsAllocated) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory] N:104. Rationale: the statistical counters stored in the driver instance are updated in debug builds.
//lint -efunc(818, _CalcStorePSHWithCRCAndECC) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory] N:104. Rationale: the function updates the statistical counters on debug builds.
//lint -efunc(818, _LoadWorkBlock) Pointer parameter 'pWorkBlock' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory] N:104. Rationale: the structure is updated during a work block conversion if FAIL_SAFE_ERASE_NO_REWRITE is set to 1.
//lint -efunc(818, _RemoveDataBlock) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory] N:104. Rationale: the function updates the list of data blocks if FS_NOR_OPTIMIZE_DATA_WRITE is set to 1.

#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ALL

/*********************************************************************
*
*       _TypeToName
*/
static const char * _TypeToName(unsigned Type) {
  unsigned i;

  for (i = 0; i < SEGGER_COUNTOF(_aDesc); i++) {
    if (_aDesc[i].Type == Type) {
      return _aDesc[i].s;
    }
  }
  return "---";
}

#endif

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
static void _CallTestHookDataReadBegin(U8 Unit, void * pData, U32 * pOff, U32 * pNumBytes) {
  if (_pfTestHookDataReadBegin != NULL) {
    _pfTestHookDataReadBegin(Unit, pData, pOff, pNumBytes);
  }
}

/*********************************************************************
*
*       _CallTestHookDataReadEnd
*/
static void _CallTestHookDataReadEnd(U8 Unit, void * pData, U32 Off, U32 NumBytes, int * pResult) {
  if (_pfTestHookDataReadEnd != NULL) {
    _pfTestHookDataReadEnd(Unit, pData, Off, NumBytes, pResult);
  }
}

/*********************************************************************
*
*       _CallTestHookDataWriteBegin
*/
static void _CallTestHookDataWriteBegin(U8 Unit, const void ** pData, U32 * pOff, U32 * pNumBytes) {
  if (_pfTestHookDataWriteBegin != NULL) {
    _pfTestHookDataWriteBegin(Unit, pData, pOff, pNumBytes);
  }
}

/*********************************************************************
*
*       _CallTestHookDataWriteEnd
*/
static void _CallTestHookDataWriteEnd(U8 Unit, const void * pData, U32 Off, U32 NumBytes, int * pResult) {
  if (_pfTestHookDataWriteEnd != NULL) {
    _pfTestHookDataWriteEnd(Unit, pData, Off, NumBytes, pResult);
  }
}

/*********************************************************************
*
*       _CallTestHookSectorErase
*/
static void _CallTestHookSectorErase(U8 Unit, U32 PhySectorIndex, int * pResult) {
  if (_pfTestHookSectorErase != NULL) {
    _pfTestHookSectorErase(Unit, PhySectorIndex, pResult);
  }
}

#endif // FS_SUPPORT_TEST

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

#if FS_NOR_ENABLE_STATS && (FS_NOR_SUPPORT_ECC != 0)

/*********************************************************************
*
*       _UpdateNumBitErrors
*/
static void _UpdateNumBitErrors(NOR_BM_INST * pInst, int NumBitErrors) {
  pInst->StatCounters.BitErrorCnt += (U32)NumBitErrors;
  if ((NumBitErrors > 0) && (NumBitErrors <= FS_NOR_STAT_MAX_BIT_ERRORS)) {
    pInst->StatCounters.aBitErrorCnt[NumBitErrors - 1]++;
  }
}

#endif // FS_NOR_ENABLE_STATS && FS_NOR_SUPPORT_ECC != 0

#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE

/*********************************************************************
*
*       _CalcSectionSizeLSH
*
*  Function description
*    Calculates the actual number of bytes stored in a section
*    of a logical sector header.
*
*  Parameters
*    SectionIndex   Index of the LSH section.
*
*  Return value
*    Number of bytes in the section.
*
*  Additional information
*    The logical sector header contains only one section (with index 0)
*    when the sources are compiled with FS_NOR_CAN_REWRITE set to 1 (default).
*    If the sources are compiled with FS_NOR_CAN_REWRITE set to 0 then
*    the header of the logical sector contains 3 sections:
*      Section 0: Contains data type, sector index, etc.
*      Section 1: Indicates that the sector data is valid.
*      Section 2: Indicates that the sector data has been invalidated.
*/
static U32 _CalcSectionSizeLSH(unsigned SectionIndex) {
  U32 NumBytes;

  //
  // Sanity checking.
  //
#if (FS_NOR_CAN_REWRITE == 0)
  FS_DEBUG_ASSERT(FS_MTYPE_DRIVER, SectionIndex <= 2u);
#else
  FS_DEBUG_ASSERT(FS_MTYPE_DRIVER, SectionIndex == 0u);
#endif
  //
  // Calculate the section size by its index.
  //
  NumBytes = 0;
  switch (SectionIndex) {
  case 0:
#if (FS_NOR_LOG_SECTOR_RESERVE < SIZEOF_CRC_LSH)
    NumBytes = 0u
             + OFFSET_OF_MEMBER(NOR_BM_LSH, brsi)            // Last member
             + SIZE_OF_MEMBER(NOR_BM_LSH, brsi)
             - OFFSET_OF_MEMBER(NOR_BM_LSH, DataStat);       // First member
#elif (FS_NOR_LOG_SECTOR_RESERVE < (SIZEOF_CRC_LSH + SIZEOF_ECC_LSH))
    NumBytes = 0u
             + OFFSET_OF_MEMBER(NOR_BM_LSH, crcStat)         // Last member
             + SIZE_OF_MEMBER(NOR_BM_LSH, crcStat)
             - OFFSET_OF_MEMBER(NOR_BM_LSH, DataStat);       // First member
#elif (FS_NOR_LOG_SECTOR_RESERVE < (SIZEOF_CRC_LSH + SIZEOF_ECC_LSH_ALIGNED + 4))
    NumBytes = 0u
             + OFFSET_OF_MEMBER(NOR_BM_LSH, ecc1Stat)        // Last member
             + SIZE_OF_MEMBER(NOR_BM_LSH, ecc1Stat)
             - OFFSET_OF_MEMBER(NOR_BM_LSH, DataStat);       // First member
#else
    NumBytes = 0u
             + OFFSET_OF_MEMBER(NOR_BM_LSH, abReservedLSH)   // Last member
             + SIZE_OF_MEMBER(NOR_BM_LSH, abReservedLSH)
             - OFFSET_OF_MEMBER(NOR_BM_LSH, DataStat);       // First member
#endif // FS_NOR_LOG_SECTOR_RESERVE < SIZEOF_CRC_LSH
    break;
#if (FS_NOR_CAN_REWRITE == 0)
  case 1:
#if (FS_NOR_LINE_SIZE == 1)
    NumBytes = 0u
             + OFFSET_OF_MEMBER(NOR_BM_LSH, IsValid)         // Last member
             + SIZE_OF_MEMBER(NOR_BM_LSH, IsValid)
             - OFFSET_OF_MEMBER(NOR_BM_LSH, IsValid);        // First member
#elif (FS_NOR_LOG_SECTOR_RESERVE_EX >= SIZEOF_ECC_LSH_EX)
    NumBytes = 0u
             + OFFSET_OF_MEMBER(NOR_BM_LSH, eccStatData)     // Last member
             + SIZE_OF_MEMBER(NOR_BM_LSH, eccStatData)
             - OFFSET_OF_MEMBER(NOR_BM_LSH, IsValid);        // First member
#else
    NumBytes = 0u
             + OFFSET_OF_MEMBER(NOR_BM_LSH, IsValid)         // Last member
             + SIZE_OF_MEMBER(NOR_BM_LSH, IsValid)
             - OFFSET_OF_MEMBER(NOR_BM_LSH, IsValid);        // First member
#endif // FS_NOR_LINE_SIZE == 1
    break;
  case 2:
#if (FS_NOR_LINE_SIZE == 1)
    NumBytes = 0u
             + OFFSET_OF_MEMBER(NOR_BM_LSH, IsInvalid)       // Last member
             + SIZE_OF_MEMBER(NOR_BM_LSH, IsInvalid)
             - OFFSET_OF_MEMBER(NOR_BM_LSH, IsInvalid);      // First member
#elif (FS_NOR_LOG_SECTOR_RESERVE_EX >= SIZEOF_ECC_LSH_EX)
    NumBytes = 0u
             + OFFSET_OF_MEMBER(NOR_BM_LSH, eccStatInvalid)  // Last member
             + SIZE_OF_MEMBER(NOR_BM_LSH, eccStatInvalid)
             - OFFSET_OF_MEMBER(NOR_BM_LSH, IsInvalid);      // First member
#else
    NumBytes = 0u
             + OFFSET_OF_MEMBER(NOR_BM_LSH, IsInvalid)       // Last member
             + SIZE_OF_MEMBER(NOR_BM_LSH, IsInvalid)
             - OFFSET_OF_MEMBER(NOR_BM_LSH, IsInvalid);      // First member
#endif // FS_NOR_LINE_SIZE == 1
    break;
#endif // FS_NOR_CAN_REWRITE == 0
  default:
    //
    // Error, invalid section index.
    //
    break;
  }
  return NumBytes;
}

/*********************************************************************
*
*       _CalcSectionSizePSH
*
*  Function description
*    Calculates the actual number of bytes stored in a section
*    of a physical sector header.
*
*  Parameters
*    SectionIndex   Index of the PSH section.
*
*  Return value
*    Number of bytes in the section.
*
*  Additional information
*    The physical sector header contains only one section (with index 0)
*    when the sources are compiled with FS_NOR_CAN_REWRITE set to 1 (default).
*    If the sources are compiled with FS_NOR_CAN_REWRITE set to 0 then
*    the header of the physical sector contains 4 sections:
*      Section 0: Contains data type, erase count, etc.
*      Section 1: Indicates that the phy. sector stores a work block.
*      Section 2: Indicates that the phy. sector stores a data block.
*      Section 3: Indicates that the phy. sector contains invalid data.
*/
static U32 _CalcSectionSizePSH(unsigned SectionIndex) {
  U32 NumBytes;

  //
  // Sanity checking.
  //
#if (FS_NOR_CAN_REWRITE == 0)
  FS_DEBUG_ASSERT(FS_MTYPE_DRIVER, SectionIndex <= 3u);
#else
  FS_DEBUG_ASSERT(FS_MTYPE_DRIVER, SectionIndex == 0u);
#endif
  //
  // Calculate the section size by its index.
  //
  NumBytes = 0;
  switch (SectionIndex) {
  case 0:
#if (FS_NOR_PHY_SECTOR_RESERVE < SIZEOF_ERASE_SIGNATURE)
    NumBytes = 0u
             + OFFSET_OF_MEMBER(NOR_BM_PSH, EraseCnt)              // Last member
             + SIZE_OF_MEMBER(NOR_BM_PSH, EraseCnt)
             - OFFSET_OF_MEMBER(NOR_BM_PSH, DataStat);             // First member
#elif (FS_NOR_PHY_SECTOR_RESERVE < (SIZEOF_ERASE_SIGNATURE + SIZEOF_CRC_PSH))
    NumBytes = 0u
             + OFFSET_OF_MEMBER(NOR_BM_PSH, EraseSignature)        // Last member
             + SIZE_OF_MEMBER(NOR_BM_PSH, EraseSignature)
             - OFFSET_OF_MEMBER(NOR_BM_PSH, DataStat);             // First member
#elif (FS_NOR_PHY_SECTOR_RESERVE < (SIZEOF_ERASE_SIGNATURE + SIZEOF_CRC_PSH + SIZEOF_ECC_PSH))
    NumBytes = 0u
             + OFFSET_OF_MEMBER(NOR_BM_PSH, crcStat)               // Last member
             + SIZE_OF_MEMBER(NOR_BM_PSH, crcStat)
             - OFFSET_OF_MEMBER(NOR_BM_PSH, DataStat);             // First member
#elif (FS_NOR_PHY_SECTOR_RESERVE < (SIZEOF_ERASE_SIGNATURE + SIZEOF_CRC_PSH + SIZEOF_ECC_PSH_ALIGNED + 4))
    NumBytes = 0u
             + OFFSET_OF_MEMBER(NOR_BM_PSH, ecc1Stat)              // Last member
             + SIZE_OF_MEMBER(NOR_BM_PSH, ecc1Stat)
             - OFFSET_OF_MEMBER(NOR_BM_PSH, DataStat);             // First member
#else
    NumBytes = 0u
             + OFFSET_OF_MEMBER(NOR_BM_PSH, abReservedPSH)         // Last member
             + SIZE_OF_MEMBER(NOR_BM_PSH, abReservedPSH)
             - OFFSET_OF_MEMBER(NOR_BM_PSH, DataStat);             // First member
#endif
    break;
#if (FS_NOR_CAN_REWRITE == 0)
  case 1:
#if (FS_NOR_LINE_SIZE == 1)
    NumBytes = 0u
             + OFFSET_OF_MEMBER(NOR_BM_PSH, IsWork)                 // Last member
             + SIZE_OF_MEMBER(NOR_BM_PSH, IsWork)
             - OFFSET_OF_MEMBER(NOR_BM_PSH, IsWork);                // First member
#elif (FS_NOR_PHY_SECTOR_RESERVE_EX < SIZEOF_CRC_PSH_EX)
    NumBytes = 0u
             + OFFSET_OF_MEMBER(NOR_BM_PSH, IsWork)                 // Last member
             + SIZE_OF_MEMBER(NOR_BM_PSH, IsWork)
             - OFFSET_OF_MEMBER(NOR_BM_PSH, IsWork);                // First member
#elif (FS_NOR_PHY_SECTOR_RESERVE_EX < (SIZEOF_CRC_PSH_EX + SIZEOF_ECC_PSH_EX))
    NumBytes = 0u
             + OFFSET_OF_MEMBER(NOR_BM_PSH, crcWork)                // Last member
             + SIZE_OF_MEMBER(NOR_BM_PSH, crcWork)
             - OFFSET_OF_MEMBER(NOR_BM_PSH, IsWork);                // First member
#elif (FS_NOR_PHY_SECTOR_RESERVE_EX < (SIZEOF_CRC_PSH_EX + SIZEOF_ECC_PSH_ALIGNED_EX + 4))
    NumBytes = 0u
             + OFFSET_OF_MEMBER(NOR_BM_PSH, eccStatWork)            // Last member
             + SIZE_OF_MEMBER(NOR_BM_PSH, eccStatWork)
             - OFFSET_OF_MEMBER(NOR_BM_PSH, IsWork);                // First member
#else
    NumBytes = 0u
             + OFFSET_OF_MEMBER(NOR_BM_PSH, abReservedWork)         // Last member
             + SIZE_OF_MEMBER(NOR_BM_PSH, abReservedWork)
             - OFFSET_OF_MEMBER(NOR_BM_PSH, IsWork);                // First member
#endif // FS_NOR_LINE_SIZE == 1
    break;
  case 2:
#if (FS_NOR_LINE_SIZE == 1)
    NumBytes = 0u
             + OFFSET_OF_MEMBER(NOR_BM_PSH, IsValid)                // Last member
             + SIZE_OF_MEMBER(NOR_BM_PSH, IsValid)
             - OFFSET_OF_MEMBER(NOR_BM_PSH, IsValid);               // First member
#elif (FS_NOR_PHY_SECTOR_RESERVE_EX < SIZEOF_CRC_PSH_EX)
    NumBytes = 0u
             + OFFSET_OF_MEMBER(NOR_BM_PSH, IsValid)                // Last member
             + SIZE_OF_MEMBER(NOR_BM_PSH, IsValid)
             - OFFSET_OF_MEMBER(NOR_BM_PSH, IsValid);               // First member
#elif (FS_NOR_PHY_SECTOR_RESERVE_EX < (SIZEOF_CRC_PSH_EX + SIZEOF_ECC_PSH_EX))
    NumBytes = 0u
             + OFFSET_OF_MEMBER(NOR_BM_PSH, crcData)                // Last member
             + SIZE_OF_MEMBER(NOR_BM_PSH, crcData)
             - OFFSET_OF_MEMBER(NOR_BM_PSH, IsValid);               // First member
#elif (FS_NOR_PHY_SECTOR_RESERVE_EX < (SIZEOF_CRC_PSH_EX + SIZEOF_ECC_PSH_ALIGNED_EX + 4))
    NumBytes = 0u
             + OFFSET_OF_MEMBER(NOR_BM_PSH, eccStatData)            // Last member
             + SIZE_OF_MEMBER(NOR_BM_PSH, eccStatData)
             - OFFSET_OF_MEMBER(NOR_BM_PSH, IsValid);               // First member
#else
    NumBytes = 0u
             + OFFSET_OF_MEMBER(NOR_BM_PSH, abReservedData)         // Last member
             + SIZE_OF_MEMBER(NOR_BM_PSH, abReservedData)
             - OFFSET_OF_MEMBER(NOR_BM_PSH, IsValid);               // First member
#endif
    break;
  case 3:
#if (FS_NOR_LINE_SIZE == 1)
    NumBytes = 0u
             + OFFSET_OF_MEMBER(NOR_BM_PSH, IsInvalid)              // Last member
             + SIZE_OF_MEMBER(NOR_BM_PSH, IsInvalid)
             - OFFSET_OF_MEMBER(NOR_BM_PSH, IsInvalid);             // First member
#elif (FS_NOR_PHY_SECTOR_RESERVE_EX >= SIZEOF_ECC_PSH_EX)
    NumBytes = 0u
             + OFFSET_OF_MEMBER(NOR_BM_PSH, eccStatInvalid)         // Last member
             + SIZE_OF_MEMBER(NOR_BM_PSH, eccStatInvalid)
             - OFFSET_OF_MEMBER(NOR_BM_PSH, IsInvalid);             // First member
#else
    NumBytes = 0u
             + OFFSET_OF_MEMBER(NOR_BM_PSH, IsInvalid)              // Last member
             + SIZE_OF_MEMBER(NOR_BM_PSH, IsInvalid)
             - OFFSET_OF_MEMBER(NOR_BM_PSH, IsInvalid);             // First member
#endif // FS_NOR_LINE_SIZE == 1
    break;
#endif // FS_NOR_CAN_REWRITE == 0
  default:
    //
    // Error, invalid section index.
    //
    break;
  }
  return NumBytes;
}

/*********************************************************************
*
*       _CalcUpdateSizeOfLSH
*
*  Function description
*    Calculates and updates to driver instance the size of the logical header.
*/
static void _CalcUpdateSizeOfLSH(NOR_BM_INST * pInst) {
  unsigned NumBytes;
  unsigned BytesPerLine;
  unsigned SectionIndex;

  NumBytes = sizeof(NOR_BM_LSH);
  BytesPerLine = 1uL << pInst->ldBytesPerLine;
  if (BytesPerLine < (unsigned)FS_NOR_LINE_SIZE) {
    SectionIndex = 0;
    NumBytes = _CalcSectionSizeLSH(SectionIndex++);
    NumBytes = ALIGN_TO_BOUNDARY(NumBytes, BytesPerLine);
#if (FS_NOR_CAN_REWRITE == 0)
    if (pInst->IsRewriteSupported == 0u) {
      NumBytes += _CalcSectionSizeLSH(SectionIndex++);
      NumBytes  = ALIGN_TO_BOUNDARY(NumBytes, BytesPerLine);
      NumBytes += _CalcSectionSizeLSH(SectionIndex);
      NumBytes  = ALIGN_TO_BOUNDARY(NumBytes, BytesPerLine);
    }
#endif
  }
  pInst->SizeOfLSH = (U8)NumBytes;
}

/*********************************************************************
*
*       _CalcUpdateSizeOfPSH
*
*  Function description
*    Calculates and updates to driver instance the size of the physical header.
*/
static void _CalcUpdateSizeOfPSH(NOR_BM_INST * pInst) {
  unsigned NumBytes;
  unsigned BytesPerLine;
  unsigned SectionIndex;

  NumBytes = sizeof(NOR_BM_PSH);
  BytesPerLine = 1uL << pInst->ldBytesPerLine;
  if (BytesPerLine < (unsigned)FS_NOR_LINE_SIZE) {
    SectionIndex = 0;
    NumBytes = _CalcSectionSizePSH(SectionIndex++);
    NumBytes = ALIGN_TO_BOUNDARY(NumBytes, BytesPerLine);
#if (FS_NOR_CAN_REWRITE == 0)
    if (pInst->IsRewriteSupported == 0u) {
      NumBytes += _CalcSectionSizePSH(SectionIndex++);
      NumBytes  = ALIGN_TO_BOUNDARY(NumBytes, BytesPerLine);
      NumBytes += _CalcSectionSizePSH(SectionIndex++);
      NumBytes  = ALIGN_TO_BOUNDARY(NumBytes, BytesPerLine);
      NumBytes += _CalcSectionSizePSH(SectionIndex);
      NumBytes  = ALIGN_TO_BOUNDARY(NumBytes, BytesPerLine);
    }
#endif
  }
  pInst->SizeOfPSH = (U8)NumBytes;
}

/*********************************************************************
*
*       _EncodeLSH
*/
static U32 _EncodeLSH(const NOR_BM_INST * pInst, const NOR_BM_LSH * pLSH, U8 * pData) {
  U32      NumBytesToCopy;
  U32      BytesPerLine;
  unsigned SectionIndex;
  U32      NumBytes;

  NumBytes     = 0;
  BytesPerLine = 1uL << pInst->ldBytesPerLine;
  if (BytesPerLine < (unsigned)FS_NOR_LINE_SIZE) {
    FS_MEMSET(pData, 0xFF, sizeof(NOR_BM_LSH));
    SectionIndex   = 0;
    NumBytesToCopy = _CalcSectionSizeLSH(SectionIndex++);
    FS_MEMCPY(pData + NumBytes, &pLSH->DataStat, NumBytesToCopy);
    NumBytes += NumBytesToCopy;
    NumBytes  = ALIGN_TO_BOUNDARY(NumBytes, BytesPerLine);
#if (FS_NOR_CAN_REWRITE == 0)
    if (pInst->IsRewriteSupported == 0u) {
      NumBytesToCopy = _CalcSectionSizeLSH(SectionIndex++);
      FS_MEMCPY(pData + NumBytes, &pLSH->IsValid, NumBytesToCopy);
      NumBytes += NumBytesToCopy;
      NumBytes  = ALIGN_TO_BOUNDARY(NumBytes, BytesPerLine);
      NumBytesToCopy = _CalcSectionSizeLSH(SectionIndex);
      FS_MEMCPY(pData + NumBytes, &pLSH->IsInvalid, NumBytesToCopy);
      NumBytes += NumBytesToCopy;
      NumBytes  = ALIGN_TO_BOUNDARY(NumBytes, BytesPerLine);
    }
#endif // FS_NOR_CAN_REWRITE == 0
  }
  return NumBytes;
}

/*********************************************************************
*
*       _DecodeLSH
*/
static U32 _DecodeLSH(const NOR_BM_INST * pInst, NOR_BM_LSH * pLSH, const U8 * pData) {
  U32      NumBytesToCopy;
  U32      BytesPerLine;
  unsigned SectionIndex;
  U32      NumBytes;

  NumBytes     = 0;
  BytesPerLine = 1uL << pInst->ldBytesPerLine;
  if (BytesPerLine < (unsigned)FS_NOR_LINE_SIZE) {
    FS_MEMSET(pLSH, 0xFF, sizeof(NOR_BM_LSH));
    SectionIndex   = 0;
    NumBytesToCopy = _CalcSectionSizeLSH(SectionIndex++);
    FS_MEMCPY(&pLSH->DataStat, pData + NumBytes, NumBytesToCopy);
    NumBytes += NumBytesToCopy;
    NumBytes  = ALIGN_TO_BOUNDARY(NumBytes, BytesPerLine);
#if (FS_NOR_CAN_REWRITE == 0)
    if (pInst->IsRewriteSupported == 0u) {
      NumBytesToCopy = _CalcSectionSizeLSH(SectionIndex++);
      FS_MEMCPY(&pLSH->IsValid, pData + NumBytes, NumBytesToCopy);
      NumBytes += NumBytesToCopy;
      NumBytes  = ALIGN_TO_BOUNDARY(NumBytes, BytesPerLine);
      NumBytesToCopy = _CalcSectionSizeLSH(SectionIndex);
      FS_MEMCPY(&pLSH->IsInvalid, pData + NumBytes, NumBytesToCopy);
      NumBytes += NumBytesToCopy;
      NumBytes  = ALIGN_TO_BOUNDARY(NumBytes, BytesPerLine);
    }
#endif // FS_NOR_CAN_REWRITE == 0
  }
  return NumBytes;
}

/*********************************************************************
*
*       _EncodePSH
*/
static U32 _EncodePSH(const NOR_BM_INST * pInst, const NOR_BM_PSH * pPSH, U8 * pData) {
  U32      NumBytesToCopy;
  U32      BytesPerLine;
  unsigned SectionIndex;
  U32      NumBytes;

  NumBytes     = 0;
  BytesPerLine = 1uL << pInst->ldBytesPerLine;
  if (BytesPerLine < (unsigned)FS_NOR_LINE_SIZE) {
    FS_MEMSET(pData, 0xFF, sizeof(NOR_BM_PSH));
    SectionIndex   = 0;
    NumBytesToCopy = _CalcSectionSizePSH(SectionIndex++);
    FS_MEMCPY(pData + NumBytes, &pPSH->DataStat, NumBytesToCopy);
    NumBytes += NumBytesToCopy;
    NumBytes  = ALIGN_TO_BOUNDARY(NumBytes, BytesPerLine);
#if (FS_NOR_CAN_REWRITE == 0)
    if (pInst->IsRewriteSupported == 0u) {
      NumBytesToCopy = _CalcSectionSizePSH(SectionIndex++);
      FS_MEMCPY(pData + NumBytes, &pPSH->IsWork, NumBytesToCopy);
      NumBytes += NumBytesToCopy;
      NumBytes  = ALIGN_TO_BOUNDARY(NumBytes, BytesPerLine);
      NumBytesToCopy = _CalcSectionSizePSH(SectionIndex++);
      FS_MEMCPY(pData + NumBytes, &pPSH->IsValid, NumBytesToCopy);
      NumBytes += NumBytesToCopy;
      NumBytes  = ALIGN_TO_BOUNDARY(NumBytes, BytesPerLine);
      NumBytesToCopy = _CalcSectionSizePSH(SectionIndex);
      FS_MEMCPY(pData + NumBytes, &pPSH->IsInvalid, NumBytesToCopy);
      NumBytes += NumBytesToCopy;
      NumBytes  = ALIGN_TO_BOUNDARY(NumBytes, BytesPerLine);
    }
#endif // FS_NOR_CAN_REWRITE == 0
  }
  return NumBytes;
}

/*********************************************************************
*
*       _DecodePSH
*/
static U32 _DecodePSH(const NOR_BM_INST * pInst, NOR_BM_PSH * pPSH, const U8 * pData) {
  U32      NumBytesToCopy;
  U32      BytesPerLine;
  unsigned SectionIndex;
  U32      NumBytes;

  NumBytes     = 0;
  BytesPerLine = 1uL << pInst->ldBytesPerLine;
  if (BytesPerLine < (unsigned)FS_NOR_LINE_SIZE) {
    FS_MEMSET(pPSH, 0xFF, sizeof(NOR_BM_PSH));
    SectionIndex   = 0;
    NumBytesToCopy = _CalcSectionSizePSH(SectionIndex++);
    FS_MEMCPY(&pPSH->DataStat, pData + NumBytes, NumBytesToCopy);
    NumBytes += NumBytesToCopy;
    NumBytes  = ALIGN_TO_BOUNDARY(NumBytes, BytesPerLine);
#if (FS_NOR_CAN_REWRITE == 0)
    if (pInst->IsRewriteSupported == 0u) {
      NumBytesToCopy = _CalcSectionSizePSH(SectionIndex++);
      FS_MEMCPY(&pPSH->IsWork, pData + NumBytes, NumBytesToCopy);
      NumBytes += NumBytesToCopy;
      NumBytes  = ALIGN_TO_BOUNDARY(NumBytes, BytesPerLine);
      NumBytesToCopy = _CalcSectionSizePSH(SectionIndex++);
      FS_MEMCPY(&pPSH->IsValid, pData + NumBytes, NumBytesToCopy);
      NumBytes += NumBytesToCopy;
      NumBytes  = ALIGN_TO_BOUNDARY(NumBytes, BytesPerLine);
      NumBytesToCopy = _CalcSectionSizePSH(SectionIndex);
      FS_MEMCPY(&pPSH->IsInvalid, pData + NumBytes, NumBytesToCopy);
      NumBytes += NumBytesToCopy;
      NumBytes  = ALIGN_TO_BOUNDARY(NumBytes, BytesPerLine);
    }
#endif // FS_NOR_CAN_REWRITE == 0
  }
  return NumBytes;
}

#endif // FS_NOR_SUPPORT_VARIABLE_LINE_SIZE

#if (FS_NOR_CAN_REWRITE == 0) || (FS_NOR_SUPPORT_CRC != 0)  || (FS_NOR_SUPPORT_ECC != 0)

/*********************************************************************
*
*       _IsRewriteSupported
*/
static int _IsRewriteSupported(const NOR_BM_INST * pInst) {
  int r;

  r = FS_NOR_CAN_REWRITE;
#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  if (pInst->IsRewriteSupported != 0u) {
    r = 1;
  }
#else
  FS_USE_PARA(pInst);
#endif // FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  return r;
}

#endif // FS_NOR_CAN_REWRITE == 0)

#if FS_NOR_OPTIMIZE_HEADER_WRITE

/*********************************************************************
*
*       _InitDataRange
*/
static void _InitDataRange(NOR_BM_DATA_RANGE * pDataRange) {
  pDataRange->OffStart = (U16)~0u;
  pDataRange->OffEnd   = 0;
}

/*********************************************************************
*
*       _UpdateDataRange
*
*  Additional information
*    NumBytes have to be larger than 0.
*/
static void _UpdateDataRange(NOR_BM_DATA_RANGE * pDataRange, unsigned Off, unsigned NumBytes) {
  unsigned OffEnd;

  if (Off < pDataRange->OffStart) {
    pDataRange->OffStart = (U16)Off;
  }
  OffEnd = Off + NumBytes - 1u;
  if (OffEnd > pDataRange->OffEnd) {
    pDataRange->OffEnd = (U16)OffEnd;
  }
}

/*********************************************************************
*
*       _CalcDataRange
*
*  Function description
*    Calculates the byte range of the physical sector header that
*    has to be written to storage.
*
*  Parameters
*    pInst      Driver instance.
*    pDataRange Range of data written by the driver relative to the begin of the header.
*    ppData     [IN]  Data requested to be written.
*               [OUT] Data that has to be written.
*               Cannot be NULL.
*    pOff       [IN]  Absolute byte offset of the physical sector.
*               [OUT] Absolute byte offset of the first byte to be stored.
*               Cannot be NULL.
*    pNumBytes  [IN]  Size of the physical sector header in bytes.
*               [OUT] Number of bytes to be written to storage.
*               Cannot be NULL.
*
*  Additional information
*    The returned range of bytes is aligned to flash line boundary.
*/
static void _CalcDataRange(const NOR_BM_INST * pInst, const NOR_BM_DATA_RANGE * pDataRange, const U8 ** ppData, U32 * pOff, unsigned * pNumBytes) {
  U32        Off;
  unsigned   BytesPerLine;
  unsigned   NumBytes;
  unsigned   NumBytesCalc;
  U32        OffCalc;
  const U8 * pData;
  unsigned   OffStart;
  unsigned   OffEnd;

#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  BytesPerLine = 1uL << pInst->ldBytesPerLine;
#else
  FS_USE_PARA(pInst);
  BytesPerLine = FS_NOR_LINE_SIZE;
#endif // FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  pData    = *ppData;
  Off      = *pOff;
  NumBytes = *pNumBytes;
  OffStart = pDataRange->OffStart;
  OffEnd   = pDataRange->OffEnd;
  if (OffEnd >= OffStart) {
    NumBytesCalc = (OffEnd - OffStart) + 1u;
    //
    // Align offset to flash line boundary.
    //
    OffCalc       = OffStart & ~(BytesPerLine - 1u);
    NumBytesCalc += OffStart & (BytesPerLine - 1u);
    //
    // Align number of bytes to flash line boundary.
    //
    NumBytesCalc  = ALIGN_TO_BOUNDARY(NumBytesCalc, BytesPerLine);
    if (NumBytesCalc < NumBytes) {
      pData    += OffCalc;
      Off      += OffCalc;
      NumBytes  = NumBytesCalc;
    }
  }
  //
  // Return the calculated values.
  //
  *ppData    = pData;
  *pOff      = Off;
  *pNumBytes = NumBytes;
}

#if FS_SUPPORT_TEST

/*********************************************************************
*
*       _InitVerify
*/
static void _InitVerify(const U8 * pData, U32 Off, unsigned NumBytes) {
  _pVerifyData    = pData;
  _VerifyOff      = Off;
  _VerifyNumBytes = NumBytes;
}

#endif // FS_SUPPORT_TEST

#endif // FS_NOR_OPTIMIZE_HEADER_WRITE

/*********************************************************************
*
*       _UseFreeMem
*/
static U32 * _UseFreeMem(I32 * pNumBytes) {
  U32 * p;

  p          = NULL;
  *pNumBytes = 0;
  if (_IsFreeMemInUse == 0u) {
    p = SEGGER_PTR2PTR(U32, FS_GetFreeMem(pNumBytes));                    // MISRA deviation D:100d
    if (*pNumBytes == 0) {
      p = NULL;
    } else {
      _IsFreeMemInUse = 1;
    }
  }
  return p;
}

/*********************************************************************
*
*       _UnuseFreeMem
*/
static void _UnuseFreeMem(I32 NumBytes) {
  if (NumBytes != 0) {
    _IsFreeMemInUse = 0;
  }
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
*    Data       Value of the byte.
*    FirstBit   Position of the first bit to check (0-based).
*    LastBit    Position of the last bit to check (0-based).
*    Off        Byte offset to be added to the found bit position.
*
*  Return value
*    >= 0 On Success, Bit position of first 0.
*    -1   On Error, No 0-bit found.
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
*    >= 0 On Success, Bit position of first 0
*    -1   On Error, No 0-bit found
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
    BitPos = FirstOff << 3;
    return r + (int)BitPos;
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
*  Function description
*    Computes the default number of work blocks.
*    This is a percentage of number of NOR physical sectors.
*/
static U32 _CalcNumWorkBlocksDefault(U32 NumPhyBlocks) {
  U32 NumWorkBlocks;

  //
  // Allocate 1% of NOR capacity for work blocks
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
  return NumWorkBlocks;
}

/*********************************************************************
*
*       _CalcNumBlocksToUse
*
*  Function description
*    Computes the number of logical blocks available to file system.
*/
static int _CalcNumBlocksToUse(U32 NumPhyBlocks, U32 NumWorkBlocks) {
  int NumLogBlocks;
  int Reserve;

  //
  // Compute the number of logical blocks. These are the blocks that are
  // actually available to the file system for data storage and therefore
  // determine the capacity. We reserve some sectors for driver management:
  // the number of work blocks + 1 info block (first block) + 1 block for copy operations
  //
  NumLogBlocks  = (int)NumPhyBlocks;
  Reserve       = (int)NumWorkBlocks + 2;
  NumLogBlocks -= Reserve;
  return NumLogBlocks;
}

/*********************************************************************
*
*       _FindSectorRangeToUse
*
*  Function description
*    Searches for the longest continuous range of physical sectors with the same size.
*
*  Parameters
*    pInst              Driver instance.
*    pFirstPhySector    [OUT] Index of the first physical sector to be used as storage.
*    pPhySectorSize     [OUT] Size in bytes of the physical sectors to be used as storage.
*
*  Return value
*    !=0    OK, number of the physical sectors to be used as storage.
*    ==0    An error occurred.
*
*  Additional information
*    Typ. called at driver initialization to determine which physical sectors should be used as storage.
*/
static unsigned _FindSectorRangeToUse(const NOR_BM_INST * pInst, unsigned * pFirstPhySector, U32 * pPhySectorSize) {
  int      NumPhySectors;
  U32      PhySectorSize;
  U32      NumPhySectorsToUse;
  U32      NumPhySectorsInRange;
  unsigned FirstPhySector;
  U32      PhySectorIndex;
  U32      NumBytesPrev;
  U32      NumBytes;

  NumPhySectors = pInst->pPhyType->pfGetNumSectors(pInst->Unit);
  if (NumPhySectors <= 0) {
    return 0;                                                     // Error, could not get the number of physical sectors.
  }
  //
  // Search for the longest sequence of sectors with the same size
  //
  NumBytesPrev         = 0;
  FirstPhySector       = 0;
  PhySectorSize        = 0;
  NumPhySectorsToUse   = 0;
  NumPhySectorsInRange = 0;
  PhySectorIndex       = 0;
  do {
    NumBytes = 0;
    pInst->pPhyType->pfGetSectorInfo(pInst->Unit, PhySectorIndex, NULL, &NumBytes);
    if (NumBytes == 0u) {
      return 0;                                                   // Error, could not get information about the physical sector.
    }
    if (NumBytes != NumBytesPrev) {                               // New physical sector range ?
      if (NumPhySectorsInRange > NumPhySectorsToUse) {
        FirstPhySector     = PhySectorIndex - NumPhySectorsInRange;
        PhySectorSize      = NumBytesPrev;
        NumPhySectorsToUse = NumPhySectorsInRange;
      }
      NumPhySectorsInRange = 0;
    }
    ++NumPhySectorsInRange;
    ++PhySectorIndex;
    NumBytesPrev = NumBytes;
  } while (--NumPhySectors != 0);
  if (NumPhySectorsInRange > NumPhySectorsToUse) {
    FirstPhySector     = PhySectorIndex - NumPhySectorsInRange;
    PhySectorSize      = NumBytesPrev;
    NumPhySectorsToUse = NumPhySectorsInRange;
  }
  if (NumPhySectorsToUse == 0u) {
    PhySectorSize      = NumBytesPrev;
    NumPhySectorsToUse = NumPhySectorsInRange;
  }
  if (pFirstPhySector != NULL) {
    *pFirstPhySector = FirstPhySector;
  }
  if (pPhySectorSize != NULL) {
    *pPhySectorSize  = PhySectorSize;
  }
  return NumPhySectorsToUse;
}

/*********************************************************************
*
*       _IsCRCEnabled
*/
static int _IsCRCEnabled(const NOR_BM_INST * pInst) {
  int r;

  FS_USE_PARA(pInst);
  r = 0;              // CRC is disabled by default.
#if FS_NOR_SUPPORT_CRC
  if (_pCRC_API != NULL) {
    r = 1;            // CRC is enabled.
  }
#endif // FS_NOR_SUPPORT_CRC
  return r;
}

/*********************************************************************
*
*       _IsECCEnabled
*/
static int _IsECCEnabled(const NOR_BM_INST * pInst) {
  int r;

  r = 0;              // The ECC feature is disabled by default.
#if FS_NOR_SUPPORT_ECC
  if (pInst->pECC_API != NULL) {
    r = 1;            // The ECC feature is enabled.
  }
#else
  FS_USE_PARA(pInst);
#endif // FS_NOR_SUPPORT_ECC
  return r;
}

/*********************************************************************
*
*        _SizeOfPSH
*/
static unsigned _SizeOfPSH(const NOR_BM_INST * pInst) {
  unsigned NumBytes;

#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  NumBytes = pInst->SizeOfPSH;
#else
  FS_USE_PARA(pInst);
  NumBytes = sizeof(NOR_BM_PSH);
#endif
  return NumBytes;
}

/*********************************************************************
*
*        _SizeOfLSH
*/
static unsigned _SizeOfLSH(const NOR_BM_INST * pInst) {
  unsigned NumBytes;

#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  NumBytes = pInst->SizeOfLSH;
#else
  FS_USE_PARA(pInst);
  NumBytes = sizeof(NOR_BM_LSH);
#endif
  return NumBytes;
}

/*********************************************************************
*
*       _CalcLSectorsPerPSector
*
*  Function description
*    Computes the number of logical sectors that fit in a physical sector.
*/
static unsigned _CalcLSectorsPerPSector(const NOR_BM_INST * pInst, U32 PhySectorSize, U32 LogSectorSize) {
  unsigned SizeOfLSH;
  unsigned SizeOfPSH;

  SizeOfLSH = _SizeOfLSH(pInst);
  SizeOfPSH = _SizeOfPSH(pInst);
  return (PhySectorSize - SizeOfPSH) / (SizeOfLSH + LogSectorSize);
}

/*********************************************************************
*
*       _InitDevice
*
*  Function description
*    Initializes the physical layer.
*
*  Return value
*    ==0    O.K.
*    !=0    Error, Could not initialize.
*
*  Additional information
*    For historical reasons, the initialization function of the physical
*    layer is optional.
*/
static int _InitDevice(const NOR_BM_INST * pInst) {
  U8  Unit;
  int r;

  r    = 0;             // Set to indicate success.
  Unit = pInst->Unit;
  if (pInst->pPhyType->pfInit != NULL) {
    r = pInst->pPhyType->pfInit(Unit);
  }
  return r;
}

/*********************************************************************
*
*       _ReadApplyDeviceParas
*
*  Function description
*    Reads the device info and computes the parameters stored in the instance structure
*    such as number of logical blocks, number of logical sectors etc.
*
*  Return value
*    ==0    O.K.
*    !=0    Error, Could not apply device paras
*/
static int _ReadApplyDeviceParas(NOR_BM_INST * pInst) {
  unsigned NumPhySectors;
  unsigned NumWorkBlocks;
  int      NumLogBlocks;
  U32      BytesPerSector;
  unsigned LSectorsPerPSector;
  U32      PhySectorSize;
  unsigned FirstPhySector;
  int      r;

  //
  // Initialize the physical layer.
  //
  r = _InitDevice(pInst);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_BM: Could not initialize device."));
    return 1;
  }
  //
  // Determine the range of physical sectors to be used by the driver
  //
  NumPhySectors = _FindSectorRangeToUse(pInst, &FirstPhySector, &PhySectorSize);
  if (NumPhySectors == 0u) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_BM: No physical sectors found."));
    return 1;
  }
  //
  // Compute a default number of work blocks if the application did not configured it yet.
  //
  if (pInst->NumWorkBlocksConf == 0u) {
    NumWorkBlocks = _CalcNumWorkBlocksDefault(NumPhySectors);
  } else {
    NumWorkBlocks = pInst->NumWorkBlocksConf;
  }
  //
  // Compute the number of logical blocks available to file system
  //
  NumLogBlocks = _CalcNumBlocksToUse(NumPhySectors, NumWorkBlocks);
  if (NumLogBlocks <= 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_BM: Insufficient physical sectors."));
    return 1;
  }
  //
  // Set a default sector size if the user did not configured one
  //
  if (pInst->BytesPerSectorConf == 0u) {
    BytesPerSector = FS_Global.MaxSectorSize;
  } else {
    BytesPerSector = pInst->BytesPerSectorConf;
  }
  LSectorsPerPSector = _CalcLSectorsPerPSector(pInst, PhySectorSize, BytesPerSector);
#if FS_NOR_SUPPORT_ECC
  if (_IsECCEnabled(pInst) != 0) {
    unsigned NumBytesECC;
    unsigned NumBlocksECC;

    NumBytesECC  = 1uL << pInst->pECCHookData->ldBytesPerBlock;
    NumBlocksECC = BytesPerSector / NumBytesECC;
    if ((NumBlocksECC == 0u) || (NumBlocksECC > (unsigned)FS_NOR_MAX_NUM_BLOCKS_ECC_DATA)) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_BM: Invalid ECC configuration."));
      return 1;
    }
    pInst->NumBlocksECC = (U8)NumBlocksECC;
    FS_ALLOC_ZEROED_PTR(SEGGER_PTR2PTR(void *, &_pECCBuffer),  (I32)NumBytesECC, "NOR_BM_ECC_BUFFER");      // MISRA deviation D:100d
    if (_pECCBuffer == NULL) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_BM: Could not allocate ECC buffer."));
      return 1;
    }
  }
#endif // FS_NOR_SUPPORT_ECC
  //
  // Store the values into the driver instance
  //
  pInst->NumPhySectors         = (U16)NumPhySectors;
  pInst->PhySectorSize         = PhySectorSize;
  pInst->NumBitsPhySectorIndex = (U8)FS_BITFIELD_CalcNumBitsUsed(NumPhySectors);
  pInst->FirstPhySector        = (U16)FirstPhySector;
  pInst->NumLogBlocks          = (U16)NumLogBlocks;
  pInst->NumWorkBlocks         = (U8)NumWorkBlocks;
  pInst->NumLogSectors         = (U32)NumLogBlocks * LSectorsPerPSector;
  pInst->LSectorsPerPSector    = (U16)LSectorsPerPSector;
  pInst->NumBitsSRSI           = (U8)FS_BITFIELD_CalcNumBitsUsed(LSectorsPerPSector);
  pInst->ldBytesPerSector      = (U16)_ld(BytesPerSector);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR_BM: READ_APPLY_DEV_PARA NumPhySectors: %u, PhySectorSize: %lu,", pInst->NumPhySectors, pInst->PhySectorSize));
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, " FirstPhySector: %u, NumLogBlocks: %u, NumLogSectors: %lu", pInst->FirstPhySector, pInst->NumLogBlocks, pInst->NumLogSectors));
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, " LSectorsPerPSector: %u, LogSectorSize: %u\n", pInst->LSectorsPerPSector, BytesPerSector));
  return 0;                   // O.K., successfully identified
}

/*********************************************************************
*
*       _GetPhySectorInfo
*
*  Function description
*    Returns the size and the offset in bytes of a physical sector.
*
*  Parameters
*    pInst            Driver instance.
*    PhySectorIndex   Index of the physical sector.
*    pOff             [OUT] Offset of the physical sector in bytes. Can be NULL.
*    pNumBytes        [OUT] Size of the physical sector in bytes. Can be NULL.
*
*  Additional information
*    PhySectorIndex is relative to the first physical sector index
*    used as storage.
*/
static void _GetPhySectorInfo(const NOR_BM_INST * pInst, unsigned PhySectorIndex, U32 * pOff, U32 * pNumBytes) {
   PhySectorIndex += pInst->FirstPhySector;
   pInst->pPhyType->pfGetSectorInfo(pInst->Unit, PhySectorIndex, pOff, pNumBytes);
}

/*********************************************************************
*
*       _ReadOff
*
*  Function description
*    Reads data from the given offset of the NOR flash.
*
*  Return value
*    ==0                  OK, data read successfully.
*    ==RESULT_READ_ERROR  Error while reading from NOR flash.
*/
static int _ReadOff(NOR_BM_INST * pInst, void * pData, U32 Off, U32 NumBytes) {
  int r;
  U8  Unit;

  Unit = pInst->Unit;
  CALL_TEST_HOOK_DATA_READ_BEGIN(Unit, pData, &Off, &NumBytes);
  r = pInst->pPhyType->pfReadOff(Unit, pData, Off, NumBytes);
  CALL_TEST_HOOK_DATA_READ_END(Unit, pData, Off, NumBytes, &r);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR_BM: _ReadOff: Read failed Off: 0x%08x, r: %d.", Off, r));
    r = RESULT_READ_ERROR;
  }
  IF_STATS(pInst->StatCounters.ReadCnt++);
  IF_STATS(pInst->StatCounters.ReadByteCnt += NumBytes);
  return r;
}

#if ((FS_NOR_VERIFY_WRITE != 0) || (FS_NOR_VERIFY_ERASE != 0) || (FS_SUPPORT_TEST != 0))

/*********************************************************************
*
*       _ReadOffWithRetry
*
*  Function description
*    Reads data from the given offset of the NOR flash.
*    The operation is retried for a configured number of times
*    if it fails.
*
*  Return value
*    ==0                  OK, data read successfully.
*    ==RESULT_READ_ERROR  Error while reading from NOR flash.
*/
static int _ReadOffWithRetry(NOR_BM_INST * pInst, void * pData, U32 Off, U32 NumBytes) {
  int r;
  int NumRetries;

  NumRetries = FS_NOR_NUM_READ_RETRIES;
  for (;;) {
    r = _ReadOff(pInst, pData, Off, NumBytes);
    if (r == 0) {
      break;                        // OK, data has been read successfully.
    }
    if (NumRetries-- == 0) {
      break;                        // Error, no more read retries.
    }
  }
  return r;
}

#endif // (FS_NOR_VERIFY_WRITE || FS_NOR_VERIFY_ERASE || FS_SUPPORT_TEST)

#if FS_SUPPORT_TEST

/*********************************************************************
*
*        _PreVerifyWrite
*
*  Function description
*    Verifies that no attempt is made to change the value of a bit
*    from 0 to 1. A bit value can be changed from 0 to 1 only by
*    erasing the physical sector.
*/
static int _PreVerifyWrite(NOR_BM_INST * pInst, const void * pData, U32 Off, U32 NumBytes) {
  U32        aBuffer[FS_NOR_DATA_BUFFER_SIZE / 4];
  U32      * pBuffer;
  int        NumRetries;
  unsigned   SizeOfBuffer;
  I32        NumBytesFree;
  unsigned   NumBytesAtOnce;
  U8       * pDataRead;
  const U8 * pDataWritten;
  U8         DataRead;
  U8         DataWritten;
  unsigned   NumBytesToCheck;
  int        r;

  //
  // If possible, use a larger buffer for the read operation to increase performance.
  //
  pBuffer      = _UseFreeMem(&NumBytesFree);
  SizeOfBuffer = sizeof(aBuffer);
  if (pBuffer != NULL) {
    if ((I32)SizeOfBuffer < NumBytesFree) {
      SizeOfBuffer = (U32)NumBytesFree;
    } else {
      pBuffer = NULL;
    }
  }
  if (pBuffer == NULL) {
    pBuffer = aBuffer;
  }
  //
  // Check if all the bytes were correctly written.
  //
  pDataWritten = SEGGER_CONSTPTR2PTR(const U8, pData);                              // MISRA deviation D:100e
  do {
    NumBytesAtOnce = SEGGER_MIN(NumBytes, SizeOfBuffer);
    r = _ReadOffWithRetry(pInst, pBuffer, Off, NumBytesAtOnce);
    if (r != 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR_BM: _PreVerifyWrite: Could not read data."));
      r = RESULT_WRITE_ERROR;            // Error, could not read data.
      goto Done;
    }
    pDataRead       = SEGGER_PTR2PTR(U8, pBuffer);                                  // MISRA deviation D:100e
    NumBytesToCheck = NumBytesAtOnce;
    do {
      DataRead    = *pDataRead;
      DataWritten = *pDataWritten;
      if ((DataRead & DataWritten) != DataWritten) {
        NumRetries = FS_NOR_NUM_READ_RETRIES;
        for (;;) {
          r = _ReadOffWithRetry(pInst, &DataRead, Off, 1);
          if (r != 0) {
            FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR_BM: _PreVerifyWrite: Could not read data."));
            r = RESULT_WRITE_ERROR;       // Error, could not read data.
            goto Done;
          }
          if ((DataRead & DataWritten) == DataWritten) {
            break;                          // OK, the data has been correctly written.
          }
          if (NumRetries-- == 0) {
            FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR_BM: _PreVerifyWrite: Invalid operation Off: 0x%08x, DataWr: 0x%x, DataRd: 0x%x.", Off, DataWritten, DataRead));
            r = RESULT_WRITE_ERROR;       // Error, no more read retries.
            goto Done;
          }
        }
      }
      ++Off;
      ++pDataRead;
      ++pDataWritten;
    } while (--NumBytesToCheck != 0u);
    NumBytes -= NumBytesAtOnce;
  } while (NumBytes != 0u);
Done:
  _UnuseFreeMem(NumBytesFree);
  return r;
}

#endif // FS_SUPPORT_TEST

#if FS_NOR_VERIFY_WRITE

/*********************************************************************
*
*        _VerifyWrite
*
*  Function description
*    Verifies if the data has been written correctly to NOR flash device.
*/
static int _VerifyWrite(NOR_BM_INST * pInst, const void * pData, U32 Off, U32 NumBytes) {
  U32        aBuffer[FS_NOR_DATA_BUFFER_SIZE / 4];
  U32      * pBuffer;
  int        NumRetries;
  unsigned   SizeOfBuffer;
  I32        NumBytesFree;
  unsigned   NumBytesAtOnce;
  U8       * pDataRead;
  const U8 * pDataWritten;
  U8         DataRead;
  U8         DataWritten;
  unsigned   NumBytesToCheck;
  int        r;

  if (NumBytes == 0u) {                     // Can occur during the testing of the driver.
    return 0;
  }
  //
  // If possible, use a larger buffer for the read operation to increase performance.
  //
  pBuffer      = _UseFreeMem(&NumBytesFree);
  SizeOfBuffer = sizeof(aBuffer);
  if (pBuffer != NULL) {
    if ((I32)SizeOfBuffer < NumBytesFree) {
      SizeOfBuffer = (U32)NumBytesFree;
    } else {
      pBuffer = NULL;
    }
  }
  if (pBuffer == NULL) {
    pBuffer = aBuffer;
  }
  //
  // Check if all the bytes were correctly written.
  //
  pDataWritten = SEGGER_CONSTPTR2PTR(const U8, pData);                              // MISRA deviation D:100e
  do {
    NumBytesAtOnce = SEGGER_MIN(NumBytes, SizeOfBuffer);
    r = _ReadOffWithRetry(pInst, pBuffer, Off, NumBytesAtOnce);
    if (r != 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR_BM: _VerifyWrite: Could not read data."));
      r = RESULT_WRITE_ERROR;               // Error, could not read data.
      goto Done;
    }
    pDataRead       = SEGGER_PTR2PTR(U8, pBuffer);                                  // MISRA deviation D:100e
    NumBytesToCheck = NumBytesAtOnce;
    do {
      DataRead    = *pDataRead;
      DataWritten = *pDataWritten;
      if (DataRead != DataWritten) {
        NumRetries = FS_NOR_NUM_READ_RETRIES;
        for (;;) {
          r = _ReadOffWithRetry(pInst, &DataRead, Off, 1);
          if (r != 0) {
            FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR_BM: _VerifyWrite: Could not read data."));
            r = RESULT_WRITE_ERROR;         // Error, could not read data.
            goto Done;
          }
          if (DataRead == DataWritten) {
            break;                          // OK, the data has been correctly written.
          }
          if (NumRetries-- == 0) {
            FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR_BM: _VerifyWrite: Data mismatch Off: 0x%08x, DataWr: 0x%x, DataRd: 0x%x.", Off, DataWritten, DataRead));
            r = RESULT_WRITE_ERROR;         // Error, no more read retries.
            goto Done;
          }
        }
      }
      ++Off;
      ++pDataRead;
      ++pDataWritten;
    } while (--NumBytesToCheck != 0u);
    NumBytes -= NumBytesAtOnce;
  } while (NumBytes != 0u);
Done:
  _UnuseFreeMem(NumBytesFree);
  return r;
}

#endif // FS_NOR_VERIFY_WRITE

/*********************************************************************
*
*        _WriteOff
*
*  Function description
*    Writes data to the NOR flash using the low-level flash driver.
*
*  Return value
*    ==0                    OK, data has been written correctly.
*    ==RESULT_WRITE_ERROR   An error occurred while writing data.
*/
static int _WriteOff(NOR_BM_INST * pInst, const void * pData, U32 Off, U32 NumBytes) {
  int r;
  U8  Unit;

  Unit = pInst->Unit;
#if FS_SUPPORT_TEST
  r = _PreVerifyWrite(pInst, pData, Off, NumBytes);
  if (r != 0) {
    return RESULT_WRITE_ERROR;
  }
#endif // FS_SUPPORT_TEST
  CALL_TEST_HOOK_DATA_WRITE_BEGIN(Unit, &pData, &Off, &NumBytes);
  r =  pInst->pPhyType->pfWriteOff(Unit, Off, pData, NumBytes);
  CALL_TEST_HOOK_DATA_WRITE_END(Unit, pData, Off, NumBytes, &r);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR_BM: _WriteOff: Write failed Off: 0x%08x, r: %d.", Off, r));
    r = RESULT_WRITE_ERROR;
  }
  //
  // On higher debug levels, check if the write succeeded.
  //
#if FS_NOR_VERIFY_WRITE
  if (r == 0) {
    if (pInst->VerifyWrite != 0u) {
      r = _VerifyWrite(pInst, pData, Off, NumBytes);
    }
  }
#endif // FS_NOR_VERIFY_WRITE
  IF_STATS(pInst->StatCounters.WriteCnt++);
  IF_STATS(pInst->StatCounters.WriteByteCnt += NumBytes);
  return r;
}

#if FS_NOR_VERIFY_ERASE

/*********************************************************************
*
*        _VerifyErase
*
*  Function description
*    Verifies if all the bytes in a phy. sector are set to 0xFF.
*/
static int _VerifyErase(NOR_BM_INST * pInst, U32 PhySectorIndex) {
  U32        Off;
  U32        aBuffer[FS_NOR_DATA_BUFFER_SIZE / 4];
  U32        NumItems;
  U32        NumBytes;
  U32      * pBuffer;
  int        NumRetries;
  unsigned   SizeOfBuffer;
  I32        NumBytesFree;
  U32        Data32;
  U8         Unit;
  U32      * pData32;
  unsigned   NumBytesAtOnce;
  int        r;

  NumBytes = 0;
  Off      = 0;
  Unit     = pInst->Unit;
  pInst->pPhyType->pfGetSectorInfo(Unit, PhySectorIndex, &Off, &NumBytes);
  if (NumBytes == 0u) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR_BM: _VerifyErase: Could not read phy. sector info."));
    return RESULT_ERASE_ERROR;                // Phy. sector is not blank.
  }
  //
  // If possible, use a larger buffer for the read operation to increase performance.
  //
  pBuffer      = _UseFreeMem(&NumBytesFree);
  SizeOfBuffer = sizeof(aBuffer);
  if (pBuffer != NULL) {
    if ((I32)SizeOfBuffer < NumBytesFree) {
      SizeOfBuffer = (U32)NumBytesFree;
    } else {
      pBuffer = NULL;
    }
  }
  if (pBuffer == NULL) {
    pBuffer = aBuffer;
  }
  //
  // Check if all the bytes are set to 0xFF.
  //
  do {
    NumBytesAtOnce = SEGGER_MIN(NumBytes, SizeOfBuffer);
    r = _ReadOffWithRetry(pInst, pBuffer, Off, NumBytesAtOnce);
    if (r != 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR_BM: _VerifyErase: Could not read data."));
      r = RESULT_ERASE_ERROR;             // Error, could not read data.
      goto Done;
    }
    NumItems = NumBytesAtOnce >> 2;
    pData32  = pBuffer;
    do {
      Data32 = *pData32;
      if (Data32 != 0xFFFFFFFFuL) {
        NumRetries = FS_NOR_NUM_READ_RETRIES;
        for (;;) {
          r = _ReadOffWithRetry(pInst, &Data32, Off, sizeof(Data32));
          if (r != 0) {
            FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR_BM: _VerifyErase: Could not read data."));
            r = RESULT_ERASE_ERROR;       // Error, could not read data.
            goto Done;
          }
          if (Data32 == 0xFFFFFFFFuL) {
            break;                        // OK, the data is correct.
          }
          if (NumRetries-- == 0) {
            FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR_BM: _VerifyErase: Data mismatch Off: 0x%x, Data: 0x%x.", Off, Data32));
            r = RESULT_ERASE_ERROR;       // Error, no more read retries.
            goto Done;
          }
        }
      }
      Off += 4u;
      ++pData32;
    } while (--NumItems != 0u);
    NumBytes -= NumBytesAtOnce;
  } while (NumBytes != 0u);
Done:
  _UnuseFreeMem(NumBytesFree);
  return r;
}

#endif

/*********************************************************************
*
*        _ErasePhySector
*
*  Function description
*    Sets all bytes in a phy. sector to 0xFF.
*/
static int _ErasePhySector(NOR_BM_INST * pInst, U32 PhySectorIndex, U32 * pEraseCnt) {
  int r;
  U8  Unit;
  U32 EraseCnt;

  Unit = pInst->Unit;
  PhySectorIndex += pInst->FirstPhySector;
  r =  pInst->pPhyType->pfEraseSector(Unit, PhySectorIndex);
  CALL_TEST_HOOK_SECTOR_ERASE(Unit, PhySectorIndex, &r);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_BM: _ErasePhySector: Erase failed @ sector %lu with %d.", PhySectorIndex, r));
    return RESULT_ERASE_ERROR;
  }
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR_BM: ERASE PSI: %lu\n", PhySectorIndex));
  IF_STATS(pInst->StatCounters.EraseCnt++);     // Increment statistics counter if enabled
  //
  // On higher debug levels, check if erase worked.
  //
#if FS_NOR_VERIFY_ERASE
  r = 0;
  if (pInst->VerifyErase != 0u) {
    r = _VerifyErase(pInst, PhySectorIndex);
  }
#endif
  if (pEraseCnt != NULL) {
    EraseCnt = *pEraseCnt;
    ++EraseCnt;
    *pEraseCnt = EraseCnt;
  }
  return r;
}

/*********************************************************************
*
*       _GetLogSectorHeaderOff
*/
static U32 _GetLogSectorHeaderOff(const NOR_BM_INST * pInst, unsigned PhySectorIndex, unsigned srsi) {
  U32      Off;
  unsigned SizeOfLSH;
  unsigned SizeOfPSH;

  SizeOfLSH = _SizeOfLSH(pInst);
  SizeOfPSH = _SizeOfPSH(pInst);
  _GetPhySectorInfo(pInst, PhySectorIndex, &Off, NULL);
  Off += SizeOfPSH;
  Off += (SizeOfLSH + (1uL << pInst->ldBytesPerSector)) * srsi;
  return Off;
}

/*********************************************************************
*
*       _GetLogSectorDataOff
*/
static U32 _GetLogSectorDataOff(const NOR_BM_INST * pInst, unsigned PhySectorIndex, unsigned srsi) {
  U32 Off;
  unsigned SizeOfLSH;

  SizeOfLSH = _SizeOfLSH(pInst);
  Off  = _GetLogSectorHeaderOff(pInst, PhySectorIndex, srsi);
  Off += SizeOfLSH;
  return Off;
}

/*********************************************************************
*
*       _WriteLogSectorData
*/
static int _WriteLogSectorData(NOR_BM_INST * pInst, unsigned PhySectorIndex, unsigned srsi, const void * pData, unsigned OffData, unsigned NumBytes) {
  int r;
  U32 Off;
#if (FS_NOR_LINE_SIZE >= 4)
  U32 aBuffer[FS_NOR_LINE_SIZE / 4];
#endif

  Off = _GetLogSectorDataOff(pInst, PhySectorIndex, srsi);
#if (FS_NOR_LINE_SIZE >= 4)
  {
    unsigned BytesPerLine;

    //
    // Make sure that we write at least one flash line.
    // This is required only for the write operations
    // performed by the driver such as when the low-level
    // format information is stored. Sector data written
    // by the file system is always a multiple of a
    // flash line size because the size of a logical
    // sector greater than the size of flash line and
    // both are power of 2 values.
    //
#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
    BytesPerLine = 1uL << pInst->ldBytesPerLine;
#else
    BytesPerLine = FS_NOR_LINE_SIZE;
#endif
    if (NumBytes < BytesPerLine) {
      FS_MEMSET(aBuffer, 0xFF, sizeof(aBuffer));      // 0xFF because we want to leave unchanged the flash locations that are not written.
      FS_MEMCPY(aBuffer, pData, NumBytes);
      pData    = aBuffer;
      NumBytes = BytesPerLine;
    }
  }
#endif // FS_NOR_LINE_SIZE >= 4
  Off += OffData;
  r    = _WriteOff(pInst, pData, Off, NumBytes);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR_BM: WRITE_SECTOR PSI: %u, SRSI: %u, Off: 0x%08x, NumBytes: %u, r: %d\n", PhySectorIndex, srsi, Off, NumBytes, r));
  return r;
}

/*********************************************************************
*
*       _ReadLogSectorData
*/
static int _ReadLogSectorData(NOR_BM_INST * pInst, unsigned PhySectorIndex, unsigned srsi, void * pData, unsigned OffData, unsigned NumBytes) {
  int r;
  U32 Off;

  Off  = _GetLogSectorDataOff(pInst, PhySectorIndex, srsi);
  Off += OffData;
  r    = _ReadOff(pInst, pData, Off, NumBytes);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR_BM: READ_SECTOR PSI: %u, SRSI: %u, Off: 0x%08x, NumBytes: %u, r: %d\n", PhySectorIndex, srsi, Off, NumBytes, r));
  return r;
}

/*********************************************************************
*
*       _GetPhySectorDataStat
*/
static unsigned _GetPhySectorDataStat(const NOR_BM_INST * pInst, const NOR_BM_PSH * pPSH) {
  unsigned DataStat;

#if (FS_NOR_CAN_REWRITE == 0)
  if (_IsRewriteSupported(pInst) != 0) {
    DataStat = pPSH->DataStat;
  } else {
    if (pPSH->IsInvalid == 0u) {        // Reversed logic: 0 means data is invalid
      DataStat = DATA_STAT_INVALID;
    } else {
      if (pPSH->IsValid == 0u) {        // Reversed logic: 0 means data block
        DataStat = DATA_STAT_VALID;
      } else {
        if (pPSH->IsWork == 0u) {       // Reversed logic: 0 means work block
          DataStat = DATA_STAT_WORK;
        } else {
          DataStat = DATA_STAT_EMPTY;
        }
      }
    }
  }
#else
  FS_USE_PARA(pInst);
  DataStat   = pPSH->DataStat;
#endif // FS_NOR_CAN_REWRITE == 0
  return DataStat;
}

/*********************************************************************
*
*       _GetPhySectorDataStatNR
*
*  Function description
*    Determines the type of data stored to a physical sector.
*
*  Parameters
*    pInst        Driver instance.
*    pPSH         [IN] Header of the physical sector.
*
*  Return value
*    The type of data stored. One of DATA_STAT_... values.
*/
static unsigned _GetPhySectorDataStatNR(NOR_BM_INST * pInst, NOR_BM_PSH * pPSH) {
  unsigned DataStat;

  FS_USE_PARA(pInst);
#if (FS_NOR_CAN_REWRITE == 0)
  if (_IsRewriteSupported(pInst) != 0) {
    DataStat = pPSH->DataStat;
  } else {
    if (pPSH->IsInvalid == 0u) {                  // Reversed logic: 0 means data is invalid
      DataStat = DATA_STAT_INVALID;
    } else {
      int IsData;
      int IsWork;

      IsData = 0;
      if (pPSH->IsValid == 0u) {                  // Reversed logic: 0 means data block
#if FAIL_SAFE_ERASE_NO_REWRITE
        if (pInst->FailSafeErase != 0u) {
          if (pPSH->lbi != LBI_INVALID) {
            IsData = 1;
          } else {
            //
            // The LBI is stored on the same flash line as the data indicator.
            //
            if (pPSH->lbiData != LBI_INVALID) {     // Validate the LBI value.
#if (FS_NOR_SUPPORT_CRC != 0) && (FS_NOR_SUPPORT_ECC != 0)
              if ((_IsCRCEnabled(pInst) != 0) && (_IsECCEnabled(pInst) != 0)) {
                int r;
                int NumBitsCorrected;

                if (pPSH->eccStatData == 0) {     // Is ECC valid?
                  //
                  // Check that the data was completely written by verifying the ECC.
                  //
                  NumBitsCorrected = 0;
                  r = pInst->pECC_API->pfLoadApplyPSH_Data(pInst, pPSH, &NumBitsCorrected);
                  if (r == 0) {
                    UPDATE_NUM_BIT_ERRORS(pInst, NumBitsCorrected);
                    //
                    // Check that the data was completely written by verifying the CRC.
                    //
                    r = _pCRC_API->pfLoadVerifyPSH_Data(pInst, pPSH);
                    if (r == 0) {
                      IsData = 1;
                    }
                  }
                }
              } else {
                if ((_IsCRCEnabled(pInst) != 0) && (_IsECCEnabled(pInst) == 0)) {
                  int r;

                  //
                  // Check that the data was completely written by verifying the CRC.
                  //
                  r = _pCRC_API->pfLoadVerifyPSH_Data(pInst, pPSH);
                  if (r == 0) {
                    IsData = 1;
                  }
                } else {
                  if ((_IsCRCEnabled(pInst) == 0) && (_IsECCEnabled(pInst) != 0)) {
                    int r;
                    int NumBitsCorrected;

                    if (pPSH->eccStatData == 0) {     // Is ECC valid?
                      //
                      // Check that the data was completely written by verifying the ECC.
                      //
                      NumBitsCorrected = 0;
                      r = pInst->pECC_API->pfLoadApplyPSH_Data(pInst, pPSH, &NumBitsCorrected);
                      if (r == 0) {
                        UPDATE_NUM_BIT_ERRORS(pInst, NumBitsCorrected);
                        IsData = 1;
                      }
                    }
                  } else {
                    if (pPSH->crcData == 0u) {
                      IsData = 1;
                    }
                  }
                }
              }
#elif (FS_NOR_SUPPORT_CRC != 0) && (FS_NOR_SUPPORT_ECC == 0)
              if (_IsCRCEnabled(pInst) != 0) {
                int r;

                //
                // Check that the data was completely written by verifying the CRC.
                //
                r = _pCRC_API->pfLoadVerifyPSH_Data(pInst, pPSH);
                if (r == 0) {
                  IsData = 1;
                }
              } else {
                if (pPSH->crcData == 0u) {
                  IsData = 1;
                }
              }
#elif (FS_NOR_SUPPORT_CRC == 0) && (FS_NOR_SUPPORT_ECC != 0)
              if (_IsECCEnabled(pInst) != 0) {
                int r;
                int NumBitsCorrected;

                if (pPSH->eccStatData == 0) {     // Is ECC valid?
                  //
                  // Check that the data was completely written by verifying the ECC.
                  //
                  NumBitsCorrected = 0;
                  r = pInst->pECC_API->pfLoadApplyPSH_Data(pInst, pPSH, &NumBitsCorrected);
                  if (r == 0) {
                    UPDATE_NUM_BIT_ERRORS(pInst, NumBitsCorrected);
                    IsData = 1;
                  }
                }
              } else {
                if (pPSH->crcData == 0u) {
                  IsData = 1;
                }
              }
#else
              //
              // Check that the data was completely written by looking at the
              // value of the NOR_BM_PSH::crcData member. If the value is set
              // to 0 then there is a great chance that the write operation was
              // not interrupted.
              //
              if (pPSH->crcData == 0u) {
                IsData = 1;
              }
#endif // FS_NOR_SUPPORT_CRC != 0 && FS_NOR_SUPPORT_ECC != 0
            }
          }
        } else {
          IsData = 1;
        }
#else
        IsData = 1;
#endif // FAIL_SAFE_ERASE_NO_REWRITE
      }
      IsWork = 0;
      if (pPSH->IsWork == 0u) {                   // Reversed logic: 0 means work block
#if FAIL_SAFE_ERASE_NO_REWRITE
        if (pInst->FailSafeErase != 0u) {
          if (pPSH->lbi != LBI_INVALID) {
            IsWork = 1;
          } else {
            //
            // The LBI is stored on the same flash line as the data indicator.
            //
            if (pPSH->lbiWork != LBI_INVALID) {     // Validate the LBI value.
#if (FS_NOR_SUPPORT_CRC != 0) && (FS_NOR_SUPPORT_ECC != 0)
              if ((_IsCRCEnabled(pInst) != 0) && (_IsECCEnabled(pInst) != 0)) {
                int r;
                int NumBitsCorrected;

                if (pPSH->eccStatWork == 0) {     // Is ECC valid?
                  //
                  // Check that the data was completely written by verifying the ECC.
                  //
                  NumBitsCorrected = 0;
                  r = pInst->pECC_API->pfLoadApplyPSH_Work(pInst, pPSH, &NumBitsCorrected);
                  if (r == 0) {
                    UPDATE_NUM_BIT_ERRORS(pInst, NumBitsCorrected);
                    //
                    // Check that the data was completely written by verifying the CRC.
                    //
                    r = _pCRC_API->pfLoadVerifyPSH_Work(pInst, pPSH);
                    if (r == 0) {
                      IsWork = 1;
                    }
                  }
                }
              } else {
                if ((_IsCRCEnabled(pInst) != 0) && (_IsECCEnabled(pInst) == 0)) {
                  int r;

                  //
                  // Check that the data was completely written by verifying the CRC.
                  //
                  r = _pCRC_API->pfLoadVerifyPSH_Work(pInst, pPSH);
                  if (r == 0) {
                    IsWork = 1;
                  }
                } else {
                  if ((_IsCRCEnabled(pInst) == 0) && (_IsECCEnabled(pInst) != 0)) {
                    int r;
                    int NumBitsCorrected;

                    if (pPSH->eccStatWork == 0) {     // Is ECC valid?
                      //
                      // Check that the data was completely written by verifying the ECC.
                      //
                      NumBitsCorrected = 0;
                      r = pInst->pECC_API->pfLoadApplyPSH_Work(pInst, pPSH, &NumBitsCorrected);
                      if (r == 0) {
                        UPDATE_NUM_BIT_ERRORS(pInst, NumBitsCorrected);
                        IsWork = 1;
                      }
                    }
                  } else {
                    if (pPSH->crcWork == 0u) {
                      IsWork = 1;
                    }
                  }
                }
              }
#elif (FS_NOR_SUPPORT_CRC != 0) && (FS_NOR_SUPPORT_ECC == 0)
              if (_IsCRCEnabled(pInst) != 0) {
                int r;

                //
                // Check that the data was completely written by verifying the CRC.
                //
                r = _pCRC_API->pfLoadVerifyPSH_Work(pInst, pPSH);
                if (r == 0) {
                  IsWork = 1;
                }
              } else {
                if (pPSH->crcWork == 0u) {
                  IsWork = 1;
                }
              }
#elif (FS_NOR_SUPPORT_CRC == 0) && (FS_NOR_SUPPORT_ECC != 0)
              if (_IsECCEnabled(pInst) != 0) {
                int r;
                int NumBitsCorrected;

                if (pPSH->eccStatWork == 0) {     // Is ECC valid?
                  //
                  // Check that the data was completely written by verifying the ECC.
                  //
                  NumBitsCorrected = 0;
                  r = pInst->pECC_API->pfLoadApplyPSH_Work(pInst, pPSH, &NumBitsCorrected);
                  if (r == 0) {
                    UPDATE_NUM_BIT_ERRORS(pInst, NumBitsCorrected);
                    IsWork = 1;
                  }
                }
              } else {
                if (pPSH->crcWork == 0u) {
                  IsWork = 1;
                }
              }
#else
              //
              // Check that the data was completely written by looking at the
              // value of the NOR_BM_PSH::crcWork member. If the value is set
              // to 0 then there is a great chance that the write operation was
              // not interrupted. In addition, we validate the value of the
              // NOR_BM_PSH::lbiWork member.
              //
              if (pPSH->crcWork == 0u) {
                IsWork = 1;
              }
#endif // FS_NOR_SUPPORT_CRC != 0 && FS_NOR_SUPPORT_ECC != 0
            }
          }
        } else {
          IsWork = 1;
        }
#else
        IsWork = 1;
#endif // FAIL_SAFE_ERASE_NO_REWRITE
      }
      if (IsData != 0) {
        DataStat = DATA_STAT_VALID;
      } else {
        if (IsWork != 0) {
          DataStat = DATA_STAT_WORK;
        } else {
          DataStat = DATA_STAT_EMPTY;
        }
      }
    }
  }
#else
  DataStat = pPSH->DataStat;
#endif // FS_NOR_CAN_REWRITE == 0
  return DataStat;
}

/*********************************************************************
*
*       _SetPhySectorDataStat
*/
static void _SetPhySectorDataStat(const NOR_BM_INST * pInst, NOR_BM_PSH * pPSH, unsigned DataStat) {
  FS_USE_PARA(pInst);
#if (FS_NOR_CAN_REWRITE == 0)
  if (_IsRewriteSupported(pInst) != 0) {
    pPSH->DataStat = (U8)DataStat;
    UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_PSH, DataStat), sizeof(pPSH->DataStat));
  } else {
    if (DataStat == DATA_STAT_WORK) {
      pPSH->IsWork = 0;                     // Reversed logic: 0 means work block
      UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_PSH, IsWork), sizeof(pPSH->IsWork));
    } else {
      if (DataStat == DATA_STAT_VALID) {
        pPSH->IsValid = 0;                  // Reversed logic: 0 means data block
        UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_PSH, IsValid), sizeof(pPSH->IsValid));
      } else {
        if (DataStat == DATA_STAT_INVALID) {
          pPSH->IsInvalid = 0;              // Reversed logic: 0 means data is invalid
          UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_PSH, IsInvalid), sizeof(pPSH->IsInvalid));
        }
      }
    }
  }
#else
  pPSH->DataStat = (U8)DataStat;
  UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_PSH, DataStat), sizeof(pPSH->DataStat));
#endif // FS_NOR_CAN_REWRITE == 0
}

/*********************************************************************
*
*       _IsEraseCntValid
*
*  Function description
*    Checks if the erase count of a physical sector is valid.
*
*  Parameters
*    pPSH     [IN] Physical sector header.
*
*  Return value
*    ==1      The erase count is valid.
*    ==0      The erase count is invalid.
*/
static int _IsEraseCntValid(const NOR_BM_PSH * pPSH) {
  int r;
  U32 EraseCnt;

  r = 1;            // Assume that the erase count is valid.
  EraseCnt = pPSH->EraseCnt;
  if ((EraseCnt == ERASE_CNT_INVALID) || (EraseCnt > (unsigned)FS_NOR_MAX_ERASE_CNT)) {
    r = 0;          // The erase count is not valid.
  }
  return r;
}

/*********************************************************************
*
*       _GetPhySectorEraseCnt
*/
static U32 _GetPhySectorEraseCnt(const NOR_BM_INST * pInst, const NOR_BM_PSH * pPSH) {
  U32 EraseCnt;

  if (_IsEraseCntValid(pPSH) == 0) {
    EraseCnt = pInst->EraseCntMax;
  } else {
    EraseCnt = pPSH->EraseCnt;
  }
  return EraseCnt;
}

#if FS_NOR_SUPPORT_FAIL_SAFE_ERASE

/*********************************************************************
*
*       _GetPhySectorEraseSignature
*/
static U32 _GetPhySectorEraseSignature(const NOR_BM_PSH * pPSH) {
  U32 Signature;

  Signature = pPSH->EraseSignature;
  return Signature;
}

#if (FS_NOR_CAN_REWRITE == 0)

/*********************************************************************
*
*       _SetPhySectorEraseSignature
*/
static void _SetPhySectorEraseSignature(const NOR_BM_INST * pInst, NOR_BM_PSH * pPSH, U32 Signature) {
  if (pInst->FailSafeErase != 0u) {
    pPSH->EraseSignature = Signature;
    UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_PSH, EraseSignature), sizeof(pPSH->EraseSignature));
  }
}

#endif // FS_NOR_CAN_REWRITE == 0

#endif // FS_NOR_SUPPORT_FAIL_SAFE_ERASE

/*********************************************************************
*
*       _GetPhySectorLBI
*/
static unsigned _GetPhySectorLBI(const NOR_BM_INST * pInst, const NOR_BM_PSH * pPSH) {
  unsigned lbi;

  lbi = pPSH->lbi;
#if FAIL_SAFE_ERASE_NO_REWRITE
  if (pInst->FailSafeErase != 0u) {
     unsigned DataStat;

    if (lbi == LBI_INVALID) {
      if (_IsRewriteSupported(pInst) == 0) {
        DataStat = _GetPhySectorDataStat(pInst, pPSH);
        if (DataStat == DATA_STAT_VALID) {
          lbi = pPSH->lbiData;
        } else {
          if (DataStat == DATA_STAT_WORK) {
            lbi = pPSH->lbiWork;
          }
        }
      }
    }
  }
#else
  FS_USE_PARA(pInst);
#endif // FAIL_SAFE_ERASE_NO_REWRITE
  return lbi;
}

/*********************************************************************
*
*       _GetPhySectorLBI_NR
*/
static unsigned _GetPhySectorLBI_NR(const NOR_BM_INST * pInst, const NOR_BM_PSH * pPSH, unsigned DataStat) {
  unsigned lbi;

  lbi = pPSH->lbi;
#if FAIL_SAFE_ERASE_NO_REWRITE
  if (pInst->FailSafeErase != 0u) {
    if (lbi == LBI_INVALID) {
      if (_IsRewriteSupported(pInst) == 0) {
        if (DataStat == DATA_STAT_VALID) {
          lbi = pPSH->lbiData;
        } else {
          if (DataStat == DATA_STAT_WORK) {
            lbi = pPSH->lbiWork;
          }
        }
      }
    }
  }
#else
  FS_USE_PARA(pInst);
  FS_USE_PARA(DataStat);
#endif // FAIL_SAFE_ERASE_NO_REWRITE
  return lbi;
}

/*********************************************************************
*
*       _GetPhySectorDataCnt
*/
static unsigned _GetPhySectorDataCnt(const NOR_BM_INST * pInst, const NOR_BM_PSH * pPSH) {
  unsigned DataCnt;

  DataCnt = pPSH->DataCnt;
#if FAIL_SAFE_ERASE_NO_REWRITE
  if (pInst->FailSafeErase) {
    unsigned DataStat;
    unsigned lbi;

    //
    // All the values stored to NOR_BM_PSH::DataCnt member are valid therefore
    // we have to check the value of the NOR_BM_PSH::lbi member here because
    // the values of these two members are always stored together.
    //
    lbi = pPSH->lbi;
    if (lbi == LBI_INVALID) {
      if (_IsRewriteSupported(pInst) == 0) {
        DataStat = _GetPhySectorDataStat(pInst, pPSH);
        if (DataStat == DATA_STAT_VALID) {
          DataCnt = pPSH->DataCntData;
        } else {
          if (DataStat == DATA_STAT_WORK) {
            DataCnt = pPSH->DataCntWork;
          }
        }
      }
    }
  }
#else
  FS_USE_PARA(pInst);
#endif // FAIL_SAFE_ERASE_NO_REWRITE
  return DataCnt;
}

#if (FS_NOR_SUPPORT_CRC != 0) || (FS_NOR_SUPPORT_ECC != 0)

/*********************************************************************
*
*       _IsBlankPSH
*
*  Function description
*    Checks if all the bytes in a physical sector header are set to 0xFF.
*
*  Parameters
*    pInst        Driver instance.
*    pPSH         [IN] Physical sector header.
*
*  Return value
*    ==1      Is empty.
*    ==0      Is not empty.
*/
static int _IsBlankPSH(const NOR_BM_INST * pInst, const NOR_BM_PSH * pPSH) {
  const U8 * p;
  unsigned   NumBytes;
  int        r;

  r = 1;                // Assume that all the bytes are set to 0xFF.
  p = SEGGER_CONSTPTR2PTR(const U8, pPSH);                                          // MISRA deviation D:100e
#if FS_NOR_SUPPORT_FAIL_SAFE_ERASE
  if (pInst->FailSafeErase == 0u) {
#if (FS_NOR_PHY_SECTOR_RESERVE >= (SIZEOF_ERASE_SIGNATURE + SIZEOF_CRC_PSH + SIZEOF_ECC_PSH))
    NumBytes = OFFSET_OF_MEMBER(NOR_BM_PSH, ecc1Stat) + SIZE_OF_MEMBER(NOR_BM_PSH, ecc1Stat);
#elif (FS_NOR_PHY_SECTOR_RESERVE >= (SIZEOF_ERASE_SIGNATURE + SIZEOF_CRC_PSH))
    NumBytes = OFFSET_OF_MEMBER(NOR_BM_PSH, crcStat) + SIZE_OF_MEMBER(NOR_BM_PSH, crcStat);
#else
    NumBytes = OFFSET_OF_MEMBER(NOR_BM_PSH, EraseSignature) + SIZE_OF_MEMBER(NOR_BM_PSH, EraseSignature);
#endif // (FS_NOR_PHY_SECTOR_RESERVE >= (SIZEOF_ERASE_SIGNATURE + SIZEOF_CRC_PSH))
    do {
      if (*p++ != 0xFFu) {
        r = 0;          // Not empty.
        break;
      }
    } while (--NumBytes != 0u);
  } else
#else
  FS_USE_PARA(pInst);
#endif // FS_NOR_SUPPORT_FAIL_SAFE_ERASE
  {
    //
    // Do not check the EraseCnt and EraseSignature members
    // because they are stored without CRC.
    //
    NumBytes = OFFSET_OF_MEMBER(NOR_BM_PSH, EraseCnt);
    do {
      if (*p++ != 0xFFu) {
        r = 0;            // Not empty.
        break;
      }
    } while (--NumBytes != 0u);
#if (FS_NOR_PHY_SECTOR_RESERVE >= (SIZEOF_ERASE_SIGNATURE + SIZEOF_CRC_PSH))
    if (r != 0) {
      unsigned Off;

      Off      = OFFSET_OF_MEMBER(NOR_BM_PSH, EraseSignature) + SIZE_OF_MEMBER(NOR_BM_PSH, EraseSignature);
#if (FS_NOR_PHY_SECTOR_RESERVE >= (SIZEOF_ERASE_SIGNATURE + SIZEOF_CRC_PSH + SIZEOF_ECC_PSH))
      NumBytes = OFFSET_OF_MEMBER(NOR_BM_PSH, ecc1Stat) + SIZE_OF_MEMBER(NOR_BM_PSH, ecc1Stat);
#else
      NumBytes = OFFSET_OF_MEMBER(NOR_BM_PSH, crcStat) + SIZE_OF_MEMBER(NOR_BM_PSH, crcStat);
#endif
      if (Off < NumBytes) {
        p         = SEGGER_CONSTPTR2PTR(const U8, pPSH);                            // MISRA deviation D:100e
        p        += Off;
        NumBytes -= Off;
        do {
          if (*p++ != 0xFFu) {
            r = 0;        // Not empty.
            break;
          }
        } while (--NumBytes != 0u);
      }
    }
#endif // (FS_NOR_PHY_SECTOR_RESERVE >= (SIZEOF_ERASE_SIGNATURE + SIZEOF_CRC_PSH))
  }
  return r;
}

/*********************************************************************
*
*       _IsBlankLSH
*
*  Function description
*    Checks if all the bytes in a logical sector header are set to 0xFF.
*
*  Parameters
*    pLSH         [IN] Logical sector header.
*
*  Return value
*    ==1      Is empty.
*    ==0      Is not empty.
*/
static int _IsBlankLSH(const NOR_BM_LSH * pLSH) {
  const U8 * p;
  unsigned   NumBytes;
  int        r;

  r        = 1;         // Assume that all the bytes are set to 0xFF.
  p        = SEGGER_CONSTPTR2PTR(const U8, pLSH);                                   // MISRA deviation D:100e
#if (FS_NOR_LOG_SECTOR_RESERVE >= (SIZEOF_CRC_LSH + SIZEOF_ECC_LSH))
  NumBytes = OFFSET_OF_MEMBER(NOR_BM_LSH, ecc1Stat) + SIZE_OF_MEMBER(NOR_BM_LSH, ecc1Stat);
#elif (FS_NOR_LOG_SECTOR_RESERVE >= SIZEOF_CRC_LSH)
  NumBytes = OFFSET_OF_MEMBER(NOR_BM_LSH, crcStat) + SIZE_OF_MEMBER(NOR_BM_LSH, crcStat);
#else
  NumBytes = OFFSET_OF_MEMBER(NOR_BM_LSH, brsi) + SIZE_OF_MEMBER(NOR_BM_LSH, brsi);
#endif // (FS_NOR_LOG_SECTOR_RESERVE >= (SIZEOF_CRC_LSH + SIZEOF_ECC_LSH))
  do {
    if (*p++ != 0xFFu) {
      r = 0;            // Not empty.
      break;
    }
  } while (--NumBytes != 0u);
  return r;
}

#endif // FS_NOR_SUPPORT_CRC != 0 || FS_NOR_SUPPORT_ECC != 0

#if FS_NOR_SUPPORT_CRC

/*********************************************************************
*
*       _CalcLSH_CRC
*/
static U8 _CalcLSH_CRC(const NOR_BM_LSH * pLSH) {
  U8         crc;
  const U8 * pBRSI;
  const U8 * pCRCSectorData;
#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  U16        brsi;
  U16        crcSectorData;
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER

#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  brsi           = _pMultiByteAPI->pfLoadU16(SEGGER_CONSTPTR2PTR(const U8, &pLSH->brsi));           // MISRA deviation D:100e
  crcSectorData  = _pMultiByteAPI->pfLoadU16(SEGGER_CONSTPTR2PTR(const U8, &pLSH->crcSectorData));  // MISRA deviation D:100e
  pBRSI          = SEGGER_CONSTPTR2PTR(const U8, &brsi);                                            // MISRA deviation D:100e
  pCRCSectorData = SEGGER_CONSTPTR2PTR(const U8, &crcSectorData);                                   // MISRA deviation D:100e
#else
  pBRSI          = SEGGER_CONSTPTR2PTR(const U8, &pLSH->brsi);                                      // MISRA deviation D:100e
  pCRCSectorData = SEGGER_CONSTPTR2PTR(const U8, &pLSH->crcSectorData);                             // MISRA deviation D:100e
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  crc = CRC_DRIVER_DATA_INIT;
  crc = _pCRCHook->pfCalcCRC8(&pLSH->DataStat, sizeof(pLSH->DataStat),      crc);
  crc = _pCRCHook->pfCalcCRC8(pBRSI,           sizeof(pLSH->brsi),          crc);
  crc = _pCRCHook->pfCalcCRC8(pCRCSectorData,  sizeof(pLSH->crcSectorData), crc);
  return crc;
}

/*********************************************************************
*
*       _CalcStoreCRC_LSH
*
*  Function description
*    Calculates the 8-bit CRC of the logical sector header.
*    The calculated CRC is stored to logical sector header.
*
*  Return value
*    ==0      OK, CRC calculated and stored.
*    !=0      Logical sector header is not consistent.
*/
static int _CalcStoreCRC_LSH(NOR_BM_LSH * pLSH) {
  U8  crc;
  int r;

  r   = 0;                          // Set to indicate success.
  crc = _CalcLSH_CRC(pLSH);
  switch (pLSH->crcStat) {
  case CRC_STAT_INVALID:
    pLSH->crc0    = crc;
    pLSH->crcStat = CRC_STAT_VALID0;
    UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_LSH, crc0), sizeof(pLSH->crc0));
    UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_LSH, crcStat), sizeof(pLSH->crcStat));
    break;
  case CRC_STAT_VALID0:
    pLSH->crc1    = crc;
    pLSH->crcStat = CRC_STAT_VALID1;
    UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_LSH, crc1), sizeof(pLSH->crc1));
    UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_LSH, crcStat), sizeof(pLSH->crcStat));
    break;
  default:
    r = RESULT_INCONSISTENT_DATA;   // Error, the logical sector header is not consistent.
    break;
  }
  return r;
}

#if FS_NOR_OPTIMIZE_DATA_WRITE

/*********************************************************************
*
*       _CountTrailingBlankBytes
*
*  Function description
*    Counts the number of bytes set to 0xFF at the end of the specified array.
*
*  Parameters
*    p          [IN] Array of bytes to be checked.
*    NumBytes   Number of bytes in the array to the checked.
*
*  Return value
*    Number of bytes set to 0xFF.
*
*  Additional information
*    The return value of this function is 3 for an array of 5 bytes
*    that contains the following values: 0xFF 0xAA 0xFF 0xFF 0xFF.
*/
static unsigned _CountTrailingBlankBytes(const U8 * p, unsigned NumBytes) {
  unsigned NumBytesBlank;

  NumBytesBlank = 0;
  if (NumBytes != 0u) {
    p += NumBytes - 1u;
    do {
      if (*p != 0xFFu) {
        break;
      }
      ++NumBytesBlank;
      --p;
    } while (--NumBytes != 0u);
  }
  return NumBytesBlank;
}

#endif // FS_NOR_OPTIMIZE_DATA_WRITE

/*********************************************************************
*
*       _LoadVerifyCRC_LSH
*
*  Function description
*    Verifies the data integrity of the logical sector header using CRC.
*
*  Parameters
*    pInst        Driver instance.
*    pLSH         Logical sector header.
*
*  Return value
*    ==0      OK, no bit errors detected.
*    !=0      Error, bit errors detected.
*/
static int _LoadVerifyCRC_LSH(const NOR_BM_INST * pInst, NOR_BM_LSH * pLSH) {
  U8  crc;
  U8  crcCalc;
  int DoVerify;
  int r;

  FS_USE_PARA(pInst);
  r        = 0;                           // Set to indicate success.
  DoVerify = 1;                           // Set to perform the CRC verification.
  crc      = CRC_DRIVER_DATA_INIT;
  switch (pLSH->crcStat) {
  case CRC_STAT_INVALID:
    if (_IsBlankLSH(pLSH) != 0) {
      DoVerify = 0;                       // CRC verification is not required since the logical sector header is empty.
    }
    break;
  case CRC_STAT_VALID0:
    crc = pLSH->crc0;
    break;
  case CRC_STAT_VALID1:
    crc = pLSH->crc1;
    break;
  default:
    DoVerify = 0;
    r        = RESULT_INCONSISTENT_DATA;  // Error, the logical sector header is not consistent.
    break;
  }
  if (DoVerify != 0) {
    crcCalc = _CalcLSH_CRC(pLSH);
    if (crc != crcCalc) {
      if (pLSH->crcStat != CRC_STAT_VALID1) {
#if FS_NOR_OPTIMIZE_DATA_WRITE
        unsigned   DataStat;
        unsigned   DataStatToCheck;
        const U8 * p;
        unsigned   NumBytes;
        unsigned   NumBytesBlank;
        unsigned   NumBytesBlankMin;

        //
        // We try to recover here from an interrupted write operation that marked the beginning
        // of a sector write operation. In this case, the BRSI is written first without marking
        // the data as valid. We set the data status to valid here and verify again the CRC.
        // With the support for ECC enabled the write sequence is different in that all the
        // management information is written (including the data status which is set to valid)
        // with the exception of the ECC status which is set to invalid. The ECC status is set
        // to valid at the end of the sector write operation. The error recovery procedure
        // performed in _LoadApplyECC_LSH() sets the data status to invalid therefore we
        // have to take this into account when we verify the CRC for a second time here.
        //
        r               = RESULT_CRC_ERROR;
        DataStat        = pLSH->DataStat;
        DataStatToCheck = DATA_STAT_EMPTY;
#if FS_NOR_SUPPORT_ECC
        if (_IsECCEnabled(pInst) != 0) {
          DataStatToCheck = DATA_STAT_INVALID;
        }
#endif // FS_NOR_SUPPORT_ECC
        if (DataStat == DataStatToCheck) {
          pLSH->DataStat = DATA_STAT_VALID;
          crcCalc = _CalcLSH_CRC(pLSH);
          if (crc == crcCalc) {
            DataStat = DATA_STAT_EMPTY;
            r = 0;
          } else {
            //
            // We try to recover here from an interrupted write operation that stores
            // the BRSI and the CRC of the sector data. In this case the value of all
            // the bytes after the last byte successfully written up to and including
            // crcStat are set to 0xFF.
            //
            p  = SEGGER_CONSTPTR2PTR(const U8, pLSH);                                   // MISRA deviation D:100e
            p += OFFSET_OF_MEMBER(NOR_BM_LSH, brsi);
            NumBytes         = 0u
                              + SIZE_OF_MEMBER(NOR_BM_LSH, brsi)
                              + SIZE_OF_MEMBER(NOR_BM_LSH, crcSectorData)
                              + SIZE_OF_MEMBER(NOR_BM_LSH, crc1)
                              + SIZE_OF_MEMBER(NOR_BM_LSH, crcStat)
                              ;
            NumBytesBlankMin = 0u
                              + SIZE_OF_MEMBER(NOR_BM_LSH, crc1)
                              + SIZE_OF_MEMBER(NOR_BM_LSH, crcStat)
                              ;
            NumBytesBlank = _CountTrailingBlankBytes(p, NumBytes);
            if (NumBytesBlank >= NumBytesBlankMin) {
              DataStat = DATA_STAT_EMPTY;
              r = 0;
            }
          }
          pLSH->DataStat = (U8)DataStat;
        }
#else
        r = RESULT_CRC_ERROR;
#endif // FS_NOR_OPTIMIZE_DATA_WRITE
      } else {
        //
        // Try to recover from an interrupted write operation that marked the logical sector as invalid.
        // In this case the data status is written last which makes the CRC verification to fail.
        // We set the data status to invalid here and verify again the CRC.
        //
        pLSH->DataStat = DATA_STAT_INVALID;
        crcCalc = _CalcLSH_CRC(pLSH);
        if (crc != crcCalc) {
          r = RESULT_CRC_ERROR;
        }
      }
    }
  }
#if (FS_NOR_CAN_REWRITE == 0)
  if (r == 0) {
    //
    // The status flags are not protected by CRC to increase the performance.
    // Each flag occupies a single byte at the beginning of a flash line
    // and it can be either set to 0xFF if not set or to 0x00 if set.
    // We explicitly check here for these values and return a CRC error
    // in case the flag is not set to one of these values.
    //
    if (_IsRewriteSupported(pInst) == 0) {
      if ((pLSH->IsValid != 0xFFu) && (pLSH->IsValid != 0x00u)) {
        r = RESULT_CRC_ERROR;
      }
      if ((pLSH->IsInvalid != 0xFFu) && (pLSH->IsInvalid != 0x00u)) {
        r = RESULT_CRC_ERROR;
      }
    }
  }
#endif // FS_NOR_CAN_REWRITE == 0
  return r;
}

/*********************************************************************
*
*       _CalcPSH_CRC
*/
static U8 _CalcPSH_CRC(const NOR_BM_INST * pInst, const NOR_BM_PSH * pPSH) {
  U8          crc;
  const U8  * pLBI;
  const U8  * pEraseCnt;
  const U8  * pEraseSignature;
#if FS_NOR_SUPPORT_FAIL_SAFE_ERASE
  U32         EraseCnt;
  U32         EraseSignature;
#endif // FS_NOR_SUPPORT_FAIL_SAFE_ERASE
#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  U16         lbi;
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER

#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  lbi             = _pMultiByteAPI->pfLoadU16(SEGGER_CONSTPTR2PTR(const U8, &pPSH->lbi));               // MISRA deviation D:100e
  EraseCnt        = _pMultiByteAPI->pfLoadU32(SEGGER_CONSTPTR2PTR(const U8, &pPSH->EraseCnt));          // MISRA deviation D:100e
  EraseSignature  = _pMultiByteAPI->pfLoadU32(SEGGER_CONSTPTR2PTR(const U8, &pPSH->EraseSignature));    // MISRA deviation D:100e
  pLBI            = SEGGER_CONSTPTR2PTR(const U8, &lbi);                                                // MISRA deviation D:100e
  pEraseCnt       = SEGGER_CONSTPTR2PTR(const U8, &EraseCnt);                                           // MISRA deviation D:100e
  pEraseSignature = SEGGER_CONSTPTR2PTR(const U8, &EraseSignature);                                     // MISRA deviation D:100e
#else
  pLBI            = SEGGER_CONSTPTR2PTR(const U8, &pPSH->lbi);                                          // MISRA deviation D:100e
  pEraseCnt       = SEGGER_CONSTPTR2PTR(const U8, &pPSH->EraseCnt);                                     // MISRA deviation D:100e
  pEraseSignature = SEGGER_CONSTPTR2PTR(const U8, &pPSH->EraseSignature);                               // MISRA deviation D:100e
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
#if FS_NOR_SUPPORT_FAIL_SAFE_ERASE
  if (pInst->FailSafeErase != 0u) {
    //
    // The EraseCnt and the EraseSignature are not protected by CRC if the fail safe erase operation is active.
    // We set these members to default values for backward compatibility to older emFile versions.
    //
    EraseCnt        = 0xFFFFFFFFuL;
    EraseSignature  = 0xFFFFFFFFuL;
    pEraseCnt       = SEGGER_PTR2PTR(U8, &EraseCnt);                                                // MISRA deviation D:100e
    pEraseSignature = SEGGER_PTR2PTR(U8, &EraseSignature);                                          // MISRA deviation D:100e
  }
#else
  FS_USE_PARA(pInst);
#endif // FS_NOR_SUPPORT_FAIL_SAFE_ERASE
  crc = CRC_DRIVER_DATA_INIT;
  crc = _pCRCHook->pfCalcCRC8(&pPSH->DataStat, sizeof(pPSH->DataStat),       crc);
  crc = _pCRCHook->pfCalcCRC8(&pPSH->DataCnt,  sizeof(pPSH->DataCnt),        crc);
  crc = _pCRCHook->pfCalcCRC8(pLBI,            sizeof(pPSH->lbi),            crc);
  crc = _pCRCHook->pfCalcCRC8(pEraseCnt,       sizeof(pPSH->EraseCnt),       crc);
  crc = _pCRCHook->pfCalcCRC8(pEraseSignature, sizeof(pPSH->EraseSignature), crc);
  return crc;
}

#if FAIL_SAFE_ERASE_NO_REWRITE

/*********************************************************************
*
*       _CalcPSH_CRC_Data
*/
static U8 _CalcPSH_CRC_Data(const NOR_BM_PSH * pPSH) {
  U8          crc;
  const U8  * pLBI;
#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  U16         lbi;
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER

#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  lbi  = _pMultiByteAPI->pfLoadU16(SEGGER_CONSTPTR2PTR(const U8, &pPSH->lbiData));                  // MISRA deviation D:100e
  pLBI = SEGGER_CONSTPTR2PTR(const U8, &lbi);                                                       // MISRA deviation D:100e
#else
  pLBI = SEGGER_CONSTPTR2PTR(const U8, &pPSH->lbiData);                                             // MISRA deviation D:100e
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  crc = CRC_DRIVER_DATA_INIT;
  crc = _pCRCHook->pfCalcCRC8(&pPSH->IsValid,     sizeof(pPSH->IsValid),     crc);
  crc = _pCRCHook->pfCalcCRC8(&pPSH->DataCntData, sizeof(pPSH->DataCntData), crc);
  crc = _pCRCHook->pfCalcCRC8(pLBI,               sizeof(pPSH->lbiData),     crc);
  return crc;
}

/*********************************************************************
*
*       _CalcPSH_CRC_Work
*/
static U8 _CalcPSH_CRC_Work( const NOR_BM_PSH * pPSH) {
  U8          crc;
  const U8  * pLBI;
#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  U16         lbi;
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER

#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  lbi  = _pMultiByteAPI->pfLoadU16(SEGGER_CONSTPTR2PTR(const U8, &pPSH->lbiWork));                  // MISRA deviation D:100e
  pLBI = SEGGER_CONSTPTR2PTR(const U8, &lbi);                                                       // MISRA deviation D:100e
#else
  pLBI = SEGGER_CONSTPTR2PTR(const U8, &pPSH->lbiWork);                                             // MISRA deviation D:100e
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  crc = CRC_DRIVER_DATA_INIT;
  crc = _pCRCHook->pfCalcCRC8(&pPSH->IsWork,      sizeof(pPSH->IsWork),      crc);
  crc = _pCRCHook->pfCalcCRC8(&pPSH->DataCntWork, sizeof(pPSH->DataCntWork), crc);
  crc = _pCRCHook->pfCalcCRC8(pLBI,               sizeof(pPSH->lbiWork),     crc);
  return crc;
}

#endif // FAIL_SAFE_ERASE_NO_REWRITE

/*********************************************************************
*
*       _CalcStoreCRC_PSH
*
*  Function description
*    Calculates the 8-bit CRC of the physical sector header.
*    The calculated CRC is stored to physical sector header.
*
*  Return value
*    ==0      OK, CRC calculated and stored.
*    !=0      Physical sector header is not consistent.
*/
static int _CalcStoreCRC_PSH(const NOR_BM_INST * pInst, NOR_BM_PSH * pPSH) {
  U8  crc;
  int r;

  r   = 0;                          // Set to indicate success.
  crc = _CalcPSH_CRC(pInst, pPSH);
  switch (pPSH->crcStat) {
  case CRC_STAT_INVALID:
    pPSH->crc0    = crc;
    pPSH->crcStat = CRC_STAT_VALID0;
    UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_PSH, crc0), sizeof(pPSH->crc0));
    UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_PSH, crcStat), sizeof(pPSH->crcStat));
    break;
  case CRC_STAT_VALID0:
    pPSH->crc1    = crc;
    pPSH->crcStat = CRC_STAT_VALID1;
    UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_PSH, crc1), sizeof(pPSH->crc1));
    UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_PSH, crcStat), sizeof(pPSH->crcStat));
    break;
  case CRC_STAT_VALID1:
    pPSH->crc2    = crc;
    pPSH->crcStat = CRC_STAT_VALID2;
    UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_PSH, crc2), sizeof(pPSH->crc2));
    UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_PSH, crcStat), sizeof(pPSH->crcStat));
    break;
  default:
    r = RESULT_INCONSISTENT_DATA;   // Error, the logical sector header is not consistent.
    break;
  }
  return r;
}

#if FAIL_SAFE_ERASE_NO_REWRITE

/*********************************************************************
*
*       _CalcStoreCRC_PSH_Data
*
*  Function description
*    Calculates the 8-bit CRC of the data block indicator and of the additional
*    values stored on the same flash line. The calculated CRC is stored
*    to physical sector header.
*
*  Return value
*    ==0      OK, CRC calculated and stored.
*    !=0      Physical sector header is not consistent.
*/
static int _CalcStoreCRC_PSH_Data(const NOR_BM_INST * pInst, NOR_BM_PSH * pPSH) {
  U8  crc;
  int r;

  FS_USE_PARA(pInst);
  if (pPSH->crcData != CRC_DRIVER_DATA_INVALID) {
    r = RESULT_INCONSISTENT_DATA;   // Error, the physical sector header is not consistent.
  } else {
    crc = _CalcPSH_CRC_Data(pPSH);
    pPSH->crcData = crc;
    UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_PSH, crcData), sizeof(pPSH->crcData));
    r = 0;                          // OK, CRC calculated and stored.
  }
  return r;
}

/*********************************************************************
*
*       _CalcStoreCRC_PSH_Work
*
*  Function description
*    Calculates the 8-bit CRC of the work block indicator and of the additional
*    values stored on the same flash line. The calculated CRC is stored
*    to physical sector header.
*
*  Return value
*    ==0      OK, CRC calculated and stored.
*    !=0      Physical sector header is not consistent.
*/
static int _CalcStoreCRC_PSH_Work(const NOR_BM_INST * pInst, NOR_BM_PSH * pPSH) {
  U8  crc;
  int r;

  FS_USE_PARA(pInst);
  if (pPSH->crcWork != CRC_DRIVER_DATA_INVALID) {
    r = RESULT_INCONSISTENT_DATA;   // Error, the physical sector header is not consistent.
  } else {
    crc = _CalcPSH_CRC_Work(pPSH);
    pPSH->crcWork = crc;
    UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_PSH, crcWork), sizeof(pPSH->crcWork));
    r = 0;                          // OK, CRC calculated and stored.
  }
  return r;
}

/*********************************************************************
*
*       _LoadVerifyCRC_PSH_Work
*
*  Function description
*    Verifies the data integrity of the flash line of physical sector header
*    that contains the work block indicator. The verification is performed
*    using CRC
*
*  Return value
*    ==0      OK, no bit errors detected.
*    !=0      Error, bit errors detected.
*/
static int _LoadVerifyCRC_PSH_Work(const NOR_BM_INST * pInst, const NOR_BM_PSH * pPSH) {
  int r;
  U8  crcCalc;

  FS_USE_PARA(pInst);
  r = 0;        // Set to indicate that no bit errors were detected.
  if ((pPSH->DataCntWork != DATA_CNT_INVALID) || (pPSH->lbiWork != LBI_INVALID)) {
    crcCalc = _CalcPSH_CRC_Work(pPSH);
    if (crcCalc != pPSH->crcWork) {
      r = RESULT_CRC_ERROR;
    }
  }
  return r;
}

/*********************************************************************
*
*       _LoadVerifyCRC_PSH_Data
*
*  Function description
*    Verifies the data integrity of the flash line of physical sector header
*    that contains the data block indicator. The verification is performed
*    using CRC
*
*  Return value
*    ==0      OK, no bit errors detected.
*    !=0      Error, bit errors detected.
*/
static int _LoadVerifyCRC_PSH_Data(const NOR_BM_INST * pInst, const NOR_BM_PSH * pPSH) {
  int r;
  U8  crcCalc;

  FS_USE_PARA(pInst);
  r = 0;        // Set to indicate that no bit errors were detected.
  if ((pPSH->DataCntData != DATA_CNT_INVALID) || (pPSH->lbiData != LBI_INVALID)) {
    crcCalc = _CalcPSH_CRC_Data(pPSH);
    if (crcCalc != pPSH->crcData) {
      r = RESULT_CRC_ERROR;
    }
  }
  return r;
}

#endif // FAIL_SAFE_ERASE_NO_REWRITE

/*********************************************************************
*
*       _LoadVerifyCRC_PSH
*
*  Function description
*    Verifies the data integrity of the entire physical sector header using CRC.
*
*  Return value
*    ==0      OK, no bit errors detected.
*    !=0      Error, bit errors detected.
*/
static int _LoadVerifyCRC_PSH(const NOR_BM_INST * pInst, const NOR_BM_PSH * pPSH) {
  U8  crc;
  U8  crcCalc;
  U8  DoVerify;
  int r;

  FS_USE_PARA(pInst);
  r        = 0;                           // Set to indicate success.
  DoVerify = 1;                           // Set to perform the CRC verification.
  crc      = CRC_DRIVER_DATA_INIT;
  switch (pPSH->crcStat) {
  case CRC_STAT_INVALID:
    if (_IsBlankPSH(pInst, pPSH) != 0) {
      DoVerify = 0;                       // CRC verification is not required since the phy. sector header is empty.
    }
    break;
  case CRC_STAT_VALID0:
    crc = pPSH->crc0;
    break;
  case CRC_STAT_VALID1:
    crc = pPSH->crc1;
    break;
  case CRC_STAT_VALID2:
    crc = pPSH->crc2;
    break;
  default:
    DoVerify = 0;
    r        = RESULT_INCONSISTENT_DATA;  // Error, the phy. sector header is not consistent.
    break;
  }
  if (DoVerify != 0u) {
    crcCalc = _CalcPSH_CRC(pInst, pPSH);
    if (crc != crcCalc) {
      r = RESULT_CRC_ERROR;
    }
  }
#if (FS_NOR_CAN_REWRITE == 0)
  if (r == 0) {
    //
    // The status flags are not protected by CRC to increase the performance.
    // Each flag occupies a single byte at the beginning of a flash line
    // and it can be either set to 0xFF if the flag is not set or to 0x00
    // if the flag is set (reversed logic).
    // We explicitly check here for these values and return a CRC error
    // in case the flag is not set to one of these values.
    //
    if (_IsRewriteSupported(pInst) == 0) {
      if ((pPSH->IsInvalid != 0xFFu) && (pPSH->IsInvalid != 0x00u)) {
        r = RESULT_CRC_ERROR;
      }
      if ((pPSH->IsValid != 0xFFu) && (pPSH->IsValid != 0x00u)) {
        r = RESULT_CRC_ERROR;
      }
      if ((pPSH->IsWork != 0xFFu) && (pPSH->IsWork != 0x00u)) {
        r = RESULT_CRC_ERROR;
      }
#if FAIL_SAFE_ERASE_NO_REWRITE
      if (r == 0) {
        if (pInst->FailSafeErase != 0u) {
          int IsWorkPresent;
          int IsDataPresent;

          if (pPSH->IsInvalid != 0x00u) {
            //
            // The data count and LBI are stored along with the indicators of a data
            // and work block if the physical sector is erased via a clean operation.
            // These values are not present if the physical sector is erased during a
            // write operation because the data count and the LBI are stored together
            // with the erase count to the original location in the first flash line
            // of the physical sector header. We verify the CRC only if the indicator
            // is set to 0x00 and the following 4 bytes are different than 0xFF.
            // In addition we verify that the LBI stored at original location is invalid
            // (i.e. set to 0xFFFF) if a valid LBI is stored along with a work or data block
            // indicator.
            //
            IsWorkPresent = 0;
            if (pPSH->IsWork == 0x00u) {
              if ((pPSH->DataCntWork != DATA_CNT_INVALID) || (pPSH->lbiWork != LBI_INVALID)) {
                r = _LoadVerifyCRC_PSH_Work(pInst, pPSH);
                if (r == 0) {
                  IsWorkPresent = 1;
                }
              }
            }
            IsDataPresent = 0;
            if (pPSH->IsValid == 0x00u) {
              if ((pPSH->DataCntData != DATA_CNT_INVALID) || (pPSH->lbiData != LBI_INVALID)) {
                r = _LoadVerifyCRC_PSH_Data(pInst, pPSH);
                if (r == 0) {
                  IsDataPresent = 1;
                } else {
                  if (IsWorkPresent != 0) {
                    //
                    // Ignore the CRC error if the information about the work block is valid
                    // because this may by the result of an interrupted in-place block conversion
                    // operation.
                    //
                    r = 0;
                  }
                }
              }
            }
            //
            // As additional sanity check we verify that the LBI stored at original location
            // is invalid when at least one of the other LBIs are valid.
            //
            if (r == 0) {
              if ((IsWorkPresent != 0) || (IsDataPresent != 0)) {
                if (pPSH->lbi != LBI_INVALID) {
                  r = RESULT_INCONSISTENT_DATA;
                } else {
                  if ((IsWorkPresent != 0) && (IsDataPresent != 0)) {
                    if (pPSH->lbiWork != pPSH->lbiData) {
                      r = RESULT_INCONSISTENT_DATA;
                    }
                  }
                }
              }
            }
          }
        }
      }
#endif // FAIL_SAFE_ERASE_NO_REWRITE
    }
  }
#endif // FS_NOR_CAN_REWRITE == 0
  return r;
}

/*********************************************************************
*
*       _CalcDataCRC
*
*  Function description
*    Calculates the CRC of the sector data.
*/
static U16 _CalcDataCRC(const U8 * pData, unsigned NumBytes, U16 crc) {
  U16 r;

  r = _pCRCHook->pfCalcCRC16(pData, NumBytes, crc);
  return r;
}

/*********************************************************************
*
*       _CRC_API
*/
static const NOR_BM_CRC_API _CRC_API = {
  _CalcStoreCRC_PSH,
  _LoadVerifyCRC_PSH,
  _CalcStoreCRC_LSH,
  _LoadVerifyCRC_LSH,
  _CalcDataCRC
#if FAIL_SAFE_ERASE_NO_REWRITE
  , _CalcStoreCRC_PSH_Data
  , _CalcStoreCRC_PSH_Work
  , _LoadVerifyCRC_PSH_Data
  , _LoadVerifyCRC_PSH_Work
#endif // FAIL_SAFE_ERASE_NO_REWRITE
};

#endif // FS_NOR_SUPPORT_CRC

#if FS_NOR_SUPPORT_ECC

/*********************************************************************
*
*       _CalcECC_LSH
*/
static void _CalcECC_LSH(const NOR_BM_INST * pInst, NOR_BM_LSH * pLSH, U8 * pECC) {
  U8  crc0;
#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  U16 brsi;
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER

  //
  // Save the value of the members that are not protected
  // by ECC or contain multiple bytes.
  //
  crc0       = pLSH->crc0;
  pLSH->crc0 = ECC_DRIVER_DATA_INVALID;   // Set to a known value.
#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  brsi       = pLSH->brsi;
  pLSH->brsi = _pMultiByteAPI->pfLoadU16(SEGGER_CONSTPTR2PTR(const U8, &pLSH->brsi));         // MISRA deviation D:100e
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  //
  // Calculate the ECC.
  //
  pInst->pECCHookMan->pfCalc(SEGGER_PTR2PTR(U32, pLSH), pECC);                                // MISRA deviation D:100e
  //
  // Restore the member values.
  //
  pLSH->crc0 = crc0;
#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  pLSH->brsi = brsi;
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
}

/*********************************************************************
*
*       _ApplyLSH_ECC
*
*  Function description
*    Verifies the ECC and corrects the bit errors.
*
*  Parameters
*    pInst      Driver instance.
*    pLSH       [IN] Read logical sector header.
*               [OUT] Corrected logical sector header.
*    pECC       [IN] Read ECC value.
*               [OUT] Corrected ECC value.
*
*  Return value
*    >=0      OK, number of bit errors corrected.
*    < 0      Uncorrectable bit errors occurred.
*/
static int _ApplyLSH_ECC(const NOR_BM_INST * pInst, NOR_BM_LSH * pLSH, U8 * pECC) {
  U8  crc0;
  int r;

  //
  // Save the value of the members that are not protected
  // by ECC or contain multiple bytes.
  //
  crc0       = pLSH->crc0;
  pLSH->crc0 = ECC_DRIVER_DATA_INVALID;   // Set to a known value.
#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  pLSH->brsi = _pMultiByteAPI->pfLoadU16(SEGGER_CONSTPTR2PTR(const U8, &pLSH->brsi));         // MISRA deviation D:100e
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  //
  // Verify the ECC and try to correct the bit errors.
  //
  r = pInst->pECCHookMan->pfApply(SEGGER_PTR2PTR(U32, pLSH), pECC);                           // MISRA deviation D:100e
  //
  // Restore the member values.
  //
  pLSH->crc0 = crc0;
#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  //
  // Convert the value back to the original byte ordering so
  // that we can preserve the eventual corrected bit errors.
  //
  pLSH->brsi = _pMultiByteAPI->pfLoadU16(SEGGER_CONSTPTR2PTR(const U8, &pLSH->brsi));         // MISRA deviation D:100e
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  return r;
}

/*********************************************************************
*
*       _Count1Bits
*
*  Function description
*    Returns the number of bits set to 1.
*
*  Parameters
*    Value    8-bit value to check.
*
*  Return value
*    Number of bits set to 1.
*/
static unsigned _Count1Bits(unsigned Value) {
  Value = (Value & 0x55uL) + ((Value & 0xAAuL) >> 1);
  Value = (Value & 0x33uL) + ((Value & 0xCCuL) >> 2);
  Value = (Value & 0x0FuL) + ((Value & 0xF0uL) >> 4);
  return Value;
}

/*********************************************************************
*
*       _LoadApplyECCStat
*
*  Function description
*    Corrects bit errors in the ECC status of PSH and LSH.
*
*  Parameters
*    pECCStat             [IN] ECC status to be corrected.
*                         [IN] Corrected ECC status.
*    pNumBitsCorrected    [OUT] Number of bits corrected. It can be set to NULL.
*
*  Return value
*    Corrected ECC status.
*
*  Additional information
*    The ECC status can take only 3 values: ECC_STAT_EMPTY, ECC_STAT_VALID and ECC_STAT_INVALID.
*    We use this information to correct eventual bit errors by comparing with these values.
*    If the status is not equal to any of these values then we count
*    the number of bits set to 1 to decide which value it may be.
*/
static unsigned _LoadApplyECCStat(U8 * pECCStat, int * pNumBitsCorrected) {
  unsigned NumBits;
  unsigned eccStat;
  unsigned eccStatCorrected;
  unsigned eccStatXOR;
  int      NumBitsCorrected;

  eccStat          = *pECCStat;
  eccStatCorrected = eccStat;
  NumBitsCorrected = 0;
  if (   (eccStat != ECC_STAT_EMPTY)
      && (eccStat != ECC_STAT_VALID)
      && (eccStat != ECC_STAT_INVALID)) {
    //
    // Not an expected value. Count the number of bits set to 1.
    //
    NumBits = _Count1Bits(eccStat);
    if (NumBits <= 2u) {
      eccStatCorrected = ECC_STAT_INVALID;       // This status has all bits set to 0.
    } else {
      if (NumBits <= 6u) {
        eccStatCorrected = ECC_STAT_VALID;       // This status has 4 bits set to 1.
      } else {
        eccStatCorrected = ECC_STAT_EMPTY;       // This status has all the bits set to 1.
      }
    }
    if (pNumBitsCorrected != NULL) {
      //
      // Calculate the number of bits corrected.
      //
      eccStatXOR = eccStat ^ eccStatCorrected;
      NumBitsCorrected = (int)_Count1Bits(eccStatXOR);
    }
  }
  if (pNumBitsCorrected != NULL) {
    *pNumBitsCorrected = NumBitsCorrected;
  }
  *pECCStat = (U8)eccStatCorrected;
  return eccStatCorrected;
}

/*********************************************************************
*
*       _CalcStoreECC_LSH
*
*  Function description
*    Calculates the ECC of the management data stored in a logical sector header.
*    The calculated ECC is stored to logical sector header.
*
*  Parameters
*    pInst              Driver instance.
*    pLSH               Header of the logical sector.
*    pNumBitsCorrected  [OUT] Number of bit errors corrected.
*
*  Return value
*    ==0      OK, ECC calculated and stored.
*    !=0      Logical sector header is not consistent.
*/
static int _CalcStoreECC_LSH(const NOR_BM_INST * pInst, NOR_BM_LSH * pLSH, int * pNumBitsCorrected) {
  int      r;
  unsigned eccStat;

  r       = 0;                            // Set to indicate success.
  eccStat = _LoadApplyECCStat(&pLSH->ecc0Stat, pNumBitsCorrected);
  if (eccStat == ECC_STAT_EMPTY) {
    _CalcECC_LSH(pInst, pLSH, pLSH->abECC0);
    pLSH->ecc0Stat = ECC_STAT_VALID;
    UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_LSH, abECC0), sizeof(pLSH->abECC0));
    UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_LSH, ecc0Stat), sizeof(pLSH->ecc0Stat));
  } else {
    if (eccStat == ECC_STAT_VALID) {
      _CalcECC_LSH(pInst, pLSH, pLSH->abECC1);
      pLSH->ecc0Stat = ECC_STAT_INVALID;
      pLSH->ecc1Stat = ECC_STAT_VALID;
      UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_LSH, abECC1), sizeof(pLSH->abECC1));
      UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_LSH, ecc0Stat), sizeof(pLSH->ecc0Stat));
      UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_LSH, ecc1Stat), sizeof(pLSH->ecc1Stat));
    } else {
       r = RESULT_INCONSISTENT_DATA;      // Error, the logical sector header is not consistent.
    }
  }
  return r;
}

#if FAIL_SAFE_ERASE_NO_REWRITE

/*********************************************************************
*
*       _IsBlankECC_LSH_Data
*
*  Function description
*    Checks if all the bytes in the ECC that protects the data valid
*    indicator set to 0xFF.
*
*  Parameters
*    pLSH     Logical sector header.
*
*  Return value
*    ==1      Is empty.
*    ==0      Is not empty.
*/
static int _IsBlankECC_LSH_Data(const NOR_BM_LSH * pLSH) {
  const U8 * p;
  unsigned   NumBytes;
  int        r;

  r        = 1;                                   // Assume that all the bytes are set to 0xFF.
  p        = pLSH->abECCData;
  NumBytes = FS_NOR_MAX_NUM_BYTES_ECC_MAN + 1;    // +1 because we also check the value of the eccStatData member.
  do {
    if (*p++ != 0xFFu) {
      r = 0;            // Not empty.
      break;
    }
  } while (--NumBytes != 0u);
  return r;
}

/*********************************************************************
*
*       _IsBlankECC_LSH_Invalid
*
*  Function description
*    Checks if all the bytes in the ECC that protects the invalid data
*    indicator are set to 0xFF.
*
*  Parameters
*    pLSH     Logical sector header.
*
*  Return value
*    ==1      Is empty.
*    ==0      Is not empty.
*/
static int _IsBlankECC_LSH_Invalid(const NOR_BM_LSH * pLSH) {
  const U8 * p;
  unsigned   NumBytes;
  int        r;

  r        = 1;                                   // Assume that all the bytes are set to 0xFF.
  p        = pLSH->abECCInvalid;
  NumBytes = FS_NOR_MAX_NUM_BYTES_ECC_MAN + 1;    // +1 because we also check the value of the eccStatInvalid member.
  do {
    if (*p++ != 0xFFu) {
      r = 0;            // Not empty.
      break;
    }
  } while (--NumBytes != 0u);
  return r;
}

/*********************************************************************
*
*       _LoadApplyECCStatEx
*
*  Function description
*    Corrects bit errors in the ECC status of PSH and LSH.
*
*  Parameters
*    pECCStat             [IN] ECC status to be corrected.
*                         [IN] Corrected ECC status.
*    pNumBitsCorrected    [OUT] Number of bits corrected. It can be set to NULL.
*
*  Return value
*    Corrected ECC status.
*
*  Additional information
*    The ECC status can take only 2 values: ECC_STAT_EMPTY and ECC_STAT_VALID_EX.
*    We use this information to correct eventual bit errors by comparing with these values.
*    If the status is not equal to any of these values then we count
*    the number of bits set to 1 to decide which value it may be.
*/
static unsigned _LoadApplyECCStatEx(U8 * pECCStat, int * pNumBitsCorrected) {
  unsigned NumBits;
  unsigned eccStat;
  unsigned eccStatCorrected;
  unsigned eccStatXOR;
  int      NumBitsCorrected;

  eccStat          = *pECCStat;
  eccStatCorrected = eccStat;
  NumBitsCorrected = 0;
  if (   (eccStat != ECC_STAT_EMPTY)
      && (eccStat != ECC_STAT_VALID_EX)) {
    //
    // Not an expected value. Count the number of bits set to 1.
    //
    NumBits = _Count1Bits(eccStat);
    if (NumBits < 4u) {
      eccStatCorrected = ECC_STAT_VALID_EX;       // This status has all bits set to 0.
    } else {
      eccStatCorrected = ECC_STAT_EMPTY;          // This status has all the bits set to 1.
    }
    if (pNumBitsCorrected != NULL) {
      //
      // Calculate the number of bits corrected.
      //
      eccStatXOR = eccStat ^ eccStatCorrected;
      NumBitsCorrected = (int)_Count1Bits(eccStatXOR);
    }
  }
  if (pNumBitsCorrected != NULL) {
    *pNumBitsCorrected = NumBitsCorrected;
  }
  *pECCStat = (U8)eccStatCorrected;
  return eccStatCorrected;
}

/*********************************************************************
*
*       _CalcStoreECC_LSH_Data
*
*  Function description
*    Calculates the ECC of the data valid indicator.
*    The calculated ECC is stored to logical sector header.
*
*  Parameters
*    pInst    Driver instance.
*    pLSH     Logical sector header.
*
*  Return value
*    ==0      OK, ECC calculated and stored.
*    !=0      Physical sector header is not consistent.
*/
static int _CalcStoreECC_LSH_Data(const NOR_BM_INST * pInst, NOR_BM_LSH * pLSH) {
  int r;

  if (_IsBlankECC_LSH_Data(pLSH) == 0) {
    r = RESULT_INCONSISTENT_DATA;   // Error, the logical sector header is not consistent.
  } else {
    //
    // Calculate the ECC.
    //
    pInst->pECCHookMan->pfCalc(SEGGER_PTR2PTR(U32, &pLSH->IsValid), pLSH->abECCData);               // MISRA deviation D:100e
    UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_LSH, abECCData), sizeof(pLSH->abECCData));
    pLSH->eccStatData = 0;          // Indicate that the ECC is valid.
    UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_LSH, eccStatData), sizeof(pLSH->eccStatData));
    r = 0;                          // OK, ECC calculated and stored.
  }
  return r;
}

/*********************************************************************
*
*       _CalcStoreECC_LSH_Invalid
*
*  Function description
*    Calculates the ECC of the invalid block indicator.
*    The calculated ECC is stored to logical sector header.
*
*  Parameters
*    pInst    Driver instance.
*    pLSH     Logical sector header.
*
*  Return value
*    ==0      OK, ECC calculated and stored.
*    !=0      Physical sector header is not consistent.
*/
static int _CalcStoreECC_LSH_Invalid(const NOR_BM_INST * pInst, NOR_BM_LSH * pLSH) {
  int r;

  if (_IsBlankECC_LSH_Invalid(pLSH) == 0) {
    r = RESULT_INCONSISTENT_DATA;   // Error, the logical sector header is not consistent.
  } else {
    //
    // Calculate the ECC.
    //
    pInst->pECCHookMan->pfCalc(SEGGER_PTR2PTR(U32, &pLSH->IsInvalid), pLSH->abECCInvalid);          // MISRA deviation D:100e
    UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_LSH, abECCInvalid), sizeof(pLSH->abECCInvalid));
    pLSH->eccStatInvalid = 0;       // Indicate that the ECC is valid.
    UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_LSH, eccStatInvalid), sizeof(pLSH->eccStatInvalid));
    r = 0;                          // OK, ECC calculated and stored.
  }
  return r;
}

/*********************************************************************
*
*       _LoadApplyECC_LSH_Data
*
*  Function description
*    Verifies the data integrity of the flash line of logical sector header
*    that contains the valid data indicator. The verification is performed
*    using ECC.
*
*  Parameters
*    pInst              Driver instance.
*    pLSH               Header of logical sector.
*    pNumBitsCorrected  [OUT] Number of bit errors corrected. Can be set to NULL.
*
*  Return value
*    ==0      OK, no bit errors detected or bit errors detected and corrected.
*    !=0      Error, uncorrectable bit errors detected.
*/
static int _LoadApplyECC_LSH_Data(const NOR_BM_INST * pInst, NOR_BM_LSH * pLSH, int * pNumBitsCorrected) {
  int      r;
  int      Result;
  int      NumBitsCorrected;
  unsigned eccStat;

  r                = 0;                   // Set to indicate that no uncorrectable bit errors occurred.
  NumBitsCorrected = 0;
  eccStat = _LoadApplyECCStatEx(&pLSH->eccStatData, &NumBitsCorrected);
  if (eccStat == ECC_STAT_EMPTY) {
    if (_IsBlankECC_LSH_Data(pLSH) == 0) {
      r = RESULT_INCONSISTENT_DATA;       // Error, the phy. sector header is not consistent.
    }
    if (pLSH->IsValid != 0xFFu) {
      r = RESULT_INCONSISTENT_DATA;       // Error, the phy. sector header is not consistent.
    }
  } else {
    if (eccStat == ECC_STAT_VALID_EX) {
      Result = pInst->pECCHookMan->pfApply(SEGGER_PTR2PTR(U32, &pLSH->IsValid), pLSH->abECCData);     // MISRA deviation D:100e
      if (Result < 0) {
        r = RESULT_ECC_ERROR;
      } else {
        NumBitsCorrected += Result;
      }
    } else {
      r = RESULT_INCONSISTENT_DATA;       // Error, the phy. sector header is not consistent.
    }
  }
  if (pNumBitsCorrected != NULL) {
    *pNumBitsCorrected = NumBitsCorrected;
  }
  return r;
}

/*********************************************************************
*
*       _LoadApplyECC_LSH_Invalid
*
*  Function description
*    Verifies the data integrity of the flash line of logical sector header
*    that contains the invalid data indicator. The verification is performed
*    using ECC.
*
*  Parameters
*    pInst              Driver instance.
*    pLSH               Header of logical sector.
*    pNumBitsCorrected  [OUT] Number of bit errors corrected. Can be set to NULL.
*
*  Return value
*    ==0      OK, no bit errors detected or bit errors detected and corrected.
*    !=0      Error, uncorrectable bit errors detected.
*/
static int _LoadApplyECC_LSH_Invalid(const NOR_BM_INST * pInst, NOR_BM_LSH * pLSH, int * pNumBitsCorrected) {
  int      r;
  int      Result;
  int      NumBitsCorrected;
  unsigned eccStat;

  r                = 0;                   // Set to indicate that no uncorrectable bit errors occurred.
  NumBitsCorrected = 0;
  eccStat = _LoadApplyECCStatEx(&pLSH->eccStatInvalid, &NumBitsCorrected);
  if (eccStat == ECC_STAT_EMPTY) {
    if (_IsBlankECC_LSH_Invalid(pLSH) == 0) {
      r = RESULT_INCONSISTENT_DATA;       // Error, the phy. sector header is not consistent.
    }
    if (pLSH->IsInvalid != 0xFFu) {
      r = RESULT_INCONSISTENT_DATA;       // Error, the phy. sector header is not consistent.
    }
  } else {
    if (eccStat == ECC_STAT_VALID_EX) {
      Result = pInst->pECCHookMan->pfApply(SEGGER_PTR2PTR(U32, &pLSH->IsInvalid), pLSH->abECCInvalid);    // MISRA deviation D:100e
      if (Result < 0) {
        r = RESULT_ECC_ERROR;
      } else {
        NumBitsCorrected += Result;
      }
    } else {
      r = RESULT_INCONSISTENT_DATA;       // Error, the phy. sector header is not consistent.
    }
  }
  if (pNumBitsCorrected != NULL) {
    *pNumBitsCorrected = NumBitsCorrected;
  }
  return r;
}

#endif // FAIL_SAFE_ERASE_NO_REWRITE

/*********************************************************************
*
*       _LoadApplyECC_LSH
*
*  Function description
*    Verifies and corrects the data integrity of the logical sector header using ECC.
*
*  Parameters
*    pInst              Driver instance.
*    pLSH               Logical sector header.
*    pNumBitsCorrected  [OUT] Number of bit errors corrected. Can be set to NULL.
*
*  Return value
*    ==0      No bit errors or correctable bit errors occurred.
*    !=0      An error occurred including an uncorrectable bit error.
*/
static int _LoadApplyECC_LSH(const NOR_BM_INST * pInst, NOR_BM_LSH * pLSH, int * pNumBitsCorrected) {
  U8       * pECC;
  int        r;
  int        Result;
  unsigned   eccStat;
  int        NumBitsCorrected;
  int        NumBitsCorrectedTotal;

  r                     = 0;                  // Set to indicate success.
  pECC                  = NULL;
  NumBitsCorrectedTotal = 0;
  NumBitsCorrected      = 0;
  eccStat = _LoadApplyECCStat(&pLSH->ecc0Stat, &NumBitsCorrected);
  NumBitsCorrectedTotal += NumBitsCorrected;
  if (eccStat == ECC_STAT_EMPTY) {
    if (_IsBlankLSH(pLSH) == 0) {
#if (FS_NOR_OPTIMIZE_DATA_WRITE == 0)
      r = RESULT_INCONSISTENT_DATA;           // Error, the logical sector header is not consistent.
#else
      //
      // A write operation that stores the sector data directly to a data block
      // writes all the relevant management data to the header of the logical sector
      // and marks it as valid at the beginning of the write operation. Only the ECC is
      // not marked as valid to indicate that the operation is still in progress.
      // We try to recover from an interrupted write operation here by checking if the
      // data status is valid. If yes, then we return that the logical
      // sector is invalid to prevent that we write any data into it.
      //
      if (pLSH->DataStat == DATA_STAT_VALID) {
        pLSH->DataStat = DATA_STAT_INVALID;
        pLSH->ecc0Stat = ECC_STAT_INVALID;
      } else {
        r = RESULT_INCONSISTENT_DATA;         // Error, the logical sector header is not consistent.
      }
#endif // FS_NOR_OPTIMIZE_DATA_WRITE == 0
    }
  } else {
    if (eccStat == ECC_STAT_VALID) {
      pECC = pLSH->abECC0;
    } else {
      NumBitsCorrected = 0;
      eccStat = _LoadApplyECCStat(&pLSH->ecc1Stat, &NumBitsCorrected);
      if (eccStat == ECC_STAT_VALID) {
        NumBitsCorrectedTotal += NumBitsCorrected;
        pECC = pLSH->abECC1;
      } else {
        if (eccStat == ECC_STAT_EMPTY) {
          //
          // Try to recover from an interrupted write operation that marked the logical sector as invalid.
          // In this case the ECC is validated via a write operation to the status of the second ECC.
          // For this reason we mark the second ECC as valid and let the ECC routine check for bit errors.
          //
          if ((pLSH->DataStat == DATA_STAT_VALID) || (pLSH->DataStat == DATA_STAT_INVALID)) {
            pLSH->DataStat = DATA_STAT_INVALID;
            pLSH->ecc1Stat = ECC_STAT_VALID;
            pECC           = pLSH->abECC1;
          } else {
            r = RESULT_INCONSISTENT_DATA;     // Error, the logical sector header is not consistent.
          }
        }
      }
    }
  }
  if (pECC != NULL) {
    Result = _ApplyLSH_ECC(pInst, pLSH, pECC);
    if (Result < 0) {
      r = RESULT_ECC_ERROR;
    } else {
      NumBitsCorrectedTotal += Result;
    }
  }
#if FAIL_SAFE_ERASE_NO_REWRITE
  if (r == 0) {
    if (_IsRewriteSupported(pInst) == 0) {
      int IsInvalid;

      IsInvalid = 0;
      //
      // Check first if the logical sector is marked as invalid.
      //
      NumBitsCorrected = 0;
      r = _LoadApplyECC_LSH_Invalid(pInst, pLSH, &NumBitsCorrected);
      if (r == 0) {
        NumBitsCorrectedTotal += NumBitsCorrected;
        if (pLSH->IsInvalid == 0x00u) {             // Reversed logic: 0x00 means invalid, 0xFF means not invalid.
          IsInvalid = 1;
        } else {
          if (pLSH->IsInvalid != 0xFFu) {           // Additional sanity check.
            r = RESULT_INCONSISTENT_DATA;
          }
        }
      }
      if (r == 0) {
        if (IsInvalid == 0) {
          NumBitsCorrected = 0;
          r = _LoadApplyECC_LSH_Data(pInst, pLSH, &NumBitsCorrected);
          if (r == 0) {
            NumBitsCorrectedTotal += NumBitsCorrected;
            if (   (pLSH->IsValid != 0xFFu)
                && (pLSH->IsValid != 0x00u)) {      // Additional sanity check.
              r = RESULT_INCONSISTENT_DATA;
            }
          }
        }
      }
    }
  }
#endif // FAIL_SAFE_ERASE_NO_REWRITE
  if (pNumBitsCorrected != NULL) {
    *pNumBitsCorrected = NumBitsCorrectedTotal;
  }
  return r;
}

/*********************************************************************
*
*       _CalcPSH_ECC
*/
static void _CalcPSH_ECC(const NOR_BM_INST * pInst, NOR_BM_PSH * pPSH, U8 * pECC) {
#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  U16 lbi;
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER

#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  //
  // Save the value of the members that contain multiple bytes.
  //
  lbi       = pPSH->lbi;
  pPSH->lbi = _pMultiByteAPI->pfLoadU16(SEGGER_CONSTPTR2PTR(const U8, &pPSH->lbi));           // MISRA deviation D:100e
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  //
  // Calculate the ECC.
  //
  pInst->pECCHookMan->pfCalc(SEGGER_PTR2PTR(U32, pPSH), pECC);                                // MISRA deviation D:100e
#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  //
  // Restore the member values.
  //
  pPSH->lbi = lbi;
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
}

/*********************************************************************
*
*       _ApplyPSH_ECC
*/
static int _ApplyPSH_ECC(const NOR_BM_INST * pInst, NOR_BM_PSH * pPSH, U8 * pECC) {
  int r;

#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  pPSH->lbi = _pMultiByteAPI->pfLoadU16(SEGGER_CONSTPTR2PTR(const U8, &pPSH->lbi));           // MISRA deviation D:100e
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  //
  // Verify the ECC and try to correct the bit errors.
  //
  r = pInst->pECCHookMan->pfApply(SEGGER_PTR2PTR(U32, pPSH), pECC);                           // MISRA deviation D:100e
#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  //
  // Convert the value back to the original byte ordering so
  // that we can preserve the eventual corrected bit errors.
  //
  pPSH->lbi = _pMultiByteAPI->pfLoadU16(SEGGER_CONSTPTR2PTR(const U8, &pPSH->lbi));           // MISRA deviation D:100e
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  return r;
}

/*********************************************************************
*
*       _CalcStoreECC_PSH
*
*  Function description
*    Calculates the ECC of the physical sector header.
*    The calculated ECC is stored to physical sector header.
*
*  Parameters
*    pInst                Driver instance.
*    pPSH                 Header of the physical sector.
*    pNumBitsCorrected    Number of bit errors corrected.
*
*  Return value
*    ==0      OK, ECC calculated and stored.
*    !=0      Physical sector header is not consistent.
*/
static int _CalcStoreECC_PSH(const NOR_BM_INST * pInst, NOR_BM_PSH * pPSH, int * pNumBitsCorrected) {
  int      r;
  unsigned eccStat;

  r       = 0;                        // Set to indicate success.
  eccStat = _LoadApplyECCStat(&pPSH->ecc0Stat, pNumBitsCorrected);
  if (eccStat == ECC_STAT_EMPTY) {
    _CalcPSH_ECC(pInst, pPSH, pPSH->abECC0);
    pPSH->ecc0Stat = ECC_STAT_VALID;
    UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_PSH, abECC0), sizeof(pPSH->abECC0));
    UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_PSH, ecc0Stat), sizeof(pPSH->ecc0Stat));
  } else {
    if (eccStat == ECC_STAT_VALID) {
      _CalcPSH_ECC(pInst, pPSH, pPSH->abECC1);
      pPSH->ecc0Stat = ECC_STAT_INVALID;
      pPSH->ecc1Stat = ECC_STAT_VALID;
      UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_PSH, abECC1), sizeof(pPSH->abECC1));
      UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_PSH, ecc0Stat), sizeof(pPSH->ecc0Stat));
      UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_PSH, ecc1Stat), sizeof(pPSH->ecc1Stat));
    } else {
      r = RESULT_INCONSISTENT_DATA;   // Error, the logical sector header is not consistent.
    }
  }
  return r;
}

#if FAIL_SAFE_ERASE_NO_REWRITE

/*********************************************************************
*
*       _IsBlankECC_PSH_Data
*
*  Function description
*    Checks if all the bytes in the ECC that protects the valid indicator set to 0xFF.
*
*  Parameters
*    pPSH     Physical sector header.
*
*  Return value
*    ==1      Is empty.
*    ==0      Is not empty.
*/
static int _IsBlankECC_PSH_Data(const NOR_BM_PSH * pPSH) {
  const U8 * p;
  unsigned   NumBytes;
  int        r;

  r        = 1;         // Assume that all the bytes are set to 0xFF.
  p        = pPSH->abECCData;
  NumBytes = FS_NOR_MAX_NUM_BYTES_ECC_MAN + 1;    // +1 because we also check the value of the eccStatData member.
  do {
    if (*p++ != 0xFFu) {
      r = 0;            // Not empty.
      break;
    }
  } while (--NumBytes != 0u);
  return r;
}

/*********************************************************************
*
*       _IsBlankECC_PSH_Work
*
*  Function description
*    Checks if all the bytes in the ECC that protects the work block
*    indicator are set to 0xFF.
*
*  Parameters
*    pPSH     Physical sector header.
*
*  Return value
*    ==1      Is empty.
*    ==0      Is not empty.
*/
static int _IsBlankECC_PSH_Work(const NOR_BM_PSH * pPSH) {
  const U8 * p;
  unsigned   NumBytes;
  int        r;

  r        = 1;                                   // Assume that all the bytes are set to 0xFF.
  p        = pPSH->abECCWork;
  NumBytes = FS_NOR_MAX_NUM_BYTES_ECC_MAN + 1;    // +1 because we also check the value of the eccStatWork member.
  do {
    if (*p++ != 0xFFu) {
      r = 0;            // Not empty.
      break;
    }
  } while (--NumBytes != 0u);
  return r;
}

/*********************************************************************
*
*       _IsBlankECC_PSH_Invalid
*
*  Function description
*    Checks if all the bytes in the ECC that protects the invalid block
*    indicator are set to 0xFF.
*
*  Parameters
*    pPSH     Physical sector header.
*
*  Return value
*    ==1      Is empty.
*    ==0      Is not empty.
*/
static int _IsBlankECC_PSH_Invalid(const NOR_BM_PSH * pPSH) {
  const U8 * p;
  unsigned   NumBytes;
  int        r;

  r        = 1;                                   // Assume that all the bytes are set to 0xFF.
  p        = pPSH->abECCInvalid;
  NumBytes = FS_NOR_MAX_NUM_BYTES_ECC_MAN + 1;    // +1 because we also check the value of the eccStatInvalid member.
  do {
    if (*p++ != 0xFFu) {
      r = 0;            // Not empty.
      break;
    }
  } while (--NumBytes != 0u);
  return r;
}

/*********************************************************************
*
*       _CalcStoreECC_PSH_Data
*
*  Function description
*    Calculates the ECC of the data block indicator and of the additional
*    values stored on the same flash line. The calculated ECC is stored
*    to physical sector header.
*
*  Return value
*    ==0      OK, ECC calculated and stored.
*    !=0      Physical sector header is not consistent.
*/
static int _CalcStoreECC_PSH_Data(const NOR_BM_INST * pInst, NOR_BM_PSH * pPSH) {
  int r;
#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  U16 lbi;
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER

  if (_IsBlankECC_PSH_Data(pPSH) == 0) {
    r = RESULT_INCONSISTENT_DATA;   // Error, the physical sector header is not consistent.
  } else {
#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
    //
    // Save the value of the members that contain multiple bytes.
    //
    lbi           = pPSH->lbiData;
    pPSH->lbiData = _pMultiByteAPI->pfLoadU16(SEGGER_CONSTPTR2PTR(const U8, &pPSH->lbiData)); // MISRA deviation D:100e
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
    //
    // Calculate the ECC.
    //
    pInst->pECCHookMan->pfCalc(SEGGER_PTR2PTR(U32, &pPSH->IsValid), pPSH->abECCData);         // MISRA deviation D:100e
#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
    //
    // Restore the member values.
    //
    pPSH->lbiData = lbi;
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
    UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_PSH, abECCData), sizeof(pPSH->abECCData));
    pPSH->eccStatData = 0;          // Indicate that the ECC is valid.
    UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_PSH, eccStatData), sizeof(pPSH->eccStatData));
    r = 0;                          // OK, ECC calculated and stored.
  }
  return r;
}

/*********************************************************************
*
*       _CalcStoreECC_PSH_Work
*
*  Function description
*    Calculates the ECC of the work block indicator and of the additional
*    values stored on the same flash line. The calculated ECC is stored
*    to physical sector header.
*
*  Return value
*    ==0      OK, ECC calculated and stored.
*    !=0      Physical sector header is not consistent.
*/
static int _CalcStoreECC_PSH_Work(const NOR_BM_INST * pInst, NOR_BM_PSH * pPSH) {
  int r;
#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  U16 lbi;
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER

  if (_IsBlankECC_PSH_Work(pPSH) == 0) {
    r = RESULT_INCONSISTENT_DATA;   // Error, the physical sector header is not consistent.
  } else {
#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
    //
    // Save the value of the members that contain multiple bytes.
    //
    lbi           = pPSH->lbiWork;
    pPSH->lbiWork = _pMultiByteAPI->pfLoadU16(SEGGER_CONSTPTR2PTR(const U8, &pPSH->lbiWork)); // MISRA deviation D:100e
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
    //
    // Calculate the ECC.
    //
    pInst->pECCHookMan->pfCalc(SEGGER_PTR2PTR(U32, &pPSH->IsWork), pPSH->abECCWork);          // MISRA deviation D:100e
#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
    //
    // Restore the member values.
    //
    pPSH->lbiWork = lbi;
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
    UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_PSH, abECCWork), sizeof(pPSH->abECCWork));
    pPSH->eccStatWork = 0;          // Indicate that the ECC is valid.
    UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_PSH, eccStatWork), sizeof(pPSH->eccStatWork));
    r = 0;                          // OK, ECC calculated and stored.
  }
  return r;
}

/*********************************************************************
*
*       _CalcStoreECC_PSH_Invalid
*
*  Function description
*    Calculates the ECC of the invalid block indicator.
*    The calculated ECC is stored to physical sector header.
*
*  Return value
*    ==0      OK, ECC calculated and stored.
*    !=0      Physical sector header is not consistent.
*/
static int _CalcStoreECC_PSH_Invalid(const NOR_BM_INST * pInst, NOR_BM_PSH * pPSH) {
  int r;

  if (_IsBlankECC_PSH_Invalid(pPSH) == 0) {
    r = RESULT_INCONSISTENT_DATA;   // Error, the physical sector header is not consistent.
  } else {
    //
    // Calculate the ECC.
    //
    pInst->pECCHookMan->pfCalc(SEGGER_PTR2PTR(U32, &pPSH->IsInvalid), pPSH->abECCInvalid);          // MISRA deviation D:100e
    UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_PSH, abECCInvalid), sizeof(pPSH->abECCInvalid));
    pPSH->eccStatInvalid = 0;       // Indicate that the ECC is valid.
    UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_PSH, eccStatInvalid), sizeof(pPSH->eccStatInvalid));
    r = 0;                          // OK, ECC calculated and stored.
  }
  return r;
}

/*********************************************************************
*
*       _LoadApplyECC_PSH_Data
*
*  Function description
*    Verifies the data integrity of the flash line of physical sector header
*    that contains the data block indicator. The verification is performed
*    using ECC.
*
*  Parameters
*    pInst              Driver instance.
*    pPSH               Header of physical sector.
*    pNumBitsCorrected  [OUT] Number of bit errors corrected. Can be set to NULL.
*
*  Return value
*    ==0      OK, no bit errors detected or bit errors detected and corrected.
*    !=0      Error, uncorrectable bit errors detected.
*/
static int _LoadApplyECC_PSH_Data(const NOR_BM_INST * pInst, NOR_BM_PSH * pPSH, int * pNumBitsCorrected) {
  int      r;
  int      Result;
#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  U16      lbi;
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  int      NumBitsCorrected;
  unsigned eccStat;

  r                = 0;                   // Set to indicate that no uncorrectable bit errors occurred.
  NumBitsCorrected = 0;
  eccStat = _LoadApplyECCStatEx(&pPSH->eccStatData, &NumBitsCorrected);
  if (eccStat == ECC_STAT_EMPTY) {
    if (_IsBlankECC_PSH_Data(pPSH) == 0) {
      r = RESULT_INCONSISTENT_DATA;       // Error, the phy. sector header is not consistent.
    }
    if (   (pPSH->IsValid     != 0xFFu)
        || (pPSH->DataCntData != 0xFFu)
        || (pPSH->lbiData     != 0xFFFFu)) {
      r = RESULT_INCONSISTENT_DATA;       // Error, the phy. sector header is not consistent.
    }
  } else {
    if (eccStat == ECC_STAT_VALID_EX) {
#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
      //
      // Save the value of the members that contain multiple bytes.
      //
      lbi           = pPSH->lbiData;
      pPSH->lbiData = _pMultiByteAPI->pfLoadU16(SEGGER_CONSTPTR2PTR(const U8, &pPSH->lbiData));       // MISRA deviation D:100e
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
      Result = pInst->pECCHookMan->pfApply(SEGGER_PTR2PTR(U32, &pPSH->IsValid), pPSH->abECCData);     // MISRA deviation D:100e
#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
      //
      // Restore the member values.
      //
      pPSH->lbiData = lbi;
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
      if (Result < 0) {
        r = RESULT_ECC_ERROR;
      } else {
        NumBitsCorrected += Result;
      }
    } else {
      r = RESULT_INCONSISTENT_DATA;       // Error, the phy. sector header is not consistent.
    }
  }
  if (pNumBitsCorrected != NULL) {
    *pNumBitsCorrected = NumBitsCorrected;
  }
  return r;
}

/*********************************************************************
*
*       _LoadApplyECC_PSH_Work
*
*  Function description
*    Verifies the data integrity of the flash line of physical sector header
*    that contains the work block indicator. The verification is performed
*    using ECC.
*
*  Parameters
*    pInst              Driver instance.
*    pPSH               Header of the physical sector.
*    pNumBitsCorrected  [OUT] Number of bit errors corrected.
*
*  Return value
*    ==0      OK, no bit errors detected or bit errors detected and corrected.
*    !=0      Error, uncorrectable bit errors detected.
*/
static int _LoadApplyECC_PSH_Work(const NOR_BM_INST * pInst, NOR_BM_PSH * pPSH, int * pNumBitsCorrected) {
  int      r;
  int      Result;
#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  U16      lbi;
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  int      NumBitsCorrected;
  unsigned eccStat;

  r                = 0;                   // Set to indicate that no uncorrectable bit errors occurred.
  NumBitsCorrected = 0;
  eccStat = _LoadApplyECCStatEx(&pPSH->eccStatWork, &NumBitsCorrected);
  if (eccStat == ECC_STAT_EMPTY) {
    if (_IsBlankECC_PSH_Work(pPSH) == 0) {
      r = RESULT_INCONSISTENT_DATA;       // Error, the phy. sector header is not consistent.
    }
    if (   (pPSH->IsWork      != 0xFFu)
        || (pPSH->DataCntWork != 0xFFu)
        || (pPSH->lbiWork     != 0xFFFFu)) {
      r = RESULT_INCONSISTENT_DATA;       // Error, the phy. sector header is not consistent.
    }
  } else {
    if (eccStat == ECC_STAT_VALID_EX) {
#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
      //
      // Save the value of the members that contain multiple bytes.
      //
      lbi           = pPSH->lbiWork;
      pPSH->lbiWork = _pMultiByteAPI->pfLoadU16(SEGGER_CONSTPTR2PTR(const U8, &pPSH->lbiWork));       // MISRA deviation D:100e
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
      Result = pInst->pECCHookMan->pfApply(SEGGER_PTR2PTR(U32, &pPSH->IsWork), pPSH->abECCWork);      // MISRA deviation D:100e
#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
      //
      // Restore the member values.
      //
      pPSH->lbiWork = lbi;
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
      if (Result < 0) {
        r = RESULT_ECC_ERROR;
      } else {
        NumBitsCorrected += Result;
      }
    } else {
      r = RESULT_INCONSISTENT_DATA;       // Error, the phy. sector header is not consistent.
    }
  }
  if (pNumBitsCorrected != NULL) {
    *pNumBitsCorrected = NumBitsCorrected;
  }
  return r;
}

/*********************************************************************
*
*       _LoadApplyECC_PSH_Invalid
*
*  Function description
*    Verifies the data integrity of the flash line of physical sector header
*    that contains the invalid block indicator. The verification is performed
*    using ECC.
*
*  Parameters
*    pInst              Driver instance.
*    pPSH               Header of the physical sector.
*    pNumBitsCorrected  [OUT] Number of bit errors corrected.
*
*  Return value
*    ==0      OK, no bit errors detected or bit errors detected and corrected.
*    !=0      Error, uncorrectable bit errors detected.
*/
static int _LoadApplyECC_PSH_Invalid(const NOR_BM_INST * pInst, NOR_BM_PSH * pPSH, int * pNumBitsCorrected) {
  int      r;
  int      Result;
  int      NumBitsCorrected;
  unsigned eccStat;

  r                = 0;                   // Set to indicate that no uncorrectable bit errors occurred.
  NumBitsCorrected = 0;
  eccStat = _LoadApplyECCStatEx(&pPSH->eccStatInvalid, &NumBitsCorrected);
  if (eccStat == ECC_STAT_EMPTY) {
    //
    // Note that we purposely do not check pPSH->IsInvalid for being set to 0xFF here
    // because _PreErasePhySector() sets this member to 0 without updating the ECC.
    //
    if (_IsBlankECC_PSH_Invalid(pPSH) == 0) {
      r = RESULT_INCONSISTENT_DATA;       // Error, the phy. sector header is not consistent.
    }
  } else {
    if (eccStat == ECC_STAT_VALID_EX) {
      Result = pInst->pECCHookMan->pfApply(SEGGER_PTR2PTR(U32, &pPSH->IsInvalid), pPSH->abECCInvalid);      // MISRA deviation D:100e
      if (Result < 0) {
        r = RESULT_ECC_ERROR;
      } else {
        NumBitsCorrected += Result;
      }
    } else {
      r = RESULT_INCONSISTENT_DATA;       // Error, the phy. sector header is not consistent.
    }
  }
  if (pNumBitsCorrected != NULL) {
    *pNumBitsCorrected = NumBitsCorrected;
  }
  return r;
}

#endif // FAIL_SAFE_ERASE_NO_REWRITE

/*********************************************************************
*
*       _LoadApplyECC_PSH
*
*  Function description
*    Verifies the data integrity of the entire physical sector header using CRC.
*
*  Return value
*    ==0      OK, no bit errors detected.
*    !=0      Error, bit errors detected.
*/
static int _LoadApplyECC_PSH(const NOR_BM_INST * pInst, NOR_BM_PSH * pPSH, int * pNumBitsCorrected) {
  U8       * pECC;
  int        r;
  int        Result;
  unsigned   eccStat;
  int        NumBitsCorrected;
  int        NumBitsCorrectedTotal;

  r                     = 0;              // Set to indicate success.
  pECC                  = NULL;
  NumBitsCorrectedTotal = 0;
  NumBitsCorrected      = 0;
  eccStat = _LoadApplyECCStat(&pPSH->ecc0Stat, &NumBitsCorrected);
  NumBitsCorrectedTotal += NumBitsCorrected;
  if (eccStat == ECC_STAT_EMPTY) {
    if (_IsBlankPSH(pInst, pPSH) == 0) {
      r = RESULT_INCONSISTENT_DATA;       // Error, the phy. sector header is not consistent.
    }
  } else {
    if (eccStat == ECC_STAT_VALID) {
      pECC = pPSH->abECC0;
    } else {
      NumBitsCorrected = 0;
      eccStat = _LoadApplyECCStat(&pPSH->ecc1Stat, &NumBitsCorrected);
      if (eccStat == ECC_STAT_VALID) {
        NumBitsCorrectedTotal += NumBitsCorrected;
        pECC = pPSH->abECC1;
      } else {
        r = RESULT_INCONSISTENT_DATA;     // Error, the phy. sector header is not consistent.
      }
    }
  }
  if (pECC != NULL) {
    Result = _ApplyPSH_ECC(pInst, pPSH, pECC);
    if (Result < 0) {
      r = RESULT_ECC_ERROR;
    } else {
      NumBitsCorrectedTotal += Result;
    }
  }
#if FAIL_SAFE_ERASE_NO_REWRITE
  if (r == 0) {
    if (_IsRewriteSupported(pInst) == 0) {
      int IsWorkPresent;
      int IsDataPresent;
      int IsInvalid;

      IsInvalid = 0;
      //
      // Check first if the physical sector is marked as invalid.
      //
      NumBitsCorrected = 0;
      r = _LoadApplyECC_PSH_Invalid(pInst, pPSH, &NumBitsCorrected);
      if (r == 0) {
        NumBitsCorrectedTotal += NumBitsCorrected;
        if (pPSH->IsInvalid == 0x00u) {       // Reversed logic: 0x00 means invalid, 0xFF means not invalid.
          IsInvalid = 1;
        } else {
          if (pPSH->IsInvalid != 0xFFu) {     // Additional sanity check.
            r = RESULT_INCONSISTENT_DATA;
          }
        }
      }
      if (r == 0) {
        if (IsInvalid == 0) {
          //
          // OK, the physical sector stores valid data.
          // Determine if it a work or data block.
          //
          IsWorkPresent    = 0;
          NumBitsCorrected = 0;
          r = _LoadApplyECC_PSH_Work(pInst, pPSH, &NumBitsCorrected);
          if (r == 0) {
            NumBitsCorrectedTotal += NumBitsCorrected;
            if (pPSH->IsWork == 0x00u) {      // Reversed logic: 0x00 means work block, 0xFF means not work block.
              IsWorkPresent = 1;              // It is a work block.
            } else {
              if (pPSH->IsWork != 0xFFu) {    // Additional sanity check.
                r = RESULT_INCONSISTENT_DATA;
              }
            }
          }
          IsDataPresent    = 0;
          NumBitsCorrected = 0;
          r = _LoadApplyECC_PSH_Data(pInst, pPSH, &NumBitsCorrected);
          if (r == 0) {
            NumBitsCorrectedTotal += NumBitsCorrected;
            if (pPSH->IsValid == 0x00u) {     // Reversed logic: 0x00 means data block, 0xFF means not data block.
              IsDataPresent = 1;              // It is a data block.
            } else {
              if (pPSH->IsValid != 0xFFu) {   // Additional sanity check.
                r = RESULT_INCONSISTENT_DATA;
              }
            }
          } else {
            if (IsWorkPresent != 0) {
              //
              // Ignore the ECC error if the information about the work block is valid
              // because this may by the result of an interrupted in-place block conversion
              // operation.
              //
              r = 0;
            }
          }
        }
      }
    }
  }
#endif // FAIL_SAFE_ERASE_NO_REWRITE
  if (pNumBitsCorrected != NULL) {
    *pNumBitsCorrected = NumBitsCorrectedTotal;
  }
  return r;
}

/*********************************************************************
*
*       _CalcECCData
*
*  Function description
*    Calculates the ECC of the sector data.
*/
static void _CalcECCData(const NOR_BM_INST * pInst, const U32 * pData, U8 * pECC) {
  pInst->pECCHookData->pfCalc(pData, pECC);
}

/*********************************************************************
*
*       _ApplyECCData
*
*  Function description
*    Calculates the ECC of the sector data.
*
*  Return value
*    >=0  OK, number of bit errors corrected.
*    < 0  An uncorrectable bit error occurred.
*/
static int _ApplyECCData(const NOR_BM_INST * pInst, U32 * pData, U8 * pECC) {
  int r;

  r = pInst->pECCHookData->pfApply(pData, pECC);
  return r;
}

/*********************************************************************
*
*       _ECC_API
*/
static const NOR_BM_ECC_API _ECC_API = {
  _CalcStoreECC_PSH,
  _LoadApplyECC_PSH,
  _CalcStoreECC_LSH,
  _LoadApplyECC_LSH,
  _CalcECCData,
  _ApplyECCData
#if FAIL_SAFE_ERASE_NO_REWRITE
  , _CalcStoreECC_PSH_Data
  , _CalcStoreECC_PSH_Work
  , _CalcStoreECC_PSH_Invalid
  , _LoadApplyECC_PSH_Data
  , _LoadApplyECC_PSH_Work
  , _LoadApplyECC_PSH_Invalid
  , _CalcStoreECC_LSH_Data
  , _CalcStoreECC_LSH_Invalid
#endif // FAIL_SAFE_ERASE_NO_REWRITE
};

#endif // FS_NOR_SUPPORT_ECC

/*********************************************************************
*
*       _GetLogSectorDataStat
*/
static unsigned _GetLogSectorDataStat(const NOR_BM_INST * pInst, const NOR_BM_LSH * pLSH) {
  unsigned DataStat;

  FS_USE_PARA(pInst);
#if (FS_NOR_CAN_REWRITE == 0)
  if (_IsRewriteSupported(pInst) != 0) {
    DataStat = pLSH->DataStat;
  } else {
    if (pLSH->IsInvalid == 0u) {            // Reversed logic: 0 means invalid
      DataStat = DATA_STAT_INVALID;
    } else {
      if (pLSH->IsValid == 0u) {            // Reversed logic: 0 means valid
        DataStat = DATA_STAT_VALID;
      } else {
        DataStat = DATA_STAT_EMPTY;
      }
    }
  }
#else
  DataStat = pLSH->DataStat;
#endif // FS_NOR_CAN_REWRITE == 0
  return DataStat;
}

/*********************************************************************
*
*       _SetLogSectorDataStat
*/
static void _SetLogSectorDataStat(const NOR_BM_INST * pInst, NOR_BM_LSH * pLSH, unsigned DataStat) {
  FS_USE_PARA(pInst);
#if (FS_NOR_CAN_REWRITE == 0)
  if (_IsRewriteSupported(pInst) != 0) {
    pLSH->DataStat = (U8)DataStat;
    UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_LSH, DataStat), sizeof(pLSH->DataStat));
  } else {
    if (DataStat == DATA_STAT_VALID) {
      pLSH->IsValid = 0;                  // Reversed logic: 0 means valid
      UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_LSH, IsValid), sizeof(pLSH->IsValid));
    } else {
      if (DataStat == DATA_STAT_INVALID) {
        pLSH->IsInvalid = 0;              // Reversed logic: 0 means invalid
        UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_LSH, IsInvalid), sizeof(pLSH->IsInvalid));
      }
    }
  }
#else
  pLSH->DataStat = (U8)DataStat;
  UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_LSH, DataStat), sizeof(pLSH->DataStat));
#endif // FS_NOR_CAN_REWRITE == 0
}

/*********************************************************************
*
*        _CopyLogSectorData
*
*  Function description
*    Copies the data part of a logical sector.
*
*  Return value
*    ==0                    OK, sector data has been copied.
*    ==RESULT_READ_ERROR    Error reading data.
*    ==RESULT_WRITE_ERROR   Error writing data.
*
*  Additional information
*     The logical sector header is not copied and has to be stored separately.
*
*    The sector is copied in multiple chunks, where a small
*    array on the stack is used as buffer. The reason why the data
*    is copied in multiple chunks is simply to keep the stack load low.
*/
static int _CopyLogSectorData(NOR_BM_INST * pInst, unsigned psiSrc, unsigned srsiSrc, unsigned psiDest, unsigned srsiDest) {
  U32        aBuffer[FS_NOR_DATA_BUFFER_SIZE / 4];
  int        r;
  unsigned   NumBytesAtOnce;
  unsigned   NumBytes;
  U32        OffSrc;
  U32        OffDest;
  U32        SizeOfBuffer;
  U32      * pBuffer;
  I32        NumBytesFree;
  int        NumRetries;

  pBuffer      = (U32 *)_UseFreeMem(&NumBytesFree);
  SizeOfBuffer = sizeof(aBuffer);
  if (pBuffer != NULL) {
    if ((I32)SizeOfBuffer < NumBytesFree) {
      SizeOfBuffer = (U32)NumBytesFree;
    } else {
      pBuffer = NULL;
    }
  }
  if (pBuffer == NULL) {
    pBuffer = aBuffer;
  }
  SizeOfBuffer &= ~((U32)FS_NOR_LINE_SIZE - 1uL);     // Make sure that the buffer is aligned to the size of the flash line.
  NumRetries = FS_NOR_NUM_READ_RETRIES;
  for (;;) {
    NumBytes = 1uL << pInst->ldBytesPerSector;
    OffSrc   = _GetLogSectorDataOff(pInst, psiSrc,  srsiSrc);
    OffDest  = _GetLogSectorDataOff(pInst, psiDest, srsiDest);
    do {
      NumBytesAtOnce = SEGGER_MIN(NumBytes, SizeOfBuffer);
      r = _ReadOff(pInst, pBuffer, OffSrc,  NumBytesAtOnce);
      if (r != 0) {
        break;                                        // Error, could not read data from NOR flash.
      }
      r = _WriteOff(pInst, pBuffer, OffDest, NumBytesAtOnce);
      if (r != 0) {
        break;                                        // Error, could not write data to NOR flash.
      }
      NumBytes -= NumBytesAtOnce;
      OffSrc   += NumBytesAtOnce;
      OffDest  += NumBytesAtOnce;
    } while (NumBytes != 0u);
    //
    // Write errors are handled in the calling function.
    //
    if ((r == RESULT_NO_ERROR) || (r == RESULT_WRITE_ERROR)) {
      break;
    }
    //
    // Read again the data from the source logical sector.
    //
    if (NumRetries-- == 0) {
      break;                                          // Error, no more retries left.
    }
    FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR_BM: COPY_LOG_SECTOR_DATA psiSrc: %u, srsiSrc: %u, psiDest: %u, srsiDest: %u, Retries: %d/%d, r: %d\n", psiSrc, srsiSrc, psiDest, srsiDest, NumRetries, FS_NOR_NUM_READ_RETRIES, r));
    IF_STATS(pInst->StatCounters.NumReadRetries++);
  }
  _UnuseFreeMem(NumBytesFree);
  return r;
}

#if FS_NOR_SUPPORT_CRC

/*********************************************************************
*
*        _CopyLogSectorDataWithCRC
*
*  Function description
*    Copies the data part of a logical sector and checks the data consistency by using CRC.
*
*  Parameters
*    pInst        Driver instance.
*    psiSrc       Index of the source physical sector.
*    srsiSrc      Index of the source logical sector relative to physical sector.
*    psiDest      Index of the destination physical sector.
*    srsiDest     Index of the destination logical sector relative to physical sector.
*    pLSH         Header of the source logical sector (cannot be NULL).
*
*  Return value
*    ==0                    OK, sector data has been copied.
*    ==RESULT_READ_ERROR    Error reading data.
*    ==RESULT_WRITE_ERROR   Error writing data.
*    ==RESULT_CRC_ERROR     CRC verification failed.
*
*  Additional information
*    The logical sector header is not copied and has to be stored separately.
*    This function assumes that the verification via CRC is enabled at runtime.
*/
static int _CopyLogSectorDataWithCRC(NOR_BM_INST * pInst, unsigned psiSrc, unsigned srsiSrc, unsigned psiDest, unsigned srsiDest, const NOR_BM_LSH * pLSH) {
  U32        aBuffer[FS_NOR_DATA_BUFFER_SIZE / 4];
  int        r;
  unsigned   NumBytesAtOnce;
  unsigned   NumBytes;
  U32        OffSrc;
  U32        OffDest;
  U32        SizeOfBuffer;
  U32      * pBuffer;
  I32        NumBytesFree;
  int        NumRetries;
  U16        crcCalc;
  U16        crcRead;
  int        NumErrorsCRC;

  pBuffer      = (U32 *)_UseFreeMem(&NumBytesFree);
  SizeOfBuffer = sizeof(aBuffer);
  if (pBuffer != NULL) {
    if ((I32)SizeOfBuffer < NumBytesFree) {
      SizeOfBuffer = (U32)NumBytesFree;
    } else {
      pBuffer = NULL;
    }
  }
  if (pBuffer == NULL) {
    pBuffer = aBuffer;
  }
  SizeOfBuffer &= ~((U32)FS_NOR_LINE_SIZE - 1uL);     // Make sure that the buffer is aligned to the size of the flash line.
  SizeOfBuffer &= ~3u;                                // Make sure that the buffer is at least 4 bytes aligned.
  NumRetries    = FS_NOR_NUM_READ_RETRIES;
  NumErrorsCRC  = 0;
  for (;;) {
    NumBytes       = 1uL << pInst->ldBytesPerSector;
    OffSrc         = _GetLogSectorDataOff(pInst, psiSrc,  srsiSrc);
    OffDest        = _GetLogSectorDataOff(pInst, psiDest, srsiDest);
    crcCalc        = CRC_SECTOR_DATA_INIT;
    NumBytesAtOnce = SEGGER_MIN(NumBytes, SizeOfBuffer);
    //
    // The copy operation is performed in two different ways that depend
    // on the size of the copy buffer.
    // 1) If the copy buffer is larger than or equal to the logical sector size,
    //    then the data is written to the destination sector only if the CRC
    //    verification succeeds.
    // 2) Else the CRC verification is performed after the sector data
    //    is written to the destination. In this case we return a write
    //    error to the caller if the CRC verification fails because
    //    the destination sector contains corrupted data.
    //
    if (NumBytesAtOnce == NumBytes) {
      r = _ReadOff(pInst, pBuffer, OffSrc, NumBytesAtOnce);
      if (r == 0) {
        crcCalc = _pCRC_API->pfCalcData(SEGGER_PTR2PTR(U8, pBuffer), NumBytesAtOnce, crcCalc);        // MISRA deviation D:100e
        crcRead = pLSH->crcSectorData;
        if (crcCalc != crcRead) {
          FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_BM: _CopyLogSectorDataWithCRC: CRC check failed PSI: %lu, SRSI: %lu, Calc: 0x%02x, Read: 0x%02x", psiSrc, srsiSrc, crcCalc, crcRead));
          r = RESULT_CRC_ERROR;
        } else {
          r = _WriteOff(pInst, pBuffer, OffDest, NumBytesAtOnce);
        }
      }
    } else {
      do {
        NumBytesAtOnce = SEGGER_MIN(NumBytes, SizeOfBuffer);
        r = _ReadOff(pInst, pBuffer, OffSrc,  NumBytesAtOnce);
        if (r != 0) {
          break;                        // Error, could not read data from NOR flash.
        }
        r = _WriteOff(pInst, pBuffer, OffDest, NumBytesAtOnce);
        if (r != 0) {
          break;                        // Error, could not write data to NOR flash.
        }
        crcCalc = _pCRC_API->pfCalcData(SEGGER_PTR2PTR(U8, pBuffer), NumBytesAtOnce, crcCalc);        // MISRA deviation D:100e
        NumBytes -= NumBytesAtOnce;
        OffSrc   += NumBytesAtOnce;
        OffDest  += NumBytesAtOnce;
      } while (NumBytes != 0u);
      if (r == 0) {
        crcRead = pLSH->crcSectorData;
        if (crcCalc != crcRead) {
          ++NumErrorsCRC;
          FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_BM: _CopyLogSectorDataWithCRC: CRC check failed PSI: %lu, SRSI: %lu, Calc: 0x%02x, Read: 0x%02x", psiSrc, srsiSrc, crcCalc, crcRead));
          r = RESULT_CRC_ERROR;
        }
      }
    }
    //
    // A write error must be handled by the calling function.
    //
    if (r == RESULT_WRITE_ERROR) {
      break;
    }
    //
    // The copy operation was successful.
    //
    if (r == RESULT_NO_ERROR) {
      //
      // If we encountered at least one CRC error then we probably
      // wrote corrupted data to destination sector. In this case
      // we report a write error so that the caller can try to
      // recover from this.
      //
      if (NumErrorsCRC != 0) {
        r = RESULT_WRITE_ERROR;
      }
      break;
    }
    //
    // Read again the data from the source logical sector.
    //
    if (NumRetries-- == 0) {
      break;                            // Error, no more retries left.
    }
    FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR_BM: COPY_LOG_SECTOR_DATA_CRC psiSrc: %u, srsiSrc: %u, psiDest: %u, srsiDest: %u, Retries: %d/%d, r: %d\n", psiSrc, srsiSrc, psiDest, srsiDest, NumRetries, FS_NOR_NUM_READ_RETRIES, r));
    IF_STATS(pInst->StatCounters.NumReadRetries++);
  }
  _UnuseFreeMem(NumBytesFree);
  return r;
}

#endif // FS_NOR_SUPPORT_CRC

#if FS_NOR_SUPPORT_ECC

/*********************************************************************
*
*        _CopyLogSectorDataWithECC
*
*  Function description
*    Copies the data part of a logical sector and checks the data consistency by using ECC.
*
*  Parameters
*    pInst        Driver instance.
*    psiSrc       Index of the source physical sector.
*    srsiSrc      Index of the source logical sector relative to physical sector.
*    psiDest      Index of the destination physical sector.
*    srsiDest     Index of the destination logical sector relative to physical sector.
*    pLSH         Header of the source logical sector.
*
*  Return value
*    ==0                    OK, sector data has been copied.
*    ==RESULT_READ_ERROR    Error reading data.
*    ==RESULT_WRITE_ERROR   Error writing data.
*    ==RESULT_ECC_ERROR     ECC verification failed.
*
*  Additional information
*    The logical sector header is not copied and has to be stored separately.
*    This function assumes that the verification via ECC is enabled at runtime.
*/
static int _CopyLogSectorDataWithECC(NOR_BM_INST * pInst, unsigned psiSrc, unsigned srsiSrc, unsigned psiDest, unsigned srsiDest, NOR_BM_LSH * pLSH) {
  int        r;
  unsigned   NumBytesAtOnce;
  unsigned   NumBytes;
  U32        OffSrc;
  U32        OffDest;
  U32        SizeOfBuffer;
  U32      * pBuffer;
  I32        NumBytesFree;
  int        NumRetries;
  unsigned   BytesPerBlock;
  unsigned   ldBytesPerBlock;
  unsigned   iBlock;
  U8       * pData8;
  unsigned   NumBlocks;
  int        Result;

  ldBytesPerBlock = pInst->pECCHookData->ldBytesPerBlock;
  BytesPerBlock   = 1uL << ldBytesPerBlock;
  pBuffer         = (U32 *)_UseFreeMem(&NumBytesFree);
  SizeOfBuffer    = BytesPerBlock;
  if (pBuffer != NULL) {
    if ((I32)SizeOfBuffer < NumBytesFree) {
      SizeOfBuffer = (U32)NumBytesFree & ~(BytesPerBlock - 1u);     // Make sure that the buffer is a multiple of ECC block size.
    } else {
      pBuffer = NULL;
    }
  }
  if (pBuffer == NULL) {
    pBuffer = _pECCBuffer;
  }
  NumRetries = FS_NOR_NUM_READ_RETRIES;
  for (;;) {
    NumBytes = 1uL << pInst->ldBytesPerSector;
    OffSrc   = _GetLogSectorDataOff(pInst, psiSrc,  srsiSrc);
    OffDest  = _GetLogSectorDataOff(pInst, psiDest, srsiDest);
    iBlock   = 0;
    do {
      NumBytesAtOnce = SEGGER_MIN(NumBytes, SizeOfBuffer);
      //
      // Read either the entire data of the logical sector from the NOR flash device
      // if the buffer is sufficiently large or only a limited number of ECC blocks.
      //
      r = _ReadOff(pInst, pBuffer, OffSrc,  NumBytesAtOnce);
      if (r != 0) {
        break;                        // Error, could not read data from NOR flash.
      }
      //
      // Correct the eventual bit errors using ECC.
      //
      NumBlocks = NumBytesAtOnce >> ldBytesPerBlock;
      pData8    = SEGGER_PTR2PTR(U8, pBuffer);                                                                      // MISRA deviation D:100e
      do {
        Result = pInst->pECC_API->pfApplyData(pInst, SEGGER_PTR2PTR(U32, pData8), pLSH->aaECCSectorData[iBlock]);   // MISRA deviation D:100e
        if (Result < 0) {
          FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_BM: _CopyLogSectorDataWithECC: ECC check failed PSI: %lu, SRSI: %lu", psiSrc, srsiSrc));
          r = RESULT_ECC_ERROR;
          break;
        }
        UPDATE_NUM_BIT_ERRORS(pInst, Result);
        pData8 += BytesPerBlock;
        ++iBlock;
        if (iBlock >= (unsigned)FS_NOR_MAX_NUM_BLOCKS_ECC_DATA) {
          break;
        }
      } while (--NumBlocks != 0u);
      if (r != 0) {
        break;
      }
      //
      // Write the sector data back to the NOR flash device.
      //
      r = _WriteOff(pInst, pBuffer, OffDest, NumBytesAtOnce);
      if (r != 0) {
        break;                        // Error, could not write data to NOR flash.
      }
      if (iBlock >= (unsigned)FS_NOR_MAX_NUM_BLOCKS_ECC_DATA) {
        break;
      }
      NumBytes -= NumBytesAtOnce;
      OffSrc   += NumBytesAtOnce;
      OffDest  += NumBytesAtOnce;
    } while (NumBytes != 0u);
    //
    // Write errors must be handled by the calling function.
    //
    if ((r == RESULT_NO_ERROR) || (r == RESULT_WRITE_ERROR)) {
      break;
    }
    //
    // Read again the data from the source logical sector.
    //
    if (NumRetries-- == 0) {
      break;                            // Error, no more retries left.
    }
    FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR_BM: COPY_LOG_SECTOR_DATA_ECC psiSrc: %u, srsiSrc: %u, psiDest: %u, srsiDest: %u, Retries: %d/%d, r: %d\n", psiSrc, srsiSrc, psiDest, srsiDest, NumRetries, FS_NOR_NUM_READ_RETRIES, r));
    IF_STATS(pInst->StatCounters.NumReadRetries++);
  }
  _UnuseFreeMem(NumBytesFree);
  return r;
}

#endif // FS_NOR_SUPPORT_ECC

#if (FS_NOR_SUPPORT_CRC != 0) && (FS_NOR_SUPPORT_ECC != 0)

/*********************************************************************
*
*        _CopyLogSectorDataWithCRCAndECC
*
*  Function description
*    Copies the data part of a logical sector and checks the data
*    consistency using CRC and ECC.
*
*  Parameters
*    pInst        Driver instance.
*    psiSrc       Index of the source physical sector.
*    srsiSrc      Index of the source logical sector relative to physical sector.
*    psiDest      Index of the destination physical sector.
*    srsiDest     Index of the destination logical sector relative to physical sector.
*    pLSH         Header of the source logical sector (cannot be NULL).
*
*  Return value
*    ==0                    OK, sector data has been copied.
*    ==RESULT_READ_ERROR    Error reading data.
*    ==RESULT_WRITE_ERROR   Error writing data.
*    ==RESULT_CRC_ERROR     CRC verification failed.
*    ==RESULT_ECC_ERROR     ECC verification failed.
*
*  Additional information
*    The logical sector header is not copied and has to be stored separately.
*    This function assumes that the verification via CRC as well as via ECC
*    are enabled at runtime.
*/
static int _CopyLogSectorDataWithCRCAndECC(NOR_BM_INST * pInst, unsigned psiSrc, unsigned srsiSrc, unsigned psiDest, unsigned srsiDest, NOR_BM_LSH * pLSH) {
  int        r;
  unsigned   NumBytesAtOnce;
  unsigned   NumBytes;
  U32        OffSrc;
  U32        OffDest;
  U32        SizeOfBuffer;
  U32      * pBuffer;
  I32        NumBytesFree;
  int        NumRetries;
  U16        crcCalc;
  U16        crcRead;
  unsigned   BytesPerBlock;
  unsigned   ldBytesPerBlock;
  unsigned   iBlock;
  U8       * pData8;
  unsigned   NumBlocks;
  int        Result;
  int        NumErrorsCRC;

  ldBytesPerBlock = pInst->pECCHookData->ldBytesPerBlock;
  BytesPerBlock   = 1uL << ldBytesPerBlock;
  pBuffer         = (U32 *)_UseFreeMem(&NumBytesFree);
  SizeOfBuffer    = BytesPerBlock;
  if (pBuffer != NULL) {
    if ((I32)SizeOfBuffer < NumBytesFree) {
      SizeOfBuffer = (U32)NumBytesFree & ~(BytesPerBlock - 1u);     // Make sure that the buffer is a multiple of ECC block size.
    } else {
      pBuffer = NULL;
    }
  }
  if (pBuffer == NULL) {
    pBuffer = _pECCBuffer;
  }
  NumRetries   = FS_NOR_NUM_READ_RETRIES;
  NumErrorsCRC = 0;
  for (;;) {
    NumBytes       = 1uL << pInst->ldBytesPerSector;
    OffSrc         = _GetLogSectorDataOff(pInst, psiSrc,  srsiSrc);
    OffDest        = _GetLogSectorDataOff(pInst, psiDest, srsiDest);
    crcCalc        = CRC_SECTOR_DATA_INIT;
    iBlock         = 0;
    NumBytesAtOnce = SEGGER_MIN(NumBytes, SizeOfBuffer);
    //
    // The copy operation is performed in two different ways that depends
    // on the size of the copy buffer.
    // 1) If the copy buffer is larger than or equal to the logical sector size,
    //    then the data is written to the destination sector only if the CRC
    //    verification succeeds.
    // 2) Else the CRC verification is performed after the sector data
    //    is written to the destination. In this case we return a write
    //    error to the caller if the CRC verification fails because
    //    the destination sector contains corrupted data.
    //
    if (NumBytesAtOnce == NumBytes) {
      r = _ReadOff(pInst, pBuffer, OffSrc,  NumBytesAtOnce);
      if (r == 0) {
        //
        // Correct the eventual bit errors using ECC.
        //
        NumBlocks = NumBytesAtOnce >> ldBytesPerBlock;
        pData8    = SEGGER_PTR2PTR(U8, pBuffer);                                                                        // MISRA deviation D:100e
        do {
          Result = pInst->pECC_API->pfApplyData(pInst, SEGGER_PTR2PTR(U32, pData8), pLSH->aaECCSectorData[iBlock]);     // MISRA deviation D:100e
          if (Result < 0) {
            FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_BM: _CopyLogSectorDataWithCRCAndECC: ECC check failed PSI: %lu, SRSI: %lu", psiSrc, srsiSrc));
            r = RESULT_ECC_ERROR;
            break;
          }
          UPDATE_NUM_BIT_ERRORS(pInst, Result);
          pData8 += BytesPerBlock;
          ++iBlock;
          if (iBlock >= (unsigned)FS_NOR_MAX_NUM_BLOCKS_ECC_DATA) {
            break;
          }
        } while (--NumBlocks != 0u);
        if (r == 0) {
          crcCalc = _pCRC_API->pfCalcData(SEGGER_PTR2PTR(U8, pBuffer), NumBytesAtOnce, crcCalc);                        // MISRA deviation D:100e
          crcRead = pLSH->crcSectorData;
          if (crcCalc != crcRead) {
            FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_BM: _CopyLogSectorDataWithCRCAndECC: CRC check failed PSI: %lu, SRSI: %lu, Calc: 0x%02x, Read: 0x%02x", psiSrc, srsiSrc, crcCalc, crcRead));
            r = RESULT_CRC_ERROR;
          } else {
            r = _WriteOff(pInst, pBuffer, OffDest, NumBytesAtOnce);
          }
        }
      }
    } else {
      do {
        NumBytesAtOnce = SEGGER_MIN(NumBytes, SizeOfBuffer);
        r = _ReadOff(pInst, pBuffer, OffSrc,  NumBytesAtOnce);
        if (r != 0) {
          break;                                                      // Error, could not read data from NOR flash.
        }
        //
        // Correct the eventual bit errors using ECC.
        //
        NumBlocks = NumBytesAtOnce >> ldBytesPerBlock;
        pData8    = SEGGER_PTR2PTR(U8, pBuffer);                                                                      // MISRA deviation D:100e
        do {
          Result = pInst->pECC_API->pfApplyData(pInst, SEGGER_PTR2PTR(U32, pData8), pLSH->aaECCSectorData[iBlock]);   // MISRA deviation D:100e
          if (Result < 0) {
            FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_BM: _CopyLogSectorDataWithCRCAndECC: ECC check failed PSI: %lu, SRSI: %lu", psiSrc, srsiSrc));
            r = RESULT_ECC_ERROR;
            break;
          }
          UPDATE_NUM_BIT_ERRORS(pInst, Result);
          pData8 += BytesPerBlock;
          ++iBlock;
        } while (--NumBlocks != 0u);
        if (r != 0) {
          break;
        }
        r = _WriteOff(pInst, pBuffer, OffDest, NumBytesAtOnce);
        if (r != 0) {
          break;                        // Error, could not write data to NOR flash.
        }
        crcCalc = _pCRC_API->pfCalcData(SEGGER_PTR2PTR(U8, pBuffer), NumBytesAtOnce, crcCalc);                        // MISRA deviation D:100e
        NumBytes -= NumBytesAtOnce;
        OffSrc   += NumBytesAtOnce;
        OffDest  += NumBytesAtOnce;
      } while (NumBytes != 0u);
      if (r == 0) {
        crcRead = pLSH->crcSectorData;
        if (crcCalc != crcRead) {
          ++NumErrorsCRC;
          FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_BM: _CopyLogSectorDataWithCRCAndECC: CRC check failed PSI: %lu, SRSI: %lu, Calc: 0x%02x, Read: 0x%02x", psiSrc, srsiSrc, crcCalc, crcRead));
          r = RESULT_CRC_ERROR;
        }
      }
    }
    //
    // A write error must be handled by the calling function.
    //
    if (r == RESULT_WRITE_ERROR) {
      break;
    }
    //
    // The copy operation was successful.
    //
    if (r == RESULT_NO_ERROR) {
      //
      // If we encountered at least one CRC error then we probably
      // wrote corrupted data to destination sector. In this case
      // we report a write error so that the caller can try to
      // recover from this.
      //
      if (NumErrorsCRC != 0) {
        r = RESULT_WRITE_ERROR;
      }
      break;
    }
    //
    // Read again the data from the source logical sector.
    //
    if (NumRetries-- == 0) {
      break;                            // Error, no more retries left.
    }
    FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR_BM: COPY_LOG_SECTOR_DATA_CRC_ECC psiSrc: %u, srsiSrc: %u, psiDest: %u, srsiDest: %u, Retries: %d/%d, r: %d\n", psiSrc, srsiSrc, psiDest, srsiDest, NumRetries, FS_NOR_NUM_READ_RETRIES, r));
    IF_STATS(pInst->StatCounters.NumReadRetries++);
  }
  _UnuseFreeMem(NumBytesFree);
  return r;
}

#endif // FS_NOR_SUPPORT_CRC != 0 && FS_NOR_SUPPORT_ECC != 0

/*********************************************************************
*
*       _IsBlankLogSector
*
*  Function description
*    It checks if all data bytes of a logical sector are set to 0xFF.
*
*  Parameters
*    pInst            Driver instance.
*    PhySectorIndex   Index of the physical sector that contains the logical sector.
*    srsi             Index of the logical sector relative to the physical sector.
*
*  Return value
*    ==1    Sector is blank
*    ==0    Sector is not blank
*/
static int _IsBlankLogSector(NOR_BM_INST * pInst, unsigned PhySectorIndex, unsigned srsi) {
  U32        aBuffer[FS_NOR_DATA_BUFFER_SIZE / 4];
  U32        Off;
  unsigned   NumBytes;
  unsigned   NumBytesAtOnce;
  unsigned   NumItems;
  U32      * p;
  int        r;

  NumBytes = 1uL << pInst->ldBytesPerSector;
  Off      = _GetLogSectorDataOff(pInst, PhySectorIndex,  srsi);
  do {
    NumBytesAtOnce = SEGGER_MIN(NumBytes, sizeof(aBuffer));
    r = _ReadOff(pInst, aBuffer, Off, NumBytesAtOnce);
    if (r != 0) {
      return 0;
    }
    NumItems = NumBytesAtOnce >> 2;
    p        = aBuffer;
    do {
      if (*p++ != 0xFFFFFFFFuL) {
        return 0;
      }
    } while (--NumItems != 0u);
    NumBytes -= NumBytesAtOnce;
    Off      += NumBytesAtOnce;
  } while (NumBytes != 0u);
  return 1;
}

/*********************************************************************
*
*       _WritePSH
*
*   Function description
*     Writes the header of a given physical sector.
*/
static int _WritePSH(NOR_BM_INST * pInst, unsigned PhySectorIndex, const NOR_BM_PSH * pPSH) {
  U32          Off;
  int          r;
  unsigned     NumBytes;
  const U8   * pData;
#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  NOR_BM_PSH   psh;
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  U32          aData[sizeof(NOR_BM_PSH) / 4u];
  unsigned     NumBytesToWrite;
#endif // FS_NOR_SUPPORT_VARIABLE_LINE_SIZE

  NumBytes = _SizeOfPSH(pInst);
  pData    = SEGGER_CONSTPTR2PTR(const U8, pPSH);                                             // MISRA deviation D:100e
  _GetPhySectorInfo(pInst, PhySectorIndex, &Off, NULL);
#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  FS_MEMCPY(&psh, pData, sizeof(NOR_BM_PSH));
  _pMultiByteAPI->pfStoreU16(SEGGER_PTR2PTR(U8, &psh.lbi),      psh.lbi);                     // MISRA deviation D:100e
  _pMultiByteAPI->pfStoreU32(SEGGER_PTR2PTR(U8, &psh.EraseCnt), psh.EraseCnt);                // MISRA deviation D:100e
#if (FS_NOR_PHY_SECTOR_RESERVE >= 4)
  _pMultiByteAPI->pfStoreU32(SEGGER_PTR2PTR(U8, &psh.EraseSignature), psh.EraseSignature);    // MISRA deviation D:100e
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
#if FAIL_SAFE_ERASE_NO_REWRITE
  _pMultiByteAPI->pfStoreU16(SEGGER_PTR2PTR(U8, &psh.lbiWork),  psh.lbiWork);                 // MISRA deviation D:100e
  _pMultiByteAPI->pfStoreU16(SEGGER_PTR2PTR(U8, &psh.lbiData),  psh.lbiData);                 // MISRA deviation D:100e
#endif // FAIL_SAFE_ERASE_NO_REWRITE
  pPSH  = &psh;
  pData = SEGGER_CONSTPTR2PTR(const U8, pPSH);                                                // MISRA deviation D:100e
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  FS_MEMSET(aData, 0xFF, sizeof(aData));
  NumBytesToWrite = _EncodePSH(pInst, pPSH, SEGGER_PTR2PTR(U8, aData));                       // MISRA deviation D:100e
  if (NumBytesToWrite != 0u) {
    pData    = SEGGER_CONSTPTR2PTR(const U8, aData);                                          // MISRA deviation D:100e
    NumBytes = NumBytesToWrite;
  }
#endif // FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  INIT_VERIFY(pData, Off, NumBytes);
  CALC_PSH_DATA_RANGE(pInst, &pData, &Off, &NumBytes);
  r = _WriteOff(pInst, pData, Off, NumBytes);
  VERIFY_WRITE(pInst);
  INIT_PSH_DATA_RANGE();
  IF_STATS(pInst->StatCounters.WritePSHCnt++);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR_BM: WRITE_PSH PSI: %u, DataStat: %s, DataCnt: %d,", PhySectorIndex, _TypeToName(pPSH->DataStat), pPSH->DataCnt));
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, " LBI: %u, EraseCnt: 0x%08x,", pPSH->lbi, pPSH->EraseCnt));
#if FS_NOR_SUPPORT_FAIL_SAFE_ERASE
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, " EraseSig: 0x%08x,", pPSH->EraseSignature));
#endif // FS_NOR_SUPPORT_FAIL_SAFE_ERASE
#if FS_NOR_SUPPORT_CRC
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, " crc0: 0x%02x, crc1: 0x%02x, crc2: 0x%02x, crcStat: 0x%02x,", pPSH->crc0, pPSH->crc1, pPSH->crc2, pPSH->crcStat));
#endif // FS_NOR_SUPPORT_CRC
#if (FS_NOR_CAN_REWRITE == 0)
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, " IsWork: %u, IsValid: %u, IsInvalid: %u,", pPSH->IsWork, pPSH->IsValid, pPSH->IsInvalid));
#endif // FS_NOR_CAN_REWRITE == 0
#if FAIL_SAFE_ERASE_NO_REWRITE
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, " DataCntWork: %u, lbiWork: %u, crcWork: 0x%02x,", pPSH->DataCntWork, pPSH->lbiWork, pPSH->crcWork));
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, " DataCntData: %u, lbiData: %u, crcData: 0x%02x,", pPSH->DataCntData, pPSH->lbiData, pPSH->crcData));
#endif // FAIL_SAFE_ERASE_NO_REWRITE
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, " Off: %lu, NumBytes: %lu, r: %d\n", Off, NumBytes, r));
  return r;
}

/*********************************************************************
*
*       _ReadPSHEx
*
*  Function description
*    Reads the header of a physical sector.
*
*  Parameters
*    pInst                      Driver instance.
*    PhySectorIndex             Index of the physical sector.
*    pPSH                       [OUT] Read data.
*    SkipCheckParity            Set to 1 if neither CRC nor ECC have to be checked.
*    SkipCheckEraseSignature    Set to 1 if the validity of the erase signature does not have to be checked.
*
*  Return value
*    ==0    OK, physical sector header read.
*    !=0    An error occurred.
*/
static int _ReadPSHEx(NOR_BM_INST * pInst, unsigned PhySectorIndex, NOR_BM_PSH * pPSH, int SkipCheckParity, int SkipCheckEraseSignature) {
  U32   Off;
  int   r;
  int   NumRetries;
  U32   NumBytes;
  U8  * pData;
#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  U32   aData[sizeof(NOR_BM_PSH) / 4u];
  U32   NumBytesRead;
  U32   BytesPerLine;
#endif // FS_NOR_SUPPORT_VARIABLE_LINE_SIZE

  NumBytes = _SizeOfPSH(pInst);
  pData    = SEGGER_PTR2PTR(U8, pPSH);                                                              // MISRA deviation D:100e
  _GetPhySectorInfo(pInst, PhySectorIndex, &Off, NULL);
#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  BytesPerLine = 1u << pInst->ldBytesPerLine;
  FS_MEMSET(aData, 0xFF, sizeof(aData));
  if (BytesPerLine < (unsigned)FS_NOR_LINE_SIZE) {
    pData = SEGGER_PTR2PTR(U8, aData);                                                              // Use a temporarily buffer to read the header. MISRA deviation D:100e
  }
#endif // FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  //
  // Repeat the read operation in case of an error.
  //
  NumRetries = FS_NOR_NUM_READ_RETRIES;
  for (;;) {
    r = _ReadOff(pInst, pData, Off, NumBytes);
#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
    NumBytesRead = _DecodePSH(pInst, pPSH, pData);
    if (NumBytesRead != 0u) {
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ALL)
       NumBytes = NumBytesRead;
#endif
    }
#endif // FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
    pPSH->lbi            = _pMultiByteAPI->pfLoadU16(SEGGER_PTR2PTR(U8, &pPSH->lbi));               // MISRA deviation D:100e
    pPSH->EraseCnt       = _pMultiByteAPI->pfLoadU32(SEGGER_PTR2PTR(U8, &pPSH->EraseCnt));          // MISRA deviation D:100e
#if (FS_NOR_PHY_SECTOR_RESERVE >= 4)
    pPSH->EraseSignature = _pMultiByteAPI->pfLoadU32(SEGGER_PTR2PTR(U8, &pPSH->EraseSignature));    // MISRA deviation D:100e
#endif
#if FAIL_SAFE_ERASE_NO_REWRITE
    pPSH->lbiWork        = _pMultiByteAPI->pfLoadU16(SEGGER_PTR2PTR(U8, &pPSH->lbiWork));           // MISRA deviation D:100e
    pPSH->lbiData        = _pMultiByteAPI->pfLoadU16(SEGGER_PTR2PTR(U8, &pPSH->lbiData));           // MISRA deviation D:100e
#endif // FAIL_SAFE_ERASE_NO_REWRITE
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
    if (SkipCheckParity == 0) {
#if FS_NOR_SUPPORT_ECC
      if (r == 0) {
        if (_IsECCEnabled(pInst) != 0) {
          int NumBitsCorrected;

          NumBitsCorrected = 0;
          r = pInst->pECC_API->pfLoadApplyPSH(pInst, pPSH, &NumBitsCorrected);
          UPDATE_NUM_BIT_ERRORS(pInst, NumBitsCorrected);
        }
      }
#endif // FS_NOR_SUPPORT_ECC
#if FS_NOR_SUPPORT_CRC
      if (r == 0) {
        if (_IsCRCEnabled(pInst) != 0) {
          r = _pCRC_API->pfLoadVerifyPSH(pInst, pPSH);
        }
      }
#endif // FS_NOR_SUPPORT_CRC
    }
#if FS_NOR_SUPPORT_FAIL_SAFE_ERASE
    if (SkipCheckEraseSignature == 0) {
      if (r == 0) {
        if (pInst->FailSafeErase != 0u) {
          if (pPSH->EraseSignature != ERASE_SIGNATURE_VALID) {
            r = RESULT_READ_ERROR;
          }
        }
      }
    }
#else
    FS_USE_PARA(SkipCheckEraseSignature);
#endif // FS_NOR_SUPPORT_FAIL_SAFE_ERASE
    if (r == 0) {
      break;                        // OK, data read successfully.
    }
    if (NumRetries-- == 0) {
      break;                        // Error, no more read retries.
    }
    FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR_BM: READ_PSH PSI: %u, Retries: %d/%d, r: %d\n", PhySectorIndex, NumRetries, FS_NOR_NUM_READ_RETRIES, r));
    IF_STATS(pInst->StatCounters.NumReadRetries++);
  }
  IF_STATS(pInst->StatCounters.ReadPSHCnt++);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR_BM: READ_PSH PSI: %u, DataStat: %s, DataCnt: %d,", PhySectorIndex, _TypeToName(pPSH->DataStat), pPSH->DataCnt));
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, " LBI: %u, EraseCnt: 0x%08x,", pPSH->lbi, pPSH->EraseCnt));
#if FS_NOR_SUPPORT_FAIL_SAFE_ERASE
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, " EraseSig: 0x%08x,", pPSH->EraseSignature));
#endif
#if FS_NOR_SUPPORT_CRC
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, " crc0: 0x%02x, crc1: 0x%02x, crc2: 0x%02x, crcStat: 0x%02x,", pPSH->crc0, pPSH->crc1, pPSH->crc2, pPSH->crcStat));
#endif
#if (FS_NOR_CAN_REWRITE == 0)
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, " IsWork: %u, IsValid: %u, IsInvalid: %u,", pPSH->IsWork, pPSH->IsValid, pPSH->IsInvalid));
#endif // FS_NOR_CAN_REWRITE == 0
#if FAIL_SAFE_ERASE_NO_REWRITE
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, " DataCntWork: %u, lbiWork: %u, crcWork: 0x%02x,", pPSH->DataCntWork, pPSH->lbiWork, pPSH->crcWork));
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, " DataCntData: %u, lbiData: %u, crcData: 0x%02x,", pPSH->DataCntData, pPSH->lbiData, pPSH->crcData));
#endif // FAIL_SAFE_ERASE_NO_REWRITE
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, " NumBytes: %lu, r: %d\n", NumBytes, r));
  return r;
}

/*********************************************************************
*
*       _ReadPSH
*
*  Function description
*    Reads the header of a physical sector.
*/
static int _ReadPSH(NOR_BM_INST * pInst, unsigned PhySectorIndex, NOR_BM_PSH * pPSH) {
  int r;

  r = _ReadPSHEx(pInst, PhySectorIndex, pPSH, 0, 1);
  return r;
}

/*********************************************************************
*
*       _WriteLSH
*
*  Function description
*    Reads the header of a logical sector.
*/
static int _WriteLSH(NOR_BM_INST * pInst, unsigned PhySectorIndex, unsigned srsi, const NOR_BM_LSH * pLSH) {
  U32          Off;
  int          r;
  unsigned     NumBytes;
  const U8   * pData;
#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  NOR_BM_LSH   lsh;
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  U32          aData[sizeof(NOR_BM_LSH) / 4u];
  unsigned     NumBytesToWrite;
#endif // FS_NOR_SUPPORT_VARIABLE_LINE_SIZE

  NumBytes = _SizeOfLSH(pInst);
  pData    = SEGGER_CONSTPTR2PTR(const U8, pLSH);                                             // MISRA deviation D:100e
  Off      = _GetLogSectorHeaderOff(pInst, PhySectorIndex, srsi);
#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  FS_MEMCPY(&lsh, pData, sizeof(NOR_BM_LSH));
  _pMultiByteAPI->pfStoreU16(SEGGER_PTR2PTR(U8, &lsh.brsi),          lsh.brsi);               // MISRA deviation D:100e
#if (FS_NOR_LOG_SECTOR_RESERVE >= 4)
  _pMultiByteAPI->pfStoreU16(SEGGER_PTR2PTR(U8, &lsh.crcSectorData), lsh.crcSectorData);      // MISRA deviation D:100e
#endif
  pLSH  = &lsh;
  pData = SEGGER_CONSTPTR2PTR(const U8, pLSH);                                                // MISRA deviation D:100e
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  FS_MEMSET(aData, 0xFF, sizeof(aData));
  NumBytesToWrite = _EncodeLSH(pInst, pLSH, SEGGER_PTR2PTR(U8, aData));                       // MISRA deviation D:100e
  if (NumBytesToWrite != 0u) {
    pData    = SEGGER_CONSTPTR2PTR(const U8, aData);                                          // MISRA deviation D:100e
    NumBytes = NumBytesToWrite;
  }
#endif // FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  INIT_VERIFY(pData, Off, NumBytes);
  CALC_LSH_DATA_RANGE(pInst, &pData, &Off, &NumBytes);
  r = _WriteOff(pInst, pData, Off, NumBytes);
  VERIFY_WRITE(pInst);
  INIT_LSH_DATA_RANGE();
  IF_STATS(pInst->StatCounters.WriteLSHCnt++);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR_BM: WRITE_LSH PSI: %u, SRSI: %u,", PhySectorIndex, srsi));
#if FS_NOR_SUPPORT_CRC
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, " crc0: 0x%02x, crc1: 0x%02x, crcSectorData: 0x%02x, crcStat: 0x%02x,", pLSH->crc0, pLSH->crc1, pLSH->crcSectorData, pLSH->crcStat));
#endif
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, " Off: %lu, NumBytes: %lu, r: %d\n", Off, NumBytes, r));
  return r;
}

/*********************************************************************
*
*       _ReadLSH
*
*  Function description
*    Reads the header of a logical sector.
*/
static int _ReadLSH(NOR_BM_INST * pInst, unsigned PhySectorIndex, unsigned srsi, NOR_BM_LSH * pLSH) {
  U32   Off;
  int   r;
  int   NumRetries;
  U32   NumBytes;
  U8  * pData;
#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  U32   aData[sizeof(NOR_BM_LSH) / 4u];
  U32   NumBytesRead;
  U32   BytesPerLine;
#endif // FS_NOR_SUPPORT_VARIABLE_LINE_SIZE

  NumBytes = _SizeOfLSH(pInst);
  pData    = SEGGER_PTR2PTR(U8, pLSH);                                                              // MISRA deviation D:100e
  Off      = _GetLogSectorHeaderOff(pInst, PhySectorIndex, srsi);
#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  BytesPerLine = 1u << pInst->ldBytesPerLine;
  FS_MEMSET(aData, 0xFF, sizeof(aData));
  if (BytesPerLine < (unsigned)FS_NOR_LINE_SIZE) {
    pData = SEGGER_PTR2PTR(U8, aData);                                                              // Use a temporarily buffer to read the header. MISRA deviation D:100e
  }
#endif // FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  //
  // Repeat the read operation in case of an error.
  //
  NumRetries = FS_NOR_NUM_READ_RETRIES;
  for (;;) {
    r = _ReadOff(pInst, pData, Off, NumBytes);
#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
    NumBytesRead = _DecodeLSH(pInst, pLSH, pData);
    if (NumBytesRead != 0u) {
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ALL)
       NumBytes = NumBytesRead;
#endif
    }
#endif // FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
    pLSH->brsi          = _pMultiByteAPI->pfLoadU16(SEGGER_PTR2PTR(U8, &pLSH->brsi));               // MISRA deviation D:100e
#if (FS_NOR_LOG_SECTOR_RESERVE >= 4)
    pLSH->crcSectorData = _pMultiByteAPI->pfLoadU16(SEGGER_PTR2PTR(U8, &pLSH->crcSectorData));      // MISRA deviation D:100e
#endif
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
#if FS_NOR_SUPPORT_ECC
    if (r == 0) {
      if (_IsECCEnabled(pInst) != 0) {
        int NumBitsCorrected;

        NumBitsCorrected = 0;
        r = pInst->pECC_API->pfLoadApplyLSH(pInst, pLSH, &NumBitsCorrected);
        UPDATE_NUM_BIT_ERRORS(pInst, NumBitsCorrected);
      }
    }
#endif // FS_NOR_SUPPORT_ECC
#if FS_NOR_SUPPORT_CRC
    if (r == 0) {
      if (_IsCRCEnabled(pInst) != 0) {
        r = _pCRC_API->pfLoadVerifyLSH(pInst, pLSH);
      }
    }
#endif // FS_NOR_SUPPORT_CRC
    if (r == 0) {
      break;                        // OK, data read successfully.
    }
    if (NumRetries-- == 0) {
      break;                        // Error, no more read retries.
    }
    FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR_BM: READ_LSH PSI: %u, SRSI: %u, Retries: %d/%d, r: %d\n", PhySectorIndex, srsi, NumRetries, FS_NOR_NUM_READ_RETRIES, r));
    IF_STATS(pInst->StatCounters.NumReadRetries++);
  }
  IF_STATS(pInst->StatCounters.ReadLSHCnt++);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR_BM: READ_LSH PSI: %u, SRSI: %u,", PhySectorIndex, srsi));
#if FS_NOR_SUPPORT_CRC
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, " crc0: 0x%02x, crc1: 0x%02x, crcSectorData: 0x%02x, crcStat: 0x%02x,", pLSH->crc0, pLSH->crc1, pLSH->crcSectorData, pLSH->crcStat));
#endif
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, " NumBytes: %u, r: %d\n", NumBytes, r));
  return r;
}

/*********************************************************************
*
*       _WriteLogSectorDataStatFast
*
*  Function description
*    Modifies the data validity of a logical sector (fast version).
*
*  Return value
*    ==0    OK, data status of the logical sector has been modified.
*    !=0    An error occurred.
*/
static int _WriteLogSectorDataStatFast(NOR_BM_INST * pInst, unsigned PhySectorIndex, unsigned srsi, unsigned DataStat) {
  int r;
  U32 Off;
  U8  Data;

  Data  = (U8)DataStat;
  Off   = _GetLogSectorHeaderOff(pInst, PhySectorIndex, srsi);
  Off  += OFFSET_OF_MEMBER(NOR_BM_LSH, DataStat);
  r     = _WriteOff(pInst, &Data, Off, sizeof(Data));
  return r;
}

#if FS_NOR_SUPPORT_ECC

/*********************************************************************
*
*       _SetLSH_ECCToEmpty
*
*  Function description
*    Marks the ECC of LSH as empty.
*
*  Parameters
*    pLSH       Header of the logical sector.
*
*  Additional information
*    This function assumes that the ECC status does not
*    contain any bit errors.
*/
static void _SetLSH_ECCToEmpty(NOR_BM_LSH * pLSH) {
  unsigned eccStat;

  eccStat = pLSH->ecc0Stat;
  if (eccStat == ECC_STAT_VALID) {
    pLSH->ecc0Stat = ECC_STAT_EMPTY;
  } else {
    eccStat = pLSH->ecc1Stat;
    if (eccStat == ECC_STAT_VALID) {
      pLSH->ecc1Stat = ECC_STAT_EMPTY;
    }
  }
}

/*********************************************************************
*
*       _SetLSH_ECCToValid
*
*  Function description
*    Marks the ECC of LSH as valid.
*
*  Parameters
*    pLSH       Header of the logical sector.
*
*  Additional information
*    This function assumes that the ECC status does not
*    contain any bit errors.
*/
static void _SetLSH_ECCToValid(NOR_BM_LSH * pLSH) {
  unsigned eccStat;

  eccStat = pLSH->ecc0Stat;
  if (eccStat == ECC_STAT_EMPTY) {
    pLSH->ecc0Stat = ECC_STAT_VALID;
  } else {
    eccStat = pLSH->ecc1Stat;
    if (eccStat == ECC_STAT_EMPTY) {
      pLSH->ecc1Stat = ECC_STAT_VALID;
    }
  }
}

/*********************************************************************
*
*       _WriteLogSectorECC0Stat
*
*  Function description
*    Writes to storage the status of the ECC0 of a LSH.
*
*  Parameters
*    pInst              Driver instance.
*    PhySectorIndex     Index of the physical sector that stores the logical sector.
*    srsi               Location of the logical sector in the physical sector.
*    eccStat            Value of the ECC status to be set.
*
*  Return value
*    ==0    OK, ECC status of the logical sector was modified.
*    !=0    An error occurred.
*/
static int _WriteLogSectorECC0Stat(NOR_BM_INST * pInst, unsigned PhySectorIndex, unsigned srsi, unsigned eccStat) {
  int r;
  U32 Off;
  U8  Data;

  Data  = (U8)eccStat;
  Off   = _GetLogSectorHeaderOff(pInst, PhySectorIndex, srsi);
  Off  += OFFSET_OF_MEMBER(NOR_BM_LSH, ecc0Stat);
  r     = _WriteOff(pInst, &Data, Off, sizeof(Data));
  return r;
}

/*********************************************************************
*
*       _WriteLogSectorECC1Stat
*
*  Function description
*    Writes to storage the status of the ECC1 in a LSH.
*
*  Parameters
*    pInst              Driver instance.
*    PhySectorIndex     Index of the physical sector that stores the logical sector.
*    srsi               Location of the logical sector in the physical sector.
*    eccStat            Value of the ECC status to be set.
*
*  Return value
*    ==0    OK, ECC status of the logical sector was modified.
*    !=0    An error occurred.
*/
static int _WriteLogSectorECC1Stat(NOR_BM_INST * pInst, unsigned PhySectorIndex, unsigned srsi, unsigned eccStat) {
  int r;
  U32 Off;
  U8  Data;

  Data  = (U8)eccStat;
  Off   = _GetLogSectorHeaderOff(pInst, PhySectorIndex, srsi);
  Off  += OFFSET_OF_MEMBER(NOR_BM_LSH, ecc1Stat);
  r     = _WriteOff(pInst, &Data, Off, sizeof(Data));
  return r;
}

#endif // FS_NOR_SUPPORT_ECC

#if (FS_NOR_SUPPORT_CRC != 0) || (FS_NOR_SUPPORT_ECC != 0)

/*********************************************************************
*
*       _CalcStoreLSHWithCRCAndECC
*
*  Function description
*    Calculates and stores to LSH the CRC and ECC of the management data.
*
*  Parameters
*    pInst            [IN] Driver instance.
*    pLSH             Logical sector header.
*    pDataCheck       CRC and ECC of the sector data. Can be set to NULL.
*
*  Return value
*    ==0      OK, CRC and ECC calculated.
*    !=0      An error occurred.
*/
static int _CalcStoreLSHWithCRCAndECC(NOR_BM_INST * pInst, NOR_BM_LSH * pLSH, const DATA_CHECK * pDataCheck) {
  int r;

  r = 0;
#if (FS_NOR_SUPPORT_CRC != 0) && (FS_NOR_SUPPORT_ECC != 0)
  if (_IsECCEnabled(pInst) != 0) {
    int NumBitsCorrected;

    if (pDataCheck != NULL) {
      FS_MEMCPY(pLSH->aaECCSectorData, pDataCheck->aaECC, sizeof(pLSH->aaECCSectorData));
      UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_LSH, aaECCSectorData), sizeof(pLSH->aaECCSectorData));
    }
    NumBitsCorrected = 0;
    r = pInst->pECC_API->pfCalcStoreLSH(pInst, pLSH, &NumBitsCorrected);
    if (r == 0) {
      UPDATE_NUM_BIT_ERRORS(pInst, NumBitsCorrected);
    }
  }
  if (r == 0) {
    if (_IsCRCEnabled(pInst) != 0) {
      if (pDataCheck != NULL) {
        pLSH->crcSectorData = pDataCheck->crc;
        UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_LSH, crcSectorData), sizeof(pLSH->crcSectorData));
      }
      r = _pCRC_API->pfCalcStoreLSH(pLSH);
    }
  }
#elif (FS_NOR_SUPPORT_CRC != 0) && (FS_NOR_SUPPORT_ECC == 0)
  if (_IsCRCEnabled(pInst) != 0) {
    if (pDataCheck != NULL) {
      pLSH->crcSectorData = pDataCheck->crc;
      UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_LSH, crcSectorData), sizeof(pLSH->crcSectorData));
    }
    r = _pCRC_API->pfCalcStoreLSH(pLSH);
  }
#elif (FS_NOR_SUPPORT_CRC == 0) && (FS_NOR_SUPPORT_ECC != 0)
  if (_IsECCEnabled(pInst) != 0) {
    int NumBitsCorrected;

    if (pDataCheck != NULL) {
      FS_MEMCPY(pLSH->aaECCSectorData, pDataCheck->aaECC, sizeof(pLSH->aaECCSectorData));
      UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_LSH, aaECCSectorData), sizeof(pLSH->aaECCSectorData));
    }
    NumBitsCorrected = 0;
    r = pInst->pECC_API->pfCalcStoreLSH(pInst, pLSH, &NumBitsCorrected);
    if (r == 0) {
      UPDATE_NUM_BIT_ERRORS(pInst, NumBitsCorrected);
    }
  }
#endif // FS_NOR_SUPPORT_CRC != 0 && FS_NOR_SUPPORT_ECC != 0
  return r;
}

#endif // FS_NOR_SUPPORT_CRC != 0 || FS_NOR_SUPPORT_ECC != 0

#if (FAIL_SAFE_ERASE_NO_REWRITE != 0) && (FS_NOR_SUPPORT_ECC != 0)

/*********************************************************************
*
*       _CalcStoreLSH_NRWithECC
*
*  Function description
*    Calculates and stores to LSH the ECC of the management data
*    for configurations with no incremental write support.
*
*  Parameters
*    pInst            [IN] Driver instance.
*    pLSH             Logical sector header.
*
*  Return value
*    ==0      OK, CRC and ECC calculated.
*    !=0      An error occurred.
*
*  Additional information
*    This function assumes that the support for incremental write
*    operations is disabled. No CRC is calculated because the
*    valid and invalid indicators are checked by value.
*/
static int _CalcStoreLSH_NRWithECC(const NOR_BM_INST * pInst, NOR_BM_LSH * pLSH) {
  int      r;
  unsigned DataStat;

  r        = 0;
  DataStat = _GetLogSectorDataStat(pInst, pLSH);
  if (_IsECCEnabled(pInst) != 0) {
    if (DataStat == DATA_STAT_VALID) {
      r = pInst->pECC_API->pfCalcStoreLSH_Data(pInst, pLSH);
    } else {
      if (DataStat == DATA_STAT_INVALID) {
        r = pInst->pECC_API->pfCalcStoreLSH_Invalid(pInst, pLSH);
      }
    }
  }
  return r;
}

#endif // FAIL_SAFE_ERASE_NO_REWRITE != 0 && FS_NOR_SUPPORT_ECC != 0

#if (FS_NOR_CAN_REWRITE == 0)

/*********************************************************************
*
*       _WriteLogSectorDataStatSlow
*
*  Function description
*    Modifies the data validity of a logical sector (Read-Modify-Write version).
*
*  Parameters
*    pInst            Driver instance.
*    PhySectorIndex   Index of the physical sector where the logical sector is located.
*    srsi             Index of the logical sector in the physical sector.
*    DataStat         Status of the data to be set.
*
*  Return value
*    ==0    OK, data status of the logical sector has been modified.
*    !=0    An error occurred.
*/
static int _WriteLogSectorDataStatSlow(NOR_BM_INST * pInst, unsigned PhySectorIndex, unsigned srsi, unsigned DataStat) {
  int          r;
  NOR_BM_LSH   lsh;

  FS_MEMSET(&lsh, 0xFF, sizeof(lsh));
  r = _ReadLSH(pInst, PhySectorIndex, srsi, &lsh);
  if (r == 0) {
    INIT_LSH_DATA_RANGE();
    _SetLogSectorDataStat(pInst, &lsh, DataStat);
    r = _WriteLSH(pInst, PhySectorIndex, srsi, &lsh);
    INIT_LSH_DATA_RANGE();
  }
  return r;
}

#endif // FS_NOR_CAN_REWRITE == 0

/*********************************************************************
*
*       _WriteLogSectorBRSIFast
*
*  Function description
*    Modifies the block-relative index of a logical sector.
*
*  Parameters
*    pInst            Driver instance.
*    PhySectorIndex   Index of the physical sector that stores the logical sector.
*    srsi             Index of the logical sector in the physical sector.
*    brsi             Index of the logical sector in the data block.
*
*  Return value
*    ==0      OK, BRSI modified successfully.
*    !=0      An error occurred.
*
*  Additional information
*    This function assumes that the support for incremental write
*    operations is enabled.
*/
static int _WriteLogSectorBRSIFast(NOR_BM_INST * pInst, unsigned PhySectorIndex, unsigned srsi, U16 brsi) {
  int r;
  U32 Off;

  Off  = _GetLogSectorHeaderOff(pInst, PhySectorIndex, srsi);
  Off += OFFSET_OF_MEMBER(NOR_BM_LSH, brsi);
#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  _pMultiByteAPI->pfStoreU16(SEGGER_PTR2PTR(U8, &brsi), brsi);                                // MISRA deviation D:100e
#endif
  r = _WriteOff(pInst, &brsi, Off, sizeof(brsi));
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR_BM: WRITE_SECTOR_BRSI PSI: %u, SRSI: %u, BRSI: %u, r: %d\n", PhySectorIndex, srsi, brsi, r));
  return r;
}

#if (FS_NOR_CAN_REWRITE == 0)

/*********************************************************************
*
*       _WriteLogSectorBRSISlow
*
*  Function description
*    Modifies the block-relative index of a logical sector.
*
*  Parameters
*    pInst            Driver instance.
*    PhySectorIndex   Index of the physical sector that stores the logical sector.
*    srsi             Index of the logical sector in the physical sector.
*    brsi             Index of the logical sector in the data block.
*
*  Return value
*    ==0      OK, BRSI modified successfully.
*    !=0      An error occurred.
*
*  Additional information
*    This function is implemented as a read-modify-write operation.
*    The BRSI is stored as an entire flash line.
*/
static int _WriteLogSectorBRSISlow(NOR_BM_INST * pInst, unsigned PhySectorIndex, unsigned srsi, U16 brsi) {
  int        r;
  NOR_BM_LSH lsh;

  FS_MEMSET(&lsh, 0xFF, sizeof(lsh));
  r = _ReadLSH(pInst, PhySectorIndex, srsi, &lsh);
  if (r == 0) {
    INIT_LSH_DATA_RANGE();
    lsh.brsi = brsi;
    UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_LSH, brsi), sizeof(lsh.brsi));
    r = _WriteLSH(pInst, PhySectorIndex, srsi, &lsh);
    INIT_LSH_DATA_RANGE();
  }
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR_BM: WRITE_SECTOR_BRSI PSI: %u, SRSI: %u, BRSI: %u, r: %d\n", PhySectorIndex, srsi, brsi, r));
  return r;
}

#endif // FS_NOR_CAN_REWRITE == 0

#if FS_NOR_CAN_REWRITE

/*********************************************************************
*
*       _ReadLogSectorDataStatFast
*
*   Function description
*     Reads only the status from the header of a logical sector (fast version).
*/
static int _ReadLogSectorDataStatFast(NOR_BM_INST * pInst, unsigned PhySectorIndex, unsigned srsi, NOR_BM_LSH * pLSH) {
  int r;
  U32 Off;

  FS_MEMSET(pLSH, 0xFF, sizeof(NOR_BM_LSH));
  Off  = _GetLogSectorHeaderOff(pInst, PhySectorIndex, srsi);
  Off += OFFSET_OF_MEMBER(NOR_BM_LSH, DataStat);
  r    = _ReadOff(pInst, &pLSH->DataStat, Off, sizeof(pLSH->DataStat));
  return r;
}

#endif // FS_NOR_CAN_REWRITE

/*********************************************************************
*
*       _ReadLogSectorDataStat
*
*  Function description
*    Reads only the status from the header of a logical sector.
*/
static int _ReadLogSectorDataStat(NOR_BM_INST * pInst, unsigned PhySectorIndex, unsigned srsi, unsigned * pDataStat) {
  NOR_BM_LSH lsh;
  int        r;
  unsigned   DataStat;

  FS_MEMSET(&lsh, 0xFF, sizeof(NOR_BM_LSH));
#if FS_NOR_CAN_REWRITE
  if ((_IsCRCEnabled(pInst) != 0) || _IsECCEnabled(pInst) != 0) {
    r = _ReadLSH(pInst, PhySectorIndex, srsi, &lsh);
  } else {
    r = _ReadLogSectorDataStatFast(pInst, PhySectorIndex, srsi, &lsh);
  }
#else
  r = _ReadLSH(pInst, PhySectorIndex, srsi, &lsh);
#endif
  DataStat = 0;
  if (r == 0) {
    DataStat = _GetLogSectorDataStat(pInst, &lsh);
  }
  if (pDataStat != NULL) {
    *pDataStat = DataStat;
  }
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR_BM: READ_SECTOR_STAT PSI: %u, SRSI: %u, DataStat: %s, r: %d\n", PhySectorIndex, srsi, _TypeToName(DataStat), r));
  return r;
}

#if FS_NOR_CAN_REWRITE

/*********************************************************************
*
*       _ReadPhySectorDataCntFast
*
*  Function description
*    Reads the data count of a phy. sector (fast version).
*/
static int _ReadPhySectorDataCntFast(NOR_BM_INST * pInst, unsigned PhySectorIndex, NOR_BM_PSH * pPSH) {
  int r;
  U32 Off;

  Off = 0;
  FS_MEMSET(pPSH, 0xFF, sizeof(NOR_BM_PSH));
  _GetPhySectorInfo(pInst, PhySectorIndex, &Off, NULL);
  Off += OFFSET_OF_MEMBER(NOR_BM_PSH, DataCnt);
  r    = _ReadOff(pInst, &pPSH->DataCnt, Off, sizeof(pPSH->DataCnt));
  return r;
}

#endif // FS_NOR_CAN_REWRITE

/*********************************************************************
*
*       _ReadPhySectorDataCnt
*
*  Function description
*    Reads the data count of a phy. sector.
*
*  Additional information
*    The data count value is used to tell the difference between two
*    copies of the same logical block.
*/
static int _ReadPhySectorDataCnt(NOR_BM_INST * pInst, unsigned PhySectorIndex, U8 * pDataCnt) {
  int        r;
  U8         DataCnt;
  NOR_BM_PSH psh;

  FS_MEMSET(&psh, 0xFF, sizeof(NOR_BM_PSH));
#if FS_NOR_CAN_REWRITE
  if ((_IsCRCEnabled(pInst) != 0) || (_IsECCEnabled(pInst) != 0)) {
    r = _ReadPSH(pInst, PhySectorIndex, &psh);
  } else {
    r = _ReadPhySectorDataCntFast(pInst, PhySectorIndex, &psh);
  }
#else
  r = _ReadPSH(pInst, PhySectorIndex, &psh);
#endif
  DataCnt = (U8)_GetPhySectorDataCnt(pInst, &psh);
  if (pDataCnt != NULL) {
    *pDataCnt = DataCnt;
  }
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR_BM: READ_SECTOR_DATA_CNT PSI: %u, DataCnt: %d, r: %d\n", PhySectorIndex, DataCnt, r));
  return r;
}

/*********************************************************************
*
*       _LogSectorIndex2LogBlockIndex
*
*   Function description
*     Computes the index of the logical block where a logical sector is stored.
*     The position of the logical sector in the logical block is returned in pBRSI.
*/
static unsigned _LogSectorIndex2LogBlockIndex(const NOR_BM_INST * pInst, U32 LogSectorIndex, unsigned * pBRSI) {
  unsigned lbi;
  U32      brsi;

  lbi    = FS__DivModU32(LogSectorIndex, pInst->LSectorsPerPSector, &brsi);
  *pBRSI = brsi;
  return lbi;
}

#if FS_NOR_SUPPORT_FAIL_SAFE_ERASE

/*********************************************************************
*
*       _WritePhySectorEraseSignature
*
*  Function description
*    Modifies the erase signature in the header of a physical sector.
*
*  Parameters
*    pInst            Driver instance.
*    PhySectorIndex   Index of the physical sector.
*    Signature        Value to be stored.
*
*  Return value
*    ==0    OK, erase signature was modified.
*    !=0    An error occurred.
*/
static int _WritePhySectorEraseSignature(NOR_BM_INST * pInst, unsigned PhySectorIndex, U32 Signature) {
  int        r;
  U32        Off;
  const U8 * pData8;
  U8         Data8;
  unsigned   NumBytes;
#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  U8         aSignature[sizeof(Signature)];
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER

  _GetPhySectorInfo(pInst, PhySectorIndex, &Off, NULL);
  Off += OFFSET_OF_MEMBER(NOR_BM_PSH, EraseSignature);
#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  _pMultiByteAPI->pfStoreU32(aSignature, Signature);
  pData8 = aSignature;
#else
  pData8 = SEGGER_PTR2PTR(U8, &Signature);                                                    // MISRA deviation D:100e
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  //
  // This is critical data that has to be written byte by byte
  // to reduce the chance that a power failure interrupts
  // the write operation of the NOR flash device leaving
  // the memory cell in an unstable condition.
  //
  NumBytes = sizeof(Signature);
  do {
    Data8 = *pData8++;
    r = _WriteOff(pInst, &Data8, Off, sizeof(Data8));
    if (r != 0) {
      break;
    }
    ++Off;
  } while (--NumBytes != 0u);
  return r;
}

/*********************************************************************
*
*        _ErasePhySectorFailSafe
*
*  Function description
*    Erases a phy. sector and marks it as erased.
*
*  Parameters
*    pInst            Driver instance.
*    PhySectorIndex   Index of the phy. sector to be erased.
*    pEraseCnt        [IN]  Initial value of the erase count.
*                           If ERASE_CNT_INVALID is passed then the erase
*                           count is taken from the header of the phy. sector.
*                     [OUT] Actual value of the erase count.
*
*  Return value
*    ==0      OK, phy. sector erased.
*    !=0      An error occurred.
*
*  Additional information
*    A signature is stored to the phy. sector header to indicate that
*    the phy. sector has been successfully erased. This signature is
*    invalidated before the erase operation. If the erase operation is
*    interrupted by an unexpected reset the low-level mount operation
*    will detect that the signature is missing and the phy. sector
*    is erased again before use.
*    The erase count is also stored together with the erase signature
*    in order to minimize the chance that an unexpected reset corrupts
*    its value. Before being written to phy. sector the erase count
*    is incremented by 1.
*/
static int _ErasePhySectorFailSafe(NOR_BM_INST * pInst, U32 PhySectorIndex, U32 * pEraseCnt) {
  int        r;
  int        FailSafeErase;
  NOR_BM_PSH psh;
  U32        EraseCnt;
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS)
  U32        ErrorFilter;
#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS
  U32        EraseSignature;

  EraseCnt       = ERASE_CNT_INVALID;
  if (pEraseCnt != NULL) {
    EraseCnt     = *pEraseCnt;
  }
  FS_MEMSET(&psh, 0xFF, sizeof(NOR_BM_PSH));
  FailSafeErase = (int)pInst->FailSafeErase;
  if (FailSafeErase != 0) {
    INIT_PSH_DATA_RANGE();
    (void)_ReadPSH(pInst, PhySectorIndex, &psh);
    //
    // First, mark the phy. sector as being erased. Invalidate the signature only if it is valid
    // because writing to partially erased sectors may cause write errors.
    //
    EraseSignature = _GetPhySectorEraseSignature(&psh);
    if (EraseSignature != ERASE_SIGNATURE_INVALID) {
      //
      // We disable temporarily the error messages and give only a warning that the erase operation
      // is no longer fail safe.
      //
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS)
      ErrorFilter = FS__GetErrorFilterNL();
      FS__SetErrorFilterNL(ErrorFilter & ~FS_MTYPE_DRIVER);
#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS
#if (FS_NOR_CAN_REWRITE == 0)
      if (_IsRewriteSupported(pInst) == 0) {
        unsigned DataStat;

        r = 0;
        //
        // If the physical sector is empty then we mark it as invalid before the erase operation
        // so that we can check if the operation finished successfully.
        //
        DataStat = _GetPhySectorDataStat(pInst, &psh);
        if (DataStat == DATA_STAT_EMPTY) {
          _SetPhySectorDataStat(pInst, &psh, DATA_CNT_INVALID);
          r = _WritePSH(pInst, PhySectorIndex, &psh);
        }
      } else {
        r = _WritePhySectorEraseSignature(pInst, PhySectorIndex, ERASE_SIGNATURE_INVALID);
      }
#else
      r = _WritePhySectorEraseSignature(pInst, PhySectorIndex, ERASE_SIGNATURE_INVALID);
#endif // FS_NOR_CAN_REWRITE
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS)
      FS__SetErrorFilterNL(ErrorFilter);
#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS
      if (r != 0) {
        FS_DEBUG_WARN((FS_MTYPE_DRIVER, "NOR_BM: _ErasePhySectorFailSafe: Erase operation of sector %d is not fail safe.", PhySectorIndex));
      }
    } else {
      //
      // If the erase signature is not valid it is a great probability
      // that the erase count is also not valid. Therefore we invalidate
      // the value of the erase count here.
      //
      FS_MEMSET(&psh, 0xFF, sizeof(psh));
    }
    INIT_PSH_DATA_RANGE();
  }
  //
  // Erase the phy. sector.
  //
  r = _ErasePhySector(pInst, PhySectorIndex, NULL);
  if (r != 0) {
    return r;       // Error, could not erase phy. sector.
  }
  if (FailSafeErase != 0) {
    INIT_PSH_DATA_RANGE();
    //
    // Set the erase count here to minimize the chance that an unexpected reset corrupts its value.
    //
    if (EraseCnt == ERASE_CNT_INVALID) {
      EraseCnt = _GetPhySectorEraseCnt(pInst, &psh);
    }
    FS_MEMSET(&psh, 0xFF, sizeof(psh));
    ++EraseCnt;
    psh.EraseCnt = EraseCnt;
    UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_PSH, EraseCnt), sizeof(psh.EraseCnt));
#if (FS_NOR_CAN_REWRITE == 0)
    if (_IsRewriteSupported(pInst) == 0) {
      _SetPhySectorEraseSignature(pInst, &psh, ERASE_SIGNATURE_VALID);
      r = _WritePSH(pInst, PhySectorIndex, &psh);
    } else
#endif // FS_NOR_CAN_REWRITE == 0
    {
      //
      // Write the erase count and the erase signature separately
      // to detect if the value of the erase count was written correctly.
      // If the erase signature is not valid there is a great probability
      // that the erase count value was not written correctly.
      //
      r = _WritePSH(pInst, PhySectorIndex, &psh);
      if (r == 0) {
        //
        // Fail-safe TP. If the power fails at this point we end up with a phy. sector which is
        // completely erased but it does not have a valid erase signature.
        //
        CALL_TEST_HOOK_FAIL_SAFE(pInst->Unit);

        //
        // Mark the phy. sector as erased.
        //
        r = _WritePhySectorEraseSignature(pInst, PhySectorIndex, ERASE_SIGNATURE_VALID);
      }
    }
    INIT_PSH_DATA_RANGE();
  } else {
    if (EraseCnt != ERASE_CNT_INVALID) {
      ++EraseCnt;
    }
  }
  if (pEraseCnt != NULL) {
    *pEraseCnt = EraseCnt;
  }
  return r;
}

#endif // FS_NOR_SUPPORT_FAIL_SAFE_ERASE

/*********************************************************************
*
*       _MarkPhySectorAsFree
*
*   Function description
*     Marks physical sector as free in management data.
*/
static void _MarkPhySectorAsFree(NOR_BM_INST * pInst, unsigned PhySectorIndex) {
  unsigned   Mask;
  unsigned   Data;
  U8       * pData;

  if (PhySectorIndex >= pInst->NumPhySectors) {
    return;
  }
  if (pInst->pFreeMap == NULL) {
    return;
  }
  Mask  = 1uL << (PhySectorIndex & 7u);
  pData = pInst->pFreeMap + (PhySectorIndex >> 3);
  Data  = *pData;
#if FS_NOR_ENABLE_STATS
  if ((Data & Mask) == 0u) {
    pInst->StatCounters.NumFreeBlocks++;
  }
#endif
  Data   |= Mask;
  *pData  = (U8)Data;         // Mark physical sector as being free.
}

/*********************************************************************
*
*       _MarkPhySectorAsAllocated
*
*   Function description
*     Mark a physical sector as "in use" in management data.
*/
static void _MarkPhySectorAsAllocated(NOR_BM_INST * pInst, unsigned PhySectorIndex) {
  unsigned   Mask;
  unsigned   Data;
  U8       * pData;

  Mask  = 1uL << (PhySectorIndex & 7u);
  pData = pInst->pFreeMap + (PhySectorIndex >> 3);
  Data  = *pData;
#if FS_NOR_ENABLE_STATS
  if ((Data & Mask) != 0u) {
    pInst->StatCounters.NumFreeBlocks--;
  }
#endif // FS_NOR_ENABLE_STATS
  Data   &= ~Mask;
  *pData  = (U8)Data;    // Mark physical sector as being allocated.
}

/*********************************************************************
*
*       _IsPhySectorFree
*
*   Function description
*     Checks if physical sector is free to use.
*/
static int _IsPhySectorFree(const NOR_BM_INST * pInst, unsigned PhySectorIndex) {
  unsigned   Mask;
  U8       * pData;
  int        IsFree;

  Mask   = 1uL << (PhySectorIndex & 7u);
  pData  = pInst->pFreeMap + (PhySectorIndex >> 3);
  IsFree = 0;
  if ((*pData & Mask) != 0u) {
    IsFree = 1;
  }
  return IsFree;
}

#if (FS_NOR_CAN_REWRITE == 0)

/*********************************************************************
*
*       _IsLineBlank
*/
static int _IsLineBlank(const void * p) {
  int IsBlank;

  IsBlank = 1;
#if ((FS_NOR_LINE_SIZE % 4) == 0)
  {
    unsigned    NumItems;
    const U32 * pData32;

    NumItems = (unsigned)FS_NOR_LINE_SIZE >> 2;
    pData32  = SEGGER_CONSTPTR2PTR(const U32, p);   // The a flash line is always aligned to a line size. MISRA deviation D:100e
    do {
      if (*pData32++ != 0xFFFFFFFFuL) {
        IsBlank = 0;                                // The line is not blank.
        break;
      }
    } while (--NumItems != 0u);
  }
#else
  {
    unsigned   NumBytes;
    const U8 * pData8;

    NumBytes = FS_NOR_LINE_SIZE;
    pData8   = SEGGER_CONSTPTR2PTR(const U8, p);    // MISRA deviation D:100e
    do {
      if (*pData8++ != 0xFFu) {
        IsBlank = 0;                            // The line is not blank.
        break;
      }
    } while (--NumBytes != 0u);
  }
#endif // (FS_NOR_LINE_SIZE % 4) == 0
  return IsBlank;
}

#endif // (FS_NOR_CAN_REWRITE == 0)

/*********************************************************************
*
*       _PreErasePhySector
*
*   Function description
*     Pre-erasing means writing an value into the data status which indicates that
*     the data is invalid and the physical sector needs to be erased.
*/
static int _PreErasePhySector(NOR_BM_INST * pInst, unsigned PhySectorIndex) {
  NOR_BM_PSH psh;
  int        r;
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS)
  U32        ErrorFilter;
#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS
  U8         DataStat;

  INIT_PSH_DATA_RANGE();
  _MarkPhySectorAsFree(pInst, PhySectorIndex);
  //
  // We disable temporarily the error messages since we perform error recovery
  // if the invalidate operation fails.
  //
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS)
  ErrorFilter = FS__GetErrorFilterNL();
  FS__SetErrorFilterNL(ErrorFilter & ~FS_MTYPE_DRIVER);
#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS
  FS_MEMSET(&psh, 0xFF, sizeof(psh));
  r = _ReadPSH(pInst, PhySectorIndex, &psh);
  if (r == 0) {
    DataStat = (U8)_GetPhySectorDataStat(pInst, &psh);
    if (DataStat != DATA_STAT_INVALID) {
#if (FS_NOR_CAN_REWRITE == 0)
      //
      // We can invalidate the phy. sector only when the flash
      // line that stores the invalid status flag is blank.
      //
      if (_IsLineBlank(&psh.IsInvalid) == 0) {
        r = 1;                    // The phy. sector has to be erased.
      } else
#endif // FS_NOR_CAN_REWRITE == 0
      {
        _SetPhySectorDataStat(pInst, &psh, DATA_STAT_INVALID);
#if (FS_NOR_SUPPORT_CRC != 0) && (FS_NOR_SUPPORT_ECC != 0)
        if (_IsRewriteSupported(pInst) != 0) {
          if (_IsECCEnabled(pInst) != 0) {
            int NumBitsCorrected;

            r = pInst->pECC_API->pfCalcStorePSH(pInst, &psh, &NumBitsCorrected);
            UPDATE_NUM_BIT_ERRORS(pInst, NumBitsCorrected);
          }
          if (r == 0) {
            if (_IsCRCEnabled(pInst) != 0) {
              r = _pCRC_API->pfCalcStorePSH(pInst, &psh);
            }
          }
        }
        if (r == 0)
#elif (FS_NOR_SUPPORT_CRC != 0)  && (FS_NOR_SUPPORT_ECC == 0)
        if (_IsRewriteSupported(pInst) != 0) {
          if (_IsCRCEnabled(pInst) != 0) {
            r = _pCRC_API->pfCalcStorePSH(pInst, &psh);
          }
        }
        if (r == 0)
#elif (FS_NOR_SUPPORT_CRC == 0)  && (FS_NOR_SUPPORT_ECC != 0)
        if (_IsRewriteSupported(pInst) != 0) {
          if (_IsECCEnabled(pInst) != 0) {
            int NumBitsCorrected;

            NumBitsCorrected = 0;
            r = pInst->pECC_API->pfCalcStorePSH(pInst, &psh, &NumBitsCorrected);
            UPDATE_NUM_BIT_ERRORS(pInst, NumBitsCorrected);
          }
        }
        if (r == 0)
#endif // FS_NOR_SUPPORT_CRC != 0  && FS_NOR_SUPPORT_ECC != 0
        {
          r = _WritePSH(pInst, PhySectorIndex, &psh);
        }
      }
    }
  }
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS)
  FS__SetErrorFilterNL(ErrorFilter);
#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS
  //
  // Some NOR flash devices (for example N25Q128A) report a write
  // error if the phy. sector has not been erased correctly.
  // If this is the case, we erase the phy. block again here
  // to make sure that the next write operations to this phy. sector
  // will succeed.
  //
  if (r != 0) {
    if (pInst->EraseCntMax == 0u) {
      //
      // If the maximum erase count is not available (for example during mount operation)
      // we only erase the physical sector without storing the erase count.
      // The erase count will be stored later when the phy. sector is used.
      //
      r = _ErasePhySector(pInst, PhySectorIndex, NULL);
    } else {
      U32 * pEraseCnt;          // Just to silence PC-lint

      pEraseCnt = NULL;
      r = ERASE_PHY_SECTOR(pInst, PhySectorIndex, pEraseCnt);
    }
  }
  INIT_PSH_DATA_RANGE();
  IF_STATS(pInst->StatCounters.PreEraseCnt++);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR_BM: PRE_ERASE PSI: %u, r: %d\n", PhySectorIndex, r));
  return r;
}

/*********************************************************************
*
*       _L2P_Read
*
*  Function description
*    Returns the contents of the given entry in the L2P table (physical sector lookup table)
*/
static unsigned _L2P_Read(const NOR_BM_INST * pInst, U32 LogIndex) {
  U32 v;

  v = FS_BITFIELD_ReadEntry(pInst->pLog2PhyTable, LogIndex, pInst->NumBitsPhySectorIndex);
  return v;
}

/*********************************************************************
*
*       _L2P_Write
*
*  Function description
*    Updates the contents of the given entry in the L2P table (physical sector lookup table)
*/
static void _L2P_Write(const NOR_BM_INST * pInst, U32 LogIndex, unsigned v) {
  FS_BITFIELD_WriteEntry(pInst->pLog2PhyTable, LogIndex, pInst->NumBitsPhySectorIndex, v);
}

/*********************************************************************
*
*       _L2P_GetSize
*
*  Function description
*    Computes & returns the size of the L2P assignment table of a work block.
*    Is used before allocation to find out how many bytes need to be allocated.
*/
static unsigned _L2P_GetSize(const NOR_BM_INST * pInst) {
  unsigned v;

  v = FS_BITFIELD_CalcSize(pInst->NumLogBlocks, pInst->NumBitsPhySectorIndex);
  return v;
}

/*********************************************************************
*
*       _WB_IsSectorUsed
*
*  Function description
*    Checks if any data was stored to a logical sector of a work block.
*
*  Parameters
*    pWorkBlock     Work block that stores the logical sector.
*    brsi           Index of the logical sector (relative to logical block)
*
*  Return value
*    ==0      Logical sector is not used.
*    !=0      Logical sector is used.
*/
static int _WB_IsSectorUsed(const NOR_BM_WORK_BLOCK * pWorkBlock, unsigned brsi) {
  unsigned Data;
  unsigned Off;
  int      r;

  Off    = brsi >> 3;
  Data   = pWorkBlock->paIsWritten[Off];
  Data >>= brsi & 7u;
  r = 0;
  if ((Data & 1u) != 0u) {
    r = 1;
  }
  return r;
}

/*********************************************************************
*
*       _WB_MarkSectorAsUsed
*
*  Function description
*    Marks a logical sector as used in a work block.
*
*  Parameters
*    pWorkBlock     Work block that stores the logical sector.
*    brsi           Index of the logical sector (relative to logical block)
*/
static void _WB_MarkSectorAsUsed(const NOR_BM_WORK_BLOCK * pWorkBlock, unsigned brsi) {
  unsigned Mask;
  unsigned Off;
  unsigned Data;

  Off   = brsi >> 3;
  Mask  = 1uL << (brsi & 7u);
  Data  = pWorkBlock->paIsWritten[Off];
  Data |= Mask;
  pWorkBlock->paIsWritten[Off] = (U8)Data;
}

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
static unsigned _WB_ReadAssignment(const NOR_BM_INST * pInst, const NOR_BM_WORK_BLOCK * pWorkBlock, unsigned Index) {
  unsigned r;

  r = FS_BITFIELD_ReadEntry(SEGGER_CONSTPTR2PTR(const U8, pWorkBlock->paAssign), Index, pInst->NumBitsSRSI);    // MISRA deviation D:100e
  return r;
}

/*********************************************************************
*
*       _WB_WriteAssignment
*
*  Function description
*    Writes an entry in the assignment table of a work block.
*    It is necessary to use a subroutine to do the job since the entries are stored in a bit field.
*    Logically, the code does the following:
*      pWorkBlock->aAssign[Index] = v;
*/
static void _WB_WriteAssignment(const NOR_BM_INST * pInst, const NOR_BM_WORK_BLOCK * pWorkBlock, unsigned Index, unsigned v) {
  FS_BITFIELD_WriteEntry(SEGGER_PTR2PTR(U8, pWorkBlock->paAssign), Index, pInst->NumBitsSRSI, v);               // MISRA deviation D:100e
}

/*********************************************************************
*
*       _WB_GetAssignmentSize
*
*  Function description
*    Returns the size of the assignment table of a work block.
*/
static unsigned _WB_GetAssignmentSize(const NOR_BM_INST * pInst) {
  unsigned v;

  v = FS_BITFIELD_CalcSize(1uL << pInst->NumBitsSRSI, pInst->NumBitsSRSI);
  return v;
}

/*********************************************************************
*
*       _FindFreeSectorInWorkBlock
*
*  Function description
*    Locate a free sector in a work block.
*
*  Return value
*    !=BRSI_INVALID     Block-relative sector index of the free sector
*    ==BRSI_INVALID     No free sector
*
*  Additional information
*    If available, we try to locate the BRSI at the native position, meaning physical BRSI = logical BRSI,
*    because this leaves the option to later convert the work block into a data block without copying the data.
*/
static unsigned _FindFreeSectorInWorkBlock(const NOR_BM_INST * pInst, const NOR_BM_WORK_BLOCK * pWorkBlock, unsigned brsi) {
  unsigned NumSectors;
  int      i;

  //
  // Preferred position is the real position within the block. So we first check if it is available.
  //
  if (_WB_IsSectorUsed(pWorkBlock, brsi) == 0) {
    return brsi;
  }
  //
  // Preferred position is taken. Let's use first free position.
  //
  NumSectors = pInst->LSectorsPerPSector;
  i = _Find0BitInArray(pWorkBlock->paIsWritten, 1, NumSectors - 1u);   // Returns bit position (1 ... NumSectors - 1) if a 0-bit has been found, else -1
  if (i > 0) {
    return (unsigned)i;
  }
  return BRSI_INVALID;     // No free logical sector in this block
}

/*********************************************************************
*
*       _WB_RemoveFromList
*
*  Function description
*    Removes a given work block from list of work blocks.
*/
static void _WB_RemoveFromList(const NOR_BM_WORK_BLOCK * pWorkBlock, NOR_BM_WORK_BLOCK ** ppFirst) {
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
static void _WB_AddToList(NOR_BM_WORK_BLOCK * pWorkBlock, NOR_BM_WORK_BLOCK ** ppFirst) {
  NOR_BM_WORK_BLOCK * pPrevFirst;

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
static void _WB_RemoveFromUsedList(NOR_BM_INST * pInst, const NOR_BM_WORK_BLOCK * pWorkBlock) {
  _WB_RemoveFromList(pWorkBlock, &pInst->pFirstWorkBlockInUse);
}

/*********************************************************************
*
*       _WB_AddToUsedList
*
*  Function description
*    Adds a given work block to the list of used work blocks.
*/
static void _WB_AddToUsedList(NOR_BM_INST * pInst, NOR_BM_WORK_BLOCK * pWorkBlock) {
  _WB_AddToList(pWorkBlock, &pInst->pFirstWorkBlockInUse);
}

/*********************************************************************
*
*       _WB_RemoveFromFreeList
*
*  Function description
*    Removes a given work block from list of free work blocks.
*/
static void _WB_RemoveFromFreeList(NOR_BM_INST * pInst, const NOR_BM_WORK_BLOCK * pWorkBlock) {
  _WB_RemoveFromList(pWorkBlock, &pInst->pFirstWorkBlockFree);
}

/*********************************************************************
*
*       _WB_AddToFreeList
*
*  Function description
*    Adds a given work block to the list of free work blocks.
*/
static void _WB_AddToFreeList(NOR_BM_INST * pInst, NOR_BM_WORK_BLOCK * pWorkBlock) {
  _WB_AddToList(pWorkBlock, &pInst->pFirstWorkBlockFree);
}

/*********************************************************************
*
*       _brsi2srsi
*
*  Function description
*    Calculates the location of a logical sector in a work block.
*
*  Parameters
*    pInst        Driver instance. It cannot be NULL.
*    pWorkBlock   Work block to be searched. It cannot be NULL.
*    brsi         Index of the logical sector relative to the logical block.
*
*  Return value
*    !=BRSI_INVALID     Index of the logical sector relative to physical sector that stores the data.
*    ==BRSI_INVALID     Logical sector is not located in the work block or it was invalidated.
*/
static unsigned _brsi2srsi(NOR_BM_INST * pInst, const NOR_BM_WORK_BLOCK * pWorkBlock, unsigned brsi) {
  unsigned srsi;
  unsigned DataStat;
  int      r;

  //
  // In case of logical sector index != 0 we need to check the physical sector index.
  // The physical sector index of such a logical sector index will never be zero,
  // since we do not assign such a value to a logical sector index.
  // (see function _FindFreeSectorInWorkBlock)
  //
  if (brsi != 0u) {
    srsi = _WB_ReadAssignment(pInst, pWorkBlock, brsi);
    if (srsi == 0u) {
      return BRSI_INVALID;
    }
    return srsi;
  }
  //
  // brsi == 0 (first sector in work block) requires special handling.
  //
  if (_WB_IsSectorUsed(pWorkBlock, 0) == 0) {
    return BRSI_INVALID;
  }
  srsi = _WB_ReadAssignment(pInst, pWorkBlock, 0);
  //
  // srsi == 0 has 2 different meanings:
  //    1. Logical sector 0 is stored on sector 0 of the physical sector
  //    2. Logical sector 0 has been invalidated
  // We have to differentiate between these and return the correct value.
  //
  if (srsi == 0u) {
    unsigned psiWork;

    srsi    = BRSI_INVALID;
    psiWork = pWorkBlock->psi;
    r = _ReadLogSectorDataStat(pInst, psiWork, 0, &DataStat);
    if (r == 0) {
      if (DataStat == DATA_STAT_VALID) {
        srsi = 0;
      }
    }
  }
  return srsi;
}

#if FS_NOR_OPTIMIZE_DATA_WRITE

/*********************************************************************
*
*       _DB_IsSectorUsed
*
*  Function description
*    Checks if any data was stored to a logical sector of a data block.
*
*  Parameters
*    pDataBlock     Data block that stores the logical sector.
*    brsi           Index of the logical sector (relative to logical block)
*
*  Return value
*    ==0      Logical sector not used.
*    !=0      Logical sector used.
*/
static int _DB_IsSectorUsed(const NOR_BM_DATA_BLOCK * pDataBlock, unsigned brsi) {
  unsigned Data;
  unsigned Off;
  int      r;

  Off    = brsi >> 3;
  Data   = pDataBlock->paIsWritten[Off];
  Data >>= brsi & 7u;
  r = 0;
  if ((Data & 1u) != 0u) {
    r = 1;
  }
  return r;
}

/*********************************************************************
*
*       _DB_MarkSectorAsUsed
*
*  Function description
*    Marks sector as being written.
*
*  Parameters
*    pDataBlock     Data block that stores the logical sector.
*    brsi           Index of the logical sector (relative to logical block)
*/
static void _DB_MarkSectorAsUsed(const NOR_BM_DATA_BLOCK * pDataBlock, unsigned brsi) {
  unsigned Mask;
  unsigned Off;
  unsigned Data;

  Off   = brsi >> 3;
  Mask  = 1uL << (brsi & 7u);
  Data  = pDataBlock->paIsWritten[Off];
  Data |= Mask;
  pDataBlock->paIsWritten[Off] = (U8)Data;
}

/*********************************************************************
*
*       _DB_RemoveFromList
*
*  Function description
*    Removes the specified data block from a data block list.
*
*  Parameters
*    pDataBlock     Data block to be removed.
*    ppFirst        Address of the first data block in the list.
*/
static void _DB_RemoveFromList(const NOR_BM_DATA_BLOCK * pDataBlock, NOR_BM_DATA_BLOCK ** ppFirst) {
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL)
  {
    NOR_BM_DATA_BLOCK * pDataBlockToCheck;

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
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR_BM: Data block is not contained in the list."));
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);
    }
  }
#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  if (pDataBlock != NULL) {
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
}

/*********************************************************************
*
*       _DB_AddToList
*
*  Function description
*    Adds the specified data block to a data block list.
*
*  Parameters
*    pDataBlock     Data block to be added.
*    ppFirst        Address of the first data block in the list.
*/
static void _DB_AddToList(NOR_BM_DATA_BLOCK * pDataBlock, NOR_BM_DATA_BLOCK ** ppFirst) {
  NOR_BM_DATA_BLOCK * pPrevFirst;

#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL)
  {
    NOR_BM_DATA_BLOCK * pDataBlockToCheck;

    //
    // Make sure that the data block is not already contained in the list.
    //
    pDataBlockToCheck = *ppFirst;
    while (pDataBlockToCheck != NULL) {
      if (pDataBlockToCheck == pDataBlock) {
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR_BM: Data block is already contained in the list."));
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
*    Removes the specified data block from list of used data blocks.
*
*  Parameters
*    pInst        Driver instance.
*    pDataBlock   Data block to be removed.
*/
static void _DB_RemoveFromUsedList(NOR_BM_INST * pInst, const NOR_BM_DATA_BLOCK * pDataBlock) {
  _DB_RemoveFromList(pDataBlock, &pInst->pFirstDataBlockInUse);
}

/*********************************************************************
*
*       _DB_AddToUsedList
*
*  Function description
*    Adds the specified data block to the list of used data blocks.
*
*  Parameters
*    pInst        Driver instance.
*    pDataBlock   Data block to be added.
*/
static void _DB_AddToUsedList(NOR_BM_INST * pInst, NOR_BM_DATA_BLOCK * pDataBlock) {
  _DB_AddToList(pDataBlock, &pInst->pFirstDataBlockInUse);
}

/*********************************************************************
*
*       _DB_RemoveFromFreeList
*
*  Function description
*    Removes the specified data block from list of free data blocks.
*
*  Parameters
*    pInst        Driver instance.
*    pDataBlock   Data block to be removed.
*/
static void _DB_RemoveFromFreeList(NOR_BM_INST * pInst, const NOR_BM_DATA_BLOCK * pDataBlock) {
  _DB_RemoveFromList(pDataBlock, &pInst->pFirstDataBlockFree);
}

/*********************************************************************
*
*       _DB_AddToFreeList
*
*  Function description
*    Adds the specified data block to the list of free data blocks.
*
*  Parameters
*    pInst        Driver instance.
*    pDataBlock   Data block to be added.
*/
static void _DB_AddToFreeList(NOR_BM_INST * pInst, NOR_BM_DATA_BLOCK * pDataBlock) {
  _DB_AddToList(pDataBlock, &pInst->pFirstDataBlockFree);
}

#endif // FS_NOR_OPTIMIZE_DATA_WRITE

/*********************************************************************
*
*       _AllocWorkBlockDesc
*
*  Function description
*    Allocates a NOR_BM_WORK_BLOCK from the array in the pInst structure.
*/
static NOR_BM_WORK_BLOCK * _AllocWorkBlockDesc(NOR_BM_INST * pInst, unsigned lbi) {
  NOR_BM_WORK_BLOCK * pWorkBlock;

  //
  // Check if a free work block is available.
  //
  pWorkBlock = pInst->pFirstWorkBlockFree;
  if (pWorkBlock != NULL) {
    unsigned NumBytesAssignment;
    unsigned NumBytesIsWritten;
    //
    // Initialize work block descriptor, mark it as in use and add it to the list.
    //
    NumBytesAssignment = _WB_GetAssignmentSize(pInst);
    NumBytesIsWritten  = pInst->NumBytesIsWritten;
    _WB_RemoveFromFreeList(pInst, pWorkBlock);
    _WB_AddToUsedList(pInst, pWorkBlock);
    pWorkBlock->lbi    = lbi;
    FS_MEMSET(pWorkBlock->paIsWritten, 0, NumBytesIsWritten);   // Mark all entries as unused: Work block does not yet contain any sectors (data)
    FS_MEMSET(pWorkBlock->paAssign,    0, NumBytesAssignment);  // Make sure that no old assignment info from previous descriptor is in the table
  }
  return pWorkBlock;
}

#if ((FS_NOR_SKIP_BLANK_SECTORS != 0) || (FS_NOR_SUPPORT_CLEAN != 0) || ((FS_NOR_CAN_REWRITE == 0) && (FS_NOR_SUPPORT_FAIL_SAFE_ERASE != 0)))

/*********************************************************************
*
*       _IsDataBlank
*
*  Function description
*    Checks if a range of the bytes stored to NOR flash are set to 0xFF.
*
*  Return value
*   ==1   Data is blank
*   ==0   Data is not blank
*
*  Additional information
*    Off and NumBytes have to be aligned to 4 bytes.
*/
static int _IsDataBlank(NOR_BM_INST * pInst, U32 Off, U32 NumBytes) {
  U32        aBuffer[FS_NOR_DATA_BUFFER_SIZE / 4];
  unsigned   NumBytesAtOnce;
  unsigned   NumItems;
  U32      * p;
  U32        SizeOfBuffer;
  U32      * pBuffer;
  I32        NumBytesFree;
  int        r;
  int        Result;

  FS_DEBUG_ASSERT(FS_MTYPE_DRIVER, (Off & 3u) == 0u);
  FS_DEBUG_ASSERT(FS_MTYPE_DRIVER, (NumBytes & 3u) == 0u);
  //
  // If possible, use a larger buffer for the read operation to increase performance.
  //
  pBuffer      = _UseFreeMem(&NumBytesFree);
  SizeOfBuffer = sizeof(aBuffer);
  if (pBuffer != NULL) {
    if ((I32)SizeOfBuffer < NumBytesFree) {
      SizeOfBuffer = (U32)NumBytesFree;
    } else {
      pBuffer = NULL;
    }
  }
  if (pBuffer == NULL) {
    pBuffer = aBuffer;
  }
  //
  // Read data from NOR flash and check if all the bytes are set to 0xFF.
  //
  r = 1;              // Set to indicate that the phy. sector is blank.
  do {
    NumBytesAtOnce = SEGGER_MIN(NumBytes, SizeOfBuffer);
    Result = _ReadOff(pInst, pBuffer, Off, NumBytesAtOnce);
    if (Result != 0) {
      r = 0;          // Phy. sector is not blank.
      goto Done;
    }
    NumItems = NumBytesAtOnce >> 2;
    p        = pBuffer;
    do {
      if (*p++ != 0xFFFFFFFFuL) {
        r = 0;        // Phy. sector is not blank.
        goto Done;
      }
    } while (--NumItems != 0u);
    NumBytes -= NumBytesAtOnce;
    Off      += NumBytesAtOnce;
  } while (NumBytes != 0u);
Done:
  _UnuseFreeMem(NumBytesFree);
  return r;
}

#endif // ((FS_NOR_SKIP_BLANK_SECTORS != 0) || (FS_NOR_SUPPORT_CLEAN != 0) || ((FS_NOR_CAN_REWRITE == 0) && (FS_NOR_SUPPORT_FAIL_SAFE_ERASE != 0)))

#if (FS_NOR_SKIP_BLANK_SECTORS != 0) || (FS_NOR_SUPPORT_CLEAN != 0)

/*********************************************************************
*
*       _IsBlankPhySector
*
*  Function description
*    Checks if all the bytes in the physical sector are set to 0xFF.
*
*  Parameters
*    pInst            Driver instance.
*    PhySectorIndex   Index of the physical sector.
*
*  Return value
*   ==1   Phy. sector is blank.
*   ==0   Phy. sector is not blank or an error occurred.
*/
static int _IsBlankPhySector(NOR_BM_INST * pInst, unsigned PhySectorIndex) {
  U32 NumBytes;
  U32 Off;
  int r;

  //
  // We prefer the implementation of the phy. layer because it is usually faster
  // than by reading back all the data. In addition, the blank checking operation
  // of the physical layer can provide information about the correct completion
  // of the erase operation which is necessary for the fail safety.
  //
  if (pInst->pPhyType->pfIsSectorBlank != NULL) {
    r = pInst->pPhyType->pfIsSectorBlank(pInst->Unit, PhySectorIndex);
    return r;
  }
  //
  // Verify for blank by reading back the data.
  //
  Off      = 0;
  NumBytes = 0;
  _GetPhySectorInfo(pInst, PhySectorIndex, &Off, &NumBytes);
  if (NumBytes == 0u) {
    return 0;       // Error, could not get information about the phy. sector. Assume that it is not blank.
  }
  r = _IsDataBlank(pInst, Off, NumBytes);
  return r;
}

#endif // (FS_NOR_SKIP_BLANK_SECTORS != 0) || (FS_NOR_SUPPORT_CLEAN != 0)

#if (FS_NOR_CAN_REWRITE == 0) && (FS_NOR_SUPPORT_FAIL_SAFE_ERASE != 0)

/*********************************************************************
*
*       _IsPhySectorBlankLimited
*
*  Function description
*    Checks if all the bytes in the physical sector are set to 0xFF
*    with the exception of the erase count and of the erase signature.
*
*  Parameters
*    pInst            Driver instance.
*    PhySectorIndex   Index of the physical sector to be checked.
*
*  Return value
*    ==1    Phy. sector is blank.
*    ==0    Phy. sector is not blank or an error occurred.
*/
static int _IsPhySectorBlankLimited(NOR_BM_INST * pInst, unsigned PhySectorIndex) {
  U32 NumBytes;
  U32 Off;
  int r;
  U32 NumBytesToCheck;

  //
  // Verify for blank by reading back the data.
  //
  Off      = 0;
  NumBytes = 0;
  _GetPhySectorInfo(pInst, PhySectorIndex, &Off, &NumBytes);
  if (NumBytes == 0u) {
    return 0;       // Error, could not get information about the phy. sector. Assume that it is not blank.
  }
  //
  // Check the area before the erase count.
  //
  NumBytesToCheck = OFFSET_OF_MEMBER(NOR_BM_PSH, EraseCnt);
  r = _IsDataBlank(pInst, Off, NumBytesToCheck);
  if (r != 0) {
    //
    // Skip over the erase count and erase signature and then
    // check the remaining physical sector data.
    //
    NumBytesToCheck += SIZE_OF_MEMBER(NOR_BM_PSH, EraseCnt);
#if (FS_NOR_PHY_SECTOR_RESERVE >= 4)
    NumBytesToCheck += SIZE_OF_MEMBER(NOR_BM_PSH, EraseSignature);
#endif // FS_NOR_PHY_SECTOR_RESERVE >= 4
#if ((FS_NOR_SUPPORT_CRC != 0) || ((FS_NOR_SUPPORT_ECC != 0))) && (FS_NOR_PHY_SECTOR_RESERVE >= 8)
    //
    // If the support for CRC is enabled then we have to skip
    // over the bytes that store the CRC too.
    //
    if ((_IsCRCEnabled(pInst) != 0) || (_IsECCEnabled(pInst) != 0)) {
      NumBytesToCheck += SIZE_OF_MEMBER(NOR_BM_PSH, crc0);
      NumBytesToCheck += SIZE_OF_MEMBER(NOR_BM_PSH, crc1);
      NumBytesToCheck += SIZE_OF_MEMBER(NOR_BM_PSH, crc2);
      NumBytesToCheck += SIZE_OF_MEMBER(NOR_BM_PSH, crcStat);
    }
#endif // FS_NOR_SUPPORT_CRC != 0 && FS_NOR_PHY_SECTOR_RESERVE >= 8
#if (FS_NOR_SUPPORT_ECC != 0) && (FS_NOR_PHY_SECTOR_RESERVE >= 16)
    //
    // If the support for ECC is enabled then we have to skip
    // over the bytes that store the ECC.
    //
    if (_IsECCEnabled(pInst) != 0) {
      NumBytesToCheck += SIZE_OF_MEMBER(NOR_BM_PSH, abECC0);
      NumBytesToCheck += SIZE_OF_MEMBER(NOR_BM_PSH, abECC1);
      NumBytesToCheck += SIZE_OF_MEMBER(NOR_BM_PSH, ecc0Stat);
      NumBytesToCheck += SIZE_OF_MEMBER(NOR_BM_PSH, ecc1Stat);
    }
#endif // FS_NOR_SUPPORT_ECC != 0 && FS_NOR_PHY_SECTOR_RESERVE >= 16
    Off      += NumBytesToCheck;
    NumBytes -= NumBytesToCheck;
    r = _IsDataBlank(pInst, Off, NumBytes);
  }
  return r;
}

#endif // (FS_NOR_CAN_REWRITE == 0) && (FS_NOR_SUPPORT_FAIL_SAFE_ERASE != 0)

#if FS_NOR_SUPPORT_FORMAT

/*********************************************************************
*
*       _IsPhySectorEraseRequired
*/
static int _IsPhySectorEraseRequired(NOR_BM_INST * pInst, unsigned PhySectorIndex) {
#if FS_NOR_SKIP_BLANK_SECTORS
  if (pInst->SkipBlankSectors != 0u) {
    if (_IsBlankPhySector(pInst, PhySectorIndex) != 0) {
      return 0;       // The phy. sector does not have to be erased.
    }
  }
  return 1;         // The phy. sector has to be erased.
#else
  FS_USE_PARA(pInst);
  FS_USE_PARA(PhySectorIndex);
  return 1;         // The phy. sector must be erased.
#endif
}

#endif // FS_NOR_SUPPORT_FORMAT

/*********************************************************************
*
*       _IsPhySectorEmpty
*
*  Function description
*    Checks if the physical sector does not contain any data.
*
*  Return value
*    ==1    Sector is empty, that is all data bytes are set to 0xFF
*    ==0    Sector is not empty
*/
static int _IsPhySectorEmpty(NOR_BM_INST * pInst, unsigned PhySectorIndex, const NOR_BM_PSH * pPSH) {
  int r;

#if (FS_NOR_SUPPORT_CLEAN == 0)
  FS_USE_PARA(pInst);
  FS_USE_PARA(PhySectorIndex);
  FS_USE_PARA(pPSH);
  //
  // If the support for the clean operation is disabled then we always indicate
  // that the physical sector is not empty. In this way the physical sector is
  // erased before use which makes sure that we never write to physical sectors
  // that were not fully erased. Writing to physical sectors that were partially
  // erased can lead to bit errors.
  //
  r = 0;
#else
  r = 1;                // Phy. sector is empty and does not have to be erased.
#if FS_NOR_SUPPORT_FAIL_SAFE_ERASE
  if (pInst->FailSafeErase != 0u) {
    U8  DataStat;
    U32 EraseSignature;

    //
    // With the fail safe erase operation we can determine if the physical
    // sector is empty by checking the erase signature, the erase count
    // and the data status. This procedure can be applied only for the
    // NOR flash devices that can rewrite because physical sectors that
    // are partially written are detected as such and marked as invalid
    // during the low-level mount operation. For NOR flash devices that
    // cannot rewrite we have to perform a blank check instead of checking
    // the data status.
    //
    EraseSignature = _GetPhySectorEraseSignature(pPSH);
    if (EraseSignature != ERASE_SIGNATURE_VALID) {
      r = 0;            // The erase signature is missing. We have to erase phy. sector again.
      goto Done;
    }
    //
    // The erase signature is present. Check if the erase count is valid.
    //
    if (_IsEraseCntValid(pPSH) == 0) {
      r = 0;            // The phy. sector has to be erased again.
      goto Done;
    }
#if (FS_NOR_CAN_REWRITE == 0)
    if (_IsRewriteSupported(pInst) == 0) {
      //
      // For NOR flash devices that cannot rewrite we have to perform a blank
      // check of all the bytes in the physical sector with the exception of
      // the erase count and of the erase signature.
      //
      r = _IsPhySectorBlankLimited(pInst, PhySectorIndex);
    } else
#endif // FS_NOR_CAN_REWRITE == 0
    {
      //
      // The erase count seems to be valid. Look at the data status to tell if the phy. sector is empty.
      //
      DataStat = (U8)_GetPhySectorDataStat(pInst, pPSH);
      if (DataStat != DATA_STAT_EMPTY) {
        r = 0;            // Phy. sector is not empty.
      }
    }
  } else
#endif // FS_NOR_SUPPORT_FAIL_SAFE_ERASE
  {
    U32 EraseCnt;
    U8  DataStat;

    //
    // The fail safe erase operation is not enabled. The erase operation can still be
    // performed in a fail safe way if the NOR physical layer provides a blank checking
    // function that can provide accurate information about whether the physical sector
    // was successfully erased or not.
    //
    EraseCnt = pPSH->EraseCnt;
    //
    // Check if the physical sector does not have the erase count set. This can happen if
    // the erase operation was interrupted by an unexpected reset or if the NOR flash was
    // formatted with a previous version of emFile.
    //
    if (EraseCnt == ERASE_CNT_INVALID) {
      r = _IsBlankPhySector(pInst, PhySectorIndex);
      goto Done;
    }
    //
    // Look at the data status to tell if the phy. sector is empty.
    //
    DataStat = (U8)_GetPhySectorDataStat(pInst, pPSH);
    if (DataStat != DATA_STAT_EMPTY) {
      r = 0;            // Phy. sector is not empty.
    }
  }
Done:
#endif // (FS_NOR_SUPPORT_CLEAN == 0)
  return r;
}

#if FS_NOR_SUPPORT_ECC

/*********************************************************************
*
*       _WritePhySectorECC0Stat
*
*  Function description
*    Writes to storage the status of the ECC0 of a PSH.
*
*  Parameters
*    pInst              Driver instance.
*    PhySectorIndex     Index of the physical sector.
*    eccStat            Status to be set.
*
*  Return value
*    ==0      OK, ECC status successfully written.
*    !=0      An error occurred.
*/
static int _WritePhySectorECC0Stat(NOR_BM_INST * pInst, unsigned PhySectorIndex, unsigned eccStat) {
  int r;
  U32 Off;
  U8  Data8;

  Data8 = (U8)eccStat;
  _GetPhySectorInfo(pInst, PhySectorIndex, &Off, NULL);
  Off += OFFSET_OF_MEMBER(NOR_BM_PSH, ecc0Stat);
  r = _WriteOff(pInst, &Data8, Off, sizeof(Data8));
  return r;
}

/*********************************************************************
*
*       _WritePhySectorECC1Stat
*
*  Function description
*    Writes to storage the status of the ECC1 of a PSH.
*
*  Parameters
*    pInst              Driver instance.
*    PhySectorIndex     Index of the physical sector.
*    eccStat            Status to be set.
*
*  Return value
*    ==0      OK, ECC status successfully written.
*    !=0      An error occurred.
*/
static int _WritePhySectorECC1Stat(NOR_BM_INST * pInst, unsigned PhySectorIndex, unsigned eccStat) {
  int r;
  U32 Off;
  U8  Data8;

  Data8 = (U8)eccStat;
  _GetPhySectorInfo(pInst, PhySectorIndex, &Off, NULL);
  Off += OFFSET_OF_MEMBER(NOR_BM_PSH, ecc1Stat);
  r = _WriteOff(pInst, &Data8, Off, sizeof(Data8));
  return r;
}

#endif // FS_NOR_SUPPORT_ECC

/*********************************************************************
*
*       _WritePhySectorDataStatFast
*/
static int _WritePhySectorDataStatFast(NOR_BM_INST * pInst, unsigned PhySectorIndex, unsigned DataStat) {
  int r;
  U32 Off;
  U8  Data8;

  Data8 = (U8)DataStat;
  _GetPhySectorInfo(pInst, PhySectorIndex, &Off, NULL);
  Off += OFFSET_OF_MEMBER(NOR_BM_PSH, DataStat);
  r = _WriteOff(pInst, &Data8, Off, sizeof(Data8));
  return r;
}

#if (FS_NOR_CAN_REWRITE == 0)

#if (FAIL_SAFE_ERASE_NO_REWRITE != 0) && ((FS_NOR_SUPPORT_CRC != 0) || (FS_NOR_SUPPORT_ECC != 0))

/*********************************************************************
*
*       _CalcStorePSH_NRWithCRCAndECC
*
*  Function description
*    Calculates and stores to PSH the CRC and ECC of the management data.
*
*  Parameters
*    pInst            [IN] Driver instance.
*    pPSH             Physical sector header.
*
*  Return value
*    ==0      OK, CRC and ECC calculated.
*    !=0      An error occurred.
*/
static int _CalcStorePSH_NRWithCRCAndECC(const NOR_BM_INST * pInst, NOR_BM_PSH * pPSH) {
  int      r;
  unsigned DataStat;

  r = 0;
  DataStat = _GetPhySectorDataStat(pInst, pPSH);
#if (FS_NOR_SUPPORT_CRC != 0) && (FS_NOR_SUPPORT_ECC != 0)
  //
  // The CRC is calculated only when the LBI is valid to improve performance.
  // If the LBI is not valid then the CRC is not calculated the data or work block
  // indicator is checked for validity by comparing it wit 0x00 or 0xFF (see _LoadVerifyCRC_PSH())
  // The ECC is always calculated irrespective of the LBI value.
  //
  if (DataStat == DATA_STAT_WORK) {
    if (pPSH->lbiWork != LBI_INVALID) {
      if (_IsCRCEnabled(pInst) != 0) {
        r = _pCRC_API->pfCalcStorePSH_Work(pInst, pPSH);
      }
    }
    if (r == 0) {
      if (_IsECCEnabled(pInst) != 0) {
        r = pInst->pECC_API->pfCalcStorePSH_Work(pInst, pPSH);
      }
    }
  } else {
    if (DataStat == DATA_STAT_VALID) {
      if (pPSH->lbiData != LBI_INVALID) {
        if (_IsCRCEnabled(pInst) != 0) {
          r = _pCRC_API->pfCalcStorePSH_Data(pInst, pPSH);
        }
      }
      if (r == 0) {
        if (_IsECCEnabled(pInst) != 0) {
          r = pInst->pECC_API->pfCalcStorePSH_Data(pInst, pPSH);
        }
      }
    } else {
      if (DataStat == DATA_STAT_INVALID) {
        //
        // We calculate only the ECC here because no CRC is used to
        // protect the invalid indicator.
        //
        if (_IsECCEnabled(pInst) != 0) {
          r = pInst->pECC_API->pfCalcStorePSH_Invalid(pInst, pPSH);
        }
      }
    }
  }
#elif (FS_NOR_SUPPORT_CRC != 0) && (FS_NOR_SUPPORT_ECC == 0)
  if (pInst->FailSafeErase != 0u) {
    if (_IsCRCEnabled(pInst) != 0) {
      //
      // If required, calculate the CRC of the information stored
      // together with the work or data block indicator. The CRC value
      // has to be calculated only if the LBI contains a valid value.
      //
      if (DataStat == DATA_STAT_WORK) {
        if (pPSH->lbiWork != LBI_INVALID) {
          r = _pCRC_API->pfCalcStorePSH_Work(pInst, pPSH);
        }
      } else {
        if (DataStat == DATA_STAT_VALID) {
          if (pPSH->lbiData != LBI_INVALID) {
            r = _pCRC_API->pfCalcStorePSH_Data(pInst, pPSH);
          }
        }
      }
    }
  }
#elif (FS_NOR_SUPPORT_CRC == 0) && (FS_NOR_SUPPORT_ECC != 0)
  if (_IsECCEnabled(pInst) != 0) {
    if (DataStat == DATA_STAT_WORK) {
      r = pInst->pECC_API->pfCalcStorePSH_Work(pInst, pPSH);
    } else {
      if (DataStat == DATA_STAT_VALID) {
        r = pInst->pECC_API->pfCalcStorePSH_Data(pInst, pPSH);
      } else {
        if (DataStat == DATA_STAT_INVALID) {
          r = pInst->pECC_API->pfCalcStorePSH_Invalid(pInst, pPSH);
        }
      }
    }
  }
#endif // FS_NOR_SUPPORT_CRC != 0 && FS_NOR_SUPPORT_ECC != 0
  return r;
}

#endif // (FAIL_SAFE_ERASE_NO_REWRITE != 0) && ((FS_NOR_SUPPORT_CRC != 0) || (FS_NOR_SUPPORT_ECC != 0))

#if (FAIL_SAFE_ERASE_NO_REWRITE != 0) && (FS_NOR_SUPPORT_ECC != 0)

/*********************************************************************
*
*       _CalcStorePSH_NRWithECC
*
*  Function description
*    Calculates and stores to PSH the ECC of the management data.
*
*  Parameters
*    pInst            [IN] Driver instance.
*    pPSH             Physical sector header.
*
*  Return value
*    ==0      OK, ECC successfully calculated.
*    !=0      An error occurred.
*
*  Additional information
*    This function assumes that the checking via ECC is enabled.
*/
static int _CalcStorePSH_NRWithECC(const NOR_BM_INST * pInst, NOR_BM_PSH * pPSH) {
  int      r;
  unsigned DataStat;

  r = 0;
  if (_IsECCEnabled(pInst) != 0) {
    DataStat = _GetPhySectorDataStat(pInst, pPSH);
    if (DataStat == DATA_STAT_WORK) {
      r = pInst->pECC_API->pfCalcStorePSH_Work(pInst, pPSH);
    } else {
      if (DataStat == DATA_STAT_VALID) {
        r = pInst->pECC_API->pfCalcStorePSH_Data(pInst, pPSH);
      } else {
        if (DataStat == DATA_STAT_INVALID) {
          r = pInst->pECC_API->pfCalcStorePSH_Invalid(pInst, pPSH);
        }
      }
    }
  }
  return r;
}

#endif // (FAIL_SAFE_ERASE_NO_REWRITE != 0) && (FS_NOR_SUPPORT_ECC != 0)

/*********************************************************************
*
*       _WritePhySectorDataStat
*
*  Function description
*    Sets the data status of a physical sector and updates the PSH to storage.
*
*  Parameters
*    pInst            Driver instance.
*    PhySectorIndex   Index of the physical sector.
*    DataStat         Status of the data stored in the physical sector.
*    pPSH             Physical sector header.
*
*  Return value
*    ==0      OK, PSH stored successfully.
*    !=0      An error occurred.
*
*  Additional information
*    This function assumes that the support for incremental write operations
*    is disabled at runtime.
*/
static int _WritePhySectorDataStat(NOR_BM_INST * pInst, unsigned PhySectorIndex, unsigned DataStat, NOR_BM_PSH * pPSH) {
  int r;

  _SetPhySectorDataStat(pInst, pPSH, DataStat);
#if (FAIL_SAFE_ERASE_NO_REWRITE != 0) && ((FS_NOR_SUPPORT_CRC != 0) || (FS_NOR_SUPPORT_ECC != 0))
  r = _CalcStorePSH_NRWithCRCAndECC(pInst, pPSH);
  if (r == 0) {
    r = _WritePSH(pInst, PhySectorIndex, pPSH);
  }
#else
  r = _WritePSH(pInst, PhySectorIndex, pPSH);
#endif // (FAIL_SAFE_ERASE_NO_REWRITE != 0) && ((FS_NOR_SUPPORT_CRC != 0) || (FS_NOR_SUPPORT_ECC != 0))
  return r;
}

#endif // FS_NOR_CAN_REWRITE == 0

#if FS_NOR_SUPPORT_ECC

/*********************************************************************
*
*       _SetPSH_ECCToEmpty
*
*  Function description
*    Marks the ECC of PSH as empty.
*
*  Parameters
*    pPSH     Header of the physical sector.
*
*  Additional information
*    This function assumes that the ECC status does not contain any bit errors.
*/
static void _SetPSH_ECCToEmpty(NOR_BM_PSH * pPSH) {
  unsigned eccStat;

  eccStat = pPSH->ecc0Stat;
  if (eccStat == ECC_STAT_VALID) {
    pPSH->ecc0Stat = ECC_STAT_EMPTY;
  } else {
    eccStat = pPSH->ecc1Stat;
    if (eccStat == ECC_STAT_VALID) {
      pPSH->ecc1Stat = ECC_STAT_EMPTY;
    }
  }
}

/*********************************************************************
*
*       _SetPSH_ECCToValid
*
*  Function description
*    Marks the ECC of PSH as valid.
*
*  Parameters
*    pPSH     Header of the physical sector.
*
*  Additional information
*    This function assumes that the ECC status does not contain any bit errors.
*/
static void _SetPSH_ECCToValid(NOR_BM_PSH * pPSH) {
  unsigned eccStat;

  eccStat = pPSH->ecc0Stat;
  if (eccStat == ECC_STAT_EMPTY) {
    pPSH->ecc0Stat = ECC_STAT_VALID;
  } else {
    eccStat = pPSH->ecc1Stat;
    if (eccStat == ECC_STAT_EMPTY) {
      pPSH->ecc1Stat = ECC_STAT_VALID;
    }
  }
}

#endif // FS_NOR_SUPPORT_ECC

#if FS_NOR_SUPPORT_ECC

/*********************************************************************
*
*       _MarkPhySectorECCAsValid
*
*  Function description
*    Marks the ECC that protects the PSH as valid.
*
*  Parameters
*    pInst            Driver instance.
*    PhySectorIndex   Index of the physical sector.
*    pPSH             Physical sector header.
*
*  Return value
*    ==0    OK, data written successfully.
*    !=0    An error occurred.
*/
static int _MarkPhySectorECCAsValid(NOR_BM_INST * pInst, unsigned PhySectorIndex, NOR_BM_PSH * pPSH) {
  int      r;
  unsigned eccStat;

  _SetPSH_ECCToValid(pPSH);
  eccStat = pPSH->ecc0Stat;
  if (eccStat == ECC_STAT_VALID) {
    r = _WritePhySectorECC0Stat(pInst, PhySectorIndex, eccStat);
  } else {
    eccStat = pPSH->ecc1Stat;
    if (eccStat == ECC_STAT_VALID) {
      r = _WritePhySectorECC1Stat(pInst, PhySectorIndex, eccStat);
    } else {
      r = 1;            // Error, no ECC status is marked as valid.
    }
  }
  return r;
}

/*********************************************************************
*
*       _MarkPhySectorAsValidWithECC
*
*  Function description
*    Updates the PSH to indicate that the data is valid.
*
*  Parameters
*    pInst            Driver instance.
*    PhySectorIndex   Index of the physical sector.
*    DataStat         Status of the data stored in the physical sector.
*    pPSH             Physical sector header.
*
*  Return value
*    ==0    OK, data written successfully.
*    !=0    An error occurred.
*/
static int _MarkPhySectorAsValidWithECC(NOR_BM_INST * pInst, unsigned PhySectorIndex, unsigned DataStat, NOR_BM_PSH * pPSH) {
  int r;

  if (_IsECCEnabled(pInst) != 0) {
    //
    // If the check via ECC is enabled we mark the ECC as valid on the storage
    // to indicate that the data is valid.
    //
    r = _MarkPhySectorECCAsValid(pInst, PhySectorIndex, pPSH);
  } else {
    //
    // The sector data status has to be written last in order to make sure that
    // after an unexpected reset the other information in the header is valid.
    //
    r = _WritePhySectorDataStatFast(pInst, PhySectorIndex, DataStat);
  }
  return r;
}

#endif // FS_NOR_SUPPORT_ECC

/*********************************************************************
*
*       _MarkPhySectorAsValid
*
*  Function description
*    Updates the PSH to indicate that the data is valid.
*
*  Parameters
*    pInst            Driver instance.
*    PhySectorIndex   Index of the physical sector.
*    DataStat         Status of the data stored in the physical sector.
*    pPSH             Physical sector header.
*
*  Return value
*    ==0    OK, data written successfully.
*    !=0    An error occurred.
*
*  Additional information
*    Depending on the driver configuration the information is updated
*    in 3 different ways as follows:
*    1) If the checking via ECC and the support for incremental write operations
*       is enabled then only the ECC status is updated (NOR_BM_PSH::eccStat)
*    2) If the checking via ECC is disabled and the support for incremental
*       write operations is enabled then only the data status is updated (NOR_BM_PSH::DataStat)
*    3) If the support for incremental write operations is disabled then
*       the entire physical sector header is updated.
*/
static int _MarkPhySectorAsValid(NOR_BM_INST * pInst, unsigned PhySectorIndex, unsigned DataStat, NOR_BM_PSH * pPSH) {
  int r;

#if FS_NOR_CAN_REWRITE
#if FS_NOR_SUPPORT_ECC
  r = _MarkPhySectorAsValidWithECC(pInst, PhySectorIndex, DataStat, pPSH);
#else
  FS_USE_PARA(pPSH);
  r = _WritePhySectorDataStatFast(pInst, PhySectorIndex, DataStat);
#endif // FS_NOR_SUPPORT_ECC
#else
  if (_IsRewriteSupported(pInst) != 0) {
#if FS_NOR_SUPPORT_ECC
    r = _MarkPhySectorAsValidWithECC(pInst, PhySectorIndex, DataStat, pPSH);
#else
    FS_USE_PARA(pPSH);
    r = _WritePhySectorDataStatFast(pInst, PhySectorIndex, DataStat);
#endif // FS_NOR_SUPPORT_ECC
  } else {
    r = _WritePhySectorDataStat(pInst, PhySectorIndex, DataStat, pPSH);
  }
#endif // FS_NOR_CAN_REWRITE
  return r;
}

/*********************************************************************
*
*       _IsDataCntUpdateAllowed
*
*  Function description
*    Verifies if the value of data count member in the header of a
*    physical sector can be updated.
*
*  Parameters
*    pInst            [IN] Driver instance.
*    DataStatNew      New data status of the physical sector.
*    pPSH             [IN] Header of the physical sector.
*
*  Return value
*    ==1    DataCnt can be updated.
*    ==0    DataCnt cannot be updated.
*/
static int _IsDataCntUpdateAllowed(const NOR_BM_INST * pInst, unsigned DataStatNew, const NOR_BM_PSH * pPSH) {
  int r;

  r = 1;          // Set to indicate that DataCnt can be updated.
#if (FS_NOR_CAN_REWRITE == 0)
  {
    unsigned DataStatOld;

    if (_IsRewriteSupported(pInst) == 0) {
      //
      // If the work block is converted in-place then we are not allowed
      // to store the data count to prevent that we write twice to the same flash line.
      // The data count is stored when the work block is created based on the
      // data count of the corresponding data block if such a block exists.
      //
      DataStatOld = _GetPhySectorDataStat(pInst, pPSH);
      if ((DataStatOld == DATA_STAT_WORK) && (DataStatNew == DATA_STAT_VALID)) {
        r = 0;
      }
    }
  }
#else
  FS_USE_PARA(pInst);
  FS_USE_PARA(DataStatNew);
  FS_USE_PARA(pPSH);
#endif
  return r;
}

#if FAIL_SAFE_ERASE_NO_REWRITE

/*********************************************************************
*
*       _IsPhySectorErasedViaClean
*
*  Function description
*    Checks if a physical sector was erased via a clean operation.
*
*  Parameters
*    pInst            [IN] Driver instance.
*    pPSH             [IN] Physical sector header.
*
*  Return value
*    !=0    Erased via clean operation
*    ==0    Erased while allocated.
*/
static int _IsPhySectorErasedViaClean(NOR_BM_INST * pInst, const NOR_BM_PSH * pPSH) {
  int r;
  U32 EraseCnt;

  r = 0;              // Set to indicate the physical sector was not erased via a clean operation.
  if (pInst->FailSafeErase != 0u) {
    if (_IsRewriteSupported(pInst) == 0) {
      EraseCnt = pPSH->EraseCnt;
      if (   (EraseCnt != ERASE_CNT_INVALID)
          && (EraseCnt <= (U32)FS_NOR_MAX_ERASE_CNT)) {   // Valid erase count?
        if (pPSH->lbi == LBI_INVALID) {                   // Invalid LBI?
          r = 1;      // OK, erased via a clean operation.
        }
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _SetPhySectorLBIAndDataCnt
*
*  Function description
*    Stores the LBI and the data count to the physical sector header.
*
*  Parameters
*    pInst            [IN] Driver instance.
*    pPSH             Physical sector header.
*    lbi              Index of logical block assigned to physical sector.
*    DataStat         Status of the data in the physical sector.
*    DataCnt          Number of times the data of the physical sector has been copied.
*/
static void _SetPhySectorLBIAndDataCnt(NOR_BM_INST * pInst, NOR_BM_PSH * pPSH, unsigned lbi, unsigned DataStat, unsigned DataCnt) {
  if (DataStat == DATA_STAT_WORK) {
    pPSH->DataCntWork = DataCnt;
    pPSH->lbiWork     = lbi;
    UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_PSH, DataCntWork), sizeof(pPSH->DataCntWork));
    UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_PSH, lbiWork),     sizeof(pPSH->lbiWork));
    if (_IsCRCEnabled(pInst) == 0) {
      //
      // If the checking via CRC is disabled we set the crcData member to 0
      // so that we can check during the low-level mount operation if the
      // write operation completed successfully.
      //
      pPSH->crcWork = 0;
      UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_PSH, crcWork), sizeof(pPSH->crcWork));
    }
#if FS_NOR_SUPPORT_ECC
    if (_IsECCEnabled(pInst) == 0) {
      //
      // If the checking via ECC is disabled we set the eccStatWork member to 0
      // so that we can check during the low-level mount operation if the write
      // operation completed successfully.
      //
      pPSH->eccStatWork = 0;
      UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_PSH, eccStatWork), sizeof(pPSH->eccStatWork));
    }
#endif // FS_NOR_SUPPORT_ECC
  } else {
    if (DataStat == DATA_STAT_VALID) {
      pPSH->DataCntData = DataCnt;
      pPSH->lbiData     = lbi;
      UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_PSH, DataCntData), sizeof(pPSH->DataCntData));
      UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_PSH, lbiData),     sizeof(pPSH->lbiData));
      if (_IsCRCEnabled(pInst) == 0) {
        //
        // If the checking via CRC is disabled we set the crcData member to 0
        // so that we can check during the low-level mount operation if the write
        // operation completed successfully.
        //
        pPSH->crcData = 0;
        UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_PSH, crcData), sizeof(pPSH->crcData));
      }
#if FS_NOR_SUPPORT_ECC
      if (_IsECCEnabled(pInst) == 0) {
        //
        // If the checking via ECC is disabled we set the eccStatData member to 0
        // so that we can check during the low-level mount operation if the write
        // operation completed successfully.
        //
        pPSH->eccStatData = 0;
        UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_PSH, eccStatData), sizeof(pPSH->eccStatData));
      }
#endif // FS_NOR_SUPPORT_ECC
    }
  }
}

#endif // FAIL_SAFE_ERASE_NO_REWRITE

#if (FS_NOR_SUPPORT_CRC != 0) || (FS_NOR_SUPPORT_ECC != 0)

/*********************************************************************
*
*       _CalcStorePSHWithCRCAndECC
*
*  Function description
*    Calculates and stores to PSH the CRC and ECC of the management data.
*
*  Parameters
*    pInst            [IN] Driver instance.
*    pPSH             Physical sector header.
*
*  Return value
*    ==0      OK, CRC and ECC calculated.
*    !=0      An error occurred.
*/
static int _CalcStorePSHWithCRCAndECC(NOR_BM_INST * pInst, NOR_BM_PSH * pPSH) {
  int r;

  r = 0;
#if (FS_NOR_SUPPORT_CRC != 0) && (FS_NOR_SUPPORT_ECC != 0)
  if (_IsECCEnabled(pInst) != 0) {
    int NumBitsCorrected;

    NumBitsCorrected = 0;
    r = pInst->pECC_API->pfCalcStorePSH(pInst, pPSH, &NumBitsCorrected);
    if (r == 0) {
      UPDATE_NUM_BIT_ERRORS(pInst, NumBitsCorrected);
    }
  }
  if (r == 0) {
    if (_IsCRCEnabled(pInst) != 0) {
      r = _pCRC_API->pfCalcStorePSH(pInst, pPSH);
    }
  }
#elif (FS_NOR_SUPPORT_CRC != 0) && (FS_NOR_SUPPORT_ECC == 0)
  if (_IsCRCEnabled(pInst) != 0) {
    r = _pCRC_API->pfCalcStorePSH(pInst, pPSH);
  }
#elif (FS_NOR_SUPPORT_CRC == 0) && (FS_NOR_SUPPORT_ECC != 0)
  if (_IsECCEnabled(pInst) != 0) {
    int NumBitsCorrected;

    NumBitsCorrected = 0;
    r = pInst->pECC_API->pfCalcStorePSH(pInst, pPSH, &NumBitsCorrected);
    if (r == 0) {
      UPDATE_NUM_BIT_ERRORS(pInst, NumBitsCorrected);
    }
  }
#endif // FS_NOR_SUPPORT_CRC != 0 && FS_NOR_SUPPORT_ECC != 0
  return r;
}

#endif // FS_NOR_SUPPORT_CRC != 0 || FS_NOR_SUPPORT_ECC != 0

/*********************************************************************
*
*       _WritePhySectorData
*
*  Function description
*    Updates the LBI, EraseCnt and the data count to the physical sector header.
*
*  Parameters
*    pInst            [IN] Driver instance.
*    PhySectorIndex   Index of the physical sector to be updated.
*    pPSH             Physical sector header.
*    lbi              Index of logical block assigned to physical sector.
*    EraseCnt         Number of times the physical sector has been erased.
*    DataStat         Status of the data in the physical sector.
*    DataCnt          Number of times the data of the physical sector has been copied.
*    SkipCheck        Specifies if the CRC and ECC verification of the PSH has to be omitted.
*/
static int _WritePhySectorData(NOR_BM_INST * pInst, unsigned PhySectorIndex, NOR_BM_PSH * pPSH, unsigned lbi, U32 EraseCnt, unsigned DataStat, unsigned DataCnt, int SkipCheck) {
  int IsUpdateRequired;
  int r;

  r                = 0;
  IsUpdateRequired = 0;
  if (DataCnt != DATA_CNT_INVALID) {
    if (pPSH->DataCnt != DataCnt) {
      if (_IsDataCntUpdateAllowed(pInst, DataStat, pPSH) != 0) {
        pPSH->DataCnt    = (U8)DataCnt;
        IsUpdateRequired = 1;
        UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_PSH, DataCnt), sizeof(pPSH->DataCnt));
      }
    }
  } else {
    //
    // In case of a data count overflow we have to update the
    // CRC if data was previously stored to the physical sector header
    // without CRC causing a CRC verification error during the read operation.
    //
    if (SkipCheck != 0) {
      IsUpdateRequired = 1;         // Indicate that the CRC has to be updated.
    }
  }
  if (lbi != LBI_INVALID) {
    if (pPSH->lbi != lbi) {
      pPSH->lbi        = (U16)lbi;
      IsUpdateRequired = 1;
      UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_PSH, lbi), sizeof(pPSH->lbi));
    }
  }
  if (EraseCnt != ERASE_CNT_INVALID) {
    if (pPSH->EraseCnt != EraseCnt) {
      pPSH->EraseCnt   = EraseCnt;
      IsUpdateRequired = 1;
      UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_PSH, EraseCnt), sizeof(pPSH->EraseCnt));
    }
  }
  if (IsUpdateRequired != 0) {
#if (FS_NOR_SUPPORT_CRC != 0) || (FS_NOR_SUPPORT_ECC != 0)
    r = 0;        // Set to indicate success.
    //
    // We have to update the CRC and ECC here for NOR flash devices that cannot
    // rewrite because for this type of devices the CRC and ECC is not calculated
    // when the type of the data block is set via _WritePhySectorDataStat().
    // The reason for this is that the data status is stored to a different
    // flash line in order to prevent that the same flash line is modified
    // more than once between two block erase operations.
    //
    // In addition, we calculate the CRC and ECC here for NOR flash devices
    // that can rewrite but if the check via ECC is disabled we do not store the actual data status.
    // This is done later so that the critical information is stored as a single byte.
    // Note that _UpdatePSH() is sometimes called with DataStat set to DATA_STAT_EMPTY to set only the LBI.
    // In this case we purposely do not calculate neither the CRC nor the ECC so that the physical
    // sector gets discarded because of a CRC or ECC error when an unexpected reset interrupts
    // the block creation operation.
    //
    // If the check via ECC is enabled we use the ECC status instead of data status to indicate
    // if the data is valid or not. That is the data status is written together with the ECC
    // but the ECC is marked as invalid. The ECC is marked as valid in _MarkPhySectorAsValid().
    //
    if (DataStat != DATA_STAT_EMPTY) {
      if (_IsRewriteSupported(pInst) == 0) {
        r = _CalcStorePSHWithCRCAndECC(pInst, pPSH);
      } else {
#if FS_NOR_SUPPORT_ECC
        if (_IsECCEnabled(pInst) != 0) {
          _SetPhySectorDataStat(pInst, pPSH, DataStat);
          r = _CalcStorePSHWithCRCAndECC(pInst, pPSH);
          _SetPSH_ECCToEmpty(pPSH);
        } else
#endif // FS_NOR_SUPPORT_ECC
        {
          unsigned DataStatSaved;

          DataStatSaved = _GetPhySectorDataStat(pInst, pPSH);
          _SetPhySectorDataStat(pInst, pPSH, DataStat);
          r = _CalcStorePSHWithCRCAndECC(pInst, pPSH);
          _SetPhySectorDataStat(pInst, pPSH, DataStatSaved);
        }
      }
    }
    if (r == 0) {
      r = _WritePSH(pInst, PhySectorIndex, pPSH);
    }
#else
    r = _WritePSH(pInst, PhySectorIndex, pPSH);
#endif // FS_NOR_SUPPORT_CRC != 0 || FS_NOR_SUPPORT_ECC != 0
  }
  return r;
}

/*********************************************************************
*
*       _UpdatePSH
*
*  Function description
*    Modifies information in the header of a phy. sector.
*
*  Parameters
*    pInst            [IN] Driver instance.
*    PhySectorIndex   Index of physical sector to write.
*    lbi              Index of logical block assigned to physical sector.
*    EraseCnt         Number of times the physical sector has been erased.
*    DataStat         Status of the data in the physical sector.
*    DataCnt          Number of times the data of the physical sector has been copied.
*    SkipCheck        Specifies if the CRC and ECC verification of the PSH has to be omitted.
*
*  Return value
*    ==0    OK, phy. sector updated.
*    !=0    An error occurred.
*/
static int _UpdatePSH(NOR_BM_INST * pInst, unsigned PhySectorIndex, unsigned lbi, U32 EraseCnt, unsigned DataStat, unsigned DataCnt, int SkipCheck) {
  NOR_BM_PSH psh;
  int        r;

  INIT_PSH_DATA_RANGE();
  FS_MEMSET(&psh, 0xFF, sizeof(psh));
  r = _ReadPSHEx(pInst, PhySectorIndex, &psh, SkipCheck, 0);
  if (r == 0) {
#if FAIL_SAFE_ERASE_NO_REWRITE
    //
    // We handle here physical sectors that were erased via a clean operation
    // for NOR flash devices that do not support incremental write operations.
    // For these types of physical sectors the data count and the LBI
    // have to be stored on the same flash line as the work or data block
    // indicator.
    //
    if (_IsPhySectorErasedViaClean(pInst, &psh) != 0) {
      //
      // The physical sector was erased via a clean operation.
      // Store the LBI and the data count to PSH. The information
      // is updated to storage when _WritePhySectorDataStat()
      // is called at the end of this function.
      //
      _SetPhySectorLBIAndDataCnt(pInst, &psh, lbi, DataStat, DataCnt);
    } else {
      //
      // Update the LBI, erase count and data count to storage.
      //
      r = _WritePhySectorData(pInst, PhySectorIndex, &psh, lbi, EraseCnt, DataStat, DataCnt, SkipCheck);
    }
#else
    //
    // Update the LBI, erase count and data count to storage.
    //
    r = _WritePhySectorData(pInst, PhySectorIndex, &psh, lbi, EraseCnt, DataStat, DataCnt, SkipCheck);
#endif // FAIL_SAFE_ERASE_NO_REWRITE
    if (r == 0) {
      if (DataStat != DATA_STAT_EMPTY) {
        r = _MarkPhySectorAsValid(pInst, PhySectorIndex, DataStat, &psh);
      }
    }
  }
  INIT_PSH_DATA_RANGE();
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR_BM: UPDATE_PSH PSI: %u, r: %d\n", PhySectorIndex, r));
  return r;
}

/*********************************************************************
*
*       _MarkAsWorkBlock
*
*  Function description
*    Marks a physical sector as work block.
*
*  Parameters
*    pInst            [IN] Driver instance.
*    PhySectorIndex   Index of physical sector to write.
*    lbi              Index of logical block assigned to physical sector.
*    EraseCnt         Number of times the physical sector has been erased.
*    DataCnt          Data count plus 1 of the corresponding data block,
*                     if such data block exists.
*
*  Return value
*    ==0    OK, block is marked as work block.
*    !=0    A write error occurred (recoverable)
*/
static int _MarkAsWorkBlock(NOR_BM_INST * pInst, unsigned PhySectorIndex, unsigned lbi, U32 EraseCnt, U8 DataCnt) {
  int r;

  r = _UpdatePSH(pInst, PhySectorIndex, lbi, EraseCnt, DATA_STAT_WORK, DataCnt, 0);
  return r;
}

/*********************************************************************
*
*       _MarkAsDataBlock
*
*  Function description
*    Marks a physical sector as data block.
*
*  Parameters
*    pInst            [IN] Driver instance.
*    PhySectorIndex   Index of physical sector to write.
*    lbi              Index of logical block assigned to physical sector.
*    EraseCnt         Number of times the physical sector has been erased.
*    DataCnt          Number of times the data block was copied. Used to
*                     determine which of two data blocks with the same LBI
*                     is the most recent one (see _IsPhySectorDataMoreRecent).
*    SkipCheck        Specifies if the CRC and ECC verification of the PSH has to be omitted.
*
*  Return value
*    ==0    OK, block is marked as data block.
*    !=0    A write error occurred (recoverable)
*
*  Additional information
*    ReadError is used when updating the phy. sector header for the second
*    time with CRC enabled.
*/
static int _MarkAsDataBlock(NOR_BM_INST * pInst, unsigned PhySectorIndex, unsigned lbi, U32 EraseCnt, unsigned DataCnt, int SkipCheck) {
  int r;

  r = _UpdatePSH(pInst, PhySectorIndex, lbi, EraseCnt, DATA_STAT_VALID, DataCnt, SkipCheck);
  return r;
}

#if FS_NOR_SUPPORT_ECC

/*********************************************************************
*
*        _MarkLogSectorECCAsValid
*
*  Function description
*    Marks the ECC that protects the LSH as valid.
*
*  Parameters
*    pInst            Driver instance.
*    PhySectorIndex   Index of the physical sector that stores the logical sector.
*    srsi             Index of the logical sector in the physical sector.
*    pLSH             Header of the logical sector.
*
*  Return value
*    ==0      OK, data stored successfully.
*    !=0      An error occurred.
*/
static int _MarkLogSectorECCAsValid(NOR_BM_INST * pInst, unsigned PhySectorIndex, unsigned srsi, NOR_BM_LSH * pLSH) {
  int      r;
  unsigned eccStat;

  r = 0;                // Set to indicate success.
  _SetLSH_ECCToValid(pLSH);
  eccStat = pLSH->ecc0Stat;
  if (eccStat == ECC_STAT_VALID) {
    r = _WriteLogSectorECC0Stat(pInst, PhySectorIndex, srsi, pLSH->ecc0Stat);
  } else {
    eccStat = pLSH->ecc1Stat;
    if (eccStat == ECC_STAT_VALID) {
      r = _WriteLogSectorECC1Stat(pInst, PhySectorIndex, srsi, pLSH->ecc1Stat);
    }
  }
  return r;
}

#endif // FS_NOR_SUPPORT_ECC

/*********************************************************************
*
*        _MarkLogSectorAsValidWithCRCAndECC
*
*  Function description
*    Updates LSH to indicate that the sector data is valid.
*
*  Parameters
*    pInst            Driver instance.
*    PhySectorIndex   Index of the physical sector that stores the logical sector.
*    srsi             Index of the logical sector in the physical sector.
*    brsi             Index of the logical sector in the data block.
*    pDataCheck       CRC and ECC of the sector data.
*
*  Return value
*    ==0      OK, data stored successfully.
*    !=0      An error occurred.
*
*  Additional information
*    This function is called during a sector write operation.
*    It assumes that the support for incremental write operations
*    is enabled.
*/
static int _MarkLogSectorAsValidWithCRCAndECC(NOR_BM_INST * pInst, unsigned PhySectorIndex, unsigned srsi, unsigned brsi, const DATA_CHECK * pDataCheck) {
  int        r;
  NOR_BM_LSH lsh;

  INIT_LSH_DATA_RANGE();
  FS_MEMSET(&lsh, 0xFF, sizeof(lsh));
  if (brsi != BRSI_INVALID) {
    _SetLogSectorDataStat(pInst, &lsh, DATA_STAT_VALID);
    lsh.brsi = (U16)brsi;
    UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_LSH, brsi), sizeof(lsh.brsi));
#if (FS_NOR_SUPPORT_CRC != 0) || (FS_NOR_SUPPORT_ECC != 0)
    r = _CalcStoreLSHWithCRCAndECC(pInst, &lsh, pDataCheck);
    if (r == 0) {
#if FS_NOR_SUPPORT_ECC
      if (_IsECCEnabled(pInst) != 0) {
        //
        // With the ECC enabled we use the ECC status to indicate that the data of the LSH is valid.
        // The ECC status is set here to invalid and is set to valid in the operation that marks the
        // logical sector as valid.
        //
        _SetLSH_ECCToEmpty(&lsh);
        UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_LSH, DataStat), sizeof(lsh.DataStat));
        r = _WriteLSH(pInst, PhySectorIndex, srsi, &lsh);
        if (r == 0) {
          //
          // Now make the ECC status valid and write it to storage.
          //
          r = _MarkLogSectorECCAsValid(pInst, PhySectorIndex, srsi, &lsh);
        }
      } else
#endif // FS_NOR_SUPPORT_ECC
      {
        //
        // Update the data in the logical sector header with the data status information
        // set to default value (i.e. log. sector empty) to indicate that the information
        // is not complete. The data status is updated in the next step via a separate
        // write operation in order to make sure that the operation is fail-safe.
        //
        _SetLogSectorDataStat(pInst, &lsh, DATA_STAT_EMPTY);
        r = _WriteLSH(pInst, PhySectorIndex, srsi, &lsh);
        if (r == 0) {
          r = _WriteLogSectorDataStatFast(pInst, PhySectorIndex, srsi, DATA_STAT_VALID);
        }
      }
    }
#else
    FS_USE_PARA(pDataCheck);
    //
    // Update the data in the logical sector header with the data status information
    // set to default value (i.e. log. sector empty) to indicate that the information
    // is not complete. The data status is updated in the next step via a separate write
    // operation in order to make sure that the operation is fail-safe.
    //
    _SetLogSectorDataStat(pInst, &lsh, DATA_STAT_EMPTY);
    r = _WriteLSH(pInst, PhySectorIndex, srsi, &lsh);
    if (r == 0) {
      r = _WriteLogSectorDataStatFast(pInst, PhySectorIndex, srsi, DATA_STAT_VALID);
    }
#endif // (FS_NOR_SUPPORT_CRC != 0) || (FS_NOR_SUPPORT_ECC != 0)
  } else {
    //
    // The BRSI is not valid. Typically, this is the case when the function
    // is called only to mark the logical sector as valid.
    //
#if (FS_NOR_SUPPORT_CRC != 0) || (FS_NOR_SUPPORT_ECC != 0)
#if FS_NOR_SUPPORT_ECC
    if (_IsECCEnabled(pInst) != 0) {
      //
      // With the ECC enabled we use the ECC status to indicate that the data of the LSH is valid.
      //
      r = _MarkLogSectorECCAsValid(pInst, PhySectorIndex, srsi, &lsh);
    } else
#endif // FS_NOR_SUPPORT_ECC
    {
      //
      // We assume that all the other information about the logical sector were
      // updated in a previous step therefore we update here only the data status
      // in the logical sector header.
      //
      r = _WriteLogSectorDataStatFast(pInst, PhySectorIndex, srsi, DATA_STAT_VALID);
    }
#else
    FS_USE_PARA(pDataCheck);
    //
    // We assume that all the other information about the logical sector were
    // updated in a previous step therefore we update here only the data status
    // in the logical sector header.
    //
    r = _WriteLogSectorDataStatFast(pInst, PhySectorIndex, srsi, DATA_STAT_VALID);
#endif // (FS_NOR_SUPPORT_CRC != 0) || (FS_NOR_SUPPORT_ECC != 0)
  }
  INIT_LSH_DATA_RANGE();
  return r;
}

#if (FS_NOR_CAN_REWRITE == 0)

/*********************************************************************
*
*        _MarkLogSectorAsValidNRWithCRCAndECC
*
*  Function description
*    Marks a logical sector as valid.
*
*  Parameters
*    pInst            Driver instance.
*    PhySectorIndex   Index of the physical sector that stores the logical sector.
*    srsi             Index of the logical sector in the physical sector.
*    brsi             Index of the logical sector in the data block.
*    pDataCheck       CRC and ECC of the sector data.
*
*  Return value
*    ==0      OK, data stored successfully.
*    !=0      An error occurred.
*
*  Additional information
*    This function assumes that the support for incremental write
*    operations is disabled (i.e. _IsRewriteSupported(pInst) == 0 is true)
*/
static int _MarkLogSectorAsValidNRWithCRCAndECC(NOR_BM_INST * pInst, unsigned PhySectorIndex, unsigned srsi, unsigned brsi, const DATA_CHECK * pDataCheck) {
  int        r;
  NOR_BM_LSH lsh;

  FS_MEMSET(&lsh, 0xFF, sizeof(lsh));
  r = _ReadLSH(pInst, PhySectorIndex, srsi, &lsh);
  if (r == 0) {
    if (brsi != BRSI_INVALID) {
      INIT_LSH_DATA_RANGE();
      lsh.brsi = (U16)brsi;
      UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_LSH, brsi), sizeof(lsh.brsi));
#if (FS_NOR_SUPPORT_CRC != 0) || (FS_NOR_SUPPORT_ECC != 0)
      r = _CalcStoreLSHWithCRCAndECC(pInst, &lsh, pDataCheck);
      if (r == 0) {
        r = _WriteLSH(pInst, PhySectorIndex, srsi, &lsh);
      }
#else
      FS_USE_PARA(pDataCheck);
      r = _WriteLSH(pInst, PhySectorIndex, srsi, &lsh);
#endif // (FS_NOR_SUPPORT_CRC != 0) || (FS_NOR_SUPPORT_ECC != 0)
    }
  }
  if (r == 0) {
    //
    // We have to write the sector status last to make sure the BRSI information is valid.
    //
    INIT_LSH_DATA_RANGE();
    _SetLogSectorDataStat(pInst, &lsh, DATA_STAT_VALID);
#if (FAIL_SAFE_ERASE_NO_REWRITE != 0) && (FS_NOR_SUPPORT_ECC != 0)
    if (_IsECCEnabled(pInst) != 0) {
      //
      // With the support for incremental write operation
      // disabled the valid and invalid indicators are
      // not protected via CRC but only via ECC.
      //
      r = _CalcStoreLSH_NRWithECC(pInst, &lsh);
    }
    if (r == 0)
#endif // (FAIL_SAFE_ERASE_NO_REWRITE != 0) && (FS_NOR_SUPPORT_ECC != 0)
    {
      r = _WriteLSH(pInst, PhySectorIndex, srsi, &lsh);
    }
  }
  INIT_LSH_DATA_RANGE();
  return r;
}

#endif // FS_NOR_CAN_REWRITE == 0

/*********************************************************************
*
*        _MarkLogSectorAsValidFast
*
*  Function description
*    Updates LSH to indicate that the data is valid.
*
*  Parameters
*    pInst            Driver instance.
*    PhySectorIndex   Index of the physical sector that stores the logical sector.
*    srsi             Index of the logical sector in the physical sector.
*    brsi             Index of the logical sector in the data block.
*
*  Return value
*    ==0    OK, data marked as valid.
*    !=0    An error occurred.
*
*  Additional information
*    This function assumes that the checking via CRC and ECC are disabled
*    and that the support for incremental write operations is enabled.
*/
static int _MarkLogSectorAsValidFast(NOR_BM_INST * pInst, unsigned PhySectorIndex, unsigned srsi, unsigned brsi) {
  int r;

  r = 0;
  if (brsi != BRSI_INVALID) {
    r = _WriteLogSectorBRSIFast(pInst, PhySectorIndex, srsi, (U16)brsi);
  }
  if (r == 0) {
    //
    // We have to write the sector status last to make sure the BRSI information is valid.
    //
    r = _WriteLogSectorDataStatFast(pInst, PhySectorIndex, srsi, DATA_STAT_VALID);
  }
  return r;
}

#if (FS_NOR_CAN_REWRITE == 0)

/*********************************************************************
*
*        _MarkLogSectorAsValidSlow
*
*  Function description
*    Updates LSH to indicate that the data is valid.
*
*  Parameters
*    pInst            Driver instance.
*    PhySectorIndex   Index of the physical sector that stores the logical sector.
*    srsi             Index of the logical sector in the physical sector.
*    brsi             Index of the logical sector in the data block.
*
*  Return value
*    ==0    OK, data marked as valid.
*    !=0    An error occurred.
*
*  Additional information
*    This function assumes that the checking via CRC and ECC are disabled
*    and that the support for incremental write operations is disabled.
*/
static int _MarkLogSectorAsValidSlow(NOR_BM_INST * pInst, unsigned PhySectorIndex, unsigned srsi, unsigned brsi) {
  int r;

  r = 0;
  if (brsi != BRSI_INVALID) {
    r = _WriteLogSectorBRSISlow(pInst, PhySectorIndex, srsi, (U16)brsi);
  }
  if (r == 0) {
    //
    // We have to write the sector status last to make sure the BRSI information is valid.
    //
    r = _WriteLogSectorDataStatSlow(pInst, PhySectorIndex, srsi, DATA_STAT_VALID);
  }
  return r;
}

#endif // FS_NOR_CAN_REWRITE == 0

/*********************************************************************
*
*        _MarkLogSectorAsValidNC
*
*  Function description
*    Updates LSH to indicate that the data is valid.
*
*  Parameters
*    pInst            Driver instance.
*    PhySectorIndex   Index of the physical sector that stores the logical sector.
*    srsi             Index of the logical sector in the physical sector.
*    brsi             Index of the logical sector in the data block.
*
*  Return value
*    ==0    OK, data marked as valid.
*    !=0    An error occurred.
*
*  Additional information
*    This function assumes that the checking via CRC and ECC is disabled.
*/
static int _MarkLogSectorAsValidNC(NOR_BM_INST * pInst, unsigned PhySectorIndex, unsigned srsi, unsigned brsi) {
  int r;

#if FS_NOR_CAN_REWRITE
  r = _MarkLogSectorAsValidFast(pInst, PhySectorIndex, srsi, brsi);
#else
  if (_IsRewriteSupported(pInst) != 0) {
    r = _MarkLogSectorAsValidFast(pInst, PhySectorIndex, srsi, brsi);
  } else {
    r = _MarkLogSectorAsValidSlow(pInst, PhySectorIndex, srsi, brsi);
  }
#endif // FS_NOR_CAN_REWRITE
  return r;
}

/*********************************************************************
*
*        _MarkLogSectorAsValid
*
*  Function description
*    Updates LSH to indicate that the data is valid.
*
*  Parameters
*    pInst            Driver instance.
*    PhySectorIndex   Index of the physical sector that stores the logical sector.
*    srsi             Index of the logical sector in the physical sector.
*    brsi             Index of the logical sector in the data block.
*    pDataCheck       CRC and ECC information of the sector data. It can be set to NULL.
*
*  Return value
*    ==0    OK, data marked as valid.
*    !=0    An error occurred.
*/
static int _MarkLogSectorAsValid(NOR_BM_INST * pInst, unsigned PhySectorIndex, unsigned srsi, unsigned brsi, const DATA_CHECK * pDataCheck) {
  int r;

#if FS_NOR_CAN_REWRITE
  if ((_IsCRCEnabled(pInst) != 0) || (_IsECCEnabled(pInst) != 0)) {
    r = _MarkLogSectorAsValidWithCRCAndECC(pInst, PhySectorIndex, srsi, brsi, pDataCheck);
  } else {
    r = _MarkLogSectorAsValidNC(pInst, PhySectorIndex, srsi, brsi);
  }
#else
  if ((_IsCRCEnabled(pInst) == 0) && (_IsECCEnabled(pInst) == 0)) {
    r = _MarkLogSectorAsValidNC(pInst, PhySectorIndex, srsi, brsi);
  } else {
    if (_IsRewriteSupported(pInst) != 0) {
      r = _MarkLogSectorAsValidWithCRCAndECC(pInst, PhySectorIndex, srsi, brsi, pDataCheck);
    } else {
      r = _MarkLogSectorAsValidNRWithCRCAndECC(pInst, PhySectorIndex, srsi, brsi, pDataCheck);
    }
  }
#endif // FS_NOR_CAN_REWRITE
  return r;
}

/*********************************************************************
*
*        _MarkLogSectorAsInvalidWithCRCAndECC
*
*  Function description
*    Updates LSH to indicate that the sector data is invalid.
*
*  Parameters
*    pInst            Driver instance.
*    PhySectorIndex   Index of the physical sector that stores the logical sector.
*    srsi             Index of the logical sector in the physical sector.
*
*  Return value
*    ==0      OK, data stored successfully.
*    !=0      An error occurred.
*
*  Additional information
*    This function is called during a sector write operation.
*    It assumes that the support for incremental write operations
*    is enabled.
*/
static int _MarkLogSectorAsInvalidWithCRCAndECC(NOR_BM_INST * pInst, unsigned PhySectorIndex, unsigned srsi) {
  int        r;
  NOR_BM_LSH lsh;

  FS_MEMSET(&lsh, 0xFF, sizeof(lsh));
  r = _ReadLSH(pInst, PhySectorIndex, srsi, &lsh);
  if (r == 0) {
    INIT_LSH_DATA_RANGE();
#if (FS_NOR_SUPPORT_CRC != 0) || (FS_NOR_SUPPORT_ECC != 0)
    {
      unsigned DataStatSaved;

      DataStatSaved = _GetLogSectorDataStat(pInst, &lsh);
      _SetLogSectorDataStat(pInst, &lsh, DATA_STAT_INVALID);
      r = _CalcStoreLSHWithCRCAndECC(pInst, &lsh, NULL);
      if (r == 0) {
          //
          // Update the data in the logical sector header with the data status information
          // set to the previous value to indicate that the information is not complete.
          // The data status is updated later via a separate write operation
          // in order to make sure that the operation is fail-safe.
          //
          _SetLogSectorDataStat(pInst, &lsh, DataStatSaved);
#if FS_NOR_SUPPORT_ECC
        if (_IsECCEnabled(pInst) != 0) {
          //
          // With the ECC enabled we use the ECC status to indicate that the
          // data of the LSH is valid.
          //
          _SetLSH_ECCToEmpty(&lsh);
          r = _WriteLSH(pInst, PhySectorIndex, srsi, &lsh);
          if (r == 0) {
            //
            // Mark the logical sector as invalid.
            //
            r = _WriteLogSectorDataStatFast(pInst, PhySectorIndex, srsi, DATA_STAT_INVALID);
            if (r == 0) {
              //
              // Now make the ECC status valid and write it to storage.
              //
              r = _MarkLogSectorECCAsValid(pInst, PhySectorIndex, srsi, &lsh);
            }
          }
        } else
#endif // FS_NOR_SUPPORT_ECC
        {
          r = _WriteLSH(pInst, PhySectorIndex, srsi, &lsh);
          if (r == 0) {
            r = _WriteLogSectorDataStatFast(pInst, PhySectorIndex, srsi, DATA_STAT_INVALID);
          }
        }
      }
    }
#else
    r = _WriteLogSectorDataStatFast(pInst, PhySectorIndex, srsi, DATA_STAT_INVALID);
#endif // (FS_NOR_SUPPORT_CRC != 0) || (FS_NOR_SUPPORT_ECC != 0)
    INIT_LSH_DATA_RANGE();
  }
  return r;
}

#if (FS_NOR_CAN_REWRITE == 0)

/*********************************************************************
*
*        _MarkLogSectorAsInvalidNRWithCRCAndECC
*
*  Function description
*    Marks a logical sector as invalid.
*
*  Parameters
*    pInst            Driver instance.
*    PhySectorIndex   Index of the physical sector that stores the logical sector.
*    srsi             Index of the logical sector in the physical sector.
*
*  Return value
*    ==0      OK, data stored successfully.
*    !=0      An error occurred.
*
*  Additional information
*    This function assumes that the support for incremental write
*    operations is disabled (i.e. _IsRewriteSupported(pInst) == 0 is true)
*/
static int _MarkLogSectorAsInvalidNRWithCRCAndECC(NOR_BM_INST * pInst, unsigned PhySectorIndex, unsigned srsi) {
  int        r;
  NOR_BM_LSH lsh;

  FS_MEMSET(&lsh, 0xFF, sizeof(lsh));
  r = _ReadLSH(pInst, PhySectorIndex, srsi, &lsh);
  if (r == 0) {
    INIT_LSH_DATA_RANGE();
    _SetLogSectorDataStat(pInst, &lsh, DATA_STAT_INVALID);
#if (FAIL_SAFE_ERASE_NO_REWRITE != 0) && (FS_NOR_SUPPORT_ECC != 0)
    if (_IsECCEnabled(pInst) != 0) {
      //
      // With the support for incremental write operation
      // disabled the valid and invalid indicators are
      // not protected via CRC but only via ECC.
      //
      r = _CalcStoreLSH_NRWithECC(pInst, &lsh);
    }
    if (r == 0)
#endif // (FAIL_SAFE_ERASE_NO_REWRITE != 0) && (FS_NOR_SUPPORT_ECC != 0)
    {
      r = _WriteLSH(pInst, PhySectorIndex, srsi, &lsh);
    }
    INIT_LSH_DATA_RANGE();
  }
  return r;
}

#endif // (FS_NOR_CAN_REWRITE == 0)

/*********************************************************************
*
*        _MarkLogSectorAsInvalidNC
*
*  Function description
*    Updates LSH to indicate that the data is invalid.
*
*  Parameters
*    pInst            Driver instance.
*    PhySectorIndex   Index of the physical sector that stores the logical sector.
*    srsi             Index of the logical sector in the physical sector.
*
*  Return value
*    ==0    OK, data marked as invalid.
*    !=0    An error occurred.
*
*  Additional information
*    This function assumes that the checking via CRC and ECC are disabled.
*/
static int _MarkLogSectorAsInvalidNC(NOR_BM_INST * pInst, unsigned PhySectorIndex, unsigned srsi) {
  int r;

#if FS_NOR_CAN_REWRITE
  r = _WriteLogSectorDataStatFast(pInst, PhySectorIndex, srsi, DATA_STAT_INVALID);
#else
  if (_IsRewriteSupported(pInst) != 0) {
    r = _WriteLogSectorDataStatFast(pInst, PhySectorIndex, srsi, DATA_STAT_INVALID);
  } else {
    r = _WriteLogSectorDataStatSlow(pInst, PhySectorIndex, srsi, DATA_STAT_INVALID);
  }
#endif // FS_NOR_CAN_REWRITE
  return r;
}

/*********************************************************************
*
*        _MarkLogSectorAsInvalid
*
*  Function description
*    Updates LSH to indicate that the data is invalid.
*
*  Parameters
*    pInst            Driver instance.
*    PhySectorIndex   Index of the physical sector that stores the logical sector.
*    srsi             Index of the logical sector in the physical sector.
*
*  Return value
*    ==0    OK, data marked as valid.
*    !=0    An error occurred.
*
*  Additional information
*    This function assumes that the checking via CRC and ECC are disabled.
*/
static int _MarkLogSectorAsInvalid(NOR_BM_INST * pInst, unsigned PhySectorIndex, unsigned srsi) {
  int r;

#if FS_NOR_CAN_REWRITE
  if ((_IsCRCEnabled(pInst) != 0) || (_IsECCEnabled(pInst) != 0)) {
    r = _MarkLogSectorAsInvalidWithCRCAndECC(pInst, PhySectorIndex, srsi);
  } else {
    r = _MarkLogSectorAsInvalidNC(pInst, PhySectorIndex, srsi);
  }
#else
  if ((_IsCRCEnabled(pInst) == 0) && (_IsECCEnabled(pInst) == 0)) {
    r = _MarkLogSectorAsInvalidNC(pInst, PhySectorIndex, srsi);
  } else {
    if (_IsRewriteSupported(pInst) != 0) {
      r = _MarkLogSectorAsInvalidWithCRCAndECC(pInst, PhySectorIndex, srsi);
    } else {
      r = _MarkLogSectorAsInvalidNRWithCRCAndECC(pInst, PhySectorIndex, srsi);
    }
  }
#endif // FS_NOR_CAN_REWRITE
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
*    pInst        [IN]  Driver instance.
*    ErrorType    Identifies the error.
*    ErrorPSI     Index of the physical sector where the error occurred.
*/
static void _OnFatalError(NOR_BM_INST * pInst, int ErrorType, U32 ErrorPSI) {
  FS_NOR_FATAL_ERROR_INFO ErrorInfo;
  int                     Result;
  U16                     IsWriteProtected;
  U16                     HasFatalError;
  U16                     ErrorType16;
  U8                      MarkAsReadOnly;

  MarkAsReadOnly       = 0;        // Per default, leave the NOR flash writable.
  pInst->HasFatalError = 1;
  pInst->ErrorType     = (U8)ErrorType;
  pInst->ErrorPSI      = ErrorPSI;
  FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_BM: Fatal error %d occurred on phy. sector %u.", (int)ErrorType, ErrorPSI));
  if (_pfOnFatalError != NULL) {
    ErrorInfo.Unit      = pInst->Unit;
    ErrorInfo.ErrorType = (U8)ErrorType;
    ErrorInfo.ErrorPSI  = ErrorPSI;
    Result =  _pfOnFatalError(&ErrorInfo);
    if (Result == 0) {                // Did application request to mark the NOR flash as read-only?
      MarkAsReadOnly = 1;
    }
  }
  //
  // If requested, mark the NOR flash as read-only.
  //
  if (MarkAsReadOnly != 0u) {
    //
    // Do not attempt to write to NOR flash if it is write protected.
    //
    if (pInst->IsWriteProtected == 0u) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_BM: Switching permanently to read-only mode."));
      pInst->IsWriteProtected = 1;
      IsWriteProtected = 0;   // Reversed logic 0 means write protected.
      HasFatalError    = 0;   // Reversed logic 0 means fatal error.
      ErrorType16      = (U16)ErrorType;
#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
      _pMultiByteAPI->pfStoreU16(SEGGER_PTR2PTR(U8, &IsWriteProtected), IsWriteProtected);                        // MISRA deviation D:100e
      _pMultiByteAPI->pfStoreU16(SEGGER_PTR2PTR(U8, &HasFatalError),    HasFatalError);                           // MISRA deviation D:100e
      _pMultiByteAPI->pfStoreU16(SEGGER_PTR2PTR(U8, &ErrorType16),      ErrorType16);                             // MISRA deviation D:100e
      _pMultiByteAPI->pfStoreU32(SEGGER_PTR2PTR(U8, &ErrorPSI),         ErrorPSI);                                // MISRA deviation D:100e
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
      //
      // Save the write protected status and the error information into the first block
      //
#if FS_NOR_SUPPORT_ECC
      if (_IsECCEnabled(pInst) != 0) {
        unsigned   NumBytes;
        U8       * pData8;

        //
        // With the ECC feature enabled the error information is written at once.
        //
        NumBytes = 1uL << pInst->pECCHookData->ldBytesPerBlock;
        pData8   = SEGGER_PTR2PTR(U8, _pECCBuffer);                                                               // MISRA deviation D:100e
        FS_MEMSET(pData8, 0xFF, NumBytes);
        FS_MEMCPY(pData8 + INFO_OFF_IS_WRITE_PROTECTED, &IsWriteProtected, sizeof(IsWriteProtected));
        FS_MEMCPY(pData8 + INFO_OFF_HAS_FATAL_ERROR,    &HasFatalError,    sizeof(HasFatalError));
        FS_MEMCPY(pData8 + INFO_OFF_ERROR_TYPE,         &ErrorType,        sizeof(ErrorType));
        FS_MEMCPY(pData8 + INFO_OFF_ERROR_PSI,          &ErrorPSI,         sizeof(ErrorPSI));
        (void)_WriteLogSectorData(pInst, PSI_INFO_BLOCK, SRSI_INFO_ERROR, _pECCBuffer, 0, NumBytes);
      } else
#endif // FS_NOR_SUPPORT_ECC
      {
        //
        // Write the error information one value at a time in order to keep the RAM usage low.
        //
        (void)_WriteLogSectorData(pInst, PSI_INFO_BLOCK, SRSI_INFO_ERROR, &IsWriteProtected, INFO_OFF_IS_WRITE_PROTECTED, sizeof(IsWriteProtected));
        (void)_WriteLogSectorData(pInst, PSI_INFO_BLOCK, SRSI_INFO_ERROR, &HasFatalError,    INFO_OFF_HAS_FATAL_ERROR,    sizeof(HasFatalError));
        (void)_WriteLogSectorData(pInst, PSI_INFO_BLOCK, SRSI_INFO_ERROR, &ErrorType16,      INFO_OFF_ERROR_TYPE,         sizeof(ErrorType16));
        (void)_WriteLogSectorData(pInst, PSI_INFO_BLOCK, SRSI_INFO_ERROR, &ErrorPSI,         INFO_OFF_ERROR_PSI,          sizeof(ErrorPSI));
      }
#if (FS_NOR_SUPPORT_CRC != 0) || (FS_NOR_SUPPORT_ECC != 0)
      {
        NOR_BM_LSH lsh;

        FS_MEMSET(&lsh, 0xFF, sizeof(lsh));
        INIT_LSH_DATA_RANGE();
#if FS_NOR_SUPPORT_CRC
        if (_IsCRCEnabled(pInst) != 0) {
          U16 crc;

          crc = CRC_SECTOR_DATA_INIT;
          crc = _pCRC_API->pfCalcData(SEGGER_PTR2PTR(U8, &IsWriteProtected), sizeof(IsWriteProtected), crc);      // MISRA deviation D:100e
          crc = _pCRC_API->pfCalcData(SEGGER_PTR2PTR(U8, &HasFatalError),    sizeof(HasFatalError),    crc);      // MISRA deviation D:100e
          crc = _pCRC_API->pfCalcData(SEGGER_PTR2PTR(U8, &ErrorType16),      sizeof(ErrorType16),      crc);      // MISRA deviation D:100e
          crc = _pCRC_API->pfCalcData(SEGGER_PTR2PTR(U8, &ErrorPSI),         sizeof(ErrorPSI),         crc);      // MISRA deviation D:100e
          lsh.crcSectorData = crc;
          UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_LSH, crcSectorData), sizeof(lsh.crcSectorData));
          (void)_pCRC_API->pfCalcStoreLSH(&lsh);
        }
#endif // FS_NOR_SUPPORT_CRC
#if FS_NOR_SUPPORT_ECC
        if (_IsECCEnabled(pInst) != 0) {
          //
          // Calculate the ECC of the LSH and of the error information and store them to LSH.
          // The error information was stored above to the ECC buffer.
          //
          pInst->pECC_API->pfCalcData(pInst, _pECCBuffer, lsh.aaECCSectorData[0]);
          UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_LSH, aaECCSectorData), sizeof(lsh.aaECCSectorData));
          (void)pInst->pECC_API->pfCalcStoreLSH(pInst, &lsh, NULL);
        }
#endif // FS_NOR_SUPPORT_ECC
        //
        // Write the LSH of the error information.
        //
        (void)_WriteLSH(pInst, PSI_INFO_BLOCK, SRSI_INFO_ERROR, &lsh);
        INIT_LSH_DATA_RANGE();
      }
#endif // FS_NOR_SUPPORT_CRC != 0 || FS_NOR_SUPPORT_ECC != 0
    }
  }
}

#if FS_SUPPORT_TEST

/*********************************************************************
*
*       _IsPSIAssignedToWorkBlockDesc
*
*  Function description
*    Checks if the specified physical sector is used as work block.
*
*  Parameters
*    psi          Index of the physical sector to be checked.
*    pWorkBlock   First work block to be checked.
*
*  Return value
*    ==0      The block is not used as work block.
*    ==1      The block is used as work block.
*/
static int _IsPSIAssignedToWorkBlockDesc(unsigned psi, const NOR_BM_WORK_BLOCK * pWorkBlock) {
  while (pWorkBlock != NULL) {
    if (psi == pWorkBlock->psi) {
      return 1;
    }
    pWorkBlock = pWorkBlock->pNext;
  }
  return 0;
}

#if FS_NOR_OPTIMIZE_DATA_WRITE

/*********************************************************************
*
*       _IsPSIAssignedToDataBlockDesc
*
*  Function description
*    Checks if the specified physical sector is used as data block.
*
*  Parameters
*    psi          Index of the physical sector to be checked.
*    pDataBlock   First data block to be checked.
*
*  Return value
*    ==0      The physical sector is not used as data block.
*    ==1      The physical sector is used as data block.
*/
static int _IsPSIAssignedToDataBlockDesc(unsigned psi, const NOR_BM_DATA_BLOCK * pDataBlock) {
  while (pDataBlock != NULL) {
    if (psi == pDataBlock->psi) {
      return 1;
    }
    pDataBlock = pDataBlock->pNext;
  }
  return 0;
}

#endif // FS_NOR_OPTIMIZE_DATA_WRITE

/*********************************************************************
*
*       _IsPSIAssignedToDataBlock
*
*  Function description
*    Checks if the specified phy. sector is used as data block.
*
*  Parameters
*    pInst        Driver instance.
*    psi          Index of the physical sector to be checked.
*    lbiStart     Index of the first logical block to be checked.
*
*  Return value
*    ==0      The block is not used as work block.
*    ==1      The block is used as work block.
*/
static int _IsPSIAssignedToDataBlock(const NOR_BM_INST * pInst, unsigned psi, unsigned lbiStart) {
  unsigned lbi;
  unsigned psiToCheck;

  for (lbi = lbiStart; lbi < pInst->NumLogBlocks; ++lbi) {
    psiToCheck = _L2P_Read(pInst, lbi);
    if (psiToCheck == psi) {
      return 1;
    }
  }
  return 0;
}

#if (FS_NOR_SUPPORT_CRC != 0) && (FS_NOR_SUPPORT_ECC != 0)

/*********************************************************************
*
*       _CheckOneLogSectorWithCRCAndECC
*
*  Function description
*    Verifies the data integrity of the specified logical sector using CRC and ECC.
*/
static int _CheckOneLogSectorWithCRCAndECC(NOR_BM_INST * pInst, unsigned psi, unsigned srsi) {
  int          r;
  unsigned     DataStat;
  int          NumRetries;
  NOR_BM_LSH   lsh;
  unsigned     NumBytesAtOnce;
  unsigned     NumBytes;
  U32          Off;
  U32          SizeOfBuffer;
  U32        * pBuffer;
  I32          NumBytesFree;
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS)
  U32          FilterMask;
#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS
  unsigned     BytesPerBlock;
  unsigned     ldBytesPerBlock;
  unsigned     iBlock;
  U8         * pData8;
  unsigned     NumBlocks;
  int          Result;
  U16          crcCalc;
  U16          crcRead;

  FS_MEMSET(&lsh, 0xFF, sizeof(lsh));
  r = _ReadLSH(pInst, psi, srsi, &lsh);
  if (r != 0) {
    return 1;             // Error, could not read the header of the logical sector.
  }
  DataStat = _GetLogSectorDataStat(pInst, &lsh);
  if (DataStat != DATA_STAT_VALID) {
    return 0;             // OK, the sector data is not valid.
  }
  //
  // Choose a buffer for the sector data.
  //
  ldBytesPerBlock = pInst->pECCHookData->ldBytesPerBlock;
  BytesPerBlock   = 1uL << ldBytesPerBlock;
  pBuffer         = _UseFreeMem(&NumBytesFree);
  SizeOfBuffer    = BytesPerBlock;
  if (pBuffer != NULL) {
    if ((I32)SizeOfBuffer < NumBytesFree) {
      SizeOfBuffer = (U32)NumBytesFree & ~(BytesPerBlock - 1u);     // Make sure that the buffer is a multiple of ECC block size.
    } else {
      pBuffer = NULL;
    }
  }
  if (pBuffer == NULL) {
    pBuffer = _pECCBuffer;
  }
  NumRetries = FS_NOR_NUM_READ_RETRIES;
  for (;;) {
    NumBytes = 1uL << pInst->ldBytesPerSector;
    Off      = _GetLogSectorDataOff(pInst, psi,  srsi);
    crcCalc  = CRC_SECTOR_DATA_INIT;
    iBlock   = 0;
    do {
      NumBytesAtOnce = SEGGER_MIN(NumBytes, SizeOfBuffer);
      //
      // Disable the error message output by the _ReadOff to prevent that
      // the test application is stopped in case of a temporarily read failure.
      //
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS)
      FilterMask = FS__GetErrorFilterNL();
      FS__SetErrorFilterNL(FilterMask & ~FS_MTYPE_DRIVER);
#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS
      r = _ReadOff(pInst, pBuffer, Off,  NumBytesAtOnce);
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS)
      FS__SetErrorFilterNL(FilterMask);
#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS
      if (r != 0) {
        FS_DEBUG_WARN((FS_MTYPE_DRIVER, "NOR_BM: _CheckOneLogSectorWithCRCAndECC: Read failed PSI: %lu, SRSI: %lu", psi, srsi));
        break;                        // Error, could not read data from NOR flash.
      }
      //
      // Correct the eventual bit errors using ECC.
      //
      NumBlocks = NumBytesAtOnce >> ldBytesPerBlock;
      pData8    = SEGGER_PTR2PTR(U8, pBuffer);                                                                    // MISRA deviation D:100e
      do {
        Result = pInst->pECC_API->pfApplyData(pInst, SEGGER_PTR2PTR(U32, pData8), lsh.aaECCSectorData[iBlock]);   // MISRA deviation D:100e
        if (Result < 0) {
          FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_BM: _CheckOneLogSectorWithCRCAndECC: ECC check failed PSI: %lu, SRSI: %lu", psi, srsi));
          r = RESULT_ECC_ERROR;
          break;
        }
        UPDATE_NUM_BIT_ERRORS(pInst, Result);
        pData8 += BytesPerBlock;
        ++iBlock;
        if (iBlock >= FS_NOR_MAX_NUM_BLOCKS_ECC_DATA) {
          break;
        }
      } while (--NumBlocks != 0u);
      if (r != 0) {
        break;
      }
      crcCalc = _pCRC_API->pfCalcData(SEGGER_PTR2PTR(U8, pBuffer), NumBytesAtOnce, crcCalc);                      // MISRA deviation D:100e
      NumBytes -= NumBytesAtOnce;
      Off      += NumBytesAtOnce;
    } while (NumBytes != 0u);
    if (r == 0) {
      crcRead = lsh.crcSectorData;
      if (crcCalc == crcRead) {
        break;                        // OK, the data is consistent.
      }
      FS_DEBUG_WARN((FS_MTYPE_DRIVER, "NOR_BM: _CheckOneLogSectorWithCRCAndECC: CRC check failed PSI: %lu, SRSI: %lu, Calc: 0x%02x, Read: 0x%02x", psi, srsi, crcCalc, crcRead));
    }
    //
    // The CRC check or the write operation failed. Try again.
    //
    if (NumRetries-- == 0) {
      r = 1;
      break;                        // Error, no more read retries.
    }
  }
  _UnuseFreeMem(NumBytesFree);
  return r;
}

#endif // FS_NOR_SUPPORT_CRC != 0 && FS_NOR_SUPPORT_ECC != 0

#if FS_NOR_SUPPORT_CRC

/*********************************************************************
*
*       _CheckOneLogSectorWithCRC
*
*  Function description
*    Verifies the data integrity of the specified logical sector using CRC.
*/
static int _CheckOneLogSectorWithCRC(NOR_BM_INST * pInst, unsigned psi, unsigned srsi) {
  int          r;
  unsigned     DataStat;
  int          NumRetries;
  NOR_BM_LSH   lsh;
  U16          crcCalc;
  U16          crcRead;
  U32          aBuffer[FS_NOR_DATA_BUFFER_SIZE / 4];
  unsigned     NumBytesAtOnce;
  unsigned     NumBytes;
  U32          Off;
  U32          SizeOfBuffer;
  U32        * pBuffer;
  I32          NumBytesFree;
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS)
  U32          FilterMask;
#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS

  FS_MEMSET(&lsh, 0xFF, sizeof(lsh));
  r = _ReadLSH(pInst, psi, srsi, &lsh);
  if (r != 0) {
    return 1;             // Error, could not read the header of the logical sector.
  }
  DataStat = _GetLogSectorDataStat(pInst, &lsh);
  if (DataStat != DATA_STAT_VALID) {
    return 0;             // OK, the sector data is not valid.
  }
  //
  // Choose a buffer for the sector data.
  //
  pBuffer      = _UseFreeMem(&NumBytesFree);
  SizeOfBuffer = sizeof(aBuffer);
  if (pBuffer != NULL) {
    if ((I32)SizeOfBuffer < NumBytesFree) {
      SizeOfBuffer = (U32)NumBytesFree;
    } else {
      pBuffer = NULL;
    }
  }
  if (pBuffer == NULL) {
    pBuffer = aBuffer;
  }
  NumRetries = FS_NOR_NUM_READ_RETRIES;
  for (;;) {
    NumBytes = 1uL << pInst->ldBytesPerSector;
    Off      = _GetLogSectorDataOff(pInst, psi,  srsi);
    crcCalc  = CRC_SECTOR_DATA_INIT;
    do {
      NumBytesAtOnce = SEGGER_MIN(NumBytes, SizeOfBuffer);
      //
      // Disable the error message output by the _ReadOff to prevent that
      // the test application is stopped in case of a temporarily read failure.
      //
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS)
      FilterMask = FS__GetErrorFilterNL();
      FS__SetErrorFilterNL(FilterMask & ~FS_MTYPE_DRIVER);
#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS
      r = _ReadOff(pInst, pBuffer, Off,  NumBytesAtOnce);
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS)
      FS__SetErrorFilterNL(FilterMask);
#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS
      if (r != 0) {
        FS_DEBUG_WARN((FS_MTYPE_DRIVER, "NOR_BM: _CheckOneLogSector: Read failed PSI: %lu, SRSI: %lu", psi, srsi));
        break;                        // Error, could not read data from NOR flash.
      }
      crcCalc = _pCRC_API->pfCalcData(SEGGER_PTR2PTR(U8, pBuffer), NumBytesAtOnce, crcCalc);                      // MISRA deviation D:100e
      NumBytes -= NumBytesAtOnce;
      Off      += NumBytesAtOnce;
    } while (NumBytes != 0u);
    if (r == 0) {
      crcRead = lsh.crcSectorData;
      if (crcCalc == crcRead) {
        break;                        // OK, the data is consistent.
      }
      FS_DEBUG_WARN((FS_MTYPE_DRIVER, "NOR_BM: _CheckOneLogSector: CRC check failed PSI: %lu, SRSI: %lu, Calc: 0x%02x, Read: 0x%02x", psi, srsi, crcCalc, crcRead));
    }
    //
    // The CRC check or the write operation failed. Try again.
    //
    if (NumRetries-- == 0) {
      r = 1;
      break;                        // Error, no more read retries.
    }
  }
  _UnuseFreeMem(NumBytesFree);
  return r;
}

#endif // FS_NOR_SUPPORT_CRC

#if FS_NOR_SUPPORT_ECC

/*********************************************************************
*
*       _CheckOneLogSectorWithECC
*
*  Function description
*    Verifies the data integrity of the specified logical sector using ECC.
*/
static int _CheckOneLogSectorWithECC(NOR_BM_INST * pInst, unsigned psi, unsigned srsi) {
  int          r;
  unsigned     DataStat;
  int          NumRetries;
  NOR_BM_LSH   lsh;
  unsigned     NumBytesAtOnce;
  unsigned     NumBytes;
  U32          Off;
  U32          SizeOfBuffer;
  U32        * pBuffer;
  I32          NumBytesFree;
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS)
  U32          FilterMask;
#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS
  unsigned     BytesPerBlock;
  unsigned     ldBytesPerBlock;
  unsigned     iBlock;
  U8         * pData8;
  unsigned     NumBlocks;
  int          Result;

  FS_MEMSET(&lsh, 0xFF, sizeof(lsh));
  r = _ReadLSH(pInst, psi, srsi, &lsh);
  if (r != 0) {
    return 1;             // Error, could not read the header of the logical sector.
  }
  DataStat = _GetLogSectorDataStat(pInst, &lsh);
  if (DataStat != DATA_STAT_VALID) {
    return 0;             // OK, the sector data is not valid.
  }
  //
  // Choose a buffer for the sector data.
  //
  ldBytesPerBlock = pInst->pECCHookData->ldBytesPerBlock;
  BytesPerBlock   = 1uL << ldBytesPerBlock;
  pBuffer         = _UseFreeMem(&NumBytesFree);
  SizeOfBuffer    = BytesPerBlock;
  if (pBuffer != NULL) {
    if ((I32)SizeOfBuffer < NumBytesFree) {
      SizeOfBuffer = (U32)NumBytesFree & ~(BytesPerBlock - 1u);     // Make sure that the buffer is a multiple of ECC block size.
    } else {
      pBuffer = NULL;
    }
  }
  if (pBuffer == NULL) {
    pBuffer = _pECCBuffer;
  }
  NumRetries = FS_NOR_NUM_READ_RETRIES;
  for (;;) {
    NumBytes = 1uL << pInst->ldBytesPerSector;
    Off      = _GetLogSectorDataOff(pInst, psi,  srsi);
    iBlock   = 0;
    do {
      NumBytesAtOnce = SEGGER_MIN(NumBytes, SizeOfBuffer);
      //
      // Disable the error message output by the _ReadOff to prevent that
      // the test application is stopped in case of a temporarily read failure.
      //
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS)
      FilterMask = FS__GetErrorFilterNL();
      FS__SetErrorFilterNL(FilterMask & ~FS_MTYPE_DRIVER);
#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS
      //
      // Read either the entire data of the logical sector from the NOR flash device
      // if the buffer is sufficiently large or only a limited number of ECC blocks.
      //
      r = _ReadOff(pInst, pBuffer, Off,  NumBytesAtOnce);
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS)
      FS__SetErrorFilterNL(FilterMask);
#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS
      if (r != 0) {
        FS_DEBUG_WARN((FS_MTYPE_DRIVER, "NOR_BM: _CheckOneLogSectorWithECC: Read failed PSI: %lu, SRSI: %lu", psi, srsi));
        break;                        // Error, could not read data from NOR flash.
      }
      //
      // Correct the eventual bit errors using ECC.
      //
      NumBlocks = NumBytesAtOnce >> ldBytesPerBlock;
      pData8    = SEGGER_PTR2PTR(U8, pBuffer);                                                                      // MISRA deviation D:100e
      do {
        Result = pInst->pECC_API->pfApplyData(pInst, SEGGER_PTR2PTR(U32, pData8), lsh.aaECCSectorData[iBlock]);     // MISRA deviation D:100e
        if (Result < 0) {
          FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_BM: _CheckOneLogSectorWithECC: ECC check failed PSI: %lu, SRSI: %lu", psi, srsi));
          r = RESULT_ECC_ERROR;
          break;
        }
        UPDATE_NUM_BIT_ERRORS(pInst, Result);
        pData8 += BytesPerBlock;
        ++iBlock;
        if (iBlock >= FS_NOR_MAX_NUM_BLOCKS_ECC_DATA) {
          break;
        }
      } while (--NumBlocks != 0u);
      if (r != 0) {
        break;
      }
      if (iBlock >= FS_NOR_MAX_NUM_BLOCKS_ECC_DATA) {
        break;
      }
      NumBytes -= NumBytesAtOnce;
      Off      += NumBytesAtOnce;
    } while (NumBytes != 0u);
    if (r == 0) {
      break;                        // OK, the data is consistent.
    }
    //
    // The ECC check or the write operation failed. Try again.
    //
    if (NumRetries-- == 0) {
      r = 1;
      break;                        // Error, no more read retries.
    }
  }
  _UnuseFreeMem(NumBytesFree);
  return r;
}

#endif // FS_NOR_SUPPORT_ECC

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
static int _CheckConsistency(const NOR_BM_INST * pInst) {
  unsigned            lbi;
  unsigned            psi;
  NOR_BM_WORK_BLOCK * pWorkBlock;

  if (pInst->IsLLMounted == 0u) {
    return 0;                                     // OK, NOR flash not mounted yet.
  }
  //
  // Check if all the PBIs of data blocks are marked as used.
  //
  for (lbi = 0; lbi < pInst->NumLogBlocks; ++lbi) {
    psi = _L2P_Read(pInst, lbi);
    if (psi != 0u) {
      if (_IsPhySectorFree(pInst, psi) != 0) {
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_BM: _CheckConsistency: Data block marked as free (psi: %u)", psi));
        return 1;                                 // Error, data block is marked as free.
      }
      //
      // Check if the physical blocks that are assigned to work blocks are not assigned to a data block.
      //
      if (_IsPSIAssignedToWorkBlockDesc(psi, pInst->pFirstWorkBlockInUse) != 0) {
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_BM: _CheckConsistency: Work block used as data block (psi: %u)", psi));
        return 1;                                 // Error, work block is used as data block.
      }
      //
      // Check if the PBIs of data blocks are unique.
      //
      if (_IsPSIAssignedToDataBlock(pInst, psi, lbi + 1u) != 0) {
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_BM: _CheckConsistency: Duplicated data block found (psi: %u)", psi));
        return 1;                                 // Error, same physical block assigned to 2 data blocks.
      }
    }
  }
  //
  // Check if the PBIs of work blocks are marked as used.
  //
  pWorkBlock = pInst->pFirstWorkBlockInUse;       // Start with the first block in use.
  while (pWorkBlock != NULL) {
    psi = pWorkBlock->psi;
    if (_IsPhySectorFree(pInst, psi) != 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_BM: _CheckConsistency: Work block is marked as free (psi: %u)", psi));
      return 1;                                   // Error, work block is marked as free.
    }
    pWorkBlock = pWorkBlock->pNext;
    //
    // Check if the PBIs of work blocks are unique.
    //
    if (_IsPSIAssignedToWorkBlockDesc(psi, pWorkBlock) != 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_BM: _CheckConsistency: Duplicated work block found (psi: %u)", psi));
      return 1;                                   // Error, same physical block is assigned to 2 work blocks.
    }
  }
#if FS_NOR_OPTIMIZE_DATA_WRITE
  {
    NOR_BM_DATA_BLOCK * pDataBlock;
    //
    // Check if the PBIs of "special" data blocks are marked as used.
    //
    pDataBlock = pInst->pFirstDataBlockInUse;     // Start with the first block in use.
    while (pDataBlock != NULL) {
      psi = pDataBlock->psi;
      if (_IsPhySectorFree(pInst, psi) != 0) {
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_BM: _CheckConsistency: Data block is marked as free (psi: %u)\n", psi));
        return 1;                                 // Error, data block is marked as free.
      }
      pDataBlock = pDataBlock->pNext;
      //
      // Check if the PBIs of data blocks are unique.
      //
      if (_IsPSIAssignedToDataBlockDesc(psi, pDataBlock) != 0) {
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_BM: _CheckConsistency: Duplicated data block found (psi: %u)\n", psi));
        return 1;                                 // Error, same physical block is assigned to 2 data blocks.
      }
    }
  }
#endif // FS_NOR_OPTIMIZE_DATA_WRITE
  return 0;                                       // OK, no errors found.
}

#if (FS_NOR_SUPPORT_CRC != 0) || (FS_NOR_SUPPORT_ECC != 0)

/*********************************************************************
*
*       _CheckLogSectors
*
*  Function description
*    Checks the consistency of all logical sectors.
*
*  Parameters
*    pInst        Driver instance.
*
*  Return value
*    ==0      No problems found.
*    !=0      Inconsistencies found.
*/
static int _CheckLogSectors(NOR_BM_INST * pInst) {
  unsigned            lbi;
  unsigned            psi;
  NOR_BM_WORK_BLOCK * pWorkBlock;
  unsigned            srsi;
  int                 r;
  unsigned            LSectorsPerPSector;

  if ((_IsCRCEnabled(pInst) == 0) && (_IsECCEnabled(pInst) == 0)) {
    return 0;                   // Do nothing if neither CRC nor ECC is enabled.
  }
  LSectorsPerPSector = pInst->LSectorsPerPSector;
  //
  // Check the logical sectors stored in data blocks.
  //
  for (lbi = 0; lbi < pInst->NumLogBlocks; ++lbi) {
    psi = _L2P_Read(pInst, lbi);
    if (psi != 0u) {
      for (srsi = 0; srsi < LSectorsPerPSector; ++srsi) {
        r = 0;
#if (FS_NOR_SUPPORT_CRC != 0) && (FS_NOR_SUPPORT_ECC != 0)
        if ((_IsCRCEnabled(pInst) != 0) && (_IsECCEnabled(pInst) != 0)) {
          r = _CheckOneLogSectorWithCRCAndECC(pInst, psi, srsi);
        } else {
          if ((_IsCRCEnabled(pInst) != 0) && (_IsECCEnabled(pInst) == 0)) {
            r = _CheckOneLogSectorWithCRC(pInst, psi, srsi);
          } else {
            if ((_IsCRCEnabled(pInst) == 0) && (_IsECCEnabled(pInst) != 0)) {
              r = _CheckOneLogSectorWithECC(pInst, psi, srsi);
            }
          }
        }
#elif (FS_NOR_SUPPORT_CRC != 0) && (FS_NOR_SUPPORT_ECC == 0)
        if (_IsCRCEnabled(pInst) != 0) {
          r = _CheckOneLogSectorWithCRC(pInst, psi, srsi);
        }
#elif (FS_NOR_SUPPORT_CRC == 0) && (FS_NOR_SUPPORT_ECC != 0)
        if (_IsECCEnabled(pInst) != 0) {
          r = _CheckOneLogSectorWithECC(pInst, psi, srsi);
        }
#endif // FS_NOR_SUPPORT_CRC != 0 && FS_NOR_SUPPORT_ECC != 0
        if (r != 0) {
          FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_BM: _CheckLogSectors: Damaged log. sector on data block (psi: %u, srsi: %u)\n", psi, srsi));
          return 1;
        }
      }
    }
  }
  //
  // Check the logical sectors stored in work blocks.
  //
  pWorkBlock = pInst->pFirstWorkBlockInUse;       // Start with the first block in use.
  while (pWorkBlock != NULL) {
    psi = pWorkBlock->psi;
    for (srsi = 0; srsi < LSectorsPerPSector; ++srsi) {
      r = 0;
#if (FS_NOR_SUPPORT_CRC != 0) && (FS_NOR_SUPPORT_ECC != 0)
      if ((_IsCRCEnabled(pInst) != 0) && (_IsECCEnabled(pInst) != 0)) {
        r = _CheckOneLogSectorWithCRCAndECC(pInst, psi, srsi);
      } else {
        if ((_IsCRCEnabled(pInst) != 0) && (_IsECCEnabled(pInst) == 0)) {
          r = _CheckOneLogSectorWithCRC(pInst, psi, srsi);
        } else {
          if ((_IsCRCEnabled(pInst) == 0) && (_IsECCEnabled(pInst) != 0)) {
            r = _CheckOneLogSectorWithECC(pInst, psi, srsi);
          }
        }
      }
#elif (FS_NOR_SUPPORT_CRC != 0) && (FS_NOR_SUPPORT_ECC == 0)
      if (_IsCRCEnabled(pInst) != 0) {
        r = _CheckOneLogSectorWithCRC(pInst, psi, srsi);
      }
#elif (FS_NOR_SUPPORT_CRC == 0) && (FS_NOR_SUPPORT_ECC != 0)
      if (_IsECCEnabled(pInst) != 0) {
        r = _CheckOneLogSectorWithECC(pInst, psi, srsi);
      }
#endif // FS_NOR_SUPPORT_CRC != 0 && FS_NOR_SUPPORT_ECC != 0
      if (r != 0) {
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_BM: _CheckLogSectors: Damaged log. sector on work block (psi: %u, srsi: %u)\n", psi, srsi));
        return 1;
      }
    }
    pWorkBlock = pWorkBlock->pNext;
  }
  return 0;                     // OK, no errors found.
}

#endif // FS_NOR_SUPPORT_CRC != 0 || FS_NOR_SUPPORT_ECC != 0

#endif // FS_SUPPORT_TEST

/*********************************************************************
*
*        _MarkPhySectorAsInvalidWithCRCAndECC
*
*  Function description
*    Updates PSH to indicate that the sector data is invalid.
*
*  Parameters
*    pInst            Driver instance.
*    PhySectorIndex   Index of the physical sector that stores the logical sector.
*    pPSH             Header of the physical sector.
*
*  Return value
*    ==0      OK, data stored successfully.
*    !=0      An error occurred.
*
*  Additional information
*    This function is called during a sector write operation.
*    It assumes that the support for incremental write operations
*    is enabled.
*/
static int _MarkPhySectorAsInvalidWithCRCAndECC(NOR_BM_INST * pInst, unsigned PhySectorIndex, NOR_BM_PSH * pPSH) {
  int r;

  INIT_LSH_DATA_RANGE();
#if (FS_NOR_SUPPORT_CRC != 0) || (FS_NOR_SUPPORT_ECC != 0)
  {
    unsigned DataStatSaved;

    DataStatSaved = _GetPhySectorDataStat(pInst, pPSH);
    _SetPhySectorDataStat(pInst, pPSH, DATA_STAT_INVALID);
    r = _CalcStorePSHWithCRCAndECC(pInst, pPSH);
    if (r == 0) {
#if FS_NOR_SUPPORT_ECC
      if (_IsECCEnabled(pInst) != 0) {
        //
        // With the ECC enabled we use the ECC status in order to indicate that the
        // data of the PSH is valid.
        //
        _SetPSH_ECCToEmpty(pPSH);
        UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_PSH, DataStat), sizeof(pPSH->DataStat));
        r = _WritePSH(pInst, PhySectorIndex, pPSH);
        if (r == 0) {
          //
          // Now make the ECC status valid and write it to storage.
          //
          r = _MarkPhySectorECCAsValid(pInst, PhySectorIndex, pPSH);
        }
      } else
#endif // FS_NOR_SUPPORT_ECC
      {
        //
        // Update the data in the phy. sector header with the data status information
        // set to the previous value to indicate that the information is not complete.
        // The data status is updated later via a separate write operation
        // in order to make sure that the operation is fail-safe.
        //
        _SetPhySectorDataStat(pInst, pPSH, DataStatSaved);
        r = _WritePSH(pInst, PhySectorIndex, pPSH);
        if (r == 0) {
          r = _WritePhySectorDataStatFast(pInst, PhySectorIndex, DATA_STAT_INVALID);
        }
      }
    }
  }
#else
  FS_USE_PARA(pPSH);
  r = _WritePhySectorDataStatFast(pInst, PhySectorIndex, DATA_STAT_INVALID);
#endif // (FS_NOR_SUPPORT_CRC != 0) || (FS_NOR_SUPPORT_ECC != 0)
  INIT_LSH_DATA_RANGE();
  return r;
}

#if (FS_NOR_CAN_REWRITE == 0)

/*********************************************************************
*
*        _MarkPhySectorAsInvalidNRWithECC
*
*  Function description
*    Marks a logical sector as invalid.
*
*  Parameters
*    pInst            Driver instance.
*    PhySectorIndex   Index of the physical sector that stores the logical sector.
*    pPSH             Header of the physical sector.
*
*  Additional information
*    This function assumes that the support for incremental write
*    operations is disabled (i.e. _IsRewriteSupported(pInst) == 0 is true)
*    The invalid data indicator is not protected by CRC but only by ECC.
*/
static int _MarkPhySectorAsInvalidNRWithECC(NOR_BM_INST * pInst, unsigned PhySectorIndex, NOR_BM_PSH * pPSH) {
  int r;

  INIT_LSH_DATA_RANGE();
  _SetPhySectorDataStat(pInst, pPSH, DATA_STAT_INVALID);
#if (FAIL_SAFE_ERASE_NO_REWRITE != 0) && (FS_NOR_SUPPORT_ECC != 0)
  r = _CalcStorePSH_NRWithECC(pInst, pPSH);
  if (r == 0)
#endif // (FAIL_SAFE_ERASE_NO_REWRITE != 0) && (FS_NOR_SUPPORT_ECC != 0)
  {
    r = _WritePSH(pInst, PhySectorIndex, pPSH);
  }
  INIT_LSH_DATA_RANGE();
  return r;
}

#endif // (FS_NOR_CAN_REWRITE == 0)

/*********************************************************************
*
*        _MarkPhySectorAsInvalidNC
*
*  Function description
*    Updates PSH to indicate that the data is invalid.
*
*  Parameters
*    pInst            Driver instance.
*    PhySectorIndex   Index of the physical sector.
*    pPSH             Header of the physical sector.
*
*  Return value
*    ==0    OK, data marked as invalid.
*    !=0    An error occurred.
*
*  Additional information
*    This function assumes that the checking via CRC and ECC are disabled.
*/
static int _MarkPhySectorAsInvalidNC(NOR_BM_INST * pInst, unsigned PhySectorIndex, NOR_BM_PSH * pPSH) {
  int r;

#if FS_NOR_CAN_REWRITE
  FS_USE_PARA(pPSH);
  r = _WritePhySectorDataStatFast(pInst, PhySectorIndex, DATA_STAT_INVALID);
#else
  if (_IsRewriteSupported(pInst) != 0) {
    r = _WritePhySectorDataStatFast(pInst, PhySectorIndex, DATA_STAT_INVALID);
  } else {
    INIT_PSH_DATA_RANGE();
    _SetPhySectorDataStat(pInst, pPSH, DATA_STAT_INVALID);
    r = _WritePSH(pInst, PhySectorIndex, pPSH);
    INIT_PSH_DATA_RANGE();
  }
#endif // FS_NOR_CAN_REWRITE
  return r;
}

/*********************************************************************
*
*        _MarkPhySectorAsInvalid
*
*  Function description
*    Updates PSH to indicate that the data is invalid.
*
*  Parameters
*    pInst            Driver instance.
*    PhySectorIndex   Index of the physical sector.
*    pPSH             Header of the physical sector.
*
*  Return value
*    ==0    OK, data marked as valid.
*    !=0    An error occurred.
*
*  Additional information
*    This function assumes that the checking via CRC and ECC are disabled.
*/
static int _MarkPhySectorAsInvalid(NOR_BM_INST * pInst, unsigned PhySectorIndex, NOR_BM_PSH * pPSH) {
  int r;

#if FS_NOR_CAN_REWRITE
  if ((_IsCRCEnabled(pInst) != 0) || (_IsECCEnabled(pInst) != 0)) {
    r = _MarkPhySectorAsInvalidWithCRCAndECC(pInst, PhySectorIndex, pPSH);
  } else {
    r = _MarkPhySectorAsInvalidNC(pInst, PhySectorIndex, pPSH);
  }
#else
  if ((_IsCRCEnabled(pInst) == 0) && (_IsECCEnabled(pInst) == 0)) {
    r = _MarkPhySectorAsInvalidNC(pInst, PhySectorIndex, pPSH);
  } else {
    if (_IsRewriteSupported(pInst) != 0) {
      r = _MarkPhySectorAsInvalidWithCRCAndECC(pInst, PhySectorIndex, pPSH);
    } else {
      r = _MarkPhySectorAsInvalidNRWithECC(pInst, PhySectorIndex, pPSH);
    }
  }
#endif // FS_NOR_CAN_REWRITE
  return r;
}

/*********************************************************************
*
*       _FreePhySector
*
*  Function description
*    Marks the data of a physical sector as invalid. The physical sector
*    is put in the in the list of free sectors.
*
*  Parameters
*    pInst            [IN] Driver instance.
*    PhySectorIndex   Index of the physical sector to be marked as free.
*
*  Return value
*    ==0    OK, the phy. sector has been marked as available.
*    !=0    A write error occurred, recoverable.
*/
static int _FreePhySector(NOR_BM_INST * pInst, unsigned PhySectorIndex) {
  int        r;
  NOR_BM_PSH psh;
  unsigned   DataStat;
  U32        EraseCnt;

  r = 0;
  //
  // Physical sector 0 stores only management information and can never be freed.
  //
  if (PhySectorIndex != 0u) {
    //
    // Mark physical sector as invalid and put it into the free list.
    //
    FS_MEMSET(&psh, 0xFF, sizeof(psh));
    r = _ReadPSH(pInst, PhySectorIndex, &psh);
    if (r == 0) {
      DataStat = _GetPhySectorDataStat(pInst, &psh);
      EraseCnt = _GetPhySectorEraseCnt(pInst, &psh);
      r = _MarkPhySectorAsInvalid(pInst, PhySectorIndex, &psh);
      if (DataStat == DATA_STAT_VALID) {
        if ((pInst->NumBlocksEraseCntMin != 0u) && (pInst->EraseCntMin == EraseCnt)) {
          pInst->NumBlocksEraseCntMin--;
        }
      }
    }
    _MarkPhySectorAsFree(pInst, PhySectorIndex);
    if (r != 0) {
      //
      // Error while invalidating the physical sector.
      // Erase it to make sure that the low-level mount
      // operation does not consider the data as valid.
      //
      (void)_PreErasePhySector(pInst, PhySectorIndex);
    }
#if FS_NOR_SUPPORT_CLEAN
    pInst->IsCleanPhySector = 0;
#endif // FS_NOR_SUPPORT_CLEAN
  }
  return r;
}

/*********************************************************************
*
*       _CopyLogSectorFast
*
*  Function description
*    Copies the data of a logical sector into another logical sector (fast version).
*
*  Return value
*    ==0                    OK, sector data has been copied.
*    ==RESULT_READ_ERROR    Error reading data.
*    ==RESULT_WRITE_ERROR   Error writing data.
*    ==RESULT_CRC_ERROR     CRC verification failed.
*    < 0                    OK, sector data is not valid.
*/
static int _CopyLogSectorFast(NOR_BM_INST * pInst, unsigned psiSrc, unsigned srsiSrc, unsigned psiDest, unsigned srsiDest, unsigned brsi) {
  int      r;
  unsigned DataStat;

  DataStat = DATA_STAT_INVALID;
  r = _ReadLogSectorDataStat(pInst, psiSrc, srsiSrc, &DataStat);
  if (r == 0) {
    r = -1;       // Set to indicate that the sector data is not valid.
    if (DataStat == DATA_STAT_VALID) {
      //
      // Copy the sector data.
      //
      r = _CopyLogSectorData(pInst, psiSrc, srsiSrc, psiDest, srsiDest);
      if (r == 0) {
        //
        // Fail-safe TP. At this point the sector data is written but the header is not updated yet.
        //
        CALL_TEST_HOOK_FAIL_SAFE(pInst->Unit);

        //
        // The destination sector contains now valid data therefore
        // we mark the logical sector as valid. The index of the logical
        // sector relative to phy. sector is also set.
        //
        r = _MarkLogSectorAsValidNC(pInst, psiDest, srsiDest, brsi);
      }
    }
  }
  return r;
}

#if (FS_NOR_SUPPORT_CRC != 0) || (FS_NOR_SUPPORT_ECC != 0)

/*********************************************************************
*
*       _CopyLogSectorWithCRCAndECC
*
*  Function description
*    Copies the data of a logical sector into another logical sector
*    using CRC and ECC.
*
*  Return value
*    ==0                    OK, sector data has been copied.
*    ==RESULT_READ_ERROR    Error reading data.
*    ==RESULT_WRITE_ERROR   Error writing data.
*    ==RESULT_CRC_ERROR     CRC verification failed.
*    ==RESULT_ECC_ERROR     ECC verification failed.
*    < 0                    OK, sector data is not valid.
*
*  Additional information
*    This function assumes that either ECC or CRC (or both)
*    is enabled at compile time as well at runtime.
*/
static int _CopyLogSectorWithCRCAndECC(NOR_BM_INST * pInst, unsigned psiSrc, unsigned srsiSrc, unsigned psiDest, unsigned srsiDest, unsigned brsi) {
  int        r;
  unsigned   DataStat;
  NOR_BM_LSH lsh;

  FS_MEMSET(&lsh, 0xFF, sizeof(lsh));
  r = _ReadLSH(pInst, psiSrc, srsiSrc, &lsh);
  if (r == 0) {
    r = -1;       // Set to indicate that the sector data is not valid.
    DataStat = _GetLogSectorDataStat(pInst, &lsh);
    if (DataStat == DATA_STAT_VALID) {
      //
      // Copy the sector data.
      //
#if (FS_NOR_SUPPORT_CRC != 0) && (FS_NOR_SUPPORT_ECC != 0)
      if ((_IsCRCEnabled(pInst) != 0) && (_IsECCEnabled(pInst) != 0)) {
        r = _CopyLogSectorDataWithCRCAndECC(pInst, psiSrc, srsiSrc, psiDest, srsiDest, &lsh);
      } else {
        if ((_IsCRCEnabled(pInst) != 0) && (_IsECCEnabled(pInst) == 0)) {
          r = _CopyLogSectorDataWithCRC(pInst, psiSrc, srsiSrc, psiDest, srsiDest, &lsh);
        } else {
          if ((_IsCRCEnabled(pInst) == 0) && (_IsECCEnabled(pInst) != 0)) {
            r = _CopyLogSectorDataWithECC(pInst, psiSrc, srsiSrc, psiDest, srsiDest, &lsh);
          }
        }
      }
#elif (FS_NOR_SUPPORT_CRC != 0) && (FS_NOR_SUPPORT_ECC == 0)
      if (_IsCRCEnabled(pInst) != 0) {
        r = _CopyLogSectorDataWithCRC(pInst, psiSrc, srsiSrc, psiDest, srsiDest, &lsh);
      }
#elif (FS_NOR_SUPPORT_CRC == 0) && (FS_NOR_SUPPORT_ECC != 0)
      if (_IsECCEnabled(pInst) != 0) {
        r = _CopyLogSectorDataWithECC(pInst, psiSrc, srsiSrc, psiDest, srsiDest, &lsh);
      }
#endif // FS_NOR_SUPPORT_CRC != 0 && FS_NOR_SUPPORT_ECC != 0
      if (r == 0) {

        //
        // Fail-safe TP. At this point the sector data is written but the header has not been updated yet.
        //
        CALL_TEST_HOOK_FAIL_SAFE(pInst->Unit);

        //
        // The destination sector contains now valid data.
        // We have to set the index of the logical sector relative to phy. sector
        // for the case that we clean a work block "in place" so that
        // in case of a power failure the sector is marked as in use.
        //
        INIT_LSH_DATA_RANGE();
        //
        // Make sure that we always update the first flash line of the logical sector header.
        // This flash line contains information about the data stored in the logical sector.
        //
        UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_LSH, DataStat), sizeof(lsh.DataStat));    // First member of NOR_BM_LSH
#if FS_NOR_SUPPORT_ECC
        UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_LSH, ecc1Stat), sizeof(lsh.ecc1Stat));    // Last member of NOR_BM_LSH
#else
        UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_LSH, crcStat), sizeof(lsh.crcStat));      // Last member of NOR_BM_LSH
#endif // FS_NOR_SUPPORT_ECC
        if (brsi != BRSI_INVALID) {
          if (lsh.brsi != (U16)brsi) {
            lsh.brsi = (U16)brsi;
#if (FS_NOR_SUPPORT_CRC != 0) && (FS_NOR_SUPPORT_ECC != 0)
            if ((_IsCRCEnabled(pInst) != 0) && (_IsECCEnabled(pInst) != 0)) {
              int NumBitsCorrected;

              r = _pCRC_API->pfCalcStoreLSH(&lsh);
              if (r == 0) {
                NumBitsCorrected = 0;
                r = pInst->pECC_API->pfCalcStoreLSH(pInst, &lsh, &NumBitsCorrected);
                UPDATE_NUM_BIT_ERRORS(pInst, NumBitsCorrected);
              }
            } else {
              if ((_IsCRCEnabled(pInst) != 0) && (_IsECCEnabled(pInst) == 0)) {
                r = _pCRC_API->pfCalcStoreLSH(&lsh);
              } else {
                if ((_IsCRCEnabled(pInst) == 0) && (_IsECCEnabled(pInst) != 0)) {
                  int NumBitsCorrected;

                  NumBitsCorrected = 0;
                  r = pInst->pECC_API->pfCalcStoreLSH(pInst, &lsh, &NumBitsCorrected);
                  UPDATE_NUM_BIT_ERRORS(pInst, NumBitsCorrected);
                }
              }
            }
#elif (FS_NOR_SUPPORT_CRC != 0) && (FS_NOR_SUPPORT_ECC == 0)
            if (_IsCRCEnabled(pInst) != 0) {
              r = _pCRC_API->pfCalcStoreLSH(&lsh);
            }
#elif (FS_NOR_SUPPORT_CRC == 0) && (FS_NOR_SUPPORT_ECC != 0)
            if (_IsECCEnabled(pInst) != 0) {
              int NumBitsCorrected;

              NumBitsCorrected = 0;
              r = pInst->pECC_API->pfCalcStoreLSH(pInst, &lsh, &NumBitsCorrected);
              UPDATE_NUM_BIT_ERRORS(pInst, NumBitsCorrected);
            }
#endif // FS_NOR_SUPPORT_CRC != 0 && FS_NOR_SUPPORT_ECC != 0
          }
        }
        if (r == 0) {
          //
          // Make sure that the IsValid member of NOR_BM_LSH is also updated.
          // The IsInvalid member of NOR_BM_LSH does not have to be written
          // because we never copy invalid sectors.
          //
          _SetLogSectorDataStat(pInst, &lsh, DATA_STAT_VALID);
          r = _WriteLSH(pInst, psiDest, srsiDest, &lsh);
        }
        INIT_LSH_DATA_RANGE();
      }
    }
  }
  return r;
}

#endif // FS_NOR_SUPPORT_CRC != 0 || FS_NOR_SUPPORT_ECC != 0

/*********************************************************************
*
*       _CopyLogSector
*
*  Function description
*    Copies the data of a logical sector into another logical sector.
*
*  Parameters
*    pInst      [IN]  Driver instance.
*    psiSrc     Source physical sector index.
*    srsiSrc    Source sector relative sector index.
*    psiDest    Destination physical sector index.
*    srsiDest   Destination sector relative sector index.
*    brsi       Block relative index of the copied sector.
*
*  Return value
*    ==0    OK, sector header and data copied to destination.
*    !=0    An error occurred.
*/
static int _CopyLogSector(NOR_BM_INST * pInst, unsigned psiSrc, unsigned srsiSrc, unsigned psiDest, unsigned srsiDest, unsigned brsi) {
  int r;

#if (FS_NOR_SUPPORT_CRC != 0) || (FS_NOR_SUPPORT_ECC != 0)
  {
    int CopyFast;

    CopyFast = 1;
#if FS_NOR_SUPPORT_CRC
    if (_IsCRCEnabled(pInst) != 0) {
      CopyFast = 0;
    }
#endif // FS_NOR_SUPPORT_CRC
#if FS_NOR_SUPPORT_ECC
    if (_IsECCEnabled(pInst) != 0) {
      CopyFast = 0;
    }
#endif // FS_NOR_SUPPORT_ECC
    if (CopyFast == 0) {
      r = _CopyLogSectorWithCRCAndECC(pInst, psiSrc, srsiSrc, psiDest, srsiDest, brsi);
    } else {
      r = _CopyLogSectorFast(pInst, psiSrc, srsiSrc, psiDest, srsiDest, brsi);
    }
  }
#else
  r = _CopyLogSectorFast(pInst, psiSrc, srsiSrc, psiDest, srsiDest, brsi);
#endif // FS_NOR_SUPPORT_CRC !=0 || FS_NOR_SUPPORT_ECC != 0
  if (r < 0) {
    r = 0;            // The sector data is not valid.
  } else {
    //
    // OK, logical sector copied.
    //
    IF_STATS(pInst->StatCounters.CopySectorCnt++);
    FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR_BM: COPY_LOG_SECTOR psiSrc: %u, srsiSrc: %lu, psiDest: %u, srsiDest: %u, r: %d\n", psiSrc, srsiSrc, psiDest, srsiDest, r));
  }
  return r;
}

/*********************************************************************
*
*       _CountDataBlocksWithEraseCntMin
*
*  Function description
*    Walks through all blocks and counts the data blocks with the lowest erase count.
*
*  Parameters
*    pInst        [IN]  Driver instance.
*    pEraseCnt    [OUT] Minimum erase count.
*    pPSI         [OUT] Index of the first physical sector with the minimum erase count.
*
*  Return value
*    Number of data sectors found with a minimum erase count.
*/
static U32 _CountDataBlocksWithEraseCntMin(NOR_BM_INST * pInst, U32 * pEraseCnt, unsigned * pPSI) {
  unsigned iSector;
  unsigned psi;
  U32      EraseCntMin;
  U32      EraseCnt;
  U32      NumPhySectors;
  int      r;

  psi           = 0;
  EraseCntMin   = ERASE_CNT_INVALID;
  NumPhySectors = 0;
  //
  // Read and compare the erase count of all data blocks except the first one
  // which stores only management information
  //
  for (iSector = PSI_FIRST_STORAGE_BLOCK; iSector < pInst->NumPhySectors; ++iSector) {
    unsigned   DataStat;
    NOR_BM_PSH psh;

    FS_MEMSET(&psh, 0xFF, sizeof(psh));
    r = _ReadPSH(pInst, iSector, &psh);
    if (r == 0) {
      DataStat = _GetPhySectorDataStat(pInst, &psh);
      //
      // Only physical sectors that store valid data are checked
      //
      if (DataStat == DATA_STAT_VALID) {
        EraseCnt = _GetPhySectorEraseCnt(pInst, &psh);
        if ((EraseCntMin == ERASE_CNT_INVALID) || (EraseCnt < EraseCntMin)) {
          psi           = iSector;
          EraseCntMin   = EraseCnt;
          NumPhySectors = 1;
        } else {
          if (EraseCnt == EraseCntMin) {
            ++NumPhySectors;
          }
        }
      }
    }
  }
  *pEraseCnt = EraseCntMin;
  *pPSI      = psi;
  return NumPhySectors;
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
*    ==0    No data block found
*    !=0    Physical sector index of the found data block
*/
static unsigned _FindDataBlockByEraseCnt(NOR_BM_INST * pInst, U32 EraseCnt) {
  unsigned   iSector;
  int        r;
  NOR_BM_PSH psh;
  unsigned   DataStat;
  U32        EraseCntData;

  //
  // Read and compare the erase count of all data blocks except the first one
  // which stores only management information
  //
  for (iSector = PSI_FIRST_STORAGE_BLOCK; iSector < pInst->NumPhySectors; ++iSector) {
    r = _ReadPSH(pInst, iSector, &psh);
    if (r == 0) {
      DataStat = _GetPhySectorDataStat(pInst, &psh);
      //
      // Only physical sectors that store valid data are checked
      //
      if (DataStat == DATA_STAT_VALID) {
        EraseCntData = _GetPhySectorEraseCnt(pInst, &psh);
        if (EraseCnt == EraseCntData) {
          return iSector;
        }
      }
    }
  }
  return 0;     // No data sector found with the requested erase count
}

/*********************************************************************
*
*       _CheckActiveWearLeveling
*
*  Function description
*    Checks if it is time to perform active wear leveling by
*    comparing the given erase count to the lowest erase count.
*    If so (difference is too big), the index of the data block with the lowest erase count is returned.
*
*  Parameters
*    pInst            [IN]  Driver instance.
*    EraseCnt         Erase count of physical sector selected to be erased.
*    pDataEraseCnt    [OUT] Erase count of the data block which must be erased. Can be NULL.
*
*  Return value
*    ==0    No data block found.
*    !=0    Physical sector index of the data block found.
*/
static unsigned _CheckActiveWearLeveling(NOR_BM_INST * pInst, U32 EraseCnt, U32 * pDataEraseCnt) {
  unsigned psi;
  I32      EraseCntDiff;
  U32      NumBlocks;
  U32      EraseCntMin;

  //
  // Update pInst->EraseCntMin if necessary.
  //
  psi         = 0;
  NumBlocks   = pInst->NumBlocksEraseCntMin;
  EraseCntMin = pInst->EraseCntMin;
  if (NumBlocks == 0u) {
    NumBlocks = _CountDataBlocksWithEraseCntMin(pInst, &EraseCntMin, &psi);
    if (NumBlocks == 0u) {
      return 0;     // We don't have any data block yet, it can happen if the flash is empty.
    }
    pInst->EraseCntMin          = EraseCntMin;
    pInst->NumBlocksEraseCntMin = NumBlocks;
  }
  //
  // Check if the threshold for active wear leveling has been reached.
  //
  EraseCntDiff = (I32)EraseCnt - (I32)EraseCntMin;
  if (EraseCntDiff < (I32)pInst->MaxEraseCntDiff) {
    return 0;       // Active wear leveling not necessary, EraseCntDiff is not large enough.
  }
  if (psi == 0u) {
    psi = _FindDataBlockByEraseCnt(pInst, EraseCntMin);
  }
  if (pDataEraseCnt != NULL) {
    *pDataEraseCnt = EraseCntMin;
  }
  --pInst->NumBlocksEraseCntMin;
  return psi;
}

/*********************************************************************
*
*       _PerformPassiveWearLeveling
*
*  Function description
*    Searches for the next free physical sector and returns its index.
*    The sector is marked as allocated in the internal list.
*
*  Parameters
*    pInst              [IN]  Driver instance.
*    pPSH               [OUT] Phy. sector header. Can not be NULL.
*    pIsPhySectorEmpty  [OUT] Set to 1 if the selected phy. sector
*                       can be directly used for data storage
*                       (i.e. without erasing it first).
*
*  Return value
*    ==0    No more free blocks
*    !=0    Physical sector index of the allocated block
*/
static unsigned _PerformPassiveWearLeveling(NOR_BM_INST * pInst, NOR_BM_PSH * pPSH, U8 * pIsPhySectorEmpty) {
  unsigned i;
  unsigned iSector;
  int      r;
  int      IsPhySectorEmpty;

  //
  // Search for an empty physical sector if the wear leveling has been disabled by the application.
  //
  if (pInst->IsWLSuspended != 0u) {
    iSector = pInst->MRUFreeBlock;
    for (i = 0; i < pInst->NumPhySectors; i++) {
      if (++iSector >= pInst->NumPhySectors) {
        iSector = PSI_FIRST_STORAGE_BLOCK;
      }
      if (_IsPhySectorFree(pInst, iSector) != 0) {
        r = _ReadPSH(pInst, iSector, pPSH);
        if (r == 0) {
          IsPhySectorEmpty = _IsPhySectorEmpty(pInst, iSector, pPSH);
          if (IsPhySectorEmpty != 0) {
            _MarkPhySectorAsAllocated(pInst, iSector);
            pInst->MRUFreeBlock = iSector;
            if (pIsPhySectorEmpty != NULL) {
              *pIsPhySectorEmpty = (U8)IsPhySectorEmpty;
            }
            return iSector;                 // We found an empty phy. sector.
          }
        }
      }
    }
    //
    // Re-enable the wear leveling if we did not find any empty physical sector.
    //
    pInst->IsWLSuspended = 0;
  }
  //
  // We did not find any empty physical sector or the wear leveling is enabled.
  // Search for a free physical sector.
  //
  iSector = pInst->MRUFreeBlock;
  for (i = 0; i < pInst->NumPhySectors; i++) {
    if (++iSector >= pInst->NumPhySectors) {
      iSector = PSI_FIRST_STORAGE_BLOCK;
    }
    if (_IsPhySectorFree(pInst, iSector) != 0) {
      r = _ReadPSH(pInst, iSector, pPSH);
      _MarkPhySectorAsAllocated(pInst, iSector);
      pInst->MRUFreeBlock = iSector;
      if (pIsPhySectorEmpty != NULL) {
        IsPhySectorEmpty = 0;
        if (r == 0) {
          IsPhySectorEmpty = _IsPhySectorEmpty(pInst, iSector, pPSH);
        }
        *pIsPhySectorEmpty = (U8)IsPhySectorEmpty;
        return iSector;                     // We found a free phy. sector.
      }
    }
  }
  FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR_BM: _PerformPassiveWearLeveling: No more free physical sectors."));
  return 0;               // Error, no more free physical sectors
}

/*********************************************************************
*
*       _RemoveDataBlock
*
*  Function description
*    Removes the data block from internal list and from the mapping table.
*
*   Parameters
*     pInst       Driver instance.
*     lbi         Index of the logical block to be removed.
*
*  Return value
*    ==0        Data block found and removed.
*    !=0        Data block not found.
*/
static int _RemoveDataBlock(NOR_BM_INST * pInst, unsigned lbi) {
  unsigned psi;
  int      r;

  r   = 1;                        // Set to indicate that the data block was not found.
  psi = _L2P_Read(pInst, lbi);
  if (psi != 0u) {
    _L2P_Write(pInst, lbi, 0);    // Remove the logical block from the mapping table.
#if FS_NOR_OPTIMIZE_DATA_WRITE
    {
      NOR_BM_DATA_BLOCK * pDataBlock;

      //
      // Remove the data block from the list of used data blocks.
      //
      pDataBlock = pInst->pFirstDataBlockInUse;
      while (pDataBlock != NULL) {
        if (pDataBlock->psi == psi) {
          _DB_RemoveFromUsedList(pInst, pDataBlock);
          _DB_AddToFreeList(pInst, pDataBlock);
          break;
        }
        pDataBlock = pDataBlock->pNext;
      }
    }
#endif // FS_NOR_OPTIMIZE_DATA_WRITE
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
*    pInst        [IN]  Driver instance.
*    psiSrc       Index of the physical sector to be copied.
*    psiDest      Index of the physical sector where to copy.
*    EraseCnt     Erase count of the source physical sector.
*
*  Return value
*    ==0      OK
*    !=0      An error occurred.
*/
static int _MoveDataBlock(NOR_BM_INST * pInst, unsigned psiSrc, unsigned psiDest, U32 EraseCnt) {
  unsigned NumSectors;
  unsigned iSector;
  int      r;
  unsigned lbi;
  unsigned psi;
  U8       DataCnt;
  int      SkipCheck;

  NumSectors = pInst->LSectorsPerPSector;
  //
  // Get the logical block index of the copied sector.
  // We do this by searching table that translates a logical block to physical sector.
  //
  for (lbi = 0; lbi < pInst->NumLogBlocks; ++lbi) {
    psi = FS_BITFIELD_ReadEntry(pInst->pLog2PhyTable, lbi, pInst->NumBitsPhySectorIndex);
    if (psi == psiSrc) {
      break;
    }
  }
  //
  // This optimization is not supported for NOR flash devices that cannot rewrite
  // since the operation that marks the block as valid writes the second time
  // to the same flash line.
  //
#if FS_NOR_CAN_REWRITE
  //
  // Set the index of the logical block but do not mark the phy. sector as valid.
  // If the copy operation is interrupted by an unexpected reset we end up
  // with an invalid data block which has a valid LBI. We use this information
  // to determine if the block has to be erased before use.
  //
  r = _UpdatePSH(pInst, psiDest, lbi, ERASE_CNT_INVALID, DATA_STAT_EMPTY, DATA_CNT_INVALID, 0);
  if (r != 0) {
    return r;                 // Error, could not update the phy. sector header.
  }

  //
  // Fail-safe TP. At this point we have an invalid data block with a valid logical block index.
  //
  CALL_TEST_HOOK_FAIL_SAFE(pInst->Unit);
#endif // FS_NOR_CAN_REWRITE
  for (iSector = 0; iSector < NumSectors; ++iSector) {
    r = _CopyLogSector(pInst, psiSrc, iSector, psiDest, iSector, BRSI_INVALID);
    if (r != 0) {
      return r;
    }
  }
  //
  // Get the data count form the source data block.
  //
  DataCnt = 0;
  (void)_ReadPhySectorDataCnt(pInst, psiSrc, &DataCnt);
  ++DataCnt;
  //
  // With CRC or ECC enabled reading the phy. sector in _MarkAsDataBlock() will
  // result in an error since _UpdatePSH() did not update the CRC or ECC value.
  // In this case the we do not check the validity of the data.
  //
  SkipCheck = 0;
  if (_IsCRCEnabled(pInst) != 0) {
    SkipCheck = 1;
  }
  if (_IsECCEnabled(pInst) != 0) {
    SkipCheck = 1;
  }
  //
  // Copy operation finished. Mark the phy. sector as data block.
  //
  r = _MarkAsDataBlock(pInst, psiDest, lbi, EraseCnt, DataCnt, SkipCheck);
  if (r != 0) {
    return r;                 // Error, could not mark phy. sector as data block.
  }
  //
  // Update the mapping of physical sectors to logical blocks.
  //
  (void)_RemoveDataBlock(pInst, lbi);
  _L2P_Write(pInst, lbi, psiDest);

  //
  // Fail-safe TP. At this point we have two data blocks with the same LBI
  //
  CALL_TEST_HOOK_FAIL_SAFE(pInst->Unit);

  (void)_FreePhySector(pInst, psiSrc);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR_BM: MOVE_DATA_BLOCK psiSrc: %u, psiDest: %u, r: %d\n", psiSrc, psiDest, r));
  return r;
}

/*********************************************************************
*
*       _AllocErasedBlock
*
*  Function description
*    Selects a block to write data into. The physical sector is erased.
*
*  Parameters
*    pInst        [IN]  Driver instance.
*    pEraseCnt    [OUT] Erase count of the selected physical sector. Can be NULL.
*
*  Return value
*    ==0    An error occurred.
*    !=0    Physical sector index.
*/
static unsigned _AllocErasedBlock(NOR_BM_INST * pInst, U32 * pEraseCnt) {
  unsigned   psiFree;
  unsigned   psiData;
  U32        EraseCntFree;
  U32        EraseCntData;
  int        r;
  NOR_BM_PSH psh;
  U8         IsPhySectorEmpty;
  unsigned   NumRetries;
  unsigned   NumPhySectors;

  //
  // Loop until we can allocate an empty phy. sector.
  //
  NumRetries    = 0;
  NumPhySectors = pInst->NumPhySectors;
  for (;;) {
    if (NumRetries++ > (unsigned)FS_NOR_NUM_ERASE_RETRIES) {
      if (NumRetries > NumPhySectors) {
        //
        // Raise a fatal error if we tried with all the physical sectors.
        //
        _OnFatalError(pInst, RESULT_OUT_OF_FREE_SECTORS, 0);
      }
      return 0;                                   // Error, too many retries.
    }
    //
    // Passive wear leveling. Get the next free physical sector in the row.
    //
    FS_MEMSET(&psh, 0, sizeof(psh));
    IsPhySectorEmpty = 0;
    psiFree = _PerformPassiveWearLeveling(pInst, &psh, &IsPhySectorEmpty);
    if (psiFree == 0u) {
      _OnFatalError(pInst, RESULT_OUT_OF_FREE_SECTORS, 0);
      return 0;                                   // Fatal error, no more free physical sectors.
    }
    //
    // Erase the physical sector if required.
    //
    EraseCntFree = _GetPhySectorEraseCnt(pInst, &psh);
    if (IsPhySectorEmpty == 0u) {
      r = ERASE_PHY_SECTOR(pInst, psiFree, &EraseCntFree);
      if (r != 0) {
        _MarkPhySectorAsFree(pInst, psiFree);
        continue;                                 // Erasing of physical sector failed. Try to find another one.
      }
    } else {
      //
      // Do not perform active wear leveling if the physical sector is blank.
      // This sector has been erased via a clean operation that is taking care
      // of performing the active wear leveling.
      //
      if (pEraseCnt != NULL) {
        *pEraseCnt = EraseCntFree;
      }
      return psiFree;
    }
    //
    // OK, we erased a free physical sector.
    // Check if the erase count is too high so we need to use active wear leveling.
    //
    psiData = _CheckActiveWearLeveling(pInst, EraseCntFree, &EraseCntData);
    if (psiData == 0u) {
      if (pEraseCnt != NULL) {
        *pEraseCnt = EraseCntFree;                // No data block has an erase count low enough. Keep the physical sector allocated by passive wear leveling
      }
      return psiFree;
    }
    //
    // Perform active wear leveling.
    // A block containing data has a much lower erase count. This block is now moved, giving us a free block with low erase count.
    // This procedure makes sure that blocks which contain data that does not change are evenly erased.
    //
    r = _MoveDataBlock(pInst, psiData, psiFree, EraseCntFree);
    if (r != 0) {
      _MarkPhySectorAsFree(pInst, psiFree);
      continue;                                   // Error, could not relocate data block. Try to find another empty physical sector.
    }
    //
    // The data has been moved and the data block is now free to use.
    //
    r = ERASE_PHY_SECTOR(pInst, psiData, &EraseCntData);
    if (r != 0) {
      continue;                                   // Erasing of physical sector failed. Try to find another one.
    }
    _MarkPhySectorAsAllocated(pInst, psiData);    // This is required since _MoveDataBlock() marks the psiData block as available.
    if (pEraseCnt != NULL) {
      *pEraseCnt = EraseCntData;
    }
    return psiData;
  }
}

/*********************************************************************
*
*       _ConvertWorkBlockViaCopy
*
*  Function description
*    Converts a work block into a data block.
*
*  Parameters
*    pInst        Driver instance.
*    pWorkBlock   Work block to convert.
*
*  Return value
*    ==0    OK, work block converted successfully.
*    !=0    An error occurred.
*
*  Additional information
*    The data of the work block is merged with the data of the source block
*    and the result is stored into another free block.
*    The merging operation copies sector-wise the data from work block
*    into the free block. If the sector data is invalid in the work block
*    the sector data from the source block is copied instead. The sectors
*    in the work block doesn't have to be on their native positions.
*/
static int _ConvertWorkBlockViaCopy(NOR_BM_INST * pInst, NOR_BM_WORK_BLOCK * pWorkBlock) {
  unsigned iSector;
  unsigned psiSrc;
  unsigned psiWork;
  unsigned psiDest;
  unsigned NumSectors;
  unsigned srsi;
  unsigned lbiWork;
  U8       DataCnt;
  U32      EraseCntDest;
  int      r;
  int      FatalError;
  int      NumRetries;
  int      SkipCheck;

  psiWork     = pWorkBlock->psi;
  lbiWork     = pWorkBlock->lbi;
  NumSectors  = pInst->LSectorsPerPSector;
  NumRetries  = 0;
  //
  // Loop until the block is converted or a fatal error occurs.
  //
  for (;;) {
    if (NumRetries++ > FS_NOR_NUM_WRITE_RETRIES) {
      return 1;                                 // Error, too many write retries.
    }
    DataCnt      = 0;
    EraseCntDest = ERASE_CNT_INVALID;
    FatalError   = 0;
    //
    // We need to allocate a new block to copy data into
    //
    psiDest = _AllocErasedBlock(pInst, &EraseCntDest);
    if (psiDest == 0u) {
      return RESULT_OUT_OF_FREE_SECTORS;                // Error, no more free physical sectors
    }
    //
    // OK, we have an empty block to copy our data into
    //
    psiSrc = _L2P_Read(pInst, lbiWork);
    //
    // This optimization is not supported for NOR flash devices that cannot rewrite
    // since this operation that marks the block as valid writes the second time
    // to the same flash line.
    //
#if (FS_NOR_CAN_REWRITE == 0)
    if (_IsRewriteSupported(pInst) != 0)
#endif // FS_NOR_CAN_REWRITE
    {
      //
      // Set the index of the logical block but do not mark the phy. sector as valid.
      // If the copy operation is interrupted by an unexpected reset we end up
      // with an invalid data block which has a valid LBI. We use this information
      // to determine if the block has to be erased before use.
      // _UpdatePSH() does not calculate and store the CRC if only the LBI is written.
      //
      r = _UpdatePSH(pInst, psiDest, lbiWork, ERASE_CNT_INVALID, DATA_STAT_EMPTY, DATA_CNT_INVALID, 0);
      if (r != 0) {
        (void)_PreErasePhySector(pInst, psiDest);
        continue;               // Error, could not update the phy. sector header.
      }

      //
      // Fail-safe TP. At this point we have an invalid data block with a valid logical block index.
      //
      CALL_TEST_HOOK_FAIL_SAFE(pInst->Unit);
    }
    r = 0;
    //
    // Copy the data sector by sector.
    //
    for (iSector = 0; iSector < NumSectors; iSector++) {
      //
      // If data is not in work block, take it from source block.
      //
      srsi = _brsi2srsi(pInst, pWorkBlock, iSector);
      if (srsi != BRSI_INVALID) { // In work block ?
        r = _CopyLogSector(pInst, psiWork, srsi, psiDest, iSector, BRSI_INVALID);
        if (r != 0) {
          //
          // The copy operation failed. Try again with another phy. sector.
          //
          if (r == RESULT_WRITE_ERROR) {
            (void)_PreErasePhySector(pInst, psiDest);
            break;                // Error, could not write.
          }
          _OnFatalError(pInst, r, psiDest);
          if (FatalError == 0) {
            FatalError = r;       // Error, while copying the data. We cannot recover from this error.
          }
        }
      } else {
        if (psiSrc != 0u) {       // In source block ?
          //
          // Copy if we have a data source.
          //
          r = _CopyLogSector(pInst, psiSrc, iSector, psiDest, iSector, BRSI_INVALID);
          if (r != 0) {
            //
            // The copy operation failed. Try again with another phy. sector.
            //
            if (r == RESULT_WRITE_ERROR) {
              (void)_PreErasePhySector(pInst, psiDest);
              break;              // Error, could not write. Try to write to another phy. sector.
            }
            _OnFatalError(pInst, r, psiDest);
            if (FatalError == 0) {
              FatalError = r;       // Error, while copying the data. We cannot recover from this error.
            }
          }
        }
      }
    }
    //
    // Perform error recovery for the case that the write operation failed.
    //
    if (r == RESULT_WRITE_ERROR) {
      continue;                   // Try to write to another phy. sector.
    }
    if (psiSrc != 0u) {
      DataCnt = 0;
      (void)_ReadPhySectorDataCnt(pInst, psiSrc, &DataCnt);
      DataCnt++;
    }
    //
    // With CRC or ECC enabled reading the phy. sector in _MarkAsDataBlock() will
    // result in an error since _UpdatePSH() did not update the CRC or ECC value.
    // In this case the we do not check the validity of the data.
    //
    SkipCheck = 0;
    if (_IsCRCEnabled(pInst) != 0) {
      SkipCheck = 1;
    }
    if (_IsECCEnabled(pInst) != 0) {
      SkipCheck = 1;
    }
    //
    // Mark the newly allocated block as data block.
    //
    r = _MarkAsDataBlock(pInst, psiDest, lbiWork, EraseCntDest, DataCnt, SkipCheck);
    if (r != 0) {
      (void)_PreErasePhySector(pInst, psiDest);
      continue;                   // Error, could not write. Try to write to another phy. sector.
    }
    break;
  }

  //
  // Fail-safe TP. At this point we have two data blocks with the same LBI.
  //
  CALL_TEST_HOOK_FAIL_SAFE(pInst->Unit);

  //
  // Update the mapping of physical sectors to logical blocks
  //
  if (psiSrc != 0u) {
    (void)_RemoveDataBlock(pInst, lbiWork);
  }
  _L2P_Write(pInst, lbiWork, psiDest);
  //
  // Mark former work block as invalid and put it in the free list.
  //
  (void)_FreePhySector(pInst, psiWork);
  //
  // Change data status of block which contained the "old" data
  // as invalid and put it to the free list.
  //
  (void)_FreePhySector(pInst, psiSrc);
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
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR_BM: CONV_WORK_BLOCK_VIA_COPY psiWork: %u, psiSrc: %u, psiDest: %u, LBI: %u, FatalError: %d.\n", psiWork, psiSrc, psiDest, lbiWork, FatalError));
  return FatalError;
}

/*********************************************************************
*
*       _ConvertWorkBlockInPlace
*
*  Function description
*    Converts a work block into a data block. It assumes that
*    the sectors are on their native positions. The missing sectors
*    are copied from the source block into the work block.
*
*  Parameters
*    pInst        [IN]  Driver instance.
*    pWorkBlock   [IN]  Work block to convert.
*
*  Return value
*    ==0    OK
*    !=0    An error occurred.
*/
static int _ConvertWorkBlockInPlace(NOR_BM_INST * pInst, NOR_BM_WORK_BLOCK * pWorkBlock) {
  unsigned iSector;
  unsigned psiData;
  unsigned psiWork;
  unsigned NumSectors;
  int      r;
  unsigned brsi;
  unsigned lbiWork;
  U8       DataCnt;

  brsi       = BRSI_INVALID;
  DataCnt    = 0;
  lbiWork    = pWorkBlock->lbi;
  psiData    = _L2P_Read(pInst, lbiWork);
  psiWork    = pWorkBlock->psi;
  NumSectors = pInst->LSectorsPerPSector;
  //
  // If there is a source block, then we use it to "fill the gaps", by copying data
  // from the source block to the logical sectors that are empty in the work block.
  //
  if (psiData != 0u) {
    for (iSector = 0; iSector < NumSectors; iSector++) {
      if (_WB_IsSectorUsed(pWorkBlock, iSector) == 0) {
        if (iSector != 0u) {
          brsi = iSector;
        }
        r = _CopyLogSector(pInst, psiData, iSector, psiWork, iSector, brsi);
        if (r != 0) {
          _OnFatalError(pInst, r, psiWork);
          return r;                                   // Error, could not copy logical sector
        }
      }
    }
    // TBD: It is not necessary to read DataCnt if the NOR flash device cannot rewrite
    //      because DataCnt is set to the correct value when the work block is allocated.
    (void)_ReadPhySectorDataCnt(pInst, psiData, &DataCnt);
    DataCnt++;
  }
  //
  // Convert work block into valid data block by changing the data status.
  //
  r = _MarkAsDataBlock(pInst, psiWork, lbiWork, ERASE_CNT_INVALID, DataCnt, 0);
  if (r != 0) {
    _OnFatalError(pInst, r, psiWork);
  } else {

    //
    // Fail-safe TP. At this point we have two data blocks with the same logical block index
    //
    CALL_TEST_HOOK_FAIL_SAFE(pInst->Unit);

    //
    // Converted work block is now data block, update Log2Phy table here.
    //
    if (psiData != 0u) {                            // Is there any old data sector that has to be freed?
      (void)_RemoveDataBlock(pInst, lbiWork);
    }
    _L2P_Write(pInst, lbiWork, psiWork);
    //
    // Change data status of block that contains the "old" data
    // to invalid and put it in the free list.
    //
    if (psiData != 0u) {                            // Is there any old data sector that has to be freed?
      (void)_FreePhySector(pInst, psiData);
    }
    //
    // Remove the work block from the internal list
    //
    _WB_RemoveFromUsedList(pInst, pWorkBlock);
    _WB_AddToFreeList(pInst, pWorkBlock);
    IF_STATS(pInst->StatCounters.ConvertInPlaceCnt++);
  }
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR_BM: CONV_WORK_BLOCK_IN_PLACE psiWork: %u, psiData: %u, LBI: %u, r: %d\n", psiWork, psiData, lbiWork, r));
  return r;
}

/*********************************************************************
*
*       _IsInPlaceConversionAllowed
*
*  Function description
*    Checks if a work block can be converted.
*    This is the case if all written sectors are in the right place.
*
*  Parameters
*    pInst        Driver instance.
*    pWorkBlock   Descriptor of the work block to be checked.
*    pPSIData     [OUT] Index of the physical block assigned to the
*                 work block associated with the work block. Can be NULL.
*
*  Return value
*    ==0      Found sectors with valid data not on their native positions.
*    ==1      All the sectors having valid data found on their native positions.
*
*  Additional information
*    *pPSIData is set to 0 if no data block is associated with the work block.
*    This information is used by the clean operation in order to calculate
*    how many clean steps are still required in order to have a clean storage.
*/
static int _IsInPlaceConversionAllowed(NOR_BM_INST * pInst, const NOR_BM_WORK_BLOCK * pWorkBlock, unsigned * pPSIData) {
  unsigned   srsi;
  unsigned   brsi;
  unsigned   NumSectors;
  unsigned   lbi;
  unsigned   psiData;
  NOR_BM_LSH lsh;
  unsigned   DataStat;
  int        r;

  lbi     = pWorkBlock->lbi;
  psiData = _L2P_Read(pInst, lbi);
  if (pPSIData != NULL) {
    *pPSIData = psiData;
  }
  if ((_IsCRCEnabled(pInst) != 0) || (_IsECCEnabled(pInst) != 0)) {
    //
    // If a power failure interrupts the write to the header of the work block
    // the CRC or ECC check fails at low-level mount and the block is discarded leading
    // to data loss. Therefore, conversion in-place with CRC or ECC enabled is not allowed.
    // This is true only for NOR flash devices that can rewrite. For NOR flash
    // devices that cannot rewrite we do not modify any existing data and therefore
    // we can perform and in-place conversion.
    //
#if (FS_NOR_CAN_REWRITE != 0)
    return 0;
#else
    if (_IsRewriteSupported(pInst) != 0) {
      return 0;                         // Cannot convert in place.
    }
#endif // FS_NOR_CAN_REWRITE != 0
  }
#if (FS_NOR_CAN_REWRITE == 0)
  //
  // For NOR flash devices that cannot rewrite DataCnt is set to the correct value
  // (one more than the DataCnt of the corresponding data block) when the work block
  // is allocated in _AllocWorkBlock(). We do this because modifying DataCnt at the
  // work block conversion will require a second write to the same flash line
  // which is preventing us from converting the  work block in place. However, the
  // wear leveling procedure can move the data block to another location and by doing
  // this DataCnt will no longer correspond to DataCnt stored in the header of the work block.
  // Therefore we have to check here that DataCnt of the data block still matches.
  // If not, an in-place conversion of the work block is no longer possible.
  //
  if (_IsRewriteSupported(pInst) == 0) {
    unsigned psiWork;
    U8       DataCntData;
    U8       DataCntWork;

    psiWork = pWorkBlock->psi;
    if (psiData != 0u) {
      //
      // Data block found. Read DataCnt of data and work block.
      //
      r = _ReadPhySectorDataCnt(pInst, psiData, &DataCntData);
      if (r != 0) {
        return 0;                       // Work block cannot be converted in place.
      }
      r = _ReadPhySectorDataCnt(pInst, psiWork, &DataCntWork);
      if (r != 0) {
        return 0;                       // Work block cannot be converted in place.
      }
      ++DataCntData;
      if (DataCntData != DataCntWork) {
        return 0;                       // Work block cannot be converted in place.
      }
    }
  }
#endif // FS_NOR_CAN_REWRITE == 0
  NumSectors = pInst->LSectorsPerPSector;
  for (brsi = 0; brsi < NumSectors; brsi++) {
    //
    // Get the position of the logical sector on the storage inside the work block.
    //
    srsi = _brsi2srsi(pInst, pWorkBlock, brsi);
    if (srsi != BRSI_INVALID) {         // Not written to or invalidated?
      if (srsi != brsi) {
        return 0;                       // Work block cannot be converted in place.
      }
    } else {
      //
      // The position of the logical sector was not found. This can mean that:
      // 1) The logical sector has not been written yet. In this case the flag in pWorkBlock->paIsWritten
      //    corresponding to the native position is cleared. The work block can be converted in place.
      // 2) The data of logical sector has been written to the native position and then invalidated
      //    or another logical sector has been written to this position and then invalidated.
      //    In this case the corresponding flag in pWorkBlock->paIsWritten is set. The work block can NOT
      //    be converted in place if there is a valid copy of the logical sector in a data block.
      //
      if (_WB_IsSectorUsed(pWorkBlock, brsi) != 0) {
        if (psiData != 0u) {            // Sector in a data block?
          FS_MEMSET(&lsh, 0xFF, sizeof(lsh));
          r = _ReadLSH(pInst, psiData, brsi, &lsh);
          if (r != 0) {
            return 0;                   // Error, could not read log. sector header. Convert work block via copy.
          }
          DataStat = _GetLogSectorDataStat(pInst, &lsh);
          if (DataStat == DATA_STAT_VALID) {
            return 0;                   // Work block cannot be converted in-place.
          }
        }
      }
    }
  }
  return 1;                             // Work block can be converted in-place.
}

/*********************************************************************
*
*       _CleanWorkBlock
*
*  Function description
*    Closes the work buffer.
*    - Convert work block into normal data buffer by copy all data into it and marking it as data block
*    - Invalidate and mark as free the block which contained the same logical data area before
*
*  Parameters
*    pInst        [IN]  Driver instance.
*    pWorkBlock   [IN]  Work block to be cleaned.
*
*  Return values
*    ==0    OK
*    !=0    An error occurred
*/
static int _CleanWorkBlock(NOR_BM_INST * pInst, NOR_BM_WORK_BLOCK * pWorkBlock) {
  int r;

  r = _IsInPlaceConversionAllowed(pInst, pWorkBlock, NULL);
  if (r != 0) {             // Can work block be converted in-place ?
    r = _ConvertWorkBlockInPlace(pInst, pWorkBlock);
    if (r == 0) {
      return 0;             // Block converted, we are done
    }
    return 1;               // Fatal error, no recovery is possible
  }
  //
  // Work block could not be converted in place, try via copy.
  //
  r = _ConvertWorkBlockViaCopy(pInst, pWorkBlock);
  if (r != 0) {
    return 1;               // Error, could not convert work block.
  }
  return 0;
}

/*********************************************************************
*
*       _CleanLastWorkBlock
*
*  Function description
*    Removes the least recently used work block from list of work blocks and converts it into data block
*/
static int _CleanLastWorkBlock(NOR_BM_INST * pInst) {
  NOR_BM_WORK_BLOCK * pWorkBlock;
  int                 r;

  r = 1;              // Set to indicate an error.
  //
  // Find last work block in list.
  //
  pWorkBlock = pInst->pFirstWorkBlockInUse;
  if (pWorkBlock != NULL) {
    while (pWorkBlock->pNext != NULL) {
      pWorkBlock = pWorkBlock->pNext;
    }
    r = _CleanWorkBlock(pInst, pWorkBlock);
  }
  return r;
}

#if FS_NOR_SUPPORT_CLEAN

/*********************************************************************
*
*       _CleanWorkBlockLimited
*
*  Function description
*    Closes the specified work buffer.
*
*  Parameters
*    pInst        [IN]  Driver instance.
*    pWorkBlock   [IN]  Work block to be cleaned.
*
*  Return values
*    > 0    OK, work block has been closed.
*    ==0    OK, work block has not been closed.
*    < 0    An error occurred.
*
*  Additional information
*    This function performs the same operation as _CleanWorkBlock()
*    with the exception that it does not clean work blocks that
*    can be converted in place and which do not have an associated
*    data block.
*/
static int _CleanWorkBlockLimited(NOR_BM_INST * pInst, NOR_BM_WORK_BLOCK * pWorkBlock) {
  int      r;
  unsigned psiData;

  psiData = 0;
  r = _IsInPlaceConversionAllowed(pInst, pWorkBlock, &psiData);
  if (r != 0) {             // Can work block be converted in-place?
    if (psiData == 0u) {    // No associated data block?
      //
      // No clean operation is required after a block is converted
      // in place with no associated data block because in this case
      // no physical sector has to be erased.
      //
      return 0;
    }
    r = _ConvertWorkBlockInPlace(pInst, pWorkBlock);
    if (r == 0) {
      return 1;             // OK work block converted in place.
    }
    return -1;              // Fatal error, no recovery is possible
  }
  //
  // Work block could not be converted in place, try via copy.
  //
  r = _ConvertWorkBlockViaCopy(pInst, pWorkBlock);
  if (r != 0) {
    return -1;              // Error, could not convert work block.
  }
  return 1;                 // OK, work block converted via copy.
}

/*********************************************************************
*
*       _CleanAllWorkBlocks
*
*  Function description
*    Closes all work blocks.
*/
static int _CleanAllWorkBlocks(NOR_BM_INST * pInst) {
  int                 r;
  NOR_BM_WORK_BLOCK * pWorkBlock;
  NOR_BM_WORK_BLOCK * pWorkBlockNext;

  pWorkBlock = pInst->pFirstWorkBlockInUse;
  while (pWorkBlock != NULL) {
    pWorkBlockNext = pWorkBlock->pNext;
    r = _CleanWorkBlockLimited(pInst, pWorkBlock);
    if (r < 0) {
      return 1;     // Error, could not clean work block.
    }
    pWorkBlock = pWorkBlockNext;
  }
  return 0;         // OK, alls work blocks have been cleaned.
}

#endif // FS_NOR_SUPPORT_CLEAN

/*********************************************************************
*
*       _AllocWorkBlock
*
*  Function description
*    Allocates a new work block.
*
*  Parameters
*    pInst        [IN]  Driver instance.
*    lbi          Logical block index assigned to work block.
*
*  Return values
*    !=NULL   Pointer to allocated work block.
*    ==NULL   An error occurred, typ. a fatal error.
*
*  Additional information
*    This function performs the following operations:
*    - Allocates a new work block descriptor by freeing an existing one, if necessary.
*    - Selects a free physical sector and erases it.
*    - Marks the selected physical sector as work block by writing management
*      information such as EraseCnt and LBI to its header.
*/
static NOR_BM_WORK_BLOCK * _AllocWorkBlock(NOR_BM_INST * pInst, unsigned lbi) {
  NOR_BM_WORK_BLOCK * pWorkBlock;
  U32                 EraseCnt;
  unsigned            psi;
  int                 r;
  U8                  DataCnt;

  pWorkBlock = _AllocWorkBlockDesc(pInst, lbi);
  if (pWorkBlock == NULL) {
    r = _CleanLastWorkBlock(pInst);
    if (r != 0) {
      return NULL;                  // Error, could not convert the oldest work block (occurs only in case of a fatal error).
    }
    pWorkBlock = _AllocWorkBlockDesc(pInst, lbi);
    if (pWorkBlock == NULL) {
      _OnFatalError(pInst, RESULT_OUT_OF_WORK_BLOCKS, 0);
      return NULL;                  // Error, no more available work blocks.
    }
  }
  //
  // Get an empty block to write to.
  //
  psi = _AllocErasedBlock(pInst, &EraseCnt);
  if (psi == 0u) {
    //
    // Move the work block back to the list of unused blocks.
    //
    _WB_RemoveFromUsedList(pInst, pWorkBlock);
    _WB_AddToFreeList(pInst, pWorkBlock);
    return NULL;                    // Error, could not erase phy. sector (occurs only in case of a fatal error).
  }
  //
  // New work block allocated. Check if there is a associated data block
  // and if so read the data count. We have to increase the data count
  // by 1 and to store it to the header of the work block to make sure
  // that we discard the old data block when the in-place conversion
  // of the work block is interrupted by a power failure. This operation
  // has to be performed only for NOR flash devices that cannot rewrite.
  // For NOR flash devices that can rewrite DataCnt is stored when the
  // work block is converted in place. In this way we can spare a read
  // access to the header of the corresponding data block if such a block
  // is present.
  //
  DataCnt = 0xFF;
#if (FS_NOR_CAN_REWRITE == 0)
  {
    unsigned psiData;

    if (_IsRewriteSupported(pInst) == 0) {
      psiData = _L2P_Read(pInst, lbi);
      if (psiData != 0u) {
        (void)_ReadPhySectorDataCnt(pInst, psiData, &DataCnt);
        ++DataCnt;
      }
    }
  }
#endif
  pWorkBlock->psi = psi;
  r = _MarkAsWorkBlock(pInst, psi, lbi, EraseCnt, DataCnt);
  if (r != 0) {
    _MarkPhySectorAsFree(pInst, psi);
    _WB_RemoveFromUsedList(pInst, pWorkBlock);
    _WB_AddToFreeList(pInst, pWorkBlock);
    return NULL;                    // Error, could not mark the allocated block as work block.
  }
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR_BM: ALLOC_WORK_BLOCK PSI: %u, LBI: %u\n", psi, lbi));
  return pWorkBlock;
}

/*********************************************************************
*
*       _FindWorkBlock
*
*  Function description
*    Tries to locate a work block for a given logical block.
*/
static NOR_BM_WORK_BLOCK * _FindWorkBlock(const NOR_BM_INST * pInst, unsigned lbi) {
  NOR_BM_WORK_BLOCK * pWorkBlock;

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
static void _MarkWorkBlockAsMRU(NOR_BM_INST * pInst, NOR_BM_WORK_BLOCK * pWorkBlock) {
  if (pWorkBlock != pInst->pFirstWorkBlockInUse) {
    _WB_RemoveFromUsedList(pInst, pWorkBlock);
    _WB_AddToUsedList(pInst, pWorkBlock);
  }
}

#if FAIL_SAFE_ERASE_NO_REWRITE

/*********************************************************************
*
*       _IsWorkPSHConsistent
*
*  Function description
*    Checks if the data stored in a physical sector header of a work block makes sense.
*
*  Additional information
*    This function is used to check if the data stored in a physical
*    sector header of a work block was damaged by an unexpected reset
*    when an attempt was made to convert the work block to a data block
*    via an in-place conversion.
*/
static int _IsWorkPSHConsistent(NOR_BM_INST * pInst, unsigned psi) {
  int        r;
  NOR_BM_PSH psh;

  FS_MEMSET(&psh, 0, sizeof(psh));
  r = _ReadPSH(pInst, psi, &psh);
  if (r != 0) {
    return 0;           // Could not read physical sector header.
  }
  r = 1;                // Set to indicate that the physical sector header is not damaged.
  if (psh.IsValid == 0x00u) {
    if ((psh.DataCntData != DATA_CNT_INVALID) || (psh.lbiData != LBI_INVALID)) {
#if (FS_NOR_SUPPORT_CRC != 0) && (FS_NOR_SUPPORT_ECC != 0)
      if ((_IsCRCEnabled(pInst) != 0) && (_IsECCEnabled(pInst) != 0)) {
        int Result;
        int NumBitsCorrected;

        if (psh.eccStatData == 0) {       // Is ECC valid?
          //
          // Check that the data was completely written by verifying the ECC.
          //
          NumBitsCorrected = 0;
          Result = pInst->pECC_API->pfLoadApplyPSH_Data(pInst, &psh, &NumBitsCorrected);
          if (Result != 0) {
            r = 0;      // The physical sector header is not consistent.
          } else {
            //
            // Count the corrected bit errors.
            //
            UPDATE_NUM_BIT_ERRORS(pInst, NumBitsCorrected);
            //
            // We check here that the information related to the data block
            // was stored correctly by verifying the CRC.
            //
            Result = _pCRC_API->pfLoadVerifyPSH_Data(pInst, &psh);
            if (Result != 0) {
              r = 0;        // The physical sector header is not consistent.
            }
          }
        }
      } else {
        if ((_IsCRCEnabled(pInst) != 0) && (_IsECCEnabled(pInst) == 0)) {
          int Result;

          //
          // We check here that the information related to the data block
          // was stored correctly by verifying the CRC.
          //
          Result = _pCRC_API->pfLoadVerifyPSH_Data(pInst, &psh);
          if (Result != 0) {
            r = 0;        // The physical sector header is not consistent.
          }
        } else {
          if ((_IsCRCEnabled(pInst) == 0) && (_IsECCEnabled(pInst) != 0)) {
            int Result;
            int NumBitsCorrected;

            if (psh.eccStatData == 0) {       // Is ECC valid?
              //
              // Check that the data was completely written by verifying the ECC.
              //
              NumBitsCorrected = 0;
              Result = pInst->pECC_API->pfLoadApplyPSH_Data(pInst, &psh, &NumBitsCorrected);
              if (Result != 0) {
                r = 0;      // The physical sector header is not consistent.
              } else {
                //
                // Count the corrected bit errors.
                //
                UPDATE_NUM_BIT_ERRORS(pInst, NumBitsCorrected);
              }
            }
          } else {
            if (psh.crcData != 0u) {
              r = 0;        // The physical sector header is not consistent.
            }
          }
        }
      }
#elif (FS_NOR_SUPPORT_CRC != 0) && (FS_NOR_SUPPORT_ECC == 0)
      if (_IsCRCEnabled(pInst) != 0) {
        int Result;

        //
        // We check here that the information related to the data block
        // was stored correctly by verifying the CRC.
        //
        Result = _pCRC_API->pfLoadVerifyPSH_Data(pInst, &psh);
        if (Result != 0) {
          r = 0;        // The physical sector header is not consistent.
        }
      } else {
        if (psh.crcData != 0u) {
          r = 0;        // The physical sector header is not consistent.
        }
      }
#elif (FS_NOR_SUPPORT_CRC == 0) && (FS_NOR_SUPPORT_ECC != 0)
      if (_IsECCEnabled(pInst) != 0) {
        int Result;
        int NumBitsCorrected;

        if (psh.eccStatData == 0) {       // Is ECC valid?
          //
          // Check that the data was completely written by verifying the ECC.
          //
          NumBitsCorrected = 0;
          Result = pInst->pECC_API->pfLoadApplyPSH_Data(pInst, &psh, &NumBitsCorrected);
          if (Result != 0) {
            r = 0;      // The physical sector header is not consistent.
          } else {
            //
            // Count the corrected bit errors.
            //
            UPDATE_NUM_BIT_ERRORS(pInst, NumBitsCorrected);
          }
        }
      } else {
        if (psh.crcData != 0u) {
          r = 0;        // The physical sector header is not consistent.
        }
      }
#else
      //
      // We check here that the information related to the data block
      // was stored correctly by verifying that the NOR_BM_PSH::crcData
      // member is set to 0.
      //
      if (psh.crcData != 0u) {
        r = 0;        // The physical sector header is not consistent.
      }
#endif // FS_NOR_SUPPORT_CRC != 0 && FS_NOR_SUPPORT_ECC != 0
    }
  }
  return r;
}

#endif // FAIL_SAFE_ERASE_NO_REWRITE

/*********************************************************************
*
*       _LoadWorkBlock
*
*  Function description
*    Loads information about a work block from the NOR flash device.
*
*  Parameters
*    pInst        Driver instance.
*    pWorkBlock   [IN] Index of the physical sector assigned to the work block.
*                 [OUT] Information about the work block.
*
*  Return value
*    ==0      OK, work block loaded successfully.
*    !=0      An error occurred.
*
*  Additional information
*    This function is called during low-level mount operation,
*    because at all other times the information about the work blocks
*    are up to date and kept in RAM.
*/
static int _LoadWorkBlock(NOR_BM_INST * pInst, NOR_BM_WORK_BLOCK * pWorkBlock) {
  unsigned   NumSectors;
  unsigned   iSector;
  unsigned   brsi;
  unsigned   psiWork;
  unsigned   DataStat;
  NOR_BM_LSH lsh;
  int        r;
  int        IsRelocationRequired;

  psiWork              = pWorkBlock->psi;
  NumSectors           = pInst->LSectorsPerPSector;
  IsRelocationRequired = 0;
  //
  // Iterate over all logical sectors and read the header information in order
  // to find out if a sector contains any data and if the data is valid or not.
  //
  for (iSector = 0; iSector < NumSectors; iSector++) {
    FS_MEMSET(&lsh, 0xFF, sizeof(lsh));
    r = _ReadLSH(pInst, psiWork, iSector, &lsh);
    if (r != 0) {
      //
      // Read or CRC error. Mark sector as used to prevent that we write any data to it.
      // In addition, indicate that the work block has to be converted to a data block
      // in order to correct the errors.
      //
      _WB_MarkSectorAsUsed(pWorkBlock, iSector);
      IsRelocationRequired = 1;
    } else {
      DataStat = _GetLogSectorDataStat(pInst, &lsh);
      if (DataStat == DATA_STAT_EMPTY) {
        if (_IsBlankLogSector(pInst, psiWork, iSector) == 0) {
          //
          // Sector is not blank, even when the data status in the header indicates this.
          // This situation can happen if a sector write operation has been interrupted by
          // a reset; in this case the header is still marked as blank, but a part of the data
          // area is not blank. We mark the sector as containing invalid data so we do not
          // run into it again. It will be re-used when the physical sector is erased.
          //
          (void)_MarkLogSectorAsInvalid(pInst, psiWork, iSector);
          _WB_MarkSectorAsUsed(pWorkBlock, iSector);
        }
        continue;
      }
      //
      // Found a sector which contains data.
      //
      brsi = lsh.brsi;
      _WB_MarkSectorAsUsed(pWorkBlock, iSector);
      if (brsi < NumSectors) {
        if (DataStat == DATA_STAT_VALID) {
          //
          // The sector contains valid data. Update the mapping list.
          //
          _WB_WriteAssignment(pInst, pWorkBlock, brsi, iSector);
        }
      }
    }
  }
  r = 0;
#if FAIL_SAFE_ERASE_NO_REWRITE
  //
  // Check if the information stored in the physical sector header is corrupted.
  // This can happen if an in-place conversion is interrupted by an unexpected
  // reset before the NOR flash device has a chance to update the information
  // related to data block. Only the NOR flash devices that do not support
  // incremental write operations are affected by this.
  //
  if (pInst->FailSafeErase != 0u) {
    if (_IsRewriteSupported(pInst) == 0) {
      if (_IsWorkPSHConsistent(pInst, psiWork) == 0) {
        r = _ConvertWorkBlockViaCopy(pInst, pWorkBlock);
        IsRelocationRequired = 0;
      }
    }
  }
  if (r == 0)
#endif // FAIL_SAFE_ERASE_NO_REWRITE
  {
    if (IsRelocationRequired != 0) {
      r = _ConvertWorkBlockViaCopy(pInst, pWorkBlock);
    }
  }
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR_BM: LOAD_WORK_BLOCK PSI: %u, LBI: %u, r: %d\n", pWorkBlock->psi, pWorkBlock->lbi, r));
  return r;
}

/*********************************************************************
*
*       _IsPhySectorDataMoreRecent
*
*  Function description
*    Checks which of the two data blocks contains actual data.
*
*  Parameters
*    pInst        Driver instance.
*    pPSH         [IN] Header of the current phy. sector to be checked.
*    psiPrev      Sector index of the previous phy. sector found with
*                 the same LBI.
*
*  Return value
*    ==0        Current phy. sector (pPSH) contains actual data.
*    ==1        psiPrev contains actual data.
*
*  Additional information
*    Used during low-level mount only to determine which data block to
*    discard when two data blocks are found with the same LBI.
*/
static int _IsPhySectorDataMoreRecent(NOR_BM_INST * pInst, const NOR_BM_PSH * pPSH, unsigned psiPrev) {
  NOR_BM_PSH pshPrev;
  U8         Data;
  int        r;
  U8         DataCnt;
  U8         DataCntPrev;

  r = _ReadPSH(pInst, psiPrev, &pshPrev);
  if (r == 0) {
    //
    // This sequence works only if the difference between the data counts
    // is 1 which should always be the case. Data has to be either of U8
    // type or the result have to be masked with 0xFF for the check sequence
    // to work as expected.
    //
    DataCnt     = (U8)_GetPhySectorDataCnt(pInst, pPSH);
    DataCntPrev = (U8)_GetPhySectorDataCnt(pInst, &pshPrev);
    Data = DataCntPrev - DataCnt;
    if (Data == 1u) {
      return 1;      // The data block specified by psiPrev is newer.
    }
  }
  return 0;          // The data block specified by psiPrev is older.
}

#if FS_NOR_ENABLE_STATS

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
*    (1) A sector in a work block which contains valid data has also a valid BRSI assigned to it
*/
static U32 _GetNumValidSectors(NOR_BM_INST * pInst, unsigned lbi) {
  unsigned            LSectorsPerPSector;
  U32                 NumSectors;
  unsigned            psiSrc;
  unsigned            srsi;
  unsigned            brsi;
  unsigned            DataStat;
  NOR_BM_WORK_BLOCK * pWorkBlock;
  int                 r;

  psiSrc             = _L2P_Read(pInst, lbi);
  pWorkBlock         = _FindWorkBlock(pInst, lbi);
  LSectorsPerPSector = pInst->LSectorsPerPSector;
  NumSectors         = 0;
  //
  // 1st case: a data block is assigned to logical block
  //
  if ((psiSrc != 0u) && (pWorkBlock == NULL)) {
    srsi = 0;
    do {
      r = _ReadLogSectorDataStat(pInst, psiSrc, srsi, &DataStat);
      if (r == 0) {
        if (DataStat == DATA_STAT_VALID) {
          ++NumSectors;
        }
      }
      ++srsi;
    } while (--LSectorsPerPSector != 0u);
  }
  //
  // 2nd case: a work block is assigned to logical block
  //
  if ((psiSrc == 0u) && (pWorkBlock != NULL)) {
    brsi = 0;
    do {
      if (_brsi2srsi(pInst, pWorkBlock, brsi) != BRSI_INVALID) {     // Note 1
        ++NumSectors;
      }
      ++brsi;
    } while (--LSectorsPerPSector != 0u);
  }
  //
  // 3rd case: a data block and a work block are assigned to logical block
  //
  if ((psiSrc != 0u) && (pWorkBlock != NULL)) {
    srsi = 0;
    do {
      r = _ReadLogSectorDataStat(pInst, psiSrc, srsi, &DataStat);
      if (r == 0) {
        if (DataStat == DATA_STAT_VALID) {
          ++NumSectors;
        } else {
          if (_brsi2srsi(pInst, pWorkBlock, srsi) != BRSI_INVALID) {   // Note 1
            ++NumSectors;
          }
        }
      }
      ++srsi;
    } while (--LSectorsPerPSector != 0u);
  }
  return NumSectors;
}

#endif

/*********************************************************************
*
*       _LowLevelMount
*
*  Function description
*    Reads and analyzes management information from NOR flash device.
*
*  Parameters
*    pInst      Driver instance.
*
*  Return value
*    ==0    OK, the NOR flash device can be accessed.
*    !=0    An error occurred.
*/
static int _LowLevelMount(NOR_BM_INST * pInst) {
  U32                 EraseCntMax;                  // Highest erase count on any physical sector.
  U32                 EraseCnt;
  U32                 EraseCntMin;
  U32                 NumBlocksEraseCntMin;
  unsigned            DataStat;
  unsigned            lbi;
  unsigned            iSector;
  unsigned            psiPrev;
  U16                 aInfo[sizeof(_acInfo) / 2u];  // 16-bit alignment needed for the CRC calculation.
  U32                 Version;
  U32                 BytesPerSectorFromDevice;
  U32                 NumLogBlocksFromDevice;
  int                 NumLogBlocksToUse;
  U16                 NumWorkBlocksFromDevice;
  unsigned            NumWorkBlocks;
  unsigned            NumWorkBlocksToAllocate;
  unsigned            NumBlocks;
  unsigned            LSectorsPerPSector;
  unsigned            LSectorsPerPSectorMax;
  unsigned            NumPhySectors;
  U32                 PhySectorSize;
  U16                 FailSafeErase;
  NOR_BM_WORK_BLOCK * pWorkBlock;
  NOR_BM_PSH          psh;
  int                 r;
  int                 Result;
  U16                 IsWriteProtected;
  U16                 HasFatalError;
  U16                 ErrorType;
  U32                 ErrorPSI;
  unsigned            DataCnt;
  NOR_BM_WORK_BLOCK * pWorkBlockNext;

  FS_MEMSET(aInfo, 0, sizeof(aInfo));
  Version                  = 0;
  BytesPerSectorFromDevice = 0;
  NumLogBlocksFromDevice   = 0;
  NumWorkBlocksFromDevice  = 0;
  FailSafeErase            = 0;
  //
  // Read the format information from the NOR flash device.
  // If the ECC verification is enabled, then this information
  // is read below after the header of the logical sector is read.
  //
  if (_IsECCEnabled(pInst) == 0) {
    (void)_ReadLogSectorData(pInst, PSI_INFO_BLOCK, SRSI_INFO_FORMAT, aInfo,                     INFO_OFF_FORMAT_SIGNATURE, sizeof(aInfo));
    (void)_ReadLogSectorData(pInst, PSI_INFO_BLOCK, SRSI_INFO_FORMAT, &Version,                  INFO_OFF_FORMAT_VERSION,   sizeof(Version));
    (void)_ReadLogSectorData(pInst, PSI_INFO_BLOCK, SRSI_INFO_FORMAT, &BytesPerSectorFromDevice, INFO_OFF_BYTES_PER_SECTOR, sizeof(BytesPerSectorFromDevice));
    (void)_ReadLogSectorData(pInst, PSI_INFO_BLOCK, SRSI_INFO_FORMAT, &NumLogBlocksFromDevice,   INFO_OFF_NUM_LOG_BLOCKS,   sizeof(NumLogBlocksFromDevice));
    (void)_ReadLogSectorData(pInst, PSI_INFO_BLOCK, SRSI_INFO_FORMAT, &NumWorkBlocksFromDevice,  INFO_OFF_NUM_WORK_BLOCKS,  sizeof(NumWorkBlocksFromDevice));
    (void)_ReadLogSectorData(pInst, PSI_INFO_BLOCK, SRSI_INFO_FORMAT, &FailSafeErase,            INFO_OFF_FAIL_SAFE_ERASE,  sizeof(FailSafeErase));
  }
#if (FS_NOR_SUPPORT_CRC != 0) || (FS_NOR_SUPPORT_ECC != 0)
  if ((_IsCRCEnabled(pInst) != 0) || (_IsECCEnabled(pInst) != 0)) {
    NOR_BM_LSH lsh;

    FS_MEMSET(&lsh, 0xFF, sizeof(lsh));
    r = _ReadLSH(pInst, PSI_INFO_BLOCK, SRSI_INFO_FORMAT, &lsh);
    if (r != 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_BM: _LowLevelMount: Could not read LSH of format info."));
      return 1;
    }
#if FS_NOR_SUPPORT_ECC
    if (_IsECCEnabled(pInst) != 0) {
      unsigned   NumBytes;
      const U8 * pData8;

      //
      // Read the format information from NOR flash device.
      //
      NumBytes = 1uL << pInst->pECCHookData->ldBytesPerBlock;
      r = _ReadLogSectorData(pInst, PSI_INFO_BLOCK, SRSI_INFO_FORMAT, _pECCBuffer, 0, NumBytes);
      if (r != 0) {
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_BM: _LowLevelMount: Could not read format info."));
        return 1;
      }
      r = pInst->pECC_API->pfApplyData(pInst, _pECCBuffer, lsh.aaECCSectorData[0]);
      if (r < 0) {
        FS_DEBUG_WARN((FS_MTYPE_DRIVER, "NOR_BM: _LowLevelMount: Uncorrectable bit error in format info."));
        return 1;
      }
      UPDATE_NUM_BIT_ERRORS(pInst, r);
      //
      // Load the format information into local variables.
      //
      pData8 = SEGGER_PTR2PTR(U8, _pECCBuffer);                                                                                   // MISRA deviation D:100e
      FS_MEMCPY(aInfo,                     pData8 + INFO_OFF_FORMAT_SIGNATURE, sizeof(aInfo));
      FS_MEMCPY(&Version,                  pData8 + INFO_OFF_FORMAT_VERSION,   sizeof(Version));
      FS_MEMCPY(&BytesPerSectorFromDevice, pData8 + INFO_OFF_BYTES_PER_SECTOR, sizeof(BytesPerSectorFromDevice));
      FS_MEMCPY(&NumLogBlocksFromDevice,   pData8 + INFO_OFF_NUM_LOG_BLOCKS,   sizeof(NumLogBlocksFromDevice));
      FS_MEMCPY(&NumWorkBlocksFromDevice,  pData8 + INFO_OFF_NUM_WORK_BLOCKS,  sizeof(NumWorkBlocksFromDevice));
      FS_MEMCPY(&FailSafeErase,            pData8 + INFO_OFF_FAIL_SAFE_ERASE,  sizeof(FailSafeErase));
    }
#endif // FS_NOR_SUPPORT_ECC
#if FS_NOR_SUPPORT_CRC
    if (_IsCRCEnabled(pInst) != 0) {
      U16 crcCalc;
      U16 crcRead;

      crcCalc = CRC_SECTOR_DATA_INIT;
      crcCalc = _pCRC_API->pfCalcData(SEGGER_PTR2PTR(U8, aInfo),                     sizeof(aInfo),                    crcCalc);  // MISRA deviation D:100e
      crcCalc = _pCRC_API->pfCalcData(SEGGER_PTR2PTR(U8, &Version),                  sizeof(Version),                  crcCalc);  // MISRA deviation D:100e
      crcCalc = _pCRC_API->pfCalcData(SEGGER_PTR2PTR(U8, &BytesPerSectorFromDevice), sizeof(BytesPerSectorFromDevice), crcCalc);  // MISRA deviation D:100e
      crcCalc = _pCRC_API->pfCalcData(SEGGER_PTR2PTR(U8, &NumLogBlocksFromDevice),   sizeof(NumLogBlocksFromDevice),   crcCalc);  // MISRA deviation D:100e
      crcCalc = _pCRC_API->pfCalcData(SEGGER_PTR2PTR(U8, &NumWorkBlocksFromDevice),  sizeof(NumWorkBlocksFromDevice),  crcCalc);  // MISRA deviation D:100e
      crcCalc = _pCRC_API->pfCalcData(SEGGER_PTR2PTR(U8, &FailSafeErase),            sizeof(FailSafeErase),            crcCalc);  // MISRA deviation D:100e
      crcRead = lsh.crcSectorData;
      if (crcCalc != crcRead)  {
        FS_DEBUG_WARN((FS_MTYPE_DRIVER, "NOR_BM: _LowLevelMount: CRC check of format info failed Calc: 0x%02x, Read: 0x%02x", crcCalc, crcRead));
        return 1;
      }
    }
#endif // FS_NOR_SUPPORT_CRC
  }
#endif // FS_NOR_SUPPORT_CRC != 0 || FS_NOR_SUPPORT_ECC != 0
#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  Version                  = _pMultiByteAPI->pfLoadU32(SEGGER_PTR2PTR(U8, &Version));                                             // MISRA deviation D:100e
  BytesPerSectorFromDevice = _pMultiByteAPI->pfLoadU32(SEGGER_PTR2PTR(U8, &BytesPerSectorFromDevice));                            // MISRA deviation D:100e
  NumLogBlocksFromDevice   = _pMultiByteAPI->pfLoadU32(SEGGER_PTR2PTR(U8, &NumLogBlocksFromDevice));                              // MISRA deviation D:100e
  NumWorkBlocksFromDevice  = _pMultiByteAPI->pfLoadU16(SEGGER_PTR2PTR(U8, &NumWorkBlocksFromDevice));                             // MISRA deviation D:100e
  FailSafeErase            = _pMultiByteAPI->pfLoadU16(SEGGER_PTR2PTR(U8, &FailSafeErase));                                       // MISRA deviation D:100e
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER                                                                                      // MISRA deviation D:100e
  if (FS_MEMCMP(_acInfo, aInfo, sizeof(_acInfo)) != 0) {
    FS_DEBUG_WARN((FS_MTYPE_DRIVER, "NOR_BM: _LowLevelMount: Invalid low-level signature."));
    return 1;                   // Error
  }
  if (Version != (U32)LLFORMAT_VERSION) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_BM: _LowLevelMount: Invalid low-level format version."));
    return 1;                   // Error
  }
  if (BytesPerSectorFromDevice > (U32)FS_Global.MaxSectorSize) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_BM: _LowLevelMount: Sector size specified in drive is higher than the sector size that can be stored by the FS."));
    return 1;                   // Error
  }
  if (NumWorkBlocksFromDevice >= pInst->NumPhySectors) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_BM: _LowLevelMount: Invalid number of work blocks."));
    return 1;                   // Error
  }
  PhySectorSize             = pInst->PhySectorSize;
  LSectorsPerPSector        = _CalcLSectorsPerPSector(pInst, PhySectorSize, BytesPerSectorFromDevice);
  LSectorsPerPSectorMax     = SEGGER_MAX(LSectorsPerPSector, pInst->LSectorsPerPSector);   // Required for the correct allocation of paIsWritten and paAssign arrays
  pInst->LSectorsPerPSector = (U16)LSectorsPerPSector;
  pInst->NumBitsSRSI        = (U8)FS_BITFIELD_CalcNumBitsUsed(LSectorsPerPSectorMax);
  pInst->ldBytesPerSector   = (U16)_ld(BytesPerSectorFromDevice);
#if FS_NOR_SUPPORT_FAIL_SAFE_ERASE
  pInst->FailSafeErase      = FailSafeErase != 0u ? 0u : 1u;        // Reversed logic: 0xFFFF -> not supported, 0x0000 -> supported
#endif // FS_NOR_SUPPORT_FAIL_SAFE_ERASE
  //
  // Find out how many work blocks are required to be allocated.
  // We take the maximum between the number of work blocks read from device
  // and the number of work blocks configured. The reason is to prevent
  // an overflow in the paWorkBlock array when the application increases
  // the number of work blocks and does a low-level format.
  //
  NumWorkBlocks           = pInst->NumWorkBlocks;
  NumWorkBlocksToAllocate = SEGGER_MAX(NumWorkBlocksFromDevice, NumWorkBlocks);
  NumWorkBlocks           = NumWorkBlocksFromDevice;
  //
  // Check if there are enough logical sectors to store the file system.
  //
  NumPhySectors     = pInst->NumPhySectors;
  NumLogBlocksToUse = _CalcNumBlocksToUse(NumPhySectors, NumWorkBlocks);
  if (NumLogBlocksToUse <= 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_BM: _LowLevelMount: Insufficient number of physical sectors. Low-level format required."));
    return 1;
  }
#if FS_NOR_STRICT_FORMAT_CHECK
  if (NumLogBlocksFromDevice != (U32)NumLogBlocksToUse) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_BM: _LowLevelMount: Invalid number of logical blocks. Low-level format required."));
    return 1;
  }
#else
  if (NumLogBlocksFromDevice > (U32)NumLogBlocksToUse) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_BM: _LowLevelMount: Number of logical blocks has shrunk. Low-level format required."));
    return 1;
  }
#endif
  pInst->NumLogBlocks  = (U16)NumLogBlocksToUse;
  pInst->NumWorkBlocks = (U8)NumWorkBlocks;
  pInst->NumLogSectors = (U32)NumLogBlocksToUse * LSectorsPerPSector;
  //
  // Load the information about the fatal error.
  //
  r = 0;
  pInst->IsWriteProtected = 0;
  pInst->HasFatalError    = 0;
  pInst->ErrorType        = RESULT_NO_ERROR;
  pInst->ErrorPSI         = 0;
  IsWriteProtected        = 0xFFFF;
  HasFatalError           = 0xFFFF;
  ErrorType               = 0;
  ErrorPSI                = 0;
  //
  // With ECC enabled the information about a fatal error is read
  // further below as one block at once.
  //
  if (_IsECCEnabled(pInst) == 0) {
    Result = _ReadLogSectorData(pInst, PSI_INFO_BLOCK, SRSI_INFO_ERROR, &IsWriteProtected, INFO_OFF_IS_WRITE_PROTECTED, sizeof(IsWriteProtected));
    r = ((r == 0) && (Result != 0)) ? Result : r;
    Result = _ReadLogSectorData(pInst, PSI_INFO_BLOCK, SRSI_INFO_ERROR, &HasFatalError,    INFO_OFF_HAS_FATAL_ERROR,    sizeof(HasFatalError));
    r = ((r == 0) && (Result != 0)) ? Result : r;
    Result = _ReadLogSectorData(pInst, PSI_INFO_BLOCK, SRSI_INFO_ERROR, &ErrorType,        INFO_OFF_ERROR_TYPE,         sizeof(ErrorType));
    r = ((r == 0) && (Result != 0)) ? Result : r;
    Result = _ReadLogSectorData(pInst, PSI_INFO_BLOCK, SRSI_INFO_ERROR, &ErrorPSI,         INFO_OFF_ERROR_PSI,          sizeof(ErrorPSI));
    r = ((r == 0) && (Result != 0)) ? Result : r;
  }
#if (FS_NOR_SUPPORT_CRC != 0) || (FS_NOR_SUPPORT_ECC != 0)
  if (r == 0) {
    NOR_BM_LSH lsh;

    FS_MEMSET(&lsh, 0xFF, sizeof(lsh));
    r = _ReadLSH(pInst, PSI_INFO_BLOCK, SRSI_INFO_ERROR, &lsh);
    if (r == 0) {
#if FS_NOR_SUPPORT_ECC
      if (_IsECCEnabled(pInst) != 0) {
        unsigned   NumBytes;
        const U8 * pData8;

        //
        // Read the fatal error information from NOR flash device.
        //
        NumBytes = 1uL << pInst->pECCHookData->ldBytesPerBlock;
        r = _ReadLogSectorData(pInst, PSI_INFO_BLOCK, SRSI_INFO_ERROR, _pECCBuffer, 0, NumBytes);
        if (r != 0) {
          FS_DEBUG_WARN((FS_MTYPE_DRIVER, "NOR_BM: _LowLevelMount: Could not read error info."));
          r = 1;
        } else {
          r = pInst->pECC_API->pfApplyData(pInst, _pECCBuffer, lsh.aaECCSectorData[0]);
          if (r < 0) {
            FS_DEBUG_WARN((FS_MTYPE_DRIVER, "NOR_BM: _LowLevelMount: Uncorrectable bit error in error info."));
            r = 1;
          } else {
            UPDATE_NUM_BIT_ERRORS(pInst, r);
            //
            // Load the error information into local variables.
            //
            pData8 = SEGGER_CONSTPTR2PTR(const U8, _pECCBuffer);                                                                  // MISRA deviation D:100e
            FS_MEMCPY(&IsWriteProtected, pData8 + INFO_OFF_IS_WRITE_PROTECTED, sizeof(IsWriteProtected));
            FS_MEMCPY(&HasFatalError,    pData8 + INFO_OFF_HAS_FATAL_ERROR,    sizeof(HasFatalError));
            FS_MEMCPY(&ErrorType,        pData8 + INFO_OFF_ERROR_TYPE,         sizeof(ErrorType));
            FS_MEMCPY(&ErrorPSI,         pData8 + INFO_OFF_ERROR_PSI,          sizeof(ErrorPSI));
          }
        }
      }
#endif // FS_NOR_SUPPORT_ECC
#if FS_NOR_SUPPORT_CRC
      if (_IsCRCEnabled(pInst) != 0) {
        U16 crcCalc;
        U16 crcRead;

        crcCalc = CRC_SECTOR_DATA_INIT;
        crcCalc = _pCRC_API->pfCalcData(SEGGER_PTR2PTR(U8, &IsWriteProtected), sizeof(IsWriteProtected), crcCalc);                // MISRA deviation D:100e
        crcCalc = _pCRC_API->pfCalcData(SEGGER_PTR2PTR(U8, &HasFatalError),    sizeof(HasFatalError),    crcCalc);                // MISRA deviation D:100e
        crcCalc = _pCRC_API->pfCalcData(SEGGER_PTR2PTR(U8, &ErrorType),        sizeof(ErrorType),        crcCalc);                // MISRA deviation D:100e
        crcCalc = _pCRC_API->pfCalcData(SEGGER_PTR2PTR(U8, &ErrorPSI),         sizeof(ErrorPSI),         crcCalc);                // MISRA deviation D:100e
        crcRead = lsh.crcSectorData;
        if (crcCalc != crcRead)  {
          r = 1;
        }
      }
#endif // FS_NOR_SUPPORT_CRC
    }
  }
#endif // FS_NOR_SUPPORT_CRC != 0 || FS_NOR_SUPPORT_ECC != 0
  if (r == 0) {
#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
    IsWriteProtected = _pMultiByteAPI->pfLoadU16(SEGGER_PTR2PTR(U8, &IsWriteProtected));                                          // MISRA deviation D:100e
    HasFatalError    = _pMultiByteAPI->pfLoadU16(SEGGER_PTR2PTR(U8, &HasFatalError));                                             // MISRA deviation D:100e
    ErrorType        = _pMultiByteAPI->pfLoadU16(SEGGER_PTR2PTR(U8, &ErrorType));                                                 // MISRA deviation D:100e
    ErrorPSI         = _pMultiByteAPI->pfLoadU32(SEGGER_PTR2PTR(U8, &ErrorPSI));                                                  // MISRA deviation D:100e
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
    pInst->IsWriteProtected = IsWriteProtected != 0xFFFFu ? 1u : 0u;    // Inverted, 0xFFFF is not write protected
    pInst->HasFatalError    = HasFatalError    != 0xFFFFu ? 1u : 0u;    // Inverted, 0xFFFF doesn't have fatal error
    if (pInst->HasFatalError != 0u) {
      pInst->ErrorType = (U8)ErrorType;
      pInst->ErrorPSI  = ErrorPSI;
    }
  }
  //
  // Assign reasonable default for configurable values.
  //
  if (pInst->MaxEraseCntDiff == 0u) {
    pInst->MaxEraseCntDiff = FS_NOR_MAX_ERASE_CNT_DIFF;
  }
  //
  // Allocate memory for the internal tables.
  //
  {
    unsigned NumBytesL2PTable;
    unsigned NumBytesFreeMap;

    NumBytesL2PTable = _L2P_GetSize(pInst);
    NumBytesFreeMap  = (pInst->NumPhySectors + 7uL) / 8uL;
    FS_ALLOC_ZEROED_PTR(SEGGER_PTR2PTR(void *, &pInst->pLog2PhyTable), (I32)NumBytesL2PTable, "NOR_BM_SECTOR_MAP");               // MISRA deviation D:100d
    FS_ALLOC_ZEROED_PTR(SEGGER_PTR2PTR(void *, &pInst->pFreeMap),      (I32)NumBytesFreeMap,  "NOR_BM_FREE_MAP");                 // MISRA deviation D:100d
    if ((pInst->pLog2PhyTable == NULL) || (pInst->pFreeMap == NULL)) {
      return 1;                     // Error, could not allocate memory for the internal tables.
    }
  }
  //
  //  Initialize work block descriptors: Allocate memory and add them to free list
  //
  {
    unsigned   NumBytesAssign;
    unsigned   NumBytesIsWritten;
    unsigned   NumBytesWorkBlocks;
    U8       * pIsWritten;
    U8       * pAssign;

    NumBytesWorkBlocks = sizeof(NOR_BM_WORK_BLOCK) * NumWorkBlocksToAllocate;
    if (pInst->paWorkBlock == NULL) {
      pInst->paWorkBlock = SEGGER_PTR2PTR(NOR_BM_WORK_BLOCK, FS_ALLOC_ZEROED((I32)NumBytesWorkBlocks, "NOR_BM_WORK_BLOCK"));      // MISRA deviation D:100d
      if (pInst->paWorkBlock == NULL) {
        return 1;                   // Error, could not allocate memory for the work blocks.
      }
      FS_MEMSET(pInst->paWorkBlock, 0, NumBytesWorkBlocks);
    }
    NumBytesAssign    = _WB_GetAssignmentSize(pInst);
    NumBytesIsWritten = (LSectorsPerPSectorMax + 7uL) >> 3;
    pWorkBlock        = pInst->paWorkBlock;
    //
    // Allocate the memory for all the work blocks at once.
    //
    FS_ALLOC_ZEROED_PTR(SEGGER_PTR2PTR(void *, &pWorkBlock->paIsWritten), (I32)NumBytesIsWritten * (I32)NumWorkBlocksToAllocate, "NOR_BM_WB_IS_WRITTEN");     // MISRA deviation D:100d
    FS_ALLOC_ZEROED_PTR(SEGGER_PTR2PTR(void *, &pWorkBlock->paAssign),    (I32)NumBytesAssign * (I32)NumWorkBlocksToAllocate, "NOR_BM_WB_ASSIGN");            // MISRA deviation D:100d
    if ((pWorkBlock->paIsWritten == NULL) || (pWorkBlock->paAssign == NULL)) {
      return 1;                     // Error, could not allocate memory for the work blocks.
    }
    pIsWritten = pWorkBlock->paIsWritten;
    pAssign    = SEGGER_PTR2PTR(U8, pWorkBlock->paAssign);                                                                        // MISRA deviation D:100e
    NumBlocks  = NumWorkBlocksToAllocate;
    do {
      pWorkBlock->paIsWritten = pIsWritten;
      pWorkBlock->paAssign    = SEGGER_PTR2PTR(void, pAssign);                                                                    // MISRA deviation D:100e
      //
      // Not all the work block descriptors are available if the number of work blocks
      // specified in the device is smaller than the number of work blocks configured.
      //
      if (NumWorkBlocks != 0u) {
        _WB_AddToFreeList(pInst, pWorkBlock);
        NumWorkBlocks--;
      }
      ++pWorkBlock;
      pIsWritten += NumBytesIsWritten;
      pAssign    += NumBytesAssign;
    } while (--NumBlocks != 0u);
    pInst->NumBytesIsWritten = (U8)NumBytesIsWritten;
#if FS_NOR_OPTIMIZE_DATA_WRITE
    {
      NOR_BM_DATA_BLOCK * pDataBlock;
      unsigned            NumBytesDataBlock;

      //
      // We allocate the same number of data blocks as the number of configured work blocks
      // because this is typically the number of different blocks the file system has to write
      // to during an operation.
      //
      NumBytesDataBlock = sizeof(NOR_BM_DATA_BLOCK) * NumWorkBlocksToAllocate;
      if (pInst->paDataBlock == NULL) {
        pInst->paDataBlock = SEGGER_PTR2PTR(NOR_BM_DATA_BLOCK, FS_ALLOC_ZEROED((I32)NumBytesDataBlock, "NOR_BM_DATA_BLOCK"));     // MISRA deviation D:100e
        if (pInst->paDataBlock == NULL) {
          return 1;                 // Error, could not allocate memory.
        }
        FS_MEMSET(pInst->paDataBlock, 0, NumBytesDataBlock);
      }
      pDataBlock = pInst->paDataBlock;
      //
      // Allocate memory for the data status array. The memory is allocated here at once for all the data blocks.
      //
      FS_ALLOC_ZEROED_PTR(SEGGER_PTR2PTR(void *, &pDataBlock->paIsWritten), (I32)NumBytesIsWritten * (I32)NumWorkBlocksToAllocate, "NOR_BM_DB_IS_WRITTEN");   // MISRA deviation D:100d
      if (pDataBlock->paIsWritten == NULL) {
        return 1;                   // Error, could not allocate memory for the data blocks.
      }
      //
      // Put all the data blocks in the free list.
      //
      NumBlocks  = NumWorkBlocksToAllocate;
      pIsWritten = pDataBlock->paIsWritten;
      do {
        pDataBlock->paIsWritten = pIsWritten;
        _DB_AddToFreeList(pInst, pDataBlock);
        ++pDataBlock;
        pIsWritten += NumBytesIsWritten;
      } while (--NumBlocks != 0u);
    }
#endif // FS_NOR_OPTIMIZE_DATA_WRITE
}
  //
  // O.K., we read the physical sector headers and fill the tables
  //
  EraseCntMax          = 0;
  EraseCntMin          = ERASE_CNT_INVALID;
  NumBlocksEraseCntMin = 0;
  //
  // Initialize the information about the erase counts to driver instance.
  //
  pInst->EraseCntMax          = EraseCntMax;
  pInst->EraseCntMin          = EraseCntMin;
  pInst->NumBlocksEraseCntMin = NumBlocksEraseCntMin;
  //
  // Loop over all the physical sectors and collect information about the data stored in them.
  //
  for (iSector = PSI_FIRST_STORAGE_BLOCK; iSector < pInst->NumPhySectors; iSector++) {
    FS_MEMSET(&psh, 0xFF, sizeof(psh));
    r = _ReadPSH(pInst, iSector, &psh);
    if (r != 0) {
      (void)_PreErasePhySector(pInst, iSector);
      continue;
    }
    DataStat = _GetPhySectorDataStatNR(pInst, &psh);
    lbi      = _GetPhySectorLBI_NR(pInst, &psh, DataStat);
    EraseCnt = psh.EraseCnt;
#if FS_NOR_SUPPORT_FAIL_SAFE_ERASE
    //
    // Check if the phy. sector has been correctly erased.
    // If not, marked it as free and erase at a later point.
    //
    if (pInst->FailSafeErase != 0u) {
      U32 EraseSignature;

      EraseSignature = _GetPhySectorEraseSignature(&psh);
      if (EraseSignature != ERASE_SIGNATURE_VALID) {
        (void)_PreErasePhySector(pInst, iSector);
        continue;
      }
    }
#endif // FS_NOR_SUPPORT_FAIL_SAFE_ERASE
    //
    // Has this physical sector been used as work block ?
    //
    if (DataStat == DATA_STAT_WORK) {
      //
      // If the work block is an invalid one do a pre-erase.
      //
      if (lbi >= pInst->NumLogBlocks) {
        (void)_PreErasePhySector(pInst, iSector);
        continue;
      }
      if (pInst->pFirstWorkBlockFree != NULL) {
        //
        // Check if we already have a block with this LBI.
        // If we do, then we erase it and add it to the free list.
        //
        pWorkBlock = _FindWorkBlock(pInst, lbi);
        if (pWorkBlock != NULL) {
          FS_DEBUG_WARN((FS_MTYPE_DRIVER, "NOR_BM: _LowLevelMount: Found a work block with the same lbi."));
          (void)_PreErasePhySector(pInst, iSector);
          continue;
        }
        pWorkBlock      = _AllocWorkBlockDesc(pInst, lbi);
        pWorkBlock->psi = iSector;
      } else {
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_BM: _LowLevelMount: Found more work blocks than can be handled. Configuration changed?"));
        (void)_PreErasePhySector(pInst, iSector);
      }
      continue;
    }
    //
    // Is this a block containing valid data ?
    //
    if (DataStat == DATA_STAT_VALID) {
      if (lbi >= pInst->NumLogBlocks) {
        (void)_PreErasePhySector(pInst, iSector);
        continue;
      }
      psiPrev = _L2P_Read(pInst, lbi);
      if (psiPrev == 0u) {                                  // Has this lbi already been assigned ?
        _L2P_Write(pInst, lbi, iSector);                    // Add block to the translation table
        if ((EraseCnt < (U32)FS_NOR_MAX_ERASE_CNT) && (EraseCnt > EraseCntMax)) {
          EraseCntMax = EraseCnt;
        }
        continue;
      }
      if (_IsPhySectorDataMoreRecent(pInst, &psh, psiPrev) != 0) {
        (void)_PreErasePhySector(pInst, iSector);
      } else {
        (void)_PreErasePhySector(pInst, psiPrev);
        _L2P_Write(pInst, lbi, iSector);                    // Add block to the translation table
      }
      if ((EraseCntMin == ERASE_CNT_INVALID) || (EraseCnt < EraseCntMin)) {   // Collect information for the active wear leveling
        EraseCntMin          = EraseCnt;
        NumBlocksEraseCntMin = 1;
      } else {
        if (EraseCnt == EraseCntMin) {
          ++NumBlocksEraseCntMin;
        }
      }
      continue;
    }
    //
    // Check if the phy. sector has a valid data count or a valid LBI.
    // Typ. this is an indication that an unexpected reset interrupted
    // a block copy operation and the phy. sector contains inconsistent data.
    // If so mark the phy. sectors as invalid to make sure that it is erased before use.
    //
    DataCnt = _GetPhySectorDataCnt(pInst, &psh);
    if (DataStat == DATA_STAT_EMPTY) {
      if ((DataCnt != DATA_CNT_INVALID) || (lbi != LBI_INVALID)) {
        (void)_PreErasePhySector(pInst, iSector);
      }
    }
    //
    // Any other physical sectors are considered free.
    //
    _MarkPhySectorAsFree(pInst, iSector);
  }
  //
  // Save the information about the erase counts to driver instance.
  // This information is required later by the wear-leveling procedure.
  //
  pInst->EraseCntMax          = EraseCntMax;
  pInst->EraseCntMin          = EraseCntMin;
  pInst->NumBlocksEraseCntMin = NumBlocksEraseCntMin;
  //
  // Handle the work blocks we found
  //
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
      return 1;       // Error, failed to load work block.
    }
    pWorkBlock = pWorkBlockNext;
  }
  //
  // On debug builds we count here the number of valid sectors
  //
#if FS_NOR_ENABLE_STATS
  {
    U32      NumSectors;
    unsigned iBlock;

    for (iBlock = 0; iBlock < pInst->NumLogBlocks; ++iBlock) {
      NumSectors = _GetNumValidSectors(pInst, iBlock);
      pInst->StatCounters.NumValidSectors += NumSectors;
    }
  }
#endif
#if (FS_SUPPORT_TEST != 0) && ((FS_NOR_SUPPORT_CRC != 0) || (FS_NOR_SUPPORT_ECC != 0))
  r = _CheckLogSectors(pInst);
#else
  r = 0;
#endif
#if FS_NOR_SUPPORT_CLEAN
  pInst->IsCleanPhySector = 0;
  pInst->IsCleanWorkBlock = 0;
#endif // FS_NOR_SUPPORT_CLEAN
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR_BM: LL_MOUNT LogSectorSize: %u, NumLogBlocks: %u, NumWorkBlocks: %u", BytesPerSectorFromDevice, NumLogBlocksFromDevice, NumWorkBlocksFromDevice));
#if FS_NOR_SUPPORT_FAIL_SAFE_ERASE
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, ", FSErase: %d", pInst->FailSafeErase));
#endif // FS_NOR_SUPPORT_FAIL_SAFE_ERASE
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "\n"));
  return r;
}

/*********************************************************************
*
*       _LowLevelMountIfRequired
*
*  Function description
*    Mounts the NOR flash device if it is not already mounted.
*
*  Parameters
*    pInst      Driver instance.
*
*  Return value
*    ==0      OK, operation completed successfully.
*    !=0      An error occurred.
*/
static int _LowLevelMountIfRequired(NOR_BM_INST * pInst) {
  int r;

  if (pInst->IsLLMounted != 0u) {
    return 0;                   // OK, the NOR flash device is already mounted.
  }
  if (pInst->LLMountFailed != 0u) {
    return 1;                   // Error, we were not able to mount the NOR flash device and do not want to try again.
  }
  r = _LowLevelMount(pInst);
  if (r == 0) {
    pInst->IsLLMounted = 1;
  } else {
    pInst->LLMountFailed = 1;
  }
  return r;
}

#if (FS_NOR_SUPPORT_CRC != 0) || (FS_NOR_SUPPORT_ECC != 0)

/*********************************************************************
*
*       _ReadOneLogSectorWithCRCAndECC
*
*  Function description
*    Reads one logical sectors from storage device and validates
*    the data using CRC and ECC.
*
*  Parameters
*    pInst        Driver instance.
*    psi          Index of the physical sector that stores the logical sector (0-based).
*    srsi         Position of the logical sector in the physical sector (0-based).
*    pData        [OUT] Logical sector data.
*
*  Return value
*    ==0    OK, sector data read.
*    > 0    Error, could not read data.
*    < 0    Error, the sector data is invalid.
*/
static int _ReadOneLogSectorWithCRCAndECC(NOR_BM_INST * pInst, unsigned psi, unsigned srsi, void * pData) {
  int        r;
  unsigned   BytesPerSector;
  unsigned   DataStat;
  int        NumRetries;
  NOR_BM_LSH lsh;

  BytesPerSector = 1uL << pInst->ldBytesPerSector;
  FS_MEMSET(&lsh, 0xFF, sizeof(lsh));
  r = _ReadLSH(pInst, psi, srsi, &lsh);
  if (r != 0) {
    FS_MEMSET(pData, FS_NOR_READ_BUFFER_FILL_PATTERN, BytesPerSector);
    if (pInst->InvalidSectorError != 0u) {
      r = -1;                         // Error, the sector data is not present.
    }
  } else {
    DataStat = _GetLogSectorDataStat(pInst, &lsh);
    if (DataStat != DATA_STAT_VALID) {
      //
      // Data of logical sector has been invalidated.
      //
      FS_MEMSET(pData, FS_NOR_READ_BUFFER_FILL_PATTERN, BytesPerSector);
      if (pInst->InvalidSectorError != 0u) {
        r = -1;                       // Error, the sector data is not present.
      }
    } else {
      NumRetries = FS_NOR_NUM_READ_RETRIES;
      for (;;) {
        r = _ReadLogSectorData(pInst, psi, srsi, pData, 0, BytesPerSector);
        if (r == 0) {
#if FS_NOR_SUPPORT_ECC
          if (_IsECCEnabled(pInst) != 0) {
            unsigned   iBlock;
            unsigned   NumBlocks;
            int        Result;
            unsigned   BytesPerBlock;
            unsigned   ldBytesPerBlock;
            unsigned   ldBytesPerSector;
            U8       * pData8;

            ldBytesPerBlock  = pInst->pECCHookData->ldBytesPerBlock;
            ldBytesPerSector = pInst->ldBytesPerSector;
            BytesPerBlock    = 1uL << ldBytesPerBlock;
            NumBlocks        = 1uL << (ldBytesPerSector - ldBytesPerBlock);
            pData8           = SEGGER_PTR2PTR(U8, pData);                                                                         // MISRA deviation D:100e
            for (iBlock = 0; iBlock < NumBlocks; ++iBlock) {
              Result = pInst->pECC_API->pfApplyData(pInst, SEGGER_PTR2PTR(U32, pData8), lsh.aaECCSectorData[iBlock]);             // MISRA deviation D:100e
              if (Result < 0) {
                r = RESULT_ECC_ERROR;
              } else {
                UPDATE_NUM_BIT_ERRORS(pInst, Result);       // Update the statistical counters.
              }
              pData8 += BytesPerBlock;
            }
            if (r != 0) {
              FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_BM: _ReadOneLogSectorWithCRCAndECC: ECC check failed PSI: %lu, SRSI: %lu", psi, srsi));
            }
          }
#endif // FS_NOR_SUPPORT_ECC
#if FS_NOR_SUPPORT_CRC
#if FS_NOR_SUPPORT_ECC
          if (r == 0)
#endif // FS_NOR_SUPPORT_ECC
          {
            //
            // Verify the CRC of the read data.
            //
            if (_IsCRCEnabled(pInst) != 0) {
              U16 crcCalc;
              U16 crcRead;

              crcCalc = CRC_SECTOR_DATA_INIT;
              crcCalc = _pCRC_API->pfCalcData(SEGGER_PTR2PTR(U8, pData), BytesPerSector, crcCalc);                                // MISRA deviation D:100e
              crcRead = lsh.crcSectorData;
              if (crcCalc != crcRead) {
                FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_BM: _ReadOneLogSectorWithCRCAndECC: CRC check failed PSI: %lu, SRSI: %lu, Calc: 0x%02x, Read: 0x%02x", psi, srsi, crcCalc, crcRead));
                r = RESULT_CRC_ERROR;
              }
            }
          }
#endif // FS_NOR_SUPPORT_CRC
        }
        if (r == 0) {
          break;                        // OK, data read successfully.
        }
        FS_MEMSET(pData, FS_NOR_READ_BUFFER_FILL_PATTERN, BytesPerSector);
        if (NumRetries-- == 0) {
          break;                        // Error, no more read retries.
        }
        IF_STATS(pInst->StatCounters.NumReadRetries++);
      }
    }
  }
  return r;
}

#endif // FS_NOR_SUPPORT_CRC != 0 || FS_NOR_SUPPORT_ECC != 0

/*********************************************************************
*
*       _ReadOneLogSectorFast
*
*  Function description
*    Reads one logical sectors from storage device (fast version without CRC and ECC).
*
*  Parameters
*    pInst        Driver instance.
*    psi          Index of the physical sector that stores the logical sector (0-based).
*    srsi         Position of the logical sector in the physical sector (0-based).
*    pData        [OUT] Logical sector data.
*
*  Return value
*    ==0    OK, sector data read.
*    > 0    Error, could not read data.
*    < 0    Error, the sector data is invalid.
*/
static int _ReadOneLogSectorFast(NOR_BM_INST * pInst, unsigned psi, unsigned srsi, void * pData) {
  int      r;
  unsigned BytesPerSector;
  unsigned DataStat;
  int      NumRetries;

  BytesPerSector = 1uL << pInst->ldBytesPerSector;
  r = _ReadLogSectorDataStat(pInst, psi, srsi, &DataStat);
  if (r != 0) {
    FS_MEMSET(pData, FS_NOR_READ_BUFFER_FILL_PATTERN, BytesPerSector);
  } else {
    if (DataStat == DATA_STAT_VALID) {
      NumRetries = FS_NOR_NUM_READ_RETRIES;
      for (;;) {
        r = _ReadLogSectorData(pInst, psi, srsi, pData, 0, BytesPerSector);
        if (r == 0) {
          break;                        // OK, data read successfully.
        }
        if (NumRetries-- == 0) {
          break;                        // Error, no more read retries.
        }
        IF_STATS(pInst->StatCounters.NumReadRetries++);
      }
    } else {
      //
      // Data of logical sector has been invalidated.
      //
      FS_MEMSET(pData, FS_NOR_READ_BUFFER_FILL_PATTERN, BytesPerSector);
      if (pInst->InvalidSectorError != 0u) {
        r = -1;                         // Error, the sector data is not present.
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _ReadOneLogSector
*
*  Function description
*    Reads one logical sector from storage device.
*
*  Parameters
*    pInst            Driver instance. It cannot be NULL.
*    LogSectorIndex   Index of the logical sector to be read.
*    pData            [OUT] Data of the logical sector read from storage. It cannot be NULL.
*
*  Return value
*    ==0    Data successfully read.
*    !=0    An error has occurred.
*
*  Additional information
*    There are 3 possibilities:
*      a) Data is located in work block.
*      b) There is a physical sector assigned to the logical block -> Read from hardware
*      c) There is a no physical block assigned to this logical block.
*         This means data has never been written to storage. Fill data with FS_NOR_READ_BUFFER_FILL_PATTERN.
*/
static int _ReadOneLogSector(NOR_BM_INST * pInst, unsigned LogSectorIndex, void * pData) {
  int                 r;
  unsigned            lbi;
  unsigned            psi;
  unsigned            srsi;
  unsigned            brsi;
  unsigned            BytesPerSector;
  NOR_BM_WORK_BLOCK * pWorkBlock;

  //
  // Physical block index is taken from Log2Phy table or is work block
  //
  lbi        = _LogSectorIndex2LogBlockIndex(pInst, LogSectorIndex, &brsi);
  psi        = _L2P_Read(pInst, lbi);
  srsi       = brsi;
  pWorkBlock = _FindWorkBlock(pInst, lbi);
  if (pWorkBlock != NULL) {
    unsigned u;

    u = _brsi2srsi(pInst, pWorkBlock, brsi);
    if (u != BRSI_INVALID) {
      psi  = pWorkBlock->psi;
      srsi = u;
    }
  }
  //
  // Get data from NOR flash device.
  //
  r = 0;
  BytesPerSector = 1uL << pInst->ldBytesPerSector;
  if (psi == 0u) {
    //
    // Logical sector not written yet.
    //
    FS_MEMSET(pData, FS_NOR_READ_BUFFER_FILL_PATTERN, BytesPerSector);
    if (pInst->InvalidSectorError != 0u) {
      r = 1;                                // Error, the sector data is not present.
    }
  } else {
    //
    // Read the data from the NOR flash device.
    //
#if (FS_NOR_SUPPORT_CRC != 0) || (FS_NOR_SUPPORT_ECC != 0)
    if ((_IsCRCEnabled(pInst) != 0) || (_IsECCEnabled(pInst) != 0)) {
      r = _ReadOneLogSectorWithCRCAndECC(pInst, psi, srsi, pData);
    } else {
      r = _ReadOneLogSectorFast(pInst, psi, srsi, pData);
    }
#else
    r = _ReadOneLogSectorFast(pInst, psi, srsi, pData);
#endif
    if (r != 0) {
      if (r > 0) {
        //
        // Reading the value of an invalid sector is not a fatal error.
        //
        _OnFatalError(pInst, r, psi);
      }
      r = 1;
    }
  }
  return r;
}

#if FS_NOR_OPTIMIZE_DATA_WRITE

/*********************************************************************
*
*       _FindDataBlock
*
*  Function description
*    Tries to locate a data block descriptor by the physical sector index.
*
*  Parameters
*    pInst        Driver instance. It cannot be NULL.
*    psi          Index of the physical sector assigned to the data block.
*
*  Return value
*    !=NULL       Data block descriptor.
*    ==NULL       Data block not found.
*/
static NOR_BM_DATA_BLOCK * _FindDataBlock(const NOR_BM_INST * pInst, unsigned psi) {
  NOR_BM_DATA_BLOCK * pDataBlock;

  //
  // Iterate over used-list
  //
  pDataBlock = pInst->pFirstDataBlockInUse;
  for (;;) {
    if (pDataBlock == NULL) {
      break;                          // No match
    }
    if (pDataBlock->psi == psi) {
      break;                          // Found it
    }
    pDataBlock = pDataBlock->pNext;
  }
  return pDataBlock;
}

/*********************************************************************
*
*       _LoadDataBlock
*
*  Function description
*    Loads information about a data block from the NOR flash device.
*
*  Parameters
*    pInst        Driver instance.
*    pDataBlock   [IN] Index of the physical sector assigned to the data block.
*                 [OUT] Information about the data block.
*
*  Return value
*    ==0      OK, data block loaded successfully.
*    !=0      An error occurred.
*/
static int _LoadDataBlock(NOR_BM_INST * pInst, const NOR_BM_DATA_BLOCK * pDataBlock) {
  unsigned   NumSectors;
  unsigned   iSector;
  unsigned   brsi;
  unsigned   psi;
  unsigned   DataStat;
  NOR_BM_LSH lsh;
  int        r;

  psi        = pDataBlock->psi;
  NumSectors = pInst->LSectorsPerPSector;
  //
  // Iterate over all logical sectors and read the header information in order
  // to find out if a sector contains any data and if the data is valid or not.
  //
  for (iSector = 0; iSector < NumSectors; iSector++) {
    FS_MEMSET(&lsh, 0xFF, sizeof(lsh));
    r = _ReadLSH(pInst, psi, iSector, &lsh);
    if (r != 0) {
      //
      // Read or CRC error. Mark sector as used to prevent that we write any data to it.
      //
      _DB_MarkSectorAsUsed(pDataBlock, iSector);
    } else {
      DataStat = _GetLogSectorDataStat(pInst, &lsh);
      if (DataStat == DATA_STAT_EMPTY) {
        brsi = lsh.brsi;
        if (brsi != BRSI_INVALID) {
          //
          // The sector is not blank even when the data status in the header indicates this.
          // This can happen if a sector write operation was interrupted by a unexpected
          // reset. In this case, the BRSI has a valid value because it was updated before
          // the NOR driver started to write the sector data. We mark the sector as containing
          // invalid data so that we do write any new data into it.
          //
          (void)_MarkLogSectorAsInvalid(pInst, psi, iSector);
          _DB_MarkSectorAsUsed(pDataBlock, iSector);
        }
        continue;
      }
      //
      // Found a sector that contains data. Note that for a data block we do not need any
      // assignment list because the logical sectors are always stored to their native position.
      //
      _DB_MarkSectorAsUsed(pDataBlock, iSector);
    }
  }
  r = 0;
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR_BM: LOAD_DATA_BLOCK PSI: %u, r: %d\n", pDataBlock->psi, r));
  return r;
}

/*********************************************************************
*
*       _AllocDataBlockDesc
*
*  Function description
*    Allocates a data block descriptor.
*
*  Parameters
*    pInst      Driver instance.
*
*  Return value
*    !=NULL     OK, data block descriptor allocated.
*    ==NULL     No more data block descriptors available.
*/
static NOR_BM_DATA_BLOCK * _AllocDataBlockDesc(NOR_BM_INST * pInst) {
  NOR_BM_DATA_BLOCK * pDataBlock;
  unsigned            NumBytesIsWritten;

  //
  // Check if a free data block is available.
  //
  pDataBlock = pInst->pFirstDataBlockFree;
  if (pDataBlock != NULL) {
    //
    // Initialize the data block descriptor and mark it as in use.
    //
    NumBytesIsWritten = pInst->NumBytesIsWritten;
    _DB_RemoveFromFreeList(pInst, pDataBlock);
    _DB_AddToUsedList(pInst, pDataBlock);
    FS_MEMSET(pDataBlock->paIsWritten, 0, NumBytesIsWritten);
  }
  return pDataBlock;
}

/*********************************************************************
*
*       _FreeDataBlockDescLRU
*
*  Function description
*    Frees the least recently used data block descriptor.
*
*  Parameters
*    pInst      Driver instance.
*/
static void _FreeDataBlockDescLRU(NOR_BM_INST * pInst) {
  NOR_BM_DATA_BLOCK * pDataBlock;

  pDataBlock = pInst->pFirstDataBlockInUse;
  if (pDataBlock != NULL) {
    while (pDataBlock->pNext != NULL) {
      pDataBlock = pDataBlock->pNext;
    }
    _DB_RemoveFromUsedList(pInst, pDataBlock);
    _DB_AddToFreeList(pInst, pDataBlock);
  }
}

/*********************************************************************
*
*       _FreeDataBlockDesc
*
*  Function description
*    Frees a data block descriptor.
*
*  Parameters
*    pInst        Driver instance.
*    pDataBlock   Data block descriptor to be freed.
*/
static void _FreeDataBlockDesc(NOR_BM_INST * pInst, NOR_BM_DATA_BLOCK * pDataBlock) {
  _DB_RemoveFromUsedList(pInst, pDataBlock);
  _DB_AddToFreeList(pInst, pDataBlock);
}

/*********************************************************************
*
*       _MarkDataBlockAsMRU
*
*  Function description
*    Marks the given data block as most-recently used.
*
*  Parameters
*    pInst        Driver instance.
*    pDataBlock   Data block to be marked as MRU.
*
*  Additional information
*    This is operation makes sure that the least recently used
*    data block is removed form the list when a new one is required.
*/
static void _MarkDataBlockAsMRU(NOR_BM_INST * pInst, NOR_BM_DATA_BLOCK * pDataBlock) {
  if (pDataBlock != pInst->pFirstDataBlockInUse) {
    _DB_RemoveFromUsedList(pInst, pDataBlock);
    _DB_AddToUsedList(pInst, pDataBlock);
  }
}

/*********************************************************************
*
*       _AllocDataBlock
*
*  Function description
*    Allocates a new data block.
*
*  Parameters
*    pInst        Driver instance. It cannot be NULL.
*    lbi          Logical block index assigned to data block.
*
*  Return values
*    !=NULL   Pointer to allocated data block.
*    ==NULL   An error occurred.
*/
static NOR_BM_DATA_BLOCK * _AllocDataBlock(NOR_BM_INST * pInst, unsigned lbi) {
  NOR_BM_DATA_BLOCK * pDataBlock;
  NOR_BM_WORK_BLOCK * pWorkBlock;
  U32                 EraseCnt;
  unsigned            psi;
  int                 r;
  U8                  DataCnt;
  unsigned            psiWork;

  //
  // Get an empty block to write to.
  //
  psi = _AllocErasedBlock(pInst, &EraseCnt);
  if (psi == 0u) {
    return NULL;                    // Error, could not erase phy. sector (occurs only in case of a fatal error).
  }
  //
  // Try to get a free data block descriptor.
  //
  pDataBlock = _AllocDataBlockDesc(pInst);
  if (pDataBlock == NULL) {
    //
    // No more data blocks free. Remove the least recently used data block from the list.
    //
    _FreeDataBlockDescLRU(pInst);
    pDataBlock = _AllocDataBlockDesc(pInst);
  }
  if (pDataBlock == NULL) {
    return NULL;                    // Error, no more available data block descriptors.
  }
  //
  // New data block allocated. Check if there is a associated work block
  // and if so read its data count. We have to decrease the data count
  // by 1 and to store it to the header of the work block to make sure
  // that we discard the old data block when the in-place conversion
  // of the work block is interrupted by a power failure.
  //
  DataCnt = 0;
  pWorkBlock = _FindWorkBlock(pInst, lbi);
  if (pWorkBlock != NULL) {
    psiWork = pWorkBlock->psi;
    (void)_ReadPhySectorDataCnt(pInst, psiWork, &DataCnt);
    --DataCnt;
  }
  pDataBlock->psi = psi;
  r = _MarkAsDataBlock(pInst, psi, lbi, EraseCnt, DataCnt, 0);
  if (r != 0) {
    //
    // Marking the data block failed. Free the allocated physical sector
    // and the data block descriptor.
    //
    _MarkPhySectorAsFree(pInst, psi);
    _DB_RemoveFromUsedList(pInst, pDataBlock);
    _DB_AddToFreeList(pInst, pDataBlock);
    return NULL;                    // Error, could not mark the allocated block as data block.
  }
  _L2P_Write(pInst, lbi, psi);      // Update the mapping table.
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR_BM: ALLOC_DATA_BLOCK PSI: %u, LBI: %u\n", psi, lbi));
  return pDataBlock;
}

/*********************************************************************
*
*       _RecoverDataBlock
*
*  Function description
*    Moves the contents of a data block to a different physical sector.
*
*  Parameters
*    pInst        Driver instance. It cannot be NULL.
*    pDataBlock   Data block to be relocated. It cannot be NULL.
*
*  Return values
*    ==0      OK, operation successful.
*    !=0      An error occurred.
*/
static int _RecoverDataBlock(NOR_BM_INST * pInst, const NOR_BM_DATA_BLOCK * pDataBlock) {
  int      r;
  U32      EraseCnt;
  unsigned psiDest;
  unsigned psiSrc;

  //
  // Get an empty block to write to.
  //
  psiDest = _AllocErasedBlock(pInst, &EraseCnt);
  if (psiDest == 0u) {
    return 1;                    // Error, could not erase phy. sector (occurs only in case of a fatal error).
  }
  //
  // Move the contents of the data block to the newly allocated block.
  // Note that _MoveDataBlock() removes the data block from the list
  // of used data blocks so there is no need to do this here explicitly.
  //
  psiSrc = pDataBlock->psi;
  r = _MoveDataBlock(pInst, psiSrc, psiDest, EraseCnt);
  if (r != 0) {
    _OnFatalError(pInst, r, psiDest);
  }
  return r;
}

/*********************************************************************
*
*        _WriteLogSectorBRSIWithCRCAndECC
*
*  Function description
*    Writes the BRSI of a logical sector to storage.
*
*  Parameters
*    pInst            Driver instance.
*    PhySectorIndex   Index of the physical sector that stores the logical sector.
*    srsi             Index of the logical sector in the physical sector.
*    brsi             Index of the logical sector in the data block.
*    pDataCheck       CRC and ECC of the sector data.
*
*  Return value
*    ==0      OK, data stored successfully.
*    !=0      An error occurred.
*
*  Additional information
*    This function is called during a sector write operation.
*    It assumes that the support for incremental write operations
*    is enabled.
*/
static int _WriteLogSectorBRSIWithCRCAndECC(NOR_BM_INST * pInst, unsigned PhySectorIndex, unsigned srsi, unsigned brsi, const DATA_CHECK * pDataCheck) {
  int        r;
  NOR_BM_LSH lsh;

  INIT_LSH_DATA_RANGE();
  FS_MEMSET(&lsh, 0xFF, sizeof(lsh));
  _SetLogSectorDataStat(pInst, &lsh, DATA_STAT_VALID);
  lsh.brsi = (U16)brsi;
  UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_LSH, brsi), sizeof(lsh.brsi));
#if (FS_NOR_SUPPORT_CRC != 0) || (FS_NOR_SUPPORT_ECC != 0)
  r = _CalcStoreLSHWithCRCAndECC(pInst, &lsh, pDataCheck);
  if (r == 0) {
#if FS_NOR_SUPPORT_ECC
    if (_IsECCEnabled(pInst) != 0) {
      //
      // With the ECC enabled we use the ECC status to indicate that the data of the LSH is valid.
      // The ECC status is set here to invalid and is set to valid in the operation that marks the
      // logical sector as valid.
      //
      _SetLSH_ECCToEmpty(&lsh);
      UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_LSH, DataStat), sizeof(lsh.DataStat));
      r = _WriteLSH(pInst, PhySectorIndex, srsi, &lsh);
    } else
#endif // FS_NOR_SUPPORT_ECC
    {
      //
      // Update the data in the logical sector header with the data status information
      // set to default value (i.e. log. sector empty) to indicate that the information
      // is not complete. The data status is updated in a different step via a separate
      // write operation in order to make sure that the operation is fail-safe.
      //
      _SetLogSectorDataStat(pInst, &lsh, DATA_STAT_EMPTY);
      r = _WriteLSH(pInst, PhySectorIndex, srsi, &lsh);
    }
  }
#else
  FS_USE_PARA(pDataCheck);
  //
  // Update the data in the logical sector header with the data status information
  // set to default value (i.e. log. sector empty) to indicate that the information
  // is not complete. The data status is updated in a different step via a separate write
  // operation in order to make sure that the operation is fail-safe.
  //
  _SetLogSectorDataStat(pInst, &lsh, DATA_STAT_EMPTY);
  r = _WriteLSH(pInst, PhySectorIndex, srsi, &lsh);
#endif // (FS_NOR_SUPPORT_CRC != 0) || (FS_NOR_SUPPORT_ECC != 0)
  INIT_LSH_DATA_RANGE();
  return r;
}

#if (FS_NOR_CAN_REWRITE == 0)

/*********************************************************************
*
*        _WriteLogSectorBRSI_NRWithCRCAndECC
*
*  Function description
*    Writes the BRSI of a logical sector to storage.
*
*  Parameters
*    pInst            Driver instance.
*    PhySectorIndex   Index of the physical sector that stores the logical sector.
*    srsi             Index of the logical sector in the physical sector.
*    brsi             Index of the logical sector in the data block. Must be a valid value.
*    pDataCheck       CRC and ECC of the sector data.
*
*  Return value
*    ==0      OK, data stored successfully.
*    !=0      An error occurred.
*
*  Additional information
*    This function assumes that the support for incremental write
*    operations is disabled (i.e. _IsRewriteSupported(pInst) == 0 is true)
*/
static int _WriteLogSectorBRSI_NRWithCRCAndECC(NOR_BM_INST * pInst, unsigned PhySectorIndex, unsigned srsi, unsigned brsi, const DATA_CHECK * pDataCheck) {
  int        r;
  NOR_BM_LSH lsh;

  FS_MEMSET(&lsh, 0xFF, sizeof(lsh));
  r = _ReadLSH(pInst, PhySectorIndex, srsi, &lsh);
  if (r == 0) {
    INIT_LSH_DATA_RANGE();
    lsh.brsi = (U16)brsi;
    UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_LSH, brsi), sizeof(lsh.brsi));
#if (FS_NOR_SUPPORT_CRC != 0) || (FS_NOR_SUPPORT_ECC != 0)
    r = _CalcStoreLSHWithCRCAndECC(pInst, &lsh, pDataCheck);
    if (r == 0) {
      r = _WriteLSH(pInst, PhySectorIndex, srsi, &lsh);
    }
#else
    FS_USE_PARA(pDataCheck);
    r = _WriteLSH(pInst, PhySectorIndex, srsi, &lsh);
#endif // (FS_NOR_SUPPORT_CRC != 0) || (FS_NOR_SUPPORT_ECC != 0)
  }
  INIT_LSH_DATA_RANGE();
  return r;
}

#endif // FS_NOR_CAN_REWRITE == 0

/*********************************************************************
*
*        _WriteLogSectorBRSI_NC
*
*  Function description
*    Writes the BRSI of a logical sector to storage.
*
*  Parameters
*    pInst            Driver instance.
*    PhySectorIndex   Index of the physical sector that stores the logical sector.
*    srsi             Index of the logical sector in the physical sector.
*    brsi             Index of the logical sector in the data block.
*
*  Return value
*    ==0    OK, data marked as valid.
*    !=0    An error occurred.
*
*  Additional information
*    This function assumes that the checking via CRC and ECC is disabled.
*/
static int _WriteLogSectorBRSI_NC(NOR_BM_INST * pInst, unsigned PhySectorIndex, unsigned srsi, unsigned brsi) {
  int r;

#if FS_NOR_CAN_REWRITE
  r = _WriteLogSectorBRSIFast(pInst, PhySectorIndex, srsi, (U16)brsi);
#else
  if (_IsRewriteSupported(pInst) != 0) {
    r = _WriteLogSectorBRSIFast(pInst, PhySectorIndex, srsi, (U16)brsi);
  } else {
    r = _WriteLogSectorBRSISlow(pInst, PhySectorIndex, srsi, (U16)brsi);
  }
#endif // FS_NOR_CAN_REWRITE
  return r;
}

/*********************************************************************
*
*       _WriteLogSectorBRSI
*
*  Function description
*    Writes the BRSI of a logical sector to storage.
*
*  Parameters
*    pInst            Driver instance.
*    PhySectorIndex   Index of the physical sector that stores the logical sector.
*    srsi             Index of the logical sector in the physical sector.
*    brsi             Index of the logical sector in the data block.
*    pDataCheck       CRC and ECC of the sector data.
*
*  Return value
*    ==0      OK, BRSI modified successfully.
*    !=0      An error occurred.
*/
static int _WriteLogSectorBRSI(NOR_BM_INST * pInst, unsigned PhySectorIndex, unsigned srsi, unsigned brsi, const DATA_CHECK * pDataCheck) {
  int r;

#if FS_NOR_CAN_REWRITE
  if ((_IsCRCEnabled(pInst) != 0) || (_IsECCEnabled(pInst) != 0)) {
    r = _WriteLogSectorBRSIWithCRCAndECC(pInst, PhySectorIndex, srsi, brsi, pDataCheck);
  } else {
    r = _WriteLogSectorBRSI_NC(pInst, PhySectorIndex, srsi, brsi);
  }
#else
  if ((_IsCRCEnabled(pInst) == 0) && (_IsECCEnabled(pInst) == 0)) {
    r = _WriteLogSectorBRSI_NC(pInst, PhySectorIndex, srsi, brsi);
  } else {
    if (_IsRewriteSupported(pInst) != 0) {
      r = _WriteLogSectorBRSIWithCRCAndECC(pInst, PhySectorIndex, srsi, brsi, pDataCheck);
    } else {
      r = _WriteLogSectorBRSI_NRWithCRCAndECC(pInst, PhySectorIndex, srsi, brsi, pDataCheck);
    }
  }
#endif // FS_NOR_CAN_REWRITE
  return r;
}

/*********************************************************************
*
*       _CheckWriteToDataBlock
*
*  Function description
*    Checks if data can be written to the specified position
*    on a data block.
*
*  Parameters
*    pInst        Driver instance.
*    psi          Index of the physical sector where the data block is stored.
*    brsi         Index of the logical sector to be written (relative to logical block).
*
*  Return value
*    !=NULL     OK, data can be written to data block (data block descriptor).
*    ==NULL     Data cannot be written to data block.
*
*  Additional information
*    The sector data can be written to a data block if the logical sector
*    on which the data has to be stored is empty.
*/
static NOR_BM_DATA_BLOCK * _CheckWriteToDataBlock(NOR_BM_INST * pInst, unsigned psi, unsigned brsi) {
  int                 r;
  NOR_BM_DATA_BLOCK * pDataBlock;

  //
  // Try to get the information about the last written physical sector from the internal list.
  //
  pDataBlock = _FindDataBlock(pInst, psi);
  if (pDataBlock == NULL) {
    //
    // The information about which logical sector can be written to
    // is not cached and has to be calculated.
    // First, try to get a free data block descriptor.
    //
    pDataBlock = _AllocDataBlockDesc(pInst);
    if (pDataBlock == NULL) {
      //
      // No more data blocks free. Remove the least recently used data block from the list.
      //
      _FreeDataBlockDescLRU(pInst);
      pDataBlock = _AllocDataBlockDesc(pInst);
    }
    if (pDataBlock != NULL) {
      //
      // OK, data block descriptor allocated.
      // Load the information about the usage status
      // of the logical sectors from the NOR flash device.
      //
      pDataBlock->psi = psi;
      r = _LoadDataBlock(pInst, pDataBlock);
      if (r != 0) {
        _FreeDataBlockDesc(pInst, pDataBlock);
        pDataBlock = NULL;                              // Error, could not load data block from storage.
      }
    }
  }
  if (pDataBlock != NULL) {
    if (_DB_IsSectorUsed(pDataBlock, brsi) != 0) {      // Is logical sector used?
      pDataBlock = NULL;                                // It is not possible to write to data block.
    }
  }
  return pDataBlock;
}

/*********************************************************************
*
*       _TryWriteOneLogSectorToDataBlock
*
*  Function description
*    Tries writing one logical sector to a data block.
*
*  Parameters
*    pInst            Driver instance. It cannot be NULL.
*    LogSectorIndex   Index of the logical sector to be written.
*    pData            [IN] Data of the logical sector to be written. It cannot be NULL.
*
*  Return value
*    ==0      Data successfully written.
*    !=0      An error has occurred or the data cannot be written to a data block.
*
*  Notes
*    (1) Typically, after a write failure any other following write operation
*        to the same data block will also fail. For this reason, we copy
*        the existing data to a different physical sector to prevent any further
*        write errors.
*/
static int _TryWriteOneLogSectorToDataBlock(NOR_BM_INST * pInst, U32 LogSectorIndex, const void * pData) {
  int                 r;
  unsigned            lbi;
  unsigned            brsi;
  unsigned            srsi;
  unsigned            psi;
  unsigned            NumBytes;
  NOR_BM_WORK_BLOCK * pWorkBlock;
  NOR_BM_DATA_BLOCK * pDataBlock;
  int                 NumRetries;
  DATA_CHECK        * pDataCheck;
#if (FS_NOR_SUPPORT_CRC != 0) || (FS_NOR_SUPPORT_ECC != 0)
  DATA_CHECK          DataCheck;
#endif // (FS_NOR_SUPPORT_CRC != 0) || (FS_NOR_SUPPORT_ECC != 0)

  //
  // Initialize local variables.
  //
  lbi        = _LogSectorIndex2LogBlockIndex(pInst, LogSectorIndex, &brsi);
  NumRetries = 0;
  pDataCheck = NULL;
  //
  // Check if the logical sector is stored in a work block. If yes, then we do not write to a data block
  // because we will not be able to tell apart which version of the sector data is more recent when
  // the NOR flash device is mounted.
  //
  pWorkBlock = _FindWorkBlock(pInst, lbi);
  if (pWorkBlock != NULL) {
    srsi = _brsi2srsi(pInst, pWorkBlock, brsi);
    if (srsi != BRSI_INVALID) {
      return 1;                                           // OK, the sector data has to be written to a work block.
    }
  }
  //
  // Repeat the operation until either we are able to successfully write the data or a fatal error occurs.
  //
  for (;;) {
    if (NumRetries++ > FS_NOR_NUM_WRITE_RETRIES) {
      return 1;                                           // Error could not write data.
    }
    psi = _L2P_Read(pInst, lbi);
    //
    // Search for a data block to write to.
    //
    if (psi == 0u) {
      //
      // Data block does not exist. Create a new data block on the NOR flash device.
      //
      pDataBlock = _AllocDataBlock(pInst, lbi);
    } else {
      //
      // Data block exists. Write the sector data to it if possible.
      //
      pDataBlock = _CheckWriteToDataBlock(pInst, psi, brsi);
    }
    if (pDataBlock == NULL) {
      r = 1;                                              // Error, could not allocate of find a data block.
      break;
    }
    //
    // Write the sector data to the data block.
    //
    psi      = pDataBlock->psi;
    srsi     = brsi;                                      // On data blocks, the sector data is always written to its native position.
    NumBytes = 1uL << pInst->ldBytesPerSector;
    //
    // Calculate the parity checksums because we have to
    // store them to the NOR flash together with the BRSI.
    //
#if (FS_NOR_SUPPORT_CRC != 0) || (FS_NOR_SUPPORT_ECC != 0)
    FS_MEMSET(&DataCheck, 0xFF, sizeof(DataCheck));
    pDataCheck = &DataCheck;
#if FS_NOR_SUPPORT_CRC
    if (_IsCRCEnabled(pInst) != 0) {
      DataCheck.crc = CRC_SECTOR_DATA_INIT;
      DataCheck.crc = _pCRC_API->pfCalcData(SEGGER_CONSTPTR2PTR(const U8, pData), NumBytes, DataCheck.crc);         // MISRA deviation D:100e
    }
#endif // FS_NOR_SUPPORT_CRC
#if FS_NOR_SUPPORT_ECC
    if (_IsECCEnabled(pInst) != 0) {
      unsigned   iBlock;
      unsigned   NumBlocks;
      unsigned   ldBytesPerBlock;
      unsigned   ldBytesPerSector;
      unsigned   BytesPerBlock;
      const U8 * pData8;

      //
      // Calculate the ECC for each ECC block and store it to LSH.
      //
      ldBytesPerBlock  = pInst->pECCHookData->ldBytesPerBlock;
      ldBytesPerSector = pInst->ldBytesPerSector;
      BytesPerBlock    = 1uL << ldBytesPerBlock;
      NumBlocks        = 1uL << (ldBytesPerSector - ldBytesPerBlock);
      pData8           = SEGGER_CONSTPTR2PTR(const U8, pData);                                                      // MISRA deviation D:100e
      for (iBlock = 0; iBlock < NumBlocks; ++iBlock) {
        pInst->pECC_API->pfCalcData(pInst, SEGGER_CONSTPTR2PTR(const U32, pData8), DataCheck.aaECC[iBlock]);        // MISRA deviation D:100e
        pData8 += BytesPerBlock;
      }
    }
#endif // FS_NOR_SUPPORT_ECC
#endif // FS_NOR_SUPPORT_CRC != 0 || FS_NOR_SUPPORT_ECC != 0
    //
    // Update the BRSI on the storage before writing the sector data.
    // This helps us detect an interrupted write operation by only
    // evaluating the header of the logical sector instead of checking
    // if the contents of the logical sector is blank.
    //
    r = _WriteLogSectorBRSI(pInst, psi, srsi, srsi, pDataCheck);
    if (r != 0) {
      r = _RecoverDataBlock(pInst, pDataBlock);           // Note 1
      if (r != 0) {
        return r;                                         // Error, could move data block.
      }
      continue;                                           // Try again with a different data block.
    }

    //
    // Fail-safe TP. At this point the BRSI is written but the sector data is not updated yet.
    //
    CALL_TEST_HOOK_FAIL_SAFE(pInst->Unit);

    //
    // OK, BRSI written successfully. Write the sector data.
    //
    r = _WriteLogSectorData(pInst, psi, srsi, pData, 0, NumBytes);
    if (r != 0) {
      r = _RecoverDataBlock(pInst, pDataBlock);           // Note 1
      if (r != 0) {
        return r;                                         // Error, could move data block.
      }
      continue;                                           // Try again with a different data block.
    }

    //
    // Fail-safe TP. At this point the sector data is written but the header is not updated yet.
    //
    CALL_TEST_HOOK_FAIL_SAFE(pInst->Unit);

    //
    // Now mark the sector data as valid by updating the LSH.
    // Note that the BRSI is not updated because it was already
    // stored at the beginning of the write operation.
    //
    r = _MarkLogSectorAsValid(pInst, psi, srsi, BRSI_INVALID, pDataCheck);
    if (r != 0) {
      r = _RecoverDataBlock(pInst, pDataBlock);           // Note 1
      if (r != 0) {
        return r;                                         // Error, could move data block.
      }
      continue;                                           // Try again with a different data block.
    }
    _MarkDataBlockAsMRU(pInst, pDataBlock);
    _DB_MarkSectorAsUsed(pDataBlock, srsi);               // Mark sector as in use.
    IF_STATS(pInst->StatCounters.NumValidSectors++);
    break;
  }
  return r;
}

#endif // FS_NOR_OPTIMIZE_DATA_WRITE

/*********************************************************************
*
*       _WriteOneLogSectorToWorkBlock
*
*  Function description
*    Writes one logical sector to a work block.
*
*  Parameters
*    pInst            Driver instance. It cannot be NULL.
*    LogSectorIndex   Index of the logical sector to be written.
*    pData            [IN] Data of the logical sector to be written. It cannot be NULL.
*
*  Return value
*    ==0      Data successfully written.
*    !=0      An error has occurred.
*/
static int _WriteOneLogSectorToWorkBlock(NOR_BM_INST * pInst, U32 LogSectorIndex, const void * pData) {
  int                 r;
  unsigned            lbi;
  unsigned            brsi;
  unsigned            srsi;
  unsigned            srsiPrev;
  unsigned            psiWork;
  unsigned            NumBytes;
  NOR_BM_WORK_BLOCK * pWorkBlock;
  int                 NumRetries;
  DATA_CHECK        * pDataCheck;
#if (FS_NOR_SUPPORT_CRC != 0) || (FS_NOR_SUPPORT_ECC != 0)
  DATA_CHECK          DataCheck;
#endif // (FS_NOR_SUPPORT_CRC != 0) || (FS_NOR_SUPPORT_ECC != 0)

  lbi        = _LogSectorIndex2LogBlockIndex(pInst, LogSectorIndex, &brsi);
  srsi       = ~0u;
  NumRetries = 0;
  pDataCheck = NULL;
  //
  // Repeat the operation until either we are able to successfully write the data or a fatal error occurs.
  //
  for (;;) {
    if (NumRetries++ > FS_NOR_NUM_WRITE_RETRIES) {
      return 1;                           // Error could not write data.
    }
    //
    // Find (or create) a work block and the sector to be used in it.
    //
    pWorkBlock = _FindWorkBlock(pInst, lbi);
    if (pWorkBlock != NULL) {
      //
      // Make sure that sector to write is in work area and has not already been written
      //
      srsi = _FindFreeSectorInWorkBlock(pInst, pWorkBlock, brsi);
      if (srsi == BRSI_INVALID) {
        r = _CleanWorkBlock(pInst, pWorkBlock);
        if (r != 0) {
          return r;         // Error, could not clean work block.
        }
        pWorkBlock = NULL;
      }
    }
    if (pWorkBlock == NULL) {
      pWorkBlock = _AllocWorkBlock(pInst, lbi);
      if (pWorkBlock == NULL) {
        return 1;           // Error, could not allocate a new work block.
      }
      srsi = brsi;          // Preferred position is free, so let's use it.
    }
    //
    // Write data to work block
    //
    psiWork  = pWorkBlock->psi;
    NumBytes = 1uL << pInst->ldBytesPerSector;
    r = _WriteLogSectorData(pInst, psiWork, srsi, pData, 0, NumBytes);
    if (r != 0) {
      //
      // We do not know if data has been written to this sector.
      // We mark the sector as used to prevent that we write to same sector twice.
      //
      _WB_MarkSectorAsUsed(pWorkBlock, srsi);
      //
      // If the write operation fails it looks like the other write operations in this block will also fail.
      // Clean the work block here to prevent further errors.
      //
      r = _CleanWorkBlock(pInst, pWorkBlock);
      if (r != 0) {
        return r;         // Error, could not clean work block.
      }
      continue;
    }

    //
    // Fail-safe TP. At this point the sector data is written but the header is not updated yet.
    //
    CALL_TEST_HOOK_FAIL_SAFE(pInst->Unit);

    //
    // The logical sector contains now valid data.
    // Calculate and store the parity checksums.
    //
#if (FS_NOR_SUPPORT_CRC != 0) || (FS_NOR_SUPPORT_ECC != 0)
    FS_MEMSET(&DataCheck, 0xFF, sizeof(DataCheck));
    pDataCheck = &DataCheck;
#if FS_NOR_SUPPORT_CRC
    if (_IsCRCEnabled(pInst) != 0) {
      DataCheck.crc = CRC_SECTOR_DATA_INIT;
      DataCheck.crc = _pCRC_API->pfCalcData(SEGGER_CONSTPTR2PTR(const U8, pData), NumBytes, DataCheck.crc);         // MISRA deviation D:100e
    }
#endif // FS_NOR_SUPPORT_CRC
#if FS_NOR_SUPPORT_ECC
    if (_IsECCEnabled(pInst) != 0) {
      unsigned   iBlock;
      unsigned   NumBlocks;
      unsigned   ldBytesPerBlock;
      unsigned   ldBytesPerSector;
      unsigned   BytesPerBlock;
      const U8 * pData8;

      //
      // Calculate the ECC for each ECC block and store it to LSH.
      //
      ldBytesPerBlock  = pInst->pECCHookData->ldBytesPerBlock;
      ldBytesPerSector = pInst->ldBytesPerSector;
      BytesPerBlock    = 1uL << ldBytesPerBlock;
      NumBlocks        = 1uL << (ldBytesPerSector - ldBytesPerBlock);
      pData8           = SEGGER_CONSTPTR2PTR(const U8, pData);                                                      // MISRA deviation D:100e
      for (iBlock = 0; iBlock < NumBlocks; ++iBlock) {
        pInst->pECC_API->pfCalcData(pInst, SEGGER_CONSTPTR2PTR(const U32, pData8), DataCheck.aaECC[iBlock]);        // MISRA deviation D:100e
        pData8 += BytesPerBlock;
      }
    }
#endif // FS_NOR_SUPPORT_ECC
#endif // FS_NOR_SUPPORT_CRC != 0 || FS_NOR_SUPPORT_ECC != 0
    //
    // Now mark the sector data as valid by updating the LSH.
    //
    r = _MarkLogSectorAsValid(pInst, psiWork, srsi, brsi, pDataCheck);
    if (r != 0) {
      //
      // We are not able to mark the data as valid.
      // We mark the sector as used to prevent that we write to same sector twice.
      //
      _WB_MarkSectorAsUsed(pWorkBlock, srsi);
      //
      // Invalidate the data of the logical sector on the NOR flash device.
      //
      (void)_MarkLogSectorAsInvalid(pInst, psiWork, srsi);
      continue;
    }
#if FS_NOR_ENABLE_STATS
    //
    // For debug builds only. Keep the number of valid sectors up to date
    //
    {
      unsigned psiSrc;
      unsigned DataStat;
      int      Result;

      //
      // The number of valid sectors is increased only if the sector
      // is written for the first time since the last low-level format
      // or it is re-written after its value has been invalidated.
      //
      psiSrc   = _L2P_Read(pInst, lbi);
      srsiPrev = _brsi2srsi(pInst, pWorkBlock, brsi);
      if (srsiPrev == BRSI_INVALID) {     // Sector not written yet ?
        if (psiSrc != 0u) {               // Sector in data block ?
          Result = _ReadLogSectorDataStat(pInst, psiSrc, brsi, &DataStat);
          if (Result == 0) {
            if (DataStat != DATA_STAT_VALID) {
              pInst->StatCounters.NumValidSectors++;
            }
          }
        } else {
          pInst->StatCounters.NumValidSectors++;
        }
      }
    }
#endif // FS_NOR_ENABLE_STATS
    //
    // Invalidate data previously used for the same BRSI (if necessary).
    //
    srsiPrev = _brsi2srsi(pInst, pWorkBlock, brsi);
    if (srsiPrev != BRSI_INVALID) {       // Sector written ?

      //
      // Fail-safe TP. At this point we have 2 valid versions of the same logical sector.
      //
      CALL_TEST_HOOK_FAIL_SAFE(pInst->Unit);

      (void)_MarkLogSectorAsInvalid(pInst, psiWork, srsiPrev);
    }
    //
    // Invalidate old sector data that is located in a data block
    // in order to make sure that any attempt by the driver to read
    // data discarded during the low-level mount operation is reported
    // as error to the application.
    //
    if (pInst->InvalidSectorError != 0u) {
      unsigned psiSrc;
      unsigned DataStat;
      int      Result;

      psiSrc   = _L2P_Read(pInst, lbi);
      if (psiSrc != 0u) {               // Sector in data block ?
        Result = _ReadLogSectorDataStat(pInst, psiSrc, brsi, &DataStat);
        if (Result == 0) {
          if (DataStat == DATA_STAT_VALID) {
            (void)_MarkLogSectorAsInvalid(pInst, psiSrc, brsi);
          }
        }
      }
    }
    //
    // Update work block management info
    //
    _MarkWorkBlockAsMRU(pInst, pWorkBlock);
    _WB_MarkSectorAsUsed(pWorkBlock, srsi);               // Mark sector as in use.
    _WB_WriteAssignment(pInst, pWorkBlock, brsi, srsi);   // Update the look-up table.
#if FS_NOR_SUPPORT_CLEAN
    pInst->IsCleanWorkBlock = 0;
#endif // FS_NOR_SUPPORT_CLEAN
    break;
  }
  return r;
}

/*********************************************************************
*
*       _WriteOneLogSector
*
*  Function description
*    Writes one logical sector to storage device.
*
*  Parameters
*    pInst            Driver instance. It cannot be NULL.
*    LogSectorIndex   Index of the logical sector to be written.
*    pData            [IN] Data of the logical sector to be written. It cannot be NULL.
*
*  Return value
*    ==0      Data successfully written.
*    !=0      An error has occurred.
*/
static int _WriteOneLogSector(NOR_BM_INST * pInst, U32 LogSectorIndex, const void * pData) {
  int r;

#if FS_NOR_OPTIMIZE_DATA_WRITE
  r = _TryWriteOneLogSectorToDataBlock(pInst, LogSectorIndex, pData);
  if (r != 0)                                                         // Write via work block if writing to a data block failed or not possible.
#endif // FS_NOR_OPTIMIZE_DATA_WRITE;
  {
    r = _WriteOneLogSectorToWorkBlock(pInst, LogSectorIndex, pData);
  }
  return r;
}

#if FS_SUPPORT_FREE_SECTOR

/*********************************************************************
*
*        _FreeOneSector
*
*  Function description
*    Invalidates the data of a logical sector.
*
*  Parameters
*    pInst            Driver instance.
*    LogSectorIndex   Index of the logical sector.
*
*  Return value
*    ==1    OK, sector freed.
*    ==0    OK, no sector freed.
*    < 0    An error occurred.
*/
static int _FreeOneSector(NOR_BM_INST * pInst, U32 LogSectorIndex) {
  unsigned            lbi;
  unsigned            psiSrc;
  unsigned            psiWork;
  unsigned            brsi;
  unsigned            srsi;
  unsigned            DataStat;
  int                 r;
  NOR_BM_WORK_BLOCK * pWorkBlock;
  NOR_BM_LSH          lsh;
  int                 Result;

  r      = 0;    // No sector freed yet
  lbi    = _LogSectorIndex2LogBlockIndex(pInst, LogSectorIndex, &brsi);
  srsi   = brsi;
  psiSrc = _L2P_Read(pInst, lbi);
  //
  // If necessary, mark the sector as free in the data block.
  //
  if (psiSrc != 0u) {             // Sector in a data block ?
    //
    // Invalidate only sectors which contain valid data.
    //
    FS_MEMSET(&lsh, 0xFF, sizeof(lsh));
    Result = _ReadLSH(pInst, psiSrc, srsi, &lsh);
    if (Result != 0) {
      r = -1;                     // Error, could not read log. sector header.
    } else {
      DataStat = _GetLogSectorDataStat(pInst, &lsh);
      if (DataStat == DATA_STAT_VALID) {
        r = 1;                    // Sector has been freed.
        Result = _MarkLogSectorAsInvalid(pInst, psiSrc, srsi);
        if (Result != 0) {
          r = -1;                 // An error occurred.
        }
      }
    }
  }
  //
  // If necessary, mark the sector as free in the work block.
  //
  pWorkBlock = _FindWorkBlock(pInst, lbi);
  if (pWorkBlock != NULL) {
    psiWork = pWorkBlock->psi;
    srsi    = _brsi2srsi(pInst, pWorkBlock, brsi);
    if (srsi != BRSI_INVALID) {   // Sector in a work block ?
      r = 1;                      // Sector has been freed.
      //
      // Mark the sector data as invalid on the NOR flash.
      //
      Result = _MarkLogSectorAsInvalid(pInst, psiWork, srsi);
      //
      // Remove the assignment of the logical sector
      //
      _WB_WriteAssignment(pInst, pWorkBlock, brsi, 0);
      if (Result != 0) {
        r = -1;                   // An error occurred.
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
static int _FreeOneBlock(NOR_BM_INST * pInst, unsigned lbi) {
  int                 r;
  int                 Result;
  unsigned            psi;
  NOR_BM_WORK_BLOCK * pWorkBlock;

  r = 0;                                    // Set to indicate success.
  //
  // First, free the work block if one is assigned to logical block.
  //
  pWorkBlock = _FindWorkBlock(pInst, lbi);
  if (pWorkBlock != NULL) {
    psi = pWorkBlock->psi;
    _WB_RemoveFromUsedList(pInst, pWorkBlock);
    _WB_AddToFreeList(pInst, pWorkBlock);
    Result = _FreePhySector(pInst, psi);
    if (Result != 0) {
      r = Result;                           // Error, could not free physical sector.
    }
  }
  //
  // Free the data block if one is assigned to the logical block.
  //
  psi = _L2P_Read(pInst, lbi);
  if (psi != 0u) {
    (void)_RemoveDataBlock(pInst, lbi);     // Remove the logical block from the mapping table.
    Result = _FreePhySector(pInst, psi);
    if (Result != 0) {
      r = Result;                           // Error, could not free physical sector.
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
static int _FreeSectors(NOR_BM_INST * pInst, U32 SectorIndex, U32 NumSectors) {
  int      r;
  int      Result;
  unsigned NumBlocks;
  U32      NumSectorsAtOnce;
  unsigned LSectorsPerPSector;
  unsigned lbi;
  U32      NumSectorsTotal;

  //
  // Validate the sector range.
  //
  NumSectorsTotal = pInst->NumLogSectors;
  if ((SectorIndex >= NumSectorsTotal) || ((SectorIndex + NumSectors) > NumSectorsTotal)) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_BM: _FreeSectors: Invalid sector range ([%d, %d] not in [0, %d]).", (int)SectorIndex, (int)SectorIndex + (int)NumSectors - 1, (int)NumSectorsTotal - 1));
    return 1;
  }
  r = 0;                            // Set to indicate success.
  LSectorsPerPSector = pInst->LSectorsPerPSector;
  //
  // Free single sectors until we reach a block boundary.
  //
  NumSectorsAtOnce  = LSectorsPerPSector - (SectorIndex % LSectorsPerPSector);
  NumSectorsAtOnce %= LSectorsPerPSector;
  NumSectorsAtOnce  = SEGGER_MIN(NumSectorsAtOnce, NumSectors);
  if (NumSectorsAtOnce != 0u) {
    do {
      Result = _FreeOneSector(pInst, SectorIndex);
      if (Result < 0) {
        r = 1;                      // Error, could not free sector.
      } else {
        if (Result != 0) {
          IF_STATS(pInst->StatCounters.NumValidSectors--);
        }
      }
      SectorIndex++;
      NumSectors--;
    } while (--NumSectorsAtOnce != 0u);
  }
  //
  // Free entire blocks (phy. sectors).
  //
  NumBlocks = NumSectors / LSectorsPerPSector;
  if (NumBlocks != 0u) {
    NumSectorsAtOnce = (U32)NumBlocks * LSectorsPerPSector;
    lbi              = SectorIndex / LSectorsPerPSector;
    do {
      Result = _FreeOneBlock(pInst, lbi);
      if (Result < 0) {
        r = 1;                      // Error, could not free block.
      } else {
        if (Result != 0) {
          IF_STATS(pInst->StatCounters.NumValidSectors -= NumSectorsAtOnce);
        }
      }
      ++lbi;
    } while (--NumBlocks != 0u);
    SectorIndex += NumSectorsAtOnce;
    NumSectors  -= NumSectorsAtOnce;
  }
  //
  // Free the remaining sectors one at a time.
  //
  if (NumSectors != 0u) {
    do {
      Result = _FreeOneSector(pInst, SectorIndex);
      if (Result < 0) {
        r = 1;                    // Error, could not free sector.
      } else {
        if (Result != 0) {
          IF_STATS(pInst->StatCounters.NumValidSectors--);
        }
      }
      SectorIndex++;
    } while (--NumSectors != 0u);
  }
  return r;
}

#endif // FS_SUPPORT_FREE_SECTOR

/*********************************************************************
*
*        _GetSectorUsage
*
*   Function description
*     Checks if a logical sector contains valid data.
*
*   Return values
*     ==FS_SECTOR_IN_USE          Sector in use (contains valid data.)
*     ==FS_SECTOR_NOT_USED        Sector not in use (was not written nor was invalidated.)
*     ==FS_SECTOR_USAGE_UNKNOWN   An error occurred.
*/
static int _GetSectorUsage(NOR_BM_INST * pInst, U32 SectorIndex) {
  unsigned            lbi;
  unsigned            pbiSrc;
  unsigned            brsi;
  unsigned            srsi;
  unsigned            DataStat;
  int                 SectorUsage;
  NOR_BM_WORK_BLOCK * pWorkBlock;
  int                 r;
  U32                 NumSectorsTotal;

  //
  // Validate the sector range.
  //
  NumSectorsTotal = pInst->NumLogSectors;
  if (SectorIndex >= NumSectorsTotal) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_BM: _GetSectorUsage: Invalid sector index (%d not in [0, %d]).", (int)SectorIndex, (int)NumSectorsTotal - 1));
    return FS_SECTOR_USAGE_UNKNOWN;         // Error, invalid sector index.
  }
  SectorUsage = FS_SECTOR_NOT_USED;
  lbi         = _LogSectorIndex2LogBlockIndex(pInst, SectorIndex, &brsi);
  pbiSrc      = _L2P_Read(pInst, lbi);
  //
  // First, check if the sector data is present in a data block.
  //
  if (pbiSrc != 0u) {                       // Sector in a data block ?
    r = _ReadLogSectorDataStat(pInst, pbiSrc, brsi, &DataStat);
    if (r == 0) {
      if (DataStat == DATA_STAT_VALID) {
        SectorUsage = FS_SECTOR_IN_USE;     // The sector contains valid data.
      }
    }
  }
  //
  // Second, check if the sector data is present in a work block.
  //
  if (SectorUsage == FS_SECTOR_NOT_USED) {
    pWorkBlock = _FindWorkBlock(pInst, lbi);
    if (pWorkBlock != NULL) {
      srsi = _brsi2srsi(pInst, pWorkBlock, brsi);
      if (srsi != BRSI_INVALID) {           // Sector in a work block ?
        SectorUsage = FS_SECTOR_IN_USE;     // The sector contains valid data.
      }
    }
  }
  return SectorUsage;
}

#if FS_NOR_SUPPORT_CLEAN

/*********************************************************************
*
*       _EraseOneFreeBlock
*
*  Function description
*    Erases 1 physical sector which will be used to store work blocks.
*/
static int _EraseOneFreeBlock(NOR_BM_INST * pInst, int * pMore) {
  unsigned   NumPhySectors;
  unsigned   iSector;
  unsigned   i;
  U32        EraseCnt;
  U32        EraseCntData;
  int        r;
  int        Result;
  int        More;
  unsigned   psiData;
  unsigned   MRUFreeBlock;
  NOR_BM_PSH psh;


  r             = 0;              // Set to indicate success.
  More          = 0;
  NumPhySectors = pInst->NumPhySectors;
  MRUFreeBlock  = pInst->MRUFreeBlock;
  iSector       = MRUFreeBlock;
  for (i = PSI_FIRST_STORAGE_BLOCK; i < NumPhySectors; ++i) {
    if (++iSector >= NumPhySectors) {
      iSector = PSI_FIRST_STORAGE_BLOCK;
    }
    if (_IsPhySectorFree(pInst, iSector) != 0) {
      FS_MEMSET(&psh, 0xFF, sizeof(NOR_BM_PSH));
      (void)_ReadPSH(pInst, iSector, &psh);
      EraseCnt = _GetPhySectorEraseCnt(pInst, &psh);
      if (_IsPhySectorEmpty(pInst, iSector, &psh) == 0) {      // Phy. sector not blank?
        Result = ERASE_PHY_SECTOR(pInst, iSector, &EraseCnt);
        if (Result != 0) {
          _OnFatalError(pInst, Result, iSector);
          r = 1;
          goto Done;    // Error, could not erase the physical sector.
        }
        //
        // Check if a wear leveling operation has to be performed.
        //
        EraseCntData = ERASE_CNT_INVALID;
        psiData = _CheckActiveWearLeveling(pInst, EraseCnt, &EraseCntData);
        if (psiData != 0u) {
          //
          // Perform active wear leveling. A block containing data has a much lower erase count.
          // This block is now moved, giving us a free block with low erase count.
          // This procedure makes sure that blocks which contain data that does not change are evenly erased.
          //
          Result = _MoveDataBlock(pInst, psiData, iSector, EraseCnt);
          if (Result != 0) {
            _OnFatalError(pInst, Result, iSector);
            r = 1;
            goto Done;          // Fatal error, could not relocate data block.
          }
          _MarkPhySectorAsAllocated(pInst, iSector);
          //
          // The data has been moved and the data block is now free to use.
          // Note that EraseCntData is incremented by ERASE_PHY_SECTOR()
          // even if the fail safe erase operation is disabled.
          //
          Result = ERASE_PHY_SECTOR(pInst, psiData, &EraseCntData);
          if (Result != 0) {
            _OnFatalError(pInst, Result, psiData);
            r = 1;
            goto Done;          // Fatal error, erasing of physical sector failed.
          }
        }
        More = 1;
        goto Done;
      }
    }
  }
Done:
  if (pMore != NULL) {
    *pMore = More;
  }
  return r;
}

/*********************************************************************
*
*       _CleanOneWorkBlock
*/
static int _CleanOneWorkBlock(NOR_BM_INST * pInst, int * pMore) {
  int                 More;
  int                 r;
  NOR_BM_WORK_BLOCK * pWorkBlock;
  NOR_BM_WORK_BLOCK * pWorkBlockNext;

  r    = 0;
  More = 0;
  pWorkBlock = pInst->pFirstWorkBlockInUse;
  while (pWorkBlock != NULL) {
    pWorkBlockNext = pWorkBlock->pNext;
    r = _CleanWorkBlockLimited(pInst, pWorkBlock);
    if (r < 0) {
      r = 1;              // Error, could not clean work block.
      break;
    }
    if (r > 0) {
      More = 1;           // A work block has been converted and a physical sector has to be erased.
      r    = 0;           // Set to indicate success.
      break;
    }
    //
    // No work block has been converted. Get the next one in the row.
    //
    pWorkBlock = pWorkBlockNext;
  }
  if (pMore != NULL) {
    *pMore = More;
  }
  return r;
}

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
*    The executed operation can be one of the following:
*    * a work block is convert into a data block
*    * a block marked as invalid is erased
*    * a data block is relocated (copy + erase)
*/
static int _CleanOne(NOR_BM_INST * pInst, int * pMore) {
  int More;
  int r;

  More = 0;
  r    = 0;
  //
  // Clean invalid blocks first.
  //
  if (pInst->IsCleanPhySector == 0u) {
    r = _EraseOneFreeBlock(pInst, &More);
    if (r == 0) {
      if (More == 0) {
        pInst->IsCleanPhySector = 1;
      }
    }
  }
  if (r == 0) {
    if (More == 0) {
      if (pInst->IsCleanWorkBlock == 0u) {
        r = _CleanOneWorkBlock(pInst, &More);
        if (r == 0) {
          if (More == 0) {
            pInst->IsCleanWorkBlock = 1;
          }
        }
      }
    }
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
*    pInst      Driver instance.
*
*  Return value
*    ==0    OK, operation completed successfully.
*    !=0    An error occurred.
*
*  Additional information
*    This function converts all work blocks into data blocks and erases
*    all the blocks that contain invalid data. It also performs active
*    wear leveling by moving data blocks with lower erase counts to erased
*    blocks with higher erase counts.
*/
static int _Clean(NOR_BM_INST * pInst) {
  int      r;
  int      More;
  unsigned NumSectors;

  if ((pInst->IsCleanPhySector != 0u) && (pInst->IsCleanWorkBlock != 0u)) {
    return 0;                     //OK, nothing to do. The NOR flash device is clean.
  }
  //
  // Convert all work blocks to data blocks.
  //
  r = _CleanAllWorkBlocks(pInst);
  if (r != 0) {
    return 1;       // Error, could not clean work blocks.
  }
  //
  // Erase the physical sectors which will be used to store work blocks.
  //
  NumSectors = pInst->NumPhySectors;
  for (;;) {
    r = _EraseOneFreeBlock(pInst, &More);
    if (r != 0) {
      return 1;     // Error, could not erase free block.
    }
    if (More == 0) {
      break;
    }
    if (--NumSectors == 0u) {
      return 1;     // Error, too many physical sectors erased. Should actually never happen.
    }
  }
  pInst->IsCleanPhySector = 1;
  pInst->IsCleanWorkBlock = 1;
  return 0;         // OK, operation complete.
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
static U32 _GetCleanCnt(NOR_BM_INST * pInst) {
  unsigned            NumPhySectors;
  unsigned            iSector;
  unsigned            CleanCnt;
  unsigned            CleanCntTotal;
  NOR_BM_PSH          psh;
  NOR_BM_WORK_BLOCK * pWorkBlock;
  int                 r;
  unsigned            psiData;

  CleanCntTotal = 0;
  //
  // Count the number of free physical sectors which are not blank.
  //
  NumPhySectors = pInst->NumPhySectors;
  for (iSector = PSI_FIRST_STORAGE_BLOCK; iSector < NumPhySectors; ++iSector) {
    if (_IsPhySectorFree(pInst, iSector) != 0) {
      FS_MEMSET(&psh, 0xFF, sizeof(NOR_BM_PSH));
      (void)_ReadPSH(pInst, iSector, &psh);
      if (_IsPhySectorEmpty(pInst, iSector, &psh) == 0) {      // Phy. sector not blank?
        ++CleanCntTotal;
      }
    }
  }
  //
  // Count the number of allocated work blocks.
  //
  pWorkBlock = pInst->pFirstWorkBlockInUse;
  while (pWorkBlock != NULL) {
    CleanCnt = 0;
    psiData = 0;
    r = _IsInPlaceConversionAllowed(pInst, pWorkBlock, &psiData);
    if (r == 0) {
      //
      // The conversion via copy requires an empty block
      // which has to be erased in the worst case.
      // Each work block has to be erased after conversion
      // which counts as an additional clean operation.
      // The original data block will also have to be erased.
      //
      if (psiData != 0u) {
        CleanCnt = 3;
      } else {
        CleanCnt = 2;
      }
    } else {
      //
      // In case of an in-place conversion no physical sector
      // has to be erased when the converted work block does
      // not have an associated data block. However, when a data block
      // is present then two clean operations are required:
      // one that merges the data and the work block and another
      // one that erases the old data block.
      //
      if (psiData != 0u) {
        CleanCnt = 2;
      }
    }
    CleanCntTotal += CleanCnt;
    pWorkBlock = pWorkBlock->pNext;
  }
  return CleanCntTotal;
}

#endif // FS_NOR_SUPPORT_CLEAN

#if FS_NOR_SUPPORT_FORMAT

/*********************************************************************
*
*       _LowLevelFormat
*
*  Function description
*    Prepares the NOR flash device for operation.
*
*  Parameters
*    pInst        Driver instance.
*
*  Return value
*    ==0    OK, the NOR flash is formatted.
*    !=0    An error occurred.
*
*  Additional information
*    This function erases all physical sectors in the partition
*    and stores the format information to the first physical sector.
*/
static int _LowLevelFormat(NOR_BM_INST * pInst) {
  int r;
  U16 NumPhySectors;
  U32 PhySectorIndex;
  U32 Version;
  U32 BytesPerSector;
  U32 NumLogBlocks;
  U16 NumWorkBlocks;
  U16 FailSafeErase;
  U32 EraseCnt;
  int Result;
#if FS_NOR_SUPPORT_CRC
  U16 crc;
#endif // FS_NOR_SUPPORT_CRC

  pInst->LLMountFailed = 0;
  pInst->IsLLMounted   = 0;
  FailSafeErase        = FS_NOR_SUPPORT_FAIL_SAFE_ERASE;
#if FS_NOR_SUPPORT_FAIL_SAFE_ERASE
  {
    U8 FailSafeEraseConf;

#if FS_NOR_SUPPORT_CRC
    if (_IsCRCEnabled(pInst) != 0) {
      FailSafeErase = 0;                // For backward compatibility the fail safe erase operation is disabled when the CRC verification is active.
    }
#endif // FS_NOR_SUPPORT_CRC
#if (FS_NOR_CAN_REWRITE == 0)
    if (_IsRewriteSupported(pInst) == 0) {
      FailSafeErase = 0;                // For backward compatibility the fail safe erase operation is disabled when the NOR flash device cannot rewrite.
    }
#endif // FS_NOR_CAN_REWRITE == 0
    //
    // Evaluate the configuration requested by the application
    // regarding the fail safe erase operation.
    //
    FailSafeEraseConf = pInst->FailSafeEraseConf;
    if (FailSafeEraseConf != FAIL_SAFE_ERASE_INVALID) {
      FailSafeErase = FailSafeEraseConf;
    }
  }
  pInst->FailSafeErase = (U8)FailSafeErase;
#endif // FS_NOR_SUPPORT_FAIL_SAFE_ERASE
  //
  // Erase all physical sectors of NOR flash.
  //
  NumPhySectors = pInst->NumPhySectors;
  for (PhySectorIndex = 0; PhySectorIndex < NumPhySectors; ++PhySectorIndex) {
    if (PhySectorIndex == PSI_INFO_BLOCK) {
      if (_IsPhySectorEraseRequired(pInst, PhySectorIndex) != 0) {
        r = _ErasePhySector(pInst, PhySectorIndex, NULL);
        if (r != 0) {
          return 1;
        }
      }
    } else {
      if (pInst->EraseUsedSectors != 0u) {
        if (_IsPhySectorEraseRequired(pInst, PhySectorIndex) != 0) {
          EraseCnt = 0;
          r = ERASE_PHY_SECTOR(pInst, PhySectorIndex, &EraseCnt);     // Set the erase count to a known value.
          if (r != 0) {
            return 1;
          }
        }
      } else {
        r = _PreErasePhySector(pInst, PhySectorIndex);
        if (r != 0) {
          return 1;
        }
      }
    }
  }
#if FS_NOR_SUPPORT_CLEAN
  pInst->IsCleanWorkBlock = 1;
  pInst->IsCleanPhySector = 0;
  if (pInst->EraseUsedSectors != 0u) {
    pInst->IsCleanPhySector = 1;
  }
#endif // FS_NOR_SUPPORT_CLEAN
  IF_STATS(pInst->StatCounters.NumValidSectors = 0);
  //
  // Write the format information to NOR flash device. This information is used by the driver at low-level mount.
  // The signature is written last to make sure that the format operation is fail safe.
  //
  Version        = LLFORMAT_VERSION;
  BytesPerSector = 1uL << pInst->ldBytesPerSector;
  NumLogBlocks   = pInst->NumLogBlocks;
  NumWorkBlocks  = pInst->NumWorkBlocks;
  FailSafeErase  = (FailSafeErase != 0u) ? 0x0000u : 0xFFFFu;     // Reversed logic: 0x0000 -> supported, 0xFFFF -> not supported
#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
  _pMultiByteAPI->pfStoreU32(SEGGER_PTR2PTR(U8, &Version),        Version);                                         // MISRA deviation D:100e
  _pMultiByteAPI->pfStoreU32(SEGGER_PTR2PTR(U8, &BytesPerSector), BytesPerSector);                                  // MISRA deviation D:100e
  _pMultiByteAPI->pfStoreU32(SEGGER_PTR2PTR(U8, &NumLogBlocks),   NumLogBlocks);                                    // MISRA deviation D:100e
  _pMultiByteAPI->pfStoreU16(SEGGER_PTR2PTR(U8, &NumWorkBlocks),  NumWorkBlocks);                                   // MISRA deviation D:100e
  _pMultiByteAPI->pfStoreU16(SEGGER_PTR2PTR(U8, &FailSafeErase),  FailSafeErase);                                   // MISRA deviation D:100e
#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER
#if FS_NOR_SUPPORT_CRC
  crc = 0;
  if (_IsCRCEnabled(pInst) != 0) {
    U16 aInfo[sizeof(_acInfo) / 2u];          // 16-bit alignment is required for the CRC calculation.

    FS_MEMCPY(aInfo, _acInfo, sizeof(aInfo));
    crc = CRC_SECTOR_DATA_INIT;
    crc = _pCRC_API->pfCalcData(SEGGER_PTR2PTR(U8, aInfo),           sizeof(aInfo),          crc);                  // MISRA deviation D:100e
    crc = _pCRC_API->pfCalcData(SEGGER_PTR2PTR(U8, &Version),        sizeof(Version),        crc);                  // MISRA deviation D:100e
    crc = _pCRC_API->pfCalcData(SEGGER_PTR2PTR(U8, &BytesPerSector), sizeof(BytesPerSector), crc);                  // MISRA deviation D:100e
    crc = _pCRC_API->pfCalcData(SEGGER_PTR2PTR(U8, &NumLogBlocks),   sizeof(NumLogBlocks),   crc);                  // MISRA deviation D:100e
    crc = _pCRC_API->pfCalcData(SEGGER_PTR2PTR(U8, &NumWorkBlocks),  sizeof(NumWorkBlocks),  crc);                  // MISRA deviation D:100e
    crc = _pCRC_API->pfCalcData(SEGGER_PTR2PTR(U8, &FailSafeErase),  sizeof(FailSafeErase),  crc);                  // MISRA deviation D:100e
  }
#endif // FS_NOR_SUPPORT_CRC
  r = 0;
  //
  // Write the format information to NOR flash device.
  //
#if FS_NOR_SUPPORT_ECC
  if (_IsECCEnabled(pInst) != 0) {
    unsigned   NumBytes;
    U8       * pData8;

    //
    // Store the format information to the ECC buffer.
    //
    NumBytes = 1uL << pInst->pECCHookData->ldBytesPerBlock;
    pData8   = SEGGER_PTR2PTR(U8, _pECCBuffer);                                                                     // MISRA deviation D:100e
    FS_MEMSET(pData8, 0xFF, NumBytes);
    FS_MEMCPY(pData8 + INFO_OFF_FORMAT_VERSION,   &Version,        sizeof(Version));
    FS_MEMCPY(pData8 + INFO_OFF_BYTES_PER_SECTOR, &BytesPerSector, sizeof(BytesPerSector));
    FS_MEMCPY(pData8 + INFO_OFF_NUM_LOG_BLOCKS,   &NumLogBlocks,   sizeof(NumLogBlocks));
    FS_MEMCPY(pData8 + INFO_OFF_NUM_WORK_BLOCKS,  &NumWorkBlocks,  sizeof(NumWorkBlocks));
    FS_MEMCPY(pData8 + INFO_OFF_FAIL_SAFE_ERASE,  &FailSafeErase,  sizeof(FailSafeErase));
    FS_MEMCPY(pData8 + INFO_OFF_FORMAT_SIGNATURE, _acInfo,         sizeof(_acInfo));
    //
    // For fail-safety, the signature is written separately in a second step.
    //
    pData8    = SEGGER_PTR2PTR(U8, _pECCBuffer);                                                                    // MISRA deviation D:100e
    pData8   += sizeof(_acInfo);
    NumBytes -= sizeof(_acInfo);
    r = _WriteLogSectorData(pInst, PSI_INFO_BLOCK, SRSI_INFO_FORMAT, pData8, INFO_OFF_FORMAT_VERSION, NumBytes);
    if (r == 0) {
      r = _WriteLogSectorData(pInst, PSI_INFO_BLOCK, SRSI_INFO_FORMAT, _acInfo, INFO_OFF_FORMAT_SIGNATURE, sizeof(_acInfo));
    }
  } else
#endif // FS_NOR_SUPPORT_ECC
  {
    Result = _WriteLogSectorData(pInst, PSI_INFO_BLOCK, SRSI_INFO_FORMAT, &Version,        INFO_OFF_FORMAT_VERSION,   sizeof(Version));
    r = ((Result != 0) && (r == 0)) ? Result : r;
    Result = _WriteLogSectorData(pInst, PSI_INFO_BLOCK, SRSI_INFO_FORMAT, &BytesPerSector, INFO_OFF_BYTES_PER_SECTOR, sizeof(BytesPerSector));
    r = ((Result != 0) && (r == 0)) ? Result : r;
    Result = _WriteLogSectorData(pInst, PSI_INFO_BLOCK, SRSI_INFO_FORMAT, &NumLogBlocks,   INFO_OFF_NUM_LOG_BLOCKS,   sizeof(NumLogBlocks));
    r = ((Result != 0) && (r == 0)) ? Result : r;
    Result = _WriteLogSectorData(pInst, PSI_INFO_BLOCK, SRSI_INFO_FORMAT, &NumWorkBlocks,  INFO_OFF_NUM_WORK_BLOCKS,  sizeof(NumWorkBlocks));
    r = ((Result != 0) && (r == 0)) ? Result : r;
    Result = _WriteLogSectorData(pInst, PSI_INFO_BLOCK, SRSI_INFO_FORMAT, &FailSafeErase,  INFO_OFF_FAIL_SAFE_ERASE,  sizeof(FailSafeErase));
    r = ((Result != 0) && (r == 0)) ? Result : r;
    Result = _WriteLogSectorData(pInst, PSI_INFO_BLOCK, SRSI_INFO_FORMAT, _acInfo,         INFO_OFF_FORMAT_SIGNATURE, sizeof(_acInfo));
    r = ((Result != 0) && (r == 0)) ? Result : r;
  }
#if (FS_NOR_SUPPORT_CRC != 0) || (FS_NOR_SUPPORT_ECC != 0)
  if (r == 0) {
    NOR_BM_LSH lsh;

    INIT_LSH_DATA_RANGE();
    FS_MEMSET(&lsh, 0xFF, sizeof(lsh));
#if (FS_NOR_SUPPORT_CRC != 0) && (FS_NOR_SUPPORT_ECC != 0)
    if (_IsCRCEnabled(pInst) != 0) {
      lsh.crcSectorData = crc;
      UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_LSH, crcSectorData), sizeof(lsh.crcSectorData));
      r = _pCRC_API->pfCalcStoreLSH(&lsh);
    }
    if (r == 0) {
      if (_IsECCEnabled(pInst) != 0) {
        int NumBitsCorrected;

        pInst->pECC_API->pfCalcData(pInst, _pECCBuffer, lsh.aaECCSectorData[0]);
        UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_LSH, aaECCSectorData), sizeof(lsh.aaECCSectorData));
        NumBitsCorrected = 0;
        r = pInst->pECC_API->pfCalcStoreLSH(pInst, &lsh, &NumBitsCorrected);
        UPDATE_NUM_BIT_ERRORS(pInst, NumBitsCorrected);
      }
    }
#elif (FS_NOR_SUPPORT_CRC != 0) && (FS_NOR_SUPPORT_ECC == 0)
    if (_IsCRCEnabled(pInst) != 0) {
      lsh.crcSectorData = crc;
      UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_LSH, crcSectorData), sizeof(lsh.crcSectorData));
      r = _pCRC_API->pfCalcStoreLSH(&lsh);
    }
#elif (FS_NOR_SUPPORT_CRC == 0) && (FS_NOR_SUPPORT_ECC != 0)
    if (_IsECCEnabled(pInst) != 0) {
      int NumBitsCorrected;

      pInst->pECC_API->pfCalcData(pInst, _pECCBuffer, lsh.aaECCSectorData[0]);
      UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_BM_LSH, aaECCSectorData), sizeof(lsh.aaECCSectorData));
      NumBitsCorrected = 0;
      r = pInst->pECC_API->pfCalcStoreLSH(pInst, &lsh, &NumBitsCorrected);
      UPDATE_NUM_BIT_ERRORS(pInst, NumBitsCorrected);
    }
#endif // FS_NOR_SUPPORT_CRC != 0 && FS_NOR_SUPPORT_ECC != 0
    if (r == 0) {
      r = _WriteLSH(pInst, PSI_INFO_BLOCK, SRSI_INFO_FORMAT, &lsh);
    }
    INIT_LSH_DATA_RANGE();
  }
#endif // FS_NOR_SUPPORT_CRC != 0 || FS_NOR_SUPPORT_ECC != 0
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR_BM: LL_FORMAT LogSectorSize: %u, NumLogBlocks: %u, NumWorkBlocks: %u", BytesPerSector, NumLogBlocks, NumWorkBlocks));
#if FS_NOR_SUPPORT_FAIL_SAFE_ERASE
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, ", FSErase: %d", pInst->FailSafeErase));
#endif // FS_NOR_SUPPORT_FAIL_SAFE_ERASE
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, ", r: %d\n", r));
  return r;
}

#endif // FS_NOR_SUPPORT_FORMAT

/*********************************************************************
*
*       _InitIfRequired
*
*  Function description
*    Initializes the NOR device if not already done.
*
*/
static int _InitIfRequired(NOR_BM_INST * pInst) {
  int r;

  if (pInst->IsInited != 0u) {
    return 0;                           // OK, nothing to do, already initialized.
  }
  r = _ReadApplyDeviceParas(pInst);
  if (r != 0) {
    return 1;                           // Error, could not read parameters from device.
  }
  pInst->IsInited = 1;
  return 0;                             // OK, the NOR flash device can be accessed.
}

/*********************************************************************
*
*       _AllocInstIfRequired
*
*  Function description
*    Allocate memory for the specified unit if required.
*/
static NOR_BM_INST * _AllocInstIfRequired(U8 Unit) {
  NOR_BM_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  pInst = NULL;                         // Set to indicate an error.
  if (Unit < (U8)FS_NOR_NUM_UNITS) {
    pInst = _apInst[Unit];
    if (pInst == NULL) {
      FS_ALLOC_ZEROED_PTR(SEGGER_PTR2PTR(void *, &pInst), (I32)sizeof(NOR_BM_INST), "NOR_BM_INST");                 // MISRA deviation D:100d
      if (pInst != NULL) {
        _apInst[Unit] = pInst;
        pInst->Unit               = Unit;
#if FS_NOR_SKIP_BLANK_SECTORS
        pInst->SkipBlankSectors   = 1;
#endif
#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
        pInst->ldBytesPerLine     = (U8)_ld(FS_NOR_LINE_SIZE);
        pInst->IsRewriteSupported = FS_NOR_CAN_REWRITE;
        pInst->SizeOfPSH          = (U8)sizeof(NOR_BM_PSH);
        pInst->SizeOfLSH          = (U8)sizeof(NOR_BM_LSH);
#endif // FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
#if FS_NOR_SUPPORT_FAIL_SAFE_ERASE
        pInst->FailSafeEraseConf  = FAIL_SAFE_ERASE_INVALID;
#endif // FS_NOR_SUPPORT_FAIL_SAFE_ERASE
#if FS_NOR_SUPPORT_ECC
        pInst->pECCHookData       = FS_NOR_ECC_HOOK_DATA_DEFAULT;
        pInst->pECCHookMan        = FS_NOR_ECC_HOOK_MAN_DEFAULT;
#endif // FS_NOR_SUPPORT_ECC
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
*    Returns a driver instance by unit number.
*/
static NOR_BM_INST * _GetInst(U8 Unit) {
  NOR_BM_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  pInst = NULL;                         // Set to indicate an error.
  if (Unit < (U8)FS_NOR_NUM_UNITS) {
    pInst = _apInst[Unit];
  }
  return pInst;
}

/*********************************************************************
*
*       _Unmount
*
*  Function description
*    Marks the NOR flash device as not initialized.
*
*  Parameters
*    pInst        Driver instance.
*/
static void _Unmount(NOR_BM_INST * pInst) {
  pInst->IsInited             = 0;
  pInst->IsLLMounted          = 0;
  pInst->LLMountFailed        = 0;
  pInst->MRUFreeBlock         = 0;
  pInst->pFirstWorkBlockFree  = NULL;
  pInst->pFirstWorkBlockInUse = NULL;
  pInst->HasFatalError        = 0;
  pInst->ErrorType            = 0;
  pInst->ErrorPSI             = 0;
#if FS_NOR_OPTIMIZE_DATA_WRITE
  pInst->pFirstDataBlockFree  = NULL;
  pInst->pFirstDataBlockInUse = NULL;
#endif // FS_NOR_OPTIMIZE_DATA_WRITE
#if FS_NOR_ENABLE_STATS
  FS_MEMSET(&pInst->StatCounters, 0, sizeof(FS_NOR_BM_STAT_COUNTERS));
#endif
}

/*********************************************************************
*
*       _ExecCmdGetDevInfo
*/
static int _ExecCmdGetDevInfo(NOR_BM_INST * pInst, void * pBuffer) {
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
      pDevInfo = SEGGER_PTR2PTR(FS_DEV_INFO, pBuffer);                                        // MISRA deviation D:100e
      pDevInfo->NumSectors     = pInst->NumLogSectors;
      pDevInfo->BytesPerSector = (U16)(1uL << pInst->ldBytesPerSector);
      r = 0;
    }
  }
  return r;
}

/*********************************************************************
*
*       _ExecCmdRequiresFormat
*/
static int _ExecCmdRequiresFormat(NOR_BM_INST * pInst) {
  int r;
  int Result;

  r = 1;          // Set to indicate that the NOR flash device is not formatted.
  Result = _LowLevelMountIfRequired(pInst);
  if (Result == 0) {
    r = 0;        // NOR flash is formatted.
  }
  return r;
}

/*********************************************************************
*
*       _ExecCmdUnmount
*/
static int _ExecCmdUnmount(NOR_BM_INST * pInst) {
  _Unmount(pInst);
  return 0;
}

/*********************************************************************
*
*       _ExecCmdGetSectorUsage
*/
static int _ExecCmdGetSectorUsage(NOR_BM_INST * pInst, int Aux, void * pBuffer) {
  int   r;
  int   Result;
  int * pSectorUsage;

  r = -1;         // Set to indicate failure.
  if (pBuffer != NULL) {
    Result = _LowLevelMountIfRequired(pInst);
    if (Result == 0) {
      pSectorUsage  = SEGGER_PTR2PTR(int, pBuffer);                                           // MISRA deviation D:100e
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
*
*  Function description
*    Executes the FS_CMD_DEINIT command.
*
*  Parameters
*    pInst      Driver instance.
*/
static int _ExecCmdDeInit(NOR_BM_INST * pInst) {
  U8                  Unit;
  NOR_BM_WORK_BLOCK * pWorkBlock;

  Unit = pInst->Unit;
  //
  // Deinitialize the physical layer first.
  //
  if (pInst->pPhyType->pfDeInit != NULL) {
    pInst->pPhyType->pfDeInit(Unit);
  }
  FS_FREE(pInst->pLog2PhyTable);
  FS_FREE(pInst->pFreeMap);
  if (pInst->paWorkBlock != NULL) {         // The array is allocated only when the volume is mounted.
    pWorkBlock = &pInst->paWorkBlock[0];
    FS_FREE(pWorkBlock->paIsWritten);       // This array is allocated at once for all the work blocks. The address of the allocated memory is stored to the first work block.
    FS_FREE(pWorkBlock->paAssign);          // This array is allocated at once for all the work blocks. The address of the allocated memory is stored to the first work block.
    FS_FREE(pInst->paWorkBlock);
  }
#if FS_NOR_OPTIMIZE_DATA_WRITE
  if (pInst->paDataBlock != NULL) {         // The array is allocated only when the volume is mounted.
    NOR_BM_DATA_BLOCK * pDataBlock;

    pDataBlock = &pInst->paDataBlock[0];
    FS_FREE(pDataBlock->paIsWritten);       // This array is allocated at once for all the data blocks. The address of the allocated memory is stored to the first data block.
    FS_FREE(pInst->paDataBlock);
  }
#endif // FS_NOR_OPTIMIZE_DATA_WRITE
  FS_FREE(pInst);
  _apInst[Unit] = NULL;
  _NumUnits--;
#if FS_NOR_SUPPORT_ECC
  //
  // If all driver instances have been removed, then remove the ECC buffer.
  //
  if (_NumUnits == 0u) {
    FS_FREE(_pECCBuffer);
    _pECCBuffer = NULL;
  }
#endif // FS_NOR_SUPPORT_ECC
  return 0;
}

#endif // FS_SUPPORT_DEINIT

#if FS_NOR_SUPPORT_FORMAT

/*********************************************************************
*
*       _ExecCmdFormatLowLevel
*/
static int _ExecCmdFormatLowLevel(NOR_BM_INST * pInst) {
  int r;
  int Result;

  r = -1;         // Set to indicate failure.
  Result = _LowLevelFormat(pInst);
  if (Result == 0) {
    r = 0;
  }
  return r;
}

#endif // FS_NOR_SUPPORT_FORMAT

#if FS_NOR_SUPPORT_CLEAN

/*********************************************************************
*
*       _ExecCmdCleanOne
*/
static int _ExecCmdCleanOne(NOR_BM_INST * pInst, void * pBuffer) {
  int   r;
  int   Result;
  int   More;
  int * pMore;

  r = -1;           // Set to indicate failure.
  Result = _LowLevelMountIfRequired(pInst);
  if (Result == 0) {
    More = 0;
    Result = _CleanOne(pInst, &More);
    pMore = SEGGER_PTR2PTR(int, pBuffer);                                                     // MISRA deviation D:100e
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
static int _ExecCmdClean(NOR_BM_INST * pInst) {
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
static int _ExecCmdGetCleanCnt(NOR_BM_INST * pInst, void * pBuffer) {
  int   r;
  int   Result;
  U32   Cnt;
  U32 * pCnt;

  r = -1;           // Set to indicate failure.
  Result = _LowLevelMountIfRequired(pInst);
  if (Result == 0) {
    Cnt = _GetCleanCnt(pInst);
    pCnt = SEGGER_PTR2PTR(U32, pBuffer);                                                      // MISRA deviation D:100e
    if (pCnt != NULL) {
      *pCnt = Cnt;
      r = 0;
    }
  }
  return r;
}

#endif // FS_NOR_SUPPORT_CLEAN

#if FS_NOR_SUPPORT_TRIM

/*********************************************************************
*
*       _ExecCmdFreeSectors
*/
static int _ExecCmdFreeSectors(NOR_BM_INST * pInst, int Aux, const void * pBuffer) {
  int r;
  int Result;
  U32 SectorIndex;
  U32 NumSectors;

  r = -1;         // Set to indicate failure.
  if (pBuffer != NULL) {
    Result = _LowLevelMountIfRequired(pInst);
    if (Result == 0) {
      SectorIndex = (U32)Aux;
      NumSectors  = *SEGGER_CONSTPTR2PTR(const U32, pBuffer);                                 // MISRA deviation D:100e
      Result = _FreeSectors(pInst, SectorIndex, NumSectors);
      if (Result == 0) {
        r = 0;
      }
    }
  }
  return r;
}

#endif // FS_NOR_SUPPORT_TRIM

/*********************************************************************
*
*       Static code (public via callback)
*
**********************************************************************
*/

/*********************************************************************
*
*       _NOR_InitMedium
*
*   Function description
*     Initialize and identifies the storage device.
*
*   Return value
*     ==0   Device OK and ready for operation.
*     !=0   An error has occurred.
*/
static int _NOR_InitMedium(U8 Unit) {
  NOR_BM_INST * pInst;
  int           r;

  r = 1;                      // Set to indicate an error.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    ASSERT_PHY_TYPE_IS_SET(pInst);
    r = _InitIfRequired(pInst);
  }
  return r;
}

/*********************************************************************
*
*       _NOR_GetDriverName
*/
static const char * _NOR_GetDriverName(U8 Unit) {
  FS_USE_PARA(Unit);
  return "nor";
}

/*********************************************************************
*
*       _NOR_AddDevice
*/
static int _NOR_AddDevice(void) {
  NOR_BM_INST * pInst;
  int           r;

  r = -1;             // Set to indicate an error.
  pInst = _AllocInstIfRequired(_NumUnits);
  if (pInst != NULL) {
    r = (int)_NumUnits++;
  }
  return r;
}

/*********************************************************************
*
*       _NOR_Read
*/
static int _NOR_Read(U8 Unit, U32 SectorIndex, void * pData, U32 NumSectors) {
  U8          * pData8;
  NOR_BM_INST * pInst;
  int           r;
  unsigned      BytesPerSector;
  U32           NumSectorsTotal;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;             // Error, instance not found.
  }
  //
  // Validate the sector range.
  //
  NumSectorsTotal = pInst->NumLogSectors;
  if ((SectorIndex >= NumSectorsTotal) || ((SectorIndex + NumSectors) > NumSectorsTotal)) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_BM: _NOR_Read: Invalid sector range ([%d, %d] not in [0, %d]).", (int)SectorIndex, (int)SectorIndex + (int)NumSectors - 1, (int)NumSectorsTotal - 1));
    return 1;
  }
  //
  // Make sure device is low-level mounted. If it is not, there is nothing we can do.
  //
  r = _LowLevelMountIfRequired(pInst);
  if (r != 0) {
    return r;             // Error, could not mount NOR flash device.
  }
  //
  // Read data one sector at a time
  //
  pData8         = SEGGER_PTR2PTR(U8, pData);                                                 // MISRA deviation D:100e
  BytesPerSector = 1uL << pInst->ldBytesPerSector;
  do {
    r = _ReadOneLogSector(pInst, SectorIndex, pData8);
    if (r != 0) {
      CHECK_CONSISTENCY(pInst);
      break;              // Error, could not read data.
    }
    pData8 += BytesPerSector;
    ++SectorIndex;
    IF_STATS(pInst->StatCounters.ReadSectorCnt++);
  } while (--NumSectors != 0u);
  CHECK_CONSISTENCY(pInst);
  return r;
}

/*********************************************************************
*
*       _NOR_Write
*/
static int _NOR_Write(U8 Unit, U32 SectorIndex, const void * pData, U32 NumSectors, U8 RepeatSame) {
  const U8    * pData8;
  NOR_BM_INST * pInst;
  int           r;
  unsigned      BytesPerSector;
  U32           NumSectorsTotal;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;             // Error, instance not found.
  }
  //
  // Validate the sector range.
  //
  NumSectorsTotal = pInst->NumLogSectors;
  if ((SectorIndex >= NumSectorsTotal) || ((SectorIndex + NumSectors) > NumSectorsTotal)) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_BM: _NOR_Write: Invalid sector range ([%d, %d] not in [0, %d]).", (int)SectorIndex, (int)SectorIndex + (int)NumSectors - 1, (int)NumSectorsTotal - 1));
    return 1;
  }
  //
  // Mount the NOR flash if not already mounted
  //
  r = _LowLevelMountIfRequired(pInst);
  if (r != 0) {
    return r;                       // Error, could not mount NOR flash.
  }
  //
  // Do not perform any operation if no sector data is provided
  // or no sectors are written.
  //
  if ((pData == NULL) || (NumSectors == 0u)) {
    return 0;
  }
  //
  // Write data one sector at a time.
  //
  pData8         = SEGGER_CONSTPTR2PTR(const U8, pData);                                      // MISRA deviation D:100e
  BytesPerSector = 1uL << pInst->ldBytesPerSector;
  for (;;) {
    r = _WriteOneLogSector(pInst, SectorIndex, pData8);
    if (r != 0) {
      CHECK_CONSISTENCY(pInst);
      break;                        // Error, could not write data.
    }
    IF_STATS(pInst->StatCounters.WriteSectorCnt++);
    if (--NumSectors == 0u) {
      break;
    }
    if (RepeatSame == 0u) {
      pData8 += BytesPerSector;
    }
    ++SectorIndex;
  }
  CHECK_CONSISTENCY(pInst);
  return r;
}

/*********************************************************************
*
*       _NOR_IoCtl
*/
static int _NOR_IoCtl(U8 Unit, I32 Cmd, I32 Aux, void * pBuffer) {
  NOR_BM_INST * pInst;
  int           r;
  int           IsLLMounted;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return -1;            // Error, instance not found.
  }
  r = -1;                 // Set to indicate an error.
  IsLLMounted = (int)pInst->IsLLMounted;
  switch (Cmd) {
  case FS_CMD_GET_DEVINFO:
    r = _ExecCmdGetDevInfo(pInst, pBuffer);
    break;
#if FS_NOR_SUPPORT_FORMAT
  case FS_CMD_FORMAT_LOW_LEVEL:
    r = _ExecCmdFormatLowLevel(pInst);
    break;
#endif // FS_NOR_SUPPORT_FORMAT
  case FS_CMD_REQUIRES_FORMAT:
    r = _ExecCmdRequiresFormat(pInst);
    break;
  case FS_CMD_UNMOUNT:
    //lint through
  case FS_CMD_UNMOUNT_FORCED:
    r = _ExecCmdUnmount(pInst);
    break;
#if FS_NOR_SUPPORT_CLEAN
  case FS_CMD_CLEAN_ONE:
    r = _ExecCmdCleanOne(pInst, pBuffer);
    break;
  case FS_CMD_CLEAN:
    r = _ExecCmdClean(pInst);
    break;
  case FS_CMD_GET_CLEAN_CNT:
    r = _ExecCmdGetCleanCnt(pInst, pBuffer);
    break;
#endif // FS_NOR_SUPPORT_CLEAN
  case FS_CMD_GET_SECTOR_USAGE:
    r = _ExecCmdGetSectorUsage(pInst, Aux, pBuffer);
    break;
  case FS_CMD_FREE_SECTORS:
#if FS_NOR_SUPPORT_TRIM
    r = _ExecCmdFreeSectors(pInst, Aux, pBuffer);
#else
    //
    // Return OK even if we do nothing here in order to
    // prevent that the file system reports an error.
    //
    r = 0;
#endif // FS_NOR_SUPPORT_TRIM
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
  // Check the consistency only if the NOR flash device
  // is mounted by the current I/O control operation.
  //
  if (IsLLMounted == 0) {
    CHECK_CONSISTENCY(pInst);
  }
  return r;
}

/*********************************************************************
*
*       _NOR_GetNumUnits
*/
static int _NOR_GetNumUnits(void) {
  return (int)_NumUnits;
}

/*********************************************************************
*
*       _NOR_GetStatus
*/
static int _NOR_GetStatus(U8 Unit) {
  FS_USE_PARA(Unit);
  return FS_MEDIA_IS_PRESENT;
}

/*********************************************************************
*
*       Driver API Table
*/
const FS_DEVICE_TYPE FS_NOR_BM_Driver = {
  _NOR_GetDriverName,
  _NOR_AddDevice,
  _NOR_Read,
  _NOR_Write,
  _NOR_IoCtl,
  _NOR_InitMedium,
  _NOR_GetStatus,
  _NOR_GetNumUnits
};

/*********************************************************************
*
*       Public code (for internal use only)
*
**********************************************************************
*/

#if FS_SUPPORT_TEST

/*********************************************************************
*
*       FS__NOR_BM_SetTestHookFailSafe
*/
void FS__NOR_BM_SetTestHookFailSafe(FS_NOR_TEST_HOOK_NOTIFICATION * pfTestHook) {
  _pfTestHookFailSafe = pfTestHook;
}

/*********************************************************************
*
*       FS__NOR_BM_SetTestHookDataReadBegin
*/
void FS__NOR_BM_SetTestHookDataReadBegin(FS_NOR_TEST_HOOK_DATA_READ_BEGIN * pfTestHook) {
  _pfTestHookDataReadBegin = pfTestHook;
}

/*********************************************************************
*
*       FS__NOR_BM_SetTestHookDataReadEnd
*/
void FS__NOR_BM_SetTestHookDataReadEnd(FS_NOR_TEST_HOOK_DATA_READ_END * pfTestHook) {
  _pfTestHookDataReadEnd = pfTestHook;
}

/*********************************************************************
*
*       FS__NOR_BM_SetTestHookDataWriteBegin
*/
void FS__NOR_BM_SetTestHookDataWriteBegin(FS_NOR_TEST_HOOK_DATA_WRITE_BEGIN * pfTestHook) {
  _pfTestHookDataWriteBegin = pfTestHook;
}

/*********************************************************************
*
*       FS__NOR_BM_SetTestHookDataWriteEnd
*/
void FS__NOR_BM_SetTestHookDataWriteEnd(FS_NOR_TEST_HOOK_DATA_WRITE_END * pfTestHook) {
  _pfTestHookDataWriteEnd = pfTestHook;
}

/*********************************************************************
*
*       FS__NOR_BM_SetTestHookSectorErase
*/
void FS__NOR_BM_SetTestHookSectorErase(FS_NOR_TEST_HOOK_SECTOR_ERASE * pfTestHook) {
  _pfTestHookSectorErase = pfTestHook;
}

#endif

/*********************************************************************
*
*       FS__NOR_BM_GetPSHInfo
*/
void FS__NOR_BM_GetPSHInfo(U8 Unit, FS_NOR_BM_PSH_INFO * pPSHInfo) {
  NOR_BM_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pPSHInfo->NumBytes          = (U8)_SizeOfPSH(pInst);
    pPSHInfo->OffEraseCnt       = (U8)OFFSET_OF_MEMBER(NOR_BM_PSH, EraseCnt);
#if FS_NOR_SUPPORT_FAIL_SAFE_ERASE
    pPSHInfo->OffEraseSignature = (U8)OFFSET_OF_MEMBER(NOR_BM_PSH, EraseSignature);
#else
    pPSHInfo->OffEraseSignature = pPSHInfo->NumBytes;       // Invalid offset.
#endif
    pPSHInfo->OffDataCnt        = (U8)OFFSET_OF_MEMBER(NOR_BM_PSH, DataCnt);
  }
}

/*********************************************************************
*
*       FS__NOR_BM_GetLSHInfo
*/
void FS__NOR_BM_GetLSHInfo(U8 Unit, FS_NOR_BM_LSH_INFO * pLSHInfo) {
  NOR_BM_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pLSHInfo->NumBytes = (U8)_SizeOfLSH(pInst);
  }
}

/*********************************************************************
*
*       FS__NOR_BM_IsRewriteSupported
*/
U8 FS__NOR_BM_IsRewriteSupported(U8 Unit) {
  U8 r;

  r = 1;          // Set to indicate that the NOR flash device can rewrite.
#if (FS_NOR_CAN_REWRITE == 0)
  {
    NOR_BM_INST * pInst;

    pInst = _AllocInstIfRequired(Unit);
    if (pInst != NULL) {
      r = (U8)_IsRewriteSupported(pInst);
    }
  }
#else
  FS_USE_PARA(Unit);
#endif // FS_NOR_CAN_REWRITE == 0
  return r;
}

/*********************************************************************
*
*       FS__NOR_BM_IsFailSafeEraseSupported
*/
U8 FS__NOR_BM_IsFailSafeEraseSupported(U8 Unit) {
  U8 r;

  r = 1;          // Set to indicate that the NOR flash device can rewrite.
#if FS_NOR_SUPPORT_FAIL_SAFE_ERASE
  {
    NOR_BM_INST * pInst;

    pInst = _AllocInstIfRequired(Unit);
    if (pInst != NULL) {
      r = pInst->FailSafeErase;
    }
  }
#else
  FS_USE_PARA(Unit);
#endif // FS_NOR_SUPPORT_FAIL_SAFE_ERASE
  return r;
}

/*********************************************************************
*
*       FS__NOR_BM_GetPhyType
*/
const FS_NOR_PHY_TYPE * FS__NOR_BM_GetPhyType(U8 Unit) {
  NOR_BM_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    return pInst->pPhyType;
  }
  return NULL;
}

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_NOR_BM_Configure
*
*  Function description
*    Configures an instance of the Block Map NOR driver
*
*  Parameters
*    Unit           Index of the Block Map NOR driver (0-based).
*    BaseAddr       Address of the first byte in NOR flash.
*    StartAddr      Address of the first byte the NOR driver is permitted
*                   to use as storage.
*    NumBytes       Number of bytes starting from StartAddr available to
*                   be used by the NOR driver as storage.
*
*  Additional information
*    This function is mandatory and it has to be called once in FS_X_AddDevices()
*    for each instance of the Block Map NOR driver created by the application.
*    Different instances of the NOR driver are identified by the Unit parameter.
*
*    BaseAddr is used only for NOR flash devices that are memory mapped.
*    For serial NOR flash devices that are not memory mapped BaseAddr
*    has to be set to 0.
*
*    StartAddr has to be greater than or equal to BaseAddr and smaller
*    than the total number of bytes in the NOR flash device. The block map
*    NOR driver rounds up StartAddr to the start address of the next physical
*    sector in the NOR flash device.
*
*    NumBytes is rounded up to a physical sector boundary if the memory
*    range defined by StartAddr and NumBytes is smaller than the capacity
*    of the NOR flash device. If the memory range defined by StartAddr
*    and NumBytes is larger than the capacity of the NOR flash device
*    than NumBytes is rounded down so that the memory range fits into
*    the NOR flash device.
*
*    The Block Map NOR driver can work only with physical sectors
*    of the same size. The longest continuous range of physical sectors
*    with the same size is chosen that fits in the range defined by
*    StartAddr and NumBytes parameters.
*/
void FS_NOR_BM_Configure(U8 Unit, U32 BaseAddr, U32 StartAddr, U32 NumBytes) {
  NOR_BM_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    ASSERT_PHY_TYPE_IS_SET(pInst);
    if (pInst->pPhyType != NULL) {
      pInst->pPhyType->pfConfigure(Unit, BaseAddr, StartAddr, NumBytes);
    }
  }
}

#if FS_NOR_ENABLE_STATS

/*********************************************************************
*
*       FS_NOR_BM_GetStatCounters
*
*  Function description
*    Returns the values of the statistical counters.
*
*  Parameters
*    Unit           Index of the Block Map NOR driver (0-based).
*    pStat          [OUT] Statistical counter values.
*
*  Additional information
*    This function is optional. The application can use it to get
*    the actual values of the statistical counters maintained by
*    the Block Map NOR driver. The statistical counters provide
*    information about the number of internal operations performed
*    by the Block Map NOR driver such as sector read and write.
*    All statistical counters are set to 0 when the NOR flash device
*    is low-level mounted. The application can explicitly set them
*    to 0 by using FS_NOR_BM_ResetStatCounters(). A separate set of
*    statistical counters is maintained for each instance of the
*    Block Map NOR driver.
*
*    The statistical counters are available only when the block
*    map NOR driver is compiled with the FS_DEBUG_LEVEL configuration
*    define set to a value greater than or equal to FS_DEBUG_LEVEL_CHECK_ALL
*    or with the FS_NOR_ENABLE_STATS configuration define set to 1.
*/
void FS_NOR_BM_GetStatCounters(U8 Unit, FS_NOR_BM_STAT_COUNTERS * pStat) {
  NOR_BM_INST * pInst;

  pInst  = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    if (pStat != NULL) {
      *pStat = pInst->StatCounters;
    }
  }
}

/*********************************************************************
*
*       FS_NOR_BM_ResetStatCounters
*
*  Function description
*    Sets the value of the statistical counters to 0.
*
*  Parameters
*    Unit           Index of the Block Map NOR driver (0-based).
*
*  Additional information
*    This function is optional. The application can use it to set
*    the statistical counters maintained by the Block Map NOR driver
*    to 0. The statistical counters can be read via FS_NOR_BM_GetStatCounters()
*
*    FS_NOR_BM_ResetStatCounters() is available only when the block
*    map NOR driver is compiled with the FS_DEBUG_LEVEL configuration
*    define set to a value greater than or equal to FS_DEBUG_LEVEL_CHECK_ALL
*    or with the FS_NOR_ENABLE_STATS configuration define set to 1.
*/
void FS_NOR_BM_ResetStatCounters(U8 Unit) {
  NOR_BM_INST             * pInst;
  FS_NOR_BM_STAT_COUNTERS * pStat;
  U32                       NumFreeBlocks;
  U32                       NumValidSectors;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pStat = &pInst->StatCounters;
    NumFreeBlocks   = pStat->NumFreeBlocks;
    NumValidSectors = pStat->NumValidSectors;
    FS_MEMSET(pStat, 0, sizeof(FS_NOR_BM_STAT_COUNTERS));
    pStat->NumFreeBlocks   = NumFreeBlocks;
    pStat->NumValidSectors = NumValidSectors;
  }
}

#endif // FS_NOR_ENABLE_STATS

/*********************************************************************
*
*       FS_NOR_BM_SetPhyType
*
*  Function description
*    Configures the type of NOR physical layer.
*
*  Parameters
*    Unit           Index of the Block Map NOR driver (0-based).
*    pPhyType       [IN] NOR physical layer.
*
*  Additional information
*    This function is mandatory and it has to be called once in FS_X_AddDevices()
*    for each instance of the Block Map NOR driver created by the application.
*    Different instances of the Block Map NOR driver are identified by the Unit parameter.
*
*    Permitted values for the pPhyType parameter are:
*    +---------------------+--------------------------------------------------------------------------------------------------+
*    | Identifier          | Description                                                                                      |
*    +---------------------+--------------------------------------------------------------------------------------------------+
*    | FS_NOR_PHY_CFI_1x16 | One CFI compliant NOR flash device with 16-bit interface.                                        |
*    +---------------------+--------------------------------------------------------------------------------------------------+
*    | FS_NOR_PHY_CFI_2x16 | Two CFI compliant NOR flash device with 16-bit interfaces.                                       |
*    +---------------------+--------------------------------------------------------------------------------------------------+
*    | FS_NOR_PHY_DSPI     | This a pseudo physical layer that uses the physical layers FS_NOR_PHY_ST_M25 and FS_NOR_PHY_SFDP |
*    +---------------------+--------------------------------------------------------------------------------------------------+
*    | FS_NOR_PHY_SFDP     | Serial NOR flash devices that support Serial Flash Discoverable Parameters (SFDP)                |
*    +---------------------+--------------------------------------------------------------------------------------------------+
*    | FS_NOR_PHY_SPIFI    | Memory mapped serial quad NOR flash devices.                                                     |
*    +---------------------+--------------------------------------------------------------------------------------------------+
*    | FS_NOR_PHY_ST_M25   | Serial NOR flash devices compatible to ST ST25Pxx.                                               |
*    +---------------------+--------------------------------------------------------------------------------------------------+
*/
void FS_NOR_BM_SetPhyType(U8 Unit, const FS_NOR_PHY_TYPE * pPhyType) {
  NOR_BM_INST * pInst;

  pInst  = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    if (pPhyType != NULL) {
      pInst->pPhyType = pPhyType;
      pPhyType->pfOnSelectPhy(Unit);
    }
  }
}

/*********************************************************************
*
*       FS_NOR_BM_SetMaxEraseCntDiff
*
*  Function description
*    Configure the threshold for the active wear leveling operation.
*
*  Parameters
*    Unit           Index of the Block Map NOR driver (0-based).
*    EraseCntDiff   Difference between the erase counts.
*
*  Additional information
*    This function is optional. The application can use
*    FS_NOR_BM_SetMaxEraseCntDiff() to modify the value of
*    the threshold that is used to decide if a data block has to be
*    relocated to make sure that is equally erased. The block map
*    NOR driver compares the difference between the erase counts
*    of two blocks with the configured value. If the calculated
*    difference is larger than EraseCntDiff then the block with
*    the smaller erase count is copied to the block with the larger
*    erase count then the block with the smaller erase count is
*    used to store new data to it.
*/
void FS_NOR_BM_SetMaxEraseCntDiff(U8 Unit, U32 EraseCntDiff) {
  NOR_BM_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->MaxEraseCntDiff = EraseCntDiff;
  }
}

/*********************************************************************
*
*       FS_NOR_BM_SetNumWorkBlocks
*
*  Function description
*    Configures the number of work blocks.
*
*  Parameters
*    Unit           Index of the Block Map NOR driver (0-based).
*    NumWorkBlocks  Number of work blocks to configure.
*
*  Additional information
*    This function is optional. Work blocks are physical sectors that
*    are used by the Block Map NOR driver to temporarily store the data
*    written to NOR flash device by the file system layer. An application
*    can use this function to modify the number of work blocks used by the
*    Block Map NOR driver according to its requirements. By default,
*    the Block Map NOR driver allocates 1% from the total number of physical
*    sectors configured for storage but no more than 10 work blocks.
*    The minimum number of work blocks allocated by default depends on whether
*    journaling is activated or not in the application. If the journal is active
*    the 4 work blocks are allocated otherwise 3.
*
*    The write performance of the Block Map NOR driver can be improved by
*    increasing the number work blocks which at the same time increases the
*    RAM usage.
*
*    The NOR flash device has to be reformatted in order for the new number of
*    work blocks to take effect.
*/
void FS_NOR_BM_SetNumWorkBlocks(U8 Unit, unsigned NumWorkBlocks) {
  NOR_BM_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->NumWorkBlocksConf = (U8)NumWorkBlocks;
  }
}

/*********************************************************************
*
*       FS_NOR_BM_SetSectorSize
*
*   Function description
*     Configures the number of bytes in a logical sector.
*
*  Parameters
*    Unit           Index of the Block Map NOR driver (0-based).
*    SectorSize     Number of bytes in a logical sector.
*
*  Additional information
*    This function is optional. It can be used to modify the size
*    of the logical sector used by the Block Map NOR driver.
*    By default the Block Map NOR driver uses the logical sector
*    size configured a file system level that is set to 512 bytes
*    at the file system initialization and can be later changed
*    via FS_SetMaxSectorSize(). The NOR flash device has to be
*    reformatted in order for the new logical sector size to take
*    effect.
*
*    SectorSize has to be a power of 2 value.
*/
void FS_NOR_BM_SetSectorSize(U8 Unit, unsigned SectorSize) {
  NOR_BM_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->BytesPerSectorConf = (U16)SectorSize;
  }
}

/*********************************************************************
*
*       FS_NOR_BM_GetDiskInfo
*
*   Function description
*     Returns information about the organization and the management
*     of the NOR flash device.
*
*  Parameters
*    Unit           Index of the Block Map NOR driver (0-based).
*    pDiskInfo      [OUT] Requested information.
*
*  Return value
*    ==0 - OK, information returned.
*    !=0 - An error occurred.
*
*  Additional information
*    This function is not required for the functionality of the block
*    map NOR driver and will typically not be linked in production builds.
*/
int FS_NOR_BM_GetDiskInfo(U8 Unit, FS_NOR_BM_DISK_INFO * pDiskInfo) {
  NOR_BM_INST * pInst;
  unsigned      iSector;
  U16           NumPhySectors;
  U16           NumUsedPhySectors;
  U16           NumPhySectorsValid;
  U32           EraseCntMax;
  U32           EraseCntMin;
  U32           EraseCntAvg;
  U32           EraseCnt;
  U32           EraseCntTotal;
  U32           NumEraseCnt;
  int           r;
  NOR_BM_PSH    psh;
  int           Result;
  U16           NumWorkBlocks;

  if (pDiskInfo == NULL) {
    return 1;                             // Error, invalid parameter.
  }
  //
  // Allocate a driver instance, initialize it and mount the NOR flash.
  //
  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {                    // Error, could not allocate driver instance.
    return 1;
  }
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return 1;                             // Error, could not initialize the NOR flash.
  }
  FS_MEMSET(pDiskInfo, 0, sizeof(FS_NOR_BM_DISK_INFO));
  //
  // Retrieve the number of used physical blocks and block status.
  //
  NumUsedPhySectors  = 0;
  NumPhySectorsValid = 0;
  NumPhySectors      = pInst->NumPhySectors;
  EraseCntMax        = 0;
  EraseCntMin        = 0xFFFFFFFFuL;
  EraseCntAvg        = 0;
  NumEraseCnt        = 0;
  EraseCntTotal      = 0;
  NumWorkBlocks      = 0;
  r = _LowLevelMountIfRequired(pInst);
  if (r == 0) {
    //
    // Iterate over all physical sectors (except the first one which contains only format information)
    // and collect information about the number of physical sectors in use and about the erase counts.
    //
    for (iSector = PSI_FIRST_STORAGE_BLOCK; iSector < NumPhySectors; iSector++) {
      //
      // Check if the physical sector is not used for data storage
      //
      if (_IsPhySectorFree(pInst, iSector) == 0) {
        NumUsedPhySectors++;
      }
      FS_MEMSET(&psh, 0xFF, sizeof(NOR_BM_PSH));
      Result = _ReadPSH(pInst, iSector, &psh);
      if (Result == 0) {
        NumPhySectorsValid++;
        EraseCnt = psh.EraseCnt;
        if ((EraseCnt != ERASE_CNT_INVALID) && (EraseCnt < (U32)FS_NOR_MAX_ERASE_CNT)) {
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
    NumWorkBlocks = pInst->NumWorkBlocks;
  }
  pDiskInfo->NumPhySectors      = NumPhySectors;
  pDiskInfo->NumLogBlocks       = pInst->NumLogBlocks;
  pDiskInfo->NumUsedPhySectors  = NumUsedPhySectors;
  pDiskInfo->LSectorsPerPSector = pInst->LSectorsPerPSector;
  pDiskInfo->BytesPerSector     = (U16)(1uL << pInst->ldBytesPerSector);
  pDiskInfo->EraseCntMax        = EraseCntMax;
  pDiskInfo->EraseCntMin        = EraseCntMin;
  pDiskInfo->EraseCntAvg        = EraseCntAvg;
  pDiskInfo->IsWriteProtected   = pInst->IsWriteProtected;
  pDiskInfo->HasFatalError      = pInst->HasFatalError;
  pDiskInfo->ErrorType          = pInst->ErrorType;
  pDiskInfo->ErrorPSI           = pInst->ErrorPSI;
  pDiskInfo->IsWLSuspended      = pInst->IsWLSuspended;
  pDiskInfo->MaxEraseCntDiff    = pInst->MaxEraseCntDiff;
  pDiskInfo->NumEraseCnt        = (U16)NumEraseCnt;
  pDiskInfo->NumPhySectorsValid = NumPhySectorsValid;
  pDiskInfo->NumWorkBlocks      = NumWorkBlocks;
  return r;
}

/*********************************************************************
*
*       FS_NOR_BM_GetSectorInfo
*
*  Function description
*    Returns information about a specified physical sector.
*
*  Parameters
*    Unit           Index of the Block Map NOR driver (0-based).
*    PhySectorIndex Index of the physical sector to be queried (0-based).
*    pSectorInfo    [OUT] Information related to the specified physical sector.
*
*  Return value
*    ==0 - OK, information returned.
*    !=0 - An error occurred.
*
*  Additional information
*    This function is optional. The application can use it to get
*    information about the usage of a particular physical sector.
*
*    PhySectorIndex is relative to the beginning of the region configured
*    as storage via FS_NOR_BM_Configure().
*/
int FS_NOR_BM_GetSectorInfo(U8 Unit, U32 PhySectorIndex, FS_NOR_BM_SECTOR_INFO * pSectorInfo) {
  int           r;
  U32           Off;
  U32           NumBytes;
  U8            Type;
  U32           EraseCnt;
  U16           lbi;
  unsigned      DataStat;
  NOR_BM_PSH    psh;
  NOR_BM_INST * pInst;

  if (pSectorInfo == NULL) {
    return 1;                             // Error, invalid parameter.
  }
  //
  // Allocate a driver instance, initialize it and mount the NOR flash.
  //
  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;                             // Error, invalid driver instance.
  }
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return 1;                             // Error, could not initialize the NOR flash.
  }
  FS_MEMSET(pSectorInfo, 0, sizeof(FS_NOR_BM_SECTOR_INFO));
  //
  // Check if the physical sector is not used for data storage
  //
  Type     = FS_NOR_BLOCK_TYPE_UNKNOWN;
  EraseCnt = 0;
  lbi      = 0;
  Off      = 0;
  NumBytes = 0;
  _GetPhySectorInfo(pInst, PhySectorIndex, &Off, &NumBytes);
  r = _LowLevelMountIfRequired(pInst);
  if (r == 0) {
    FS_MEMSET(&psh, 0xFF, sizeof(psh));
    r = _ReadPSH(pInst, PhySectorIndex, &psh);
    if (r == 0) {
      EraseCnt = psh.EraseCnt;
      lbi      = (U16)_GetPhySectorLBI(pInst, &psh);
      DataStat = _GetPhySectorDataStat(pInst, &psh);
      if (DataStat == DATA_STAT_VALID) {
        Type = FS_NOR_BLOCK_TYPE_DATA;
      } else if (DataStat == DATA_STAT_WORK) {
        Type = FS_NOR_BLOCK_TYPE_WORK;
      } else if (DataStat == DATA_STAT_EMPTY) {
        Type = FS_NOR_BLOCK_TYPE_EMPTY_ERASED;
      } else if (DataStat == DATA_STAT_INVALID) {
        Type = FS_NOR_BLOCK_TYPE_EMPTY_NOT_ERASED;
      } else {
        //
        // Unknown block type.
        //
      }
    }
  }
  pSectorInfo->Off      = Off;
  pSectorInfo->Size     = NumBytes;
  pSectorInfo->EraseCnt = EraseCnt;
  pSectorInfo->lbi      = lbi;
  pSectorInfo->Type     = Type;
  return r;
}

/*********************************************************************
*
*       FS_NOR_BM_ReadOff
*
*  Function description
*    Reads a range of bytes from the NOR flash device.
*
*  Parameters
*    Unit           Index of the Block Map NOR driver (0-based).
*    pData          [OUT] Read data.
*    Off            Offset of the first byte to be read.
*    NumBytes       Number of bytes to be read.
*
*  Return value
*    ==0 - OK, data read.
*    !=0 - An error occurred.
*
*  Additional information
*    This function is not required for the functionality of the driver
*    and will typically not be linked in production builds.
*
*    Off has to be specified in bytes and is relative to the beginning
*    of the NOR flash area configured via FS_NOR_BM_Configure().
*/
int FS_NOR_BM_ReadOff(U8 Unit, void * pData, U32 Off, U32 NumBytes) {
  NOR_BM_INST * pInst;
  int           r;
  U32           OffSector;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;
  }
  r = _InitIfRequired(pInst);
  if (r == 0) {
    OffSector = 0;
    _GetPhySectorInfo(pInst, 0, &OffSector, NULL);
    Off += OffSector;
    r = _ReadOff(pInst, pData, Off, NumBytes);
  }
  return r;
}

/*********************************************************************
*
*       FS_NOR_BM_IsLLFormatted
*
*   Function description
*     Checks it the NOR flash is low-level formatted.
*
*  Parameters
*    Unit           Index of the Block Map NOR driver (0-based).
*
*  Return value
*    !=0 - The NOR flash device is low-level formatted.
*    ==0 - The NOR flash device is not low-level formatted or an error has occurred.
*
*  Additional information
*    This function is optional. An application should use FS_IsLLFormatted() instead.
*/
int FS_NOR_BM_IsLLFormatted(U8 Unit) {
  NOR_BM_INST * pInst;
  int           r;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 0;         // Error, could not initialize NOR driver instance. Return that the NOR flash device is not formatted.
  }
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return 0;         // Error, could not initialize NOR flash device. Return that the NOR flash device is not formatted.
  }
  r = _LowLevelMountIfRequired(pInst);
  if (r != 0) {
    return 0;         // NOR flash not formatted.
  }
  return 1;           // NOR flash formatted.
}

#if FS_NOR_SUPPORT_FORMAT

/*********************************************************************
*
*       FS_NOR_BM_FormatLow
*
*  Function description
*    Performs a low-level format of NOR flash device.
*
*  Parameters
*    Unit           Index of the Block Map NOR driver (0-based).
*
*  Return value
*    ==0 - OK, NOR flash device has been successfully low-level formated.
*    !=0 - An error occurred.
*
*  Additional information
*    This function is optional. FS_NOR_BM_FormatLow() erases the first
*    physical sector and stores the format information in it. The other
*    physical sectors are either erased or invalidated. Per default
*    the physical sectors are invalidated in order to reduce the time
*    it takes for the operation to complete. The application can request
*    the Block Map NOR driver to erase the physical sectors instead
*    of invalidating them by calling FS_NOR_BM_SetUsedSectorsErase()
*    with the Off parameter set to 1.
*/
int FS_NOR_BM_FormatLow(U8 Unit) {
  NOR_BM_INST * pInst;
  int           r;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;           // Error, driver instance not available.
  }
  _Unmount(pInst);
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return 1;           // Error, could not initialize the driver instance.
  }
  r = _LowLevelFormat(pInst);
  return r;
}

#endif // FS_NOR_SUPPORT_FORMAT

/*********************************************************************
*
*      FS_NOR_BM_EraseDevice
*
*  Function description
*    Erases all the physical sectors configured as storage.
*
*  Parameters
*    Unit           Index of the Block Map NOR driver (0-based).
*
*  Return value
*    ==0 - Physical sectors erased.
*    !=0 - An error occurred.
*
*  Additional information
*    This function is optional. After the call to this function
*    all the bytes in area of the NOR flash device configured
*    as storage are set to 0xFF.
*/
int FS_NOR_BM_EraseDevice(U8 Unit) {
  NOR_BM_INST * pInst;
  int           r;
  U32           NumPhySectors;
  U32           iSector;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;             // Error, driver instance not available.
  }
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return 1;             // Error, could not initialize the driver.
  }
  _Unmount(pInst);
  NumPhySectors = pInst->NumPhySectors;
  for (iSector = 0; iSector < NumPhySectors; ++iSector) {
    r = _ErasePhySector(pInst, iSector, NULL);
    if (r != 0) {
      return 1;           // Error, could not erase physical sector.
    }
  }
  return 0;               // OK, all physical sectors have been erased.
}

#if FS_NOR_VERIFY_ERASE

/*********************************************************************
*
*       FS_NOR_BM_SetEraseVerification
*
*  Function description
*    Enables or disables the checking of the sector erase operation.
*
*  Parameters
*    Unit           Index of the Block Map NOR driver (0-based).
*    OnOff          Specifies if the feature has to be enabled or disabled
*                   * ==0   The erase operation is not checked.
*                   * !=0   The erase operation is checked.
*
*  Additional information
*    This function is optional. The result of a sector erase operation
*    is normally checked by evaluating the error bits maintained by the
*    NOR flash device in a internal status register. FS_NOR_BM_SetEraseVerification()
*    can be used to enable additional verification of the sector erase
*    operation that is realized by reading back the contents of the entire
*    erased physical sector and by checking that all the bytes in it are
*    set to 0xFF. Enabling this feature can negatively impact the write
*    performance of Block Map NOR driver.
*
*    The sector erase verification feature is active only when the block map
*    NOR driver is compiled with the FS_NOR_VERIFY_ERASE configuration
*    define is set to 1 (default is 0) or when the FS_DEBUG_LEVEL
*    configuration define is set to a value greater than or equal to
*    FS_DEBUG_LEVEL_CHECK_ALL.
*/
void FS_NOR_BM_SetEraseVerification(U8 Unit, U8 OnOff) {
  NOR_BM_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->VerifyErase = OnOff;
  }
}

#endif // FS_NOR_VERIFY_ERASE

#if FS_NOR_VERIFY_WRITE

/*********************************************************************
*
*       FS_NOR_BM_SetWriteVerification
*
*  Function description
*    Enables or disables the checking of the page write operation.
*
*  Parameters
*    Unit           Index of the Block Map NOR driver (0-based).
*    OnOff          Specifies if the feature has to be enabled or disabled
*                   * ==0   The write operation is not checked.
*                   * !=0   The write operation is checked.
*
*  Additional information
*    This function is optional. The result of a page write operation
*    is normally checked by evaluating the error bits maintained by the
*    NOR flash device in a internal status register. FS_NOR_BM_SetWriteVerification()
*    can be used to enable additional verification of the page write
*    operation that is realized by reading back the contents of the written
*    page and by checking that all the bytes are matching the data
*    requested to be written. Enabling this feature can negatively
*    impact the write performance of Block Map NOR driver.
*
*    The page write verification feature is active only when the block map
*    NOR driver is compiled with the FS_NOR_VERIFY_WRITE configuration
*    define is set to 1 (default is 0) or when the FS_DEBUG_LEVEL
*    configuration define is set to a value greater than or equal to
*    FS_DEBUG_LEVEL_CHECK_ALL.
*/
void FS_NOR_BM_SetWriteVerification(U8 Unit, U8 OnOff) {
  NOR_BM_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->VerifyWrite = OnOff;
  }
}

#endif // FS_NOR_VERIFY_WRITE

#if FS_NOR_SKIP_BLANK_SECTORS

/*********************************************************************
*
*       FS_NOR_BM_SetBlankSectorSkip
*
*  Function description
*    Configures if the physical sectors which are already blank
*    should be erased during the low-level format operation.
*
*  Parameters
*    Unit           Index of the Block Map NOR driver (0-based).
*    OnOff          Specifies if the feature has to be enabled or disabled
*                   * ==0   All the physical sectors are erased.
*                   * !=0   Physical sectors are not erased if they are blank (default).
*
*  Additional information
*    This function is optional. The blank checking feature is disabled
*    by default and has to be explicitly enabled at compile time by
*    setting FS_NOR_SKIP_BLANK_SECTORS to 1. The feature can then be
*    enabled or disabled at runtime using FS_NOR_BM_SetBlankSectorSkip().
*
*    Activating this feature can improve the speed of the low-level
*    format operation when most of the physical sectors of the NOR flash
*    device are already blank which is typically the case with NOR flash
*    devices that ship from factory.
*
*    It is not recommended to enable this feature when performing a low-level
*    format of a NOR flash device that was already used for data storage
*    because the blank check operation is not able to determine if a physical
*    sector was completely erased only by checking that all the bits are set to 1.
*    An erase operation that is interrupted by an unexpected reset may leave
*    the storage cells in an instable state even when the values of all storage
*    cells in a physical sector read back as 1.
*/
void FS_NOR_BM_SetBlankSectorSkip(U8 Unit, U8 OnOff) {
  NOR_BM_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->SkipBlankSectors = OnOff;
  }
}

#endif // FS_NOR_SKIP_BLANK_SECTORS

/*********************************************************************
*
*       FS_NOR_BM_SetUsedSectorsErase
*
*  Function description
*    Configures if the physical sectors have to be erased at low-level format.
*
*  Parameters
*    Unit           Index of the Block Map NOR driver (0-based).
*    OnOff          Specifies if the feature has to be enabled or disabled
*                   * ==0   Physical sectors are invalidated (default).
*                   * !=0   Physical sectors are erased.
*
*  Additional information
*    This function is optional. The default behavior of the block map
*    NOR driver is to invalidate the physical sectors at low-level format
*    which makes the format operation faster.
*/
void FS_NOR_BM_SetUsedSectorsErase(U8 Unit, U8 OnOff) {
  NOR_BM_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->EraseUsedSectors = OnOff;
  }
}

/*********************************************************************
*
*       FS_NOR_BM_WriteOff
*
*  Function description
*    Writes data to NOR flash memory.
*
*  Parameters
*    Unit           Index of the Block Map NOR driver (0-based).
*    pData          [IN] Data to be written.
*    Off            Location where to write the data.
*    NumBytes       Number of bytes to be written.
*
*  Return value
*    ==0 - OK, data written successfully.
*    !=0 - An error occurred.
*
*  Additional information
*    Off has to be specified in bytes and is relative to the beginning
*    of the NOR flash area configured via FS_NOR_BM_Configure().
*
*    FS_NOR_BM_WriteOff() is able to write across page and physical
*    sector boundaries. This function can only change bit values from
*    1 to 0. The bits can be set to 1 block-wise via FS_NOR_BM_ErasePhySector().
*
*    The function takes care of the alignment required when writing
*    to NOR flash devices with line size larger than 1.
*/
int FS_NOR_BM_WriteOff(U8 Unit, const void * pData, U32 Off, U32 NumBytes) {
  NOR_BM_INST * pInst;
  int           r;
  U32           OffSector;
#if (FS_NOR_LINE_SIZE > 1)
#if (FS_NOR_LINE_SIZE > 4)
  U32           aBuffer[FS_NOR_LINE_SIZE / 4];
#else
  U16           aBuffer[FS_NOR_LINE_SIZE / 2];
#endif
  U32           OffLine;
  U32           NumBytesAtOnce;
  const U8    * pData8;
  U8          * pBuffer8;
  U32           NumLines;
  U32           OffAligned;
  unsigned      ldBytesPerLine;
  unsigned      BytesPerLine;
#endif // FS_NOR_LINE_SIZE > 1

  if (NumBytes == 0u) {
    return 0;                                       // OK, nothing to write.
  }
  if (pData == NULL) {
    return 1;                                       // Error, invalid parameter.
  }
  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;                                       // Error, could not get the driver instance.
  }
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return 1;                                       // Error, could not initialize driver instance.
  }
  OffSector = 0;
  _GetPhySectorInfo(pInst, 0, &OffSector, NULL);
  Off += OffSector;
#if (FS_NOR_LINE_SIZE > 1)
  //
  // Take care of the first bytes that are not aligned to a flash line.
  //
#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  ldBytesPerLine = pInst->ldBytesPerLine;
#else
  ldBytesPerLine = _ld(FS_NOR_LINE_SIZE);
#endif
  BytesPerLine = 1uL << ldBytesPerLine;
  pData8 = SEGGER_CONSTPTR2PTR(const U8, pData);    // MISRA deviation D:100e
  OffLine = Off & (BytesPerLine - 1u);
  if (OffLine != 0u) {
    NumBytesAtOnce  = BytesPerLine - OffLine;
    NumBytesAtOnce  = SEGGER_MIN(NumBytesAtOnce, NumBytes);
    OffAligned      = Off & ~(BytesPerLine - 1u);
    pBuffer8        = SEGGER_PTR2PTR(U8, aBuffer);  // MISRA deviation D:100e
    pBuffer8       += OffLine;
    r = _ReadOff(pInst, aBuffer, OffAligned, BytesPerLine);
    if (r != 0) {
      return 1;                                     // Error, could not read data.
    }
    FS_MEMCPY(pBuffer8, pData8, NumBytesAtOnce);
    r = _WriteOff(pInst, aBuffer, OffAligned, BytesPerLine);
    if (r != 0) {
      return r;                                     // Error, could not write data.
    }
    Off      += NumBytesAtOnce;
    NumBytes -= NumBytesAtOnce;
    pData8   += NumBytesAtOnce;
  }
  if (NumBytes != 0u) {
    //
    // Write entire flash lines.
    //
    NumLines = NumBytes >> ldBytesPerLine;
    if (NumLines != 0u) {
      NumBytesAtOnce = NumLines << ldBytesPerLine;
      if ((SEGGER_PTR2ADDR(pData8) & 3u) == 0u) {   // MISRA deviation D:100e
        r = _WriteOff(pInst, pData8, Off, NumBytesAtOnce);
        if (r != 0) {
          return r;                                 // Error, could not write data.
        }
        Off      += NumBytesAtOnce;
        NumBytes -= NumBytesAtOnce;
        pData8   += NumBytesAtOnce;
      } else {
        NumBytesAtOnce = 1uL << ldBytesPerLine;
        do {
          FS_MEMCPY(aBuffer, pData8, NumBytesAtOnce);
          r = _WriteOff(pInst, aBuffer, Off, NumBytesAtOnce);
          if (r != 0) {
            return r;                               // Error, could not write data.
          }
          Off      += NumBytesAtOnce;
          NumBytes -= NumBytesAtOnce;
          pData8   += NumBytesAtOnce;
        } while (--NumLines != 0u);
      }
    }
  }
  if (NumBytes != 0u) {
    //
    // Take care of the last bytes that are not aligned to a flash line.
    //
    r = _ReadOff(pInst, aBuffer, Off, BytesPerLine);
    if (r != 0) {
      return 1;                                     // Error, could not read data.
    }
    FS_MEMCPY(aBuffer, pData8, NumBytes);
    r = _WriteOff(pInst, aBuffer, Off, BytesPerLine);
    if (r != 0) {
      return r;                                     // Error, could not write data.
    }
  }
#else
  r = _WriteOff(pInst, pData, Off, NumBytes);
#endif // FS_NOR_LINE_SIZE > 1
  return r;
}

/*********************************************************************
*
*       FS_NOR_BM_ErasePhySector
*
*  Function description
*    Sets all the bits in a physical sector to 1.
*
*  Parameters
*    Unit           Index of the Block Map NOR driver (0-based).
*    PhySectorIndex Index of the physical sector to be erased.
*
*  Return value
*    ==0 - OK, physical sector erased successfully.
*    !=0 - An error occurred.
*
*  Additional information
*    PhySectorIndex is 0-based and is relative to the beginning
*    of the NOR flash area configured via FS_NOR_BM_Configure().
*    The number of bytes actually erased depends on the size of the
*    physical sector supported by the NOR flash device.
*    Information about a physical sector can be obtained via
*    FS_NOR_BM_GetSectorInfo().
*/
int FS_NOR_BM_ErasePhySector(U8 Unit, U32 PhySectorIndex) {
  NOR_BM_INST * pInst;
  int           r;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;
  }
  r = _InitIfRequired(pInst);
  if (r == 0) {
    r = _ErasePhySector(pInst, PhySectorIndex, NULL);
    if (r != 0) {
      return 1;                           // Error, could not erase physical sector.
    }
  }
  return r;
}

/*********************************************************************
*
*       FS_NOR_BM_SetOnFatalErrorCallback
*
*  Function description
*    Registers a function to be called by the driver encounters a fatal error.
*
*  Parameters
*    pfOnFatalError     Address to the callback function.
*
*  Additional information
*    Typically, the NOR driver reports a fatal error when data integrity
*    check (CRC) fails.
*
*    All instances of the NOR driver share the same callback function.
*    The Unit member of the FS_NOR_FATAL_ERROR_INFO structure passed
*    as parameter to the pfOnFatalError callback function indicates which
*    driver instance triggered the fatal error.
*/
void FS_NOR_BM_SetOnFatalErrorCallback(FS_NOR_ON_FATAL_ERROR_CALLBACK * pfOnFatalError) {
  _pfOnFatalError = pfOnFatalError;
}

#if FS_NOR_SUPPORT_CRC

/*********************************************************************
*
*       FS_NOR_BM_EnableCRC
*
*  Function description
*    Enables the data integrity check.
*
*  Return value
*    ==0 - Data integrity check activated.
*    !=0 - An error occurred.
*
*  Additional information
*    The support for data integrity check is not available by default and
*    it has to be included at compile time via the FS_NOR_SUPPORT_CRC define.
*    This function enables the data integrity check for all instances
*    of the NOR driver.
*/
int FS_NOR_BM_EnableCRC(void) {
  _pCRC_API = &_CRC_API;
  return 0;
}

/*********************************************************************
*
*       FS_NOR_BM_DisableCRC
*
*  Function description
*    Disables the data integrity check.
*
*  Return value
*    ==0 - Data integrity check deactivated.
*    !=0 - An error occurred.
*
*  Additional information
*    The support for data integrity check is not available by default and
*    it has to be included at compile time via the FS_NOR_SUPPORT_CRC define.
*    This function disables the data integrity check for all instances
*    of the NOR driver.
*/
int FS_NOR_BM_DisableCRC(void) {
  _pCRC_API = NULL;
  return 0;
}

/*********************************************************************
*
*       FS_NOR_BM_IsCRCEnabled
*
*  Function description
*    Checks if the data integrity checking is enabled.
*
*  Return value
*    ==0 - Data integrity check is deactivated.
*    !=0 - Data integrity check is activated.
*
*  Additional information
*    This function is available only the file system sources are compiled
*    with FS_NOR_SUPPORT_CRC set to 1.
*/
int FS_NOR_BM_IsCRCEnabled(void) {
  int r;

  r = _IsCRCEnabled(NULL);
  return r;
}

#endif // FS_NOR_SUPPORT_CRC


#if FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER

/*********************************************************************
*
*       FS_NOR_BM_SetByteOrderLE
*
*  Function description
*    Sets the byte order of multi-byte management data to little-endian format.
*
*  Return value
*    ==0 - Multi-byte management data is stored in little-endian format.
*    !=0 - An error occurred.
*
*  Additional information
*    This function is optional. Multi-byte management data is stored
*    in the byte order of the CPU. Typically, FS_NOR_BM_SetByteOrderLE()
*    is used by the NOR Image Creator utility to create NOR images
*    with a byte order of the target CPU when the byte order of the
*    host CPU is different. The byte order can be set to big-endian
*    using FS_NOR_BM_SetByteOrderBE(). By default the byte order is
*    set to little-endian.
*
*    The support for configurable byte order is disabled by default and
*    has to be enabled by compiling the Block Map NOR driver with the
*    FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER configuration define set to 1
*    otherwise FS_NOR_BM_SetByteOrderLE() does nothing.
*/
int FS_NOR_BM_SetByteOrderLE(void) {
  _pMultiByteAPI = &_MultiByteAPI_LE;
  return 0;
}

/*********************************************************************
*
*       FS_NOR_BM_SetByteOrderBE
*
*  Function description
*    Sets the byte order of multi byte management data to big-endian (default).
*
*  Return value
*    ==0 - Multi-byte management data is stored in big-endian format.
*    !=0 - An error occurred.
*
*  Additional information
*    This function is optional. Multi-byte management data is stored
*    in the byte order of the CPU. Typically, FS_NOR_BM_SetByteOrderBE()
*    is used by the NOR Image Creator utility to create NOR images
*    with a byte order of the target CPU when the byte order of the
*    host CPU is different. The byte order can be set to little-endian
*    using FS_NOR_BM_SetByteOrderLE(). By default the byte order is
*    set to little-endian.
*
*    The support for configurable byte order is disabled by default and
*    has to be enabled by compiling the Block Map NOR driver with the
*    FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER configuration define set to 1
*    otherwise FS_NOR_BM_SetByteOrderBE() does nothing.
*/
int FS_NOR_BM_SetByteOrderBE(void) {
  _pMultiByteAPI = &_MultiByteAPI_BE;
  return 0;
}

#endif // FS_NOR_SUPPORT_VARIABLE_BYTE_ORDER

#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE

/*********************************************************************
*
*       FS_NOR_BM_SetDeviceLineSize
*
*  Function description
*    Configures the minimum number of bytes that can be written to NOR flash.
*
*  Parameters
*    Unit           Index of the Block Map NOR driver (0-based).
*    ldBytesPerLine Line size in bytes as power of 2 exponent.
*
*  Return value
*    ==0 - OK, line size changed.
*    !=0 - Error code indicating the failure reason.
*
*  Additional information
*    This function is optional. Typically, the NOR flash have lines smaller
*    than 4 bytes which is the fixed default value configured at compile time.
*    The FS_NOR_SUPPORT_VARIABLE_LINE_SIZE configuration define has to be set to a value
*    different than 0 in order to enable this function.
*/
int FS_NOR_BM_SetDeviceLineSize(U8 Unit, U8 ldBytesPerLine) {
  NOR_BM_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;             // Error, cannot get the driver instance.
  }
  pInst->ldBytesPerLine = ldBytesPerLine;
  _CalcUpdateSizeOfLSH(pInst);
  _CalcUpdateSizeOfPSH(pInst);
  return 0;
}

/*********************************************************************
*
*       FS_NOR_BM_SetDeviceRewriteSupport
*
*  Function description
*    Specifies if the NOR flash device can rewrite the same data
*    if 0s are preserved.
*
*  Parameters
*    Unit           Index of the Block Map NOR driver (0-based).
*    OnOff          Enables / disables the support for rewrite.
*                   * ==0   Rewrite support is disabled.
*                   * !=0   Rewrite support is enabled.
*
*  Return value
*    ==0 - OK, support for rewrite changed.
*    !=0 - Error code indicating the failure reason.
*
*  Additional information
*    This function is optional. Typically, the NOR flash devices are
*    able to rewrite the same data and by default this feature is
*    disabled at compile time. The FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
*    configuration define has to be set to a value different than 0
*    in order to enable this function, otherwise the function does nothing.
*/
int FS_NOR_BM_SetDeviceRewriteSupport(U8 Unit, U8 OnOff) {
  NOR_BM_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;             // Error, cannot get the driver instance.
  }
  pInst->IsRewriteSupported = OnOff;
  _CalcUpdateSizeOfLSH(pInst);
  _CalcUpdateSizeOfPSH(pInst);
  return 0;
}
#endif // FS_NOR_SUPPORT_VARIABLE_LINE_SIZE

#if FS_NOR_SUPPORT_CRC

/*********************************************************************
*
*       FS_NOR_BM_SetCRCHook
*
*  Function description
*    Configures the calculation routines for CRC verification.
*
*  Parameters
*    pCRCHook     CRC calculation routines.
*
*  Return value
*    ==0 - OK, CRC calculation routines set.
*    !=0 - Error code indicating the failure reason.
*
*  Additional information
*    This function is optional. The driver uses by default the internal
*    software routines of the file system for the CRC calculation.
*    FS_NOR_BM_SetCRCHook() can be used by the application to specify
*    different CRC calculation functions. This function is available
*    only when the FS_NOR_SUPPORT_CRC configuration define is set to 1.
*    In order to save ROM space, the internal CRC software routines can
*    be disabled a runtime by setting the FS_NOR_CRC_HOOK_DEFAULT
*    configuration define to NULL.
*/
int FS_NOR_BM_SetCRCHook(const FS_NOR_CRC_HOOK * pCRCHook) {
  _pCRCHook = pCRCHook;
  return 0;
}

#endif // FS_NOR_SUPPORT_CRC

#if FS_NOR_SUPPORT_FAIL_SAFE_ERASE

/*********************************************************************
*
*       FS_NOR_BM_SetFailSafeErase
*
*  Function description
*    Configures the fail-safe mode of the sector erase operation.
*
*  Parameters
*    Unit           Index of the Block Map NOR driver (0-based).
*    OnOff          Specifies if the feature has to be enabled or disabled
*                   * ==0   Sector erase operation is not fail-safe.
*                   * !=0   Sector erase operation is fail-safe.
*
*  Return value
*    ==0    OK, fail-safe mode configured.
*    !=0    Error code indicating the failure reason.
*
*  Additional information
*    This function is optional. It can be used by the application to
*    enable or disable the fail-safe sector erase feature of the block map
*    NOR driver. The fail-safe sector erase feature helps the driver
*    identify a sector erase operation that did not completed successfully
*    due to an unexpected power failure. The Block Map NOR driver enables
*    the fail-safe sector erase feature by default except when the NOR
*    flash device does not support modifying the same data more than once
*    (rewrite) or when the application activates the data integrity check
*    feature via CRC. This is done for backward compatibility reasons.
*    The application has to use FS_NOR_BM_SetFailSafeErase() to explicitly
*    enable the fail-safe sector erase feature for configurations that
*    require a fail-safe operation.
*
*    In addition, enabling the fail-safe sector erase feature has the potential
*    of improving the performance of the clean operation executed via
*    FS_STORAGE_Clean() and FS_STORAGE_CleanOne().
*
*    The value configured via FS_NOR_BM_SetFailSafeErase() is evaluated
*    only during the low-level format operation. That is, after changing
*    the activation status of the fail-safe sector erase feature a low-level
*    format operation has to be performed via FS_FormatLow() in order for
*    the new configuration to be applied.
*/
int FS_NOR_BM_SetFailSafeErase(U8 Unit, U8 OnOff) {
  NOR_BM_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;             // Error, cannot get the driver instance.
  }
  pInst->FailSafeEraseConf = OnOff;
  return 0;
}

#endif // FS_NOR_SUPPORT_FAIL_SAFE_ERASE

/*********************************************************************
*
*       FS_NOR_BM_SuspendWearLeveling
*
*  Function description
*    Disables temporarily the wear leveling process.
*
*  Parameters
*    Unit           Index of the Block Map NOR driver (0-based).
*
*  Additional information
*    This function is optional. Disabling the wear leveling can help
*    reduce the write latencies that occur when the Block Map NOR driver
*    has to make space for the new data by erasing storage blocks that
*    contain invalid data. With the wear leveling disabled the block map
*    NOR driver searches for an already empty block (i.e. a storage block
*    that does not have to be erased before use) when it needs more space.
*    Empty blocks are created when the application performs a clean operation
*    via FS_STORAGE_Clean() or FS_STORAGE_CleanOne(). The wear leveling is
*    automatically re-enabled by the Block Map NOR driver when no more
*    empty storage blocks are available and an erase operation is required
*    to create one.
*
*    FS_NOR_BM_SuspendWearLeveling() disables only the wear leveling operation
*    performed when the file system writes data to NOR flash device. The wear
*    leveling is still performed during a clean operation.
*
*    The activation status of the wear leveling is returned via the IsWLSuspended
*    member of the FS_NOR_BM_DISK_INFO structure that can be queried via
*    FS_NOR_BM_GetDiskInfo()
*/
void FS_NOR_BM_SuspendWearLeveling(U8 Unit) {
  NOR_BM_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->IsWLSuspended = 1;
  }
}

/*********************************************************************
*
*       FS_NOR_BM_SetInvalidSectorError
*
*  Function description
*    Configures if an error is reported when an invalid sector is read.
*
*  Parameters
*    Unit           Index of the Block Map NOR driver (0-based).
*    OnOff          Specifies if the feature has to be enabled or disabled
*                   * ==0   No error is reported.
*                   * !=0   An error is reported.
*
*  Additional information
*    This function is optional. An invalid sector is a logical sector
*    that does not store any data. All logical sectors are invalid after
*    a low-level format operation. A logical sector becomes valid as
*    soon as the file system writes some data to it. The file system
*    does not read invalid sectors with the exception of a high-level
*    format operation.
*
*    By default the Block Map NOR driver does not report any error when
*    the file system reads an invalid sector. The contents of an invalid
*    sector is filled with the byte pattern specified via FS_NOR_READ_BUFFER_FILL_PATTERN.
*/
void FS_NOR_BM_SetInvalidSectorError(U8 Unit, U8 OnOff) {
  NOR_BM_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->InvalidSectorError = OnOff;
  }
}

/*********************************************************************
*
*       FS_NOR_BM_Mount
*
*  Function description
*    Mounts the NOR flash device.
*
*  Parameters
*    Unit         Index of the driver instance (0-based).
*    pMountInfo   [OUT] Information about the mounted NOR flash device. Can be set to NULL.
*
*  Return value
*    ==0    OK, NOR flash device successfully mounted.
*    !=0    Error, could not mount NOR flash device.
*
*  Additional information
*    FS_NOR_BM_Mount() can be used to explicitly mount the NOR flash
*    device and to get information about it. This function returns a subset
*    of the information returned by FS_NOR_BM_GetDiskInfo() and therefore
*    can be used instead of it if the application does not require statistical
*    information about the usage of the NOR sectors. Typically, FS_NOR_BM_Mount()
*    requires less time to complete than FS_NOR_BM_GetDiskInfo().
*
*    This function is not required for the functionality of the
*    Block Map driver and is typically not linked in production
*    builds.
*/
int FS_NOR_BM_Mount(U8 Unit, FS_NOR_BM_MOUNT_INFO * pMountInfo) {
  NOR_BM_INST * pInst;
  int           r;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;         // Error, could not allocate driver instance.
  }
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return 1;         // Error, device initialization failed.
  }
  //
  // Some of the information can be collected only when the NOR flash is low-level mounted.
  //
  r = _LowLevelMountIfRequired(pInst);
  if (r != 0) {
    return 1;         // Error, could not mount NOR flash device.
  }
  //
  // Return information about the mounted NOR flash device to the caller.
  //
  if (pMountInfo != NULL) {
    FS_MEMSET(pMountInfo, 0, sizeof(FS_NOR_BM_MOUNT_INFO));
    //
    // Retrieve the number of used physical blocks and block status.
    //
    pMountInfo->NumPhySectors      = pInst->NumPhySectors;
    pMountInfo->NumLogBlocks       = pInst->NumLogBlocks;
    pMountInfo->LSectorsPerPSector = pInst->LSectorsPerPSector;
    pMountInfo->BytesPerSector     = (U16)(1uL << pInst->ldBytesPerSector);
    pMountInfo->IsWriteProtected   = pInst->IsWriteProtected;
    pMountInfo->HasFatalError      = pInst->HasFatalError;
    pMountInfo->ErrorType          = pInst->ErrorType;
    pMountInfo->ErrorPSI           = pInst->ErrorPSI;
    pMountInfo->IsWLSuspended      = pInst->IsWLSuspended;
    pMountInfo->NumWorkBlocks      = pInst->NumWorkBlocks;
  }
  return 0;           // OK, NOR flash device successfully mounted.
}

#if FS_NOR_SUPPORT_ECC

/*********************************************************************
*
*       FS_NOR_BM_DisableECC
*
*  Function description
*    Disables the bit error correction via ECC.
*
*  Parameters
*    Unit         Index of the driver instance (0-based).
*
*  Return value
*    ==0    OK, ECC feature disabled.
*    !=0    An error occurred.
*
*  Additional information
*    This function is available only the file system sources are compiled
*    with FS_NOR_SUPPORT_ECC set to 1.
*/
int FS_NOR_BM_DisableECC(U8 Unit) {
  NOR_BM_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;         // Error, could not allocate driver instance.
  }
  pInst->pECC_API = NULL;
  return 0;
}

/*********************************************************************
*
*       FS_NOR_BM_EnableECC
*
*  Function description
*    Enables the bit error correction via ECC.
*
*  Parameters
*    Unit         Index of the driver instance (0-based).
*
*  Return value
*    ==0    OK, ECC feature enabled.
*    !=0    An error occurred.
*
*  Additional information
*    This function is available only the file system sources are compiled
*    with FS_NOR_SUPPORT_ECC set to 1.
*/
int FS_NOR_BM_EnableECC(U8 Unit) {
  NOR_BM_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;         // Error, could not allocate driver instance.
  }
  pInst->pECC_API = &_ECC_API;
  return 0;
}

/*********************************************************************
*
*       FS_NOR_BM_IsECCEnabled
*
*  Function description
*    Checks if the bit error correction via ECC is enabled or not.
*
*  Parameters
*    Unit         Index of the driver instance (0-based).
*
*  Return value
*    !=0    The ECC feature is enabled.
*    ==0    The ECC feature is disabled.
*
*  Additional information
*    This function is available only the file system sources are compiled
*    with FS_NOR_SUPPORT_ECC set to 1.
*/
int FS_NOR_BM_IsECCEnabled(U8 Unit) {
  NOR_BM_INST * pInst;
  int           r;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 0;         // Error, could not allocate driver instance.
  }
  r = _IsECCEnabled(pInst);
  return r;
}

/*********************************************************************
*
*       FS_NOR_BM_SetECCHook
*
*  Function description
*    Configures the functions to be used for performing
*    the bit error correction via ECC.
*
*  Parameters
*    Unit           Index of the driver instance (0-based).
*    pECCHookMan    Functions for the bit error correction of the management data.
*    pECCHookData   Functions for the bit error correction of the sector data.
*
*  Return value
*    ==0    OK, ECC functions registered.
*    !=0    An error occurred.
*
*  Additional information
*    This function is optional. The driver uses by default the internal
*    software routines of the file system for the parity check calculation.
*    FS_NOR_BM_SetECCHook() can be used by the application to specify
*    different parity check calculation functions. This function is available
*    only when the FS_NOR_SUPPORT_ECC configuration define is set to 1.
*    In order to save ROM space, the internal CRC software routines can
*    be disabled a runtime by setting the FS_NOR_ECC_HOOK_MAN_DEFAULT
*    and FS_NOR_ECC_HOOK_DATA_DEFAULT configuration define to NULL.
*/
int FS_NOR_BM_SetECCHook(U8 Unit, const FS_NOR_ECC_HOOK * pECCHookMan, const FS_NOR_ECC_HOOK * pECCHookData) {
  NOR_BM_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;         // Error, could not allocate driver instance.
  }
  pInst->pECCHookMan  = pECCHookMan;
  pInst->pECCHookData = pECCHookData;
  return 0;
}

#endif // FS_NOR_SUPPORT_ECC

/*************************** End of file ****************************/
