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
File        : USBH_Enum.c
Purpose     : USB host implementation
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/

#include "USBH_Int.h"
#include "USBH_Util.h"

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/
/*********************************************************************
*
*       USBH_RegisterEnumErrorNotification
*
*  Function description
*    Registers a notification for a port enumeration error.
*
*  Parameters
*    pContext:              A user defined pointer that is passed unchanged to the
*                           notification callback function.
*    pfEnumErrorCallback:   A pointer to a notification function of type
*                           USBH_ON_ENUM_ERROR_FUNC that is called if a port enumeration error occurs.
*
*  Return value
*    On success a valid handle to the added notification is returned. A NULL is returned in
*    case of an error.
*
*  Additional information
*    To remove the notification USBH_UnregisterEnumErrorNotification() must be called. The
*    pfOnEnumError callback routine is called in the context of the process where the
*    interrupt status of a host controller is processed. The callback routine must not block.
*/
USBH_ENUM_ERROR_HANDLE USBH_RegisterEnumErrorNotification(void * pContext, USBH_ON_ENUM_ERROR_FUNC * pfEnumErrorCallback) {
  ENUM_ERROR_NOTIFICATION * pNotification;

  USBH_LOG((USBH_MCAT_PNP, "USBH_RegisterEnumErrorNotification context: 0%p",pContext));
  USBH_ASSERT_PTR(pfEnumErrorCallback);
  //
  // Create new pNotification
  //
  pNotification = (ENUM_ERROR_NOTIFICATION *)USBH_TRY_MALLOC_ZEROED(sizeof(ENUM_ERROR_NOTIFICATION));
  if (NULL == pNotification) {
    USBH_WARN((USBH_MCAT_PNP, "USBH_RegisterEnumErrorNotification(): USBH_MALLOC!"));
    return pNotification;
  }
  USBH_IFDBG(pNotification->Magic = ENUM_ERROR_NOTIFICATION_MAGIC);
  pNotification->pContext         = pContext;
  pNotification->pfOnEnumError    = pfEnumErrorCallback;
  USBH_DLIST_InsertTail(&USBH_Global.EnumErrorNotificationList, &pNotification->ListEntry);
  return pNotification;
}

/*********************************************************************
*
*       USBH_UnregisterEnumErrorNotification
*
*  Function description
*    Removes a registered notification for a port enumeration error.
*
*  Parameters
*    hEnumError:      A valid handle for the notification previously returned
*                     from  USBH_RegisterEnumErrorNotification().
*
*  Additional information
*    Must be called for a port enumeration error notification that was successfully
*    registered by a call to USBH_RegisterEnumErrorNotification().
*/
void USBH_UnregisterEnumErrorNotification(USBH_ENUM_ERROR_HANDLE hEnumError) {
  ENUM_ERROR_NOTIFICATION * pNotification;

  USBH_LOG((USBH_MCAT_PNP, "USBH_UnregisterEnumErrorNotification!"));
  pNotification = hEnumError;
  USBH_ASSERT_MAGIC(pNotification, ENUM_ERROR_NOTIFICATION);
  USBH_DLIST_RemoveEntry(&pNotification->ListEntry);
  USBH_FREE(pNotification);
}

/*********************************************************************
*
*       USBH_UnregisterAllEnumErrorNotifications
*
*  Function description
*    Removes all registered notification for a port enumeration error.
*/
void USBH_UnregisterAllEnumErrorNotifications(void) {
  USBH_DLIST              * pListHead;
  USBH_DLIST              * pEntry;
  ENUM_ERROR_NOTIFICATION * pNotification;

  pListHead = &USBH_Global.EnumErrorNotificationList;
  for (;;) {
    pEntry = USBH_DLIST_GetNext(pListHead);
    if (pEntry == pListHead) {
      break;
    }
    pNotification = GET_ENUM_ERROR_NOTIFICATION_FROM_ENTRY(pEntry);
    USBH_UnregisterEnumErrorNotification(pNotification);
  }
}

