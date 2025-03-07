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
File        : FS_NOR_PHY_ST_M25.c
Purpose     : Low level flash driver for NOR SPI flash (ST/Numonyx M25 series)
Literature  :
  [1] M25P10 1 Mbit, Low Voltage, Serial Flash Memory With 20 MHz SPI Bus Interface
    (\\fileserver\Techinfo\Company\ST\Flash\SerialFlash\M25P10.pdf)
  [2] 16 Mbit SPI Serial Flash SST25VF016B
    (\\fileserver\Techinfo\Company\SST\Flash\SST25VF016B.pdf)
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
*       NOR flash commands
*/
#define CMD_WRSR              0x01    // Write the status register
#define CMD_PP                0x02    // Page Program
#define CMD_RDSR              0x05    // Read Status Register
#define CMD_WREN              0x06    // Write Enable
#define CMD_FAST_READ         0x0B    // Read Data Bytes at Higher Speed
#define CMD_RDID              0x9F    // Read Identification
#define CMD_RES               0xAB    // Release from deep power-down
#define CMD_EN4B              0xB7    // Enter 4-byte address mode
#define CMD_SE                0xD8    // Sector Erase
#define CMD_RDFSR             0x70    // Read flag status register
#define CMD_CLFSR             0x50    // Clear flag status register


/*********************************************************************
*
*       Status register
*/
#define STATUS_BP_MASK        0x3Cu   // Bit mask of the write protection flags
#define STATUS_WEL_MASK       0x02u
#define STATUS_BUSY_MASK      0x01u

/*********************************************************************
*
*       Flag status register
*/
#define FLAG_READY_MASK       0x80u
#define FLAG_ERROR_MASK       0x3Au

/*********************************************************************
*
*       ASSERT_SECTOR_INDEX_IS_IN_RANGE
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_SECTOR_INDEX_IS_IN_RANGE(pInst, SectorIndex)                 \
    if ((SectorIndex) >= (pInst)->NumSectors) {                               \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: Invalid sector index.")); \
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);                                    \
    }
#else
  #define ASSERT_SECTOR_INDEX_IS_IN_RANGE(pInst, SectorIndex)
#endif

/*********************************************************************
*
*       ASSERT_DEVICE_IS_SET
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_DEVICE_IS_SET(pInst)                                         \
    if ((pInst)->pDevicePara == NULL) {                                       \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: Device not set."));       \
      FS_X_PANIC(FS_ERRCODE_UNKNOWN_DEVICE);                                  \
    }
#else
  #define ASSERT_DEVICE_IS_SET(pInst)
#endif

/*********************************************************************
*
*       ASSERT_UNIT_NO_IS_IN_RANGE
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)                                    \
    if ((Unit) >= (U8)FS_NOR_NUM_UNITS) {                                     \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: Invalid unit number."));  \
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);                                    \
    }
#else
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)
#endif

/*********************************************************************
*
*       ASSERT_HW_TYPE_IS_SET
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_HW_TYPE_IS_SET(pInst)                                     \
    if ((pInst)->pHWType == NULL) {                                        \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR_SPI: HW layer not set.")); \
      FS_X_PANIC(FS_ERRCODE_HW_LAYER_NOT_SET);                             \
    }
#else
  #define ASSERT_HW_TYPE_IS_SET(Unit)
#endif

/*********************************************************************
*
*       ASSERT_DEVICE_LIST_NOT_FULL
*/
#if (FS_NOR_MAX_NUM_DEVICES > 0)
  #if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL)
    #define ASSERT_DEVICE_LIST_NOT_FULL()                                       \
      if (_NumDevicesUser >= (U8)FS_NOR_MAX_NUM_DEVICES) {                      \
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: Device list is full."));  \
        FS_X_PANIC(FS_ERRCODE_INVALID_PARA);                                    \
      }
  #else
    #define ASSERT_DEVICE_LIST_NOT_FULL()
  #endif
#endif

/*********************************************************************
*
*      Local data types
*
**********************************************************************
*/

/*********************************************************************
*
*      NOR_SPI_INST
*/
typedef struct {
  U32                            BaseAddr;                // Address of the first byte in the NOR flash device.
  U32                            StartAddrConf;           // Configured start address
  U32                            StartAddrUsed;           // Start address actually used for data storage (aligned to a physical sector boundary)
  U32                            NumBytes;                // Total number of bytes to be used as storage
  U32                            NtimeoutSectorErase;     // Number of cycles to wait for sector erase completion until timeout
  U32                            NtimeoutPageWrite;       // Number of cycles to wait for page program completion until timeout
  const FS_NOR_SPI_DEVICE_PARA * pDevicePara;             // Parameters of the selected NOR flash device.
  U32                            NumSectors;              // Total number of physical sectors
  U8                             IsInited;                // Set to 1 if the NOR flash device has been identified and ready to be used.
  U8                             IsHWInited;              // Set to 1 if the HW layer is initialized.
  U8                             IsUserConfigured;        // Set to 1 if the geometry of the NOR device has been set at file system configuration (no auto-detection).
  U8                             Unit;                    // Index of this physical layer (0-based).
  const FS_NOR_HW_TYPE_SPI     * pHWType;                 // HW access routines
} NOR_SPI_INST;

/*********************************************************************
*
*      Static const data
*
**********************************************************************
*/
static const FS_NOR_SPI_DEVICE_PARA _aDeviceListDefault[] = {
  {0x11, 15, 8, 3,    4, 0, 0, 0, 0, 0, 0},   //   1 MBit
  {0x12, 16, 8, 3,    4, 0, 0, 0, 0, 0, 0},   //   2 MBit
  {0x13, 16, 8, 3,    8, 0, 0, 0, 0, 0, 0},   //   4 MBit
  {0x14, 16, 8, 3,   16, 0, 0, 0, 0, 0, 0},   //   8 MBit
  {0x15, 16, 8, 3,   32, 0, 0, 0, 0, 0, 0},   //  16 MBit
  {0x16, 16, 8, 3,   64, 0, 0, 0, 0, 0, 0},   //  32 MBit
  {0x17, 16, 8, 3,  128, 0, 0, 0, 0, 0, 0},   //  64 MBit
  {0x18, 18, 8, 3,   64, 0, 0, 0, 0, 0, 0},   // 128 MBit
  {0x19, 16, 8, 4,  512, 0, 0, 0, 0, 0, 0},   // 256 MBit
  {0x1A, 16, 8, 4, 1024, 0, 0, 0, 0, 0, 0},   // 512 MBit
  {0x00,  0, 0, 0,    0, 0, 0, 0, 0, 0, 0}
};

/*********************************************************************
*
*      Static data
*
**********************************************************************
*/
static NOR_SPI_INST                   * _apInst[FS_NOR_NUM_UNITS];
#if FS_NOR_MAX_NUM_DEVICES
  static const FS_NOR_SPI_DEVICE_PARA * _apDeviceListUser[FS_NOR_MAX_NUM_DEVICES];
  static U8                             _NumDevicesUser = 0;
#endif

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
*      _Write
*/
static void _Write(const NOR_SPI_INST * pInst, const U8 * pData, unsigned NumBytes) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWType->pfWrite(Unit, pData, (int)NumBytes);
}

