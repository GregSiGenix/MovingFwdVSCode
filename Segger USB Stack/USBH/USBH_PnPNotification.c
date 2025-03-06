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
File        : USBH_PnPNotification.c
Purpose     : Handle PNP notification objects
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/

#include "USBH_Int.h"

/*********************************************************************
*
*       USBH_PNP_NotifyWrapperCallbackRoutine
*
*  Function description
*    Instead direct call of the user PNP notification routine an timer
*    routine calls the user notification callback routines.
*    wrapper pContext is used to call the user notification routines
*    in the timer pContext.
*/
void USBH_PNP_NotifyWrapperCallbackRoutine(void * pContext) {
  USBH_DLIST                 * pEntry;
  DELAYED_PNP_NOTIFY_CONTEXT * pDelayedPnPContext;

  USBH_LOG((USBH_MCAT_PNP, "USBH_PNP_NotifyWrapperCallbackRoutine"));
  USBH_USE_PARA(pContext);
  //
  // Search all entries in DelayedPnPNotificationList
  // and execute the notification routine.
  // Delete the entry from the list.
  //
  while (USBH_DLIST_IsEmpty(&USBH_Global.DelayedPnPNotificationList) == 0) { // Check all entries
    pEntry             = USBH_DLIST_GetNext(&USBH_Global.DelayedPnPNotificationList);
    pDelayedPnPContext = GET_DELAYED_PNP_NOTIFY_CONTEXT_FROM_ENTRY(pEntry);
    USBH_ASSERT_MAGIC(pDelayedPnPContext, DELAYED_PNP_NOTIFY_CONTEXT);
    USBH_ASSERT_PTR  (pDelayedPnPContext->pfNotifyCallback);
    USBH_DLIST_RemoveEntry(pEntry);                                    // Remove entry from the list
    // Call the notification routine and release the list object
    USBH_LOG((USBH_MCAT_PNP, "USBH_PNP_NotifyWrapperCallbackRoutine notification for interface ID: %d!",pDelayedPnPContext->Id));
    pDelayedPnPContext->pfNotifyCallback(pDelayedPnPContext->pContext, pDelayedPnPContext->Event, pDelayedPnPContext->Id);
    USBH_FREE(pDelayedPnPContext);
  }
}

/*********************************************************************
*
*       _PNP_ProcessDeviceNotifications
*
*  Function description
*    If this interface matches with the interface Mask of pPnpNotification
*    the Event notification function is called with the Event.
*
*  Parameters
*    pPnpNotification  : Pointer to the notification.
*    pDev              : Pointer to a device.
*    Event             : Device is connected, device is removed!
*                        Normally one device at the time is changed.
*/
static void _PNP_ProcessDeviceNotifications(USBH_NOTIFICATION * pPnpNotification, const USB_DEVICE * pDev, USBH_PNP_EVENT Event) {
  USB_INTERFACE              * pIface;
  USBH_ON_PNP_EVENT_FUNC     * pNotifyCallback; // Notification function
  void                       * pContext;
  USBH_INTERFACE_MASK        * iMask;
  USBH_DLIST                 * pEntry;
  DELAYED_PNP_NOTIFY_CONTEXT * pDelayedPnpContext;

  USBH_ASSERT_MAGIC(pPnpNotification, USBH_PNP_NOTIFICATION);
  USBH_ASSERT_MAGIC(pDev, USB_DEVICE);
  // Get notification values
  pNotifyCallback = pPnpNotification->Notification.PNP.pfPnpNotification;
  pContext        = pPnpNotification->Notification.PNP.pContext;
  iMask           = &pPnpNotification->Notification.PNP.InterfaceMask;
  pEntry          = USBH_DLIST_GetNext(&pDev->UsbInterfaceList);
  while (pEntry != &pDev->UsbInterfaceList) { // Search in all device interfaces and notify every interface
    pIface = GET_USB_INTERFACE_FROM_ENTRY(pEntry);
    USBH_ASSERT_MAGIC(pIface, USB_INTERFACE);
    if (USBH_STATUS_SUCCESS == USBH_CompareUsbInterface(pIface, iMask, TRUE)) {
      // One of the devices interfaces does match
      pDelayedPnpContext = (DELAYED_PNP_NOTIFY_CONTEXT *)USBH_TRY_MALLOC_ZEROED(sizeof(DELAYED_PNP_NOTIFY_CONTEXT));
      if (NULL == pDelayedPnpContext) {
        USBH_WARN((USBH_MCAT_PNP, "_PNP_ProcessDeviceNotifications: no memory"));
      } else { // Initialize the allocated delayed Pnp pContext
        USBH_LOG((USBH_MCAT_PNP, "_PNP_ProcessDeviceNotifications: pfNotifyCallback: USB addr:%u Interf.ID: %d Event:%d", pDev->UsbAddress, pIface->InterfaceId, Event));
        USBH_IFDBG(pDelayedPnpContext->Magic = DELAYED_PNP_NOTIFY_CONTEXT_MAGIC);
        pDelayedPnpContext->pContext         = pContext;
        pDelayedPnpContext->Event            = Event;
        pDelayedPnpContext->Id               = pIface->InterfaceId;
        pDelayedPnpContext->pfNotifyCallback = pNotifyCallback;
        USBH_DLIST_InsertTail(&USBH_Global.DelayedPnPNotificationList, &pDelayedPnpContext->ListEntry); // Insert entry at the tail of the list
        USBH_StartTimer(&USBH_Global.DelayedPnPNotifyTimer, 1);
      }
    }
    pEntry = USBH_DLIST_GetNext(pEntry);
  }
}

