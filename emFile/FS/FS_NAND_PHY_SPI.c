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
File        : FS_NAND_PHY_SPI.c
Purpose     : Physical layer for SPI NAND flashes
Literature  : [1] \\fileserver\Techinfo\Company\Micron\NANDFlash\SerialNAND\MT29F1G01AAADD.pdf
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#include "FS_Int.h"
#if FS_SUPPORT_TEST
  #include "FS_NAND_Int.h"
#endif

/*********************************************************************
*
*       Defines, fixed
*
**********************************************************************
*/
#if       (FS_NAND_MAX_SPARE_AREA_SIZE != 0)
  #define MAX_SPARE_AREA_SIZE               FS_NAND_MAX_SPARE_AREA_SIZE
#else
  #define MAX_SPARE_AREA_SIZE               64
#endif

/*********************************************************************
*
*       Commands
*/
#define CMD_READ_DATA                       0x03
#define CMD_ENABLE_WRITE                    0x06
#define CMD_DISABLE_WRITE                   0x04
#define CMD_GET_FEATURES                    0x0F
#define CMD_READ_DATA_X1                    0x0B
#define CMD_EXEC_PROG                       0x10
#define CMD_READ_PAGE                       0x13
#define CMD_SET_FEATURES                    0x1F
#define CMD_LOAD_PROG_RAND_X4               0x34
#define CMD_READ_DATA_X2                    0x3B
#define CMD_READ_DATA_X4                    0x6B
#define CMD_READ_ECC_STATUS                 0x7C      // Macronix only
#define CMD_LOAD_PROG_RAND                  0x84
#define CMD_READ_ID                         0x9F
#define CMD_SELECT_DIE                      0xC2      // Winbond only
#define CMD_ERASE_BLOCK                     0xD8
#define CMD_RESET                           0xFF

/*********************************************************************
*
*       Feature addresses
*/
#define FEAT_ADDR_ECC_STATUS                0x30u     // Toshiba and Winbond specific
#define FEAT_ADDR_BLOCK_LOCK                0xA0u
#define FEAT_ADDR_OTP                       0xB0u
#define FEAT_ADDR_STATUS                    0xC0u
#define FEAT_ADDR_DIE_SELECT                0xD0u     // Micron only
#define FEAT_ADDR_STATUS_EX                 0xF0u     // GigaDevice specific

/*********************************************************************
*
*       Status flags
*/
#define STATUS_IN_PROGRESS_BIT              0u
#define STATUS_IN_PROGRESS                  (1u << STATUS_IN_PROGRESS_BIT)
#define STATUS_WRITE_ENABLED_BIT            1u
#define STATUS_ERASE_ERROR                  0x04u
#define STATUS_PROGRAM_ERROR                0x08u
#define STATUS_READ_ERROR_MASK              0x30u
#define STATUS_READ_ERROR_MASK_EX           0x70u
#define STATUS_READ_ERROR_CORRECTED         0x10u
#define STATUS_READ_ERROR_CORRECTED_EX      0x30u     // Winbond and Alliance Memory only
#define STATUS_READ_ERROR_NOT_CORRECTED     0x20u
#define STATUS_READ_ERROR_NOT_CORRECTED_EX  0x70u     // GigaDevice only
#define STATUS_READ_ERROR_CORRECTED_1_3     0x10u
#define STATUS_READ_ERROR_CORRECTED_4_6     0x30u
#define STATUS_READ_ERROR_CORRECTED_7_8     0x50u
#define STATUS_READ_ERROR_CORRECTED_4       0x20u     // GigaDevice only
#define STATUS_READ_ERROR_CORRECTED_5       0x30u     // GigaDevice only
#define STATUS_READ_ERROR_CORRECTED_6       0x40u     // GigaDevice only
#define STATUS_READ_ERROR_CORRECTED_7       0x50u     // GigaDevice only
#define STATUS_READ_ERROR_CORRECTED_8       0x60u     // GigaDevice only

/*********************************************************************
*
*       ONFI parameters
*/
#define ONFI_PAGE_SIZE                      256u
#define ONFI_CRC_POLY                       0x8005u
#define ONFI_CRC_INIT                       0x4F4Eu
#define NUM_ONFI_PAGES                      3
#define PAGE_INDEX_ONFI                     1
#define PAGE_INDEX_ONFI_EX                  0         // Alliance Memory only

#if FS_NAND_SUPPORT_READ_CACHE

/*********************************************************************
*
*       Page cache status
*/
#define CACHE_STATUS_DEFAULT                0u        // By default the caching is enabled
#define CACHE_STATUS_ENABLED                1u
#define CACHE_STATUS_DISABLED               2u

#endif // FS_NAND_SUPPORT_READ_CACHE

/*********************************************************************
*
*       Manufacturer IDs
*/
#define MFG_ID_WINBOND                      0xEFu
#define MFG_ID_MICRON                       0x2Cu
#define MFG_ID_TOSHIBA                      0x98u
#define MFG_ID_MACRONIX                     0xC2u
#define MFG_ID_GIGADEVICE                   0xC8u
#define MFG_ID_ISSI                         MFG_ID_GIGADEVICE     // It seems that the ISSI devices report the same id as the GigaDevice devices.
#define MFG_ID_ALLIANCEMEMORY               0x52u

/*********************************************************************
*
*       Feature flags
*/
#define FEAT_QE                             0x01u
#define FEAT_OTP_ENABLE                     0x40u
#define FEAT_ECC_ENABLE                     0x10u
#define FEAT_BUF_MODE                       0x08u     // Winbond only
#define FEAT_DIE_SELECT                     0x40u
#define FEAT_CONT_READ                      0x01u     // Micron only
#define FEAT_HS_MODE                        0x02u     // Toshiba only
#define FEAT_HOLD_FUNC                      0x01u     // Toshiba only

/*********************************************************************
*
*       Type of responses to READ ID command
*
*  Notes
*    (1) The numerical order of these defines is relevant.
*/
#define DEVICE_ID_TYPE_ENHANCED             0         // Command sequence: CMD_READ_ID MfgId DeviceId1 DeviceId2
#define DEVICE_ID_TYPE_STANDARD             1         // Command sequence: CMD_READ_ID DummyByte MfgId DeviceId
#define DEVICE_ID_TYPE_COUNT                2         // Number of response types

/*********************************************************************
*
*       Misc. defines
*/
#define NUM_BYTES_ADDR                      3
#define NUM_BYTES_OFF                       2
#define NUM_BYTES_DUMMY                     1
#define PAGE_INDEX_INVALID                  0xFFFFFFFFuL
#define ECC_STATUS_MBF_BIT                  4         // Toshiba and Winbond specific
#define ECC_STATUS_MASK                     0xFu      // Macronix specific
#define OFF_USER_DATA                       4u
#define NUM_BYTES_USER_DATA                 4u
#define OFF_USER_DATA_ISSI                  8u        // ISSI specific
#define ECC_STATUS_BIT                      4u        // GigaDevice specific

/*********************************************************************
*
*       ASSERT_UNIT_NO_IS_IN_RANGE
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)                                         \
    if ((Unit) >= (U8)FS_NAND_NUM_UNITS) {                                         \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NAND_PHY_SPI: Invalid unit number.")); \
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);                                         \
    }
#else
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)
#endif

/*********************************************************************
*
*       ASSERT_HW_TYPE_IS_SET
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_HW_TYPE_IS_SET(pInst)                                          \
    if (((pInst)->pHWTypeQSPI == NULL) ||                                       \
        (((pInst)->pHWTypeQSPI == &_DefaultHWLayer) &&                          \
         ((pInst)->pHWTypeSPI  == NULL))) {                                     \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NAND_PHY_SPI: HW layer not set.")); \
      FS_X_PANIC(FS_ERRCODE_HW_LAYER_NOT_SET);                                  \
    }
#else
  #define ASSERT_HW_TYPE_IS_SET(Unit)
#endif

/*********************************************************************
*
*       ASSERT_ENTIRE_SPARE_AREA
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_ENTIRE_SPARE_AREA(pInst, Off, NumBytes)                                    \
    if (((Off) != (1uL << (pInst)->ldBytesPerPage)) ||                                      \
        ((NumBytes) != (pInst)->BytesPerSpareArea)) {                                       \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NAND_PHY_SPI: Invalid access to spare area.")); \
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);                                                  \
    }
#else
  #define ASSERT_ENTIRE_SPARE_AREA(Unit, Off, NumBytes)
#endif

/*********************************************************************
*
*       ASSERT_IS_ECC_ENABLED
*/
#if FS_SUPPORT_TEST
  #define ASSERT_IS_ECC_ENABLED(pInst)            \
    if (_IsECCEnabled(pInst) == 0u) {             \
      FS_X_PANIC(FS_ERRCODE_INVALID_USAGE);       \
    }
#else
  #define ASSERT_IS_ECC_ENABLED(Unit)
#endif

/*********************************************************************
*
*       ASSERT_IS_ECC_DISABLED
*/
#if FS_SUPPORT_TEST
  #define ASSERT_IS_ECC_DISABLED(pInst)           \
    if (_IsECCEnabled(pInst) != 0u) {             \
      FS_X_PANIC(FS_ERRCODE_INVALID_USAGE);       \
    }
#else
  #define ASSERT_IS_ECC_DISABLED(Unit)
#endif

/*********************************************************************
*
*       CALL_TEST_HOOK_READ_BEGIN
*/
#if FS_SUPPORT_TEST
  #define CALL_TEST_HOOK_READ_BEGIN(Unit, pData, pNumBytes)        _CallTestHookReadBegin(Unit, pData, pNumBytes)
#else
  #define CALL_TEST_HOOK_READ_BEGIN(Unit, pData, pNumBytes)
#endif

/*********************************************************************
*
*       CALL_TEST_HOOK_READ_END
*/
#if FS_SUPPORT_TEST
  #define CALL_TEST_HOOK_READ_END(Unit, pData, NumBytes, pResult)  _CallTestHookReadEnd(Unit, pData,  NumBytes, pResult)
#else
  #define CALL_TEST_HOOK_READ_END(Unit, pData, NumBytes, pResult)
#endif

/*********************************************************************
*
*       CALL_TEST_HOOK_WRITE_BEGIN
*/
#if FS_SUPPORT_TEST
  #define CALL_TEST_HOOK_WRITE_BEGIN(Unit, ppData, pNumBytes)      _CallTestHookWriteBegin(Unit, ppData, pNumBytes)
#else
  #define CALL_TEST_HOOK_WRITE_BEGIN(Unit, pData, pNumBytes)
#endif

/*********************************************************************
*
*       CALL_TEST_HOOK_WRITE_END
*/
#if FS_SUPPORT_TEST
  #define CALL_TEST_HOOK_WRITE_END(Unit, pData, NumBytes, pResult) _CallTestHookWriteEnd(Unit, pData, NumBytes, pResult)
#else
  #define CALL_TEST_HOOK_WRITE_END(Unit, pData, NumBytes, pResult)
#endif

/*********************************************************************
*
*       Local types
*
**********************************************************************
*/

/*********************************************************************
*
*       NAND_SPI_INST
*
*  Description
*    Physical layer instance.
*
*  Notes
*    (1) ONFI uses the term unit to refer to a die inside a stacked device.
*        We prefer to use the term die in order to avoid confusion with the
*        unit number that identifies the instance of a physical layer.
*/
typedef struct {
  const FS_NAND_HW_TYPE_SPI     * pHWTypeSPI;                 // Table containing the pointers to the low-level access routines (for SPI)
  const FS_NAND_HW_TYPE_QSPI    * pHWTypeQSPI;                // Table containing the pointers to the low-level access routines (for quad and dual SPI)
  const FS_NAND_SPI_DEVICE_TYPE * pDevice;                    // Device-specific API functions.
  const FS_NAND_SPI_DEVICE_LIST * pDeviceList;                // List of supported devices
  U32                             TimeOut;                    // Number of cycles to poll for the end of a NAND flash operation
#if FS_NAND_SUPPORT_READ_CACHE
  U32                             CachePageIndex;             // Number of the last page read from NAND flash
#endif // FS_NAND_SUPPORT_READ_CACHE
  U16                             BytesPerSpareArea;          // Number of bytes in the spare area.
  U16                             BusWidthRead;               // Number of data lines to be used for the read operation
  U16                             BusWidthWrite;              // Number of data lines to be used for the write operation
  U8                              Unit;                       // Index of the physical layer
  U8                              ldNumPlanes;                // Number of planes (as power of 2 exponent)
  U8                              ldBlocksPerDie;             // Number of blocks in one die of the NAND device (as a power of 2 exponent).
  U8                              ldPagesPerBlock;            // Number of pages in a block (as power of 2 exponent)
  U8                              NumBitErrorsCorrectable;    // Number of bit errors the ECC should be able to correct.
  U8                              HasHW_ECC;                  // Set to 1 if the NAND flash device supports HW ECC.
  U8                              ldNumDies;                  // Number of stacked devices (as power of 2 exponent).
  U8                              IsPageCopyAllowed;          // Set to 1 if the phy. layer is allowed to let the NAND flash copy pages internally.
                                                              // This is possible only when the HW ECC of the NAND flash is enabled.
                                                              // By doing otherwise bit errors are propagated that can lead to data loss due to uncorrectable bit errors.
  U8                              IsECCEnabled;               // Set to 1 if the HW ECC is enabled on the NAND flash device.
  U8                              ldBytesPerPage;             // Number of bytes in a page (without spare area, as power of 2 exponent)
  U8                              ldNumECCBlocks;             // Number of ECC that cover the data in a page (as power of 2 exponent)
  U8                              Allow2bitMode;              // Enables / disables the physical layer to use 2 lines for the data transfer
  U8                              Allow4bitMode;              // Enables / disables the physical layer to use 4 lines for the data transfer
  U8                              CmdRead;                    // Code of the command used to receive data form NAND flash device
  U8                              CmdWrite;                   // Code of the command used to send data to NAND flash device
  U8                              DieIndexSelected;           // Id of the currently selected die.
#if FS_NAND_SUPPORT_READ_CACHE
  U8                              CacheStatus;                // Indicates whether the caching is enabled or not. See CACHE_STATUS_...
#endif // FS_NAND_SUPPORT_READ_CACHE
#if (FS_NAND_SUPPORT_COMPATIBILITY_MODE != 0)
  U8                              CompatibilityMode;          // Compatibility mode for handling the data stored in the spare area of Micron MT29F1G01ABAFD (for testing only).
#endif // FS_NAND_SUPPORT_COMPATIBILITY_MODE != 0
} NAND_SPI_INST;

/*********************************************************************
*
*       NAND_SPI_PARA
*
*  Description
*    Parameters of the NAND flash device.
*/
typedef struct {
  U32              BytesPerPage;
  U32              PagesPerBlock;
  U32              NumBlocks;
  U16              BytesPerSpareArea;
  U8               MfgId;
  U8               NumDies;
  FS_NAND_ECC_INFO ECCInfo;
} NAND_SPI_PARA;

/*********************************************************************
*
*       FS_NAND_SPI_DEVICE_TYPE
*
*  Description
*    Device-specific API functions.
*
*  Additional information
*    pfIdentify(), pfReadApplyPara(), and pfSelectDie() are optional and can be set to NULL.
*/
struct FS_NAND_SPI_DEVICE_TYPE {
  //
  //lint -esym(9058, FS_NAND_SPI_DEVICE_TYPE)
  //  We cannot define this structure in FS.h because all the functions take a pointer
  //  to the instance of the physical layer that is a structure which is only visible in this module.
  //
  int (*pfIdentify)         (      NAND_SPI_INST * pInst, const U8 * pId);
  int (*pfReadApplyPara)    (      NAND_SPI_INST * pInst, const U8 * pId);
  int (*pfReadDataFromCache)(const NAND_SPI_INST * pInst, U32 PageIndex,       void * pData, unsigned Off, unsigned NumBytes);
  int (*pfWriteDataToCache) (const NAND_SPI_INST * pInst, U32 PageIndex, const void * pData, unsigned Off, unsigned NumBytes);
  int (*pfGetECCResult)     (const NAND_SPI_INST * pInst, FS_NAND_ECC_RESULT * pResult);
  int (*pfSelectDie)        (      NAND_SPI_INST * pInst, unsigned DieIndex);
  int (*pfIsReadError)      (U8 Status);
  int (*pfBeginPageCopy)    (const NAND_SPI_INST * pInst);
  int (*pfEndPageCopy)      (const NAND_SPI_INST * pInst);
};

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static NAND_SPI_INST                   * _apInst[FS_NAND_NUM_UNITS];
#if FS_SUPPORT_TEST
  static FS_NAND_TEST_HOOK_READ_BEGIN  * _pfTestHookReadBegin;
  static FS_NAND_TEST_HOOK_READ_END    * _pfTestHookReadEnd;
  static FS_NAND_TEST_HOOK_WRITE_BEGIN * _pfTestHookWriteBegin;
  static FS_NAND_TEST_HOOK_WRITE_END   * _pfTestHookWriteEnd;
#endif // FS_SUPPORT_TEST

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

#if FS_SUPPORT_TEST

/*********************************************************************
*
*       _CallTestHookReadBegin
*/
static void _CallTestHookReadBegin(U8 Unit, void * pData, unsigned * pNumBytes) {
  if (_pfTestHookReadBegin != NULL) {
    _pfTestHookReadBegin(Unit, pData, pNumBytes);
  }
}

/*********************************************************************
*
*       _CallTestHookReadEnd
*/
static void _CallTestHookReadEnd(U8 Unit, void * pData, unsigned NumBytes, int * pResult) {
  if (_pfTestHookReadEnd != NULL) {
    _pfTestHookReadEnd(Unit, pData, NumBytes, pResult);
  }
}

/*********************************************************************
*
*       _CallTestHookWriteBegin
*/
static void _CallTestHookWriteBegin(U8 Unit, const void ** ppData, unsigned * pNumBytes) {
  if (_pfTestHookWriteBegin != NULL) {
    _pfTestHookWriteBegin(Unit, ppData, pNumBytes);
  }
}

/*********************************************************************
*
*       _CallTestHookWriteEnd
*/
static void _CallTestHookWriteEnd(U8 Unit, const void * pData, unsigned NumBytes, int * pResult) {
  if (_pfTestHookWriteEnd != NULL) {
    _pfTestHookWriteEnd(Unit, pData, NumBytes, pResult);
  }
}

#endif // FS_SUPPORT_TEST

/*********************************************************************
*
*       _ld
*/
static U16 _ld(U32 Value) {
  U16 i;

  for (i = 0; i < 32u; i++) {
    if ((1uL << i) == Value) {
      break;
    }
  }
  return i;
}

/*********************************************************************
*
*       _CalcBlockIndex
*
*  Function description
*    Remaps a block index so that consecutive logical blocks are
*    located on the same plane.
*
*  Additional information
*    The memory array of some of Micron NAND flash devices is organized
*    in two planes. One plane contains even-numbered physical blocks
*    while the other plane odd-numbered ones. An internal copy operation
*    can be executed only if the source and destination pages are on the
*    same plane. This remapping increases the chance that pages belonging
*    to different blocks are located on the same plane.
*
*      LBI      PBI
*      0        0
*      1        2
*      2        4
*      3        6
*      ...
*      1023     2046
*      1024     1
*      1025     3
*      ...
*      2047     2047
*/
static U32 _CalcBlockIndex(const NAND_SPI_INST * pInst, U32 BlockIndex) {
  U32      BlockIndexNew;
  U32      BlocksPerPlane;
  unsigned ldNumPlanes;
  U32      NumBlocks;
  unsigned ldBlocksPerDie;
  unsigned ldNumDies;

  BlockIndexNew  = BlockIndex;
  ldNumPlanes    = pInst->ldNumPlanes;
  ldBlocksPerDie = pInst->ldBlocksPerDie;
  ldNumDies      = pInst->ldNumDies;
  NumBlocks      = 1uL << (ldBlocksPerDie + ldNumDies);
  if (ldNumPlanes != 0u) {
    BlocksPerPlane = (U32)NumBlocks >> ldNumPlanes;
    BlockIndexNew  = (BlockIndex & (BlocksPerPlane - 1u)) << 1;
    if ((BlockIndex & ~(BlocksPerPlane - 1u)) != 0u) {
      ++BlockIndexNew;
    }
  }
  return BlockIndexNew;
}

/*********************************************************************
*
*       _CalcPageIndex
*/
static U32 _CalcPageIndex(const NAND_SPI_INST * pInst, U32 PageIndex) {
  U32 BlockIndex;
  U32 PageOff;
  U32 ldPagesPerBlock;

  ldPagesPerBlock = pInst->ldPagesPerBlock;
  BlockIndex = PageIndex >> ldPagesPerBlock;
  PageOff    = PageIndex & ((1uL << ldPagesPerBlock) - 1u);
  BlockIndex = _CalcBlockIndex(pInst, BlockIndex);
  PageIndex  = (BlockIndex << ldPagesPerBlock) | PageOff;
  return PageIndex;
}

/*********************************************************************
*
*       _IsSamePlane
*/
static int _IsSamePlane(const NAND_SPI_INST * pInst, U32 PageIndex1, U32 PageIndex2) {
  U32      Mask;
  unsigned ldPagesPerBlock;
  unsigned ldNumPlanes;
  unsigned ldNumDies;
  unsigned ldBlocksPerDie;

  ldNumPlanes     = pInst->ldNumPlanes;
  ldPagesPerBlock = pInst->ldPagesPerBlock;
  ldNumDies       = pInst->ldNumDies;
  ldBlocksPerDie  = pInst->ldBlocksPerDie;
  //
  // Check if the pages are on the same die.
  // A plane is always limited to one die.
  //
  if (ldNumDies != 0u) {
    Mask   = (1uL << ldNumDies) - 1u;
    Mask <<= ldBlocksPerDie + ldPagesPerBlock;
    if ((PageIndex1 & Mask) != (PageIndex2 & Mask)) {
      return 0;               // Not on the same die.
    }
  }
  //
  // If the die has only one plane then we are done.
  // This is the most common case.
  //
  if (ldNumPlanes == 0u) {
    return 1;
  }
  //
  // Check if the pages are on the same plane.
  //
  Mask   = (1uL << ldNumPlanes) - 1u;
  Mask <<= ldPagesPerBlock;
  if ((PageIndex1 & Mask) == (PageIndex2 & Mask)) {
    return 1;
  }
  return 0;                   // Not on the same plane.
}

/*********************************************************************
*
*       _SetCachePageIndex
*/
static void _SetCachePageIndex(NAND_SPI_INST * pInst, U32 PageIndex) {
#if FS_NAND_SUPPORT_READ_CACHE
  pInst->CachePageIndex = PageIndex;
#else
  FS_USE_PARA(pInst);
  FS_USE_PARA(PageIndex);
#endif
}

/*********************************************************************
*
*       _IsPageInCache
*/
static int _IsPageInCache(const NAND_SPI_INST * pInst, U32 PageIndex) {
  int r;

  r = 0;          // Page not in cache.
#if FS_NAND_SUPPORT_READ_CACHE
  {
    U32 CachePageIndex;
    U8  CacheStatus;

    CacheStatus = pInst->CacheStatus;
    if ((CacheStatus == CACHE_STATUS_DEFAULT) ||
        (CacheStatus == CACHE_STATUS_ENABLED)) {
      //
      // Get the number of the last page read and check if
      // it is stored in the internal register of NAND flash.
      //
      CachePageIndex = pInst->CachePageIndex;
      if (PageIndex == CachePageIndex) {
        r = 1;      // OK, page is cache.
      }
    }
  }
#else
  FS_USE_PARA(pInst);
  FS_USE_PARA(PageIndex);
#endif
  return r;
}

/*********************************************************************
*
*      _IsPageCopyAllowed
*/
static int _IsPageCopyAllowed(const NAND_SPI_INST * pInst) {
  int r;

  r = (int)pInst->IsPageCopyAllowed;
  return r;
}

/*********************************************************************
*
*      _AllowPageCopy
*/
static void _AllowPageCopy(NAND_SPI_INST * pInst, U8 OnOff) {
  pInst->IsPageCopyAllowed = OnOff;
}

/*********************************************************************
*
*      _BeginPageCopy
*/
static int _BeginPageCopy(const NAND_SPI_INST * pInst) {
  int r;

  r = 0;          // Set to indicate success.
  if (pInst->pDevice->pfBeginPageCopy != NULL) {
    r = pInst->pDevice->pfBeginPageCopy(pInst);
  }
  return r;
}

/*********************************************************************
*
*      _EndPageCopy
*/
static int _EndPageCopy(const NAND_SPI_INST * pInst) {
  int r;

  r = 0;          // Set to indicate success.
  if (pInst->pDevice->pfEndPageCopy != NULL) {
    r = pInst->pDevice->pfEndPageCopy(pInst);
  }
  return r;
}

/*********************************************************************
*
*      _SPI_Init
*/
static int _SPI_Init(const NAND_SPI_INST * pInst) {
  U8  Unit;
  int Freq_kHz;

  Unit = pInst->Unit;
  Freq_kHz = pInst->pHWTypeSPI->pfInit(Unit);
  return Freq_kHz;
}

/*********************************************************************
*
*      _SPI_DisableCS
*/
static void _SPI_DisableCS(const NAND_SPI_INST * pInst) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWTypeSPI->pfDisableCS(Unit);
}

/*********************************************************************
*
*      _SPI_EnableCS
*/
static void _SPI_EnableCS(const NAND_SPI_INST * pInst) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWTypeSPI->pfEnableCS(Unit);
}

/*********************************************************************
*
*      _SPI_Delay
*/
static void _SPI_Delay(const NAND_SPI_INST * pInst, int ms) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWTypeSPI->pfDelay(Unit, ms);
}

/*********************************************************************
*
*      _SPI_Read
*/
static int _SPI_Read(const NAND_SPI_INST * pInst, void * pData, unsigned NumBytes) {
  U8  Unit;
  int r;

  Unit = pInst->Unit;
  CALL_TEST_HOOK_READ_BEGIN(Unit, pData, &NumBytes);
  r = pInst->pHWTypeSPI->pfRead(Unit, pData, NumBytes);
  CALL_TEST_HOOK_READ_END(Unit, pData, NumBytes, &r);
  return r;
}

/*********************************************************************
*
*      _SPI_Write
*/
static int _SPI_Write(const NAND_SPI_INST * pInst, const void * pData, unsigned NumBytes) {
  U8  Unit;
  int r;

  Unit = pInst->Unit;
  CALL_TEST_HOOK_WRITE_BEGIN(Unit, &pData, &NumBytes);
  r = pInst->pHWTypeSPI->pfWrite(Unit, pData, NumBytes);
  CALL_TEST_HOOK_WRITE_END(Unit, pData, NumBytes, &r);
  return r;
}

