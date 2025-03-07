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
File        : FS_NOR_PHY_SPIFI.c
Purpose     : Low level flash driver for NOR SPI flash connected via a memory-mapped SPI interface.
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
#define MAX_NUM_BYTES_CMD           2

/*********************************************************************
*
*       ASSERT_SECTOR_INDEX_IS_IN_RANGE
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_SECTOR_INDEX_IS_IN_RANGE(pInst, SectorIndex)                                                                               \
    if ((SectorIndex) >= (pInst)->NumSectors) {                                                                                             \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPIFI: Invalid sector index (%lu not in [0, %lu])", SectorIndex, (pInst)->NumSectors - 1u)); \
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);                                                                                                  \
    }
#else
  #define ASSERT_SECTOR_INDEX_IS_IN_RANGE(pInst, SectorIndex)
#endif

/*********************************************************************
*
*       ASSERT_UNIT_NO_IS_IN_RANGE
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)                                                                                    \
    if ((Unit) >= (U8)FS_NOR_NUM_UNITS) {                                                                                     \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPIFI: Invalid unit number (%d not in [0, %d])", Unit, FS_NOR_NUM_UNITS - 1)); \
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);                                                                                    \
    }
#else
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)
#endif

/*********************************************************************
*
*       ASSERT_HW_TYPE_IS_SET
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_HW_TYPE_IS_SET(pInst)                                       \
    if ((pInst)->pHWType == NULL) {                                          \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR_SPIFI: HW layer not set.")); \
      FS_X_PANIC(FS_ERRCODE_HW_LAYER_NOT_SET);                               \
    }
#else
  #define ASSERT_HW_TYPE_IS_SET(pInst)
#endif

/*********************************************************************
*
*      Local data types
*
**********************************************************************
*/

/*********************************************************************
*
*      NOR_SPIFI_INST
*/
typedef struct {
  const FS_NOR_SPI_DEVICE_LIST      * pDeviceList;            // List of supported devices.
  const FS_NOR_HW_TYPE_SPIFI        * pHWType;                // HW access routines.
  const FS_NOR_SPI_DEVICE_PARA_LIST * pDeviceParaList;        // List of device parameters specified by the application.
  FS_NOR_SPI_POLL_PARA                PollParaSectorErase;    // Polling parameters for the sector erase operation.
  FS_NOR_SPI_POLL_PARA                PollParaPageWrite;      // Polling parameters for the page write operation.
  FS_NOR_SPI_DEVICE                   Device;                 // NOR device related information.
  U32                                 NumSectors;             // Number of sectors to be used as storage.
  U32                                 BaseAddr;               // Start address of the NOR flash.
  U32                                 StartAddrConf;          // Configured start address.
  U32                                 StartAddrUsed;          // Start address actually used (aligned to the start of a physical sector).
  U32                                 NumBytes;               // Number of bytes to be used as storage.
  U32                                 Delay1ms;               // Number of software cycles to block the execution for about 1 ms.
  U8                                  Unit;                   // Index of the HW layer to be used for the data transfer.
  U8                                  IsInited;               // Set to 1 if the driver instance has been initialized.
} NOR_SPIFI_INST;

/*********************************************************************
*
*      Static data
*
**********************************************************************
*/
static NOR_SPIFI_INST * _apInst[FS_NOR_NUM_UNITS];

/*********************************************************************
*
*      Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _ld
*/
static unsigned _ld(U32 Value) {
  unsigned i;

  for (i = 0; i < 32u; i++) {
    if ((1uL << i) == Value) {
      break;
    }
  }
  return i;
}

/*********************************************************************
*
*      _Unmap
*
*  Function description
*    Configures the hardware for direct access to serial NOR flash.
*
*  Parameters
*    pInst          Driver instance.
*/
static void _Unmap(const NOR_SPIFI_INST * pInst) {
  U8 Unit;

  Unit = pInst->Unit;
  if (pInst->pHWType->pfSetCmdMode != NULL) {
    pInst->pHWType->pfSetCmdMode(Unit);
  }
}

/*********************************************************************
*
*      _Map
*
*  Function description
*    Configures the hardware for access to serial NOR flash via system memory.
*
*  Parameters
*    pInst          Driver instance.
*    pCmd           [IN] Code of the command to be used for reading the data. Cannot be NULL.
*    NumBytesCmd    Number of bytes in the command code. Cannot be 0.
*    pPara          [IN] Command parameters (address and dummy bytes). Can be NULL.
*    NumBytesPara   Total number of address and dummy bytes to be sent. Can be 0.
*    NumBytesAddr   Number of address bytes to be send. Can be 0.
*    BusWidth       Number of data lines to be used for the data transfer.
*    Flags          Options for the data exchange.
*
*  Return value
*    ==0      OK, memory mode configured successfully.
*    !=0      An error occurred.
*/
static int _Map(const NOR_SPIFI_INST * pInst, const U8 * pCmd, unsigned NumBytesCmd, const U8 * pPara, unsigned NumBytesPara, unsigned NumBytesAddr, unsigned BusWidth, unsigned Flags) {
  U8       Unit;
  unsigned NumBytesDummy;
  int      r;

  r    = 0;
  Unit = pInst->Unit;
  if (pInst->pHWType->pfMapEx != NULL) {
    r = pInst->pHWType->pfMapEx(Unit, pCmd, NumBytesCmd, pPara, NumBytesPara, NumBytesAddr, (U16)BusWidth, Flags);
  } else {
    if (pInst->pHWType->pfSetMemMode != NULL) {
      FS_DEBUG_ASSERT(FS_MTYPE_DRIVER, NumBytesCmd == 1u);
      NumBytesDummy = NumBytesPara - NumBytesAddr;
      pInst->pHWType->pfSetMemMode(Unit, *pCmd, NumBytesAddr, NumBytesDummy, (U16)BusWidth);
    }
  }
  return r;
}

/*********************************************************************
*
*      _IsMappingSupported
*
*   Function description
*     Checks if the physical layer supports memory mapping of the NOR flash contents.
*
*  Parameters
*    pInst          Driver instance.
*
*  Return value
*    !=0      Memory mapping supported.
*    ==0      Memory mapping not supported.
*/
static int _IsMappingSupported(const NOR_SPIFI_INST * pInst) {
  int r;

  r = 1;
  if (   (pInst->pHWType->pfSetMemMode == NULL)
      && (pInst->pHWType->pfMapEx      == NULL)) {
    r = 0;
  }
  return r;
}

/*********************************************************************
*
*      _Control
*
*  Function description
*    Sends a command to serial NOR flash that does not transfer any data.
*
*  Parameters
*    pInst          Driver instance.
*    pCmd           [IN] Code of the command to be sent. Cannot be NULL.
*    NumBytesCmd    Number of bytes in the command code. Cannot be 0.
*    BusWidth       Number of data lines to be used for sending the command.
*    Flags          Options for the data exchange.
*
*  Return value
*    ==0      OK, command transferred successfully.
*    !=0      An error occurred.
*/
static int _Control(const NOR_SPIFI_INST * pInst, const U8 * pCmd, unsigned NumBytesCmd, unsigned BusWidth, unsigned Flags) {
  U8  Unit;
  int r;

  r    = 0;
  Unit = pInst->Unit;
  if (pInst->pHWType->pfControlEx != NULL) {
    r = pInst->pHWType->pfControlEx(Unit, pCmd, NumBytesCmd, (U8)BusWidth, Flags);
  } else {
    FS_DEBUG_ASSERT(FS_MTYPE_DRIVER, NumBytesCmd == 1u);
    pInst->pHWType->pfExecCmd(Unit, *pCmd, (U8)BusWidth);
  }
  return r;
}

/*********************************************************************
*
*      _Write
*
*  Function description
*    Sends a command that transfers data from MCU to serial NOR flash.
*
*  Parameters
*    pInst          Driver instance.
*    pCmd           [IN] Code of the command to be sent. Cannot be NULL.
*    NumBytesCmd    Number of bytes in the command code. Cannot be 0.
*    pPara          [IN] Command parameters (address and dummy bytes). Can be NULL.
*    NumBytesPara   Total number of address and dummy bytes to be sent. Can be 0.
*    NumBytesAddr   Number of address bytes to be send. Can be 0.
*    pData          [IN] Data to be sent to the serial NOR flash device. Can be NULL.
*    NumBytesData   Number of bytes to be written the serial NOR flash device. Can be 0.
*    BusWidth       Number of data lines to be used for the data transfer.
*    Flags          Options for the data exchange.
*
*  Return value
*    ==0      OK, data transferred successfully.
*    !=0      An error occurred.
*/
static int _Write(const NOR_SPIFI_INST * pInst, const U8 * pCmd, unsigned NumBytesCmd, const U8 * pPara, unsigned NumBytesPara, unsigned NumBytesAddr, const U8 * pData, unsigned NumBytesData, unsigned BusWidth, unsigned Flags) {
  U8  Unit;
  int r;

  r    = 0;
  Unit = pInst->Unit;
  if (pInst->pHWType->pfWriteEx != NULL) {
    r = pInst->pHWType->pfWriteEx(Unit, pCmd, NumBytesCmd, pPara, NumBytesPara, NumBytesAddr, pData, NumBytesData, (U16)BusWidth, Flags);
  } else {
    FS_DEBUG_ASSERT(FS_MTYPE_DRIVER, NumBytesCmd == 1u);
    pInst->pHWType->pfWriteData(Unit, *pCmd, pPara, NumBytesPara, NumBytesAddr, pData, NumBytesData, (U16)BusWidth);
  }
  return r;
}

/*********************************************************************
*
*      _Read
*
*  Function description
*    Sends a command that transfers data from serial NOR flash to MCU.
*
*  Parameters
*    pInst          Driver instance.
*    pCmd           [IN] Code of the command to be sent. Cannot be NULL.
*    NumBytesCmd    Number of bytes in the command code. Cannot be 0.
*    pPara          [IN] Command parameters (address and dummy bytes). Can be NULL.
*    NumBytesPara   Total number of address and dummy bytes to be sent. Can be 0.
*    NumBytesAddr   Number of address bytes to be send. Can be 0.
*    pData          [OUT] Data read from the serial NOR flash device. Can be NULL.
*    NumBytesData   Number of bytes to read from the serial NOR flash device. Can be 0.
*    BusWidth       Number of data lines to be used for the data transfer.
*    Flags          Options for the data exchange.
*
*  Return value
*    ==0      OK, data transferred successfully.
*    !=0      An error occurred.
*/
static int _Read(const NOR_SPIFI_INST * pInst, const U8 * pCmd, unsigned NumBytesCmd, const U8 * pPara, unsigned NumBytesPara, unsigned NumBytesAddr, U8 * pData, unsigned NumBytesData, unsigned BusWidth, unsigned Flags) {
  U8  Unit;
  int r;

  r    = 0;
  Unit = pInst->Unit;
  if (pInst->pHWType->pfReadEx != NULL) {
    r = pInst->pHWType->pfReadEx(Unit, pCmd, NumBytesCmd, pPara, NumBytesPara, NumBytesAddr, pData, NumBytesData, (U16)BusWidth, Flags);
  } else {
    FS_DEBUG_ASSERT(FS_MTYPE_DRIVER, NumBytesCmd == 1u);
    pInst->pHWType->pfReadData(Unit, *pCmd, pPara, NumBytesPara, NumBytesAddr, pData, NumBytesData, (U16)BusWidth);
  }
  return r;
}

