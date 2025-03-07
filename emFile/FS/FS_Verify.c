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
File        : FS_Verify.c
Purpose     : Implementation of FS_Verify
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
*       FS__Verify
*
*  Function description
*    Internal version of FS_Verify
*    Verifies a file with a given data buffer
*
*  Parameters
*    pFile      Pointer to an open file.
*    pData      Pointer to the data source of verification.
*    NumBytes   Number of bytes to be verified.
*
*  Return value
*    ==0      Verification was successful.
*    !=0      Verification failed.
*/
int FS__Verify(FS_FILE * pFile, const void * pData, U32 NumBytes) {
  U32            aVerifyBuffer[FS_BUFFER_SIZE_VERIFY / 4];
  FS_FILE_SIZE   NumBytesInFile;
  FS_FILE_SIZE   NumBytesToCheck;
  U32            NumBytesAtOnce;
  U32            NumBytesRead;
  int            r;
  const U8     * p;

  r = FS_ERRCODE_INVALID_PARA;
  if (pData != NULL) {
    p = SEGGER_CONSTPTR2PTR(const U8, pData);
    if (pFile != NULL) {
      NumBytesInFile = FS__GetFileSize(pFile);
      NumBytesToCheck = SEGGER_MIN(NumBytes, NumBytesInFile);
      for (;;) {
        //
        // Request only as much bytes as are available.
        //
        NumBytesAtOnce = (U32)SEGGER_MIN((unsigned)FS_BUFFER_SIZE_VERIFY, NumBytesToCheck);
        NumBytesRead   = FS__Read(pFile, aVerifyBuffer, NumBytesAtOnce);
        if (NumBytesRead < NumBytesAtOnce) {
          r = FS_ERRCODE_READ_FAILURE;
          break;                        // Error, could not read sufficient data.
        }
        r = FS_MEMCMP(p, aVerifyBuffer, NumBytesRead);
        if (r != 0) {
          r = FS_ERRCODE_VERIFY_FAILURE;
          break;                        // Verification failed.
        }
        NumBytesToCheck -= NumBytesRead;
        p               += NumBytesRead;
        if (NumBytesToCheck == 0u) {
          r = FS_ERRCODE_OK;
          break;                        // Data successfully verified.
        }
      }
    }
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
*       FS_Verify
*
*  Function description
*    Verifies the file contents.
*
*  Parameters
*    pFile        Handle to opened file.
*    pData        Data to be checked against.
*    NumBytes     Number of bytes to be checked.
*
*  Return value
*    ==0      Verification was successful.
*    !=0      Verification failed.
*
*  Additional information
*    The function starts checking at the current file position.
*    That is the byte read from file position + 0 is checked
*    against the byte at pData + 0, the byte read from file position + 1
*    is checked against the byte at pData + 1 and so on.
*    FS_Verify() does not modify the file position.
*/
int FS_Verify(FS_FILE * pFile, const void * pData, U32 NumBytes) {
  int r;

  FS_LOCK();
  r = FS__Verify(pFile, pData, NumBytes);
  FS_UNLOCK();
  return r;
}

/*************************** End of file ****************************/
