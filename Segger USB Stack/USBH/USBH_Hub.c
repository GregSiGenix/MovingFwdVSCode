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
#include "USBH.h"
#include "USBH_Util.h"

/*********************************************************************
*
*       Defines
*
**********************************************************************
*/

//
// Number of retries for URBs (control + INT requests) to a HUB
//
#define USBH_HUB_URB_CTL_RETRY_COUNTER          5u
#define USBH_HUB_URB_INT_RETRY_COUNTER          5u

//
// Reset INT request error counter after this number of NAKs were received.
//
#define USBH_HUB_INT_ERR_CNT_RESTORE_THRESHOLD  32

//
// Delay before retry a failed URB to a HUB (ms)
//
#define USBH_HUB_URB_RETRY_DELAY                3u

//
// Poll delay to start a new port reset, when a device enumeration is running on the port (ms)
//
#define USBH_HUB_ENUM_POLL_DELAY               50u

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _HubPrepareGetPortStatus
*/
static void _HubPrepareGetPortStatus(USBH_URB * pUrb, USB_DEVICE * pDevice, U16 Selector, void * pBuffer) {
  USBH_LOG((USBH_MCAT_HUB_URB, "_HubPrepareGetPortStatus: Selector: %u", Selector));
  USBH_ZERO_MEMORY(pUrb, sizeof(USBH_URB));
  pUrb->Header.Function = USBH_FUNCTION_CONTROL_REQUEST;
  pUrb->Header.pDevice  = pDevice;
  if (Selector != 0u) {
    pUrb->Request.ControlRequest.Setup.Type = USB_TO_HOST | USB_REQTYPE_CLASS | USB_OTHER_RECIPIENT;  // Device
  } else {
    pUrb->Request.ControlRequest.Setup.Type = USB_TO_HOST | USB_REQTYPE_CLASS | USB_DEVICE_RECIPIENT; // Port
  }
  pUrb->Request.ControlRequest.Setup.Request = HDC_REQTYPE_GET_STATUS;
  pUrb->Request.ControlRequest.Setup.Value   = 0;
  pUrb->Request.ControlRequest.Setup.Index   = Selector;
  pUrb->Request.ControlRequest.Setup.Length  = HCD_GET_STATUS_LENGTH;
  pUrb->Request.ControlRequest.pBuffer       = pBuffer;
}

/*********************************************************************
*
*       _HubPrepareStandardOutRequest
*/
static void _HubPrepareStandardOutRequest(USBH_URB * pUrb, USB_DEVICE * pDevice, U8 Request, U16 Value, U16 Index) {
  USBH_LOG((USBH_MCAT_HUB_URB, "_HubPrepareStandardOutRequest: request: %u", Request));
  USBH_ZERO_MEMORY(pUrb,sizeof(USBH_URB));
  pUrb->Header.pDevice                       = pDevice;
  pUrb->Header.Function                      = USBH_FUNCTION_CONTROL_REQUEST;
  // pUrb->Request.ControlRequest.Setup.Type is 0x00    STD, OUT, device
  pUrb->Request.ControlRequest.Setup.Request = Request;
  pUrb->Request.ControlRequest.Setup.Value   = Value;
  pUrb->Request.ControlRequest.Setup.Index   = Index;
}

/*********************************************************************
*
*       _HubPrepareGetHubDesc
*/
static void _HubPrepareGetHubDesc(USBH_URB * pUrb, USB_DEVICE * pDevice, void * pBuffer, U16 NumBytesReq) {
  U16 Length;

  USBH_LOG((USBH_MCAT_HUB_URB, "HubPrepareGetDescClassReq: length: %u", NumBytesReq));
  Length = USBH_MIN(NumBytesReq, HDC_MAX_HUB_DESCRIPTOR_LENGTH);
  USBH_ZERO_MEMORY(pUrb, sizeof(USBH_URB));
  pUrb->Header.Function                      = USBH_FUNCTION_CONTROL_REQUEST;
  pUrb->Header.pDevice                       = pDevice;
  pUrb->Request.ControlRequest.Setup.Type    = USB_TO_HOST | USB_REQTYPE_CLASS; // class Request, IN, device
  pUrb->Request.ControlRequest.Setup.Request = USB_REQ_GET_DESCRIPTOR;
  pUrb->Request.ControlRequest.Setup.Value   = (U16)USB_HUB_DESCRIPTOR_TYPE << 8;
  pUrb->Request.ControlRequest.Setup.Length  = Length;
  pUrb->Request.ControlRequest.pBuffer       = pBuffer;
}

/*********************************************************************
*
*       _HubPrepareHubRequest
*/
static void _HubPrepareHubRequest(USBH_URB * pUrb, USB_DEVICE * pDevice, unsigned Feature, unsigned Selector) {
  USBH_ZERO_MEMORY(pUrb, sizeof(USBH_URB));
  pUrb->Header.Function = USBH_FUNCTION_CONTROL_REQUEST;
  pUrb->Header.pDevice  = pDevice;
  if (Selector != 0u) {
    pUrb->Request.ControlRequest.Setup.Type = USB_REQTYPE_CLASS | USB_OTHER_RECIPIENT;  // class Request, IN, device
  } else {
    pUrb->Request.ControlRequest.Setup.Type = USB_REQTYPE_CLASS | USB_DEVICE_RECIPIENT; // class Request, IN, device
  }
  pUrb->Request.ControlRequest.Setup.Value   = Feature;
  pUrb->Request.ControlRequest.Setup.Index   = Selector;
  // pUrb->Request.ControlRequest.Setup.Length is already 0
}

/*********************************************************************
*
*       _HubPrepareSetFeatureReq
*/
static void _HubPrepareSetFeatureReq(USBH_URB * pUrb, USB_DEVICE * pDevice, unsigned Feature, unsigned Selector) {
  USBH_LOG((USBH_MCAT_HUB_URB, "_HubPrepareSetFeatureReq: Feature: %u Selector: %u", Feature, Selector));
  _HubPrepareHubRequest(pUrb, pDevice, Feature, Selector);
  pUrb->Request.ControlRequest.Setup.Request = USB_REQ_SET_FEATURE;
}

/*********************************************************************
*
*       _HubPrepareClrFeatureReq
*/
static void _HubPrepareClrFeatureReq(USBH_URB * pUrb, USB_DEVICE * pDevice, unsigned Feature, unsigned Selector) {
  USBH_LOG((USBH_MCAT_HUB_URB, "_HUB_PrepareClrFeatureReq: Feature: %u Selector: %u", Feature, Selector));
  _HubPrepareHubRequest(pUrb, pDevice, Feature, Selector);
  pUrb->Request.ControlRequest.Setup.Request = USB_REQ_CLEAR_FEATURE;
}

/*********************************************************************
*
*       _HubPrepareSetAlternate
*/
static int _HubPrepareSetAlternate(USBH_HUB * pHub) {
  USB_DEVICE *pDev;
  const U8 * pDesc;
  int        RemLen;
  unsigned   DescLen;

  pDev   = pHub->pHubDevice;
  pDesc  = pDev->pConfigDescriptor;
  RemLen = (int)pDev->ConfigDescriptorSize;
  //
  // Search all interface descriptors for a multi-TT HUB interface
  //
  while (RemLen > 0) {
    DescLen = *pDesc;
    if (DescLen >= USB_INTERFACE_DESCRIPTOR_LENGTH && pDesc[1] == USB_INTERFACE_DESCRIPTOR_TYPE) {
      if (pDesc[USB_INTERFACE_DESC_CLASS_OFS] == USB_DEVICE_CLASS_HUB &&
          pDesc[USB_INTERFACE_DESC_PROTOCOL_OFS] == USBH_HUB_PROTOCOL_MULTI_TT) {
        unsigned IntfNo;
        unsigned AltNo;
        USBH_URB * pUrb;

        IntfNo = pDesc[USB_INTERFACE_DESC_NUMBER_OFS];
        AltNo  = pDesc[USB_INTERFACE_DESC_ALTSETTING_OFS];
        USBH_LOG((USBH_MCAT_HUB, "Found HUB multi TT alterate setting %u %u", IntfNo, AltNo));
        pHub->MultiTT = 1;
        if (AltNo == 0u) {
          break;
        }
        pHub->InterfaceNo       = IntfNo;
        pHub->MultiTTAltSetting = AltNo;
        pUrb = &pDev->EnumUrb;
        _HubPrepareStandardOutRequest(pUrb, pDev, USB_REQ_SET_INTERFACE, AltNo, IntfNo);
        pUrb->Request.ControlRequest.Setup.Type = USB_TO_DEVICE | USB_REQTYPE_STANDARD | USB_INTERFACE_RECIPIENT;
        return 1;
      }
    }
    pDesc  += DescLen;
    RemLen -= (int)DescLen;
  }
  return 0;
}

/*********************************************************************
*
*       _ParseHubDescriptor
*/
static int _ParseHubDescriptor(USBH_HUB * pHub, const U8 * pBuffer, U32 Length) {
  USBH_ASSERT_MAGIC(pHub, USBH_HUB);
  USBH_ASSERT_PTR(pBuffer);

  if (Length < HDC_DESC_MIN_LENGTH) {
    USBH_WARN((USBH_MCAT_HUB, "_ParseHubDescriptor: Bad length: %u", Length));
    return 1;
  }
  pHub->PortCount       = pBuffer[HDC_DESC_PORT_NUMBER_OFS];
  pHub->Characteristics = USBH_LoadU16LE(pBuffer + HDC_DESC_CHARACTERISTICS_LOW_OFS);
  pHub->PowerGoodTime   = pBuffer[HDC_DESC_POWER_GOOD_TIME_OFS];
  pHub->PowerGoodTime   = pHub->PowerGoodTime << 1;
  USBH_LOG((USBH_MCAT_HUB_URB, "_ParseHubDescriptor: Ports: %d, Character.: 0x%x, powergoodtime: %d", pHub->PortCount, pHub->Characteristics, pHub->PowerGoodTime));
  return 0;
}

/*********************************************************************
*
*       _PortEvent
*
*  Function description
*    Signal a port event to application.
*/
static void _PortEvent(USBH_PORT_EVENT_TYPE EventType, const USBH_HUB * pHub, const USBH_HUB_PORT * pHubPort) {
  USBH_PORT_EVENT Event;

  if (USBH_Global.pfOnPortEvent != NULL) {
    Event.Event          = EventType;
    Event.HCIndex        = pHub->pHubDevice->pHostController->Index;
    Event.PortNumber     = pHubPort->HubPortNumber;
    Event.HubInterfaceId = pHub->InterfaceId;
    USBH_Global.pfOnPortEvent(&Event);
  }
}

