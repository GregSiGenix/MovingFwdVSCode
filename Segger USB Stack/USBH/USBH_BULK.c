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
File        : USBH_BULK.c
Purpose     : API of the USB host stack
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include section
*
**********************************************************************
*/
#include "USBH_Int.h"
#include "USBH_BULK.h"
#include "USBH_Util.h"

/*********************************************************************
*
*       Defines
*
**********************************************************************
*/
#define USBH_BULK_NUM_DEVICES               32u  // NOTE: Limited by the number of bits in DevIndexUsedMask which by now is 32

#define USBH_BULK_REMOVAL_TIMEOUT          100

#if USBH_REF_TRACE
  #define DEC_REF_CNT(pInst)        _DecRefCnt(pInst, __func__, __LINE__)
  #define INC_REF_CNT(pInst)        _IncRefCnt(pInst, __func__, __LINE__)
#else
  #define DEC_REF_CNT(pInst)        _DecRefCnt(pInst)
  #define INC_REF_CNT(pInst)        _IncRefCnt(pInst)
#endif

/*********************************************************************
*
*       Types
*
**********************************************************************
*/
typedef struct {
  U8                          EPAddr;
  volatile I8                 InUse;
  U8                          EPType;
  U16                         MaxPacketSize;
  U32                         MaxTransferSize;
  USBH_URB                    Urb;
  USBH_OS_EVENT_OBJ         * pEvent;
  U8                        * pInBuffer;
  USBH_BUFFER                 RingBuffer;
  struct _USBH_BULK_INST    * pInst;
} BULK_EP_DATA;

typedef struct _USBH_BULK_INST {
  struct _USBH_BULK_INST    * pNext;
  USBH_INTERFACE_ID           InterfaceID;
  USBH_INTERFACE_HANDLE       hInterface;
  USBH_TIMER                  RemovalTimer;
  U8                          NumEPs;
  U8                          DevIndex;
  I8                          IsOpened;
  I8                          AllowShortRead;
  BULK_EP_DATA              * pEndpoints;
  USBH_BULK_HANDLE            Handle;
  I32                         RefCnt;
  BULK_EP_DATA                Control;
} USBH_BULK_INST;

typedef struct {
  USBH_BULK_INST            * pFirst;
  U8                          NumDevices;
  USBH_BULK_HANDLE            NextHandle;
  USBH_NOTIFICATION_HOOK    * pFirstNotiHook;
  U32                         DevIndexUsedMask;
  USBH_INTERFACE_MASK         InitInterfaceMask;
} USBH_BULK_GLOBAL;

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static USBH_BULK_GLOBAL       USBH_BULK_Global;
static I8                     _isInited;
static USBH_NOTIFICATION_HOOK _Hook;

/*********************************************************************
*
*       Prototypes
*
**********************************************************************
*/

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _PrepareSetupPacket
*
*  Function description
*    Prepares a setup packet that shall be sent to device.
*
*  Parameters
*    pRequest      : Pointer to the setup request.
*    RequestType   : Setup request's bmRequest value.
*    Request       : Setup request's bRequest value.
*    wValue        : Setup request's wValue value.
*    wIndex        : Setup request's wIndex value.
*    pData         : Pointer to the data/buffer when NumBytesData > 0.
*    NumBytesData  : Number of bytes to send/receive.
*/
static void _PrepareSetupPacket(USBH_CONTROL_REQUEST * pRequest, U8 RequestType, U8 Request, U16 wValue, U16 wIndex, void * pData, U32 NumBytesData) {
  pRequest->Setup.Type    = RequestType;
  pRequest->Setup.Request = Request;
  pRequest->Setup.Value   = wValue;
  pRequest->Setup.Index   = wIndex;
  pRequest->Setup.Length  = NumBytesData;
  pRequest->pBuffer       = pData;
}

/*********************************************************************
*
*       _AllocateDevIndex()
*
*   Function description
*     Searches for an available device index which is the index
*     of the first cleared bit in the DevIndexUsedMask.
*
*   Return value
*     A device index or USBH_BULK_NUM_DEVICES in case all device indexes are allocated.
*/
static unsigned _AllocateDevIndex(void) {
  unsigned i;
  U32      Mask;

  Mask = 1;
  for (i = 0; i < USBH_BULK_NUM_DEVICES; ++i) {
    if ((USBH_BULK_Global.DevIndexUsedMask & Mask) == 0u) {
      USBH_BULK_Global.DevIndexUsedMask |= Mask;
      break;
    }
    Mask <<= 1;
  }
  return i;
}

/*********************************************************************
*
*       _FreeDevIndex()
*
*   Function description
*     Marks a device index as free by clearing the corresponding bit
*     in the DevIndexUsedMask.
*
*   Parameters
*     DevIndex     : Device Index that should be freed.
*/
static void _FreeDevIndex(unsigned DevIndex) {
  USBH_BULK_Global.DevIndexUsedMask &= ~(1uL << DevIndex);
}

/*********************************************************************
*
*       _h2p()
*/
static USBH_BULK_INST * _h2p(USBH_BULK_HANDLE Handle) {
  USBH_BULK_INST * pInst;

  if (Handle == 0u) {
    return NULL;
  }
  //
  // Iterate over linked list to find an instance with matching handle. Return if found.
  //
  pInst = USBH_BULK_Global.pFirst;
  while (pInst != NULL) {
    if (pInst->Handle == Handle) {                                        // Match ?
      return pInst;
    }
    pInst = pInst->pNext;
  }
  //
  // Error handling: Device handle not found in list.
  //
  USBH_WARN((USBH_MCAT_BULK, "HANDLE: handle %d not in instance list", Handle));
  return NULL;
}

/*********************************************************************
*
*       _AbortEP
*
*  Function description
*    Abort any URB transaction on the specified EP.
*
*  Parameters
*    pInst     : Pointer to the BULK instance.
*    pEPData   : Pointer to the BULK_EP_DATA structure.
*/
static USBH_STATUS _AbortEP(const USBH_BULK_INST * pInst, BULK_EP_DATA * pEPData) {
  USBH_URB    * pUrb;
  USBH_URB      AbortUrb;

  USBH_LOG((USBH_MCAT_BULK, "_AbortEP: Aborting an URB!"));
  USBH_ZERO_MEMORY(&AbortUrb, sizeof(USBH_URB));
  pUrb = &pEPData->Urb;
  if (pUrb->Header.Function != USBH_FUNCTION_CONTROL_REQUEST &&
      pUrb->Header.Function != USBH_FUNCTION_RESET_ENDPOINT) {
    AbortUrb.Request.EndpointRequest.Endpoint = pUrb->Request.BulkIntRequest.Endpoint;
  }
  USBH_LOG((USBH_MCAT_BULK, "_AbortEP: Abort Ep: 0x%x", pUrb->Request.EndpointRequest.Endpoint));
  AbortUrb.Header.Function = USBH_FUNCTION_ABORT_ENDPOINT;
  return USBH_SubmitUrb(pInst->hInterface, &AbortUrb);
}

/*********************************************************************
*
*       _AbortEPAddr
*
*  Function description
*    Abort any URB transaction on the specified EP.
*
*  Parameters
*    pInst     : Pointer to the BULK instance.
*    EPAddr    : Endpoint address.
*/
static USBH_STATUS _AbortEPAddr(const USBH_BULK_INST * pInst, U8 EPAddr) {
  BULK_EP_DATA    * pEPData;
  unsigned          i;

  pEPData = pInst->pEndpoints;
  for (i = pInst->NumEPs; i > 0u; i--) {
    if (pEPData->EPAddr == EPAddr) {
      break;
    }
    pEPData++;
  }
  if (i == 0u) {
    return USBH_STATUS_INVALID_PARAM;
  }
  return _AbortEP(pInst, pEPData);
}

/*********************************************************************
*
*       _GetEPData
*
*  Function description
*    Find endpoint with given address in the endpoint list of an interface
*    and mark it as 'Used'.
*/
static USBH_STATUS _GetEPData(const USBH_BULK_INST * pInst, U8 EPAddr, BULK_EP_DATA ** ppEPData, USBH_FUNCTION *pFunction) {
  BULK_EP_DATA    * pEPData;
  unsigned          i;
  I8                InUse;

  pEPData = pInst->pEndpoints;
  for (i = pInst->NumEPs; i > 0u; i--) {
    if (pEPData->EPAddr == EPAddr) {
      break;
    }
    pEPData++;
  }
  if (i == 0u) {
    return USBH_STATUS_INVALID_PARAM;
  }
  switch (pEPData->EPType) {
  case USB_EP_TYPE_BULK:
    *pFunction = USBH_FUNCTION_BULK_REQUEST;
    break;
  case USB_EP_TYPE_INT:
    *pFunction = USBH_FUNCTION_INT_REQUEST;
    break;
#if USBH_SUPPORT_ISO_TRANSFER
  case USB_EP_TYPE_ISO:
    *pFunction = USBH_FUNCTION_ISO_REQUEST;
    break;
#endif
  default:
    return USBH_STATUS_ENDPOINT_INVALID;     //lint -e{9077} D:102[a]
  }
  //
  // Check if the endpoint is not in use.
  //
  USBH_OS_Lock(USBH_MUTEX_BULK);
  InUse = pEPData->InUse;
  pEPData->InUse = 1;
  USBH_OS_Unlock(USBH_MUTEX_BULK);
  if (InUse != 0) {
    return USBH_STATUS_BUSY;
  }
  *ppEPData = pEPData;
  return USBH_STATUS_SUCCESS;
}

/*********************************************************************
*
*       _RemoveInstanceFromList
*
*  Function description
*    Removes the instance pointer from the singly linked list.
*
*  Notes
*    Calling function checks pInst.
*/
static void _RemoveInstanceFromList(const USBH_BULK_INST * pInst) {
  USBH_BULK_INST * pPrev;
  USBH_BULK_INST * pCurrent;

  if (pInst == USBH_BULK_Global.pFirst) {
    USBH_BULK_Global.pFirst = USBH_BULK_Global.pFirst->pNext;
  } else {
    pPrev = USBH_BULK_Global.pFirst;
    pCurrent = pPrev->pNext;
    while (pCurrent != NULL) {
      if (pInst == pCurrent) {
        pPrev->pNext = pCurrent->pNext;
        break;
      }
      pPrev = pCurrent;
      pCurrent = pCurrent->pNext;
    }
  }
}