/*********************************************************************
*
*      _Read
*/
static void _Read(const NOR_SPI_INST * pInst, U8 * pData, unsigned NumBytes) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWType->pfRead(Unit, pData, (int)NumBytes);
}

/*********************************************************************
*
*      _EnableCS
*/
static void _EnableCS(const NOR_SPI_INST * pInst) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWType->pfEnableCS(Unit);
}

/*********************************************************************
*
*      _DisableCS
*/
static void _DisableCS(const NOR_SPI_INST * pInst) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWType->pfDisableCS(Unit);
}

/*********************************************************************
*
*      _EnableWrite
*
*   Function description
*     Sets the write enable latch in the SPI flash.
*/
static int _EnableWrite(const NOR_SPI_INST * pInst) {
  U8  Cmd;
  U32 TimeOut;
  U8  Status;
  int r;

  //
  // Set the flag in NOR flash device.
  //
  Cmd = CMD_WREN;
  _EnableCS(pInst);
  _Write(pInst, &Cmd, sizeof(Cmd));
  _DisableCS(pInst);
  //
  // Check if the flag has been set.
  //
  TimeOut = pInst->NtimeoutPageWrite;
  _EnableCS(pInst);
  Cmd = CMD_RDSR;
  _Write(pInst, &Cmd, sizeof(Cmd));
  r = 1;        // Set to indicate an error.
  do {
    _Read(pInst, &Status, sizeof(Status));
    if ((Status & STATUS_WEL_MASK) != 0u) {
      r = 0;    // OK, WEL flag is set.
      break;
    }
  } while (--TimeOut != 0u);
  _DisableCS(pInst);
  return r;
}

/*********************************************************************
*
*      _ReadFlagStatusRegister
*/
static U8 _ReadFlagStatusRegister(const NOR_SPI_INST * pInst) {
  U8 Status;
  U8 Cmd;

  Cmd = CMD_RDFSR;
  _EnableCS(pInst);
  _Write(pInst, &Cmd, sizeof(Cmd));
  _Read(pInst, &Status, sizeof(Status));
  _DisableCS(pInst);
  return Status;
}

/*********************************************************************
*
*      _ClearFlagStatusRegister
*/
static void _ClearFlagStatusRegister(const NOR_SPI_INST * pInst) {
  U8 Cmd;

  Cmd = CMD_CLFSR;
  _EnableCS(pInst);
  _Write(pInst, &Cmd, sizeof(Cmd));
  _DisableCS(pInst);
}

/*********************************************************************
*
*      _WaitForEndOfOperation
*
*   Function description
*     Waits for flash to be ready for next command.
*/
static int _WaitForEndOfOperation(const NOR_SPI_INST * pInst, U32 TimeOut) {
  int r;
  U8  Cmd;
  U8  Status;
  U8  Status2;
  U8  Flags;

  r = 1;              // Set to indicate an error.
  Flags = pInst->pDevicePara->Flags;
  if ((Flags & FS_NOR_SPI_DEVICE_FLAG_ERROR_STATUS) != 0u) {
    do {
      //
      // We need to poll the flag status register two times, one time for each die on the device.
      //
      Status  = _ReadFlagStatusRegister(pInst);
      Status2 = _ReadFlagStatusRegister(pInst);
      if (((Status & FLAG_READY_MASK) != 0u) && ((Status2 & FLAG_READY_MASK) != 0u)) {
        if (((Status & FLAG_ERROR_MASK) != 0u) || ((Status2 & FLAG_ERROR_MASK) != 0u)) {
          _ClearFlagStatusRegister(pInst);
          break;        // Error, operation failed.
        }
        r = 0;          // OK, SPI flash is ready for a new operation.
        break;
      }
    } while (--TimeOut != 0u);
  } else {
    Cmd = CMD_RDSR;
    _EnableCS(pInst);
    _Write(pInst, &Cmd, sizeof(Cmd));
    do {
      _Read(pInst, &Status, sizeof(Status));
      if ((Status & STATUS_BUSY_MASK) == 0u) {
        r = 0;          // OK, SPI flash is ready for a new operation.
        break;
      }
    } while (--TimeOut != 0u);
    _DisableCS(pInst);
  }
  return r;
}

