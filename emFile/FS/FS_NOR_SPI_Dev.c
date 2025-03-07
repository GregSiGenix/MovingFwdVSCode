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
File        : FS_NOR_SPI_Dev.c
Purpose     : Routines related to handling of SPI NOR devices.
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#include <stddef.h>
#include "FS_Int.h"
#include "FS_NOR_Int.h"

/*********************************************************************
*
*       Defines, configurable (for testing only)
*
**********************************************************************
*/
#ifndef   FS_NOR_SFDP_DENSITY_SHIFT
  #define FS_NOR_SFDP_DENSITY_SHIFT     0       // Capacity of the storage device as power of 2 exponent.
                                                // 0 means that the value read from the NOR flash device has to be used.
#endif

/*********************************************************************
*
*       Defines, fixed
*
**********************************************************************
*/

/*********************************************************************
*
*       Commands
*/
#define CMD_WRSR                        0x01u   // Write the status register
#define CMD_PP                          0x02u   // Page Program
#define CMD_WRDIS                       0x04u   // Write disable
#define CMD_RDSR                        0x05u   // Read Status Register
#define CMD_WREN                        0x06u   // Write Enable
#define CMD_RDSR2                       0x07u   // Read Status Register 2
#define CMD_FAST_READ                   0x0Bu   // Read Data Bytes at Higher Speed
#define CMD_FAST_READ4B                 0x0Cu   // Read Data Bytes at Higher Speed with 4-byte address
#define CMD_READ_DTR                    0x0Du   // Read data in 1S-1D-1D mode (Winbond specific)
#define CMD_PP4B                        0x12u   // Page Program with 4-byte address
#define CMD_RDCR_ALT                    0x15u   // Read configuration register (Macronix specific)
#define CMD_RDSR3                       0x15u   // Read status register 3 (GigaDevice specific)
#define CMD_BRRD                        0x16u   // Bank Register Read (Spansion and ISSI specific)
#define CMD_BRWR                        0x17u   // Bank Register Write (Spansion and ISSI specific)
#define CMD_P4E                         0x20u   // 4 KB sector erase
#define CMD_RDSCUR                      0x2Bu   // Security register (Macronix specific)
#define CMD_CLSR                        0x30u   // Clear the error bits in the status register (Spansion and GigaDevice specific)
#define CMD_WRSR2                       0x31u   // Write status register 2 (GigaDevice specific)
#define CMD_RDCR                        0x35u   // Read configuration register (Spansion and Microchip specific)
#define CMD_RDSR2_ALT                   0x35u   // Read status register 2 (GigaDevice and Winbond specific)
#define CMD_WBPR                        0x42u   // Write Block-Protection Register (Microchip specific)
#define CMD_CFSR                        0x50u   // Clear the error bits flag status register (Micron specific)
#define CMD_WRENV                       0x50u   // Enable write for volatile (Cypress specific)
#define CMD_READ_SFDP                   0x5Au   // Read parameters
#define CMD_WECR                        0x61u   // Write Enhanced Volatile Configuration Register (Micron specific)
#define CMD_RDSRI                       0x65u   // Read status register indirect (Adesto specific)
#define CMD_RECR                        0x65u   // Read Enhanced Volatile Configuration Register (Micron specific)
#define CMD_RFSR                        0x70u   // Read flag status register
#define CMD_RDCR2                       0x71u   // Read Configuration Register 2 (Macronx specific)
#define CMD_RBPR                        0x72u   // Read Block-Protection Register (Microchip specific)
#define CMD_WRCR2                       0x72u   // Write Configuration Register 2 (Macronx specific)
#define CMD_RDERP                       0x81u   // Read Extended Read Register (ISSI specific)
#define CMD_CLERP                       0x82u   // Clear Extended Read Register (ISSI specific)
#define CMD_RDID                        0x9Fu   // Read Identification
#define CMD_RES                         0xABu   // Release from deep power-down
#define CMD_EN4B                        0xB7u   // Enter 4-byte address mode (Micron and GigaDevice specific)
#define CMD_DUAL_READ                   0xBBu   // Read data via 2 lines (ISSI specific)
#define CMD_DUAL_READ_DTR               0xBDu   // Read data in 1S-2D-2D mode (Winbond specific)
#define CMD_SE                          0xD8u   // Sector Erase
#define CMD_SE4B                        0xDCu   // Sector Erase with 4-byte address.
#define CMD_EX4B                        0xE9u   // Exit 4-byte address mode (Micron and GigaDevice specific)
#define CMD_QUAD_READ                   0xEBu   // Read data via 4 lines (ISSI specific)
#define CMD_QUAD_READ_DTR               0xEDu   // Read data in 1S-4D-4D mode (Winbond specific)
#define CMD_8READ                       0xECu   // Read data via 8 lines in STR mode (Macronix specific)
#define CMD_8DTRD                       0xEEu   // Read data via 8 lines in DTR mode (Macronix specific)

/*********************************************************************
*
*       Bits and masks in the status register
*/
#define STATUS_BUSY_BIT                 0
#define STATUS_WEL_BIT                  1
#define STATUS_BP_BIT                   2
#define STATUS_BP_MASK                  0x7uL   // Bit mask of the write protection flags
#define STATUS_E_ERR_BIT                5       // Spansion specific
#define STATUS_P_ERR_BIT                6       // Spansion specific
#define STATUS_QE_BIT                   6       // ISSI and Macronix specific (enables pins for QUAD operation, non-volatile)

/*********************************************************************
*
*       Bits in the Extended Read Register (ISSI specific)
*/
#define EXT_READ_PROT_E_BIT             1
#define EXT_READ_P_ERR_BIT              2
#define EXT_READ_E_ERR_BIT              3

/*********************************************************************
*
*       Bits and masks in the status register 3 (GigaDevice specific)
*/
#define STATUS3_PE_BIT                  2
#define STATUS3_EE_BIT                  3

/*********************************************************************
*
*       Bits in the flag status register (Micron specific)
*/
#define FLAG_STATUS_ADDR_BIT            0
#define FLAG_STATUS_PROT_ERROR_BIT      1
#define FLAG_STATUS_VPP_ERROR_BIT       3
#define FLAG_STATUS_PROG_ERROR_BIT      4
#define FLAG_STATUS_ERASE_ERROR_BIT     5
#define FLAG_STATUS_READY_BIT           7

/*********************************************************************
*
*       Bits in the security register (Macronix specific)
*/
#define SCUR_E_FAIL_BIT                 6
#define SCUR_P_FAIL_BIT                 5

/*********************************************************************
*
*       Bits in the status register 2 (Cypress specific)
*/
#define STATUS2_E_ERR_BIT               6
#define STATUS2_P_ERR_BIT               5

/*********************************************************************
*
*       Bits in the status register 4 (Adesto specific)
*/
#define STATUS4_EE_BIT                  4
#define STATUS4_PE_BIT                  5

/*********************************************************************
*
*       Defines related to Configuration Register 2 (Macronix specific)
*/
#define CONFIG2_ADDR_MODE               0x00000000uL
#define CONFIG2_ADDR_DUMMY              0x00000300uL
#define CONFIG2_MODE_BIT                0
#define CONFIG2_MODE_SPI                0x0uL
#define CONFIG2_MODE_SOPI               0x1uL
#define CONFIG2_MODE_DOPI               0x2uL
#define CONFIG2_MODE_MASK               0x3uL
#define CONFIG2_DUMMY_BIT               0
#define CONFIG2_DUMMY_MASK              0x7uL

/*********************************************************************
*
*       Manufacturer IDs
*/
#define MFG_ID_SPANSION                 0x01u
#define MFG_ID_MICRON                   0x20u
#define MFG_ID_MICROCHIP                0xBFu
#define MFG_ID_MACRONIX                 0xC2u
#define MFG_ID_WINBOND                  0xEFu
#define MFG_ID_ISSI                     0x9Du
#define MFG_ID_GIGADEVICE               0xC8u
#define MFG_ID_BYT                      0x68u
#define MFG_ID_CYPRESS                  MFG_ID_SPANSION
#define MFG_ID_EON                      0x1Cu
#define MFG_ID_ADESTO                   0x1Fu

/*********************************************************************
*
*       Microchip defines
*/
#define OFF_FIRST_SECTOR_BLOCK          0x4Cu
#define NUM_BYTES_SECTOR_BLOCK          4u
#define NUM_SECTOR_BLOCKS               5u
#define OFF_FIRST_SECTOR_TYPE           0x1Cu
#define NUM_BYTES_SECTOR_TYPE           2u

/*********************************************************************
*
*       Bits for the supported read modes as defined by JEDEC SFDP
*/
#define READ_MODE_112_BIT               0u
#define READ_MODE_122_BIT               4u
#define READ_MODE_144_BIT               5u
#define READ_MODE_114_BIT               6u

/*********************************************************************
*
*       Misc. defines
*/
#define SFDP_MIN_REVISION_SUPPORTED     1u
#define CONFIG_QUAD_BIT                 1     // Spansion specific
#define CONFIG_LATENCY_BIT              6     // Spansion specific
#define CONFIG_LATENCY_MASK             0x3u  // Spansion specific
#define CONFIG_LATENCY_NONE             0x3u  // Spansion specific
#define CONFIG_IOC_BIT                  1     // Microchip specific
#define BAR_EXTADD_BIT                  7     // Spansion specific
#define CONFIG_4BYTE_BIT                5     // Macronix specific
#define STATUS2_ADS_BIT                 0     // GigaDevice specific
#define STATUS2_QE_BIT                  1     // GigaDevice specific
#define STATUS2_D8H_O_BIT               7     // Spansion specific
#define CONFIG2_ADS_BIT                 0     // Cypress specific
#define REG_ADDR_ERROR                  4     // Adesto specific
#define CONFIG_HOLD_BIT                 4     // Micron specific
#define NUM_BYTES_DUMMY_OPI             4u    // Macronix specific
#define NUM_BYTES_ADDR_OPI              4u    // Macronix specific
#define NUM_BYTES_CMD_OPI               2u    // Macronix specific
#define NUM_BYTES_ADDR_SFDP             3u
#define NUM_BYTES_DUMMY_SFDP            1u
#define MAX_NUM_BYTES_ADDR              4u
#define NUM_CYCLES_DUMMY_DEFAULT        20u   // Macronix specific

/*********************************************************************
*
*       ASSERT_IS_NUM_BYTES_EVEN
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_IS_NUM_BYTES_EVEN(NumBytes)                                      \
    if (((NumBytes) & 1u) != 0u) {                                                \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR_SPI: Invalid number of bytes.")); \
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);                                        \
    }
#else
  #define ASSERT_IS_NUM_BYTES_EVEN(Unit)
#endif

/*********************************************************************
*
*      Local data types
*
**********************************************************************
*/

/*********************************************************************
*
*      DEVICE_INFO
*/
typedef struct {
  U8 Id;                  // 3rd byte in the response to READ ID command
  U8 ldBytesPerSector;    // Number of bytes in a physical sector (as power of 2)
  U8 ldNumSectors;        // Number of physical sectors on the NOR flash (as power of 2)
  U8 NumBytesAddr;        // Number of address bytes for the read, program and erase operations (3 for NOR flashes <= 128 Mbit, else 4)
} DEVICE_INFO;

/*********************************************************************
*
*      Static const data
*
**********************************************************************
*/

/*********************************************************************
*
*      _aDeviceInfo
*
*  This table contains parameters of NOR flash devices identified by id.
*/
static const DEVICE_INFO _aDeviceInfo[] = {
//   Id, ldBytesPerSector, ldNumSectors, NumBytesAddr
  {0x11,               15,            2,            3},   //   1MBit
  {0x12,               16,            2,            3},   //   2MBit
  {0x13,               16,            3,            3},   //   4MBit
  {0x14,               16,            4,            3},   //   8MBit
  {0x15,               16,            5,            3},   //  16MBit
  {0x16,               16,            6,            3},   //  32MBit
  {0x17,               16,            7,            3},   //  64MBit
  {0x18,               18,            6,            3},   // 128MBit
  {0x19,               16,            9,            4},   // 256MBit
  {0x1A,               16,           10,            4},   // 512MBit
  {0x00,                0,            0,            0}    // end-of-list
};

static const U8 _abDummyCycles[] = {20, 18, 16, 14, 12, 10, 8, 6};

/*********************************************************************
*
*      Static code
*
**********************************************************************
*/

//lint -efunc(818, _Init) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory] N:104. Rationale: this is an internal API function.
//lint -efunc(818, _Init_x2) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory] N:104. Rationale: this is an internal API function.
//lint -efunc(818, _SPANSION_SetBusWidth) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory] N:104. Rationale: this is an internal API function.
//lint -efunc(818, _MICRON_SetBusWidth) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory] N:104. Rationale: this is an internal API function.
//lint -efunc(818, _MICROCHIP_SetBusWidth) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory] N:104. Rationale: this is an internal API function.
//lint -efunc(818, _WINBOND_SetBusWidth) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory] N:104. Rationale: this is an internal API function.
//lint -efunc(818, _ISSI_SetBusWidth) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory] N:104. Rationale: this is an internal API function.
//lint -efunc(818, _MACRONIX_SetBusWidth) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory] N:104. Rationale: this is an internal API function.
//lint -efunc(818, _GIGADEVICE_SetBusWidth) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory] N:104. Rationale: this is an internal API function.
//lint -efunc(818, _GIGADEVICE_SetBusWidthLowVoltage) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory] N:104. Rationale: this is an internal API function.
//lint -efunc(818, _CYPRESS_SetBusWidth) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory] N:104. Rationale: this is an internal API function.
//lint -efunc(818, _ADESTO_SetBusWidth) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory] N:104. Rationale: this is an internal API function.

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
*       _ClearBits
*
*  Function description
*    Sets to 0 the specified number of bits in the bit field.
*    The bit with index 0 is the bit 0 in the *(pData + NumBytesAvailable - 1) byte.
*
*  Parameters
*    pData              Array of bits.
*    FirstBit           Index of the first bit to be cleared.
*    LastBit            Index of the last bit to be cleared. Has to be >= FirstBit.
*    NumBytesAvailable  Number of bytes in pData.
*/
static void _ClearBits(U8 * pData, unsigned FirstBit, unsigned LastBit, unsigned NumBytesAvailable) {
  unsigned Off;
  unsigned OffLast;
  unsigned OffBit;
  unsigned OffBitLast;
  unsigned NumBitsRem;
  unsigned Mask;
  unsigned Data;
  unsigned NumBitsAtOnce;

  Off        = FirstBit / 8u;
  OffBit     = FirstBit & 7u;
  NumBitsRem = (FirstBit - LastBit) + 1u;
  OffLast    = LastBit / 8u;
  OffBitLast = LastBit & 7u;
  //
  // The bit with the offset 0 is the bit 0 of the last byte
  // (that is the byte at *(pData + NumBytesAvailable - 1)).
  //
  Off        = (NumBytesAvailable - 1u) - Off;
  OffLast    = (NumBytesAvailable - 1u) - OffLast;
  do {
    //
    // Create the mask of the bits that have to be set to 0
    // in the current byte.
    //
    Mask            = 0xFF;
    Mask          >>= 7u - OffBit;
    NumBitsAtOnce   = OffBit + 1u;
    OffBit          = 7;
    //
    // Take care of the bits in the last byte.
    //
    if ((Off == OffLast) && (NumBitsRem < 8u)) {
      Mask          >>= OffBitLast;
      Mask          <<= OffBitLast;
      NumBitsAtOnce  -= OffBitLast;
    }
    //
    // Clear the bits in the array.
    //
    Data        = pData[Off];
    Data       &= ~Mask;
    pData[Off]  = (U8)Data;
    ++Off;
    NumBitsRem -= NumBitsAtOnce;
  }
  while (NumBitsRem != 0u);
}

/*********************************************************************
*
*      _CalcDeviceCapacity
*
*  Function description
*    Calculates the capacity of the NOR device in KBytes.
*/
static U32 _CalcDeviceCapacity(const FS_NOR_SPI_INST * pInst) {
  const FS_NOR_SPI_SECTOR_BLOCK * pSectorBlock;
  U8                              NumSectorBlocks;
  U8                              iBlock;
  U8                              ldBytesPerSector;
  U32                             NumSectors;
  U32                             NumKBytes;

  NumKBytes       = 0;
  NumSectorBlocks = pInst->NumSectorBlocks;
  pSectorBlock    = pInst->aSectorBlock;
  for (iBlock = 0; iBlock < NumSectorBlocks; ++iBlock) {
    NumSectors        = pSectorBlock->NumSectors;
    ldBytesPerSector  = pSectorBlock->ldBytesPerSector;
    ldBytesPerSector -= 10u;          // Convert to KBytes;
    NumKBytes += NumSectors << ldBytesPerSector;
    ++pSectorBlock;
  }
  return NumKBytes;
}

/*********************************************************************
*
*      _Control
*
*  Function description
*    Sends a command to NOR flash.
*
*  Parameters
*    pInst          Driver instance.
*    Cmd            Command code.
*    BusWidth       Number of data lines to be used (encoded)
*
*  Return value
*    ==0      OK, command sent successfully.
*    !=0      An error occurred.
*
*  Additional information
*    The command has no parameters and it does not transfer any data.
*/
static int _Control(const FS_NOR_SPI_INST * pInst, U8 Cmd, unsigned BusWidth) {
  void * pContext;
  int    r;

  pContext = pInst->pContext;
  r = pInst->pCmd->pfControl(pContext, Cmd, BusWidth);
  return r;
}

/*********************************************************************
*
*      _Write
*
*  Function description
*    Sends a command that transfers data from MCU to NOR flash device.
*
*  Parameters
*    pInst          Driver instance.
*    Cmd            Command code.
*    pData          [IN] Data to be transferred to NOR flash device.
*    NumBytes       Number of bytes to be transferred.
*    BusWidth       Number of data lines to be used for the data transfer.
*
*  Return value
*    ==0      OK, data transferred successfully.
*    !=0      An error occurred.
*/
static int _Write(const FS_NOR_SPI_INST * pInst, U8 Cmd, const U8 * pData, unsigned NumBytes, unsigned BusWidth) {
  void * pContext;
  int    r;

  pContext = pInst->pContext;
  r = pInst->pCmd->pfWrite(pContext, Cmd, pData, NumBytes, BusWidth);
  return r;
}

/*********************************************************************
*
*      _Read
*
*  Function description
*    Sends a command that transfers data from NOR flash device to MCU.
*
*  Parameters
*    pInst          Driver instance.
*    Cmd            Command code.
*    pData          [OUT] Data transferred form NOR flash device.
*    NumBytes       Number of bytes transferred.
*    BusWidth       Number of data lines to be used for the data transfer.
*
*  Return value
*    ==0      OK, data transferred successfully.
*    !=0      An error occurred.
*/
static int _Read(const FS_NOR_SPI_INST * pInst, U8 Cmd, U8 * pData, unsigned NumBytes, unsigned BusWidth) {
  void * pContext;
  int    r;

  pContext = pInst->pContext;
  r = pInst->pCmd->pfRead(pContext, Cmd, pData, NumBytes, BusWidth);
  return r;
}

/*********************************************************************
*
*      _WriteWithAddr
*
*  Function description
*    Sends a command that transfers data from MCU to NOR flash device.
*
*  Parameters
*    pInst          Driver instance.
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
*
*  Additional information
*    _WriteWithAddr() performs the same operation as _Write() with the
*    difference that it is able to send an address and optional dummy bytes.
*/
static int _WriteWithAddr(const FS_NOR_SPI_INST * pInst, U8 Cmd, const U8 * pPara, unsigned NumBytesPara, unsigned NumBytesAddr, const U8 * pData, unsigned NumBytesData, unsigned BusWidth) {
  void * pContext;
  int    r;

  pContext = pInst->pContext;
  r = pInst->pCmd->pfWriteWithAddr(pContext, Cmd, pPara, NumBytesPara, NumBytesAddr, pData, NumBytesData, BusWidth);
  return r;
}

