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
File        : FS_FAT_DirEntry.c
Purpose     : FAT routines for retrieving/setting dir entry info
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*             #include Section
*
**********************************************************************
*/
#include "FS_Int.h"
#include "FS_FAT.h"
#include "FS_FAT_Int.h"

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _GetDirInfo
*/
static void _GetDirInfo(const FS_FAT_DENTRY * pDirEntry, void * p, int Mask) {
  U32 * pData32;

  pData32 = SEGGER_PTR2PTR(U32, p);                                                         // MISRA deviation D:100[e]
  switch (Mask) {
    case FS_DIRENTRY_GET_ATTRIBUTES:
    {
      U8 * pAttributes;

      pAttributes = SEGGER_PTR2PTR(U8, p);                                                  // MISRA deviation D:100[e]
      *pAttributes = pDirEntry->Data[DIR_ENTRY_OFF_ATTRIBUTES];
      break;
    }
    case FS_DIRENTRY_GET_TIMESTAMP_CREATE:
      *pData32 = FS_LoadU32LE(&pDirEntry->Data[DIR_ENTRY_OFF_CREATION_TIME]);
      break;
    case FS_DIRENTRY_GET_TIMESTAMP_MODIFY:
      *pData32 = FS_LoadU32LE(&pDirEntry->Data[DIR_ENTRY_OFF_WRITE_TIME]);
      break;
    case FS_DIRENTRY_GET_TIMESTAMP_ACCESS:
      *pData32 = (U32)FS_LoadU16LE(&pDirEntry->Data[DIR_ENTRY_OFF_LAST_ACCESS_DATE]) << 16;
      break;
    case FS_DIRENTRY_GET_SIZE:
      *pData32 = FS_LoadU32LE(&pDirEntry->Data[DIR_ENTRY_OFF_SIZE]);
      break;
    default:
      //
      // Error, invalid parameter.
      //
      break;
  }
}

/*********************************************************************
*
*       _SetDirInfo
*/
static void _SetDirInfo(FS_FAT_DENTRY * pDirEntry, const void  * p, int Mask) {
  U32      TimeStamp;
  unsigned Date;
  unsigned Time;

  if (Mask == FS_DIRENTRY_SET_ATTRIBUTES) {
    unsigned Attributes;
    unsigned AttributesSet;

    AttributesSet = *SEGGER_CONSTPTR2PTR(const U8, p);                                      // MISRA deviation D:100[e]
    Attributes    = pDirEntry->Data[DIR_ENTRY_OFF_ATTRIBUTES];
    //
    // Preserve the value of the directory flag.
    //
    if ((Attributes & FS_FAT_ATTR_DIRECTORY) == FS_FAT_ATTR_DIRECTORY) {
      AttributesSet |= FS_FAT_ATTR_DIRECTORY;
    } else {
      AttributesSet &= ~FS_FAT_ATTR_DIRECTORY;
    }
    pDirEntry->Data[DIR_ENTRY_OFF_ATTRIBUTES] = (U8)AttributesSet;
  } else {
    TimeStamp = *SEGGER_CONSTPTR2PTR(const U32, p);                                         // MISRA deviation D:100[e]
    Date = TimeStamp >> 16;
    Time = TimeStamp & 0xFFFFu;
    switch (Mask) {
    case FS_DIRENTRY_SET_TIMESTAMP_CREATE:
      FS_StoreU16LE(&pDirEntry->Data[DIR_ENTRY_OFF_CREATION_TIME], Time);
      FS_StoreU16LE(&pDirEntry->Data[DIR_ENTRY_OFF_CREATION_DATE], Date);
      break;
    case FS_DIRENTRY_SET_TIMESTAMP_MODIFY:
      FS_StoreU16LE(&pDirEntry->Data[DIR_ENTRY_OFF_WRITE_TIME], Time);
      FS_StoreU16LE(&pDirEntry->Data[DIR_ENTRY_OFF_WRITE_DATE], Date);
      break;
    case FS_DIRENTRY_SET_TIMESTAMP_ACCESS:
      FS_StoreU16LE(&pDirEntry->Data[DIR_ENTRY_OFF_LAST_ACCESS_DATE], Date);
      break;
    default:
      //
      // Error, invalid parameter.
      //
      break;
    }
  }
}