/*********************************************************************
*
*      _ReadStatusRegister
*
*   Function description
*     Returns the contents of the status register.
*/
static U8 _ReadStatusRegister(const NOR_SPI_INST * pInst) {
  U8  Cmd;
  U8  Status;

  Cmd = CMD_RDSR;
  _EnableCS(pInst);
  _Write(pInst, &Cmd, sizeof(Cmd));
  _Read(pInst, &Status, sizeof(Status));
  _DisableCS(pInst);
  return Status;
}

/*********************************************************************
*
*      _WriteStatusRegister
*
*   Function description
*     Writes a value to status register. Typ. called to remove the write protection of physical blocks.
*/
static int _WriteStatusRegister(const NOR_SPI_INST * pInst, U8 Value) {
  int r;
  U8  aCmd[2];

  r = _EnableWrite(pInst);
  if (r == 0) {
    aCmd[0] = CMD_WRSR;
    aCmd[1] = Value;
    _EnableCS(pInst);
    _Write(pInst, aCmd, sizeof(aCmd));
    _DisableCS(pInst);
  }
  return r;
}

/*********************************************************************
*
*      _RemoveWriteProtection
*
*  Function description
*    Makes all physical sectors writable.
*/
static int _RemoveWriteProtection(const NOR_SPI_INST * pInst) {
  U8  Status;
  int r;

  r      = 0;                     // No error so far.
  Status = _ReadStatusRegister(pInst);
  if ((Status & STATUS_BP_MASK) != 0u) {
    r = _WriteStatusRegister(pInst, 0);
    if (r == 0) {
      r = _WaitForEndOfOperation(pInst, pInst->NtimeoutPageWrite);
    }
  }
  return r;
}

/*********************************************************************
*
*      _Enter4ByteAddrMode
*
*  Function description
*    Requests the device to accept 4-byte addresses.
*/
static void _Enter4ByteAddrMode(const NOR_SPI_INST * pInst) {
  U8 Cmd;

  Cmd = CMD_EN4B;
  _EnableCS(pInst);
  _Write(pInst, &Cmd, sizeof(Cmd));
  _DisableCS(pInst);
}

/*********************************************************************
*
*      _ReadDeviceId
*/
static void _ReadDeviceId(const NOR_SPI_INST * pInst, U8 * pId, unsigned NumBytes) {
  U8 Cmd;

  Cmd = CMD_RDID;
  _EnableCS(pInst);
  _Write(pInst, &Cmd, sizeof(Cmd));
  _Read(pInst, pId, NumBytes);
  _DisableCS(pInst);
}

/*********************************************************************
*
*      _IdentifyDevice
*/
static int _IdentifyDevice(NOR_SPI_INST * pInst) {
  int                            r;
  U8                             aData[3];
  const FS_NOR_SPI_DEVICE_PARA * pDevicePara;
  U8                             Id;

  r = 0;        // Set to indicate that the device has been identified.
  //
  // Identify the NOR flash by checking the device id.
  //
  _ReadDeviceId(pInst, aData, sizeof(aData));
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR_SPI: Found Serial NOR Flash with following ids: Manufacturer: 0x%2x, MemoryType: 0x%2x,", aData[0], aData[1]));
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, " MemoryCapacity 0x%2x.\n", aData[0], aData[1]));
  Id = aData[2];
  //
  // First, check the user defined list, if any.
  //
#if FS_NOR_MAX_NUM_DEVICES
  if (_NumDevicesUser != 0u) {
    const FS_NOR_SPI_DEVICE_PARA ** ppDevicePara;
    U8                              NumDevices;

    ppDevicePara = &_apDeviceListUser[0];
    NumDevices   = _NumDevicesUser;
    do {
      pDevicePara = *ppDevicePara;
      if (pDevicePara->Id == Id) {
        pInst->pDevicePara = pDevicePara;
        break;
      }
      ppDevicePara++;
    } while (--NumDevices != 0u);
  }
#endif
  if (pInst->pDevicePara == NULL) {
    //
    // Now, check the default list.
    //
    pDevicePara = &_aDeviceListDefault[0];
    for (;;) {
      if (pDevicePara->Id == Id) {
        pInst->pDevicePara = pDevicePara;
        break;
      }
      pDevicePara++;
      if (pDevicePara->Id == 0u) {
        r = 1;                  // Error, could identify NOR device.
        break;
      }
    }
  }
  return r;
}

/*********************************************************************
*
*      _InitHW
*
*  Function description
*    Initializes the HW layer.
*
*  Notes
*    (1) The emFile manual wrongly documented that the initialization function
*        of the HW layer has to return the SPI frequency in Hz but all the emFile
*        HW samples returned the frequency in kHz. To stay compatible with these
*        HW layers we try to detect here if the frequency is returned in Hz or kHz
*        by checking the range of the returned value. The current serial NOR flash
*        devices typically operate at a maximum frequency of about 140 MHz but to
*        be on the safe side we consider 500 MHz as the maximum  operating frequency
*        and we test against this value. That is if the returned value is larger than
*        500,000 we consider the value as being returned in Hz and we convert it to kHz.
*/
static int _InitHW(NOR_SPI_INST * pInst) {
  U32 Freq_kHz;
  U32 srpms;          // Status Requests Per Millisecond.
  U8  Unit;

  ASSERT_HW_TYPE_IS_SET(pInst);
  Unit = pInst->Unit;
  //
  // Initialize the HW.
  //
  Freq_kHz = (U32)pInst->pHWType->pfInit(Unit);
  if (Freq_kHz == 0u) {
    return 1;                         // Error, could not initialize hardware.
  }
  if (Freq_kHz > 500000uL) {          // Note 1
    Freq_kHz /= 1000u;
  }
  //
  // Calculate the number of status requests that can be executed in 1 millisecond.
  // At least 16-bits are exchanged on each NOR device status request.
  //
  srpms = ((Freq_kHz * 1000u) >> 4) / 1000u;
  pInst->NtimeoutSectorErase = srpms * (U32)FS_NOR_TIMEOUT_SECTOR_ERASE;
  pInst->NtimeoutPageWrite   = srpms * (U32)FS_NOR_TIMEOUT_PAGE_WRITE;
  return 0;
}

