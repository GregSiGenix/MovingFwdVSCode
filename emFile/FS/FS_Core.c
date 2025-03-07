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
File        : FS_Core.c
Purpose     : File system's core routines
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#include <stdlib.h>
#include "FS_Int.h"

//lint -efunc(818, FS_Free) pData could be declared as pointing to const N:104. Rationale: this is an API function and we want to be flexible about what we can do with pData.

/*********************************************************************
*
*       Defines, fixed
*
**********************************************************************
*/

/*********************************************************************
*
*       CALL_TEST_HOOK_MEM_ALLOC_BEGIN
*/
#if FS_SUPPORT_TEST
  #define CALL_TEST_HOOK_MEM_ALLOC_BEGIN(pNumBytes)                   _CallTestHookMemAllocBegin(NULL, pNumBytes)
#else
  #define CALL_TEST_HOOK_MEM_ALLOC_BEGIN(pNumBytes)
#endif

/*********************************************************************
*
*       CALL_TEST_HOOK_MEM_ALLOC_END
*/
#if FS_SUPPORT_TEST
  #define CALL_TEST_HOOK_MEM_ALLOC_END(NumBytes, ppData)              _CallTestHookMemAllocEnd(NULL, NumBytes, ppData)
#else
  #define CALL_TEST_HOOK_MEM_ALLOC_END(NumBytes, ppData)
#endif

#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ALL)

/*********************************************************************
*
*       CALL_TEST_HOOK_MEM_ALLOC_BEGIN_EX
*/
#if FS_SUPPORT_TEST
  #define CALL_TEST_HOOK_MEM_ALLOC_BEGIN_EX(sDesc, pNumBytes)         _CallTestHookMemAllocBegin(sDesc, pNumBytes)
#else
  #define CALL_TEST_HOOK_MEM_ALLOC_BEGIN_EX(sDesc, pNumBytes)
#endif

/*********************************************************************
*
*       CALL_TEST_HOOK_MEM_ALLOC_END_EX
*/
#if FS_SUPPORT_TEST
  #define CALL_TEST_HOOK_MEM_ALLOC_END_EX(sDesc, NumBytes, ppData)    _CallTestHookMemAllocEnd(sDesc, NumBytes, ppData)
#else
  #define CALL_TEST_HOOK_MEM_ALLOC_END_EX(sDesc, NumBytes, ppData)
#endif

#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ALL

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
#if FS_SUPPORT_TEST
  static FS_TEST_HOOK_MEM_ALLOC_BEGIN * _pfTestHookMemAllocBegin;
  static FS_TEST_HOOK_MEM_ALLOC_END   * _pfTestHookMemAllocEnd;
#endif // FS_SUPPORT_TEST

/*********************************************************************
*
*       Public data
*
**********************************************************************
*/
FS_GLOBAL FS_Global;

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

#if FS_SUPPORT_TEST

/*********************************************************************
*
*       _CallTestHookMemAllocBegin
*/
static void _CallTestHookMemAllocBegin(const char * sDesc, I32 * pNumBytes) {
  if (_pfTestHookMemAllocBegin != NULL) {
    _pfTestHookMemAllocBegin(sDesc, pNumBytes);
  }
}

/*********************************************************************
*
*       _CallTestHookMemAllocEnd
*/
static void _CallTestHookMemAllocEnd(const char * sDesc, I32 NumBytes, void ** ppData) {
  if (_pfTestHookMemAllocEnd != NULL) {
    _pfTestHookMemAllocEnd(sDesc, NumBytes, ppData);
  }
}

#endif // FS_SUPPORT_TEST

#if FS_SUPPORT_DEINIT

/*********************************************************************
*
*       _RemoveFileHandles
*
*  Function description
*    Frees the memory allocated for all the file handles.
*/
static void _RemoveFileHandles(void) {
  FS_FILE        * pFile;
  FS_FILE        * pFileNext;
#if FS_SUPPORT_FILE_BUFFER
  FS_FILE_BUFFER * pFileBuffer;
#endif // FS_SUPPORT_FILE_BUFFER

  pFile = FS_Global.pFirstFileHandle;
  while (pFile != NULL) {
    pFileNext = pFile->pNext;
#if FS_SUPPORT_FILE_BUFFER
    if (FS_Global.FileBufferSize != 0u) {       // emFile allocated the file buffer?
      pFileBuffer = pFile->pBuffer;
      if (pFileBuffer != NULL) {
        FS_Free(pFileBuffer);
      }
    }
#endif // FS_SUPPORT_FILE_BUFFER
    FS_Free(pFile);
    pFile = pFileNext;
  }
}

/*********************************************************************
*
*       _RemoveFileObjects
*
*  Function description
*    Frees the memory allocated for all the file objects.
*/
static void _RemoveFileObjects(void) {
  FS_FILE_OBJ    * pFileObj;
  FS_FILE_OBJ    * pFileObjNext;

  pFileObj = FS_Global.pFirstFileObj;
  while (pFileObj != NULL) {
    pFileObjNext = pFileObj->pNext;
    FS_Free(pFileObj);
    pFileObj = pFileObjNext;
  }
}

#endif // FS_SUPPORT_DEINIT

#if FS_SUPPORT_EXT_MEM_MANAGER

/*********************************************************************
*
*       _Alloc
*
*  Function description
*    Allocates a memory block from the memory pool.
*
*  Parameters
*    NumBytes     Number of bytes to be allocated.
*
*  Return value
*    !=NULL       Allocated memory block.
*    ==NULL       An error occurred.
*
*  Additional information
*    This function is called by the file system to allocate memory required
*    for the different components.
*/
static void * _Alloc(U32 NumBytes) {
  void           * pData;
  FS_MEM_MANAGER * pMemManager;

  pData       = NULL;
  pMemManager = &FS_Global.MemManager;
  if (pMemManager->pfAlloc != NULL) {
    if (NumBytes != 0u) {
      pData = pMemManager->pfAlloc(NumBytes);
    }
  }
  return pData;
}

#else

/*********************************************************************
*
*       _Alloc
*
*  Function description
*    Allocates a memory block from the internal memory pool.
*
*  Parameters
*    NumBytes     Number of bytes to be allocated.
*
*  Return value
*    !=NULL       Allocated memory block.
*    ==NULL       An error occurred.
*
*  Additional information
*    This function is called by the file system to allocate memory required
*    for the different components.
*/
static void * _Alloc(U32 NumBytes) {
  void           * pData;
  FS_MEM_MANAGER * pMemManager;
  U32              NumBytesAllocated;
  U32              NumBytesTotal;
#if FS_SUPPORT_DEINIT
  U32              NumBlocksAllocated;
#endif // FS_SUPPORT_DEINIT

  pMemManager = &FS_Global.MemManager;
  pData             = pMemManager->pData;
  NumBytesAllocated = pMemManager->NumBytesAllocated;
  NumBytesTotal     = pMemManager->NumBytesTotal;
#if FS_SUPPORT_DEINIT
  NumBlocksAllocated = pMemManager->NumBlocksAllocated;
#endif // FS_SUPPORT_DEINIT
  if (pMemManager->pData == NULL) {
    FS_DEBUG_ERROROUT((FS_MTYPE_API, "_Alloc: No memory assigned yet."));
    return NULL;                                              // Error, no memory block assigned.
  }
  if (NumBytes == 0u) {
    return NULL;                                              // Error, invalid number of bytes.
  }
  NumBytes = (NumBytes + 3uL) & ~3uL;                         // Round upwards to a multiple of 4 (memory is managed in 32-bit units)
  if ((NumBytes + NumBytesAllocated) > NumBytesTotal) {
    return NULL;                                              // Error, no more memory available.
  }
  pData = SEGGER_PTR2PTR(U8, pData) + NumBytesAllocated;      // MISRA deviation D:100[d]
  NumBytesAllocated += NumBytes;
  pMemManager->NumBytesAllocated  = NumBytesAllocated;
#if FS_SUPPORT_DEINIT
  NumBlocksAllocated++;
  pMemManager->NumBlocksAllocated = NumBlocksAllocated;
#endif // FS_SUPPORT_DEINIT
  return pData;
}

