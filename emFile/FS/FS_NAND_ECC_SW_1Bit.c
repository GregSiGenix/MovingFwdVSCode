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
File       : FS_NAND_ECC_SW_1Bit.c
Purpose    : ECC routines for correcting 1 bit error over 512 bytes of data + 4 bytes of spare area.
Literature :
  [1] ECC Algorithm (512 Byte)
    (\\fileserver\Techinfo\Company\Samsung\NAND_Flash\ecc_algorithm_for_web_512b.pdf)
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
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
#define LD_NUM_BYTES_DATA     9u
#define NUM_BYTES_DATA        (1uL << LD_NUM_BYTES_DATA)
#define OFF_SPARE_DATA        4u
#define OFF_SPARE_ECC_DATA0   8u
#define OFF_SPARE_ECC_DATA1   11u
#define OFF_SPARE_ECC_SPARE   14u
#define NUM_BIT_ERRORS        1

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _StoreU16LE
*
*  Function description
*    Writes 16 bit little endian.
*/
static
FS_OPTIMIZE
void _StoreU16LE(U8 *pBuffer, unsigned Data) {
  *pBuffer++ = (U8)Data;
  Data >>= 8;
  *pBuffer = (U8)Data;
}

/*********************************************************************
*
*       _LoadU16LE
*
*  Function description
*    Reads a 16 bit little endian from a char array.
*/
static
FS_OPTIMIZE
U16 _LoadU16LE(const U8 *pBuffer) {
  U16 r;

  r = (U16)*pBuffer | ((U16)pBuffer[1] << 8);
  return r;
}

/*********************************************************************
*
*       _StoreU24LE
*
*  Function description
*    Stores the 24 bit ECC to memory
*/
static
FS_OPTIMIZE
void _StoreU24LE(U8 * p, U32 ecc) {
  *p++ = (U8)(ecc >>  0);
  *p++ = (U8)(ecc >>  8);
  *p   = (U8)(ecc >> 16);
}

/*********************************************************************
*
*       _LoadU24LE
*
*  Function description
*    Loads a 24 bit ECC from memory
*/
static
FS_OPTIMIZE
U32 _LoadU24LE(const U8 * p) {
  U32 ecc;
  ecc  = *p++;
  ecc |= ((U32)*p++) <<  8;
  ecc |= ((U32)*p++) << 16;
  return ecc;
}

/*********************************************************************
*
*       _StoreU32LE
*
*  Function description
*    Stores 32 bits little endian into memory.
*/
static
FS_OPTIMIZE
void _StoreU32LE(U8 *pBuffer, U32 Data) {
  *pBuffer++ = (U8)Data;
  Data >>= 8;
  *pBuffer++ = (U8)Data;
  Data >>= 8;
  *pBuffer++ = (U8)Data;
  Data >>= 8;
  *pBuffer   = (U8)Data;
}

/*********************************************************************
*
*       _LoadU32LE
*
*  Function description
*    Reads a 32 bit little endian from a char array.
*/
static
FS_OPTIMIZE
U32 _LoadU32LE(const U8 *pBuffer) {
  U32 r;
  r = (U32)pBuffer[3] & 0x000000FFu;
  r <<= 8;
  r += (U32)pBuffer[2] & 0x000000FFu;
  r <<= 8;
  r += (U32)pBuffer[1] & 0x000000FFu;
  r <<= 8;
  r += (U32)pBuffer[0] & 0x000000FFu;
  return r;
}

/*********************************************************************
*
*       _CalcParity32
*
*  Function description
*    Computes the parity of a 32-bit item.
*    Returns 0 for even parity, 1 for odd parity.
*
*  Notes
*    (1) Most compilers will (and should) inline this code in higher optimization levels
*/
static
FS_OPTIMIZE
U16 _CalcParity32(U32 Data) {
  Data = (Data >> 16) ^ Data;           // Reduce 32 bits to 16 bits
  Data = (Data >>  8) ^ Data;           // Reduce 16 bits to 8 bits
  Data = (Data >>  4) ^ Data;           // Reduce 8 bits to 4 bits
  Data = (Data >>  2) ^ Data;           // Reduce 4 bits to 2 bits
  Data = (Data >>  1) ^ Data;           // Reduce 2 bits to 1 bit
  return (U16)(Data & 1u);
}

