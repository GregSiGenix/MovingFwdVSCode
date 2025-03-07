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
File        : FS_NAND_PHY_2048x8.c
Purpose     : Large page NAND flashes physical 8-bit access
Literature  : [1] \\fileserver\techinfo\Company\Samsung\NAND_Flash\Device\K9K8G08U0A_2KPageSLC_R11.pdf
              [2] \\fileserver\techinfo\Company\Micron\NAND\MT29F2G0_8AAD_16AAD_08ABD_16ABD.pdf
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
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
*       Operation status
*/
#define STATUS_ERROR                0x01u     // 0:Pass,    1:Fail
#define STATUS_READY                0x40u     // 0:Busy,    1:Ready
#define STATUS_WRITE_PROTECTED      0x80u     // 0:Protect, 1:Not Protect

/*********************************************************************
*
*       NAND commands
*/
#define CMD_READ_1                  0x00      // Start read access. Followed by 2-byte Col, 3 byte Row, then 0x30
#define CMD_READ_RANDOM_0           0x05      // Modifies ColAddr. Followed by 2-byte ColAddr and 0xe0
#define CMD_PROGRAM                 0x10
#define CMD_WRITE_TWO_PLANE_1       0x11
#define CMD_READ_2                  0x30
#define CMD_READ_COPY               0x35      // Toshiba only.
#define CMD_ERASE_1                 0x60
#define CMD_READ_TWO_PLANE          0x60
#define CMD_READ_STATUS             0x70
#define CMD_READ_ECC_STATUS         0x7A      // Toshiba only.
#define CMD_WRITE_1                 0x80
#define CMD_WRITE_TWO_PLANE_2       0x81
#define CMD_WRITE_RANDOM            0x85      // Modifies ColAddr. Followed by 2-byte ColAddr, then data
#define CMD_READ_ID                 0x90
#define CMD_ERASE_2                 0xD0
#define CMD_READ_RANDOM_1           0xE0
#define CMD_RESET                   0xFF

/*********************************************************************
*
*       Read cache
*/
#if FS_NAND_SUPPORT_READ_CACHE
  #define PAGE_INDEX_INVALID        0xFFFFFFFFu
  #define CACHE_STATUS_DEFAULT      0u       // By default the caching is enabled
  #define CACHE_STATUS_ENABLED      1u
  #define CACHE_STATUS_DISABLED     2u
#endif // FS_NAND_SUPPORT_READ_CACHE

/*********************************************************************
*
*       ECC status (Toshiba)
*/
#define BIT_ERRORS_NOT_CORR         0x0Fu
#define NUM_BIT_ERRORS_MASK         0x0Fu

/*********************************************************************
*
*       Manufacturer id
*/
#define MFG_ID_TOSHIBA              0x98u
#define MFG_ID_ISSI                 0xC8u
#define MFG_ID_SAMSUNG              0xECu

/*********************************************************************
*
*       Misc. defines
*/
#define PPB_SHIFT                   6         // Number of pages per block as power of 2 exponent.
#define BPP_SHIFT                   11u       // Number of bytes in a physical page as power of 2 exponent.
#define PPD_SHIFT                   1u        // Number of planes in the device as power of 2 exponent.
#define PPO_SHIFT                   1         // Number of operations performed in parallel as power of 2 exponent.

/*********************************************************************
*
*       ASSERT_UNIT_NO_IS_IN_RANGE
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)                                            \
    if ((Unit) >= (U8)FS_NAND_NUM_UNITS) {                                            \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NAND_PHY_2048x8: Invalid unit number.")); \
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);                                            \
    }
#else
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)
#endif

/*********************************************************************
*
*       ASSERT_HW_TYPE_IS_SET
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_HW_TYPE_IS_SET(pInst)                                             \
    if ((pInst)->pHWType == NULL) {                                                \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NAND_PHY_2048x8: HW layer not set.")); \
      FS_X_PANIC(FS_ERRCODE_HW_LAYER_NOT_SET);                                     \
    }
#else
  #define ASSERT_HW_TYPE_IS_SET(pInst)
#endif

/*********************************************************************
*
*       SET_CACHE_PAGE_INDEX
*/
#if FS_NAND_SUPPORT_READ_CACHE
  #define SET_CACHE_PAGE_INDEX(pInst, PageIndex)  _SetCachePageIndex(pInst, PageIndex)
#else
  #define SET_CACHE_PAGE_INDEX(pInst, PageIndex)
#endif

/*********************************************************************
*
*       Local data types
*
**********************************************************************
*/

/*********************************************************************
*
*       NAND_2048X8_PARA
*
*  Description
*    Parameters of the NAND flash device.
*/
typedef struct {
  FS_NAND_ECC_INFO ECCInfo;
  U16              NumBlocks;
  U8               BadBlockMarkingType;       // Specifies how the device marks a block as defective.
} NAND_2048X8_PARA;

/*********************************************************************
*
*       NAND_2048X8_INST
*
*  Description
*    Driver instance.
*
*  Additional information
*    NumBitErrorsCorrectable is used for determining if an uncorrectable
*    bit error occurred. This is required for the handling of the
*    Samsung K9F1G08U0F NAND flash device that is not reporting such
*    errors via the status register. Instead, we have to read the
*    number of bit errors corrected and to compare it with
*    NumBitErrorsCorrectable. If the number of bit errors corrected
*    is greater than NumBitErrorsCorrectable then we can assume that
*    an uncorrectable bit error occurred.
*/
typedef struct {
  const FS_NAND_HW_TYPE            * pHWType;                   // HW access functions.
  const FS_NAND_2048X8_DEVICE_TYPE * pDevice;                   // Device-specific processing functions.
  const FS_NAND_2048X8_DEVICE_LIST * pDeviceList;               // List of supported devices.
#if FS_NAND_SUPPORT_READ_CACHE
  U32                                CachePageIndex;            // Index of the last page read from NAND flash.
  U8                                 CacheStatus;               // Indicates whether the caching is enabled or not. See CACHE_STATUS_...
#endif // FS_NAND_SUPPORT_READ_CACHE
  U8                                 Unit;                      // Index of the phy. layer instance (0-based)
  U8                                 NumECCBlocks;              // Number of ECC blocks in a page. Set to 0 if the NAND flash does not have HW ECC.
  U8                                 NumBitErrorsCorrectable;   // Number of bit errors the HW ECC is able to correct.
  U8                                 ldNumPlanes;               // Number of memory planes in the device (as power of 2 exponent)
} NAND_2048X8_INST;

/*********************************************************************
*
*       FS_NAND_2048X8_DEVICE_TYPE
*
*  Description
*    Device-specific API functions.
*
*  Additional information
*    pfGetECCResult() and pfCopyPage() are optional and can be set to NULL.
*/
struct FS_NAND_2048X8_DEVICE_TYPE {
  //
  //lint -esym(9058, FS_NAND_2048X8_DEVICE_TYPE)        // MISRA deviation N:999
  //  We cannot define this structure in FS.h because all the functions take a pointer
  //  to the instance of the physical layer that is a structure which is only visible in this module.
  //
  int (*pfIdentify)        (      NAND_2048X8_INST * pInst, const U8 * pId);
  int (*pfReadApplyPara)   (      NAND_2048X8_INST * pInst, const U8 * pId, NAND_2048X8_PARA * pPara);
  int (*pfWaitForEndOfRead)(const NAND_2048X8_INST * pInst);
  int (*pfCopyPage)        (      NAND_2048X8_INST * pInst, U32 PageIndexSrc, U32 PageIndexDest);
  int (*pfGetECCResult)    (const NAND_2048X8_INST * pInst, FS_NAND_ECC_RESULT * pResult);
};

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static NAND_2048X8_INST * _apInst[FS_NAND_NUM_UNITS];

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/
//lint -efunc(818, _Reset) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory] N:104. Rationale: cached page index has to be updated to driver instance.

/*********************************************************************
*
*       _Init_x8
*/
static void _Init_x8(const NAND_2048X8_INST * pInst) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWType->pfInit_x8(Unit);
}

/*********************************************************************
*
*       _DisableCE
*/
static void _DisableCE(const NAND_2048X8_INST * pInst) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWType->pfDisableCE(Unit);
}

/*********************************************************************
*
*       _EnableCE
*/
static void _EnableCE(const NAND_2048X8_INST * pInst) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWType->pfEnableCE(Unit);
}

/*********************************************************************
*
*       _SetAddrMode
*/
static void _SetAddrMode(const NAND_2048X8_INST * pInst) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWType->pfSetAddrMode(Unit);
}

/*********************************************************************
*
*       _SetCmdMode
*/
static void _SetCmdMode(const NAND_2048X8_INST * pInst) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWType->pfSetCmdMode(Unit);
}

/*********************************************************************
*
*       _SetDataMode
*/
static void _SetDataMode(const NAND_2048X8_INST * pInst) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWType->pfSetDataMode(Unit);
}

/*********************************************************************
*
*       _WaitWhileBusy
*/
static int _WaitWhileBusy(const NAND_2048X8_INST * pInst, unsigned us) {
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
static void _Read_x8(const NAND_2048X8_INST * pInst, void * pBuffer, unsigned NumBytes) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWType->pfRead_x8(Unit, pBuffer, NumBytes);
}

/*********************************************************************
*
*       _Write_x8
*/
static void _Write_x8(const NAND_2048X8_INST * pInst, const void * pBuffer, unsigned NumBytes) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWType->pfWrite_x8(Unit, pBuffer, NumBytes);
}

/*********************************************************************
*
*       _WriteCmd
*
*  Function description
*    Writes a single byte command to the NAND flash
*/
static void _WriteCmd(const NAND_2048X8_INST * pInst, U8 Cmd) {
  _SetCmdMode(pInst);
  _Write_x8(pInst, &Cmd, 1);
}

/*********************************************************************
*
*       _WriteAddrRow
*
*  Function description
*    Writes the row address into the NAND flash.
*
*  Parameters
*    pInst      Driver instance.
*    RowAddr    Zero-based page index.
*
*  Notes
*    (1) RowAddr
*        This is the zero based page index.
*        A block consists of 64 pages, so that BlockIndex = RowAddr / 64.
*/
static void _WriteAddrRow(const NAND_2048X8_INST * pInst, unsigned RowAddr) {
  U8 aAddr[3];

  _SetAddrMode(pInst);
  FS_StoreU24LE(&aAddr[0], RowAddr);
  _Write_x8(pInst, aAddr, sizeof(aAddr));
}

