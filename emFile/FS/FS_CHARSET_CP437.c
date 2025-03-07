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

File    : FS_CHARSET_CP437.c
Purpose : Support for single byte character encoding of the Latin alphabet.
Literature:
  [1] Code page 437
    (en.wikipedia.org/wiki/Code_page_437)

Additional information:
  The tables in this file are generated using the GenerateCP437Tables.py
  script located in the Intern/FS/Tool/CharSet folder.
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
*       Defines, fixed
*
**********************************************************************
*/
#define FIRST_EXT_ASCII_CHAR      0x80u
#define LATIN_CAPITAL_LETTER_A    0x41u
#define LATIN_CAPITAL_LETTER_Z    0x5Au
#define LATIN_SMALL_LETTER_A      0x61u
#define LATIN_SMALL_LETTER_Z      0x7Au

/*********************************************************************
*
*       Local data types
*
**********************************************************************
*/

#if ((FS_SUPPORT_EXT_ASCII != 0) || (FS_SUPPORT_FILE_NAME_ENCODING != 0))

/*********************************************************************
*
*       CP437_CHAR
*/
typedef struct {
  U16 Unicode;
  U8  oem;
} CP437_CHAR;

#endif // FS_SUPPORT_EXT_ASCII != 0 || FS_SUPPORT_FILE_NAME_ENCODING != 0

/*********************************************************************
*
*       Static const data
*
**********************************************************************
*/

#if ((FS_SUPPORT_EXT_ASCII != 0) || (FS_SUPPORT_FILE_NAME_ENCODING != 0))

/*********************************************************************
*
*       _aCP437ToUpper
*
*  Description
*    Converts letters in the range 0x80 to 0xFF from lowercase to upper.
*
*  Additional information
*    Non-letter characters or small letters that do not have a capital variant
*    are left unchanged.
*/
static const U8 _aCP437ToUpper[128] = {
  0x80u, 0x9Au, 0x90u, 0x83u, 0x8Eu, 0x85u, 0x8Fu, 0x80u, 0x88u, 0x89u, 0x8Au, 0x8Bu, 0x8Cu, 0x8Du, 0x8Eu, 0x8Fu,
  0x90u, 0x91u, 0x92u, 0x93u, 0x99u, 0x95u, 0x96u, 0x97u, 0x98u, 0x99u, 0x9Au, 0x9Bu, 0x9Cu, 0x9Du, 0x9Eu, 0x9Fu,
  0xA0u, 0xA1u, 0xA2u, 0xA3u, 0xA5u, 0xA5u, 0xA6u, 0xA7u, 0xA8u, 0xA9u, 0xAAu, 0xABu, 0xACu, 0xADu, 0xAEu, 0xAFu,
  0xB0u, 0xB1u, 0xB2u, 0xB3u, 0xB4u, 0xB5u, 0xB6u, 0xB7u, 0xB8u, 0xB9u, 0xBAu, 0xBBu, 0xBCu, 0xBDu, 0xBEu, 0xBFu,
  0xC0u, 0xC1u, 0xC2u, 0xC3u, 0xC4u, 0xC5u, 0xC6u, 0xC7u, 0xC8u, 0xC9u, 0xCAu, 0xCBu, 0xCCu, 0xCDu, 0xCEu, 0xCFu,
  0xD0u, 0xD1u, 0xD2u, 0xD3u, 0xD4u, 0xD5u, 0xD6u, 0xD7u, 0xD8u, 0xD9u, 0xDAu, 0xDBu, 0xDCu, 0xDDu, 0xDEu, 0xDFu,
  0xE0u, 0xE1u, 0xE2u, 0xE3u, 0xE4u, 0xE4u, 0xE6u, 0xE7u, 0xE8u, 0xE9u, 0xEAu, 0xEBu, 0xECu, 0xE8u, 0xEEu, 0xEFu,
  0xF0u, 0xF1u, 0xF2u, 0xF3u, 0xF4u, 0xF5u, 0xF6u, 0xF7u, 0xF8u, 0xF9u, 0xFAu, 0xFBu, 0xFCu, 0xFDu, 0xFEu, 0xFFu
};