/*********************************************************************
*
*      _Poll
*
*  Function description
*    Checks periodically the value of a status flag.
*
*  Parameters
*    pInst          Driver instance.
*    pCmd           [IN] Code of the command to be sent. Cannot be NULL.
*    NumBytesCmd    Number of bytes in the command code. Cannot be 0.
*    pPara          [IN] Command parameters (address and dummy bytes). Can be NULL.
*    NumBytesPara   Total number of address and dummy bytes to be sent. Can be 0.
*    NumBytesAddr   Number of address bytes to be send. Can be 0.
*    BitPos         Position of the bit to be checked.
*    BitValue       Value of the bit to wait for.
*    Delay          Number of clock cycles to wait between two queries.
*    TimeOut_ms     Maximum number of milliseconds to wait for the bit to be set.
*    BusWidth       Number of data lines to be used for the data transfer.
*    Flags          Options for the data exchange.
*
*  Return value
*    > 0    Timeout occurred.
*    ==0    OK, bit set to specified value.
*    < 0    Feature not supported.
*/
static int _Poll(const NOR_SPIFI_INST * pInst, const U8 * pCmd, unsigned NumBytesCmd, const U8 * pPara, unsigned NumBytesPara, unsigned NumBytesAddr, U8 BitPos, U8 BitValue, U32 Delay, U32 TimeOut_ms, unsigned BusWidth, unsigned Flags) {
  U8  Unit;
  int r;

  r    = -1;                      // Set to indicate that the feature is not supported.
  Unit = pInst->Unit;
  if (pInst->pHWType->pfPollEx != NULL) {
    r = pInst->pHWType->pfPollEx(Unit, pCmd, NumBytesCmd, pPara, NumBytesPara, NumBytesAddr, BitPos, BitValue, Delay, TimeOut_ms, (U16)BusWidth, Flags);
  } else {
    if (pInst->pHWType->pfPoll != NULL) {
      FS_DEBUG_ASSERT(FS_MTYPE_DRIVER, NumBytesCmd == 1u);
      r = pInst->pHWType->pfPoll(Unit, *pCmd, BitPos, BitValue, Delay, TimeOut_ms, (U16)BusWidth);
    }
  }
  return r;
}

/*********************************************************************
*
*      _Delay
*
*  Function description
*    Blocks the execution for the specified time.
*
*  Parameters
*    pInst          Driver instance.
*    ms             Number of milliseconds to block the execution.
*
*  Return value
*    ==0    OK, delay executed.
*    < 0    Functionality not supported.
*/
static int _Delay(const NOR_SPIFI_INST * pInst, U32 ms) {
  U8  Unit;
  int r;

  r    = -1;                      // Set to indicate that the feature is not supported.
  Unit = pInst->Unit;
  if (pInst->pHWType->pfDelay != NULL) {
    r = pInst->pHWType->pfDelay(Unit, ms);
  }
  return r;
}

/*********************************************************************
*
*      _Lock
*
*  Function description
*    Requests exclusive access to SPI bus.
*
*  Parameters
*    pInst          Driver instance.
*/
static void _Lock(const NOR_SPIFI_INST * pInst) {
  U8 Unit;

  Unit = pInst->Unit;
  if (pInst->pHWType->pfLock != NULL) {
    pInst->pHWType->pfLock(Unit);
  }
}

/*********************************************************************
*
*      _Unlock
*
*  Function description
*    Requests exclusive access to SPI bus.
*
*  Parameters
*    pInst          Driver instance.
*/
static void _Unlock(const NOR_SPIFI_INST * pInst) {
  U8 Unit;

  Unit = pInst->Unit;
  if (pInst->pHWType->pfUnlock != NULL) {
    pInst->pHWType->pfUnlock(Unit);
  }
}

/*********************************************************************
*
*      _EnterCmdMode
*
*  Function description
*    Disables the memory mapped access and enables the direct access to the NOR flash device.
*
*  Parameters
*    pInst          Driver instance.
*/
static void _EnterCmdMode(const NOR_SPIFI_INST * pInst) {
  _Unmap(pInst);
}

/*********************************************************************
*
*      _LeaveCmdMode
*
*  Function description
*    Disables the direct access and enables the memory mapped access to the NOR flash device.
*
*  Parameters
*    pInst          Driver instance.
*
*  Return value
*    ==0      OK, command mode left successfully.
*    !=0      An error occurred.
*/
static int _LeaveCmdMode(const NOR_SPIFI_INST * pInst) {
  unsigned Cmd;
  unsigned CmdEx;
  unsigned BusWidth;
  unsigned NumBytesAddr;
  unsigned NumBytesDummy;
  unsigned NumBytesPara;
  unsigned NumBytesCmd;
  U8       abCmd[MAX_NUM_BYTES_CMD];
  unsigned Flags;
  int      r;

  //
  // Fill the local variables.
  //
  Cmd           = pInst->Device.Inst.CmdRead;
  CmdEx         = pInst->Device.Inst.CmdReadEx;
  NumBytesAddr  = pInst->Device.Inst.NumBytesAddr;
  NumBytesDummy = pInst->Device.Inst.NumBytesReadDummy;
  BusWidth      = pInst->Device.Inst.BusWidthRead;
  Flags         = pInst->Device.Inst.FlagsRead;
  NumBytesPara  = NumBytesAddr + NumBytesDummy;
  //
  // Encode the read command.
  //
  NumBytesCmd = 0;
  abCmd[NumBytesCmd++] = (U8)Cmd;
  if (CmdEx != 0u) {
    abCmd[NumBytesCmd++] = (U8)CmdEx;
  }
  //
  // Switch to access via the system memory.
  //
  r = _Map(pInst, abCmd, NumBytesCmd, NULL, NumBytesPara, NumBytesAddr, BusWidth, Flags);
  return r;
}

/*********************************************************************
*
*      _CMD_Control
*
*  Function description
*    Sends a command to NOR flash.
*
*  Parameters
*    pContext       Driver instance.
*    Cmd            Command code.
*    BusWidth       Number of data lines to be used (encoded)
*
*  Return value
*    ==0      OK, command sent successfully.
*    !=0      An error occurred.
*/
static int _CMD_Control(void * pContext, U8 Cmd, unsigned BusWidth) {
  NOR_SPIFI_INST * pInst;
  int              r;

  pInst = SEGGER_PTR2PTR(NOR_SPIFI_INST, pContext);                               // MISRA deviation D:100a
  r = _Control(pInst, &Cmd, sizeof(Cmd), FS_BUSWIDTH_GET_CMD(BusWidth), 0);
  return r;
}

/*********************************************************************
*
*      _CMD_Write
*
*  Function description
*    Sends a command that transfers data from MCU to NOR flash device.
*
*  Parameters
*    pContext       Driver instance.
*    Cmd            Command code.
*    pData          [IN] Data to be transferred to NOR flash device.
*    NumBytes       Number of bytes to be transferred.
*    BusWidth       Number of data lines to be used for the data transfer.
*
*  Return value
*    ==0      OK, data transferred successfully.
*    !=0      An error occurred.
*/
static int _CMD_Write(void * pContext, U8 Cmd, const U8 * pData, unsigned NumBytes, unsigned BusWidth) {
  NOR_SPIFI_INST * pInst;
  int              r;

  pInst = SEGGER_PTR2PTR(NOR_SPIFI_INST, pContext);                               // MISRA deviation D:100a
  r = _Write(pInst, &Cmd, sizeof(Cmd), NULL, 0, 0, pData, NumBytes, BusWidth, 0);
  return r;
}

/*********************************************************************
*
*      _CMD_Read
*
*  Function description
*    Sends a command that transfers data from NOR flash device to MCU.
*
*  Parameters
*    pContext       Driver instance.
*    Cmd            Command code.
*    pData          [OUT] Data transferred form NOR flash device.
*    NumBytes       Number of bytes transferred.
*    BusWidth       Number of data lines to be used for the data transfer.
*
*  Return value
*    ==0      OK, data transferred successfully.
*    !=0      An error occurred.
*/
static int _CMD_Read(void * pContext, U8 Cmd, U8 * pData, unsigned NumBytes, unsigned BusWidth) {
  NOR_SPIFI_INST * pInst;
  int              r;

  pInst = SEGGER_PTR2PTR(NOR_SPIFI_INST, pContext);                               // MISRA deviation D:100a
  r = _Read(pInst, &Cmd, sizeof(Cmd), NULL, 0, 0, pData, NumBytes, BusWidth, 0);
  return r;
}

/*********************************************************************
*
*      _CMD_WriteWithAddr
*
*  Function description
*    Sends a command that transfers data from MCU to NOR flash device.
*
*  Parameters
*    pContext       Driver instance.
*    Cmd            Command code.
*    pPara          [IN] Command parameters.
*    NumBytesPara   Number of parameter bytes.
*    NumBytesAddr   Number of address bytes.
*    pData          [IN] Data to be transferred to NOR flash device.
*    NumBytesData   Number of bytes to be transferred.
*    BusWidth       Number of data lines to be used for the data transfer.
*
*  Return value
*    ==0      OK, data transferred successfully.
*    !=0      An error occurred.
*/
static int _CMD_WriteWithAddr(void * pContext, U8 Cmd, const U8 * pPara, unsigned NumBytesPara, unsigned NumBytesAddr, const U8 * pData, unsigned NumBytesData, unsigned BusWidth) {
  NOR_SPIFI_INST * pInst;
  int              r;

  pInst = SEGGER_PTR2PTR(NOR_SPIFI_INST, pContext);                               // MISRA deviation D:100a
  r = _Write(pInst, &Cmd, sizeof(Cmd), pPara, NumBytesPara, NumBytesAddr, pData, NumBytesData, BusWidth, 0);
  return r;
}

/*********************************************************************
*
*      _CMD_ReadWithAddr
*
*  Function description
*    Sends a command that transfers data from NOR flash device to MCU.
*
*  Parameters
*    pContext       Driver instance.
*    Cmd            Command code.
*    pPara          [IN] Command parameters.
*    NumBytesPara   Number of parameter bytes.
*    NumBytesAddr   Number of address bytes.
*    pData          [OUT] Data transferred from NOR flash device.
*    NumBytesData   Number of bytes transferred.
*    BusWidth       Number of data lines to be used for the data transfer.
*
*  Return value
*    ==0      OK, data transferred successfully.
*    !=0      An error occurred.
*/
static int _CMD_ReadWithAddr(void * pContext, U8 Cmd, const U8 * pPara, unsigned NumBytesPara, unsigned NumBytesAddr, U8 * pData, unsigned NumBytesData, unsigned BusWidth) {
  NOR_SPIFI_INST * pInst;
  int              r;

  pInst = SEGGER_PTR2PTR(NOR_SPIFI_INST, pContext);                               // MISRA deviation D:100a
  r = _Read(pInst, &Cmd, sizeof(Cmd), pPara, NumBytesPara, NumBytesAddr, pData, NumBytesData, BusWidth, 0);
  return r;
}