/*********************************************************************
*
*       _WriteAddrColRow
*
*  Function description
*    Writes the column and row address into the NAND flash.
*
*  Parameters
*    pInst      Driver instance.
*    ColAddr    Byte-offset within a page.
*    RowAddr    Zero-based page index.
*/
static void _WriteAddrColRow(const NAND_2048X8_INST * pInst, unsigned ColAddr, unsigned RowAddr) {
  U8 aAddr[5];

  _SetAddrMode(pInst);
  FS_StoreU16LE(&aAddr[0], ColAddr);
  FS_StoreU24LE(&aAddr[2], RowAddr);
  _Write_x8(pInst, aAddr, sizeof(aAddr));
}

/*********************************************************************
*
*       _WriteAddrCol
*
*  Function description
*    Writes the column into the NAND flash.
*
*  Parameters
*    pInst      Driver instance.
*    ColAddr    Byte-offset within the selected page.
*/
static void _WriteAddrCol(const NAND_2048X8_INST * pInst, unsigned ColAddr) {
  U8 aAddr[2];

  _SetAddrMode(pInst);
  FS_StoreU16LE(&aAddr[0], ColAddr);
  _Write_x8(pInst, aAddr, sizeof(aAddr));
}

/*********************************************************************
*
*       _ReadData
*
*  Function description
*    Transfers data from device to host CPU.
*/
static void _ReadData(const NAND_2048X8_INST * pInst, void * pData, unsigned NumBytes) {
  _SetDataMode(pInst);
  _Read_x8(pInst, pData, NumBytes);
}

/*********************************************************************
*
*       _ReadDataDummy
*
*  Function description
*    Transfers data from device to host CPU and discards it.
*/
static void _ReadDataDummy(const NAND_2048X8_INST * pInst, unsigned NumBytes) {
  U32      aData[32 / 4];
  unsigned NumBytesAtOnce;

  _SetDataMode(pInst);
  if (NumBytes != 0u) {
    do {
      NumBytesAtOnce = SEGGER_MIN(NumBytes, sizeof(aData));
      _Read_x8(pInst, aData, NumBytesAtOnce);
      NumBytes -= NumBytesAtOnce;
    } while (NumBytes != 0u);
  }
}

/*********************************************************************
*
*       _WriteData
*
*  Function description
*    Transfers data from host CPU to device.
*/
static void _WriteData(const NAND_2048X8_INST * pInst, const void * pData, unsigned NumBytes) {
  _SetDataMode(pInst);
  _Write_x8(pInst, pData, NumBytes);
}

/*********************************************************************
*
*       _WriteDataDummy
*
*  Function description
*    Transfers constant data from host CPU to device.
*/
static void _WriteDataDummy(const NAND_2048X8_INST * pInst, unsigned NumBytes) {
  U32      aData[32 / 4];
  unsigned NumBytesAtOnce;

  _SetDataMode(pInst);
  if (NumBytes != 0u) {
    FS_MEMSET(aData, 0xFF, sizeof(aData));
    do {
      NumBytesAtOnce = SEGGER_MIN(NumBytes, sizeof(aData));
      _Write_x8(pInst, aData, NumBytesAtOnce);
      NumBytes -= NumBytesAtOnce;
    } while (NumBytes != 0u);
  }
}

/*********************************************************************
*
*       _WriteAddrByte
*
*  Function description
*    Writes the byte address of the parameter to read from.
*/
static void _WriteAddrByte(const NAND_2048X8_INST * pInst, U8 ByteAddr) {
  _SetAddrMode(pInst);
  _Write_x8(pInst, &ByteAddr, sizeof(ByteAddr));
}

