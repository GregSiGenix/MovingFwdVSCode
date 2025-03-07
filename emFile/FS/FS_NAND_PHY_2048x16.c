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
File        : FS_NAND_PHY_2048x16.c
Purpose     : Large page NAND flashes physical 16-bit access
Literature  : [1] \\fileserver\techinfo\Company\Samsung\NAND_Flash\Device\K9K8G08U0A_2KPageSLC_R11.pdf
              [2] \\Fileserver\techinfo\Company\Samsung\NAND_Flash\Device\K9F1Gxx0M_2KPageSLC_R13.pdf
              [3] \\fileserver\techinfo\Company\Micron\NAND\MT29F2G0_8AAD_16AAD_08ABD_16ABD.pdf
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
#define STATUS_ERROR            0x01u   // 0:Pass,          1:Fail
#define STATUS_READY            0x40u   // 0:Busy,          1:Ready
#define STATUS_WRITE_PROTECTED  0x80u   // 0:Protect,       1:Not Protect

/*********************************************************************
*
*       NAND commands
*/
#define NAND_CMD_WRITE_1        0x80
#define NAND_CMD_WRITE_2        0x10
#define NAND_CMD_READ_1         0x00
#define NAND_CMD_READ_2         0x30
#define NAND_CMD_RESET_CHIP     0xFF
#define NAND_CMD_ERASE_1        0x60
#define NAND_CMD_ERASE_2        0xD0
#define NAND_CMD_READ_STATUS    0x70
#define NAND_CMD_READ_ID        0x90
#define NAND_CMD_RANDOM_READ_1  0x05
#define NAND_CMD_RANDOM_READ_2  0xE0
#define NAND_CMD_RANDOM_WRITE   0x85

/*********************************************************************
*
*       Misc. defines
*/
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ALL)
  #define PAGES_PER_BLOCK       64u
#endif

/*********************************************************************
*
*       ASSERT_UNIT_NO_IS_IN_RANGE
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)                                             \
    if ((Unit) >= (U8)FS_NAND_NUM_UNITS) {                                             \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NAND_PHY_2048x16: Invalid unit number.")); \
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);                                             \
    }
#else
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)
#endif

/*********************************************************************
*
*       ASSERT_HW_TYPE_IS_SET
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_HW_TYPE_IS_SET(Unit)                                               \
    if (_aInst[Unit].pHWType == NULL) {                                             \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NAND_PHY_2048x16: HW layer not set.")); \
      FS_X_PANIC(FS_ERRCODE_HW_LAYER_NOT_SET);                                      \
    }
#else
  #define ASSERT_HW_TYPE_IS_SET(Unit)
#endif

/*********************************************************************
*
*       Local data types
*
**********************************************************************
*/

/*********************************************************************
*
*       NAND_2048X16_INST
*/
typedef struct {
  const FS_NAND_HW_TYPE * pHWType;
} NAND_2048X16_INST;

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static NAND_2048X16_INST _aInst[FS_NAND_NUM_UNITS];

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _Init_x16
*/
static void _Init_x16(U8 Unit) {
  NAND_2048X16_INST * pInst;

  pInst = &_aInst[Unit];
  pInst->pHWType->pfInit_x16(Unit);
}

/*********************************************************************
*
*       _DisableCE
*/
static void _DisableCE(U8 Unit) {
  NAND_2048X16_INST * pInst;

  pInst = &_aInst[Unit];
  pInst->pHWType->pfDisableCE(Unit);
}

/*********************************************************************
*
*       _EnableCE
*/
static void _EnableCE(U8 Unit) {
  NAND_2048X16_INST * pInst;

  pInst = &_aInst[Unit];
  pInst->pHWType->pfEnableCE(Unit);
}

/*********************************************************************
*
*       _SetAddrMode
*/
static void _SetAddrMode(U8 Unit) {
  NAND_2048X16_INST * pInst;

  pInst = &_aInst[Unit];
  pInst->pHWType->pfSetAddrMode(Unit);
}

