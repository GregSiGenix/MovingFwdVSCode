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
File        : USBH_CDC.c
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
#include "USBH_CDC.h"
#include "USBH_Util.h"

/*********************************************************************
*
*       Defines
*
**********************************************************************
*/
#define USBH_CDC_NUM_DEVICES          32u // NOTE: Limited by the number of bits in DevIndexUsedMask which by now is 32

#define USBH_CDC_DEFAULT_TIMEOUT       5000

#define USBH_CDC_REMOVAL_TIMEOUT        100

#define USBH_CDC_SERIAL_STATE_SIZE                     0x0Au // Size of CDC serial state is always ten bytes long

#if USBH_REF_TRACE
  #define DEC_REF_CNT(pInst)        _DecRefCnt(pInst, __func__, __LINE__)
  #define INC_REF_CNT(pInst)        _IncRefCnt(pInst, __func__, __LINE__)
  #define EP_INC_REF_CNT(pEPData)   _EPIncRefCnt(pEPData, __func__, __LINE__)
  #define EP_DEC_REF_CNT(pEPData)   _EPDecRefCnt(pEPData, __func__, __LINE__)
#else
  #define DEC_REF_CNT(pInst)        _DecRefCnt(pInst)
  #define INC_REF_CNT(pInst)        _IncRefCnt(pInst)
  #define EP_INC_REF_CNT(pEPData)   _EPIncRefCnt(pEPData)
  #define EP_DEC_REF_CNT(pEPData)   _EPDecRefCnt(pEPData)
#endif

/*********************************************************************
*
*       Types
*
**********************************************************************
*/
typedef enum {      // Order of states is important !!
  StateInit = 1,    // Set during device initialization
  StateRunning,     // Working state.
  StateSuspend,     // Suspended
  StateStop,        // Device is removed.
  StateError        // Application/Hardware error, the device has to be removed.
} USBH_CDC_STATE;

typedef struct {
  U8                    EPAddr;
  volatile USBH_BOOL    InUse;
  U16                   MaxPacketSize;
  USBH_URB              Urb;
  USBH_OS_EVENT_OBJ   * pEvent;
  unsigned              RefCount;
  I8                    AbortFlag;
  USBH_INTERFACE_HANDLE hInterface;
} CDC_EP_DATA;

typedef struct _USBH_CDC_INST {
  struct _USBH_CDC_INST          * pNext;
  USBH_CDC_STATE                   RunningState;
  U8                               ACMInterfaceNo;
  U8                               ControlLineState;
  U8                               AllowShortRead;
  USBH_INTERFACE_ID                ControlInterfaceID;
  USBH_INTERFACE_ID                DATAInterfaceID;
  USBH_INTERFACE_HANDLE            hControlInterface;
  USBH_INTERFACE_HANDLE            hDATAInterface;
  USBH_TIMER                       RemovalTimer;
  CDC_EP_DATA                      Control;
  CDC_EP_DATA                      BulkIn;
  CDC_EP_DATA                      BulkOut;
  CDC_EP_DATA                      IntIn;
  U32                              MaxOutTransferSize;
  U32                              MaxInTransferSize;
  U8                             * pBulkInBuffer;
  U8                             * pIntInBuffer;
  I8                               IsOpened;
  U8                               DevIndex;
  U16                              IntErrCnt;
  USBH_CDC_HANDLE                  Handle;
  U32                              ReadTimeOut;
  U32                              WriteTimeOut;
  USBH_BUFFER                      RxRingBuffer;
  USBH_CDC_SERIALSTATE             SerialState;
  U8                               aEP0Buffer[64];
  U8                               Flags;
  I32                              RefCnt;
  USBH_CDC_SERIAL_STATE_CALLBACK * pfOnSerialStateChange;
  USBH_CDC_INT_STATE_CALLBACK    * pfOnIntState;
  void                           * pOnSerialStateUContext;
  unsigned                         EnableDataAltSet;
  unsigned                         DisableDataAltSet;
} USBH_CDC_INST;

typedef struct {
  USBH_CDC_INST             * pFirst;
  U8                          NumDevices;
  U8                          DefaultFlags;
  USBH_NOTIFICATION_HANDLE    hDevNotificationACM;
  USBH_NOTIFICATION_HANDLE    hDevNotificationData;
  USBH_CDC_HANDLE             NextHandle;
  USBH_NOTIFICATION_HOOK    * pFirstNotiHook;
  U32                         DevIndexUsedMask;
  U32                         DefaultReadTimeOut;
  U32                         DefaultWriteTimeOut;
} USBH_CDC_GLOBAL;

