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
File        : FS_Attrib.c
Purpose     : Handling of file attributes.
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#include "FS_Int.h"

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

#if (FS_OS_LOCK_PER_DRIVER == 0)

/*********************************************************************
*
*       _GetFileAttrNL
*
*  Function description
*    Returns attributes of an opened file (without global locking).
*/
static int _GetFileAttrNL(const FS_FILE * pFile, U8 * pAttr) {
  int                     r;
  FS_FILE_OBJ           * pFileObj;
  FS_VOLUME             * pVolume;
  const FS_DIRENTRY_POS * pDirEntryPos;

  if (pFile->InUse == 0u) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;      // Error, the file handle has been invalidated.
  }
  pFileObj = pFile->pFileObj;
  if (pFileObj == NULL) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;      // Error, the file object has been invalidated by a forced unmount operation.
  }
  pVolume = pFileObj->pVolume;
  if (pVolume == NULL) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;      // Error, the file handle has been invalidated.
  }
  pDirEntryPos = &pFileObj->DirEntryPos;
  r = FS_GET_DIRENTRY_INFO_EX(pVolume, pDirEntryPos, pAttr, FS_DIRENTRY_GET_ATTRIBUTES);
  return r;
}

#endif // FS_OS_LOCK_PER_DRIVER == 0

#if FS_OS_LOCK_PER_DRIVER

/*********************************************************************
*
*       _GetFileAttrDL
*
*  Function description
*    Returns attributes of an opened file (with driver locking).
*/
static int _GetFileAttrDL(const FS_FILE * pFile, U8 * pAttr) {
  FS_VOLUME             * pVolume;
  int                     r;
  FS_FILE_OBJ           * pFileObj;
  FS_DEVICE             * pDevice;
  int                     InUse;
  const FS_DIRENTRY_POS * pDirEntryPos;

  pVolume  = NULL;
  FS_LOCK_SYS();
  InUse    = (int)pFile->InUse;
  pFileObj = pFile->pFileObj;
  if (pFileObj != NULL) {
    pVolume = pFileObj->pVolume;
  }
  pDevice = NULL;
  if (pVolume != NULL) {
    pDevice = &pVolume->Partition.Device;
  }
  FS_UNLOCK_SYS();
  if (InUse == 0) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;    // Error, the file handle has been closed.
  }
  if (pVolume == NULL) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;    // Error, the file object has been closed.
  }
  FS_LOCK_DRIVER(pDevice);
  FS_LOCK_SYS();
  if (pFileObj != pFile->pFileObj) {
    InUse = 0;                                // Error, the file object has been invalidated by a forced unmount operation.
  }
  if (pFile->InUse == 0u) {                   // Error, the file handle has been closed by another task.
    InUse = 0;
  }
  FS_UNLOCK_SYS();
  if ((InUse == 0) || (pFileObj == NULL)) {
    r = FS_ERRCODE_INVALID_FILE_HANDLE;       // Error, invalid file handle.
  } else {
    pDirEntryPos = &pFileObj->DirEntryPos;
    r = FS_GET_DIRENTRY_INFO_EX(pVolume, pDirEntryPos, pAttr, FS_DIRENTRY_GET_ATTRIBUTES);
  }
  FS_UNLOCK_DRIVER(pDevice);
  return r;
}

#endif // FS_OS_LOCK_PER_DRIVER

/*********************************************************************
*
*       _SetFileAttrNS
*
*  Function description
*    Internal version of the FS__SetFileAttr() without protection
*    against unexpected resets (i.e. not fail-safe).
*/
static int _SetFileAttrNS(FS_VOLUME * pVolume, const FS_FILE_OBJ * pFileObj, U8 Attr) {
  int                     r;
  const FS_DIRENTRY_POS * pDirEntryPos;

  pDirEntryPos = &pFileObj->DirEntryPos;
  r = FS_SET_DIRENTRY_INFO_EX(pVolume, pDirEntryPos, &Attr, FS_DIRENTRY_SET_ATTRIBUTES);
  return r;
}

#if FS_SUPPORT_JOURNAL

