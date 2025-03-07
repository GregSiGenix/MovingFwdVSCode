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
File        : FS_SetEndOfFile.c
Purpose     : Implementation of FS_SetEndOfFile
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

/*********************************************************************
*
*       _SetEndOfFileNS
*
*  Function description
*    Internal version of the FS__SetEndOfFile() without protection
*    against unexpected resets (i.e. not fail-safe).
*/
static int _SetEndOfFileNS(FS_FILE * pFile) {
  int r;

#if FS_SUPPORT_FILE_BUFFER
  r = FS__FB_Clean(pFile);
  if (r == 0) {
    FS__FB_SetFileSize(pFile);
    r = FS_SET_END_OF_FILE(pFile);
  }
#else
  r = FS_SET_END_OF_FILE(pFile);
#endif // FS_SUPPORT_FILE_BUFFER
  return r;
}

#if FS_SUPPORT_JOURNAL

/*********************************************************************
*
*       _SetEndOfFileFS
*
*  Function description
*    Internal version of the FS__SetEndOfFile() with protection against
*    unexpected reset (i.e. fail-safe operation).
*/
static int _SetEndOfFileFS(FS_VOLUME * pVolume, FS_FILE * pFile) {
  int r;
  int Result;

  r = FS__JOURNAL_Begin(pVolume);
  if (r == 0) {
    r = _SetEndOfFileNS(pFile);
    FS__JOURNAL_SetError(pVolume, r);
    Result = FS__JOURNAL_End(pVolume);
    if (Result != 0) {
      r = Result;
    }
  } else {
    (void)_SetEndOfFileNS(pFile);         // Perform the operation without journaling.
  }
  return r;
}

#endif // FS_SUPPORT_JOURNAL

#if FS_OS_LOCK_PER_DRIVER

/*********************************************************************
*
*       _SetEndOfFileDL
*
*  Function description
*    Internal version of the FS__SetEndOfFile() with driver locking.
*/
static int _SetEndOfFileDL(FS_FILE * pFile) {
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
    r = _SetEndOfFileFS(pVolume, pFile);
#else
    r = _SetEndOfFileNS(pFile);
#endif // FS_SUPPORT_JOURNAL
  }
  FS_UNLOCK_DRIVER(pDevice);
  return r;
}

#else

/*********************************************************************
*
*       _SetEndOfFileNL
*
*  Function description
*    Internal version of the FS__SetEndOfFile() without any locking.
*/
static int _SetEndOfFileNL(FS_FILE * pFile) {
  FS_FILE_OBJ * pFileObj;
  int           r;

  if (pFile->InUse == 0u) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;      // Error, the file handle has been invalidated.
  }
  pFileObj = pFile->pFileObj;
  if (pFileObj == NULL) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;      // Error, the file object has been invalidated by a forced unmount operation.
  }
#if FS_SUPPORT_JOURNAL
  {
    FS_VOLUME * pVolume;

    pVolume = pFile->pFileObj->pVolume;
    r = _SetEndOfFileFS(pVolume, pFile);
  }
#else
  r = _SetEndOfFileNS(pFile);
#endif // FS_SUPPORT_JOURNAL
  return r;
}

#endif // FS_OS_LOCK_PER_DRIVER

/*********************************************************************
*
*       _SetFileSizeNS
*
*  Function description
*    Internal version of the FS__SetFileSize() without protection
*    against unexpected resets (i.e. not fail-safe).
*/
static int _SetFileSizeNS(FS_FILE * pFile, FS_FILE_SIZE NumBytes) {
  int r;

#if FS_SUPPORT_FILE_BUFFER
  r = FS__FB_Clean(pFile);
  if (r == 0) {
    FS_FILE_SIZE FilePos;

    //
    // FS__FB_SetFileSize() uses the file position stored
    // in the file handle, therefore we have to save the file
    // position here and restore it after the function call.
    //
    FilePos = pFile->FilePos;
    pFile->FilePos = NumBytes;
    FS__FB_SetFileSize(pFile);
    pFile->FilePos = FilePos;
    r = FS_FILE_SET_SIZE(pFile, NumBytes);
  }
#else
  r = FS_FILE_SET_SIZE(pFile, NumBytes);
#endif // FS_SUPPORT_FILE_BUFFER
  return r;
}

#if FS_SUPPORT_JOURNAL

/*********************************************************************
*
*       _SetFileSizeFS
*
*  Function description
*    Internal version of the FS__SetFileSize() with protection against
*    unexpected reset (i.e. fail-safe operation).
*/
static int _SetFileSizeFS(FS_VOLUME * pVolume, FS_FILE * pFile, FS_FILE_SIZE NumBytes) {
  int r;
  int Result;

  r = FS__JOURNAL_Begin(pVolume);
  if (r == 0) {
    r = _SetFileSizeNS(pFile, NumBytes);
    FS__JOURNAL_SetError(pVolume, r);
    Result = FS__JOURNAL_End(pVolume);
    if (Result != 0) {
      r = Result;
    }
  } else {
    (void)_SetFileSizeNS(pFile, NumBytes);        // Perform the operation without journaling.
  }
  return r;
}

#endif // FS_SUPPORT_JOURNAL

#if FS_OS_LOCK_PER_DRIVER

