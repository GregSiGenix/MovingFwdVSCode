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
File        : FS_Write.c
Purpose     : Implementation of file write operation
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
*       Local data types
*
**********************************************************************
*/

/*********************************************************************
*
*       WRITE_FORMATTED_CONTEXT
*/
typedef struct {
  FS_FILE * pFile;              // Opened file to write to.
  U32       NumBytes;           // Number of bytes written to file.
  U8        IsError;            // Error indicator.
} WRITE_FORMATTED_CONTEXT;

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

//lint -efunc(818, _cbWriteFormatted) Pointer parameter 'pContext' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory] N:104. Rationale: The prototype of this function is defined by an external API.

/*********************************************************************
*
*       _WriteNL
*
*  Function description
*    Writes data to an opened file. The function does not perform any locking.
*
*  Return value
*    Number of bytes written.
*/
static U32 _WriteNL(FS_FILE * pFile, const void * pData, U32 NumBytes) {
  U32           NumBytesWritten;
#if FS_SUPPORT_JOURNAL
  U8            IsJournalActive;
  U8            IsJournalPresent;
  FS_FILE_OBJ * pFileObj;
  FS_DEVICE   * pDevice;
#endif

#if FS_SUPPORT_JOURNAL
  NumBytesWritten  = 0;
  pFileObj         = pFile->pFileObj;
  pDevice          = &pFileObj->pVolume->Partition.Device;
  IsJournalActive  = pDevice->Data.JournalData.IsActive;
  IsJournalPresent = (U8)FS__JOURNAL_IsPresent(pDevice);
  if ((IsJournalActive != 0u) && (IsJournalPresent != 0u)) {
    FS_VOLUME * pVolume;
    int         r;

    pVolume = pFileObj->pVolume;
    r       = 0;
    do {
      U32 NumBytesWrittenAtOnce;
      U32 NumBytesAtOnce;
      I32 SpaceInJournal;

      //
      // Reserve 2 sectors and about 8% from journal space for management and directory data.
      //
      SpaceInJournal = ((I32)FS__JOURNAL_GetNumFreeSectors(pVolume) - 2) * 15 / 16;
      if (SpaceInJournal <= 0) {
        FS_DEBUG_WARN((FS_MTYPE_JOURNAL, "_WriteNL: Insufficient space in journal."));
        NumBytesWritten += FS_FILE_WRITE(pFile, pData, NumBytes);
        break;
      }
      SpaceInJournal *= (I32)pVolume->FSInfo.Info.BytesPerSector;       // Convert number of sectors into number of bytes.
      NumBytesAtOnce  = SEGGER_MIN((U32)SpaceInJournal, NumBytes);
      r = FS__JOURNAL_Begin(pVolume);
      if (r != 0) {
        FS_DEBUG_WARN((FS_MTYPE_JOURNAL, "_WriteNL: Could not open journal transaction."));
        NumBytesWritten += FS_FILE_WRITE(pFile, pData, NumBytes);       // Perform the operation without journaling.
        break;                                                          // Could not open journal transaction.
      }
      NumBytesWrittenAtOnce = FS_FILE_WRITE(pFile, pData, NumBytesAtOnce);
      r = 0;
      if (NumBytesWrittenAtOnce != NumBytesAtOnce) {
        r = FS_ERRCODE_WRITE_FAILURE;
      }
      FS__JOURNAL_SetError(pVolume, r);
      r = FS__JOURNAL_End(pVolume);
      if (r != 0) {
        break;                                                          // Could not close journal transaction.
      }
      NumBytesWritten += NumBytesWrittenAtOnce;
      if (NumBytesWrittenAtOnce != NumBytesAtOnce) {
        break;                                                          // Error, not all the bytes have been written.
      }
      NumBytes -= NumBytesAtOnce;
      pData     = NumBytesAtOnce + SEGGER_CONSTPTR2PTR(const U8, pData);
    } while (NumBytes != 0u);
    //
    // Update the error code if required.
    //
    if (r != 0) {
      if (pFile->Error == 0) {
        pFile->Error = (I16)r;
      }
    }
  } else
#endif // FS_SUPPORT_JOURNAL
  {
    NumBytesWritten = FS_FILE_WRITE(pFile, pData, NumBytes);            // Execute the file system write function.
  }
  return NumBytesWritten;
}

#if FS_SUPPORT_FILE_BUFFER

/*********************************************************************
*
*       _FB_Clean
*
*  Function description
*    Writes the contents of the file buffer to file.
*
*  Notes
*     (1) The function does not check if the buffer is dirty.
*         This check has to be performed in the calling function.
*/
static int _FB_Clean(FS_FILE * pFile) {
  FS_FILE_BUFFER * pFileBuffer;
  U32              NumBytesInBuffer;
  U32              NumBytesWritten;
  U8             * pData;
  FS_FILE_SIZE     FilePos;
  U8               IsDirty;


  pFileBuffer      = pFile->pBuffer;
  NumBytesInBuffer = pFileBuffer->NumBytesInBuffer;
  IsDirty          = pFileBuffer->IsDirty;
  if (IsDirty != 0u) {
    //
    // Save the current file position and restore it after the write operation.
    //
    FilePos = pFile->FilePos;
    //
    // Write the data from file buffer to file.
    //
    pData = pFileBuffer->pData;
    pFile->FilePos = pFileBuffer->FilePos;
    NumBytesWritten = _WriteNL(pFile, pData, NumBytesInBuffer);
    pFile->FilePos = FilePos;
    if (NumBytesWritten != NumBytesInBuffer) {
      return FS_ERRCODE_WRITE_FAILURE;                    // Error, could not write data to file.
    }
    pFileBuffer->IsDirty          = 0u;
    pFileBuffer->NumBytesInBuffer = 0u;
  }
  return 0;                                               // OK, data written to file.
}