/*********************************************************************
*
*       _SetFileAttrFS
*
*  Function description
*    Internal version of the FS__SetFileAttr() with protection against
*    unexpected reset (i.e. fail-safe operation).
*/
static int _SetFileAttrFS(FS_VOLUME * pVolume, const FS_FILE_OBJ * pFileObj, U8 Attr) {
  int r;
  int Result;

  r = FS__JOURNAL_Begin(pVolume);
  if (r == 0) {
    r = _SetFileAttrNS(pVolume, pFileObj, Attr);
    FS__JOURNAL_SetError(pVolume, r);
    Result = FS__JOURNAL_End(pVolume);
    if (Result != 0) {
      r = Result;
    }
  } else {
    (void)_SetFileAttrNS(pVolume, pFileObj, Attr);      // Perform the operation without journal.
  }
  return r;
}

#endif // FS_SUPPORT_JOURNAL

#if (FS_OS_LOCK_PER_DRIVER == 0)

/*********************************************************************
*
*       _SetFileAttrNL
*
*  Function description
*    Sets attributes of an opened file (without global locking).
*/
static int _SetFileAttrNL(const FS_FILE * pFile, U8 Attr) {
  int           r;
  FS_FILE_OBJ * pFileObj;
  FS_VOLUME   * pVolume;

  if (pFile->InUse == 0u) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;      // Error, the file handle has been invalidated.
  }
  pFileObj = pFile->pFileObj;
  if (pFileObj == NULL) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;      // Error, the file object has been invalidated by a forced unmount operation.
  }
  pVolume = pFileObj->pVolume;
  if (pVolume == NULL) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;      // Error, the file handle has been invalidated.
  }
#if FS_SUPPORT_JOURNAL
  r = _SetFileAttrFS(pVolume, pFileObj, Attr);
#else
  r = _SetFileAttrNS(pVolume, pFileObj, Attr);
#endif // FS_SUPPORT_JOURNAL
  return r;
}

#endif // FS_OS_LOCK_PER_DRIVER == 0

#if FS_OS_LOCK_PER_DRIVER

/*********************************************************************
*
*       _SetFileAttrDL
*
*  Function description
*    Returns attributes of an opened file (with driver locking).
*/
static int _SetFileAttrDL(const FS_FILE * pFile, U8 Attr) {
  FS_VOLUME   * pVolume;
  int           r;
  FS_FILE_OBJ * pFileObj;
  FS_DEVICE   * pDevice;
  int           InUse;

  pVolume  = NULL;
  FS_LOCK_SYS();
  InUse    = (int)pFile->InUse;
  pFileObj = pFile->pFileObj;
  if (pFileObj != NULL) {
    pVolume = pFileObj->pVolume;
  }
  pDevice = NULL;
  if (pVolume != NULL) {
    pDevice = &pVolume->Partition.Device;
  }
  FS_UNLOCK_SYS();
  if (InUse == 0) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;    // Error, the file handle has been closed.
  }
  if (pVolume == NULL) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;    // Error, the file object has been closed.
  }
  FS_LOCK_DRIVER(pDevice);
  FS_LOCK_SYS();
  if (pFileObj != pFile->pFileObj) {
    InUse = 0;                                // Error, the file object has been invalidated by a forced unmount operation.
  }
  if (pFile->InUse == 0u) {                   // Error, the file handle has been closed by another task.
    InUse = 0;
  }
  FS_UNLOCK_SYS();
  if (InUse == 0) {
    r = FS_ERRCODE_INVALID_FILE_HANDLE;       // Error, invalid file handle.
  } else {
#if FS_SUPPORT_JOURNAL
    r = _SetFileAttrFS(pVolume, pFileObj, Attr);
#else
    r = _SetFileAttrNS(pVolume, pFileObj, Attr);
#endif // FS_SUPPORT_JOURNAL
  }
  FS_UNLOCK_DRIVER(pDevice);
  return r;
}

#endif // FS_OS_LOCK_PER_DRIVER

#if (FS_OS_LOCK_PER_DRIVER == 0)