/*********************************************************************
*
*      _CMD_Poll
*
*  Function description
*    Checks periodically the value of a status flag.
*
*  Parameters
*    pContext       Driver instance.
*    Cmd            Command code.
*    BitPos         Position of the bit to be checked.
*    BitValue       Value of the bit to be checked.
*    Delay          Number of clock cycles to wait between two queries.
*    TimeOut_ms     Maximum number of milliseconds to wait for the bit to be set.
*    BusWidth       Number of data lines to be used for the data transfer.
*
*  Return value
*    > 0    Timeout occurred.
*    ==0    OK, bit set to specified value.
*    < 0    Feature not supported.
*/
static int _CMD_Poll(void * pContext, U8 Cmd, U8 BitPos, U8 BitValue, U32 Delay, U32 TimeOut_ms, unsigned BusWidth) {
  NOR_SPIFI_INST * pInst;
  int              r;

  pInst = SEGGER_PTR2PTR(NOR_SPIFI_INST, pContext);                               // MISRA deviation D:100a
  r = _Poll(pInst, &Cmd, sizeof(Cmd), NULL, 0, 0, BitPos, BitValue, Delay, TimeOut_ms, BusWidth, 0);
  return r;
}

/*********************************************************************
*
*      _CMD_Delay
*
*  Function description
*    Blocks the execution for a specified number of milliseconds.
*
*  Parameters
*    pContext       Driver instance.
*    ms             Number of milliseconds to block the execution.
*
*  Return value
*    ==0    OK, delay executed.
*    < 0    Functionality not supported.
*/
static int _CMD_Delay(void * pContext, unsigned ms) {
  NOR_SPIFI_INST * pInst;
  int             r;

  pInst = SEGGER_PTR2PTR(NOR_SPIFI_INST, pContext);                               // MISRA deviation D:100a
  r = _Delay(pInst, ms);
  return r;
}

/*********************************************************************
*
*      _CMD_ControlWithCmdEx
*
*  Function description
*    Sends a command to NOR flash.
*
*  Parameters
*    pContext       Driver instance.
*    pCmd           [IN] Command code.
*    NumBytesCmd    Number of bytes in the command code.
*    BusWidth       Number of data lines to be used (encoded)
*    Flags          Command options.
*
*  Return value
*    ==0      OK, command sent successfully.
*    !=0      An error occurred.
*/
static int _CMD_ControlWithCmdEx(void * pContext, const U8 * pCmd, unsigned NumBytesCmd, unsigned BusWidth, unsigned Flags) {
  NOR_SPIFI_INST * pInst;
  int              r;

  pInst = SEGGER_PTR2PTR(NOR_SPIFI_INST, pContext);                               // MISRA deviation D:100a
  r = _Control(pInst, pCmd, NumBytesCmd, FS_BUSWIDTH_GET_CMD(BusWidth), Flags);
  return r;
}

/*********************************************************************
*
*      _CMD_WriteWithCmdEx
*
*  Function description
*    Sends a command that transfers data from MCU to NOR flash device.
*
*  Parameters
*    pContext       Driver instance.
*    pCmd           [IN] Command code.
*    NumBytesCmd    Number of bytes the in command code.
*    pData          [IN] Data to be transferred to NOR flash device.
*    NumBytesData   Number of bytes to be transferred.
*    BusWidth       Number of data lines to be used for the data transfer.
*    Flags          Command options.
*
*  Return value
*    ==0      OK, data transferred successfully.
*    !=0      An error occurred.
*/
static int _CMD_WriteWithCmdEx(void * pContext, const U8 * pCmd, unsigned NumBytesCmd, const U8 * pData, unsigned NumBytesData, unsigned BusWidth, unsigned Flags) {
  NOR_SPIFI_INST * pInst;
  int              r;

  pInst = SEGGER_PTR2PTR(NOR_SPIFI_INST, pContext);                               // MISRA deviation D:100a
  r = _Write(pInst, pCmd, NumBytesCmd, NULL, 0, 0, pData, NumBytesData, BusWidth, Flags);
  return r;
}

/*********************************************************************
*
*      _CMD_WriteWithCmdExAndAddr
*
*  Function description
*    Sends a command that transfers data from MCU to NOR flash device.
*
*  Parameters
*    pContext       Driver instance.
*    pCmd           [IN] Command code.
*    NumBytesCmd    Number of bytes the in command code.
*    pPara          [IN] Command parameters.
*    NumBytesPara   Number of parameter bytes.
*    NumBytesAddr   Number of address bytes.
*    pData          [IN] Data to be transferred to NOR flash device.
*    NumBytesData   Number of bytes transferred.
*    BusWidth       Number of data lines to be used for the data transfer.
*    Flags          Command options.
*
*  Return value
*    ==0      OK, data transferred successfully.
*    !=0      An error occurred.
*/
static int _CMD_WriteWithCmdExAndAddr(void * pContext, const U8 * pCmd, unsigned NumBytesCmd, const U8 * pPara, unsigned NumBytesPara, unsigned NumBytesAddr, const U8 * pData, unsigned NumBytesData, unsigned BusWidth, unsigned Flags) {
  NOR_SPIFI_INST * pInst;
  int              r;

  pInst = SEGGER_PTR2PTR(NOR_SPIFI_INST, pContext);                               // MISRA deviation D:100a
  r = _Write(pInst, pCmd, NumBytesCmd, pPara, NumBytesPara, NumBytesAddr, pData, NumBytesData, BusWidth, Flags);
  return r;
}

/*********************************************************************
*
*      _CMD_ReadWithCmdExAndAddr
*
*  Function description
*    Sends a command that transfers data from NOR flash device to MCU.
*
*  Parameters
*    pContext       Driver instance.
*    pCmd           [IN] Command code.
*    NumBytesCmd    Number of bytes the in command code.
*    pPara          [IN] Command parameters.
*    NumBytesPara   Number of parameter bytes.
*    NumBytesAddr   Number of address bytes.
*    pData          [OUT] Data transferred from NOR flash device.
*    NumBytesData   Number of bytes transferred.
*    BusWidth       Number of data lines to be used for the data transfer.
*    Flags          Command options.
*
*  Return value
*    ==0      OK, data transferred successfully.
*    !=0      An error occurred.
*/
static int _CMD_ReadWithCmdExAndAddr(void * pContext, const U8 * pCmd, unsigned NumBytesCmd, const U8 * pPara, unsigned NumBytesPara, unsigned NumBytesAddr, U8 * pData, unsigned NumBytesData, unsigned BusWidth, unsigned Flags) {
  NOR_SPIFI_INST * pInst;
  int              r;

  pInst = SEGGER_PTR2PTR(NOR_SPIFI_INST, pContext);                               // MISRA deviation D:100a
  r = _Read(pInst, pCmd, NumBytesCmd, pPara, NumBytesPara, NumBytesAddr, pData, NumBytesData, BusWidth, Flags);
  return r;
}

/*********************************************************************
*
*      _CMD_PollWithCmdEx
*
*  Function description
*    Checks periodically the value of a status flag.
*
*  Parameters
*    pContext       Driver instance.
*    pCmd           [IN] Command code.
*    NumBytesCmd    Number of bytes the in command code.
*    pPara          [IN] Command parameters.
*    NumBytesPara   Number of parameter bytes.
*    NumBytesAddr   Number of address bytes.
*    BitPos         Position of the bit to be checked.
*    BitValue       Value of the bit to be checked.
*    Delay          Number of clock cycles to wait between two queries.
*    TimeOut_ms     Maximum number of milliseconds to wait for the bit to be set.
*    BusWidth       Number of data lines to be used for the data transfer.
*    Flags          Command options.
*
*  Return value
*    > 0    Timeout occurred.
*    ==0    OK, bit set to specified value.
*    < 0    Feature not supported.
*/
static int _CMD_PollWithCmdEx(void * pContext, const U8 * pCmd, unsigned NumBytesCmd, const U8 * pPara, unsigned NumBytesPara, unsigned NumBytesAddr, U8 BitPos, U8 BitValue, U32 Delay, U32 TimeOut_ms, unsigned BusWidth, unsigned Flags) {
  NOR_SPIFI_INST * pInst;
  int              r;

  pInst = SEGGER_PTR2PTR(NOR_SPIFI_INST, pContext);                               // MISRA deviation D:100a
  r = _Poll(pInst, pCmd, NumBytesCmd, pPara, NumBytesPara, NumBytesAddr, BitPos, BitValue, Delay, TimeOut_ms, BusWidth, Flags);
  return r;
}

/*********************************************************************
*
*      _Cmd
*/
static const FS_NOR_SPI_CMD _Cmd = {
  _CMD_Control,
  _CMD_Write,
  _CMD_Read,
  _CMD_WriteWithAddr,
  _CMD_ReadWithAddr,
  _CMD_Poll,
  _CMD_Delay,
  _CMD_ControlWithCmdEx,
  _CMD_WriteWithCmdEx,
  _CMD_WriteWithCmdExAndAddr,
  _CMD_ReadWithCmdExAndAddr,
  _CMD_PollWithCmdEx
};

/*********************************************************************
*
*      _CalcStorageArea
*
*  Function description
*    Determines which physical sectors are used as storage.
*/
static int _CalcStorageArea(NOR_SPIFI_INST * pInst) {
  U32                       NumSectors;
  U32                       BytesPerSector;
  I32                       NumBytesToSkip;
  I32                       NumBytesRem;
  U32                       NumBytes;
  U32                       NumBytesBlock;
  U32                       NumBytesSkipped;
  U8                        ldBytesPerSector;
  unsigned                  NumSectorBlocksConf;
  unsigned                  NumSectorBlocksUsed;
  U32                       NumSectorsRem;
  U32                       NumSectorsTotal;
  FS_NOR_SPI_SECTOR_BLOCK * pSectorBlockConf;
  FS_NOR_SPI_SECTOR_BLOCK * pSectorBlockUsed;

  pSectorBlockConf    = pInst->Device.Inst.aSectorBlock;
  pSectorBlockUsed    = pInst->Device.Inst.aSectorBlock;
  NumSectorBlocksConf = pInst->Device.Inst.NumSectorBlocks;
  NumSectorBlocksUsed = 0;
  NumSectorsTotal     = 0;
  NumBytesToSkip      = (I32)pInst->StartAddrConf - (I32)pInst->BaseAddr;
  NumBytesSkipped     = 0;
  NumBytesRem         = (I32)pInst->NumBytes;
  NumBytes            = 0;
  if (NumSectorBlocksConf == 0u) {
    return 1;                     // Error, invalid number of sectors.
  }
  do {
    ldBytesPerSector = pSectorBlockConf->ldBytesPerSector;
    NumSectors       = pSectorBlockConf->NumSectors;
    BytesPerSector   = 1uL << ldBytesPerSector;
    //
    // Take care of bytes to skip before storage area.
    //
    while ((NumSectors != 0u) && (NumBytesToSkip > 0)) {
      NumBytesToSkip  -= (I32)BytesPerSector;
      NumBytesSkipped += BytesPerSector;
      NumSectors--;
    }
    if (NumSectors != 0u) {
      NumSectorsRem = (U32)NumBytesRem >> ldBytesPerSector;
      if (NumSectors > NumSectorsRem) {
        NumSectors   = NumSectorsRem;
      }
      NumBytesBlock  = NumSectors << ldBytesPerSector;
      NumBytesRem   -= (I32)NumBytesBlock;
      NumBytes      += NumBytesBlock;         // Update the actual number of bytes used as storage.
      //
      // Take care of bytes to skip after data area
      //
      if (NumSectors != 0u) {
        pSectorBlockUsed->ldBytesPerSector = ldBytesPerSector;
        pSectorBlockUsed->NumSectors       = NumSectors;
        ++NumSectorBlocksUsed;
        ++pSectorBlockUsed;
        NumSectorsTotal += NumSectors;
      }
    }
    ++pSectorBlockConf;
  } while (--NumSectorBlocksConf != 0u);
  if (NumSectorBlocksUsed == 0u) {
    return 1;                       // Error, Flash size too small for this configuration
  }
  pInst->Device.Inst.NumSectorBlocks = (U8)NumSectorBlocksUsed;
  pInst->NumSectors                  = NumSectorsTotal;
  pInst->StartAddrUsed               = NumBytesSkipped;
  pInst->NumBytes                    = NumBytes;
  return 0;                                   // OK, storage area determined.
}

