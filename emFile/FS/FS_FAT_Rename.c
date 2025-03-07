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
File        : FS_FAT_Rename.c
Purpose     : FAT routines for renaming files or directories
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#include "FS_FAT_Int.h"

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_FAT_Rename
*
*  Function description
*    Renames a file or directory.
*
*  Parameters
*    pVolume      Volume on which the file or directory is stored.
*    sOldName     Name of the file or directory to be renamed (fully qualified).
*    sNewName     New name of the file or directory name (without path).
*
*  Return value
*    ==0          OK, file or directory renamed.
*    !=0          Error code indicating the failure reason.
*/
int FS_FAT_Rename(FS_VOLUME * pVolume, const char * sOldName, const char * sNewName) {
  const char * sOldNameNQ;
  int          r;
  FS_SB        sb;
  U32          DirStart;
  int          Result;

  r = FS_ERRCODE_PATH_NOT_FOUND;    // Set to indicate error.
  (void)FS__SB_Create(&sb, pVolume);
  //
  // Check that the source file or directory exists.
  //
  Result = FS_FAT_FindPath(pVolume, &sb, sOldName, &sOldNameNQ, &DirStart);
  if (Result != 0) {
    //
    // The source file or directory exists. Perform the operation.
    //
    r = FS_FAT_MoveEx(pVolume, DirStart, DirStart, sOldNameNQ, sNewName, &sb);
  }
  FS__SB_Delete(&sb);
  if (r == 0) {
    r = FS__SB_GetError(&sb);
  }
  return r;
}

/*************************** End of file ****************************/