/*********************************************************************
*
*       _IncRefCnt
*
*  Function description
*    Increments the reference counter of the device instance.
*
*  Parameters
*    pInst     : Pointer to the BULK instance.
*    s         : For debugging only.
*    d         : For debugging only.
*/
static USBH_STATUS _IncRefCnt(USBH_BULK_INST * pInst
#if USBH_REF_TRACE
                              , const char * s, int d
#endif
                             ) {
  USBH_STATUS Ret;

  Ret = USBH_STATUS_SUCCESS;
  USBH_OS_Lock(USBH_MUTEX_BULK);
  if (pInst->RefCnt == 0) {
    Ret = USBH_STATUS_DEVICE_REMOVED;
  } else {
    pInst->RefCnt++;
  }
  USBH_OS_Unlock(USBH_MUTEX_BULK);
#if USBH_REF_TRACE
  USBH_LOG((USBH_MCAT_BULK, "_IncRefCnt: [iface%d] %d %s@%d", pInst->Handle, pInst->RefCnt, s, d));
#endif
  return Ret;
}

/*********************************************************************
*
*       _DecRefCnt
*
*  Function description
*    Decrements the reference counter of the device instance.
*
*  Parameters
*    pInst     : Pointer to the BULK instance.
*    s         : For debugging only.
*    d         : For debugging only.
*/
static void _DecRefCnt(USBH_BULK_INST * pInst
#if USBH_REF_TRACE
                      , const char * s, int d
#endif
                     ) {
  int RefCount;

  USBH_OS_Lock(USBH_MUTEX_BULK);
  RefCount = pInst->RefCnt - 1;
  if (RefCount >= 0) {
    pInst->RefCnt = RefCount;
  }
  USBH_OS_Unlock(USBH_MUTEX_BULK);
#if USBH_REF_TRACE
  if (RefCount < 0) {
    USBH_WARN((USBH_MCAT_BULK, "Invalid RefCnt found: [iface%d] %d %s@%d", pInst->Handle, RefCount, s, d));
  }
  USBH_LOG((USBH_MCAT_BULK, "_DecRefCnt: [iface%d] %d %s@%d", pInst->Handle, RefCount, s, d));
#endif
}

/*********************************************************************
*
*       _RemovalTimer
*/
static void _RemovalTimer(void * pContext) {
  USBH_BULK_INST * pInst;
  unsigned         i;
  BULK_EP_DATA   * pEP;

  pInst = USBH_CTX2PTR(USBH_BULK_INST, pContext);
  if (pInst->IsOpened != 0 || pInst->RefCnt != 0) {
    USBH_StartTimer(&pInst->RemovalTimer, USBH_BULK_REMOVAL_TIMEOUT);
    return;
  }
  pEP = pInst->pEndpoints;
  for (i = 0; i < pInst->NumEPs; i++) {
    if (pEP->pEvent != NULL) {
      USBH_OS_FreeEvent(pEP->pEvent);
    }
    if (pEP->RingBuffer.pData != NULL) {
      USBH_FREE(pEP->RingBuffer.pData);
    }
    if (pEP->pInBuffer != NULL) {
      USBH_FREE(pEP->pInBuffer);
    }
    pEP++;
  }
  if (pInst->pEndpoints != NULL) {
    USBH_FREE(pInst->pEndpoints);
  }
  if (pInst->Control.pEvent != NULL) {
    USBH_OS_FreeEvent(pInst->Control.pEvent);
  }
  USBH_CloseInterface(pInst->hInterface);
  _FreeDevIndex(pInst->DevIndex);
  USBH_ReleaseTimer(&pInst->RemovalTimer);
  USBH_BULK_Global.NumDevices--;
  //
  // Remove instance from list
  //
  _RemoveInstanceFromList(pInst);
  //
  // Free the memory that is used by the instance
  //
  USBH_FREE(pInst);
}

/*********************************************************************
*
*       _OnSubmitUrbCompletion
*/
static void _OnSubmitUrbCompletion(USBH_URB * pUrb) USBH_CALLBACK_USE {
  BULK_EP_DATA  * pEPData;

  pEPData = USBH_CTX2PTR(BULK_EP_DATA, pUrb->Header.pContext);
  USBH_LOG((USBH_MCAT_BULK, "_OnSubmitUrbCompletion URB st: %s", USBH_GetStatusStr(pUrb->Header.Status)));
  USBH_OS_SetEvent(pEPData->pEvent);
}

/*********************************************************************
*
*       _SubmitUrbAndWait
*
*  Function description
*    Submits an URB to the USB bus driver synchronous, it uses the
*    OS event functions. On successful completion the URB Status is returned!
*/
static USBH_STATUS _SubmitUrbAndWait(const USBH_BULK_INST * pInst, BULK_EP_DATA * pEPData, U32 Timeout) {
  USBH_STATUS   Status;
  int           EventStatus;
  USBH_URB    * pUrb;

  if (pEPData->pEvent == NULL) {
    pEPData->pEvent = USBH_OS_AllocEvent();
    if (pEPData->pEvent == NULL) {
      return USBH_STATUS_RESOURCES;
    }
  }
  USBH_LOG((USBH_MCAT_BULK, "_SubmitUrbAndWait"));
  pUrb = &pEPData->Urb;
  pUrb->Header.pfOnCompletion = _OnSubmitUrbCompletion;
  pUrb->Header.pContext       = pEPData;
  USBH_OS_ResetEvent(pEPData->pEvent);
  Status = USBH_SubmitUrb(pInst->hInterface, pUrb);
  if (Status != USBH_STATUS_PENDING) {
    USBH_LOG((USBH_MCAT_BULK, "_SubmitUrbAndWait: USBH_SubmitUrb st: 0x%08x", USBH_GetStatusStr(Status)));
  } else {                                // Pending URB
    //
    // Wait for completion.
    //
    EventStatus = USBH_WaitEventTimed(pEPData->pEvent, Timeout);
    if (EventStatus != USBH_OS_EVENT_SIGNALED) {
      USBH_LOG((USBH_MCAT_BULK, "_SubmitUrbAndWait: Time-out Status: 0x%08x, now Abort the URB!", EventStatus));
      Status = _AbortEP(pInst, pEPData);
      if (Status != USBH_STATUS_SUCCESS) {
        USBH_LOG((USBH_MCAT_BULK, "_SubmitUrbAndWait: USBH_FUNCTION_ABORT_ENDPOINT st: 0x%08x", Status));
      }
      //
      // In case of an error (in most of the cases USBH_STATUS_DEVICE_REMOVED) return with an error.
      //
      if (Status == USBH_STATUS_SUCCESS) {
        //
        // Abort URB sent out successfully, wait for URB to terminate.
        //
        USBH_OS_WaitEvent(pEPData->pEvent);
        Status = pUrb->Header.Status;
        if (Status == USBH_STATUS_CANCELED || Status == USBH_STATUS_SUCCESS) {
          Status = USBH_STATUS_TIMEOUT;
        }
      }
    } else {
      //
      // In case the event was signaled the status is retrieved from the URB.
      //
      Status = pUrb->Header.Status;
      USBH_LOG((USBH_MCAT_BULK, "_SubmitUrbAndWait: URB Status: %s", USBH_GetStatusStr(Status)));
    }
  }
  return Status;
}

/*********************************************************************
*
*       _OnResetReadEndpointCompletion
*
*  Function description
*    Endpoint reset is complete. It submits an new URB if possible!
*/
#if 0
static void _OnResetReadEndpointCompletion(USBH_URB * pUrb) USBH_CALLBACK_USE {
  USBH_BULK_INST * pInst;
  USBH_ASSERT(pUrb != NULL);
  pInst = USBH_CTX2PTR(USBH_BULK_INST, pUrb->Header.pContext);
  EP_DEC_REF_CNT(&pInst->IntIn);
  if (pInst->RunningState == StateInit || pInst->RunningState == StateRunning) {
    // Resubmit an transfer request
    if (USBH_STATUS_SUCCESS != pUrb->Header.Status) {
      USBH_WARN((USBH_MCAT_BULK, "_OnResetReadEndpointCompletion: URB Status: %s", USBH_GetStatusStr(pUrb->Header.Status)));
      pInst->RunningState = StateError;
      //
      // Decrement reference count when changing state to error.
      // This is done because normally _StopDevice would decrement
      // the ref count when the device is removed. In case of StateError
      // this does not happen.
      //
      DEC_REF_CNT(pInst);
    } else {
      _SubmitIntTransfer(pInst, pInst->pIntInBuffer, pInst->IntIn.MaxPacketSize);
    }
  }
}
#endif

/*********************************************************************
*
*       _OnDeviceNotification
*/
static void _OnDeviceNotification(void * pContext, USBH_PNP_EVENT Event, USBH_INTERFACE_ID InterfaceID) {
  USBH_BULK_INST         * pInst;
  USBH_NOTIFICATION_HOOK * pHook;

  pHook = USBH_CTX2PTR(USBH_NOTIFICATION_HOOK, pContext);
  if (Event == USBH_ADD_DEVICE) {
    //
    // Check if max. number of devices allowed is exceeded.
    //
    if (USBH_BULK_Global.NumDevices >= USBH_BULK_NUM_DEVICES) {
      USBH_WARN((USBH_MCAT_BULK, "Too many BULK devices!"));
      return;
    }
    pInst = (USBH_BULK_INST *)USBH_TRY_MALLOC_ZEROED(sizeof(USBH_BULK_INST));
    if (pInst == NULL) {
      USBH_WARN((USBH_MCAT_BULK, "_OnDeviceNotification: device instance not created (no memory)!"));
      return;
    }
    if (USBH_OpenInterface(InterfaceID, 0, &pInst->hInterface) == USBH_STATUS_SUCCESS) {
      pInst->Handle      = ++USBH_BULK_Global.NextHandle;
      pInst->DevIndex    = _AllocateDevIndex();
      //
      // Initial reference counter.
      //
      pInst->RefCnt      = 1;
      pInst->InterfaceID = InterfaceID;
      pInst->pNext = USBH_BULK_Global.pFirst;
      USBH_BULK_Global.pFirst = pInst;
      USBH_BULK_Global.NumDevices++;
      USBH_InitTimer(&pInst->RemovalTimer, _RemovalTimer, pInst);
      USBH_LOG((USBH_MCAT_BULK, "_OnDeviceNotification: USB BULK device detected interface ID: %u !", InterfaceID));
      if (pHook != NULL && pHook->pfNotification != NULL) {
        pHook->pfNotification(pHook->pContext, pInst->DevIndex, USBH_DEVICE_EVENT_ADD);
      }
    }
    return;
  }
  if (Event == USBH_REMOVE_DEVICE) {
    for (pInst = USBH_BULK_Global.pFirst; pInst != NULL; pInst = pInst->pNext) {
      if (pInst->InterfaceID == InterfaceID) {
        //
        // Init and start the removal timer, the timer is responsible for
        // freeing all resources when the device is removed.
        //
        USBH_StartTimer(&pInst->RemovalTimer, USBH_BULK_REMOVAL_TIMEOUT);
        DEC_REF_CNT(pInst);
        USBH_LOG((USBH_MCAT_BULK, "_OnDeviceNotification: USB BULK device removed interface  ID: %u !", InterfaceID));
        if (pHook != NULL && pHook->pfNotification != NULL) {
          pHook->pfNotification(pHook->pContext, pInst->DevIndex, USBH_DEVICE_EVENT_REMOVE);
        }
        return;
      }
    }
    USBH_WARN((USBH_MCAT_BULK, "_OnDeviceNotification: pInst not found for notified interface %u!", InterfaceID));
  }
}

