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
File        : FS_IDE_Drv.c
Purpose     : File system generic IDE driver
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
*       IDE commands
*/
#define CMD_READ_SECTORS                0x20uL
#define CMD_IDENTIFY                    0xECuL
#define CMD_SET_FEATURES                0xEFuL
#define CMD_WRITE_SECTORS               0x30uL

/*********************************************************************
*
*       IDE status
*/
#define STAT_BUSY                       (1uL << 7)
#define STAT_READY                      (1uL << 6)
#define STAT_WRITE_FAIL                 (1uL << 5)
#define STAT_DISC_SEEK_COMPLETE         (1uL << 4)
#define STAT_DATA_REQUEST               (1uL << 3)
#define STAT_CORRECTABLE                (1uL << 2)
#define STAT_ERROR                      (1uL << 0)

/*********************************************************************
*
*       Drive/Head register
*/
#define DH_REG_LBA                      (7uL << 5)
#define DH_REG_DRIVE0                   (0uL << 4)
#define DH_REG_DRIVE1                   (1uL << 4)

/*********************************************************************
*
*       IDE feature commands
*/
#define FEATURE_ENABLE_WRITE_CACHE      0x02u
#define FEATURE_ENABLE_READ_LOOK_AHEAD  0xAAu

/*********************************************************************
*
*       IDE register offsets
*/
#define IDE_ADDR_OFF_SECTOR             0x02u
#define IDE_ADDR_OFF_CYLINDER           0x04u
#define IDE_ADDR_OFF_DH_CMD             0x06u
#define IDE_ADDR_OFF_FEAT_ERROR         0x0Cu
#define IDE_ADDR_OFF_DEVICE_CONTROL     0x0Eu

/*********************************************************************
*
*       Device control register
*
* Notes:
*   (1) CF spec changed
*       CF 2.0 specifies that only bit 1 and 2 are used.
*       All other bits should be zero.
*       (CF spec 1.4 said bit 4 should be set to 1).
*/
#define DC_REG_INT_ENABLE               (1u << 1)
#define DC_REG_SW_RESET                 (1u << 2)

/*********************************************************************
*
*       Misc. defines
*/
#define NUM_SECTORS_AT_ONCE             255u
#define IDE_SECTOR_SIZE                 512u

/*********************************************************************
*
*       ASSERT_UNIT_NO_IS_IN_RANGE
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)                                 \
    if ((Unit) >= (U8)FS_IDE_NUM_UNITS) {                                  \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "IDE: Invalid unit number."));  \
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);                                 \
    }
#else
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)
#endif

/*********************************************************************
*
*       ASSERT_HW_TYPE_IS_SET
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_HW_TYPE_IS_SET(pInst)                                         \
    if ((pInst)->pHWType == NULL) {                                            \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "IDE: HW layer type is not set.")); \
      FS_X_PANIC(FS_ERRCODE_HW_LAYER_NOT_SET);                                 \
    }
#else
  #define ASSERT_HW_TYPE_IS_SET(pInst)
#endif

/*********************************************************************
*
*       Local data types
*
**********************************************************************
*/

/*********************************************************************
*
*       IDE_INST
*/
typedef struct {
  U8                     IsInited;
  U8                     Unit;
  U16                    NumHeads;
  U16                    SectorsPerTrack;
  U32                    NumSectors;
  U16                    BytesPerSector;
  U8                     MaxPioMode;
  U8                     IsSlave;
  const FS_IDE_HW_TYPE * pHWType;
} IDE_INST;

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static IDE_INST * _apInst[FS_IDE_NUM_UNITS];
static U8         _NumUnits = 0;
static U8         _HeadRegister[FS_IDE_NUM_UNITS];

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _Reset
*/
static void _Reset(const IDE_INST * pInst) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWType->pfReset(Unit);
}

/*********************************************************************
*
*       _IsPresent
*/
static int _IsPresent(const IDE_INST * pInst) {
  U8  Unit;
  int r;

  Unit = pInst->Unit;
  r = pInst->pHWType->pfIsPresent(Unit);
  return r;
}

/*********************************************************************
*
*       _Delay
*/
static void _Delay(const IDE_INST * pInst) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWType->pfDelay400ns(Unit);
}

/*********************************************************************
*
*       _WriteReg
*/
static void _WriteReg(const IDE_INST * pInst, unsigned AddrOff, U16 Data) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWType->pfWriteReg(Unit, AddrOff, Data);
}

/*********************************************************************
*
*       _ReadReg
*/
static U16 _ReadReg(const IDE_INST * pInst, unsigned AddrOff) {
  U8  Unit;
  U16 Data;

  Unit = pInst->Unit;
  Data = pInst->pHWType->pfReadReg(Unit, AddrOff);
  return Data;
}

