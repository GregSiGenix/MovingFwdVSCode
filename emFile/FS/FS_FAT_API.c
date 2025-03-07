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
File        : FS_FAT_API.c
Purpose     : FAT File System Layer function table
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#include "FS_FAT.h"

#if FS_SUPPORT_MULTIPLE_FS

/*********************************************************************
*
*       Defines, configurable
*
**********************************************************************
*/

#ifndef     FS_FAT_READ_AT_ENTRY
  #if FS_SUPPORT_TEST
    #define FS_FAT_READ_AT_ENTRY    FS_FAT_ReadATEntry
  #else
    #define FS_FAT_READ_AT_ENTRY    NULL
  #endif
#endif

/*********************************************************************
*
*       Public const data
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_FAT_API
*/
const FS_FS_API FS_FAT_API = {
  FS_FAT_CheckBPB,
  FS_FAT_OpenFile,
  FS_FAT_CloseFile,
  FS_FAT_Read,
  FS_FAT_Write,
  FS_FAT_Format,
  FS_FAT_OpenDir,
  FS_FAT_CloseDir,
  FS_FAT_ReadDir,
  FS_FAT_RemoveDir,
  FS_FAT_CreateDir,
  FS_FAT_DeleteDir,
  FS_FAT_Rename,
  FS_FAT_Move,
  FS_FAT_SetDirEntryInfo,
  FS_FAT_GetDirEntryInfo,
  FS_FAT_SetEndOfFile,
  FS_FAT_Clean,
  FS_FAT_GetDiskInfo,
  FS_FAT_GetVolumeLabel,
  FS_FAT_SetVolumeLabel,
  FS_FAT_CreateJournalFile,
  FS_FAT_OpenJournalFile,
  FS_FAT_GetIndexOfLastSector,
  FS_FAT_CheckVolume,
  FS_FAT_CloseFile,
  FS_FAT_SetFileSize,
  FS_FAT_FreeSectors,
  FS_FAT_GetFreeSpace,
  FS_FAT_GetATInfo,
  FS_FAT_CheckDir,
  FS_FAT_CheckAT,
  FS_FAT_READ_AT_ENTRY,
  FS_FAT_SetDirEntryInfoEx,
  FS_FAT_GetDirEntryInfoEx
};

#else

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FAT_API_c
*
*  Function description
*    Dummy function to prevent compiler errors.
*/
void FAT_API_c(void);
void FAT_API_c(void){}

#endif

/*************************** End of file ****************************/