/*********************************************************************
*
*       _IsRootDir
*
*  Function description
*    Checks if the name specifies the root directory.
*/
static int _IsRootDir(const char * sName) {
  int r;

  r = 0;
  if (*sName == '\0') {
    r = 1;
  } else {
    if (   (*sName       == FS_DIRECTORY_DELIMITER)
        && (*(sName + 1) == '\0')) {
      r = 1;
    }
  }
  return r;
}

/*********************************************************************
*
*       _IsRootDirEntry
*
*  Function description
*    Checks if the position specifies the root directory.
*/
static int _IsRootDirEntry(const FS_DIRENTRY_POS * pDirEntryPos) {
  int r;

  r = 0;
  if (   (pDirEntryPos->fat.SectorIndex   == SECTOR_INDEX_INVALID)
      && (pDirEntryPos->fat.DirEntryIndex == 0u)) {
    r = 1;
  }
  return r;
}

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_FAT_GetDirEntryInfo
*
*  Function description
*    Retrieves information about a directory entry.
*
*  Parameters
*    pVolume    Volume on which the directory entry is stored.
*    sName      Name of the file or directory (partially qualified).
*    p          Pointer to a buffer that receives the information.
*    Mask       Type of the information that need to be retrieved.
*
*  Return value
*    ==0        OK, information returned.
*    !=0        Error code indicating the failure reason.
*/
int FS_FAT_GetDirEntryInfo(FS_VOLUME * pVolume, const char * sName, void * p, int Mask) {
  const char    * sNamePQ;
  int             r;
  FS_SB           sb;
  U32             DirStart;
  FS_FAT_DENTRY * pDirEntry;
  U8            * pAttributes;

  if (_IsRootDir(sName) != 0) {
    r = FS_ERRCODE_INVALID_PARA;
    //
    // Only the file attributes of the root directory are supported.
    // The time stamp information is not available.
    //
    if (Mask == FS_DIRENTRY_GET_ATTRIBUTES) {
      pAttributes = SEGGER_PTR2PTR(U8, p);                                                  // MISRA deviation D:100[e]
      *pAttributes = FS_FAT_ATTR_DIRECTORY;
      r = FS_ERRCODE_OK;
    }
  } else {
    //
    // Search for the directory.
    //
    sNamePQ  = NULL;
    DirStart = 0;
    (void)FS__SB_Create(&sb, pVolume);
    r = FS_ERRCODE_PATH_NOT_FOUND;
    if (FS_FAT_FindPath(pVolume, &sb, sName, &sNamePQ, &DirStart) != 0) {
      r = FS_ERRCODE_FILE_DIR_NOT_FOUND;
      if (sNamePQ != NULL) {
        //
        // Check if the directory entry exists.
        //
        pDirEntry = FS_FAT_FindDirEntry(pVolume, &sb, sNamePQ, 0, DirStart, 0, NULL);
        if (pDirEntry != NULL) {
          _GetDirInfo(pDirEntry, p, Mask);
          r = FS_ERRCODE_OK;
        }
      }
    }
    FS__SB_Delete(&sb);
  }
  return r;
}

/*********************************************************************
*
*       FS_FAT_SetDirEntryInfo
*
*  Function description
*    Modifies information of a directory entry.
*
*  Parameters
*    pVolume    Volume on which the directory entry is stored.
*    sName      Path to the file/directory.
*    p          Pointer to a buffer that holds the information.
*    Mask       Type of the information that needs to be updated.
*
*  Return value
*    ==0        OK, information modified.
*    !=0        Error code indicating the failure reason.
*/
int FS_FAT_SetDirEntryInfo(FS_VOLUME * pVolume, const char * sName, const void * p, int Mask) {
  const char    * pName;
  int             r;
  FS_SB           sb;
  U32             DirStart;
  FS_FAT_DENTRY * pDirEntry;

  //
  // Search for the directory.
  //
  (void)FS__SB_Create(&sb, pVolume);
  if (FS_FAT_FindPath(pVolume, &sb, sName, &pName, &DirStart) != 0) {
    //
    // Check if the director entry exists.
    //
    pDirEntry = FS_FAT_FindDirEntry(pVolume, &sb, pName, 0, DirStart, 0, NULL);
    if (pDirEntry != NULL) {
      _SetDirInfo(pDirEntry, p, Mask);
      //
      // First, mark the volume as dirty.
      //
      FS_FAT_UpdateDirtyFlagIfRequired(pVolume, 1);
      //
      // Update the information to storage.
      //
      FS__SB_MarkDirty(&sb);
      r = FS_ERRCODE_OK;
    } else {
      r = FS_ERRCODE_FILE_DIR_NOT_FOUND;
    }
  } else {
    r = FS_ERRCODE_PATH_NOT_FOUND;
  }
  FS__SB_Delete(&sb);
  if (r == 0) {
    r = FS__SB_GetError(&sb);
  }
  return r;
}

