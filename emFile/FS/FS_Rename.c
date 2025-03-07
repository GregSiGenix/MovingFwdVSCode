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
File        : FS_Rename.c
Purpose     : Implementation of FS_Rename
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

#if FS_SUPPORT_JOURNAL

/*********************************************************************
*
*       _RenameFS
*/
static int _RenameFS(FS_VOLUME * pVolume, const char * sOldName, const char * sNewName) {
  int r;
  int Result;

  r = FS__JOURNAL_Begin(pVolume);
  if (r == 0) {
    r = FS_RENAME(pVolume, sOldName, sNewName);
    FS__JOURNAL_SetError(pVolume, r);
    Result = FS__JOURNAL_End(pVolume);
    if (Result != 0) {
      r = Result;
    }
  } else {
    (void)FS_RENAME(pVolume, sOldName, sNewName);       // Perform the operation without journaling.
  }
  return r;
}

#endif // FS_SUPPORT_JOURNAL

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_Rename
*
*  Function description
*    Changes the name of a file or directory.
*
*  Parameters
*    sNameOld   Old file or directory name (including the path).
*    sNameNew   New file or directory name (without path).
*
*  Return value
*    ==0      OK, file or directory has been renamed.
*    !=0      Error code indicating the failure reason.
*
*  Additional information
*    The function can rename either a file or a directory.
*
*    By default, the files and directories that have the
*    FS_ATTR_READ_ONLY attribute set and that are located on a volume
*    formatted as FAT cannot be renamed. This behavior can be changed
*    by compiling the file system sources with the
*    FS_FAT_PERMIT_RO_FILE_MOVE configuration define to 1.
*    FS_FAT_ConfigROFileMovePermission() can be used to change
*    the behavior at runtime.
*
*    Source files and directories located on a EFS formatted volume
*    can be moved event if they have the FS_ATTR_READ_ONLY attribute
*    set.
*/
int FS_Rename(const char * sNameOld, const char * sNameNew) {
  const char * s;
  int          r;
  FS_VOLUME  * pVolume;

  FS_LOCK();
  r = FS_ERRCODE_VOLUME_NOT_FOUND;
  pVolume = FS__FindVolumeEx(sNameOld, &s);
  if (pVolume != NULL) {
    r = FS__AutoMount(pVolume);
    switch ((unsigned)r) {
    case FS_MOUNT_RW:
      //
      // Make sure that the new file name does not contain any directory delimiter.
      //
      if (FS__FindDirDelimiter(sNameNew) != NULL) {
        r = FS_ERRCODE_INVALID_PARA;
      } else {
#if FS_MULTI_HANDLE_SAFE
        //
        // Verify that the path to the destination file fits into the internal file name buffer.
        //
        {
          unsigned     NumBytesFree;
          unsigned     NumBytesNameOld;
          unsigned     NumBytesNameNew;
          const char * sName;
          const char * sNameNext;
          int          Result;

          //
          // Calculate how many bytes are required to store the fully qualified name of the old file or directory.
          //
          Result = FS__BuildFileNameFQ(pVolume, s, NULL, FS_MAX_LEN_FULL_FILE_NAME);
          if (Result < 0) {
            r = Result;
          } else {
            r = 0;
            //
            // Calculate the number of bytes still free in the buffer.
            //
            NumBytesFree = (unsigned)FS_MAX_LEN_FULL_FILE_NAME - (unsigned)Result;
            //
            // Calculate the number of bytes required to store only the name of the file or directory.
            //
            NumBytesNameNew = (unsigned)FS_STRLEN(sNameNew);
            sName     = sNameOld;
            sNameNext = NULL;
            for (;;) {
              sNameNext = FS__FindDirDelimiter(sName);
              if (sNameNext == NULL) {
                break;
              }
              sName = sNameNext + 1;                                // Skip over the directory delimiter.
            }
            NumBytesNameOld = (unsigned)FS_STRLEN(sName);
            //
            // Check if the new name is longer and if so if the additional number
            // of bytes do not exceed the number of available bytes.
            //
            if (NumBytesNameNew > NumBytesNameOld) {
              if ((NumBytesNameNew - NumBytesNameOld) > NumBytesFree) {
                r = FS_ERRCODE_FILENAME_TOO_LONG;                   // Error, file name is too long to store in the internal file name buffer.
              }
            }
          }
        }
        if (r == 0)
#endif // FS_MULTI_HANDLE_SAFE
        {
          //
          // Call the function of the file system layer to do the actual work.
          //
          FS_LOCK_DRIVER(&pVolume->Partition.Device);
#if FS_SUPPORT_JOURNAL
          r = _RenameFS(pVolume, s, sNameNew);
#else
          r = FS_RENAME(pVolume, s, sNameNew);
#endif // FS_SUPPORT_JOURNAL
          FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
        }
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
  FS_UNLOCK();
  return r;
}

/*************************** End of file ****************************/
