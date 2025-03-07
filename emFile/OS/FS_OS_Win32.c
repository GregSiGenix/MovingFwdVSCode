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
-------------------------- END-OF-HEADER -----------------------------

File    : FS_OS_Win32.c
Purpose : Win32 API OS Layer for the file system
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#include <windows.h>
#include <stdio.h>
#include "FS.h"
#include "FS_OS.h"

/*********************************************************************
*
*       Defines, fixed
*
**********************************************************************
*/

/*********************************************************************
*
*       ASSERT_NOT_LOCKED
*/
#if FS_SUPPORT_TEST
  #define ASSERT_NOT_LOCKED(pInst)                                                                                           \
     if (pInst->OpenCnt != 0) {                                                                                              \
      FS_DEBUG_ERROROUT((FS_MTYPE_OS, "OS: Mutex locked recursively (Index: 0x%8x, Name: %s).", LockIndex, pInst->acName));  \
      FS_X_PANIC(FS_ERRCODE_INVALID_USAGE);                                                                                  \
    }
#else
  #define ASSERT_NOT_LOCKED(pInst)
#endif // FS_SUPPORT_TEST

/*********************************************************************
*
*       ASSERT_NOT_UNLOCKED
*/
#if FS_SUPPORT_TEST
  #define ASSERT_NOT_UNLOCKED(pInst)                                                                                                    \
     if (pInst->OpenCnt == 0) {                                                                                                         \
      FS_DEBUG_ERROROUT((FS_MTYPE_OS, "OS: Mutex unlocked without being locked (Index: 0x%8x, Name: %s).", LockIndex, pInst->acName));  \
      FS_X_PANIC(FS_ERRCODE_INVALID_USAGE);                                                                                             \
    }
#else
  #define ASSERT_NOT_UNLOCKED(pInst)
#endif // FS_SUPPORT_TEST

/*********************************************************************
*
*       Local types
*
**********************************************************************
*/
typedef struct {
  HANDLE hMutex;
  char   acName[60];
  int    OpenCnt;
}  LOCK_INST;

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static LOCK_INST * _paInst;
static int         _IsInited;
static HANDLE      _hEvent = INVALID_HANDLE_VALUE;
#if FS_SUPPORT_DEINIT
  static int       _NumLocks;
#endif

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _AtExit
*/
static void _AtExit(void) {
  timeEndPeriod(1);
}

/*********************************************************************
*
*       _CheckInit
*/
static void _CheckInit(void) {
  if (_IsInited == 0) {
    timeBeginPeriod(1);
    atexit(_AtExit);
    _IsInited = 1;
  }
}

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_X_OS_Lock
*/
void FS_X_OS_Lock(unsigned LockIndex) {
  HANDLE      hMutex;
  LOCK_INST * pInst;

  if (_paInst != NULL) {
    pInst = (_paInst + LockIndex);
    hMutex = pInst->hMutex;
    if (hMutex != NULL) {
      FS_DEBUG_LOG((FS_MTYPE_OS, "OS: LOCK   Index: 0x%8x, Name: %s\n", LockIndex, pInst->acName));
      WaitForSingleObject(hMutex, INFINITE);
      ASSERT_NOT_LOCKED(pInst)
      pInst->OpenCnt++;
    }
  }
}

/*********************************************************************
*
*       FS_X_OS_Unlock
*/
void FS_X_OS_Unlock(unsigned LockIndex) {
  HANDLE      hMutex;
  LOCK_INST * pInst;

  if (_paInst != NULL) {
    pInst = (_paInst + LockIndex);
    hMutex = pInst->hMutex;
    if (hMutex != NULL) {
      FS_DEBUG_LOG((FS_MTYPE_OS, "OS: UNLOCK Index: 0x%8x, Name: %s\n", LockIndex, pInst->acName));
      ASSERT_NOT_UNLOCKED(pInst)
      pInst->OpenCnt--;
      ReleaseMutex(hMutex);
    }
  }
}

/*********************************************************************
*
*       FS_X_OS_Init
*
*  Function description
*    Initializes the OS resources. Specifically, you will need to
*    create four binary semaphores. This function is called by
*    FS_Init(). You should create all resources required by the
*    OS to support multi threading of the file system.
*
*  Parameters
*    Number of locks needed.
*/
void FS_X_OS_Init(unsigned NumLocks) {
  unsigned    i;
  LOCK_INST * pInst;

  _CheckInit();
  _paInst = FS_AllocZeroed((I32)(NumLocks * sizeof(LOCK_INST)));
  if (_paInst != NULL) {
    pInst    = &_paInst[0];
    for (i = 0; i < NumLocks; i++) {
      SEGGER_snprintf(pInst->acName, sizeof(pInst->acName), "FS Semaphore %.3d", (int)i);
      pInst->hMutex = CreateMutex(NULL, 0, pInst->acName);
      if (pInst->hMutex == NULL) {
        FS_DEBUG_ERROROUT((FS_MTYPE_OS, "OS: Could not create semaphore."));
        return;
      }
      pInst++;
    }
  }
  if (_hEvent == INVALID_HANDLE_VALUE) {
    _hEvent = CreateEvent(NULL, FALSE, FALSE, "FS Event");
  }
#if FS_SUPPORT_DEINIT
  _NumLocks = NumLocks;
#endif
}

#if FS_SUPPORT_DEINIT

/*********************************************************************
*
*       FS_X_OS_DeInit
*
*  Function description
*    Delete all locks that have been created by FS_X_OS_Init().
*    This makes sure that a
*/
void FS_X_OS_DeInit(void) {
  int         i;
  int         NumLocks;
  LOCK_INST * pInst;

  if (_paInst != NULL) {
    NumLocks = _NumLocks;
    pInst    = &_paInst[0];
    for (i = 0; i < NumLocks; i++) {
      CloseHandle(pInst->hMutex);
      pInst++;
    }
    FS_Free(_paInst);
    _paInst   = NULL;
  }
  _NumLocks = 0;
}

#endif // FS_SUPPORT_DEINIT

/*********************************************************************
*
*       FS_X_OS_Delay
*/
void FS_X_OS_Delay(int ms) {
  _CheckInit();
  Sleep((DWORD)ms);
}

/*********************************************************************
*
*       FS_X_OS_GetTime
*
*/
U32 FS_X_OS_GetTime(void) {
  _CheckInit();
  return timeGetTime();
}

/*********************************************************************
*
*       FS_X_OS_Wait
*
*  Function description
*    Wait for an event to be signaled.
*
*  Parameters
*    Timeout    Time to be wait for the event object.
*
*  Return value
*    ==0    Event object was signaled within the timeout value
*    !=0    An error or a timeout occurred.
*/
int FS_X_OS_Wait(int Timeout) {
  int r;

  r = -1;
  _CheckInit();
  if (WaitForSingleObject(_hEvent, (DWORD)Timeout) == WAIT_OBJECT_0) {
    r = 0;
  }
  return r;
}

/*********************************************************************
*
*       FS_X_OS_Signal
*
*  Function description:
*    Signals a event
*/
void FS_X_OS_Signal(void) {
  _CheckInit();
  SetEvent(_hEvent);
}

/*************************** End of file ****************************/
