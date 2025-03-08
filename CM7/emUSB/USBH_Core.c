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
File        : USBH_Core.c
Purpose     : USB host implementation
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#define USBHCORE_C
#include "USBH_Int.h"
#include "USBH_Util.h"

/*********************************************************************
*
*       Defines, fixed
*
**********************************************************************
*/
//
// Default USB Host controller configuration this implies
// Roothub configuration and number of endpoints, number of devices.
//
#define HC_ROOTHUB_PORTS_ALWAYS_POWERED 0 // If set, ports are always powered on  when the Host Controller is powered on. The default value is 0.
#define HC_ROOTHUB_PER_PORT_POWERED     1 // Not all host controller supports individually port switching. Because of this the default value is 0.
#define HC_ROOTHUB_OVERCURRENT          1 // This define can set to 1 if the hardware on the USB port detects an over current condition on the Vbus line.
#if HC_ROOTHUB_PORTS_ALWAYS_POWERED != 0 && HC_ROOTHUB_PER_PORT_POWERED != 0
  #error HC_ROOTHUB_PORTS_ALWAYS_POWERED or HC_ROOTHUB_PER_PORT_POWERED is allowed
#endif

#define VERSION2STRING_HELPER(x) #x  //lint !e9024 hash in macro N:999
#define VERSION2STRING(x) VERSION2STRING_HELPER(x)

/*********************************************************************
*
*       Local data types
*
**********************************************************************
*/

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static USBH_DLIST _TimerList;
static USBH_TIME  _NextTimeout;
static I8         _TimerListModified;

/*********************************************************************
*
*       Public data
*
**********************************************************************
*/
SEGGER_CACHE_CONFIG USBH_CacheConfig;

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       Helper functions
*
**********************************************************************
*/

/*********************************************************************
*
*       _UpdateTimeout
*
*  Function description
*    Compute timeout of next expiring timer.
*    System lock is active, so we can simply iterate over the timers in the list.
*/
static void _UpdateTimeout(void) {
  USBH_DLIST * pEntry;
  USBH_TIMER * pTimer;
  USBH_TIME    NextExpire;
  //
  // Compute next time out relative to current time.
  // max. timeout for USBH_OS_WaitNetEvent() is 0x7FFFFF
  //
  NextExpire = USBH_TIME_CALC_EXPIRATION(0x7FFFFF);
  pEntry = &_TimerList;
  for(;;) {
    pEntry = USBH_DLIST_GetNext(pEntry);
    if (pEntry == &_TimerList) {
      break;
    }
    pTimer = GET_TIMER_FROM_ENTRY(pEntry);
    if (pTimer->IsActive != 0) {
      if (USBH_TimeDiff(pTimer->TimeOfExpiration, NextExpire) < 0) {
        NextExpire = pTimer->TimeOfExpiration;
      }
    }
  }
  _NextTimeout = NextExpire;
}

/*********************************************************************
*
*       USBH_InitTimer
*
*  Function description
*    Initializes a timer object.
*
*  Parameters
*    pTimer    : Pointer to the timer object to be initialized.
*    pfHandler : Pointer to a function which should be called when
*                the timer expires.
*    pContext  : Pointer to a context which will be passed to
*                the function pointed to by pfHandler.
*/
void USBH_InitTimer(USBH_TIMER * pTimer, USBH_TIMER_FUNC * pfHandler, void * pContext) {
  USBH_LOG((USBH_MCAT_TIMER, "Init timer %p", pTimer));
  USBH_ASSERT(pTimer->Magic != USBH_TIMER_MAGIC);
  pTimer->pfHandler = pfHandler;
  pTimer->pContext = pContext;
  pTimer->IsActive = 0;
  USBH_IFDBG(pTimer->Magic = USBH_TIMER_MAGIC);
  //
  // Add timer to linked list.
  //
  USBH_OS_Lock(USBH_MUTEX_TIMER);
  USBH_DLIST_InsertEntry(&_TimerList, &pTimer->List);
  _TimerListModified = 1;
  USBH_OS_Unlock(USBH_MUTEX_TIMER);
}

/*********************************************************************
*
*       USBH_ReleaseTimer
*
*  Function description
*    Release a timer object.
*
*  Parameters
*    pTimer : Pointer to a timer object which should be released.
*/
void USBH_ReleaseTimer(USBH_TIMER * pTimer) {
  USBH_LOG((USBH_MCAT_TIMER, "Release timer %p", pTimer));
  USBH_ASSERT_MAGIC(pTimer, USBH_TIMER);
  USBH_OS_Lock(USBH_MUTEX_TIMER);
  pTimer->IsActive = 0;
  //
  // Unlink
  //
  USBH_DLIST_RemoveEntry(&pTimer->List);
  USBH_IFDBG(pTimer->Magic = 0);
  _TimerListModified = 1;
  USBH_OS_Unlock(USBH_MUTEX_TIMER);
}