/*********************************************************************
*
*       _ParityToECC
*
*  Function description
*    Compute the ECC Pn bits (located at odd bit positions)
*/
static
FS_OPTIMIZE
U32 _ParityToECC(U32 ParLo, U32 ParHi) {
  U32 ecc;
  U32 Necc;
  ecc  = (U32)_CalcParity32(ParLo & 0xAAAAAAAAuL) << 19;  // p1
  ecc |= (U32)_CalcParity32(ParLo & 0xCCCCCCCCuL) << 21;  // p2
  ecc |= (U32)_CalcParity32(ParLo & 0xF0F0F0F0uL) << 23;  // p4
  ecc |= (U32)_CalcParity32(ParLo & 0xFF00FF00uL) << 1;   // p8
  ecc |= (U32)_CalcParity32(ParLo & 0xFFFF0000uL) << 3;   // p16

  ecc |= (ParHi & (1uL << 0)) << 5;                       // p32
  ecc |= (ParHi & (1uL << 1)) << 6;                       // p64
  ecc |= (ParHi & (1uL << 2)) << 7;                       // p128
  ecc |= (ParHi & (1uL << 3)) << 8;                       // p256
  ecc |= (ParHi & (1uL << 4)) << 9;                       // p512
  ecc |= (ParHi & (1uL << 5)) << 10;                      // p1024
  //
  // Compute the even bits of the ECC: Pn' = Pn ^ P;
  //
  Necc = ecc >> 1;
  if (_CalcParity32(ParLo) != 0u) {
    Necc ^= 0x00545555uL;
  }
  ecc |= Necc;
  return ecc ^ 0xFCFFFFuL;      // Note: Bits 16 and 17 are not used, therefor 0
}

/*********************************************************************
*
*       _ECC1_256_Calc
*
*  Function description
*    Calculates the ECC on a given 256 bytes block.
*/
static
FS_OPTIMIZE
U32 _ECC1_256_Calc(const U32 * pData) {
  U32 i = 0;
  U32 ParLo  = 0;       // Parity info for low bits 0..4
  U32 Par32  = 0;       // Parity info for bit 5  (A2 == 1)
  U32 Par64  = 0;       // Parity info for bit 6  (A3 == 1)
  U32 Par128 = 0;       // Parity info for bit 7  (A4 == 1)
  U32 ParHi  = 0;       // Parity info for high bits 8..10
  //
  // Load all data as words and xor into the variables.
  //
  do {
    U32 Data;
    U32 Par = 0;
    Data = *pData++; Par ^= Data;
    Data = *pData++; Par ^= Data; Par32 ^= Data;
    Data = *pData++; Par ^= Data;                Par64 ^= Data;
    Data = *pData++; Par ^= Data; Par32 ^= Data; Par64 ^= Data;
    Data = *pData++; Par ^= Data;                               Par128 ^= Data;
    Data = *pData++; Par ^= Data; Par32 ^= Data;                Par128 ^= Data;
    Data = *pData++; Par ^= Data;                Par64 ^= Data; Par128 ^= Data;
    Data = *pData++; Par ^= Data; Par32 ^= Data; Par64 ^= Data; Par128 ^= Data;
    ParLo ^= Par;
    //
    // Compute High parity bits
    //
    ParHi ^= i * _CalcParity32(Par);
  } while (++i < 8u);
  ParHi <<= 3;
  ParHi |= (U32)_CalcParity32(Par32);
  ParHi |= (U32)_CalcParity32(Par64)  << 1;
  ParHi |= (U32)_CalcParity32(Par128) << 2;
  return _ParityToECC(ParLo, ParHi);
}

