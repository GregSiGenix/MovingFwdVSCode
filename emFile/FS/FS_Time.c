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
File        : FS_Time.c
Purpose     : Implementation of file system's time stamp functions
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#include "FS_ConfDefaults.h"        // FS Configuration
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
*       _GetFileTimeNL
*
*  Function description
*    Returns the time stamp of an opened file (without global locking).
*/
static int _GetFileTimeNL(const FS_FILE * pFile, U32 * pTimeStamp, int TimeIndex) {
  int                     r;
  FS_FILE_OBJ           * pFileObj;
  FS_VOLUME             * pVolume;
  const FS_DIRENTRY_POS * pDirEntryPos;
  int                     TypeMask;

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
  r = 0;
  pDirEntryPos = &pFileObj->DirEntryPos;
  switch (TimeIndex) {
  case FS_FILETIME_CREATE:
    TypeMask = FS_DIRENTRY_GET_TIMESTAMP_CREATE;
    break;
  case FS_FILETIME_ACCESS:
    TypeMask = FS_DIRENTRY_GET_TIMESTAMP_ACCESS;
    break;
  case FS_FILETIME_MODIFY:
    TypeMask = FS_DIRENTRY_GET_TIMESTAMP_MODIFY;
    break;
  default:
    TypeMask = 0;
    r = FS_ERRCODE_INVALID_PARA;        // Error, unknown TimeIndex used
    break;
  }
  if (r == 0) {
    r = FS_GET_DIRENTRY_INFO_EX(pVolume, pDirEntryPos, pTimeStamp, TypeMask);
  }
  return r;
}

#endif // FS_OS_LOCK_PER_DRIVER == 0

#if FS_OS_LOCK_PER_DRIVER

/*********************************************************************
*
*       _GetFileTimeDL
*
*  Function description
*    Returns the time stamp of an opened file (with driver locking).
*/
static int _GetFileTimeDL(const FS_FILE * pFile, U32 * pTimeStamp, int TimeIndex) {
  FS_VOLUME             * pVolume;
  int                     r;
  FS_FILE_OBJ           * pFileObj;
  FS_DEVICE             * pDevice;
  int                     InUse;
  const FS_DIRENTRY_POS * pDirEntryPos;
  int                     TypeMask;

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
    r = 0;
    pDirEntryPos = &pFileObj->DirEntryPos;
    switch (TimeIndex) {
    case FS_FILETIME_CREATE:
      TypeMask = FS_DIRENTRY_GET_TIMESTAMP_CREATE;
      break;
    case FS_FILETIME_ACCESS:
      TypeMask = FS_DIRENTRY_GET_TIMESTAMP_ACCESS;
      break;
    case FS_FILETIME_MODIFY:
      TypeMask = FS_DIRENTRY_GET_TIMESTAMP_MODIFY;
      break;
    default:
      TypeMask = 0;
      r = FS_ERRCODE_INVALID_PARA;        // Error, unknown TimeIndex used
      break;
    }
    if (r == 0) {
      r = FS_GET_DIRENTRY_INFO_EX(pVolume, pDirEntryPos, pTimeStamp, TypeMask);
    }
  }
  FS_UNLOCK_DRIVER(pDevice);
  return r;
}

#endif // FS_OS_LOCK_PER_DRIVER

/*********************************************************************
*
*       _SetFileTimeNS
*
*  Function description
*    Internal version of the FS__SetFileTime() without protection
*    against unexpected resets (i.e. not fail-safe).
*/
static int _SetFileTimeNS(FS_VOLUME * pVolume, const FS_FILE_OBJ * pFileObj, U32 TimeStamp, int TimeIndex) {
  int                     r;
  const FS_DIRENTRY_POS * pDirEntryPos;
  int                     TypeMask;

  r = 0;
  pDirEntryPos = &pFileObj->DirEntryPos;
  switch (TimeIndex) {
  case FS_FILETIME_CREATE:
    TypeMask = FS_DIRENTRY_SET_TIMESTAMP_CREATE;
    break;
  case FS_FILETIME_ACCESS:
    TypeMask = FS_DIRENTRY_SET_TIMESTAMP_ACCESS;
    break;
  case FS_FILETIME_MODIFY:
    TypeMask = FS_DIRENTRY_SET_TIMESTAMP_MODIFY;
    break;
  default:
    TypeMask = 0;
    r = FS_ERRCODE_INVALID_PARA;        // Error, unknown TimeIndex used
    break;
  }
  if (r == 0) {
    r = FS_SET_DIRENTRY_INFO_EX(pVolume, pDirEntryPos, &TimeStamp, TypeMask);
  }
  return r;
}