/*********************************************************************
*
*       USBH_StartTimer
*
*  Function description
*    Starts a timer. The timer is restarted again if it is running.
*
*  Parameters
*    pTimer : Pointer to a timer object.
*    ms     : Time-out in milliseconds.
*/
void USBH_StartTimer(USBH_TIMER * pTimer, U32 ms) {
  USBH_LOG((USBH_MCAT_TIMER_EX, "Starting timer %p with timeout = %u ms", pTimer, ms));
  USBH_ASSERT((I32)ms >= 0);
  USBH_OS_Lock(USBH_MUTEX_TIMER);
  USBH_ASSERT_MAGIC(pTimer, USBH_TIMER);
  pTimer->IsActive         = 1;
  pTimer->TimeOfExpiration = USBH_TIME_CALC_EXPIRATION(ms);
  //
  // Check if this affects the expiration time of the next timer
  //
  if (USBH_Global.TimerTaskIsRunning != 0) {
    if (USBH_TimeDiff(pTimer->TimeOfExpiration, _NextTimeout) < 0) {
      _NextTimeout = pTimer->TimeOfExpiration;
      USBH_OS_SignalNetEvent(); // Timeout change means we need to wake the task to make sure it does not sleep too long
    }
  }
  USBH_OS_Unlock(USBH_MUTEX_TIMER);
}

/*********************************************************************
*
*       USBH_CancelTimer
*
*  Function description
*    Cancels an timer if running, the completion routine is not called.
*
*  Parameters
*    pTimer : Pointer to a timer object.
*/
void USBH_CancelTimer(USBH_TIMER * pTimer) {
  USBH_ASSERT_MAGIC(pTimer, USBH_TIMER);
  pTimer->IsActive = 0;
}

/*********************************************************************
*
*       USBH_IsTimerActive
*
*  Function description
*    Returns whether the timer is active and running.
*
*  Parameters
*    pTimer : Pointer to a timer object.
*
*  Return value
*      0       - Timer is not active.
*      1       - Timer is currently active.
*/
int USBH_IsTimerActive(const USBH_TIMER * pTimer) {
  USBH_ASSERT_MAGIC(pTimer, USBH_TIMER);
  return pTimer->IsActive;
}

/*********************************************************************
*
*       USBH_AllocTimer
*
*  Function description
*    Allocates memory for & initializes timer object.
*
*  Parameters
*    pfHandler : Pointer to a function which should be called when
*                the timer expires.
*    pContext  : Pointer to a context which will be passed to
*                the function pointed to by pfHandler.
*  Return value
*    Handle to the new timer object.
*    NULL when there is not enough memory.
*/
USBH_TIMER_HANDLE USBH_AllocTimer(USBH_TIMER_FUNC * pfHandler, void * pContext) {
  USBH_TIMER * pTimer;
  pTimer = (USBH_TIMER *)USBH_TRY_MALLOC_ZEROED(sizeof(USBH_TIMER));
  USBH_LOG((USBH_MCAT_TIMER, "Allocating timer %p", pTimer));
  if (pTimer != NULL) {
    USBH_InitTimer(pTimer, pfHandler, pContext);
  }
  return pTimer;
}

/*********************************************************************
*
*       USBH_FreeTimer
*
*  Function description
*    Frees a timer object via timer handle.
*
*  Parameters
*    hTimer : Handle to a timer object which should be freed.
*/
void USBH_FreeTimer(USBH_TIMER_HANDLE hTimer) {
  USBH_ReleaseTimer(hTimer);
  USBH_LOG((USBH_MCAT_TIMER, "Freeing timer 0x%x", hTimer));
  USBH_FREE(hTimer);
}