#endif // FS_SUPPORT_EXT_MEM_MANAGER

/*********************************************************************
*
*       Public code (internal, for testing only)
*
**********************************************************************
*/

#if FS_SUPPORT_TEST

/*********************************************************************
*
*       FS__SetTestHookMemAllocBegin
*/
void FS__SetTestHookMemAllocBegin(FS_TEST_HOOK_MEM_ALLOC_BEGIN * pfTestHook) {
  _pfTestHookMemAllocBegin = pfTestHook;
}

/*********************************************************************
*
*       FS__SetTestHookMemAllocEnd
*/
void FS__SetTestHookMemAllocEnd(FS_TEST_HOOK_MEM_ALLOC_END * pfTestHook) {
  _pfTestHookMemAllocEnd = pfTestHook;
}

#endif // FS_SUPPORT_TEST

/*********************************************************************
*
*       Public code (internal)
*
**********************************************************************
*/

/*********************************************************************
*
*       FS__DivModU32
*
*  Function description
*    Divides 2 32-bit numbers, delivering result and remainder.
*
*  Additional information
*    v / Div:   200 / 56
*
*    Prepare division:
*      Shift = 1, Div = 112
*
*    Perform division:
*      Result = 1; v = 88; Div = 56;
*      Result = 3; v = 32;
*/
U32 FS__DivModU32(U32 v, U32 Div, U32 * pRem) {
  int Shift;
  U32 Result;

  //
  // Prepare division.
  // Shift Divisor left until it is at the limit or highest 1-bit is in the same position as in Divider v
  //
  Shift = 0;
  for (;;) {
    if ((Div & (1uL << 31)) != 0u) {
      break;
    }
    if ((Div << 1) > v) {
      break;
    }
    Div <<= 1;
    Shift++;
  }
  //
  // Perform Division
  // Shift Divisor back to its original position, then we are done
  //
  Result = 0;
  while (Shift >= 0) {
    Result <<= 1;
    if (Div <= v) {
      v -= Div;
      Result |= 1uL;
    }
    Div >>= 1;
    Shift--;
  };
  if (pRem != NULL) {
    *pRem = v;
  }
  return Result;
}

/*********************************************************************
*
*       FS__DivideU32Up
*/
U32 FS__DivideU32Up(U32 Nom, U32 Div) {
  if (Div != 0u) {
    return (Nom + Div - 1uL) / Div;
  }
  return 0;
}

/*********************************************************************
*
*       FS_LoadU16BE
*
*  Function description
*    Reads a 16 bit value stored in big endian format from a byte array.
*/
U16 FS_LoadU16BE(const U8 * pBuffer) {
  U16 r;

  r = *pBuffer++;
  r = (U16)((r << 8) | *pBuffer);
  return r;
}

/*********************************************************************
*
*       FS_LoadU32BE
*
*  Function description
*    Reads a 32 bit value stored in big endian format from a byte array.
*/
U32 FS_LoadU32BE(const U8 *pBuffer) {
  U32 r;
  r = *pBuffer++;
  r = (r << 8) | *pBuffer++;
  r = (r << 8) | *pBuffer++;
  r = (r << 8) | *pBuffer;
  return r;
}

/*********************************************************************
*
*       FS_StoreU16BE
*
*  Function description
*    Stores a 16 bit value in big endian format into a byte array.
*/
void FS_StoreU16BE(U8 *pBuffer, unsigned Data) {
  *pBuffer++ = (U8)(Data >> 8);
  *pBuffer   = (U8) Data;
}

/*********************************************************************
*
*       FS_StoreU24BE
*
*  Function description
*    Stores a 24 bit value in big endian format into a byte array.
*/
void FS_StoreU24BE(U8 * pBuffer, U32 Data) {
  *pBuffer++ = (U8)(Data >> 16);
  *pBuffer++ = (U8)(Data >> 8);
  *pBuffer   = (U8) Data;
}

/*********************************************************************
*
*       FS_StoreU32BE
*
*  Function description
*    Stores a 32 bit value in big endian format into a byte array.
*/
void FS_StoreU32BE(U8 * pBuffer, U32 Data) {
  *pBuffer++ = (U8)(Data >> 24);
  *pBuffer++ = (U8)(Data >> 16);
  *pBuffer++ = (U8)(Data >> 8);
  *pBuffer   = (U8) Data;
}

/*********************************************************************
*
*       FS_LoadU64LE
*
*  Function description
*    Reads a 64 bit little endian value from memory.
*
*  Parameters
*    pBuffer    Memory region to read from.
*
*  Return value
*    The read value as U64 data type.
*/
U64 FS_LoadU64LE(const U8 * pBuffer) {
  U64 r;

  r   = (U64)pBuffer[7] & 0xFFuLL;
  r <<= 8;
  r  += (U64)pBuffer[6] & 0xFFuLL;
  r <<= 8;
  r  += (U64)pBuffer[5] & 0xFFuLL;
  r <<= 8;
  r  += (U64)pBuffer[4] & 0xFFuLL;
  r <<= 8;
  r  += (U64)pBuffer[3] & 0xFFuLL;
  r <<= 8;
  r  += (U64)pBuffer[2] & 0xFFuLL;
  r <<= 8;
  r  += (U64)pBuffer[1] & 0xFFuLL;
  r <<= 8;
  r  += (U64)pBuffer[0] & 0xFFuLL;
  return r;
}

/*********************************************************************
*
*       FS_LoadU32LE
*
*  Function description
*    Reads a 32 bit little endian value from a char array.
*
*  Parameters
*    pBuffer    Pointer to a char array.
*
*  Return value
*    The value as U32 data type.
*/
U32 FS_LoadU32LE(const U8 * pBuffer) {
  U32 r;
  r   = (U32)pBuffer[3] & 0x000000FFuL;
  r <<= 8;
  r  += (U32)pBuffer[2] & 0x000000FFuL;
  r <<= 8;
  r  += (U32)pBuffer[1] & 0x000000FFuL;
  r <<= 8;
  r  += (U32)pBuffer[0] & 0x000000FFuL;
  return r;
}

/*********************************************************************
*
*       FS_StoreU64LE
*
*  Function description
*    Stores 64 bits little endian into memory.
*/
void FS_StoreU64LE(U8 *pBuffer, U64 Data) {
  *pBuffer++ = (U8)Data;
  Data >>= 8;
  *pBuffer++ = (U8)Data;
  Data >>= 8;
  *pBuffer++ = (U8)Data;
  Data >>= 8;
  *pBuffer++ = (U8)Data;
  Data >>= 8;
  *pBuffer++ = (U8)Data;
  Data >>= 8;
  *pBuffer++ = (U8)Data;
  Data >>= 8;
  *pBuffer++ = (U8)Data;
  Data >>= 8;
  *pBuffer   = (U8)Data;
}

/*********************************************************************
*
*       FS_StoreU32LE
*
*  Function description
*    Stores 32 bits little endian into memory.
*/
void FS_StoreU32LE(U8 *pBuffer, U32 Data) {
  *pBuffer++ = (U8)Data;
  Data >>= 8;
  *pBuffer++ = (U8)Data;
  Data >>= 8;
  *pBuffer++ = (U8)Data;
  Data >>= 8;
  *pBuffer   = (U8)Data;
}

/*********************************************************************
*
*       FS_StoreU24LE
*
*  Function description
*    Stores 24 bits little endian into memory.
*/
void FS_StoreU24LE(U8 *pBuffer, U32 Data) {
  *pBuffer++ = (U8)Data;
  Data >>= 8;
  *pBuffer++ = (U8)Data;
  Data >>= 8;
  *pBuffer = (U8)Data;
}

