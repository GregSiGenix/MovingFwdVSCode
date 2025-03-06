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
Purpose     : Handling of USB device objects and enumeration.
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
*       Defines
*
**********************************************************************
*/
//
// The default size of the buffer to get descriptors from the device. If the buffer is too small for the configuration descriptor,
// a new buffer is dynamically allocated.
//
#define DEFAULT_TRANSFERBUFFER_SIZE     64u

/*********************************************************************
*
*       _ConvDeviceDesc
*
*  Function description
*    _ConvDeviceDesc convert a received byte aligned buffer to
*    a machine independent structure USBH_DEVICE_DESCRIPTOR
*
*  Parameters
*    pBuffer  : [IN] Device descriptor.
*    pDevDesc : [OUT] Pointer to a empty structure.
*/
static void _ConvDeviceDesc(const U8 * pBuffer, USBH_DEVICE_DESCRIPTOR * pDevDesc) {
  pDevDesc->bLength            = pBuffer[0];
  pDevDesc->bDescriptorType    = pBuffer[1];
  pDevDesc->bcdUSB             = (U16)USBH_LoadU16LE(pBuffer + 0x02);
  pDevDesc->bDeviceClass       = pBuffer[4];
  pDevDesc->bDeviceSubClass    = pBuffer[5];
  pDevDesc->bDeviceProtocol    = pBuffer[6];
  pDevDesc->bMaxPacketSize0    = pBuffer[7];
  pDevDesc->idVendor           = (U16)USBH_LoadU16LE(pBuffer + 0x08);
  pDevDesc->idProduct          = (U16)USBH_LoadU16LE(pBuffer + 0x0a);
  pDevDesc->bcdDevice          = (U16)USBH_LoadU16LE(pBuffer + 0x0c);
  pDevDesc->iManufacturer      = pBuffer[14];
  pDevDesc->iProduct           = pBuffer[15];
  pDevDesc->iSerialNumber      = pBuffer[16];
  pDevDesc->bNumConfigurations = pBuffer[17];
}

/*********************************************************************
*
*       _AbortDeviceEndpoints
*
*  Function description
*    Abort URB's on all related endpoints
*/
static void _AbortDeviceEndpoints(const USB_DEVICE * pDev) {
  USBH_DLIST             * pInterface;
  USB_INTERFACE          * pUsbInterface;
  USB_ENDPOINT           * pEndpoint;
  USBH_HOST_CONTROLLER   * pHostController;

  USBH_ASSERT_MAGIC(pDev, USB_DEVICE);
  pHostController = pDev->pHostController;
  pInterface = USBH_DLIST_GetNext(&pDev->UsbInterfaceList); // For each interface
  while (pInterface != &pDev->UsbInterfaceList) {
    pUsbInterface = GET_USB_INTERFACE_FROM_ENTRY(pInterface);
    USBH_ASSERT_MAGIC(pUsbInterface, USB_INTERFACE);
    pInterface = USBH_DLIST_GetNext(pInterface);
    for (pEndpoint = pUsbInterface->pEndpointList; pEndpoint != NULL; pEndpoint = pEndpoint->pNext) {
      USBH_ASSERT_MAGIC(pEndpoint, USB_ENDPOINT);
      if (pEndpoint->ActiveUrb != 0) {
        (void)USBH_AbortEndpoint(pHostController, pEndpoint->hEP);
      }
    }
  }
  (void)USBH_AbortEndpoint(pHostController, pDev->DefaultEp.hEP);
}

/*********************************************************************
*
*       _EnumPrepareSetConfiguration
*
*  Function description
*    Fills the Set Configuration URB.
*/
static void _EnumPrepareSetConfiguration(USB_DEVICE * pDev) {
  USBH_URB    * pUrb = &pDev->EnumUrb;

  USBH_ZERO_MEMORY(pUrb, sizeof(USBH_URB));
  pUrb->Header.Function                      = USBH_FUNCTION_CONTROL_REQUEST;
  // pUrb->Request.ControlRequest.Setup.Type is 0x00    STD, OUT, device
  pUrb->Request.ControlRequest.Setup.Request = USB_REQ_SET_CONFIGURATION;
  pUrb->Request.ControlRequest.Setup.Value   = pDev->pConfigDescriptor[5];
}

/*********************************************************************
*
*       _CheckDescriptor
*
*  Function description
*    Parse descriptor for a valid structure.
*/
static int _CheckDescriptor(const U8 *pDesc, unsigned DescLen) {
  unsigned Len;

  while (DescLen > 0u) {
    Len = *pDesc;
    if (Len == 0u || Len > DescLen) {
      return 1;
    }
    DescLen -= Len;
    pDesc   += Len;
  }
  return 0;
}

/*********************************************************************
*
*       _InitDefaultEndpoint
*
*  Function description
*    Initializes the embedded default endpoint object in the device
*    and creates an new default endpoint in the host controller driver.
*
*  Parameters
*    pUsbDevice : Pointer to a USB device object.
*
*  Return value
*    USBH_STATUS_SUCCESS : Endpoint was initialized successfully.
*    USBH_STATUS_ERROR   : Initialization failed.
*/
static USBH_STATUS _InitDefaultEndpoint(USB_DEVICE * pUsbDevice) {
  USBH_DEFAULT_EP           * ep;
  USBH_HOST_CONTROLLER      * pHostController;

  USBH_ASSERT_MAGIC(pUsbDevice, USB_DEVICE);
  ep = &pUsbDevice->DefaultEp;
  // After allocation the device is set with zero values
  USBH_ASSERT(ep->hEP == NULL);
  USBH_IFDBG(ep->Magic = USBH_DEFAULT_EP_MAGIC);
  ep->pUsbDevice  = pUsbDevice;
  ep->UrbCount   = 0;
  pHostController = pUsbDevice->pHostController;
  ep->hEP   = pHostController->pDriver->pfAddEndpoint(pHostController->pPrvData, USB_EP_TYPE_CONTROL, pUsbDevice->UsbAddress, 0,
                                                      pUsbDevice->MaxFifoSize, 0, pUsbDevice->DeviceSpeed);
  if (ep->hEP == NULL) {
    USBH_WARN((USBH_MCAT_DEVICE, "Error: _InitDefaultEndpoint: pfAddEndpoint failed"));
    return USBH_STATUS_ERROR;
  }
  return USBH_STATUS_SUCCESS;
}