/*********************************************************************
*
*      _InitDevice
*/
static void _InitDevice(NOR_SPIFI_INST * pInst) {
  FS_NOR_SPI_DEVICE * pDevice;

  pDevice = &pInst->Device;
  pDevice->pType->pfInit(&pDevice->Inst);
}

/*********************************************************************
*
*      _SetBusWidth
*
*  Function description
*    Configures the number of data lines for the data transfer.
*
*  Parameters
*    pInst      Driver instance.
*
*  Return value
*    ==0      OK, configured successfully.
*    !=0      An error occurred.
*/
static int _SetBusWidth(NOR_SPIFI_INST * pInst) {
  int                 r;
  FS_NOR_SPI_DEVICE * pDevice;

  r       = 0;
  pDevice = &pInst->Device;
  if (pDevice->pType->pfSetBusWidth != NULL) {
    r = pDevice->pType->pfSetBusWidth(&pDevice->Inst);
  }
  return r;
}

/*********************************************************************
*
*      _SetNumBytesAddr
*
*  Function description
*    Configures the number of address bytes.
*
*  Parameters
*    pInst      Driver instance.
*
*  Return value
*    ==0    OK, successfully configured.
*    !=0    An error occurred.
*/
static int _SetNumBytesAddr(NOR_SPIFI_INST * pInst) {
  int                 r;
  FS_NOR_SPI_DEVICE * pDevice;

  r       = 0;
  pDevice = &pInst->Device;
  if (pDevice->pType->pfSetNumBytesAddr != NULL) {
    r = pDevice->pType->pfSetNumBytesAddr(&pDevice->Inst);
  }
  return r;
}

/*********************************************************************
*
*      _RemoveWriteProtection
*/
static int _RemoveWriteProtection(NOR_SPIFI_INST * pInst, U32 StartAddr, U32 NumBytes) {
  int                 r;
  FS_NOR_SPI_DEVICE * pDevice;

  r       = 0;
  pDevice = &pInst->Device;
  if (pDevice->pType->pfRemoveWriteProtection != NULL) {
    r = pDevice->pType->pfRemoveWriteProtection(&pDevice->Inst, StartAddr, NumBytes);
  }
  return r;
}

/*********************************************************************
*
*      _SetNumCyclesDummy
*
*  Function description
*    Configures the number of dummy cycles for the memory array read operation.
*
*  Parameters
*    pInst        Driver instance.
*    Freq_Hz      Frequency supplied to NOR flash device in Hz.
*
*  Return value
*    ==0      OK, number of dummy bytes set.
*    !=0      An error occurred.
*/
static int _SetNumCyclesDummy(NOR_SPIFI_INST * pInst, U32 Freq_Hz) {
  int                 r;
  FS_NOR_SPI_DEVICE * pDevice;

  r       = 0;
  pDevice = &pInst->Device;
  if (pDevice->pType->pfSetNumCyclesDummy != NULL) {
    r = pDevice->pType->pfSetNumCyclesDummy(&pDevice->Inst, Freq_Hz);
  }
  return r;
}

/*********************************************************************
*
*      _WritePage
*/
static int _WritePage(NOR_SPIFI_INST * pInst, U32 Addr, const U8 * pData, U32 NumBytes) {
  int                 r;
  FS_NOR_SPI_DEVICE * pDevice;

  pDevice = &pInst->Device;
  r = pDevice->pType->pfWritePage(&pDevice->Inst, Addr, pData, NumBytes);
  return r;
}

/*********************************************************************
*
*      _WaitForEndOfOperation
*/
static int _WaitForEndOfOperation(NOR_SPIFI_INST * pInst, const FS_NOR_SPI_POLL_PARA * pPollPara) {
  int                 r;
  FS_NOR_SPI_DEVICE * pDevice;

  pDevice = &pInst->Device;
  r = pDevice->pType->pfWaitForEndOfOperation(&pDevice->Inst, pPollPara);
  return r;
}

/*********************************************************************
*
*      _ReadApplyParaBySFDP
*/
static int _ReadApplyParaBySFDP(NOR_SPIFI_INST * pInst) {
  int                 r;
  FS_NOR_SPI_DEVICE * pDevice;

  r       = 0;
  pDevice = &pInst->Device;
  if (pDevice->pType->pfReadApplyPara != NULL) {
    r = pDevice->pType->pfReadApplyPara(&pDevice->Inst);
  }
  return r;
}

/*********************************************************************
*
*      _ReadApplyParaById
*/
static int _ReadApplyParaById(NOR_SPIFI_INST * pInst) {
  int                 r;
  FS_NOR_SPI_DEVICE * pDevice;

  pDevice = &pInst->Device;
  r = FS_NOR_SPI_ReadApplyParaById(&pDevice->Inst);
  return r;
}

/*********************************************************************
*
*      _IdentifyDevice
*
*  Function description
*    Tries to identify manufacturer specific features by using the
*    id information. This includes error reporting flags and settings that
*    are required to work with the NOR flash in quad mode.
*
*  Return value
*    ==0    OK, device identified
*    !=0    Could not identify device
*/
static int _IdentifyDevice(NOR_SPIFI_INST * pInst, U8 * pDeviceId, unsigned SizeOfDeviceId) {
  const FS_NOR_SPI_TYPE        *  pDevice;
  const FS_NOR_SPI_TYPE        ** ppDevice;
  const FS_NOR_SPI_DEVICE_LIST *  pDeviceList;
  int                             r;
  unsigned                        NumDevices;
  unsigned                        iDevice;

  pDevice     = NULL;
  pDeviceList = pInst->pDeviceList;
  NumDevices  = pDeviceList->NumDevices;
  ppDevice    = pDeviceList->ppDevice;
  //
  // Make sure that we access the NOR flash directly via SPI.
  //
  _EnterCmdMode(pInst);
  //
  // The information about error flags is not present in the SFDP data.
  // We use the id bytes to determine if the type of the NOR flash connected.
  //
  FS_MEMSET(pDeviceId, 0, SizeOfDeviceId);
  FS_NOR_SPI_ReadId(&pInst->Device.Inst, pDeviceId, SizeOfDeviceId);
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
    r = pDevice->pfIdentify(&pInst->Device.Inst, pDeviceId);
    if (r == 0) {
      break;                          // OK, device found.
    }
    ++ppDevice;
  }
  if (iDevice == NumDevices) {
    return 1;                         // Error, could not identify device.
  }
  pInst->Device.pType = pDevice;
  return 0;
}

/*********************************************************************
*
*      _ApplyParaConf
*
*  Function description
*    Configures the operation according to the user-provided device parameters.
*/
static void _ApplyParaConf(NOR_SPIFI_INST * pInst, const U8 * pDeviceId) {
  unsigned                            NumParas;
  unsigned                            iPara;
  unsigned                            MfgId;
  unsigned                            Density;
  U8                                  CmdWrite;
  const FS_NOR_SPI_DEVICE_PARA_LIST * pDeviceParaList;
  const FS_NOR_SPI_DEVICE_PARA      * pPara;

  pDeviceParaList = pInst->pDeviceParaList;
  if (pDeviceParaList != NULL) {
    MfgId    = pDeviceId[0];
    Density  = pDeviceId[2];
    NumParas = pDeviceParaList->NumParas;
    pPara    = pDeviceParaList->pPara;
    for (iPara = 0; iPara < NumParas; ++iPara) {
      if ((pPara->MfgId == MfgId) && (pPara->Id == Density)) {
        if (pInst->Device.Inst.Allow2bitMode != 0u) {
          CmdWrite = pPara->CmdWrite112;
          if (CmdWrite != 0u) {
            pInst->Device.Inst.CmdWrite      = CmdWrite;
            pInst->Device.Inst.BusWidthWrite = (U16)FS_BUSWIDTH_MAKE(1uL, 1uL, 2uL);
          }
          CmdWrite = pPara->CmdWrite122;
          if (CmdWrite != 0u) {
            pInst->Device.Inst.CmdWrite      = CmdWrite;
            pInst->Device.Inst.BusWidthWrite = (U16)FS_BUSWIDTH_MAKE(1uL, 2uL, 2uL);
          }
        }
        if (pInst->Device.Inst.Allow4bitMode != 0u) {
          CmdWrite = pPara->CmdWrite114;
          if (CmdWrite != 0u) {
            pInst->Device.Inst.CmdWrite      = CmdWrite;
            pInst->Device.Inst.BusWidthWrite = (U16)FS_BUSWIDTH_MAKE(1uL, 1uL, 4uL);
          }
          CmdWrite = pPara->CmdWrite144;
          if (CmdWrite != 0u) {
            pInst->Device.Inst.CmdWrite      = CmdWrite;
            pInst->Device.Inst.BusWidthWrite = (U16)FS_BUSWIDTH_MAKE(1uL, 4uL, 4uL);
          }
        }
        break;
      }
      ++pPara;
    }
  }
}

/*********************************************************************
*
*      _ReleaseFromPowerDown
*
*  Function description
*    Tries to release the NOR device from power down.
*/
static void _ReleaseFromPowerDown(NOR_SPIFI_INST * pInst) {
  const FS_NOR_SPI_TYPE        *  pDevice;
  const FS_NOR_SPI_TYPE        ** ppDevice;
  const FS_NOR_SPI_DEVICE_LIST *  pDeviceList;
  unsigned                        NumDevices;
  unsigned                        iDevice;

  pDevice     = NULL;
  pDeviceList = pInst->pDeviceList;
  NumDevices  = pDeviceList->NumDevices;
  ppDevice    = pDeviceList->ppDevice;
  for (iDevice = 0; iDevice < NumDevices; ++iDevice) {
    pDevice = *ppDevice;
    if (pDevice->pfInit != NULL) {
      pDevice->pfInit(&pInst->Device.Inst);
    }
    ++ppDevice;
  }
}