/*********************************************************************
*
*       _HubFatalError
*
*  Function description
*  Called on fatal errors in the HUB state machine.
*  The pHub device and all connected child devices are deleted.
*/
static void _HubFatalError(USBH_HUB * pHub, USBH_STATUS Status, USBH_BOOL bRetry) {
  USBH_HUB_PORT * pParentPort;
  USB_DEVICE    * pDev;
  unsigned        Flags;

  USBH_ASSERT_MAGIC(pHub, USBH_HUB);
  USBH_WARN((USBH_MCAT_HUB, "HUB fatal error %x: Remove HUB", Status));
  pDev = pHub->pHubDevice;
  USBH_ASSERT_MAGIC(pDev, USB_DEVICE);
  pParentPort = pDev->pParentPort;
  USBH_ASSERT_MAGIC(pParentPort, USBH_HUB_PORT);
  if ((pParentPort->PortStatus & PORT_STATUS_ENABLED) != 0u) {
    //
    // Disable the parent port
    //
    if (NULL != pParentPort->pRootHub) {
      const USBH_HOST_DRIVER * pDriver;

      pDriver = pDev->pHostController->pDriver;
      pDriver->pfDisablePort(pDev->pHostController->pPrvData, pParentPort->HubPortNumber);
    } else {
      pParentPort->ToDo |= USBH_PORT_DO_DISABLE;
      USBH_StartTimer(&pHub->ProcessPorts, 0);
    }
  }
  if (bRetry != FALSE) {
    Flags = USBH_ENUM_ERROR_HUB_PORT_RESET | USBH_ENUM_ERROR_EXTHUBPORT_FLAG | USBH_ENUM_ERROR_RETRY_FLAG;
  } else {
    pParentPort->RetryCounter = USBH_RESET_RETRY_COUNTER + 1u;
    Flags = USBH_ENUM_ERROR_HUB_PORT_RESET | USBH_ENUM_ERROR_EXTHUBPORT_FLAG | USBH_ENUM_ERROR_STOP_ENUM_FLAG;
  }
  //
  // Notify user from port enumeration error
  //
  USBH_SetEnumErrorNotification(Flags, Status, (int)pHub->PortResetEnumState, pParentPort->HubPortNumber);
  USBH_MarkParentAndChildDevicesAsRemoved(pDev);
  USBH_HC_ServicePorts(pDev->pHostController);
}

/*********************************************************************
*
*       _EnumPrepareGetDeviceStatus
*/
#if 0
static void _EnumPrepareGetDeviceStatus(USB_DEVICE * pDev, void * pBuffer, U16 NumBytesReq) {
  USBH_URB * pUrb;
  U16        Length;
  Length                                     = USBH_MIN(NumBytesReq, USB_STATUS_LENGTH);
  pUrb                                       = &pDev->EnumUrb;
  USBH_ZERO_MEMORY(pUrb, sizeof(USBH_URB));
  pUrb->Header.Function                      = USBH_FUNCTION_CONTROL_REQUEST;
  pUrb->Request.ControlRequest.Setup.Type    = USB_STATUS_DEVICE; // STD, IN, device
  pUrb->Request.ControlRequest.Setup.Request = USB_REQ_GET_STATUS;
  pUrb->Request.ControlRequest.Setup.Length  = Length;
  pUrb->Request.ControlRequest.pBuffer       = pBuffer;
  pUrb->Request.ControlRequest.Length        = Length;
}
#endif

/*********************************************************************
*
*       _HubStatusRequestCompletion
*
*  Function description
*    Called on completion of the interrupt URB which is used to
*    poll status changes from the HUB.
*/

static void _HubStatusRequestCompletion(USBH_URB * pUrb) USBH_CALLBACK_USE {
  USBH_HUB      * pHub;
  U32             Length;
  U32             Notification;
  USBH_HUB_PORT * pPort;
  unsigned        i;

  pHub = USBH_CTX2PTR(USBH_HUB, pUrb->Header.pInternalContext);
  USBH_ASSERT_MAGIC(pHub, USBH_HUB);
  USBH_LOG((USBH_MCAT_HUB_URB, "_HubStatusRequestCompletion Ref.ct: %d", pHub->pHubDevice->RefCount));
  USBH_DEC_REF(pHub->pHubDevice);                                                    // Clear the local reference
  pHub->pInterruptEp->ActiveUrb = 0;
  pHub->InterruptUrbStatus = pUrb->Header.Status;
  if (USBH_STATUS_SUCCESS != pUrb->Header.Status) {                            // Check Status
    USBH_WARN((USBH_MCAT_HUB_URB, "_HubStatusRequestCompletion: st:%s", USBH_GetStatusStr(pUrb->Header.Status)));
  } else {
    Length       = pHub->InterruptUrb.Request.BulkIntRequest.Length;
    Notification = USBH_LoadU32LE(pHub->InterruptTransferBuffer);
    if (Length < 4u) {
      Notification &= ((1uL << (Length * 8u)) - 1u);
    }
    USBH_LOG((USBH_MCAT_HUB_URB, "_HubStatusRequestCompletion Notification %x", Notification));
    if ((Notification & 1u) != 0u) {
      USBH_WARN((USBH_MCAT_HUB, "HUB State notification"));
      // Handle Hub status change ??
    }
    pPort = pHub->pPortList;
    for (i = 1; i <= pHub->PortCount; i++) {
      USBH_ASSERT_MAGIC(pPort, USBH_HUB_PORT);
      Notification >>= 1;
      if ((Notification & 1u) != 0u) {
        pPort->ToDo |= USBH_PORT_DO_UPDATE_STATUS;
      }
      pPort++;
    }
  }
  USBH_StartTimer(&pHub->ProcessPorts, 0);
}

/*********************************************************************
*
*       _HubInstallPeriodicStatusTransfer
*/
static USBH_STATUS _HubInstallPeriodicStatusTransfer(USBH_HUB * pHub) {
  USB_DEVICE          * pDev;
  USB_INTERFACE       * pInterface;
  USBH_INTERFACE_MASK   iMask;
  USBH_STATUS           Status;
  USBH_EP_MASK          EpMask;
  USB_ENDPOINT        * pEndpoint;
  unsigned              MaxPacketSize;

  USBH_LOG((USBH_MCAT_HUB_URB, "_HubInstallPeriodicStatusTransfer !"));
  USBH_ASSERT_MAGIC(pHub, USBH_HUB);
  pDev = pHub->pHubDevice;
  USBH_ASSERT_MAGIC(pDev, USB_DEVICE);
  //
  // Get the first interface
  //
  iMask.Mask      = USBH_INFO_MASK_INTERFACE | USBH_INFO_MASK_CLASS;
  iMask.Interface = USBHUB_DEFAULT_INTERFACE;
  iMask.Class     = USB_DEVICE_CLASS_HUB;
  Status = USBH_SearchUsbInterface(pDev, &iMask, &pInterface);
  if (Status != USBH_STATUS_SUCCESS) {
    USBH_WARN((USBH_MCAT_HUB, "_HubInstallPeriodicStatusTransfer: Interface not found"));
    return Status;
  }
  //
  // Get the interrupt in endpoint
  //
  EpMask.Mask      = USBH_EP_MASK_TYPE | USBH_EP_MASK_DIRECTION;
  EpMask.Direction = USB_IN_DIRECTION;
  EpMask.Type      = USB_EP_TYPE_INT;
  pEndpoint        = USBH_BD_SearchUsbEndpointInInterface(pInterface, &EpMask);
  if(NULL == pEndpoint) {
    USBH_WARN((USBH_MCAT_HUB, "_HubInstallPeriodicStatusTransfer: Endpoint not found"));
    return USBH_STATUS_INVALID_PARAM;
  }
  pHub->pInterruptEp = pEndpoint;
  pHub->InterfaceId  = pInterface->InterfaceId;
  //
  // Check transfer size
  //
  MaxPacketSize = USBH_LoadU16LE(pEndpoint->pEndpointDescriptor + USB_EP_DESC_PACKET_SIZE_OFS);
  if (MaxPacketSize > sizeof(pHub->InterruptTransferBuffer)) {
    USBH_WARN((USBH_MCAT_HUB, "_HubInstallPeriodicStatusTransfer: HUB INT transfer size (%u) not supported", pHub->InterruptTransferBufferSize));
    return USBH_STATUS_XFER_SIZE;
  }
  pHub->InterruptTransferBufferSize = MaxPacketSize;
  pHub->InterruptUrbStatus = USBH_STATUS_SUCCESS;
  return USBH_STATUS_SUCCESS;
}

/*********************************************************************
*
*       _HubStartPeriodicStatusTransfer
*/
static USBH_STATUS _HubStartPeriodicStatusTransfer(USBH_HUB * pHub) {
  USBH_STATUS   Status;
  USBH_URB    * pUrb;

  pUrb = &pHub->InterruptUrb;
  USBH_ZERO_MEMORY(pUrb, sizeof(USBH_URB));
  pUrb->Header.pfOnInternalCompletion  = _HubStatusRequestCompletion;
  pUrb->Header.pInternalContext        = pHub;
  pUrb->Header.Function                = USBH_FUNCTION_INT_REQUEST;
  pUrb->Request.BulkIntRequest.pBuffer = pHub->InterruptTransferBuffer;
  pUrb->Request.BulkIntRequest.Length  = pHub->InterruptTransferBufferSize;
  //
  // Set the interrupt URB status to pending _before_ submitting the URB.
  // The status is overwritten by the correct URB status inside the completion callback.
  //
  pHub->InterruptUrbStatus = USBH_STATUS_PENDING;
  USBH_ASSERT_MAGIC(pHub->pInterruptEp, USB_ENDPOINT);
  Status = USBH_EpSubmitUrb(pHub->pInterruptEp, pUrb);
  if (Status != USBH_STATUS_PENDING) {
    USBH_WARN((USBH_MCAT_HUB_URB, "_HubStartPeriodicStatusTransfer: USBH_EpSubmitUrb: st:%s", USBH_GetStatusStr(Status)));
    //
    // If the submit routine fails the callback is not called and we have to overwrite the status.
    //
    pHub->InterruptUrbStatus = Status;
  }
  return Status;
}