/*********************************************************************
*
*       _ProcessEnum
*
*  Function description
*    Is called with an unlinked device object, this means this device is not in the host controllers device list.
*    The hub port enumDevice element is also NULL, because the device has a unique USB address so another port
*    reset state machine can run during this device enumeration! If enumeration fails this state machine must
*    delete the device object. Stops on error and disables the parent port.
*/
static void _ProcessEnum(void *pContext) {
  USB_DEVICE     * pEnumDev;
  unsigned         RequestLength;
  USBH_STATUS      Status;
  USBH_URB       * pUrb;
  USBH_HUB_PORT  * pParentPort;
  U8             * pDesc;
  unsigned         DescLen;

  pEnumDev = USBH_CTX2PTR(USB_DEVICE, pContext);
  USBH_ASSERT_MAGIC(pEnumDev, USB_DEVICE);
  Status = USBH_STATUS_DEVICE_REMOVED;
  pParentPort = pEnumDev->pParentPort;
  USBH_ASSERT_MAGIC(pParentPort, USBH_HUB_PORT);
  //
  // Restart the devices enumeration state if host is removed, the port not enabled or the hub does not work
  //
  if (pEnumDev->pHostController->State == HC_REMOVED) { // Root hub removed
    USBH_WARN((USBH_MCAT_DEVICE, "_ProcessEnum: host removed"));
    goto RestartPort;
  } else {
    if ((pParentPort->PortStatus & PORT_STATUS_ENABLED) == 0u) {
      USBH_WARN((USBH_MCAT_DEVICE, "_ProcessEnum: parent port not enabled"));
      goto RestartPort;
    } else {
      if (USBH_Global.pExtHubApi != NULL) {
        if (NULL != pParentPort->pExtHub) {
          if (NULL != pParentPort->pExtHub->pHubDevice) {
            if (pParentPort->pExtHub->pHubDevice->State < DEV_STATE_WORKING) {
              USBH_WARN((USBH_MCAT_DEVICE, "_ProcessEnum: Hub does not work"));
              goto RestartPort;
            }
          }
        }
      }
    }
  }
  USBH_LOG((USBH_MCAT_DEVICE_ENUM, "_ProcessEnum %s", USBH_EnumState2Str(pEnumDev->EnumState)));
  pUrb = &pEnumDev->EnumUrb;
  switch (pEnumDev->EnumState) {                    //lint --e{9077,9042, 9090} D:102[a]
  case DEV_ENUM_START:
    //
    // Request device descriptor
    //
    pEnumDev->EnumState  = DEV_ENUM_GET_DEVICE_DESC;
    RequestLength        = USB_DEVICE_DESCRIPTOR_LENGTH;
    if (pEnumDev->DefaultEp.hEP == NULL) {
      Status = _InitDefaultEndpoint(pEnumDev);
      if (Status != USBH_STATUS_SUCCESS) {
        USBH_WARN((USBH_MCAT_DEVICE, "_ProcessEnum: InitDefaultEndpoint failed"));
        goto StopPort;
      }
    }
    if (USBH_CheckCtrlTransferBuffer(pEnumDev, RequestLength) != 0) {
      Status = USBH_STATUS_MEMORY;
      goto StopPort;
    }
    // Prepare an URB
    USBH_EnumPrepareGetDescReq(&pEnumDev->EnumUrb, USB_DEVICE_DESCRIPTOR_TYPE, 0, 0, RequestLength, pEnumDev->pCtrlTransferBuffer);
    Status = USBH_URB_SubStateSubmitRequest(&pEnumDev->SubState, pUrb, USBH_DEFAULT_SETUP_TIMEOUT, pEnumDev);
    if (Status != USBH_STATUS_PENDING) {
      USBH_WARN((USBH_MCAT_DEVICE, "_ProcessEnum: DEV_ENUM_START USBH_URB_SubStateSubmitRequest failed %s", USBH_GetStatusStr(Status)));
      goto StopPort;
    }
    break;
  case DEV_ENUM_GET_DEVICE_DESC:
    //
    // Check device descriptor response
    //
    if (pUrb->Header.Status != USBH_STATUS_SUCCESS || USB_DEVICE_DESCRIPTOR_LENGTH != pUrb->Request.ControlRequest.Length) {
      USBH_WARN((USBH_MCAT_DEVICE, "_ProcessEnum: DEV_ENUM_GET_DEVICE_DESC failed st:%s, len:%d ", USBH_GetStatusStr(pUrb->Header.Status), pUrb->Request.ControlRequest.Length));
      Status = pUrb->Header.Status;
      goto RestartPort;
    }
    _ConvDeviceDesc(pEnumDev->pCtrlTransferBuffer, &pEnumDev->DeviceDescriptor);                         // Store the device descriptor in a typed format
    //
    // Most devices only have one configuration, to speed this up, and to not
    // allocate 64 byte blocks for a 4 and 2 byte alloc we simply save it to the EnumDev struct.
    //
    pEnumDev->NumConfigurations = pEnumDev->DeviceDescriptor.bNumConfigurations;
    if (pEnumDev->NumConfigurations > 1u) {
      pEnumDev->ppConfigDesc = (U8  **)USBH_TRY_MALLOC_ZEROED(sizeof(U8  *) * pEnumDev->NumConfigurations);
      pEnumDev->paConfigSize = (U16  *)USBH_TRY_MALLOC(sizeof(U16) * pEnumDev->NumConfigurations);
      if (pEnumDev->ppConfigDesc == NULL || pEnumDev->paConfigSize == NULL) {
        Status = USBH_STATUS_MEMORY;
        goto StopPort;
      }
    } else {
      //
      // This handles NumConfigurations == 1 and == 0
      // If we get NumConfigurations == 0 from a device
      // we assume it is a mistake and it has 1 configuration.
      //
      pEnumDev->ppConfigDesc = &pEnumDev->pConfigDesc;
      pEnumDev->paConfigSize = &pEnumDev->ConfigSize;
    }
    //
    // Prepare an URB to read first 9 bytes from configuration descriptor
    //
    USBH_EnumPrepareGetDescReq(&pEnumDev->EnumUrb, USB_CONFIGURATION_DESCRIPTOR_TYPE, pEnumDev->ConfigurationIndex, 0, USB_CONFIGURATION_DESCRIPTOR_LENGTH, pEnumDev->pCtrlTransferBuffer);
    pEnumDev->EnumState = DEV_ENUM_GET_CONFIG_DESC_PART;
    Status = USBH_URB_SubStateSubmitRequest(&pEnumDev->SubState, pUrb, USBH_DEFAULT_SETUP_TIMEOUT, pEnumDev);
    if (Status != USBH_STATUS_PENDING) {
      USBH_WARN((USBH_MCAT_DEVICE, "_ProcessEnum: DEV_ENUM_GET_DEVICE_DESC USBH_URB_SubStateSubmitRequest failed %s", USBH_GetStatusStr(Status)));
      goto StopPort;
    }
    break;
  case DEV_ENUM_GET_CONFIG_DESC_PART:
    //
    // Check header of configuration descriptor
    //
    if (pUrb->Header.Status != USBH_STATUS_SUCCESS || pUrb->Request.ControlRequest.Length != USB_CONFIGURATION_DESCRIPTOR_LENGTH) {
      USBH_WARN((USBH_MCAT_DEVICE, "_ProcessEnum: DEV_ENUM_GET_CONFIG_DESC_PART failed st:%s, len:%d ", USBH_GetStatusStr(pUrb->Header.Status), pUrb->Request.ControlRequest.Length));
      Status = pUrb->Header.Status;
      goto RestartPort;
    }
    RequestLength = (U16)USBH_LoadU16LE(pEnumDev->pCtrlTransferBuffer + 2);
    if (RequestLength < USB_CONFIGURATION_DESCRIPTOR_LENGTH) {
      USBH_WARN((USBH_MCAT_DEVICE, "_ProcessEnum: DEV_ENUM_GET_CONFIG_DESC_PART bad descriptor length"));
      Status = USBH_STATUS_INVALID_DESCRIPTOR;
      goto RestartPort;
    }
    if (USBH_CheckCtrlTransferBuffer(pEnumDev, RequestLength) != 0) {
      Status = USBH_STATUS_MEMORY;
      goto StopPort;
    }
    //
    // Prepare an URB to read the hole configuration descriptor
    //
    pEnumDev->paConfigSize[pEnumDev->ConfigurationIndex] = RequestLength;
    USBH_EnumPrepareGetDescReq(&pEnumDev->EnumUrb, USB_CONFIGURATION_DESCRIPTOR_TYPE, pEnumDev->ConfigurationIndex, 0, RequestLength, pEnumDev->pCtrlTransferBuffer);
    pEnumDev->EnumState = DEV_ENUM_GET_CONFIG_DESC;
    Status = USBH_URB_SubStateSubmitRequest(&pEnumDev->SubState, pUrb, USBH_DEFAULT_SETUP_TIMEOUT, pEnumDev);
    if (Status != USBH_STATUS_PENDING) {
      USBH_WARN((USBH_MCAT_DEVICE, "_ProcessEnum: DEV_ENUM_GET_CONFIG_DESC_PART SubmitRequest failed %s", USBH_GetStatusStr(Status)));
      goto StopPort;
    }
    break;
  case DEV_ENUM_GET_CONFIG_DESC:
    //
    // Check complete configuration descriptor
    //
    DescLen = pEnumDev->paConfigSize[pEnumDev->ConfigurationIndex];
    if (pUrb->Header.Status != USBH_STATUS_SUCCESS || pUrb->Request.ControlRequest.Length != DescLen) {
      USBH_WARN((USBH_MCAT_DEVICE, "_ProcessEnum: DEV_ENUM_GET_CONFIG_DESC failed st:%s, Expected:%d bytes Received:%d bytes", USBH_GetStatusStr(pUrb->Header.Status), DescLen, pUrb->Request.ControlRequest.Length));
      Status = pUrb->Header.Status;
      goto RestartPort;
    }
    pDesc = (U8 *)USBH_TRY_MALLOC(pUrb->Request.ControlRequest.Length);
    if (pDesc == NULL) {
      Status = USBH_STATUS_MEMORY;
      goto StopPort;
    }
    pEnumDev->ppConfigDesc[pEnumDev->ConfigurationIndex] = pDesc;
    USBH_MEMCPY(pDesc, pEnumDev->pCtrlTransferBuffer, DescLen);
    if (_CheckDescriptor(pDesc, DescLen) != 0) {
      USBH_WARN((USBH_MCAT_DEVICE, "_ProcessEnum: DEV_ENUM_GET_CONFIG_DESC bad descriptor received"));
      Status = USBH_STATUS_INVALID_DESCRIPTOR;
      goto RestartPort;
    }
    //
    // Are there more configurations in the device ?
    //
    if (pEnumDev->ConfigurationIndex < pEnumDev->DeviceDescriptor.bNumConfigurations - 1u) {
      //
      // Read next configuration.
      //
      pEnumDev->ConfigurationIndex++;
      USBH_EnumPrepareGetDescReq(&pEnumDev->EnumUrb, USB_CONFIGURATION_DESCRIPTOR_TYPE, pEnumDev->ConfigurationIndex, 0, USB_CONFIGURATION_DESCRIPTOR_LENGTH, pEnumDev->pCtrlTransferBuffer);
      pEnumDev->EnumState = DEV_ENUM_GET_CONFIG_DESC_PART;
      Status = USBH_URB_SubStateSubmitRequest(&pEnumDev->SubState, pUrb, USBH_DEFAULT_SETUP_TIMEOUT, pEnumDev);
      if (Status != USBH_STATUS_PENDING) {
        USBH_WARN((USBH_MCAT_DEVICE, "_ProcessEnum: DEV_ENUM_GET_DEVICE_DESC USBH_URB_SubStateSubmitRequest failed %s", USBH_GetStatusStr(Status)));
        goto StopPort;
      }
      break;
    }
    //
    // Prepare an URB for the language ID
    //
    if (USBH_CheckCtrlTransferBuffer(pEnumDev, 256) != 0) {
      Status = USBH_STATUS_MEMORY;
      goto StopPort;
    }
    USBH_EnumPrepareGetDescReq(&pEnumDev->EnumUrb, USB_STRING_DESCRIPTOR_TYPE, 0, 0, 255, pEnumDev->pCtrlTransferBuffer);
    pEnumDev->EnumState = DEV_ENUM_GET_LANG_ID;
    Status = USBH_URB_SubStateSubmitRequest(&pEnumDev->SubState, pUrb, USBH_DEFAULT_SETUP_TIMEOUT, pEnumDev);
    if (Status != USBH_STATUS_PENDING) {
      USBH_WARN((USBH_MCAT_DEVICE, "_ProcessEnum: DEV_ENUM_GET_CONFIG_DESC_PART USBH_URB_SubStateSubmitRequest failed %s", USBH_GetStatusStr(Status)));
      goto StopPort;
    }
    break;
  case DEV_ENUM_GET_LANG_ID:
    //
    // Check language ID response
    //
    if (pUrb->Header.Status != USBH_STATUS_SUCCESS || pUrb->Request.ControlRequest.Length < 4u) {
      USBH_WARN((USBH_MCAT_DEVICE, "_ProcessEnum: DEV_ENUM_GET_LANG_ID failed st:%s, len:%d ", USBH_GetStatusStr(pUrb->Header.Status), pUrb->Request.ControlRequest.Length));
    } else {
      pEnumDev->LanguageId = (U16)USBH_LoadU16LE(pEnumDev->pCtrlTransferBuffer + 2);
    }
    //
    // The language ID is now 0 or the first ID reported by the device
    //
    if (pEnumDev->DeviceDescriptor.iSerialNumber == 0u) {
      //
      // Device don't has a serial number: Skip reading of serial number.
      //
      pEnumDev->EnumState = DEV_ENUM_PREP_SET_CONFIG;
      USBH_URB_SubStateWait(&pEnumDev->SubState, 1, NULL);
      break;
    }
    //
    // Prepare an URB to read the serial number
    //
    USBH_EnumPrepareGetDescReq(&pEnumDev->EnumUrb, USB_STRING_DESCRIPTOR_TYPE, pEnumDev->DeviceDescriptor.iSerialNumber, pEnumDev->LanguageId, 255, pEnumDev->pCtrlTransferBuffer);
    pEnumDev->EnumState = DEV_ENUM_GET_SERIAL_DESC;
    Status = USBH_URB_SubStateSubmitRequest(&pEnumDev->SubState, pUrb, USBH_DEFAULT_SETUP_TIMEOUT, pEnumDev);
    if (Status != USBH_STATUS_PENDING) {
      USBH_WARN((USBH_MCAT_DEVICE, "_ProcessEnum: DEV_ENUM_GET_LANG_ID USBH_URB_SubStateSubmitRequest failed %s", USBH_GetStatusStr(Status)));
      goto StopPort;
    }
    break;
  case DEV_ENUM_GET_SERIAL_DESC:
    //
    // Check serial number response
    //
    if (pUrb->Header.Status != USBH_STATUS_SUCCESS) {
      USBH_WARN((USBH_MCAT_DEVICE, "_ProcessEnum: DEV_ENUM_GET_SERIAL_DESC failed st:%s", USBH_GetStatusStr(pUrb->Header.Status)));
      goto RestartPort;
    } else {
      if (pUrb->Request.ControlRequest.Length > 2u) {
        pEnumDev->SerialNumberSize = pUrb->Request.ControlRequest.Length - 2u; // Don't copy the header
        pEnumDev->pSerialNumber    = (U8 *)USBH_TRY_MALLOC(pEnumDev->SerialNumberSize);
        if (pEnumDev->pSerialNumber == NULL) {
          USBH_WARN((USBH_MCAT_DEVICE, "_ProcessEnum: USBH_MALLOC %d failed", pEnumDev->SerialNumberSize));
          Status = USBH_STATUS_MEMORY;
          goto StopPort;
        }
        USBH_MEMCPY(pEnumDev->pSerialNumber, pEnumDev->pCtrlTransferBuffer + 2, pEnumDev->SerialNumberSize);
      }
    }
    //lint -fallthrough
    //lint -e{9090} D:102[b]
  case DEV_ENUM_PREP_SET_CONFIG:
    //
    // Prepare an URB to set the configuration
    //
    pEnumDev->ConfigurationIndex = 0;
    if (pEnumDev->NumConfigurations > 1u) {
      USBH_SET_CONF_HOOK * pHook;
      USBH_STATUS StatusHook;
      U8          ConfigIndex;

      pHook = USBH_Global.pFirstOnSetConfHook;
      ConfigIndex = 0;
      while (pHook != NULL) {
        if (pHook->pfOnSetConfig != NULL) {
          StatusHook = pHook->pfOnSetConfig(pHook->pContext, &pEnumDev->DeviceDescriptor, (const U8 * const *)pEnumDev->ppConfigDesc, pEnumDev->NumConfigurations, &ConfigIndex); //lint !e9087  N:100
          if (StatusHook == USBH_STATUS_SUCCESS) {
            break;
          }
        }
        pHook = pHook->pNext;
      }
      if (ConfigIndex >= pEnumDev->NumConfigurations) {
        Status = USBH_STATUS_INVALID_PARAM;
        USBH_WARN((USBH_MCAT_DEVICE, "Wrong configuration index (%d) was selected for device(%d): Halting USB port", ConfigIndex, pEnumDev->DeviceId));
        goto StopPort;
      }
      pEnumDev->ConfigurationIndex = ConfigIndex;
    }
    pEnumDev->pConfigDescriptor    = pEnumDev->ppConfigDesc[pEnumDev->ConfigurationIndex];
    pEnumDev->ConfigDescriptorSize = pEnumDev->paConfigSize[pEnumDev->ConfigurationIndex];
    USBH_ASSERT(pEnumDev->pConfigDescriptor != NULL);
    _EnumPrepareSetConfiguration(pEnumDev);
    Status = USBH_URB_SubStateSubmitRequest(&pEnumDev->SubState, pUrb, USBH_DEFAULT_SETUP_TIMEOUT, pEnumDev);
    if (Status != USBH_STATUS_PENDING) {
      USBH_WARN((USBH_MCAT_DEVICE, "_ProcessEnum: USBH_URB_SubStateSubmitRequest failed %s", USBH_GetStatusStr(Status)));
      goto StopPort;
    }
    pEnumDev->EnumState = DEV_ENUM_SET_CONFIGURATION;
    break;
  case DEV_ENUM_SET_CONFIGURATION:
    //
    // Check 'set configuration' response
    //
    if (pUrb->Header.Status != USBH_STATUS_SUCCESS) {
      USBH_WARN((USBH_MCAT_DEVICE, "_ProcessEnum: DEV_ENUM_SET_CONFIGURATION failed st:%s", USBH_GetStatusStr(pUrb->Header.Status)));
      Status = pUrb->Header.Status;
      goto RestartPort;
    }
    if (pEnumDev->DeviceDescriptor.bDeviceClass == USB_DEVICE_CLASS_HUB) {
      if (USBH_Global.pExtHubApi != NULL) {
        USBH_Global.pExtHubApi->pfStartHub(pEnumDev);
        goto FinishEnum;
      } else {
        USBH_WARN((USBH_MCAT_DEVICE, "_ProcessEnum: Hub connected, but hub support not enabled!"));
      }
    }
    pEnumDev->EnumState = DEV_ENUM_IDLE;
    Status = USBH_CreateInterfaces(pEnumDev, 0, 0);     // Add new device to DeviceList
    if (Status != USBH_STATUS_SUCCESS) {
      goto StopPort;
    }
    //
    // CtrlTransferBuffer is not needed any more
    //
    USBH_FREE(pEnumDev->pCtrlTransferBuffer);
    pEnumDev->CtrlTransferBufferSize = 0;
    pEnumDev->pCtrlTransferBuffer    = NULL;
    //
    // Configure EPs if necessary
    //
    if (pEnumDev->pHostController->Caps.NeedConfigureEPs == 0) {
      goto FinishEnum;
    }
    pUrb->Header.Function = USBH_FUNCTION_CONFIGURE_EPS;
    Status = USBH_URB_SubStateSubmitRequest(&pEnumDev->SubState, pUrb, USBH_DEFAULT_SETUP_TIMEOUT, pEnumDev);
    if (Status != USBH_STATUS_PENDING) {
      USBH_WARN((USBH_MCAT_DEVICE, "_ProcessEnum: USBH_URB_SubStateSubmitRequest failed %s", USBH_GetStatusStr(Status)));
      goto StopPort;
    }
    pEnumDev->EnumState = DEV_ENUM_CONFIGURE_EPS;
    break;
  case DEV_ENUM_CONFIGURE_EPS:
    //
    // Check 'configure EPs' response
    //
    if (pUrb->Header.Status != USBH_STATUS_SUCCESS) {
      USBH_WARN((USBH_MCAT_DEVICE, "_ProcessEnum: DEV_ENUM_CONFIGURE_EPS failed st:%s", USBH_GetStatusStr(pUrb->Header.Status)));
      Status = pUrb->Header.Status;
      goto StopPort;
    }
    goto FinishEnum;
  default:
    USBH_ASSERT0;
    break;
  } // Switch
  return;
FinishEnum:
  USBH_HC_DEC_REF(pEnumDev->pHostController);     // Reset ref from USBH_StartEnumeration()
  pEnumDev->pParentPort->DeviceEnumActive = 0;
  USBH_ReleaseActiveEnumeration(pEnumDev->pHostController);
  USBH_LOG((USBH_MCAT_DEVICE, "_ProcessEnum: Enumeration successful"));
  return;
RestartPort:
  USBH_ReleaseActiveEnumeration(pEnumDev->pHostController);
  USBH_ProcessEnumError(pEnumDev, Status, TRUE);
  return;
StopPort:
  USBH_ReleaseActiveEnumeration(pEnumDev->pHostController);
  USBH_ProcessEnumError(pEnumDev, Status, FALSE);
}