/*********************************************************************
*
*      _Init
*
*  Function description
*    Initializes the HW layer and configures the NOR flash device.
*
*  Parameters
*    pInst      Physical layer instance.
*
*  Return value
*    ==0    OK, initialization was successful.
*    !=0    An error occurred.
*/
static int _Init(NOR_SPIFI_INST * pInst) {
  U32                    Freq_Hz;
  U32                    StartAddr;
  U32                    NumBytes;
  U8                     Unit;
  int                    r;
  U32                    TimeOutSectorErase;
  U32                    TimeOutPageWrite;
  U32                    DelaySectorErase;
  U32                    Delay1ms;
  U32                    srpms;
  FS_NOR_SPI_POLL_PARA * pPollPara;
  U8                     abDeviceId[3 * 2];   // *2 for dual flash mode support.

  FS_MEMSET(abDeviceId, 0, sizeof(abDeviceId));
  Unit = pInst->Unit;
  ASSERT_HW_TYPE_IS_SET(pInst);
  //
  // Initialize SPI HW.
  //
  Freq_Hz = (U32)pInst->pHWType->pfInit(Unit);
  if (Freq_Hz == 0u) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPIFI: _Init: Could not initialize HW."));
    return 1;                         // Error, could not identify device
  }
  //
  // Calculate the number of status requests that can be executed in 1 millisecond.
  // At least 16-bits are exchanged on each NOR device status request.
  //
  srpms = (Freq_Hz >> 4) / 1000u;
  TimeOutSectorErase = srpms * (U32)FS_NOR_TIMEOUT_SECTOR_ERASE;
  TimeOutPageWrite   = srpms * (U32)FS_NOR_TIMEOUT_PAGE_WRITE;
  DelaySectorErase   = srpms * (U32)FS_NOR_DELAY_SECTOR_ERASE;
  Delay1ms           = srpms;
  //
  // Save parameters to phy. layer instance.
  //
  pPollPara = &pInst->PollParaSectorErase;
  FS_MEMSET(pPollPara, 0, sizeof(FS_NOR_SPI_POLL_PARA));
  pPollPara->TimeOut    = TimeOutSectorErase;
  pPollPara->TimeOut_ms = FS_NOR_TIMEOUT_SECTOR_ERASE;
  pPollPara->Delay      = DelaySectorErase;
  pPollPara->Delay_ms   = FS_NOR_DELAY_SECTOR_ERASE;
  pPollPara = &pInst->PollParaPageWrite;
  FS_MEMSET(pPollPara, 0, sizeof(FS_NOR_SPI_POLL_PARA));
  pPollPara->TimeOut    = TimeOutPageWrite;
  pPollPara->TimeOut_ms = FS_NOR_TIMEOUT_PAGE_WRITE;
  pPollPara = &pInst->Device.Inst.PollParaRegWrite;
  FS_MEMSET(pPollPara, 0, sizeof(FS_NOR_SPI_POLL_PARA));
  pPollPara->TimeOut    = TimeOutPageWrite;
  pPollPara->TimeOut_ms = FS_NOR_TIMEOUT_PAGE_WRITE;
  pInst->Delay1ms = Delay1ms;
  //
  // Make sure that we exchange the data in SPI mode.
  //
  pInst->Device.Inst.BusWidth = (U16)FS_BUSWIDTH_MAKE(1uL, 1uL, 1uL);
  //
  // Identify features that are not standardized such as
  // error flags, special settings that have to be
  // performed in order to enable the quad mode, etc.
  //
  r = _IdentifyDevice(pInst, abDeviceId, sizeof(abDeviceId));
  if (r != 0) {
    //
    // Try to release the device from power down.
    //
    _ReleaseFromPowerDown(pInst);
    r = _IdentifyDevice(pInst, abDeviceId, sizeof(abDeviceId));
    if (r != 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPIFI: _Init: Could not identify device."));
      return 1;                       // Error, could not identify device
    }
  }
  //
  // Wait for the last operation before reset to complete.
  //
  (void)_WaitForEndOfOperation(pInst, &pInst->PollParaSectorErase);
  //
  // Wake-up NOR flash if required, clear the write mode flag, etc.
  //
  _InitDevice(pInst);
  //
  // Identify the device parameters. First, try to see if it supports SFDP.
  // If it does not support SFDP we try to identify the parameters by id.
  //
  r = _ReadApplyParaBySFDP(pInst);
  if (r != 0) {
    r = _ReadApplyParaById(pInst);
  }
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPIFI: _Init: Device does not support SFDP or the id is unknown."));
    return 1;                         // Error, could not identify device
  }
  //
  // OK, the device is identified. Determine which physical sectors are used as storage.
  //
  r = _CalcStorageArea(pInst);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPIFI: _Init: Could not calculate the storage area."));
    return 1;                         // Error
  }
  //
  // Switch to 4-byte address if required.
  //
  r = _SetNumBytesAddr(pInst);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPIFI: _Init: Could not set address mode."));
    return 1;
  }
  //
  // Remove the write protection of all physical sectors.
  //
  StartAddr = pInst->StartAddrUsed;
  NumBytes  = pInst->NumBytes;
  r = _RemoveWriteProtection(pInst, StartAddr, NumBytes);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPIFI: _Init: Could not remove write protection."));
    return 1;
  }
  //
  // Configure the number of dummy cycles for the memory array read operation.
  //
  r = _SetNumCyclesDummy(pInst, Freq_Hz);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPIFI: _Init: Could not set dummy cycles."));
    return 1;
  }
  //
  // Switch to single, quad or dual mode.
  //
  r = _SetBusWidth(pInst);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPIFI: _Init: Could not configure bus width."));
    return 1;
  }
  //
  // Determine the command code and the bus width for the write operation.
  //
  _ApplyParaConf(pInst, abDeviceId);
  pInst->IsInited = 1;
  //
  // Enter the memory-mapped mode.
  //
  r = _LeaveCmdMode(pInst);
  return r;
}

/*********************************************************************
*
*      _WritePageAligned
*
*  Function description
*    Writes data to the memory array of the NOR flash device.
*
*  Parameters
*    pInst      Physical layer instance.
*    Addr       NOR flash address to write to.
*    pData      [IN] Data to be written.
*    NumBytes   Number of bytes to be written.
*
*  Return value
*    ==0    OK, data was written.
*    !=0    An error occurred.
*
*  Additional information
*    This function performs more than one write operation if the data
*    is not aligned to and is not a multiple of the minimum number
*    of bytes that can be written to NOR flash device.
*/
static int _WritePageAligned(NOR_SPIFI_INST * pInst, U32 Addr, const U8 * pData, U32 NumBytes) {
  int                    r;
  int                    IsDualDeviceMode;
  FS_NOR_SPI_POLL_PARA * pPollPara;
  U8                     abData[2];
  U32                    NumBytesToWrite;

  pPollPara        = &pInst->PollParaPageWrite;
  IsDualDeviceMode = (int)pInst->Device.Inst.IsDualDeviceMode;
  if (IsDualDeviceMode == 0) {
    r = _WritePage(pInst, Addr, pData, NumBytes);
    if (r == 0) {
      r = _WaitForEndOfOperation(pInst, pPollPara);
    }
  } else {
    //
    // Handle leading unaligned bytes.
    //
    r = 0;
    if ((Addr & 1u) != 0u) {
      //
      // Align the data to be written.
      //
      abData[0] = 0xFF;         // Do not modify already existing data.
      abData[1] = *pData;
      r = _WritePage(pInst, Addr - 1u, abData, sizeof(abData));
      if (r == 0) {
        r = _WaitForEndOfOperation(pInst, pPollPara);
      }
      ++Addr;
      --NumBytes;
      ++pData;
    }
    //
    // Handle aligned bytes.
    //
    if (r == 0) {
      NumBytesToWrite  = NumBytes;
      NumBytesToWrite &= ~1u;
      if (NumBytesToWrite != 0u) {
        r = _WritePage(pInst, Addr, pData, NumBytesToWrite);
        if (r == 0) {
          r = _WaitForEndOfOperation(pInst, pPollPara);
        }
        Addr     += NumBytesToWrite;
        NumBytes -= NumBytesToWrite;
        pData    += NumBytesToWrite;
      }
      //
      // Handle unaligned trailing bytes.
      //
      if (r == 0) {
        if (NumBytes != 0u) {
          abData[0] = *pData;
          abData[1] = 0xFF;         // Do not modify already existing data.
          r = _WritePage(pInst, Addr, abData, sizeof(abData));
          if (r == 0) {
            r = _WaitForEndOfOperation(pInst, pPollPara);
          }
        }
      }
    }
  }
  return r;
}

/*********************************************************************
*
*      _WriteOff
*
*  Function description
*    Writes data to the memory array of the NOR flash device.
*
*  Parameters
*    pInst      Physical layer instance.
*    Off        Bytes offset of the written data.
*    pData      [IN] Data to be written.
*    NumBytes   Number of bytes to be written.
*
*  Return value
*    ==0    OK, data was written.
*    !=0    An error occurred.
*
*  Additional information
*    This function performs more than one write operation is the data
*    crosses a NOR page boundary.
*/
static int _WriteOff(NOR_SPIFI_INST * pInst, U32 Off, const U8 * pData, U32 NumBytes) {
  U32 NumBytesToWrite;
  int r;
  U32 BytesPerPage;
  U32 Addr;

  Addr         = pInst->StartAddrUsed + Off;
  BytesPerPage = FS_NOR_BYTES_PER_PAGE;
  if ((Addr & (BytesPerPage - 1u)) != 0u) {
    NumBytesToWrite = BytesPerPage - (Addr & (BytesPerPage - 1u));
    NumBytesToWrite = SEGGER_MIN(NumBytesToWrite, NumBytes);
    r = _WritePageAligned(pInst, Addr, pData, NumBytesToWrite);
    if (r != 0) {
      return 1;           // Error, write failed.
    }
    pData    += NumBytesToWrite;
    NumBytes -= NumBytesToWrite;
    Addr     += (U32)NumBytesToWrite;
  }
  if (NumBytes > 0u) {
    do {
      NumBytesToWrite = SEGGER_MIN(NumBytes, BytesPerPage);
      r = _WritePageAligned(pInst, Addr, pData, NumBytesToWrite);
      if (r != 0) {
        return 1;         // Error, write failed.
      }
      pData    += NumBytesToWrite;
      NumBytes -= NumBytesToWrite;
      Addr     += (U32)NumBytesToWrite;
    } while(NumBytes != 0u);
  }
  return 0;               // OK, data written successfully.
}

/*********************************************************************
*
*      _ReadRange
*
*  Function description
*    Reads data from the memory array of the NOR flash device.
*
*  Parameters
*    pInst        Physical layer instance.
*    Addr         NOR flash address to read from.
*    pData        [OUT] Read data.
*    NumBytes     Number of bytes to read.
*
*  Return value
*    ==0      OK, data read successfully.
*    !=0      An error occurred.
*/
static int _ReadRange(const NOR_SPIFI_INST * pInst, U32 Addr, U8 * pData, U32 NumBytes) {
  U8       Cmd;
  U8       CmdEx;
  U16      BusWidth;
  unsigned NumBytesAddr;
  unsigned NumBytesDummy;
  U8       abPara[FS_NOR_MAX_NUM_BYTES_DUMMY + 4];    // +4 for the address bytes.
  unsigned NumBytesPara;
  unsigned Flags;
  int      r;
  U8       abCmd[MAX_NUM_BYTES_CMD];
  unsigned NumBytesCmd;

  Cmd           = pInst->Device.Inst.CmdRead;
  CmdEx         = pInst->Device.Inst.CmdReadEx;
  NumBytesAddr  = pInst->Device.Inst.NumBytesAddr;
  NumBytesDummy = pInst->Device.Inst.NumBytesReadDummy;
  BusWidth      = pInst->Device.Inst.BusWidthRead;
  Flags         = pInst->Device.Inst.FlagsRead;
  FS_DEBUG_ASSERT(FS_MTYPE_DRIVER, NumBytesDummy <= (unsigned)FS_NOR_MAX_NUM_BYTES_DUMMY);
  NumBytesDummy = SEGGER_MIN(NumBytesDummy, (unsigned)FS_NOR_MAX_NUM_BYTES_DUMMY);
  //
  // Check if the address has to be extended by one byte in dual flash mode.
  //
  if (pInst->Device.Inst.IsDualDeviceMode != 0u) {
    if (NumBytesAddr == 3u) {
      if (Addr > 0x00FFFFFFuL) {
        NumBytesAddr  = 4;
        Flags        |= FS_NOR_HW_FLAG_ADDR_3BYTE;
      }
    }
  }
  //
  // Encode the address and the dummy bytes.
  //
  NumBytesPara = 0;
  if (NumBytesAddr == 4u) {
    abPara[NumBytesPara++] = (U8)(Addr >> 24);
  }
  abPara[NumBytesPara++]   = (U8)(Addr >> 16);
  abPara[NumBytesPara++]   = (U8)(Addr >>  8);
  abPara[NumBytesPara++]   = (U8)Addr;
  if (NumBytesDummy != 0u) {
    do {
      abPara[NumBytesPara++] = 0xFF;
    } while (--NumBytesDummy != 0u);
  }
  //
  // Encode the read command.
  //
  NumBytesCmd = 0;
  abCmd[NumBytesCmd++] = (U8)Cmd;
  if (CmdEx != 0u) {
    abCmd[NumBytesCmd++] = (U8)CmdEx;
  }
  //
  // Execute the operation.
  //
  r = _Read(pInst, abCmd, NumBytesCmd, abPara, NumBytesPara, NumBytesAddr, pData, NumBytes, BusWidth, Flags);
  return r;
}