/*********************************************************************
*
*       _ECC1_256_Apply
*
*  Function description
*    Uses the ECC values to correct the data if necessary
*    Works on a 256 bytes block.
*
*  Parameters
*    pData      [IN]  Data to be checked. Can not be NULL.
*               [OUT] Corrected data.
*    pECCRead   [IN]  ECC read from device. Can not be NULL.
*               [OUT] Corrected ECC.
*
*  Return value
*    >=0        Number of bit errors corrected.
*    < 0        Correction not possible.
*/
static
FS_OPTIMIZE
int _ECC1_256_Apply(U32 * pData, U32 * pECCRead) {
  U32      eccCalced;
  U32      eccXor;
  unsigned i;
  int      NumDiffBits;
  unsigned BitPos;
  unsigned Off;

  eccCalced = _ECC1_256_Calc(pData);
  eccXor = eccCalced ^ *pECCRead;
  if (eccXor == 0u) {
    return 0;                                       // Both ECCs match, data is O.K. without correction.
  }
  //
  // Count number of different bits in both ECCs
  //
  NumDiffBits = 0;
  for (i = 0; i < 24u; i++) {
    if ((eccXor & (1uL << i)) != 0u) {
      NumDiffBits++;
    }
  }
  //
  // Check if this is a correctable error
  //
  if (NumDiffBits == 1) {
    *pECCRead = eccCalced;
    return 1;                                       // Error in ECC.
  }
  if (NumDiffBits != 11) {
    return -1;                                      // Uncorrectable error bit error detected.
  }
  //
  // Perform correction
  //
  BitPos =   ((eccXor >> 19) & 1u)
          | (((eccXor >> 21) & 1u) << 1)
          | (((eccXor >> 23) & 1u) << 2)
          | (((eccXor >>  1) & 1u) << 3)
          | (((eccXor >>  3) & 1u) << 4);
  Off =      ((eccXor >>  5) & 1u)
          | (((eccXor >>  7) & 1u) << 1)
          | (((eccXor >>  9) & 1u) << 2)
          | (((eccXor >> 11) & 1u) << 3)
          | (((eccXor >> 13) & 1u) << 4)
          | (((eccXor >> 15) & 1u) << 5);
  *(pData + Off) ^= (U32)(1uL << BitPos);
  return 1;                                         // 1 bit error has been corrected.
}

/*********************************************************************
*
*       _ECC1_4_Calc
*
*   Function description
*     Computes a 1-bit ECC over 4 bytes. Typ. used to protect the data from the spare area.
*/
static
FS_OPTIMIZE
U16 _ECC1_4_Calc(const U8 * pData) {
  U16 ecc;
  U16 Necc;
  U32 Data32;

  //
  // Load the data in a 32-bit word.
  //
  Data32 = _LoadU32LE(pData);
  //
  // Compute the ECC Pn bits (located at odd bit positions).
  // Bit locations were taken from [1].
  //
  ecc  = (U16)(_CalcParity32(Data32 & 0xAAAAAAAAuL) << 5);   // p1
  ecc |= (U16)(_CalcParity32(Data32 & 0xCCCCCCCCuL) << 7);   // p2
  ecc |= (U16)(_CalcParity32(Data32 & 0xF0F0F0F0uL) << 9);   // p4
  ecc |= (U16)(_CalcParity32(Data32 & 0xFF00FF00uL) << 1);   // p8
  ecc |= (U16)(_CalcParity32(Data32 & 0xFFFF0000uL) << 3);   // p16
  //
  // Compute the even bits of the ECC: Pn' = Pn ^ P;
  //
  Necc = ecc >> 1;
  if (_CalcParity32(Data32) != 0u) {
    Necc ^= 0x5555u;
  }
  ecc |= Necc;
  ecc ^= 0xFFFFu;
  return ecc;
}