/*********************************************************************
*
*       _PortResetSetIdle
*
*  Function description
*    Set state machine to idle.
*/
static void _PortResetSetIdle(USBH_HUB * pHub) {
  pHub->PortResetEnumState = USBH_HUB_PORTRESET_IDLE;
  pHub->pEnumDevice        = NULL;
  pHub->pEnumPort          = NULL;
  // Allow starting an port reset on another port
  USBH_ReleaseActivePortReset(pHub->pHubDevice->pHostController);
  USBH_DEC_REF(pHub->pHubDevice);                                                    // Clear the local reference
}

/*********************************************************************
*
*       _PortResetFail
*
*  Function description
*    Called when the state machine encounters an error.
*    The state machine is restarted (if bRetry == TRUE) or stopped for this
*    port until a de-connect occurs.
*/
static void _PortResetFail(USBH_HUB * pHub, USBH_STATUS Status, USBH_BOOL bRetry) {
  USBH_HUB_PORT   * pEnumPort;
  unsigned          Flags;

  pEnumPort = pHub->pEnumPort;
  USBH_ASSERT_MAGIC(pEnumPort, USBH_HUB_PORT);
  USBH_LOG((USBH_MCAT_HUB, "_PortResetFail: Port %u, %x %s", pEnumPort->HubPortNumber,
                           Status, USBH_HubPortResetState2Str(pHub->PortResetEnumState)));
  pEnumPort->ToDo = USBH_PORT_DO_DISABLE | USBH_PORT_DO_UPDATE_STATUS;
  if (bRetry != FALSE) {
    Flags = USBH_ENUM_ERROR_HUB_PORT_RESET | USBH_ENUM_ERROR_EXTHUBPORT_FLAG | USBH_ENUM_ERROR_RETRY_FLAG;
  } else {
    pEnumPort->RetryCounter = USBH_RESET_RETRY_COUNTER + 1u;
    Flags = USBH_ENUM_ERROR_HUB_PORT_RESET | USBH_ENUM_ERROR_EXTHUBPORT_FLAG | USBH_ENUM_ERROR_STOP_ENUM_FLAG;
  }
  //
  // Notify user from port enumeration error
  //
  if ((pEnumPort->PortStatus & PORT_STATUS_CONNECT) == 0u) {
    Flags |= USBH_ENUM_ERROR_DISCONNECT_FLAG;
  }
  USBH_SetEnumErrorNotification(Flags, Status, (int)pHub->PortResetEnumState, pEnumPort->HubPortNumber);
  if (NULL != pHub->pEnumDevice) {
    USBH_DEC_REF(pHub->pEnumDevice);    // Delete the device, this is the initial reference on default
    pHub->pEnumDevice = NULL;
  }
  _PortResetSetIdle(pHub);
}

/*********************************************************************
*
*       _ProcessPortReset
*
*  Function description
*    Sub state machine for handling reset and 'set address' for a HUB port.
*/
static void _ProcessPortReset(USBH_HUB * pHub, USBH_HUB_PORT * pPort) {
  USB_DEVICE             * pEnumDevice;
  USBH_STATUS              Status;
  USBH_HOST_CONTROLLER   * pHostController;
  USBH_URB               * pUrb;
  //
  // Check, if port is still connected
  //
  if ((pPort->PortStatus & PORT_STATUS_CONNECT) == 0u) {
    //
    // Port was disconnected.
    //
    USBH_WARN((USBH_MCAT_HUB_SM, "_ProcessPortReset: Port disconnected after port reset"));
    _PortResetFail(pHub, USBH_STATUS_PORT, TRUE);
    return;
  }
  USBH_LOG((USBH_MCAT_HUB_SM, "_ProcessPortReset: Port %u: %s", pPort->HubPortNumber,
                              USBH_HubPortResetState2Str(pHub->PortResetEnumState)));
  switch(pHub->PortResetEnumState) {
  case USBH_HUB_PORTRESET_START:
    pPort->ToDo |= USBH_PORT_DO_DELAY;
    pPort->DelayUntil = USBH_TIME_CALC_EXPIRATION(USBH_Global.Config.DefaultPowerGoodTime);
    pHub->PortResetEnumState = USBH_HUB_PORTRESET_WAIT_RESTART;
    break;
  case USBH_HUB_PORTRESET_RESTART:
    pPort->ToDo |= USBH_PORT_DO_DELAY;
    pPort->DelayUntil = USBH_TIME_CALC_EXPIRATION(USBH_Global.Config.DefaultPowerGoodTime + USBH_DELAY_FOR_REENUM);
    pHub->PortResetEnumState = USBH_HUB_PORTRESET_WAIT_RESTART;
    break;
  case USBH_HUB_PORTRESET_WAIT_RESTART:
    pPort->ToDo |= USBH_PORT_DO_RESET;
    pHub->PortResetEnumState = USBH_HUB_PORTRESET_WAIT_RESET_0;
    break;
  case USBH_HUB_PORTRESET_WAIT_RESET_0:
    pPort->ToDo |= USBH_PORT_DO_DELAY | USBH_PORT_DO_UPDATE_STATUS;
    pPort->DelayUntil = USBH_TIME_CALC_EXPIRATION(USBH_HUB_WAIT_AFTER_RESET);
    pHub->PortResetEnumState = USBH_HUB_PORTRESET_IS_ENABLED_0;
    break;
  case USBH_HUB_PORTRESET_IS_ENABLED_0:
    if ((pPort->PortStatus & PORT_STATUS_ENABLED) == 0u) {
      USBH_WARN((USBH_MCAT_HUB, "_ProcessPortReset: Port not enabled after port reset"));
      _PortResetFail(pHub, USBH_STATUS_PORT, TRUE);
      break;
    }
    pPort->PortSpeed = USBH_FULL_SPEED;
    if ((pPort->PortStatus & PORT_STATUS_LOW_SPEED) != 0u) {
      pPort->PortSpeed = USBH_LOW_SPEED;
    }
    if ((pPort->PortStatus & PORT_STATUS_HIGH_SPEED) != 0u) {
      pPort->PortSpeed = USBH_HIGH_SPEED;
    }
    pHostController = pHub->pHubDevice->pHostController;
    pEnumDevice = USBH_CreateNewUsbDevice(pHostController);
    if (pEnumDevice == NULL) {
      USBH_WARN((USBH_MCAT_HUB, "_ProcessPortReset: USBH_CreateNewUsbDevice fails"));
      _PortResetFail(pHub, USBH_STATUS_MEMORY, FALSE);
      break;
    }
    pHub->pEnumDevice         = pEnumDevice;
    pEnumDevice->DeviceSpeed  = pPort->PortSpeed;
    pEnumDevice->pParentPort  = pPort;
    if (USBH_CheckCtrlTransferBuffer(pEnumDevice, USBH_DEFAULT_STATE_EP0_SIZE) != 0) {
      USBH_WARN((USBH_MCAT_HUB, "_ProcessPortReset: No memory"));
      _PortResetFail(pHub, USBH_STATUS_MEMORY, FALSE);
      break;
    }
    if (USBH_INC_REF(pHub->pHubDevice) != USBH_STATUS_SUCCESS) {
      _PortResetFail(pHub, USBH_STATUS_DEVICE_REMOVED, FALSE);
      break;
    }
    pEnumDevice->pHubDevice   = pHub->pHubDevice;
    USBH_ASSERT((int)pPort->PortSpeed >= 1 && (int)pPort->PortSpeed <= (int)pHostController->Caps.MaxSpeed);
    pHub->PortResetEp0Handle  = pHostController->RootEndpoints[(int)pPort->PortSpeed - 1];
    if (pHub->PortResetEp0Handle == NULL) {
      USBH_WARN((USBH_MCAT_HUB, "_ProcessPortReset: No EP0 handle for enumeration available!"));
      _PortResetFail(pHub, USBH_STATUS_PORT, FALSE);
      break;
    }
    pUrb = &pHub->PortsUrb;
    USBH_EnumPrepareGetDescReq(pUrb, USB_DEVICE_DESCRIPTOR_TYPE, 0, 0, USBH_DEFAULT_STATE_EP0_SIZE, pEnumDevice->pCtrlTransferBuffer);
    pHub->PendingAction      = USBH_HUB_ACT_GET_DESC;
    pHub->pPendingActionPort = pPort;
    pUrb->Header.pDevice     = pEnumDevice;
    Status = USBH_URB_SubStateSubmitRequest(&pHub->PortResetControlUrbSubState, pUrb, USBH_DEFAULT_SETUP_TIMEOUT, pHub->pHubDevice);
    if (Status != USBH_STATUS_PENDING) {
      _PortResetFail(pHub, Status, FALSE);
      break;
    }
    pHub->PortResetEnumState = USBH_HUB_PORTRESET_GET_DEV_DESC;
    break;
  case USBH_HUB_PORTRESET_GET_DEV_DESC:
    pPort->ToDo |= USBH_PORT_DO_RESET;
    pHub->PortResetEnumState = USBH_HUB_PORTRESET_WAIT_RESET_1;
    break;
  case USBH_HUB_PORTRESET_WAIT_RESET_1:
    pPort->ToDo |= USBH_PORT_DO_DELAY | USBH_PORT_DO_UPDATE_STATUS;
    pPort->DelayUntil = USBH_TIME_CALC_EXPIRATION(USBH_HUB_WAIT_AFTER_RESET);
    pHub->PortResetEnumState = USBH_HUB_PORTRESET_IS_ENABLED_1;
    break;
  case USBH_HUB_PORTRESET_IS_ENABLED_1:
    if ((pPort->PortStatus & PORT_STATUS_ENABLED) == 0u) {
      USBH_WARN((USBH_MCAT_HUB, "_ProcessPortReset: Port disabled after port reset"));
      _PortResetFail(pHub, USBH_STATUS_PORT, TRUE);
      break;
    }
    pPort->PortSpeed = USBH_FULL_SPEED;
    if ((pPort->PortStatus & PORT_STATUS_LOW_SPEED) != 0u) {
      pPort->PortSpeed = USBH_LOW_SPEED;
    }
    if ((pPort->PortStatus & PORT_STATUS_HIGH_SPEED) != 0u) {
      pPort->PortSpeed = USBH_HIGH_SPEED;
    }
    pHostController = pHub->pHubDevice->pHostController;
    pEnumDevice               = pHub->pEnumDevice;
    pEnumDevice->DeviceSpeed  = pPort->PortSpeed;
    pEnumDevice->UsbAddress   = USBH_GetUsbAddress(pHostController);
    if (pEnumDevice->UsbAddress == 0u) {
      //
      // Stop current enumeration.
      //
      USBH_WARN((USBH_MCAT_HUB, "_ProcessPortReset: Enumeration stopped. No free USB address is available."));
      _PortResetFail(pHub, USBH_STATUS_RESOURCES, FALSE);
      break;
    }
    pHub->PortResetEp0Handle  = pHostController->RootEndpoints[(int)pPort->PortSpeed - 1];
    //
    // Prepare the set address Request
    //
    pUrb = &pHub->PortsUrb;
    _HubPrepareStandardOutRequest(pUrb, pEnumDevice, USB_REQ_SET_ADDRESS, pEnumDevice->UsbAddress, 0);
    pHub->PendingAction      = USBH_HUB_ACT_SET_ADDRESS;
    pHub->pPendingActionPort = pPort;
    Status = USBH_URB_SubStateSubmitRequest(&pHub->PortResetControlUrbSubState, pUrb, USBH_DEFAULT_SETUP_TIMEOUT, pHub->pHubDevice);
    if (Status != USBH_STATUS_PENDING) {
      _PortResetFail(pHub, Status, FALSE);
      break;
    }
    pHub->PortResetEnumState = USBH_HUB_PORTRESET_SET_ADDRESS;
    break;
  case USBH_HUB_PORTRESET_SET_ADDRESS:
    pPort->ToDo |= USBH_PORT_DO_DELAY;
    pPort->DelayUntil = USBH_TIME_CALC_EXPIRATION(WAIT_AFTER_SETADDRESS);
    pHub->PortResetEnumState = USBH_HUB_PORTRESET_START_DEVICE_ENUM;
    break;
  case USBH_HUB_PORTRESET_START_DEVICE_ENUM:
    pEnumDevice = pHub->pEnumDevice;
    pHub->pEnumDevice = NULL;
    USBH_LOG((USBH_MCAT_HUB, "_ProcessPortReset: Successful on port %u, start enumeration...", pPort->HubPortNumber));
    USBH_StartEnumeration(pEnumDevice);
    _PortResetSetIdle(pHub);
    break;
  default:
    USBH_WARN((USBH_MCAT_HUB_SM, "_ProcessPortReset: Bad State %u", pHub->PortResetEnumState));
    _PortResetFail(pHub, USBH_STATUS_PORT, FALSE);
    break;
  }
}

