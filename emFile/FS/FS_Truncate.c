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
File        : FS_Truncate.c
Purpose     : Implementation of FS_Truncate
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
*       _ShrinkFile
*
*  Function description
*    Reduces the size of a file.
*
*  Parameters
*    pFile        Pointer to a valid opened file with write access. It cannot be NULL.
*    NewFileSize  The new size of the file that has to be smaller than
*                 the current file size.
*
*  Return value
*    ==0        OK, file size has been reduced.
*    !=0        Error code indicating the failure reason.
*/
static int _ShrinkFile(FS_FILE * pFile, FS_FILE_SIZE NewFileSize) {
  int r;

  r = FS_ERRCODE_INVALID_PARA;
  if (pFile->pFileObj->Size > NewFileSize) {
    FS__SetFilePos(pFile, (FS_FILE_OFF)NewFileSize, FS_SEEK_SET);
    //
    // Free clusters.
    //
    r = FS__SetEndOfFile(pFile);
  }
  return r;
}

/*********************************************************************
*
*       Public code (internal)
*
**********************************************************************
*/

#if FS_SUPPORT_POSIX

/*********************************************************************
*
*       FS__TruncateFile
*
*  Function description
*    Changes the size of a file.
*
*  Parameters
*    pFile        Pointer to a valid opened file with write access. It cannot be NULL.
*    FileSizeNew  The new size of the file.
*
*  Return value
*    ==0        OK, file size has been changed.
*    !=0        Error code indicating the failure reason.
*
*  Notes
*    (1) This function allocates FS_BUFFER_SIZE_TRUNCATE bytes on the stack.
*/
int FS__TruncateFile(FS_FILE * pFile, FS_FILE_SIZE FileSizeNew) {
  int r;
  I32 FilePos;
  U32 aBuffer[FS_BUFFER_SIZE_TRUNCATE / 4];
  U32 NumBytesToFill;
  U32 NumBytesToWrite;
  U32 NumBytesWritten;
  U32 FileSizeOld;

  //
  // In POSIX mode the file position has to stay untouched therefore
  // we have to save it here.
  //
  FileSizeOld = FS__GetFileSize(pFile);
  FilePos = FS__FTell(pFile);
  FS__SetFilePos(pFile, (I32)FileSizeNew, FS_SEEK_SET);
  //
  // Allocate / free clusters.
  //
  r = FS__SetEndOfFile(pFile);
  if (r == 0) {
    if ((U32)FileSizeNew > FileSizeOld) {
      //
      // Set the file pointer at the end of previous file size.
      //
      FS__SetFilePos(pFile, (I32)FileSizeOld, FS_SEEK_SET);
      //
      // Fill the new data with 0s.
      //
      NumBytesToFill = FileSizeNew - FileSizeOld;
      FS_MEMSET(aBuffer, 0, sizeof(aBuffer));
      do {
        NumBytesToWrite = SEGGER_MIN(NumBytesToFill, sizeof(aBuffer));
        NumBytesWritten = FS_FILE_WRITE(pFile, aBuffer, NumBytesToWrite);
        if (NumBytesWritten != NumBytesToWrite) {
          r = pFile->Error;         // Error, could not write data to file.
          break;
        }
        NumBytesToFill -= NumBytesWritten;
      } while (NumBytesToFill != 0u);
    }
  }
  //
  // Restore the file position.
  //
  FS__SetFilePos(pFile, FilePos, FS_SEEK_SET);
  return r;
}

#endif // FS_SUPPORT_POSIX

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_Truncate
*
*  Function description
*    Changes the size of a file.
*
*  Parameters
*    pFile        Pointer to a valid opened file with write access.
*    NewFileSize  The new size of the file.
*
*  Return value
*    ==0        OK, file size has been truncated.
*    !=0        Error code indicating the failure reason.
*
*  Additional information
*    The file has to be opened with write permissions.
*    For more information about open modes refer to FS_FOpen().
*    An error is returned if NewSize is larger than the actual
*    file size.
*
*    If the application uses FS_Truncate() to increase the size of a file then,
*    by default, the extra bytes are left uninitialized for performance reasons.
*    This behavior is not compatible to the POSIX specification which requests
*    that ftruncate() fills the extra bytes with 0s. This behavior can be changed
*    by enabling the support for POSIX operation. The application can enable the
*    support for POSIX operation by setting FS_SUPPORT_POSIX to 1 at compile time
*    and by calling FS_ConfigPOSIXSupport() with the OnOff parameter set to 1 at runtime.
*
*    FS_Truncate() allocates FS_BUFFER_SIZE_TRUNCATE bytes on the stack
*    when the size of the file is increased with support for the POSIX operation enabled.
*/
int FS_Truncate(FS_FILE * pFile, U32 NewFileSize) {
  int r;

  FS_LOCK();
  r = FS_ERRCODE_INVALID_PARA;
  if (pFile != NULL) {
#if FS_SUPPORT_POSIX
    if (FS_IsPOSIXSupported != 0u) {
      r = FS__TruncateFile(pFile, (FS_FILE_SIZE)NewFileSize);
    } else {
      r = _ShrinkFile(pFile, (FS_FILE_SIZE)NewFileSize);
    }
#else
    r = _ShrinkFile(pFile, (FS_FILE_SIZE)NewFileSize);
#endif // FS_SUPPORT_POSIX
  }
  FS_UNLOCK();
  return r;
}

/*************************** End of file ****************************/