/*********************************************************************
*
*       _SetCmdMode
*/
static void _SetCmdMode(U8 Unit) {
  NAND_2048X16_INST * pInst;

  pInst = &_aInst[Unit];
  pInst->pHWType->pfSetCmdMode(Unit);
}

/*********************************************************************
*
*       _SetDataMode
*/
static void _SetDataMode(U8 Unit) {
  NAND_2048X16_INST * pInst;

  pInst = &_aInst[Unit];
  pInst->pHWType->pfSetDataMode(Unit);
}

/*********************************************************************
*
*       _WaitWhileBusy
*/
static int _WaitWhileBusy(U8 Unit, unsigned us) {
  NAND_2048X16_INST * pInst;
  int                 r;

  pInst = &_aInst[Unit];
  r = pInst->pHWType->pfWaitWhileBusy(Unit, us);
  return r;
}

/*********************************************************************
*
*       _Read_x16
*/
static void _Read_x16(U8 Unit, void * pBuffer, unsigned NumBytes) {
  NAND_2048X16_INST * pInst;

  pInst = &_aInst[Unit];
  pInst->pHWType->pfRead_x16(Unit, pBuffer, NumBytes);
}

/*********************************************************************
*
*       _Write_x16
*/
static void _Write_x16(U8 Unit, const void * pBuffer, unsigned NumBytes) {
  NAND_2048X16_INST * pInst;

  pInst = &_aInst[Unit];
  pInst->pHWType->pfWrite_x16(Unit, pBuffer, NumBytes);
}

/*********************************************************************
*
*       _WriteCmd
*
*  Function description
*    Writes a single byte command to the NAND flash
*/
static void _WriteCmd(U8 Unit, U16 Cmd) {
  _SetCmdMode(Unit);
  _Write_x16(Unit, &Cmd, sizeof(Cmd));
  _SetDataMode(Unit);      // Switch back to data mode (default)
}

/*********************************************************************
*
*       _StartOperation
*
*  Function description
*    Enables CE and writes a single byte command to the NAND flash
*/
static void _StartOperation(U8 Unit, U8 Cmd) {
  _EnableCE(Unit);
  _WriteCmd(Unit, Cmd);
}

/*********************************************************************
*
*       _WriteRowAddr
*
*  Function description
*    Writes the row address into the NAND flash.
*
*  Parameters
*    Unit       Driver index.
*    RowAddr    Zero-based page index.
*
*  Notes
*    (1) RowAddr
*        This is the zero based page index.
*        A block consists of 64 pages, so that BlockIndex = RowAddr / 64.
*/
static void _WriteRowAddr(U8 Unit, U32 RowAddr) {
  U16 aAddr[3];

  _SetAddrMode(Unit);
  aAddr[0] = (U16)(RowAddr & 0xFFu);
  aAddr[1] = (U16)((RowAddr >> 8) & 0xFFu);
  aAddr[2] = (U16)((RowAddr >> 16) & 0xFFu);
  _Write_x16(Unit, aAddr, sizeof(aAddr));
}

/*********************************************************************
*
*       _WriteCRAddr
*
*  Function description
*    Writes the column and row address into the NAND flash.
*
*  Parameters
*    Unit       Driver index.
*    ColAddr    Byte-offset within a page.
*    RowAddr    Zero-based page index.
*/
static void _WriteCRAddr(U8 Unit, unsigned ColAddr, U32 RowAddr) {
  U16 aAddr[5];

  ColAddr >>= 1;
  _SetAddrMode(Unit);
  aAddr[0] = (U16)(ColAddr & 0xFFu);
  aAddr[1] = (U16)((ColAddr >> 8) & 0xFFu);
  aAddr[2] = (U16)(RowAddr & 0xFFu);
  aAddr[3] = (U16)((RowAddr >> 8) & 0xFFu);
  aAddr[4] = (U16)((RowAddr >> 16) & 0xFFu);
  _Write_x16(Unit, aAddr, sizeof(aAddr));
  _SetDataMode(Unit);
}

