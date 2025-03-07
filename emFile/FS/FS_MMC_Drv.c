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
File        : FS_MMC_Drv.c
Purpose     : File system generic MMC/SD driver using SPI mode
Literature  :
  [1]  SD Specifications, Part 1, PHYSICAL LAYER, Simplified Specification Version 2.00, September 25, 2006
  [2]  The MultiMediaCard System Specification Version 3.2
  [3]  SD Specifications, Part 1, PHYSICAL LAYER Specification, Version 2.00, May 9, 2006
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
#define BYTES_PER_SECTOR                512u      // Number of bytes in a sector
#if FS_MMC_SUPPORT_HIGH_SPEED
  #define SD_SPEC_VER_200               2u
#endif

/*********************************************************************
*
*       Hardware specific defines
*/
#define LOW_VOLT_MIN                    1650u     // Low voltage mode minimum value (mV)
#define STARTUP_FREQ                    400u      // Max. startup frequency (KHz)

/*********************************************************************
*
*       Timeouts
*
*  Additional information
*    NUM_CYCLES_INIT defines the number of empty byte cycles that are transferred to
*    the card before any command is issued. Referring to the specifications of SDA, this period
*    must be at least 74 cycles, but it is recommended to send more that just the minimum value.
*    The value here is a byte count so is multiplied by 8 to get cycle count.
*
*    NAC_CSD_MAX is the maximum read/write timeout. These values are documented in
*    [1]: 4.6.2 Read, Write and Erase Timeout Conditions.
*/
#define NUM_CYCLES_INIT                 10        // Cycles sent to the card before initialization starts
#define NUM_RETRIES_INIT                5         // Number of times to repeat the initialization of the SD car or MMC device.
#define NAC_CSD_MAX                     50000     // Max read cycles for CSD read
#define NUM_RETRIES_POWERUP             2000

/*********************************************************************
*
*       Types of storage cards
*/
#define CARD_TYPE_MMC                   0u
#define CARD_TYPE_SD                    1u
#define CARD_TYPE_SDHC                  2u

/*********************************************************************
*
*       MMC/SD response tokens
*/
#define TOKEN_MULTI_BLOCK_WRITE_START   0xFCu
#define TOKEN_MULTI_BLOCK_WRITE_STOP    0xFDu
#define TOKEN_BLOCK_READ_START          0xFEu
#define TOKEN_BLOCK_WRITE_START         0xFEu

/*********************************************************************
*
*       MMC/SD card commands
*/
#define CMD_SEND_OP_COND                1u
#if FS_MMC_SUPPORT_HIGH_SPEED
  #define CMD_SWITCH_FUNC               6u
#endif
#define CMD_SEND_IF_COND                8u
#define CMD_SEND_CSD                    9u
#define CMD_SEND_CID                    10u
#define CMD_STOP_TRANSMISSION           12u
#define CMD_READ_SINGLE_BLOCK           17u
#define CMD_READ_MULTIPLE_BLOCKS        18u
#define CMD_WRITE_SINGLE_BLOCK          24u
#define CMD_WRITE_MULTIPLE_BLOCK        25u
#define CMD_ACMD_CMD                    55u
#define CMD_READ_OCR                    58u
#define ACMD_SEND_OP_COND               41u
#if FS_MMC_SUPPORT_HIGH_SPEED
  #define ACMD_SEND_SCR                 51u
#endif
#define CMD_LEN                         6

/*********************************************************************
*
*       CSD register access macros
*/
#define CSD_STRUCTURE(pCSD)             _GetFromCSD(pCSD, 126, 127)
#define CSD_WRITE_PROTECT(pCSD)         _GetFromCSD(pCSD,  12,  13)
#if FS_MMC_SUPPORT_HIGH_SPEED
  #define CSD_CCC_CLASSES(pCSD)         _GetFromCSD(pCSD,  84,  95)
#endif
#define CSD_R2W_FACTOR(pCSD)            _GetFromCSD(pCSD,  26,  28)
#define CSD_C_SIZE_MULT(pCSD)           _GetFromCSD(pCSD,  47,  49)
#define CSD_C_SIZE(pCSD)                _GetFromCSD(pCSD,  62,  73)
#define CSD_READ_BL_LEN(pCSD)           _GetFromCSD(pCSD,  80,  83)
#define CSD_TRAN_SPEED(pCSD)            ((pCSD)->aData[3])   // Same as, but more efficient than: _GetFromCSD(pCSD,  96, 103)
#define CSD_NSAC(pCSD)                  ((pCSD)->aData[2])   // Same as, but more efficient than: _GetFromCSD(pCSD, 104, 111)
#define CSD_TAAC(pCSD)                  ((pCSD)->aData[1])   // Same as, but more efficient than: _GetFromCSD(pCSD, 112, 119)
#define CSD_C_SIZE_V2(pCSD)             _GetFromCSD(pCSD,  48, 69)

/*********************************************************************
*
*       ASSERT_UNIT_NO_IS_IN_RANGE
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)                                    \
    if ((Unit) >= (U8)FS_MMC_NUM_UNITS) {                                     \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "MMC_SPI: Invalid unit number.")); \
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
  #define ASSERT_HW_TYPE_IS_SET(pInst)                                             \
    if ((pInst)->pHWType == NULL) {                                                \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "MMC_SPI: HW layer type is not set.")); \
      FS_X_PANIC(FS_ERRCODE_HW_LAYER_NOT_SET);                                     \
    }
#else
  #define ASSERT_HW_TYPE_IS_SET(pInst)
#endif

/*********************************************************************
*
*       Statistics
*/
#if FS_MMC_ENABLE_STATS
  #define IF_STATS(Exp) Exp
#else
  #define IF_STATS(Exp)
#endif

/*********************************************************************
*
*       Locking
*/
#if FS_MMC_SUPPORT_LOCKING
  #define LOCK_SPI(pInst)         _Lock(pInst)
  #define UNLOCK_SPI(pInst)       _Unlock(pInst)
#else
  #define LOCK_SPI(pInst)
  #define UNLOCK_SPI(pInst)
#endif

/*********************************************************************
*
*       Local data types
*
**********************************************************************
*/
typedef struct {
  U8 aData[16]; // Size is 128 bit
} CSD;

typedef struct {
  const FS_MMC_HW_TYPE_SPI * pHWType;                       // Routines for the hardware access.
  U32                        NumSectors;                    // Total number of logical sectors in the storage device.
  U32                        Nac;                           // Maximum configured byte transfer cycles for read access.
  U32                        Nwrite;                        // Write timeout in transfer cycles.
  U32                        Freq_kHz;                      // Clock frequency supplied to the storage device.
#if FS_MMC_ENABLE_STATS
  FS_MMC_STAT_COUNTERS       StatCounters;                  // Statistical counters.
#endif
  U8                         IsInited;                      // Set to 1 if the driver instance is initialized.
  U8                         Unit;                          // Index of the driver instance (0-based)
  U8                         CardType;                      // Type of the storage device (SD card or MMC device)
  U8                         IsWriteProtected;              // Set to 1 if the data on the storage device cannot be changed.
  U8                         AccessMode;                    // Current access mode.
} MMC_INST;

/*********************************************************************
*
*       Prototypes
*
**********************************************************************
*/
static U16 _CalcDataCRC16Dummy(const U8 * p, unsigned NumBytes);

/*********************************************************************
*
*       Static const data
*
**********************************************************************
*/

//
// Time value conversion factors for CSD TAAC and TRAN_SPEED values from
// [1]: 5.3.2. Values here are 10x the spec values.
//
static const U8 _aFactor[16] = {
  0,    // 0: reserved - not supported
  10,   // x1
  12,   // x2
  13,   // x3
  15,   // x4
  20,   // x5
  25,   // x6
  30,   // x7
  35,   // x8
  40,   // x9
  45,   // x10
  50,   // x11
  55,   // x12
  60,   // x13
  65,   // x14
  80    // x15
};

//
// Time unit conversion factors for CSD TAAC values from [1]: 5.3.2.
// Values here are divisors that are 1/100 of the spec values (ie. the
// result of dividing by them is 100 times larger than it should be).
//
static const U32 _aUnit[8] = {
  10000000uL,  // 0 -   1ns
  1000000uL,   // 1 -  10ns
  100000uL,    // 2 - 100ns
  10000uL,     // 3 -   1us
  1000uL,      // 4 -  10us
  100uL,       // 5 - 100us
  10uL,        // 6 -   1ms
  1uL          // 7 -  10ms
};

//
// Transfer rate conversion factors for CSD TRAN_SPEED values from [1]: 5.3.2.
// Values here are multipliers that are 1/10 the value needed to convert to
// kbits/s.
//
static const U32 _aRateUnit[4] = {
  10uL,        // 0 - 100 kbits/s
  100uL,       // 1 -   1 Mbits/s
  1000uL,      // 2 -  10 Mbits/s
  10000uL      // 3 - 100 Mbits/s
};

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static MMC_INST * _apInst[FS_MMC_NUM_UNITS];   // per Unit card info
static U8         _NumUnits = 0;
static U16     (* _pfCalcCRC)(const U8 * p, unsigned NumBytes) = _CalcDataCRC16Dummy;  // Function pointer for CRC check code for all cards

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _GetBits
*
*  Function description
*    Returns a value from the bit field.
*/
static U32 _GetBits(const U8 * pData, unsigned FirstBit, unsigned LastBit, unsigned NumBytesAvailable) {
  unsigned Off;
  unsigned OffLast;
  unsigned NumBytes;
  U32 Data;

  Off      = FirstBit / 8u;
  OffLast  = LastBit / 8u;
  NumBytes = (OffLast - Off) + 1u;
  Off      = (NumBytesAvailable - 1u) - OffLast;        // Bytes are reversed in CSD
  Data = 0;
  //
  // Read data into 32 bits
  //
  do {
    Data <<= 8;
    Data |= pData[Off++];
  } while (--NumBytes != 0u);
  //
  // Shift and mask result
  //
  Data >>= (FirstBit & 7u);
  Data &= (2uL << (LastBit - FirstBit)) - 1u;           // Mask out bits that are outside of given bit range
  return Data;
}

