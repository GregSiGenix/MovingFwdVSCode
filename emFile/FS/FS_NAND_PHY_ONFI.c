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
File        : FS_NAND_PHY_ONFI.c
Purpose     : Physical Layer for the NAND driver that uses ONFI
Literature  :
  [1] Open NAND Flash Interface Specification
    (\\fileserver\techinfo\Subject\NANDFlash\ONFI\ONFI_30.pdf)
  [2] Datasheet NAND Flash Memory MT29F2G08ABAEAH4, MT29F2G08ABAEAWP, MT29F2G08ABBEAH4 MT29F2G08ABBEAHC, MT29F2G16ABAEAWP, MT29F2G16ABBEAH4, MT29F2G16ABBEAHC
    (\\fileserver\techinfo\Company\Micron\NANDFlash\SLC\MT29F2G_08ABAEA_08ABBEA_16ABAEA_16ABBEA.pdf)
  [3] S34ML04G3 Internal ECC Corrections and Status
    (\\fileserver\Techinfo\Company\SkyHigh\NANDFlash\S34ML04G3_Internal_ECC_CorrectionsAndStatus.pdf)
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include section
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

/*********************************************************************
*
*       Execution status
*/
#define STATUS_ERROR                0x01u   // 0:Pass,        1:Fail
#define STATUS_REWRITE_RECOMMENDED  0x08u   // 0:No rewrite,  1:Rewrite
#define STATUS_READY                0x40u   // 0:Busy,        1:Ready
#define STATUS_WRITE_PROTECTED      0x80u   // 0:Protect,     1:Not Protect
#define STATUS_ECC_MASK             0x18u   // ECC correction status
#define STATUS_ECC_1_3_BIT_ERRORS   0x10u   // 1-3 bit errors corrected
#define STATUS_ECC_4_6_BIT_ERRORS   0x08u   // 4-6 bit errors corrected
#define STATUS_ECC_7_8_BIT_ERRORS   0x18u   // 7-8 bit errors corrected
#define STATUS_READ_ERROR           0x10u   // Uncorrectable bit error or page needs rewrite (SkyHigh only)

/*********************************************************************
*
*       NAND commands
*/
#define CMD_READ_1                  0x00
#define CMD_RANDOM_READ_1           0x05
#define CMD_WRITE_2                 0x10
#define CMD_READ_2                  0x30
#define CMD_READ_INTERNAL           0x35
#define CMD_ERASE_1                 0x60
#define CMD_ERASE_2                 0xD0
#define CMD_READ_STATUS             0x70
#define CMD_READ_STATUS_ENHANCED    0x78
#define CMD_WRITE_1                 0x80
#define CMD_RANDOM_WRITE            0x85
#define CMD_READ_ID                 0x90
#define CMD_RANDOM_READ_2           0xE0
#define CMD_READ_PARA_PAGE          0xEC
#define CMD_GET_FEATURES            0xEE
#define CMD_SET_FEATURES            0xEF
#define CMD_RESET                   0xFF

/*********************************************************************
*
*       Device features
*/
#define NUM_FEATURE_PARA            4
#define MICRON_ECC_FEATURE_ADDR     0x90
#define MICRON_ECC_FEATURE_MASK     0x08u
#define SKYHIGH_ECC_FEATURE_ADDR    0x90
#define SKYHIGH_ECC_FEATURE_MASK    0x10u

/*********************************************************************
*
*       ONFI parameters
*/
#define PARA_PAGE_SIZE              256u
#define PARA_CRC_POLY               0x8005u
#define PARA_CRC_INIT               0x4F4Eu
#define NUM_PARA_PAGES              30        // Some MLC devices have up to 28 parameter pages.

/*********************************************************************
*
*       Manufacturer ids
*/
#define MFG_ID_SKYHIGH              0x01u
#define MFG_ID_MICRON               0x2Cu
#define MFG_ID_MACRONIX             0xC2u
#define MFG_ID_GIGADEVICE           0xC8u
#define MFG_ID_SKHYNIX              0xADu
#define MFG_ID_WINBOND              0xEFu

/*********************************************************************
*
*       Misc. defines
*/
#define PLANE_INFO_BYTE_OFF         4
#define PLANE_INFO_MASK             0x03u
#define PLANE_INFO_BIT              2
#define PLANE_INFO_2PLANES          0x01u
#define ECC_STATUS_BYTE_OFF         4
#define ECC_STATUS_BIT              7
#if FS_NAND_SUPPORT_EXT_ONFI_PARA
  #define SECTION_TYPE_ECC          2u
#endif
#define OFF_BBM_MAIN                0u    // GigaDevice specific.
#define OFF_BBM_SPARE               1u    // GigaDevice specific.

/*********************************************************************
*
*       ASSERT_PARA_IS_ALIGNED
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_PARA_IS_ALIGNED(pInst, Para)                \
    if ((pInst)->DataBusWidth == 16u) {                      \
      FS_DEBUG_ASSERT(FS_MTYPE_DRIVER, ((Para) & 1u) == 0u); \
    }
#else
  #define ASSERT_PARA_IS_ALIGNED(pInst, Para)
#endif

/*********************************************************************
*
*       ASSERT_UNIT_NO_IS_IN_RANGE
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)                                          \
    if ((Unit) >= (U8)FS_NAND_NUM_UNITS) {                                          \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NAND_PHY_ONFI: Invalid unit number.")); \
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);                                          \
    }
#else
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)
#endif

/*********************************************************************
*
*       ASSERT_HW_TYPE_IS_SET
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_HW_TYPE_IS_SET(pInst)                                           \
    if ((pInst)->pHWType == NULL) {                                              \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NAND_PHY_ONFI: HW layer not set.")); \
      FS_X_PANIC(FS_ERRCODE_HW_LAYER_NOT_SET);                                   \
    }
#else
  #define ASSERT_HW_TYPE_IS_SET(pInst)
#endif

/*********************************************************************
*
*       ASSERT_IS_ECC_ENABLED
*/
#if FS_SUPPORT_TEST
  #define ASSERT_IS_ECC_ENABLED(pInst)            \
    if (_IsECCEnabled(pInst) == 0) {              \
      FS_X_PANIC(FS_ERRCODE_INVALID_USAGE);       \
    }
#else
  #define ASSERT_IS_ECC_ENABLED(pInst)
#endif

/*********************************************************************
*
*       ASSERT_IS_ECC_DISABLED
*/
#if FS_SUPPORT_TEST
  #define ASSERT_IS_ECC_DISABLED(pInst)           \
    if (_IsECCDisabled(pInst) == 0) {             \
      FS_X_PANIC(FS_ERRCODE_INVALID_USAGE);       \
    }
#else
  #define ASSERT_IS_ECC_DISABLED(pInst)
#endif

/*********************************************************************
*
*       ASSERT_IS_ECC_CORRECTION_STATUS_ENABLED
*/
#if FS_SUPPORT_TEST
  #define ASSERT_IS_ECC_CORRECTION_STATUS_ENABLED(pInst)            \
    if (_IsECCCorrectionStatusEnabled(pInst) == 0) {                \
      FS_X_PANIC(FS_ERRCODE_INVALID_USAGE);                         \
    }
#else
  #define ASSERT_IS_ECC_CORRECTION_STATUS_ENABLED(pInst)
#endif

/*********************************************************************
*
*       ASSERT_IS_ECC_CORRECTION_STATUS_DISABLED
*/
#if FS_SUPPORT_TEST
  #define ASSERT_IS_ECC_CORRECTION_STATUS_DISABLED(pInst)           \
    if (_IsECCCorrectionStatusEnabled(pInst) != 0) {                \
      FS_X_PANIC(FS_ERRCODE_INVALID_USAGE);                         \
    }
#else
  #define ASSERT_IS_ECC_CORRECTION_STATUS_DISABLED(pInst)
#endif

/*********************************************************************
*
*       Local types
*
**********************************************************************
*/

/*********************************************************************
*
*       NAND_ONFI_INST
*
*  Notes
*    (1) ONFI uses the term unit to refer to a die inside a stacked device.
*        We prefer to use the term die in order to avoid confusion with the
*        unit number that identifies the instance of a physical layer.
*/
typedef struct {
  const FS_NAND_HW_TYPE          * pHWType;                   // HW access functions.
  const FS_NAND_ONFI_DEVICE_TYPE * pDevice;                   // Device-specific processing functions.
  const FS_NAND_ONFI_DEVICE_LIST * pDeviceList;               // List of supported devices
  U16                              BytesPerSpareArea;         // Number of bytes in the spare area.
  U8                               Unit;                      // Index of the phy. layer instance (0-based)
  U8                               DataBusWidth;              // Width of the data bus in bits (16 or 8)
  U8                               NumBytesColAddr;           // Number of bytes in a column address
  U8                               NumBytesRowAddr;           // Number of bytes in a row address
  U8                               NumBitErrorsCorrectable;   // Number of bit errors the HW ECC is able to correct
  U8                               ldNumPlanes;               // Number of memory planes in the device (as power of 2)
  U8                               ldPagesPerBlock;           // Number of pages in a block
  U8                               IsPageCopyAllowed;         // Set to 1 if the phy. layer is allowed to let the NAND flash copy pages internally.
                                                              // This is possible only when the HW ECC of the NAND flash is enabled.
  U8                               IsECCEnabledPerm;          // Set to 1 if the HW ECC cannot be disabled.
  U8                               ldNumDies;                 // Number of physical logical units on the device as a power of 2 exponent.
  U8                               ldBlocksPerDie;            // Total number of NAND blocks in one die of the device as a power of 2 exponent.
  U8                               IsRawMode;                 // Set to 1 if the data has to be accessed without any relocation.
  U8                               ldBytesPerPage;            // Number of bytes in a page (without spare area, as power of 2)
} NAND_ONFI_INST;

/*********************************************************************
*
*       NAND_ONFI_PARA
*/
typedef struct {
  U16              Features;
  U16              BytesPerSpareArea;
  U32              BytesPerPage;
  U32              PagesPerBlock;
  U32              NumBlocks;
  U8               NumAddrBytes;
  U8               NumDies;
  U8               BadBlockMarkingType;       // Specifies how the device marks a block as defective.
  FS_NAND_ECC_INFO ECCInfo;
} NAND_ONFI_PARA;

/*********************************************************************
*
*       FS_NAND_ONFI_DEVICE_TYPE
*
*  Description
*    Device-specific API functions.
*
*  Additional information
*    pfIdentify() and pfCopyPage() are optional and can be set to NULL.
*/
struct FS_NAND_ONFI_DEVICE_TYPE {
  //
  //lint -esym(9058, FS_NAND_ONFI_DEVICE_TYPE)
  //  We cannot define this structure in FS.h because all the functions take a pointer
  //  to the instance of the physical layer that is a structure which is only visible in this module.
  //
  int  (*pfIdentify)        (      NAND_ONFI_INST * pInst, const U8 * pId);
  int  (*pfReadApplyPara)   (      NAND_ONFI_INST * pInst, NAND_ONFI_PARA * pPara);
  int  (*pfWaitForEndOfRead)(const NAND_ONFI_INST * pInst);
  int  (*pfCopyPage)        (const NAND_ONFI_INST * pInst, U32 PageIndexSrc, U32 PageIndexDest);
  void (*pfGetECCResult)    (const NAND_ONFI_INST * pInst, FS_NAND_ECC_RESULT * pResult);
  int  (*pfReadFromPage)    (const NAND_ONFI_INST * pInst, U32 PageIndex,       void * pData0, unsigned Off0, unsigned NumBytes0,       void * pData1, unsigned Off1, unsigned NumBytes1);
  int  (*pfWriteToPage)     (const NAND_ONFI_INST * pInst, U32 PageIndex, const void * pData0, unsigned Off0, unsigned NumBytes0, const void * pData1, unsigned Off1, unsigned NumBytes1);
};

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static NAND_ONFI_INST * _apInst[FS_NAND_NUM_UNITS];

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _ld
*/
static U16 _ld(U32 Value) {
  U16 i;

  for (i = 0; i < 16u; i++) {
    if ((1uL << i) == Value) {
      break;
    }
  }
  return i;
}

/*********************************************************************
*
*      _IsPageCopyAllowed
*/
static int _IsPageCopyAllowed(const NAND_ONFI_INST * pInst) {
  int r;

  r = (int)pInst->IsPageCopyAllowed;
  return r;
}

/*********************************************************************
*
*      _AllowPageCopy
*/
static void _AllowPageCopy(NAND_ONFI_INST * pInst, U8 OnOff) {
  pInst->IsPageCopyAllowed = OnOff;
}

/*********************************************************************
*
*       _Init_x8
*/
static void _Init_x8(const NAND_ONFI_INST * pInst) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWType->pfInit_x8(Unit);
}

/*********************************************************************
*
*       _Init_x16
*/
static void _Init_x16(const NAND_ONFI_INST * pInst) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWType->pfInit_x16(Unit);
}

/*********************************************************************
*
*       _DisableCE
*/
static void _DisableCE(const NAND_ONFI_INST * pInst) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWType->pfDisableCE(Unit);
}

/*********************************************************************
*
*       _EnableCE
*/
static void _EnableCE(const NAND_ONFI_INST * pInst) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWType->pfEnableCE(Unit);
}

/*********************************************************************
*
*       _SetAddrMode
*/
static void _SetAddrMode(const NAND_ONFI_INST * pInst) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWType->pfSetAddrMode(Unit);
}

/*********************************************************************
*
*       _SetCmdMode
*/
static void _SetCmdMode(const NAND_ONFI_INST * pInst) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWType->pfSetCmdMode(Unit);
}

/*********************************************************************
*
*       _SetDataMode
*/
static void _SetDataMode(const NAND_ONFI_INST * pInst) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWType->pfSetDataMode(Unit);
}

/*********************************************************************
*
*       _WaitWhileBusy
*/
static int _WaitWhileBusy(const NAND_ONFI_INST * pInst, unsigned us) {
  U8  Unit;
  int r;

  Unit = pInst->Unit;
  r = pInst->pHWType->pfWaitWhileBusy(Unit, us);
  return r;
}

/*********************************************************************
*
*       _Read_x8
*/
static void _Read_x8(const NAND_ONFI_INST * pInst, void * pBuffer, unsigned NumBytes) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWType->pfRead_x8(Unit, pBuffer, NumBytes);
}

/*********************************************************************
*
*       _Write_x8
*/
static void _Write_x8(const NAND_ONFI_INST * pInst, const void * pBuffer, unsigned NumBytes) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWType->pfWrite_x8(Unit, pBuffer, NumBytes);
}

/*********************************************************************
*
*       _Read_x16
*/
static void _Read_x16(const NAND_ONFI_INST * pInst, void * pBuffer, unsigned NumBytes) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWType->pfRead_x16(Unit, pBuffer, NumBytes);
}

/*********************************************************************
*
*       _Write_x16
*/
static void _Write_x16(const NAND_ONFI_INST * pInst, const void * pBuffer, unsigned NumBytes) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWType->pfWrite_x16(Unit, pBuffer, NumBytes);
}

/*********************************************************************
*
*       _WriteCmd
*
*   Function description
*     Writes a single byte command to the NAND flash
*/
static void _WriteCmd(const NAND_ONFI_INST * pInst, U8 Cmd) {
  _SetCmdMode(pInst);
  _Write_x8(pInst, &Cmd, sizeof(Cmd));
}

/*********************************************************************
*
*       _WriteAddrRow
*
*   Function description
*     Selects the address of the page to be accessed.
*/
static void _WriteAddrRow(const NAND_ONFI_INST * pInst, U32 RowAddr, unsigned NumBytesRowAddr) {
  U8         aAddr[sizeof(RowAddr)];
  U8       * p;
  unsigned   NumBytes;

  _SetAddrMode(pInst);
  p        = aAddr;
  NumBytes = NumBytesRowAddr;
  do {
    *p++      = (U8)RowAddr;
    RowAddr >>= 8;
  } while (--NumBytes != 0u);
  _Write_x8(pInst, aAddr, NumBytesRowAddr);
}