/*********************************************************************
*
*       FS_StoreU16LE
*
*  Function description
*    Writes 16 bit little endian.
*/
void FS_StoreU16LE(U8 *pBuffer, unsigned Data) {
  *pBuffer++ = (U8)Data;
  Data >>= 8;
  *pBuffer = (U8)Data;
}

/*********************************************************************
*
*       FS_LoadU16LE
*
*  Function description
*    Reads a 16 bit little endian from a char array.
*
*  Parameters
*    pBuffer    Pointer to a char array.
*
*  Return value
*    The value as U16 data type
*/
U16 FS_LoadU16LE(const U8 *pBuffer) {
  U16 r;

  r = (U16)pBuffer[0] | ((U16)pBuffer[1] << 8);
  return r;
}

#if FS_SUPPORT_DEINIT

/*********************************************************************
*
*       FS__RemoveDevices
*
*  Function description
*    Removes a volume from the file system.
*/
void FS__RemoveDevices(void) {
  int         i;
  int         NumVolumes;
  FS_VOLUME * pVolume;
  int         Status;
  FS_DEVICE * pDevice;
  FS_VOLUME * pVolumeNext;

  pVolume    = &FS_Global.FirstVolume;
  NumVolumes = (int)FS_Global.NumVolumes;
  //
  // Get through the whole volume list and deinitialize the modules.
  // We have to do it in two steps because the Journaling needs the whole
  // list of volumes to select the corresponding instance.
  //
  for (i = 0; i < NumVolumes; i++) {
    pDevice = &pVolume->Partition.Device;
    Status  = FS_LB_GetStatus(pDevice);
    if (Status == FS_MEDIA_NOT_PRESENT) {
      FS__UnmountForcedNL(pVolume);
    } else {
      FS__UnmountNL(pVolume);
    }
    (void)FS__IoCtlNL(pVolume, FS_CMD_DEINIT, 0, NULL);
    FS_JOURNAL_DEINIT(pVolume);
    FS_OS_REMOVE_DRIVER(pVolume->Partition.Device.pType);
    pVolume = pVolume->pNext;
  }
  //
  // Get through the whole volume list and free the memory allocated by the volume list.
  //
  pVolume = &FS_Global.FirstVolume;
  for (i = 0; i < NumVolumes; i++) {
    pVolumeNext = pVolume->pNext;
    FS_MEMSET(pVolume, 0, sizeof(FS_VOLUME));
    if (pVolume != &FS_Global.FirstVolume) {
      FS_FREE(pVolume);
    }
    pVolume = pVolumeNext;
    --FS_Global.NumVolumes;
  }
}

#if FS_SUPPORT_TEST

/*********************************************************************
*
*       FS__SetOnDeInitCallback
*
*  Function description
*    Registers a function to be called when the file system is deinitialized.
*
*  Parameters
*    pfOnDeInit     Function to be called on deinitialization.
*/
void FS__SetOnDeInitCallback(FS_ON_DEINIT_CALLBACK * pfOnDeInit) {
  FS_Global.pfOnDeInit = pfOnDeInit;
}

#endif // FS_SUPPORT_TEST

#endif // FS_SUPPORT_DEINIT

/*********************************************************************
*
*       FS__IsSystemDirName
*
*  Function description
*    Checks if the name matches either "." or "..".
*
*  Parameters
*    sDirName     File name to be checked.
*
*  Return value
*    ==1    This is a system directory.
*    ==0    This is not a system directory.
*/
int FS__IsSystemDirName(const char * sDirName) {
  int FileNameLen;

  FileNameLen = (int)FS_STRLEN(sDirName);
  if (FileNameLen == 1) {
    if (*sDirName == '.') {
      return 1;       // This is a "." entry.
    }
  }
  if (FileNameLen == 2) {
    if (*sDirName == '.') {
      if (*(sDirName + 1) == '.') {
        return 1;     // This is a ".." entry.
      }
    }
  }
  return 0;           // Not a dot entry.
}

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

#if FS_SUPPORT_BUSY_LED

/*********************************************************************
*
*       FS_SetBusyLEDCallback
*
*  Function description
*    Registers a callback for busy status changes of a volume.
*
*  Parameters
*    sVolumeName    Name of the volume for which the callback has
*                   to be registered. It cannot be NULL.
*    pfBusyLED      Function to be called when the busy status
*                   changes. It cannot be NULL.
*
*  Return value
*    ==0      OK, callback registered.
*    !=0      Error code indicating the failure reason.
*
*  Additional information
*    The application can use this FS_SetBusyLEDCallback() to register
*    a function that is called by the file system each time the busy
*    status of a volume changes. The volume becomes busy when it
*    starts an access to storage device. When the access to storage
*    device ends the volume becomes ready. The busy status of a volume
*    can change several times during a single file system operation.
*
*    FS_SetBusyLEDCallback() is available if the FS_SUPPORT_BUSY_LED
*    configuration define is set to 1. The function does nothing
*    if FS_SUPPORT_BUSY_LED is set to 0.
*/
int FS_SetBusyLEDCallback(const char * sVolumeName, FS_BUSY_LED_CALLBACK * pfBusyLED) {
  FS_VOLUME * pVolume;
  int         r;

  r = FS_ERRCODE_INVALID_PARA;
  FS_LOCK();
  if (sVolumeName != NULL) {
    r = FS_ERRCODE_VOLUME_NOT_FOUND;
    pVolume = FS__FindVolume(sVolumeName);
    FS_LOCK_SYS();
    if (pVolume != NULL) {
      pVolume->Partition.Device.Data.pfSetBusyLED = pfBusyLED;
      r = FS_ERRCODE_OK;
    }
    FS_UNLOCK_SYS();
  }
  FS_UNLOCK();
  return r;
}

#endif // FS_SUPPORT_BUSY_LED

#if FS_SUPPORT_CHECK_MEMORY

/*********************************************************************
*
*       FS_SetMemCheckCallback
*
*  Function description
*    Registers a callback for checking of 0-copy operations.
*
*  Parameters
*    sVolumeName    Name of the volume for which the callback
*                   has to be registered. It cannot be NULL.
*    pfMemCheck     Function to be called before a 0-copy
*                   operation is executed. It cannot be NULL.
*
*  Return value
*    ==0      OK, callback registered.
*    !=0      Error code indicating the failure reason.
*
*  Additional information
*    FS_SetMemCheckCallback() can be used by an application to
*    register a function that is called by the file system before
*    any read or write operation to check if a data buffer can be
*    used in 0-copy operation. In a 0-copy operation, a pointer to
*    the data is passed directly to the device driver instead of
*    the data being copied first in an internal buffer and then being
*    passed it to device driver.
*/
int FS_SetMemCheckCallback(const char * sVolumeName, FS_MEM_CHECK_CALLBACK * pfMemCheck) {
  FS_VOLUME * pVolume;
  int         r;

  r = FS_ERRCODE_INVALID_PARA;
  FS_LOCK();
  if (sVolumeName != NULL) {
    r = FS_ERRCODE_VOLUME_NOT_FOUND;
    pVolume = FS__FindVolume(sVolumeName);
    FS_LOCK_SYS();
    if (pVolume != NULL) {
      pVolume->Partition.Device.Data.pfMemCheck = pfMemCheck;
    }
    FS_UNLOCK_SYS();
  }
  FS_UNLOCK();
  return r;
}

#endif // FS_SUPPORT_CHECK_MEMORY

