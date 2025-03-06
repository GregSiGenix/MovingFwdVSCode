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
File        : USBH_RootHub.c
Purpose     : Root HUB state machine.
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
*       Defines, fixed
*
**********************************************************************
*/

/*********************************************************************
*
*       Static const
*
**********************************************************************
*/
static const USBH_SPEED _SpeedTab[] = {
  USBH_FULL_SPEED,
  USBH_LOW_SPEED,
  USBH_HIGH_SPEED,
  USBH_SUPER_SPEED
};

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _PortResetSetIdleServicePorts
*
*  Function description
*    Set state machine to idle.
*/
static void _PortResetSetIdleServicePorts(ROOT_HUB * pRootHub) {
  USBH_ASSERT_MAGIC(pRootHub, ROOT_HUB);
  pRootHub->PortResetEnumState = USBH_HUB_PORTRESET_IDLE;
  pRootHub->pEnumDevice = NULL;
  pRootHub->pEnumPort   = NULL;
  // Allow starting an port reset on another port
  USBH_ReleaseActivePortReset(pRootHub->pHostController);
  USBH_HC_DEC_REF(pRootHub->pHostController);
}

/*********************************************************************
*
*       _PortEvent
*
*  Function description
*    Signal a port event to application.
*/
static void _PortEvent(USBH_PORT_EVENT_TYPE EventType, const USBH_HOST_CONTROLLER * pHostController, const USBH_HUB_PORT * pHubPort) {
  USBH_PORT_EVENT Event;

  if (USBH_Global.pfOnPortEvent != NULL) {
    Event.Event          = EventType;
    Event.HCIndex        = pHostController->Index;
    Event.PortNumber     = pHubPort->HubPortNumber;
    Event.HubInterfaceId = 0;
    USBH_Global.pfOnPortEvent(&Event);
  }
}

/*********************************************************************
*
*       _PortResetFail
*
*  Function description
*    Called when the state machine encounters an error.
*    The state machine is restarted (if bRetry == TRUE) for this
*    port or stopped until a de-connect occurs.
*/
static void _PortResetFail(ROOT_HUB * pRootHub, USBH_STATUS Status, USBH_BOOL bRetry) {
  const USBH_HOST_DRIVER * pDriver;
  USBH_HUB_PORT          * pEnumPort;
  unsigned                 Flags;

  USBH_ASSERT_MAGIC(pRootHub, ROOT_HUB);
  pEnumPort = pRootHub->pEnumPort;
  USBH_ASSERT_MAGIC(pEnumPort, USBH_HUB_PORT);
  USBH_WARN((USBH_MCAT_RHUB, "_PortResetFail: %s", USBH_HubPortResetState2Str(pRootHub->PortResetEnumState)));
  pDriver = pRootHub->pHostController->pDriver;
  pDriver->pfDisablePort(pRootHub->pHostController->pPrvData, pEnumPort->HubPortNumber);
  if (bRetry != FALSE) {
    Flags = USBH_ENUM_ERROR_ROOT_PORT_RESET | USBH_ENUM_ERROR_RETRY_FLAG;
  } else {
    pEnumPort->RetryCounter = USBH_RESET_RETRY_COUNTER;
    Flags = USBH_ENUM_ERROR_ROOT_PORT_RESET | USBH_ENUM_ERROR_STOP_ENUM_FLAG;
  }
  //
  // Notify user from port enumeration error
  //
  if ((pEnumPort->PortStatus & PORT_STATUS_CONNECT) == 0u) {
    Flags |= USBH_ENUM_ERROR_DISCONNECT_FLAG;
  }
  USBH_SetEnumErrorNotification(Flags, Status, (int)pRootHub->PortResetEnumState, pEnumPort->HubPortNumber);
  if (NULL != pRootHub->pEnumDevice) {
    USBH_DEC_REF(pRootHub->pEnumDevice);    // Delete the device, this is the initial reference on default
    pRootHub->pEnumDevice = NULL;
  }
  _PortResetSetIdleServicePorts(pRootHub);
}

