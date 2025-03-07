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
File        : FS_FAT_Move.c
Purpose     : FAT routines for moving files or directory
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
*       FS_FAT_MoveEx
*
*  Function description
*    Move a file or directory to a new location on the same volume.
*
*  Parameters
*    pVolume        Volume where the file or directory is stored.
*    DirStartSrc    Id of the first cluster assigned to the source parent directory.
*    DirStartDest   Id of the first cluster assigned to the destination parent directory.
*    sNameSrcNQ     Name of the source file or directory (not qualified).
*    sNameDestNQ    Name of the destination file or directory (not qualified).
*    pSB            Sector buffer for the access to storage device.
*
*  Return value
*    ==0      OK, file or directory moved.
*    !=0      Error code indicating the failure reason.
*/
int FS_FAT_MoveEx(FS_VOLUME * pVolume, U32 DirStartSrc, U32 DirStartDest, const char * sNameSrcNQ, const char * sNameDestNQ, FS_SB * pSB) {
  unsigned        Date;
  unsigned        Time;
  U32             ClusterId;
  U32             Size;
  unsigned        Attr;
  FS_FAT_DENTRY * pDirEntryCheck;
  FS_FAT_DENTRY * pDirEntry;
  FS_DIR_POS      DirPosLFN;
  U32             SectorIndex;
  int             IsMovePermitted;
  int             r;
  FS_DIR_POS      DirPos;
  FS_FAT_INFO   * pFATInfo;
  U8            * pBuffer;
  I32             DirEntryIndex;

  FS_FAT_InvalidateDirPos(&DirPosLFN);
  //
  // Check that the destination file or directory does not exist.
  //
  pDirEntryCheck = FS_FAT_FindDirEntry(pVolume, pSB, sNameDestNQ, 0, DirStartDest, 0, NULL);
  if(pDirEntryCheck != NULL) {
    return FS_ERRCODE_FILE_DIR_EXISTS;                                          // Error, the destination name already exists.
  }
  //
  // Get the information about the source file or directory.
  //
  pDirEntry = FS_FAT_FindDirEntry(pVolume, pSB, sNameSrcNQ, 0, DirStartSrc, 0, &DirPosLFN);
  if (pDirEntry == NULL) {
    return FS_ERRCODE_FILE_DIR_NOT_FOUND;                                       // Error, source file or directory not found.
  }
  //
  // We need to remember the sector number of the source directory entry
  // because we have to mark the source directory entry as deleted at the
  // end of operation. We also have to calculate the relative directory
  // entry index so that we can check if the file is opened or not.
  //
  pBuffer       = FS__SB_GetBuffer(pSB);
  DirEntryIndex = (I32)(pDirEntry - SEGGER_PTR2PTR(FS_FAT_DENTRY, pBuffer));    //lint !e946 !e947 D:106
  SectorIndex   = FS__SB_GetSectorIndex(pSB);
  //
  // Load the information about the directory entry from the sector buffer.
  //
  Attr      = pDirEntry->Data[DIR_ENTRY_OFF_ATTRIBUTES];
  Time      = FS_LoadU16LE(&pDirEntry->Data[DIR_ENTRY_OFF_CREATION_TIME]);
  Date      = FS_LoadU16LE(&pDirEntry->Data[DIR_ENTRY_OFF_CREATION_DATE]);
  ClusterId = FS_FAT_GetFirstCluster(pDirEntry);
  Size      = FS_LoadU32LE(&pDirEntry->Data[DIR_ENTRY_OFF_SIZE]);
  //
  // Check if the file or directory can be moved.
  //
  IsMovePermitted = 1;
  if ((Attr & FS_FAT_ATTR_READ_ONLY) != 0u) {
#if FS_FAT_PERMIT_RO_FILE_MOVE
    IsMovePermitted = (int)FAT_PermitROFileMove;
#else
    IsMovePermitted = 0;
#endif
  }
  if (IsMovePermitted == 0) {
    return FS_ERRCODE_READ_ONLY_FILE;                                           // Error, source file or directory is read-only and can not be moved.
  }
  //
  // Refuse moving an opened file.
  //
  if ((Attr & FS_FAT_ATTR_DIRECTORY) == 0u) {
    if (FS_FAT_IsFileOpen(pVolume, SectorIndex, (U32)DirEntryIndex) != 0) {
      return FS_ERRCODE_FILE_IS_OPEN;                                           // Error, moving opened files is not permitted.
    }
  }
  //
  // First, mark the volume as dirty.
  //
  FS_FAT_UpdateDirtyFlagIfRequired(pVolume, 1);
  //
  // Create the destination directory entry.
  //
  pDirEntryCheck = FAT_pDirEntryAPI->pfCreateDirEntry(pVolume, pSB, sNameDestNQ, DirStartDest, ClusterId, Attr, Size, Time, Date);
  if (pDirEntryCheck == NULL) {
    return FS_ERRCODE_WRITE_FAILURE;                                            // Error, could not create the directory entry.
  }
  //
  // In case of a directory we also have to update the cluster id of
  // the parent directory that is stored in the ".." directory entry.
  //
  if ((Attr & FS_FAT_ATTR_DIRECTORY) == FS_FAT_ATTR_DIRECTORY) {
    pFATInfo = &pVolume->FSInfo.FATInfo;
    FS_FAT_InitDirEntryScan(pFATInfo, &DirPos, ClusterId);
    pDirEntryCheck = FS_FAT_GetDirEntry(pVolume, pSB, &DirPos);       // Get the "." directory entry which is located on the first position in the directory.
    if (pDirEntryCheck == NULL) {
      return FS_ERRCODE_INVALID_DIRECTORY_ENTRY;                      // Error, no entries found in the subdirectory.
    }
    FS_FAT_IncDirPos(&DirPos);
    pDirEntryCheck = FS_FAT_GetDirEntry(pVolume, pSB, &DirPos);       // Get the ".." directory entry which is located on the second position in the directory.
    if (pDirEntryCheck == NULL) {
      return FS_ERRCODE_INVALID_DIRECTORY_ENTRY;                      // Error, second entry not found in the subdirectory.
    }
    if (FS_MEMCMP(&pDirEntryCheck->Data[0], "..         ", 11) != 0) {
      return FS_ERRCODE_INVALID_DIRECTORY_ENTRY;                      // Error, this is not the ".." directory entry.
    }
    //
    // ".." directory entry found. Link it to the new parent directory.
    //
    FS_FAT_WriteDirEntryCluster(pDirEntryCheck, DirStartDest);
    FS__SB_MarkDirty(pSB);
  }
  //
  // Mark old directory entry as invalid.
  //
  FS__SB_SetSector(pSB, SectorIndex, FS_SECTOR_TYPE_DIR, 1);
  r = FS__SB_Read(pSB);
  if (r != 0) {
    return FS_ERRCODE_READ_FAILURE;
  }
  pDirEntry->Data[0] = DIR_ENTRY_INVALID_MARKER;
  FS__SB_MarkDirty(pSB);
  r = FS_FAT_DelLongDirEntry(pVolume, pSB, &DirPosLFN);
  if (r != 0) {
    return FS_ERRCODE_WRITE_FAILURE;                                  // Error, could not delete the directory entry.
  }
  return FS_ERRCODE_OK;                                               // OK, file or directory moved.
}

