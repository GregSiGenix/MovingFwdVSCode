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
File        : FS_NOR_Drv.c
Purpose     : File system high level NOR FLASH driver
----------------------------------------------------------------------
General info on the inner workings of this high level flash driver:

Layered approach
================
All read, write and erase operations are performed by the low level
flash driver. The low level flash driver is also responsible for
returning information about the organization of the NOR flash.
This driver assumes the following:
- The flash is organized in physical sectors
- The phys. sectors are at least 1kb in size
- Erasing a phys. sector fills all bytes with FF
- Writing is permitted in arbitrary units (bytes)
- Writing can change bits from 1 to 0, even if the byte already
  had a value other than FF

Data storage
============
Data is stored in logical sectors of 512 bytes each. Each logical
sector has a 4 byte "Id".
A logical sector can be in 1 of 3 states:
- Blank
- Valid
- Erasable.
If the sector is blank (all bytes are ff), the Id value is FFFFFFFF.
The sector does not contain any data, but can be used to store data.
If the sector is valid, its Id value contains the logical sector
number and the data area contains valid data.
If the sector is dirty, the sector does not contain any data, but can
not be used to store data without erasing of the physical sector in
which it is contained.

Info sector
===========
The info sector is used when checking integrity of the low level format.
It's format is as follows:
Off  Type Meaning    Explanation
-----------------------------------------
0x00 U32 Signature             0x464c4153
0x04 U32 Version               Rev + (Min << 8) + (Maj << 16)
0x08 U32 NumLogSectors
0x0c U32 NumPhySectors
0x10 U32 BytesPerLogSector
0x14 U32 HasError              (0xFFFFFFFF means Format is o.k, 0xFFFFFFFE means R/O, everything else: no format)

Phy. sector signature
=====================
The signature of the phy. sector is used to indicate if the DataStat field
in the header of all log. sectors stored in a particular phy. sector is valid.
This is required so that the newer versions of the driver can access data stored
to NOR flash by an older version of the driver (that is that does not support the
DataStat flag). The phy. sectors erased using the new version of the driver
use the new signature (PHY_SECTOR_SIGNATURE). Log. sector data written to a phy. sector
with a legacy signature (PHY_SECTOR_SIGNATURE_LEGACY) also sets the DataStat field.
When the data of a log. sector in invalidated in a legacy phy. sector
both the Id and the DataStat fields are set.

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
*       Format version
*/
#define FORMAT_VERSION                        1uL     // Should be incremented if a format change results in an incompatible format
#define VERSION_MAJOR                         FORMAT_VERSION
#define VERSION_MINOR                         0x20uL
#define VERSION_REV                           0x1uL
#define VERSION                               ((VERSION_MAJOR << 16) | (VERSION_MINOR << 8) | VERSION_REV)
#define PHY_SECTOR_SIGNATURE_LEGACY           0x50u   // Signature value used for phy. sectors that store log. sectors with invalid DataStat field.
#define PHY_SECTOR_SIGNATURE                  0x51u   // Signature value used for phy. sectors that store log. sectors with valid DataStat field.

/*********************************************************************
*
*       Storage space reservation
*/
#define PCT_LOG_SECTORS_RESERVED              10      // Number of logical sectors to be reserved (in percents)
#define NUM_PHY_SECTORS_RESERVED              2u      // 1 physical sector is used as work block and the other one is reseved for future improvments.

/*********************************************************************
*
*       Status of data in a physical sector
*/
#define PHY_SECTOR_TYPE_WORK                  0xFFu
#define PHY_SECTOR_TYPE_DATA                  0x02u   // This is the type required to identify a physical sector containing valid data. Everything else is invalid.
#define PHY_SECTOR_TYPE_INVALID               0x00u

/*********************************************************************
*
*       Handling of physical sector sizes
*/
#define SECTOR_SIZE_SHIFT                     8u
#define MAX_SECTOR_SIZE_INDEX                 10      // Defines the max. phys. sector size. 10 -> 512kb, 11 -> 1024kb, ...

/*********************************************************************
*
*       Types of logical sectors
*/
#define LOG_SECTOR_ID_BLANK                   0xFFFFFFFFuL      // Logical sector is blank. It can be used to store data
#define LOG_SECTOR_ID_ERASABLE                0xFFFFFFFEuL      // Logical sector is erasable. The data it contains is obsolete !
#define LOG_SECTOR_ID_INFO                    0xFFFF0000uL      // Logical sector is an info sector

/*********************************************************************
*
*       Special values for "INVALID"
*/
#define ERASE_CNT_INVALID                     0xFFFFFFFFuL
#define PSI_INVALID                           (-1)

/*********************************************************************
*
*       Format information
*/
#define INFO_SECTOR_OFF_SIGNATURE             0x00u
#define INFO_SECTOR_OFF_VERSION               0x04u
#define INFO_SECTOR_OFF_NUM_LOG_SECTORS       0x08u
#define INFO_SECTOR_OFF_BYTES_PER_LOG_SECTOR  0x10u
#define INFO_SECTOR_OFF_HAS_ERROR             0x14u
#define SIGNATURE                             0x464c4153uL

/*********************************************************************
*
*       Error information
*/
#define NOR_ERROR_STATE_OK                    0xFFFFFFFFuL
#define NOR_ERROR_STATE_READONLY              0xFFFFFFFEuL

/*********************************************************************
*
*       Phy. sector erase status
*/
#define ERASE_SIGNATURE_VALID                 0x45525344uL    // "ERSD"
#define ERASE_SIGNATURE_INVALID               0uL

/*********************************************************************
*
*       Status of data in a logical sector
*/
#define DATA_STAT_INVALID                     0xFFu
#define DATA_STAT_VALID                       0xFEu
#define DATA_STAT_ERASABLE                    0x00u

/*********************************************************************
*
*       Utility macros
*/
//lint -emacro(413, OFFSET_OF_MEMBER, SIZE_OF_MEMBER) Likely use of null pointer. Rationale: This is the only way the offset or the size of the structure member can be calculated.
#if (FS_NOR_SUPPORT_VARIABLE_LINE_SIZE != 0) || (FS_NOR_OPTIMIZE_HEADER_WRITE != 0)
  #define OFFSET_OF_MEMBER(Type, Member)      (unsigned)SEGGER_PTR2ADDR(&(((Type*)0)->Member))
  #define ALIGN_TO_BOUNDARY(Value, Boundary)  (((Value) + (Boundary) - 1u) & ~((Boundary) - 1u))
#endif // FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  #define SIZE_OF_MEMBER(Type, Member)        (sizeof(((Type *)0)->Member))
#endif

/*********************************************************************
*
*       Statistical counters
*/
#if FS_NOR_ENABLE_STATS
  #define IF_STATS(Exp) Exp
#else
  #define IF_STATS(Exp)
#endif

/*********************************************************************
*
*       ASSERT_UNIT_NO_IS_IN_RANGE
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)                                \
    if ((Unit) >= (U8)FS_NOR_NUM_UNITS) {                                 \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR: Invalid unit number.")); \
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);                                \
    }
#else
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)
#endif

/*********************************************************************
*
*       ASSERT_PHY_TYPE_IS_SET
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_PHY_TYPE_IS_SET(pInst)                                         \
    if ((pInst)->pPhyType == NULL) {                                            \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR: Physical layer is not set.")); \
      FS_X_PANIC(FS_ERRCODE_UNKNOWN_DEVICE);                                    \
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
  #define CALC_PSH_DATA_RANGE(pInst, ppData, pOff, pNumBytes)   _CalcDataRange(pInst, &_pshDataRange, ppData, pOff, pNumBytes)
#else
  #define CALC_PSH_DATA_RANGE(pInst, ppData, pOff, pNumBytes)
#endif

/*********************************************************************
*
*       SAVE_PSH_DATA_RANGE
*/
#if FS_NOR_OPTIMIZE_HEADER_WRITE
  #define SAVE_PSH_DATA_RANGE(pDataRange)                       _CopyDataRange(&_pshDataRange, pDataRange)
#else
  #define SAVE_PSH_DATA_RANGE(pDataRange)
#endif

/*********************************************************************
*
*       RESTORE_PSH_DATA_RANGE
*/
#if FS_NOR_OPTIMIZE_HEADER_WRITE
  #define RESTORE_PSH_DATA_RANGE(pDataRange)                    _CopyDataRange(pDataRange, &_pshDataRange)
#else
  #define RESTORE_PSH_DATA_RANGE(pDataRange)
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
  #define INIT_VERIFY(pData, Off, NumBytes)                     _InitVerify(pData, Off, NumBytes)
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
*       Invokes the test hook function if the support for testing is enabled.
*/
#if FS_SUPPORT_TEST
  #define CALL_TEST_HOOK_FAIL_SAFE(Unit) _CallTestHookFailSafe(Unit)
#else
  #define CALL_TEST_HOOK_FAIL_SAFE(Unit)
#endif

/*********************************************************************
*
*       FS_NOR_TEST_HOOK_DATA_READ_BEGIN
*/
#if FS_SUPPORT_TEST
  #define CALL_TEST_HOOK_DATA_READ_BEGIN(Unit, pData, pOff, pNumBytes) _CallTestHookDataReadBegin(Unit, pData, pOff, pNumBytes)
#else
  #define CALL_TEST_HOOK_DATA_READ_BEGIN(Unit, pData, pOff, pNumBytes)
#endif

/*********************************************************************
*
*       FS_NOR_TEST_HOOK_DATA_READ_END
*/
#if FS_SUPPORT_TEST
  #define CALL_TEST_HOOK_DATA_READ_END(Unit, pData, Off, NumBytes, pResult) _CallTestHookDataReadEnd(Unit, pData, Off, NumBytes, pResult)
#else
  #define CALL_TEST_HOOK_DATA_READ_END(Unit, pData, Off, NumBytes, pResult)
#endif

/*********************************************************************
*
*       FS_NOR_TEST_HOOK_DATA_WRITE_BEGIN
*/
#if FS_SUPPORT_TEST
  #define CALL_TEST_HOOK_DATA_WRITE_BEGIN(Unit, pData, pOff, pNumBytes) _CallTestHookDataWriteBegin(Unit, pData, pOff, pNumBytes)
#else
  #define CALL_TEST_HOOK_DATA_WRITE_BEGIN(Unit, pData, pOff, pNumBytes)
#endif

/*********************************************************************
*
*       FS_NOR_TEST_HOOK_DATA_WRITE_END
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
*       Conversion of logical sector indices to physical sector indices
*
*  The 2 macros convert a physical into a logical sector index and vice versa.
*  This is required since logical sector indices start at 0, but 0 is a reserved
*  value since it is also used to mark the sector as invalid, so the
*  physical and logical values have to be different.
*/
#define LSI2PSI(lsi) ((lsi) + 0x100000UL)
#define PSI2LSI(psi) ((psi) - 0x100000UL)

/*********************************************************************
*
*       Local types
*
**********************************************************************
*/

#if FS_NOR_OPTIMIZE_HEADER_WRITE

/*********************************************************************
*
*       NOR_DATA_RANGE
*
*  Notes
*    (1) We assume that the header of a logical or physical sector is
*        never larger than 65536 bytes.
*/
typedef struct {
  U16 OffStart;   // Note 1
  U16 OffEnd;     // Note 1
} NOR_DATA_RANGE;

#endif // FS_NOR_OPTIMIZE_HEADER_WRITE

//lint -esym(754, NOR_LSH::abReserved, NOR_LSH::abPadding1, NOR_LSH::abPadding2, NOR_PSH::Type) local structure member not referenced. Rationale: these members are used only for padding of other members to a required byte boundary.

/*********************************************************************
*
*       NOR_LSH
*
*  This header is placed in front of every logical sector. With most
*  external flashes, this header consists of only 8 bytes indicating
*  the sector number of the data,
*  0xFFFFFFFF for blank or 0 for invalid (obsolete) data.
*  For flashes with bigger flash lines and / or flashes
*  which can not be rewritten without erase, the header is bigger,
*  but still stores the same information.
*
*  Notes
*    (1) After adding new members make sure that the functions
*        that take a member offset to calculate the number of bytes
*        used in a flash line are updated accordingly.
*/
typedef struct {
  U32 Id;
  U8  DataStat;
  U8  abReserved[3];
#if (FS_NOR_LINE_SIZE > 8)
  U8  abPadding1[FS_NOR_LINE_SIZE - 8];     // Pad to line size
#endif
#if (FS_NOR_CAN_REWRITE == 0)
  U8  IsErasable;
#if (FS_NOR_LINE_SIZE > 1)
  U8  abPadding2[FS_NOR_LINE_SIZE - 1];     // Pad to line size
#endif
#endif
} NOR_LSH;

//lint -esym(754, NOR_PSH::abReserved, NOR_PSH::abPadding1, NOR_PSH::abPadding2, NOR_PSH::abPadding3, NOR_PSH::FailSafeErase) local structure member not referenced. Rationale: these members are used only for padding of other members to a required byte boundary.

/*********************************************************************
*
*       NOR_PSH
*
*  Notes
*    (1) After adding new members make sure that the functions
*        that take a member offset to calculate the number of bytes
*        used in a flash line are updated accordingly.
*/
typedef struct {
  U8  Signature;            // The signature is used to check if the DataStat field in the headers of log. sector is valid.
  U8  FormatVersion;
  U8  FailSafeErase;        // Indicates if the phy. sector should be marked as successfully erased.
  U8  Type;
  U32 EraseCnt;
  U32 EraseSignature;
  U8  abReserved[4];
#if (FS_NOR_LINE_SIZE > 16)
  U8  abPadding1[FS_NOR_LINE_SIZE - 16];
#endif
#if (FS_NOR_CAN_REWRITE == 0)
  U8  IsWork;
#if (FS_NOR_LINE_SIZE > 1)
  U8  abPadding2[FS_NOR_LINE_SIZE - 1];
#endif
  U8  IsValid;
#if (FS_NOR_LINE_SIZE > 1)
  U8  abPadding3[FS_NOR_LINE_SIZE - 1];
#endif
#endif
} NOR_PSH;

/*********************************************************************
*
*       FREE_SECTOR_CACHE
*/
typedef struct {
  U8  SkipFill;
  U32 RdPos;
  U32 Cnt;
  U32 aData[FS_NOR_NUM_FREE_SECTORCACHE];                     // Contains offsets of free sectors.
} FREE_SECTOR_CACHE;

/*********************************************************************
*
*       NOR_STATUS
*/
typedef struct {
  I32               aWorkIndex[MAX_SECTOR_SIZE_INDEX + 1];    // Indexes of the physical sectors that can be used as work block (one for each sector size supported by the NOR flash device).
  U32               WLSectorSize;                             // Sector size for which wear leveling needs to be done
  U32               OffInfoSector;                            // Offset of info sector. Used as temp during LL-mount.
  FREE_SECTOR_CACHE FreeSectorCache;                          // Information about which logical sectors are free.
  int               psiLastCleared;                           // Index of the last physical sector that has been cleaned.
  U8                HasError;                                 // Set to 1 if the driver encountered a permanent error.
  U8                LLMountFailed;                            // Set to 1 if the low-level mount operation failed.
  U8                IsLLMounted;                              // Set to 1 if the NOR flash device has been successfully mounted.
  U8                FailSafeErase;                            // Set to 1 if the physical sectors have to be erased using the fail-safe procedure.
  U8                LegacyPhySectorsFound;                    // Set to 1 during the low-level mount operation if data sectors are found with the signature set to PHY_SECTOR_SIGNATURE_LEGACY.
} NOR_STATUS;

/*********************************************************************
*
*       NOR_INST
*
*  Description
*    This is the central data structure for the entire driver.
*    It contains data items of one instance of the driver.
*/
typedef struct {
  NOR_STATUS                Status;                                           // Operating status.
  U8                      * pL2P;                                             // Look-up table for logical to physical translation
  const FS_NOR_PHY_TYPE   * pPhyType;                                         // Physical layer used to access the NOR flash device.
#if FS_NOR_OPTIMIZE_DIRTY_CHECK
  U8                      * pDirtyMap;                                        // Pointer to an array of bits where each bit represents one physical sector. 1: Empty logical sectors have to be checked, 0: Empty logical sectors are known to be blank.
#endif
  U32                       NumLogSectors;                                    // Number of logical sectors (Computed from number and size of physical sectors)
  U32                       NumPhySectors;                                    // Total number of physical sectors.
  U32                       aNumPhySectorsPerSize[MAX_SECTOR_SIZE_INDEX + 1]; // Number of physical sectors in a range.
  U32                       NumBitsUsed;                                      // Size of an entry in the logical to physical mapping table in bits.
#if FS_NOR_ENABLE_STATS
  FS_NOR_STAT_COUNTERS      StatCounters;                                     // Statistical counters.
#endif
#if FS_NOR_SUPPORT_COMPATIBILITY_MODE
  U32                       OffLogSectorInvalid;
  U32                       LogSectorIndexInvalid;
  U32                       NumLogSectorsInvalid;
#endif
  U16                       SectorSize;                                       // Size of the logical sector in bytes.
  U8                        Unit;                                             // Index of the driver.
  U8                        IsInited;                                         // Set to 1 if the driver was successfully initialized. Set to 0 when the NOR flash is unmounted.
  U8                        pctLogSectorsReserved;                            // Percentage of all logical sectors to be reserved.
#if FS_NOR_VERIFY_ERASE
  U8                        VerifyErase;                                      // If set to 1 after the erase operation the driver checks if all bytes in the physical sector are set to 0xFF.
#endif
#if FS_NOR_VERIFY_WRITE
  U8                        VerifyWrite;                                      // If set to 1 after the write operation the driver reads back and compares the data.
#endif
#if FS_NOR_SKIP_BLANK_SECTORS
  U8                        SkipBlankSectors;                                 // If set to 1 the low-level format operation will not erase physical sectors which are already blank.
#endif
#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  U8                        ldBytesPerLine;                                   // Indicates the number of bytes in a block that can be written only once (power of 2).
  U8                        IsRewriteSupported;                               // If set to 1 the same data can be rewritten as long as 0 bits are preserved.
  U8                        SizeOfLSH;                                        // Number of bytes in the header of a logical sector.
  U8                        SizeOfPSH;                                        // Number of bytes in the header of a physical sector.
#endif
#if FS_NOR_SUPPORT_LEGACY_MODE
  U8                        IsLegacyModeSupported;                            // If set to 1 the driver works in legacy mode.
#endif
#if FS_NOR_OPTIMIZE_DIRTY_CHECK
  U8                        IsDirtyCheckOptimized;                            // Set to 1 if the dirty check optimization is enabled.
#endif
#if FS_NOR_SUPPORT_CLEAN
  U8                        IsClean;                                          // Set to 1 if all the invalid physical sectors are erased.
#endif // FS_NOR_SUPPORT_CLEAN
} NOR_INST;

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static U8                                    _NumUnits = 0;
static NOR_INST                            * _apInst[FS_NOR_NUM_UNITS];
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_PARA)
  static U32                                 _FlashStart;
  static U32                                 _FlashEnd;
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
  static NOR_DATA_RANGE                      _pshDataRange;
  static NOR_DATA_RANGE                      _lshDataRange;
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

//lint -efunc(818, _WriteOff) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory]. Rationale: the statistical counters stored in the driver instance are updated in debug builds.
//lint -efunc(818, _ReadOff) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory]. Rationale: the statistical counters stored in the driver instance are updated in debug builds.
//lint -efunc(818, _CopySectorData) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory]. Rationale: the statistical counters stored in the driver instance are updated in debug builds.
//lint -efunc(818, _ErasePhySector) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory]. Rationale: the statistical counters stored in the driver instance are updated in debug builds.

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

#if FS_NOR_OPTIMIZE_DIRTY_CHECK

/*********************************************************************
*
*       _SizeOfDirtyMap
*/
static U32 _SizeOfDirtyMap(const NOR_INST * pInst) {
  U32 NumPhySectors;
  U32 NumBytes;

  NumPhySectors = pInst->NumPhySectors;
  NumBytes = (NumPhySectors + 7uL) / 8uL;
  return NumBytes;
}

/*********************************************************************
*
*       _MarkPhySectorAsDirty
*
*  Function description
*    Set that the logical sectors stored in the phy. sector
*    have to be checked for blank.
*/
static void _MarkPhySectorAsDirty(const NOR_INST * pInst, unsigned PhySectorIndex) {
  unsigned   Mask;
  U8       * pData;
  unsigned   Data;

  if (pInst->IsDirtyCheckOptimized != 0u) {
    if (pInst->pDirtyMap != NULL) {
      Mask    = 1uL << (PhySectorIndex & 7u);
      pData   = pInst->pDirtyMap + (PhySectorIndex >> 3);
      Data    = *pData;
      Data   |= Mask;
      *pData  = (U8)Data;
    }
  }
}

/*********************************************************************
*
*       _MarkPhySectorAsClean
*
*  Function description
*    Set that the logical sectors stored in the phy. sector
*    do not have to be checked for blank.
*/
static void _MarkPhySectorAsClean(const NOR_INST * pInst, unsigned PhySectorIndex) {
  unsigned   Mask;
  U8       * pData;
  unsigned   Data;

  if (pInst->IsDirtyCheckOptimized != 0u) {
    if (pInst->pDirtyMap != NULL) {
      Mask    = 1uL << (PhySectorIndex & 7u);
      pData   = pInst->pDirtyMap + (PhySectorIndex >> 3);
      Data    = *pData;
      Data   &= ~Mask;
      *pData  = (U8)Data;
    }
  }
}

