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
File        : FS_NAND_PHY_x8.c
Purpose     : General physical layer for NAND flashes
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*     #include Section
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

#if (FS_NAND_SUPPORT_AUTO_DETECTION == 0)

/*********************************************************************
*
*       Execution status
*/
#define STATUS_ERROR                0x01u   // 0:Pass,        1:Fail
#define STATUS_READY                0x40u   // 0:Busy,        1:Ready
#define STATUS_WRITE_PROTECTED      0x80u   // 0:Protect,     1:Not Protect

/*********************************************************************
*
*       NAND commands
*/
#define CMD_READ_1                  0x00
#define CMD_RANDOM_READ_1           0x05
#define CMD_WRITE_2                 0x10
#define CMD_READ_2                  0x30
#define CMD_ERASE_1                 0x60
#define CMD_ERASE_2                 0xD0
#define CMD_READ_STATUS             0x70
#define CMD_WRITE_1                 0x80
#define CMD_RANDOM_WRITE            0x85
#define CMD_RANDOM_READ_2           0xE0
#define CMD_RESET                   0xFF

/*********************************************************************
*
*       Number of bytes in a column or row address
*/
#define NUM_BYTES_COL_ADDR          2
#define NUM_BYTES_ROW_ADDR          3

#endif // FS_NAND_SUPPORT_AUTO_DETECTION == 0

/*********************************************************************
*
*       ASSERT_UNIT_NO_IS_IN_RANGE
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)                                        \
    if ((Unit) >= (U8)FS_NAND_NUM_UNITS) {                                        \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NAND_PHY_x8: Invalid unit number.")); \
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);                                        \
    }
#else
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)
#endif

#if (FS_NAND_SUPPORT_AUTO_DETECTION == 0)

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
*       ASSERT_HW_TYPE_IS_SET
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_HW_TYPE_IS_SET(Unit)                                          \
    if (_aInst[Unit].pHWType == NULL) {                                        \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NAND_PHY_x8: HW layer not set.")); \
      FS_X_PANIC(FS_ERRCODE_HW_LAYER_NOT_SET);                                 \
    }
#else
  #define ASSERT_HW_TYPE_IS_SET(Unit)
#endif

#endif // FS_NAND_SUPPORT_AUTO_DETECTION == 0

/*********************************************************************
*
*       Local types
*
**********************************************************************
*/

#if FS_NAND_SUPPORT_AUTO_DETECTION

/*********************************************************************
*
*       PHY_INFO
*/
typedef struct {
  const FS_NAND_PHY_TYPE  * pPhyType;
  void                   (* pfSetHWType)(U8 Unit, const FS_NAND_HW_TYPE * pHWType);
} PHY_INFO;

#else

/*********************************************************************
*
*       NAND_X8_INST
*/
typedef struct {
  U8                      Unit;                     // Index of the physical layer
  U8                      DataBusWidth;             // Width of the data bus in bits (16 or 8)
  U8                      NumColAddrBytes;          // Number of bytes in a column address
  U8                      NumRowAddrBytes;          // Number of bytes in a row address
  U8                      ldPagesPerBlock;          // Number of pages in a block
  U8                      ldBytesPerPage;           // Number of bytes in a page
  U16                     NumBlocks;                // Number of blocks in the device
  U16                     BytesPerSpareArea;        // Number of bytes in the spare area
  const FS_NAND_HW_TYPE * pHWType;                  // Pointer to HW access routines
} NAND_X8_INST;

#endif

/*********************************************************************
*
*       Static const data
*
**********************************************************************
*/

#if FS_NAND_SUPPORT_AUTO_DETECTION

/*********************************************************************
*
*       _apPhyType
*
*  This is the list of physical layers which are checked. The first one in the list which works is used.
*/
static const PHY_INFO _aPhyList_x8[] = {
  {&FS_NAND_PHY_512x8,  FS_NAND_512x8_SetHWType},
  {&FS_NAND_PHY_2048x8, FS_NAND_2048x8_SetHWType},
  {&FS_NAND_PHY_4096x8, FS_NAND_4096x8_SetHWType},
  {&FS_NAND_PHY_ONFI,   FS_NAND_ONFI_SetHWType},
  {(const FS_NAND_PHY_TYPE *)NULL, NULL}
};

