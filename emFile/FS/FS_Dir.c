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
File        : FS_Dir.c
Purpose     : Directory support functions
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
    (void)FS_CREATE_DIR(pVolume, sDirName);         // Perform the operation without journal.
  }
  return r;
}

/*********************************************************************
*
*       _RemoveDirFS
*/
static int _RemoveDirFS(FS_VOLUME * pVolume, const char * sDirName) {
  int r;
  int Result;

  r = FS__JOURNAL_Begin(pVolume);
  if (r == 0) {
    r = FS_REMOVE_DIR(pVolume, sDirName);
    FS__JOURNAL_SetError(pVolume, r);
    Result = FS__JOURNAL_End(pVolume);
    if (Result != 0) {
      r = Result;
    }
  } else {
    (void)FS_REMOVE_DIR(pVolume, sDirName);         // Perform the operation without journal.
  }
  return r;
}

/*********************************************************************
*
*       _DeleteDirFS
*/
static int _DeleteDirFS(FS_VOLUME * pVolume, const char * sDirName, int MaxRecursionLevel) {
  int r;
  int Result;

  r = FS__JOURNAL_Begin(pVolume);
  if (r == 0) {
    r = FS_DELETE_DIR(pVolume, sDirName, MaxRecursionLevel);
    FS__JOURNAL_SetError(pVolume, r);
    Result = FS__JOURNAL_End(pVolume);
    if (Result != 0) {
      r = Result;
    }
  } else {
    (void)FS_DELETE_DIR(pVolume, sDirName, MaxRecursionLevel);        // Perform the operation without journal.
  }
  return r;
}

#endif // FS_SUPPORT_JOURNAL

/*********************************************************************
*
*       Public code (internal version)
*
**********************************************************************
*/