/*********************************************************************
*
*       _FB_Sync
*
*  Function description
*    Synchronizes the contents of the file buffers assigned
*    to file handles that the access same file.
*
*  Additional information
*    The function does nothing if FS_MULTI_HANDLE_SAFE is set to 0 (default).
*/
static int _FB_Sync(FS_FILE * pFile) {
  int r;

  FS_USE_PARA(pFile);
  r = 0;                                                  // Set to indicate success.
#if FS_MULTI_HANDLE_SAFE
  {
    FS_FILE        * pFileToCheck;
    FS_FILE_OBJ    * pFileObjToCheck;
    FS_FILE_OBJ    * pFileObj;
    FS_FILE_BUFFER * pFileBufferToCheck;

    pFileToCheck = FS_Global.pFirstFileHandle;
    while (pFileToCheck != NULL) {
      if (pFileToCheck != pFile) {                        // Skip over the current file handle.
        pFileObj        = pFile->pFileObj;
        pFileObjToCheck = pFileToCheck->pFileObj;
        if (pFileObj == pFileObjToCheck) {                // File handles that access the same file share the same file object.
          pFileBufferToCheck = pFileToCheck->pBuffer;
          if (pFileBufferToCheck != NULL) {
            // TBD: Check the range of data stored to file buffers and invalidate/clean only if necessary.
            if (pFileBufferToCheck->IsDirty != 0u) {
              r = _FB_Clean(pFileToCheck);                // Write file buffer data to file.
            } else {
              pFileBufferToCheck->NumBytesInBuffer = 0;   // Discard data from file buffer.
            }
          }
        }
      }
      pFileToCheck = pFileToCheck->pNext;
    }
  }
#endif
  return r;
}

#endif // FS_SUPPORT_FILE_BUFFER

/*********************************************************************
*
*       _cbWriteFormatted
*
*  Function description
*    Writes data formatted via SEGGER_vsnprintfEx() to a file.
*
*  Parameters
*    pContext     Operation status.
*/
static void _cbWriteFormatted(SEGGER_SNPRINTF_CONTEXT * pContext) {
  U32                       NumBytesWritten;
  U32                       NumBytes;
  WRITE_FORMATTED_CONTEXT * pWriteFormattedContext;
  SEGGER_BUFFER_DESC      * pBufferDesc;
  FS_FILE                 * pFile;
  void                    * pData;

  pBufferDesc            = pContext->pBufferDesc;
  pWriteFormattedContext = SEGGER_PTR2PTR(WRITE_FORMATTED_CONTEXT, pContext->pContext);         // MISRA deviation D:100[a]
  if (pWriteFormattedContext->IsError == 0u) {                // Do not perform any operation if an error occurred.
    pFile    = pWriteFormattedContext->pFile;
    pData    = pBufferDesc->pBuffer;
    NumBytes = (U32)pBufferDesc->Cnt;
    NumBytesWritten = FS__Write(pFile, pData, NumBytes);
    if (NumBytesWritten != NumBytes) {
      pWriteFormattedContext->IsError = 1;                    // Error, could not write data to file.
    } else {
      //
      // Buffer cleaned successfully.
      //
      pWriteFormattedContext->NumBytes += NumBytesWritten;
      pBufferDesc->Cnt                  = 0;
    }
  }
}

/*********************************************************************
*
*       Public code (internal)
*
**********************************************************************
*/

#if FS_SUPPORT_FILE_BUFFER

/*********************************************************************
*
*       FS__FB_GetFileSize
*
*  Function description
*    Returns the actual size of the file taking into account
*    the number of bytes stored in the file buffer.
*
*  Notes
*     (1) The caller has to make sure that the file system is locked.
*/
FS_FILE_SIZE FS__FB_GetFileSize(const FS_FILE * pFile) {
  FS_FILE_SIZE     NumBytes;
  FS_FILE_BUFFER * pFileBuffer;
  unsigned         NumBytesInBuffer;
  FS_FILE_SIZE     FilePos;

  NumBytes = pFile->pFileObj->Size;
  pFileBuffer = pFile->pBuffer;
  if (pFileBuffer != NULL) {
    FilePos          = pFileBuffer->FilePos;
    NumBytesInBuffer = pFileBuffer->NumBytesInBuffer;
    NumBytes         = SEGGER_MAX(NumBytes, FilePos + NumBytesInBuffer);
  }
  return NumBytes;
}

/*********************************************************************
*
*       FS__FB_SetFileSize
*
*  Function description
*    Changes the file size making sure that the file buffer is
*    invalidated if necessary.
*
*  Notes
*     (1) The caller has to make sure that the file system is locked.
*/
void FS__FB_SetFileSize(const FS_FILE * pFile) {
  U32              NumBytes;
  FS_FILE_BUFFER * pFileBuffer;
  unsigned         NumBytesInBuffer;
  FS_FILE_SIZE     FilePos;
  FS_FILE_SIZE     FilePosBuffer;
  FS_FILE_SIZE     FilePosBufferEnd;

  FilePos = pFile->FilePos;
  pFileBuffer = pFile->pBuffer;
  if (pFileBuffer != NULL) {
    FilePosBuffer    = pFileBuffer->FilePos;
    NumBytesInBuffer = pFileBuffer->NumBytesInBuffer;
    FilePosBufferEnd = FilePosBuffer + NumBytesInBuffer;
    if (FilePosBufferEnd > FilePos) {
      NumBytes = (U32)(FilePosBufferEnd - FilePos);
      if (NumBytesInBuffer > NumBytes) {
        NumBytesInBuffer -= NumBytes;
      } else {
        FilePosBuffer    = 0;
        NumBytesInBuffer = 0;
      }
    }
    pFileBuffer->FilePos          = FilePosBuffer;
    pFileBuffer->NumBytesInBuffer = NumBytesInBuffer;
  }
}