static const PHY_INFO _aPhyList_x[] = {
  {&FS_NAND_PHY_512x8,   FS_NAND_512x8_SetHWType},
  {&FS_NAND_PHY_2048x8,  FS_NAND_2048x8_SetHWType},
  {&FS_NAND_PHY_2048x16, FS_NAND_2048x16_SetHWType},
  {&FS_NAND_PHY_4096x8,  FS_NAND_4096x8_SetHWType},
  {&FS_NAND_PHY_ONFI,    FS_NAND_ONFI_SetHWType},
  {(const FS_NAND_PHY_TYPE *)NULL, NULL}
};

#endif

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
#if FS_NAND_SUPPORT_AUTO_DETECTION
  static const FS_NAND_PHY_TYPE * _apPhyType[FS_NAND_NUM_UNITS];
#else
  static NAND_X8_INST _aInst[FS_NAND_NUM_UNITS];
#endif

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/
#if (FS_NAND_SUPPORT_AUTO_DETECTION == 0)

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
*       _Init_x8
*/
static void _Init_x8(U8 Unit) {
  NAND_X8_INST * pInst;

  pInst = &_aInst[Unit];
  pInst->pHWType->pfInit_x8(Unit);
}

/*********************************************************************
*
*       _Init_x16
*/
static void _Init_x16(U8 Unit) {
  NAND_X8_INST * pInst;

  pInst = &_aInst[Unit];
  pInst->pHWType->pfInit_x16(Unit);
}

/*********************************************************************
*
*       _DisableCE
*/
static void _DisableCE(U8 Unit) {
  NAND_X8_INST * pInst;

  pInst = &_aInst[Unit];
  pInst->pHWType->pfDisableCE(Unit);
}

/*********************************************************************
*
*       _EnableCE
*/
static void _EnableCE(U8 Unit) {
  NAND_X8_INST * pInst;

  pInst = &_aInst[Unit];
  pInst->pHWType->pfEnableCE(Unit);
}

/*********************************************************************
*
*       _SetAddrMode
*/
static void _SetAddrMode(U8 Unit) {
  NAND_X8_INST * pInst;

  pInst = &_aInst[Unit];
  pInst->pHWType->pfSetAddrMode(Unit);
}

/*********************************************************************
*
*       _SetCmdMode
*/
static void _SetCmdMode(U8 Unit) {
  NAND_X8_INST * pInst;

  pInst = &_aInst[Unit];
  pInst->pHWType->pfSetCmdMode(Unit);
}

/*********************************************************************
*
*       _SetDataMode
*/
static void _SetDataMode(U8 Unit) {
  NAND_X8_INST * pInst;

  pInst = &_aInst[Unit];
  pInst->pHWType->pfSetDataMode(Unit);
}

/*********************************************************************
*
*       _WaitWhileBusy
*/
static int _WaitWhileBusy(U8 Unit, unsigned us) {
  NAND_X8_INST * pInst;
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
  NAND_X8_INST * pInst;

  pInst = &_aInst[Unit];
  pInst->pHWType->pfRead_x8(Unit, pBuffer, NumBytes);
}

/*********************************************************************
*
*       _Write_x8
*/
static void _Write_x8(U8 Unit, const void * pBuffer, unsigned NumBytes) {
  NAND_X8_INST * pInst;

  pInst = &_aInst[Unit];
  pInst->pHWType->pfWrite_x8(Unit, pBuffer, NumBytes);
}

/*********************************************************************
*
*       _Read_x16
*/
static void _Read_x16(U8 Unit, void * pBuffer, unsigned NumBytes) {
  NAND_X8_INST * pInst;

  pInst = &_aInst[Unit];
  pInst->pHWType->pfRead_x16(Unit, pBuffer, NumBytes);
}

/*********************************************************************
*
*       _Write_x16
*/
static void _Write_x16(U8 Unit, const void * pBuffer, unsigned NumBytes) {
  NAND_X8_INST * pInst;

  pInst = &_aInst[Unit];
  pInst->pHWType->pfWrite_x16(Unit, pBuffer, NumBytes);
}

/*********************************************************************
*
*       _WriteCmd
*
*   Function description
*     Writes a single byte command to the NAND flash
*/
static void _WriteCmd(U8 Unit, U8 Cmd) {
  _SetCmdMode(Unit);
  _Write_x8(Unit, &Cmd, sizeof(Cmd));
}

/*********************************************************************
*
*       _WriteAddrRow
*
*   Function description
*     Selects the address of the page to be accessed.
*/
static void _WriteAddrRow(U8 Unit, U32 RowAddr, unsigned NumRowAddrBytes) {
  U8         aAddr[sizeof(RowAddr)];
  U8       * p;
  unsigned   NumBytes;

  _SetAddrMode(Unit);
  p        = aAddr;
  NumBytes = NumRowAddrBytes;
  do {
    *p++      = (U8)RowAddr;
    RowAddr >>= 8;
  } while (--NumBytes != 0u);
  _Write_x8(Unit, aAddr, NumRowAddrBytes);
}

