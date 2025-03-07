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
File        : FS_CRC32.c
Purpose     : Compute CRC32 in high speed.
              A CRC table with 256 entries is used.
              The polynomial used is the mirrored version of 0x04c11DB7,
              which is for V.42, MPEG-2, PNG and others.
              The initial value can be freely chosen; 0xFFFFFFFF is
              recommended.
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
#define CRC_POLY    0xEDB88320uL      // Normal form is 0x04C11DB7

/*********************************************************************
*
*       Static const data
*
**********************************************************************
*/
static const U32 _aCRC[256] = {
  0x00000000uL, 0x77073096uL, 0xEE0E612CuL, 0x990951BAuL,
  0x076DC419uL, 0x706AF48FuL, 0xE963A535uL, 0x9E6495A3uL,
  0x0EDB8832uL, 0x79DCB8A4uL, 0xE0D5E91EuL, 0x97D2D988uL,
  0x09B64C2BuL, 0x7EB17CBDuL, 0xE7B82D07uL, 0x90BF1D91uL,
  0x1DB71064uL, 0x6AB020F2uL, 0xF3B97148uL, 0x84BE41DEuL,
  0x1ADAD47DuL, 0x6DDDE4EBuL, 0xF4D4B551uL, 0x83D385C7uL,
  0x136C9856uL, 0x646BA8C0uL, 0xFD62F97AuL, 0x8A65C9ECuL,
  0x14015C4FuL, 0x63066CD9uL, 0xFA0F3D63uL, 0x8D080DF5uL,
  0x3B6E20C8uL, 0x4C69105EuL, 0xD56041E4uL, 0xA2677172uL,
  0x3C03E4D1uL, 0x4B04D447uL, 0xD20D85FDuL, 0xA50AB56BuL,
  0x35B5A8FAuL, 0x42B2986CuL, 0xDBBBC9D6uL, 0xACBCF940uL,
  0x32D86CE3uL, 0x45DF5C75uL, 0xDCD60DCFuL, 0xABD13D59uL,
  0x26D930ACuL, 0x51DE003AuL, 0xC8D75180uL, 0xBFD06116uL,
  0x21B4F4B5uL, 0x56B3C423uL, 0xCFBA9599uL, 0xB8BDA50FuL,
  0x2802B89EuL, 0x5F058808uL, 0xC60CD9B2uL, 0xB10BE924uL,
  0x2F6F7C87uL, 0x58684C11uL, 0xC1611DABuL, 0xB6662D3DuL,
  0x76DC4190uL, 0x01DB7106uL, 0x98D220BCuL, 0xEFD5102AuL,
  0x71B18589uL, 0x06B6B51FuL, 0x9FBFE4A5uL, 0xE8B8D433uL,
  0x7807C9A2uL, 0x0F00F934uL, 0x9609A88EuL, 0xE10E9818uL,
  0x7F6A0DBBuL, 0x086D3D2DuL, 0x91646C97uL, 0xE6635C01uL,
  0x6B6B51F4uL, 0x1C6C6162uL, 0x856530D8uL, 0xF262004EuL,
  0x6C0695EDuL, 0x1B01A57BuL, 0x8208F4C1uL, 0xF50FC457uL,
  0x65B0D9C6uL, 0x12B7E950uL, 0x8BBEB8EAuL, 0xFCB9887CuL,
  0x62DD1DDFuL, 0x15DA2D49uL, 0x8CD37CF3uL, 0xFBD44C65uL,
  0x4DB26158uL, 0x3AB551CEuL, 0xA3BC0074uL, 0xD4BB30E2uL,
  0x4ADFA541uL, 0x3DD895D7uL, 0xA4D1C46DuL, 0xD3D6F4FBuL,
  0x4369E96AuL, 0x346ED9FCuL, 0xAD678846uL, 0xDA60B8D0uL,
  0x44042D73uL, 0x33031DE5uL, 0xAA0A4C5FuL, 0xDD0D7CC9uL,
  0x5005713CuL, 0x270241AAuL, 0xBE0B1010uL, 0xC90C2086uL,
  0x5768B525uL, 0x206F85B3uL, 0xB966D409uL, 0xCE61E49FuL,
  0x5EDEF90EuL, 0x29D9C998uL, 0xB0D09822uL, 0xC7D7A8B4uL,
  0x59B33D17uL, 0x2EB40D81uL, 0xB7BD5C3BuL, 0xC0BA6CADuL,
  0xEDB88320uL, 0x9ABFB3B6uL, 0x03B6E20CuL, 0x74B1D29AuL,
  0xEAD54739uL, 0x9DD277AFuL, 0x04DB2615uL, 0x73DC1683uL,
  0xE3630B12uL, 0x94643B84uL, 0x0D6D6A3EuL, 0x7A6A5AA8uL,
  0xE40ECF0BuL, 0x9309FF9DuL, 0x0A00AE27uL, 0x7D079EB1uL,
  0xF00F9344uL, 0x8708A3D2uL, 0x1E01F268uL, 0x6906C2FEuL,
  0xF762575DuL, 0x806567CBuL, 0x196C3671uL, 0x6E6B06E7uL,
  0xFED41B76uL, 0x89D32BE0uL, 0x10DA7A5AuL, 0x67DD4ACCuL,
  0xF9B9DF6FuL, 0x8EBEEFF9uL, 0x17B7BE43uL, 0x60B08ED5uL,
  0xD6D6A3E8uL, 0xA1D1937EuL, 0x38D8C2C4uL, 0x4FDFF252uL,
  0xD1BB67F1uL, 0xA6BC5767uL, 0x3FB506DDuL, 0x48B2364BuL,
  0xD80D2BDAuL, 0xAF0A1B4CuL, 0x36034AF6uL, 0x41047A60uL,
  0xDF60EFC3uL, 0xA867DF55uL, 0x316E8EEFuL, 0x4669BE79uL,
  0xCB61B38CuL, 0xBC66831AuL, 0x256FD2A0uL, 0x5268E236uL,
  0xCC0C7795uL, 0xBB0B4703uL, 0x220216B9uL, 0x5505262FuL,
  0xC5BA3BBEuL, 0xB2BD0B28uL, 0x2BB45A92uL, 0x5CB36A04uL,
  0xC2D7FFA7uL, 0xB5D0CF31uL, 0x2CD99E8BuL, 0x5BDEAE1DuL,
  0x9B64C2B0uL, 0xEC63F226uL, 0x756AA39CuL, 0x026D930AuL,
  0x9C0906A9uL, 0xEB0E363FuL, 0x72076785uL, 0x05005713uL,
  0x95BF4A82uL, 0xE2B87A14uL, 0x7BB12BAEuL, 0x0CB61B38uL,
  0x92D28E9BuL, 0xE5D5BE0DuL, 0x7CDCEFB7uL, 0x0BDBDF21uL,
  0x86D3D2D4uL, 0xF1D4E242uL, 0x68DDB3F8uL, 0x1FDA836EuL,
  0x81BE16CDuL, 0xF6B9265BuL, 0x6FB077E1uL, 0x18B74777uL,
  0x88085AE6uL, 0xFF0F6A70uL, 0x66063BCAuL, 0x11010B5CuL,
  0x8F659EFFuL, 0xF862AE69uL, 0x616BFFD3uL, 0x166CCF45uL,
  0xA00AE278uL, 0xD70DD2EEuL, 0x4E048354uL, 0x3903B3C2uL,
  0xA7672661uL, 0xD06016F7uL, 0x4969474DuL, 0x3E6E77DBuL,
  0xAED16A4AuL, 0xD9D65ADCuL, 0x40DF0B66uL, 0x37D83BF0uL,
  0xA9BCAE53uL, 0xDEBB9EC5uL, 0x47B2CF7FuL, 0x30B5FFE9uL,
  0xBDBDF21CuL, 0xCABAC28AuL, 0x53B39330uL, 0x24B4A3A6uL,
  0xBAD03605uL, 0xCDD70693uL, 0x54DE5729uL, 0x23D967BFuL,
  0xB3667A2EuL, 0xC4614AB8uL, 0x5D681B02uL, 0x2A6F2B94uL,
  0xB40BBE37uL, 0xC30C8EA1uL, 0x5A05DF1BuL, 0x2D02EF8DuL
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
*       FS_CRC32_BuildTable
*
*  Function description
*    Build CRC table. This code has been used to build the table.
*
*  Notes
*    (1) The same code can also be used to build a CRC table for any other polynomial
*/
void FS_CRC32_BuildTable(void) {
  U32 i;
  U32 n;
  U32 v;
  U32 aCRC[256];

  for (n = 0; n < 256; n++) {
    v = n;
    i = 8;
    do {
      if (v & 1) {
        v = (v >> 1) ^ CRC_POLY;
      } else {
        v >>= 1;
      }
    } while (--i);
    aCRC[n] = v;
  }
  printf("static const U32 _aCRC[256] = {\n");
  for (n = 0; n < 256; n += 4) {
    printf("  0x%08XuL, 0x%08XuL, 0x%08XuL, 0x%08XuL,\n", aCRC[n], aCRC[n+1], aCRC[n+2], aCRC[n+3]);     //lint !e661 !e662 !e705 N:107
  }
  printf("};\n");
}

#endif

/*********************************************************************
*
*       FS_CRC32_Calc
*/
U32 FS_CRC32_Calc(const U8 * pData, unsigned NumBytes, U32 crc) {
  //
  // Calculate CRC in units of 8 bytes
  //
  if (NumBytes >= 8u) {
    do {
      crc ^= *pData++;
      crc  = _aCRC[crc & 0xFFu] ^ (U32)(crc >> 8);
      crc ^= *pData++;
      crc  = _aCRC[crc & 0xFFu] ^ (U32)(crc >> 8);
      crc ^= *pData++;
      crc  = _aCRC[crc & 0xFFu] ^ (U32)(crc >> 8);
      crc ^= *pData++;
      crc  = _aCRC[crc & 0xFFu] ^ (U32)(crc >> 8);
      crc ^= *pData++;
      crc  = _aCRC[crc & 0xFFu] ^ (U32)(crc >> 8);
      crc ^= *pData++;
      crc  = _aCRC[crc & 0xFFu] ^ (U32)(crc >> 8);
      crc ^= *pData++;
      crc  = _aCRC[crc & 0xFFu] ^ (U32)(crc >> 8);
      crc ^= *pData++;
      crc  = _aCRC[crc & 0xFFu] ^ (U32)(crc >> 8);
      NumBytes -= 8u;
    } while (NumBytes >= 8u);
  }
  //
  // Calculate CRC in units of bytes
  //
  if (NumBytes != 0u) {
    do {
      crc ^= *pData++;
      crc  = _aCRC[crc & 0xFFu] ^ (crc >> 8);
    } while (--NumBytes != 0u);
  }
  return crc;
}

/*********************************************************************
*
*       FS_CRC32_CalcBitByBit
*/
FS_OPTIMIZE
U32 FS_CRC32_CalcBitByBit(const U8 * pData, unsigned NumBytes, U32 crc, U32 Poly) {
  if (NumBytes != 0u) {
    int i;

    do {
      crc ^= (U32)*pData++;
      i = 8;
      do {
        if ((crc & 1u) != 0u) {
          crc = (U32)((crc >> 1) ^ Poly);
        } else {
          crc >>= 1;
        }
      } while (--i != 0);
    } while (--NumBytes != 0u);
  }
  return crc;
}

/*********************************************************************
*
*       FS_CRC32_Validate
*
*  Function description
*    Verifies proper operation of the CRC generation code.
*
*  Return value
*    ==0    OK, the implementation is correct.
*    !=0    Error, test failed.
*
*  Additional information
*    It calculates the CRC of an empty 512-byte sector by calling
*    the CRC calculation routine 128 times with an argument set to
*    0xFFFFFFFF.
*/
int FS_CRC32_Validate(void) {
  U8       abData[16];
  U32      crcFast;
  U32      crcSlow;
  unsigned i;

  crcFast   = 0;
  crcSlow   = 0;
  abData[0] = 0xFF;
  abData[1] = 0xFF;
  abData[2] = 0xFF;
  abData[3] = 0xFF;
  for (i = 0; i < 128u; i++) {
    crcFast = FS_CRC32_Calc(abData, 4, crcFast);
    crcSlow = FS_CRC32_CalcBitByBit(abData, 4, crcSlow, CRC_POLY);
    if (crcFast != crcSlow) {
      return 1;                 // Test failed.
    }
  }
  if (crcFast != 0x0FD1B6E7uL) {
    return 1;                   // Test failed.
  }
  crcFast    = 0;
  crcSlow    = 0;
  abData[0]  = 0xFF;
  abData[1]  = 0xFF;
  abData[2]  = 0xFF;
  abData[3]  = 0xFF;
  abData[4]  = 0xFF;
  abData[5]  = 0xFF;
  abData[6]  = 0xFF;
  abData[7]  = 0xFF;
  abData[8]  = 0xFF;
  abData[9]  = 0xFF;
  abData[10] = 0xFF;
  abData[11] = 0xFF;
  abData[12] = 0xFF;
  abData[13] = 0xFF;
  abData[14] = 0xFF;
  abData[15] = 0xFF;
  for (i = 0; i < 128u; i++) {
    crcFast = FS_CRC32_Calc(abData, sizeof(abData), crcFast);
    crcSlow = FS_CRC32_CalcBitByBit(abData, sizeof(abData), crcSlow, CRC_POLY);
    if (crcFast != crcSlow) {
      return 1;                 // Test failed.
    }
  }
  if (crcFast != 0xCEBD6BE1uL) {
    return 1;                   // Test failed.
  }
  return 0;                     // Test passed.
}

/*************************** End of file ****************************/