/*********************************************************************
*
*       _IsPhySectorDirty
*
*  Function description
*    Returns if the empty logical blocks have to be checked for blank.
*/
static int _IsPhySectorDirty(const NOR_INST * pInst, unsigned PhySectorIndex) {
  unsigned   Mask;
  U8       * pData;
  unsigned   Data;
  int        r;

  r = 1;          // Set to indicate that the phy. sector is dirty.
  if (pInst->IsDirtyCheckOptimized != 0u) {
    if (pInst->pDirtyMap != NULL) {
      Mask = 1uL << (PhySectorIndex & 7u);
      pData = pInst->pDirtyMap + (PhySectorIndex >> 3);
      Data  = *pData;
      if ((Data & Mask) == 0u) {
        r = 0;
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _MarkAllPhySectorsAsClean
*
*  Function description
*    Set that the logical sectors stored in all phy. sectors
*    do not have to be checked for blank.
*/
static void _MarkAllPhySectorsAsClean(const NOR_INST * pInst) {
  U32 NumBytes;

  if (pInst->IsDirtyCheckOptimized != 0u) {
    if (pInst->pDirtyMap != NULL) {
      NumBytes = _SizeOfDirtyMap(pInst);
      FS_MEMSET(pInst->pDirtyMap, 0, NumBytes);
    }
  }
}

/*********************************************************************
*
*       _IsAnyPhySectorDirty
*
*  Function description
*    Checks if there is at least one physical sector that is marked as dirty.
*/
static int _IsAnyPhySectorDirty(const NOR_INST * pInst) {
  int   r;
  U32   NumBytes;
  U8  * p;

  r = 1;              // Assume that not all the phy. sectors are clean.
  if (pInst->IsDirtyCheckOptimized != 0u) {
    if (pInst->pDirtyMap != NULL) {
      r = 0;
      NumBytes = _SizeOfDirtyMap(pInst);
      p = SEGGER_PTR2PTR(U8, pInst->pDirtyMap);
      do {
        if (*p++ != 0u) {
          r = 1;      // Found at least one phy. sector that is not clean.
          break;
        }
      } while (--NumBytes != 0u);
    }
  }
  return r;
}

#endif // FS_NOR_OPTIMIZE_DIRTY_CHECK

#if (FS_NOR_SUPPORT_VARIABLE_LINE_SIZE != 0) || (FS_NOR_LINE_SIZE > 1)

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

#endif // (FS_NOR_SUPPORT_VARIABLE_LINE_SIZE != 0) || (FS_NOR_LINE_SIZE > 1)

#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE

/*********************************************************************
*
*       _CalcUpdateSizeOfLSH
*
*  Function description
*    Calculates and updates to driver instance the size of the logical header.
*/
static void _CalcUpdateSizeOfLSH(NOR_INST * pInst) {
  unsigned NumBytes;
  unsigned BytesPerLine;

  NumBytes = sizeof(NOR_LSH);
  BytesPerLine = 1uL << pInst->ldBytesPerLine;
  if (BytesPerLine < (unsigned)FS_NOR_LINE_SIZE) {
    NumBytes = 0u
              + OFFSET_OF_MEMBER(NOR_LSH, abReserved[0])        // Last member
              + SIZE_OF_MEMBER(NOR_LSH, abReserved[0])
              - OFFSET_OF_MEMBER(NOR_LSH, Id);                  // First member
    NumBytes = ALIGN_TO_BOUNDARY(NumBytes, BytesPerLine);       // Align to flash line boundary.
#if (FS_NOR_CAN_REWRITE == 0)
    if (pInst->IsRewriteSupported == 0u) {
      NumBytes += 0u
                + OFFSET_OF_MEMBER(NOR_LSH, IsErasable)         // Last member
                + SIZE_OF_MEMBER(NOR_LSH, IsErasable)
                - OFFSET_OF_MEMBER(NOR_LSH, IsErasable);        // First member
      NumBytes = ALIGN_TO_BOUNDARY(NumBytes, BytesPerLine);     // Align to flash line boundary.
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
static void _CalcUpdateSizeOfPSH(NOR_INST * pInst) {
  unsigned NumBytes;
  unsigned BytesPerLine;

  NumBytes = sizeof(NOR_PSH);
  BytesPerLine = 1uL << pInst->ldBytesPerLine;
  if (BytesPerLine < (unsigned)FS_NOR_LINE_SIZE) {
    NumBytes = 0u
              + OFFSET_OF_MEMBER(NOR_PSH, abReserved[0])       // Last member
              + SIZE_OF_MEMBER(NOR_PSH, abReserved[0])
              - OFFSET_OF_MEMBER(NOR_PSH, Signature);           // First member
    NumBytes = ALIGN_TO_BOUNDARY(NumBytes, BytesPerLine);       // Align to flash line boundary.
#if (FS_NOR_CAN_REWRITE == 0)
    if (pInst->IsRewriteSupported == 0u) {
      NumBytes += 0u
                + OFFSET_OF_MEMBER(NOR_PSH, IsWork)             // Last member
                + SIZE_OF_MEMBER(NOR_PSH, IsWork)
                - OFFSET_OF_MEMBER(NOR_PSH, IsWork);            // First member
      NumBytes = ALIGN_TO_BOUNDARY(NumBytes, BytesPerLine);     // Align to flash line boundary.
      NumBytes += 0u
                + OFFSET_OF_MEMBER(NOR_PSH, IsValid)            // Last member
                + SIZE_OF_MEMBER(NOR_PSH, IsValid)
                - OFFSET_OF_MEMBER(NOR_PSH, IsValid);           // First member
      NumBytes = ALIGN_TO_BOUNDARY(NumBytes, BytesPerLine);     // Align to flash line boundary.
    }
#endif
  }
  pInst->SizeOfPSH = (U8)NumBytes;
}

#endif // FS_NOR_SUPPORT_VARIABLE_LINE_SIZE

#if (FS_NOR_SUPPORT_FAIL_SAFE_ERASE != 0) || (FS_NOR_CAN_REWRITE == 0)

/*********************************************************************
*
*       _IsRewriteSupported
*/
static int _IsRewriteSupported(const NOR_INST * pInst) {
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

#endif // (FS_NOR_SUPPORT_FAIL_SAFE_ERASE != 0) || (FS_NOR_CAN_REWRITE == 0)

#if FS_NOR_OPTIMIZE_HEADER_WRITE

/*********************************************************************
*
*       _InitDataRange
*/
static void _InitDataRange(NOR_DATA_RANGE * pDataRange) {
  pDataRange->OffStart = (U16)~0u;
  pDataRange->OffEnd   = 0;
}

/*********************************************************************
*
*       _UpdateDataRange
*
*  Additional information
*    NumBytes has to be larger than 0.
*/
static void _UpdateDataRange(NOR_DATA_RANGE * pDataRange, unsigned Off, unsigned NumBytes) {
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
static void _CalcDataRange(const NOR_INST * pInst, const NOR_DATA_RANGE * pDataRange, const U8 ** ppData, U32 * pOff, unsigned * pNumBytes) {
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

/*********************************************************************
*
*       _CopyDataRange
*/
static void _CopyDataRange(const NOR_DATA_RANGE * pDataRangeSrc, NOR_DATA_RANGE * pDataRangeDest) {
  *pDataRangeDest = *pDataRangeSrc;       // Struct copy.
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
*       _SetError
*
*  Function description
*    Sets Error flag. If the error flag is set, write operations are
*    no longer permitted and are ignored.
*/
static void _SetError(NOR_INST * pInst) {
  pInst->Status.HasError = 1;
}

/*********************************************************************
*
*       _ReadOff
*
*  Function description
*    Reads data from the NOR flash device.
*
*  Parameters
*    pInst      Driver instance.
*    Off        Byte offset to read from.
*    pData      Data read from NOR flash device.
*    NumBytes   Number of bytes to read.
*
*  Return value
*    ==0      OK, data read successfully.
*    !=0      An error occurred.
*/
static int _ReadOff(NOR_INST * pInst, U32 Off, void * pData, U32 NumBytes) {
  int r;
  U8  Unit;

#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_PARA)
  if ((Off < _FlashStart) || (Off > _FlashEnd) || ((Off + NumBytes) > _FlashEnd)) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR: _ReadOff: Out-of-bounds access Off: 0x%08x, NumBytes: %lu.", Off, NumBytes));
    return 1;
  }
#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_PARA
  Unit = pInst->Unit;
  CALL_TEST_HOOK_DATA_READ_BEGIN(Unit, pData, &Off, &NumBytes);
  r = pInst->pPhyType->pfReadOff(Unit, pData, Off, NumBytes);
  CALL_TEST_HOOK_DATA_READ_END(Unit, pData, Off, NumBytes, &r);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR: _ReadOff: Read failed Off: 0x%08x, r: %d.", Off, r));
  }
  IF_STATS(pInst->StatCounters.ReadCnt++);
  IF_STATS(pInst->StatCounters.ReadByteCnt += NumBytes);
  return r;
}

/*********************************************************************
*
*       _SectorSize2ShiftCnt
*
*  Function description
*    Converts the size of a physical sector into a shift count.
*/
static int _SectorSize2ShiftCnt(U32 SectorSize) {
  int i;

  SectorSize >>= SECTOR_SIZE_SHIFT;
  for (i = 0; i <= MAX_SECTOR_SIZE_INDEX; i++) {
    if (SectorSize == 1u) {
      return i;
    }
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL)
    if ((SectorSize & 1u) != 0u) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR: _SectorSize2ShiftCnt: Invalid sector size (Not a power of 2)."));
      FS_X_PANIC(FS_ERRCODE_VERIFY_FAILURE);
    }
#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_PARA
    SectorSize >>= 1;
  }
  FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR: _SectorSize2ShiftCnt: Unsupported sector size."));
  return -1;     // Error, unsupported sector size.
}

/*********************************************************************
*
*       _SectorShiftCnt2Size
*
*  Function description
*    Converts the shift count of a physical sector into its size.
*/
static U32 _SectorShiftCnt2Size(unsigned SectorSizeIndex) {
  return 1uL << (SECTOR_SIZE_SHIFT + SectorSizeIndex);
}

/*********************************************************************
*
*       _GetSectorInfo
*
*  Function description
*    Returns the size & offset of a physical sector in bytes by querying the physical layer.
*/
static void _GetSectorInfo(const NOR_INST * pInst, unsigned int PhySectorIndex, U32 * pOff, U32 * pSize) {
   pInst->pPhyType->pfGetSectorInfo(pInst->Unit, PhySectorIndex, pOff, pSize);
}

/*********************************************************************
*
*       _WriteL2PEntry
*
*  Function description
*    Writes the physical offset for sector into the look-up table.
*
*  Parameters
*    pInst            Driver instance.
*    LogSectorIndex   The index of the logical sector.
*                     The first sector has Index 0 !
*    Off              Byte offset of the sector.
*
*  Return value
*    The former offset is returned.
*/
static U32 _WriteL2PEntry(const NOR_INST * pInst, U32 LogSectorIndex, U32 Off) {
  U32 r;

  //
  // Higher debug levels: Check if an other logical index is using the same physical offset, which would be
  // a fatal error.
  //
#if FS_SUPPORT_TEST
  if (Off != 0u) {
    U32 i;
    U32 L2PEntry;

    for (i = 0; i < pInst->NumLogSectors; i++) {
      if (i != LogSectorIndex) {
        L2PEntry  = FS_BITFIELD_ReadEntry(pInst->pL2P, i, pInst->NumBitsUsed);
        if (L2PEntry == Off) {
          FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR: _WriteL2PEntry: Physical data area identified by \"Off\" is cross-linked to 2 or more logical sectors."));
        }
      }
    }
  }
#endif
  r = FS_BITFIELD_ReadEntry(pInst->pL2P, LogSectorIndex, pInst->NumBitsUsed);
  FS_BITFIELD_WriteEntry(pInst->pL2P, LogSectorIndex, pInst->NumBitsUsed, Off);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR: WRITE_L2P_ENTRY LSI: %d, NewOff: 0x%8x, PrevOff: 0x%8x\n", LogSectorIndex, Off, r));
  return r;
}

#if FS_NOR_VERIFY_WRITE

/*********************************************************************
*
*        _VerifyWrite
*
*  Function description
*    Verifies if the data has been written correctly to NOR flash device.
*/
static int _VerifyWrite(NOR_INST * pInst, const void * pData, U32 Off, U32 NumBytes) {
  U8  Data;
  U8  DataSrc;
  U32 i;
  int r;

  r = 0;
  for (i = 0; i < NumBytes; i++) {
    r = _ReadOff(pInst, Off + i, &Data, 1);
    if (r != 0) {
      r = 1;                        // Error, cannot read data.
      break;
    }
    DataSrc = *(SEGGER_CONSTPTR2PTR(const U8, pData) + i);
    if (Data != DataSrc) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR: _VerifyWrite: Data mismatch Off: 0x%08x, DataWr: 0x%x, DataRd: 0x%x.", Off + i, DataSrc, Data));
      r = 1;                        // Error, data does not match.
      break;
    }
  }
  return r;
}

#endif // FS_NOR_VERIFY_WRITE

/*********************************************************************
*
*        _WriteOff
*
*  Function description
*    Writes data to NOR flash.
*
*  Parameters
*    pInst      Driver instance.
*    Off        Bytes offset to write to.
*    pData      Data to be written.
*    NumBytes   Number of bytes to be written.
*
*  Return value
*    ==0      OK, data written successfully.
*    !=0      An error occurred.
*/
static int _WriteOff(NOR_INST * pInst, U32 Off, const void * pData, U32 NumBytes) {
  int r;
  U8  Unit;

  Unit = pInst->Unit;
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_PARA
  if ((Off < _FlashStart) || (Off > _FlashEnd) || ((Off + NumBytes) > _FlashEnd)) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR: _WriteOff: Out-of-bounds access Off: 0x%08x, NumBytes: %lu.", Off, NumBytes));
    return 1;
  }
#endif
  CALL_TEST_HOOK_DATA_WRITE_BEGIN(Unit, &pData, &Off, &NumBytes);
  r =  pInst->pPhyType->pfWriteOff(Unit, Off, pData, NumBytes);
  CALL_TEST_HOOK_DATA_WRITE_END(Unit, pData, Off, NumBytes, &r);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR: _WriteOff: Write failed Off: 0x%08x, r: %d.", Off, r));
  }
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

/*********************************************************************
*
*       _ReadLogSectorData
*/
static int _ReadLogSectorData(NOR_INST * pInst, U32 Off, void * p, U32 NumBytes) {
  int r;

  r = _ReadOff(pInst, Off, p, NumBytes);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR: READ_LOG_SECTOR Off: 0x%8x, NumBytes: %lu, r: %d\n", Off, NumBytes, r));
  return r;
}

/*********************************************************************
*
*       _WriteLogSectorData
*/
static int _WriteLogSectorData(NOR_INST * pInst, U32 Off, const void * p, U32 NumBytes) {
  int r;

  r = _WriteOff(pInst, Off, p, NumBytes);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR: WRITE_LOG_SECTOR Off: 0x%8x, NumBytes: %lu, r: %d\n", Off, NumBytes, r));
  return r;
}

/*********************************************************************
*
*        _SizeOfLSH
*/
static unsigned _SizeOfLSH(const NOR_INST * pInst) {
  unsigned NumBytes;

#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  NumBytes = pInst->SizeOfLSH;
#else
  FS_USE_PARA(pInst);
  NumBytes = sizeof(NOR_LSH);
#endif
  return NumBytes;
}

/*********************************************************************
*
*        _WriteLogSectorHeader
*/
static int _WriteLogSectorHeader(NOR_INST * pInst, U32 Off, const NOR_LSH * pLSH) {
  int        r;
  unsigned   NumBytes;
  const U8 * pData;
#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  U8         abData[sizeof(NOR_LSH)];
  unsigned   NumBytesToCopy;
  U32        BytesPerLine;
#endif // FS_NOR_SUPPORT_VARIABLE_LINE_SIZE

  NumBytes = _SizeOfLSH(pInst);
  pData    = SEGGER_CONSTPTR2PTR(const U8, pLSH);
#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  BytesPerLine = 1uL << pInst->ldBytesPerLine;
  if (BytesPerLine < (unsigned)FS_NOR_LINE_SIZE) {
    FS_MEMSET(abData, 0xFF, sizeof(abData));
    pData    = abData;
    NumBytes = 0;
    NumBytesToCopy = 0u
                   + OFFSET_OF_MEMBER(NOR_LSH, abReserved[0])   // Last member
                   + SIZE_OF_MEMBER(NOR_LSH, abReserved[0])
                   - OFFSET_OF_MEMBER(NOR_LSH, Id);             // First member
    FS_MEMCPY(&abData[NumBytes], &pLSH->Id, NumBytesToCopy);
    NumBytes += NumBytesToCopy;
    NumBytes  = ALIGN_TO_BOUNDARY(NumBytes, BytesPerLine);      // Align to flash line boundary.
#if (FS_NOR_CAN_REWRITE == 0)
    if (pInst->IsRewriteSupported == 0u) {
      NumBytesToCopy = 0u
                     + OFFSET_OF_MEMBER(NOR_LSH, IsErasable)
                     + SIZE_OF_MEMBER(NOR_LSH, IsErasable)
                     - OFFSET_OF_MEMBER(NOR_LSH, IsErasable);
      FS_MEMCPY(&abData[NumBytes], &pLSH->IsErasable, NumBytesToCopy);
      NumBytes += NumBytesToCopy;
      NumBytes  = ALIGN_TO_BOUNDARY(NumBytes, BytesPerLine);    // Align to flash line boundary.
    }
#endif // FS_NOR_CAN_REWRITE == 0
  }
#endif // FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  INIT_VERIFY(pData, Off, NumBytes);
  CALC_LSH_DATA_RANGE(pInst, &pData, &Off, &NumBytes);
  r = _WriteOff(pInst, Off, pData, NumBytes);
  VERIFY_WRITE(pInst);
  INIT_LSH_DATA_RANGE();
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR: WRITE_LSH Off: 0x%8x, Id: 0x%8x, NumBytes: %d, r: %d\n", Off, pLSH->Id, NumBytes, r));
  return r;
}

/*********************************************************************
*
*        _ReadLogSectorHeader
*/
static int _ReadLogSectorHeader(NOR_INST * pInst, U32 Off, NOR_LSH * pLSH) {
  int   r;
  U32   NumBytes;
  U8  * pData;
#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  U8    abData[sizeof(NOR_LSH)];
  U32   NumBytesToCopy;
  U32   BytesPerLine;
#endif // FS_NOR_SUPPORT_VARIABLE_LINE_SIZE

  NumBytes     = _SizeOfLSH(pInst);
  pData        = SEGGER_PTR2PTR(U8, pLSH);
#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  BytesPerLine = 1uL << pInst->ldBytesPerLine;
  FS_MEMSET(abData, 0xFF, sizeof(abData));
  if (BytesPerLine < (unsigned)FS_NOR_LINE_SIZE) {
    pData = abData;
  }
#endif
  r = _ReadOff(pInst, Off, pData, NumBytes);
#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  if (BytesPerLine < (unsigned)FS_NOR_LINE_SIZE) {
    FS_MEMSET(pLSH, 0xFF, sizeof(NOR_LSH));
    NumBytes       = 0;
    NumBytesToCopy = 0u
                   + OFFSET_OF_MEMBER(NOR_LSH, abReserved[0])   // Last member
                   + SIZE_OF_MEMBER(NOR_LSH, abReserved[0])
                   - OFFSET_OF_MEMBER(NOR_LSH, Id);             // First member
    FS_MEMCPY(&pLSH->Id, &abData[NumBytes], NumBytesToCopy);
#if (FS_NOR_CAN_REWRITE == 0)
    NumBytes += NumBytesToCopy;
    NumBytes  = ALIGN_TO_BOUNDARY(NumBytes, BytesPerLine);      // Align to flash line boundary.
    if (pInst->IsRewriteSupported == 0u) {
      NumBytesToCopy = 0u
                     + OFFSET_OF_MEMBER(NOR_LSH, IsErasable)    // Last member
                     + SIZE_OF_MEMBER(NOR_LSH, IsErasable)
                     - OFFSET_OF_MEMBER(NOR_LSH, IsErasable);   // First member
      FS_MEMCPY(&pLSH->IsErasable, &abData[NumBytes], NumBytesToCopy);
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ALL)
      NumBytes += NumBytesToCopy;
      NumBytes  = ALIGN_TO_BOUNDARY(NumBytes, BytesPerLine);    // Align to flash line boundary.
#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ALL
    }
#endif // FS_NOR_CAN_REWRITE == 0
  }
#endif // FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR: READ_LSH Off: 0x%8x, Id: 0x%8x, NumBytes: %d, r: %d\n", Off, pLSH->Id, NumBytes, r));
  return r;
}

/*********************************************************************
*
*        _WriteLogSectorInfo
*
*  Function description
*    Writes the initial sector index into the logical header
*/
static int _WriteLogSectorInfo(NOR_INST * pInst, U32 Off, U32 LogSectorId) {
  NOR_LSH lsh;
  int     r;

  FS_MEMSET(&lsh, 0xFF, sizeof(lsh));
  if (LogSectorId < pInst->NumLogSectors) {
    lsh.Id = LSI2PSI(LogSectorId);
  } else {
    lsh.Id = LogSectorId;      // Handle special cases such as the info sector
  }
  UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_LSH, Id), sizeof(lsh.Id));
  lsh.DataStat = DATA_STAT_VALID;
  UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_LSH, DataStat), sizeof(lsh.DataStat));
  r = _WriteLogSectorHeader(pInst, Off, &lsh);
  return r;
}

/*********************************************************************
*
*        _CopySectorData
*
*  Function description
*    Copies the data part of a sector. The logical sector header is
*    not copied.
*
*  Notes
*    (1) The sector is copied in multiple chunks, where a small
*        array on the stack is used as buffer. The reason why the data
*        is copied in multiple chunks is simply to keep the stack load low.
*/
static int _CopySectorData(NOR_INST * pInst, U32 DestAddr, U32 SrcAddr) {
  U32      acBuffer[32];
  int      r;
  unsigned NumBytesAtOnce;
  unsigned NumBytes;
  U32      DestAddrToCopy;
  U32      SrcAddrToCopy;
  int      Result;

  r              = 0;
  NumBytes       = pInst->SectorSize;
  DestAddrToCopy = DestAddr;
  SrcAddrToCopy  = SrcAddr;
  do {
    NumBytesAtOnce = SEGGER_MIN(NumBytes, sizeof(acBuffer));
    Result = _ReadOff (pInst, SrcAddrToCopy, acBuffer, NumBytesAtOnce);
    if (Result == 0) {
      Result = _WriteOff(pInst, DestAddrToCopy, acBuffer, NumBytesAtOnce);
    }
    if ((r == 0) && (Result != 0)) {
      r = Result;
    }
    NumBytes       -= NumBytesAtOnce;
    SrcAddrToCopy  += NumBytesAtOnce;
    DestAddrToCopy += NumBytesAtOnce;
  } while (NumBytes != 0u);
  IF_STATS(++pInst->StatCounters.CopySectorCnt);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR: COPY_LOG_SECTOR SrcOff: 0x%8x, DestOff: 0x%8x, NumBytes: %d, r: %d\n", SrcAddr, DestAddr, pInst->SectorSize, r));
  return r;
}

/*********************************************************************
*
*       _FindLogSector
*
*  Function description
*    Locates the specified logical sector in flash memory.
*
*  Parameters
*    pInst            Driver instance.
*    LogSectorIndex   The index of the logical sector.
*                     The first sector has Index 0 !
*
*  Return value
*    >0     Address offset of found sector
*    ==0    Sector not found.
*/
static U32 _FindLogSector(const NOR_INST * pInst, U32 LogSectorIndex) {
  U32 r;

  //
  // Check parameters (higher debug level only).
  //
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_PARA)
  if (LogSectorIndex >= pInst->NumLogSectors) { // Test if the sector ID is within the valid range.
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR: _FindLogSector: LogSectorIndex out of range."));
    return 0;
  }
#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_PARA
  //
  // Look-up table for logical to physical translation.
  //
  r = FS_BITFIELD_ReadEntry(pInst->pL2P, LogSectorIndex, pInst->NumBitsUsed);
  return r;
}

/*********************************************************************
*
*       _GetLogSectorIndex
*
*  Function description
*    Returns the index of a logical sector. The return value can be a
*    sector number or a special code for special function sectors or
*    types such as "blank" or "info".
*
*  Return value
*    0 .. 0xFFFF0000    Index of logical sector
*    >=   0xFFFF0000    Not a logical sector (special code)
*
*  Additional information
*    pIsLSHConsistent is used to inform the caller if the data in
*    the header of the logical sector is consistent or not.
*
*    The version 4.04d adds a DataStat member to the header of the
*    logical sector that is used to mark the logical sector as invalid.
*    The logical sector is invalidated by setting  to 0 the DataStat member.
*    Previous versions were setting only the Id member (4 bytes) to 0
*    an operation that is not fail-safe on a serial NOR flash.
*    For performance reasons the version 4.04d sets only the DataStat
*    member to 0 and leaves the Id member untouched. This creates an
*    incompatibility with the older versions of emFile which are not
*    able anymore to correctly identify an invalid logical sector.
*    pIsLSHConsistent is set to 0 for erasable logical sectors that
*    have the members of the logical sector header set like this:
*    DataStat == 0 and Id != 0. In this case, the caller has to
*    re-invalidate the logical sector.
*
*    An additional situation when *pIsLSHConsistent is set to 0
*    is when the function detects a valid value in the Id member but the
*    DataStat member is set to 0xFF. This can happen when an emFile version
*    older than 4.04d writes to a physical sector formatted by an emFile
*    version >= 4.04d. This can also happen when a write operation performed
*    by an emFile version >= 4.04d is interrupted by an unexpected reset.
*    The caller has to differentiate between these two cases by counting
*    the number of times such an inconsistency is reported. If the count
*    is larger than 1 the values of all logical sectors reported as having
*    an inconsistent header have to be considered valid.
*/
static U32 _GetLogSectorIndex(NOR_INST * pInst, U32 Off, U8 PhySectorSignature, U8 * pIsLSHConsistent) {
  U32     LogSectorIndex;
  NOR_LSH lsh;
  U8      DataStat;
  U8      IsLSHConsistent;

  IsLSHConsistent = 1;          // Set to indicate that LSH is consistent.
  (void)_ReadLogSectorHeader(pInst, Off, &lsh);
  if (PhySectorSignature == PHY_SECTOR_SIGNATURE_LEGACY) {        // Legacy phy. sector?
    //
    // DataStat field in the header of the log. sector is not valid.
    //
    switch (lsh.Id) {
    case LOG_SECTOR_ID_BLANK:
      LogSectorIndex = LOG_SECTOR_ID_BLANK;
      break;
    case LOG_SECTOR_ID_INFO:
      LogSectorIndex = LOG_SECTOR_ID_INFO;
      break;
    default:
#if FS_NOR_CAN_REWRITE
      if (lsh.Id == 0u) {
        LogSectorIndex = LOG_SECTOR_ID_ERASABLE;
        break;
      }
#else
#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
      if (pInst->IsRewriteSupported != 0u) {
        if (lsh.Id == 0u) {
          LogSectorIndex = LOG_SECTOR_ID_ERASABLE;
          break;
        }
      } else
#endif  // FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
      {
        if (lsh.IsErasable == 0u) {
          LogSectorIndex = LOG_SECTOR_ID_ERASABLE;
          break;
        }
      }
#endif
      LogSectorIndex = PSI2LSI(lsh.Id);
      if (LogSectorIndex >= pInst->NumLogSectors) {
        FS_DEBUG_WARN((FS_MTYPE_DRIVER, "NOR: _GetLogSectorIndex: Logical sector index out of bounds."));
        LogSectorIndex = LOG_SECTOR_ID_ERASABLE;
      }
      break;
    }
  } else {
    //
    // DataStat field in the header of the log. sector is valid and has to be evaluated.
    //
    DataStat = lsh.DataStat;
#if (FS_NOR_CAN_REWRITE == 0)
#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
    if (pInst->IsRewriteSupported == 0u) {
      if (lsh.IsErasable == 0u) {
        DataStat = DATA_STAT_ERASABLE;
      }
    }
#else
    if (lsh.IsErasable == 0u) {
      DataStat = DATA_STAT_ERASABLE;
    }
#endif  // FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
#endif  // FS_NOR_CAN_REWRITE == 0
    if ((lsh.Id == LOG_SECTOR_ID_BLANK) && (DataStat == DATA_STAT_INVALID)) {
      LogSectorIndex = LOG_SECTOR_ID_BLANK;
    } else {
      if (DataStat == DATA_STAT_VALID) {
        if (lsh.Id == LOG_SECTOR_ID_INFO) {
          LogSectorIndex = LOG_SECTOR_ID_INFO;
        } else {
#if FS_NOR_SUPPORT_COMPATIBILITY_MODE
          //
          // The logical sector has been invalidated by an older emFile version (< 4.04d)
          // and the data valid flag was not cleared. Handle this case here in order
          // to suppress the error message below.
          //
          if (lsh.Id == 0u) {
            LogSectorIndex = LOG_SECTOR_ID_ERASABLE;
          } else
#endif // FS_NOR_SUPPORT_COMPATIBILITY_MODE
          {
            LogSectorIndex = PSI2LSI(lsh.Id);
            if (LogSectorIndex >= pInst->NumLogSectors) {
              FS_DEBUG_WARN((FS_MTYPE_DRIVER, "NOR: _GetLogSectorIndex: Logical sector index out of bounds."));
              LogSectorIndex = LOG_SECTOR_ID_ERASABLE;
            }
          }
        }
      } else {
        LogSectorIndex = LOG_SECTOR_ID_ERASABLE;
#if FS_NOR_SUPPORT_COMPATIBILITY_MODE
        if (DataStat == DATA_STAT_INVALID) {
          U32 lsiToCheck;

          //
          // Check for the consistency of the logical sector header. It is possible
          // that an older version of emFile (< 4.04d) stored data in this
          // physical sector formatted by a newer emFile version (>= 4.04d).
          // The caller is responsible for handling this case.
          //
          if ((lsh.Id != LOG_SECTOR_ID_INFO) && (lsh.Id != LOG_SECTOR_ID_ERASABLE)) {
            lsiToCheck = PSI2LSI(lsh.Id);
            if (lsiToCheck < pInst->NumLogSectors) {
              LogSectorIndex  = lsiToCheck;
              IsLSHConsistent = 0;
            }
          }
        }
#endif
      }
    }
  }
  //
  // Check if the logical sector has to be invalidated again.
  //
  if (pIsLSHConsistent != NULL) {
    if (((lsh.Id != 0u) && (lsh.DataStat == 0u)) || ((lsh.Id == 0u) && (lsh.DataStat != 0u))) {
      IsLSHConsistent = 0;
    }
  }
  //
  // Return information about the consistency of the logical sector header.
  //
  if (pIsLSHConsistent != NULL) {
    *pIsLSHConsistent = IsLSHConsistent;
  }
  return LogSectorIndex;
}