/*********************************************************************
*
*       _WriteData
*/
static void _WriteData(const IDE_INST * pInst, const U8 * pData, unsigned NumBytes) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWType->pfWriteData(Unit, pData, NumBytes);
}

/*********************************************************************
*
*       _ReadData
*/
static void _ReadData(const IDE_INST * pInst, U8 * pData, unsigned NumBytes) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWType->pfReadData(Unit, pData, NumBytes);
}

/*********************************************************************
*
*       _SetFeatures
*
*  Function description
*    FS driver hardware layer function. Set the FEATURES register.
*
*  Parameters
*    pInst    Driver instance.
*    Data     Value to write to the FEATURES register.
*/
static void _SetFeatures(const IDE_INST * pInst, unsigned Data) {
  _WriteReg(pInst, IDE_ADDR_OFF_FEAT_ERROR, (U16)(Data << 8));
}

/*********************************************************************
*
*       _GetError
*
*  Function description
*    FS driver hardware layer function. Read the ERROR register.
*
*  Parameters
*    pInst    Driver instance.
*
*  Return value
*    The value of the ERROR register
*/
static U8 _GetError(const IDE_INST * pInst) {
  U16 Data;

  Data = _ReadReg(pInst, IDE_ADDR_OFF_FEAT_ERROR);
  return (U8)(Data >> 8);
}

/*********************************************************************
*
*       _GetAltStatus
*
*  Function description
*    FS driver hardware layer function. Read the ALTERNATE STATUS register.
*
*  Parameters
*    pInst    Driver instance.
*
*  Return value
*    Value of the ALTERNATE STATUS register.
*/
static U8 _GetAltStatus(const IDE_INST * pInst) {
  U16 Data;

  Data = _ReadReg(pInst, IDE_ADDR_OFF_DEVICE_CONTROL);
  return (U8)Data & 0xFFu;
}

/*********************************************************************
*
*       _GetStatus
*
*  Function description
*    FS driver hardware layer function. Read the STATUS register.
*
*  Parameters
*    pInst    Driver instance.
*
*  Return value
*    Value of the STATUS register.
*/
static U8 _GetStatus(const IDE_INST * pInst) {
  U16 Data;

  Data = _ReadReg(pInst, IDE_ADDR_OFF_DH_CMD);
  return (U8)(Data >> 8);
}

/*********************************************************************
*
*       _WaitWhileBusy
*
*  Function description
*    FS driver internal function. Wait for a maximum of N * access-time
*    (400ns), that the device is not busy.
*
*  Parameters
*    pInst    Driver instance.
*
*  Return value
*    ==0    Device is not busy.
*    ==1    Timeout, device is still busy.
*/
static int _WaitWhileBusy(const IDE_INST * pInst) {
  U8  Status;
  U32 NumLoops;

  NumLoops = (U32)FS_IDE_DEVICE_BUSY_TIMEOUT;
  do {
    _Delay(pInst);
    Status = _GetAltStatus(pInst);
    if (--NumLoops == 0u) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "IDE: _WaitWhileBusy: time out."));
      return 1;
    }
  } while ((Status & STAT_BUSY) != 0u);
  _Delay(pInst);
  Status = _GetAltStatus(pInst);
  if ((Status & STAT_ERROR) != 0u)   {
    if (_GetError(pInst) == (1u << 2)) {
      //
      //  Clear error
      //
      Status = _GetStatus(pInst);
    } else {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "IDE: _WaitWhileBusy: Drive reported error."));
      return 1;
    }
  }
  if ((Status & (STAT_DISC_SEEK_COMPLETE | STAT_READY)) != (STAT_DISC_SEEK_COMPLETE | STAT_READY)) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "IDE: _WaitWhileBusy: drive not ready."));
    return 1;
  }
  return 0;
}

/*********************************************************************
*
*       _SetDevice
*
*  Function description
*    FS driver hardware layer function. Set the DEVICE/HEAD register.
*
*  Parameters
*    pInst    Driver instance.
*    Data     Value to write to the DEVICE/HEAD register.
*
*  Notes
*       When 16 bit memory access is used, this register can only be
*       written together with command register.
*/
static void _SetDevice(const IDE_INST * pInst, U8 Data) {
  U16 Data16;
  U8  Unit;

  Unit = pInst->Unit;
  _HeadRegister[Unit] = Data;           // Save new Device/Head settings for command writes.
  Data16 = (U16)(0x00uL << 8) | Data;   // Upper byte = 0x97 => idle
  _WriteReg(pInst, IDE_ADDR_OFF_DH_CMD, Data16);
}

