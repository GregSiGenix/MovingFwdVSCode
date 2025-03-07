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
-------------------------- END-OF-HEADER -----------------------------

File    : FS_CHARSET_Unicode.c
Purpose : Support for UTF-8 encoded file names.
Literature:
  [1] UTF-8
    (en.wikipedia.org/wiki/UTF-8)
*/

/*********************************************************************
*
*       #include section
*
**********************************************************************
*/
#include "FS_Int.h"

/*********************************************************************
*
*       Local data types
*
**********************************************************************
*/

#if FS_SUPPORT_EXT_ASCII

/*********************************************************************
*
*       CASE_INFO
*/
typedef struct {
  U16 Lower;
  U16 Upper;
} CASE_INFO;

#endif // FS_SUPPORT_EXT_ASCII

/*********************************************************************
*
*       Static const data
*
**********************************************************************
*/

#if FS_SUPPORT_EXT_ASCII

/*********************************************************************
*
*       _aToUpper
*
*  Description
*    Converts small letters to capital letters.
*/
static const CASE_INFO _aToUpper[] = {
  //
  // Latin-1 Supplement
  //
  {0x00E0u, 0x00C0u}, {0x00E1u, 0x00C1u}, {0x00E2u, 0x00C2u}, {0x00E3u, 0x00C3u}, {0x00E4u, 0x00C4u}, {0x00E5u, 0x00C5u}, {0x00E6u, 0x00C6u}, {0x00E7u, 0x00C7u},
  {0x00E8u, 0x00C8u}, {0x00E9u, 0x00C9u}, {0x00EAu, 0x00CAu}, {0x00EBu, 0x00CBu}, {0x00ECu, 0x00CCu}, {0x00EDu, 0x00CDu}, {0x00EEu, 0x00CEu}, {0x00EFu, 0x00CFu},
  {0x00F0u, 0x00D0u}, {0x00F1u, 0x00D1u}, {0x00F2u, 0x00D2u}, {0x00F3u, 0x00D3u}, {0x00F4u, 0x00D4u}, {0x00F5u, 0x00D5u}, {0x00F6u, 0x00D6u}, {0x00F8u, 0x00D8u},
  {0x00F9u, 0x00D9u}, {0x00FAu, 0x00DAu}, {0x00FBu, 0x00DBu}, {0x00FCu, 0x00DCu}, {0x00FDu, 0x00DDu}, {0x00FEu, 0x00DEu}, {0x00FFu, 0x0178u},
  //
  // Latin-1 Extended A
  //
  {0x0101u, 0x0100u}, {0x0103u, 0x0102u}, {0x0105u, 0x0104u}, {0x0107u, 0x0106u}, {0x0109u, 0x0108u}, {0x010Bu, 0x010Au}, {0x010Du, 0x010Cu}, {0x010Fu, 0x010Eu},
  {0x0111u, 0x0110u}, {0x0113u, 0x0112u}, {0x0115u, 0x0114u}, {0x0117u, 0x0116u}, {0x0119u, 0x0118u}, {0x011Bu, 0x011Au}, {0x011Du, 0x011Cu}, {0x011Fu, 0x011Eu},
  {0x0121u, 0x0120u}, {0x0123u, 0x0122u}, {0x0125u, 0x0124u}, {0x0127u, 0x0126u}, {0x0129u, 0x0128u}, {0x012Bu, 0x012Au}, {0x012Du, 0x012Cu}, {0x012Fu, 0x012Eu},
  {0x0131u, 0x0130u}, {0x0133u, 0x0132u}, {0x0135u, 0x0134u}, {0x0137u, 0x0136u}, {0x013Au, 0x0139u}, {0x013Cu, 0x013Bu}, {0x013Eu, 0x013Du}, {0x0140u, 0x013Fu},
  {0x0142u, 0x0141u}, {0x0144u, 0x0143u}, {0x0146u, 0x0145u}, {0x0148u, 0x0147u}, {0x014Bu, 0x014Au}, {0x014Du, 0x014Cu}, {0x014Fu, 0x014Eu}, {0x0151u, 0x0150u},
  {0x0153u, 0x0152u}, {0x0155u, 0x0154u}, {0x0157u, 0x0156u}, {0x0159u, 0x0158u}, {0x015Bu, 0x015Au}, {0x015Du, 0x015Cu}, {0x015Fu, 0x015Eu}, {0x0161u, 0x0160u},
  {0x0163u, 0x0162u}, {0x0165u, 0x0164u}, {0x0167u, 0x0166u}, {0x0169u, 0x0168u}, {0x016Bu, 0x016Au}, {0x016Du, 0x016Cu}, {0x016Fu, 0x016Eu}, {0x0171u, 0x0170u},
  {0x0173u, 0x0172u}, {0x0175u, 0x0174u}, {0x0177u, 0x0176u}, {0x017Au, 0x0179u}, {0x017Cu, 0x017Bu}, {0x017Eu, 0x017Du}, {0x017Fu, 0x0053u},
  //
  // Latin-1 Extended B
  //
  {0x0180u, 0x0243u}, {0x0183u, 0x0182u}, {0x0185u, 0x0184u}, {0x0188u, 0x0187u}, {0x018Cu, 0x018Bu}, {0x0192u, 0x0191u}, {0x0195u, 0x01F6u}, {0x0199u, 0x0198u},
  {0x019Au, 0x023Du}, {0x019Eu, 0x0220u}, {0x01A1u, 0x01A0u}, {0x01A3u, 0x01A2u}, {0x01A5u, 0x01A4u}, {0x01A8u, 0x01A7u}, {0x01ADu, 0x01ACu}, {0x01B0u, 0x01AFu},
  {0x01B4u, 0x01B3u}, {0x01B6u, 0x01B5u}, {0x01B9u, 0x01B8u}, {0x01BDu, 0x01BCu}, {0x01C5u, 0x01C4u}, {0x01C6u, 0x01C4u}, {0x01C8u, 0x01C7u}, {0x01C9u, 0x01C7u},
  {0x01CBu, 0x01CAu}, {0x01CCu, 0x01CAu}, {0x01CEu, 0x01CDu}, {0x01D0u, 0x01CFu}, {0x01D2u, 0x01D1u}, {0x01D4u, 0x01D3u}, {0x01D6u, 0x01D5u}, {0x01D8u, 0x01D7u},
  {0x01DAu, 0x01D9u}, {0x01DCu, 0x01DBu}, {0x01DDu, 0x018Eu}, {0x01DFu, 0x01DEu}, {0x01E1u, 0x01E0u}, {0x01E3u, 0x01E2u}, {0x01E5u, 0x01E4u}, {0x01E7u, 0x01E6u},
  {0x01E9u, 0x01E8u}, {0x01EBu, 0x01EAu}, {0x01EDu, 0x01ECu}, {0x01EFu, 0x01EEu}, {0x01F2u, 0x01F1u}, {0x01F3u, 0x01F1u}, {0x01F5u, 0x01F4u}, {0x01F9u, 0x01F8u},
  {0x01FBu, 0x01FAu}, {0x01FDu, 0x01FCu}, {0x01FFu, 0x01FEu}, {0x0201u, 0x0200u}, {0x0203u, 0x0202u}, {0x0205u, 0x0204u}, {0x0207u, 0x0206u}, {0x0209u, 0x0208u},
  {0x020Bu, 0x020Au}, {0x020Du, 0x020Cu}, {0x020Fu, 0x020Eu}, {0x0211u, 0x0210u}, {0x0213u, 0x0212u}, {0x0215u, 0x0214u}, {0x0217u, 0x0216u}, {0x0219u, 0x0218u},
  {0x021Bu, 0x021Au}, {0x021Du, 0x021Cu}, {0x021Fu, 0x021Eu}, {0x0223u, 0x0222u}, {0x0225u, 0x0224u}, {0x0227u, 0x0226u}, {0x0229u, 0x0228u}, {0x022Bu, 0x022Au},
  {0x022Du, 0x022Cu}, {0x022Fu, 0x022Eu}, {0x0231u, 0x0230u}, {0x0233u, 0x0232u}, {0x023Cu, 0x023Bu}, {0x023Fu, 0x2C7Eu}, {0x0240u, 0x2C7Fu}, {0x0242u, 0x0241u},
  {0x0247u, 0x0246u}, {0x0249u, 0x0248u}, {0x024Bu, 0x024Au}, {0x024Du, 0x024Cu}, {0x024Fu, 0x024Eu},
  //
  // Greek and Coptic
  //
  {0x0371u, 0x0370u}, {0x0373u, 0x0372u}, {0x0377u, 0x0376u}, {0x03ACu, 0x0386u}, {0x03ADu, 0x0388u}, {0x03AEu, 0x0389u}, {0x03AFu, 0x038Au}, {0x03B1u, 0x0391u},
  {0x03B2u, 0x0392u}, {0x03B3u, 0x0393u}, {0x03B4u, 0x0394u}, {0x03B5u, 0x0395u}, {0x03B6u, 0x0396u}, {0x03B7u, 0x0397u}, {0x03B8u, 0x0398u}, {0x03B9u, 0x0399u},
  {0x03BAu, 0x039Au}, {0x03BBu, 0x039Bu}, {0x03BCu, 0x039Cu}, {0x03BDu, 0x039Du}, {0x03BEu, 0x039Eu}, {0x03BFu, 0x039Fu}, {0x03C0u, 0x03A0u}, {0x03C1u, 0x03A1u},
  {0x03C2u, 0x03A3u}, {0x03C3u, 0x03A3u}, {0x03C4u, 0x03A4u}, {0x03C5u, 0x03A5u}, {0x03C6u, 0x03A6u}, {0x03C7u, 0x03A7u}, {0x03C8u, 0x03A8u}, {0x03C9u, 0x03A9u},
  {0x03CAu, 0x03AAu}, {0x03CBu, 0x03ABu}, {0x03CCu, 0x038Cu}, {0x03CDu, 0x038Eu}, {0x03CEu, 0x038Fu}, {0x03D9u, 0x03D8u}, {0x03DBu, 0x03DAu}, {0x03DDu, 0x03DCu},
  {0x03DFu, 0x03DEu}, {0x03E1u, 0x03E0u}, {0x03E3u, 0x03E2u}, {0x03E5u, 0x03E4u}, {0x03E7u, 0x03E6u}, {0x03E9u, 0x03E8u}, {0x03EBu, 0x03EAu}, {0x03EDu, 0x03ECu},
  {0x03EFu, 0x03EEu}, {0x03F8u, 0x03F7u}, {0x03FBu, 0x03FAu},
  //
  // Cyrillic
  //
  {0x0430u, 0x0410u}, {0x0431u, 0x0411u}, {0x0432u, 0x0412u}, {0x0433u, 0x0413u}, {0x0434u, 0x0414u}, {0x0435u, 0x0415u}, {0x0436u, 0x0416u}, {0x0437u, 0x0417u},
  {0x0438u, 0x0418u}, {0x0439u, 0x0419u}, {0x043Au, 0x041Au}, {0x043Bu, 0x041Bu}, {0x043Cu, 0x041Cu}, {0x043Du, 0x041Du}, {0x043Eu, 0x041Eu}, {0x043Fu, 0x041Fu},
  {0x0440u, 0x0420u}, {0x0441u, 0x0421u}, {0x0442u, 0x0422u}, {0x0443u, 0x0423u}, {0x0444u, 0x0424u}, {0x0445u, 0x0425u}, {0x0446u, 0x0426u}, {0x0447u, 0x0427u},
  {0x0448u, 0x0428u}, {0x0449u, 0x0429u}, {0x044Au, 0x042Au}, {0x044Bu, 0x042Bu}, {0x044Cu, 0x042Cu}, {0x044Du, 0x042Du}, {0x044Eu, 0x042Eu}, {0x044Fu, 0x042Fu},
  {0x0450u, 0x0400u}, {0x0451u, 0x0401u}, {0x0452u, 0x0402u}, {0x0453u, 0x0403u}, {0x0454u, 0x0404u}, {0x0455u, 0x0405u}, {0x0456u, 0x0406u}, {0x0457u, 0x0407u},
  {0x0458u, 0x0408u}, {0x0459u, 0x0409u}, {0x045Au, 0x040Au}, {0x045Bu, 0x040Bu}, {0x045Cu, 0x040Cu}, {0x045Du, 0x040Du}, {0x045Eu, 0x040Eu}, {0x045Fu, 0x040Fu},
  {0x0461u, 0x0460u}, {0x0463u, 0x0462u}, {0x0465u, 0x0464u}, {0x0467u, 0x0466u}, {0x0469u, 0x0468u}, {0x046Bu, 0x046Au}, {0x046Du, 0x046Cu}, {0x046Fu, 0x046Eu},
  {0x0471u, 0x0470u}, {0x0473u, 0x0472u}, {0x0475u, 0x0474u}, {0x0477u, 0x0476u}, {0x0479u, 0x0478u}, {0x047Bu, 0x047Au}, {0x047Du, 0x047Cu}, {0x047Fu, 0x047Eu},
  {0x0481u, 0x0480u}, {0x048Bu, 0x048Au}, {0x048Du, 0x048Cu}, {0x048Fu, 0x048Eu}, {0x0491u, 0x0490u}, {0x0493u, 0x0492u}, {0x0495u, 0x0494u}, {0x0497u, 0x0496u},
  {0x0499u, 0x0498u}, {0x049Bu, 0x049Au}, {0x049Du, 0x049Cu}, {0x049Fu, 0x049Eu}, {0x04A1u, 0x04A0u}, {0x04A3u, 0x04A2u}, {0x04A5u, 0x04A4u}, {0x04A7u, 0x04A6u},
  {0x04A9u, 0x04A8u}, {0x04ABu, 0x04AAu}, {0x04ADu, 0x04ACu}, {0x04AFu, 0x04AEu}, {0x04B1u, 0x04B0u}, {0x04B3u, 0x04B2u}, {0x04B5u, 0x04B4u}, {0x04B7u, 0x04B6u},
  {0x04B9u, 0x04B8u}, {0x04BBu, 0x04BAu}, {0x04BDu, 0x04BCu}, {0x04BFu, 0x04BEu}, {0x04C2u, 0x04C1u}, {0x04C4u, 0x04C3u}, {0x04C6u, 0x04C5u}, {0x04C8u, 0x04C7u},
  {0x04CAu, 0x04C9u}, {0x04CCu, 0x04CBu}, {0x04CEu, 0x04CDu}, {0x04CFu, 0x04C0u}, {0x04D1u, 0x04D0u}, {0x04D3u, 0x04D2u}, {0x04D5u, 0x04D4u}, {0x04D7u, 0x04D6u},
  {0x04D9u, 0x04D8u}, {0x04DBu, 0x04DAu}, {0x04DDu, 0x04DCu}, {0x04DFu, 0x04DEu}, {0x04E1u, 0x04E0u}, {0x04E3u, 0x04E2u}, {0x04E5u, 0x04E4u}, {0x04E7u, 0x04E6u},
  {0x04E9u, 0x04E8u}, {0x04EBu, 0x04EAu}, {0x04EDu, 0x04ECu}, {0x04EFu, 0x04EEu}, {0x04F1u, 0x04F0u}, {0x04F3u, 0x04F2u}, {0x04F5u, 0x04F4u}, {0x04F7u, 0x04F6u},
  {0x04F9u, 0x04F8u}, {0x04FBu, 0x04FAu}, {0x04FDu, 0x04FCu}, {0x04FFu, 0x04FEu},
  //
  // Application extensions
  //
  FS_UNICODE_UPPERCASE_EXT,
  //
  // End of table
  //
  {0x0000, 0x0000}
};