/*********************************************************************
*
*       _WriteAddrCol
*
*  Function description
*    Selects the address of the byte to be accessed.
*/
static void _WriteAddrCol(U8 Unit, U32 ColAddr, unsigned NumColAddrBytes, U8 DataBusWidth) {
  U8         aAddr[sizeof(ColAddr)];
  U8       * p;
  unsigned   NumBytes;

  _SetAddrMode(Unit);
  if (DataBusWidth == 16u) {
    ColAddr >>= 1;        // Convert to a 16-bit word address
  }
  p        = aAddr;
  NumBytes = NumColAddrBytes;
  do {
    *p++      = (U8)ColAddr;
    ColAddr >>= 8;
  } while (--NumBytes != 0u);
  _Write_x8(Unit, aAddr, NumColAddrBytes);
}

/*********************************************************************
*
*       _WriteAddrColRow
*
*  Function description
*    Selects the byte and the page address to be accessed
*/
static void _WriteAddrColRow(U8 Unit, U32 ColAddr, U32 NumColAddrBytes, U32 RowAddr, U32 NumRowAddrBytes, U8 DataBusWidth) {
  U8         aAddr[sizeof(ColAddr) + sizeof(RowAddr)];
  U8       * p;
  unsigned   NumBytes;

  _SetAddrMode(Unit);
  if (DataBusWidth == 16u) {
    ColAddr >>= 1;        // Convert to a 16-bit word address
  }
  p        = aAddr;
  NumBytes = NumColAddrBytes;
  do {
    *p++      = (U8)ColAddr;
    ColAddr >>= 8;
  } while (--NumBytes != 0u);
  NumBytes = NumRowAddrBytes;
  do {
    *p++      = (U8)RowAddr;
    RowAddr >>= 8;
  } while (--NumBytes != 0u);
  _Write_x8(Unit, aAddr, NumColAddrBytes + NumRowAddrBytes);
}

/*********************************************************************
*
*       _ReadData
*
*  Function description
*    Transfers data from device to host CPU.
*/
static void _ReadData(U8 Unit, void * pData, unsigned NumBytes, U8 DataBusWidth) {
  _SetDataMode(Unit);
  if (DataBusWidth == 16u) {
    _Read_x16(Unit, pData, NumBytes);
  } else {
    _Read_x8(Unit, pData, NumBytes);
  }
}

/*********************************************************************
*
*       _WriteData
*
*  Function description
*    Transfers data from host CPU to device.
*/
static void _WriteData(U8 Unit, const void * pData, unsigned NumBytes, U8 DataBusWidth) {
  _SetDataMode(Unit);
  if (DataBusWidth == 16u) {
    _Write_x16(Unit, pData, NumBytes);
  } else {
    _Write_x8(Unit, pData, NumBytes);
  }
}

/*********************************************************************
*
*       _ReadStatus
*
*  Function description
*    Reads and returns the contents of the status register.
*/
static U8 _ReadStatus(U8 Unit) {
  U8 Status;

  _WriteCmd(Unit, CMD_READ_STATUS);
  _ReadData(Unit, &Status, sizeof(Status), 8);
  return Status;
}

/*********************************************************************
*
*       _WaitBusy
*
*  Function description
*    Waits for the NAND to complete its last operation.
*
*  Parameters
*    Unit   Device unit number.
*
*  Return value
*    ==0    Success.
*    !=0    An error has occurred.
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
static void _Reset(U8 Unit) {
  U8 Status;

  _EnableCE(Unit);
  _WriteCmd(Unit, CMD_RESET);
  do {
    Status = _ReadStatus(Unit);
  } while ((Status & STATUS_READY) == 0u);
  _DisableCE(Unit);
}

/*********************************************************************
*
*       _GetInst
*/
static NAND_X8_INST * _GetInst(U8 Unit) {
  NAND_X8_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  pInst = NULL;
  if (Unit < (U8)FS_NAND_NUM_UNITS) {
    pInst = &_aInst[Unit];
  }
  return pInst;
}

#endif // FS_NAND_SUPPORT_AUTO_DETECTION

/*********************************************************************
*
*       Static code (public via callback)
*
**********************************************************************
*/

