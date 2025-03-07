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
File        : FS_FAT_LFN.c
Purpose     : Handling of long file names for FAT file system.
Literature  :
  [1] Microsoft Extensible Firmware Initiative FAT32 File System Specification
    (\\FILESERVER\Techinfo\Subject\Filesystem\FAT\FAT_Spec_V103.pdf)

Additional information:
  Storage of a Long-Name Within Long Directory Entries
  ----------------------------------------------------
  A long name can consist of more characters than can fit in a single long directory entry. When this
  occurs the name is stored in more than one long entry. Index any event, the name fields themselves
  within the long entries are disjoint. The following example is provided to illustrate how a long name
  is stored across several long directory entries. Names are also NUL terminated and padded with
  0xFFFF characters in order to detect corruption of long name fields by errant disk utilities. A name
  that fits exactly in a n long directory entries (i.e. is an integer multiple of 13) is not NUL terminated
  and not padded with 0xFFFFs.
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#include "FS_FAT_Int.h"

/*********************************************************************
*
*       Defines, configurable
*
**********************************************************************
*/
#ifndef   FS_FAT_SUPPORT_LFN
  #define FS_FAT_SUPPORT_LFN                  1     // This define is used only to simplify the build of libraries.
#endif

#if FS_FAT_SUPPORT_LFN

/*********************************************************************
*
*       Defines, fixed
*
**********************************************************************
*/

/*********************************************************************
*
*       DECODE_CHAR
*/
#if FS_SUPPORT_FILE_NAME_ENCODING
  #define DECODE_CHAR(pName, NumBytes, pNumBytes)         _DecodeCharMB(pName, NumBytes, pNumBytes)
#else
  #define DECODE_CHAR(pName, NumBytes, pNumBytes)         _DecodeCharSB(pName, NumBytes, pNumBytes)
#endif

/*********************************************************************
*
*       ENCODE_CHAR
*/
#if FS_SUPPORT_FILE_NAME_ENCODING
  #define ENCODE_CHAR(pName, MaxNumBytes, Char)           _EncodeCharMB(pName, MaxNumBytes, Char)
#else
  #define ENCODE_CHAR(pName, MaxNumBytes, Char)           _EncodeCharSB(pName, MaxNumBytes, Char)
#endif

/*********************************************************************
*
*       GET_NUM_CHARS
*/
#if FS_SUPPORT_FILE_NAME_ENCODING
  #define GET_NUM_CHARS(pName, NumBytes)                  _GetNumCharsMB(pName, NumBytes)
#else
  #define GET_NUM_CHARS(pName, NumBytes)                  _GetNumCharsSB(pName, NumBytes)
#endif

/*********************************************************************
*
*       GET_CHAR_OFF
*/
#if FS_SUPPORT_FILE_NAME_ENCODING
  #define GET_CHAR_OFF(pName, NumBytes, CharPos)          _GetCharOffMB(pName, NumBytes, CharPos)
#else
  #define GET_CHAR_OFF(pName, NumBytes, CharPos)          _GetCharOffSB(pName, NumBytes, CharPos)
#endif

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
#if FS_SUPPORT_FILE_NAME_ENCODING
  static const FS_UNICODE_CONV * _pUnicodeConv = FS_FAT_LFN_UNICODE_CONV_DEFAULT;
#endif // FS_SUPPORT_FILE_NAME_ENCODING

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

#if FS_SUPPORT_FILE_NAME_ENCODING

/*********************************************************************
*
*       _DecodeCharMB
*/
static FS_WCHAR _DecodeCharMB(const U8 * pName, unsigned NumBytes, unsigned * pNumBytes) {
  FS_WCHAR r;

  r = _pUnicodeConv->pfDecodeChar(pName, NumBytes, pNumBytes);
  return r;
}

/*********************************************************************
*
*       _EncodeCharMB
*/
static int _EncodeCharMB(U8 * pName, unsigned MaxNumBytes, FS_WCHAR Char) {
  int r;

  r = _pUnicodeConv->pfEncodeChar(pName, MaxNumBytes, Char);
  return r;
}

/*********************************************************************
*
*       _GetNumCharsMB
*/
static int _GetNumCharsMB(const U8 * pName, unsigned NumBytes) {
  int r;

  r = _pUnicodeConv->pfGetNumChars(pName, NumBytes);
  return r;
}

/*********************************************************************
*
*       _GetCharOffMB
*/
static int _GetCharOffMB(const U8 * pName, unsigned NumBytes, unsigned CharPos) {
  int r;

  r = _pUnicodeConv->pfGetCharOff(pName, NumBytes, CharPos);
  return r;
}

#if FS_SUPPORT_MBCS

/*********************************************************************
*
*       _IsOEMEncoding
*/
static U8 _IsOEMEncoding(void) {
  FS_UNICODE_CONV_INFO Info;

  FS_MEMSET(&Info, 0, sizeof(Info));
  _pUnicodeConv->pfGetInfo(&Info);
  return Info.IsOEMEncoding;
}

/*********************************************************************
*
*       _IsMBEncoding
*/
static U8 _IsMBEncoding(void) {
  FS_UNICODE_CONV_INFO Info;

  FS_MEMSET(&Info, 0, sizeof(Info));
  _pUnicodeConv->pfGetInfo(&Info);
  if (Info.MaxBytesPerChar == 1u) {
    return 0;
  }
  return 1;
}

#endif // FS_SUPPORT_MBCS

#else

/*********************************************************************
*
*       _DecodeCharSB
*/
static FS_WCHAR _DecodeCharSB(const U8 * pName, unsigned NumBytes, unsigned * pNumBytes) {
  FS_WCHAR r;

  r = FS_UNICODE_CONV_CP437.pfDecodeChar(pName, NumBytes, pNumBytes);
  return r;
}

/*********************************************************************
*
*       _EncodeCharSB
*/
static int _EncodeCharSB(U8 * pName, unsigned MaxNumBytes, FS_WCHAR Char) {
  int r;

  r = FS_UNICODE_CONV_CP437.pfEncodeChar(pName, MaxNumBytes, Char);
  return r;
}

/*********************************************************************
*
*       _GetNumCharsSB
*/
static int _GetNumCharsSB(const U8 * pName, unsigned NumBytes) {
  int r;

  r = FS_UNICODE_CONV_CP437.pfGetNumChars(pName, NumBytes);
  return r;
}

/*********************************************************************
*
*       _GetCharOffSB
*/
static int _GetCharOffSB(const U8 * pName, unsigned NumBytes, unsigned CharPos) {
  int r;

  r = FS_UNICODE_CONV_CP437.pfGetCharOff(pName, NumBytes, CharPos);
  return r;
}

#endif // FS_SUPPORT_FILE_NAME_ENCODING

/*********************************************************************
*
*       _LoadNamePartial
*
*  Function description
*    Loads a part of a file or directory name from the directory entry.
*/
static int _LoadNamePartial(U8 * pName, unsigned MaxNumBytes, const U8 * pDirEntryData, unsigned NumChars) {
  unsigned NumBytes;
  FS_WCHAR Char;
  int      r;
  unsigned NumBytesEncoded;

  NumBytes = 0;
  if (NumChars != 0u) {
    if (MaxNumBytes > 0u) {       // We need at leas one byte for the 0-terminator.
      while (NumChars-- != 0u) {
        Char = FS_LoadU16LE(pDirEntryData);
        if ((Char == 0u) || (Char == 0xFFFFu)) {
          break;                  // End of file name reached.
        }
        r = ENCODE_CHAR(pName, MaxNumBytes, Char);
        if (r <= 0) {
          return r;               // Error, could not encode the file name.
        }
        NumBytesEncoded  = (unsigned)r;
        pName           += NumBytesEncoded;
        NumBytes        += NumBytesEncoded;
        MaxNumBytes     -= NumBytesEncoded;
        pDirEntryData   += 2;     // A single character occupies two bytes.
      }
    }
  }
  return (int)NumBytes;
}

/*********************************************************************
*
*       _CalcNumLongEntries
*
*  Function description
*    Calculates the number of entries required to store the long file name.
*
*  Return value
*    >=0    OK, number of directory entries for the long file name.
*    < 0    Error, invalid character sequence.
*
*  Additional information
*    Examples showing how the number of entries are calculated:
*      "test.txt"                         -> 1
*      "FileName.txt"                     -> 1
*      "FileName1.txt"                    -> 1
*      "LongFileName.txt"                 -> 2
*      "Very very very LongFileName.txt"  -> 3
*/
static int _CalcNumLongEntries(const char * sFileName, unsigned NumBytes, unsigned * pNumChars) {
  int      r;
  unsigned NumChars;
  unsigned NumLongEntries;

  NumLongEntries = 0;
  r = GET_NUM_CHARS((const U8 *)sFileName, NumBytes);
  if (r >= 0) {
    NumChars = (unsigned)r;
    NumLongEntries = (NumChars + 12u) / 13u;
    if (pNumChars != NULL) {
      *pNumChars = NumChars;
    }
  }
  if (NumLongEntries == 0u) {
    return FS_ERRCODE_INVALID_PARA;           // Error, invalid file name.
  }
  return (int)NumLongEntries;
}

/*********************************************************************
*
*       _LoadNameFromDirEntry
*/
static int _LoadNameFromDirEntry(U8 * pName, unsigned MaxNumBytes, const U8 * pDirEntryData) {
  int      r;
  unsigned NumBytes;

  NumBytes = 0;
  r = _LoadNamePartial(pName, MaxNumBytes, pDirEntryData, 5);
  if (r < 0) {
    return r;
  }
  pName       += (unsigned)r;
  NumBytes    += (unsigned)r;
  MaxNumBytes -= (unsigned)r;
  r = _LoadNamePartial(pName, MaxNumBytes, (pDirEntryData + 13), 6);
  if (r < 0) {
    return r;
  }
  pName       += (unsigned)r;
  NumBytes    += (unsigned)r;
  MaxNumBytes -= (unsigned)r;
  r = _LoadNamePartial(pName, MaxNumBytes, (pDirEntryData + 27), 2);
  if (r < 0) {
    return r;
  }
  NumBytes += (unsigned)r;
  return (int)NumBytes;
}

/*********************************************************************
*
*       _CalcNumCharsPartial
*/
static unsigned _CalcNumCharsPartial(const U8 * pDirEntryData, unsigned MaxNumChars) {
  unsigned NumChars;

  for (NumChars = 0; NumChars < MaxNumChars; NumChars++) {
    if ((*pDirEntryData == 0u) && (*(pDirEntryData + 1) == 0u)) {
      break;
    }
    if (*pDirEntryData == 0xFFu) {
      break;
    }
    pDirEntryData += 2;         // Each character occupies two bytes.
  }
  return NumChars;
}