/*********************************************************************
*
*      _ReadOff
*
*  Function description
*    Reads data from the memory array of the NOR flash device.
*
*  Parameters
*    pInst        Physical layer instance.
*    Off          Byte offset to read from.
*    pData        [OUT] Read data.
*    NumBytes     Number of bytes to read.
*
*  Return value
*    ==0      OK, data read successfully.
*    !=0      An error occurred.
*/
static int _ReadOff(const NOR_SPIFI_INST * pInst, U32 Off, U8 * pData, U32 NumBytes) {
  U32 Addr;
  int r;
  int IsDualDeviceMode;
  U8  abData[2];
  U32 NumBytesToRead;

  Addr             = pInst->StartAddrUsed + Off;
  IsDualDeviceMode = (int)pInst->Device.Inst.IsDualDeviceMode;
  if (IsDualDeviceMode == 0) {
    r = _ReadRange(pInst, Addr, pData, NumBytes);
  } else {
    //
    // Handle leading unaligned bytes.
    //
    r = 0;
    if ((Addr & 1u) != 0u) {
      //
      // Align the data to be read.
      //
      FS_MEMSET(abData, 0xFF, sizeof(abData));
      r = _ReadRange(pInst, Addr - 1u, abData, sizeof(abData));
      if (r == 0) {
        *pData = abData[1];
      }
      ++Addr;
      --NumBytes;
      ++pData;
    }
    //
    // Handle aligned bytes.
    //
    if (r == 0) {
      NumBytesToRead  = NumBytes;
      NumBytesToRead &= ~1u;
      if (NumBytesToRead != 0u) {
        r = _ReadRange(pInst, Addr, pData, NumBytesToRead);
        Addr     += NumBytesToRead;
        NumBytes -= NumBytesToRead;
        pData    += NumBytesToRead;
      }
      //
      // Handle unaligned trailing bytes.
      //
      if (r == 0) {
        if (NumBytes != 0u) {
          FS_MEMSET(abData, 0xFF, sizeof(abData));
          r = _ReadRange(pInst, Addr, abData, sizeof(abData));
          if (r == 0) {
            *pData = abData[0];
          }
        }
      }
    }
  }
  return r;
}

/*********************************************************************
*
*      _EraseSector
*
*  Function description
*    Sets all the bytes of a physical sector to 0xFF.
*
*  Parameters
*    pInst          Physical layer instance.
*    SectorIndex    Index of the physical sector.
*
*  Return value
*    ==0    OK, physical sector erased successfully.
*    !=0    An error occurred.
*/
static int _EraseSector(NOR_SPIFI_INST * pInst, U32 SectorIndex) {
  int                    r;
  U32                    Off;
  FS_NOR_SPI_DEVICE    * pDevice;
  U8                     Cmd;
  FS_NOR_SPI_POLL_PARA * pPollPara;
  U32                    Addr;

  //
  // Fill local variables.
  //
  pDevice   = &pInst->Device;
  pPollPara = &pInst->PollParaSectorErase;
  //
  // Calculate the start address of the physical sector.
  //
  Off  = FS_NOR_SPI_GetSectorOff(&pInst->Device.Inst, SectorIndex);
  Cmd  = FS_NOR_SPI_GetSectorEraseCmd(&pInst->Device.Inst, SectorIndex);
  Addr = pInst->StartAddrUsed + Off;
  //
  // Erase the physical sector.
  //
  r = pDevice->pType->pfEraseSector(&pDevice->Inst, Cmd, Addr);
  if (r == 0) {
    r = pDevice->pType->pfWaitForEndOfOperation(&pDevice->Inst, pPollPara);
  }
  return r;
}

/*********************************************************************
*
*      _InitIfRequired
*
*  Function description
*    Initializes a physical layer instance.
*
*  Parameters
*    pInst      Physical layer instance.
*
*  Return value
*    ==0      OK, instance initialized successfully.
*    !=0      An error occurred.
*
*  Additional information
*    This function does not perform any operation if the physical layer
*    instance is already initialized.
*/
static int _InitIfRequired(NOR_SPIFI_INST * pInst) {
  int r;

  r = 0;        // Set to indicate success.
  if ((pInst->IsInited) == 0u) {
    r = _Init(pInst);
  }
  return r;
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
*    !=NULL   Physical layer instance.
*    ==NULL   An error occurred.
*/
static NOR_SPIFI_INST * _AllocInstIfRequired(U8 Unit) {
  NOR_SPIFI_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  pInst = NULL;
  if (Unit < (U8)FS_NOR_NUM_UNITS) {
    pInst = _apInst[Unit];
    if (pInst == NULL) {
      pInst = SEGGER_PTR2PTR(NOR_SPIFI_INST, FS_ALLOC_ZEROED((I32)sizeof(NOR_SPIFI_INST), "NOR_SPIFI_INST"));
      if (pInst != NULL) {
        pInst->Unit                 = Unit;
        pInst->pDeviceList          = FS_NOR_DEVICE_LIST_DEFAULT;
        pInst->Device.Inst.pCmd     = &_Cmd;
        pInst->Device.Inst.pContext = SEGGER_PTR2PTR(void, pInst);
        pInst->Device.Inst.BusWidth = (U16)FS_BUSWIDTH_MAKE(1uL, 1uL, 1uL);     // By default, all the operations are executed in single SPI  mode.
        pInst->Device.Inst.Flags    = 0;                                        // By default, the data is transfer only on one clock edge.
        _apInst[Unit]               = pInst;
      }
    }
  }
  return pInst;
}

/*********************************************************************
*
*      _GetInst
*
*  Function description
*    Returns a physical layer instance by its index.
*
*  Parameters
*    Unit     Index of the physical layer.
*
*  Return value
*    !=NULL   Physical layer instance.
*    ==NULL   An error occurred.
*/
static NOR_SPIFI_INST * _GetInst(U8 Unit) {
  NOR_SPIFI_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  pInst = NULL;
  if (Unit < (U8)FS_NOR_NUM_UNITS) {
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
*       _PHY_WriteOff
*
*  Function description
*    Writes data to the memory array of the NOR flash device.
*
*  Parameters
*    Unit           Index of the physical layer.
*    Off            Byte offset to write to.
*    pData          [IN] Data to be written.
*    NumBytes       Number of bytes to be written.
*
*  Return value
*    ==0    OK, data written successfully.
*    !=0    An error occurred.
*
*  Additional information
*    This routine does not check if the memory location to be written
*    has been previously erased. It is able to write data that crosses
*    a page boundary.
*/
static int _PHY_WriteOff(U8 Unit, U32 Off, const void * pData, U32 NumBytes) {
  int              r;
  NOR_SPIFI_INST * pInst;
  int              Result;

  r = 1;                  // Set to indicate an error.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    r = _InitIfRequired(pInst);
    if (r == 0) {
      if (NumBytes != 0u) {
        _Lock(pInst);
        //
        // Make sure that we are communicating directly with the NOR flash via SPI.
        //
        _EnterCmdMode(pInst);
        //
        // Write data to NOR flash and take care of unaligned page accesses.
        //
        r = _WriteOff(pInst, Off, SEGGER_CONSTPTR2PTR(const U8, pData), NumBytes);
        //
        // Go back to memory-mapped mode.
        //
        Result = _LeaveCmdMode(pInst);
        r = (Result != 0) ? Result : r;
        _Unlock(pInst);
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _PHY_ReadOff
*
*  Function description
*    Reads data from the memory array of the NOR flash device.
*
*  Parameters
*    Unit           Index of the physical layer.
*    pData          [OUT] Read data.
*    Off            Byte offset to read from.
*    NumBytes       Number of bytes to be read.
*
*  Return value
*    ==0    OK, data read successfully.
*    !=0    An error occurred.
*/
static int _PHY_ReadOff(U8 Unit, void * pData, U32 Off, U32 NumBytes) {
  U32              Addr;
  int              r;
  NOR_SPIFI_INST * pInst;

  r = 1;                  // Set to indicate an error.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    r = _InitIfRequired(pInst);
    if (r == 0) {
      _Lock(pInst);
      if (_IsMappingSupported(pInst) != 0) {
        Addr = pInst->BaseAddr + pInst->StartAddrUsed + Off;
        FS_MEMCPY(pData, SEGGER_ADDR2PTR(void, Addr), NumBytes);
      } else {
        _EnterCmdMode(pInst);
        r = _ReadOff(pInst, Off, SEGGER_PTR2PTR(U8, pData), NumBytes);
      }
      _Unlock(pInst);
    }
  }
  return r;
}

/*********************************************************************
*
*       _PHY_EraseSector
*
*  Function description
*    Erases one physical sector.
*
*  Parameters
*    Unit           Index of the physical layer.
*    SectorIndex    Index of the physical sector to be erased.
*
*  Return value
*    ==0    OK, physical sector erased successfully.
*    !=0    An error occurred.
*/
static int _PHY_EraseSector(U8 Unit, unsigned SectorIndex) {
  int              r;
  NOR_SPIFI_INST * pInst;
  int              Result;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;               // Error, driver instance not found.
  }
  ASSERT_SECTOR_INDEX_IS_IN_RANGE(pInst, SectorIndex);
  if (SectorIndex >= pInst->NumSectors) {
    return 1;               // Error, invalid sector index.
  }
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return 1;               // Error, could not initialize driver instance.
  }
  _Lock(pInst);
  //
  // Make sure that we are communicating directly with the NOR flash via SPI.
  //
  _EnterCmdMode(pInst);
  //
  // Erase the physical sector.
  //
  r = _EraseSector(pInst, SectorIndex);
  //
  // Go back to memory-mapped mode.
  //
  Result = _LeaveCmdMode(pInst);
  r = (Result != 0) ? Result : r;
  _Unlock(pInst);
  return r;
}

/*********************************************************************
*
*       _PHY_GetSectorInfo
*
*  Function description
*    Returns information about a physical sector.
*
*  Parameters
*    Unit           Index of the physical layer.
*    SectorIndex    Index of the physical sector (0-based).
*    pOff           [OUT] Byte offset of the physical sector.
*    pNumBytes      [OUT] Size in bytes of the physical sector.
*
*  Additional information
*    SectorIndex and *pOff are relative to the range of physical sectors
*    used as storage.
*
*    An error is reported by setting *pOff to SECTOR_OFF_INVALID and *pNumBytes to 0.
*/
static void _PHY_GetSectorInfo(U8 Unit, unsigned SectorIndex, U32 * pOff, U32 * pNumBytes) {
  int              r;
  U32              SectorOff;
  U32              BytesPerSector;
  NOR_SPIFI_INST * pInst;

  SectorOff      = SECTOR_OFF_INVALID;  // Set to an invalid value.
  BytesPerSector = 0;                   // Set to an invalid value.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    ASSERT_SECTOR_INDEX_IS_IN_RANGE(pInst, SectorIndex);
    if (SectorIndex < pInst->NumSectors) {
      _Lock(pInst);
      r = _InitIfRequired(pInst);
      if (r == 0) {
        SectorOff      = FS_NOR_SPI_GetSectorOff(&pInst->Device.Inst, SectorIndex);
        BytesPerSector = FS_NOR_SPI_GetSectorSize(&pInst->Device.Inst, SectorIndex);
      }
      _Unlock(pInst);
    }
  }
  if (pOff != NULL) {
    *pOff = SectorOff;
  }
  if (pNumBytes != NULL) {
    *pNumBytes = BytesPerSector;
  }
}

/*********************************************************************
*
*       _PHY_GetNumSectors
*
*  Function description
*    Returns the number total number of physical sectors used as storage.
*
*  Parameters
*    Unit           Index of the physical layer.
*
*  Return value
*    !=0        Number of physical sectors.
*    ==0        An error occurred.
*/
static int _PHY_GetNumSectors(U8 Unit) {
  int              r;
  int              NumSectors;
  NOR_SPIFI_INST * pInst;

  NumSectors = 0;               // Set to indicate an error.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    _Lock(pInst);
    r = _InitIfRequired(pInst);
    if (r == 0) {
      NumSectors = (int)pInst->NumSectors;
    }
    _Unlock(pInst);
  }
  return NumSectors;
}

/*********************************************************************
*
*       _PHY_Configure
*
*  Function description
*    Configures the physical layer.
*
*  Parameters
*    Unit           Index of the physical layer.
*    BaseAddr       Address of the first byte in the memory array of the NOR flash device.
*    StartAddr      Address of the first byte to be used as storage.
*    NumBytes       Number of bytes to be used as storage.
*
*  Additional information
*    StartAddr has to be greater than or equal to BaseAddr.
*/
static void _PHY_Configure(U8 Unit, U32 BaseAddr, U32 StartAddr, U32 NumBytes) {
  NOR_SPIFI_INST * pInst;

  FS_DEBUG_ASSERT(FS_MTYPE_DRIVER, StartAddr >= BaseAddr);
  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->BaseAddr      = BaseAddr;
    pInst->StartAddrConf = StartAddr;
    pInst->NumBytes      = NumBytes;
    pInst->IsInited      = 0;             // The layer needs to be re-initialized.
  }
}