/*********************************************************************
*
*       _SendControlRequest
*
*  Function description
*    Sends a control URB to the device via EP0.
*
*  Parameters
*    pInst         : Pointer to the BULK instance.
*    RequestType   : IN/OUT direction.
*    Request       : Request code in the setup request.
*    wValue        : wValue in the setup request.
*    wIndex        : wIndex in the setup request.
*    pData         : Additional data for the setup request.
*    pNumBytesData : Number of data to be received/sent in pData.
*    Timeout       : Timeout in ms.
*
*  Return value
*    USBH_STATUS   : Transfer status.
*/
static USBH_STATUS _SendControlRequest(USBH_BULK_INST * pInst, U8 RequestType, U8 Request, U16 wValue, U16 wIndex, void * pData, U32 * pNumBytesData, U32 Timeout) {
  USBH_STATUS  Status;
  if (pInst->IsOpened != 0) {
    BULK_EP_DATA * pEPData;

    pEPData = &pInst->Control;
    pEPData->Urb.Header.Function = USBH_FUNCTION_CONTROL_REQUEST;
    if (pNumBytesData != NULL) {
      _PrepareSetupPacket(&pEPData->Urb.Request.ControlRequest, RequestType, Request, wValue, wIndex, pData, *pNumBytesData);
    } else {
      _PrepareSetupPacket(&pEPData->Urb.Request.ControlRequest, RequestType, Request, wValue, wIndex, pData, 0);
    }
    Status = INC_REF_CNT(pInst);
    if (Status == USBH_STATUS_SUCCESS) {
      Status = _SubmitUrbAndWait(pInst, pEPData, Timeout);
      DEC_REF_CNT(pInst);
      if (pNumBytesData != NULL && Status == USBH_STATUS_SUCCESS) {
        *pNumBytesData = pEPData->Urb.Request.ControlRequest.Length;
      }
    }
    return Status;
  } else {
    return USBH_STATUS_NOT_OPENED;
  }
}

/*********************************************************************
*
*       _ResetPipe
*
*  Function description
*    Resets a specific endpoint for a given device.
*
*  Parameters
*    pInst    : Pointer to a BULK device object.
*    EndPoint : Endpoint number and direction.
*/
static void _ResetPipe(USBH_BULK_INST * pInst, U8 EndPoint) {
  USBH_STATUS   Status;
  USBH_URB    * pUrb;
  BULK_EP_DATA * pEPData;

  pEPData = &pInst->Control;
  pUrb                                   = &pEPData->Urb;
  pUrb->Header.Function                  = USBH_FUNCTION_RESET_ENDPOINT;
  pUrb->Header.pfOnCompletion            = NULL;
  pUrb->Request.EndpointRequest.Endpoint = EndPoint;
  Status                                 = _SubmitUrbAndWait(pInst, pEPData, USBH_BULK_EP0_TIMEOUT); // On error this URB is not aborted
  if (Status != USBH_STATUS_SUCCESS) { // Reset pipe does not wait
    USBH_WARN((USBH_MCAT_BULK, "_ResetPipe: USBH_SubmitUrb Status = %s", USBH_GetStatusStr(Status)));
  }
}

/*********************************************************************
*
*       _OnAsyncCompletion
*
*  Function description
*    BULK internal completion routine for the USBH_BULK_ReadAsync and
*    USBH_BULK_WriteAsync functions.
*    Calls the user callback.
*
*  Parameters
*    pUrb     : Pointer to the completed URB.
*/
static void _OnAsyncCompletion(USBH_URB * pUrb) {
  USBH_BULK_RW_CONTEXT         * pRWContext;
  BULK_EP_DATA                 * pEPData;
  USBH_BULK_INT_REQUEST        * pBulkRequest;

  pEPData    = USBH_CTX2PTR(BULK_EP_DATA, pUrb->Header.pContext);
  pRWContext = USBH_CTX2PTR(USBH_BULK_RW_CONTEXT, pUrb->Header.pUserContext);
  //
  //  Update RWContext
  //
  pBulkRequest = &pUrb->Request.BulkIntRequest;
  pRWContext->Status = pUrb->Header.Status;
  pRWContext->NumBytesTransferred = pBulkRequest->Length;
  pRWContext->Terminated = 1;
  pEPData->InUse = 0;
  DEC_REF_CNT(pEPData->pInst);
  //
  // Call user function
  //
  pUrb->Header.pfOnUserCompletion(pRWContext);
}

/*********************************************************************
*
*       _GetEndpointInfo
*
*  Function description
*    Return array with info's about all endpoints.
*    The memory is allocated by this function.
*/
static int _GetEndpointInfo(USBH_BULK_INST * pInst) {
  USBH_EP_MASK        EPMask;
  unsigned int        Length;
  BULK_EP_DATA      * pEP;
  U8                  aEpDesc[USB_ENDPOINT_DESCRIPTOR_LENGTH];
  unsigned            CurrentAltInt;
  unsigned            NumEPs;
  U16                 MaxPacketSize;

  //
  // Find all endpoints.
  //
  EPMask.Mask = USBH_EP_MASK_INDEX;
  CurrentAltInt = 0;
  (void)USBH_GetInterfaceCurrAltSetting(pInst->hInterface, &CurrentAltInt);
  Length = sizeof(aEpDesc);
  EPMask.Index = 0;
  while (USBH_GetEndpointDescriptor(pInst->hInterface, CurrentAltInt, &EPMask, aEpDesc, &Length) == USBH_STATUS_SUCCESS) {
    EPMask.Index++;
  }
  if (EPMask.Index != 0u) {
    NumEPs = EPMask.Index;
    pInst->pEndpoints = (BULK_EP_DATA *)USBH_TRY_MALLOC_ZEROED(NumEPs * sizeof(BULK_EP_DATA));
    if (pInst->pEndpoints == NULL) {
      USBH_WARN((USBH_MCAT_BULK, "USBH_BULK_Open: Can't alloc memory for EPs"));
      return 1;
    }
    pEP = pInst->pEndpoints;
    for (EPMask.Index = 0; EPMask.Index < NumEPs; EPMask.Index++) {
      Length = sizeof(aEpDesc);
      if (USBH_GetEndpointDescriptor(pInst->hInterface, CurrentAltInt, &EPMask, aEpDesc, &Length) != USBH_STATUS_SUCCESS) {
        break;
      }
      pEP->EPAddr   = aEpDesc[USB_EP_DESC_ADDRESS_OFS];
      pEP->EPType   = aEpDesc[USB_EP_DESC_ATTRIB_OFS] & USB_EP_DESC_ATTRIB_MASK;
      pEP->pInst    = pInst;
      MaxPacketSize = aEpDesc[USB_EP_DESC_PACKET_SIZE_OFS] + ((unsigned)aEpDesc[USB_EP_DESC_PACKET_SIZE_OFS + 1u] << 8);
      pEP->MaxPacketSize = MaxPacketSize & 0x7FFuL;
      if (pEP->EPType == USB_EP_TYPE_ISO) {
        pEP->MaxPacketSize *= ((MaxPacketSize >> 11) & 3u) + 1u;
      }
      USBH_LOG((USBH_MCAT_BULK, "BULK_Open: Found EP 0x%02X, type %u, MaxPacketSize %u", pEP->EPAddr, pEP->EPType, pEP->MaxPacketSize));
      if (USBH_GetMaxTransferSize(pInst->hInterface, pEP->EPAddr, &pEP->MaxTransferSize) == USBH_STATUS_SUCCESS) {
        pInst->NumEPs++;
        pEP++;
      }
    }
  }
  return 0;
}

/*********************************************************************
*
*       _OnIsoCompletion
*
*  Function description
*    BULK internal completion routine for the USBH_BULK_ReadAsync and
*    USBH_BULK_WriteAsync functions.
*    Calls the user callback.
*
*  Parameters
*    pUrb     : Pointer to the completed URB.
*/
#if USBH_SUPPORT_ISO_TRANSFER
static void _OnIsoCompletion(USBH_URB * pUrb) {
  USBH_BULK_RW_CONTEXT         * pRWContext;
  BULK_EP_DATA                 * pEPData;
  USBH_ISO_REQUEST             * pIsoRequest;

  pEPData    = USBH_CTX2PTR(BULK_EP_DATA, pUrb->Header.pContext);
  pRWContext = USBH_CTX2PTR(USBH_BULK_RW_CONTEXT, pUrb->Header.pUserContext);
  //
  //  Update RWContext
  //
  if (pUrb->Header.Status != USBH_STATUS_SUCCESS) {
    //
    // The whole URB was terminated, return the final status of the URB
    //
    pRWContext->Status = pUrb->Header.Status;
    pRWContext->NumBytesTransferred = 0;
    pRWContext->Terminated = 1;
    pEPData->InUse = 0;
    DEC_REF_CNT(pEPData->pInst);
  } else {
    //
    // A single ISO transfer was finished, return data and transfer status
    //
    pIsoRequest = &pUrb->Request.IsoRequest;
    pRWContext->Status = pIsoRequest->Status;
    pRWContext->NumBytesTransferred = pIsoRequest->Length;
    pRWContext->pUserBuffer = (void *)pIsoRequest->pData;     //lint !e9005  D:105[a]
    pRWContext->Terminated = 0;
  }
  //
  // Call user function
  //
  pUrb->Header.pfOnUserCompletion(pRWContext);
}
#endif

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       USBH_BULK_Init
*
*  Function description
*    Initializes and registers the BULK device module with emUSB-Host.
*
*  Parameters
*    pInterfaceMask: Deprecated parameter. Please use
*                    USBH_BULK_AddNotification to add new interfaces masks.
*                    To be backward compatible the mask added through
*                    this parameter will be automatically added when
*                    USBH_BULK_RegisterNotification is called.
*
*  Return value
*    == USBH_STATUS_SUCCESS: Success or module already initialized.
*
*  Additional information
*    This function can be called multiple times, but only the first
*    call initializes the module. Any further calls only increase
*    the initialization counter. This is useful for cases where
*    the module is initialized from different places which
*    do not interact with each other, To de-initialize
*    the module USBH_BULK_Exit has to be called
*    the same number of times as this function was called.
*/
USBH_STATUS USBH_BULK_Init(const USBH_INTERFACE_MASK * pInterfaceMask) {
  if (_isInited++ == 0) {
    USBH_MEMSET(&USBH_BULK_Global, 0, sizeof(USBH_BULK_Global));
    if (pInterfaceMask != NULL) {
      USBH_BULK_Global.InitInterfaceMask = *pInterfaceMask;
    }
  }
  return USBH_STATUS_SUCCESS;
}

