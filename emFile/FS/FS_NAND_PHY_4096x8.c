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
File        : FS_NAND_PHY_4096x8.c
Purpose     : 4kByte page NAND flashes physical 8-bit access
Literature  : [1] \\Fileserver\techinfo\Company\Samsung\NAND_Flash\Device\K9F8G08x0M_4KPageSLC_R10.pdf
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
*       NAND status
*/
#define STATUS_ERROR                0x01u   // 0:Pass,          1:Fail
#define STATUS_READY                0x40u   // 0:Busy,          1:Ready
#define STATUS_WRITE_PROTECTED      0x80u   // 0:Protect,       1:Not Protect

/*********************************************************************
*
*       NAND commands
*/
#define NAND_CMD_WRITE_1            0x80
#define NAND_CMD_WRITE_2            0x10
#define NAND_CMD_READ_1             0x00
#define NAND_CMD_READ_2             0x30
#define NAND_CMD_RESET_CHIP         0xFF
#define NAND_CMD_ERASE_1            0x60
#define NAND_CMD_ERASE_2            0xD0
#define NAND_CMD_READ_STATUS        0x70
#define NAND_CMD_READ_ID            0x90
#define NAND_CMD_RANDOM_DATA_OUT_1  0x05
#define NAND_CMD_RANDOM_DATA_OUT_2  0xE0
#define NAND_CMD_RANDOM_DATA_IN     0x85

/*********************************************************************
*
*       ASSERT_UNIT_NO_IS_IN_RANGE
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)                                            \
    if ((Unit) >= (U8)FS_NAND_NUM_UNITS) {                                            \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NAND_PHY_4096x8: Invalid unit number.")); \
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
  #define ASSERT_HW_TYPE_IS_SET(Unit)                                              \
    if (_aInst[Unit].pHWType == NULL) {                                            \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NAND_PHY_4096x8: HW layer not set.")); \
      FS_X_PANIC(FS_ERRCODE_HW_LAYER_NOT_SET);                                     \
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
*       NAND_4096X8_INST
*/
typedef struct {
  const FS_NAND_HW_TYPE * pHWType;
} NAND_4096X8_INST;

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static NAND_4096X8_INST _aInst[FS_NAND_NUM_UNITS];

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _Init_x8
*/
static void _Init_x8(U8 Unit) {
  NAND_4096X8_INST * pInst;

  pInst = &_aInst[Unit];
  pInst->pHWType->pfInit_x8(Unit);
}

/*********************************************************************
*
*       _DisableCE
*/
static void _DisableCE(U8 Unit) {
  NAND_4096X8_INST * pInst;

  pInst = &_aInst[Unit];
  pInst->pHWType->pfDisableCE(Unit);
}

/*********************************************************************
*
*       _EnableCE
*/
static void _EnableCE(U8 Unit) {
  NAND_4096X8_INST * pInst;

  pInst = &_aInst[Unit];
  pInst->pHWType->pfEnableCE(Unit);
}

/*********************************************************************
*
*       _SetAddrMode
*/
static void _SetAddrMode(U8 Unit) {
  NAND_4096X8_INST * pInst;

  pInst = &_aInst[Unit];
  pInst->pHWType->pfSetAddrMode(Unit);
}

/*********************************************************************
*
*       _SetCmdMode
*/
static void _SetCmdMode(U8 Unit) {
  NAND_4096X8_INST * pInst;

  pInst = &_aInst[Unit];
  pInst->pHWType->pfSetCmdMode(Unit);
}

/*********************************************************************
*
*       _SetDataMode
*/
static void _SetDataMode(U8 Unit) {
  NAND_4096X8_INST * pInst;

  pInst = &_aInst[Unit];
  pInst->pHWType->pfSetDataMode(Unit);
}

/*********************************************************************
*
*       _WaitWhileBusy
*/
static int _WaitWhileBusy(U8 Unit, unsigned us) {
  NAND_4096X8_INST * pInst;
  int        r;

  pInst = &_aInst[Unit];
  r = pInst->pHWType->pfWaitWhileBusy(Unit, us);
  return r;
}

/*********************************************************************
*
*       _Read_x8
*/
static void _Read_x8(U8 Unit, void * pBuffer, unsigned NumBytes) {
  NAND_4096X8_INST * pInst;

  pInst = &_aInst[Unit];
  pInst->pHWType->pfRead_x8(Unit, pBuffer, NumBytes);
}