/*********************************************************************
*
*       FS__FB_Clean
*
*  Function description
*    Stores data from file buffer to file if the data in the file buffer
*    has been changed.
*
*  Return value
*    ==0      OK, data written to file or nothing to do
*    !=0      An error occurred
*
*  Notes
*     (1) The caller has to make sure that the file system is locked.
*/
int FS__FB_Clean(FS_FILE * pFile) {
  FS_FILE_BUFFER * pFileBuffer;
  unsigned         NumBytesInBuffer;
  int              r;
  int              rSync;

  pFileBuffer = pFile->pBuffer;
  if (pFileBuffer == NULL) {
    return 0;                 // OK, the file buffer is not enabled.
  }
  NumBytesInBuffer = pFileBuffer->NumBytesInBuffer;
  if (NumBytesInBuffer == 0u) {
    return 0;                 // OK, no bytes in buffer.
  }
  r = _FB_Clean(pFile);
  //
  // Clear the buffers of the other file handles which access the same file.
  //
  rSync = _FB_Sync(pFile);
  if (rSync != 0) {
    r = rSync;
  }
  return r;
}

/*********************************************************************
*
*       FS__FB_Sync
*
*  Function description
*    Synchronizes the contents of the file buffers assigned
*    to file handles that the access same file.
*
*  Return value
*    ==0      OK, data synchronized.
*    !=0      An error occurred.
*
*  Notes
*     (1) The caller has to make sure that the file system is locked.
*/
int FS__FB_Sync(FS_FILE * pFile) {
  int r;

  r = _FB_Sync(pFile);
  return r;
}

/*********************************************************************
*
*       FS__FB_Read
*
*  Function description
*    Reads data from the file buffer.
*
*  Return value
*    >=0      Number of bytes read
*    < 0      An error occurred
*
*  Notes
*     (1) The caller has to make sure that the file system is locked.
*/
int FS__FB_Read(FS_FILE * pFile, void * pData, U32 NumBytes) {
  FS_FILE_BUFFER * pFileBuffer;
  unsigned         BufferSize;
  unsigned         NumBytesInBuffer;
  U8             * pDataDest;
  U8             * pDataSrc;
  unsigned         NumBytesToRead;
  int              r;
  FS_FILE_SIZE     FilePosBuffer;
  FS_FILE_SIZE     FilePos;
  U32              Off;
  U32              NumBytesAvail;
  FS_FILE_SIZE     FileSize;
  FS_FILE_SIZE     NumBytesAvailInFile;
  U32              BytesPerSector;
  U32              NumBytesToReadAligned;
  U8               Flags;
  unsigned         NumBytesRead;
  U8             * pDataToRead;
  U32              NumBytesAlignment;
  U32              FilePosAligned;

  pFileBuffer = pFile->pBuffer;
  if (pFileBuffer == NULL) {
    return 0;                 // OK, the file buffer is not enabled.
  }
  BufferSize = pFileBuffer->BufferSize;
  //
  // Do not read with file buffer if the number of bytes to be read
  // is larger than the file buffer size in order to enable 0-copy requests.
  //
  if (NumBytes >= BufferSize) {
    r = _FB_Clean(pFile);
    if (r != 0) {
      return FS_ERRCODE_WRITE_FAILURE;      // Error, could not clean buffer.
    }
    return 0;                               // OK, let FS layer read the data directly from file.
  }
  //
  // Fill local variables.
  //
  NumBytesToRead   = NumBytes;
  pDataDest        = SEGGER_PTR2PTR(U8, pData);
  pDataSrc         = pFileBuffer->pData;
  NumBytesInBuffer = pFileBuffer->NumBytesInBuffer;
  FilePosBuffer    = pFileBuffer->FilePos;
  FilePos          = pFile->FilePos;
  Flags            = pFileBuffer->Flags;
  //
  // Clean the file buffer if the position of the file pointer has been changed.
  //
  if ((FilePos <  FilePosBuffer) ||
      (FilePos > (FilePosBuffer + NumBytesInBuffer))) {
    r = _FB_Clean(pFile);
    if (r != 0) {
      return FS_ERRCODE_WRITE_FAILURE;      // Error, could not clean buffer.
    }
    NumBytesInBuffer = 0;
  }
  if (NumBytesInBuffer != 0u) {
    //
    // Return as much as possible data from the file buffer.
    //
    Off             = (U32)(FilePos - FilePosBuffer);
    NumBytesAvail   = NumBytesInBuffer - Off;
    NumBytesToRead  = SEGGER_MIN(NumBytesAvail, NumBytes);
    FS_MEMCPY(pDataDest, pDataSrc + Off, NumBytesToRead);
    FilePos        += NumBytesToRead;
    pDataDest      += NumBytesToRead;
    NumBytesToRead  = NumBytes - NumBytesToRead;
  }
  if (NumBytesToRead != 0u) {
    //
    // If not all the bytes have been read, fill the buffer with data from storage.
    //
    r = _FB_Clean(pFile);
    if (r != 0) {
      return FS_ERRCODE_WRITE_FAILURE;      // Error, could not clean buffer.
    }
    FileSize          = pFile->pFileObj->Size;
    BytesPerSector    = pFile->pFileObj->pVolume->FSInfo.Info.BytesPerSector;
    NumBytesAlignment = 0;
    if ((Flags & FS_FILE_BUFFER_ALIGNED) != 0u) {
      if (BufferSize >= BytesPerSector) {                                         // It does not make sense to keep the file buffer aligned if it is not larger than or equal to the sector size.
        BufferSize        &= ~(BytesPerSector - 1u);                              // Keep the number of bytes in the file buffer aligned to sector size.
        NumBytesAlignment  = FilePos & (BytesPerSector - 1u);
      }
    }
    if (NumBytesAlignment != 0u) {
      //
      // First, decide where to read from. If the file buffer is sufficiently large
      // to store all the requested data then we move the file pointer back to the
      // first sector boundary. Else we move the file pointer forward to the next sector boundary.
      // In this case, the rest of the data is read directly to user buffer.
      //
      FilePosAligned         = FilePos - NumBytesAlignment;
      NumBytesAvailInFile    = FileSize - FilePosAligned;
      NumBytesToReadAligned  = NumBytesAlignment + NumBytesToRead;
      NumBytesAvail          = (U32)SEGGER_MIN(NumBytesAvailInFile, NumBytesToReadAligned);
      Off                    = NumBytesAlignment;
      if (NumBytesAvail > BufferSize) {
        //
        // We cannot read all the requested data in the buffer therefore we have
        // to read some data directly to the user buffer up to the next sector
        // boundary.
        //
        NumBytesToReadAligned = BytesPerSector - NumBytesAlignment;
        pFile->FilePos = FilePos;
        NumBytesRead = FS_FILE_READ(pFile, pDataDest, NumBytesToReadAligned);
        if (NumBytesRead != NumBytesToReadAligned) {
          return FS_ERRCODE_READ_FAILURE;     // Error, could not read from storage.
        }
        NumBytesToRead      -= NumBytesRead;
        pDataDest           += NumBytesRead;
        FilePosAligned       = FilePos + NumBytesRead;
        NumBytesAvailInFile  = FileSize - FilePosAligned;
        Off                  = 0;
      }
      //
      // Make sure that there are sufficient bytes in the file.
      //
      NumBytesAvail = (U32)SEGGER_MIN(NumBytesAvailInFile, BufferSize);
      if (NumBytesAvail == 0u) {
        return 0;                                                                   // End of file reached. Let the caller handle this condition.
      }
      //
      // Read the data to file buffer.
      //
      pDataToRead    = pFileBuffer->pData;
      pFile->FilePos = FilePosAligned;
      NumBytesInBuffer = FS_FILE_READ(pFile, pDataToRead, NumBytesAvail);
      pFile->FilePos = FilePosAligned;
      if (NumBytesInBuffer != NumBytesAvail) {
        return FS_ERRCODE_READ_FAILURE;
      }
      pFileBuffer->NumBytesInBuffer = NumBytesInBuffer;
      pFileBuffer->FilePos          = FilePosAligned;
    } else {
      NumBytesAvailInFile = FileSize - FilePos;
      NumBytesAvail       = (U32)SEGGER_MIN(NumBytesAvailInFile, BufferSize);
      Off                 = 0;
      if (NumBytesAvail == 0u) {
        return 0;                             // End of file reached. Let the caller report this condition.
      }
      //
      // Read the data from storage to the file buffer.
      //
      pFile->FilePos = FilePos;
      NumBytesInBuffer = FS_FILE_READ(pFile, pDataSrc, NumBytesAvail);
      if (NumBytesInBuffer != NumBytesAvail) {
        return FS_ERRCODE_READ_FAILURE;       // Error, could not read from storage.
      }
      //
      // Update the file buffer information.
      //
      pFileBuffer->NumBytesInBuffer = NumBytesInBuffer;
      pFileBuffer->FilePos          = FilePos;
    }
    //
    // Copy the remaining data to user buffer.
    //
    if (NumBytesInBuffer > Off) {
      NumBytesInBuffer -= Off;
    } else {
      NumBytesInBuffer = 0;
    }
    if (NumBytesToRead > NumBytesInBuffer) {
      NumBytes       -= NumBytesToRead - NumBytesInBuffer;
      NumBytesToRead = NumBytesInBuffer;
      pFile->Error = FS_ERRCODE_EOF;        // End of file reached.
    }
    FS_MEMCPY(pDataDest, pDataSrc + Off, NumBytesToRead);
    FilePos += NumBytesToRead;
  }
  //
  // Update the file pointer.
  //
  pFile->FilePos = FilePos;
  return (int)NumBytes;
}