/*********************************************************************
*
*      _SPI_Lock
*/
static void _SPI_Lock(const NAND_SPI_INST * pInst) {
  U8 Unit;

  Unit = pInst->Unit;
  if (pInst->pHWTypeSPI->pfLock != NULL) {
    pInst->pHWTypeSPI->pfLock(Unit);
  }
}

/*********************************************************************
*
*      _SPI_Unlock
*/
static void _SPI_Unlock(const NAND_SPI_INST * pInst) {
  U8 Unit;

  Unit = pInst->Unit;
  if (pInst->pHWTypeSPI->pfUnlock != NULL) {
    pInst->pHWTypeSPI->pfUnlock(Unit);
  }
}

/*********************************************************************
*
*       _IsProgramError
*
*  Function description
*    Checks if an error occurred during the program operation.
*/
static int _IsProgramError(U8 Status) {
  int r;

  r = 0;        // Set to indicate that no error occurred.
  if ((Status & STATUS_PROGRAM_ERROR) != 0u) {
    r = 1;
  }
  return r;
}

/*********************************************************************
*
*       _IsEraseError
*
*  Function description
*    Checks if an error occurred during the erase operation.
*/
static int _IsEraseError(U8 Status) {
  int r;

  r = 0;        // Set to indicate that no error occurred.
  if ((Status & STATUS_ERASE_ERROR) != 0u) {
    r = 1;
  }
  return r;
}

/*********************************************************************
*
*       _IsReadError
*
*  Function description
*    Checks if an error occurred during the read operation (ECC error).
*/
static int _IsReadError(U8 Status) {
  int r;

  r = 0;        // Set to indicate that no error occurred.
  Status &= STATUS_READ_ERROR_MASK;
  if (Status == STATUS_READ_ERROR_NOT_CORRECTED) {
    r = 1;
  }
  return r;
}

/*********************************************************************
*
*      _QSPI_Init
*/
static int _QSPI_Init(U8 Unit) {
  int             Freq_kHz;
  NAND_SPI_INST * pInst;

  pInst = _apInst[Unit];
  Freq_kHz = _SPI_Init(pInst);
  return Freq_kHz;
}

/*********************************************************************
*
*      _QSPI_ExecCmd
*/
static int _QSPI_ExecCmd(U8 Unit, U8 Cmd, U8 BusWidth) {
  int             r;
  NAND_SPI_INST * pInst;

  FS_USE_PARA(BusWidth);
  pInst = _apInst[Unit];
  _SPI_EnableCS(pInst);
  r = _SPI_Write(pInst, &Cmd, sizeof(Cmd));
  _SPI_DisableCS(pInst);
  return r;
}

/*********************************************************************
*
*      _QSPI_ReadData
*/
static int _QSPI_ReadData(U8 Unit, U8 Cmd, const U8 * pPara, unsigned NumBytesPara, unsigned NumBytesAddr, U8 * pData, unsigned NumBytesData, U16 BusWidth) {
  int             r;
  int             Result;
  NAND_SPI_INST * pInst;

  FS_USE_PARA(BusWidth);
  FS_USE_PARA(NumBytesAddr);
  r = 0;
  pInst = _apInst[Unit];
  _SPI_EnableCS(pInst);
  Result = _SPI_Write(pInst, &Cmd, sizeof(Cmd));
  r = (Result != 0) ? Result : r;
  if ((pPara != NULL) && (NumBytesPara != 0u)) {
    Result = _SPI_Write(pInst, pPara, NumBytesPara);
    r = (Result != 0) ? Result : r;
  }
  if ((pData != NULL) && (NumBytesData != 0u)) {
    Result = _SPI_Read(pInst, pData, NumBytesData);
    r = (Result != 0) ? Result : r;
  }
  _SPI_DisableCS(pInst);
  return r;
}

/*********************************************************************
*
*      _QSPI_WriteData
*/
static int _QSPI_WriteData(U8 Unit, U8 Cmd, const U8 * pPara, unsigned NumBytesPara, unsigned NumBytesAddr, const U8 * pData, unsigned NumBytesData, U16 BusWidth) {
  int             r;
  int             Result;
  NAND_SPI_INST * pInst;

  FS_USE_PARA(BusWidth);
  FS_USE_PARA(NumBytesAddr);
  r = 0;
  pInst = _apInst[Unit];
  _SPI_EnableCS(pInst);
  Result = _SPI_Write(pInst, &Cmd, sizeof(Cmd));
  r = (Result != 0) ? Result : r;
  if ((pPara != NULL) && (NumBytesPara != 0u)) {
    Result = _SPI_Write(pInst, pPara, NumBytesPara);
    r = (Result != 0) ? Result : r;
  }
  if ((pData != NULL) && (NumBytesData != 0u)) {
    Result = _SPI_Write(pInst, pData, NumBytesData);
    r = (Result != 0) ? Result : r;
  }
  _SPI_DisableCS(pInst);
  return r;
}

/*********************************************************************
*
*      _QSPI_Delay
*/
static void _QSPI_Delay(U8 Unit, int ms) {
  NAND_SPI_INST * pInst;

  pInst = _apInst[Unit];
  _SPI_Delay(pInst, ms);
}

/*********************************************************************
*
*      _QSPI_Lock
*/
static void _QSPI_Lock(U8 Unit) {
  NAND_SPI_INST * pInst;

  pInst = _apInst[Unit];
  _SPI_Lock(pInst);
}

/*********************************************************************
*
*      _QSPI_Unlock
*/
static void _QSPI_Unlock(U8 Unit) {
  NAND_SPI_INST * pInst;

  pInst = _apInst[Unit];
  _SPI_Unlock(pInst);
}

/*********************************************************************
*
*      _DefaultHWLayer
*/
static const FS_NAND_HW_TYPE_QSPI _DefaultHWLayer = {
  _QSPI_Init,
  _QSPI_ExecCmd,
  _QSPI_ReadData,
  _QSPI_WriteData,
  NULL,
  _QSPI_Delay,
  _QSPI_Lock,
  _QSPI_Unlock
};

/*********************************************************************
*
*       _Init
*/
static int _Init(const NAND_SPI_INST * pInst) {
  U8  Unit;
  int Freq_kHz;

  Unit = pInst->Unit;
  Freq_kHz = pInst->pHWTypeQSPI->pfInit(Unit);
  return Freq_kHz;
}

/*********************************************************************
*
*      _ExecCmd
*/
static int _ExecCmd(const NAND_SPI_INST * pInst, U8 Cmd, unsigned BusWidth) {
  U8  Unit;
  int r;

  Unit = pInst->Unit;
  r = pInst->pHWTypeQSPI->pfExecCmd(Unit, Cmd, (U8)BusWidth);
  return r;
}

/*********************************************************************
*
*       _ReadData
*/
static int _ReadData(const NAND_SPI_INST * pInst, U8 Cmd, const U8 * pPara, unsigned NumBytesPara, unsigned NumBytesAddr, U8 * pData, unsigned NumBytesData, unsigned BusWidth) {
  U8  Unit;
  int r;

  Unit = pInst->Unit;
  r = pInst->pHWTypeQSPI->pfReadData(Unit, Cmd, pPara, NumBytesPara, NumBytesAddr, pData, NumBytesData, (U16)BusWidth);
  return r;
}

/*********************************************************************
*
*      _WriteData
*/
static int _WriteData(const NAND_SPI_INST * pInst, U8 Cmd, const U8 * pPara, unsigned NumBytesPara, unsigned NumBytesAddr, const U8 * pData, unsigned NumBytesData, unsigned BusWidth) {
  U8  Unit;
  int r;

  Unit = pInst->Unit;
  r = pInst->pHWTypeQSPI->pfWriteData(Unit, Cmd, pPara, NumBytesPara, NumBytesAddr, pData, NumBytesData, (U16)BusWidth);
  return r;
}

/*********************************************************************
*
*      _Delay
*/
static void _Delay(const NAND_SPI_INST * pInst, int ms) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWTypeQSPI->pfDelay(Unit, ms);
}

/*********************************************************************
*
*      _Lock
*/
static void _Lock(const NAND_SPI_INST * pInst) {
  U8 Unit;

  Unit = pInst->Unit;
  if (pInst->pHWTypeQSPI->pfLock != NULL) {
    pInst->pHWTypeQSPI->pfLock(Unit);
  }
}

/*********************************************************************
*
*      _Unlock
*/
static void _Unlock(const NAND_SPI_INST * pInst) {
  U8 Unit;

  Unit = pInst->Unit;
  if (pInst->pHWTypeQSPI->pfUnlock != NULL) {
    pInst->pHWTypeQSPI->pfUnlock(Unit);
  }
}

/*********************************************************************
*
*      _GetFeatures
*/
static int _GetFeatures(const NAND_SPI_INST * pInst, U8 Addr, U8 * pValue) {
  unsigned BusWidth;
  int      r;

  BusWidth = FS_BUSWIDTH_MAKE(1uL, 1uL, 1uL);       // This command is always sent in SPI mode.
  r = _ReadData(pInst, CMD_GET_FEATURES, &Addr, sizeof(Addr), sizeof(Addr), pValue, sizeof(U8), BusWidth);
  return r;
}

/*********************************************************************
*
*      _SetFeatures
*/
static int _SetFeatures(const NAND_SPI_INST * pInst, U8 Addr, U8 Value) {
  unsigned BusWidth;
  int      r;

  BusWidth = FS_BUSWIDTH_MAKE(1uL, 1uL, 1uL);       // This command is always sent in SPI mode.
  r = _WriteData(pInst, CMD_SET_FEATURES, &Addr, sizeof(Addr), sizeof(Addr), &Value, sizeof(Value), BusWidth);
  return r;
}

/*********************************************************************
*
*      _ReadPageToCache
*
*  Function description
*    Reads the contents of a page from memory array to cache buffer.
*/
static int _ReadPageToCache(const NAND_SPI_INST * pInst, U32 PageIndex) {
  unsigned BusWidth;
  U8       abAddr[NUM_BYTES_ADDR];
  int      r;

  BusWidth = FS_BUSWIDTH_MAKE(1uL, 1uL, 0uL);       // This command is always sent in SPI mode.
  FS_StoreU24BE(abAddr, PageIndex);
  r = _WriteData(pInst, CMD_READ_PAGE, abAddr, sizeof(abAddr), sizeof(abAddr), NULL, 0, BusWidth);
  return r;
}

/*********************************************************************
*
*      _EraseBlock
*
*  Function description
*    Sets all the bytes in a block to 0xFF.
*/
static int _EraseBlock(const NAND_SPI_INST * pInst, U32 PageIndex) {
  unsigned BusWidth;
  U8       abAddr[NUM_BYTES_ADDR];
  int      r;

  BusWidth = FS_BUSWIDTH_MAKE(1uL, 1uL, 0uL);         // This command is always sent in SPI mode.
  FS_StoreU24BE(abAddr, PageIndex);
  r = _WriteData(pInst, CMD_ERASE_BLOCK, abAddr, sizeof(abAddr), sizeof(abAddr), NULL, 0u, BusWidth);
  return r;
}

/*********************************************************************
*
*       _CalcPlaneSelectMask
*
*  Function description
*    Calculates the bit-mask for the selection of a plane.
*
*  Parameters
*    pInst      Driver instance.
*    PageIndex  Index of the accessed page.
*
*  Return value
*    Bit-mask to be applied to the MSB of the page offset.
*
*  Additional information
*    Some NAND flash devices use the first unused bit in the 16-bit
*    page offset to select between planes. This function calculates
*    the bit-mask of that bit. This mask has to be applied to the
*    MSB of the page offset. This mask does not apply to the entire
*    16-bit offset.
*/
static unsigned _CalcPlaneSelectMask(const NAND_SPI_INST * pInst, U32 PageIndex) {
  unsigned Mask;

  Mask = 0;
  if ((PageIndex & (1uL << pInst->ldPagesPerBlock)) != 0u) {
    Mask = 1uL << ((pInst->ldBytesPerPage + 1u) - 8u);          // +1 to take into account the spare area and -8 to remove the LSB of the page offset.
  }
  return Mask;
}

/*********************************************************************
*
*      _ReadDataFromCache
*
*  Function description
*    Transfers data from NAND flash to host.
*
*  Parameters
*    pInst        Driver instance.
*    PageIndex    Index of the page to read from.
*    pData        Data read from the page.
*    Off          Byte offset to read from.
*    NumBytes     Number of bytes to be read.
*
*  Return value
*    ==0      OK, data read successfully.
*    !=0      An error occurred.
*/
static int _ReadDataFromCache(const NAND_SPI_INST * pInst, U32 PageIndex, void * pData, unsigned Off, unsigned NumBytes) {
  unsigned BusWidth;
  U8       abPara[NUM_BYTES_OFF + NUM_BYTES_DUMMY];       // According to [1] dummy bytes have to be sent to give NAND flash device time to prepare the data.
  U8       Cmd;
  int      r;
  unsigned Mask;
  unsigned Addr;

  FS_MEMSET(abPara, 0xFF, sizeof(abPara));
  FS_StoreU16BE(abPara, Off);                             // The offset is sent before the dummy bytes.
  if (pInst->ldNumPlanes != 0u) {
    Mask       = _CalcPlaneSelectMask(pInst, PageIndex);
    Addr       = abPara[0];
    Addr      |= Mask;
    abPara[0]  = (U8)Addr;
  }
  BusWidth = pInst->BusWidthRead;
  Cmd      = pInst->CmdRead;
  r = _ReadData(pInst, Cmd, abPara, sizeof(abPara), NUM_BYTES_OFF, SEGGER_PTR2PTR(U8, pData), NumBytes, BusWidth);
  return r;
}

/*********************************************************************
*
*       _ReadStatus
*
*  Function description
*    Returns the contents of the status register.
*/
static U8 _ReadStatus(const NAND_SPI_INST * pInst) {
  U8  Status;
  int r;

  Status = 0;
  r = _GetFeatures(pInst, FEAT_ADDR_STATUS, &Status);
  if (r != 0) {
    Status = STATUS_IN_PROGRESS;      // Force a timeout error.
  }
  return Status;
}

/*********************************************************************
*
*       _GetTimeOut
*
*  Function description
*    Returns the maximum number of cycles to poll the end of an operation.
*/
static U32 _GetTimeOut(const NAND_SPI_INST * pInst) {
  U32 TimeOut;

  TimeOut = pInst->TimeOut;
  return TimeOut;
}

/*********************************************************************
*
*      _EnableWrite
*/
static int _EnableWrite(const NAND_SPI_INST * pInst) {
  int r;
  U32 TimeOut;
  U8  Status;

  r = _ExecCmd(pInst, CMD_ENABLE_WRITE, 1);         // This command is always sent in SPI mode.
  if (r == 0) {
    //
    // Check that the write operation was actually enabled.
    //
    TimeOut = _GetTimeOut(pInst);
    for (;;) {
      Status = _ReadStatus(pInst);
      if ((Status & (1uL << STATUS_WRITE_ENABLED_BIT)) != 0u) {
        break;
      }
      if (TimeOut != 0u) {
        if (--TimeOut == 0u) {
          r = 1;
          break;                                    // Error, the write operation was not enabled.
        }
      }
    }
  }
  return r;
}