/*********************************************************************
*
*        _SizeOfPSH
*/
static unsigned _SizeOfPSH(const NOR_INST * pInst) {
  unsigned NumBytes;

#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  NumBytes = pInst->SizeOfPSH;
#else
  FS_USE_PARA(pInst);
  NumBytes = sizeof(NOR_PSH);
#endif
  return NumBytes;
}

/*********************************************************************
*
*       _GetEraseCnt
*/
static U32 _GetEraseCnt(const NOR_PSH * pPSH) {
  return pPSH->EraseCnt;
}

/*********************************************************************
*
*       _GetPhySectorType
*/
static U8 _GetPhySectorType(const NOR_INST * pInst, const NOR_PSH * pPSH) {
  FS_USE_PARA(pInst);
#if FS_NOR_CAN_REWRITE
  return pPSH->Type;
#else
#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  if (pInst->IsRewriteSupported != 0u) {
    return pPSH->Type;
  }
#endif  // FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  if (pPSH->IsValid == 0u) {
    return PHY_SECTOR_TYPE_INVALID;
  }
  if (pPSH->IsWork != 0u) {
    return PHY_SECTOR_TYPE_WORK;
  }
  return PHY_SECTOR_TYPE_DATA;
#endif  // FS_NOR_CAN_REWRITE
}

/*********************************************************************
*
*       _GetPhySectorFailSafeErase
*
*  Return value
*    ==0    Not supported
*    !=0    Supported
*/
static U8 _GetPhySectorFailSafeErase(const NOR_PSH * pPSH) {
  U8 r;

  r = 0;                              // Not supported
#if FS_NOR_SUPPORT_FAIL_SAFE_ERASE
  if (pPSH->FailSafeErase == 0u) {    // Reversed logic: 0x00 -> Supported, 0xFF -> not supported
    r = 1;                            // Supported
  }
#else
  FS_USE_PARA(pPSH);
#endif
  return r;
}

/*********************************************************************
*
*       _ReadPSH
*
*  Function description
*    Read physical sector header.
*/
static int _ReadPSH(NOR_INST * pInst, U32 Off, NOR_PSH * pPSH) {
  int   r;
  U32   NumBytes;
  U8  * pData;
#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  U8    abData[sizeof(NOR_PSH)];
  U32   NumBytesToCopy;
  U32   BytesPerLine;
#endif // FS_NOR_SUPPORT_VARIABLE_LINE_SIZE

  FS_MEMSET(pPSH, 0xFF, sizeof(NOR_PSH));
  NumBytes     = _SizeOfPSH(pInst);
  pData        = SEGGER_PTR2PTR(U8, pPSH);
#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  BytesPerLine = 1uL << pInst->ldBytesPerLine;
  FS_MEMSET(abData, 0xFF, sizeof(abData));
  if (BytesPerLine < (unsigned)FS_NOR_LINE_SIZE) {
    pData = abData;
  }
#endif // FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  r = _ReadOff(pInst, Off, pData, NumBytes);
#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  if (BytesPerLine < (unsigned)FS_NOR_LINE_SIZE) {
    FS_MEMSET(pPSH, 0xFF, sizeof(NOR_PSH));
    NumBytes       = 0;
    NumBytesToCopy = 0u
                   + OFFSET_OF_MEMBER(NOR_PSH, abReserved[0])   // Last member
                   + SIZE_OF_MEMBER(NOR_PSH, abReserved[0])
                   - OFFSET_OF_MEMBER(NOR_PSH, Signature);      // First member
    FS_MEMCPY(&pPSH->Signature, &abData[NumBytes], NumBytesToCopy);
#if (FS_NOR_CAN_REWRITE == 0)
    NumBytes += NumBytesToCopy;
    NumBytes  = ALIGN_TO_BOUNDARY(NumBytes, BytesPerLine);      // Align to flash line boundary.
    if (pInst->IsRewriteSupported == 0u) {
      NumBytesToCopy = 0u
                     + OFFSET_OF_MEMBER(NOR_PSH, IsWork)        // Last member
                     + SIZE_OF_MEMBER(NOR_PSH, IsWork)
                     - OFFSET_OF_MEMBER(NOR_PSH, IsWork);       // First member
      FS_MEMCPY(&pPSH->IsWork, &abData[NumBytes], NumBytesToCopy);
      NumBytes += NumBytesToCopy;
      NumBytes  = ALIGN_TO_BOUNDARY(NumBytes, BytesPerLine);    // Align to flash line boundary.
      NumBytesToCopy = 0u
                     + OFFSET_OF_MEMBER(NOR_PSH, IsValid)       // Last member
                     + SIZE_OF_MEMBER(NOR_PSH, IsValid)
                     - OFFSET_OF_MEMBER(NOR_PSH, IsValid);      // First member
      FS_MEMCPY(&pPSH->IsValid, &abData[NumBytes], NumBytesToCopy);
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ALL)
      NumBytes += NumBytesToCopy;
      NumBytes  = ALIGN_TO_BOUNDARY(NumBytes, BytesPerLine);    // Align to flash line boundary.
#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ALL
    }
#endif  // FS_NOR_CAN_REWRITE == 0
  }
#endif  // FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
#if FS_NOR_SUPPORT_LEGACY_MODE
  if (pInst->IsLegacyModeSupported != 0u) {
    pPSH->Signature = PHY_SECTOR_SIGNATURE_LEGACY;        // Fake the signature to avoid modifying other parts of the code.
  }
#endif
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR: READ_PSH Off: 0x%8x, Type: 0x%x, EraseCnt: 0x%8x,", Off, pPSH->Type, pPSH->EraseCnt));
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, " NumBytes: %d, Sig: 0x%2x, r: %d\n", NumBytes, pPSH->Signature, r));
  return r;
}

/*********************************************************************
*
*       _WritePSH
*
*  Function description
*    Write physical sector header.
*/
static int _WritePSH(NOR_INST * pInst, U32 Off, const NOR_PSH * pPSH) {
  int        r;
  unsigned   NumBytes;
  const U8 * pData;
#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  U8         abData[sizeof(NOR_PSH)];
  unsigned   NumBytesToCopy;
  U32        BytesPerLine;
#endif // FS_NOR_SUPPORT_VARIABLE_LINE_SIZE

  NumBytes = sizeof(NOR_PSH);
  pData    = SEGGER_CONSTPTR2PTR(const U8, pPSH);
#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  BytesPerLine = 1uL << pInst->ldBytesPerLine;
  if (BytesPerLine < (unsigned)FS_NOR_LINE_SIZE) {
    FS_MEMSET(abData, 0xFF, sizeof(abData));
    pData    = abData;
    NumBytes = 0;
    NumBytesToCopy = 0u
                   + OFFSET_OF_MEMBER(NOR_PSH, abReserved[0])   // Last member
                   + SIZE_OF_MEMBER(NOR_PSH, abReserved[0])
                   - OFFSET_OF_MEMBER(NOR_PSH, Signature);      // First member
    FS_MEMCPY(&abData[NumBytes], &pPSH->Signature, NumBytesToCopy);
    NumBytes += NumBytesToCopy;
    NumBytes  = ALIGN_TO_BOUNDARY(NumBytes, BytesPerLine);      // Align to flash line boundary.
#if (FS_NOR_CAN_REWRITE == 0)
    if (pInst->IsRewriteSupported == 0u) {
      NumBytesToCopy = 0u
                     + OFFSET_OF_MEMBER(NOR_PSH, IsWork)        // Last member
                     + SIZE_OF_MEMBER(NOR_PSH, IsWork)
                     - OFFSET_OF_MEMBER(NOR_PSH, IsWork);       // First member
      FS_MEMCPY(&abData[NumBytes], &pPSH->IsWork, NumBytesToCopy);
      NumBytes += NumBytesToCopy;
      NumBytes  = ALIGN_TO_BOUNDARY(NumBytes, BytesPerLine);    // Align to flash line boundary.
      NumBytesToCopy = 0u
                     + OFFSET_OF_MEMBER(NOR_PSH, IsValid)       // Last member
                     + SIZE_OF_MEMBER(NOR_PSH, IsValid)
                     - OFFSET_OF_MEMBER(NOR_PSH, IsValid);      // First member
      FS_MEMCPY(&abData[NumBytes], &pPSH->IsValid, NumBytesToCopy);
      NumBytes += NumBytesToCopy;
      NumBytes  = ALIGN_TO_BOUNDARY(NumBytes, BytesPerLine);    // Align to flash line boundary.
    }
#endif  // FS_NOR_CAN_REWRITE == 0
  }
#endif  // FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  INIT_VERIFY(pData, Off, NumBytes);
  CALC_PSH_DATA_RANGE(pInst, &pData, &Off, &NumBytes);
  r = _WriteOff(pInst, Off, pData, NumBytes);
  VERIFY_WRITE(pInst);
  INIT_PSH_DATA_RANGE();
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR: WRITE_PSH Off: 0x%8x, Type: 0x%x, EraseCnt: 0x%8x,", Off, pPSH->Type, pPSH->EraseCnt));
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, " NumBytes: %d, r: %d\n", NumBytes, r));
  return r;
}

/*********************************************************************
*
*       _GetMaxEraseCnt
*
*  Function description
*    Returns the highest erase count of all physical data sectors of the given size.
*    This can be useful if the erase count of a physical sector has been lost because
*    of a power failure during or right after erase of the sector.
*/
static U32 _GetMaxEraseCnt(NOR_INST * pInst, U32 SectorSize) {
  U32     NumPhySectors;
  U32     MaxEraseCnt;
  U32     Addr;
  U32     Size;
  U32     EraseCnt;
  U32     i;
  NOR_PSH psh;
  int     r;

  NumPhySectors = pInst->NumPhySectors;
  MaxEraseCnt   = 0;
  for (i = 0; i < NumPhySectors; i++) {
    _GetSectorInfo(pInst, i, &Addr, &Size);
    if (Size == SectorSize) {
      r = _ReadPSH(pInst, Addr, &psh);
      if (r == 0) {
        if (_GetPhySectorType(pInst, &psh) == PHY_SECTOR_TYPE_DATA) {
          EraseCnt = _GetEraseCnt(&psh);
          if ((EraseCnt > MaxEraseCnt) && (EraseCnt != ERASE_CNT_INVALID) && (EraseCnt < (U32)FS_NOR_MAX_ERASE_CNT)) {
            MaxEraseCnt = EraseCnt;
          }
        }
      }
    }
  }
  return MaxEraseCnt;
}

/*********************************************************************
*
*       _GetPhySectorSignature
*
*  Function description
*    Returns the signature of the physical sector to be used
*    when creating new data blocks.
*/
static U8 _GetPhySectorSignature(const NOR_INST * pInst) {
  U8 PhySectorSignature;

  PhySectorSignature = PHY_SECTOR_SIGNATURE;
#if FS_NOR_SUPPORT_LEGACY_MODE
  if (pInst->IsLegacyModeSupported != 0u) {
    PhySectorSignature = PHY_SECTOR_SIGNATURE_LEGACY;
  }
#else
  FS_USE_PARA(pInst);
#endif
  return PhySectorSignature;
}

/*********************************************************************
*
*       _SetPhySectorType
*
*  Function description
*    Modifies in the type of physical sector.
*/
static void _SetPhySectorType(const NOR_INST * pInst, NOR_PSH * pPSH, U8 SectorType) {
  FS_USE_PARA(pInst);
#if FS_NOR_CAN_REWRITE
  pPSH->Type = SectorType;
  UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_PSH, Type), sizeof(pPSH->Type));
#else
#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  if (pInst->IsRewriteSupported != 0u) {
    pPSH->Type = SectorType;
    UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_PSH, Type), sizeof(pPSH->Type));
  } else
#endif  // FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  if (SectorType == PHY_SECTOR_TYPE_INVALID) {
    pPSH->IsValid = 0;
    UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_PSH, IsValid), sizeof(pPSH->IsValid));
  } else if (SectorType == PHY_SECTOR_TYPE_DATA) {
    pPSH->IsWork  = 0;
    UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_PSH, IsWork), sizeof(pPSH->IsWork));
  } else {
    //
    // Error, invalid phy. sector type.
    //
  }
#endif  // FS_NOR_CAN_REWRITE
}

/*********************************************************************
*
*       _InvalidatePhySector
*
*  Function description
*    Invalidate the physical sector.
*    This is done typically before the sector is erased, in order to avoid that a
*    partial erase leads to data corruption.
*    This way, if the sector is not completely erased, it will not be mounted in the
*    case the header is still intact (but data is not).
*    We currently only invalidate the physical header, but could also invalidate
*    logical headers (which would eat up some more time)
*/
static int _InvalidatePhySector(NOR_INST * pInst, U32 PhySectorIndex) {
  NOR_PSH psh;
  U32     SectorOff;
  int     r;

  _GetSectorInfo(pInst, PhySectorIndex, &SectorOff, NULL);
  FS_MEMSET(&psh, 0xFF, sizeof(psh));
  r = _ReadPSH(pInst, SectorOff, &psh);
  if (r == 0) {
    //
    // Invalidate the physical sector only if necessary.
    //
    if (_GetPhySectorType(pInst, &psh) != PHY_SECTOR_TYPE_INVALID) {
      INIT_PSH_DATA_RANGE();
      _SetPhySectorType(pInst, &psh, PHY_SECTOR_TYPE_INVALID);
      r = _WritePSH(pInst, SectorOff, &psh);
#if FS_NOR_SUPPORT_CLEAN
      if (r == 0) {
        pInst->IsClean = 0;
      }
#endif // FS_NOR_SUPPORT_CLEAN
    }
  }
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR: INV_PHY_SECTOR PSI: %lu, Off: 0x%8x, r: %d\n", PhySectorIndex, SectorOff, r));
  return r;
}

/*********************************************************************
*
*       _ContainsErasable
*
*  Function description
*    Search for a physical flash sector that contains
*    erasable logical sectors and return its ID.
*
*  Parameters
*    pInst            Driver instance.
*    PhySectorOff     Address of the phy. sector.
*    PhySectorSize    Length of the phy. sector in bytes.
*
*  Return value
*    ==1      There is at least one free logical sector.
*    ==0      No free sector within this flash sector.
*/
static int _ContainsErasable(NOR_INST * pInst, U32 PhySectorOff, U32 PhySectorSize) {
  int      r;
  NOR_PSH  psh;
  unsigned LogSectorSize;
  U32      LogSectorIndex;
  unsigned SizeOfPSH;
  unsigned SizeOfLSH;
  U32      NumBytes;
  U32      Off;
  int      Result;

  r = 0;                    // Set to 0 -> no erasable sector found
  //
  // Make sure this is a data sector.
  //
  Result = _ReadPSH(pInst, PhySectorOff, &psh);
  if (Result != 0) {
    goto Done;              // Error, cannot read physical sector header.
  }
  if (_GetPhySectorType(pInst, &psh) != PHY_SECTOR_TYPE_DATA) {
    goto Done;
  }
#if FS_NOR_SUPPORT_COMPATIBILITY_MODE
  if (psh.Signature == PHY_SECTOR_SIGNATURE_LEGACY) {
    r = 1;                  // Prevent writing to physical sectors formatted by an old emFile version (< V4.04d)
    goto Done;
  }
#endif // FS_NOR_SUPPORT_COMPATIBILITY_MODE
  SizeOfLSH = _SizeOfLSH(pInst);
  SizeOfPSH = _SizeOfPSH(pInst);
  //
  // Try to find an erasable sector.
  //
  NumBytes      = PhySectorSize - SizeOfPSH;
  Off           = PhySectorOff + SizeOfPSH;
  LogSectorSize = SizeOfLSH + pInst->SectorSize;
  while (NumBytes >= LogSectorSize) {
    LogSectorIndex = _GetLogSectorIndex(pInst, Off, psh.Signature, NULL);
    if (LogSectorIndex == LOG_SECTOR_ID_ERASABLE) {
      r = 1;               // Erasable sector found !
      break;
    }
    NumBytes -= LogSectorSize;
    Off      += LogSectorSize;
  }
Done:
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR: CONTAINS_ERASABLE Off: %8x, r: %d\n", PhySectorOff, r));
  return r;
}

/*********************************************************************
*
*       _IsPhySectorFree
*
*  Function description
*    Checks if a physical sector is free, so it can be used as work buffer
*
*  Parameters
*    pInst            Driver instance.
*    PhySectorIndex   Physical sector to be checked.
*
*  Return value
*    ==1      Physical sector is free.
*    ==0      Physical sector is not free.
*/
static int _IsPhySectorFree(NOR_INST * pInst, U32 PhySectorIndex) {
  NOR_PSH  psh;
  U32      Off;
  U32      NumBytes;
  int      r;
  unsigned LogSectorSize;
  U32      LogSectorIndex;
  unsigned SizeOfLSH;
  unsigned SizeOfPSH;

  r = 1;                      // Default: Physical sector is free
  _GetSectorInfo(pInst, PhySectorIndex, &Off, &NumBytes);
  //
  // If it is not a data sector, this sector is free
  //
  FS_MEMSET(&psh, 0xFF, sizeof(psh));
  (void)_ReadPSH(pInst, Off, &psh);
  if (_GetPhySectorType(pInst, &psh) != PHY_SECTOR_TYPE_DATA) {
    goto Done;
  }
  SizeOfLSH = _SizeOfLSH(pInst);
  SizeOfPSH = _SizeOfPSH(pInst);
  //
  // Try to find an erasable sector
  //
  NumBytes      -= SizeOfPSH;
  Off           += SizeOfPSH;
  LogSectorSize  = SizeOfLSH + pInst->SectorSize;
  while (NumBytes >= LogSectorSize) {
    LogSectorIndex = _GetLogSectorIndex(pInst, Off, psh.Signature, NULL);
    if ((LogSectorIndex <= pInst->NumLogSectors) ||  (LogSectorIndex == LOG_SECTOR_ID_INFO)) {
      r = 0;                  // Physical sector is not free
      break;
    }
    NumBytes -= LogSectorSize;
    Off      += LogSectorSize;
  }
Done:
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR: IS_PHY_SECTOR_FREE PSI: %lu, r: d\n", PhySectorIndex, r));
  return r;
}

#if ((FS_NOR_CAN_REWRITE != 0) || (FS_NOR_SUPPORT_VARIABLE_LINE_SIZE != 0) || (FS_NOR_OPTIMIZE_DIRTY_CHECK != 0))

/*********************************************************************
*
*        _FindPhySector
*
*  Function description
*    Finds out which phy. sector contains a specified offset.
*
*  Parameters
*    pInst      Driver instance.
*    Off        Byte offset to be checked.
*
*  Return value
*    !=PSI_INVALID    Index of the physical sector.
*    ==PSI_INVALID    Phy. sector not found.
*/
static I32 _FindPhySector(const NOR_INST * pInst, U32 Off) {
  unsigned NumPhySectors;
  unsigned PhySectorIndex;
  U32      PhySectorOff;
  U32      PhySectorSize;

  NumPhySectors = pInst->NumPhySectors;
  for (PhySectorIndex = 0; PhySectorIndex < NumPhySectors; ++PhySectorIndex) {
    _GetSectorInfo(pInst, PhySectorIndex, &PhySectorOff, &PhySectorSize);
    if ((Off >= PhySectorOff) && (Off < (PhySectorOff + PhySectorSize))) {
      return (I32)PhySectorIndex;   // OK, phy. sector found.
    }
  }
  return PSI_INVALID;               // Error, phy. sector not found.
}

#endif // FS_NOR_CAN_REWRITE || FS_NOR_SUPPORT_VARIABLE_LINE_SIZE

/*********************************************************************
*
*        _MarkLogSectorAsInvalid
*
*  Function description
*    Marks a logical sector as invalid.
*    This routine is called from the higher layer file-system to
*    help the driver to manage the data:
*    This way sectors which are no longer in use by the higher
*    layer file system do not need to be copied.
*/
static int _MarkLogSectorAsInvalid(NOR_INST * pInst, U32 Off) {
  NOR_LSH lsh;
  U8      IsUpdateRequired;
  int     r;
#if ((FS_NOR_CAN_REWRITE != 0) || (FS_NOR_SUPPORT_VARIABLE_LINE_SIZE != 0))
  U8      PhySectorSignature;
  NOR_PSH psh;
  U32     PhySectorOff;
  I32     PhySectorIndex;
#endif // (FS_NOR_CAN_REWRITE != 0) || (FS_NOR_SUPPORT_VARIABLE_LINE_SIZE != 0)

  r = 0;              // Set to indicate success.
#if ((FS_NOR_CAN_REWRITE != 0) || (FS_NOR_SUPPORT_VARIABLE_LINE_SIZE != 0))
  PhySectorSignature = _GetPhySectorSignature(pInst);
  if (pInst->Status.LegacyPhySectorsFound != 0u) {
    PhySectorSignature = PHY_SECTOR_SIGNATURE_LEGACY;
    //
    // Get the signature from the header of the phy. sector
    // if we have physical sectors with different signatures
    // (that is PHY_SECTOR_SIGNATURE_LEGACY and PHY_SECTOR_SIGNATURE).
    //
    PhySectorIndex = _FindPhySector(pInst, Off);
    if (PhySectorIndex != PSI_INVALID) {
      _GetSectorInfo(pInst, (unsigned int)PhySectorIndex, &PhySectorOff, NULL);
      FS_MEMSET(&psh, 0xFF, sizeof(psh));
      (void)_ReadPSH(pInst, PhySectorOff, &psh);
      PhySectorSignature = psh.Signature;
    }
  }
#endif // (FS_NOR_CAN_REWRITE != 0) || (FS_NOR_SUPPORT_VARIABLE_LINE_SIZE != 0)
  INIT_LSH_DATA_RANGE();
  FS_MEMSET(&lsh, 0xFF, sizeof(lsh));
  (void)_ReadLogSectorHeader(pInst, Off, &lsh);
  IsUpdateRequired = 0;
#if FS_NOR_CAN_REWRITE
  if (PhySectorSignature == PHY_SECTOR_SIGNATURE_LEGACY) {
    if (lsh.Id != 0u) {
      lsh.Id           = 0;
      lsh.DataStat     = DATA_STAT_ERASABLE;
      IsUpdateRequired = 1;
      UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_LSH, Id), sizeof(lsh.Id));
      UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_LSH, DataStat), sizeof(lsh.DataStat));
    }
  } else {
    if ((lsh.Id != 0u) || (lsh.DataStat != DATA_STAT_ERASABLE)) {
      lsh.Id           = 0;
      lsh.DataStat     = DATA_STAT_ERASABLE;
      IsUpdateRequired = 1;
      UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_LSH, Id), sizeof(lsh.Id));
      UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_LSH, DataStat), sizeof(lsh.DataStat));
    }
  }
#else
#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  if (pInst->IsRewriteSupported != 0u) {
    if (PhySectorSignature == PHY_SECTOR_SIGNATURE_LEGACY) {
      if (lsh.Id != 0u) {
        lsh.Id           = 0;
        lsh.DataStat     = DATA_STAT_ERASABLE;
        IsUpdateRequired = 1;
        UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_LSH, Id), sizeof(lsh.Id));
        UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_LSH, DataStat), sizeof(lsh.DataStat));
      }
    } else {
      if ((lsh.Id != 0u) || (lsh.DataStat != DATA_STAT_ERASABLE)) {
        lsh.Id           = 0;
        lsh.DataStat     = DATA_STAT_ERASABLE;
        IsUpdateRequired = 1;
        UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_LSH, Id), sizeof(lsh.Id));
        UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_LSH, DataStat), sizeof(lsh.DataStat));
      }
    }
  } else