/*********************************************************************
*
*       FS__FB_Write
*
*  Function description
*    Writes data to file buffer.
*
*  Return value
*    >=0      Number of bytes written
*    < 0      An error occurred
*
*  Notes
*     (1) The caller has to make sure that the file system is locked.
*/
int FS__FB_Write(FS_FILE * pFile, const void * pData, U32 NumBytes) {
  FS_FILE_BUFFER * pFileBuffer;
  U32              BufferSize;
  U32              NumBytesInBuffer;
  U8             * pDataDest;
  const U8       * pDataSrc;
  U32              NumBytesToWrite;
  int              r;
  U8               Flags;
  FS_FILE_SIZE     FilePosBuffer;
  FS_FILE_SIZE     FilePos;
  U32              Off;
  U32              NumBytesAvail;
  FS_FILE_SIZE     NumBytesAvailInFile;
  U32              NumBytesToRead;
  U32              FilePosSaved;
  U32              FilePosToRead;
  U8             * pDataToRead;
  U32              NumBytesRead;
  U32              FileSize;
  U32              BytesPerSector;

  pFileBuffer = pFile->pBuffer;
  if (pFileBuffer == NULL) {
    return 0;                 // OK, the file buffer is not enabled.
  }
  //
  // Fill local variables.
  //
  BufferSize       = pFileBuffer->BufferSize;
  NumBytesToWrite  = NumBytes;
  pDataDest        = pFileBuffer->pData;
  pDataSrc         = SEGGER_CONSTPTR2PTR(const U8, pData);
  NumBytesInBuffer = pFileBuffer->NumBytesInBuffer;
  FilePosBuffer    = pFileBuffer->FilePos;
  Off              = 0;
  FilePos          = pFile->FilePos;
  Flags            = pFile->pBuffer->Flags;
  //
  // Discard the data from the file buffer if the file buffer
  // is working in read mode and the write operation modifies
  // the data that is stored in the file buffer.
  //
  if ((Flags & FS_FILE_BUFFER_WRITE) == 0u) {
    if (((FilePos + NumBytesToWrite) > FilePosBuffer) &&
        (FilePos < (FilePosBuffer + NumBytesInBuffer))) {
      pFileBuffer->NumBytesInBuffer = 0;
    }
    return 0;
  }
  //
  // Do not write with buffer if the number of bytes to be written
  // is larger than the buffer size in order to enable 0-copy requests.
  //
  if (NumBytes >= BufferSize) {
    r = _FB_Clean(pFile);
    if (r != 0) {
      r = FS_ERRCODE_WRITE_FAILURE;         // Error, could not clean buffer.
    }
    //
    // If required, invalidate the data in the buffer.
    //
    if (((FilePos + NumBytesToWrite) > FilePosBuffer) &&
        (FilePos < (FilePosBuffer + NumBytesInBuffer))) {
      pFileBuffer->NumBytesInBuffer = 0;
    }
    return r;                               // OK, let FS layer write the data directly to file.
  }
  //
  // Check if the maximum file size has been reached.
  //
  NumBytesAvailInFile = FS_MAX_FILE_SIZE - FilePos;
  if (NumBytes > NumBytesAvailInFile) {
    return 0;                               // Let the FS layer take care of this error.
  }
  //
  // Clean the file buffer if the position of the file pointer has been changed.
  //
  if ((FilePos <  FilePosBuffer) ||
      (FilePos > (FilePosBuffer + BufferSize))) {
    r = _FB_Clean(pFile);
    if (r != 0) {
      return FS_ERRCODE_WRITE_FAILURE;      // Error, could not clean buffer.
    }
    NumBytesInBuffer = 0;
  } else {
    if (NumBytesInBuffer > 0u) {
      if (FilePos > (FilePosBuffer + NumBytesInBuffer)) {
        U32 NumBytesReq;
        U32 NumBytesGap;

        FileSize    = pFile->pFileObj->Size;
        NumBytesGap = FilePos - (FilePosBuffer + NumBytesInBuffer);
        //
        // Save the current file position and restore it after the write operation.
        //
        FilePosSaved = pFile->FilePos;
        //
        // Calculate the position in the file buffer where to read the data.
        //
        pDataToRead   = (U8 *)pFileBuffer->pData + NumBytesInBuffer;
        FilePosToRead = FilePosBuffer + NumBytesInBuffer;
        //
        // Calculate how many bytes are required to be read in order to fill the gap.
        // We limit the number of bytes to a sector boundary in order to make sure
        // that we write the minimum number of bytes possible when the file buffer
        // is cleared.
        //
        BytesPerSector  = pFile->pFileObj->pVolume->FSInfo.Info.BytesPerSector;
        NumBytesReq     = (FilePos + BytesPerSector - 1u) & ~(BytesPerSector - 1u);   // Round up to the next logical sector boundary.
        NumBytesReq    -= FilePosToRead;                                              // Number of bytes to read in order to reach a logical sector boundary.
        NumBytesAvail   = BufferSize - NumBytesInBuffer;
        NumBytesReq     = SEGGER_MIN(NumBytesReq, NumBytesAvail);                     // Make sure that we do not read more bytes than available in the file buffer.
        NumBytesRead    = 0;
        if (FileSize > FilePosToRead) {
          NumBytesAvail   = FileSize - FilePosToRead;                                 // Number of bytes available in the file.
          NumBytesToRead  = SEGGER_MIN(NumBytesAvail, NumBytesReq);                   // Make sure that we do not read more bytes than available in the file.
          //
          // Read the data from the file and restore the file position after the operation.
          //
          pFile->FilePos = FilePosToRead;
          NumBytesRead = FS_FREAD(pFile, pDataToRead, NumBytesToRead);
          pFile->FilePos = FilePosSaved;
          if (NumBytesRead != NumBytesToRead) {
            return FS_ERRCODE_READ_FAILURE;
          }
        }
        //
        // Update the number of bytes in the buffer with the number of bytes required
        // and not with the number of bytes read from file. The values of the bytes
        // in the gap that were not read from file are undefined.
        //
        if (NumBytesRead < NumBytesReq) {
          NumBytesReq = NumBytesGap;
        }
        NumBytesInBuffer += NumBytesReq;
        pFileBuffer->NumBytesInBuffer = NumBytesInBuffer;
      }
    }
  }
  if ((Flags & FS_FILE_BUFFER_ALIGNED) != 0u) {
    if (NumBytesInBuffer == 0u) {
      BytesPerSector = pFile->pFileObj->pVolume->FSInfo.Info.BytesPerSector;
      if (BufferSize >= BytesPerSector) {
        //
        // Keep the file position aligned to allow 0-copy operations.
        //
        NumBytesToRead = FilePos & (BytesPerSector - 1u);
        if (NumBytesToRead != 0u) {
          FileSize      = pFile->pFileObj->Size;
          FilePosToRead = FilePos - NumBytesToRead;
          if (FileSize > FilePosToRead) {
            NumBytesAvail  = FileSize - FilePosToRead;                                 // Number of bytes available in the file.
            NumBytesToRead = SEGGER_MIN(NumBytesAvail, NumBytesToRead);                // Make sure that we do not read more bytes than available in the file.
            pDataToRead    = pFileBuffer->pData;
            FilePosSaved   = pFile->FilePos;
            pFile->FilePos = FilePosToRead;
            NumBytesRead = FS_FREAD(pFile, pDataToRead, NumBytesToRead);
            pFile->FilePos = FilePosSaved;
            if (NumBytesRead != NumBytesToRead) {
              return FS_ERRCODE_READ_FAILURE;
            }
          }
          NumBytesInBuffer     = NumBytesToRead;
          FilePosBuffer        = FilePosToRead;
          pFileBuffer->FilePos = FilePosBuffer;
        }
      }
    }
  }
  //
  // If not all the data can be stored to file buffer,
  // fill up the file buffer and clean it.
  //
  if (NumBytesInBuffer != 0u) {
    Off           = (U32)(FilePos - FilePosBuffer);
    NumBytesAvail = BufferSize - Off;
    if (NumBytes > NumBytesAvail) {
      NumBytesToWrite = NumBytesAvail;
      if (NumBytesToWrite != 0u) {
        FS_MEMCPY(pDataDest + Off, pDataSrc, NumBytesToWrite);
        FilePos  += NumBytesToWrite;
        pDataSrc += NumBytesToWrite;
        NumBytesAvail = Off + NumBytesToWrite;
        if (NumBytesAvail > NumBytesInBuffer) {
          NumBytesInBuffer = NumBytesAvail;
        }
        //
        // Update the file buffer information so that the clean operation
        // can write the correct number of bytes.
        //
        pFileBuffer->IsDirty          = 1;
        pFileBuffer->NumBytesInBuffer = NumBytesInBuffer;
      }
      r = _FB_Clean(pFile);
      if (r != 0) {
        return FS_ERRCODE_WRITE_FAILURE;      // Error, could not clean buffer.
      }
      NumBytesInBuffer = 0;
      Off              = 0;
      NumBytesToWrite  = NumBytes - NumBytesToWrite;
    }
  }
  //
  // Save the current file position so that FS__FB_Clean()
  // knowns where to write the data in the file.
  //
  if (NumBytesInBuffer == 0u) {
    FilePosBuffer = FilePos;
  }
  //
  // Store data to file buffer.
  //
  FS_MEMCPY(pDataDest + Off, pDataSrc, NumBytesToWrite);
  FilePos += NumBytesToWrite;
  //
  // If required, update the total number of bytes stored to buffer.
  //
  Off += NumBytesToWrite;
  if (Off > NumBytesInBuffer) {
    NumBytesInBuffer = Off;
  }
  //
  // Update file buffer information.
  //
  pFileBuffer->NumBytesInBuffer = NumBytesInBuffer;
  pFileBuffer->IsDirty          = 1;
  pFileBuffer->FilePos          = FilePosBuffer;
  //
  // Update the file pointer.
  //
  pFile->FilePos = FilePos;
  return (int)NumBytes;
}