/*********************************************************************
*
*       _ProcessPortResetSetAddress
*
*  Function description
*/
static void _ProcessPortResetSetAddress(void * pContext) {
  const USBH_HOST_DRIVER * pDriver;
  USBH_HUB_PORT          * pEnumPort;
  ROOT_HUB               * pRootHub;
  USBH_URB               * pUrb;
  USBH_STATUS              Status;

  pRootHub    = USBH_CTX2PTR(ROOT_HUB, pContext);
  USBH_ASSERT_MAGIC(pRootHub, ROOT_HUB);
  pDriver     = pRootHub->pHostController->pDriver;
  pEnumPort   = pRootHub->pEnumPort;
#if USBH_DEBUG > 1
  if (pRootHub->PortResetEnumState >= USBH_HUB_PORTRESET_START) {
    USBH_ASSERT_MAGIC(pEnumPort, USBH_HUB_PORT);
  }
#endif

  if (pRootHub->pHostController->State < HC_WORKING) {
    _PortResetFail(pRootHub, USBH_STATUS_CANCELED, FALSE);
    return;
  }
  if (pRootHub->PortResetEnumState >= USBH_HUB_PORTRESET_WAIT_RESTART) {
    //
    // Check, if port is still connected
    //
    if ((pEnumPort->PortStatus & PORT_STATUS_CONNECT) == 0u) {
      //
      // Port was disconnected.
      //
      USBH_WARN((USBH_MCAT_RHUB, "_ProcessPortResetSetAddress: Port disconnected after port reset"));
      _PortResetFail(pRootHub, USBH_STATUS_PORT, TRUE);
      return;
    } else {
      if (pRootHub->PortResetEnumState >= USBH_HUB_PORTRESET_WAIT_RESET_0) {
        //
        // Check if port is enabled
        //
        if ((pEnumPort->PortStatus & PORT_STATUS_ENABLED) == 0u) {
          //
          // Port is not enabled: Restart state machine
          //
          USBH_WARN((USBH_MCAT_RHUB, "_ProcessPortResetSetAddress: Port disabled after port reset"));
          _PortResetFail(pRootHub, USBH_STATUS_PORT, TRUE);
          return;
        }
      }
    }
  }
  USBH_LOG((USBH_MCAT_RHUB_SM, "_ProcessPortResetSetAddress: %s", USBH_HubPortResetState2Str(pRootHub->PortResetEnumState)));
  switch (pRootHub->PortResetEnumState) {
  case USBH_HUB_PORTRESET_START:
    //
    // Normal port reset: wait before reseting the port
    //
    pRootHub->PortResetEnumState = USBH_HUB_PORTRESET_WAIT_RESTART;
    USBH_URB_SubStateWait(&pRootHub->SubState, USBH_Global.Config.DefaultPowerGoodTime, NULL);
    break;
  case USBH_HUB_PORTRESET_RESTART:
    //
    // Delayed port reset: wait about one second
    //
    pRootHub->PortResetEnumState = USBH_HUB_PORTRESET_WAIT_RESTART;
    USBH_URB_SubStateWait(&pRootHub->SubState, USBH_Global.Config.DefaultPowerGoodTime + USBH_DELAY_FOR_REENUM, NULL);
    break;
  case USBH_HUB_PORTRESET_WAIT_RESTART:
    //
    // Now reset the port.
    //
    pRootHub->PortResetEnumState = USBH_HUB_PORTRESET_WAIT_RESET_0;
    pDriver->pfResetPort(pRootHub->pHostController->pPrvData, pEnumPort->HubPortNumber);
    USBH_URB_SubStateWait(&pRootHub->SubState, USBH_WAIT_AFTER_RESET, NULL);
    break;
  case USBH_HUB_PORTRESET_WAIT_RESET_0:
    //
    // Ok, port is enabled now
    //
    pEnumPort->PortSpeed = _SpeedTab[PORT_STATUS_SPEED_IDX(pEnumPort->PortStatus)];
    //
    // Create device object
    //
    USBH_ASSERT(pRootHub->pEnumDevice == NULL);
    pRootHub->pEnumDevice = USBH_CreateNewUsbDevice(pRootHub->pHostController); // Now create a new device
    if (pRootHub->pEnumDevice == NULL) { // On error abort the port enumeration
      USBH_WARN((USBH_MCAT_RHUB, "ROOT_HUB_PORT_RESET: USBH_CreateNewUsbDevice fails, no memory, no retry!"));
      _PortResetFail(pRootHub, USBH_STATUS_MEMORY, FALSE);
      break;
    }
    //
    // Prepare to get the device descriptor initially.
    // This complies to the Windows/Linux enumeration behavior (Reset -> GetDeviceDesc -> Reset -> SetAdress -> GetDeviceDesc).
    //
    pRootHub->pEnumDevice->DeviceSpeed = pEnumPort->PortSpeed;
    //
    // Backward pointer to the pHub port, the ports device pointer is set after complete enumeration of this device.
    // The state machine of the later device enumeration checks the port state and delete the device if the state is removed.
    //
    pRootHub->pEnumDevice->pParentPort = pEnumPort;
    pUrb = &pRootHub->EnumUrb;
    if (USBH_CheckCtrlTransferBuffer(pRootHub->pEnumDevice, USBH_DEFAULT_STATE_EP0_SIZE) != 0) {
      _PortResetFail(pRootHub, USBH_STATUS_MEMORY, FALSE);
      break;
    }
    USBH_EnumPrepareGetDescReq(pUrb, USB_DEVICE_DESCRIPTOR_TYPE, 0, 0, USBH_DEFAULT_STATE_EP0_SIZE, pRootHub->pEnumDevice->pCtrlTransferBuffer);
    USBH_ASSERT((int)pEnumPort->PortSpeed >= 1 && (int)pEnumPort->PortSpeed <= (int)pRootHub->pHostController->Caps.MaxSpeed);
    pRootHub->hEnumEP = pRootHub->pHostController->RootEndpoints[(int)pEnumPort->PortSpeed - 1];
    //
    // If endpoint is not available, ignore the device and return that the device is removed.
    //
    if (pRootHub->hEnumEP == NULL) {
      USBH_WARN((USBH_MCAT_RHUB, "HUB_PORTRESET_GET_DEV_DESC: Device not available"));
      _PortResetFail(pRootHub, USBH_STATUS_DEVICE_REMOVED, FALSE);
      break;
    }

    // Set a new  state
    pRootHub->PortResetEnumState = USBH_HUB_PORTRESET_GET_DEV_DESC;
    // Setup a timer if the device does not answer
    // Submit the request
    Status = USBH_URB_SubStateSubmitRequest(&pRootHub->SubState, &pRootHub->EnumUrb, USBH_DEFAULT_SETUP_TIMEOUT, pRootHub->pEnumDevice);
    if (Status != USBH_STATUS_PENDING) { // Error on submitting: set port to PORT_ERROR
      USBH_WARN((USBH_MCAT_RHUB, "HUB_PORTRESET_GET_DEV_DESC: USBH_URB_SubStateSubmitRequest failed %s",USBH_GetStatusStr(Status)));
      _PortResetFail(pRootHub, Status, FALSE);
    }
    break;
  case USBH_HUB_PORTRESET_GET_DEV_DESC:
    pUrb = &pRootHub->EnumUrb;
    if (pUrb->Header.Status != USBH_STATUS_SUCCESS || pUrb->Request.ControlRequest.Length < USB_DEVICE_DESCRIPTOR_EP0_FIFO_SIZE_OFS + 1u) {
      USBH_WARN((USBH_MCAT_RHUB, "USBH_HUB_PORTRESET_GET_DEV_DESC failed %s", USBH_GetStatusStr(pUrb->Header.Status)));
      _PortResetFail(pRootHub, pUrb->Header.Status, TRUE);
      break;
    }
    //
    // Extract the EP0 FIFO size
    //
    pRootHub->pEnumDevice->MaxFifoSize = pRootHub->pEnumDevice->pCtrlTransferBuffer[USB_DEVICE_DESCRIPTOR_EP0_FIFO_SIZE_OFS];
    pRootHub->PortResetEnumState = USBH_HUB_PORTRESET_WAIT_RESET_1;
    //
    // Second Port reset.
    //
    pDriver->pfResetPort(pRootHub->pHostController->pPrvData, pEnumPort->HubPortNumber);
    USBH_URB_SubStateWait(&pRootHub->SubState, USBH_WAIT_AFTER_RESET, NULL);
    break;
  case USBH_HUB_PORTRESET_WAIT_RESET_1:
    //
    // Ok, port is enabled
    //
    pEnumPort->PortSpeed = _SpeedTab[PORT_STATUS_SPEED_IDX(pEnumPort->PortStatus)];
    //
    // Init the device structure
    //
    pRootHub->pEnumDevice->DeviceSpeed         = pEnumPort->PortSpeed;
    pRootHub->pEnumDevice->UsbAddress          = USBH_GetUsbAddress(pRootHub->pHostController);
    if (pRootHub->pEnumDevice->UsbAddress == 0u) {
      //
      // Stop current enumeration.
      //
      USBH_WARN((USBH_MCAT_RHUB, "_ProcessPortReset: Enumeration stopped. No free USB address is available."));
      _PortResetFail(pRootHub, USBH_STATUS_RESOURCES, FALSE);
      break;
    }
    //
    // Backward pointer to the pHub port, the ports device pointer is set after complete enumeration of this device.
    // The state machine of the later device enumeration checks the port state and delete the device if the state is removed.
    //
    USBH_ASSERT_PTR(pEnumPort);
    pRootHub->pEnumDevice->pParentPort         = pEnumPort;
    //
    // Prepare the set address request
    //
    pUrb = &pRootHub->EnumUrb;
    USBH_ZERO_MEMORY(pUrb, sizeof(USBH_URB));
    pUrb->Header.Function                      = USBH_FUNCTION_CONTROL_REQUEST;
    // pUrb->Request.ControlRequest.Setup.Type is 0x00   STD, OUT, device
    pUrb->Request.ControlRequest.Setup.Request = USB_REQ_SET_ADDRESS;
    pUrb->Request.ControlRequest.Setup.Value   = pRootHub->pEnumDevice->UsbAddress;
    //
    // Select one of the preallocated endpoints
    //
    pRootHub->hEnumEP = pRootHub->pHostController->RootEndpoints[(int)pEnumPort->PortSpeed - 1];
    pRootHub->PortResetEnumState = USBH_HUB_PORTRESET_SET_ADDRESS;
    //
    // Submit the request
    //
    Status = USBH_URB_SubStateSubmitRequest(&pRootHub->SubState, &pRootHub->EnumUrb, USBH_DEFAULT_SETUP_TIMEOUT, pRootHub->pEnumDevice);
    if (Status != USBH_STATUS_PENDING) { // Error on submitting: set port to PORT_ERROR
      USBH_WARN((USBH_MCAT_RHUB, "HUB_PORTRESET_SET_ADDRESS: USBH_URB_SubStateSubmitRequest failed %s", USBH_GetStatusStr(Status)));
      _PortResetFail(pRootHub, Status, FALSE);
    }
    break;
  case USBH_HUB_PORTRESET_SET_ADDRESS:
    if (pRootHub->EnumUrb.Header.Status != USBH_STATUS_SUCCESS) {
      USBH_WARN((USBH_MCAT_RHUB, "HUB_PORTRESET_SET_ADDRESS failed st: %s",USBH_GetStatusStr(pRootHub->EnumUrb.Header.Status)));
      _PortResetFail(pRootHub, pRootHub->EnumUrb.Header.Status, TRUE);
      break;
    }
    //
    // Ok, now the device is addressed, wait some ms to let the device switch to the new address
    //
    pRootHub->PortResetEnumState = USBH_HUB_PORTRESET_START_DEVICE_ENUM;
    USBH_URB_SubStateWait(&pRootHub->SubState, WAIT_AFTER_SETADDRESS, NULL);
    break;
  case USBH_HUB_PORTRESET_START_DEVICE_ENUM:
    {
      USB_DEVICE * pEnumDevice;
      //
      // 1. The device that is connected to the port is added after successful enumeration (Port->Device = Device)
      // 2. start the device enumeration process
      // 3. release this port enumeration and wait for connecting other ports! at this point the port state is PORT_ENABLED!
      //
      pEnumDevice                   = pRootHub->pEnumDevice;
      // Prevent access to the enum device after starting the enumeration process
      pRootHub->pEnumDevice         = NULL;
      USBH_LOG((USBH_MCAT_RHUB, "_ProcessPortResetSetAddress: Successfull"));
      USBH_StartEnumeration(pEnumDevice);
      _PortResetSetIdleServicePorts(pRootHub);
    }
    break;
  default:
    USBH_ASSERT0;
    break;
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
*       USBH_ROOTHUB_Init
*
*  Function description
*/
void USBH_ROOTHUB_Init(USBH_HOST_CONTROLLER * pHostController) {
  ROOT_HUB    * pRootHub = &pHostController->RootHub;

  USBH_IFDBG(pRootHub->Magic = ROOT_HUB_MAGIC);
  pRootHub->pHostController = pHostController;
  USBH_URB_SubStateInit(&pRootHub->SubState, pHostController, &pRootHub->hEnumEP, _ProcessPortResetSetAddress, pRootHub);
}

/*********************************************************************
*
*       USBH_ROOTHUB_Release
*
*  Function description
*/
void USBH_ROOTHUB_Release(ROOT_HUB * pRootHub) {
  USBH_LOG((USBH_MCAT_RHUB, "Release RootHub"));
  USBH_URB_SubStateExit(&pRootHub->SubState);
  if (pRootHub->pPortList != NULL) {
    USBH_FREE(pRootHub->pPortList);
  }
}

/*********************************************************************
*
*       USBH_ROOTHUB_OnNotification
*
*  Function description
*    Called from the Host controller driver if an root hub event occurs
*    bit0 indicates a Status change of the HUB, bit 1 of port 1 of the Hub and so on.
*/
void USBH_ROOTHUB_OnNotification(void * pRootHubContext, U32 Notification) {
  ROOT_HUB               * pRootHub;
  USBH_HOST_CONTROLLER   * pHostController;

  pRootHub = USBH_CTX2PTR(ROOT_HUB, pRootHubContext);
  USBH_ASSERT_MAGIC(pRootHub, ROOT_HUB);
  pHostController = pRootHub->pHostController;
  USBH_LOG((USBH_MCAT_RHUB_PORT, "_OnNotification: 0x%x!", Notification));
  USBH_USE_PARA(Notification);
  USBH_HC_ServicePorts(pHostController);
}

/*********************************************************************
*
*       USBH_ROOTHUB_ServicePorts
*
*  Function description
*    Called after a notification or if the enumeration of a device has finished
*/
void USBH_ROOTHUB_ServicePorts(ROOT_HUB * pRootHub) {
  USBH_HUB_PORT * pHubPort;
  USBH_HUB_PORT * pPortToStart = NULL;
  unsigned        i;
  USBH_HOST_CONTROLLER   * pHostController;
  const USBH_HOST_DRIVER * pDriver;

  pHostController = pRootHub->pHostController;
  if (pHostController->State < HC_WORKING) {
    return;
  }
  pDriver = pHostController->pDriver;
  //
  // Run over all ports.
  //
  pHubPort = pHostController->RootHub.pPortList;
  for (i = 0; i < pHostController->RootHub.PortCount; i++) {
    USBH_ASSERT_MAGIC(pHubPort, USBH_HUB_PORT);
    pHubPort->PortStatus = pDriver->pfGetPortStatus(pHostController->pPrvData, pHubPort->HubPortNumber);
    USBH_LOG((USBH_MCAT_RHUB_PORT, "Port %d Status %X = %s", pHubPort->HubPortNumber, pHubPort->PortStatus, USBH_PortStatus2Str(pHubPort->PortStatus)));
    if (pHubPort == pRootHub->pEnumPort) {
      //
      // Skip port that is currently handled by the state machine.
      //
      pHubPort++;
      continue;
    }
    //
    // Over current ?
    //
    if ((pHubPort->PortStatus & PORT_STATUS_OVER_CURRENT) != 0u) {
      USBH_WARN((USBH_MCAT_RHUB, "PORT_STATUS_OVER_CURRENT Port:%d Status: 0x%X", pHubPort->HubPortNumber, pHubPort->PortStatus));
      _PortEvent(USBH_PORT_EVENT_OVER_CURRENT, pHostController, pHubPort);
    }
    if ((pHubPort->PortStatus & (PORT_STATUS_OVER_CURRENT | PORT_STATUS_POWER)) == (PORT_STATUS_OVER_CURRENT | PORT_STATUS_POWER)) {
      //
      // The device uses too much current, power down port
      //
      if (pHubPort->pDevice != NULL) {
        USBH_MarkParentAndChildDevicesAsRemoved(pHubPort->pDevice);
      }
      //
      // Power down the port to avoid fire :-)
      //
      if (USBH_Global.pfOnSetPortPower != NULL) {
        USBH_Global.pfOnSetPortPower(pHostController->Index, pHubPort->HubPortNumber, 0);
      }
      pDriver->pfSetPortPower(pHostController->pPrvData, pHubPort->HubPortNumber, 0);
      pHubPort->PortStatus = 0;
    }
    //
    // New connection ?
    //
    if ((pHubPort->PortStatus & PORT_STATUS_CONNECT) != 0u && (pHubPort->PortStatus & PORT_STATUS_ENABLED) == 0u) {
      // This device must be enumerated
      if (pHubPort->pDevice != NULL) {
        // Remove the old connected device first
        USBH_LOG((USBH_MCAT_RHUB, "delete dev., port connected but not enabled Port:%d Status: 0x%X", pHubPort->HubPortNumber, pHubPort->PortStatus));
        USBH_MarkParentAndChildDevicesAsRemoved(pHubPort->pDevice);
      }
      if (pHubPort->RetryCounter <= USBH_RESET_RETRY_COUNTER) {
        pPortToStart = pHubPort;
      }
    }
    //
    // Device removed ?
    //
    if ((pHubPort->PortStatus & PORT_STATUS_CONNECT) == 0u) {
      if (pHubPort->pDevice != NULL) { // This device is removed
        USBH_LOG((USBH_MCAT_RHUB, "ROOT_HUB_NOTIFY: port not connected, delete dev., Port:%d Status: 0x%X", pHubPort->HubPortNumber, pHubPort->PortStatus));
        USBH_MarkParentAndChildDevicesAsRemoved(pHubPort->pDevice);
      }
      if ((pHubPort->PortStatus & PORT_STATUS_ENABLED) != 0u) {
        pDriver->pfDisablePort(pHostController->pPrvData, pHubPort->HubPortNumber); // Disable the port
      }
      pHubPort->RetryCounter = 0;
    }
    pHubPort++;
  }
  if (pPortToStart != NULL &&
      pRootHub->PortResetEnumState == USBH_HUB_PORTRESET_IDLE &&
      pPortToStart->DeviceEnumActive == 0) {
    pHubPort = pPortToStart;
    if (pHubPort->RetryCounter < USBH_RESET_RETRY_COUNTER) {
      if (USBH_ClaimActivePortReset(pHostController) == 0u) {
        if (pHubPort->RetryCounter != 0u) {
          pRootHub->PortResetEnumState = USBH_HUB_PORTRESET_RESTART;
        } else {
          pRootHub->PortResetEnumState = USBH_HUB_PORTRESET_START;
        }
        pHubPort->RetryCounter++;
        USBH_HC_INC_REF(pHostController);
        pRootHub->pEnumPort = pHubPort;
        //
        // Call _ProcessPortResetSetAddress() using timer to avoid recursion.
        //
        USBH_LOG((USBH_MCAT_RHUB, "New device on port %u, start state machine...", pHubPort->HubPortNumber));
        USBH_URB_SubStateWait(&pRootHub->SubState, 1, NULL);  // Start the port reset
      }
    } else {
      if (pHubPort->RetryCounter == USBH_RESET_RETRY_COUNTER) {
        pHubPort->RetryCounter++;
        USBH_WARN((USBH_MCAT_RHUB, "USBH_ROOTHUB_ServicePorts: Max. port retries on port %u -> PORT_ERROR!", pHubPort->HubPortNumber));
        USBH_SetEnumErrorNotification(USBH_ENUM_ERROR_ROOT_PORT_RESET | USBH_ENUM_ERROR_STOP_ENUM_FLAG,
                                      USBH_STATUS_ERROR, 0, pHubPort->HubPortNumber);
      }
    }
  }
}

/*********************************************************************
*
*       USBH_ROOTHUB_InitPorts
*
*  Function description
*    Create all needed root Hub ports and power them up.
*/
void USBH_ROOTHUB_InitPorts(ROOT_HUB * pRootHub) {
  USBH_HUB_PORT           * pHubPort;
  USBH_HOST_CONTROLLER    * pHostController = pRootHub->pHostController;
  const USBH_HOST_DRIVER  * pDriver         = pHostController->pDriver;
  unsigned                  PortCount;
  unsigned                  i;

  USBH_ASSERT_MAGIC(pRootHub, ROOT_HUB);
  PortCount = pDriver->pfGetPortCount(pHostController->pPrvData);
  pHubPort  = (USBH_HUB_PORT *)USBH_MALLOC_ZEROED(PortCount * sizeof(USBH_HUB_PORT));
  pHostController->RootHub.pPortList = pHubPort;
  for (i = 1; i <= PortCount; i++) {
    USBH_IFDBG(pHubPort->Magic = USBH_HUB_PORT_MAGIC);
    //
    // Call the user callback if available.
    //
    if (USBH_Global.pfOnSetPortPower != NULL) {
      USBH_Global.pfOnSetPortPower(pHostController->Index, i, 1);
    }
    pDriver->pfSetPortPower(pHostController->pPrvData, i, 1); // Turn the power on
    // Init the pHub port
    pHubPort->HubPortNumber = i;
    pHubPort->pRootHub       = pRootHub;
    pHubPort++;
  }
  pRootHub->PortCount          = PortCount;
  pRootHub->PortResetEnumState = USBH_HUB_PORTRESET_IDLE;
}

/*************************** End of file ****************************/