/*********************************************************************
*
*       _ReleaseDefaultEndpoint
*
*  Function description
*    Removes the default endpoint for the host controller.
*
*  Parameters
*    pUsbEndpoint Pointer to the default endpoint object.
*/
static void _ReleaseDefaultEndpoint(USBH_DEFAULT_EP * pUsbEndpoint) {
  USBH_HOST_CONTROLLER * pHostController = pUsbEndpoint->pUsbDevice->pHostController;

  USBH_ASSERT_MAGIC(pHostController, USBH_HOST_CONTROLLER);
  // An URB must have a reference and the device must not be deleted if the URB has the reference.
  USBH_ASSERT(pUsbEndpoint->UrbCount == 0);
  USBH_LOG((USBH_MCAT_DEVICE_ENUM, "_ReleaseDefaultEndpoint: urbcount: %u", pUsbEndpoint->UrbCount));
  if (pUsbEndpoint->hEP != NULL) {
    USBH_HC_INC_REF(pHostController);
    pHostController->pDriver->pfReleaseEndpoint(pUsbEndpoint->hEP, USBH_DefaultReleaseEpCompletion, pHostController);
  }
  pUsbEndpoint->hEP = NULL;
}

/*********************************************************************
*
*       _DecRef
*
*  Function description
*    Decrement reference count of a device object.
*/
static int _DecRef(USB_DEVICE * pDevice
#if USBH_DEBUG > 1
                        , const char *pFile, int Line
#endif
                        ) {
  int RefCount;

  USBH_OS_Lock(USBH_MUTEX_DEVICE);
  RefCount = pDevice->RefCount - 1;
  if (RefCount >= 0) {
    pDevice->RefCount = RefCount;
  }
  USBH_OS_Unlock(USBH_MUTEX_DEVICE);
  if (RefCount < 0) {
    USBH_WARN((USBH_MCAT_DEVICE_REF, "Core: [UID%d, USBAddr%d] DEC_REF RefCount UNDERFLOW %s(%d)",
                                     pDevice->UniqueID, pDevice->UsbAddress, pFile, Line));
  } else {
    USBH_LOG((USBH_MCAT_DEVICE_REF, "Core: [UID%d, USBAddr%d] DEC_REF RefCount is %d %s(%d)",
                                    pDevice->UniqueID, pDevice->UsbAddress, pDevice->RefCount, pFile, Line));
  }
  return RefCount;
}