/*********************************************************************
*
*       _GetDirAttrNL
*
*  Function description
*    Returns attributes of an opened directory (without global locking).
*/
static int _GetDirAttrNL(FS_DIR * pDir, U8 * pAttr) {
  int                     r;
  FS_DIR_OBJ            * pDirObj;
  FS_VOLUME             * pVolume;
  const FS_DIRENTRY_POS * pDirEntryPos;

  if (pDir->InUse == 0u) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;      // Error, the directory handle has been invalidated.
  }
  pDirObj = &pDir->DirObj;
  pVolume = pDirObj->pVolume;
  if (pVolume == NULL) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;      // Error, the directory handle has been invalidated.
  }
  pDirEntryPos = &pDirObj->ParentDirPos;
  r = FS_GET_DIRENTRY_INFO_EX(pVolume, pDirEntryPos, pAttr, FS_DIRENTRY_GET_ATTRIBUTES);
  return r;
}

#endif // FS_OS_LOCK_PER_DRIVER == 0

#if FS_OS_LOCK_PER_DRIVER

/*********************************************************************
*
*       _GetDirAttrDL
*
*  Function description
*    Returns attributes of an opened directory (with driver locking).
*/
static int _GetDirAttrDL(FS_DIR * pDir, U8 * pAttr) {
  FS_VOLUME             * pVolume;
  int                     r;
  FS_DIR_OBJ            * pDirObj;
  FS_DEVICE             * pDevice;
  int                     InUse;
  const FS_DIRENTRY_POS * pDirEntryPos;

  FS_LOCK_SYS();
  InUse   = (int)pDir->InUse;
  pDirObj = &pDir->DirObj;
  pVolume = pDirObj->pVolume;
  pDevice = NULL;
  if (pVolume != NULL) {
    pDevice = &pVolume->Partition.Device;
  }
  FS_UNLOCK_SYS();
  if (InUse == 0) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;    // Error, the directory handle has been closed.
  }
  if (pVolume == NULL) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;    // Error, the directory object has been closed.
  }
  FS_LOCK_DRIVER(pDevice);
  FS_LOCK_SYS();
  if (pDir->InUse == 0u) {                    // Error, the directory handle has been closed by another task.
    InUse = 0;
  }
  FS_UNLOCK_SYS();
  if (InUse == 0) {
    r = FS_ERRCODE_INVALID_FILE_HANDLE;       // Error, invalid directory handle.
  } else {
    pDirEntryPos = &pDirObj->ParentDirPos;
    r = FS_GET_DIRENTRY_INFO_EX(pVolume, pDirEntryPos, pAttr, FS_DIRENTRY_GET_ATTRIBUTES);
  }
  FS_UNLOCK_DRIVER(pDevice);
  return r;
}

#endif // FS_OS_LOCK_PER_DRIVER


/*********************************************************************
*
*       _SetDirAttrNS
*
*  Function description
*    Internal version of the FS__SetDirAttr() without protection
*    against unexpected resets (i.e. not fail-safe).
*/
static int _SetDirAttrNS(FS_VOLUME * pVolume, const FS_DIR_OBJ * pDirObj, U8 Attr) {
  int                     r;
  const FS_DIRENTRY_POS * pDirEntryPos;

  pDirEntryPos = &pDirObj->ParentDirPos;
  r = FS_SET_DIRENTRY_INFO_EX(pVolume, pDirEntryPos, &Attr, FS_DIRENTRY_SET_ATTRIBUTES);
  return r;
}

#if FS_SUPPORT_JOURNAL

/*********************************************************************
*
*       _SetDirAttrFS
*
*  Function description
*    Internal version of the FS__SetDirAttr() with protection against
*    unexpected reset (i.e. fail-safe operation).
*/
static int _SetDirAttrFS(FS_VOLUME * pVolume, const FS_DIR_OBJ * pDirObj, U8 Attr) {
  int r;
  int Result;

  r = FS__JOURNAL_Begin(pVolume);
  if (r == 0) {
    r = _SetDirAttrNS(pVolume, pDirObj, Attr);
    FS__JOURNAL_SetError(pVolume, r);
    Result = FS__JOURNAL_End(pVolume);
    if (Result != 0) {
      r = Result;
    }
  } else {
    (void)_SetDirAttrNS(pVolume, pDirObj, Attr);        // Perform the operation without journal.
  }
  return r;
}

#endif // FS_SUPPORT_JOURNAL

#if (FS_OS_LOCK_PER_DRIVER == 0)