/*********************************************************************
*
*       FS_STORAGE_Init
*
*  Function description
*    Initializes the storage layer.
*
*  Return value
*    Number of OS synchronization objects required to protect
*    the file system against concurrent access from different tasks.
*
*  Additional information
*    This function initializes the only drivers and if necessary
*    the OS layer. It has to be called before any other function
*    of the storage layer (\tt{FS_STORAGE_...}) The storage layer
*    allows an application to access the file system at logical
*    sector level. The storage device is presented as an array
*    of logical sector that can be accessed via a 0-based index.
*    This can be useful when using the file system as USB mass storage
*    client driver.
*
*    FS_STORAGE_Init() is called internally at the initialization
*    of the file system. The return value of this function is used
*    by FS_Init() to calculate the number of internal buffers the
*    file system has to allocate for the read and write operations.
*    The application is not required to call FS_STORAGE_Init() if
*    it already calls FS_Init().
*
*    FS_STORAGE_DeInit() is the counterpart of FS_STORAGE_Init()
*    that can be used to free the resources allocated by the
*    drivers and if enabled of the OS layer.
*/
unsigned FS_STORAGE_Init(void) {
  unsigned NumDriverLocks;

  NumDriverLocks = 0;
  if (FS_Global.IsStorageInited == 0u) {
    //
    // Setup the default value for max sector size
    //
    FS_Global.MaxSectorSize = 512;
    //
    // Add all drivers that should be used.
    //
    FS_X_AddDevices();
    //
    // Calculate the number of locks that are needed.
    //
    NumDriverLocks = FS_OS_GETNUM_DRIVERLOCKS();
    //
    // Tell OS layer how many locks are necessary.
    //
    FS_OS_INIT(FS_OS_GETNUM_SYSLOCKS() + NumDriverLocks);
#if (FS_OS_LOCK_PER_DRIVER == 0)
    NumDriverLocks++;
#endif
    FS_Global.IsStorageInited = 1;
    FS_Global.sCopyright      = "SEGGER emFile V" FS_VERSION2STRING(FS_VERSION);
  }
  return NumDriverLocks;
}

/*********************************************************************
*
*       FS_SetMaxSectorSize
*
*  Function description
*    Configures the maximum size of a logical sector.
*
*  Parameters
*    MaxSectorSize    Number of bytes in the logical sector.
*
*  Return value
*    ==0      OK, the sector size had been set.
*    !=0      Error code indicating the failure reason.
*
*  Additional information
*    The file system uses internal RAM buffers to store the data of
*    logical sectors it accesses. The storage devices added to file
*    system can have different logical sector sizes. Since the size
*    of the logical sectors is not known at the time the internal RAM
*    buffers are allocated the application has to call
*    FS_SetMaxSectorSize() to specify the size of the largest logical
*    sector used by the configured drivers.
*
*    The default value for the maximum size of a logical sector
*    is 512 bytes. The size of the logical sector supported by a
*    driver can be found in the section that describes the specific
*    driver.
*
*    FS_SetMaxSectorSize() can be called only at file system
*    initialization in FS_X_AddDevices().
*/
int FS_SetMaxSectorSize(unsigned MaxSectorSize) {
  if (FS_Global.IsInited != 0u) {
    FS_DEBUG_WARN((FS_MTYPE_API, "FS_SetMaxSectorSize: Can only be called before FS_Init() or in FS_X_AddDevices()."));
    return FS_ERRCODE_INVALID_USAGE;
  }
#if FS_SUPPORT_FAT
  if ((MaxSectorSize & 0xFE00u) == 0u) {
    FS_DEBUG_ERROROUT((FS_MTYPE_API, "FS_SetMaxSectorSize: The FAT file system requires a sector size of minimum 512 bytes."));
    return FS_ERRCODE_INVALID_PARA;
  }
#endif
  if ((MaxSectorSize & (MaxSectorSize - 1u)) != 0u) {
    FS_DEBUG_ERROROUT((FS_MTYPE_API, "FS_SetMaxSectorSize: The sector size has to be a power of 2 value."));
    return FS_ERRCODE_INVALID_PARA;
  }
  FS_Global.MaxSectorSize = (U16)MaxSectorSize;
  return 0;
}

#if FS_SUPPORT_DEINIT

/*********************************************************************
*
*       FS_STORAGE_DeInit
*
*  Function description
*    Frees the resources allocated by the storage layer.
*
*  Additional information
*    This function is optional. FS_STORAGE_DeInit() frees all
*    resources that are allocated by the storage layer after
*    initialization. The application can call this function
*    only after it called FS_STORAGE_Init().
*
*    This function is available if the emFile sources are compiled
*    with the FS_SUPPORT_DEINIT configuration define set to 1.
*/
void FS_STORAGE_DeInit(void) {
  if (FS_Global.IsStorageInited != 0u) {
    FS__RemoveDevices();
    FS_OS_DEINIT();
    FS_Global.IsStorageInited = 0;
  }
}

/*********************************************************************
*
*       FS_DeInit
*
*  Function description
*    Frees allocated resources.
*
*  Additional information
*    This function is optional. FS_DeInit() frees all resources
*    that are allocated by the file system after initialization.
*    Also, all static variables of all file system layers
*    are reset in order to guarantee that the file system remains
*    in a known state after deinitialization.The application can
*    call this function only after it called FS_Init().
*
*    This function has to be used when the file system is reset at
*    runtime. For example this is the case if the system uses a
*    software reboot which reinitializes the target application.
*
*    This function is available if the emFile sources are compiled
*    with the FS_SUPPORT_DEINIT configuration define set to 1.
*/
void FS_DeInit(void) {
  //
  // Generate a warning if the file system has been deinitialized already.
  //
  if (FS_Global.IsInited == 0u) {
    FS_DEBUG_WARN((FS_MTYPE_API, "File system already deinitialized."));
  }
  FS_STORAGE_DeInit();
  if (FS_Global.IsInited != 0u) {
    //
    // Free memory that was used by sector buffers.
    //
    if (FS_Global.paSectorBuffer != NULL) {
      FS_Free(FS_Global.paSectorBuffer->pBuffer);
      FS_Free(FS_Global.paSectorBuffer);
    }
#if (FS_SUPPORT_EFS != 0) && (FS_EFS_SUPPORT_DIRENTRY_BUFFERS != 0)
    if (FS_Global.paDirEntryBuffer != NULL) {
      FS_Free(FS_Global.paDirEntryBuffer->pBuffer);
      FS_Free(FS_Global.paDirEntryBuffer);
    }
#endif
    FS_Global.NumSectorBuffers = 0;
    _RemoveFileObjects();
    _RemoveFileHandles();
    if (FS_Global.pFirstOnExit != NULL) {
      FS_ON_EXIT_CB * pOnExitHandler;

      //
      // Iterate over all exit handlers and call each of them.
      //
      for (pOnExitHandler = FS_Global.pFirstOnExit; pOnExitHandler != NULL; pOnExitHandler = pOnExitHandler->pNext) {
        pOnExitHandler->pfOnExit();
      }
      FS_Global.pFirstOnExit = NULL;
    }
    FS_Global.IsInited = 0;
#if FS_SUPPORT_EXT_MEM_MANAGER
    FS_Global.MemManager.pfAlloc           = NULL;
    FS_Global.MemManager.pfFree            = NULL;
    FS_Global.MemManager.NumBytesAllocated = 0;
#endif // FS_SUPPORT_EXT_MEM_MANAGER
  }
#if FS_SUPPORT_TEST
  if (FS_Global.pfOnDeInit != NULL) {
    FS_Global.pfOnDeInit();
  }
#endif // FS_SUPPORT_TEST
}

/*********************************************************************
*
*       FS_AddOnExitHandler
*
*  Function description
*    Registers a deinitialization callback.
*
*  Parameters
*    pCB        [IN] Structure holding the callback information.
*    pfOnExit   Pointer to the callback function to be invoked.
*
*  Additional information
*    The pCB memory location is used internally by the file system
*    and it should remain valid from the time the handler is registered
*    until the FS_DeInit() function is called. The FS_DeInit() function
*    invokes all the registered callback functions in reversed order
*    that is the last registered function is called first.
*    In order to use this function the binary compile time
*    switch FS_SUPPORT_DEINIT has to be set to 1.
*/
void FS_AddOnExitHandler(FS_ON_EXIT_CB * pCB, void (*pfOnExit)(void)) {
  pCB->pfOnExit = pfOnExit;     // Remember callback function
  //
  // Add new CB to the beginning of the list
  //
  pCB->pNext             = FS_Global.pFirstOnExit;
  FS_Global.pFirstOnExit = pCB;
}