/*********************************************************************
*
*      _ReadWithAddr
*
*  Function description
*    Sends a command that transfers data from NOR flash device to MCU.
*
*  Parameters
*    pInst          Driver instance.
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
*
*  Additional information
*    _ReadWithAddr() performs the same operation as _Read() with the
*    difference that it is able to send an address and optional dummy bytes.
*/
static int _ReadWithAddr(const FS_NOR_SPI_INST * pInst, U8 Cmd, const U8 * pPara, unsigned NumBytesPara, unsigned NumBytesAddr, U8 * pData, unsigned NumBytesData, unsigned BusWidth) {
  void * pContext;
  int    r;

  pContext = pInst->pContext;
  r = pInst->pCmd->pfReadWithAddr(pContext, Cmd, pPara, NumBytesPara, NumBytesAddr, pData, NumBytesData, BusWidth);
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
static int _Poll(const FS_NOR_SPI_INST * pInst, U8 Cmd, U8 BitPos, U8 BitValue, U32 Delay, U32 TimeOut_ms, unsigned BusWidth) {
  int    r;
  void * pContext;

  r        = -1;                    // Set to indicate that the feature is not supported.
  pContext = pInst->pContext;
  if (pInst->pCmd->pfPoll != NULL) {
    r = pInst->pCmd->pfPoll(pContext, Cmd, BitPos, BitValue, Delay, TimeOut_ms, (U16)BusWidth);
  }
  return r;
}

/*********************************************************************
*
*      _Delay
*
*  Function description
*    Blocks the execution for a specified number of milliseconds.
*
*  Parameters
*    pInst          Driver instance.
*    ms             Number of milliseconds to block the execution.
*
*  Return value
*    ==0    OK, delay executed.
*    < 0    Functionality not supported.
*/
static int _Delay(const FS_NOR_SPI_INST * pInst, unsigned ms) {
  void * pContext;
  int    r;

  r = -1;                           // Set to indicate that the feature is not supported.
  pContext = pInst->pContext;
  if (pInst->pCmd->pfDelay != NULL) {
    r = pInst->pCmd->pfDelay(pContext, ms);
  }
  return r;
}

/*********************************************************************
*
*      _ControlWithCmdEx
*
*  Function description
*    Sends a command to NOR flash.
*
*  Parameters
*    pInst          Driver instance.
*    pCmd           [IN] Command code.
*    NumBytesCmd    Number of bytes in the command code.
*    BusWidth       Number of data lines to be used (encoded)
*    Flags          Command options.
*
*  Return value
*    ==0      OK, command sent successfully.
*    !=0      An error occurred.
*
*  Additional information
*    _ControlWithCmdEx() performs the same operation as _Control() with the
*    difference that _ControlWithCmdEx() is able to send multi-byte commands.
*    The executed command has no parameters and it does not transfer any data.
*/
static int _ControlWithCmdEx(const FS_NOR_SPI_INST * pInst, const U8 * pCmd, unsigned NumBytesCmd, unsigned BusWidth, unsigned Flags) {
  void * pContext;
  int    r;

  pContext = pInst->pContext;
  r = pInst->pCmd->pfControlWithCmdEx(pContext, pCmd, NumBytesCmd, BusWidth, Flags);
  return r;
}

/*********************************************************************
*
*      _WriteWithCmdEx
*
*  Function description
*    Sends a command that transfers data from MCU to NOR flash device.
*
*  Parameters
*    pInst          Driver instance.
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
*
*  Additional information
*    _WriteWithCmdEx() performs the same operation as _Write() with the
*    difference that _WriteWithCmdEx() is able to send multi-byte commands.
*/
static int _WriteWithCmdEx(const FS_NOR_SPI_INST * pInst, const U8 * pCmd, unsigned NumBytesCmd, const U8 * pData, unsigned NumBytesData, unsigned BusWidth, unsigned Flags) {
  void * pContext;
  int    r;

  r        = 1;                   // Set to indicate error.
  pContext = pInst->pContext;
  if (pInst->pCmd->pfWriteWithCmdEx != NULL) {
    r = pInst->pCmd->pfWriteWithCmdEx(pContext, pCmd, NumBytesCmd, pData, NumBytesData, BusWidth, Flags);
  }
  return r;
}

/*********************************************************************
*
*      _ReadWithCmdExAndAddr
*
*  Function description
*    Sends a command that transfers data from NOR flash device to MCU.
*
*  Parameters
*    pInst          Driver instance.
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
*
*  Additional information
*    _ReadWithCmdExAndAddr() performs the same operation as _ReadWithAddr() with the
*    difference that _ReadWithCmdExAndAddr() is able to send multi-byte commands.
*/
static int _ReadWithCmdExAndAddr(const FS_NOR_SPI_INST * pInst, const U8 * pCmd, unsigned NumBytesCmd, const U8 * pPara, unsigned NumBytesPara, unsigned NumBytesAddr, U8 * pData, unsigned NumBytesData, unsigned BusWidth, unsigned Flags) {
  void * pContext;
  int    r;

  r        = 1;                   // Set to indicate error.
  pContext = pInst->pContext;
  if (pInst->pCmd->pfReadWithCmdExAndAddr != NULL) {
    r = pInst->pCmd->pfReadWithCmdExAndAddr(pContext, pCmd, NumBytesCmd, pPara, NumBytesPara, NumBytesAddr, pData, NumBytesData, (U16)BusWidth, Flags);
  }
  return r;
}

/*********************************************************************
*
*      _WriteWithCmdExAndAddr
*
*  Function description
*    Sends a command that transfers data from MCU to NOR flash device.
*
*  Parameters
*    pInst          Driver instance.
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
*
*  Additional information
*    _WriteWithCmdExAndAddr() performs the same operation as _WriteWithAddr() with the
*    difference that _WriteWithCmdExAndAddr() is able to send multi-byte commands.
*/
static int _WriteWithCmdExAndAddr(const FS_NOR_SPI_INST * pInst, const U8 * pCmd, unsigned NumBytesCmd, const U8 * pPara, unsigned NumBytesPara, unsigned NumBytesAddr, const U8 * pData, unsigned NumBytesData, unsigned BusWidth, unsigned Flags) {
  void * pContext;
  int    r;

  r        = 1;                   // Set to indicate error.
  pContext = pInst->pContext;
  if (pInst->pCmd->pfWriteWithCmdExAndAddr != NULL) {
    r = pInst->pCmd->pfWriteWithCmdExAndAddr(pContext, pCmd, NumBytesCmd, pPara, NumBytesPara, NumBytesAddr, pData, NumBytesData, BusWidth, Flags);
  }
  return r;
}

/*********************************************************************
*
*      _PollWithCmdEx
*
*  Function description
*    Checks periodically the value of a status flag.
*
*  Parameters
*    pInst          Driver instance.
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
*
*  Additional information
*    _PollWithCmdEx() performs the same operation as _Poll() with the
*    difference that _PollWithCmdEx() is able to send multi-byte commands.
*/
static int _PollWithCmdEx(const FS_NOR_SPI_INST * pInst, const U8 * pCmd, unsigned NumBytesCmd, const U8 * pPara, unsigned NumBytesPara, unsigned NumBytesAddr, U8 BitPos, U8 BitValue, U32 Delay, U32 TimeOut_ms, unsigned BusWidth, unsigned Flags) {
  int    r;
  void * pContext;

  r        = -1;                    // Set to indicate that the feature is not supported.
  pContext = pInst->pContext;
  if (pInst->pCmd->pfPollWithCmdEx != NULL) {
    r = pInst->pCmd->pfPollWithCmdEx(pContext, pCmd, NumBytesCmd, pPara, NumBytesPara, NumBytesAddr, BitPos, BitValue, Delay, TimeOut_ms, BusWidth, Flags);
  }
  return r;
}

/*********************************************************************
*
*       _DelayPoll
*/
static U32 _DelayPoll(const FS_NOR_SPI_INST * pInst, U32 TimeOut, const FS_NOR_SPI_POLL_PARA * pPollPara) {
  U32 Delay;
  U16 Delay_ms;
  int r;

  Delay    = pPollPara->Delay;
  Delay_ms = pPollPara->Delay_ms;
  if (Delay != 0u) {
    r = _Delay(pInst, Delay_ms);
    if (r == 0) {
      if (TimeOut > Delay) {
        TimeOut -= Delay;
      } else {
        TimeOut = 0;
      }
    }
  }
  return TimeOut;
}

/*********************************************************************
*
*      _ReadId
*
*  Function description
*    Reads the id information from the serial NOR flash.
*
*  Parameters
*    pInst      Driver instance.
*    pData      [OUT] Read id information.
*    NumBytes   Number of bytes to read.
*/
static void _ReadId(const FS_NOR_SPI_INST * pInst, U8 * pData, U32 NumBytes) {
  unsigned BusWidth;

  BusWidth = pInst->BusWidth;
  (void)_Read(pInst, CMD_RDID, pData, NumBytes, BusWidth);
}

/*********************************************************************
*
*      _ReadStatusRegister
*
*  Function description
*    Returns the contents of the Status Register (all manufacturers).
*
*  Parameters
*    pInst        Driver instance.
*
*  Return value
*    Value of status register.
*/
static unsigned _ReadStatusRegister(const FS_NOR_SPI_INST * pInst) {
  U8       Data;
  unsigned BusWidth;

  Data     = 0;
  BusWidth = pInst->BusWidth;
  (void)_Read(pInst, CMD_RDSR, &Data, sizeof(Data), BusWidth);
  return Data;
}

/*********************************************************************
*
*      _ReadStatusRegister_x2
*
*  Function description
*    Returns the contents of the Status Register (all manufacturers).
*
*  Parameters
*    pInst        Driver instance.
*    pStatus      [OUT]Value of status register.
*
*  Additional information
*    This function performs the same operation as _ReadStatusRegister()
*    with the difference that it returns the status from two NOR flash
*    devices. pStatus has to be at least two bytes large.
*/
static void _ReadStatusRegister_x2(const FS_NOR_SPI_INST * pInst, U8 * pStatus) {
  unsigned BusWidth;

  BusWidth = pInst->BusWidth;
  (void)_Read(pInst, CMD_RDSR, pStatus, 2, BusWidth);       // 2 because the status is returned by two NOR flash devices.
}

/*********************************************************************
*
*      _ReadStatusRegisterCEI
*
*  Function description
*    Returns the contents of the Status Register.
*
*  Parameters
*    pInst        Driver instance.
*    pValue       [OUT] Value of the status register.
*
*  Return value
*    ==0      Value of status register.
*    !=0      An error occurred.
*
*  Additional information
*    This function performs the same operation as _ReadStatusRegister()
*    with the difference that the command is sent as a two byte sequence
*    with the second byte set to the negated value of the first
*    (Command Extension Inverted).
*
*    This function is used for Macronix devices that support octal mode.
*/
static int _ReadStatusRegisterCEI(const FS_NOR_SPI_INST * pInst, unsigned * pValue) {
  U8       Data;
  unsigned BusWidth;
  U8       abPara[NUM_BYTES_ADDR_OPI + NUM_BYTES_DUMMY_OPI * 2u];     // *2 because in DTR mode a byte is exchanged on each clock edge.
  U8       abCmd[NUM_BYTES_CMD_OPI];
  unsigned Cmd;
  unsigned Flags;
  unsigned NumBytes;
  int      r;

  FS_MEMSET(abPara, 0, sizeof(abPara));
  Data     = 0;
  BusWidth = pInst->BusWidth;
  Flags    = pInst->Flags;
  Cmd      = CMD_RDSR;
  abCmd[0] = (U8)Cmd;
  abCmd[1] = (U8)(~Cmd);
  //
  // Calculate the number of parameter bytes to send.
  // In DTR mode we have to send two times more dummy bytes.
  //
  NumBytes = NUM_BYTES_ADDR_OPI + NUM_BYTES_DUMMY_OPI;
  if ((Flags & FS_NOR_HW_FLAG_DTR_ADDR) != 0u) {
    NumBytes += NUM_BYTES_DUMMY_OPI;
  }
  //
  // Read the register value.
  //
  r = _ReadWithCmdExAndAddr(pInst, abCmd, sizeof(abCmd), abPara, NumBytes, NUM_BYTES_ADDR_OPI, &Data, sizeof(Data), BusWidth, Flags);
  if (pValue != NULL) {
    *pValue = Data;
  }
  return r;
}

/*********************************************************************
*
*      _PollStatusRegister
*
*  Function description
*    Polls the status register until the specified bit is cleared.
*
*  Parameters
*    pInst        Driver instance.
*    BitPos       Location of the bit in the status register to be checked.
*    pPollPara    [IN] Parameters of the polling operation.
*
*  Return value
*    > 0      Timeout occurred.
*    ==0      OK, bit set to specified value.
*    < 0      Feature not supported.
*/
static int _PollStatusRegister(const FS_NOR_SPI_INST * pInst, U8 BitPos, const FS_NOR_SPI_POLL_PARA * pPollPara) {
  unsigned BusWidth;
  int      r;
  U32      TimeOut_ms;
  U32      Delay;

  TimeOut_ms = pPollPara->TimeOut_ms;
  Delay      = pPollPara->Delay;
  BusWidth   = pInst->BusWidth;
  r = _Poll(pInst, CMD_RDSR, BitPos, 0, Delay, TimeOut_ms, BusWidth);
  return r;
}

/*********************************************************************
*
*      _PollStatusRegisterCEI
*
*  Function description
*    Polls the status register until the specified bit is cleared.
*
*  Parameters
*    pInst        Driver instance.
*    BitPos       Location of the bit in the status register to be checked.
*    pPollPara    [IN] Parameters of the polling operation.
*
*  Return value
*    > 0      Timeout occurred.
*    ==0      OK, bit set to specified value.
*    < 0      Feature not supported.
*/
static int _PollStatusRegisterCEI(const FS_NOR_SPI_INST * pInst, U8 BitPos, const FS_NOR_SPI_POLL_PARA * pPollPara) {
  unsigned BusWidth;
  int      r;
  U32      TimeOut_ms;
  U32      Delay;
  U8       abPara[NUM_BYTES_ADDR_OPI + NUM_BYTES_DUMMY_OPI * 2u];     // *2 because in DTR mode a bytes is exchanged on each clock edge.
  U8       abCmd[2];
  unsigned Cmd;
  unsigned Flags;
  unsigned NumBytes;

  FS_MEMSET(abPara, 0, sizeof(abPara));
  BusWidth   = pInst->BusWidth;
  Flags      = pInst->Flags;
  Cmd        = CMD_RDSR;
  abCmd[0]   = (U8)Cmd;
  abCmd[1]   = (U8)(~Cmd);
  TimeOut_ms = pPollPara->TimeOut_ms;
  Delay      = pPollPara->Delay;
  //
  // Calculate the number of parameter bytes to send.
  // In DTR mode we have to send two times more dummy bytes.
  //
  NumBytes = NUM_BYTES_ADDR_OPI + NUM_BYTES_DUMMY_OPI;
  if ((Flags & FS_NOR_HW_FLAG_DTR_ADDR) != 0u) {
    NumBytes += NUM_BYTES_DUMMY_OPI;
  }
  //
  // Wait for the NOR flash device to become ready.
  //
  r = _PollWithCmdEx(pInst, abCmd, sizeof(abCmd), abPara, NumBytes, NUM_BYTES_ADDR_OPI, BitPos, 0, Delay, TimeOut_ms, BusWidth, Flags);
  return r;
}

/*********************************************************************
*
*      _ReadStatusRegister2
*
*  Function description
*    Returns the contents of the second status register.
*
*  Parameters
*    pInst        Driver instance.
*
*  Return value
*    Value of status register.
*
*  Additional information
*    This function is used only for Spansion serial NOR flash devices.
*/
static U8 _ReadStatusRegister2(const FS_NOR_SPI_INST * pInst) {
  U8       Status;
  unsigned BusWidth;

  BusWidth = pInst->BusWidth;
  (void)_Read(pInst, CMD_RDSR2, &Status, sizeof(Status), BusWidth);
  return Status;
}

/*********************************************************************
*
*      _ReadStatusRegister2Alt
*
*  Function description
*    Returns the contents of the second status register (GigaDevice and Winbond only).
*/
static U8 _ReadStatusRegister2Alt(const FS_NOR_SPI_INST * pInst) {
  U8       Status;
  unsigned BusWidth;

  BusWidth = pInst->BusWidth;
  (void)_Read(pInst, CMD_RDSR2_ALT, &Status, sizeof(Status), BusWidth);
  return Status;
}

/*********************************************************************
*
*      _ReadStatusRegister3
*
*  Function description
*    Returns the contents of the second status register 3 (GigaDevice only).
*/
static U8 _ReadStatusRegister3(const FS_NOR_SPI_INST * pInst) {
  U8       Status;
  unsigned BusWidth;

  BusWidth = pInst->BusWidth;
  (void)_Read(pInst, CMD_RDSR3, &Status, sizeof(Status), BusWidth);
  return Status;
}

/*********************************************************************
*
*      _ReadFlagStatusRegister
*
*  Function description
*    Returns the contents of the status register (Micron only).
*/
static U8 _ReadFlagStatusRegister(const FS_NOR_SPI_INST * pInst) {
  U8       Status;
  unsigned BusWidth;

  BusWidth = pInst->BusWidth;
  (void)_Read(pInst, CMD_RFSR, &Status, sizeof(Status), BusWidth);
  return Status;
}

/*********************************************************************
*
*      _ReadFlagStatusRegister_x2
*
*  Function description
*    Returns the contents of the status register of both NOR flash
*    devices connected in parallel (Micron only).
*/
static void _ReadFlagStatusRegister_x2(const FS_NOR_SPI_INST * pInst, U8 * pStatus) {
  unsigned BusWidth;

  BusWidth = pInst->BusWidth;
  (void)_Read(pInst, CMD_RFSR, pStatus, 2, BusWidth);     // Each NOR flash device returns one byte of status
}

/*********************************************************************
*
*      _PollFlagStatusRegister
*
*  Function description
*    Polls the status register until the specified bit is set or cleared (Micron only).
*/
static int _PollFlagStatusRegister(const FS_NOR_SPI_INST * pInst, U8 BitPos, const FS_NOR_SPI_POLL_PARA * pPollPara) {
  unsigned BusWidth;
  int      r;
  U32      TimeOut_ms;
  U32      Delay;

  TimeOut_ms = pPollPara->TimeOut_ms;
  Delay      = pPollPara->Delay;
  BusWidth   = pInst->BusWidth;
  r = _Poll(pInst, CMD_RFSR, BitPos, 1, Delay, TimeOut_ms, BusWidth);
  return r;
}

/*********************************************************************
*
*      _ReadExtendedReadRegister
*
*  Function description
*    Returns the contents of the Extended Read Register (ISSI specific).
*/
static U8 _ReadExtendedReadRegister(const FS_NOR_SPI_INST * pInst) {
  U8       Status;
  unsigned BusWidth;

  BusWidth = pInst->BusWidth;
  (void)_Read(pInst, CMD_RDERP, &Status, sizeof(Status), BusWidth);
  return Status;
}

/*********************************************************************
*
*      _ClearExtendedReadRegister
*
*  Function description
*    Clears the error flags in the Extended Read Register (ISSI specific).
*/
static int _ClearExtendedReadRegister(const FS_NOR_SPI_INST * pInst) {
  int      r;
  unsigned BusWidth;

  BusWidth = pInst->BusWidth;
  r = _Control(pInst, CMD_CLERP, BusWidth);
  return r;
}

/*********************************************************************
*
*      _ReadSecurityRegister
*
*  Function description
*    Returns the contents of the Security Register.
*
*  Parameters
*    pInst      Driver instance.
*
*  Return value
*    Register contents.
*
*  Additional information
*    This function is used only for Macronix serial NOR flash devices.
*/
static unsigned _ReadSecurityRegister(const FS_NOR_SPI_INST * pInst) {
  U8       Data;
  unsigned BusWidth;

  Data     = 0;
  BusWidth = pInst->BusWidth;
  (void)_Read(pInst, CMD_RDSCUR, &Data, sizeof(Data), BusWidth);
  return Data;
}

/*********************************************************************
*
*      _ReadSecurityRegisterCEI
*
*  Function description
*    Returns the contents of the Security Register.
*
*  Parameters
*    pInst      Driver instance.
*    pValue    [OUT] Register value.
*
*  Return value
*    ==0      OK, register read successfully.
*    !=0      An error occurred.
*
*  Additional information
*    This function performs the same operation as _ReadSecurityRegister()
*    with the difference that the command is sent as a two byte sequence
*    with the second byte set to the negated value of the first
*    (Command Extension Inverted).
*
*    This function is used for Macronix devices that support octal mode.
*/
static int _ReadSecurityRegisterCEI(const FS_NOR_SPI_INST * pInst, unsigned * pValue) {
  U8       Data;
  unsigned BusWidth;
  U8       abPara[NUM_BYTES_ADDR_OPI + NUM_BYTES_DUMMY_OPI * 2u];     // *2 because in DTR mode a bytes is exchanged on each clock edge.
  U8       abCmd[NUM_BYTES_CMD_OPI];
  unsigned Cmd;
  unsigned Flags;
  unsigned NumBytes;
  int      r;

  FS_MEMSET(abPara, 0, sizeof(abPara));
  Data      = 0;
  BusWidth  = pInst->BusWidth;
  Flags     = pInst->Flags;
  Cmd       = CMD_RDSCUR;
  abCmd[0]  = (U8)Cmd;
  abCmd[1]  = (U8)(~Cmd);
  //
  // Calculate the number of parameter bytes to send.
  // In DTR mode we have to send two times more dummy bytes.
  //
  NumBytes = NUM_BYTES_ADDR_OPI + NUM_BYTES_DUMMY_OPI;
  if ((Flags & FS_NOR_HW_FLAG_DTR_ADDR) != 0u) {
    NumBytes += NUM_BYTES_DUMMY_OPI;
  }
  //
  // Read the register value.
  //
  r = _ReadWithCmdExAndAddr(pInst, abCmd, sizeof(abCmd), abPara, NumBytes, NUM_BYTES_ADDR_OPI, &Data, sizeof(Data), BusWidth, Flags);
  if (pValue != NULL) {
    *pValue = Data;
  }
  return r;
}

/*********************************************************************
*
*      _ReadStatusRegisterIndirect
*
*  Function description
*    Returns the contents of a status register (Adesto only).
*/
static U8 _ReadStatusRegisterIndirect(const FS_NOR_SPI_INST * pInst, unsigned Addr) {
  U8       Status;
  unsigned BusWidth;
  U8       abPara[2];     // One address and one dummy byte.

  Status    = 0;
  BusWidth  = pInst->BusWidth;
  abPara[0] = (U8)Addr;
  abPara[1] = 0xFF;
  (void)_ReadWithAddr(pInst, CMD_RDSRI, abPara, sizeof(abPara), 1, &Status, sizeof(Status), BusWidth);
  return Status;
}

/*********************************************************************
*
*      _WaitForEndOfOperation
*
*  Function description
*    Waits for a NOR flash operation to complete.
*
*  Parameters
*    pInst        Driver instance.
*    pPollPara    [IN] Parameters of the polling operation.
*
*  Return value
*    ==0      OK, operation completed successfully.
*    !=0      An error occurred.
*/
static int _WaitForEndOfOperation(const FS_NOR_SPI_INST * pInst, const FS_NOR_SPI_POLL_PARA * pPollPara) {
  int      r;
  int      rPoll;
  unsigned Status;
  U32      TimeOut;

  r       = 1;                                        // Set to indicate error.
  TimeOut = pPollPara->TimeOut;
  //
  // Prefer polling the status register in the hardware if supported.
  //
  rPoll = _PollStatusRegister(pInst, STATUS_BUSY_BIT, pPollPara);
  if (rPoll > 0) {                                    // Timeout reported?
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _WaitForEndOfOperation: Timeout expired."));
  } else {
    if (rPoll < 0) {                                  // Is the feature not supported?
      //
      // Poll the status register in the software.
      //
      for (;;) {
        //
        // Read the status from NOR device.
        //
        Status = _ReadStatusRegister(pInst);
        if ((Status & (1uL << STATUS_BUSY_BIT)) == 0u) {
          r = 0;                                      // OK, the NOR flash is ready for a new operation.
          break;
        }
        --TimeOut;                                    // We have executed one wait cycle.
        //
        // If possible, delay the next request to reduce CPU usage.
        //
        TimeOut = _DelayPoll(pInst, TimeOut, pPollPara);
        if (TimeOut == 0u) {
          FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _WaitForEndOfOperation: Timeout expired."));
          break;
        }
      }
    } else {
      r = 0;                                          // OK, the NOR flash is ready for a new operation.
    }
  }
  return r;
}

/*********************************************************************
*
*      _WaitForEndOfOperation_x2
*
*  Function description
*    Waits for a NOR flash operation to complete.
*
*  Parameters
*    pInst        Driver instance.
*    pPollPara    [IN] Parameters of the polling operation.
*
*  Return value
*    ==0      OK, operation completed successfully.
*    !=0      An error occurred.
*/
static int _WaitForEndOfOperation_x2(const FS_NOR_SPI_INST * pInst, const FS_NOR_SPI_POLL_PARA * pPollPara) {
  int r;
  int rPoll;
  U8  abStatus[2];
  U32 TimeOut;

  r       = 1;            // Set to indicate an error.
  TimeOut = pPollPara->TimeOut;
  //
  // Prefer polling the status register in the hardware if supported.
  //
  rPoll = _PollStatusRegister(pInst, STATUS_BUSY_BIT, pPollPara);
  if (rPoll > 0) {            // Timeout reported?
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _WaitForEndOfOperation_x2: Timeout expired."));
  } else {
    if (rPoll < 0) {          // Is the feature not supported?
      //
      // Poll the status register in the software.
      //
      for (;;) {
        //
        // Read the status from NOR device.
        //
        FS_MEMSET(abStatus, 0, sizeof(abStatus));
        _ReadStatusRegister_x2(pInst, abStatus);
        if (   ((abStatus[0] & (1u << STATUS_BUSY_BIT)) == 0u)
            && ((abStatus[1] & (1u << STATUS_BUSY_BIT)) == 0u)) {
          r = 0;                                      // OK, the NOR flash is ready for a new operation.
          break;
        }
        --TimeOut;                                    // We have executed one wait cycle.
        //
        // If possible, delay the next request to reduce CPU usage.
        //
        TimeOut = _DelayPoll(pInst, TimeOut, pPollPara);
        if (TimeOut == 0u) {
          FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _WaitForEndOfOperation_x2: Timeout expired."));
          break;
        }
      }
    } else {
      r = 0;                                          // OK, the NOR flash is ready for a new operation.
    }
  }
  return r;
}

/*********************************************************************
*
*      _EnableWrite
*
*  Function description
*    Sets the write enable latch (WEL) bit in the status register of the NOR flash device.
*
*  Parameters
*    pInst        Driver instance.
*
*  Return value
*    ==0      OK, write operation enabled.
*    !=0      An error occurred.
*
*  Additional information
*    Commands that modify the contents of the NOR flash device(write page, sector erase, etc.)
*    are accepted only when the WEL bit is set. The bit is reset automatically
*    by the NOR flash at the end of operation.
*/
static int _EnableWrite(const FS_NOR_SPI_INST * pInst) {
  int      r;
  unsigned Status;
  U32      TimeOut;
  unsigned BusWidth;

  TimeOut  = pInst->PollParaRegWrite.TimeOut;
  BusWidth = pInst->BusWidth;
  for (;;) {
    r = _Control(pInst, CMD_WREN, BusWidth);
    if (r == 0) {
      Status = _ReadStatusRegister(pInst);
      if ((Status & (1uL << STATUS_WEL_BIT)) != 0u) {
        r = 0;                      // OK, the NOR flash is ready for the write operation.
        break;
      }
    }
    if (--TimeOut == 0u) {
      r = 1;                        // Error, could not enable the write operation.
      break;
    }
  }
  return r;
}

/*********************************************************************
*
*      _EnableWrite_x2
*
*  Function description
*    Sets the write enable latch (WEL) bit in the status register of the NOR flash device.
*
*  Parameters
*    pInst        Driver instance.
*
*  Return value
*    ==0      OK, write operation enabled.
*    !=0      An error occurred.
*
*  Additional information
*    Commands that modify the contents of the NOR flash device(write page, sector erase, etc.)
*    are accepted only when the WEL bit is set. The bit is reset automatically
*    by the NOR flash at the end of operation.
*    This function performs the same operation as _EnableWrite() with the
*    exception that the command is sent to two NOR flash devices at the same time.
*/
static int _EnableWrite_x2(const FS_NOR_SPI_INST * pInst) {
  int      r;
  U8       abStatus[2];
  U32      TimeOut;
  unsigned BusWidth;

  TimeOut  = pInst->PollParaRegWrite.TimeOut;
  BusWidth = pInst->BusWidth;
  for (;;) {
    r = _Control(pInst, CMD_WREN, BusWidth);
    if (r == 0) {
      _ReadStatusRegister_x2(pInst, abStatus);
      if (   ((abStatus[0] & (1u << STATUS_WEL_BIT)) != 0u)
          && ((abStatus[1] & (1u << STATUS_WEL_BIT)) != 0u)) {
        r = 0;                      // OK, the NOR flash is ready for the write operation.
        break;
      }
    }
    if (--TimeOut == 0u) {
      r = 1;                        // Error, could not enable the write operation.
      break;
    }
  }
  return r;
}

/*********************************************************************
*
*      _EnableWriteCEI
*
*  Function description
*    Sets the write enable latch (WEL) bit in the status register of the NOR flash device.
*
*  Parameters
*    pInst        Driver instance.
*
*  Return value
*    ==0      OK, write operation enabled.
*    !=0      An error occurred.
*
*  Additional information
*    Commands that modify the contents of the NOR flash device(write page, sector erase, etc.)
*    are accepted only when the WEL bit is set. The bit is reset automatically
*    by the NOR flash at the end of operation.
*
*    This function performs the same operation as _EnableWrite() with the
*    exception that the command code is composed of two bytes with the second
*    byte set to the negated value of the first (Command Extension Inverted).
*
*    This function is used for Macronix devices that support octal mode.
*/
static int _EnableWriteCEI(const FS_NOR_SPI_INST * pInst) {
  int      r;
  unsigned Status;
  U32      TimeOut;
  unsigned BusWidth;
  unsigned Cmd;
  U8       abCmd[NUM_BYTES_CMD_OPI];
  unsigned Flags;

  Cmd      = CMD_WREN;
  abCmd[0] = (U8)Cmd;
  abCmd[1] = (U8)(~Cmd);
  TimeOut  = pInst->PollParaRegWrite.TimeOut;
  BusWidth = pInst->BusWidth;
  Flags    = pInst->Flags;
  for (;;) {
    r = _ControlWithCmdEx(pInst, abCmd, sizeof(abCmd), BusWidth, Flags);
    if (r != 0) {
      break;                        // Error, could not send command.
    }
    Status = 0;
    r = _ReadStatusRegisterCEI(pInst, &Status);
    if (r != 0) {
      break;                        // Error, could not read status.
    }
    if ((Status & (1uL << STATUS_WEL_BIT)) != 0u) {
      r = 0;                        // OK, the NOR flash device is ready for the write operation.
      break;
    }
    if (--TimeOut == 0u) {
      r = 1;                        // Error, could not enable the write operation.
      break;
    }
  }
  return r;
}

/*********************************************************************
*
*      _DisableWrite
*
*  Function description
*    Clears the write enable latch (WEL) bit in the status register of the NOR flash.
*/
static int _DisableWrite(const FS_NOR_SPI_INST * pInst) {
  int      r;
  unsigned Status;
  U32      TimeOut;
  unsigned BusWidth;

  TimeOut  = pInst->PollParaRegWrite.TimeOut;
  BusWidth = pInst->BusWidth;
  for (;;) {
    r = _Control(pInst, CMD_WRDIS, BusWidth);
    if (r == 0) {
      Status = _ReadStatusRegister(pInst);
      if ((Status & (1uL << STATUS_WEL_BIT)) == 0u) {
        r = 0;                      // OK, the NOR flash is ready for the write operation.
        break;
      }
    }
    if (--TimeOut == 0u) {
      r = 1;                        // Error, could not enable the write operation.
      break;
    }
  }
  return r;
}

/*********************************************************************
*
*      _DisableWrite_x2
*
*  Function description
*    Clears the write enable latch (WEL) bit in the status register of the NOR flash.
*/
static int _DisableWrite_x2(const FS_NOR_SPI_INST * pInst) {
  int      r;
  U8       abStatus[2];
  U32      TimeOut;
  unsigned BusWidth;

  TimeOut  = pInst->PollParaRegWrite.TimeOut;
  BusWidth = pInst->BusWidth;
  for (;;) {
    r = _Control(pInst, CMD_WRDIS, BusWidth);
    if (r == 0) {
      FS_MEMSET(abStatus, 0, sizeof(abStatus));
      _ReadStatusRegister_x2(pInst, abStatus);
      if (   ((abStatus[0] & (1u << STATUS_WEL_BIT)) == 0u)
          && ((abStatus[1] & (1u << STATUS_WEL_BIT)) == 0u)) {
        r = 0;                      // OK, the NOR flash is ready for the write operation.
        break;
      }
    }
    if (--TimeOut == 0u) {
      r = 1;                        // Error, could not enable the write operation.
      break;
    }
  }
  return r;
}

/*********************************************************************
*
*      _WriteStatusRegister
*
*  Function description
*    Writes a value to status register. Typ. called to remove the write protection of physical blocks.
*/
static int _WriteStatusRegister(const FS_NOR_SPI_INST * pInst, const U8 * pData, unsigned NumBytes) {
  int                          r;
  const FS_NOR_SPI_POLL_PARA * pPollPara;
  unsigned                     BusWidth;

  pPollPara = &pInst->PollParaRegWrite;
  BusWidth  = pInst->BusWidth;
  //
  // The command is accepted only when the NOR flash is in write mode.
  //
  r = _EnableWrite(pInst);
  if (r == 0) {
    r = _Write(pInst, CMD_WRSR, pData, NumBytes, BusWidth);
    if (r == 0) {
      r = _WaitForEndOfOperation(pInst, pPollPara);
    }
  }
  return r;
}

/*********************************************************************
*
*      _WriteStatusRegister_x2
*
*  Function description
*    Writes a value to status register. Typ. called to remove the write protection of physical blocks.
*/
static int _WriteStatusRegister_x2(const FS_NOR_SPI_INST * pInst, const U8 * pData, unsigned NumBytes) {
  int                          r;
  const FS_NOR_SPI_POLL_PARA * pPollPara;
  unsigned                     BusWidth;

  pPollPara = &pInst->PollParaRegWrite;
  BusWidth  = pInst->BusWidth;
  //
  // The command is accepted only when the NOR flash is in write mode.
  //
  r = _EnableWrite_x2(pInst);
  if (r == 0) {
    r = _Write(pInst, CMD_WRSR, pData, NumBytes, BusWidth);
    if (r == 0) {
      r = _WaitForEndOfOperation_x2(pInst, pPollPara);
    }
  }
  return r;
}

/*********************************************************************
*
*      _WriteStatusRegister2
*
*  Function description
*    Writes a value to status register (GigaDevice specific).
*/
static int _WriteStatusRegister2(const FS_NOR_SPI_INST * pInst, const U8 * pData, unsigned NumBytes) {
  int                          r;
  const FS_NOR_SPI_POLL_PARA * pPollPara;
  unsigned                     BusWidth;

  pPollPara = &pInst->PollParaRegWrite;
  BusWidth  = pInst->BusWidth;
  //
  // The command is accepted only when the NOR flash is in write mode.
  //
  r = _EnableWrite(pInst);
  if (r == 0) {
    r = _Write(pInst, CMD_WRSR2, pData, NumBytes, BusWidth);
    if (r == 0) {
      r = _WaitForEndOfOperation(pInst, pPollPara);
    }
  }
  return r;
}

/*********************************************************************
*
*      _ClearStatusRegister
*
*  Function description
*    Clears the error flags in the status register (Spansion only).
*/
static int _ClearStatusRegister(const FS_NOR_SPI_INST * pInst) {
  int      r;
  unsigned BusWidth;

  BusWidth = pInst->BusWidth;
  r = _Control(pInst, CMD_CLSR, BusWidth);
  return r;
}

/*********************************************************************
*
*      _ClearFlagStatusRegister
*
*  Function description
*    Clears the error flags in the flag status register (Micron only).
*/
static int _ClearFlagStatusRegister(const FS_NOR_SPI_INST * pInst) {
  int      r;
  unsigned BusWidth;

  BusWidth = pInst->BusWidth;
  r = _Control(pInst, CMD_CFSR, BusWidth);
  return r;
}

/*********************************************************************
*
*      _ReadConfigRegister
*
*  Function description
*    Returns the contents of the configuration register.
*    Present only in newer Spansion devices.
*/
static U8 _ReadConfigRegister(const FS_NOR_SPI_INST * pInst) {
  U8       Config;
  unsigned BusWidth;

  BusWidth = pInst->BusWidth;
  (void)_Read(pInst, CMD_RDCR, &Config, sizeof(Config), BusWidth);
  return Config;
}

/*********************************************************************
*
*      _ReadConfigRegisterAlt
*
*  Function description
*    Returns the contents of the configuration register (for Macronix devices only).
*/
static U8 _ReadConfigRegisterAlt(const FS_NOR_SPI_INST * pInst) {
  U8       Config;
  unsigned BusWidth;

  BusWidth = pInst->BusWidth;
  (void)_Read(pInst, CMD_RDCR_ALT, &Config, sizeof(Config), BusWidth);
  return Config;
}

/*********************************************************************
*
*      _ReadSFDP
*
*  Function description
*    Reads the operating parameters of the serial flash.
*/
static void _ReadSFDP(const FS_NOR_SPI_INST * pInst, U32 Addr, U8 * pData, U32 NumBytes) {
  U8       abPara[NUM_BYTES_ADDR_SFDP + NUM_BYTES_DUMMY_SFDP];
  unsigned BusWidth;

  BusWidth  = pInst->BusWidth;
  abPara[0] = (U8)(Addr >> 16);
  abPara[1] = (U8)(Addr >>  8);
  abPara[2] = (U8)Addr;
  abPara[3] = 0xFF;                                     // The data is available only after 8 clock cycles.
  (void)_ReadWithAddr(pInst, CMD_READ_SFDP, abPara, sizeof(abPara), NUM_BYTES_ADDR_SFDP, pData, NumBytes, BusWidth);
}

/*********************************************************************
*
*      _ReadSFDP_x2
*
*  Function description
*    Reads the operating parameters of the serial flash.
*/
static int _ReadSFDP_x2(const FS_NOR_SPI_INST * pInst, U32 Addr, U8 * pData, U32 NumBytes) {
  U8         abPara[NUM_BYTES_ADDR_SFDP + NUM_BYTES_DUMMY_SFDP];
  unsigned   BusWidth;
  U8       * pDest;
  U8       * pSrc0;
  U8       * pSrc1;
  int        r;
  unsigned   i;

  ASSERT_IS_NUM_BYTES_EVEN(NumBytes);
  r = 0;        // Set to indicate success.
  BusWidth  = pInst->BusWidth;
  abPara[0] = (U8)(Addr >> 16);
  abPara[1] = (U8)(Addr >>  8);
  abPara[2] = (U8)Addr;
  abPara[3] = 0xFFu;             // The data is available only after 8 clock cycles.
  (void)_ReadWithAddr(pInst, CMD_READ_SFDP, abPara, sizeof(abPara), NUM_BYTES_ADDR_SFDP, pData, NumBytes, BusWidth);
  //
  // Deduplicate the data and check it.
  //
  pSrc0 = pData;
  pSrc1 = pData + 1;
  if (NumBytes == 2u) {
    //
    // Nothing to deduplicate. Perform only a check.
    //
    if (*pSrc0 != *pSrc1) {
      r = 1;
    }
  } else {
    pDest = pData + 1;
    for (i = 0; i < (NumBytes / 2u) - 1u; ++i) {
      if (*pSrc0 != *pSrc1) {
        r = 1;
      }
      pSrc0    += 2;
      pSrc1    += 2;
      *pDest++  = *pSrc0;
    }
  }
  return r;
}

/*********************************************************************
*
*      _ReadBlockProtectionRegister
*
*  Function description
*    Reads the contents of the Block-Protection Register from a Microchip device.
*/
static void _ReadBlockProtectionRegister(const FS_NOR_SPI_INST * pInst, U8 * pData, unsigned NumBytes) {
  unsigned BusWidth;

  BusWidth = pInst->BusWidth;
  (void)_Read(pInst, CMD_RBPR, pData, NumBytes, BusWidth);
}

/*********************************************************************
*
*      _WriteBlockProtectionRegister
*
*  Function description
*    Modifies the contents of the Block-Protection Register from a Microchip device.
*/
static int _WriteBlockProtectionRegister(const FS_NOR_SPI_INST * pInst, const U8 * pData, unsigned NumBytes) {
  int                          r;
  const FS_NOR_SPI_POLL_PARA * pPollPara;
  unsigned                     BusWidth;

  pPollPara = &pInst->PollParaRegWrite;
  BusWidth  = pInst->BusWidth;
  //
  // The command is accepted only when the NOR flash is in write mode.
  //
  r = _EnableWrite(pInst);
  if (r == 0) {
    r = _Write(pInst, CMD_WBPR, pData, NumBytes, BusWidth);
    if (r == 0) {
      r = _WaitForEndOfOperation(pInst, pPollPara);
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
static int _Enter4ByteAddrMode(const FS_NOR_SPI_INST * pInst) {
  int      r;
  unsigned BusWidth;

  BusWidth = pInst->BusWidth;
  r = _Control(pInst, CMD_EN4B, BusWidth);
  return r;
}

/*********************************************************************
*
*      _Exit4ByteAddrMode
*
*  Function description
*    Requests the device to accept 4-byte addresses.
*/
static int _Exit4ByteAddrMode(const FS_NOR_SPI_INST * pInst) {
  int      r;
  unsigned BusWidth;

  BusWidth = pInst->BusWidth;
  r = _Control(pInst, CMD_EX4B, BusWidth);
  return r;
}

/*********************************************************************
*
*      _ReleaseFromPowerDown
*/
static int _ReleaseFromPowerDown(const FS_NOR_SPI_INST * pInst) {
  U8       abData[4];
  unsigned BusWidth;
  int      r;
  int      Result;

  r        = 0;
  BusWidth = pInst->BusWidth;
  //
  // Release device from an possible deep power-down mode (Mode for PE devices
  // and newer P -devices, which do not need or accept dummy bytes).
  //
  Result = _Control(pInst, CMD_RES, BusWidth);
  r = (Result != 0) ? Result : r;
  //
  // Wait for NOR flash device to leave the power down mode.
  //
  (void)_Delay(pInst, 1);
  //
  // Release device from an possible deep power-down mode with dummy bytes.
  //
  FS_MEMSET(abData, 0, sizeof(abData));
  Result = _Write(pInst, CMD_RES, abData, sizeof(abData), BusWidth);
  r = (Result != 0) ? Result : r;
  return r;
}

/*********************************************************************
*
*      _ReleaseFromPowerDown_x2
*/
static int _ReleaseFromPowerDown_x2(const FS_NOR_SPI_INST * pInst) {
  U8       abData[4 * 2];
  unsigned BusWidth;
  int      r;
  int      Result;

  r        = 0;
  BusWidth = pInst->BusWidth;
  //
  // Release device from an possible deep power-down mode (Mode for PE devices
  // and newer P -devices, which do not need or accept dummy bytes).
  //
  Result = _Control(pInst, CMD_RES, BusWidth);
  r = (Result != 0) ? Result : r;
  //
  // Wait for NOR flash device to leave the power down mode.
  //
  (void)_Delay(pInst, 1);
  //
  // Release device from an possible deep power-down mode with dummy bytes.
  //
  FS_MEMSET(abData, 0, sizeof(abData));
  Result = _Write(pInst, CMD_RES, abData, sizeof(abData), BusWidth);
  r = (Result != 0) ? Result : r;
  return r;
}

/*********************************************************************
*
*      _ReadBankRegister
*
*  Function description
*    Reads the contents of the Bank Address Register (Spansion specific).
*/
static U8 _ReadBankRegister(const FS_NOR_SPI_INST * pInst) {
  unsigned BusWidth;
  U8       Data;

  Data     = 0;
  BusWidth = pInst->BusWidth;
  (void)_Read(pInst, CMD_BRRD, &Data, sizeof(Data), BusWidth);
  return Data;
}

/*********************************************************************
*
*      _WriteBankRegister
*
*  Function description
*    Modifies the contents of the Bank Address Register (Spansion specific).
*/
static int _WriteBankRegister(const FS_NOR_SPI_INST * pInst, U8 Data) {
  unsigned BusWidth;
  int      r;

  BusWidth = pInst->BusWidth;
  r = _Write(pInst, CMD_BRWR, &Data, sizeof(Data), BusWidth);
  return r;
}

/*********************************************************************
*
*      _DisableWriteIfRequired
*
*  Function description
*    If set, clears the WEL bit of the status register.
*/
static int _DisableWriteIfRequired(const FS_NOR_SPI_INST * pInst) {
  unsigned Status;
  int      r;

  r      = 0;
  Status = _ReadStatusRegister(pInst);
  if ((Status & (1uL << STATUS_WEL_BIT)) != 0u) {
    r = _DisableWrite(pInst);
  }
  return r;
}

/*********************************************************************
*
*      _DisableWriteIfRequired_x2
*
*  Function description
*    If set, clears the WEL bit of the status register.
*/
static int _DisableWriteIfRequired_x2(const FS_NOR_SPI_INST * pInst) {
  U8  abStatus[2];
  int r;

  r = 0;
  FS_MEMSET(abStatus, 0, sizeof(abStatus));
  _ReadStatusRegister_x2(pInst, abStatus);
  if (   ((abStatus[0] & (1uL << STATUS_WEL_BIT)) != 0u)
      || ((abStatus[1] & (1uL << STATUS_WEL_BIT)) != 0u)) {
    r = _DisableWrite_x2(pInst);
  }
  return r;
}

/*********************************************************************
*
*      _EnableWriteForVolatile
*
*  Function description
*    Enables the write operation to the volatile registers (Cypress specific).
*/
static int _EnableWriteForVolatile(const FS_NOR_SPI_INST * pInst) {
  int      r;
  unsigned BusWidth;

  BusWidth = pInst->BusWidth;
  r = _Control(pInst, CMD_WRENV, BusWidth);
  return r;
}

/*********************************************************************
*
*      _Init
*
*  Function description
*    Prepares the NOR flash for operation.
*/
static void _Init(FS_NOR_SPI_INST * pInst) {
  //
  // Wake-up NOR flash if required.
  //
  (void)_ReleaseFromPowerDown(pInst);
  //
  // Disable the write mode. Some NOR devices (for example Micron N25Q032A)
  // do not respond to read commands if the write enable latch bit in the
  // status register is set.
  //
  (void)_DisableWriteIfRequired(pInst);
}

/*********************************************************************
*
*      _Init_x2
*
*  Function description
*    Prepares both NOR flash devices for operation.
*/
static void _Init_x2(FS_NOR_SPI_INST * pInst) {
  //
  // Wake-up NOR flash if required.
  //
  (void)_ReleaseFromPowerDown_x2(pInst);
  //
  // Disable the write mode. Some NOR devices (for example Micron N25Q032A)
  // do not respond to read commands if the write enable latch bit in the
  // status register is set.
  //
  (void)_DisableWriteIfRequired_x2(pInst);
}

/*********************************************************************
*
*      _RemoveWriteProtection
*
*  Function description
*    Makes all physical sectors writable.
*/
static int _RemoveWriteProtection(const FS_NOR_SPI_INST * pInst, U32 Addr, U32 NumBytes) {
  unsigned Status;
  int      r;
  U8       Data;

  FS_USE_PARA(Addr);
  FS_USE_PARA(NumBytes);
  r      = 0;                     // No error so far.
  Data   = 0;                     // Remove the write protection of all physical sectors.
  Status = _ReadStatusRegister(pInst);
  if ((Status & (STATUS_BP_MASK << STATUS_BP_BIT)) != 0u) {
    r = _WriteStatusRegister(pInst, &Data, sizeof(Data));
  }
  return r;
}

/*********************************************************************
*
*      _RemoveWriteProtection_x2
*
*  Function description
*    Makes all physical sectors writable.
*/
static int _RemoveWriteProtection_x2(const FS_NOR_SPI_INST * pInst, U32 Addr, U32 NumBytes) {
  U8  abStatus[2];
  int r;
  U8  abData[2];

  FS_USE_PARA(Addr);
  FS_USE_PARA(NumBytes);
  r = 0;                                  // No error so far.
  FS_MEMSET(abData, 0, sizeof(abData));   // Remove the write protection of all physical sectors.
  _ReadStatusRegister_x2(pInst, abStatus);
  if (   ((abStatus[0] & (STATUS_BP_MASK << STATUS_BP_BIT)) != 0u)
      || ((abStatus[1] & (STATUS_BP_MASK << STATUS_BP_BIT)) != 0u)) {
    r = _WriteStatusRegister_x2(pInst, abData, sizeof(abData));
  }
  return r;
}

/*********************************************************************
*
*      _WritePageData
*
*  Function description
*    Writes data to a page of NOR flash device.
*
*  Parameters
*    pInst      Driver instance.
*    CmdWrite   Command code of the write operation.
*    Addr       Address to write to.
*    pData      [IN] Data to be written.
*    NumBytes   Number of bytes to write.
*    BusWidth   Number of data lines to be used.
*
*  Return value
*    ==0      OK, data written successfully.
*    !=0      An error occurred.
*
*  Additional information
*    This function does not set the write latch. The write latch has
*    to be set by the calling function.
*/
static int _WritePageData(const FS_NOR_SPI_INST * pInst, U8 CmdWrite, U32 Addr, const U8 * pData, U32 NumBytes, unsigned BusWidth) {
  U8       abPara[MAX_NUM_BYTES_ADDR];
  unsigned NumBytesPara;
  unsigned NumBytesAddr;
  int      r;

  //
  // Initialize local variables.
  //
  NumBytesPara = 0;
  NumBytesAddr = pInst->NumBytesAddr;
  //
  // Encode the byte address to write to.
  //
  FS_MEMSET(abPara, 0, sizeof(abPara));
  if (NumBytesAddr == 4u) {
    abPara[NumBytesPara++] = (U8)(Addr >> 24);
  }
  abPara[NumBytesPara++] = (U8)(Addr >> 16);
  abPara[NumBytesPara++] = (U8)(Addr >>  8);
  abPara[NumBytesPara++] = (U8)Addr;
  //
  // Send command and write page data to NOR flash.
  //
  r = _WriteWithAddr(pInst, CmdWrite, abPara, NumBytesPara, NumBytesAddr, pData, NumBytes, BusWidth);
  return r;
}

/*********************************************************************
*
*      _WritePageData_x2
*
*  Function description
*    Writes data to a page of NOR flash device.
*
*  Parameters
*    pInst      Driver instance.
*    CmdWrite   Command code of the write operation.
*    Addr       Address to write to.
*    pData      [IN] Data to be written.
*    NumBytes   Number of bytes to write.
*    BusWidth   Number of data lines to be used.
*
*  Return value
*    ==0      OK, data written successfully.
*    !=0      An error occurred.
*
*  Additional information
*    This function does not set the write latch. The write latch has
*    to be set by the calling function.
*
*    For the special case where the capacity of the connected NOR flash
*    devices is 16 Mbit, the address of the data that is located
*    on the upper-half of the storage is encoded using four bytes because
*    the address requires one bit more than the three byte address used
*    by these devices. This is indicated to the HW layer by passing
*    FS_NOR_HW_FLAG_ADDR_3BYTE via the Flags parameter.
*/
static int _WritePageData_x2(const FS_NOR_SPI_INST * pInst, U8 CmdWrite, U32 Addr, const U8 * pData, U32 NumBytes, unsigned BusWidth) {
  U8       abPara[MAX_NUM_BYTES_ADDR];
  unsigned NumBytesPara;
  unsigned NumBytesAddr;
  int      r;
  unsigned Flags;

  //
  // Initialize local variables.
  //
  NumBytesPara = 0;
  NumBytesAddr = pInst->NumBytesAddr;
  Flags        = 0;
  //
  // Check if the address has to be extended by one byte.
  //
  if (NumBytesAddr == 3u) {
    if (Addr > 0x00FFFFFFuL) {
      NumBytesAddr = 4;
      Flags        = FS_NOR_HW_FLAG_ADDR_3BYTE;
    }
  }
  //
  // Encode the byte address to write to.
  //
  FS_MEMSET(abPara, 0, sizeof(abPara));
  if (NumBytesAddr == 4u) {
    abPara[NumBytesPara++] = (U8)(Addr >> 24);
  }
  abPara[NumBytesPara++] = (U8)(Addr >> 16);
  abPara[NumBytesPara++] = (U8)(Addr >>  8);
  abPara[NumBytesPara++] = (U8)Addr;
  //
  // Send command and write page data to NOR flash.
  //
  r = _WriteWithCmdExAndAddr(pInst, &CmdWrite, sizeof(CmdWrite), abPara, NumBytesPara, NumBytesAddr, pData, NumBytes, BusWidth, Flags);
  return r;
}

/*********************************************************************
*
*      _WritePageDataCEI
*
*  Function description
*    Writes data to a page of NOR flash device.
*
*  Parameters
*    pInst      Driver instance.
*    CmdWrite   Command code of the write operation.
*    Addr       Address to write to.
*    pData      [IN] Data to be written.
*    NumBytes   Number of bytes to write.
*    BusWidth   Number of data lines to be used.
*    Flags      Command options.
*
*  Return value
*    ==0      OK, data written successfully.
*    !=0      An error occurred.
*
*  Additional information
*    This function does not set the write latch. The write latch has
*    to be set by the calling function.
*/
static int _WritePageDataCEI(const FS_NOR_SPI_INST * pInst, U8 CmdWrite, U32 Addr, const U8 * pData, U32 NumBytes, unsigned BusWidth, unsigned Flags) {
  U8       abAddr[NUM_BYTES_ADDR_OPI];
  int      r;
  unsigned Cmd;
  U8       abCmd[NUM_BYTES_CMD_OPI];

  Cmd       = CmdWrite;
  abCmd[0]  = (U8)Cmd;
  abCmd[1]  = (U8)(~Cmd);
  abAddr[0] = (U8)(Addr >> 24);
  abAddr[1] = (U8)(Addr >> 16);
  abAddr[2] = (U8)(Addr >>  8);
  abAddr[3] = (U8)Addr;
  //
  // Send command and write page data to NOR flash.
  //
  r = _WriteWithCmdExAndAddr(pInst, abCmd, sizeof(abCmd), abAddr, sizeof(abAddr), sizeof(abAddr), pData, NumBytes, BusWidth, Flags);
  return r;
}

/*********************************************************************
*
*      _WritePage
*
*  Function description
*    Writes data to a page of NOR flash.
*/
static int _WritePage(const FS_NOR_SPI_INST * pInst, U32 Addr, const U8 * pData, U32 NumBytes) {
  unsigned BusWidth;
  int      r;
  U8       Cmd;

  //
  // Initialize local variables.
  //
  Cmd      = CMD_PP;
  BusWidth = pInst->BusWidth;
  //
  // Check if a multi-bit write operation is supported and if yes use it.
  //
  if (pInst->CmdWrite != 0u) {
    Cmd      = pInst->CmdWrite;
    BusWidth = pInst->BusWidthWrite;
  }
  //
  // The write page operation is accepted only when the NOR flash is in write mode.
  //
  r = _EnableWrite(pInst);
  //
  // Send command and write page data to NOR flash.
  //
  if (r == 0) {
    r = _WritePageData(pInst, Cmd, Addr, pData, NumBytes, BusWidth);
  }
  return r;
}

/*********************************************************************
*
*      _WritePage_x2
*
*  Function description
*    Writes data to a page of NOR flash.
*
*  Parameters
*    pInst      Driver instance.
*    Addr       Address to write to.
*    pData      [IN] Data to be written.
*    NumBytes   Number of bytes to be written.
*
*  Additional information
*    ==0      OK, data written successfully.
*    !=0      An error occurred
*/
static int _WritePage_x2(const FS_NOR_SPI_INST * pInst, U32 Addr, const U8 * pData, U32 NumBytes) {
  unsigned BusWidth;
  int      r;
  U8       Cmd;

  //
  // Initialize local variables.
  //
  Cmd      = CMD_PP;
  BusWidth = pInst->BusWidth;
  //
  // Check if a multi-bit write operation is supported and if yes use it.
  //
  if (pInst->CmdWrite != 0u) {
    Cmd      = pInst->CmdWrite;
    BusWidth = pInst->BusWidthWrite;
  }
  //
  // The write page operation is accepted only when the NOR flash is in write mode.
  //
  r = _EnableWrite_x2(pInst);
  if (r == 0) {
    r = _WritePageData_x2(pInst, Cmd, Addr, pData, NumBytes, BusWidth);
  }
  return r;
}

/*********************************************************************
*
*       _EraseSector
*
*  Function description
*    Erases a physical sector.
*
*  Parameters
*    pInst      Driver instance.
*    CmdErase   Code of the erase command.
*    Addr       Address of the physical sector.
*
*  Return value
*    ==0      OK, physical sector erased successfully.
*    !=0      An error occurred.
*/
static int _EraseSector(const FS_NOR_SPI_INST * pInst, U8 CmdErase, U32 Addr) {
  U32      NumBytes;
  U32      NumBytesAddr;
  U8       abData[MAX_NUM_BYTES_ADDR];
  unsigned BusWidth;
  int      r;

  //
  // Fill local variables.
  //
  NumBytesAddr = pInst->NumBytesAddr;
  BusWidth     = pInst->BusWidth;
  //
  // Calculate the start address of the physical sector.
  //
  NumBytes = 0;
  if (NumBytesAddr == 4u) {
    abData[NumBytes++] = (U8)(Addr >> 24);
  }
  abData[NumBytes++]   = (U8)(Addr >> 16);
  abData[NumBytes++]   = (U8)(Addr >>  8);
  abData[NumBytes++]   = (U8)Addr;
  //
  // The sector erase command is accepted only when the write mode is active.
  //
  r = _EnableWrite(pInst);
  //
  // Send sector erase command to NOR flash.
  //
  if (r == 0) {
    r = _Write(pInst, CmdErase, abData, NumBytes, BusWidth);
  }
  return r;
}

/*********************************************************************
*
*       _EraseSector_x2
*
*  Function description
*    Erases a physical sector.
*
*  Parameters
*    pInst      Driver instance.
*    CmdErase   Code of the erase command.
*    Addr       Address of the physical sector.
*
*  Return value
*    ==0      OK, physical sector erased successfully.
*    !=0      An error occurred.
*
*  Additional information
*    This function erases two physical sectors: one located
*    on the first NOR flash device and the other one located
*    on the second NOR flash device.
*
*    For the special case where the capacity of the connected NOR flash
*    devices is 16 Mbit, the address of a physical sector that is located
*    on the upper-half of the storage is encoded using four bytes because
*    the address requires one bit more than the three byte address used
*    by these devices. This is indicated to the HW layer by passing
*    FS_NOR_HW_FLAG_ADDR_3BYTE via the Flags parameter.
*/
static int _EraseSector_x2(const FS_NOR_SPI_INST * pInst, U8 CmdErase, U32 Addr) {
  U32      NumBytes;
  U32      NumBytesAddr;
  U8       abData[MAX_NUM_BYTES_ADDR];
  unsigned BusWidth;
  int      r;
  unsigned Flags;

  //
  // Fill local variables.
  //
  NumBytesAddr = pInst->NumBytesAddr;
  BusWidth     = pInst->BusWidth;
  Flags        = 0;
  //
  // Check if the address has to be extended by one byte.
  //
  if (NumBytesAddr == 3u) {
    if (Addr > 0x00FFFFFFuL) {
      NumBytesAddr = 4;
      Flags        = FS_NOR_HW_FLAG_ADDR_3BYTE;
    }
  }
  //
  // Calculate the start address of the physical sector.
  //
  NumBytes = 0;
  if (NumBytesAddr == 4u) {
    abData[NumBytes++] = (U8)(Addr >> 24);
  }
  abData[NumBytes++]   = (U8)(Addr >> 16);
  abData[NumBytes++]   = (U8)(Addr >>  8);
  abData[NumBytes++]   = (U8)Addr;
  //
  // The sector erase command is accepted only when the write mode is active.
  //
  r = _EnableWrite_x2(pInst);
  //
  // Send sector erase command to NOR flash.
  //
  if (r == 0) {
    //
    // We have to send the address bytes as address and not as data because
    // the HW divides the address value by two before sending it to both NOR
    // flash devices.
    //
    r = _WriteWithCmdExAndAddr(pInst, &CmdErase, sizeof(CmdErase), abData, NumBytes, NumBytes, NULL, 0, BusWidth, Flags);
  }
  return r;
}

/*********************************************************************
*
*      _SFDP_IsSupported
*
*  Function description
*    Checks if the device supports SFDP.
*/
static int _SFDP_IsSupported(const FS_NOR_SPI_INST * pInst) {
  U8 abData[6];

  //
  // Read from the beginning of SFDP table.
  //
  _ReadSFDP(pInst, 0x00, abData, sizeof(abData));
  //
  // Check the SFDP signature.
  //
  if ((abData[0] == (U8)'S') &&
      (abData[1] == (U8)'F') &&
      (abData[2] == (U8)'D') &&
      (abData[3] == (U8)'P')) {
    //
    // OK, the device is SFDP compliant. Check the major revision of SFDP.
    //
    if (abData[5] <= SFDP_MIN_REVISION_SUPPORTED) {
      return 1;             // OK, supported.
    }
  }
  return 0;                 // Error, revision of SFDP not supported.
}

/*********************************************************************
*
*      _SFDP_IsSupported_x2
*
*  Function description
*    Checks if the device supports SFDP.
*/
static int _SFDP_IsSupported_x2(const FS_NOR_SPI_INST * pInst) {
  U8  abData[6 * 2];
  int r;

  //
  // Read from the beginning of SFDP table.
  //
  r = _ReadSFDP_x2(pInst, 0x00, abData, sizeof(abData));
  if (r != 0) {
    return 0;               // Error, could not read data.
  }
  //
  // Check the SFDP signature.
  //
  if (   (abData[0] == (U8)'S')
      && (abData[1] == (U8)'F')
      && (abData[2] == (U8)'D')
      && (abData[3] == (U8)'P')) {
    //
    // OK, the device is SFDP compliant. Check the major revision of SFDP.
    //
    if (abData[5] <= SFDP_MIN_REVISION_SUPPORTED) {
      return 1;             // OK, supported.
    }
  }
  return 0;                 // Error, revision of SFDP not supported.
}

/*********************************************************************
*
*      _SFDP_GetBPTAddr
*
*  Function description
*    Returns the address and the size of the Basic Parameter Table.
*
*  Parameters
*    pInst        [IN]  Serial device instance.
*    pNumBytes    [OUT] Size of the Basic Parameter Table in bytes.
*
*  Return value
*    !=0    Address of the Basic Parameter table.
*    ==0    An error occurred.
*/
static U32 _SFDP_GetBPTAddr(const FS_NOR_SPI_INST * pInst, U32 * pNumBytes) {
  U32      NumBytes;
  U32      Addr;
  U8       abData[8];                   // Buffer for the header data
  unsigned NumParas;
  unsigned MfgId;
  unsigned MinorRev;
  unsigned Off;

  //
  // Get the number of parameter tables.
  //
  _ReadSFDP(pInst, 0, abData, sizeof(abData));
  NumParas = (unsigned)abData[6] + 1u;  // The value is 0-based
  //
  // Check the revision of the basic parameter table.
  //
  _ReadSFDP(pInst, 0x08, abData, sizeof(abData));
  if (abData[0] != 0x00u) {
    return 0;                           // Error, expected a JEDEC header.
  }
  if (abData[2] > SFDP_MIN_REVISION_SUPPORTED) {
    return 0;                           // Error, revision of BPT not supported.
  }
  NumBytes = (U32)abData[3] << 2;       // The size of the table is given in 32-bit entities.
  if (NumBytes < 36u) {
    return 0;                           // Error, the table does not contain the required information.
  }
  Addr  = (U32)abData[4];
  Addr |= (U32)abData[5] << 8;
  Addr |= (U32)abData[6] << 16;
  if (pNumBytes != NULL) {
    *pNumBytes = NumBytes;
  }
  //
  // Early Spansion S25FL127S devices store the address as double word.
  // We use the minor version of the first Spansion header to determine
  // if the address has to be corrected. The minor revision is set to 0
  // for the devices which need address correction.
  //
  Off = 0x10;                           // Offset of the first vendor parameter table.
  do {
    _ReadSFDP(pInst, Off, abData, sizeof(abData));
    MfgId = abData[0];
    if (MfgId == MFG_ID_SPANSION) {
      MinorRev = abData[1];
      if (MinorRev == 0u) {
        Addr <<= 2;                     // Convert to byte address.
        break;
      }
    }
    Off += sizeof(abData);
  } while (--NumParas != 0u);
  return Addr;                          // OK, address and the length of the table returned.
}

/*********************************************************************
*
*      _SFDP_GetBPTAddr_x2
*
*  Function description
*    Returns the address and the size of the Basic Parameter Table.
*
*  Parameters
*    pInst        [IN]  Serial device instance.
*    pNumBytes    [OUT] Size of the Basic Parameter Table in bytes.
*
*  Return value
*    !=0    Address of the Basic Parameter table.
*    ==0    An error occurred.
*/
static U32 _SFDP_GetBPTAddr_x2(const FS_NOR_SPI_INST * pInst, U32 * pNumBytes) {
  U32      NumBytes;
  U32      Addr;
  U8       abData[8 * 2];                 // Buffer for the header data
  unsigned NumParas;
  int      r;
  unsigned MfgId;
  unsigned MinorRev;
  unsigned Off;

  //
  // Get the number of parameter tables.
  //
  r = _ReadSFDP_x2(pInst, 0, abData, sizeof(abData));
  if (r != 0) {
    return 0;                             // Error, invalid data.
  }
  NumParas = (unsigned)abData[6] + 1u;    // The value is 0-based
  //
  // Check the revision of the basic parameter table.
  //
  r = _ReadSFDP_x2(pInst, 0x08 * 2, abData, sizeof(abData));
  if (r != 0) {
    return 0;                             // Error, invalid data.
  }
  if (abData[0] != 0x00u) {
    return 0;                             // Error, expected a JEDEC header.
  }
  if (abData[2] > SFDP_MIN_REVISION_SUPPORTED) {
    return 0;                             // Error, revision of BPT not supported.
  }
  NumBytes = (U32)abData[3] << 2;         // The size of the table is given in 32-bit entities.
  if (NumBytes < 36u) {
    return 0;                             // Error, the table does not contain the required information.
  }
  Addr  = (U32)abData[4];
  Addr |= (U32)abData[5] << 8;
  Addr |= (U32)abData[6] << 16;
  if (pNumBytes != NULL) {
    *pNumBytes = NumBytes;
  }
  //
  // Early Spansion S25FL127S devices store the address as double word.
  // We use the minor version of the first Spansion header to determine
  // if the address has to be corrected. The minor revision is set to 0
  // for the devices which need address correction.
  //
  Off = 0x10;                   // Offset of the first vendor parameter table.
  do {
    r = _ReadSFDP_x2(pInst, (U32)Off * 2u, abData, sizeof(abData));
    if (r != 0) {
      return 0;                 // Error, invalid data.
    }
    MfgId = abData[0];
    if (MfgId == MFG_ID_SPANSION) {
      MinorRev = abData[1];
      if (MinorRev == 0u) {
        Addr <<= 2;             // Convert to byte address.
        break;
      }
    }
    Off += sizeof(abData);
  } while (--NumParas != 0u);
  return Addr;                    // OK, address and the length of the table returned.
}

/*********************************************************************
*
*      _SFDP_GetVPTAddr
*
*  Function description
*    Returns the address of the first Vendor Parameter Table.
*
*  Parameters
*    pInst        [IN]  Serial device instance.
*    MfgId        JEDEC vendor id.
*    pNumBytes    [OUT] Size of the Vendor Parameter Table in bytes.
*
*  Return value
*    !=0    Address of the Vendor Parameter table.
*    ==0    An error occurred.
*/
static U32 _SFDP_GetVPTAddr(const FS_NOR_SPI_INST * pInst, unsigned MfgId, unsigned * pNumBytes) {
  unsigned NumBytes;
  U32      Addr;
  U8       abData[8];                     // Buffer for the header data
  unsigned NumParas;
  unsigned Off;

  Addr = 0;                               // Set to indicate an error.
  //
  // Get the number of parameter tables.
  //
  _ReadSFDP(pInst, 0, abData, sizeof(abData));
  NumParas = (unsigned)abData[6] + 1u;    // The value is 0-based
  Off = 0x10;                             // Offset of the first vendor parameter table.
  do {
    _ReadSFDP(pInst, Off, abData, sizeof(abData));
    if (MfgId == abData[0]) {
      NumBytes  = (U32)abData[3] << 2;    // The size of the table is given in 32-bit entities.
      Addr      = (U32)abData[4];
      Addr     |= (U32)abData[5] << 8;
      Addr     |= (U32)abData[6] << 16;
      if (pNumBytes != NULL) {
        *pNumBytes = NumBytes;
      }
      break;                              // Vendor Parameter Table found
    }
    Off += sizeof(abData);
  } while (--NumParas != 0u);
  return Addr;                            // OK, address and the length of the table returned.
}

/*********************************************************************
*
*      _SFDP_ReadApplyDeviceGeometry
*
*  Function description
*    Determines the size of the physical sectors and the number of sectors in the device.
*/
static int _SFDP_ReadApplyDeviceGeometry(FS_NOR_SPI_INST * pInst, U32 BaseAddr) {
  U16                       ldNumBits;
  U32                       Addr;
  U8                        abData[4];
  U8                        ldBytesPerSector;
  U8                        ldBPSToCheck;
  U8                        CmdErase;
  U32                       NumSectors;
  int                       i;
  U8                        NumBytesAddr;
  FS_NOR_SPI_SECTOR_BLOCK * pSectorBlock;
  U8                        ldBPSRequested;

#if (FS_NOR_SFDP_DENSITY_SHIFT == 0)
  {
    U32 Density;

    //
    // Read the device density.
    //
    Addr = BaseAddr + 0x04u;
    _ReadSFDP(pInst, Addr, abData, sizeof(abData));
    //
    // From the spec:
    //   Density <= 2Gbits: bit 31 is set to 0 and the bits 30:0 define the size in bits
    //   Density >  2Gbits: bit 31 is set to 1 and the bits 30:0 define the size as power of 2
    //
    Density  = (U32)abData[0];
    Density |= (U32)abData[1] << 8;
    Density |= (U32)abData[2] << 16;
    Density |= (U32)abData[3] << 24;
    if ((Density & (1uL << 31)) != 0u) {
      ldNumBits = (U16)(Density & ~(1uL << 31));
    } else {
      ldNumBits = (U16)_ld(Density + 1u);
    }
  }
#else
  ldNumBits = FS_NOR_SFDP_DENSITY_SHIFT;
#endif // FS_NOR_SFDP_DENSITY_SHIFT
  //
  // Find the largest erasable physical sector or the physical sector specified by the application.
  //
  CmdErase         = CMD_SE;
  ldBytesPerSector = 0;
  ldBPSRequested   = pInst->ldBytesPerSector;
  Addr = BaseAddr + 0x1Cu;
  for (i = 0; i < 4; ++i) {
    _ReadSFDP(pInst, Addr, abData, 2);
    ldBPSToCheck = abData[0];
    if (ldBPSRequested == 0u) {
      //
      // Choose the largest supported physical sector.
      //
      if (ldBPSToCheck > ldBytesPerSector) {
        ldBytesPerSector = ldBPSToCheck;
        CmdErase = abData[1];
      }
    } else {
      //
      // Choose the physical sector specified by the application.
      //
      if (ldBPSToCheck == ldBPSRequested) {
        ldBytesPerSector = ldBPSToCheck;
        CmdErase = abData[1];
        break;
      }
    }
    Addr += 2u;
  }
  if (ldBytesPerSector == 0u) {
    return 1;                     // Error, no valid sector information found.
  }
  //
  // Calculate the number of physical sectors.
  //
  NumSectors = 1uL << ((ldNumBits - 3u) - ldBytesPerSector);
  //
  // Determine the number of address bytes;
  //
  NumBytesAddr = 3;
  if (ldNumBits > 27u) {          // Devices with a capacity greater than 128Mbit require a 4 byte address.
    NumBytesAddr = 4;
  }
  //
  // Save the geometry info to instance.
  //
  pSectorBlock = &pInst->aSectorBlock[0];
  pSectorBlock->NumSectors       = NumSectors;
  pSectorBlock->CmdErase         = CmdErase;
  pSectorBlock->ldBytesPerSector = ldBytesPerSector;
  pInst->NumSectorBlocks         = 1;
  pInst->NumBytesAddr            = NumBytesAddr;
  return 0;                       // OK, device geometry determined.
}

/*********************************************************************
*
*      _SFDP_ReadApplyDeviceGeometry_x2
*
*  Function description
*    Determines the size of the physical sectors and the number of sectors in the device.
*/
static int _SFDP_ReadApplyDeviceGeometry_x2(FS_NOR_SPI_INST * pInst, U32 BaseAddr) {
  U16                       ldNumBits;
  U32                       Addr;
  U8                        abData[4 * 2];
  U8                        ldBytesPerSector;
  U8                        ldBPSToCheck;
  U8                        CmdErase;
  U32                       NumSectors;
  int                       i;
  U8                        NumBytesAddr;
  FS_NOR_SPI_SECTOR_BLOCK * pSectorBlock;
  U8                        ldBPSRequested;
  int                       r;

#if (FS_NOR_SFDP_DENSITY_SHIFT == 0)
  {
    U32 Density;

    //
    // Read the device density.
    //
    Addr = BaseAddr + 0x04u;
    r = _ReadSFDP_x2(pInst, Addr * 2u, abData, sizeof(abData));
    if (r != 0) {
      return 1;               // Error, invalid information.
    }
    //
    // From the SFDP specification:
    //   Density <= 2Gbits: bit 31 is set to 0 and the bits 30:0 define the size in bits.
    //   Density >  2Gbits: bit 31 is set to 1 and the bits 30:0 define the size as power of 2 exponent.
    //
    Density  = (U32)abData[0];
    Density |= (U32)abData[1] << 8;
    Density |= (U32)abData[2] << 16;
    Density |= (U32)abData[3] << 24;
    if ((Density & (1uL << 31)) != 0u) {
      ldNumBits = (U16)(Density & ~(1uL << 31));
    } else {
      ldNumBits = (U16)_ld(Density + 1u);
    }
  }
#else
  ldNumBits = FS_NOR_SFDP_DENSITY_SHIFT;
#endif // FS_NOR_SFDP_DENSITY_SHIFT
  //
  // Find the largest erasable physical sector or the physical sector specified by the application.
  //
  CmdErase         = CMD_SE;
  ldBytesPerSector = 0;
  ldBPSRequested   = pInst->ldBytesPerSector;
  Addr = BaseAddr + 0x1Cu;
  for (i = 0; i < 4; ++i) {
    r = _ReadSFDP_x2(pInst, Addr * 2u, abData, 2 * 2);
    if (r != 0) {
      return 1;               // Error, invalid information.
    }
    ldBPSToCheck = abData[0];
    if (ldBPSRequested == 0u) {
      //
      // Choose the largest supported physical sector.
      //
      if (ldBPSToCheck > ldBytesPerSector) {
        ldBytesPerSector = ldBPSToCheck;
        CmdErase = abData[1];
      }
    } else {
      //
      // Choose the physical sector specified by the application.
      //
      if (ldBPSToCheck == ldBPSRequested) {
        ldBytesPerSector = ldBPSToCheck;
        CmdErase = abData[1];
        break;
      }
    }
    Addr += 2u;
  }
  if (ldBytesPerSector == 0u) {
    return 1;                     // Error, no valid sector information found.
  }
  //
  // Calculate the number of physical sectors.
  //
  NumSectors = 1uL << ((ldNumBits - 3u) - ldBytesPerSector);
  //
  // Determine the number of address bytes;
  //
  NumBytesAddr = 3;
  if (ldNumBits > 27u) {          // Devices with a capacity greater than 128Mbit require a 4 byte address.
    NumBytesAddr = 4;
  }
  //
  // Save the geometry info to instance.
  //
  pSectorBlock = &pInst->aSectorBlock[0];
  pSectorBlock->NumSectors       = NumSectors;
  pSectorBlock->CmdErase         = CmdErase;
  pSectorBlock->ldBytesPerSector = ldBytesPerSector + 1u;     // +1 because we are erasing two sectors at a time.
  pInst->NumSectorBlocks         = 1;
  pInst->NumBytesAddr            = NumBytesAddr;
  return 0;                       // OK, device geometry determined.
}

/*********************************************************************
*
*      _SFDP_ReadApplyReadMode
*
*  Function description
*    Determines the how many data lines and the command to be used for the read operation.
*/
static int _SFDP_ReadApplyReadMode(FS_NOR_SPI_INST * pInst, U32 BaseAddr) {
  unsigned Cmd;
  unsigned BusWidthCmd;
  unsigned BusWidthAddr;
  unsigned BusWidthData;
  unsigned NumBytesDummy;
  unsigned NumClocksWait;
  unsigned NumClocksMode;
  unsigned NumBitsDummy;
  U32      Addr;
  int      IsMode144Supported;
  int      IsMode114Supported;
  int      IsMode122Supported;
  int      IsMode112Supported;
  U8       abData[4];
  unsigned ReadModesSupported;
  unsigned ReadModesDisabled;
  unsigned BusWidth;
  unsigned NumBitsMode;
  unsigned Flags;

  //
  // Set default read mode to 1-1-1.
  //
  Cmd           = CMD_FAST_READ;
  BusWidthCmd   = 1;
  BusWidthAddr  = 1;
  BusWidthData  = 1;
  NumBytesDummy = 1;
  NumBitsMode   = 0;
  //
  // Check which read commands are supported.
  //
  IsMode144Supported = 0;
  IsMode114Supported = 0;
  IsMode122Supported = 0;
  IsMode112Supported = 0;
  Addr = BaseAddr + 0x02u;
  _ReadSFDP(pInst, Addr, abData, 1);
  ReadModesSupported  = abData[0];
  ReadModesDisabled   = pInst->ReadModesDisabled;
  ReadModesSupported &= ~(unsigned)ReadModesDisabled;
  if ((ReadModesSupported & (1uL << READ_MODE_112_BIT)) != 0u) {
    IsMode112Supported = 1;
  }
  if ((ReadModesSupported & (1uL << READ_MODE_122_BIT)) != 0u) {
    IsMode122Supported = 1;
  }
  if ((ReadModesSupported & (1uL << READ_MODE_144_BIT)) != 0u) {
    IsMode144Supported = 1;
  }
  if ((ReadModesSupported & (1uL << READ_MODE_114_BIT)) != 0u) {
    IsMode114Supported = 1;
  }
  //
  // Find a read command for exchanging data via 4 data lines.
  //
  if (pInst->Allow4bitMode != 0u) {
    //
    // 1-4-4 mode
    //
    if (IsMode144Supported != 0) {
      Addr = BaseAddr + 0x08u;
      _ReadSFDP(pInst, Addr, abData, 2);                      // Get number of wait states, mode bits and command code.
      NumClocksWait = (unsigned)abData[0] & 0x1Fu;
      NumClocksMode = ((unsigned)abData[0] >> 5) & 0x07u;
      NumBitsDummy  = (NumClocksWait + NumClocksMode) << 2;   // << 2 because 4 bits are exchanged on each clock cycle.
      NumBitsMode   =  NumClocksMode << 2;                    // << 2 because 4 bits are exchanged on each clock cycle.
      Cmd           = abData[1];
      BusWidthCmd   = 1;
      BusWidthAddr  = 4;
      BusWidthData  = 4;
      NumBytesDummy = NumBitsDummy >> 3;
      goto Done;
    }
    //
    // 1-1-4 mode
    //
    if (IsMode114Supported !=0) {
      Addr = BaseAddr + 0x0Au;
      _ReadSFDP(pInst, Addr, abData, 2);                      // Get number of wait states, mode bits and command code
      NumClocksWait = (unsigned)abData[0] & 0x1Fu;
      NumClocksMode = ((unsigned)abData[0] >> 5) & 0x07u;
      NumBitsDummy  = NumClocksWait + NumClocksMode;
      NumBitsMode   = NumClocksMode;
      Cmd           = abData[1];
      BusWidthCmd   = 1;
      BusWidthAddr  = 1;
      BusWidthData  = 4;
      NumBytesDummy = NumBitsDummy >> 3;
      goto Done;
    }
  }
  //
  // Check for supported read commands which exchange data via 2 data lines.
  //
  if (pInst->Allow2bitMode != 0u) {
    //
    // 1-2-2 mode
    //
    if (IsMode122Supported != 0) {
      Addr = BaseAddr + 0x0Eu;
      _ReadSFDP(pInst, Addr, abData, 2);                      // Get number of wait states, mode bits and command code
      NumClocksWait = (unsigned)abData[0] & 0x1Fu;
      NumClocksMode = ((unsigned)abData[0] >> 5) & 0x07u;
      NumBitsDummy  = (NumClocksWait + NumClocksMode) << 1;   // << 1 because 2 bits are exchanged on each clock cycle.
      NumBitsMode   = NumClocksMode << 1;                     // << 1 because 2 bits are exchanged on each clock cycle.
      Cmd           = abData[1];
      BusWidthCmd   = 1;
      BusWidthAddr  = 2;
      BusWidthData  = 2;
      NumBytesDummy = NumBitsDummy >> 3;
      goto Done;
    }
    //
    // 1-1-2 mode
    //
    if (IsMode112Supported != 0) {
      Addr = BaseAddr + 0x0Cu;
      _ReadSFDP(pInst, Addr, abData, 2);                      // Get number of wait states, mode bits and command code
      NumClocksWait = (unsigned)abData[0] & 0x1Fu;
      NumClocksMode = ((unsigned)abData[0] >> 5) & 0x07u;
      NumBitsDummy  = NumClocksWait + NumClocksMode;
      NumBitsMode   = NumClocksMode;
      Cmd           = abData[1];
      BusWidthCmd   = 1;
      BusWidthAddr  = 1;
      BusWidthData  = 2;
      NumBytesDummy = NumBitsDummy >> 3;
      goto Done;
    }
  }
Done:
  Flags = pInst->FlagsRead;
  if (NumBitsMode > 0u) {
    if (NumBitsMode <= 4u) {
      Flags |= FS_NOR_HW_FLAG_MODE_4BIT;
    } else {
      Flags |= FS_NOR_HW_FLAG_MODE_8BIT;
    }
  }
  BusWidth                 = FS_BUSWIDTH_MAKE(BusWidthCmd, BusWidthAddr, BusWidthData);
  pInst->CmdRead           = (U8)Cmd;
  pInst->BusWidthRead      = (U16)BusWidth;
  pInst->NumBytesReadDummy = (U8)NumBytesDummy;
  pInst->FlagsRead         = (U16)Flags;
  return 0;                 // OK, read mode determined.
}

/*********************************************************************
*
*      _SFDP_ReadApplyReadMode_x2
*
*  Function description
*    Determines the how many data lines and the command to be used for the read operation.
*/
static int _SFDP_ReadApplyReadMode_x2(FS_NOR_SPI_INST * pInst, U32 BaseAddr) {
  unsigned  Cmd;
  unsigned BusWidthCmd;
  unsigned BusWidthAddr;
  unsigned BusWidthData;
  unsigned NumBytesDummy;
  unsigned NumClocksWait;
  unsigned NumClocksMode;
  unsigned NumBitsDummy;
  U32      Addr;
  int      IsMode144Supported;
  int      IsMode114Supported;
  int      IsMode122Supported;
  int      IsMode112Supported;
  U8       abData[4 * 2];
  unsigned ReadModesSupported;
  unsigned ReadModesDisabled;
  int      r;
  unsigned BusWidth;

  //
  // Set default read mode to 1-1-1.
  //
  Cmd           = CMD_FAST_READ;
  BusWidthCmd   = 1;
  BusWidthAddr  = 1;
  BusWidthData  = 1;
  NumBytesDummy = 1;
  //
  // Check which read commands are supported.
  //
  IsMode144Supported = 0;
  IsMode114Supported = 0;
  IsMode122Supported = 0;
  IsMode112Supported = 0;
  Addr = BaseAddr + 0x02u;
  r = _ReadSFDP_x2(pInst, Addr * 2u, abData, 1 * 2);
  if (r != 0) {
    return 1;                       // Error, invalid data.
  }
  ReadModesSupported  = abData[0];
  ReadModesDisabled   = pInst->ReadModesDisabled;
  ReadModesSupported &= ~(unsigned)ReadModesDisabled;
  if ((ReadModesSupported & (1uL << READ_MODE_112_BIT)) != 0u) {
    IsMode112Supported = 1;
  }
  if ((ReadModesSupported & (1uL << READ_MODE_122_BIT)) != 0u) {
    IsMode122Supported = 1;
  }
  if ((ReadModesSupported & (1uL << READ_MODE_144_BIT)) != 0u) {
    IsMode144Supported = 1;
  }
  if ((ReadModesSupported & (1uL << READ_MODE_114_BIT)) != 0u) {
    IsMode114Supported = 1;
  }
  //
  // Find a read command for exchanging data via 4 data lines.
  //
  if (pInst->Allow4bitMode != 0u) {
    //
    // 1-4-4 mode
    //
    if (IsMode144Supported != 0) {
      Addr = BaseAddr + 0x08u;
      r = _ReadSFDP_x2(pInst, Addr * 2u, abData, 2 * 2);            // Get number of wait states, mode bits and command code
      if (r != 0) {
        return 1;                                                   // Error, invalid data read.
      }
      NumClocksWait = (unsigned)abData[0] & 0x1Fu;
      NumClocksMode = ((unsigned)abData[0] >> 5) & 0x07u;
      NumBitsDummy  = (NumClocksWait + NumClocksMode) << 2;         // 4 bits are exchanged on each clock cycle.
      Cmd           = abData[1];
      BusWidthCmd   = 1;
      BusWidthAddr  = 4;
      BusWidthData  = 4;
      NumBytesDummy = NumBitsDummy >> 3;
      goto Done;
    }
    //
    // 1-1-4 mode
    //
    if (IsMode114Supported != 0) {
      Addr = BaseAddr + 0x0Au;
      r = _ReadSFDP_x2(pInst, Addr * 2u, abData, 2 * 2);            // Get number of wait states, mode bits and command code
      if (r != 0) {
        return 1;                                                   // Error, invalid data read.
      }
      NumClocksWait = (unsigned)abData[0] & 0x1Fu;
      NumClocksMode = ((unsigned)abData[0] >> 5) & 0x07u;
      NumBitsDummy  = NumClocksWait + NumClocksMode;
      Cmd           = abData[1];
      BusWidthCmd   = 1;
      BusWidthAddr  = 1;
      BusWidthData  = 4;
      NumBytesDummy = NumBitsDummy >> 3;
      goto Done;
    }
  }
  //
  // Check for supported read commands which exchange data via 2 data lines.
  //
  if (pInst->Allow2bitMode != 0u) {
    //
    // 1-2-2 mode
    //
    if (IsMode122Supported != 0) {
      Addr = BaseAddr + 0x0Eu;
      r = _ReadSFDP_x2(pInst, Addr * 2u, abData, 2 * 2);            // Get number of wait states, mode bits and command code
      if (r != 0) {
        return 1;                                                   // Error, invalid data read.
      }
      NumClocksWait = (unsigned)abData[0] & 0x1Fu;
      NumClocksMode = ((unsigned)abData[0] >> 5) & 0x07u;
      NumBitsDummy  = (NumClocksWait + NumClocksMode) << 1;         // 2 bits are exchanged on each clock cycle.
      Cmd           = abData[1];
      BusWidthCmd   = 1;
      BusWidthAddr  = 2;
      BusWidthData  = 2;
      NumBytesDummy = NumBitsDummy >> 3;
      goto Done;
    }
    //
    // 1-1-2 mode
    //
    if (IsMode112Supported != 0) {
      Addr = BaseAddr + 0x0Cu;
      r = _ReadSFDP_x2(pInst, Addr * 2u, abData, 2 * 2);            // Get number of wait states, mode bits and command code
      if (r != 0) {
        return 1;                                                   // Error, invalid data read.
      }
      NumClocksWait = (unsigned)abData[0] & 0x1Fu;
      NumClocksMode = ((unsigned)abData[0] >> 5) & 0x07u;
      NumBitsDummy  = NumClocksWait + NumClocksMode;
      Cmd           = abData[1];
      BusWidthCmd   = 1;
      BusWidthAddr  = 1;
      BusWidthData  = 2;
      NumBytesDummy = NumBitsDummy >> 3;
      goto Done;
    }
  }
Done:
  BusWidth                 = FS_BUSWIDTH_MAKE(BusWidthCmd, BusWidthAddr, BusWidthData);
  pInst->CmdRead           = (U8)Cmd;
  pInst->BusWidthRead      = (U16)BusWidth;
  pInst->NumBytesReadDummy = (U8)NumBytesDummy;
  return 0;                 // OK, read mode determined.
}

/*********************************************************************
*
*       _SFDP_ReadApplyPara
*
*  Function description
*    Tries to identify the parameters of the serial NOR flash device
*    by using the Serial Flash Discovery Parameters.
*
*  Return value
*    ==0    OK, serial NOR flash device identified.
*    !=0    Error, serial NOR flash device does not support SFDP.
*/
static int _SFDP_ReadApplyPara(FS_NOR_SPI_INST * pInst) {
  int r;
  U32 AddrBPT;

  //
  // Check if the device supports SFDP.
  //
  r = _SFDP_IsSupported(pInst);
  if (r == 0) {
    return 1;                       // Error, device does not support SFDP.
  }
  //
  // OK, the device supports SFDP.
  // Get the position and the size of the Basic Parameter Table.
  //
  AddrBPT = _SFDP_GetBPTAddr(pInst, NULL);
  if (AddrBPT == 0u) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _SFDP_ReadApplyPara: Could not get BPT address."));
    return 1;                       // Error, BPT not found or invalid.
  }
  //
  // Determine the device geometry.
  //
  r = _SFDP_ReadApplyDeviceGeometry(pInst, AddrBPT);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _SFDP_ReadApplyPara: Could not get device geometry."));
    return 1;                       // Error, could not determine the device geometry.
  }
  //
  // Determine the most suitable read command to use.
  //
  r = _SFDP_ReadApplyReadMode(pInst, AddrBPT);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _SFDP_ReadApplyPara: Could not get read mode."));
    return 1;                       // Error, could not determine the read mode.
  }
  return 0;                         // OK, device identified.
}

/*********************************************************************
*
*       _SFDP_ReadApplyPara_x2
*
*  Function description
*    Tries to identify the parameters of the serial NOR flash device
*    by using the Serial Flash Discovery Parameters.
*
*  Return value
*    ==0    OK, serial NOR flash device identified.
*    !=0    Error, serial NOR flash device does not support SFDP.
*/
static int _SFDP_ReadApplyPara_x2(FS_NOR_SPI_INST * pInst) {
  int r;
  U32 AddrBPT;

  //
  // Check if the device supports SFDP.
  //
  r = _SFDP_IsSupported_x2(pInst);
  if (r == 0) {
    return 1;                       // Error, device does not support SFDP.
  }
  //
  // OK, the device supports SFDP.
  // Get the position and the size of the Basic Parameter Table.
  //
  AddrBPT = _SFDP_GetBPTAddr_x2(pInst, NULL);
  if (AddrBPT == 0u) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _SFDP_ReadApplyPara_x2: Could not get BPT address."));
    return 1;                       // Error, BPT not found or invalid.
  }
  //
  // Determine the device geometry.
  //
  r = _SFDP_ReadApplyDeviceGeometry_x2(pInst, AddrBPT);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _SFDP_ReadApplyPara_x2: Could not get device geometry."));
    return 1;                       // Error, could not determine the device geometry.
  }
  //
  // Determine the most suitable read command to use.
  //
  r = _SFDP_ReadApplyReadMode_x2(pInst, AddrBPT);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _SFDP_ReadApplyPara_x2: Could not get read mode."));
    return 1;                       // Error, could not determine the read mode.
  }
  return 0;                         // OK, device identified.
}

