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
File        : FS_OS_Interface.c
Purpose     : File system OS interface
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

#if FS_OS

/*********************************************************************
*
*       Defines, fixed
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_OS_LOCK
*/
#if FS_OS_SUPPORT_RUNTIME_CONFIG
  #define FS_OS_LOCK(LockIndex)         _pOSType->pfLock(LockIndex)
#else
  #define FS_OS_LOCK(LockIndex)         FS_X_OS_Lock(LockIndex)
#endif // FS_OS_SUPPORT_RUNTIME_CONFIG

/*********************************************************************
*
*       FS_OS_UNLOCK
*/
#if FS_OS_SUPPORT_RUNTIME_CONFIG
  #define FS_OS_UNLOCK(LockIndex)       _pOSType->pfUnlock(LockIndex)
#else
  #define FS_OS_UNLOCK(LockIndex)       FS_X_OS_Unlock(LockIndex)
#endif // FS_OS_SUPPORT_RUNTIME_CONFIG

/*********************************************************************
*
*       Local data types
*
**********************************************************************
*/
#if FS_OS_LOCK_PER_DRIVER    // One lock per driver

typedef struct DRIVER_LOCK DRIVER_LOCK;         //lint -esym(9058, DRIVER_LOCK) tag unused outside of typedefs. Rationale: the typedef is used as forward declaration.

struct DRIVER_LOCK  {
  DRIVER_LOCK          * pNext;
  U8                     Id;
  const FS_DEVICE_TYPE * pDriver;
  U8                     References;
};

#endif // FS_OS_LOCK_PER_DRIVER

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static U8                   _IsInited = 0;
#if FS_OS_LOCK_PER_DRIVER    // One lock per driver
  static int                _NumDriverLocks = 0;
  static DRIVER_LOCK      * _pDriverLock;
#endif // FS_OS_LOCK_PER_DRIVER
#if FS_OS_SUPPORT_RUNTIME_CONFIG
  static const FS_OS_TYPE * _pOSType;
#endif // FS_OS_SUPPORT_RUNTIME_CONFIG

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

#if FS_OS_LOCK_PER_DRIVER    // One lock per driver

/*********************************************************************
*
*       _AddDriver
*
*  Function description
*    Adds a driver to the lock list.
*    If the driver is already in the list, its reference count is incremented;
*    if not, a memory block is allocated and added to the lock list as last element.
*/
static void _AddDriver(const FS_DEVICE_TYPE * pDriver) {
  DRIVER_LOCK * pDriverLock;
  DRIVER_LOCK ** ppPrev;

  pDriverLock = _pDriverLock;
  ppPrev       = &_pDriverLock;
  for (;;) {
    if (pDriverLock == NULL) {
      pDriverLock = SEGGER_PTR2PTR(DRIVER_LOCK, FS_ALLOC_ZEROED((I32)sizeof(DRIVER_LOCK), "DRIVER_LOCK"));
      if (pDriverLock != NULL) {
        pDriverLock->Id       = (U8)_NumDriverLocks++;
        pDriverLock->pDriver  = pDriver;
        pDriverLock->References++;
        *ppPrev = pDriverLock;
      }
      break;
    }
    if (pDriverLock->pDriver == pDriver) {
      pDriverLock->References++;
      break;
    }
    ppPrev      = &pDriverLock->pNext;
    pDriverLock = pDriverLock->pNext;
  }
}

/*********************************************************************
*
*       _RemoveDriver
*
*  Function description
*    Removes a driver from the lock list, in case the reference count is zero;
*/
static void _RemoveDriver(const FS_DEVICE_TYPE * pDriver) {
  DRIVER_LOCK *  pDriverLock;
  DRIVER_LOCK ** ppPrev;

  pDriverLock = _pDriverLock;
  ppPrev      = &_pDriverLock;
  if (pDriverLock != NULL) {
    do {
      if (pDriver == pDriverLock->pDriver) {
        if (--pDriverLock->References == 0u) {
          (*ppPrev)= pDriverLock->pNext;
#if FS_SUPPORT_DEINIT
          FS_Free(pDriverLock);
#endif
          _NumDriverLocks--;
          break;
        }
      }
      ppPrev      = &pDriverLock->pNext;
      pDriverLock = pDriverLock->pNext;
    } while (pDriverLock != NULL);
  }
}

