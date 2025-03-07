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
File        : FS_Move.c
Purpose     : Implementation of FS_Move
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
*       _MoveFS
*/
static int _MoveFS(FS_VOLUME * pVolume, const char * sOldName, const char * sNewName) {
  int r;
  int Result;

  r = FS__JOURNAL_Begin(pVolume);
  if (r == 0) {
    r = FS_MOVE(pVolume, sOldName, sNewName);
    FS__JOURNAL_SetError(pVolume, r);
    Result = FS__JOURNAL_End(pVolume);
    if (Result != 0) {
      r = Result;
    }
  } else {
    (void)FS_MOVE(pVolume, sOldName, sNewName);           // Perform the operation without journal.
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
*       FS__MoveEx
*
*  Function description
*    Internal version of FS_Move() without global locking.
*
*  Parameters
*    pVolume      Volume on which the source and the destination file are located.
*    sNameSrc     Name of the source file or directory (not qualified). Cannot be NULL.
*    sNameDest    Name of the destination file or directory (not qualified). Cannot be NULL.
*
*  Return value
*    ==0      OK, file has been moved.
*    !=0      Error code indicating the failure reason.
*
*  Additional information
*    This function can move only files or directories that are located
*    on the same volume.
*/
int FS__MoveEx(FS_VOLUME * pVolume, const char * sNameSrc, const char * sNameDest) {
  int r;

  r = FS__AutoMount(pVolume);
  switch ((unsigned)r) {
  case FS_MOUNT_RW:
    FS_LOCK_DRIVER(&pVolume->Partition.Device);
#if FS_SUPPORT_JOURNAL
    r = _MoveFS(pVolume, sNameSrc, sNameDest);
#else
    r = FS_MOVE(pVolume, sNameSrc, sNameDest);
#endif // FS_SUPPORT_JOURNAL
    FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
    break;
  case FS_MOUNT_RO:
    r = FS_ERRCODE_READ_ONLY_VOLUME;
    break;
  case 0:
    r = FS_ERRCODE_VOLUME_NOT_MOUNTED;
    break;
  default:
    //
    // Error, could not mount the volume.
    //
    break;
  }
  return r;
}

/*********************************************************************
*
*       FS__Move
*
*  Function description
*    Internal version of FS_Move() without global locking.
*
*  Parameters
*    sNameSrc     Name of the source file or directory (partially qualified). Cannot be NULL.
*    sNameDest    Name of the destination file or directory  (partially qualified). Cannot be NULL.
*
*  Return value
*    ==0      OK, file has been moved.
*    !=0      Error code indicating the failure reason.
*/
int FS__Move(const char * sNameSrc, const char * sNameDest) {
  const char * sNameSrcNQ;
  const char * sNameDestNQ;
  FS_VOLUME  * pVolumeSrc;
  FS_VOLUME  * pVolumeDest;
  int          r;

  pVolumeSrc  = FS__FindVolumeEx(sNameSrc,  &sNameSrcNQ);
  pVolumeDest = FS__FindVolumeEx(sNameDest, &sNameDestNQ);
  if (pVolumeSrc == pVolumeDest) {
    if (pVolumeSrc == NULL) {
      r = FS_ERRCODE_VOLUME_NOT_FOUND;
    } else {
#if FS_MULTI_HANDLE_SAFE
      {
        int Result;

        //
        // Verify that the path to the destination file
        // fits into the internal file name buffer.
        //
        Result = FS__BuildFileNameFQ(pVolumeDest, sNameDestNQ, NULL, FS_MAX_LEN_FULL_FILE_NAME);
        if (Result < 0) {
          r = Result;
        } else {
          r = 0;
        }
      }
      if (r == 0)
#endif // FS_MULTI_HANDLE_SAFE
      {
        r = FS__MoveEx(pVolumeSrc, sNameSrcNQ, sNameDestNQ);
      }
    }
  } else {
    //
    // Perform a copy operation if the source and destination are
    // located on different volumes.
    //
    r = FS__CopyFile(sNameSrc, sNameDest);
    if (r == 0) {
      r = FS__Remove(sNameSrc);
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
*       FS_Move
*
*  Function description
*    Moves a file or directory to another location.
*
*  Parameters
*    sNameSrc     Name of the source file or directory (partially qualified). Cannot be NULL.
*    sNameDest    Name of the destination file or directory  (partially qualified). Cannot be NULL.
*
*  Return value
*    ==0      OK, file has been moved.
*    !=0      Error code indicating the failure reason.
*
*  Additional information
*    FS_Move() can be used to relocate a file to another location
*    on the same or on a different volume either in the same in a different
*    directory. If the source and the destination files are located on the
*    same volume, the file is moved, otherwise the file is copied and the
*    source is deleted.
*
*    The function is also able to move and entire directory trees when
*    the source and destination are located on the same volume.
*    Moving an entire directory tree on a different volume is not supported.
*    This operation has to be performed in the application by iterating
*    over the files and directories and by copying them one-by-one.
*
*    By default, the source files and directories that have the
*    FS_ATTR_READ_ONLY attribute set and that are located on a volume
*    formatted as FAT cannot be moved. This behavior can be changed by
*    compiling the file system sources with FS_FAT_PERMIT_RO_FILE_MOVE
*    configuration define to 1. FS_FAT_ConfigROFileMovePermission()
*    can be used to change the behavior at runtime.
*
*    Source files and directories located on a EFS formatted volume
*    can be moved even if they have the FS_ATTR_READ_ONLY attribute
*    set.
*
*    The operation fails if the destination file or directory already exists.
*/
int FS_Move(const char * sNameSrc, const char * sNameDest) {
  int r;

  FS_LOCK();
  r = FS__Move(sNameSrc, sNameDest);
  FS_UNLOCK();
  return r;
}

/*************************** End of file ****************************/