#endif  // FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  {
    if (lsh.IsErasable != 0u) {
      lsh.IsErasable   = 0;
      UPDATE_LSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_LSH, IsErasable), sizeof(lsh.IsErasable));
      IsUpdateRequired = 1;
    }
  }
#endif  // FS_NOR_CAN_REWRITE
  if (IsUpdateRequired != 0u) {
    r = _WriteLogSectorHeader(pInst, Off, &lsh);      // Mark sector as erasable.
#if FS_NOR_SUPPORT_CLEAN
    if (r == 0) {
      pInst->IsClean = 0;
    }
#endif // FS_NOR_SUPPORT_CLEAN
  }
  INIT_LSH_DATA_RANGE();
  return r;
}

#if FS_SUPPORT_FREE_SECTOR

/*********************************************************************
*
*        _FreeSectors
*
*  Function description
*    Marks a logical sector as free.
*    This routine is called from the higher layer file-system to
*    help the driver to manage the data:
*    This way sectors which are no longer in use by the higher
*    layer file system do not need to be copied.
*/
static void _FreeSectors(NOR_INST * pInst, U32 LogSectorIndex, U32 NumSectors) {
  U32 PhysAddr;

  //
  // Check parameters (higher debug level only)
  //
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_PARA)
  if (LogSectorIndex >= pInst->NumLogSectors) {         // Test if the id is within valid range.
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR: _FreeSectors: Sector index out of range."));
    return;
  }
#endif  // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_PARA
  do {
    PhysAddr = _FindLogSector(pInst, LogSectorIndex);   // Check if sector already exists.
    if (PhysAddr != 0u) {
      (void)_MarkLogSectorAsInvalid(pInst, PhysAddr);
      (void)_WriteL2PEntry(pInst, LogSectorIndex, 0);
    }
    LogSectorIndex++;
  } while (--NumSectors != 0u);
}

#endif // FS_SUPPORT_FREE_SECTOR

/*********************************************************************
*
*        _ErasePhySector
*
*  Function description
*    Sets to 1 all the bits in a physical sector.
*/
static int _ErasePhySector(NOR_INST * pInst, U32 PhySectorIndex) {
  int r;
  U8  Unit;

  Unit = pInst->Unit;
  r = pInst->pPhyType->pfEraseSector(Unit, PhySectorIndex);
  CALL_TEST_HOOK_SECTOR_ERASE(Unit, PhySectorIndex, &r);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR: _ErasePhySector: Erase failed with %d @ sector 0x%x.", r, PhySectorIndex));
    return r;
  }
#if FS_NOR_OPTIMIZE_DIRTY_CHECK
  _MarkPhySectorAsClean(pInst, PhySectorIndex);
#endif
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR: ERASE_PHY_SECTOR PSI: %lu\n", PhySectorIndex));
  IF_STATS(pInst->StatCounters.EraseCnt++);     // Increment statistical counter if enabled.
  //
  // On higher debug levels, check if erase worked.
  //
#if FS_NOR_VERIFY_ERASE
  if (pInst->VerifyErase != 0u) {
    U32 Off;
    U32 Size;
    U8  aData[32];

    _GetSectorInfo(pInst, PhySectorIndex, &Off, &Size);
    for (;;) {
      unsigned i;

      r = _ReadOff(pInst, Off, &aData[0], sizeof(aData));
      if (r == 0) {
        for (i = 0; i < sizeof(aData); i++) {
          if (aData[i] != 0xFFu) {
            FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR: _ErasePhySector: Verification failed @ Off 0x%08x.", Off + i));
            r = 1;
            break;
          }
        }
      }
      if (r != 0) {
        break;
      }
      Off  += sizeof(aData);
      Size -= sizeof(aData);
      if (Size == 0u) {
        break;
      }
    }
  }
#endif // FS_NOR_VERIFY_ERASE
  return r;
}

/*********************************************************************
*
*        _ErasePhySectorFailSafe
*
*  Function description
*    Sets all the bytes in a phy. sector to 0xFF and writes a
*    signature to header indicating that the phy. sector has been
*    successfully erased.
*
*  Parameters
*    pInst            Driver instance.
*    PhySectorIndex   Index of the phy. sector to be erased.
*    EraseCntInit     Optional erase count to be stored in the header
*                     of the phy. sector.
*
*  Return value
*    ==0      OK, phy. sector erased.
*    !=0      An error occurred.
*/
static int _ErasePhySectorFailSafe(NOR_INST * pInst, U32 PhySectorIndex, U32 EraseCntInit) {
  int     r;
  U32     SectorOff;
  U32     SectorLen;
  U8      FailSafeErase;
  NOR_PSH psh;
  int     Result;

  r = 0;
  FailSafeErase = pInst->Status.FailSafeErase;
  _GetSectorInfo(pInst, PhySectorIndex, &SectorOff, &SectorLen);
  if (FailSafeErase != 0u) {
    INIT_PSH_DATA_RANGE();
    FS_MEMSET(&psh, 0xFF, sizeof(psh));
    //
    // First, mark the phy. sector as being erased.
    //
    (void)_ReadPSH(pInst, SectorOff, &psh);
    //
    // Invalidate the signature only if it is valid.
    // Writing to partially erased sectors may cause write errors.
    //
    if (psh.EraseSignature == ERASE_SIGNATURE_VALID) {
      psh.EraseSignature = ERASE_SIGNATURE_INVALID;
      UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_PSH, EraseSignature), sizeof(psh.EraseSignature));
      Result = _WritePSH(pInst, SectorOff, &psh);
      if (Result != 0) {
        r = Result;                 // Error, could not write signature.
      }
    }
    INIT_PSH_DATA_RANGE();
  }
  //
  // Erase the phy. sector.
  //
  Result = _ErasePhySector(pInst, PhySectorIndex);
  if (Result != 0) {
    r = Result;                     // Error, could not erase phy. sector.
  }
  if (FailSafeErase != 0u) {
    INIT_PSH_DATA_RANGE();
    //
    // TestPoint: Set break and reset the target here.
    //
    CALL_TEST_HOOK_FAIL_SAFE(pInst->Unit);

    FS_MEMSET(&psh, 0xFF, sizeof(psh));
    //
    // Set the erase count here to minimize the chance that an unexpected reset corrupts its value.
    //
    psh.EraseCnt = EraseCntInit;
    //
    // Mark the phy. sector as erased.
    //
    psh.EraseSignature = ERASE_SIGNATURE_VALID;
    UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_PSH, EraseSignature), sizeof(psh.EraseSignature));
    Result = _WritePSH(pInst, SectorOff, &psh);
    if (Result != 0) {
      r = Result;
    }
    INIT_PSH_DATA_RANGE();
  }
  return r;
}

/*********************************************************************
*
*        _IsValidEraseSignature
*/
static int _IsValidEraseSignature(const NOR_PSH * pPSH) {
  U32 EraseSignature;

  EraseSignature = pPSH->EraseSignature;
  if (EraseSignature == ERASE_SIGNATURE_VALID) {
    return 1;   // Valid
  }
  return 0;     // Invalid
}

#if FS_NOR_SKIP_BLANK_SECTORS

/*********************************************************************
*
*       _IsPhySectorBlank
*
*  Function description
*    Checks if all the bytes in a physical sector are set to 0xFF.
*/
static int _IsPhySectorBlank(NOR_INST * pInst, U32 PhySectorIndex) {
  U32 SectorOff;
  U32 SectorSize;
  int r;
  U32 aBuffer[8];       // Note: the size of the buffer must be a power of 2 since the size of a physical sector is always a power of 2.

  SectorOff  = 0;
  SectorSize = 0;
  _GetSectorInfo(pInst, PhySectorIndex, &SectorOff, &SectorSize);
  if (SectorSize != 0u) {
    do {
      r = _ReadOff(pInst, SectorOff, aBuffer, sizeof(aBuffer));
      if (r != 0) {
        return 0;       // Error, could not read data.
      }
      if ((aBuffer[0] != 0xFFFFFFFFuL) ||
          (aBuffer[1] != 0xFFFFFFFFuL) ||
          (aBuffer[2] != 0xFFFFFFFFuL) ||
          (aBuffer[3] != 0xFFFFFFFFuL) ||
          (aBuffer[4] != 0xFFFFFFFFuL) ||
          (aBuffer[5] != 0xFFFFFFFFuL) ||
          (aBuffer[6] != 0xFFFFFFFFuL) ||
          (aBuffer[7] != 0xFFFFFFFFuL)) {
        return 0;       // The physical sector is not blank.
      }
      SectorOff  += sizeof(aBuffer);
      SectorSize -= sizeof(aBuffer);
    } while (SectorSize != 0u);
  }
  return 1;             // The physical sector is blank
}

#endif // FS_NOR_SKIP_BLANK_SECTORS

/*********************************************************************
*
*       _IsPhySectorEraseRequired
*/
static int _IsPhySectorEraseRequired(NOR_INST * pInst, U32 PhySectorIndex) {
#if FS_NOR_SKIP_BLANK_SECTORS
  if (pInst->SkipBlankSectors != 0u) {
    if (_IsPhySectorBlank(pInst, PhySectorIndex) != 0) {
      return 0;     // The phy. sector does not have be erased.
    }
  }
  return 1;         // The phy. sector has to be erased.
#else
  FS_USE_PARA(pInst);
  FS_USE_PARA(PhySectorIndex);
  return 1;         // The phy. sector must be erased.
#endif  // FS_NOR_SKIP_BLANK_SECTORS
}

/*********************************************************************
*
*       _MarkPhySectorAsData
*
*  Function description
*    Marks a physical sector as data sector. The phys. sector header
*    must have been written before.
*/
static int _MarkPhySectorAsData(NOR_INST * pInst, U32 PhySectorIndex) {
  NOR_PSH psh;
  U32     SectorOff;
  int     r;

  _GetSectorInfo(pInst, PhySectorIndex, &SectorOff, NULL);
  FS_MEMSET(&psh, 0xFF, sizeof(psh));
  r = _ReadPSH(pInst, SectorOff, &psh);
  if (r == 0) {
    if (_GetPhySectorType(pInst, &psh) != PHY_SECTOR_TYPE_DATA) {   // Change the type of the sector only if necessary.
      INIT_PSH_DATA_RANGE();
      _SetPhySectorType(pInst, &psh, PHY_SECTOR_TYPE_DATA);
      r = _WritePSH(pInst, SectorOff, &psh);
    }
  }
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR: MARK_AS_DATA PSI: %lu, Off: 0x%8x, r: %d\n", PhySectorIndex, SectorOff, r));
  return r;
}

/*********************************************************************
*
*       _AddFreeSectorToCache
*
*  Function description
*    Adds a free sector to the free sector list.
*    If there is no space in the free sector list, the entry is not added.
*    Not adding the entry is no problem since the list is filled up if it is empty;
*    This will only cost some time, but will work fine.
*
*  Parameters
*    pInst     Driver instance.
*    Off       The offset of the free logical sector to add.
*/
static void _AddFreeSectorToCache(NOR_INST * pInst, U32 Off) {
  unsigned WrPos;
  unsigned CountOf;

  CountOf = SEGGER_COUNTOF(pInst->Status.FreeSectorCache.aData);
  if (pInst->Status.FreeSectorCache.Cnt >= CountOf) {
    pInst->Status.FreeSectorCache.SkipFill = 0;
    return;       // Cache is already full
  }
  WrPos = pInst->Status.FreeSectorCache.RdPos + pInst->Status.FreeSectorCache.Cnt;
  if (WrPos >= CountOf) {
    WrPos -= CountOf;
  }
  pInst->Status.FreeSectorCache.aData[WrPos] = Off;
  pInst->Status.FreeSectorCache.Cnt++;
}

/*********************************************************************
*
*       _ChangeWorkSector
*
*  Function description
*    Sets the index of a physical sector to be used as work sector.
*
*  Parameters
*    pInst            Driver instance.
*    PhySectorIndex   Index of the physical sector that has to be invalidated.
*
*  Notes
*    (1) When a data sector is changed to a work sector,
*        the FreeSectorCache table may contain entries from this physical sector.
*        These entries need to be invalidated in the FreeSectorCache.
*/
static void _ChangeWorkSector(NOR_INST * pInst, U32 PhySectorIndex) {
  U32      StartOff;
  U32      EndOff;
  U32      Len;
  unsigned CountOf;
  unsigned i;
  int      SectorShiftCnt;

  _GetSectorInfo(pInst, PhySectorIndex, &StartOff, &Len);
  SectorShiftCnt = _SectorSize2ShiftCnt(Len);
  if (SectorShiftCnt >= 0) {
    pInst->Status.aWorkIndex[SectorShiftCnt] = (I32)PhySectorIndex;
    //
    // Make sure that there are no entries in the free list for this phy. sector
    //
    EndOff  = StartOff + Len - 1u;
    CountOf = SEGGER_COUNTOF(pInst->Status.FreeSectorCache.aData);
    for (i = 0; i < CountOf; i++) {
      U32 CacheEntry;

      CacheEntry = pInst->Status.FreeSectorCache.aData[i];
      if ((StartOff <= CacheEntry) && ((CacheEntry < EndOff))) {
         pInst->Status.FreeSectorCache.aData[i] = 0;
      }
    }
  }
}

/*********************************************************************
*
*       _RemoveFreeSectorFromCache
*
*  Function description
*    Removes a free sector from the free sector list.
*
*  Return value
*    pInst      Driver instance.
*/
static U32 _RemoveFreeSectorFromCache(NOR_INST * pInst) {
  U32      Off;
  unsigned RdPos;

  //
  // If the cache is empty, return 0
  //
  if (pInst->Status.FreeSectorCache.Cnt == 0u) {
    return 0;
  }
  RdPos = pInst->Status.FreeSectorCache.RdPos;
  Off   = pInst->Status.FreeSectorCache.aData[RdPos];
  RdPos++;
  if (RdPos >= (unsigned)FS_NOR_NUM_FREE_SECTORCACHE) {
    RdPos = 0;
  }
  pInst->Status.FreeSectorCache.RdPos = RdPos;
  pInst->Status.FreeSectorCache.Cnt--;
  if (pInst->Status.FreeSectorCache.Cnt == 0u) {
    pInst->Status.FreeSectorCache.SkipFill = 0;
  }
  //
  // Higher debug levels: Check if this Offset is currently in use
  // which would be a fatal error.
  //
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL)
  if (Off != 0u) {
    unsigned i;

    for (i = 0; i < pInst->NumLogSectors; i++) {
      U32 L2PEntry;

      L2PEntry  = FS_BITFIELD_ReadEntry(pInst->pL2P, i, pInst->NumBitsUsed);
      if (L2PEntry == Off) {
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR: _RemoveFreeSectorFromCache: Free sector is in use."));
        FS_X_PANIC(FS_ERRCODE_VERIFY_FAILURE);
      }
    }
  }
#endif  // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  return Off;
}

/*********************************************************************
*
*       _CopyDataSector
*
*  Function description
*    Copies the data contained in one physical sector into an other physical sector of the same size.
*    All valid logical sectors inside this physical sector are copied; the invalid / unused parts of the old sector
*    are not copied and left blank in the destination sector. The blank parts of the destination sector
*    can later on be used to store other data.
*/
static int _CopyDataSector(NOR_INST * pInst, U32 DstPhySec, U32 SrcPhySec) {
  NOR_PSH  psh;
  U32      DstAddr;
  U32      DstLen;
  U32      SrcAddr;
  U32      SrcLen;
  U32      SrcAddrStart;
  U32      DstAddrStart;
  U32      EraseCnt;
  unsigned LogSectorSize;
  int      r;
  U8       PhySectorSignature;
  U32      LogSectorIndex;
  unsigned SizeOfLSH;
  unsigned SizeOfPSH;

  _GetSectorInfo(pInst, DstPhySec, &DstAddr, &DstLen);
  _GetSectorInfo(pInst, SrcPhySec, &SrcAddr, &SrcLen);
  DstAddrStart = DstAddr;      // Remember it for later
  SrcAddrStart = SrcAddr;
  //
  // Erase destination sector. Before doing so, we need to read the header of
  // of both the source and destination phy. sectors. The header of the destination
  // phy. sector contains the same information as the header of the source phy. sector
  // except of the EraseCnt.
  //
  FS_MEMSET(&psh, 0xFF, sizeof(psh));
  (void)_ReadPSH(pInst, DstAddr, &psh);
  EraseCnt = _GetEraseCnt(&psh);
  if ((EraseCnt == ERASE_CNT_INVALID) || (EraseCnt >= (U32)FS_NOR_MAX_ERASE_CNT)) {
    EraseCnt = _GetMaxEraseCnt(pInst, SrcLen);
  } else {
    EraseCnt++;
  }
  FS_MEMSET(&psh, 0xFF, sizeof(psh));
  (void)_ReadPSH(pInst, SrcAddr, &psh);
  PhySectorSignature = psh.Signature;
  r = _ErasePhySectorFailSafe(pInst, DstPhySec, EraseCnt);
  if (r != 0) {
    return r;                                         // Error, could not ease physical sector.
  }
  FS_MEMSET(&psh, 0xFF, sizeof(psh));
  INIT_PSH_DATA_RANGE();
  psh.Signature     = _GetPhySectorSignature(pInst);
  psh.FormatVersion = (U8)FORMAT_VERSION;
  psh.EraseCnt      = EraseCnt;
  UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_PSH, Signature), sizeof(psh.Signature));
  UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_PSH, FormatVersion), sizeof(psh.FormatVersion));
  UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_PSH, EraseCnt), sizeof(psh.EraseCnt));
#if FS_NOR_SUPPORT_FAIL_SAFE_ERASE
  if (_IsRewriteSupported(pInst) != 0) {
    psh.FailSafeErase  = 0;     // Reversed logic: 0x00 -> supported, 0xFF -> not supported
    psh.EraseSignature = ERASE_SIGNATURE_VALID;
    UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_PSH, FailSafeErase), sizeof(psh.FailSafeErase));
    UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_PSH, EraseSignature), sizeof(psh.EraseSignature));
  }
#endif // FS_NOR_SUPPORT_FAIL_SAFE_ERASE
  r = _WritePSH(pInst, DstAddrStart, &psh);
  if (r != 0) {
    return r;                                         // Error, could not update the header of the physical sector.
  }
  SizeOfLSH = _SizeOfLSH(pInst);
  SizeOfPSH = _SizeOfPSH(pInst);
  //
  // Copy all logical sectors containing data.
  //
  pInst->Status.WLSectorSize = DstLen;                // Check wear level of all sector of this size on next occasion
  SrcAddr += SizeOfPSH;
  DstAddr += SizeOfPSH;
  LogSectorSize = SizeOfLSH + pInst->SectorSize;
  while ((SrcAddr + LogSectorSize) <= (SrcAddrStart + SrcLen)) {
    LogSectorIndex = _GetLogSectorIndex(pInst, SrcAddr, PhySectorSignature, NULL);
    if (   (LogSectorIndex < pInst->NumLogSectors)
        || (LogSectorIndex == LOG_SECTOR_ID_INFO)) {  // Does this sector contain data ? If so, copy it.
      //
      // Copy the data portion of log. sector
      //
      r = _CopySectorData(pInst, DstAddr + SizeOfLSH, SrcAddr + SizeOfLSH);
      if (r != 0) {
        break;                                        // Error, could not copy sector data.
      }
      //
      // Update the logical sector header
      //
      r = _WriteLogSectorInfo(pInst, DstAddr, LogSectorIndex);
      if (r != 0) {
        break;                                        // Error, could not update the header of the logical sector.
      }
      //
      // Update L2P table.
      //
      if (LogSectorIndex != LOG_SECTOR_ID_INFO) {
        if (_WriteL2PEntry(pInst, LogSectorIndex, DstAddr) != SrcAddr) {
          FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR: L2P entry is not correct."));
        }
      }
    } else {
      _AddFreeSectorToCache(pInst, DstAddr);
    }
    SrcAddr += LogSectorSize;
    DstAddr += LogSectorSize;
  }
  if (r == 0) {
    //
    // Mark target data as valid data sector and source as invalid.
    // Note that this is a critical point for stability: If we have an unexpected RESET
    // after validating the destination, but before invalidating the source, we will have 2 physical sectors
    // with identical logical sectors. This needs to be handled correctly.
    //
    r = _MarkPhySectorAsData(pInst, DstPhySec);
    if (r != 0) {
      return r;                                           // Error, could not mark physical sector as data.
    }

    CALL_TEST_HOOK_FAIL_SAFE(pInst->Unit);                // TestPoint: Set break and RESET here.

    r = _InvalidatePhySector(pInst, SrcPhySec);
    if (r == 0) {
      _ChangeWorkSector(pInst, SrcPhySec);
    }
  }
  return r;
}

/*********************************************************************
*
*       _GetWorkSectorIndex
*
*  Function description
*    Returns the index of the physical work sector for the given sector size.
*/
static I32 _GetWorkSectorIndex(const NOR_INST * pInst, U32 SectorSize) {
  I32 WorkSectorIndex;
  int SectorShiftCnt;

  WorkSectorIndex = PSI_INVALID;
  SectorShiftCnt  = _SectorSize2ShiftCnt(SectorSize);
  if (SectorShiftCnt >= 0) {
    WorkSectorIndex = pInst->Status.aWorkIndex[SectorShiftCnt];
    //
    // On higher debug levels heck if we have a valid work sector and if no L2P entries are in it.
    //
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL)
    if ((U32)WorkSectorIndex >= pInst->NumPhySectors) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR: Invalid work sector index."));
      FS_X_PANIC(FS_ERRCODE_VERIFY_FAILURE);
    } else {
      unsigned i;
      U32      Off;
      U32      StartOff;
      U32      Size;

      _GetSectorInfo(pInst, (unsigned int)WorkSectorIndex, &StartOff, &Size);
      for (i = 0; i < pInst->NumLogSectors; i++) {
        Off = FS_BITFIELD_ReadEntry(pInst->pL2P, i, pInst->NumBitsUsed);
        if ((Off != 0u) && (Off >= StartOff) && (Off < (StartOff + Size))) {
          FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR: Physical work sector still contains valid data."));
          FS_X_PANIC(FS_ERRCODE_VERIFY_FAILURE);
        }
      }
    }
#endif  // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  }
  return WorkSectorIndex;
}

#if FS_NOR_SUPPORT_CLEAN

/*********************************************************************
*
*       _SetWorkSectorIndex
*
*  Function description
*    Sets the index of a physical sector for a given sector size.
*/
static void _SetWorkSectorIndex(NOR_INST * pInst, U32 SectorSize, I32 WorkSectorIndex) {
  int SectorShiftCnt;

  SectorShiftCnt = _SectorSize2ShiftCnt(SectorSize);
  if (SectorShiftCnt >= 0) {
    pInst->Status.aWorkIndex[SectorShiftCnt] = WorkSectorIndex;
  }
}

#endif // FS_NOR_SUPPORT_CLEAN

/*********************************************************************
*
*       _WearLevel
*
*  Function description
*    Perform the actual wear leveling.
*
*  Notes
*    (1) The sector index given is the index of the work sector. If
*        this work sector has been erased a lot more often than an other
*        physical sector of the same size, this sector is consider "fitter".
*        In this case, the contents of the fitter sector are copied
*        to the work sector and the fitter sector is used as work sector.
*/
static int _WearLevel(NOR_INST * pInst) {
  NOR_PSH psh;
  U32     Addr;
  U32     MinCnt;
  U32     MinCntSector;
  U32     EraseCnt;
  U32     Size;
  U32     WLSize;
  U32     WLEraseCnt;
  int     NumPhySectors;
  int     i;
  int     r;

  //
  // Check if wear leveling is required (usually only after a ERASE)
  //
  if (pInst->Status.WLSectorSize == 0u) {
    return 0;     // OK, nothing to do.
  }
  r = 0;
  WLSize = pInst->Status.WLSectorSize;
  MinCntSector = 0;
  //
  // Find sector of the same size with smallest erase count
  //
  NumPhySectors = (int)pInst->NumPhySectors;
  MinCnt = 0xFFFFFFFFuL;
  for (i = 0; i < NumPhySectors; i++) {
    _GetSectorInfo(pInst, (unsigned int)i, &Addr, &Size);
    if (Size == WLSize) {
      r = _ReadPSH(pInst, Addr, &psh);
      if (r == 0) {
        if (_GetPhySectorType(pInst, &psh) == PHY_SECTOR_TYPE_DATA) {
          EraseCnt = _GetEraseCnt(&psh);
          if (EraseCnt < MinCnt) {
            MinCnt = EraseCnt;
            MinCntSector = (U32)i;
          }
        }
      }
    }
  }
  //
  // Copy data if it makes sense, meaning if the erase count difference exceed a certain predefined number.
  //
  i = _GetWorkSectorIndex(pInst, WLSize);
  _GetSectorInfo(pInst, (unsigned int)i, &Addr, NULL);
  FS_MEMSET(&psh, 0xFF, sizeof(psh));
  (void)_ReadPSH(pInst, Addr, &psh);
  WLEraseCnt = _GetEraseCnt(&psh);
  if (WLEraseCnt > (MinCnt + (U32)FS_NOR_MAX_ERASE_CNT_DIFF)) {
    int SectorShiftCnt;

    //
    // Invalidate the old work sector.
    //
    SectorShiftCnt = _SectorSize2ShiftCnt(WLSize);
    if (SectorShiftCnt >= 0) {
      pInst->Status.aWorkIndex[SectorShiftCnt] = PSI_INVALID;
    }
    r = _CopyDataSector(pInst, (U32)i, MinCntSector);
  }
  pInst->Status.WLSectorSize = 0;      // Do not perform this again until next erase.
  return r;
}

