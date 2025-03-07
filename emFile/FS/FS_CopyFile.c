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
File        : FS_CopyFile.c
Purpose     : Implementation of FS_CopyFile
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
*       Public code (internal)
*
**********************************************************************
*/

/*********************************************************************
*
*       FS__CopyFileEx
*
*  Function description
*    Internal version of FS_CopyFileEx. Copies a file.
*
*  Parameters
*    sFileNameSrc       Name of the source file (fully qualified).
*    sFileNameDest      Name of the destination file (fully qualified).
*    pBuffer            Buffer to temporary store the copied data.
*    NumBytes           Size of the buffer size in bytes.
*
*  Return value
*    ==0      OK, file has been copied.
*    !=0      Error code indicating the failure reason.
*/
int FS__CopyFileEx(const char * sFileNameSrc, const char * sFileNameDest, void * pBuffer, U32 NumBytes) {
  int            r;
  FS_FILE      * pFileSrc;
  FS_FILE      * pFileDest;
  FS_FILE_SIZE   NumBytesInFile;
  U32            TimeStamp;

  pFileSrc  = NULL;
  pFileDest = NULL;
  //
  // Open source file.
  //
  r = FS__OpenFileEx(sFileNameSrc, (U8)FS_FILE_ACCESS_FLAG_R, 0, 0, 1, &pFileSrc);
  if ((r != 0) || (pFileSrc == NULL)) {
    return r;
  }
  //
  // Open destination file.
  //
  r = FS__OpenFileEx(sFileNameDest, (U8)FS_FILE_ACCESS_FLAGS_CW, 1, 1, 0, &pFileDest);
  if ((r != 0) || (pFileDest == NULL)) {
    (void)FS__CloseFile(pFileSrc);
    return r;
  }
  r = FS_ERRCODE_OK;     // Set to indicate success.
  NumBytesInFile = FS__GetFileSize(pFileSrc);
  if (NumBytesInFile != 0u) {
    //
    // Preallocate destination file to optimize the performance of the copy operation.
    //
    (void)FS__FSeek(pFileDest, (FS_FILE_OFF)NumBytesInFile, FS_SEEK_SET);
    (void)FS__SetEndOfFile(pFileDest);
    (void)FS__FSeek(pFileDest, (FS_FILE_OFF)0, FS_SEEK_SET);
    //
    // Now copy the data to the destination file.
    //
    for (;;) {
      U32 NumBytesRead;
      U32 NumBytesWritten;

      NumBytesRead = FS__Read(pFileSrc, pBuffer, NumBytes);
      if (NumBytesRead == 0u) {
        r = pFileSrc->Error;      // Error, could not read from source file.
        break;
      }
      NumBytesWritten  = FS__Write(pFileDest, pBuffer, NumBytesRead);
      NumBytesInFile  -= NumBytesRead;
      if (NumBytesWritten != NumBytesRead) {
        r = pFileDest->Error;     // Not all bytes have been written, maybe the volume is full.
        break;
      }
      if (NumBytesInFile == 0u) {
        break;
      }
    }
  }
  //
  // Close source and destination file
  // and update the directory entry for destination file.
  //
  (void)FS__CloseFile(pFileSrc);
  (void)FS__CloseFile(pFileDest);
  //
  // Since we have copied the file, we need to set the attributes
  // and time stamp of destination file to the same as source file.
  //
  if (r == 0) {
    U8 Attrib;

    (void)FS__GetFileTimeEx(sFileNameSrc, &TimeStamp, FS_FILETIME_CREATE);
    (void)FS__SetFileTimeEx(sFileNameDest,   TimeStamp, FS_FILETIME_CREATE);
    Attrib = FS__GetFileAttributes(sFileNameSrc);
    (void)FS__SetFileAttributes(sFileNameDest, Attrib);
  } else {
    //
    // Error occurred, delete the destination file.
    //
    (void)FS__Remove(sFileNameDest);
  }
  return r;
}