/*********************************************************************
*
*       _aCP437ToLower
*
*  Description
*    Converts letters in the range 0x80 to 0xFF from uppercase to lowercase.
*
*  Additional information
*    Non-letter characters or capital letters that do not have a small variant
*    are left unchanged.
*/
static const U8 _aCP437ToLower[128] = {
  0x87u, 0x81u, 0x82u, 0x83u, 0x84u, 0x85u, 0x86u, 0x87u, 0x88u, 0x89u, 0x8Au, 0x8Bu, 0x8Cu, 0x8Du, 0x84u, 0x86u,
  0x82u, 0x91u, 0x92u, 0x93u, 0x94u, 0x95u, 0x96u, 0x97u, 0x98u, 0x94u, 0x81u, 0x9Bu, 0x9Cu, 0x9Du, 0x9Eu, 0x9Fu,
  0xA0u, 0xA1u, 0xA2u, 0xA3u, 0xA4u, 0xA4u, 0xA6u, 0xA7u, 0xA8u, 0xA9u, 0xAAu, 0xABu, 0xACu, 0xADu, 0xAEu, 0xAFu,
  0xB0u, 0xB1u, 0xB2u, 0xB3u, 0xB4u, 0xB5u, 0xB6u, 0xB7u, 0xB8u, 0xB9u, 0xBAu, 0xBBu, 0xBCu, 0xBDu, 0xBEu, 0xBFu,
  0xC0u, 0xC1u, 0xC2u, 0xC3u, 0xC4u, 0xC5u, 0xC6u, 0xC7u, 0xC8u, 0xC9u, 0xCAu, 0xCBu, 0xCCu, 0xCDu, 0xCEu, 0xCFu,
  0xD0u, 0xD1u, 0xD2u, 0xD3u, 0xD4u, 0xD5u, 0xD6u, 0xD7u, 0xD8u, 0xD9u, 0xDAu, 0xDBu, 0xDCu, 0xDDu, 0xDEu, 0xDFu,
  0xE0u, 0xE1u, 0xE2u, 0xE3u, 0xE5u, 0xE5u, 0xE6u, 0xE7u, 0xEDu, 0xE9u, 0xEAu, 0xEBu, 0xECu, 0xEDu, 0xEEu, 0xEFu,
  0xF0u, 0xF1u, 0xF2u, 0xF3u, 0xF4u, 0xF5u, 0xF6u, 0xF7u, 0xF8u, 0xF9u, 0xFAu, 0xFBu, 0xFCu, 0xFDu, 0xFEu, 0xFFu
};