#endif // FS_SUPPORT_DEINIT

#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ALL)

/*********************************************************************
*
*       FS_AllocEx
*
*  Function description
*    Reserves a block of memory from the memory pool.
*
*  Additional information
*    This function is a variant of FS_Alloc() that accepts an
*    additional 0-terminated literal string as parameter indicating
*    the purpose for which the memory was allocated.
*/
void * FS_AllocEx(I32 NumBytes, const char * sDesc) {
  void * p;

  CALL_TEST_HOOK_MEM_ALLOC_BEGIN_EX(sDesc, &NumBytes);
  p = _Alloc((U32)NumBytes);
  CALL_TEST_HOOK_MEM_ALLOC_END_EX(sDesc, NumBytes, &p);
  if (p == NULL) {
#if FS_SUPPORT_EXT_MEM_MANAGER
    FS_DEBUG_ERROROUT((FS_MTYPE_API, "FS_AllocEx: Could not allocate memory (NumBytesReq: %lu, Desc: %s).", NumBytes, sDesc));
#else
    FS_DEBUG_ERROROUT((FS_MTYPE_API, "FS_AllocEx: Could not allocate memory (NumBytesReq: %lu, NumBytesAvail: %lu, Desc: %s).", NumBytes, FS_Global.MemManager.NumBytesTotal - FS_Global.MemManager.NumBytesAllocated, sDesc));
#endif // FS_SUPPORT_EXT_MEM_MANAGER
    FS_X_PANIC(FS_ERRCODE_OUT_OF_MEMORY);
  } else {
#if FS_SUPPORT_EXT_MEM_MANAGER
    FS_DEBUG_LOG((FS_MTYPE_MEM, "MEM: ALLOC % 6lu@0x%08X, % 6lu, %s\n", NumBytes, p, FS_Global.MemManager.NumBytesAllocated, sDesc));
#else
    FS_DEBUG_LOG((FS_MTYPE_MEM, "MEM: ALLOC % 6lu@0x%08X, % 6lu of % 6lu, %s\n", NumBytes, p, FS_Global.MemManager.NumBytesAllocated, FS_Global.MemManager.NumBytesTotal, sDesc));
#endif // FS_SUPPORT_EXT_MEM_MANAGER
  }
  return p;
}

/*********************************************************************
*
*       FS_TryAllocEx
*
*  Function description
*    Reserves a block of memory from the memory pool if available.
*
*  Additional information
*    This function is a variant of FS_TryAlloc() that accepts an
*    additional 0-terminated literal string as parameter indicating
*    the purpose for which the memory was allocated.
*/
void * FS_TryAllocEx(I32 NumBytes, const char * sDesc) {
  void * p;

  CALL_TEST_HOOK_MEM_ALLOC_BEGIN_EX(sDesc, &NumBytes);
  p = _Alloc((U32)NumBytes);
  CALL_TEST_HOOK_MEM_ALLOC_END_EX(sDesc, NumBytes, &p);
  if (p != NULL) {
#if FS_SUPPORT_EXT_MEM_MANAGER
    FS_DEBUG_LOG((FS_MTYPE_MEM, "MEM: ALLOC % 6lu@0x%08X, % 6lu, %s\n", NumBytes, p, FS_Global.MemManager.NumBytesAllocated, sDesc));
#else
    FS_DEBUG_LOG((FS_MTYPE_MEM, "MEM: ALLOC % 6lu@0x%08X, % 6lu of % 6lu, %s\n", NumBytes, p, FS_Global.MemManager.NumBytesAllocated, FS_Global.MemManager.NumBytesTotal, sDesc));
#endif // FS_SUPPORT_EXT_MEM_MANAGER
  }
  return p;
}

/*********************************************************************
*
*       FS_AllocZeroedEx
*
*  Function description
*    Reserves a block of memory from the memory pool and initializes
*    its contents with 0.
*
*  Additional information
*    This function is a variant of FS_AllocZeroed() that accepts an
*    additional 0-terminated literal string as parameter indicating
*    the purpose for which the memory was allocated.
*/
void * FS_AllocZeroedEx(I32 NumBytes, const char * sDesc) {
  void * p;

  p = FS_AllocEx(NumBytes, sDesc);
  if (p != NULL) {
    FS_MEMSET(p, 0, (unsigned)NumBytes);
  }
  return p;
}

/*********************************************************************
*
*       FS_AllocZeroedPtrEx
*
*  Function description
*    Reserves a block of memory from the memory pool and initializes
*    its contents with 0.
*
*  Additional information
*    This function is a variant of FS_AllocZeroedPtr() that accepts an
*    additional 0-terminated literal string as parameter indicating
*    the purpose for which the memory was allocated.
*/
void FS_AllocZeroedPtrEx(void ** pp, I32 NumBytes, const char * sDesc) {
  void * p;

  p = *pp;
  if (p == NULL) {
    p   = FS_AllocEx(NumBytes, sDesc);
    *pp = p;
  }
  if (p != NULL) {
    FS_MEMSET(p, 0, (unsigned)NumBytes);
  }
}

#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ALL

/*********************************************************************
*
*       FS_Alloc
*
*  Function description
*    Reserves a block of memory from the memory pool.
*
*  Notes
*    (1)  Fragmentation
*         The file system allocates memory only in the configuration phase,
*         not during normal operation, so that fragmentation should not occur.
*    (2)  Failure
*         Since the memory is required for proper operation of the file system,
*         this function does not return on failure.
*         In case of a configuration problem where insufficient memory is available
*         to the application, this is normally detected by the programmer in the debug phase.
*/
void * FS_Alloc(I32 NumBytes) {
  void * p;

  CALL_TEST_HOOK_MEM_ALLOC_BEGIN(&NumBytes);
  p = _Alloc((U32)NumBytes);
  CALL_TEST_HOOK_MEM_ALLOC_END(NumBytes, &p);
  if (p == NULL) {
#if FS_SUPPORT_EXT_MEM_MANAGER
    FS_DEBUG_ERROROUT((FS_MTYPE_API, "FS_Alloc: Could not allocate memory (NumBytesReq: %lu).", NumBytes));
#else
    FS_DEBUG_ERROROUT((FS_MTYPE_API, "FS_Alloc: Could not allocate memory (NumBytesReq: %lu, NumBytesAvail: %lu).", NumBytes, FS_Global.MemManager.NumBytesTotal - FS_Global.MemManager.NumBytesAllocated));
#endif // FS_SUPPORT_EXT_MEM_MANAGER
    FS_X_PANIC(FS_ERRCODE_OUT_OF_MEMORY);
  }
  return p;
}

/*********************************************************************
*
*       FS_TryAlloc
*
*  Function description
*    Reserves a block of memory from the memory pool if available.
*/
void * FS_TryAlloc(I32 NumBytes) {
  void * p;

  CALL_TEST_HOOK_MEM_ALLOC_BEGIN(&NumBytes);
  p = _Alloc((U32)NumBytes);
  CALL_TEST_HOOK_MEM_ALLOC_END(NumBytes, &p);
  return p;
}

/*********************************************************************
*
*       FS_AllocZeroed
*
*  Function description
*    Reserves a block of memory from the memory pool and initializes
*    its contents with 0.
*/
void * FS_AllocZeroed(I32 NumBytes) {
  void * p;

  p = FS_Alloc(NumBytes);
  if (p != NULL) {
    FS_MEMSET(p, 0, (unsigned)NumBytes);
  }
  return p;
}