/*********************************************************************
*
*       _CalcNumLogSectors
*
*  Function description
*    Calculate number of logical sectors.
*
*  Return value
*    Number of available sectors.
*
*  Notes
*    (1) This function does not need to access the flash. It simply performs this calculation
*        based on the size and number of available physical sectors.
*/
static U32 _CalcNumLogSectors(const NOR_INST * pInst) {
  U32      LogSectorCnt;
  U32      PhySectorSize;
  U32      NumPhySectors;
  U32      LSectorsPerPSector;
  unsigned i;
  unsigned LogSectorSize;
  unsigned SizeOfLSH;
  unsigned SizeOfPSH;

  SizeOfLSH     = _SizeOfLSH(pInst);
  SizeOfPSH     = _SizeOfPSH(pInst);
  LogSectorCnt  = 0;
  LogSectorSize = SizeOfLSH + pInst->SectorSize;
  for (i = 0; i < SEGGER_COUNTOF(pInst->Status.aWorkIndex); i++) {      // Iterate over all sizes.
    NumPhySectors = pInst->aNumPhySectorsPerSize[i];
    if (NumPhySectors != 0u) {
      PhySectorSize = _SectorShiftCnt2Size(i);
      LSectorsPerPSector = (PhySectorSize - SizeOfPSH) / LogSectorSize;
      LogSectorCnt += LSectorsPerPSector * (NumPhySectors - 1u);
    }
  }
  LogSectorCnt = (LogSectorCnt * (100uL - pInst->pctLogSectorsReserved)) / 100uL;
  return LogSectorCnt;
}

/*********************************************************************
*
*       _InitSizeInfo
*
*  Function description
*    Initializes information about the geometry of the NOR flash device.
*/
static void _InitSizeInfo(NOR_INST * pInst) {
  U32 SecOff;
  U32 SecLen;
  int NumPhySectors;
  int i;
  int SectorShiftCnt;

  FS_MEMSET(pInst->Status.aWorkIndex, 0, sizeof(pInst->Status.aWorkIndex));   // Not required, to be safe for re-init.
  NumPhySectors = (int)pInst->NumPhySectors;
  for (i = 0; i < NumPhySectors; i++) {
    _GetSectorInfo(pInst, (unsigned int)i, &SecOff, &SecLen);
    SectorShiftCnt = _SectorSize2ShiftCnt(SecLen);
    if (SectorShiftCnt >= 0) {
      pInst->aNumPhySectorsPerSize[SectorShiftCnt]++;
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_PARA)
      if (_FlashStart > SecOff) {
        _FlashStart = SecOff;
      }
      if (_FlashEnd < (SecOff + SecLen)) {
        _FlashEnd = SecOff + SecLen;
      }
#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_PARA
    }
  }
}

/*********************************************************************
*
*       _InitStatus
*
*  Function description
*    Initialize the status of an instance.
*
*  Parameters
*    pInst      Driver instance.
*
*  Return value
*    ==0        OK, status initialized.
*    !=0        An error occurred.
*/
static int _InitStatus(NOR_INST * pInst) {
  NOR_STATUS * pStatus;
  unsigned     i;
  U32          SizeOfBitField;
  U32          Off;
  U32          Len;
  U32          LastOff;
  U32          NumPhySectors;
  int          r;

  r       = 0;          // Set to indicate success.
  pStatus = &pInst->Status;
  Off     = 0;
  Len     = 0;
  //
  // Invalidate all information with 0.
  //
  FS_MEMSET(pStatus, 0, sizeof(NOR_STATUS));
  //
  // Mark all work sector as unknown
  //
  for (i = 0; i < SEGGER_COUNTOF(pStatus->aWorkIndex); i++) {
    pInst->Status.aWorkIndex[i] = -1;
  }
  //
  // Initialize the logical 2 physical table.
  //
  NumPhySectors = pInst->NumPhySectors;
  if (NumPhySectors != 0u) {
    pInst->pPhyType->pfGetSectorInfo(pInst->Unit, NumPhySectors - 1u, &Off, &Len);
    LastOff = Off + Len - 1u;
    pInst->NumBitsUsed = FS_BITFIELD_CalcNumBitsUsed(LastOff);
    SizeOfBitField = FS_BITFIELD_CalcSize(pInst->NumLogSectors, pInst->NumBitsUsed);
    if (pInst->NumLogSectors != 0u) {
      FS_ALLOC_ZEROED_PTR(SEGGER_PTR2PTR(void *, &pInst->pL2P), (I32)SizeOfBitField, "NOR_SECTOR_MAP");
      if (pInst->pL2P == NULL) {
        r = 1;                      // Error, could not allocate memory.
      }
    }
    //
    // Allocate memory for the bit array that indicates
    // if a blank check has to be performed on all the
    // the empty logical sectors of a physical sector.
    //
#if FS_NOR_OPTIMIZE_DIRTY_CHECK
    if (pInst->IsDirtyCheckOptimized != 0u) {
      U32 NumBytes;

      NumBytes = _SizeOfDirtyMap(pInst);
      FS_ALLOC_ZEROED_PTR(SEGGER_PTR2PTR(void *, &pInst->pDirtyMap), (I32)NumBytes, "NOR_DIRTY_MAP");
      if (pInst->pDirtyMap == NULL) {
        r = 1;                        // Error, could not allocate memory.
      }
    }
#endif  // FS_NOR_OPTIMIZE_DIRTY_CHECK
  }
  return r;
}

/*********************************************************************
*
*       _CheckInfoSector
*
*  Function description
*    Checks if the information in the info sector matches the current version and settings.
*
*  Return value
*    ==0  O.K.
*    !=0  Error
*/
static int _CheckInfoSector(NOR_INST * pInst, U32 Off) {
  U32      aInfo[8];
  U32      DriveState;
  unsigned SizeOfLSH;

  SizeOfLSH = _SizeOfLSH(pInst);
  FS_MEMSET(aInfo, 0xFF, sizeof(aInfo));
  (void)_ReadOff(pInst, Off + SizeOfLSH, aInfo, sizeof(aInfo));
  if (aInfo[INFO_SECTOR_OFF_SIGNATURE] != SIGNATURE) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR: Signature mismatch."));
    return 1;
  }
  if ((aInfo[INFO_SECTOR_OFF_VERSION >> 2] >> 16) != VERSION_MAJOR) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR: Version mismatch."));
    return 1;
  }
  if (aInfo[INFO_SECTOR_OFF_NUM_LOG_SECTORS >> 2] != pInst->NumLogSectors) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR: Number of logical sectors mismatch."));
    return 1;
  }
  if (aInfo[INFO_SECTOR_OFF_BYTES_PER_LOG_SECTOR >> 2] != pInst->SectorSize) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR: Logical sector size mismatch."));
    return 1;
  }
  DriveState = aInfo[INFO_SECTOR_OFF_HAS_ERROR >> 2];
  if (DriveState  != NOR_ERROR_STATE_OK) {
    if (DriveState  == NOR_ERROR_STATE_READONLY) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR: Flash is in readonly mode."));
      _SetError(pInst);
    } else {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR: Unexpected error."));
      return 1;
    }
  }
  return 0;
}

/*********************************************************************
*
*       _IsLogSectorBlank
*
*  Function description
*    Checks if all the bytes in a logical sector are set to 0xFF.
*
*  Parameters
*    pInst      Driver instance.
*    Off        Offset of the first byte in the logical sector header.
*
*  Return value
*    ==1    Sector is blank.
*    ==0    Sector is not blank.
*/
static int _IsLogSectorBlank(NOR_INST * pInst, U32 Off) {
  U32        acBuffer[32];
  unsigned   NumBytes;
  unsigned   NumBytesAtOnce;
  unsigned   NumItems;
  U32      * p;
  unsigned   SizeOfLSH;
  int        r;

  SizeOfLSH = _SizeOfLSH(pInst);
  NumBytes = pInst->SectorSize + SizeOfLSH;
  do {
    NumBytesAtOnce = SEGGER_MIN(NumBytes, sizeof(acBuffer));
    r = _ReadOff(pInst, Off, acBuffer, NumBytesAtOnce);
    if (r != 0) {
      return 0;                     // Error, could not read data.
    }
    NumItems = NumBytesAtOnce >> 2;
    p        = acBuffer;
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
*       _AddPhySectorData
*
*  Function description
*    Adds all logical sectors in a physical data sector to the L2P table.
*/
static void _AddPhySectorData(NOR_INST * pInst, U32 PhySectorOff, U32 PhySectorSize, U8 PhySectorSignature) {
  U32      Off;
  U32      LastPermittedLogSectorOff;
  U32      LogSectorIndex;
  unsigned LogSectorSize;
  U32      PrevOff;
  unsigned SizeOfLSH;
  unsigned SizeOfPSH;
  U8       IsLSHConsistent;

  SizeOfLSH = _SizeOfLSH(pInst);
  SizeOfPSH = _SizeOfPSH(pInst);
  LogSectorSize = SizeOfLSH + pInst->SectorSize;
  LastPermittedLogSectorOff = PhySectorOff + PhySectorSize - LogSectorSize;
  PhySectorOff += SizeOfPSH;
  //
  // Now we add the logical sectors one by one.
  //
  for (Off = PhySectorOff; Off <= LastPermittedLogSectorOff; Off += LogSectorSize) {
    LogSectorIndex = _GetLogSectorIndex(pInst, Off, PhySectorSignature, &IsLSHConsistent);
    if (LogSectorIndex < pInst->NumLogSectors) {
#if FS_NOR_SUPPORT_COMPATIBILITY_MODE
      if (IsLSHConsistent == 0u) {
        U32 NumLogSectorsInvalid;
        U32 OffLogSectorInvalid;
        U32 LogSectorIndexInvalid;

        NumLogSectorsInvalid = pInst->NumLogSectorsInvalid;
        if (NumLogSectorsInvalid == 0u) {
          //
          // Remember the first invalid logical sector.
          //
          pInst->OffLogSectorInvalid   = Off;
          pInst->LogSectorIndexInvalid = LogSectorIndex;
        } else if (NumLogSectorsInvalid == 1u) {
          LogSectorIndexInvalid = pInst->LogSectorIndexInvalid;
          OffLogSectorInvalid   = pInst->OffLogSectorInvalid;
          //
          // Add the previous invalid logical sector to the list of valid sectors.
          //
          PrevOff = _WriteL2PEntry(pInst, LogSectorIndexInvalid, OffLogSectorInvalid);
          //
          // Check if this entry is already set. If so, this is a copy of an other logical data sector.
          //
          if (PrevOff != 0u) {
            (void)_WriteL2PEntry(pInst, LogSectorIndexInvalid, PrevOff);        // Restore former value.
            (void)_MarkLogSectorAsInvalid(pInst, OffLogSectorInvalid);
          } else {
            //
            // Mark the first invalid logical sectors as valid.
            //
            (void)_WriteLogSectorInfo(pInst, OffLogSectorInvalid, LogSectorIndexInvalid);
          }
          //
          // Mark the second invalid logical sectors as valid.
          //
          (void)_WriteLogSectorInfo(pInst, Off, LogSectorIndex);
        } else {
          //
          // Mark the invalid logical sector as valid.
          //
          (void)_WriteLogSectorInfo(pInst, Off, LogSectorIndex);
        }
        pInst->NumLogSectorsInvalid = NumLogSectorsInvalid + 1u;
        if (NumLogSectorsInvalid == 0u) {
          continue;
        }
      }
#endif
      PrevOff = _WriteL2PEntry(pInst, LogSectorIndex, Off);
      //
      // Check if this entry is already set. If so, this is a copy of an other logical data sector.
      //
      if (PrevOff != 0u) {
        (void)_WriteL2PEntry(pInst, LogSectorIndex, PrevOff);                   // Restore former value.
        (void)_MarkLogSectorAsInvalid(pInst, Off);
      }
    } else if (LogSectorIndex == LOG_SECTOR_ID_INFO) {
      if (pInst->Status.OffInfoSector != 0u) {                                  // Check if this entry is already set.
        (void)_MarkLogSectorAsInvalid(pInst, Off);
      } else {
        pInst->Status.OffInfoSector = Off;
      }
    } else if (LogSectorIndex == LOG_SECTOR_ID_ERASABLE) {
      if (IsLSHConsistent == 0u) {
        (void)_MarkLogSectorAsInvalid(pInst, Off);                              // The logical sector has to be marked again as invalid.
      }
    } else if (LogSectorIndex != LOG_SECTOR_ID_BLANK) {
      (void)_MarkLogSectorAsInvalid(pInst, Off);
    } else {
      //
      // Invalid sector index.
      //
    }
  }
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
static int _InitDevice(const NOR_INST * pInst) {
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
*    Calculates the operation parameters of the NOR flash device.
*
*  Parameters
*    pInst      Driver instance.
*
*  Return value
*    ==0   OK, parameters calculated.
*    !=0   An error occurred.
*/
static int _ReadApplyDeviceParas(NOR_INST * pInst) {
  U32 NumLogSectors;
  int r;

  //
  // Initialize the physical layer.
  //
  r = _InitDevice(pInst);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR: Could not initialize device."));
    return 1;
  }
  pInst->NumPhySectors = (U32)pInst->pPhyType->pfGetNumSectors(pInst->Unit);
  //
  // Fill the SizeInfostructure
  //
  _InitSizeInfo(pInst);
  //
  // Determine the sector size
  //
  if (pInst->SectorSize == 0u) {
    pInst->SectorSize = FS_Global.MaxSectorSize;
  }
  //
  // Calculate the number of logical sectors
  //
  if (pInst->NumLogSectors == 0u) {
    NumLogSectors = _CalcNumLogSectors(pInst);
    if (NumLogSectors == 0u) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR: Cannot calculate the number of logical sectors with this configuration."));
      return 1;
    }
    pInst->NumLogSectors = NumLogSectors;
  }
  r = _InitStatus(pInst);
  return r;
}

/*********************************************************************
*
*       _InvalidateRecoverPhySector
*
*  Function description
*    Marks a phy. sector as invalid. If the operation fails
*    the phy. sector is erased to make sure that the next write
*    operation to this phy. sector succeeds.
*/
static void _InvalidateRecoverPhySector(NOR_INST * pInst, unsigned PhySectorIndex) {
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS)
  U32 ErrorFilter;
#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS
  int r;

  //
  // Disable temporarily the error messages since we perform error recovery
  // if the invalidate operation fails.
  //
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS)
  ErrorFilter = FS__GetErrorFilterNL();
  FS__SetErrorFilterNL(ErrorFilter & ~FS_MTYPE_DRIVER);
#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS
  r = _InvalidatePhySector(pInst, PhySectorIndex);    // Mark this sector as invalid
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
    (void)_ErasePhySectorFailSafe(pInst, PhySectorIndex, ERASE_CNT_INVALID);
  }
}

/*********************************************************************
*
*       _LowLevelMount
*
*  Function description
*    Reads and analyzes management information from NAND flash.
*    If the information makes sense and allows us to read and write from
*    the medium, it can perform read and write operations.
*
*  Return value
*    ==0     O.K., device has been successfully mounted and is accessible
*    !=0     Error
*/
static int _LowLevelMount(NOR_INST * pInst) {
  U32     i;
  U32     NumPhySectors;
  U32     NumPhyDataSectors;
  U32     Start;
  U32     SecLen;
  NOR_PSH psh;
  int     SectorShiftCnt;
  U8      SectorType;
  int     r;
  U8      FailSafeErase;
  I32     WorkSectorIndex;
  U8      LegacyPhySectorsFound;

  r = _InitStatus(pInst);
  if (r != 0) {
    return r;                                   // Error, could not initialize status.
  }
  if (pInst->NumLogSectors == 0u) {
    r = _ReadApplyDeviceParas(pInst);
    if (r != 0) {
      return 1;                                 // Error, could not read NOR flash info.
    }
  }
  //
  // Perform a quick check if the medium is LL-formatted.
  //
  NumPhySectors         = pInst->NumPhySectors;
  NumPhyDataSectors     = 0;
  FailSafeErase         = 0;
  LegacyPhySectorsFound = 0;
  for (i = 0; i < NumPhySectors; i++) {
    _GetSectorInfo(pInst, i, &Start, &SecLen);
    FS_MEMSET(&psh, 0xFF, sizeof(psh));
    (void)_ReadPSH(pInst, Start, &psh);
    SectorType = _GetPhySectorType(pInst, &psh);
    if (SectorType == PHY_SECTOR_TYPE_DATA) {
      NumPhyDataSectors++;
    }
    //
    // Check if the NOR flash has been formatted with support for fail-safe erase.
    //
    if ((psh.Signature == PHY_SECTOR_SIGNATURE_LEGACY) || (psh.Signature == PHY_SECTOR_SIGNATURE)) {  // Make sure that the information in the phy. sector header is valid.
      if (_GetPhySectorFailSafeErase(&psh) != 0u) {
        FailSafeErase = 1;
      }
    }
    //
    // Check if valid log. sectors exist with invalid DataStat field.
    //
    if (psh.Signature == PHY_SECTOR_SIGNATURE_LEGACY) {
      LegacyPhySectorsFound = 1;
    }
  }
  if (NumPhyDataSectors == 0u) {
    FS_DEBUG_WARN((FS_MTYPE_DRIVER, "NOR: No data sectors found. Low level format required."));
    return 1;
  }
  pInst->Status.FailSafeErase         = FailSafeErase;
  pInst->Status.LegacyPhySectorsFound = LegacyPhySectorsFound;
  //
  //  Build the L2P table and check physical format at the same time
  //
  for (i = 0; i < NumPhySectors; i++) {
    _GetSectorInfo(pInst, i, &Start, &SecLen);
    FS_MEMSET(&psh, 0, sizeof(psh));
    (void)_ReadPSH(pInst, Start, &psh);
#if FS_NOR_OPTIMIZE_DIRTY_CHECK
    //
    // All the logical sectors have to be checked for blank before use since
    // we do not know if a power fail interrupted a write operation or not.
    //
    _MarkPhySectorAsDirty(pInst, i);
#endif // FS_NOR_OPTIMIZE_DIRTY_CHECK
    //
    // First, check if the phy. sector has been correctly erased.
    //
    if (FailSafeErase != 0u) {
      if (_IsValidEraseSignature(&psh) == 0) {
        _InvalidateRecoverPhySector(pInst, i);
      }
    }
    SectorType = _GetPhySectorType(pInst, &psh);
    if (SectorType == PHY_SECTOR_TYPE_DATA) {
      _AddPhySectorData(pInst, Start, SecLen, psh.Signature);
      continue;           // O.K., this physical sector is a valid data sector. Next in line!
    }
    //
    // This sector is not a valid data sector. We'll remember it and will use it as work sector when required.
    //
    SectorShiftCnt = _SectorSize2ShiftCnt(SecLen);
    if (SectorShiftCnt < 0) {
      return 1;           // Error, should not happen.
    }
    //
    // If we already have a work sector for sectors of this size, let us erase the previous one since we only need one.
    //
    WorkSectorIndex = pInst->Status.aWorkIndex[SectorShiftCnt];
    if (WorkSectorIndex != -1) {
      _InvalidateRecoverPhySector(pInst, (unsigned)WorkSectorIndex);
    }
    //
    // Make this work sector for its size
    //
    pInst->Status.aWorkIndex[SectorShiftCnt] = (I32)i;
  }
#if FS_NOR_SUPPORT_COMPATIBILITY_MODE
  {
    U32 Off;
    U32 NumLogSectorsInvalid;

    //
    // Mark the logical sector as invalid if any has been found.
    //
    Off                  = pInst->OffLogSectorInvalid;
    NumLogSectorsInvalid = pInst->NumLogSectorsInvalid;
    if ((Off != 0u) && (NumLogSectorsInvalid == 1u)) {
      (void)_MarkLogSectorAsInvalid(pInst, Off);
    }
  }
#endif // FS_NOR_SUPPORT_COMPATIBILITY_MODE
  //
  // Verify that a work sector exists for every sector size
  //
  for (i = 0; i < SEGGER_COUNTOF(pInst->Status.aWorkIndex); i++) {
    if (pInst->aNumPhySectorsPerSize[i] != 0u) {
      if (pInst->Status.aWorkIndex[i] == -1) {
        U32 iSector;

        //
        // Check if a sector was marked as data, but no data have been written to this sector
        // In this case we can use it as work sector.
        //
        for (iSector = 0; iSector < NumPhySectors; iSector++) {
          if (_IsPhySectorFree(pInst, iSector) != 0) {
            _InvalidateRecoverPhySector(pInst, iSector);
            pInst->Status.aWorkIndex[i] = (I32)iSector;
            break;
          }
        }
        if (pInst->Status.aWorkIndex[i] == -1) {
          FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR: No work sector available for sector index %d, can not low-level mount.", i));
          return 1;         // No work sector for this size. Fatal error; should not be possible under any circumstances!
        }
      }
    }
  }
  //
  // Make sure we have a valid info sector. If not, refuse to low-level mount.
  //
  if (pInst->Status.OffInfoSector == 0u) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR: No info sector found, can not low-level mount."));
    return 1;                        // No work sector sector for this size. Fatal error; should not be possible under any circumstances!
  }
  if (_CheckInfoSector(pInst, pInst->Status.OffInfoSector) != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR: Incompatible format acc. to info sector, can not low-level mount."));
    return 1;                        // If the info sector is not o.k., this is a fatal error!
  }
#if FS_NOR_SUPPORT_CLEAN
  pInst->IsClean = 0;
#endif // FS_NOR_SUPPORT_CLEAN
  pInst->Status.IsLLMounted = 1;
  return 0;
}

/*********************************************************************
*
*       _FillFreeSectorCache
*
*  Function description
*    Fills the free sectors cache.
*    It iterates over all physical sectors, checks if the sector is a data sector
*    and adds all blank logical sectors to the FreeSectorList.
*
*  Return value
*    FreeSectorCnt    0 means that no free sectors are available and should not happen.
*/
static int _FillFreeSectorCache(NOR_INST * pInst) {
  U32                 NumPhySectors;
  U32                 i;
  U32                 Start;
  U32                 SecLen;
  U32                 Off;
  U32                 End;
  NOR_PSH             psh;
  unsigned            LogSectorSize;
  U32                 LogSectorIndex;
  U8                  Type;
  FREE_SECTOR_CACHE * pCache;
  unsigned            SizeOfLSH;
  unsigned            SizeOfPSH;
  U8                  Signature;

  pCache = &pInst->Status.FreeSectorCache;
  if (pCache->SkipFill != 0u) {
    return 0;
  }
  SizeOfLSH = _SizeOfLSH(pInst);
  SizeOfPSH = _SizeOfPSH(pInst);
  LogSectorSize = SizeOfLSH + pInst->SectorSize;
  NumPhySectors = pInst->NumPhySectors;
  for (i = 0; i < NumPhySectors; i++) {
    _GetSectorInfo(pInst, i, &Start, &SecLen);
    End = Start + SecLen;
    FS_MEMSET(&psh, 0, sizeof(psh));
    (void)_ReadPSH(pInst, Start, &psh);
    Type = _GetPhySectorType(pInst, &psh);
    Signature = psh.Signature;
#if (FS_NOR_SUPPORT_COMPATIBILITY_MODE == 0)
    if (Type == PHY_SECTOR_TYPE_DATA)
#else
    if (   (Type == PHY_SECTOR_TYPE_DATA)
        && (Signature != PHY_SECTOR_SIGNATURE_LEGACY))      // Writing to physical sectors formatted by an old emFile version (< 4.04d)
                                                            // is not fail-safe on a serial NOR flash. Prevent writing to these sectors
#endif // FS_NOR_SUPPORT_COMPATIBILITY_MODE
    {
      Off = Start + SizeOfPSH;
      while (Off < End) {
        LogSectorIndex = _GetLogSectorIndex(pInst, Off, Signature, NULL);
        if (LogSectorIndex == LOG_SECTOR_ID_BLANK) {
          _AddFreeSectorToCache(pInst, Off);
          //
          // Check if we have found enough free sectors.
          //
          if (pCache->Cnt >= (U32)FS_NOR_NUM_FREE_SECTORCACHE) {
            pCache->SkipFill = 0;
            return (int)pCache->Cnt;
          }
        }
        Off += LogSectorSize;
        if ((Off + LogSectorSize) > End) {
          break;
        }
      }
    }
  }
  if (pCache->Cnt != 0u) {
    pInst->Status.FreeSectorCache.SkipFill = 1;
  }
  return (int)pCache->Cnt;
}