/*********************************************************************
*
*      _DisableWrite
*/
static int _DisableWrite(const NAND_SPI_INST * pInst) {
  int r;
  U32 TimeOut;
  U8  Status;

  r = _ExecCmd(pInst, CMD_DISABLE_WRITE, 1u);       // This command is always sent in SPI mode.
  if (r == 0) {
    //
    // Check that the write operation was actually disabled.
    //
    TimeOut = _GetTimeOut(pInst);
    for (;;) {
      Status = _ReadStatus(pInst);
      if ((Status & (1uL << STATUS_WRITE_ENABLED_BIT)) == 0u) {
        break;
      }
      if (TimeOut != 0u) {
        if (--TimeOut == 0u) {
          r = 1;
          break;                                    // Error, the write operation was not disabled.
        }
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _PollStatus
*
*  Function description
*    Returns the maximum number of cycles to poll the end of an operation.
*/
static int _PollStatus(const NAND_SPI_INST * pInst) {
  U8       Unit;
  int      r;
  unsigned BusWidth;
  U8       Addr;

  r    = -1;                                        // Feature not supported by the HW layer.
  Unit = pInst->Unit;
  if (pInst->pHWTypeQSPI->pfPoll != NULL) {
    Addr     = FEAT_ADDR_STATUS;
    BusWidth = FS_BUSWIDTH_MAKE(1uL, 1uL, 1uL);     // All the data is sent and received in standard SPI mode.
    r = pInst->pHWTypeQSPI->pfPoll(Unit,
                                   CMD_GET_FEATURES,
                                   &Addr,
                                   sizeof(Addr),
                                   STATUS_IN_PROGRESS_BIT,
                                   0,               // The NAND flash device sets the "IN PROGRESS" bit to 0 when ready.
                                   FS_NAND_DEVICE_OPERATION_POLL_DELAY,
                                   FS_NAND_DEVICE_OPERATION_TIMEOUT,
                                   (U16)BusWidth);
  }
  return r;
}

/*********************************************************************
*
*       _WaitForEndOfOperation
*
*  Function description
*    Waits for the NAND to complete its last operation.
*
*  Return value
*    The contents of the status register or a negative value on a timeout error.
*/
static int _WaitForEndOfOperation(const NAND_SPI_INST * pInst) {
  U8  Status;
  U32 TimeOut;
  int r;

  r = _PollStatus(pInst);
  if (r == 0) {
    Status = _ReadStatus(pInst);
    return (int)Status;           // The NAND flash device is ready.
  }
  if (r == 1) {
    return -1;                    // Error, the NAND flash device does not respond.
  }
  //
  // Polling by HW not supported. Do it here in the software.
  //
  TimeOut = _GetTimeOut(pInst);
  for (;;) {
    Status = _ReadStatus(pInst);
    if ((Status & STATUS_IN_PROGRESS) == 0u) {
      return (int)Status;
    }
    if (TimeOut != 0u) {
      if (--TimeOut == 0u) {
        return -1;                // Error, the NAND flash device does not respond.
      }
    }
  }
}

/*********************************************************************
*
*       _Reset
*
*  Function description
*    Resets the NAND flash by command
*/
static int _Reset(NAND_SPI_INST * pInst) {
  int r;
  int Result;

  r = 0;
  _SetCachePageIndex(pInst, PAGE_INDEX_INVALID);
  pInst->DieIndexSelected = 0;
  Result = _ExecCmd(pInst, CMD_RESET, 1u);     // This command is always send in SPI mode.
  r = (Result != 0) ? Result : r;
  //
  // According to [1] the next command can be issued only after a 1 ms delay.
  //
  _Delay(pInst, FS_NAND_RESET_TIME);
  //
  // The Micron MT29F1G01ABAFD device indicates that is ready
  // after reset by setting the STATUS_IN_PROGRESS bit in
  // the status register.
  //
  Result = _WaitForEndOfOperation(pInst);
  r = (Result != 0) ? Result : r;
  return r;
}

/*********************************************************************
*
*      _WritePageFromCache
*
*  Function description
*    Writes the contents of a page from cache buffer to memory array.
*/
static int _WritePageFromCache(const NAND_SPI_INST * pInst, U32 PageIndex) {
  unsigned BusWidth;
  U8       abAddr[NUM_BYTES_ADDR];
  int      r;

  FS_StoreU24BE(abAddr, PageIndex);
  BusWidth = FS_BUSWIDTH_MAKE(1uL, 1uL, 0uL);         // This command is always sent in SPI mode.
  r = _WriteData(pInst, CMD_EXEC_PROG, abAddr, sizeof(abAddr), sizeof(abAddr), NULL, 0, BusWidth);
  return r;
}

/*********************************************************************
*
*      _WriteDataToCache
*
*  Function description
*    Transfers data from host to NAND flash.
*/
static int _WriteDataToCache(const NAND_SPI_INST * pInst, U32 PageIndex, const void * pData, unsigned Off, unsigned NumBytes) {
  unsigned BusWidth;
  U8       abAddr[NUM_BYTES_OFF];
  U8       Cmd;
  int      r;
  unsigned Mask;
  unsigned Addr;

  FS_StoreU16BE(abAddr, Off);
  if (pInst->ldNumPlanes != 0u) {
    Mask       = _CalcPlaneSelectMask(pInst, PageIndex);
    Addr       = abAddr[0];
    Addr      |= Mask;
    abAddr[0]  = (U8)Addr;
  }
  BusWidth = pInst->BusWidthWrite;
  Cmd      = pInst->CmdWrite;
  r = _WriteData(pInst, Cmd, abAddr, sizeof(abAddr), sizeof(abAddr), SEGGER_CONSTPTR2PTR(const U8, pData), NumBytes, BusWidth);
  return r;
}

/*********************************************************************
*
*      _CheckONFISignature
*
*  Function description
*    Checks if the signature of the NAND flash parameters is valid.
*
*  Return value
*    ==0    OK, the signature is valid.
*    !=0    The signature is not valid.
*/
static int _CheckONFISignature(const U8 * pData) {
  if ((*(pData + 0) == (U8)'O') &&
      (*(pData + 1) == (U8)'N') &&
      (*(pData + 2) == (U8)'F') &&
      (*(pData + 3) == (U8)'I')) {
    return 0;        // OK, the ONFI signature is valid.
  }
  //
  // The Toshiba TC58CVG1S3HxAIx serial NAND flash device does not send a valid
  // ONFI signature/ but the layout of the following parameters matches the ONFI
  // specification.
  //
  if ((*(pData + 0) == 0u) &&
      (*(pData + 1) == 0u) &&
      (*(pData + 2) == 0u) &&
      (*(pData + 3) == 0u)) {
    return 0;        // OK, the ONFI signature is valid.
  }
  if ((*(pData + 0) == (U8)'N') &&
      (*(pData + 1) == (U8)'A') &&
      (*(pData + 2) == (U8)'N') &&
      (*(pData + 3) == (U8)'D')) {
    return 0;        // OK, the ONFI signature is valid.
  }
  return 1;          // Not a valid ONFI signature.
}

/*********************************************************************
*
*       _ReadONFIPara
*
*  Function description
*    Reads parameters from the ONFI parameter page.
*
*  Parameters
*    pInst        Physical layer instance.
*    PageIndex    Index of the parameter page in the OTP area.
*    pDevicePara  [OUT] Information read from the parameter page.
*
*  Return value
*    ==0    ONFI parameters read.
*    !=0    An error occurred.
*
*  Additional information
*    A page has 256 bytes. The data integrity is checked using CRC.
*/
static int _ReadONFIPara(const NAND_SPI_INST * pInst, unsigned PageIndex, NAND_SPI_PARA * pDevicePara) {
  int r;
  U16 crcRead;
  U16 crcCalc;
  U32 NumLoops;
  int iParaPage;
  int iByte;
  U8  acBuffer[4];
  U8  IsValid;
  U8  OTPFeat;
  int Status;
  U32 Off;
  int Result;

  crcCalc = 0;
  pDevicePara->ECCInfo.NumBitsCorrectable = 0;                          // Information not available
  pDevicePara->ECCInfo.ldBytesPerBlock    = 9;                          // 512 byte ECC block.
  r = _GetFeatures(pInst, FEAT_ADDR_OTP, &OTPFeat);                     // Save the current features.
  if (r == 0) {
    r = _SetFeatures(pInst, FEAT_ADDR_OTP, OTPFeat | FEAT_OTP_ENABLE);  // Enable the access to ONFI parameters.
    if (r == 0) {
      //
      // Copy the ONFI parameters to cache buffer.
      //
      r = _ReadPageToCache(pInst, PageIndex);
      if (r == 0) {
        //
        // Check the result of the read operation.
        //
        Status = _WaitForEndOfOperation(pInst);
        if (Status >= 0) {                      // No timeout error?
          //
          // We do not check for ECC errors via _IsReadError() here
          // because the ONFI data is not protected by ECC and for
          // some devices (e.g. Micron MT29F1G01ABAFD) the read status
          // is set to reflect the contents of block 0, page 0.
          // As a consequence an ECC error in this page would make it
          // impossible to correctly identify the NAND flash device.
          //
          Off      = 0;
          IsValid  = 0;
          //
          // Multiple identical parameter pages are stored in a device.
          // Read from the first one which stores valid information.
          //
          for (iParaPage = 0; iParaPage < NUM_ONFI_PAGES; ++iParaPage) {
            iByte    = 0;
            IsValid  = 0;
            NumLoops = (ONFI_PAGE_SIZE - sizeof(crcRead)) / sizeof(acBuffer);
            do {
              r = _ReadDataFromCache(pInst, PageIndex, acBuffer, Off, sizeof(acBuffer));
              if (r != 0) {
                break;
              }
              Off += sizeof(acBuffer);
              if (iByte == 0) {
                //
                // Check the signature.
                //
                if (_CheckONFISignature(acBuffer) == 0) {
                  IsValid = 1;                // Valid parameter page.
                }
              } else if (iByte == 64) {
                pDevicePara->MfgId                      = acBuffer[0];
              } else if (iByte == 80) {
                pDevicePara->BytesPerPage               = FS_LoadU32LE(&acBuffer[0]);
              } else if (iByte == 84) {
                pDevicePara->BytesPerSpareArea          = FS_LoadU16LE(&acBuffer[0]);
              } else if (iByte == 92) {
                pDevicePara->PagesPerBlock              = FS_LoadU32LE(&acBuffer[0]);
              } else if (iByte == 96) {
                pDevicePara->NumBlocks                  = FS_LoadU32LE(&acBuffer[0]);
              } else if (iByte == 100) {
                pDevicePara->NumDies                    = acBuffer[0];
              } else if (iByte == 112) {
                pDevicePara->ECCInfo.NumBitsCorrectable = acBuffer[0];
              } else if (iByte == 248) {
                //
                // Micron MT29F1G01ABAFD reports in the vendor specific area
                // the error correction capability of the HW ECC with the
                // "Number of ECC bits" (offset 112) being set to 0.
                //
                if (pDevicePara->MfgId == MFG_ID_MICRON) {
                  pDevicePara->ECCInfo.NumBitsCorrectable = acBuffer[0];
                }
              } else {
                //
                // These ONFI parameters are not interesting for the file system.
                //
              }
              //
              // Accumulate the CRC of parameter values.
              //
              if (iByte == 0) {
                crcCalc = ONFI_CRC_INIT;
              }
              crcCalc = FS_CRC16_CalcBitByBit(acBuffer, sizeof(acBuffer), crcCalc, ONFI_CRC_POLY);
              iByte += (int)sizeof(acBuffer);
            } while (--NumLoops != 0u);
            //
            // Quit the read loop on error.
            //
            if (r != 0) {
              break;
            }
            //
            // Read the last 2 bytes and the CRC.
            //
            r = _ReadDataFromCache(pInst, PageIndex, acBuffer, Off, sizeof(acBuffer));
            if (r != 0) {
              break;                  // Error, could not read data from NAND flash device.
            }
            if (IsValid != 0u) {      // Signature OK?
              //
              // Verify the CRC.
              //
              crcCalc = FS_CRC16_CalcBitByBit(&acBuffer[0], 2, crcCalc, ONFI_CRC_POLY);
              crcRead = FS_LoadU16LE(&acBuffer[2]);
              if (crcCalc == crcRead) {
                r = 0;
                break;
              }
              //
              // Winbond devices store the CRC in big-endian format
              //
              crcRead = FS_LoadU16BE(&acBuffer[2]);
              if (crcCalc == crcRead) {
                r = 0;
                break;
              }
            }
          }
          if (IsValid == 0u) {
            r = 1;                      // Error, no valid parameter page found.
          }
        }
      }
    }
    Result = _SetFeatures(pInst, FEAT_ADDR_OTP, OTPFeat);   // Restore the old features.
    if (Result != 0) {
      r = Result;
    }
  }
  return r;
}

/*********************************************************************
*
*       _ReadECCStatus
*
*  Function description
*    Reads the ECC correction status. This command is only supported by Macronix devices.
*/
static int _ReadECCStatus(const NAND_SPI_INST * pInst, U8 * pStatus) {
  U8       Dummy;
  unsigned BusWidth;
  int      r;

  Dummy    = 0;
  BusWidth = FS_BUSWIDTH_MAKE(1uL, 1uL, 1uL);       // This command is always sent in SPI mode.
  //
  // - Send the command byte
  // - Send a dummy byte
  // - Read the ECC status
  //
  r = _ReadData(pInst, CMD_READ_ECC_STATUS, &Dummy, sizeof(Dummy), sizeof(Dummy), pStatus, sizeof(*pStatus), BusWidth);
  return r;
}

/*********************************************************************
*
*       _ReadIdDefault
*
*  Function description
*    Executes the READ ID command and reads the data returned by the NAND flash.
*
*  Parameters
*    pInst        Physical layer instance.
*    pDeviceId    [OUT] Response to READ ID command.
*    NumBytes     Maximum number of bytes to store to pDeviceId.
*
*  Return value
*    ==0     OK, device id read successfully.
*    !=0     An error occurred.
*
*  Additional information
*    The command sequence looks like this: CMD_READ_ID DummyByte MfgId DeviceId
*/
static int _ReadIdDefault(const NAND_SPI_INST * pInst, U8 * pDeviceId, unsigned NumBytes) {
  U8       Dummy;
  unsigned BusWidth;
  int      r;

  Dummy = 0;
  BusWidth = FS_BUSWIDTH_MAKE(1uL, 1uL, 1uL);      // This command is always sent in SPI mode.
  r = _ReadData(pInst, CMD_READ_ID, &Dummy, sizeof(Dummy), sizeof(Dummy), pDeviceId, NumBytes, BusWidth);
  return r;
}

/*********************************************************************
*
*       _ReadIdEnhanced
*
*  Function description
*    Executes the READ ID command and reads the data returned by the NAND flash.
*
*  Parameters
*    pInst        Physical layer instance.
*    pDeviceId    [OUT] Response to READ ID command.
*    NumBytes     Maximum number of bytes to store to pDeviceId.
*
*  Return value
*    ==0     OK, device id read successfully.
*    !=0     An error occurred.
*
*  Additional information
*    The command sequence looks like this: CMD_READ_ID MfgId DeviceId1 DeviceId2
*    Typically, this type of command sequence is used by GigaDevice NAND flash devices.
*/
static int _ReadIdEnhanced(const NAND_SPI_INST * pInst, U8 * pDeviceId, unsigned NumBytes) {
  unsigned BusWidth;
  int      r;

  BusWidth = FS_BUSWIDTH_MAKE(1uL, 1uL, 1uL);      // This command is always sent in SPI mode.
  r = _ReadData(pInst, CMD_READ_ID, NULL, 0, 0, pDeviceId, NumBytes, BusWidth);
  return r;
}

/*********************************************************************
*
*       _ReadId
*
*  Function description
*    Executes the READ ID command and reads the data returned by the NAND flash.
*/
static int _ReadId(const NAND_SPI_INST * pInst, U8 * pDeviceId, unsigned NumBytes, int DeviceIdType) {
  int r;

  switch (DeviceIdType) {
  case DEVICE_ID_TYPE_ENHANCED:
    r = _ReadIdEnhanced(pInst, pDeviceId, NumBytes);
    break;
  case DEVICE_ID_TYPE_STANDARD:
    //lint through
  default:
    r = _ReadIdDefault(pInst, pDeviceId, NumBytes);
    break;
  }
  return r;
}

#if FS_SUPPORT_TEST

/*********************************************************************
*
*       _IsECCEnabled
*/
static U8 _IsECCEnabled(const NAND_SPI_INST * pInst) {
  U8  Feat;
  U8  r;
  int Result;

  r = 0;
  Result = _GetFeatures(pInst, FEAT_ADDR_OTP, &Feat);
  if (Result == 0) {
    if ((Feat & FEAT_ECC_ENABLE) != 0u) {
      r = 1;
    }
  }
  return r;
}

#endif // FS_SUPPORT_TEST

/*********************************************************************
*
*       _EnableECC
*
*  Function description
*    Activates the internal HW ECC of NAND flash device.
*
*  Notes
*    (1) A read-modify-write operation is required since more than
*        one feature is stored in a parameter.
*/
static int _EnableECC(NAND_SPI_INST * pInst) {
  U8  Feat;
  int r;

  r = _GetFeatures(pInst, FEAT_ADDR_OTP, &Feat);        // Note 1
  if (r == 0) {
    if ((Feat & FEAT_ECC_ENABLE) == 0u) {
      Feat |= FEAT_ECC_ENABLE;
      r = _SetFeatures(pInst, FEAT_ADDR_OTP, Feat);
    }
  }
  if (r == 0) {
    ASSERT_IS_ECC_ENABLED(pInst);
    pInst->IsECCEnabled = 1;
  }
  return r;
}

/*********************************************************************
*
*       _DisableECC
*
*  Function description
*    Deactivates the internal HW ECC of NAND flash device.
*
*  Notes
*    (1) A read-modify-write operation is required since more than
*        one feature is stored in a parameter.
*/
static int _DisableECC(NAND_SPI_INST * pInst) {
  U8       Feat;
  int      r;
  unsigned v;

  r = _GetFeatures(pInst, FEAT_ADDR_OTP, &Feat);        // Note 1
  if (r == 0) {
    if ((Feat & FEAT_ECC_ENABLE) != 0u) {
      v = Feat;
      v &= ~FEAT_ECC_ENABLE;
      Feat = (U8)v;
      r = _SetFeatures(pInst, FEAT_ADDR_OTP, Feat);
    }
  }
  if (r == 0) {
    ASSERT_IS_ECC_DISABLED(pInst);
    pInst->IsECCEnabled = 0;
  }
  return r;
}

/*********************************************************************
*
*      _ReadApplyParaEx
*
*  Function description
*    Reads parameters from NAND flash device and stores them to instance.
*
*  Parameters
*    pInst        Driver instance.
*    pId          Device id.
*    PageIndex    Index of the OTP page where the ONFI parameters are stored.
*
*  Return value
*    ==0        OK, parameters successfully read.
*    !=0        An error occurred.
*
*  Additional information
*    This function can read only ONFI information. If the NAND flash
*    device does not support ONFI then the parameters have to be stored
*    based on the id of the device in the device identification function.
*/
static int _ReadApplyParaEx(NAND_SPI_INST * pInst, const U8 * pId, unsigned PageIndex) {
  int           r;
  NAND_SPI_PARA Para;
  U8            ldBytesPerPage;
  U8            CmdRead;
  U8            CmdWrite;
  unsigned      BusWidthRead;
  unsigned      BusWidthWrite;

  FS_USE_PARA(pId);
  FS_MEMSET(&Para, 0, sizeof(Para));
  r = _ReadONFIPara(pInst, PageIndex, &Para);
  if (r == 0) {
    ldBytesPerPage = (U8)_ld(Para.BytesPerPage);
    pInst->ldBlocksPerDie          = (U8)_ld(Para.NumBlocks);
    pInst->ldNumPlanes             = 0;                           // This information is not part of the ONFI parameters and has to be determined separately.
    pInst->ldPagesPerBlock         = (U8)_ld(Para.PagesPerBlock);
    pInst->NumBitErrorsCorrectable = Para.ECCInfo.NumBitsCorrectable;
    pInst->ldBytesPerPage          = ldBytesPerPage;
    pInst->ldNumECCBlocks          = ldBytesPerPage - Para.ECCInfo.ldBytesPerBlock;
    pInst->BytesPerSpareArea       = Para.BytesPerSpareArea;
    pInst->ldNumDies               = (U8)_ld(Para.NumDies);
    //
    // Configure the commands for reading and writing data fast.
    //
    CmdRead       = pInst->CmdRead;
    BusWidthRead  = pInst->BusWidthRead;
    CmdWrite      = pInst->CmdWrite;
    BusWidthWrite = pInst->BusWidthWrite;
    if (pInst->Allow2bitMode != 0u) {
      CmdRead       = CMD_READ_DATA_X2;
      BusWidthRead  = FS_BUSWIDTH_MAKE(1uL, 1uL, 2uL);
    }
    if (pInst->Allow4bitMode != 0u) {
      CmdRead       = CMD_READ_DATA_X4;
      BusWidthRead  = FS_BUSWIDTH_MAKE(1uL, 1uL, 4uL);
      CmdWrite      = CMD_LOAD_PROG_RAND_X4;
      BusWidthWrite = FS_BUSWIDTH_MAKE(1uL, 1uL, 4uL);
    }
    pInst->CmdRead       = CmdRead;
    pInst->BusWidthRead  = (U16)BusWidthRead;
    pInst->CmdWrite      = CmdWrite;
    pInst->BusWidthWrite = (U16)BusWidthWrite;
    //
    // Unlock all the device blocks.
    //
    r = _SetFeatures(pInst, FEAT_ADDR_BLOCK_LOCK, 0);
    if (r == 0) {
      //
      // Initially, access memory array without HW ECC. The HW ECC will be enabled by
      // the Universal NAND driver as needed. Doing this allows us to use the software
      // ECC to correct bit errors if required.
      //
      r = _DisableECC(pInst);
    }
  }
  return r;
}

/*********************************************************************
*
*      _ReadApplyPara
*
*  Function description
*    Reads parameters from NAND flash device and stores them to instance.
*
*  Parameters
*    pInst        Driver instance.
*    pId          Device id.
*
*  Return value
*    ==0        OK, parameters successfully read.
*    !=0        An error occurred.
*
*  Additional information
*    This function performs the same operation as _ReadApplyParaEx()
*    with the difference that is uses a fixed page index value.
*    The page index used by this function works with the NAND flash
*    devices from all the manufacturers with the exception of devices
*    from Alliance Memory that use a different value.
*/
static int _ReadApplyPara(NAND_SPI_INST * pInst, const U8 * pId) {
  int r;

  r = _ReadApplyParaEx(pInst, pId, PAGE_INDEX_ONFI);
  return r;
}

/*********************************************************************
*
*       _GetECCResult
*
*  Function description
*    Returns the result of the ECC correction status.
*
*  Return value
*    ==0      OK, status returned
*    !=0      An error occurred
*/
static int _GetECCResult(const NAND_SPI_INST * pInst, FS_NAND_ECC_RESULT * pResult) {
  U8  Status;
  U8  CorrectionStatus;
  U8  MaxNumBitErrorsCorrected;
  int r;

  r                        = 0;
  CorrectionStatus         = FS_NAND_CORR_NOT_APPLIED;
  MaxNumBitErrorsCorrected = 0;                         // Set to indicate that the device is not able to report the number of bit errors corrected.
  //
  // Read the status of the last page read operation to find out if any uncorrectable bit errors occurred.
  //
  Status = _ReadStatus(pInst);
  if ((Status & STATUS_IN_PROGRESS) != 0u) {
    r = 1;          // Could not read status.
  } else {
    if (_IsReadError(Status) != 0) {
      CorrectionStatus = FS_NAND_CORR_FAILURE;
    } else {
      Status &= STATUS_READ_ERROR_MASK;
      if (Status == STATUS_READ_ERROR_CORRECTED) {
        CorrectionStatus = FS_NAND_CORR_APPLIED;
        if (pInst->NumBitErrorsCorrectable == 1u) {
          MaxNumBitErrorsCorrected = 1;
        }
      }
    }
  }
  //
  // Return the calculated values.
  //
  pResult->CorrectionStatus    = CorrectionStatus;
  pResult->MaxNumBitsCorrected = MaxNumBitErrorsCorrected;
  return r;
}

/*********************************************************************
*
*       _IsDataRelocationRequired
*
*  Function description
*    Checks if the specified data has to be moved to a different position.
*
*  Parameters
*    pInst      Physical layer instance.
*    Off        Offset of the first byte to be accessed.
*    NumBytes   Number of bytes to be accessed.
*
*  Return value
*    ==0    Data does not have to be relocated.
*    !=0    Data has to be relocated.
*
*  Additional information
*    This function is called at the beginning of a read or write operation
*    to check if the specified data is located at a different position
*    in a NAND page than on the buffer used by the NAND driver.
*    Typically, this is the case with the data stored in the spare area
*    which has to be stored to a specific location so that it is covered
*    by the HW ECC.
*
*    Off is relative to the beginning of the page. That is, the offset
*    of the first byte in the spare area of a NAND flash with 2KiB pages
*    is 2048.
*/
static int _IsDataRelocationRequired(const NAND_SPI_INST * pInst, unsigned Off, unsigned NumBytes) {
  int      r;
  unsigned BytesPerPage;
  unsigned NumECCBlocks;
  unsigned OffEnd;
  unsigned OffUserDataStart;
  unsigned OffUserDataEnd;
  unsigned BytesPerSpareStripe;

  r = 0;
  if (pInst->IsECCEnabled != 0u) {
    BytesPerPage        = 1uL << pInst->ldBytesPerPage;
    NumECCBlocks        = 1uL << pInst->ldNumECCBlocks;
    BytesPerSpareStripe = (unsigned)pInst->BytesPerSpareArea >> pInst->ldNumECCBlocks;
    OffEnd              = Off + NumBytes;
    OffUserDataStart    = BytesPerPage + OFF_USER_DATA;
    OffUserDataEnd      = BytesPerPage + OFF_USER_DATA + NUM_BYTES_USER_DATA;
    do {
      if ((Off < OffUserDataEnd) && (OffEnd > OffUserDataStart)) {
        r = 1;
        break;
      }
      OffUserDataEnd   += BytesPerSpareStripe;
      OffUserDataStart += BytesPerSpareStripe;
    } while (--NumECCBlocks != 0u);
  }
  return r;
}

#if FS_SUPPORT_TEST

/*********************************************************************
*
*      _WriteDataToCacheWithECCPreserved
*
*  Function description
*    Writes data to spare area.
*
*  Parameters
*    pInst        Physical layer instance.
*    PageIndex    Index of the page to write to.
*    pData        [IN] Data to be written.
*    Off          Byte offset relative to the beginning of the page.
*    NumBytes     Number of bytes to be written.
*
*  Return value
*    ==0      OK, data written successfully.
*    !=0      An error occurred.
*
*  Additional information
*    Toshiba, GigaDevice and some Winbond NAND flash devices store the ECC on the
*    second half of the spare area which is 128 bytes large.
*    During the testing we have to write 0xFF to the area where the ECC
*    is stored in order to make sure that the existing ECC is preserved.
*/
static int _WriteDataToCacheWithECCPreserved(const NAND_SPI_INST * pInst, U32 PageIndex, const void * pData, unsigned Off, unsigned NumBytes) {
  int      r;
  U32      aSpareArea[MAX_SPARE_AREA_SIZE / 4];
  unsigned BytesPerSpareArea;

  ASSERT_ENTIRE_SPARE_AREA(pInst, Off, NumBytes);
  if (pInst->IsECCEnabled == 0u) {
    FS_MEMSET(aSpareArea, 0xFF, sizeof(aSpareArea));
    FS_MEMCPY(aSpareArea, pData, NumBytes);
    BytesPerSpareArea = pInst->BytesPerSpareArea;
    if (BytesPerSpareArea == 72u) {                     // This is for Alliance Memory 2Gb and 4Gb devices.
      NumBytes = 128;
    } else {
      if (BytesPerSpareArea == 144u) {                  // This is for Alliance Memory 8Gb devices.
        NumBytes = 256;
      } else {
        NumBytes = BytesPerSpareArea << 1;              // Typically, the ECC parity checksum is stored in the second half of the spare area.
      }
    }
    pData = SEGGER_PTR2PTR(U32, aSpareArea);
  }
  r = _WriteDataToCache(pInst, PageIndex, pData, Off, NumBytes);
  return r;
}

#endif // FS_SUPPORT_TEST

/*********************************************************************
*
*      _ISSI_RelocateSpareAreaData
*
*  Function description
*    Swaps data in the spare are according to the layout of
*    ISSI IS37SML01G1 and IS38SML01G1 devices.
*
*  Additional information
*    The ISSI IS37SML01G1 and IS38SML01G1 have a different layout
*    of the spare area than the supported devices form other
*    manufacturers. The user data has to be stored in the last
*    8 bytes of a spare area stripe. The layout of a spare area
*    stripe looks like this:
*
*      0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
*    +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*    | B | e | e | e | E | E | E | E | U | U | U | U | U | U | U | U |
*    +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*
*    B[1] - stores the bad block marker (only for the first stripe).
*    e[3] - ECC for the page data (generated by HW ECC of NAND flash device).
*    E[4] - ECC for the spare area (generated by HW ECC of NAND flash device).
*    U[8] - User data (NAND driver data can be stored here).
*
*    The Universal NAND driver stores the data to byte offsets 4-7
*    therefore we have to relocate this data to byte offsets 8-B
*    to prevent that the HW ECC overwrites it.
*/
static void _ISSI_RelocateSpareAreaData(const NAND_SPI_INST * pInst, U32 * pData) {
  unsigned   NumECCBlocks;
  unsigned   OffUserDataISSI;
  unsigned   OffUserData;
  unsigned   BytesPerSpareStripe;
  U8         Data8;
  U8       * pData8;
  unsigned   NumBytes;

  NumECCBlocks        = 1uL << pInst->ldNumECCBlocks;
  BytesPerSpareStripe = (unsigned)pInst->BytesPerSpareArea >> pInst->ldNumECCBlocks;
  OffUserDataISSI     = OFF_USER_DATA_ISSI;
  OffUserData         = OFF_USER_DATA;
  pData8              = SEGGER_PTR2PTR(U8, pData);
  do {
    NumBytes = NUM_BYTES_USER_DATA;
    do {
      Data8                       = *(pData8 + OffUserData);
      *(pData8 + OffUserData)     = *(pData8 + OffUserDataISSI);
      *(pData8 + OffUserDataISSI) = Data8;
      ++OffUserData;
      ++OffUserDataISSI;
    } while (--NumBytes != 0u);
    OffUserData     += BytesPerSpareStripe - NUM_BYTES_USER_DATA;
    OffUserDataISSI += BytesPerSpareStripe - NUM_BYTES_USER_DATA;
  } while (--NumECCBlocks != 0u);
}

/*********************************************************************
*
*      _ISSI_CalcUserDataSpareOff
*
*  Function description
*    Calculates the offset where the data is actually stored to spare area.
*
*  Parameters
*    pInst      Physical layer instance.
*    Off        Byte offset used by the Universal NAND driver.
*    NumBytes   Number of bytes to read from the specified offset.
*
*  Return value
*    ==0        Entire spare area has to be read.
*    !=0        Actual byte offset to read from.
*
*  Additional information
*    See _ISSI_RelocateSpareAreaData() for information about how the
*    data is stored to spare area.
*/
static unsigned _ISSI_CalcUserDataSpareOff(const NAND_SPI_INST * pInst, unsigned Off, unsigned NumBytes) {
  unsigned NumECCBlocks;
  unsigned OffUserDataISSI;
  unsigned OffUserData;
  unsigned BytesPerSpareStripe;
  unsigned BytesPerPage;

  BytesPerPage        = 1uL << pInst->ldBytesPerPage;
  NumECCBlocks        = 1uL << pInst->ldNumECCBlocks;
  BytesPerSpareStripe = (unsigned)pInst->BytesPerSpareArea >> pInst->ldNumECCBlocks;
  OffUserDataISSI     = OFF_USER_DATA_ISSI;
  OffUserData         = OFF_USER_DATA;
  if (NumBytes == NUM_BYTES_USER_DATA) {
    if (Off >= BytesPerPage) {
      Off -= BytesPerPage;
      do {
        if (Off == OffUserData) {
          Off = BytesPerPage + OffUserDataISSI;
          return Off;
        }
        OffUserData     += BytesPerSpareStripe;
        OffUserDataISSI += BytesPerSpareStripe;
      } while (--NumECCBlocks != 0u);
    }
  }
  return 0;             // No access to a user data in the spare area.
}

/*********************************************************************
*
*      _ISSI_Identify
*/
static int _ISSI_Identify(NAND_SPI_INST * pInst, const U8 * pId) {
  int r;
  U8  MfgId;
  U8  DeviceId;

  r        = 1;           // Device not supported.
  MfgId    = *pId;        // The first byte is the manufacturer id.
  DeviceId = *(pId + 1);
  if (MfgId == MFG_ID_ISSI) {
    //
    // The following ISSI devices are supported:
    //
    // Id          Device
    // ------------------
    // 0xC8 0x21   ISSI IS37SML01G1, IS38SML01G1
    //
    if (DeviceId == 0x21u) {
      pInst->ldBytesPerPage          = 11;   // 2048 bytes
      pInst->ldPagesPerBlock         = 6;    // 64 pages
      pInst->ldBlocksPerDie          = 10;   // 1024 blocks
      pInst->ldNumDies               = 0;
      pInst->BytesPerSpareArea       = 64;
      pInst->NumBitErrorsCorrectable = 1;
      pInst->ldNumECCBlocks          = 2;
      pInst->HasHW_ECC               = 1;
      r = 0;              // This device is supported.
    }
  }
  return r;
}

/*********************************************************************
*
*      _ISSI_ReadApplyPara
*
*  Function description
*    Prepares the NAND flash device for data access.
*/
static int _ISSI_ReadApplyPara(NAND_SPI_INST * pInst, const U8 * pId) {
  int      r;
  U8       CmdRead;
  U8       CmdWrite;
  unsigned BusWidthRead;
  unsigned BusWidthWrite;

  FS_USE_PARA(pId);
  //
  // Configure the commands for reading and writing data fast.
  //
  CmdRead       = pInst->CmdRead;
  BusWidthRead  = pInst->BusWidthRead;
  CmdWrite      = pInst->CmdWrite;
  BusWidthWrite = pInst->BusWidthWrite;
  if (pInst->Allow2bitMode != 0u) {
    CmdRead       = CMD_READ_DATA_X2;
    BusWidthRead  = FS_BUSWIDTH_MAKE(1uL, 1uL, 2uL);
  }
  if (pInst->Allow4bitMode != 0u) {
    CmdRead       = CMD_READ_DATA_X4;
    BusWidthRead  = FS_BUSWIDTH_MAKE(1uL, 1uL, 4uL);
    CmdWrite      = CMD_LOAD_PROG_RAND_X4;
    BusWidthWrite = FS_BUSWIDTH_MAKE(1uL, 1uL, 4uL);
  }
  pInst->CmdRead       = CmdRead;
  pInst->BusWidthRead  = (U16)BusWidthRead;
  pInst->CmdWrite      = CmdWrite;
  pInst->BusWidthWrite = (U16)BusWidthWrite;
  //
  // Unlock all the device blocks.
  //
  r = _SetFeatures(pInst, FEAT_ADDR_BLOCK_LOCK, 0);
  if (r == 0) {
    //
    // Initially, access memory array without HW ECC. The HW ECC will be enabled by
    // the Universal NAND driver as needed. Doing this allows us to use the software
    // ECC to correct bit errors if required.
    //
    r = _SetFeatures(pInst, FEAT_ADDR_OTP, 0);
    if (r == 0) {
      if (pInst->BytesPerSpareArea > (unsigned)(MAX_SPARE_AREA_SIZE)) {
        r = 1;                    // Spare area buffer too small.
      }
    }
  }
  return r;
}

/*********************************************************************
*
*      _ISSI_ReadDataFromCache
*
*  Function description
*    Transfers data from the internal page register of the NAND flash
*    device to host.
*
*  Parameters
*    pInst        Physical layer instance.
*    PageIndex    Index of the NAND page to be read.
*    pData        [OUT] Data read from the page.
*    Off          Offset of the first byte to be read.
*    NumBytes     Number of bytes to be read.
*
*  Return value
*    ==0      OK, data read successfully.
*    !=0      An error occurred.
*/
static int _ISSI_ReadDataFromCache(const NAND_SPI_INST * pInst, U32 PageIndex, void * pData, unsigned Off, unsigned NumBytes) {
  int        r;
  U32        aSpareArea[MAX_SPARE_AREA_SIZE / 4];
  unsigned   BytesPerPage;
  unsigned   BytesPerSpareArea;
  unsigned   NumBytesToRead;
  unsigned   OffCalc;
  U8       * pData8;

  if (_IsDataRelocationRequired(pInst, Off, NumBytes) == 0) {
    r = _ReadDataFromCache(pInst, PageIndex, pData, Off, NumBytes);
  } else {
    //
    // Data has to be relocated. First, process requests that read
    // the entire data stored in a spare area stripe. This type of
    // requests are generated by the Universal NAND driver when
    // FS_NAND_OPTIMIZE_SPARE_AREA_READ is set to 1.
    //
    OffCalc = _ISSI_CalcUserDataSpareOff(pInst, Off, NumBytes);
    if (OffCalc != 0u) {
      r = _ReadDataFromCache(pInst, PageIndex, pData, OffCalc, NumBytes);
    } else {
      r = 0;
      BytesPerPage      = 1uL << pInst->ldBytesPerPage;
      BytesPerSpareArea = pInst->BytesPerSpareArea;
      if (Off < BytesPerPage) {
        //
        // Read bytes from main area.
        //
        pData8         = SEGGER_PTR2PTR(U8, pData);
        NumBytesToRead = BytesPerPage - Off;
        NumBytesToRead = SEGGER_MIN(NumBytes, NumBytesToRead);
        r = _ReadDataFromCache(pInst, PageIndex, pData8, Off, NumBytesToRead);
        Off      += NumBytesToRead;
        NumBytes -= NumBytesToRead;
        pData8   += NumBytesToRead;
        pData     = pData8;
      }
      if (r == 0) {
        if (NumBytes != 0u) {
          Off -= BytesPerPage;
          //
          // Read data from the spare area.
          //
          FS_MEMSET(aSpareArea, 0xFF, sizeof(aSpareArea));
          r = _ReadDataFromCache(pInst, PageIndex, aSpareArea, BytesPerPage, BytesPerSpareArea);
          if (r == 0) {
            _ISSI_RelocateSpareAreaData(pInst, aSpareArea);
            pData8 = SEGGER_PTR2PTR(U8, aSpareArea);
            FS_MEMCPY(pData, pData8 + Off, NumBytes);
          }
        }
      }
    }
  }
  return r;
}

/*********************************************************************
*
*      _ISSI_WriteDataToCache
*
*  Function description
*    Transfers data from host to NAND flash.
*
*  Parameters
*    pInst        Phy. layer instance.
*    PageIndex    Index of the NAND flash page to be written (0-based).
*    pData        Data to be written.
*    Off          Byte offset to write to (relative to beginning of the page).
*    NumBytes     Number of bytes to be written.
*
*  Return value
*    ==0      OK, data written successfully.
*    !=0      An error occurred.
*/
static int _ISSI_WriteDataToCache(const NAND_SPI_INST * pInst, U32 PageIndex, const void * pData, unsigned Off, unsigned NumBytes) {
  int r;
  U32 aSpareArea[MAX_SPARE_AREA_SIZE / 4];

  ASSERT_ENTIRE_SPARE_AREA(pInst, Off, NumBytes);
  if (_IsDataRelocationRequired(pInst, Off, NumBytes) != 0) {
    FS_MEMSET(aSpareArea, 0xFF, sizeof(aSpareArea));
    FS_MEMCPY(aSpareArea, pData, NumBytes);
    _ISSI_RelocateSpareAreaData(pInst, aSpareArea);
    pData = aSpareArea;
  }
  r = _WriteDataToCache(pInst, PageIndex, pData, Off, NumBytes);
  return r;
}

/*********************************************************************
*
*      _MACRONIX_Identify
*/
static int _MACRONIX_Identify(NAND_SPI_INST * pInst, const U8 * pId) {
  int r;
  U8  MfgId;

  FS_USE_PARA(pInst);
  r = 1;          // Not a Macronix device.
  MfgId = *pId;   // The first byte is the manufacturer id.
  if (MfgId == MFG_ID_MACRONIX) {
    r = 0;        // This is a Macronix device.
  }
  return r;
}

/*********************************************************************
*
*      _MACRONIX_IdentifyNoHW_ECC
*
*  Function description
*    Checks for a Macronix NAND flash device without HW ECC.
*
*  Parameters
*    pInst      Physical layer instance.
*    pId        Data returned by the READ ID command. It has to be at least 2 bytes long.
*
*  Return value
*    ==0      This is a Macronix device without HW ECC.
*    !=0      Not a Macronix device without HW ECC.
*/
static int _MACRONIX_IdentifyNoHW_ECC(NAND_SPI_INST * pInst, const U8 * pId) {
  int      r;
  unsigned MfgId;
  unsigned DeviceId;

  FS_USE_PARA(pInst);
  r        = 1;                     // Not a Macronix device.
  MfgId    = pId[0];                // The first byte is the manufacturer id.
  DeviceId = pId[1];                // The second byte is the device id.
  if (MfgId == MFG_ID_MACRONIX) {
    if (   (DeviceId == 0x14u)      // Macronix MX35LF1G24AD
        || (DeviceId == 0x24u)      // Macronix MX35LF2G24AD
        || (DeviceId == 0x35u)) {   // Macronix MX35LF4G24AD
      r = 0;                        // This is a Macronix device without HW ECC.
    }
  }
  return r;
}

/*********************************************************************
*
*      _MACRONIX_ReadApplyPara
*/
static int _MACRONIX_ReadApplyPara(NAND_SPI_INST * pInst, const U8 * pId) {
  int      r;
  U8       Feat;
  int      NumBitErrorsCorrectable;
  int      HasHW_ECC;
  unsigned DeviceId;
  unsigned ldNumPlanes;
  unsigned BytesPerSpareArea;

  Feat = 0;
  //
  // Make sure that the quad mode is disabled during the initialization.
  //
  r = _SetFeatures(pInst, FEAT_ADDR_OTP, 0);
  if (r == 0) {
    r = _ReadApplyPara(pInst, pId);
    if (r == 0) {
      r = _GetFeatures(pInst, FEAT_ADDR_OTP, &Feat);
      if (r == 0) {
        DeviceId = pId[1];
        //
        // Enable quad operation in the NAND flash if required.
        //
        if (pInst->Allow4bitMode != 0u) {
          Feat |= FEAT_QE;
          r = _SetFeatures(pInst, FEAT_ADDR_OTP, Feat);
        }
        //
        // Determine the information related to ECC.
        //
        HasHW_ECC               = 1;          // All the older Macronix devices have HW ECC.
        NumBitErrorsCorrectable = (int)pInst->NumBitErrorsCorrectable;
        BytesPerSpareArea       = pInst->BytesPerSpareArea;
        if (NumBitErrorsCorrectable != 0) {   // A device with HW ECC has NumBitErrorsCorrectable set to 0.
          HasHW_ECC = 0;
        } else {
          //
          // The information about the number of bits the HW ECC is able to correct
          // is not stored in the ONFI parameters. Therefore, we have to determine it
          // based on the second byte returned as a response to the READ ID command.
          // In addition, the newer devices with HW ECC report a spare area that
          // is two times larger than the space available for the application because
          // it includes also the area that is used to store the ECC. The ECC is stored
          // in the last half of the spare area and therefore we report that the ECC
          // area is only half as large than it actually is.
          //
          NumBitErrorsCorrectable = 4;        // All the older Macronix devices have HW ECC that is able to correct up to 4 bit errors.
          if (   (DeviceId == 0x26u)          // MX35LF2GE4AD
              || (DeviceId == 0x37u)) {       // MX35LF4GE4AD
            NumBitErrorsCorrectable   = 8;
            BytesPerSpareArea       >>= 1u;
          }
        }
        //
        // Determine the number of planes in the device.
        //
        ldNumPlanes = 0;                      // Assume that the device has a single plane.
        if (   (DeviceId == 0x24u)            // MX35LF2G24AD
            || (DeviceId == 0x35u)) {         // MX35LF4G24AD
            ldNumPlanes = 1;                  // These devices have two planes.
        }
        //
        // Save the calculated values to instance.
        //
        pInst->NumBitErrorsCorrectable = (U8)NumBitErrorsCorrectable;
        pInst->HasHW_ECC               = (U8)HasHW_ECC;
        pInst->ldNumPlanes             = (U8)ldNumPlanes;
        pInst->BytesPerSpareArea       = (U16)BytesPerSpareArea;
      }
    }
  }
  return r;
}

/*********************************************************************
*
*      _MACRONIX_ReadDataFromCache
*
*  Function description
*    Transfers data from NAND flash to host.
*
*  Parameters
*    pInst        Driver instance.
*    PageIndex    Index of the page to read from.
*    pData        Data read from the page.
*    Off          Byte offset to read from.
*    NumBytes     Number of bytes to be read.
*
*  Return value
*    ==0      OK, data read successfully.
*    !=0      An error occurred.
*
*  Notes
*    (1) According to the data sheet of MX35LF2G24AD and MX35LF4G24AD
*        no plane selection is required when reading from the internal
*        cache of the NAND flash device.
*/
static int _MACRONIX_ReadDataFromCache(const NAND_SPI_INST * pInst, U32 PageIndex, void * pData, unsigned Off, unsigned NumBytes) {
  unsigned BusWidth;
  U8       abPara[NUM_BYTES_OFF + NUM_BYTES_DUMMY];       // According to [1] dummy bytes have to be sent to give NAND flash device time to prepare the data.
  U8       Cmd;
  int      r;

  FS_USE_PARA(PageIndex);
  FS_MEMSET(abPara, 0xFF, sizeof(abPara));
  FS_StoreU16BE(abPara, Off);                             // The offset is sent before the dummy bytes.
  BusWidth = pInst->BusWidthRead;
  Cmd      = pInst->CmdRead;
  r = _ReadData(pInst, Cmd, abPara, sizeof(abPara), NUM_BYTES_OFF, SEGGER_PTR2PTR(U8, pData), NumBytes, BusWidth);
  return r;
}

/*********************************************************************
*
*       _MACRONIX_GetECCResult
*
*  Function description
*    Returns the result of the ECC correction status.
*
*  Return value
*    ==0      OK, status returned
*    !=0      An error occurred
*/
static int _MACRONIX_GetECCResult(const NAND_SPI_INST * pInst, FS_NAND_ECC_RESULT * pResult) {
  U8  Status;
  U8  CorrectionStatus;
  U8  MaxNumBitErrorsCorrected;
  U8  eccStatus;
  int r;

  //
  // Initialize local variables.
  //
  r                        = 0;
  CorrectionStatus         = FS_NAND_CORR_NOT_APPLIED;
  MaxNumBitErrorsCorrected = 0;
  //
  // Read the status of the last page read operation to find out if any uncorrectable bit errors occurred.
  //
  Status = _ReadStatus(pInst);
  if ((Status & STATUS_IN_PROGRESS) != 0u) {
    r = 1;              // Error, could not read status.
  } else {
    if (_IsReadError(Status) != 0) {
      CorrectionStatus = FS_NAND_CORR_FAILURE;
    } else {
      //
      // Analyze the status and get the number of bit errors.
      //
      Status &= STATUS_READ_ERROR_MASK;
      if (Status == STATUS_READ_ERROR_CORRECTED) {
        eccStatus = 0;
        r = _ReadECCStatus(pInst, &eccStatus);
        if (r == 0) {
          MaxNumBitErrorsCorrected = eccStatus & ECC_STATUS_MASK;
        }
        CorrectionStatus = FS_NAND_CORR_APPLIED;
      }
    }
  }
  //
  // Return the calculated values.
  //
  pResult->CorrectionStatus    = CorrectionStatus;
  pResult->MaxNumBitsCorrected = MaxNumBitErrorsCorrected;
  return r;
}

/*********************************************************************
*
*      _MICRON_IdentifyLegacy
*
*  Function description
*    Identifies Micron devices that are organized in two planes.
*    The other Micron devices are handled via the default routines.
*/
static int _MICRON_IdentifyLegacy(NAND_SPI_INST * pInst, const U8 * pId) {
  int r;
  U8  MfgId;
  U8  DeviceId;

  FS_USE_PARA(pInst);
  r        = 1;           // Device not supported.
  MfgId    = *pId;        // The first byte is the manufacturer id.
  DeviceId = *(pId + 1);
  if (MfgId == MFG_ID_MICRON) {
    //
    // The following Micron devices are supported:
    //
    // Id          Device
    // ------------------
    // 0x2C 0x12   Micron MT29F1G01AAADD
    // 0x2C 0x22   Micron MT29F2G01AAAED
    //
    if (   (DeviceId == 0x12u)
        || (DeviceId == 0x22u)) {
      r = 0;              // This device is supported.
    }
  }
  return r;
}

/*********************************************************************
*
*      _MICRON_ReadApplyParaLegacy
*/
static int _MICRON_ReadApplyParaLegacy(NAND_SPI_INST * pInst, const U8 * pId) {
  int r;

  r = _ReadApplyPara(pInst, pId);
  if (r == 0) {
    //
    // The only way to get the number of planes in the Micron device
    // without looking at the device model in the ONFI parameters
    // is to check the second byte returned by the READ ID function
    // MT29F1G01AAADD -> 0x12 -> 2 planes
    // MT29F2G01AAAED -> 0x22 -> 2 planes
    //
    pInst->ldNumPlanes = 1;
    pInst->HasHW_ECC   = 1;
  }
  return r;
}

/*********************************************************************
*
*      _MICRON_RelocateSpareAreaData
*
*  Function description
*    Swaps data in the spare are according to the layout of
*    Micron MT29F1G01ABAFD NAND flash device.
*
*  Additional information
*    The Micron MT29F1G01ABAFD NAND flash device uses a different layout
*    for the data stored in the spare area than the previous devices.
*
*        0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
*      +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*    0 | B | N | N | N | g | g | g | g | N | N | N | N | N | N | N | N |
*      +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*    1 | N | N | N | N | h | h | h | h | N | N | N | N | N | N | N | N |
*      +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*    2 | G | G | G | G | i | i | i | i | H | H | H | H | U | U | U | U |
*      +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*    3 | I | I | I | I | j | j | j | j | J | J | J | J | U | U | U | U |
*      +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*    4 |                             ECC0                              |
*      +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*    5 |                             ECC1                              |
*      +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*    6 |                             ECC2                              |
*      +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*    7 |                             ECC3                              |
*      +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*
*    B[1]     - stores the bad block marker (not protected by ECC).
*    N[]      - locations that are not protected by ECC.
*    g[4]     - Data stored by NAND driver in the spare area stripe 0.
*               The data of this field has to be located to field G[4].
*    G[4]     - The location where the data of field g[4] is actually stored.
*               The data of this field is protected by ECC0.
*    h[4]     - Data stored by NAND driver in the spare area stripe 1.
*               The data of this field has to be located to field H[4].
*    H[4]     - The location where the data of field h[4] is actually stored.
*               The data of this field is protected by ECC1.
*    i[4]     - Data stored by NAND driver in the spare area stripe 2.
*               The data of this field has to be located to field I[4].
*    I[4]     - The location where the data of field i[4] is actually stored.
*               The data of this field is protected by ECC2.
*    j[4]     - Data stored by NAND driver in the spare area stripe 3.
*               The data of this field has to be located to field J[4].
*    J[4]     - The location where the data of field j[4] is actually stored.
*               The data of this field is protected by ECC3.
*    ECC0[16] - ECC for the ECC data block 0 and for bytes 0-7 in
*               the spare area stripe 2 (generated by HW ECC of NAND flash device).
*    ECC1[16] - ECC for the ECC data block 1 and for bytes 8-F in
*               the spare area stripe 2 (generated by HW ECC of NAND flash device).
*    ECC2[16] - ECC for the ECC data block 2 and for bytes 0-7 in
*               the spare area stripe 3 (generated by HW ECC of NAND flash device).
*    ECC3[16] - ECC for the ECC data block 3 and for bytes 8-F in
*               the spare area stripe 3 (generated by HW ECC of NAND flash device).
*    U[]      - Locations not used to store data that are protected by ECC.
*
*    The Universal NAND driver stores the data to byte offsets 4-7 of each
*    spare area stripe therefore we have to relocate this data to stripe 2
*    so that the data is protected by ECC.
*/
static void _MICRON_RelocateSpareAreaData(const NAND_SPI_INST * pInst, U32 * pData) {
  U32      * pUserData;
  U32      * pUserDataMicron;
  U32        Data32;
  unsigned   BytesPerSpareStripe;
  unsigned   NumECCBlocks;
  unsigned   ldNumECCBlocks;
  unsigned   StripeIndex;

  ldNumECCBlocks      = pInst->ldNumECCBlocks;
  NumECCBlocks        = 1uL << ldNumECCBlocks;
  BytesPerSpareStripe = (unsigned)pInst->BytesPerSpareArea >> ldNumECCBlocks;
  pUserData           = pData + (OFF_USER_DATA >> 2u);
  StripeIndex         = NumECCBlocks >> 1u;       // This is the index of the first stripe protected by ECC.
  pUserDataMicron     = pData + ((BytesPerSpareStripe * StripeIndex) >> 2u);
  do {
    Data32            = *pUserDataMicron;
    *pUserDataMicron  = *pUserData;
    *pUserData        = Data32;
    pUserDataMicron  += (BytesPerSpareStripe >> 1u) >> 2u;
    pUserData        += BytesPerSpareStripe >> 2u;
  } while (--NumECCBlocks != 0u);
}

/*********************************************************************
*
*      _MICRON_CalcUserDataSpareOff
*
*  Function description
*    Calculates the offset where the data is actually stored to spare area.
*
*  Parameters
*    pInst      Physical layer instance.
*    Off        Byte offset used by the Universal NAND driver.
*    NumBytes   Number of bytes to read from the specified offset.
*
*  Return value
*    ==0        Entire spare area has to be read.
*    !=0        Actual byte offset to read from.
*
*  Additional information
*    See _MICRON_RelocateSpareAreaData() for information about how the
*    data is stored to spare area.
*/
static unsigned _MICRON_CalcUserDataSpareOff(const NAND_SPI_INST * pInst, unsigned Off, unsigned NumBytes) {
  unsigned NumECCBlocks;
  unsigned OffUserDataMicron;
  unsigned OffUserData;
  unsigned BytesPerSpareStripe;
  unsigned BytesPerPage;
  unsigned ldNumECCBlocks;
  unsigned StripeIndex;

#if (FS_NAND_SUPPORT_COMPATIBILITY_MODE != 0)
  if (pInst->CompatibilityMode > 0u) {
    return 0;           // Not supported in compatibility mode.
  }
#endif // FS_NAND_SUPPORT_COMPATIBILITY_MODE != 0
  BytesPerPage        = 1uL << pInst->ldBytesPerPage;
  ldNumECCBlocks      = pInst->ldNumECCBlocks;
  NumECCBlocks        = 1uL << ldNumECCBlocks;
  BytesPerSpareStripe = (unsigned)pInst->BytesPerSpareArea >> ldNumECCBlocks;
  StripeIndex         = NumECCBlocks >> 1u;       // This is the index of the first stripe protected by ECC.
  OffUserDataMicron   = BytesPerSpareStripe * StripeIndex;
  OffUserData         = OFF_USER_DATA;
  if (NumBytes == NUM_BYTES_USER_DATA) {
    if (Off >= BytesPerPage) {
      Off -= BytesPerPage;
      do {
        if (Off == OffUserData) {
          Off = BytesPerPage + OffUserDataMicron;
          return Off;
        }
        OffUserData       += BytesPerSpareStripe;
        OffUserDataMicron += BytesPerSpareStripe >> 1u;
      } while (--NumECCBlocks != 0u);
    }
  }
  return 0;             // No access to a user data in the spare area.
}

#if (FS_NAND_SUPPORT_COMPATIBILITY_MODE > 0)

/*********************************************************************
*
*      _MICRON_RelocateAtReadSpareAreaData
*
*  Function description
*    Relocates the data from the spare area when reading data
*    in compatibility mode.
*
*  Parameters
*    pInst      Physical layer instance.
*    pData      Spare area data.
*
*  Additional information
*    See _MICRON_RelocateSpareAreaData() for information about how the
*    data is stored to spare area. The function makes sure that the data
*    is relocated only if has been stored using an emFile version newer
*    than 4.06a. Older emFile versions store the data at the same offset
*    as expected by the Universal NAND driver therefore no relocation
*    is necessary in this case.
*/
static void _MICRON_RelocateAtReadSpareAreaData(const NAND_SPI_INST * pInst, U32 * pData) {
  U32      * pUserData;
  U32      * pUserDataMicron;
  U32        Data32;
  unsigned   BytesPerSpareStripe;
  unsigned   NumECCBlocks;
  unsigned   ldNumECCBlocks;
  unsigned   StripeIndex;

  ldNumECCBlocks      = pInst->ldNumECCBlocks;
  NumECCBlocks        = 1uL << ldNumECCBlocks;
  BytesPerSpareStripe = (unsigned)pInst->BytesPerSpareArea >> ldNumECCBlocks;
  pUserData           = pData + (OFF_USER_DATA >> 2u);
  StripeIndex         = NumECCBlocks >> 1u;       // This is the index of the first stripe protected by ECC.
  pUserDataMicron     = pData + ((BytesPerSpareStripe * StripeIndex) >> 2u);
  do {
    Data32             = *pUserDataMicron;
    //
    // We copy the data only if it is valid.
    //
    if (Data32 != 0xFFFFFFFFuL) {
      *pUserDataMicron = *pUserData;
      *pUserData       = Data32;
    }
    pUserDataMicron  += (BytesPerSpareStripe >> 1u) >> 2u;
    pUserData        += BytesPerSpareStripe >> 2u;
  } while (--NumECCBlocks != 0u);
}

#endif // FS_NAND_SUPPORT_COMPATIBILITY_MODE > 0

#if (FS_NAND_SUPPORT_COMPATIBILITY_MODE > 1)

/*********************************************************************
*
*      _MICRON_RelocateAtWriteSpareAreaData
*
*  Function description
*    Relocates the data from the spare area when writing data
*    in compatibility mode.
*
*  Parameters
*    pInst      Physical layer instance.
*    pData      Spare area data.
*
*  Additional information
*    See _MICRON_RelocateSpareAreaData() for information about how the
*    data is stored to spare area. The function makes sure that the data
*    is updated to the new (protected by ECC) as well as to old location
*    so that an emFile version older than or equal ton 4.06a can correctly
*    read it.
*/
static void _MICRON_RelocateAtWriteSpareAreaData(const NAND_SPI_INST * pInst, U32 * pData) {
  U32      * pUserData;
  U32      * pUserDataMicron;
  U32        Data32;
  unsigned   BytesPerSpareStripe;
  unsigned   NumECCBlocks;
  unsigned   ldNumECCBlocks;
  unsigned   StripeIndex;

  ldNumECCBlocks      = pInst->ldNumECCBlocks;
  NumECCBlocks        = 1uL << ldNumECCBlocks;
  BytesPerSpareStripe = (unsigned)pInst->BytesPerSpareArea >> ldNumECCBlocks;
  pUserData           = pData + (OFF_USER_DATA >> 2u);
  StripeIndex         = NumECCBlocks >> 1u;       // This is the index of the first stripe protected by ECC.
  pUserDataMicron     = pData + ((BytesPerSpareStripe * StripeIndex) >> 2u);
  do {
    //
    // Duplicate the data at the new location.
    //
    Data32            = *pUserData;
    *pUserDataMicron  = Data32;
    pUserDataMicron  += (BytesPerSpareStripe >> 1u) >> 2u;
    pUserData        += BytesPerSpareStripe >> 2u;
  } while (--NumECCBlocks != 0u);
}

#endif // FS_NAND_SUPPORT_COMPATIBILITY_MODE > 1

/*********************************************************************
*
*      _MICRON_Identify
*/
static int _MICRON_Identify(NAND_SPI_INST * pInst, const U8 * pId) {
  int r;
  U8  MfgId;
  U8  DeviceId;

  FS_USE_PARA(pInst);
  r        = 1;           // Device not supported.
  MfgId    = *pId;        // The first byte is the manufacturer id.
  DeviceId = *(pId + 1);
  if (MfgId == MFG_ID_MICRON) {
    //
    // The following Micron devices are supported:
    //
    // Id          Device
    // ------------------
    // 0x2C 0x14   Micron MT29F1G01ABAFD
    // 0x2C 0x15   Micron MT29F1G01ABBFD
    // 0x2C 0x24   Micron MT29F2G01ABAGD
    // 0x2C 0x35   Micron MT29F4G01ABBFD
    //
    if (   (DeviceId == 0x14u)
        || (DeviceId == 0x15u)
        || (DeviceId == 0x24u)
        || (DeviceId == 0x35u)) {
      r = 0;              // This device is supported.
    }
  }
  return r;
}

/*********************************************************************
*
*      _MICRON_ReadApplyPara
*/
static int _MICRON_ReadApplyPara(NAND_SPI_INST * pInst, const U8 * pId) {
  int      r;
  unsigned BytesPerSpareArea;
  U8       DeviceId;
  U8       Feat;
  unsigned v;

  r = _ReadApplyPara(pInst, pId);
  if (r == 0) {
    //
    // MT29F1G01ABAFD and MT29F8G01ADAFD store the ECC in the last half of the spare area.
    // We report that the spare area is half as large as reported by the NAND flash device
    // to prevent that the Universal NAND driver stores data in the ECC area.
    //
    BytesPerSpareArea          = pInst->BytesPerSpareArea;
    BytesPerSpareArea        >>= 1;
    pInst->BytesPerSpareArea   = (U16)BytesPerSpareArea;
    DeviceId = *(pId + 1);
    if (DeviceId == 0x24u) {    // Set the correct number of planes for MT29F2G01ABAGD -> 2 planes
      pInst->ldNumPlanes = 1;
    }
    //
    // Disable the continuous read mode if required.
    //
    Feat = 0;
    r = _GetFeatures(pInst, FEAT_ADDR_OTP, &Feat);
    if (r == 0) {
      //
      // Enable buffer mode and disable continuous read mode if required.
      //
      if ((Feat & FEAT_CONT_READ) != 0u) {
        v     =  Feat;
        v    &= ~FEAT_CONT_READ;
        Feat  = (U8)v;
        r = _SetFeatures(pInst, FEAT_ADDR_OTP, Feat);
      }
    }
    pInst->HasHW_ECC = 1;
    if (BytesPerSpareArea > (unsigned)(MAX_SPARE_AREA_SIZE)) {
      r = 1;                    // Error, spare area buffer too small.
    }
  }
  return r;
}

/*********************************************************************
*
*       _MICRON_GetECCResult
*
*  Function description
*    Returns the result of the ECC correction status.
*
*  Return value
*    ==0      OK, status returned
*    !=0      An error occurred
*/
static int _MICRON_GetECCResult(const NAND_SPI_INST * pInst, FS_NAND_ECC_RESULT * pResult) {
  U8  Status;
  U8  CorrectionStatus;
  U8  MaxNumBitErrorsCorrected;
  int r;

  //
  // Initialize local variables.
  //
  r                        = 0;
  CorrectionStatus         = FS_NAND_CORR_NOT_APPLIED;
  MaxNumBitErrorsCorrected = 0;
  //
  // Read the status of the last page read operation to find out if any uncorrectable bit errors occurred.
  //
  Status = _ReadStatus(pInst);
  if ((Status & STATUS_IN_PROGRESS) != 0u) {
    r = 1;            // Error, could not read status.
  } else {
    if (_IsReadError(Status) != 0) {
      CorrectionStatus = FS_NAND_CORR_FAILURE;
    } else {
      //
      // Micron MT29F1G01ABAFD reports the approximate number of bit errors corrected.
      // For data reliability reasons, we return the highest number of bit errors corrected.
      //
      Status &= STATUS_READ_ERROR_MASK_EX;
      if (Status == STATUS_READ_ERROR_CORRECTED_1_3) {
        MaxNumBitErrorsCorrected = 3;
      } else if (Status == STATUS_READ_ERROR_CORRECTED_4_6) {
        MaxNumBitErrorsCorrected = 6;
      } else if (Status == STATUS_READ_ERROR_CORRECTED_7_8) {
        MaxNumBitErrorsCorrected = 8;
      } else {
        //
        // Unknown number of corrected bit errors.
        //
        MaxNumBitErrorsCorrected = 0;
      }
      if (MaxNumBitErrorsCorrected != 0u) {
        CorrectionStatus = FS_NAND_CORR_APPLIED;
      }
    }
  }
  //
  // Return the determined values.
  //
  pResult->CorrectionStatus    = CorrectionStatus;
  pResult->MaxNumBitsCorrected = MaxNumBitErrorsCorrected;
  return r;
}

/*********************************************************************
*
*      _MICRON_ReadDataFromCache
*
*  Function description
*    Transfers data from the internal page register of the NAND flash
*    device to host.
*
*  Parameters
*    pInst        Physical layer instance.
*    PageIndex    Index of the NAND page to be read.
*    pData        [OUT] Data read from the page.
*    Off          Offset of the first byte to be read.
*    NumBytes     Number of bytes to be read.
*
*  Return value
*    ==0      OK, data read successfully.
*    !=0      An error occurred.
*/
static int _MICRON_ReadDataFromCache(const NAND_SPI_INST * pInst, U32 PageIndex, void * pData, unsigned Off, unsigned NumBytes) {
  int        r;
  U32        aSpareArea[MAX_SPARE_AREA_SIZE / 4];
  unsigned   BytesPerPage;
  unsigned   BytesPerSpareArea;
  unsigned   NumBytesToRead;
  unsigned   OffCalc;
  U8       * pData8;

  if (_IsDataRelocationRequired(pInst, Off, NumBytes) == 0) {
    r = _ReadDataFromCache(pInst, PageIndex, pData, Off, NumBytes);
  } else {
    //
    // Data has to be relocated. First, process requests that read
    // the entire data stored in a spare area stripe. This type of
    // requests are generated by the Universal NAND driver when
    // FS_NAND_OPTIMIZE_SPARE_AREA_READ is set to 1.
    //
    OffCalc = _MICRON_CalcUserDataSpareOff(pInst, Off, NumBytes);
    if (OffCalc != 0u) {
      r = _ReadDataFromCache(pInst, PageIndex, pData, OffCalc, NumBytes);
    } else {
      r = 0;
      BytesPerPage      = 1uL << pInst->ldBytesPerPage;
      BytesPerSpareArea = pInst->BytesPerSpareArea;
      if (Off < BytesPerPage) {
        //
        // Read bytes from main area.
        //
        pData8         = SEGGER_PTR2PTR(U8, pData);
        NumBytesToRead = BytesPerPage - Off;
        NumBytesToRead = SEGGER_MIN(NumBytes, NumBytesToRead);
        r = _ReadDataFromCache(pInst, PageIndex, pData8, Off, NumBytesToRead);
        Off      += NumBytesToRead;
        NumBytes -= NumBytesToRead;
        pData8   += NumBytesToRead;
        pData     = pData8;
      }
      if (r == 0) {
        if (NumBytes != 0u) {
          Off -= BytesPerPage;
          //
          // Read data from the spare area.
          //
          FS_MEMSET(aSpareArea, 0xFF, sizeof(aSpareArea));
          r = _ReadDataFromCache(pInst, PageIndex, aSpareArea, BytesPerPage, BytesPerSpareArea);
          if (r == 0) {
#if (FS_NAND_SUPPORT_COMPATIBILITY_MODE > 0)
            if (pInst->CompatibilityMode > 0u) {
              _MICRON_RelocateAtReadSpareAreaData(pInst, aSpareArea);
            } else
#endif // FS_NAND_SUPPORT_COMPATIBILITY_MODE
            {
              _MICRON_RelocateSpareAreaData(pInst, aSpareArea);
            }
            pData8 = SEGGER_PTR2PTR(U8, aSpareArea);
            FS_MEMCPY(pData, pData8 + Off, NumBytes);
          }
        }
      }
    }
  }
  return r;
}

/*********************************************************************
*
*      _MICRON_WriteDataToCache
*/
static int _MICRON_WriteDataToCache(const NAND_SPI_INST * pInst, U32 PageIndex, const void * pData, unsigned Off, unsigned NumBytes) {
  int r;
  U32 aSpareArea[MAX_SPARE_AREA_SIZE / 4];

  ASSERT_ENTIRE_SPARE_AREA(pInst, Off, NumBytes);
  if (_IsDataRelocationRequired(pInst, Off, NumBytes) != 0) {
    FS_MEMSET(aSpareArea, 0xFF, sizeof(aSpareArea));
    FS_MEMCPY(aSpareArea, pData, NumBytes);
#if (FS_NAND_SUPPORT_COMPATIBILITY_MODE > 1)
    if (pInst->CompatibilityMode > 1u) {
      _MICRON_RelocateAtWriteSpareAreaData(pInst, aSpareArea);
    } else
#endif // FS_NAND_SUPPORT_COMPATIBILITY_MODE > 1
    {
      _MICRON_RelocateSpareAreaData(pInst, aSpareArea);
    }
    pData = SEGGER_PTR2PTR(U32, aSpareArea);
  }
  r = _WriteDataToCache(pInst, PageIndex, pData, Off, NumBytes);
  return r;
}

/*********************************************************************
*
*      _MICRON_IdentifyStacked
*
*  Function description
*    Identifies Micron devices that are organized in two dies.
*/
static int _MICRON_IdentifyStacked(NAND_SPI_INST * pInst, const U8 * pId) {
  int r;
  U8  MfgId;
  U8  DeviceId;

  FS_USE_PARA(pInst);
  r        = 1;           // Device not supported.
  MfgId    = *pId;        // The first byte is the manufacturer id.
  DeviceId = *(pId + 1);
  if (MfgId == MFG_ID_MICRON) {
    //
    // The following Micron devices are supported:
    //
    // Id          Device
    // ------------------
    // 0x2C 0x46   Micron MT29F8G01ADAFD (3.3V)
    // 0x2C 0x47   Micron MT29F8G01ADBFD (1.8V)
    //
    if (   (DeviceId == 0x46u)
        || (DeviceId == 0x47u)) {
      r = 0;              // This device is supported.
    }
  }
  return r;
}

/*********************************************************************
*
*      _MICRON_SetDieIndex
*/
static int _MICRON_SetDieIndex(const NAND_SPI_INST * pInst, unsigned DieIndex) {
  int r;
  U8  Value;
  U8  ValueToCheck;

  Value = 0;
  if (DieIndex != 0u) {
    Value = FEAT_DIE_SELECT;
  }
  //
  // Select the specified die.
  //
  r = _SetFeatures(pInst, FEAT_ADDR_DIE_SELECT, Value);
  if (r == 0) {
    //
    // Check that the correct die was selected.
    //
    r = _GetFeatures(pInst, FEAT_ADDR_DIE_SELECT, &ValueToCheck);
    if (r == 0) {
      if (ValueToCheck != Value) {
        r = 1;                            // Error, the die was not selected.
      }
    }
  }
  return r;
}

/*********************************************************************
*
*      _MICRON_SelectDie
*/
static int _MICRON_SelectDie(NAND_SPI_INST * pInst, unsigned DieIndex) {
  int      r;
  unsigned DieIndexSelected;

  r                = 0;
  DieIndexSelected = pInst->DieIndexSelected;
  if (DieIndexSelected != DieIndex) {
    r = _MICRON_SetDieIndex(pInst, DieIndex);
    if (r == 0) {
      pInst->DieIndexSelected = (U8)DieIndex;
    }
  }
  return r;
}

/*********************************************************************
*
*      _MICRON_ReadApplyParaStacked
*/
static int _MICRON_ReadApplyParaStacked(NAND_SPI_INST * pInst, const U8 * pId) {
  int      r;
  int      Result;
  unsigned NumDies;
  unsigned DieIndex;
  unsigned iDie;

  r = _MICRON_ReadApplyPara(pInst, pId);
  if (r == 0) {
    NumDies = 1uL << pInst->ldNumDies;
    //
    // Disable the HW ECC on all dies because we don't know
    // which die will actually be accessed by the NAND driver.
    // In addition, we remove the locking of all blocks.
    //
    DieIndex = pInst->DieIndexSelected;
    for (iDie = 0; iDie < NumDies; ++iDie) {
      Result = _MICRON_SetDieIndex(pInst, iDie);
      if (Result != 0) {
        r = Result;
      }
      Result = _DisableECC(pInst);
      if (Result != 0) {
        r = Result;
      }
      //
      // Unlock all the device blocks.
      //
      Result = _SetFeatures(pInst, FEAT_ADDR_BLOCK_LOCK, 0);
      if (Result != 0) {
        r = Result;
      }
    }
    //
    // Re-select the original die.
    //
    Result = _MICRON_SetDieIndex(pInst, DieIndex);
    if (Result != 0) {
      r = Result;
    }
  }
  return r;
}

/*********************************************************************
*
*      _TOSHIBA_Identify
*/
static int _TOSHIBA_Identify(NAND_SPI_INST * pInst, const U8 * pId) {
  int r;
  U8  MfgId;

  FS_USE_PARA(pInst);
  r     = 1;      // Not a Toshiba device.
  MfgId = *pId;   // The first byte is the manufacturer id.
  if (MfgId == MFG_ID_TOSHIBA) {
    r = 0;        // This is a Toshiba device that we support.
  }
  return r;
}

/*********************************************************************
*
*      _TOSHIBA_ReadApplyPara
*/
static int _TOSHIBA_ReadApplyPara(NAND_SPI_INST * pInst, const U8 * pId) {
  int r;
  U8  Feat;
  U8  FeatCurrent;
  U8  DeviceId;

  r = _ReadApplyPara(pInst, pId);
  if (r == 0) {
    pInst->NumBitErrorsCorrectable = 8;       // All the supported devices have a HW ECC that can correct up to 8 bit errors.
    pInst->HasHW_ECC               = 1;
    //
    // Configure the device.
    //
    Feat = 0;
    r = _GetFeatures(pInst, FEAT_ADDR_OTP, &Feat);
    if (r == 0) {
      FeatCurrent = Feat;
      //
      // Enable the high speed mode if required.
      //
      if ((Feat & FEAT_HS_MODE) == 0u) {
        Feat |= FEAT_HS_MODE;
      }
      //
      // Disable the function of the HOLD function if required.
      // According to the data sheet the HOLD function has to
      // be disabled if the MCU transfers the data to the NAND
      // flash device via 4 data lines.
      //
      if (pInst->Allow4bitMode != 0u) {
        DeviceId = pId[1];
        if (   (DeviceId == 0xDBu)          // TC58CYG1S3HRAIJ
            || (DeviceId == 0xEDu)          // TC58CVG2S0HRAIJ
            || (DeviceId == 0xD2u)          // TC58CYG0S3HRAIJ
            || (DeviceId == 0xDDu)          // TC58CYG2S0HRAIJ
            || (DeviceId == 0xE4u)          // TH58CVG3S0HRAIJ
            || (DeviceId == 0xD4u)          // TH58CYG3S0HRAIJ
            || (DeviceId == 0xEBu)          // TC58CVG1S3HRAIJ
            || (DeviceId == 0xE2u)) {       // TC58CVG0S3HRAIJ
          if ((Feat & FEAT_HOLD_FUNC) == 0u) {
            Feat |= FEAT_HOLD_FUNC;
          }
        }
      }
      if (Feat != FeatCurrent) {
        r = _SetFeatures(pInst, FEAT_ADDR_OTP, Feat);
      }
    }
#if FS_SUPPORT_TEST
    if (((unsigned)pInst->BytesPerSpareArea << 1u) > (unsigned)(MAX_SPARE_AREA_SIZE)) {
      r = 1;                // Spare area buffer too small.
    }
#endif // FS_SUPPORT_TEST
  }
  return r;
}

/*********************************************************************
*
*       _TOSHIBA_GetECCResult
*
*  Function description
*    Returns the result of the ECC correction status.
*
*  Return value
*    ==0      OK, status returned
*    !=0      An error occurred
*/
static int _TOSHIBA_GetECCResult(const NAND_SPI_INST * pInst, FS_NAND_ECC_RESULT * pResult) {
  U8  Status;
  U8  CorrectionStatus;
  U8  MaxNumBitErrorsCorrected;
  U8  eccStatus;
  int r;

  //
  // Initialize local variables.
  //
  r                        = 0;
  CorrectionStatus         = FS_NAND_CORR_NOT_APPLIED;
  MaxNumBitErrorsCorrected = 0;
  //
  // Read the status of the last page read operation to find out if any uncorrectable bit errors occurred.
  //
  Status = _ReadStatus(pInst);
  if ((Status & STATUS_IN_PROGRESS) != 0u) {
    r = 1;              // Error, could not read status.
  } else {
    if (_IsReadError(Status) != 0) {
      CorrectionStatus = FS_NAND_CORR_FAILURE;
    } else {
      //
      // Analyze the status and get the number of bit errors.
      //
      Status &= STATUS_READ_ERROR_MASK;
      if ((Status & STATUS_READ_ERROR_CORRECTED) != 0u) {
        eccStatus = 0;
        r = _GetFeatures(pInst, FEAT_ADDR_ECC_STATUS, &eccStatus);
        if (r == 0) {
          MaxNumBitErrorsCorrected = eccStatus >> ECC_STATUS_MBF_BIT;
        }
        CorrectionStatus = FS_NAND_CORR_APPLIED;
      }
    }
  }
  //
  // Return the calculated values.
  //
  pResult->CorrectionStatus    = CorrectionStatus;
  pResult->MaxNumBitsCorrected = MaxNumBitErrorsCorrected;
  return r;
}

/*********************************************************************
*
*      _TOSHIBA_BeginPageCopy
*
*  Function description
*    Disables the high speed mode before a page copy operation.
*
*  Parameters
*    pInst      Driver instance.
*
*  Return value
*    ==0    OK, high speed mode disabled.
*    !=0    An error occurred.
*/
static int _TOSHIBA_BeginPageCopy(const NAND_SPI_INST * pInst) {
  int      r;
  U8       Feat;
  unsigned v;

  Feat = 0;
  r = _GetFeatures(pInst, FEAT_ADDR_OTP, &Feat);
  if (r == 0) {
    if ((Feat & FEAT_HS_MODE) != 0u) {
      v     = Feat;
      v    &= ~FEAT_HS_MODE;
      Feat  = (U8)v;
      r = _SetFeatures(pInst, FEAT_ADDR_OTP, Feat);
    }
  }
  return r;
}

/*********************************************************************
*
*      _TOSHIBA_EndPageCopy
*
*  Function description
*    Enables the high speed mode after a page copy operation.
*
*  Parameters
*    pInst      Driver instance.
*
*  Return value
*    ==0    OK, high speed mode enabled.
*    !=0    An error occurred.
*/
static int _TOSHIBA_EndPageCopy(const NAND_SPI_INST * pInst) {
  int      r;
  U8       Feat;
  unsigned v;

  Feat = 0;
  r = _GetFeatures(pInst, FEAT_ADDR_OTP, &Feat);
  if (r == 0) {
    if ((Feat & FEAT_HS_MODE) == 0u) {
      v     = Feat;
      v    |= FEAT_HS_MODE;
      Feat  = (U8)v;
      r = _SetFeatures(pInst, FEAT_ADDR_OTP, Feat);
    }
  }
  return r;
}

/*********************************************************************
*
*      _WINBOND_Identify
*/
static int _WINBOND_Identify(NAND_SPI_INST * pInst, const U8 * pId) {
  int r;
  U8  MfgId;

  FS_USE_PARA(pInst);
  r     = 1;      // Not a Winbond device.
  MfgId = *pId;   // The first byte is the manufacturer id.
  if (MfgId == MFG_ID_WINBOND) {
    r = 0;        // This is a Winbond device.
  }
  return r;
}

/*********************************************************************
*
*      _WINBOND_ReadApplyPara
*/
static int _WINBOND_ReadApplyPara(NAND_SPI_INST * pInst, const U8 * pId) {
  U8  Feat;
  int r;

  Feat = 0;
  r = _ReadApplyPara(pInst, pId);
  if (r == 0) {
    r = _GetFeatures(pInst, FEAT_ADDR_OTP, &Feat);
    if (r == 0) {
      //
      // Enable buffer mode and disable continuous read mode if required.
      //
      if ((Feat & FEAT_BUF_MODE) == 0u) {
        Feat |= FEAT_BUF_MODE;
        r = _SetFeatures(pInst, FEAT_ADDR_OTP, Feat);
      }
    }
    pInst->NumBitErrorsCorrectable = 1;
    pInst->HasHW_ECC               = 1;
  }
  return r;
}

/*********************************************************************
*
*      _WINBOND_IdentifyStacked
*/
static int _WINBOND_IdentifyStacked(NAND_SPI_INST * pInst, const U8 * pId) {
  int r;
  U8  MfgId;
  U8  DeviceId;

  FS_USE_PARA(pInst);
  r        = 1;        // Not a Winbond device.
  MfgId    = pId[0];    // The first byte is the manufacturer id.
  DeviceId = pId[1];
  if (MfgId == MFG_ID_WINBOND) {
    if (DeviceId == 0xABu) {
      r = 0;            // This is a Winbond W25M02GV device.
    }
  }
  return r;
}

/*********************************************************************
*
*      _WINBOND_SelectDie
*/
static int _WINBOND_SelectDie(NAND_SPI_INST * pInst, unsigned DieIndex) {
  unsigned BusWidth;
  int      r;
  unsigned DieIndexSelected;
  U8       Value;

  r                = 0;
  DieIndexSelected = pInst->DieIndexSelected;
  if (DieIndexSelected != DieIndex) {
    BusWidth = FS_BUSWIDTH_MAKE(1uL, 1uL, 1uL);       // This command is always sent in SPI mode.
    Value    = (U8)DieIndex;
    r = _WriteData(pInst, CMD_SELECT_DIE, NULL, 0, 0, &Value, sizeof(Value), BusWidth);
    if (r == 0) {
      pInst->DieIndexSelected = (U8)DieIndex;
    }
  }
  return r;
}

/*********************************************************************
*
*      _WINBOND_ReadApplyParaStacked
*/
static int _WINBOND_ReadApplyParaStacked(NAND_SPI_INST * pInst, const U8 * pId) {
  U8       Feat;
  int      r;
  unsigned ldNumDies;
  unsigned NumDies;
  unsigned iDie;
  int      Result;
  unsigned v;

  Feat = 0;
  r = _ReadApplyPara(pInst, pId);
  if (r == 0) {
    ldNumDies = 1;              // 2 dies in the stacked package.
    NumDies   = 1uL << ldNumDies;
    //
    // Each die has its own set of features therefore we
    // have to configure all the dies here.
    //
    for (iDie = 0; iDie < NumDies; ++iDie) {
      Result = _WINBOND_SelectDie(pInst, iDie);
      if (Result != 0) {
        r = 1;                  // Error, could not select die.
      } else {
        Result = _GetFeatures(pInst, FEAT_ADDR_OTP, &Feat);
        if (Result != 0) {
          r = 1;                // Error, could not read feature.
        }
        //
        // Enable buffer mode and disable continuous read mode if required.
        // Initially, access memory array without HW ECC. The HW ECC will be enabled by
        // the Universal NAND driver as needed. Doing this allows us to use the software
        // ECC to correct bit errors if required.
        //
        if (((Feat & FEAT_BUF_MODE) == 0u) || ((Feat & FEAT_ECC_ENABLE) != 0u)) {
          v     =  Feat;
          v    &= ~FEAT_ECC_ENABLE;
          v    |=  FEAT_BUF_MODE;
          Feat  = (U8)v;
          Result = _SetFeatures(pInst, FEAT_ADDR_OTP, Feat);
          if (Result != 0) {
            r = 1;              // Error, could not set feature.
          }
        }
        //
        // Unlock all the device blocks.
        //
        Result = _SetFeatures(pInst, FEAT_ADDR_BLOCK_LOCK, 0);
        if (Result != 0) {
          r = Result;           // Error, could not unlock blocks.
        }
      }
    }
    pInst->NumBitErrorsCorrectable = 1;
    pInst->HasHW_ECC               = 1;
    pInst->ldNumDies               = (U8)ldNumDies;
  }
  return r;
}

/*********************************************************************
*
*      _WINBOND_IdentifyEnhanced
*
*  Notes
*    (1) We cannot use the second byte of the id for the identification
*        as with NAND flash devices from other manufacturers because
*        the 2nd id byte is identical for W25N01GV, W25N02KV and W25N04KV.
*        Therefore, we have to use the 3rd id byte.
*/
static int _WINBOND_IdentifyEnhanced(NAND_SPI_INST * pInst, const U8 * pId) {
  int r;
  U8  MfgId;
  U8  DeviceId;

  FS_USE_PARA(pInst);
  r        = 1;                       // Not a Winbond device.
  MfgId    = pId[0];                  // The first byte is the manufacturer id.
  DeviceId = pId[2];                  // Note 1
  if (MfgId == MFG_ID_WINBOND) {
    if (   (DeviceId == 0x22u)        // Winbond W25N02KV
        || (DeviceId == 0x23u)) {     // Winbond W25N04KV
      r = 0;
    }
  }
  return r;
}

/*********************************************************************
*
*      _WINBOND_ReadApplyParaEnhanced
*/
static int _WINBOND_ReadApplyParaEnhanced(NAND_SPI_INST * pInst, const U8 * pId) {
  U8       Feat;
  int      r;
  unsigned BytesPerSpareArea;
  unsigned ldBlocksPerDie;
  unsigned ldNumDies;

  Feat = 0;
  //
  // It seems that the device is not able to return valid
  // parameter values if the last operation accessed the
  // second half of the device. Therefore, we perform
  // here a dummy read to the first page of the device.
  //
  r = _ReadPageToCache(pInst, 0);
  if (r == 0) {
    //
    // Wait for the read operation to finish.
    //
    (void)_WaitForEndOfOperation(pInst);
  }
  r = _ReadApplyPara(pInst, pId);
  if (r == 0) {
    r = _GetFeatures(pInst, FEAT_ADDR_OTP, &Feat);
    if (r == 0) {
      //
      // Enable buffer mode and disable continuous read mode if required.
      //
      if ((Feat & FEAT_BUF_MODE) == 0u) {
        Feat |= FEAT_BUF_MODE;
        r = _SetFeatures(pInst, FEAT_ADDR_OTP, Feat);
      }
    }
    //
    // The last half of the spare area is reserved to the parity checksums
    // and cannot be used as storage therefore we report that the spare
    // area is only half of the actual size.
    //
    BytesPerSpareArea   = pInst->BytesPerSpareArea;
    BytesPerSpareArea >>= 1;
#if FS_SUPPORT_TEST
    if (BytesPerSpareArea > (unsigned)(MAX_SPARE_AREA_SIZE)) {
      r = 1;                          // Spare area buffer too small.
    }
#endif // FS_SUPPORT_TEST
    //
    // W25N02KV is organized as two separate dies each with its
    // own data buffer. This means that it is not possible to perform
    // internal page copy operations across internal dies.
    // However, the device reports that it has only one die
    // via the "Number of logical units" ONFI parameter
    // that is set to 1. Therefore, we have to correct here
    // the number of dies as well as the number of blocks
    // per die so that the internal copy page operation
    // works correctly.
    //
    ldNumDies      = pInst->ldNumDies;
    ldBlocksPerDie = pInst->ldBlocksPerDie;
    if (ldNumDies == 0u) {
      ldNumDies       = 1;
      ldBlocksPerDie -= 1u;
    }
    pInst->ldNumDies               = (U8)ldNumDies;
    pInst->ldBlocksPerDie          = (U8)ldBlocksPerDie;
    pInst->BytesPerSpareArea       = (U16)BytesPerSpareArea;
    pInst->NumBitErrorsCorrectable = 8;
    pInst->HasHW_ECC               = 1;
  }
  return r;
}

/*********************************************************************
*
*       _WINBOND_GetECCResult
*
*  Function description
*    Returns the result of the ECC correction status.
*
*  Return value
*    ==0      OK, status returned
*    !=0      An error occurred
*/
static int _WINBOND_GetECCResult(const NAND_SPI_INST * pInst, FS_NAND_ECC_RESULT * pResult) {
  U8  Status;
  U8  CorrectionStatus;
  U8  MaxNumBitErrorsCorrected;
  int r;
  U8  eccStatus;

  r                        = 0;
  CorrectionStatus         = FS_NAND_CORR_NOT_APPLIED;
  MaxNumBitErrorsCorrected = 0;                         // Set to indicate that the device is not able to report the number of bit errors corrected.
  //
  // Read the status of the last page read operation to find out if any uncorrectable bit errors occurred.
  //
  Status = _ReadStatus(pInst);
  if ((Status & STATUS_IN_PROGRESS) != 0u) {
    r = 1;          // Could not read status.
  } else {
    if (_IsReadError(Status) != 0) {
      CorrectionStatus = FS_NAND_CORR_FAILURE;
    } else {
      Status &= STATUS_READ_ERROR_MASK;
      if (   (Status == STATUS_READ_ERROR_CORRECTED)
          || (Status == STATUS_READ_ERROR_CORRECTED_EX)) {
        CorrectionStatus = FS_NAND_CORR_APPLIED;
        //
        // The devices with a HW ECC that is able to correct
        // more than 1 bit error are able to report the number
        // of bit errors corrected.
        //
        eccStatus = 0;
        r = _GetFeatures(pInst, FEAT_ADDR_ECC_STATUS, &eccStatus);
        if (r == 0) {
          MaxNumBitErrorsCorrected = eccStatus >> ECC_STATUS_MBF_BIT;
        }
      }
    }
  }
  //
  // Return the calculated values.
  //
  pResult->CorrectionStatus    = CorrectionStatus;
  pResult->MaxNumBitsCorrected = MaxNumBitErrorsCorrected;
  return r;
}

/*********************************************************************
*
*      _GIGADEVICE_Identify
*
*  Function description
*    Checks if the connected device is a GigaDevice serial NAND flash.
*
*  Parameters
*    pInst      Physical layer instance.
*    pId        [IN] Device id information.
*
*  Return value
*    ==0      OK, this is the expected GigaDevice NAND flash.
*    !=0      OK, this is not the expected GigaDevice NAND flash.
*
*  Additional information
*    The NAND flash devices that are successfully identified by
*    this function expect a dummy byte after the command byte
*    that initiates the transfer of the data from the internal
*    register of the NAND flash to host. In addition, these
*    NAND flash devices report ECC errors via three bits located
*    in the status register.
*/
static int _GIGADEVICE_Identify(NAND_SPI_INST * pInst, const U8 * pId) {
  int r;
  U8  MfgId;
  U8  DeviceId;

  r        = 1;                               // Device not supported.
  MfgId    = *pId;                            // The first byte is the manufacturer id.
  DeviceId = *(pId + 1);
  if (MfgId == MFG_ID_GIGADEVICE) {
    //
    // The following GigaDevice devices are supported:
    //
    // Id          Device
    // ------------------
    // 0xC8 0xB1   GigaDevice GD5F1GQ4UF
    //
    if (DeviceId == 0xB1u) {
      pInst->ldBytesPerPage          = 11;    // 2048 bytes
      pInst->ldPagesPerBlock         = 6;     // 64 pages
      pInst->ldBlocksPerDie          = 10;    // 1024 blocks
      pInst->ldNumDies               = 0;
      pInst->BytesPerSpareArea       = 64;    // The spare are is actually 128 bytes large but the last 64 bytes are used to store the ECC.
      pInst->NumBitErrorsCorrectable = 8;     // The data sheet states that the device is capable of correcting up to 4 bit errors but our tests show that up to 8 bit errors can be corrected.
      pInst->ldNumECCBlocks          = 2;
      pInst->HasHW_ECC               = 1;
      r = 0;                                  // This device is supported.
    }
  }
  return r;
}

/*********************************************************************
*
*      _GIGADEVICE_IdentifyEnhanced
*
*  Function description
*    Checks if the connected device is a GigaDevice serial NAND flash.
*
*  Parameters
*    pInst      Physical layer instance.
*    pId        [IN] Device id information.
*
*  Return value
*    ==0      OK, this is the expected GigaDevice NAND flash.
*    !=0      OK, this is not the expected GigaDevice NAND flash.
*/
static int _GIGADEVICE_IdentifyEnhanced(NAND_SPI_INST * pInst, const U8 * pId) {
  int r;
  U8  MfgId;
  U8  DeviceId;

  r        = 1;                               // Device not supported.
  MfgId    = *pId;                            // The first byte is the manufacturer id.
  DeviceId = *(pId + 1);
  if (MfgId == MFG_ID_GIGADEVICE) {
    //
    // The following GigaDevice devices are supported:
    //
    // Id          Device
    // ------------------
    // 0xC8 0x52   GigaDevice GD5F2GQ5UE
    //
    if (DeviceId == 0x52u) {
      pInst->ldBytesPerPage          = 11;    // 2048 bytes
      pInst->ldPagesPerBlock         = 6;     // 64 pages
      pInst->ldBlocksPerDie          = 11;    // 2048 blocks
      pInst->ldNumDies               = 0;
      pInst->ldNumPlanes             = 1;     // The data sheet is not explicit about the number of planes. However the copy operation works only between block indexes with the same parity.
      pInst->BytesPerSpareArea       = 64;    // The spare are is actually 128 bytes large but the last 64 bytes are used to store the ECC.
      pInst->NumBitErrorsCorrectable = 4;
      pInst->ldNumECCBlocks          = 2;
      pInst->HasHW_ECC               = 1;
      r = 0;                                  // This device is supported.
    }
  }
  return r;
}

/*********************************************************************
*
*      _GIGADEVICE_ReadApplyPara
*
*  Function description
*    Prepares the NAND flash device for data access.
*
*  Parameters
*    pInst      Physical layer instance.
*    pId        [IN] Device id information.
*
*  Return value
*    ==0      OK, the device is ready for operation.
*    !=0      An error occurred.
*/
static int _GIGADEVICE_ReadApplyPara(NAND_SPI_INST * pInst, const U8 * pId) {
  int      r;
  U8       CmdRead;
  U8       CmdWrite;
  unsigned BusWidthRead;
  unsigned BusWidthWrite;
  U8       Feat;

  FS_USE_PARA(pId);
  //
  // Configure the commands for reading and writing data.
  //
  CmdWrite        = pInst->CmdWrite;
  BusWidthWrite   = pInst->BusWidthWrite;
  //
  // We have to use a different read command for single SPI mode
  // than for the other NAND flash devices because the 0x03 read
  // command uses a different format than the read commands
  // for dual and quad.
  //
  CmdRead         = CMD_READ_DATA_X1;
  BusWidthRead    = FS_BUSWIDTH_MAKE(1uL, 1uL, 1uL);
  if (pInst->Allow2bitMode != 0u) {
    CmdRead       = CMD_READ_DATA_X2;
    BusWidthRead  = FS_BUSWIDTH_MAKE(1uL, 1uL, 2uL);
  }
  if (pInst->Allow4bitMode != 0u) {
    CmdRead       = CMD_READ_DATA_X4;
    BusWidthRead  = FS_BUSWIDTH_MAKE(1uL, 1uL, 4uL);
    CmdWrite      = CMD_LOAD_PROG_RAND_X4;
    BusWidthWrite = FS_BUSWIDTH_MAKE(1uL, 1uL, 4uL);
  }
  pInst->CmdRead       = CmdRead;
  pInst->BusWidthRead  = (U16)BusWidthRead;
  pInst->CmdWrite      = CmdWrite;
  pInst->BusWidthWrite = (U16)BusWidthWrite;
  //
  // Unlock all the device blocks.
  //
  r = _SetFeatures(pInst, FEAT_ADDR_BLOCK_LOCK, 0);
  if (r == 0) {
    //
    // Initially, we access memory array without HW ECC. The HW ECC will be enabled by
    // the Universal NAND driver as needed. Doing this allows us to use the software
    // ECC to correct bit errors if required. In addition, we enable quad operation
    // in the NAND flash if required.
    //
    Feat = 0;
    if (pInst->Allow4bitMode != 0u) {
      Feat |= FEAT_QE;
    }
    r = _SetFeatures(pInst, FEAT_ADDR_OTP, Feat);
#if FS_SUPPORT_TEST
    if (((unsigned)pInst->BytesPerSpareArea << 1u) > (unsigned)(MAX_SPARE_AREA_SIZE)) {
      r = 1;                // Spare area buffer too small.
    }
#endif // FS_SUPPORT_TEST
  }
  return r;
}

/*********************************************************************
*
*      _GIGADEVICE_ReadDataFromCache
*
*  Function description
*    Transfers data from NAND flash to host.
*
*  Parameters
*    pInst        Driver instance.
*    PageIndex    Index of the page to read from.
*    pData        Data read from the page.
*    Off          Byte offset to read from.
*    NumBytes     Number of bytes to be read.
*
*  Return value
*    ==0      OK, data read successfully.
*    !=0      An error occurred.
*/
static int _GIGADEVICE_ReadDataFromCache(const NAND_SPI_INST * pInst, U32 PageIndex, void * pData, unsigned Off, unsigned NumBytes) {
  unsigned BusWidth;
  U8       abPara[NUM_BYTES_OFF + 2 * NUM_BYTES_DUMMY];   // The read offset is enclosed by two dummy bytes.
  U8       Cmd;
  int      r;
  unsigned NumBytesAddr;

  FS_USE_PARA(PageIndex);
  FS_MEMSET(abPara, 0xFF, sizeof(abPara));
  FS_StoreU16BE(&abPara[1], Off);                         // A dummy byte is sent before and after the offset.
  NumBytesAddr = NUM_BYTES_OFF + 1;                       // The first dummy byte has to be sent as address.
  BusWidth = pInst->BusWidthRead;
  Cmd      = pInst->CmdRead;
  r = _ReadData(pInst, Cmd, abPara, sizeof(abPara), NumBytesAddr, SEGGER_PTR2PTR(U8, pData), NumBytes, BusWidth);
  return r;
}

/*********************************************************************
*
*       _GIGADEVICE_IsReadError
*
*  Function description
*    Checks if an error occurred during the read operation (ECC error).
*
*  Parameters
*    Status   Value of the status register read from the NAND flash device.
*
*  Return value
*    ==0      The read operation was successful.
*    ==1      Uncorrectable bit errors detected.
*/
static int _GIGADEVICE_IsReadError(U8 Status) {
  int r;

  r = 0;        // Set to indicate that no error occurred.
  Status &= STATUS_READ_ERROR_MASK_EX;
  if (Status == STATUS_READ_ERROR_NOT_CORRECTED_EX) {
    r = 1;
  }
  return r;
}

/*********************************************************************
*
*       _GIGADEVICE_GetECCResult
*
*  Function description
*    Returns the result of the ECC correction status.
*
*  Parameters
*    pInst      Physical layer instance.
*    pResult    [OUT] The ECC correction result.
*
*  Return value
*    ==0      OK, status returned
*    !=0      An error occurred
*/
static int _GIGADEVICE_GetECCResult(const NAND_SPI_INST * pInst, FS_NAND_ECC_RESULT * pResult) {
  U8  Status;
  U8  CorrectionStatus;
  U8  MaxNumBitErrorsCorrected;
  int r;

  //
  // Initialize local variables.
  //
  r                        = 0;
  CorrectionStatus         = FS_NAND_CORR_NOT_APPLIED;
  MaxNumBitErrorsCorrected = 0;
  //
  // Read the status of the last page read operation to find out if any uncorrectable bit errors occurred.
  //
  Status = _ReadStatus(pInst);
  if ((Status & STATUS_IN_PROGRESS) != 0u) {
    r = 1;            // Error, could not read status.
  } else {
    if (_GIGADEVICE_IsReadError(Status) != 0) {
      CorrectionStatus = FS_NAND_CORR_FAILURE;
    } else {
      //
      // GigaDevice GD5F1GQ4UFYIG reports the approximate number of bit errors corrected
      // if the number of bit errors is smaller than or equal to 3. For data reliability
      // reasons, we return the highest number of bit errors corrected.
      //
      Status &= STATUS_READ_ERROR_MASK_EX;
      if (Status == STATUS_READ_ERROR_CORRECTED_1_3) {
        MaxNumBitErrorsCorrected = 3;
      } else if (Status == STATUS_READ_ERROR_CORRECTED_4) {
        MaxNumBitErrorsCorrected = 4;
      } else if (Status == STATUS_READ_ERROR_CORRECTED_5) {
        MaxNumBitErrorsCorrected = 5;
      } else if (Status == STATUS_READ_ERROR_CORRECTED_6) {
        MaxNumBitErrorsCorrected = 6;
      } else if (Status == STATUS_READ_ERROR_CORRECTED_7) {
        MaxNumBitErrorsCorrected = 7;
      } else if (Status == STATUS_READ_ERROR_CORRECTED_8) {
        MaxNumBitErrorsCorrected = 8;
      } else {
        //
        // Unknown number of corrected bit errors.
        //
        MaxNumBitErrorsCorrected = 0;
      }
      if (MaxNumBitErrorsCorrected != 0u) {
        CorrectionStatus = FS_NAND_CORR_APPLIED;
      }
    }
  }
  //
  // Return the determined values.
  //
  pResult->CorrectionStatus    = CorrectionStatus;
  pResult->MaxNumBitsCorrected = MaxNumBitErrorsCorrected;
  return r;
}

/*********************************************************************
*
*       _GIGADEVICE_GetECCResultEnhanced
*
*  Function description
*    Returns the result of the ECC correction status.
*
*  Parameters
*    pInst      Physical layer instance.
*    pResult    [OUT] The ECC correction result.
*
*  Return value
*    ==0      OK, status returned
*    !=0      An error occurred
*/
static int _GIGADEVICE_GetECCResultEnhanced(const NAND_SPI_INST * pInst, FS_NAND_ECC_RESULT * pResult) {
  U8  Status;
  U8  CorrectionStatus;
  U8  MaxNumBitErrorsCorrected;
  U8  eccStatus;
  int r;

  //
  // Initialize local variables.
  //
  r                        = 0;
  CorrectionStatus         = FS_NAND_CORR_NOT_APPLIED;
  MaxNumBitErrorsCorrected = 0;
  //
  // Read the status of the last page read operation to find out if any uncorrectable bit errors occurred.
  //
  Status = _ReadStatus(pInst);
  if ((Status & STATUS_IN_PROGRESS) != 0u) {
    r = 1;              // Error, could not read status.
  } else {
    if (_IsReadError(Status) != 0) {
      CorrectionStatus = FS_NAND_CORR_FAILURE;
    } else {
      //
      // Analyze the status and get the number of bit errors.
      //
      Status &= STATUS_READ_ERROR_MASK;
      if ((Status & STATUS_READ_ERROR_CORRECTED) != 0u) {
        eccStatus = 0;
        r = _GetFeatures(pInst, FEAT_ADDR_STATUS_EX, &eccStatus);
        if (r == 0) {
          eccStatus  &= STATUS_READ_ERROR_MASK;
          eccStatus >>= ECC_STATUS_BIT;
          MaxNumBitErrorsCorrected = eccStatus + 1u;
        }
        CorrectionStatus = FS_NAND_CORR_APPLIED;
      }
    }
  }
  //
  // Return the calculated values.
  //
  pResult->CorrectionStatus    = CorrectionStatus;
  pResult->MaxNumBitsCorrected = MaxNumBitErrorsCorrected;
  return r;
}

/*********************************************************************
*
*      _ALLIANCEMEMORY_Identify
*
*  Function description
*    Identifies an Alliance Memory NAND flash device.
*
*  Parameters
*    pInst        Instance of the physical layer.
*    pId          [IN] Device id.
*
*  Return value
*    ==0      This is an Alliance Memory device.
*    !=0      This is not an Alliance Memory device.
*/
static int _ALLIANCEMEMORY_Identify(NAND_SPI_INST * pInst, const U8 * pId) {
  int r;
  U8  MfgId;

  FS_USE_PARA(pInst);
  r = 1;          // Not an Alliance Memory device.
  MfgId = *pId;   // The first byte is the manufacturer id.
  if (MfgId == MFG_ID_ALLIANCEMEMORY) {
    r = 0;        // This is an Alliance Memory device.
  }
  return r;
}

/*********************************************************************
*
*      _ALLIANCEMEMORY_ReadApplyPara
*
*  Function description
*    Reads parameters from the NAND flash device and stores them
*    to the instance of the physical layer.
*
*  Parameters
*    pInst        Instance of the physical layer.
*    pId          [IN] Device id.
*
*  Return value
*    ==0      OK, parameters successfully read.
*    !=0      An error occurred.
*/
static int _ALLIANCEMEMORY_ReadApplyPara(NAND_SPI_INST * pInst, const U8 * pId) {
  int      r;
  unsigned BytesPerSpareArea;
  U8       Feat;

  r = _ReadApplyParaEx(pInst, pId, PAGE_INDEX_ONFI_EX);
  if (r == 0) {
    //
    // All supported devices have internal HW ECC.
    //
    pInst->HasHW_ECC = 1;
    //
    // The ECC parity checksums are stored in one block at the end
    // of the spare area. In addition, only the bytes starting at
    // the byte offset 4 to the end of a spare area stripe are
    // protected by ECC which matches the way the Universal NAND driver
    // stores the data to the spare area.
    // For this reason we report that the spare area is smaller
    // than actually is by leaving the area that stores the ECC
    // parity checksum out.
    //
    BytesPerSpareArea = pInst->BytesPerSpareArea;
    if (BytesPerSpareArea == 128u) {
      BytesPerSpareArea = 72;
    } else {
      if (BytesPerSpareArea == 256u) {
        BytesPerSpareArea = 144;
      }
    }
    pInst->BytesPerSpareArea = (U16)BytesPerSpareArea;
    //
    // Unlock all the device blocks.
    //
    r = _SetFeatures(pInst, FEAT_ADDR_BLOCK_LOCK, 0);
    if (r == 0) {
      //
      // Initially, we access memory array without HW ECC. The HW ECC will be enabled by
      // the Universal NAND driver as needed. By doing this we use the software
      // ECC to correct bit errors if required. In addition, we enable quad operation
      // in the NAND flash if required.
      //
      Feat = 0;
      if (pInst->Allow4bitMode != 0u) {
        Feat |= FEAT_QE;
      }
      r = _SetFeatures(pInst, FEAT_ADDR_OTP, Feat);
    }
  }
  return r;
}

/*********************************************************************
*
*       _ALLIANCEMEMORY_GetECCResult
*
*  Function description
*    Returns the result of the ECC correction status.
*
*  Parameters
*    pInst        Instance of the physical layer.
*    pResult      [OUT] ECC correction result.
*
*  Return value
*    ==0      OK, status returned.
*    !=0      An error occurred.
*/
static int _ALLIANCEMEMORY_GetECCResult(const NAND_SPI_INST * pInst, FS_NAND_ECC_RESULT * pResult) {
  U8  Status;
  U8  CorrectionStatus;
  U8  MaxNumBitErrorsCorrected;
  int r;

  r                        = 0;
  CorrectionStatus         = FS_NAND_CORR_NOT_APPLIED;
  MaxNumBitErrorsCorrected = 0;                         // Set to indicate that the device is not able to report the number of bit errors corrected.
  //
  // Read the status of the last page read operation to find out if any uncorrectable bit errors occurred.
  //
  Status = _ReadStatus(pInst);
  if ((Status & STATUS_IN_PROGRESS) != 0u) {
    r = 1;          // Could not read status.
  } else {
    if (_IsReadError(Status) != 0) {
      CorrectionStatus = FS_NAND_CORR_FAILURE;
    } else {
      Status &= STATUS_READ_ERROR_MASK;
      if (   (Status == STATUS_READ_ERROR_CORRECTED)
          || (Status == STATUS_READ_ERROR_CORRECTED_EX)) {
        //
        // The Alliance Memory devices are not able to report the number of bit errors corrected.
        // For data reliability reasons, we report that the number of bit errors corrected is
        // equal to the maximum number of bit errors the HW ECC is able to correct.
        //
        CorrectionStatus         = FS_NAND_CORR_APPLIED;
        MaxNumBitErrorsCorrected = pInst->NumBitErrorsCorrectable;
      }
    }
  }
  //
  // Return the calculated values.
  //
  pResult->CorrectionStatus    = CorrectionStatus;
  pResult->MaxNumBitsCorrected = MaxNumBitErrorsCorrected;
  return r;
}

/*********************************************************************
*
*      _DeviceISSI
*/
static const FS_NAND_SPI_DEVICE_TYPE _DeviceISSI = {
  _ISSI_Identify,
  _ISSI_ReadApplyPara,
  _ISSI_ReadDataFromCache,
  _ISSI_WriteDataToCache,
  _GetECCResult,
  NULL,
  _IsReadError,
  NULL,
  NULL
};

/*********************************************************************
*
*      _DeviceMacronix
*/
static const FS_NAND_SPI_DEVICE_TYPE _DeviceMacronix = {
  _MACRONIX_Identify,
  _MACRONIX_ReadApplyPara,
  _ReadDataFromCache,
  _WriteDataToCache,
  _MACRONIX_GetECCResult,
  NULL,
  _IsReadError,
  NULL,
  NULL
};

/*********************************************************************
*
*      _DeviceMacronix
*/
static const FS_NAND_SPI_DEVICE_TYPE _DeviceMacronixNoHW_ECC = {
  _MACRONIX_IdentifyNoHW_ECC,
  _MACRONIX_ReadApplyPara,
  _MACRONIX_ReadDataFromCache,
  _WriteDataToCache,
  NULL,
  NULL,
  _IsReadError,
  NULL,
  NULL
};

/*********************************************************************
*
*      _DeviceMicronLegacy
*/
static const FS_NAND_SPI_DEVICE_TYPE _DeviceMicronLegacy = {
  _MICRON_IdentifyLegacy,
  _MICRON_ReadApplyParaLegacy,
  _ReadDataFromCache,
  _WriteDataToCache,
  _GetECCResult,
  NULL,
  _IsReadError,
  NULL,
  NULL
};

/*********************************************************************
*
*      _DeviceMicron
*/
static const FS_NAND_SPI_DEVICE_TYPE _DeviceMicron = {
  _MICRON_Identify,
  _MICRON_ReadApplyPara,
  _MICRON_ReadDataFromCache,
  _MICRON_WriteDataToCache,
  _MICRON_GetECCResult,
  NULL,
  _IsReadError,
  NULL,
  NULL
};

/*********************************************************************
*
*      _DeviceMicronStacked
*/
static const FS_NAND_SPI_DEVICE_TYPE _DeviceMicronStacked = {
  _MICRON_IdentifyStacked,
  _MICRON_ReadApplyParaStacked,
  _MICRON_ReadDataFromCache,
  _MICRON_WriteDataToCache,
  _MICRON_GetECCResult,
  _MICRON_SelectDie,
  _IsReadError,
  NULL,
  NULL
};

/*********************************************************************
*
*      _DeviceToshiba
*/
static const FS_NAND_SPI_DEVICE_TYPE _DeviceToshiba = {
  _TOSHIBA_Identify,
  _TOSHIBA_ReadApplyPara,
  _ReadDataFromCache,
#if FS_SUPPORT_TEST
  _WriteDataToCacheWithECCPreserved,
#else
  _WriteDataToCache,
#endif
  _TOSHIBA_GetECCResult,
  NULL,
  _IsReadError,
  _TOSHIBA_BeginPageCopy,
  _TOSHIBA_EndPageCopy
};

/*********************************************************************
*
*      _DeviceWinbond
*/
static const FS_NAND_SPI_DEVICE_TYPE _DeviceWinbond = {
  _WINBOND_Identify,
  _WINBOND_ReadApplyPara,
  _ReadDataFromCache,
  _WriteDataToCache,
  _GetECCResult,
  NULL,
  _IsReadError,
  NULL,
  NULL
};

/*********************************************************************
*
*      _DeviceWinbondStacked
*/
static const FS_NAND_SPI_DEVICE_TYPE _DeviceWinbondStacked = {
  _WINBOND_IdentifyStacked,
  _WINBOND_ReadApplyParaStacked,
  _ReadDataFromCache,
  _WriteDataToCache,
  _GetECCResult,
  _WINBOND_SelectDie,
  _IsReadError,
  NULL,
  NULL
};

/*********************************************************************
*
*      _DeviceWinbondEnhanced
*/
static const FS_NAND_SPI_DEVICE_TYPE _DeviceWinbondEnhanced = {
  _WINBOND_IdentifyEnhanced,
  _WINBOND_ReadApplyParaEnhanced,
  _ReadDataFromCache,
#if FS_SUPPORT_TEST
  _WriteDataToCacheWithECCPreserved,
#else
  _WriteDataToCache,
#endif
  _WINBOND_GetECCResult,
  NULL,
  _IsReadError,
  NULL,
  NULL
};

/*********************************************************************
*
*      _DeviceGigaDevice
*/
static const FS_NAND_SPI_DEVICE_TYPE _DeviceGigaDevice = {
  _GIGADEVICE_Identify,
  _GIGADEVICE_ReadApplyPara,
  _GIGADEVICE_ReadDataFromCache,
#if FS_SUPPORT_TEST
  _WriteDataToCacheWithECCPreserved,
#else
  _WriteDataToCache,
#endif
  _GIGADEVICE_GetECCResult,
  NULL,
  _GIGADEVICE_IsReadError,
  NULL,
  NULL
};

/*********************************************************************
*
*      _DeviceGigaDeviceEnhanced
*/
static const FS_NAND_SPI_DEVICE_TYPE _DeviceGigaDeviceEnhanced = {
  _GIGADEVICE_IdentifyEnhanced,
  _GIGADEVICE_ReadApplyPara,
  _ReadDataFromCache,
#if FS_SUPPORT_TEST
  _WriteDataToCacheWithECCPreserved,
#else
  _WriteDataToCache,
#endif
  _GIGADEVICE_GetECCResultEnhanced,
  NULL,
  _IsReadError,
  NULL,
  NULL
};

/*********************************************************************
*
*      _DeviceAllianceMemory
*/
static const FS_NAND_SPI_DEVICE_TYPE _DeviceAllianceMemory = {
  _ALLIANCEMEMORY_Identify,
  _ALLIANCEMEMORY_ReadApplyPara,
  _ReadDataFromCache,
#if FS_SUPPORT_TEST
  _WriteDataToCacheWithECCPreserved,
#else
  _WriteDataToCache,
#endif
  _ALLIANCEMEMORY_GetECCResult,
  NULL,
  _IsReadError,
  NULL,
  NULL
};

/*********************************************************************
*
*      _DeviceDefault
*/
static const FS_NAND_SPI_DEVICE_TYPE _DeviceDefault = {
  NULL,
  _ReadApplyPara,
  _ReadDataFromCache,
  _WriteDataToCache,
  _GetECCResult,
  NULL,
  _IsReadError,
  NULL,
  NULL
};

/*********************************************************************
*
*      _apDeviceAll
*
*  Description
*    List of all supported device types.
*
*  Notes
*    (1) Typically, the order of the initializers is significant for
*        entries of the same manufacturer.
*/
static const FS_NAND_SPI_DEVICE_TYPE * _apDeviceAll[] = {
  &_DeviceISSI,
  &_DeviceMacronixNoHW_ECC,
  &_DeviceMacronix,
  &_DeviceMicron,
  &_DeviceMicronLegacy,
  &_DeviceMicronStacked,
  &_DeviceToshiba,
  &_DeviceWinbondStacked,
  &_DeviceWinbondEnhanced,
  &_DeviceWinbond,
  &_DeviceGigaDevice,
  &_DeviceGigaDeviceEnhanced,
  &_DeviceAllianceMemory,
  &_DeviceDefault
};

/*********************************************************************
*
*       _apDeviceDefault
*/
static const FS_NAND_SPI_DEVICE_TYPE * _apDeviceDefault[] = {
  &_DeviceDefault
};

/*********************************************************************
*
*       _apDeviceISSI
*/
static const FS_NAND_SPI_DEVICE_TYPE * _apDeviceISSI[] = {
  &_DeviceISSI
};

/*********************************************************************
*
*       _apDeviceMacronix
*/
static const FS_NAND_SPI_DEVICE_TYPE * _apDeviceMacronix[] = {
  &_DeviceMacronixNoHW_ECC,
  &_DeviceMacronix
};

/*********************************************************************
*
*       _apDeviceMicron
*/
static const FS_NAND_SPI_DEVICE_TYPE * _apDeviceMicron[] = {
  &_DeviceMicron,
  &_DeviceMicronLegacy,
  &_DeviceMicronStacked
};

/*********************************************************************
*
*       _apDeviceToshiba
*/
static const FS_NAND_SPI_DEVICE_TYPE * _apDeviceToshiba[] = {
  &_DeviceToshiba
};

/*********************************************************************
*
*       _apDeviceWinbond
*/
static const FS_NAND_SPI_DEVICE_TYPE * _apDeviceWinbond[] = {
  &_DeviceWinbondStacked,
  &_DeviceWinbondEnhanced,
  &_DeviceWinbond
};

/*********************************************************************
*
*       _apDeviceGigaDevice
*/
static const FS_NAND_SPI_DEVICE_TYPE * _apDeviceGigaDevice[] = {
  &_DeviceGigaDevice
};

/*********************************************************************
*
*       _apDeviceAllianceMemory
*/
static const FS_NAND_SPI_DEVICE_TYPE * _apDeviceAllianceMemory[] = {
  &_DeviceAllianceMemory
};

/*********************************************************************
*
*      _IdentifyDeviceEx
*
*  Function description
*    Tries to identify the NAND flash device using the manufacturer
*    and the device id.
*
*  Parameters
*    pInst            Physical layer instance.
*    pDeviceId        [OUT] Response to READ ID command.
*    SizeOfDeviceId   Maximum number of bytes that can be stored to pDeviceId.
*    DeviceIdType     Type of answer returned to READ ID command.
*
*  Return value
*    ==0    OK, device identified.
*    !=0    Could not identify device.
*/
static int _IdentifyDeviceEx(NAND_SPI_INST * pInst, U8 * pDeviceId, unsigned SizeOfDeviceId, int DeviceIdType) {
  const FS_NAND_SPI_DEVICE_TYPE  * pDevice;
  const FS_NAND_SPI_DEVICE_TYPE ** ppDevice;
  const FS_NAND_SPI_DEVICE_LIST  * pDeviceList;
  int                              r;
  unsigned                         NumDevices;
  unsigned                         iDevice;

  pDevice     = NULL;
  pDeviceList = pInst->pDeviceList;
  NumDevices  = pDeviceList->NumDevices;
  ppDevice    = pDeviceList->ppDevice;
  FS_MEMSET(pDeviceId, 0, SizeOfDeviceId);
  (void)_ReadId(pInst, pDeviceId, SizeOfDeviceId, DeviceIdType);
  //
  // A value of 0xFF or 0x00 is not a valid manufacturer id and it typically indicates
  // that the device did not respond to read id command.
  //
  if ((*pDeviceId == 0xFFu) || (*pDeviceId == 0x00u)) {
    return 1;                         // Error, could not identify device.
  }
  for (iDevice = 0; iDevice < NumDevices; ++iDevice) {
    pDevice = *ppDevice;
    if (pDevice->pfIdentify == NULL) {
      break;                          // OK, device found.
    }
    r = pDevice->pfIdentify(pInst, pDeviceId);
    if (r == 0) {
      break;                          // OK, device found.
    }
    ++ppDevice;
  }
  if (iDevice == NumDevices) {
    return 1;                         // Error, could not identify device.
  }
  pInst->pDevice = pDevice;
  return 0;
}

/*********************************************************************
*
*      _IdentifyDevice
*
*  Function description
*    Tries to identify the NAND flash device using the manufacturer
*    and the device id.
*
*  Parameters
*    pInst            Physical layer instance.
*    pDeviceId        [OUT] Response to READ ID command.
*    SizeOfDeviceId   Maximum number of bytes that can be stored to pDeviceId.
*
*  Return value
*    ==0    OK, device identified.
*    !=0    Could not identify device.
*/
static int _IdentifyDevice(NAND_SPI_INST * pInst, U8 * pDeviceId, unsigned SizeOfDeviceId) {
  int r;
  int DeviceIdType;

  r = 1;
  for (DeviceIdType = DEVICE_ID_TYPE_ENHANCED; DeviceIdType < DEVICE_ID_TYPE_COUNT; ++DeviceIdType) {
    r = _IdentifyDeviceEx(pInst, pDeviceId, SizeOfDeviceId, DeviceIdType);
    if (r == 0) {
      //
      // Do not exit the loop until we have a match or
      // we checked with all the device id types.
      //
      if (pInst->pDevice->pfIdentify != NULL) {
        break;          // OK, device identified.
      }
    }
  }
  return r;
}

/*********************************************************************
*
*      _SelectDie
*
*  Function description
*    Selects a die by its index.
*
*  Parameters
*    pInst            Physical layer instance.
*    DieIndex         Index of the die to be selected (0-based)
*
*  Return value
*    ==0    OK, die selected.
*    !=0    An error occurred.
*/
static int _SelectDie(NAND_SPI_INST * pInst, unsigned DieIndex) {
  int      r;
  unsigned ldNumDies;
  unsigned NumDies;

  r         = 0;        // Set to indicate success.
  ldNumDies = pInst->ldNumDies;
  if (ldNumDies > 0u) {
    NumDies = 1uL << ldNumDies;
    if (DieIndex >= NumDies) {
      r = 1;            // Error, invalid die index.
    } else {
      if (pInst->pDevice->pfSelectDie != NULL) {
        r = pInst->pDevice->pfSelectDie(pInst, DieIndex);
      }
    }
  }
  return r;
}

/*********************************************************************
*
*      _IsDieSelectable
*
*  Function description
*    Checks if a die can be selected.
*
*  Parameters
*    pInst            Physical layer instance.
*
*  Return value
*    ==0    Die cannot be selected.
*    ==1    Die can be selected.
*/
static int _IsDieSelectable(const NAND_SPI_INST * pInst) {
  int r;

  r = 0;
  if (pInst->pDevice->pfSelectDie != NULL) {
    r = 1;
  }
  return r;
}

/*********************************************************************
*
*      _SelectDieByPage
*
*  Function description
*    Selects a die by the index of a page located on that die.
*
*  Parameters
*    pInst            Physical layer instance.
*    PageIndex        Index of the page located in the die to be selected.
*
*  Return value
*    ==0        OK, die selected successfully.
*    !=0        An error occurred.
*/
static int _SelectDieByPage(NAND_SPI_INST * pInst, U32 PageIndex) {
  int      r;
  unsigned ldNumDies;
  unsigned DieIndex;
  U32      BlocksPerDie;
  unsigned ldPagesPerBlock;
  U32      PagesPerDie;
  unsigned ldPagesPerDie;

  r         = 0;        // Set to indicate success.
  ldNumDies = pInst->ldNumDies;
  if (ldNumDies > 0u) {
    if (_IsDieSelectable(pInst) != 0) {
      BlocksPerDie    = 1uL << pInst->ldBlocksPerDie;
      ldPagesPerBlock = pInst->ldPagesPerBlock;
      PagesPerDie     = BlocksPerDie << ldPagesPerBlock;
      ldPagesPerDie   = _ld(PagesPerDie);
      DieIndex        = PageIndex >> ldPagesPerDie;
      r = _SelectDie(pInst, DieIndex);
    }
  }
  return r;
}

/*********************************************************************
*
*      _CalcDieRelativePageIndex
*
*  Function description
*    Calculates the index of a page relative to the beginning of a die.
*
*  Parameters
*    pInst      Physical layer instance.
*    PageIndex  Absolute page index.
*
*  Return value
*    Calculated page index.
*/
static U32 _CalcDieRelativePageIndex(const NAND_SPI_INST * pInst, U32 PageIndex) {
  U32      BlocksPerDie;
  unsigned ldPagesPerBlock;
  U32      PagesPerDie;

  if (_IsDieSelectable(pInst) != 0) {
    ldPagesPerBlock  = pInst->ldPagesPerBlock;
    BlocksPerDie     = 1uL << pInst->ldBlocksPerDie;
    PagesPerDie      = BlocksPerDie << ldPagesPerBlock;
    PageIndex       &= PagesPerDie - 1u;
  }
  return PageIndex;
}

/*********************************************************************
*
*      _AllocInstIfRequired
*
*  Function description
*    Allocates memory for the instance of a physical layer.
*
*  Parameters
*    Unit     Index of the physical layer.
*
*  Return value
*    !=NULL   Allocated instance.
*    ==NULL   An error occurred.
*/
static NAND_SPI_INST * _AllocInstIfRequired(U8 Unit) {
  NAND_SPI_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  pInst = NULL;                     // Set to indicate an error.
  if (Unit < (U8)FS_NAND_NUM_UNITS) {
    pInst = _apInst[Unit];
    if (pInst == NULL) {
      pInst = SEGGER_PTR2PTR(NAND_SPI_INST, FS_ALLOC_ZEROED((I32)sizeof(NAND_SPI_INST), "NAND_SPI_INST"));
      if (pInst != NULL) {
        pInst->Unit              = Unit;
        pInst->pHWTypeQSPI       = &_DefaultHWLayer;
        pInst->pDeviceList       = FS_NAND_SPI_DEVICE_LIST_DEFAULT;
#if (FS_NAND_SUPPORT_COMPATIBILITY_MODE != 0)
        pInst->CompatibilityMode = FS_NAND_SUPPORT_COMPATIBILITY_MODE;
#endif // FS_NAND_SUPPORT_COMPATIBILITY_MODE != 0
        _apInst[Unit]            = pInst;
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
static NAND_SPI_INST * _GetInst(U8 Unit) {
  NAND_SPI_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  pInst = NULL;
  if (Unit < (U8)FS_NAND_NUM_UNITS) {
    pInst = _apInst[Unit];
  }
  return pInst;
}

/*********************************************************************
*
*       Static code (public via callback)
*
**********************************************************************
*/

/*********************************************************************
*
*       _PHY_Read
*
*  Function description
*    Reads data from a complete or a part of a page.
*    This code is identical for main memory and spare area; the spare area
*    is located right after the main area.
*
*  Return value
*    ==0     Data successfully transferred.
*    !=0     An error has occurred.
*/
static int _PHY_Read(U8 Unit, U32 PageIndex, void * pData, unsigned Off, unsigned NumBytes) {
  int             r;
  int             Status;
  NAND_SPI_INST * pInst;
  int             Result;
  int             IsReadError;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;           // Error, invalid parameter.
  }
  _Lock(pInst);
  r           = 0;      // Set to indicate success.
  IsReadError = 0;
  PageIndex   = _CalcPageIndex(pInst, PageIndex);
  if (_IsPageInCache(pInst, PageIndex) == 0) {
    //
    // Select the correct die if required.
    //
    r = _SelectDieByPage(pInst, PageIndex);
    if (r == 0) {
      PageIndex = _CalcDieRelativePageIndex(pInst, PageIndex);
      //
      // Copy the contents of the page from memory array to cache buffer.
      //
      r = _ReadPageToCache(pInst, PageIndex);
      if (r == 0) {
        //
        // Check the result of the read operation.
        //
        r = 1;                // Set to indicate an error.
        Status = _WaitForEndOfOperation(pInst);
        if (Status >= 0) {     // No timeout error?
          IsReadError = pInst->pDevice->pfIsReadError((U8)Status);
          if (IsReadError == 0) {
            r = 0;            // OK, data read.
          }
        }
      }
    }
  }
  //
  // Transfer data from NAND flash to host.
  //
  if ((pData != NULL) && (NumBytes != 0u)) {
    Result = pInst->pDevice->pfReadDataFromCache(pInst, PageIndex, pData, Off, NumBytes);
    if (Result != 0) {
      r = Result;
    }
  }
  if (r == 0) {
    _SetCachePageIndex(pInst, PageIndex);
  } else {
    if (IsReadError == 0) {     // Do not reset in order to be able to get later the ECC status.
      (void)_Reset(pInst);
    }
  }
  _Unlock(pInst);
  return r;
}

/*********************************************************************
*
*       _PHY_ReadEx
*
*  Function description
*    Reads data from two locations on a page.
*    Typically used to read data and spare area at once.
*
*  Return value
*    ==0    Data successfully transferred.
*    !=0    An error has occurred.
*/
static int _PHY_ReadEx(U8 Unit, U32 PageIndex, void * pData, unsigned Off, unsigned NumBytes, void * pSpare, unsigned OffSpare, unsigned NumBytesSpare) {
  int             r;
  int             Status;
  NAND_SPI_INST * pInst;
  int             Result;
  int             IsReadError;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;           // Error, invalid parameter.
  }
  _Lock(pInst);
  r           = 0;      // Set to indicate success.
  IsReadError = 0;
  PageIndex   = _CalcPageIndex(pInst, PageIndex);
  if (_IsPageInCache(pInst, PageIndex) == 0) {
    //
    // Select the correct die if required.
    //
    r = _SelectDieByPage(pInst, PageIndex);
    if (r == 0) {
      PageIndex = _CalcDieRelativePageIndex(pInst, PageIndex);
      //
      // Copy the contents of the page from memory array to cache buffer.
      //
      r = _ReadPageToCache(pInst, PageIndex);
      if (r == 0) {
        //
        // Check the result of the read operation.
        //
        r = 1;                // Set to indicate an error.
        Status = _WaitForEndOfOperation(pInst);
        if (Status >= 0) {    // No timeout error?
          IsReadError = pInst->pDevice->pfIsReadError((U8)Status);
          if (IsReadError == 0) {
            r = 0;            // OK, data read.
          }
        }
      }
    }
  }
  //
  // Transfer data from NAND flash to host.
  //
  if ((pData != NULL) && (NumBytes != 0u)) {
    Result = pInst->pDevice->pfReadDataFromCache(pInst, PageIndex, pData, Off, NumBytes);
    if (Result != 0) {
      r = Result;
    }
  }
  if ((pSpare != NULL) && (NumBytesSpare != 0u)) {
    Result = pInst->pDevice->pfReadDataFromCache(pInst, PageIndex, pSpare, OffSpare, NumBytesSpare);
    if (Result != 0) {
      r = Result;
    }
  }
  if (r == 0) {
    _SetCachePageIndex(pInst, PageIndex);
  } else {
    if (IsReadError == 0) {     // Do not reset in order to be able to get later the ECC status.
      (void)_Reset(pInst);
    }
  }
  _Unlock(pInst);
  return r;
}

/*********************************************************************
*
*       _PHY_Write
*
*  Function description
*    Writes data into a complete or a part of a page.
*    This code is identical for main memory and spare area; the spare area
*    is located right after the main area.
*
*  Return value
*    ==0    Data successfully transferred.
*    !=0    An error has occurred.
*
*  Additional information
*    The Universal NAND driver uses this function to write the information
*    related to the bad block marking. The data is written without ECC
*    (that is without relocation) which means that some of the information
*    is written to the area reserved to ECC on the ISSI IS37SML01G1
*    and IS38SML01G1 devices. This works since the Universal NAND driver
*    does not store any value in the corresponding ECC blocks and thus
*    the value of the ECC is set to all 0xFF by the NAND flash device.
*/
static int _PHY_Write(U8 Unit, U32 PageIndex, const void * pData, unsigned Off, unsigned NumBytes) {
  int             r;
  int             Status;
  NAND_SPI_INST * pInst;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;           // Invalid parameter.
  }
  _Lock(pInst);
  PageIndex = _CalcPageIndex(pInst, PageIndex);
  _SetCachePageIndex(pInst, PAGE_INDEX_INVALID);
  //
  // Select the correct die if required.
  //
  r = _SelectDieByPage(pInst, PageIndex);
  if (r == 0) {
    PageIndex = _CalcDieRelativePageIndex(pInst, PageIndex);
    //
    // Inform the NAND flash device that data will be modified.
    //
    r = _EnableWrite(pInst);
    if (r == 0) {
      //
      // Transfer the data to cache buffer of NAND flash.
      //
      r = pInst->pDevice->pfWriteDataToCache(pInst, PageIndex, pData, Off, NumBytes);
      if (r == 0) {
        //
        // Start programming data from cache to memory array.
        //
        r = _WritePageFromCache(pInst, PageIndex);
        if (r == 0) {
          //
          // Wait for the write operation to complete.
          //
          Status = _WaitForEndOfOperation(pInst);
          if ((Status < 0) || (_IsProgramError((U8)Status) != 0)) {
            (void)_Reset(pInst);
            r = 1;          // Error, could not write data.
          }
        }
      }
      (void)_DisableWrite(pInst);
    }
  }
  _Unlock(pInst);
  return r;
}

/*********************************************************************
*
*       _PHY_WriteEx
*
*  Function description
*    Writes data to 2 parts of a page.
*    Typically used to write data and spare area at the same time.
*
*  Return value
*    ==0    Data successfully transferred.
*    !=0    An error has occurred.
*/
static int _PHY_WriteEx(U8 Unit, U32 PageIndex, const void * pData, unsigned Off, unsigned NumBytes, const void * pSpare, unsigned OffSpare, unsigned NumBytesSpare) {
  int             r;
  int             Status;
  NAND_SPI_INST * pInst;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;           // Invalid parameter.
  }
  _Lock(pInst);
  PageIndex = _CalcPageIndex(pInst, PageIndex);
  _SetCachePageIndex(pInst, PAGE_INDEX_INVALID);
  //
  // Select the correct die if required.
  //
  r = _SelectDieByPage(pInst, PageIndex);
  if (r == 0) {
    PageIndex = _CalcDieRelativePageIndex(pInst, PageIndex);
    //
    // Inform the NAND flash device that data will be modified.
    //
    r = _EnableWrite(pInst);
    if (r == 0) {
      //
      // Transfer the data to cache buffer of NAND flash.
      //
      r = _WriteDataToCache(pInst, PageIndex, pData, Off, NumBytes);
      if (r == 0) {
        if ((pSpare != NULL) && (NumBytesSpare != 0u)) {
          r = pInst->pDevice->pfWriteDataToCache(pInst, PageIndex, pSpare, OffSpare, NumBytesSpare);
        }
      }
      if (r == 0) {
        //
        // Start programming data from cache to memory array.
        //
        r = _WritePageFromCache(pInst, PageIndex);
        if (r == 0) {
          //
          // Wait for the write operation to complete.
          //
          Status = _WaitForEndOfOperation(pInst);
          if ((Status < 0) || (_IsProgramError((U8)Status) != 0)) {
            (void)_Reset(pInst);
            r = 1;          // Error, could not write data.
          }
        }
      }
      (void)_DisableWrite(pInst);
    }
  }
  _Unlock(pInst);
  return r;
}

/*********************************************************************
*
*       _PHY_EraseBlock
*
*  Function description
*    Sets all the bytes in a block to 0xFF
*
*  Return value
*    ==0    Block erased.
*    !=0    An error has occurred.
*/
static int _PHY_EraseBlock(U8 Unit, U32 PageIndex) {
  int             r;
  int             Status;
  NAND_SPI_INST * pInst;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;           // Error, invalid parameter.
  }
  _Lock(pInst);
  PageIndex = _CalcPageIndex(pInst, PageIndex);
  _SetCachePageIndex(pInst, PAGE_INDEX_INVALID);
  //
  // Select the correct die if required and return the actual page index.
  //
  r = _SelectDieByPage(pInst, PageIndex);
  if (r == 0) {
    PageIndex = _CalcDieRelativePageIndex(pInst, PageIndex);
    //
    // Inform the NAND flash device that data will be modified.
    //
    r = _EnableWrite(pInst);
    if (r == 0) {
      //
      // Start the block erase operation.
      //
      r = _EraseBlock(pInst, PageIndex);
      if (r == 0) {
        //
        // Wait for the block erase operation to complete.
        //
        Status = _WaitForEndOfOperation(pInst);
        if ((Status < 0) || (_IsEraseError((U8)Status) != 0)) {
          (void)_Reset(pInst);
          r = 1;        // Error, could not erase block.
        }
      }
      (void)_DisableWrite(pInst);
    }
  }
  _Unlock(pInst);
  return r;
}

/*********************************************************************
*
*       _PHY_InitGetDeviceInfo
*
*  Function description
*    Initializes hardware layer, resets NAND flash and tries to identify the NAND flash.
*    If the NAND flash can be handled, Device Info is filled.
*
*  Return value
*    ==0    O.K., device can be handled
*    !=0    Error: device can not be handled
*
*  Notes
*    (1) We have to read 3 bytes instead of 2 because some Winbond devices
*        can be identified correctly only by using the 3rd byte returned
*        by the READ ID command. Reading more id bytes than available in the
*        device should not be a problem because typically these devices will
*        return the value of last valid id byte.
*/
static int _PHY_InitGetDeviceInfo(U8 Unit, FS_NAND_DEVICE_INFO * pDevInfo) {
  int             r;
  NAND_SPI_INST * pInst;
  U8              abDeviceInfo[3];            // Note 1
  U32             TimeOut;
  int             Freq_kHz;
  int             srpms;
  unsigned        ldBlocksPerDie;
  unsigned        ldNumDies;

  r = 1;              // Set to indicate an error.
  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    ASSERT_HW_TYPE_IS_SET(pInst);
    FS_MEMSET(abDeviceInfo, 0, sizeof(abDeviceInfo));
    //
    // Initialize hardware and reset the device
    //
    Freq_kHz = _Init(pInst);
    //
    // Calculate the number of status requests that can be executed in 1 millisecond.
    // At least 24-bits are exchanged on each NAND device status request.
    //
    srpms = ((Freq_kHz * 1000) / 24) / 1000;
    TimeOut = (U32)srpms * (U32)FS_NAND_DEVICE_OPERATION_TIMEOUT;
    pInst->TimeOut = TimeOut;
    //
    // Set safe defaults for read and write commands.
    //
    pInst->CmdRead       = CMD_READ_DATA;
    pInst->BusWidthRead  = (U16)FS_BUSWIDTH_MAKE(1uL, 1uL, 1uL);
    pInst->CmdWrite      = CMD_LOAD_PROG_RAND;
    pInst->BusWidthWrite = (U16)FS_BUSWIDTH_MAKE(1uL, 1uL, 1uL);
    //
    // Initialize the NAND flash device.
    //
    _Lock(pInst);
    (void)_Reset(pInst);
    r = _IdentifyDevice(pInst, abDeviceInfo, sizeof(abDeviceInfo));
    if (r == 0) {
      r = pInst->pDevice->pfReadApplyPara(pInst, abDeviceInfo);
      if (r == 0) {
        ldBlocksPerDie = pInst->ldBlocksPerDie;
        ldNumDies      = pInst->ldNumDies;
        //
        // Fill in the info needed by the NAND driver.
        //
        pDevInfo->BPP_Shift                   = pInst->ldBytesPerPage;
        pDevInfo->PPB_Shift                   = pInst->ldPagesPerBlock;
        pDevInfo->NumBlocks                   = (U16)(1uL << (ldBlocksPerDie + ldNumDies));
        pDevInfo->DataBusWidth                = 1;
        pDevInfo->BadBlockMarkingType         = FS_NAND_BAD_BLOCK_MARKING_TYPE_FPS;
        pDevInfo->BytesPerSpareArea           = pInst->BytesPerSpareArea;
        pDevInfo->ECC_Info.HasHW_ECC          = pInst->HasHW_ECC;
        pDevInfo->ECC_Info.NumBitsCorrectable = pInst->NumBitErrorsCorrectable;
        pDevInfo->ECC_Info.ldBytesPerBlock    = pInst->ldBytesPerPage - pInst->ldNumECCBlocks;
      }
    }
    _Unlock(pInst);
  }
  return r;
}

/*********************************************************************
*
*       _PHY_IsWP
*
*  Function description
*    Checks if the device is write protected.
*    This is done by reading bit 7 of the status register.
*    Typical reason for write protection is that either the supply voltage is too low
*    or the /WP-pin is active (low)
*
*  Return value
*    ==0    Not write protected
*    > 0    Write protected
*/
static int _PHY_IsWP(U8 Unit) {
  FS_USE_PARA(Unit);
  return 0;       // This information is not available.
}

/*********************************************************************
*
*       _PHY_EnableECC
*
*  Function description
*    Activates the internal HW ECC of NAND flash device.
*
*  Return value
*    ==0     Internal HW ECC activated
*    !=0     An error occurred
*/
static int _PHY_EnableECC(U8 Unit) {
  NAND_SPI_INST * pInst;
  int             r;
  int             Result;
  unsigned        NumDies;
  unsigned        iDie;
  unsigned        DieIndex;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;           // Invalid parameter.
  }
  _Lock(pInst);
  r = 0;                // Set to indicate success.
  NumDies = 1uL << pInst->ldNumDies;
  if (NumDies > 1u) {
    //
    // Enable the HW ECC on all dies because we don't know
    // which die will actually be accessed by the NAND driver.
    //
    DieIndex = pInst->DieIndexSelected;
    for (iDie = 0; iDie < NumDies; ++iDie) {
      Result = _SelectDie(pInst, iDie);
      if (Result != 0) {
        r = Result;
      }
      Result = _EnableECC(pInst);
      if (Result != 0) {
        r = Result;
      }
    }
    //
    // Re-select the original die.
    //
    Result = _SelectDie(pInst, DieIndex);
    if (Result != 0) {
      r = Result;
    }
  } else {
    r = _EnableECC(pInst);
  }
  _AllowPageCopy(pInst, 1);                         // Internal copy operation is allowed when the internal ECC is enabled.
  _SetCachePageIndex(pInst, PAGE_INDEX_INVALID);    // Invalidate the cache so that the NAND driver can read directly from the memory array.
  _Unlock(pInst);
  return r;
}

/*********************************************************************
*
*       _PHY_DisableECC
*
*  Function description
*    Deactivates the internal HW ECC of NAND flash device.
*
*  Return value
*    ==0     Internal HW ECC deactivated.
*    !=0     An error occurred.
*/
static int _PHY_DisableECC(U8 Unit) {
  NAND_SPI_INST * pInst;
  int             r;
  int             Result;
  unsigned        NumDies;
  unsigned        iDie;
  unsigned        DieIndex;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;           // Invalid parameter.
  }
  _Lock(pInst);
  r = 0;                // Set to indicate success.
  NumDies = 1uL << pInst->ldNumDies;
  if (NumDies > 1u) {
    //
    // Disable the HW ECC on all dies because we don't know
    // which die will actually be accessed by the NAND driver.
    //
    DieIndex = pInst->DieIndexSelected;
    for (iDie = 0; iDie < NumDies; ++iDie) {
      Result = _SelectDie(pInst, iDie);
      if (Result != 0) {
        r = Result;
      }
      Result = _DisableECC(pInst);
      if (Result != 0) {
        r = Result;
      }
    }
    //
    // Re-select the original die.
    //
    Result = _SelectDie(pInst, DieIndex);
    if (Result != 0) {
      r = Result;
    }
  } else {
    r = _DisableECC(pInst);
  }
  _AllowPageCopy(pInst, 0);                         // Internal copy operation is not allowed when the internal ECC is disabled.
  _SetCachePageIndex(pInst, PAGE_INDEX_INVALID);    // Invalidate the cache so that the NAND driver can read directly from the memory array.
  _Unlock(pInst);
  return r;
}