/*********************************************************************
*
*       _SetDirAttrNL
*
*  Function description
*    Sets attributes of an opened directory (without global locking).
*/
static int _SetDirAttrNL(FS_DIR * pDir, U8 Attr) {
  int          r;
  FS_DIR_OBJ * pDirObj;
  FS_VOLUME  * pVolume;

  if (pDir->InUse == 0u) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;      // Error, the directory handle has been invalidated.
  }
  pDirObj = &pDir->DirObj;
  pVolume = pDirObj->pVolume;
  if (pVolume == NULL) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;      // Error, the directory handle has been invalidated.
  }
#if FS_SUPPORT_JOURNAL
  r = _SetDirAttrFS(pVolume, pDirObj, Attr);
#else
  r = _SetDirAttrNS(pVolume, pDirObj, Attr);
#endif // FS_SUPPORT_JOURNAL
  return r;
}

#endif // FS_OS_LOCK_PER_DRIVER == 0

#if FS_OS_LOCK_PER_DRIVER

/*********************************************************************
*
*       _SetDirAttrDL
*
*  Function description
*    Returns attributes of an opened directory (with driver locking).
*/
static int _SetDirAttrDL(FS_DIR * pDir, U8 Attr) {
  FS_VOLUME       * pVolume;
  int               r;
  FS_DIR_OBJ      * pDirObj;
  FS_DEVICE       * pDevice;
  int               InUse;

  FS_LOCK_SYS();
  InUse   = (int)pDir->InUse;
  pDirObj = &pDir->DirObj;
  pVolume = pDirObj->pVolume;
  pDevice = NULL;
  if (pVolume != NULL) {
    pDevice = &pVolume->Partition.Device;
  }
  FS_UNLOCK_SYS();
  if (InUse == 0) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;    // Error, the directory handle has been closed.
  }
  if (pVolume == NULL) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;    // Error, the directory object has been closed.
  }
  FS_LOCK_DRIVER(pDevice);
  FS_LOCK_SYS();
  if (pDir->InUse == 0u) {                    // Error, the directory handle has been closed by another task.
    InUse = 0;
  }
  FS_UNLOCK_SYS();
  if (InUse == 0) {
    r = FS_ERRCODE_INVALID_FILE_HANDLE;       // Error, invalid directory handle.
  } else {
#if FS_SUPPORT_JOURNAL
    r = _SetDirAttrFS(pVolume, pDirObj, Attr);
#else
    r = _SetDirAttrNS(pVolume, pDirObj, Attr);
#endif // FS_SUPPORT_JOURNAL
  }
  FS_UNLOCK_DRIVER(pDevice);
  return r;
}

#endif // FS_OS_LOCK_PER_DRIVER

#if FS_SUPPORT_JOURNAL

/*********************************************************************
*
*       _SetDirEntryInfoFS
*
*  Function description
*    Modifies attributes of a directory entry (fail-safe variant).
*/
static int _SetDirEntryInfoFS(FS_VOLUME * pVolume, const char * sName, const U8 * p, int Mask) {
  int r;
  int Result;

  r = FS__JOURNAL_Begin(pVolume);
  if (r == 0) {
    r = FS_SET_DIRENTRY_INFO(pVolume, sName, p, Mask);
    FS__JOURNAL_SetError(pVolume, r);
    Result = FS__JOURNAL_End(pVolume);
    if (Result != 0) {
      r = Result;
    }
  } else {
    (void)FS_SET_DIRENTRY_INFO(pVolume, sName, p, Mask);        // Perform the operation without journal.
  }
  return r;
}

#endif // FS_SUPPORT_JOURNAL

/*********************************************************************
*
*       Public code (internal)
*
**********************************************************************
*/