/*********************************************************************
*
*       _ECC1_4_Apply
*
*  Function description
*    Checks and corrects 4 byte data using 1-bit ECC.
*
*  Parameters
*    pData      [IN]  Data to be checked. Can not be NULL.
*               [OUT] Corrected data.
*    pECCRead   [IN]  ECC read from device. Can not be NULL.
*               [OUT] Corrected ECC.
*
*  Return value
*    >=0        Number of bit errors corrected.
*    < 0        Correction not possible.
*/
static
FS_OPTIMIZE
int _ECC1_4_Apply(U8 * pData, U16 * pECCRead) {
  unsigned eccCalced;
  unsigned eccXor;
  unsigned i;
  int      NumDiffBits;
  unsigned BitPos;
  U32      Data32;

  eccCalced = _ECC1_4_Calc(pData);
  eccXor = eccCalced ^ *pECCRead;
  if (eccXor == 0u) {
    return 0;                                       // Both ECCs match, data is O.K. without correction.
  }
  //
  // Count number of different bits in both ECCs
  //
  NumDiffBits = 0;
  for (i = 0; i < 16u; i++) {
    if ((eccXor & (1uL << i)) != 0u) {
      NumDiffBits++;
    }
  }
  //
  // Check if this is a correctable error
  //
  if (NumDiffBits == 1) {
    *pECCRead = (U16)eccCalced;
    return 1;                                       // Error in ECC.
  }
  if (NumDiffBits != 8) {
    return -1;                                      // Uncorrectable bit error detected.
  }
  //
  // Perform correction
  //
  Data32 = _LoadU32LE(pData);
  BitPos =  0u
         |  ((eccXor >> 5) & 1u)
         | (((eccXor >> 7) & 1u) << 1)
         | (((eccXor >> 9) & 1u) << 2)
         | (((eccXor >> 1) & 1u) << 3)
         | (((eccXor >> 3) & 1u) << 4)
         ;
  Data32 ^= (U32)(1uL << BitPos);
  _StoreU32LE(pData, Data32);
  return 1;                                         // 1 bit error has been corrected.
}

/*********************************************************************
*
*       _Calc
*
*  Function description
*    Computes a 1-bit ECC over 512 data bytes and 4 spare bytes.
*    3 separate ECCs are calculated: 2 for each 256 byte of data block
*    and 1 for the 4 bytes of the spare area. The ECC is stored
*    to byte offset 8 in the spare area
*
*  Parameters
*    pData      [IN]  512 bytes of data to be protected by ECC.
*    pSpare     [IN]  4 bytes of spare area to be protected by ECC starting at byte offset 4.
*               [OUT] ECC stored at byte offset 8.
*
*  Notes
*    (1) Bits 17/16 are not used. We set them to 1 to avoid ECC errors on a blank block.
*/
static
FS_OPTIMIZE
void _Calc(const U32 * pData, U8 * pSpare) {
  U32 eccData0;
  U32 eccData1;
  U16 eccSpare;

  //
  // Compute the ECC of the data area and spare area.
  //
  eccData0  = _ECC1_256_Calc(pData);
  eccData0 |= 0x30000uL;                    // Note 1
  eccData1  = _ECC1_256_Calc(pData + 64);
  eccData1 |= 0x30000uL;                    // Note 1
  eccSpare  = _ECC1_4_Calc(pSpare + OFF_SPARE_DATA);
  //
  // Encode the computed ECC to spare area.
  //
  _StoreU24LE(pSpare + OFF_SPARE_ECC_DATA0, eccData0);
  _StoreU24LE(pSpare + OFF_SPARE_ECC_DATA1, eccData1);
  _StoreU16LE(pSpare + OFF_SPARE_ECC_SPARE, eccSpare);
}