/*********************************************************************
*
*       _PHY_CopyPage
*
*  Function description
*    Copies the contents of a page to an other page. The destination page should be blank.
*
*  Return value
*    ==0     Page copied
*    !=0     An error occurred
*/
static int _PHY_CopyPage(U8 Unit, U32 PageIndexSrc, U32 PageIndexDest) {
  int             r;
  int             Status;
  NAND_SPI_INST * pInst;
  U8              StatusRead;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;           // Invalid parameter.
  }
  _Lock(pInst);
  r = 1;                // Set to indicate error.
  if (_IsPageCopyAllowed(pInst) != 0) {
    //
    // Some of the Micron NAND flash devices have the memory array organized in 2 planes.
    // One plane contains the odd numbered blocks while the other plane the even numbered ones.
    // Page data can be copied only between the pages on the same plane so we have to remap the blocks here.
    //
    PageIndexSrc  = _CalcPageIndex(pInst, PageIndexSrc);
    PageIndexDest = _CalcPageIndex(pInst, PageIndexDest);
    if (_IsSamePlane(pInst, PageIndexSrc, PageIndexDest) != 0) {
      //
      // Invalidate the cache.
      //
      _SetCachePageIndex(pInst, PAGE_INDEX_INVALID);
      //
      // Select the correct die if required.
      //
      r = _SelectDieByPage(pInst, PageIndexSrc);
      if (r == 0) {
        PageIndexSrc  = _CalcDieRelativePageIndex(pInst, PageIndexSrc);
        PageIndexDest = _CalcDieRelativePageIndex(pInst, PageIndexDest);
        //
        // If required, configure the device for the copy operation.
        //
        r = _BeginPageCopy(pInst);
        if (r == 0) {
          //
          // Copy the contents of the page from memory array to cache buffer.
          //
          r = _ReadPageToCache(pInst, PageIndexSrc);
          if (r == 0) {
            //
            // Check the result of the read operation.
            //
            Status = _WaitForEndOfOperation(pInst);
            if (Status >= 0) {
              r = 1;                  // Set to indicate a read error.
              if (pInst->pDevice->pfIsReadError((U8)Status) == 0) {
                //
                // No ECC errors. Write data to destination page.
                //
                r = _EnableWrite(pInst);
                if (r == 0) {
                  //
                  // Program data from cache to memory array.
                  //
                  r = _WritePageFromCache(pInst, PageIndexDest);
                  if (r == 0) {
                    Status = _WaitForEndOfOperation(pInst);
                    if (Status < 0) {
                      r = 1;          // Error, could not write data.
                    }
                    StatusRead = (U8)Status;
                    if ((StatusRead & STATUS_PROGRAM_ERROR) != 0u) {
                      r = 1;          // Error, could not write data.
                    }
                  }
                }
                (void)_DisableWrite(pInst);
              }
            }
          }
        }
        (void)_EndPageCopy(pInst);
      }
      if (r != 0) {
        (void)_Reset(pInst);
      }
    }
  }
  _Unlock(pInst);
  return r;
}