/*********************************************************************
*
*       USBH_Task
*
*  Function description
*    Manages the internal software timers.
*    This function must run as a separate task in order to use the emUSBH stack.
*    The functions only returns, if the USBH stack is shut down (if USBH_Exit() was called).
*
*  Additional information
*    The function iterates over the list of active timers
*    and invokes the registered callback functions in case the timer expired.
*
*    When USBH_Exit() is used in the application this function should
*    not be directly started as a task, as it returns when USBH_Exit()
*    is called. A wrapper function can be used in this case,
*    see USBH_IsRunning() for a sample.
*/
void USBH_Task(void) {
  USBH_DLIST *pEntry;
  USBH_TIMER *pTimer;
  I32         tDiff;

  USBH_LOG((USBH_MCAT_INIT, "USBH_Task started"));
  USBH_OS_Lock(USBH_MUTEX_TIMER);
  _UpdateTimeout();
  USBH_Global.TimerTaskIsRunning = 1;
  USBH_OS_Unlock(USBH_MUTEX_TIMER);
  while (USBH_Global.IsRunning != 0) {
    //
    // If timeout is expired, call all expired timers & compute next timeout.
    //
    USBH_OS_Lock(USBH_MUTEX_TIMER);
    if (USBH_TIME_IS_EXPIRED(_NextTimeout)) {
      pEntry = &_TimerList;
      for(;;) {
        pEntry = USBH_DLIST_GetNext(pEntry);
        if (pEntry == &_TimerList) {
          break;
        }
        pTimer = GET_TIMER_FROM_ENTRY(pEntry);
        if (pTimer->IsActive != 0) {
          if (USBH_TIME_IS_EXPIRED(pTimer->TimeOfExpiration)) {
            pTimer->IsActive = 0;
            _TimerListModified = 0;
            USBH_OS_Unlock(USBH_MUTEX_TIMER);
            USBH_LOG((USBH_MCAT_TIMER_EX, "Execute timer %p", pTimer));
            pTimer->pfHandler(pTimer->pContext);
            USBH_OS_Lock(USBH_MUTEX_TIMER);
            if (_TimerListModified != 0) {
              //
              // Linked list of timers was modified, the pEntry may not be valid any more.
              // So restart and process the list from the beginning.
              //
              pEntry = &_TimerList;
            }
          }
        }
      }
      _UpdateTimeout();
    }
    USBH_OS_Unlock(USBH_MUTEX_TIMER);
    //
    // Pause as long as no timer expires.
    //
    tDiff = USBH_TimeDiff(_NextTimeout, USBH_OS_GetTime32());
#if 0
    if (tDiff > 1000) {
      //
      // Sleep max. 1 second to regularly call USBH_CleanupDeviceList().
      //
      tDiff = 1000;
    }
#endif
    if (tDiff > 0) {
      USBH_OS_WaitNetEvent((U32)tDiff);
    }
    USBH_CleanupDeviceList();
  }
  USBH_Global.TimerTaskIsRunning = 0;
}

/*********************************************************************
*
*       USBH_ISRTask
*
*  Function description
*    Processes the events triggered from the interrupt handler.
*    This function must run as a separate task in order to use the emUSBH stack.
*    The functions only returns, if the USBH stack is shut down (if USBH_Exit() was called).
*    In order for the emUSB-Host to work reliably, the task should have the highest
*    priority of all tasks dealing with USB.
*
*  Additional information
*    This function waits for events from the interrupt
*    handler of the host controller and processes them.
*
*    When USBH_Exit() is used in the application this function should
*    not be directly started as a task, as it returns when USBH_Exit()
*    is called. A wrapper function can be used in this case,
*    see USBH_IsRunning() for a sample.
*/
void USBH_ISRTask(void) {
  U32 DevMask;
  unsigned i;

  USBH_LOG((USBH_MCAT_INIT, "USBH_ISRTask started"));
  USBH_Global.ISRTaskIsRunning = 1;
  for(;;) {
    DevMask = USBH_OS_WaitISR();
    //
    //  Check whether the stack is running, otherwise the task returns.
    //
    if (USBH_Global.IsRunning == 0) {
      break;
    }
    i = 0;
    while (DevMask != 0u) {
      if ((DevMask & 1u) != 0u) {
        USBH_HOST_CONTROLLER * pHost;

        pHost = USBH_HCIndex2Inst(i);
        USBH_ASSERT_MAGIC(pHost, USBH_HOST_CONTROLLER);
        (pHost->pDriver->pfIsr)(pHost->pPrvData);
      }
      DevMask >>= 1;
      i++;
    }
  }
  USBH_Global.ISRTaskIsRunning = 0;
}

/*********************************************************************
*
*       USBH_GetVersion
*
*  Function description
*    Returns the version of the stack.
*
*  Return value
*    Format: Mmmrr; e.g: 10201 is 1.02a
*/
U32 USBH_GetVersion(void) {
  return USBH_VERSION;
}