/*********************************************************************
*
*       USBH_PNP_ProcessNotification
*
*  Function description
*    If found an valid interface the USBH_ADD_DEVICE Event is sent.
*    if not found an valid interface nothing is sent.
*    This function is called the first time an notification is registered.
*    It searches in all host controller device lists.
*/
void USBH_PNP_ProcessNotification(USBH_NOTIFICATION * pPnpNotification) {
  USBH_DLIST           * pDevEntry;
  USBH_HOST_CONTROLLER * pHost;
  USB_DEVICE           * pUSBDev;
  unsigned               NumHC;
  unsigned               i;

  // Notification function
  USBH_LOG((USBH_MCAT_PNP, "USBH_PNP_ProcessNotification"));
  USBH_ASSERT_MAGIC(pPnpNotification, USBH_PNP_NOTIFICATION);
  NumHC = USBH_Global.HostControllerCount;
  for (i = 0; i < NumHC; i++) {       // Search in all host controller
    pHost = USBH_Global.aHostController[i];
    USBH_ASSERT_MAGIC(pHost, USBH_HOST_CONTROLLER);
    USBH_LockDeviceList(pHost);
    pDevEntry = USBH_DLIST_GetNext(&pHost->DeviceList);
    while (pDevEntry != &pHost->DeviceList) {
      pUSBDev = GET_USB_DEVICE_FROM_ENTRY(pDevEntry);
      USBH_ASSERT_MAGIC(pUSBDev, USB_DEVICE);
      if (pUSBDev->RefCount != 0) {
        _PNP_ProcessDeviceNotifications(pPnpNotification, pUSBDev, USBH_ADD_DEVICE);
      }
      pDevEntry = USBH_DLIST_GetNext(pDevEntry);
    }
    USBH_UnlockDeviceList(pHost);
  }
}

/*********************************************************************
*
*       USBH_ProcessDevicePnpNotifications
*
*  Function description
*    Called if an device is successful added to the device list
*    or before it is removed from the device list.
*    If match an devices interface with one of the notification list
*    the notification function is called.
*
*  Parameters
*     pDevice - Pointer to the device instance.
*     Event   - Device event.
*/

void USBH_ProcessDevicePnpNotifications(const USB_DEVICE * pDevice, USBH_PNP_EVENT Event) {
  USBH_DLIST        * pEntry;
  USBH_NOTIFICATION * pNotification;

  USBH_ASSERT_MAGIC(pDevice, USB_DEVICE);
  pEntry = USBH_DLIST_GetNext(&USBH_Global.NotificationList);
  while (pEntry != &USBH_Global.NotificationList) {
    pNotification = GET_NOTIFICATION_FROM_ENTRY(pEntry);
    USBH_ASSERT_MAGIC(pNotification, USBH_PNP_NOTIFICATION);
    _PNP_ProcessDeviceNotifications(pNotification, pDevice, Event);
    pEntry = USBH_DLIST_GetNext(pEntry);
  }
}

