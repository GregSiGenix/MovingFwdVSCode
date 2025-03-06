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
#include "USBH_Util.h"

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/

/*********************************************************************
*
*       _ResetPipeCompletion
*
*  Function description
*    Is used for URB requests where the default completion routine of
*    the default endpoint (USBH_DEFAULT_EP) object or from the USB endpoint
*    object (USB_ENDPOINT) object can not be used. The URBs internal
*    context and the URB UbdContext contains additional information!
*/
static void _ResetPipeCompletion(USBH_URB * pUrb) {
  USBH_URB               * pOriginalUrb;
  USBH_DEFAULT_EP        * pDefaultEP;
  USB_ENDPOINT           * pEndpoint;
  USBH_STATUS              Status;
  USB_DEVICE             * pDevice;
  const USBH_HOST_DRIVER * pDriver;

  USBH_ASSERT(pUrb->Header.pfOnCompletion == NULL);                   // The helpers URBs completion routine should always NULL
  pDefaultEP  = USBH_CTX2PTR(USBH_DEFAULT_EP, pUrb->Header.pInternalContext);
  USBH_ASSERT_MAGIC(pDefaultEP, USBH_DEFAULT_EP);
  pDevice     = pDefaultEP->pUsbDevice;
  pDriver     = pDevice->pHostController->pDriver;
  pDefaultEP->UrbCount--;                                             // Decrement the count
  USBH_LOG((USBH_MCAT_URB, "_ResetPipeCompletion: urbcount: %u", pDefaultEP->UrbCount));
  pOriginalUrb = pUrb->Header.IntContext.pUrb;
  pEndpoint = USBH_CTX2PTR(USB_ENDPOINT, pOriginalUrb->Header.IntContext.pEndpoint);
  Status    = pUrb->Header.Status;  // Transfer the Status
  if (Status == USBH_STATUS_SUCCESS) {
    Status = pDriver->pfResetEndpoint(pEndpoint->hEP);
  }
  pOriginalUrb->Header.Status = Status;
  if (pOriginalUrb->Header.pfOnCompletion != NULL) {
    pOriginalUrb->Header.pfOnCompletion(pOriginalUrb);
  }
  USBH_FREE(pUrb);                                   // Delete the helper URB
  USBH_DEC_REF(pDevice);
}

/*********************************************************************
*
*       _SubmitClearFeatureEndpointStall
*
*  Function description
*    The URB is submitted if the function returns USBH_STATUS_PENDING.
*
*  Parameters
*    pDefaultEp          : Pointer to the default endpoint object.
*    pUrb                : Pointer to the URB (USB Request Block).
*    Endpoint            : Endpoint number.
*    pfInternalCompletion: Pointer to a function which will be called
*                          as soon as the request is completed.
*    pOriginalUrb        : Urb from endpoint to be cleared.
*
*  Return value
*    USBH_STATUS_PENDING : The URB has been scheduled, success!
*    Other               : An error occurred.
*/
static USBH_STATUS _SubmitClearFeatureEndpointStall(USBH_DEFAULT_EP * pDefaultEp, USBH_URB * pUrb, U8 Endpoint, USBH_ON_COMPLETION_FUNC * pfInternalCompletion, USBH_URB * pOriginalUrb) {
  USB_DEVICE  * pDevice = pDefaultEp->pUsbDevice;
  USBH_STATUS   Status;

  USBH_ASSERT_MAGIC(pDefaultEp, USBH_DEFAULT_EP);
  USBH_ZERO_MEMORY(pUrb, sizeof(USBH_URB));
  pUrb->Header.pfOnInternalCompletion = pfInternalCompletion;
  pUrb->Header.pInternalContext       = pDefaultEp;
  pUrb->Header.IntContext.pUrb        = pOriginalUrb;
  pUrb->Header.pDevice = pDevice;
  // Set clear feature endpoint stall request.
  pUrb->Header.Function                      = USBH_FUNCTION_CONTROL_REQUEST;
  pUrb->Request.ControlRequest.Setup.Type    = USB_ENDPOINT_RECIPIENT; // STD, OUT, endpoint.
  pUrb->Request.ControlRequest.Setup.Request = USB_REQ_CLEAR_FEATURE;
  pUrb->Request.ControlRequest.Setup.Value   = USB_FEATURE_STALL;
  pUrb->Request.ControlRequest.Setup.Index   = (U16)Endpoint;
  Status = USBH_INC_REF(pDevice);
  if (Status == USBH_STATUS_SUCCESS) {
    pDefaultEp->UrbCount++;
    Status = USBH_SubmitRequest(pDevice->pHostController, pDefaultEp->hEP, pUrb);
    if (Status != USBH_STATUS_PENDING) {
      pDefaultEp->UrbCount--;
      USBH_WARN((USBH_MCAT_URB, "_SubmitClearFeatureEndpointStall failed %s", USBH_GetStatusStr(Status)));
      USBH_DEC_REF(pDevice);
    }
  }
  return Status;
}

/*********************************************************************
*
*       _ResetEndpoint
*
*  Function description
*    First submits an ClearFeatureEndpointStall control request with
*    a new created URB. The control request URB UbdContext points to
*    the original URB. In the original the HCContext is set to the URB
*    function USBH_FUNCTION_RESET_ENDPOINT. In the default endpoint
*    completion routine the control request URB is destroyed.
*
*  Parameters
*    pEndpoint : Endpoint number with direction bit
*    pUrb      : Original URB
*
*  Return value
*    USBH_STATUS_PENDING on success
*    other values on error
*/
static USBH_STATUS _ResetEndpoint(USB_ENDPOINT * pEndpoint, USBH_URB * pUrb) {
  USB_DEVICE  * pDevice;
  USBH_URB    * pUrb4EP0;
  USBH_STATUS   Status;

  pDevice = pEndpoint->pUsbInterface->pDevice;
  pUrb->Header.IntContext.pEndpoint = pEndpoint;              // Store the pEndpoint pointer in the original URB
  pUrb4EP0 = (USBH_URB *)USBH_TRY_MALLOC(sizeof(USBH_URB)); // The URB must be allocated because of the asynchronous request
  if (!pUrb4EP0) {
    USBH_WARN((USBH_MCAT_URB, "_ResetEndpoint: No memory"));
    return USBH_STATUS_MEMORY;
  }
  // Prepare and submit the URB, the control endpoint is never in Halt!
  Status = _SubmitClearFeatureEndpointStall(&pDevice->DefaultEp, pUrb4EP0, pUrb->Request.EndpointRequest.Endpoint, _ResetPipeCompletion, pUrb);
  if (Status != USBH_STATUS_PENDING) {
    USBH_WARN((USBH_MCAT_URB, "_ResetEndpoint: Status: %s", USBH_GetStatusStr(Status)));
    pUrb->Header.Status = Status;
    USBH_FREE(pUrb4EP0);
  }
  return Status;
}

/*********************************************************************
*
*       _AbortEP0
*/
static USBH_STATUS _AbortEP0(const USBH_DEFAULT_EP * pEndpoint, USBH_URB * pUrb) {
  USBH_STATUS              Status;

  USBH_ASSERT_MAGIC(pEndpoint, USBH_DEFAULT_EP);
  if (pEndpoint == NULL) {
    pUrb->Header.Status = USBH_STATUS_INVALID_PARAM;
    return USBH_STATUS_INVALID_PARAM;
  }
  Status = USBH_AbortEndpoint(pEndpoint->pUsbDevice->pHostController, pEndpoint->hEP);
  USBH_ASSERT(Status != USBH_STATUS_PENDING);      // Do not return status pending and we do not call the completion routine
  return Status;
}

/*********************************************************************
*
*       _AbortEndpoint
*/
static USBH_STATUS _AbortEndpoint(const USB_ENDPOINT * pEndpoint) {
  USBH_STATUS              Status;

  Status = USBH_AbortEndpoint(pEndpoint->pUsbInterface->pDevice->pHostController, pEndpoint->hEP);
  USBH_ASSERT(Status != USBH_STATUS_PENDING);      // Do not return status pending and we do not call the completion routine
  return Status;
}

/*********************************************************************
*
*       _NewEndpoint
*
*  Function description
*    Allocates an new endpoint object, clears the object sets the interface pointer
*    and initializes the interfaces list. The endpoint handle is invalid!
*/
static USB_ENDPOINT * _NewEndpoint(USB_INTERFACE * pUsbInterface, const U8 * pEndpointDescriptor) {
  USB_ENDPOINT         * pEP;
  USB_DEVICE           * pDevice;
  USBH_HOST_CONTROLLER * pHostController;
  U16                    IntervalTime;
  U8                     EPType;
  U8                     DevAddr;
  U8                     EPAddr;
  U16                    MaxPacketSize;
  USBH_SPEED             DevSpeed;

  pDevice         = pUsbInterface->pDevice;
  pHostController = pDevice->pHostController;
  pEP             = (USB_ENDPOINT *)USBH_TRY_MALLOC_ZEROED(sizeof(USB_ENDPOINT));
  if (pEP == NULL) {
    USBH_WARN((USBH_MCAT_INTF, "NewEndpoint: USBH_MALLOC!"));
    return NULL;
  }
  pEP->pUsbInterface       = pUsbInterface;
  USBH_IFDBG(pEP->Magic    = USB_ENDPOINT_MAGIC);
  pEP->pEndpointDescriptor = pEndpointDescriptor;
  IntervalTime             = pEndpointDescriptor[USB_EP_DESC_INTERVAL_OFS];
  EPType                   = pEndpointDescriptor[USB_EP_DESC_ATTRIB_OFS] & 0x3u;
  pEP->EPType              = EPType;
  MaxPacketSize            = (U16)USBH_LoadU16LE(pEndpointDescriptor + USB_EP_DESC_PACKET_SIZE_OFS);
  if (EPType == USB_EP_TYPE_INT) {
    if (pDevice->DeviceSpeed == USBH_HIGH_SPEED) {
      if (IntervalTime == 0u || IntervalTime > 16u) {
        IntervalTime = (4uL << 3);     // Default 4ms
      } else {
        IntervalTime = (1uL << (IntervalTime - 1u));
      }
    } else {                          // LS and FS: IntervalTime in ms
      if (IntervalTime == 0u) {
        IntervalTime = 4;             // Default 4ms
      }
      IntervalTime <<= 3;             // convert to micro frames
    }
  }
#if USBH_SUPPORT_ISO_TRANSFER
  if (EPType == USB_EP_TYPE_ISO) {
    if (IntervalTime == 0u || IntervalTime > 16u) {
      IntervalTime = 8;               // Default 8
    }
    IntervalTime = 1uL << (IntervalTime - 1u);
    if (pDevice->DeviceSpeed == USBH_HIGH_SPEED) {
      pEP->MultiPktCount = ((MaxPacketSize >> 11) & 3u) + 1u;
    } else {
      IntervalTime <<= 3;             // convert to micro frames
      pEP->MultiPktCount = 1;
    }
  } else
#endif
  {
    MaxPacketSize &= 0x7FFu;
  }
  DevAddr = pUsbInterface->pDevice->UsbAddress;
  EPAddr = pEndpointDescriptor[USB_EP_DESC_ADDRESS_OFS];
  DevSpeed = pUsbInterface->pDevice->DeviceSpeed;
  pEP->EPAddr        = EPAddr;
  pEP->MaxPacketSize = MaxPacketSize;
  pEP->IntervalTime  = IntervalTime;
  pEP->hEP = pHostController->pDriver->pfAddEndpoint(pHostController->pPrvData, EPType, DevAddr, EPAddr, MaxPacketSize, IntervalTime, DevSpeed);
  if (pEP->hEP == NULL) {
    USBH_WARN((USBH_MCAT_INTF, "NewEndpoint: pfAddEndpoint dev: %d ep: 0x%x failed", pUsbInterface->pDevice->UsbAddress, pEndpointDescriptor[USB_EP_DESC_ADDRESS_OFS]));
    USBH_FREE(pEP);
    return NULL;
  }
  return pEP;
}