/*********************************************************************
*
*       FS_AllocZeroedPtr
*
*  Function description
*    Reserves a block of memory from the memory pool and initializes
*    its contents with 0.
*
*  Additional information
*    If pp is NULL, the memory block is allocated and pp is updated
*    with the address of allocated memory block. Else no memory is
*    allocated. In either case memory is initialized with 0.
*/
void FS_AllocZeroedPtr(void ** pp, I32 NumBytes) {
  void * p;

  p = *pp;
  if (p == NULL) {
    p   = FS_Alloc(NumBytes);
    *pp = p;
  }
  if (p != NULL) {
    FS_MEMSET(p, 0, (unsigned)NumBytes);
  }
}

#if FS_SUPPORT_DEINIT

/*********************************************************************
*
*       FS_Free
*
*  Function description
*    Releases a memory block that was allocated via FS_Alloc(),
*    FS_AllocZeroed(), FS_AllocZeroedPtr() or FS_TryAlloc().
*/
void FS_Free(void * pData) {
  FS_MEM_MANAGER * pMemManager;

  pMemManager = &FS_Global.MemManager;
#if FS_SUPPORT_EXT_MEM_MANAGER
  if (pMemManager->pfFree != NULL) {
    pMemManager->pfFree(pData);
    if (pData != NULL) {
      FS_DEBUG_LOG((FS_MTYPE_MEM, "MEM: FREE @0x%08X\n", pData));
    }
  }
#else
  if (pData != NULL) {
    U32 NumBlocksAllocated;

    NumBlocksAllocated = pMemManager->NumBlocksAllocated;
    if (NumBlocksAllocated != 0u) {
      --NumBlocksAllocated;
      if (NumBlocksAllocated == 0u) {
        pMemManager->NumBytesAllocated = 0;
      }
      pMemManager->NumBlocksAllocated = NumBlocksAllocated;
      FS_DEBUG_LOG((FS_MTYPE_MEM, "MEM: FREE @0x%08X\n", pData));
    } else {
      //
      // Error, the file system is trying to free memory it did not allocate.
      //
      FS_DEBUG_ERROROUT((FS_MTYPE_API, "FS_Free: Freeing unallocated memory."));
      FS_X_PANIC(FS_ERRCODE_INVALID_USAGE);
    }
  }
#endif // FS_SUPPORT_EXT_MEM_MANAGER
}

#endif // FS_SUPPORT_DEINIT

/*********************************************************************
*
*       FS_GetFreeMem
*
*  Function description
*    Returns a pointer to the memory left unused by the file system.
*/
void * FS_GetFreeMem(I32 * pNumBytes) {
#if (FS_SUPPORT_EXT_MEM_MANAGER != 0) || (FS_OS_LOCKING == 2)
  FS_USE_PARA(pNumBytes);
  return NULL;
#else
  I32              NumBytes;
  U8             * pData;
  FS_MEM_MANAGER * pMemManager;
  I32              NumBytesAllocated;
  I32              NumBytesTotal;

  pMemManager = &FS_Global.MemManager;
  NumBytesTotal     = (I32)pMemManager->NumBytesTotal;
  NumBytesAllocated = (I32)pMemManager->NumBytesAllocated;
  pData    = NULL;
  NumBytes = NumBytesTotal - NumBytesAllocated;
  if (NumBytes > 0) {
    pData      = SEGGER_PTR2PTR(U8, pMemManager->pData);            // MISRA deviation D:100[d]
    *pNumBytes = NumBytes;
    pData      = pData + NumBytesAllocated;
  }
  return pData;
#endif // FS_SUPPORT_EXT_MEM_MANAGER
}

#if (FS_SUPPORT_EXT_MEM_MANAGER == 0)

/*********************************************************************
*
*       FS_AssignMemory
*
*  Function description
*    Assigns a memory pool to the file system.
*
*  Parameters
*    pData      [IN] A pointer to the start of the memory region
*               assigned to file system.
*    NumBytes   Size of the memory pool assigned.
*
*  Additional information
*    emFile comes with a simple semi-dynamic internal memory manager
*    that is used to satisfy the runtime memory requirements of the
*    file system. FS_AssignMemory() can be used to provide a memory
*    pool to the internal memory manager of the file system.
*    If not enough memory is assigned, the file system calls
*    FS_X_Panic() in debug builds which by default halts the execution
*    of the application. The actual number of bytes allocated is
*    stored in the global variable FS_Global.MemManager.NumBytesAllocated.
*    This variable can be used to fine-tune the size of the memory pool.
*
*    emFile supports also the use of an external memory manager
*    (e.g. via malloc() and free() functions of the standard C library).
*    The selection between the internal and the external memory
*    management has to be done at compile time via the
*    FS_SUPPORT_EXT_MEM_MANAGER define. The configuration of the
*    memory management functions is done via FS_SetMemHandler().
*
*    This function has to be called in the initialization phase
*    of the file system; typically in FS_X_AddDevices().
*    The support for internal memory management has to be enabled
*    at compile time by setting the FS_SUPPORT_EXT_MEM_MANAGER define
*    to 0. FS_AssignMemory() does nothing if the FS_SUPPORT_EXT_MEM_MANAGER
*    define is set to 1.
*/
void FS_AssignMemory(U32 * pData, U32 NumBytes) {
  FS_MEM_MANAGER * pMemManager;

  pMemManager = &FS_Global.MemManager;
  pMemManager->pData             = pData;
  pMemManager->NumBytesTotal     = NumBytes;
  pMemManager->NumBytesAllocated = 0;
}

#else // FS_SUPPORT_EXT_MEM_MANAGER == 0

/*********************************************************************
*
*       FS_SetMemHandler
*
*  Function description
*    Configures functions for memory management.
*
*  Parameters
*    pfAlloc    Pointer to a function that allocates memory
*               (e.g. malloc()). It cannot be NULL.
*    pfFree     Pointer to a function that frees memory
*               (e.g. free()). It cannot be NULL.
*
*  Additional information
*    The application can use this function to configure functions
*    for the memory management. The file system calls pfAlloc
*    to allocate memory and pfFree to release the allocated memory.
*
*    This function has to be called in the initialization phase
*    of the file system; typically in FS_X_AddDevices().
*    The support for external memory management has to be enabled
*    at compile time by setting the FS_SUPPORT_EXT_MEM_MANAGER define
*    to 1. FS_SetMemHandler() does nothing if FS_SUPPORT_EXT_MEM_MANAGER
*    is set to 0 (default).
*/
void FS_SetMemHandler(FS_MEM_ALLOC_CALLBACK * pfAlloc, FS_MEM_FREE_CALLBACK * pfFree) {
  FS_MEM_MANAGER * pMemManager;

  pMemManager = &FS_Global.MemManager;
  if (pMemManager->pfAlloc != NULL) {
    FS_DEBUG_WARN((FS_MTYPE_API, "FS_SetMemHandler: The memory allocation function is already set."));
  }
  if (pMemManager->pfFree != NULL) {
    FS_DEBUG_WARN((FS_MTYPE_API, "FS_SetMemHandler: The memory free function is already set."));
  }
  pMemManager->pfAlloc = pfAlloc;
  pMemManager->pfFree  = pfFree;
}

#endif // FS_SUPPORT_EXT_MEM_MANAGER

/*********************************************************************
*
*       FS_GetMemInfo
*
*  Function description
*    Returns information about the memory management.
*
*  Parameters
*    pMemInfo   [OUT] Returned information.
*
*  Return value
*    ==0    OK, information returned.
*    !=0    Error code indicating the failure reason.
*
*  Additional information
*    The application can use this function to obtain information
*    about the memory management such as number of bytes allocated
*    by the file system, type of memory management used, etc.
*/
int FS_GetMemInfo(FS_MEM_INFO * pMemInfo) {
  if (pMemInfo == NULL) {
    return FS_ERRCODE_INVALID_PARA;
  }
  FS_LOCK();
  FS_LOCK_SYS();
#if FS_SUPPORT_EXT_MEM_MANAGER
  pMemInfo->IsExternal        = 1;
  pMemInfo->NumBytesAllocated = FS_Global.MemManager.NumBytesAllocated;
  pMemInfo->NumBytesTotal     = 0;
#else
  pMemInfo->IsExternal        = 0;
  pMemInfo->NumBytesAllocated = FS_Global.MemManager.NumBytesAllocated;
  pMemInfo->NumBytesTotal     = FS_Global.MemManager.NumBytesTotal;
#endif // FS_SUPPORT_EXT_MEM_MANAGER
  FS_UNLOCK_SYS();
  FS_UNLOCK();
  return 0;
}