/*********************************************************************
*
*       _PHY_GetECCResult
*
*  Function description
*    Returns the result of the ECC correction status.
*
*  Return value
*    ==0      OK, status returned
*    !=0      An error occurred
*/
static int _PHY_GetECCResult(U8 Unit, FS_NAND_ECC_RESULT * pResult) {
  NAND_SPI_INST * pInst;
  int             r;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;           // Invalid parameter.
  }
  _Lock(pInst);
  //
  // Initialize the ECC correction status.
  //
  pResult->CorrectionStatus    = FS_NAND_CORR_NOT_APPLIED;
  pResult->MaxNumBitsCorrected = 0;
  //
  // Determine the actual ECC correction status.
  //
  r = pInst->pDevice->pfGetECCResult(pInst, pResult);
  _Unlock(pInst);
  return r;
}

/*********************************************************************
*
*       _PHY_DeInit
*
*  Function description
*    Frees the resources allocated by this physical layer.
*
*  Parameters
*    Unit   Index of the physical layer.
*/
static void _PHY_DeInit(U8 Unit) {
#if FS_SUPPORT_DEINIT
  NAND_SPI_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  if (Unit < (U8)FS_NAND_NUM_UNITS) {
    pInst = _apInst[Unit];
    if (pInst != NULL) {
      FS_Free(SEGGER_PTR2PTR(void, pInst));
      _apInst[Unit] = NULL;
    }
  }
#else
  FS_USE_PARA(Unit);
#endif
}

