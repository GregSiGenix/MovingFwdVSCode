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
File        : FS_GetFileSize.c
Purpose     : Implementation of FS_GetFileSize
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
*       _GetFileSizeNL
*
*  Function description
*    Internal version of FS__GetFileSize() without locking.
*/
static FS_FILE_SIZE _GetFileSizeNL(const FS_FILE * pFile) {
  FS_FILE_SIZE   NumBytes;
  FS_FILE_OBJ  * pFileObj;

  if (pFile->InUse == 0u) {
    return (FS_FILE_SIZE)~0uL;                  // Error, the file handle has been invalidated.
  }
  pFileObj = pFile->pFileObj;
  if (pFileObj == NULL) {
    return (FS_FILE_SIZE)~0uL;                  // Error, the file object has been invalidated by a forced unmount operation.
  }
#if FS_SUPPORT_FILE_BUFFER
  NumBytes = FS__FB_GetFileSize(pFile);
#else
  NumBytes = pFile->pFileObj->Size;
#endif // FS_SUPPORT_FILE_BUFFER
  return NumBytes;
}

#if FS_OS_LOCK_PER_DRIVER

/*********************************************************************
*
*       _GetFileSizeDL
*
*  Function description
*    Internal version of FS__GetFileSize() with driver locking.
*
*  Additional information
*    A driver locking is required in order to prevent a forced
*    unmount operation from invalidating the file object assigned
*    to the file handle.
*/
static FS_FILE_SIZE _GetFileSizeDL(const FS_FILE * pFile) {
  FS_FILE_SIZE   NumBytes;
  FS_VOLUME    * pVolume;
  FS_FILE_OBJ  * pFileObj;
  FS_DEVICE    * pDevice;
  int            InUse;

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
    return (FS_FILE_SIZE)~0uL;                // Error, the file handle has been closed.
  }
  if (pVolume == NULL) {
    return (FS_FILE_SIZE)~0uL;                // Error, the file object has been closed.
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
    NumBytes = (FS_FILE_SIZE)~0uL;            // Error, invalid file handle.
  } else {
    NumBytes = _GetFileSizeNL(pFile);
  }
  FS_UNLOCK_DRIVER(pDevice);
  return NumBytes;
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
*       FS__GetFileSize
*
*  Function description
*    Internal version of FS_GetFileSize.
*    Returns the size of a file
*
*  Parameters
*    pFile      Pointer to a FS_FILE data structure.
*               The file must have been opened with read or write access.
*
*  Return value
*    0xFFFFFFFF       Indicates failure
*    0 - 0xFFFFFFFE   File size of the given file
*/
FS_FILE_SIZE FS__GetFileSize(const FS_FILE * pFile) {
  FS_FILE_SIZE NumBytes;

  if (pFile == NULL) {
    return (FS_FILE_SIZE)~0uL;             // Error, invalid file handle
  }
#if FS_OS_LOCK_PER_DRIVER
  NumBytes = _GetFileSizeDL(pFile);
#else
  NumBytes = _GetFileSizeNL(pFile);
#endif // FS_OS_LOCK_PER_DRIVER
  return NumBytes;
}

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_GetFileSize
*
*  Function description
*    Returns the size of a file.
*
*  Parameters
*    pFile      Handle to opened file.
*
*  Return value
*    !=0xFFFFFFFF    File size of the given file in bytes.
*    ==0xFFFFFFFF    An error occurred.
*
*  Additional information
*    The file has to be opened with read or write access.
*/
U32 FS_GetFileSize(const FS_FILE * pFile) {
  FS_FILE_SIZE r;

  FS_LOCK();
  r = (U32)FS__GetFileSize(pFile);
  FS_UNLOCK();
  return r;
}

/*************************** End of file ****************************/
