/*********************************************************************
*                   (c) SEGGER Microcontroller GmbH                  *
*                        The Embedded Experts                        *
**********************************************************************
*                                                                    *
*       (c) 2003 - 2022     SEGGER Microcontroller GmbH              *
*                                                                    *
*       www.segger.com     Support: www.segger.com/ticket            *
*                                                                    *
**********************************************************************
*                                                                    *
*       emUSB-Host * USB Host stack for embedded applications        *
*                                                                    *
*       Please note: Knowledge of this file may under no             *
*       circumstances be used to write a similar product.            *
*       Thank you for your fairness !                                *
*                                                                    *
**********************************************************************
*                                                                    *
*       emUSB-Host version: V2.36.1                                  *
*                                                                    *
**********************************************************************
----------------------------------------------------------------------
Licensing information
Licensor:                 SEGGER Microcontroller Systems LLC
Licensed to:              React Health, Inc., 203 Avenue A NW, Suite 300, Winter Haven FL 33881, USA
Licensed SEGGER software: emUSB-Host
License number:           USBH-00304
License model:            SSL [Single Developer Single Platform Source Code License]
Licensed product:         -
Licensed platform:        STM32F4, IAR
Licensed number of seats: 1
----------------------------------------------------------------------
Support and Update Agreement (SUA)
SUA period:               2022-05-19 - 2022-11-19
Contact to extend SUA:    sales@segger.com
----------------------------------------------------------------------
File        : USBH_OS_embOSv5.c
Purpose     : Kernel abstraction for the embOS version >= 5.0
              Do not modify to allow easy updates!
---------------------------END-OF-HEADER------------------------------
*/
/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/

#include "USBH.h"
#include "USBH_Util.h"
#include "USBH_MEM.h"
#include "RTOS.h"

/*********************************************************************
*
*       Defines, configurable
*
**********************************************************************
*/
//
// Tick rate of the OS system tick counter in Hz
//
#ifndef USBH_OS_TICK_RATE_HZ
  #define USBH_OS_TICK_RATE_HZ     1000u
#endif

/*********************************************************************
*
*       Type definitions
*
**********************************************************************
*/
struct _USBH_OS_EVENT_OBJ {
  USBH_DLIST  ListEntry;
  OS_EVENT    EventTask;
};

#define GET_EVENT_OBJ_FROM_ENTRY(pListEntry)        STRUCT_BASE_POINTER(pListEntry, USBH_OS_EVENT_OBJ, ListEntry)

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static OS_MUTEX      _aMutex[USBH_MUTEX_COUNT];
static OS_EVENT      _EventNet;
static OS_EVENT      _EventISR;
static volatile U32  _IsrMask;
static USBH_DLIST    _UserEventList;

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       USBH_OS_DisableInterrupt
*
*  Function description
*    Enter a critical region for the USB stack: Increments interrupt disable count and
*    disables interrupts.
*    The USB stack will perform nested calls to USBH_OS_DisableInterrupt() and USBH_OS_EnableInterrupt().
*
*  Additional information
*    Any implementation of this function must at least:
*      1) Disable USB related interrupts and
*      2) Prevent task switches
*/
void USBH_OS_DisableInterrupt(void) {
  OS_INT_IncDI();
}

/*********************************************************************
*
*       USBH_OS_EnableInterrupt
*
*  Function description
*    Leave a critical region for the USB stack: Decrements interrupt disable count and
*    enable interrupts if counter reaches 0.
*    The USB stack will perform nested calls to USBH_OS_DisableInterrupt() and USBH_OS_EnableInterrupt().
*/
void USBH_OS_EnableInterrupt(void) {
  OS_INT_DecRI();
}

/*********************************************************************
*
*       USBH_OS_Init
*
*  Function description
*    Initialize (create) all objects required for task synchronization.
*/
void USBH_OS_Init(void) {
  unsigned i;

#if USBH_DEBUG > 0
  if (OS_TIME_Convertms2Ticks(1000) != USBH_OS_TICK_RATE_HZ) {
    USBH_PANIC("Setting of USBH_OS_TICK_RATE_HZ does not match OS configuration");
  }
#endif
  OS_EVENT_CreateEx(&_EventNet, OS_EVENT_RESET_MODE_AUTO);
  OS_EVENT_CreateEx(&_EventISR, OS_EVENT_RESET_MODE_AUTO);
  for (i = 0; i < SEGGER_COUNTOF(_aMutex); i++) {
    OS_MUTEX_Create(&_aMutex[i]);
  }
  USBH_DLIST_Init(&_UserEventList);
}