/*********************************************************************
*
*       FS__SetFileBuffer
*
*  Function description
*    Internal version of FS_SetFileBuffer without global locking.
*/
int FS__SetFileBuffer(FS_FILE * pFile, void * pData, I32 NumBytes, int Flags) {
  FS_FILE_BUFFER * pFileBuffer;
  U8             * pData8;
  PTR_ADDR         NumBytesAlign;

  //
  // Sanity checks.
  //
  FS_DEBUG_ASSERT(FS_MTYPE_API, FS_SIZEOF_FILE_BUFFER_STRUCT == sizeof(FS_FILE_BUFFER));    //lint !e506 !e774
  if (FS_Global.FileBufferSize != 0u) {
    FS_DEBUG_ERROROUT((FS_MTYPE_API, "FS_SetFileBuffer: The file buffer is already allocated by the file system."));
    return FS_ERRCODE_INVALID_USAGE;    // Error, file buffer already allocated by file system.
  }
  if ((pFile == NULL) || (pData == NULL)) {
    return FS_ERRCODE_INVALID_PARA;     // Error, invalid file handle or file buffer.
  }
  if (pFile->InUse == 0u) {
    return FS_ERRCODE_INVALID_PARA;     // Error, the file handle has been closed.
  }
  pData8 = SEGGER_PTR2PTR(U8, pData);
  //
  // Align the pointer to a 32-bit boundary
  //
  if ((SEGGER_PTR2ADDR(pData8) & 3u) != 0u) {
    NumBytesAlign  = 4u - (SEGGER_PTR2ADDR(pData8) & 3u);
    NumBytes      -= (I32)NumBytesAlign;
    pData8        += 4u - (SEGGER_PTR2ADDR(pData8) & 3u);
  }
  //
  // The buffer must be large enough to store the file buffer information.
  //
  if (NumBytes <= (I32)sizeof(FS_FILE_BUFFER)) {
    return FS_ERRCODE_INVALID_PARA;     // Error, buffer too small.
  }
  pFileBuffer  = SEGGER_PTR2PTR(FS_FILE_BUFFER, pData8);
  NumBytes    -= (I32)sizeof(FS_FILE_BUFFER);
  pData8      += sizeof(FS_FILE_BUFFER);
  //
  // The data buffer is also 32-bit aligned and allocated after the file buffer information.
  //
  if ((SEGGER_PTR2ADDR(pData8) & 3u) != 0u) {
    NumBytesAlign  = 4u - (SEGGER_PTR2ADDR(pData8) & 3u);
    NumBytes      -= (I32)NumBytesAlign;
    pData8        += 4u - (SEGGER_PTR2ADDR(pData8) & 3u);
  }
  //
  // The data buffer must be large enough to store at least 1 byte.
  //
  if (NumBytes < 0) {
    return FS_ERRCODE_INVALID_PARA;     // Error, buffer too small.
  }
  //
  // Initialize the file buffer.
  //
  FS_MEMSET(pFileBuffer, 0, sizeof(FS_FILE_BUFFER));
  FS_LOCK_SYS();
  pFileBuffer->BufferSize = (U32)NumBytes;
  pFileBuffer->pData      = pData8;
  pFileBuffer->Flags      = (U8)Flags;
  //
  // Assign the file buffer to file handle.
  //
  pFile->pBuffer = pFileBuffer;
  FS_UNLOCK_SYS();
  return 0;               // OK, buffer assigned.
}