/*********************************************************************
*
*       _CreateEndpoints
*
*  Function description
*    Creates new endpoints for an interface.
*
*  Return value
*    USBH_STATUS_SUCCESS on success
*    Any other value means error.
*/
static USBH_STATUS _CreateEndpoints(USB_INTERFACE * pInterface) {
  const U8      * pEndpointDesc;
  USB_ENDPOINT  * pEndpoint;
  const U8      * pDesc;
  unsigned        DescLen;
  unsigned        NumEPs;
  unsigned        i;

  USBH_ASSERT_MAGIC(pInterface, USB_INTERFACE);
  USBH_FindAltInterfaceDesc(pInterface, pInterface->CurrentAlternateSetting, &pDesc, &DescLen);
  if (pDesc == NULL) {
    USBH_WARN((USBH_MCAT_INTF, "_CreateEndpoints: invalid configuration descriptor!"));
    return USBH_STATUS_INVALID_DESCRIPTOR;
  }
  NumEPs = pDesc[USB_INTERFACE_DESC_NUM_EPS_OFS];
  for (i = 0; i < NumEPs; i++) { // For each endpoint of this interface
    pEndpointDesc = USBH_FindNextEndpointDesc(&pDesc, &DescLen);
    if (pEndpointDesc == NULL) {
      USBH_WARN((USBH_MCAT_INTF, "_CreateEndpoints: invalid configuration descriptor!"));
      return USBH_STATUS_INVALID_DESCRIPTOR;
    } else {
#if USBH_SUPPORT_ISO_TRANSFER == 0
      U8 EPType;

      EPType = pEndpointDesc[USB_EP_DESC_ATTRIB_OFS] & 0x3u;
      //
      // In case we found a endpoint type that is an isochronous EP, which is by default enabled,
      // we are going to ignore them
      //
      if (EPType == USB_EP_TYPE_ISO) {
        USBH_WARN((USBH_MCAT_INTF, "_CreateEndpoints: Isochronous data transfer is disabled, ignoring EP!"));
        continue;
      }
#endif
      pEndpoint = _NewEndpoint(pInterface, pEndpointDesc);
      if (pEndpoint == NULL) {
        USBH_WARN((USBH_MCAT_INTF, "_CreateEndpoints: NewEndpoint failed!"));
        return USBH_STATUS_RESOURCES;
      } else {
        pEndpoint->pNext = pInterface->pEndpointList;
        pInterface->pEndpointList = pEndpoint;
      }
    }
  }
  return USBH_STATUS_SUCCESS;
}

/*********************************************************************
*
*       _RemoveEndpoints
*
*  Function description
*    Removes all endpoints from the interface and from the host controller.
*    Before this function can be called all URBs to this endpoint should be completed.
*/
static void _RemoveEndpoints(USB_INTERFACE * pInterface) {
  USBH_HOST_CONTROLLER * pHostController;
  USB_ENDPOINT         * pEndpoint;

  pEndpoint = pInterface->pEndpointList;
  while (pEndpoint != NULL) {
    USBH_ASSERT_MAGIC(pEndpoint, USB_ENDPOINT);
    USBH_ASSERT(pEndpoint->hEP != NULL);  // The EP must have a handle to the physical endpoint
    USBH_ASSERT(0 == pEndpoint->ActiveUrb);
    pHostController = pInterface->pDevice->pHostController;
    USBH_HC_INC_REF(pHostController);
    pHostController->pDriver->pfReleaseEndpoint(pEndpoint->hEP, USBH_DefaultReleaseEpCompletion, pHostController);
    pInterface->pEndpointList = pEndpoint->pNext;
    USBH_FREE(pEndpoint);
    pEndpoint = pInterface->pEndpointList;
  }
}

/*********************************************************************
*
*       _SetInterfaceCompletion
*/
static void _SetInterfaceCompletion(USBH_URB * pUrb) {
  USBH_URB      * pOriginalUrb;
  USB_INTERFACE * pInterface;
  USB_DEVICE    * pDevice;
  USBH_STATUS     Status;

  pInterface   = USBH_CTX2PTR(USB_INTERFACE, pUrb->Header.pInternalContext);
  pDevice      = pInterface->pDevice;
  pDevice->DefaultEp.UrbCount--;                    // Decrement the count
  USBH_LOG((USBH_MCAT_INTF, "_SetInterfaceCompletion: urbcount: %u", pDevice->DefaultEp.UrbCount));
  pOriginalUrb = USBH_CTX2PTR(USBH_URB, pUrb->Header.IntContext.pUrb);
  Status      = pUrb->Header.Status;
  if (Status == USBH_STATUS_SUCCESS) {              // On error the old endpoint structure is valid
    _RemoveEndpoints(pInterface);                   // Delete all endpoints
    pInterface->CurrentAlternateSetting = pInterface->NewAlternateSetting;      // store new alternate setting
    Status = _CreateEndpoints(pInterface);          // Add new endpoints
  }
  pOriginalUrb->Header.Status = Status;             // Update the status
  if (pOriginalUrb->Header.pfOnCompletion != NULL) {// Call the completion
    pOriginalUrb->Header.pfOnCompletion(pOriginalUrb);
  }
  USBH_FREE(pUrb);                                  // Delete the helper URB
  USBH_DEC_REF(pDevice);
}

/*********************************************************************
*
*       _GetPendingUrbCount
*
*  Function description
*/
static unsigned int _GetPendingUrbCount(const USB_INTERFACE * pInterface) {
  USB_ENDPOINT * pEndpoint;
  unsigned int   UrbCount;

  UrbCount = 0;
  for (pEndpoint = pInterface->pEndpointList; pEndpoint != NULL; pEndpoint = pEndpoint->pNext) {
    USBH_ASSERT_MAGIC(pEndpoint, USB_ENDPOINT);
    if (pEndpoint->ActiveUrb != 0) {
      UrbCount++;
    }
  }
  return UrbCount;
}

/*********************************************************************
*
*       _SubmitSetInterface
*
*  Function description
*    The URB is submitted if the function returns USBH_STATUS_PENDING.
*
*  Parameters
*    pUsbInterface    : Pointer to the USB interface object.
*    Interface        : Interface index.
*    AlternateSetting : Alternate setting value.
*    pfCompletion     : Pointer to a function which will be called
*                       as soon as the request is completed.
*    pOriginalUrb     : Pointer to a URB which will be passed
*                       to the function pointed to by pfCompletion.
*
*  Return value
*    USBH_STATUS_PENDING : The URB has been scheduled, success!
*    Other               : An error occurred.
*/
static USBH_STATUS _SubmitSetInterface(USB_INTERFACE * pUsbInterface, U16 Interface, U16 AlternateSetting, USBH_ON_COMPLETION_FUNC * pfCompletion, USBH_URB * pOriginalUrb) {
  USBH_STATUS   Status;
  USBH_URB    * pUrb;

  // Prepare a request for new interface.
  pUrb = (USBH_URB *)USBH_TRY_MALLOC_ZEROED(sizeof(USBH_URB)); // URB must be allocated because of the asynchronous request.
  if (!pUrb) {
    USBH_WARN((USBH_MCAT_INTF, "UBD Error: USBH_BD_SubmitSetInterface: USBH_MALLOC!"));
    return USBH_STATUS_MEMORY;
  }
  pUrb->Header.pfOnInternalCompletion        = pfCompletion;
  pUrb->Header.pInternalContext              = pUsbInterface;
  pUrb->Header.IntContext.pUrb               = pOriginalUrb;
  pUrb->Header.pDevice                       = pUsbInterface->pDevice;
  pUrb->Header.Function                      = USBH_FUNCTION_CONTROL_REQUEST;
  pUrb->Request.ControlRequest.Setup.Type    = USB_INTERFACE_RECIPIENT; // STD, OUT, interface.
  pUrb->Request.ControlRequest.Setup.Request = USB_REQ_SET_INTERFACE;
  pUrb->Request.ControlRequest.Setup.Value   = AlternateSetting;
  pUrb->Request.ControlRequest.Setup.Index   = Interface;
  pUsbInterface->pDevice->DefaultEp.UrbCount++;
  Status = USBH_INC_REF(pUsbInterface->pDevice);
  if (Status == USBH_STATUS_SUCCESS) {
    Status = USBH_SubmitRequest(pUsbInterface->pDevice->pHostController, pUsbInterface->pDevice->DefaultEp.hEP, pUrb);
  }
  if (Status != USBH_STATUS_PENDING) {
    USBH_WARN((USBH_MCAT_INTF, "_SubmitSetInterface failed %08x", Status));
    pUsbInterface->pDevice->DefaultEp.UrbCount--;
    USBH_DEC_REF(pUsbInterface->pDevice);
    USBH_FREE(pUrb);
  }
  return Status;
}

/*********************************************************************
*
*       _SetInterface
*
*  Function description
*    Sets a new interface in the device. All endpoint handles associated with
*    the interface will be unbound and all pending requests will be cancelled.
*    If this request returns with success, new endpoint objects are available.
*
*  Parameters
*    pInterface : Pointer to a interface structure.
*    pUrb       : Pointer to a URB structure.
*
*  Return value
*    USBH_STATUS_PENDING on success
*    other values on error
*  Notes
*    Before calling this function please do the following:
*    0. Check if the same interface is active.
*    1. Check if the alternate setting available.
*    2. Abort all data endpoint requests.
*    3. Delete all interfaces.
*    4. Create and add all interfaces.
*/
static USBH_STATUS _SetInterface(USB_INTERFACE * pInterface, USBH_URB * pUrb) {
  USBH_STATUS    Status;
  U8             AlternateSetting = pUrb->Request.SetInterface.AlternateSetting;
  U8             InterfaceNum     = pInterface->pInterfaceDescriptor[USB_INTERFACE_DESC_NUMBER_OFS];
  unsigned int   PendingUrbs;
  const U8     * pDesc;
  unsigned       DescLen;

  if (AlternateSetting == pInterface->CurrentAlternateSetting) {                          // On the same alternate setting do nothing
    pUrb->Header.Status = USBH_STATUS_SUCCESS;
    return USBH_STATUS_SUCCESS;
  }
  PendingUrbs = _GetPendingUrbCount(pInterface);
  if (PendingUrbs > 0u) {                                                                 // Check pending count
    pUrb->Header.Status = USBH_STATUS_BUSY;
    return USBH_STATUS_BUSY;
  }
  USBH_FindAltInterfaceDesc(pInterface, AlternateSetting, &pDesc, &DescLen);
  if (pDesc == NULL) {
    pUrb->Header.Status = USBH_STATUS_INVALID_PARAM;
    return USBH_STATUS_INVALID_PARAM;
  }
  pInterface->NewAlternateSetting = AlternateSetting;
  // Prepare and submit the URB, the control endpoint is never in Halt!
  Status                          = _SubmitSetInterface(pInterface, InterfaceNum, AlternateSetting, _SetInterfaceCompletion, pUrb);
  if (Status != USBH_STATUS_PENDING) {
    pUrb->Header.Status = Status;
    USBH_LOG((USBH_MCAT_INTF, "_SubmitSetInterface: %s", USBH_GetStatusStr(Status)));
  }
  return Status;
}