/*********************************************************************
*
*       _ProcessPorts
*
*  Function description
*    Main state machine for HUB port processing.
*/
static void _ProcessPorts(void * p) {
  USBH_HUB        * pHub;
  USBH_HUB_PORT   * pPort;
  USBH_HUB_PORT   * pEnumPort;
  USB_DEVICE      * pHubDevice;
  USBH_URB        * pUrb;
  USBH_STATUS       Status;
  unsigned          i;
  U8                ToDo;
  USBH_TIME         CurrentTime;
  U32               SleepTime;
  USBH_BOOL         Restart;
  U32               Mask;
  unsigned          Feature;
#if USBH_SUPPORT_HUB_CLEAR_TT_BUFFER
  unsigned          j;
#endif

  pHub = USBH_CTX2PTR(USBH_HUB, p);
  USBH_ASSERT_MAGIC(pHub, USBH_HUB);
  pHubDevice = pHub->pHubDevice;
  USBH_ASSERT_MAGIC(pHubDevice, USB_DEVICE);
  if (pHubDevice->State == DEV_STATE_REMOVED) {
    if (pHub->PortResetEnumState != USBH_HUB_PORTRESET_IDLE) {
      if (NULL != pHub->pEnumDevice) {
        USBH_DEC_REF(pHub->pEnumDevice);    // Delete the device, this is the initial reference on default
        pHub->pEnumDevice = NULL;
      }
      _PortResetSetIdle(pHub);
    }
    return;
  }
  if (pHub->Suspend != 0) {
    return;
  }
  if (pHub->PendingAction != USBH_HUB_ACT_IDLE) {
    //
    // An URB is in progress. Waiting for completion.
    // The completion routine will trigger this function later.
    //
    return;
  }
  pEnumPort = NULL;
  Restart   = FALSE;
  //
  // Check all ports.
  //
  pPort = pHub->pPortList;
  CurrentTime = USBH_OS_GetTime32();
  SleepTime = 0;
  for (i = 0; i < pHub->PortCount; i++) {
    ToDo = pPort->ToDo;
    if (ToDo != 0u) {
      USBH_LOG((USBH_MCAT_HUB_SM, "_ProcessPorts: Port %u ToDo: %s", pPort->HubPortNumber, USBH_PortToDo2Str(ToDo)));
    }
    if ((ToDo & USBH_PORT_DO_DELAY) != 0u) {
      I32 Diff;

      Diff = USBH_TimeDiff(pPort->DelayUntil, CurrentTime);
      if (Diff > 0) {
        //
        // Not expired. Skip this port for now.
        //
        if (Restart == FALSE || (U32)Diff < SleepTime) {
          Restart   = TRUE;
          SleepTime = (U32)Diff;
        }
        pPort++;
        continue;
      }
      pPort->ToDo &= ~USBH_PORT_DO_DELAY;
    }
    //
    // Process ToDo's
    //
    pUrb = &pHub->PortsUrb;
    if ((ToDo & USBH_PORT_DO_DISABLE) != 0u) {
      _HubPrepareClrFeatureReq(pUrb, pHubDevice, HDC_SELECTOR_PORT_ENABLE, pPort->HubPortNumber);
      pHub->PendingAction = USBH_HUB_ACT_DISABLE;
      goto Submit;
    }
    if ((ToDo & USBH_PORT_DO_POWER_UP) != 0u) {
      _HubPrepareSetFeatureReq(pUrb, pHubDevice, HDC_SELECTOR_PORT_POWER, pPort->HubPortNumber);
      pHub->PendingAction = USBH_HUB_ACT_POWER_UP;
      goto Submit;
    }
    if ((ToDo & USBH_PORT_DO_POWER_DOWN) != 0u) {
      _HubPrepareClrFeatureReq(pUrb, pHubDevice, HDC_SELECTOR_PORT_POWER, pPort->HubPortNumber);
      pHub->PendingAction = USBH_HUB_ACT_POWER_DOWN;
      goto Submit;
    }
    if ((ToDo & USBH_PORT_DO_RESET) != 0u) {
      _HubPrepareSetFeatureReq(pUrb, pHubDevice, HDC_SELECTOR_PORT_RESET, pPort->HubPortNumber);
      pHub->PendingAction = USBH_HUB_ACT_RESET;
      goto Submit;
    }
    if ((ToDo & USBH_PORT_DO_UPDATE_STATUS) != 0u) {
      _HubPrepareGetPortStatus(pUrb, pHubDevice, pPort->HubPortNumber, pHubDevice->pCtrlTransferBuffer);
      pHub->PendingAction = USBH_HUB_ACT_GET_PORT_STATUS;
      goto Submit;
    }
    if ((ToDo & USBH_PORT_DO_SUSPEND) != 0u) {
      _HubPrepareSetFeatureReq(pUrb, pHubDevice, HDC_SELECTOR_PORT_SUSPEND, pPort->HubPortNumber);
      pHub->PendingAction = USBH_HUB_ACT_SUSPEND;
      goto Submit;
    }
    if ((ToDo & USBH_PORT_DO_RESUME) != 0u) {
      _HubPrepareClrFeatureReq(pUrb, pHubDevice, HDC_SELECTOR_PORT_SUSPEND, pPort->HubPortNumber);
      pHub->PendingAction = USBH_HUB_ACT_SUSPEND;
      goto Submit;
    }
    //
    // Handle change bits.
    //
    if ((pPort->PortStatus & PORT_C_STATUS_OVER_CURRENT) != 0u) {
      USBH_WARN((USBH_MCAT_HUB, "_ProcessPorts: PORT_C_STATUS_OVER_CURRENT Port:%d Status: 0x%X = %s", pPort->HubPortNumber, pPort->PortStatus, USBH_PortStatus2Str(pPort->PortStatus)));
      _PortEvent(USBH_PORT_EVENT_OVER_CURRENT, pHub, pPort);
    }
    Feature = HDC_SELECTOR_C_PORT_CONNECTION;
    for (Mask = PORT_C_STATUS_CONNECT; Mask <= PORT_C_STATUS_RESET; Mask <<= 1) {     // Check all five port change bits
      if ((pPort->PortStatus & Mask) != 0u) {
        pPort->PortStatus &= ~Mask;
        _HubPrepareClrFeatureReq(pUrb, pHubDevice, Feature, pPort->HubPortNumber);
        pHub->PendingAction = USBH_HUB_ACT_CLR_CHANGE;
        pPort->ToDo |= USBH_PORT_DO_UPDATE_STATUS;        // Make sure we don't miss any state change of the port
        goto Submit;
      }
      Feature++;
    }
    if (pPort == pHub->pEnumPort) {
      //
      // Skip port that is currently handled by the sub state machine.
      //
      pPort++;
      continue;
    }
    //
    // Over current ?
    //
    if ((pPort->PortStatus & (PORT_STATUS_OVER_CURRENT | PORT_STATUS_POWER)) == (PORT_STATUS_OVER_CURRENT | PORT_STATUS_POWER)) {
      USBH_WARN((USBH_MCAT_HUB, "_ProcessPorts: PORT_STATUS_OVER_CURRENT Port:%d Status: 0x%X = %s", pPort->HubPortNumber, pPort->PortStatus, USBH_PortStatus2Str(pPort->PortStatus)));
      _PortEvent(USBH_PORT_EVENT_OVER_CURRENT, pHub, pPort);
      //
      // The device uses too much current, power down port
      //
      if (pPort->pDevice != NULL) {
        USBH_MarkParentAndChildDevicesAsRemoved(pPort->pDevice);
      }
      _HubPrepareClrFeatureReq(pUrb, pHubDevice, HDC_SELECTOR_PORT_POWER, pPort->HubPortNumber);
      pHub->PendingAction = USBH_HUB_ACT_POWER_DOWN;
      goto Submit;
    }
    //
    // New connection ?
    //
    if ((pPort->PortStatus & (PORT_STATUS_CONNECT | PORT_STATUS_ENABLED)) == PORT_STATUS_CONNECT) {
      if (pPort->pDevice != NULL) {
        // Remove the old connected device first
        USBH_LOG((USBH_MCAT_HUB, "_ProcessPorts: delete dev., port connected but not enabled Port:%d Status: 0x%X = %s", pPort->HubPortNumber, pPort->PortStatus, USBH_PortStatus2Str(pPort->PortStatus)));
        USBH_MarkParentAndChildDevicesAsRemoved(pPort->pDevice);
      }
      if (pPort->RetryCounter <= USBH_RESET_RETRY_COUNTER) {
        pEnumPort = pPort;
#if USBH_SUPPORT_HUB_CLEAR_TT_BUFFER
        USBH_MEMSET(pPort->ClearTTQueue, 0, sizeof(pPort->ClearTTQueue));
#endif
      }
    }
    //
    // Device removed ?
    //
    if ((pPort->PortStatus & PORT_STATUS_CONNECT) == 0u) {
      if (pPort->pDevice != NULL) { // This device is removed
        USBH_LOG((USBH_MCAT_HUB, "_ProcessPorts: port not connected, delete dev., Port:%d Status: 0x%X = %s", pPort->HubPortNumber, pPort->PortStatus, USBH_PortStatus2Str(pPort->PortStatus)));
        USBH_MarkParentAndChildDevicesAsRemoved(pPort->pDevice);
      }
      pPort->RetryCounter = 0;
      if ((pPort->PortStatus & PORT_STATUS_ENABLED) != 0u) {
        _HubPrepareClrFeatureReq(pUrb, pHubDevice, HDC_SELECTOR_PORT_ENABLE, pPort->HubPortNumber);
        pHub->PendingAction = USBH_HUB_ACT_DISABLE;
        goto Submit;
      }
    }
#if USBH_SUPPORT_HUB_CLEAR_TT_BUFFER
    //
    // Clear TT buffer required ?
    //
    for (j = 0; j < SEGGER_COUNTOF(pPort->ClearTTQueue); j++) {
      if (pPort->ClearTTQueue[j] != 0u) {
        _HubPrepareHubRequest(pUrb, pHubDevice, pPort->ClearTTQueue[j], 1);
        pUrb->Request.ControlRequest.Setup.Request = USB_REQ_CLEAR_TT_BUFFER;
        pPort->ClearTTQueue[j] = 0;
        pHub->PendingAction = USBH_HUB_ACT_CLEAR_TT;
        goto Submit;
      }
    }
#endif
    pPort++;
  }
  //
  // If currently a port is to be reseted, run the sub state machine
  //
  if (pHub->PortResetEnumState != USBH_HUB_PORTRESET_IDLE) {
    pPort = pHub->pEnumPort;
    USBH_ASSERT_MAGIC(pPort, USBH_HUB_PORT);
    if ((pPort->ToDo & USBH_PORT_DO_DELAY) == 0u) {
      _ProcessPortReset(pHub, pPort);
      //
      // Sub state machine may have set ToDo's for the port, so trigger the main state machine immediately.
      //
      Restart = TRUE;
      SleepTime = 0;
    }
  } else {
    USBH_HOST_CONTROLLER   * pHostController;
    //
    // Start sub state machine to perform reset and 'set address' for a new connection, if possible
    //
    pHostController = pHub->pHubDevice->pHostController;
    if (pEnumPort != NULL) {
      if (pEnumPort->RetryCounter < USBH_RESET_RETRY_COUNTER) {
        if (pEnumPort->DeviceEnumActive != 0) {
          pEnumPort->DelayUntil = USBH_TIME_CALC_EXPIRATION(USBH_HUB_ENUM_POLL_DELAY);
          pEnumPort->ToDo |= USBH_PORT_DO_DELAY | USBH_PORT_DO_UPDATE_STATUS;
          Restart = TRUE;
          SleepTime = 0;
        } else {
          if (USBH_ClaimActivePortReset(pHostController) == 0u) {
            if (USBH_INC_REF(pHub->pHubDevice) == USBH_STATUS_SUCCESS) {
              pHub->pEnumPort = pEnumPort;
              if (pEnumPort->RetryCounter != 0u) {
                pHub->PortResetEnumState = USBH_HUB_PORTRESET_RESTART;
              } else {
                pHub->PortResetEnumState = USBH_HUB_PORTRESET_START;
              }
              pEnumPort->RetryCounter++;
              USBH_LOG((USBH_MCAT_HUB, "_ProcessPorts: New device on port %u", pEnumPort->HubPortNumber));
            } else {
              USBH_ReleaseActivePortReset(pHostController);
            }
            Restart = TRUE;
            SleepTime = 0;
          }
        }
      } else {
        if (pEnumPort->RetryCounter == USBH_RESET_RETRY_COUNTER) {
          pEnumPort->RetryCounter++;
          USBH_WARN((USBH_MCAT_HUB, "_ProcessPorts: Max. port retries on port %u -> PORT_ERROR!", pEnumPort->HubPortNumber));
          USBH_SetEnumErrorNotification(USBH_ENUM_ERROR_HUB_PORT_RESET | USBH_ENUM_ERROR_EXTHUBPORT_FLAG | USBH_ENUM_ERROR_STOP_ENUM_FLAG,
                                        USBH_STATUS_ERROR, 0, pEnumPort->HubPortNumber);
        }
      }
    }
  }
  if (Restart != FALSE) {
    //
    // Trigger _ProcessPorts() to be called later again
    //
    USBH_StartTimer(&pHub->ProcessPorts, SleepTime);
  } else {
    //
    // No more todo's, start interrupt transfer request for change notification.
    //
    if (pHub->InterruptUrbStatus != USBH_STATUS_PENDING) {
      //
      // No interrupt transfer is pending.
      //
      if (pHub->InterruptUrbStatus != USBH_STATUS_SUCCESS) {
        //
        // Last transfer was terminated with error. Check for retry.
        //
        if (USBH_TimeDiff(CurrentTime, pHub->IntLastErrorTime) > ((I32)pHub->pInterruptEp->IntervalTime * USBH_HUB_INT_ERR_CNT_RESTORE_THRESHOLD) / 8) {
          pHub->IntRetryCounter = 0;
        }
        pHub->IntLastErrorTime = CurrentTime;
        if (++pHub->IntRetryCounter > USBH_HUB_URB_INT_RETRY_COUNTER) {
          _HubFatalError(pHub, USBH_STATUS_ERROR, TRUE);
          return;
        }
      } else {
        pHub->IntRetryCounter = 0;
      }
      if (_HubStartPeriodicStatusTransfer(pHub) != USBH_STATUS_PENDING) {
        //
        // Trigger _ProcessPorts() to be called later for retry.
        //
        USBH_StartTimer(&pHub->ProcessPorts, 500);
      }
    }
  }
  return;
Submit:
  pHub->pPendingActionPort = pPort;
  Status = USBH_URB_SubStateSubmitRequest(&pHub->PortsSubState, pUrb, USBH_DEFAULT_SETUP_TIMEOUT, pHubDevice);
  if (Status != USBH_STATUS_PENDING) {
     USBH_WARN((USBH_MCAT_HUB, "_ProcessPorts: USBH_URB_SubStateSubmitRequest: st:%s", USBH_GetStatusStr(Status)));
    _HubFatalError(pHub, Status, FALSE);
  }
  USBH_LOG((USBH_MCAT_HUB_SM, "_ProcessPorts: PendingAction %s", USBH_HubAction2Str(pHub->PendingAction)));
}