/*********************************************************************
*
*       _aUnicodeToCP437
*
*  Description
*    Converts characters in the range0x80 to 0xFF from Unicode to OEM.
*
*  Additional information
*    The elements are sorted in the ascending order of their Unicode value.
*/
static const CP437_CHAR _aUnicodeToCP437[] = {
  {0x00A0u, 0xFFu}, {0x00A1u, 0xADu}, {0x00A2u, 0x9Bu}, {0x00A3u, 0x9Cu}, {0x00A5u, 0x9Du}, {0x00AAu, 0xA6u}, {0x00ABu, 0xAEu}, {0x00ACu, 0xAAu},
  {0x00B0u, 0xF8u}, {0x00B1u, 0xF1u}, {0x00B2u, 0xFDu}, {0x00B5u, 0xE6u}, {0x00B7u, 0xFAu}, {0x00BAu, 0xA7u}, {0x00BBu, 0xAFu}, {0x00BCu, 0xACu},
  {0x00BDu, 0xABu}, {0x00BFu, 0xA8u}, {0x00C4u, 0x8Eu}, {0x00C5u, 0x8Fu}, {0x00C6u, 0x92u}, {0x00C7u, 0x80u}, {0x00C9u, 0x90u}, {0x00D1u, 0xA5u},
  {0x00D6u, 0x99u}, {0x00DCu, 0x9Au}, {0x00DFu, 0xE1u}, {0x00E0u, 0x85u}, {0x00E1u, 0xA0u}, {0x00E2u, 0x83u}, {0x00E4u, 0x84u}, {0x00E5u, 0x86u},
  {0x00E6u, 0x91u}, {0x00E7u, 0x87u}, {0x00E8u, 0x8Au}, {0x00E9u, 0x82u}, {0x00EAu, 0x88u}, {0x00EBu, 0x89u}, {0x00ECu, 0x8Du}, {0x00EDu, 0xA1u},
  {0x00EEu, 0x8Cu}, {0x00EFu, 0x8Bu}, {0x00F1u, 0xA4u}, {0x00F2u, 0x95u}, {0x00F3u, 0xA2u}, {0x00F4u, 0x93u}, {0x00F6u, 0x94u}, {0x00F7u, 0xF6u},
  {0x00F9u, 0x97u}, {0x00FAu, 0xA3u}, {0x00FBu, 0x96u}, {0x00FCu, 0x81u}, {0x00FFu, 0x98u}, {0x0192u, 0x9Fu}, {0x0393u, 0xE2u}, {0x0398u, 0xE9u},
  {0x03A3u, 0xE4u}, {0x03A6u, 0xE8u}, {0x03A9u, 0xEAu}, {0x03B1u, 0xE0u}, {0x03B4u, 0xEBu}, {0x03B5u, 0xEEu}, {0x03C0u, 0xE3u}, {0x03C3u, 0xE5u},
  {0x03C4u, 0xE7u}, {0x03C6u, 0xEDu}, {0x207Fu, 0xFCu}, {0x20A7u, 0x9Eu}, {0x2219u, 0xF9u}, {0x221Au, 0xFBu}, {0x221Eu, 0xECu}, {0x2229u, 0xEFu},
  {0x2248u, 0xF7u}, {0x2261u, 0xF0u}, {0x2264u, 0xF3u}, {0x2265u, 0xF2u}, {0x2310u, 0xA9u}, {0x2320u, 0xF4u}, {0x2321u, 0xF5u}, {0x2500u, 0xC4u},
  {0x2502u, 0xB3u}, {0x250Cu, 0xDAu}, {0x2510u, 0xBFu}, {0x2514u, 0xC0u}, {0x2518u, 0xD9u}, {0x251Cu, 0xC3u}, {0x2524u, 0xB4u}, {0x252Cu, 0xC2u},
  {0x2534u, 0xC1u}, {0x253Cu, 0xC5u}, {0x2550u, 0xCDu}, {0x2551u, 0xBAu}, {0x2552u, 0xD5u}, {0x2553u, 0xD6u}, {0x2554u, 0xC9u}, {0x2555u, 0xB8u},
  {0x2556u, 0xB7u}, {0x2557u, 0xBBu}, {0x2558u, 0xD4u}, {0x2559u, 0xD3u}, {0x255Au, 0xC8u}, {0x255Bu, 0xBEu}, {0x255Cu, 0xBDu}, {0x255Du, 0xBCu},
  {0x255Eu, 0xC6u}, {0x255Fu, 0xC7u}, {0x2560u, 0xCCu}, {0x2561u, 0xB5u}, {0x2562u, 0xB6u}, {0x2563u, 0xB9u}, {0x2564u, 0xD1u}, {0x2565u, 0xD2u},
  {0x2566u, 0xCBu}, {0x2567u, 0xCFu}, {0x2568u, 0xD0u}, {0x2569u, 0xCAu}, {0x256Au, 0xD8u}, {0x256Bu, 0xD7u}, {0x256Cu, 0xCEu}, {0x2580u, 0xDFu},
  {0x2584u, 0xDCu}, {0x2588u, 0xDBu}, {0x258Cu, 0xDDu}, {0x2590u, 0xDEu}, {0x2591u, 0xB0u}, {0x2592u, 0xB1u}, {0x2593u, 0xB2u}, {0x25A0u, 0xFEu}
};