#if FS_NAND_SUPPORT_AUTO_DETECTION

/*********************************************************************
*
*       _PHY_Read
*/
static int _PHY_Read(U8 Unit, U32 PageIndex, void * pData, unsigned Off, unsigned NumBytes) {
  int                      r;
  const FS_NAND_PHY_TYPE * pPhyType;

  r = -1;     // Error
  pPhyType = _apPhyType[Unit];
  if (pPhyType != NULL) {
    r = pPhyType->pfRead(Unit, PageIndex, pData, Off, NumBytes);
  }
  return r;
}


/*********************************************************************
*
*       _PHY_ReadEx
*/
static int _PHY_ReadEx(U8 Unit, U32 PageIndex, void * pBuffer0, unsigned Off0, unsigned NumBytes0, void * pBuffer1, unsigned Off1, unsigned NumBytes1) {
  int                      r;
  const FS_NAND_PHY_TYPE * pPhyType;

  r = -1;     // Error
  pPhyType = _apPhyType[Unit];
  if (pPhyType != NULL) {
    r = pPhyType->pfReadEx(Unit, PageIndex, pBuffer0, Off0, NumBytes0, pBuffer1, Off1, NumBytes1);
  }
  return r;
}

/*********************************************************************
*
*       _PHY_Write
*/
static int _PHY_Write(U8 Unit, U32 PageIndex, const void * pData, unsigned Off, unsigned NumBytes) {
  int                      r;
  const FS_NAND_PHY_TYPE * pPhyType;

  r = -1;     // Error
  pPhyType = _apPhyType[Unit];
  if (pPhyType != NULL) {
    r = pPhyType->pfWrite(Unit, PageIndex, pData, Off, NumBytes);
  }
  return r;
}


/*********************************************************************
*
*       _PHY_WriteEx
*/
static int _PHY_WriteEx(U8 Unit, U32 PageIndex, const void * pBuffer0, unsigned Off0, unsigned NumBytes0, const void * pBuffer1, unsigned Off1, unsigned NumBytes1) {
  int                      r;
  const FS_NAND_PHY_TYPE * pPhyType;

  r = -1;     // Error
  pPhyType = _apPhyType[Unit];
  if (pPhyType != NULL) {
    r = pPhyType->pfWriteEx(Unit, PageIndex, pBuffer0, Off0, NumBytes0, pBuffer1, Off1, NumBytes1);
  }
  return r;
}

/*********************************************************************
*
*       _PHY_EraseBlock
*/
static int _PHY_EraseBlock(U8 Unit, U32 BlockIndex) {
  int                      r;
  const FS_NAND_PHY_TYPE * pPhyType;

  r = -1;     // Error
  pPhyType = _apPhyType[Unit];
  if (pPhyType != NULL) {
    r = pPhyType->pfEraseBlock(Unit, BlockIndex);
  }
  return r;
}

/*********************************************************************
*
*       _PHY_IsWP
*/
static int _PHY_IsWP(U8 Unit) {
  int                      r;
  const FS_NAND_PHY_TYPE * pPhyType;

  r = -1;     // Error
  pPhyType = _apPhyType[Unit];
  if (pPhyType != NULL) {
    r = pPhyType->pfIsWP(Unit);
  }
  return r;
}

/*********************************************************************
*
*       _InitGetDeviceInfo
*/
static int _InitGetDeviceInfo(U8 Unit, FS_NAND_DEVICE_INFO * pDevInfo, const PHY_INFO * pPhyInfo) {
  const FS_NAND_PHY_TYPE * pPhyType;

  for (;;) {
    pPhyType = pPhyInfo->pPhyType;
    if (pPhyType == NULL) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_PHY_x8: Could not identify NAND flash."));
      return 1;       // Error, end of the list reached.
    }
    if (pPhyType->pfInitGetDeviceInfo(Unit, pDevInfo) == 0) {
      _apPhyType[Unit] = pPhyType;
      return 0;       // Success! Device is recognized by this physical layer.
    }
    ++pPhyInfo;
  }
}

/*********************************************************************
*
*       _PHY_InitGetDeviceInfo_x
*/
static int _PHY_InitGetDeviceInfo_x(U8 Unit, FS_NAND_DEVICE_INFO * pDevInfo) {
  return _InitGetDeviceInfo(Unit, pDevInfo, &_aPhyList_x[0]);
}

/*********************************************************************
*
*       _PHY_InitGetDeviceInfo_x8
*/
static int _PHY_InitGetDeviceInfo_x8(U8 Unit, FS_NAND_DEVICE_INFO * pDevInfo) {
  return _InitGetDeviceInfo(Unit, pDevInfo, &_aPhyList_x8[0]);
}