/*********************************************************************
*
*       _PHY_OnSelectPhy
*
*  Function description
*    Selects the physical layer.
*
*  Parameters
*    Unit  Physical layer number.
*/
static void _PHY_OnSelectPhy(U8 Unit) {
  (void)_AllocInstIfRequired(Unit);
}

/*********************************************************************
*
*       _PHY_DeInit
*
*  Function description
*    Frees the resources allocated for the physical layer instance.
*
*  Parameters
*    Unit  Physical layer number.
*/
static void _PHY_DeInit(U8 Unit) {
#if FS_SUPPORT_DEINIT
  NOR_SPIFI_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  if (Unit < (U8)FS_NOR_NUM_UNITS) {
    pInst = _apInst[Unit];
    if (pInst != NULL) {
      FS_Free(SEGGER_PTR2PTR(void, pInst));
    }
    _apInst[Unit] = NULL;
  }
#else
  FS_USE_PARA(Unit);
#endif
}

/*********************************************************************
*
*       _PHY_Init
*
*  Function description
*    Initializes the physical layer.
*
*  Parameters
*    Unit         Index of the physical layer.
*
*  Return value
*    ==0      OK, physical layer initialized.
*    !=0      An error occurred.
*/
static int _PHY_Init(U8 Unit) {
  int              r;
  NOR_SPIFI_INST * pInst;

  r = 1;        // Set to indicate failure.
  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    _Lock(pInst);
    r = _Init(pInst);
    _Unlock(pInst);
  }
  return r;
}

/*********************************************************************
*
*       Public const data
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_NOR_PHY_SPIFI
*/
const FS_NOR_PHY_TYPE FS_NOR_PHY_SPIFI = {
  _PHY_WriteOff,
  _PHY_ReadOff,
  _PHY_EraseSector,
  _PHY_GetSectorInfo,
  _PHY_GetNumSectors,
  _PHY_Configure,
  _PHY_OnSelectPhy,
  _PHY_DeInit,
  NULL,
  _PHY_Init
};

/*********************************************************************
*
*       Public code (for internal use only)
*
**********************************************************************
*/

/*********************************************************************
*
*       FS__NOR_SPIFI_ReadData
*/
int FS__NOR_SPIFI_ReadData(U8 Unit, U8 Cmd, const U8 * pPara, unsigned NumBytesPara, unsigned NumBytesAddr, U8 * pData, unsigned NumBytesData) {
  NOR_SPIFI_INST * pInst;
  int              r;
  unsigned         BusWidth;
  int              Result;

  //
  // Validate parameters.
  //
  if ((pData == NULL) || (NumBytesData == 0u)) {
    return FS_ERRCODE_INVALID_PARA;                     // Error, invalid parameters.
  }
  //
  // Allocate the driver instance if required.
  //
  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return FS_ERRCODE_OUT_OF_MEMORY;                    // Error, could not allocate driver instance.
  }
  ASSERT_HW_TYPE_IS_SET(pInst);
  if (pInst->pHWType == NULL) {
    return FS_ERRCODE_HW_LAYER_NOT_SET;                 // Error, HW layer not configured.
  }
  _Lock(pInst);
  //
  // Initialize the storage device.
  //
  r = _InitIfRequired(pInst);
  if (r != 0) {
    r = FS_ERRCODE_INIT_FAILURE;
  } else {
    _EnterCmdMode(pInst);
    //
    // Read the data.
    //
    BusWidth = FS_BUSWIDTH_MAKE(1uL, 1uL, 1uL);
    Result = _Read(pInst, &Cmd, sizeof(Cmd), pPara, NumBytesPara, NumBytesAddr, pData, NumBytesData, (U16)BusWidth, 0);
    r = (Result != 0) ? FS_ERRCODE_READ_FAILURE : r;
    Result = _LeaveCmdMode(pInst);
    r = (Result != 0) ? FS_ERRCODE_INIT_FAILURE : r;
  }
  _Unlock(pInst);
  return r;
}

/*********************************************************************
*
*      Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_NOR_SPIFI_Allow2bitMode
*
*  Function description
*    Specifies if the physical layer is permitted to exchange data
*    via two data lines.
*
*  Parameters
*    Unit       Index of the physical layer (0-based).
*    OnOff      Activation status of the option.
*               * 0   Disable the option.
*               * 1   Enable the option.
*
*  Additional information
*    This function is optional. By default the data is exchanged via one
*    data line (standard SPI). The data transfer via two data lines is
*    used only if this type of data transfer is supported by the serial
*    NOR flash device. In dual mode two bits of data are transferred with
*    each clock period which helps improve the performance. If the serial
*    NOR flash device does not support the dual mode then the data is
*    transferred in standard mode (one data bit per clock period).
*
*    The application is permitted to call this function only at the file
*    system initialization in FS_X_AddDevices().
*/
void FS_NOR_SPIFI_Allow2bitMode(U8 Unit, U8 OnOff) {
  NOR_SPIFI_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->Device.Inst.Allow2bitMode = OnOff;
  }
}

/*********************************************************************
*
*       FS_NOR_SPIFI_Allow4bitMode
*
*  Function description
*    Specifies if the physical layer is permitted to exchange data
*    via four data lines.
*
*  Parameters
*    Unit       Index of the physical layer (0-based).
*    OnOff      Activation status of the option.
*               * 0   Disable the option.
*               * 1   Enable the option.
*
*  Additional information
*    This function is optional. By default the data is exchanged via one
*    data line (standard SPI). The data transfer via four data lines
*    is used only if this type of data transfer is supported by the serial
*    NOR flash device. In quad mode four bits of data are transferred on
*    each clock period which helps improve the performance. If the serial
*    NOR flash device does not support the quad mode then the data is
*    transferred in dual mode if enabled and supported or in standard
*    mode (one bit per clock period).
*
*    The application is permitted to call this function only at the file
*    system initialization in FS_X_AddDevices().
*/
void FS_NOR_SPIFI_Allow4bitMode(U8 Unit, U8 OnOff) {
  NOR_SPIFI_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->Device.Inst.Allow4bitMode = OnOff;
  }
}

/*********************************************************************
*
*       FS_NOR_SPIFI_SetHWType
*
*  Function description
*    Configures the HW access routines.
*
*  Parameters
*    Unit       Index of the physical layer.
*    pHWType    Hardware layer to be used for data exchanged.
*
*  Additional information
*    It is mandatory to call this function during the file system
*    initialization in FS_X_AddDevices() once for each instance
*    of a physical layer.
*/
void FS_NOR_SPIFI_SetHWType(U8 Unit, const FS_NOR_HW_TYPE_SPIFI * pHWType) {
  NOR_SPIFI_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    if (pHWType != NULL) {
      pInst->pHWType = pHWType;
      //
      // Disable the octal and DTR mode if the hardware layer
      // does not define the required data transfer functions.
      //
      if (   (pInst->pHWType->pfControlEx == NULL)
          || (pInst->pHWType->pfReadEx    == NULL)
          || (pInst->pHWType->pfWriteEx   == NULL)) {
        pInst->Device.Inst.AllowOctalMode = 0;
        pInst->Device.Inst.AllowDTRMode   = 0;
      }
    }
  }
}