/*********************************************************************
*
*       _WriteAddrCol
*
*  Function description
*    Selects the address of the byte to be accessed.
*/
static void _WriteAddrCol(const NAND_ONFI_INST * pInst, U32 ColAddr, unsigned NumBytesColAddr, U8 DataBusWidth) {
  U8         aAddr[sizeof(ColAddr)];
  U8       * p;
  unsigned   NumBytes;

  _SetAddrMode(pInst);
  if (DataBusWidth == 16u) {
    ColAddr >>= 1;        // Convert to a 16-bit word address
  }
  p        = aAddr;
  NumBytes = NumBytesColAddr;
  do {
    *p++      = (U8)ColAddr;
    ColAddr >>= 8;
  } while (--NumBytes != 0u);
  _Write_x8(pInst, aAddr, NumBytesColAddr);
}

/*********************************************************************
*
*       _WriteAddrColRow
*
*  Function description
*    Selects the byte and the page address to be accessed
*/
static void _WriteAddrColRow(const NAND_ONFI_INST * pInst, U32 ColAddr, U32 NumBytesColAddr, U32 RowAddr, U32 NumBytesRowAddr, U8 DataBusWidth) {
  U8         aAddr[sizeof(ColAddr) + sizeof(RowAddr)];
  U8       * p;
  unsigned   NumBytes;

  _SetAddrMode(pInst);
  if (DataBusWidth == 16u) {
    ColAddr >>= 1;        // Convert to a 16-bit word address
  }
  p        = aAddr;
  NumBytes = NumBytesColAddr;
  do {
    *p++      = (U8)ColAddr;
    ColAddr >>= 8;
  } while (--NumBytes != 0u);
  NumBytes = NumBytesRowAddr;
  do {
    *p++      = (U8)RowAddr;
    RowAddr >>= 8;
  } while (--NumBytes != 0u);
  _Write_x8(pInst, aAddr, NumBytesColAddr + NumBytesRowAddr);
}

/*********************************************************************
*
*       _WriteAddrByte
*
*  Function description
*    Writes the byte address of the parameter to read from.
*/
static void _WriteAddrByte(const NAND_ONFI_INST * pInst, U8 ByteAddr) {
  _SetAddrMode(pInst);
  _Write_x8(pInst, &ByteAddr, sizeof(ByteAddr));
}

/*********************************************************************
*
*       _ReadData
*
*  Function description
*    Transfers data from device to host CPU.
*/
static void _ReadData(const NAND_ONFI_INST * pInst, void * pData, unsigned NumBytes, U8 DataBusWidth) {
  _SetDataMode(pInst);
  if (DataBusWidth == 16u) {
    _Read_x16(pInst, pData, NumBytes);
  } else {
    _Read_x8(pInst, pData, NumBytes);
  }
}

/*********************************************************************
*
*       _WriteData
*
*  Function description
*    Transfers data from host CPU to device.
*/
static void _WriteData(const NAND_ONFI_INST * pInst, const void * pData, unsigned NumBytes, U8 DataBusWidth) {
  _SetDataMode(pInst);
  if (DataBusWidth == 16u) {
    _Write_x16(pInst, pData, NumBytes);
  } else {
    _Write_x8(pInst, pData, NumBytes);
  }
}

/*********************************************************************
*
*       _ReadId
*
*  Function description
*    Reads the id string from NAND flash device.
*
*  Parameters
*    pInst      Driver instance.
*    pData      [OUT] Id information read from NAND flash device.
*    NumBytes   Number of id bytes to read.
*
*  Notes
*    (1) According to [2] a target command can be executed only if
*        the R/B signal is high.
*/
static void _ReadId(const NAND_ONFI_INST * pInst, U8 * pData, unsigned NumBytes) {
  _EnableCE(pInst);
  (void)_WaitWhileBusy(pInst, 0);       // Note 1
  _WriteCmd(pInst, CMD_READ_ID);
  _WriteAddrByte(pInst, 0);
  _ReadData(pInst, pData, NumBytes, 8);
  _DisableCE(pInst);
}

/*********************************************************************
*
*       _ReadStatus
*
*  Function description
*    Reads and returns the contents of the status register.
*/
static U8 _ReadStatus(const NAND_ONFI_INST * pInst) {
  U8 Status;

  _WriteCmd(pInst, CMD_READ_STATUS);
  _ReadData(pInst, &Status, sizeof(Status), 8);
  return Status;
}

/*********************************************************************
*
*       _ReadStatusEnhanced
*
*  Function description
*    Reads and returns the contents of the status register.
*
*  Notes
*    (1) According to [2] a target command can be executed only if
*        the R/B signal is high.
*/
static U8 _ReadStatusEnhanced(const NAND_ONFI_INST * pInst, U32 BlockIndex) {
  U8       Status;
  unsigned NumBytesRowAddr;

  _EnableCE(pInst);
  (void)_WaitWhileBusy(pInst, 0);       // Note 1
  NumBytesRowAddr = pInst->NumBytesRowAddr;
  _WriteCmd(pInst, CMD_READ_STATUS_ENHANCED);
  _WriteAddrRow(pInst, BlockIndex, NumBytesRowAddr);
  _ReadData(pInst, &Status, sizeof(Status), 8);
  _DisableCE(pInst);
  return Status;
}

/*********************************************************************
*
*       _WaitForEndOfOperation
*
*  Function description
*    Waits for the NAND to complete its last operation.
*
*  Parameters
*    pInst    Phy. layer instance.
*
*  Return value
*    ==0    Success.
*    !=0    An error has occurred.
*/
static int _WaitForEndOfOperation(const NAND_ONFI_INST * pInst) {
  U8 Status;

  //
  // Try to use the hardware pin to find out when busy is cleared.
  //
  (void)_WaitWhileBusy(pInst, 0);
  //
  // Wait until the NAND flash is ready for the next operation.
  //
  do {
    Status = _ReadStatus(pInst);
  } while ((Status & STATUS_READY) == 0u);
  if ((Status & STATUS_ERROR) != 0u) {
    return 1;
  }
  return 0;
}

