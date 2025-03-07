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
File        : FS_CreateDir.c
Purpose     : Implementation of said function
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include section
*
**********************************************************************
*/
#include <stdlib.h>
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
*       _CreateDirFS
*/
static int _CreateDirFS(FS_VOLUME * pVolume, const char * sDirName) {
  int r;
  int Result;

  r = FS__JOURNAL_Begin(pVolume);
  if (r == 0) {
    r = FS_CREATE_DIR(pVolume, sDirName);
    FS__JOURNAL_SetError(pVolume, r);
    Result = FS__JOURNAL_End(pVolume);
    if (Result != 0) {
      r = Result;
    }
  } else {
    (void)FS_CREATE_DIR(pVolume, sDirName);           // Perform the operation without journal.
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
*       FS__CreateDir
*
*  Function description
*    Internal version of FS__CreateDir.
*    Creates a directory, directory path - if a directory does not
*    exist in the directory path, the directory is created.
*
*  Parameters
*    sDirName     Partially qualified name of the directory to create.
*
*  Return value
*    ==0      Directory path has been created.
*    ==1      Directory path already exists.
*    < 0      Error code indicating the failure reason.
*
*  Notes
*    (1) The function allocates about FS_MAX_PATH bytes on the stack.
*/
int FS__CreateDir(const char * sDirName) {
  int          r;
  FS_VOLUME  * pVolume;
  const char * sDirPath;

  //
  // Find correct volume.
  //
  pVolume = FS__FindVolumeEx(sDirName, &sDirPath);
  if (pVolume != NULL) {
    if (*sDirPath != '\0') {
      //
      // Mount the volume if necessary.
      //
      r = FS__AutoMount(pVolume);
      switch ((unsigned)r) {
      case FS_MOUNT_RW:
        {
          FS_FIND_DATA fd;
          const char * sNextDirName;
          unsigned     NumChars;
          unsigned     DestLen;
          unsigned     SrcLen;
          char         acDestPath[FS_MAX_PATH];

          //
          // Remove the leading directory delimiter from the path.
          //
          sNextDirName = FS__FindDirDelimiter(sDirPath);
          if (sDirPath == sNextDirName) {
            ++sDirPath;
          }
          acDestPath[0] = '\0';
          DestLen       = 0;
          SrcLen        = (unsigned)FS_STRLEN(sDirPath);
          if (SrcLen > sizeof(acDestPath)) {
            FS_DEBUG_ERROROUT((FS_MTYPE_API, "FS__CreateDir: Path is too long."));
            return FS_ERRCODE_PATH_TOO_LONG;
          }
          //
          // Parse the directory path.
          //
          for (;;) {
            //
            // For each FS_DIRECTORY_DELIMITER in string
            // check the directory by opening it.
            //
            sNextDirName = FS__FindDirDelimiter(sDirPath);
            if (sNextDirName != NULL) {
              NumChars = (unsigned)SEGGER_PTR_DISTANCE(sNextDirName, sDirPath);       // MISRA deviation D:103[e]
            } else if (SrcLen > 0u) {
              NumChars = SrcLen;
            } else {
              break;
            }
            (void)FS_STRNCAT(acDestPath, sDirPath, (U32)NumChars);
            //
            // Open a handle to the directory.
            //
            if (FS__FindFirstFileEx(&fd, pVolume, acDestPath, NULL, 0) < 0) {
              //
              // Create the directory.
              //
              FS_LOCK_DRIVER(&pVolume->Partition.Device);
#if FS_SUPPORT_JOURNAL
              r = _CreateDirFS(pVolume, acDestPath);
#else
              r = FS_CREATE_DIR(pVolume, acDestPath);
#endif // FS_SUPPORT_JOURNAL
              FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
              if (r != 0) {
                break;
              }
            } else {
              //
              // Close the handle to the opened directory.
              //
              FS__FindClose(&fd);
              r = 1;
            }
            //
            // Update length of the path strings.
            //
            DestLen += NumChars;
            SrcLen  -= NumChars;
            if (DestLen != 0u) {
              //
              // Append directory delimiter to path.
              //
              acDestPath[DestLen++] = FS_DIRECTORY_DELIMITER;
              acDestPath[DestLen]   = '\0';
            }
            if (SrcLen != 0u) {
              SrcLen--;
            }
            if (sNextDirName != NULL) {
              sDirPath = sNextDirName + 1;
            }
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
    } else {
      r = FS_ERRCODE_INVALID_PARA;        // Error, path to directory is missing.
    }
  } else {
    r = FS_ERRCODE_VOLUME_NOT_FOUND;      // Error, volume not found.
  }
  return r;
}

/*********************************************************************
*
*       FS_CreateDir
*
*  Function description
*    Creates a directory including any missing directories from path.
*
*  Parameters
*    sDirName   Directory name.
*
*  Return value
*    ==0      Directory path has been created.
*    ==1      Directory path already exists.
*    < 0      Error code indicating the failure reason.
*
*  Additional information
*    The function creates automatically any subdirectories that are
*    specified in the path but do not exist on the storage.
*
*    FS_CreateDir() uses a work buffer of FS_MAX_PATH bytes allocated
*    on the stack for parsing the path to directory.
*/
int FS_CreateDir(const char * sDirName) {
  int  r;

  FS_LOCK();
  r = FS__CreateDir(sDirName);
  FS_UNLOCK();
  return r;
}

/*************************** End of file ****************************/