#endif // FS_SUPPORT_FILE_BUFFER

/*********************************************************************
*
*       FS__Write
*
*  Function description
*    Internal version of FS_Write.
*    Write data to a file.
*
*  Parameters
*    pFile        Pointer to an opened file handle.
*    pData        Pointer to the data to be written.
*    NumBytes     Number of bytes to be written.
*
*  Return value
*    Number of bytes written.
*/
U32 FS__Write(FS_FILE * pFile, const void * pData, U32 NumBytes) {
  U8            InUse;
  U32           NumBytesWritten;
  FS_FILE_OBJ * pFileObj;
  FS_DEVICE   * pDevice;

  if (NumBytes == 0u) {
    return 0;                     // OK, nothing to write.
  }
  if (pFile == NULL) {
    return 0;                     // Error, no file handle.
  }
  pDevice         = NULL;
  NumBytesWritten = 0;
  //
  // Load file information.
  //
  FS_LOCK_SYS();
  InUse    = pFile->InUse;
  pFileObj = pFile->pFileObj;
  if (pFileObj != NULL) {
    if (pFileObj->pVolume != NULL) {
      pDevice = &pFileObj->pVolume->Partition.Device;
    }
  }
  FS_UNLOCK_SYS();
  if ((InUse == 0u) || (pFileObj == NULL)) {
    FS_DEBUG_ERROROUT((FS_MTYPE_API, "FS__Write: Application closed the file."));
    return 0;                     // Error, the file handle was closed by an other task.
  }
  if (pDevice == NULL) {
    FS_DEBUG_ERROROUT((FS_MTYPE_API, "FS__Write: Application unmounted the volume."));
    return 0;                     // Error, the volume was unmounted by an other task.
  }
  //
  // Lock driver before performing operation.
  //
  FS_LOCK_DRIVER(pDevice);
  //
  // Multi-tasking environments with per-driver-locking:
  // Make sure that relevant file information has not changed (an other task may have closed the file, unmounted the volume etc.)
  // If it has, no action is performed.
  //
#if FS_OS_LOCK_PER_DRIVER
  FS_LOCK_SYS();
  if (pFileObj != pFile->pFileObj) {
    if (pFile->pFileObj == NULL) {
      InUse = 0;
    }
  }
  if (pFile->InUse == 0u) {
    InUse = 0;
  }
  FS_UNLOCK_SYS();
  if (InUse == 0u) {              // Let's make sure the file is still valid
    FS_DEBUG_ERROROUT((FS_MTYPE_API, "FS__Write: Application closed the file."));
  } else
#endif
  if ((pFile->AccessFlags & FS_FILE_ACCESS_FLAG_W) == 0u) {
    pFile->Error = FS_ERRCODE_READ_ONLY_FILE;               // Error, open mode does now allow write access.
  } else {
    //
    // All checks and locking operations completed. Call the File system (FAT/EFS) layer.
    //
#if FS_SUPPORT_FILE_BUFFER
    int r;

    r = FS__FB_Write(pFile, pData, NumBytes);
    if (r < 0) {
      if (pFile->Error == 0) {
        pFile->Error = (I16)r;                              // Error, could not write data.
      }
    } else {
      NumBytesWritten  = (U32)r;
      NumBytes        -= NumBytesWritten;
      if (NumBytes != 0u) {
        NumBytesWritten += _WriteNL(pFile, pData, NumBytes);
      }
      r = _FB_Sync(pFile);
      if (r != 0) {
        if (pFile->Error == 0) {
          pFile->Error = (I16)r;                            // Error, could not write data.
        }
      }
    }
#else
    NumBytesWritten += _WriteNL(pFile, pData, NumBytes);
#endif
  }
  FS_UNLOCK_DRIVER(pDevice);
  return NumBytesWritten;
}

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_Write
*
*  Function description
*    Writes data to file.
*
*  Parameters
*    pFile        Handle to opened file.
*    pData        The data to be written to file.
*    NumBytes     Number of bytes to be written to file.
*
*  Return value
*    Number of bytes written.
*
*  Additional information
*    The file has to be opened with write permissions.
*    For more information about open modes refer to FS_FOpen().
*
*    The application has to check for possible errors using FS_FError()
*    if the number of bytes actually written is different than
*    the number of bytes requested to be written by the application.
*
*    The data is written at the current position in the file that is
*    indicated by the file pointer. FS_FWrite() moves the file pointer
*    forward by the number of bytes successfully written.
*/
U32 FS_Write(FS_FILE * pFile, const void * pData, U32 NumBytes) {
  U32 NumBytesWritten;

  FS_LOCK();
  FS_PROFILE_CALL_U32x3(FS_EVTID_WRITE, SEGGER_PTR2ADDR(pFile), SEGGER_PTR2ADDR(pData), NumBytes);
  NumBytesWritten = FS__Write(pFile, pData, NumBytes);
  FS_PROFILE_END_CALL_U32(FS_EVTID_WRITE, NumBytesWritten);
  FS_UNLOCK();
  return NumBytesWritten;
}

