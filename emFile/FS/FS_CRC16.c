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
File        : FS_CRC16.c
Purpose     : Compute the 16-bit CRC for polynomial 0x1021, MSB first
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#include "FS_Int.h"
#ifdef _WIN32
  #include <stdio.h>
#endif

/*********************************************************************
*
*       Defines, fixed
*
**********************************************************************
*/
#define CRC_POLY    0x1021      // CRC-16-CCITT

/*********************************************************************
*
*       Static const data
*
**********************************************************************
*/

/*********************************************************************
*
*       _aCRC
*/
static const U16 _aCRC[256] = {
  0x0000u,  0x1021u,  0x2042u,  0x3063u,  0x4084u,  0x50A5u,  0x60C6u,  0x70E7u,
  0x8108u,  0x9129u,  0xA14Au,  0xB16Bu,  0xC18Cu,  0xD1ADu,  0xE1CEu,  0xF1EFu,
  0x1231u,  0x0210u,  0x3273u,  0x2252u,  0x52B5u,  0x4294u,  0x72F7u,  0x62D6u,
  0x9339u,  0x8318u,  0xB37Bu,  0xA35Au,  0xD3BDu,  0xC39Cu,  0xF3FFu,  0xE3DEu,
  0x2462u,  0x3443u,  0x0420u,  0x1401u,  0x64E6u,  0x74C7u,  0x44A4u,  0x5485u,
  0xA56Au,  0xB54Bu,  0x8528u,  0x9509u,  0xE5EEu,  0xF5CFu,  0xC5ACu,  0xD58Du,
  0x3653u,  0x2672u,  0x1611u,  0x0630u,  0x76D7u,  0x66F6u,  0x5695u,  0x46B4u,
  0xB75Bu,  0xA77Au,  0x9719u,  0x8738u,  0xF7DFu,  0xE7FEu,  0xD79Du,  0xC7BCu,
  0x48C4u,  0x58E5u,  0x6886u,  0x78A7u,  0x0840u,  0x1861u,  0x2802u,  0x3823u,
  0xC9CCu,  0xD9EDu,  0xE98Eu,  0xF9AFu,  0x8948u,  0x9969u,  0xA90Au,  0xB92Bu,
  0x5AF5u,  0x4AD4u,  0x7AB7u,  0x6A96u,  0x1A71u,  0x0A50u,  0x3A33u,  0x2A12u,
  0xDBFDu,  0xCBDCu,  0xFBBFu,  0xEB9Eu,  0x9B79u,  0x8B58u,  0xBB3Bu,  0xAB1Au,
  0x6CA6u,  0x7C87u,  0x4CE4u,  0x5CC5u,  0x2C22u,  0x3C03u,  0x0C60u,  0x1C41u,
  0xEDAEu,  0xFD8Fu,  0xCDECu,  0xDDCDu,  0xAD2Au,  0xBD0Bu,  0x8D68u,  0x9D49u,
  0x7E97u,  0x6EB6u,  0x5ED5u,  0x4EF4u,  0x3E13u,  0x2E32u,  0x1E51u,  0x0E70u,
  0xFF9Fu,  0xEFBEu,  0xDFDDu,  0xCFFCu,  0xBF1Bu,  0xAF3Au,  0x9F59u,  0x8F78u,
  0x9188u,  0x81A9u,  0xB1CAu,  0xA1EBu,  0xD10Cu,  0xC12Du,  0xF14Eu,  0xE16Fu,
  0x1080u,  0x00A1u,  0x30C2u,  0x20E3u,  0x5004u,  0x4025u,  0x7046u,  0x6067u,
  0x83B9u,  0x9398u,  0xA3FBu,  0xB3DAu,  0xC33Du,  0xD31Cu,  0xE37Fu,  0xF35Eu,
  0x02B1u,  0x1290u,  0x22F3u,  0x32D2u,  0x4235u,  0x5214u,  0x6277u,  0x7256u,
  0xB5EAu,  0xA5CBu,  0x95A8u,  0x8589u,  0xF56Eu,  0xE54Fu,  0xD52Cu,  0xC50Du,
  0x34E2u,  0x24C3u,  0x14A0u,  0x0481u,  0x7466u,  0x6447u,  0x5424u,  0x4405u,
  0xA7DBu,  0xB7FAu,  0x8799u,  0x97B8u,  0xE75Fu,  0xF77Eu,  0xC71Du,  0xD73Cu,
  0x26D3u,  0x36F2u,  0x0691u,  0x16B0u,  0x6657u,  0x7676u,  0x4615u,  0x5634u,
  0xD94Cu,  0xC96Du,  0xF90Eu,  0xE92Fu,  0x99C8u,  0x89E9u,  0xB98Au,  0xA9ABu,
  0x5844u,  0x4865u,  0x7806u,  0x6827u,  0x18C0u,  0x08E1u,  0x3882u,  0x28A3u,
  0xCB7Du,  0xDB5Cu,  0xEB3Fu,  0xFB1Eu,  0x8BF9u,  0x9BD8u,  0xABBBu,  0xBB9Au,
  0x4A75u,  0x5A54u,  0x6A37u,  0x7A16u,  0x0AF1u,  0x1AD0u,  0x2AB3u,  0x3A92u,
  0xFD2Eu,  0xED0Fu,  0xDD6Cu,  0xCD4Du,  0xBDAAu,  0xAD8Bu,  0x9DE8u,  0x8DC9u,
  0x7C26u,  0x6C07u,  0x5C64u,  0x4C45u,  0x3CA2u,  0x2C83u,  0x1CE0u,  0x0CC1u,
  0xEF1Fu,  0xFF3Eu,  0xCF5Du,  0xDF7Cu,  0xAF9Bu,  0xBFBAu,  0x8FD9u,  0x9FF8u,
  0x6E17u,  0x7E36u,  0x4E55u,  0x5E74u,  0x2E93u,  0x3EB2u,  0x0ED1u,  0x1EF0u
};

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