#endif // FS_SUPPORT_EXT_ASCII != 0 || FS_SUPPORT_FILE_NAME_ENCODING != 0

#if FS_SUPPORT_EXT_ASCII

/*********************************************************************
*
*       _aCP437ToUnicode
*
*  Description
*    Converts characters in the range 0x80 to 0xFF from OEM to Unicode.
*
*  Additional information
*    OEM characters that are not used are mapped to 0xFFFF which is
*    an invalid Unicode character value. Characters with a value
*    greater than 0x9F translate to the same Unicode character value.
*/
static const U16 _aCP437ToUnicode[128] = {
  0x00C7u, 0x00FCu, 0x00E9u, 0x00E2u, 0x00E4u, 0x00E0u, 0x00E5u, 0x00E7u, 0x00EAu, 0x00EBu, 0x00E8u, 0x00EFu, 0x00EEu, 0x00ECu, 0x00C4u, 0x00C5u,
  0x00C9u, 0x00E6u, 0x00C6u, 0x00F4u, 0x00F6u, 0x00F2u, 0x00FBu, 0x00F9u, 0x00FFu, 0x00D6u, 0x00DCu, 0x00A2u, 0x00A3u, 0x00A5u, 0x20A7u, 0x0192u,
  0x00E1u, 0x00EDu, 0x00F3u, 0x00FAu, 0x00F1u, 0x00D1u, 0x00AAu, 0x00BAu, 0x00BFu, 0x2310u, 0x00ACu, 0x00BDu, 0x00BCu, 0x00A1u, 0x00ABu, 0x00BBu,
  0x2591u, 0x2592u, 0x2593u, 0x2502u, 0x2524u, 0x2561u, 0x2562u, 0x2556u, 0x2555u, 0x2563u, 0x2551u, 0x2557u, 0x255Du, 0x255Cu, 0x255Bu, 0x2510u,
  0x2514u, 0x2534u, 0x252Cu, 0x251Cu, 0x2500u, 0x253Cu, 0x255Eu, 0x255Fu, 0x255Au, 0x2554u, 0x2569u, 0x2566u, 0x2560u, 0x2550u, 0x256Cu, 0x2567u,
  0x2568u, 0x2564u, 0x2565u, 0x2559u, 0x2558u, 0x2552u, 0x2553u, 0x256Bu, 0x256Au, 0x2518u, 0x250Cu, 0x2588u, 0x2584u, 0x258Cu, 0x2590u, 0x2580u,
  0x03B1u, 0x00DFu, 0x0393u, 0x03C0u, 0x03A3u, 0x03C3u, 0x00B5u, 0x03C4u, 0x03A6u, 0x0398u, 0x03A9u, 0x03B4u, 0x221Eu, 0x03C6u, 0x03B5u, 0x2229u,
  0x2261u, 0x00B1u, 0x2265u, 0x2264u, 0x2320u, 0x2321u, 0x00F7u, 0x2248u, 0x00B0u, 0x2219u, 0x00B7u, 0x221Au, 0x207Fu, 0x00B2u, 0x25A0u, 0x00A0u
};

#endif // FS_SUPPORT_EXT_ASCII

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

#if ((FS_SUPPORT_EXT_ASCII != 0) || (FS_SUPPORT_FILE_NAME_ENCODING != 0))