/*********************************************************************
*
*      _ReadEnhancedConfigRegister
*
*  Function description
*    Returns the contents of the Enhanced Volatile Configuration Register (Micron specific).
*
*  Parameters
*    pInst      Driver instance.
*
*  Return value
*    Register value.
*/
static U8 _ReadEnhancedConfigRegister(const FS_NOR_SPI_INST * pInst) {
  U8       Config;
  unsigned BusWidth;

  Config   = 0;
  BusWidth = pInst->BusWidth;
  (void)_Read(pInst, CMD_RECR, &Config, sizeof(Config), BusWidth);
  return Config;
}

/*********************************************************************
*
*      _WriteEnhancedConfigRegister
*
*  Function description
*    Modifies the value of the Enhanced Volatile Configuration Register (Micron specific).
*
*  Parameters
*    pInst      Driver instance.
*    Config     Register value.
*
*  Return value
*    ==0        OK, register value set successfully.
*    !=0        An error occurred.
*/
static int _WriteEnhancedConfigRegister(const FS_NOR_SPI_INST * pInst, U8 Config) {
  int      r;
  unsigned BusWidth;

  BusWidth = pInst->BusWidth;
  //
  // The command is accepted only when the NOR flash is in write mode.
  //
  r = _EnableWrite(pInst);
  if (r == 0) {
    r = _Write(pInst, CMD_WECR, &Config, sizeof(Config), BusWidth);
  }
  return r;
}

