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
File        : FS_GetNumFiles.c
Purpose     : Implementation of FS_GetNumFiles
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
*       FS__GetNumFiles
*
*  Function description
*    Returns the number of files in a directory.
*
*  Parameters
*    pDir       Pointer to an opened directory handle.
*
*  Return value
*    0xFFFFFFFF       Indicates failure
*    0 - 0xFFFFFFFE   File size of the given file
*/
U32 FS__GetNumFiles(FS_DIR * pDir) {
  U32 r;

  if (pDir != NULL) {
    FS_DIR_POS DirPosOld;

    //
    // Save the old position in pDir structure.
    //
    DirPosOld = pDir->DirObj.DirPos;              // Structure copy.
    FS__RewindDir(pDir);
    r = 0;
    for (;;) {
      U8 Attr;

      if (FS__ReadDir(pDir) == (FS_DIRENT *)NULL) {
        break;    // No more files.
      }
      FS__DirEnt2Attr(pDir->pDirEntry, &Attr);
      if ((Attr & FS_ATTR_DIRECTORY) == 0u) {     // If it is directory entry the volume ID or a directory then we ignore them.
        r++;
      }
    }
    //
    // Restore the old position in pDir structure.
    //
    pDir->DirObj.DirPos = DirPosOld;              // Structure copy.
  } else {
    r = 0xFFFFFFFFuL;
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
*       FS_GetNumFiles
*
*  Function description
*    API function. Returns the number of files in a directory.
*
*  Parameters
*    pDir       Pointer to an opened directory handle.
*
*  Return value
*    0xFFFFFFFF       Indicates failure.
*    0 - 0xFFFFFFFE   File size of the given file.
*/
U32 FS_GetNumFiles(FS_DIR * pDir) {
  U32 r;

  FS_LOCK();
  r = FS__GetNumFiles(pDir);
  FS_UNLOCK();
  return r;
}

/*************************** End of file ****************************/