/*********************************************************************
*
*       _IsSamePlane
*
*  Function description
*    Verifies if the specified pages are located on the same plane.
*
*  Parameters
*    pInst        Phy. layer instance.
*    PageIndex1   Index of the first page to be checked.
*    PageIndex2   Index of the second page to be checked.
*
*  Return value
*    ==1    The pages are on the same plane.
*    ==0    The pages are not on the same plane.
*/
static int _IsSamePlane(const NAND_ONFI_INST * pInst, U32 PageIndex1, U32 PageIndex2) {
  U32      Mask;
  unsigned ldBlocksPerDie;
  unsigned ldPagesPerBlock;
  unsigned ldNumPlanes;
  unsigned ldNumDies;

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
*       _IsFirstPage
*
*  Function description
*    Verifies if the specified page is the first in a block.
*
*  Parameters
*    pInst        Phy. layer instance.
*    PageIndex    Index of the page to be checked.
*
*  Return value
*    ==1    This is the first page.
*    ==0    This is not the first page.
*/
static int _IsFirstPage(const NAND_ONFI_INST * pInst, U32 PageIndex) {
  U32      Mask;
  unsigned ldPagesPerBlock;

  ldPagesPerBlock = pInst->ldPagesPerBlock;
  Mask = (1uL << ldPagesPerBlock) - 1u;
  if ((PageIndex & Mask) == 0u) {
    return 1;
  }
  return 0;
}

/*********************************************************************
*
*       _IsLastPage
*
*  Function description
*    Verifies if the specified page is the last in a block.
*
*  Parameters
*    pInst        Phy. layer instance.
*    PageIndex    Index of the page to be checked.
*
*  Return value
*    ==1    This is the last page.
*    ==0    This is not the last page.
*/
static int _IsLastPage(const NAND_ONFI_INST * pInst, U32 PageIndex) {
  U32      Mask;
  unsigned ldPagesPerBlock;

  ldPagesPerBlock = pInst->ldPagesPerBlock;
  Mask = (1uL << ldPagesPerBlock) - 1u;
  if ((PageIndex & Mask) == Mask) {
    return 1;
  }
  return 0;
}

/*********************************************************************
*
*       _IsFirstBlock
*
*  Function description
*    Verifies if the specified page is located in the first NAND block.
*
*  Parameters
*    pInst        Phy. layer instance.
*    PageIndex    Index of the page to be checked.
*
*  Return value
*    ==1    The page is located in the first block.
*    ==0    The page is not located in the first block.
*/
static int _IsFirstBlock(const NAND_ONFI_INST * pInst, U32 PageIndex) {
  U32      Mask;
  unsigned ldPagesPerBlock;

  ldPagesPerBlock = pInst->ldPagesPerBlock;
  Mask = ~((1uL << ldPagesPerBlock) - 1u);
  if ((PageIndex & Mask) == 0u) {
    return 1;
  }
  return 0;
}

/*********************************************************************
*
*       _Reset
*
*  Function description
*    Resets the NAND flash by command
*/
static void _Reset(const NAND_ONFI_INST * pInst) {
  U8 Status;

  _EnableCE(pInst);
  _WriteCmd(pInst, CMD_RESET);
  (void)_WaitWhileBusy(pInst, 0);
  do {
    Status = _ReadStatus(pInst);
  } while ((Status & STATUS_READY) == 0u);
  _DisableCE(pInst);
}

/*********************************************************************
*
*       _GetFeatures
*
*  Function description
*    Reads the device settings.
*
*  Parameters
*    pInst    Phy. layer instance.
*    Addr     Feature address (see device datasheet).
*    pData    [OUT] Read device settings. Must be at least 4 bytes long.
*
*  Return value
*    ==0      Settings read
*    !=0      An error occurred
*
*  Notes
*    (1) According to [2] a target command can be executed only if
*        the R/B signal is high.
*/
static int _GetFeatures(const NAND_ONFI_INST * pInst, U8 Addr, U8 * pData) {
  int r;

  _EnableCE(pInst);
  (void)_WaitWhileBusy(pInst, 0);       // Note 1
  _WriteCmd(pInst, CMD_GET_FEATURES);
  _WriteAddrByte(pInst, Addr);
  r = _WaitForEndOfOperation(pInst);
  if (r == 0) {
    _WriteCmd(pInst, CMD_READ_1);       // Revert to read mode. _WaitForEndOfOperation() change it to status mode.
    _ReadData(pInst, pData, NUM_FEATURE_PARA, 8);
  }
  _DisableCE(pInst);
  if (r != 0) {
    _Reset(pInst);
  }
  return r;
}

/*********************************************************************
*
*       _SetFeatures
*
*  Function description
*    Modifies the device settings.
*
*  Parameters
*    pInst    Phy. layer instance.
*    Addr     Feature address (see device datasheet).
*    pData    [IN]  New device settings.  Must be at least 4 bytes long.
*
*  Return value
*    ==0      Settings written.
*    !=0      An error occurred.
*
*  Notes
*    (1) According to [2] a target command can be executed only if
*        the R/B signal is high.
*/
static int _SetFeatures(const NAND_ONFI_INST * pInst, U8 Addr, const U8 * pData) {
  int r;

  _EnableCE(pInst);
  (void)_WaitWhileBusy(pInst, 0);       // Note 1
  _WriteCmd(pInst, CMD_SET_FEATURES);
  _WriteAddrByte(pInst, Addr);
  _WriteData(pInst, pData, NUM_FEATURE_PARA, 8);
  r = _WaitForEndOfOperation(pInst);
  _DisableCE(pInst);
  if (r != 0) {
    _Reset(pInst);
  }
  return r;
}

/*********************************************************************
*
*       _ReadONFIParaPage
*
*  Function description
*    Reads information from the ONFI main parameter page of the NAND flash device.
*
*  Parameters
*    pInst      Phy. layer instance.
*    pONFIPara  [OUT] Information read from the parameter page.
*
*  Return value
*    > 0      OK, extended ECC information present.
*    ==0      OK, information read.
*    < 0      An error occurred.
*/
static int _ReadONFIParaPage(const NAND_ONFI_INST * pInst, NAND_ONFI_PARA * pONFIPara) {
  int r;
  U16 crcRead;
  U16 crcCalc;
  U32 NumLoops;
  int iParaPage;
  int iByte;
  U8  acBuffer[4];
  U8  IsValid;
  U8  NumBitsCorrectable;
  U8  ldBytesPerBlock;
  U8  Data8;
  U8  HasExtECCInfo;

  r             = -1;                 // No parameter page found, yet.
  crcCalc       = 0;
  HasExtECCInfo = 0;
  //
  // Multiple identical parameter pages are stored in a device.
  // We read the information from the first valid parameter page.
  //
  for (iParaPage = 0; iParaPage < NUM_PARA_PAGES; ++iParaPage) {
    iByte    = 0;
    IsValid  = 0;
    NumLoops = (PARA_PAGE_SIZE - sizeof(crcRead)) / sizeof(acBuffer);
    do {
      _ReadData(pInst, acBuffer, sizeof(acBuffer), 8);
      if (iByte == 0) {
        //
        // Check the signature.
        //
        if ((acBuffer[0] == (U8)'O') &&
            (acBuffer[1] == (U8)'N') &&
            (acBuffer[2] == (U8)'F') &&
            (acBuffer[3] == (U8)'I')) {
          IsValid = 1;                // Valid parameter page.
        }
      } else if (iByte == 4) {
        pONFIPara->Features          = FS_LoadU16LE(&acBuffer[2]);
      } else if (iByte == 80) {
        pONFIPara->BytesPerPage      = FS_LoadU32LE(&acBuffer[0]);
      } else if (iByte == 84) {
        pONFIPara->BytesPerSpareArea = FS_LoadU16LE(&acBuffer[0]);
      } else if (iByte == 92) {
        pONFIPara->PagesPerBlock     = FS_LoadU32LE(&acBuffer[0]);
      } else if (iByte == 96) {
        pONFIPara->NumBlocks         = FS_LoadU32LE(&acBuffer[0]);
      } else if (iByte == 100) {
        pONFIPara->NumDies           = acBuffer[0];
        pONFIPara->NumAddrBytes      = acBuffer[1];
      } else if (iByte == 112) {
        //
        // Information about ECC.
        //
        NumBitsCorrectable = 0;
        ldBytesPerBlock    = 9;     // 512 bytes
        Data8 = acBuffer[0];
        if (Data8 != 0xFFu) {       // Is information valid?
          NumBitsCorrectable = Data8;
        } else {
          //
          // Read information about ECC from the Extended ECC Information area.
          //
          HasExtECCInfo = 1;
        }
        pONFIPara->ECCInfo.NumBitsCorrectable = NumBitsCorrectable;
        pONFIPara->ECCInfo.ldBytesPerBlock    = ldBytesPerBlock;
      } else {
        //
        // These values are not interesting for the physical layer.
        //
      }
      //
      // Accumulate the CRC of parameter values.
      //
      if (iByte == 0) {
        crcCalc = PARA_CRC_INIT;
      }
      crcCalc = FS_CRC16_CalcBitByBit(acBuffer, sizeof(acBuffer), crcCalc, PARA_CRC_POLY);
      iByte += (int)sizeof(acBuffer);
    } while (--NumLoops != 0u);
    //
    // Read the last 2 bytes and the CRC.
    //
    _ReadData(pInst, acBuffer, sizeof(acBuffer), 8);
    if (IsValid != 0u) {            // Signature OK?
      //
      // Verify the CRC.
      //
      crcCalc = FS_CRC16_CalcBitByBit(&acBuffer[0], 2, crcCalc, PARA_CRC_POLY);
      crcRead = FS_LoadU16LE(&acBuffer[2]);
      if (crcCalc == crcRead) {
        r = 0;
        break;
      }
    }
  }
  if (r == 0) {
    if (HasExtECCInfo != 0u) {
      r = 1;                // Extended ECC Information is present.
    }
  }
  return r;
}

#if FS_NAND_SUPPORT_EXT_ONFI_PARA

/*********************************************************************
*
*       _ReadExtONFIParaPage
*
*  Function description
*    Reads information from the ONFI extended parameter page of the NAND flash device.
*
*  Parameters
*    pInst      Phy. layer instance.
*    pONFIPara  [OUT] Information read from the parameter page.
*
*  Return value
*    ==0      OK, information read.
*    < 0      An error occurred.
*/
static int _ReadExtONFIParaPage(const NAND_ONFI_INST * pInst, NAND_ONFI_PARA * pONFIPara) {
  int      r;
  U16      crcRead;
  U16      crcCalc;
  U32      NumLoops;
  int      iParaPage;
  int      iByte;
  U8       acBuffer[4];
  U8       IsValid;
  U8       FoundExtECCInfo;
  unsigned Off;
  unsigned NumBytes;
  U8       SectionType;
  U8       SectionSize;

  r               = -1;         // No parameter page found, yet.
  FoundExtECCInfo = 0;
  //
  // Skip other redundant parameter page definitions.
  //
  for (iParaPage = 0; iParaPage < NUM_PARA_PAGES; ++iParaPage) {
    iByte    = 0;
    IsValid  = 1;
    NumLoops = PARA_PAGE_SIZE / sizeof(acBuffer);
    do {
      _ReadData(pInst, acBuffer, sizeof(acBuffer), 8);
      if (iByte == 0) {
        //
        // Check the signature. The cast is needed to silence PC Lint.
        //
        IsValid = 0;
        if ((acBuffer[0] == (U8)'O') &&
            (acBuffer[1] == (U8)'N') &&
            (acBuffer[2] == (U8)'F') &&
            (acBuffer[3] == (U8)'I')) {
          IsValid = 1;
        }
        if (IsValid == 0u) {
          break;                // The parameter page is not valid. Quit searching.
        }
      }
      iByte += (int)sizeof(acBuffer);
    } while (--NumLoops != 0u);
    if (IsValid == 0u) {
      break;                    // The parameter page is not valid. Quit searching.
    }
  }
  //
  // Several identical parameter pages are stored in a device.
  // Read the information from the first valid parameter page.
  //
  for (iParaPage = 0; iParaPage < NUM_PARA_PAGES; ++iParaPage) {
    //
    // The extended parameter page starts with 2 CRC bytes.
    //
    crcCalc = PARA_CRC_INIT;
    crcRead = FS_LoadU16LE(&acBuffer[0]);
    //
    // The next 4 bytes are the signature. The first 2 bytes of the signature were already read so check them here.
    //
    if ((acBuffer[2] == (U8)'E') &&
        (acBuffer[3] == (U8)'P')) {
      crcCalc = FS_CRC16_CalcBitByBit(&acBuffer[2], 2, crcCalc, PARA_CRC_POLY);
      //
      // Read the next 2 bytes of the signature and check them.
      //
      _ReadData(pInst, acBuffer, 2, 8);
      if ((acBuffer[0] == (U8)'P') &&
          (acBuffer[1] == (U8)'S')) {
        crcCalc = FS_CRC16_CalcBitByBit(&acBuffer[0], 2, crcCalc, PARA_CRC_POLY);
        //
        // OK, the signature matches. Skip the next 10 reserved bytes.
        //
        NumLoops = 10u / 2u;            // Read 2 bytes at a time in the loop.
        do {
          _ReadData(pInst, acBuffer, 2, 8);
          crcCalc = FS_CRC16_CalcBitByBit(&acBuffer[0], 2, crcCalc, PARA_CRC_POLY);
        } while (--NumLoops != 0u);
        //
        // Search for the Extended ECC Information section.
        //
        Off      = 0;                   // Byte offset from the end of section list.
        NumBytes = 0;                   // Number of information bytes.
        NumLoops = 8;                   // A maximum of 8 sections are defined.
        do {
          _ReadData(pInst, acBuffer, 2, 8);
          crcCalc = FS_CRC16_CalcBitByBit(&acBuffer[0], 2, crcCalc, PARA_CRC_POLY);
          SectionType   = acBuffer[0];
          SectionSize   = acBuffer[1];
          SectionSize <<= 4;            // The size of a section is specified in multiples of 16 bytes.
          if (FoundExtECCInfo == 0u) {
            if (SectionType == SECTION_TYPE_ECC) {
              FoundExtECCInfo = 1;
            }
          }
          if (FoundExtECCInfo == 0u) {
            Off += SectionSize;
          }
          NumBytes += SectionSize;
        } while (--NumLoops != 0u);
        if (FoundExtECCInfo != 0u) {
          //
          // Skip non-ECC sections.
          //
          NumLoops = Off / sizeof(acBuffer);
          if (NumLoops != 0u) {
            do {
              _ReadData(pInst, acBuffer, sizeof(acBuffer), 8);
              crcCalc = FS_CRC16_CalcBitByBit(acBuffer, sizeof(acBuffer), crcCalc, PARA_CRC_POLY);
              NumBytes -= sizeof(acBuffer);
            } while (--NumLoops != 0u);
          }
          //
          // Read the bit error correction capability and the size of ECC block.
          //
          _ReadData(pInst, acBuffer, sizeof(acBuffer), 8);
          crcCalc = FS_CRC16_CalcBitByBit(acBuffer, sizeof(acBuffer), crcCalc, PARA_CRC_POLY);
          pONFIPara->ECCInfo.NumBitsCorrectable = acBuffer[0];
          pONFIPara->ECCInfo.ldBytesPerBlock    = acBuffer[1];
          NumBytes -= sizeof(acBuffer);
          //
          // Calculate the CRC for the remaining bytes.
          //
          NumLoops = NumBytes / sizeof(acBuffer);
          if (NumLoops != 0u) {
            do {
              _ReadData(pInst, acBuffer, sizeof(acBuffer), 8);
              crcCalc = FS_CRC16_CalcBitByBit(acBuffer, sizeof(acBuffer), crcCalc, PARA_CRC_POLY);
            } while (--NumLoops != 0u);
          }
          //
          // Verify the CRC.
          //
          if (crcCalc == crcRead) {
            r = 0;
            break;                          // OK, the information is valid.
          }
        }
      }
    }
  }
  return r;
}

#endif // FS_NAND_SUPPORT_EXT_ONFI_PARA

/*********************************************************************
*
*       _ReadONFIPara
*
*  Function description
*    Reads the ONFI parameter page.
*    A page has 256 bytes. The integrity of information is checked using CRC.
*
*  Parameters
*    pInst      Phy. layer instance.
*    pONFIPara  [OUT] Information read from the parameter page.
*
*  Return value
*    ==0      ONFI parameters read.
*    !=0      An error occurred.
*
*  Notes
*    (1) According to [2] a target command can be executed only if
*        the R/B signal is high.
*/
static int _ReadONFIPara(const NAND_ONFI_INST * pInst, NAND_ONFI_PARA * pONFIPara) {
  int r;

  _EnableCE(pInst);
  (void)_WaitWhileBusy(pInst, 0);       // Note 1
  _WriteCmd(pInst, CMD_READ_PARA_PAGE);
  _WriteAddrByte(pInst, 0);
  r = _WaitForEndOfOperation(pInst);
  if (r == 0) {
    _WriteCmd(pInst, CMD_READ_1);       // Switch back to read mode. _WaitForEndOfOperation() function changed it to status mode.
    //
    // Read information from the parameter pages of NAND flash.
    //
    r = _ReadONFIParaPage(pInst, pONFIPara);
#if FS_NAND_SUPPORT_EXT_ONFI_PARA
    if (r > 0) {
      r = _ReadExtONFIParaPage(pInst, pONFIPara);
    }
#endif // FS_NAND_SUPPORT_EXT_ONFI_PARA
  }
  _DisableCE(pInst);
  return r;
}

/*********************************************************************
*
*       _EnableECC
*
*  Function description
*    Activates the internal ECC engine of NAND flash.
*
*  Notes
*     (1) A read-modify-write operation is required since more than one feature is stored in a parameter.
*/
static int _EnableECC(const NAND_ONFI_INST * pInst) {
  U8  abPara[4];
  int r;

  FS_MEMSET(abPara, 0, sizeof(abPara));
  r = _GetFeatures(pInst, MICRON_ECC_FEATURE_ADDR, abPara);    // Note 1
  if (r == 0) {
    if ((abPara[0] & MICRON_ECC_FEATURE_MASK) == 0u) {
      abPara[0] |= MICRON_ECC_FEATURE_MASK;
      r = _SetFeatures(pInst, MICRON_ECC_FEATURE_ADDR, abPara);
    }
  }
  return r;
}

/*********************************************************************
*
*       _DisableECC
*
*  Function description
*    Deactivates the internal ECC engine of NAND flash.
*
*  Notes
*     (1) A read-modify-write operation is required since more than one feature is stored in a parameter.
*/
static int _DisableECC(const NAND_ONFI_INST * pInst) {
  U8       abPara[4];
  int      r;
  unsigned Para;

  FS_MEMSET(abPara, 0, sizeof(abPara));
  r = _GetFeatures(pInst, MICRON_ECC_FEATURE_ADDR, abPara);    // Note 1
  if (r == 0) {
    Para = abPara[0];
    if ((Para & MICRON_ECC_FEATURE_MASK) != 0u) {
      Para &= ~MICRON_ECC_FEATURE_MASK;
      abPara[0] = (U8)Para;
      r = _SetFeatures(pInst, MICRON_ECC_FEATURE_ADDR, abPara);
    }
  }
  return r;
}

/*********************************************************************
*
*       _IsECCEnabledPerm
*/
static U8 _IsECCEnabledPerm(const NAND_ONFI_INST * pInst) {
  return pInst->IsECCEnabledPerm;
}

#if FS_SUPPORT_TEST

/*********************************************************************
*
*       _IsECCEnabled
*/
static int _IsECCEnabled(const NAND_ONFI_INST * pInst) {
  U8       abId[5];
  int      r;
  unsigned MfgId;

  r = 1;
  if (pInst->IsECCEnabledPerm == 0u) {
    FS_MEMSET(abId, 0, sizeof(abId));
    _ReadId(pInst, abId, sizeof(abId));
    MfgId = abId[0];
    if (   (MfgId == MFG_ID_MICRON)
        || (MfgId == MFG_ID_MACRONIX)) {
      if ((abId[ECC_STATUS_BYTE_OFF] & (1u << ECC_STATUS_BIT)) == 0u) {
        r = 0;
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _IsECCDisabled
*/
static int _IsECCDisabled(const NAND_ONFI_INST * pInst) {
  U8       abId[5];
  int      r;
  unsigned MfgId;

  r = 1;
  if (pInst->IsECCEnabledPerm != 0u) {
    r = 0;
  } else {
    FS_MEMSET(abId, 0, sizeof(abId));
    _ReadId(pInst, abId, sizeof(abId));
    MfgId = abId[0];
    if (   (MfgId == MFG_ID_MICRON)
        || (MfgId == MFG_ID_MACRONIX)) {
      if ((abId[ECC_STATUS_BYTE_OFF] & (1u << ECC_STATUS_BIT)) != 0u) {
        r = 0;
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _IsECCCorrectionStatusEnabled
*/
static int _IsECCCorrectionStatusEnabled(const NAND_ONFI_INST * pInst) {
  U8  abPara[4];
  int r;
  int Result;

  r = 0;
  FS_MEMSET(abPara, 0, sizeof(abPara));
  Result = _GetFeatures(pInst, SKYHIGH_ECC_FEATURE_ADDR, abPara);
  if (Result == 0) {
    if ((abPara[0] & SKYHIGH_ECC_FEATURE_MASK) != 0u) {
      r = 1;
    }
  }
  return r;
}

#endif // FS_SUPPORT_TEST

/*********************************************************************
*
*      _AllocInstIfRequired
*
*  Function description
*    Allocates memory for the instance of a physical layer.
*/
static NAND_ONFI_INST * _AllocInstIfRequired(U8 Unit) {
  NAND_ONFI_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  pInst = NULL;                     // Set to indicate an error.
  if (Unit < (U8)FS_NAND_NUM_UNITS) {
    pInst = _apInst[Unit];
    if (pInst == NULL) {
      pInst = SEGGER_PTR2PTR(NAND_ONFI_INST, FS_ALLOC_ZEROED((I32)sizeof(NAND_ONFI_INST), "NAND_ONFI_INST"));
      if (pInst != NULL) {
        pInst->Unit        = Unit;
        pInst->pDeviceList = FS_NAND_ONFI_DEVICE_LIST_DEFAULT;
        _apInst[Unit]      = pInst;
      }
    }
  }
  return pInst;
}

/*********************************************************************
*
*       _EnableECCCorrectionStatus
*
*  Function description
*    Activates the ECC status that indicates an uncorrectable bit error.
*
*  Parameters
*    pInst    Physical layer instance.
*
*  Return value
*    ==0    OK, ECC status enabled.
*    !=0    An error occurred.
*
*  Additional information
*    This function can be used only for SkyHigh NAND flash devices
*    with HW ECC.
*
*  Notes
*     (1) A read-modify-write operation is required because more than one
*         feature is stored in a parameter.
*/
static int _EnableECCCorrectionStatus(const NAND_ONFI_INST * pInst) {
  U8  abPara[4];
  int r;

  FS_MEMSET(abPara, 0, sizeof(abPara));
  r = _GetFeatures(pInst, SKYHIGH_ECC_FEATURE_ADDR, abPara);    // Note 1
  if (r == 0) {
    if ((abPara[0] & SKYHIGH_ECC_FEATURE_MASK) == 0u) {
      abPara[0] |= SKYHIGH_ECC_FEATURE_MASK;
      r = _SetFeatures(pInst, SKYHIGH_ECC_FEATURE_ADDR, abPara);
      if (r == 0) {
        ASSERT_IS_ECC_CORRECTION_STATUS_ENABLED(pInst);         // Verify that the feature was modified.
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _DisableECCCorrectionStatus
*
*  Function description
*    Activates the ECC status that recommends a rewrite operation.
*
*  Parameters
*    pInst    Physical layer instance.
*
*  Return value
*    ==0    OK, ECC status disabled.
*    !=0    An error occurred.
*
*  Additional information
*    This function can be used only for SkyHigh NAND flash devices
*    with HW ECC.
*
*  Notes
*     (1) A read-modify-write operation is required because more than one
*         feature is stored in a parameter.
*/
static int _DisableECCCorrectionStatus(const NAND_ONFI_INST * pInst) {
  U8       abPara[4];
  int      r;
  unsigned Para;

  FS_MEMSET(abPara, 0, sizeof(abPara));
  r = _GetFeatures(pInst, SKYHIGH_ECC_FEATURE_ADDR, abPara);    // Note 1
  if (r == 0) {
    Para = abPara[0];
    if ((Para & SKYHIGH_ECC_FEATURE_MASK) != 0u) {
      Para &= ~SKYHIGH_ECC_FEATURE_MASK;
      abPara[0] = (U8)Para;
      r = _SetFeatures(pInst, SKYHIGH_ECC_FEATURE_ADDR, abPara);
      if (r == 0) {
        ASSERT_IS_ECC_CORRECTION_STATUS_DISABLED(pInst);        // Verify that the feature was modified.
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _GetInst
*
*  Function description
*    Returns a driver instance by unit number.
*/
static NAND_ONFI_INST * _GetInst(U8 Unit) {
  NAND_ONFI_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  pInst = NULL;
  if (Unit < (U8)FS_NAND_NUM_UNITS) {
    pInst = _apInst[Unit];
  }
  return pInst;
}

/*********************************************************************
*
*      _GetECCInfo
*
*  Function description
*    Returns information about the HW ECC if present.
*
*  Parameters
*    pInst                      Physical layer instance.
*    pIsECCPermanentlyEnabled   [OUT] Information regarding the ability to enable / disable the HW ECC.
*                               * 0   The HW ECC can be enabled or disabled.
*                               * 1   The HW ECC is permanently enabled.
*
*  Return value
*    ==0      The NAND flash device does not have a HW ECC.
*    !=0      The NAND flash device had HW ECC.
*
*  Additional information
*    This function works only for NAND flash devices from Micron and Macronix.
*    pId is also used as a buffer
*/
static int _GetECCInfo(const NAND_ONFI_INST * pInst, int * pIsECCPermanentlyEnabled) {
  int      IsECCPresent;
  int      IsECCEnabledPerm;
  U8       abId[ECC_STATUS_BYTE_OFF + 1];
  unsigned eccStat;

  IsECCPresent     = 0;
  IsECCEnabledPerm = 0;
  FS_MEMSET(abId, 0, sizeof(abId));
  _ReadId(pInst, abId, sizeof(abId));
  //
  // Check if the device supports HW ECC and if the HW ECC can be
  // disabled or enabled. The actual status of the HW ECC is stored
  // in the byte 4 of the id string. For example, on the Micron MT29F1G08ABAFA
  // NAND flash device the HW ECC cannot be disabled. The HW ECC is on this device
  // always enabled.
  //
  eccStat = abId[ECC_STATUS_BYTE_OFF];
  if ((eccStat & (1uL << ECC_STATUS_BIT)) != 0u) {
    IsECCPresent = 1;
    //
    // The HW ECC is enabled. Try to disable it to check if the device supports this feature.
    //
    (void)_DisableECC(pInst);
    FS_MEMSET(abId, 0, sizeof(abId));
    _ReadId(pInst, abId, sizeof(abId));
    eccStat = abId[ECC_STATUS_BYTE_OFF];
    if ((eccStat & (1uL << ECC_STATUS_BIT)) != 0u) {
      IsECCEnabledPerm = 1;
    } else {
      (void)_EnableECC(pInst);          // Restore the ECC status.
    }
  } else {
    //
    // The HW ECC is disabled. Try to enable it to check if the device supports this feature.
    //
    (void)_EnableECC(pInst);
    _ReadId(pInst, abId, sizeof(abId));
    eccStat = abId[ECC_STATUS_BYTE_OFF];
    if ((eccStat & (1uL << ECC_STATUS_BIT)) != 0u) {
      IsECCPresent = 1;
      (void)_DisableECC(pInst);         // Restore the ECC status.
    }
  }
  if (pIsECCPermanentlyEnabled != NULL) {
    *pIsECCPermanentlyEnabled = IsECCEnabledPerm;
  }
  return IsECCPresent;
}

/*********************************************************************
*
*      _ReadApplyPara
*
*  Function description
*    Reads the ONFI parameters from NAND flash device and stores
*    the required information to physical layer instance.
*
*  Parameters
*    pInst      Instance of the physical layer.
*    pPara      [OUT] Read ONFI parameters.
*
*  Return value
*    ==0    OK, operation succeeded.
*    !=0    An error occurred.
*/
static int _ReadApplyPara(NAND_ONFI_INST * pInst, NAND_ONFI_PARA * pPara) {
  int      r;
  unsigned Features;
  unsigned PagesPerBlock;
  unsigned ldPagesPerBlock;
  unsigned NumAddrBytes;
  unsigned DataBusWidth;
  unsigned ldBlocksPerDie;
  unsigned ldNumDies;
  unsigned ldBytesPerPage;

  //
  // Read the ONFI parameters from NAND flash device.
  //
  r = _ReadONFIPara(pInst, pPara);
  if (r == 0) {
    //
    // Load local variables.
    //
    Features        = pPara->Features;
    PagesPerBlock   = pPara->PagesPerBlock;
    ldPagesPerBlock = _ld(PagesPerBlock);
    NumAddrBytes    = pPara->NumAddrBytes;
    ldBlocksPerDie  = _ld(pPara->NumBlocks);
    ldNumDies       = _ld(pPara->NumDies);
    ldBytesPerPage  = _ld(pPara->BytesPerPage);
    //
    // Determine the width of the data bus.
    //
    DataBusWidth = 8;
    if ((Features & 1u) != 0u) {
      DataBusWidth = 16;
    }
    //
    // Set the default bad block marking type.
    //
    pPara->BadBlockMarkingType = FS_NAND_BAD_BLOCK_MARKING_TYPE_FPS;
    //
    // Fill in the information required by the physical layer.
    //
    pInst->NumBytesColAddr         = (U8)(NumAddrBytes >> 4);
    pInst->NumBytesRowAddr         = (U8)(NumAddrBytes & 0x0Fu);
    pInst->NumBitErrorsCorrectable = pPara->ECCInfo.NumBitsCorrectable;
    pInst->ldPagesPerBlock         = (U8)ldPagesPerBlock;
    pInst->DataBusWidth            = (U8)DataBusWidth;
    pInst->ldNumPlanes             = 0;     // Typically, a NAND flash device has only 1 plane.
    pInst->IsECCEnabledPerm        = 0;     // Typically, the HW ECC can be enabled and disabled.
    pInst->ldNumDies               = (U8)ldNumDies;
    pInst->ldBlocksPerDie          = (U8)ldBlocksPerDie;
    pInst->ldBytesPerPage          = (U8)ldBytesPerPage;
    pInst->BytesPerSpareArea       = pPara->BytesPerSpareArea;
  }
  return r;
}

/*********************************************************************
*
*       _CopyPage
*
*  Function description
*    Copies the contents of a page without reading the data to host
*    and then writing it back.
*
*  Parameters
*    pInst          Physical layer instance.
*    PageIndexSrc   Index of the page to read from.
*    PageIndexDest  Index of the page to write to.
*
*  Return value
*    ==0      OK, page contents copied.
*    !=0      An error occurred or feature not supported.
*
*  Notes
*    (1) We do not reset the NAND flash device in case of read error
*        in order to preserve the contents of the status register.
*        The contents of the status register is read by the Universal
*        NAND driver via _PHY_GetECCResult() to check the number of
*        bit errors.
*/
static int _CopyPage(const NAND_ONFI_INST * pInst, U32 PageIndexSrc, U32 PageIndexDest) {
  int r;
  U8  DataBusWidth;
  U8  NumBytesColAddr;
  U8  NumBytesRowAddr;
  int IsReadError;

  r           = 1;                    // Set to indicate an error.
  IsReadError = 0;
  if (_IsPageCopyAllowed(pInst) != 0) {
    if (_IsSamePlane(pInst, PageIndexSrc, PageIndexDest) != 0) {
      DataBusWidth    = pInst->DataBusWidth;
      NumBytesColAddr = pInst->NumBytesColAddr;
      NumBytesRowAddr = pInst->NumBytesRowAddr;
      _EnableCE(pInst);
      //
      // Select the start address to read from.
      //
      _WriteCmd(pInst, CMD_READ_1);
      _WriteAddrColRow(pInst, 0, NumBytesColAddr, PageIndexSrc, NumBytesRowAddr, DataBusWidth);
      //
      // Start the execution of read command and wait for it to finish.
      //
      _WriteCmd(pInst, CMD_READ_INTERNAL);
      r = _WaitForEndOfOperation(pInst);
      if (r == 0) {
        //
        // The read data is now stored in the data register of device. Write it to the other page.
        //
        _WriteCmd(pInst, CMD_RANDOM_WRITE);
        _WriteAddrColRow(pInst, 0, NumBytesColAddr, PageIndexDest, NumBytesRowAddr, DataBusWidth);
        //
        // Execute the write command and wait for it to finish.
        //
        _WriteCmd(pInst, CMD_WRITE_2);
        r = _WaitForEndOfOperation(pInst);
      } else {
        IsReadError = 1;
      }
      _DisableCE(pInst);
      if (r != 0) {
        if (IsReadError == 0) {
          _Reset(pInst);                // Note 1
        }
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _GetECCResult
*
*  Function description
*    Returns the result of the ECC correction status.
*
*  Parameters
*    pInst      Physical layer instance.
*    pResult    [OUT] ECC correction result.
*/
static void _GetECCResult(const NAND_ONFI_INST * pInst, FS_NAND_ECC_RESULT * pResult) {
  U8  Status;
  U8  CorrectionStatus;
  U8  MaxNumBitErrorsCorrected;
  U8  NumBitErrorsCorrectable;
  U8  eccStatus;

  //
  // Initialize local variables.
  //
  CorrectionStatus         = FS_NAND_CORR_NOT_APPLIED;
  MaxNumBitErrorsCorrected = 0;
  //
  // Read the status of the last page read operation and determine if there were any uncorrectable bit errors.
  //
  _EnableCE(pInst);
  Status = _ReadStatus(pInst);
  _DisableCE(pInst);
  if ((Status & STATUS_ERROR) != 0u) {
    CorrectionStatus = FS_NAND_CORR_FAILURE;
    _Reset(pInst);                // Clear the read error to prevent the failure of other operations.
  } else {
    NumBitErrorsCorrectable = pInst->NumBitErrorsCorrectable;
    if (NumBitErrorsCorrectable == 8u) {
      //
      // Micron NAND flash devices with HW ECC that are able to correct 8 bit errors
      // return an approximate number of bit errors corrected. This information is
      // encoded in bits 3 and 4 of the status register.
      //
      eccStatus = Status & STATUS_ECC_MASK;
      if (eccStatus != 0u) {
        CorrectionStatus = FS_NAND_CORR_APPLIED;
        if (eccStatus == STATUS_ECC_1_3_BIT_ERRORS) {
          MaxNumBitErrorsCorrected = 3;
        } else {
          if (eccStatus == STATUS_ECC_4_6_BIT_ERRORS) {
            MaxNumBitErrorsCorrected = 6;
          } else {
            if (eccStatus == STATUS_ECC_7_8_BIT_ERRORS) {
              MaxNumBitErrorsCorrected = 8;
            }
          }
        }
      }
    } else {
      //
      // Some of the Micron NAND flash devices are not able to return the actual number
      // of bit errors corrected. Since this value is unknown, we set it to the maximum
      // number of bit errors the HW ECC is able to correct, if the NAND flash device
      // reports that a page has to be re-written.
      //
      if ((Status & STATUS_REWRITE_RECOMMENDED) != 0u) {
        CorrectionStatus         = FS_NAND_CORR_APPLIED;
        MaxNumBitErrorsCorrected = NumBitErrorsCorrectable;
      }
    }
  }
  //
  // Return the determined values.
  //
  pResult->CorrectionStatus    = CorrectionStatus;
  pResult->MaxNumBitsCorrected = MaxNumBitErrorsCorrected;
}

/*********************************************************************
*
*       _ReadFromPage
*
*  Function description
*    Reads data from two different locations of a NAND page.
*
*  Parameters
*    pInst        Physical layer instance.
*    PageIndex    Index of the NAND page to read from (0-based).
*    pData0       [OUT] Data read from Off0 of the NAND page.
*    Off0         Byte offset to read from for pData0.
*    NumBytes0    Number of bytes to be read.
*    pData1       [OUT] Data read from Off1 of a NAND page.
*    Off1         Byte offset to read from for pData1.
*    NumBytes1    Number of bytes to be read from Off1.
*
*  Return value
*    ==0      OK, data successfully read.
*    !=0      An error occurred.
*
*  Additional information
*    Typically used to read data and spare area at once.
*
*  Notes
*    (1) We do not reset the NAND flash device in case of read error
*        in order to preserve the contents of the status register.
*        The contents of the status register is read by the Universal
*        NAND driver via _PHY_GetECCResult() to check the number of
*        bit errors.
*/
static int _ReadFromPage(const NAND_ONFI_INST * pInst, U32 PageIndex, void * pData0, unsigned Off0, unsigned NumBytes0, void * pData1, unsigned Off1, unsigned NumBytes1) {
  int r;
  U8  DataBusWidth;
  U8  NumBytesColAddr;
  U8  NumBytesRowAddr;

  ASSERT_PARA_IS_ALIGNED(pInst, SEGGER_PTR2ADDR(pData0) | Off0 |  NumBytes0 | SEGGER_PTR2ADDR(pData1) | Off1 | NumBytes1);
  DataBusWidth    = pInst->DataBusWidth;
  NumBytesColAddr = pInst->NumBytesColAddr;
  NumBytesRowAddr = pInst->NumBytesRowAddr;
  _EnableCE(pInst);
  //
  // Select the start address of the first location to read from
  //
  _WriteCmd(pInst, CMD_READ_1);
  _WriteAddrColRow(pInst, Off0, NumBytesColAddr, PageIndex, NumBytesRowAddr, DataBusWidth);
  //
  // Start the execution of read command and wait for it to finish
  //
  _WriteCmd(pInst, CMD_READ_2);
  r = pInst->pDevice->pfWaitForEndOfRead(pInst);
  //
  // The data to read is now in the data register of device.
  // Copy the data from the first location to host memory
  //
  if ((pData0 != NULL) && (NumBytes0 != 0u)) {
    _WriteCmd(pInst, CMD_READ_1);    // Revert to read mode. pfWaitForEndOfRead() change it to status mode
    _ReadData(pInst, pData0, NumBytes0, DataBusWidth);
  }
  if ((pData1 != NULL) && (NumBytes1 != 0u)) {
    //
    // Select the start address of the second location to read from
    //
    _WriteCmd(pInst, CMD_RANDOM_READ_1);
    _WriteAddrCol(pInst, Off1, NumBytesColAddr, DataBusWidth);
    _WriteCmd(pInst, CMD_RANDOM_READ_2);
    //
    // Copy the data from the second location to host memory
    //
    _ReadData(pInst, pData1, NumBytes1, DataBusWidth);
  }
  _DisableCE(pInst);
  //
  // Note 1
  //
  return r;
}

/*********************************************************************
*
*       _WriteToPage
*
*  Function description
*    Writes data to two different locations of a NAND page.
*
*  Parameters
*    pInst        Physical layer instance.
*    PageIndex    Index of the NAND page to write to (0-based).
*    pData0       [IN] Data to be written to the NAND page at Off0.
*    Off0         Byte offset to write to for pData0.
*    NumBytes0    Number of bytes to be written at Off0.
*    pData1       [IN] Data to be written to the NAND page at Off1.
*    Off1         Byte offset to write to for pData1.
*    NumBytes1    Number of bytes to be written at Off1.
*
*  Return value
*    ==0      OK, data successfully written.
*    !=0      An error has occurred.
*
*  Additional information
*    Typically used to write data and spare area at the same time.
*/
static int _WriteToPage(const NAND_ONFI_INST * pInst, U32 PageIndex, const void * pData0, unsigned Off0, unsigned NumBytes0, const void * pData1, unsigned Off1, unsigned NumBytes1) {
  int r;
  U8  DataBusWidth;
  U8  NumBytesColAddr;
  U8  NumBytesRowAddr;

  ASSERT_PARA_IS_ALIGNED(pInst, SEGGER_PTR2ADDR(pData0) | Off0 | NumBytes0 | SEGGER_PTR2ADDR(pData1) | Off1 | NumBytes1);
  DataBusWidth    = pInst->DataBusWidth;
  NumBytesColAddr = pInst->NumBytesColAddr;
  NumBytesRowAddr = pInst->NumBytesRowAddr;
  _EnableCE(pInst);
  //
  // Select the start address of the first location to write to.
  //
  _WriteCmd(pInst, CMD_WRITE_1);
  _WriteAddrColRow(pInst, Off0, NumBytesColAddr, PageIndex, NumBytesRowAddr, DataBusWidth);
  //
  // Load the data register of device with the first data to write.
  //
  _WriteData(pInst, pData0, NumBytes0, DataBusWidth);
  if ((pData1 != NULL) && (NumBytes1 != 0u)) {
    //
    // Select the start address of the second location to write to.
    //
    _WriteCmd(pInst, CMD_RANDOM_WRITE);
    _WriteAddrCol(pInst, Off1, NumBytesColAddr, DataBusWidth);
    //
    // Load the data register of device with the second data to write
    //
    _WriteData(pInst, pData1, NumBytes1, DataBusWidth);
  }
  //
  // Execute the write command and wait for it to finish
  //
  _WriteCmd(pInst, CMD_WRITE_2);
  r = _WaitForEndOfOperation(pInst);
  _DisableCE(pInst);
  if (r != 0) {
    _Reset(pInst);
  }
  return r;
}

/*********************************************************************
*
*      _MACRONIX_Identify
*
*  Function description
*    Checks for a Macronix NAND flash device.
*
*  Parameters
*    pInst    Physical layer instance.
*    pId      Information returned by the READ ID command.
*
*  Return value
*    ==0    This is a Macronix NAND flash device.
*    !=0    This is not a Macronix NAND flash device.
*
*  Additional information
*    pId has to contain at least 3 bytes.
*/
static int _MACRONIX_Identify(NAND_ONFI_INST * pInst, const U8 * pId) {
  int r;
  U8  MfgId;

  FS_USE_PARA(pInst);
  r     = 1;           // Device not supported.
  MfgId = *pId;        // The first byte is the manufacturer id.
  if (MfgId == MFG_ID_MACRONIX) {
    //
    // All Macronix devices are supported.
    //
    r = 0;             // This device is supported.
  }
  return r;
}

/*********************************************************************
*
*      _MACRONIX_ReadApplyPara
*
*  Function description
*    Reads the ONFI parameters from a Macronix NAND flash device
*    and stores the required information to physical layer instance.
*
*  Parameters
*    pInst      Instance of the physical layer.
*    pPara      [OUT] Read ONFI parameters.
*
*  Return value
*    ==0    OK, operation succeeded.
*    !=0    An error occurred.
*/
static int _MACRONIX_ReadApplyPara(NAND_ONFI_INST * pInst, NAND_ONFI_PARA * pPara) {
  int r;
  int IsECCPresent;
  int IsECCEnabledPerm;

  r = _ReadApplyPara(pInst, pPara);
  if (r == 0) {
    //
    // Check if the device has HW ECC and if the ECC is always enabled.
    //
    IsECCEnabledPerm = 0;
    IsECCPresent = _GetECCInfo(pInst, &IsECCEnabledPerm);
    //
    // All devices with internal ECC report an bit error correctability of 0
    // via the ONFI parameters. We set here the correct ECC level knowing that
    // all Macronix devices with internal ECC are able to correct up to 4 bit
    // errors.
    //
    if (IsECCPresent != 0) {
      pPara->ECCInfo.NumBitsCorrectable = 4;
    }
    //
    // Save the calculated information.
    //
    pInst->IsECCEnabledPerm            = (U8)IsECCEnabledPerm;
    pPara->ECCInfo.IsHW_ECCEnabledPerm = (U8)IsECCEnabledPerm;
    pPara->ECCInfo.HasHW_ECC           = (U8)IsECCPresent;
  }
  return r;
}

/*********************************************************************
*
*      _MICRON_Identify
*
*  Function description
*    Checks for a Micron NAND flash device.
*
*  Parameters
*    pInst    Physical layer instance.
*    pId      Information returned by the READ ID command.
*
*  Return value
*    ==0    This is a Micron NAND flash device.
*    !=0    This is not a Micron NAND flash device.
*
*  Additional information
*    pId has to contain at least 3 bytes.
*/
static int _MICRON_Identify(NAND_ONFI_INST * pInst, const U8 * pId) {
  int r;
  U8  MfgId;

  FS_USE_PARA(pInst);
  r     = 1;           // Device not supported.
  MfgId = *pId;        // The first byte is the manufacturer id.
  if (MfgId == MFG_ID_MICRON) {
    //
    // All Micron devices are supported.
    //
    r = 0;             // This device is supported.
  }
  return r;
}

/*********************************************************************
*
*      _MICRON_ReadApplyPara
*
*  Function description
*    Reads the ONFI parameters from a Micron NAND flash device
*    and stores the required information to physical layer instance.
*
*  Parameters
*    pInst      Instance of the physical layer.
*    pPara      [OUT] Read ONFI parameters.
*
*  Return value
*    ==0    OK, operation succeeded.
*    !=0    An error occurred.
*/
static int _MICRON_ReadApplyPara(NAND_ONFI_INST * pInst, NAND_ONFI_PARA * pPara) {
  int      r;
  unsigned ldNumPlanes;
  unsigned PlaneInfo;
  int      IsECCPresent;
  int      IsECCEnabledPerm;
  U8       abId[PLANE_INFO_BYTE_OFF + 1];
  unsigned BytesPerSpareArea;

  r = _ReadApplyPara(pInst, pPara);
  if (r == 0) {
    ldNumPlanes = 0;
    FS_MEMSET(abId, 0, sizeof(abId));
    _ReadId(pInst, abId, sizeof(abId));
    //
    // Most of the Micron devices with HW ECC have 2 planes.
    // The number of planes is not encoded in the ONFI parameters.
    // We have to take this information from the byte 4 of
    // the response to READ ID command.
    //
    PlaneInfo = abId[PLANE_INFO_BYTE_OFF];
    if (((PlaneInfo >> PLANE_INFO_BIT) & PLANE_INFO_MASK) == PLANE_INFO_2PLANES) {
      ldNumPlanes = 1;
    }
    //
    // The first 64 bytes of the spare are protected by ECC
    // on the Micron devices with HW ECC that are able to correct 8-bit errors.
    // The remaining of the spare area (64 bytes) is used to store the ECC
    // and it cannot be used to store any other data. We report that the spare
    // area is 64 instead of 128 bytes large in order to prevent that the Universal
    // NAND driver stores data to area reserved for ECC. One example of such
    // a NAND flash device is the Micron MT29F1G08ABAFA.
    //
    BytesPerSpareArea = pPara->BytesPerSpareArea;
    if (   (pPara->ECCInfo.NumBitsCorrectable == 8u)
        && (BytesPerSpareArea == 128u)) {
      BytesPerSpareArea = 64;
    }
    //
    // Check if the device has HW ECC and if the ECC is always enabled.
    //
    IsECCEnabledPerm = 0;
    IsECCPresent = _GetECCInfo(pInst, &IsECCEnabledPerm);
    //
    // Save the calculated information.
    //
    pInst->ldNumPlanes                 = (U8)ldNumPlanes;
    pInst->IsECCEnabledPerm            = (U8)IsECCEnabledPerm;
    pInst->BytesPerSpareArea           = (U16)BytesPerSpareArea;
    pPara->ECCInfo.IsHW_ECCEnabledPerm = (U8)IsECCEnabledPerm;
    pPara->ECCInfo.HasHW_ECC           = (U8)IsECCPresent;
  }
  return r;
}

/*********************************************************************
*
*      _SKYHIGH_Identify
*
*  Function description
*    Checks for a SkyHigh NAND flash device.
*
*  Parameters
*    pInst    Physical layer instance.
*    pId      Information returned by the READ ID command.
*
*  Return value
*    ==0    This is a SkyHigh NAND flash device.
*    !=0    This is not a SkyHigh NAND flash device.
*
*  Additional information
*    pId has to contain at least 3 bytes.
*/
static int _SKYHIGH_Identify(NAND_ONFI_INST * pInst, const U8 * pId) {          //lint -efunc(818, _SKYHIGH_Identify) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory]. Rationale: this function is called indirectly via a pointer.
  int            r;
  U8             MfgId;
  NAND_ONFI_PARA Para;

  r     = 1;           // Device not supported.
  MfgId = *pId;        // The first byte is the manufacturer id.
  if (MfgId == MFG_ID_SKYHIGH) {
    //
    // The SkyHigh NAND flash devices without internal HW ECC have the
    // "Number of bits ECC correctability" field in the ONFI parameters
    // set to a value different than 0 therefore we use this information
    // here in order to identify them.
    //
    FS_MEMSET(&Para, 0, sizeof(Para));
    r = _ReadONFIPara(pInst, &Para);
    if (Para.ECCInfo.NumBitsCorrectable != 0u) {
      r = 0;            // This device is supported.
    }
  }
  return r;
}

/*********************************************************************
*
*      _SKYHIGH_IdentifyHW_ECC
*
*  Function description
*    Checks for a SkyHigh NAND flash device with internal HW ECC.
*
*  Parameters
*    pInst    Physical layer instance.
*    pId      Information returned by the READ ID command.
*
*  Return value
*    ==0    This is a SkyHigh NAND flash device.
*    !=0    This is not aSkyHigh NAND flash device.
*
*  Additional information
*    pId has to contain at least 3 bytes.
*/
static int _SKYHIGH_IdentifyHW_ECC(NAND_ONFI_INST * pInst, const U8 * pId) {     //lint -efunc(818, _SKYHIGH_IdentifyHW_ECC) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory]. Rationale: this function is called indirectly via a pointer.
  int            r;
  int            Result;
  U8             MfgId;
  NAND_ONFI_PARA Para;

  r     = 1;           // Device not supported.
  MfgId = *pId;        // The first byte is the manufacturer id.
  if (MfgId == MFG_ID_SKHYNIX) {
    //
    // Stacked SkyHigh devices report a different manufacturer id (that is of SK Hynix)
    // if the last selected die before reset was not the first die. We select
    // the first die here via the READ STATUS ENHANCED command and read the id
    // again to check if it is indeed a SK Hynix device.
    //
    FS_MEMSET(&Para, 0, sizeof(Para));
    Result = _ReadONFIPara(pInst, &Para);      // Get the number of row address bytes.
    if (Result == 0) {
      pInst->NumBytesRowAddr = (U8)(Para.NumAddrBytes & 0x0Fu);
      (void)_ReadStatusEnhanced(pInst, 0);
      _ReadId(pInst, &MfgId, sizeof(MfgId));
    }
  }
  if (MfgId == MFG_ID_SKYHIGH) {
    //
    // The SkyHigh NAND flash devices with internal HW ECC have the
    // "Number of bits ECC correctability" field in the ONFI parameters
    // set to 0 therefore we use this information here in order to identify them.
    // In addition, the device returns valid ONFI information only with the HW ECC
    // enabled.
    //
    (void)_EnableECC(pInst);
    FS_MEMSET(&Para, 0, sizeof(Para));
    Result = _ReadONFIPara(pInst, &Para);
    if (Result == 0) {
      if (Para.ECCInfo.NumBitsCorrectable == 0u) {
        r = 0;            // This device is supported.
      }
    }
  }
  return r;
}

/*********************************************************************
*
*      _SKYHIGH_ReadApplyPara
*
*  Function description
*    Reads the ONFI parameters from a SkyHigh NAND flash device
*    and stores the required information to physical layer instance.
*
*  Parameters
*    pInst      Instance of the physical layer.
*    pPara      [OUT] Read ONFI parameters.
*
*  Return value
*    ==0    OK, operation succeeded.
*    !=0    An error occurred.
*/
static int _SKYHIGH_ReadApplyPara(NAND_ONFI_INST * pInst, NAND_ONFI_PARA * pPara) {
  int      r;
  int      IsECCPresent;
  int      IsECCEnabledPerm;
  U8       abId[PLANE_INFO_BYTE_OFF + 1];
  unsigned ldNumPlanes;
  unsigned PlaneInfo;
  unsigned ldNumDies;
  unsigned DeviceId1;
  unsigned DeviceId2;
  unsigned ldBlocksPerDie;
  unsigned NumBitsCorrectable;

  r = _ReadApplyPara(pInst, pPara);
  if (r == 0) {
    //
    // All devices with HW ECC report an error correctability of 0 via the
    // ONFI parameters. All these devices are able to correct 4 bit errors
    // using the HW ECC. In addition, the HW ECC cannot be disabled on
    // these devices.
    //
    IsECCPresent       = 0;
    IsECCEnabledPerm   = 0;
    NumBitsCorrectable = pPara->ECCInfo.NumBitsCorrectable;
    if (NumBitsCorrectable == 0u) {
      //
      // According to [3] the SkyHigh NAND flash devices are able to
      // correct up to 6 bit errors in a 32 byte block. We set
      // here the number of bits the HW ECC is able to correct
      // and leave the size of ECC block unchanged (that is 512 bytes)
      // because the Universal NAND driver expects that the stripe
      // of the spare area corresponding to an ECC block is larger
      // than 8 bytes. Setting the size of the ECC block to 32 bytes
      // would result in a spare area stripe size of 4 bytes for a
      // total number of bytes in the spare area of 128 bytes.
      // However, this value is too small for the Universal NAND driver
      // to operate correctly.
      //
      NumBitsCorrectable = 6;
      IsECCPresent       = 1;
      IsECCEnabledPerm   = 1;
      //
      // Make sure that the bit 4 in the status register
      // is set to 1 if an uncorrectable bit error occurs.
      //
      r = _EnableECCCorrectionStatus(pInst);
    }
    //
    // Calculate the number of planes.
    //
    ldNumPlanes = 0;
    FS_MEMSET(abId, 0, sizeof(abId));
    _ReadId(pInst, abId, sizeof(abId));
    //
    // The number of planes is not encoded in the ONFI parameters.
    // We have to take this information from the byte 4 of
    // the response to READ ID command.
    //
    PlaneInfo = abId[PLANE_INFO_BYTE_OFF];
    if (((PlaneInfo >> PLANE_INFO_BIT) & PLANE_INFO_MASK) == PLANE_INFO_2PLANES) {
      ldNumPlanes = 1;
    }
    //
    // Calculate the number of units.
    //
    ldNumDies      = _ld(pPara->NumDies);
    ldBlocksPerDie = pInst->ldBlocksPerDie;
    DeviceId1      = abId[1];
    DeviceId2      = abId[2];
    //
    // S34ML08G3 has one CE signal and 2 or 4 KiB pages.
    // This device is composed of two separate 4 Gbit dies which is not reported as such
    // via the "Number of logical units (LUNs)" ONFI parameter.
    // Therefore, we correct here the number of dies and the number of blocks in a die for this device.
    //
    // S34ML16G3 has two CE signals and 2 KiB pages.
    // This device reports the same id as S34ML08G3 with one CE signal and 2 KiB pages.
    // S34ML16G3 is composed of four separate 4 Gbit dies with each group of 2 dies being selected via
    // a CE signal. That is, this device behaves as two separate S34ML08G3 devices. S34ML16G3
    // reports the correct number of dies but the number of blocks per die is incorrect therefore we
    // have to correct this value here.
    //
    if (   (DeviceId1 == 0xD3u)
        && (DeviceId2 == 0x01u)) {
      if (ldNumDies == 0u) {              // Is a S34ML08G3 device?
        ++ldNumDies;
      }
      --ldBlocksPerDie;
    }
    //
    // S34ML16G2 with two CE signals. This device is composed of four separate 4 Gbit dies but
    // the device reports only two via the "Number of logical units (LUNs)" ONFI parameter.
    // Therefore, we correct here the number of dies and the number of blocks in a die.
    //
    if (   (DeviceId1 == 0xD3u)
        && (DeviceId2 == 0xD1u)) {
      ++ldNumDies;
      --ldBlocksPerDie;
    }
    //
    // Save the calculated information.
    //
    pInst->ldNumPlanes                 = (U8)ldNumPlanes;
    pInst->ldNumDies                   = (U8)ldNumDies;
    pInst->ldBlocksPerDie              = (U8)ldBlocksPerDie;
    pInst->IsECCEnabledPerm            = (U8)IsECCEnabledPerm;
    pPara->ECCInfo.IsHW_ECCEnabledPerm = (U8)IsECCEnabledPerm;
    pPara->ECCInfo.HasHW_ECC           = (U8)IsECCPresent;
    pPara->ECCInfo.NumBitsCorrectable  = (U8)NumBitsCorrectable;
    pPara->BadBlockMarkingType         = FS_NAND_BAD_BLOCK_MARKING_TYPE_FSLPS;
  }
  return r;
}

/*********************************************************************
*
*       _SKYHIGH_WaitForEndOfRead
*
*  Function description
*    Waits for the NAND flash device to complete a read operation with ECC.
*
*  Parameters
*    pInst    Phy. layer instance.
*
*  Return value
*    ==0    OK, Page read successfully.
*    !=0    An error occurred.
*/
static int _SKYHIGH_WaitForEndOfRead(const NAND_ONFI_INST * pInst) {
  U8 Status;

  //
  // Try to use the hardware pin to find out when busy is cleared.
  //
  (void)_WaitWhileBusy(pInst, 0);
  //
  // Wait until the NAND flash is ready for the next operation.
  //
  do {
    Status = _ReadStatus(pInst);
  } while ((Status & STATUS_READY) == 0u);
  if ((Status & STATUS_READ_ERROR) != 0u) {
    return 1;
  }
  return 0;
}

/*********************************************************************
*
*       _SKYHIGH_GetECCResult
*
*  Function description
*    Returns the status of the ECC correction.
*
*  Parameters
*    pInst      Physical layer instance.
*    pResult    [OUT] ECC correction result.
*/
static void _SKYHIGH_GetECCResult(const NAND_ONFI_INST * pInst, FS_NAND_ECC_RESULT * pResult) {
  U8  Status;
  U8  CorrectionStatus;
  U8  MaxNumBitErrorsCorrected;
  int r;
  U8  NumBitErrorsCorrectable;

  //
  // Initialize local variables.
  //
  CorrectionStatus         = FS_NAND_CORR_NOT_APPLIED;
  MaxNumBitErrorsCorrected = 0;
  NumBitErrorsCorrectable  = pInst->NumBitErrorsCorrectable;
  //
  // Read the status of the last page read operation and determine if there were any uncorrectable bit errors.
  //
  _EnableCE(pInst);
  Status = _ReadStatus(pInst);
  _DisableCE(pInst);
  if ((Status & STATUS_READ_ERROR) != 0u) {
    CorrectionStatus = FS_NAND_CORR_FAILURE;
  } else {
    //
    // Check if the "rewrite recommended" flag is set. This flag shares the same position in the status
    // register with the uncorrectable bit error flag (STATUS_READ_ERROR). The meaning of this flag
    // can be changed via a feature set operation. Therefore, we have to temporarily switch the meaning
    // of this flag here and read the status register again.
    //
    r = _DisableECCCorrectionStatus(pInst);
    if (r == 0) {
      _EnableCE(pInst);
      Status = _ReadStatus(pInst);
      _DisableCE(pInst);
      if ((Status & STATUS_READ_ERROR) != 0u) {
        CorrectionStatus         = FS_NAND_CORR_APPLIED;
        MaxNumBitErrorsCorrected = NumBitErrorsCorrectable;
      }
    }
    (void)_EnableECCCorrectionStatus(pInst);
  }
  //
  // Return the determined values.
  //
  pResult->CorrectionStatus    = CorrectionStatus;
  pResult->MaxNumBitsCorrected = MaxNumBitErrorsCorrected;
}

/*********************************************************************
*
*       _GIGADEVICE_WriteToPageWithRelocation
*
*  Function description
*    Writes data to a NAND page and relocates the bad block marker.
*
*  Parameters
*    pInst        Physical layer instance.
*    PageIndex    Index of the NAND page to write to (0-based).
*    pDataMain    [IN] Data to be written to the main area of the page.
*    pDataSpare   [IN] Data to be written to the spare area of the page.
*
*  Return value
*    ==0      OK, data successfully written.
*    !=0      An error has occurred.
*/
static int _GIGADEVICE_WriteToPageWithRelocation(const NAND_ONFI_INST * pInst, U32 PageIndex, const U8 * pDataMain, const U8 * pDataSpare) {
  int      r;
  U8       DataBusWidth;
  U8       NumBytesColAddr;
  U8       NumBytesRowAddr;
  U8       abDataMain[2];
  U8       abDataSpare[2];
  unsigned Off;
  unsigned NumBytesMain;
  unsigned NumBytesSpare;
  unsigned BytesPerPage;
  unsigned BytesPerSpareArea;
  unsigned NumBytesToWrite;

  DataBusWidth      = pInst->DataBusWidth;
  NumBytesColAddr   = pInst->NumBytesColAddr;
  NumBytesRowAddr   = pInst->NumBytesRowAddr;
  BytesPerPage      = 1uL << pInst->ldBytesPerPage;
  BytesPerSpareArea = pInst->BytesPerSpareArea;
  //
  // Relocate the bad block marker to the second byte of the spare area.
  // We use temporary buffers of 2 bytes in order to support NAND flash
  // devices with a bus width of 16 bits.
  //
  FS_MEMCPY(abDataMain,  pDataMain,  2);
  FS_MEMCPY(abDataSpare, pDataSpare, 2);
  abDataMain[OFF_BBM_MAIN]   = pDataSpare[OFF_BBM_SPARE];
  abDataSpare[OFF_BBM_SPARE] = pDataMain[OFF_BBM_MAIN];
  pDataMain    += 2u;
  NumBytesMain  = BytesPerPage - 2u;
  pDataSpare   += 2u;
  NumBytesSpare = BytesPerSpareArea - 2u;
  Off           = 0;
  _EnableCE(pInst);
  //
  // Load the first 2 bytes of the main area.
  //
  _WriteCmd(pInst, CMD_WRITE_1);
  _WriteAddrColRow(pInst, Off, NumBytesColAddr, PageIndex, NumBytesRowAddr, DataBusWidth);
  NumBytesToWrite = sizeof(abDataMain);
  _WriteData(pInst, abDataMain, NumBytesToWrite, DataBusWidth);
  Off += NumBytesToWrite;
  //
  // Load the remaining bytes of the main area.
  //
  _WriteCmd(pInst, CMD_RANDOM_WRITE);
  _WriteAddrCol(pInst, Off, NumBytesColAddr, DataBusWidth);
  _WriteData(pInst, pDataMain, NumBytesMain, DataBusWidth);
  Off += NumBytesMain;
  //
  // Load the first 2 bytes of the spare area.
  //
  _WriteCmd(pInst, CMD_RANDOM_WRITE);
  _WriteAddrCol(pInst, Off, NumBytesColAddr, DataBusWidth);
  NumBytesToWrite = sizeof(abDataSpare);
  _WriteData(pInst, abDataSpare, NumBytesToWrite, DataBusWidth);
  Off += NumBytesToWrite;
  //
  // Load the remaining bytes of the spare area.
  //
  _WriteCmd(pInst, CMD_RANDOM_WRITE);
  _WriteAddrCol(pInst, Off, NumBytesColAddr, DataBusWidth);
  _WriteData(pInst, pDataSpare, NumBytesSpare, DataBusWidth);
  //
  // Execute the write command and wait for it to finish.
  //
  _WriteCmd(pInst, CMD_WRITE_2);
  r = _WaitForEndOfOperation(pInst);
  _DisableCE(pInst);
  if (r != 0) {
    _Reset(pInst);
  }
  return r;
}

/*********************************************************************
*
*      _GIGADEVICE_Identify
*
*  Function description
*    Checks for a GigaDevice NAND flash device.
*
*  Parameters
*    pInst    Physical layer instance.
*    pId      Information returned by the READ ID command.
*
*  Return value
*    ==0    This is a GigaDevice NAND flash device.
*    !=0    This is not a GigaDevice NAND flash device.
*
*  Additional information
*    pId has to contain at least 3 bytes.
*/
static int _GIGADEVICE_Identify(NAND_ONFI_INST * pInst, const U8 * pId) {
  int r;
  U8  MfgId;

  FS_USE_PARA(pInst);
  r     = 1;           // Device not supported.
  MfgId = *pId;        // The first byte is the manufacturer id.
  if (MfgId == MFG_ID_GIGADEVICE) {
    //
    // All GigaDevice devices are supported.
    //
    r = 0;             // This device is supported.
  }
  return r;
}

/*********************************************************************
*
*      _GIGADEVICE_ReadApplyPara
*
*  Function description
*    Reads the ONFI parameters from a GigaDevice NAND flash device
*    and stores the required information to physical layer instance.
*
*  Parameters
*    pInst      Instance of the physical layer.
*    pPara      [OUT] Read ONFI parameters.
*
*  Return value
*    ==0    OK, operation succeeded.
*    !=0    An error occurred.
*/
static int _GIGADEVICE_ReadApplyPara(NAND_ONFI_INST * pInst, NAND_ONFI_PARA * pPara) {
  int r;
  int IsECCPresent;
  int IsECCEnabledPerm;

  r = _ReadApplyPara(pInst, pPara);
  if (r == 0) {
    //
    // Check if the device has HW ECC and if the ECC is always enabled.
    //
    IsECCEnabledPerm = 0;
    IsECCPresent = _GetECCInfo(pInst, &IsECCEnabledPerm);
    //
    // Save the calculated information.
    //
    pInst->IsECCEnabledPerm            = (U8)IsECCEnabledPerm;
    pPara->ECCInfo.IsHW_ECCEnabledPerm = (U8)IsECCEnabledPerm;
    pPara->ECCInfo.HasHW_ECC           = (U8)IsECCPresent;
    pPara->BadBlockMarkingType         = FS_NAND_BAD_BLOCK_MARKING_TYPE_FLPMS;
  }
  return r;
}

/*********************************************************************
*
*       _GIGADEVICE_ReadFromPage
*
*  Function description
*    Reads data from two different locations of a NAND page (GigaDevice specific).
*
*  Parameters
*    pInst        Physical layer instance.
*    PageIndex    Index of the NAND page to read from (0-based).
*    pData0       [OUT] Data read from Off0 of the NAND page.
*    Off0         Byte offset to read from for pData0.
*    NumBytes0    Number of bytes to be read.
*    pData1       [OUT] Data read from Off1 of a NAND page.
*    Off1         Byte offset to read from for pData1.
*    NumBytes1    Number of bytes to be read from Off1.
*
*  Return value
*    ==0      OK, data successfully read.
*    !=0      An error occurred.
*
*  Additional information
*    Typically used to read data and spare area at once.
*
*  Notes
*    (1) We do not reset the NAND flash device in case of read error
*        in order to preserve the contents of the status register.
*        The contents of the status register is read by the Universal
*        NAND driver via _PHY_GetECCResult() to check the number of
*        bit errors.
*/
static int _GIGADEVICE_ReadFromPage(const NAND_ONFI_INST * pInst, U32 PageIndex, void * pData0, unsigned Off0, unsigned NumBytes0, void * pData1, unsigned Off1, unsigned NumBytes1) {
  int        r;
  int        IsRawMode;
  unsigned   BytesPerPage;
  unsigned   BytesPerSpareArea;
  U8         BlockStatus;
  U8       * pDataMain;
  U8       * pDataSpare;

  ASSERT_PARA_IS_ALIGNED(pInst, SEGGER_PTR2ADDR(pData0) | Off0 |  NumBytes0 | SEGGER_PTR2ADDR(pData1) | Off1 | NumBytes1);
  IsRawMode = (int)pInst->IsRawMode;
  if (   (IsRawMode != 0)
      || (_IsFirstBlock(pInst, PageIndex) != 0)
      || (   (_IsFirstPage(pInst, PageIndex) == 0)
          && (_IsLastPage(pInst, PageIndex) == 0))) {
    //
    // The bad block marker is stored only in the first and last page of a block.
    // Therefore we do not have to relocate the bad block marker for any other page.
    // In addition, we write the data as is if the application requests it.
    // The manufacturer guarantees that the first block is not defective and
    // because of this we do not perform any relocation for this block.
    //
    r = _ReadFromPage(pInst, PageIndex, pData0, Off0, NumBytes0, pData1, Off1, NumBytes1);
  } else {
    BytesPerPage      = 1uL << pInst->ldBytesPerPage;
    BytesPerSpareArea = pInst->BytesPerSpareArea;
    if (   ((Off0 == 0u) && (NumBytes0 == BytesPerPage) && (Off1 == BytesPerPage) && (NumBytes1 == BytesPerSpareArea))
        || ((Off0 == 0u) && (NumBytes0 == (BytesPerPage + BytesPerSpareArea)))) {
      //
      // Handle the most common case where the Universal NAND driver reads the entire
      // page including the spare area.
      //
      r = _ReadFromPage(pInst, PageIndex, pData0, Off0, NumBytes0, pData1, Off1, NumBytes1);
      //
      // Relocate the bad block marker from the main to the spare area.
      //
      pDataMain  = SEGGER_PTR2PTR(U8, pData0);
      if (pData1 == NULL) {
        pDataSpare = pDataMain + BytesPerPage;
      } else {
        pDataSpare = SEGGER_PTR2PTR(U8, pData1);
      }
      BlockStatus               = pDataMain[OFF_BBM_MAIN];
      pDataMain[OFF_BBM_MAIN]   = pDataSpare[OFF_BBM_SPARE];
      pDataSpare[OFF_BBM_SPARE] = BlockStatus;
    } else {
      if (    (   ((Off0 > OFF_BBM_MAIN) && ((Off0 + NumBytes0) <= (BytesPerPage + OFF_BBM_SPARE)))
               || (Off0 > (BytesPerPage + OFF_BBM_SPARE)))
           && (   ((Off1 > OFF_BBM_MAIN) && ((Off1 + NumBytes1) <= (BytesPerPage + OFF_BBM_SPARE)))
               || (Off1 > (BytesPerPage + OFF_BBM_SPARE)))) {
        //
        // Do not perform any relocation if neither the bad block marker stored
        // in the main area nor the bad block marker stored in the spare area are read.
        //
        r = _ReadFromPage(pInst, PageIndex, pData0, Off0, NumBytes0, pData1, Off1, NumBytes1);
      } else {
        if (NumBytes1 == 0u) {
          if (NumBytes0 == 1u) {
            if (Off0 == OFF_BBM_MAIN) {
              //
              // This is reached only during the testing of the physical layer
              // which reads the data byte by byte. Read here the data directly from
              // the spare area.
              //
              r = _ReadFromPage(pInst, PageIndex, pData0, BytesPerPage + OFF_BBM_SPARE, NumBytes0, NULL, 0, 0);
            } else {
              if (Off0 == (BytesPerPage + OFF_BBM_SPARE)) {
                //
                // Another common case is when the Universal NAND driver reads only the bad block
                // marker that is stored in the main area. In this case we read only the bad block
                // marker from the main instead of the spare area.
                //
                r = _ReadFromPage(pInst, PageIndex, pData0, OFF_BBM_MAIN, NumBytes0, NULL, 0, 0);
              } else {
                r = _ReadFromPage(pInst, PageIndex, pData0, Off0, NumBytes0, NULL, 0, 0);
              }
            }
          } else {
            if ((Off0 == BytesPerPage) && (NumBytes0 == BytesPerSpareArea)) {
              //
              // Handle the case where the Universal NAND driver reads only the spare area of a page.
              //
              r = _ReadFromPage(pInst, PageIndex, &BlockStatus, OFF_BBM_MAIN, sizeof(BlockStatus), pData0, Off0, NumBytes0);
              pDataSpare = SEGGER_PTR2PTR(U8, pData0);
              pDataSpare[OFF_BBM_SPARE] = BlockStatus;
            } else {
              r = 1;              // Error, this read operation is not supported.
            }
          }
        } else {
          if ((NumBytes1 == 1u) && (NumBytes0 == 1u)) {
            //
            // This branch is reached only during the testing of the physical layer
            // which reads the data byte by byte.
            //
            if (Off1 == OFF_BBM_MAIN) {
              Off1 = BytesPerPage + OFF_BBM_SPARE;
            } else {
              if (Off1 == (BytesPerPage + OFF_BBM_SPARE)) {
                Off1 = OFF_BBM_MAIN;
              }
            }
            if (Off0 == OFF_BBM_MAIN) {
              Off0 = BytesPerPage + OFF_BBM_SPARE;
            } else {
              if (Off0 == (BytesPerPage + OFF_BBM_SPARE)) {
                Off0 = OFF_BBM_MAIN;
              }
            }
            r = _ReadFromPage(pInst, PageIndex, pData0, Off0, NumBytes0, pData1, Off1, NumBytes1);
          } else {
            r = 1;              // Error, this read operation is not supported.
          }
        }
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _GIGADEVICE_WriteToPage
*
*  Function description
*    Writes data to two different locations of a NAND page (GigaDevice specific).
*
*  Parameters
*    pInst        Physical layer instance.
*    PageIndex    Index of the NAND page to write to (0-based).
*    pData0       [IN] Data to be written to the NAND page at Off0.
*    Off0         Byte offset to write to for pData0.
*    NumBytes0    Number of bytes to be written at Off0.
*    pData1       [IN] Data to be written to the NAND page at Off1.
*    Off1         Byte offset to write to for pData1.
*    NumBytes1    Number of bytes to be written at Off1.
*
*  Return value
*    ==0      OK, data successfully written.
*    !=0      An error has occurred.
*
*  Additional information
*    Typically used to write data and spare area at the same time.
*/
static int _GIGADEVICE_WriteToPage(const NAND_ONFI_INST * pInst, U32 PageIndex, const void * pData0, unsigned Off0, unsigned NumBytes0, const void * pData1, unsigned Off1, unsigned NumBytes1) {
  int      r;
  int      IsRawMode;
  unsigned BytesPerPage;
  unsigned BytesPerSpareArea;

  ASSERT_PARA_IS_ALIGNED(pInst, SEGGER_PTR2ADDR(pData0) | Off0 | NumBytes0 | SEGGER_PTR2ADDR(pData1) | Off1 | NumBytes1);
  IsRawMode = (int)pInst->IsRawMode;
  if (   (IsRawMode != 0)
      || (_IsFirstBlock(pInst, PageIndex) != 0)
      || (   (_IsFirstPage(pInst, PageIndex) == 0)
          && (_IsLastPage(pInst, PageIndex) == 0))) {
    //
    // The bad block marker is stored only in the first and last page of a block.
    // Therefore we do not have to relocate the bad block marker for any other page.
    // In addition, we write the data as is if the application requests it.
    // The manufacturer guarantees that the first block is not defective and
    // because of this we do not perform any relocation for this block.
    //
    r = _WriteToPage(pInst, PageIndex, pData0, Off0, NumBytes0, pData1, Off1, NumBytes1);
  } else {
    //
    // We can safely assume here that the Universal NAND driver calls this function
    // to write the entire main and spare area of a page. Any other write operation
    // is currently not supported.
    //
    BytesPerPage      = 1uL << pInst->ldBytesPerPage;
    BytesPerSpareArea = pInst->BytesPerSpareArea;
    if ((Off0 == 0u) && (NumBytes0 == BytesPerPage) && (Off1 == BytesPerPage) && (NumBytes1 == BytesPerSpareArea)) {
      //
      // Swap the first byte in the main area with the second byte in the spare area.
      // We have to do this because the first byte in the main area of the first and
      // last page is used as bad block marker.
      //
      r = _GIGADEVICE_WriteToPageWithRelocation(pInst, PageIndex, SEGGER_CONSTPTR2PTR(const U8, pData0), SEGGER_CONSTPTR2PTR(const U8, pData1));
    } else {
      r = 1;                // Error, this write operation is not supported.
    }
  }
  return r;
}

/*********************************************************************
*
*      _WINBOND_Identify
*
*  Function description
*    Checks for a Winbond NAND flash device.
*
*  Parameters
*    pInst    Physical layer instance.
*    pId      Information returned by the READ ID command.
*
*  Return value
*    ==0    This is a Winbond NAND flash device.
*    !=0    This is not a Winbond NAND flash device.
*
*  Additional information
*    pId has to contain at least 3 bytes.
*/
static int _WINBOND_Identify(NAND_ONFI_INST * pInst, const U8 * pId) {
  int r;
  U8  MfgId;

  FS_USE_PARA(pInst);
  r     = 1;           // Device not supported.
  MfgId = *pId;        // The first byte is the manufacturer id.
  if (MfgId == MFG_ID_WINBOND) {
    //
    // All Winbond devices are supported.
    //
    r = 0;             // This device is supported.
  }
  return r;
}

/*********************************************************************
*
*      _WINBOND_ReadApplyPara
*
*  Function description
*    Reads the ONFI parameters from a Winbond NAND flash device
*    and stores the required information to physical layer instance.
*
*  Parameters
*    pInst      Instance of the physical layer.
*    pPara      [OUT] Read ONFI parameters.
*
*  Return value
*    ==0    OK, operation succeeded.
*    !=0    An error occurred.
*/
static int _WINBOND_ReadApplyPara(NAND_ONFI_INST * pInst, NAND_ONFI_PARA * pPara) {
  int r;

  r = _ReadApplyPara(pInst, pPara);
  if (r == 0) {
    //
    // Only the type of bad block marking is different than the default.
    //
    pPara->BadBlockMarkingType = FS_NAND_BAD_BLOCK_MARKING_TYPE_FSPS;
  }
  return r;
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
*
*  Return value
*    ==0    OK, device identified
*    !=0    Could not identify device
*/
static int _IdentifyDevice(NAND_ONFI_INST * pInst) {
  const FS_NAND_ONFI_DEVICE_TYPE  * pDevice;
  const FS_NAND_ONFI_DEVICE_TYPE ** ppDevice;
  const FS_NAND_ONFI_DEVICE_LIST  * pDeviceList;
  int                               r;
  unsigned                          NumDevices;
  unsigned                          iDevice;
  U8                                abId[3];

  pDevice     = NULL;
  pDeviceList = pInst->pDeviceList;
  NumDevices  = pDeviceList->NumDevices;
  ppDevice    = pDeviceList->ppDevice;
  FS_MEMSET(abId, 0, sizeof(abId));
  _ReadId(pInst, abId, sizeof(abId));
  //
  // A value of 0xFF or 0x00 is not a valid manufacturer id and it typically indicates
  // that the device did not respond to read id command.
  //
  if ((abId[0] == 0xFFu) || (abId[0] == 0x00u)) {
    return 1;                         // Error, could not identify device.
  }
  for (iDevice = 0; iDevice < NumDevices; ++iDevice) {
    pDevice = *ppDevice;
    if (pDevice->pfIdentify == NULL) {
      break;                          // OK, device found.
    }
    r = pDevice->pfIdentify(pInst, abId);
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
*      _DeviceMacronix
*/
static const FS_NAND_ONFI_DEVICE_TYPE _DeviceMacronix = {
  _MACRONIX_Identify,
  _MACRONIX_ReadApplyPara,
  _WaitForEndOfOperation,
  NULL,
  _GetECCResult,
  _ReadFromPage,
  _WriteToPage
};

/*********************************************************************
*
*      _DeviceMicron
*/
static const FS_NAND_ONFI_DEVICE_TYPE _DeviceMicron = {
  _MICRON_Identify,
  _MICRON_ReadApplyPara,
  _WaitForEndOfOperation,
  _CopyPage,
  _GetECCResult,
  _ReadFromPage,
  _WriteToPage
};

/*********************************************************************
*
*      _DeviceSkyHigh
*/
static const FS_NAND_ONFI_DEVICE_TYPE _DeviceSkyHigh = {
  _SKYHIGH_Identify,
  _SKYHIGH_ReadApplyPara,
  _WaitForEndOfOperation,
  NULL,
  NULL,
  _ReadFromPage,
  _WriteToPage
};

/*********************************************************************
*
*      _DeviceSkyHighHW_ECC
*/
static const FS_NAND_ONFI_DEVICE_TYPE _DeviceSkyHighHW_ECC = {
  _SKYHIGH_IdentifyHW_ECC,
  _SKYHIGH_ReadApplyPara,
  _SKYHIGH_WaitForEndOfRead,
  _CopyPage,
  _SKYHIGH_GetECCResult,
  _ReadFromPage,
  _WriteToPage
};

/*********************************************************************
*
*      _DeviceGigaDevice
*/
static const FS_NAND_ONFI_DEVICE_TYPE _DeviceGigaDevice = {
  _GIGADEVICE_Identify,
  _GIGADEVICE_ReadApplyPara,
  _WaitForEndOfOperation,
  NULL,
  NULL,
  _GIGADEVICE_ReadFromPage,
  _GIGADEVICE_WriteToPage
};

/*********************************************************************
*
*      _DeviceWinbond
*/
static const FS_NAND_ONFI_DEVICE_TYPE _DeviceWinbond = {
  _WINBOND_Identify,
  _WINBOND_ReadApplyPara,
  _WaitForEndOfOperation,
  NULL,
  NULL,
  _ReadFromPage,
  _WriteToPage
};

/*********************************************************************
*
*      _DeviceDefault
*/
static const FS_NAND_ONFI_DEVICE_TYPE _DeviceDefault = {
  NULL,
  _ReadApplyPara,
  _WaitForEndOfOperation,
  NULL,
  NULL,
  _ReadFromPage,
  _WriteToPage
};

/*********************************************************************
*
*      _apDeviceAll
*
*  Description
*    List of all supported device types.
*
*  Additional information
*    The order of the entries is relevant especially for ShyHigh.
*/
static const FS_NAND_ONFI_DEVICE_TYPE * _apDeviceAll[] = {
  &_DeviceMacronix,
  &_DeviceMicron,
  &_DeviceSkyHighHW_ECC,
  &_DeviceSkyHigh,
  &_DeviceGigaDevice,
  &_DeviceWinbond,
  &_DeviceDefault
};

/*********************************************************************
*
*       _apDeviceDefault
*/
static const FS_NAND_ONFI_DEVICE_TYPE * _apDeviceDefault[] = {
  &_DeviceMacronix,
  &_DeviceMicron,
  &_DeviceDefault
};

/*********************************************************************
*
*       _apDeviceMacronix
*/
static const FS_NAND_ONFI_DEVICE_TYPE * _apDeviceMacronix[] = {
  &_DeviceMacronix
};

/*********************************************************************
*
*       _apDeviceMicron
*/
static const FS_NAND_ONFI_DEVICE_TYPE * _apDeviceMicron[] = {
  &_DeviceMicron
};

/*********************************************************************
*
*       _apDeviceSkyHigh
*/
static const FS_NAND_ONFI_DEVICE_TYPE * _apDeviceSkyHigh[] = {
  &_DeviceSkyHighHW_ECC,
  &_DeviceSkyHigh,
};

/*********************************************************************
*
*       _apDeviceGigaDevice
*/
static const FS_NAND_ONFI_DEVICE_TYPE * _apDeviceGigaDevice[] = {
  &_DeviceGigaDevice
};

/*********************************************************************
*
*       _apDeviceWinbond
*/
static const FS_NAND_ONFI_DEVICE_TYPE * _apDeviceWinbond[] = {
  &_DeviceWinbond
};

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
*    Reads data from a NAND page.
*
*  Parameters
*    Unit         Index of the NAND physical layer instance (0-based)
*    PageIndex    Index of the NAND page to read from (0-based).
*    pData        [OUT] Data read from NAND page.
*    Off          Byte offset to read from.
*    NumBytes     Number of bytes to be read.
*
*  Return value
*    ==0      OK, data successfully read.
*    !=0      An error has occurred.
*
*  Additional information
*    This code is identical for main memory and spare area; the spare area
*    is located right after the main area.
*/
static int _PHY_Read(U8 Unit, U32 PageIndex, void * pData, unsigned Off, unsigned NumBytes) {
  NAND_ONFI_INST * pInst;
  int              r;

  r = 1;                      // Set to indicate error.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    r = pInst->pDevice->pfReadFromPage(pInst, PageIndex, pData, Off, NumBytes, NULL, 0, 0);
  }
  return r;
}

/*********************************************************************
*
*       _PHY_ReadEx
*
*  Function description
*    Reads data from two different locations of a NAND page.
*
*  Parameters
*    Unit         Index of the NAND physical layer instance (0-based)
*    PageIndex    Index of the NAND page to read from (0-based).
*    pData0       [OUT] Data read from Off0 of the NAND page.
*    Off0         Byte offset to read from for pData0.
*    NumBytes0    Number of bytes to be read.
*    pData1       [OUT] Data read from Off1 of a NAND page.
*    Off1         Byte offset to read from for pData1.
*    NumBytes1    Number of bytes to be read from Off1.
*
*  Return value
*    ==0      OK, data successfully read.
*    !=0      An error occurred.
*
*  Additional information
*    Typically used to read data and spare area at once.
*/
static int _PHY_ReadEx(U8 Unit, U32 PageIndex, void * pData0, unsigned Off0, unsigned NumBytes0, void * pData1, unsigned Off1, unsigned NumBytes1) {
  NAND_ONFI_INST * pInst;
  int              r;

  r = 1;                      // Set to indicate error.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    r = pInst->pDevice->pfReadFromPage(pInst, PageIndex, pData0, Off0, NumBytes0, pData1, Off1, NumBytes1);
  }
  return r;
}

/*********************************************************************
*
*       _PHY_Write
*
*  Function description
*    Writes data to a NAND page.
*
*  Parameters
*    Unit         Index of the NAND physical layer instance (0-based)
*    PageIndex    Index of the NAND page to write to (0-based).
*    pData        [IN] Data to be written to the NAND page.
*    Off          Byte offset to write to.
*    NumBytes     Number of bytes to be written.
*
*  Return value
*    ==0      OK, data successfully written.
*    !=0      An error occurred.
*
*  Additional information
*    This code is identical for main memory and spare area; the spare area
*    is located right after the main area.
*/
static int _PHY_Write(U8 Unit, U32 PageIndex, const void * pData, unsigned Off, unsigned NumBytes) {
  NAND_ONFI_INST * pInst;
  int              r;

  r = 1;                      // Set to indicate error.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    r = pInst->pDevice->pfWriteToPage(pInst, PageIndex, pData, Off, NumBytes, NULL, 0, 0);
  }
  return r;
}

/*********************************************************************
*
*       _PHY_WriteRO
*
*  Function description
*    Stub for the write operation. Returns an error to indicate
*    that the operation is not supported.
*
*  Return value
*    !=0      An error occurred.
*/
static int _PHY_WriteRO(U8 Unit, U32 PageIndex, const void * pData, unsigned Off, unsigned NumBytes) {
  FS_USE_PARA(Unit);
  FS_USE_PARA(PageIndex);
  FS_USE_PARA(pData);
  FS_USE_PARA(Off);
  FS_USE_PARA(NumBytes);
  return 1;
}

/*********************************************************************
*
*       _PHY_WriteEx
*
*  Function description
*    Writes data to two different locations of a NAND page.
*
*  Parameters
*    Unit         Index of the NAND physical layer instance (0-based)
*    PageIndex    Index of the NAND page to write to (0-based).
*    pData0       [IN] Data to be written to the NAND page at Off0.
*    Off0         Byte offset to write to for pData0.
*    NumBytes0    Number of bytes to be written at Off0.
*    pData1       [IN] Data to be written to the NAND page at Off1.
*    Off1         Byte offset to write to for pData1.
*    NumBytes1    Number of bytes to be written at Off1.
*
*  Return value
*    ==0      OK, data successfully written.
*    !=0      An error has occurred.
*
*  Additional information
*    Typically used to write data and spare area at the same time.
*/
static int _PHY_WriteEx(U8 Unit, U32 PageIndex, const void * pData0, unsigned Off0, unsigned NumBytes0, const void * pData1, unsigned Off1, unsigned NumBytes1) {
  NAND_ONFI_INST * pInst;
  int              r;

  r = 1;                      // Set to indicate error.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    r = pInst->pDevice->pfWriteToPage(pInst, PageIndex, pData0, Off0, NumBytes0, pData1, Off1, NumBytes1);
  }
  return r;
}

/*********************************************************************
*
*       _PHY_WriteExRO
*
*  Function description
*    Stub for the write operation. Returns an error to indicate
*    that the operation is not supported.
*
*  Return value
*    !=0      An error has occurred.
*/
static int _PHY_WriteExRO(U8 Unit, U32 PageIndex, const void * pData, unsigned Off, unsigned NumBytes, const void * pSpare, unsigned OffSpare, unsigned NumBytesSpare) {
  FS_USE_PARA(Unit);
  FS_USE_PARA(PageIndex);
  FS_USE_PARA(pData);
  FS_USE_PARA(Off);
  FS_USE_PARA(NumBytes);
  FS_USE_PARA(pSpare);
  FS_USE_PARA(OffSpare);
  FS_USE_PARA(NumBytesSpare);
  return 1;
}

/*********************************************************************
*
*       _PHY_EraseBlock
*
*  Function description
*    Sets all the bytes in a block to 0xFF
*
*  Return value
*    ==0      Block successfully erased.
*    !=0      An error has occurred.
*/
static int _PHY_EraseBlock(U8 Unit, U32 PageIndex) {
  NAND_ONFI_INST * pInst;
  int              r;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;                 // Invalid parameter.
  }
  _EnableCE(pInst);
  _WriteCmd(pInst, CMD_ERASE_1);
  _WriteAddrRow(pInst, PageIndex, pInst->NumBytesRowAddr);
  _WriteCmd(pInst, CMD_ERASE_2);
  r = _WaitForEndOfOperation(pInst);
  _DisableCE(pInst);
  if (r != 0) {
    _Reset(pInst);
  }
  return r;
}

/*********************************************************************
*
*       _PHY_EraseBlockRO
*
*  Function description
*    Stub for the erase operation. Returns an error to indicate
*    that the operation is not supported.
*
*  Return value
*    !=0      An error has occurred.
*/
static int _PHY_EraseBlockRO(U8 Unit, U32 PageIndex) {
  FS_USE_PARA(Unit);
  FS_USE_PARA(PageIndex);
  return 1;
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
*   ==0     O.K., device can be handled.
*   ==1     Error: device can not be handled.
*
*  Notes
*     (1) The first command to be issued after power-on is RESET (see [2])
*/
static int _PHY_InitGetDeviceInfo(U8 Unit, FS_NAND_DEVICE_INFO * pDevInfo) {
  int                     r;
  NAND_ONFI_PARA          Para;
  const FS_NAND_HW_TYPE * pHWType;
  unsigned                ldNumDies;
  unsigned                ldBlocksPerDie;
  NAND_ONFI_INST        * pInst;
  int                     IsONFISupported;
  int                     Result;

  r = 1;
  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    ASSERT_HW_TYPE_IS_SET(pInst);
    //
    // Initialize hardware and reset the device
    //
    _Init_x8(pInst);
    _Reset(pInst);                  // Note 1
    pHWType = pInst->pHWType;
    _EnableCE(pInst);
    IsONFISupported = FS__NAND_IsONFISupported(Unit, pHWType);
    _DisableCE(pInst);
    if (IsONFISupported != 0) {
      _Reset(pInst);
      Result = _IdentifyDevice(pInst);
      if (Result == 0) {
        FS_MEMSET(&Para, 0, sizeof(Para));
        Result = pInst->pDevice->pfReadApplyPara(pInst, &Para);
        if (Result == 0) {
          ldBlocksPerDie = pInst->ldBlocksPerDie;
          ldNumDies      = pInst->ldNumDies;
          //
          // Fill in the info required by the NAND driver.
          //
          pDevInfo->BPP_Shift           = pInst->ldBytesPerPage;
          pDevInfo->PPB_Shift           = pInst->ldPagesPerBlock;
          pDevInfo->NumBlocks           = (U16)(1uL << (ldBlocksPerDie + ldNumDies));
          FS_MEMCPY(&pDevInfo->ECC_Info, &Para.ECCInfo, sizeof(pDevInfo->ECC_Info));
          pDevInfo->BytesPerSpareArea   = pInst->BytesPerSpareArea;
          pDevInfo->DataBusWidth        = pInst->DataBusWidth;
          pDevInfo->BadBlockMarkingType = Para.BadBlockMarkingType;
          //
          // If required, initialize the HW to work in 16-bit bus mode.
          //
          if (pInst->DataBusWidth == 16u) {
            _Init_x16(pInst);
          }
          r = 0;
        }
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _PHY_IsWP
*
*  Function description
*    Checks if the device is write protected.
*
*  Return value
*    ==0      Not write protected.
*    !=0      Write protected.
*
*  Additional information
*    This is done by reading bit 7 of the status register.
*    Typical reason for write protection is that either the supply voltage is too low
*    or the /WP-pin is active (low)
*/
static int _PHY_IsWP(U8 Unit) {
  U8               Status;
  NAND_ONFI_INST * pInst;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 0;                 // Invalid parameter. We assume that the NAND flash device is not write protected.
  }
  _EnableCE(pInst);
  Status = _ReadStatus(pInst);
  _DisableCE(pInst);
  if ((Status & STATUS_WRITE_PROTECTED) != 0u) {
    return 0;
  }
  return 1;
}

/*********************************************************************
*
*       _PHY_EnableECC
*
*  Function description
*    Activates the internal ECC engine of NAND flash.
*
*  Return value
*    ==0      Internal HW ECC activated.
*    !=0      An error occurred.
*/
static int _PHY_EnableECC(U8 Unit) {
  int              r;
  NAND_ONFI_INST * pInst;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;                                 // Invalid parameter.
  }
  r = 0;
  if (_IsECCEnabledPerm(pInst) != 0u) {
    ASSERT_IS_ECC_ENABLED(pInst);
    _AllowPageCopy(pInst, 1);                 // Internal copy operation is allowed when the internal ECC is enabled.
  } else {
    r = _EnableECC(pInst);
    if (r == 0) {
      ASSERT_IS_ECC_ENABLED(pInst);
      _AllowPageCopy(pInst, 1);               // Internal copy operation is allowed when the internal ECC is enabled.
    } else {
      _Reset(pInst);
    }
  }
  return r;
}

/*********************************************************************
*
*       _PHY_DisableECC
*
*  Function description
*    Deactivates the internal ECC engine of NAND flash.
*
*  Return value
*    ==0      Internal HW ECC deactivated.
*    !=0      An error occurred.
*/
static int _PHY_DisableECC(U8 Unit) {
  int              r;
  NAND_ONFI_INST * pInst;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;                                 // Invalid parameter.
  }
  r = 0;
  if (_IsECCEnabledPerm(pInst) != 0u) {
    ASSERT_IS_ECC_ENABLED(pInst);
    _AllowPageCopy(pInst, 1);                 // Internal copy operation is allowed when the internal ECC is enabled.
  } else {
    r = _DisableECC(pInst);
    if (r == 0) {
      ASSERT_IS_ECC_DISABLED(pInst);
      _AllowPageCopy(pInst, 0);               // Internal copy operation is not allowed when the internal ECC is disabled.
    } else {
      _Reset(pInst);
    }
  }
  return r;
}

/*********************************************************************
*
*       _PHY_CopyPage
*
*  Function description
*    Copies the contents of a page without reading the data to host
*    and then writing it back.
*
*  Return value
*    ==0      OK, page contents copied.
*    !=0      An error occurred or feature not supported.
*/
static int _PHY_CopyPage(U8 Unit, U32 PageIndexSrc, U32 PageIndexDest) {
  NAND_ONFI_INST * pInst;
  int              r;

  r = 1;                              // Set to indicate error.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    if (pInst->pDevice->pfCopyPage != NULL) {
      r = pInst->pDevice->pfCopyPage(pInst, PageIndexSrc, PageIndexDest);
    }
  }
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
*    ==0      OK, status returned.
*    !=0      An error occurred.
*/
static int _PHY_GetECCResult(U8 Unit, FS_NAND_ECC_RESULT * pResult) {
  NAND_ONFI_INST * pInst;
  int              r;

  r = 1;                        // Set to indicate error.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    if (pInst->pDevice->pfGetECCResult != NULL) {
      pInst->pDevice->pfGetECCResult(pInst, pResult);
    } else {
      pResult->CorrectionStatus    = FS_NAND_CORR_NOT_APPLIED;
      pResult->MaxNumBitsCorrected = 0;
    }
    r = 0;
  }
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
  NAND_ONFI_INST * pInst;

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
*       _PHY_SetRawMode
*
*  Function description
*    Enables or disables the data translation.
*
*  Parameters
*    Unit         Index of the NAND physical layer instance (0-based)
*    OnOff        Activation status of the feature.
*
*  Return value
*    ==0  OK, status changed.
*    !=0  An error occurred or operation not supported.
*/
static int _PHY_SetRawMode(U8 Unit, U8 OnOff) {
  NAND_ONFI_INST * pInst;
  int              r;

  r = 1;                        // Set to indicate error.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    pInst->IsRawMode = OnOff;
    r = 0;
  }
  return r;
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
*       FS__NAND_ONFI_ReadONFIPara
*
*  Function description
*    Reads the ONFI parameters from NAND flash device.
*
*  Parameters
*    Unit     Index of the physical layer.
*    pPara    [OUT] Device parameters. The buffer has to be at least 256 bytes large.
*
*  Return value
*    ==0      OK, ONFI parameters read.
*    !=0      An error occurred.
*
*  Notes
*    (1) According to [2] a target command can be executed only if
*        the R/B signal is high.
*/
int FS__NAND_ONFI_ReadONFIPara(U8 Unit, void * pPara) {
  int              r;
  U16              crcRead;
  U16              crcCalc;
  int              iParaPage;
  U8             * pData8;
  NAND_ONFI_INST * pInst;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;
  }
  _Reset(pInst);
  _EnableCE(pInst);
  (void)_WaitWhileBusy(pInst, 0);       // Note 1
  _WriteCmd(pInst, CMD_READ_PARA_PAGE);
  _WriteAddrByte(pInst, 0);
  r = _WaitForEndOfOperation(pInst);
  if (r == 0) {
    _WriteCmd(pInst, CMD_READ_1);     // Switch back to read mode. _WaitForEndOfOperation() function changed it to status mode.
    //
    // Several identical parameter pages are stored in a device.
    // Read the information from the first valid parameter page.
    //
    for (iParaPage = 0; iParaPage < NUM_PARA_PAGES; ++iParaPage) {
      _ReadData(pInst, pPara, PARA_PAGE_SIZE, 8);
      pData8 = SEGGER_PTR2PTR(U8, pPara);
      //
      // Check the signature.
      //
      if ((*(pData8 + 0) == (U8)'O') &&
          (*(pData8 + 1) == (U8)'N') &&
          (*(pData8 + 2) == (U8)'F') &&
          (*(pData8 + 3) == (U8)'I')) {
        //
        // Calculate the CRC.
        //
        crcCalc = FS_CRC16_CalcBitByBit(pData8, PARA_PAGE_SIZE - 2u, PARA_CRC_INIT, PARA_CRC_POLY);
        //
        // Verify the CRC.
        //
        pData8 += (PARA_PAGE_SIZE - 2u);
        crcRead = FS_LoadU16LE(pData8);
        if (crcCalc == crcRead) {
          r = 0;
          break;
        }
      }
    }
  }
  _DisableCE(pInst);
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
*       FS_NAND_PHY_ONFI
*
*  Description
*    NAND physical layer for ONFI-compliant NAND flash devices.
*/
const FS_NAND_PHY_TYPE FS_NAND_PHY_ONFI = {
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
  _PHY_SetRawMode
};

/*********************************************************************
*
*       FS_NAND_PHY_ONFI_RO
*
*  Description
*    NAND physical layer for ONFI-compliant NAND flash devices (read-only version).
*
*  Additional information
*    This NAND physical layer supports the same NAND flash devices
*    as FS_NAND_PHY_ONFI. In comparison FS_NAND_PHY_ONFI it is only
*    able only able to read the data stored on the NAND flash device
*    but not to modify it.
*/
const FS_NAND_PHY_TYPE FS_NAND_PHY_ONFI_RO = {
  _PHY_EraseBlockRO,
  _PHY_InitGetDeviceInfo,
  _PHY_IsWP,
  _PHY_Read,
  _PHY_ReadEx,
  _PHY_WriteRO,
  _PHY_WriteExRO,
  _PHY_EnableECC,
  _PHY_DisableECC,
  NULL,
  NULL,
  _PHY_GetECCResult,
  _PHY_DeInit,
  NULL
};

/*********************************************************************
*
*       FS_NAND_PHY_ONFI_Small
*
*  Description
*    NAND physical layer for ONFI-compliant NAND flash devices (version with minimal ROM usage).
*
*  Additional information
*    This physical layer provides the smallest ROM usage in comparison
*    to FS_NAND_PHY_ONFI and FS_NAND_PHY_ONFI_RO. FS_NAND_PHY_ONFI_Small
*    supports the same NAND flash devices as FS_NAND_PHY_ONFI and
*    FS_NAND_PHY_ONFI_RO but it does not provide support for the NAND
*    internal page copy operation and for the reading the ECC correction
*    result. FS_NAND_PHY_ONFI_Small provides read as well as write access
*    to NAND flash device.
*/
const FS_NAND_PHY_TYPE FS_NAND_PHY_ONFI_Small = {
  _PHY_EraseBlock,
  _PHY_InitGetDeviceInfo,
  _PHY_IsWP,
  _PHY_Read,
  _PHY_ReadEx,
  NULL,
  _PHY_WriteEx,
  _PHY_EnableECC,
  _PHY_DisableECC,
  NULL,
  NULL,
  NULL,
  _PHY_DeInit,
  NULL
};

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_NAND_ONFI_SetHWType
*
*  Function description
*    Configures the hardware access routines for a NAND physical layer
*    of type FS_NAND_PHY_ONFI.
*
*  Parameters
*    Unit       Index of the physical layer instance (0-based)
*    pHWType    Type of the hardware layer to use. Cannot be NULL.
*
*  Additional information
*    This function is mandatory and has to be called once in FS_X_AddDevices()
*    for every instance of a NAND physical layer of type FS_NAND_PHY_ONFI.
*/
void FS_NAND_ONFI_SetHWType(U8 Unit, const FS_NAND_HW_TYPE * pHWType) {
  NAND_ONFI_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->pHWType = pHWType;
  }
}

/*********************************************************************
*
*       FS_NAND_ONFI_SetDeviceList
*
*  Function description
*    Specifies the list of ONFI NAND flash devices that require special handling.
*
*  Parameters
*    Unit         Index of the physical layer (0-based)
*    pDeviceList  [IN] List of ONFI NAND flash devices.
*
*  Additional information
*    NAND flash devices that do not require special handling such
*    as devices without HW ECC are always enabled. The special handling
*    is required for example to determine if the HW ECC of the NAND flash
*    device can be enabled and disabled at runtime.
*
*    By default, only special handling for NAND flash devices from
*    Micron and Macronix is enabled (FS_NAND_ONFI_DeviceListDefault).
*    The correct operation of NAND flash device from a manufacturer
*    not included in the configured list of devices is not guaranteed
*    if the the NAND flash device requires special handling.
*
*    Permitted values for the pDeviceList parameter are:
*    +------------------------------------+--------------------------------------------------------------------------+
*    | Identifier                         | Description                                                              |
*    +------------------------------------+--------------------------------------------------------------------------+
*    | FS_NAND_ONFI_DeviceListAll         | Enables special handling for all supported NAND flash devices.           |
*    +------------------------------------+--------------------------------------------------------------------------+
*    | FS_NAND_ONFI_DeviceListDefault     | Enables special handling of NAND flash devices from Micron and Macronix. |
*    +------------------------------------+--------------------------------------------------------------------------+
*    | FS_NAND_ONFI_DeviceListMacronix    | Enables special handling of Macronix parallel NAND flash devices.        |
*    +------------------------------------+--------------------------------------------------------------------------+
*    | FS_NAND_ONFI_DeviceListMicron      | Enables special handling of Micron parallel NAND flash devices.          |
*    +------------------------------------+--------------------------------------------------------------------------+
*    | FS_NAND_ONFI_DeviceListSkyHigh     | Enables special handling of SkyHigh parallel NAND flash devices.         |
*    +------------------------------------+--------------------------------------------------------------------------+
*    | FS_NAND_ONFI_DeviceListGigaDevice  | Enables special handling of GigaDevice parallel NAND flash devices.      |
*    +------------------------------------+--------------------------------------------------------------------------+
*    | FS_NAND_ONFI_DeviceListWinbond     | Enables special handling of Winbond parallel NAND flash devices.         |
*    +------------------------------------+--------------------------------------------------------------------------+
*/
void FS_NAND_ONFI_SetDeviceList(U8 Unit, const FS_NAND_ONFI_DEVICE_LIST * pDeviceList) {
  NAND_ONFI_INST * pInst;

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
*       FS_NAND_ONFI_DeviceListAll
*/
const FS_NAND_ONFI_DEVICE_LIST FS_NAND_ONFI_DeviceListAll = {
  (U8)SEGGER_COUNTOF(_apDeviceAll),
  _apDeviceAll
};

/*********************************************************************
*
*       FS_NAND_ONFI_DeviceListDefault
*/
const FS_NAND_ONFI_DEVICE_LIST FS_NAND_ONFI_DeviceListDefault = {
  (U8)SEGGER_COUNTOF(_apDeviceDefault),
  _apDeviceDefault
};

/*********************************************************************
*
*       FS_NAND_ONFI_DeviceListMacronix
*/
const FS_NAND_ONFI_DEVICE_LIST FS_NAND_ONFI_DeviceListMacronix = {
  (U8)SEGGER_COUNTOF(_apDeviceMacronix),
  _apDeviceMacronix
};

/*********************************************************************
*
*       FS_NAND_ONFI_DeviceListMicron
*/
const FS_NAND_ONFI_DEVICE_LIST FS_NAND_ONFI_DeviceListMicron = {
  (U8)SEGGER_COUNTOF(_apDeviceMicron),
  _apDeviceMicron
};

/*********************************************************************
*
*       FS_NAND_ONFI_DeviceListSkyHigh
*/
const FS_NAND_ONFI_DEVICE_LIST FS_NAND_ONFI_DeviceListSkyHigh = {
  (U8)SEGGER_COUNTOF(_apDeviceSkyHigh),
  _apDeviceSkyHigh
};

/*********************************************************************
*
*       FS_NAND_ONFI_DeviceListGigaDevice
*/
const FS_NAND_ONFI_DEVICE_LIST FS_NAND_ONFI_DeviceListGigaDevice = {
  (U8)SEGGER_COUNTOF(_apDeviceGigaDevice),
  _apDeviceGigaDevice
};

/*********************************************************************
*
*       FS_NAND_ONFI_DeviceListWinbond
*/
const FS_NAND_ONFI_DEVICE_LIST FS_NAND_ONFI_DeviceListWinbond = {
  (U8)SEGGER_COUNTOF(_apDeviceWinbond),
  _apDeviceWinbond
};

/*************************** End of file ****************************/