/*********************************************************************
*
*       _OnSubmitUrbCompletion
*
*  Function description
*/
static void _OnSubmitUrbCompletion(USBH_URB * pUrb) USBH_CALLBACK_USE {
  USBH_OS_EVENT_OBJ * pEvent;

  pEvent = USBH_CTX2PTR(USBH_OS_EVENT_OBJ, pUrb->Header.pContext);
  USBH_OS_SetEvent(pEvent);
}

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       USBH_EnumPrepareGetDescReq
*
*  Function description
*    Fills a URB structure with the given values.
*/
void USBH_EnumPrepareGetDescReq(USBH_URB * pUrb, U8 DescType, U8 DescIndex, U16 LanguageID, U16 RequestLength, void * pBuffer) {
  USBH_ZERO_MEMORY(pUrb, sizeof(USBH_URB));
  pUrb->Header.Function                      = USBH_FUNCTION_CONTROL_REQUEST;
  pUrb->Request.ControlRequest.Setup.Type    = 0x80; // STD, IN, device
  pUrb->Request.ControlRequest.Setup.Request = USB_REQ_GET_DESCRIPTOR;
  pUrb->Request.ControlRequest.Setup.Value   = (((U16)DescType << 8) | DescIndex);
  pUrb->Request.ControlRequest.Setup.Index   = LanguageID;
  pUrb->Request.ControlRequest.Setup.Length  = RequestLength;
  pUrb->Request.ControlRequest.pBuffer       = pBuffer;
}

/*********************************************************************
*
*       USBH_CreateNewUsbDevice
*
*  Function description
*    Allocates device object and makes an basic initialization. Set the reference counter to one. Set the pHostController pointer.
*    Initialize all dlists and needed IDs. In the default endpoint the URB list is initialized and a pointer to this object is set.
*/
USB_DEVICE * USBH_CreateNewUsbDevice(USBH_HOST_CONTROLLER * pHostController) {
  USB_DEVICE * pDev;

  USBH_ASSERT_MAGIC(pHostController, USBH_HOST_CONTROLLER);
  USBH_LOG((USBH_MCAT_DEVICE_ENUM, "USBH_CreateNewUsbDevice!"));
  pDev = (USB_DEVICE *)USBH_TRY_MALLOC_ZEROED(sizeof(USB_DEVICE));
  if (NULL == pDev) {
    USBH_WARN((USBH_MCAT_DEVICE, "USBH_CreateNewUsbDevice: USBH_MALLOC!"));
    return NULL;
  }
#if USBH_DEBUG > 1
  pDev->UniqueID = USBH_Global.DevUniqueID;
  USBH_Global.DevUniqueID++;
#endif
  USBH_IFDBG(pDev->Magic = USB_DEVICE_MAGIC);
  pDev->pHostController = pHostController;
  USBH_DLIST_Init(&pDev->UsbInterfaceList);
  pDev->DeviceId = ++USBH_Global.NextDeviceId;
  pDev->RefCount = 1;               // Initial refcount
  // The sub state machine increments the reference count of the device before submitting the request
  USBH_URB_SubStateInit(&pDev->SubState, pHostController, &pDev->DefaultEp.hEP, _ProcessEnum, pDev);
  pDev->EnumState = DEV_ENUM_IDLE; // default basic initialization
  pDev->DefaultEp.pUsbDevice = pDev;
  return pDev;
}

/*********************************************************************
*
*       USBH_MarkDeviceAsRemoved
*
*  Function description
*    Marks a device as removed.
*    Send an abort-URB to all endpoints of the device and removes
*    the device from the host controller's device list.
*/
void USBH_MarkDeviceAsRemoved(USB_DEVICE * pDev) {
  USBH_NOTIFICATION          * pNotification;
  USBH_DLIST                 * pNotificationList;
  USBH_DLIST                 * pEntry;
  USB_DEV_STATE                DevState;

  USBH_ASSERT_MAGIC(pDev, USB_DEVICE);
  USBH_LOG((USBH_MCAT_DEVICE, "MarkDeviceAsRemoved pDev-addr: %u!", pDev->UsbAddress));
  USBH_OS_Lock(USBH_MUTEX_DEVICE);
  DevState    = pDev->State;
  pDev->State = DEV_STATE_REMOVED;        // Mark device as removed.
  USBH_OS_Unlock(USBH_MUTEX_DEVICE);
  if (DevState == DEV_STATE_REMOVED) {
    USBH_WARN((USBH_MCAT_DEVICE, "USBH_MarkDeviceAsRemoved pDev-addr: %u already removed!", pDev->UsbAddress));
    return;
  }
  //
  // Notify all callbacks.
  //
  pNotificationList = &USBH_Global.DeviceRemovalNotificationList;
  pEntry = USBH_DLIST_GetNext(pNotificationList);
  while (pEntry != pNotificationList) {
    pNotification = GET_NOTIFICATION_FROM_ENTRY(pEntry);
    USBH_ASSERT_MAGIC(pNotification, USBH_DEV_REM_NOTIFICATION);
    pNotification->Notification.DevRem.pDevRemNotification(pDev);
    pEntry = USBH_DLIST_GetNext(pEntry);
  }
  USBH_ProcessDevicePnpNotifications(pDev, USBH_REMOVE_DEVICE);
  //
  // Abort all endpoints
  //
  _AbortDeviceEndpoints(pDev);
  USBH_ASSERT_MAGIC(pDev->pParentPort, USBH_HUB_PORT);
  pDev->pParentPort->pDevice = NULL;  // Delete the link between the hub port and the device in both directions
  USBH_DEC_REF(pDev);
}

/*********************************************************************
*
*       USBH_MarkParentAndChildDevicesAsRemoved
*
*  Function description
*    Marks the device and all child devices if the device is an hub
*    as removed. If an device already removed then nothing is done.
*/
void USBH_MarkParentAndChildDevicesAsRemoved(USB_DEVICE * pUsbDevice) {
  USBH_ASSERT_MAGIC(pUsbDevice, USB_DEVICE);
  USBH_LOG((USBH_MCAT_DEVICE_ENUM, "USBH_MarkParentAndChildDevicesAsRemoved pDev-addr: %u!", pUsbDevice->UsbAddress));
  //
  // Mark the device as removed.
  //
  USBH_MarkDeviceAsRemoved(pUsbDevice);
  //
  // Mark all children as removed.
  //
  if (USBH_Global.pExtHubApi != NULL) {
    USBH_Global.pExtHubApi->pfMarkChildDevicesAsRemoved(pUsbDevice->pHostController);
  }
}

/*********************************************************************
*
*       USBH_DeleteDevice
*
*  Function description
*    Removes a device.
*    Releases all resources associated with the device.
*/
void USBH_DeleteDevice(USB_DEVICE * pDev) {
  unsigned iConfiguration;

  USBH_LOG((USBH_MCAT_DEVICE, "USBH_DeleteDevice pDev-addr: %u!", pDev->UsbAddress));
  USBH_URB_SubStateExit(&pDev->SubState);
  USBH_IFDBG(pDev->Magic = 0);
  if (NULL != pDev->pHubDevice) {
#if USBH_DEBUG > 1
    (void)_DecRef(pDev->pHubDevice, __FILE__, __LINE__);
#else
    (void)_DecRef(pDev->pHubDevice);
#endif
  }
  if (NULL != pDev->pUsbHub) {
    USBH_Global.pExtHubApi->pfDeleteHub(pDev->pUsbHub);
  }
  USBH_DeleteInterfaces(pDev);                   // Delete all interfaces, endpoints and notify the application of a remove event
  _ReleaseDefaultEndpoint(&pDev->DefaultEp);     // Release the default endpoint if any
  if (pDev->pCtrlTransferBuffer != NULL) {
    USBH_FREE(pDev->pCtrlTransferBuffer);
  }
  for (iConfiguration = 0; iConfiguration < pDev->NumConfigurations; iConfiguration++) {
    if (pDev->ppConfigDesc[iConfiguration] != NULL) {
      USBH_FREE(pDev->ppConfigDesc[iConfiguration]);
    }
  }
  if (pDev->ppConfigDesc != NULL && pDev->ppConfigDesc != &pDev->pConfigDesc) {
    USBH_FREE(pDev->ppConfigDesc);
  }
  if (pDev->paConfigSize != NULL && pDev->paConfigSize != &pDev->ConfigSize) {
    USBH_FREE(pDev->paConfigSize);
  }
  if (pDev->pSerialNumber != NULL) {
    USBH_FREE(pDev->pSerialNumber);
  }
  USBH_FreeUsbAddress(pDev->pHostController, pDev->UsbAddress);
  USBH_FREE(pDev);
}

/*********************************************************************
*
*       USBH_CheckCtrlTransferBuffer
*
*  Function description
*    Checks if a request fits into the transfer buffer, allocates a new
*    transfer buffer if it does not.
*/
int USBH_CheckCtrlTransferBuffer(USB_DEVICE * pDev, unsigned RequestLength) {
  unsigned Tmp;

  //
  // Round up to multiple of PacketSize
  //
  if (pDev->MaxFifoSize > 0u) {
    Tmp = RequestLength & ((unsigned)pDev->MaxFifoSize - 1u);
    if (Tmp != 0u) {
      RequestLength += pDev->MaxFifoSize - Tmp;
    }
  }
  if (pDev->CtrlTransferBufferSize < RequestLength) {
    if (pDev->pCtrlTransferBuffer != NULL) {
      USBH_FREE(pDev->pCtrlTransferBuffer);
    }
    // Allocate a new buffer
    pDev->CtrlTransferBufferSize = USBH_MAX(DEFAULT_TRANSFERBUFFER_SIZE, RequestLength);
    pDev->pCtrlTransferBuffer    = (U8 *)USBH_TRY_MALLOC(pDev->CtrlTransferBufferSize);
    if (pDev->pCtrlTransferBuffer == NULL) {
      pDev->CtrlTransferBufferSize = 0;
      USBH_WARN((USBH_MCAT_DEVICE, "USBH_CheckCtrlTransferBuffer: No memory"));
      return 1;
    }
  }
  return 0;
}