/*********************************************************************
*
*       USBH_SetEnumErrorNotification
*
*  Function description
*    Called from any device enumeration state machine if an error occurs.
*/
void USBH_SetEnumErrorNotification(unsigned Flags, USBH_STATUS Status, int ExtInfo, unsigned PortNumber) {
  USBH_DLIST              * pEntry;
  USBH_DLIST              * pNotifyList;
  ENUM_ERROR_NOTIFICATION * pEnumErrorNotify;
  USBH_ENUM_ERROR           EnumError;

  USBH_LOG((USBH_MCAT_PNP, "USBH_SetEnumErrorNotification!"));
  USBH_MEMSET(&EnumError, 0, sizeof(EnumError));
  EnumError.Flags                    = Flags;
  EnumError.ExtendedErrorInformation = ExtInfo;
  EnumError.Status                   = Status;
  EnumError.PortNumber               = (int)PortNumber;
  //
  // Walk trough the driver enum error notify list and notify user from enum error!
  //
  pNotifyList = &USBH_Global.EnumErrorNotificationList;
  pEntry      = USBH_DLIST_GetNext(pNotifyList);
  while (pEntry != pNotifyList) {
    pEnumErrorNotify = GET_ENUM_ERROR_NOTIFICATION_FROM_ENTRY(pEntry);
    USBH_ASSERT_MAGIC(pEnumErrorNotify, ENUM_ERROR_NOTIFICATION);
    pEnumErrorNotify->pfOnEnumError(pEnumErrorNotify->pContext, &EnumError);
    pEntry = USBH_DLIST_GetNext(pEntry);
  }
}

/*********************************************************************
*
*       USBH_RestartEnumError
*
*  Function description
*    Restarts the enumeration process for all devices that have failed to enumerate.
*
*  Additional information
*   If any problem occur during enumeration of a device, the device is reset and enumeration is retried.
*   To avoid an endless enumeration loop on broken devices there is a maximum retry count of 5 (USBH_RESET_RETRY_COUNTER).
*   After the retry count is expired, the port where the device is connected to is finally disabled.
*   Calling USBH_RestartEnumError() resets the retry counts and restarts enumeration on disabled ports.
*/
void USBH_RestartEnumError(void) {
  unsigned               NumHC;
  unsigned               j;
  USBH_HUB_PORT        * pPort;
  unsigned               i;
  USBH_HOST_CONTROLLER * pHostController;
  const USBH_HOST_DRIVER * pDriver;

  USBH_LOG((USBH_MCAT_PNP, "USBH_RestartEnumError!"));
  //
  // For all hosts checks all ports
  //
  NumHC = USBH_Global.HostControllerCount;
  for (j = 0; j < NumHC; j++) {       // Search in all host controller
    pHostController = USBH_Global.aHostController[j];
    USBH_ASSERT_MAGIC(pHostController, USBH_HOST_CONTROLLER);
    //
    // First checks all root hub ports
    //
    pPort = pHostController->RootHub.pPortList;
    for (i = 0; i < pHostController->RootHub.PortCount; i++) {
      USBH_ASSERT_MAGIC(pPort, USBH_HUB_PORT);
      pPort->RetryCounter = 0;
      if ((pPort->PortStatus & PORT_STATUS_POWER) == 0u) {
        pDriver = pHostController->pDriver;
        pDriver->pfSetPortPower(pHostController->pPrvData, pPort->HubPortNumber, 1);
      }
      pPort++;
    }
    //
    // Check extern HUB ports
    //
    if (USBH_Global.pExtHubApi != NULL) {
      USBH_Global.pExtHubApi->pfReStartHubPort(pHostController);
    }
    USBH_HC_ServicePorts(pHostController); // Services all host controller ports
  }
}

/*************************** End of file ****************************/