/*********************************************************************
*
*       _GetFromCSD
*
*  Function description
*    Returns a value from the CSD field. These values are stored in
*    a 128 bit array; the bit-indices documented in [1]: 5.3 CSD register, page 69
*    can be used as parameters when calling the function
*/
static U32 _GetFromCSD(const CSD * pCSD, unsigned FirstBit, unsigned LastBit) {
  U32 Data;

  Data = _GetBits(pCSD->aData, FirstBit, LastBit, sizeof(pCSD->aData));
  return Data;
}

/*********************************************************************
*
*       _CalcCRC7
*
*  Function description
*    Returns the 7 bit CRC generated with the 0x1021 polynomial.
*/
static unsigned _CalcCRC7(const U8 * pData, unsigned NumBytes) {
  unsigned crc;
  unsigned iBit;
  unsigned iByte;
  unsigned Data;

  crc = 0;
  for (iByte = 0; iByte < NumBytes; iByte++) {
    Data = *pData++;
    for (iBit = 0; iBit < 8u; iBit++) {
      crc <<= 1;
      if (((Data & 0x80u) ^ (crc & 0x80u)) != 0u) {
        crc ^= 0x09u;
      }
      Data <<= 1;
    }
  }
  crc = (crc << 1) | 1u;
  return crc & 0xFFu;
}

/*********************************************************************
*
*       _CalcDataCRC16ViaTable
*
*  Function description
*    Returns the 16 bit CRC generated with the 1021 polynomial using a table.
*    Using the table is about 10 times as fast as computing the CRC on a bit by bit basis,
*    which is the reason why it is used here.
*    The CRC algorithm is described in some detail in [1]: 4.5, page 40.
*
*  Notes
*    (1) Code verification
*        The table has been generated automatically, so typos can be ruled out.
*        Code verification is difficult if tables are used; but in this case,
*        the table can be verified also by running the "CRC Sample":
*        512 bytes with 0xFF data --> CRC16 = 0x7FA1
*/
static U16 _CalcDataCRC16ViaTable(const U8 * pData, unsigned NumBytes) {
  U16 Crc;

  Crc = FS_CRC16_Calc(pData, NumBytes, 0);
  return Crc;
}

/*********************************************************************
*
*       _CalcDataCRC16Dummy
*
*  Function description
*    Returns a dummy value (0xFFFF) which indicates that CRC has not been computed.
*/
static U16 _CalcDataCRC16Dummy(const U8 * p, unsigned NumBytes) {
  FS_USE_PARA(p);
  FS_USE_PARA(NumBytes);
  return 0xFFFFu;
}

/*********************************************************************
*
*       _CalcDataCRC16
*
*  Function description
*    Is used to compute the 16-bit CRC for data.
*    It calls the actual computation routine via function pointer.
*    The function pointer is either the "Dummy" routine returning the
*    0xFFFF or a routine computing the correct 16-bit CRC
*/
static U16 _CalcDataCRC16(const void * p, unsigned NumBytes) {
  return _pfCalcCRC(SEGGER_CONSTPTR2PTR(const U8, p), NumBytes);
}

/*********************************************************************
*
*       _EnableCS
*
*  Function description
*    Asserts the chip select signal.
*/
static void _EnableCS(const MMC_INST * pInst) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWType->pfEnableCS(Unit);
}

/*********************************************************************
*
*       _DisableCS
*
*  Function description
*    De-asserts the chip select signal.
*/
static void _DisableCS(const MMC_INST * pInst) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWType->pfDisableCS(Unit);
}

/*********************************************************************
*
*       _Read
*
*  Function description
*    Receives data from SD/MMC card via SPI.
*/
static int _Read(const MMC_INST * pInst, U8 * pData, int NumBytes) {
  int r;
  U8  Unit;

  Unit = pInst->Unit;
  if (pInst->pHWType->pfRead != NULL) {
    pInst->pHWType->pfRead(Unit, pData, NumBytes);
    r = 0;
  } else {
    r = pInst->pHWType->pfReadEx(Unit, pData, NumBytes);
    if (r != 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "MMC_SPI: _Read: HW reports read error."));
    }
  }
  return r;
}

/*********************************************************************
*
*       _Write
*
*  Function description
*    Send data to SD/MMC card via SPI.
*/
static int _Write(const MMC_INST * pInst, const U8 * pData, int NumBytes) {
  int r;
  U8  Unit;

  Unit = pInst->Unit;
  if (pInst->pHWType->pfWrite != NULL) {
    pInst->pHWType->pfWrite(Unit, pData, NumBytes);
    r = 0;
  } else {
    r = pInst->pHWType->pfWriteEx(Unit, pData, NumBytes);
    if (r != 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "MMC_SPI: _Write: HW reports write error."));
    }
  }
  return r;
}

/*********************************************************************
*
*       _IsPresent
*
*  Function description
*    Checks if the SD/MMC card is inserted.
*/
static int _IsPresent(const MMC_INST * pInst) {
  U8  Unit;
  int Status;

  Unit = pInst->Unit;
  Status = pInst->pHWType->pfIsPresent(Unit);
  return Status;
}

/*********************************************************************
*
*       _IsWriteProtected
*
*  Function description
*    Checks if data can be written to SD/MMC card.
*/
static int _IsWriteProtected(const MMC_INST * pInst) {
  U8  Unit;
  int Status;

  Unit = pInst->Unit;
  Status = pInst->pHWType->pfIsWriteProtected(Unit);
  return Status;
}

/*********************************************************************
*
*       _SetMaxSpeed
*
*  Function description
*    Configures the clock speed of SPI.
*/
static U16 _SetMaxSpeed(const MMC_INST * pInst, U16 MaxFreq) {
  U16 Speed;
  U8  Unit;

  Unit = pInst->Unit;
  Speed = pInst->pHWType->pfSetMaxSpeed(Unit, MaxFreq);
  return Speed;
}

/*********************************************************************
*
*       _SetVoltage
*
*  Function description
*    Configures the operating voltage of SPI.
*/
static int _SetVoltage(const MMC_INST * pInst, U16 Vmin, U16 Vmax) {
  U8  Unit;
  int r;

  Unit = pInst->Unit;
  r = pInst->pHWType->pfSetVoltage(Unit, Vmin, Vmax);
  return r;
}

#if FS_MMC_SUPPORT_LOCKING

/*********************************************************************
*
*       _Lock
*
*  Function description
*    Requests exclusive access to the SPI bus.
*/
static void _Lock(const MMC_INST * pInst) {
  U8 Unit;

  Unit = pInst->Unit;
  if (pInst->pHWType->pfLock != NULL) {
    pInst->pHWType->pfLock(Unit);
  }
}

/*********************************************************************
*
*       _Unlock
*
*  Function description
*    Releases the SPI bus.
*/
static void _Unlock(const MMC_INST * pInst) {
  U8 Unit;

  Unit = pInst->Unit;
  if (pInst->pHWType->pfUnlock != NULL) {
    pInst->pHWType->pfUnlock(Unit);
  }
}

#endif // FS_MMC_SUPPORT_LOCKING

/*********************************************************************
*
*       _SendEmptyCycles
*
*  Function description
*    After each transaction, the MMC card
*    needs at least one empty cycle phase. During this 8 clock cycle phase
*    data line must be at high level.
*
*  Parameters
*    pInst    Driver instance.
*    n        Number of cycles to be generated.
*/
static void _SendEmptyCycles(const MMC_INST * pInst, int n) {
  U8  c;

  c = 0xFF;   // The data line must be kept high.
  for (; n > 0; n--) {
    (void)_Write(pInst, &c, 1);
  }
}

/*********************************************************************
*
*       _CheckR1
*
*  Function description
*    Read the R1 response,assert that no error occurred
*    and returns the response R1.
*    Bit definition of R1:
*      Bit 0:    In idle state
*      Bit 1:    Erase Reset
*      Bit 2:    Illegal command
*      Bit 3:    Communication CRC error
*      Bit 4:    Erase sequence error
*      Bit 5:    Address error
*      Bit 6:    Parameter error
*      Bit 7:    Always 0.
*
*  Parameters
*    pInst    Driver instance.
*
*  Notes
*    (1) The response is as follows:
*        NCR bits of value 1 (up to 8 bits)
*        single byte, response format R1 (MMC spec [2]: 7.6.2)
*        The bit 7 is always 0, the other bits indicate errors if set.
*        In other words: The response consists of 8 - 16 bits.
*        The last 8 bits are relevant; of these bit 7 is always 0.
*/
static U8 _CheckR1(const MMC_INST * pInst) {
  U8       Response;
  unsigned NumLoops;
  int      r;

  //
  // Read NCR bits and response token
  //
  Response = 0;
  r = _Read(pInst, &Response, 1);             // First byte is always NCR, never a valid response.
  if ((r == 0) && (Response != 0xFFu)) {
    NumLoops = 10;
  } else {
    NumLoops = 7;
  }
  for (;;) {
    r = _Read(pInst, &Response, 1);           // Note 1
    if ((r == 0) && (Response != 0xFFu)) {
      break;
    }
    if (--NumLoops == 0u) {
      return 0xFF;                            // Error, more than 8 bytes NCR (0xff)
    }
  }
  return Response;                            // OK, R1 response received.
}