/*********************************************************************
*
*      _InitHWIfRequired
*
*   Function description
*     Initializes the HW layer if not already initialized.
*/
static int _InitHWIfRequired(NOR_SPI_INST * pInst) {
  int r;

  r = 0;
  if (pInst->IsHWInited == 0u) {
    r = _InitHW(pInst);
    if (r == 0) {
      pInst->IsHWInited = 1;
    }
  }
  return r;
}

/*********************************************************************
*
*      _Init
*
*  Function description
*    Initializes the HW layer and auto-detects the NOR flash if not configured manually.
*
*  Return value
*    ==0    NOR device identified and initialized.
*    !=0    An error occurred.
*/
static int _Init(NOR_SPI_INST * pInst) {
  U8                             Cmd;
  U8                             aData[3];
  const FS_NOR_SPI_DEVICE_PARA * pDevicePara;
  U32                            NumSectors;
  U32                            SectorSize;
  U32                            BaseAddr;
  U32                            StartAddrUsed;
  I32                            NumBytesToSkip;
  I32                            NumBytesRem;
  U32                            NumBytesSkipped;
  int                            r;
  U8                             Flags;
  U32                            NumSectorsRem;

  //
  // Initialize the SPI hardware.
  //
  r = _InitHWIfRequired(pInst);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _Init: Could not initialize HW."));
    return 1;             // Error
  }
  //
  // Release device from an possible deep power-down mode (Mode for PE devices and newer P -devices, which do not need or accept dummy bytes).
  //
  Cmd = CMD_RES;
  _EnableCS(pInst);
  _Write(pInst, &Cmd, sizeof(Cmd));
  _DisableCS(pInst);
  //
  // Release device from an possible Deep power mode with dummy bytes.
  //
  _EnableCS(pInst);
  _Write(pInst, &Cmd, sizeof(Cmd));
  FS_MEMSET(aData, 0, sizeof(aData));
  _Write(pInst, aData, sizeof(aData));
  _DisableCS(pInst);
  //
  // Try to identify the NOR flash device.
  //
  if (pInst->pDevicePara == NULL) {
    r = _IdentifyDevice(pInst);
    if (r != 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _Init: Could not identify device."));
      return 1;                   // Error, could identify NOR device.
    }
  } else {
    //
    // This is typically done for older devices, which do not support RDID command.
    //
    FS_DEBUG_LOG((FS_MTYPE_DRIVER, "NOR_SPI: _Init: Device is configured by the user.\n"));
  }
  pDevicePara = pInst->pDevicePara;
  //
  // Switch to 4-byte address if required.
  //
  if (pDevicePara->NumBytesAddr == 4u) {
    //
    // The write latch has to be set for some Micron devices
    // before switching to 4-byte address mode.
    //
    Flags = pDevicePara->Flags;
    if ((Flags & FS_NOR_SPI_DEVICE_FLAG_WEL_ADDR_MODE) != 0u) {
      r = _EnableWrite(pInst);
      if (r != 0) {
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _Init: Could not enter 4-byte address mode."));
        return 1;                 // Error
      }
    }
    _Enter4ByteAddrMode(pInst);
  }
  //
  // Remove the write protection of all physical sectors.
  //
  (void)_RemoveWriteProtection(pInst);
  //
  // OK, the device is identified. Determine which physical sectors are used as storage.
  //
  NumBytesSkipped = 0;
  BaseAddr        = pInst->BaseAddr;
  NumBytesToSkip  = (I32)pInst->StartAddrConf - (I32)BaseAddr;
  NumBytesRem     = (I32)pInst->NumBytes;
  SectorSize      = 1uL << pInst->pDevicePara->ldBytesPerSector;
  NumSectors      = pInst->pDevicePara->NumSectors;
  //
  // Take care of bytes to skip before storage area.
  //
  while ((NumSectors != 0u) && (NumBytesToSkip > 0)) {
    NumBytesToSkip  -= (I32)SectorSize;
    NumBytesSkipped += SectorSize;
    NumSectors--;
  }
  StartAddrUsed = BaseAddr + NumBytesSkipped;
  if (NumSectors != 0u) {
    NumSectorsRem = (U32)NumBytesRem / SectorSize;
    if (NumSectors > NumSectorsRem) {
      NumSectors  = NumSectorsRem;
    }
  }
  if (NumSectors == 0u) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _Init: Device size too small for the configuration."));
    return 1;               // Error, no sectors available for storage.
  }
  pInst->StartAddrUsed = StartAddrUsed;
  pInst->NumSectors    = NumSectors;
  return 0;                 // OK.
}