/*********************************************************************
*
*       USBH_ProcessEnumError
*
*  Function description
*    On error during enumeration the parent port is disabled and the enumeration device is deleted.
*    By calling USBH_HC_ServicePorts(), the underlying HUB may start a retry for that port (if bRetry == TRUE).
*/
void USBH_ProcessEnumError(USB_DEVICE * pDev, USBH_STATUS Status, USBH_BOOL bRetry) {
  USBH_HUB_PORT        * pParentPort;
  USBH_HOST_CONTROLLER * pHostController;
  unsigned               Flags;

  USBH_ASSERT_MAGIC(pDev, USB_DEVICE);
  pParentPort = pDev->pParentPort;
  USBH_ASSERT_MAGIC(pParentPort, USBH_HUB_PORT);
  USBH_WARN((USBH_MCAT_DEVICE, "EnumPortError: portnumber: %u portstate: 0x%X = %s", pParentPort->HubPortNumber,
                               pParentPort->PortStatus, USBH_PortStatus2Str(pParentPort->PortStatus)));
  if ((pParentPort->PortStatus & PORT_STATUS_ENABLED) != 0u) {
    //
    // Disable the parent port
    //
    if (NULL != pParentPort->pRootHub) {
      const USBH_HOST_DRIVER * pDriver;

      pDriver = pDev->pHostController->pDriver;
      pDriver->pfDisablePort(pDev->pHostController->pPrvData, pParentPort->HubPortNumber);
    } else {
      if (USBH_Global.pExtHubApi != NULL)  {  // Parent hub port is an external port
        USBH_Global.pExtHubApi->pfDisablePort(pParentPort);
      }
    }
  }
  if (bRetry != FALSE) {
    Flags = USBH_ENUM_ERROR_RETRY_FLAG;
  } else {
    pParentPort->RetryCounter = USBH_RESET_RETRY_COUNTER;
    Flags = USBH_ENUM_ERROR_STOP_ENUM_FLAG;
  }
  pParentPort->pDevice = NULL;
  //
  // Notify the user
  //
  if ((pParentPort->PortStatus & PORT_STATUS_CONNECT) == 0u) {
    Flags |= USBH_ENUM_ERROR_DISCONNECT_FLAG;
  }
  if (pDev->pUsbHub != NULL) {
    Flags |= USBH_ENUM_ERROR_INIT_HUB;
  } else {
    Flags |= USBH_ENUM_ERROR_INIT_DEVICE;
  }
  USBH_SetEnumErrorNotification(Flags, Status, (int)pDev->EnumState, pParentPort->HubPortNumber);
  pHostController = pDev->pHostController;
  pDev->pParentPort->DeviceEnumActive = 0;
  USBH_DEC_REF(pDev);                      // Delete the initial reference
  USBH_HC_ServicePorts(pHostController);   // Service all ports
  USBH_HC_DEC_REF(pHostController);        // Reset ref from USBH_StartEnumeration()
}

/*********************************************************************
*
*       USBH_StartEnumeration
*/
void USBH_StartEnumeration(USB_DEVICE * pDev) {
  USBH_ASSERT(pDev->EnumState == DEV_ENUM_IDLE);
  USBH_LOG((USBH_MCAT_DEVICE, "Device Notification:  USBH_StartEnumeration!"));
  pDev->EnumState              = DEV_ENUM_START;
  pDev->State                  = DEV_STATE_ENUMERATE;
  pDev->pParentPort->DeviceEnumActive = 1;
  USBH_HC_INC_REF(pDev->pHostController);
  USBH_ClaimActiveEnumeration(pDev->pHostController);
  USBH_URB_SubStateWait(&pDev->SubState, 1, NULL);
}

/*********************************************************************
*
*       USBH_FindNextEndpointDesc
*
*  Function description
*    Returns NULL or the pointer of the descriptor.
*
*  Parameters
*    ppDesc           : Pointer to pointer to the descriptor data to be parsed.
*                       Will be advanced by the function for repeated calls to this function.
*    pDescLen         : Pointer to the length of the descriptor data to be parsed.
*                       Will be decremented while parsing.
*
*  Return value
*    Pointer to the endpoint descriptor found. NULL if not found.
*/
const U8 * USBH_FindNextEndpointDesc(const U8 ** ppDesc, unsigned * pDescLen) {
  const U8 * pDesc;
  int        DescLen;
  const U8 * pRet;
  const U8 * p;

  pDesc   = *ppDesc;
  DescLen = (int)*pDescLen;
  pRet    = NULL;
  while (DescLen > 0) {
    p        = pDesc;
    DescLen -= (int)*pDesc;
    pDesc   += *pDesc;
    if (p[1] == USB_ENDPOINT_DESCRIPTOR_TYPE) {
      pRet = p;
      break;
    }
  }
  if (pRet != NULL) {
    *ppDesc   = pDesc;
    *pDescLen = (unsigned)DescLen;
  }
  return pRet;
}

/*********************************************************************
*
*       USBH_GetDeviceDescriptor
*
*  Function description
*    Obsolete function, use USBH_GetDeviceDescriptorPtr().
*    Retrieves the current device descriptor of the device containing the given interface.
*
*  Parameters
*    hInterface:     Valid handle to an interface, returned by USBH_OpenInterface().
*    pDescriptor:    Pointer to a buffer where the descriptor is stored.
*    pBufferSize:    * [IN]  Size of the buffer pointed to by pDescriptor.
*                    * [OUT] Number of bytes copied into the buffer.
*
*  Return value
*    USBH_STATUS_SUCCESS on success. Other values indicate an error.
*
*  Additional information
*    The function returns a copy of the current device descriptor, that
*    was stored during the device enumeration. If the given buffer size is too small
*    the device descriptor returned is truncated.
*/
USBH_STATUS USBH_GetDeviceDescriptor(USBH_INTERFACE_HANDLE hInterface, U8 * pDescriptor, unsigned * pBufferSize) {
  USB_INTERFACE * pUsbInterface;
  USB_DEVICE    * pDev;
  USBH_STATUS     Status = USBH_STATUS_SUCCESS;

  USBH_LOG((USBH_MCAT_INTF_API, "USBH_GetDeviceDescriptor"));
  pUsbInterface = hInterface;
  USBH_ASSERT_MAGIC(pUsbInterface, USB_INTERFACE);
  pDev          = pUsbInterface->pDevice;
  USBH_ASSERT_MAGIC(pDev, USB_DEVICE);
  if (pDev->State < DEV_STATE_WORKING) {
    return USBH_STATUS_DEVICE_REMOVED;
  }
  *pBufferSize = USBH_MIN(*pBufferSize, USB_DEVICE_DESCRIPTOR_LENGTH);
  {
    USBH_DEVICE_DESCRIPTOR * pDesc = &pDev->DeviceDescriptor;
    U8              aDeviceDesc[18];
    unsigned        i = 0;

    aDeviceDesc[i++] = pDesc->bLength;
    aDeviceDesc[i++] = pDesc->bDescriptorType;
    aDeviceDesc[i++] = pDesc->bcdUSB   & 0xFFu;
    aDeviceDesc[i++] = pDesc->bcdUSB   >> 8;
    aDeviceDesc[i++] = pDesc->bDeviceClass;
    aDeviceDesc[i++] = pDesc->bDeviceSubClass;
    aDeviceDesc[i++] = pDesc->bDeviceProtocol;
    aDeviceDesc[i++] = pDesc->bMaxPacketSize0;
    aDeviceDesc[i++] = pDesc->idVendor & 0xFFu;
    aDeviceDesc[i++] = pDesc->idVendor >> 8;
    aDeviceDesc[i++] = pDesc->idProduct & 0xFFu;
    aDeviceDesc[i++] = pDesc->idProduct >> 8;
    aDeviceDesc[i++] = pDesc->bcdDevice & 0xFFu;
    aDeviceDesc[i++] = pDesc->bcdDevice >> 8;
    aDeviceDesc[i++] = pDesc->iManufacturer;
    aDeviceDesc[i++] = pDesc->iProduct;
    aDeviceDesc[i++] = pDesc->iSerialNumber;
    aDeviceDesc[i]   = pDesc->bNumConfigurations;
    USBH_MEMCPY(pDescriptor, aDeviceDesc, *pBufferSize);
  }
  return Status;
}

/*********************************************************************
*
*       USBH_GetDeviceDescriptorPtr
*
*  Function description
*    Returns a pointer to the device descriptor structure of the device containing the given interface.
*
*  Parameters
*    hInterface:     Valid handle to an interface, returned by USBH_OpenInterface().
*
*  Return value
*    Pointer to the current device descriptor information (read only), that
*    was stored during the device enumeration. The pointer gets invalid, when the
*    interface is closed using USBH_CloseInterface().
*/
const USBH_DEVICE_DESCRIPTOR * USBH_GetDeviceDescriptorPtr(USBH_INTERFACE_HANDLE hInterface) {
  USB_INTERFACE * pUsbInterface;
  USB_DEVICE    * pDev;

  pUsbInterface = hInterface;
  USBH_ASSERT_MAGIC(pUsbInterface, USB_INTERFACE);
  pDev          = pUsbInterface->pDevice;
  USBH_ASSERT_MAGIC(pDev, USB_DEVICE);
  return &pDev->DeviceDescriptor;
}

/*********************************************************************
*
*       USBH_GetCurrentConfDescriptorPtr
*
*  Function description
*    Retrieves the current configuration descriptor of the device containing the given interface.
*/
USBH_STATUS USBH_GetCurrentConfDescriptorPtr(USBH_INTERFACE_HANDLE hInterface, const U8 ** pDesc, unsigned * pDescLen) {
  USB_INTERFACE * pUsbInterface;
  USB_DEVICE    * pDev;

  USBH_LOG((USBH_MCAT_INTF_API, "USBH_GetCurrentConfDescriptorPtr"));
  pUsbInterface = hInterface;
  USBH_ASSERT_MAGIC(pUsbInterface, USB_INTERFACE);
  pDev          = pUsbInterface->pDevice;
  USBH_ASSERT_MAGIC(pDev, USB_DEVICE);
  if (pDev->State < DEV_STATE_WORKING) {
    return USBH_STATUS_DEVICE_REMOVED;
  }
  *pDesc = pDev->pConfigDescriptor;
  *pDescLen    = pDev->ConfigDescriptorSize;
  return USBH_STATUS_SUCCESS;
}