/*********************************************************************
*
*       _WaitUntilReady
*
*  Function description
*    Wait for the busy flag to be deactivated (DOUT == 1)
*
*  Parameters
*    pInst    Driver instance.
*
*  Return value
*    ==0    O.K.
*    !=0    An error occurred
*/
static int _WaitUntilReady(const MMC_INST * pInst) {
  U32 TimeOut;
  U8  aData[2];
  int r;

  //
  // Wait for the card to report ready
  //
  TimeOut = pInst->Nac;
  for (;;) {
    r = _Read(pInst, aData, 1);
    if ((r == 0) && (aData[0] == 0xFFu)) {
      return 0;                               // OK, card is ready
    }
    if (TimeOut-- == 0u) {
      return 1;                               // Error, time out
    }
  }
}

/*********************************************************************
*
*       _ExecCmdR1
*
*  Function description
*    MMC/SD driver internal function. Execute a command sequence with R1
*    response and return the card's response.
*
*  Parameters
*    pInst  Driver instance.
*    Cmd    Command index.
*    Arg    Command argument.
*
*  Return value
*    !=0xFF   Card response token type R1.
*    ==0xFF   An error occurred.
*/
static U8 _ExecCmdR1(const MMC_INST * pInst, unsigned char Cmd, U32 Arg) {
  U8  r;
  U8  aCmdBuffer[CMD_LEN];
  int Result;

  //
  // Build setup command token (48 bit)
  //
  aCmdBuffer[0] = (U8)(0x40u | (Cmd & 0x3Fu));
  aCmdBuffer[1] = (U8)((Arg >> 24)  & 0xFFu);
  aCmdBuffer[2] = (U8)((Arg >> 16)  & 0xFFu);
  aCmdBuffer[3] = (U8)((Arg >>  8)  & 0xFFu);
  aCmdBuffer[4] = (U8)( Arg         & 0xFFu);
  aCmdBuffer[5] = (U8)_CalcCRC7(&aCmdBuffer[0], 5);
  //
  // Make sure the card is ready.
  //
  Result = _WaitUntilReady(pInst);
  if (Result != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "MMC_SPI: _ExecCmdR1: Timeout occurred when receiving the response."));
    return 0xFF;                                  // Error, time out waiting for ready.
  }
  Result = _Write(pInst, aCmdBuffer, CMD_LEN);
  if (Result != 0) {
    return 0xFF;                                  // Error, HW write error.
  }
  r = _CheckR1(pInst);                            // Receive response.
  return r;                                       // Return the received response token.
}

/*********************************************************************
*
*       _ReadCSD
*
*  Function description
*    MMC/SD driver internal function.
*    Read the card's CSD (card specific data) register.
*
*  Parameters
*    pInst    Driver instance.
*    pCSD     [OUT] Contents of CSD register.
*
*  Return value
*    ==0    CSD has been read and all parameters are valid.
*    !=0    An error has occurred.
*/
static int _ReadCSD(const MMC_INST * pInst, CSD * pCSD) {
  U8  crc;
  U8  c;
  U32 TimeOut;
  int r;
  U8  Response;
  int Result;

  r = 1;
  _SendEmptyCycles(pInst, 1);
  //
  // Execute CMD9 (SEND_CSD)
  //
  _EnableCS(pInst);
  Response = _ExecCmdR1(pInst, CMD_SEND_CSD, 0);
  if (Response != 0u) {
    goto Done;
  }
  //
  // Wait for CSD transfer to begin.
  //
  TimeOut = pInst->Nac;
  for (;;) {
    r = _Read(pInst, &c, 1);
    if (r == 0) {
      if (c == TOKEN_BLOCK_READ_START) {
        break;
      }
      //
      // a = c & 0x3F; filter CSD signature bits.
      //
      if (c == 0xFCu) {
        break;
      }
    }
    if (TimeOut-- == 0u) {
      goto Done;      // Error, timeout reached.
    }
  }
  //
  // Read the CSD register.
  //
  r = 0;       // Set to indicate success.
  Result = _Read(pInst, pCSD->aData, (int)sizeof(CSD));
  r = (Result != 0) ? Result : r;
  Result = _Read(pInst, &crc, 1);  // Read CRC16 high part.
  r = (Result != 0) ? Result : r;
  Result = _Read(pInst, &crc, 1);  // Read CRC16 low part.
  r = (Result != 0) ? Result : r;
  FS_USE_PARA(crc);
Done:
  _DisableCS(pInst);
  _SendEmptyCycles(pInst, 1);      // Clock card after command failure.
  return r;
}

/*********************************************************************
*
*       _WaitToGetReady
*
*  Function description
*    Waits until the card returns from busy state.
*    This function only waits _nwrite[Unit] cycles,
*    if the card is still in busy after this cycles, a timeout occurs.
*
*  Return value
*    ==0    Card is ready to accept data/commands.
*    !=0    A timeout occurred.
*/
static int _WaitToGetReady(const MMC_INST * pInst) {
  U8       BusyState;
  unsigned NumLoops;
  int      r;

  NumLoops = pInst->Nwrite;
  for (;;) {
    r = _Read(pInst, &BusyState, 1);
    if (r == 0) {
      if (BusyState != 0u) {
        break;
      }
    }
    if (--NumLoops == 0u) {
      return 1;             // Error, timeout occurred.
    }
  }
  return 0;                 // OK, card is ready.
}

/*********************************************************************
*
*       _WaitBlockRdTok
*
*  Function description
*    Wait for valid block read confirmation token.
*
*  Parameters
*    pInst    Driver instance.
*
*  Return value
*    0xfe   Success.
*    0xff   Error, timeout occurred.
*/
static U8 _WaitBlockRdTok(const MMC_INST * pInst) {
  U8  c;
  U32 TimeOut;
  int r;

  TimeOut = pInst->Nac;
  for (;;) {
    r = _Read(pInst, &c, 1);
    if (r != 0) {
      return 0xFF;      // Error, could not read data.
    }
    if (c == TOKEN_BLOCK_READ_START) {
      return c;
    }
    if (TimeOut-- == 0u) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "MMC_SPI: _WaitBlockRdTok: timed out."));
      return 0xFF;
    }
  }
}

/*********************************************************************
*
*       _ExecCmdInit
*
*  Function description
*    Sends initialization command (CMD0)
*
*  Parameters
*    pInst    Driver instance.
*
*  Notes
*    The sequence is very similar, but not identical to the CmdR1 sequence.
*    Main difference is that we do not "Wait until ready" because the card's
*    output may not be enabled for SD cards.
*/
static U8 _ExecCmdInit(const MMC_INST * pInst) {
  U8       r;
  int      Result;
  const U8 CmdBuffer[CMD_LEN] = { 0x40, 0, 0, 0, 0, 0x95 };

  _EnableCS(pInst);
  r = 0xFF;                               // Set to indicate an error.
  Result = _Write(pInst, CmdBuffer, CMD_LEN);
  if (Result == 0) {
    r = _CheckR1(pInst);                  // Receive response.
  }
  _DisableCS(pInst);
  _SendEmptyCycles(pInst, 1);
  return r;                               // Return the received response token.
}

/*********************************************************************
*
*       _CheckCardOCR
*
*  Function description
*    MMC/SD driver driver internal function. Read the card's OCR register and checks,
*    if the provided voltage is supported.
*
*  Parameters
*    pInst    Driver instance.
*    pCCS     Pointer to variable to receive CCS bit (if CCS is valid)
*
*  Return value
*    ==0    Voltage is supported.
*    !=0    An error has occurred.
*/
static int _CheckCardOCR(const MMC_INST * pInst, U8 * pCCS) {
  unsigned Value;
  unsigned Vmax;
  unsigned Vmin;
  unsigned i;
  U8       abOCR[4];
  U8       c;
  unsigned VMode;
  U32      ocr;
  unsigned Mask;
  U8       Response;

  _EnableCS(pInst);
  Response = _ExecCmdR1(pInst, CMD_READ_OCR, 0);
  if (Response != 0u) {
    _DisableCS(pInst);
    _SendEmptyCycles(pInst, 1);
    return 1;                         // Error
  }
  //
  // Get the OCR register.
  //
  (void)_Read(pInst, &abOCR[0], 4);
  ocr = FS_LoadU32BE(&abOCR[0]);
  _DisableCS(pInst);
  _SendEmptyCycles(pInst, 1);
  //
  // Return CCS bit value if caller wants it. It's only valid if power status bit is set.
  //
  if ((pCCS != NULL) && (((ocr >> 31) & 1u) != 0u)) {
    *pCCS = (U8)((ocr >> 30) & 1u);
  }
  //
  // Test for low voltage mode support.
  //
  VMode = ocr & 0x80u;
  if (VMode != 0u) {
    Vmin = LOW_VOLT_MIN;
  } else {
    Vmin = 0;
  }
  //
  // Calculate lower voltage limit.
  //
  Value = (ocr >> 8) & 0xFFFFu;
  Mask  = 0x0001;
  VMode = 0;
  for (c = 0; c < 16u; c++) {
    i = Value & Mask;
    if (i != 0u) {
      break;
    }
    Mask <<= 1;
    VMode++;
  }
  //
  // Calculate voltage from OCR field. Bit position means 100mV, offset is 2000mV
  //
  if (Vmin < LOW_VOLT_MIN) {
    Vmin = 2000u + (VMode * 100u);
  }
  //
  // Calculate high voltage limit
  //
  for (; c < 16u; c++) {
    i = Value & Mask;
    if (i == 0u) {
      break;
    }
    Mask <<= 1;
    VMode++;
  }
  Vmax = (2000u + (VMode * 100u));
  c = (U8)_SetVoltage(pInst, (U16)Vmin, (U16)Vmax);
  //
  // Indicate error if card does not support requested voltage range.
  //
  if (c == 0u) {
    return 1;                 // Error
  }
  return 0;                   // OK
}

