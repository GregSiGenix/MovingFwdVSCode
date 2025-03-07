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
File        : FS_Read.c
Purpose     : Implementation of FS_Read
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#include <stdio.h>
#include "FS_Int.h"

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _ReadDataNL
*
*  Function description
*    Reads data from a file.
*
*  Parameters
*    pFile        Pointer to an opened file handle.
*    pData        Pointer to a buffer to receive the data.
*    NumBytes     Number of bytes to be read.
*
*  Return value
*    Number of bytes read.
*/
static U32 _ReadDataNL(FS_FILE * pFile, void * pData, U32 NumBytes) {
  U32 NumBytesRead;

#if FS_SUPPORT_FILE_BUFFER
  {
    int r;

    NumBytesRead = 0;                               // Set to indicate error.
    //
    // Clean the buffers of the other file handles that 
    // are used to access the file we are reading from.
    //
    r = FS__FB_Sync(pFile);
    if (r == 0) {
      r = FS__FB_Read(pFile, pData, NumBytes);
    }
    if (r < 0) {
      if (pFile->Error == 0) {
        pFile->Error = (I16)r;                      // Error, could not read or write data.
      }
    } else {
      NumBytesRead  = (U32)r;
      NumBytes     -= NumBytesRead;
      if (NumBytes != 0u) {
        NumBytesRead += FS_FILE_READ(pFile, pData, NumBytes);
      }
    }
  }
#else
  NumBytesRead = FS_FILE_READ(pFile, pData, NumBytes);
#endif // FS_SUPPORT_FILE_BUFFER
  return NumBytesRead;
}

/*********************************************************************
*
*       _ReadLineNL
*
*  Function description
*    Reads a line of text from file.
*
*  Parameters
*    pFile        Handle to opened file. It cannot be NULL.
*    sData        [OUT] Buffer that receives the data read from file. It cannot be NULL.
*    SizeOfData   Number of bytes in sData. It cannot be 0.
*
*  Return value
*    ==0        OK, line read successfully.
*    !=0        Error code indicating the failure reason.
*/
static int _ReadLineNL(FS_FILE * pFile, char * sData, unsigned SizeOfData) {
  int            r;
  unsigned       NumBytesStored;
  unsigned       BytesPerSector;
  FS_FILE_SIZE   FilePos;
  FS_FILE_SIZE   FileSize;
  U32            NumBytesToRead;
  U32            NumBytesRead;
  unsigned       NumBytesAvail;
  char         * s;
  char           c;
  int            IsLFExpected;
  int            IsLineEnding;
  FS_FILE_OBJ  * pFileObj;

  r              = 0;                                         // Set to indicate success.
  NumBytesStored = 0;
  pFileObj       = pFile->pFileObj;
  BytesPerSector = pFileObj->pVolume->FSInfo.Info.BytesPerSector;
#if FS_SUPPORT_FILE_BUFFER
  FileSize       = FS__FB_GetFileSize(pFile);
#else
  FileSize       = pFile->pFileObj->Size;
#endif // FS_SUPPORT_FILE_BUFFER
  FilePos        = pFile->FilePos;
  --SizeOfData;                                               // Reserve space for the 0-terminator.
  if (SizeOfData == 0u) {
    goto Done;                                                // Buffer size too small.
  }
  if (FileSize <= FilePos) {
    r = FS_ERRCODE_EOF;                                       // End of file reached.
    pFile->Error = (I16)r;
    goto Done;
  }
  s             = sData;
  NumBytesAvail = FileSize - FilePos;                         // Make sure that we do not try to read more bytes than available in the file.
  NumBytesAvail = SEGGER_MIN(NumBytesAvail, SizeOfData);
  IsLFExpected  = 0;
  IsLineEnding  = 0;
  //
  // We read the data directly to buffer and then check for a line ending.
  // The number of bytes read at once is limited to a logical sector boundary
  // in order to prevent that we read too much data.
  //
  for (;;) {
    FilePos = pFile->FilePos;
    NumBytesToRead = BytesPerSector - (FilePos & (BytesPerSector - 1u));
    NumBytesToRead = SEGGER_MIN(NumBytesToRead, NumBytesAvail);
    NumBytesRead = _ReadDataNL(pFile, s, NumBytesToRead);
    if (NumBytesRead == 0u) {
      break;                                                  // No more data available in the file.
    }
    NumBytesAvail -= NumBytesRead;
    //
    // Check for a line ending.
    //
    for (;;) {
      c = *s++;
      if (IsLFExpected != 0) {
        if (c == '\n') {
          --NumBytesRead;
          ++NumBytesStored;                                   // Windows line ending.
        }
        IsLineEnding = 1;                                     // Line ending found.
        break;
      }
      ++NumBytesStored;
      --NumBytesRead;
      if (c == '\n') {                                        // UNIX line ending.
        IsLineEnding = 1;                                     // Line ending found.
        break;
      }
      if (c == '\r') {                                        // Windows or macOS line ending.
        IsLFExpected = 1;                                     // One additional byte is required to decide the style of line ending.
      }
      if (NumBytesRead == 0u) {
        break;
      }
    }
    if (NumBytesRead != 0u) {
      //
      // Move the file pointer to the first character that was not used.
      //
      FilePos         = pFile->FilePos;
      FilePos        -= NumBytesRead;
      pFile->FilePos  = FilePos;
    }
    if (IsLineEnding != 0) {
      break;                                                  // Line ending found.
    }
  }
Done:
  sData[NumBytesStored] = '\0';                               // Store the 0-terminator.
  return r;
}