/*********************************************************************
*
*       _SetCommand
*
*  Function description
*    FS driver hardware layer function. Set the COMMAND register.
*
*  Parameters
*    pInst    Driver instance.
*    Data     Value to write to the COMMAND register.
*
*  Notes
*       When 16 bit memory access is used, this register can only be
*       written together with Select card / head register.
*/
static void _SetCommand(const IDE_INST * pInst, unsigned Data) {
  U8  Unit;
  U16 Data16;

  Unit = pInst->Unit;
  Data16 = (U16)(_HeadRegister[Unit] | (Data << 8));
  _WriteReg(pInst, IDE_ADDR_OFF_DH_CMD, Data16);
}

/*********************************************************************
*
*       _SetDevControl
*
*  Function description
*    FS driver hardware layer function. Set the DEVICE CONTROL register.
*
*  Parameters
*    pInst    Driver instance.
*    Data     Value to write to the DEVICE CONTROL register.
*/
static void _SetDevControl(const IDE_INST * pInst, U8 Data) {
  _WriteReg(pInst, IDE_ADDR_OFF_DEVICE_CONTROL, Data);
}

/*********************************************************************
*
*       _SetSectorReg
*
*  Function description
*    FS driver hardware layer function. Set the sector count and the sector number register.
*
*  Parameters
*    pInst    Driver instance.
*    Data     Value to write to the DEVICE CONTROL register.
*/
static void _SetSectorReg(const IDE_INST * pInst, U16 Data) {
  _WriteReg(pInst, IDE_ADDR_OFF_SECTOR, Data);
}

/*********************************************************************
*
*       _SetCylReg
*
*  Function description
*    FS driver hardware layer function. Set the DEVICE CONTROL register.
*
*  Parameters
*    pInst    Driver instance.
*    Data     Value to write to the DEVICE CONTROL register.
*/
static void _SetCylReg(const IDE_INST * pInst, U16 Data) {
  _WriteReg(pInst, IDE_ADDR_OFF_CYLINDER, Data);
}

/*********************************************************************
*
*       _SetDCReg
*
*  Function description
*    FS driver hardware layer function. Set the DEVICE CONTROL register.
*
*  Parameters
*    pInst    Driver instance.
*    Data     Value to write to the DEVICE CONTROL register.
*/
static void _SetDCReg(const IDE_INST * pInst, U16 Data) {
  _WriteReg(pInst, IDE_ADDR_OFF_DH_CMD, Data);
}

/*********************************************************************
*
*       _SelectDevice
*
*  Function description
*    FS driver internal function. Select a device.
*
*  Parameters
*    pInst    Driver instance.
*
*  Return value
*    ==0      Device has been selected.
*    !=0      Could not select the device.
*/
static int _SelectDevice(const IDE_INST * pInst) {
  int TimeOut;
  U8  Status;
  U16 DeviceReg;
  int r;

  DeviceReg  = (U16)DH_REG_LBA;
  DeviceReg |= (pInst->IsSlave == 0u) ? (U16)DH_REG_DRIVE0 : (U16)DH_REG_DRIVE1;
  _SetDCReg(pInst, DeviceReg);
  r = _WaitWhileBusy(pInst);
  if (r != 0) {
    return r;     // Error, device does not respond.
  }
  r = 1;          // Set to indicate an error.
  //
  // Wait until BUSY=0 / RDY=1 / DSC=1
  //
  TimeOut = FS_IDE_DEVICE_SELECT_TIMEOUT;
  for (;;) {
    _Delay(pInst);
    Status = _GetStatus(pInst);
    if ((Status & 0x01u) != 0u)   {
      if (_GetError(pInst) == (1u << 2)) {
        goto CheckForReady;
      }
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "IDE: Device reports error."));
      break;
    }
CheckForReady:
    if ((Status & (STAT_DISC_SEEK_COMPLETE | STAT_READY)) == (STAT_DISC_SEEK_COMPLETE | STAT_READY)) {
      r = 0;
      break;
    }
    if (--TimeOut != 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "IDE: Device selection timed out."));
      break;
    }
  }
  return r;
}

/*********************************************************************
*
*       _WriteFeature
*/
static int _WriteFeature(const IDE_INST * pInst, U8 Cmd, U16 Para) {
  int r;
  U8  Status;
  U16 DeviceReg;

  //
  //  Select device
  //
  r = _SelectDevice(pInst);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "IDE: _WriteFeature: Device selection failed."));
    return r;                   // Error, could not write feature.
  }
  _SetFeatures(pInst, Cmd);
  _SetSectorReg(pInst, Para);
  DeviceReg  = (U16)DH_REG_LBA;
  DeviceReg |= (pInst->IsSlave == 0u) ? (U16)DH_REG_DRIVE0 : (U16)DH_REG_DRIVE1;
  DeviceReg |= (U16)CMD_SET_FEATURES << 8;
  _SetDCReg(pInst, DeviceReg);  // Start command.
  r = _WaitWhileBusy(pInst);
  if (r != 0) {
    return r;
  }
  Status = _GetStatus(pInst);
  if (((Status & STAT_ERROR) != 0u) || ((Status & STAT_BUSY) != 0u)) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "IDE: _WriteFeature: Command not supported."));
    return 1;                   // Error, command not supported.
  }
  return 0;                     // OK, feature set.
}