/*********************************************************************
*
*       _SetPowerState
*/
static USBH_STATUS _SetPowerState(const USB_INTERFACE * pInterface, const USBH_URB * pUrb) {
  USBH_POWER_STATE         PowerState;
  USBH_STATUS              Status;
  USB_DEVICE             * pUsbDevice;
  USBH_HUB_PORT          * pHubPort;
  USBH_HOST_CONTROLLER   * pHostController;
  const USBH_HOST_DRIVER * pDriver;

  Status          = USBH_STATUS_INVALID_PARAM;
  pUsbDevice      = pInterface->pDevice;
  pHubPort        = pUsbDevice->pParentPort;
  USBH_ASSERT_MAGIC(pHubPort, USBH_HUB_PORT);
  pHostController = pUsbDevice->pHostController;
  pDriver         = pHostController->pDriver;
  PowerState      = pUrb->Request.SetPowerState.PowerState;
  if (pHubPort->pRootHub != NULL) { // This is a root hub
    switch (PowerState) {
    case USBH_NORMAL_POWER:
      pDriver->pfSetPortSuspend(pHostController->pPrvData, pHubPort->HubPortNumber, USBH_PORT_POWER_RUNNING);
      Status = USBH_STATUS_SUCCESS;
      break;
    case USBH_SUSPEND:
      pDriver->pfSetPortSuspend(pHostController->pPrvData, pHubPort->HubPortNumber, USBH_PORT_POWER_SUSPEND);
      Status = USBH_STATUS_SUCCESS;
      break;
    default:
      USBH_WARN((USBH_MCAT_URB, "_SetPowerState: invalid param"));
      break;
    }
  } else {
    if (USBH_Global.pExtHubApi != NULL)  {
      USBH_Global.pExtHubApi->pfSetPortPower(pHubPort, PowerState);
    }
  }
  return Status;
}

/*********************************************************************
*
*       _ResetDevice
*
*  Function description
*    On reset we mark this device as removed and create a new device. The reason is,
*    that under some circumstances the device may change the descriptors and the interface.
*    E.g. the DFU class requires this. So we have to enumerate a new device to handle this.
*/
static void _ResetDevice(USB_DEVICE * pDevice) {
  USBH_HUB_PORT * pHubPort;
  const USBH_HOST_DRIVER * pDriver;

  pHubPort = pDevice->pParentPort;                      // Make a local copy of the parent port, the link is cleared with USBH_MarkParentAndChildDevicesAsRemoved
  USBH_ASSERT_MAGIC(pHubPort, USBH_HUB_PORT);
  USBH_MarkParentAndChildDevicesAsRemoved(pDevice);     // Delete the old instance of the device completely
  pHubPort->RetryCounter = 0;
  //
  // Disable the port
  //
  if (NULL != pHubPort->pRootHub) {
    pDriver = pDevice->pHostController->pDriver;
    pDriver->pfDisablePort(pDevice->pHostController->pPrvData, pHubPort->HubPortNumber);
  } else {
    if (USBH_Global.pExtHubApi != NULL)  {  // Parent hub port is an external port
      USBH_Global.pExtHubApi->pfDisablePort(pHubPort);
    }
  }
  USBH_HC_ServicePorts(pDevice->pHostController);       // Service all ports
}

/*********************************************************************
*
*       _DeleteUsbInterface
*
*  Function description
*    Deletes a USB interface.
*/
static void _DeleteUsbInterface(USB_INTERFACE * pInterface) {
  USBH_ASSERT(_GetPendingUrbCount(pInterface) == 0);
  _RemoveEndpoints(pInterface);
  USBH_FREE(pInterface);
}

/*********************************************************************
*
*       _AddUsbInterface
*
*  Function description
*    Adds an interface to the devices list
*/
static void _AddUsbInterface(USB_INTERFACE * pInterface) {
  USB_DEVICE * pDevice;

  USBH_ASSERT_MAGIC(pInterface, USB_INTERFACE);
  USBH_LOG((USBH_MCAT_INTF, "_AddUsbInterface pDevice-addr: %u!", pInterface->pDevice->UsbAddress));
  pDevice = pInterface->pDevice;
  USBH_DLIST_InsertTail(&pDevice->UsbInterfaceList, &pInterface->ListEntry);
  pDevice->InterfaceCount++;
}

/*********************************************************************
*
*       _RemoveUsbInterface
*
*  Function description
*    Removes an interface from the devices list.
*/
static void _RemoveUsbInterface(USB_INTERFACE * pInterface) {
  USB_DEVICE * pDevice;

  USBH_ASSERT_MAGIC(pInterface, USB_INTERFACE);
  USBH_LOG((USBH_MCAT_INTF, "_RemoveUsbInterface pDevice-addr: %u!", pInterface->pDevice->UsbAddress));
  pDevice = pInterface->pDevice;
  USBH_DLIST_RemoveEntry(&pInterface->ListEntry);
  USBH_ASSERT(pDevice->InterfaceCount);
  pDevice->InterfaceCount--;
}

/*********************************************************************
*
*       _EpUrbCompletion
*
*  Function description
*    Is called from the driver on completion of an URB.
*/
static void _EpUrbCompletion(USBH_URB * pUrb) {
  USB_ENDPOINT  * pEndpoint;
  USB_INTERFACE * pUsbInterface;

  pEndpoint = USBH_CTX2PTR(USB_ENDPOINT, pUrb->Header.pInternalContext);
  USBH_ASSERT_MAGIC(pEndpoint, USB_ENDPOINT);
  pEndpoint->ActiveUrb = 0;
  USBH_LOG((USBH_MCAT_URB, "_EpUrbCompletion: [UID %u] complete, %d, 0x%x, Status = %s",
                           pUrb->UID, pUrb->Header.Function, pEndpoint->hEP, USBH_GetStatusStr(pUrb->Header.Status)));
  if (pUrb->Header.pfOnCompletion != NULL) {
    pUrb->Header.pfOnCompletion(pUrb);
  }
  pUsbInterface = pEndpoint->pUsbInterface;
  USBH_ASSERT_MAGIC(pUsbInterface, USB_INTERFACE);
  USBH_DEC_REF(pUsbInterface->pDevice);
#if USBH_URB_QUEUE_SIZE != 0u
  USBH_RetryRequest(pUsbInterface->pDevice->pHostController);
#endif
}

/*********************************************************************
*
*       _EpIsoUrbCompletion
*
*  Function description
*    Is called from the driver on completion of an ISO transaction.
*/
static void _EpIsoUrbCompletion(USBH_URB * pUrb) {
  if (pUrb->Header.Status == USBH_STATUS_SUCCESS) {
    //
    // A single transaction was completed.
    //
    if (pUrb->Header.pfOnCompletion != NULL) {
      pUrb->Header.pfOnCompletion(pUrb);
    }
  } else {
    //
    // URB is finally terminated.
    //
    pUrb->Header.pfOnInternalCompletion = NULL;
    _EpUrbCompletion(pUrb);
  }
}

/*********************************************************************
*
*       _DefaultEpSubmitUrb
*
*  Function description
*    If the function returns USBH_STATUS_PENDING the completion routine is called.
*    On other status codes the completion routine is never called.
*
*  Parameters
*    pDevice : Pointer to the USB device object.
*    pUrb    : Pointer to the URB (USB Request Block).
*
*  Return value
*    USBH_STATUS_PENDING       : The URB has been scheduled, success!
*    USBH_STATUS_INVALID_PARAM : pDevice points to NULL.
*    Other                     : An error occurred.
*/
static USBH_STATUS _DefaultEpSubmitUrb(USB_DEVICE * pDevice, USBH_URB * pUrb) {
  USBH_STATUS       Status;
  USBH_DEFAULT_EP      * pDefaultEndpoint;
  if (pDevice == NULL) {
    return USBH_STATUS_INVALID_PARAM;
  }
  USBH_ASSERT_MAGIC(pDevice, USB_DEVICE);
  pDefaultEndpoint = &pDevice->DefaultEp;
  USBH_ASSERT_MAGIC(pDefaultEndpoint, USBH_DEFAULT_EP);
  pDefaultEndpoint->UrbCount++;
  pUrb->Header.pDevice = pDevice;
  Status = USBH_INC_REF(pDevice);
  if (Status == USBH_STATUS_SUCCESS) {
    Status = USBH_SubmitRequest(pDevice->pHostController, pDefaultEndpoint->hEP, pUrb);
  }
  if (Status != USBH_STATUS_PENDING) {
    //
    // Completion routine is never called in this case
    //
    USBH_WARN((USBH_MCAT_URB, "_DefaultEpSubmitUrb: %s", USBH_GetStatusStr(Status)));
    pUrb->Header.Status = Status;
    pDefaultEndpoint->UrbCount--;
    USBH_DEC_REF(pDevice);
  }
  return Status;
}

/*********************************************************************
*
*       _DefaultEpUrbCompletion
*
*  Function description
*    URBs internal default endpoint completion routine.
*
*  Parameters
*    pUrb : Pointer to the URB (USB Request Block).
*/
static void _DefaultEpUrbCompletion(USBH_URB * pUrb) {
  USBH_DEFAULT_EP * pUsbEndpoint;

  pUsbEndpoint = USBH_CTX2PTR(USBH_DEFAULT_EP, pUrb->Header.pInternalContext);
  USBH_ASSERT_MAGIC(pUsbEndpoint, USBH_DEFAULT_EP);
  pUsbEndpoint->UrbCount--;
  USBH_LOG((USBH_MCAT_URB, "_DefaultEpUrbCompletion: [UID %u] complete, urbcount: %u", pUrb->UID, pUsbEndpoint->UrbCount));
  if (pUrb->Header.pfOnCompletion != NULL) {
    pUrb->Header.pfOnCompletion(pUrb); // Complete the URB
  }
  USBH_DEC_REF(pUsbEndpoint->pUsbDevice);
#if USBH_URB_QUEUE_SIZE != 0u
  USBH_RetryRequest(pUsbEndpoint->pUsbDevice->pHostController);
#endif
}