/*********************************************************************
*
*       _ReadLine
*
*  Function description
*    Reads a line of text from file.
*
*  Parameters
*    pFile        Handle to opened file. It cannot be NULL.
*    sData        [OUT] Buffer that receives the data read from file. It cannot be NULL.
*    SizeOfData   Number of bytes in sData. It cannot be 0.
*
*  Return value
*    ==0        OK, line read successfully.
*    !=0        Error code indicating the failure reason.
*/
static int _ReadLine(FS_FILE * pFile, char * sData, unsigned SizeOfData) {
  int           r;
  U8            InUse;
  FS_FILE_OBJ * pFileObj;

  //
  // Load file information.
  //
  FS_LOCK_SYS();
  InUse    = pFile->InUse;
  pFileObj = pFile->pFileObj;
  FS_UNLOCK_SYS();
  if ((InUse == 0u) || (pFileObj == NULL)) {
    FS_DEBUG_ERROROUT((FS_MTYPE_API, "API: _ReadLine: File handle closed by application."));
    return 0;
  }
  //
  // Lock driver before performing operation.
  //
  FS_LOCK_DRIVER(&pFileObj->pVolume->Partition.Device);
#if FS_OS_LOCK_PER_DRIVER
  FS_LOCK_SYS();
  if (pFileObj != pFile->pFileObj) {
    InUse = 0;
  }
  if (pFile->InUse == 0u) {
    InUse = 0;
  }
  FS_UNLOCK_SYS();
  if (InUse == 0u) {                                  // Make sure the file is still valid.
    FS_DEBUG_ERROROUT((FS_MTYPE_API, "API: _ReadLine: File handle closed by application."));
    r = FS_ERRCODE_INVALID_USAGE;
  } else
#endif // FS_OS_LOCK_PER_DRIVER
  {
    //
    // All checks and locking operations completed. Perform the operation.
    //
    if ((pFile->AccessFlags & FS_FILE_ACCESS_FLAG_R) == 0u) {
      r            = FS_ERRCODE_WRITE_ONLY_FILE;      // Error, file open mode does not allow read operations.
      pFile->Error = (I16)r;
    } else {
      r = _ReadLineNL(pFile, sData, SizeOfData);
    }
  }
  FS_UNLOCK_DRIVER(&pFileObj->pVolume->Partition.Device);
  return r;
}

/*********************************************************************
*
*       Public code (internal)
*
**********************************************************************
*/

/*********************************************************************
*
*       FS__Read
*
*  Function description
*    Internal version of FS_Read.
*    Reads data from a file.
*
*  Parameters
*    pFile        Pointer to an opened file handle.
*    pData        Pointer to a buffer to receive the data.
*    NumBytes     Number of bytes to be read.
*
*  Return value
*    Number of bytes read.
*/
U32 FS__Read(FS_FILE * pFile, void * pData, U32 NumBytes) {
  U8            InUse;
  U32           NumBytesRead;
  FS_FILE_OBJ * pFileObj;

  if (NumBytes == 0u) {
    return 0;                 // OK, nothing to read.
  }
  if (pFile == NULL) {
    return 0;                 // Error, no pointer to a FS_FILE structure.
  }
  NumBytesRead = 0;
  //
  // Load file information.
  //
  FS_LOCK_SYS();
  InUse    = pFile->InUse;
  pFileObj = pFile->pFileObj;
  FS_UNLOCK_SYS();
  if ((InUse == 0u) || (pFileObj == NULL)) {
    FS_DEBUG_ERROROUT((FS_MTYPE_API, "API: FS__Read: File handle closed by application."));
    return 0;
  }
  //
  // Lock driver before performing operation.
  //
  FS_LOCK_DRIVER(&pFileObj->pVolume->Partition.Device);
  //
  // Multi-tasking environments with per-driver-locking:
  // Make sure that relevant file information has not changed (an other task may have closed the file, unmounted the volume etc.)
  // If it has, no action is performed.
  //
#if FS_OS_LOCK_PER_DRIVER
  FS_LOCK_SYS();
  if (pFileObj != pFile->pFileObj) {
    InUse = 0;
  }
  if (pFile->InUse == 0u) {
    InUse = 0;
  }
  FS_UNLOCK_SYS();
  if (InUse == 0u) {                                  // Let's make sure the file is still valid.
    FS_DEBUG_ERROROUT((FS_MTYPE_API, "API: FS__Read: File handle closed by application."));
  } else
#endif
  //
  // All checks and locking operations completed. Call the File system (FAT/EFS) layer.
  //
  {
    if ((pFile->AccessFlags & FS_FILE_ACCESS_FLAG_R) == 0u) {
      pFile->Error = FS_ERRCODE_WRITE_ONLY_FILE;      // File open mode does not allow read ops.
    } else {
      NumBytesRead = _ReadDataNL(pFile, pData, NumBytes);
    }
  }
  FS_UNLOCK_DRIVER(&pFileObj->pVolume->Partition.Device);
  return NumBytesRead;
}

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_Read
*
*  Function description
*    Reads data from a file.
*
*  Parameters
*    pFile        Handle to an opened file. It cannot be NULL.
*    pData        Buffer to receive the read data.
*    NumBytes     Number of bytes to be read.
*
*  Return value
*    Number of bytes read.
*
*  Additional information
*    The file has to be opened with read permissions.
*    For more information about open modes refer to FS_FOpen().
*
*    The application has to check for possible errors using FS_FError()
*    if the number of bytes actually read is different than the number
*    of bytes requested to be read by the application.
*
*    The data is read from the current position in the file that is
*    indicated by the file pointer. FS_Read() moves the file pointer
*    forward by the number of bytes successfully read.
*/
U32 FS_Read(FS_FILE * pFile, void * pData, U32 NumBytes) {
  U32 NumBytesRead;

  FS_LOCK();
  FS_PROFILE_CALL_U32x3(FS_EVTID_READ, SEGGER_PTR2ADDR(pFile), SEGGER_PTR2ADDR(pData), NumBytes);
  NumBytesRead = FS__Read(pFile, pData, NumBytes);
  FS_PROFILE_END_CALL_U32(FS_EVTID_READ, NumBytesRead);
  FS_UNLOCK();
  return NumBytesRead;
}