/*********************************************************************
*
*      _InitIfRequired
*
*  Function description
*    Initializes the HW layer and identifies the NOR flash if not already done.
*
*  Return value
*    ==0    NOR device identified and initialized.
*    !=0    An error occurred.
*/
static int _InitIfRequired(NOR_SPI_INST * pInst) {
  int r;

  r = 0;
  if (pInst->IsInited == 0u) {
    r = _Init(pInst);
    pInst->IsInited = 1;
  }
  return r;
}

/*********************************************************************
*
*      _WritePage
*
*  Function description
*    Writes data to a page of SPI flash.
*
*  Return value
*    ==0    O.K., data has been written
*    !=0    An error occurred
*/
static int _WritePage(const NOR_SPI_INST * pInst, U32 Addr, const U8 * pSrc, U32 NumBytes) {
  U8       aCmd[5];
  U8       NumBytesAddr;
  unsigned NumBytesCmd;
  int      r;

  NumBytesAddr = pInst->pDevicePara->NumBytesAddr;
  NumBytesCmd  = 0;
  //
  // Send write page command to serial flash
  //
  aCmd[NumBytesCmd++] = CMD_PP;
  if (NumBytesAddr == 4u) {
    aCmd[NumBytesCmd++] = (U8)(Addr >> 24);
  }
  aCmd[NumBytesCmd++] = (U8)(Addr   >> 16);
  aCmd[NumBytesCmd++] = (U8)(Addr   >>  8);
  aCmd[NumBytesCmd++] = (U8)Addr;
  r = _EnableWrite(pInst);
  if (r == 0) {
    _EnableCS(pInst);
    _Write(pInst, aCmd, NumBytesCmd);
    _Write(pInst, pSrc, NumBytes);
    _DisableCS(pInst);
    r = _WaitForEndOfOperation(pInst, pInst->NtimeoutPageWrite);
  }
  return r;
}

/*********************************************************************
*
*      _WriteSectorData
*
*  Function description
*    Writes data to SPI flash and handles relocation if necessary.
*
*  Return value
*    ==0    O.K., data has been written
*    !=0    An error occurred
*/
static int _WriteSectorData(const NOR_SPI_INST * pInst, U32 Addr, const U8 * pData, U32 NumBytes) {
  U32 NumBytes2Write;
  int r;
  U32 BytesPerPage;

  BytesPerPage = 1uL << pInst->pDevicePara->ldBytesPerPage;
  if ((Addr & (BytesPerPage - 1u)) != 0u) {
    NumBytes2Write = BytesPerPage - (Addr & (BytesPerPage - 1u));
    NumBytes2Write = SEGGER_MIN(NumBytes2Write, NumBytes);
    r = _WritePage(pInst, Addr, pData, NumBytes2Write);
    if (r != 0) {
      return 1;           // Error, write failed.
    }
    pData    += NumBytes2Write;
    NumBytes -= NumBytes2Write;
    Addr     += (U32)NumBytes2Write;
  }
  if (NumBytes > 0u) {
    do {
      NumBytes2Write = SEGGER_MIN(NumBytes, BytesPerPage);
      r = _WritePage(pInst, Addr, pData, NumBytes2Write);
      if (r != 0) {
        return 1;         // Error, write failed.
      }
      pData    += NumBytes2Write;
      NumBytes -= NumBytes2Write;
      Addr     += (U32)NumBytes2Write;
    } while (NumBytes != 0u);
  }
  return 0;               // OK, data written successfully.
}

/*********************************************************************
*
*      _AllocInstIfRequired
*
*  Function description
*    Allocates memory for the instance of a physical layer.
*/
static NOR_SPI_INST * _AllocInstIfRequired(U8 Unit) {
  NOR_SPI_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  pInst = NULL;
  if (Unit < (U8)FS_NOR_NUM_UNITS) {
    pInst = _apInst[Unit];
    if (pInst == NULL) {
      pInst = SEGGER_PTR2PTR(NOR_SPI_INST, FS_ALLOC_ZEROED((I32)sizeof(NOR_SPI_INST), "NOR_SPI_INST"));
      if (pInst != NULL) {
         pInst->Unit  = Unit;
        _apInst[Unit] = pInst;
      }
    }
  }
  return pInst;
}

/*********************************************************************
*
*      _GetInst
*/
static NOR_SPI_INST * _GetInst(U8 Unit) {
  NOR_SPI_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  pInst = NULL;
  if (Unit < (U8)FS_NOR_NUM_UNITS) {
    pInst = _apInst[Unit];
  }
  return pInst;
}

/*********************************************************************
*
*      Static code (public via callback)
*
**********************************************************************
*/

