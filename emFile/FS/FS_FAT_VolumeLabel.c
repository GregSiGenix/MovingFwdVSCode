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
File        : FS_FAT_VolumeLabel.c
Purpose     : FAT File System Layer for handling the volume label
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
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _CopyName
*
*  Function description
*    Decodes a volume label.
*
*  Parameters
*    pDest              [OUT] Volume name as 0-terminated string.
*    pSrc               [IN] Volume name as stored on the storage device.
*    SizeOfVolumeLabel  Size of pDest in bytes.
*/
static void _CopyName(char * pDest, const U8 * pSrc, unsigned SizeOfVolumeLabel) {
  unsigned   i;
  char     * p;
  unsigned   NumBytes;

  if (SizeOfVolumeLabel != 0u) {
    --SizeOfVolumeLabel;                          // Reserve space for the 0-terminator.
    NumBytes = SEGGER_MIN(FAT_MAX_NUM_BYTES_SFN, SizeOfVolumeLabel);
    FS_MEMCPY(pDest, pSrc, NumBytes);
    p = pDest + NumBytes;
    *p-- = '\0';
    //
    // Remove trailing spaces.
    //
    for (i = NumBytes; i != 0u; i--) {
      if (*p != ' ') {
        break;
      }
      *p = '\0';
      p--;
    }
  }
}

/*********************************************************************
*
*       _FindVolumeDirEntry
*
*  Function description
*    Searches for the directory entry that stores the volume name.
*
*  Parameters
*    pVolume      Volume instance.
*    pSB          Sector buffer.
*
*  Return value
*    ==NULL       Directory entry not found.
*    !=NULL       Directory entry that stores the volume label.
*/
static FS_FAT_DENTRY * _FindVolumeDirEntry(FS_VOLUME * pVolume, FS_SB * pSB) {
  FS_FAT_DENTRY * pDirEntry;
  FS_DIR_POS      DirPos;

  FS_FAT_InitDirEntryScan(&pVolume->FSInfo.FATInfo, &DirPos, 0);
  for (;;) {
    pDirEntry = FS_FAT_GetDirEntry(pVolume, pSB, &DirPos);
    if (!pDirEntry) {
      break;
    }
    if (pDirEntry->Data[0] == 0u) {
      pDirEntry = NULL;
      break;                                                        // No more entries. Volume label not found.
    }
    if (   (pDirEntry->Data[DIR_ENTRY_OFF_ATTRIBUTES] == FS_FAT_ATTR_VOLUME_ID)
        && (pDirEntry->Data[0] != DIR_ENTRY_INVALID_MARKER)) {      // Attributes do match and not a deleted entry.
      break;
    }
    FS_FAT_IncDirPos(&DirPos);
  }
  return pDirEntry;
}

/*********************************************************************
*
*       _IsValidChar
*
*  Function description
*    Checks if a character is allowed in the name of a volume.
*
*  Parameters
*    c    Character to be checked.
*
*  Return value
*    !=0    The character is valid.
*    ==0    The character is invalid.
*/
static int _IsValidChar(char c) {
  int r;

  switch (c) {
  case '"':
  case '&':
  case '*':
  case '+':
  case '-':
  case ',':
  case '.':
  case '/':
  case ':':
  case ';':
  case '<':
  case '=':
  case '>':
  case '?':
  case '[':
  case ']':
  case '\\':
    r = 0;        // Invalid character
    break;
  default:
    r = 1;        // Permitted character
    break;
  }
  return r;
}