/*********************************************************************
*
*       _ProcessPortsComplete
*
*  Function description
*    Is called on completion of a URB addressed to the HUB.
*/
static void _ProcessPortsComplete(void * p) {
  USBH_HUB        * pHub;
  USBH_HUB_PORT   * pPort;
  USBH_URB        * pUrb;

  pHub = USBH_CTX2PTR(USBH_HUB, p);
  USBH_ASSERT_MAGIC(pHub, USBH_HUB);
  pPort = pHub->pPendingActionPort;
  USBH_ASSERT_MAGIC(pPort, USBH_HUB_PORT);
  USBH_LOG((USBH_MCAT_HUB_SM, "_ProcessPortsComplete: PendingAction %s", USBH_HubAction2Str(pHub->PendingAction)));
  pUrb = &pHub->PortsUrb;
  if (pUrb->Header.Status != USBH_STATUS_SUCCESS) {
    USBH_WARN((USBH_MCAT_HUB_SM, "_ProcessPortsComplete: Action %s, urb st:%s", USBH_HubAction2Str(pHub->PendingAction), USBH_GetStatusStr(pUrb->Header.Status)));
    if (pHub->Suspend != 0) {
      goto End;
    }
#if USBH_SUPPORT_HUB_CLEAR_TT_BUFFER
    if (pHub->PendingAction == USBH_HUB_ACT_CLEAR_TT) {
      goto End;
    }
#endif
    if (++pHub->CtrlRetryCounter > USBH_HUB_URB_CTL_RETRY_COUNTER) {
      _HubFatalError(pHub, pUrb->Header.Status, TRUE);
      goto End;
    }
    pPort->ToDo |= USBH_PORT_DO_DELAY;
    pPort->DelayUntil = USBH_TIME_CALC_EXPIRATION(USBH_HUB_URB_RETRY_DELAY);
    goto End;
  }
  switch(pHub->PendingAction) {
  case USBH_HUB_ACT_GET_PORT_STATUS:
    if (pUrb->Request.ControlRequest.Length < 4u) {
      USBH_WARN((USBH_MCAT_HUB_SM, "_ProcessPortsComplete: USBH_HUB_ACT_GET_PORT_STATUS Len %u", pUrb->Request.ControlRequest.Length));
      if (++pHub->CtrlRetryCounter > USBH_HUB_URB_CTL_RETRY_COUNTER) {
        _HubFatalError(pHub, USBH_STATUS_INVALID_DESCRIPTOR, TRUE);
        break;
      }
      pPort->ToDo |= USBH_PORT_DO_DELAY;
      pPort->DelayUntil = USBH_TIME_CALC_EXPIRATION(USBH_HUB_URB_RETRY_DELAY);
      goto End;         //lint !e9042  D:102[a]
    }
    //
    // Hub has send port status.
    //
    pPort->PortStatus = USBH_LoadU32LE(pHub->pHubDevice->pCtrlTransferBuffer);
    pPort->ToDo &= ~USBH_PORT_DO_UPDATE_STATUS;
    USBH_LOG((USBH_MCAT_HUB_SM, "Port %u: port status: 0x%X = %s", pPort->HubPortNumber, pPort->PortStatus, USBH_PortStatus2Str(pPort->PortStatus)));
    break;
  case USBH_HUB_ACT_POWER_UP:
    //
    // Port is powered now.
    //
    pPort->ToDo &= ~USBH_PORT_DO_POWER_UP;
    pPort->ToDo |= USBH_PORT_DO_DELAY | USBH_PORT_DO_UPDATE_STATUS;
    pPort->DelayUntil = USBH_TIME_CALC_EXPIRATION(pHub->PowerGoodTime);
    break;
  case USBH_HUB_ACT_POWER_DOWN:
    //
    // Port is switched off now.
    //
    pPort->ToDo = USBH_PORT_DO_UPDATE_STATUS;
    break;
  case USBH_HUB_ACT_CLR_CHANGE:
    //
    // A change bis was cleared. Nothing to do here.
    //
    break;
  case USBH_HUB_ACT_DISABLE:
    //
    // Port is disabled now.
    //
    pPort->ToDo &= ~USBH_PORT_DO_DISABLE;
    pPort->ToDo |= USBH_PORT_DO_UPDATE_STATUS;
    break;
  case USBH_HUB_ACT_RESET:
    //
    // Device was reset.
    //
    pPort->ToDo &= ~USBH_PORT_DO_RESET;
    break;
  case USBH_HUB_ACT_SUSPEND:
    //
    // Port was suspended or resumed.
    //
    pPort->ToDo &= ~(USBH_PORT_DO_SUSPEND | USBH_PORT_DO_RESUME);
    pPort->ToDo |= USBH_PORT_DO_UPDATE_STATUS;
    break;
#if USBH_SUPPORT_HUB_CLEAR_TT_BUFFER
  case USBH_HUB_ACT_CLEAR_TT:
    //
    // TT buffer was cleared. Nothing to do here.
    //
    break;
#endif
  default:
    USBH_WARN((USBH_MCAT_HUB_URB, "_ProcessPortsComplete: Bad PendingAction %s", USBH_HubAction2Str(pHub->PendingAction)));
    break;
  }
  pHub->CtrlRetryCounter = 0;
End:
  pHub->PendingAction = USBH_HUB_ACT_IDLE;
  USBH_StartTimer(&pHub->ProcessPorts, 0);
  return;
}