/*********************************************************************
*
*      _PHY_WriteOff
*
*  Function description
*    Physical layer function.
*    This routine writes data into any section of the flash. It does not
*    check if this section has been previously erased; this is in the responsibility of the user program.
*    Data written into multiple sectors at a time can be handled by this routine.
*
*  Return value
*    ==0    O.K., data has been written.
*    !=0    An error occurred.
*/
static int _PHY_WriteOff(U8 Unit, U32 Off, const void * pData, U32 NumBytes) {
  int            r;
  NOR_SPI_INST * pInst;
  const U8     * pData8;

  r     = 1;             // Set to indicate an error.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    r = _InitIfRequired(pInst);
    if (r == 0) {
      if (NumBytes != 0u) {
        pData8 = SEGGER_CONSTPTR2PTR(const U8, pData);
        //
        // Skip leading bytes set to 0xFF.
        //
        for (;;) {
          if (*pData8 != 0xFFu) {
            break;
          }
          ++pData8;
          ++Off;
          if (--NumBytes == 0u) {
            break;
          }
        }
        if (NumBytes != 0u) {
          //
          // Skip trailing bytes set to 0xFF.
          //
          for (;;) {
            if (*(pData8 + (NumBytes - 1u)) != 0xFFu) {
              break;
            }
            if (--NumBytes == 0u) {
              break;
            }
          }
          if (NumBytes != 0u) {
            Off += pInst->StartAddrUsed;
            r = _WriteSectorData(pInst, Off, pData8, NumBytes);
          }
        }
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
*    Physical layer function. Reads data from the given offset of the flash.
*/
static int _PHY_ReadOff(U8 Unit, void * pDest, U32 Off, U32 NumBytes) {
  U32            Addr;
  int            r;
  U8             NumBytesAddr;
  U32            NumBytesCmd;
  NOR_SPI_INST * pInst;
  U8             aCmd[6];

  r     = 1;              // Set to indicate an error.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    r = _InitIfRequired(pInst);
    if (r == 0) {
      Addr = pInst->StartAddrUsed + Off;
      NumBytesAddr = pInst->pDevicePara->NumBytesAddr;
      NumBytesCmd  = 0;
      aCmd[NumBytesCmd++] = CMD_FAST_READ;
      if (NumBytesAddr == 4u) {
        aCmd[NumBytesCmd++] = (U8)(Addr >> 24);
      }
      aCmd[NumBytesCmd++]   = (U8)(Addr >> 16);
      aCmd[NumBytesCmd++]   = (U8)(Addr >>  8);
      aCmd[NumBytesCmd++]   = (U8)Addr;
      aCmd[NumBytesCmd++]   = 0xFF;
      _EnableCS(pInst);
      _Write(pInst, aCmd, NumBytesCmd);
      _Read(pInst, SEGGER_PTR2PTR(U8, pDest), NumBytes);
      _DisableCS(pInst);
    }
  }
  return r;
}

/*********************************************************************
*
*      _PHY_EraseSector
*
*  Function description
*    Physical layer function. Erases one physical sector.
*
*  Return value
*    ==0    O.K., sector has been erased
*    !=0    An error occurred
*/
static int _PHY_EraseSector(U8 Unit, unsigned SectorIndex) {
  int            r;
  U32            SectorSize;
  U32            Addr;
  U8             NumBytesAddr;
  U32            NumBytesCmd;
  NOR_SPI_INST * pInst;
  U8             aCmd[5];

  r     = 1;                // Set to indicate an error.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    ASSERT_SECTOR_INDEX_IS_IN_RANGE(pInst, SectorIndex);
    ASSERT_DEVICE_IS_SET(pInst);
    r = _InitIfRequired(pInst);
    if (r == 0) {
      //
      // Send Sector erase command to flash
      //
      SectorSize    = 1uL << pInst->pDevicePara->ldBytesPerSector;
      Addr          = SectorSize * SectorIndex;
      Addr         += pInst->StartAddrUsed;
      NumBytesAddr  = pInst->pDevicePara->NumBytesAddr;
      NumBytesCmd   = 0;
      aCmd[NumBytesCmd++] = CMD_SE;
      if (NumBytesAddr == 4u) {
        aCmd[NumBytesCmd++] = (U8)(Addr >> 24);
      }
      aCmd[NumBytesCmd++]   = (U8)(Addr >> 16);
      aCmd[NumBytesCmd++]   = (U8)(Addr >>  8);
      aCmd[NumBytesCmd++]   = (U8)Addr;
      r = _EnableWrite(pInst);
      if (r == 0) {
        _EnableCS(pInst);
        _Write(pInst, aCmd, NumBytesCmd);
        _DisableCS(pInst);
        r = _WaitForEndOfOperation(pInst, pInst->NtimeoutSectorErase);
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _PHY_GetSectorInfo
*
*  Function description
*    Physical layer function. Returns the offset and length of the given physical sector.
*/
static void _PHY_GetSectorInfo(U8 Unit, unsigned SectorIndex, U32 * pOff, U32 * pLen) {
  U32            SectorOff;
  U32            SectorSize;
  NOR_SPI_INST * pInst;
  int            r;

  SectorOff  = 0;
  SectorSize = 0;
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    ASSERT_SECTOR_INDEX_IS_IN_RANGE(pInst, SectorIndex);
    ASSERT_DEVICE_IS_SET(pInst);
    r = _InitIfRequired(pInst);
    if (r == 0) {
      //
      // Calculate the result.
      //
      SectorSize = 1uL << pInst->pDevicePara->ldBytesPerSector;
      SectorOff  = SectorSize * SectorIndex;
    }
  }
  if (pOff != NULL) {
    *pOff = SectorOff;
  }
  if (pLen != NULL) {
    *pLen = SectorSize;
  }
}

/*********************************************************************
*
*       _PHY_GetNumSectors
*
*  Function description
*    Physical layer function. Returns the number total number of physical sectors in the SPI flash.
*/
static int _PHY_GetNumSectors(U8 Unit) {
  int            NumSectors;
  NOR_SPI_INST * pInst;
  int            r;

  NumSectors = 0;                     // Set to indicate an error.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    r = _InitIfRequired(pInst);
    if (r == 0) {
      NumSectors = (int)pInst->NumSectors;
    }
  }
  return NumSectors;
}

/*********************************************************************
*
*       _PHY_Configure
*
*  Function description
*    Physical layer function. Configures a single instance of the driver.
*/
static void _PHY_Configure(U8 Unit, U32 BaseAddr, U32 StartAddr, U32 NumBytes) {
  NOR_SPI_INST * pInst;

  FS_DEBUG_ASSERT(FS_MTYPE_DRIVER, StartAddr >= BaseAddr);
  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->BaseAddr      = BaseAddr;
    pInst->StartAddrConf = StartAddr;
    pInst->NumBytes      = NumBytes;
    pInst->IsInited      = 0;         // The physical layer needs to be re-initialized.
    pInst->IsHWInited    = 0;         // The hardware layer needs to be re-initialized.
  }
}

/*********************************************************************
*
*       _PHY_OnSelectPhy
*
*  Function description
*    Physical layer function. Called right after selection of the physical layer.
*/
static void _PHY_OnSelectPhy(U8 Unit) {
  (void)_AllocInstIfRequired(Unit);
}

/*********************************************************************
*
*       _PHY_DeInit
*
*  Function description
*    Physical layer function.
*    This function frees up memory resources allocated for the instance of a physical layer.
*
*  Parameters
*    Unit   Physical layer number.
*/
static void _PHY_DeInit(U8 Unit) {
#if FS_SUPPORT_DEINIT
  NOR_SPI_INST * pInst;

  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    if (pInst->IsUserConfigured != 0u) {                      // Has NOR device been manually configured?
      FS_Free(SEGGER_PTR2PTR(void, pInst->pDevicePara));      //lint !e9005 Attempt to cast away const/volatile from a pointer or reference. Rationale: pDevicePara can point to const data as well as to memory dynamically allocated.
    }
    FS_Free(SEGGER_PTR2PTR(void, pInst));
  }
  _apInst[Unit] = NULL;
#else
  FS_USE_PARA(Unit);
#endif
}

/*********************************************************************
*
*       Public const data
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_NOR_PHY_ST_M25
*/
const FS_NOR_PHY_TYPE FS_NOR_PHY_ST_M25 = {       // ST M25P compliant Serial NOR flash
  _PHY_WriteOff,
  _PHY_ReadOff,
  _PHY_EraseSector,
  _PHY_GetSectorInfo,
  _PHY_GetNumSectors,
  _PHY_Configure,
  _PHY_OnSelectPhy,
  _PHY_DeInit,
  NULL,
  NULL
};

/*********************************************************************
*
*      Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_NOR_SPI_Configure
*
*  Function description
*    Configures the parameters of the NOR flash device.
*
*  Parameters
*    Unit           Index of the physical layer (0-based).
*    SectorSize     The size of a physical sector in bytes.
*    NumSectors     The number of physical sectors available.
*
*  Additional information
*    This function is optional. By default the physical layer identifies
*    the parameters of the NOR flash device automatically using the
*    information returned by the READ ID (0x9F) command. This method
*    does not work for some older ST M25 NOR flash devices. In this
*    case the application can use FS_NOR_SPI_Configure() to specify
*    the parameters of the NOR flash device. SPI NOR (M25 series) flash
*    devices have uniform sectors, which means only one sector size
*    is used for the entire device.
*
*    The capacity of the serial NOR flash device is determined as follows:
*    +-------------------+-------------------+
*    | Value of 3rd byte | Capacity in Mbits |
*    +-------------------+-------------------+
*    | 0x11              | 1                 |
*    +-------------------+-------------------+
*    | 0x12              | 2                 |
*    +-------------------+-------------------+
*    | 0x13              | 4                 |
*    +-------------------+-------------------+
*    | 0x14              | 8                 |
*    +-------------------+-------------------+
*    | 0x15              | 16                |
*    +-------------------+-------------------+
*    | 0x16              | 32                |
*    +-------------------+-------------------+
*    | 0x17              | 64                |
*    +-------------------+-------------------+
*    | 0x18              | 128               |
*    +-------------------+-------------------+
*
*    The application required to call FS_NOR_SPI_Configure(), only if the serial
*    NOR flash device does not identify itself with one of the values specified
*    in the table above.
*
*    SectorSize must be set to the size of the size of the storage area
*    erased via the Block Erase (0xD8) command. NumSectors is the device
*    capacity in bytes divided by SectorSize.
*
*    The application is permitted to call this function only at the file system
*    initialization in FS_X_AddDevices().
*/
void FS_NOR_SPI_Configure(U8 Unit, U32 SectorSize, U32 NumSectors) {
  NOR_SPI_INST           * pInst;
  FS_NOR_SPI_DEVICE_PARA * pDevicePara;
  U32                      NumBytesDevice;
  U8                       NumBytesAddr;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pDevicePara = SEGGER_PTR2PTR(FS_NOR_SPI_DEVICE_PARA, pInst->pDevicePara);     //lint !e9005: attempt to cast away const/volatile from a pointer or reference [MISRA 2012 Rule 11.8, required]. Rationale: The device parameters can be either automatically detected (ROM) or manually configured (RAM).
    if (pDevicePara == NULL) {
      pDevicePara = SEGGER_PTR2PTR(FS_NOR_SPI_DEVICE_PARA, FS_ALLOC_ZEROED((I32)sizeof(FS_NOR_SPI_DEVICE_PARA), "FS_NOR_SPI_DEVICE_PARA"));
    } else {
      if (pInst->IsUserConfigured == 0u) {
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: Could not configure SPI flash. Device has been auto-detected."));
        return;
      }
    }
    NumBytesDevice = NumSectors * SectorSize;
    NumBytesAddr = 3;
    if (NumBytesDevice > (16uL * 1024uL * 1024uL)) {  // Devices with a capacity greater than 16MB require a 4 byte address.
      NumBytesAddr = 4;
    }
    if (pDevicePara != NULL) {
      pDevicePara->NumSectors       = NumSectors;
      pDevicePara->ldBytesPerSector = (U8)_ld(SectorSize);
      pDevicePara->ldBytesPerPage   = (U8)_ld(256);
      pDevicePara->NumBytesAddr     = NumBytesAddr;
      pInst->pDevicePara = SEGGER_CONSTPTR2PTR(const FS_NOR_SPI_DEVICE_PARA, pDevicePara);
      pInst->IsUserConfigured = 1;                  // Remember that we configured the device manually.
    }
  }
}