/*********************************************************************
*
*       _SetFileSizeDL
*
*  Function description
*    Internal version of the FS__SetFileSize() with driver locking.
*/
static int _SetFileSizeDL(FS_FILE * pFile, FS_FILE_SIZE NumBytes) {
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
    r = _SetFileSizeFS(pVolume, pFile, NumBytes);
#else
    r = _SetFileSizeNS(pFile, NumBytes);
#endif // FS_SUPPORT_JOURNAL
  }
  FS_UNLOCK_DRIVER(pDevice);
  return r;
}

#else

/*********************************************************************
*
*       _SetFileSizeNL
*
*  Function description
*    Internal version of the FS__SetFileSize() without any locking.
*/
static int _SetFileSizeNL(FS_FILE * pFile, FS_FILE_SIZE NumBytes) {
  FS_FILE_OBJ * pFileObj;
  int           r;

  if (pFile->InUse == 0u) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;      // Error, the file handle has been invalidated.
  }
  pFileObj = pFile->pFileObj;
  if (pFileObj == NULL) {
    return FS_ERRCODE_INVALID_FILE_HANDLE;      // Error, the file object has been invalidated by a forced unmount operation.
  }
#if FS_SUPPORT_JOURNAL
  {
    FS_VOLUME * pVolume;

    pVolume = pFile->pFileObj->pVolume;
    r = _SetFileSizeFS(pVolume, pFile, NumBytes);
  }
#else
  r = _SetFileSizeNS(pFile, NumBytes);
#endif // FS_SUPPORT_JOURNAL
  return r;
}

#endif // FS_OS_LOCK_PER_DRIVER

/*********************************************************************
*
*       Public code (internal)
*
**********************************************************************
*/

/*********************************************************************
*
*       FS__SetEndOfFile
*
*  Function description
*    Internal version of the FS_SetEndOfFile()
*
*  Parameters
*    pFile    Pointer to an opened file handle.
*
*  Return value
*    ==0    OK, new file size has been set.
*    !=0    Error code indicating the failure reason.
*/
int FS__SetEndOfFile(FS_FILE * pFile) {
  int r;

  if (pFile == NULL) {
    return FS_ERRCODE_INVALID_PARA;
  }
  if ((pFile->AccessFlags & FS_FILE_ACCESS_FLAGS_ACW) == 0u) {
    return FS_ERRCODE_READ_ONLY_FILE;           // Error, invalid access mode.
  }
#if FS_OS_LOCK_PER_DRIVER
  r = _SetEndOfFileDL(pFile);
#else
  r = _SetEndOfFileNL(pFile);
#endif // FS_OS_LOCK_PER_DRIVER
  return r;
}

/*********************************************************************
*
*       FS__SetFileSize
*
*  Function description
*    Internal version of the FS_SetFileSize().
*
*  Parameters
*    pFile      Pointer to an opened file handle.
*    NumBytes   New file size.
*
*  Return value
*    ==0    OK, new file size has been set.
*    !=0    Error code indicating the failure reason.
*/
int FS__SetFileSize(FS_FILE * pFile, FS_FILE_SIZE NumBytes) {
  int r;

  if (pFile == NULL) {
    return FS_ERRCODE_INVALID_PARA;             // Error, invalid file handle
  }
  if ((pFile->AccessFlags & FS_FILE_ACCESS_FLAGS_ACW) == 0u) {
    return FS_ERRCODE_READ_ONLY_FILE;           // Error, invalid access mode.
  }
#if FS_OS_LOCK_PER_DRIVER
  r = _SetFileSizeDL(pFile, NumBytes);
#else
  r = _SetFileSizeNL(pFile, NumBytes);
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
*       FS_SetEndOfFile
*
*  Function description
*    Sets the file size to current file position.
*
*  Parameters
*    pFile    Handle to opened file.
*
*  Return value
*    ==0    OK, new file size has been set.
*    !=0    Error code indicating the failure reason.
*
*  Additional information
*    The file has to be opened with write permissions.
*    Refer to FS_FOpen() for more information about the file open modes.
*
*    FS_SetEndOfFile() can be used to truncate as well as to extend
*    a file. If the file is extended, the contents of the file between
*    the old end-of-file and the new one are not defined. Extending
*    a file (preallocation) can increase the write performance when
*    the application writes large amounts of data to file as the
*    file system is not required anymore to access the allocation
*    table.
*/
int FS_SetEndOfFile(FS_FILE * pFile) {
  int r;

  FS_LOCK();
  FS_PROFILE_CALL_U32(FS_EVTID_SETENDOFFILE, SEGGER_PTR2ADDR(pFile));
  r = FS__SetEndOfFile(pFile);
  FS_PROFILE_END_CALL_U32(FS_EVTID_SETENDOFFILE, r);
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_SetFileSize
*
*  Function description
*    Sets the file size to the specified number of bytes.
*
*  Parameters
*    pFile      Handle to an opened file.
*    NumBytes   New file size.
*
*  Return value
*    ==0    OK, new file size has been set.
*    !=0    Error code indicating the failure reason.
*
*  Additional information
*    The file has to be opened with write permissions.
*    Refer to FS_FOpen() for more information about the file
*    open modes. FS_SetFileSize() can be used to extend
*    as well as truncate a file. The file position is preserved
*    if the new file size is larger than or equal to the current
*    file position. Else the file position is set to the end of
*    the file.
*/
int FS_SetFileSize(FS_FILE * pFile, U32 NumBytes) {
  int r;

  FS_LOCK();
  r = FS__SetFileSize(pFile, (FS_FILE_SIZE)NumBytes);
  FS_UNLOCK();
  return r;
}

/*************************** End of file ****************************/
