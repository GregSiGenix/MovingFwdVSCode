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
File        : FS_DirOld.c
Purpose     : Obsolete directory functions
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
*       Static data
*
**********************************************************************
*/
static FS_DIR    _aDirHandle[FS_NUM_DIR_HANDLES];
static FS_DIRENT _aDirEntry[FS_NUM_DIR_HANDLES];

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

//lint -efunc(818, FS_DirEnt2Attr) pDirEnt could be declared as pointing to const N:104. Rationale: pDirEnt cannot be declared as pointing to const because FS_DirEnt2Attr() is an API function.
//lint -efunc(818, FS_DirEnt2Name) pDirEnt could be declared as pointing to const N:104. Rationale: pDirEnt cannot be declared as pointing to const because FS_DirEnt2Name() is an API function.
//lint -efunc(818, FS_DirEnt2Size) pDirEnt could be declared as pointing to const N:104. Rationale: pDirEnt cannot be declared as pointing to const because FS_DirEnt2Size() is an API function.
//lint -efunc(818, FS_DirEnt2Time) pDirEnt could be declared as pointing to const N:104. Rationale: pDirEnt cannot be declared as pointing to const because FS_DirEnt2Time() is an API function.

/*********************************************************************
*
*       _AllocDirHandle
*/
static FS_DIR * _AllocDirHandle(void) {
  FS_DIR   * pDir;
  unsigned   i;

  pDir = NULL;
  FS_LOCK_SYS();
  for (i = 0; i < SEGGER_COUNTOF(_aDirHandle); i++) {
    if (_aDirHandle[i].InUse == 0u) {
      pDir = &_aDirHandle[i];
      pDir->pDirEntry = &_aDirEntry[i];
      pDir->InUse     = 1;
      break;
    }
  }
  FS_UNLOCK_SYS();
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  if (pDir == NULL) {
    FS_DEBUG_ERROROUT((FS_MTYPE_API, "_AllocDirHandle: No directory handle available."));
  }
#endif
  return pDir;
}

/*********************************************************************
*
*       _FreeDirHandle
*/
static void _FreeDirHandle(FS_DIR * pHandle) {
  if (pHandle != NULL) {
    FS_LOCK_SYS();
    pHandle->InUse = 0u;
    FS_UNLOCK_SYS();
  }
}

/*********************************************************************
*
*       Public code (internal)
*
**********************************************************************
*/

/*********************************************************************
*
*       FS__OpenDirEx
*
*  Function description
*    Opens an existing directory for reading.
*
*  Parameters
*    pVolume      Volume on which the directory is located.
*    sDirName     Directory name (Not qualified).
*
*  Return value
*    ==0      Unable to open the directory.
*    !=0      Address of an FS_DIR data structure.
*/
FS_DIR * FS__OpenDirEx(FS_VOLUME * pVolume, const char * sDirName) {
  FS_DIR * pDirHandle;
  int      r;

  pDirHandle = NULL;
  if (((unsigned)FS__AutoMount(pVolume) & FS_MOUNT_R) != 0u)  {
    //
    //  Find next free entry in the list of handles.
    //
    pDirHandle = _AllocDirHandle();
    if (pDirHandle != NULL) {
      pDirHandle->DirObj.pVolume = pVolume;
      FS_LOCK_DRIVER(&pVolume->Partition.Device);
      r = FS_OPENDIR(sDirName, &pDirHandle->DirObj);
      FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
      if (r != 0) {
        _FreeDirHandle(pDirHandle);
        pDirHandle = NULL;
      }
    }
  }
  return pDirHandle;
}

/*********************************************************************
*
*       FS__OpenDir
*
*  Function description
*    Internal version of FS_OpenDir.
*    Opens an existing directory for reading.
*
*  Parameters
*    sDirName     Fully qualified directory name.
*
*  Return value
*    ==0      Unable to open the directory.
*    !=0      Address of an FS_DIR data structure.
*/
FS_DIR * FS__OpenDir(const char * sDirName) {
  FS_DIR     * pDirHandle;
  FS_VOLUME  * pVolume;
  const char * sDirNameNQ;

  pDirHandle = NULL;
  //
  // Find correct volume.
  //
  sDirNameNQ = NULL;
  pVolume = FS__FindVolumeEx(sDirName, &sDirNameNQ);
  if (pVolume != NULL) {
    if (sDirNameNQ != NULL) {
      pDirHandle = FS__OpenDirEx(pVolume, sDirNameNQ);
    }
  }
  return pDirHandle;
}