/*********************************************************************
*
*       _FindClearableSector
*
*  Function description
*    Searches for a phy. sector that contains erasable logical sectors.
*
*  Parameters
*    pInst      Driver instance.
*
*  Return value
*    !=PSI_INVALID   Valid physical sector number.
*    ==PSI_INVALID   No clearable sector found.
*/
static I32 _FindClearableSector(NOR_INST * pInst) {
  U32 SrcLen;
  U32 SrcAddr;
  int NumPhySectors;
  int i;
  int PhySectorIndex;
  int r;

  NumPhySectors = (int)pInst->NumPhySectors;
  //
  // Search for a physical sector that contains erasable logical sectors.
  //
  for (i = 0; i < NumPhySectors; i++) {
    PhySectorIndex = pInst->Status.psiLastCleared + i;
    if (PhySectorIndex >= NumPhySectors) {
      PhySectorIndex -= NumPhySectors;
    }
    _GetSectorInfo(pInst, (unsigned int)PhySectorIndex, &SrcAddr, &SrcLen);
    r = _ContainsErasable(pInst, SrcAddr, SrcLen);
    if (r != 0) {
      pInst->Status.psiLastCleared = PhySectorIndex + 1;
      return PhySectorIndex;      // Erasable phy. sector found.
    }
  }
  return PSI_INVALID;             // No clearable sector found.
}

/*********************************************************************
*
*       _FindInvalidSector
*
*  Function description
*    Searches for a phy. sector that contains invalid data.
*
*  Parameters
*    pInst      Driver instance.
*
*  Return value
*    !=PSI_INVALID   Valid physical sector number.
*    ==PSI_INVALID   No invalid sector found.
*/
static I32 _FindInvalidSector(NOR_INST * pInst) {
  U32     SectorLen;
  U32     SectorOff;
  U32     NumPhySectors;
  U32     iPhySector;
  NOR_PSH psh;
  U8      PhySectorType;
  I32     WorkSectorIndex;
  int     r;

  //
  // Search for a physical sector which contains invalid data.
  //
  NumPhySectors = pInst->NumPhySectors;
  for (iPhySector = 0; iPhySector < NumPhySectors; iPhySector++) {
    _GetSectorInfo(pInst, iPhySector, &SectorOff, &SectorLen);
    WorkSectorIndex = _GetWorkSectorIndex(pInst, SectorLen);
    if (WorkSectorIndex != (I32)iPhySector) {   // Skip over the work block.
      r = _ReadPSH(pInst, SectorOff, &psh);
      if (r == 0) {
        PhySectorType = _GetPhySectorType(pInst, &psh);
        if (PhySectorType == PHY_SECTOR_TYPE_INVALID) {
          return (I32)iPhySector;
        }
      }
    }
  }
  return PSI_INVALID;                           // No phy. sector found that contains invalid data.
}

/*********************************************************************
*
*       _CreateDataSector
*
*  Function description
*    Erases the specified phy. sector and marks it as data sector.
*
*  Parameters
*    pInst            Driver instance.
*    PhySectorIndex   Index of the phy. sector.
*
*  Return value
*    ==0    OK, data sector created.
*    !=0    An error occurred.
*/
static int _CreateDataSector(NOR_INST * pInst, U32 PhySectorIndex) {
  U32     SectorOff;
  U32     SectorLen;
  NOR_PSH psh;
  U32     EraseCnt;
  int     r;

  //
  // Get the erase count of the phy. sector and erase the phy. sector.
  //
  _GetSectorInfo(pInst, (unsigned int)PhySectorIndex, &SectorOff, &SectorLen);
  FS_MEMSET(&psh, 0xFF, sizeof(psh));
  (void)_ReadPSH(pInst, SectorOff, &psh);
  r = _ErasePhySectorFailSafe(pInst, (U32)PhySectorIndex, ERASE_CNT_INVALID);
  if (r != 0) {
    return 1;                   // Error, could not erase phy. sector.
  }
  //
  // Mark the phy. sector as data block.
  //
  EraseCnt = _GetEraseCnt(&psh);
  if ((EraseCnt == ERASE_CNT_INVALID) || (EraseCnt >= (U32)FS_NOR_MAX_ERASE_CNT)) {
    EraseCnt = _GetMaxEraseCnt(pInst, SectorLen);
  }
  INIT_PSH_DATA_RANGE();
  psh.EraseCnt       = EraseCnt;
  psh.FormatVersion  = (U8)FORMAT_VERSION;
  psh.Signature      = _GetPhySectorSignature(pInst);
  UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_PSH, EraseCnt), sizeof(psh.EraseCnt));
  UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_PSH, FormatVersion), sizeof(psh.FormatVersion));
  UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_PSH, Signature), sizeof(psh.Signature));
#if FS_NOR_SUPPORT_FAIL_SAFE_ERASE
  if (_IsRewriteSupported(pInst) != 0) {
    psh.FailSafeErase  = 0;     // Reversed logic: 0x00 -> supported, 0xFF -> not supported
    psh.EraseSignature = ERASE_SIGNATURE_VALID;
    UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_PSH, FailSafeErase), sizeof(psh.FailSafeErase));
    UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_PSH, EraseSignature), sizeof(psh.EraseSignature));
  }
#endif // FS_NOR_SUPPORT_FAIL_SAFE_ERASE
  _SetPhySectorType(pInst, &psh, PHY_SECTOR_TYPE_DATA);
  r = _WritePSH(pInst, SectorOff, &psh);
  if (r != 0) {
    return 1;                   // Error, could not write phy. sector header.
  }
  //
  // Add the newly created empty logical sectors to cache.
  //
  if (pInst->Status.FreeSectorCache.Cnt == 0u) {
    r = _FillFreeSectorCache(pInst);
    if (r == 0) {
      return 1;                 // Error, could not fill cache.
    }
  }
  return 0;                     // OK, data sector created.
}

/*********************************************************************
*
*       _MakeCleanSector
*
*  Function description
*    Copy the data of a physical sector which contains cleanable log. sectors
*    to the work sector. This produces one or more free log. sectors.
*
*  Return value
*    ==0    OK, created free log. sectors.
*    !=0    Error, no clearable log. sectors found.
*/
static int _MakeCleanSector(NOR_INST * pInst) {
  U32 SectorOff;
  U32 SectorLen;
  I32 psiSrc;
  I32 psiDest;
  int r;

  //
  // Erase invalid phy. sectors if required.
  //
  psiSrc = _FindInvalidSector(pInst);
  if (psiSrc != PSI_INVALID) {
    //
    // Convert the invalid sector to a data sector so that
    // the logical sectors can be added to cache.
    //
    r = _CreateDataSector(pInst, (U32)psiSrc);
  } else {
    r = 1;                                    // Set to indicate an error.
    psiSrc = _FindClearableSector(pInst);     // Find a sector that contains erasable sectors.
    if (psiSrc != PSI_INVALID) {
      SectorOff = 0;
      SectorLen = 0;
      _GetSectorInfo(pInst, (unsigned)psiSrc, &SectorOff, &SectorLen);
      psiDest = _GetWorkSectorIndex(pInst, SectorLen);
      if (psiDest != PSI_INVALID) {
        int SectorShiftCnt;

        //
        // Invalidate old work sector.
        //
        SectorShiftCnt = _SectorSize2ShiftCnt(SectorLen);
        if (SectorShiftCnt >= 0) {
          pInst->Status.aWorkIndex[SectorShiftCnt] = PSI_INVALID;
        }
        r = _CopyDataSector(pInst, (U32)psiDest, (U32)psiSrc);
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _FindFreeLogSector
*
*  Function description
*    Finds a free logical sector.
*
*  Return value
*    > 0      Address of a free logical sector
*    ==0      No free sector found
*/
static U32 _FindFreeLogSector(NOR_INST * pInst) {
  U32 Off;     // Offset of free logical sector

  for (;;) {
    if (pInst->Status.FreeSectorCache.Cnt == 0u) {
      if (_FillFreeSectorCache(pInst) == 0) {
        (void)_MakeCleanSector(pInst);
        if (pInst->Status.FreeSectorCache.Cnt == 0u) {
          FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR: Could not find / create a free logical sector."));
          return 0;                 // Fatal error!
        }
      }
    }
    //
    // Remove one item from free sector cache and check if it is actually blank
    //
    Off = _RemoveFreeSectorFromCache(pInst);
    if (Off == 0u) {
      continue;
    }
#if FS_NOR_OPTIMIZE_DIRTY_CHECK
    if (pInst->IsDirtyCheckOptimized != 0u) {
      I32 PhySectorIndex;

      PhySectorIndex = _FindPhySector(pInst, Off);
      if (PhySectorIndex != PSI_INVALID) {
        if (_IsPhySectorDirty(pInst, (U32)PhySectorIndex) == 0) {
          //
          // We know that the physical sector has been erased at least once since the last mount operation.
          // It is not required anymore to check if the contents of the logical sector is blank.
          //
          break;
        }
      }
    }
#endif // FS_NOR_OPTIMIZE_DIRTY_CHECK
    if (_IsLogSectorBlank(pInst, Off) != 0) {
      break;
    }
    //
    // Sector is not blank, even though it is in the free list and should be blank.
    // This situation can arise if a sector write operation has been interrupted by
    // a reset; in this case the header is still marked as blank, but a part of the data
    // area is not blank. We mark the sector as containing invalid data so we do not
    // run into it again. It will be re-used when the physical sector is erased.
    //
    (void)_MarkLogSectorAsInvalid(pInst, Off);      // Not completely blank, so mark it as invalid to make sure we know next time.
  }
  return Off;
}

/*********************************************************************
*
*       _WriteInfoSector
*
*  Function description
*    Write the info sector, which contains all relevant information
*    of the NOR flash device.
*/
static int _WriteInfoSector(NOR_INST * pInst) {
  U32      aInfo[8];
  U32      Off;
  int      r;
  unsigned SizeOfLSH;

  FS_MEMSET(aInfo, 0x00, sizeof(aInfo));
  aInfo[INFO_SECTOR_OFF_SIGNATURE]                 = SIGNATURE;
  aInfo[INFO_SECTOR_OFF_VERSION >> 2]              = VERSION;
  aInfo[INFO_SECTOR_OFF_NUM_LOG_SECTORS >> 2]      = pInst->NumLogSectors;
  aInfo[INFO_SECTOR_OFF_BYTES_PER_LOG_SECTOR >> 2] = pInst->SectorSize;
  aInfo[INFO_SECTOR_OFF_HAS_ERROR >> 2]            = NOR_ERROR_STATE_OK;
  Off = _FindFreeLogSector(pInst);        // Try to get empty sector
  if (Off == 0u) {
    return 1;                             // Error, could not get an empty logical sector.
  }
  SizeOfLSH = _SizeOfLSH(pInst);
  r = _WriteLogSectorData(pInst, Off + SizeOfLSH, aInfo, sizeof(aInfo));
  if (r == 0) {
    r = _WriteLogSectorInfo(pInst, Off, LOG_SECTOR_ID_INFO);
  }
  return r;
}

/*********************************************************************
*
*       _LowLevelFormat
*
*  Function description
*    Prepares the NOR flash device for the operation.
*
*  Parameters
*    pInst        Driver instance.
*
*  Return value
*    ==0    OK, NOR flash device initialized successfully.
*    !=0    An error occurred.
*/
static int _LowLevelFormat(NOR_INST * pInst) {
  NOR_PSH        psh;
  U32            SecOffs;
  U32            SecLen;
  int            NumSectors;
  int            i;
  int            SectorShiftCnt;
  U8             SectorType;
  int            r;
  U8             FailSafeErase;
#if FS_NOR_OPTIMIZE_HEADER_WRITE
  NOR_DATA_RANGE pshDataRange;
#endif // FS_NOR_OPTIMIZE_HEADER_WRITE

  NumSectors = (int)pInst->NumPhySectors;
  FS_MEMSET(&psh, 0xFF, sizeof(NOR_PSH));
  INIT_PSH_DATA_RANGE();
  psh.EraseCnt      = 1;
  psh.FormatVersion = (U8)FORMAT_VERSION;
  psh.Signature     = _GetPhySectorSignature(pInst);
  UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_PSH, EraseCnt), sizeof(psh.EraseCnt));
  UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_PSH, FormatVersion), sizeof(psh.FormatVersion));
  UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_PSH, Signature), sizeof(psh.Signature));
  FailSafeErase = 0;
#if FS_NOR_SUPPORT_FAIL_SAFE_ERASE
  if (_IsRewriteSupported(pInst) != 0) {
    psh.FailSafeErase  = 0;      // Reversed logic: 0x00 -> supported, 0xFF -> not supported
    psh.EraseSignature = ERASE_SIGNATURE_VALID;
    UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_PSH, FailSafeErase), sizeof(psh.FailSafeErase));
    UPDATE_PSH_DATA_RANGE(OFFSET_OF_MEMBER(NOR_PSH, EraseSignature), sizeof(psh.EraseSignature));
    FailSafeErase = 1;
  }