/*********************************************************************
*
*       _StopTransmission
*
*  Function description
*    Requests the card to stop sending data.
*/
static int _StopTransmission(const MMC_INST * pInst) {
  int NumRetries;
  int r;
  U8  Response;

  r          = 1;   // Set to indicate an error.
  NumRetries = FS_MMC_NUM_RETRIES;
  for (;;) {
    Response = _ExecCmdR1(pInst, CMD_STOP_TRANSMISSION, 0);
    if (Response == 0u) {
      r = 0;        // OK, command successful.
      break;
    }
    if (NumRetries-- == 0) {
      break;        // Error, error response after several retries.
    }
  }
  return r;
}

#if FS_MMC_SUPPORT_HIGH_SPEED

/*********************************************************************
*
*       _ExecSwitchFunc
*
*  Function description
*    Sets the card in high-speed mode.
*/
static int _ExecSwitchFunc(const MMC_INST * pInst, int Mode, int Group, U8 Value, U8 * pResp) {
  U32      Arg;
  unsigned Response;
  U8       aCRC[2];
  int      r;
  int      Result;

  Arg = ((U32)Mode << 31) | 0x00FFFFFFuL;
  Arg &= ~(0x0FuL   << ((U32)Group * 4u));
  Arg |= (U32)Value << ((U32)Group * 4u);
  _EnableCS(pInst);
  Response = _ExecCmdR1(pInst, CMD_SWITCH_FUNC, Arg);
  if (Response == 0xFFu) {
    r = 1;                            // Error
    goto Done;
  }
  Response = _WaitBlockRdTok(pInst);  // Wait for the data block to begin
  if (Response == 0xFFu) {
    r = 1;                            // Error
    goto Done;
  }
  r = 0;
  Result = _Read(pInst, pResp, 64);   // Read SCR data
  r = (Result != 0) ? Result : r;
  Result = _Read(pInst, aCRC, 2);     // Read CRC16
  r = (Result != 0) ? Result : r;
Done:
  _DisableCS(pInst);
  return r;
}

#endif // FS_MMC_SUPPORT_HIGH_SPEED

/*********************************************************************
*
*      _ApplyCSD
*
*  Function description
*    MMC/SD driver internal function.
*    Read the card's CSD (card specific data) registers and check
*    its contents.
*
*  Parameters
*    pInst    Driver instance.
*    pCSD     Contents of the CSD register.
*
*  Return value
*    ==0    CSD has been read and all parameters are valid.
*    !=0    An error has occurred.
*
*  Notes
*    (1) SectorSize
*        Newer SD card (4 GByte-card) return a block size larger than 512.
*        Sector Size used however is always 512 bytes.
*/
static int _ApplyCSD(MMC_INST * pInst, CSD * pCSD) {            //lint -efunc(818, _ApplyCSD) pCSD cannot be declared as pointing to const because this buffer is used for reading data in high-speed mode.
  int      r;
  U32      Value;
  U32      TimeUnit;
  U32      Factor;
  U32      Freq;          // Card transfer rate in kbit/s
  U32      TimeValue;
  U32      Nac;           // Max configured byte transfer cycles for read access
  U32      NacRead;       // Max byte transfer cycles to allow for read access
  U32      NacWrite;      // Max byte transfer cycles to allow for write access
  U32      CardSize;
  U8       ccs;
  unsigned CSDVersion;
  unsigned AccessMode;

  ccs = 0;
  r = _CheckCardOCR(pInst, &ccs);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "MMC_SPI: _ApplyCSD: OCR invalid."));
    return -1;
  }
  //
  // CSD version is only checked for SD card. MMC card have almost the same
  // CSD structure as SD V1 cards.
  if (pInst->CardType == CARD_TYPE_SD) {
    CSDVersion = CSD_STRUCTURE(pCSD);
  } else {
    CSDVersion = 0;
  }
  AccessMode = FS_MMC_ACCESS_MODE_DS;
#if FS_MMC_SUPPORT_HIGH_SPEED
  {
    U32 CCCSupported;
    int AllowHighSpeed;

    AllowHighSpeed = 0;
    //
    // If the switch function is supported, we
    // ask the card whether it supports high speed mode.
    // This is only true for SD-Cards.
    // MMCs do not support such a command class
    //
    CCCSupported = CSD_CCC_CLASSES(pCSD);
    if (((CCCSupported & (1uL << 10)) != 0u) && (pInst->CardType == CARD_TYPE_SD)) {
      unsigned Response;
      U8       aSCR[8];
      U8       aCRC[2];
      U8       ScrVersion;
      int      Result;
      U8       aSwitch[64];
      U32      Data;

      //
      // Retrieve the SCR (SD card register)
      //
      _EnableCS(pInst);
      Response = _ExecCmdR1(pInst, CMD_ACMD_CMD, 0);    // Prepare for advanced command
      if (Response == 0xFFu) {
        _DisableCS(pInst);
        goto Continue;                                  // Error
      }
      _SendEmptyCycles(pInst, 1);                       // Clock card before next command.
      Response = _ExecCmdR1(pInst, ACMD_SEND_SCR, 0);   // Send ACMD51 (SD_SEND_SCR)
      if (Response == 0xFFu) {
        _DisableCS(pInst);
        goto Continue;                                  // Error
      }
      Response = _WaitBlockRdTok(pInst);                // Wait for data block to begin
      if (Response == 0xFFu) {
        _DisableCS(pInst);
        goto Continue;                                  // Error
      }
      r = 0;
      Result = _Read(pInst, aSCR, 8);                   // Read SCR data
      r = (Result != 0) ? Result : r;
      Result = _Read(pInst, aCRC, 2);                   // Read CRC16
      r = (Result != 0) ? Result : r;
      _DisableCS(pInst);
      if (r != 0) {
        goto Continue;                                  // Error, could not read SCR register.
      }
      ScrVersion = (U8)_GetBits(aSCR, 56, 59, sizeof(aSCR));
      if (ScrVersion >= SD_SPEC_VER_200) {
        if (_ExecSwitchFunc(pInst, 0, 0, 1, aSwitch) == 0) {
          Data = _GetBits(aSwitch, 400, 415, 64);
          if ((Data & 1u) != 0u) {
            if (_ExecSwitchFunc(pInst, 1, 0, 1, aSwitch) == 0) {
              Data = _GetBits(aSwitch, 376, 379, 64);
              if ((Data & 0xFu) == 1u) {
                AllowHighSpeed = 1;
              }
            }
          }
        }
      }
    }
Continue:
    if (AllowHighSpeed != 0) {
      r = _ReadCSD(pInst, pCSD);
      if (r != 0) {
        return 1;                                       // Error, could not read CSD register.
      }
      AccessMode = FS_MMC_ACCESS_MODE_HS;
    }
  }