/*********************************************************************
*
*       USBH_Init
*
*  Function description
*    Initializes the emUSB-Host stack.
*
*  Additional information
*    Has to be called one time during startup before any other
*    function. The library initializes or allocates global resources
*    within this function.
*/
void USBH_Init(void) {
  unsigned i;

  USBH_MEMSET(&USBH_Global, 0, sizeof(USBH_Global));
  USBH_Global.sCopyright = "SEGGER emUSBH V" VERSION2STRING(USBH_VERSION);
  _NextTimeout = (USBH_TIME)0;
  USBH_DLIST_Init(&_TimerList);
  USBH_OS_Init();
  USBH_LOG((USBH_MCAT_INIT, "emUSB-Host Init started. Version %u.%u.%u", USBH_VERSION / 10000, (USBH_VERSION / 100) % 100, USBH_VERSION % 100));
#if USBH_DEBUG
  if (sizeof(USBH_Global.pExtHubApi) > sizeof(PTR_ADDR)) {
    USBH_PANIC(("Bad PTR_ADDR definition!"));
  }
#endif

#if USBH_SUPPORT_TRACE
  //
  // Grant SystemViewer some time to connect after the target has started.
  // A delay of 30ms seems to be enough. To be on the safe side we use 100ms
  // as it does not hurt during initialization.
  //
  USBH_OS_Delay(100);
#endif

  USBH_DLIST_Init(&USBH_Global.NotificationList);
  USBH_DLIST_Init(&USBH_Global.EnumErrorNotificationList);
  USBH_DLIST_Init(&USBH_Global.DelayedPnPNotificationList);
  USBH_DLIST_Init(&USBH_Global.DeviceRemovalNotificationList);
  USBH_Global.Config.RootHubPortsAlwaysPowered = HC_ROOTHUB_PORTS_ALWAYS_POWERED;
  USBH_Global.Config.RootHubPerPortPowered     = HC_ROOTHUB_PER_PORT_POWERED;
  USBH_Global.Config.RootHubSupportOvercurrent = HC_ROOTHUB_OVERCURRENT;
  //
  // USB host controller driver endpoint resources.
  // That are all endpoints that can be used at the same time.
  // The number of control endpoint is calculated from the number
  // of the USB devices and additional control endpoints that are
  // needed for the USB device enumeration. The following defines determine indirect
  // also additional bus master memory.
  //
  USBH_Global.Config.DefaultPowerGoodTime = 300;   // Use a default time of 300 ms after the device has been powered on.
  USBH_X_Config();
#if USBH_SUPPORT_LOG
  USBH_LOG((USBH_MCAT_INIT, "*********************************************************************"));
  USBH_LOG((USBH_MCAT_INIT, "*                       emUSB-Host Configuration                    *"));
  USBH_LOG((USBH_MCAT_INIT, "*********************************************************************"));
  if (USBH_Global.pExtHubApi == NULL) {
    USBH_LOG((USBH_MCAT_INIT, "* External hubs are NOT allowed"));
  } else {
    USBH_LOG((USBH_MCAT_INIT, "* External hubs are ALLOWED"));
  }
  USBH_LOG((USBH_MCAT_INIT, "* Time before communicating with a newly connected device: %d ms", USBH_Global.Config.DefaultPowerGoodTime));
  USBH_LOG((USBH_MCAT_INIT, "*********************************************************************"));
#endif
  USBH_InitTimer(&USBH_Global.DelayedPnPNotifyTimer, USBH_PNP_NotifyWrapperCallbackRoutine, NULL);
  USBH_Global.ConfigCompleted = 1;
  USBH_LOG((USBH_MCAT_INIT, "Init completed"));
  USBH_Global.IsRunning = 1;
  for (i = 0; i < USBH_Global.HostControllerCount; i++) {
    USBH_StartHostController(USBH_Global.aHostController[i]);
  }
  USBH_LOG((USBH_MCAT_INIT, "Enumeration of devices enabled"));
}

/*********************************************************************
*
*       USBH_Exit
*
*  Function description
*    Shuts down and de-initializes the emUSB-Host stack. All resources will be
*    freed within this function. This includes also the removing and deleting of all host controllers.
*
*    Before this function can be used, the exit functions of all initialized USB classes
*    (e.g. USBH_CDC_Exit(), USBH_MSD_Exit(), ...) must be called.
*
*    Calling USBH_Exit() will cause the functions USBH_Task() and USBH_ISRTask() to return.
*
*  Additional information
*    After this function call, no other function of the USB stack should be called.
*/
void USBH_Exit(void) {
  unsigned     NumHC;
  unsigned     i;

  USBH_LOG((USBH_MCAT_INIT, "USBH_Exit!"));
  NumHC = USBH_Global.HostControllerCount;
  for (i = 0; i < NumHC; i++) {
    USBH_RemoveHostController(USBH_Global.aHostController[i]);
  }
  //
  //  Clean up.
  //
  USBH_ASSERT(0 != USBH_DLIST_IsEmpty(&USBH_Global.NotificationList));
  USBH_ASSERT(0 != USBH_DLIST_IsEmpty(&USBH_Global.DelayedPnPNotificationList));
  USBH_UnregisterAllEnumErrorNotifications();
  USBH_ReleaseTimer(&USBH_Global.DelayedPnPNotifyTimer);
  //
  // Add a small delay before disabling the handling tasks.
  // This ensures that the very last interrupt is executed
  // and that all timers return.
  //
  USBH_OS_Delay(50);
  //
  // Wait until all USBH related tasks return.
  //
  USBH_Global.IsRunning = 0;
  while (USBH_Global.TimerTaskIsRunning != 0 || USBH_Global.ISRTaskIsRunning != 0) {
    //
    // Wake up task.
    //
    USBH_OS_Delay(10);
    USBH_OS_SignalNetEvent();
    USBH_OS_Delay(10);
    for (i = 0; i < NumHC; i++) {
      USBH_OS_SignalISREx(i);
    }
  }
  for (i = 0; i < NumHC; i++) {
    USBH_FREE(USBH_Global.aHostController[i]);
  }
  USBH_OS_DeInit();
}