/*********************************************************************
*
*       USBH_RegisterPnPNotification
*
*  Function description
*    Registers a notification function for PnP events.
*
*  Parameters
*    pPnPNotification:    Pointer to a caller provided structure.
*
*  Return value
*    On success a valid handle to the added notification is returned. A NULL is returned in
*    case of an error.
*
*  Additional information
*    An application can register any number of
*    notifications. The user notification routine is called in the context of a notify timer
*    that is global for all USB bus PnP notifications. If this function is called while the bus
*    driver has already enumerated devices that match the USBH_INTERFACE_MASK the
*    callback function passed in the USBH_PNP_NOTIFICATION structure is called for each
*    matching interface.
*/
USBH_NOTIFICATION_HANDLE USBH_RegisterPnPNotification(const USBH_PNP_NOTIFICATION * pPnPNotification) {
  USBH_NOTIFICATION * pNotification;

  USBH_LOG((USBH_MCAT_PNP, "USBH_RegisterPnPNotification: VendorId: 0x%x ProductId: 0x%x interface: %u", pPnPNotification->InterfaceMask.VendorId, pPnPNotification->InterfaceMask.ProductId, pPnPNotification->InterfaceMask.Interface));
  pNotification = (USBH_NOTIFICATION *)USBH_TRY_MALLOC_ZEROED(sizeof(USBH_NOTIFICATION));
  if (pNotification == NULL) {
    USBH_WARN((USBH_MCAT_PNP, "USBH_RegisterPnPNotification: No memory"));
    return NULL;
  }
  USBH_IFDBG(pNotification->Magic = USBH_PNP_NOTIFICATION_MAGIC);
  pNotification->Notification.PNP = *pPnPNotification;
  USBH_DLIST_InsertTail(&USBH_Global.NotificationList, &pNotification->ListEntry);
  // Always USBH_ADD_DEVICE is sent after the notification function is added if an interface is available
  USBH_PNP_ProcessNotification(pNotification);
  return pNotification;
}

/*********************************************************************
*
*       USBH_UnregisterPnPNotification
*
*  Function description
*    Removes a previously registered notification for PnP events.
*
*  Parameters
*    hNotification:    A valid handle for a PnP notification previously registered
*                      by a call to USBH_RegisterPnPNotification().
*
*  Additional information
*    Must be called for to unregister a PnP notification that was successfully registered by a call to
*    USBH_RegisterPnPNotification().
*/
void USBH_UnregisterPnPNotification(USBH_NOTIFICATION_HANDLE hNotification) {
  USBH_NOTIFICATION * pNotification;

  USBH_LOG((USBH_MCAT_PNP, "USBH_UnregisterPnPNotification!"));
  pNotification = hNotification;
  USBH_ASSERT_MAGIC(pNotification, USBH_PNP_NOTIFICATION);
  USBH_DLIST_RemoveEntry(&pNotification->ListEntry);
  USBH_FREE(pNotification);
}

/*********************************************************************
*
*       USBH_RegisterDeviceRemovalNotification
*
*  Function description
*/
USBH_NOTIFICATION_HANDLE USBH_RegisterDeviceRemovalNotification(const USBH_DEV_REM_NOTIFICATION * pDevRemNotification) {
  USBH_NOTIFICATION * pNotification;

  pNotification = (USBH_NOTIFICATION *)USBH_TRY_MALLOC_ZEROED(sizeof(USBH_NOTIFICATION));
  if (NULL == pNotification) {
    USBH_WARN((USBH_MCAT_PNP, "USBH_RegisterDeviceRemovalNotification: No memory"));
    return NULL;
  }
  USBH_IFDBG(pNotification->Magic = USBH_DEV_REM_NOTIFICATION_MAGIC);
  pNotification->Notification.DevRem = *pDevRemNotification;
  USBH_DLIST_InsertTail(&USBH_Global.DeviceRemovalNotificationList, &pNotification->ListEntry);
  return pNotification;
}