/*********************************************************************
*
*      _ReadConfigRegister2
*
*  Function description
*    Returns the contents of the Configuration Register 2 (Macronix specific).
*
*  Parameters
*    pInst        Driver instance.
*    Addr         Register address.
*
*  Return value
*    Register value.
*/
static unsigned _ReadConfigRegister2(const FS_NOR_SPI_INST * pInst, U32 Addr) {
  U8       Data;
  unsigned BusWidth;
  U8       abPara[4];

  Data      = 0;
  BusWidth  = pInst->BusWidth;
  abPara[0] = (U8)(Addr >> 24);
  abPara[1] = (U8)(Addr >> 16);
  abPara[2] = (U8)(Addr >>  8);
  abPara[3] = (U8)Addr;
  (void)_ReadWithAddr(pInst, CMD_RDCR2, abPara, sizeof(abPara), sizeof(abPara), &Data, sizeof(Data), BusWidth);
  return Data;
}

/*********************************************************************
*
*      _ReadConfigRegister2_CEI
*
*  Function description
*    Returns the contents of the Configuration Register 2.
*
*  Parameters
*    pInst        Driver instance.
*    Addr         Register address.
*    pValue       [OUT] Register value.
*
*  Return value
*    ==0      OK, register read successfully.
*    !=0      An error occurred.
*
*  Additional information
*    This function performs the same operation as _ReadConfigRegister2()
*    with the difference that the command is sent as a two byte sequence
*    with the second byte being set to the negated value of the first
*    (Command Extension Inverted).
*
*    This function is used for Macronix devices with octal interface.
*/
static int _ReadConfigRegister2_CEI(const FS_NOR_SPI_INST * pInst, U32 Addr, unsigned * pValue) {
  U8       Data;
  unsigned BusWidth;
  U8       abPara[NUM_BYTES_ADDR_OPI + NUM_BYTES_DUMMY_OPI * 2u];         // *2 because in DTR mode two bytes are exchanged on each clock edge.
  U8       abCmd[NUM_BYTES_CMD_OPI];
  unsigned Cmd;
  unsigned Flags;
  unsigned NumBytes;
  int      r;

  FS_MEMSET(abPara, 0, sizeof(abPara));
  Data      = 0;
  BusWidth  = pInst->BusWidth;
  Flags     = pInst->Flags;
  Cmd       = CMD_RDCR2;
  abCmd[0]  = (U8)Cmd;
  abCmd[1]  = (U8)(~Cmd);
  abPara[0] = (U8)(Addr >> 24);
  abPara[1] = (U8)(Addr >> 16);
  abPara[2] = (U8)(Addr >>  8);
  abPara[3] = (U8)Addr;
  //
  // Calculate the number of parameter bytes to send.
  // In DTR mode we have to send two times more dummy bytes.
  //
  NumBytes = NUM_BYTES_ADDR_OPI + NUM_BYTES_DUMMY_OPI;
  if ((Flags & FS_NOR_HW_FLAG_DTR_ADDR) != 0u) {
    NumBytes += NUM_BYTES_DUMMY_OPI;
  }
  //
  // Read the register value.
  //
  r = _ReadWithCmdExAndAddr(pInst, abCmd, sizeof(abCmd), abPara, NumBytes, sizeof(Addr), &Data, sizeof(Data), BusWidth, Flags);
  if (pValue != NULL) {
    *pValue = Data;
  }
  return r;
}

/*********************************************************************
*
*      _WriteConfigRegister2
*
*  Function description
*    Modifies the value of the Configuration Register 2 (Macronix specific).
*
*  Parameters
*    pInst      Driver instance.
*    Addr       Register address.
*    Value      Register value.
*
*  Return value
*    ==0        OK, register value set successfully.
*    !=0        An error occurred.
*/
static int _WriteConfigRegister2(const FS_NOR_SPI_INST * pInst, U32 Addr, unsigned Value) {
  int      r;
  unsigned BusWidth;
  U8       abPara[4];
  U8       Data;

  BusWidth  = pInst->BusWidth;
  abPara[0] = (U8)(Addr >> 24);
  abPara[1] = (U8)(Addr >> 16);
  abPara[2] = (U8)(Addr >>  8);
  abPara[3] = (U8)Addr;
  Data      = (U8)Value;
  r = _EnableWrite(pInst);                  // The command is accepted only when the NOR flash is in write mode.
  if (r == 0) {
    r = _WriteWithAddr(pInst, CMD_WRCR2, abPara, sizeof(abPara), sizeof(abPara), &Data, sizeof(Data), BusWidth);
  }
  return r;
}

/*********************************************************************
*
*      _WriteConfigRegister2_CEI
*
*  Function description
*    Modifies the value of the Configuration Register 2.
*
*  Parameters
*    pInst      Driver instance.
*    Addr       Register address.
*    Value      Register value.
*
*  Return value
*    ==0        OK, register value set successfully.
*    !=0        An error occurred.
*
*  Additional information
*    This function performs the same operation as _WriteConfigRegister2()
*    with the difference that the command is sent as a two byte sequence
*    with the second byte being set to the negated value of the first
*    (Command Extension Inverted).
*
*    This function is used for Macronix devices with octal interface.
*/
static int _WriteConfigRegister2_CEI(const FS_NOR_SPI_INST * pInst, U32 Addr, unsigned Value) {
  int      r;
  unsigned BusWidth;
  U8       abData[2];
  U8       abPara[NUM_BYTES_ADDR_OPI + NUM_BYTES_DUMMY_OPI];
  U8       abCmd[NUM_BYTES_CMD_OPI];
  unsigned Cmd;
  unsigned Flags;
  unsigned NumBytes;

  FS_MEMSET(abPara, 0, sizeof(abPara));
  BusWidth  = pInst->BusWidth;
  Flags     = pInst->Flags;
  Cmd       = CMD_WRCR2;
  abCmd[0]  = (U8)Cmd;
  abCmd[1]  = (U8)(~Cmd);
  abPara[0] = (U8)(Addr >> 24);
  abPara[1] = (U8)(Addr >> 16);
  abPara[2] = (U8)(Addr >>  8);
  abPara[3] = (U8)Addr;
  //
  // Calculate the number of data bytes to read.
  // In DTR mode we have to read a multiple of two bytes.
  //
  NumBytes = 0;
  abData[NumBytes++] = (U8)Value;
  if ((Flags & FS_NOR_HW_FLAG_DTR_DATA) != 0u) {
    abData[NumBytes++] = (U8)Value;
  }
  //
  // Read the register value.
  //
  r = _EnableWriteCEI(pInst);                // The command is accepted only when the NOR flash is in write mode.
  if (r == 0) {
    r = _WriteWithCmdExAndAddr(pInst, abCmd, sizeof(abCmd), abPara, sizeof(abPara), sizeof(abPara), abData, NumBytes, BusWidth, Flags);
  }
  return r;
}

/*********************************************************************
*
*      Static code (public via callback)
*
**********************************************************************
*/