#else

/*********************************************************************
*
*       _PHY_Read
*/
static int _PHY_Read(U8 Unit, U32 PageIndex, void * pData, unsigned Off, unsigned NumBytes) {
  NAND_X8_INST * pInst;
  int            r;
  U8             DataBusWidth;
  U8             NumColAddrBytes;
  U8             NumRowAddrBytes;

  pInst = &_aInst[Unit];
  ASSERT_PARA_IS_ALIGNED(pInst, NumBytes | Off | SEGGER_PTR2ADDR(pData));
  DataBusWidth    = pInst->DataBusWidth;
  NumColAddrBytes = pInst->NumColAddrBytes;
  NumRowAddrBytes = pInst->NumRowAddrBytes;
  _EnableCE(Unit);
  //
  // Select the start address to read from
  //
  _WriteCmd(Unit, CMD_READ_1);
  _WriteAddrColRow(Unit, Off, NumColAddrBytes, PageIndex, NumRowAddrBytes, DataBusWidth);
  //
  // Start the execution of read command and wait for it to finish
  //
  _WriteCmd(Unit, CMD_READ_2);
  r = _WaitBusy(Unit);
  //
  // The data to read is now in the data register of device; copy it to host memory
  //
  _WriteCmd(Unit, CMD_READ_1);     // Revert to read mode. _WaitBusy() change it to status mode
  _ReadData(Unit, pData, NumBytes, DataBusWidth);
  _DisableCE(Unit);
  if (r != 0) {
    _Reset(Unit);
  }
  return r;
}

/*********************************************************************
*
*       _PHY_ReadEx
*/
static int _PHY_ReadEx(U8 Unit, U32 PageIndex, void * pData, unsigned Off, unsigned NumBytes, void * pSpare, unsigned OffSpare, unsigned NumBytesSpare) {
  NAND_X8_INST * pInst;
  int            r;
  U8             DataBusWidth;
  U8             NumColAddrBytes;
  U8             NumRowAddrBytes;

  pInst = &_aInst[Unit];
  ASSERT_PARA_IS_ALIGNED(pInst, NumBytes | Off | SEGGER_PTR2ADDR(pSpare) | OffSpare | NumBytesSpare);
  DataBusWidth    = pInst->DataBusWidth;
  NumColAddrBytes = pInst->NumColAddrBytes;
  NumRowAddrBytes = pInst->NumRowAddrBytes;
  _EnableCE(Unit);
  //
  // Select the start address of the first location to read from
  //
  _WriteCmd(Unit, CMD_READ_1);
  _WriteAddrColRow(Unit, Off, NumColAddrBytes, PageIndex, NumRowAddrBytes, DataBusWidth);
  //
  // Start the execution of read command and wait for it to finish
  //
  _WriteCmd(Unit, CMD_READ_2);
  r = _WaitBusy(Unit);
  //
  // The data to read is now in the data register of device.
  // Copy the data from the first location to host memory
  //
  _WriteCmd(Unit, CMD_READ_1);     // Revert to read mode. _WaitBusy() change it to status mode
  _ReadData(Unit, pData, NumBytes, DataBusWidth);
  //
  // Select the start address of the second location to read from
  //
  _WriteCmd(Unit, CMD_RANDOM_READ_1);
  _WriteAddrCol(Unit, OffSpare, NumColAddrBytes, DataBusWidth);
  _WriteCmd(Unit, CMD_RANDOM_READ_2);
  //
  // Copy the data from the second location to host memory
  //
  _ReadData(Unit, pSpare, NumBytesSpare, DataBusWidth);
  _DisableCE(Unit);
  if (r != 0) {
    _Reset(Unit);
  }
  return r;
}