/*********************************************************************
*
*       USBH_WaitEventTimed
*
*  Function description
*    Wait for the specific event within a given timeout.
*
*  Parameters
*    pEvent:        Pointer to an event object that was returned by USBH_OS_AllocEvent().
*    Timeout:       Timeout in milliseconds, 0 means infinite timeout.
*
*  Return value:
*    == USBH_OS_EVENT_SIGNALED:   Event was signaled.
*    == USBH_OS_EVENT_TIMEOUT:    Timeout occurred.
*/
int USBH_WaitEventTimed(USBH_OS_EVENT_OBJ * pEvent, U32 Timeout) {
  if (Timeout != 0u) {
    return USBH_OS_WaitEventTimed(pEvent, Timeout);
  }
  USBH_OS_WaitEvent(pEvent);
  return USBH_OS_EVENT_SIGNALED;
}

/*********************************************************************
*
*       Configuration descriptor enumeration functions
*
*  The configuration descriptor consists of the following descriptors:
*  Configuration descriptor and then the Interface descriptor.
*  After the interface descriptor follow none, one or more endpoint descriptors.
*  The configuration can have more than one interface.
*
**********************************************************************
*/

/*********************************************************************
*
*       USBH_ConfigTransferBufferSize
*
*  Function description
*    Configures the size of a copy buffer that can be used if
*    the USB controller has limited access to the system memory
*    or the system is using cached (data) memory. Transfer buffers
*    of this size are allocated for each used endpoint.
*    If this functions is not called, a driver specific default size
*    is used.
*
*  Parameters
*    HCIndex:  Index of the host controller.
*    Size:     Size of the buffer in bytes. Must be a multiple of the
*              maximum packet size (512 for high speed, 64 for full speed).
*/
void USBH_ConfigTransferBufferSize(U32 HCIndex, U32 Size) {
  USBH_HOST_CONTROLLER   * pHost;
  const USBH_HOST_DRIVER * pDriver;
  USBH_IOCTL_PARA          IoctlPara;

  pHost = USBH_HCIndex2Inst(HCIndex);
  USBH_ASSERT_MAGIC(pHost, USBH_HOST_CONTROLLER);
  pDriver = pHost->pDriver;
  USBH_ASSERT_PTR(pHost->pDriver);
  if (pDriver->pfIoctl != NULL) {
    IoctlPara.u.MaxTransferSize.Size = Size;
    (void)pDriver->pfIoctl(pHost->pPrvData, USBH_IOCTL_FUNC_CONF_MAX_XFER_BUFF_SIZE, &IoctlPara);
  }
}

/*********************************************************************
*
*       USBH_ConfigRootHub
*
*  Function description
*    Sets power related features of the host controller.
*    Not all drivers support this.
*
*  Parameters
*    SupportOvercurrent    : If over-current is supported by the host controller set to 1.
*    PortsAlwaysPowered    : Specifies whether the USB port is always powered.
*    PerPortPowered        : Specifies that the power of each port can be set individually.
*/
void USBH_ConfigRootHub(U8 SupportOvercurrent, U8 PortsAlwaysPowered, U8 PerPortPowered) {
  if (PortsAlwaysPowered != 0u && PerPortPowered != 0u) {
    USBH_PANIC("Setting PortsAlwaysPowered and PerPortPowered simultaneously is not allowed");
  }
  USBH_Global.Config.RootHubSupportOvercurrent = SupportOvercurrent;
  USBH_Global.Config.RootHubPortsAlwaysPowered = PortsAlwaysPowered;
  USBH_Global.Config.RootHubPerPortPowered     = PerPortPowered;
}