/*********************************************************************
*
*       FS__CopyFile
*
*  Function description
*    Internal version of FS_CopyFile. Copies a file. It uses an internal temporary buffer.
*
*  Parameters
*    sFileNameSrc     Name of the source file (fully qualified).
*    sFileNameDest    Name of the destination file (fully qualified).
*
*  Return value
*    ==0      OK, file has been copied.
*    !=0      Error code indicating the failure reason
*
*  Notes
*    (1) The function allocates FS_BUFFER_SIZE_FILE_COPY bytes on the stack.
*/
int FS__CopyFile(const char * sFileNameSrc, const char * sFileNameDest) {
  U32    aBuffer[FS_BUFFER_SIZE_FILE_COPY / 4];
  void * pBuffer;
  U32    NumBytes;

  pBuffer  = aBuffer;
  NumBytes = sizeof(aBuffer);
  return FS__CopyFileEx(sFileNameSrc, sFileNameDest, pBuffer, NumBytes);
}

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_CopyFileEx
*
*  Function description
*    Copies a file.
*
*  Parameters
*    sFileNameSrc     Name of the source file (fully qualified).
*    sFileNameDest    Name of the destination file (fully qualified).
*    pBuffer          Buffer to temporary store the copied data.
*                     It cannot be NULL.
*    NumBytes         Size of the buffer size in bytes.
*
*  Return value
*    ==0     OK, the file has been copied.
*    !=0     Error code indicating the failure reason.
*
*  Additional information
*    The copy process uses an external buffer provided by the
*    application. FS_CopyFile() overwrites the destination file
*    if it exists. The destination file has to be writable, that
*    is the FS_ATTR_READ_ONLY flag is set to 0.
*
*    The best performance is achieved when the copy buffer is a
*    multiple of sector size and is 32-bit aligned. For example using 
*    a 7 Kbyte copy buffer to copy 512 byte sectors is more efficient 
*    than using a copy buffer of 7.2 Kbyte therefore the function rounds 
*    down the size of the copy buffer to a multiple of sector size.
*    If the application specifies a copy buffer smaller than the
*    sector size a warning is generated in debug builds indicating
*    that the performance of the copy operation is not optimal.
*/
int FS_CopyFileEx(const char * sFileNameSrc, const char * sFileNameDest, void * pBuffer, U32 NumBytes) {
  int r;
  U32 MaxSectorSize;

  FS_LOCK();
  MaxSectorSize = FS_GetMaxSectorSize();
  if (NumBytes > MaxSectorSize) {
    //
    // Get a buffer size that is multiple of MaxSectorSize;
    // MaxSectorSize has to be a power of 2.
    //
    NumBytes &= ~(MaxSectorSize - 1u);
  } else {
    if (NumBytes < MaxSectorSize) {
      FS_DEBUG_WARN((FS_MTYPE_API, "FS_CopyFileEx: Using a buffer of %d bytes is inefficient. Use a buffer size multiple of %d bytes.", NumBytes, MaxSectorSize));
    }
  }
  r = FS__CopyFileEx(sFileNameSrc, sFileNameDest, pBuffer, NumBytes);
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_CopyFile
*
*  Function description
*    Copies a file.
*
*  Parameters
*    sFileNameSrc     Name of the source file (fully qualified).
*    sFileNameDest    Name of the destination file (fully qualified).
*
*  Return value
*    ==0     OK, the file has been copied
*    !=0     Error code indicating the failure reason
*
*  Additional information
*    The copy process uses an internal temporary buffer of 512 bytes
*    that is allocated on the stack. The size of this buffer can be
*    configured via FS_BUFFER_SIZE_FILE_COPY. Alternatively,
*    FS_CopyFileEx() can be used that lets the application specify
*    a copy buffer of an arbitrary size.
*
*    FS_CopyFile() overwrites the destination file if it exists.
*    The destination file has to be writable, that is the
*    FS_ATTR_READ_ONLY flag is set to 0.
*/
int FS_CopyFile(const char * sFileNameSrc, const char * sFileNameDest) {
  int    r;

  FS_LOCK();
  r = FS__CopyFile(sFileNameSrc, sFileNameDest);
  FS_UNLOCK();
  return r;
}

/*************************** End of file ****************************/