#endif // FS_NOR_SUPPORT_FAIL_SAFE_ERASE
  //
  // We save here the range of bytes that have to be updated
  // and restore this value a the beginning of the loop that
  // initializes the physical sectors.
  //
  SAVE_PSH_DATA_RANGE(&pshDataRange);
  //
  // Initialize the sector info: No work sector so far.
  //
  r = _InitStatus(pInst);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR: _LowLevelFormat: Failed to initialize status."));
    return 1;                                         // Error, could not initialize status.
  }
  pInst->Status.FailSafeErase = FailSafeErase;
  //
  // For each sector: erase and write physical header at beginning of the sector
  //
  for (i = 0; i < NumSectors; i++) {
    if (_IsPhySectorEraseRequired(pInst, (U32)i) != 0) {
      //
      // Erase the sector
      //
      r = _ErasePhySectorFailSafe(pInst, (U32)i, ERASE_CNT_INVALID);
      if (r != 0) {
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR: _LowLevelFormat: Failed to erase sector: %d.", i));
        return 1;                                         // Error, could not erase phy. sector.
      }
    }
    //
    // Retrieve sector info
    //
    _GetSectorInfo(pInst, (unsigned int)i, &SecOffs, &SecLen);
    SectorShiftCnt = _SectorSize2ShiftCnt(SecLen);
    if (SectorShiftCnt < 0) {
      return -1;
    }
    RESTORE_PSH_DATA_RANGE(&pshDataRange);
    if (pInst->Status.aWorkIndex[SectorShiftCnt] == PSI_INVALID) {
      pInst->Status.aWorkIndex[SectorShiftCnt] = i;     // Remember it as work sector
      SectorType = PHY_SECTOR_TYPE_WORK;                // Work type, can later be converted to data
      _SetPhySectorType(pInst, &psh, SectorType);
      r = _WritePSH(pInst, SecOffs, &psh);
    } else {
      SectorType = PHY_SECTOR_TYPE_DATA;                // Sector will be used for data
      _SetPhySectorType(pInst, &psh, SectorType);
      r = _WritePSH(pInst, SecOffs, &psh);
    }
    if (r != 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR: _LowLevelFormat: Failed to write to physical header @: 0x%x.", SecOffs));
      return -1;
    }
  }
  //
  // Write the info sector
  //
  r = _WriteInfoSector(pInst);
  if (r == 0) {
    //
    // Invalidate all the instance information.
    //
    r = _InitStatus(pInst);
    if (r == 0) {
      //
      // Mount the NOR flash device.
      //
      r = _LowLevelMount(pInst);
      if (r == 0) {
#if FS_NOR_SUPPORT_CLEAN
        pInst->IsClean = 1;
#endif // FS_NOR_SUPPORT_CLEAN
#if FS_NOR_OPTIMIZE_DIRTY_CHECK
        _MarkAllPhySectorsAsClean(pInst);
#endif // FS_NOR_OPTIMIZE_DIRTY_CHECK
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _LowLevelMountIfRequired
*
*  Function description
*    LL-Mounts the device if it is not LLMounted and this has not already been
*    tried in vain.
*
*  Return value
*    ==0    O.K., device has been successfully mounted and is accessible
*    !=0    Error
*/
static int _LowLevelMountIfRequired(NOR_INST * pInst) {
  if (pInst->Status.IsLLMounted != 0u) {
    return 0;                   // O.K., is mounted
  }
  if (pInst->Status.LLMountFailed != 0u) {
    return 1;                   // Error, we could not mount it and do not want to try again
  }
  (void)_LowLevelMount(pInst);
  if (pInst->Status.IsLLMounted == 0u) {
    pInst->Status.LLMountFailed = 1;
  }
  if (pInst->Status.IsLLMounted == 0u) {
    return 1;
  }
  return 0;
}

/*********************************************************************
*
*       _InitIfRequired
*
*  Function description
*    Initializes the driver instance.
*
*  Parameters
*    pInst    Driver instance.
*
*  Return value
*    ==0      OK, driver instance initialized.
*    !=0      An error occurred.
*/
static int _InitIfRequired(NOR_INST * pInst) {
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
static NOR_INST * _AllocInstIfRequired(U8 Unit) {
  NOR_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  pInst = NULL;
  if (Unit < (U8)FS_NOR_NUM_UNITS) {
    pInst = _apInst[Unit];
    if (pInst == NULL) {
      pInst = SEGGER_PTR2PTR(NOR_INST, FS_ALLOC_ZEROED((I32)sizeof(NOR_INST), "NOR_INST"));
      if (pInst != NULL) {
        _apInst[Unit]                = pInst;
        pInst->Unit                  = Unit;
        pInst->pctLogSectorsReserved = PCT_LOG_SECTORS_RESERVED;
#if FS_NOR_SKIP_BLANK_SECTORS
        pInst->SkipBlankSectors      = 1;
#endif
#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
        pInst->ldBytesPerLine        = (U8)_ld(FS_NOR_LINE_SIZE);
        pInst->IsRewriteSupported    = FS_NOR_CAN_REWRITE;
        pInst->SizeOfPSH             = (U8)sizeof(NOR_PSH);
        pInst->SizeOfLSH             = (U8)sizeof(NOR_LSH);
#endif
#if FS_NOR_SUPPORT_LEGACY_MODE
        pInst->IsLegacyModeSupported = 1;
#endif
#if FS_NOR_OPTIMIZE_DIRTY_CHECK
        pInst->IsDirtyCheckOptimized = 1;
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
*    Returns a driver instance by unit number.
*/
static NOR_INST * _GetInst(U8 Unit) {
  NOR_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  pInst = NULL;
  if (Unit < (U8)FS_NOR_NUM_UNITS) {
    pInst = _apInst[Unit];
  }
  return pInst;
}

/*********************************************************************
*
*       _WriteOneSector
*/
static int _WriteOneSector(NOR_INST * pInst, U32 LogSectorIndex, const U8 * pBuffer) {
  U32      OffNew;
  U32      OffOld;
  unsigned SizeOfLSH;
  int      r;

  if (pInst->Status.HasError != 0u) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR: _WriteOneSector: Write ignored."));
    return 1;                   // Error, driver encountered a permanent error.
  }
  //
  // Find a free log. sector first.
  //
  OffNew = _FindFreeLogSector(pInst);
  if (OffNew == 0u) {
    return 1;                   // Error, could not get a free logical sector.
  }
  OffOld = _FindLogSector(pInst, LogSectorIndex);  // Check if sector already exists.
  //
  // Write sector data.
  //
  SizeOfLSH = _SizeOfLSH(pInst);
  r = _WriteLogSectorData(pInst, OffNew + SizeOfLSH, pBuffer, pInst->SectorSize);
  if (r != 0) {
    return r;                   // Error, could not write logical sector data.
  }
  //
  // Write log. sector header.
  //
  r = _WriteLogSectorInfo(pInst, OffNew, LogSectorIndex);
  if (r != 0) {
    return r;                   // Error, could not update the header of the logical sector.
  }
  //
  // Clear the old sector.
  //
  if (OffOld != 0u) {
    //
    // TestPoint: Set break and RESET here.
    //
    CALL_TEST_HOOK_FAIL_SAFE(pInst->Unit);

    (void)_MarkLogSectorAsInvalid(pInst, OffOld);
  }
  //
  // Update L2P table.
  //
  (void)_WriteL2PEntry(pInst, LogSectorIndex, OffNew);
  r = _WearLevel(pInst);
  return r;
}

#if FS_NOR_SUPPORT_CLEAN

/*********************************************************************
*
*       _CleanPhySector
*
*  Function description
*    Converts the erasable logical sectors by copying the valid logical sectors to work sector.
*/
static int _CleanPhySector(NOR_INST * pInst, U32 psiSrc) {
  int psiDest;
  U32 SectorLen;
  int r;

  SectorLen = 0;
  _GetSectorInfo(pInst, psiSrc, NULL, &SectorLen);
  psiDest = _GetWorkSectorIndex(pInst, SectorLen);
  //
  // Invalidate the old work sector.
  //
  _SetWorkSectorIndex(pInst, SectorLen, PSI_INVALID);
  //
  // Copy the valid logical sector to work sector.
  //
  r = _CopyDataSector(pInst, (U32)psiDest, psiSrc);
  if (r == 0) {
    //
    // Perform the wear leveling.
    //
    r = _WearLevel(pInst);
  }
  return r;
}

/*********************************************************************
*
*       _IsPhySectorCleanable
*
*  Function description
*    Checks whether a physical sector can be cleaned which means that
*    the following conditions must be true:
*      - physical sector contains no free logical sectors
*      - physical sector contains at least one erasable sector
*
*  Return value
*    ==0   Physical sector is not cleanable.
*    ==1   Physical sector can be cleaned.
*/
static int _IsPhySectorCleanable(NOR_INST * pInst, U32 PhySectorIndex, const NOR_PSH * pPSH) {
  U32      SectorOff;
  U32      SectorLen;
  int      r;
  unsigned LogSectorSize;
  U8       PhySectorType;
  int      NumErasableSectors;
  U32      LogSectorIndex;
  unsigned SizeOfLSH;
  unsigned SizeOfPSH;

  r = 0;      // Physical sector can not be cleaned
  _GetSectorInfo(pInst, PhySectorIndex, &SectorOff, &SectorLen);
  PhySectorType = _GetPhySectorType(pInst, pPSH);
  if (PhySectorType == PHY_SECTOR_TYPE_DATA) {
    NumErasableSectors = 0;
    SizeOfLSH      = _SizeOfLSH(pInst);
    SizeOfPSH      = _SizeOfPSH(pInst);
    SectorLen     -= SizeOfPSH;
    SectorOff     += SizeOfPSH;
    LogSectorSize  = SizeOfLSH + pInst->SectorSize;
    //
    // Check the index of all the logical sectors.
    //
    while (SectorLen >= LogSectorSize) {
      LogSectorIndex = _GetLogSectorIndex(pInst, SectorOff, pPSH->Signature, NULL);
      if (LogSectorIndex == LOG_SECTOR_ID_BLANK) {
#if FS_NOR_OPTIMIZE_DIRTY_CHECK
        if (pInst->IsDirtyCheckOptimized != 0u) {
          if (_IsLogSectorBlank(pInst, SectorOff) == 0) {
            ++NumErasableSectors;
            break;                  // We found a logical sector marked as blank in the header than contains non-blank data. The phy. sector has to be cleaned.
          }
        }
#endif // FS_NOR_OPTIMIZE_DIRTY_CHECK
        NumErasableSectors = 0;
        break;                      // Free logical sector found. Physical sector can not be cleaned.
      }
      if (LogSectorIndex == LOG_SECTOR_ID_ERASABLE) {
        ++NumErasableSectors;
      }
      SectorLen  -= LogSectorSize;
      SectorOff  += LogSectorSize;
    }
    if (NumErasableSectors != 0) {
      r = 1;                        // At least one erasable logical sector found. Physical sector can be cleaned.
    }
#if FS_NOR_OPTIMIZE_DIRTY_CHECK
    else {
      _MarkPhySectorAsClean(pInst, PhySectorIndex);
    }
#endif
  }
  return r;
}

/*********************************************************************
*
*       _FindPhySectorToClean
*
*  Function description
*    Searches for a physical sector which contains no free sectors.
*
*  Return value
*    >=0    Index of the physical sector found.
*    < 0    No physical sector found.
*/
static int _FindPhySectorToClean(NOR_INST * pInst) {
  U32     iSector;
  U32     NumPhySectors;
  U8      PhySectorType;
  int     r;
  NOR_PSH psh;
  U32     SectorOff;
  int     Result;

  r = -1;       // No suitable physical sector found yet.
  //
  // Iterate through all data physical sectors and check for free sectors.
  //
  NumPhySectors = pInst->NumPhySectors;
  for (iSector = 0; iSector < NumPhySectors; ++iSector) {
    _GetSectorInfo(pInst, iSector, &SectorOff, NULL);
    Result = _ReadPSH(pInst, SectorOff, &psh);
    if (Result == 0) {
      PhySectorType = _GetPhySectorType(pInst, &psh);
      if (PhySectorType == PHY_SECTOR_TYPE_DATA) {
        if (_IsPhySectorCleanable(pInst, iSector, &psh) != 0) {
          r = (int)iSector;
          break;
        }
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _CleanOne
*
*  Function description
*    Performs garbage collection on the NOR flash device.
*
*  Parameters
*    pInst      Driver instance.
*    pMore      [OUT] Indicates if all sectors have been cleaned.
*               * ==0   Nothing else to "clean".
*               * ==1   At least one more "clean" job left to do.
*
*  Return value
*    ==0    OK
*    !=0    An error occurred
*
*  Additional information
*    This function relocates the contents of no more than one physical
*    sector that contain erasable logical sectors in oder to create writable
*    (blank) logical sectors.
*    The caller is informed via the return value if more operations are required
*    to clean the storage completely.
*/
static int _CleanOne(NOR_INST * pInst, int * pMore) {
  I32 PhySectorIndex;
  int r;
  int More;

  r    = 0;             // Nothing more to clean.
  More = 0;
  if (pInst->IsClean == 0u) {
    //
    // First, process the invalid sectors.
    //
    PhySectorIndex = _FindInvalidSector(pInst);
    if (PhySectorIndex != PSI_INVALID) {
      r = _CreateDataSector(pInst, (U32)PhySectorIndex);
      PhySectorIndex = _FindInvalidSector(pInst);
      if (PhySectorIndex >= 0) {
        More = 1;       // One more invalid sectors found.
      } else {
        PhySectorIndex = _FindPhySectorToClean(pInst);
        if (PhySectorIndex >= 0) {
          More = 1;     // One more data sector with cleanable logical sectors found.
        }
      }
    } else {
      PhySectorIndex = _FindPhySectorToClean(pInst);
      if (PhySectorIndex >= 0) {
        r = _CleanPhySector(pInst, (U32)PhySectorIndex);
        PhySectorIndex = _FindPhySectorToClean(pInst);
        if (PhySectorIndex >= 0) {
          More = 1;     // One more data sector with cleanable logical sectors found.
        }
      }
    }
    if (r == 0) {
      if (More == 0) {
        pInst->IsClean = 1;
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
*    Performs garbage collection on the NOR flash device.
*
*  Additional information
*    This function relocates the contents of all the physical sectors
*    that contain erasable logical sectors to other free physical sectors
*    in order to create free (writable) logical sectors.
*/
static int _Clean(NOR_INST * pInst) {
  U32     iPhySector;
  U32     NumPhySectors;
  U8      PhySectorType;
  NOR_PSH psh;
  I32     WorkSectorIndex;
  U32     SectorOff;
  U32     SectorLen;
  int     r;
  int     Result;
  int     IsClean;

  IsClean = (int)pInst->IsClean;
#if FS_NOR_OPTIMIZE_DIRTY_CHECK
  if (IsClean != 0) {
    if (_IsAnyPhySectorDirty(pInst) != 0) {
      IsClean = 0;
    }
  }
#endif // FS_NOR_OPTIMIZE_DIRTY_CHECK
  if (IsClean != 0) {
    return 0;                       // Nothing to do. The NOR flash is clean.
  }
  r = 0;                            // Set to indicate success.
  //
  // Iterate through all physical sectors and process them.
  //
  NumPhySectors = pInst->NumPhySectors;
  for (iPhySector = 0; iPhySector < NumPhySectors; ++iPhySector) {
    _GetSectorInfo(pInst, iPhySector, &SectorOff, &SectorLen);
    FS_MEMSET(&psh, 0, sizeof(psh));
    (void)_ReadPSH(pInst, SectorOff, &psh);
    PhySectorType = _GetPhySectorType(pInst, &psh);
    if (PhySectorType == PHY_SECTOR_TYPE_DATA) {
      //
      // We have to process only the physical sectors that do not contain any free logical sectors.
      //
      if (_IsPhySectorCleanable(pInst, iPhySector, &psh) != 0) {
        Result = _CleanPhySector(pInst, iPhySector);
        if (Result != 0) {
          r = 1;                    // Error, could not clean physical sector.
        }
      }
    } else {
      //
      // Any other physical sector, with the exception of the work sector, is converted to a data sector.
      //
      WorkSectorIndex = _GetWorkSectorIndex(pInst, SectorLen);
      if (WorkSectorIndex != (I32)iPhySector) {
        Result = _CreateDataSector(pInst, iPhySector);
        if (Result != 0) {
          r = 1;                    // Error, could not clean physical sector.
        }
      }
    }
  }
  if (r == 0) {
    pInst->IsClean = 1;
  }
  return r;
}

/*********************************************************************
*
*       _GetCleanCnt
*
*  Function description
*    Returns the number of clean operations that are required to be
*    performed in order to completely clean the NOR flash device.
*/
static U32 _GetCleanCnt(NOR_INST * pInst) {
  U32      iPhySector;
  U32      NumPhySectors;
  U8       PhySectorType;
  NOR_PSH  psh;
  I32      WorkSectorIndex;
  U32      SectorOff;
  U32      SectorLen;
  unsigned CleanCntTotal;

  CleanCntTotal = 0;
  //
  // Iterate through all physical sectors and count how many have to be cleaned.
  //
  NumPhySectors = pInst->NumPhySectors;
  for (iPhySector = 0; iPhySector < NumPhySectors; ++iPhySector) {
    _GetSectorInfo(pInst, iPhySector, &SectorOff, &SectorLen);
    FS_MEMSET(&psh, 0, sizeof(psh));
    (void)_ReadPSH(pInst, SectorOff, &psh);
    PhySectorType = _GetPhySectorType(pInst, &psh);
    if (PhySectorType == PHY_SECTOR_TYPE_DATA) {
      //
      // We have to clean only the physical sectors that do not contain any free logical sectors.
      //
      if (_IsPhySectorCleanable(pInst, iPhySector, &psh) != 0) {
        ++CleanCntTotal;
      }
    } else {
      //
      // Any other physical sector, with the exception of the work sector, has to be converted to a data sector.
      //
      WorkSectorIndex = _GetWorkSectorIndex(pInst, SectorLen);
      if (WorkSectorIndex != (I32)iPhySector) {
        ++CleanCntTotal;
      }
    }
  }
  return CleanCntTotal;
}

#endif // FS_NOR_SUPPORT_CLEAN

#if (FS_NOR_SUPPORT_CLEAN != 0) || (FS_SUPPORT_FREE_SECTOR != 0)

/*********************************************************************
*
*        _GetSectorUsage
*
*  Function description
*    Checks if a logical sector contains valid data.
*
*  Parameters
*    pInst            Driver instance.
*    LogSectorIndex   Index of the logical sector to be checked.
*
*  Return value
*    ==FS_SECTOR_IN_USE         Sector in use (contains valid data).
*    ==FS_SECTOR_NOT_USED       Sector not in use (was not written nor was invalidated).
*    ==FS_SECTOR_USAGE_UNKNOWN  Information about sector usage is not available.
*/
static int _GetSectorUsage(const NOR_INST * pInst, U32 LogSectorIndex) {
  U32 Off;
  int r;

  r   = FS_SECTOR_NOT_USED;
  Off = _FindLogSector(pInst, LogSectorIndex);
  if (Off != 0u) {
    r = FS_SECTOR_IN_USE;
  }
  return r;
}

#endif // (FS_NOR_SUPPORT_CLEAN != 0) || (FS_SUPPORT_FREE_SECTOR != 0)

/*********************************************************************
*
*       Static code (public via callback)
*
**********************************************************************
*/

/*********************************************************************
*
*       _NOR_GetDriverName
*
*  Function description
*    FS driver function. Returns the string which identifies the driver.
*/
static const char * _NOR_GetDriverName(U8 Unit) {
  FS_USE_PARA(Unit);
  return "nor";
}

/*********************************************************************
*
*       _NOR_AddDevice
*
*  Function description
*    FS driver function. Initializes the low-level driver object.
*
*  Return value
*    >=0      Device allocated.
*    < 0      Error, could not add device.
*/
static int _NOR_AddDevice(void) {
  NOR_INST * pInst;

  if (_NumUnits >= (U8)FS_NOR_NUM_UNITS) {
    return -1;                // Error, too many instances.
  }
  pInst = _AllocInstIfRequired(_NumUnits);
  if (pInst == NULL) {
    return -1;                // Error, could not create driver instance.
  }
  return (int)_NumUnits++;
}

/*********************************************************************
*
*       _NOR_Read
*
*  Function description
*    FS driver function. Reads the contents of one logical sector to given buffer space.
*
*  Parameters
*    Unit           Device number.
*    SectorIndex    Number of the logical sector to be read.
*    pData          [OUT] Contents of logical sector.
*    NumSectors     Number of sectors to be read.
*
*  Return value
*    ==0    Sector data read.
*    !=0    An error has occurred.
*/
static int _NOR_Read(U8 Unit, U32 SectorIndex, void * pData, U32 NumSectors) {
  U32        Off;
  NOR_INST * pInst;
  U8       * pData8;
  int        r;
  unsigned   SizeOfLSH;
  U32        NumSectorsTotal;
  unsigned   BytesPerSector;

  pInst  = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;                     // Error, instance not found.
  }
  //
  // Validate the sector range.
  //
  NumSectorsTotal = pInst->NumLogSectors;
  if ((SectorIndex >= NumSectorsTotal) || ((SectorIndex + NumSectors) > NumSectorsTotal)) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR: _NOR_Read: Invalid sector range ([%d, %d] not in [0, %d]).", (int)SectorIndex, (int)SectorIndex + (int)NumSectors - 1, (int)NumSectorsTotal - 1));
    return 1;
  }
  //
  // Mount the NOR flash device if required.
  //
  r = _LowLevelMountIfRequired(pInst);
  if (r != 0) {
    return r;                     // Error, could not mount NOR flash device.
  }
  r              = 0;              // Set to indicate success.
  SizeOfLSH      = _SizeOfLSH(pInst);
  BytesPerSector = pInst->SectorSize;
  pData8         = SEGGER_PTR2PTR(U8, pData);
  do {
    Off = _FindLogSector(pInst, SectorIndex);
    if (Off != 0u) {
      r = _ReadLogSectorData(pInst, Off + SizeOfLSH, pData8, BytesPerSector);
      if (r != 0) {
        break;
      }
    } else {
      FS_MEMSET(pData8, FS_NOR_READ_BUFFER_FILL_PATTERN, BytesPerSector);
    }
    IF_STATS(pInst->StatCounters.ReadSectorCnt++);
    pData8 += BytesPerSector;
    SectorIndex++;
  } while (--NumSectors != 0u);
  return r;
}

/*********************************************************************
*
*       _NOR_Write
*
*  Function description
*    FS driver function. Writes one or more logical sectors to storage device.
*
*  Return value
*    ==0    Data successfully written.
*    !=0    An error has occurred.
*/
static int _NOR_Write(U8 Unit, U32 SectorIndex, const void * pData, U32 NumSectors, U8 RepeatSame) {
  int        r;
  const U8 * pData8;
  NOR_INST * pInst;
  U32        NumSectorsTotal;
  unsigned   BytesPerSector;

  pInst  = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;                     // Error, instance not found.
  }
  //
  // Validate the sector range.
  //
  NumSectorsTotal = pInst->NumLogSectors;
  if ((SectorIndex >= NumSectorsTotal) || ((SectorIndex + NumSectors) > NumSectorsTotal)) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR: _NOR_Write: Invalid sector range ([%d, %d] not in [0, %d]).", (int)SectorIndex, (int)SectorIndex + (int)NumSectors - 1, (int)NumSectorsTotal - 1));
    return 1;
  }
  r = _LowLevelMountIfRequired(pInst);
  if (r != 0) {
    return r;                     // Error, could not mount NOR flash device.
  }
  pData8         = SEGGER_CONSTPTR2PTR(const U8, pData);
  BytesPerSector = pInst->SectorSize;
  do {
    r = _WriteOneSector(pInst, SectorIndex++, pData8);
    if (r != 0) {
      break;
    }
    IF_STATS(pInst->StatCounters.WriteSectorCnt++);
    if (RepeatSame == 0u) {
      pData8 += BytesPerSector;
    }
  } while (--NumSectors != 0u);
  return r;
}

/*********************************************************************
*
*       _NOR_IoCtl
*
*  Function description
*    FS driver function. Device IO control interface function.
*
*  Parameters
*    Unit       Device index number.
*    Cmd        Command to be executed.
*    Aux        Parameter for the command.
*    pBuffer    Pointer to a buffer holding add. parameters.
*
*  Return value
*    ==0    Command successfully executed
*    !=0    An error has occurred.
*/
static int _NOR_IoCtl(U8 Unit, I32 Cmd, I32 Aux, void * pBuffer) {
  FS_DEV_INFO * pInfo;
  NOR_INST    * pInst;
  int           r;
  int           Result;

  FS_USE_PARA(Aux);
  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return -1;            // Error, instance not found.
  }
  r = -1;                 // Set to indicate error.
  switch (Cmd) {
  case FS_CMD_GET_DEVINFO:
    if (pBuffer != NULL) {
      Result = _LowLevelMountIfRequired(pInst);
      if (Result == 0) {
        pInfo = SEGGER_PTR2PTR(FS_DEV_INFO, pBuffer);
        pInfo->NumSectors     = pInst->NumLogSectors - NUM_PHY_SECTORS_RESERVED;
        pInfo->BytesPerSector = pInst->SectorSize;
        r = 0;
      }
    }
    break;
  case FS_CMD_FORMAT_LOW_LEVEL:
    Result = _LowLevelFormat(pInst);
    if (Result == 0) {
      r = 0;
    }
    break;
#if FS_SUPPORT_FREE_SECTOR
  case FS_CMD_FREE_SECTORS:
    if (pBuffer != NULL) {
      U32 SectorIndex;
      U32 NumSectors;

      SectorIndex = (U32)Aux;
      NumSectors  = *SEGGER_PTR2PTR(U32, pBuffer);
      _FreeSectors(pInst, SectorIndex, NumSectors);
      r = 0;
    }
    break;
#endif // FS_SUPPORT_FREE_SECTOR
  case FS_CMD_REQUIRES_FORMAT:
    r      = 1;       // Set to indicate that the NOR flash device is not formatted.
    Result = _LowLevelMountIfRequired(pInst);
    if (Result == 0) {
      r = 0;
    }
    break;
#if FS_NOR_SUPPORT_CLEAN
  case FS_CMD_CLEAN_ONE:
    Result = _LowLevelMountIfRequired(pInst);
    if (Result == 0) {
      int   More;
      int * pMore;

      Result = _CleanOne(pInst, &More);
      if (Result == 0) {
        pMore = SEGGER_PTR2PTR(int, pBuffer);
        if (pMore != NULL) {
          *pMore = More;
        }
        r = 0;
      }
    }
    break;
  case FS_CMD_CLEAN:
    Result = _LowLevelMountIfRequired(pInst);
    if (Result == 0) {
      Result = _Clean(pInst);
      if (Result == 0) {
        r = 0;
      }
    }
    break;
  case FS_CMD_GET_CLEAN_CNT:
    Result = _LowLevelMountIfRequired(pInst);
    if (Result == 0) {
      U32   Cnt;
      U32 * pCnt;

      Cnt = _GetCleanCnt(pInst);
      pCnt = SEGGER_PTR2PTR(U32, pBuffer);
      if (pCnt != NULL) {
        *pCnt = Cnt;
      }
      r = 0;
    }
    break;
#endif // FS_NOR_SUPPORT_CLEAN
#if (FS_NOR_SUPPORT_CLEAN != 0) || (FS_SUPPORT_FREE_SECTOR != 0)
  case FS_CMD_GET_SECTOR_USAGE:
    if (pBuffer != NULL) {
      Result = _LowLevelMountIfRequired(pInst);
      if (Result == 0) {
        int * pSectorUsage;

        pSectorUsage  = SEGGER_PTR2PTR(int, pBuffer);
        *pSectorUsage = _GetSectorUsage(pInst, (U32)Aux);
        r = 0;
      }
    }
    break;
#endif // (FS_NOR_SUPPORT_CLEAN != 0) || (FS_SUPPORT_FREE_SECTOR != 0)
#if FS_SUPPORT_DEINIT
  case FS_CMD_DEINIT:
    //
    // Deinitialize the driver
    //
    if (pInst->pPhyType->pfDeInit != NULL) {
      pInst->pPhyType->pfDeInit(Unit);
    }
    FS_FREE(pInst->pL2P);
    FS_FREE(pInst);
    _apInst[Unit] = NULL;
    _NumUnits--;
    r = 0;
    break;
#endif // FS_SUPPORT_DEINIT
  case FS_CMD_UNMOUNT:
    //lint through
  case FS_CMD_UNMOUNT_FORCED:
    pInst->IsInited = 0;
    (void)_InitStatus(pInst);
    break;
  default:
    //
    // Command not supported.
    //
    break;
  }
  return r;
}

/*********************************************************************
*
*       _NOR_InitMedium
*
*  Function description
*    FS driver function. Initializes and identifies the storage device.
*
*  Return value
*    ==0    Device okay and ready for operation
*    !=0    An error has occurred.
*/
static int _NOR_InitMedium(U8 Unit) {
  int        r;
  NOR_INST * pInst;

  r     = 1;                  // Set to indicate error.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    ASSERT_PHY_TYPE_IS_SET(pInst);
    if (pInst->pPhyType != NULL) {
      r = _ReadApplyDeviceParas(pInst);
    }
  }
  return r;
}

/*********************************************************************
*
*       _NOR_GetStatus
*
*  Function description
*    FS driver function. Checks device status and returns it.
*/
static int _NOR_GetStatus(U8 Unit) {
  FS_USE_PARA(Unit);
  return FS_MEDIA_IS_PRESENT;
}

/*********************************************************************
*
*       _NOR_GetNumUnits
*
*  Function description
*    FS driver function. Returns the number of allocated driver instances.
*/
static int _NOR_GetNumUnits(void) {
  return (int)_NumUnits;
}

/*********************************************************************
*
*       Public const data
*
**********************************************************************
*/

/*********************************************************************
*
*       Driver API table
*/
const FS_DEVICE_TYPE FS_NOR_Driver = {
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
*       FS__NOR_SetTestHookFailSafe
*/
void FS__NOR_SetTestHookFailSafe(FS_NOR_TEST_HOOK_NOTIFICATION * pfTestHook) {
  _pfTestHookFailSafe = pfTestHook;
}

/*********************************************************************
*
*       FS__NOR_SetTestHookDataReadBegin
*/
void FS__NOR_SetTestHookDataReadBegin(FS_NOR_TEST_HOOK_DATA_READ_BEGIN * pfTestHook) {
  _pfTestHookDataReadBegin = pfTestHook;
}

/*********************************************************************
*
*       FS__NOR_SetTestHookDataReadEnd
*/
void FS__NOR_SetTestHookDataReadEnd(FS_NOR_TEST_HOOK_DATA_READ_END * pfTestHook) {
  _pfTestHookDataReadEnd = pfTestHook;
}

/*********************************************************************
*
*       FS__NOR_SetTestHookDataWriteBegin
*/
void FS__NOR_SetTestHookDataWriteBegin(FS_NOR_TEST_HOOK_DATA_WRITE_BEGIN * pfTestHook) {
  _pfTestHookDataWriteBegin = pfTestHook;
}

/*********************************************************************
*
*       FS__NOR_SetTestHookDataWriteEnd
*/
void FS__NOR_SetTestHookDataWriteEnd(FS_NOR_TEST_HOOK_DATA_WRITE_END * pfTestHook) {
  _pfTestHookDataWriteEnd = pfTestHook;
}

/*********************************************************************
*
*       FS__NOR_SetTestHookSectorErase
*/
void FS__NOR_SetTestHookSectorErase(FS_NOR_TEST_HOOK_SECTOR_ERASE * pfTestHook) {
  _pfTestHookSectorErase = pfTestHook;
}

#endif // FS_SUPPORT_TEST

/*********************************************************************
*
*       FS__NOR_GetLSHInfo
*/
void FS__NOR_GetLSHInfo(U8 Unit, FS_NOR_LSH_INFO * pLSHInfo) {
  unsigned SizeOfLSH;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  {
    NOR_INST * pInst;

    SizeOfLSH = 0;
    pInst = _GetInst(Unit);
    if (pInst != NULL) {
      SizeOfLSH = pInst->SizeOfLSH;
    }
  }
#else
  FS_USE_PARA(Unit);
  SizeOfLSH = sizeof(NOR_LSH);
#endif // FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  pLSHInfo->NumBytes = (U8)SizeOfLSH;
}

/*********************************************************************
*
*       FS__NOR_GetPSHInfo
*/
void FS__NOR_GetPSHInfo(U8 Unit, FS_NOR_PSH_INFO * pPSHInfo) {
  unsigned SizeOfPSH;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  {
    NOR_INST * pInst;

    SizeOfPSH = 0;
    pInst = _GetInst(Unit);
    if (pInst != NULL) {
      SizeOfPSH = pInst->SizeOfPSH;
    }
  }
#else
  FS_USE_PARA(Unit);
  SizeOfPSH = sizeof(NOR_PSH);
#endif // FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
  pPSHInfo->NumBytes = (U8)SizeOfPSH;
}

/*********************************************************************
*
*       FS__NOR_IsRewriteSupported
*/
U8 FS__NOR_IsRewriteSupported(U8 Unit) {
  U8 r;

  r = 1;          // Set to indicate that the NOR flash device can rewrite.
#if (FS_NOR_CAN_REWRITE == 0)
  {
    NOR_INST * pInst;

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
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_NOR_Configure
*
*  Function description
*    Configures an instance of the sector map NOR driver
*
*  Parameters
*    Unit           Index of the sector map NOR driver (0-based).
*    BaseAddr       Address of the first byte in NOR flash.
*    StartAddr      Address of the first byte the NOR driver is permitted
*                   to use as storage.
*    NumBytes       Number of bytes starting from StartAddr available to
*                   be used by the NOR driver as storage.
*
*  Additional information
*    This function is mandatory and it has to be called once in FS_X_AddDevices()
*    for each instance of the sector map NOR driver created by the application.
*    Different instances of the NOR driver are identified by the Unit parameter.
*
*    BaseAddr is used only for NOR flash devices that are memory mapped.
*    For serial NOR flash devices that are not memory mapped BaseAddr
*    has to be set to 0.
*
*    StartAddr has to be greater than or equal to BaseAddr and smaller
*    than the total number of bytes in the NOR flash device. The sector map
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
*    The sector map NOR driver can work with physical sectors of different size.
*    It is required that at least two physical sectors of each sector size are
*    available.
*/
void FS_NOR_Configure(U8 Unit, U32 BaseAddr, U32 StartAddr, U32 NumBytes) {
  NOR_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    ASSERT_PHY_TYPE_IS_SET(pInst);
    if (pInst->pPhyType != NULL) {
      pInst->pPhyType->pfConfigure(Unit, BaseAddr, StartAddr, NumBytes);
    }
  }
}

/*********************************************************************
*
*       FS_NOR_ConfigureReserve
*
*  Function description
*    Configures the number of logical sectors to be reserved.
*
*  Parameters
*    Unit           Index of the sector map NOR driver (0-based).
*    pctToReserve   Percent of the total number of logical sectors to reserve.
*
*  Additional information
*    This function is optional. By default, the sector map NOR driver
*    reserves about 10% of the total number of logical sector for
*    future improvements and extensions. FS_NOR_ConfigureReserve()
*    can be used in an application to modify this value. If set to 0
*    the sector map NOR driver uses all the available logical sectors
*    to store file system data.
*
*    The application has to reformat the NOR flash device in order
*    for the modified value to take effect.
*/
void FS_NOR_ConfigureReserve(U8 Unit, U8 pctToReserve) {
  NOR_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->pctLogSectorsReserved = pctToReserve;
  }
}

/*********************************************************************
*
*       FS_NOR_SetPhyType
*
*  Function description
*    Configures the type of NOR physical layer.
*
*  Parameters
*    Unit           Index of the sector map NOR driver (0-based).
*    pPhyType       [IN] NOR physical layer.
*
*  Additional information
*    This function is mandatory and it has to be called once in FS_X_AddDevices()
*    for each instance of the sector map NOR driver created by the application.
*    Different instances of the sector map NOR driver are identified by the Unit parameter.
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
void FS_NOR_SetPhyType(U8 Unit, const FS_NOR_PHY_TYPE * pPhyType) {
  NOR_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    if (pPhyType != NULL) {
      pInst->pPhyType = pPhyType;
      pPhyType->pfOnSelectPhy(Unit);
    }
  }
}

/*********************************************************************
*
*       FS_NOR_GetDiskInfo
*
*   Function description
*     Returns information about the organization and the management
*     of the NOR flash device.
*
*  Parameters
*    Unit           Index of the sector map NOR driver (0-based).
*    pDiskInfo      [OUT] Requested information.
*
*  Return value
*    ==0 - OK, information returned.
*    !=0 - An error occurred.
*
*  Additional information
*    This function is not required for the functionality of the sector
*    map NOR driver and will typically not be linked in production builds.
*/
int FS_NOR_GetDiskInfo(U8 Unit, FS_NOR_DISK_INFO * pDiskInfo) {
  unsigned   i;
  U32        NumUsedSectors;
  NOR_INST * pInst;
  int        r;
  unsigned   BytesPerSector;

  if (pDiskInfo == NULL) {
    return 1;                             // Error, invalid parameter.
  }
  NumUsedSectors     = 0;
  BytesPerSector     = 0;
  FS_MEMSET(pDiskInfo, 0, sizeof(FS_NOR_DISK_INFO));
  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;       // Error, could not allocate driver instance.
  }
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return 1;                             // Error, could not initialize the NOR flash.
  }
  r = _LowLevelMountIfRequired(pInst);
  if (r == 0) {
    BytesPerSector = pInst->SectorSize;
    for (i = 0; i < pInst->NumLogSectors; i++) {
      U32 L2PEntry;

      L2PEntry = FS_BITFIELD_ReadEntry(pInst->pL2P, i, pInst->NumBitsUsed);
      if (L2PEntry != 0u) {
        NumUsedSectors++;
      }
    }
  }
  pDiskInfo->NumPhysSectors = pInst->NumPhySectors;
  pDiskInfo->NumLogSectors  = pInst->NumLogSectors - NUM_PHY_SECTORS_RESERVED;
  pDiskInfo->NumUsedSectors = NumUsedSectors;
  pDiskInfo->BytesPerSector = (U16)BytesPerSector;
  return r;
}

/*********************************************************************
*
*       FS_NOR_GetSectorInfo
*
*  Function description
*    Returns information about a specified physical sector.
*
*  Parameters
*    Unit           Index of the sector map NOR driver (0-based).
*    PhySectorIndex Index of the physical sector to be queried (0-based).
*    pSectorInfo    [OUT] Information related to the specified physical sector.
*
*  Additional information
*    This function is optional. The application can use it to get
*    information about the usage of a particular physical sector.
*
*    PhySectorIndex is relative to the beginning of the region configured
*    as storage via FS_NOR_Configure().
*
*  Return value
*    ==0      OK, information returned successfully.
*    !=0      An error occurred.
*/
int FS_NOR_GetSectorInfo(U8 Unit, U32 PhySectorIndex, FS_NOR_SECTOR_INFO * pSectorInfo) {
  int        r;
  U32        u;
  U32        SectorOff;
  U32        SectorSize;
  U16        NumEraseableSectors;
  U16        NumFreeSectors;
  U16        NumUsedSectors;
  U8         PhySectorType;
  U32        EraseCnt;
  unsigned   LogSectorSize;
  NOR_INST * pInst;
  NOR_PSH    psh;
  U32        LogSectorIndex;
  unsigned   SizeOfLSH;
  unsigned   SizeOfPSH;
  U8         Type;
  I32        psiWork;

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
  SectorOff           = 0;
  SectorSize          = 0;
  EraseCnt            = 0;
  NumEraseableSectors = 0;
  NumFreeSectors      = 0;
  NumUsedSectors      = 0;
  Type                = FS_NOR_SECTOR_TYPE_UNKNOWN;
  FS_MEMSET(pSectorInfo, 0, sizeof(FS_NOR_SECTOR_INFO));
  SizeOfLSH = _SizeOfLSH(pInst);
  SizeOfPSH = _SizeOfPSH(pInst);
  _GetSectorInfo(pInst, PhySectorIndex, &SectorOff, &SectorSize);
  r = _LowLevelMountIfRequired(pInst);
  if (r == 0) {
    FS_MEMSET(&psh, 0xFF, sizeof(psh));
    (void)_ReadPSH(pInst, SectorOff, &psh);
    EraseCnt      = psh.EraseCnt;
    LogSectorSize = SizeOfLSH + pInst->SectorSize;
    PhySectorType = _GetPhySectorType(pInst, &psh);
    switch (PhySectorType) {
    case PHY_SECTOR_TYPE_DATA:
      for (u = SectorOff + SizeOfPSH; u + LogSectorSize <= SectorOff + SectorSize; u += LogSectorSize) {
        LogSectorIndex = _GetLogSectorIndex(pInst, u, psh.Signature, NULL);
        switch (LogSectorIndex) {
        case LOG_SECTOR_ID_ERASABLE:
          NumEraseableSectors++;
          break;
        case LOG_SECTOR_ID_BLANK:
          NumFreeSectors++;
          break;
        default:
          NumUsedSectors++;
          break;
        }
      }
      Type = FS_NOR_SECTOR_TYPE_DATA;
      break;
    case PHY_SECTOR_TYPE_WORK:
      // through
    case PHY_SECTOR_TYPE_INVALID:
      Type = FS_NOR_SECTOR_TYPE_INVALID;
      psiWork = _GetWorkSectorIndex(pInst, SectorSize);
      if (psiWork != PSI_INVALID) {
        if ((U32)psiWork == PhySectorIndex) {
          Type = FS_NOR_SECTOR_TYPE_WORK;
        }
      }
      break;
    default:
      //
      // Invalid phy. sector type.
      //
      break;
    }
  }
  pSectorInfo->Off                 = SectorOff;
  pSectorInfo->Size                = SectorSize;
  pSectorInfo->EraseCnt            = EraseCnt;
  pSectorInfo->NumEraseableSectors = NumEraseableSectors;
  pSectorInfo->NumFreeSectors      = NumFreeSectors;
  pSectorInfo->NumUsedSectors      = NumUsedSectors;
  pSectorInfo->Type                = Type;
  return r;
}

/*********************************************************************
*
*       FS_NOR_LogSector2PhySectorAddr
*
*  Function description
*    Returns the address in memory of a specified logical sector.
*
*  Parameters
*    Unit             Index of the sector map NOR driver (0-based).
*    LogSectorIndex   Index of the logical sector for which the
*                     address has to be calculated.
*
*  Return value
*    !=NULL     OK, address of the first byte in the logical sector.
*    ==NULL     An error occurred.
*
*  Additional information
*    This function is optional. It can be used only with NOR flash
*    devices that are memory mapped. FS_NOR_LogSector2PhySectorAddr()
*    returns the address in the system memory of the first byte in the
*    specified logical sector.
*/
const void * FS_NOR_LogSector2PhySectorAddr(U8 Unit, U32 LogSectorIndex) {
  NOR_INST   * pInst;
  const void * pSectorData;
  unsigned     Addr;
  unsigned     SizeOfLSH;
  int          r;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return NULL;              // Error, could not allocate driver instance
  }
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return NULL;              // Error, could not initialize NOR flash. Return that it is not formatted.
  }
  r = _LowLevelMountIfRequired(pInst);
  if (r != 0) {
    return NULL;              // Error, could not mount NOR flash device.
  }
  if (LogSectorIndex >= pInst->NumLogSectors) {
    return NULL;              // Error, invalid logical sector index.
  }
  SizeOfLSH    = _SizeOfLSH(pInst);
  Addr         = _FindLogSector(pInst, LogSectorIndex);
  Addr        += SizeOfLSH;
  pSectorData  = SEGGER_ADDR2PTR(void, Addr);
  return pSectorData;
}

/*********************************************************************
*
*       FS_NOR_SetSectorSize
*
*   Function description
*     Configures the number of bytes in a logical sector.
*
*  Parameters
*    Unit           Index of the sector map NOR driver (0-based).
*    SectorSize     Number of bytes in a logical sector.
*
*  Additional information
*    This function is optional. It can be used to modify the size
*    of the logical sector used by the sector map NOR driver.
*    By default the sector map NOR driver uses the logical sector
*    size configured a file system level that is set to 512 bytes
*    at the file system initialization and can be later changed
*    via FS_SetMaxSectorSize(). The NOR flash device has to be
*    reformatted in order for the new logical sector size to take
*    effect.
*
*    SectorSize has to be a power of 2 value.
*/
void FS_NOR_SetSectorSize(U8 Unit, U16 SectorSize) {
  NOR_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->SectorSize = SectorSize;
  }
}

/*********************************************************************
*
*       FS_NOR_IsLLFormatted
*
*   Function description
*     Checks it the NOR flash is low-level formatted.
*
*  Parameters
*    Unit           Index of the sector map NOR driver (0-based).
*
*  Return value
*    !=0 - The NOR flash device is low-level formatted.
*    ==0 - The NOR flash device is not low-level formatted or an error has occurred.
*
*  Additional information
*    This function is optional. An application should use FS_IsLLFormatted() instead.
*/
int FS_NOR_IsLLFormatted(U8 Unit) {
  NOR_INST * pInst;
  int        r;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 0;         // Error, could not create driver instance.
  }
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return 0;         // Error, could not initialize NOR flash. Return that it is not formatted.
  }
  r = _LowLevelMountIfRequired(pInst);
  if (r != 0) {
    return 0;         // NOR flash not formatted.
  }
  return 1;           // NOR flash formatted.
}

/*********************************************************************
*
*       FS_NOR_FormatLow
*
*  Function description
*    Performs a low-level format of NOR flash device.
*
*  Parameters
*    Unit           Index of the sector map NOR driver (0-based).
*
*  Return value
*    ==0 - OK, NOR flash device has been successfully low-level formated.
*    !=0 - An error occurred.
*
*  Additional information
*    This function is optional. FS_NOR_FormatLow() erases the first
*    physical sector and stores the format information in it. The other
*    physical sectors are either erased or invalidated. Per default
*    the physical sectors are invalidated in order to reduce the time
*    it takes for the operation to complete.
*/
int FS_NOR_FormatLow(U8 Unit) {
  NOR_INST * pInst;
  int        r;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;       // Error, could not create driver instance.
  }
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return 1;       // Error, could not format NOR flash.
  }
  r = _LowLevelFormat(pInst);
  return r;
}

#if FS_NOR_ENABLE_STATS

/*********************************************************************
*
*       FS_NOR_GetStatCounters
*
*  Function description
*    Returns the values of the statistical counters.
*
*  Parameters
*    Unit           Index of the sector map NOR driver (0-based).
*    pStat          [OUT] Statistical counter values.
*
*  Additional information
*    This function is optional. The application can use it to get
*    the actual values of the statistical counters maintained by
*    the sector map NOR driver. The statistical counters provide
*    information about the number of internal operations performed
*    by the sector map NOR driver such as sector read and write.
*    All statistical counters are set to 0 when the NOR flash device
*    is low-level mounted. The application can explicitly set them
*    to 0 by using FS_NOR_ResetStatCounters(). A separate set of
*    statistical counters is maintained for each instance of the
*    sector map NOR driver.
*
*    The statistical counters are available only when the sector
*    map NOR driver is compiled with the FS_DEBUG_LEVEL configuration
*    define set to a value greater than or equal to FS_DEBUG_LEVEL_CHECK_ALL
*    or with the FS_NOR_ENABLE_STATS configuration define set to 1.
*/
void FS_NOR_GetStatCounters(U8 Unit, FS_NOR_STAT_COUNTERS * pStat) {
  NOR_INST * pInst;

  pInst  = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    if (pStat != NULL) {
      *pStat = pInst->StatCounters;
    }
  }
}

