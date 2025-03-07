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

File    : FS_OS_None.c
Purpose : OS layer for the file system that does nothing.
Additional information:
  This OS layer can be used with an emFile library compiled with OS
  support (FS_OS_LOCKING != 0) and an application that does not require
  OS support.
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#include "FS_Int.h"
#include "FS_OS.h"

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_X_OS_Lock
*
*  Function description
*    Acquires the specified OS synchronization object.
*
*  Parameters
*    LockIndex    Index of the OS synchronization object (0-based).
*
*  Additional information
*    This function has to be implemented by any OS layer.
*    The file system calls FS_X_OS_Lock() when it tries to enter a critical
*    section that is protected by the OS synchronization object specified via
*    LockIndex. FS_X_OS_Lock() has to block the execution of the calling task
*    until the OS synchronization object can be acquired. The OS synchronization
*    object is later released via a call to FS_X_OS_Unlock(). All OS synchronization
*    objects are created in FS_X_OS_Init().
*
*    It is guaranteed that the file system does not perform a recursive locking
*    of the OS synchronization object. That is FS_X_OS_Lock() is not called
*    two times in a row from the same task on the same OS synchronization object
*    without a call to FS_X_OS_Unlock() in between.
*/
void FS_X_OS_Lock(unsigned LockIndex) {
  FS_USE_PARA(LockIndex);
  //lint -esym(522, FS_X_OS_Lock) Highest operation lacks side-effects
}

/*********************************************************************
*
*       FS_X_OS_Unlock
*
*  Function description
*    Releases the specified OS synchronization object.
*
*  Parameters
*    LockIndex    Index of the OS synchronization object (0-based).
*
*  Additional information
*    This function has to be implemented by any OS layer.
*    The OS synchronization object to be released was acquired via
*    a call to FS_X_OS_Lock(). All OS synchronization objects are
*    created in FS_X_OS_Init().
*/
void FS_X_OS_Unlock(unsigned LockIndex) {
  FS_USE_PARA(LockIndex);
  //lint -esym(522, FS_X_OS_Unlock) Highest operation lacks side-effects
}

/*********************************************************************
*
*       FS_X_OS_Init
*
*  Function description
*    Allocates the OS layer resources.
*
*  Parameters
*    NumLocks   Number of OS synchronization objects required.
*
*  Additional information
*    This function has to be implemented by any OS layer.
*    FS_X_OS_Init() is called during the file system initialization.
*    It has to create the number of specified OS synchronization objects.
*    The type of the OS synchronization object is not relevant as long
*    as it can be used to protect a critical section. The file system
*    calls FS_X_OS_Lock() before it enters a critical section and
*    FS_X_OS_Unlock() when the critical sector is leaved.
*
*    In addition, FS_X_OS_Init() has to create the OS synchronization
*    object used by the optional functions FS_X_OS_Signal() and
*    FS_X_OS_Wait().
*/
void FS_X_OS_Init(unsigned NumLocks) {
  FS_USE_PARA(NumLocks);
  //lint -esym(522, FS_X_OS_Init) Highest operation lacks side-effects
}

#if FS_SUPPORT_DEINIT

/*********************************************************************
*
*       FS_X_OS_DeInit
*
*  Function description
*    Releases the OS layer resources.
*
*  Additional information
*    This function has to be implemented only for file system configurations
*    that set FS_SUPPORT_DEINIT to 1. FS_X_OS_DeInit() has to release all
*    the OS synchronization objects that were allocated in FS_X_OS_Init().
*/
void FS_X_OS_DeInit(void) {
  //lint -esym(522, FS_X_OS_DeInit) Highest operation lacks side-effects
}

#endif // FS_SUPPORT_DEINIT

/*********************************************************************
*
*       FS_X_OS_GetTime
*
*  Function description
*    Number of milliseconds elapsed since the start of the application.
*
*  Return value
*    Number of milliseconds elapsed.
*
*  Additional information
*    The implementation of this function is optional. FS_X_OS_GetTime()
*    is not called by the file system. It is typically used by some
*    test applications as time base for performance measurements.
*/
U32 FS_X_OS_GetTime(void) {
  return 0;
}

/*********************************************************************
*
*       FS_X_OS_Wait
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
*    The implementation of this function is optional. FS_X_OS_Wait()
*    is called by some hardware layer implementations that work
*    in event-driven mode. That is a condition is not check periodically
*    by the CPU until is met but the hardware layer calls FS_X_OS_Wait()
*    to block the execution while waiting for the condition to be met.
*    The blocking is realized via an OS synchronization object that is
*    signaled via FS_X_OS_Signal() in an interrupt that is triggered
*    when the condition is met.
*/
int FS_X_OS_Wait(int TimeOut) {
  FS_USE_PARA(TimeOut);
  return 0;
}

/*********************************************************************
*
*       FS_X_OS_Signal
*
*  Function description
*    Signals an OS synchronization object.
*
*  Additional information
*    The implementation of this function is optional. FS_X_OS_Signal()
*    is called by some hardware layer implementations that work in
*    event-driven mode. Refer to FS_X_OS_Wait() for more details about
*    how this works.
*/
void FS_X_OS_Signal(void) {
  //lint -esym(522, FS_X_OS_Signal) Highest operation lacks side-effects
}

/*********************************************************************
*
*       FS_X_OS_Delay
*
*  Function description
*    Blocks the execution for the specified time.
*
*  Parameters
*    ms     Number of milliseconds to block the execution.
*
*  Additional information
*    The implementation of this function is optional. FS_X_OS_Delay()
*    is called by implementations of the hardware layers to block
*    efficiently the execution of a task.
*/
void FS_X_OS_Delay(int ms) {
  FS_USE_PARA(ms);
  //lint -esym(522, FS_X_OS_Delay) Highest operation lacks side-effects
}

/*************************** End of file ****************************/