/*********************************************************************
*
*       USBH_BULK_Exit
*
*  Function description
*    Unregisters and de-initializes the BULK device module from emUSB-Host.
*
*  Additional information
*    Before this function is called any notifications added via
*    USBH_BULK_AddNotification() must be removed
*    via USBH_BULK_RemoveNotification().
*    Has to be called the same number of times USBH_BULK_Init was
*    called in order to de-initialize the module.
*    This function will release resources that were used by this
*    device driver. It has to be called if the application is closed.
*    This has to be called before USBH_Exit() is called. No more
*    functions of this module may be called after calling
*    USBH_BULK_Exit(). The only exception is USBH_BULK_Init(),
*    which would in turn re-init the module and allow further calls.
*/
void USBH_BULK_Exit(void) {
  USBH_BULK_INST * pInst;
  unsigned         i;
  BULK_EP_DATA   * pEP;
  USBH_NOTIFICATION_HOOK *  p;
  USBH_NOTIFICATION_HOOK *  pNext;

  USBH_LOG((USBH_MCAT_BULK, "USBH_BULK_Exit"));
  if (--_isInited != 0) {
    return;
  }
  pInst = USBH_BULK_Global.pFirst;
  while (pInst != NULL) {   // Iterate over all instances
    while (pInst->IsOpened != 0) {
      --pInst->IsOpened;
      DEC_REF_CNT(pInst);
    }
    pEP = pInst->pEndpoints;
    for (i = 0; i < pInst->NumEPs; i++) {
      //
      // If the EP is in use, we have to abort the EP.
      //
      if (pEP->InUse != 0) {
        (void)_AbortEP(pInst, pEP);
      }
      pEP++;
    }
    if (pInst->RefCnt > 0) {
      DEC_REF_CNT(pInst);       // Initial RefCount
    }
    USBH_StartTimer(&pInst->RemovalTimer, USBH_BULK_REMOVAL_TIMEOUT);
    pInst = pInst->pNext;
  }
  //
  // Remove any registered hooks.
  //
  p = USBH_BULK_Global.pFirstNotiHook;
  while (p != NULL) {
    pNext = p->pNext;
    (void)USBH_BULK_RemoveNotification(p);
    p = pNext;
  }
}

/*********************************************************************
*
*       USBH_BULK_Open
*
*  Function description
*    Opens a device interface given by an index.
*
*  Parameters
*    Index   : Index of the interface that shall be opened.
*              In general this means: the first connected interface is 0,
*              second interface is 1 etc.
*
*  Return value
*    == USBH_BULK_INVALID_HANDLE     : Device not available or removed.
*    != USBH_BULK_INVALID_HANDLE     : Handle to a BULK device
*
*  Additional information
*    The index of a new connected device is provided to the callback function
*    registered with USBH_BULK_AddNotification().
*/
USBH_BULK_HANDLE USBH_BULK_Open(unsigned Index) {
  USBH_BULK_INST    * pInst;
  USBH_BULK_HANDLE    Handle;

  Handle = USBH_BULK_INVALID_HANDLE;
  for (pInst = USBH_BULK_Global.pFirst; pInst != NULL; pInst = pInst->pNext) {
    if (Index == pInst->DevIndex) {
      break;
    }
  }
  if (pInst == NULL) {
    goto End;
  }
  //
  // Device found
  //
  if (INC_REF_CNT(pInst) != USBH_STATUS_SUCCESS) {
    goto End;
  }
  if (pInst->IsOpened == 0 && pInst->NumEPs == 0u) {
    if (_GetEndpointInfo(pInst) != 0) {
      DEC_REF_CNT(pInst);
      goto End;
    }
  }
  pInst->IsOpened++;
  Handle = pInst->Handle;
End:
  return Handle;
}

/*********************************************************************
*
*       USBH_BULK_Close
*
*  Function description
*    Closes a handle to an opened device.
*
*  Parameters
*    hDevice    :  Handle to an open device returned by USBH_BULK_Open().
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_BULK_Close(USBH_BULK_HANDLE hDevice) {
  USBH_BULK_INST * pInst;

  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    if (pInst->IsOpened == 0) {
      return USBH_STATUS_NOT_OPENED;
    }
    if (--pInst->IsOpened == 0) {
      pInst->AllowShortRead = 0;
    }
    DEC_REF_CNT(pInst);
    return USBH_STATUS_SUCCESS;
  }
  return USBH_STATUS_DEVICE_REMOVED;
}

/*********************************************************************
*
*       USBH_BULK_Write
*
*  Function description
*    Writes data to the BULK device. The function blocks until all data
*    has been written or until the timeout has been reached.
*
*  Parameters
*    hDevice          : Handle to an open device returned by USBH_BULK_Open().
*    EPAddr           : Endpoint address (can be retrieved via USBH_BULK_GetEndpointInfo()).
*                       Must be an OUT endpoint.
*    pData            : Pointer to data to be sent.
*    NumBytes         : Number of bytes to send.
*    pNumBytesWritten : Pointer to a variable  which receives the number
*                       of bytes written to the device. Can be NULL.
*    Timeout          : Timeout in ms. 0 means infinite timeout.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*
*  Additional information
*    If the function returns an error code (including USBH_STATUS_TIMEOUT) it already may
*    have written part of the data. The number of bytes written successfully is always
*    stored in the variable pointed to by pNumBytesWritten.
*/
USBH_STATUS USBH_BULK_Write(USBH_BULK_HANDLE hDevice, U8 EPAddr, const U8 * pData, U32 NumBytes, U32 * pNumBytesWritten, U32 Timeout) {
  USBH_BULK_INST * pInst;
  USBH_STATUS     Status;
  BULK_EP_DATA   * pEPData;
  U32             BytesAtOnce;
  U32             NumBytesWritten;
  USBH_FUNCTION   Function;

  if (pNumBytesWritten != NULL) {
    *pNumBytesWritten = 0;
  }
  pInst = _h2p(hDevice);
  if (pInst == NULL) {
    return USBH_STATUS_INVALID_HANDLE;
  }
  if (pInst->IsOpened == 0) {
    return USBH_STATUS_NOT_OPENED;
  }
  if ((EPAddr & 0x80u) != 0u) {
    return USBH_STATUS_INVALID_PARAM;
  }
  Status = _GetEPData(pInst, EPAddr, &pEPData, &Function);
  if (Status != USBH_STATUS_SUCCESS) {
    return Status;
  }
  if (pEPData->EPType == USB_EP_TYPE_ISO) {
    return USBH_STATUS_ENDPOINT_INVALID;
  }
  NumBytesWritten = 0;
  do {
    BytesAtOnce = NumBytes;
    if (BytesAtOnce > pEPData->MaxTransferSize) {
      BytesAtOnce = pEPData->MaxTransferSize;
    }
    pEPData->Urb.Header.pContext                 = pInst;
    pEPData->Urb.Header.Function                 = Function;
    pEPData->Urb.Request.BulkIntRequest.Endpoint = EPAddr;
    pEPData->Urb.Request.BulkIntRequest.pBuffer  = (U8 *)pData;       //lint !e9005 D:105[a]
    pEPData->Urb.Request.BulkIntRequest.Length   = BytesAtOnce;
    Status = INC_REF_CNT(pInst);
    if (Status == USBH_STATUS_SUCCESS) {
      Status = _SubmitUrbAndWait(pInst, pEPData, Timeout);
      DEC_REF_CNT(pInst);
      if (Status == USBH_STATUS_SUCCESS || Status == USBH_STATUS_TIMEOUT) {
        U32 BytesWritten;

        BytesWritten     = pEPData->Urb.Request.BulkIntRequest.Length;
        NumBytes        -= BytesWritten;
        pData           += BytesWritten;
        NumBytesWritten += BytesWritten;
      }
    }
  } while(NumBytes > 0u && Status == USBH_STATUS_SUCCESS);
  switch (Status) {
  //
  //  We are done in case of a time-out
  //
  case USBH_STATUS_TIMEOUT:
    if (NumBytes == 0u) {
      //
      // All bytes are written successfully, there is no need to report a timeout.
      //
      Status = USBH_STATUS_SUCCESS;
    }
    //lint -fallthrough
    //lint -e{9090} D:102[b]
  case USBH_STATUS_SUCCESS:
    if (pNumBytesWritten != NULL) {
      *pNumBytesWritten = NumBytesWritten;
    }
    break;
  case USBH_STATUS_STALL:
    //
    // We received a stall, remove that stall state and return that status back to application
    //
    _ResetPipe(pInst, EPAddr);
    break;
  default:
    //
    // In any other case, output a warning.
    //
    USBH_WARN((USBH_MCAT_BULK, "USBH_BULK_Write failed, Status = %s", USBH_GetStatusStr(Status)));
    break;
  }
  pEPData->InUse = 0;
  return Status;
}