/*********************************************************************
*
*       FS_NOR_ResetStatCounters
*
*  Function description
*    Sets the value of the statistical counters to 0.
*
*  Parameters
*    Unit           Index of the sector map NOR driver (0-based).
*
*  Additional information
*    This function is optional. The application can use it to set
*    the statistical counters maintained by the sector map NOR driver
*    to 0. The statistical counters can be read via FS_NOR_GetStatCounters()
*
*    FS_NOR_ResetStatCounters() is available only when the sector
*    map NOR driver is compiled with the FS_DEBUG_LEVEL configuration
*    define set to a value greater than or equal to FS_DEBUG_LEVEL_CHECK_ALL
*    or with the FS_NOR_ENABLE_STATS configuration define set to 1.
*/
void FS_NOR_ResetStatCounters(U8 Unit) {
  NOR_INST             * pInst;
  FS_NOR_STAT_COUNTERS * pStat;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pStat = &pInst->StatCounters;
    pStat->CopySectorCnt  = 0;
    pStat->EraseCnt       = 0;
    pStat->ReadSectorCnt  = 0;
    pStat->WriteSectorCnt = 0;
    pStat->ReadCnt        = 0;
    pStat->ReadByteCnt    = 0;
    pStat->WriteCnt       = 0;
    pStat->WriteByteCnt   = 0;
  }
}

#endif // FS_NOR_ENABLE_STATS

/*********************************************************************
*
*      FS_NOR_ReadOff
*
*  Function description
*    Reads a range of bytes from the NOR flash device.
*
*  Parameters
*    Unit           Index of the sector map NOR driver (0-based).
*    Off            Offset of the first byte to be read.
*    pData          [OUT] Read data.
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
*    of the NOR flash area configured via FS_NOR_Configure().
*/
int FS_NOR_ReadOff(U8 Unit, U32 Off, void * pData, U32 NumBytes) {
  NOR_INST * pInst;
  int        r;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;
  }                       // Error, could not allocate driver instance.
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return 1;             // Error, could not initialize NOR flash.
  }
  r = _ReadOff(pInst, Off, pData, NumBytes);
  return r;
}

/*********************************************************************
*
*      FS_NOR_EraseDevice
*
*  Function description
*    Erases all the physical sectors configured as storage.
*
*  Parameters
*    Unit           Index of the sector map NOR driver (0-based).
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
int FS_NOR_EraseDevice(U8 Unit) {
  NOR_INST * pInst;
  int        r;
  U32        NumPhySectors;
  U32        iSector;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;             // Error, could not allocate driver instance.
  }
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return 1;             // Error, could not initialize NOR flash.
  }
  NumPhySectors = pInst->NumPhySectors;
  for (iSector = 0; iSector < NumPhySectors; ++iSector) {
    r = _ErasePhySector(pInst, iSector);
    if (r != 0) {
      return r;           // Error, could not erase physical sector.
    }
  }
  return 0;               // OK, all physical sectors have been erased.
}

#if FS_NOR_VERIFY_ERASE

/*********************************************************************
*
*       FS_NOR_SetEraseVerification
*
*  Function description
*    Enables or disables the checking of the sector erase operation.
*
*  Parameters
*    Unit           Index of the sector map NOR driver (0-based).
*    OnOff          Specifies if the feature has to be enabled or disabled
*                   * ==0   The erase operation is not checked.
*                   * !=0   The erase operation is checked.
*
*  Additional information
*    This function is optional. The result of a sector erase operation
*    is normally checked by evaluating the error bits maintained by the
*    NOR flash device in a internal status register. FS_NOR_SetEraseVerification()
*    can be used to enable additional verification of the sector erase
*    operation that is realized by reading back the contents of the entire
*    erased physical sector and by checking that all the bytes in it are
*    set to 0xFF. Enabling this feature can negatively impact the write
*    performance of sector map NOR driver.
*
*    The sector erase verification feature is active only when the sector map
*    NOR driver is compiled with the FS_NOR_VERIFY_ERASE configuration
*    define is set to 1 (default is 0) or when the FS_DEBUG_LEVEL
*    configuration define is set to a value greater than or equal to
*    FS_DEBUG_LEVEL_CHECK_ALL.
*/
void FS_NOR_SetEraseVerification(U8 Unit, U8 OnOff) {
  NOR_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->VerifyErase = OnOff;
  }
}

#endif // FS_NOR_VERIFY_ERASE

#if FS_NOR_VERIFY_WRITE

/*********************************************************************
*
*       FS_NOR_SetWriteVerification
*
*  Function description
*    Enables or disables the checking of the page write operation.
*
*  Parameters
*    Unit           Index of the sector map NOR driver (0-based).
*    OnOff          Specifies if the feature has to be enabled or disabled
*                   * ==0   The write operation is not checked.
*                   * !=0   The write operation is checked.
*
*  Additional information
*    This function is optional. The result of a page write operation
*    is normally checked by evaluating the error bits maintained by the
*    NOR flash device in a internal status register. FS_NOR_SetWriteVerification()
*    can be used to enable additional verification of the page write
*    operation that is realized by reading back the contents of the written
*    page and by checking that all the bytes are matching the data
*    requested to be written. Enabling this feature can negatively
*    impact the write performance of sector map NOR driver.
*
*    The page write verification feature is active only when the sector map
*    NOR driver is compiled with the FS_NOR_VERIFY_WRITE configuration
*    define is set to 1 (default is 0) or when the FS_DEBUG_LEVEL
*    configuration define is set to a value greater than or equal to
*    FS_DEBUG_LEVEL_CHECK_ALL.
*/
void FS_NOR_SetWriteVerification(U8 Unit, U8 OnOff) {
  NOR_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->VerifyWrite = OnOff;
  }
}

#endif // FS_NOR_VERIFY_WRITE

#if FS_NOR_SKIP_BLANK_SECTORS

/*********************************************************************
*
*       FS_NOR_SetBlankSectorSkip
*
*  Function description
*    Configures if the physical sectors which are already blank
*    should be erased during the low-level format operation.
*
*  Parameters
*    Unit           Index of the sector map NOR driver (0-based).
*    OnOff          Specifies if the feature has to be enabled or disabled
*                   * ==0   All the physical sectors are erased.
*                   * !=0   Physical sectors are not erased if they are blank (default).
*
*  Additional information
*    This function is optional. The blank checking feature is disabled
*    by default and has to be explicitly enabled at compile time by
*    setting FS_NOR_SKIP_BLANK_SECTORS to 1. The feature can then be
*    enabled or disabled at runtime using FS_NOR_SetBlankSectorSkip().
*
*    Activating this feature can improve the speed of the low-level
*    format operation when most of the physical sectors of the NOR flash
*    device are already blank which is typically the case with NOR flash
*    devices that ship from factory.
*/
void FS_NOR_SetBlankSectorSkip(U8 Unit, U8 OnOff) {
  NOR_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->SkipBlankSectors = OnOff;
  }
}

#endif // FS_NOR_SKIP_BLANK_SECTORS

#if FS_NOR_SUPPORT_VARIABLE_LINE_SIZE

/*********************************************************************
*
*       FS_NOR_SetDeviceLineSize
*
*  Function description
*    Configures the minimum number of bytes that can be written to NOR flash.
*
*  Parameters
*    Unit             Index of the sector map NOR driver (0-based).
*    ldBytesPerLine   Line size in bytes as power of 2 exponent.
*
*  Additional information
*    This function is optional. Typically, the NOR flash have lines smaller
*    than 4 bytes which is the fixed default value configured at compile time.
*    The FS_NOR_SUPPORT_VARIABLE_LINE_SIZE configuration define has to be set to a value
*    different than 0 in order to enable this function.
*/
void FS_NOR_SetDeviceLineSize(U8 Unit, U8 ldBytesPerLine) {
  NOR_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->ldBytesPerLine = ldBytesPerLine;
    _CalcUpdateSizeOfLSH(pInst);
    _CalcUpdateSizeOfPSH(pInst);
  }
}

/*********************************************************************
*
*       FS_NOR_SetDeviceRewriteSupport
*
*  Function description
*    Specifies if the NOR flash device can rewrite the same data
*    if 0s are preserved.
*
*  Parameters
*    Unit           Index of the sector map NOR driver (0-based).
*    OnOff          Specifies if the feature has to be enabled or disabled
*                   * ==0   Rewrite operations are not performed.
*                   * !=0   Rewrite operation are performed (default).
*
*  Additional information
*    This function is optional. Typically, the NOR flash devices are
*    able to rewrite the same data and by default this feature is
*    disabled at compile time. The FS_NOR_SUPPORT_VARIABLE_LINE_SIZE
*    configuration define has to be set to a value different than 0
*    in order to enable this function, otherwise the function does nothing.
*/
void FS_NOR_SetDeviceRewriteSupport(U8 Unit, U8 OnOff) {
  NOR_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->IsRewriteSupported = OnOff;
    _CalcUpdateSizeOfLSH(pInst);
    _CalcUpdateSizeOfPSH(pInst);
  }
}

#endif // FS_NOR_SUPPORT_VARIABLE_LINE_SIZE

#if FS_NOR_OPTIMIZE_DIRTY_CHECK

/*********************************************************************
*
*       FS_NOR_SetDirtyCheckOptimization
*
*  Function description
*    Enables or disables the blank checking of a logical sector before write.
*
*  Parameters
*    Unit           Index of the sector map NOR driver (0-based).
*    OnOff          Specifies if the feature has to be enabled or disabled
*                   * ==0   Dirty check is disabled (default).
*                   * !=0   Dirty check is enabled.
*
*  Additional information
*    This function is optional. Per default, the NOR driver checks
*    if the data of the logical sector is blank (all bytes 0xFF)
*    before it writes the data. This is necessary in order to make
*    sure that the NOR driver does not write to partially written
*    logical sectors. Writing to a partially written logical sector
*    can cause a data loss since the write operation can change
*    the value of a bit only from 1 to 0. A partially written logical
*    sector occurs when the write operation is interrupted by an
*    unexpected reset. In this case the status of the logical sector
*    indicates that the logical sector is blank which is not correct.
*    Therefore the logical sector cannot be used for storage and
*    it is marked by the NOR driver as invalid.
*
*    Typically, the blank checking runs fast but on some targets
*    it may reduce the write performance. In this cases, this option
*    can be used to skip the blank checking which helps improve
*    the performance. When the optimization is enabled, the blank
*    checking is not longer performed on the logical sectors located
*    on physical sectors that have been erased at least once since
*    the last mount operation. The NOR driver can skip the blank
*    checking for these physical sectors since it knows that they
*    do not contain any partially written logical sectors. The application
*    can remove any partially written logical sectors by performing
*    a clean operation of the entire storage via the FS_STORAGE_Clean()
*    or FS_STORAGE_CleanOne() API functions. The NOR driver requires
*    one bit of RAM storage for each physical sector used as storage.
*
*    The FS_NOR_OPTIMIZE_DIRTY_CHECK configuration define has to be
*    set to a value different than 0 in order to enable this function,
*    otherwise the function does nothing.
*/
void FS_NOR_SetDirtyCheckOptimization(U8 Unit, U8 OnOff) {
  NOR_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->IsDirtyCheckOptimized = OnOff;
  }
}

#endif // FS_NOR_OPTIMIZE_DIRTY_CHECK

/*********************************************************************
*
*       FS_NOR_WriteOff
*
*  Function description
*    Writes data to NOR flash memory.
*
*  Parameters
*    Unit           Index of the Sector Map NOR driver (0-based).
*    Off            Location where to write the data.
*    pData          [IN] Data to be written.
*    NumBytes       Number of bytes to be written.
*
*  Return value
*    ==0 - OK, data written successfully.
*    !=0 - An error occurred.
*
*  Additional information
*    Off has to be specified in bytes and is relative to the beginning
*    of the NOR flash area configured via FS_NOR_Configure().
*
*    FS_NOR_WriteOff() is able to write across page and physical
*    sector boundaries. This function can only change bit values from
*    1 to 0. The bits can be set to 1 block-wise via FS_NOR_ErasePhySector().
*
*    The function takes care of the alignment required when writing
*    to NOR flash devices with line size larger than 1.
*/
int FS_NOR_WriteOff(U8 Unit, U32 Off, const void * pData, U32 NumBytes) {
  NOR_INST * pInst;
  int        r;
  U32        OffSector;
#if (FS_NOR_LINE_SIZE > 1)
#if (FS_NOR_LINE_SIZE > 4)
  U32        aBuffer[FS_NOR_LINE_SIZE / 4];
#else
  U16        aBuffer[FS_NOR_LINE_SIZE / 2];
#endif
  U32        OffLine;
  U32        NumBytesAtOnce;
  const U8 * pData8;
  U8       * pBuffer8;
  U32        NumLines;
  U32        OffAligned;
  unsigned   ldBytesPerLine;
  unsigned   BytesPerLine;
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
  _GetSectorInfo(pInst, 0, &OffSector, NULL);
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
    r = _ReadOff(pInst, OffAligned, aBuffer, BytesPerLine);
    if (r != 0) {
      return 1;                                     // Error, could not read data.
    }
    FS_MEMCPY(pBuffer8, pData8, NumBytesAtOnce);
    r = _WriteOff(pInst, OffAligned, aBuffer, BytesPerLine);
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
        r = _WriteOff(pInst, Off, pData8, NumBytesAtOnce);
        if (r != 0) {
          return r;                                 // Error, could not write data.
        }
        Off      += NumBytesAtOnce;
        NumBytes -= NumBytesAtOnce;
        pData8   += NumBytesAtOnce;
      } else {
        NumBytesAtOnce = sizeof(aBuffer);
        do {
          FS_MEMCPY(aBuffer, pData8, NumBytesAtOnce);
          r = _WriteOff(pInst, Off, aBuffer, NumBytesAtOnce);
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
    r = _ReadOff(pInst, Off, aBuffer, BytesPerLine);
    if (r != 0) {
      return 1;                                     // Error, could not read data.
    }
    FS_MEMCPY(aBuffer, pData8, NumBytes);
    r = _WriteOff(pInst, Off, aBuffer, BytesPerLine);
    if (r != 0) {
      return r;                                     // Error, could not write data.
    }
  }
#else
  r = _WriteOff(pInst, Off, pData, NumBytes);
#endif // FS_NOR_LINE_SIZE > 1
  return r;
}

/*********************************************************************
*
*       FS_NOR_ErasePhySector
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
*    of the NOR flash area configured via FS_NOR_Configure().
*    The number of bytes actually erased depends on the size of the
*    physical sector supported by the NOR flash device.
*    Information about a physical sector can be obtained via
*    FS_NOR_GetSectorInfo().
*/
int FS_NOR_ErasePhySector(U8 Unit, U32 PhySectorIndex) {
  NOR_INST * pInst;
  int        r;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;
  }
  r = _InitIfRequired(pInst);
  if (r == 0) {
    r = _ErasePhySector(pInst, PhySectorIndex);
    if (r != 0) {
      return 1;                           // Error, could not erase physical sector.
    }
  }
  return r;
}

/*************************** End of file ****************************/