/*********************************************************************
*
*       _WriteSectors
*/
static int _WriteSectors(const IDE_INST * pInst , U32 SectorIndex, const U8 * pBuffer, U8 NumSectors, U8 RepeatSame) {
  int r;
  U8  Status;
  U16 Data16;

  //
  // Wait until not busy; should never be the case.
  //
  r = _WaitWhileBusy(pInst);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "IDE: _WriteSectors: Busy on entry."));
    return r;
  }
  //
  // Select device
  //
  r = _SelectDevice(pInst);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "IDE: _WriteSectors: Device selection failed."));
    return r;
  }
  //
  // Setup all necessary registers for command
  //
  _SetDevControl(pInst, DC_REG_INT_ENABLE);
  Data16 = (U16)(SectorIndex << 8) | NumSectors;
  _SetSectorReg (pInst, Data16);
  Data16 = (U16)((SectorIndex >> 8) & 0xFFFFu);
  _SetCylReg(pInst, Data16);
  Data16  = (U16)(DH_REG_LBA | ((SectorIndex >> 24) & 0x0FuL));
  Data16 |= (pInst->IsSlave == 0u) ? (U16)DH_REG_DRIVE0 : (U16)DH_REG_DRIVE1;
  Data16 |= (U16)(CMD_WRITE_SECTORS << 8);
  _SetDCReg(pInst, Data16);   // Start command
  //
  // Wait maximum of 8Mio * 400ns = 32s for command to complete
  //
  r = _WaitWhileBusy(pInst);
  if (r != 0) {
    return r;
  }
  //
  // Write sector
  //
  Status = _GetStatus(pInst);
  if ((Status & (STAT_BUSY | STAT_DATA_REQUEST)) != STAT_DATA_REQUEST) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "IDE: _WriteSectors: Not ready to write."));
    return 1;
  }
  do {
    _WriteData(pInst, pBuffer, IDE_SECTOR_SIZE);
    if (RepeatSame == 0u) {
      pBuffer += IDE_SECTOR_SIZE;
    }
    r = _WaitWhileBusy(pInst);
    if (r != 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "IDE: _WriteSectors: Time out while writing."));
      return r;             // Error, device reports busy.
    }
  } while (--NumSectors > 0u);
  //
  // Wait maximum of 8Mio * 400ns = 32s for command to complete.
  //
  r = _WaitWhileBusy(pInst);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "IDE: _WriteSectors: Time out after write."));
    return r;
  }
  //
  // Check for error.
  //
  Status = _GetStatus(pInst);
  if ((Status & (STAT_CORRECTABLE | STAT_WRITE_FAIL | STAT_BUSY)) != 0u) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "IDE: _WriteSectors: Drive reported error."));
    return 1;
  }
  return 0;
}

/*********************************************************************
*
*       _ReadSectors
*/
static int _ReadSectors(const IDE_INST * pInst, U32 SectorIndex, U8 NumSectors, U8 * pBuffer) {
  int r;
  U8  Status;
  U16 Data16;

  //
  // Wait until not busy; should never be the case.
  //
  r = _WaitWhileBusy(pInst);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "IDE: _ReadSectors: Busy on entry."));
    return r;
  }
  r = _SelectDevice(pInst);   // Select device.
  if (r != 0) {
    return r;
  }
  //
  // Setup all necessary registers for command.
  //
  _SetDevControl(pInst, DC_REG_INT_ENABLE);
  Data16 = (U16)(SectorIndex << 8) | (U16)NumSectors;
  _SetSectorReg (pInst, Data16);
  Data16 = (U16)((SectorIndex >>  8) & 0xFFFFu);
  _SetCylReg(pInst, Data16);
  Data16   = (U16)(DH_REG_LBA | ((SectorIndex >> 24) & 0x0FuL));
  Data16  |= (pInst->IsSlave == 0u) ? (U16)DH_REG_DRIVE0 : (U16)DH_REG_DRIVE1;
  Data16  |= (U16)(CMD_READ_SECTORS << 8);
  _SetDCReg(pInst, Data16);   // Start command.
  //
  // Wait maximum of 8Mio * 400ns = 32s for command to complete
  //
  r = _WaitWhileBusy(pInst);
  if (r != 0) {
    return r;
  }
  //
  // Read sector.
  //
  Status = _GetStatus(pInst);
  if ((Status & (STAT_BUSY | STAT_DATA_REQUEST)) == STAT_DATA_REQUEST) {
    do {
      _ReadData(pInst, pBuffer, IDE_SECTOR_SIZE);
      r = _WaitWhileBusy(pInst);
      if (r != 0) {
        return r;                     // Error, device reports busy.
      }
      pBuffer += IDE_SECTOR_SIZE;
    } while (--NumSectors > 0u);
  }
  //
  // Check for error.
  //
  Status = _GetStatus(pInst);
  if (((Status & (STAT_CORRECTABLE | STAT_WRITE_FAIL)) != 0u) || ((Status & STAT_BUSY) != 0u)) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "_ReadSectors: Drive reported error. Device Status = 0x%x.", Status));
    return 1;
  }
  return 0;
}