#endif
  //
  // Interpret card parameters. Some of this code has to differ depending on
  // the card's CSD version (1 or 2). However, we can use the same code to
  // calculate timeout values for any card. We can use the same logic for
  // either CSD version 1 or 2 because version 2 cards supply hard coded timing
  // parameters that are guaranteed to match or exceed the maximum allowed
  // timeouts. Thus, for version 2 cards the timeouts will always be set to
  // the maximum allowed values. This is correct behavior according to the
  // spec. See [1] 5.3.3 CSD Register (CSD Version 2.0), TAAC description.
  //
  // Calculate maximum communication speed according to card specification.
  // Determine transfer rate unit and then combine with time value to get
  // rate in kbit/s.
  //
  Value   = CSD_TRAN_SPEED(pCSD);
  Factor  = Value & 0x03u;
  Freq    = _aRateUnit[Factor];
  Factor  = (Value & 0x78u) >> 3;   // Filter the frequency bits.
  Freq   *= _aFactor[Factor];
  //
  //  Set the rate that will be used to talk to card to highest supported rate
  //  that is less than max allowed rate. Freq is set to that actual rate.
  //
  Freq = _SetMaxSpeed(pInst, (U16)Freq);
  if (Freq == 0u) {
    return 1;                     // Error, clock frequency not supported.
  }
  //
  // Determine asynchronous (ie. time based) part of data access time by
  // decoding TAAC value. We determine a numerator and denominator that when
  // combined, via division, yield the access time as fractions of a second.
  // The numerator (stored in TimeValue) is based on the "time value" spec
  // quantity. The denominator (stored in TimeUnit) is based on the "time
  // unit" spec quantity. Because the TimeValue is 10 times what it should
  // be and the TimeUnit is 1/100 what it should be, when we do the division
  // the result is 1000 times what it should be. This means it yields an
  // access time in msec.
  //
  Nac       = 0;
  Value     = CSD_TAAC(pCSD);
  TimeUnit  = _aUnit[Value & 0x07u];
  TimeValue = _aFactor[((U32)Value >> 3) & 0x0Fu];
  //
  // This is a workaround for the cards which do not encode the TAAC field properly.
  // In this case fixed timeouts are used: 100 ms for read operations and 250 ms for write operations.
  //
  if (TimeValue != 0u) {
    //
    // According to [1] the description of NSAC says the total typical read
    // access time is "the sum of TAAC and NSAC". [3] clarifies that the values
    // are combined "according to Table 4.47". That table specifies the
    // equation for the maximum read access time as 100 times the typical
    // access time:
    //     Nac(max) = 100(TAAC*FOP + 100*NSAC)
    // Because of the units used in the earlier calculations, we can compute
    // TAAC*FOP by combining the quantities already determined to get a value
    // in cycles.
    //
    Nac   = Freq * TimeValue / TimeUnit;
    Nac  += 100uL * CSD_NSAC(pCSD);         // Add in the "clock dependent" factor of the access time
    Nac  *= 100uL;                          // Worst case value is 100 times typical value.
    Nac >>= 3;                              // We want timeout as a count of byte transfers, not bit transfers
  }
  //
  // According to [1]: "4.6.2.1 Read" (Timeout Conditions), the maximum read
  // timeout needs to be limited to 100 msec. Convert 100 msec to byte
  // transfers using the FOP.
  //
  NacRead   = 100uL * Freq;
  NacRead >>= 3;
  if ((NacRead > Nac) && (Nac > 0u)) {
    NacRead = Nac;
  }
  //
  // The write timeout is calculated from Nac using the R2W_FACTOR.
  // R2W_FACTOR is a power-of-2 value so we can use a simple
  // shift to apply it. Note that even with all the configuration parameters
  // at their maximum values, we can be sure this calculation won't overflow
  // 32 bits.
  //
  Factor   = CSD_R2W_FACTOR(pCSD);
  Nac    <<= Factor;
  //
  // According to [1]: "4.6.2.2 Write" (Timeout Conditions), the maximum
  // write timeout needs to be limited to 250 msec. Convert 250 msec to
  // byte transfers using the FOP.
  //
  NacWrite   = 250u * Freq;
  NacWrite >>= 3;
  if ((NacWrite > Nac) && (Nac > 0u)) {
    NacWrite = Nac;
  }
  //
  // Decode the version-specific parameters.
  //
  if (CSDVersion == 0u) {
    //
    // Calculate number of sectors available on the medium.
    //
    Factor    = (1uL << CSD_READ_BL_LEN(pCSD)) / BYTES_PER_SECTOR;
    Factor   *= 1uL << (CSD_C_SIZE_MULT(pCSD) + 2u);
    CardSize  = CSD_C_SIZE(pCSD) + 1u;
    CardSize *= Factor;
  } else if (CSDVersion == 1u){     // Newer SD V2 cards.
    //
    // Calculate number of sectors available on the medium.
    //
    CardSize   = (CSD_C_SIZE_V2(pCSD) + 1u) << 10;
    //
    // Version 2 cards use CCS to specify SDHC support
    //
    if (ccs != 0u) {
      pInst->CardType = CARD_TYPE_SDHC;
    }
  } else {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "MMC_SPI: _ApplyCSD: Unsupported CSD version."));
    return 1;
  }
  //
  // Store calculated values into medium's instance structure.
  //
  pInst->Nac              = NacRead;
  pInst->Nwrite           = NacWrite;
  pInst->IsWriteProtected = (U8)CSD_WRITE_PROTECT(pCSD) | (U8)_IsWriteProtected(pInst);
  pInst->NumSectors       = CardSize;
  pInst->AccessMode       = (U8)AccessMode;
  pInst->Freq_kHz         = Freq;
  return 0;
}

/*********************************************************************
*
*       _InitMMC_SD
*
*  Function description
*    Initializes SD V1.xx and all MMC card in order to get all necessary
*    information from card.
*
*  Parameters
*    pInst    Driver instance.
*
*  Return value
*    ==0  CSD has been read and all parameters are OK .
*    !=0   An error has occurred.
*
*  Notes
*    (1) MMC Specs says (sect 6.3, power up) that it takes max. 1 ms
*        for the card to be ready (leave idle/init). At 400 kHz, this equals 9 loops.
*        At lower speeds, less repetitions would be OK, but a few more ms.
*        should not hurt (only in case the card is not present)
*/
static int _InitMMC_SD(MMC_INST * pInst) {
  CSD csd;
  int i;
  U8  Response;
  int r;

  i = NUM_RETRIES_POWERUP;                                // Note 1
  for (;;) {
    //
    // Try initializing as SD card first. Note (1).
    //
    _EnableCS(pInst);
    Response = _ExecCmdR1(pInst, CMD_ACMD_CMD, 0);        // Prepare for advanced command.
    _SendEmptyCycles(pInst, 1);                           // Clock card before next command.
    if (Response == 0u) {
      Response = _ExecCmdR1(pInst, ACMD_SEND_OP_COND, 0); // Send ACMD41 (SD_SEND_OP_COND).
    }
    _DisableCS(pInst);
    _SendEmptyCycles(pInst, 1);                           // Clock card before next command.
    if (Response == 0u) {
      pInst->CardType = CARD_TYPE_SD;                     // SD card is now ready.
      break;
    }
    if ((Response & 4u) != 0u) {
      break;                                              // Error, command not accepted.
    }
    if (--i == 0) {
      Response = 0xFF;                                    // Error, response timeout.
      break;
    }
  }
  if (Response != 0u) {                                   // We need to try to initialize the card as MMC.
    i = NUM_RETRIES_POWERUP;                              // Note 1
    for (;;) {
      _EnableCS(pInst);
      Response = _ExecCmdR1(pInst, CMD_SEND_OP_COND, 0);  // Send CMD1 (SEND_OP_COND) until ready or timeout.
      _DisableCS(pInst);
      _SendEmptyCycles(pInst, 1);
      if (Response == 0u) {
        pInst->CardType = CARD_TYPE_MMC;
        break;                                            // The card is ready!

      }
      if (Response != 1u) {
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "MMC_SPI: _InitMMC_SD: Invalid response."));
        return 1;
      }
      if  (--i == 0) {
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "MMC_SPI: _InitMMC_SD: Time out during init."));
        return 1;
      }
    }
  }
  r = _ReadCSD(pInst, &csd);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "MMC_SPI: _InitMMC_SD: CSD (card spec. data) invalid."));
    return 1;         // Error, could not read CSD register.
  }
  r = _ApplyCSD(pInst, &csd);
  if (r != 0) {
    return 1;         // Error, invalid parameters in CSD register.
  }
  pInst->IsInited = 1;
  return 0;           // OK, card identified.
}

/*********************************************************************
*
*       _InitSD_V2
*
*  Function description
*    Initialize a version 2 SD card.
*
*  Parameters
*    pInst    Driver instance.
*
*  Notes
*    (1) MMC Specs says (sect 6.3, power up) that it takes max. 1 ms
*        for the card to be ready (leave idle/init). At 400 kHz, this equals 9 loops.
*        At lower speeds, less repetitions would be OK, but a few more ms.
*        should not hurt (only in case the card is not present)
*/
static int _InitSD_V2(MMC_INST * pInst) {
  CSD csd;
  int TimeOut;
  U8  Response;
  int r;

  TimeOut = NUM_RETRIES_POWERUP;        // Note 1
  for (;;) {
    _EnableCS(pInst);
    //
    // Prepare for advanced command
    //
    Response = _ExecCmdR1(pInst, CMD_ACMD_CMD, 0);
    _SendEmptyCycles(pInst, 1);
    if (Response != 0xFFu) {
      //
      // Send ACMD41 (SD_SEND_OP_COND), set argument, that host supports HC.
      //
      Response = _ExecCmdR1(pInst, ACMD_SEND_OP_COND, (1uL << 30));
    }
    _DisableCS(pInst);
    _SendEmptyCycles(pInst, 1);
    if (Response == 0u) {
      break;          // SD card is now ready.
    }
    if (--TimeOut == 0) {
      break;
    }
  }
  r = _ReadCSD(pInst, &csd);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "MMC_SPI: _InitSD_V2: CSD (card spec. data) invalid."));
    return -1;        // Error, could not read CSD register.
  }
  //
  // Card is identified as SD card. We check later whether it is a standard or HC card.
  //
  pInst->CardType = CARD_TYPE_SD;
  r = _ApplyCSD(pInst, &csd);
  if (r != 0) {
    return -1;        // Error, invalid parameters in CSD register.
  }
  pInst->IsInited = 1;
  return 0;           // OK, card identified.
}