/*********************************************************************
*
*       FS_NOR_SPI_SetPageSize
*
*  Function description
*    Specifies the number of bytes in page.
*
*  Parameters
*    Unit           Index of the physical layer (0-based).
*    BytesPerPage   Number of bytes in a page.
*
*  Additional information
*    This function is optional. A page is the largest amount of bytes
*    that can be written at once to a serial NOR flash device. By default
*    the physical layer uses a page size of 256 bytes a value that is supported
*    by the majority of serial NOR flash devices.
*
*    The size of a page cannot be automatically detected by the physical layer
*    at runtime. Therefore, if the used serial NOR flash device has a page size
*    different than 256 bytes, FS_NOR_SPI_SetPageSize() has to be used to configure
*    the page size to the actual value. The write operation fails if the page
*    size used by the physical layer is larger than the page size used by the
*    serial NOR flash device. The write operation works if the application
*    specifies a smaller page size than the actual page size of the serial NOR
*    flash device but the write performance will be worst.
*
*    BytesPerPage has to be a power of 2 value.
*/
void FS_NOR_SPI_SetPageSize(U8 Unit, U16 BytesPerPage) {
  NOR_SPI_INST           * pInst;
  FS_NOR_SPI_DEVICE_PARA * pDevicePara;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pDevicePara = SEGGER_PTR2PTR(FS_NOR_SPI_DEVICE_PARA, pInst->pDevicePara);     //lint !e9005: attempt to cast away const/volatile from a pointer or reference [MISRA 2012 Rule 11.8, required]. Rationale: The device parameters can be either automatically detected (ROM) or manually configured (RAM).
    if (pDevicePara == NULL) {
      pDevicePara = SEGGER_PTR2PTR(FS_NOR_SPI_DEVICE_PARA, FS_ALLOC_ZEROED((I32)sizeof(FS_NOR_SPI_DEVICE_PARA), "FS_NOR_SPI_DEVICE_PARA"));
    } else {
      if (pInst->IsUserConfigured == 0u) {
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: Could not set page size. Device has been auto-detected."));
        return;
      }
    }
    if (pDevicePara != NULL) {
      pDevicePara->ldBytesPerPage = (U8)_ld(BytesPerPage);
      pInst->pDevicePara          = SEGGER_CONSTPTR2PTR(const FS_NOR_SPI_DEVICE_PARA, pDevicePara);
      pInst->IsUserConfigured     = 1;        // Remember that we configured the device manually.
    }
  }
}