/*********************************************************************
*
*       _ReadDevicePara
*
*  Function description
*    Reads and Identifies Drive Information from the media.
*
*  Parameters
*    pInst    Driver instance.
*    pBuffer  Pointer to buffer for storing the data.
*
*  Return value
*    ==0      Info has been read and copied to pBuffer.
*    < 0      An error has occurred.
*/
static int _ReadDevicePara(const IDE_INST * pInst, U16 * pBuffer) {
  int r;
  U8  Status;

  //
  // Wait until not busy; should never be the case.
  //
  r = _WaitWhileBusy(pInst);
  if (r != 0) {
    return r;
  }
  r = _SelectDevice(pInst);           // Select device.
  if (r != 0) {
    return r;                         // Error, could not select device.
  }
  //
  // Setup command parameters.
  //
  _Delay(pInst);
  _SetCommand(pInst, CMD_IDENTIFY);   // Start command.
  //
  // Wait maximum of 8Mio * 400ns = 32s for command to complete.
  //
  r = _WaitWhileBusy(pInst);
  if (r != 0) {
    return r;
  }
  //
  // Read info
  //
  Status = _GetStatus(pInst);
  if ((Status & (STAT_BUSY | STAT_DATA_REQUEST)) == STAT_DATA_REQUEST) {
    _ReadData(pInst, SEGGER_PTR2PTR(U8, pBuffer), IDE_SECTOR_SIZE);                     // MISRA deviation D:100[e]
    r = _WaitWhileBusy(pInst);
    if (r != 0) {
      return r;
    }
  }
  //
  // Check for error.
  //
  Status = _GetStatus(pInst);
  if (((Status & (STAT_BUSY | STAT_WRITE_FAIL | STAT_CORRECTABLE)) != 0u) || ((Status & STAT_BUSY) != 0u)) {
    return 1;
  }
  return 0;
}

/*********************************************************************
*
*       _ApplyPara
*
*  Function description
*    Processes the devices parameters.
*
*  Parameters
*    pInst    Driver instance.
*    pPara    Device parameters.
*/
static void _ApplyPara(IDE_INST * pInst, const U16 * pPara) {
  unsigned Data;

  pInst->NumHeads        = FS_LoadU16LE((const U8 *)&pPara[3]);                               // Number of heads
  pInst->SectorsPerTrack = FS_LoadU16LE((const U8 *)&pPara[6]);                               // Number of sectors per track
  pInst->NumSectors      = FS_LoadU32LE((const U8 *)&pPara[60]);                              // Number of sectors
  pInst->BytesPerSector  = IDE_SECTOR_SIZE;
  Data = FS_LoadU16LE(SEGGER_CONSTPTR2PTR(const U8, &pPara[53]));                             // MISRA deviation D:100[e]
  if ((Data & (1uL << 1)) != 0u) {
    U8 MaxPioMode = 2;

    Data = FS_LoadU16LE(SEGGER_CONSTPTR2PTR(const U8, &pPara[64])) & 0xFFuL;                  // MISRA deviation D:100[e]
    do {
      if ((Data & 1u) != 0u) {
        MaxPioMode++;
      }
      Data >>= 1;
    } while (Data != 0u);
    pInst->MaxPioMode = MaxPioMode;
  } else {
    pInst->MaxPioMode = (U8)(FS_LoadU16LE(SEGGER_CONSTPTR2PTR(const U8, &pPara[51])) >> 8);   // Max PIO mode D:100[e].
  }
}

/*********************************************************************
*
*       _IsWriteCacheSupported
*/
static int _IsWriteCacheSupported(const U16 * pPara) {
  int      r;
  unsigned Data;

  r    = 0;
  Data = FS_LoadU16LE((const U8 *)&pPara[82]);
  if ((Data & (1uL << 5)) != 0u) {
    r = 1;
  }
  return r;
}

/*********************************************************************
*
*       _IsWriteCacheEnabled
*/
static int _IsWriteCacheEnabled(const U16 * pPara) {
  int      r;
  unsigned Data;

  r    = 0;
  Data = FS_LoadU16LE((const U8 *)&pPara[85]);
  if ((Data & (1uL << 5)) != 0u) {
    r = 1;
  }
  return r;
}