/*********************************************************************
*
*       FS_NOR_SPIFI_SetDeviceList
*
*  Function description
*    Configures the type of handled serial NOR flash devices.
*
*  Parameters
*    Unit         Index of the physical layer (0-based).
*    pDeviceList  List of NOR flash devices the physical layer can handle.
*
*  Additional information
*    This function is optional. By default the physical layer is configured
*    to handle only Micron serial NOR flash devices. Handling for serial NOR
*    flash devices from other manufacturers has to be explicitly enabled via
*    this function.
*
*    Permitted values for the pDeviceList parameter are:
*    +------------------------------------+-------------------------------------------------------------------------------------+
*    | Identifier                         | Description                                                                         |
*    +------------------------------------+-------------------------------------------------------------------------------------+
*    | FS_NOR_SPI_DeviceListAdesto        | Enables handling of Adesto serial NOR flash devices.                                |
*    +------------------------------------+-------------------------------------------------------------------------------------+
*    | FS_NOR_SPI_DeviceListAll           | Enables handling of serial NOR flash devices from all manufacturers.                |
*    +------------------------------------+-------------------------------------------------------------------------------------+
*    | FS_NOR_SPI_DeviceListCypress       | Enables handling of Cypress serial NOR flash devices.                               |
*    +------------------------------------+-------------------------------------------------------------------------------------+
*    | FS_NOR_SPI_DeviceListDefault       | Enables handling of Micron and of SFDP compatible serial NOR flash devices.         |
*    +------------------------------------+-------------------------------------------------------------------------------------+
*    | FS_NOR_SPI_DeviceListEon           | Enables handling of Eon serial NOR flash devices.                                   |
*    +------------------------------------+-------------------------------------------------------------------------------------+
*    | FS_NOR_SPI_DeviceListGigaDevice    | Enables handling of GigaDevice serial NOR flash devices.                            |
*    +------------------------------------+-------------------------------------------------------------------------------------+
*    | FS_NOR_SPI_DeviceListISSI          | Enables handling of ISSI serial NOR flash devices.                                  |
*    +------------------------------------+-------------------------------------------------------------------------------------+
*    | FS_NOR_SPI_DeviceListMacronix      | Enables handling of Macronix serial NOR flash devices.                              |
*    +------------------------------------+-------------------------------------------------------------------------------------+
*    | FS_NOR_SPI_DeviceListMacronixOctal | Enables handling of Macronix serial NOR flash devices working in octal mode.        |
*    +------------------------------------+-------------------------------------------------------------------------------------+
*    | FS_NOR_SPI_DeviceList_Micron       | Enables handling of Micron serial NOR flash devices.                                |
*    +------------------------------------+-------------------------------------------------------------------------------------+
*    | FS_NOR_SPI_DeviceList_Micron_x     | Enables handling of Micron serial NOR flash devices in single and dual chip setups. |
*    +------------------------------------+-------------------------------------------------------------------------------------+
*    | FS_NOR_SPI_DeviceList_Micron_x2    | Enables handling of Micron serial NOR flash devices in dual chip setups.            |
*    +------------------------------------+-------------------------------------------------------------------------------------+
*    | FS_NOR_SPI_DeviceListMicrochip     | Enables handling of Microchip serial NOR flash devices.                             |
*    +------------------------------------+-------------------------------------------------------------------------------------+
*    | FS_NOR_SPI_DeviceListSpansion      | Enables handling of Spansion serial NOR flash devices.                              |
*    +------------------------------------+-------------------------------------------------------------------------------------+
*    | FS_NOR_SPI_DeviceListWinbond       | Enables handling of Winbond serial NOR flash devices.                               |
*    +------------------------------------+-------------------------------------------------------------------------------------+
*
*    The application can save ROM space by setting FS_NOR_DEVICE_LIST_DEFAULT
*    to NULL at compile time and by calling at runtime FS_NOR_SFDP_SetDeviceList()
*    with the actual list of serial NOR flash devices that have to be
*    handled.
*
*    The application is permitted to call this function only at the file
*    system initialization in FS_X_AddDevices().
*/
void FS_NOR_SPIFI_SetDeviceList(U8 Unit, const FS_NOR_SPI_DEVICE_LIST * pDeviceList) {
  NOR_SPIFI_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->pDeviceList = pDeviceList;
  }
}

/*********************************************************************
*
*       FS_NOR_SPIFI_SetSectorSize
*
*  Function description
*    Configures the size of the physical sector to be used by the driver.
*
*  Parameters
*    Unit             Index of the physical layer (0-based).
*    BytesPerSector   Sector size to be used.
*
*  Additional information
*    Typically, a serial NOR flash device supports erase commands
*    that can be used to erase sectors of different sizes (4 KB, 32 KB, etc.)
*    For performance reasons the physical layer chooses always the
*    erase command corresponding to the largest physical sector.
*    This function can be used to request the physical layer to use
*    a different (smaller) physical sector size. The mount operation fails
*    if the serial NOR flash device does not support the specified
*    physical sector size.
*
*    The application is permitted to call this function only at the file
*    system initialization in FS_X_AddDevices().
*/
void FS_NOR_SPIFI_SetSectorSize(U8 Unit, U32 BytesPerSector) {
  NOR_SPIFI_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->Device.Inst.ldBytesPerSector = (U8)_ld(BytesPerSector);
  }
}

/*********************************************************************
*
*       FS_NOR_SPIFI_SetDeviceParaList
*
*  Function description
*    Configures parameters of serial NOR flash devices.
*
*  Parameters
*    Unit             Index of the physical layer (0-based).
*    pDeviceParaList  List of device parameters.
*
*  Additional information
*    This function is optional. This function can be used to enable handling
*    for vendor specific features of serial NOR flash device such as error
*    handling and data protection. By default, the parameters of the used
*    serial NOR flash device are determined by evaluating the SFDP
*    tables stored in it. However, the information about the commands
*    that can be used to write the data via two and four data lines
*    is not stored to this parameters. FS_NOR_SFDP_SetDeviceParaList()
*    can be used to specify this information. The parameters are matched
*    by comparing the first byte (manufacturer id) and the third byte
*    (device id) of the information returned by the READ ID (0x9F) function
*    with the MfgId and Id members of the FS_NOR_SPI_DEVICE_PARA structure.
*
*    The application is permitted to call this function only at the file
*    system initialization in FS_X_AddDevices().
*/
void FS_NOR_SPIFI_SetDeviceParaList(U8 Unit, const FS_NOR_SPI_DEVICE_PARA_LIST * pDeviceParaList) {
  NOR_SPIFI_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->pDeviceParaList = pDeviceParaList;
  }
}

/*********************************************************************
*
*       FS_NOR_SPIFI_ExecCmd
*
*  Function description
*    Sends command sequences to NOR flash device.
*
*  Parameters
*    Unit       Index of the physical layer (0-based).
*    pCmd       List of command codes. Must be different than NULL.
*    NumBytes   Number of command codes. Must be different than 0.
*
*  Return values
*    ==0        OK, command sequence sent successfully.
*    !=0        Error code that indicates the failure reason.
*
*  Additional information
*    This function is optional. It can be used to send one or more
*    single byte commands to a NOR flash device. Each command code has to be
*    specified as a single byte in pCmd. The specified commands
*    are executed sequentially in separate SPI transactions beginning
*    with pCmd[0]. All the commands are sent in single SPI mode.
*/
int FS_NOR_SPIFI_ExecCmd(U8 Unit, const U8 * pCmd, unsigned NumBytes) {
  NOR_SPIFI_INST * pInst;
  int              r;
  int              Result;

  //
  // Validate parameters.
  //
  if ((pCmd == NULL) || (NumBytes == 0u)) {
    return FS_ERRCODE_INVALID_PARA;                     // Error, invalid parameters.
  }
  //
  // Allocate the driver instance if required.
  //
  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return FS_ERRCODE_OUT_OF_MEMORY;                    // Error, could not allocate driver instance.
  }
  ASSERT_HW_TYPE_IS_SET(pInst);
  if (pInst->pHWType == NULL) {
    return FS_ERRCODE_HW_LAYER_NOT_SET;                 // Error, HW layer not configured.
  }
  _Lock(pInst);
  //
  // Initialize the storage device.
  //
  r = _InitIfRequired(pInst);
  if (r != 0) {
    r = FS_ERRCODE_INIT_FAILURE;
  } else {
    _EnterCmdMode(pInst);
    //
    // Execute the commands.
    //
    do {
      Result = _Control(pInst, pCmd++, 1, 1, 0);        // 1 means send one command byte, 1 means single SPI mode, 0 means no flags.
      r = (Result != 0) ? FS_ERRCODE_WRITE_FAILURE : r;
    } while (--NumBytes != 0u);
    Result = _LeaveCmdMode(pInst);
    r = (Result != 0) ? FS_ERRCODE_WRITE_FAILURE : r;
  }
  _Unlock(pInst);
  return r;
}

/*********************************************************************
*
*       FS_NOR_SPIFI_AllowOctalMode
*
*  Function description
*    Specifies if the physical layer is permitted to exchange all
*    the data via eight data lines (octal mode).
*
*  Parameters
*    Unit       Index of the physical layer (0-based).
*    OnOff      Activation status of the option.
*               * 0   Disable the option.
*               * 1   Enable the option.
*
*  Additional information
*    This function is optional. By default the data is exchanged via one
*    data line (standard SPI). The data transfer via eight data lines is
*    used only if this type of data transfer is supported by the serial
*    NOR flash device. In octal mode eight bits of data are transferred with
*    each clock period which helps improve the performance. If the serial
*    NOR flash device does not support the octal mode then the data is
*    transferred in standard mode (one bit per clock period).
*
*    The application is permitted to call this function only at the file
*    system initialization in FS_X_AddDevices().
*
*    The octal mode is enabled only if the configured hardware layer
*    implements the following optional functions: FS_NOR_HW_TYPE_SPIFI_CONTROL_EX,
*    FS_NOR_HW_TYPE_SPIFI_READ_EX and FS_NOR_HW_TYPE_SPIFI_WRITE_EX
*/
void FS_NOR_SPIFI_AllowOctalMode(U8 Unit, U8 OnOff) {
  NOR_SPIFI_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    //
    // Check that the hardware layer implements the function required
    // for the data transfer in quad mode.
    //
    if (OnOff != 0u) {
      if (pInst->pHWType != NULL) {
        if (   (pInst->pHWType->pfControlEx == NULL)
            || (pInst->pHWType->pfReadEx    == NULL)
            || (pInst->pHWType->pfWriteEx   == NULL)) {
          OnOff = 0;                // Octal mode is not allowed.
        }
      }
    }
    pInst->Device.Inst.AllowOctalMode = OnOff;
  }
}

/*********************************************************************
*
*       FS_NOR_SPIFI_AllowDTRMode
*
*  Function description
*    Specifies if the physical layer is permitted to exchange data
*    on both clock edges.
*
*  Parameters
*    Unit       Index of the physical layer (0-based).
*    OnOff      Activation status of the option.
*               * 0   Disable the option.
*               * 1   Enable the option.
*
*  Additional information
*    This function is optional. By default the data is exchanged only on one
*    of the clock edges (SDR mode). In DTR mode the data is transferred on
*    each edge of the clock which helps improve the performance. The SPIFI NOR
*    physical layer transfers the data in DTR mode only if the used serial NOR
*    flash device supports it.
*
*    The application is permitted to call this function only at the file
*    system initialization in FS_X_AddDevices().
*
*    The DTR mode is enabled only if the configured hardware layer
*    implements the following optional functions: FS_NOR_HW_TYPE_SPIFI_CONTROL_EX,
*    FS_NOR_HW_TYPE_SPIFI_READ_EX and FS_NOR_HW_TYPE_SPIFI_WRITE_EX
*/
void FS_NOR_SPIFI_AllowDTRMode(U8 Unit, U8 OnOff) {
  NOR_SPIFI_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    //
    // Check that the hardware layer implements the function required
    // for the data transfer in quad mode.
    //
    if (OnOff != 0u) {
      if (pInst->pHWType != NULL) {
        if (   (pInst->pHWType->pfControlEx == NULL)
            || (pInst->pHWType->pfReadEx    == NULL)
            || (pInst->pHWType->pfWriteEx   == NULL)) {
          OnOff = 0;                // DTR mode is not allowed.
        }
      }
    }
    pInst->Device.Inst.AllowDTRMode = OnOff;
  }
}

/*************************** End of file ****************************/