/*********************************************************************
*
*       _SPANSION_Identify
*
*  Function description
*    Identifies a Spansion NOR flash device by device id.
*
*  Parameters
*    pInst        Driver instance.
*    pId          [IN] Device id.
*
*  Return value
*    ==0      Device supported.
*    !=0      Device not supported.
*
*  Notes
*    (1) The S25FL256L device identifies itself with the same manufacturer
*        and device id as the S25FL256S device (that is with an 'S' at the
*        end instead of an 'L'). Since these devices are not 100% compatible,
*        we have to check the second byte returned in the response to
*        READ ID (0x9F) command. This byte is set to 0x02 for the S variant
*        and to 0x60 for the L variant.
*        The same applies to S25FL164K and S25FL064L devices.
*/
static int _SPANSION_Identify(FS_NOR_SPI_INST * pInst, const U8 * pId) {
  int      r;
  unsigned MfgId;
  unsigned DeviceId;
  unsigned DeviceType;

  FS_USE_PARA(pInst);
  r          = 1;                 // Set to indicate an error.
  MfgId      = pId[0];
  DeviceType = pId[1];
  DeviceId   = pId[2];
  if (MfgId == MFG_ID_SPANSION) {
    //
    // The following Spansion devices support error reporting:
    //
    // Id                Device
    // ------------------------
    // 0x01 0x.. 0x15    Spansion S25FL032P
    // 0x01 0x.. 0x16    Spansion S25FL064P, S25FL132K
    // 0x01 0x.. 0x17    Spansion S25FL164K
    // 0x01 0x.. 0x18    Spansion S25FL129P, S25FS128S, S25FL127S, S70FL256P
    // 0x01 0x.. 0x19    Spansion S25FS256S, S25FL256S
    // 0x01 0x.. 0x20    Spansion S25FS512S, S25FL512S, S70FL01GS
    //
    if ((DeviceId >= 0x15u) && (DeviceId <= 0x20u)) {
      if ((DeviceId >= 0x17u) && (DeviceId <= 0x19u)) {   // Note 1
        if (DeviceType == 0x02u) {
          r = 0;                                          // OK, device identified.
        }
      } else {
        r = 0;                                            // OK, device identified.
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _SPANSION_SetBusWidth
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
static int _SPANSION_SetBusWidth(FS_NOR_SPI_INST * pInst) {
  U8       abRegData[2];
  int      r;
  int      WriteReg;
  unsigned Data;
  unsigned BusWidth;

  r        = 0;
  WriteReg = 0;
  BusWidth = pInst->BusWidthRead;
  BusWidth = FS_BUSWIDTH_GET_DATA(BusWidth);
  //
  // We have to read the status register here since
  // the write command has to write first the status and
  // then the configuration register.
  //
  abRegData[0] = (U8)_ReadStatusRegister(pInst);
  abRegData[1] = (U8)_ReadConfigRegister(pInst);
  switch (BusWidth) {
  default:
  case 1:
    // through
  case 2:
    if ((abRegData[1] & (1u << CONFIG_QUAD_BIT)) != 0u) {
      Data  = abRegData[1];
      Data &= ~(1uL << CONFIG_QUAD_BIT);
      abRegData[1] = (U8)Data;
      WriteReg = 1;
    }
    break;
  case 4:
    if ((abRegData[1] & (1u << CONFIG_QUAD_BIT)) == 0u) {
      abRegData[1] |= 1u << CONFIG_QUAD_BIT;
      WriteReg = 1;
    }
    break;
  }
  if (WriteReg != 0) {
    r = _WriteStatusRegister(pInst, abRegData, sizeof(abRegData));
  }
  return r;
}

/*********************************************************************
*
*       _CFI_IsSupported
*
*  Function description
*    Checks if the NOR device supports CFI.
*/
static int _CFI_IsSupported(const FS_NOR_SPI_INST * pInst) {
  U8 ac[19];

  _ReadId(pInst, ac, sizeof(ac));
  if ((ac[0x10] == (U8)'Q') &&
      (ac[0x11] == (U8)'R') &&
      (ac[0x12] == (U8)'Y')) {
    return 1;             // OK, CFI supported.
  }
  return 0;               // CFI not supported.
}

/*********************************************************************
*
*       _CFI_ReadApplyDeviceGeometry
*/
static int _CFI_ReadApplyDeviceGeometry(FS_NOR_SPI_INST * pInst) {
  U8                        ac[0x2D + FS_NOR_MAX_SECTOR_BLOCKS * 4];  // The information about the device organization is stored at offsets 0x2C-0x34
                                                                      // Since it is not possible to read only the information about the device geometry
                                                                      // we have to allocate a buffer large enough to store all the read bytes.
  unsigned                  NumSectorBlocks;
  FS_NOR_SPI_SECTOR_BLOCK * pSectorBlock;
  unsigned                  iBlock;
  unsigned                  NumSectors;
  unsigned                  Off;
  unsigned                  NumChunks;
  unsigned                  ldBytesPerSector;
  U8                        CmdErase;

  _ReadId(pInst, ac, sizeof(ac));
  //
  // Get the number of sector blocks.
  //
  NumSectorBlocks = ac[0x2C];
  if (NumSectorBlocks > (unsigned)FS_NOR_MAX_SECTOR_BLOCKS) {
    return 1;             // Error, invalid number of sector blocks.
  }
  //
  // For each sector block read the sector size and the number of sectors.
  //
  Off          = 0x2D;
  pSectorBlock = pInst->aSectorBlock;
  for (iBlock = 0; iBlock < NumSectorBlocks; ++iBlock) {
    NumSectors  = (unsigned)ac[Off++];
    NumSectors |= (unsigned)ac[Off++] << 8;
    ++NumSectors;                                 // The encoded value is the actual number of sectors - 1 (Ex: 32 sectors -> 32 - 1 -> 0x1F)
    NumChunks  = (unsigned)ac[Off++];
    NumChunks |= (unsigned)ac[Off++] << 8;
    ldBytesPerSector = 8u + _ld(NumChunks);       // The sector size is encoded as a factor of 256 byte chunks.
    if (ldBytesPerSector == 12u) {                // The 4 KB sectors have a different erase command.
      CmdErase = CMD_P4E;
    } else {
      CmdErase = CMD_SE;
    }
    pSectorBlock->NumSectors       = NumSectors;
    pSectorBlock->ldBytesPerSector = (U8)ldBytesPerSector;
    pSectorBlock->CmdErase         = CmdErase;
    ++pSectorBlock;
  }
  pInst->NumSectorBlocks = (U8)NumSectorBlocks;
  return 0;
}

/*********************************************************************
*
*       _SPANSION_SetNumBytesAddr
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
static int _SPANSION_SetNumBytesAddr(const FS_NOR_SPI_INST * pInst) {
  unsigned Data;
  int      r;
  unsigned NumBytes;

  r        = 0;
  NumBytes = pInst->NumBytesAddr;
  Data     = _ReadBankRegister(pInst);
  if (NumBytes == 4u) {
    if ((Data & (1uL << BAR_EXTADD_BIT)) == 0u) {
      Data |= 1uL << BAR_EXTADD_BIT;
      r = _WriteBankRegister(pInst, (U8)Data);
    }
  } else {
    if ((Data & (1uL << BAR_EXTADD_BIT)) != 0u) {
      Data &= ~(1uL << BAR_EXTADD_BIT);
      r = _WriteBankRegister(pInst, (U8)Data);
    }
  }
  return r;
}

/*********************************************************************
*
*      _SPANSION_ReadApplyDeviceGeometry
*
*  Function description
*    Determines the size of the physical sectors and the number of sectors in the device.
*/
static int _SPANSION_ReadApplyDeviceGeometry(FS_NOR_SPI_INST * pInst, U32 BaseAddr) {
  U16                       ldNumBits;
  U32                       Density;
  U32                       Addr;
  U8                        abData[4];
  U8                        ldBytesPerSector;
  U8                        ldBPSToCheck;
  U8                        CmdErase;
  U32                       NumSectors;
  int                       i;
  U8                        NumBytesAddr;
  FS_NOR_SPI_SECTOR_BLOCK * pSectorBlock;
  int                       IsBlockSupported64KB;
  int                       IsBlockSupported256KB;
  U8                        Status;

  //
  // Read the device density.
  //
  Addr = BaseAddr + 0x04u;
  _ReadSFDP(pInst, Addr, abData, sizeof(abData));
  //
  // From the spec:
  //   Density <= 2Gbits: bit 31 is set to 0 and the bits 30:0 define the size in bits
  //   Density >  2Gbits: bit 31 is set to 1 and the bits 30:0 define the size as power of 2
  //
  Density  = (U32)abData[0];
  Density |= (U32)abData[1] << 8;
  Density |= (U32)abData[2] << 16;
  Density |= (U32)abData[3] << 24;
  if ((Density & (1uL << 31)) != 0u) {
    ldNumBits = (U16)(Density & ~(1uL << 31));
  } else {
    ldNumBits = (U16)_ld(Density + 1u);
  }
  //
  // Find the largest erasable physical sector.
  //
  CmdErase              = CMD_SE;
  ldBytesPerSector      = 0;
  IsBlockSupported64KB  = 0;
  IsBlockSupported256KB = 0;
  Addr = BaseAddr + 0x1Cu;
  for (i = 0; i < 4; ++i) {
    _ReadSFDP(pInst, Addr, abData, 2);
    ldBPSToCheck = abData[0];
    if (ldBPSToCheck > ldBytesPerSector) {
      ldBytesPerSector = ldBPSToCheck;
      CmdErase = abData[1];
    }
    if (ldBPSToCheck == 16u) {
      IsBlockSupported64KB = 1;
    }
    if (ldBPSToCheck == 18u) {
      IsBlockSupported256KB = 1;
    }
    Addr += 2u;
  }
  if (ldBytesPerSector == 0u) {
    return 1;                     // Error, no valid sector information found.
  }
  //
  // Calculate the number of physical sectors.
  //
  NumSectors = 1uL << ((ldNumBits - 3u) - ldBytesPerSector);
  //
  // Correct the sector size and the number of sectors for devices that support 256 KB and 64 KB erasable phy. sectors.
  // The sector size is configured via D8h_O flag (bit 7 in the Status Register 2).
  // If the flag is set to 1 the same erase command 0xD8 erases 256 KB at once. When set to 0 the device erases a block of 64 KB.
  // It seems that the device reports the wrong sector size when D8h_O flag is set to 1. The sector size encoded in SFDP is 64 KB instead of 256 KB.
  // The opposite seems also to be true: The device reports a sector size of 256 KB even when the D8h_O flag is set to 0.
  // The Spansion S25FL127S device has to be checked separately since sector size cannot be determined via SFDP.
  //
  if ((ldNumBits == 27u) || ((IsBlockSupported64KB != 0) && (IsBlockSupported256KB != 0))) {
    Status = _ReadStatusRegister2(pInst);
    if ((Status & (1u << STATUS2_D8H_O_BIT)) != 0u) {     // 256 KB sectors?
      if (ldBytesPerSector == 16u) {
        ldBytesPerSector   = 18;
        NumSectors       >>= 2;
      }
    } else {
      if (ldBytesPerSector == 18u) {
        ldBytesPerSector   = 16;
        NumSectors       <<= 2;
      }
    }
  }
  //
  // Determine the number of address bytes;
  //
  NumBytesAddr = 3;
  if (ldNumBits > 27u) {          // Devices with a capacity greater than 128Mbit require a 4 byte address.
    NumBytesAddr = 4;
  }
  //
  // Save the geometry info to instance.
  //
  pSectorBlock = &pInst->aSectorBlock[0];
  pSectorBlock->NumSectors       = NumSectors;
  pSectorBlock->CmdErase         = CmdErase;
  pSectorBlock->ldBytesPerSector = ldBytesPerSector;
  pInst->NumSectorBlocks         = 1;
  pInst->NumBytesAddr            = NumBytesAddr;
  return 0;                       // OK, device geometry determined.
}

/*********************************************************************
*
*       _SPANSION_ReadApplyPara
*
*  Function description
*    Tries to identify the parameters of the serial NOR flash device
*    by using the Serial Flash Discovery Parameters.
*
*  Parameters
*    pInst        Driver instance.
*
*  Return value
*    ==0    OK, serial NOR flash device identified.
*    !=0    Error, serial NOR flash device does not support SFDP.
*/
static int _SPANSION_ReadApplyPara(FS_NOR_SPI_INST * pInst) {
  int                       r;
  U32                       AddrBPT;
  U8                        ldBytesPerSector;
  U32                       NumSectors;
  U32                       NumSectors4KB;
  U8                        Config;
  U8                        NumSectorBlocks;
  U8                        iBlock;
  U8                        NumBytesAddr;
  U32                       NumKBytes;
  FS_NOR_SPI_SECTOR_BLOCK * pSectorBlock;

  //
  // Check if the device supports SFDP.
  //
  r = _SFDP_IsSupported(pInst);
  if (r == 0) {
    r = _CFI_IsSupported(pInst);
    if (r == 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _SPANSION_ReadApplyPara: SFDP or CFI are not supported."));
      return 1;                     // Error, device does not support either SFDP nor CFI.
    }
    r = _CFI_ReadApplyDeviceGeometry(pInst);
    if (r != 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _SPANSION_ReadApplyPara: Could not get device geometry."));
      return 1;                     // Error, could not determine the device geometry.
    }
    //
    // The erase command for 64 or 256 KB sectors can also be used to erase consecutive 4 KB sectors.
    // In this case, we can safely report to physical layer that the device is organized in uniform sectors.
    //
    NumSectorBlocks = pInst->NumSectorBlocks;
    if (NumSectorBlocks == 2u) {
      NumSectors4KB    = 0;
      NumSectors       = 0;
      ldBytesPerSector = 0;
      pSectorBlock = pInst->aSectorBlock;
      for (iBlock = 0; iBlock < NumSectorBlocks; ++iBlock) {
        if (pSectorBlock->ldBytesPerSector == 12u) {
          NumSectors4KB = pSectorBlock->NumSectors;
        } else {
          NumSectors       = pSectorBlock->NumSectors;
          ldBytesPerSector = pSectorBlock->ldBytesPerSector;
        }
        ++pSectorBlock;
      }
      if (NumSectors4KB != 0u) {
        pSectorBlock = pInst->aSectorBlock;
        NumSectors4KB >>= ldBytesPerSector - 12u;
        NumSectors     += NumSectors4KB;
        pSectorBlock->CmdErase         = CMD_SE;
        pSectorBlock->ldBytesPerSector = ldBytesPerSector;
        pSectorBlock->NumSectors       = NumSectors;
        pInst->NumSectorBlocks = 1;
      }
    }
    //
    // NOR devices with a capacity greater than 128 MBit (16 MByte) require a 4-byte address.
    //
    NumKBytes    = _CalcDeviceCapacity(pInst);
    NumBytesAddr = 3;
    if (NumKBytes > (16u * 1024u)) {
      NumBytesAddr = 4;
    }
    //
    // Configure the read mode.
    //
    pInst->NumBytesAddr      = NumBytesAddr;
    pInst->CmdRead           = CMD_FAST_READ;
    pInst->BusWidthRead      = (U16)FS_BUSWIDTH_MAKE(1uL, 1uL, 1uL);
    pInst->NumBytesReadDummy = 1;
    return 0;                         // OK, device identified.
  }
  //
  // OK, the device supports SFDP.
  // Get the position and the size of the Basic Parameter Table.
  //
  AddrBPT = _SFDP_GetBPTAddr(pInst, NULL);
  if (AddrBPT == 0u) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _SPANSION_ReadApplyPara: Could not get BPT address."));
    return 1;                       // Error, BPT not found or invalid.
  }
  //
  // Determine the device geometry.
  //
  r = _SPANSION_ReadApplyDeviceGeometry(pInst, AddrBPT);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _SPANSION_ReadApplyPara: Could not get device geometry."));
    return 1;                       // Error, could not determine the device geometry.
  }
  //
  // Determine the most suitable read command to use.
  //
  r = _SFDP_ReadApplyReadMode(pInst, AddrBPT);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _SPANSION_ReadApplyPara: Could not get read mode."));
    return 1;                       // Error, could not determine the read mode.
  }
  //
  // Configure the number of dummy cycles for the read command in SPI mode.
  // The number of dummy cycles is encoded in the bits LC1 and LC0 of the configuration register.
  //
  if (pInst->CmdRead == CMD_FAST_READ) {
    Config = _ReadConfigRegister(pInst);
    if ((Config & (CONFIG_LATENCY_MASK << CONFIG_LATENCY_BIT)) == (CONFIG_LATENCY_NONE << CONFIG_LATENCY_BIT)) {
      pInst->NumBytesReadDummy = 0;
    }
  }
  return 0;                         // OK, device identified.
}

/*********************************************************************
*
*       _SPANSION_WaitForEndOfOperation
*
*  Function description
*    Waits for a NOR flash operation to complete.
*
*  Parameters
*    pInst        Driver instance.
*    pPollPara    [IN] Parameters of the polling operation.
*
*  Return value
*    ==0      OK, operation completed successfully.
*    !=0      An error occurred.
*/
static int _SPANSION_WaitForEndOfOperation(const FS_NOR_SPI_INST * pInst, const FS_NOR_SPI_POLL_PARA * pPollPara) {
  int      r;
  unsigned Status;
  U32      TimeOut;
  int      rPoll;

  r       = 1;                                                      // Set to indicate error.
  TimeOut = pPollPara->TimeOut;
  //
  // Prefer polling the status register in the hardware if supported.
  //
  rPoll = _PollStatusRegister(pInst, STATUS_BUSY_BIT, pPollPara);
  if (rPoll > 0) {                                                  // Timeout reported?
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _SPANSION_WaitForEndOfOperation: Timeout expired."));
  } else {
    if (rPoll < 0) {                                                // Is the feature not supported?
      //
      // Poll the status register in the software.
      //
      for (;;) {
        //
        // Read the status from NOR device.
        //
        Status = _ReadStatusRegister(pInst);
        if ((Status & (1uL << STATUS_BUSY_BIT)) == 0u) {
          r = 0;                                                    // OK, the NOR flash is ready for a new operation.
          break;
        }
        --TimeOut;                                                  // We have executed one wait cycle.
        //
        // If possible, delay the next request to reduce CPU usage.
        //
        TimeOut = _DelayPoll(pInst, TimeOut, pPollPara);
        if (TimeOut == 0u) {
          FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _SPANSION_WaitForEndOfOperation: Timeout expired."));
          break;
        }
      }
    } else {
      r = 0;                                                        // OK, the NOR device flash is ready for a new operation.
    }
  }
  if (r == 0) {
    //
    // Check the error flags and clear them if necessary.
    //
    Status = _ReadStatusRegister(pInst);
    if (   ((Status & (1uL << STATUS_E_ERR_BIT)) != 0u)
        || ((Status & (1uL << STATUS_P_ERR_BIT)) != 0u)) {          // Is any error present?
      (void)_ClearStatusRegister(pInst);
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _SPANSION_WaitForEndOfOperation: NOR flash reports error 0x%x.", Status));
      r = 1;                                                        // The NOR flash reports an error.
    }
  }
  return r;
}

/*********************************************************************
*
*       _MICROCHIP_ReadSectorBlockInfo
*/
static void _MICROCHIP_ReadSectorBlockInfo(const FS_NOR_SPI_INST * pInst, U32 AddrBPT, U32 AddrVPT, unsigned BlockIndex, FS_NOR_SPI_SECTOR_BLOCK * pSectorBlock) {
  U32 Addr;
  U8  SectorType;
  U8  abData[2];
  U32 NumSectors;
  U8  ldBytesPerSector;
  U8  CmdErase;

  Addr = AddrVPT + OFF_FIRST_SECTOR_BLOCK + (U32)BlockIndex * NUM_BYTES_SECTOR_BLOCK;
  //
  // The type and the number of sectors are stored in the first 2 bytes of a sector block.
  //
  _ReadSFDP(pInst, Addr, abData, sizeof(abData));
  SectorType = abData[0] - 1u;        // The encoded sector type is 1-based.
  NumSectors = 1uL << abData[1];
  if (BlockIndex == 2u) {
    NumSectors -= 2u;                 // The number of sectors in the 3rd sector block is not a power of 2.
  }
  //
  // Read the sector size and the opcode of the erase operation
  //
  Addr = AddrBPT + OFF_FIRST_SECTOR_TYPE + ((U32)SectorType * NUM_BYTES_SECTOR_TYPE);
  _ReadSFDP(pInst, Addr, abData, NUM_BYTES_SECTOR_TYPE);
  ldBytesPerSector = abData[0];
  CmdErase         = abData[1];
  //
  // Store the read data to sector block.
  //
  pSectorBlock->NumSectors       = NumSectors;
  pSectorBlock->CmdErase         = CmdErase;
  pSectorBlock->ldBytesPerSector = ldBytesPerSector;
}

/*********************************************************************
*
*      _MICROCHIP_CalcBitIndex
*/
static U16 _MICROCHIP_CalcBitIndex(I8 c, unsigned m) {
  int      BitIndex;
  unsigned u;

  if (c == 0) {
    BitIndex = 0;
  } else {
    u        = 1uL << m;
    BitIndex = (int)u + 1 + (int)c;
  }
  return (U16)BitIndex;
}

/*********************************************************************
*
*      _MICROCHIP_ClearWriteLockBits
*
*  Function description
*    Clears the write lock bits assigned to the specified address range.
*/
static int _MICROCHIP_ClearWriteLockBits(const FS_NOR_SPI_INST * pInst, U32 Addr, unsigned NumBytes, U8 * pDataBPR, unsigned NumBytesBPR) {
  FS_NOR_SPI_SECTOR_BLOCK SectorBlock;
  unsigned                iBlock;
  U32                     AddrBPT;
  U32                     AddrVPT;
  unsigned                BytesPerSector;
  U32                     AddrStart;
  U32                     AddrEnd;
  U16                     ldBytesPerSector;
  U32                     NumSectors;
  U8                      abData[4];
  U32                     AddrSectorBlock;
  U8                      BitFactor;
  int                     r;
  U32                     NumSectorsBlock;
  U32                     NumBytesBlock;
  U32                     SectorOff;
  U32                     BitStart;
  U32                     BitEnd;
  U32                     AddrDensity;
  U32                     Density;
  unsigned                ldNumBitsDensity;

  r = 1;              // Set to indicate an error.
  FS_MEMSET(&SectorBlock, 0, sizeof(SectorBlock));
  //
  // Get the position and the size of the Basic Parameter Table.
  //
  AddrBPT = _SFDP_GetBPTAddr(pInst, NULL);
  if (AddrBPT != 0u) {
    //
    // Use the device density to calculate the number of bytes in the Block Protection Register.
    //
    AddrDensity = AddrBPT + 0x04u;
    _ReadSFDP(pInst, AddrDensity, abData, sizeof(abData));
    //
    // From the spec:
    //   Density <= 2Gbits: bit 31 is set to 0 and the bits 30:0 define the size in bits
    //   Density >  2Gbits: bit 31 is set to 1 and the bits 30:0 define the size as power of 2
    //
    Density  = (U32)abData[0];
    Density |= (U32)abData[1] << 8;
    Density |= (U32)abData[2] << 16;
    Density |= (U32)abData[3] << 24;
    if ((Density & (1uL << 31)) != 0u) {
      ldNumBitsDensity = Density & ~(1uL << 31);
    } else {
      ldNumBitsDensity = _ld(Density + 1u);
    }
    if (ldNumBitsDensity == 24u) {      // 16 Mbit
      NumBytesBPR = 6;
    } else {
      if (ldNumBitsDensity == 25u) {    // 32 Mbit
        NumBytesBPR = 10;
      }
    }
    //
    // Get the address of the table that contains the vendor specific parameters.
    // The device geometry is stored in this table.
    //
    AddrVPT = _SFDP_GetVPTAddr(pInst, MFG_ID_MICROCHIP, NULL);
    if (AddrVPT != 0u) {
      //
      // A factor is used to compute the index of the bits in the Block-Protection register.
      // The factor is stored in the 3rd sector block entry as the number of sectors value.
      //
      AddrSectorBlock = AddrVPT + OFF_FIRST_SECTOR_BLOCK + 2u * NUM_BYTES_SECTOR_BLOCK + 1u;
      _ReadSFDP(pInst, AddrSectorBlock, abData, 1);
      BitFactor = abData[0];
      //
      // Loop over all sector blocks and clear the write lock bits
      // of the specified address range.
      //
      AddrStart = 0;
      for (iBlock = 0; iBlock < NUM_SECTOR_BLOCKS; ++iBlock) {
        _MICROCHIP_ReadSectorBlockInfo(pInst, AddrBPT, AddrVPT, iBlock, &SectorBlock);
        ldBytesPerSector = SectorBlock.ldBytesPerSector;
        NumSectorsBlock  = SectorBlock.NumSectors;
        BytesPerSector   = 1uL << ldBytesPerSector;
        NumBytesBlock    = NumSectorsBlock << ldBytesPerSector;
        AddrEnd          = AddrStart + NumBytesBlock;
        if ((Addr >= AddrStart) && (Addr < AddrEnd)) {
          //
          // Calculate the index of the first sector and the number of sectors
          // mapped to the specified address range.
          //
          NumBytesBlock = AddrEnd - Addr;
          SectorOff     = (Addr - AddrStart) >> ldBytesPerSector;       // Offset of the sector within the sector block.
          NumBytesBlock = SEGGER_MIN(NumBytesBlock, NumBytes);
          NumSectors    = (NumBytesBlock + (BytesPerSector - 1u)) >> ldBytesPerSector;
          //
          // Read the information about the lock bits assigned to this sector block.
          // Start and end bit index is stored in the last 2 bytes of a sector block.
          //
          AddrSectorBlock = AddrVPT + OFF_FIRST_SECTOR_BLOCK + (U32)iBlock * NUM_BYTES_SECTOR_BLOCK + 2u;
          //
          // The type and the number of sectors are stored in the first 2 bytes of a sector block.
          //
          _ReadSFDP(pInst, AddrSectorBlock, abData, 2);
          BitStart = _MICROCHIP_CalcBitIndex((I8)abData[0], BitFactor);
          BitEnd   = _MICROCHIP_CalcBitIndex((I8)abData[1], BitFactor);
          if ((BitEnd - BitStart) > NumSectorsBlock) {
            //
            // Number of bits is larger than the actual number of sectors in the sector block.
            // This is the case when the sector block has read lock bits. The write lock bits
            // are stored at even bit indexes.
            //
            BitStart += SectorOff << 1;
            do {
              _ClearBits(pDataBPR, BitStart, BitStart, NumBytesBPR);
              BitStart += 2u;
            } while (--NumSectors != 0u);
          } else {
            BitStart += SectorOff;
            BitEnd    = BitStart + NumSectors - 1u;
            _ClearBits(pDataBPR, BitEnd, BitStart, NumBytesBPR);
          }
          NumBytes -= NumBytesBlock;
          Addr     += NumBytesBlock;
        }
        if (NumBytes == 0u) {
          break;
        }
        AddrStart = AddrEnd;
      }
      r = 0;
    }
  }
  return r;
}

/*********************************************************************
*
*       _MICROCHIP_ReadApplyDeviceGeometry
*/
static int _MICROCHIP_ReadApplyDeviceGeometry(FS_NOR_SPI_INST * pInst, U32 AddrBPT, U32 AddrVPT, unsigned NumBytesVPT) {
  U16                       ldNumBits;
  U32                       Density;
  U32                       Addr;
  U32                       AddrType;
  U8                        abData[4];
  U8                        ldBytesPerSector;
  U8                        CmdErase;
  U32                       NumSectors;
  U8                        NumSectorBlocks;
  U8                        NumBytesAddr;
  unsigned                  iBlock;
  U8                        SectorType;
  FS_NOR_SPI_SECTOR_BLOCK * pSectorBlock;

  //
  // Read the device density.
  //
  Addr = AddrBPT + 0x04u;
  _ReadSFDP(pInst, Addr, abData, sizeof(abData));
  //
  // From the spec:
  //   Density <= 2Gbits: bit 31 is set to 0 and the bits 30:0 define the size in bits
  //   Density >  2Gbits: bit 31 is set to 1 and the bits 30:0 define the size as power of 2
  //
  Density  = (U32)abData[0];
  Density |= (U32)abData[1] << 8;
  Density |= (U32)abData[2] << 16;
  Density |= (U32)abData[3] << 24;
  if ((Density & (1uL << 31)) != 0u) {
    ldNumBits = (U16)(Density & ~(1uL << 31));
  } else {
    ldNumBits = (U16)_ld(Density + 1u);
  }
  //
  // Determine the location and size of the erase blocks.
  //
  if (NumBytesVPT > OFF_FIRST_SECTOR_BLOCK) {
    pSectorBlock    = pInst->aSectorBlock;
    NumSectorBlocks = 0;
    Addr            = AddrVPT + OFF_FIRST_SECTOR_BLOCK;
    //
    // Go through all sector blocks an collect information about the sector size
    // and the number of adjacent sectors with the same size.
    //
    for (iBlock = 0; iBlock < NUM_SECTOR_BLOCKS; ++iBlock) {
      //
      // The type and the number of sectors are stored in the first 2 bytes of a sector block.
      //
      _ReadSFDP(pInst, Addr, abData, 2);
      SectorType = abData[0] - 1u;        // The encoded sector type is 1-based.
      NumSectors = 1uL << abData[1];
      if (iBlock == 2u) {
        NumSectors -= 2u;                 // The number of sectors in the 3rd sector block is not a power of 2.
      }
      //
      // Read the sector size and the opcode of the erase operation
      //
      AddrType = AddrBPT + OFF_FIRST_SECTOR_TYPE + ((U32)SectorType * NUM_BYTES_SECTOR_TYPE);
      _ReadSFDP(pInst, AddrType, abData, NUM_BYTES_SECTOR_TYPE);
      ldBytesPerSector = abData[0];
      CmdErase         = abData[1];
      //
      // Store the read data to sector block.
      //
      pSectorBlock->NumSectors       = NumSectors;
      pSectorBlock->CmdErase         = CmdErase;
      pSectorBlock->ldBytesPerSector = ldBytesPerSector;
      ++NumSectorBlocks;
      ++pSectorBlock;
      Addr += NUM_BYTES_SECTOR_BLOCK;
    }
    pInst->NumSectorBlocks = NumSectorBlocks;
  }
  //
  // Determine the number of address bytes.
  //
  NumBytesAddr = 3;
  if (ldNumBits > 27u) {          // Devices with a capacity greater than 128Mbit require a 4 byte address.
    NumBytesAddr = 4;
  }
  //
  // Save the geometry info to instance.
  //
  pInst->NumBytesAddr = NumBytesAddr;
  return 0;                       // OK, device geometry determined.
}

/*********************************************************************
*
*       _MICRON_Identify
*/
static int _MICRON_Identify(FS_NOR_SPI_INST * pInst, const U8 * pId) {
  int      r;
  unsigned MfgId;
  unsigned DeviceId;

  FS_USE_PARA(pInst);
  r        = 1;               // Set to indicate an error.
  MfgId    = pId[0];
  DeviceId = pId[2];
  if (MfgId == MFG_ID_MICRON) {
    //
    // The following Micron devices have a FLAG STATUS register
    // which contains flags that indicate if a program or erase operation failed.
    // If set these flags have to be cleared otherwise the device ignores the next
    // erase or program operation that may cause a data loss.
    //
    // Id               Device
    // -----------------------
    // 0x20 0x.. 0x16   Micron N25Q032A
    // 0x20 0x.. 0x17   Micron N25Q064A
    // 0x20 0x.. 0x18   Micron N25Q128A
    // 0x20 0x.. 0x19   Micron N25Q256A
    // 0x20 0x.. 0x20   Micron N25Q512A
    // 0x20 0x.. 0x21   Micron MT25QL01GB, MT25QU01GAB, N25Q00AA
    // 0x20 0x.. 0x22   Micron MT25QL02GC, MT25QU02GAB
    //
    if ((DeviceId >= 0x16u) && (DeviceId <= 0x22u)) {
      r = 0;
    }
  }
  return r;
}

/*********************************************************************
*
*       _MICRON_Identify_x2
*/
static int _MICRON_Identify_x2(FS_NOR_SPI_INST * pInst, const U8 * pId) {
  int      r;
  unsigned MfgId0;
  unsigned MfgId1;
  unsigned DeviceId0;
  unsigned DeviceId1;

  FS_USE_PARA(pInst);
  r         = 1;               // Set to indicate an error.
  MfgId0    = pId[0];
  MfgId1    = pId[1];
  DeviceId0 = pId[4];
  DeviceId1 = pId[5];
  if (   (MfgId0 == MFG_ID_MICRON)
      && (MfgId1 == MFG_ID_MICRON)
      && (DeviceId0 == DeviceId1)) {
    //
    // The following Micron devices have a FLAG STATUS register
    // which contains flags that indicate if a program or erase operation failed.
    // If set these flags have to be cleared otherwise the device ignores the next
    // erase or program operation that may cause a data loss.
    //
    // Id               Device
    // -----------------------
    // 0x20 0x.. 0x16   Micron N25Q032A
    // 0x20 0x.. 0x17   Micron N25Q064A
    // 0x20 0x.. 0x18   Micron N25Q128A
    // 0x20 0x.. 0x19   Micron N25Q256A
    // 0x20 0x.. 0x20   Micron N25Q512A
    // 0x20 0x.. 0x21   Micron MT25QL01GB, MT25QU01GAB, N25Q00AA
    // 0x20 0x.. 0x22   Micron MT25QL02GC, MT25QU02GAB
    //
    if ((DeviceId0 >= 0x16u) && (DeviceId0 <= 0x22u)) {
      pInst->IsDualDeviceMode = 1;
      r = 0;                  // OK, we support this device.
    }
  }
  return r;
}

/*********************************************************************
*
*       _MICRON_SetBusWidth
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
static int _MICRON_SetBusWidth(FS_NOR_SPI_INST * pInst) {
  U8       Config;
  int      r;
  unsigned Data;
  unsigned BusWidth;

  r        = 0;         // Set to indicate success.
  BusWidth = pInst->BusWidthRead;
  BusWidth = FS_BUSWIDTH_GET_DATA(BusWidth);
  //
  // Make sure that the DQ3 line is used for the data transfer and not as HOLD or RESET signal.
  //
  if (BusWidth == 4u) {
    Config = _ReadEnhancedConfigRegister(pInst);
    if ((Config & (1uL << CONFIG_HOLD_BIT)) != 0u) {
      Data    = Config;
      Data   &= ~(1uL << CONFIG_HOLD_BIT);
      Config  = (U8)Data;
      r = _WriteEnhancedConfigRegister(pInst, Config);
    }
  }
  return r;
}

/*********************************************************************
*
*       _MICRON_SetNumBytesAddr
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
static int _MICRON_SetNumBytesAddr(const FS_NOR_SPI_INST * pInst) {
  int      r;
  unsigned Status;
  unsigned NumBytes;

  NumBytes = pInst->NumBytesAddr;
  //
  // For some 512 Mbit devices the write enable operation has to be performed
  // first before switching the address mode.
  //
  r = _EnableWrite(pInst);
  if (r == 0) {
    if (NumBytes == 4u) {
      r = _Enter4ByteAddrMode(pInst);
      if (r == 0) {
        Status = _ReadFlagStatusRegister(pInst);
        if ((Status & (1uL << FLAG_STATUS_ADDR_BIT)) == 0u) {
          r = 1;        // Could not switch to 4-byte address mode.
        }
      }
    } else {
      r = _Exit4ByteAddrMode(pInst);
      if (r == 0) {
        Status = _ReadFlagStatusRegister(pInst);
        //
        // On devices which use only 3-byte address the FLAG_STATUS_ADDR_BIT is reserved.
        // Since the reserved flags are set to 0 the test below should work for these devices too.
        //
        if ((Status & (1uL << FLAG_STATUS_ADDR_BIT)) != 0u) {
          r = 1;        // Could not switch to 3-byte address mode.
        }
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _MICRON_SetNumBytesAddr_x2
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
static int _MICRON_SetNumBytesAddr_x2(const FS_NOR_SPI_INST * pInst) {
  int      r;
  U8       abStatus[2];
  unsigned NumBytes;

  NumBytes = pInst->NumBytesAddr;
  FS_MEMSET(abStatus, 0, sizeof(abStatus));
  //
  // For some 512 Mbit devices the write enable operation has to be performed
  // first before switching the address mode.
  //
  r = _EnableWrite_x2(pInst);
  if (r == 0) {
    if (NumBytes == 4u) {
      r = _Enter4ByteAddrMode(pInst);
      if (r == 0) {
        _ReadFlagStatusRegister_x2(pInst, abStatus);
        if (   ((abStatus[0] & (1u << FLAG_STATUS_ADDR_BIT)) == 0u)
            || ((abStatus[1] & (1u << FLAG_STATUS_ADDR_BIT)) == 0u)) {
          r = 1;        // Could not switch to 4-byte address mode.
        }
      }
    } else {
      r = _Exit4ByteAddrMode(pInst);
      if (r == 0) {
        _ReadFlagStatusRegister_x2(pInst, abStatus);
        //
        // On devices which use only 3-byte address the FLAG_STATUS_ADDR_BIT is reserved.
        // Since the reserved flags are set to 0 the test below should work for these devices too.
        //
        if (   ((abStatus[0] & (1u << FLAG_STATUS_ADDR_BIT)) != 0u)
            || ((abStatus[1] & (1u << FLAG_STATUS_ADDR_BIT)) != 0u)) {
          r = 1;        // Could not switch to 3-byte address mode.
        }
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _MICRON_WaitForEndOfOperation
*
*  Function description
*    Waits for a NOR flash operation to complete.
*
*  Parameters
*    pInst        Driver instance.
*    pPollPara    [IN] Parameters of the polling operation.
*
*  Return value
*    ==0      OK, operation completed successfully.
*    !=0      An error occurred.
*/
static int _MICRON_WaitForEndOfOperation(const FS_NOR_SPI_INST * pInst, const FS_NOR_SPI_POLL_PARA * pPollPara) {
  int      r;
  int      rPoll;
  unsigned Status;
  U32      TimeOut;

  r       = 1;            // Set to indicate an error.
  Status  = 0;
  TimeOut = pPollPara->TimeOut;
  //
  // Prefer polling the status register in the hardware if supported.
  //
  rPoll = _PollFlagStatusRegister(pInst, FLAG_STATUS_READY_BIT, pPollPara);
  if (rPoll > 0) {            // Timeout reported?
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _MICRON_WaitForEndOfOperation: Timeout expired."));
  } else {
    if (rPoll < 0) {          // Is the feature not supported?
      //
      // Poll the status register in the software.
      //
      for (;;) {
        //
        // Read the status from NOR device.
        //
        Status = _ReadFlagStatusRegister(pInst);
        if ((Status & (1uL << FLAG_STATUS_READY_BIT)) != 0u) {
          r = 0;                                      // OK, the NOR flash is ready for a new operation.
          break;
        }
        --TimeOut;                                    // We have executed one wait cycle.
        //
        // If possible, delay the next request to reduce CPU usage.
        //
        TimeOut = _DelayPoll(pInst, TimeOut, pPollPara);
        if (TimeOut == 0u) {
          FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _MICRON_WaitForEndOfOperation: Timeout expired."));
          break;
        }
      }
    } else {
      Status = _ReadFlagStatusRegister(pInst);
      r      = 0;
    }
  }
  if (r == 0) {
    //
    // Check and clear the error flags if necessary.
    //
    if (   ((Status & (1uL << FLAG_STATUS_PROT_ERROR_BIT))  != 0u)
        || ((Status & (1uL << FLAG_STATUS_VPP_ERROR_BIT))   != 0u)
        || ((Status & (1uL << FLAG_STATUS_PROG_ERROR_BIT))  != 0u)
        || ((Status & (1uL << FLAG_STATUS_ERASE_ERROR_BIT)) != 0u)) {  // Is an error present?
      (void)_ClearFlagStatusRegister(pInst);
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _MICRON_WaitForEndOfOperation: NOR flash reports error 0x%x.", Status));
      r = 1;                                      // The NOR flash has reported an error.
    }
  }
  return r;
}

/*********************************************************************
*
*       _MICRON_WaitForEndOfOperation_x2
*
*  Function description
*    Waits for a NOR flash operation to complete.
*
*  Parameters
*    pInst        Driver instance.
*    pPollPara    [IN] Parameters of the polling operation.
*
*  Return value
*    ==0      OK, operation completed successfully.
*    !=0      An error occurred.
*/
static int _MICRON_WaitForEndOfOperation_x2(const FS_NOR_SPI_INST * pInst, const FS_NOR_SPI_POLL_PARA * pPollPara) {
  int r;
  int rPoll;
  U8  abStatus[2];
  U32 TimeOut;

  r       = 1;            // Set to indicate an error.
  TimeOut = pPollPara->TimeOut;
  FS_MEMSET(abStatus, 0, sizeof(abStatus));
  //
  // Prefer polling the status register in the hardware if supported.
  //
  rPoll = _PollFlagStatusRegister(pInst, FLAG_STATUS_READY_BIT, pPollPara);
  if (rPoll > 0) {            // Timeout reported?
    //
    // Error, polling timeout expired.
    //
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _MICRON_WaitForEndOfOperation_x2: Timeout expired."));
  } else {
    if (rPoll < 0) {          // Is the feature not supported?
      //
      // Poll the status register in the software.
      //
      for (;;) {
        //
        // Read the status from NOR device.
        //
        _ReadFlagStatusRegister_x2(pInst, abStatus);
        if (   ((abStatus[0] & (1u << FLAG_STATUS_READY_BIT)) != 0u)
            && ((abStatus[1] & (1u << FLAG_STATUS_READY_BIT)) != 0u)) {
          r = 0;                                      // OK, both NOR flash devices are ready for a new operation.
          break;
        }
        --TimeOut;                                    // We have executed one wait cycle.
        //
        // If possible, delay the next request to reduce CPU usage.
        //
        TimeOut = _DelayPoll(pInst, TimeOut, pPollPara);
        if (TimeOut == 0u) {
          //
          // Error, polling timeout expired. Quit the waiting loop.
          //
          FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _MICRON_WaitForEndOfOperation_x2: Timeout expired."));
          break;
        }
      }
    } else {
      _ReadFlagStatusRegister_x2(pInst, abStatus);
      r = 0;
    }
  }
  if (r == 0) {
    //
    // Check and clean the error flags if necessary.
    //
    if (   ((abStatus[0] & (1u << FLAG_STATUS_PROT_ERROR_BIT))  != 0u)
        || ((abStatus[0] & (1u << FLAG_STATUS_VPP_ERROR_BIT))   != 0u)
        || ((abStatus[0] & (1u << FLAG_STATUS_PROG_ERROR_BIT))  != 0u)
        || ((abStatus[0] & (1u << FLAG_STATUS_ERASE_ERROR_BIT)) != 0u)
        || ((abStatus[1] & (1u << FLAG_STATUS_PROT_ERROR_BIT))  != 0u)
        || ((abStatus[1] & (1u << FLAG_STATUS_VPP_ERROR_BIT))   != 0u)
        || ((abStatus[1] & (1u << FLAG_STATUS_PROG_ERROR_BIT))  != 0u)
        || ((abStatus[1] & (1u << FLAG_STATUS_ERASE_ERROR_BIT)) != 0u)) {   // Is an error present?
      (void)_ClearFlagStatusRegister(pInst);
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _MICRON_WaitForEndOfOperation: NOR flash reports error 0x%x 0x%x.", abStatus[0], abStatus[1]));
      r = 1;                                      // The NOR flash has reported an error.
    }
  }
  return r;
}

/*********************************************************************
*
*       _MICROCHIP_Identify
*/
static int _MICROCHIP_Identify(FS_NOR_SPI_INST * pInst, const U8 * pId) {
  int      r;
  unsigned MfgId;
  unsigned DeviceId;

  FS_USE_PARA(pInst);
  r        = 1;               // Set to indicate an error.
  MfgId    = pId[0];
  DeviceId = pId[2];
  if (MfgId == MFG_ID_MICROCHIP) {
    //
    // The following Microchip devices support SFDP.
    //
    // Id               Device
    // -----------------------
    // 0xBF 0x.. 0x41   Microchip SST26VF016B
    // 0xBF 0x.. 0x42   Microchip SST26VF032B, SST26VF032BA
    // 0xBF 0x.. 0x43   Microchip SST26VF064B
    //
    if ((DeviceId >= 0x41u) && (DeviceId <= 0x43u)) {
      r = 0;
    }
  }
  return r;
}

/*********************************************************************
*
*       _MICROCHIP_SetBusWidth
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
static int _MICROCHIP_SetBusWidth(FS_NOR_SPI_INST * pInst) {
  U8       abRegData[2];
  int      r;
  unsigned Data;
  unsigned BusWidth;

  BusWidth = pInst->BusWidthRead;
  BusWidth = FS_BUSWIDTH_GET_DATA(BusWidth);
  //
  // We have to read the status register here because
  // the write command has to write first the status and
  // then the configuration register.
  //
  abRegData[0] = (U8)_ReadStatusRegister(pInst);
  abRegData[1] = (U8)_ReadConfigRegister(pInst);
  switch (BusWidth) {
  default:
  case 1:
    // through
  case 2:
    Data = abRegData[1];
    Data &= ~(1uL << CONFIG_IOC_BIT);
    abRegData[1] = (U8)Data;
    break;
  case 4:
    abRegData[1] |= 1u << CONFIG_IOC_BIT;
    break;
  }
  r = _WriteStatusRegister(pInst, abRegData, sizeof(abRegData));
  return r;
}

/*********************************************************************
*
*       _MICROCHIP_ReadApplyPara
*/
static int _MICROCHIP_ReadApplyPara(FS_NOR_SPI_INST * pInst) {
  int      r;
  U32      AddrBPT;
  U32      AddrVPT;
  unsigned NumBytes;

  //
  // Check if the device supports SFDP.
  //
  r = _SFDP_IsSupported(pInst);
  if (r == 0) {
    return 1;                       // Error, device does not support SFDP.
  }
  //
  // OK, the device supports SFDP.
  // Get the position and the size of the Basic Parameter Table.
  //
  AddrBPT = _SFDP_GetBPTAddr(pInst, NULL);
  if (AddrBPT == 0u) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _MICROCHIP_ReadApplyPara: Could not get BPT address."));
    return 1;                       // Error, BPT not found or invalid.
  }
  //
  // Get the address of the table that contains the vendor specific parameters.
  // The device geometry is stored in this table.
  //
  AddrVPT = _SFDP_GetVPTAddr(pInst, MFG_ID_MICROCHIP, &NumBytes);
  if (AddrVPT == 0u) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _MICROCHIP_ReadApplyPara: Could not get VPT address."));
    return 1;                       // Error, VPT not found or invalid.
  }
  //
  // Determine the device geometry.
  //
  r = _MICROCHIP_ReadApplyDeviceGeometry(pInst, AddrBPT, AddrVPT, NumBytes);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _MICROCHIP_ReadApplyPara: Could not get device geometry."));
    return 1;                       // Error, could not determine the device geometry.
  }
  //
  // Determine the most suitable read command to use.
  //
  r = _SFDP_ReadApplyReadMode(pInst, AddrBPT);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _MICROCHIP_ReadApplyPara: Could not get read mode."));
    return 1;                       // Error, could not determine the read mode.
  }
  return 0;                         // OK, device identified.
}

/*********************************************************************
*
*      _MICROCHIP_RemoveWriteProtection
*
*  Function description
*    Makes all physical sectors writable.
*
*  Parameters
*    pInst        Driver instance.
*    Addr         Address of the first byte in the range.
*    NumBytes     Number of bytes in the range.
*
*  Return value
*    ==0      OK, write protection successfully removed.
*    !=0      An error occurred.
*
*  Notes
*    (1) The protection removal operation fails if the WPEN bit
*        in the Configuration Register is set to 1 and the WP#
*        pin is set to LOW. In addition, the Configuration Register
*        can be modified only if WP# pin is set to HIGH. The user
*        has to make sure that the WPEN bit in the Configuration
*        Register is set to 0 otherwise this function is not able
*        to remove the write protection of the physical sectors
*        used as storage.
*/
static int _MICROCHIP_RemoveWriteProtection(const FS_NOR_SPI_INST * pInst, U32 Addr, U32 NumBytes) {
  int r;
  U8  abData[18];

#if FS_SUPPORT_TEST
  //
  // Purposely lock all the physical sectors to check if the unlock procedure works.
  //
  FS_MEMSET(abData, 0xFF, sizeof(abData));
  //
  // We enable only the write protection for the physical sectors that also have a read protection
  // because when the read protection is enabled the NOR flash device does not return any SFDP information.
  //
  abData[0] = 0x55;
  abData[1] = 0x55;
  r = _WriteBlockProtectionRegister(pInst, abData, sizeof(abData));
  if (r == 0)
#endif // FS_SUPPORT_TEST
  {
    _ReadBlockProtectionRegister(pInst, abData, sizeof(abData));
    r = _MICROCHIP_ClearWriteLockBits(pInst, Addr, NumBytes, abData, sizeof(abData));
    if (r == 0) {
      r = _WriteBlockProtectionRegister(pInst, abData, sizeof(abData));     // Note 1
    }
  }
  return r;
}

/*********************************************************************
*
*       _WINBOND_Identify
*
*  Function description
*    Identifies a Winbond NOR flash device with SFDP support.
*
*  Parameters
*    pInst      Driver instance.
*    pId        Device id.
*
*  Return value
*    ==0      This is a matching device.
*    !=0      This is not a matching device.
*/
static int _WINBOND_Identify(FS_NOR_SPI_INST * pInst, const U8 * pId) {
  int      r;
  unsigned MfgId;
  unsigned DeviceId;

  FS_USE_PARA(pInst);
  r        = 1;               // Set to indicate an error.
  MfgId    = pId[0];
  DeviceId = pId[2];
  if (MfgId == MFG_ID_WINBOND) {
    //
    // The following Winbond devices support SFDP.
    //
    // Id               Device
    // -----------------------
    // 0xEF 0x.. 0x14   Winbond W25Q80EW
    // 0xEF 0x.. 0x15   Winbond W25Q16DV, W25Q16JV
    // 0xEF 0x.. 0x16   Winbond W25Q32JV
    // 0xEF 0x.. 0x17   Winbond W25Q64FW, W25Q64JV
    // 0xEF 0x.. 0x18   Winbond W25Q128FW
    // 0xEF 0x.. 0x19   Winbond W25Q256JV
    // 0xEF 0x.. 0x20   Winbond W25Q512JV
    //
    if ((DeviceId >= 0x15u) && (DeviceId <= 0x20u)) {
      r = 0;
    }
  }
  return r;
}