/*********************************************************************
*
*       FS__SetFileAttributes
*
*  Function description
*    Internal version of FS_SetFileAttributes
*    Sets the attributes of a specified file/directory.
*
*  Parameters
*    sName        Pointer to a string that contains the file/directory name.
*    Attr         Bit-mask of the attributes that have to be set.
*
*  Return value
*    ==0      OK, attributes set.
*    !=0      Error code indicating the failure reason.
*/
int FS__SetFileAttributes(const char * sName, U8 Attr) {
  int          r;
  const char * s;
  FS_VOLUME  * pVolume;
  unsigned     AttrPermitted;

  //
  // Silently discard undefined attributes from the masks.
  //
  AttrPermitted = FS_ATTR_ARCHIVE | FS_ATTR_DIRECTORY | FS_ATTR_HIDDEN | FS_ATTR_READ_ONLY | FS_ATTR_SYSTEM;
  Attr          = (U8)((unsigned)Attr & AttrPermitted);
  //
  // Execute the operation.
  //
  r = FS_ERRCODE_VOLUME_NOT_FOUND;
  pVolume = FS__FindVolumeEx(sName, &s);
  if (pVolume != NULL) {
    r = FS__AutoMount(pVolume);
    switch ((unsigned)r) {
    case FS_MOUNT_RW:
      FS_LOCK_DRIVER(&pVolume->Partition.Device);
#if FS_SUPPORT_JOURNAL
      r = _SetDirEntryInfoFS(pVolume, s, &Attr, FS_DIRENTRY_SET_ATTRIBUTES);
#else
      r = FS_SET_DIRENTRY_INFO(pVolume, s, &Attr, FS_DIRENTRY_SET_ATTRIBUTES);
#endif // FS_SUPPORT_JOURNAL
      FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
      break;
    case FS_MOUNT_RO:
      r = FS_ERRCODE_READ_ONLY_VOLUME;
      break;
    case 0:
      r = FS_ERRCODE_VOLUME_NOT_MOUNTED;
      break;
    default:
      //
      // An error occurred during the mount operation.
      //
      break;
    }
  }
  return r;
}

/*********************************************************************
*
*       FS__GetFileAttributesEx
*
*  Function description
*    Internal version of FS_GetFileAttributes
*    Gets the attributes of a specified file/directory.
*
*  Parameters
*    pVolume    Volume on which the file or directory is located. Cannot be NULL.
*    sName      Partially qualified file or directory that is name without volume name.
*
*  Return value
*    ==0xFF     An error occurred.
*    !=0xFF     Bit-mask containing the attributes of file/directory.
*/
U8 FS__GetFileAttributesEx(FS_VOLUME * pVolume, const char * sName) {
  U8  Attr;
  int r;

  Attr = 0xFF;              // Set to indicate an error.
  r = FS__AutoMount(pVolume);
  switch ((unsigned)r) {
  case FS_MOUNT_RW:
    // through
  case FS_MOUNT_RO:
    FS_LOCK_DRIVER(&pVolume->Partition.Device);
    r = FS_GET_DIRENTRY_INFO(pVolume, sName, &Attr, FS_DIRENTRY_GET_ATTRIBUTES);
    FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
    if (r != 0) {
      Attr = 0xFF;    // Error, could not get the file attributes.
    }
    break;
  default:
    //
    // An error occurred during the mount operation.
    //
    break;
  }
  return Attr;
}

/*********************************************************************
*
*       FS__GetFileAttributes
*
*  Function description
*    Internal version of FS_GetFileAttributes
*    Gets the attributes of a specified file/directory.
*
*  Parameters
*    sName      Pointer to a string that contains the file/directory name.
*
*  Return value
*    ==0xFF     An error occurred.
*    !=0xFF     Bit-mask containing the attributes of file/directory.
*/
U8 FS__GetFileAttributes(const char * sName) {
  U8           Attr;
  const char * s;
  FS_VOLUME  * pVolume;

  Attr = 0xFF;          // Set to indicate an error.
  pVolume = FS__FindVolumeEx(sName, &s);
  if (pVolume != NULL) {
    Attr = FS__GetFileAttributesEx(pVolume, s);
  }
  return Attr;
}