/*********************************************************************
*
*       USBH_GetCurrentConfigurationDescriptor
*
*  Function description
*    Retrieves the current configuration descriptor of the device containing the given interface.
*
*  Parameters
*    hInterface:     Valid handle to an interface, returned by USBH_OpenInterface().
*    pDescriptor:    Pointer to a buffer where the descriptor is stored.
*    pBufferSize:    * [IN]  Size of the buffer pointed to by pDescriptor.
*                    * [OUT] Number of bytes copied into the buffer.
*
*  Return value
*    USBH_STATUS_SUCCESS on success. Other values indicate an error.
*
*  Additional information
*    The function returns a copy of the current configuration descriptor, that
*    was stored during the device enumeration. If the given buffer size is too small
*    the configuration descriptor returned is truncated.
*/
USBH_STATUS USBH_GetCurrentConfigurationDescriptor(USBH_INTERFACE_HANDLE hInterface, U8 * pDescriptor, unsigned * pBufferSize) {
  unsigned        DescLen;
  const U8      * pDesc;
  USBH_STATUS     Status;

  Status = USBH_GetCurrentConfDescriptorPtr(hInterface, &pDesc, &DescLen);
  if (Status == USBH_STATUS_SUCCESS) {
    *pBufferSize = USBH_MIN(*pBufferSize, DescLen);
    USBH_MEMCPY(pDescriptor, pDesc, *pBufferSize);
  }
  return Status;
}

/*********************************************************************
*
*       USBH_SearchUsbInterface
*
*  Function description
*    Searches in the interface list of the device an interface that matches with iMask.
*
*  Return value
*    On success: Pointer to the interface descriptor!
*    Else:       Error
*/
USBH_STATUS USBH_SearchUsbInterface(const USB_DEVICE * pDev, const USBH_INTERFACE_MASK * pInterfaceMask, USB_INTERFACE ** ppUsbInterface) {
  USB_INTERFACE * pInterface;
  USBH_STATUS     Status = USBH_STATUS_INVALID_PARAM;
  USBH_DLIST         * pEntry;

  USBH_ASSERT_MAGIC(pDev, USB_DEVICE);
  pEntry = USBH_DLIST_GetNext(&pDev->UsbInterfaceList);
  while (pEntry != &pDev->UsbInterfaceList) {
    pInterface = GET_USB_INTERFACE_FROM_ENTRY(pEntry); // Search in all device interfaces and notify every interface
    USBH_ASSERT_MAGIC(pInterface, USB_INTERFACE);
    Status = USBH_CompareUsbInterface(pInterface, pInterfaceMask, TRUE);
    if (USBH_STATUS_SUCCESS == Status) {
      *ppUsbInterface = pInterface;
      break;
    }
    pEntry = USBH_DLIST_GetNext(pEntry);
  }
  return Status;
}

/*********************************************************************
*
*       USBH_GetEndpointDescriptor
*
*  Function description
*    Retrieves an endpoint descriptor of the device containing the given interface.
*
*  Parameters
*    hInterface:        Valid handle to an interface, returned by USBH_OpenInterface().
*    AlternateSetting:  Specifies the alternate setting for the interface. The function
*                       returns endpoint descriptors that are inside the specified alternate setting.
*    pMask:             Pointer to a caller allocated structure of type  USBH_EP_MASK, that
*                       specifies the endpoint selection pattern.
*    pBuffer:           Pointer to a buffer where the descriptor is stored.
*    pBufferSize:       * [IN]  Size of the buffer pointed to by pBuffer.
*                       * [OUT] Number of bytes copied into the buffer.
*
*  Return value
*    USBH_STATUS_SUCCESS on success. Other values indicate an error.
*
*  Additional information
*    The endpoint descriptor is extracted from the current configuration descriptor, that
*    was stored during the device enumeration. If the given buffer size is too small
*    the endpoint descriptor returned is truncated.
*/
USBH_STATUS USBH_GetEndpointDescriptor(USBH_INTERFACE_HANDLE hInterface, U8 AlternateSetting, const USBH_EP_MASK * pMask, U8 * pBuffer, unsigned * pBufferSize) {
  USB_INTERFACE * pUsbInterface;
  const U8      * pDesc;
  unsigned        DescLen;
  const U8      * pEndpointDesc;
  unsigned int    Index = 0;

  USBH_LOG((USBH_MCAT_INTF_API, "USBH_GetEndpointDescriptor: Alt Setting:%d pMask: 0x%x", AlternateSetting, pMask->Mask));
  pUsbInterface = hInterface;
  USBH_ASSERT_MAGIC(pUsbInterface, USB_INTERFACE);
  USBH_FindAltInterfaceDesc(pUsbInterface, AlternateSetting, &pDesc, &DescLen);
  if (pDesc == NULL) {
    USBH_WARN((USBH_MCAT_INTF, "USBH_GetEndpointDescriptor: Alternate setting not found!"));
    return USBH_STATUS_INVALID_PARAM;
  }
  for (;;) {
    pEndpointDesc = USBH_FindNextEndpointDesc(&pDesc, &DescLen);
    if (NULL == pEndpointDesc) {
      USBH_LOG((USBH_MCAT_INTF, "Warning: No endpoint descriptor found with set mask!"));
      return USBH_STATUS_INVALID_PARAM;
    }
    // Check the mask
    if ((((pMask->Mask & USBH_EP_MASK_INDEX)     == 0u) || (Index >= pMask->Index)) &&
        (((pMask->Mask & USBH_EP_MASK_ADDRESS)   == 0u) || (pEndpointDesc[USB_EP_DESC_ADDRESS_OFS]                            == pMask->Address)) &&
        (((pMask->Mask & USBH_EP_MASK_TYPE)      == 0u) || (pEndpointDesc[USB_EP_DESC_ATTRIB_OFS]  & USB_EP_DESC_ATTRIB_MASK) == pMask->Type)     &&
        (((pMask->Mask & USBH_EP_MASK_DIRECTION) == 0u) || (pEndpointDesc[USB_EP_DESC_ADDRESS_OFS] & USB_EP_DESC_DIR_MASK)    == pMask->Direction))
      {
      break;
    }
    Index++;
  }
  *pBufferSize = USBH_MIN(*pBufferSize, USB_ENDPOINT_DESCRIPTOR_LENGTH);
  USBH_MEMCPY(pBuffer, pEndpointDesc, *pBufferSize);
  return USBH_STATUS_SUCCESS;
}

/*********************************************************************
*
*       USBH_GetDescriptorPtr
*
*  Function description
*    Find descriptor with given type in the configuration descriptor and
*    and return a pointer to it. The pointer is valid until the device
*    is removed.
*/
USBH_STATUS USBH_GetDescriptorPtr(USBH_INTERFACE_HANDLE hInterface, U8 AlternateSetting, U8 Type, const U8 ** ppDesc) {
  USB_INTERFACE * pUsbInterface;
  const U8      * pDesc;
  unsigned        DescLen;

  USBH_LOG((USBH_MCAT_INTF_API, "USBH_GetDescriptorPtr: Alt Setting:%d Type: 0x%x", AlternateSetting, Type));
  pUsbInterface = hInterface;
  USBH_ASSERT_MAGIC(pUsbInterface, USB_INTERFACE);
  USBH_FindAltInterfaceDesc(pUsbInterface, AlternateSetting, &pDesc, &DescLen);
  if (pDesc == NULL) {
    USBH_WARN((USBH_MCAT_INTF, "USBH_GetDescriptorPtr: Alternate setting not found!"));
    return USBH_STATUS_INVALID_PARAM;
  }
  while (DescLen > 0u) {
    if (pDesc[1] == Type) {
      break;
    }
    DescLen -= *pDesc;
    pDesc   += *pDesc;
  }
  if (DescLen == 0u) {
    return USBH_STATUS_INVALID_PARAM;
  }
  *ppDesc = pDesc;
  return USBH_STATUS_SUCCESS;
}

/*********************************************************************
*
*       USBH_GetDescriptor
*
*  Function description
*    Find descriptor with given type in the configuration descriptor and
*    copy it to the user buffer.
*/
USBH_STATUS USBH_GetDescriptor(USBH_INTERFACE_HANDLE hInterface, U8 AlternateSetting, U8 Type, U8 * pBuffer, unsigned * pBufferSize) {
  unsigned        Len;
  const U8      * pDesc;
  USBH_STATUS     Status;

  Status = USBH_GetDescriptorPtr(hInterface, AlternateSetting, Type, &pDesc);
  if (Status != USBH_STATUS_SUCCESS) {
    return Status;
  }
  Len = *pDesc;
  *pBufferSize = USBH_MIN(*pBufferSize, Len);
  USBH_MEMCPY(pBuffer, pDesc, *pBufferSize);
  return USBH_STATUS_SUCCESS;
}

/*********************************************************************
*
*       USBH_GetInterfaceDescriptorPtr
*
*  Function description
*    Retrieves the interface descriptor of the given interface.
*/
USBH_STATUS USBH_GetInterfaceDescriptorPtr(USBH_INTERFACE_HANDLE hInterface, U8 AlternateSetting, const U8 ** ppDesc, unsigned * pDescLen) {
  USB_INTERFACE    * pUsbInterface;

  USBH_LOG((USBH_MCAT_INTF_API, "USBH_GetInterfaceDescriptorPtr: Alt Setting:%d", AlternateSetting));
  pUsbInterface = hInterface;
  USBH_ASSERT_MAGIC(pUsbInterface, USB_INTERFACE);
  USBH_FindAltInterfaceDesc(pUsbInterface, AlternateSetting, ppDesc, pDescLen);
  if (*ppDesc == NULL) {
    USBH_WARN((USBH_MCAT_INTF_API, "USBH_GetInterfaceDescriptorPtr: Alternate setting not found!"));
    return USBH_STATUS_INVALID_PARAM;
  }
  return USBH_STATUS_SUCCESS;
}