/*********************************************************************
*
*       USBH_OS_Lock
*
*  Function description
*    This function locks a mutex object, guarding sections of the stack code
*    against other threads. Mutexes are recursive.
*
*  Parameters
*    Idx:     Index of the mutex to be locked (0 .. USBH_MUTEX_COUNT-1).
*/
void USBH_OS_Lock(unsigned Idx) {
#if USBH_SUPPORT_WARN
  unsigned i;
  OS_TASK *pMe;

  if (Idx >= USBH_MUTEX_COUNT) {
    USBH_PANIC("OS: bad mutex index");
  }
  //
  // Check mutex hierarchy.
  //
  pMe = OS_TASK_GetID();
  for (i = 0; i < Idx; i++) {
    if (OS_MUTEX_GetOwner(&_aMutex[i]) == pMe) {
      USBH_Warnf(USBH_MCAT_ASSERT, "OS: Mutex hierarchy violated!");
    }
  }
#endif
  OS_MUTEX_LockBlocked(&_aMutex[Idx]);
}

/*********************************************************************
*
*       USBH_OS_Unlock
*
*  Function description
*    Unlocks the mutex used by a previous call to USBH_OS_Lock().
*    Mutexes are recursive.
*
*  Parameters
*    Idx:     Index of the mutex to be released (0 .. USBH_MUTEX_COUNT-1).
*/
void USBH_OS_Unlock(unsigned Idx) {
  OS_MUTEX_Unlock(&_aMutex[Idx]);
}

/*********************************************************************
*
*       USBH_OS_GetTime32
*
*  Function description
*    Return the current system time in ms.
*    The value will wrap around after app. 49.7 days. This is taken into
*    account by the stack.
*
*  Return value
*    Current system time.
*/
USBH_TIME USBH_OS_GetTime32(void) {
#if USBH_OS_TICK_RATE_HZ > 1000
  U32        Ticks;
  static U32 HighTickBits = 0;
  static U32 LastTicks    = 0;

  Ticks = OS_TIME_GetTicks32();
  //
  // The value returned must use the full value range of an U32 before wrapping around,
  // in order to allow calculation of correct time differences.
  // When dividing OS_TIME_GetTicks32(), the upper bits are lost,
  // so we have to simulate them.
  //
  if (Ticks < LastTicks) {
    //
    // Wrap around of xTaskGetTickCount() value occurred
    //
    HighTickBits += 0xFFFFFFFF / (USBH_OS_TICK_RATE_HZ / 1000) + 1;
  }
  LastTicks = Ticks;
  return HighTickBits + Ticks / (USBH_OS_TICK_RATE_HZ / 1000);
#else
  //
  // Case USB_OS_TICK_RATE_HZ <= 1000:
  //
  // Only a single multiplication with a constant, to achieve a correct wrap around.
  // Even tough "(Ticks * 1000) / configTICK_RATE_HZ" may be more accurate for small values of Ticks,
  // it would give completely wrong results for values of Ticks near 0xFFFFFFFF.
  //
  return OS_TIME_GetTicks32() * (1000 / USBH_OS_TICK_RATE_HZ);
#endif
}

/*********************************************************************
*
*       USBH_OS_Delay
*
*  Function description
*    Blocks the calling task for a given time.
*
*  Parameters
*    ms:     Delay in milliseconds.
*/
void USBH_OS_Delay(unsigned ms) {
#if USBH_OS_TICK_RATE_HZ != 1000u
  ms = (ms * USBH_OS_TICK_RATE_HZ + 999u) / 1000u;
#endif
  OS_TASK_Delay(ms);
}

/*********************************************************************
*
*       USBH_OS_WaitNetEvent
*
*  Function description
*    Blocks until the timeout expires or a USBH-event occurs.
*    Called from USBH_MainTask() only.
*    A USBH-event is signaled with USBH_OS_SignalNetEvent() called from an other task or ISR.
*
*  Parameters
*    ms:     Timeout in milliseconds.
*/
void USBH_OS_WaitNetEvent(unsigned ms) {
#if USBH_OS_TICK_RATE_HZ != 1000u
  ms = (ms * USBH_OS_TICK_RATE_HZ + 999u) / 1000u;
#endif
  OS_EVENT_GetTimed(&_EventNet, ms);
}

/*********************************************************************
*
*       USBH_OS_SignalNetEvent
*
*  Function description
*    Wakes the USBH_MainTask() if it is waiting for a event or timeout
*    in the function USBH_OS_WaitNetEvent().
*/
void USBH_OS_SignalNetEvent(void) {
  OS_EVENT_Set(&_EventNet);
}

/*********************************************************************
*
*       USBH_OS_WaitISR
*
*  Function description:
*    Blocks until USBH_OS_SignalISR() is called (from ISR).
*    Called from USBH_ISRTask() only.
*
*  Return value:
*    An ISR mask, where each bit set corresponds to a host controller index.
*/
U32 USBH_OS_WaitISR(void) {
  U32 r;

  OS_EVENT_GetBlocked(&_EventISR);
  OS_INT_IncDI();
  r = _IsrMask;
  _IsrMask = 0;
  OS_INT_DecRI();
  return r;
}