#endif // FS_SUPPORT_EXT_ASCII

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _CalcSizeOfChar
*
*  Function description
*    Calculates the number of bytes required for the encoding
*    of the specified character.
*/
static unsigned _CalcSizeOfChar(FS_WCHAR Char) {
  unsigned NumBytes;

  if ((Char & 0xF800u) != 0u) {           // 3 byte sequence
    NumBytes = 3;
  } else if ((Char & 0xFF80u) != 0u) {    // Double byte sequence
    NumBytes = 2;
  } else {                                // Single byte (ASCII)
    NumBytes = 1;
  }
  return NumBytes;
}

/*********************************************************************
*
*       _GetCharSize
*
*  Function description
*    Calculates the number of bytes in the character sequence.
*
*  Parameters
*    pChar    [IN] First byte of the character sequence.
*
*  Return value
*    > 0    Number of bytes in the character sequence.
*    < 0    Error code indicating the failure reason.
*/
static int _GetCharSize(const U8 * pChar) {
  U8 Byte;

  Byte = *pChar;
  if ((Byte & 0x80u) == 0u) {
    return 1;
  } else if ((Byte & 0xE0u) == 0xC0u) {
    return 2;
  } else if ((Byte & 0xF0u) == 0xE0u) {
    return 3;
  } else {
    return FS_ERRCODE_INVALID_CHAR;
  }
}