/*********************************************************************
*
*       _CalcNumCharsInDirEntry
*/
static unsigned _CalcNumCharsInDirEntry(const U8 * pDirEntryData) {
  unsigned NumChars;

  NumChars  = 0;
  NumChars += _CalcNumCharsPartial(pDirEntryData,      5u);
  NumChars += _CalcNumCharsPartial(pDirEntryData + 13, 6u);
  NumChars += _CalcNumCharsPartial(pDirEntryData + 27, 2u);
  return NumChars;
}

/*********************************************************************
*
*       _TrimFileName
*
*  Function description
*    Removes leading space characters and trailing space and period
*    characters from a file name.
*
*  Parameters
*    sFileName      [IN] File name to be processed.
*    NumBytes       Length of the file name in bytes.
*    pNumBytes      [OUT] Number of bytes in the trimmed file name.
*
*  Return value
*    Pointer in sFileName to the beginning of the first non-space character.
*
*  Additional information
*    NumBytes can be 0 in which case sFileName has to be 0-terminated.
*/
static const char * _TrimFileName(const char * sFileName, unsigned NumBytes, unsigned * pNumBytes) {
  const char * s;

  //
  // Determine the length of the string
  //
  if (NumBytes == 0u) {
    NumBytes = (unsigned)FS_STRLEN(sFileName);
  }
  if (NumBytes != 0u) {
    //
    // Remove leading spaces.
    //
    for (;;) {
      if (*sFileName != ' ') {
        break;
      }
      NumBytes--;
      if (NumBytes == 0u) {
        break;
      }
      ++sFileName;
    }
    if (NumBytes != 0u) {
      //
      // Set pointer to the end of the string and
      // check the string in reverse from any space
      // or period characters that need to be removed.
      //
      s = sFileName + NumBytes - 1u;
      for (;;) {
        if ((*s != ' ') && (*s != '.')) {
          break;
        }
        NumBytes--;
        if (NumBytes == 0u) {
          break;
        }
        s--;
      }
    }
  }
  if (pNumBytes != NULL) {
    *pNumBytes = NumBytes;
  }
  return sFileName;
}

/*********************************************************************
*
*       _StoreNamePartial
*
*  Function description
*    Stores a part of a long file name to a directory entry.
*
*  Parameters
*    pDest        [OUT] Stored file name in Unicode format.
*    pName        [IN] Long file name to store.
*    NumBytes     Number of bytes in the name.
*    NumChars     Number of Unicode characters to store.
*    NumCharsRem  Number of characters remaining to be stored.
*
*  Return value
*    Number of bytes written.
*/
static unsigned _StoreNamePartial(U8 * pDest, const U8 * pName, unsigned NumBytes, unsigned NumChars, int NumCharsRem) {
  unsigned NumBytesRead;
  unsigned NumBytesReadTotal;
  FS_WCHAR Char;

  NumBytesReadTotal = 0;
  do {
    if (NumCharsRem > 0) {
      NumBytesRead = 0;
      Char = DECODE_CHAR(pName, NumBytes, &NumBytesRead);
      //
      // The validity of the byte sequence is checked before this function is called
      // therefore we do not check the returned value for errors.
      //
      *pDest        = (U8)Char;
      *(pDest + 1)  = (U8)(Char >> 8);
      pName             += NumBytesRead;
      NumBytesReadTotal += NumBytesRead;
      NumBytes          -= NumBytesRead;
    } else if (NumCharsRem < 0) {             // Does the name has to be padded?
      *pDest       = 0xFF;
      *(pDest + 1) = 0xFF;
    } else {
      *pDest       = 0;                       // Add 0-terminator.
      *(pDest + 1) = 0;
    }
    pDest += 2;
    NumCharsRem--;
  } while (--NumChars != 0u);
  return NumBytesReadTotal;
}

/*********************************************************************
*
*       _IsInvalidLongNameChar
*/
static int _IsInvalidLongNameChar(U16 UnicodeChar) {
  char c;
  int  r;

  if (UnicodeChar > 0xFFu) {
    return 0;
  }
  if (UnicodeChar < 0x20u) {
    return 1;                             // Control characters are not allowed in a long file name.
  }
  c = (char)UnicodeChar;
  switch(c) {
  case '\\':
  case '/':
  case ':':
  case '*':
  case '?':
  case '<':
  case '>':
  case '|':
  case '"':
  case '\x7F':                            // A DEL character is not allowed in a long file name.
    r = 1;
    break;
  default:
    r = 0;
    break;
  }
  return r;
}

/*********************************************************************
*
*       _IsValidFileName
*
*  Function description
*    Verifies if the file name is valid.
*
*  Parameters
*    pName        Name of the file or directory to be checked.
*    NumBytes     Number of bytes in the name.
*
*  Return value
*    ==1    File name is valid.
*    ==0    File name is invalid.
*/
static int _IsValidFileName(const U8 * pName, unsigned NumBytes) {
  unsigned NumBytesRead;
  int      ContainsOnlyPeriods;
  FS_WCHAR Char;
  unsigned NumChars;
  int      r;

  if (NumBytes == 0u) {
    return 0;                 // Error, the file name does not contain any characters.
  }
  r = GET_NUM_CHARS(pName, NumBytes);
  if (r <= 0) {
    return 0;                 // Error, character encoding.
  }
  NumChars = (unsigned)r;
  if (NumChars > FAT_MAX_NUM_CHARS_LFN) {
    return 0;                 // Error, file names longer than 255 characters can not be handled by Windows.
  }
  ContainsOnlyPeriods = 1;    // Assume that the file name contains only period characters.
  do {
    Char = DECODE_CHAR(pName, NumBytes, &NumBytesRead);
    if (Char == FS_WCHAR_INVALID) {
      return 0;
    }
    if (_IsInvalidLongNameChar(Char) != 0) {
      return 0;               // Invalid long file name.
    }
    pName    += NumBytesRead;
    NumBytes -= NumBytesRead;
    if (Char != (FS_WCHAR)'.') {
      ContainsOnlyPeriods = 0;
    }
  } while (NumBytes != 0u);
  if (ContainsOnlyPeriods != 0) {
    return 0;                 // Invalid long file name. A file name is not allowed to contain only period characters.
  }
  return 1;                   // Valid file name
}

/*********************************************************************
*
*       _StoreLongDirEntry
*
*  Function description
*    Stores the entire or a part of a long file name to a directory entry.
*
*  Parameters
*    pDirEntry      [OUT] Created directory entry.
*    pName          [IN] Long file name.
*    NumBytes       Number of bytes in long file name.
*    NumChars       Total number of characters in the file name.
*    NumDirEntries  Total number of directory entries that have to be created.
*    Index          Index of the directory entry to be created.
*    CheckSum       Check sum of the directory entry.
*
*  Return value
*    ==0      OK, directory entry created.
*    !=0      Error code indicating the failure reason.
*/
static int _StoreLongDirEntry(FS_FAT_DENTRY * pDirEntry, const U8 * pName, unsigned NumBytes, unsigned NumChars, unsigned NumDirEntries, unsigned Index, unsigned CheckSum) {
  unsigned CharPos;
  unsigned Off;

  FS_MEMSET(pDirEntry, 0, sizeof(FS_FAT_DENTRY));
  //
  // Ordinal. Or 0x40 for last (first) entry.
  //
  pDirEntry->Data[0] = (U8)Index;
  if (Index == NumDirEntries) {
    pDirEntry->Data[0] |= 0x40u;
  }
  pDirEntry->Data[11] = (U8)FS_FAT_ATTR_LONGNAME;     // Attributes. Must be long file name.
  pDirEntry->Data[13] = (U8)CheckSum;
  //
  // Write file name to output buffer. We know that the name is correctly encoded
  // since it has been checked for validity when NumChars was calculated.
  // Therefore, we do not check the return value of GET_CHAR_OFF() and _StoreNamePartial() here.
  //
  CharPos  = (Index - 1u) * 13u;
  Off      = (unsigned)GET_CHAR_OFF(pName, NumBytes, CharPos);
  Off     += (unsigned)_StoreNamePartial(&pDirEntry->Data[1],  pName + Off, NumBytes - Off, 5, (int)NumChars - (int)CharPos);
  Off     += (unsigned)_StoreNamePartial(&pDirEntry->Data[14], pName + Off, NumBytes - Off, 6, ((int)NumChars - (int)CharPos) - 5);
  (void)_StoreNamePartial(&pDirEntry->Data[28], pName + Off, NumBytes - Off, 2, ((int)NumChars - (int)CharPos) - 11);
  return 0;
}

/*********************************************************************
*
*       _CompareUnicodeChar
*
*  Function description
*    Compares two characters for equality.
*
*  Return value
*    ==0      Equal
*    ==1      Not equal
*/
static int _CompareUnicodeChar(const U8 * p0, const U8 * p1, unsigned NumBytes) {
  U16 c0, c1;

  do {
    c0  = *p0;
    c0 |= ((U16)*(++p0) << 8);
    c1  = *p1;
    c1 |= ((U16)*(++p1) << 8);
    c0 = FS_UNICODE_ToUpper(c0);
    c1 = FS_UNICODE_ToUpper(c1);
    if (c0 != c1) {
      return 1;           // Not equal
    }
    p0++;
    p1++;
  } while (--NumBytes != 0u);
  return 0;               // Equal
}