/*********************************************************************
*
*       USBH_GetInterfaceDescriptor
*
*  Function description
*    Retrieves the interface descriptor of the given interface.
*
*  Parameters
*    hInterface:        Valid handle to an interface, returned by USBH_OpenInterface().
*    AlternateSetting:  Specifies the alternate setting for this interface.
*    pBuffer:           Pointer to a buffer where the descriptor is stored.
*    pBufferSize:       * [IN]  Size of the buffer pointed to by pBuffer.
*                       * [OUT] Number of bytes copied into the buffer.
*
*  Return value
*    USBH_STATUS_SUCCESS on success. Other values indicate an error.
*
*  Additional information
*    The interface descriptor is extracted from the current configuration descriptor, that
*    was stored during the device enumeration.
*    The interface descriptor belongs to the
*    interface that is identified by hInterface. If the interface has different
*    alternate settings the interface descriptors of each alternate setting can be
*    requested.
*
*    If the given buffer size is too small
*    the interface descriptor returned is truncated.
*/
USBH_STATUS USBH_GetInterfaceDescriptor(USBH_INTERFACE_HANDLE hInterface, U8 AlternateSetting, U8 * pBuffer, unsigned * pBufferSize) {
  USBH_STATUS        Status;
  const U8         * pDesc;
  unsigned           DescLen;

  Status = USBH_GetInterfaceDescriptorPtr(hInterface, AlternateSetting, &pDesc, &DescLen);
  if (Status == USBH_STATUS_SUCCESS) {
    *pBufferSize = USBH_MIN(*pBufferSize, DescLen);
    USBH_MEMCPY(pBuffer, pDesc, *pBufferSize);
  }
  return Status;
}

/*********************************************************************
*
*       USBH_GetFullInterfaceDescriptorPtr
*
*  Function description
*    Retrieves the interface descriptor of the given interface with all alternate settings.
*/
void USBH_GetFullInterfaceDescriptorPtr(USBH_INTERFACE_HANDLE hInterface, const U8 ** ppDesc, unsigned * pDescLen) {
  USB_INTERFACE    * pUsbInterface;

  pUsbInterface = hInterface;
  USBH_ASSERT_MAGIC(pUsbInterface, USB_INTERFACE);
  *ppDesc   = pUsbInterface->pInterfaceDescriptor;
  *pDescLen = pUsbInterface->InterfaceDescriptorSize;
}

/*********************************************************************
*
*       USBH_GetDescriptorEx
*
*  Function description
*/
USBH_STATUS USBH_GetDescriptorEx(USBH_INTERFACE_HANDLE hInterface, U8 Type, U8 DescIndex, U16 LangID, U8 * pBuffer, unsigned * pBufferSize) {
  USB_INTERFACE     * pUsbInterface;
  USB_DEVICE        * pDev;
  USBH_STATUS         Status;
  USBH_OS_EVENT_OBJ * pEvent;
  USBH_URB            Urb;
  unsigned            Len;

  Len = *pBufferSize;
  if (Len < 8u || Len > 255u) {
    return USBH_STATUS_INVALID_PARAM;
  }
  pEvent = USBH_OS_AllocEvent();
  if (pEvent == NULL) {
    USBH_WARN((USBH_MCAT_DEVICE, "Allocation of an event object failed"));
    return USBH_STATUS_RESOURCES;
  }
  pUsbInterface = hInterface;
  pDev          = pUsbInterface->pDevice;
  USBH_ASSERT_MAGIC(pUsbInterface, USB_INTERFACE);
  // Prepare an URB
  if (USBH_CheckCtrlTransferBuffer(pDev, Len) != 0) {
    USBH_WARN((USBH_MCAT_DEVICE, "USBH_GetDescriptorEx: USBH_CheckCtrlTransferBuffer: No Memory"));
    Status = USBH_STATUS_MEMORY;
    goto End;
  }
  USBH_EnumPrepareGetDescReq(&Urb, Type, DescIndex, LangID, Len, pDev->pCtrlTransferBuffer);
  Urb.Header.pfOnCompletion = _OnSubmitUrbCompletion;
  Urb.Header.pContext = USBH_PTR2CTX(pEvent);
  Status = USBH_SubmitUrb(hInterface, &Urb);
  if (Status == USBH_STATUS_PENDING) {
    if (USBH_OS_WaitEventTimed(pEvent, USBH_DEFAULT_SETUP_TIMEOUT) != USBH_OS_EVENT_SIGNALED) {
      USBH_URB  AbortURB;

      USBH_MEMSET(&AbortURB, 0, sizeof(AbortURB));
      AbortURB.Header.Function = USBH_FUNCTION_ABORT_ENDPOINT;
      AbortURB.Request.EndpointRequest.Endpoint = 0x00;
      AbortURB.Header.pfOnCompletion = _OnSubmitUrbCompletion;
      AbortURB.Header.pContext       = USBH_PTR2CTX(pEvent);
      Status = USBH_SubmitUrb(hInterface, &AbortURB);
      if (Status == USBH_STATUS_PENDING) {
        USBH_OS_WaitEvent(pEvent);
        Status = USBH_STATUS_CANCELED;
      }
    } else {
      Status = Urb.Header.Status;
      if (Status == USBH_STATUS_SUCCESS) {
        Len = USBH_MIN(Len, Urb.Request.ControlRequest.Length);
        if (pBuffer != NULL) {
          USBH_MEMCPY(pBuffer, pDev->pCtrlTransferBuffer, Len);
        }
        *pBufferSize = Len;
      } else {
        USBH_WARN((USBH_MCAT_DEVICE, "USBH_GetDescriptorEx:  URB signaled with status %s", USBH_GetStatusStr(Status)));
      }
    }
  } else {
    USBH_WARN((USBH_MCAT_DEVICE, "USBH_GetDescriptorEx: USBH_SubmitUrb failed %s", USBH_GetStatusStr(Status)));
  }
End:
  USBH_OS_FreeEvent(pEvent);
  return Status;
}

/*********************************************************************
*
*       USBH_GetStringDescriptor
*
*  Function description
*    Retrieves the raw string descriptor from the device containing the given interface.
*    First two bytes of descriptor are type (always USB_STRING_DESCRIPTOR_TYPE) and length.
*    The rest contains a UTF-16 LE string.
*    If the given buffer size is too small the string is truncated.
*
*  Parameters
*    hInterface:     Valid handle to an interface, returned by USBH_OpenInterface().
*    StringIndex:    Index of the string.
*    LangID:         Language index. The default language of a device has the ID 0.
*                    See "Universal Serial Bus Language Identifiers (LANGIDs) version 1.0" for more details.
*                    This document is available on usb.org.
*    pBuffer:        Pointer to a buffer where the string is stored.
*    pNumBytes:      * [IN]  Size of the buffer pointed to by pBuffer.
*                    * [OUT] Number of bytes copied into the buffer.
*
*  Return value
*    USBH_STATUS_SUCCESS on success. Other values indicate an error.
*/
USBH_STATUS USBH_GetStringDescriptor(USBH_INTERFACE_HANDLE hInterface, U8 StringIndex, U16 LangID, U8 * pBuffer, unsigned * pNumBytes) {
  return USBH_GetDescriptorEx(hInterface, USB_STRING_DESCRIPTOR_TYPE, StringIndex, LangID, pBuffer, pNumBytes);
}

/*********************************************************************
*
*       USBH_GetStringDescriptorASCII
*
*  Function description
*    Retrieves a string from a string descriptor from the device containing the given interface.
*    The string returned is 0-terminated.
*    The returned data does not contain a USB descriptor header and is encoded in the
*    first language Id. Non-ASCII characters are replaced by '@'.
*    If the given buffer size is too small the string is truncated.
*    The maximum string length returned is BufferSize - 1.
*
*  Parameters
*    hInterface:     Valid handle to an interface, returned by USBH_OpenInterface().
*    StringIndex:    Index of the string.
*    pBuffer:        Pointer to a buffer where the string is stored.
*    BufferSize:     Size of the buffer pointed to by pBuffer.
*
*  Return value
*    USBH_STATUS_SUCCESS on success. Other values indicate an error.
*/
USBH_STATUS USBH_GetStringDescriptorASCII(USBH_INTERFACE_HANDLE hInterface, U8 StringIndex, char * pBuffer, unsigned BufferSize) {
  USB_INTERFACE * pUsbInterface;
  USB_DEVICE    * pDev;
  unsigned        Len;
  U8            * p;
  USBH_STATUS     Status;

  if (StringIndex == 0u) {
    return USBH_STATUS_NOT_FOUND;
  }
  pUsbInterface = hInterface;
  USBH_ASSERT_MAGIC(pUsbInterface, USB_INTERFACE);
  pDev           = pUsbInterface->pDevice;
  if (pDev->State < DEV_STATE_WORKING) {
    return USBH_STATUS_DEVICE_REMOVED;
  }
  if (BufferSize == 0u) {
    return USBH_STATUS_INVALID_PARAM;
  }
  Len = 255;
  Status = USBH_GetDescriptorEx(hInterface, USB_STRING_DESCRIPTOR_TYPE, StringIndex, pDev->LanguageId, NULL, &Len);
  if (Status == USBH_STATUS_SUCCESS) {
    p = pDev->pCtrlTransferBuffer + 2;
    while (Len >= 4u && BufferSize > 1u) {
      *pBuffer++ = (p[1] == 0u) ? (char)p[0] : '@';
      Len -= 2u;
      p   += 2;
      BufferSize--;
    }
    *pBuffer = '\0';
  }
  return Status;
}