/*********************************************************************
*
*       _IsReadLookAheadSupported
*/
static int _IsReadLookAheadSupported(const U16 * pPara) {
  int      r;
  unsigned Data;

  r    = 0;
  Data = FS_LoadU16LE((const U8 *)&pPara[82]);
  if ((Data & (1uL << 6)) != 0u) {
    r = 1;
  }
  return r;
}

/*********************************************************************
*
*       _IsReadLookAheadEnabled
*/
static int _IsReadLookAheadEnabled(const U16 * pPara) {
  int      r;
  unsigned Data;

  r    = 0;
  Data = FS_LoadU16LE((const U8 *)&pPara[85]);
  if ((Data & (1uL << 6)) != 0u) {
    r = 1;
  }
  return r;
}

/*********************************************************************
*
*       _Init
*
*  Function description
*    Resets/Initializes the device.
*
*  Parameters
*    pInst    Driver instance.
*
*  Return value
*    ==0      Device has been reset.
*    < 0      An error has occurred.
*
*  Notes
*    (1) This function allocates 512 bytes on the stack.
*/
static int _Init(IDE_INST * pInst) {
  int r;
  int i;
  U16 aPara[256];

  _Reset(pInst);
  //
  //  Do a soft reset of IDE device
  //
  _SetDevControl(pInst, (U8)(DC_REG_SW_RESET | DC_REG_INT_ENABLE));
  //
  // Wait at least 80ms before releasing the soft reset.
  //
  for (i = 0; i < FS_IDE_DEVICE_SELECT_TIMEOUT; i++) {
    _Delay(pInst);
  }
  //
  //  Release soft reset
  //
  _SetDevControl(pInst, DC_REG_INT_ENABLE);
  if (pInst->IsSlave == 0u) {
    _SetDevice(pInst, (U8)(DH_REG_LBA | DH_REG_DRIVE0));
  } else {
    _SetDevice(pInst, (U8)(DH_REG_LBA | DH_REG_DRIVE1));
  }
  //
  // Wait maximum of 8Mio * 400ns = 32s for device to get ready.
  //
  r = _WaitWhileBusy(pInst);
  if (r != 0) {
    return r;
  }
  //
  // Read the device parameters.
  //
  FS_MEMSET(aPara, 0, sizeof(aPara));
  r = _ReadDevicePara(pInst, aPara);
  if (r != 0) {
    return r;           // Error, could not read parameters.
  }
  //
  // Process the device parameters.
  //
  _ApplyPara(pInst, aPara);
  //
  // Setup the storage device for better performance.
  //
  if (_IsWriteCacheSupported(aPara) != 0) {
    if (_IsWriteCacheEnabled(aPara) == 0) {
      r = _WriteFeature(pInst, FEATURE_ENABLE_WRITE_CACHE, 0);
      if (r != 0) {
        return r;
      }
    }
  }
  if (_IsReadLookAheadSupported(aPara) != 0) {
    if (_IsReadLookAheadEnabled(aPara) == 0) {
      r = _WriteFeature(pInst, FEATURE_ENABLE_READ_LOOK_AHEAD, 0);
      if (r != 0) {
        return r;
      }
    }
  }
  pInst->IsInited = 1;
  return r;
}

/*********************************************************************
*
*      _InitIfRequired
*/
static int _InitIfRequired(IDE_INST * pInst) {
  int r;

  r = 0;      // Set to indicate success.
  if (pInst->IsInited == 0u) {
    r = _Init(pInst);
  }
  return r;
}