/*********************************************************************
*
*       FS_GetMaxSectorSize
*
*  Function description
*    Queries the maximum configured logical sector size.
*
*  Return value
*    Maximum logical sector size in bytes.
*
*  Additional information
*    Default value of the maximum logical sector size is 512 bytes.
*    Refer to FS_SetMaxSectorSize() for more information about
*    the maximum logical sector size.
*/
U32 FS_GetMaxSectorSize(void) {
  return FS_Global.MaxSectorSize;
}

/*********************************************************************
*
*       FS_BITFIELD_CalcNumBitsUsed
*
*  Function description
*    Computes the number of bits used to store the give value
*/
unsigned FS_BITFIELD_CalcNumBitsUsed(U32 NumItems) {
  unsigned r;

  r = 0;
  do {
    r++;
    NumItems >>= 1;
  } while (NumItems != 0u);
  return r;
}

/*********************************************************************
*
*      FS_BITFIELD_ReadEntry
*
*  Function description
*    Reads a single entry of <NumBits> from the bitfield
*/
U32 FS_BITFIELD_ReadEntry(const U8 * pBase, U32 Index, unsigned NumBits) {
  U32 v;
  U32 Off;
  U32 OffEnd;
  U32 Mask;
  U32 BitOff;
  U32 i;

  BitOff = Index * NumBits;
  Off    = BitOff >> 3;
  OffEnd = (BitOff + NumBits - 1u) >> 3;
  pBase += Off;
  i = OffEnd - Off;
  //
  // Read data little endian
  //
  v = *pBase++;
  if (i != 0u) {
    unsigned Shift = 0;
    do {
      Shift += 8u;
      v     |= (U32)*pBase++ << Shift;
    } while (--i != 0u);
  }
  //
  // Shift, mask & return result
  //
  v    >>= (BitOff & 7u);
  Mask   = (1uL << NumBits) - 1u;
  v     &= Mask;
  return v;
}

/*********************************************************************
*
*      FS_BITFIELD_WriteEntry
*
*  Function description
*    Writes a single entry of <NumBits> into the bitfield
*/
void FS_BITFIELD_WriteEntry(U8 * pBase, U32 Index, unsigned NumBits, U32 v) {
  U32   Mask;
  U8  * p;
  U32   u;
  U32   BitOff;

  BitOff = Index * NumBits;
  p      = (U8 *)pBase + (BitOff >> 3);
  Mask   = (1uL << NumBits) - 1u;
  Mask <<= (BitOff & 7u);
  v    <<= (BitOff & 7u);
  //
  // Read, mask, or and write data little endian byte by byte
  //
  do {
    u  = *p;
    u &= ~Mask;
    u |= v;
    *p = (U8)u;
    p++;
    Mask  >>= 8;
    v     >>= 8;
  } while (Mask != 0u);
}

/*********************************************************************
*
*       FS_BITFIELD_CalcSize
*
*  Function description
*    Returns the size of bit field in bytes.
*/
U32 FS_BITFIELD_CalcSize(U32 NumItems, unsigned BitsPerItem) {
  U32 v;

  v =  NumItems * BitsPerItem;  // Compute the number of bits used for storage
  v = (v + 7u) >> 3;            // Convert into bytes
  return v;
}

/*********************************************************************
*
*       FS__AllocSectorBuffer
*
*  Function description
*    Allocates a sector buffer.
*
*  Return value
*    ==0    Cannot allocate a buffer.
*    !=0    Address of a buffer.
*/
U8 * FS__AllocSectorBuffer(void) {
  unsigned         i;
  U8             * pBuffer;
  FS_DATA_BUFFER * pSB;

  pBuffer = NULL;
  FS_LOCK_SYS();
  pSB = FS_Global.paSectorBuffer;
  if (pSB != NULL) {
    for (i = 0; i < FS_Global.NumSectorBuffers; i++) {
      if (pSB->InUse == 0u) {
        pSB->InUse = 1;
        pBuffer = (U8 *)pSB->pBuffer;
        break;
      }
      ++pSB;
    }
  }
  FS_UNLOCK_SYS();
  return pBuffer;
}

/*********************************************************************
*
*       FS__FreeSectorBuffer
*
*  Function description
*    Frees a sector buffer.
*
*  Parameters
*    pBuffer  Pointer to a buffer to be freed.
*/
void FS__FreeSectorBuffer(const void * pBuffer) {
  unsigned         i;
  FS_DATA_BUFFER * pSB;

  FS_LOCK_SYS();
  pSB = FS_Global.paSectorBuffer;
  for (i = 0; i < FS_Global.NumSectorBuffers; i++) {
    if (SEGGER_CONSTPTR2PTR(const void, pSB->pBuffer) == pBuffer) {     // MISRA deviation D:100[e]
      pSB->InUse       = 0;
#if FS_SUPPORT_SECTOR_BUFFER_CACHE
      pSB->pVolume     = NULL;
      pSB->SectorIndex = SECTOR_INDEX_INVALID;
#endif // FS_SUPPORT_SECTOR_BUFFER_CACHE
      break;
    }
    ++pSB;
  }
  FS_UNLOCK_SYS();
}

#if FS_SUPPORT_SECTOR_BUFFER_CACHE

/*********************************************************************
*
*       FS__AllocSectorBufferEx
*
*  Function description
*    Allocates a sector buffer.
*
*  Parameters
*    pVolume        Preferred volume.
*    SectorIndex    Preferred sector index.
*    pIsMatching    [OUT] Set to 1 if a matching sector data was found. Can be NULL.
*
*  Return value
*    !=NULL     OK, sector buffer allocated.
*    ==NULL     Error, cannot allocate a sector buffer.
*/
U8 * FS__AllocSectorBufferEx(const FS_VOLUME * pVolume, U32 SectorIndex, int * pIsMatching) {
  unsigned         i;
  U8             * pBuffer;
  FS_DATA_BUFFER * pSB;
  FS_DATA_BUFFER * pSBFound;
  int              IsMatching;
  unsigned         NumBuffers;

  pBuffer    = NULL;
  IsMatching = 0;
  pSBFound   = NULL;
  NumBuffers = 0;
  FS_LOCK_SYS();
  pSB = FS_Global.paSectorBuffer;
  for (i = 0; i < FS_Global.NumSectorBuffers; i++) {
    if (pSB->InUse == 0u) {
      if (pSBFound == NULL) {
        //
        // Remember the first free sector buffer for the case
        // that we do not find any sector buffer that matches
        // the search criteria.
        //
        pSBFound = pSB;
      }
      if (pSB->pVolume == pVolume) {
        //
        // Prefer a sector buffer that was used for the same volume
        // to increase the chance that a request for a sector buffer
        // allocation for a different volume finds a matching sector data.
        //
        ++NumBuffers;
        if (NumBuffers == FS_NUM_SECTOR_BUFFERS_PER_OPERATION) {
          pSBFound = pSB;
        }
        if (pSB->SectorIndex == SectorIndex) {
          //
          // Found a matching sector data.
          //
          IsMatching = 1;
          pSBFound   = pSB;
          break;
        }
      }
    }
    ++pSB;
  }
  if (pSBFound != NULL) {
    pSBFound->InUse       = 1;
    pSBFound->pVolume     = NULL;
    pSBFound->SectorIndex = SECTOR_INDEX_INVALID;
    pBuffer               = (U8 *)pSBFound->pBuffer;

  }
  FS_UNLOCK_SYS();
  if (pIsMatching != NULL) {
    *pIsMatching = IsMatching;
  }
  return pBuffer;
}