/*********************************************************************
*
*       _WINBOND_IdentifyDTR
*
*  Function description
*    Identifies a Winbond NOR flash device with support for SFDP and DTR.
*
*  Parameters
*    pInst      Driver instance.
*    pId        Device id.
*
*  Return value
*    ==0      This is a matching device.
*    !=0      This is not a matching device.
*/
static int _WINBOND_IdentifyDTR(FS_NOR_SPI_INST * pInst, const U8 * pId) {
  int      r;
  unsigned MfgId;
  unsigned DeviceId1;
  unsigned DeviceId2;

  FS_USE_PARA(pInst);
  r         = 1;            // Set to indicate an error.
  MfgId     = pId[0];
  DeviceId1 = pId[1];
  DeviceId2 = pId[2];
  if (MfgId == MFG_ID_WINBOND) {
    //
    // The following Winbond devices support SFDP and DTR.
    //
    // Id               Device
    // -----------------------
    // 0xEF 0x70 0x17   Winbond W25Q64JV
    // 0xEF 0x70 0x18   Winbond W25Q128JV
    // 0xEF 0x80 0x19   Winbond W25Q256JW
    // 0xEF 0x70 0x20   Winbond W25Q512JV
    //
    if (pInst->AllowDTRMode != 0u) {
      if (DeviceId1 == 0x70u) {
        if (   (DeviceId2 == 0x17u)
            || (DeviceId2 == 0x18u)
            || (DeviceId2 == 0x20u)) {
          r = 0;
        }
      } else {
        if (DeviceId1 == 0x80u) {
          if (DeviceId2 == 0x19u) {
            r = 0;
          }
        }
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _WINBOND_ReadApplyParaDTR
*
*  Function description
*    Configures the driver using NOR flash device parameters.
*
*  Parameters
*    pInst      Driver instance.
*
*  Return value
*    ==0    OK, successfully configured.
*    !=0    An error occurred.
*/
static int _WINBOND_ReadApplyParaDTR(FS_NOR_SPI_INST * pInst) {
  int      r;
  unsigned Flags;

  r = _SFDP_ReadApplyPara(pInst);
  if (r == 0) {
    //
    // Set the correct codes for the read command in DTR mode.
    // This information is not available via SFDP.
    //
    Flags = 0u                                                   // Only the address and the data are transferred in DTR mode.
          | FS_NOR_HW_FLAG_DTR_ADDR
          | FS_NOR_HW_FLAG_DTR_DATA
          ;
    if (pInst->Allow4bitMode != 0u) {
      //
      // Inform the hardware layer that the first byte after the
      // address contains the bits that enable/disable the Read Command Bypass Mode.
      //
      Flags                    |= FS_NOR_HW_FLAG_MODE_8BIT;
      pInst->CmdRead            = CMD_QUAD_READ_DTR;
      pInst->NumBytesReadDummy  = 8;
      pInst->BusWidthRead       = (U16)FS_BUSWIDTH_MAKE(1uL, 4uL, 4uL);
      pInst->FlagsRead          = (U16)Flags;
    } else if (pInst->Allow2bitMode != 0u) {
      //
      // Inform the hardware layer that the first byte after the
      // address contains the bits that enable/disable the Read Command Bypass Mode.
      //
      Flags                    |= FS_NOR_HW_FLAG_MODE_8BIT;
      pInst->CmdRead            = CMD_DUAL_READ_DTR;
      pInst->NumBytesReadDummy  = 3;
      pInst->BusWidthRead       = (U16)FS_BUSWIDTH_MAKE(1uL, 2uL, 2uL);
      pInst->FlagsRead          = (U16)Flags;
    } else {
      //
      // In this read mode the driver has to generate 6 dummy cycles
      // which is the equivalent of 1.5 bytes. We inform the HW layer
      // that an additional nibble of dummy cycles have to be generated
      // via the Flags parameter of the read function.
      //
      Flags                    |= FS_NOR_HW_FLAG_DUMMY_4BIT;
      pInst->CmdRead            = CMD_READ_DTR;
      pInst->NumBytesReadDummy  = 1;
      pInst->BusWidthRead       = (U16)FS_BUSWIDTH_MAKE(1uL, 1uL, 1uL);
      pInst->FlagsRead          = (U16)Flags;
    }
  }
  return r;
}

/*********************************************************************
*
*       _WINBOND_SetBusWidth
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
static int _WINBOND_SetBusWidth(FS_NOR_SPI_INST * pInst) {
  U8       RegData;
  unsigned Data;
  int      r;
  unsigned BusWidth;

  r        = 0;
  BusWidth = pInst->BusWidthRead;
  BusWidth = FS_BUSWIDTH_GET_DATA(BusWidth);
  RegData  = _ReadStatusRegister2Alt(pInst);
  switch (BusWidth) {
  default:
  case 1:
    // through
  case 2:
    if ((RegData & (1u << STATUS2_QE_BIT)) != 0u) {
      Data     = RegData;
      Data    &= ~(1uL << STATUS2_QE_BIT);
      RegData  = (U8)Data;
      r = _WriteStatusRegister2(pInst, &RegData, sizeof(RegData));
    }
    break;
  case 4:
    if ((RegData & (1u << STATUS2_QE_BIT)) == 0u) {
      RegData |= 1u << STATUS2_QE_BIT;
      r = _WriteStatusRegister2(pInst, &RegData, sizeof(RegData));
    }
    break;
  }
  return r;
}

/*********************************************************************
*
*       _WINBOND_SetNumBytesAddr
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
static int _WINBOND_SetNumBytesAddr(const FS_NOR_SPI_INST * pInst) {
  int      r;
  unsigned NumBytes;

  NumBytes = pInst->NumBytesAddr;
  if (NumBytes == 4u) {
    r = _Enter4ByteAddrMode(pInst);
  } else {
    r = _Exit4ByteAddrMode(pInst);
  }
  return r;
}

/*********************************************************************
*
*       _ISSI_IdentifyEnhanced
*/
static int _ISSI_IdentifyEnhanced(FS_NOR_SPI_INST * pInst, const U8 * pId) {
  int      r;
  unsigned MfgId;
  unsigned DeviceId;
  unsigned Status;

  FS_USE_PARA(pInst);
  r        = 1;               // Set to indicate an error.
  MfgId    = pId[0];
  DeviceId = pId[2];
  if (MfgId == MFG_ID_ISSI) {
    //
    // The following ISSI devices support SFDP and Extended Read Register.
    //
    // Id               Device
    // -----------------------
    // 0x9D 0x.. 0x18   ISSI IS25LP128F, IS25WP128F
    // 0x9D 0x.. 0x19   ISSI IS25LP256D, IS25WP256D
    // 0x9D 0x.. 0x1A   ISSI IS25LP512M, IS25WP512M
    // 0x9D 0x.. 0x1B   ISSI IS25LP01G, IS25WP01G
    //
    if (   (DeviceId == 0x19u)
        || (DeviceId == 0x1Au)
        || (DeviceId == 0x1Bu)) {
      r = 0;
    } else {
      //
      // IS25LP128 reports the same device id as IS25LP128F.
      // In order to differentiate them, we have to check the
      // value returned when reading the Extended Read Register
      // register because this register is present only on IS25LP128F.
      // The ISSI support recommends checking the 5 least significant
      // bits of the returned value. If the returned value is 0x10
      // then we can assume that this is a IS25LP128F device.
      //
      if (DeviceId == 0x18u) {
        Status  = _ReadExtendedReadRegister(pInst);
        Status &= 0x1Fu;
        if (Status == 0x10u) {
          r = 0;
        }
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _ISSI_IdentifyStandard
*/
static int _ISSI_IdentifyStandard(FS_NOR_SPI_INST * pInst, const U8 * pId) {
  int      r;
  unsigned MfgId;
  unsigned DeviceId;

  FS_USE_PARA(pInst);
  r        = 1;               // Set to indicate an error.
  MfgId    = pId[0];
  DeviceId = pId[2];
  if (MfgId == MFG_ID_ISSI) {
    //
    // The following ISSI devices support SFDP and standard status register.
    //
    // Id               Device
    // -----------------------
    // 0x9D 0x.. 0x14   ISSI IS25LQ080B
    // 0x9D 0x.. 0x15   ISSI IS25LQ016B
    // 0x9D 0x.. 0x16   ISSI IS25LQ032B
    // 0x9D 0x.. 0x17   ISSI IS25LP064A
    // 0x9D 0x.. 0x18   ISSI IS25LP128
    //
    if ((DeviceId >= 0x14u) && (DeviceId <= 0x18u)) {
      if (DeviceId == 0x17u) {
        //
        // This device specifies the wrong number of dummy cycles in SFDP
        // so we disable the 1-2-2 read mode here. The 1-1-2 read mode
        // works correctly.
        //
        pInst->ReadModesDisabled = 1u << READ_MODE_122_BIT;
      }
      r = 0;
    }
  }
  return r;
}

/*********************************************************************
*
*       _ISSI_IdentifyLegacy
*/
static int _ISSI_IdentifyLegacy(FS_NOR_SPI_INST * pInst, const U8 * pId) {
  int                       r;
  unsigned                  MfgId;
  unsigned                  DeviceId;
  FS_NOR_SPI_SECTOR_BLOCK * pSectorBlock;

  FS_USE_PARA(pInst);
  r        = 1;               // Set to indicate an error.
  MfgId    = *pId;
  //
  // The ISSI IS25LQ080 device sends 0x7F instead of manufacturer id as first byte.
  // The following byte is the actual manufacturer id.
  //
  if (MfgId == 0x7Fu) {
    MfgId  = pId[1];
  }
  DeviceId = pId[2];
  if (MfgId == MFG_ID_ISSI) {
    //
    // The following ISSI devices do not have support for SFDP.
    // The device geometry is calculated based on the device id.
    //
    // Id               Device
    // -----------------------
    // 0x7F 0x9D 0x44   ISSI IS25LQ080
    //
    if (DeviceId == 0x44u) {
      if (pInst->Allow4bitMode != 0u) {
        pInst->CmdRead           = CMD_QUAD_READ;
        pInst->NumBytesReadDummy = 3;
        pInst->BusWidthRead      = (U16)FS_BUSWIDTH_MAKE(1uL, 4uL, 4uL);
      } else if (pInst->Allow2bitMode != 0u) {
        pInst->CmdRead           = CMD_DUAL_READ;
        pInst->NumBytesReadDummy = 1;
        pInst->BusWidthRead      = (U16)FS_BUSWIDTH_MAKE(1uL, 2uL, 2uL);
      } else {
        pInst->CmdRead           = CMD_FAST_READ;
        pInst->NumBytesReadDummy = 1;
        pInst->BusWidthRead      = (U16)FS_BUSWIDTH_MAKE(1uL, 1uL, 1uL);
      }
      pSectorBlock = &pInst->aSectorBlock[0];
      pSectorBlock->NumSectors       = 16;
      pSectorBlock->ldBytesPerSector = 16;
      pSectorBlock->CmdErase         = CMD_SE;
      pInst->NumSectorBlocks         = 1;
      pInst->NumBytesAddr            = 3;
      r = 0;
    }
  }
  return r;
}

/*********************************************************************
*
*       _ISSI_SetBusWidth
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
static int _ISSI_SetBusWidth(FS_NOR_SPI_INST * pInst) {
  U8       RegData;
  unsigned Data;
  int      r;
  unsigned BusWidth;

  r        = 0;
  BusWidth = pInst->BusWidthRead;
  BusWidth = FS_BUSWIDTH_GET_DATA(BusWidth);
  Data     = _ReadStatusRegister(pInst);
  switch (BusWidth) {
  default:
  case 1:
    // through
  case 2:
    if ((Data & (1uL << STATUS_QE_BIT)) != 0u) {
      Data    &= ~(1uL << STATUS_QE_BIT);
      RegData  = (U8)Data;
      r = _WriteStatusRegister(pInst, &RegData, sizeof(RegData));
    }
    break;
  case 4:
    if ((Data & (1uL << STATUS_QE_BIT)) == 0u) {
      Data    |= 1uL << STATUS_QE_BIT;
      RegData  = (U8)Data;
      r = _WriteStatusRegister(pInst, &RegData, sizeof(RegData));
    }
    break;
  }
  return r;
}

/*********************************************************************
*
*       _ISSI_SetNumBytesAddr
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
static int _ISSI_SetNumBytesAddr(const FS_NOR_SPI_INST * pInst) {
  unsigned Data;
  int      r;
  unsigned NumBytes;

  r        = 0;
  NumBytes = pInst->NumBytesAddr;
  Data     = _ReadBankRegister(pInst);
  if (NumBytes == 4u) {
    if ((Data & (1uL << BAR_EXTADD_BIT)) == 0u) {
      Data |= 1uL << BAR_EXTADD_BIT;
      r = _WriteBankRegister(pInst, (U8)Data);
    }
  } else {
    if ((Data & (1uL << BAR_EXTADD_BIT)) != 0u) {
      Data &= ~(1uL << BAR_EXTADD_BIT);
      r = _WriteBankRegister(pInst, (U8)Data);
    }
  }
  return r;
}

/*********************************************************************
*
*       _ISSI_WaitForEndOfOperation
*
*  Function description
*    Waits for a NOR flash operation to complete.
*
*  Parameters
*    pInst        Driver instance.
*    pPollPara    [IN] Parameters of the polling operation.
*
*  Return value
*    ==0      OK, operation completed successfully.
*    !=0      An error occurred.
*/
static int _ISSI_WaitForEndOfOperation(const FS_NOR_SPI_INST * pInst, const FS_NOR_SPI_POLL_PARA * pPollPara) {
  int      r;
  unsigned Status;
  U32      TimeOut;
  int      rPoll;

  r       = 1;                                                      // Set to indicate error.
  TimeOut = pPollPara->TimeOut;
  //
  // Prefer polling the status register in the hardware if supported.
  //
  rPoll = _PollStatusRegister(pInst, STATUS_BUSY_BIT, pPollPara);
  if (rPoll > 0) {                                                  // Timeout reported?
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _ISSI_WaitForEndOfOperation: Timeout expired."));
  } else {
    if (rPoll < 0) {                                                // Is the feature not supported?
      //
      // Poll the status register in the software.
      //
      for (;;) {
        //
        // Read the status from NOR device.
        //
        Status = _ReadStatusRegister(pInst);
        if ((Status & (1uL << STATUS_BUSY_BIT)) == 0u) {
          r = 0;                                                    // OK, the NOR flash is ready for a new operation.
          break;
        }
        --TimeOut;                                                  // We have executed one wait cycle.
        //
        // If possible, delay the next request to reduce CPU usage.
        //
        TimeOut = _DelayPoll(pInst, TimeOut, pPollPara);
        if (TimeOut == 0u) {
          FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _ISSI_WaitForEndOfOperation: Timeout expired."));
          break;
        }
      }
    } else {
      r = 0;                                                        // OK, the NOR device flash is ready for a new operation.
    }
  }
  if (r == 0) {
    //
    // Check and clear the error flags if necessary.
    //
    Status = _ReadExtendedReadRegister(pInst);
    if (   ((Status & (1uL << EXT_READ_PROT_E_BIT)) != 0u)
        || ((Status & (1uL << EXT_READ_P_ERR_BIT))  != 0u)
        || ((Status & (1uL << EXT_READ_E_ERR_BIT))  != 0u)) {       // Is an error present?
      (void)_ClearExtendedReadRegister(pInst);
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _ISSI_WaitForEndOfOperation: NOR flash reports error 0x%x.", Status));
      r = 1;                                                        // The NOR flash device reports an error.
    }
  }
  return r;
}

/*********************************************************************
*
*       _MACRONIX_Identify
*/
static int _MACRONIX_Identify(FS_NOR_SPI_INST * pInst, const U8 * pId) {
  int      r;
  unsigned MfgId;
  unsigned DeviceId;

  FS_USE_PARA(pInst);
  r        = 1;               // Set to indicate an error.
  MfgId    = pId[0];
  DeviceId = pId[2];
  if (MfgId == MFG_ID_MACRONIX) {
    //
    // The following Macronix devices have a security register that contains
    // flags which indicate if a program or erase operation failed.
    //
    // Id               Device
    // -----------------------
    // 0xC2 0x.. 0x15   Macronix MX25V1635
    // 0xC2 0x.. 0x16   Macronix MX25L3233
    // 0xC2 0x.. 0x17   Macronix MX25R64
    // 0xC2 0x.. 0x18   Macronix MX25L128
    // 0xC2 0x.. 0x19   Macronix MX25L256
    // 0xC2 0x.. 0x1A   Macronix MX25L512
    // 0xC2 0x.. 0x39   Macronix MX25U256
    // 0xC2 0x.. 0x3C   Macronix MX66L2G45G
    //
    if (   (DeviceId == 0x15u)
        || (DeviceId == 0x16u)
        || (DeviceId == 0x17u)
        || (DeviceId == 0x18u)
        || (DeviceId == 0x19u)
        || (DeviceId == 0x1Au)
        || (DeviceId == 0x39u)
        || (DeviceId == 0x3Cu)) {
      r = 0;
    }
  }
  return r;
}

/*********************************************************************
*
*       _MACRONIX_IdentifyOctal
*
*  Function description
*    Identifies a Macronix serial NOR flash with octal interface in single SPI mode.
*
*  Parameters
*    pInst      Driver instance.
*    pId        [IN] Device identification.
*
*  Return value
*    ==0    This is a Macronix device.
*    !=0    This is not a Macronix device.
*/
static int _MACRONIX_IdentifyOctal(FS_NOR_SPI_INST * pInst, const U8 * pId) {
  int                       r;
  unsigned                  MfgId;
  unsigned                  DeviceId;
  FS_NOR_SPI_SECTOR_BLOCK * pSectorBlock;

  FS_USE_PARA(pInst);
  r        = 1;               // Set to indicate an error.
  MfgId    = pId[0];
  DeviceId = pId[2];
  if (MfgId == MFG_ID_MACRONIX) {
    //
    // The following Macronix devices have a security register that contains
    // flags which indicate if a program or erase operation failed. In addition,
    // these devices do not have support for SFDP.
    //
    // Id               Device
    // -----------------------
    // 0xC2 0x.. 0x3A   Macronix MX25LM51245G
    //
    if (DeviceId == 0x3Au) {
      if (pInst->AllowOctalMode == 0u) {
        //
        // Configure commands for single SPI mode.
        //
        pInst->CmdRead           = CMD_FAST_READ4B;
        pInst->NumBytesReadDummy = 1;
        pInst->BusWidthRead      = (U16)FS_BUSWIDTH_MAKE(1uL, 1uL, 1uL);
        pInst->CmdWrite          = CMD_PP4B;
        pInst->BusWidthWrite     = (U16)FS_BUSWIDTH_MAKE(1uL, 1uL, 1uL);
        //
        // Configure device organization.
        //
        pSectorBlock = &pInst->aSectorBlock[0];
        pSectorBlock->NumSectors       = 1024;
        pSectorBlock->ldBytesPerSector = 16;
        pSectorBlock->CmdErase         = CMD_SE4B;
        pInst->NumSectorBlocks         = 1;
        pInst->NumBytesAddr            = 4;
        r = 0;
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _MACRONIX_OCTAL_IdentifySTR
*
*  Function description
*    Identifies a Macronix serial NOR flash device with octal interface in STR octal mode.
*
*  Parameters
*    pInst      Driver instance.
*    pId        [IN] Device identification.
*
*  Return value
*    ==0    This is the Macronix device we are looking for.
*    !=0    This is another device.
*
*  Additional information
*    The device will be switched to OPI STR mode during initialization.
*/
static int _MACRONIX_OCTAL_IdentifySTR(FS_NOR_SPI_INST * pInst, const U8 * pId) {
  int                       r;
  unsigned                  MfgId;
  unsigned                  DeviceId;
  FS_NOR_SPI_SECTOR_BLOCK * pSectorBlock;
  unsigned                  ReadCmd;

  FS_USE_PARA(pInst);
  r        = 1;                           // Set to indicate an error.
  MfgId    = pId[0];
  DeviceId = pId[2];
  if (MfgId == MFG_ID_MACRONIX) {
    //
    // The following Macronix devices have a security register that contains
    // flags which indicate if a program or erase operation failed. In addition,
    // these devices do not have support for SFDP.
    //
    // Id               Device
    // -----------------------
    // 0xC2 0x.. 0x3A   Macronix MX25LM51245G
    //
    if (DeviceId == 0x3Au) {
      if (   (pInst->AllowOctalMode != 0u)
          && (pInst->AllowDTRMode   == 0u)) {
        //
        // Configure commands for OPI STR mode.
        //
        ReadCmd                  = CMD_8READ;
        pInst->CmdRead           = (U8)ReadCmd;
        pInst->CmdReadEx         = (U8)(~ReadCmd);
        pInst->NumBytesReadDummy = NUM_CYCLES_DUMMY_DEFAULT;
        pInst->BusWidthRead      = (U16)FS_BUSWIDTH_MAKE(8uL, 8uL, 8uL);
        pInst->CmdWrite          = CMD_PP4B;
        pInst->BusWidthWrite     = (U16)FS_BUSWIDTH_MAKE(8uL, 8uL, 8uL);
        //
        // Configure device organization.
        //
        pSectorBlock = &pInst->aSectorBlock[0];
        pSectorBlock->NumSectors       = 1024;
        pSectorBlock->ldBytesPerSector = 16;
        pSectorBlock->CmdErase         = CMD_SE4B;
        pInst->NumSectorBlocks         = 1;
        pInst->NumBytesAddr            = 4;
        r = 0;
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _MACRONIX_OCTAL_IdentifyDTR
*
*  Function description
*    Identifies a Macronix serial NOR flash device with octal interface in DTR octal mode.
*
*  Parameters
*    pInst      Driver instance.
*    pId        [IN] Device identification.
*
*  Return value
*    ==0    This is the Macronix device we are looking for.
*    !=0    This is another device.
*
*  Additional information
*    The device will be switched to OPI DTR mode during initialization.
*/
static int _MACRONIX_OCTAL_IdentifyDTR(FS_NOR_SPI_INST * pInst, const U8 * pId) {
  int                       r;
  unsigned                  MfgId;
  unsigned                  DeviceId;
  FS_NOR_SPI_SECTOR_BLOCK * pSectorBlock;
  unsigned                  ReadCmd;
  unsigned                  Flags;

  FS_USE_PARA(pInst);
  r        = 1;                           // Set to indicate an error.
  MfgId    = pId[0];
  DeviceId = pId[2];
  if (MfgId == MFG_ID_MACRONIX) {
    //
    // The following Macronix devices have a security register that contains
    // flags which indicate if a program or erase operation failed. In addition,
    // these devices do not have support for SFDP.
    //
    // Id               Device
    // -----------------------
    // 0xC2 0x.. 0x3A   Macronix MX25LM51245G
    //
    if (DeviceId == 0x3Au) {
      if (   (pInst->AllowOctalMode != 0u)
          && (pInst->AllowDTRMode   != 0u)) {
        //
        // Configure commands for OPI DTR mode.
        //
        ReadCmd                  = CMD_8DTRD;
        Flags                    = 0u                                   // Command, address and data are exchanged on both clock edges.
                                 | FS_NOR_HW_FLAG_DTR_DATA
                                 | FS_NOR_HW_FLAG_DTR_ADDR
                                 | FS_NOR_HW_FLAG_DTR_CMD
                                 | FS_NOR_HW_FLAG_DTR_D1_D0
                                 ;
        pInst->CmdRead           = (U8)ReadCmd;
        pInst->CmdReadEx         = (U8)(~ReadCmd);
        pInst->NumBytesReadDummy = (U8)NUM_CYCLES_DUMMY_DEFAULT << 1;   // <<1 because two bytes are sent for one dummy cycle.
        pInst->BusWidthRead      = (U16)FS_BUSWIDTH_MAKE(8uL, 8uL, 8uL);
        pInst->FlagsRead         = (U16)Flags;
        pInst->CmdWrite          = CMD_PP4B;
        pInst->BusWidthWrite     = (U16)FS_BUSWIDTH_MAKE(8uL, 8uL, 8uL);
        //
        // Configure device organization.
        //
        pSectorBlock = &pInst->aSectorBlock[0];
        pSectorBlock->NumSectors       = 1024;
        pSectorBlock->ldBytesPerSector = 16;
        pSectorBlock->CmdErase         = CMD_SE4B;
        pInst->NumSectorBlocks         = 1;
        pInst->NumBytesAddr            = 4;
        r = 0;
      }
    }
  }
  return r;
}

/*********************************************************************
*
*      _MACRONIX_OCTAL_InitSTR
*
*  Function description
*    Prepares the NOR flash for operation.
*
*  Parameters
*    pInst      Driver instance.
*/
static void _MACRONIX_OCTAL_InitSTR(FS_NOR_SPI_INST * pInst) {
  U8       abDeviceId[3];
  unsigned MfgId;
  unsigned DeviceId;
  unsigned Config;

  //
  // Try to recover only if the operation in octal mode is allowed.
  //
  if (pInst->AllowOctalMode != 0u) {
    //
    // Check the operating mode of the NOR flash device by reading the device id.
    //
    FS_MEMSET(abDeviceId, 0, sizeof(abDeviceId));
    _ReadId(pInst, abDeviceId, sizeof(abDeviceId));
    MfgId    = abDeviceId[0];
    DeviceId = abDeviceId[2];
    if ((MfgId != MFG_ID_MACRONIX) || (DeviceId != 0x3Au)) {                            // Macronix MX25LM51245G
      //
      // The device id is not correct which means that the NOR flash device
      // probably operates in OPI mode. Try switching from OPI to SPI mode.
      //
      pInst->BusWidth = (U16)FS_BUSWIDTH_MAKE(8uL, 8uL, 8uL);                           // The NOR flash device is possibly in OPI mode.
      Config          = CONFIG2_MODE_SPI;
      (void)_WriteConfigRegister2_CEI(pInst, CONFIG2_ADDR_MODE, Config);
      pInst->BusWidth = (U16)FS_BUSWIDTH_MAKE(1uL, 1uL, 1uL);                           // Indicate that we switched to SPI mode.
    }
  }
  _Init(pInst);
}

/*********************************************************************
*
*      _MACRONIX_OCTAL_InitDTR
*
*  Function description
*    Prepares the NOR flash for operation.
*
*  Parameters
*    pInst      Driver instance.
*/
static void _MACRONIX_OCTAL_InitDTR(FS_NOR_SPI_INST * pInst) {
  U8       abDeviceId[3];
  unsigned MfgId;
  unsigned DeviceId;
  unsigned Config;
  unsigned Flags;

  //
  // Try to recover only if the operation in octal mode is allowed.
  //
  if (pInst->AllowOctalMode != 0u) {
    //
    // Check the operating mode of the NOR flash device by reading the device id.
    //
    FS_MEMSET(abDeviceId, 0, sizeof(abDeviceId));
    _ReadId(pInst, abDeviceId, sizeof(abDeviceId));
    MfgId    = abDeviceId[0];
    DeviceId = abDeviceId[2];
    if ((MfgId != MFG_ID_MACRONIX) || (DeviceId != 0x3Au)) {                            // Macronix MX25LM51245G
      //
      // The device id is not correct which means that the NOR flash device
      // probably operates in OPI mode. Try switching from OPI to SPI mode.
      //
      Flags           = 0u                                                              // Command and data ares exchanged on both clock edges.
                      | FS_NOR_HW_FLAG_DTR_ADDR
                      | FS_NOR_HW_FLAG_DTR_CMD
                      | FS_NOR_HW_FLAG_DTR_D1_D0
                      ;
      pInst->BusWidth = (U16)FS_BUSWIDTH_MAKE(8uL, 8uL, 8uL);                           // The NOR flash device is possibly in OPI mode.
      pInst->Flags    = (U16)Flags;
      Config          = CONFIG2_MODE_SPI;
      (void)_WriteConfigRegister2_CEI(pInst, CONFIG2_ADDR_MODE, Config);
      pInst->BusWidth = (U16)FS_BUSWIDTH_MAKE(1uL, 1uL, 1uL);                           // Indicate that we switched to SPI mode.
      pInst->Flags    = 0;                                                              // All data is exchanged on a single clock edge.
    }
  }
  _Init(pInst);
}

/*********************************************************************
*
*       _MACRONIX_SetBusWidth
*
*  Function description
*    Configures the number of lines for the data transfer.
*
*  Parameters
*    pInst        Driver instance.
*
*  Return value
*    ==0      OK, number of data lines configured successfully.
*    !=0      An error occurred.
*/
static int _MACRONIX_SetBusWidth(FS_NOR_SPI_INST * pInst) {
  U8       RegData;
  unsigned Data;
  int      r;
  unsigned BusWidth;

  r        = 0;
  BusWidth = pInst->BusWidthRead;
  BusWidth = FS_BUSWIDTH_GET_DATA(BusWidth);
  RegData  = (U8)_ReadStatusRegister(pInst);
  switch (BusWidth) {
  default:
  case 1:
    // through
  case 2:
    if ((RegData & (1uL << STATUS_QE_BIT)) != 0u) {
      Data     = RegData;
      Data    &= ~(1uL << STATUS_QE_BIT);
      RegData  = (U8)Data;
      r = _WriteStatusRegister(pInst, &RegData, sizeof(RegData));
    }
    break;
  case 4:
    if ((RegData & (1uL << STATUS_QE_BIT)) == 0u) {
      RegData |= 1u << STATUS_QE_BIT;
      r = _WriteStatusRegister(pInst, &RegData, sizeof(RegData));
    }
    break;
  }
  return r;
}

/*********************************************************************
*
*       _MACRONIX_OCTAL_SetBusWidthSTR
*
*  Function description
*    Configures the number of lines for the data transfer.
*
*  Parameters
*    pInst        Driver instance.
*
*  Return value
*    ==0      OK, number of data lines configured successfully.
*    !=0      An error occurred.
*/
static int _MACRONIX_OCTAL_SetBusWidthSTR(FS_NOR_SPI_INST * pInst) {
  unsigned Config;
  int      r;
  unsigned BusWidthSaved;
  unsigned BusWidth;

  r        = 0;
  BusWidth = pInst->BusWidthRead;
  BusWidth = FS_BUSWIDTH_GET_DATA(BusWidth);
  Config   = _ReadConfigRegister2(pInst, CONFIG2_ADDR_MODE);
  if (BusWidth == 8u) {
    if ((Config & (CONFIG2_MODE_MASK << CONFIG2_MODE_BIT)) != CONFIG2_MODE_SOPI) {
      Config &= ~(CONFIG2_MODE_MASK << CONFIG2_MODE_BIT);
      Config |=   CONFIG2_MODE_SOPI << CONFIG2_MODE_BIT;
      r = _WriteConfigRegister2(pInst, CONFIG2_ADDR_MODE, Config);
      if (r == 0) {
        BusWidthSaved   = pInst->BusWidth;
        pInst->BusWidth = (U16)FS_BUSWIDTH_MAKE(8uL, 8uL, 8uL);                           // Indicate that we switched to OPI mode.
        //
        // Check that the NOR flash device entered STR OPI mode.
        //
        Config = 0;
        r = _ReadConfigRegister2_CEI(pInst, CONFIG2_ADDR_MODE, &Config);
        if (r == 0) {
          if ((Config & (CONFIG2_MODE_MASK << CONFIG2_MODE_BIT)) != CONFIG2_MODE_SOPI) {
            r = 1;                                                                        // Error, could not switch to OPI mode.
          }
        }
        if (r != 0) {
          pInst->BusWidth = (U16)BusWidthSaved;
        }
      }
    }
  } else {
    if ((Config & (CONFIG2_MODE_MASK << CONFIG2_MODE_BIT)) != CONFIG2_MODE_SPI) {
      Config &= ~(CONFIG2_MODE_MASK << CONFIG2_MODE_BIT);
      r = _WriteConfigRegister2_CEI(pInst, CONFIG2_ADDR_MODE, Config);
      if (r == 0) {
        BusWidthSaved   = pInst->BusWidth;
        pInst->BusWidth = (U16)FS_BUSWIDTH_MAKE(1uL, 1uL, 1uL);                           // Indicate that we switched to SPI mode.
        //
        // Check that the NOR flash device entered SPI mode.
        //
        Config = _ReadConfigRegister2(pInst, CONFIG2_ADDR_MODE);
        if ((Config & (CONFIG2_MODE_MASK << CONFIG2_MODE_BIT)) != CONFIG2_MODE_SPI) {
          pInst->BusWidth = (U16)BusWidthSaved;
          r = 1;                                                                          // Error, could not switch to SPI mode.
        }
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _MACRONIX_OCTAL_SetBusWidthDTR
*
*  Function description
*    Configures the number of lines for the data transfer.
*
*  Parameters
*    pInst        Driver instance.
*
*  Return value
*    ==0      OK, number of data lines configured successfully.
*    !=0      An error occurred.
*/
static int _MACRONIX_OCTAL_SetBusWidthDTR(FS_NOR_SPI_INST * pInst) {
  unsigned Config;
  int      r;
  unsigned BusWidthSaved;
  unsigned TransferRateSaved;
  unsigned Flags;
  unsigned BusWidth;

  r        = 0;
  BusWidth = pInst->BusWidthRead;
  BusWidth = FS_BUSWIDTH_GET_DATA(BusWidth);
  Config   = _ReadConfigRegister2(pInst, CONFIG2_ADDR_MODE);
  if (BusWidth == 8u) {
    if ((Config & (CONFIG2_MODE_MASK << CONFIG2_MODE_BIT)) != CONFIG2_MODE_DOPI) {
      Config &= ~(CONFIG2_MODE_MASK << CONFIG2_MODE_BIT);
      Config |=   CONFIG2_MODE_DOPI << CONFIG2_MODE_BIT;
      r = _WriteConfigRegister2(pInst, CONFIG2_ADDR_MODE, Config);
      if (r == 0) {
        Flags             = 0u                                                            // Command and address are exchanged on both clock edges.
                          | FS_NOR_HW_FLAG_DTR_ADDR
                          | FS_NOR_HW_FLAG_DTR_CMD
                          ;
        BusWidthSaved     = pInst->BusWidth;
        TransferRateSaved = pInst->Flags;
        pInst->BusWidth   = (U16)FS_BUSWIDTH_MAKE(8uL, 8uL, 8uL);                         // Indicate that we switched to OPI mode.
        pInst->Flags      = (U16)Flags;
        //
        // Check that the NOR flash device entered DTR OPI mode.
        //
        Config = 0;
        r = _ReadConfigRegister2_CEI(pInst, CONFIG2_ADDR_MODE, &Config);
        if (r == 0) {
          if ((Config & (CONFIG2_MODE_MASK << CONFIG2_MODE_BIT)) != CONFIG2_MODE_DOPI) {
            r = 1;                                                                        // Error, could not switch to OPI mode.
          }
        }
        if (r != 0) {
          pInst->BusWidth = (U16)BusWidthSaved;
          pInst->Flags    = (U16)TransferRateSaved;
        } else {
          pInst->IsDualDeviceMode = 1;                                                    // Two data bytes are transferred on each clock period.
        }
      }
    }
  } else {
    if ((Config & (CONFIG2_MODE_MASK << CONFIG2_MODE_BIT)) != CONFIG2_MODE_SPI) {
      Config &= ~(CONFIG2_MODE_MASK << CONFIG2_MODE_BIT);
      r = _WriteConfigRegister2_CEI(pInst, CONFIG2_ADDR_MODE, Config);
      if (r == 0) {
        BusWidthSaved     = pInst->BusWidth;
        TransferRateSaved = pInst->Flags;
        pInst->BusWidth   = (U16)FS_BUSWIDTH_MAKE(1uL, 1uL, 1uL);                         // Indicate that we switched to SPI mode.
        pInst->Flags      = 0;                                                            // All data is exchanged on a single clock edge.
        //
        // Check that the NOR flash device entered SPI mode.
        //
        Config = _ReadConfigRegister2(pInst, CONFIG2_ADDR_MODE);
        if ((Config & (CONFIG2_MODE_MASK << CONFIG2_MODE_BIT)) != CONFIG2_MODE_SPI) {
          pInst->BusWidth = (U16)BusWidthSaved;
          pInst->Flags    = (U16)TransferRateSaved;
          r = 1;                                                                          // Error, could not switch to SPI mode.
        } else {
          pInst->IsDualDeviceMode = 0;                                                    // One data byte is transferred on each clock period.
        }
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _MACRONIX_SetNumBytesAddr
*
*  Function description
*    Sets the number of address bytes accepted by the data commands.
*
*  Parameters
*    pInst        Driver instance.
*
*  Return value
*    ==0      OK, number of address bytes set successfully.
*    !=0      An error occurred.
*/
static int _MACRONIX_SetNumBytesAddr(const FS_NOR_SPI_INST * pInst) {
  int      r;
  unsigned Config;
  unsigned NumBytes;

  NumBytes = pInst->NumBytesAddr;
  if (NumBytes == 4u) {
    r = _Enter4ByteAddrMode(pInst);
    if (r == 0) {
      Config = _ReadConfigRegisterAlt(pInst);
      if ((Config & (1uL << CONFIG_4BYTE_BIT)) == 0u) {
        r = 1;        // Could not switch to 4-byte address mode.
      }
    }
  } else {
    r = _Exit4ByteAddrMode(pInst);
    if (r == 0) {
      Config = _ReadConfigRegisterAlt(pInst);
      //
      // On devices which use only 3-byte address the CONFIG_4BYTE_BIT is reserved.
      // Since the reserved flags are set to 0 the test below should work for these devices too.
      //
      if ((Config & (1uL << CONFIG_4BYTE_BIT)) != 0u) {
        r = 1;        // Could not switch to 3-byte address mode.
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _MACRONIX_OCTAL_EraseSector
*
*  Function description
*    Erases one physical sector.
*
*  Parameters
*    pInst        Driver instance.
*    CmdErase     Code of the erase command.
*    Addr         Address of the physical sector to be erased.
*
*  Return value
*    ==0      OK, physical sector erased successfully.
*    !=0      An error occurred.
*/
static int _MACRONIX_OCTAL_EraseSector(const FS_NOR_SPI_INST * pInst, U8 CmdErase, U32 Addr) {
  U8       abAddr[NUM_BYTES_ADDR_OPI];
  unsigned BusWidth;
  int      r;
  unsigned Cmd;
  U8       abCmd[NUM_BYTES_CMD_OPI];
  unsigned Flags;

  BusWidth  = pInst->BusWidth;
  Flags     = pInst->Flags;
  Cmd       = CmdErase;
  abCmd[0]  = (U8)Cmd;
  abCmd[1]  = (U8)(~Cmd);
  abAddr[0] = (U8)(Addr >> 24);
  abAddr[1] = (U8)(Addr >> 16);
  abAddr[2] = (U8)(Addr >>  8);
  abAddr[3] = (U8)Addr;
  //
  // The sector erase command is accepted only when the write mode is active.
  //
  r = _EnableWriteCEI(pInst);
  //
  // Send sector erase command to NOR flash.
  //
  if (r == 0) {
    //
    // The address of the physical sector to be erased is sent as data.
    // Therefore, we have to set the DTR flags accordingly.
    //
    if (((Flags & FS_NOR_HW_FLAG_DTR_ADDR) != 0u) && ((Flags & FS_NOR_HW_FLAG_DTR_DATA) == 0u)) {
      Flags |= FS_NOR_HW_FLAG_DTR_DATA;
    }
    r = _WriteWithCmdEx(pInst, abCmd, sizeof(abCmd), abAddr, sizeof(abAddr), BusWidth, Flags);
  }
  return r;
}

/*********************************************************************
*
*      _MACRONIX_OCTAL_WritePage
*
*  Function description
*    Writes data to a page of the NOR flash device.
*
*  Parameters
*    pInst      Driver instance.
*    Addr       Address to write to.
*    pData      [IN] Data to be written.
*    NumBytes   Number of bytes to be written.
*
*  Return value
*    ==0      OK, data written successfully.
*    !=0      An error occurred.
*/
static int _MACRONIX_OCTAL_WritePage(const FS_NOR_SPI_INST * pInst, U32 Addr, const U8 * pData, U32 NumBytes) {
  unsigned BusWidth;
  int      r;
  U8       Cmd;
  unsigned Flags;

  //
  // Initialize local variables.
  //
  Cmd      = CMD_PP;
  BusWidth = pInst->BusWidth;
  Flags    = pInst->FlagsRead;          // The same transfer rate is used for writing and reading.
  //
  // Check if a multi-bit write operation is supported and if yes use it.
  //
  if (pInst->CmdWrite != 0u) {
    Cmd      = pInst->CmdWrite;
    BusWidth = pInst->BusWidthWrite;
  }
  //
  // The write page operation is accepted only when the NOR flash is in write mode.
  //
  r = _EnableWriteCEI(pInst);
  //
  // Send command and write page data to NOR flash.
  //
  if (r == 0) {
    r = _WritePageDataCEI(pInst, Cmd, Addr, pData, NumBytes, BusWidth, Flags);
  }
  return r;
}

/*********************************************************************
*
*       _MACRONIX_WaitForEndOfOperation
*
*  Function description
*    Waits for a NOR flash operation to complete.
*
*  Parameters
*    pInst        Driver instance.
*    pPollPara    [IN] Parameters of the polling operation.
*
*  Return value
*    ==0      OK, operation completed successfully.
*    !=0      An error occurred.
*/
static int _MACRONIX_WaitForEndOfOperation(const FS_NOR_SPI_INST * pInst, const FS_NOR_SPI_POLL_PARA * pPollPara) {
  int      r;
  int      rPoll;
  unsigned Status;
  U32      TimeOut;

  r       = 1;                                            // Set to indicate an error.
  TimeOut = pPollPara->TimeOut;
  //
  // Prefer polling the status register in the hardware if supported.
  //
  rPoll = _PollStatusRegister(pInst, STATUS_BUSY_BIT, pPollPara);
  if (rPoll > 0) {                                        // Timeout reported?
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _MACRONIX_WaitForEndOfOperation: Timeout expired."));
  } else {
    if (rPoll < 0) {                                      // Is the feature not supported?
      //
      // Poll the status register in the software.
      //
      for (;;) {
        //
        // Read the status from NOR device.
        //
        Status = _ReadStatusRegister(pInst);
        if ((Status & (1uL << STATUS_BUSY_BIT)) == 0u) {
          r = 0;                                          // OK, the NOR flash is ready for a new operation.
          break;
        }
        --TimeOut;                                        // We have executed one wait cycle.
        //
        // If possible, delay the next request to reduce CPU usage.
        //
        TimeOut = _DelayPoll(pInst, TimeOut, pPollPara);
        if (TimeOut == 0u) {
          FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _MACRONIX_WaitForEndOfOperation: Timeout expired."));
          break;
        }
      }
    } else {
      r = 0;                                              // OK, the NOR flash is ready for a new operation.
    }
  }
  if (r == 0) {
    //
    // Check the error flags.
    //
    Status = _ReadSecurityRegister(pInst);
    if (   ((Status & (1uL << SCUR_E_FAIL_BIT)) != 0u)
        || ((Status & (1uL << SCUR_P_FAIL_BIT)) != 0u)) { // Is an error present?
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _MACRONIX_WaitForEndOfOperation: NOR flash reports error 0x%x.", Status));
      r = 1;                                              // The NOR flash reports an error.
    }
  }
  return r;
}

/*********************************************************************
*
*       _MACRONIX_OCTAL_WaitForEndOfOperation
*
*  Function description
*    Waits for a NOR flash operation to complete.
*
*  Parameters
*    pInst        Driver instance.
*    pPollPara    [IN] Parameters of the polling operation.
*
*  Return value
*    ==0      OK, operation completed successfully.
*    !=0      An error occurred.
*/
static int _MACRONIX_OCTAL_WaitForEndOfOperation(const FS_NOR_SPI_INST * pInst, const FS_NOR_SPI_POLL_PARA * pPollPara) {
  int      r;
  int      rPoll;
  unsigned Status;
  U32      TimeOut;

  r       = 1;                                            // Set to indicate an error.
  TimeOut = pPollPara->TimeOut;
  //
  // Prefer polling the status register in the hardware if supported.
  //
  rPoll = _PollStatusRegisterCEI(pInst, STATUS_BUSY_BIT, pPollPara);
  if (rPoll > 0) {                                            // Timeout reported?
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _MACRONIX_OCTAL_WaitForEndOfOperation: Timeout expired."));
  } else {
    if (rPoll < 0) {                                          // Is the feature not supported?
      //
      // Poll the status register in the software.
      //
      for (;;) {
        //
        // Read the status from NOR device.
        //
        Status = 0;
        r = _ReadStatusRegisterCEI(pInst, &Status);
        if (r != 0) {
          break;                                              // Error, could not read status.
        }
        if ((Status & (1uL << STATUS_BUSY_BIT)) == 0u) {
          r = 0;                                              // OK, the NOR flash is ready for a new operation.
          break;
        }
        --TimeOut;                                            // We have executed one wait cycle.
        //
        // If possible, delay the next request to reduce CPU usage.
        //
        TimeOut = _DelayPoll(pInst, TimeOut, pPollPara);
        if (TimeOut == 0u) {
          FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _MACRONIX_OCTAL_WaitForEndOfOperation: Timeout expired."));
          break;
        }
      }
    } else {
      r = 0;                                                  // OK, the NOR flash is ready for a new operation.
    }
  }
  if (r == 0) {
    //
    // Check the error flags.
    //
    Status = 0;
    r = _ReadSecurityRegisterCEI(pInst, &Status);
    if (r == 0) {
      if (   ((Status & (1uL << SCUR_E_FAIL_BIT)) != 0u)
          || ((Status & (1uL << SCUR_P_FAIL_BIT)) != 0u)) {
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _MACRONIX_WaitForEndOfOperation: NOR flash reports error 0x%x.", Status));
        r = 1;                                                // The NOR flash reports an error.
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _MACRONIX_EncodeDummyCycles
*
*  Function description
*    Maps the number of dummy cycles from actual value to the register value.
*
*  Parameters
*    NumCyclesDummy     Actual number of dummy cycles.
*
*  Return value
*    Value stored in the device register.
*/
static unsigned _MACRONIX_EncodeDummyCycles(unsigned NumCyclesDummy) {
  unsigned RegValue;

  for (RegValue = 0; RegValue < SEGGER_COUNTOF(_abDummyCycles); ++RegValue) {
    if ((unsigned)_abDummyCycles[RegValue] == NumCyclesDummy) {
      return RegValue;
    }
  }
  return 0;               // This is the default register value.
}

/*********************************************************************
*
*       _MACRONIX_SetNumCyclesDummy
*
*  Function description
*    Sets the number of dummy cycles.
*
*  Parameters
*    pInst        Driver instance.
*    Freq_Hz      [IN] Clock frequency supplied to NOR flash device.
*
*  Return value
*    ==0      OK, dummy cycles configured successfully.
*    !=0      An error occurred.
*/
static int _MACRONIX_SetNumCyclesDummy(FS_NOR_SPI_INST * pInst, U32 Freq_Hz) {
  unsigned NumBytesDummy;
  unsigned NumCyclesDummy;
  unsigned NumCyclesDummyToCheck;
  unsigned Config;
  int      r;

  r = 0;          // Set to indicate success.
  //
  // Calculate the minimum number of dummy cycles. According to the data sheet
  // of the Macronix MX25UM51245G NOR flash device, the number of dummy cycles
  // depends on the frequency of the serial clock and on the package type.
  // Because it is not possible to determine the package type at runtime,
  // we configure the minimum number of dummy cycles that works with all the
  // supported package types for the specified clock frequency.
  //
  NumBytesDummy = NUM_CYCLES_DUMMY_DEFAULT;
  if (Freq_Hz <= 66000000uL) {
    NumBytesDummy = 6;
  }
  NumCyclesDummy = _MACRONIX_EncodeDummyCycles(NumBytesDummy);
  //
  // Set the calculated number of dummy cycles if required.
  //
  Config = _ReadConfigRegister2(pInst, CONFIG2_ADDR_DUMMY);
  NumCyclesDummyToCheck = (Config >> CONFIG2_DUMMY_BIT) & CONFIG2_DUMMY_MASK;
  if (NumCyclesDummyToCheck != NumCyclesDummy) {
    Config &= ~(CONFIG2_DUMMY_MASK << CONFIG2_DUMMY_BIT);
    Config |=   NumCyclesDummy     << CONFIG2_DUMMY_BIT;
    r = _WriteConfigRegister2(pInst, CONFIG2_ADDR_DUMMY, Config);
    if (r == 0) {
      //
      // Verify that the value was stored correctly.
      //
      Config = _ReadConfigRegister2(pInst, CONFIG2_ADDR_DUMMY);
      NumCyclesDummyToCheck = (Config >> CONFIG2_DUMMY_BIT) & CONFIG2_DUMMY_MASK;
      if (NumCyclesDummyToCheck != NumCyclesDummy) {
        r = 1;                                            // Error, the value was not set correctly.
      }
    }
  }
  if (pInst->AllowDTRMode != 0u) {
    NumBytesDummy <<= 1;
  }
  pInst->NumBytesReadDummy = (U8)NumBytesDummy;           // Update the value used for the memory array read operations.
  return r;
}

/*********************************************************************
*
*       _GIGADEVICE_IdentifyEnhanced
*/
static int _GIGADEVICE_IdentifyEnhanced(FS_NOR_SPI_INST * pInst, const U8 * pId) {
  int      r;
  unsigned MfgId;
  unsigned DeviceId;

  FS_USE_PARA(pInst);
  r        = 1;               // Set to indicate an error.
  MfgId    = pId[0];
  DeviceId = pId[2];
  if (MfgId == MFG_ID_GIGADEVICE) {
    //
    // The following GigaDevice devices are supported:
    //
    // Id               Device
    // -----------------------
    // 0xC8 0x.. 0x19   GigaDevice GD25Q256D
    //
    if (DeviceId == 0x19u) {
      r = 0;
    }
  }
  return r;
}

/*********************************************************************
*
*       _GIGADEVICE_IdentifyStandard
*/
static int _GIGADEVICE_IdentifyStandard(FS_NOR_SPI_INST * pInst, const U8 * pId) {
  int      r;
  unsigned MfgId;
  unsigned DeviceId1;
  unsigned DeviceId2;

  FS_USE_PARA(pInst);
  r         = 1;              // Set to indicate an error.
  MfgId     = pId[0];
  DeviceId1 = pId[1];
  DeviceId2 = pId[2];
  if (MfgId == MFG_ID_GIGADEVICE) {
    //
    // The following GigaDevice devices are supported:
    //
    // Id               Device
    // -----------------------
    // 0xC8 0x40 0x16   GigaDevice GD25Q32C
    // 0xC8 0x40 0x17   GigaDevice GD25Q64C
    // 0xC8 0x40 0x18   GigaDevice GD25Q127C
    //
    if (DeviceId1 == 0x40u) {
      if (   (DeviceId2 == 0x16u)
          || (DeviceId2 == 0x17u)
          || (DeviceId2 == 0x18u)) {
        r = 0;
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _GIGADEVICE_IdentifyLowVoltage
*
*  Function description
*    Identifies an 1.8V serial NOR flash device.
*
*  Parameters
*    pInst      Driver instance.
*    pId        [IN] Identification information.
*
*  Return value
*    ==0      Device identified.
*    !=0      Unknown device.
*
*  Additional information
*   All 1.8V devices have only two status registers and the Write Status Register
*   (0x01) command modifies both of them. This command works differently on
*   the 3.3V devices which have 3 status registers. The Write Status Register
*   (0x01) command of the 3.3V devices modifies only the first status register.
*   Therefore, we have to create a new list for the 1.8V devices to handle this
*   difference in behavior.
*   In addition, the 1.8V devices have to be identified using the second id byte
*   because the third is identical to a 3.3V device with the same capacity.
*/
static int _GIGADEVICE_IdentifyLowVoltage(FS_NOR_SPI_INST * pInst, const U8 * pId) {
  int      r;
  unsigned MfgId;
  unsigned DeviceId1;
  unsigned DeviceId2;

  FS_USE_PARA(pInst);
  r         = 1;              // Set to indicate an error.
  MfgId     = pId[0];
  DeviceId1 = pId[1];
  DeviceId2 = pId[2];
  if (MfgId == MFG_ID_GIGADEVICE) {
    //
    // The following GigaDevice devices are supported:
    //
    // Id               Device
    // -----------------------
    // 0xC8 0x60 0x15   GigaDevice GD25LQ16C
    // 0xC8 0x60 0x18   GigaDevice GD25LQ128D, GD25LQ128E
    //
    if (DeviceId1 == 0x60u) {
      if (   (DeviceId2 == 0x15u)
          || (DeviceId2 == 0x18u)) {
        r = 0;
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _BYT_Identify
*/
static int _BYT_Identify(FS_NOR_SPI_INST * pInst, const U8 * pId) {
  int      r;
  unsigned MfgId;
  unsigned DeviceId1;
  unsigned DeviceId2;

  FS_USE_PARA(pInst);
  r         = 1;              // Set to indicate an error.
  MfgId     = pId[0];
  DeviceId1 = pId[1];
  DeviceId2 = pId[2];
  if (MfgId == MFG_ID_BYT) {
    //
    // The following BYT devices are supported:
    //
    // Id               Device
    // -----------------------
    // 0x68 0x40 0x16   BYT BY25Q32ES
    // 0x68 0x40 0x17   BYT BY25Q64ES
    // 0x68 0x40 0x18   BYT BY25Q128ES
    //
    if (DeviceId1 == 0x40u) {
      if (   (DeviceId2 == 0x16u)
          || (DeviceId2 == 0x17u)
          || (DeviceId2 == 0x18u)) {
        r = 0;
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _GIGADEVICE_SetBusWidth
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
static int _GIGADEVICE_SetBusWidth(FS_NOR_SPI_INST * pInst) {
  U8       RegData;
  unsigned Data;
  int      r;
  unsigned BusWidth;

  r        = 0;
  BusWidth = pInst->BusWidthRead;
  BusWidth = FS_BUSWIDTH_GET_DATA(BusWidth);
  RegData  = _ReadStatusRegister2Alt(pInst);
  switch (BusWidth) {
  default:
  case 1:
    // through
  case 2:
    if ((RegData & (1u << STATUS2_QE_BIT)) != 0u) {
      Data     = RegData;
      Data    &= ~(1uL << STATUS2_QE_BIT);
      RegData  = (U8)Data;
      r = _WriteStatusRegister2(pInst, &RegData, sizeof(RegData));
    }
    break;
  case 4:
    if ((RegData & (1u << STATUS2_QE_BIT)) == 0u) {
      RegData |= 1u << STATUS2_QE_BIT;
      r = _WriteStatusRegister2(pInst, &RegData, sizeof(RegData));
    }
    break;
  }
  return r;
}

/*********************************************************************
*
*       _BYT_SetBusWidth
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
static int _BYT_SetBusWidth(FS_NOR_SPI_INST * pInst) {
  U8       RegData;
  unsigned Data;
  int      r;
  unsigned BusWidth;

  r        = 0;
  BusWidth = pInst->BusWidthRead;
  BusWidth = FS_BUSWIDTH_GET_DATA(BusWidth);
  RegData  = _ReadStatusRegister2Alt(pInst);
  switch (BusWidth) {
  default:
  case 1:
    // through
  case 2:
    if ((RegData & (1u << STATUS2_QE_BIT)) != 0u) {
      Data     = RegData;
      Data    &= ~(1uL << STATUS2_QE_BIT);
      RegData  = (U8)Data;
      r = _WriteStatusRegister2(pInst, &RegData, sizeof(RegData));
    }
    break;
  case 4:
    if ((RegData & (1u << STATUS2_QE_BIT)) == 0u) {
      RegData |= 1u << STATUS2_QE_BIT;
      r = _WriteStatusRegister2(pInst, &RegData, sizeof(RegData));
    }
    break;
  }
  return r;
}

/*********************************************************************
*
*       _GIGADEVICE_SetBusWidthLowVoltage
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
static int _GIGADEVICE_SetBusWidthLowVoltage(FS_NOR_SPI_INST * pInst) {
  U8       abRegData[2];
  unsigned Data;
  int      r;
  unsigned BusWidth;

  r        = 0;
  BusWidth = pInst->BusWidthRead;
  BusWidth = FS_BUSWIDTH_GET_DATA(BusWidth);
  //
  // The quad mode flag is located in the second byte of the status register.
  // We have to read the first byte of the status register too because the
  // WRITE STATUS REGISTER command writes both bytes.
  //
  FS_MEMSET(abRegData, 0, sizeof(abRegData));
  abRegData[0] = (U8)_ReadStatusRegister(pInst);
  abRegData[1] = (U8)_ReadStatusRegister2Alt(pInst);
  switch (BusWidth) {
  default:
  case 1:
    // through
  case 2:
    if ((abRegData[1] & (1u << STATUS2_QE_BIT)) != 0u) {
      Data          = abRegData[1];
      Data         &= ~(1uL << STATUS2_QE_BIT);
      abRegData[1]  = (U8)Data;
      r = _WriteStatusRegister(pInst, abRegData, sizeof(abRegData));
    }
    break;
  case 4:
    if ((abRegData[1] & (1u << STATUS2_QE_BIT)) == 0u) {
      abRegData[1] |= 1u << STATUS2_QE_BIT;
      r = _WriteStatusRegister(pInst, abRegData, sizeof(abRegData));
    }
    break;
  }
  return r;
}

/*********************************************************************
*
*       _GIGADEVICE_SetNumBytesAddr
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
static int _GIGADEVICE_SetNumBytesAddr(const FS_NOR_SPI_INST * pInst) {
  int      r;
  unsigned Status;
  unsigned NumBytes;

  r        = 0;           // Set to indicate success.
  NumBytes = pInst->NumBytesAddr;
  if (NumBytes == 4u) {
    Status = _ReadStatusRegister2Alt(pInst);
    if ((Status & (1uL << STATUS2_ADS_BIT)) == 0u) {
      r = _Enter4ByteAddrMode(pInst);
      if (r == 0) {
        Status = _ReadStatusRegister2Alt(pInst);
        if ((Status & (1uL << STATUS2_ADS_BIT)) == 0u) {
          r = 1;        // Could not switch to 4-byte address mode.
        }
      }
    }
  } else {
    Status = _ReadStatusRegister2Alt(pInst);
    if ((Status & (1uL << STATUS2_ADS_BIT)) != 0u) {
      r = _Exit4ByteAddrMode(pInst);
      if (r == 0) {
        Status = _ReadStatusRegister2Alt(pInst);
        //
        // On devices which use only 3-byte address the FLAG_STATUS_ADDR_BIT is reserved.
        // Since the reserved flags are set to 0 the test below should work for these devices too.
        //
        if ((Status & (1uL << STATUS2_ADS_BIT)) != 0u) {
          r = 1;        // Could not switch to 3-byte address mode.
        }
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _GIGADEVICE_WaitForEndOfOperation
*
*  Function description
*    Waits for a NOR flash operation to complete.
*
*  Parameters
*    pInst        Driver instance.
*    pPollPara    [IN] Parameters of the polling operation.
*
*  Return value
*    ==0      OK, operation completed successfully.
*    !=0      An error occurred.
*/
static int _GIGADEVICE_WaitForEndOfOperation(const FS_NOR_SPI_INST * pInst, const FS_NOR_SPI_POLL_PARA * pPollPara) {
  int      r;
  unsigned Status;
  U32      TimeOut;
  int      rPoll;

  r       = 1;                                                      // Set to indicate error.
  TimeOut = pPollPara->TimeOut;
  //
  // Prefer polling the status register in the hardware if supported.
  //
  rPoll = _PollStatusRegister(pInst, STATUS_BUSY_BIT, pPollPara);
  if (rPoll > 0) {                                                  // Timeout reported?
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _GIGADEVICE_WaitForEndOfOperation: Timeout expired."));
  } else {
    if (rPoll < 0) {                                                // Is the feature not supported?
      //
      // Poll the status register in the software.
      //
      for (;;) {
        //
        // Read the status from NOR device.
        //
        Status = _ReadStatusRegister(pInst);
        if ((Status & (1uL << STATUS_BUSY_BIT)) == 0u) {
          r = 0;                                                    // OK, the NOR flash is ready for a new operation.
          break;
        }
        --TimeOut;                                                  // We have executed one wait cycle.
        //
        // If possible, delay the next request to reduce CPU usage.
        //
        TimeOut = _DelayPoll(pInst, TimeOut, pPollPara);
        if (TimeOut == 0u) {
          FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _GIGADEVICE_WaitForEndOfOperation: Timeout expired."));
          break;
        }
      }
    } else {
      r = 0;                                                        // OK, the NOR device flash is ready for a new operation.
    }
  }
  if (r == 0) {
    //
    // Check the error flags and clear them if necessary.
    //
    Status = _ReadStatusRegister3(pInst);
    if (   ((Status & (1uL << STATUS3_EE_BIT)) != 0u)
        || ((Status & (1uL << STATUS3_PE_BIT)) != 0u)) {            // Is any error present?
      (void)_ClearStatusRegister(pInst);
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _GIGADEVICE_WaitForEndOfOperation: NOR flash reports error 0x%x.", Status));
      r = 1;                                                        // The NOR flash reports an error.
    }
  }
  return r;
}

/*********************************************************************
*
*       _CYPRESS_Identify
*
*  Function description
*    Identifies a Cypress NOR flash device by device id.
*
*  Parameters
*    pInst        Driver instance.
*    pId          [IN] Device id.
*
*  Return value
*    ==0      Device supported.
*    !=0      Device not supported.
*
*  Notes
*    (1) The S25FL256L device identifies itself with the same manufacturer and device id
*        as S25FL256S (with an 'S' at the end instead of 'L') but it is not 100%
*        compatible with it. Therefore, we also have to check the second byte returned
*        in the response to READ ID (0x9F) command. This byte is set to 0x02 for the S
*        variant and to 0x60 for the L variant.
*        The same applies to S25FL164K and S25FL064L devices.
*/
static int _CYPRESS_Identify(FS_NOR_SPI_INST * pInst, const U8 * pId) {
  int      r;
  unsigned MfgId;
  unsigned DeviceType;
  unsigned DeviceId;

  FS_USE_PARA(pInst);
  r          = 1;                                         // Set to indicate error.
  MfgId      = pId[0];
  DeviceType = pId[1];
  DeviceId   = pId[2];
  if (MfgId == MFG_ID_CYPRESS) {
    //
    // Id                Device
    // ------------------------
    // 0x01 0x60 0x17    Cypress S25FL064L
    // 0x01 0x60 0x18    Cypress S25FL128L
    // 0x01 0x60 0x19    Cypress S25FL256L
    //
    if (DeviceType == 0x60u) {
      if ((DeviceId >= 0x17u) && (DeviceId <= 0x19u)) {   // Note 1
        r = 0;                                            // OK, device identified.
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _CYPRESS_SetBusWidth
*
*  Function description
*    Configures the number of data lines for the data transfer.
*
*  Parameters
*    pInst        Driver instance.
*
*  Return value
*    ==0      OK, number of data lines configured.
*    !=0      An error occurred.
*
*  Additional information
*    The quad mode can be entered by setting the QUAD bit of the
*    Configuration Register-1 to 1 (CR1V[1] = 1). This register can
*    be modified using the Write Register (WRR, 01h) command.
*    The Write Enable for Volatile Registers (WRENV, 0x50) has to be
*    set first in order to be able to modify the values of the volatile
*    registers. The WRR command writes the following registers in this
*    order:
*      1) Status Register-1
*      2) Configuration Register-1
*      3) Configuration Register-2
*      4) Configuration Register-3
*    In order to modify the QUAD bit (1) of the Configuration Register-1
*    a read-modify-write operation has to be performed by reading
*    first the contents of the Status Register-1 and Configuration Register-1
*    setting or clearing the QUAD bit value and then writing both
*    registers back.
*/
static int _CYPRESS_SetBusWidth(FS_NOR_SPI_INST * pInst) {
  U8       abRegData[2];     // Status Register-1 (SR1V) + Configuration Register-1 (CR1V)
  int      r;
  unsigned Data;
  unsigned BusWidth;

  r            = 0;
  BusWidth     = pInst->BusWidthRead;
  BusWidth     = FS_BUSWIDTH_GET_DATA(BusWidth);
  abRegData[0] = (U8)_ReadStatusRegister(pInst);    // SR1V
  abRegData[1] = (U8)_ReadConfigRegister(pInst);    // CR1V
  switch (BusWidth) {
  default:
  case 1:
    // through
  case 2:
    if ((abRegData[1] & (1u << CONFIG_QUAD_BIT)) != 0u) {
      Data = abRegData[1];
      Data &= ~(1uL << CONFIG_QUAD_BIT);
      abRegData[1] = (U8)Data;
      r = _EnableWriteForVolatile(pInst);
      if (r == 0) {
        r = _WriteStatusRegister(pInst, abRegData, sizeof(abRegData));
      }
    }
    break;
  case 4:
    if ((abRegData[1] & (1u << CONFIG_QUAD_BIT)) == 0u) {
      abRegData[1] |= 1u << CONFIG_QUAD_BIT;
      r = _EnableWriteForVolatile(pInst);
      if (r == 0) {
        r = _WriteStatusRegister(pInst, abRegData, sizeof(abRegData));
      }
    }
    break;
  }
  return r;
}

/*********************************************************************
*
*       _CYPRESS_SetNumBytesAddr
*
*  Function description
*    Configures the number of address bytes.
*
*  Parameters
*    pInst        Driver instance.
*
*  Return value
*    ==0      OK, number of address bytes configured.
*    !=0      An error occurred.
*/
static int _CYPRESS_SetNumBytesAddr(const FS_NOR_SPI_INST * pInst) {
  int      r;
  unsigned Status;
  unsigned NumBytes;

  r        = 0;             // Set to indicate success.
  NumBytes = pInst->NumBytesAddr;
  if (NumBytes == 4u) {
    Status = _ReadConfigRegisterAlt(pInst);
    if ((Status & (1uL << CONFIG2_ADS_BIT)) == 0u) {
      r = _Enter4ByteAddrMode(pInst);
      if (r == 0) {
        Status = _ReadConfigRegisterAlt(pInst);
        if ((Status & (1uL << CONFIG2_ADS_BIT)) == 0u) {
          r = 1;        // Could not switch to 4-byte address mode.
        }
      }
    }
  } else {
    Status = _ReadConfigRegisterAlt(pInst);
    if ((Status & (1uL << CONFIG2_ADS_BIT)) != 0u) {
      r = _Exit4ByteAddrMode(pInst);
      if (r == 0) {
        Status = _ReadConfigRegisterAlt(pInst);
        if ((Status & (1uL << CONFIG2_ADS_BIT)) != 0u) {
          r = 1;        // Could not switch to 3-byte address mode.
        }
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _CYPRESS_WaitForEndOfOperation
*
*  Function description
*    Waits for the NOR flash device to finish the current operation.
*
*  Parameters
*    pInst        Driver instance.
*    pPollPara    [IN] Information about how to perform the queries.
*
*  Return value
*    ==0      OK, operation completed successfully.
*    !=0      An error occurred.
*/
static int _CYPRESS_WaitForEndOfOperation(const FS_NOR_SPI_INST * pInst, const FS_NOR_SPI_POLL_PARA * pPollPara) {
  int      r;
  unsigned Status;
  U32      TimeOut;
  int      rPoll;

  r       = 1;                                                      // Set to indicate error.
  TimeOut = pPollPara->TimeOut;
  //
  // Prefer polling the status register in the hardware if supported.
  //
  rPoll = _PollStatusRegister(pInst, STATUS_BUSY_BIT, pPollPara);
  if (rPoll > 0) {                                                  // Timeout reported?
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _CYPRESS_WaitForEndOfOperation: Timeout expired."));
  } else {
    if (rPoll < 0) {                                                // Is the feature not supported?
      //
      // Poll the status register in the software.
      //
      for (;;) {
        //
        // Read the status from NOR device.
        //
        Status = _ReadStatusRegister(pInst);
        if ((Status & (1uL << STATUS_BUSY_BIT)) == 0u) {
          r = 0;                                                    // OK, the NOR flash device is ready for a new operation.
          break;
        }
        --TimeOut;                                                  // We have executed one wait cycle.
        //
        // If possible, delay the next request to reduce CPU usage.
        //
        TimeOut = _DelayPoll(pInst, TimeOut, pPollPara);
        if (TimeOut == 0u) {
          FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _CYPRESS_WaitForEndOfOperation: Timeout expired."));
          break;
        }
      }
    } else {
      r = 0;                                                        // OK, the NOR device flash is ready for a new operation.
    }
  }
  if (r == 0) {
    //
    // Check and clear the error flags.
    //
    Status = _ReadStatusRegister2(pInst);
    if (   ((Status & (1uL << STATUS2_E_ERR_BIT)) != 0u)
        || ((Status & (1uL << STATUS2_P_ERR_BIT)) != 0u)) {         // Is any error present?
      (void)_ClearStatusRegister(pInst);
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _CYPRESS_WaitForEndOfOperation: NOR flash reports error 0x%x.", Status));
      r = 1;                                                        // The NOR flash device reports error.
    }
  }
  return r;
}

/*********************************************************************
*
*       _ADESTO_IdentifyStandard
*/
static int _ADESTO_IdentifyStandard(FS_NOR_SPI_INST * pInst, const U8 * pId) {
  int      r;
  unsigned MfgId;
  unsigned DeviceId;

  FS_USE_PARA(pInst);
  r          = 1;               // Set to indicate an error.
  MfgId      = pId[0];
  DeviceId   = pId[2];
  if (MfgId == MFG_ID_ADESTO) {
    //
    // Id                Device
    // ------------------------
    // 0x1F 0x89 0x01    Adesto AT25SF128A
    // 0x1F 0x42 0x16    Adesto AT25SL321
    //
    if (   (DeviceId == 0x01u)
        || (DeviceId == 0x16u)) {
      r = 0;                    // OK, device identified.
    }
  }
  return r;
}

/*********************************************************************
*
*       _ADESTO_IdentifyEnhanced
*
*  Function description
*    Identifies an serial NOR flash device that can report erase
*    or programming errors.
*
*  Parameters
*    pInst      Driver instance.
*    pId        [IN] Identification information.
*
*  Return value
*    ==0      Device identified.
*    !=0      Unknown device.
*
*  Additional information
*    We have to use the first id byte to identify the device because
*    the second device id byte does not provide any information about
*    the capacity of the storage device.
*/
static int _ADESTO_IdentifyEnhanced(FS_NOR_SPI_INST * pInst, const U8 * pId) {
  int      r;
  unsigned MfgId;
  unsigned DeviceId;

  FS_USE_PARA(pInst);
  r        = 1;               // Set to indicate an error.
  MfgId    = pId[0];
  DeviceId = pId[1];          // We have to use the first id byte to identify the device.
  if (MfgId == MFG_ID_ADESTO) {
    //
    // Id                Device
    // ------------------------
    // 0x1F 0x44 0x0C    Adesto AT25XE041D
    // 0x1F 0x45 0x0C    Adesto AT25XE081D
    // 0x1F 0x46 0x0C    Adesto AT25XE161D
    //
    if (   (DeviceId == 0x44u)
        || (DeviceId == 0x45u)
        || (DeviceId == 0x46u)) {
      //
      // We disable the 1-4-4 read mode because the device
      // reports the wrong number of dummy bytes via SFDP.
      // We use the 1-1-4 mode instead.
      //
      pInst->ReadModesDisabled = 1u << READ_MODE_144_BIT;
      r = 0;                    // OK, device identified.
    }
  }
  return r;
}

/*********************************************************************
*
*       _ADESTO_SetBusWidth
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
static int _ADESTO_SetBusWidth(FS_NOR_SPI_INST * pInst) {
  U8       RegData;
  unsigned Data;
  int      r;
  unsigned BusWidth;

  r        = 0;
  BusWidth = pInst->BusWidthRead;
  BusWidth = FS_BUSWIDTH_GET_DATA(BusWidth);
  RegData  = _ReadStatusRegister2Alt(pInst);
  switch (BusWidth) {
  default:
  case 1:
    // through
  case 2:
    if ((RegData & (1u << STATUS2_QE_BIT)) != 0u) {
      Data     = RegData;
      Data    &= ~(1uL << STATUS2_QE_BIT);
      RegData  = (U8)Data;
      r = _WriteStatusRegister2(pInst, &RegData, sizeof(RegData));
    }
    break;
  case 4:
    if ((RegData & (1u << STATUS2_QE_BIT)) == 0u) {
      RegData |= 1u << STATUS2_QE_BIT;
      r = _WriteStatusRegister2(pInst, &RegData, sizeof(RegData));
    }
    break;
  }
  return r;
}

/*********************************************************************
*
*       _ADESTO_WaitForEndOfOperation
*
*  Function description
*    Waits for a NOR flash operation to complete.
*
*  Parameters
*    pInst        Driver instance.
*    pPollPara    [IN] Parameters of the polling operation.
*
*  Return value
*    ==0      OK, operation completed successfully.
*    !=0      An error occurred.
*/
static int _ADESTO_WaitForEndOfOperation(const FS_NOR_SPI_INST * pInst, const FS_NOR_SPI_POLL_PARA * pPollPara) {
  int      r;
  unsigned Status;
  U32      TimeOut;
  int      rPoll;

  r       = 1;                                                      // Set to indicate error.
  TimeOut = pPollPara->TimeOut;
  //
  // Prefer polling the status register in the hardware if supported.
  //
  rPoll = _PollStatusRegister(pInst, STATUS_BUSY_BIT, pPollPara);
  if (rPoll > 0) {                                                  // Timeout reported?
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _ADESTO_WaitForEndOfOperation: Timeout expired."));
  } else {
    if (rPoll < 0) {                                                // Is the feature not supported?
      //
      // Poll the status register in the software.
      //
      for (;;) {
        //
        // Read the status from NOR device.
        //
        Status = _ReadStatusRegister(pInst);
        if ((Status & (1uL << STATUS_BUSY_BIT)) == 0u) {
          r = 0;                                                    // OK, the NOR flash is ready for a new operation.
          break;
        }
        --TimeOut;                                                  // We have executed one wait cycle.
        //
        // If possible, delay the next request to reduce CPU usage.
        //
        TimeOut = _DelayPoll(pInst, TimeOut, pPollPara);
        if (TimeOut == 0u) {
          FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _ADESTO_WaitForEndOfOperation: Timeout expired."));
          break;
        }
      }
    } else {
      r = 0;                                                        // OK, the NOR device flash is ready for a new operation.
    }
  }
  if (r == 0) {
    //
    // Check and clear the error flags.
    //
    Status = _ReadStatusRegisterIndirect(pInst, REG_ADDR_ERROR);
    if (   ((Status & (1uL << STATUS4_EE_BIT)) != 0u)
        || ((Status & (1uL << STATUS4_PE_BIT)) != 0u)) {            // Is any error present?
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_SPI: _ADESTO_WaitForEndOfOperation: NOR flash reports error 0x%x.", Status));
      r = 1;                                                        // The NOR flash reports an error.
    }
  }
  return r;
}

/*********************************************************************
*
*       _EON_Identify
*/
static int _EON_Identify(FS_NOR_SPI_INST * pInst, const U8 * pId) {
  int      r;
  unsigned MfgId;
  unsigned DeviceId;

  FS_USE_PARA(pInst);
  r          = 1;               // Set to indicate an error.
  MfgId      = pId[0];
  DeviceId   = pId[2];
  if (MfgId == MFG_ID_EON) {
    //
    // Id                Device
    // ------------------------
    // 0x1C 0x70 0x18    Eon EN25QH128A
    //
    if (DeviceId == 0x18u) {
      r = 0;                    // OK, device supported.
    }
  }
  return r;
}

/*********************************************************************
*
*       _EON_ReadApplyPara
*
*  Function description
*    Tries to identify the parameters of the serial NOR flash device
*    by using the Serial Flash Discovery Parameters.
*
*  Return value
*    ==0    OK, serial NOR flash device identified.
*    !=0    Error, serial NOR flash device does not support SFDP.
*/
static int _EON_ReadApplyPara(FS_NOR_SPI_INST * pInst) {
  int r;

  r = _SFDP_ReadApplyPara(pInst);
  if (r == 0) {
    //
    // The device reports an incorrect number of dummy cycles
    // for the 1-4-4 read command. We correct this value here.
    //
    if (pInst->CmdRead == 0xEBu) {
      pInst->NumBytesReadDummy = 3;
    }
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
*       _DeviceMicron
*/
const FS_NOR_SPI_TYPE FS_NOR_SPI_DeviceMicron = {
  _MICRON_Identify,
  _Init,
  _MICRON_SetBusWidth,
  _MICRON_SetNumBytesAddr,
  _SFDP_ReadApplyPara,
  _RemoveWriteProtection,
  _EraseSector,
  _WritePage,
  _MICRON_WaitForEndOfOperation,
  NULL
};

/*********************************************************************
*
*       FS_NOR_SPI_DeviceMicron_x2
*
*  Description
*    Configuration with two identical NOR flash devices connected in parallel.
*/
const FS_NOR_SPI_TYPE FS_NOR_SPI_DeviceMicron_x2 = {
  _MICRON_Identify_x2,
  _Init_x2,
  NULL,
  _MICRON_SetNumBytesAddr_x2,
  _SFDP_ReadApplyPara_x2,
  _RemoveWriteProtection_x2,
  _EraseSector_x2,
  _WritePage_x2,
  _MICRON_WaitForEndOfOperation_x2,
  NULL
};

/*********************************************************************
*
*       FS_NOR_SPI_DeviceSpansion
*/
const FS_NOR_SPI_TYPE FS_NOR_SPI_DeviceSpansion = {
  _SPANSION_Identify,
  _Init,
  _SPANSION_SetBusWidth,
  _SPANSION_SetNumBytesAddr,
  _SPANSION_ReadApplyPara,
  _RemoveWriteProtection,
  _EraseSector,
  _WritePage,
  _SPANSION_WaitForEndOfOperation,
  NULL
};

/*********************************************************************
*
*       FS_NOR_SPI_DeviceMicrochip
*/
const FS_NOR_SPI_TYPE FS_NOR_SPI_DeviceMicrochip = {
  _MICROCHIP_Identify,
  _Init,
  _MICROCHIP_SetBusWidth,
  NULL,
  _MICROCHIP_ReadApplyPara,
  _MICROCHIP_RemoveWriteProtection,
  _EraseSector,
  _WritePage,
  _WaitForEndOfOperation,
  NULL
};

/*********************************************************************
*
*       FS_NOR_SPI_DeviceWinbond
*/
const FS_NOR_SPI_TYPE FS_NOR_SPI_DeviceWinbond = {
  _WINBOND_Identify,
  _Init,
  _WINBOND_SetBusWidth,
  _WINBOND_SetNumBytesAddr,
  _SFDP_ReadApplyPara,
  _RemoveWriteProtection,
  _EraseSector,
  _WritePage,
  _WaitForEndOfOperation,
  NULL
};

/*********************************************************************
*
*       FS_NOR_SPI_DeviceWinbondDTR
*/
const FS_NOR_SPI_TYPE FS_NOR_SPI_DeviceWinbondDTR = {
  _WINBOND_IdentifyDTR,
  _Init,
  _WINBOND_SetBusWidth,
  _WINBOND_SetNumBytesAddr,
  _WINBOND_ReadApplyParaDTR,
  _RemoveWriteProtection,
  _EraseSector,
  _WritePage,
  _WaitForEndOfOperation,
  NULL
};

/*********************************************************************
*
*       FS_NOR_SPI_DeviceISSIEnhanced
*/
const FS_NOR_SPI_TYPE FS_NOR_SPI_DeviceISSIEnhanced = {
  _ISSI_IdentifyEnhanced,
  _Init,
  _ISSI_SetBusWidth,
  _ISSI_SetNumBytesAddr,
  _SFDP_ReadApplyPara,
  _RemoveWriteProtection,
  _EraseSector,
  _WritePage,
  _ISSI_WaitForEndOfOperation,
  NULL
};

/*********************************************************************
*
*       FS_NOR_SPI_DeviceISSIStandard
*/
const FS_NOR_SPI_TYPE FS_NOR_SPI_DeviceISSIStandard = {
  _ISSI_IdentifyStandard,
  _Init,
  _ISSI_SetBusWidth,
  NULL,
  _SFDP_ReadApplyPara,
  _RemoveWriteProtection,
  _EraseSector,
  _WritePage,
  _WaitForEndOfOperation,
  NULL
};

/*********************************************************************
*
*       FS_NOR_SPI_DeviceISSILegacy
*/
const FS_NOR_SPI_TYPE FS_NOR_SPI_DeviceISSILegacy = {
  _ISSI_IdentifyLegacy,
  _Init,
  _ISSI_SetBusWidth,
  NULL,
  NULL,
  _RemoveWriteProtection,
  _EraseSector,
  _WritePage,
  _WaitForEndOfOperation,
  NULL
};

/*********************************************************************
*
*       FS_NOR_SPI_DeviceMacronix
*/
const FS_NOR_SPI_TYPE FS_NOR_SPI_DeviceMacronix = {
  _MACRONIX_Identify,
  _Init,
  _MACRONIX_SetBusWidth,
  _MACRONIX_SetNumBytesAddr,
  _SFDP_ReadApplyPara,
  _RemoveWriteProtection,
  _EraseSector,
  _WritePage,
  _MACRONIX_WaitForEndOfOperation,
  NULL
};

/*********************************************************************
*
*       FS_NOR_SPI_DeviceMacronixOctal
*/
const FS_NOR_SPI_TYPE FS_NOR_SPI_DeviceMacronixOctal = {
  _MACRONIX_IdentifyOctal,
  _Init,
  NULL,
  NULL,
  NULL,
  _RemoveWriteProtection,
  _EraseSector,
  _WritePage,
  _MACRONIX_WaitForEndOfOperation,
  NULL
};

/*********************************************************************
*
*       FS_NOR_SPI_DeviceMacronixOctalSTR
*/
const FS_NOR_SPI_TYPE FS_NOR_SPI_DeviceMacronixOctalSTR = {
  _MACRONIX_OCTAL_IdentifySTR,
  _MACRONIX_OCTAL_InitSTR,
  _MACRONIX_OCTAL_SetBusWidthSTR,
  NULL,
  NULL,
  _RemoveWriteProtection,
  _MACRONIX_OCTAL_EraseSector,
  _MACRONIX_OCTAL_WritePage,
  _MACRONIX_OCTAL_WaitForEndOfOperation,
  _MACRONIX_SetNumCyclesDummy
};

/*********************************************************************
*
*       FS_NOR_SPI_DeviceMacronixOctalDTR
*/
const FS_NOR_SPI_TYPE FS_NOR_SPI_DeviceMacronixOctalDTR = {
  _MACRONIX_OCTAL_IdentifyDTR,
  _MACRONIX_OCTAL_InitDTR,
  _MACRONIX_OCTAL_SetBusWidthDTR,
  NULL,
  NULL,
  _RemoveWriteProtection,
  _MACRONIX_OCTAL_EraseSector,
  _MACRONIX_OCTAL_WritePage,
  _MACRONIX_OCTAL_WaitForEndOfOperation,
  _MACRONIX_SetNumCyclesDummy
};

/*********************************************************************
*
*       FS_NOR_SPI_DeviceGigaDeviceEnhanced
*/
const FS_NOR_SPI_TYPE FS_NOR_SPI_DeviceGigaDeviceEnhanced = {
  _GIGADEVICE_IdentifyEnhanced,
  _Init,
  _GIGADEVICE_SetBusWidth,
  _GIGADEVICE_SetNumBytesAddr,
  _SFDP_ReadApplyPara,
  _RemoveWriteProtection,
  _EraseSector,
  _WritePage,
  _GIGADEVICE_WaitForEndOfOperation,
  NULL
};

/*********************************************************************
*
*       FS_NOR_SPI_DeviceGigaDeviceStandard
*/
const FS_NOR_SPI_TYPE FS_NOR_SPI_DeviceGigaDeviceStandard = {
  _GIGADEVICE_IdentifyStandard,
  _Init,
  _GIGADEVICE_SetBusWidth,
  NULL,
  _SFDP_ReadApplyPara,
  _RemoveWriteProtection,
  _EraseSector,
  _WritePage,
  _WaitForEndOfOperation,
  NULL
};

/*********************************************************************
*
*       FS_NOR_SPI_DeviceGigaDeviceLowVoltage
*/
const FS_NOR_SPI_TYPE FS_NOR_SPI_DeviceGigaDeviceLowVoltage = {
  _GIGADEVICE_IdentifyLowVoltage,
  _Init,
  _GIGADEVICE_SetBusWidthLowVoltage,
  NULL,
  _SFDP_ReadApplyPara,
  _RemoveWriteProtection,
  _EraseSector,
  _WritePage,
  _WaitForEndOfOperation,
  NULL
};

/*********************************************************************
*
*       FS_NOR_SPI_DeviceBYT
*/
const FS_NOR_SPI_TYPE FS_NOR_SPI_DeviceBYT = {
  _BYT_Identify,
  _Init,
  _BYT_SetBusWidth,
  NULL,
  _SFDP_ReadApplyPara,
  _RemoveWriteProtection,
  _EraseSector,
  _WritePage,
  _WaitForEndOfOperation,
  NULL
};

/*********************************************************************
*
*       FS_NOR_SPI_DeviceCypress
*/
const FS_NOR_SPI_TYPE FS_NOR_SPI_DeviceCypress = {
  _CYPRESS_Identify,
  _Init,
  _CYPRESS_SetBusWidth,
  _CYPRESS_SetNumBytesAddr,
  _SFDP_ReadApplyPara,
  _RemoveWriteProtection,
  _EraseSector,
  _WritePage,
  _CYPRESS_WaitForEndOfOperation,
  NULL
};

/*********************************************************************
*
*       FS_NOR_SPI_DeviceAdestoStandard
*/
const FS_NOR_SPI_TYPE FS_NOR_SPI_DeviceAdestoStandard = {
  _ADESTO_IdentifyStandard,
  _Init,
  _ADESTO_SetBusWidth,
  NULL,
  _SFDP_ReadApplyPara,
  _RemoveWriteProtection,
  _EraseSector,
  _WritePage,
  _WaitForEndOfOperation,
  NULL
};

/*********************************************************************
*
*       FS_NOR_SPI_DeviceAdestoEnhanced
*/
const FS_NOR_SPI_TYPE FS_NOR_SPI_DeviceAdestoEnhanced = {
  _ADESTO_IdentifyEnhanced,
  _Init,
  _ADESTO_SetBusWidth,
  NULL,
  _SFDP_ReadApplyPara,
  _RemoveWriteProtection,
  _EraseSector,
  _WritePage,
  _ADESTO_WaitForEndOfOperation,
  NULL
};

/*********************************************************************
*
*       FS_NOR_SPI_DeviceEon
*/
const FS_NOR_SPI_TYPE FS_NOR_SPI_DeviceEon = {
  _EON_Identify,
  _Init,
  NULL,
  NULL,
  _EON_ReadApplyPara,
  _RemoveWriteProtection,
  _EraseSector,
  _WritePage,
  _WaitForEndOfOperation,
  NULL
};

/*********************************************************************
*
*       FS_NOR_SPI_DeviceDefault
*/
const FS_NOR_SPI_TYPE FS_NOR_SPI_DeviceDefault = {
  NULL,
  _Init,
  NULL,
  NULL,
  _SFDP_ReadApplyPara,
  _RemoveWriteProtection,
  _EraseSector,
  _WritePage,
  _WaitForEndOfOperation,
  NULL
};

/*********************************************************************
*
*       _apDeviceAll
*/
static const FS_NOR_SPI_TYPE * _apDeviceAll[] = {
  &FS_NOR_SPI_DeviceMicron,
  &FS_NOR_SPI_DeviceMicron_x2,
  &FS_NOR_SPI_DeviceSpansion,
  &FS_NOR_SPI_DeviceMicrochip,
  &FS_NOR_SPI_DeviceWinbondDTR,
  &FS_NOR_SPI_DeviceWinbond,
  &FS_NOR_SPI_DeviceISSIEnhanced,
  &FS_NOR_SPI_DeviceISSIStandard,
  &FS_NOR_SPI_DeviceISSILegacy,
  &FS_NOR_SPI_DeviceMacronix,
  &FS_NOR_SPI_DeviceMacronixOctalSTR,
  &FS_NOR_SPI_DeviceMacronixOctalDTR,
  &FS_NOR_SPI_DeviceMacronixOctal,
  &FS_NOR_SPI_DeviceGigaDeviceEnhanced,
  &FS_NOR_SPI_DeviceGigaDeviceStandard,
  &FS_NOR_SPI_DeviceGigaDeviceLowVoltage,
  &FS_NOR_SPI_DeviceBYT,
  &FS_NOR_SPI_DeviceCypress,
  &FS_NOR_SPI_DeviceAdestoStandard,
  &FS_NOR_SPI_DeviceAdestoEnhanced,
  &FS_NOR_SPI_DeviceEon,
  &FS_NOR_SPI_DeviceDefault
};

/*********************************************************************
*
*       _apDeviceDefault
*/
static const FS_NOR_SPI_TYPE * _apDeviceDefault[] = {       // Micron is also included because the first version of the SPIFI phy. layer supported the special features of these devices.
  &FS_NOR_SPI_DeviceMicron,
  &FS_NOR_SPI_DeviceDefault
};

/*********************************************************************
*
*       _apDeviceMicron
*/
static const FS_NOR_SPI_TYPE * _apDeviceMicron[] = {
  &FS_NOR_SPI_DeviceMicron
};

/*********************************************************************
*
*       _apDeviceMicron_x
*/
static const FS_NOR_SPI_TYPE * _apDeviceMicron_x[] = {
  &FS_NOR_SPI_DeviceMicron,
  &FS_NOR_SPI_DeviceMicron_x2
};

/*********************************************************************
*
*       _apDeviceMicron_x2
*/
static const FS_NOR_SPI_TYPE * _apDeviceMicron_x2[] = {
  &FS_NOR_SPI_DeviceMicron_x2
};

/*********************************************************************
*
*       _apDeviceSpansion
*/
static const FS_NOR_SPI_TYPE * _apDeviceSpansion[] = {
  &FS_NOR_SPI_DeviceSpansion
};

/*********************************************************************
*
*       _apDeviceMicrochip
*/
static const FS_NOR_SPI_TYPE * _apDeviceMicrochip[] = {
  &FS_NOR_SPI_DeviceMicrochip
};

/*********************************************************************
*
*       _apDeviceWinbond
*/
static const FS_NOR_SPI_TYPE * _apDeviceWinbond[] = {
  &FS_NOR_SPI_DeviceWinbondDTR,
  &FS_NOR_SPI_DeviceWinbond
};

/*********************************************************************
*
*       _apDeviceISSI
*/
static const FS_NOR_SPI_TYPE * _apDeviceISSI[] = {
  &FS_NOR_SPI_DeviceISSIEnhanced,
  &FS_NOR_SPI_DeviceISSIStandard,
  &FS_NOR_SPI_DeviceISSILegacy
};

/*********************************************************************
*
*       _apDeviceMacronix
*/
static const FS_NOR_SPI_TYPE * _apDeviceMacronix[] = {
  &FS_NOR_SPI_DeviceMacronix,
  &FS_NOR_SPI_DeviceMacronixOctal
};

/*********************************************************************
*
*       _apDeviceMacronixOctal
*/
static const FS_NOR_SPI_TYPE * _apDeviceMacronixOctal[] = {
  &FS_NOR_SPI_DeviceMacronixOctalSTR,
  &FS_NOR_SPI_DeviceMacronixOctalDTR
};

/*********************************************************************
*
*       _apDeviceGigaDevice
*/
static const FS_NOR_SPI_TYPE * _apDeviceGigaDevice[] = {
  &FS_NOR_SPI_DeviceGigaDeviceEnhanced,
  &FS_NOR_SPI_DeviceGigaDeviceStandard,
  &FS_NOR_SPI_DeviceGigaDeviceLowVoltage
};

/*********************************************************************
*
*       _apDeviceBYT
*/
static const FS_NOR_SPI_TYPE * _apDeviceBYT[] = {
  &FS_NOR_SPI_DeviceBYT
};

/*********************************************************************
*
*       _apDeviceCypress
*/
static const FS_NOR_SPI_TYPE * _apDeviceCypress[] = {
  &FS_NOR_SPI_DeviceCypress
};

/*********************************************************************
*
*       _apDeviceAdesto
*/
static const FS_NOR_SPI_TYPE * _apDeviceAdesto[] = {
  &FS_NOR_SPI_DeviceAdestoStandard,
  &FS_NOR_SPI_DeviceAdestoEnhanced
};

/*********************************************************************
*
*       _apDeviceEon
*/
static const FS_NOR_SPI_TYPE * _apDeviceEon[] = {
  &FS_NOR_SPI_DeviceEon
};

/*********************************************************************
*
*       Public code (used internally)
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_NOR_SPI_ReadId
*
*  Function description
*    Reads the id information from serial NOR flash device.
*/
void FS_NOR_SPI_ReadId(const FS_NOR_SPI_INST * pInst, U8 * pData, unsigned NumBytes) {
  _ReadId(pInst, pData, NumBytes);
}

/*********************************************************************
*
*       FS_NOR_SPI_ReadApplyParaById
*
*  Function description
*    Tries to identifies the parameters of the serial NOR flash device by using the device id.
*
*  Return value
*    ==0      OK, serial NOR flash device identified.
*    !=0      Error, could not identify the serial NOR flash device.
*/
int FS_NOR_SPI_ReadApplyParaById(FS_NOR_SPI_INST * pInst) {
  U8                        aId[3];     // Only the 3rd byte of the id data is used.
  const DEVICE_INFO       * pDeviceInfo;
  U8                        Id;
  FS_NOR_SPI_SECTOR_BLOCK * pSectorBlock;

  pDeviceInfo = _aDeviceInfo;
  //
  // Read the id from the serial NOR flash device.
  // We use the value of 3rd byte to identify the device.
  //
  _ReadId(pInst, aId, sizeof(aId));
  //
  // Look up the device id in the list.
  //
  for (;;) {
    Id = pDeviceInfo->Id;
    if (Id == aId[2]) {
      break;                      // OK, found a matching id.
    }
    if (Id == 0u) {
      break;                      // Error, could not find a matching device.
    }
    pDeviceInfo++;
  }
  if (Id == 0u) {
    return 1;                     // Error, could not identify device
  }
  //
  // Store the device parameters to instance.
  //
  pSectorBlock = &pInst->aSectorBlock[0];
  pSectorBlock->NumSectors       = 1uL << pDeviceInfo->ldNumSectors;
  pSectorBlock->CmdErase         = CMD_SE;
  pSectorBlock->ldBytesPerSector = pDeviceInfo->ldBytesPerSector;
  pInst->NumSectorBlocks   = 1;
  pInst->NumBytesAddr      = pDeviceInfo->NumBytesAddr;
  pInst->CmdRead           = CMD_FAST_READ;
  pInst->BusWidthRead      = (U16)FS_BUSWIDTH_MAKE(1uL, 1uL, 1uL);
  pInst->NumBytesReadDummy = 1;
  return 0;                       // OK, device identified.
}

/*********************************************************************
*
*      FS_NOR_SPI_GetSectorOff
*
*  Function description
*    Returns the byte offset of a relative sector.
*/
U32 FS_NOR_SPI_GetSectorOff(const FS_NOR_SPI_INST * pInst, U32 SectorIndex) {
  unsigned                        NumSectorBlocks;
  U32                             NumSectors;
  unsigned                        ldBytesPerSector;
  U32                             Off;
  const FS_NOR_SPI_SECTOR_BLOCK * pSectorBlock;

  pSectorBlock    = pInst->aSectorBlock;
  NumSectorBlocks = pInst->NumSectorBlocks;
  Off             = 0;
  if (NumSectorBlocks != 0u) {
    //
    // Optimization for common case where the serial NOR device has uniform sectors.
    //
    if (NumSectorBlocks == 1u) {
      ldBytesPerSector = pSectorBlock->ldBytesPerSector;
      Off              = SectorIndex << ldBytesPerSector;
    } else {
      do {
        NumSectors       = pSectorBlock->NumSectors;
        ldBytesPerSector = pSectorBlock->ldBytesPerSector;
        if (SectorIndex < NumSectors) {
          NumSectors = SectorIndex;
        }
        Off += NumSectors << ldBytesPerSector;
        SectorIndex -= NumSectors;                      // Number of remaining sectors
        if (SectorIndex == 0u) {
          break;
        }
        ++pSectorBlock;
      } while (--NumSectorBlocks != 0u);
    }
  }
  return Off;
}

/*********************************************************************
*
*      FS_NOR_SPI_GetSectorSize
*
*  Function description
*    Returns the number of bytes in a physical sector.
*/
U32 FS_NOR_SPI_GetSectorSize(const FS_NOR_SPI_INST * pInst, U32 SectorIndex) {
  unsigned                        NumSectorBlocks;
  U32                             NumSectors;
  U32                             BytesPerSector;
  const FS_NOR_SPI_SECTOR_BLOCK * pSectorBlock;

  BytesPerSector  = 0;                // Set to indicate an error.
  pSectorBlock    = pInst->aSectorBlock;
  NumSectorBlocks = pInst->NumSectorBlocks;
  if (NumSectorBlocks != 0u) {
    //
    // Optimization for common case where the serial NOR device has uniform sectors.
    //
    if (NumSectorBlocks == 1u) {
      BytesPerSector = 1uL << pSectorBlock->ldBytesPerSector;
    } else {
      do {
        NumSectors = pSectorBlock->NumSectors;
        if (SectorIndex < NumSectors) {
          BytesPerSector = 1uL << pSectorBlock->ldBytesPerSector;
          break;
        }
        SectorIndex -= NumSectors;    // Number of remaining sectors
        ++pSectorBlock;
      } while (--NumSectorBlocks != 0u);
    }
  }
  return BytesPerSector;
}

/*********************************************************************
*
*      FS_NOR_SPI_GetSectorEraseCmd
*
*  Function description
*    Returns the command used for the erasing of the specified physical sector.
*/
U8 FS_NOR_SPI_GetSectorEraseCmd(const FS_NOR_SPI_INST * pInst, U32 SectorIndex) {
  unsigned                        NumSectorBlocks;
  U32                             NumSectors;
  U8                              Cmd;
  const FS_NOR_SPI_SECTOR_BLOCK * pSectorBlock;

  Cmd             = 0;                // Set to indicate an error.
  pSectorBlock    = pInst->aSectorBlock;
  NumSectorBlocks = pInst->NumSectorBlocks;
  if (NumSectorBlocks != 0u) {
    //
    // Optimization for common case where the serial NOR device has uniform sectors.
    //
    if (NumSectorBlocks == 1u) {
      Cmd = pSectorBlock->CmdErase;
    } else {
      do {
        NumSectors = pSectorBlock->NumSectors;
        if (SectorIndex < NumSectors) {
          Cmd = pSectorBlock->CmdErase;
          break;
        }
        SectorIndex -= NumSectors;    // Number of remaining sectors
        ++pSectorBlock;
      } while (--NumSectorBlocks != 0u);
    }
  }
  return Cmd;
}

/*********************************************************************
*
*       Public const data
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_NOR_SPI_DeviceListAll
*/
const FS_NOR_SPI_DEVICE_LIST FS_NOR_SPI_DeviceListAll = {
  (U8)SEGGER_COUNTOF(_apDeviceAll),
  _apDeviceAll
};

/*********************************************************************
*
*       FS_NOR_SPI_DeviceListDefault
*/
const FS_NOR_SPI_DEVICE_LIST FS_NOR_SPI_DeviceListDefault = {
  (U8)SEGGER_COUNTOF(_apDeviceDefault),
  _apDeviceDefault
};

/*********************************************************************
*
*       FS_NOR_SPI_DeviceListMicron
*/
const FS_NOR_SPI_DEVICE_LIST FS_NOR_SPI_DeviceListMicron = {
  (U8)SEGGER_COUNTOF(_apDeviceMicron),
  _apDeviceMicron
};

/*********************************************************************
*
*       FS_NOR_SPI_DeviceListMicron_x
*/
const FS_NOR_SPI_DEVICE_LIST FS_NOR_SPI_DeviceListMicron_x = {
  (U8)SEGGER_COUNTOF(_apDeviceMicron_x),
  _apDeviceMicron_x
};

/*********************************************************************
*
*       FS_NOR_SPI_DeviceListMicron_x2
*/
const FS_NOR_SPI_DEVICE_LIST FS_NOR_SPI_DeviceListMicron_x2 = {
  (U8)SEGGER_COUNTOF(_apDeviceMicron_x2),
  _apDeviceMicron_x2
};

/*********************************************************************
*
*       FS_NOR_SPI_DeviceListSpansion
*/
const FS_NOR_SPI_DEVICE_LIST FS_NOR_SPI_DeviceListSpansion = {
  (U8)SEGGER_COUNTOF(_apDeviceSpansion),
  _apDeviceSpansion
};

/*********************************************************************
*
*       FS_NOR_SPI_DeviceListMicrochip
*/
const FS_NOR_SPI_DEVICE_LIST FS_NOR_SPI_DeviceListMicrochip = {
  (U8)SEGGER_COUNTOF(_apDeviceMicrochip),
  _apDeviceMicrochip
};

/*********************************************************************
*
*       FS_NOR_SPI_DeviceListWinbond
*/
const FS_NOR_SPI_DEVICE_LIST FS_NOR_SPI_DeviceListWinbond = {
  (U8)SEGGER_COUNTOF(_apDeviceWinbond),
  _apDeviceWinbond
};

/*********************************************************************
*
*       FS_NOR_SPI_DeviceListISSI
*/
const FS_NOR_SPI_DEVICE_LIST FS_NOR_SPI_DeviceListISSI = {
  (U8)SEGGER_COUNTOF(_apDeviceISSI),
  _apDeviceISSI
};

/*********************************************************************
*
*       FS_NOR_SPI_DeviceListMacronix
*/
const FS_NOR_SPI_DEVICE_LIST FS_NOR_SPI_DeviceListMacronix = {
  (U8)SEGGER_COUNTOF(_apDeviceMacronix),
  _apDeviceMacronix
};

/*********************************************************************
*
*       FS_NOR_SPI_DeviceListMacronixOctal
*/
const FS_NOR_SPI_DEVICE_LIST FS_NOR_SPI_DeviceListMacronixOctal = {
  (U8)SEGGER_COUNTOF(_apDeviceMacronixOctal),
  _apDeviceMacronixOctal
};

/*********************************************************************
*
*       FS_NOR_SPI_DeviceListGigaDevice
*/
const FS_NOR_SPI_DEVICE_LIST FS_NOR_SPI_DeviceListGigaDevice = {
  (U8)SEGGER_COUNTOF(_apDeviceGigaDevice),
  _apDeviceGigaDevice
};

/*********************************************************************
*
*       FS_NOR_SPI_DeviceListBYT
*/
const FS_NOR_SPI_DEVICE_LIST FS_NOR_SPI_DeviceListBYT = {
  (U8)SEGGER_COUNTOF(_apDeviceBYT),
  _apDeviceBYT
};

/*********************************************************************
*
*       FS_NOR_SPI_DeviceListCypress
*/
const FS_NOR_SPI_DEVICE_LIST FS_NOR_SPI_DeviceListCypress = {
  (U8)SEGGER_COUNTOF(_apDeviceCypress),
  _apDeviceCypress
};

/*********************************************************************
*
*       FS_NOR_SPI_DeviceListAdesto
*/
const FS_NOR_SPI_DEVICE_LIST FS_NOR_SPI_DeviceListAdesto = {
  (U8)SEGGER_COUNTOF(_apDeviceAdesto),
  _apDeviceAdesto
};

/*********************************************************************
*
*       FS_NOR_SPI_DeviceListEon
*/
const FS_NOR_SPI_DEVICE_LIST FS_NOR_SPI_DeviceListEon = {
  (U8)SEGGER_COUNTOF(_apDeviceEon),
  _apDeviceEon
};

/*************************** End of file ****************************/