/*********************************************************************
*
*       USBH_ConfigMaxUSBDevices
*
*  Function description
*    Obsolete function.
*
*  Parameters
*    NumDevices : Maximum number of devices to be supported.
*/
void USBH_ConfigMaxUSBDevices(U8 NumDevices) {
  USBH_USE_PARA(NumDevices);
}

/*********************************************************************
*
*       USBH_ConfigMaxNumEndpoints
*
*  Function description
*    Obsolete function.
*
*  Parameters
*    MaxNumBulkEndpoints : Maximum number of Bulk endpoints to be supported.
*    MaxNumIntEndpoints  : Maximum number of Interrupt endpoints to be supported.
*    MaxNumIsoEndpoints  : Maximum number of Isochronous endpoints to be supported.
*/
void USBH_ConfigMaxNumEndpoints(U8 MaxNumBulkEndpoints, U8 MaxNumIntEndpoints, U8 MaxNumIsoEndpoints) {
  USBH_USE_PARA(MaxNumBulkEndpoints);
  USBH_USE_PARA(MaxNumIntEndpoints);
  USBH_USE_PARA(MaxNumIsoEndpoints);
}

/*********************************************************************
*
*       USBH_ConfigPortPowerPinEx
*
*  Function description
*    Setups how the port-power pin should be set in order to
*    enable port for this port.
*    In normal case low means power enable. This feature must be
*    supported by the USBH driver.
*
*  Parameters
*    HCIndex             : Index of the host controller.
*    SetHighIsPowerOn    : Select which logical voltage level enables the port.
*                          1 - To enable port power, set the pin high.
*                          0 - To enable port power, set the pin low.
*
*  Return value
*    == USBH_STATUS_SUCCESS: Configuration set.
*    == USBH_STATUS_ERROR:   Invalid HCIndex.
*/
USBH_STATUS USBH_ConfigPortPowerPinEx(U32 HCIndex, U8 SetHighIsPowerOn) {
  USBH_HOST_CONTROLLER   * pHost;
  const USBH_HOST_DRIVER * pDriver;
  USBH_IOCTL_PARA          IoctlPara;

  pHost = USBH_HCIndex2Inst(HCIndex);
  if (pHost != NULL) {
    pDriver = pHost->pDriver;
    USBH_ASSERT_PTR(pHost->pDriver);
    if (pDriver->pfIoctl != NULL) {
      IoctlPara.u.SetHighIsPowerOn = SetHighIsPowerOn;
      (void)pDriver->pfIoctl(pHost->pPrvData, USBH_IOCTL_FUNC_CONF_POWER_PIN_ON_LEVEL, &IoctlPara);
    }
    return USBH_STATUS_SUCCESS;
  } else {
    return USBH_STATUS_ERROR;
  }
}

/*********************************************************************
*
*       USBH_ConfigPortPowerPin
*
*  Function description
*    Deprecated, please use USBH_ConfigPortPowerPinEx().
*/
void USBH_ConfigPortPowerPin(U8 SetHighIsPowerOn) {
  (void)USBH_ConfigPortPowerPinEx(0, SetHighIsPowerOn);
}

/*********************************************************************
*
*       USBH_ConfigPowerOnGoodTime
*
*  Function description
*    Configures the power on time that the host waits after connecting a device
*    before starting to communicate with the device. The default value is 300 ms.
*
*  Parameters
*    PowerGoodTime : Time the stack should wait before doing any
*                    other operation (im ms).
*
*  Additional information
*    If you are dealing with problematic devices which have long initialization sequences it
*    is advisable to increase this timeout.
*/
void USBH_ConfigPowerOnGoodTime(unsigned PowerGoodTime) {
  USBH_Global.Config.DefaultPowerGoodTime = PowerGoodTime;
}

/*********************************************************************
*
*       USBH_ServiceISR
*
*  Function description
*    This routine is called from the ISR and is responsible for
*    checking whether the interrupt contains status bits which need
*    to be handled. In that case the USBH_ISRTask is signalled.
*
*  Parameters
*    Index : Zero-based index of the host controller, the index
*            is returned by the emUSB-Host driver xxx_Add functions.
*            When only one driver is included the index is zero.
*/
void USBH_ServiceISR(unsigned Index) {
  USBH_HOST_CONTROLLER * pHost;

  if (USBH_Global.IsRunning != 0) {
    pHost = USBH_HCIndex2Inst(Index);
    USBH_ASSERT_PTR(pHost);
    USBH_ASSERT_PTR(pHost->pDriver);
    if ((pHost->pDriver->pfCheckIsr)(pHost->pPrvData) != 0) {
      USBH_OS_SignalISREx(Index);
    }
  }
}