/*********************************************************************
*
*       USBH_BULK_Receive
*
*  Function description
*    Reads one packet from the device. The size of the buffer provided by the caller must
*    be at least the maximum packet size of the endpoint referenced.
*    The maximum packet size of the endpoint can be retrieved using USBH_BULK_GetEndpointInfo().
*
*  Parameters
*    hDevice        : Handle to an open device returned by USBH_BULK_Open().
*    EPAddr         : Endpoint address (can be retrieved via USBH_BULK_GetEndpointInfo()).
*                     Must be an IN endpoint.
*    pData          : Pointer to a buffer to store the data read.
*    pNumBytesRead  : Pointer to a variable  which receives the number
*                     of bytes read from the device.
*    Timeout        : Timeout in ms. 0 means infinite timeout.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*
*  Additional information
*    This function does not access the buffer used by the function USBH_BULK_Read().
*    Data contained in this buffer are not returned by USBH_BULK_Receive().
*    Intermixing calls to USBH_BULK_Read() and USBH_BULK_Receive() for the same endpoint
*    should be avoided or used with care.
*/
USBH_STATUS USBH_BULK_Receive(USBH_BULK_HANDLE hDevice, U8 EPAddr, U8 * pData, U32 * pNumBytesRead, U32 Timeout) {
  USBH_BULK_INST * pInst;
  USBH_STATUS      Status;
  BULK_EP_DATA   * pEPData;
  USBH_FUNCTION    Function;

  *pNumBytesRead = 0;
  pInst = _h2p(hDevice);
  if (pInst == NULL) {
    return USBH_STATUS_INVALID_HANDLE;
  }
  if (pInst->IsOpened == 0) {
    return USBH_STATUS_NOT_OPENED;
  }
  if ((EPAddr & 0x80u) == 0u) {
    return USBH_STATUS_INVALID_PARAM;
  }
  Status = _GetEPData(pInst, EPAddr, &pEPData, &Function);
  if (Status != USBH_STATUS_SUCCESS) {
    return Status;
  }
  if (pEPData->EPType == USB_EP_TYPE_ISO) {
    Status = USBH_STATUS_ENDPOINT_INVALID;
    goto ReadEnd;
  }
  //
  // Fill URB structure
  //
  USBH_MEMSET(&pEPData->Urb, 0, sizeof(USBH_URB));
  pEPData->Urb.Header.Function = Function;
  pEPData->Urb.Request.BulkIntRequest.Endpoint = pEPData->EPAddr;
  pEPData->Urb.Request.BulkIntRequest.pBuffer  = pData;
  pEPData->Urb.Request.BulkIntRequest.Length   = pEPData->MaxPacketSize;
  //
  // Send and wait until data have been received.
  // In case of an error the function will also return
  //
  Status = INC_REF_CNT(pInst);
  if (Status == USBH_STATUS_SUCCESS) {
    Status = _SubmitUrbAndWait(pInst, pEPData, Timeout);
    DEC_REF_CNT(pInst);
  }
  switch (Status) {
  case USBH_STATUS_SUCCESS:
    *pNumBytesRead = pEPData->Urb.Request.BulkIntRequest.Length;
    break;
  case USBH_STATUS_STALL:
    //
    // We received a stall, remove that status and return that status back to application
    //
    _ResetPipe(pInst, pEPData->EPAddr);
    break;
  case USBH_STATUS_TIMEOUT: // No warning for timeout
    USBH_LOG((USBH_MCAT_BULK, "USBH_BULK_Receive failed, Status = %s", USBH_GetStatusStr(Status)));
    break;
  default:
    USBH_WARN((USBH_MCAT_BULK, "USBH_BULK_Receive failed, Status = %s", USBH_GetStatusStr(Status)));
    break;
  }
ReadEnd:
  pEPData->InUse = 0;
  return Status;
}

/*********************************************************************
*
*       USBH_BULK_Read
*
*  Function description
*    Reads from the BULK device. Depending of the ShortRead mode (see USBH_BULK_AllowShortRead()),
*    this function will either return as soon as data is available or
*    all data have been read from the device.
*    This function will also return when a set timeout is expired,
*    whatever comes first.
*
*    The USB stack can only read complete packets from the USB device.
*    If the size of a received packet exceeds NumBytes then all data that does not
*    fit into the callers buffer (pData) is stored in an internal buffer and
*    will be returned by the next call to USBH_BULK_Read(). See also USBH_BULK_GetNumBytesInBuffer().
*
*    To read a null packet, set pData = NULL and NumBytes = 0.
*    For this, the internal buffer must be empty.
*
*  Parameters
*    hDevice        : Handle to an open device returned by USBH_BULK_Open().
*    EPAddr         : Endpoint address (can be retrieved via USBH_BULK_GetEndpointInfo()).
*                     Must be an IN endpoint.
*    pData          : Pointer to a buffer to store the read data.
*    NumBytes       : Number of bytes to be read from the device.
*    pNumBytesRead  : Pointer to a variable  which receives the number
*                     of bytes read from the device. Can be NULL.
*    Timeout        : Timeout in ms. 0 means infinite timeout.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*
*  Additional information
*    If the function returns an error code (including USBH_STATUS_TIMEOUT) it already may
*    have read part of the data. The number of bytes read successfully is always
*    stored in the variable pointed to by pNumBytesRead.
*/
USBH_STATUS USBH_BULK_Read(USBH_BULK_HANDLE hDevice, U8 EPAddr, U8 * pData, U32 NumBytes, U32 * pNumBytesRead, U32 Timeout) {
  USBH_BULK_INST * pInst;
  USBH_STATUS      Status;
  U32              NumBytesTotal;
  U32              NumBytesRead;
  U32              NumBytes2Read;
  U32              NumBytesTransfered;
  U32              NumBytes2Copy;
  USBH_TIME        ExpiredTime;
  U8             * pBuf;
  BULK_EP_DATA   * pEPData;
  USBH_FUNCTION    Function;

  if (pNumBytesRead != NULL) {
    *pNumBytesRead = 0;
  }
  pInst = _h2p(hDevice);
  if (pInst == NULL) {
    return USBH_STATUS_INVALID_HANDLE;
  }
  if (pInst->IsOpened == 0) {
    return USBH_STATUS_NOT_OPENED;
  }
  if ((EPAddr & 0x80u) == 0u ||
      (pData == NULL && NumBytes != 0u)) {
    return USBH_STATUS_INVALID_PARAM;
  }
  Status = _GetEPData(pInst, EPAddr, &pEPData, &Function);
  if (Status != USBH_STATUS_SUCCESS) {
    return Status;
  }
  if (pEPData->EPType == USB_EP_TYPE_ISO) {
    Status = USBH_STATUS_ENDPOINT_INVALID;
    goto ReadEnd;
  }
  if (pEPData->RingBuffer.pData == NULL) {
    pBuf = (U8 *)USBH_TRY_MALLOC(pEPData->MaxPacketSize);
    if (pBuf == NULL) {
      USBH_WARN((USBH_MCAT_BULK, "Buffer allocation failed."));
      Status = USBH_STATUS_MEMORY;
      goto ReadEnd;
    }
    USBH_BUFFER_Init(&pEPData->RingBuffer, pBuf, pEPData->MaxPacketSize);
  }
  if (pEPData->pInBuffer == NULL) {
    pEPData->pInBuffer = (U8 *)USBH_TRY_MALLOC(pEPData->MaxPacketSize);
    if (pEPData->pInBuffer == NULL) {
      USBH_WARN((USBH_MCAT_BULK, "Buffer allocation failed."));
      Status = USBH_STATUS_MEMORY;
      goto ReadEnd;
    }
  }
  NumBytesTotal = NumBytes;
  if (pData == NULL) {
    //
    // Reading a NULL packet is possible only if the buffer is empty.
    // (A non-zero-length packet may be received).
    //
    if (pEPData->RingBuffer.NumBytesIn != 0u) {
      Status = USBH_STATUS_INTERNAL_BUFFER_NOT_EMPTY;
      goto ReadEnd;
    }
  } else {
    NumBytesTransfered = USBH_BUFFER_Read(&pEPData->RingBuffer, pData, NumBytesTotal);
    if (NumBytesTransfered != 0u) {
      NumBytesTotal -= NumBytesTransfered;
      pData += NumBytesTransfered;
      if (pNumBytesRead != NULL) {
        *pNumBytesRead = NumBytesTransfered;
      }
    }
    if (NumBytesTotal == 0u) {
      Status = USBH_STATUS_SUCCESS;
      goto ReadEnd;
    }
  }
  ExpiredTime = USBH_TIME_CALC_EXPIRATION(Timeout);
  for(;;) {
    if (Timeout != 0u && USBH_TIME_IS_EXPIRED(ExpiredTime)) {
      Status = USBH_STATUS_TIMEOUT;
      goto ReadEnd;
    }
    //
    // Check whether we can use the user buffer directly to read data into.
    // This is possible if the buffer is a multiple of MaxPacketSize.
    //
    if (pData != NULL && (NumBytesTotal % pEPData->MaxPacketSize) == 0u) {
      pBuf = pData;
      NumBytes2Read = USBH_MIN(NumBytesTotal, pEPData->MaxTransferSize);
    } else {
      pBuf = pEPData->pInBuffer;
      NumBytes2Read = pEPData->MaxPacketSize;
    }
    //
    // Fill URB structure
    //
    USBH_MEMSET(&pEPData->Urb, 0, sizeof(USBH_URB));
    pEPData->Urb.Header.Function = Function;
    pEPData->Urb.Request.BulkIntRequest.Endpoint = pEPData->EPAddr;
    pEPData->Urb.Request.BulkIntRequest.pBuffer  = pBuf;
    pEPData->Urb.Request.BulkIntRequest.Length   = NumBytes2Read;
    //
    // Send and wait until data have been received.
    // In case of an error the function will also return
    //
    Status = INC_REF_CNT(pInst);
    if (Status == USBH_STATUS_SUCCESS) {
      Status = _SubmitUrbAndWait(pInst, pEPData, Timeout);
      DEC_REF_CNT(pInst);
    }
    NumBytesRead = pEPData->Urb.Request.BulkIntRequest.Length;
    if (Status == USBH_STATUS_SUCCESS || (Status == USBH_STATUS_TIMEOUT && NumBytesRead != 0u)) {
      //
      // On USBH_STATUS_TIMEOUT, we may still received some data.
      // So timeout is ignored here.
      // Timeout condition is checked via 'TimeStart' at the top of the loop.
      //
      Status = USBH_STATUS_SUCCESS;
      //
      // Check how many bytes have been received.
      //
      if (pBuf == pEPData->pInBuffer) {
        if (NumBytesTotal == 0u) {
          USBH_BUFFER_Write(&pEPData->RingBuffer, pEPData->pInBuffer, NumBytesRead);
          goto ReadEnd;
        }
        NumBytes2Copy = USBH_MIN(NumBytesRead, NumBytesTotal);
        USBH_MEMCPY(pData, pEPData->pInBuffer, NumBytes2Copy);
        if (pNumBytesRead != NULL) {
          *pNumBytesRead += NumBytes2Copy;
        }
        pData += NumBytes2Copy;
        NumBytesTotal -= NumBytes2Copy;
        NumBytesRead -= NumBytes2Copy;
        if (NumBytesRead != 0u) {
          USBH_BUFFER_Write(&pEPData->RingBuffer, &pEPData->pInBuffer[NumBytes2Copy], NumBytesRead);
        }
      } else {
        pData += NumBytesRead;
        NumBytesTotal -= NumBytesRead;
        if (pNumBytesRead != NULL) {
          *pNumBytesRead += NumBytesRead;
        }
      }
      if (pInst->AllowShortRead != 0) {
        goto ReadEnd;
      }
      if (NumBytesTotal == 0u) {
        break;
      }
    } else {
      if (Status == USBH_STATUS_STALL) {
        //
        // We received a stall, remove that status and return that status back to application
        //
        _ResetPipe(pInst, pEPData->EPAddr);
      } else {
        //
        // A timeout status can be intended by the application. In any other case, output a warning.
        //
        if (Status != USBH_STATUS_TIMEOUT) {
          USBH_WARN((USBH_MCAT_BULK, "USBH_BULK_Read failed, Status = %s", USBH_GetStatusStr(Status)));
        }
      }
      break;
    }
  }
ReadEnd:
  pEPData->InUse = 0;
  return Status;
}