/*********************************************************************
*
*       _DecodeChar
*
*  Function description
*    Converts a UTF-8 sequence to a Unicode character.
*/
static FS_WCHAR _DecodeChar(const U8 * pChar, unsigned NumBytes, unsigned * pNumBytes) {
  FS_WCHAR UnicodeChar;
  U8       Byte;
  unsigned NumBytesRead;

  NumBytesRead = 0;
  if (NumBytes == 0u) {
    return FS_WCHAR_INVALID;                    // Error, insufficient number of characters.
  }
  UnicodeChar = FS_WCHAR_INVALID;
  Byte        = *pChar++;
  if (Byte != 0u) {                             // End of string not reached?
    if ((Byte & 0x80u) == 0u) {                 // Single byte (ASCII)?
      UnicodeChar  = (FS_WCHAR)Byte;
      NumBytesRead = 1;
    } else if ((Byte & 0xE0u) == 0xC0u) {       // Double byte sequence?
      if (NumBytes >= 2u) {
        UnicodeChar = (FS_WCHAR)((unsigned)Byte & 0x1Fu) << 6;
        Byte        = *pChar;
        if ((Byte & 0xC0u) == 0x80u) {
          Byte         &= 0x3Fu;
          UnicodeChar  |= Byte;
          NumBytesRead  = 2;
        } else {
          UnicodeChar = FS_WCHAR_INVALID;       // Error, invalid character sequence.
        }
      }
    } else if ((Byte & 0xF0u) == 0xE0u) {       // 3 byte sequence?
      if (NumBytes >= 3u) {
        Byte        &= 0x0Fu;
        UnicodeChar  = (FS_WCHAR)((unsigned)Byte << 12);
        Byte         = *pChar++;
        if ((Byte & 0xC0u) == 0x80u) {
          Byte        &= 0x3Fu;
          UnicodeChar |= ((FS_WCHAR)Byte << 6);
          Byte         = *pChar;
          if ((Byte & 0xC0u) == 0x80u) {
            Byte         &= 0x3Fu;
            UnicodeChar  |= (FS_WCHAR)Byte;
            NumBytesRead  = 3;
          } else {
            UnicodeChar = FS_WCHAR_INVALID;     // Error, invalid character sequence.
          }
        } else {
          UnicodeChar = FS_WCHAR_INVALID;       // Error, invalid character sequence.
        }
      }
    } else {
      //
      // Error, invalid character sequence.
      //
    }
  }
  if (pNumBytes != NULL) {
    *pNumBytes = NumBytesRead;
  }
  return UnicodeChar;
}