/*********************************************************************
*
*       USBH_UnegisterDeviceRemovalNotification
*
*  Function description
*/
void USBH_UnegisterDeviceRemovalNotification(USBH_NOTIFICATION_HANDLE Handle) {
  USBH_NOTIFICATION * pNotification;

  pNotification = (USBH_NOTIFICATION *)Handle;
  USBH_ASSERT_MAGIC(pNotification, USBH_DEV_REM_NOTIFICATION);
  USBH_DLIST_RemoveEntry(&pNotification->ListEntry);
  USBH_FREE(pNotification);
}

/*********************************************************************
*
*       USBH__AddNotification
*
*  Function description
*    Adds a callback in order to be notified when a device is added or removed.
*
*  Parameters
*    pHook           : Pointer to a user provided USBH_NOTIFICATION_HOOK structure, which is initialized and used
*                      by this function. The memory area must be valid, until the notification is removed.
*    pfNotification  : Pointer to a function the stack should call when a device is connected or disconnected.
*    pContext        : Pointer to a user context that is passed to the callback function.
*    ppFirst         : Pointer to a pointer to the first hook USBH_NOTIFICATION_HOOK structure.
*    Handle          : Handler returned by USBH_RegisterPnPNotification.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH__AddNotification(USBH_NOTIFICATION_HOOK * pHook, USBH_NOTIFICATION_FUNC * pfNotification,
                                  void * pContext, USBH_NOTIFICATION_HOOK ** ppFirst, USBH_NOTIFICATION_HANDLE Handle) {
  USBH_NOTIFICATION_HOOK *  p;

  USBH_MEMSET(pHook, 0, sizeof(USBH_NOTIFICATION_HOOK));
  pHook->pfNotification = pfNotification;
  pHook->pContext = pContext;
  pHook->Handle   = Handle;
  //
  // Check if this hook is already in list. If so, return error.
  //
  p = *ppFirst;
  while (p != NULL) {
    if (p == pHook) {
      if (p->pfNotification == pfNotification && p->pContext == pContext) {
        return USBH_STATUS_ALREADY_ADDED;     // Error, hook already in list.
      } else {
        p->pContext = pContext;
        p->pfNotification = pfNotification;
        return USBH_STATUS_SUCCESS;
      }
    }
    p = p->pNext;
  }
  //
  // Make new hook first in list.
  //
  pHook->pNext = *ppFirst;
  *ppFirst = pHook;
  return USBH_STATUS_SUCCESS;
}

/*********************************************************************
*
*       USBH__RemoveNotification
*
*  Function description
*    Removes a callback added via USBH__AddNotification.
*
*  Parameters
*    pHook           : Pointer to a user provided USBH_NOTIFICATION_HOOK variable.
*    ppFirst         : Pointer to a pointer to the first hook USBH_NOTIFICATION_HOOK structure.
*
*  Return value
*    == USBH_STATUS_SUCCESS       : Notification removed.
*    == USBH_STATUS_INVALID_PARAM : Notification not found in the list.
*/
USBH_STATUS USBH__RemoveNotification(const USBH_NOTIFICATION_HOOK * pHook, USBH_NOTIFICATION_HOOK ** ppFirst) {
  USBH_NOTIFICATION_HOOK *  p;
  USBH_NOTIFICATION_HOOK *  pPrev;

  p = *ppFirst;
  if (p == pHook) {
    if (pHook->Handle != NULL) {
      USBH_UnregisterPnPNotification(pHook->Handle);
    }
    *ppFirst = p->pNext;
    return USBH_STATUS_SUCCESS;
  } else {
    pPrev = p;
    p = p->pNext;
    while (p != NULL) {
      if (p == pHook) {
        if (pHook->Handle != NULL) {
          USBH_UnregisterPnPNotification(pHook->Handle);
        }
        pPrev->pNext = p->pNext;
        return USBH_STATUS_SUCCESS;
      }
      pPrev = p;
      p = p->pNext;
    }
  }
  return USBH_STATUS_INVALID_PARAM;
}

/*********************************************************************
*
*       USBH_PnPNotificationIdle
*
*  Function description
*    Check, if PNP notification are pending.
*
*  Return value
*    == 0:  At least one PNP notification is pending to be executed.
*    == 1:  No PNP notifications pending.
*/
int USBH_PnPNotificationIdle(void) {
  return USBH_DLIST_IsEmpty(&USBH_Global.DelayedPnPNotificationList);
}

/*************************** End of file ****************************/