/*********************************************************************
*
*       FS_FRead
*
*  Function description
*    Reads data from file.
*
*  Parameters
*    pData      Buffer that receives the data read from file.
*    ItemSize   Size of one item to be read from file (in bytes).
*    NumItems   Number of items to be read from the file.
*    pFile      Handle to opened file. It cannot be NULL.
*
*  Return value
*    Number of items read.
*
*  Additional information
*    The file has to be opened with read permissions.
*    For more information about open modes refer to FS_FOpen().
*
*    The application has to check for possible errors using FS_FError()
*    if the number of items actually read is different than the number
*    of items requested to be read by the application.
*
*    The data is read from the current position in the file that is
*    indicated by the file pointer. FS_FRead() moves the file pointer
*    forward by the number of bytes successfully read.
*/
U32 FS_FRead(void * pData, U32 ItemSize, U32 NumItems, FS_FILE * pFile) {
  U32 NumBytesRead;
  U32 NumBytes;

  //
  // Validate parameters.
  //
  if (ItemSize == 0u)  {
    return 0;             // Return here to avoid dividing by zero at the end of the function.
  }
  //
  // Perform the operation.
  //
  FS_LOCK();
  NumBytes = NumItems * ItemSize;
  NumBytesRead = FS__Read(pFile, pData, NumBytes);
  FS_UNLOCK();
  return (NumBytesRead / ItemSize);
}

/*********************************************************************
*
*       FS_FGets
*
*  Function description
*    Reads a line of text from file.
*
*  Parameters
*    sData        [OUT] Buffer that receives the data read from file. It cannot be NULL.
*    SizeOfData   Number of bytes in sData. It cannot be 0.
*    pFile        Handle to opened file. It cannot be NULL.
*
*  Return value
*    !=NULL     OK, data read successfully. The returned value is sData buffer.
*    ==NULL     An error occurred.
*
*  Additional information
*    This function starts reading from the current position in the file
*    and advances the current file position by the number of bytes read.
*
*    FS_FGets() returns when either a line terminator is read from file
*    and stored to sData, SizeOfData - 1 bytes are stored to sData
*    or the end of file is reached. The data stored to sData is 0-terminated.
*
*    A line terminator can be either a single Line Feed character (0x0A),
*    a single Carriage Return character (0x0D) or a Carriage Return
*    and Line Feed character sequence (0x0D 0x0A).
*
*    The file to read from has to be opened with read permissions.
*    For more information about open modes refer to FS_FOpen().
*
*    The application can check for the actual error using FS_FError().
*/
char * FS_FGets(char * sData, int SizeOfData, FS_FILE * pFile) {
  int r;

  //
  // Validate parameters.
  //
  if (pFile == NULL) {
    return NULL;                // Error, invalid file pointer.
  }
  if (sData == NULL) {
    pFile->Error = FS_ERRCODE_INVALID_PARA;
    return NULL;                // Error, invalid buffer.
  }
  if (SizeOfData == 0) {
    pFile->Error = FS_ERRCODE_INVALID_PARA;
    return NULL;                // Error, invalid buffer.
  }
  //
  // Perform the operation.
  //
  FS_LOCK();
  r = _ReadLine(pFile, sData, (unsigned)SizeOfData);
  if (r != 0) {
    sData = NULL;               // Error, could not read data.
  }
  FS_UNLOCK();
  return sData;
}

/*************************** End of file ****************************/