/*********************************************************************
*
*       _ProcessDeviceComplete
*
*  Function description
*    Is called on completion of a URB addressed to the new device.
*/
static void _ProcessDeviceComplete(void * p) {
  USBH_HUB        * pHub;
  USBH_URB        * pUrb;
  USB_DEVICE      * pEnumDevice;

  pHub = USBH_CTX2PTR(USBH_HUB, p);
  USBH_ASSERT_MAGIC(pHub, USBH_HUB);
  USBH_LOG((USBH_MCAT_HUB_URB, "_ProcessDeviceComplete: PendingAction %s", USBH_HubAction2Str(pHub->PendingAction)));
  pUrb = &pHub->PortsUrb;
  if (pUrb->Header.Status != USBH_STATUS_SUCCESS) {
    USBH_WARN((USBH_MCAT_HUB_URB, "_ProcessDeviceComplete: Action %u, urb st:%s", pHub->PendingAction, USBH_GetStatusStr(pUrb->Header.Status)));
    _PortResetFail(pHub, pUrb->Header.Status, TRUE);
    goto End;
  }
  switch(pHub->PendingAction) {
  case USBH_HUB_ACT_GET_DESC:
    if (pUrb->Request.ControlRequest.Length < USB_DEVICE_DESCRIPTOR_EP0_FIFO_SIZE_OFS + 1u) {
      USBH_WARN((USBH_MCAT_HUB_URB, "_ProcessDeviceComplete: USBH_HUB_ACT_GET_DESC Len %u", pUrb->Request.ControlRequest.Length));
      _PortResetFail(pHub, USBH_STATUS_INVALID_DESCRIPTOR, TRUE);
      break;
    }
    //
    // Extract the EP0 FIFO size
    //
    pEnumDevice = pHub->pEnumDevice;
    pEnumDevice->MaxFifoSize = pEnumDevice->pCtrlTransferBuffer[USB_DEVICE_DESCRIPTOR_EP0_FIFO_SIZE_OFS];
    break;
  case USBH_HUB_ACT_SET_ADDRESS:
    break;
  default:
    USBH_WARN((USBH_MCAT_HUB_URB, "_ProcessDeviceComplete: Bad PendingAction %s", USBH_HubAction2Str(pHub->PendingAction)));
    break;
  }
End:
  pHub->PendingAction = USBH_HUB_ACT_IDLE;
  USBH_StartTimer(&pHub->ProcessPorts, 0);
  return;
}

/*********************************************************************
*
*       _HubAddAllPorts
*/
static USBH_STATUS _HubAddAllPorts(USBH_HUB * pHub) {
  unsigned int    i;
  USBH_HUB_PORT * pHubPort;
  USBH_TIME       Time;

  USBH_ASSERT_MAGIC(pHub, USBH_HUB);
  USBH_LOG((USBH_MCAT_HUB, "_HubAddAllPorts %u Ports", pHub->PortCount));
  if (pHub->PortCount == 0u) {
    USBH_WARN((USBH_MCAT_HUB, "HubCreateAllPorts: no ports!"));
    return USBH_STATUS_INVALID_PARAM;
  }
  pHubPort = (USBH_HUB_PORT *)USBH_TRY_MALLOC_ZEROED(pHub->PortCount * sizeof(USBH_HUB_PORT));
  if (pHubPort == NULL) {
    USBH_WARN((USBH_MCAT_HUB, "HubCreateAllPorts: No Memory"));
    return USBH_STATUS_MEMORY;
  }
  Time = USBH_OS_GetTime32();
  pHub->pPortList = pHubPort;
  for (i = 1; i <= pHub->PortCount; i++) {
    //
    // Initialize and create the hub ports
    //
    USBH_IFDBG(pHubPort->Magic = USBH_HUB_PORT_MAGIC);
    pHubPort->HubPortNumber = (U8)i;
    pHubPort->pExtHub       = pHub;
    pHubPort->ToDo          = USBH_PORT_DO_POWER_UP | USBH_PORT_DO_DELAY;
    Time                    = USBH_TimeAdd(Time, 10);
    pHubPort->DelayUntil    = Time;
    pHubPort++;
  }
  return USBH_STATUS_SUCCESS;
}

/*********************************************************************
*
*       _ProcessEnumHub
*
*  Function description
*    Enumeration handling for external HUB.
*/
static void _ProcessEnumHub(void * p) {
  USB_DEVICE  * pHubDev;
  USBH_STATUS   Status;
  USBH_HUB    * pHub;
  USBH_URB    * pUrb;

  pHub = USBH_CTX2PTR(USBH_HUB, p);
  USBH_ASSERT_MAGIC(pHub, USBH_HUB);
  pHubDev  = pHub->pHubDevice;
  USBH_ASSERT_MAGIC(pHubDev, USB_DEVICE);
  pUrb     = &pHubDev->EnumUrb;             // During device enumeration the URB from the device is used!
  USBH_LOG((USBH_MCAT_HUB_SM, "_ProcessEnumHub %s Dev.ref.ct: %ld", USBH_HubEnumState2Str(pHub->EnumState), pHubDev->RefCount));
  if (pHubDev->pHostController->State == HC_REMOVED) {
    Status = USBH_STATUS_DEVICE_REMOVED;
    goto RestartPort;
  }
  if (NULL != pHubDev->pParentPort->pExtHub) {
    USBH_HUB * pParentHub;                          // The parent port is an external pHub
    pParentHub = pHubDev->pParentPort->pExtHub;
    if (pParentHub->pHubDevice->State < DEV_STATE_WORKING) {
      Status = USBH_STATUS_DEVICE_REMOVED;
      goto RestartPort;
    }
  }
  switch (pHub->EnumState) {          //lint --e{9042}  D:102[a]
  case USBH_HUB_ENUM_START:
#if 0
    //
    // Request device status
    //
    if (USBH_CheckCtrlTransferBuffer(pHubDev, HDC_MAX_HUB_DESCRIPTOR_LENGTH) != 0) {
      USBH_WARN((USBH_MCAT_HUB_URB, "_ProcessEnumHub: USBH_HUB_ENUM_START: USBH_CheckCtrlTransferBuffer!"));
      Status = USBH_STATUS_MEMORY;
      goto StopPort;
    }
    _EnumPrepareGetDeviceStatus(pHubDev, pHubDev->pCtrlTransferBuffer, HDC_MAX_HUB_DESCRIPTOR_LENGTH);
    pHub->EnumState = USBH_HUB_ENUM_GET_STATUS;
    Status          = USBH_URB_SubStateSubmitRequest(&pHub->EnumSubState, pUrb, USBH_DEFAULT_SETUP_TIMEOUT, pHubDev);
    if (Status != USBH_STATUS_PENDING) {
      USBH_WARN((USBH_MCAT_HUB_URB, "_ProcessEnumHub: USBH_HUB_ENUM_START submit pUrb: st:%s", USBH_GetStatusStr(Status)));
      goto StopPort;
    }
    break;
  case USBH_HUB_ENUM_GET_STATUS:
    //
    // Check device status and request HUB descriptor
    //
    if (pUrb->Header.Status != USBH_STATUS_SUCCESS) {
      USBH_WARN((USBH_MCAT_HUB_URB, "_ProcessEnumHub: get device Status pUrb st:%s", USBH_GetStatusStr(pUrb->Header.Status)));
      Status = pUrb->Header.Status;
      goto RestartPort;
    }
    pHubDev->DevStatus = (U16)USBH_LoadU16LE((U8 *)pUrb->Request.ControlRequest.pBuffer);                    // Copy the device Status
#endif
    _HubPrepareGetHubDesc(pUrb, pHubDev, pHubDev->pCtrlTransferBuffer, HDC_MAX_HUB_DESCRIPTOR_LENGTH);
    pHub->EnumState    = USBH_HUB_ENUM_HUB_DESC;
    Status             = USBH_URB_SubStateSubmitRequest(&pHub->EnumSubState, pUrb, USBH_DEFAULT_SETUP_TIMEOUT, pHubDev);
    if (Status != USBH_STATUS_PENDING) {
      USBH_WARN((USBH_MCAT_HUB_URB, "_ProcessEnumHub: Get Hub descriptor st %s", USBH_GetStatusStr(pUrb->Header.Status)));
      goto StopPort;
    }
    break;
  case USBH_HUB_ENUM_HUB_DESC:
    //
    // Check Hub descriptor
    //
    if (pUrb->Header.Status != USBH_STATUS_SUCCESS) {
      //
      // On error. This can be also a timeout.
      //
      USBH_WARN((USBH_MCAT_HUB_URB, "_ProcessEnumHub: Get Hub descriptor st %s", USBH_GetStatusStr(pUrb->Header.Status)));
      Status = pUrb->Header.Status;
      goto RestartPort;
    }
    if (_ParseHubDescriptor(pHub, USBH_U8PTR(pUrb->Request.ControlRequest.pBuffer), pUrb->Request.ControlRequest.Length) != 0) {
      USBH_WARN((USBH_MCAT_HUB_URB, "_ProcessEnumHub: _ParseHubDescriptor failed"));
      Status = USBH_STATUS_INVALID_DESCRIPTOR;
      goto RestartPort;
    }
    //
    // Enable multi TT mode, if possible
    //
    if (_HubPrepareSetAlternate(pHub) != 0) {
      pHub->EnumState    = USBH_HUB_ENUM_SET_ALTERNATE;
      Status             = USBH_URB_SubStateSubmitRequest(&pHub->EnumSubState, pUrb, USBH_DEFAULT_SETUP_TIMEOUT, pHubDev);
      if (Status != USBH_STATUS_PENDING) {
        USBH_WARN((USBH_MCAT_HUB_URB, "_ProcessEnumHub: Set Alt setting st %s", USBH_GetStatusStr(pUrb->Header.Status)));
        goto StopPort;
      }
    } else {
      pHub->EnumState    = USBH_HUB_ENUM_DONE;
      USBH_URB_SubStateWait(&pHub->EnumSubState, 1, pHubDev);
    }
    break;
  case USBH_HUB_ENUM_SET_ALTERNATE:
    //
    // Check status from SetAltSetting control request
    //
    if (pUrb->Header.Status != USBH_STATUS_SUCCESS) {
      USBH_WARN((USBH_MCAT_HUB_URB, "_ProcessEnumHub: Set Alt setting pUrb st:%s", USBH_GetStatusStr(pUrb->Header.Status)));
      Status = pUrb->Header.Status;
      goto RestartPort;
    }
    //lint -fallthrough
    //lint -e{9090}  D:102[b]
  case USBH_HUB_ENUM_DONE:
    //
    // Add all ports to the pHub
    //
    Status = _HubAddAllPorts(pHub);
    if (Status != USBH_STATUS_SUCCESS) {
      USBH_WARN((USBH_MCAT_HUB, "_ProcessEnumHub: _HubAddAllPorts failed st: %s", USBH_GetStatusStr(Status)));
      goto StopPort;
    }
    //
    // Device enumeration now complete
    //
    pHub->EnumState    = USBH_HUB_ENUM_IDLE;
    pHubDev->EnumState = DEV_ENUM_IDLE;
    Status = USBH_CreateInterfaces(pHubDev, pHub->InterfaceNo, pHub->MultiTTAltSetting);
    if (Status != USBH_STATUS_SUCCESS) {
      goto StopPort;
    }
    Status = _HubInstallPeriodicStatusTransfer(pHub);
    if (Status != USBH_STATUS_SUCCESS) {
      USBH_WARN((USBH_MCAT_HUB_URB, "_ProcessEnumHub: _HubInstallPeriodicStatusTransfer st:%s", USBH_GetStatusStr(Status)));
      goto StopPort;
    }
    USBH_HC_DEC_REF(pHubDev->pHostController);     // Reset ref from _StartHub()
    USBH_StartTimer(&pHub->ProcessPorts, 1);
    USBH_LOG((USBH_MCAT_HUB, "_ProcessEnumHub: Hub enumeration successful"));
    break;
  default:
    USBH_ASSERT0;
    break;
  }
  return;
RestartPort:
  USBH_WARN((USBH_MCAT_HUB, "_ProcessEnumHub: Hub enumeration failed"));
  USBH_ProcessEnumError(pHubDev, Status, TRUE);
  return;
StopPort:
  USBH_WARN((USBH_MCAT_HUB, "_ProcessEnumHub: Hub enumeration failed"));
  USBH_ProcessEnumError(pHubDev, Status, FALSE);
}