#ifdef _WIN32

/*********************************************************************
*
*       FS_CRC16_BuildTable
*
*  Function description
*    Build CRC table. This code has been used to build the table.
*
*  Notes
*    (1) The code can also be used to build a CRC table for any other 16-bit polynomial with MSB first
*/
void FS_CRC16_BuildTable(void) {
  U32 i;
  U32 n;
  U32 v;
  U32 h;
  U16 aCRC[256];

  //
  // Build CRC table (8-bit table with 256 entries)
  //
  for (n = 0; n < 256; n++) {
    v = n << 8;
    i = 8;
    do {
      h = v & 0x8000uL;           // Use the MSB of the current CRC value. This is bit 15 for a 16-bit CRC, bit 31 for a 32-bit CRC
      v <<= 1;
      if (h) {
        v ^= CRC_POLY;
      }
    } while (--i);
    aCRC[n] = (U16)v;
  }
  //
  // Output the table contents.
  //
  printf("static const U16 _aCRC[256] = {\n");
  for (n = 0; n < 256; n += 8) {
    printf("  0x%.4X, 0x%.4X, 0x%.4X, 0x%.4X, 0x%.4X, 0x%.4X, 0x%.4X, 0x%.4X,\n", aCRC[n],     aCRC[n + 1], aCRC[n + 2], aCRC[n + 3],
                                                                                  aCRC[n + 4], aCRC[n + 5], aCRC[n + 6], aCRC[n + 7]);    //lint !e661 !e662 N:107
  }
  printf("};\n");
}

#endif

/*********************************************************************
*
*       FS_CRC16_Calc
*
*  Function description
*    Compute the 16-bit MSB-first CRC for polynomial 0x1021 using the CRC table.
*
*  Parameters
*    pData      Data to be protected via CRC.
*    NumBytes   Number of bytes to be protected via CRC.
*    crc        Initial CRC value.
*
*  Return value
*    Calculated CRC value.
*
*  Notes
*    (1) NumBytes has to be an even value grater than 0 and pData has to
*        be aligned to a 16-bit boundary.
*/
U16 FS_CRC16_Calc(const U8 * pData, unsigned NumBytes, U16 crc) {
  unsigned Xor;
  unsigned NumLoops;

  FS_DEBUG_ASSERT(FS_MTYPE_FS, (NumBytes & 1u) == 0u);
  FS_DEBUG_ASSERT(FS_MTYPE_FS, (SEGGER_PTR2ADDR(pData) & 1u) == 0u);                    // MISRA deviation D:100e
  NumLoops = NumBytes >> 1;
  do {
    crc  ^= (U16)*pData++ << 8;   // XOR data into upper byte
    Xor   = (unsigned)crc >> 8;   // Use upper byte for table index since it would be shifted out next
    crc <<= 8;
    crc  ^= _aCRC[Xor];

    crc  ^= (U16)*pData++ << 8;   // XOR data into upper byte
    Xor   = (unsigned)crc >> 8;   // Use upper byte for table index since it would be shifted out next
    crc <<= 8;
    crc  ^= _aCRC[Xor];
  } while (--NumLoops != 0u);
  return crc;
}

/*********************************************************************
*
*       FS_CRC16_CalcBitByBit
*
*  Function description
*    Compute the 16-bit MSB-first CRC for polynomial 0x1021 bit-by-bit (slow)
*
*  Notes
*    (1) This code is MUCH slower than the accelerated code above using a table.
*        Do not use this routine in "production code" unless there is a specific reason, such as the smaller size.
*/
FS_OPTIMIZE
U16 FS_CRC16_CalcBitByBit(const U8 * pData, unsigned NumBytes, U16 crc, U16 Poly) {
  if (NumBytes != 0u) {
    int i;
    do {
      crc ^= (U16)*pData++ << 8;
      i = 8;
      do {
        if ((crc & 0x8000u) != 0u) {
          crc = (U16)((crc << 1) ^ Poly);
        } else {
          crc <<= 1;
        }
      } while (--i != 0);
    } while (--NumBytes != 0u);
  }
  return crc;
}

/*********************************************************************
*
*       FS_CRC16_Validate
*
*  Function description
*    Verifies proper operation of the CRC generation code.
*
*  Return value
*     == 0: Implementation is correct. Test passed.
*     != 0: Test failed.
*
*  Additional information
*    This function calculates the CRC of an empty 512-byte sector
*    by calling the routine 256 times with a two byte argument of 0xFFFFF.
*    Expected result is the CRC of an empty sector: 0x7FA1
*/
int FS_CRC16_Validate(void) {
  U8       abData[2];
  U16      crcFast;
  U16      crcSlow;
  unsigned i;

  crcFast   = 0;
  crcSlow   = 0;
  abData[0] = 0xFF;
  abData[1] = 0xFF;
  for (i = 0; i < 256u; i++) {
    crcFast = FS_CRC16_Calc(abData, sizeof(abData), crcFast);
    crcSlow = FS_CRC16_CalcBitByBit(abData, sizeof(abData), crcSlow, CRC_POLY);
    if (crcFast != crcSlow) {
      return 1;                 // Test failed.
    }
  }
  if (crcFast != 0x7FA1u) {
    return 1;                   // Test failed.
  }
  return 0;                     // Test passed.
}

/*************************** End of file ****************************/