#if FS_SUPPORT_JOURNAL

/*********************************************************************
*
*       _SetFileTimeFS
*
*  Function description
*    Internal version of the FS__SetFileTime() with protection against
*    unexpected reset (i.e. fail-safe operation).
*/
static int _SetFileTimeFS(FS_VOLUME * pVolume, const FS_FILE_OBJ * pFileObj, U32 TimeStamp, int TimeIndex) {
  int r;
  int Result;

  r = FS__JOURNAL_Begin(pVolume);
  if (r == 0) {
    r = _SetFileTimeNS(pVolume, pFileObj, TimeStamp, TimeIndex);
    FS__JOURNAL_SetError(pVolume, r);
    Result = FS__JOURNAL_End(pVolume);
    if (Result != 0) {
      r = Result;
    }
  } else {
    (void)_SetFileTimeNS(pVolume, pFileObj, TimeStamp, TimeIndex);        // Perform the operation without journaling.
  }
  return r;
}

#endif // FS_SUPPORT_JOURNAL

#if (FS_OS_LOCK_PER_DRIVER == 0)

/*********************************************************************
*
*       _SetFileTimeNL
*
*  Function description
*    Sets time stamps of an opened file (without global locking).
*/
static int _SetFileTimeNL(const FS_FILE * pFile, U32 TimeStamp, int TimeIndex) {
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
  r = _SetFileTimeFS(pVolume, pFileObj, TimeStamp, TimeIndex);
#else
  r = _SetFileTimeNS(pVolume, pFileObj, TimeStamp, TimeIndex);
#endif // FS_SUPPORT_JOURNAL
  return r;
}

#endif // FS_OS_LOCK_PER_DRIVER == 0

#if FS_OS_LOCK_PER_DRIVER

/*********************************************************************
*
*       _SetFileTimeDL
*
*  Function description
*    Returns time stamps of an opened file (with driver locking).
*/
static int _SetFileTimeDL(const FS_FILE * pFile, U32 TimeStamp, int TimeIndex) {
  FS_VOLUME       * pVolume;
  int               r;
  FS_FILE_OBJ     * pFileObj;
  FS_DEVICE       * pDevice;
  int               InUse;

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
    r = _SetFileTimeFS(pVolume, pFileObj, TimeStamp, TimeIndex);
#else
    r = _SetFileTimeNS(pVolume, pFileObj, TimeStamp, TimeIndex);
#endif // FS_SUPPORT_JOURNAL
  }
  FS_UNLOCK_DRIVER(pDevice);
  return r;
}

#endif // FS_OS_LOCK_PER_DRIVER

#if (FS_OS_LOCK_PER_DRIVER == 0)

/*********************************************************************
*
*       _GetDirTimeNL
*
*  Function description
*    Returns the time stamp of an opened directory (without global locking).
*/
static int _GetDirTimeNL(FS_DIR * pDir, U32 * pTimeStamp, int TimeIndex) {
  int                     r;
  FS_DIR_OBJ            * pDirObj;
  FS_VOLUME             * pVolume;
  const FS_DIRENTRY_POS * pDirEntryPos;
  int                     TypeMask;

  if (pDir->InUse == 0u) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;      // Error, the directory handle has been invalidated.
  }
  pDirObj = &pDir->DirObj;
  pVolume = pDirObj->pVolume;
  if (pVolume == NULL) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;      // Error, the directory handle has been invalidated.
  }
  r = 0;
  pDirEntryPos = &pDirObj->ParentDirPos;
  switch (TimeIndex) {
  case FS_FILETIME_CREATE:
    TypeMask = FS_DIRENTRY_GET_TIMESTAMP_CREATE;
    break;
  case FS_FILETIME_ACCESS:
    TypeMask = FS_DIRENTRY_GET_TIMESTAMP_ACCESS;
    break;
  case FS_FILETIME_MODIFY:
    TypeMask = FS_DIRENTRY_GET_TIMESTAMP_MODIFY;
    break;
  default:
    TypeMask = 0;
    r = FS_ERRCODE_INVALID_PARA;        // Error, unknown TimeIndex used
    break;
  }
  if (r == 0) {
    r = FS_GET_DIRENTRY_INFO_EX(pVolume, pDirEntryPos, pTimeStamp, TypeMask);
  }
  return r;
}