/*********************************************************************
*
*       _UnicodeToCP437
*
*  Function description
*    Converts a Unicode character to an OEM character.
*
*  Parameters
*    UnicodeChar    The value of the Unicode character to be converted.
*
*  Return value
*    > 0    The value of the OEM character (8-bits).
*    < 0    The Unicode character does not map to any OEM character.
*/
static int _UnicodeToCP437(FS_WCHAR UnicodeChar) {
  int                r;
  const CP437_CHAR * pChar;
  unsigned           i;

  r     = FS_ERRCODE_INVALID_PARA;
  pChar = _aUnicodeToCP437;
  // TBD: Use binary instead of linear search (see bsearch()).
  for (i = 0; i < SEGGER_COUNTOF(_aUnicodeToCP437); ++i) {
    if (pChar->Unicode == UnicodeChar) {
      r = (int)pChar->oem;
      break;
    }
    ++pChar;
  }
  return r;
}

#endif // (FS_SUPPORT_EXT_ASCII != 0) || (FS_SUPPORT_FILE_NAME_ENCODING != 0)

/*********************************************************************
*
*       Static code (public via callback)
*
**********************************************************************
*/

/*********************************************************************
*
*       _CP437_DecodeChar
*
*  Function description
*    Converts an OEM character to a Unicode character.
*
*  Parameters
*    pChar        [IN] OEM character sequence. Cannot be NULL.
*    NumBytes     Number of byte in the character sequence.
*    pNumBytes    [OUT] Number of bytes read from the OEM character sequence.
*                 Can be set to NULL.
*
*  Return value
*    !=FS_WCHAR_INVALID   OK, value of the decoded Unicode character.
*    ==FS_WCHAR_INVALID   Error, invalid character sequence.
*/
static FS_WCHAR _CP437_DecodeChar(const U8 * pChar, unsigned NumBytes, unsigned * pNumBytes) {
  FS_WCHAR r;
  U8       Byte;

  r = FS_WCHAR_INVALID;
  if (NumBytes != 0u) {
    Byte = *pChar;
    if (Byte < FIRST_EXT_ASCII_CHAR) {
      r = (FS_WCHAR)Byte;
    }
#if FS_SUPPORT_EXT_ASCII
    else {
      Byte -= FIRST_EXT_ASCII_CHAR;
      r = _aCP437ToUnicode[Byte];
    }
#endif // FS_SUPPORT_EXT_ASCII
  }
  if (pNumBytes != NULL) {
    *pNumBytes = 1;
  }
  return r;
}

/*********************************************************************
*
*       _CP437_EncodeChar
*
*  Function description
*    Converts a Unicode character to an OEM character.
*
*  Parameters
*    pChar        [OUT] Encoded character sequence. Cannot be NULL.
*    MaxNumBytes  Maximum number of bytes in the character sequence.
*    UnicodeChar  Value of the Unicode character to be encoded.
*
*  Return value
*    >=0    OK, number of bytes encoded.
*    < 0    Error, invalid Unicode character value.
*/
static int _CP437_EncodeChar(U8 * pChar, unsigned MaxNumBytes, FS_WCHAR UnicodeChar) {
  int r;

  r = FS_ERRCODE_INVALID_PARA;
  if (MaxNumBytes != 0u) {
    if (UnicodeChar < FIRST_EXT_ASCII_CHAR) {
      *pChar = (U8)UnicodeChar;   // Discard the most significant byte which in this case is always set to 0.
      r      = 1;                 // We have processed one byte so far.
    }
#if FS_SUPPORT_EXT_ASCII
    else {
      r = _UnicodeToCP437(UnicodeChar);
      if (r >= 0) {
        *pChar = (U8)r;
        r      = 1;               // We have processed one byte so far.
      }
    }
#endif // FS_SUPPORT_EXT_ASCII
  }
  return r;
}