/*********************************************************************
*
*       FS__ReadDir
*
*  Function description
*    Internal version of FS_ReadDir.
*    Read next directory entry in directory specified by pDir.
*
*  Parameters
*    pDir     Pointer to a FS_DIR data structure.
*
*  Return value
*    ==0      No more directory entries or error.
*    !=0      Pointer to a directory entry.
*/
FS_DIRENT * FS__ReadDir(FS_DIR * pDir) {
  FS_DIRENT        * pDirEntry;
  FS_DIRENTRY_INFO   DirEntryInfo;
  FS_VOLUME        * pVolume;

  pDirEntry = NULL;
  if (pDir != NULL) {
    pVolume = pDir->DirObj.pVolume;
    if (pVolume != NULL) {
      FS_MEMSET(&DirEntryInfo, 0, sizeof(FS_DIRENTRY_INFO));
      DirEntryInfo.sFileName      = &pDir->pDirEntry->DirName[0];
      DirEntryInfo.SizeofFileName = (int)sizeof(pDir->pDirEntry->DirName);
      FS_LOCK_DRIVER(&pVolume->Partition.Device);
      if (FS_READDIR(&pDir->DirObj, &DirEntryInfo) == 0) {
        pDirEntry = pDir->pDirEntry;
        pDirEntry->Attributes = DirEntryInfo.Attributes;
        pDirEntry->Size       = DirEntryInfo.FileSize;
        pDirEntry->TimeStamp  = DirEntryInfo.CreationTime;
      }
      FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
    }
  }
  return pDirEntry;
}

/*********************************************************************
*
*       FS__CloseDir
*
*  Function description
*    Internal version of FS_CloseDir.
*    Close a directory referred by pDir.
*
*  Parameters
*    pDir     Pointer to a FS_DIR data structure.
*
*  Return value
*    ==0      Directory has been closed.
*    ==-1     Unable to close directory.
*/
int FS__CloseDir(FS_DIR * pDir) {
  int         r;
  FS_VOLUME * pVolume;

  r = -1;
  if (pDir != NULL) {
    pVolume = pDir->DirObj.pVolume;
    if (pVolume != NULL) {
      FS_LOCK_DRIVER(&pVolume->Partition.Device);
      r = FS_CLOSEDIR(&pDir->DirObj);
      FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
    }
  }
  _FreeDirHandle(pDir);
  return r;
}

/*********************************************************************
*
*       FS__RewindDir
*
*  Function description
*    Internal version of FS_RewindDir.
*    Set pointer for reading the next directory entry to
*    the first entry in the directory.
*
*  Parameters
*    pDir     Pointer to a FS_DIR data structure.
*/
void FS__RewindDir(FS_DIR * pDir) {
  if (pDir != NULL) {
    FS_LOCK_SYS();
    pDir->DirObj.DirPos.DirEntryIndex = 0u;       // Only rewind, if we have a valid pointer.
    FS_UNLOCK_SYS();
  }
}

/*********************************************************************
*
*       FS__DirEnt2Attr
*
*  Function description
*    Internal version of FS_DirEnt2Attr.
*    Gets the directory entry attributes.
*
*  Parameters
*    pDirEnt    Pointer to a FS_DIRENT data structure.
*    pAttr      Pointer to a buffer to be copied to.
*/
void FS__DirEnt2Attr(const FS_DIRENT * pDirEnt, U8 * pAttr) {
  if (pDirEnt != NULL) {
    if (pAttr != NULL) {
      *pAttr = pDirEnt->Attributes;
    }
  }
}