/*********************************************************************
*
*       FS_FAT_GetDirEntryInfoEx
*
*  Function description
*    Retrieves information about a directory entry.
*
*  Parameters
*    pVolume        Volume on which the directory entry is stored.
*    pDirEntryPos   Position of the directory entry.
*    p              Pointer to a buffer that receives the information.
*    Mask           Type of the information that need to be retrieved.
*
*  Return value
*    ==0        OK, information returned.
*    !=0        Error code indicating the failure reason.
*/
int FS_FAT_GetDirEntryInfoEx(FS_VOLUME  * pVolume, const FS_DIRENTRY_POS * pDirEntryPos, void * p, int Mask) {
  int             r;
  FS_SB           sb;
  FS_FAT_DENTRY * pDirEntry;
  U8            * pAttr;

  r = FS_ERRCODE_OK;            // Set to indicate success.
  if (_IsRootDirEntry(pDirEntryPos) != 0) {
    r = FS_ERRCODE_NOT_SUPPORTED;
    //
    // Only the file attributes of the root directory are supported.
    // The time stamp information is not available.
    //
    if (Mask == FS_DIRENTRY_GET_ATTRIBUTES) {
      pAttr = SEGGER_PTR2PTR(U8, p);                                                        // MISRA deviation D:100[e]
      *pAttr = FS_FAT_ATTR_DIRECTORY;
      r = FS_ERRCODE_OK;
    }
  } else {
    (void)FS__SB_Create(&sb, pVolume);
    //
    // Check if the directory entry exists.
    //
    pDirEntry = FS_FAT_GetDirEntryEx(pVolume, &sb, pDirEntryPos);
    if (pDirEntry != NULL) {
      _GetDirInfo(pDirEntry, p, Mask);
    } else {
      r = FS_ERRCODE_FILE_DIR_NOT_FOUND;
    }
    FS__SB_Delete(&sb);
    if (r == 0) {
      r = FS__SB_GetError(&sb);
    }
  }
  return r;
}

/*********************************************************************
*
*       FS_FAT_SetDirEntryInfoEx
*
*  Function description
*    Modifies information of a directory entry.
*
*  Parameters
*    pVolume        Volume on which the directory entry is stored.
*    pDirEntryPos   Position of the directory entry.
*    p              Pointer to a buffer that holds the information.
*    Mask           Type of the information that needs to be updated.
*
*  Return value
*    ==0        OK, information modified.
*    !=0        Error code indicating the failure reason.
*/
int FS_FAT_SetDirEntryInfoEx(FS_VOLUME  * pVolume, const FS_DIRENTRY_POS * pDirEntryPos, const void * p, int Mask) {
  int             r;
  FS_SB           sb;
  FS_FAT_DENTRY * pDirEntry;

  r = FS_ERRCODE_OK;
  if (_IsRootDirEntry(pDirEntryPos) != 0) {
    r = FS_ERRCODE_NOT_SUPPORTED;
  } else {
    (void)FS__SB_Create(&sb, pVolume);
    pDirEntry = FS_FAT_GetDirEntryEx(pVolume, &sb, pDirEntryPos);
    if (pDirEntry != NULL) {
      _SetDirInfo(pDirEntry, p, Mask);
      //
      // First, mark the volume as dirty.
      //
      FS_FAT_UpdateDirtyFlagIfRequired(pVolume, 1);
      //
      // Update the information to storage.
      //
      FS__SB_MarkDirty(&sb);
    } else {
      r = FS_ERRCODE_FILE_DIR_NOT_FOUND;
    }
    FS__SB_Delete(&sb);
    if (r == 0) {
      r = FS__SB_GetError(&sb);
    }
  }
  return r;
}

/*************************** End of file ****************************/