/*********************************************************************
*
*       _FindNextInterfaceDesc
*
*  Function description
*    Finds an interface descriptor in a configuration descriptor.
*    Can be called multiple times to parse the whole configuration descriptor.
*
*  Parameters
*    ppDesc           : Pointer to pointer to the descriptor data to be parsed.
*                       Will be advanced by the function for repeated calls to this function.
*    pDescLen         : Pointer to the length of the descriptor data to be parsed.
*                       Will be decremented while parsing.
*    AlternateSetting : Alternate setting to be searched for.
*                       If -1, find the whole interface descriptor containing all alternate settings.
*    pLen             : Returns the length of the interface descriptor found.
*
*  Return value
*    Pointer to the interface descriptor found. NULL if not found.
*/
static const U8 * _FindNextInterfaceDesc(const U8 ** ppDesc, unsigned * pDescLen, int AlternateSetting, unsigned *pLen) {
  const U8 * pDesc;
  int        DescLen;
  const U8 * p;
  const U8 * pRet;

  pDesc   = *ppDesc;
  DescLen = (int)*pDescLen;
  pRet    = NULL;
  //
  // Find start of interface descriptor
  //
  while (DescLen > 0) {
    p        = pDesc;
    DescLen -= (int)*pDesc;
    pDesc   += *pDesc;
    if (p[1] == USB_INTERFACE_DESCRIPTOR_TYPE && (AlternateSetting < 0 || p[USB_INTERFACE_DESC_ALTSETTING_OFS] == (unsigned)AlternateSetting)) {
      pRet = p;
      break;
    }
  }
  //
  // Find end of interface descriptor
  //
  while (DescLen > 0) {
    if (pDesc[1] == USB_INTERFACE_DESCRIPTOR_TYPE && (AlternateSetting >= 0 || pDesc[USB_INTERFACE_DESC_ALTSETTING_OFS] == 0u)) {
      break;
    }
    DescLen -= (int)*pDesc;
    pDesc   += *pDesc;
  }
  if (DescLen < 0) {
    // Malformed descriptor.
    return NULL;
  }
  if (pRet != NULL) {
    *pLen     = SEGGER_PTR_DISTANCE(pDesc, pRet);     // lint N:100
    *ppDesc   = pDesc;
    *pDescLen = (unsigned)DescLen;
  }
  return pRet;
}

/*********************************************************************
*
*       _FindNextIADDesc
*
*  Function description
*    Finds an interface association descriptor in a configuration descriptor.
*    Can be called multiple times to parse the whole configuration descriptor.
*
*  Parameters
*    ppDesc           : Pointer to pointer to the descriptor data to be parsed.
*                       Will be advanced by the function for repeated calls to this function.
*    pDescLen         : Pointer to the length of the descriptor data to be parsed.
*                       Will be decremented while parsing.
*    pLen             : Returns the length of the interface descriptor found.
*
*  Return value
*    Pointer to the interface descriptor found. NULL if not found.
*/
static const U8 * _FindNextIADDesc(const U8 ** ppDesc, unsigned * pDescLen, unsigned *pLen) {
  const U8 * pDesc;
  int        DescLen;
  const U8 * p;
  const U8 * pRet;

  pDesc   = *ppDesc;
  DescLen = (int)*pDescLen;
  pRet    = NULL;
  //
  // Find start of interface association descriptor
  //
  while (DescLen > 0) {
    p        = pDesc;
    DescLen -= (int)*pDesc;
    pDesc   += *pDesc;
    if (p[1] == USB_INTERFACE_ASSOCIATION_TYPE) {
      pRet = p;
      break;
    }
  }
  //
  // Find end of interface association descriptor
  //
  while (DescLen > 0) {
    if (pDesc[1] == USB_INTERFACE_DESCRIPTOR_TYPE) {
      break;
    }
    DescLen -= (int)*pDesc;
    pDesc   += *pDesc;
  }
  if (DescLen < 0) {
    // Malformed descriptor.
    return NULL;
  }
  if (pRet != NULL) {
    *pLen     = SEGGER_PTR_DISTANCE(pDesc, pRet);     // lint N:100
    *ppDesc   = pDesc;
    *pDescLen = (unsigned)DescLen;
  }
  return pRet;
}

/*********************************************************************
*
*       _FindEndpoint
*
*  Function description
*    Finds an alternate setting inside in an interface descriptor.
*
*  Parameters
*    pInterface    : Pointer to the interface structure.
*    EPAddr        : Endpoint address.
*    ppEndpoint    : Pointer to the endpoint structure is stored here.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
static USBH_STATUS _FindEndpoint(const USB_INTERFACE * pInterface, U8 EPAddr, USB_ENDPOINT **ppEndpoint) {
  USB_ENDPOINT * pEndpoint;

  for (pEndpoint = pInterface->pEndpointList; pEndpoint != NULL; pEndpoint = pEndpoint->pNext) {
    if (pEndpoint->EPAddr == EPAddr) {
      USBH_ASSERT_MAGIC(pEndpoint, USB_ENDPOINT);
      *ppEndpoint = pEndpoint;
      return USBH_STATUS_SUCCESS;
    }
  }
  return USBH_STATUS_ENDPOINT_INVALID;
}

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       USBH_GetInterfaceById
*/
USB_INTERFACE * USBH_GetInterfaceById(USBH_INTERFACE_ID InterfaceID) {
  USBH_DLIST           * pDevEntry;
  USBH_DLIST           * pInterfaceEntry;
  USBH_HOST_CONTROLLER * pHost;
  USB_DEVICE           * pUSBDev;
  USB_INTERFACE        * pUSBInterface;
  unsigned               NumHC;
  unsigned               i;

  USBH_LOG((USBH_MCAT_INTF_API, "USBH_GetInterfaceById: InterfaceID: %u!", InterfaceID));
  NumHC = USBH_Global.HostControllerCount;
  for (i = 0; i < NumHC; i++) {       // Search in all host controller
    pHost = USBH_Global.aHostController[i];
    USBH_ASSERT_MAGIC(pHost, USBH_HOST_CONTROLLER);
    USBH_LockDeviceList(pHost);
    pDevEntry = USBH_DLIST_GetNext(&pHost->DeviceList);
    while (pDevEntry != &pHost->DeviceList) { // Search in all devices
      pUSBDev = GET_USB_DEVICE_FROM_ENTRY(pDevEntry);
      USBH_ASSERT_MAGIC(pUSBDev, USB_DEVICE);
      if (pUSBDev->RefCount != 0) {
        pInterfaceEntry = USBH_DLIST_GetNext(&pUSBDev->UsbInterfaceList);
        while (pInterfaceEntry != &pUSBDev->UsbInterfaceList) { // Search in all interfaces
          pUSBInterface = GET_USB_INTERFACE_FROM_ENTRY(pInterfaceEntry);
          USBH_ASSERT_MAGIC(pUSBInterface, USB_INTERFACE);
          if (pUSBInterface->InterfaceId == InterfaceID) { // USB interface does match
            if (USBH_INC_REF(pUSBInterface->pDevice) != USBH_STATUS_SUCCESS) {
              pUSBInterface = NULL;
            }
            USBH_UnlockDeviceList(pHost);
            return pUSBInterface;
          }
          pInterfaceEntry = USBH_DLIST_GetNext(pInterfaceEntry);
        }
      }
      pDevEntry = USBH_DLIST_GetNext(pDevEntry);
    }
    USBH_UnlockDeviceList(pHost);
  }
  USBH_LOG((USBH_MCAT_INTF_API, "USBH_GetInterfaceById: No interface found!"));
  return NULL;
}

/*********************************************************************
*
*       USBH_FindAltInterfaceDesc
*
*  Function description
*    Finds an alternate setting inside in an interface descriptor.
*
*  Parameters
*    pInterface       : Pointer to the interface structure.
*    AlternateSetting : Alternate setting to search for.
*    ppDesc           : Pointer to pointer to the descriptor found.
*    pDescLen         : Returns the length of the interface descriptor found.
*/
void USBH_FindAltInterfaceDesc(const USB_INTERFACE * pInterface, unsigned AlternateSetting, const U8 ** ppDesc, unsigned * pDescLen) {
  const U8 * pDesc;
  unsigned   DescLen;

  pDesc   = pInterface->pInterfaceDescriptor;
  DescLen = pInterface->InterfaceDescriptorSize;
  *ppDesc = _FindNextInterfaceDesc(&pDesc, &DescLen, AlternateSetting, pDescLen);
}

/*********************************************************************
*
*       USBH_EpSubmitUrb
*
*  Function description
*/
USBH_STATUS USBH_EpSubmitUrb(USB_ENDPOINT * pUsbEndpoint, USBH_URB * pUrb) {
  USBH_STATUS            Status;
  USB_DEVICE           * pDevice;
  USBH_HOST_CONTROLLER * pHostController;

  pDevice = pUsbEndpoint->pUsbInterface->pDevice;
  USBH_ASSERT_MAGIC(pDevice, USB_DEVICE);
  pHostController = pDevice->pHostController;
  USBH_ASSERT_MAGIC(pHostController, USBH_HOST_CONTROLLER);
  pUsbEndpoint->ActiveUrb = 1;
  pUrb->Header.pDevice = pDevice;
  Status = USBH_INC_REF(pDevice);
  if (Status == USBH_STATUS_SUCCESS) {
    Status = USBH_SubmitRequest(pHostController, pUsbEndpoint->hEP, pUrb);
  }
  if (Status != USBH_STATUS_PENDING) {          // Completion routine is never called in this case
    USBH_WARN((USBH_MCAT_URB, "USBH_EpSubmitUrb: %s", USBH_GetStatusStr(Status)));
    pUrb->Header.Status = Status;
    pUsbEndpoint->ActiveUrb = 0;
    USBH_DEC_REF(pDevice);
  }
  return Status;
}