/*********************************************************************
*
*       Prototypes
*
**********************************************************************
*/
static void _SubmitIntTransfer(USBH_CDC_INST * pInst, U8 * pBuffer, U32 NumBytes);

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static USBH_CDC_GLOBAL USBH_CDC_Global;
static I8              _isInited;

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
*    wLength       : Setup request's wLength value (should be identical to NumBytesData).
*    pData         : Pointer to the data/buffer when wLength && NumBytesData > 0.
*/
static void _PrepareSetupPacket(USBH_CONTROL_REQUEST * pRequest, U8 RequestType, U8 Request, U16 wValue, U16 wIndex, U16 wLength, void * pData) {
  pRequest->Setup.Type    = RequestType;
  pRequest->Setup.Request = Request;
  pRequest->Setup.Value   = wValue;
  pRequest->Setup.Index   = wIndex;
  pRequest->Setup.Length  = wLength;
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
*     A device index or USBH_CDC_NUM_DEVICES in case all device indexes are allocated.
*/
static unsigned _AllocateDevIndex(void) {
  unsigned i;
  U32 Mask;

  Mask = 1;
  for (i = 0; i < USBH_CDC_NUM_DEVICES; ++i) {
    if ((USBH_CDC_Global.DevIndexUsedMask & Mask) == 0u) {
      USBH_CDC_Global.DevIndexUsedMask |= Mask;
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
static void _FreeDevIndex(U8 DevIndex) {
  U32 Mask;

  if (DevIndex < USBH_CDC_NUM_DEVICES) {
    Mask = (1UL << DevIndex);
    USBH_CDC_Global.DevIndexUsedMask &= ~Mask;
  }
}

/*********************************************************************
*
*       _h2p()
*/
static USBH_CDC_INST * _h2p(USBH_CDC_HANDLE Handle) {
  USBH_CDC_INST * pInst;

  if (Handle == 0u) {
    return NULL;
  }
  //
  // Iterate over linked list to find an instance with matching handle. Return if found.
  //
  pInst = USBH_CDC_Global.pFirst;
  while (pInst != NULL) {
    if (pInst->Handle == Handle) {                                        // Match ?
      return pInst;
    }
    pInst = pInst->pNext;
  }
  //
  // Error handling: Device handle not found in list.
  //
  USBH_WARN((USBH_MCAT_CDC, "HANDLE: handle %d not in instance list", Handle));
  return NULL;
}

/*********************************************************************
*
*       _EPIncRefCnt
*/
static void _EPIncRefCnt(CDC_EP_DATA * pEPData
#if USBH_REF_TRACE
                         , const char * s, int d
#endif
                        ) {
  USBH_OS_Lock(USBH_MUTEX_CDC);
  if (pEPData->RefCount != 0u) {
    pEPData->RefCount++;
  }
  USBH_OS_Unlock(USBH_MUTEX_CDC);
#if USBH_REF_TRACE
  USBH_LOG((USBH_MCAT_CDC, "_EPIncRefCnt: [EP0x%x] %d %s@%d", pEPData->EPAddr, pEPData->RefCount, s, d));
#endif
}

/*********************************************************************
*
*       _EPDecRefCnt
*/
static void _EPDecRefCnt(CDC_EP_DATA * pEPData
#if USBH_REF_TRACE
                         , const char * s, int d
#endif
                        ) {
  int RefCount;

  USBH_OS_Lock(USBH_MUTEX_CDC);
  RefCount = (int)pEPData->RefCount - 1;
  if (RefCount >= 0) {
    pEPData->RefCount = (unsigned)RefCount;
  }
  USBH_OS_Unlock(USBH_MUTEX_CDC);
#if USBH_REF_TRACE
  if (RefCount < 0) {
    USBH_WARN((USBH_MCAT_CDC, "_EPDecRefCnt: Invalid RefCnt found: [EP0x%x] %d %s@%d", pEPData->EPAddr, pEPData->RefCount, s, d));
  }
  USBH_LOG((USBH_MCAT_CDC, "_EPDecRefCnt: [EP0x%x] %d %s@%d", pEPData->EPAddr, pEPData->RefCount, s, d));
#endif
}

/*********************************************************************
*
*       _AbortEP
*
*  Function description
*    Abort any URB transaction on the specified EP.
*
*  Parameters
*    pEPData     : Pointer to the CDC_EP_DATA structure.
*/
static USBH_STATUS _AbortEP(CDC_EP_DATA * pEPData) {
  USBH_URB    * pUrb;
  USBH_URB      AbortUrb;

  USBH_LOG((USBH_MCAT_CDC, "_AbortEP: Aborting an URB!"));
  pUrb = &pEPData->Urb;
  USBH_ZERO_MEMORY(&AbortUrb, sizeof(USBH_URB));
  switch (pUrb->Header.Function) {
  case USBH_FUNCTION_BULK_REQUEST:
  case USBH_FUNCTION_INT_REQUEST:
    AbortUrb.Request.EndpointRequest.Endpoint = pUrb->Request.BulkIntRequest.Endpoint;
    break;
  case USBH_FUNCTION_CONTROL_REQUEST:
    // AbortUrb.Request.EndpointRequest.Endpoint is already 0
    break;
  default:
    USBH_WARN((USBH_MCAT_CDC, "_AbortEP: invalid URB function: %d", pUrb->Header.Function));
    break;
  }
  USBH_LOG((USBH_MCAT_CDC, "_AbortEP: Abort Ep: 0x%x", pUrb->Request.EndpointRequest.Endpoint));
  AbortUrb.Header.Function = USBH_FUNCTION_ABORT_ENDPOINT;
  return USBH_SubmitUrb(pEPData->hInterface, &AbortUrb);
}

/*********************************************************************
*
*       _RemoveInstanceFromList
*
*  Function description
*    Removes the instance pointer from the single linked list.
*
*  Notes
*    Calling function checks pInst.
*/
static void _RemoveInstanceFromList(const USBH_CDC_INST * pInst) {
  USBH_CDC_INST * pPrev;
  USBH_CDC_INST * pCurrent;

  if (pInst == USBH_CDC_Global.pFirst) {
    USBH_CDC_Global.pFirst = USBH_CDC_Global.pFirst->pNext;
  } else {
    pPrev = USBH_CDC_Global.pFirst;
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
*       _RemoveDevInstance
*
*  Function description
*    Frees memory allocated for the CDC instance.
*
*  Notes
*    Calling function checks pInst.
*/
static void _RemoveDevInstance(USBH_CDC_INST * pInst) {
  //
  //  Free all associated EP buffers
  //
  if (pInst->pBulkInBuffer != NULL) {
    USBH_FREE(pInst->pBulkInBuffer);
    pInst->pBulkInBuffer = (U8 *)NULL;
  }
  if (pInst->pIntInBuffer != NULL) {
    USBH_FREE(pInst->pIntInBuffer);
    pInst->pIntInBuffer = (U8 *)NULL;
  }
  if (pInst->RxRingBuffer.pData != NULL) {
    USBH_FREE(pInst->RxRingBuffer.pData);
    pInst->RxRingBuffer.pData = (U8 *)NULL;
  }
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
*       _IncRefCnt
*
*  Function description
*    Increments the reference counter of the device instance.
*
*  Parameters
*    pInst     : Pointer to the CDC instance.
*    s         : For debugging only.
*    d         : For debugging only.
*/
static USBH_STATUS _IncRefCnt(USBH_CDC_INST * pInst
#if USBH_REF_TRACE
                              , const char * s, int d
#endif
                             ) {
  USBH_STATUS Ret;

  Ret = USBH_STATUS_SUCCESS;
  USBH_OS_Lock(USBH_MUTEX_CDC);
  if (pInst->RefCnt == 0) {
    Ret = USBH_STATUS_DEVICE_REMOVED;
  } else {
    pInst->RefCnt++;
  }
  USBH_OS_Unlock(USBH_MUTEX_CDC);
#if USBH_REF_TRACE
  USBH_LOG((USBH_MCAT_CDC, "_IncRefCnt: [iface%d] %d %s@%d", pInst->Handle, pInst->RefCnt, s, d));
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
*    pInst     : Pointer to the CDC instance.
*    s         : For debugging only.
*    d         : For debugging only.
*
*  Return value
*    TRUE        : Device was removed.
*    FALSE       : Device is not removed.
*/
static int _DecRefCnt(USBH_CDC_INST * pInst
#if USBH_REF_TRACE
                      , const char * s, int d
#endif
                     ) {
  int RefCount;

  USBH_OS_Lock(USBH_MUTEX_CDC);
  RefCount = pInst->RefCnt - 1;
  if (RefCount >= 0) {
    pInst->RefCnt = RefCount;
  }
  USBH_OS_Unlock(USBH_MUTEX_CDC);
#if USBH_REF_TRACE
  if (RefCount < 0) {
    USBH_WARN((USBH_MCAT_CDC, "Invalid RefCnt found: [iface%d] %d %s@%d", pInst->Handle, RefCount, s, d));
  }
  USBH_LOG((USBH_MCAT_CDC, "_DecRefCnt: [iface%d] %d %s@%d", pInst->Handle, RefCount, s, d));
#endif
  if (RefCount == 0) {
    return 1;
  } else {
    return 0;
  }
}

/*********************************************************************
*
*       _StopDevice
*/
static void _StopDevice(USBH_CDC_INST * pInst) {
  if (pInst->RunningState < StateRunning || pInst->RunningState > StateStop) {
    USBH_LOG((USBH_MCAT_CDC, "_StopDevice: Device not in running state: %d!", pInst->RunningState));
    return;
  }
  //
  // Stops submitting of new URBs from the application
  //
  pInst->RunningState = StateStop;
  (void)DEC_REF_CNT(pInst);
}

/*********************************************************************
*
*       _RemoveAllInstances
*/
static void _RemoveAllInstances(void) {
  USBH_CDC_INST * pInst;

  pInst = USBH_CDC_Global.pFirst;
  while (pInst != NULL) {
    //
    // Check ref count here because in the special case when the device is added via USBH_CDC_AddDevice we can arrive here with ref count < 2 when the device is removed.
    //
    if (pInst->RefCnt > 0) {
      (void)DEC_REF_CNT(pInst);  // CreateDevInstance()
    }
    if (pInst->RefCnt > 0) {
      (void)DEC_REF_CNT(pInst);  // CreateDevInstance() This is done twice because a CDC instance has two interfaces.
    }
    pInst = pInst->pNext;
  }
}

/*********************************************************************
*
*       _RemovalTimer
*/
static void _RemovalTimer(void * pContext) {
  USBH_CDC_INST * pInst;
  CDC_EP_DATA   * apEPData[4];
  unsigned        i;

  pInst = USBH_CTX2PTR(USBH_CDC_INST, pContext);
  if ((pInst->IsOpened == 0) && (pInst->RefCnt == 0)) {
    apEPData[0] = &pInst->Control;
    apEPData[1] = &pInst->BulkIn;
    apEPData[2] = &pInst->BulkOut;
    apEPData[3] = &pInst->IntIn;
    if (pInst->RunningState >= StateStop) {
      for (i = 0; i < SEGGER_COUNTOF(apEPData); i++) {
        //
        // It is possible for a device to be removed before endpoints were
        // allocated, we have to check whether the endpoint has
        // the initial ref count in this case.
        //
        if ((apEPData[i]->RefCount != 0u) && (apEPData[i]->AbortFlag == 0)) {
          EP_DEC_REF_CNT(apEPData[i]);
        }
        //
        // If the reference count is still not zero - we have to abort the EP.
        //
        if ((apEPData[i]->RefCount != 0u) && (apEPData[i]->AbortFlag == 0)) {
          apEPData[i]->AbortFlag = 1;
          (void)_AbortEP(apEPData[i]);
        }
      }
      for (i = 0; i < SEGGER_COUNTOF(apEPData); i++) {
        if (apEPData[i]->RefCount != 0u) {
          //
          // Make sure the abort URB had time to complete.
          // An event must never be freed while a different task is in the wait routine.
          // The ref count is counted down to zero in the completion routine,
          // as long as this does not happen restart the timer.
          //
          USBH_StartTimer(&pInst->RemovalTimer, USBH_CDC_REMOVAL_TIMEOUT);
          return;
        } else {
          //
          // If the ref count is zero we can free the event.
          //
          if (apEPData[i]->pEvent != NULL) {
            USBH_OS_FreeEvent(apEPData[i]->pEvent);
            apEPData[i]->pEvent = NULL;
          }
        }
      }
      //
      // We do not close interfaces until all EP ref counts are zero, that is checked in the loop above.
      //
      if (pInst->hControlInterface != NULL) {
        USBH_CloseInterface(pInst->hControlInterface);
        pInst->hControlInterface = NULL;
      }
      if (pInst->hDATAInterface != NULL) {
        USBH_CloseInterface(pInst->hDATAInterface);
        pInst->hDATAInterface = NULL;
      }
      _FreeDevIndex(pInst->DevIndex);
      USBH_ReleaseTimer(&pInst->RemovalTimer);
      USBH_CDC_Global.NumDevices--;
      _RemoveDevInstance(pInst);
    } else {
      USBH_WARN((USBH_MCAT_CDC, "Removing an instance where state is not error or stop!"));
    }
  } else {
    USBH_StartTimer(&pInst->RemovalTimer, USBH_CDC_REMOVAL_TIMEOUT);
  }
}

/*********************************************************************
*
*       _CreateDevInstance
*/
static USBH_CDC_INST * _CreateDevInstance(void) {
  USBH_CDC_INST * pInst;

  //
  // Check if max. number of devices allowed is exceeded.
  //
  if (USBH_CDC_Global.NumDevices >= USBH_CDC_NUM_DEVICES) {
    USBH_WARN((USBH_MCAT_CDC, "No instance available for creating a new CDC device! (Increase USBH_CDC_NUM_DEVICES)"));
    return NULL;
  }
  pInst = (USBH_CDC_INST *)USBH_TRY_MALLOC_ZEROED(sizeof(USBH_CDC_INST));
  if (pInst != NULL) {
    pInst->Handle           = ++USBH_CDC_Global.NextHandle;
    pInst->DevIndex         = _AllocateDevIndex();
    //
    // Initial reference counter.
    // Set to two because CDC interfaces receive two
    // removal notifications (for the DATA and the ACM interface).
    // In case USBH_CDC_AddDevice/USBH_CDC_RemoveDevice is used
    // USBH_CDC_RemoveDevice is responsible for decrementing the counter twice.
    //
    pInst->RefCnt           = 2;
    pInst->Control.RefCount = 1; // Initial reference counter.
    pInst->BulkIn.RefCount  = 1; // Initial reference counter.
    pInst->BulkOut.RefCount = 1; // Initial reference counter.
    pInst->IntIn.RefCount   = 1; // Initial reference counter.
    //
    // Init and start the removal timer, the timer is responsible for
    // freeing all resources when the device is removed.
    //
    USBH_InitTimer(&pInst->RemovalTimer, _RemovalTimer, pInst);
    USBH_StartTimer(&pInst->RemovalTimer, USBH_CDC_REMOVAL_TIMEOUT);
    pInst->pNext = USBH_CDC_Global.pFirst;
    USBH_CDC_Global.pFirst = pInst;
    USBH_CDC_Global.NumDevices++;
  }
  return pInst;
}

/*********************************************************************
*
*       _GetCSDesc
*
*  Function description
*    Retrieves a pointer to the CDC descriptor indicated by CDCDescType
*    and CDCDescSubType.
*
*  Parameters
*    pInterfaceDesc     :  Pointer to the interface descriptor.
*    InterfaceDescSize  :  Size of the interface descriptor.
*    CDCDescType        :  CDC descriptor type to retrieve from the interface descriptor.
*                          Currently the following are available:
*                          CDC_CS_INTERFACE_DESCRIPTOR_TYPE
*                          CDC_CS_ENDPOINT_DESCRIPTOR_TYPE.
*    CDCDescSubType     :  Specifies the sub descriptor type to look for in the interface descriptor:
*                            USBH_CDC_DESC_SUBTYPE_HEADER
*                            USBH_CDC_DESC_SUBTYPE_CALL_MANAGEMENT
*                            USBH_CDC_DESC_SUBTYPE_ACM
*                            USBH_CDC_DESC_SUBTYPE_UNION_FUCTIONAL.
*
*  Return value
*    != NULL            : Success, Pointer to the desired CDC descriptor.
*    == NULL            : Fail, desired CDC descriptor was not found in the interface descriptor.
*/
static const U8 * _GetCSDesc(const U8 * pInterfaceDesc, unsigned InterfaceDescSize, U8 CDCDescType, U8 CDCDescSubType) {
  const U8  * pDesc;
  unsigned    Len;

  pDesc = pInterfaceDesc;
  if (pDesc != NULL) {
    do {
      Len = pDesc[USB_DESC_LENGTH_INDEX];
      if (pDesc[USB_DESC_TYPE_INDEX] == CDCDescType) {
        if (pDesc[2] == CDCDescSubType) {
          break;
        }
      }
      pDesc += Len;
      InterfaceDescSize -= Len;
    } while (InterfaceDescSize != 0u);
  }
  return pDesc;
}

/*********************************************************************
*
*       _GetDataInterfaceIdx
*
*  Function description
*    Retrieves the data interface ID from a interface descriptor.
*
*  Parameters
*    pDesc              : Pointer to the interface descriptor.
*    InterfaceDescSize  : Size of the interface descriptor.
*    pDataInterface     : Pointer to a variable which will receive the data interface ID.
*
*  Return value
*    == 0               : Success, data interface index found
*    == NULL            : Fail, cannot find the data interface index.
*/
#if USBH_CDC_DISABLE_AUTO_DETECT == 0
static int _GetDataInterfaceIdx(const U8 * pDesc, unsigned InterfaceDescSize, unsigned * pDataInterface) {
  int Len;

  Len = InterfaceDescSize;
  while (Len >= 5) {
    if (pDesc[1] == USBH_CDC_CS_INTERFACE_DESCRIPTOR_TYPE && pDesc[2] == USBH_CDC_DESC_SUBTYPE_UNION_FUCTIONAL) {
      *pDataInterface = pDesc[4];
      return 0;
    }
    Len   -= (int)*pDesc;
    pDesc += *pDesc;
  }
  return 1;
}
#endif

/*********************************************************************
*
*       _AssignInst
*
*  Function description
*    Assign an instance
*
*  Parameters
*    InterfaceID     : Interface ID to check.
*
*  Return value
*    != NULL : Pointer to a valid CDC Inst.
*    == NULL : CDC Inst, could not be created.
*/
#if USBH_CDC_DISABLE_AUTO_DETECT == 0
static USBH_CDC_INST * _AssignInst(USBH_INTERFACE_ID InterfaceID) {
  USBH_CDC_INST         * pInst;
  USBH_INTERFACE_HANDLE   hInterface = NULL;
  const U8              * pDesc;
  unsigned                Size;
  unsigned                DataInterface;
  USBH_INTERFACE_INFO     InterfaceInfo;
  USBH_STATUS             Status;

  pInst = USBH_CDC_Global.pFirst;
  while (pInst != NULL) {   // Iterate over all instances
    if (pInst->DATAInterfaceID == 0u) {
      //
      // Check whether the stored ControlInterfaceID is available
      //
      if (USBH_OpenInterface(pInst->ControlInterfaceID, 0, &hInterface) != USBH_STATUS_SUCCESS) {
        continue;
      }
      //
      //  Retrieve through the ACM class information the data interface
      //
      Status = USBH_GetInterfaceDescriptorPtr(hInterface, 0, &pDesc, &Size);
      USBH_CloseInterface(hInterface);
      if (Status != USBH_STATUS_SUCCESS) {
        return NULL;
      }
      //
      // Instead of using the call management function descriptor we simply use the one
      //
      if (_GetDataInterfaceIdx(pDesc, Size, &DataInterface) != 0) {
        USBH_WARN((USBH_MCAT_CDC, "_AssignInst: USBH_CDC_DESC_SUBTYPE_UNION_FUCTIONAL not available, aborting"));
        return NULL;
      }
      Status = USBH_GetInterfaceInfo(InterfaceID, &InterfaceInfo);
      if (Status != USBH_STATUS_SUCCESS) {
        return NULL;
      }
      if ((((USBH_CDC_Global.DefaultFlags & USBH_CDC_DISABLE_INTERFACE_CHECK) != 0u)  && (InterfaceInfo.Interface  == (pDesc[2] + 1u))) || InterfaceInfo.Interface == DataInterface) {
        USBH_LOG((USBH_MCAT_CDC, "_AssignInst: Found data interface to control interface."));
        pInst->DATAInterfaceID = InterfaceID;
        return pInst;
      }
    }
    pInst = pInst->pNext;
  }
  return NULL;
}
#endif

/*********************************************************************
*
*       _OnSubmitUrbCompletion
*/
static void _OnSubmitUrbCompletion(USBH_URB * pUrb) USBH_CALLBACK_USE {
  CDC_EP_DATA  * pEPData;

  pEPData = USBH_CTX2PTR(CDC_EP_DATA, pUrb->Header.pContext);
  if (pEPData->RefCount == 0u) {
    USBH_LOG((USBH_MCAT_CDC, "_OnSubmitUrbCompletion EP RefCount zero!"));
    return;
  }
  USBH_LOG((USBH_MCAT_CDC, "_OnSubmitUrbCompletion URB st: %s", USBH_GetStatusStr(pUrb->Header.Status)));
  EP_DEC_REF_CNT(pEPData);
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
static USBH_STATUS _SubmitUrbAndWait(const USBH_CDC_INST * pInst, USBH_INTERFACE_HANDLE hInterface, CDC_EP_DATA * pEPData, U32 Timeout) {
  USBH_STATUS   Status;
  int           EventStatus;
  USBH_URB    * pUrb;

  if (pInst->RunningState != StateRunning) {
    return USBH_STATUS_DEVICE_REMOVED;
  }
  USBH_ASSERT_PTR(pEPData->pEvent);
  USBH_LOG((USBH_MCAT_CDC, "_SubmitUrbAndWait"));
  pUrb = &pEPData->Urb;
  pUrb->Header.pfOnCompletion = _OnSubmitUrbCompletion;
  pUrb->Header.pContext       = pEPData;
  USBH_OS_ResetEvent(pEPData->pEvent);
  EP_INC_REF_CNT(pEPData);
  Status = USBH_SubmitUrb(hInterface, pUrb);
  if (Status != USBH_STATUS_PENDING) {
    USBH_LOG((USBH_MCAT_CDC, "_SubmitUrbAndWait: USBH_SubmitUrb st: 0x%08x", USBH_GetStatusStr(Status)));
    EP_DEC_REF_CNT(pEPData);
  } else {                                // Pending URB
    //
    // Wait for completion.
    //
    EventStatus = USBH_OS_WaitEventTimed(pEPData->pEvent, Timeout);
    if (EventStatus != USBH_OS_EVENT_SIGNALED) {
      USBH_LOG((USBH_MCAT_CDC, "_SubmitUrbAndWait: Time-out, now Abort the URB!"));
      EP_INC_REF_CNT(pEPData);
      Status = _AbortEP(pEPData);
      if (Status != USBH_STATUS_SUCCESS) {
        USBH_LOG((USBH_MCAT_CDC, "_SubmitUrbAndWait: USBH_FUNCTION_ABORT_ENDPOINT st: 0x%08x", Status));
      } else {
        //
        // Abort URB sent out successfully, wait for URB to terminate.
        //
        USBH_OS_WaitEvent(pEPData->pEvent);
        Status = pUrb->Header.Status;
        if (Status == USBH_STATUS_CANCELED || Status == USBH_STATUS_SUCCESS) {
          Status = USBH_STATUS_TIMEOUT;
        }
      }
      EP_DEC_REF_CNT(pEPData);
    } else {
      //
      // In case the event was signaled the status is retrieved from the URB.
      //
      Status = pUrb->Header.Status;
      USBH_LOG((USBH_MCAT_CDC, "_SubmitUrbAndWait: URB Status: %s", USBH_GetStatusStr(Status)));
    }
  }
  return Status;
}

/*********************************************************************
*
*       _OnIntInCompletion
*
*  Function description
*    Is called when an URB is completed.
*/
static void _OnIntInCompletion(USBH_URB * pUrb) USBH_CALLBACK_USE {
  USBH_CDC_INST * pInst;
  U8            * pBuffer;

  USBH_LOG((USBH_MCAT_CDC, "[_OnIntInCompletion"));
  USBH_ASSERT(pUrb != NULL);
  pInst = USBH_CTX2PTR(USBH_CDC_INST, pUrb->Header.pContext);
  //
  // Check if RefCnt is zero, this occurs when the device
  // has been removed or when USBH_CDC_Exit is called.
  //
  if (pInst->RefCnt == 0) {
    USBH_LOG((USBH_MCAT_CDC, "_OnIntInCompletion: device RefCnt is zero!"));
    goto Err;
  }
  if (pInst->RunningState > StateRunning) {
    USBH_WARN((USBH_MCAT_CDC, "_OnIntInCompletion: device has an error or is stopped!"));
    goto Err;
  }
  if (pUrb->Header.Status == USBH_STATUS_SUCCESS) {
    U8    bRequestType;
    U8    bNotification;
    U16   wLength;
    U16   SerialState;
    U8  * pData;
    U32   NumBytes;

    pBuffer = USBH_U8PTR(pUrb->Request.BulkIntRequest.pBuffer);
    NumBytes = pUrb->Request.BulkIntRequest.Length;
    //
    // Check if we have received something
    // if not, just ignore it.
    // Some USB host controller return success even though
    // the item was never triggered.
    // This happens when a controller uses a frame list
    // to handle INTERRUPT and ISOCHRONOUS transfers.
    //
    if (pInst->pfOnIntState != NULL) {
      pInst->pfOnIntState(pInst->Handle, pBuffer, NumBytes, pInst->pOnSerialStateUContext);
    } else {
      if (pUrb->Request.BulkIntRequest.Length == USBH_CDC_SERIAL_STATE_SIZE) {
        pInst->IntErrCnt = 0;
        bRequestType = *pBuffer;
        bNotification = *(pBuffer + 1);
        wLength = (U16)USBH_LoadU16LE(pBuffer + 6);
        pData = pBuffer + 8;

        if (bRequestType == USBH_CDC_NOTIFICATION_REQUEST &&
            bNotification == USBH_CDC_NOTIFICATION_TYPE_SERIAL_STATE &&
            wLength == 0x02u) {
          SerialState = (U16)USBH_LoadU16LE(pData);
          pInst->SerialState.bRxCarrier  = (U8)((SerialState >> 0) & 1u);
          pInst->SerialState.bTxCarrier  = (U8)((SerialState >> 1) & 1u);
          pInst->SerialState.bBreak      = (U8)((SerialState >> 2) & 1u);
          pInst->SerialState.bRingSignal = (U8)((SerialState >> 3) & 1u);
          pInst->SerialState.bFraming    = (U8)((SerialState >> 4) & 1u);
          pInst->SerialState.bParity     = (U8)((SerialState >> 5) & 1u);
          pInst->SerialState.bOverRun    = (U8)((SerialState >> 6) & 1u);
          if (pInst->pfOnSerialStateChange != NULL) {
            pInst->pfOnSerialStateChange(pInst->Handle, &pInst->SerialState);
          }
        } else {
          USBH_WARN((USBH_MCAT_CDC, "Unknown notification received, ReqType = 0x%x, bNotifcation=0x%x", bRequestType, bNotification));
        }
      }
    }
  } else {
    if (++pInst->IntErrCnt > 10u) {
      pInst->RunningState = StateError;
      (void)DEC_REF_CNT(pInst);
    }
  }
  if (pInst->RunningState <= StateRunning) {
    //
    // Resubmit a transfer request
    //
    _SubmitIntTransfer(pInst, pInst->pIntInBuffer, pInst->IntIn.MaxPacketSize);
  }
Err:
  EP_DEC_REF_CNT(&pInst->IntIn);
  USBH_LOG((USBH_MCAT_CDC, "]_OnIntInCompletion"));
}

/*********************************************************************
*
*       _SubmitIntTransfer
*
*  Function description
*    Submits a request to the CDC device.
*/
static void _SubmitIntTransfer(USBH_CDC_INST * pInst, U8 * pBuffer, U32 NumBytes) {
  USBH_STATUS Status;

  if (pInst->hControlInterface == NULL || pInst->RunningState > StateRunning) {
    USBH_WARN((USBH_MCAT_CDC, "_SubmitIntTransfer: Device removed"));
    return;
  }
  pInst->IntIn.Urb.Header.pContext                 = pInst;
  pInst->IntIn.Urb.Header.pfOnCompletion           = _OnIntInCompletion;
  pInst->IntIn.Urb.Header.Function                 = USBH_FUNCTION_INT_REQUEST;
  pInst->IntIn.Urb.Request.BulkIntRequest.Endpoint = pInst->IntIn.EPAddr;
  pInst->IntIn.Urb.Request.BulkIntRequest.pBuffer  = pBuffer;
  pInst->IntIn.Urb.Request.BulkIntRequest.Length   = NumBytes;
  EP_INC_REF_CNT(&pInst->IntIn);
  Status = USBH_SubmitUrb(pInst->hControlInterface, &pInst->IntIn.Urb);
  if (Status != USBH_STATUS_PENDING) {
    USBH_WARN((USBH_MCAT_CDC, "_SubmitIntTransfer: USBH_SubmitUrb %s", USBH_GetStatusStr(Status)));
    EP_DEC_REF_CNT(&pInst->IntIn);
  }
}

/*********************************************************************
*
*       _GetValidAlternateSetting
*
*  Function description
*   Searches for a valid alternate interface setting that
*   contains the needed bulk endpoint descriptors in the CDC data interface.
*   We will only look for the bulk out endpoint and
*   assume that the device contains both bulk endpoints (in/out)
*   in this alternate setting.
*
*  Parameters
*    pInst             : Pointer to the CDC device instance.
*    pEnableDataAltSet : ID of the alternate setting which contains endpoints (communication enabled).
*    pDisableDataAltSet: ID of the alternate setting without endpoints (no communication).
*/
static void _GetValidAlternateSetting(const USBH_CDC_INST * pInst, unsigned * pEnableDataAltSet, unsigned * pDisableDataAltSet) {
  unsigned NumAlternateInterfaces;
  USBH_STATUS  Status;
  USBH_EP_MASK EPMask;
  unsigned int Length;
  U8           aEpDesc[USB_ENDPOINT_DESCRIPTOR_LENGTH];
  unsigned     i;
  unsigned     DisableAltSet;
  unsigned     EnableAltSet;

  DisableAltSet = 0xffffffffu;
  EnableAltSet  = 0xffffffffu;
  NumAlternateInterfaces = USBH_GetNumAlternateSettings(pInst->hDATAInterface);
  for (i = 0; i < NumAlternateInterfaces; i++) {
    if ((DisableAltSet != 0xffffffffu) && (EnableAltSet  != 0xffffffffu)) {
      //
      // We found our settings
      //
      break;
    }
    //
    // Get first the BULK EP OUT descriptor
    //
    USBH_MEMSET(&EPMask,  0, sizeof(USBH_EP_MASK));
    EPMask.Mask      = USBH_EP_MASK_TYPE | USBH_EP_MASK_DIRECTION;
    EPMask.Direction = USB_OUT_DIRECTION;
    EPMask.Type      = USB_EP_TYPE_BULK;
    Length           = sizeof(aEpDesc);
    Status           = USBH_GetEndpointDescriptor(pInst->hDATAInterface, i, &EPMask, aEpDesc, &Length);
    if (Status == USBH_STATUS_SUCCESS) {
      EnableAltSet = i;
    } else {
      DisableAltSet = i;
    }
  }
  *pEnableDataAltSet = EnableAltSet;
  *pDisableDataAltSet = DisableAltSet;
}

/*********************************************************************
*
*       _StartDevice
*
*  Function description
*   Starts the application and is called if a USB device is connected.
*   The function uses the first interface of the device.
*
*  Parameters
*    pInst  : Pointer to the CDC device instance.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
static USBH_STATUS _StartDevice(USBH_CDC_INST * pInst) {
  USBH_STATUS  Status;
  USBH_EP_MASK EPMask;
  unsigned int Length;
  U8           aEpDesc[USB_ENDPOINT_DESCRIPTOR_LENGTH];
  USBH_INTERFACE_INFO InterfaceInfo;
  //
  // Open the ACM interface
  //
  Status = USBH_OpenInterface(pInst->ControlInterfaceID, 0, &pInst->hControlInterface);
  if (USBH_STATUS_SUCCESS != Status) {
    USBH_WARN((USBH_MCAT_CDC, "_StartDevice: USBH_OpenInterface failed %s", USBH_GetStatusStr(Status)));
    goto Err;
  }
  Status = USBH_GetInterfaceInfo(pInst->ControlInterfaceID, &InterfaceInfo);
  if (Status != USBH_STATUS_SUCCESS) {
    USBH_WARN((USBH_MCAT_CDC, "_StartDevice: Failed to get interface info of ACM interface (InterfaceId = %d), failed %s!", pInst->ControlInterfaceID, USBH_GetStatusStr(Status)));
    goto Err;
  }
  pInst->ACMInterfaceNo = InterfaceInfo.Interface;
  Status = USBH_OpenInterface(pInst->DATAInterfaceID, 0, &pInst->hDATAInterface);
  if (USBH_STATUS_SUCCESS != Status) {
    USBH_WARN((USBH_MCAT_CDC, "_StartDevice: USBH_OpenInterface failed %s", USBH_GetStatusStr(Status)));
    goto Err;
  }
  pInst->Control.pEvent = USBH_OS_AllocEvent();
  if (pInst->Control.pEvent == NULL) {
    goto Err;
  }
  pInst->Control.hInterface = pInst->hControlInterface;
  _GetValidAlternateSetting(pInst, &pInst->EnableDataAltSet, &pInst->DisableDataAltSet);
  //
  // Get first the BULK EP OUT descriptor
  //
  USBH_MEMSET(&EPMask,  0, sizeof(USBH_EP_MASK));
  EPMask.Mask      = USBH_EP_MASK_TYPE | USBH_EP_MASK_DIRECTION;
  EPMask.Direction = USB_OUT_DIRECTION;
  EPMask.Type      = USB_EP_TYPE_BULK;
  Length           = sizeof(aEpDesc);
  Status           = USBH_GetEndpointDescriptor(pInst->hDATAInterface, pInst->EnableDataAltSet, &EPMask, aEpDesc, &Length);
  if (Status != USBH_STATUS_SUCCESS) {
    USBH_WARN((USBH_MCAT_CDC, "_StartDevice: Could not find Data BULK EP Out Error=%s", USBH_GetStatusStr(Status)));
    goto Err;
  } else {
    pInst->BulkOut.MaxPacketSize = aEpDesc[USB_EP_DESC_PACKET_SIZE_OFS] + ((U16)aEpDesc[USB_EP_DESC_PACKET_SIZE_OFS + 1u] << 8);
    pInst->BulkOut.EPAddr        = aEpDesc[USB_EP_DESC_ADDRESS_OFS];
    pInst->BulkOut.pEvent        = USBH_OS_AllocEvent();
    if (pInst->BulkOut.pEvent == NULL) {
      USBH_WARN((USBH_MCAT_CDC, "Allocation of an event object failed"));
      Status = USBH_STATUS_RESOURCES;
      goto Err;
    }
    pInst->BulkOut.hInterface = pInst->hDATAInterface;
    USBH_LOG((USBH_MCAT_CDC, "Address   MaxPacketSize"));
    USBH_LOG((USBH_MCAT_CDC, "0x%02X      %5d      ", pInst->BulkOut.EPAddr, pInst->BulkOut.MaxPacketSize));
  }
  //
  // Now try to get the BULK EP IN descriptor
  //
  USBH_MEMSET(&EPMask,  0, sizeof(USBH_EP_MASK));
  EPMask.Mask      = USBH_EP_MASK_TYPE | USBH_EP_MASK_DIRECTION;
  EPMask.Direction = USB_IN_DIRECTION;
  EPMask.Type      = USB_EP_TYPE_BULK;
  Length           = sizeof(aEpDesc);
  Status           = USBH_GetEndpointDescriptor(pInst->hDATAInterface, pInst->EnableDataAltSet, &EPMask, aEpDesc, &Length);
  if (Status != USBH_STATUS_SUCCESS) {
    USBH_WARN((USBH_MCAT_CDC, "_StartDevice: Could not find Data BULK EP In Error=%s", USBH_GetStatusStr(Status)));
    goto Err;
  } else {
    pInst->BulkIn.EPAddr        = aEpDesc[USB_EP_DESC_ADDRESS_OFS];
    pInst->BulkIn.MaxPacketSize = aEpDesc[USB_EP_DESC_PACKET_SIZE_OFS] + ((U16)aEpDesc[USB_EP_DESC_PACKET_SIZE_OFS + 1u] << 8);
    pInst->BulkIn.pEvent        = USBH_OS_AllocEvent();
    pInst->pBulkInBuffer        = (U8 *)USBH_TRY_MALLOC(pInst->BulkIn.MaxPacketSize);
    if (pInst->pBulkInBuffer == NULL) {
      USBH_WARN((USBH_MCAT_CDC, "Buffer allocation failed."));
      Status = USBH_STATUS_MEMORY;
      goto Err;
    }
    pInst->RxRingBuffer.pData   = (U8 *)USBH_TRY_MALLOC(pInst->BulkIn.MaxPacketSize);
    if (pInst->RxRingBuffer.pData == NULL) {
      USBH_WARN((USBH_MCAT_CDC, "Buffer allocation failed."));
      Status = USBH_STATUS_MEMORY;
      goto Err;
    }
    pInst->RxRingBuffer.Size    = pInst->BulkIn.MaxPacketSize;
    if (pInst->BulkIn.pEvent == NULL) {
      USBH_WARN((USBH_MCAT_CDC, "Allocation of an event object failed"));
      Status = USBH_STATUS_RESOURCES;
      goto Err;
    }
    pInst->BulkIn.hInterface = pInst->hDATAInterface;
    USBH_LOG((USBH_MCAT_CDC, "Address   MaxPacketSize"));
    USBH_LOG((USBH_MCAT_CDC, "0x%02X      %5d      ", pInst->BulkIn.EPAddr, pInst->BulkIn.MaxPacketSize));
  }
  Status = USBH_GetMaxTransferSize(pInst->BulkOut.hInterface, pInst->BulkOut.EPAddr, &pInst->MaxOutTransferSize);
  if (Status != USBH_STATUS_SUCCESS) {
    //
    // Needs to be done later when the alternate setting is set.
    //
    pInst->MaxOutTransferSize = 0;
    //goto Err;
  }
  Status = USBH_GetMaxTransferSize(pInst->BulkIn.hInterface, pInst->BulkIn.EPAddr, &pInst->MaxInTransferSize);
  if (Status != USBH_STATUS_SUCCESS) {
    //
    // Needs to be done later when the alternate setting is set.
    //
    pInst->MaxInTransferSize = 0;
//    goto Err;
  }
  if ((pInst->Flags & USBH_CDC_IGNORE_INT_EP) == 0u) {
    //
    // Now try to get the INT EP IN descriptor
    //
    USBH_MEMSET(&EPMask,  0, sizeof(USBH_EP_MASK));
    EPMask.Mask      = USBH_EP_MASK_TYPE | USBH_EP_MASK_DIRECTION;
    EPMask.Direction = USB_IN_DIRECTION;
    EPMask.Type      = USB_EP_TYPE_INT;
    Length           = sizeof(aEpDesc);
    Status           = USBH_GetEndpointDescriptor(pInst->hControlInterface, 0, &EPMask, aEpDesc, &Length);
    if (Status != USBH_STATUS_SUCCESS) {
      USBH_WARN((USBH_MCAT_CDC, "_StartDevice: Could not find Interrupt EP In. Error=%s", USBH_GetStatusStr(Status)));
      goto Err;
    } else {
      pInst->IntIn.EPAddr        = aEpDesc[USB_EP_DESC_ADDRESS_OFS];
      pInst->IntIn.MaxPacketSize = aEpDesc[USB_EP_DESC_PACKET_SIZE_OFS] + ((U16)aEpDesc[USB_EP_DESC_PACKET_SIZE_OFS + 1u] << 8);
      pInst->IntIn.pEvent        = USBH_OS_AllocEvent();
      if (pInst->IntIn.pEvent == NULL) {
        USBH_WARN((USBH_MCAT_CDC, "Allocation of an event object failed"));
        Status = USBH_STATUS_RESOURCES;
        goto Err;
      }
      pInst->pIntInBuffer = (U8 *)USBH_TRY_MALLOC(pInst->IntIn.MaxPacketSize);
      if (pInst->pIntInBuffer == NULL) {
        USBH_WARN((USBH_MCAT_CDC, "Buffer allocation failed."));
        Status = USBH_STATUS_MEMORY;
        goto Err;
      }
      pInst->IntIn.hInterface = pInst->hControlInterface;
      USBH_LOG((USBH_MCAT_CDC, "Address   MaxPacketSize"));
      USBH_LOG((USBH_MCAT_CDC, "0x%02X      %5d      ", pInst->IntIn.EPAddr, pInst->IntIn.MaxPacketSize));
    }
  }
  pInst->ReadTimeOut  = USBH_CDC_Global.DefaultReadTimeOut;
  pInst->WriteTimeOut = USBH_CDC_Global.DefaultWriteTimeOut;
  Status = INC_REF_CNT(pInst);
  if (Status == USBH_STATUS_SUCCESS) {
    if ((pInst->Flags & USBH_CDC_IGNORE_INT_EP) == 0u) {
      _SubmitIntTransfer(pInst, pInst->pIntInBuffer, pInst->IntIn.MaxPacketSize);
    }
  }
  return Status;
Err: // on error
  //
  // Removal is handled by the timer.
  //
  (void)DEC_REF_CNT(pInst);  // CreateDevInstance()
  (void)DEC_REF_CNT(pInst);  // CreateDevInstance() This is done twice because a CDC instance has two interfaces.
  return Status;
}

/*********************************************************************
*
*       _ACM_OnDeviceNotification
*/
#if USBH_CDC_DISABLE_AUTO_DETECT == 0
static void _ACM_OnDeviceNotification(void * pContext, USBH_PNP_EVENT Event, USBH_INTERFACE_ID InterfaceID) {
  USBH_CDC_INST * pInst;
  int             Found;

  USBH_USE_PARA(pContext);
  Found = 0;
  if (Event == USBH_ADD_DEVICE) {
    pInst = _CreateDevInstance();
    if (pInst != NULL) {
      USBH_LOG((USBH_MCAT_CDC, "_ACM_OnDeviceNotification: USB CDC device detected interface ID: %u !", InterfaceID));
      pInst->RunningState = StateInit;
      pInst->ControlInterfaceID = InterfaceID;
    } else {
      USBH_WARN((USBH_MCAT_CDC, "_ACM_OnDeviceNotification: device instance not created!"));
    }
  } else if (Event == USBH_REMOVE_DEVICE) {
    pInst = USBH_CDC_Global.pFirst;
    while (pInst != NULL) {   // Iterate over all instances
      if (pInst->ControlInterfaceID == InterfaceID) {
        Found = 1;
        break;
      }
      pInst = pInst->pNext;
    }
    if (Found != 0) {
      _StopDevice(pInst);
      (void)DEC_REF_CNT(pInst);
      USBH_LOG((USBH_MCAT_CDC, "_ACM_OnDeviceNotification: USB CDC device removed interface  ID: %u !", InterfaceID));
    } else {
      USBH_WARN((USBH_MCAT_CDC, "_ACM_OnDeviceNotification: pInst not found for notified interface!"));
    }
  } else {
    // MISRA dummy
  }
}
#endif

/*********************************************************************
*
*       _DATA_OnDeviceNotification
*/
#if USBH_CDC_DISABLE_AUTO_DETECT == 0
static void _DATA_OnDeviceNotification(void * pContext, USBH_PNP_EVENT Event, USBH_INTERFACE_ID InterfaceID) {
  USBH_CDC_INST             * pInst;
  USBH_STATUS                 Status;
  USBH_NOTIFICATION_HOOK    * pHook;

  USBH_USE_PARA(pContext);
  if (Event == USBH_ADD_DEVICE) {
    USBH_LOG((USBH_MCAT_CDC, "_DATA_OnDeviceNotification: USB CDC device detected interface ID: %u !", InterfaceID));
    pInst = _AssignInst(InterfaceID);
    if (pInst == NULL) {
      USBH_WARN((USBH_MCAT_CDC, "No ACM interface found for data interface ID %u found", InterfaceID));
      return;
    }
    pInst->RunningState = StateInit;
    if (pInst->hDATAInterface == NULL) {
      // Only one device is handled from the application at the same time
      pInst->Flags       = USBH_CDC_Global.DefaultFlags;
      Status             = _StartDevice(pInst);
      if (Status != USBH_STATUS_SUCCESS) { // On error
        pInst->RunningState = StateError; // _StartDevice decrements ref count on error internally.
      } else {
        pInst->RunningState = StateRunning;
        pHook = USBH_CDC_Global.pFirstNotiHook;
        while (pHook != NULL) {
          if (pHook->pfNotification != NULL) {
            pHook->pfNotification(pHook->pContext, pInst->DevIndex, USBH_DEVICE_EVENT_ADD);
          }
          pHook = pHook->pNext;
        }
      }
    }
  } else if (Event == USBH_REMOVE_DEVICE) {
    //
    // Find the instance to the appropriate InterfaceId.
    //
    pInst = USBH_CDC_Global.pFirst;
    while (pInst != NULL) {   // Iterate over all instances
      //
      // When found, delete
      if (pInst->DATAInterfaceID == InterfaceID) {
        if (pInst->hDATAInterface == NULL) {
          // Only one device is handled from the application at the same time
          return;
        }
        USBH_LOG((USBH_MCAT_CDC, "_DATA_OnDeviceNotification: USB CDC device removed interface  ID: %u !", InterfaceID));
        pHook = USBH_CDC_Global.pFirstNotiHook;
        while (pHook != NULL) {
          if (pHook->pfNotification != NULL) {
            pHook->pfNotification(pHook->pContext, pInst->DevIndex, USBH_DEVICE_EVENT_REMOVE);
          }
          pHook = pHook->pNext;
        }
        _StopDevice(pInst);
        return; // Stop processing the list as pInst may have been freed.
      }
      pInst = pInst->pNext;
    }
    USBH_WARN((USBH_MCAT_CDC, "_DATA_OnDeviceNotification: pInst not found for notified interface!"));
  } else {
    // MISRA dummy
  }
}
#endif

/*********************************************************************
*
*       _SendControlRequest
*
*  Function description
*    Sends a control URB to the device via EP0.
*
*  Parameters
*    pInst         : Pointer to the CDC instance.
*    RequestType   : IN/OUT direction.
*    Request       : Request code in the setup request.
*    wValue        : wValue in the setup request.
*    wIndex        : wIndex in the setup request.
*    wLength       : wLength in the setup request.
*    pData         : Additional data for the setup request.
*    pNumBytesData : Number of data to be received/sent in pData.
*
*  Return value
*    USBH_STATUS   : Transfer status.
*/
static USBH_STATUS _SendControlRequest(USBH_CDC_INST * pInst, U8 RequestType, U8 Request, U16 wValue, U16 wIndex, U16 wLength, void * pData, U32 * pNumBytesData) {
  USBH_STATUS  Status;
  if (pInst->IsOpened != 0) {
    CDC_EP_DATA * pEPData;

    pEPData = &pInst->Control;
    pEPData->Urb.Header.Function = USBH_FUNCTION_CONTROL_REQUEST;
    _PrepareSetupPacket(&pEPData->Urb.Request.ControlRequest, RequestType, Request, wValue, wIndex, wLength, pData);
    Status = INC_REF_CNT(pInst);
    if (Status == USBH_STATUS_SUCCESS) {
      Status = _SubmitUrbAndWait(pInst, pInst->hControlInterface, pEPData, USBH_CDC_EP0_TIMEOUT);
      if (DEC_REF_CNT(pInst) != 0) {
        Status = USBH_STATUS_DEVICE_REMOVED;
      }
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
*    pInst    : Pointer to a CDC device object.
*    EndPoint : Endpoint number and direction.
*/
static void _ResetPipe(USBH_CDC_INST * pInst, U8 EndPoint) {
  USBH_STATUS   Status;
  USBH_URB    * pUrb;
  CDC_EP_DATA * pEPData;

  pEPData = &pInst->Control;
  pUrb                                   = &pEPData->Urb;
  pUrb->Header.Function                  = USBH_FUNCTION_RESET_ENDPOINT;
  pUrb->Header.pfOnCompletion            = NULL;
  pUrb->Request.EndpointRequest.Endpoint = EndPoint;
  Status                                 = _SubmitUrbAndWait(pInst, pInst->hDATAInterface, pEPData, USBH_CDC_EP0_TIMEOUT); // On error this URB is not aborted
  if (Status != USBH_STATUS_SUCCESS) { // Reset pipe does not wait
    USBH_WARN((USBH_MCAT_CDC, "_ResetPipe: USBH_SubmitUrb Status = %s", USBH_GetStatusStr(Status)));
  }
}

/*********************************************************************
*
*       _SendControlLineState
*
*  Function description
*    Sends the new control line state to the device.
*
*  Parameters
*    hDevice     : Handle to the opened device.
*    SetValue    : bits to be set in the control line state
*    ResetValue  : bits to be cleared in the control line state
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
static USBH_STATUS _SendControlLineState(USBH_CDC_HANDLE hDevice, unsigned SetValue, unsigned ResetValue) {
  USBH_CDC_INST * pInst;

  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    USBH_STATUS Status;

    Status = INC_REF_CNT(pInst);
    if (Status == USBH_STATUS_SUCCESS) {
      pInst->ControlLineState |= SetValue;
      pInst->ControlLineState &= ~ResetValue;
      Status = _SendControlRequest(pInst, USB_REQTYPE_CLASS | USB_INTERFACE_RECIPIENT, USBH_CDC_REQ_SET_CONTROL_LINE_STATE, pInst->ControlLineState, pInst->ACMInterfaceNo, 0, NULL, NULL);
      if (DEC_REF_CNT(pInst) != 0) {
        Status = USBH_STATUS_DEVICE_REMOVED;
      }
    }
    return Status;
  }
  return USBH_STATUS_INVALID_HANDLE;
}

/*********************************************************************
*
*       _GetEPData
*/
static CDC_EP_DATA * _GetEPData(USBH_CDC_INST * pInst, U8 EPAddr) {
  if (pInst->BulkIn.EPAddr == EPAddr) {
    return &pInst->BulkIn;
  }
  if (pInst->BulkOut.EPAddr == EPAddr) {
    return &pInst->BulkOut;
  }
  return NULL;
}

/*********************************************************************
*
*       _OnAsyncCompletion
*
*  Function description
*    CDC internal completion routine for the USBH_CDC_ReadAsync and
*    USBH_CDC_WriteAsync functions.
*    Calls the user callback.
*
*  Parameters
*    pUrb     : Pointer to the completed URB.
*/
static void _OnAsyncCompletion(USBH_URB * pUrb) {
  USBH_CDC_INST                * pInst;
  USBH_ON_COMPLETION_USER_FUNC * pfOnComplete;
  USBH_CDC_RW_CONTEXT          * pRWContext;
  CDC_EP_DATA                  * pEPData;
  U8                             EPAddr;
  USBH_BULK_INT_REQUEST        * pBulkRequest;

  //
  // Get all necessary pointers
  //
  pInst         = USBH_CTX2PTR(USBH_CDC_INST, pUrb->Header.pContext);
  pfOnComplete  = pUrb->Header.pfOnUserCompletion;
  pRWContext    = USBH_CTX2PTR(USBH_CDC_RW_CONTEXT, pUrb->Header.pUserContext);
  pBulkRequest  = &pUrb->Request.BulkIntRequest;
  EPAddr        = pBulkRequest->Endpoint;
  pEPData       = _GetEPData(pInst, EPAddr);
  if (pEPData != NULL) {
    pEPData->InUse = FALSE;
  }
  //
  //  Update RWContext
  //
  pRWContext->Status = pUrb->Header.Status;
  pRWContext->NumBytesTransferred = pBulkRequest->Length;
  (void)DEC_REF_CNT(pInst);
  //
  // Call user function
  //
  pfOnComplete(pRWContext);
}

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       USBH_CDC_Init
*
*  Function description
*    Initializes and registers the CDC device module with emUSB-Host.
*
*  Return value
*    == 1:   Success or module already initialized.
*    == 0:   Could not register CDC device module.
*
*  Additional information
*    This function can be called multiple times, but only the first
*    call initializes the module. Any further calls only increase
*    the initialization counter. This is useful for cases where
*    the module is initialized from different places which
*    do not interact with each other, To de-initialize
*    the module USBH_CDC_Exit has to be called
*    the same number of times as this function was called.
*/
U8 USBH_CDC_Init(void) {
#if USBH_CDC_DISABLE_AUTO_DETECT == 0
  USBH_PNP_NOTIFICATION   PnpNotifyACM;
  USBH_INTERFACE_MASK   * pInterfaceMaskACM;
  USBH_PNP_NOTIFICATION   PnpNotifyData;
  USBH_INTERFACE_MASK   * pInterfaceMaskData;
#endif
  if (_isInited == 0) {
    USBH_LOG((USBH_MCAT_CDC, "USBH_CDC_Init"));
    USBH_MEMSET(&USBH_CDC_Global, 0, sizeof(USBH_CDC_Global));
    USBH_CDC_Global.DefaultReadTimeOut = USBH_CDC_DEFAULT_TIMEOUT;
    USBH_CDC_Global.DefaultWriteTimeOut = USBH_CDC_DEFAULT_TIMEOUT;
    //
    // Add CDC ACM PnP notification, this makes sure that as soon as a device with the specific interface
    // is available we will be notified.
    //
#if USBH_CDC_DISABLE_AUTO_DETECT == 0
    pInterfaceMaskACM = &PnpNotifyACM.InterfaceMask;
    pInterfaceMaskACM->Mask = USBH_INFO_MASK_CLASS;
    pInterfaceMaskACM->Class = USB_DEVICE_CLASS_COMMUNICATIONS;
    //  pInterfaceMaskACM->SubClass  = USBH_CDC_ABSTRACT_LINE_CONTROL_MODEL;
    PnpNotifyACM.pContext = NULL;
    PnpNotifyACM.pfPnpNotification = _ACM_OnDeviceNotification;
    USBH_CDC_Global.hDevNotificationACM = USBH_RegisterPnPNotification(&PnpNotifyACM); // Register the  PNP notification
    if (NULL == USBH_CDC_Global.hDevNotificationACM) {
      USBH_WARN((USBH_MCAT_CDC, "USBH_CDC_Init: USBH_RegisterPnPNotification"));
      return 0;
    }
    // Add CDC DATA notification
    pInterfaceMaskData = &PnpNotifyData.InterfaceMask;
    pInterfaceMaskData->Mask = USBH_INFO_MASK_CLASS;
    pInterfaceMaskData->Class = USB_DEVICE_CLASS_DATA;
    PnpNotifyData.pContext = NULL;
    PnpNotifyData.pfPnpNotification = _DATA_OnDeviceNotification;
    USBH_CDC_Global.hDevNotificationData = USBH_RegisterPnPNotification(&PnpNotifyData); // Register the  PNP notification
    if (NULL == USBH_CDC_Global.hDevNotificationData) {
      USBH_WARN((USBH_MCAT_CDC, "USBH_CDC_Init: USBH_RegisterPnPNotification"));
      USBH_UnregisterPnPNotification(USBH_CDC_Global.hDevNotificationACM);
      USBH_CDC_Global.hDevNotificationACM = NULL;
      return 0;
    }
#endif
  }
  _isInited++;
  return 1;
}

/*********************************************************************
*
*       USBH_CDC_Exit
*
*  Function description
*    Unregisters and de-initializes the CDC device module from emUSB-Host.
*
*  Additional information
*    Before this function is called any notifications added via
*    USBH_CDC_AddNotification() must be removed
*    via USBH_CDC_RemoveNotification().
*    Has to be called the same number of times USBH_CDC_Init was
*    called in order to de-initialize the module.
*    This function will release resources that were used by this
*    device driver. It has to be called if the application is closed.
*    This has to be called before USBH_Exit() is called. No more
*    functions of this module may be called after calling
*    USBH_CDC_Exit(). The only exception is USBH_CDC_Init(),
*    which would in turn re-init the module and allow further calls.
*/
void USBH_CDC_Exit(void) {
  USBH_CDC_INST * pInst;

  _isInited--;
  if (_isInited == 0) {
    USBH_LOG((USBH_MCAT_CDC, "USBH_CDC_Exit"));
    pInst = USBH_CDC_Global.pFirst;
    while (pInst != NULL) {   // Iterate over all instances
      while (pInst->IsOpened != 0) {
        --pInst->IsOpened;
        (void)DEC_REF_CNT(pInst);
      }
      _StopDevice(pInst);
      pInst = pInst->pNext;
    }
    if (USBH_CDC_Global.hDevNotificationACM != NULL) {
      USBH_UnregisterPnPNotification(USBH_CDC_Global.hDevNotificationACM);
      USBH_CDC_Global.hDevNotificationACM = NULL;
    }
    if (USBH_CDC_Global.hDevNotificationData != NULL) {
      USBH_UnregisterPnPNotification(USBH_CDC_Global.hDevNotificationData);
      USBH_CDC_Global.hDevNotificationData = NULL;
    }
    _RemoveAllInstances();
  }
}

/*********************************************************************
*
*       USBH_CDC_Open
*
*  Function description
*    Opens a device given by an index.
*
*  Parameters
*    Index   : Index of the device that shall be opened.
*              In general this means: the first connected device is 0,
*              second device is 1 etc.
*
*  Return value
*    == USBH_CDC_INVALID_HANDLE     : Device not available or removed.
*    != USBH_CDC_INVALID_HANDLE     : Handle to a CDC device
*
*  Additional information
*    The index of a new connected device is provided to the callback function
*    registered with USBH_CDC_AddNotification().
*/
USBH_CDC_HANDLE USBH_CDC_Open(unsigned Index) {
  USBH_CDC_INST * pInst;
  USBH_CDC_HANDLE Handle;

  Handle = USBH_CDC_INVALID_HANDLE;
  pInst = USBH_CDC_Global.pFirst;
  while (pInst != NULL) {
    if (Index == pInst->DevIndex) {
      //
      // Device found
      //
      if (INC_REF_CNT(pInst) != USBH_STATUS_SUCCESS) {
        return USBH_CDC_INVALID_HANDLE;
      }
      Handle = pInst->Handle;
      pInst->IsOpened++;
      break;
    }
    pInst = pInst->pNext;
  }
  return Handle;
}

/*********************************************************************
*
*       USBH_CDC_Close
*
*  Function description
*    Closes a handle to an opened device.
*
*  Parameters
*    hDevice    :  Handle to an open device returned by USBH_CDC_Open().
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_CDC_Close(USBH_CDC_HANDLE hDevice) {
  USBH_CDC_INST * pInst;

  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    if (pInst->IsOpened == 0) {
      return USBH_STATUS_NOT_OPENED;
    }
    if (--pInst->IsOpened == 0) {
      //
      // Last handle closed, reset settings.
      //
      pInst->ReadTimeOut           = USBH_CDC_Global.DefaultReadTimeOut;
      pInst->WriteTimeOut          = USBH_CDC_Global.DefaultWriteTimeOut;
      pInst->AllowShortRead        = 0;
      pInst->pfOnSerialStateChange = NULL;
      pInst->pfOnIntState          = NULL;
    }
    (void)DEC_REF_CNT(pInst);
    return USBH_STATUS_SUCCESS;
  }
  return USBH_STATUS_DEVICE_REMOVED;
}

/*********************************************************************
*
*       USBH_CDC_Write
*
*  Function description
*    Writes data to the CDC device. The function blocks until all data
*    has been written or until the timeout has been reached. If a timeout
*    is not specified via USBH_CDC_SetTimeouts() the default timeout (USBH_CDC_DEFAULT_TIMEOUT) is used.
*
*  Parameters
*    hDevice          : Handle to an open device returned by USBH_CDC_Open().
*    pData            : Pointer to data to be sent.
*    NumBytes         : Number of bytes to send.
*    pNumBytesWritten : Pointer to a variable  which receives the number
*                       of bytes written to the device. Can be NULL.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*
*  Additional information
*    If the function returns an error code (including USBH_STATUS_TIMEOUT) it already may
*    have written part of the data. The number of bytes written successfully is always
*    stored in the variable pointed to by pNumBytesWritten.
*/
USBH_STATUS USBH_CDC_Write(USBH_CDC_HANDLE hDevice, const U8 * pData, U32 NumBytes, U32 * pNumBytesWritten) {
  USBH_CDC_INST * pInst;
  USBH_STATUS     Status;
  CDC_EP_DATA   * pEPData;
  U32             BytesAtOnce;
  U32             NumBytesWritten;

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
  pEPData = &pInst->BulkOut;
  if (pEPData->InUse != FALSE) {
    return USBH_STATUS_BUSY;
  }
  pEPData->InUse = TRUE;
  NumBytesWritten = 0;
  do {
    BytesAtOnce = NumBytes;
    if (BytesAtOnce > pInst->MaxOutTransferSize) {
      BytesAtOnce = pInst->MaxOutTransferSize;
    }
    pEPData->Urb.Header.pContext                 = pInst;
    pEPData->Urb.Header.Function                 = USBH_FUNCTION_BULK_REQUEST;
    pEPData->Urb.Request.BulkIntRequest.Endpoint = pEPData->EPAddr;
    pEPData->Urb.Request.BulkIntRequest.pBuffer  = (U8 *)pData;       //lint !e9005 D:105[a]
    pEPData->Urb.Request.BulkIntRequest.Length   = BytesAtOnce;
    Status = INC_REF_CNT(pInst);
    if (Status == USBH_STATUS_SUCCESS) {
      Status = _SubmitUrbAndWait(pInst, pInst->hDATAInterface, pEPData, pInst->WriteTimeOut);
      if (DEC_REF_CNT(pInst) != 0) {
        Status = USBH_STATUS_DEVICE_REMOVED;
      }
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
    _ResetPipe(pInst, pEPData->EPAddr);
    break;
  default:
    //
    // In any other case, output a warning.
    //
    USBH_WARN((USBH_MCAT_CDC, "USBH_CDC_Write failed, Status = %s", USBH_GetStatusStr(Status)));
    break;
  }
  pEPData->InUse = FALSE;
  return Status;
}

/*********************************************************************
*
*       USBH_CDC_Read
*
*  Function description
*    Reads from the CDC device. Depending of the ShortRead mode (see USBH_CDC_AllowShortRead()),
*    this function will either return as soon as data are available or
*    all data have been read from the device.
*    This function will also return when a set timeout is expired,
*    whatever comes first. If a timeout
*    is not specified via USBH_CDC_SetTimeouts() the default timeout (USBH_CDC_DEFAULT_TIMEOUT) is used.
*
*    The USB stack can only read complete packets from the USB device.
*    If the size of a received packet exceeds NumBytes then all data that does not
*    fit into the callers buffer (pData) is stored in an internal buffer and
*    will be returned by the next call to USBH_CDC_Read(). See also USBH_CDC_GetQueueStatus().
*
*    To read a null packet, set pData = NULL and NumBytes = 0.
*    For this, the internal buffer must be empty.
*
*  Parameters
*    hDevice        : Handle to an open device returned by USBH_CDC_Open().
*    pData          : Pointer to a buffer to store the read data.
*    NumBytes       : Number of bytes to be read from the device.
*    pNumBytesRead  : Pointer to a variable  which receives the number
*                     of bytes read from the device. Can be NULL.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*
*  Additional information
*    If the function returns an error code (including USBH_STATUS_TIMEOUT) it already may
*    have read part of the data. The number of bytes read successfully is always
*    stored in the variable pointed to by pNumBytesRead.
*/
USBH_STATUS USBH_CDC_Read(USBH_CDC_HANDLE hDevice, U8 * pData, U32 NumBytes, U32 * pNumBytesRead) {
  USBH_CDC_INST  * pInst;
  USBH_STATUS      Status;
  U32              NumBytesTotal;
  U32              NumBytesRead;
  U32              NumBytes2Read;
  U32              NumBytesTransfered;
  U32              NumBytes2Copy;
  USBH_TIME        ExpiredTime;
  U8             * pBuf;
  CDC_EP_DATA    * pEPData;

  if (pNumBytesRead != NULL) {
    *pNumBytesRead = 0;
  }
  pEPData = NULL;
  pInst = _h2p(hDevice);
  NumBytesTotal = NumBytes;
  if (pInst == NULL) {
    return USBH_STATUS_INVALID_HANDLE;
  }
  if (pInst->IsOpened == 0) {
    return USBH_STATUS_NOT_OPENED;
  }
  if (pData == NULL && NumBytes != 0u) {
    return USBH_STATUS_INVALID_PARAM;
  }
  if (pData == NULL) {
    //
    // Reading a NULL packet is possible only if the buffer is empty.
    // (A non-zero-length packet may be received).
    //
    if (pInst->RxRingBuffer.NumBytesIn != 0u) {
      return USBH_STATUS_INTERNAL_BUFFER_NOT_EMPTY;
    }
  } else {
    NumBytesTransfered = USBH_BUFFER_Read(&pInst->RxRingBuffer, pData, NumBytesTotal);
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
  //
  // We should at least have 2 ms in order to have
  // enough time to receive at least one byte from device.
  //
#if USBH_DEBUG > 1
  if (pInst->ReadTimeOut < 2) {
    USBH_WARN((USBH_MCAT_CDC, "Read timeout too small [%d]! Data loss likely.", pInst->ReadTimeOut));
  }
#endif
  //
  // Check if the endpoint is not in use.
  //
  pEPData = &pInst->BulkIn;
  if (pEPData->InUse != FALSE) {
    return USBH_STATUS_BUSY;
  }
  pEPData->InUse = TRUE;
  ExpiredTime = USBH_TIME_CALC_EXPIRATION(pInst->ReadTimeOut);
  for(;;) {
    if (USBH_TIME_IS_EXPIRED(ExpiredTime)) {
      Status = USBH_STATUS_TIMEOUT;
      goto ReadEnd;
    }
    //
    // Check whether we can use the user buffer directly to read data into.
    // This is possible if the buffer is a multiple of MaxPacketSize.
    //
    if (pData != NULL && (NumBytesTotal % pEPData->MaxPacketSize) == 0u) {
      pBuf = pData;
      NumBytes2Read = USBH_MIN(NumBytesTotal, pInst->MaxInTransferSize);
    } else {
      pBuf = pInst->pBulkInBuffer;
      NumBytes2Read = pEPData->MaxPacketSize;
    }
    //
    // Fill URB structure, if endpoint is not in use.
    //
    USBH_MEMSET(&pEPData->Urb, 0, sizeof(USBH_URB));
    pEPData->Urb.Header.Function = USBH_FUNCTION_BULK_REQUEST;
    pEPData->Urb.Request.BulkIntRequest.Endpoint = pEPData->EPAddr;
    pEPData->Urb.Request.BulkIntRequest.pBuffer  = pBuf;
    pEPData->Urb.Request.BulkIntRequest.Length   = NumBytes2Read;
    //
    // Send and wait until data have been received.
    // In case of an error the function will also return
    //
    Status = INC_REF_CNT(pInst);
    if (Status == USBH_STATUS_SUCCESS) {
      Status = _SubmitUrbAndWait(pInst, pInst->hDATAInterface, pEPData, pInst->ReadTimeOut);
      if (DEC_REF_CNT(pInst) != 0) {
        Status = USBH_STATUS_DEVICE_REMOVED;
      }
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
      if (pBuf == pInst->pBulkInBuffer) {
        if (NumBytesTotal == 0u) {
          USBH_BUFFER_Write(&pInst->RxRingBuffer, pInst->pBulkInBuffer, NumBytesRead);
          goto ReadEnd;
        }
        NumBytes2Copy = USBH_MIN(NumBytesRead, NumBytesTotal);
        USBH_MEMCPY(pData, pInst->pBulkInBuffer, NumBytes2Copy);
        if (pNumBytesRead != NULL) {
          *pNumBytesRead += NumBytes2Copy;
        }
        pData += NumBytes2Copy;
        NumBytesTotal -= NumBytes2Copy;
        NumBytesRead -= NumBytes2Copy;
        if (NumBytesRead != 0u) {
          USBH_BUFFER_Write(&pInst->RxRingBuffer, &pInst->pBulkInBuffer[NumBytes2Copy], NumBytesRead);
        }
      } else {
        pData += NumBytesRead;
        NumBytesTotal -= NumBytesRead;
        if (pNumBytesRead != NULL) {
          *pNumBytesRead += NumBytesRead;
        }
      }
      if (pInst->AllowShortRead != 0u) {
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
          USBH_WARN((USBH_MCAT_CDC, "USBH_CDC_Read failed, Status = %s", USBH_GetStatusStr(Status)));
        }
      }
      break;
    }
  }
ReadEnd:
  if (pEPData != NULL) {
    pEPData->InUse = FALSE;
  }
  return Status;
}

/*********************************************************************
*
*       USBH_CDC_RegisterNotification
*
*  Function description
*    This function is deprecated, please use function USBH_CDC_AddNotification!
*    Sets a callback in order to be notified when a device is added or removed.
*
*  Parameters
*    pfNotification  : Pointer to a function the stack should call when a device is connected or disconnected.
*    pContext        : Pointer to a user context that is passed to the callback function.
*
*  Additional information
*    This function is deprecated, please use function USBH_CDC_AddNotification.
*/
void USBH_CDC_RegisterNotification(USBH_NOTIFICATION_FUNC * pfNotification, void * pContext) {
  static USBH_NOTIFICATION_HOOK _Hook;
  (void)USBH_CDC_AddNotification(&_Hook, pfNotification, pContext);
}

/*********************************************************************
*
*       USBH_CDC_AddNotification
*
*  Function description
*    Adds a callback in order to be notified when a device is added or removed.
*
*  Parameters
*    pHook           : Pointer to a user provided USBH_NOTIFICATION_HOOK variable.
*    pfNotification  : Pointer to a function the stack should call when a device is connected or disconnected.
*    pContext        : Pointer to a user context that is passed to the callback function.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_CDC_AddNotification(USBH_NOTIFICATION_HOOK * pHook, USBH_NOTIFICATION_FUNC * pfNotification, void * pContext) {
  return USBH__AddNotification(pHook, pfNotification, pContext, &USBH_CDC_Global.pFirstNotiHook, NULL);
}

/*********************************************************************
*
*       USBH_CDC_RemoveNotification
*
*  Function description
*    Removes a callback added via USBH_CDC_AddNotification.
*
*  Parameters
*    pHook          : Pointer to a user provided USBH_NOTIFICATION_HOOK variable.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_CDC_RemoveNotification(const USBH_NOTIFICATION_HOOK * pHook) {
  return USBH__RemoveNotification(pHook, &USBH_CDC_Global.pFirstNotiHook);
}

/*********************************************************************
*
*       USBH_CDC_GetDeviceInfo
*
*  Function description
*    Retrieves information about the CDC device.
*
*  Parameters
*    hDevice    : Handle to an open device returned by USBH_CDC_Open().
*    pDevInfo   : Pointer to a USBH_CDC_DEVICE_INFO structure that receives the information.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_CDC_GetDeviceInfo(USBH_CDC_HANDLE hDevice, USBH_CDC_DEVICE_INFO * pDevInfo) {
  USBH_CDC_INST       * pInst;
  USBH_INTERFACE_INFO   InterFaceInfo;
  USBH_STATUS           Status;

  USBH_ASSERT_PTR(pDevInfo);
  pInst = _h2p(hDevice);
  if (pInst == NULL) {
    return USBH_STATUS_INVALID_HANDLE;
  }
  if (pInst->IsOpened == 0) {
    return USBH_STATUS_NOT_OPENED;
  }
  Status = INC_REF_CNT(pInst);
  if (Status != USBH_STATUS_SUCCESS) {
    return Status;
  }
  Status = USBH_GetInterfaceInfo(pInst->DATAInterfaceID, &InterFaceInfo);
  if (Status != USBH_STATUS_SUCCESS) {
    goto End;
  }
  pDevInfo->DataInterfaceID = pInst->DATAInterfaceID;
  pDevInfo->VendorId        = InterFaceInfo.VendorId;
  pDevInfo->ProductId       = InterFaceInfo.ProductId;
  pDevInfo->DataInterfaceNo = InterFaceInfo.Interface;
  pDevInfo->DataClass       = InterFaceInfo.Class;
  pDevInfo->DataSubClass    = InterFaceInfo.SubClass;
  pDevInfo->DataProtocol    = InterFaceInfo.Protocol;
  pDevInfo->Speed           = InterFaceInfo.Speed;
  pDevInfo->MaxPacketSize   = pInst->BulkIn.MaxPacketSize;
  Status = USBH_GetInterfaceInfo(pInst->ControlInterfaceID, &InterFaceInfo);
  if (Status != USBH_STATUS_SUCCESS) {
    goto End;
  }
  pDevInfo->ControlInterfaceID = pInst->ControlInterfaceID;
  pDevInfo->ControlInterfaceNo = InterFaceInfo.Interface;
  pDevInfo->ControlClass       = InterFaceInfo.Class;
  pDevInfo->ControlSubClass    = InterFaceInfo.SubClass;
  pDevInfo->ControlProtocol    = InterFaceInfo.Protocol;
End:
  (void)DEC_REF_CNT(pInst);
  return Status;
}

/*********************************************************************
*
*       USBH_CDC_SetTimeouts
*
*  Function description
*    Sets up the timeouts for read and write operations.
*
*  Parameters
*    hDevice       : Handle to an open device returned by USBH_CDC_Open().
*    ReadTimeout   : Read  timeout given in ms.
*    WriteTimeout  : Write timeout given in ms.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_CDC_SetTimeouts(USBH_CDC_HANDLE hDevice, U32 ReadTimeout, U32 WriteTimeout) {
  USBH_CDC_INST * pInst;

  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    if (INC_REF_CNT(pInst) == USBH_STATUS_SUCCESS) {
      pInst->ReadTimeOut  = ReadTimeout;
      pInst->WriteTimeOut = WriteTimeout;
      (void)DEC_REF_CNT(pInst);
      return USBH_STATUS_SUCCESS;
    }
    return USBH_STATUS_DEVICE_REMOVED;
  }
  return USBH_STATUS_INVALID_HANDLE;
}

/*********************************************************************
*
*       USBH_CDC_AllowShortRead
*
*  Function description
*    Enables or disables short read mode.
*    If enabled, the function USBH_CDC_Read() returns as soon as data was
*    read from the device. This allows the application to read data where the number of
*    bytes to read is undefined.
*
*  Parameters
*    hDevice        : Handle to an open device returned by USBH_CDC_Open().
*    AllowShortRead : Define whether short read mode shall be used or not.
*                     * 1 - Allow short read.
*                     * 0 - Short read mode disabled.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_CDC_AllowShortRead(USBH_CDC_HANDLE hDevice, U8 AllowShortRead) {
  USBH_CDC_INST * pInst;

  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    if (INC_REF_CNT(pInst) == USBH_STATUS_SUCCESS) {
      pInst->AllowShortRead = AllowShortRead;
      (void)DEC_REF_CNT(pInst);
      return USBH_STATUS_SUCCESS;
    }
    return USBH_STATUS_DEVICE_REMOVED;
  }
  return USBH_STATUS_INVALID_HANDLE;
}

/*********************************************************************
*
*       USBH_CDC_SetCommParas
*
*  Function description
*    Setups the serial communication with the given characteristics.
*
*  Parameters
*    hDevice  : Handle to an open device returned by USBH_CDC_Open().
*    Baudrate : Transfer rate.
*    DataBits : Number of bits per word. Must be between USBH_CDC_BITS_5 and USBH_CDC_BITS_8.
*    StopBits : Number of stop bits. Must be USBH_CDC_STOP_BITS_1 or USBH_CDC_STOP_BITS_2.
*    Parity   : Parity - must be must be one of the following values:
*               * UBSH_CDC_PARITY_NONE
*               * UBSH_CDC_PARITY_ODD
*               * UBSH_CDC_PARITY_EVEN
*               * UBSH_CDC_PARITY_MARK
*               * USBH_CDC_PARITY_SPACE
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_CDC_SetCommParas(USBH_CDC_HANDLE hDevice, U32 Baudrate, U8  DataBits, U8 StopBits,  U8 Parity) {
  USBH_CDC_INST * pInst;
  U32 NumBytes;

  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    USBH_STATUS Status;

    Status = INC_REF_CNT(pInst);
    if (Status == USBH_STATUS_SUCCESS) {
      USBH_ASSERT(((DataBits == 5) || (DataBits == 6) || (DataBits == 7) || (DataBits == 8) || (DataBits == 16)));
      USBH_ASSERT((StopBits <= 1));
      USBH_ASSERT((Parity <= 4));

      USBH_MEMSET(pInst->aEP0Buffer, 0, sizeof(pInst->aEP0Buffer));
      USBH_StoreU32LE(&pInst->aEP0Buffer[0], Baudrate);
      pInst->aEP0Buffer[4] = StopBits;
      pInst->aEP0Buffer[5] = Parity;
      pInst->aEP0Buffer[6] = DataBits;
      NumBytes = USBH_CDC_SET_LINE_CODING_LEN;
      Status = _SendControlRequest(pInst, USB_REQTYPE_CLASS | USB_INTERFACE_RECIPIENT, USBH_CDC_REQ_SET_LINE_CODING, 0, pInst->ACMInterfaceNo, USBH_CDC_SET_LINE_CODING_LEN, pInst->aEP0Buffer, &NumBytes);
      if (DEC_REF_CNT(pInst) != 0) {
        Status = USBH_STATUS_DEVICE_REMOVED;
      }
    }
    return Status;
  }
  return USBH_STATUS_INVALID_HANDLE;
}

/*********************************************************************
*
*       USBH_CDC_SetDtr
*
*  Function description
*    Sets the Data Terminal Ready (DTR) control signal.
*
*  Parameters
*    hDevice    :  Handle to an open device returned by USBH_CDC_Open().
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_CDC_SetDtr(USBH_CDC_HANDLE hDevice) {
  return _SendControlLineState(hDevice, (1uL << USBH_CDC_DTR_BIT), 0);
}

/*********************************************************************
*
*       USBH_CDC_ClrDtr
*
*  Function description
*    Clears the Data Terminal Ready (DTR) control signal.
*
*  Parameters
*    hDevice    :  Handle to an open device returned by USBH_CDC_Open().
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_CDC_ClrDtr(USBH_CDC_HANDLE hDevice) {
  return _SendControlLineState(hDevice, 0, (1uL << USBH_CDC_DTR_BIT));
}

/*********************************************************************
*
*       USBH_CDC_SetRts
*
*  Function description
*    Sets the Request To Send (RTS) control signal.
*
*  Parameters
*    hDevice    :  Handle to an open device returned by USBH_CDC_Open().
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_CDC_SetRts(USBH_CDC_HANDLE hDevice) {
  return _SendControlLineState(hDevice, (1uL << USBH_CDC_RTS_BIT), 0);
}

/*********************************************************************
*
*       USBH_CDC_ClrRts
*
*  Function description
*    Clears the Request To Send (RTS) control signal.
*
*  Parameters
*    hDevice    :  Handle to an open device returned by USBH_CDC_Open().
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_CDC_ClrRts(USBH_CDC_HANDLE hDevice) {
  return _SendControlLineState(hDevice, 0, (1uL << USBH_CDC_RTS_BIT));
}

/*********************************************************************
*
*       USBH_CDC_GetSerialState
*
*  Function description
*    Gets the modem status and line status from the device.
*
*  Parameters
*    hDevice      : Handle to an open device returned by USBH_CDC_Open().
*    pSerialState : Pointer to a structure of type USBH_CDC_SERIALSTATE which receives the
*                   serial status from the device.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*
*  Additional information
*    The least significant byte of the pSerialState value holds the modem status.
*    The line status is held in the second least significant byte of the pSerialState value.
*    The status is bit-mapped as follows:
*    * Data Carrier Detect  (DCD) = 0x01
*    * Data Set Ready       (DSR) = 0x02
*    * Break Interrupt      (BI)  = 0x04
*    * Ring Indicator       (RI)  = 0x08
*    * Framing Error        (FE)  = 0x10
*    * Parity Error         (PE)  = 0x20
*    * Overrun Error        (OE)  = 0x40
*/
USBH_STATUS USBH_CDC_GetSerialState(USBH_CDC_HANDLE hDevice, USBH_CDC_SERIALSTATE * pSerialState) {
  USBH_CDC_INST * pInst;
  USBH_STATUS     Status;

  Status = USBH_STATUS_INVALID_HANDLE;
  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    *pSerialState = pInst->SerialState;
    Status = USBH_STATUS_SUCCESS;
  }
  return Status;
}

/*********************************************************************
*
*       USBH_CDC_GetQueueStatus
*
*  Function description
*    Gets the number of bytes in the receive queue.
*
*    The USB stack can only read complete packets from the USB device.
*    If the size of a received packet exceeds the number of bytes requested
*    with USBH_CDC_Read(), than all data that is not returned by USBH_CDC_Read()
*    is stored in an internal buffer.
*
*    The number of bytes returned by USBH_CDC_GetQueueStatus() can be read
*    using USBH_CDC_Read() out of the buffer without a USB transaction
*    to the USB device being executed.
*
*  Parameters
*    hDevice  : Handle to an open device returned by USBH_CDC_Open().
*    pRxBytes : Pointer to a variable which receives the number
*               of bytes in the receive queue.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_CDC_GetQueueStatus(USBH_CDC_HANDLE hDevice, U32 * pRxBytes) {
  USBH_CDC_INST * pInst;

  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    if (pInst->IsOpened != 0) {
      USBH_ASSERT_PTR(pRxBytes);
      if (INC_REF_CNT(pInst) == USBH_STATUS_SUCCESS) {
        *pRxBytes = pInst->RxRingBuffer.NumBytesIn;
        (void)DEC_REF_CNT(pInst);
        return USBH_STATUS_SUCCESS;
      }
      return USBH_STATUS_DEVICE_REMOVED;
    } else {
      return USBH_STATUS_NOT_OPENED;
    }
  }
  return USBH_STATUS_INVALID_HANDLE;
}

/*********************************************************************
*
*       USBH_CDC_FlushBuffer
*
*  Function description
*    Clears the receive queue used by USBH_CDC_Read().
*    See also USBH_CDC_GetQueueStatus().
*
*  Parameters
*    hDevice  : Handle to an open device returned by USBH_CDC_Open().
*/
void USBH_CDC_FlushBuffer(USBH_CDC_HANDLE hDevice) {
  USBH_CDC_INST * pInst;

  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    if (pInst->IsOpened != 0) {
      if (INC_REF_CNT(pInst) == USBH_STATUS_SUCCESS) {
        pInst->RxRingBuffer.NumBytesIn = 0;
        pInst->RxRingBuffer.RdPos      = 0;
        (void)DEC_REF_CNT(pInst);
      }
    }
  }
}

/*********************************************************************
*
*       USBH_CDC_SetBreakOn
*
*  Function description
*    Sets the BREAK condition for the device to "on".
*
*  Parameters
*    hDevice    :  Handle to an open device returned by USBH_CDC_Open().
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_CDC_SetBreakOn(USBH_CDC_HANDLE hDevice) {
  USBH_CDC_INST * pInst;
  U16             wValue;

  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    wValue = 0xFFFF;
    return _SendControlRequest(pInst, USB_REQTYPE_CLASS | USB_INTERFACE_RECIPIENT, USBH_CDC_REQ_SEND_BREAK, wValue, pInst->ACMInterfaceNo, 0, NULL, NULL);
  }
  return USBH_STATUS_INVALID_HANDLE;
}

/*********************************************************************
*
*       USBH_CDC_SetBreakOff
*
*  Function description
*    Resets the BREAK condition for the device.
*
*  Parameters
*    hDevice    :  Handle to an open device returned by USBH_CDC_Open().
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_CDC_SetBreakOff(USBH_CDC_HANDLE hDevice) {
  USBH_CDC_INST * pInst;
  U16             wValue;

  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    wValue = 0;
    return _SendControlRequest(pInst, USB_REQTYPE_CLASS | USB_INTERFACE_RECIPIENT, USBH_CDC_REQ_SEND_BREAK, wValue, pInst->ACMInterfaceNo, 0, NULL, NULL);
  }
  return USBH_STATUS_INVALID_HANDLE;
}

/*********************************************************************
*
*       USBH_CDC_SetBreak
*
*  Function description
*    Sets the BREAK condition for the device for a limited time.
*
*  Parameters
*    hDevice  : Handle to an open device returned by USBH_CDC_Open().
*    Duration : Duration of the break condition in ms.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_CDC_SetBreak(USBH_CDC_HANDLE hDevice, U16 Duration) {
  USBH_CDC_INST * pInst;

  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    return _SendControlRequest(pInst, USB_REQTYPE_CLASS | USB_INTERFACE_RECIPIENT, USBH_CDC_REQ_SEND_BREAK, Duration, pInst->ACMInterfaceNo, 0, NULL, NULL);
  }
  return USBH_STATUS_INVALID_HANDLE;
}

/*********************************************************************
*
*       USBH_CDC_ConfigureDefaultTimeout
*
*  Function description
*    Sets the default read and write time-out that shall be used when
*    a new device is connected.
*
*  Parameters
*    ReadTimeout    : Default read timeout given in ms.
*    WriteTimeout   : Default write timeout given in ms.
*/
void USBH_CDC_ConfigureDefaultTimeout(U32 ReadTimeout, U32 WriteTimeout) {
  USBH_CDC_Global.DefaultReadTimeOut  = ReadTimeout;
  USBH_CDC_Global.DefaultWriteTimeOut = WriteTimeout;
}

/*********************************************************************
*
*       USBH_CDC_AddDevice
*
*  Function description
*    Register a device with a non-standard interface layout as a CDC device.
*    This function should not be used for CDC compliant devices!
*    After registering the device the application will receive ADD and REMOVE notifications
*    to the user callback which was set by USBH_CDC_AddNotification().
*
*  Parameters
*    ControlInterfaceID   : Numeric index of the CDC ACM interface.
*    DataInterfaceId  : Numeric index of the CDC Data interface.
*    Flags            : Reserved for future use. Should be zero.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*
*  Additional information
*    The numeric interface IDs can be retrieved by setting up a PnP notification via
*    USBH_RegisterPnPNotification(). Please note that the PnP notification callback
*    will be triggered for each interface, but you only have to add the device once.
*    Alternatively you can simply set the IDs if you know the interface layout.
*/
USBH_STATUS USBH_CDC_AddDevice(USBH_INTERFACE_ID ControlInterfaceID, USBH_INTERFACE_ID DataInterfaceId, unsigned Flags) {
  USBH_CDC_INST * pInst;
  USBH_STATUS     Status;

  pInst = _CreateDevInstance();
  if (pInst == NULL) {
    USBH_WARN((USBH_MCAT_CDC, "No memory available to create new CDC interface"));
    return USBH_STATUS_MEMORY;
  }
  pInst->ControlInterfaceID = ControlInterfaceID;
  pInst->DATAInterfaceID = DataInterfaceId;
  pInst->Flags           = Flags;
  pInst->RunningState = StateInit;
  Status = _StartDevice(pInst);
  pInst->RunningState = StateRunning;
  if (Status == USBH_STATUS_SUCCESS) {
    USBH_NOTIFICATION_HOOK * pHook;

    pHook = USBH_CDC_Global.pFirstNotiHook;
    while (pHook != NULL) {
      if (pHook->pfNotification != NULL) {
        pHook->pfNotification(pHook->pContext, pInst->DevIndex, USBH_DEVICE_EVENT_ADD);
      }
      pHook = pHook->pNext;
    }
  }
  return Status;
}

/*********************************************************************
*
*       USBH_CDC_RemoveDevice
*
*  Function description
*    Removes a non-standard CDC device which was added by USBH_CDC_AddDevice().
*
*  Parameters
*    ControlInterfaceID   : Numeric index of the CDC ACM interface.
*    DataInterfaceId  : Numeric index of the CDC Data interface.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_CDC_RemoveDevice(USBH_INTERFACE_ID ControlInterfaceID, USBH_INTERFACE_ID DataInterfaceId) {
  USBH_CDC_INST * pInst;

  pInst = USBH_CDC_Global.pFirst;
  while (pInst != NULL) {   // Iterate over all instances
    //
    // When found, delete
    //
    if ((pInst->DATAInterfaceID == DataInterfaceId) && (pInst->ControlInterfaceID == ControlInterfaceID)) {
      USBH_NOTIFICATION_HOOK * pHook;

      USBH_LOG((USBH_MCAT_CDC, "USBH_CDC_RemoveDevice: USB CDC device removed interface  ID: %u !", pInst->ACMInterfaceNo));
      pHook = USBH_CDC_Global.pFirstNotiHook;
      while (pHook != NULL) {
        if (pHook->pfNotification != NULL) {
          pHook->pfNotification(pHook->pContext, pInst->DevIndex, USBH_DEVICE_EVENT_REMOVE);
        }
        pHook = pHook->pNext;
      }
      _StopDevice(pInst);
      (void)DEC_REF_CNT(pInst);  // CreateDevInstance()
      (void)DEC_REF_CNT(pInst);  // CreateDevInstance() This is done twice because a CDC instance has two interfaces.
      return USBH_STATUS_SUCCESS;
    }
    pInst = pInst->pNext;
  }
  return USBH_STATUS_INVALID_PARAM;
}

/*********************************************************************
*
*       USBH_CDC_ReadAsync
*
*  Function description
*    Triggers a read transfer to the CDC device. The result of
*    the transfer is received through the user callback.
*    This function will return immediately while the read transfer is
*    done asynchronously. The read operation terminates either, if 'BuffSize'
*    bytes have been read or if a short packet was received from the device.
*
*  Parameters
*    hDevice       : Handle to an open device returned by USBH_CDC_Open().
*    pBuffer       : Pointer to the buffer that receives the data
*                    from the device.
*    BufferSize    : Size of the buffer in bytes. Must be a multiple of
*                    of the maximum packet size of the USB device.
*                    Use USBH_CDC_GetMaxTransferSize() to get the maximum allowed size.
*    pfOnComplete  : Pointer to a user function of type  USBH_CDC_ON_COMPLETE_FUNC
*                    which will be called after the transfer has been completed.
*    pRWContext    : Pointer to a USBH_CDC_RW_CONTEXT structure which
*                    will be filled with data after the transfer has
*                    been completed and passed as a parameter to the
*                    pfOnComplete function. The member 'pUserContext' may be set before
*                    calling USBH_CDC_ReadAsync(). Other members need not be initialized and
*                    are set by the function USBH_CDC_ReadAsync().
*                    The memory used for this structure must be valid,
*                    until the transaction is completed.
*
*  Return value
*    == USBH_STATUS_PENDING : Success, the data transfer is queued,
*                             the user callback will be called after
*                             the transfer is finished.
*    != USBH_STATUS_PENDING : An error occurred, the transfer is not started
*                             and user callback will not be called.
*
*  Additional information
*    This function performs an unbuffered read operation (in contrast to USBH_CDC_Read()),
*    so care should be taken if intermixing calls to USBH_CDC_ReadAsync() and USBH_CDC_Read().
*/
USBH_STATUS USBH_CDC_ReadAsync(USBH_CDC_HANDLE hDevice, void * pBuffer, U32 BufferSize, USBH_CDC_ON_COMPLETE_FUNC * pfOnComplete, USBH_CDC_RW_CONTEXT * pRWContext) {
  USBH_CDC_INST  * pInst;
  USBH_STATUS      Status;
  CDC_EP_DATA    * pEPData;
  USBH_URB       * pUrb;

  if (pfOnComplete == NULL || pRWContext == NULL) {
    USBH_WARN((USBH_MCAT_CDC, "USBH_CDC_ReadAsync called with invalid parameters, pfOnComplete = 0x%x, pRWContext = 0x%x", pfOnComplete, pRWContext));
    return USBH_STATUS_INVALID_PARAM;
  }
  pEPData = NULL;
  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    if (pInst->IsOpened == 0) {
      return USBH_STATUS_NOT_OPENED;
    }
    pEPData = &pInst->BulkIn;
    if (BufferSize == 0u || BufferSize % pEPData->MaxPacketSize != 0u) {
      USBH_WARN((USBH_MCAT_CDC, "BufferSize (%d) is not a multiple of MaxPacketSize(%d).", BufferSize, pEPData->MaxPacketSize));
      return USBH_STATUS_INVALID_PARAM;
    }
    if (BufferSize > pInst->MaxInTransferSize) {
      USBH_WARN((USBH_MCAT_CDC, "USBH_CDC_ReadAsync BufferSize (%d) too large, max possible is %d", BufferSize, pInst->MaxInTransferSize));
      return USBH_STATUS_XFER_SIZE;
    }
    if (pEPData->InUse != FALSE) {
      return USBH_STATUS_BUSY;
    }
    pEPData->InUse = TRUE;
    pUrb = &pEPData->Urb;
    USBH_MEMSET(pUrb, 0, sizeof(USBH_URB));
    pRWContext->pUserBuffer = pBuffer;
    pRWContext->UserBufferSize = BufferSize;
    pUrb->Header.Function                 = USBH_FUNCTION_BULK_REQUEST;
    pUrb->Request.BulkIntRequest.Endpoint = pEPData->EPAddr;
    pUrb->Request.BulkIntRequest.pBuffer  = pBuffer;
    pUrb->Request.BulkIntRequest.Length   = BufferSize;
    pUrb->Header.pfOnCompletion           = _OnAsyncCompletion;
    pUrb->Header.pContext                 = pInst;
    pUrb->Header.pfOnUserCompletion       = (USBH_ON_COMPLETION_USER_FUNC *)pfOnComplete;   //lint !e9074 !e9087  D:104
    pUrb->Header.pUserContext             = pRWContext;
    //
    // Send the URB
    // In case of an error the function will also return
    //
    Status = INC_REF_CNT(pInst);
    if (Status == USBH_STATUS_SUCCESS) {
      Status = USBH_SubmitUrb(pInst->hDATAInterface, pUrb);
      if (Status != USBH_STATUS_PENDING) {
        (void)DEC_REF_CNT(pInst);
      }
    }
    if (Status != USBH_STATUS_PENDING) {
      pEPData->InUse = FALSE;
      pRWContext->Status = Status;
    }
  } else {
    Status = USBH_STATUS_INVALID_HANDLE;
  }
  return Status;
}

/*********************************************************************
*
*       USBH_CDC_WriteAsync
*
*  Function description
*    Triggers a write transfer to the CDC device. The result of
*    the transfer is received through the user callback.
*    This function will return immediately while the write transfer is
*    done asynchronously.
*
*  Parameters
*    hDevice      : Handle to an open device returned by USBH_CDC_Open().
*    pBuffer      : Pointer to a buffer which holds the data.
*    BufferSize   : Number of bytes to write.
*                   Use USBH_CDC_GetMaxTransferSize() to get the maximum allowed size.
*    pfOnComplete : Pointer to a user function of type USBH_CDC_ON_COMPLETE_FUNC
*                   which will be called after the transfer has been completed.
*    pRWContext   : Pointer to a USBH_CDC_RW_CONTEXT structure which
*                   will be filled with data after the transfer has
*                   been completed and passed as a parameter to
*                   the pfOnComplete function.
*                   pfOnComplete function. The member 'pUserContext' may be set before
*                   calling USBH_CDC_WriteAsync(). Other members need not be initialized and
*                   are set by the function USBH_CDC_WriteAsync().
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
USBH_STATUS USBH_CDC_WriteAsync(USBH_CDC_HANDLE hDevice, void * pBuffer, U32 BufferSize, USBH_CDC_ON_COMPLETE_FUNC * pfOnComplete, USBH_CDC_RW_CONTEXT * pRWContext) {
  USBH_CDC_INST  * pInst;
  USBH_STATUS      Status;
  CDC_EP_DATA    * pEPData;
  USBH_URB       * pUrb;

  if (pfOnComplete == NULL || pRWContext == NULL) {
    USBH_WARN((USBH_MCAT_CDC, "USBH_CDC_WriteAsync called with invalid parameters, pfOnComplete = 0x%x, pRWContext = 0x%x", pfOnComplete, pRWContext));
    return USBH_STATUS_INVALID_PARAM;
  }
  pEPData = NULL;
  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    if (pInst->IsOpened == 0) {
      return USBH_STATUS_NOT_OPENED;
    }
    if (BufferSize > pInst->MaxOutTransferSize) {
      USBH_WARN((USBH_MCAT_CDC, "USBH_CDC_WriteAsync BufferSize (%d) too large, max possible is %d", BufferSize, pInst->MaxOutTransferSize));
      return USBH_STATUS_XFER_SIZE;
    }
    pEPData = &pInst->BulkOut;
    if (pEPData->InUse != FALSE) {
      return USBH_STATUS_BUSY;
    }
    pEPData->InUse = TRUE;
    pUrb = &pEPData->Urb;
    USBH_MEMSET(pUrb, 0, sizeof(USBH_URB));
    pRWContext->pUserBuffer    = pBuffer;
    pRWContext->UserBufferSize = BufferSize;
    pUrb->Header.Function                 = USBH_FUNCTION_BULK_REQUEST;
    pUrb->Request.BulkIntRequest.Endpoint = pEPData->EPAddr;
    pUrb->Request.BulkIntRequest.pBuffer  = pBuffer;
    pUrb->Request.BulkIntRequest.Length   = BufferSize;
    pUrb->Header.pfOnCompletion           = _OnAsyncCompletion;
    pUrb->Header.pContext                 = pInst;
    pUrb->Header.pfOnUserCompletion       = (USBH_ON_COMPLETION_USER_FUNC *)pfOnComplete;     //lint !e9074 !e9087  D:104
    pUrb->Header.pUserContext             = pRWContext;
    //
    // Send the URB
    // In case of an error the function will also return
    //
    Status = INC_REF_CNT(pInst);
    if (Status == USBH_STATUS_SUCCESS) {
      Status = USBH_SubmitUrb(pInst->hDATAInterface, pUrb);
      if (Status != USBH_STATUS_PENDING) {
        (void)DEC_REF_CNT(pInst);
      }
    }
    if (Status != USBH_STATUS_PENDING) {
      pEPData->InUse = FALSE;
      pRWContext->Status = Status;
    }
  } else {
    Status = USBH_STATUS_INVALID_HANDLE;
  }
  return Status;

}

/*********************************************************************
*
*       USBH_CDC_SetConfigFlags
*
*  Function description
*    Sets configuration flags for the CDC module.
*
*  Parameters
*    Flags     : A bitwise OR-combination of flags that shall be
*                set for each device. At the moment the following
*                are available:
*                * USBH_CDC_IGNORE_INT_EP:
*                  This flag prevents the interrupt endpoint of
*                  the CDC interface from being polled by the CDC
*                  module. The interrupt endpoint is normally used
*                  in the CDC protocol to communicate the changes
*                  of serial states, using this flag essentially
*                  prevents the callbacks set via USBH_CDC_SetOnIntStateChange()
*                  and USBH_CDC_SetOnSerialStateChange() from ever
*                  executing.
*                * USBH_CDC_DISABLE_INTERFACE_CHECK:
*                  According to the CDC specification CDC devices
*                  must contain two interfaces, the first being the
*                  control interface, containing an interrupt
*                  IN endpoint, the second being a data interface
*                  containing a bulk IN and a bulk OUT endpoint.
*                  Some manufacturers sometimes decide to put all
*                  3 endpoints into one interface, despite the device
*                  otherwise being compatible to the CDC
*                  specification. This flag allows such devices
*                  to be added to the CDC module.
*/
void USBH_CDC_SetConfigFlags(U32 Flags) {
  USBH_CDC_Global.DefaultFlags = Flags;
}

/*********************************************************************
*
*       USBH_CDC_GetSerialNumber
*
*  Function description
*    Get the serial number of a CDC device.
*    The serial number is in UNICODE format, not zero terminated.
*
*  Parameters
*    hDevice            : Handle to an open device returned by USBH_CDC_Open().
*    BuffSize           : Pointer to a buffer which holds the data.
*    pSerialNumber      : Size of the buffer in bytes.
*    pSerialNumberSize  : Pointer to a user function which will be called.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_CDC_GetSerialNumber(USBH_CDC_HANDLE hDevice, U32 BuffSize, U8 *pSerialNumber, U32 *pSerialNumberSize) {
  USBH_CDC_INST * pInst;
  USBH_STATUS     Status;

  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    Status = USBH_GetInterfaceSerial(pInst->DATAInterfaceID, BuffSize, pSerialNumber, pSerialNumberSize);
  } else {
    Status = USBH_STATUS_INVALID_HANDLE;
  }
  return Status;
}

/*********************************************************************
*
*       USBH_CDC_SendEncapsulatedCommand
*
*  Function description
*    Sends data via the control endpoint.
*
*  Parameters
*    hDevice      : Handle to an open device returned by USBH_CDC_Open().
*    pBuffer      : Pointer to a buffer which holds the data.
*    pNumBytes    : Pointer to the amount of bytes to send.
*                   If successful will contain the amount of bytes read.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*
*  Additional information
*    This function requires a cache-line aligned buffer (if the MCU uses cache).
*/
USBH_STATUS USBH_CDC_SendEncapsulatedCommand(USBH_CDC_HANDLE hDevice, U8 * pBuffer, U32 * pNumBytes) {
  USBH_CDC_INST * pInst;
  USBH_STATUS     Status;

  Status = USBH_STATUS_INVALID_HANDLE;
  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    Status = INC_REF_CNT(pInst);
    if (Status == USBH_STATUS_SUCCESS) {
      Status = _SendControlRequest(pInst, USB_REQTYPE_CLASS | USB_INTERFACE_RECIPIENT, USBH_CDC_REQ_SEND_ENCAPSULATED_COMMAND, 0, pInst->ACMInterfaceNo, *pNumBytes, pBuffer, pNumBytes);
      if (DEC_REF_CNT(pInst) != 0) {
        Status = USBH_STATUS_DEVICE_REMOVED;
      }
    }
  }
  return Status;
}

/*********************************************************************
*
*       USBH_CDC_GetEncapsulatedResponse
*
*  Function description
*    Receives data via the control endpoint.
*
*  Parameters
*    hDevice      : Handle to an open device returned by USBH_CDC_Open().
*    pBuffer      : Pointer to a buffer which holds the data.
*    pNumBytes    : Pointer to the amount of bytes to read.
*                   If successful will contain the amount of bytes read.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*
*  Additional information
*    This function requires a cache-line aligned buffer (if the MCU uses cache).
*/
USBH_STATUS USBH_CDC_GetEncapsulatedResponse(USBH_CDC_HANDLE hDevice, U8 * pBuffer, U32 * pNumBytes) {
  USBH_CDC_INST * pInst;
  USBH_STATUS     Status;

  Status = USBH_STATUS_INVALID_HANDLE;
  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    Status = INC_REF_CNT(pInst);
    if (Status == USBH_STATUS_SUCCESS) {
      Status = _SendControlRequest(pInst, 0xA1, USBH_CDC_REQ_GET_ENCAPSULATED_RESPONSE, 0, pInst->ACMInterfaceNo, *pNumBytes, pBuffer, pNumBytes);
      if (DEC_REF_CNT(pInst) != 0) {
        Status = USBH_STATUS_DEVICE_REMOVED;
      }
    }
  }
  return Status;
}

/*********************************************************************
*
*       USBH_CDC_SetOnSerialStateChange
*
*  Function description
*    Sets a callback which informs the user about serial state changes.
*
*  Parameters
*    hDevice                : Handle to an open device returned by USBH_CDC_Open().
*    pfOnSerialStateChange  : Pointer to the user callback.
*                             Can be NULL (to remove the callback).
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*
*  Additional information
*    The callback is called in the context of the ISR task.
*    The callback should not block.
*/
USBH_STATUS USBH_CDC_SetOnSerialStateChange(USBH_CDC_HANDLE hDevice,  USBH_CDC_SERIAL_STATE_CALLBACK * pfOnSerialStateChange) {
  USBH_CDC_INST * pInst;
  USBH_STATUS     Status;

  Status = USBH_STATUS_INVALID_HANDLE;
  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    pInst->pfOnSerialStateChange = pfOnSerialStateChange;
  }
  return Status;
}

/*********************************************************************
*
*       USBH_CDC_CancelRead
*
*  Function description
*    Cancels a running read transfer.
*
*  Parameters
*    hDevice    :  Handle to an open device returned by USBH_CDC_Open().
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*
*  Additional information
*    This function can be used to cancel a transfer which was initiated
*    by USBH_CDC_ReadAsync or USBH_CDC_Read. In the later case this
*    function has to be called from a different task.
*/
USBH_STATUS USBH_CDC_CancelRead(USBH_CDC_HANDLE hDevice) {
  USBH_CDC_INST       * pInst;
  USBH_STATUS           Status;

  Status = USBH_STATUS_INVALID_HANDLE;
  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    Status = _AbortEP(&pInst->BulkIn);
    if (Status == USBH_STATUS_PENDING) {
      Status = USBH_STATUS_SUCCESS;
    }
  }
  return Status;
}

/*********************************************************************
*
*       USBH_CDC_CancelWrite
*
*  Function description
*    Cancels a running write transfer.
*
*  Parameters
*    hDevice    :  Handle to an open device returned by USBH_CDC_Open().
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*
*  Additional information
*    This function can be used to cancel a transfer which was initiated
*    by USBH_CDC_WriteAsync or USBH_CDC_Write. In the later case this
*    function has to be called from a different task.
*/
USBH_STATUS USBH_CDC_CancelWrite(USBH_CDC_HANDLE hDevice) {
  USBH_CDC_INST       * pInst;
  USBH_STATUS           Status;

  Status = USBH_STATUS_INVALID_HANDLE;
  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    Status = _AbortEP(&pInst->BulkOut);
    if (Status == USBH_STATUS_PENDING) {
      Status = USBH_STATUS_SUCCESS;
    }
  }
  return Status;
}

/*********************************************************************
*
*       USBH_CDC_SetupRequest
*
*  Function description
*    Sends a specific request (class vendor etc) to the device.
*
*  Parameters
*    hDevice    :  Handle to an open device returned by USBH_CDC_Open().
*    RequestType   : IN/OUT direction.
*    Request       : Request code in the setup request.
*    wValue        : wValue in the setup request.
*    wIndex        : wIndex in the setup request.
*    pData         : Additional data for the setup request.
*    pNumBytesData : Number of data to be received/sent in pData.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_CDC_SetupRequest(USBH_CDC_HANDLE hDevice, U8 RequestType, U8 Request, U16 wValue, U16 wIndex, void * pData, U32 * pNumBytesData) {
  USBH_CDC_INST * pInst;
  U16             wLength;

  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    wLength = 0;
    if (pNumBytesData != NULL) {
      wLength = *pNumBytesData;
    }
    return _SendControlRequest(pInst, RequestType, Request, wValue,  wIndex, wLength, pData, pNumBytesData);
  }
  return USBH_STATUS_INVALID_HANDLE;
}

/*********************************************************************
*
*       USBH_CDC_GetCSDesc
*
*  Function description
*    Retrieves a specific CDC specific descriptor from the control interface.
*
*  Parameters
*    hDevice      : Handle to an open device returned by USBH_CDC_Open().
*    DescType     : CDC descriptor type to retrieve from the interface descriptor.
*                     Currently the following are available:
*                     * CDC_CS_INTERFACE_DESCRIPTOR_TYPE
*                     * CDC_CS_ENDPOINT_DESCRIPTOR_TYPE.
*    DescSubType  : Specifies the sub descriptor type to look for in the interface descriptor:
*                     * USBH_CDC_DESC_SUBTYPE_HEADER
*                     * USBH_CDC_DESC_SUBTYPE_CALL_MANAGEMENT
*                     * USBH_CDC_DESC_SUBTYPE_ACM
*                     * USBH_CDC_DESC_SUBTYPE_UNION_FUCTIONAL.
*    pData        : Pointer to a buffer where the descriptor will be stored.
*    pNumBytesData: Size of the buffer. Upon successful completion this variable will
*                   contain the number of bytes copied, which is either the size
*                   of the descriptor or the size of the buffer if the descriptor
*                   was longer than the given buffer.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_CDC_GetCSDesc(USBH_CDC_HANDLE hDevice, U8 DescType, U8 DescSubType, void * pData, U32 * pNumBytesData) {
  USBH_CDC_INST * pInst;
  unsigned        NumBytes2Copy;
  const U8 * pDesc;
  USBH_STATUS Status;

  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    Status = USBH_GetInterfaceDescriptorPtr(pInst->hControlInterface, 0, &pDesc, &NumBytes2Copy);
    if (Status != USBH_STATUS_SUCCESS) {
      return Status;
    }
    pDesc = _GetCSDesc(pDesc, NumBytes2Copy, DescType, DescSubType);
    NumBytes2Copy = pDesc[USB_DESC_LENGTH_INDEX];
    NumBytes2Copy = USBH_MIN(NumBytes2Copy, *pNumBytesData);
    USBH_MEMCPY(pData, pDesc, NumBytes2Copy);
    *pNumBytesData = NumBytes2Copy;
    return USBH_STATUS_SUCCESS;
  }
  return USBH_STATUS_INVALID_HANDLE;
}

/*********************************************************************
*
*       USBH_CDC_GetStringDesc
*
*  Function description
*    Retrieves a specific string descriptor from the control interface.
*
*  Parameters
*    hDevice      : Handle to an open device returned by USBH_CDC_Open().
*    StringIndex  : Index of the string descriptor.
*    pBuffer      : Pointer to a buffer where the string descriptor will be saved.
*    pNumBytesData: Size of the buffer. Upon successful completion this variable will
*                   contain the number of bytes copied, which is either the size
*                   of the descriptor or the size of the buffer if the descriptor
*                   was longer than the given buffer.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_CDC_GetStringDesc(USBH_CDC_HANDLE hDevice, U8 StringIndex, U8 * pBuffer, U32 * pNumBytesData) {
  USBH_CDC_INST * pInst;
  U8 acBuffer[255];
  unsigned NumBytes;
  USBH_STATUS Status;

  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    NumBytes = sizeof(acBuffer);
    Status = USBH_GetStringDescriptor(pInst->hControlInterface, StringIndex, 0, acBuffer, &NumBytes);
    if (Status == USBH_STATUS_SUCCESS) {
      NumBytes = acBuffer[USB_DESC_LENGTH_INDEX];
      if (NumBytes < 2u) {
        return USBH_STATUS_INVALID_DESCRIPTOR;
      }
      NumBytes = USBH_MIN(NumBytes - 2u, *pNumBytesData);
      USBH_MEMCPY(pBuffer, &acBuffer[2], NumBytes);
      *pNumBytesData = NumBytes;
    }
    return Status;
  }
  return USBH_STATUS_INVALID_HANDLE;
}

/*********************************************************************
*
*       USBH_CDC_SetDataCommunication
*
*  Function description
*    Changes the alternative interface to either the interface which
*    enables data communication or to the one which disabled it.
*
*  Parameters
*    hDevice: Handle to an open device returned by USBH_CDC_Open().
*    OnOff  : 1 - Set the interface to data-communication, Endpoints are enabled.
*             0 - Disable the data-communication, end-point are disabled.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_CDC_SetDataCommunication(USBH_CDC_HANDLE hDevice, unsigned OnOff) {
  USBH_CDC_INST       * pInst;
  USBH_STATUS           Status;
  unsigned              AltIntf2Set;
  unsigned              CurrentAltInt;
  USBH_URB            * pUrb;
  CDC_EP_DATA         * pEPData;

  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    Status = USBH_GetInterfaceCurrAltSetting(pInst->hDATAInterface, &CurrentAltInt);
    if (Status == USBH_STATUS_SUCCESS) {
      if (OnOff != 0u) {
        AltIntf2Set = pInst->EnableDataAltSet;
      } else {
        AltIntf2Set = pInst->DisableDataAltSet;
      }
      if (CurrentAltInt != AltIntf2Set) {
        pEPData = &pInst->Control;
        pUrb    = &pEPData->Urb;
        USBH_MEMSET(pUrb, 0, sizeof(*pUrb));
        pUrb->Header.Function = USBH_FUNCTION_SET_INTERFACE;
        pUrb->Request.SetInterface.AlternateSetting = AltIntf2Set;
        Status = INC_REF_CNT(pInst);
        if (Status == USBH_STATUS_SUCCESS) {
          Status = _SubmitUrbAndWait(pInst, pInst->hDATAInterface, pEPData, USBH_CDC_EP0_TIMEOUT);
          if (DEC_REF_CNT(pInst) != 0) {
            Status = USBH_STATUS_DEVICE_REMOVED;
          }
        }
        if (Status == USBH_STATUS_SUCCESS) {
          //
          // MaxTransferSizes are not set, get the value now
          //
          if (AltIntf2Set == pInst->EnableDataAltSet && pInst->MaxInTransferSize == 0u) {
            USBH_STATUS s;
            s = USBH_GetMaxTransferSize(pInst->BulkOut.hInterface, pInst->BulkOut.EPAddr, &pInst->MaxOutTransferSize);
            if (s != USBH_STATUS_SUCCESS) {
              pInst->MaxOutTransferSize = 0;
            }
            s = USBH_GetMaxTransferSize(pInst->BulkIn.hInterface, pInst->BulkIn.EPAddr, &pInst->MaxInTransferSize);
            if (s != USBH_STATUS_SUCCESS) {
              pInst->MaxInTransferSize = 0;
            }
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
*       USBH_CDC_SetOnIntStateChange
*
*  Function description
*    Sets the callback to retrieve data that are received
*    on the interrupt endpoint.
*
*  Parameters
*    hDevice      :  Handle to an open device returned by USBH_CDC_Open().
*    pfOnIntState :  Pointer to the callback that shall retrieve the data.
*    pUserContext :  Pointer to the user context.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_CDC_SetOnIntStateChange(USBH_CDC_HANDLE hDevice, USBH_CDC_INT_STATE_CALLBACK * pfOnIntState, void * pUserContext) {
  USBH_CDC_INST * pInst;

  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    pInst->pfOnIntState           = pfOnIntState;
    pInst->pOnSerialStateUContext = pUserContext;
    return USBH_STATUS_SUCCESS;
  }
  return USBH_STATUS_INVALID_HANDLE;

}

/*********************************************************************
*
*       USBH_CDC_SuspendResume
*
*  Function description
*    Prepares a CDC device for suspend (stops the interrupt endpoint)
*    or re-starts the interrupt endpoint functionality after a resume.
*
*  Parameters
*    hDevice      : Handle to an open device returned by USBH_CDC_Open().
*    State        : 0 - Prepare for suspend. 1 -  Return from resume.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*
*  Additional information
*    The application must make sure that no transactions are running
*    when setting a device into suspend mode.
*    This function is used in combination with USBH_SetRootPortPower().
*    To suspend:
*    Call this function before USBH_SetRootPortPower(x, y, USBH_SUSPEND) with State = 0.
*    To resume:
*    Call this function after USBH_SetRootPortPower(x, y, USBH_NORMAL_POWER) with State = 1.
*/
USBH_STATUS USBH_CDC_SuspendResume(USBH_CDC_HANDLE hDevice, U8 State) {
  USBH_CDC_INST * pInst;
  USBH_STATUS     Status;

  Status = USBH_STATUS_INVALID_HANDLE;
  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    Status = INC_REF_CNT(pInst);
    if (Status == USBH_STATUS_SUCCESS) {
      if (State == 0u) {
        //
        // Stop interrupt EP transfers.
        //
        pInst->RunningState = StateSuspend;
        if ((pInst->Flags & USBH_CDC_IGNORE_INT_EP) == 0u) {
          Status = _AbortEP(&pInst->IntIn);
          if (Status != USBH_STATUS_PENDING && Status != USBH_STATUS_SUCCESS) {
            USBH_WARN((USBH_MCAT_CDC, "USBH_CDC_SuspendResume: Aborting int EP failed %s", USBH_GetStatusStr(Status)));
          }
        }
      } else {
        //
        // Restart interrupt EP transfers.
        //
        if (pInst->RunningState == StateSuspend) {
          pInst->RunningState = StateRunning;
          if ((pInst->Flags & USBH_CDC_IGNORE_INT_EP) == 0u) {
            _SubmitIntTransfer(pInst, pInst->pIntInBuffer, pInst->IntIn.MaxPacketSize);
          }
        }
      }
      (void)DEC_REF_CNT(pInst);
    }
  }
  return Status;
}

/*********************************************************************
*
*       USBH_CDC_GetInterfaceHandle
*
*  Function description
*    Return the handle to the (open) USB interface. Can be used to
*    call USBH core functions like USBH_GetStringDescriptor().
*
*  Parameters
*    hDevice      : Handle to an open device returned by USBH_CDC_Open().
*
*  Return value
*    Handle to an open interface.
*/
USBH_INTERFACE_HANDLE USBH_CDC_GetInterfaceHandle(USBH_CDC_HANDLE hDevice) {
  USBH_CDC_INST      * pInst;

  pInst = _h2p(hDevice);
  USBH_ASSERT_PTR(pInst);
  return pInst->hControlInterface;
}

/*********************************************************************
*
*       USBH_CDC_GetIndex
*
*  Function description
*    Return an index that can be used for call to USBH_CDC_Open()
*    for a given interface ID.
*
*  Parameters
*    InterfaceID:    Id of the interface.
*
*  Return value
*    >= 0: Index of the CDC interface.
*    <  0: InterfaceID not found.
*/
int USBH_CDC_GetIndex(USBH_INTERFACE_ID InterfaceID) {
  USBH_CDC_INST      * pInst;

  pInst = USBH_CDC_Global.pFirst;
  while (pInst != NULL) {
    if (pInst->ControlInterfaceID == InterfaceID || pInst->DATAInterfaceID == InterfaceID) {
      return pInst->DevIndex;
    }
    pInst = pInst->pNext;
  }
  return -1;
}

/*********************************************************************
*
*       USBH_CDC_GetMaxTransferSize
*
*  Function description
*    Return the maximum transfer sizes allowed for the USBH_CDC_*Async functions.
*
*  Parameters
*    hDevice              : Handle to an open device returned by USBH_CDC_Open().
*    pMaxOutTransferSize  : Pointer to a variable which will receive the maximum
*                           transfer size for the Bulk OUT endpoint (for USBH_CDC_ReadAsync()).
*    pMaxInTransferSize   : Pointer to a variable which will receive the maximum
*                           transfer size for the Bulk IN endpoint (for USBH_CDC_WriteAsync()).
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*
*  Additional information
*    Using this function is only necessary with the USBH_CDC_*Async functions,
*    other functions handle the limits internally.
*    These limits exist because certain USB controllers have hardware limitations.
*    Some USB controllers (OHCI, EHCI, ...) do not have these limitations, therefore 0xFFFFFFFF will be returned.
*/
USBH_STATUS USBH_CDC_GetMaxTransferSize(USBH_CDC_HANDLE hDevice, U32 * pMaxOutTransferSize, U32 * pMaxInTransferSize) {
  USBH_CDC_INST * pInst;
  USBH_STATUS     Status;

  Status = USBH_STATUS_INVALID_HANDLE;
  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    if (pMaxOutTransferSize != NULL) {
      *pMaxOutTransferSize = pInst->MaxOutTransferSize;
    }
    if (pMaxInTransferSize != NULL) {
      *pMaxInTransferSize = pInst->MaxInTransferSize;
    }
    Status = USBH_STATUS_SUCCESS;
  }
  return Status;
}

/*************************** End of file ****************************/