/*********************************************************************
*
*       _CP437_DecodeCharASCII
*
*  Function description
*    Converts an OEM character to a Unicode character.
*
*  Parameters
*    pChar        [IN] OEM character sequence. Cannot be NULL.
*    NumBytes     Number of byte in the character sequence.
*    pNumBytes    [OUT] Number of bytes read from the OEM character sequence.
*                 Can be set to NULL.
*
*  Return value
*    !=FS_WCHAR_INVALID   OK, value of the decoded Unicode character.
*    ==FS_WCHAR_INVALID   Error, invalid character sequence.
*/
static FS_WCHAR _CP437_DecodeCharASCII(const U8 * pChar, unsigned NumBytes, unsigned * pNumBytes) {
  FS_WCHAR r;
  U8       Byte;

  r = FS_WCHAR_INVALID;
  if (NumBytes != 0u) {
    Byte = *pChar;
    if (Byte < FIRST_EXT_ASCII_CHAR) {
      r = (FS_WCHAR)Byte;
    }
  }
  if (pNumBytes != NULL) {
    *pNumBytes = 1;
  }
  return r;
}

/*********************************************************************
*
*       _CP437_EncodeCharASCII
*
*  Function description
*    Converts a Unicode character to an OEM character.
*
*  Parameters
*    pChar        [OUT] Encoded character sequence. Cannot be NULL.
*    MaxNumBytes  Maximum number of bytes in the character sequence.
*    UnicodeChar  Value of the Unicode character to be encoded.
*
*  Return value
*    >=0    OK, number of bytes encoded.
*    < 0    Error, invalid Unicode character value.
*
*  Additional information
*    This function can convert only ASCII characters.
*/
static int _CP437_EncodeCharASCII(U8 * pChar, unsigned MaxNumBytes, FS_WCHAR UnicodeChar) {
  int r;

  r = FS_ERRCODE_INVALID_PARA;
  if (MaxNumBytes != 0u) {
    if (UnicodeChar < FIRST_EXT_ASCII_CHAR) {
      *pChar = (U8)UnicodeChar;   // Discard the most significant byte which in this case is always set to 0.
      r      = 1;                 // We have processed one byte so far.
    }
  }
  return r;
}

/*********************************************************************
*
*       _CP437_GetNumChars
*
*  Function description
*    Returns the number of Unicode characters in the encoded string.
*
*  Parameters
*    pChar      [IN] Character sequence. Cannot be NULL.
*    NumBytes   Number of bytes in the sequence.
*
*  Return value
*    >=0    OK, number of Unicode characters.
*    < 0    Error, invalid character sequence.
*/
static int _CP437_GetNumChars(const U8 * pChar, unsigned NumBytes) {
  FS_USE_PARA(pChar);
  return (int)NumBytes;
}

/*********************************************************************
*
*       _CP437_GetCharOff
*
*  Function description
*    Returns the byte offset of the specified Unicode character.
*
*  Parameters
*    pChar        [IN] Character sequence. Cannot be NULL.
*    NumBytes     Number of bytes in the sequence.
*    CharPos      Position of the Unicode character (0-based).
*
*  Return value
*    >=0    OK, byte position of the Unicode character.
*    < 0    Error, invalid character sequence.
*/
static int _CP437_GetCharOff(const U8 * pChar, unsigned NumBytes, unsigned CharPos) {
  (void)pChar;
  (void)NumBytes;
  return (int)CharPos;
}

/*********************************************************************
*
*       _CP437_GetInfo
*
*  Function description
*    Returns information about the CP437 encoding.
*/
static void _CP437_GetInfo(FS_UNICODE_CONV_INFO * pInfo) {
  pInfo->IsOEMEncoding   = 1;
  pInfo->MaxBytesPerChar = 1;
}