/*********************************************************************
*
*       _PHY_Write
*/
static int _PHY_Write(U8 Unit, U32 PageIndex, const void * pData, unsigned Off, unsigned NumBytes) {
  NAND_X8_INST * pInst;
  int            r;
  U8             DataBusWidth;
  U8             NumColAddrBytes;
  U8             NumRowAddrBytes;

  pInst = &_aInst[Unit];
  ASSERT_PARA_IS_ALIGNED(pInst, NumBytes | Off | SEGGER_PTR2ADDR(pData));
  DataBusWidth    = pInst->DataBusWidth;
  NumColAddrBytes = pInst->NumColAddrBytes;
  NumRowAddrBytes = pInst->NumRowAddrBytes;
  _EnableCE(Unit);
  //
  // Select the start address of the location to write to
  //
  _WriteCmd(Unit, CMD_WRITE_1);
  _WriteAddrColRow(Unit, Off, NumColAddrBytes, PageIndex, NumRowAddrBytes, DataBusWidth);
  //
  // Load the data register of device with the data to write
  //
  _WriteData(Unit, pData, NumBytes, DataBusWidth);
  //
  // Execute the write command and wait for it to finish
  //
  _WriteCmd(Unit, CMD_WRITE_2);
  r = _WaitBusy(Unit);
  _DisableCE(Unit);
  if (r != 0) {
    _Reset(Unit);
  }
  return r;
}

/*********************************************************************
*
*       _PHY_WriteEx
*/
static int _PHY_WriteEx(U8 Unit, U32 PageIndex, const void * pData, unsigned Off, unsigned NumBytes, const void * pSpare, unsigned OffSpare, unsigned NumBytesSpare) {
  NAND_X8_INST * pInst;
  int            r;
  U8             DataBusWidth;
  U8             NumColAddrBytes;
  U8             NumRowAddrBytes;

  pInst = &_aInst[Unit];
  ASSERT_PARA_IS_ALIGNED(pInst, NumBytes | Off | SEGGER_PTR2ADDR(pData) | SEGGER_PTR2ADDR(pSpare) | OffSpare | NumBytesSpare);
  DataBusWidth    = pInst->DataBusWidth;
  NumColAddrBytes = pInst->NumColAddrBytes;
  NumRowAddrBytes = pInst->NumRowAddrBytes;
  _EnableCE(Unit);
  //
  // Select the start address of the first location to write to.
  //
  _WriteCmd(Unit, CMD_WRITE_1);
  _WriteAddrColRow(Unit, Off, NumColAddrBytes, PageIndex, NumRowAddrBytes, DataBusWidth);
  //
  // Load the data register of device with the first data to write.
  //
  _WriteData(Unit, pData, NumBytes, DataBusWidth);
  //
  // Select the start address of the second location to write to.
  //
  _WriteCmd(Unit, CMD_RANDOM_WRITE);
  _WriteAddrCol(Unit, OffSpare, NumColAddrBytes, DataBusWidth);
  //
  // Load the data register of device with the second data to write
  //
  _WriteData(Unit, pSpare, NumBytesSpare, DataBusWidth);
  //
  // Execute the write command and wait for it to finish
  //
  _WriteCmd(Unit, CMD_WRITE_2);
  r = _WaitBusy(Unit);
  _DisableCE(Unit);
  if (r != 0) {
    _Reset(Unit);
  }
  return r;
}

/*********************************************************************
*
*       _PHY_EraseBlock
*/
static int _PHY_EraseBlock(U8 Unit, U32 BlockIndex) {
  NAND_X8_INST * pInst;
  int            r;

  pInst = &_aInst[Unit];
  _EnableCE(Unit);
  _WriteCmd(Unit, CMD_ERASE_1);
  _WriteAddrRow(Unit, BlockIndex, pInst->NumRowAddrBytes);
  _WriteCmd(Unit, CMD_ERASE_2);
  r = _WaitBusy(Unit);
  _DisableCE(Unit);
  if (r != 0) {
    _Reset(Unit);
  }
  return r;
}

/*********************************************************************
*
*       _PHY_IsWP
*/
static int _PHY_IsWP(U8 Unit) {
  U8 Status;

  _EnableCE(Unit);
  Status = _ReadStatus(Unit);
  _DisableCE(Unit);
  if ((Status & STATUS_WRITE_PROTECTED) != 0u) {
    return 0;
  }
  return 1;
}

/*********************************************************************
*
*       _InitGetDeviceInfo
*/
static void _InitGetDeviceInfo(NAND_X8_INST * pInst, FS_NAND_DEVICE_INFO * pDevInfo) {
  U8 Unit;
  U8 DataBusWidth;

  Unit         = pInst->Unit;
  DataBusWidth = pInst->DataBusWidth;
  //
  // Initialize hardware and reset the device
  //
  if (DataBusWidth == 8u) {
    _Init_x8(Unit);
  } else {
    _Init_x16(Unit);
  }
  _Reset(Unit);
  //
  // Fill in the information required by the physical layer.
  //
  pInst->NumColAddrBytes      = NUM_BYTES_COL_ADDR;
  pInst->NumRowAddrBytes      = NUM_BYTES_ROW_ADDR;
  //
  // Fill in the info required by the NAND driver.
  //
  pDevInfo->BPP_Shift         = pInst->ldBytesPerPage;
  pDevInfo->PPB_Shift         = pInst->ldPagesPerBlock;
  pDevInfo->NumBlocks         = pInst->NumBlocks;
  pDevInfo->BytesPerSpareArea = pInst->BytesPerSpareArea;
  pDevInfo->DataBusWidth      = DataBusWidth;
}