/*********************************************************************
*
*       FS__ModifyFileAttributes
*
*  Function description
*    Internal version of FS_ModifyFileAttributes
*    Sets/clears the attributes of a specified file/directory.
*/
U8 FS__ModifyFileAttributes(const char * sName, U8 AttrSet, U8 AttrClr) {
  U8           AttrOld;
  U8           AttrNew;
  unsigned     v;
  int          r;
  const char * s;
  FS_VOLUME  * pVolume;
  unsigned     AttrPermitted;

  //
  // Silently discard undefined attributes from the masks.
  //
  AttrPermitted = FS_ATTR_ARCHIVE | FS_ATTR_DIRECTORY | FS_ATTR_HIDDEN | FS_ATTR_READ_ONLY | FS_ATTR_SYSTEM;
  AttrSet       = (U8)((unsigned)AttrSet & AttrPermitted);
  AttrClr       = (U8)((unsigned)AttrClr & AttrPermitted);
  AttrOld       = 0xFF;          // Set to indicate an error.
  pVolume       = FS__FindVolumeEx(sName, &s);
  if (pVolume != NULL) {
    r = FS__AutoMount(pVolume);
    switch ((unsigned)r) {
    case FS_MOUNT_RW:
      FS_LOCK_DRIVER(&pVolume->Partition.Device);
      r = FS_GET_DIRENTRY_INFO(pVolume, s, &AttrOld, FS_DIRENTRY_GET_ATTRIBUTES);
      if (r == 0) {
        v  =  AttrOld;
        v |=  (unsigned)AttrSet;
        v &= ~((unsigned)AttrClr);
        AttrNew = (U8)v;
#if FS_SUPPORT_JOURNAL
        r = _SetDirEntryInfoFS(pVolume, s, &AttrNew, FS_DIRENTRY_SET_ATTRIBUTES);
#else
        r = FS_SET_DIRENTRY_INFO(pVolume, s, &AttrNew, FS_DIRENTRY_SET_ATTRIBUTES);
#endif // FS_SUPPORT_JOURNAL
      }
      FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
      if (r != 0) {
        AttrOld = 0xFF;
      }
      break;
    default:
      //
      // An error occurred during the mount operation.
      //
      break;
    }
  }
  return AttrOld;
}

/*********************************************************************
*
*       FS__GetFileInfo
*
*  Function description
*    Internal version of FS_GetFileInfo
*    Returns information about a file or directory.
*
*  Parameters
*    sName    Pointer to a name of the file or directory.
*    pInfo    [OUT] Information about file or directory.
*
*  Return value
*    ==0      OK, information returned.
*    !=0      An error occurred.
*/
int FS__GetFileInfo(const char * sName, FS_FILE_INFO * pInfo) {
  int          r;
  int          Result;
  const char * s;
  FS_VOLUME  * pVolume;

  r = FS_ERRCODE_VOLUME_NOT_FOUND;                  // Set to indicate an error.
  pVolume = FS__FindVolumeEx(sName, &s);
  if (pVolume != NULL) {
    r = FS__AutoMount(pVolume);
    switch ((unsigned)r) {
    case FS_MOUNT_RO:
      // thru
    case FS_MOUNT_RW:
      if (pInfo == NULL) {
        r = FS_ERRCODE_INVALID_PARA;                // Error, return parameter not specified.
      } else {
        r = 0;
        FS_LOCK_DRIVER(&pVolume->Partition.Device);
        FS_MEMSET(pInfo, 0, sizeof(FS_FILE_INFO));
        Result = FS_GET_DIRENTRY_INFO(pVolume, s, &pInfo->Attributes, FS_DIRENTRY_GET_ATTRIBUTES);
        if (Result != 0) {
          r = Result;                               // Error, could not get attributes.
        }
        Result = FS_GET_DIRENTRY_INFO(pVolume, s, &pInfo->CreationTime, FS_DIRENTRY_GET_TIMESTAMP_CREATE);
        if (Result == FS_ERRCODE_INVALID_PARA) {    // The root directory has no creation time
          Result = 0;
        }
        if (Result != 0) {
          r = Result;                               // Error, could not get creation time.
        }
        Result = FS_GET_DIRENTRY_INFO(pVolume, s, &pInfo->LastAccessTime, FS_DIRENTRY_GET_TIMESTAMP_ACCESS);
        if (Result == FS_ERRCODE_INVALID_PARA) {    // The root directory has no access time.
          Result = 0;
        }
        if (Result != 0) {
          r = Result;                               // Error, could not get last access time.
        }
        Result = FS_GET_DIRENTRY_INFO(pVolume, s, &pInfo->LastWriteTime, FS_DIRENTRY_GET_TIMESTAMP_MODIFY);
        if (Result == FS_ERRCODE_INVALID_PARA) {    // The root directory has no modification time.
          Result = 0;
        }
        if (Result != 0) {
          r = Result;                               // Error, could not get last access time.
        }
        Result = FS_GET_DIRENTRY_INFO(pVolume, s, &pInfo->FileSize, FS_DIRENTRY_GET_SIZE);
        if (Result == FS_ERRCODE_INVALID_PARA) {    // The root directory has no size information.
          Result = 0;
        }
        if (Result != 0) {
          r = Result;                               // Error, could not get file size.
        }
        FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
      }
      break;
    case 0:
      r = FS_ERRCODE_VOLUME_NOT_MOUNTED;
      break;
    default:
      //
      // An error occurred during the mount operation.
      //
      break;
    }
  }
  return r;
}