/*********************************************************************
*
*       Static code (public via callback)
*
**********************************************************************
*/

/*********************************************************************
*
*       _UTF8_DecodeChar
*
*  Function description
*    Converts a UTF-8 sequence to a Unicode character.
*
*  Parameters
*    pChar        [IN] UTF-8 character sequence. Cannot be NULL.
*    NumBytes     Number of bytes in the character sequence.
*    pNumBytes    [OUT] Number of bytes decoded form the UTF-8 sequence.
*                 Can be set to NULL.
*
*  Return value
*    !=FS_WCHAR_INVALID   OK, value of the decoded Unicode character.
*    ==FS_WCHAR_INVALID   Error, invalid character sequence.
*/
static FS_WCHAR _UTF8_DecodeChar(const U8 * pChar, unsigned NumBytes, unsigned * pNumBytes) {
  FS_WCHAR UnicodeChar;

  UnicodeChar = _DecodeChar(pChar, NumBytes, pNumBytes);
  return UnicodeChar;
}

/*********************************************************************
*
*       _UTF8_EncodeChar
*
*  Function description
*    Converts a Unicode character to a UTF-8 sequence.
*
*  Parameters
*    pChar          [OUT] UTF-8 character sequence. Cannot be NULL.
*    MaxNumBytes    Maximum number of bytes to encode.
*    UnicodeChar    Value of the Unicode character to be encoded.
*
*  Return value
*    >=0    OK, number of bytes encoded.
*    < 0    Error, invalid Unicode character value.
*/
static int _UTF8_EncodeChar(U8 * pChar, unsigned MaxNumBytes, FS_WCHAR UnicodeChar) {
  unsigned NumBytesRead;
  int      r;

  NumBytesRead = _CalcSizeOfChar(UnicodeChar);
  if (NumBytesRead > MaxNumBytes) {
    return FS_ERRCODE_INVALID_PARA;
  }
  r = (int)NumBytesRead;
  switch (NumBytesRead) {
  case 1:
    *pChar = (U8)UnicodeChar;
    break;
  case 2:
    *pChar++ = 0xC0u | (U8)(UnicodeChar >> 6);
    *pChar   = 0x80u | (U8)(UnicodeChar & 0x3Fu);
    break;
  case 3:
    *pChar++ = 0xE0u | (U8)(UnicodeChar >> 12);
    *pChar++ = 0x80u | (U8)((UnicodeChar >> 6) & 0x3Fu);
    *pChar   = 0x80u | (U8)(UnicodeChar & 0x3Fu);
    break;
  default:
    r = FS_ERRCODE_INVALID_PARA;
    break;
  }
  return r;
}