/*********************************************************************
*
*       _ReadStatus
*
*  Function description
*    Reads and returns the contents of the status register.
*/
static U8 _ReadStatus(const NAND_2048X8_INST * pInst) {
  U8 r;

  _WriteCmd(pInst, CMD_READ_STATUS);
  _ReadData(pInst, &r, 1);
  return r;
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
static int _IsSamePlane(const NAND_2048X8_INST * pInst, U32 PageIndex1, U32 PageIndex2) {
  U32      Mask;
  unsigned ldPagesPerBlock;
  unsigned ldNumPlanes;

  ldNumPlanes     = pInst->ldNumPlanes;
  ldPagesPerBlock = PPB_SHIFT;
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

#if FS_NAND_SUPPORT_READ_CACHE

/*********************************************************************
*
*       _SetCachePageIndex
*/
static void _SetCachePageIndex(NAND_2048X8_INST * pInst, U32 PageIndex) {
  pInst->CachePageIndex = PageIndex;
}

#endif // FS_NAND_SUPPORT_READ_CACHE

/*********************************************************************
*
*       _TryReadFromCache
*/
static int _TryReadFromCache(const NAND_2048X8_INST * pInst, U32 PageIndex, void * pBuffer0, unsigned Off0, unsigned NumBytes0, void * pBuffer1, unsigned Off1, unsigned NumBytes1) {
  int r;

  r = 1;          // Page not in cache.
#if FS_NAND_SUPPORT_READ_CACHE
  {
    U32 CachePageIndex;
    U8  CacheStatus;

    if (NumBytes0 != 0u) {
      CacheStatus = pInst->CacheStatus;
      if ((CacheStatus == CACHE_STATUS_DEFAULT) ||
          (CacheStatus == CACHE_STATUS_ENABLED)) {
        //
        // Get the number of the last page read and check if
        // it is stored in the internal register of NAND flash.
        //
        CachePageIndex = pInst->CachePageIndex;
        if (PageIndex == CachePageIndex) {
          //
          // Put the NAND flash in read mode.
          //
          _EnableCE(pInst);
          _WriteCmd(pInst, CMD_READ_1);
          //
          // Set the byte address in the internal register of NAND flash to read from.
          //
          _WriteCmd(pInst, CMD_READ_RANDOM_0);
          _WriteAddrCol(pInst, Off0);
          _WriteCmd(pInst, CMD_READ_RANDOM_1);
          //
          // Copy data from internal register of NAND flash to host.
          //
          _ReadData(pInst, pBuffer0, NumBytes0);
          //
          // Copy second data area (typically the spare area) from internal register of NAND flash to host.
          //
          if (NumBytes1 != 0u) {
            _WriteCmd(pInst, CMD_READ_RANDOM_0);
            _WriteAddrCol(pInst, Off1);
            _WriteCmd(pInst, CMD_READ_RANDOM_1);
            _ReadData(pInst, pBuffer1, NumBytes1);
          }
          _DisableCE(pInst);
          r = 0;      // OK, page read from internal register of NAND flash.
        }
      }
    }
  }
#else
  FS_USE_PARA(pInst);
  FS_USE_PARA(PageIndex);
  FS_USE_PARA(pBuffer0);
  FS_USE_PARA(Off0);
  FS_USE_PARA(NumBytes0);
  FS_USE_PARA(pBuffer1);
  FS_USE_PARA(Off1);
  FS_USE_PARA(NumBytes1);
#endif
  return r;
}

/*********************************************************************
*
*       _Reset
*
*  Function description
*    Resets the NAND flash by command
*/
static void _Reset(NAND_2048X8_INST * pInst) {
  U8 Status;

  SET_CACHE_PAGE_INDEX(pInst, PAGE_INDEX_INVALID);
  _EnableCE(pInst);
  _WriteCmd(pInst, CMD_RESET);
  do {
    Status = _ReadStatus(pInst);
  } while ((Status & STATUS_READY) == 0u);
  _DisableCE(pInst);
}

/*********************************************************************
*
*       _WaitBusy
*
*  Function description
*    Waits until the NAND device has completed an operation
*
*  Return value
*    ==0    Success
*    !=0    An error occurred
*/
static int _WaitBusy(const NAND_2048X8_INST * pInst) {
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
    return 1;                       // Error, operation failed
  }
  return 0;                         // OK, operation completed
}

/*********************************************************************
*
*       _EndOperation
*
*  Function description
*    Checks status register to find out if operation was successful and disables CE.
*
*  Return value
*    ==0    Operation completed successfully
*    !=0    An error has occurred
*/
static int _EndOperation(const NAND_2048X8_INST * pInst) {
  U8  Status;
  int r;

  r      = 0;               // Set to indicate success.
  Status = _ReadStatus(pInst);
  if ((Status & (STATUS_ERROR | STATUS_READY)) != STATUS_READY) {
    r = 1;                  // Error, NAND flash device reports an error.
  }
  return r;
}

/*********************************************************************
*
*       _WaitEndOperation
*
*  Function description
*    Waits until the current operation is completed (Checking busy)
*    and ends operation, disabling CE.
*
*  Return value
*    ==0    Operation ended successfully
*    ==1    An error occurred
*/
static int _WaitEndOperation(const NAND_2048X8_INST * pInst) {
  int r;

  r = _WaitBusy(pInst);
  if (r != 0) {
    return 1;
  }
  r = _EndOperation(pInst);
  return r;
}

/*********************************************************************
*
*       _HasHW_ECC
*
*  Function description
*    Checks if the NAND flash has HW ECC.
*/
static U8 _HasHW_ECC(const NAND_2048X8_INST * pInst) {
  if (pInst->NumECCBlocks != 0u) {
    return 1;
  }
  return 0;
}

/*********************************************************************
*
*       _ReadId
*
*  Function description
*    Reads the id string from NAND flash device.
*/
static void _ReadId(const NAND_2048X8_INST * pInst, U8 * pData, unsigned NumBytes) {
  _EnableCE(pInst);
  _WriteCmd(pInst, CMD_READ_ID);
  _WriteAddrByte(pInst, 0);
  _ReadData(pInst, pData, NumBytes);
  _DisableCE(pInst);
}

/*********************************************************************
*
*       _CalcPageIndex
*/
static U32 _CalcPageIndex(U32 PageIndex) {
  U32 PageIndexBlock;

  PageIndexBlock   = PageIndex &  ((1uL << PPB_SHIFT) - 1u);
  PageIndex       &= ~((1uL << PPB_SHIFT) - 1u);
  PageIndex      <<= PPD_SHIFT;
  PageIndex       |= PageIndexBlock;
  return PageIndex;
}

/*********************************************************************
*
*       _ReadPageTP
*
*  Function description
*    Reads data from the NAND flash device.
*
*  Additional information
*    This function reads two physical pages at a time.
*/
static int _ReadPageTP(const NAND_2048X8_INST * pInst, U32 PageIndex, U8 * pData, unsigned OffData, unsigned NumBytesData, U8 * pSpare, unsigned OffSpare, unsigned NumBytesSpare) {
  U32        PlaneMask;
  U32        OnePlane;
  U32        PageIndexPlane;
  unsigned   OffPlane;
  unsigned   NumBytesAtOnce;
  unsigned   BytesPerPageData;
  unsigned   BytesPerPageSpare;
  unsigned   BytesPerVPageData;
  unsigned   OffDataRead;
  U8       * pDataRead;
  unsigned   NumBytesDataRead;
  unsigned   OffSpareRead;
  U8       * pSpareRead;
  unsigned   NumBytesSpareRead;
  int        r;

  PlaneMask         = 1uL << PPB_SHIFT;
  OnePlane          = 1uL << PPB_SHIFT;
  BytesPerPageData  = 1uL << BPP_SHIFT;
  BytesPerPageSpare = BytesPerPageData >> 5;      // The size spare area is 1/32 of the page size.
  //
  // Adjust the page number according to the size of the virtual page.
  //
  PageIndex = _CalcPageIndex(PageIndex);
  //
  // Make the offset relative to the begin of spare area.
  //
  BytesPerVPageData = BytesPerPageData << PPD_SHIFT;
  if (OffSpare < BytesPerVPageData) {
    OffSpare = 0;
  } else {
    OffSpare -= BytesPerVPageData;
  }
  //
  // Set the page index for the first plane.
  //
  PageIndexPlane = PageIndex & ~PlaneMask;
  _WriteCmd(pInst, CMD_READ_TWO_PLANE);
  _WriteAddrRow(pInst, PageIndexPlane);
  //
  // Set the page index for the second plane.
  //
  PageIndexPlane += OnePlane;
  _WriteCmd(pInst, CMD_READ_TWO_PLANE);
  _WriteAddrRow(pInst, PageIndexPlane);
  //
  // Read data to internal registers for both planes at once
  // and wait for the operation to finish.
  //
  _WriteCmd(pInst, CMD_READ_2);
  r = _WaitBusy(pInst);
  //
  // Read data from the NAND flash device and copy it to specified buffers.
  //
  PageIndexPlane = PageIndex & ~PlaneMask;
  for (;;) {
    //
    // Determine the offset and the number of bytes to be read from the data area of NAND flash.
    //
    OffDataRead      = 0;
    pDataRead        = NULL;
    NumBytesDataRead = 0;
    if (pData != NULL) {
      if (NumBytesData != 0u) {
        if (OffData < BytesPerPageData) {
          NumBytesAtOnce    = BytesPerPageData - OffData;
          NumBytesAtOnce    = SEGGER_MIN(NumBytesAtOnce, NumBytesData);
          pDataRead         = pData;
          OffDataRead       = OffData;
          NumBytesDataRead  = NumBytesAtOnce;
          NumBytesData     -= NumBytesAtOnce;
          pData            += NumBytesAtOnce;
          OffData           = 0;
        } else {
          OffData          -= BytesPerPageData;
        }
      }
    }
    //
    // Determine the offset and the number of bytes to be read from the spare area of NAND flash.
    //
    OffSpareRead      = 0;
    pSpareRead        = NULL;
    NumBytesSpareRead = 0;
    if (pSpare != NULL) {
      if (NumBytesSpare != 0u) {
        if (OffSpare < BytesPerPageSpare) {
          NumBytesAtOnce     = BytesPerPageSpare - OffSpare;
          NumBytesAtOnce     = SEGGER_MIN(NumBytesAtOnce, NumBytesSpare);
          OffSpareRead       = OffSpare + BytesPerPageData;
          pSpareRead         = pSpare;
          NumBytesSpareRead  = NumBytesAtOnce;
          NumBytesSpare     -= NumBytesAtOnce;
          pSpare            += NumBytesAtOnce;
          OffSpare           = 0;
        } else {
          OffSpare          -= BytesPerPageSpare;
        }
      }
    }
    //
    // Set the page index and the byte offset for the plane to read from.
    //
    if (pDataRead != NULL) {
      OffPlane = OffDataRead;
    } else {
      OffPlane = OffSpareRead;
    }
    _WriteCmd(pInst, CMD_READ_1);
    _WriteAddrColRow(pInst, 0, PageIndexPlane);
    _WriteCmd(pInst, CMD_READ_RANDOM_0);
    _WriteAddrCol(pInst, OffPlane);
    _WriteCmd(pInst, CMD_READ_RANDOM_1);
    //
    // Read the data from the first to buffers.
    //
    if (pDataRead != NULL) {
      _ReadData(pInst, pDataRead, NumBytesDataRead);
    }
    if (pSpareRead != NULL) {
      if (pDataRead != NULL) {
        if ((OffDataRead + NumBytesDataRead) < OffSpareRead) {
          _ReadDataDummy(pInst, OffSpareRead - (OffDataRead + NumBytesDataRead));
        }
      }
      _ReadData(pInst, pSpareRead, NumBytesSpareRead);
    }
    if ((NumBytesData == 0u) && (NumBytesSpare == 0u)) {
      break;
    }
    PageIndexPlane += OnePlane;       // Calculate the address of the next plane.
  }
  return r;
}

/*********************************************************************
*
*       _WritePageTP
*
*  Function description
*    Writes data to the NAND flash device.
*
*  Additional information
*    This function writes two physical pages at a time.
*    The total number of bytes to be written has to be equal to the
*    size of a virtual page (i.e. two physical pages).
*/
static int _WritePageTP(const NAND_2048X8_INST * pInst, U32 PageIndex, const U8 * pData, unsigned OffData, unsigned NumBytesData, const U8 * pSpare, unsigned OffSpare, unsigned NumBytesSpare) {
  U32        PlaneMask;
  U32        OnePlane;
  U32        PageIndexPlane;
  unsigned   OffPlane;
  unsigned   NumBytesAtOnce;
  unsigned   BytesPerPageData;
  unsigned   BytesPerPageSpare;
  unsigned   BytesPerVPageData;
  unsigned   OffDataWrite;
  const U8 * pDataWrite;
  unsigned   NumBytesDataWrite;
  unsigned   OffSpareWrite;
  const U8 * pSpareWrite;
  unsigned   NumBytesSpareWrite;
  int        r;
  U8         CmdWrite1;
  U8         CmdWrite2;

  PlaneMask         = 1uL << PPB_SHIFT;
  OnePlane          = 1uL << PPB_SHIFT;
  BytesPerPageData  = 1uL << BPP_SHIFT;
  BytesPerPageSpare = BytesPerPageData >> 5;      // The size spare area is 1/32 of the page size.
  //
  // Adjust the page number according to the size of the virtual page.
  //
  PageIndex = _CalcPageIndex(PageIndex);
  //
  // Make the offset relative to the begin of spare area.
  //
  BytesPerVPageData = BytesPerPageData << PPD_SHIFT;
  if (OffSpare < BytesPerVPageData) {
    OffSpare = 0;
  } else {
    OffSpare -= BytesPerVPageData;
  }
  //
  // Read data from the NAND flash device and copy it to specified buffers.
  //
  PageIndexPlane = PageIndex & ~PlaneMask;
  for (;;) {
    //
    // Determine the offset and the number of bytes to be written to the data area of NAND flash.
    //
    OffDataWrite      = 0;
    pDataWrite        = NULL;
    NumBytesDataWrite = 0;
    if (pData != NULL) {
      if (NumBytesData != 0u) {
        if (OffData < BytesPerPageData) {
          NumBytesAtOnce     = BytesPerPageData - OffData;
          NumBytesAtOnce     = SEGGER_MIN(NumBytesAtOnce, NumBytesData);
          pDataWrite         = pData;
          OffDataWrite       = OffData;
          NumBytesDataWrite  = NumBytesAtOnce;
          NumBytesData      -= NumBytesAtOnce;
          pData             += NumBytesAtOnce;
          OffData            = 0;
        } else {
          OffData           -= BytesPerPageData;
        }
      }
    }
    //
    // Determine the offset and the number of bytes to be written to the spare area of NAND flash.
    //
    OffSpareWrite      = 0;
    pSpareWrite        = NULL;
    NumBytesSpareWrite = 0;
    if (pSpare != NULL) {
      if (NumBytesSpare != 0u) {
        if (OffSpare < BytesPerPageSpare) {
          NumBytesAtOnce      = BytesPerPageSpare - OffSpare;
          NumBytesAtOnce      = SEGGER_MIN(NumBytesAtOnce, NumBytesSpare);
          OffSpareWrite       = OffSpare + BytesPerPageData;
          pSpareWrite         = pSpare;
          NumBytesSpareWrite  = NumBytesAtOnce;
          NumBytesSpare      -= NumBytesAtOnce;
          pSpare             += NumBytesAtOnce;
          OffSpare            = 0;
        } else {
          OffSpare           -= BytesPerPageSpare;
        }
      }
    }
    //
    // Set the page index and the byte offset for the plane to write to.
    //
    if (pDataWrite != NULL) {
      OffPlane = OffDataWrite;
    } else {
      OffPlane = OffSpareWrite;
    }
    CmdWrite1 = CMD_WRITE_1;
    CmdWrite2 = CMD_WRITE_TWO_PLANE_1;
    if ((NumBytesData == 0u) && (NumBytesSpare == 0u)) {
      CmdWrite1 = CMD_WRITE_TWO_PLANE_2;
      CmdWrite2 = CMD_PROGRAM;
    }
    _WriteCmd(pInst, CmdWrite1);
    _WriteAddrColRow(pInst, OffPlane, PageIndexPlane);
    //
    // Read the data from the first to buffers.
    //
    if (pDataWrite != NULL) {
      _WriteData(pInst, pDataWrite, NumBytesDataWrite);
    }
    if (pSpareWrite != NULL) {
      if (pDataWrite != NULL) {
        if ((OffDataWrite + NumBytesDataWrite) < OffSpareWrite) {
          _WriteDataDummy(pInst, OffSpareWrite - (OffDataWrite + NumBytesDataWrite));
        }
      }
      _WriteData(pInst, pSpareWrite, NumBytesSpareWrite);
    }
    _WriteCmd(pInst, CmdWrite2);
    //
    // Wait for the data to be written.
    //
    r = _WaitBusy(pInst);
    if (r != 0) {
      break;                          // Error, could not write data.
    }
    if ((NumBytesData == 0u) && (NumBytesSpare == 0u)) {
      break;
    }
    PageIndexPlane += OnePlane;       // Calculate the address of the next plane.
  }
  return r;
}

/*********************************************************************
*
*      _AllocInstIfRequired
*
*  Function description
*    Allocates memory for the instance of a physical layer.
*/
static NAND_2048X8_INST * _AllocInstIfRequired(U8 Unit) {
  NAND_2048X8_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  pInst = NULL;                     // Set to indicate an error.
  if (Unit < (U8)FS_NAND_NUM_UNITS) {
    pInst = _apInst[Unit];
    if (pInst == NULL) {
      pInst = SEGGER_PTR2PTR(NAND_2048X8_INST, FS_ALLOC_ZEROED((I32)sizeof(NAND_2048X8_INST), "NAND_2048X8_INST"));         // MISRA deviation D:100d
      if (pInst != NULL) {
        pInst->Unit        = Unit;
        pInst->pDeviceList = FS_NAND_2048X8_DEVICE_LIST_DEFAULT;
        _apInst[Unit]      = pInst;
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
*
*  Parameters
*    Unit     Driver index.
*
*  Return value
*    !=NULL   Driver instance.
*    ==NULL   An error occurred.
*/
static NAND_2048X8_INST * _GetInst(U8 Unit) {
  NAND_2048X8_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  pInst = NULL;
  if (Unit < (U8)FS_NAND_NUM_UNITS) {
    pInst = _apInst[Unit];
  }
  return pInst;
}

/*********************************************************************
*
*       _GetECCResultEx
*
*  Function description
*    Returns the ECC correction status and the number of bit errors corrected.
*
*  Parameters
*    pInst      Driver instance.
*    pResult    [OUT] ECC correction result.
*
*  Return value
*    ==0    OK, data returned
*    !=0    An error occurred
*/
static int _GetECCResultEx(const NAND_2048X8_INST * pInst, FS_NAND_ECC_RESULT * pResult) {
  unsigned Status;
  unsigned MaxNumBitsCorrected;
  unsigned iECCBlock;
  unsigned NumECCBlocks;
  unsigned NumBitsCorrected;
  unsigned CorrectionStatus;

  MaxNumBitsCorrected = 0;
  CorrectionStatus    = FS_NAND_CORR_NOT_APPLIED;
  NumECCBlocks        = pInst->NumECCBlocks;
  //
  // Tell NAND flash device that we want to read the ECC status.
  //
  _WriteCmd(pInst, CMD_READ_ECC_STATUS);
  //
  // Iterate through all ECC blocks and get the number of bits corrected.
  //
  for (iECCBlock = 0; iECCBlock < NumECCBlocks; ++iECCBlock) {
    _ReadData(pInst, &Status, sizeof(Status));
    NumBitsCorrected = Status & NUM_BIT_ERRORS_MASK;
    if (NumBitsCorrected == BIT_ERRORS_NOT_CORR) {
      CorrectionStatus = FS_NAND_CORR_FAILURE;
    } else {
      if (NumBitsCorrected > 0u) {
        if (CorrectionStatus != FS_NAND_CORR_FAILURE) {
          CorrectionStatus = FS_NAND_CORR_APPLIED;
        }
        if (NumBitsCorrected > MaxNumBitsCorrected) {
          MaxNumBitsCorrected = NumBitsCorrected;
        }
      }
    }
  }
  pResult->CorrectionStatus    = (U8)CorrectionStatus;
  pResult->MaxNumBitsCorrected = (U8)MaxNumBitsCorrected;
  return 0;
}

/*********************************************************************
*
*       _CheckForUCBE
*
*  Function description
*    Checks if an uncorrectable error occurred.
*
*  Parameters
*    pInst      Driver instance.
*
*  Return value
*    ==0      Correctable or no bit error occurred.
*    !=0      An uncorrectable bit error occurred.
*/
static int _CheckForUCBE(const NAND_2048X8_INST * pInst) {
  int                r;
  int                Result;
  unsigned           NumBitErrorsCorrectable;
  FS_NAND_ECC_RESULT eccResult;

  r = 0;                                    // No uncorrectable bit error occurred.
  NumBitErrorsCorrectable = pInst->NumBitErrorsCorrectable;
  FS_MEMSET(&eccResult, 0, sizeof(eccResult));
  Result = _GetECCResultEx(pInst, &eccResult);
  if (Result == 0) {
    if (eccResult.CorrectionStatus == FS_NAND_CORR_FAILURE) {
      r = 1;                                // Uncorrectable bit error occurred.
    } else {
      if (eccResult.MaxNumBitsCorrected > NumBitErrorsCorrectable) {
        r = 1;                              // Uncorrectable bit error occurred.
      }
    }
  }
  return r;
}

/*********************************************************************
*
*      _IdentifyDevice
*
*  Function description
*    Tries to identify the NAND flash device using the manufacturer
*    and the device code.
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
static int _IdentifyDevice(NAND_2048X8_INST * pInst, U8 * pDeviceId, unsigned SizeOfDeviceId) {
  const FS_NAND_2048X8_DEVICE_TYPE  * pDevice;
  const FS_NAND_2048X8_DEVICE_TYPE ** ppDevice;
  const FS_NAND_2048X8_DEVICE_LIST  * pDeviceList;
  int                                 r;
  unsigned                            NumDevices;
  unsigned                            iDevice;

  pDevice     = NULL;
  pDeviceList = pInst->pDeviceList;
  NumDevices  = pDeviceList->NumDevices;
  ppDevice    = pDeviceList->ppDevice;
  FS_MEMSET(pDeviceId, 0, SizeOfDeviceId);
  _ReadId(pInst, pDeviceId, SizeOfDeviceId);
  //
  // A value of 0xFF or 0x00 is not a valid manufacturer id and it typically indicates
  // that the device did not respond to the READ ID command.
  //
  if ((*pDeviceId == 0xFFu) || (*pDeviceId == 0x00u)) {
    return 1;                         // Error, could not identify device.
  }
  for (iDevice = 0; iDevice < NumDevices; ++iDevice) {
    pDevice = *ppDevice;
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
*       _CopyPage
*
*  Function description
*    Copies a page without transferring the content to MCU.
*
*  Parameters
*    pInst          Driver instance.
*    PageIndexSrc   Index of the source page.
*    PageIndexDest  Index of the destination page.
*
*  Return value
*    ==0    OK, Paged copied.
*    !=0    An error occurred.
*/
static int _CopyPage(NAND_2048X8_INST * pInst, U32 PageIndexSrc, U32 PageIndexDest) {
  int r;
  int rEnd;

  r = 1;                              // Set to indicate that the copy operation was not executed.
  if (_IsSamePlane(pInst, PageIndexSrc, PageIndexDest) != 0) {
    SET_CACHE_PAGE_INDEX(pInst, PAGE_INDEX_INVALID);
    //
    // Read source page to page buffer of NAND flash.
    //
    _EnableCE(pInst);
    _WriteCmd(pInst, CMD_READ_1);
    _WriteAddrColRow(pInst, 0, PageIndexSrc);
    _WriteCmd(pInst, CMD_READ_COPY);
    r = pInst->pDevice->pfWaitForEndOfRead(pInst);
    if (r == 0) {
      //
      // Write page buffer to destination page.
      //
      _WriteCmd(pInst, CMD_WRITE_RANDOM);
      _WriteAddrColRow(pInst, 0, PageIndexDest);
      _WriteCmd(pInst, CMD_PROGRAM);
    }
    rEnd = _WaitEndOperation(pInst);
    _DisableCE(pInst);
    if (rEnd != 0) {
      r = 1;                            // Error, copy operation failed.
    }
    if (r != 0) {
      //
      // Do not reset a NAND flash with HW ECC so that the NAND driver can read later the status of the ECC correction.
      //
      if (_HasHW_ECC(pInst) == 0u) {
        _Reset(pInst);
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
*    Returns the ECC correction status and the number of bit errors corrected.
*
*  Parameters
*    pInst      Driver instance.
*    pResult    [OUT] ECC correction result.
*
*  Return value
*    ==0    OK, data returned
*    !=0    An error occurred
*
*  Additional information
*    This function performs the same operation as _GetECCResultEx().
*    In addition, it drives the CE signal of the NAND flash.
*/
static int _GetECCResult(const NAND_2048X8_INST * pInst, FS_NAND_ECC_RESULT * pResult) {
  int r;;

  _EnableCE(pInst);
  r = _GetECCResultEx(pInst, pResult);
  _DisableCE(pInst);
  return r;
}

/*********************************************************************
*
*       _Identify
*
*  Function description
*    Checks if the NAND flash device can be handled.
*
*  Parameters
*    pInst      Driver instance.
*    pId        [IN] Information read via READ ID command.
*
*  Return value
*    ==0    The NAND flash device can be handled.
*    !=0    The NAND flash device cannot be handled.
*/
static int _Identify(NAND_2048X8_INST * pInst, const U8 * pId) {
  int      r;
  unsigned DeviceCode;

  FS_USE_PARA(pInst);
  r          = 1;                   // Device not supported.
  DeviceCode = pId[1];
  if (   (DeviceCode == 0xA2u)
      || (DeviceCode == 0xF2u)
      || (DeviceCode == 0xF1u)
      || (DeviceCode == 0xA1u)
      || (DeviceCode == 0x11u)
      || (DeviceCode == 0xD1u)
      || (DeviceCode == 0xAAu)
      || (DeviceCode == 0xDAu)
      || (DeviceCode == 0xACu)
      || (DeviceCode == 0xDCu)
      || (DeviceCode == 0xA3u)
      || (DeviceCode == 0xD3u)) {
    r = 0;                          // OK, device supported.
  }
  return r;
}

/*********************************************************************
*
*       _ReadApplyPara
*
*  Function description
*    Calculates the device parameters.
*
*  Parameters
*    pInst      Driver instance.
*    pId        [IN] Information read via READ ID command.
*    pPara      [OUT] Device parameters.
*
*  Return value
*    ==0    OK, device parameters calculated.
*    !=0    An error occurred.
*/
static int _ReadApplyPara(NAND_2048X8_INST * pInst, const U8 * pId, NAND_2048X8_PARA * pPara) {
  int      r;
  unsigned DeviceCode;
  unsigned NumBlocks;

  FS_USE_PARA(pInst);
  r = 0;                            // Set to indicate success.
  DeviceCode = pId[1];
  switch (DeviceCode)   {
  case 0xA2:
  case 0xF2:
    NumBlocks = 512;
    break;
  case 0xF1:
  case 0xA1:
  case 0x11:
  case 0xD1:
    NumBlocks = 1024;
    break;
  case 0xAA:
  case 0xDA:
    NumBlocks = 2048;
    break;
  case 0xAC:
  case 0xDC:
    NumBlocks = 4096;
    break;
  case 0xA3:
  case 0xD3:
    NumBlocks = 8192;
    break;
  default:
    NumBlocks = 0;
    r = 1;                          // Error, unknown device.
    break;
  }
  if (r == 0) {
    pPara->NumBlocks                   = (U16)NumBlocks;
    pPara->BadBlockMarkingType         = FS_NAND_BAD_BLOCK_MARKING_TYPE_FSPS;
    pPara->ECCInfo.HasHW_ECC           = 0;
    pPara->ECCInfo.IsHW_ECCEnabledPerm = 0;
    pPara->ECCInfo.NumBitsCorrectable  = 1;
    pPara->ECCInfo.ldBytesPerBlock     = 9;
    pInst->ldNumPlanes                 = 0;     // Typically, a NAND flash device has only 1 plane.
  }
  return r;
}

/*********************************************************************
*
*       _TOSHIBA_Identify
*
*  Function description
*    Checks if the Toshiba NAND flash device can be handled.
*
*  Parameters
*    pInst      Driver instance.
*    pId        [IN] Information read via READ ID command.
*
*  Return value
*    ==0    The NAND flash device can be handled.
*    !=0    The NAND flash device cannot be handled.
*/
static int _TOSHIBA_Identify(NAND_2048X8_INST * pInst, const U8 * pId) {
  int      r;
  unsigned MfgId;
  unsigned DeviceCode;

  FS_USE_PARA(pInst);
  r          = 1;                   // Device not supported.
  MfgId      = pId[0];              // The first byte is the manufacturer id.
  DeviceCode = pId[1];
  if (MfgId == MFG_ID_TOSHIBA) {
    if (   (DeviceCode == 0xF1u)    // TBD: Check also that pId[4] == 0xF2 to make sure that this is a Toshiba TC58BVG0S3HTAI0 device.
        || (DeviceCode == 0xDAu)) {
      r = 0;                        // This device is supported.
    }
  }
  return r;
}

/*********************************************************************
*
*       _TOSHIBA_ReadApplyPara
*
*  Function description
*    Calculates the device parameters.
*
*  Parameters
*    pInst      Driver instance.
*    pId        [IN] Information read via READ ID command.
*    pPara      [OUT] Device parameters.
*
*  Return value
*    ==0    OK, device parameters calculated.
*    !=0    An error occurred.
*/
static int _TOSHIBA_ReadApplyPara(NAND_2048X8_INST * pInst, const U8 * pId, NAND_2048X8_PARA * pPara) {
  unsigned NumBlocks;
  unsigned DeviceCode;
  unsigned ldNumPlanes;

  ldNumPlanes = 0;              // Typically, a NAND flash device has only 1 plane.
  DeviceCode  = pId[1];
  if (DeviceCode == 0xDAu) {
    NumBlocks   = 2048;
    ldNumPlanes = 1;            // This device has two planes.
  } else {
    NumBlocks = 1024;
  }
  pPara->NumBlocks                   = (U16)NumBlocks;
  pPara->BadBlockMarkingType         = FS_NAND_BAD_BLOCK_MARKING_TYPE_FPS;
  pPara->ECCInfo.NumBitsCorrectable  = 8;
  pPara->ECCInfo.ldBytesPerBlock     = 9;
  pPara->ECCInfo.HasHW_ECC           = 1;
  pPara->ECCInfo.IsHW_ECCEnabledPerm = 1;
  pInst->NumECCBlocks                = 1u << (BPP_SHIFT - pPara->ECCInfo.ldBytesPerBlock);
  pInst->NumBitErrorsCorrectable     = pPara->ECCInfo.NumBitsCorrectable;
  pInst->ldNumPlanes                 = (U8)ldNumPlanes;
  return 0;
}

/*********************************************************************
*
*       _SAMSUNG_Identify
*
*  Function description
*    Checks if the Samsung NAND flash device can be handled.
*
*  Parameters
*    pInst      Driver instance.
*    pId        [IN] Information read via READ ID command.
*
*  Return value
*    ==0    The NAND flash device can be handled.
*    !=0    The NAND flash device cannot be handled.
*/
static int _SAMSUNG_Identify(NAND_2048X8_INST * pInst, const U8 * pId) {
  int      r;
  unsigned MfgId;
  unsigned DeviceCode;
  unsigned PlaneInfo;

  FS_USE_PARA(pInst);
  r          = 1;                   // Device not supported.
  MfgId      = pId[0];              // The first byte is the manufacturer id.
  DeviceCode = pId[1];
  PlaneInfo  = pId[4];
  if (MfgId == MFG_ID_SAMSUNG) {
    if (DeviceCode == 0xF1u) {
      if (PlaneInfo == 0x42u) {     // Samsung K9F1G08U0F
        r = 0;                      // This device is supported.
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _SAMSUNG_ReadApplyPara
*
*  Function description
*    Calculates the device parameters.
*
*  Parameters
*    pInst      Driver instance.
*    pId        [IN] Information read via READ ID command.
*    pPara      [OUT] Device parameters.
*
*  Return value
*    ==0    OK, device parameters calculated.
*    !=0    An error occurred.
*/
static int _SAMSUNG_ReadApplyPara(NAND_2048X8_INST * pInst, const U8 * pId, NAND_2048X8_PARA * pPara) {
  FS_USE_PARA(pId);

  pPara->NumBlocks                   = 1024;
  pPara->BadBlockMarkingType         = FS_NAND_BAD_BLOCK_MARKING_TYPE_FSPS;
  pPara->ECCInfo.NumBitsCorrectable  = 4;
  pPara->ECCInfo.ldBytesPerBlock     = 9;
  pPara->ECCInfo.HasHW_ECC           = 1;
  pPara->ECCInfo.IsHW_ECCEnabledPerm = 1;
  pInst->NumECCBlocks                = 1u << (BPP_SHIFT - pPara->ECCInfo.ldBytesPerBlock);
  pInst->NumBitErrorsCorrectable     = pPara->ECCInfo.NumBitsCorrectable;
  return 0;
}

/*********************************************************************
*
*       _SAMSUNG_WaitForEndOfRead
*
*  Function description
*    Waits for the read operation to complete.
*
*  Parameters
*    pInst      Driver instance.
*
*  Return value
*    ==0    OK, data read successfully.
*    !=0    An error occurred.
*
*  Additional information
*    This function performs the same operation as _WaitBusy()
*    with the exception that it checks the number of bit errors
*    in order to determine if an uncorrectable bit error occurred.
*    We have to do this because the Samsung device does not report
*    uncorrectable bit errors via the status register as
*    NAND flash devices from other manufacturers do.
*/
static int _SAMSUNG_WaitForEndOfRead(const NAND_2048X8_INST * pInst) {
  unsigned Status;
  int      r;

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
    return 1;                       // Error, operation failed
  }
  r = _CheckForUCBE(pInst);         // Check for uncorrectable bit errors.
  return r;
}


/*********************************************************************
*
*       _ISSI_Identify
*
*  Function description
*    Checks if the ISSI NAND flash device can be handled.
*
*  Parameters
*    pInst      Driver instance.
*    pId        [IN] Information read via READ ID command.
*
*  Return value
*    ==0    The NAND flash device can be handled.
*    !=0    The NAND flash device cannot be handled.
*/
static int _ISSI_Identify(NAND_2048X8_INST * pInst, const U8 * pId) {
  int      r;
  unsigned MfgId;
  unsigned DeviceCode;
  unsigned PlaneInfo;

  FS_USE_PARA(pInst);
  r          = 1;                   // Device not supported.
  MfgId      = pId[0];              // The first byte is the manufacturer id.
  DeviceCode = pId[1];
  PlaneInfo  = pId[4];
  if (MfgId == MFG_ID_ISSI) {
    if (DeviceCode == 0xD1u) {
      if (PlaneInfo == 0x40u) {     // ISSI IS34ML01G084
        r = 0;                      // This device is supported.
      }
    }
    if (DeviceCode == 0xDAu) {
      if (PlaneInfo == 0x44u) {     // ISSI IS34ML02G084
        r = 0;                      // This device is supported.
      }
    }
    if (DeviceCode == 0xDCu) {
      if (PlaneInfo == 0x54u) {     // ISSI IS34ML04G084
        r = 0;                      // This device is supported.
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _ISSI_ReadApplyPara
*
*  Function description
*    Calculates the device parameters.
*
*  Parameters
*    pInst      Driver instance.
*    pId        [IN] Information read via READ ID command.
*    pPara      [OUT] Device parameters.
*
*  Return value
*    ==0    OK, device parameters calculated.
*    !=0    An error occurred.
*/
static int _ISSI_ReadApplyPara(NAND_2048X8_INST * pInst, const U8 * pId, NAND_2048X8_PARA * pPara) {
  int      r;
  unsigned DeviceCode;
  unsigned NumBlocks;

  FS_USE_PARA(pInst);
  r = 0;                            // Set to indicate success.
  DeviceCode = pId[1];
  switch (DeviceCode)   {
  case 0xD1:
    NumBlocks = 1024;               // ISSI IS34ML01G084
    break;
  case 0xDA:
    NumBlocks = 2048;               // ISSI IS34ML02G084
    break;
  case 0xDC:
    NumBlocks = 4096;               // ISSI IS34ML04G084
    break;
  default:
    NumBlocks = 0;
    r = 1;                          // Error, unknown device.
    break;
  }
  if (r == 0) {
    pPara->NumBlocks                   = (U16)NumBlocks;
    pPara->BadBlockMarkingType         = FS_NAND_BAD_BLOCK_MARKING_TYPE_FSPS;
    pPara->ECCInfo.NumBitsCorrectable  = 4;
    pPara->ECCInfo.ldBytesPerBlock     = 9;
    pPara->ECCInfo.HasHW_ECC           = 0;
    pPara->ECCInfo.IsHW_ECCEnabledPerm = 0;
  }
  return r;
}

/*********************************************************************
*
*      _DeviceToshibaHW_ECC
*/
static const FS_NAND_2048X8_DEVICE_TYPE _DeviceToshibaHW_ECC = {
  _TOSHIBA_Identify,
  _TOSHIBA_ReadApplyPara,
  _WaitBusy,
  _CopyPage,
  _GetECCResult
};

/*********************************************************************
*
*      _DeviceSamsungHW_ECC
*/
static const FS_NAND_2048X8_DEVICE_TYPE _DeviceSamsungHW_ECC = {
  _SAMSUNG_Identify,
  _SAMSUNG_ReadApplyPara,
  _SAMSUNG_WaitForEndOfRead,
  _CopyPage,
  _GetECCResult
};

/*********************************************************************
*
*      _DeviceISSI
*/
static const FS_NAND_2048X8_DEVICE_TYPE _DeviceISSI = {
  _ISSI_Identify,
  _ISSI_ReadApplyPara,
  _WaitBusy,
  NULL,
  NULL
};

/*********************************************************************
*
*      _DeviceDefault
*/
static const FS_NAND_2048X8_DEVICE_TYPE _DeviceDefault = {
  _Identify,
  _ReadApplyPara,
  _WaitBusy,
  NULL,
  NULL
};

/*********************************************************************
*
*      _apDeviceAll
*
*  Description
*    List of all supported device types.
*/
static const FS_NAND_2048X8_DEVICE_TYPE * _apDeviceAll[] = {
  &_DeviceToshibaHW_ECC,
  &_DeviceSamsungHW_ECC,
  &_DeviceISSI,
  &_DeviceDefault
};

/*********************************************************************
*
*       _apDeviceDefault
*/
static const FS_NAND_2048X8_DEVICE_TYPE * _apDeviceDefault[] = {
  &_DeviceToshibaHW_ECC,
  &_DeviceSamsungHW_ECC,
  &_DeviceDefault
};

/*********************************************************************
*
*       _apDeviceStandard
*/
static const FS_NAND_2048X8_DEVICE_TYPE * _apDeviceStandard[] = {
  &_DeviceDefault
};

/*********************************************************************
*
*       _apDeviceToshiba
*/
static const FS_NAND_2048X8_DEVICE_TYPE * _apDeviceToshiba[] = {
  &_DeviceToshibaHW_ECC
};

/*********************************************************************
*
*       _apDeviceSamsung
*/
static const FS_NAND_2048X8_DEVICE_TYPE * _apDeviceSamsung[] = {
  &_DeviceSamsungHW_ECC
};

/*********************************************************************
*
*       _apDeviceISSI
*/
static const FS_NAND_2048X8_DEVICE_TYPE * _apDeviceISSI[] = {
  &_DeviceISSI
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
*    Reads data from a complete or a part of a page.
*
*  Return value
*    ==0    Data successfully read
*    !=0    An error occurred
*
*  Additional information
*    This code is identical for main memory and spare area; the spare area
*    is located right after the main area.
*/
static int _PHY_Read(U8 Unit, U32 PageIndex, void * pBuffer, unsigned Off, unsigned NumBytes) {
  int                r;
  NAND_2048X8_INST * pInst;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;               // Error, invalid driver instance.
  }
  r = _TryReadFromCache(pInst, PageIndex, pBuffer, Off, NumBytes, NULL, 0, 0);
  if (r != 0) {
    SET_CACHE_PAGE_INDEX(pInst, PAGE_INDEX_INVALID);
    //
    // Put the NAND flash in read mode.
    //
    _EnableCE(pInst);
    _WriteCmd(pInst, CMD_READ_1);
    //
    // Copy page data from memory array to internal register of NAND flash.
    //
    _WriteAddrColRow(pInst, Off, PageIndex);
    _WriteCmd(pInst, CMD_READ_2);
    r = pInst->pDevice->pfWaitForEndOfRead(pInst);
    if (NumBytes != 0u) {
      //
      // Restore the read mode because pfWaitForEndOfRead() changes it to status mode.
      //
      _WriteCmd(pInst, CMD_READ_1);
      //
      // Copy data from internal register of NAND flash to host.
      //
      _ReadData(pInst, pBuffer, NumBytes);
    }
    _DisableCE(pInst);
    if (r == 0) {
      SET_CACHE_PAGE_INDEX(pInst, PageIndex);
    }
  }
  if (r != 0) {
    //
    // Do not reset a NAND flash with HW ECC so that the NAND driver can read later the status of the ECC correction.
    //
    if (_HasHW_ECC(pInst) == 0u) {
      _Reset(pInst);
    }
  }
  return r;
}

/*********************************************************************
*
*       _PHY_ReadEx
*
*  Function description
*    Reads data from 2 parts of a page.
*    Typically used to read data and spare area at the same time.
*
*  Return value
*    ==0    Data successfully read
*    !=0    An error occurred
*
*  Additional information
*    The read procedure is taken from [1], Random data output in a Page, p. 30
*/
static int _PHY_ReadEx(U8 Unit, U32 PageIndex, void * pBuffer0, unsigned Off0, unsigned NumBytes0, void * pBuffer1, unsigned Off1, unsigned NumBytes1) {
  int                r;
  NAND_2048X8_INST * pInst;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;               // Error, invalid driver instance.
  }
  r = _TryReadFromCache(pInst, PageIndex, pBuffer0, Off0, NumBytes0, pBuffer1, Off1, NumBytes1);
  if (r != 0) {
    SET_CACHE_PAGE_INDEX(pInst, PAGE_INDEX_INVALID);
    //
    // Copy page data from memory array to internal register of NAND flash.
    //
    _EnableCE(pInst);
    _WriteCmd(pInst, CMD_READ_1);
    _WriteAddrColRow(pInst, Off0, PageIndex);
    _WriteCmd(pInst, CMD_READ_2);
    r = pInst->pDevice->pfWaitForEndOfRead(pInst);
    //
    // Restore the read mode because pfWaitForEndOfRead() changes it to status mode.
    //
    _WriteCmd(pInst, CMD_READ_1);
    if (NumBytes0 != 0u) {
      //
      // Copy data from internal register of NAND flash to host.
      //
      _ReadData(pInst, pBuffer0, NumBytes0);
    }
    //
    // Copy second data area (typically the spare area) from internal register of NAND flash to host.
    //
    if (NumBytes1 != 0u) {
      _WriteCmd(pInst, CMD_READ_RANDOM_0);
      _WriteAddrCol(pInst, Off1);
      _WriteCmd(pInst, CMD_READ_RANDOM_1);
      _ReadData(pInst, pBuffer1, NumBytes1);
    }
    if (r == 0) {
      SET_CACHE_PAGE_INDEX(pInst, PageIndex);
    }
    _DisableCE(pInst);
  }
  if (r != 0) {
    //
    // Do not reset a NAND flash with HW ECC so that the NAND driver can read later the status of the ECC correction.
    //
    if (_HasHW_ECC(pInst) == 0u) {
      _Reset(pInst);
    }
  }
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
*    ==0    Data successfully written
*    !=0    An error occurred
*/
static int _PHY_Write(U8 Unit, U32 PageIndex, const void * pBuffer, unsigned Off, unsigned NumBytes) {
  int                r;
  NAND_2048X8_INST * pInst;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;               // Error, invalid driver instance.
  }
  SET_CACHE_PAGE_INDEX(pInst, PAGE_INDEX_INVALID);
  //
  // Copy data area from host to internal register of NAND flash.
  //
  _EnableCE(pInst);
  _WriteCmd(pInst, CMD_WRITE_1);
  _WriteAddrColRow(pInst, Off, PageIndex);
  _WriteData(pInst, pBuffer, NumBytes);
  //
  // Write data from internal register of NAND flash to memory array.
  //
  _WriteCmd(pInst, CMD_PROGRAM);
  r = _WaitEndOperation(pInst);
  _DisableCE(pInst);
  if (r != 0) {
    _Reset(pInst);
  }
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
*    ==0    Data successfully written
*    !=0    An error occurred
*/
static int _PHY_WriteEx(U8 Unit, U32 PageIndex, const void * pBuffer0, unsigned Off0, unsigned NumBytes0, const void * pBuffer1, unsigned Off1, unsigned NumBytes1) {
  int                r;
  NAND_2048X8_INST * pInst;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;               // Error, invalid driver instance.
  }
  SET_CACHE_PAGE_INDEX(pInst, PAGE_INDEX_INVALID);
  _EnableCE(pInst);
  _WriteCmd(pInst, CMD_WRITE_1);
  //
  // Copy first data area from host to internal register of NAND flash.
  //
  _WriteAddrColRow(pInst, Off0, PageIndex);
  _WriteData(pInst, pBuffer0, NumBytes0);
  //
  // Copy second data are (typ. spare area) from host to internal register of NAND flash.
  //
  _WriteCmd(pInst, CMD_WRITE_RANDOM);
  _WriteAddrCol(pInst, Off1);
  _WriteData(pInst, pBuffer1, NumBytes1);
  //
  // Write data from internal register of NAND flash to memory array.
  //
  _WriteCmd(pInst, CMD_PROGRAM);
  r = _WaitEndOperation(pInst);
  _DisableCE(pInst);
  if (r != 0) {
    _Reset(pInst);
  }
  return r;
}

/*********************************************************************
*
*       _PHY_EraseBlock
*
*  Function description
*    Erases a block.
*
*  Parameters
*    Unit           Driver index.
*    PageIndex      Index of the first page in the block to be erased.
*                   If the device has 64 pages per block, then the following values are permitted:
*                   0   ->  block 0
*                   64  ->  block 1
*                   128 ->  block 2
*                   etc.
*
*  Return value
*    ==0    Block erased
*    !=0    An error occurred
*/
static int _PHY_EraseBlock(U8 Unit, U32 PageIndex) {
  int                r;
  NAND_2048X8_INST * pInst;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;               // Error, invalid driver instance.
  }
  SET_CACHE_PAGE_INDEX(pInst, PAGE_INDEX_INVALID);
  _EnableCE(pInst);
  _WriteCmd(pInst, CMD_ERASE_1);
  _WriteAddrRow(pInst, PageIndex);
  _WriteCmd(pInst, CMD_ERASE_2);
  r = _WaitEndOperation(pInst);
  _DisableCE(pInst);
  if (r != 0) {
    _Reset(pInst);
  }
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
*      0    O.K., device can be handled
*      1    Error, device can not be handled
*
*  Notes
*     (1) The first command after power-on must be RESET (see [2])
*/
static int _PHY_InitGetDeviceInfo(U8 Unit, FS_NAND_DEVICE_INFO * pDevInfo) {
  int                r;
  NAND_2048X8_PARA   Para;
  NAND_2048X8_INST * pInst;
  int                Result;
  U8                 abDeviceInfo[5];

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;                               // Error, could not allocate driver instance.
  }
  r = 1;                                    // Set to indicate error.
  ASSERT_HW_TYPE_IS_SET(pInst);
  _Init_x8(pInst);
  _Reset(pInst);                            // Note 1
  Result = _IdentifyDevice(pInst, abDeviceInfo, sizeof(abDeviceInfo));
  if (Result == 0) {
    FS_MEMSET(&Para, 0, sizeof(Para));
    Result = pInst->pDevice->pfReadApplyPara(pInst, abDeviceInfo, &Para);
    if (Result == 0) {
      //
      // Fill in the info required by the NAND driver.
      //
      pDevInfo->BPP_Shift           = BPP_SHIFT;
      pDevInfo->PPB_Shift           = PPB_SHIFT;
      pDevInfo->NumBlocks           = Para.NumBlocks;
      FS_MEMCPY(&pDevInfo->ECC_Info, &Para.ECCInfo, sizeof(pDevInfo->ECC_Info));
      pDevInfo->DataBusWidth        = 8;
      pDevInfo->BadBlockMarkingType = Para.BadBlockMarkingType;
      r = 0;
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
*    < 0    Error
*    ==0    Not write protected
*    > 0    Write protected
*
*  Additional information
*    This is done by reading bit 7 of the status register.
*    Typical reason for write protection is that either
*    the supply voltage is too low or the /WP-pin is active (low)
*/
static int _PHY_IsWP(U8 Unit) {
  U8                 Status;
  NAND_2048X8_INST * pInst;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;                     // Error, invalid driver instance.
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
*       _PHY_CopyPage
*
*  Function description
*    Copies a page without transferring the content to MCU.
*
*  Return value
*    ==0    OK, Paged copied.
*    !=0    An error occurred.
*/
static int _PHY_CopyPage(U8 Unit, U32 PageIndexSrc, U32 PageIndexDest) {
  NAND_2048X8_INST * pInst;
  int                r;

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
*    Returns the ECC correction status and the number of bit errors corrected.
*
*  Return value
*    ==0    OK, data returned
*    !=0    An error occurred
*/
static int _PHY_GetECCResult(U8 Unit, FS_NAND_ECC_RESULT * pResult) {
  NAND_2048X8_INST * pInst;
  int                r;

  r = 1;                        // Set to indicate error.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    pResult->CorrectionStatus    = FS_NAND_CORR_NOT_APPLIED;
    pResult->MaxNumBitsCorrected = 0;
    if (pInst->pDevice->pfGetECCResult != NULL) {
      r = pInst->pDevice->pfGetECCResult(pInst, pResult);
    }
  }
  return r;
}

/*********************************************************************
*
*       _PHY_ReadTP
*
*  Function description
*    Reads data from a complete or a part of a page.
*    This code is identical for main memory and spare area; the spare area
*    is located right after the main area.
*
*  Return value
*    ==0    Data successfully read
*    !=0    An error occurred
*/
static int _PHY_ReadTP(U8 Unit, U32 PageIndex, void * pData, unsigned Off, unsigned NumBytes) {
  int                r;
  NAND_2048X8_INST * pInst;
  unsigned           OffSpare;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;                               // Error, invalid driver instance.
  }
  _EnableCE(pInst);
  OffSpare = 1uL << (BPP_SHIFT + 1u);       // Two physical pages in one virtual page.
  if (Off < OffSpare) {
    r = _ReadPageTP(pInst, PageIndex, SEGGER_PTR2PTR(U8, pData), Off, NumBytes, NULL, 0, 0);                                                      // MISRA deviation D:100e
  } else {
    r = _ReadPageTP(pInst, PageIndex, NULL, 0, 0, SEGGER_PTR2PTR(U8, pData), Off, NumBytes);                                                      // MISRA deviation D:100e
  }
  _DisableCE(pInst);
  if (r != 0) {
    //
    // Do not reset a NAND flash with HW ECC so that the NAND driver can read later the status of the ECC correction.
    //
    if (_HasHW_ECC(pInst) == 0u) {
      _Reset(pInst);
    }
  }
  return r;
}

/*********************************************************************
*
*       _PHY_ReadExTP
*
*  Function description
*    Reads data from 2 parts of a page.
*    Typically used to read data and spare area at the same time.
*
*  Return value
*    ==0    Data successfully read
*    !=0    An error occurred
*
*  Notes
*    (1) Literature
*        Procedure taken from [1], Random data output in a Page, p. 30
*/
static int _PHY_ReadExTP(U8 Unit, U32 PageIndex, void * pData0, unsigned Off0, unsigned NumBytes0, void * pData1, unsigned Off1, unsigned NumBytes1) {
  int                r;
  NAND_2048X8_INST * pInst;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;               // Error, invalid driver instance.
  }
  _EnableCE(pInst);
  r = _ReadPageTP(pInst, PageIndex, SEGGER_PTR2PTR(U8, pData0), Off0, NumBytes0, SEGGER_PTR2PTR(U8, pData1), Off1, NumBytes1);                    // MISRA deviation D:100e
  _DisableCE(pInst);
  if (r != 0) {
    //
    // Do not reset a NAND flash with HW ECC so that the NAND driver can read later the status of the ECC correction.
    //
    if (_HasHW_ECC(pInst) == 0u) {
      _Reset(pInst);
    }
  }
  return r;
}

/*********************************************************************
*
*       _PHY_WriteTP
*
*  Function description
*    Writes data into a complete or a part of a page.
*    This code is identical for main memory and spare area; the spare area
*    is located right after the main area.
*
*  Return value
*    ==0    Data successfully written
*    !=0    An error occurred
*/
static int _PHY_WriteTP(U8 Unit, U32 PageIndex, const void * pData, unsigned Off, unsigned NumBytes) {
  int                r;
  NAND_2048X8_INST * pInst;
  unsigned           OffSpare;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;                             // Error, invalid driver instance.
  }
  SET_CACHE_PAGE_INDEX(pInst, PAGE_INDEX_INVALID);
  _EnableCE(pInst);
  OffSpare = 1uL << (BPP_SHIFT + 1u);     // Two physical pages in one virtual page.
  if (Off < OffSpare) {
    r = _WritePageTP(pInst, PageIndex, SEGGER_CONSTPTR2PTR(const U8, pData), Off, NumBytes, NULL, 0, 0);                                          // MISRA deviation D:100e
  } else {
    r = _WritePageTP(pInst, PageIndex, NULL, 0, 0, SEGGER_CONSTPTR2PTR(const U8, pData), Off, NumBytes);                                          // MISRA deviation D:100e
  }
  _DisableCE(pInst);
  if (r != 0) {
    _Reset(pInst);
  }
  return r;
}

/*********************************************************************
*
*       _PHY_WriteExTP
*
*  Function description
*    Writes data to 2 parts of a page.
*    Typically used to write data and spare area at the same time.
*
*  Return value
*    ==0    Data successfully written
*    !=0    An error occurred
*/
static int _PHY_WriteExTP(U8 Unit, U32 PageIndex, const void * pData0, unsigned Off0, unsigned NumBytes0, const void * pData1, unsigned Off1, unsigned NumBytes1) {
  int                r;
  NAND_2048X8_INST * pInst;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;               // Error, invalid driver instance.
  }
  SET_CACHE_PAGE_INDEX(pInst, PAGE_INDEX_INVALID);
  _EnableCE(pInst);
  r = _WritePageTP(pInst, PageIndex, SEGGER_CONSTPTR2PTR(const U8, pData0), Off0, NumBytes0, SEGGER_CONSTPTR2PTR(const U8, pData1), Off1, NumBytes1);       // MISRA deviation D:100e
  _DisableCE(pInst);
  if (r != 0) {
    _Reset(pInst);
  }
  return r;
}

/*********************************************************************
*
*       _PHY_EraseBlockTP
*
*  Function description
*    Erases two ore more physical blocks.
*
*  Parameters
*    Unit           Driver index (0-based).
*    PageIndex      Index of the first page in the block to be erased.
*                   If the device has 64 pages per block, then the following values are permitted:
*                   0   ->  block 0
*                   64  ->  block 1
*                   128 ->  block 2
*                   etc.
*
*  Return value
*    ==0    Blocks successfully erased.
*    !=0    An error occurred.
*
*  Additional information
*    This function supports only the two-plane mode.
*/
static int _PHY_EraseBlockTP(U8 Unit, U32 PageIndex) {
  int                r;
  U32                PhyPageIndex;
  NAND_2048X8_INST * pInst;
  U32                PlaneMask;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;                                       // Error, invalid driver instance.
  }
  PlaneMask    = 1uL << PPB_SHIFT;
  PhyPageIndex = PageIndex << PPD_SHIFT;            // Two blocks are erased at once.
  SET_CACHE_PAGE_INDEX(pInst, PAGE_INDEX_INVALID);
  _EnableCE(pInst);
  _WriteCmd(pInst, CMD_ERASE_1);
  _WriteAddrRow(pInst, PhyPageIndex & ~PlaneMask);  // Erase the block on the plane 0.
  _WriteCmd(pInst, CMD_ERASE_1);
  _WriteAddrRow(pInst, PhyPageIndex | PlaneMask);   // Erase the block on the plane 1.
  _WriteCmd(pInst, CMD_ERASE_2);
  r = _WaitEndOperation(pInst);                     // Wait for the operation to complete.
  _DisableCE(pInst);
  if (r != 0) {
    _Reset(pInst);
  }
  return r;
}

/*********************************************************************
*
*       _PHY_InitGetDeviceInfoTP
*
*  Function description
*    Initializes the physical layer (multi-plane version).
*
*  Return value
*    ==0    O.K., device can be handled
*    ==1    Error, device can not be handled
*
*  Notes
*     (1) The first command after power-on must be RESET (see [2])
*
*  Additional information
*    This function performs the following operations:
*    * initializes hardware layer
*    * resets NAND flash device
*    * tries to identify the NAND flash
*    * if the NAND flash device can be handled the information about it is returned
*/
static int _PHY_InitGetDeviceInfoTP(U8 Unit, FS_NAND_DEVICE_INFO * pDevInfo) {
  U8                 abId[5];
  U8                 DeviceCode;
  U16                NumBlocks;
  int                r;
  U8                 MfgId;
  U8                 NumECCBlocks;
  NAND_2048X8_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;                 // Error, could not allocate driver instance.
  }
  ASSERT_HW_TYPE_IS_SET(pInst);
  _Init_x8(pInst);
  _Reset(pInst);              // Note 1
  //
  // Retrieve id information from NAND device.
  //
  FS_MEMSET(abId, 0, sizeof(abId));
  _ReadId(pInst, abId, sizeof(abId));
  //
  // Identify the NAND flash device.
  //
  r            = 1;           // Set to indicate an error.
  NumECCBlocks = 0;           // No HW ECC support.
  NumBlocks    = 0;
  MfgId        = abId[0];
  DeviceCode   = abId[1];
  switch(DeviceCode)   {
  case 0xDA:
    NumBlocks = 2048;
    if (MfgId == MFG_ID_ISSI) {
      r = 0;
    }
    break;
  default:
    //
    // Error, could not identify NAND flash device.
    //
    break;
  }
  if (r == 0) {
    pDevInfo->BPP_Shift           = (U8)(BPP_SHIFT + PPD_SHIFT);  // bytes per page * number of planes
    pDevInfo->PPB_Shift           = PPB_SHIFT;
    pDevInfo->NumBlocks           = NumBlocks >> PPD_SHIFT;       // The total number of blocks is equal to the number of blocks in a plane.
    pDevInfo->ECC_Info.HasHW_ECC  = NumECCBlocks != 0u ? 1u : 0u;
    pDevInfo->DataBusWidth        = 8;
    pDevInfo->BadBlockMarkingType = (U8)FS_NAND_BAD_BLOCK_MARKING_TYPE_FPS;
    pDevInfo->PPO_Shift           = PPO_SHIFT;
    pInst->NumECCBlocks           = NumECCBlocks;
  }
  return r;
}

/*********************************************************************
*
*       Public data
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_NAND_PHY_2048x8
*
*  Description
*    NAND physical layer for parallel NAND flash devices with 8-bit
*    bus width and 2 Kbyte pages.
*/
const FS_NAND_PHY_TYPE FS_NAND_PHY_2048x8 = {
  _PHY_EraseBlock,
  _PHY_InitGetDeviceInfo,
  _PHY_IsWP,
  _PHY_Read,
  _PHY_ReadEx,
  _PHY_Write,
  _PHY_WriteEx,
  NULL,
  NULL,
  NULL,
  _PHY_CopyPage,
  _PHY_GetECCResult,
  NULL,
  NULL
};

/*********************************************************************
*
*       FS_NAND_PHY_2048x8_TwoPlane
*
*  Description
*    NAND physical layer for parallel NAND flash devices with 8-bit
*    bus width and 2 Kbyte pages using muti-plane operations.
*/
const FS_NAND_PHY_TYPE FS_NAND_PHY_2048x8_TwoPlane = {
  _PHY_EraseBlockTP,
  _PHY_InitGetDeviceInfoTP,
  _PHY_IsWP,
  _PHY_ReadTP,
  _PHY_ReadExTP,
  _PHY_WriteTP,
  _PHY_WriteExTP,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL
};

/*********************************************************************
*
*       FS_NAND_PHY_2048x8_Small
*
*  Description
*    NAND physical layer for parallel NAND flash devices with 8-bit
*    bus width and 2 Kbyte pages.
*/
const FS_NAND_PHY_TYPE FS_NAND_PHY_2048x8_Small = {
  _PHY_EraseBlock,
  _PHY_InitGetDeviceInfo,
  _PHY_IsWP,
  _PHY_Read,
  _PHY_ReadEx,
  _PHY_Write,
  _PHY_WriteEx,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
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
*       FS_NAND_2048x8_EnableReadCache
*
*  Function description
*    Activates the page read optimization.
*
*  Parameters
*    Unit       Index of the physical layer instance (0-based)
*
*  Additional information
*    This function is optional and is available only when the file system
*    is build with FS_NAND_SUPPORT_READ_CACHE set to 1 which is the default.
*    Activating the read cache can increase the overall performance of the
*    NAND driver especially when using the SLC1 NAND driver with a logical
*    sector size smaller than the page of the used NAND flash device.
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
*    via FS_NAND_2048x8_DisableReadCache().
*/
void FS_NAND_2048x8_EnableReadCache(U8 Unit) {
  NAND_2048X8_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->CacheStatus    = CACHE_STATUS_ENABLED;
    pInst->CachePageIndex = PAGE_INDEX_INVALID;
  }
}

/*********************************************************************
*
*       FS_NAND_2048x8_DisableReadCache
*
*  Function description
*    Deactivates the page read optimization.
*
*  Parameters
*    Unit       Index of the physical layer instance (0-based)
*
*  Additional information
*    This function is optional and is available only when the file system
*    is build with FS_NAND_SUPPORT_READ_CACHE set to 1 which is the default.
*    The optimization can be enabled at runtime via FS_NAND_2048x8_EnableReadCache().
*
*    Refer to FS_NAND_2048x8_EnableReadCache() for more information about how
*    the page read optimization works
*/
void FS_NAND_2048x8_DisableReadCache(U8 Unit) {
  NAND_2048X8_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->CacheStatus = CACHE_STATUS_DISABLED;
  }
}

#endif // FS_NAND_SUPPORT_READ_CACHE

/*********************************************************************
*
*       FS_NAND_2048x8_SetHWType
*
*  Function description
*    Configures the hardware access routines for a NAND physical layer
*    of type FS_NAND_PHY_2048x8.
*
*  Parameters
*    Unit       Index of the physical layer instance (0-based)
*    pHWType    Type of the hardware layer to use. Cannot be NULL.
*
*  Additional information
*    This function is mandatory and has to be called once in FS_X_AddDevices()
*    for every instance of a NAND physical layer of type FS_NAND_PHY_2048x8.
*/
void FS_NAND_2048x8_SetHWType(U8 Unit, const FS_NAND_HW_TYPE * pHWType) {
  NAND_2048X8_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->pHWType = pHWType;
  }
}

/*********************************************************************
*
*       FS_NAND_2048X8_SetDeviceList
*
*  Function description
*    Specifies the list of NAND flash devices that require special handling.
*
*  Parameters
*    Unit         Index of the physical layer (0-based)
*    pDeviceList  [IN] List of NAND flash devices.
*
*  Additional information
*    NAND flash devices that do not require special handling such
*    as devices without HW ECC are always enabled. The special handling
*    is required for example to determine if the HW ECC of the NAND flash
*    device can be enabled and disabled at runtime.
*
*    By default, only special handling for NAND flash devices from
*    Samsung and Toshiba is enabled (FS_NAND_ONFI_DeviceListDefault).
*    The correct operation of NAND flash device from a manufacturer
*    not included in the configured list of devices is not guaranteed
*    if the the NAND flash device requires special handling.
*
*    Permitted values for the pDeviceList parameter are:
*    +------------------------------------+-------------------------------------------------------------------------------------------------------------------+
*    | Identifier                         | Description                                                                                                       |
*    +------------------------------------+-------------------------------------------------------------------------------------------------------------------+
*    | FS_NAND_2048X8_DeviceListAll       | Enables the handling for all supported NAND flash devices.                                                        |
*    +------------------------------------+-------------------------------------------------------------------------------------------------------------------+
*    | FS_NAND_2048X8_DeviceListDefault   | Enables the handling of standard NAND flash devices and the special handling of Samsung and Toshiba NAND flashes. |
*    +------------------------------------+-------------------------------------------------------------------------------------------------------------------+
*    | FS_NAND_2048X8_DeviceListStandard  | Enables the handling of NAND flash devices that do not have any special features such as HW ECC.                  |
*    +------------------------------------+-------------------------------------------------------------------------------------------------------------------+
*    | FS_NAND_2048X8_DeviceListSamsung   | Enables the special handling of Samsung NAND flash devices.                                                       |
*    +------------------------------------+-------------------------------------------------------------------------------------------------------------------+
*    | FS_NAND_2048X8_DeviceListToshiba   | Enables the special handling of Toshiba NAND flash devices.                                                       |
*    +------------------------------------+-------------------------------------------------------------------------------------------------------------------+
*/
void FS_NAND_2048X8_SetDeviceList(U8 Unit, const FS_NAND_2048X8_DEVICE_LIST * pDeviceList) {
  NAND_2048X8_INST * pInst;

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
*       FS_NAND_2048X8_DeviceListAll
*/
const FS_NAND_2048X8_DEVICE_LIST FS_NAND_2048X8_DeviceListAll = {
  (U8)SEGGER_COUNTOF(_apDeviceAll),
  _apDeviceAll
};

/*********************************************************************
*
*       FS_NAND_2048X8_DeviceListDefault
*/
const FS_NAND_2048X8_DEVICE_LIST FS_NAND_2048X8_DeviceListDefault = {
  (U8)SEGGER_COUNTOF(_apDeviceDefault),
  _apDeviceDefault
};

/*********************************************************************
*
*       FS_NAND_2048X8_DeviceListStandard
*/
const FS_NAND_2048X8_DEVICE_LIST FS_NAND_2048X8_DeviceListStandard = {
  (U8)SEGGER_COUNTOF(_apDeviceStandard),
  _apDeviceStandard
};

/*********************************************************************
*
*       FS_NAND_2048X8_DeviceListSamsung
*/
const FS_NAND_2048X8_DEVICE_LIST FS_NAND_2048X8_DeviceListSamsung = {
  (U8)SEGGER_COUNTOF(_apDeviceSamsung),
  _apDeviceSamsung
};

/*********************************************************************
*
*       FS_NAND_2048X8_DeviceListToshiba
*/
const FS_NAND_2048X8_DEVICE_LIST FS_NAND_2048X8_DeviceListToshiba = {
  (U8)SEGGER_COUNTOF(_apDeviceToshiba),
  _apDeviceToshiba
};

/*********************************************************************
*
*       FS_NAND_2048X8_DeviceListISSI
*/
const FS_NAND_2048X8_DEVICE_LIST FS_NAND_2048X8_DeviceListISSI = {
  (U8)SEGGER_COUNTOF(_apDeviceISSI),
  _apDeviceISSI
};

/*************************** End of file ****************************/