/*********************************************************************
*
*       USBH__ConvSetupPacketToBuffer
*
*  Function description
*    Converts the structure USBH_SETUP_PACKET to a byte buffer.
*
*  Parameters
*    pSetup  : Pointer to a setup packet structure.
*    pBuffer : Pointer to a empty buffer.
*/
void USBH__ConvSetupPacketToBuffer(const USBH_SETUP_PACKET * pSetup, U8 * pBuffer) {
  *pBuffer++ = pSetup->Type;
  *pBuffer++ = pSetup->Request;
  *pBuffer++ = (U8) pSetup->Value;        //LSB
  *pBuffer++ = (U8)(pSetup->Value  >> 8); //MSB
  *pBuffer++ = (U8) pSetup->Index;        //LSB
  *pBuffer++ = (U8)(pSetup->Index  >> 8); //MSB
  *pBuffer++ = (U8) pSetup->Length;       //LSB
  *pBuffer   = (U8)(pSetup->Length >> 8); //MSB
}

/*********************************************************************
*
*       USBH_SetOnSetPortPower
*
*  Function description
*    Sets a callback for the set-port-power driver function.
*    The user callback is called when the ports are added to the host driver instance, this
*    occurs during initialization, or when the ports are removed (during de-initialization).
*    Using this function is necessary if the port power is not controlled directly
*    through the USB controller but is provided from an external source.
*
*  Parameters
*    pfOnSetPortPower: Pointer to a user-provided callback function of type
*                      USBH_ON_SETPORTPOWER_FUNC.
*
*  Additional information
*    The callback function should not block.
*/
void USBH_SetOnSetPortPower(USBH_ON_SETPORTPOWER_FUNC * pfOnSetPortPower) {
  USBH_Global.pfOnSetPortPower = pfOnSetPortPower;
}

/*********************************************************************
*
*       USBH_SetOnPortEvent
*
*  Function description
*    Sets a callback to report port events to the application.
*
*  Parameters
*    pfOnPortEvent: Pointer to a user-provided callback function of type USBH_ON_PORT_EVENT_FUNC.
*
*  Additional information
*    The callback function should not block.
*/
void USBH_SetOnPortEvent(USBH_ON_PORT_EVENT_FUNC * pfOnPortEvent) {
  USBH_Global.pfOnPortEvent = pfOnPortEvent;
}

/*********************************************************************
*
*       USBH_IsRunning
*
*  Function description
*    Returns whether the stack is running or not.
*
*  Return value
*    == 0: USBH is not running
*    == 1: USBH is     running
*/
int USBH_IsRunning(void) {
  return USBH_Global.IsRunning;
}

/*********************************************************************
*
*       USBH_GetNumDevicesConnected
*
*  Function description
*    Returns the number of device connected to the USB Host controller.
*
*  Return value
*    <  0 : HCIndex is invalid.
*    >= 0 : Number of devices connected.
*/
int USBH_GetNumDevicesConnected(U32 HCIndex) {
  USBH_HOST_CONTROLLER * pHostController;
  USBH_DLIST           * pDevEntry;
  USB_DEVICE           * pUsbDev;
  int                    NumDevices;

  NumDevices  = 0;
  if (HCIndex >= USBH_Global.HostControllerCount) {
    return -1;
  }
  pHostController = USBH_Global.aHostController[HCIndex];
  USBH_ASSERT_MAGIC(pHostController, USBH_HOST_CONTROLLER);
  USBH_LockDeviceList(pHostController);
  pDevEntry = USBH_DLIST_GetNext(&pHostController->DeviceList);
  while (pDevEntry != &pHostController->DeviceList) {
    pUsbDev       = GET_USB_DEVICE_FROM_ENTRY(pDevEntry);
    USBH_ASSERT_MAGIC(pUsbDev, USB_DEVICE);
    if (pUsbDev->RefCount != 0) {
      NumDevices++;
    }
    pDevEntry = USBH_DLIST_GetNext(pDevEntry);
  }
  USBH_UnlockDeviceList(pHostController);
  return NumDevices;
}

#if USBH_SUPPORT_VIRTUALMEM
/*********************************************************************
*
*       USBH_Config_SetV2PHandler
*
*  Function description
*    Sets a virtual address to physical address translator.
*    Is required, if the physical address is not equal to the virtual address
*    of the memory used for DMA access (address translation by an MMU).
*    See USBH_AssignTransferMemory.
*
*  Parameters
*    pfV2PHandler     :  Handler to be called to convert virtual address.
*/
void USBH_Config_SetV2PHandler(USBH_V2P_FUNC * pfV2PHandler) {
  USBH_Global.pfV2P = pfV2PHandler;
}