/*********************************************************************
*
*       _Driver2Id
*
*  Function description
*    Retrieves the lock Id of the device driver.
*    The lock Id is unique for every device driver.
*/
static unsigned _Driver2Id(const FS_DEVICE_TYPE * pDriver) {
  DRIVER_LOCK * pDriverLock;

  pDriverLock = _pDriverLock;
  if (pDriverLock != NULL) {
    do {
      if (pDriverLock->pDriver == pDriver) {
        return pDriverLock->Id;
      }
      pDriverLock = pDriverLock->pNext;
    }  while (pDriverLock != NULL);
  }
  FS_DEBUG_ERROROUT((FS_MTYPE_OS, "_Driver2Id: Driver not found in the lock list."));
  return 0;
}

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_OS_AddDriver
*/
void FS_OS_AddDriver(const FS_DEVICE_TYPE * pDriver) {
  _AddDriver(pDriver);
}

/*********************************************************************
*
*       FS_OS_RemoveDriver
*/
void FS_OS_RemoveDriver(const FS_DEVICE_TYPE * pDriver) {
  _RemoveDriver(pDriver);
}

/*********************************************************************
*
*       FS_OS_LockDriver
*/
void FS_OS_LockDriver(const FS_DEVICE * pDevice) {
  unsigned LockIndex;

  if (_IsInited != 0u) {
    LockIndex = 0;
    if (pDevice != NULL) {
      LockIndex = _Driver2Id(pDevice->pType);
    }
    LockIndex += FS_LOCK_ID_DEVICE;
    FS_OS_LOCK(LockIndex);
  }
}

/*********************************************************************
*
*       FS_OS_UnlockDriver
*/
void FS_OS_UnlockDriver(const FS_DEVICE * pDevice) {
  unsigned LockIndex;

  if (_IsInited != 0u) {
    LockIndex = 0;
    if (pDevice != NULL) {
      LockIndex = _Driver2Id(pDevice->pType);
    }
    LockIndex += FS_LOCK_ID_DEVICE;
    FS_OS_UNLOCK(LockIndex);
  }
}

/*********************************************************************
*
*       FS_OS_GetNumDriverLocks
*/
unsigned FS_OS_GetNumDriverLocks(void) {
  return (unsigned)_NumDriverLocks;
}

#endif // FS_OS_LOCK_PER_DRIVER

/*********************************************************************
*
*       FS_OS_Lock
*/
void FS_OS_Lock(unsigned LockIndex) {
  if (_IsInited != 0u) {
    FS_OS_LOCK(LockIndex);
  }
  //lint -esym(522, FS_OS_Lock) Highest operation lacks side-effects. Required when using the FS_OS_None.c OS layer.
}

/*********************************************************************
*
*       FS_OS_Unlock
*/
void FS_OS_Unlock(unsigned LockIndex) {
  if (_IsInited != 0u) {
    FS_OS_UNLOCK(LockIndex);
  }
  //lint -esym(522, FS_OS_Unlock) Highest operation lacks side-effects. Required when using the FS_OS_None.c OS layer.
}

/*********************************************************************
*
*       FS_OS_Init
*/
void FS_OS_Init(unsigned NumLocks) {
#if FS_OS_SUPPORT_RUNTIME_CONFIG
  if (_pOSType != NULL) {
    _pOSType->pfInit(NumLocks);
    _IsInited = 1;
  }
#else
  FS_X_OS_Init(NumLocks);
  _IsInited = 1;
#endif // FS_OS_SUPPORT_RUNTIME_CONFIG
}

#if FS_SUPPORT_DEINIT

/*********************************************************************
*
*       FS_OS_DeInit
*/
void FS_OS_DeInit(void) {
#if FS_OS_SUPPORT_RUNTIME_CONFIG
  if (_pOSType != NULL) {
    _pOSType->pfDeInit();
  }
#else
  FS_X_OS_DeInit();
#endif // FS_OS_SUPPORT_RUNTIME_CONFIG
  _IsInited = 0;
}