/*********************************************************************
*
*       _WriteCAddr
*
*  Function description
*    Writes the column into the NAND flash.
*
*  Parameters
*    Unit      Driver index.
*    ColAddr   Byte-offset within the selected page.
*/
static void _WriteCAddr(U8 Unit, unsigned ColAddr) {
  U16 aAddr[2];

  ColAddr >>= 1;
  _SetAddrMode(Unit);
  aAddr[0] = (U16)(ColAddr & 0xFFu);
  aAddr[1] = (U16)((ColAddr >> 8) & 0xFFu);
  _Write_x16(Unit, aAddr, sizeof(aAddr));
  _SetDataMode(Unit);
}

/*********************************************************************
*
*       _ReadStatus
*
*  Function description
*    Reads and returns the contents of the status register.
*/
static U8 _ReadStatus(U8 Unit) {
  U16 r;

  _WriteCmd(Unit, NAND_CMD_READ_STATUS);
  _Read_x16(Unit, &r, sizeof(r));
  return (U8)r;
}

/*********************************************************************
*
*       _ResetErr
*
*  Function description
*    Resets the NAND flash by command
*/
static void _ResetErr(U8 Unit) {
  U8 Status;

  _StartOperation(Unit, NAND_CMD_RESET_CHIP);
  do {
    Status = _ReadStatus(Unit);
  } while ((Status & STATUS_READY) == 0u);
  _DisableCE(Unit);
}

/*********************************************************************
*
*       _WaitBusy
*
*  Function description
*    Waits for the NAND to complete its last operation.
*
*  Return value
*    ==0  Success
*    !=0  An error has occurred
*/
static int _WaitBusy(U8 Unit) {
  U8 Status;

  //
  // Try to use the hardware pin to find out when busy is cleared.
  //
  (void)_WaitWhileBusy(Unit, 0);
  //
  // Wait until the NAND flash is ready for the next operation.
  //
  do {
    Status = _ReadStatus(Unit);
  } while ((Status & STATUS_READY) == 0u);
  if ((Status & STATUS_ERROR) != 0u) {
    _ResetErr(Unit);
    return 1;                       // Error
  }
  return 0;                         // Success
}