/*********************************************************************
*
*       _Apply
*
*  Function description
*    Checks and corrects for errors 512 data bytes and 4 spare bytes using 1-bit ECC.
*
*  Parameters
*    pData      [IN]  512 bytes of data to be checked.
*               [OUT] 512 bytes of data with corrected bit error.
*    pSpare     [IN]  4 bytes of spare area + 8 bytes ECC to be checked.
*               [OUT] 4 bytes of spare area + 8 bytes ECC with bit errors corrected.
*
*  Return value
*    >=0        Number of bit errors corrected.
*    < 0        Correction not possible.
*
*  Notes
*    (1) Bits 17/16 are not used. The _ECC1_256_Apply routine expects them to be 0.
*/
static
FS_OPTIMIZE
int _Apply(U32 * pData, U8 * pSpare) {
  U32 eccData0;
  U32 eccData1;
  U32 eccData0Loaded;
  U32 eccData1Loaded;
  U16 eccSpare;
  int r;
  int Result;

  Result         = 0;
  eccData0       = 0;
  eccData1       = 0;
  eccData0Loaded = 0;
  eccData1Loaded = 0;
  if (pData != NULL) {
    eccData0Loaded = _LoadU24LE(pSpare + OFF_SPARE_ECC_DATA0);
    eccData0       = eccData0Loaded & ~0x30000uL;               // Note 1
    eccData1Loaded = _LoadU24LE(pSpare + OFF_SPARE_ECC_DATA1);
    eccData1       = eccData1Loaded & ~0x30000uL;               // Note 1
  }
  eccSpare = _LoadU16LE(pSpare + OFF_SPARE_ECC_SPARE);
  if (pData != NULL) {
    r = _ECC1_256_Apply(pData, &eccData0);
    if (r < 0) {
      return r;                               // Error, could not correct bits.
    }
    Result = r;
    r = _ECC1_256_Apply(pData + 64, &eccData1);
    if (r < 0) {
      return r;                               // Error, could not correct bits.
    }
    if (r > Result) {
      Result = r;
    }
  }
  r = _ECC1_4_Apply(pSpare + OFF_SPARE_DATA, &eccSpare);
  if (r < 0) {
    return r;
  }
  if (r > Result) {
    Result = r;
  }
  if (pData != NULL) {
    //
    // Restore the cleared bits. This is required so that the Universal NAND driver
    // can correctly detect a blank page.
    //
    eccData0 &= ~0x30000uL;
    eccData0 |= eccData0Loaded & 0x30000uL;
    eccData1 &= ~0x30000uL;
    eccData1 |= eccData1Loaded & 0x30000uL;
    //
    // Store the corrected ECC.
    //
    _StoreU24LE(pSpare + OFF_SPARE_ECC_DATA0, eccData0);
    _StoreU24LE(pSpare + OFF_SPARE_ECC_DATA1, eccData1);
  }
  _StoreU16LE(pSpare + OFF_SPARE_ECC_SPARE, eccSpare);
  return Result;
}

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_NAND_ECC_SW_1Bit_Validate
*
*  Function description
*    Checks if the ECC encoding / decoding routines work correctly.
*
*  Return value
*    ==0    Routines verified successfully
*    !=0    An error occurred
*/
int FS_NAND_ECC_SW_1Bit_Validate(void) {
  U32        aData[NUM_BYTES_DATA / 4u];
  U32        aDataCheck[sizeof(aData) / 4u];
  U8         abSpare[16];
  U8         abSpareCheck[sizeof(abSpare)];
  U8       * pData;
  U8       * pSpare;
  int        r;
  U32        i;
  unsigned   j;
  int        k;
  U8         Mask;
  U8         Data;

  pData = (U8 *)aData;
  pSpare = abSpare;
  //
  // Encoding test.
  //
  for (i = 0; i < sizeof(aData); ++i) {
    *(pData + i) = (U8)(i % 29u);
  }
  FS_MEMSET(abSpare, 0xFF, sizeof(abSpare));
  abSpare[4] = 0x12;
  abSpare[5] = 0x34;
  abSpare[6] = 0x56;
  abSpare[7] = 0x78;
  _Calc(aData, abSpare);
  if ((abSpare[8]  != 0x3Fu) ||
      (abSpare[9]  != 0xF3u) ||
      (abSpare[10] != 0xFFu) ||
      (abSpare[11] != 0xF3u) ||
      (abSpare[12] != 0xFFu) ||
      (abSpare[13] != 0xFFu) ||
      (abSpare[14] != 0x59u) ||
      (abSpare[15] != 0xAAu)) {
    return 1;
  }
  //
  // Decoding test (without error).
  //
  r = _Apply(aData, abSpare);
  if (r != 0) {
    return 1;
  }
  //
  // Toggle each bit in the data area and check if the error is corrected.
  //
  FS_MEMCPY(aDataCheck, aData, sizeof(aDataCheck));
  FS_MEMCPY(abSpareCheck, abSpare, sizeof(abSpareCheck));
  for (i = 0; i < sizeof(aData); ++i) {
    for (j = 0; j < 8u; ++j) {
      Mask = (U8)(1u << j);
      //
      // Change the bit value.
      //
      Data          = *(pData + i);
      Data         ^= Mask;
      *(pData + i)  = Data;
      //
      // Correct the bit error.
      //
      r = _Apply(aData, abSpare);
      if (r != 1) {
        return 1;
      }
      //
      // Verify the data and the spare area.
      //
      for (k = 0; k < (int)SEGGER_COUNTOF(aData); ++k) {
        if (aData[k] != aDataCheck[k]) {
          return 1;
        }
      }
      for (k = 0; k < (int)sizeof(abSpare); ++k) {
        if (abSpare[k] != abSpareCheck[k]) {
          return 1;
        }
      }
    }
  }
  //
  // Toggle each bit in the spare area and check if the error is corrected.
  //
  for (i = 0; i < sizeof(abSpare); ++i) {
    //
    // Do not test unused bytes in the spare area.
    //
    if (i < OFF_SPARE_DATA) {
      continue;
    }
    for (j = 0; j < 8u; ++j) {
      //
      // Do not test the unused bits in the ECC.
      //
      if (((i == 10u) || (i == 13u)) && ((j == 0u) || (j == 1u))) {
        continue;
      }
      Mask = (U8)(1u << j);
      //
      // Change the bit value.
      //
      Data           = *(pSpare + i);
      Data          ^= Mask;
      *(pSpare + i)  = Data;
      //
      // Correct the bit error.
      //
      r = _Apply(aData, abSpare);
      if (r != 1) {
        return 1;
      }
      //
      // Verify the data and the spare area.
      //
      for (k = 0; k < (int)SEGGER_COUNTOF(aData); ++k) {
        if (aData[k] != aDataCheck[k]) {
          return 1;
        }
      }
      for (k = 0; k < (int)sizeof(abSpare); ++k) {
        if (abSpare[k] != abSpareCheck[k]) {
          return 1;
        }
      }
    }
  }
  //
  // For each byte in the data area toggle 2 bits at a time and check if the error is detected.
  //
  for (i = 0; i < sizeof(aData); ++i) {
    for (j = 0; j < (8u - 1u); ++j) {
      Mask = (U8)(3u << j);
      //
      // Change the bit values.
      //
      Data          = *(pData + i);
      Data         ^= Mask;
      *(pData + i)  = Data;
      //
      // Correct the bit error.
      //
      r = _Apply(aData, abSpare);
      if (r >= 0) {
        return 1;
      }
      //
      // Correct the bit values.
      //
      Data          = *(pData + i);
      Data         ^= Mask;
      *(pData + i)  = Data;
      //
      // Verify the data and the spare area.
      //
      for (k = 0; k < (int)SEGGER_COUNTOF(aData); ++k) {
        if (aData[k] != aDataCheck[k]) {
          return 1;
        }
      }
      for (k = 0; k < (int)sizeof(abSpare); ++k) {
        if (abSpare[k] != abSpareCheck[k]) {
          return 1;
        }
      }
    }
  }
  //
  // For each byte in the spare area toggle 2 bits at a time and check if the error is detected.
  //
  for (i = 0; i < sizeof(abSpare); ++i) {
    //
    // Do not test unused bytes in the spare area.
    //
    if (i < OFF_SPARE_DATA) {
      continue;
    }
    for (j = 0; j < (8u - 1u); ++j) {
      //
      // Do not change the unused bits in the ECC.
      //
      if (((i == 10u) || (i == 13u)) && ((j == 0u) || (j == 1u))) {
        continue;
      }
      Mask = (U8)(3u << j);
      //
      // Change the bit values.
      //
      Data           = *(pSpare + i);
      Data          ^= Mask;
      *(pSpare + i)  = Data;
      //
      // Correct the bit error.
      //
      r = _Apply(aData, abSpare);
      if (r >= 0) {
        return 1;
      }
      //
      // Correct the bit values.
      //
      Data           = *(pSpare + i);
      Data          ^= Mask;
      *(pSpare + i)  = Data;
      //
      // Verify the data and the spare area.
      //
      for (k = 0; k < (int)SEGGER_COUNTOF(aData); ++k) {
        if (aData[k] != aDataCheck[k]) {
          return 1;
        }
      }
      for (k = 0; k < (int)sizeof(abSpare); ++k) {
        if (abSpare[k] != abSpareCheck[k]) {
          return 1;
        }
      }
    }
  }
  return 0;
}

/*********************************************************************
*
*       Public data
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_NAND_ECC_SW_1BIT
*/
const FS_NAND_ECC_HOOK FS_NAND_ECC_SW_1BIT = {
  _Calc,
  _Apply,
  NUM_BIT_ERRORS,
  LD_NUM_BYTES_DATA,
  NUM_BIT_ERRORS
};

/*************************** End of file ****************************/