/*********************************************************************
*
*       _CompareLongDirEntry
*
*  Function description
*    Compares the long file part which is stored in short filename.
*    These are 13 double-byte characters stored in the 32 byte directory entry.
*
*  Return value
*    ==0      Equal
*    ==1      Not equal
*/
static int _CompareLongDirEntry(const FS_FAT_DENTRY * pDirEntry0, const FS_FAT_DENTRY * pDirEntry1) {
  int r;

  r = 1;            // Set to indicate that the entries are not equal.
  //
  // The indices have to be equal.
  //
  if (pDirEntry0->Data[0] == pDirEntry1->Data[0]) {
    //
    // If indices are equal, we check all the UNICODE chars in the long directory entry (if possible upper case).
    //
    if (_CompareUnicodeChar((const U8 *)&pDirEntry0->Data[1], (const U8 *)&pDirEntry1->Data[1], 5) == 0) {
      if (_CompareUnicodeChar((const U8 *)&pDirEntry0->Data[14], (const U8 *)&pDirEntry1->Data[14], 9) == 0) {
        r = 0;      // The entries are equal.
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _CalcCheckSum
*/
static unsigned _CalcCheckSum(const FS_83NAME * pShortName) {
  unsigned Sum;

  Sum = FS_FAT_CalcCheckSum(&pShortName->ac[0]);
  return Sum;
}

/*********************************************************************
*
*       _MarkIndexAsUsed
*/
static void _MarkIndexAsUsed(U8 * pBase, U32 Index) {
  unsigned   Mask;
  unsigned   Data;
  U8       * pData;

  Mask = 1uL << (Index & 7u);
  if (Index >= (U32)FS_FAT_LFN_BIT_ARRAY_SIZE) {
    return;
  }
  pData   = pBase + (Index >> 3);
  Data    = *pData;
  Data   |= Mask;             // Mark block as allocated
  *pData  = (U8)Data;
}

/*********************************************************************
*
*       _IsIndexUsed
*/
static int _IsIndexUsed(U8 * pBase, U32 Index) {
  unsigned   Mask;
  U8       * pData;

  Mask  = 1uL << (Index & 7u);
  pData = pBase + (Index >> 3);
  if ((*pData & Mask) != 0u) {
    return 1;
  }
  return 0;
}

/*********************************************************************
*
*       _FindCharPos
*/
static int _FindCharPos(const U8 * pName, U8 c, int NumBytes) {
  int Pos;

  Pos = 0;
  for (;;) {
    if (*pName++ == c) {
      return Pos;
    }
    Pos++;
    if (Pos >= NumBytes) {
      break;
    }
  }
  return -1;      // Character not found in the file name.
}

/*********************************************************************
*
*       _atoi
*/
static U32 _atoi(const char * sBase, unsigned NumDigits) {
  U32 Number;
  U8  Digit;

  Number = 0;
  do {
    Number *= 10u;
    Digit   = (U8)*sBase++ - (U8)'0';
    Number += Digit;
  } while(--NumDigits != 0u);
  return Number;
}

/*********************************************************************
*
*       _FindFreeIndex
*
*  Function description
*    Finds the index of the free directory entry.
*
*  Return value
*    >=0      Index of the free directory entry
*    < 0      Error, no free directory entry found
*/
static I32 _FindFreeIndex(FS_VOLUME * pVolume, FS_SB * pSB, const FS_83NAME * pEntryName, U32 DirStart, U8 * paBitField, U32 StartIndex) {
  FS_FAT_DENTRY * pDirEntry;
  FS_DIR_POS      DirPos;
  unsigned        i;
  unsigned        NumDigits;
  U32             Index;
  int             TildePos;
  I32             FreeIndex;

  FreeIndex = -1;       // Set to indicate that no free index was found.
  FS_FAT_InitDirEntryScan(&pVolume->FSInfo.FATInfo, &DirPos, DirStart);
  for (;;) {
    pDirEntry = FS_FAT_GetDirEntry(pVolume, pSB, &DirPos);
    if (pDirEntry == NULL) {
      break;                        // Error, read failed.
    }
    if (pDirEntry->Data[0] == 0u) {
      break;                        // Error, no more directory entries found.
    }
    //
    // TBD: Shouldn't we skip invalid entries here?
    //
    TildePos = _FindCharPos(&pDirEntry->Data[0], (U8)'~', 8);
    if (TildePos != -1) {
      NumDigits = (8u - (unsigned)TildePos) - 1u;
      //
      // Check if the name matches.
      //
      if (FS_MEMCMP(&pDirEntry->Data[0], &pEntryName->ac[0], (8u - NumDigits) - 1u) == 0) {
        if (FS_MEMCMP(&pDirEntry->Data[DIR_ENTRY_OFF_EXTENSION], &pEntryName->ac[DIR_ENTRY_OFF_EXTENSION], 3) == 0) {
          if (pDirEntry->Data[TildePos] == (U8)'~') {
            Index = _atoi((const char *)&pDirEntry->Data[TildePos + 1], NumDigits) - StartIndex;
            _MarkIndexAsUsed(paBitField, Index);
          }
        }
      }
    }
    FS_FAT_IncDirPos(&DirPos);
  }
  for (i = 0; i < (unsigned)FS_FAT_LFN_BIT_ARRAY_SIZE; i++) {
    if (_IsIndexUsed(paBitField, i) == 0){
      FreeIndex = (I32)i + (I32)StartIndex;         // OK, free index found.
      break;
    }
  }
  return FreeIndex;
}

/*********************************************************************
*
*       _StoreShortNameIndexed
*
*  Function description
*    Generates a short file name from a long file name.
*
*  Parameters
*    pShortName     [OUT] Generated short file name.
*    pLongName      [IN] Long file name (0-terminated).
*    NumBytes       Number of bytes in the long file name.
*    Index          Value to be added to the generated short name.
*
*  Return value
*    ==0      OK, short file name generated.
*    !=0      An error occurred.
*/
static int _StoreShortNameIndexed(FS_83NAME * pShortName, const U8 * pLongName, unsigned NumBytes, U32 Index) {
  unsigned   NumDigits;
  U32        i;
  unsigned   Byte;
  int        HasExtension;
  const U8 * p;
  U8       * pName;
  unsigned   NumBytesBaseName;

  HasExtension = 0;
  pName = &pShortName->ac[0];
  FS_MEMSET(pName, 0x20, sizeof(pShortName->ac));                   // Initialize the short file name with spaces.
  //
  // Calculate the number of characters for the trailing index.
  //
  NumDigits = 1;
  i = Index;
  while (i >= 10u) {
    NumDigits++;
    i /= 10u;
  }
  NumBytesBaseName = (FAT_MAX_NUM_BYTES_BASE - NumDigits) - 1u;     // -1 for the tilde character.
  //
  // Copy name without extension.
  //
  for (i = 0; i < NumBytesBaseName; ++i) {
    if (NumBytes == 0u) {
      break;
    }
    Byte = *pLongName++;
    if (Byte == 0u) {
      break;                                                          // End of long file name reached.
    }
    if (Byte == (unsigned)'.') {
      break;
    }
    Byte = FS_pCharSetType->pfToUpper((FS_WCHAR)Byte);
    if (FS_FAT_IsValidShortNameChar((U8)Byte) == 0) {
      Byte = (unsigned)'_';                                           // According to FAT specification invalid characters have to be replaced with underscores.
    }
    *pName++ = (U8)Byte;
    --NumBytes;
  }
  //
  // Make sure that the index is aligned by filling
  // missing characters with underscores.
  //
  for (; i < NumBytesBaseName; ++i) {
    *pName++ = (U8)'_';
  }
  //
  // Add index.
  //
  *pName++ = (U8)'~';
  i = NumDigits;
  do {
    Byte = (unsigned)'0' + (Index % 10u);
    *(pName + i - 1) = (U8)Byte;
    Index /= 10u;
  } while (--i != 0u);
  pName += NumDigits;
  //
  // Copy extension if present.
  //
  if (NumBytes != 0u) {
    //
    // Locate the extension. Note that we check for single characters here.
    // This works also for multi-byte encodings because the period character
    // we are searching for is encoded as is in any character set and is not
    // part of any multi-byte character.
    //
    p = pLongName;
    for (i = 0; i < NumBytes; ++i) {
      if (*p++ == (U8)'.') {
        HasExtension = 1;
        pLongName = p;
      }
    }
    if (HasExtension != 0) {
      for (i = 0; i < FAT_MAX_NUM_BYTES_EXT; ++i) {
        if (NumBytes == 0u) {
          break;
        }
        Byte = *pLongName++;
        if (Byte == 0u) {
          break;                                                      // End of long file name reached.
        }
        Byte = FS_pCharSetType->pfToUpper((FS_WCHAR)Byte);
        if (FS_FAT_IsValidShortNameChar((U8)Byte) == 0) {
          Byte = (unsigned)'_';                                       // According to FAT specification invalid characters have to be replaced with underscores.
        }
        *pName++ = (U8)Byte;
        --NumBytes;
      }
    }
  }
  return 0;                                                           // OK, created directory entry for the short name.
}

#if (FS_SUPPORT_MBCS != 0) && (FS_SUPPORT_FILE_NAME_ENCODING != 0)

/*********************************************************************
*
*       _GetOEMChar
*
*  Function description
*    Decodes an OEM character suitable for a short file name.
*/
static FS_WCHAR _GetOEMChar(const U8 * pLongName, unsigned NumBytes, unsigned * pNumBytes) {
  FS_WCHAR Char;

  Char = _pUnicodeConv->pfGetChar(pLongName, NumBytes, pNumBytes);
  if (Char != FS_WCHAR_INVALID) {
    //
    // Convert the character to OEM if necessary.
    //
    if (_IsOEMEncoding() == 0u) {
      Char = FS_pCharSetType->pfToOEM(Char);
      if (Char == FS_WCHAR_INVALID) {
        Char = (FS_WCHAR)'_';                           // According to FAT specification invalid characters have to be replaced with underscores.
      }
    }
    //
    // The letter characters have to be stored in uppercase in a short name
    // therefore we have to convert them here.
    //
    Char = FS_pCharSetType->pfToUpper(Char);
    if (Char < 128u) {                                  // All invalid characters are ASCII.
      if (FS_FAT_IsValidShortNameChar((U8)Char) == 0) {
        Char = (FS_WCHAR)'_';                           // According to FAT specification invalid characters have to be replaced with underscores.
      }
    }
  }
  return Char;
}

/*********************************************************************
*
*       _StoreShortNameIndexedMB
*
*  Function description
*    Generates a short file name from a long file name.
*
*  Parameters
*    pShortName     [OUT] Generated short file name.
*    pLongName      [IN] Long file name (0-terminated).
*    NumBytes       Number of bytes in the long file name.
*    Index          Value to be added to the generated short name.
*
*  Return value
*    ==0      OK, short file name generated.
*    !=0      An error occurred.
*/
static int _StoreShortNameIndexedMB(FS_83NAME * pShortName, const U8 * pLongName, unsigned NumBytes, U32 Index) {
  unsigned   NumDigits;
  U32        i;
  unsigned   Byte;
  int        HasExtension;
  const U8 * p;
  U8       * pName;
  FS_WCHAR   Char;
  unsigned   NumBytesRead;
  unsigned   NumBytesToWrite;
  unsigned   ExtPos;
  unsigned   NumBytesBaseName;

  HasExtension = 0;
  pName = &pShortName->ac[0];
  FS_MEMSET(pName, 0x20, sizeof(pShortName->ac));                   // Initialize the short name with spaces.
  //
  // Calculate the number of characters for the trailing index.
  //
  NumDigits = 1;
  i = Index;
  while (i >= 10u) {
    NumDigits++;
    i /= 10u;
  }
  NumBytesBaseName = (FAT_MAX_NUM_BYTES_BASE - NumDigits) - 1u;     // -1 for the tilde character.
  //
  // Copy name without extension.
  //
  i = 0;
  for (;;) {
    if (NumBytes == 0u) {
      break;
    }
    //
    // Get the next character.
    //
    NumBytesRead = 0;
    Char = _GetOEMChar(pLongName, NumBytes, &NumBytesRead);
    if (Char == FS_WCHAR_INVALID) {
      return FS_ERRCODE_INVALID_CHAR;
    }
    //
    // Quit the loop if the extension separator has been reached.
    //
    if (Char == (FS_WCHAR)'.') {
      break;
    }
    //
    // Calculate how many bytes have to be written to the short name.
    //
    NumBytesToWrite = 1;
    if (Char > 255u) {
      ++NumBytesToWrite;
    }
    //
    // Quit the loop if sufficient characters have been copied.
    //
    if ((i + NumBytesToWrite) > NumBytesBaseName) {
      break;
    }
    //
    // Store the character to short file name.
    // Make sure that we do not mark an entry as deleted.
    // According to FAT specification if the first character
    // in the file name is 0xE5 character (that is the marker
    // that indicates that the entry is invalid)
    // it has to be replaced by 0x05.
    //
    if (NumBytesToWrite > 1u) {
      Byte = (unsigned)Char >> 8;
      if (i == 0u) {
        if (Byte == DIR_ENTRY_INVALID_MARKER) {
          Byte = 0x05;
        }
      }
      *pName++ = (U8)Byte;
      Byte     = Char;
      *pName++ = (U8)Byte;
    } else {
      Byte = Char;
      if (i == 0u) {
        if (Byte == DIR_ENTRY_INVALID_MARKER) {
          Byte = 0x05;
        }
      }
      *pName++ = (U8)Byte;
    }
    //
    // Update loop variables.
    //
    i         += NumBytesToWrite;
    pLongName += NumBytesRead;
    NumBytes  -= NumBytesRead;
    //
    // Quit the loop if sufficient characters have been copied.
    //
    if (i == NumBytesBaseName) {
      break;
    }
  }
  //
  // Make sure that the index is aligned by filling
  // missing characters with underscores.
  //
  for (; i < NumBytesBaseName; ++i) {
    *pName++ = (U8)'_';
  }
  //
  // Add index.
  //
  *pName++ = (U8)'~';
  i = NumDigits;
  do {
    Byte = (unsigned)'0' + (Index % 10u);
    *(pName + i - 1) = (U8)Byte;
    Index /= 10u;
  } while (--i != 0u);
  //
  // Copy extension.
  //
  if (NumBytes != 0u) {
    //
    // Locate the extension. Note that we check for single characters here.
    // This works also for multi-byte encodings because the period character
    // we are searching for is encoded as is in any character set and is not
    // part of any multi-byte character.
    //
    p      = pLongName;
    ExtPos = 0;
    for (i = 0; i < NumBytes; ++i) {
      if (*p++ == (U8)'.') {
        HasExtension = 1;
        pLongName    = p;
        ExtPos       = i;
      }
    }
    //
    // If extension found, copy it to the short name.
    //
    if (HasExtension != 0) {
      NumBytes -= ExtPos + 1u;
      pName     = &pShortName->ac[DIR_ENTRY_OFF_EXTENSION];
      i = 0;
      for (;;) {
        if (NumBytes == 0u) {
          break;
        }
        //
        // Get the next character.
        //
        NumBytesRead = 0;
        Char = _GetOEMChar(pLongName, NumBytes, &NumBytesRead);
        if (Char == FS_WCHAR_INVALID) {
          return FS_ERRCODE_INVALID_CHAR;
        }
        //
        // Calculate how many bytes have to be written to the short name.
        //
        NumBytesToWrite = 1;
        if (Char >= 256u) {
          ++NumBytesToWrite;
        }
        //
        // Quit the loop if sufficient characters have been copied.
        //
        if ((i + NumBytesToWrite) > FAT_MAX_NUM_BYTES_EXT) {
          break;
        }
        if (NumBytesToWrite > 1u) {
          *pName++ = (U8)(Char >> 8);
        }
        *pName++   = (U8)Char;
        i         += NumBytesToWrite;
        pLongName += NumBytesRead;
        NumBytes  -= NumBytesRead;
        //
        // Quit the loop if sufficient characters have been copied.
        //
        if (i == FAT_MAX_NUM_BYTES_EXT) {
          break;
        }
      }
    }
  }
  return 0;                               // OK, created directory entry for the short name.
}

#endif // (FS_SUPPORT_MBCS != 0) && (FS_SUPPORT_FILE_NAME_ENCODING != 0)

/*********************************************************************
*
*       _MakeShortNameIndexed
*
*  Function description
*    Generates a short directory entry from a long file name that
*    contains a trailing index.
*
*  Parameters
*    pShortName     [OUT] Created short name entry.
*    pLongName      [IN] Long file name.
*    NumBytes       Number of bytes in the long name.
*    Index          Value to be appended at the end of the short name.
*
*  Return value
*    ==0      OK, short directory entry created.
*    !=0      Error code indicating the failure reason.
*/
static int _MakeShortNameIndexed(FS_83NAME * pShortName, const U8 * pLongName, unsigned NumBytes, U32 Index) {
  int      r;
  unsigned Byte;

  //
  // Perform sanity checks.
  //
  if (NumBytes == 0u) {
    return FS_ERRCODE_INVALID_PARA;
  }
  if (Index >= 1000000u) {                // The trailing index can have a maximum of 6 characters.
    return FS_ERRCODE_INVALID_PARA;
  }
  //
  // Remove leading period characters.
  //
  for (;;) {
    Byte = *pLongName;
    if ((Byte == 0u) || (Byte != (unsigned)'.')) {
      break;
    }
    ++pLongName;
    --NumBytes;
    if (NumBytes == 0u) {
      return FS_ERRCODE_INVALID_PARA;
    }
  }
  //
  // Check if the long name can be stored as 8.3 and if so do not use any index.
  //
  r = FS_FAT_MakeShortName(pShortName, (const char *)pLongName, (int)NumBytes, 0);
  if (r != 0) {
#if (FS_SUPPORT_MBCS != 0) && (FS_SUPPORT_FILE_NAME_ENCODING != 0)
    if (_IsMBEncoding() != 0u) {
      r = _StoreShortNameIndexedMB(pShortName, pLongName, NumBytes, Index);
    } else
#endif
    {
      r = _StoreShortNameIndexed(pShortName, pLongName, NumBytes, Index);
    }
  }
  return r;
}

/*********************************************************************
*
*       _SetDirPosIndex
*
*  Description:
*    Increments/Decrements the position of DirPos
*/
static void _SetDirPosIndex(FS_DIR_POS * pDirPos, int Pos) {
  pDirPos->DirEntryIndex += (U32)Pos;
}

#if FS_FAT_LFN_LOWER_CASE_SHORT_NAMES

/*********************************************************************
*
*       _LoadShortNameSB
*
*  Function description
*    Copies the name of file/directory from the directory entry
*    to the specified buffer.
*
*  Additional information
*    This function works only for single-byte character sets.
*/
static void _LoadShortNameSB(char * sName, unsigned MaxNumBytes, const U8 * pDirEntry) {
  unsigned i;
  unsigned NumBytesToCopy;
  int      IsLowerCaseBase;
  int      IsLowerCaseExt;
  unsigned Flags;
  unsigned Byte;

  if ((sName != NULL) && (MaxNumBytes != 0u)) {
    Flags           = (U8)pDirEntry[DIR_ENTRY_OFF_FLAGS];
    IsLowerCaseBase = 0;
    if ((Flags & FS_FAT_FLAG_LOWER_CASE_BASE) != 0u) {
      IsLowerCaseBase = 1;
    }
    IsLowerCaseExt = 0;
    if ((Flags & FS_FAT_FLAG_LOWER_CASE_EXT) != 0u) {
      IsLowerCaseExt = 1;
    }
    NumBytesToCopy = MaxNumBytes - 1u;          // Reserve space for the 0-terminator.
    if (NumBytesToCopy != 0u) {
      for (i = 0; i < FAT_MAX_NUM_BYTES_SFN; i++) {
        //
        // Start of extension. If it is not a space character, then append the period character.
        //
        if ((i == FAT_MAX_NUM_BYTES_BASE) && (*pDirEntry != (U8)' ')) {
          *sName++ = '.';
          --NumBytesToCopy;
          if (NumBytesToCopy == 0u) {
            break;
          }
        }
        //
        // If the first character of the directory entry is 0x05,
        // it is changed to 0xe5. See the FAT spec V1.03: FAT directories.
        //
        if ((i == 0u) && (*pDirEntry == 0x05u)) {
          pDirEntry++;
          *sName++ = (char)0xE5;
          --NumBytesToCopy;
        } else if (*pDirEntry == (U8)' ') {     // Copy everything except spaces
          pDirEntry++;
        } else {
          Byte = *pDirEntry++;
          if (i < FAT_MAX_NUM_BYTES_BASE) {     // Base name?
            if (IsLowerCaseBase != 0) {
              Byte = FS_pCharSetType->pfToLower((FS_WCHAR)Byte);
            }
          } else {
            if (IsLowerCaseExt != 0) {
              Byte = FS_pCharSetType->pfToLower((FS_WCHAR)Byte);
            }
          }
          *sName++ = (char)Byte;
          --NumBytesToCopy;
        }
        if (NumBytesToCopy == 0u) {
          break;
        }
      }
    }
    *sName = '\0';
  }
}

#if (FS_SUPPORT_MBCS != 0) && (FS_SUPPORT_FILE_NAME_ENCODING != 0)

/*********************************************************************
*
*       _LoadShortNamePartialMB
*
*  Function description
*    Copies a part of the short name from storage to the specified buffer.
*
*  Parameters
*    sName          [OUT] Encoded file name (not 0-terminated)
*    MaxNumBytes    Maximum number of bytes to store to sName.
*    pShortName     [IN] Short name to be copied.
*    NumBytes       Number of bytes from pShortName to copy.
*    IsLowerCase    Set to 1 if all the letters in the name are lower case.
*
*  Return value
*    >= 0     OK, number of bytes written to sName.
*    <  0     Error code indicating the failure reason.
*
*  Additional information
*    This function works only with multi-byte character sets.
*    sName has to be 0-terminated by the calling function.
*/
static int _LoadShortNamePartialMB(char * sName, unsigned MaxNumBytes, const U8 * pShortName, unsigned NumBytes, int IsLowerCase) {
  int      r;
  unsigned NumBytesRead;
  unsigned NumBytesWritten;
  FS_WCHAR Char;
  unsigned Byte;

  NumBytesWritten = 0;
  for (;;) {
    if (MaxNumBytes == 0u) {
      r = (int)NumBytesWritten;
      break;
    }
    if (NumBytes == 0u) {
      r = (int)NumBytesWritten;
      break;
    }
    //
    // Read one character and convert it to lower case if necessary.
    //
    NumBytesRead = 0;
    Char = _pUnicodeConv->pfGetChar(pShortName, NumBytes, &NumBytesRead);         // Get the OEM char
    if (IsLowerCase != 0) {
      Char = FS_pCharSetType->pfToLower(Char);
    }
    if (Char == FS_WCHAR_INVALID) {
      r = FS_ERRCODE_INVALID_CHAR;
      break;
    }
    if (NumBytesRead == 0u) {
      r = FS_ERRCODE_INVALID_CHAR;
      break;
    }
    if (NumBytesRead > NumBytes) {
      r = FS_ERRCODE_INVALID_CHAR;
      break;
    }
    NumBytes   -= NumBytesRead;
    pShortName += NumBytesRead;
    //
    // Store the character to the output buffer. Do not copy space characters because they are used only for padding.
    //
    if (Char != (FS_WCHAR)' ') {
      if (MaxNumBytes < NumBytesRead) {
        r = FS_ERRCODE_INVALID_CHAR;
        break;
      }
      if (NumBytesRead > 1u) {              // We support only double-byte character sets.
        Byte = (unsigned)Char >> 8;
        *sName++ = (char)Byte;
      }
      *sName++ = (char)Char;
      MaxNumBytes     -= NumBytesRead;
      NumBytesWritten += NumBytesRead;
    }
  }
  return r;
}

/*********************************************************************
*
*       _LoadShortNameMB
*
*  Function description
*    Copies the name of file/directory from the directory entry to
*    the specified buffer.
*
*  Additional information
*    This function works only for multi-byte character sets.
*/
static int _LoadShortNameMB(char * sName, unsigned MaxNumBytes, const U8 * pDirEntry) {
  unsigned   NumBytesWritten;
  int        IsLowerCaseBase;
  int        IsLowerCaseExt;
  unsigned   Flags;
  U8         abShortName[FAT_MAX_NUM_BYTES_BASE];
  const U8 * pShortName;
  int        r;
  int        Result;

  r = 0;
  if ((sName != NULL) && (MaxNumBytes != 0u)) {
    Flags           = (U8)pDirEntry[DIR_ENTRY_OFF_FLAGS];
    IsLowerCaseBase = 0;
    if ((Flags & FS_FAT_FLAG_LOWER_CASE_BASE) != 0u) {
      IsLowerCaseBase = 1;
    }
    IsLowerCaseExt = 0;
    if ((Flags & FS_FAT_FLAG_LOWER_CASE_EXT) != 0u) {
      IsLowerCaseExt = 1;
    }
    --MaxNumBytes;        // Reserve one character for the 0-terminator.
    if (MaxNumBytes != 0u) {
      //
      // If the first character of the directory entry is 0x05,
      // it is changed to 0xe5. See the FAT spec V1.03: FAT directories.
      //
      pShortName = pDirEntry;
      if (*pShortName == 0x05u) {
        abShortName[0] = 0xE5u;
        FS_MEMCPY(&abShortName[1], pShortName + 1, sizeof(abShortName) - 1u);
        pShortName = abShortName;
      }
      //
      // Copy the base name first.
      //
      Result = _LoadShortNamePartialMB(sName, MaxNumBytes, pShortName, FAT_MAX_NUM_BYTES_BASE, IsLowerCaseBase);
      if (Result < 0) {
        r = Result;
      } else {
        NumBytesWritten  = (unsigned)Result;
        MaxNumBytes     -= NumBytesWritten;
        sName           += NumBytesWritten;
        if (MaxNumBytes != 0u) {
          //
          // If the extension is present add the separator and then copy the extension.
          //
          pShortName = pDirEntry;
          if (pShortName[FAT_MAX_NUM_BYTES_BASE] != (U8)' ') {
            *sName++ = '.';
            --MaxNumBytes;
            if (MaxNumBytes != 0u) {
              Result = _LoadShortNamePartialMB(sName, MaxNumBytes, &pShortName[FAT_MAX_NUM_BYTES_BASE], FAT_MAX_NUM_BYTES_EXT, IsLowerCaseExt);
              if (Result < 0) {
                r = Result;
              } else {
                NumBytesWritten  = (unsigned)Result;
                sName           += NumBytesWritten;
              }
            }
          }
        }
      }
    }
    *sName = '\0';
  }
  return r;
}

#endif // (FS_SUPPORT_MBCS != 0) && (FS_SUPPORT_FILE_NAME_ENCODING != 0)

#endif // FS_FAT_LFN_LOWER_CASE_SHORT_NAMES

/*********************************************************************
*
*       _LoadShortName
*
*  Function description
*    Copies the name of file/directory from the directory entry to
*    the specified buffer.
*
*  Additional information
*    It is equivalent to FS_FAT_LoadShortName() except it converts the base or
*    the extension or both to lower case if the corresponding flags are set in
*    the reserved byte of the directory entry. This feature is not documented
*    but is used by all NT based Windows versions.
*/
static int _LoadShortName(char * sName, unsigned MaxNumBytes, const U8 * pDirEntry) {
  int r;

  r = 0;
#if FS_FAT_LFN_LOWER_CASE_SHORT_NAMES
#if (FS_SUPPORT_MBCS != 0) && (FS_SUPPORT_FILE_NAME_ENCODING != 0)
  if (_IsMBEncoding() != 0u) {
    r = _LoadShortNameMB(sName, MaxNumBytes, pDirEntry);
  } else
#endif // (FS_SUPPORT_MBCS != 0) && (FS_SUPPORT_FILE_NAME_ENCODING)
  {
    _LoadShortNameSB(sName, MaxNumBytes, pDirEntry);
  }
#else
  FS_FAT_LoadShortName(sName, MaxNumBytes, pDirEntry);
#endif // FS_FAT_LFN_LOWER_CASE_SHORT_NAMES
  return r;
}

/*********************************************************************
*
*       _IsUpper
*
*  Function description
*    Checks if the character is a capital letter.
*
*  Parameters
*    Char   OEM character to be checked (not Unicode!).
*
*  Return value
*    ==0    The character not a capital letter.
*    ==1    The character is a capital letter.
*/
static int _IsUpper(FS_WCHAR Char) {
  int r;

  r = 0;
  if (FS_pCharSetType->pfIsUpper != NULL) {
    r = FS_pCharSetType->pfIsUpper(Char);
  }
  return r;
}

/*********************************************************************
*
*       _IsLower
*
*  Function description
*    Checks if the character is a small letter.
*
*  Parameters
*    Char   OEM character to be checked (not Unicode!).
*
*  Return value
*    ==0    The character not a small letter.
*    ==1    The character is a small letter.
*/
static int _IsLower(FS_WCHAR Char) {
  int r;

  r = 0;
  if (FS_pCharSetType->pfIsLower != NULL) {
    r = FS_pCharSetType->pfIsLower(Char);
  }
  return r;
}

/*********************************************************************
*
*       _StoreShortName
*
*  Function description
*    Convert a given name to the format which is used in the FAT directory.
*/
static int _StoreShortName(FS_83NAME * pShortName, const U8 * pLongName, unsigned NumBytes, unsigned * pFlags) {
  unsigned i;
  int      ExtPos;
  unsigned Flags;
  int      IsLowerCaseBase;
  int      IsUpperCaseBase;
  int      IsLowerCaseExt;
  int      IsUpperCaseExt;
  U8       Byte;

  ExtPos          = -1;               // Set to an invalid value.
  i               = 0;
  IsUpperCaseBase = 0;
  IsLowerCaseBase = 0;
  IsUpperCaseExt  = 0;
  IsLowerCaseExt  = 0;
  Flags           = 0;
  for (;;) {
    if (i == 13u) {
      return 1;                       // Error, file name too long.
    }
    Byte = *(pLongName + i);
    if (FS_FAT_IsValidShortNameChar(Byte) == 0) {
      return 1;                       // Invalid character used in string.
    }
    if (Byte == (U8)'.') {
      if (ExtPos >= 0) {
        return 1;                     // Only 1 period character is allowed in an 8.3 file name.
      }
      ExtPos = (int)i;
    } else {
      if (ExtPos >= 0) {
        if (_IsUpper((FS_WCHAR)Byte) != 0) {
          IsUpperCaseExt = 1;
        }
        if (_IsLower((FS_WCHAR)Byte) != 0) {
          IsLowerCaseExt = 1;
        }
      } else {
        if (_IsUpper((FS_WCHAR)Byte) != 0) {
          IsUpperCaseBase = 1;
        }
        if (_IsLower((FS_WCHAR)Byte) != 0) {
          IsLowerCaseBase = 1;
        }
      }
    }
    i++;
    if (i >= NumBytes) {              // End of name ?
      if (ExtPos == -1) {
        ExtPos = (int)i;
      }
      break;
    }
  }
  //
  // Perform some checks.
  //
  if (ExtPos == 0) {
    return 1;                         // Error, no file name.
  }
  if (ExtPos > 8) {
    return 1;                         // Error, file name too long.
  }
  if (((int)i - ExtPos) > 4) {
    return 1;                         // Error, extension too long.
  }
  if ((IsUpperCaseBase != 0) && (IsLowerCaseBase != 0)) {
    return 1;                         // Error, mixed-case base name.
  }
  if ((IsUpperCaseExt != 0) && (IsLowerCaseExt != 0)) {
    return 1;                         // Error, mixed-case extension.
  }
#if (FS_FAT_LFN_LOWER_CASE_SHORT_NAMES == 0)
  if ((IsLowerCaseBase != 0) || (IsLowerCaseExt != 0)) {
    return 1;                         // Error, base name and extension are not upper-case
  }
#else
  if (IsLowerCaseBase != 0) {
    Flags |= FS_FAT_FLAG_LOWER_CASE_BASE;
  }
  if (IsLowerCaseExt != 0) {
    Flags |= FS_FAT_FLAG_LOWER_CASE_EXT;
  }
#endif
  //
  // All checks passed, copy filename and extension.
  //
  FS_FAT_StoreShortNamePartial(&pShortName->ac[0], pLongName,              8, ExtPos);
  FS_FAT_StoreShortNamePartial(&pShortName->ac[8], pLongName + ExtPos + 1, 3, (int)i - (ExtPos + 1));
  if (pFlags != NULL) {
    *pFlags = Flags;
  }
  return 0;                           // O.K., file name successfully converted.
}

#if (FS_SUPPORT_MBCS != 0) && (FS_SUPPORT_FILE_NAME_ENCODING != 0)

/*********************************************************************
*
*       _StoreShortNameMB
*
*  Function description
*    Convert a given name to the format which is used in the FAT directory.
*/
static int _StoreShortNameMB(FS_83NAME * pShortName, const U8 * pLongName, unsigned NumBytes, unsigned * pFlags) {
  unsigned i;
  int      ExtPos;
  unsigned Flags;
  int      IsLowerCaseBase;
  int      IsUpperCaseBase;
  int      IsLowerCaseExt;
  int      IsUpperCaseExt;
  FS_WCHAR Char;
  unsigned NumBytesRead;

  ExtPos          = -1;               // Set to an invalid value.
  i               = 0;
  IsUpperCaseBase = 0;
  IsLowerCaseBase = 0;
  IsUpperCaseExt  = 0;
  IsLowerCaseExt  = 0;
  Flags           = 0;
  for (;;) {
    if (i >= 13u) {
      return 1;                       // Error, file name too long.
    }
    NumBytesRead = 0;
    Char = _pUnicodeConv->pfGetChar(pLongName + i, NumBytes - i, &NumBytesRead);
    if (Char == FS_WCHAR_INVALID) {
      return 1;                       // Invalid character encoding.
    }
    if (Char < 128u) {                // Only the ASCII characters have to be checked.
      if (FS_FAT_IsValidShortNameChar((U8)Char) == 0) {
        return 1;                     // Invalid character used in string.
      }
    }
    if (Char == (FS_WCHAR)'.') {
      if (ExtPos >= 0) {
        return 1;                     // Only one period character is allowed in an 8.3 file name.
      }
      ExtPos = (int)i;
    } else {
      if (ExtPos >= 0) {
        if (_IsUpper(Char) != 0) {
          IsUpperCaseExt = 1;
        }
        if (_IsLower(Char) != 0) {
          IsLowerCaseExt = 1;
        }
      } else {
        if (_IsUpper(Char) != 0) {
          IsUpperCaseBase = 1;
        }
        if (_IsLower(Char) != 0) {
          IsLowerCaseBase = 1;
        }
      }
    }
    i += NumBytesRead;
    if (i >= NumBytes) {              // End of name ?
      if (ExtPos == -1) {
        ExtPos = (int)i;
      }
      break;
    }
  }
  //
  // Perform some checks.
  //
  if (ExtPos == 0) {
    return 1;                         // Error, no file name.
  }
  if (ExtPos > 8) {
    return 1;                         // Error, file name too long.
  }
  if (((int)i - ExtPos) > 4) {
    return 1;                         // Error, extension too long.
  }
  if ((IsUpperCaseBase != 0) && (IsLowerCaseBase != 0)) {
    return 1;                         // Error, mixed-case base name.
  }
  if ((IsUpperCaseExt != 0) && (IsLowerCaseExt != 0)) {
    return 1;                         // Error, mixed-case extension.
  }
#if (FS_FAT_LFN_LOWER_CASE_SHORT_NAMES == 0)
  if ((IsLowerCaseBase != 0) || (IsLowerCaseExt != 0)) {
    return 1;                         // Error, base name and extension are not upper-case
  }
#else
  if (IsLowerCaseBase != 0) {
    Flags |= FS_FAT_FLAG_LOWER_CASE_BASE;
  }
  if (IsLowerCaseExt != 0) {
    Flags |= FS_FAT_FLAG_LOWER_CASE_EXT;
  }
#endif // FS_FAT_LFN_LOWER_CASE_SHORT_NAMES == 0
  //
  // All checks passed, copy filename and extension.
  //
  FS_FAT_StoreShortNameCompleteMB(pShortName, pLongName, i, (unsigned)ExtPos);
  if (pFlags != NULL) {
    *pFlags = Flags;
  }
  return 0;                           // O.K., file name successfully converted.
}

#endif // (FS_SUPPORT_MBCS != 0) && (FS_SUPPORT_FILE_NAME_ENCODING != 0)

/*********************************************************************
*
*       _MakeShortName
*
*  Function description
*    Convert a given name to the format which is used in the FAT directory.
*
*  Parameters
*    pShortName   [OUT] Encoded file or directory name.
*    pLongName    [IN] File name to be translated.
*    NumBytes     Number of bytes in the long file name.
*    pFlags       [OUT] Extended flags to be set for the directory entry.
*                 These flags indicate whether the base name, the extension
*                 or both should be displayed in lower case.
*
*  Return value
*    ==0   OK, file name can be stored in 8.3 format.
*    !=0   Error, name does not comply with the 8.3 format.
*
*  Notes
*    (1) Allowed file names
*        The filename must conform to 8.3 format.
*        The extension is optional, the name may be 8 characters at most.
*
*  Additional information
*    TEST.TXT -> 8.3 file name
*    TEST.txt -> 8.3 file name if FS_FAT_LFN_LOWER_CASE_SHORT_NAMES == 1 else long file name
*    test.TXT -> 8.3 file name if FS_FAT_LFN_LOWER_CASE_SHORT_NAMES == 1 else long file name
*    test.txt -> 8.3 file name if FS_FAT_LFN_LOWER_CASE_SHORT_NAMES == 1 else long file name
*    Text.txt -> long file name
*/
static int _MakeShortName(FS_83NAME * pShortName, const U8 * pLongName, unsigned NumBytes, unsigned * pFlags) {
  int r;

#if (FS_SUPPORT_MBCS != 0) && (FS_SUPPORT_FILE_NAME_ENCODING != 0)
  if (_IsOEMEncoding() == 0u) {
    return 1;                             // A short name can contain only OEM characters.
  }
  if (_IsMBEncoding() != 0u) {
    r = _StoreShortNameMB(pShortName, pLongName, NumBytes, pFlags);
  } else
#endif
  {
    r = _StoreShortName(pShortName, pLongName, NumBytes, pFlags);
  }
  return r;
}

/*********************************************************************
*
*       Static code (public via callback)
*
**********************************************************************
*/

/*********************************************************************
*
*       _LFN_ReadDirEntryInfo
*
*  Function description
*    Searches for long directory entry and returns information about it.
*
*  Return value
*    ==1   End of directory reached.
*    ==0   OK, information about the directory entry returned.
*    < 0   Error code indicating the failure reason.
*/
static int _LFN_ReadDirEntryInfo(FS_DIR_OBJ * pDirObj, FS_DIRENTRY_INFO * pDirEntryInfo, FS_DIR_POS * pDirPosLFN, FS_SB * pSB) {
  FS_FAT_DENTRY * pDirEntry;
  FS_FAT_INFO   * pFATInfo;
  FS_VOLUME     * pVolume;
  int             r;
  U32             DirIndex;
  FS_DIR_POS   *  pDirPos;
  FS_DIR_POS      DirPosStart;
  unsigned        Attr;
  unsigned        NumLongEntries;
  int             Index;
  unsigned        CheckSum;
  unsigned        CalcCheckSum;
  int             IsDifferent;
  unsigned        NumChars;
  int             Result;

  pVolume  = pDirObj->pVolume;
  pFATInfo = &pVolume->FSInfo.FATInfo;
  DirIndex = pDirObj->DirPos.DirEntryIndex;
  pDirPos  = &pDirObj->DirPos;
  FS_MEMSET(&DirPosStart, 0, sizeof(DirPosStart));
  if (DirIndex == 0u) {
    FS_FAT_InitDirEntryScan(pFATInfo, pDirPos, pDirObj->DirPos.FirstClusterId);
  }
  for (;;) {
    pDirEntry = FS_FAT_GetDirEntry(pVolume, pSB, pDirPos);
    DirPosStart = *pDirPos;                                 // Structure copy.
    FS_FAT_IncDirPos(pDirPos);
    if (pDirEntry == NULL) {
      r = FS__SB_GetError(pSB);
      if (r != 0) {
        r = FS_ERRCODE_READ_FAILURE;
      } else {
        r = 1;                                              // OK, end of directory reached.
      }
      break;
    }
    if (pDirEntry->Data[0] == 0u) {                         // Last entry found ?
      r = 1;
      break;
    }
    if (pDirEntry->Data[0] != DIR_ENTRY_INVALID_MARKER) {   // Not a deleted file?
      Attr = pDirEntry->Data[11];
      if (Attr != FS_FAT_ATTR_VOLUME_ID) {
        if (Attr != FS_FAT_ATTR_LONGNAME) {                 // Also not a long entry, so it is a valid entry
          r = _LoadShortName(pDirEntryInfo->sFileName, (unsigned)pDirEntryInfo->SizeofFileName, &pDirEntry->Data[0]);
          FS_FAT_CopyDirEntryInfo(pDirEntry, pDirEntryInfo);
          break;
        }
        NumLongEntries = (unsigned)pDirEntry->Data[0] & 0x3Fu;
        if (NumLongEntries == 0u) {
          r = FS_ERRCODE_INVALID_DIRECTORY_ENTRY;
          break;
        }
        Index          = (int)NumLongEntries;
        CheckSum       = pDirEntry->Data[13];
        IsDifferent    = 0;
        while (--Index != 0) {
          pDirEntry = FS_FAT_GetDirEntry(pVolume, pSB, pDirPos);
          if (pDirEntry == NULL) {
            IsDifferent = 1;
            break;
          }
          if (pDirEntry->Data[13] != (U8)CheckSum) {
            IsDifferent = 1;
          }
          FS_FAT_IncDirPos(pDirPos);
        }
        pDirEntry = FS_FAT_GetDirEntry(pVolume, pSB, pDirPos);
        if (pDirEntry == NULL) {
          r = FS_ERRCODE_READ_FAILURE;
          break;
        }
        CalcCheckSum = _CalcCheckSum(SEGGER_PTR2PTR(FS_83NAME, pDirEntry));               // MISRA deviation D:100[e]
        if ((IsDifferent == 0) && (CalcCheckSum == CheckSum)) {
          int        NumBytesCopied;
          int        DirEntryLen;
          int        UseShortName;
          char     * sFileName;
          unsigned   MaxNumBytes;

          Index        = (int)NumLongEntries - 1;
          DirEntryLen  = 0;
          UseShortName = 0;
          sFileName    = pDirEntryInfo->sFileName;
          MaxNumBytes  = (unsigned)pDirEntryInfo->SizeofFileName;
          if (sFileName != NULL) {
            r = 0;
            do {
              _SetDirPosIndex(pDirPos, -1);
              pDirEntry = FS_FAT_GetDirEntry(pVolume, pSB, pDirPos);
              if (pDirEntry == NULL) {
                r = FS_ERRCODE_READ_FAILURE;
                break;
              }
              //
              // Check if we exceed the maximum of DirName size.
              //
              NumChars = _CalcNumCharsInDirEntry(&pDirEntry->Data[1]);
              if ((DirEntryLen + (int)NumChars) > (pDirEntryInfo->SizeofFileName - 1)) {
                //
                // We cannot process this long file name. Search for the short file name.
                //
                _SetDirPosIndex(pDirPos, -Index);
                UseShortName = 1;
                break;
              }
              NumBytesCopied = _LoadNameFromDirEntry((U8 *)sFileName, MaxNumBytes, &pDirEntry->Data[1]);
              if (NumBytesCopied < 0) {
                //
                // We cannot process this long file name. Search for the short file name.
                //
                _SetDirPosIndex(pDirPos, -Index);
                UseShortName = 1;
                break;
              }
              sFileName      += NumBytesCopied;
              DirEntryLen    += NumBytesCopied;
            } while (Index-- != 0);
            if (r != 0) {
              break;
            }
          }
          _SetDirPosIndex(pDirPos, (int)NumLongEntries);
          pDirEntry = FS_FAT_GetDirEntry(pVolume, pSB, pDirPos);
          if (pDirEntry == NULL) {
            r = FS_ERRCODE_READ_FAILURE;
            break;
          }
          if (UseShortName != 0) {
            Result = _LoadShortName(pDirEntryInfo->sFileName, (unsigned)pDirEntryInfo->SizeofFileName, &pDirEntry->Data[0]);
            if (Result < 0) {
              r = Result;                     // Error, invalid short name.
              break;
            }
          } else {
            if (sFileName != NULL) {
              *sFileName = '\0';
            }
          }
          FS_FAT_CopyDirEntryInfo(pDirEntry, pDirEntryInfo);
          if (pDirPosLFN != NULL) {
            *pDirPosLFN = DirPosStart;        // Structure copy.
          }
          _SetDirPosIndex(pDirPos, 1);
          r = 0;
          break;
        }
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _LFN_FindDirEntry
*/
static FS_FAT_DENTRY * _LFN_FindDirEntry(FS_VOLUME * pVolume, FS_SB * pSB, const char * sFileName, int Len, U32 DirStart, FS_DIR_POS * pDirPos, unsigned AttrRequired, FS_DIR_POS * pDirPosLFN) {
  FS_FAT_DENTRY   DirEntry;
  FS_FAT_DENTRY * pDirEntry;
  int             NumLongEntries;
  int             CurrentIndex;
  int             LastIndex;
  FS_FAT_INFO   * pFATInfo;
  FS_DIR_POS      DirPosStart;
  unsigned        CheckSum;
  int             r;
  int             IsValidShortName;
  FS_83NAME       ShortEntry;
  unsigned        NumBytes;
  unsigned        NumChars;

  FS_MEMSET(&DirPosStart, 0, sizeof(DirPosStart));
  pFATInfo = &pVolume->FSInfo.FATInfo;
  NumChars         = 0;
  NumBytes         = 0;
  sFileName        = _TrimFileName(sFileName, (unsigned)Len, &NumBytes);
  NumLongEntries   = _CalcNumLongEntries(sFileName, NumBytes, &NumChars);
  LastIndex        = -1;                      // Set to an invalid value.
  CurrentIndex     = NumLongEntries;
  CheckSum         = 0;
  IsValidShortName = 0;
  if (NumLongEntries > 0) {
    FS_MEMSET(&DirEntry, 0, sizeof(DirEntry));
    r = FS_FAT_MakeShortName(&ShortEntry, sFileName, (int)NumBytes, 1);   // 1 means that we also search for invalid short file names that contain two or more period characters.
    if (r == 0) {
      IsValidShortName = 1;
    }
    FS_FAT_InitDirEntryScan(pFATInfo, pDirPos, DirStart);
    for (;;) {
      pDirEntry = FS_FAT_GetDirEntry(pVolume, pSB, pDirPos);
      if (pDirEntry == NULL) {
        break;                                // Error, no directory entry found.
      }
      if (pDirEntry->Data[0] == 0u) {
        break;                                // No more entries. Not found.
      }
      if (pDirEntry->Data[0] == DIR_ENTRY_INVALID_MARKER) {
        FS_FAT_IncDirPos(pDirPos);
        continue;                             // Skip deleted directory entries.
      }
      if (CurrentIndex != LastIndex) {
        if (CurrentIndex != 0) {
          r = _StoreLongDirEntry(&DirEntry, (const U8 *)sFileName, NumBytes, NumChars, (unsigned)NumLongEntries, (unsigned)CurrentIndex, 0);
          if (r < 0) {
            return NULL;                      // Error, cannot store file name.
          }
          LastIndex = CurrentIndex;
          //
          // If the length of the long name in characters is a multiple of 13 (such as "1234567890.12"),
          // The long name is not padded. This means that if we have a longer entry,
          // we need to skip all DirEntries until after we find a short one or the last long one.
          //
          if (0u == (NumBytes % 13u)) {
            if (pDirEntry->Data[0] > (0x40u + (unsigned)NumLongEntries)) {   // Is this entry is too long for what we are looking for ?
              for (;;) {
                if (pDirEntry->Data[0] == 0u) {
                  return NULL;                // End of directory, file not found
                }
                if (pDirEntry->Data[11] != 0xFu) {
                  break;
                }
                FS_FAT_IncDirPos(pDirPos);
                pDirEntry = FS_FAT_GetDirEntry(pVolume, pSB, pDirPos);
                if (pDirEntry == NULL) {
                  return NULL;                // An error occurred while reading the directory entry.
                }
              }
              FS_FAT_IncDirPos(pDirPos);      // Skip one more
              CurrentIndex = NumLongEntries;  // Start over
              continue;
            }
          }
        }
      }
      //
      // Check if the DirEntry matches. For a long name with n characters,
      // We need to check (n +12) / 13 Long-DirEntry
      // And last one Short Entry
      //
      if (CurrentIndex != 0) {
        int IsDifferent;

        //
        // Check DirEntry as part of long name.
        //
        IsDifferent = _CompareLongDirEntry(pDirEntry, &DirEntry);
        if (CurrentIndex == NumLongEntries) {
          CheckSum    = pDirEntry->Data[13];
          DirPosStart = *pDirPos;             // Structure copy.
        } else {
          if ((U8)CheckSum != pDirEntry->Data[13]) {
            IsDifferent = 1;
          }
        }
        if (IsDifferent == 0) {               // Does name match?
          CurrentIndex--;
        } else {
          //
          // In case of a mismatch check if the found directory entry
          // the corresponding short name of the long file name.
          //
          if (IsValidShortName != 0) {
            IsDifferent = FS_MEMCMP(pDirEntry->Data, &ShortEntry, sizeof(ShortEntry));
            if (IsDifferent == 0) {
              return pDirEntry;
            }
          }
          CurrentIndex = NumLongEntries;      // Start over.
        }
        FS_FAT_IncDirPos(pDirPos);
      } else {
        //
        // Long name O.K., now check short name as well.
        //
        unsigned CheckSumShort;

        CheckSumShort = _CalcCheckSum(SEGGER_PTR2PTR(FS_83NAME, pDirEntry));              // MISRA deviation D:100[e]
        if ((CheckSumShort != CheckSum) || ((pDirEntry->Data[DIR_ENTRY_OFF_ATTRIBUTES] & AttrRequired) != AttrRequired)) {
          CurrentIndex = NumLongEntries;      // Start over.
        } else {
          //
          // Success ! We have found a matching long entry.
          //
          if (pDirPosLFN != NULL) {
            *pDirPosLFN = DirPosStart;        // Structure copy
          }
          return pDirEntry;
        }
      }
    }
  }
  return NULL;                                // Not found.
}

/*********************************************************************
*
*       _LFN_DelLongEntry
*
*  Function description
*    Marks as deleted all the directory entries belonging to a long
*    file name.
*
*  Parameters
*    pVolume      [IN] Identifies the volume on which the file is located.
*    pSB          Sector buffer to work with.
*    pDirPosLFN   Position of the first directory entry in the parent directory.
*
*  Return value
*    ==0      OK, long file name deleted.
*    !=0      Error code indicating the failure reason.
*/
static int _LFN_DelLongEntry(FS_VOLUME * pVolume, FS_SB * pSB, FS_DIR_POS * pDirPosLFN) {
  FS_FAT_DENTRY * pDirEntry;
  unsigned        NumShortEntries;
  int             r;

  r = 0;          // Set to indicate success.
  //
  // Delete only if the position of the long directory entry is valid.
  //
  if (FS_FAT_IsValidDirPos(pDirPosLFN) != 0) {
    //
    // Calculate the number of short entries for this long entry.
    //
    pDirEntry = FS_FAT_GetDirEntry(pVolume, pSB, pDirPosLFN);
    if (pDirEntry != NULL) {
      if (pDirEntry->Data[0] == DIR_ENTRY_INVALID_MARKER) {
        r = FS_ERRCODE_READ_FAILURE;                        // Error, trying to delete an entry marked as deleted.
      } else {
        NumShortEntries = (unsigned)pDirEntry->Data[0] & 0x3Fu;
        if (NumShortEntries != 0u) {
          //
          // Delete entries one by one.
          //
          do {
            if (pDirEntry == NULL) {
              r = FS_ERRCODE_READ_FAILURE;                  // Error, could not delete all the entries.
              break;
            }
            pDirEntry->Data[0] = DIR_ENTRY_INVALID_MARKER;  // Mark entry as deleted
            FS__SB_MarkDirty(pSB);
            FS_FAT_IncDirPos(pDirPosLFN);                   // Advance to next directory entry.
            pDirEntry = FS_FAT_GetDirEntry(pVolume, pSB, pDirPosLFN);
          } while (--NumShortEntries != 0u);
        }
      }
    } else {
      r = FS_ERRCODE_FILE_DIR_NOT_FOUND;                    // Error, first directory entry not found.
    }
  }
  return r;
}

/*********************************************************************
*
*       _LFN_CreateDirEntry
*
*  Function description
*    Creates a long directory entry.
*
*  Return value
*    ==NULL     Error, could not create long file name entry
*    !=NULL     Pointer to "main" directory entry (of the short name)
*
*  Notes
*    (1) Order of entries
*        A long directory name consists of a number of entries making up the long name,
*        immediately followed by the short name.
*    (2) Finding a unique short name
*        The short name needs to be unique in a directory.
*        We can build different short names (basically by adding a number).
*        The strategy is to try the short names until we find one that
*        is unique.
*    (3) Finding an empty slot
*        The important point is that all directory entries (n long + 1 short)
*        are adjacent. We therefor need to look for n+1 adjacent, unused entries.
*/
static FS_FAT_DENTRY * _LFN_CreateDirEntry(FS_VOLUME * pVolume, FS_SB * pSB, const char * sFileName, U32 DirStart, U32 ClusterId, unsigned Attribute, U32 Size, unsigned Time, unsigned Date) {
  unsigned        NumLongEntries;   // Number of required directory entries
  unsigned        FreeEntryCnt;     // Number of empty, adjacent directory entries
  FS_DIR_POS      DirPos;
  FS_DIR_POS      DirPosStart;      // Remember directory position of first empty entry
  FS_FAT_INFO   * pFATInfo;
  FS_FAT_DENTRY * pDirEntry;
  FS_83NAME       ShortEntry;
  unsigned        CheckSum;
  U32             Index;            // The index can go up to 999999
  unsigned        NumBytes;
  I32             FreeIndex;
  U8              aBitField[((unsigned)FS_FAT_LFN_BIT_ARRAY_SIZE + 7u) >> 3];
  int             r;
  unsigned        Flags;
  unsigned        NumChars;

  NumBytes = 0;
  sFileName = _TrimFileName(sFileName, 0, &NumBytes);
  if (_IsValidFileName((const U8 *)sFileName, NumBytes) == 0) {
    return NULL;              // Error, the file name is not valid.
  }
  Flags = 0;
  FS_MEMSET(&ShortEntry, 0, sizeof(ShortEntry));
  pFATInfo = &pVolume->FSInfo.FATInfo;
  //
  // The return value _CalcNumLongEntries() does not have to be checked.
  // The validity of the encoded file name is checked in _IsValidFileName()
  //
  NumChars = 0;
  NumLongEntries = (unsigned)_CalcNumLongEntries(sFileName, NumBytes, &NumChars);
  if (NumLongEntries == 1u) {
    r = _MakeShortName(&ShortEntry, (const U8 *)sFileName, NumBytes, &Flags);
    if (r == 0) {
      NumLongEntries = 0;             // Create only the short name entry if the file name is in 8.3 format.
    }
  }
  //
  // Find short directory name that has not yet been taken (Note 2).
  //
  if (NumLongEntries != 0u) {
    Index = 0;
    for (;;) {
      FS_MEMSET(aBitField, 0, sizeof(aBitField));
      r = _MakeShortNameIndexed(&ShortEntry, (const U8 *)sFileName, NumBytes, Index);
      if (r != 0) {
        return NULL;                    // Error, could not generate short file name.
      }
      //
      // Look for a free index number for the short name.
      //
      FreeIndex = _FindFreeIndex(pVolume, pSB, &ShortEntry, DirStart, aBitField, Index);
      if (FreeIndex >= 0) {
        //
        // We found an entry, generate the real short name.
        //
        // TBD: It seems that we create the same short name twice when Index == FreeIndex.
        //
        r = _MakeShortNameIndexed(&ShortEntry, (const U8 *)sFileName, NumBytes, (U32)FreeIndex);
        if (r != 0) {
          return NULL;                  // Error, could not generate short file name.
        }
        break;
      }
      if (Index >= (U32)FS_FAT_LFN_MAX_SHORT_NAME) {
        return NULL;                    // Error, all short names seem to be taken.
      }
      Index += (U32)FS_FAT_LFN_BIT_ARRAY_SIZE;
    }
  }
  //
  // Read directory, trying to find an empty slot (Note 3)
  //
  FS_FAT_InitDirEntryScan(pFATInfo, &DirPos, DirStart);
  FreeEntryCnt = 0;
  for (;;) {
    unsigned Byte;

    pDirEntry = FS_FAT_GetDirEntry(pVolume, pSB, &DirPos);
    if (pDirEntry == NULL) {
      //
      // Grow directory if possible.
      //
      if ((DirStart == 0u) && (pFATInfo->RootEntCnt != 0u)) {
        //
        // Root directory of FAT12/16 medium can not be increased.
        //
        FS_DEBUG_ERROROUT((FS_MTYPE_FS, "_LFN_CreateDirEntry: Root directory too small."));
        return NULL;                // Error, can not create entry, directory is full.
      } else {
        U32 NewCluster;
        U32 LastCluster;

        //
        // Allocate a new cluster if required.
        //
        LastCluster = FS_FAT_FindLastCluster(pVolume, pSB, DirPos.ClusterId, NULL);
        NewCluster  = FS_FAT_AllocCluster(pVolume, pSB, LastCluster);
        //
        // Write the data to allocation table.
        //
        FS__SB_Flush(pSB);
        if (NewCluster != 0u) {
          U32            DirSector;
          U8           * pBuffer;
          FS_PARTITION * pPart;
          U32            NumSectors;

          //
          // Clean new directory cluster (Fill with 0)
          //
          pPart = &pVolume->Partition;
          DirSector = FS_FAT_ClusterId2SectorNo(pFATInfo, NewCluster);
          NumSectors = pFATInfo->SectorsPerCluster;
          pBuffer = FS__SB_GetBuffer(pSB);
          FS_MEMSET(pBuffer, 0x00, pFATInfo->BytesPerSector);
          r = FS_LB_WriteMultiplePart(pPart, DirSector, NumSectors, pBuffer, FS_SECTOR_TYPE_DIR, 1);
          FS__SB_MarkNotValid(pSB);       // Invalidate in sector buffer that this sector has been read.
#if FS_SUPPORT_SECTOR_BUFFER_CACHE
          FS__InvalidateSectorBuffer(pVolume, DirSector, NumSectors);
#endif // FS_SUPPORT_SECTOR_BUFFER_CACHE
          if (r != 0) {
            return NULL;            // Error, could not initialize the directory.
          }
          FS__SB_MarkValid(pSB, DirSector, FS_SECTOR_TYPE_DIR, 1);
          pDirEntry = SEGGER_PTR2PTR(FS_FAT_DENTRY, pBuffer);                             // MISRA deviation D:100[e]
        } else {
          FS_DEBUG_ERROROUT((FS_MTYPE_FS, "_LFN_CreateDirEntry: Disk is full."));
          return NULL;
        }
      }
    }
    Byte = pDirEntry->Data[0];
    if ((Byte == 0u) || (Byte == DIR_ENTRY_INVALID_MARKER)) {   // Is this entry free ?
      if (FreeEntryCnt == 0u) {
        DirPosStart = DirPos;
      }
      if (FreeEntryCnt++ == NumLongEntries) {
        break;                                                  // We found sufficient entries.
      }
    } else {
      FreeEntryCnt = 0;
    }
    FS_FAT_IncDirPos(&DirPos);
  }
  //
  // Create long file name directory entry.
  //
  if (NumLongEntries != 0u) {
    Index    = NumLongEntries;
    CheckSum = _CalcCheckSum(&ShortEntry);
    do {
      pDirEntry = FS_FAT_GetDirEntry(pVolume, pSB, &DirPosStart);
      if (pDirEntry == NULL) {
        return NULL;
      }
      r = _StoreLongDirEntry(pDirEntry, (const U8 *)sFileName, NumBytes, NumChars, NumLongEntries, Index, CheckSum);
      if (r != 0) {
        return NULL;
      }
      FS__SB_MarkDirty(pSB);
      FS_FAT_IncDirPos(&DirPosStart);
    } while (--Index != 0u);
  }
  //
  // Create short directory entry.
  //
  pDirEntry = FS_FAT_GetDirEntry(pVolume, pSB, &DirPosStart);
  if (pDirEntry != NULL) {
    FS_FAT_WriteDirEntry83(pDirEntry, &ShortEntry, ClusterId, Attribute, Size, Time, Date, Flags);
  }
  FS__SB_MarkDirty(pSB);
  return pDirEntry;
}

#if FS_SUPPORT_FAT

/*********************************************************************
*
*       Public code (internal)
*
**********************************************************************
*/

#if FS_SUPPORT_FILE_NAME_ENCODING

/*********************************************************************
*
*      FS_FAT_LFN_Save
*/
void FS_FAT_LFN_Save(FS_CONTEXT * pContext) {
  pContext->FAT_LFN_pUnicodeConv = _pUnicodeConv;
}

/*********************************************************************
*
*      FS_FAT_LFN_Restore
*/
void FS_FAT_LFN_Restore(const FS_CONTEXT * pContext) {
  _pUnicodeConv = pContext->FAT_LFN_pUnicodeConv;
}

#endif // FS_SUPPORT_FILE_NAME_ENCODING

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_FAT_SupportLFN
*
*  Function description
*    Enables the support for long file names.
*
*  Additional information
*    The file system accepts per default only file and directory
*    names in 8.3 format, that is maximum 8 characters in the base
*    name of a file, an optional period character, and an optional
*    extension of maximum 3 characters. The application can call
*    FS_FAT_SupportLFN() to enable the file system to work with
*    names for files and directories longer that in the 8.3 format.
*
*    This function applies only to volumes formatted as FAT.
*    EFS-formatted volumes have native support for long file names.
*/
void FS_FAT_SupportLFN(void) {
  FS_LOCK();
  FS_LOCK_SYS();
  FAT_pDirEntryAPI = &FAT_LFN_API;
  FS_UNLOCK_SYS();
  FS_UNLOCK();
}

/*********************************************************************
*
*       FS_FAT_DisableLFN
*
*  Function description
*    Disables the support for long file names.
*
*  Additional information
*    After calling this function the file system accepts only file
*    and directory names in 8.3 format. Files and directories created
*    with support for long file names enabled are still accessible
*    since each long file name has an associated name in 8.3 format.
*    The short name is automatically generated by the file system
*    based on the first characters of the long name and a sequential
*    index. The support for long file names can be activated via
*    FS_FAT_SupportLFN().
*
*    This function applies only to volumes formatted as FAT.
*    EFS-formatted volumes have native support for long file names.
*/
void FS_FAT_DisableLFN(void) {
  FS_LOCK();
  FS_LOCK_SYS();
  FAT_pDirEntryAPI = &FAT_SFN_API;
  FS_UNLOCK_SYS();
  FS_UNLOCK();
}

#if FS_SUPPORT_FILE_NAME_ENCODING

/*********************************************************************
*
*       FS_FAT_SetLFNConverter
*
*  Function description
*    Configures how long file names are to be encoded and decoded.
*
*  Parameters
*    pUnicodeConv   File name converter.
*
*  Additional information
*    This function is available only if FS_SUPPORT_FILE_NAME_ENCODING
*    is set to 1 which is the default.
*
*    Permitted values for pUnicodeConv are:
*    +-----------------------+---------------------------------------------------+
*    | Identifier            | Description                                       |
*    +-----------------------+---------------------------------------------------+
*    | FS_UNICODE_CONV_CP437 | Unicode <-> CP437 (DOS Latin US) converter        |
*    +-----------------------+---------------------------------------------------+
*    | FS_UNICODE_CONV_CP932 | Unicode <-> CP932 (Shift JIS) converter           |
*    +-----------------------+---------------------------------------------------+
*    | FS_UNICODE_CONV_CP936 | Unicode <-> CP936 (GBK) converter                 |
*    +-----------------------+---------------------------------------------------+
*    | FS_UNICODE_CONV_CP949 | Unicode <-> CP949 (Unified Hangul Code) converter |
*    +-----------------------+---------------------------------------------------+
*    | FS_UNICODE_CONV_CP950 | Unicode <-> CP950 (Big5) converter                |
*    +-----------------------+---------------------------------------------------+
*    | FS_UNICODE_CONV_UTF8  | Unicode <-> UTF-8 converter                       |
*    +-----------------------+---------------------------------------------------+
*/
void FS_FAT_SetLFNConverter(const FS_UNICODE_CONV * pUnicodeConv) {
  FS_LOCK();
  FS_LOCK_SYS();
  _pUnicodeConv = pUnicodeConv;
  FS_UNLOCK_SYS();
  FS_UNLOCK();
}

/*********************************************************************
*
*       FS_FAT_GetLFNConverter
*
*  Function description
*    Returns the type of configured file name converter.
*
*  Return value
*    Configured file name converter.
*
*  Additional information
*    This function is available only if FS_SUPPORT_FILE_NAME_ENCODING
*    is set to 1 which is the default.
*    Refer to pUnicodeConv parameter of FS_FAT_SetLFNConverter() for
*    a list of possible return values.
*/
const FS_UNICODE_CONV * FS_FAT_GetLFNConverter(void) {
  const FS_UNICODE_CONV * pUnicodeConv;

  FS_LOCK();
  FS_LOCK_SYS();
  pUnicodeConv = _pUnicodeConv;
  FS_UNLOCK_SYS();
  FS_UNLOCK();
  return pUnicodeConv;
}

#endif // FS_SUPPORT_FILE_NAME_ENCODING

#endif // FS_SUPPORT_FAT

/*********************************************************************
*
*       Public const data
*
**********************************************************************
*/

/*********************************************************************
*
*       FAT_LFN_API
*/
const FAT_DIRENTRY_API FAT_LFN_API = {
  _LFN_ReadDirEntryInfo,
  _LFN_FindDirEntry,
  _LFN_CreateDirEntry,
  _LFN_DelLongEntry
};

#endif  // FS_FAT_SUPPORT_LFN

/*************************** End of file ****************************/