/*********************************************************************
*
*       _HUB_Delete
*
*  Function description
*    Delete HUB object.
*/
static void _HUB_Delete(USBH_HUB * pHub) {
  USBH_LOG((USBH_MCAT_HUB, "USBH_HUB_Delete"));
  USBH_ASSERT_MAGIC(pHub, USBH_HUB);
  USBH_ASSERT(pHub->PortResetEnumState == USBH_HUB_PORTRESET_IDLE);
  USBH_IFDBG(pHub->Magic = 0);
  if (pHub->pPortList != NULL) {
    USBH_FREE(pHub->pPortList);
  }
  // Releases resources of USBH_URB_SubStateInit
  USBH_URB_SubStateExit(&pHub->EnumSubState);
  USBH_URB_SubStateExit(&pHub->PortResetControlUrbSubState);
  USBH_URB_SubStateExit(&pHub->PortsSubState);
  USBH_ReleaseTimer(&pHub->ProcessPorts);
  USBH_FREE(pHub);
}

/*********************************************************************
*
*       _HUB_ServiceAll
*
*  Function description
*    Trigger HUB state machine for all HUBs.
*/
static void _HUB_ServiceAll(USBH_HOST_CONTROLLER * pHostController) {
  USB_DEVICE * pDev;
  USBH_DLIST * pDevEntry;
  USBH_HUB   * pHub;

  USBH_ASSERT_MAGIC(pHostController, USBH_HOST_CONTROLLER);
  USBH_LockDeviceList(pHostController);
  pDevEntry  = USBH_DLIST_GetNext(&pHostController->DeviceList);
  while (pDevEntry != &pHostController->DeviceList) {
    pDev = GET_USB_DEVICE_FROM_ENTRY(pDevEntry);
    USBH_ASSERT_MAGIC(pDev, USB_DEVICE);
    pDevEntry = USBH_DLIST_GetNext(pDevEntry);
    pHub = pDev->pUsbHub;
    if (NULL != pHub && pDev->State == DEV_STATE_WORKING && pDev->RefCount != 0) {
      //
      // Device is a Hub
      //
      USBH_ASSERT_MAGIC(pHub, USBH_HUB);
      USBH_StartTimer(&pHub->ProcessPorts, 0);
    }
  }
  USBH_UnlockDeviceList(pHostController);
}

/*********************************************************************
*
*       _MarkChildDevicesAsRemoved
*
*  Function description
*    After removing a HUB device, this functions searches for all
*    devices connected directly or indirectly to this HUB and mark
*    them as removed.
*/
static void _MarkChildDevicesAsRemoved(USBH_HOST_CONTROLLER * pHostController) {
  int               Found;
  USB_DEVICE      * pDev;
  USBH_DLIST      * pDevList;
  USBH_DLIST      * pDevEntry;
  USB_DEVICE      * pHubDevice;

  USBH_ASSERT_MAGIC(pHostController, USBH_HOST_CONTROLLER);
  pDevList = &pHostController->DeviceList;
  do {
    //
    // Remove all devices, that have a removed parent.
    // Do until no more are found.
    //
    Found = 0;
    USBH_LockDeviceList(pHostController);
    pDevEntry = USBH_DLIST_GetNext(pDevList);
    while (pDevEntry != pDevList) {
      pDev = GET_USB_DEVICE_FROM_ENTRY(pDevEntry);
      USBH_ASSERT_MAGIC(pDev, USB_DEVICE);
      if (pDev->State != DEV_STATE_REMOVED) {
        pHubDevice = pDev->pHubDevice;
        if (pHubDevice != NULL && pHubDevice->State == DEV_STATE_REMOVED) {
          USBH_MarkDeviceAsRemoved(pDev);
          Found = 1;
        }
      }
      pDevEntry = USBH_DLIST_GetNext(pDevEntry);
    }
    USBH_UnlockDeviceList(pHostController);
  } while(Found != 0);
}

/*********************************************************************
*
*       _StartHub
*
*  Function description
*    Called after enumeration of a device, if it is a HUB.
*    Starts the state machine to query the HUB specific descriptors.
*/
static void _StartHub(USB_DEVICE * pEnumDev) {
  USBH_HUB * pHub;

  USBH_LOG((USBH_MCAT_HUB, "_StartHub"));
  USBH_ASSERT_MAGIC(pEnumDev, USB_DEVICE);
  USBH_ASSERT(NULL == pEnumDev->pUsbHub); // Hub object is always unlinked
  USBH_HC_INC_REF(pEnumDev->pHostController);
  pHub = (USBH_HUB *)USBH_TRY_MALLOC_ZEROED(sizeof(USBH_HUB));
  if (NULL == pHub) {
    USBH_WARN((USBH_MCAT_HUB, "StartHub failed, no memory"));
    USBH_ProcessEnumError(pEnumDev, USBH_STATUS_MEMORY, FALSE);
    return;
  }
  USBH_IFDBG(pHub->Magic = USBH_HUB_MAGIC);
  pHub->pHubDevice = pEnumDev;
  pHub->EnumState  = USBH_HUB_ENUM_START;
  USBH_URB_SubStateInit(&pHub->EnumSubState, pEnumDev->pHostController, &pEnumDev->DefaultEp.hEP, _ProcessEnumHub, pHub);
  USBH_InitTimer(&pHub->ProcessPorts, _ProcessPorts, pHub);
  USBH_URB_SubStateInit(&pHub->PortsSubState, pEnumDev->pHostController, &pEnumDev->DefaultEp.hEP, _ProcessPortsComplete, pHub);
  USBH_URB_SubStateInit(&pHub->PortResetControlUrbSubState, pEnumDev->pHostController, &pHub->PortResetEp0Handle, _ProcessDeviceComplete, pHub);
  //
  // Link the pHub to the device and start the pHub initialization
  //
  pEnumDev->pUsbHub   = pHub;
  pEnumDev->EnumState = DEV_ENUM_INIT_HUB;
  USBH_URB_SubStateWait(&pHub->EnumSubState, 1, NULL);
}

