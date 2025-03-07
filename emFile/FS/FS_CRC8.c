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
File        : FS_CRC8.c
Purpose     : Compute the 8-bit CRC for polynomial 0x07 (CRC-8-CCITT), MSB first
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
#define CRC_POLY    0x07      // CRC-8-CCITT

/*********************************************************************
*
*       Static const data
*
**********************************************************************
*/

static const U8 _abCRC[256] = {
  0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15,
  0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D,
  0x70, 0x77, 0x7E, 0x79, 0x6C, 0x6B, 0x62, 0x65,
  0x48, 0x4F, 0x46, 0x41, 0x54, 0x53, 0x5A, 0x5D,
  0xE0, 0xE7, 0xEE, 0xE9, 0xFC, 0xFB, 0xF2, 0xF5,
  0xD8, 0xDF, 0xD6, 0xD1, 0xC4, 0xC3, 0xCA, 0xCD,
  0x90, 0x97, 0x9E, 0x99, 0x8C, 0x8B, 0x82, 0x85,
  0xA8, 0xAF, 0xA6, 0xA1, 0xB4, 0xB3, 0xBA, 0xBD,
  0xC7, 0xC0, 0xC9, 0xCE, 0xDB, 0xDC, 0xD5, 0xD2,
  0xFF, 0xF8, 0xF1, 0xF6, 0xE3, 0xE4, 0xED, 0xEA,
  0xB7, 0xB0, 0xB9, 0xBE, 0xAB, 0xAC, 0xA5, 0xA2,
  0x8F, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9D, 0x9A,
  0x27, 0x20, 0x29, 0x2E, 0x3B, 0x3C, 0x35, 0x32,
  0x1F, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0D, 0x0A,
  0x57, 0x50, 0x59, 0x5E, 0x4B, 0x4C, 0x45, 0x42,
  0x6F, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7D, 0x7A,
  0x89, 0x8E, 0x87, 0x80, 0x95, 0x92, 0x9B, 0x9C,
  0xB1, 0xB6, 0xBF, 0xB8, 0xAD, 0xAA, 0xA3, 0xA4,
  0xF9, 0xFE, 0xF7, 0xF0, 0xE5, 0xE2, 0xEB, 0xEC,
  0xC1, 0xC6, 0xCF, 0xC8, 0xDD, 0xDA, 0xD3, 0xD4,
  0x69, 0x6E, 0x67, 0x60, 0x75, 0x72, 0x7B, 0x7C,
  0x51, 0x56, 0x5F, 0x58, 0x4D, 0x4A, 0x43, 0x44,
  0x19, 0x1E, 0x17, 0x10, 0x05, 0x02, 0x0B, 0x0C,
  0x21, 0x26, 0x2F, 0x28, 0x3D, 0x3A, 0x33, 0x34,
  0x4E, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5C, 0x5B,
  0x76, 0x71, 0x78, 0x7F, 0x6A, 0x6D, 0x64, 0x63,
  0x3E, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2C, 0x2B,
  0x06, 0x01, 0x08, 0x0F, 0x1A, 0x1D, 0x14, 0x13,
  0xAE, 0xA9, 0xA0, 0xA7, 0xB2, 0xB5, 0xBC, 0xBB,
  0x96, 0x91, 0x98, 0x9F, 0x8A, 0x8D, 0x84, 0x83,
  0xDE, 0xD9, 0xD0, 0xD7, 0xC2, 0xC5, 0xCC, 0xCB,
  0xE6, 0xE1, 0xE8, 0xEF, 0xFA, 0xFD, 0xF4, 0xF3,
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
*       FS_CRC8_BuildTable
*
*  Function description
*    Build CRC table. This code has been used to build the table.
*
*  Notes
*    (1) The code can also be used to build a CRC table for any other 8-bit polynomial with MSB first
*/
void FS_CRC8_BuildTable(void) {
  U32 i;
  U32 n;
  U32 v;
  U32 h;
  U8  abCRC[256];

  //
  // Build CRC table (8-bit table with 256 entries)
  //
  for (n = 0; n < 256; n++) {
    v = n;
    i = 8;
    do {
      h = v & 0x80;             // Use the MSB of the current CRC value. This is bit 7 for a 8-bit CRC.
      v <<= 1;
      if (h) {
        v ^= CRC_POLY;
      }
    } while (--i);
    abCRC[n] = (U8)v;
  }
  //
  // Output the table contents.
  //
  printf("static const U8 _abCRC[256] = {\n");
  for (n = 0; n < 256; n += 8) {
    printf("  0x%.2X, 0x%.2X, 0x%.2X, 0x%.2X, 0x%.2X, 0x%.2X, 0x%.2X, 0x%.2X,\n",  abCRC[n],     abCRC[n + 1], abCRC[n + 2], abCRC[n + 3],
                                                                                   abCRC[n + 4], abCRC[n + 5], abCRC[n + 6], abCRC[n + 7]);     //lint !e661 !e662 N:107
  }
  printf("};\n");
}

#endif

/*********************************************************************
*
*       FS_CRC8_Calc
*
*  Function description
*    Compute the 8-bit CRC using the generated table.
*/
U8 FS_CRC8_Calc(const U8 * pData, unsigned NumBytes, U8 crc) {
  U8 Index;

  if (NumBytes != 0u) {
    do {
      Index = crc ^ *pData++;
      crc   = _abCRC[Index];
    } while (--NumBytes != 0u);
  }
  return crc;
}

/*********************************************************************
*
*       FS_CRC8_CalcBitByBit
*
*  Function description
*    Compute the 8-bit CRC bit-by-bit (slow)
*
*  Notes
*    (1) This code is MUCH slower than the accelerated code above using
*        a table. Do not use this routine in "production code" unless
*        there is a specific reason, such as the smaller size.
*/
FS_OPTIMIZE
U8 FS_CRC8_CalcBitByBit(const U8 * pData, unsigned NumBytes, U8 crc, U8 Poly) {
  int i;

  if (NumBytes != 0u) {
    do {
      crc ^= *pData++;
      i = 8;
      do {
        if ((crc & 0x80u) != 0u) {
          crc = (U8)((crc << 1) ^ Poly);
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
*       FS_CRC8_Validate
*
*  Function description
*    Verifies proper operation of the CRC generation code.
*
*  Return value
*    ==0    OK, the implementation is correct.
*    !=0    Error, test failed.
*
*  Additional information
*    This function computes the CRC of an empty 512-byte sector by
*    calling the CRC calculation routine 512 times with a single byte
*    of 0xFF. Expected result is the CRC of an empty sector: 0xDE
*/
int FS_CRC8_Validate(void) {
  U8       Data;
  U8       crcFast;
  U8       crcSlow;
  unsigned i;

  crcFast = 0;
  crcSlow = 0;
  Data    = 0xFF;
  for (i = 0; i < 512u; i++) {
    crcFast = FS_CRC8_Calc(&Data, sizeof(Data), crcFast);
    crcSlow = FS_CRC8_CalcBitByBit(&Data, sizeof(Data), crcSlow, CRC_POLY);
    if (crcFast != crcSlow) {
      return 1;                 // Test failed.
    }
  }
  if (crcFast != 0xDEu) {
    return 1;                   // Test failed.
  }
  return 0;                     // Test passed.
}

/*************************** End of file ****************************/