/*********************************************************************
*
*       _MakeName
*
*  Function description
*    Encodes a volume label.
*
*  Parameters
*    pVolumeLabel   [OUT] Volume label as stored on the storage device.
*    sVolumeLabel   [IN] Volume label provided by the application (0-terminated)
*
*  Additional information
*    An invalid character in the specified volume name is replaced
*    by underscore character.
*/
static void _MakeName(FS_83NAME * pVolumeLabel, const char * sVolumeLabel) {
  U8       * p;
  unsigned   NumBytes;
  unsigned   i;
  char       c;

  NumBytes = (unsigned)FS_STRLEN(sVolumeLabel);
  NumBytes = SEGGER_MIN(sizeof(pVolumeLabel->ac), NumBytes);
  FS_MEMSET(&pVolumeLabel->ac[0], (int)' ', sizeof(pVolumeLabel->ac));
  p = &pVolumeLabel->ac[0];
  for (i = 0; i < NumBytes; i++) {
    c = *sVolumeLabel++;
    if (_IsValidChar(c) == 0) {
      *p++ = (U8)'_';
    } else {
      *p++ = (U8)FS_TOUPPER((int)c);
    }
  }
}

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_FAT_GetVolumeLabel
*
*  Function description
*    Gets the label of a FAT volume, if it exists.
*
*  Parameters
*    pVolume            Volume instance.
*    sVolumeLabel       [OUT] Read volume label as 0-terminated string.
*    SizeOfVolumeLabel  Size in bytes of sVolumeLabel.
*
*  Return value
*    ==0      OK, volume label read.
*    !=0      Error code indicating the failure reason.
*/
int FS_FAT_GetVolumeLabel(FS_VOLUME * pVolume, char * sVolumeLabel, unsigned SizeOfVolumeLabel) {
  FS_FAT_DENTRY * pDirEntry;
  FS_SB           sb;
  int             r;

  r = FS_ERRCODE_OK;        // Set to indicate success.
  (void)FS__SB_Create(&sb, pVolume);
  //
  // Find the volume label entry.
  //
  pDirEntry = _FindVolumeDirEntry(pVolume,  &sb);
  if (pDirEntry != NULL) {
    //
    // volume label found, copy the name.
    //
    _CopyName(sVolumeLabel, pDirEntry->Data, SizeOfVolumeLabel);
  } else {
    //
    // No volume label available.
    //
    *sVolumeLabel = '\0';
    r = FS_ERRCODE_FILE_DIR_NOT_FOUND;        // Error, volume label not found.
  }
  FS__SB_Delete(&sb);
  return r;
}

/*********************************************************************
*
*       FS_FAT_SetVolumeLabel
*
*  Function description
*    Sets the label of a FAT volume.
*
*  Parameters
*    pVolume          Volume instance.
*    sVolumeLabel     [IN] Volume label as 0-terminated string.
*
*  Return value
*    ==0      OK, volume label modified.
*    !=0      Error code indicating the failure reason.
*
*  Additional information
*    The volume label is deleted if sVolumeLabel is set to NULL.
*/
int FS_FAT_SetVolumeLabel(FS_VOLUME * pVolume, const char * sVolumeLabel) {
  FS_FAT_DENTRY * pDirEntry;
  FS_SB           sb;
  int             r;

  r = FS_ERRCODE_OK;      // Set to indicate success.
  (void)FS__SB_Create(&sb, pVolume);
  //
  // Find the volume label entry.
  //
  pDirEntry = _FindVolumeDirEntry(pVolume,  &sb);
  //
  // Create or delete the volume label.
  //
  if (sVolumeLabel != NULL) {
    FS_83NAME VolLabel;
    U32       TimeDate;

    TimeDate = FS__GetTimeDate();
    _MakeName(&VolLabel, sVolumeLabel);
    if (pDirEntry == NULL) {
      pDirEntry = FS_FAT_FindEmptyDirEntry(pVolume, &sb, 0);
    }
    if (pDirEntry != NULL) {
      FS_FAT_WriteDirEntry83(pDirEntry, &VolLabel, 0, FS_FAT_ATTR_VOLUME_ID, 0, TimeDate & 0xFFFFu, TimeDate >> 16, 0);
    } else {
      r = FS_ERRCODE_VOLUME_FULL;
    }
  } else {
    if (pDirEntry != NULL) {
      //
      // Delete this volume label entry
      //
      pDirEntry->Data[0] = 0xE5;
    } else {
      r = FS_ERRCODE_FILE_DIR_NOT_FOUND;      // Error, volume label not found.
    }
  }
  //
  // Mark the volume as dirty.
  //
  FS_FAT_UpdateDirtyFlagIfRequired(pVolume, 1);
  //
  // Update the volume label on the storage.
  //
  FS__SB_MarkDirty(&sb);
  FS__SB_Delete(&sb);
  if (r == 0) {
    r = FS__SB_GetError(&sb);
  }
  return r;
}

/*************************** End of file ****************************/