/*********************************************************************
*
*       USBH_SubmitUrb
*
*  Function description
*    Submits an URB. Interface function for all asynchronous requests.
*
*  Parameters
*    hInterface:   Handle to a interface.
*    pUrb:         Pointer to a caller allocated structure.
*                  * [IN]  The URB which should be submitted.
*                  * [OUT] Submitted URB with the appropriate status and
*                          the received data if any. The storage for the URB must be permanent
*                          as long as the request is pending. The host controller can
*                          define special alignment requirements for the URB or the data
*                          transfer buffer.
*
*  Return value
*    The request can fail for different reasons. In that case the return value is different
*    from  USBH_STATUS_PENDING  or  USBH_STATUS_SUCCESS.  If  the  function  returns
*    USBH_STATUS_PENDING  the completion function is called later. In all other cases the
*    completion  routine  is  not  called.  If  the  function  returns  USBH_STATUS_SUCCESS,  the
*    request was processed immediately. On error the request cannot be processed.
*
*  Additional information
*    If the status USBH_STATUS_PENDING is returned the ownership of the URB is passed to
*    the driver. The storage of the URB must not be freed nor modified as long as the
*    ownership is assigned to the driver. The driver passes the URB back to the
*    application by calling the completion routine. An URB that transfers data can be
*    pending for a long time.
*    Please make sure that the URB is not located in the stack. Otherwise the structure
*    may be corrupted in memory. Either use USBH_Malloc() or use global/static memory.
*
*  Note:
*    A pending  URB  transactions  may be  aborted  with  an  abort  request  by  using
*    USBH_SubmitUrb with a new URB where
*    \tt{Urb->Header.Function = USBH_FUNCTION_ABORT_ENDPOINT}
*    and
*    \tt{Urb->Request.EndpointRequest.Endpoint = EndpointAddressToAbort}.
*    Otherwise this operation will last until the device has responded to the request or the
*    device has been disconnected.
*/
USBH_STATUS USBH_SubmitUrb(USBH_INTERFACE_HANDLE hInterface, USBH_URB * pUrb) {
  USB_INTERFACE * pUsbInterface;
  USB_ENDPOINT  * pEndpoint;
  USB_DEVICE    * pDevice;
  USBH_STATUS     Status;

  USBH_LOG((USBH_MCAT_URB, "USBH_SubmitUrb: %s", USBH_UrbFunction2Str(pUrb->Header.Function)));
  pUsbInterface = hInterface;
  USBH_ASSERT_MAGIC(pUsbInterface, USB_INTERFACE);
  USBH_ASSERT_PTR(pUrb);
#if USBH_SUPPORT_TRACE
  USBH_TRACE_RECORD_API_U32x2(USBH_TRACE_ID_USBH_SUBMITURB, pUsbInterface->InterfaceId, (pUrb->Header.Function + USBH_TRACE_RESSOURCE_ID_OFFSET));
#endif
#if USBH_DEBUG > 1
  USBH_OS_Lock(USBH_MUTEX_DEVICE);
  pUrb->UID = ++USBH_Global.URBUniqueID;
  USBH_OS_Unlock(USBH_MUTEX_DEVICE);
#endif
  pUrb->Header.Status = USBH_STATUS_PENDING; // Default status
  pDevice             = pUsbInterface->pDevice;
  //
  // Always let abort URBs through even if the device was removed.
  //
  if ((pUrb->Header.Function != USBH_FUNCTION_ABORT_ENDPOINT) && pDevice->State < DEV_STATE_WORKING) {
    return USBH_STATUS_DEVICE_REMOVED;
  }
  switch (pUrb->Header.Function) {
  //
  // Control requests
  //
  case USBH_FUNCTION_CONTROL_REQUEST:
    pUrb->Header.pInternalContext       = &pDevice->DefaultEp;
    pUrb->Header.pfOnInternalCompletion = _DefaultEpUrbCompletion;
    USBH_LOG((USBH_MCAT_URB, "[UID %u] Submit C, 0x%x", pUrb->UID, &pDevice->DefaultEp));
    Status                              = _DefaultEpSubmitUrb(pUsbInterface->pDevice, pUrb);
    break;
  //
  // Bulk and interrupt requests
  //
  case USBH_FUNCTION_BULK_REQUEST:
  case USBH_FUNCTION_INT_REQUEST:
    Status = _FindEndpoint(pUsbInterface, pUrb->Request.BulkIntRequest.Endpoint, &pEndpoint);
    if (Status != USBH_STATUS_SUCCESS) {
      break;
    }
    if (pEndpoint->EPType == USB_EP_TYPE_ISO) {
      Status = USBH_STATUS_ENDPOINT_INVALID;
      break;
    }
    pUrb->Header.pInternalContext       = pEndpoint;
    pUrb->Header.pfOnInternalCompletion = _EpUrbCompletion;
    USBH_LOG((USBH_MCAT_URB, "[UID %u] Submit B/I, %d, 0x%x", pUrb->UID, pUrb->Header.Function, pEndpoint->hEP));
#if USBH_DEBUG > 1
    if ((pUrb->Request.BulkIntRequest.Endpoint & 0x80) != 0u &&
        (pUrb->Request.BulkIntRequest.Length == 0u || (pUrb->Request.BulkIntRequest.Length % pEndpoint->MaxPacketSize) != 0)) {
      USBH_WARN((USBH_MCAT_URB, "[UID %u] USBH_SubmitUrb: Bad IN request size %d", pUrb->UID, pUrb->Request.BulkIntRequest.Length));
      USBH_ASSERT0;
    }
#endif
    Status = USBH_EpSubmitUrb(pEndpoint, pUrb);
    if (Status != USBH_STATUS_SUCCESS && Status != USBH_STATUS_PENDING) {
      USBH_WARN((USBH_MCAT_URB, "[UID %u] USBH_SubmitUrb: Error Ep:0x%x %s", pUrb->UID, pUrb->Request.BulkIntRequest.Endpoint, USBH_GetStatusStr(Status )));
    }
    break;
#if USBH_SUPPORT_ISO_TRANSFER
  //
  // ISO requests
  //
  case USBH_FUNCTION_ISO_REQUEST:
    Status = _FindEndpoint(pUsbInterface, pUrb->Request.IsoRequest.Endpoint, &pEndpoint);
    if (Status != USBH_STATUS_SUCCESS) {
      break;
    }
    if (pEndpoint->EPType != USB_EP_TYPE_ISO) {
      Status = USBH_STATUS_ENDPOINT_INVALID;
      break;
    }
    pUrb->Header.pInternalContext       = pEndpoint;
    pUrb->Header.pfOnInternalCompletion = _EpIsoUrbCompletion;
    pUrb->Header.IntContext.pEndpoint   = pEndpoint;
    USBH_LOG((USBH_MCAT_URB, "[UID %u] Submit ISO, 0x%x", pUrb->UID, pEndpoint->hEP));
    Status                              = USBH_EpSubmitUrb(pEndpoint, pUrb);
    break;
#endif
  //
  // Reset endpoint
  //
  case USBH_FUNCTION_RESET_ENDPOINT:
    Status = _FindEndpoint(pUsbInterface, pUrb->Request.EndpointRequest.Endpoint, &pEndpoint);
    if (Status != USBH_STATUS_SUCCESS) {
      break;
    }
    USBH_LOG((USBH_MCAT_URB, "[UID %u] Reset, 0x%x", pUrb->UID, pEndpoint->hEP));
    Status = _ResetEndpoint(pEndpoint, pUrb);
    break;
  //
  // Abort endpoint
  //
  case USBH_FUNCTION_ABORT_ENDPOINT:
    if (pUrb->Request.EndpointRequest.Endpoint == 0u) {
      Status    = _AbortEP0(&pDevice->DefaultEp, pUrb);
    } else {
      Status = _FindEndpoint(pUsbInterface, pUrb->Request.EndpointRequest.Endpoint, &pEndpoint);
      if (Status != USBH_STATUS_SUCCESS) {
        break;
      }
      USBH_LOG((USBH_MCAT_URB, "[UID %u] Abort, %d, 0x%x", pUrb->UID, pUrb->Header.Function, pEndpoint->hEP));
      Status = _AbortEndpoint(pEndpoint);
    }
    break;
  case USBH_FUNCTION_SET_INTERFACE:
    Status = _SetInterface(pUsbInterface, pUrb);
    break;
  case USBH_FUNCTION_SET_POWER_STATE:
    Status = _SetPowerState(pUsbInterface, pUrb);
    break;
  case USBH_FUNCTION_RESET_DEVICE:
    _ResetDevice(pDevice);
    pUrb->Header.Status = USBH_STATUS_SUCCESS;
    Status = USBH_STATUS_SUCCESS;
    break;
  default:
    USBH_WARN((USBH_MCAT_URB, "URB: USBH_SubmitUrb: invalid URB function: %d!",pUrb->Header.Function));
    Status = USBH_STATUS_INVALID_PARAM;
    break;
  }
  if (Status != USBH_STATUS_SUCCESS && Status != USBH_STATUS_PENDING) {
    USBH_LOG((USBH_MCAT_URB, "[UID %u] USBH_SubmitUrb: %s status:%s ", pUrb->UID, USBH_UrbFunction2Str(pUrb->Header.Function),USBH_GetStatusStr(Status)));
  }
  return Status;
}

/*********************************************************************
*
*       USBH_IsoDataCtrl
*
*  Function description
*    Acknowledge ISO data received from an IN EP or provide data for OUT EPs.
*
*    On order to start ISO OUT transfers after calling USBH_SubmitUrb(), initially
*    the output packet queue must be filled. For that purpose this function
*    must be called repeatedly until is does not return USBH_STATUS_NEED_MORE_DATA any more.
*
*  Parameters
*    pUrb:         Pointer to the an active URB running ISO transfers.
*    pIsoData:     ISO data structure.
*
*  Return value
*    USBH_STATUS_SUCCESS or USBH_STATUS_NEED_MORE_DATA on success or error code on failure.
*/
USBH_STATUS USBH_IsoDataCtrl(const USBH_URB * pUrb, USBH_ISO_DATA_CTRL *pIsoData) {
  USB_ENDPOINT  * pEndpoint;
  USB_DEVICE    * pDevice;
  USBH_HOST_CONTROLLER * pHostController;

  USBH_ASSERT_PTR(pUrb);
  if (pUrb->Header.Function != USBH_FUNCTION_ISO_REQUEST ||
      pUrb->Header.pfOnInternalCompletion != _EpIsoUrbCompletion) {
    return USBH_STATUS_ENDPOINT_INVALID;
  }
  pEndpoint = pUrb->Header.IntContext.pEndpoint;
  USBH_ASSERT_MAGIC(pEndpoint, USB_ENDPOINT);
  if (pEndpoint->EPType != USB_EP_TYPE_ISO ||
      pEndpoint->ActiveUrb == 0) {
    return USBH_STATUS_ENDPOINT_INVALID;
  }
  pDevice = pUrb->Header.pDevice;
  USBH_ASSERT_MAGIC(pDevice, USB_DEVICE);
  pHostController = pDevice->pHostController;
  USBH_ASSERT_MAGIC(pHostController, USBH_HOST_CONTROLLER);
  //
  // Driver function is called only, if an ISO EP could be added before.
  //
  return pHostController->pDriver->pfIsoData(pEndpoint->hEP, pIsoData);
}

/*********************************************************************
*
*       USBH_GetMaxTransferSize
*
*  Function description
*    Returns the maximum transfer size supported by the driver
*    that can be used in an URB for an endpoint.
*/
USBH_STATUS USBH_GetMaxTransferSize(USBH_INTERFACE_HANDLE hInterface, U8 Endpoint, U32 *pMaxTransferSize) {
  USB_INTERFACE          * pUsbInterface;
  USB_ENDPOINT           * pEndpoint;
  USB_DEVICE             * pDevice;
  USBH_HC_EP_HANDLE        hEP;
  const USBH_HOST_DRIVER * pDriver;
  USBH_IOCTL_PARA          IoctlPara;
  USBH_STATUS              Status;

  pUsbInterface = hInterface;
  USBH_ASSERT_MAGIC(pUsbInterface, USB_INTERFACE);
  pDevice = pUsbInterface->pDevice;
  USBH_ASSERT_MAGIC(pDevice, USB_DEVICE);
  if (Endpoint == 0u) {
    hEP = pDevice->DefaultEp.hEP;
  } else {
    Status = _FindEndpoint(pUsbInterface, Endpoint, &pEndpoint);
    if (Status != USBH_STATUS_SUCCESS) {
      return Status;
    }
    USBH_ASSERT_MAGIC(pEndpoint, USB_ENDPOINT);
    hEP = pEndpoint->hEP;
  }
  pDriver = pDevice->pHostController->pDriver;
  if (pDriver->pfIoctl != NULL) {
    IoctlPara.u.MaxTransferSize.hEndPoint = hEP;
    if (pDriver->pfIoctl(pDevice->pHostController->pPrvData, USBH_IOCTL_FUNC_GET_MAX_TRANSFER_SIZE, &IoctlPara) == USBH_STATUS_SUCCESS) {
      *pMaxTransferSize = IoctlPara.u.MaxTransferSize.Size;
      return USBH_STATUS_SUCCESS;
    }
  }
  *pMaxTransferSize = 0x80000000u;
  return USBH_STATUS_SUCCESS;
}