/*********************************************************************
*
*       _MMC_Init
*
*  Function description
*    MMC driver internal function. Reset the card, reset SPI clock speed
*    and set it to SPI mode.
*
*  Parameters
*    pInst    Driver instance.
*
*  Return value
*    ==0      CSD has been read and all parameters are OK.
*    !=0      An error has occurred.
*
*  Notes
*    (1)   Argument structure for CMD8 (SEND_IF_COND)
*           [31..12] - Shall be zero
*           [11.. 8] - Voltage Supply (VHS):
*                      Defined Voltage Supplied Values (VHS defined by SDCard Spec V2.00)
*                        0x00 - Not Defined
*                        0x01 - 2.7-3.6V
*                        0x02 - Reserved for Low Voltage Range
*                        0x04 - Reserved
*                        0x08 - Reserved
*                        Others Not Defined
*           [ 7.. 0] - Check Pattern. It is recommended to use the value 0xAA as pattern.
*/
static int _MMC_Init(MMC_INST * pInst) {
  U8  Response;
  U16 CurrFreq;
  U8  aResponse7[4];
  int r;
  int NumRetries;

  CurrFreq = _SetMaxSpeed(pInst, STARTUP_FREQ);     // Set initial speed for SPI.
  if (CurrFreq > STARTUP_FREQ) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "MMC_SPI: _MMC_Init: Frequency is greater than allowed for initialization."));
    return -1;
  }
  pInst->Nac = NAC_CSD_MAX;
  //
  // Send empty cycles and CMD0 (GO_IDLE_STATE) until card responds with 0x01 = OK.
  // Allow multiple tries.
  //
  NumRetries = NUM_RETRIES_INIT;
  for (;;) {
    _SendEmptyCycles(pInst, NUM_CYCLES_INIT);
    Response = _ExecCmdInit(pInst);
    if ((Response != 0xFFu) && ((Response & 1u) != 0u)) {
      break;              // OK, the card is not in idle state.
    }
    if (--NumRetries == 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "MMC_SPI: _MMC_Init: Card is not in idle state."));
      return -1;          // Error, the card is not in idle state.
    }
  }
  //
  // Send CMD8 to card, SD HC or SD cards V2.00 card will accept the command
  // all other cards will reply that this is an illegal command.
  // Initially we will only read one byte from card.
  // If it is not an illegal command, we will do further reading.
  //
  NumRetries = FS_MMC_NUM_RETRIES;
  for (;;) {
    _EnableCS(pInst);
    Response = _ExecCmdR1(pInst, CMD_SEND_IF_COND, (0x01uL << 8) | 0xAAuL); // Note 1
    if ((Response & 4u) != 0u) {    // Illegal command, not a SD V2 card.
      _DisableCS(pInst);
      r = _InitMMC_SD(pInst);
    } else {
      (void)_Read(pInst, &aResponse7[0], (int)sizeof(aResponse7));
      _DisableCS(pInst);
      //
      // Did the card return the correct pattern?
      //
      if ((aResponse7[3] == 0xAAu) && ((aResponse7[2] & 0xFu) == 0x01u)) {
        r = _InitSD_V2(pInst);
      } else {
        //
        // WORKAROUND: There are SD cards that return OK as response to CMD_SEND_IF_COND
        // even if the command is not supported. Try again here to initialize them.
        //
        r = _InitMMC_SD(pInst);
      }
    }
    if (r == 0) {
      break;
    }
    if (NumRetries-- == 0) {
      break;                          // Error, retried too many times.
    }
  }
  return r;
}

/*********************************************************************
*
*       _ReadSectors
*
*  Function description
*    Reads one ore more sectors from storage medium.
*
*  Parameters
*    pInst          Driver instance.
*    SectorIndex    Index of the first sector to be read from device.
*    pBuffer        Pointer to read buffer.
*    NumSectors     Number of sectors to be transferred.
*
*  Return value
*    ==0    Sector have been read.
*    !=0    An error occurred.
*/
static int _ReadSectors(const MMC_INST * pInst, U32 SectorIndex, U8 * pBuffer, U32 NumSectors) {
  U8  Response;
  U8  aCRC[2];
  U16 Crc;
  U16 CalcedCrc;
  U8  Cmd;
  int r;
  int Result;

  r    = -1;                        // Default is to return error
  Cmd  = (NumSectors == 1u) ? CMD_READ_SINGLE_BLOCK : CMD_READ_MULTIPLE_BLOCKS;
  if (pInst->CardType != CARD_TYPE_SDHC) {
    SectorIndex *= BYTES_PER_SECTOR;
  }
  //
  // Send command.
  //
  _SendEmptyCycles(pInst, 1);
  _EnableCS(pInst);
  Response = _ExecCmdR1(pInst, Cmd, SectorIndex);
  //
  // Read sector by sector.
  //
  if (Response == 0u) {
    for (;;) {
      Response = _WaitBlockRdTok(pInst);                      // Wait for data block to begin.
      if (Response == 0xFFu) {
        break;                                                // Error, no block begin token.
      }
      r = 0;                                                  // Set to indicate success.
      Result = _Read(pInst, pBuffer, (int)BYTES_PER_SECTOR);  // Read data of one sector.
      r = (Result != 0) ? Result : r;
      Result = _Read(pInst, aCRC, 2);                         // Read CRC16.
      r = (Result != 0) ? Result : r;
      if (r != 0) {
        break;                                                // Error, could not read data.
      }
      Crc = ((U16)aCRC[0] << 8) | (aCRC[1]);
      CalcedCrc = _CalcDataCRC16(pBuffer, BYTES_PER_SECTOR);
      if ((CalcedCrc != 0xFFFFu) && (CalcedCrc != Crc)) {
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_SPI: _ReadSectors: Received wrong CRC, received 0x%8x, expected 0x%8x @Sector 0x%8x.", Crc, CalcedCrc, SectorIndex));
        if (Cmd == CMD_READ_MULTIPLE_BLOCKS) {
          (void)_ExecCmdR1(pInst, CMD_STOP_TRANSMISSION, 0);
        }
        break;
      }
      pBuffer += BYTES_PER_SECTOR;
      if (--NumSectors == 0u) {                               // Are we done?
        r = 0;
        if (Cmd == CMD_READ_MULTIPLE_BLOCKS) {
          r = _StopTransmission(pInst);
        }
        break;
      }
    }
  }
  //
  // We are done. Disable CS and send some dummy clocks.
  //
  _DisableCS(pInst);
  _SendEmptyCycles(pInst, 1);
  return r;
}

/*********************************************************************
*
*       _ReadSectorsWithRetry
*
*  Function description
*    Reads one ore more sectors from storage medium.
*    In case of a failure the read operation is executed again.
*/
static int _ReadSectorsWithRetry(MMC_INST * pInst, U32 SectorIndex, U8 * pBuffer, U32 NumSectors) {        //lint -efunc(818, _ReadSectorsWithRetry) pInst cannot be declared as pointing to const because the statistical counters are updated in debug builds.
  int r;
  int NumRetries;

  NumRetries = FS_MMC_NUM_RETRIES;
  for (;;) {
    r = _ReadSectors(pInst, SectorIndex, pBuffer, NumSectors);
    if (r == 0) {
      IF_STATS(pInst->StatCounters.ReadSectorCnt += NumSectors);
      break;
    }
    if (NumRetries-- == 0) {
      break;
    }
    IF_STATS(pInst->StatCounters.ReadErrorCnt++);
  }
  return r;
}

/*********************************************************************
*
*       _WriteSectors
*
*  Function description
*    Writes one ore more sectors to storage medium.
*
*  Parameters
*    pInst          Driver instance.
*    SectorIndex    Index of the first sector to be written to the device.
*    pBuffer        Pointer to data to be stored.
*    NumSectors     Number of sectors to be transferred.
*    RepeatSame     Shall be the same data written.
*
*  Return value
*    ==0    Sector has been written to the device.
*    !=0    An error has occurred.
*/
static int _WriteSectors(const MMC_INST * pInst, U32 SectorIndex, const U8 * pBuffer, U32 NumSectors, U8 RepeatSame) {
  U8  Response;
  int r;
  U8  aCRC[2];
  U16 crc;
  U8  Cmd;
  U8  Token;
  int Result;

  r    = -1;
  Cmd  = (NumSectors == 1u) ? CMD_WRITE_SINGLE_BLOCK : CMD_WRITE_MULTIPLE_BLOCK;
  if (pInst->CardType != CARD_TYPE_SDHC) {
    SectorIndex *= BYTES_PER_SECTOR;
  }
  //
  // Send command.
  //
  _SendEmptyCycles(pInst, 1);
  _EnableCS(pInst);
  Response = _ExecCmdR1(pInst, Cmd, SectorIndex);
  if (Response != 0u) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "MMC_SPI: _WriteSectors: Command not accepted."));
    goto Done;                // Error, command not accepted.
  }
  _SendEmptyCycles(pInst, 1);
  //
  // Transfer data one sector at a time.
  //
  do {
    crc = _CalcDataCRC16(pBuffer, BYTES_PER_SECTOR);
    aCRC[0] = (U8)(crc >> 8);
    aCRC[1] = (U8)crc;
    //
    // Send data token.
    //
    Token = (Cmd == CMD_WRITE_SINGLE_BLOCK) ? TOKEN_BLOCK_WRITE_START : TOKEN_MULTI_BLOCK_WRITE_START;
    r = 0;                    // Set to indicate success.
    Result = _Write(pInst, &Token, 1);
    r = (Result != 0) ? Result : r;
    Result = _Write(pInst, pBuffer, (int)BYTES_PER_SECTOR);
    r = (Result != 0) ? Result : r;
    Result = _Write(pInst, aCRC, 2);
    r = (Result != 0) ? Result : r;
    if (r != 0) {
      goto Done;              // Error, data not written.
    }
    //
    // Get data response token (MMC spec 7.6.2, Figure 52)
    // Should be XXX00101
    //
    r = _Read(pInst, &Response, 1);
    if (r != 0) {
      goto Done;              // Error, response not received.
    }
    if ((Response & 0x1Fu) != 5u) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "MMC_SPI: _WriteSectors: Data not accepted."));
      r = 1;
      goto Done;              // Error, invalid response.
    }
    //
    // Wait for card to get ready.
    //
    r = _WaitToGetReady(pInst);
    if (r != 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "MMC_SPI: _WriteSectors: Card ready timeout."));
      goto Done;              // Error, card not ready.
    }
    if (RepeatSame == 0u) {
      pBuffer += BYTES_PER_SECTOR;
    }
  } while (--NumSectors != 0u);
  //
  // Send the stop token to card, this indicates, that we are finished sending data to card.
  //
  if (Cmd == CMD_WRITE_MULTIPLE_BLOCK) {
    Token = TOKEN_MULTI_BLOCK_WRITE_STOP;
    r = _Write(pInst, &Token, 1);
    if (r != 0) {
      goto Done;              // Error, command not sent.
    }
    r = _WaitToGetReady(pInst);
    if (r != 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "MMC_SPI: _WriteSectors: Card ready timeout."));
      goto Done;              // Error, card not ready.
    }
  }
  r = 0;                      // OK, data written.