/*********************************************************************
*
*       FS_FAT_Move
*
*  Function description
*    Move a file or directory to a new location on the same volume.
*
*  Parameters
*    pVolume      Volume where the file or directory is stored.
*    sNameSrc     Path to the source file or directory (fully qualified).
*    sNameDest    Path to the destination file or directory (fully qualified).
*
*  Return value
*    ==0      OK, file or directory moved.
*    !=0      Error code indicating the failure reason.
*/
int FS_FAT_Move(FS_VOLUME * pVolume, const char * sNameSrc, const char * sNameDest) {
  const char    * sNameDestNQ;
  const char    * sNameSrcNQ;
  int             r;
  FS_SB           sb;
  U32             DirStartSrc;
  U32             DirStartDest;
  int             Result;
  FS_FAT_DENTRY * pDirEntry;
  unsigned        Attr;
  U32             ClusterId;

  //
  // Get a sector buffer for the read and write operations.
  //
  (void)FS__SB_Create(&sb, pVolume);
  //
  // Check that the path to the source file or directory exists.
  //
  Result = FS_FAT_FindPath(pVolume, &sb, sNameSrc, &sNameSrcNQ, &DirStartSrc);
  if (Result == 0) {
    r = FS_ERRCODE_PATH_NOT_FOUND;                // Error, the path to the source parent directory does not exist.
    goto Done;
  }
  //
  // Check that the source file or directory exists.
  //
  pDirEntry = FS_FAT_FindDirEntry(pVolume, &sb, sNameSrcNQ, 0, DirStartSrc, 0, NULL);
  if (pDirEntry == NULL) {
    r = FS_ERRCODE_FILE_DIR_NOT_FOUND;            // Error, the source file or directory does not exist.
    goto Done;
  }
  //
  // An attempt to move a directory in one of its subdirectories
  // is detected by comparing the id of the clusters assigned
  // to the directories in the path to the destination directory
  // with the cluster id assigned to the source directory.
  // This operation is performed in FS_FAT_FindPathEx().
  //
  Attr      = pDirEntry->Data[DIR_ENTRY_OFF_ATTRIBUTES];
  ClusterId = FS_FAT_GetFirstCluster(pDirEntry);
  if ((Attr & FS_FAT_ATTR_DIRECTORY) == 0u) {
    ClusterId = CLUSTER_ID_INVALID;
  }
  //
  // Check that the path to the destination file or directory exists
  // and in case of a directory that the source directory is not moved
  // inside one of its directories.
  //
  Result = FS_FAT_FindPathEx(pVolume, &sb, sNameDest, &sNameDestNQ, &DirStartDest, NULL, ClusterId);
  if (Result == 0) {
    r = FS_ERRCODE_PATH_NOT_FOUND;                // Error, the path to the destination parent directory does not exist.
    goto Done;
  }
  //
  // Use the source file or directory name if no destination name is specified.
  //
  if (*sNameDestNQ == '\0') {
    sNameDestNQ = sNameSrcNQ;
  }
  //
  // Perform the actual operation. The checking if the destination file or directory name
  // does not exist is performed in FS__FAT_Move() because this function is also used
  // for the rename operation.
  //
  r = FS_FAT_MoveEx(pVolume, DirStartSrc, DirStartDest, sNameSrcNQ, sNameDestNQ, &sb);
Done:
  //
  // Clean up.
  //
  FS__SB_Delete(&sb);
  if (r == 0) {
    r = FS__SB_GetError(&sb);
  }
  return r;
}

/*************************** End of file ****************************/