#if FS_SUPPORT_FILE_BUFFER

/*********************************************************************
*
*       FS_SetFileBuffer
*
*  Function description
*    Assigns a file buffer to an opened file.
*
*  Parameters
*    pFile        Handle to opened file.
*    pData        Pointer to the to memory area which should be used
*                 as buffer.
*    NumBytes     Number of bytes in the buffer.
*    Flags        Specifies the operating mode of the file buffer.
*                 * 0                       Read file buffer.
*                 * FS_FILE_BUFFER_WRITE    Read / write file buffer.
*                 * FS_FILE_BUFFER_ALIGNED  Logical sector boundary alignment.
*
*  Return value
*    ==0    OK, buffer assigned.
*    !=0    Error code indicating the failure reason.
*
*  Additional information
*    This function has to be called immediately after the file is
*    opened and before any read or write operation is performed on
*    the file.  If the file buffer is configured in write mode the
*    data of any operation that writes less bytes at once than the
*    size of the file buffer is stored to file buffer. The contents
*    of the file buffer is written to file in the following cases:
*    * when the file buffer is full.
*    * when place is required for new data read from file.
*    * when closing the file via FS_FClose().
*    * when synchronizing the file to storage via FS_SyncFile().
*    * when unmounting the file system via FS_Unmount()
*      or FS_UnmountForced().
*    * when the file system is synchronized via FS_Sync().
*    In case of a read operation if the data is not present in the
*    file buffer the file system fills the entire file buffer with
*    the data from file.
*
*    FS_SetFileBuffer reports an error if the file system is
*    configured to automatically allocate a file buffer for each
*    file it opens via FS_ConfigFileBufferDefault().
*
*    The data required to manage the file buffer is allocated from
*    pData. The FS_SIZEOF_FILE_BUFFER() define can be used to
*    calculate the amount of RAM required to store a specified
*    number of data bytes in the file buffer.
*
*    If the file is opened and closed in the same function the file
*    buffer can be allocated locally on the stack. Otherwise the buffer
*    has to be globally allocated. After the file is closed the memory
*    allocated for the file buffer is no longer accessed by the file
*    system and can be safely deallocated or used to store other data.
*
*    FS_SetFileBuffer() is available if the emFile sources are compiled
*    with the FS_SUPPORT_FILE_BUFFER configuration define set to 1.
*/
int FS_SetFileBuffer(FS_FILE * pFile, void * pData, I32 NumBytes, int Flags) {
  int r;

  FS_LOCK();
  r = FS__SetFileBuffer(pFile, pData, NumBytes, Flags);
  FS_UNLOCK();
  return r;
}