#endif // FS_SUPPORT_DEINIT

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

#if FS_OS_SUPPORT_RUNTIME_CONFIG

/*********************************************************************
*
*       FS_OS_SetType
*
*  Function description
*    Configures an OS layer.
*
*  Parameters
*    pOSType    OS layer to be configured.
*
*  Additional information
*    FS_OS_SetType() is available only if the FS_OS_SUPPORT_RUNTIME_CONFIG
*    is set to 1 and FS_OS_LOCKING to 1 or 2.
*/
void FS_OS_SetType(const FS_OS_TYPE * pOSType) {
  _pOSType = pOSType;
}

#endif // FS_OS_SUPPORT_RUNTIME_CONFIG

/*********************************************************************
*
*       FS_OS_GetTime
*
*  Function description
*    Number of milliseconds elapsed since the start of the application.
*
*  Return value
*    Number of milliseconds elapsed.
*
*  Additional information
*    This function is not directly called by the file system. It is typically
*    used by some of the sample applications as time base for performance
*    measurements.
*/
U32 FS_OS_GetTime(void) {
  U32 r;

#if FS_OS_SUPPORT_RUNTIME_CONFIG
  r = 0;
  if (_pOSType != NULL) {
    r = _pOSType->pfGetTime();
  }
#else
  r = FS_X_OS_GetTime();
#endif // FS_OS_SUPPORT_RUNTIME_CONFIG
  return r;
}

/*********************************************************************
*
*       FS_OS_Delay
*
*  Function description
*    Blocks the execution for the specified time.
*
*  Parameters
*    ms     Number of milliseconds to block the execution.
*
*  Additional information
*    This function is not directly called by the file system.
*    FS_OS_Delay() is called by some hardware layer implementations
*    to block the execution of a task efficiently.
*/
void FS_OS_Delay(int ms) {
#if FS_OS_SUPPORT_RUNTIME_CONFIG
  if (_pOSType != NULL) {
    _pOSType->pfDelay(ms);
  }
#else
  FS_X_OS_Delay(ms);
#endif // FS_OS_SUPPORT_RUNTIME_CONFIG
}

/*********************************************************************
*
*       FS_OS_Wait
*
*  Function description
*    Waits for an OS synchronization object to be signaled.
*
*  Parameters
*    TimeOut    Maximum time in milliseconds to wait for the
*               OS synchronization object to be signaled.
*
*  Return value
*    ==0      OK, the OS synchronization object was signaled within the timeout.
*    !=0      An error or a timeout occurred.
*
*  Additional information
*    This function is not directly called by the file system. FS_OS_Wait()
*    is called by some hardware layer implementations that work
*    in event-driven mode. That is a condition is not check periodically
*    by the CPU until is met but the hardware layer calls FS_OS_Wait()
*    to block the execution while waiting for the condition to be met.
*    The blocking is realized via an OS synchronization object that is
*    signaled via FS_OS_Signal() in an interrupt that is triggered
*    when the condition is met.
*/
int FS_OS_Wait(int TimeOut) {
  int r;

#if FS_OS_SUPPORT_RUNTIME_CONFIG
  r = 0;
  if (_pOSType != NULL) {
    r = _pOSType->pfWait(TimeOut);
  }
#else
  r = FS_X_OS_Wait(TimeOut);
#endif // FS_OS_SUPPORT_RUNTIME_CONFIG
  return r;
}

/*********************************************************************
*
*       FS_OS_Signal
*
*  Function description
*    Signals an OS synchronization object.
*
*  Additional information
*    This function is not directly called by the file system. FS_OS_Signal()
*    is called by some hardware layer implementations that work in
*    event-driven mode. Refer to FS_OS_Wait() for more details about this.
*/
void FS_OS_Signal(void) {
#if FS_OS_SUPPORT_RUNTIME_CONFIG
  if (_pOSType != NULL) {
    _pOSType->pfSignal();
  }
#else
  FS_X_OS_Signal();
#endif // FS_OS_SUPPORT_RUNTIME_CONFIG
}

#endif // FS_OS

/*************************** End of file ****************************/