/*********************************************************************
*
*       FS__IsDirHandle
*/
int FS__IsDirHandle(const FS_DIR * pDir) {
  FS_DIR   * pDirToCheck;
  unsigned   i;

  pDirToCheck = _aDirHandle;
  FS_LOCK_SYS();
  for (i = 0; i < SEGGER_COUNTOF(_aDirHandle); i++) {
    if (pDirToCheck == pDir) {
      return 1;                   // Is a directory handle.
    }
    ++pDirToCheck;
  }
  FS_UNLOCK_SYS();
  return 0;                       // Is not a directory handle.
}

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_OpenDir
*
*  Function description
*    API function. Open an existing directory for reading.
*
*  Parameters
*    sDirName   Fully qualified directory name.
*
*  Return value
*    ==0        Unable to open the directory.
*    !=0        Address of an FS_DIR data structure.
*/
FS_DIR * FS_OpenDir(const char * sDirName) {
  FS_DIR * pHandle;

  FS_LOCK();
  pHandle = FS__OpenDir(sDirName);
  FS_UNLOCK();
  return pHandle;
}


/*********************************************************************
*
*       FS_CloseDir
*
*  Function description
*    Closes a directory.
*
*  Parameters
*    pDir     Handle to an opened directory.
*
*  Return value
*    ==0      Directory has been closed.
*    !=0      An error occurred.
*/
int FS_CloseDir(FS_DIR * pDir) {
  int r;

  FS_LOCK();
  r = FS__CloseDir(pDir);
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_ReadDir
*
*  Function description
*    Reads next directory entry in directory.
*
*  Parameters
*    pDir     Handle to an opened directory.
*
*  Return value
*    !=0      Pointer to a directory entry.
*    ==0      No more directory entries or error.
*/
FS_DIRENT * FS_ReadDir(FS_DIR * pDir) {
  FS_DIRENT * pDirEnt;

  FS_LOCK();
  pDirEnt = FS__ReadDir(pDir);
  FS_UNLOCK();
  return pDirEnt;
}

/*********************************************************************
*
*       FS_RewindDir
*
*  Function description
*    Sets pointer for reading the next directory entry to
*    the first entry in the directory.
*
*  Parameters
*    pDir     Handle to an opened directory.
*/
void FS_RewindDir(FS_DIR * pDir) {
  FS_LOCK();
  FS__RewindDir(pDir);
  FS_UNLOCK();
}

/*********************************************************************
*
*       FS_DirEnt2Attr
*
*  Function description
*    Loads attributes of a directory entry.
*
*  Parameters
*    pDirEnt    [IN]  Data of directory entry.
*    pAttr      [OUT] Pointer to a memory location that
*                     receives the attributes.
*/
void FS_DirEnt2Attr(FS_DIRENT * pDirEnt, U8 * pAttr) {
  FS_LOCK();
  FS__DirEnt2Attr(pDirEnt, pAttr);
  FS_UNLOCK();
}

/*********************************************************************
*
*       FS_DirEnt2Name
*
*  Function description
*    Loads the name of a directory entry.
*
*  Parameters
*    pDirEnt    [IN]  Data of directory entry.
*    pBuffer    [OUT] Pointer to a memory location that
*                     receives the name.
*/
void FS_DirEnt2Name(FS_DIRENT * pDirEnt, char * pBuffer) {
  unsigned NumBytes;

  FS_LOCK();
  if (pDirEnt != NULL) {
    if (pBuffer != NULL) {
      NumBytes = sizeof(pDirEnt->DirName) - 1u;
      (void)FS_STRNCPY(pBuffer, pDirEnt->DirName, NumBytes);
      pBuffer[NumBytes] = '\0';
    }
  }
  FS_UNLOCK();
}

/*********************************************************************
*
*       FS_DirEnt2Size
*
*  Function description
*    Loads the size of a directory entry.
*
*  Parameters
*    pDirEnt    [IN]  Data of directory entry.
*
*  Return value
*    Size of the file or directory.
*/
U32 FS_DirEnt2Size(FS_DIRENT * pDirEnt) {
  U32 r;

  FS_LOCK();
  r = 0;
  if (pDirEnt != NULL) {
    r = (U32)pDirEnt->Size;
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_DirEnt2Time
*
*  Function description
*    Loads the time stamp of a directory entry.
*
*  Parameters
*    pDirEnt    [IN]  Data of directory entry.
*
*  Return value
*    Time stamp of the file or directory.
*/
U32 FS_DirEnt2Time(FS_DIRENT * pDirEnt) {
  U32 r;

  r = 0;
  FS_LOCK();
  if (pDirEnt != NULL) {
    r = pDirEnt->TimeStamp;
  }
  FS_UNLOCK();
  return r;
}

/*************************** End of file ****************************/