#endif // FS_OS_LOCK_PER_DRIVER == 0

#if FS_OS_LOCK_PER_DRIVER

/*********************************************************************
*
*       _GetDirTimeDL
*
*  Function description
*    Returns the time stamp of an opened directory (with driver locking).
*/
static int _GetDirTimeDL(FS_DIR * pDir, U32 * pTimeStamp, int TimeIndex) {
  FS_VOLUME             * pVolume;
  int                     r;
  FS_DIR_OBJ            * pDirObj;
  FS_DEVICE             * pDevice;
  int                     InUse;
  const FS_DIRENTRY_POS * pDirEntryPos;
  int                     TypeMask;

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
    r = FS_ERRCODE_INVALID_FILE_HANDLE;       // Error, invalid file handle.
  } else {
    r = 0;
    pDirEntryPos = &pDirObj->ParentDirPos;
    switch (TimeIndex) {
    case FS_FILETIME_CREATE:
      TypeMask = FS_DIRENTRY_GET_TIMESTAMP_CREATE;
      break;
    case FS_FILETIME_ACCESS:
      TypeMask = FS_DIRENTRY_GET_TIMESTAMP_ACCESS;
      break;
    case FS_FILETIME_MODIFY:
      TypeMask = FS_DIRENTRY_GET_TIMESTAMP_MODIFY;
      break;
    default:
      TypeMask = 0;
      r = FS_ERRCODE_INVALID_PARA;        // Error, unknown TimeIndex used
      break;
    }
    if (r == 0) {
      r = FS_GET_DIRENTRY_INFO_EX(pVolume, pDirEntryPos, pTimeStamp, TypeMask);
    }
  }
  FS_UNLOCK_DRIVER(pDevice);
  return r;
}

#endif // FS_OS_LOCK_PER_DRIVER

/*********************************************************************
*
*       _SetDirTimeNS
*
*  Function description
*    Internal version of the FS__SetDirTime() without protection
*    against unexpected resets (i.e. not fail-safe).
*/
static int _SetDirTimeNS(FS_VOLUME * pVolume, const FS_DIR_OBJ * pDirObj, U32 TimeStamp, int TimeIndex) {
  int                     r;
  const FS_DIRENTRY_POS * pDirEntryPos;
  int                     TypeMask;

  r = 0;
  pDirEntryPos = &pDirObj->ParentDirPos;
  switch (TimeIndex) {
  case FS_FILETIME_CREATE:
    TypeMask = FS_DIRENTRY_SET_TIMESTAMP_CREATE;
    break;
  case FS_FILETIME_ACCESS:
    TypeMask = FS_DIRENTRY_SET_TIMESTAMP_ACCESS;
    break;
  case FS_FILETIME_MODIFY:
    TypeMask = FS_DIRENTRY_SET_TIMESTAMP_MODIFY;
    break;
  default:
    TypeMask = 0;
    r = FS_ERRCODE_INVALID_PARA;        // Error, unknown TimeIndex used
    break;
  }
  if (r == 0) {
    r = FS_SET_DIRENTRY_INFO_EX(pVolume, pDirEntryPos, &TimeStamp, TypeMask);
  }
  return r;
}

#if FS_SUPPORT_JOURNAL

/*********************************************************************
*
*       _SetDirTimeFS
*
*  Function description
*    Internal version of the FS__SetDirTime() with protection against
*    unexpected reset (i.e. fail-safe operation).
*/
static int _SetDirTimeFS(FS_VOLUME * pVolume, const FS_DIR_OBJ * pDirObj, U32 TimeStamp, int TimeIndex) {
  int r;
  int Result;

  r = FS__JOURNAL_Begin(pVolume);
  if (r == 0) {
    r = _SetDirTimeNS(pVolume, pDirObj, TimeStamp, TimeIndex);
    FS__JOURNAL_SetError(pVolume, r);
    Result = FS__JOURNAL_End(pVolume);
    if (Result != 0) {
      r = Result;
    }
  } else {
    (void)_SetDirTimeNS(pVolume, pDirObj, TimeStamp, TimeIndex);        // Perform the operation without journaling.
  }
  return r;
}