/*********************************************************************
*
*       USBH_BULK_RegisterNotification
*
*  Function description
*    (Deprecated) Sets a callback in order to be notified when a device is added or removed.
*
*  Parameters
*    pfNotification  : Pointer to a function the stack should call when a device is connected or disconnected.
*    pContext        : Pointer to a user context that is passed to the callback function.
*
*  Additional information
*    This function is deprecated, please use function USBH_BULK_AddNotification().
*/
void USBH_BULK_RegisterNotification(USBH_NOTIFICATION_FUNC * pfNotification, void * pContext) {
  (void)USBH_BULK_AddNotification(&_Hook, pfNotification, pContext, &USBH_BULK_Global.InitInterfaceMask);
}

/*********************************************************************
*
*       USBH_BULK_AddNotification
*
*  Function description
*    Adds a callback in order to be notified when a device is added or removed.
*
*  Parameters
*    pHook           : Pointer to a user provided USBH_NOTIFICATION_HOOK variable.
*    pfNotification  : Pointer to a function the stack should call when a device is connected or disconnected.
*    pContext        : Pointer to a user context that is passed to the callback function.
*    pInterfaceMask  : Pointer to a structure of type USBH_INTERFACE_MASK.
*                      NULL means that all interfaces will be forwarded to the callback.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_BULK_AddNotification(USBH_NOTIFICATION_HOOK * pHook, USBH_NOTIFICATION_FUNC * pfNotification, void * pContext, const USBH_INTERFACE_MASK * pInterfaceMask) {
  USBH_PNP_NOTIFICATION PnpNotify;
  USBH_NOTIFICATION_HANDLE Handle;
  //
  // Add BULK PnP notification, this makes sure that as soon as a device with the specific interface
  // is available we will be notified.
  //
  PnpNotify.pContext          = pHook;
  PnpNotify.pfPnpNotification = _OnDeviceNotification;
  PnpNotify.InterfaceMask     = *pInterfaceMask;
  Handle                      = USBH_RegisterPnPNotification(&PnpNotify); // Register the  PNP notification
  if (NULL == Handle) {
    USBH_WARN((USBH_MCAT_BULK, "USBH_BULK_AddNotification: USBH_RegisterPnPNotification"));
    return USBH_STATUS_MEMORY;
  }
  return USBH__AddNotification(pHook, pfNotification, pContext, &USBH_BULK_Global.pFirstNotiHook, Handle);
}

/*********************************************************************
*
*       USBH_BULK_RemoveNotification
*
*  Function description
*    Removes a callback registered through USBH_BULK_AddNotification.
*
*  Parameters
*    pHook          : Pointer to a user provided USBH_NOTIFICATION_HOOK variable.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_BULK_RemoveNotification(const USBH_NOTIFICATION_HOOK * pHook) {
  return USBH__RemoveNotification(pHook, &USBH_BULK_Global.pFirstNotiHook);
}

/*********************************************************************
*
*       USBH_BULK_GetDeviceInfo
*
*  Function description
*    Retrieves information about the BULK device.
*
*  Parameters
*    hDevice    : Handle to an open device returned by USBH_BULK_Open().
*    pDevInfo   : Pointer to a USBH_BULK_DEVICE_INFO structure that receives the information.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_BULK_GetDeviceInfo(USBH_BULK_HANDLE hDevice, USBH_BULK_DEVICE_INFO * pDevInfo) {
  USBH_BULK_INST      * pInst;
  USBH_INTERFACE_INFO   InterfaceInfo;
  USBH_STATUS           Status;
  unsigned              i;
  BULK_EP_DATA        * pEP;

  pInst = _h2p(hDevice);
  if (pInst == NULL) {
    return USBH_STATUS_INVALID_HANDLE;
  }
  if (pInst->IsOpened == 0) {
    return USBH_STATUS_NOT_OPENED;
  }
  USBH_ASSERT_PTR(pDevInfo);
  Status = USBH_GetInterfaceInfo(pInst->InterfaceID, &InterfaceInfo);
  if (Status == USBH_STATUS_SUCCESS) {
    pDevInfo->InterfaceID = pInst->InterfaceID;
    pDevInfo->VendorId    = InterfaceInfo.VendorId;
    pDevInfo->ProductId   = InterfaceInfo.ProductId;
    pDevInfo->InterfaceNo = InterfaceInfo.Interface;
    pDevInfo->Speed       = InterfaceInfo.Speed;
    pDevInfo->NumEPs      = pInst->NumEPs;
    pDevInfo->DeviceId    = InterfaceInfo.DeviceId;
    pDevInfo->Class       = InterfaceInfo.Class;
    pDevInfo->SubClass    = InterfaceInfo.SubClass;
    pDevInfo->Protocol    = InterfaceInfo.Protocol;
    pDevInfo->AlternateSetting = InterfaceInfo.AlternateSetting;
    pEP = pInst->pEndpoints;
    for (i = 0; i < pInst->NumEPs && i < USBH_BULK_MAX_NUM_EPS; i++) {
      pDevInfo->EndpointInfo[i].Addr      = pEP->EPAddr;
      pDevInfo->EndpointInfo[i].Type      = pEP->EPType;
      pDevInfo->EndpointInfo[i].Direction = pEP->EPAddr & 0x80u;
      pDevInfo->EndpointInfo[i].MaxPacketSize = pEP->MaxPacketSize;
      pEP++;
    }
  }
  return Status;
}

/*********************************************************************
*
*       USBH_BULK_GetEndpointInfo
*
*  Function description
*    Retrieves information about an endpoint of a BULK device.
*
*  Parameters
*    hDevice    : Handle to an open device returned by USBH_BULK_Open().
*    EPIndex    : Index of the EP (0 ... DevInfo.NumEPs-1)
*    pEPInfo    : Pointer to a USBH_BULK_EP_INFO structure that receives the information.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_BULK_GetEndpointInfo(USBH_BULK_HANDLE hDevice, unsigned EPIndex, USBH_BULK_EP_INFO * pEPInfo) {
  USBH_BULK_INST      * pInst;
  BULK_EP_DATA        * pEP;

  pInst = _h2p(hDevice);
  if (pInst == NULL) {
    return USBH_STATUS_INVALID_HANDLE;
  }
  if (pInst->IsOpened == 0) {
    return USBH_STATUS_NOT_OPENED;
  }
  USBH_ASSERT_PTR(pEPInfo);
  if (EPIndex >= pInst->NumEPs) {
    return USBH_STATUS_INVALID_PARAM;
  }
  pEP = pInst->pEndpoints + EPIndex;
  pEPInfo->Addr      = pEP->EPAddr;
  pEPInfo->Type      = pEP->EPType;
  pEPInfo->Direction = pEP->EPAddr & 0x80u;
  pEPInfo->MaxPacketSize = pEP->MaxPacketSize;
  return USBH_STATUS_SUCCESS;
}

/*********************************************************************
*
*       USBH_BULK_AllowShortRead
*
*  Function description
*    Enables or disables short read mode.
*    If enabled, the function USBH_BULK_Read() returns as soon as data was
*    read from the device. This allows the application to read data where the number of
*    bytes to read is undefined.
*
*  Parameters
*    hDevice        : Handle to an open device returned by USBH_BULK_Open().
*    AllowShortRead : Define whether short read mode shall be used or not.
*                     * 1 - Allow short read.
*                     * 0 - Short read mode disabled.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_BULK_AllowShortRead(USBH_BULK_HANDLE hDevice, U8 AllowShortRead) {
  USBH_BULK_INST * pInst;

  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    if (INC_REF_CNT(pInst) == USBH_STATUS_SUCCESS) {
      pInst->AllowShortRead = AllowShortRead;
      DEC_REF_CNT(pInst);
      return USBH_STATUS_SUCCESS;
    }
    return USBH_STATUS_DEVICE_REMOVED;
  }
  return USBH_STATUS_INVALID_HANDLE;
}

/*********************************************************************
*
*       USBH_BULK_GetNumBytesInBuffer
*
*  Function description
*    Gets the number of bytes in the receive buffer.
*
*    The USB stack can only read complete packets from the USB device.
*    If the size of a received packet exceeds the number of bytes requested
*    with USBH_BULK_Read(), than all data that is not returned by USBH_BULK_Read()
*    is stored in an internal buffer.
*
*    The number of bytes returned by USBH_BULK_GetNumBytesInBuffer() can be read
*    using USBH_BULK_Read() out of the buffer without a USB transaction
*    to the USB device being executed.
*
*  Parameters
*    hDevice  : Handle to an open device returned by USBH_BULK_Open().
*    EPAddr   : Endpoint address.
*    pRxBytes : Pointer to a variable which receives the number
*               of bytes in the receive buffer.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_BULK_GetNumBytesInBuffer(USBH_BULK_HANDLE hDevice, U8 EPAddr, U32 * pRxBytes) {
  USBH_BULK_INST * pInst;
  BULK_EP_DATA   * pEPData;
  unsigned         i;
  USBH_STATUS      Status;

  pInst = _h2p(hDevice);
  if (pInst == NULL) {
    return USBH_STATUS_INVALID_HANDLE;
  }
  if (pInst->IsOpened == 0) {
    return USBH_STATUS_NOT_OPENED;
  }
  if ((EPAddr & 0x80u) == 0u) {
    return USBH_STATUS_INVALID_PARAM;
  }
  if (INC_REF_CNT(pInst) != USBH_STATUS_SUCCESS) {
    return USBH_STATUS_DEVICE_REMOVED;
  }
  pEPData = pInst->pEndpoints;
  for (i = pInst->NumEPs; i > 0u; i--) {
    if (pEPData->EPAddr == EPAddr) {
      break;
    }
    pEPData++;
  }
  if (i == 0u) {
    Status = USBH_STATUS_INVALID_PARAM;
  } else {
    if (pEPData->RingBuffer.pData != NULL) {
      *pRxBytes = pEPData->RingBuffer.NumBytesIn;
    } else {
      *pRxBytes = 0;
    }
    Status = USBH_STATUS_SUCCESS;
  }
  DEC_REF_CNT(pInst);
  return Status;
}

/*********************************************************************
*
*       USBH_BULK_ReadAsync
*
*  Function description
*    Triggers a read transfer to the BULK device. The result of
*    the transfer is received through the user callback.
*    This function will return immediately while the read transfer is
*    done asynchronously.
*
*  Parameters
*    hDevice       : Handle to an open device returned by USBH_BULK_Open().
*    EPAddr        : Endpoint address. Must be an IN endpoint.
*    pBuffer       : Pointer to the buffer that receives the data
*                    from the device. Ignored for ISO transfers.
*    BufferSize    : Size of the buffer in bytes. Must be a multiple of
*                    of the maximum packet size of the USB device.
*                    Use USBH_BULK_GetMaxTransferSize() to get the maximum allowed size.
*                    Ignored for ISO transfers.
*    pfOnComplete  : Pointer to a user function of type  USBH_BULK_ON_COMPLETE_FUNC
*                    which will be called after the transfer has been completed.
*    pRWContext    : Pointer to a USBH_BULK_RW_CONTEXT structure which
*                    will be filled with data after the transfer has
*                    been completed and passed as a parameter to the
*                    pfOnComplete function. The member 'pUserContext' may be set before
*                    calling USBH_BULK_ReadAsync(). Other members need not be initialized and
*                    are set by the function USBH_BULK_ReadAsync().
*                    The memory used for this structure must be valid,
*                    until the transaction is completed.
*
*  Return value
*    == USBH_STATUS_PENDING : Success, the data transfer is queued,
*                             the user callback will be called after
*                             the transfer is finished.
*    != USBH_STATUS_PENDING : An error occurred, the transfer is not started
*                             and user callback will not be called.
*/
USBH_STATUS USBH_BULK_ReadAsync(USBH_BULK_HANDLE hDevice, U8 EPAddr, void * pBuffer, U32 BufferSize, USBH_BULK_ON_COMPLETE_FUNC * pfOnComplete, USBH_BULK_RW_CONTEXT * pRWContext) {
  USBH_BULK_INST  * pInst;
  USBH_STATUS       Status;
  BULK_EP_DATA    * pEPData;
  USBH_URB        * pUrb;
  USBH_FUNCTION     Function;

  if (pfOnComplete == NULL || pRWContext == NULL) {
    USBH_WARN((USBH_MCAT_BULK, "USBH_BULK_ReadAsync called with invalid parameters, pfOnComplete = 0x%x, pRWContext = 0x%x", pfOnComplete, pRWContext));
    return USBH_STATUS_INVALID_PARAM;
  }
  pInst = _h2p(hDevice);
  if (pInst == NULL) {
    return USBH_STATUS_INVALID_HANDLE;
  }
  if (pInst->IsOpened == 0) {
    return USBH_STATUS_NOT_OPENED;
  }
  if ((EPAddr & 0x80u) == 0u) {
    return USBH_STATUS_ENDPOINT_INVALID;
  }
  Status = _GetEPData(pInst, EPAddr, &pEPData, &Function);
  if (Status != USBH_STATUS_SUCCESS) {
    return Status;
  }
  pUrb = &pEPData->Urb;
  USBH_MEMSET(pUrb, 0, sizeof(USBH_URB));
  pUrb->Header.Function                   = Function;
#if USBH_SUPPORT_ISO_TRANSFER
  if (Function == USBH_FUNCTION_ISO_REQUEST) {
    pUrb->Request.IsoRequest.Endpoint     = pEPData->EPAddr;
    pUrb->Header.pfOnCompletion           = _OnIsoCompletion;
  } else
#endif
  {
    if (BufferSize == 0u || BufferSize % pEPData->MaxPacketSize != 0u) {
      USBH_WARN((USBH_MCAT_BULK, "BufferSize (%d) is not a multiple of MaxPacketSize(%d).", BufferSize, pEPData->MaxPacketSize));
      return USBH_STATUS_INVALID_PARAM;
    }
    if (BufferSize > pEPData->MaxTransferSize) {
      USBH_WARN((USBH_MCAT_BULK, "USBH_BULK_ReadAsync BufferSize (%d) too large, max possible is %d", BufferSize, pEPData->MaxTransferSize));
      Status = USBH_STATUS_XFER_SIZE;
      goto End;
    }
    pRWContext->pUserBuffer               = pBuffer;
    pRWContext->UserBufferSize            = BufferSize;
    pUrb->Request.BulkIntRequest.Endpoint = pEPData->EPAddr;
    pUrb->Request.BulkIntRequest.pBuffer  = pBuffer;
    pUrb->Request.BulkIntRequest.Length   = BufferSize;
    pUrb->Header.pfOnCompletion           = _OnAsyncCompletion;
  }
  pUrb->Header.pContext                   = pEPData;
  pUrb->Header.pfOnUserCompletion         = (USBH_ON_COMPLETION_USER_FUNC *)pfOnComplete;   //lint !e9074 !e9087  D:104
  pUrb->Header.pUserContext               = pRWContext;
  //
  // Send the URB
  //
  Status = INC_REF_CNT(pInst);
  if (Status == USBH_STATUS_SUCCESS) {
    Status = USBH_SubmitUrb(pInst->hInterface, pUrb);
    if (Status != USBH_STATUS_PENDING) {
      DEC_REF_CNT(pInst);
    }
  }
End:
  if (Status != USBH_STATUS_PENDING) {
    pEPData->InUse = 0;
  }
  return Status;
}