/*********************************************************************
*
*       Public code (internal)
*
**********************************************************************
*/

#if FS_SUPPORT_TEST

/*********************************************************************
*
*       FS__NAND_SPI_SetTestHookReadBegin
*/
void FS__NAND_SPI_SetTestHookReadBegin(FS_NAND_TEST_HOOK_READ_BEGIN * pfTestHook) {
  _pfTestHookReadBegin = pfTestHook;
}

/*********************************************************************
*
*       FS__NAND_SPI_SetTestHookReadEnd
*/
void FS__NAND_SPI_SetTestHookReadEnd(FS_NAND_TEST_HOOK_READ_END * pfTestHook) {
  _pfTestHookReadEnd = pfTestHook;
}

/*********************************************************************
*
*       FS__NAND_SPI_SetTestHookWriteBegin
*/
void FS__NAND_SPI_SetTestHookWriteBegin(FS_NAND_TEST_HOOK_WRITE_BEGIN * pfTestHook) {
  _pfTestHookWriteBegin = pfTestHook;
}

/*********************************************************************
*
*       FS__NAND_SPI_SetTestHookWriteEnd
*/
void FS__NAND_SPI_SetTestHookWriteEnd(FS_NAND_TEST_HOOK_WRITE_END * pfTestHook) {
  _pfTestHookWriteEnd = pfTestHook;
}