/*********************************************************************
*
*       _PHY_InitGetDeviceInfo_x
*/
static int _PHY_InitGetDeviceInfo_x(U8 Unit, FS_NAND_DEVICE_INFO * pDevInfo) {
  NAND_X8_INST * pInst;

  ASSERT_HW_TYPE_IS_SET(Unit);
  pInst = &_aInst[Unit];
  pInst->Unit = Unit;
  _InitGetDeviceInfo(pInst, pDevInfo);
  return 0;
}

/*********************************************************************
*
*       _PHY_InitGetDeviceInfo_x8
*/
static int _PHY_InitGetDeviceInfo_x8(U8 Unit, FS_NAND_DEVICE_INFO * pDevInfo) {
  NAND_X8_INST * pInst;

  ASSERT_HW_TYPE_IS_SET(Unit);
  pInst = &_aInst[Unit];
  pInst->Unit = Unit;
  _InitGetDeviceInfo(pInst, pDevInfo);
  return 0;
}

#endif

/*********************************************************************
*
*       Public const data
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_NAND_PHY_x8
*/
const FS_NAND_PHY_TYPE FS_NAND_PHY_x8 = {
  _PHY_EraseBlock,
  _PHY_InitGetDeviceInfo_x8,
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
*       FS_NAND_PHY_x
*/
const FS_NAND_PHY_TYPE FS_NAND_PHY_x = {
  _PHY_EraseBlock,
  _PHY_InitGetDeviceInfo_x,
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
*       FS_NAND_x8_SetHWType
*
*  Function description
*    Configures the hardware access routines for a NAND physical layer
*    of type FS_NAND_PHY_x8.
*
*  Parameters
*    Unit       Index of the physical layer instance (0-based)
*    pHWType    Type of the hardware layer to use. Cannot be NULL.
*
*  Additional information
*    This function has to be called once in FS_X_AddDevices() for every
*    instance of a NAND physical layer of type FS_NAND_PHY_x8.
*/
void FS_NAND_x8_SetHWType(U8 Unit, const FS_NAND_HW_TYPE * pHWType) {
#if FS_NAND_SUPPORT_AUTO_DETECTION
  const FS_NAND_PHY_TYPE * pPhyType;
  const PHY_INFO         * pPhyInfo;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  //
  // Configure the same HW layer for all the physical layers in the list.
  //
  pPhyInfo = _aPhyList_x8;
  for (;;) {
    pPhyType = pPhyInfo->pPhyType;
    if (pPhyType == NULL) {
      break;          // OK, end of the list reached.
    }
    pPhyInfo->pfSetHWType(Unit, pHWType);
    ++pPhyInfo;
  }
#else
  NAND_X8_INST * pInst;

  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    pInst->pHWType = pHWType;
  }
#endif // FS_NAND_SUPPORT_AUTO_DETECTION
}

/*********************************************************************
*
*       FS_NAND_x_SetHWType
*
*  Function description
*    Configures the hardware access routines for a NAND physical layer
*    of type FS_NAND_PHY_x.
*
*  Parameters
*    Unit       Index of the physical layer instance (0-based)
*    pHWType    Type of the hardware layer to use. Cannot be NULL.
*
*  Additional information
*    This function is mandatory and has to be called once in
*    FS_X_AddDevices() for every instance of a NAND physical
*    layer of type FS_NAND_PHY_x.
*/
void FS_NAND_x_SetHWType(U8 Unit, const FS_NAND_HW_TYPE * pHWType) {
#if FS_NAND_SUPPORT_AUTO_DETECTION
  const FS_NAND_PHY_TYPE * pPhyType;
  const PHY_INFO         * pPhyInfo;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  //
  // Configure the same HW layer for all the physical layers in the list.
  //
  pPhyInfo = _aPhyList_x;
  for (;;) {
    pPhyType = pPhyInfo->pPhyType;
    if (pPhyType == NULL) {
      break;          // OK, end of the list reached.
    }
    pPhyInfo->pfSetHWType(Unit, pHWType);
    ++pPhyInfo;
  }
#else
  NAND_X8_INST * pInst;

  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    pInst->pHWType = pHWType;
  }
#endif // FS_NAND_SUPPORT_AUTO_DETECTION
}