/*********************************************************************
*
*       USBH_GetInterfaceInfo
*
*  Function description
*    Obtain information about a specified interface.
*
*  Parameters
*    InterfaceID:    ID of the interface to query.
*    pInterfaceInfo: Pointer to a caller allocated structure that will
*                    receive the interface information on success.
*
*  Return value
*    USBH_STATUS_SUCCESS on success.
*    Any other value means error.
*
*  Additional information
*    Can be used to identify a USB interface without having to open it. More detailed
*    information can be requested after the USB interface is opened.
*
*    If the interface belongs to a device
*    which is no longer connected to the host USBH_STATUS_DEVICE_REMOVED is
*    returned and pInterfaceInfo is not filled.
*/
USBH_STATUS USBH_GetInterfaceInfo(USBH_INTERFACE_ID InterfaceID, USBH_INTERFACE_INFO * pInterfaceInfo) {
  USB_INTERFACE * pInterface;
  USB_DEVICE    * pDevice;

  USBH_LOG((USBH_MCAT_INTF_API, "USBH_GetInterfaceInfo: InterfaceID: %u!", InterfaceID));
  pInterface = USBH_GetInterfaceById(InterfaceID);
  if (pInterface == NULL) {
    USBH_WARN((USBH_MCAT_INTF_API, "USBH_GetInterfaceInfo: USBH_BD_GetInterfaceById ID: %u failed!", InterfaceID));
    return USBH_STATUS_DEVICE_REMOVED;
  }
  pDevice                           = pInterface->pDevice;
  // Fill in the information
  pInterfaceInfo->InterfaceId       = InterfaceID;
  pInterfaceInfo->DeviceId          = pDevice->DeviceId;
  pInterfaceInfo->VendorId          = pDevice->DeviceDescriptor.idVendor;
  pInterfaceInfo->ProductId         = pDevice->DeviceDescriptor.idProduct;
  pInterfaceInfo->bcdDevice         = pDevice->DeviceDescriptor.bcdDevice;
  pInterfaceInfo->Interface         = pInterface->pInterfaceDescriptor[USB_INTERFACE_DESC_NUMBER_OFS];
  pInterfaceInfo->Class             = pInterface->pInterfaceDescriptor[USB_INTERFACE_DESC_CLASS_OFS];
  pInterfaceInfo->SubClass          = pInterface->pInterfaceDescriptor[USB_INTERFACE_DESC_SUBCLASS_OFS];
  pInterfaceInfo->Protocol          = pInterface->pInterfaceDescriptor[USB_INTERFACE_DESC_PROTOCOL_OFS];
  pInterfaceInfo->AlternateSetting  = pInterface->CurrentAlternateSetting;
  pInterfaceInfo->OpenCount         = pInterface->OpenCount;
  pInterfaceInfo->ExclusiveUsed     = pInterface->ExclusiveUsed;
  pInterfaceInfo->Speed             = pDevice->DeviceSpeed;
  pInterfaceInfo->NumConfigurations = pDevice->DeviceDescriptor.bNumConfigurations;
  pInterfaceInfo->CurrentConfiguration = pDevice->ConfigurationIndex;
  pInterfaceInfo->SerialNumberSize  = (U8)pDevice->SerialNumberSize;
  pInterfaceInfo->HCIndex = pDevice->pHostController->Index;
  USBH_DEC_REF(pDevice);
  return USBH_STATUS_SUCCESS;
}

/*********************************************************************
*
*       USBH_GetInterfaceSerial
*
*  Function description
*    Retrieves the serial number of the device containing the given interface.
*
*  Parameters
*    InterfaceID:        ID of the interface to query.
*    BuffSize:           Size of the buffer pointed to by pSerialNumber.
*    pSerialNumber:      Pointer to a buffer where the serial number is stored.
*    pSerialNumberSize:  [OUT] Number of bytes copied into the buffer.
*
*  Return value
*    USBH_STATUS_SUCCESS on success. Other values indicate an error.
*
*  Additional information
*    The serial number is returned as a UNICODE string in USB little endian format.
*    The number of valid bytes is returned in pSerialNumberSize. The string is not zero terminated.
*    The returned data does not contain a USB descriptor header and is encoded in the
*    first language Id. This string is a copy of the serial number string that was requested
*    during the enumeration.
*    If the device does not support a USB serial number string the function returns USBH_STATUS_SUCCESS
*    and a length of 0.
*    If the given buffer size is too small the serial number returned is truncated.
*/
USBH_STATUS USBH_GetInterfaceSerial(USBH_INTERFACE_ID InterfaceID, U32 BuffSize, U8 *pSerialNumber, U32 *pSerialNumberSize) {
  USB_INTERFACE * pInterface;
  USB_DEVICE    * pDevice;

  USBH_LOG((USBH_MCAT_INTF_API, "USBH_GetInterfaceSerial: InterfaceID: %u!", InterfaceID));
  *pSerialNumberSize = 0;
  pInterface = USBH_GetInterfaceById(InterfaceID);
  if (pInterface == NULL) {
    USBH_WARN((USBH_MCAT_INTF, "USBH_GetInterfaceSerial: USBH_BD_GetInterfaceById ID: %u failed!", InterfaceID));
    return USBH_STATUS_DEVICE_REMOVED;
  }
  pDevice = pInterface->pDevice;
  if (BuffSize > pDevice->SerialNumberSize) {
    BuffSize = pDevice->SerialNumberSize;
  }
  USBH_MEMCPY(pSerialNumber, pDevice->pSerialNumber, BuffSize);
  *pSerialNumberSize = BuffSize;
  USBH_DEC_REF(pDevice);
  return USBH_STATUS_SUCCESS;
}

/*********************************************************************
*
*       USBH_GetPortInfo
*
*  Function description
*    Obtains information about a connected USB device.
*
*  Parameters
*    InterfaceID:    ID of an interface of the device to query.
*    pPortInfo:      Pointer to a caller allocated structure that will
*                    receive the port information on success.
*
*  Return value
*    USBH_STATUS_SUCCESS on success.
*    Any other value means error.
*/
USBH_STATUS USBH_GetPortInfo(USBH_INTERFACE_ID InterfaceID, USBH_PORT_INFO * pPortInfo) {
  USB_INTERFACE * pInterface;
  USB_DEVICE    * pDevice;
  USBH_HUB_PORT * pHubPort;

  USBH_ASSERT(pPortInfo != NULL);
  pInterface = USBH_GetInterfaceById(InterfaceID);
  if (pInterface == NULL) {
    USBH_WARN((USBH_MCAT_INTF_API, "USBH_GetPortInfo: _GetInterfaceById ID: %u failed!", InterfaceID));
    return USBH_STATUS_DEVICE_REMOVED;
  }
  pDevice = pInterface->pDevice;
  pHubPort = pDevice->pParentPort;
  USBH_ASSERT_MAGIC(pHubPort, USBH_HUB_PORT);
  pPortInfo->IsHighSpeedCapable = (pDevice->DeviceSpeed == USBH_HIGH_SPEED) ? 1 : 0;
  if (pHubPort->pExtHub != NULL) {
    pPortInfo->IsRootHub        = 0;
    pPortInfo->HubInterfaceId   = pHubPort->pExtHub->InterfaceId;
    pPortInfo->HubDeviceId      = pDevice->pHubDevice->DeviceId;
  } else {
    pPortInfo->IsRootHub        = 1;
    pPortInfo->HubInterfaceId   = 0;
    pPortInfo->HubDeviceId      = 0;
  }
  pPortInfo->IsSelfPowered      = (pDevice->pConfigDescriptor[USBH_CONFIG_DESCRIPTOR_OFF_BMATTRIBUTES] >> 6) & 1u;
  pPortInfo->MaxPower           = (U16)pDevice->pConfigDescriptor[USBH_CONFIG_DESCRIPTOR_OFF_MAXPOWER] << 1;
  pPortInfo->PortNumber         = pHubPort->HubPortNumber;
  pPortInfo->PortSpeed          = pDevice->DeviceSpeed;
  pPortInfo->PortStatus         = pHubPort->PortStatus;
  pPortInfo->DeviceId           = pDevice->DeviceId;
  pPortInfo->HCIndex            = pDevice->pHostController->Index;
  USBH_DEC_REF(pDevice);
  return USBH_STATUS_SUCCESS;
}

/*********************************************************************
*
*       USBH_OpenInterface
*
*  Function description
*    Opens the specified interface.
*
*  Parameters
*    InterfaceID:      Specifies the interface to open by its interface Id. The interface
*                      ID can be obtained by a call to  USBH_GetInterfaceId().
*    Exclusive:        Specifies if the interface should be opened exclusive or not. If
*                      the value is nonzero the function succeeds only if no other
*                      application has an open handle to this interface.
*    pInterfaceHandle: Pointer where the handle to the opened interface is stored.
*
*  Return value
*    USBH_STATUS_SUCCESS on success.
*    Any other value means error.
*
*  Additional information
*    The handle returned by this function via the pInterfaceHandle parameter is used by the
*    functions that perform data transfers. The returned handle must be closed with
*    USBH_CloseInterface() when it is no longer required.
*
*    If the interface is allocated exclusive no other
*    application can open it.
*/
USBH_STATUS USBH_OpenInterface(USBH_INTERFACE_ID InterfaceID, U8 Exclusive, USBH_INTERFACE_HANDLE * pInterfaceHandle) {
  USB_INTERFACE * pInterface;
  USBH_STATUS     Status;

  USBH_LOG((USBH_MCAT_INTF_API, "USBH_OpenInterface: InterfaceID: %u!", InterfaceID));
  pInterface = USBH_GetInterfaceById(InterfaceID);
  if (pInterface == NULL) {
    USBH_LOG((USBH_MCAT_INTF_API, "USBH_OpenInterface: USBH_BD_GetInterfaceById iface-ID: %u!", InterfaceID));
    * pInterfaceHandle = NULL;
    Status             = USBH_STATUS_DEVICE_REMOVED;
  } else {
    Status = USBH_STATUS_BUSY;                                   // Check exclusive usage
    if (Exclusive != 0u) {
      if (pInterface->ExclusiveUsed == 0u && pInterface->OpenCount == 0u) { // On exclusive
        pInterface->ExclusiveUsed = 1;
        Status = USBH_STATUS_SUCCESS;
      }
    } else {                                                    // On not exclusive
      if (pInterface->ExclusiveUsed == 0u) {
        Status = USBH_STATUS_SUCCESS;
      }
    }
    if (Status == USBH_STATUS_SUCCESS) {                        // On success
      pInterface->OpenCount++;
      *pInterfaceHandle = pInterface;
    } else {                                                    // On error
      USBH_DEC_REF(pInterface->pDevice);
      *pInterfaceHandle = NULL;
      USBH_WARN((USBH_MCAT_INTF_API, "USBH_OpenInterface IfaceID: %u!", InterfaceID));
    }
  }
  return Status;
}