/*********************************************************************
*
*       _UTF8_GetNumChars
*
*  Function description
*    Returns the number of Unicode characters in the encoded string.
*
*  Parameters
*    pChar        [IN] UTF-8 character sequence. Cannot be NULL.
*    NumBytes     Number of bytes in the sequence.
*
*  Return value
*    >=0    OK, number of Unicode characters.
*    < 0    Error, invalid character sequence.
*/
static int _UTF8_GetNumChars(const U8 * pChar, unsigned NumBytes) {
  int      r;
  unsigned NumChars;
  unsigned NumBytesRead;
  FS_WCHAR Char;

  NumChars = 0;
  for (;;) {
    if (NumBytes == 0u) {
      r = (int)NumChars;
      break;
    }
    NumBytesRead = 0;
    Char = _DecodeChar(pChar, NumBytes, &NumBytesRead);
    if (Char == FS_WCHAR_INVALID) {
      r = FS_ERRCODE_INVALID_CHAR;
      break;
    }
    if (NumBytesRead == 0u) {
      r = FS_ERRCODE_INVALID_CHAR;
      break;
    }
    ++NumChars;
    NumBytes -= NumBytesRead;
    pChar    += NumBytesRead;
  }
  return r;
}

/*********************************************************************
*
*       _UTF8_GetCharOff
*
*  Function description
*    Returns the byte offset of the specified Unicode character.
*
*  Parameters
*    pChar        [IN] UTF-8 character sequence. Cannot be NULL.
*    NumBytes     Number of bytes in the sequence.
*    CharPos      Position of the Unicode character (0-based).
*
*  Return value
*    >=0    OK, byte position of the Unicode character.
*    < 0    Error, invalid character sequence.
*/
static int _UTF8_GetCharOff(const U8 * pChar, unsigned NumBytes, unsigned CharPos) {
  int      r;
  unsigned Off;
  unsigned NumBytesRead;

  Off = 0;
  for (;;) {
    if (NumBytes == 0u) {
      r = (int)Off;
      break;
    }
    if (CharPos == 0u) {
      r = (int)Off;
      break;
    }
    r = _GetCharSize(pChar);
    if (r < 0) {
      break;
    }
    NumBytesRead = (unsigned)r;
    if (NumBytesRead > NumBytes) {
      r = FS_ERRCODE_INVALID_CHAR;
      break;
    }
    --CharPos;
    NumBytes -= NumBytesRead;
    pChar    += NumBytesRead;
    Off      += NumBytesRead;
  }
  return r;
}