#if (FS_NAND_SUPPORT_AUTO_DETECTION == 0)

/*********************************************************************
*
*       FS_NAND_x_Configure
*
*  Function description
*    Configures the parameters of the NAND flash device for a NAND
*    physical layer of type FS_NAND_PHY_x.
*
*  Parameters
*    Unit               Index of the physical layer instance (0-based)
*    NumBlocks          Total number of blocks in the NAND flash device.
*    PagesPerBlock      Total number of pages in a NAND block.
*    BytesPerPage       Number of bytes in a page without the spare area.
*    BytesPerSpareArea  Number of bytes in the spare area of a NAND page.
*    DataBusWidth       Number of data lines used for data exchange.
*
*  Additional information
*    This function is mandatory only when the file system is built with
*    FS_NAND_SUPPORT_AUTO_DETECTION set to 0 which is not the default.
*    FS_NAND_x_Configure() has to be called once in FS_X_AddDevices() for
*    each instance of the FS_NAND_PHY_x physical layer.
*    FS_NAND_x_Configure() is not available if FS_NAND_SUPPORT_AUTO_DETECTION is set to 0.
*
*    By default, the FS_NAND_PHY_x physical layer identifies the parameters
*    of the NAND flash device by evaluating the first and second byte of the reply
*    returned by the NAND flash device to the READ ID (0x90) command.
*    The identification operation is disabled if FS_NAND_SUPPORT_AUTO_DETECTION
*    set to 0 and the application must specify the NAND flash parameters via this
*    function.
*/
void FS_NAND_x_Configure(U8 Unit, unsigned NumBlocks, unsigned PagesPerBlock, unsigned BytesPerPage, unsigned BytesPerSpareArea, unsigned DataBusWidth) {
  NAND_X8_INST * pInst;

  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    pInst->NumBlocks         = (U16)NumBlocks;
    pInst->ldPagesPerBlock   = (U8)_ld(PagesPerBlock);
    pInst->ldBytesPerPage    = (U8)_ld(BytesPerPage);
    pInst->BytesPerSpareArea = (U16)BytesPerSpareArea;
    pInst->DataBusWidth      = (U8)DataBusWidth;
  }
}

/*********************************************************************
*
*       FS_NAND_x8_Configure
*
*  Function description
*    Configures the parameters of the NAND flash device for a NAND
*    physical layer of type FS_NAND_PHY_x8.
*
*  Parameters
*    Unit               Index of the physical layer instance (0-based)
*    NumBlocks          Total number of blocks in the NAND flash device.
*    PagesPerBlock      Total number of pages in a NAND block.
*    BytesPerPage       Number of bytes in a page without the spare area.
*    BytesPerSpareArea  Number of bytes in the spare area of a NAND page.
*
*  Additional information
*    This function is mandatory only when the file system is built with
*    FS_NAND_SUPPORT_AUTO_DETECTION set to 0 which is not the default.
*    FS_NAND_x_Configure() has to be called once in FS_X_AddDevices() for
*    each instance of the FS_NAND_PHY_x8 physical layer.
*    FS_NAND_x_Configure() is not available if FS_NAND_SUPPORT_AUTO_DETECTION is set to 0.
*
*    By default, the FS_NAND_PHY_x8 physical layer identifies the parameters
*    of the NAND flash device by evaluating the first and second byte of the reply
*    returned by the NAND flash device to the READ ID (0x90) command. The identification
*    operation is disabled if FS_NAND_SUPPORT_AUTO_DETECTION set to 0 and
*    the application must specify the NAND flash parameters via this function.
*/
void FS_NAND_x8_Configure(U8 Unit, unsigned NumBlocks, unsigned PagesPerBlock, unsigned BytesPerPage, unsigned BytesPerSpareArea) {
  NAND_X8_INST * pInst;

  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    pInst->NumBlocks         = (U16)NumBlocks;
    pInst->ldPagesPerBlock   = (U8)_ld(PagesPerBlock);
    pInst->ldBytesPerPage    = (U8)_ld(BytesPerPage);
    pInst->BytesPerSpareArea = (U16)BytesPerSpareArea;
    pInst->DataBusWidth      = 8;
  }
}

#endif // FS_NAND_SUPPORT_AUTO_DETECTION == 0

/*************************** End of file ****************************/