/*********************************************************************
*
*       USBH_CloseInterface
*
*  Function description
*    Close an interface handle that was opened with USBH_OpenInterface().
*
*  Parameters
*    hInterface:    Handle to a valid interface, returned by USBH_OpenInterface().
*
*  Additional information
*    Each handle must be closed one time. Calling this function with an invalid handle
*    leads to undefined behavior.
*/
void USBH_CloseInterface(USBH_INTERFACE_HANDLE hInterface) {
  USB_INTERFACE * pInterface;

  pInterface = hInterface;
  if (pInterface == NULL) {
    USBH_WARN((USBH_MCAT_INTF_API, "USBH_CloseInterface was called with hInterface = 0 (invalid)"));
    return;
  }
  USBH_ASSERT_MAGIC(pInterface,          USB_INTERFACE);
  USBH_ASSERT_MAGIC(pInterface->pDevice, USB_DEVICE);
  USBH_ASSERT      (pInterface->OpenCount > 0); // Always unequal zero also if opened exclusive
  USBH_LOG((USBH_MCAT_INTF_API, "USBH_CloseInterface: InterfaceId: %u!", pInterface->InterfaceId));
  pInterface->ExclusiveUsed = 0;
  pInterface->OpenCount--;
  USBH_DEC_REF(pInterface->pDevice);               // The caller is responsible to cancel all pending URB before closing the interface
}

/*********************************************************************
*
*       USBH_GetInterfaceIdByHandle
*
*  Function description
*    Get the interface ID for a given index. A returned value of zero indicates an error.
*
*  Parameters
*    hInterface:    Handle to a valid interface, returned by USBH_OpenInterface().
*    pInterfaceId:  Pointer to a variable that will receive the interface id.
*
*  Return value
*    USBH_STATUS_SUCCESS on success.
*    Any other value means error.
*
*  Additional information
*    Returns the interface ID if the handle to the interface is available. This may be useful
*    if a Plug and Play notification is received and the application checks if it is related to
*    a given handle. The application can avoid calls to this function if the interface ID is
*    stored in the device context of the application.
*/
USBH_STATUS USBH_GetInterfaceIdByHandle(USBH_INTERFACE_HANDLE hInterface, USBH_INTERFACE_ID * pInterfaceId) {
  USB_INTERFACE * pInterface;

  if (NULL == hInterface) {
    return USBH_STATUS_INVALID_PARAM;
  }
  pInterface = hInterface;
  USBH_ASSERT_MAGIC(pInterface, USB_INTERFACE);
  USBH_LOG((USBH_MCAT_INTF_API, "USBH_GetInterfaceIdByHandle: InterfaceId: %u!", pInterface->InterfaceId));
  *pInterfaceId = pInterface->InterfaceId;
  USBH_ASSERT(pInterface->InterfaceId != 0);
  return USBH_STATUS_SUCCESS;
}

/*********************************************************************
*
*       USBH_GetInterfaceCurrAltSetting
*
*  Function description
*    Get the current alternate settings for the given interface handle.
*
*  Parameters
*    hInterface:      Handle to a valid interface, returned by USBH_OpenInterface().
*    pCurAltSetting:  Pointer to a variable that will receive the current alternate setting.
*
*  Return value
*    USBH_STATUS_SUCCESS on success.
*    Any other value means error.
*/
USBH_STATUS USBH_GetInterfaceCurrAltSetting(USBH_INTERFACE_HANDLE hInterface, unsigned * pCurAltSetting) {
  USB_INTERFACE * pInterface;

  if (NULL == hInterface) {
    return USBH_STATUS_INVALID_PARAM;
  }
  pInterface = hInterface;
  USBH_ASSERT_MAGIC(pInterface, USB_INTERFACE);
  USBH_LOG((USBH_MCAT_INTF_API, "USBH_GetInterfaceCurrAltSetting: CurrentAlternateSetting: %u!", pInterface->CurrentAlternateSetting));
  *pCurAltSetting = pInterface->CurrentAlternateSetting;
  return USBH_STATUS_SUCCESS;
}


/*********************************************************************
*
*       _NewUsbInterface
*
*  Function description
*    Allocates a USB interface and conducts basic initialization.
*/
static USB_INTERFACE * _NewUsbInterface(USB_DEVICE * pDevice) {
  USB_INTERFACE * pInterface;

  USBH_ASSERT_MAGIC(pDevice, USB_DEVICE);
  pInterface = (USB_INTERFACE *)USBH_TRY_MALLOC_ZEROED(sizeof(USB_INTERFACE));
  if (!pInterface) {
    USBH_WARN((USBH_MCAT_INTF, "_NewUsbInterface: USBH_MALLOC!"));
    return NULL;
  }
  USBH_IFDBG(pInterface->Magic = USB_INTERFACE_MAGIC);
  pInterface->pDevice      = pDevice;
  pInterface->InterfaceId = ++USBH_Global.NextInterfaceId; // Get a new unique interface ID
  return pInterface;
}

/*********************************************************************
*
*       USBH_CompareUsbInterface
*
*  Function description
*    Returns TRUE if the InterfaceMask matches with the
*    current interface settings.
*
*  Return value
*    USBH_STATUS_SUCCESS interface matches
*    other values on error
*/
USBH_STATUS USBH_CompareUsbInterface(const USB_INTERFACE * pInterface, const USBH_INTERFACE_MASK * pInterfaceMask, USBH_BOOL EnableHubInterfaces) {
  U16          Mask;
  USBH_STATUS  Status;
  const U8   * pInfoDesc;
  USB_DEVICE * pDevice;
  int          i;

  USBH_ASSERT_MAGIC(pInterface, USB_INTERFACE);
  pInfoDesc = pInterface->pInterfaceDescriptor;
  pDevice   = pInterface->pDevice;
  if (EnableHubInterfaces == FALSE) {
    if (pDevice->DeviceDescriptor.bDeviceClass == USB_DEVICE_CLASS_HUB || pInfoDesc[USB_INTERFACE_DESC_CLASS_OFS] == USB_DEVICE_CLASS_HUB) {
      return USBH_STATUS_ERROR;
    }
  }
  if (NULL == pInterfaceMask) {
    return USBH_STATUS_SUCCESS;
  }
  Mask = pInterfaceMask->Mask;
  Status = USBH_STATUS_ERROR;
  if ((Mask & USBH_INFO_MASK_VID) != 0u) {
    if (pDevice->DeviceDescriptor.idVendor != pInterfaceMask->VendorId) {
      USBH_LOG((USBH_MCAT_PNP, "USBH_CompareUsbInterface VendorId does not match: 0x%x ", pDevice->DeviceDescriptor.idVendor));
      goto End;
    }
  }
  if ((Mask & USBH_INFO_MASK_PID) != 0u) {
    if (pDevice->DeviceDescriptor.idProduct != pInterfaceMask->ProductId) {
      USBH_LOG((USBH_MCAT_PNP, "USBH_CompareUsbInterface ProductId does not match: 0x%x ", pDevice->DeviceDescriptor.idProduct));
      goto End;
    }
  }
  if ((Mask & USBH_INFO_MASK_DEVICE) != 0u) {
    if (pDevice->DeviceDescriptor.bcdDevice != pInterfaceMask->bcdDevice) {
      USBH_LOG((USBH_MCAT_PNP, "USBH_CompareUsbInterface bcdDevice does not match: 0x%x ", pDevice->DeviceDescriptor.bcdDevice));
      goto End;
    }
  }
  if ((Mask & USBH_INFO_MASK_INTERFACE) != 0u) { // Check the interface number
    if (pInfoDesc[USB_INTERFACE_DESC_NUMBER_OFS] != pInterfaceMask->Interface) {
      USBH_LOG((USBH_MCAT_PNP, "USBH_CompareUsbInterface interface does not match: %u ", pInfoDesc[2]));
      goto End;
    }
  }
  if ((Mask & USBH_INFO_MASK_CLASS) != 0u) {     // Check class subclass and protocol
    if (pInfoDesc[USB_INTERFACE_DESC_CLASS_OFS] != pInterfaceMask->Class) {
      USBH_LOG((USBH_MCAT_PNP, "USBH_CompareUsbInterface class does not match: %u ", pInfoDesc[5]));
      goto End;
    }
  }
  if ((Mask & USBH_INFO_MASK_SUBCLASS) != 0u) {
    if (pInfoDesc[USB_INTERFACE_DESC_SUBCLASS_OFS] != pInterfaceMask->SubClass) {
      USBH_LOG((USBH_MCAT_PNP, "USBH_CompareUsbInterface sub class does not match: %u ", pInfoDesc[6]));
      goto End;
    }
  }
  if ((Mask & USBH_INFO_MASK_PROTOCOL) != 0u) {
    if (pInfoDesc[USB_INTERFACE_DESC_PROTOCOL_OFS] != pInterfaceMask->Protocol) {
      USBH_LOG((USBH_MCAT_PNP, "USBH_CompareUsbInterface protocol does not match: %u ", pInfoDesc[7]));
      goto End;
    }
  }
  if ((Mask & (USBH_INFO_MASK_VID_ARRAY | USBH_INFO_MASK_PID_ARRAY)) != 0u) {
    for (i = pInterfaceMask->NumIds; --i >= 0;) {
      if ((Mask & USBH_INFO_MASK_VID_ARRAY) != 0u && pDevice->DeviceDescriptor.idVendor != pInterfaceMask->pVendorIds[i]) {
        continue;
      }
      if ((Mask & USBH_INFO_MASK_PID_ARRAY) != 0u && pDevice->DeviceDescriptor.idProduct != pInterfaceMask->pProductIds[i]) {
        continue;
      }
      break;
    }
    if (i < 0) {
      USBH_LOG((USBH_MCAT_PNP, "USBH_CompareUsbInterface VendorIds/ProductIds do not match"));
      goto End;
    }
  }
  // On success
  USBH_LOG((USBH_MCAT_PNP, "USBH_CompareUsbInterface: success: VendorId: 0x%x ProductId: 0x%x Class: %u Interface: %u !", pDevice->DeviceDescriptor.idVendor,
                            pDevice->DeviceDescriptor.idProduct, pInfoDesc[USB_INTERFACE_DESC_CLASS_OFS], pInfoDesc[USB_INTERFACE_DESC_NUMBER_OFS]));
  Status = USBH_STATUS_SUCCESS;
End:
  return Status;
}