Done:
  _DisableCS(pInst);
  _SendEmptyCycles(pInst, 1);
  return r;
}

/*********************************************************************
*
*       _InitIfRequired
*/
static int _InitIfRequired(MMC_INST * pInst) {
  int r;

  r = 0;                  // Set to indicate success.
  if (pInst->IsInited == 0u) {
    ASSERT_HW_TYPE_IS_SET(pInst);
    LOCK_SPI(pInst);
    r = _MMC_Init(pInst);
    UNLOCK_SPI(pInst);
    if (r != 0) {         // Error, init failed. No valid card in slot.
      FS_DEBUG_WARN((FS_MTYPE_DRIVER, "MMC_SPI: Init failure, no valid card found."));
    }
  }
  return r;
}

/*********************************************************************
*
*      _AllocInstIfRequired
*/
static MMC_INST * _AllocInstIfRequired(U8 Unit) {
  MMC_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  pInst = NULL;
  if (Unit < (U8)FS_MMC_NUM_UNITS) {
    pInst = _apInst[Unit];
    if (pInst == NULL) {
      pInst = SEGGER_PTR2PTR(MMC_INST, FS_ALLOC_ZEROED((I32)sizeof(MMC_INST), "MMC_INST"));
      if (pInst != NULL) {
        _apInst[Unit] = pInst;
        pInst->Unit   = Unit;
      }
    }
  }
  return pInst;
}

/*********************************************************************
*
*       _GetInst
*/
static MMC_INST * _GetInst(U8 Unit) {
  MMC_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  pInst = NULL;
  if (Unit < (U8)FS_MMC_NUM_UNITS) {
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
*       _MMC_GetStatus
*
*  Function description
*    FS driver function. Get status of the media,
*    Initialize the card if necessary.
*
*  Parameters
*    Unit   Unit number.
*
*  Return value
*    FS_MEDIA_STATE_UNKNOWN - if the state of the media is unknown.
*    FS_MEDIA_NOT_PRESENT   - if no card is present.
*    FS_MEDIA_IS_PRESENT    - if a card is present.
*/
static int _MMC_GetStatus(U8 Unit) {
  int        Status;
  MMC_INST * pInst;

  Status = FS_MEDIA_STATE_UNKNOWN;
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    ASSERT_HW_TYPE_IS_SET(pInst);
    LOCK_SPI(pInst);
    Status = _IsPresent(pInst);
    UNLOCK_SPI(pInst);
  }
  return Status;
}

/*********************************************************************
*
*       _MMC_IoCtl
*
*  Function description
*    FS driver function. Execute device command.
*
*  Parameters
*    Unit       Device Index.
*    Cmd        Command to be executed.
*    Aux        Parameter depending on command.
*    pBuffer    Pointer to a buffer used for the command.
*
*  Return value
*    Command specific. In general a negative value means an error.
*/
static int _MMC_IoCtl(U8 Unit, I32 Cmd, I32 Aux, void * pBuffer) {
  FS_DEV_INFO * pDevInfo;
  MMC_INST    * pInst;
  int           r;

  FS_USE_PARA(Aux);
  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return -1;                      // Error, driver instance not allocated.
  }
  r = -1;                           // Set to indicate an error.
  ASSERT_HW_TYPE_IS_SET(pInst);
  switch (Cmd) {
  case FS_CMD_UNMOUNT:
  case FS_CMD_UNMOUNT_FORCED:
    pInst->IsInited = 0;
    r = 0;
    break;
  case FS_CMD_GET_DEVINFO:          // Get general device information.
    r = _InitIfRequired(pInst);
    if (r == 0) {
      pDevInfo = SEGGER_PTR2PTR(FS_DEV_INFO, pBuffer);
      if (pDevInfo != NULL) {
        pDevInfo->BytesPerSector = BYTES_PER_SECTOR;
        pDevInfo->NumSectors     = pInst->NumSectors;
      }
    }
    break;
  case FS_CMD_FREE_SECTORS:
    //
    // Return OK even if we do nothing here in order to
    // prevent that the file system reports an error.
    //
    r = 0;
    break;
#if FS_SUPPORT_DEINIT
  case FS_CMD_DEINIT:
    FS_FREE(pInst);
    _apInst[Unit] = NULL;
    _NumUnits--;
    r = 0;
    break;
#endif
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
*       _MMC_Read
*
*  Function description
*    Reads one or more sectors from the storage device.
*
*  Parameters
*    Unit           Device index number.
*    SectorIndex    Index of the first sector to be read from the device.
*    pData          Read data read from storage device.
*    NumSectors     Number of sectors to be read.
*
*  Return value
*    ==0    OK, sector data read.
*    !=0    An error occurred.
*/
static int _MMC_Read(U8 Unit, U32 SectorIndex, void * pData, U32 NumSectors) {
  int        r;
  U8       * pData8;
  MMC_INST * pInst;
  U32        NumSectorsAtOnce;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;                 // Error, driver instance not found.
  }
  ASSERT_HW_TYPE_IS_SET(pInst);
  pData8 = SEGGER_PTR2PTR(U8, pData);
  LOCK_SPI(pInst);
  //
  // Workaround for the ATP 512 MB microSD which reports an error
  // when a multiple read operation ends on a last sector.
  //
  NumSectorsAtOnce = NumSectors;
#if FS_MMC_READ_SINGLE_LAST_SECTOR
  {
    U32 NumSectorsTotal;

    NumSectorsTotal = pInst->NumSectors;
    if ((NumSectors > 1u) && (SectorIndex + NumSectors) >= NumSectorsTotal) {
      --NumSectorsAtOnce;     // Perform a single read on the last sector.
    }
  }
#endif // FS_MMC_READ_SINGLE_LAST_SECTOR
  r = _ReadSectorsWithRetry(pInst, SectorIndex, pData8, NumSectorsAtOnce);
  if (r == 0) {
    NumSectors -= NumSectorsAtOnce;
    if (NumSectors != 0u) {
      SectorIndex += NumSectorsAtOnce;
      pData8      += NumSectorsAtOnce * BYTES_PER_SECTOR;
      r = _ReadSectorsWithRetry(pInst, SectorIndex, pData8, NumSectors);
    }
  }
  UNLOCK_SPI(pInst);
  return r;
}

/*********************************************************************
*
*       _MMC_Write
*
*  Function description
*    Write one ore more sectors to the storage device.
*
*  Parameters
*    Unit           Device index number.
*    SectorIndex    Index of the first sector to be written to the storage device.
*    pData          Data to be written to storage device.
*    NumSectors     Number of sectors to be transferred.
*    RepeatSame     Set to 1 if be the same data has to be written.
*
*  Return value
*    ==0    OK, sector data written.
*    !=0    An error occurred.
*/
static int _MMC_Write(U8 Unit, U32 SectorIndex, const void * pData, U32 NumSectors, U8 RepeatSame) {
  const U8 * pData8;
  MMC_INST * pInst;
  int        r;
  int        NumRetries;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;                 // Error, driver instance not found.
  }
  if (pInst->IsWriteProtected != 0u) {
    return 1;
  }
  ASSERT_HW_TYPE_IS_SET(pInst);
  NumRetries = FS_MMC_NUM_RETRIES;
  pData8     = SEGGER_CONSTPTR2PTR(const U8, pData);
  LOCK_SPI(pInst);
  for (;;) {
    r = _WriteSectors(pInst, SectorIndex, pData8, NumSectors, RepeatSame);
    if (r == 0) {
      IF_STATS(pInst->StatCounters.WriteSectorCnt += NumSectors);
      break;
    }
    if (NumRetries-- == 0) {
      break;
    }
    IF_STATS(pInst->StatCounters.WriteErrorCnt++);
  }
  UNLOCK_SPI(pInst);
  return r;
}