/*********************************************************************
*
*       FS__NAND_SPI_ReadONFIPara
*
*  Function description
*    Reads the ONFI parameters from NAND flash device.
*
*  Parameters
*    Unit     Index of the physical layer.
*    pPara    [OUT] Device parameters. The buffer has to be at least 256 bytes large.
*/
int FS__NAND_SPI_ReadONFIPara(U8 Unit, void * pPara) {
  NAND_SPI_INST * pInst;
  int             r;
  U16             crcRead;
  U16             crcCalc;
  int             iParaPage;
  U8              OTPFeat;
  int             Status;
  int             Result;
  U8            * pCRC;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;
  }
  _Lock(pInst);
  r = _GetFeatures(pInst, FEAT_ADDR_OTP, &OTPFeat);                     // Save the current features.
  if (r == 0) {
    r = _SetFeatures(pInst, FEAT_ADDR_OTP, OTPFeat | FEAT_OTP_ENABLE);  // Enable the access to ONFI parameters.
    if (r == 0) {
      //
      // Copy the ONFI parameters to cache buffer.
      //
      r = _ReadPageToCache(pInst, PAGE_INDEX_ONFI);
      if (r == 0) {
        //
        // Check the result of the read operation.
        //
        Status = _WaitForEndOfOperation(pInst);
        if (Status >= 0) {                        // No timeout error?
          if (pInst->pDevice->pfIsReadError((U8)Status) == 0) {
            //
            // Several identical parameter pages are stored in a device.
            // Read from the first one which stores valid information.
            //
            r = 1;
            for (iParaPage = 0; iParaPage < NUM_ONFI_PAGES; ++iParaPage) {
              Result = _ReadDataFromCache(pInst, PAGE_INDEX_ONFI, pPara, 0, ONFI_PAGE_SIZE);
              if (Result != 0) {
                break;
              }
              //
              // Check the signature.
              //
              if (_CheckONFISignature(SEGGER_PTR2PTR(U8, pPara)) != 0) {
                break;                            // Invalid parameter page.
              }
              crcCalc = FS_CRC16_CalcBitByBit(SEGGER_PTR2PTR(U8, pPara), ONFI_PAGE_SIZE - 2u, ONFI_CRC_INIT, ONFI_CRC_POLY);
              //
              // Verify the CRC.
              //
              pCRC = SEGGER_PTR2PTR(U8, pPara) + (ONFI_PAGE_SIZE - 2u);
              crcRead = FS_LoadU16LE(pCRC);
              if (crcCalc == crcRead) {
                r = 0;
                break;
              }
              //
              // Winbond devices store the CRC in big-endian format
              //
              crcRead = FS_LoadU16BE(pCRC);
              if (crcCalc == crcRead) {
                r = 0;
                break;
              }
            }
          }
        }
      }
    }
    Result = _SetFeatures(pInst, FEAT_ADDR_OTP, OTPFeat);   // Restore the old features.
    if (Result != 0) {
      r = Result;
    }
  }
  _Unlock(pInst);
  return r;
}