/*********************************************************************
*
*       FS__MkDirEx
*
*  Function description
*    Creates a directory.
*
*  Parameters
*    pVolume    Volume on which the directory is located.
*    sDirName   Directory name (not qualified).
*
*  Return value
*    ==0        OK, directory has been created
*    !=0        Error code indicating the failure reason
*/
int FS__MkDirEx(FS_VOLUME * pVolume, const char * sDirName) {
  int r;

  r = FS__AutoMount(pVolume);
  switch ((unsigned)r) {
  case FS_MOUNT_RW:
    //
    // Execute the FSL function.
    //
    FS_LOCK_DRIVER(&pVolume->Partition.Device);
#if FS_SUPPORT_JOURNAL
    r = _CreateDirFS(pVolume, sDirName);
#else
    r = FS_CREATE_DIR(pVolume, sDirName);
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
*       FS__MkDir
*
*  Function description
*    Creates a directory.
*
*  Parameters
*    sDirName   Fully qualified directory name.
*
*  Return value
*    ==0        OK, directory has been created
*    !=0        Error code indicating the failure reason
*
*  Additional information
*    Internal version of FS_MkDir().
*/
int FS__MkDir(const char * sDirName) {
  int          r;
  FS_VOLUME  * pVolume;
  const char * sDirNameNQ;

  r          = FS_ERRCODE_VOLUME_NOT_FOUND;
  sDirNameNQ = NULL;
  pVolume = FS__FindVolumeEx(sDirName, &sDirNameNQ);
  if (pVolume != NULL) {
    if ((sDirNameNQ != NULL) && (*sDirNameNQ != '\0')) {
      r = FS__MkDirEx(pVolume, sDirNameNQ);
    } else {
      r = FS_ERRCODE_INVALID_PARA;
    }
  }
  return r;
}

/*********************************************************************
*
*       FS__RmDirEx
*
*  Function description
*    Removes a directory if it is empty.
*
*  Parameters
*    pVolume    Volume on which the directory is located.
*    sDirName   Partially qualified directory name.
*
*  Return value
*    ==0        OK, directory has been removed.
*    !=0        Error code indicating the failure reason.
*/
int FS__RmDirEx(FS_VOLUME * pVolume, const char * sDirName) {
  FS_FIND_DATA fd;
  int          NumFiles;
  int          r;
  char         c;

  //
  // Check if the directory exists.
  //
  r = FS__FindFirstFileEx(&fd, pVolume, sDirName, &c, 1);
  FS_USE_PARA(c);  // 'c' is not really used.
  if (r == 0) {
    //
    // Check if directory is empty.
    //
    NumFiles = 0;
    for (;;) {
      NumFiles++;
      //
      // Do not delete directory if not empty.
      //
      if (NumFiles > 2) {       // If is more than '..' and '.'
        FS__FindClose(&fd);
        return FS_ERRCODE_DIR_NOT_EMPTY;
      }
      r = FS__FindNextFile(&fd);
      if (r < 0) {              // An error occurred?
        return r;
      }
      if (r == 1) {             // End of directory reached?
        break;
      }
    }
    FS__FindClose(&fd);
    //
    // Get the correct pVolume instance.
    //
    if (pVolume->MountType == FS_MOUNT_RW) {
      //
      // Remove the directory.
      //
      FS_LOCK_DRIVER(&pVolume->Partition.Device);
#if FS_SUPPORT_JOURNAL
      r = _RemoveDirFS(pVolume, sDirName);
#else
      r = FS_REMOVE_DIR(pVolume, sDirName);
#endif // FS_SUPPORT_JOURNAL
      FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
    } else {
      r = FS_ERRCODE_READ_ONLY_VOLUME;
    }
  }
  return r;
}

/*********************************************************************
*
*       FS__RmDir
*
*  Function description
*    Removes a directory if it is empty.
*
*  Parameters
*    sDirName   Fully qualified directory name.
*
*  Return value
*    ==0        OK, directory has been removed.
*    !=0        Error code indicating the failure reason.
*
*  Additional information
*    Internal version of FS_RmDir.
*/
int FS__RmDir(const char * sDirName) {
  FS_VOLUME  * pVolume;
  int          r;
  const char * sDirNameNQ;

  r          = FS_ERRCODE_VOLUME_NOT_FOUND;
  sDirNameNQ = NULL;
  pVolume = FS__FindVolumeEx(sDirName, &sDirNameNQ);
  if (pVolume != NULL) {
    if ((sDirNameNQ != NULL) && (*sDirNameNQ != '\0')) {
      r = FS__RmDirEx(pVolume, sDirNameNQ);
    } else {
      r = FS_ERRCODE_INVALID_PARA;
    }
  }
  return r;
}

/*********************************************************************
*
*       FS__DeleteDir
*
*  Function description
*    Removes a directory and all the files and directories in it.
*
*  Parameters
*    sDirName           Fully qualified directory name.
*    MaxRecursionLevel  Maximum depth of the directory tree.
*
*  Return value
*    ==0        OK, directory has been removed.
*    !=0        Error code indicating the failure reason.
*
*  Additional information
*    Internal version of FS_DeleteDir.
*/
int FS__DeleteDir(const char * sDirName, int MaxRecursionLevel) {
  int          r;
  FS_VOLUME  * pVolume;
  const char * s;

  r = FS_ERRCODE_VOLUME_NOT_FOUND;
  pVolume = FS__FindVolumeEx(sDirName, &s);
  if (pVolume != NULL) {
    r = FS__AutoMount(pVolume);
    switch ((unsigned)r) {
    case FS_MOUNT_RW:
      //
      // Execute the FSL function.
      //
      FS_LOCK_DRIVER(&pVolume->Partition.Device);
#if FS_SUPPORT_JOURNAL
      r = _DeleteDirFS(pVolume, s, MaxRecursionLevel);
#else
      r = FS_DELETE_DIR(pVolume, s, MaxRecursionLevel);
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
      // An error occurred during the mount operation.
      //
      break;
    }
  }
  return r;
}

/*********************************************************************
*
*       FS__FindFirstFileEx
*
*  Function description
*    This function initializes the pFD structure with the information
*    necessary to open a directory for listing.
*    It also retrieves the first entry in the given directory.
*
*  Parameters
*    pFD              Search context.
*    pVolume          Volume instance.
*    sDirName         Path to the directory to be searched.
*    sFileName        Pointer to a buffer that receives the file name.
*    SizeOfFileName   Size in bytes of the sFileName buffer.
*
*  Return value
*    ==1    No entries available in directory.
*    ==0    O.K., first file found.
*    < 0    Error code indicating the failure reason.
*/
int FS__FindFirstFileEx(FS_FIND_DATA * pFD, FS_VOLUME * pVolume, const char * sDirName, char * sFileName, int SizeOfFileName) {
  int                r;
  int                MountType;
  FS_DIR_OBJ       * pDirObj;
  FS_DIRENTRY_INFO   DirEntryInfo;

  //
  // Mount the volume if necessary.
  //
  MountType = FS__AutoMount(pVolume);
  if (((unsigned)MountType & FS_MOUNT_R) != 0u) {
    FS_MEMSET(pFD, 0, sizeof(FS_FIND_DATA));
    FS_MEMSET(&DirEntryInfo, 0, sizeof(FS_DIRENTRY_INFO));
    DirEntryInfo.sFileName      = sFileName;
    DirEntryInfo.SizeofFileName = SizeOfFileName;
    pDirObj          = &pFD->Dir.DirObj;
    pDirObj->pVolume = pVolume;
    FS_LOCK_DRIVER(&pVolume->Partition.Device);
    if (pVolume->MountType != 0u) {
      r = FS_OPENDIR(sDirName, pDirObj);
      if (r == FS_ERRCODE_OK) {
        r = FS_READDIR(pDirObj, &DirEntryInfo);
        if (r == FS_ERRCODE_OK) {
          pFD->Attributes      = DirEntryInfo.Attributes;
          pFD->CreationTime    = DirEntryInfo.CreationTime;
          pFD->FileSize        = DirEntryInfo.FileSize;
          pFD->LastAccessTime  = DirEntryInfo.LastAccessTime;
          pFD->LastWriteTime   = DirEntryInfo.LastWriteTime;
          pFD->sFileName       = DirEntryInfo.sFileName;
          pFD->SizeofFileName  = DirEntryInfo.SizeofFileName;
          pFD->Dir.InUse       = 1;
        }
      }
    } else {
      FS_DEBUG_ERROROUT((FS_MTYPE_API, "Application error: Volume has been unmounted by another task."));
      r = FS_ERRCODE_VOLUME_NOT_MOUNTED;
    }
    FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
  } else {
    if (MountType < 0) {
      r = MountType;
    } else {
      r = FS_ERRCODE_VOLUME_NOT_MOUNTED;
    }
  }
  return r;
}

/*********************************************************************
*
*       FS__FindFirstFile
*
*  Function description
*    This function calls the FS__FindFirstFileEx function to
*    open a directory for directory scanning.
*
*  Parameters
*    pFD              Directory scan context.
*    sDirName         Path to the directory to be searched.
*    sFileName        Pointer to a buffer for storing the file name.
*    SizeOfFileName   Size in bytes of the sFileName buffer.
*
*  Return value
*    ==0    O.K.
*    ==1    No entries available in directory.
*    < 0    Error code indicating the failure reason.
*/
int FS__FindFirstFile(FS_FIND_DATA * pFD, const char * sDirName, char * sFileName, int SizeOfFileName) {
  int          r;
  FS_VOLUME  * pVolume;
  const char * s;

  if ((sFileName == NULL) || (SizeOfFileName == 0)) {
    return FS_ERRCODE_INVALID_PARA;       // Error, no valid buffer specified for the file name.
  }
  r       = FS_ERRCODE_VOLUME_NOT_FOUND;  // Set as error so far.
  pVolume = FS__FindVolumeEx(sDirName, &s);
  if (pVolume != NULL) {
    r = FS__FindFirstFileEx(pFD, pVolume, s, sFileName, SizeOfFileName);
  }
  return r;
}

/*********************************************************************
*
*       FS__FindNextFile
*
*  Function description
*    Searches for the next directory entry in the directory.
*
*  Parameters
*    pFD      Directory scan context.
*
*  Return value
*    ==1    No more files or directories found.
*    ==0    OK, file or directory found.
*    < 0    Error code indicating the failure reason.
*/
int FS__FindNextFile(FS_FIND_DATA * pFD) {
  int                r;
  FS_DIR_OBJ       * pDirObj;
  FS_DIRENTRY_INFO   DirEntryInfo;
  FS_VOLUME        * pVolume;

  r = FS_ERRCODE_VOLUME_NOT_MOUNTED;
  FS_MEMSET(&DirEntryInfo, 0, sizeof(FS_DIRENTRY_INFO));
  DirEntryInfo.sFileName      = pFD->sFileName;
  DirEntryInfo.SizeofFileName = pFD->SizeofFileName;
  FS_LOCK_SYS();
  pDirObj = &pFD->Dir.DirObj;
  pVolume = pDirObj->pVolume;
  FS_UNLOCK_SYS();
  FS_LOCK_DRIVER(&pVolume->Partition.Device);
  if (pVolume->MountType != 0u) {
    r = FS_READDIR(pDirObj, &DirEntryInfo);
    if (r == 0) {
      pFD->Attributes      = DirEntryInfo.Attributes;
      pFD->CreationTime    = DirEntryInfo.CreationTime;
      pFD->FileSize        = DirEntryInfo.FileSize;
      pFD->LastAccessTime  = DirEntryInfo.LastAccessTime;
      pFD->LastWriteTime   = DirEntryInfo.LastWriteTime;
      pFD->sFileName       = DirEntryInfo.sFileName;
    }
  } else {
    //
    // Error, the volume has been unmounted by another task.
    //
    FS_DEBUG_ERROROUT((FS_MTYPE_API, "FS__FindNextFile: Volume has been unmounted by another task."));
  }
  FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
  return r;
}

/*********************************************************************
*
*       FS__FindClose
*
*  Function description
*    Closes a directory scan operation.
*
*  Parameters
*    pFD        Directory scan context.
*/
void FS__FindClose(FS_FIND_DATA * pFD) {
  FS_LOCK_SYS();
  pFD->Dir.DirObj.pVolume = NULL;
  pFD->Dir.InUse          = 0;
  FS_UNLOCK_SYS();
}

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_MkDir
*
*  Function description
*    Creates a directory.
*
*  Parameters
*    sDirName     Directory name.
*
*  Return value
*    ==0        OK, directory has been created
*    !=0        Error code indicating the failure reason
*
*  Additional information
*    The function fails if a directory with the same name already
*    exists in the target directory. FS_MkDir() expects that all
*    the directories in the path to the created directory exists.
*    Otherwise the function returns with an error.
*    FS_CreateDir() can be used to create a directory along with
*    any missing directories in the path.
*/
int FS_MkDir(const char * sDirName) {
  int r;

  FS_LOCK();
  r = FS__MkDir(sDirName);
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_RmDir
*
*  Function description
*    Removes a directory.
*
*  Parameters
*    sDirName   Directory name.
*
*  Return value
*    ==0        OK, directory has been removed
*    !=0        Error code indicating the failure reason
*
*  Additional information
*    The function fails if the directory is not empty.
*    FS_DeleteDir() can be used to remove a directory and its contents.
*/
int FS_RmDir(const char * sDirName) {
  int    r;

  FS_LOCK();
  r = FS__RmDir(sDirName);
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_DeleteDir
*
*  Function description
*    Removes a directory and its contents.
*
*  Parameters
*    sDirName           Directory name.
*    MaxRecursionLevel  Maximum depth of the directory tree.
*
*  Return value
*    ==0        OK, directory has been removed
*    !=0        Error code indicating the failure reason
*
*  Additional information
*    The function uses recursion to process the directory tree and
*    it requires about 100 bytes of stack for each directory level
*    it processes. The MaxRecursionLevel parameter can be used to
*    prevent a stack overflow if the directory tree is too deep.
*    For example:
*    * MaxRecursionLevel==0
*      Deletes the directory only if empty else an error is
*      reported and the directory is not deleted.
*    * MaxRecursionLevel==1
*      Deletes the directory and all the files in it.
*      If a subdirectory is present an error is reported
*      and the directory is not deleted.
*    * MaxRecursionLevel==2
*      Deletes the directory and all the files and subdirectories
*      in it. If the subdirectories contain other subdirectories
*      an error is reported and the directory is not deleted.
*    * And so on...
*/
int FS_DeleteDir(const char * sDirName, int MaxRecursionLevel) {
  int    r;

  FS_LOCK();
  r = FS__DeleteDir(sDirName, MaxRecursionLevel);
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_FindFirstFile
*
*  Function description
*    Initiates a directory scanning operation and returns information
*    about the first file or directory.
*
*  Parameters
*    pFD              Context of the directory scanning operation.
*    sDirName         Name of the directory to search in.
*    sFileName        Buffer that receives the name of the file found.
*    SizeOfFileName   Size in bytes of the sFileName buffer.
*
*  Return value
*    ==0    O.K.
*    ==1    No files or directories available in directory.
*    < 0    Error code indicating the failure reason.
*
*  Additional information
*    This function returns information about the first file or
*    directory found in the directory. This is typically the special
*    directory name "." when searching in a directory other than root.
*    FS_FindFirstFile() and FS_FindNextFile() can be used to list
*    all the files and directories in a directory.
*
*    The name of the file or directory is returned in sFileName.
*    FS_FindFirstFile() stores at most SizeOfFileName - 1 characters
*    in the buffer and it terminates the name of file or directory
*    by a 0 character. The file or directory name is truncated if it
*    contains more characters than the number of characters that can
*    be stored in sFileName. For files or directories stored on a FAT
*    volume with LFN support the name is truncated by returning the
*    short version (8.3 format) of the file or directory name.
*
*    Alternatively, the file name can be accessed via the sFileName
*    member of the pFD structure. Additional information about the
*    file or directory such as size, attributes and timestamps are
*    returned via the corresponding members of pFD structure.
*    For more information refer to FS_FIND_DATA.
*
*    The returned file or directory does not necessarily be the
*    first file or directory created in the root directory. The name
*    of the first file or directory can change as files are deleted
*    and created in the root directory.
*/
int FS_FindFirstFile(FS_FIND_DATA * pFD, const char * sDirName, char * sFileName, int SizeOfFileName) {
  int r;

  FS_LOCK();
  r = FS__FindFirstFile(pFD, sDirName, sFileName, SizeOfFileName);
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_FindNextFile
*
*  Function description
*    Returns information about the next file or directory
*    in a directory scanning operation.
*
*  Parameters
*    pFD    Context of the directory scanning operation.
*
*  Return value
*    ==1    O.K.
*    ==0    Error occurred or end of directory reached.
*
*  Additional information
*    FS_FindNextFile() returns information about the next file
*    or directory located after the file or directory returned via
*    a previous call to FS_FindFirstFile() or FS_FindNextFile()
*    within the same listing context.
*
*    The name of the file or directory is returned in sFileName
*    member of pFD. FS_FindNextFile() stores at most one less than the
*    buffer size characters in the buffer and it terminates the name of
*    the file or directory by a 0 character. The buffer for the file or
*    directory name is specified in the call to FS_FindFirstFile().
*    The file or directory name is truncated if it contains
*    more characters than the number of characters that can be stored
*    in sFileName. For files or directories stored on a FAT volume with
*    LFN support the name is truncated by returning the short version
*    (8.3 format) of the file or directory name.
*
*    Additional information about the file or directory such as size,
*    attributes and timestamps are returned via the corresponding members
*    of pFD structure. For more information refer to FS_FIND_DATA.
*
*    FS_FindFirstFile() has to be called first to initialize the
*    listing operation. It is allowed to perform operations
*    on files or directories (creation, deletion, etc.) within
*    the listing loop.
*/
int FS_FindNextFile(FS_FIND_DATA * pFD) {
  int r;

  FS_LOCK();
  r = FS__FindNextFile(pFD);
  if (r == 0) {
    r = 1;
  } else {
    r = 0;
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_FindNextFileEx
*
*  Function description
*    Returns information about the next file or directory
*    in a directory scanning operation.
*
*  Parameters
*    pFD    Context of the directory scanning operation.
*
*  Return value
*    ==1    No files or directories available in directory.
*    ==0    OK, information about the file or directory returned.
*    < 0    Error code indicating the failure reason.
*
*  Additional information
*    FS_FindNextFileEx() performs the same operation as FS_FindNextFile()
*    with the difference that its return value is consistent with the
*    return value of the other emFile API functions.
*/
int FS_FindNextFileEx(FS_FIND_DATA * pFD) {
  int r;

  FS_LOCK();
  r = FS__FindNextFile(pFD);
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_FindClose
*
*  Function description
*    Ends a directory scanning operation.
*
*  Parameters
*    pFD    Context of the directory scanning operation.
*
*  Additional information
*    This function has to be called at the end of the directory
*    scanning operation to clear the used context. After calling
*    this function the same context can be used for a different
*    directory scanning operation.
*/
void FS_FindClose(FS_FIND_DATA * pFD) {
  FS_LOCK();
  FS__FindClose(pFD);
  FS_UNLOCK();
}

/*************************** End of file ****************************/