/*********************************************************************
*
*       _CP437_ToUpper
*
*  Function description
*    Returns the upper case variant of the Latin character.
*
*  Parameters
*    oemChar      The Latin character that has to be converted
*                 to upper case (OEM value).
*
*  Return value
*    The upper case character or the same value passed as argument
*    if the character does not have an upper case variant.
*/
static FS_WCHAR _CP437_ToUpper(FS_WCHAR oemChar) {
  //
  // Small Latin letters
  //
  if ((oemChar >= LATIN_SMALL_LETTER_A) && (oemChar <= LATIN_SMALL_LETTER_Z)) {
    oemChar -= (FS_WCHAR)(LATIN_SMALL_LETTER_A - LATIN_CAPITAL_LETTER_A);
  }
#if ((FS_SUPPORT_EXT_ASCII != 0) || (FS_SUPPORT_FILE_NAME_ENCODING != 0))
  else {
    unsigned Index;

    //
    // Other small Latin letters
    //
    if (oemChar >= FIRST_EXT_ASCII_CHAR) {
      Index = (unsigned)oemChar - FIRST_EXT_ASCII_CHAR;
      if (Index < SEGGER_COUNTOF(_aCP437ToUpper)) {
        oemChar = _aCP437ToUpper[Index];
      }
    }
  }
#endif // FS_SUPPORT_EXT_ASCII != 0 || FS_SUPPORT_FILE_NAME_ENCODING != 0
  return oemChar;
}

/*********************************************************************
*
*       _CP437_ToLower
*
*  Function description
*    Returns the lower case variant of the Latin character.
*
*  Parameters
*    oemChar      The Latin character that has to be converted
*                 to lower case (OEM value).
*
*  Return value
*    The lower case character or the same value passed as argument
*    if the character does not have an lower case variant.
*/
static FS_WCHAR _CP437_ToLower(FS_WCHAR oemChar) {
  //
  // Capital Latin letters
  //
  if ((oemChar >= LATIN_CAPITAL_LETTER_A) && (oemChar <= LATIN_CAPITAL_LETTER_Z)) {
    oemChar += (FS_WCHAR)(LATIN_SMALL_LETTER_A - LATIN_CAPITAL_LETTER_A);
  }
#if ((FS_SUPPORT_EXT_ASCII != 0) || (FS_SUPPORT_FILE_NAME_ENCODING != 0))
  else {
    unsigned Index;

    //
    // Other capital Latin letters
    //
    if (oemChar >= FIRST_EXT_ASCII_CHAR) {
      Index = (unsigned)oemChar - FIRST_EXT_ASCII_CHAR;
      if (Index < SEGGER_COUNTOF(_aCP437ToLower)) {
        oemChar = _aCP437ToLower[Index];
      }
    }
  }
#endif // FS_SUPPORT_EXT_ASCII != 0 || FS_SUPPORT_FILE_NAME_ENCODING != 0
  return oemChar;
}

/*********************************************************************
*
*       _CP437_IsLower
*
*  Function description
*    Checks if the character is a small letter.
*
*  Parameters
*    oemChar    OEM character to be checked.
*
*  Return value
*    ==0    The character not a small letter.
*    ==1    The character is a small letter.
*/
static int _CP437_IsLower(FS_WCHAR oemChar) {
  if ((oemChar >= LATIN_SMALL_LETTER_A) && (oemChar <= LATIN_SMALL_LETTER_Z)) {
    return 1;
  }
#if ((FS_SUPPORT_EXT_ASCII != 0) || (FS_SUPPORT_FILE_NAME_ENCODING != 0))
  if (oemChar >= FIRST_EXT_ASCII_CHAR) {
    unsigned Index;
    unsigned CapitalLetter;

    Index = (unsigned)oemChar - FIRST_EXT_ASCII_CHAR;
    if (Index < SEGGER_COUNTOF(_aCP437ToUpper)) {
      CapitalLetter = _aCP437ToUpper[Index];
      if (oemChar != CapitalLetter) {
        return 1;
      }
    }
  }
#endif // FS_SUPPORT_EXT_ASCII != 0 || FS_SUPPORT_FILE_NAME_ENCODING != 0
  return 0;
}