/*********************************************************************
*
*       _MMC_InitMedium
*
*  Function description
*    Initializes the SD/MMC card.
*
*  Parameters
*    Unit  Driver unit number.
*
*  Return value
*    == 0    Device OK and ready for operation.
*    != 0    An error has occurred.
*/
static int _MMC_InitMedium(U8 Unit) {
  int        r;
  MMC_INST * pInst;
  int        Status;

  r     = 1;              // Set to indicate an error.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    r = 0;                // Set to indicate success.
    if (pInst->IsInited == 0u) {
      //
      // The hardware layer does not have a specific initialization function.
      // The initialization of the hardware layer is done when the card
      // presence detection function is called for the first time.
      //
      r = 1;              // Set to indicate error.
      LOCK_SPI(pInst);
      Status = _IsPresent(pInst);
      UNLOCK_SPI(pInst);
      if (Status != FS_MEDIA_NOT_PRESENT) {
        r = _InitIfRequired(pInst);
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _MMC_AddDevice
*
*  Function description
*    Initializes the driver instance.
*
*  Return value
*    >=0    Unit number of the created instance.
*    < 0    An error occurred.
*/
static int _MMC_AddDevice(void) {
  U8         Unit;
  MMC_INST * pInst;

  if (_NumUnits >= (U8)FS_MMC_NUM_UNITS) {
    return -1;                          // Error, too many driver instances.
  }
  Unit = _NumUnits;
  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return -1;                          // Error, could not allocate driver instance.
  }
  _NumUnits++;
  return (int)Unit;                     // OK, instance allocated.
}

/*********************************************************************
*
*       _MMC_GetNumUnits
*/
static int _MMC_GetNumUnits(void) {
  return (int)_NumUnits;
}

/*********************************************************************
*
*       _MMC_GetDriverName
*/
static const char * _MMC_GetDriverName(U8 Unit) {
  FS_USE_PARA(Unit);
  return "mmc";
}

/*********************************************************************
*
*       Public data
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_MMC_SPI_Driver
*
*  API structure of the SPI MMC driver
*/
const FS_DEVICE_TYPE FS_MMC_SPI_Driver = {
  _MMC_GetDriverName,
  _MMC_AddDevice,
  _MMC_Read,
  _MMC_Write,
  _MMC_IoCtl,
  _MMC_InitMedium,
  _MMC_GetStatus,
  _MMC_GetNumUnits
};

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*      FS_MMC_GetCardId
*
*  Function description
*    This function retrieves the card Id of SD/MMC card.
*
*  Parameters
*    Unit     Device index number.
*    pCardId  [OUT] Card identification data.
*
*  Return value
*    ==0    CardId has been read.
*    !=0    An error has occurred.
*
*  Additional information
*    This function is optional. The application can call this
*    function to get the information stored in the CID register
*    of an MMC or SD card. The CID register stores information which
*    can be used to uniquely identify the card such as serial number,
*    product name, manufacturer id, etc. For more information about
*    the information stored in this register refer to SD or MMC specification.
*/
int FS_MMC_GetCardId(U8 Unit, FS_MMC_CARD_ID * pCardId) {
  U8         Response;
  U32        TimeOut;
  U8         aData[2];
  int        r;
  MMC_INST * pInst;
  int        Result;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;                     // Error, driver instance not found.
  }
  LOCK_SPI(pInst);
  _SendEmptyCycles(pInst, 1);
  _EnableCS(pInst);
  //
  // Execute CMD10 (SEND_CID)
  //
  Response = _ExecCmdR1(pInst, CMD_SEND_CID, 0);
  if (Response != 0u) {
    r = 1;                        // Error, command execution failed.
    goto Done;
  }
  //
  // Wait for CardId transfer to begin.
  //
  TimeOut = pInst->Nac;
  for (;;) {
    r = _Read(pInst, aData, 1);
    if (r == 0) {
      if (aData[0] == TOKEN_BLOCK_READ_START) {
        break;
      }
      if (aData[0] == TOKEN_MULTI_BLOCK_WRITE_START) {
        break;
      }
    }
    if (TimeOut-- == 0u) {
      r = 1;                      // Error, response timeout.
      goto Done;
    }
  }
  //
  // Read the CardID.
  //
  r = 0;                          // Set to indicate success.
  Result = _Read(pInst, SEGGER_PTR2PTR(U8, pCardId), (int)sizeof(FS_MMC_CARD_ID));
  r = (Result != 0) ? Result : r;
  Result = _Read(pInst, aData, 2);    // Read CRC16
  r = (Result != 0) ? Result : r;
Done:
  _DisableCS(pInst);
  _SendEmptyCycles(pInst, 1);     // Clock the card after command
  UNLOCK_SPI(pInst);
  return r;
}

/*********************************************************************
*
*       FS_MMC_ActivateCRC
*
*  Function description
*    Enables the data verification.
*
*  Additional information
*    This function is optional. The data verification uses a 16-bit CRC
*    to detect any corruption of the data being exchanged with the storage
*    device. By default, the data verification is disabled to improve performance.
*    FS_MMC_ActivateCRC() can be used at runtime to enable the data
*    verification for all driver instances. The data verification can
*    be disabled via FS_MMC_DeactivateCRC().
*/
void FS_MMC_ActivateCRC(void) {
  _pfCalcCRC = _CalcDataCRC16ViaTable;
}

/*********************************************************************
*
*       FS_MMC_DeactivateCRC
*
*  Function description
*    Disables the data verification.
*
*  Additional information
*    This function is optional. It can be used by an application to
*    disable the data verification previously enabled via FS_MMC_ActivateCRC().
*/
void FS_MMC_DeactivateCRC(void) {
  _pfCalcCRC = _CalcDataCRC16Dummy;
}

#if FS_MMC_ENABLE_STATS

/*********************************************************************
*
*       FS_MMC_GetStatCounters
*
*  Function description
*    Returns the value of statistical counters.
*
*  Parameters
*    Unit       Index of the driver (0-based)
*    pStat      [OUT] The values of statistical counters.
*
*  Additional information
*    This function is optional. The SPI SD/MMC driver collects
*    statistics about the number of internal operations such as the
*    number of logical sectors read or written by the file system layer.
*    The application can use FS_MMC_GetStatCounters() to get the
*    current value of these counters. The statistical counters are
*    automatically set to 0 when the storage device is mounted or when
*    the application calls FS_MMC_ResetStatCounters().
*
*    The statistical counters are available only when the file system
*    is compiled with FS_DEBUG_LEVEL greater than or equal to
*    FS_DEBUG_LEVEL_CHECK_ALL or with FS_MMC_ENABLE_STATS set to 1.
*/
void FS_MMC_GetStatCounters(U8 Unit, FS_MMC_STAT_COUNTERS * pStat) {
  MMC_INST * pInst;

  pInst  = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    *pStat = pInst->StatCounters;     // Struct copy
  } else {
    FS_MEMSET(pStat, 0, sizeof(FS_MMC_STAT_COUNTERS));
  }
}

/*********************************************************************
*
*       FS_MMC_ResetStatCounters
*
*  Function description
*    Sets the values of all statistical counters to 0.
*
*  Parameters
*    Unit       Index of the driver instance (0-based).
*
*  Additional information
*    This function is optional. The statistical counters are
*    automatically set to 0 when the storage device is mounted.
*    The application can use FS_MMC_ResetStatCounters() at any
*    time during the file system operation. The statistical counters
*    can be queried via FS_MMC_GetStatCounters().
*
*    The statistical counters are available only when the file system
*    is compiled with FS_DEBUG_LEVEL greater than or equal to
*    FS_DEBUG_LEVEL_CHECK_ALL or with FS_MMC_ENABLE_STATS set to 1.
*/
void FS_MMC_ResetStatCounters(U8 Unit) {
  MMC_INST * pInst;

  pInst  = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    FS_MEMSET(&pInst->StatCounters, 0, sizeof(FS_MMC_STAT_COUNTERS));
  }
}

#endif // FS_MMC_ENABLE_STATS

/*********************************************************************
*
*       FS_MMC_SetHWType
*
*  Function description
*    Configures the hardware access routines.
*
*  Additional information
*    This function is mandatory and it has to be called once for each
*    instance of the driver.
*/
void FS_MMC_SetHWType(U8 Unit, const FS_MMC_HW_TYPE_SPI * pHWType) {
  MMC_INST * pInst;

  pInst  = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->pHWType = pHWType;
  }
}

/*********************************************************************
*
*      FS_MMC_GetCardInfo
*
*  Function description
*    This function returns information about the SD/MMC device.
*
*  Parameters
*    Unit         Index of the driver instance (0-based).
*    pCardInfo    [OUT] Information about the device.
*
*  Return value
*    ==0    Card information returned.
*    !=0    An error has occurred.
*
*  Additional information
*    This function is optional. It can be used to get information about the
*    type of the storage card used, about the type of the data transfer, etc.
*/
int FS_MMC_GetCardInfo(U8 Unit, FS_MMC_CARD_INFO * pCardInfo) {
  int        r;
  MMC_INST * pInst;

  if (pCardInfo == NULL) {
    return 1;       // Error, invalid parameter.
  }
  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;       // Error, could not allocate driver instance.
  }
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return 1;       // Error, could not initialize the SD/MMC card.
  }
  pCardInfo->BusWidth         = 1;
  pCardInfo->VoltageLevel     = 3300;
  pCardInfo->DriverStrength   = FS_MMC_DRIVER_STRENGTH_TYPE_B;
  pCardInfo->BytesPerSector   = (U16)BYTES_PER_SECTOR;
  pCardInfo->CardType         = pInst->CardType;
  pCardInfo->IsWriteProtected = pInst->IsWriteProtected;
  pCardInfo->NumSectors       = pInst->NumSectors;
  pCardInfo->ClockFreq        = pInst->Freq_kHz * 1000uL;
  pCardInfo->IsHighSpeedMode  = (pInst->AccessMode == FS_MMC_ACCESS_MODE_HS) ? 1u : 0u;
  pCardInfo->AccessMode       = pInst->AccessMode;
  return 0;         // OK, information returned.
}

/*************************** End of file ****************************/