/*********************************************************************
*
*       FS_NOR_SPI_SetHWType
*
*  Function description
*    Configures the HW access routines.
*
*  Parameters
*    Unit           Index of the physical layer (0-based).
*    pHWType        Hardware access routines. Cannot be NULL.
*
*  Additional information
*    This function is mandatory and it has to be called once for each
*    instance of the physical layer.
*/
void FS_NOR_SPI_SetHWType(U8 Unit, const FS_NOR_HW_TYPE_SPI * pHWType) {
  NOR_SPI_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->pHWType = pHWType;
  }
}

/*********************************************************************
*
*       FS_NOR_SPI_ReadDeviceId
*
*  Function description
*    Reads device identification information from NOR flash device.
*
*  Parameters
*    Unit       Index of the physical layer (0-based).
*    pId        [OUT] Information read from NOR flash.
*    NumBytes   Number of bytes to read from NOR flash.
*
*  Additional information
*    The data returned by this function is the response to
*    the READ ID (0x9F) command.
*/
void FS_NOR_SPI_ReadDeviceId(U8 Unit, U8 * pId, unsigned NumBytes) {
  NOR_SPI_INST * pInst;
  int            r;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    if (NumBytes != 0u) {
      r = _InitHWIfRequired(pInst);
      if (r == 0) {
        _ReadDeviceId(pInst, pId, NumBytes);
      }
    }
  }
}

#if FS_NOR_MAX_NUM_DEVICES

/*********************************************************************
*
*       FS_NOR_SPI_AddDevice
*
*  Function description
*    Specifies parameters for a NOR flash device that has to be supported.
*
*  Parameters
*    pDevicePara  Device parameters.
*
*  Additional information
*    This function is optional. It allows an application to define the parameters
*    of a NOR flash device that is not yet supported by the physical layer.
*    The maximum number of NOR flash devices that can be added to the list
*    can be specified via FS_NOR_MAX_NUM_DEVICES define. By default this
*    feature is disabled, that is FS_NOR_MAX_NUM_DEVICES is set to 0.
*    The data pointed to by pDevicePara should remain valid until FS_DeInit()
*    is called, because the function saves internally only the pointer value.
*
*    This function is available only when the file system is compiled with
*    FS_NOR_MAX_NUM_DEVICES set to a value larger than 0.
*/
void FS_NOR_SPI_AddDevice(const FS_NOR_SPI_DEVICE_PARA * pDevicePara) {
  ASSERT_DEVICE_LIST_NOT_FULL();
  _apDeviceListUser[_NumDevicesUser++] = pDevicePara;
}

#endif // FS_NOR_MAX_NUM_DEVICES

/*************************** End of file ****************************/