#endif // FS_SUPPORT_FILE_BUFFER

/*********************************************************************
*
*       FS_FWrite
*
*  Function description
*    Writes data to file.
*
*  Parameters
*    pData      Data to be written to file.
*    ItemSize   Size of an item to be written to file (in bytes).
*    NumItems   Number of items to be written to file.
*    pFile      Handle to opened file. It cannot be NULL.
*
*  Return value
*    Number of elements written.
*
*  Additional information
*    The file has to be opened with write permissions.
*    For more information about open modes refer to FS_FOpen().
*
*    The application has to check for possible errors using FS_FError()
*    if the number of items actually written is different than
*    the number of items requested to be written by the application.
*
*    The data is written at the current position in the file that is
*    indicated by the file pointer. FS_FWrite() moves the file pointer
*    forward by the number of bytes successfully written.
*/
U32 FS_FWrite(const void * pData, U32 ItemSize, U32 NumItems, FS_FILE * pFile) {
  U32 NumBytesWritten;
  U32 NumBytes;

  //
  // Validate the parameters.
  //
  if (ItemSize == 0u) {
    return 0;             // Return here to avoid dividing by zero at the end of the function.
  }
  //
  // Perform the operation.
  //
  FS_LOCK();
  NumBytes = NumItems * ItemSize;
  NumBytesWritten = FS__Write(pFile, pData, NumBytes);
  FS_UNLOCK();
  return NumBytesWritten / ItemSize;
}

/*********************************************************************
*
*       FS_FPuts
*
*  Function description
*    Writes a 0-terminated string to a file.
*
*  Parameters
*    sData      Data to be written (0-terminated string). It cannot be NULL.
*    pFile      Opened file handle. It cannot be NULL.
*
*  Return value
*    ==0    OK, data written successfully.
*    !=0    Error code indicating the failure reason.
*
*  Additional information
*    This function works in the same way as the fputs() standard C
*    library function. It writes the 0-terminated string sData to
*    the file pFile. The 0-terminator is not written to file.
*    The file position is advanced by the number of bytes written.
*/
int FS_FPuts(const char * sData, FS_FILE * pFile) {
  int r;
  U32 NumBytes;
  U32 NumBytesWritten;

  //
  // Validate parameters.
  //
  if ((sData == NULL) || (pFile == NULL)) {
    return FS_ERRCODE_INVALID_PARA;
  }
  //
  // Perform the operation.
  //
  FS_LOCK();
  r = FS_ERRCODE_OK;                  // Set to indicate success.
  NumBytes = (U32)FS_STRLEN(sData);
  NumBytesWritten = FS__Write(pFile, sData, NumBytes);
  if (NumBytesWritten != NumBytes) {
    r = FS__FError(pFile);            // Error, could not write data.
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_FPrintf
*
*  Function description
*    Writes a formatted string to a file.
*
*  Parameters
*    pFile      Opened file handle. It cannot be NULL.
*    sFormat    Format of the data to be written (0-terminated string).
*               It cannot be NULL.
*
*  Return value
*    >=0    OK, number of bytes written to file.
*    < 0    Error code indicating the failure reason.
*
*  Additional information
*    This function works in the same way as the fprintf() standard C
*    library function. It formats the data according to sFormat
*    and then writes the formatted string to pFile. The format
*    specification is identical to that of fprintf(). FS_FPrintf()
*    relies on SEGGER_vsnprintfEx() to perform the actual formatting.
*
*    The file position is advanced by the number of bytes written.
*
*    FS_FPrintf() uses a buffer allocated on the stack for the
*    formatting of the data. The size of this buffer can be configured
*    via FS_BUFFER_SIZE_FILE_PRINT and it has to be at least 1 byte large.
*/
int FS_FPrintf(FS_FILE * pFile, const char * sFormat, ...) {
  int                     r;
  SEGGER_SNPRINTF_CONTEXT PrintContext;
  va_list                 ParamList;
  U32                     aBuffer[(FS_BUFFER_SIZE_FILE_PRINT + 3) / 4];
  SEGGER_BUFFER_DESC      BufferDesc;
  WRITE_FORMATTED_CONTEXT WriteFormattedContext;

  //lint --e{438} -esym(530, ParamList) N:102.

  //
  // Validate parameters.
  //
  if ((pFile == NULL) || (sFormat == NULL)) {
    return FS_ERRCODE_INVALID_PARA;
  }
  //
  // Perform the operation.
  //
  FS_LOCK();
  FS_MEMSET(&BufferDesc, 0, sizeof(BufferDesc));
  FS_MEMSET(&WriteFormattedContext, 0, sizeof(WriteFormattedContext));
  FS_MEMSET(&PrintContext, 0, sizeof(PrintContext));
  FS_MEMSET(aBuffer, 0, sizeof(aBuffer));
  BufferDesc.pBuffer          = SEGGER_PTR2PTR(char, aBuffer);      // MISRA deviation D:100[e]
  BufferDesc.BufferSize       = (int)sizeof(aBuffer);
  WriteFormattedContext.pFile = pFile;
  PrintContext.pBufferDesc    = &BufferDesc;
  PrintContext.pContext       = &WriteFormattedContext;
  PrintContext.pfFlush        = _cbWriteFormatted;
  va_start(ParamList, sFormat);                                     //lint !e586 macro 'va_start' is deprecated N:102. Reason: this is the only way we handle a variable argument list.
  (void)SEGGER_vsnprintfEx(&PrintContext, sFormat, ParamList);
  va_end(ParamList);                                                //lint !e586 macro 'va_end' is deprecated N:102. Reason: this is the only way we handle a variable argument list.
  r = (int)WriteFormattedContext.NumBytes;
  if (WriteFormattedContext.IsError != 0u) {
    r = FS__FError(pFile);                                          // Error, could not write data.
  }
  FS_UNLOCK();
  return r;
}

/*************************** End of file ****************************/