/*********************************************************************
*
*       FS__NAND_SPI_ReadId
*
*  Function description
*    Reads the device identification parameters.
*/
int FS__NAND_SPI_ReadId(U8 Unit, U8 * pData, unsigned NumBytes) {
  NAND_SPI_INST * pInst;
  int             r;

  r = 1;
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    _Lock(pInst);
    r = _ReadId(pInst, pData, NumBytes, DEVICE_ID_TYPE_STANDARD);
    _Unlock(pInst);
  }
  return r;
}

/*********************************************************************
*
*       FS__NAND_SPI_SetCompatibilityMode
*
*  Function description
*    Changes the compatibility mode.
*/
int FS__NAND_SPI_SetCompatibilityMode(U8 Unit, U8 Mode) {
#if (FS_NAND_SUPPORT_COMPATIBILITY_MODE > 0)
  NAND_SPI_INST * pInst;
  int             r;

  r = 1;
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    pInst->CompatibilityMode = Mode;
    r = 0;
  }
  return r;
#else
  FS_USE_PARA(Unit);
  FS_USE_PARA(Mode);
  return FS_ERRCODE_NOT_SUPPORTED;
#endif
}

/*********************************************************************
*
*       FS__NAND_SPI_EnableECC
*
*  Function description
*    Enables the ECC directly.
*/
int FS__NAND_SPI_EnableECC(U8 Unit) {
  int             r;
  NAND_SPI_INST * pInst;

  r     = 1;
  pInst = _GetInst(Unit);
  _Lock(pInst);
  if (pInst != NULL) {
    r = _EnableECC(pInst);
    pInst->IsECCEnabled = 0;
  }
  _Unlock(pInst);
  return r;
}

/*********************************************************************
*
*       FS__NAND_SPI_DisableECC
*
*  Function description
*    Disables the ECC directly.
*/
int FS__NAND_SPI_DisableECC(U8 Unit) {
  int             r;
  NAND_SPI_INST * pInst;

  r     = 1;
  pInst = _GetInst(Unit);
  _Lock(pInst);
  if (pInst != NULL) {
    r = _DisableECC(pInst);
    pInst->IsECCEnabled = 0;
  }
  _Unlock(pInst);
  return r;
}

#endif // FS_SUPPORT_TEST

/*********************************************************************
*
*       Public const data
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_NAND_PHY_SPI
*/
const FS_NAND_PHY_TYPE FS_NAND_PHY_SPI = {
  _PHY_EraseBlock,
  _PHY_InitGetDeviceInfo,
  _PHY_IsWP,
  _PHY_Read,
  _PHY_ReadEx,
  _PHY_Write,
  _PHY_WriteEx,
  _PHY_EnableECC,
  _PHY_DisableECC,
  NULL,
  _PHY_CopyPage,
  _PHY_GetECCResult,
  _PHY_DeInit,
  NULL
};

/*********************************************************************
*
*       FS_NAND_PHY_QSPI
*/
const FS_NAND_PHY_TYPE FS_NAND_PHY_QSPI = {
  _PHY_EraseBlock,
  _PHY_InitGetDeviceInfo,
  _PHY_IsWP,
  _PHY_Read,
  _PHY_ReadEx,
  _PHY_Write,
  _PHY_WriteEx,
  _PHY_EnableECC,
  _PHY_DisableECC,
  NULL,
  _PHY_CopyPage,
  _PHY_GetECCResult,
  _PHY_DeInit,
  NULL
};

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

#if FS_NAND_SUPPORT_READ_CACHE

/*********************************************************************
*
*       FS_NAND_SPI_EnableReadCache
*
*  Function description
*    Activates the page read optimization
*
*  Parameters
*    Unit       Index of the physical layer instance (0-based)
*
*  Additional information
*    This function is optional and is available only when the file system
*    is build with FS_NAND_SUPPORT_READ_CACHE set to 1 which is the default.
*    Activating the read cache can increase the overall performance of the
*    NAND driver.
*
*    The optimization takes advantage of how the NAND flash device implements
*    the read page operation. A NAND page read operation consists of two steps.
*    In the first step, the page data is read from the memory array to internal
*    page register of the NAND flash device. In the second step, the data is
*    transferred from the internal page register of NAND flash device to MCU.
*    With the optimization enabled the first step is skipped whenever possible.
*
*    The optimization is enabled by default and has to be disabled if two
*    or more instances of the NAND driver are configured to access the same
*    physical NAND flash device. At runtime, the optimization can be disabled
*    via FS_NAND_SPI_DisableReadCache().
*/
void FS_NAND_SPI_EnableReadCache(U8 Unit) {
  NAND_SPI_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->CacheStatus    = CACHE_STATUS_ENABLED;
    pInst->CachePageIndex = PAGE_INDEX_INVALID;
  }
}

/*********************************************************************
*
*       FS_NAND_SPI_DisableReadCache
*
*  Function description
*    Deactivates the page read optimization
*
*  Parameters
*    Unit       Index of the physical layer instance (0-based)
*
*  Additional information
*    This function is optional and is available only when the file system
*    is build with FS_NAND_SUPPORT_READ_CACHE set to 1 which is the default.
*    The optimization can be enabled at runtime via FS_NAND_SPI_EnableReadCache().
*
*    Refer to FS_NAND_SPI_EnableReadCache() for more information about how
*    the page read optimization works
*/
void FS_NAND_SPI_DisableReadCache(U8 Unit) {
  NAND_SPI_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->CacheStatus = CACHE_STATUS_DISABLED;
  }
}

#endif // FS_NAND_SUPPORT_READ_CACHE

/*********************************************************************
*
*       FS_NAND_SPI_SetHWType
*
*  Function description
*    Configures the hardware access routines for a NAND physical layer
*    of type FS_NAND_PHY_SPI.
*
*  Parameters
*    Unit       Index of the physical layer instance (0-based)
*    pHWType    Type of the hardware layer to use. Cannot be NULL.
*
*  Additional information
*    This function is mandatory and has to be called once in FS_X_AddDevices()
*    for every instance of a NAND physical layer of type FS_NAND_PHY_SPI.
*/
void FS_NAND_SPI_SetHWType(U8 Unit, const FS_NAND_HW_TYPE_SPI * pHWType) {
  NAND_SPI_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->pHWTypeSPI = pHWType;
  }
}

#if FS_NAND_SUPPORT_READ_CACHE

/*********************************************************************
*
*       FS_NAND_QSPI_EnableReadCache
*
*  Function description
*    Activates the page read optimization
*
*  Parameters
*    Unit       Index of the physical layer instance (0-based)
*
*  Additional information
*    This function is optional and is available only when the file system
*    is build with FS_NAND_SUPPORT_READ_CACHE set to 1 which is the default.
*    Activating the read cache can increase the overall performance of the
*    NAND driver.
*
*    The optimization takes advantage of how the NAND flash device implements
*    the read page operation. A NAND page read operation consists of two steps.
*    In the first step, the page data is read from the memory array to internal
*    page register of the NAND flash device. In the second step, the data is
*    transferred from the internal page register of NAND flash device to MCU.
*    With the optimization enabled the first step is skipped whenever possible.
*
*    The optimization is enabled by default and has to be disabled if two
*    or more instances of the NAND driver are configured to access the same
*    physical NAND flash device. At runtime, the optimization can be disabled
*    via FS_NAND_QSPI_DisableReadCache().
*/
void FS_NAND_QSPI_EnableReadCache(U8 Unit) {
  NAND_SPI_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->CacheStatus    = CACHE_STATUS_ENABLED;
    pInst->CachePageIndex = PAGE_INDEX_INVALID;
  }
}

/*********************************************************************
*
*       FS_NAND_QSPI_DisableReadCache
*
*  Function description
*    Deactivates the page read optimization
*
*  Parameters
*    Unit       Index of the physical layer instance (0-based)
*
*  Additional information
*    This function is optional and is available only when the file system
*    is build with FS_NAND_SUPPORT_READ_CACHE set to 1 which is the default.
*    The optimization can be enabled at runtime via FS_NAND_QSPI_EnableReadCache().
*
*    Refer to FS_NAND_QSPI_EnableReadCache() for more information about how
*    the page read optimization works
*/
void FS_NAND_QSPI_DisableReadCache(U8 Unit) {
  NAND_SPI_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->CacheStatus = CACHE_STATUS_DISABLED;
  }
}

#endif // FS_NAND_SUPPORT_READ_CACHE


/*********************************************************************
*
*       FS_NAND_SPI_SetDeviceList
*
*  Function description
*    Specifies the list of enabled serial NAND flash devices.
*
*  Parameters
*    Unit         Index of the physical layer (0-based)
*    pDeviceList  [IN] List of serial NAND flash devices.
*
*  Additional information
*    All supported serial NAND flash devices are enabled by default.
*    Serial NAND flash devices that are not on the list are not
*    recognized by the file system.
*
*    Permitted values for the pDeviceList parameter are:
*    +----------------------------------+-----------------------------------------------------------------------+
*    | Identifier                       | Description                                                           |
*    +----------------------------------+-----------------------------------------------------------------------+
*    | FS_NAND_SPI_DeviceListAll        | Enables handling of serial NAND flash devices from all manufacturers. |
*    +----------------------------------+-----------------------------------------------------------------------+
*    | FS_NAND_SPI_DeviceListDefault    | Enables handling of NAND flash devices from any other manufacturer.   |
*    +----------------------------------+-----------------------------------------------------------------------+
*    | FS_NAND_SPI_DeviceListISSI       | Enables handling of ISSI serial NAND flash devices.                   |
*    +----------------------------------+-----------------------------------------------------------------------+
*    | FS_NAND_SPI_DeviceListMacronix   | Enables handling of Macronix serial NAND flash devices.               |
*    +----------------------------------+-----------------------------------------------------------------------+
*    | FS_NAND_SPI_DeviceListMicron     | Enables handling of Micron serial NAND flash devices.                 |
*    +----------------------------------+-----------------------------------------------------------------------+
*    | FS_NAND_SPI_DeviceListToshiba    | Enables handling of Kioxia/Toshiba serial NAND flash devices.         |
*    +----------------------------------+-----------------------------------------------------------------------+
*    | FS_NAND_SPI_DeviceListWinbond    | Enables handling of Winbond serial NAND flash devices.                |
*    +----------------------------------+-----------------------------------------------------------------------+
*/
void FS_NAND_SPI_SetDeviceList(U8 Unit, const FS_NAND_SPI_DEVICE_LIST * pDeviceList) {
  NAND_SPI_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    if (pDeviceList != NULL) {
      pInst->pDeviceList = pDeviceList;
    }
  }
}

/*********************************************************************
*
*       FS_NAND_QSPI_SetHWType
*
*  Function description
*    Configures the hardware access routines for a NAND physical layer
*    of type FS_NAND_PHY_QSPI.
*
*  Parameters
*    Unit       Index of the physical layer instance (0-based)
*    pHWType    Type of the hardware layer to use. Cannot be NULL.
*
*  Additional information
*    This function is mandatory and has to be called once in FS_X_AddDevices()
*    for every instance of a NAND physical layer of type FS_NAND_PHY_QSPI.
*/
void FS_NAND_QSPI_SetHWType(U8 Unit, const FS_NAND_HW_TYPE_QSPI * pHWType) {
  NAND_SPI_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->pHWTypeQSPI = pHWType;
  }
}

/*********************************************************************
*
*       FS_NAND_QSPI_Allow2bitMode
*
*  Function description
*    Specifies if the physical layer can exchange data via 2 data lines.
*
*  Parameters
*    Unit         Index of the physical layer (0-based)
*    OnOff        Activation status of the option.
*                 * 0   Data is exchanged via 1 data line.
*                 * 1   Data is exchanged via 2 data lines.
*
*  Additional information
*    This function is optional. By default the data is exchanged via 1
*    data line (standard SPI mode).
*/
void FS_NAND_QSPI_Allow2bitMode(U8 Unit, U8 OnOff) {
  NAND_SPI_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->Allow2bitMode = OnOff;
  }
}

/*********************************************************************
*
*       FS_NAND_QSPI_Allow4bitMode
*
*  Function description
*    Specifies if the physical layer can exchange data via 4 data lines.
*
*  Parameters
*    Unit         Index of the physical layer (0-based)
*    OnOff        Activation status of the option.
*                 * 0   Data is exchanged via 1 data line or 2 data lines.
*                 * 1   Data is exchanged via 4 data lines.
*
*  Additional information
*    This function is optional. By default the data is exchanged via 1
*    data line (standard SPI mode).
*/
void FS_NAND_QSPI_Allow4bitMode(U8 Unit, U8 OnOff) {
  NAND_SPI_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->Allow4bitMode = OnOff;
  }
}

/*********************************************************************
*
*       FS_NAND_QSPI_SetDeviceList
*
*  Function description
*    Specifies the list of enabled serial NAND flash devices.
*
*  Parameters
*    Unit         Index of the physical layer (0-based)
*    pDeviceList  [IN] List of serial NAND flash devices.
*
*  Additional information
*    All supported serial NAND flash devices are enabled by default.
*    Serial NAND flash devices that are not on the list are not
*    recognized by the file system.
*
*    Permitted values for the pDeviceList parameter are:
*    +--------------------------------------+-----------------------------------------------------------------------+
*    | Identifier                           | Description                                                           |
*    +--------------------------------------+-----------------------------------------------------------------------+
*    | FS_NAND_SPI_DeviceListAll            | Enables handling of serial NAND flash devices from all manufacturers. |
*    +--------------------------------------+-----------------------------------------------------------------------+
*    | FS_NAND_SPI_DeviceListAllianceMemory | Enables handling of Alliance Memory serial NAND flash devices.        |
*    +--------------------------------------+-----------------------------------------------------------------------+
*    | FS_NAND_SPI_DeviceListDefault        | Enables handling of NAND flash devices from any other manufacturer.   |
*    +--------------------------------------+-----------------------------------------------------------------------+
*    | FS_NAND_SPI_DeviceListGigaDevice     | Enables handling of GigaDevice serial NAND flash devices.             |
*    +--------------------------------------+-----------------------------------------------------------------------+
*    | FS_NAND_SPI_DeviceListISSI           | Enables handling of ISSI serial NAND flash devices.                   |
*    +--------------------------------------+-----------------------------------------------------------------------+
*    | FS_NAND_SPI_DeviceListMacronix       | Enables handling of Macronix serial NAND flash devices.               |
*    +--------------------------------------+-----------------------------------------------------------------------+
*    | FS_NAND_SPI_DeviceListMicron         | Enables handling of Micron serial NAND flash devices.                 |
*    +--------------------------------------+-----------------------------------------------------------------------+
*    | FS_NAND_SPI_DeviceListToshiba        | Enables handling of Kioxia/Toshiba serial NAND flash devices.         |
*    +--------------------------------------+-----------------------------------------------------------------------+
*    | FS_NAND_SPI_DeviceListWinbond        | Enables handling of Winbond serial NAND flash devices.                |
*    +--------------------------------------+-----------------------------------------------------------------------+
*/
void FS_NAND_QSPI_SetDeviceList(U8 Unit, const FS_NAND_SPI_DEVICE_LIST * pDeviceList) {
  NAND_SPI_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    if (pDeviceList != NULL) {
      pInst->pDeviceList = pDeviceList;
    }
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
*       FS_NAND_SPI_DeviceListAll
*/
const FS_NAND_SPI_DEVICE_LIST FS_NAND_SPI_DeviceListAll = {
  (U8)SEGGER_COUNTOF(_apDeviceAll),
  _apDeviceAll
};

/*********************************************************************
*
*       FS_NAND_SPI_DeviceListDefault
*/
const FS_NAND_SPI_DEVICE_LIST FS_NAND_SPI_DeviceListDefault = {
  (U8)SEGGER_COUNTOF(_apDeviceDefault),
  _apDeviceDefault
};

/*********************************************************************
*
*       FS_NAND_SPI_DeviceListISSI
*/
const FS_NAND_SPI_DEVICE_LIST FS_NAND_SPI_DeviceListISSI = {
  (U8)SEGGER_COUNTOF(_apDeviceISSI),
  _apDeviceISSI
};

/*********************************************************************
*
*       FS_NAND_SPI_DeviceListMacronix
*/
const FS_NAND_SPI_DEVICE_LIST FS_NAND_SPI_DeviceListMacronix = {
  (U8)SEGGER_COUNTOF(_apDeviceMacronix),
  _apDeviceMacronix
};

/*********************************************************************
*
*       FS_NAND_SPI_DeviceListMicron
*/
const FS_NAND_SPI_DEVICE_LIST FS_NAND_SPI_DeviceListMicron = {
  (U8)SEGGER_COUNTOF(_apDeviceMicron),
  _apDeviceMicron
};

/*********************************************************************
*
*       FS_NAND_SPI_DeviceListToshiba
*/
const FS_NAND_SPI_DEVICE_LIST FS_NAND_SPI_DeviceListToshiba = {
  (U8)SEGGER_COUNTOF(_apDeviceToshiba),
  _apDeviceToshiba
};

/*********************************************************************
*
*       FS_NAND_SPI_DeviceListWinbond
*/
const FS_NAND_SPI_DEVICE_LIST FS_NAND_SPI_DeviceListWinbond = {
  (U8)SEGGER_COUNTOF(_apDeviceWinbond),
  _apDeviceWinbond
};

/*********************************************************************
*
*       FS_NAND_SPI_DeviceListGigaDevice
*/
const FS_NAND_SPI_DEVICE_LIST FS_NAND_SPI_DeviceListGigaDevice = {
  (U8)SEGGER_COUNTOF(_apDeviceGigaDevice),
  _apDeviceGigaDevice
};

/*********************************************************************
*
*       FS_NAND_SPI_DeviceListAllianceMemory
*/
const FS_NAND_SPI_DEVICE_LIST FS_NAND_SPI_DeviceListAllianceMemory = {
  (U8)SEGGER_COUNTOF(_apDeviceAllianceMemory),
  _apDeviceAllianceMemory
};

/*************************** End of file ****************************/