/*********************************************************************
*
*       USBH_v2p
*
*  Function description
*    Converts a virtual address pointer to the physical address
*    which will then be used for the DMA engine of the USB controller.
*    This is only necessary for CPUs which have a MMU and are capable of
*    generating virtual addresses.
*    Handling is done by using a call-back which can be set by
*    USBH_Config_SetV2PHandler. If not set, default mapping is done:
*    Virtual address = physical address.
*    Please note that those virtual addresses always have to
*    be in a memory pool, which is non-cache-able and non-buffer-able.
*
*  Parameters
*    pVirtAddr     :  Pointer which has to be converted to a physical address.
*
*  Return value
*    Physical address of the given pointer.
*/
PTR_ADDR USBH_v2p(void * pVirtAddr) {
  if (USBH_Global.pfV2P != NULL) {
     return (USBH_Global.pfV2P)(pVirtAddr);
  }
  return SEGGER_PTR2ADDR(pVirtAddr);      // lint D:103[a]
}
#endif


/*********************************************************************
*
*       USBH_HCIndex2Inst
*
*  Function description
*    Converts a Driver index into a driver instance and returns the
*    pointer to the desired driver instance structure.
*    If the index is invalid the return value is NULL.
*
*  Parameters
*    HostControllerIndex : Index of the host controller.
*
*  Return value
*    NULL  : The index is not valid.
*    Other : Pointer to the driver instance.
*/
#ifndef USBH_HCIndex2Inst
USBH_HOST_CONTROLLER * USBH_HCIndex2Inst(U32 HostControllerIndex) {
  if (HostControllerIndex >= USBH_Global.HostControllerCount) {
    USBH_WARN((USBH_MCAT_HC, "Core: Bad host controller index %u", HostControllerIndex));
    return NULL;
  }
  return USBH_Global.aHostController[HostControllerIndex];
}
#endif

/*********************************************************************
*
*       USBH_SetCacheConfig()
*
*  Function description
*    Configures cache related functionality that might be required by
*    the stack for several purposes such as cache handling in drivers.
*
*  Parameters
*    pConfig : Pointer to an element of SEGGER_CACHE_CONFIG .
*    ConfSize: Size of the passed structure in case library and
*              header size of the structure differs.
*
*  Additional information
*    This function has to called in USBH_X_Config().
*/
void USBH_SetCacheConfig(const SEGGER_CACHE_CONFIG *pConfig, unsigned ConfSize) {
  if (ConfSize > sizeof(USBH_CacheConfig)) {
    ConfSize = sizeof(USBH_CacheConfig);
  }
  USBH_MEMSET(&USBH_CacheConfig, 0, sizeof(USBH_CacheConfig));
  USBH_MEMCPY(&USBH_CacheConfig, pConfig, ConfSize);
}

/*********************************************************************
*
*       USBH_AddOnSetConfigurationHook
*
*  Function description
*    Adds a callback function that will be called when the device that is being enumerated has
*    more than one configuration.
*
*  Parameters
*    pHook:                 Pointer to a USBH_SET_CONF_HOOK structure (will be initialized by this function).
*    pfOnSetConfiguration:  Pointer to the callback routine that will be called.
*    pContext:              A pointer to the user context which is used as parameter for the callback function.
*
*  Additional information
*    The USB_HOOK structure is private to the USB stack. It will be initialized by USBH_AddOnSetConfigurationHook().
*    The USB stack keeps track of all state change callback functions using a linked list. The
*    USBH_SET_CONF_HOOK structure will be included into this linked list and must reside
*    in static memory.
*
*    Note that the callback function will be called within an ISR, therefore it should never block.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_AddOnSetConfigurationHook(USBH_SET_CONF_HOOK * pHook, USBH_ONSETCONFIGURATION_FUNC * pfOnSetConfiguration, void * pContext) {
  USBH_SET_CONF_HOOK *  p;

  //
  // Check if this hook is already in list. If so, return error.
  //
  p = USBH_Global.pFirstOnSetConfHook;
  while (p != NULL) {
    if (p == pHook) {
      return USBH_STATUS_ALREADY_ADDED;     // Error, hook already in list.
    }
    p = p->pNext;
  }
  USBH_MEMSET(pHook, 0, sizeof(USBH_SET_CONF_HOOK));
  pHook->pfOnSetConfig = pfOnSetConfiguration;
  pHook->pContext      = pContext;
  //
  // Make new hook first in list.
  //
  pHook->pNext = USBH_Global.pFirstOnSetConfHook;
  USBH_Global.pFirstOnSetConfHook = pHook;
  return USBH_STATUS_SUCCESS;
}

/*************************** End of file ****************************/