/*********************************************************************
*
*       USBH_BULK_WriteAsync
*
*  Function description
*    Triggers a write transfer to the BULK device. The result of
*    the transfer is received through the user callback.
*    This function will return immediately while the write transfer is
*    done asynchronously.
*
*  Parameters
*    hDevice      : Handle to an open device returned by USBH_BULK_Open().
*    EPAddr       : Endpoint address. Must be an OUT endpoint.
*    pBuffer      : Pointer to a buffer which holds the data. Ignored for ISO transfers.
*    BufferSize   : Number of bytes to write.
*                   Use USBH_BULK_GetMaxTransferSize() to get the maximum allowed size.
*                   Ignored for ISO transfers.
*    pfOnComplete : Pointer to a user function of type USBH_BULK_ON_COMPLETE_FUNC
*                   which will be called after the transfer has been completed.
*    pRWContext   : Pointer to a USBH_BULK_RW_CONTEXT structure which
*                   will be filled with data after the transfer has
*                   been completed and passed as a parameter to
*                   the pfOnComplete function.
*                   pfOnComplete function. The member 'pUserContext' may be set before
*                   calling USBH_BULK_WriteAsync(). Other members need not be initialized and
*                   are set by the function USBH_BULK_WriteAsync().
*                   The memory used for this structure must be valid,
*                   until the transaction is completed.
*
*  Return value
*    == USBH_STATUS_PENDING : Success, the data transfer is queued,
*                             the user callback will be called after
*                             the transfer is finished.
*    != USBH_STATUS_PENDING : An error occurred, the transfer is not started
*                             and user callback will not be called.
*/
USBH_STATUS USBH_BULK_WriteAsync(USBH_BULK_HANDLE hDevice, U8 EPAddr, void * pBuffer, U32 BufferSize, USBH_BULK_ON_COMPLETE_FUNC * pfOnComplete, USBH_BULK_RW_CONTEXT * pRWContext) {
  USBH_BULK_INST  * pInst;
  USBH_STATUS       Status;
  BULK_EP_DATA    * pEPData;
  USBH_URB        * pUrb;
  USBH_FUNCTION     Function;

  if (pfOnComplete == NULL || pRWContext == NULL) {
    USBH_WARN((USBH_MCAT_BULK, "USBH_BULK_WriteAsync called with invalid parameters, pfOnComplete = 0x%x, pRWContext = 0x%x", pfOnComplete, pRWContext));
    return USBH_STATUS_INVALID_PARAM;
  }
  pInst = _h2p(hDevice);
  if (pInst == NULL) {
    return USBH_STATUS_INVALID_HANDLE;
  }
  if (pInst->IsOpened == 0) {
    return USBH_STATUS_NOT_OPENED;
  }
  if ((EPAddr & 0x80u) != 0u) {
    return USBH_STATUS_ENDPOINT_INVALID;
  }
  Status = _GetEPData(pInst, EPAddr, &pEPData, &Function);
  if (Status != USBH_STATUS_SUCCESS) {
    return Status;
  }
  pUrb = &pEPData->Urb;
  USBH_MEMSET(pUrb, 0, sizeof(USBH_URB));
  pUrb->Header.Function                   = Function;
#if USBH_SUPPORT_ISO_TRANSFER
  if (Function == USBH_FUNCTION_ISO_REQUEST) {
    pUrb->Request.IsoRequest.Endpoint     = pEPData->EPAddr;
    pUrb->Header.pfOnCompletion           = _OnIsoCompletion;
  } else
#endif
  {
    if (BufferSize > pEPData->MaxTransferSize) {
      USBH_WARN((USBH_MCAT_BULK, "USBH_BULK_WriteAsync BufferSize (%d) too large, max possible is %d", BufferSize, pEPData->MaxTransferSize));
      Status = USBH_STATUS_XFER_SIZE;
      goto End;
    }
    pRWContext->pUserBuffer               = pBuffer;
    pRWContext->UserBufferSize            = BufferSize;
    pUrb->Request.BulkIntRequest.Endpoint = pEPData->EPAddr;
    pUrb->Request.BulkIntRequest.pBuffer  = pBuffer;
    pUrb->Request.BulkIntRequest.Length   = BufferSize;
    pUrb->Header.pfOnCompletion           = _OnAsyncCompletion;
  }
  pUrb->Header.pContext                   = pEPData;
  pUrb->Header.pfOnUserCompletion         = (USBH_ON_COMPLETION_USER_FUNC *)pfOnComplete;     //lint !e9074 !e9087  D:104
  pUrb->Header.pUserContext               = pRWContext;
  //
  // Send the URB
  // In case of an error the function will also return
  //
  Status = INC_REF_CNT(pInst);
  if (Status == USBH_STATUS_SUCCESS) {
    Status = USBH_SubmitUrb(pInst->hInterface, pUrb);
    if (Status != USBH_STATUS_PENDING) {
      DEC_REF_CNT(pInst);
    }
  }
End:
  if (Status != USBH_STATUS_PENDING) {
    pEPData->InUse = 0;
  }
  pRWContext->Status = Status;
  return Status;
}