/*********************************************************************
*
*       _EndOperation
*
*  Function description
*    Checks status register to find out if operation was successful and disables CE.
*
*  Return value
*    ==0    Data successfully transferred.
*    !=0    An error has occurred.
*/
static int _EndOperation(U8 Unit) {
  U8 Status;

  Status = _ReadStatus(Unit);
  if ((Status & (STATUS_ERROR | STATUS_READY)) != STATUS_READY) {
    _ResetErr(Unit);
    return 1;                        // Error
  }
  _DisableCE(Unit);
  return 0;                          // O.K.
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
*    ==0    Data successfully transferred.
*    ==1    An error has occurred.
*/
static int _WaitEndOperation(U8 Unit) {
  if(_WaitBusy(Unit) != 0) {
    return -1;
  }
  return _EndOperation(Unit);
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
*    ==0    Data successfully transferred.
*    !=0    An error has occurred.
*/
static int _PHY_Read(U8 Unit, U32 PageNo, void * pData, unsigned Off, unsigned NumBytes) {
  FS_DEBUG_ASSERT(FS_MTYPE_DRIVER, ((NumBytes | Off | SEGGER_PTR2ADDR(pData)) & 1u) == 0u);
  _StartOperation(Unit, NAND_CMD_READ_1);
  _WriteCRAddr(Unit, Off, PageNo);
  _WriteCmd(Unit, NAND_CMD_READ_2);
  if (_WaitBusy(Unit) != 0) {
    return 1;                            // Error
  }
  //
  // Restore the command register destroyed by the READ STATUS operation
  //
  _WriteCmd(Unit, NAND_CMD_READ_1);
  _Read_x16(Unit, pData, NumBytes);
  return _EndOperation(Unit);
}

/*********************************************************************
*
*       _PHY_ReadEx
*
*  Function description
*    Reads data from a 2 parts of a page.
*    Typically used to read data and spare area at the same time.
*
*  Return value
*    ==0    Data successfully transferred.
*    !=0    An error has occurred.
*
*  Notes
*    (1) Literature
*        Procedure taken from [1], Random data output in a Page, p. 30
*/
static int _PHY_ReadEx(U8 Unit, U32 PageNo, void * pData, unsigned Off, unsigned NumBytes, void * pSpare, unsigned OffSpare, unsigned NumBytesSpare) {
  FS_DEBUG_ASSERT(FS_MTYPE_DRIVER, ((NumBytes | Off | SEGGER_PTR2ADDR(pSpare) | OffSpare | NumBytesSpare) & 1u) == 0u);
  _StartOperation(Unit, NAND_CMD_READ_1);
  _WriteCRAddr(Unit, Off, PageNo);
  _WriteCmd(Unit, NAND_CMD_READ_2);
  if (_WaitBusy(Unit) != 0) {
    return 1;                            // Error
  }
  //
  // Restore the command register destroyed by the READ STATUS operation
  //
  _WriteCmd(Unit, NAND_CMD_READ_1);
  _Read_x16(Unit, pData, NumBytes);
  _WriteCmd(Unit, NAND_CMD_RANDOM_READ_1);
  _WriteCAddr(Unit, OffSpare);
  _WriteCmd(Unit, NAND_CMD_RANDOM_READ_2);
  _Read_x16(Unit, pSpare, NumBytesSpare);
  return _EndOperation(Unit);
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
*/
static int _PHY_Write(U8 Unit, U32 PageNo, const void * pData, unsigned Off, unsigned NumBytes) {
  FS_DEBUG_ASSERT(FS_MTYPE_DRIVER, ((NumBytes | Off | SEGGER_PTR2ADDR(pData)) & 1u) == 0u);
  _StartOperation(Unit, NAND_CMD_WRITE_1);
  _WriteCRAddr(Unit, Off, PageNo);
  _Write_x16(Unit, pData, NumBytes);
  _WriteCmd(Unit, NAND_CMD_WRITE_2);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NAND_PHY_2048x16: Write:    Block: 0x%.8x,  Page: 0x%.8x, Off: 0x%.8x, NumBytes: 0x%.8x\n", (PageNo >> 6), (PageNo & 0x3Fu),  Off, NumBytes));
  return _WaitEndOperation(Unit);
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
*    ==1    An error has occurred.
*/
static int _PHY_WriteEx(U8 Unit, U32 PageNo, const void * pData, unsigned Off, unsigned NumBytes, const void * pSpare, unsigned OffSpare, unsigned NumBytesSpare) {
  FS_DEBUG_ASSERT(FS_MTYPE_DRIVER, ((NumBytes | Off | SEGGER_PTR2ADDR(pData) | SEGGER_PTR2ADDR(pSpare) | OffSpare | NumBytesSpare) & 1u) == 0u);
  _StartOperation(Unit, NAND_CMD_WRITE_1);
  _WriteCRAddr(Unit, Off, PageNo);
  _Write_x16(Unit, pData, NumBytes);
  _WriteCmd(Unit, NAND_CMD_RANDOM_WRITE);
  _WriteCAddr(Unit, OffSpare);
  _Write_x16(Unit, pSpare, NumBytesSpare);
  _WriteCmd(Unit, NAND_CMD_WRITE_2);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NAND_PHY_2048x16: WriteEx:  Block: 0x%.8x,  Page: 0x%.8x, Off: 0x%.8x, NumBytes: 0x%.8x, OffSpare: 0x%.8x, NumBytesSpare: 0x%.8x\n", PageNo / PAGES_PER_BLOCK, PageNo & (PAGES_PER_BLOCK - 1u),  Off, NumBytes, OffSpare, NumBytesSpare));
  return _WaitEndOperation(Unit);
}

/*********************************************************************
*
*       _PHY_EraseBlock
*
*  Function description
*    Erases a block.
*
*  Return value
*    ==0    Data successfully transferred.
*    !=0    An error has occurred.
*/
static int _PHY_EraseBlock(U8 Unit, U32 BlockNo) {
  _StartOperation(Unit, NAND_CMD_ERASE_1);
  _WriteRowAddr(Unit, BlockNo);
  _WriteCmd(Unit, NAND_CMD_ERASE_2);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NAND_PHY_2048x16: Erase:    Block: 0x%.8x\n", (BlockNo / PAGES_PER_BLOCK)));
  return _WaitEndOperation(Unit);
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
*    ==1    Error: device can not be handled
*
*  Notes
*       (1) A RESET command must be issued as the first command after power-on (see [3])
*/
static int _PHY_InitGetDeviceInfo(U8 Unit, FS_NAND_DEVICE_INFO * pDevInfo) {
  U16       aId[5];
  U8        DeviceCode;
  U16       NumBlocks;
  const U16 Dummy = 0;
  int       r;

  ASSERT_HW_TYPE_IS_SET(Unit);
  r         = 0;          // Set to indicate success.
  NumBlocks = 0;
  _Init_x16(Unit);
  _ResetErr(Unit);        // Note 1
  //
  // Retrieve Id information from NAND device
  //
  _StartOperation(Unit, NAND_CMD_READ_ID);
  _SetAddrMode(Unit);
  _Write_x16(Unit, &Dummy, sizeof(Dummy));
  _SetDataMode(Unit);
  _Read_x16(Unit, &aId[0], sizeof(aId));
  if (_EndOperation(Unit) != 0) {
    return 1;                   // Error
  }
  DeviceCode  = (U8)aId[1];
  switch(DeviceCode)   {
  case 0xB1:
  case 0xC1:
    NumBlocks = 1024;
    break;
  case 0xBA:
  case 0xCA:
    NumBlocks = 2048;
    break;
  case 0xBC:
  case 0xCC:
    NumBlocks = 4096;
    break;
  case 0xB3:
  case 0xC3:
    NumBlocks = 8192;
    break;
  default:
    r = 1;                        // Error
    break;
  }
  if (r == 0) {
    pDevInfo->BPP_Shift    = 11;  // 2048 bytes/page
    pDevInfo->PPB_Shift    = 6;   // Large page NAND flashes have 64 pages per block
    pDevInfo->NumBlocks    = NumBlocks;
    pDevInfo->DataBusWidth = 16;
  }
  return r;                       // Success
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
*    < 0    Error
*    ==0    Not write protected
*    > 0    Write protected
*/
static int _PHY_IsWP(U8 Unit) {
  U8 Status;

  _EnableCE(Unit);
  Status = _ReadStatus(Unit);
  if (_EndOperation(Unit) != 0) {
    return -1;                    // Error
  }
  if ((Status & STATUS_WRITE_PROTECTED) != 0u) {
    return 0;
  }
  return 1;
}

/*********************************************************************
*
*       Public data
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_NAND_PHY_2048x16
*/
const FS_NAND_PHY_TYPE FS_NAND_PHY_2048x16 = {
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

/*********************************************************************
*
*       FS_NAND_2048x16_SetHWType
*
*  Function description
*    Configures the hardware access routines for a NAND physical layer
*    of type FS_NAND_PHY_2048x16.
*
*  Parameters
*    Unit       Index of the physical layer instance (0-based)
*    pHWType    Type of the hardware layer to use. Cannot be NULL.
*
*  Additional information
*    This function is mandatory and has to be called once in FS_X_AddDevices()
*    for every instance of a NAND physical layer of type FS_NAND_PHY_2048x16.
*/
void FS_NAND_2048x16_SetHWType(U8 Unit, const FS_NAND_HW_TYPE * pHWType) {
  NAND_2048X16_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  if (Unit < (U8)FS_NAND_NUM_UNITS) {
    pInst = &_aInst[Unit];
    pInst->pHWType = pHWType;
  }
}

/*************************** End of file ****************************/