/*********************************************************************
*
*       _UTF8_GetInfo
*
*  Function description
*    Returns information about the UTF-8 encoding.
*/
static void _UTF8_GetInfo(FS_UNICODE_CONV_INFO * pInfo) {
  pInfo->IsOEMEncoding   = 0;
  pInfo->MaxBytesPerChar = 3;
}

/*********************************************************************
*
*       Public data
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_UNICODE_CONV_UTF8
*/
const FS_UNICODE_CONV FS_UNICODE_CONV_UTF8 = {
  _UTF8_DecodeChar,
  _UTF8_EncodeChar,
  _UTF8_GetNumChars,
  _UTF8_GetCharOff,
  _UTF8_DecodeChar,
  _UTF8_GetInfo
};

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_UNICODE_ToUpper
*
*  Function description
*    Converts a Unicode letter character from lower to upper case.
*
*  Additional information
*    Characters that do not have a capital letter are left unchanged.
*/
FS_WCHAR FS_UNICODE_ToUpper(FS_WCHAR UnicodeChar) {
  if ((UnicodeChar >= (FS_WCHAR)'a') && (UnicodeChar <= (FS_WCHAR)'z')) {
    UnicodeChar &= 0xDFu;
  }
#if FS_SUPPORT_EXT_ASCII
  else {
    if (UnicodeChar > 0x7Fu) {
      unsigned          i;
      const CASE_INFO * pCaseInfo;

      for (i = 0; i < SEGGER_COUNTOF(_aToUpper); i++) {
        pCaseInfo = &_aToUpper[i];
        if (UnicodeChar == pCaseInfo->Lower) {
          UnicodeChar = pCaseInfo->Upper;
          break;
        }
      }
    }
  }
#endif // FS_SUPPORT_EXT_ASCII
  return UnicodeChar;
}