/*********************************************************************
*
*       _ReStartHubPort
*
*  Function description
*    Reset retry counter of all ports to allow enumerating of devices again.
*/
static void _ReStartHubPort(USBH_HOST_CONTROLLER * pHostController) {
  USB_DEVICE      * pDev;
  USBH_DLIST      * pDevEntry;
  USBH_HUB        * pHub;
  USBH_HUB_PORT   * pPort;
  unsigned          i;

  USBH_LockDeviceList(pHostController);
  pDevEntry  = USBH_DLIST_GetNext(&pHostController->DeviceList); // Searches in all host controller devices
  while (pDevEntry != &pHostController->DeviceList) {
    pDev = GET_USB_DEVICE_FROM_ENTRY(pDevEntry);
    USBH_ASSERT_MAGIC(pDev, USB_DEVICE);
    pDevEntry = USBH_DLIST_GetNext(pDevEntry);
    if (NULL != pDev->pUsbHub && pDev->State == DEV_STATE_WORKING && pDev->RefCount != 0) {
      //
      // device is a hub device
      //
      pHub = pDev->pUsbHub;
      USBH_ASSERT_MAGIC(pHub, USBH_HUB);
      pPort = pHub->pPortList;
      for (i = 1; i <= pHub->PortCount; i++) {
        USBH_ASSERT_MAGIC(pPort, USBH_HUB_PORT);
        pPort->RetryCounter = 0;
        if ((pPort->PortStatus & PORT_STATUS_POWER) == 0u) {
          pPort->ToDo = USBH_PORT_DO_POWER_UP;
        }
        pPort++;
      }
    }
  }
  USBH_UnlockDeviceList(pHostController);
}

/*********************************************************************
*
*       _DisablePort
*/
static void _DisablePort(USBH_HUB_PORT * pPort) {
  USBH_HUB * pHub;

  USBH_ASSERT_MAGIC(pPort, USBH_HUB_PORT);
  pHub = pPort->pExtHub;
  if (pHub != NULL) {
    USBH_ASSERT_MAGIC(pHub, USBH_HUB);
    pPort->ToDo |= USBH_PORT_DO_DISABLE;
    USBH_StartTimer(&pHub->ProcessPorts, 0);
  }
}

/*********************************************************************
*
*       _SetPortPower
*/
static void _SetPortPower(USBH_HUB_PORT * pPort, USBH_POWER_STATE State) {
  USBH_HUB * pHub;

  USBH_ASSERT_MAGIC(pPort, USBH_HUB_PORT);
  pHub = pPort->pExtHub;
  if (pHub != NULL) {
    USBH_ASSERT_MAGIC(pHub, USBH_HUB);
    switch (State) {
    case USBH_POWER_OFF:
      pPort->ToDo = USBH_PORT_DO_POWER_DOWN;
      break;
    case USBH_SUSPEND:
      pPort->ToDo = USBH_PORT_DO_SUSPEND;
      break;
    default: /* USBH_NORMAL_POWER */
      if ((pPort->PortStatus & PORT_STATUS_POWER) == 0u) {
        pPort->ToDo = USBH_PORT_DO_POWER_UP;
      }
      if ((pPort->PortStatus & PORT_STATUS_SUSPEND) != 0u) {
        pPort->ToDo = USBH_PORT_DO_RESUME;
      }
      break;
    }
    USBH_StartTimer(&pHub->ProcessPorts, 0);
  }
}

/*********************************************************************
*
*       _ClearTTBuffer
*/
#if USBH_SUPPORT_HUB_CLEAR_TT_BUFFER
static void _ClearTTBuffer(USBH_HUB_PORT * pPort, U8 EndpointAddress, U8 DeviceAddress, U8 EPType) {
  U16        Value;
  unsigned   j;
  USBH_HUB * pHub;

  USBH_ASSERT_MAGIC(pPort, USBH_HUB_PORT);
  Value = ((U16)EndpointAddress & 0xFu) | ((U16)DeviceAddress << 4) | ((U16)EPType << 11) | (((U16)EndpointAddress & 0x80u) << 8);
  for (j = 0; j < SEGGER_COUNTOF(pPort->ClearTTQueue); j++) {
    if (pPort->ClearTTQueue[j] == 0u) {
      pPort->ClearTTQueue[j] = Value;
      break;
    }
  }
#if 0
  pPort->ToDo |= USBH_PORT_DO_DELAY;
  pPort->DelayUntil = USBH_TIME_CALC_EXPIRATION(1);
#endif
  pHub = pPort->pExtHub;
  if (pHub != NULL) {
    USBH_StartTimer(&pHub->ProcessPorts, 0);
  }
}
#endif


static const USBH_EXT_HUB_API _EXT_HUB_Api = {
  _StartHub,
  _ReStartHubPort,
  _DisablePort,
  _HUB_Delete,
  _MarkChildDevicesAsRemoved,
#if USBH_SUPPORT_HUB_CLEAR_TT_BUFFER
  _ClearTTBuffer,
#endif
  _HUB_ServiceAll,
  _SetPortPower
};

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       USBH_HUB_GetHighSpeedHub
*
*  Function description
*    Retrieves the high speed hub from the connection tree.
*    This information is required for SPLIT transactions.
*/
USBH_HUB_PORT * USBH_HUB_GetHighSpeedHub(USBH_HUB_PORT * pHubPort) {
  if (pHubPort == NULL) {
    return NULL;
  }
  USBH_ASSERT_MAGIC(pHubPort, USBH_HUB_PORT);
  while (pHubPort->pRootHub == NULL) {
    if (pHubPort->pExtHub->pHubDevice->DeviceSpeed == USBH_HIGH_SPEED) {
      return pHubPort;
    } else {
      pHubPort = pHubPort->pExtHub->pHubDevice->pParentPort;
    }
  }
  return NULL;
}

/*********************************************************************
*
*       USBH_HUB_SuspendResume
*
*  Function description
*    Prepares hubs for suspend (stops the interrupt endpoint)
*    or re-starts the interrupt endpoint functionality after a resume.
*
*    This function may be used, if a port of a host controller is set to
*    suspend mode via the function USBH_SetRootPortPower().
*    The application must make sure that no transactions are running
*    on that port while it is suspended.
*    If there may be any external hubs connected to that port, then
*    polling of the interrupt endpoints of these hubs must be stopped while suspending.
*    To achieve this, USBH_HUB_SuspendResume() should be called with State = 0 before
*    USBH_SetRootPortPower(x, y, USBH_SUSPEND) and with State = 1 after resume with
*    USBH_SetRootPortPower(x, y, USBH_NORMAL_POWER).
*
*    All hubs connected to the given port of a host controller (directly or indirectly)
*    are handled by the function.
*
*  Parameters
*    HCIndex:  Index of the host controller.
*    Port:     Port number of the roothub. Ports are counted starting with 1.
*              if set to 0, the function applies to all ports of the root hub.
*    State:    0 - Prepare for suspend. 1 -  Return from resume.
*/
void USBH_HUB_SuspendResume(U32 HCIndex, U8 Port, U8 State) {
  USBH_HOST_CONTROLLER * pHostController;
  USB_DEVICE           * pDev;
  USBH_DLIST           * pDevEntry;
  USBH_HUB             * pHub;
  USBH_HUB_PORT        * pHubPort;

  pHostController = USBH_HCIndex2Inst(HCIndex);
  if (pHostController == NULL) {
    return;
  }
  USBH_ASSERT_MAGIC(pHostController, USBH_HOST_CONTROLLER);
  USBH_LockDeviceList(pHostController);
  pDevEntry  = USBH_DLIST_GetNext(&pHostController->DeviceList);
  while (pDevEntry != &pHostController->DeviceList) {
    pDev = GET_USB_DEVICE_FROM_ENTRY(pDevEntry);
    USBH_ASSERT_MAGIC(pDev, USB_DEVICE);
    pDevEntry = USBH_DLIST_GetNext(pDevEntry);
    pHub = pDev->pUsbHub;
    if (NULL != pHub && pDev->State == DEV_STATE_WORKING && pDev->RefCount != 0) {
      //
      // Device is a Hub
      //
      USBH_ASSERT_MAGIC(pHub, USBH_HUB);
      //
      // Get root hub port number
      //
      pHubPort = pDev->pParentPort;
      while (pHubPort->pRootHub == NULL) {
        pHubPort = pHubPort->pExtHub->pHubDevice->pParentPort;
      }
      if (Port == 0u || Port == pHubPort->HubPortNumber) {
        if (State == 0u) {
          pHub->Suspend = 1;
        } else {
          if (pHub->InterruptUrbStatus != USBH_STATUS_PENDING) {
            pHub->InterruptUrbStatus = USBH_STATUS_SUCCESS;
          }
          pHub->Suspend = 0;
        }
        USBH_StartTimer(&pHub->ProcessPorts, 2);
      }
    }
  }
  USBH_UnlockDeviceList(pHostController);
}

/*********************************************************************
*
*       USBH_SetHubPortPower
*
*  Function description
*    Set port of an external hub to a given power state.
*
*  Parameters
*    InterfaceID:  Interface ID of the external hub. May be retrieved using USBH_GetPortInfo().
*    Port:         Port number of the hub. Ports are counted starting with 1.
*    State:        New power state of the port (USBH_NORMAL_POWER, USBH_POWER_OFF or USBH_SUSPEND).
*
*  Return value
*    USBH_STATUS_SUCCESS on success.
*    Any other value means error.
*/
USBH_STATUS USBH_SetHubPortPower(USBH_INTERFACE_ID InterfaceID, U8 Port, USBH_POWER_STATE State) {
  USB_INTERFACE   * pInterface;
  USB_DEVICE      * pDevice;
  USBH_HUB        * pHub;
  USBH_STATUS       Status;

  pInterface = USBH_GetInterfaceById(InterfaceID);
  if (pInterface == NULL) {
    return USBH_STATUS_DEVICE_REMOVED;
  }
  pDevice = pInterface->pDevice;
  USBH_ASSERT_MAGIC(pDevice, USB_DEVICE);
  pHub = pDevice->pUsbHub;
  if (pHub == NULL) {
    Status = USBH_STATUS_NOT_SUPPORTED;
    goto End;
  }
  USBH_ASSERT_MAGIC(pHub, USBH_HUB);
  if (Port == 0u || Port > pHub->PortCount) {
    Status = USBH_STATUS_PORT;
    goto End;
  }
  _SetPortPower(&pHub->pPortList[Port - 1u], State);
  Status = USBH_STATUS_SUCCESS;
End:
  USBH_DEC_REF(pDevice);
  return Status;
}

/*********************************************************************
*
*       USBH_ConfigSupportExternalHubs
*
*  Function description
*    Enable support for external USB hubs.
*
*  Parameters
*    OnOff:     1 - Enable support for external hubs
*               0 - Disable support for external hubs
*
*  Additional information
*    This function should not be called if no external hub support is required to avoid
*    the code for external hubs to be linked into the application.
*/
void USBH_ConfigSupportExternalHubs(U8 OnOff) {
  if (OnOff != 0u) {
    USBH_Global.pExtHubApi = &_EXT_HUB_Api;
  } else {
    USBH_Global.pExtHubApi = NULL;
  }
}

/*************************** End of file ****************************/