/*********************************************************************
*
*       FS__GetFileAttr
*
*  Function description
*    Returns the attributes of an opened file.
*
*  Parameters
*    pFile      Handle that identifies the opened file.
*    pAttr      [OUT] Read file attributes.
*
*  Return value
*    ==0      OK, attributes returned.
*    !=0      Error code indicating the failure reason.
*/
int FS__GetFileAttr(const FS_FILE * pFile, U8 * pAttr) {
  int r;

  if ((pFile == NULL) || (pAttr == NULL)) {
    return FS_ERRCODE_INVALID_PARA;             // Error, invalid parameters.
  }
#if FS_OS_LOCK_PER_DRIVER
  r = _GetFileAttrDL(pFile, pAttr);
#else
  r = _GetFileAttrNL(pFile, pAttr);
#endif // FS_OS_LOCK_PER_DRIVER
  return r;
}

/*********************************************************************
*
*       FS__SetFileAttr
*
*  Function description
*    Sets the attributes of an opened file.
*
*  Parameters
*    pFile      Handle that identifies the opened file.
*    Attr       Attributes to be set.
*
*  Return value
*    ==0      OK, attributes set.
*    !=0      Error code indicating the failure reason.
*/
int FS__SetFileAttr(const FS_FILE * pFile, U8 Attr) {
  int r;

  if (pFile == NULL) {
    return FS_ERRCODE_INVALID_PARA;
  }
#if FS_OS_LOCK_PER_DRIVER
  r = _SetFileAttrDL(pFile, Attr);
#else
  r = _SetFileAttrNL(pFile, Attr);
#endif // FS_OS_LOCK_PER_DRIVER
  return r;
}

/*********************************************************************
*
*       FS__GetDirAttr
*
*  Function description
*    Returns the attributes of an opened directory.
*
*  Parameters
*    pDir       Handle that identifies the opened directory.
*    pAttr      [OUT] Read file attributes.
*
*  Return value
*    ==0      OK, attributes returned.
*    !=0      Error code indicating the failure reason.
*/
int FS__GetDirAttr(FS_DIR * pDir, U8 * pAttr) {
  int r;

  if ((pDir == NULL) || (pAttr == NULL)) {
    return FS_ERRCODE_INVALID_PARA;             // Error, invalid parameters.
  }
#if FS_OS_LOCK_PER_DRIVER
  r = _GetDirAttrDL(pDir, pAttr);
#else
  r = _GetDirAttrNL(pDir, pAttr);
#endif // FS_OS_LOCK_PER_DRIVER
  return r;
}