/*********************************************************************
*
*       FS_UNICODE_ToLower
*
*  Function description
*    Converts a Unicode letter character from upper to lower case.
*
*  Additional information
*    Characters that do not have a small letter are left unchanged.
*/
FS_WCHAR FS_UNICODE_ToLower(FS_WCHAR UnicodeChar) {
  if ((UnicodeChar >= (FS_WCHAR)'A') && (UnicodeChar <= (FS_WCHAR)'Z')) {
    UnicodeChar |= 0x20u;
  }
#if FS_SUPPORT_EXT_ASCII
  else {
    if (UnicodeChar > 0x7Fu) {
      unsigned          i;
      const CASE_INFO * pCaseInfo;

      for (i = 0; i < SEGGER_COUNTOF(_aToUpper); i++) {
        pCaseInfo = &_aToUpper[i];
        if (UnicodeChar == pCaseInfo->Upper) {
          UnicodeChar = pCaseInfo->Lower;
          break;
        }
      }
    }
  }
#endif // FS_SUPPORT_EXT_ASCII
  return UnicodeChar;
}

/*********************************************************************
*
*       FS_UNICODE_EncodeCharUTF8
*
*  Function description
*    Converts a Unicode character to a UTF-8 sequence.
*
*  Parameters
*    pChar          [OUT] UTF-8 character sequence. Cannot be NULL.
*    MaxNumBytes    Maximum number of bytes to encode.
*    UnicodeChar    Value of the Unicode character to be encoded.
*
*  Return value
*    >=0    OK, number of bytes encoded.
*    < 0    Error, invalid Unicode character value.
*/
int FS_UNICODE_EncodeCharUTF8(U8 * pChar, unsigned MaxNumBytes, FS_WCHAR UnicodeChar) {
  int r;

  r = _UTF8_EncodeChar(pChar, MaxNumBytes, UnicodeChar);
  return r;
}

/*********************************************************************
*
*       FS_UNICODE_DecodeCharUTF8
*
*  Function description
*    Converts a UTF-8 sequence to a Unicode character.
*
*  Parameters
*    pChar        [IN] UTF-8 character sequence. Cannot be NULL.
*    NumBytes     Number of bytes in the character sequence.
*    pNumBytes    [OUT] Number of bytes decoded form the UTF-8 sequence.
*                 Can be set to NULL.
*
*  Return value
*    !=FS_WCHAR_INVALID   OK, value of the decoded Unicode character.
*    ==FS_WCHAR_INVALID   Error, invalid character sequence.
*/
FS_WCHAR FS_UNICODE_DecodeCharUTF8(const U8 * pChar, unsigned NumBytes, unsigned * pNumBytes) {
  FS_WCHAR UnicodeChar;

  UnicodeChar = _UTF8_DecodeChar(pChar, NumBytes, pNumBytes);
  return UnicodeChar;
}


/*************************** End of file ****************************/