/*********************************************************************
*
*       USBH_BULK_IsoDataCtrl
*
*  Function description
*    Acknowledge ISO data received from an IN EP or provide data for OUT EPs.
*
*    On order to start ISO OUT transfers after calling USBH_BULK_WriteAsync(), initially
*    the output packet queue must be filled. For that purpose this function
*    must be called repeatedly until is does not return USBH_STATUS_NEED_MORE_DATA any more.
*
*  Parameters
*    hDevice       : Handle to an open device returned by USBH_BULK_Open().
*    EPAddr        : Endpoint address.
*    pIsoData      : ISO data structure.
*
*  Return value
*    USBH_STATUS_SUCCESS or USBH_STATUS_NEED_MORE_DATA on success or error code on failure.
*/
USBH_STATUS USBH_BULK_IsoDataCtrl(USBH_BULK_HANDLE hDevice, U8 EPAddr, USBH_ISO_DATA_CTRL *pIsoData) {
  USBH_BULK_INST  * pInst;
  BULK_EP_DATA    * pEPData;
  unsigned          i;

  pInst = _h2p(hDevice);
  if (pInst == NULL) {
    return USBH_STATUS_INVALID_HANDLE;
  }
  if (pInst->IsOpened == 0) {
    return USBH_STATUS_NOT_OPENED;
  }
  pEPData = pInst->pEndpoints;
  for (i = pInst->NumEPs; i > 0u; i--) {
    if (pEPData->EPAddr == EPAddr) {
      break;
    }
    pEPData++;
  }
  if (i == 0u) {
    return USBH_STATUS_INVALID_PARAM;
  }
  if (pEPData->EPType != USB_EP_TYPE_ISO) {
    return USBH_STATUS_ENDPOINT_INVALID;
  }
  if (pEPData->InUse == 0) {
    return USBH_STATUS_INVALID_PARAM;
  }
  return USBH_IsoDataCtrl(&pEPData->Urb, pIsoData);
}

/*********************************************************************
*
*       USBH_BULK_GetSerialNumber
*
*  Function description
*    Get the serial number of a BULK device.
*    The serial number is in UNICODE format, not zero terminated.
*
*  Parameters
*    hDevice            : Handle to an open device returned by USBH_BULK_Open().
*    BuffSize           : Size of the buffer pointed to by pSerialNumber in bytes.
*    pSerialNumber      : Pointer to a buffer that receives the serial number.
*    pSerialNumberSize  : [OUT] Actual size of the returned serial number in bytes.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_BULK_GetSerialNumber(USBH_BULK_HANDLE hDevice, U32 BuffSize, U8 *pSerialNumber, U32 *pSerialNumberSize) {
  USBH_BULK_INST * pInst;
  USBH_STATUS     Status;

  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    Status = USBH_GetInterfaceSerial(pInst->InterfaceID, BuffSize, pSerialNumber, pSerialNumberSize);
  } else {
    Status = USBH_STATUS_INVALID_HANDLE;
  }
  return Status;
}

/*********************************************************************
*
*       USBH_BULK_Cancel
*
*  Function description
*    Cancels a running transfer.
*
*  Parameters
*    hDevice    :  Handle to an open device returned by USBH_BULK_Open().
*    EPAddr     :  Endpoint address.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*
*  Additional information
*    This function can be used to cancel a transfer which was initiated
*    by USBH_BULK_ReadAsync()/USBH_BULK_WriteAsync() or USBH_BULK_Read()/USBH_BULK_Write().
*    In the later case this function has to be called from a different task.
*/
USBH_STATUS USBH_BULK_Cancel(USBH_BULK_HANDLE hDevice, U8 EPAddr) {
  USBH_BULK_INST      * pInst;

  pInst = _h2p(hDevice);
  if (pInst == NULL) {
    return USBH_STATUS_INVALID_HANDLE;
  }
  return _AbortEPAddr(pInst, EPAddr);
}

/*********************************************************************
*
*       USBH_BULK_SetupRequest
*
*  Function description
*    Sends a specific request (class vendor etc) to the device.
*
*  Parameters
*    hDevice       : Handle to an open device returned by USBH_BULK_Open().
*    RequestType   : This parameter is a bitmap containing the following values:
*                    * bit 7 transfer direction:
*                    * 0 = OUT (Host to Device)
*                    * 1 = IN (Device to Host)
*                    * bits 6..5 request type:
*                    * 0 = Standard
*                    * 1 = Class
*                    * 2 = Vendor
*                    * 3 = Reserved
*                    * bits 4..0 recipient:
*                    * 0 = Device
*                    * 1 = Interface
*                    * 2 = Endpoint
*                    * 3 = Other
*    Request       : Request code in the setup request.
*    wValue        : wValue in the setup request.
*    wIndex        : wIndex in the setup request.
*    pData         : Additional data for the setup request.
*    pNumBytesData : * [IN] Number of bytes to be received/sent in pData.
*                    * [OUT] Number of bytes processed.
*    Timeout       : Timeout in ms. 0 means infinite timeout.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*
*  Additional information
*    wLength which is normally part of the setup packet will be determined given by the pNumBytes and pData.
*    In case no pBuffer is given, wLength will be 0.
*/
USBH_STATUS USBH_BULK_SetupRequest(USBH_BULK_HANDLE hDevice, U8 RequestType, U8 Request, U16 wValue, U16 wIndex, void * pData, U32 * pNumBytesData, U32 Timeout) {
  USBH_BULK_INST * pInst;
  USBH_STATUS     Status;

  pInst = _h2p(hDevice);
  if (pInst == NULL) {
    return USBH_STATUS_INVALID_HANDLE;
  }
  if (pInst->IsOpened == 0) {
    return USBH_STATUS_NOT_OPENED;
  }
  Status = _SendControlRequest(pInst, RequestType, Request, wValue, wIndex, pData, pNumBytesData, Timeout);
  if (Status != USBH_STATUS_SUCCESS) {
    //
    // In any other case, output a warning.
    //
    USBH_WARN((USBH_MCAT_BULK, "USBH_BULK_SetupRequest failed, Status = %s", USBH_GetStatusStr(Status)));
  }
  return Status;
}

/*********************************************************************
*
*       USBH_BULK_SetAlternateInterface
*
*  Function description
*    Changes the alternative interface to either the interface which
*    enables data communication or to the one which disabled it.
*
*  Parameters
*    hDevice              : Handle to an open device returned by USBH_BULK_Open().
*    AltInterfaceSetting  : Number of the alternate setting to select.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_BULK_SetAlternateInterface(USBH_BULK_HANDLE hDevice, U8 AltInterfaceSetting) {
  USBH_BULK_INST      * pInst;
  USBH_STATUS           Status;
  unsigned              CurrentAltInt;
  USBH_URB            * pUrb;
  BULK_EP_DATA        * pEPData;

  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    Status = USBH_GetInterfaceCurrAltSetting(pInst->hInterface, &CurrentAltInt);
    if (Status == USBH_STATUS_SUCCESS) {
      if (CurrentAltInt != AltInterfaceSetting) {
        pEPData = &pInst->Control;
        pUrb    = &pEPData->Urb;
        USBH_MEMSET(pUrb, 0, sizeof(*pUrb));
        pUrb->Header.Function = USBH_FUNCTION_SET_INTERFACE;
        pUrb->Request.SetInterface.AlternateSetting = AltInterfaceSetting;
        Status = INC_REF_CNT(pInst);
        if (Status == USBH_STATUS_SUCCESS) {
          Status = _SubmitUrbAndWait(pInst, pEPData, USBH_BULK_EP0_TIMEOUT);
          DEC_REF_CNT(pInst);
          if (Status == USBH_STATUS_SUCCESS) {
            pInst->NumEPs = 0;
            if (pInst->pEndpoints != NULL) {
              USBH_FREE(pInst->pEndpoints);
            }
            (void)_GetEndpointInfo(pInst);
          }
        }
      }
    }
    return Status;
  }
  return USBH_STATUS_INVALID_HANDLE;
}

/*********************************************************************
*
*       USBH_BULK_GetInterfaceHandle
*
*  Function description
*    Return the handle to the (open) USB interface. Can be used to
*    call USBH core functions like USBH_GetStringDescriptor().
*
*  Parameters
*    hDevice      : Handle to an open device returned by USBH_BULK_Open().
*
*  Return value
*    Handle to an open interface.
*/
USBH_INTERFACE_HANDLE USBH_BULK_GetInterfaceHandle(USBH_BULK_HANDLE hDevice) {
  USBH_BULK_INST      * pInst;

  pInst = _h2p(hDevice);
  USBH_ASSERT_PTR(pInst);
  if (pInst != NULL) {
    return pInst->hInterface;
  }
  USBH_WARN((USBH_MCAT_BULK, "An invalid bulk device handle was specified!"));
  return NULL;
}

/*********************************************************************
*
*       USBH_BULK_GetIndex
*
*  Function description
*    Return an index that can be used for call to USBH_BULK_Open()
*    for a given interface ID.
*
*  Parameters
*    InterfaceID:    Id of the interface.
*
*  Return value
*    >= 0: Index of the BULK interface.
*    <  0: InterfaceID not found.
*/
int USBH_BULK_GetIndex(USBH_INTERFACE_ID InterfaceID) {
  USBH_BULK_INST      * pInst;

  pInst = USBH_BULK_Global.pFirst;
  while (pInst != NULL) {
    if (pInst->InterfaceID == InterfaceID) {
      return pInst->DevIndex;
    }
    pInst = pInst->pNext;
  }
  return -1;
}


/*********************************************************************
*
*       USBH_BULK_GetMaxTransferSize
*
*  Function description
*    Return the maximum transfer size allowed for the USBH_BULK_*Async functions.
*
*  Parameters
*    hDevice          : Handle to an open device returned by USBH_BULK_Open().
*    EPAddr           : Endpoint address (can be retrieved via USBH_BULK_GetEndpointInfo()).
*    pMaxTransferSize : Pointer to a variable which will receive the maximum
*                       transfer size for the specified endpoint.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*
*  Additional information
*    Using this function is only necessary with the USBH_BULK_*Async functions,
*    other functions handle the limits internally.
*    These limits exist because certain USB controllers have hardware limitations.
*    Some USB controllers (OHCI, EHCI, ...) do not have these limitations, therefore 0xFFFFFFFF will be returned.
*/
USBH_STATUS USBH_BULK_GetMaxTransferSize(USBH_BULK_HANDLE hDevice, U8 EPAddr, U32 * pMaxTransferSize) {
  USBH_BULK_INST  * pInst;
  BULK_EP_DATA    * pEPData;
  USBH_STATUS       Status;
  unsigned          i;

  Status = USBH_STATUS_INVALID_HANDLE;
  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    pEPData = pInst->pEndpoints;
    for (i = pInst->NumEPs; i > 0u; i--) {
      if (pEPData->EPAddr == EPAddr) {
        break;
      }
      pEPData++;
    }
    if (i == 0u) {
      Status = USBH_STATUS_INVALID_PARAM;
    } else {
      *pMaxTransferSize = pEPData->MaxTransferSize;
      Status = USBH_STATUS_SUCCESS;
    }
  }
  return Status;
}

/*************************** End of file ****************************/