/*********************************************************************
*
*       FS__SetDirAttr
*
*  Function description
*    Sets the attributes of an opened directory.
*
*  Parameters
*    pDir       Handle that identifies the opened directory.
*    Attr       Attributes to be set.
*
*  Return value
*    ==0      OK, attributes set.
*    !=0      Error code indicating the failure reason.
*/
int FS__SetDirAttr(FS_DIR * pDir, U8 Attr) {
  int r;

  if (pDir == NULL) {
    return FS_ERRCODE_INVALID_PARA;
  }
#if FS_OS_LOCK_PER_DRIVER
  r = _SetDirAttrDL(pDir, Attr);
#else
  r = _SetDirAttrNL(pDir, Attr);
#endif // FS_OS_LOCK_PER_DRIVER
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
*       FS_SetFileAttributes
*
*  Function description
*    Modifies all the attributes of a file or directory.
*
*  Parameters
*    sName        Pointer to a name of the file/directory.
*    AttrMask     Bit mask of the attributes to be set.
*
*  Return value
*    ==0        OK, attributes have been set.
*    !=0        Error code indicating the failure reason.
*
*  Additional information
*    The FS_ATTR_DIRECTORY attribute cannot be modified using this
*    function. The value of AttrMask parameter is an or-combination
*    of the following attributes: FS_ATTR_READ_ONLY, FS_ATTR_HIDDEN,
*    FS_ATTR_SYSTEM, FS_ATTR_ARCHIVE, or FS_ATTR_DIRECTORY.
*    The attributes that are not set in AttrMask are set to 0.
*/
int FS_SetFileAttributes(const char * sName, U8 AttrMask) {
  int  r;

  FS_LOCK();
  r = FS__SetFileAttributes(sName, AttrMask);
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_GetFileAttributes
*
*  Function description
*    Queries the attributes of a file or directory.
*
*  Parameters
*    sName      Name of the file or directory to be queried.
*
*  Return value
*    ==0xFF       An error occurred.
*    !=0xFF       Bit mask containing the attributes of file
*                 or directory.
*
*  Additional information
*    The return value is an or-combination of the following attributes:
*    FS_ATTR_READ_ONLY, FS_ATTR_HIDDEN, FS_ATTR_SYSTEM,
*    FS_ATTR_ARCHIVE, or FS_ATTR_DIRECTORY.
*/
U8 FS_GetFileAttributes(const char * sName) {
  U8 Attributes;

  FS_LOCK();
  Attributes = FS__GetFileAttributes(sName);
  FS_UNLOCK();
  return Attributes;
}

/*********************************************************************
*
*       FS_ModifyFileAttributes
*
*  Function description
*    Sets / clears the attributes of a file or directory.
*
*  Parameters
*    sName      Name of the file or directory.
*    SetMask    Bit mask of the attributes to be set.
*    ClrMask    Bit mask of the attributes to be cleared.
*
*  Return value
*    ==0xFF     An error occurred.
*    !=0xFF     Bit mask containing the old attributes of the file
*               or directory.
*
*  Additional information
*    This function can be used to set and clear at the same time the
*    attributes of a file or directory. The FS_ATTR_DIRECTORY attribute
*    cannot be modified using this function.
*
*    The return value is an or-combination of the following attributes:
*    FS_ATTR_READ_ONLY, FS_ATTR_HIDDEN, FS_ATTR_SYSTEM,
*    FS_ATTR_ARCHIVE, or FS_ATTR_DIRECTORY.
*
*    The attributes that are specified in the SetMask are set to 1
*    while the attributes that are specified in the ClrMask are set to 0.
*    SetMask and ClrMask values are an or-combination of the following
*    attributes: FS_ATTR_READ_ONLY, FS_ATTR_HIDDEN, FS_ATTR_SYSTEM,
*    or FS_ATTR_ARCHIVE. The attributes that are not specified nor
*    in SetMask either in ClrMask are not modified.
*/
U8 FS_ModifyFileAttributes(const char * sName, U8 SetMask, U8 ClrMask) {
  U8 Attributes;

  FS_LOCK();
  Attributes = FS__ModifyFileAttributes(sName, SetMask, ClrMask);
  FS_UNLOCK();
  return Attributes;
}

/*********************************************************************
*
*       FS_GetFileInfo
*
*  Function description
*    Returns information about a file or directory.
*
*  Parameters
*    sName        Name of the file or directory.
*    pInfo        [OUT] Information about file or directory.
*
*  Return value
*    ==0        OK, information returned.
*    !=0        An error occurred.
*
*  Additional information
*    The function returns information about the attributes, size and
*    time stamps of the specified file or directory. For more
*    information refer to FS_FILE_INFO.
*/
int FS_GetFileInfo(const char * sName, FS_FILE_INFO * pInfo) {
  int r;

  FS_LOCK();
  r = FS__GetFileInfo(sName, pInfo);
  FS_UNLOCK();
  return r;
}

/*************************** End of file ****************************/