/*********************************************************************
*
*       USBH_GetSerialNumber
*
*  Function description
*    Retrieves the serial number of the device containing the given interface.
*    The serial number is returned as a UNICODE string in little endian format.
*    The number of valid bytes is returned in pBufferSize. The string is not zero terminated.
*    The returned data does not contain a USB descriptor header and is encoded in the
*    first language Id. This string is a copy of the serial number string that was requested
*    during the enumeration.
*    If the device does not support a USB serial number string the function returns USBH_STATUS_SUCCESS
*    and a length of 0.
*    If the given buffer size is too small the serial number returned is truncated.
*
*  Parameters
*    hInterface:     Valid handle to an interface, returned by USBH_OpenInterface().
*    pBuffer:        Pointer to a buffer where the serial number is stored.
*    pBufferSize:    * [IN]  Size of the buffer pointed to by pBuffer.
*                    * [OUT] Number of bytes copied into the buffer.
*
*  Return value
*    USBH_STATUS_SUCCESS on success. Other values indicate an error.
*/
USBH_STATUS USBH_GetSerialNumber(USBH_INTERFACE_HANDLE hInterface, U8 * pBuffer, unsigned * pBufferSize) {
  USB_INTERFACE * pUsbInterface;
  USB_DEVICE    * pDev;

  USBH_LOG((USBH_MCAT_INTF_API, "USBH_GetSerialNumber"));
  pUsbInterface = hInterface;
  USBH_ASSERT_MAGIC(pUsbInterface, USB_INTERFACE);
  pDev           = pUsbInterface->pDevice;
  if (pDev->State < DEV_STATE_WORKING) {
    return USBH_STATUS_DEVICE_REMOVED;
  }
  *pBufferSize = USBH_MIN(*pBufferSize, pDev->SerialNumberSize);
  if (*pBufferSize != 0u) {
    USBH_MEMCPY(pBuffer, pDev->pSerialNumber, *pBufferSize); // Returns a little endian unicode string
  }
  return USBH_STATUS_SUCCESS;
}

/*********************************************************************
*
*       USBH_GetSerialNumberASCII
*
*  Function description
*    Retrieves the serial number of the device containing the given interface.
*    The serial number is returned as 0 terminated string.
*    The returned data does not contain a USB descriptor header and is encoded in the
*    first language Id. This string is a copy of the serial number string that was requested
*    during the enumeration. Non-ASCII characters are replaced by '@'.
*    If the device does not support a USB serial number string the function returns USBH_STATUS_SUCCESS
*    and a zero length string.
*    If the given buffer size is too small the serial number returned is truncated.
*    The maximum string length returned is BuffSize - 1.
*
*  Parameters
*    hInterface:     Valid handle to an interface, returned by USBH_OpenInterface().
*    pBuffer:        Pointer to a buffer where the serial number is stored.
*    BufferSize:     Size of the buffer pointed to by pBuffer.
*
*  Return value
*    USBH_STATUS_SUCCESS on success. Other values indicate an error.
*/
USBH_STATUS USBH_GetSerialNumberASCII(USBH_INTERFACE_HANDLE hInterface, char * pBuffer, unsigned BufferSize) {
  USB_INTERFACE * pUsbInterface;
  USB_DEVICE    * pDev;
  unsigned        i;
  U8            * p;

  USBH_LOG((USBH_MCAT_INTF_API, "USBH_GetSerialNumberASCII"));
  pUsbInterface = hInterface;
  USBH_ASSERT_MAGIC(pUsbInterface, USB_INTERFACE);
  pDev           = pUsbInterface->pDevice;
  if (pDev->State < DEV_STATE_WORKING) {
    return USBH_STATUS_DEVICE_REMOVED;
  }
  if (BufferSize == 0u) {
    return USBH_STATUS_INVALID_PARAM;
  }
  p = pDev->pSerialNumber;
  i = pDev->SerialNumberSize;
  while (i >= 2u && BufferSize > 0u) {
    *pBuffer++ = (p[1] == 0u) ? (char)p[0] : '@';
    i -= 2u;
    p += 2;
    BufferSize--;
  }
  *pBuffer = '\0';
  return USBH_STATUS_SUCCESS;
}

/*********************************************************************
*
*       USBH_GetSpeed
*
*  Function description
*    Returns the operating speed of the device.
*
*  Parameters
*    hInterface:        Valid handle to an interface, returned by USBH_OpenInterface().
*    pSpeed:            Pointer to a variable that will receive the speed information.
*
*  Return value
*    USBH_STATUS_SUCCESS on success. Other values indicate an error.
*
*  Additional information
*    A high speed device can operate in full or high speed mode.
*/
USBH_STATUS USBH_GetSpeed(USBH_INTERFACE_HANDLE hInterface, USBH_SPEED * pSpeed) {
  USB_INTERFACE * pUsbInterface;
  USB_DEVICE    * pDev;

  USBH_LOG((USBH_MCAT_INTF_API, "USBH_GetSpeed"));
  pUsbInterface = hInterface;
  USBH_ASSERT_MAGIC(pUsbInterface, USB_INTERFACE);
  pDev          = pUsbInterface->pDevice;
  if (pDev->State < DEV_STATE_WORKING) {
    USBH_WARN((USBH_MCAT_DEVICE, "Device Notification Error:  USBH_GetSpeed: invalid device state!"));
    return USBH_STATUS_DEVICE_REMOVED;
  }
  *pSpeed = pDev->DeviceSpeed;
  return USBH_STATUS_SUCCESS;
}

/*********************************************************************
*
*       USBH_GetFrameNumber
*
*  Function description
*    Retrieves the current frame number.
*
*  Parameters
*    hInterface:        Valid handle to an interface, returned by USBH_OpenInterface().
*    pFrameNumber:      Pointer to a variable that receives the frame number.
*
*  Return value
*    USBH_STATUS_SUCCESS on success. Other values indicate an error.
*
*  Additional information
*    The frame number is transferred on the bus with 11 bits. This frame number is
*    returned as a 16 or 32 bit number related to the implementation of the host controller.
*    The last 11 bits are equal to the current frame. The frame number is increased
*    each millisecond if the host controller is running in full-speed mode,
*    or each 125 microsecond if the host controller is running in high-speed mode,
*    The returned frame number is related to the bus where the device is connected.
*    The frame numbers between different host controllers can be different.
*
*    CAUTION: The functionality is not implemented for all host drivers. For some host
*    controllers the function may always return a frame number of 0.
*/
USBH_STATUS USBH_GetFrameNumber(USBH_INTERFACE_HANDLE hInterface, U32 * pFrameNumber) {
  USB_INTERFACE        * pUsbInterface;
  USBH_HOST_CONTROLLER * pHostController;
  USB_DEVICE           * pDev;

  pUsbInterface = hInterface;
  USBH_ASSERT_MAGIC(pUsbInterface, USB_INTERFACE);
  pDev          = pUsbInterface->pDevice;
  if (pDev->State < DEV_STATE_WORKING) {
    USBH_WARN((USBH_MCAT_DEVICE, "Device Notification Error:  USBH_GetFrameNumber: invalid device state!"));
    return USBH_STATUS_DEVICE_REMOVED;
  }
  pHostController = pDev->pHostController;
  *pFrameNumber   = pHostController->pDriver->pfGetFrameNumber(pHostController->pPrvData);
  USBH_LOG((USBH_MCAT_INTF_API, "USBH_GetFrameNumber: %d", *pFrameNumber));
  return USBH_STATUS_SUCCESS;
}

/*********************************************************************
*
*       USBH_IncRef
*
*  Function description
*    Increment reference count of a device object.
*    Returns an error code, if the device is marked as 'removed'.
*/
USBH_STATUS USBH_IncRef(USB_DEVICE * pDevice
#if USBH_DEBUG > 1
                        , const char *pFile, int Line
#endif
                        ) {
  USBH_STATUS Ret;

  Ret = USBH_STATUS_SUCCESS;
  USBH_OS_Lock(USBH_MUTEX_DEVICE);
  if (pDevice->RefCount == 0) {
    Ret = USBH_STATUS_DEVICE_REMOVED;
  } else {
    pDevice->RefCount++;
  }
  USBH_OS_Unlock(USBH_MUTEX_DEVICE);
  if (Ret != USBH_STATUS_SUCCESS) {
    USBH_LOG((USBH_MCAT_DEVICE_REF, "Core: [UID%d, USBAddr%d] INC_REF RefCount is 0 %s(%d)",
                                    pDevice->UniqueID, pDevice->UsbAddress, pFile, Line));
  } else {
    USBH_LOG((USBH_MCAT_DEVICE_REF, "Core: [UID%d, USBAddr%d] INC_REF RefCount is %d %s(%d)",
                                    pDevice->UniqueID, pDevice->UsbAddress, pDevice->RefCount, pFile, Line));
  }
  return Ret;
}

/*********************************************************************
*
*       USBH_DecRef
*
*  Function description
*    Decrement reference count of a device object.
*/
void USBH_DecRef(USB_DEVICE * pDevice
#if USBH_DEBUG > 1
                        , const char *pFile, int Line
#endif
                        ) {
  int RefCount;

#if USBH_DEBUG > 1
  RefCount = _DecRef(pDevice, pFile, Line);
#else
  RefCount = _DecRef(pDevice);
#endif
  if (RefCount == 0 && pDevice->ListEntry.pNext == NULL) {
    //
    // Device does not belong to any host controller
    // (This may happen, if a device was not enumerated successfully).
    // So the device can be deleted immediately.
    //
    USBH_DeleteDevice(pDevice);
  }
}

/*********************************************************************
*
*       USBH_GetNumAlternateSettings
*
*  Function description
*    Retrieves the number of available interface alternate st.
*
*  Parameters
*    hInterface:        Valid handle to an interface, returned by USBH_OpenInterface().
*
*  Return value
*    Number of available alternate settings.
*
*  Additional information
*    The endpoint descriptor is extracted from the current configuration descriptor, that
*    was stored during the device enumeration. If the given buffer size is too small
*    the endpoint descriptor returned is truncated.
*/
unsigned USBH_GetNumAlternateSettings(USBH_INTERFACE_HANDLE hInterface) {
  USB_INTERFACE * pUsbInterface;
  const U8      * pDesc;
  unsigned        DescLen;
  unsigned        AlternateSetting;

  pUsbInterface = hInterface;
  USBH_ASSERT_MAGIC(pUsbInterface, USB_INTERFACE);
  for (AlternateSetting = 1;; AlternateSetting++) {
    USBH_FindAltInterfaceDesc(pUsbInterface, AlternateSetting, &pDesc, &DescLen);
    if (pDesc == NULL) {
      break;
    }
  }
  return AlternateSetting;
}

/*************************** End of file ****************************/