/*********************************************************************
*
*       FS__FreeSectorBufferEx
*
*  Function description
*    Frees a sector buffer.
*
*  Parameters
*    pBuffer        Pointer to a buffer to be freed.
*    pVolume        Partition on which the logical sector is stored.
*    SectorIndex    Index of the logical sector.
*    IsValid        Set to 1 if the sector data is valid.
*/
void FS__FreeSectorBufferEx(const void * pBuffer, FS_VOLUME * pVolume, U32 SectorIndex, int IsValid) {
  unsigned         i;
  FS_DATA_BUFFER * pSB;

  FS_LOCK_SYS();
  pSB = FS_Global.paSectorBuffer;
  for (i = 0; i < FS_Global.NumSectorBuffers; i++) {
    if (SEGGER_CONSTPTR2PTR(const void, pSB->pBuffer) == pBuffer) {     // MISRA deviation D:100[e]
      pSB->InUse = 0;
      if (IsValid != 0) {
        pSB->pVolume     = pVolume;
        pSB->SectorIndex = SectorIndex;
      } else {
        pSB->pVolume     = NULL;
        pSB->SectorIndex = SECTOR_INDEX_INVALID;
      }
    } else {
      //
      // Make sure that only the current data of a logical
      // sector is stored in the cache.
      //
      if (pSB->InUse == 0u) {
        if (pSB->pVolume == pVolume) {
          if (pSB->SectorIndex == SectorIndex) {
            pSB->pVolume     = NULL;
            pSB->SectorIndex = SECTOR_INDEX_INVALID;
          }
        }
      }
    }
    ++pSB;
  }
  FS_UNLOCK_SYS();
}

/*********************************************************************
*
*       FS__InvalidateSectorBuffer
*
*  Function description
*    Invalidates a sector buffer.
*
*  Parameters
*    pVolume        Volume on which the logical sector is stored.
*    SectorIndex    Index of the firs sector to be invalidated.
*    NumSectors     Number of sectors to invalidate.
*
*  Additional information
*    If SectorIndex is set to SECTOR_INDEX_INVALID or NumSectors to 0 then
*    all the sector buffers assigned on the specified partition are invalidated.
*/
void FS__InvalidateSectorBuffer(const FS_VOLUME * pVolume, U32 SectorIndex, U32 NumSectors) {
  unsigned         i;
  FS_DATA_BUFFER * pSB;

  FS_LOCK_SYS();
  pSB = FS_Global.paSectorBuffer;
  for (i = 0; i < FS_Global.NumSectorBuffers; i++) {
    if (pSB->pVolume == pVolume) {
      if (   (SectorIndex == SECTOR_INDEX_INVALID)
          || (NumSectors  == 0u)
          || (   (pSB->SectorIndex >= SectorIndex)
              && (pSB->SectorIndex <  (SectorIndex + NumSectors)))) {
        if (pSB->InUse == 0u) {
          pSB->pVolume     = NULL;
          pSB->SectorIndex = SECTOR_INDEX_INVALID;
        }
      }
    }
    ++pSB;
  }
  FS_UNLOCK_SYS();
}

#endif // FS_SUPPORT_SECTOR_BUFFER_CACHE

#if (FS_OS_LOCKING == FS_OS_LOCKING_API)

/*********************************************************************
*
*       FS_Lock
*
*  Function description
*    Claims exclusive access to file system.
*
*  Additional information
*    The execution of the task that calls this function is suspended
*    until the file system grants it exclusive access. After the task
*    gets exclusive access to file system the other tasks that try to
*    perform file system operations are blocked until the task calls
*    FS_Unlock().
*
*    FS_Lock() is typically used by applications that call
*    device driver functions from different tasks. These functions
*    are usually not protected against concurrent accesses.
*    Additionally, FS_Lock() can be used to protect a group of file
*    system operations against concurrent access.
*
*    FS_Lock() is available if FS_OS_LOCKING set to 1. The function
*    does nothing if FS_OS_LOCKING set to 0 or 2. The calls to
*    FS_Lock() / FS_Unlock() cannot be nested.
*
*    The API functions of the file system are multitasking safe.
*    It is not required to explicitly lock these function calls
*    via FS_Lock() / FS_Unlock(). All API functions call internal
*    versions of FS_Lock() and FS_Unlock() on function
*    entry and exit respectively.
*/
void FS_Lock(void) {
  FS_LOCK();
}

/*********************************************************************
*
*       FS_Unlock
*
*  Function description
*    Releases the exclusive access to file system.
*
*  Additional information
*    This function has to be called in pair with FS_Lock()
*    to allow other tasks to access the file system. For each
*    call to FS_Lock() the application has to call FS_Unlock().
*
*    FS_Unlock() is available if FS_OS_LOCKING set to 1.
*    The function does nothing if FS_OS_LOCKING set to 0 or 2.
*    The calls to FS_Lock() / FS_Unlock() cannot be nested.
*/
void FS_Unlock(void) {
  FS_UNLOCK();
}

#endif // (FS_OS_LOCKING == FS_OS_LOCKING_API)

#if (FS_OS_LOCKING == FS_OS_LOCKING_DRIVER)

/*********************************************************************
*
*       FS_LockVolume
*
*  Function description
*    Claims exclusive access to a volume.
*
*  Parameters
*    sVolumeName    Name of the volume that has to be locked.
*
*  Additional information
*    The execution of the task that calls this function is suspended
*    until the file system grants it exclusive access to the specified
*    volume. After the task gets exclusive access to volume the other
*    tasks that try to  perform file system operations on that volume
*    are blocked until the task calls FS_UnlockVolume() with the
*    same volume name as in the call to FS_LockVolume().
*
*    FS_LockVolume() is typically used by applications that call
*    device driver functions from different tasks. These functions
*    are usually not protected against concurrent accesses.
*    Additionally, FS_LockVolume() can be used to protect a group
*    of file system operations against concurrent access.
*
*    FS_LockVolume() is available if FS_OS_LOCKING set to 2.
*    The function does nothing if FS_OS_LOCKING set to 0 or 1.
*    The calls to FS_LockVolume() / FS_UnlockVolume() cannot be
*    nested.
*
*    The API functions of the file system are multitasking safe.
*    It is not required to explicitly lock these function calls
*    via FS_LockVolume() / FS_UnlockVolume(). All API functions
*    call internal versions of FS_LockVolume() and FS_UnlockVolume()
*    on function entry and exit respectively.
*/
void FS_LockVolume(const char * sVolumeName) {
  FS_VOLUME * pVolume;

  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    FS_LOCK_DRIVER(&pVolume->Partition.Device);
  }
}

/*********************************************************************
*
*       FS_UnlockVolume
*
*  Function description
*    Releases the exclusive access to a volume.
*
*  Parameters
*    sVolumeName    Name of the volume that has to be locked.
*
*  Additional information
*    This function has to be called in pair with FS_LockVolume()
*    to allow other tasks to access the volume. For each
*    call to FS_LockVolume() the application has to call
*    FS_UnlockVolume() with the same volume name as parameter.
*
*    FS_UnlockVolume() is available if FS_OS_LOCKING set to 2.
*    The function does nothing if FS_OS_LOCKING set to 0 or 1.
*    The calls to FS_LockVolume() / FS_UnlockVolume() cannot be
*    nested.
*/
void FS_UnlockVolume(const char * sVolumeName) {
  FS_VOLUME * pVolume;

  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    FS_UNLOCK_DRIVER(&pVolume->Partition.Device);
  }
}

#endif // (FS_OS_LOCKING == FS_OS_LOCKING_DRIVER)

/*************************** End of file ****************************/