/*********************************************************************
*
*      _AllocInstIfRequired
*
*  Function description
*    Allocates memory for a driver instance.
*/
static IDE_INST * _AllocInstIfRequired(U8 Unit) {
  IDE_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  pInst = NULL;
  if (Unit < (U8)FS_IDE_NUM_UNITS) {
    pInst = _apInst[Unit];
    if (pInst == NULL) {
       FS_ALLOC_ZEROED_PTR(SEGGER_PTR2PTR(void *, &pInst), (I32)sizeof(IDE_INST), "IDE_INST");    // MISRA deviation D:100[d]
       if (pInst != NULL) {
         pInst->IsSlave = Unit & 1u;
         pInst->Unit    = Unit;
         _apInst[Unit]  = pInst;
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
*    Returns a driver instance by its index.
*/
static IDE_INST * _GetInst(U8 Unit) {
  IDE_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  pInst = NULL;
  if (Unit < (U8)FS_IDE_NUM_UNITS) {
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
*       _IDE_GetStatus
*
*  Function description
*    FS driver function. Get status of the media.
*
*  Parameters
*    Unit   Unit number.
*
*  Return value
*    FS_MEDIA_STATE_UNKNOWN   The state of the media is unknown.
*    FS_MEDIA_NOT_PRESENT     The media is not present.
*    FS_MEDIA_IS_PRESENT      The media is present.
*/
static int _IDE_GetStatus(U8 Unit) {
  IDE_INST * pInst;
  int        r;

  r = FS_MEDIA_STATE_UNKNOWN;
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    ASSERT_HW_TYPE_IS_SET(pInst);
    r = _IsPresent(pInst);
  }
  return r;
}

/*********************************************************************
*
*       _IDE_Read
*
*  Function description
*    Driver callback function.
*    Reads one or more logical sectors from storage device.
*
*  Return value
*    ==0    Data successfully written.
*    !=0    An error has occurred.
*/
static int _IDE_Read(U8 Unit, U32 SectorIndex, void * pBuffer, U32 NumSectors) {
  IDE_INST * pInst;
  U32        i;
  U32        NumLoops;
  int        r;
  U8         NumSectorsAtOnce;
  U8       * pData8;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;                   // Error, could not get driver instance.
  }
  ASSERT_HW_TYPE_IS_SET(pInst);
  r      = 0;
  pData8 = SEGGER_PTR2PTR(U8, pBuffer);                                                           // MISRA deviation D:100[e]
  NumLoops = (NumSectors + NUM_SECTORS_AT_ONCE - 1u) / NUM_SECTORS_AT_ONCE;
  for (i = 0; (i < NumLoops) && (r == 0); i++) {
    if (NumSectors > NUM_SECTORS_AT_ONCE) {
      NumSectorsAtOnce = (U8)NUM_SECTORS_AT_ONCE;
    } else {
     NumSectorsAtOnce  = (U8)(NumSectors & 0xFFu);
    }
    r            = _ReadSectors(pInst, SectorIndex, NumSectorsAtOnce, pData8);
    NumSectors  -= NumSectorsAtOnce;
    pData8      += NumSectorsAtOnce * IDE_SECTOR_SIZE;
    SectorIndex += NumSectorsAtOnce;
  }
  return r;
}

/*********************************************************************
*
*       _IDE_Write
*
*  Function description
*    Driver callback function.
*    Writes one or more logical sectors to storage device.
*
*  Return value
*    ==0    Data successfully written.
*    !=0    An error has occurred.
*/
static int _IDE_Write(U8 Unit, U32 SectorIndex, const void  * pBuffer, U32 NumSectors, U8  RepeatSame) {
  IDE_INST * pInst;
  U32        i;
  U32        NumLoops;
  int        r;
  U8         NumSectorsAtOnce;
  const U8 * pData8;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;                   // Error, could not get driver instance.
  }
  ASSERT_HW_TYPE_IS_SET(pInst);
  r      = 0;
  pData8 = SEGGER_CONSTPTR2PTR(const U8, pBuffer);                                                // MISRA deviation D:100[e]
  NumLoops = (NumSectors + NUM_SECTORS_AT_ONCE - 1u) / NUM_SECTORS_AT_ONCE;
  for (i = 0; (i < NumLoops) && (r == 0); i++) {
    if (NumSectors > NUM_SECTORS_AT_ONCE) {
      NumSectorsAtOnce = (U8)NUM_SECTORS_AT_ONCE;
    } else {
      NumSectorsAtOnce = (U8)(NumSectors & 0xFFu);
    }
    r            = _WriteSectors(pInst, SectorIndex, pData8, NumSectorsAtOnce, RepeatSame);
    NumSectors  -= NumSectorsAtOnce;
    if (RepeatSame == 0u) {
      pData8    += NumSectorsAtOnce * IDE_SECTOR_SIZE;
    }
    SectorIndex += NumSectorsAtOnce;
  }
  return r;
}

/*********************************************************************
*
*       _IDE_IoCtl
*
*  Function description
*    FS driver function. Execute device command.
*
*  Parameters
*    Unit       Driver index.
*    Cmd        Command to be executed.
*    Aux        Parameter depending on command.
*    pBuffer    Pointer to a buffer used for the command.
*
*  Return value
*    Command specific. In general a negative value means an error.
*/
static int _IDE_IoCtl(U8 Unit, I32 Cmd, I32 Aux, void * pBuffer) {
  IDE_INST    * pInst;
  FS_DEV_INFO * pInfo;
  int           r;
  int           Result;

  FS_USE_PARA(Aux);
  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return -1;
  }
  r = -1;                       // Set to indicate an error.
  ASSERT_HW_TYPE_IS_SET(pInst);
  switch (Cmd) {
  case FS_CMD_UNMOUNT:
  case FS_CMD_UNMOUNT_FORCED:
    if (pInst->IsInited != 0u) {
      pInst->IsInited        = 0;
      pInst->NumHeads        = 0;
      pInst->SectorsPerTrack = 0;
      pInst->NumSectors      = 0;
      pInst->BytesPerSector  = 0;
      pInst->MaxPioMode      = 0;
      r = 0;
    }
    break;
  case FS_CMD_GET_DEVINFO:
    pInfo = SEGGER_PTR2PTR(FS_DEV_INFO, pBuffer);         // MISRA deviation D:100[f]
    if (pInfo != NULL) {
      Result = _InitIfRequired(pInst);
      if (Result == 0) {
        pInfo->NumHeads        = pInst->NumHeads;         // Number of heads
        pInfo->SectorsPerTrack = pInst->SectorsPerTrack;  // Number of sectors per track
        pInfo->NumSectors      = pInst->NumSectors;       // Number of sectors
        pInfo->BytesPerSector  = pInst->BytesPerSector;   // Number of bytes in a sector
        r = 0;
      }
    }
    break;
#if FS_SUPPORT_DEINIT
  case FS_CMD_DEINIT:
    FS_FREE(pInst);
    _NumUnits--;
    _apInst[Unit] = NULL;
    r = 0;
    break;
#endif
  case FS_CMD_FREE_SECTORS:
    //
    // Return OK even if we do nothing here in order to
    // prevent that the file system reports an error.
    //
    r = 0;
    break;
  default:
    //
    // Error, command not supported.
    //
    break;
  }
  return r;
}

/*********************************************************************
*
*       _IDE_InitMedium
*
*  Function description
*    Initialize the specified medium.
*
*  Parameters
*    Unit   Driver index.
*/
static int _IDE_InitMedium(U8 Unit) {
  int        r;
  IDE_INST * pInst;

  r     = 1;                // Set to indicate error.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    ASSERT_HW_TYPE_IS_SET(pInst);
    r = _InitIfRequired(pInst);
  }
  return r;
}

/*********************************************************************
*
*       _IDE_AddDevice
*
*  Function description
*    Initializes the driver instance.
*
*  Return value
*    >= 0     OK, driver instance added. Instance number returned.
*    <  0     Error, could not add device.
*/
static int _IDE_AddDevice(void) {
  U8         Unit;
  IDE_INST * pInst;

  if (_NumUnits >= (U8)FS_IDE_NUM_UNITS) {
    return -1;
  }
  Unit  = _NumUnits;
  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return -1;
  }
  _NumUnits++;
  return (int)Unit;
}