/*********************************************************************
*
*       USBH_OS_SignalISREx
*
*  Function description
*    Wakes the USBH_ISRTask(). Called from ISR.
*
*  Parameters
*    DevIndex:  Zero-based index of the host controller that needs attention.
*/
void USBH_OS_SignalISREx(U32 DevIndex) {
  _IsrMask |= (1 << DevIndex);
  OS_EVENT_Set(&_EventISR);
}

/*********************************************************************
*
*       USBH_OS_AllocEvent
*
*  Function description
*    Allocates and returns an event object.
*
*  Return value:
*    A pointer to a USBH_OS_EVENT_OBJ object on success or NULL on error.
*/
USBH_OS_EVENT_OBJ * USBH_OS_AllocEvent(void) {
  USBH_OS_EVENT_OBJ * p;

  p = (USBH_OS_EVENT_OBJ *)USBH_TRY_MALLOC(sizeof(USBH_OS_EVENT_OBJ));
  if (p) {
    USBH_DLIST_Init(&p->ListEntry);
    USBH_DLIST_InsertTail(&_UserEventList, &p->ListEntry);
    OS_EVENT_CreateEx(&p->EventTask, OS_EVENT_RESET_MODE_AUTO);
  }
  return p;
}

/*********************************************************************
*
*       USBH_OS_FreeEvent
*
*  Function description
*    Releases an object event.
*
*  Parameters
*    pEvent:  Pointer to an event object that was returned by USBH_OS_AllocEvent().
*/
void USBH_OS_FreeEvent(USBH_OS_EVENT_OBJ * pEvent) {
  USBH_DLIST_RemoveEntry(&pEvent->ListEntry);
  OS_EVENT_Delete(&pEvent->EventTask);
  USBH_FREE(pEvent);
}

/*********************************************************************
*
*       USBH_OS_SetEvent
*
*  Function description
*    Sets the state of the specified event object to signalled.
*
*  Parameters
*    pEvent:  Pointer to an event object that was returned by USBH_OS_AllocEvent().
*/
void USBH_OS_SetEvent(USBH_OS_EVENT_OBJ * pEvent) {
  OS_EVENT_Set(&pEvent->EventTask);
}

/*********************************************************************
*
*       USBH_OS_ResetEvent
*
*  Function description
*    Sets the state of the specified event object to non-signalled.
*
*  Parameters
*    pEvent:  Pointer to an event object that was returned by USBH_OS_AllocEvent().
*/
void USBH_OS_ResetEvent(USBH_OS_EVENT_OBJ * pEvent) {
  OS_EVENT_Reset(&pEvent->EventTask);
}

/*********************************************************************
*
*       USBH_OS_WaitEvent
*
*  Function description
*    Wait for the specific event.
*
*  Parameters
*    pEvent:  Pointer to an event object that was returned by USBH_OS_AllocEvent().
*/
void USBH_OS_WaitEvent(USBH_OS_EVENT_OBJ * pEvent) {
  OS_EVENT_GetBlocked(&pEvent->EventTask);
}

/*********************************************************************
*
*       USBH_OS_WaitEventTimed
*
*  Function description
*    Wait for the specific event within a given timeout.
*
*  Parameters
*    pEvent:        Pointer to an event object that was returned by USBH_OS_AllocEvent().
*    MilliSeconds:  Timeout in milliseconds.
*
*  Return value:
*    == USBH_OS_EVENT_SIGNALED:   Event was signaled.
*    == USBH_OS_EVENT_TIMEOUT:    Timeout occurred.
*/
int USBH_OS_WaitEventTimed(USBH_OS_EVENT_OBJ * pEvent, U32 MilliSeconds) {
#if USBH_OS_TICK_RATE_HZ != 1000u
  MilliSeconds = (MilliSeconds * USBH_OS_TICK_RATE_HZ + 999u) / 1000u;
#endif
  return OS_EVENT_GetTimed(&pEvent->EventTask, MilliSeconds);
}

/*********************************************************************
*
*       USBH_OS_DeInit
*
*  Function description
*    Deletes all objects required for task synchronization.
*/
void USBH_OS_DeInit(void) {
  USBH_DLIST * pListHead;
  USBH_DLIST * pEntry;
  unsigned     i;

  pListHead = &_UserEventList;
  pEntry    = USBH_DLIST_GetNext(pListHead);
  while (pListHead != pEntry) {
    USBH_OS_EVENT_OBJ * pEvent;

    pEvent = GET_EVENT_OBJ_FROM_ENTRY(pEntry);
    OS_EVENT_Delete(&pEvent->EventTask);
    pEntry = USBH_DLIST_GetNext(pEntry);
    USBH_DLIST_RemoveEntry(&pEvent->ListEntry);
    USBH_FREE(pEvent);
  }
  OS_EVENT_Delete(&_EventNet);
  OS_EVENT_Delete(&_EventISR);
  for (i = 0; i < SEGGER_COUNTOF(_aMutex); i++) {
    OS_MUTEX_Delete(&_aMutex[i]);
  }
}

/*************************** End of file ****************************/