/*********************************************************************
*
*       _CP437_IsUpper
*
*  Function description
*    Checks if the character is a capital letter.
*
*  Parameters
*    oemChar    OEM character to be checked.
*
*  Return value
*    ==0    The character not a capital letter.
*    ==1    The character is a capital letter.
*/
static int _CP437_IsUpper(FS_WCHAR oemChar) {
  if ((oemChar >= LATIN_CAPITAL_LETTER_A) && (oemChar <= LATIN_CAPITAL_LETTER_Z)) {
    return 1;
  }
#if ((FS_SUPPORT_EXT_ASCII != 0) || (FS_SUPPORT_FILE_NAME_ENCODING != 0))
  if (oemChar >= FIRST_EXT_ASCII_CHAR) {
    unsigned Index;
    unsigned SmallLetter;

    Index = (unsigned)oemChar - FIRST_EXT_ASCII_CHAR;
    if (Index < SEGGER_COUNTOF(_aCP437ToLower)) {
      SmallLetter = _aCP437ToLower[Index];
      if (oemChar != SmallLetter) {
        return 1;
      }
    }
  }
#endif // FS_SUPPORT_EXT_ASCII != 0 || FS_SUPPORT_FILE_NAME_ENCODING != 0
  return 0;
}

#if FS_SUPPORT_FILE_NAME_ENCODING

/*********************************************************************
*
*       _CP437_ToOEM
*
*  Function description
*    Converts a character from Unicode to OEM.
*
*  Parameters
*    UnicodeChar  Unicode character to be converted.
*
*  Return value
*    !=FS_WCHAR_INVALID   The value of the OEM character.
*    !=FS_WCHAR_INVALID   The Unicode character is not in the OEM set.
*/
static FS_WCHAR _CP437_ToOEM(FS_WCHAR UnicodeChar) {
  FS_WCHAR oemChar;
  int      r;

  r = _UnicodeToCP437(UnicodeChar);
  if (r < 0) {
    oemChar = FS_WCHAR_INVALID;
  } else {
    oemChar = (FS_WCHAR)r;
  }
  return oemChar;
}

#endif // FS_SUPPORT_FILE_NAME_ENCODING

/*********************************************************************
*
*       Public data
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_UNICODE_CONV_CP437
*/
const FS_UNICODE_CONV FS_UNICODE_CONV_CP437 = {
  _CP437_DecodeChar,
  _CP437_EncodeChar,
  _CP437_GetNumChars,
  _CP437_GetCharOff,
  NULL,
  _CP437_GetInfo
};

/*********************************************************************
*
*       FS_UNICODE_CONV_CP437_ASCII
*
*  Additional information
*    This converter is used only for testing purposes.
*/
const FS_UNICODE_CONV FS_UNICODE_CONV_CP437_ASCII = {
  _CP437_DecodeCharASCII,
  _CP437_EncodeCharASCII,
  _CP437_GetNumChars,
  _CP437_GetCharOff,
  NULL,
  _CP437_GetInfo
};

#if FS_SUPPORT_FILE_NAME_ENCODING

/*********************************************************************
*
*       FS_CHARSET_CP437
*
*  Description
*    Functions required for the processing of Latin characters.
*/
const FS_CHARSET_TYPE FS_CHARSET_CP437 = {
  NULL,
  _CP437_ToUpper,
  _CP437_ToLower,
  _CP437_IsUpper,
  _CP437_IsLower,
  _CP437_ToOEM
};

#else

/*********************************************************************
*
*       FS_CHARSET_CP437
*
*  Description
*    Functions required for the processing of Latin characters.
*/
const FS_CHARSET_TYPE FS_CHARSET_CP437 = {
  NULL,
  _CP437_ToUpper,
  _CP437_ToLower,
  _CP437_IsUpper,
  _CP437_IsLower,
  NULL
};

#endif // FS_SUPPORT_FILE_NAME_ENCODING

/*************************** End of file ****************************/