#endif // FS_SUPPORT_JOURNAL

#if (FS_OS_LOCK_PER_DRIVER == 0)

/*********************************************************************
*
*       _SetDirTimeNL
*
*  Function description
*    Sets time stamps of an opened directory (without global locking).
*/
static int _SetDirTimeNL(FS_DIR * pDir, U32 TimeStamp, int TimeIndex) {
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
  r = _SetDirTimeFS(pVolume, pDirObj, TimeStamp, TimeIndex);
#else
  r = _SetDirTimeNS(pVolume, pDirObj, TimeStamp, TimeIndex);
#endif // FS_SUPPORT_JOURNAL
  return r;
}

#endif // FS_OS_LOCK_PER_DRIVER == 0

#if FS_OS_LOCK_PER_DRIVER

/*********************************************************************
*
*       _SetDirTimeDL
*
*  Function description
*    Returns time stamps of an opened directory (with driver locking).
*/
static int _SetDirTimeDL(FS_DIR * pDir, U32 TimeStamp, int TimeIndex) {
  FS_VOLUME  * pVolume;
  int          r;
  FS_DIR_OBJ * pDirObj;
  FS_DEVICE  * pDevice;
  int          InUse;

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
  if (pDir->InUse == 0u) {                    // Error, the file handle has been closed by another task.
    InUse = 0;
  }
  FS_UNLOCK_SYS();
  if (InUse == 0) {
    r = FS_ERRCODE_INVALID_FILE_HANDLE;       // Error, invalid file handle.
  } else {
#if FS_SUPPORT_JOURNAL
    r = _SetDirTimeFS(pVolume, pDirObj, TimeStamp, TimeIndex);
#else
    r = _SetDirTimeNS(pVolume, pDirObj, TimeStamp, TimeIndex);
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
static int _SetDirEntryInfoFS(FS_VOLUME * pVolume, const char * sName, const void * p, int Mask) {
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
    (void)FS_SET_DIRENTRY_INFO(pVolume, sName, p, Mask);          // Perform the operation without journaling.
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
*       FS__GetFileTimeEx
*
*  Function description
*    Internal version of FS_GetFileTime.
*    Gets the creation timestamp of a given file/directory name.
*
*  Return value
*    ==0      OK, timestamp returned
*    !=0      Error code indicating the failure reason
*/
int FS__GetFileTimeEx(const char * sName, U32 * pTimeStamp, int TimeIndex) {
  int          r;
  int          TypeMask;
  const char * s;
  FS_VOLUME  * pVolume;

  r = FS_ERRCODE_VOLUME_NOT_FOUND;
  pVolume = FS__FindVolumeEx(sName, &s);
  if (pVolume != NULL) {
    r = FS__AutoMount(pVolume);
    switch ((unsigned)r) {
    case FS_MOUNT_RO:
      // thru
    case FS_MOUNT_RW:
      r = 0;
      switch (TimeIndex) {
      case FS_FILETIME_CREATE:
        TypeMask = FS_DIRENTRY_GET_TIMESTAMP_CREATE;
        break;
      case FS_FILETIME_ACCESS:
        TypeMask = FS_DIRENTRY_GET_TIMESTAMP_ACCESS;
        break;
      case FS_FILETIME_MODIFY:
        TypeMask = FS_DIRENTRY_GET_TIMESTAMP_MODIFY;
        break;
      default:
        TypeMask = 0;
        r = FS_ERRCODE_INVALID_PARA;        // Error, unknown TimeIndex used
        break;
      }
      if (r == 0) {
        if (pTimeStamp == NULL) {
          return FS_ERRCODE_INVALID_PARA;   // Error, output buffer not specified.
        }
        FS_LOCK_DRIVER(&pVolume->Partition.Device);
        r = FS_GET_DIRENTRY_INFO(pVolume, s, pTimeStamp, TypeMask);
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
*       FS__SetFileTimeEx
*
*  Function description
*    Sets the creation timestamp of a given file or directory.
*
*  Return value
*    ==0      OK, timestamp modified.
*    !=0      Error code indicating the failure reason.
*/
int FS__SetFileTimeEx(const char * sName, U32 TimeStamp, int TimeIndex) {
  int          r;
  const char * s;
  int          TypeMask;
  FS_VOLUME  * pVolume;

  r = FS_ERRCODE_VOLUME_NOT_FOUND;
  pVolume = FS__FindVolumeEx(sName, &s);
  if (pVolume != NULL) {
    r = FS__AutoMount(pVolume);
    switch ((unsigned)r) {
    case FS_MOUNT_RW:
      r = 0;
      switch (TimeIndex) {
      case FS_FILETIME_CREATE:
        TypeMask = FS_DIRENTRY_SET_TIMESTAMP_CREATE;
        break;
      case FS_FILETIME_ACCESS:
        TypeMask = FS_DIRENTRY_SET_TIMESTAMP_ACCESS;
        break;
      case FS_FILETIME_MODIFY:
        TypeMask = FS_DIRENTRY_SET_TIMESTAMP_MODIFY;
        break;
      default:
        TypeMask = 0;
        r = FS_ERRCODE_INVALID_PARA;        // Unknown TimeIndex used
        break;
      }
      if (r == 0) {
        FS_LOCK_DRIVER(&pVolume->Partition.Device);
#if FS_SUPPORT_JOURNAL
        r = _SetDirEntryInfoFS(pVolume, s, &TimeStamp, TypeMask);
#else
        r = FS_SET_DIRENTRY_INFO(pVolume, s, &TimeStamp, TypeMask);
#endif // FS_SUPPORT_JOURNAL
        FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
      }
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
*       FS__GetFileTime
*
*  Function description
*    Returns the time stamp of an opened file.
*
*  Parameters
*    pFile        Handle that identifies the opened file.
*    pTimeStamp   [OUT] Time stamp value.
*    TimeIndex    Type of the time stamp to read.
*
*  Return value
*    ==0      OK, time stamp read.
*    !=0      Error code indicating the failure reason.
*/
int FS__GetFileTime(const FS_FILE * pFile, U32 * pTimeStamp, int TimeIndex) {
  int r;

  if ((pFile == NULL) || (pTimeStamp == NULL)) {
    return FS_ERRCODE_INVALID_PARA;             // Error, invalid parameters.
  }
#if FS_OS_LOCK_PER_DRIVER
  r = _GetFileTimeDL(pFile, pTimeStamp, TimeIndex);
#else
  r = _GetFileTimeNL(pFile, pTimeStamp, TimeIndex);
#endif // FS_OS_LOCK_PER_DRIVER
  return r;
}

/*********************************************************************
*
*       FS__SetFileTime
*
*  Function description
*    Sets the time stamp of an opened file.
*
*  Parameters
*    pFile        Handle that identifies the opened file.
*    TimeStamp    Time stamp value.
*    TimeIndex    Type of the time stamp to set.
*
*  Return value
*    ==0      OK, time stamp set.
*    !=0      Error code indicating the failure reason.
*/
int FS__SetFileTime(const FS_FILE * pFile, U32 TimeStamp, int TimeIndex) {
  int r;

  if (pFile == NULL) {
    return FS_ERRCODE_INVALID_PARA;
  }
#if FS_OS_LOCK_PER_DRIVER
  r = _SetFileTimeDL(pFile, TimeStamp, TimeIndex);
#else
  r = _SetFileTimeNL(pFile, TimeStamp, TimeIndex);
#endif // FS_OS_LOCK_PER_DRIVER
  return r;
}

/*********************************************************************
*
*       FS__GetDirTime
*
*  Function description
*    Returns the time stamp of an opened directory.
*
*  Parameters
*    pDir         Handle that identifies the opened directory.
*    pTimeStamp   [OUT] Time stamp value.
*    TimeIndex    Type of the time stamp to read.
*
*  Return value
*    ==0      OK, time stamp read.
*    !=0      Error code indicating the failure reason.
*/
int FS__GetDirTime(FS_DIR * pDir, U32 * pTimeStamp, int TimeIndex) {
  int r;

  if ((pDir == NULL) || (pTimeStamp == NULL)) {
    return FS_ERRCODE_INVALID_PARA;             // Error, invalid parameters.
  }
#if FS_OS_LOCK_PER_DRIVER
  r = _GetDirTimeDL(pDir, pTimeStamp, TimeIndex);
#else
  r = _GetDirTimeNL(pDir, pTimeStamp, TimeIndex);
#endif // FS_OS_LOCK_PER_DRIVER
  return r;
}

/*********************************************************************
*
*       FS__SetDirTime
*
*  Function description
*    Sets the time stamp of an opened directory.
*
*  Parameters
*    pDir         Handle that identifies the opened directory.
*    TimeStamp    Time stamp value.
*    TimeIndex    Type of the time stamp to set.
*
*  Return value
*    ==0      OK, time stamp set.
*    !=0      Error code indicating the failure reason.
*/
int FS__SetDirTime(FS_DIR * pDir, U32 TimeStamp, int TimeIndex) {
  int r;

  if (pDir == NULL) {
    return FS_ERRCODE_INVALID_PARA;
  }
#if FS_OS_LOCK_PER_DRIVER
  r = _SetDirTimeDL(pDir, TimeStamp, TimeIndex);
#else
  r = _SetDirTimeNL(pDir, TimeStamp, TimeIndex);
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
*       FS_GetFileTimeEx
*
*  Function description
*    Gets the timestamp of a file or directory.
*
*  Parameters
*    sName        File or directory name.
*    pTimeStamp   [OUT] Receives the timestamp value.
*    TimeType     Type of timestamp to return.
*                 It can take one of the following values:
*                 * FS_FILETIME_CREATE
*                 * FS_FILETIME_ACCESS
*                 * FS_FILETIME_MODIFY
*
*  Return value
*    ==0      OK, timestamp returned.
*    !=0      Error code indicating the failure reason.
*
*  Additional information
*    Refer to FS_GetFileTime() for a description of the timestamp
*    format. FS_TimeStampToFileTime() can be used to convert the
*    timestamp to a FS_FILETIME structure that can be used to easily
*    process the time information.
*
*    EFS maintains only one filestamp that is updated when the file
*    is created and updated therefore the same timestamp value
*    is returned for all time types.
*/
int FS_GetFileTimeEx(const char * sName, U32 * pTimeStamp, int TimeType) {
  int r;

  FS_LOCK();
  r = FS__GetFileTimeEx(sName, pTimeStamp, TimeType);
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_SetFileTimeEx
*
*  Function description
*    Sets the timestamp of a file or directory.
*
*  Parameters
*    sName        File or directory name.
*    TimeStamp    The value of the timestamp to be set.
*    TimeType     Type of timestamp to be modified.
*                 It can take of the following values:
*                 * FS_FILETIME_CREATE
*                 * FS_FILETIME_ACCESS
*                 * FS_FILETIME_MODIFY
*
*  Return value
*    ==0      OK, timestamp modified.
*    !=0      Error code indicating the failure reason.
*
*  Additional information
*    Refer to FS_GetFileTime() for a description of the timestamp
*    format. FS_FileTimeToTimeStamp() can be used to convert
*    a FS_FILETIME structure to a timestamp that can be used in
*    a call to FS_SetFileTimeEx().
*
*    EFS maintains only one filestamp therefore the TimeType parameter
*    is ignored for files and directories stored on an EFS volume.
*
*    This function is optional. The file system updates automatically
*    the timestamps of file or directories.
*/
int FS_SetFileTimeEx(const char * sName, U32 TimeStamp, int TimeType) {
  int r;

  FS_LOCK();
  r = FS__SetFileTimeEx(sName, TimeStamp, TimeType);
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_GetFileTime
*
*  Function description
*    Returns the creation time of a file or directory.
*
*  Parameters
*    sName        File or directory name.
*    pTimeStamp   [OUT] Receives the timestamp value.
*
*  Return value
*    ==0      OK, timestamp returned.
*    !=0      Error code indicating the failure reason.
*
*  Additional information
*    The date and time encoded in the timestamp using the following format:
*    +-----------+------------------------------------------------------------+
*    | Bit field | Description                                                |
*    +-----------+------------------------------------------------------------+
*    | 0--4      | Second divided by 2                                        |
*    +-----------+------------------------------------------------------------+
*    | 5--10     | Minute (0--59)                                             |
*    +-----------+------------------------------------------------------------+
*    | 11--15    | Hour (0--23)                                               |
*    +-----------+------------------------------------------------------------+
*    | 16--20    | Day of month (1--31)                                       |
*    +-----------+------------------------------------------------------------+
*    | 21--24    | Month (1--12, 1: January, 2: February, etc.)               |
*    +-----------+------------------------------------------------------------+
*    | 25--31    | Year (offset from 1980). Add 1980 to get the current year. |
*    +-----------+------------------------------------------------------------+
*
*    FS_TimeStampToFileTime() can be used to convert the timestamp
*    to a FS_FILETIME structure that can be used to easily process
*    the time information.
*
*    The last modification and the last access timestamps can be read via
*    FS_GetFileTimeEx().
*/
int FS_GetFileTime(const char * sName, U32 * pTimeStamp) {
  int r;

  FS_LOCK();
  r = FS__GetFileTimeEx(sName, pTimeStamp, FS_FILETIME_CREATE);
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_SetFileTime
*
*  Function description
*    Sets the creation time of a file or directory.
*
*  Parameters
*    sName        File or directory name.
*    TimeStamp    The value of the timestamp to be set.
*
*  Return value
*    ==0      OK, timestamp modified.
*    !=0      Error code indicating the failure reason.
*
*  Additional information
*    Refer to FS_GetFileTime() for a description of the timestamp
*    format. FS_FileTimeToTimeStamp() can be used to convert
*    a FS_FILETIME structure to a timestamp that can be used in a
*    call to FS_SetFileTime().
*
*    This function is optional. The file system updates automatically
*    the timestamps of file or directories.
*/
int FS_SetFileTime(const char * sName, U32 TimeStamp) {
  int r;

  FS_LOCK();
  r = FS__SetFileTimeEx(sName, TimeStamp,  FS_FILETIME_CREATE);
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_TimeStampToFileTime
*
*  Function description
*    Converts a timestamp to a broken-down date and time specification.
*
*  Parameters
*    TimeStamp    Timestamp to be converted.
*    pFileTime    [OUT] Converted broken-down date and time.
*                 It cannot be NULL.
*
*  Additional information
*    This function can be used to convert a timestamp as used by the
*    file system a broken-down date and time specification.
*
*    For a description of the timestamp format refer to FS_GetFileTime().
*/
void FS_TimeStampToFileTime(U32 TimeStamp, FS_FILETIME * pFileTime) {
  U16 Date;
  U16 Time;

  Date = (U16)(TimeStamp >> 16);
  Time = (U16)(TimeStamp & 0xFFFFu);
  if (pFileTime != NULL) {
    pFileTime->Year   = (U16) ((Date >> 9) + 1980u);
    pFileTime->Month  = (U16) ((Date & 0x1E0u) >> 5);
    pFileTime->Day    = (U16) ((Date & 0x1Fu));
    pFileTime->Hour   = (U16) (Time >> 11);
    pFileTime->Minute = (U16) ((Time & 0x7E0u) >> 5);
    pFileTime->Second = (U16) ((Time & 0x1Fu) << 1);
  }
}

/*********************************************************************
*
*       FS_FileTimeToTimeStamp
*
*  Function description
*    Converts a broken-down date and time specification to a timestamp.
*
*  Parameters
*    pFileTime    [IN] Broken-down date and time to be converted.
*                 It cannot be NULL.
*    pTimeStamp   [OUT] Converted timestamp. It cannot be NULL.
*
*  Additional information
*    This function can be used to convert a broken-down date and time
*    specification to a timestamp used by the file system.
*    The converted timestamp can be directly passed to
*    FS_SetFileTime() or FS_SetFileTimeEx() to change the timestamps
*    of files and directories.
*
*    For a description of the timestamp format refer to FS_GetFileTime().
*/
void FS_FileTimeToTimeStamp(const FS_FILETIME * pFileTime, U32 * pTimeStamp) {
  U16 Date;
  U16 Time;

  if (pTimeStamp != NULL) {
    Date  = (U16) (((pFileTime->Year - 1980u) << 9) | (pFileTime->Month << 5) | pFileTime->Day);
    Time  = (U16) ((pFileTime->Hour << 11) |  (pFileTime->Minute << 5) |  (pFileTime->Second >> 1));
    *pTimeStamp = ((U32)Date << 16) | ((U32)Time & 0xFFFFu);
  }
}

/*************************** End of file ****************************/