/*********************************************************************
*
*       _IDE_GetNumUnits
*/
static int _IDE_GetNumUnits(void) {
  return (int)_NumUnits;
}

/*********************************************************************
*
*       _IDE_GetDriverName
*/
static const char * _IDE_GetDriverName(U8 Unit) {
  FS_USE_PARA(Unit);
  return "ide";
}

/*********************************************************************
*
*       Public data
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_IDE_Driver
*/
const FS_DEVICE_TYPE FS_IDE_Driver = {
  _IDE_GetDriverName,
  _IDE_AddDevice,
  _IDE_Read,
  _IDE_Write,
  _IDE_IoCtl,
  _IDE_InitMedium,
  _IDE_GetStatus,
  _IDE_GetNumUnits
};

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_IDE_Configure
*
*  Function description
*    Configures a driver instance.
*
*  Parameters
*    Unit       Driver index (0-based)
*    IsSlave    Working mode.
*               * 1   Slave mode.
*               * 0   Master mode.
*
*  Additional information
*    This function is optional. The application has to call this function only
*    when the device does not use the default IDE master/slave configuration.
*    By default, all even-numbered units (0, 2, 4...) work in master mode,
*    while all odd-numbered units work in slave mode.
*
*    FS_IDE_Configure function has to be called from FS_X_AddDevices() and
*    it can be called before or after adding the device driver to the file
*    system.
*/
void FS_IDE_Configure(U8 Unit, U8 IsSlave) {
  IDE_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->IsSlave = IsSlave;
  }
}

/*********************************************************************
*
*       FS_IDE_SetHWType
*
*  Function description
*    Configures the hardware access routines.
*
*  Parameters
*    Unit       Driver index (0-based)
*    pHWType    [IN] Hardware access routines (hardware layer).
*
*  Additional information
*    This function is mandatory. The FS_IDE_HW_Default hardware layer
*    is provided to help the porting to the new hardware layer API.
*    This hardware layer contains pointers to the public functions used
*    by the device driver to access the hardware in the version 3.x of emFile.
*    Configure FS_IDE_HW_Default as hardware layer if you do not want
*    to port your existing hardware layer to the new hardware layer API.
*/
void FS_IDE_SetHWType(U8 Unit, const FS_IDE_HW_TYPE * pHWType) {
  IDE_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->pHWType = pHWType;
  }
}

/*************************** End of file ****************************/