/*********************************************************************
*
*       USBH_BD_SearchUsbEndpointInInterface
*
*  Function description
*    Returns a pointer to USB_ENDPOINT if the parameter mask matches
*    with one of the endpoints of the interface!
*/
USB_ENDPOINT * USBH_BD_SearchUsbEndpointInInterface(const USB_INTERFACE * pInterface, const USBH_EP_MASK * pMask) {
  USB_ENDPOINT * pEndpoint;
  const U8     * pEPDesc;
  unsigned int   Index        = 0;

  for (pEndpoint = pInterface->pEndpointList; pEndpoint != NULL; pEndpoint = pEndpoint->pNext) {
    USBH_ASSERT_MAGIC(pEndpoint, USB_ENDPOINT);
    pEPDesc      = pEndpoint->pEndpointDescriptor;
    if (NULL != pEPDesc) {
      if ( (((pMask->Mask &USBH_EP_MASK_INDEX)     == 0u) || Index >= pMask->Index) && (((pMask->Mask &USBH_EP_MASK_ADDRESS) == 0u) || pEPDesc[USB_EP_DESC_ADDRESS_OFS] == pMask->Address)
        && (((pMask->Mask &USBH_EP_MASK_TYPE)      == 0u) || (pEPDesc[USB_EP_DESC_ATTRIB_OFS]&USB_EP_DESC_ATTRIB_MASK) == pMask->Type)
        && (((pMask->Mask &USBH_EP_MASK_DIRECTION) == 0u) || (pEPDesc[USB_EP_DESC_ADDRESS_OFS]&USB_EP_DESC_DIR_MASK)   == pMask->Direction)) {
        break;
      }
    }
    Index++;
  }
  return pEndpoint;
}

/*********************************************************************
*
*       USBH_CreateInterfaces
*
*  Function description
*    Create all interfaces and endpoints, create PnP notification
*/
USBH_STATUS USBH_CreateInterfaces(USB_DEVICE * pDev, unsigned InterfaceNo, unsigned AltSetting) {
  USB_INTERFACE * pUsbInterface;
  const U8      * pConfDesc;
  unsigned        ConfDescLen;
  const U8      * pDesc;
  unsigned        DescLen;
  USBH_STATUS     Status;

  USBH_ASSERT_MAGIC(pDev, USB_DEVICE);
  pConfDesc   = pDev->pConfigDescriptor;
  ConfDescLen = pDev->ConfigDescriptorSize;
  pDesc = _FindNextInterfaceDesc(&pConfDesc, &ConfDescLen, -1, &DescLen);
  while (pDesc != NULL) {
    pUsbInterface = _NewUsbInterface(pDev);
    if (pUsbInterface == NULL) {
      USBH_WARN((USBH_MCAT_INTF, "USBH_CreateInterfaces, _NewUsbInterface failed"));
      return USBH_STATUS_MEMORY;
    }
    pUsbInterface->pInterfaceDescriptor     = pDesc;
    pUsbInterface->InterfaceDescriptorSize  = DescLen;
    if (pDesc[USB_INTERFACE_DESC_NUMBER_OFS] == InterfaceNo) {
      pUsbInterface->CurrentAlternateSetting = AltSetting;
    }
    Status                                  = _CreateEndpoints(pUsbInterface); // Create the endpoints
    if (Status != USBH_STATUS_SUCCESS) {
      _DeleteUsbInterface(pUsbInterface);
      return Status;
    }
    _AddUsbInterface(pUsbInterface);                                          // Add the interfaces to the list
    pDesc = _FindNextInterfaceDesc(&pConfDesc, &ConfDescLen, -1, &DescLen);
  }
  USBH_AddUsbDevice(pDev);
  USBH_ProcessDevicePnpNotifications(pDev, USBH_ADD_DEVICE);
  return USBH_STATUS_SUCCESS;
}

/*********************************************************************
*
*       USBH_DeleteInterfaces
*
*  Function description
*    Deletes interfaces associated with a device.
*/
void USBH_DeleteInterfaces(const USB_DEVICE * pDev) {
  USB_INTERFACE * pUsbInterface;
  USBH_DLIST         * e;
  e = USBH_DLIST_GetNext(&pDev->UsbInterfaceList);
  while (e != &pDev->UsbInterfaceList) {
    pUsbInterface = GET_USB_INTERFACE_FROM_ENTRY(e);
    USBH_ASSERT_MAGIC(pUsbInterface, USB_INTERFACE);
    e = USBH_DLIST_GetNext(e);
    _RemoveUsbInterface(pUsbInterface);
    _DeleteUsbInterface(pUsbInterface);
  }
}

/*********************************************************************
*
*       USBH_ResetEndpoint
*
*  Function description
*    Resets an endpoint back to the default state.
*
*  Parameters
*    hIface              : Handle for the USB interface.
*    pUrb                : Pointer to the URB (USB Request Block).
*    Endpoint            : Endpoint number.
*    pfCompletion        : Pointer to a function which will be called
*                          as soon as the request is completed.
*    pContext            : Pointer to the context which will be passed
*                          to the function pointed to by pfCompletion.
*
*  Return value
*    USBH_STATUS_PENDING : The URB has been scheduled, success!
*    Other               : An error occurred.
*/
USBH_STATUS USBH_ResetEndpoint(USBH_INTERFACE_HANDLE hIface, USBH_URB * pUrb, U8 Endpoint, USBH_ON_COMPLETION_FUNC * pfCompletion, void * pContext) {
  USBH_STATUS Status;

  USBH_ASSERT_PTR(pfCompletion); // Completion routine is always needed
  pUrb->Header.pContext                  = pContext;
  pUrb->Header.Function                  = USBH_FUNCTION_RESET_ENDPOINT;
  pUrb->Header.pfOnCompletion            = pfCompletion;
  pUrb->Request.EndpointRequest.Endpoint = Endpoint;
  Status                                 = USBH_SubmitUrb(hIface, pUrb);
  if (Status != USBH_STATUS_PENDING) {
    USBH_WARN((USBH_MCAT_URB, "ERROR ResetEndpoint: USBH_SubmitUrb %s", USBH_GetStatusStr(Status)));
  }
  return Status;
}

/*********************************************************************
*
*       USBH_GetIADInfo
*
*  Function description
*    Obtains information about the corresponding Interface Association Descriptor
*    for an interface ID (if one is available).
*
*  Parameters
*    InterfaceID:    ID of an interface of the device to query.
*    pIADInfo:       Pointer to a caller allocated structure that will
*                    receive the IAD information on success.
*
*  Return value
*    USBH_STATUS_SUCCESS on success.
*    Any other value means error.
*/
USBH_STATUS USBH_GetIADInfo(USBH_INTERFACE_ID InterfaceID, USBH_IAD_INFO * pIADInfo) {
  USB_INTERFACE * pInterface;
  USBH_DLIST    * pEntry;
  USB_DEVICE    * pDev;
  unsigned        ConfDescLen;
  const U8      * pConfDesc;
  const U8      * pIADDesc;
  unsigned        DescLen;
  USBH_STATUS     Status;
  U8              FirstIf;
  U8              IfCount;
  U8              NumIDs;
  U8              IfNum;
  U8              Found;

  USBH_ASSERT(pIADInfo != NULL);
  //
  // Retrieve the interface structure using the interface ID.
  //
  USBH_LOG((USBH_MCAT_INTF_API, "USBH_GetIADInfo: InterfaceID: %u!", InterfaceID));
  pInterface = USBH_GetInterfaceById(InterfaceID);
  if (pInterface == NULL) {
    USBH_WARN((USBH_MCAT_INTF_API, "USBH_GetIADInfo: _GetInterfaceById ID: %u failed!", InterfaceID));
    return USBH_STATUS_DEVICE_REMOVED;
  }
  pDev = pInterface->pDevice;
  USBH_ASSERT_MAGIC(pDev, USB_DEVICE);
  pConfDesc   = pDev->pConfigDescriptor;
  ConfDescLen = pDev->ConfigDescriptorSize;
  //
  // Get the interface number from the structure.
  //
  IfNum       = pInterface->pInterfaceDescriptor[USB_INTERFACE_DESC_NUMBER_OFS];
  Found       = 0;
  //
  // Go through all available IADs and find one matching the interface number.
  //
  pIADDesc = _FindNextIADDesc(&pConfDesc, &ConfDescLen, &DescLen);
  while (pIADDesc != NULL) {
    USBH_ASSERT(pIADDesc[0] == USB_IA_DESCRIPTOR_LENGTH); // IAD must be 8 bytes long.
    FirstIf = pIADDesc[USB_IAD_FIRST_IF_OFS];
    IfCount = pIADDesc[USB_IAD_IF_COUNT_OFS];
    if (IfNum >= FirstIf && IfNum < (FirstIf + IfCount)) {
      Found = 1;
      break;
    }
    pIADDesc = _FindNextIADDesc(&pConfDesc, &ConfDescLen, &DescLen);
  }
  if (Found == 1u) {
    //
    // Find the matching interface IDs using the interface numbers from the IAD.
    //
    NumIDs = 0;
    pEntry = USBH_DLIST_GetNext(&pDev->UsbInterfaceList);
    Status = USBH_STATUS_SUCCESS;
    while (pEntry != &pDev->UsbInterfaceList) {
      pInterface = GET_USB_INTERFACE_FROM_ENTRY(pEntry); // Search in all device interfaces and notify every interface
      USBH_ASSERT_MAGIC(pInterface, USB_INTERFACE);
      IfNum = pInterface->pInterfaceDescriptor[USB_INTERFACE_DESC_NUMBER_OFS];
      if (IfNum >= FirstIf && IfNum < (FirstIf + IfCount)) {
        pIADInfo->aInterfaceIDs[NumIDs] = pInterface->InterfaceId;
        NumIDs++;
      }
      if (NumIDs >= USBH_MAX_INTERFACES_IN_IAD) {
        USBH_WARN((USBH_MCAT_INTF_API, "USBH_GetIADInfo: USBH_MAX_INTERFACES_IN_IAD too low"));
        Status = USBH_STATUS_ERROR;
        break;
      }
      pEntry = USBH_DLIST_GetNext(pEntry);
    }
    if (NumIDs != IfCount) {
      USBH_WARN((USBH_MCAT_INTF_API, "USBH_GetIADInfo: IAD IfCount %d != found interfaces %d", IfCount, NumIDs));
    }
    pIADInfo->NumIDs            = NumIDs;
    pIADInfo->FunctionClass     = pIADDesc[USB_IAD_FUNC_CLASS_OFS];
    pIADInfo->FunctionSubClass  = pIADDesc[USB_IAD_FUNC_SUBCLASS_OFS];
    pIADInfo->FunctionProtocol  = pIADDesc[USB_IAD_FUNC_PROT_OFS];
    pIADInfo->iFunction         = pIADDesc[USB_IAD_STRING_INDEX_OFS];

  } else {
    USBH_WARN((USBH_MCAT_INTF_API, "USBH_GetIADInfo: IAD not found"));
    Status = USBH_STATUS_NOT_FOUND;
  }
  USBH_DEC_REF(pDev);
  return Status;
}

/*************************** End of file ****************************/