/*********************************************************************
*
*       _Write_x8
*/
static void _Write_x8(U8 Unit, const void * pBuffer, unsigned NumBytes) {
  NAND_4096X8_INST * pInst;

  pInst = &_aInst[Unit];
  pInst->pHWType->pfWrite_x8(Unit, pBuffer, NumBytes);
}

/*********************************************************************
*
*       _WriteCmd
*
*  Function description
*    Writes a single byte command to the NAND flash
*/
static void _WriteCmd(U8 Unit, U8 Cmd) {
  _SetCmdMode(Unit);
  _Write_x8(Unit, &Cmd, 1);
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
static void _WriteRowAddr(U8 Unit, unsigned RowAddr) {
  U8 aAddr[3];

  _SetAddrMode(Unit);
  FS_StoreU24LE(&aAddr[0], RowAddr);
  _Write_x8(Unit, aAddr, sizeof(aAddr));
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
static void _WriteCRAddr(U8 Unit, unsigned ColAddr, unsigned RowAddr) {
  U8 aAddr[5];

  _SetAddrMode(Unit);
  FS_StoreU16LE(&aAddr[0], ColAddr);
  FS_StoreU24LE(&aAddr[2], RowAddr);
  _Write_x8(Unit, aAddr, sizeof(aAddr));
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
*    Unit       Driver index.
*    ColAddr    Byte-offset within the selected page.
*/
static void _WriteCAddr(U8 Unit, unsigned ColAddr) {
  U8 aAddr[2];

  _SetAddrMode(Unit);
  FS_StoreU16LE(&aAddr[0], ColAddr);
  _Write_x8(Unit, aAddr, sizeof(aAddr));
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
  U8 r;

  _WriteCmd(Unit, NAND_CMD_READ_STATUS);
  _Read_x8(Unit, &r, 1);
  return r;
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
*    Waits until the NAND device has completed an operation
*
*  Return value
*    ==0    Success
*    !=0    An error has occurred
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
static int _PHY_Read(U8 Unit, U32 PageNo, void * pBuffer, unsigned Off, unsigned NumBytes) {
  _StartOperation(Unit, NAND_CMD_READ_1);
  _WriteCRAddr(Unit, Off, PageNo);
  _WriteCmd(Unit, NAND_CMD_READ_2);
  if (_WaitBusy(Unit) != 0) {
    return 1;                             // Error
  }
  _WriteCmd(Unit, NAND_CMD_READ_1);       // Restore the read command overwritten by _WaitBusy()
  _Read_x8(Unit, pBuffer, NumBytes);
  return _EndOperation(Unit);
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
*    ==0    Data successfully transferred.
*    !=0    An error has occurred.
*
*  Notes
*    (1) Literature
*        Procedure taken from [1], Random data output in a Page, p. 30
*/
static int _PHY_ReadEx(U8 Unit, U32 PageNo, void * pBuffer0, unsigned Off0, unsigned NumBytes0, void * pBuffer1, unsigned Off1, unsigned NumBytes1) {
  _StartOperation(Unit, NAND_CMD_READ_1);
  _WriteCRAddr(Unit, Off0, PageNo);
  _WriteCmd(Unit, NAND_CMD_READ_2);
  if (_WaitBusy(Unit) != 0) {
    return 1;                             // Error
  }
  _WriteCmd(Unit, NAND_CMD_READ_1);       // Restore the read command overwritten by _WaitBusy()
  _Read_x8(Unit, pBuffer0, NumBytes0);     // Read first data
  _WriteCmd(Unit, NAND_CMD_RANDOM_DATA_OUT_1);
  _WriteCAddr(Unit, Off1);
  _WriteCmd(Unit, NAND_CMD_RANDOM_DATA_OUT_2);
  _Read_x8(Unit, pBuffer1, NumBytes1);     // Read second data (usually spare)
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
static int _PHY_Write(U8 Unit, U32 PageNo, const void * pBuffer, unsigned Off, unsigned NumBytes) {
  _StartOperation(Unit, NAND_CMD_WRITE_1);
  _WriteCRAddr(Unit, Off, PageNo);
  _Write_x8(Unit, pBuffer, NumBytes);
  _WriteCmd(Unit, NAND_CMD_WRITE_2);
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
static int _PHY_WriteEx(U8 Unit, U32 PageNo, const void * pBuffer0, unsigned Off0, unsigned NumBytes0, const void * pBuffer1, unsigned Off1, unsigned NumBytes1) {
  _StartOperation(Unit, NAND_CMD_WRITE_1);
  _WriteCRAddr(Unit, Off0, PageNo);
  _Write_x8(Unit, pBuffer0, NumBytes0);
  _WriteCmd(Unit, NAND_CMD_RANDOM_DATA_IN);
  _WriteCAddr(Unit, Off1);
  _Write_x8(Unit, pBuffer1, NumBytes1);
  _WriteCmd(Unit, NAND_CMD_WRITE_2);
  return _WaitEndOperation(Unit);
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
*    ==0    Data successfully transferred.
*    ==1    An error has occurred.
*/
static int _PHY_EraseBlock(U8 Unit, U32 PageIndex) {
  _StartOperation(Unit, NAND_CMD_ERASE_1);
  _WriteRowAddr(Unit, PageIndex);
  _WriteCmd(Unit, NAND_CMD_ERASE_2);
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
*    (1) A RESET command must be issued as the first command after power-on (see [2])
*/
static int _PHY_InitGetDeviceInfo(U8 Unit, FS_NAND_DEVICE_INFO * pDevInfo) {
  U8       aId[5];
  U8       DeviceCode;
  U16      NumBlocks;
  U8       PPB_Shift;
  const U8 Dummy = 0;
  int      r;

  ASSERT_HW_TYPE_IS_SET(Unit);
  r         = 0;              // Set to indicate success.
  NumBlocks = 0;
  _Init_x8(Unit);
  _ResetErr(Unit);            // Note 1
  //
  // Retrieve id information from NAND device.
  //
  _StartOperation(Unit, NAND_CMD_READ_ID);
  _SetAddrMode(Unit);
  _Write_x8(Unit, &Dummy, 1);
  _SetDataMode(Unit);
  _Read_x8(Unit, &aId[0], sizeof(aId));
  if (_EndOperation(Unit) != 0) {
    return 1;                 // Error, could not read id.
  }
  PPB_Shift  = 6;             // Large page NAND flashes have typically 64 pages per block.
  DeviceCode = aId[1];
  switch(DeviceCode)   {
  case 0xD3:
    NumBlocks = 4096;
    break;
  case 0xD5:
     //
     // Hynix H27UAG8T2A series uses the same product id
     // as other manufacturers but the device has a different sectorization.
     // We have to check the manufacturer id code (0xAD) in order to determine
     // the correct parameters.
     //
    if (aId[0] == 0xADu) {
      NumBlocks = 4096;
      PPB_Shift = 7;
    } else {
      NumBlocks = 8192;
    }
    break;
  case 0xD7:
    NumBlocks = 8192;
    PPB_Shift = 7;
    break;
  default:
    r = 1;                         // Error, could not identify device.
    break;
  }
  if (r == 0) {
    pDevInfo->BPP_Shift    = 12;   // 4096 bytes/page
    pDevInfo->PPB_Shift    = PPB_Shift;
    pDevInfo->NumBlocks    = NumBlocks;
    pDevInfo->DataBusWidth = 8;
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
*       FS_NAND_PHY_4096x8
*/
const FS_NAND_PHY_TYPE FS_NAND_PHY_4096x8 = {
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
*       FS_NAND_4096x8_SetHWType
*
*  Function description
*    Configures the hardware access routines for a NAND physical layer
*    of type FS_NAND_PHY_4096x8.
*
*  Parameters
*    Unit       Index of the physical layer instance (0-based)
*    pHWType    Type of the hardware layer to use. Cannot be NULL.
*
*  Additional information
*    This function is mandatory and has to be called once in FS_X_AddDevices()
*    for every instance of a NAND physical layer of type FS_NAND_PHY_4096x8.
*/
void FS_NAND_4096x8_SetHWType(U8 Unit, const FS_NAND_HW_TYPE * pHWType) {
  NAND_4096X8_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  if (Unit < (U8)FS_NAND_NUM_UNITS) {
    pInst = &_aInst[Unit];
    pInst->pHWType = pHWType;
  }
}

/*************************** End of file ****************************/
