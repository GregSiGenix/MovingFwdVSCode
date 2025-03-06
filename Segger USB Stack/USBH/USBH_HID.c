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
File        : USBH_HID.c
Purpose     : API of the USB host stack

This is the base module to handle HID devices.
It is responsible for
* Enumerating the device (read report descriptor, etc.)
* Provide basic information functions (GetDeviceInfo...)
* Provide functions to read and write reports (raw)
* Polling the interrupt IN EP at the given interval
* Provide new functions USBH_HID_RegisterPlugin() and USBH_HID_RegisterReportHandler()

To handle an actual HID device, a plug-in is required.
A plug-in has to register at the base module by calling USBH_HID_RegisterPlugin()
providing a callback function for device detection.
On each enumeration of a new device, the base module calls the callback functions of all registered plug-ins.
The callback of a plug-in the checks, if the device can be handled by this plug-in.
For this, it can check all descriptors.
If it can handle the device: It calls USBH_HID_RegisterReportHandler() on this device,
providing a function that is called by the base module every time, a new report was received.
-------------------------- END-OF-HEADER -----------------------------
*/
/*********************************************************************
*
*       #include section
*
**********************************************************************
*/
#include "USBH_HID_Int.h"
#include "USBH_Util.h"

/*********************************************************************
*
*       Defines
*
**********************************************************************
*/
#define USBH_HID_NUM_DEVICES              32u

#define USBH_HID_WRITE_DEFAULT_TIMEOUT   500 // 500ms shall be sufficient enough in order to send a SET_REPORT request to the device.

#define USBH_HID_REMOVAL_TIMEOUT         100

// HID descriptor types
#define USB_HID_DESCRIPTOR_TYPE                       0x21u
#define USB_HID_DESCRIPTOR_TYPE_REPORT                0x22u

#define USBH_HID_DESC_NUM_DESCS_OFFSET                  5u
#define USBH_HID_DESC_TYPE_OFFSET                       6u
#define USBH_HID_DESC_LEN_OFFSET                        7u
#define USBH_HID_DESC_SIZE                              3u

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


//
// HID report descriptor defines for the simple parser.
//
//lint -esym(750,USBH_HID_REPORT_*)  D:109
#define USBH_HID_REPORT_USAGE_PAGE        0x04u
#define USBH_HID_REPORT_LOGICAL_MIN       0x14u
#define USBH_HID_REPORT_LOGICAL_MAX       0x24u
#define USBH_HID_REPORT_PHYSICAL_MIN      0x34u
#define USBH_HID_REPORT_PHYSICAL_MAX      0x44u
#define USBH_HID_REPORT_SIZE              0x74u
#define USBH_HID_REPORT_ID                0x84u
#define USBH_HID_REPORT_COUNT             0x94u
#define USBH_HID_REPORT_INPUT             0x80u
#define USBH_HID_REPORT_OUTPUT            0x90u
#define USBH_HID_REPORT_COLLECTION        0xA0u
#define USBH_HID_REPORT_FEATURE           0xB0u
#define USBH_HID_REPORT_USAGE             0x08u
#define USBH_HID_REPORT_USAGE_MIN         0x18u
#define USBH_HID_REPORT_USAGE_MAX         0x28u
#define USBH_HID_REPORT_TYPE_MASK         0xFCu
#define USBH_HID_REPORT_LONG_ITEM         0xFEu

/*********************************************************************
*
*       Data structures
*
**********************************************************************
*/
typedef struct {
  USBH_HID_INST             * pFirst;
  USBH_NOTIFICATION_HANDLE    hDevNotification;
  USBH_HID_HANDLE             NextHandle;
  USBH_HID_ON_REPORT        * pfOnReport;
  USBH_NOTIFICATION_FUNC    * pfOnUserNotification;
  USBH_NOTIFICATION_HOOK    * pFirstNotiHook;
  void                      * pUserNotifyContext;
  U8                          NumDevices;
  U32                         ControlWriteTimeout;
  U32                         DevIndexUsedMask;
  USBH_DLIST                  PluginList;
} USBH_HID_GLOBAL;

/*********************************************************************
*
*       Static const
*
**********************************************************************
*/

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static USBH_HID_GLOBAL USBH_HID_Global;
static I8              _isInited;

/*********************************************************************
*
*       Static prototypes
*
**********************************************************************
*/
static USBH_STATUS _SubmitInBuffer(USBH_HID_INST * pInst, U8 * pBuffer, U32 NumBytes, USBH_HID_USER_FUNC * pfUser, USBH_HID_RW_CONTEXT * pRWContext);

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _FindReportInfo()
*
*  Function description
*    Find report info with given ID in pInst->ReportInfo.
*/
static USBH_HID_REPORT_INFO *_FindReportInfo(USBH_HID_INST * pInst, unsigned ID) {
  USBH_HID_REPORT_INFO *pInfo;
  unsigned              i;

  pInfo = pInst->ReportInfo;
  for (i = 0; i < pInst->NumReportInfos; i++) {
    if (pInfo->ReportId == ID) {
      return pInfo;
    }
  }
  return NULL;
}

/*********************************************************************
*
*       _SetReportInfo()
*
*  Function description
*    Set report info with given ID in pInst->ReportInfo.
*/
static void _SetReportInfo(USBH_HID_INST * pInst, const HID_FIELD_INFO * pField) {
  USBH_HID_REPORT_INFO *pInfo;

  if (pField->InRptLen != 0u || pField->OutRptLen != 0u) {
    pInfo = _FindReportInfo(pInst, pField->ReportId);
    if (pInfo == NULL) {
      if (pInst->NumReportInfos >= USBH_HID_MAX_REPORTS) {
        if (pInst->IgnoreReportParseWarning == 0) {
          USBH_WARN((USBH_MCAT_HID, "Too much report ID's, USBH_HID_MAX_REPORTS too small"));
        }
        return;
      }
      pInfo = pInst->ReportInfo + pInst->NumReportInfos;
      pInst->NumReportInfos++;
    }
    pInfo->ReportId         = pField->ReportId;
    pInfo->InputReportSize  = pField->InRptLen;
    pInfo->OutputReportSize = pField->OutRptLen;
  }
}

/*********************************************************************
*
*       _CheckSigned()
*
*  Function description
*    Check, if a bit field is signed and sign extent data.
*/
static unsigned _CheckSigned(U32 Data, unsigned NumSignificantBytes, USBH_ANY_SIGNED *pData) {
  U32 Signed;
  U32 NumSignificantBits;

  if (NumSignificantBytes == 0u) {
    pData->u32 = 0;
    return 0;
  }
  NumSignificantBits = 8u * NumSignificantBytes - 1u;
  Signed = Data >> NumSignificantBits;          // Get sign bit of 'Data'
  if (Signed != 0u) {
    Data |= (0xFFFFFFFFu << NumSignificantBits);
    pData->i32 = (I32)Data;
  } else {
    pData->u32 = Data;
  }
  return Signed;
}

/*********************************************************************
*
*       _h2p()
*/
static USBH_HID_INST * _h2p(USBH_HID_HANDLE Handle) {
  USBH_HID_INST * pInst;

  if (Handle == 0u) {
    return NULL;
  }
  //
  // Iterate over linked list to find an instance with matching handle. Return if found.
  //
  pInst = USBH_HID_Global.pFirst;
  while (pInst != NULL) {
    if (pInst->Handle == Handle) {                                        // Match ?
      return pInst;
    }
    pInst = pInst->pNext;
  }
  //
  // Error handling: Device handle not found in list.
  //
  USBH_WARN((USBH_MCAT_HID, "HID: Invalid handle %u", Handle));
  return NULL;
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
static void _RemoveInstanceFromList(const USBH_HID_INST * pInst) {
  USBH_HID_INST * pPrev;
  USBH_HID_INST * pCurrent;

  if (pInst == USBH_HID_Global.pFirst) {
    USBH_HID_Global.pFirst = USBH_HID_Global.pFirst->pNext;
  } else {
    pPrev = USBH_HID_Global.pFirst;
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
*/
static void _RemoveDevInstance(USBH_HID_INST * pInst) {
  USBH_DLIST            * pEntry;
  USBH_HID_HANDLER_HOOK * pHandler;

  if (pInst != NULL) {
    //
    // Remove plugin instances
    //
    for (;;) {
      pEntry = USBH_DLIST_GetNext(&pInst->HandlerList);
      if (pEntry == &pInst->HandlerList) {
        break;
      }
      pHandler = GET_HID_HANDLER_FROM_ENTRY(pEntry);
      USBH_ASSERT_MAGIC(pHandler, HID_HANDLER);
      USBH_DLIST_RemoveEntry(&pHandler->ListEntry);
      pHandler->pRemove(pHandler->pContext);
    }
    //
    //  Free all associated EP buffers
    //
    if (pInst->pInBuffer != NULL) {
      USBH_FREE(pInst->pInBuffer);
      pInst->pInBuffer = (U8 *)NULL;
    }
    if (pInst->pOutBuffer != NULL) {
      USBH_FREE(pInst->pOutBuffer);
      pInst->pOutBuffer = (U8 *)NULL;
    }
    //
    //  Free the report descriptor
    //
    if (pInst->pReportBufferDesc != NULL) {
      USBH_FREE(pInst->pReportBufferDesc);
      pInst->pReportBufferDesc = NULL;
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
}

/*********************************************************************
*
*       _IncRefCnt
*
*  Function description
*    Increments the reference counter of the device instance.
*
*  Parameters
*    pInst     : Pointer to the HID instance.
*    s         : For debugging only.
*    d         : For debugging only.
*/
static USBH_STATUS _IncRefCnt(USBH_HID_INST * pInst
#if USBH_REF_TRACE
                              , const char * s, int d
#endif
                             ) {
  USBH_STATUS Ret;

  Ret = USBH_STATUS_SUCCESS;
  USBH_OS_Lock(USBH_MUTEX_HID);
  if (pInst->RefCnt == 0u) {
    Ret = USBH_STATUS_DEVICE_REMOVED;
  } else {
    pInst->RefCnt++;
  }
  USBH_OS_Unlock(USBH_MUTEX_HID);
#if USBH_REF_TRACE
  USBH_LOG((USBH_MCAT_HID, "_IncRefCnt: [iface%d] %d %s@%d", pInst->Handle, pInst->RefCnt, s, d));
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
*    pInst     : Pointer to the HID instance.
*    s         : For debugging only.
*    d         : For debugging only.
*/
static void _DecRefCnt(USBH_HID_INST * pInst
#if USBH_REF_TRACE
                      , const char * s, int d
#endif
                     ) {
  int RefCount;

  USBH_OS_Lock(USBH_MUTEX_HID);
  RefCount = (int)pInst->RefCnt - 1;
  if (RefCount >= 0) {
    pInst->RefCnt = (unsigned)RefCount;
  }
  USBH_OS_Unlock(USBH_MUTEX_HID);
  if (RefCount < 0) {
#if USBH_REF_TRACE
    USBH_WARN((USBH_MCAT_HID, "Invalid RefCnt found: [iface%d] %d %s@%d", pInst->Handle, RefCount, s, d));
#else
    USBH_WARN((USBH_MCAT_HID, "Invalid RefCnt found: [iface%d] %d", pInst->Handle, RefCount));
#endif
  }
#if USBH_REF_TRACE
  USBH_LOG((USBH_MCAT_HID, "_DecRefCnt: [iface%d] %d %s@%d", pInst->Handle, RefCount, s, d));
#endif
}

/*********************************************************************
*
*       _EPIncRefCnt
*/
static void _EPIncRefCnt(HID_EP_DATA * pEPData
#if USBH_REF_TRACE
  , const char * s, int d
#endif
) {
  USBH_OS_Lock(USBH_MUTEX_HID);
  if (pEPData->RefCount != 0u) {
    pEPData->RefCount++;
  }
  USBH_OS_Unlock(USBH_MUTEX_HID);
#if USBH_REF_TRACE
  USBH_LOG((USBH_MCAT_HID, "_EPIncRefCnt: [EP0x%x] %d %s@%d", pEPData->EPAddr, pEPData->RefCount, s, d));
#endif
}

/*********************************************************************
*
*       _EPDecRefCnt
*/
static void _EPDecRefCnt(HID_EP_DATA * pEPData
#if USBH_REF_TRACE
  , const char * s, int d
#endif
) {
  int RefCount;

  USBH_OS_Lock(USBH_MUTEX_HID);
  RefCount = (int)pEPData->RefCount - 1;
  if (RefCount >= 0) {
    pEPData->RefCount = (unsigned)RefCount;
  }
  USBH_OS_Unlock(USBH_MUTEX_HID);
  if (RefCount < 0) {
#if USBH_REF_TRACE
    USBH_WARN((USBH_MCAT_HID, "_EPDecRefCnt: Invalid RefCnt found: [EP0x%x] %d %s@%d", pEPData->EPAddr, pEPData->RefCount, s, d));
#else
    USBH_WARN((USBH_MCAT_HID, "_EPDecRefCnt: Invalid RefCnt found: [EP0x%x] %d", pEPData->EPAddr, pEPData->RefCount));
#endif
  }
#if USBH_REF_TRACE
  USBH_LOG((USBH_MCAT_HID, "_EPDecRefCnt: [EP0x%x] %d %s@%d", pEPData->EPAddr, pEPData->RefCount, s, d));
#endif
}

/*********************************************************************
*
*       _RemoveAllInstances
*/
static void _RemoveAllInstances(void) {
  USBH_HID_INST * pInst;

  pInst = USBH_HID_Global.pFirst;
  while (pInst != NULL) {   // Iterate over all instances
    DEC_REF_CNT(pInst);     // CreateDevInstance()
    pInst = pInst->pNext;
  }
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
*     A device index or USBH_HID_NUM_DEVICES in case all device indexes are allocated.
*/
static U8 _AllocateDevIndex(void) {
  U8 i;
  U32 Mask;

  Mask = 1;
  for (i = 0; i < USBH_HID_NUM_DEVICES; ++i) {
    if ((USBH_HID_Global.DevIndexUsedMask & Mask) == 0u) {
      USBH_HID_Global.DevIndexUsedMask |= Mask;
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
*     DevIndex     - Device Index that should be freed.
*/
static void _FreeDevIndex(U8 DevIndex) {
  U32 Mask;

  if (DevIndex < USBH_HID_NUM_DEVICES) {
    Mask = (1uL << DevIndex);
    USBH_HID_Global.DevIndexUsedMask &= ~Mask;
  }
}

/*********************************************************************
*
*       _AbortEP
*
*  Function description
*    Abort any URB transaction on the specified EP.
*
*  Parameters
*    pEPData     : Pointer to the HID_EP_DATA structure.
*
*  Return value
*    == USBH_STATUS_SUCCESS : Success
*    != USBH_STATUS_SUCCESS : An error occurred.
*/
static USBH_STATUS _AbortEP(HID_EP_DATA * pEPData) {
  USBH_STATUS   Status;
  USBH_URB    * pAbortUrb = &pEPData->AbortUrb;
  USBH_URB    * pUrb = &pEPData->Urb;

  USBH_LOG((USBH_MCAT_HID_URB, "_AbortEP: Aborting an URB!"));
  USBH_ZERO_MEMORY(pAbortUrb, sizeof(USBH_URB));
  switch (pUrb->Header.Function) {
  case USBH_FUNCTION_BULK_REQUEST:
  case USBH_FUNCTION_INT_REQUEST:
    pAbortUrb->Request.EndpointRequest.Endpoint = pUrb->Request.BulkIntRequest.Endpoint;
    break;
  case USBH_FUNCTION_CONTROL_REQUEST:
    //pAbortUrb->Request.EndpointRequest.Endpoint = 0;
    break;
  default:
    USBH_WARN((USBH_MCAT_HID_URB, "_AbortEP: invalid URB function: %d", pUrb->Header.Function));
    break;
  }
  USBH_LOG((USBH_MCAT_HID_URB, "_AbortEP: Abort Ep: 0x%x", pUrb->Request.EndpointRequest.Endpoint));
  pAbortUrb->Header.Function = USBH_FUNCTION_ABORT_ENDPOINT;
  Status = USBH_SubmitUrb(pEPData->hInterface, pAbortUrb);
  return Status;
}

/*********************************************************************
*
*       _CancelIO
*/
static void _CancelIO(USBH_HID_INST * pInst) {
  if (pInst->IntIn.InUse != 0) {
    (void)_AbortEP(&pInst->IntIn);
  }
  if (pInst->IntOut.InUse != 0) {
    (void)_AbortEP(&pInst->IntOut);
  }
  if (pInst->Control.InUse != 0) {
    (void)_AbortEP(&pInst->Control);
  }
}

/*********************************************************************
*
*       _RemovalTimer
*/
static void _RemovalTimer(void * pContext) {
  USBH_HID_INST * pInst;
  HID_EP_DATA   * apEPData[3];
  unsigned        i;

  pInst = USBH_CTX2PTR(USBH_HID_INST, pContext);
  if ((pInst->IsOpened == 0) && (pInst->RefCnt == 0u)) {
    apEPData[0] = &pInst->Control;
    apEPData[1] = &pInst->IntIn;
    apEPData[2] = &pInst->IntOut;
    if (pInst->RunningState == StateStop || pInst->RunningState == StateError) {
      for (i = 0; i < SEGGER_COUNTOF(apEPData); i++) {
        //
        // It is possible for a device to be removed before endpoints were
        // allocated, we have to check whether the endpoint has
        // the initial ref count in this case.
        //
        if ((apEPData[i]->RefCount != 0u) && (apEPData[i]->AbortFlag == 0u)) {
          EP_DEC_REF_CNT(apEPData[i]);
        }
        //
        // If the reference count is still not zero - we have to abort the EP.
        //
        if ((apEPData[i]->RefCount != 0u) && (apEPData[i]->AbortFlag == 0u)) {
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
          USBH_StartTimer(&pInst->RemovalTimer, USBH_HID_REMOVAL_TIMEOUT);
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
      // We do not close interfaces until all EP ref counts are zero, that is checked in the loops above.
      //
      if (pInst->hInterface != NULL) {
        USBH_CloseInterface(pInst->hInterface);
        pInst->hInterface = NULL;
      }
      _FreeDevIndex(pInst->DevIndex);
      USBH_ReleaseTimer(&pInst->RemovalTimer);
      USBH_HID_Global.NumDevices--;
      _RemoveDevInstance(pInst);
    } else {
      USBH_WARN((USBH_MCAT_HID, "Removing an instance where state is not error or stop!"));
    }
  } else {
    USBH_StartTimer(&pInst->RemovalTimer, USBH_HID_REMOVAL_TIMEOUT);
  }
}

/*********************************************************************
*
*       _CreateDevInstance
*/
static USBH_HID_INST * _CreateDevInstance(void) {
  USBH_HID_INST * pInst;

  //
  // Check if max. number of sockets allowed is exceeded
  //
  if (USBH_HID_Global.NumDevices >= USBH_HID_NUM_DEVICES) {
    USBH_WARN((USBH_MCAT_HID, "No instance available for creating a new HID device! (Increase USBH_HID_NUM_DEVICES)"));
    return NULL;
  }
  //
  // Perform the actual allocation
  //
  pInst = (USBH_HID_INST *)USBH_TRY_MALLOC_ZEROED(sizeof(USBH_HID_INST));
  if (pInst != NULL) {
    pInst->Handle         = ++USBH_HID_Global.NextHandle;
    pInst->hInterface     = NULL;
    pInst->RefCnt         = 1; // Initial reference counter.
    pInst->ReadErrorCount = 0;
    pInst->InterfaceID    = 0;
    pInst->DevIndex       = _AllocateDevIndex();
    pInst->Control.RefCount = 1; // Initial reference counter.
    pInst->IntIn.RefCount   = 1; // Initial reference counter.
    pInst->IntOut.RefCount  = 1; // Initial reference counter.
                                 // The OUT endpoint is not always present.
                                 // In that case the ref count is removed by _StartDevice
    USBH_DLIST_Init(&pInst->HandlerList);
    USBH_InitTimer(&pInst->RemovalTimer, _RemovalTimer, pInst);
    USBH_StartTimer(&pInst->RemovalTimer, USBH_HID_REMOVAL_TIMEOUT);
    pInst->pNext = USBH_HID_Global.pFirst;
    USBH_HID_Global.pFirst = pInst;
    USBH_HID_Global.NumDevices++;
  }
  return pInst;
}

/*********************************************************************
*
*       _OnSubmitUrbCompletion
*/
static void _OnSubmitUrbCompletion(USBH_URB * pUrb) USBH_CALLBACK_USE {
  HID_EP_DATA  * pEPData;

  pEPData = USBH_CTX2PTR(HID_EP_DATA, pUrb->Header.pContext);
  if (pEPData->RefCount == 0u) {
    USBH_LOG((USBH_MCAT_HID_URB, "_OnSubmitUrbCompletion EP RefCount zero!"));
    return;
  }
  USBH_LOG((USBH_MCAT_HID_URB, "_OnSubmitUrbCompletion URB st: %s", USBH_GetStatusStr(pUrb->Header.Status)));
  EP_DEC_REF_CNT(pEPData);
  USBH_OS_SetEvent(pEPData->pEvent);
}

/*********************************************************************
*
*       _GetEPData
*/
static HID_EP_DATA * _GetEPData(USBH_HID_INST * pInst, U8 EPAddr) {
  if (pInst->IntIn.EPAddr == EPAddr) {
    return &pInst->IntIn;
  }
  USBH_ASSERT(pInst->IntOut.EPAddr == EPAddr);
  return &pInst->IntOut;
}

/*********************************************************************
*
*       _OnOutCompletion
*
*  Function description
*    Is called when an URB is completed.
*/
static void _OnOutCompletion(USBH_URB * pUrb) USBH_CALLBACK_USE {
  USBH_HID_INST * pInst;
  HID_EP_DATA * pEPData;

  USBH_ASSERT(pUrb != NULL);
  pInst = USBH_CTX2PTR(USBH_HID_INST, pUrb->Header.pContext);
  if (pUrb->Header.Function == USBH_FUNCTION_CONTROL_REQUEST) {
    pEPData =  &pInst->Control;
  } else {
    pEPData = _GetEPData(pInst, pUrb->Request.BulkIntRequest.Endpoint);
  }
  pEPData->InUse = 0;
  EP_DEC_REF_CNT(pEPData);
  if (pEPData->pEvent != NULL) {
    USBH_OS_SetEvent(pEPData->pEvent);
  }
  if (pUrb->Header.pfOnUserCompletion != NULL) {
    (pUrb->Header.pfOnUserCompletion)(pUrb->Header.pUserContext);
  }
  if (pUrb->Header.Status != USBH_STATUS_SUCCESS) {
    USBH_WARN((USBH_MCAT_HID_URB, "_OnOutCompletion: %s", USBH_GetStatusStr(pUrb->Header.Status)));
  }
  DEC_REF_CNT(pInst);
}

/*********************************************************************
*
*       _OnAsyncCompletion
*
*  Function description
*    HID internal completion routine for the USBH_HID_SetReport/USBH_HID_GetReport
*    when used with the user completion function.
*    Calls the user callback.
*
*  Parameters
*    pUrb     : Pointer to the completed URB.
*/
static void _OnAsyncCompletion(USBH_URB * pUrb) {
  USBH_HID_INST                * pInst;
  USBH_ON_COMPLETION_USER_FUNC * pfUser;
  USBH_HID_RW_CONTEXT          * pRWContext;
  HID_EP_DATA                  * pEPData;
  U8                             EPAddr;
  USBH_BULK_INT_REQUEST        * pBulkRequest;

  //
  // Get all necessary pointers
  //
  pInst         = USBH_CTX2PTR(USBH_HID_INST, pUrb->Header.pContext);
  pfUser        = pUrb->Header.pfOnUserCompletion;
  pRWContext    = USBH_CTX2PTR(USBH_HID_RW_CONTEXT, pUrb->Header.pUserContext);
  //
  //  Update URB Status in RWContext
  //
  pRWContext->Status = pUrb->Header.Status;
  if (pUrb->Header.Function == USBH_FUNCTION_INT_REQUEST) {
    pBulkRequest  = &pUrb->Request.BulkIntRequest;
    EPAddr        = pBulkRequest->Endpoint;
    pEPData       = _GetEPData(pInst, EPAddr);
    //
    // Read operation (interrupt IN endpoint).
    //
    if ((EPAddr & 0x80u) != 0u) {
      if (pRWContext->pUserBuffer != NULL) {
        USBH_MEMCPY(pRWContext->pUserBuffer, (const void *)pBulkRequest->pBuffer, pBulkRequest->Length);
      } else {
        USBH_ASSERT0; // User buffer must be set.
      }
    }
    pRWContext->NumBytesTransferred = pBulkRequest->Length;
  } else {
    pRWContext->NumBytesTransferred = pUrb->Request.ControlRequest.Length;
    pEPData =  &pInst->Control;
  }
  pEPData->InUse = 0;
  EP_DEC_REF_CNT(pEPData);
  DEC_REF_CNT(pInst);
  if (pfUser != NULL) {
    //
    // Call user function
    //
    pfUser(pRWContext);
  } else {
    USBH_ASSERT0; // User completion must be set.
    return;
  }
}

/*********************************************************************
*
*       _OnIntInCompletion
*
*  Function description
*    Is called when an URB is completed.
*/
static void _OnIntInCompletion(USBH_URB * pUrb) USBH_CALLBACK_USE {
  USBH_STATUS     Status;
  USBH_HID_INST * pInst;
  USBH_DLIST    * pEntry;

  USBH_LOG((USBH_MCAT_HID_URB, "[_OnIntInCompletion"));
  USBH_ASSERT(pUrb != NULL);
  pInst = USBH_CTX2PTR(USBH_HID_INST, pUrb->Header.pContext);
  //
  // Check if RefCnt is zero, this occurs when HID_Exit
  // has been called and the URB has been aborted.
  //
  if (pInst->RefCnt == 0u) {
    USBH_LOG((USBH_MCAT_HID_URB, "_OnIntInCompletion: device RefCnt is zero!"));
    goto Err1;
  }
  if (pInst->RunningState == StateStop || pInst->RunningState == StateError) {
    USBH_WARN((USBH_MCAT_HID_URB, "_OnIntInCompletion: device has an error or is stopped!"));
    goto Err;
  }
  if (pUrb->Header.Status == USBH_STATUS_SUCCESS) {
    U8             * pData;
    unsigned         NumBytesReceived;
    int              Handled = 0;

    pInst->IntErrCnt = 0;
    pData = USBH_U8PTR(pUrb->Request.BulkIntRequest.pBuffer);
    NumBytesReceived = pUrb->Request.BulkIntRequest.Length;
    pEntry = USBH_DLIST_GetNext(&pInst->HandlerList);
    while (pEntry != &pInst->HandlerList) {
      USBH_HID_HANDLER_HOOK *pHandler;
      pHandler = GET_HID_HANDLER_FROM_ENTRY(pEntry);
      USBH_ASSERT_MAGIC(pHandler, HID_HANDLER);
      Handled += pHandler->pHandler(pHandler->pContext, pData, NumBytesReceived, Handled);
      pEntry = USBH_DLIST_GetNext(pEntry);
    }
    if (USBH_HID_Global.pfOnReport != NULL) {
      USBH_HID_Global.pfOnReport(pInst->InterfaceID, pData, NumBytesReceived, Handled);
    }
    pInst->ReadErrorCount = 0; // On success clear error count
  } else {
    USBH_TIME Now;
    I32 tDiff;

    USBH_LOG((USBH_MCAT_HID_URB, "_OnIntInCompletion: Transaction failed: %s", USBH_GetStatusStr(pUrb->Header.Status)));
    Now = USBH_OS_GetTime32();
    tDiff = USBH_TimeDiff(Now, pInst->LastIntErr);
    if (tDiff < 0 || tDiff > 5000) {
      pInst->IntErrCnt = 0;
    }
    pInst->LastIntErr = Now;
    if (++pInst->IntErrCnt > 10u) {
      pInst->RunningState = StateError;
      USBH_WARN((USBH_MCAT_HID_URB, "_OnIntInCompletion: Retry count expired: read stopped: %s", USBH_GetStatusStr(pUrb->Header.Status)));
    }
  }
  if (pInst->RunningState == StateInit || pInst->RunningState == StateRunning) {
    //
    // Resubmit a transfer request in case the plug-in required it.
    //
    if (pInst->PollIntEP != 0u) {
      EP_INC_REF_CNT(&pInst->IntIn);
      Status = _SubmitInBuffer(pInst, pInst->pInBuffer, pInst->IntIn.MaxPacketSize, NULL, NULL);
      if (Status != USBH_STATUS_PENDING) {
        pInst->RunningState = StateError;
        EP_DEC_REF_CNT(&pInst->IntIn);
      }
    }
  }
  if (pUrb->Header.pfOnUserCompletion != NULL) {
    (pUrb->Header.pfOnUserCompletion)(pUrb->Header.pUserContext);
  } else {
    if (pUrb->Header.pUserContext != NULL) {
      USBH_HID_RW_CONTEXT * pRWContext;

      pRWContext = USBH_CTX2PTR(USBH_HID_RW_CONTEXT, pUrb->Header.pUserContext);
      pRWContext->NumBytesTransferred = pUrb->Request.BulkIntRequest.Length;
    }
  }
  if (pInst->IntIn.pEvent != NULL) {
    USBH_OS_SetEvent(pInst->IntIn.pEvent); // Used in USBH_HID_GetReport.
  }
Err:
  DEC_REF_CNT(pInst);
Err1:
  EP_DEC_REF_CNT(&pInst->IntIn);
  USBH_LOG((USBH_MCAT_HID_URB, "]_OnIntInCompletion"));
}

/*********************************************************************
*
*       _GetReportDescriptor
*
*  Function description
*    The report descriptor is the essential descriptor that is used to
*    describe the functionality of the HID device.
*    This function submits a control request in order to retrieve this
*    descriptor.
*/
static USBH_STATUS _GetReportDescriptor(USBH_HID_INST * pInst) {
  USBH_STATUS         Status;
  USBH_URB          * pURB;

  pURB                                       = &pInst->Control.Urb;
  pURB->Header.pfOnCompletion                = _OnSubmitUrbCompletion;
  pURB->Header.pContext                      = &pInst->Control;
  pURB->Header.Function                      = USBH_FUNCTION_CONTROL_REQUEST;
  pURB->Request.ControlRequest.Setup.Type    = 0x81; // STD, IN, device
  pURB->Request.ControlRequest.Setup.Request = USB_REQ_GET_DESCRIPTOR;
  pURB->Request.ControlRequest.Setup.Value   = 0x2200;
  pURB->Request.ControlRequest.Setup.Index   = pInst->DevInterfaceID;
  pURB->Request.ControlRequest.Setup.Length  = pInst->ReportDescriptorSize;
  pURB->Request.ControlRequest.pBuffer       = pInst->pReportBufferDesc;
  EP_INC_REF_CNT(&pInst->Control);
  Status                                     = USBH_SubmitUrb(pInst->hInterface, pURB);
  if (Status != USBH_STATUS_PENDING) {
    EP_DEC_REF_CNT(&pInst->Control);
    USBH_WARN((USBH_MCAT_HID_URB, "_GetReportDescriptor: USBH_SubmitUrb (%s)", USBH_GetStatusStr(Status)));
  } else {
    //
    // Aborting the URB is handled by the removal timer as this function
    // is only called during start-up and the instance is removed when this function does not succeed.
    //
    if (USBH_OS_WaitEventTimed(pInst->Control.pEvent, USBH_HID_EP0_TIMEOUT) != USBH_OS_EVENT_SIGNALED) {
      Status = USBH_STATUS_TIMEOUT;
    } else {
      Status = pURB->Header.Status;
    }
  }
  if (Status != USBH_STATUS_SUCCESS) {
    USBH_WARN((USBH_MCAT_HID_URB, "_GetReportDescriptor: USBH_SubmitUrb (%s)", USBH_GetStatusStr(Status)));
  }
  return Status;
}

/*********************************************************************
*
*       _SetDeviceIdle
*
*  Function description
*    With a SetIdle request the device is set in a state where
*    it only sends a report when a change was recognized or a given timeout passed
*    otherwise it shall answer with NAK when no notification.
*/
static USBH_STATUS _SetDeviceIdle(USBH_HID_INST * pInst) {
  USBH_STATUS         Status;
  USBH_URB          * pURB;

  pURB                                       = &pInst->Control.Urb;
  pURB->Header.pfOnCompletion                = _OnSubmitUrbCompletion;
  pURB->Header.pContext                      = &pInst->Control;
  pURB->Header.Function                      = USBH_FUNCTION_CONTROL_REQUEST;
  pURB->Request.ControlRequest.Setup.Type    = 0x21; // Interface, OUT, Class
  pURB->Request.ControlRequest.Setup.Request = 0x0a;
  pURB->Request.ControlRequest.Setup.Value   = 0x0000;
  pURB->Request.ControlRequest.Setup.Index   = pInst->DevInterfaceID;
  pURB->Request.ControlRequest.Setup.Length  = 0;
  pURB->Request.ControlRequest.pBuffer       = NULL;
  EP_INC_REF_CNT(&pInst->Control);
  Status                                     = USBH_SubmitUrb(pInst->hInterface, pURB);
  if (Status != USBH_STATUS_PENDING) {
    EP_DEC_REF_CNT(&pInst->Control);
  } else {
    if (USBH_OS_WaitEventTimed(pInst->Control.pEvent, USBH_HID_EP0_TIMEOUT) != USBH_OS_EVENT_SIGNALED) {
      //
      // Aborting the URB is handled by the removal timer as this function
      // is only called during start-up and the instance is removed when this function does not succeed.
      //
      Status = USBH_STATUS_TIMEOUT;
    } else {
      Status = pURB->Header.Status;
      if (Status == USBH_STATUS_STALL) {
        //
        // A stall from the device is not treated as error.
        //
        USBH_LOG((USBH_MCAT_HID_URB, "_SetDeviceIdle: Stall"));
        Status = USBH_STATUS_SUCCESS;
      }
    }
  }
  if (Status != USBH_STATUS_SUCCESS) {
    USBH_WARN((USBH_MCAT_HID_URB, "_SetDeviceIdle: USBH_SubmitUrb (%s)", USBH_GetStatusStr(Status)));
  }
  return Status;
}

/*********************************************************************
*
*       _SubmitInBuffer
*
*  Function description
*    Submits a request to the HID device.
*/
static USBH_STATUS _SubmitInBuffer(USBH_HID_INST * pInst, U8 * pBuffer, U32 NumBytes, USBH_HID_USER_FUNC * pfUser, USBH_HID_RW_CONTEXT * pRWContext) {
  USBH_STATUS Status;
  USBH_URB  * pURB;

  pURB = &pInst->IntIn.Urb;
  pURB->Header.pContext                 = pInst;
  pURB->Header.Function                 = USBH_FUNCTION_INT_REQUEST;
  pURB->Request.BulkIntRequest.Endpoint = pInst->IntIn.EPAddr;
  pURB->Request.BulkIntRequest.pBuffer  = pBuffer;
  pURB->Request.BulkIntRequest.Length   = NumBytes;
  if (pfUser != NULL) {
    pURB->Header.pfOnCompletion     = _OnAsyncCompletion;
    pURB->Header.pfOnUserCompletion = (USBH_ON_COMPLETION_USER_FUNC *)pfUser;     //lint !e9074 !e9087  D:104
    pRWContext->pUserBuffer         = pBuffer;
    pRWContext->UserBufferSize      = NumBytes;
    pInst->IntIn.InUse = 1;
  } else {
    pURB->Header.pfOnCompletion     = _OnIntInCompletion;
    pURB->Header.pfOnUserCompletion = NULL;
  }
  pURB->Header.pUserContext         = pRWContext;
  Status = INC_REF_CNT(pInst);
  if (Status == USBH_STATUS_SUCCESS) {
    Status = USBH_SubmitUrb(pInst->hInterface, pURB);
    if (Status != USBH_STATUS_PENDING) {
      USBH_WARN((USBH_MCAT_HID_URB, "_SubmitInBuffer: SubmitUrb %s", USBH_GetStatusStr(Status)));
      DEC_REF_CNT(pInst);
      Status = USBH_STATUS_DEVICE_REMOVED;
    }
  }
  if (Status != USBH_STATUS_PENDING) {
    USBH_WARN((USBH_MCAT_HID_URB, "_SubmitInBuffer failed: %s", USBH_GetStatusStr(Status)));
  }
  return Status;
}

/*********************************************************************
*
*       _StopDevice
*/
static void _StopDevice(USBH_HID_INST * pInst) {
  if (StateStop == pInst->RunningState || StateError == pInst->RunningState) {
    USBH_LOG((USBH_MCAT_HID, "USBH_HID_Stop: app. already stopped state: %d!", pInst->RunningState));
    return;
  }
  // Stops submitting of new URBs from the application
  pInst->RunningState = StateStop;
  if (NULL == pInst->hInterface) {
    USBH_LOG((USBH_MCAT_HID, "USBH_HID_Stop: interface handle is null, nothing to do!"));
    return;
  }
  if (pInst->RefCnt != 0u) {
    //
    // If there are any operation pending, then cancel them in order to return from those routines
    // The return value of those functions shall be USBH_STATUS_CANCELLED
    //
    _CancelIO(pInst);
  }
}

/*********************************************************************
*
*       _StartDevice
*
*  Function description
*   The function is called for every interface with HID class for
*   a newly connected device.
*
*  Parameters
*    pInst        : Pointer to a HID instance.
*
*  Return value
*    USBH_STATUS  -
*/
static USBH_STATUS _StartDevice(USBH_HID_INST * pInst) {
  USBH_STATUS  Status;
  USBH_EP_MASK EPMask;
  const U8   * pDesc;
  unsigned     Length;
  USBH_DLIST * pEntry;
  U8           aEpDesc[USB_ENDPOINT_DESCRIPTOR_LENGTH];

  //
  // Open the HID interface
  //
  Status = USBH_OpenInterface(pInst->InterfaceID, 0, &pInst->hInterface);
  if (USBH_STATUS_SUCCESS != Status) {
    USBH_WARN((USBH_MCAT_HID, "USBH_HID_Start: USBH_OpenInterface failed %s!", USBH_GetStatusStr(Status)));
    goto Err;
  }
  //
  // Get first the EP IN descriptor
  //
  USBH_MEMSET(&EPMask,  0, sizeof(USBH_EP_MASK));
  EPMask.Mask      = USBH_EP_MASK_TYPE | USBH_EP_MASK_DIRECTION;
  EPMask.Direction = USB_IN_DIRECTION;
  EPMask.Type      = USB_EP_TYPE_INT;
  Length           = sizeof(aEpDesc);
  Status           = USBH_GetEndpointDescriptor(pInst->hInterface, 0, &EPMask, aEpDesc, &Length);
  if (Status != USBH_STATUS_SUCCESS) {
    USBH_WARN((USBH_MCAT_HID, "USBH_HID_Start: USBH_GetEndpointDescriptor failed: %s", USBH_GetStatusStr(Status)));
    goto Err;
  }
  pInst->IntIn.MaxPacketSize = aEpDesc[USB_EP_DESC_PACKET_SIZE_OFS] + ((U16)aEpDesc[USB_EP_DESC_PACKET_SIZE_OFS + 1u] << 8);
  pInst->IntIn.EPAddr = aEpDesc[USB_EP_DESC_ADDRESS_OFS];
  USBH_LOG((USBH_MCAT_HID, "Address   Attrib.   MaxPacketSize   Interval"));
  USBH_LOG((USBH_MCAT_HID, "0x%02X      0x%02X      %5d             %d", (int)aEpDesc[USB_EP_DESC_ADDRESS_OFS], (int)aEpDesc[USB_EP_DESC_ATTRIB_OFS], pInst->IntIn.MaxPacketSize, (int)aEpDesc[USB_EP_DESC_INTERVAL_OFS]));
  pInst->Control.pEvent = USBH_OS_AllocEvent();
  if (pInst->Control.pEvent == NULL) {
    Status = USBH_STATUS_RESOURCES;
    goto Err;
  }
  pInst->Control.hInterface = pInst->hInterface;
  pInst->IntIn.pEvent = USBH_OS_AllocEvent();
  if (pInst->IntIn.pEvent == NULL) {
    Status = USBH_STATUS_RESOURCES;
    goto Err;
  }
  pInst->IntIn.hInterface = pInst->hInterface;
  //
  // Now try to get the EP OUT descriptor
  //
  USBH_MEMSET(&EPMask, 0, sizeof(USBH_EP_MASK));
  EPMask.Mask      = USBH_EP_MASK_TYPE | USBH_EP_MASK_DIRECTION;
  EPMask.Direction = USB_OUT_DIRECTION;
  EPMask.Type      = USB_EP_TYPE_INT;
  Length           = sizeof(aEpDesc);
  Status           = USBH_GetEndpointDescriptor(pInst->hInterface, 0, &EPMask, aEpDesc, &Length);
  if (Status == USBH_STATUS_SUCCESS) {
    pInst->IntOut.MaxPacketSize = aEpDesc[USB_EP_DESC_PACKET_SIZE_OFS] + ((U16)aEpDesc[USB_EP_DESC_PACKET_SIZE_OFS + 1u] << 8);
    pInst->IntOut.EPAddr = aEpDesc[USB_EP_DESC_ADDRESS_OFS];
    pInst->IntOut.hInterface = pInst->hInterface;
    Status = USBH_GetMaxTransferSize(pInst->IntOut.hInterface, pInst->IntOut.EPAddr, &pInst->MaxOutTransferSize);
    if (Status != USBH_STATUS_SUCCESS) {
      goto Err;
    }
  } else {
    //
    // EP Out is not always present, so this is not an error.
    // Only decrement the EP ref count.
    //
    EP_DEC_REF_CNT(&pInst->IntOut); // Remove initial ref count.
    Status = USBH_GetMaxTransferSize(pInst->Control.hInterface, pInst->Control.EPAddr, &pInst->MaxOutTransferSize);
    if (Status != USBH_STATUS_SUCCESS) {
      goto Err;
    }
  }
  Status = USBH_GetMaxTransferSize(pInst->IntIn.hInterface, pInst->IntIn.EPAddr, &pInst->MaxInTransferSize);
  if (Status != USBH_STATUS_SUCCESS) {
    goto Err;
  }
  pInst->IntOut.pEvent = USBH_OS_AllocEvent();
  if (pInst->IntOut.pEvent == NULL) {
    Status = USBH_STATUS_RESOURCES;
    goto Err;
  }
  if (USBH_GetInterfaceDescriptorPtr(pInst->hInterface, 0, &pDesc, &Length) != USBH_STATUS_SUCCESS) {
    USBH_WARN((USBH_MCAT_HID, "USBH_GetInterfaceDescriptor: failed (%s)!", USBH_GetStatusStr(Status)));
    goto Err;
  }
  pInst->DevInterfaceID = pDesc[2];
  if (USBH_GetDescriptorPtr(pInst->hInterface, 0, USB_HID_DESCRIPTOR_TYPE, &pDesc) == USBH_STATUS_SUCCESS) {
    unsigned NumDesc;
    unsigned i;

    NumDesc = pDesc[USBH_HID_DESC_NUM_DESCS_OFFSET]; // bNumDescriptors
    //
    // An HID descriptor with bNumDescriptors==1 has a size of 9 bytes.
    //
    if (*pDesc < USBH_HID_DESC_TYPE_OFFSET + USBH_HID_DESC_SIZE * NumDesc) {
      USBH_WARN((USBH_MCAT_HID, "USBH_GetDescriptor: wrong size for USB_HID_DESCRIPTOR_TYPE (%u)!", *pDesc));
      Status = USBH_STATUS_ERROR;
      goto Err;
    }
    for (i = 0; i < NumDesc; i++) {
      if (pDesc[USBH_HID_DESC_TYPE_OFFSET + USBH_HID_DESC_SIZE * i] == USB_HID_DESCRIPTOR_TYPE_REPORT) {         // bDescriptorType with offset
        pInst->ReportDescriptorSize = USBH_LoadU16LE(pDesc + USBH_HID_DESC_LEN_OFFSET + USBH_HID_DESC_SIZE * i); // wDescriptorLength with offset
        break;
      }
    }
  } else {
    USBH_WARN((USBH_MCAT_HID, "USBH_GetDescriptor: failed (%s)!", USBH_GetStatusStr(Status)));
    goto Err;
  }
  pInst->pReportBufferDesc =  (U8 *)USBH_TRY_MALLOC_ZEROED(pInst->ReportDescriptorSize);
  if (pInst->pReportBufferDesc == NULL) {
    Status = USBH_STATUS_MEMORY;
    goto Err;
  }
  pInst->pInBuffer         =  (U8 *)USBH_TRY_MALLOC_ZEROED(pInst->IntIn.MaxPacketSize);
  if (pInst->pInBuffer == NULL) {
    Status = USBH_STATUS_MEMORY;
    goto Err;
  }
  if (pInst->IntOut.MaxPacketSize != 0u) {
    pInst->pOutBuffer      =  (U8 *)USBH_TRY_MALLOC_ZEROED(pInst->IntOut.MaxPacketSize);
    if (pInst->pOutBuffer == NULL) {
      Status = USBH_STATUS_MEMORY;
      goto Err;
    }
  }
  //
  // Get the report descriptor.
  //
  Status = _GetReportDescriptor(pInst);
  if (Status != USBH_STATUS_SUCCESS) {
    goto Err;
  }
  //
  // Set the device idle, if it does not work we can continue anyway.
  //
  (void)_SetDeviceIdle(pInst);
  pEntry = USBH_DLIST_GetNext(&USBH_HID_Global.PluginList);
  while (pEntry != &USBH_HID_Global.PluginList) {
    USBH_HID_DETECTION_HOOK *pPlugin;
    pPlugin = GET_HID_PLUGIN_FROM_ENTRY(pEntry);
    USBH_ASSERT_MAGIC(pPlugin, HID_PLUGIN);
    pPlugin->pDetect(pInst);
    pEntry = USBH_DLIST_GetNext(pEntry);
  }
  if (USBH_HID_Global.pfOnReport != NULL) {
    pInst->PollIntEP = 1;
  }
  //
  // If the interface is handled by a plug-in and the plug-in needs the reports: Start the submission of interrupt IN URBs.
  //
  if (pInst->PollIntEP != 0u) {
    EP_INC_REF_CNT(&pInst->IntIn);
    Status = _SubmitInBuffer(pInst, pInst->pInBuffer, pInst->IntIn.MaxPacketSize, NULL, NULL);
    if (Status != USBH_STATUS_PENDING) {
      EP_DEC_REF_CNT(&pInst->IntIn);
      pInst->RunningState = StateError;
      goto Err;
    } else {
      Status = USBH_STATUS_SUCCESS;
    }
  } else {
    Status = USBH_STATUS_SUCCESS;
  }
  return Status;
Err:
  //
  // Removal is handled by the timer.
  //
  DEC_REF_CNT(pInst);  // CreateDevInstance()
  return Status;
}

/*********************************************************************
*
*       _OnGeneralDeviceNotification
*/
static void _OnGeneralDeviceNotification(void * pContext, USBH_PNP_EVENT Event, USBH_INTERFACE_ID InterfaceID) {
  USBH_HID_INST          * pInst;
  int                      Found;
  USBH_STATUS              Status;
  USBH_DEVICE_EVENT        DeviceEvent;
  USBH_NOTIFICATION_HOOK * pHook;

  USBH_USE_PARA(pContext);
  Found = 0;
  if (Event == USBH_ADD_DEVICE) {
    pInst = _CreateDevInstance();
    if (pInst == NULL) {
      USBH_WARN((USBH_MCAT_HID, "_OnGeneralDeviceNotification: device instance not created!"));
      return;
    }
    USBH_LOG((USBH_MCAT_HID, "DeviceNotification: USB HID device detected interface ID: %u !", InterfaceID));
    pInst->RunningState = StateInit;
    pInst->InterfaceID = InterfaceID;
    Status = _StartDevice(pInst);
    if (Status != USBH_STATUS_SUCCESS) { // On error
      pInst->RunningState = StateError;
      return;
    }
    pInst->RunningState = StateRunning;
    pInst->WasNotified  = 1;
    DeviceEvent = USBH_DEVICE_EVENT_ADD;
  } else {
    pInst = USBH_HID_Global.pFirst;
    while (pInst != NULL) {   // Iterate over all instances
      if (pInst->InterfaceID == InterfaceID) {
        Found = 1;
        break;
      }
      pInst = pInst->pNext;
    }
    if (Found == 0) {
      USBH_WARN((USBH_MCAT_HID, "_OnGeneralDeviceNotification: pInst not found for notified interface!"));
      return;
    }
    if (pInst->WasNotified == 0) {
      return;
    }
    USBH_LOG((USBH_MCAT_HID, "DeviceNotification: USB HID device removed interface  ID: %u !", InterfaceID));
    _StopDevice(pInst);
    DEC_REF_CNT(pInst);
    DeviceEvent = USBH_DEVICE_EVENT_REMOVE;
  }
  pHook = USBH_HID_Global.pFirstNotiHook;
  while (pHook != NULL) {
    pHook->pfNotification(pHook->pContext, pInst->DevIndex, DeviceEvent);
    pHook = pHook->pNext;
  }
  if (USBH_HID_Global.pfOnUserNotification != NULL) {
    USBH_HID_Global.pfOnUserNotification(USBH_HID_Global.pUserNotifyContext, pInst->DevIndex, DeviceEvent);
  }
}

/*********************************************************************
*
*       _GetDeviceInfo
*
*  Function description
*     Retrieves information about a HID device.
*
*  Parameters
*    pInst       :  Pointer to HID instance.
*    pDevInfo    :  Pointer to a USBH_HID_DEVICE_INFO buffer.
*/
static USBH_STATUS _GetDeviceInfo(USBH_HID_INST * pInst, USBH_HID_DEVICE_INFO * pDevInfo) {
  USBH_STATUS           Status;
  USBH_INTERFACE_INFO   InterFaceInfo;

  Status = USBH_GetInterfaceInfo(pInst->InterfaceID, &InterFaceInfo);
  if (Status != USBH_STATUS_SUCCESS) {
    return Status;
  }
  USBH_HID__ParseReportDesc(pInst, NULL, NULL);
  pDevInfo->InputReportSize  = pInst->ReportInfo[0].InputReportSize;
  pDevInfo->OutputReportSize = pInst->ReportInfo[0].OutputReportSize;
  pDevInfo->ProductId        = InterFaceInfo.ProductId;
  pDevInfo->VendorId         = InterFaceInfo.VendorId;
  pDevInfo->InterfaceNo      = InterFaceInfo.Interface;
  pDevInfo->DevIndex         = pInst->DevIndex;
  pDevInfo->InterfaceID      = pInst->InterfaceID;
  pDevInfo->NumReportInfos   = pInst->NumReportInfos;
  pDevInfo->DeviceType       = pInst->DeviceType;
  USBH_MEMCPY(pDevInfo->ReportInfo, pInst->ReportInfo, sizeof(pDevInfo->ReportInfo));
  return USBH_STATUS_SUCCESS;
}

/*********************************************************************
*
*       _SetReport
*/
static USBH_STATUS _SetReport(USBH_HID_HANDLE hDevice, const U8 * pBuffer, U32 BufferSize,
                              USBH_HID_USER_FUNC * pfFunc, USBH_HID_RW_CONTEXT * pRWContext, unsigned Flags) {
  USBH_HID_INST * pInst;
  USBH_STATUS     Status;
  HID_EP_DATA   * pEPData;

  Status = USBH_STATUS_INVALID_PARAM;
  if ((pfFunc != NULL) && (pRWContext == NULL)) {
    return Status;
  }
  pInst = _h2p(hDevice);
  if (pInst == NULL) {
    return USBH_STATUS_INVALID_HANDLE;
  }
  if (pInst->IsOpened == 0) {
    return USBH_STATUS_NOT_OPENED;
  }
  if (BufferSize > pInst->MaxOutTransferSize) {
    USBH_WARN((USBH_MCAT_HID_URB, "_SetReport BufferSize (%d) too large, max possible is %d", BufferSize, pInst->MaxOutTransferSize));
    return USBH_STATUS_XFER_SIZE;
  }
  if (pfFunc != NULL) {
    return USBH_HID__SubmitOutBuffer(pInst, pBuffer, BufferSize, pfFunc, pRWContext, Flags);
  }
  if (pInst->IntOut.EPAddr == 0u || (Flags & USBH_HID_FEATURE_REPORT) != 0u) {
    pEPData = &pInst->Control;
  } else {
    pEPData = &pInst->IntOut;
  }
  USBH_OS_ResetEvent(pEPData->pEvent);
  Status = USBH_HID__SubmitOutBuffer(pInst, pBuffer, BufferSize, NULL, NULL, Flags);
  if (Status == USBH_STATUS_PENDING) {
    if (USBH_OS_WaitEventTimed(pEPData->pEvent, USBH_HID_Global.ControlWriteTimeout) != USBH_OS_EVENT_SIGNALED) {
      Status = _AbortEP(pEPData);
      if (Status != USBH_STATUS_SUCCESS) {
        USBH_WARN((USBH_MCAT_HID_URB, "_SetReport: Cancel operation status: 0x%08x", Status));
      }
      USBH_OS_WaitEvent(pEPData->pEvent);
    }
    Status = pEPData->Urb.Header.Status;
  }
  return Status;
}

/*********************************************************************
*
*       _OnSetIndComplete
*
*  Function description
*    Called on completion of the indicator change operation.
*/
static void _OnSetIndComplete(USBH_HID_RW_CONTEXT * pRWContext) USBH_CALLBACK_USE {
  USBH_OS_EVENT_OBJ * pEvent;

  pEvent = USBH_CTX2PTR(USBH_OS_EVENT_OBJ, pRWContext->pUserContext);
  USBH_OS_SetEvent(pEvent);
}

/*********************************************************************
*
*       USBH_HID private functions
*
**********************************************************************
*/

/*********************************************************************
*
*       USBH_HID__GetBits
*
*  Function description
*    Returns a unsigned value from the bit field.
*/
U32 USBH_HID__GetBits(const U8 * pData, unsigned FirstBit, unsigned NumBits) {
  U32 Value;
  //
  // Find starting byte of bit field
  //
  pData += (FirstBit >> 3);
  FirstBit &= 7u;
  Value = USBH_LoadU32LE(pData);
  Value >>= FirstBit;
  Value &= ((1uL << NumBits) - 1u);
  return Value;
}

/*********************************************************************
*
*       USBH_HID__GetBitsSigned
*
*  Function description
*    Returns a signed value from the bit field.
*/
I32 USBH_HID__GetBitsSigned(const U8 * pData, unsigned FirstBit, unsigned NumBits) {
  U32 Data;
  U32 Mask;

  Data = USBH_HID__GetBits(pData, FirstBit, NumBits);
  Mask = (U32)(-1) << NumBits;
  if ((Data & (Mask >> 1)) != 0u) {
    //
    // Value is negative, add sign bits.
    //
    Data |= Mask;
  }
  return (I32)Data;
}

/*********************************************************************
*
*       USBH_HID__ParseReportDesc()
*
*  Function description
*    Simple report descriptor parser.
*    Report sizes and ID's are stored into pInst->ReportInfo.
*/
void USBH_HID__ParseReportDesc(USBH_HID_INST * pInst, _CHECK_REPORT_DESC_FUNC *pCheckFunc, void *pContext) {
  U32 ItemLen;
  U32 Len;
  U32 Data;
  U8  *p;
  U8  c;
  unsigned i;
  USBH_HID_REPORT_INFO *pInfo;
  HID_FIELD_INFO Field;

  if (pInst->NumReportInfos != 0u && pCheckFunc == NULL) {
    //
    // Already parsed.
    //
    return;
  }
  //
  // Parse report descriptor to get in/out report sizes.
  //
  p = pInst->pReportBufferDesc;
  Len = pInst->ReportDescriptorSize;
  USBH_MEMSET(&Field, 0, sizeof(Field));
  Field.pContext = pContext;
  pInst->NumReportInfos = 0;
  while (Len > 0u) {
    c = *p;
    if ((c & USBH_HID_REPORT_LONG_ITEM) == USBH_HID_REPORT_LONG_ITEM) {    // Long item
      if (Len < 3u) {
        break;
      }
      ItemLen = (U32)p[1] + 3u;
    } else { // Short item
      ItemLen = ((U32)c & 3u) + 1u;
      //
      // Short item length is as follows:
      // 0 = 0 bytes
      // 1 = 1 byte
      // 2 = 2 bytes
      // 3 = 4 bytes
      //
      if (ItemLen == 4u) {
        ItemLen = 5;
      }
    }
    if (Len < ItemLen) {
      break;
    }
    //
    // Get data of item
    //
    Data = 0;
    for (i = ItemLen - 1u; i > 0u; i--) {
      Data = (Data << 8) + p[i];
    }
    switch (c & USBH_HID_REPORT_TYPE_MASK) {
    case USBH_HID_REPORT_INPUT:                                      // Input tag
      if (pCheckFunc != NULL && (Data & 1u) == 0u) {
        pCheckFunc(Data & 2u, &Field);
      }
      Field.InRptLen += (U32)Field.RptCount * Field.RptSize;
      Field.NumUsages = 0;
      Field.UsageMax  = 0;
      Field.Signed    = 0;
      break;
    case USBH_HID_REPORT_OUTPUT:                                     // Output tag
      if (pCheckFunc != NULL && (Data & 1u) == 0u) {
        pCheckFunc((Data & 2u) + 1u, &Field);
      }
      Field.OutRptLen += (U32)Field.RptCount * Field.RptSize;
      Field.NumUsages = 0;
      Field.UsageMax  = 0;
      Field.Signed    = 0;
      break;
    case USBH_HID_REPORT_FEATURE:
    case USBH_HID_REPORT_COLLECTION:
      Field.NumUsages = 0;
      Field.UsageMax  = 0;
      break;
    case USBH_HID_REPORT_COUNT:                                      // Report count
      Field.RptCount = Data;
      break;
    case USBH_HID_REPORT_SIZE:                                       // Report size
      Field.RptSize = Data;
      break;
    case USBH_HID_REPORT_USAGE_PAGE:                                 // Usage
      Field.UsagePage = Data << 16;
      break;
    case USBH_HID_REPORT_USAGE:
      if (ItemLen < 4u) {
        Data |= Field.UsagePage;
      }
      //
      // Check for application usages
      //
      if ((Data > (USBH_HID_USAGE_PAGE_GENERIC_DESKTOP << 16) && Data < (USBH_HID_USAGE_PAGE_GENERIC_DESKTOP << 16) + 0x30u) ||
          (Data > (USBH_HID_USAGE_PAGE_DIGITIZERS      << 16) && Data < (USBH_HID_USAGE_PAGE_DIGITIZERS      << 16) + 0x30u)) {
        Field.AppUsage = Data;
        break;
      }
      if (Field.NumUsages < USBH_HID_MAX_USAGES) {
        Field.Usage[Field.NumUsages++] = Data;
      } else {
        USBH_WARN((USBH_MCAT_HID_RDESC, "USBH_HID_MAX_USAGES too small"));
      }
      break;
    case USBH_HID_REPORT_USAGE_MIN:
      if (ItemLen < 4u) {
        Data |= Field.UsagePage;
      }
      Field.UsageMin = Data;
      break;
    case USBH_HID_REPORT_USAGE_MAX:
      if (ItemLen < 4u) {
        Data |= Field.UsagePage;
      }
      Field.UsageMax = Data;
      Field.NumUsages = 0;
      break;
    case USBH_HID_REPORT_LOGICAL_MIN:
      Field.Signed = _CheckSigned(Data, ItemLen - 1u, &Field.LogicalMin);
      break;
    case USBH_HID_REPORT_LOGICAL_MAX:
      (void)_CheckSigned(Data, ItemLen - 1u, &Field.LogicalMax);
      break;
    case USBH_HID_REPORT_PHYSICAL_MIN:
      Field.PhySigned = _CheckSigned(Data, ItemLen - 1u, &Field.PhysicalMin);
      break;
    case USBH_HID_REPORT_PHYSICAL_MAX:
      (void)_CheckSigned(Data, ItemLen - 1u, &Field.PhysicalMax);
      break;
    case USBH_HID_REPORT_ID:
      pInst->ReportIDsUsed = 1;
      _SetReportInfo(pInst, &Field);
      //
      // Check, if we have already seen the new report ID
      //
      pInfo = _FindReportInfo(pInst, Data);
      if (pInfo != NULL) {
        Field.InRptLen  = pInfo->InputReportSize;
        Field.OutRptLen = pInfo->OutputReportSize;
      } else {
        Field.InRptLen  = 0;
        Field.OutRptLen = 0;
      }
      Field.ReportId = Data;
      break;
    default:
      // Ignore item
      break;
    }
    p += ItemLen;
    Len -= ItemLen;
  }
  _SetReportInfo(pInst, &Field);
  //
  // Convert length to bytes
  //
  pInfo = pInst->ReportInfo;
  for (i = 0; i < pInst->NumReportInfos; i++) {
    pInfo->InputReportSize  = (pInfo->InputReportSize  + 7u) >> 3;     // Round up to the next byte.
    pInfo->OutputReportSize = (pInfo->OutputReportSize + 7u) >> 3;
    USBH_LOG((USBH_MCAT_HID_RDESC, "Report ID %u, IN=%u OUT=%u", pInfo->ReportId, pInfo->InputReportSize, pInfo->OutputReportSize));
    pInfo++;
  }
}

/*********************************************************************
*
*       USBH_HID__SubmitOutBuffer
*
*  Function description
*    Submits a request to the HID device.
*    The submit operation depends whether there is an OUT-endpoint was specified
*    by the device.
*    If there is no OUT-endpoint, a control-request with the request type SET_REPORT.
*/
USBH_STATUS USBH_HID__SubmitOutBuffer(USBH_HID_INST * pInst, const U8 * pBuffer, U32 NumBytes,
                                      USBH_HID_USER_FUNC * pfUser, USBH_HID_RW_CONTEXT * pRWContext, unsigned Flags) {
  USBH_STATUS   Status;
  USBH_URB    * pURB;
  HID_EP_DATA * pEPData;
  unsigned      Value;

  Status = USBH_STATUS_SUCCESS;
  if (pInst->IntOut.EPAddr != 0u && (Flags & USBH_HID_FEATURE_REPORT) == 0u) {
    pEPData = &pInst->IntOut;
  } else {
    pEPData = &pInst->Control;
  }
  USBH_OS_Lock(USBH_MUTEX_HID);
  if (pEPData->InUse != 0) {
    Status = USBH_STATUS_BUSY;
  } else {
    pEPData->InUse = 1;
  }
  USBH_OS_Unlock(USBH_MUTEX_HID);
  //
  // If the device does not have an interrupt OUT endpoint - use the control endpoint.
  //
  if (Status == USBH_STATUS_SUCCESS) {
    if (pInst->IntOut.EPAddr != 0u && (Flags & USBH_HID_FEATURE_REPORT) == 0u) {
      pURB = &pEPData->Urb;
      pURB->Header.Function                      = USBH_FUNCTION_INT_REQUEST;
      pURB->Request.BulkIntRequest.Endpoint      = pEPData->EPAddr;
      pURB->Request.BulkIntRequest.pBuffer       = (U8 *)pBuffer;                  //lint !e9005 D:105[a]
      pURB->Request.BulkIntRequest.Length        = NumBytes;
    } else {
      pURB = &pEPData->Urb;
      pURB->Header.Function                      = USBH_FUNCTION_CONTROL_REQUEST;
      pURB->Request.ControlRequest.Setup.Type    = USB_TO_DEVICE | USB_REQTYPE_CLASS | USB_INTERFACE_RECIPIENT;
      pURB->Request.ControlRequest.Setup.Request = 0x09u;
      Value = 0x0200u;
      if ((Flags & USBH_HID_USE_REPORT_ID) != 0u) {
        Value |= *pBuffer;
      }
      if ((Flags & USBH_HID_FEATURE_REPORT) != 0u) {
        Value |= 0x0300u;
      }
      pURB->Request.ControlRequest.Setup.Value = Value;
      pURB->Request.ControlRequest.pBuffer       = (U8 *)pBuffer;                  //lint !e9005 D:105[a]
      pURB->Request.ControlRequest.Setup.Index   = pInst->DevInterfaceID;
      pURB->Request.ControlRequest.Setup.Length  = (U16)NumBytes;
    }
    EP_INC_REF_CNT(pEPData);
    pURB->Header.pContext       = pInst;
    if (pfUser != NULL) {
      pURB->Header.pfOnCompletion     = _OnAsyncCompletion;
      pURB->Header.pfOnUserCompletion = (USBH_ON_COMPLETION_USER_FUNC *)pfUser;    //lint !e9074 !e9087  D:104
      pURB->Header.pUserContext       = pRWContext;
      pRWContext->pUserBuffer         = (U8 *)pBuffer;                             //lint !e9005 D:105[a]
      pRWContext->UserBufferSize      = NumBytes;
    } else {
      pURB->Header.pfOnCompletion     = _OnOutCompletion;
      pURB->Header.pfOnUserCompletion = NULL;
    }
    Status = INC_REF_CNT(pInst);
    if (Status == USBH_STATUS_SUCCESS) {
      Status = USBH_SubmitUrb(pInst->hInterface, pURB);
      if (Status != USBH_STATUS_PENDING) {
        DEC_REF_CNT(pInst);
      }
    }
    if (Status != USBH_STATUS_PENDING) {
      EP_DEC_REF_CNT(pEPData);
      pEPData->InUse = 0;
    }
  }
  return Status;
}

/*********************************************************************
*
*       USBH_HID__SubmitOut
*/
USBH_STATUS USBH_HID__SubmitOut(USBH_HID_INST * pInst, const U8 * pBuffer, U32 NumBytes) {
  USBH_STATUS         Status;
  USBH_OS_EVENT_OBJ * pEvent;
  USBH_HID_RW_CONTEXT RWContext;

  if (pInst->IsOpened == 0) {
    return USBH_STATUS_NOT_OPENED;
  }
  Status = INC_REF_CNT(pInst);
  if (Status == USBH_STATUS_SUCCESS) {
    pEvent = USBH_OS_AllocEvent();
    if (pEvent != NULL) {
      RWContext.pUserContext = USBH_PTR2CTX(pEvent);
      Status = USBH_HID__SubmitOutBuffer(pInst,pBuffer, NumBytes, _OnSetIndComplete, &RWContext, USBH_HID_OUTPUT_REPORT);
      if (Status == USBH_STATUS_PENDING) {
        if (USBH_OS_WaitEventTimed(pEvent, USBH_HID_Global.ControlWriteTimeout) == USBH_OS_EVENT_SIGNALED) {
          Status = pInst->Control.Urb.Header.Status;
        } else {
          Status = _AbortEP(&pInst->Control);
          if (Status != USBH_STATUS_SUCCESS) {
            USBH_WARN((USBH_MCAT_HID_URB, "USBH_HID__SubmitOut: Cancel operation status: 0x%08x", Status));
          }
          USBH_OS_WaitEvent(pEvent);
          Status = USBH_STATUS_TIMEOUT;
        }
      } else {
        USBH_WARN((USBH_MCAT_HID_URB, "USBH_HID__SubmitOut: status: 0x%08x", Status));
      }
      USBH_OS_FreeEvent(pEvent);
    } else {
      Status = USBH_STATUS_RESOURCES;
    }
    DEC_REF_CNT(pInst);
  }
  return Status;
}

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       USBH_HID_Init
*
*  Function description
*    Initializes and registers the HID device driver with emUSB-Host.
*
*  Return value
*    == 1  : Success.
*    == 0  : Could not register HID device driver.
*
*  Additional information
*    This function can be called multiple times, but only the first
*    call initializes the module. Any further calls only increase
*    the initialization counter. This is useful for cases where
*    the module is initialized from different places which
*    do not interact with each other, To de-initialize
*    the module USBH_HID_Exit has to be called
*    the same number of times as this function was called.
*/
U8 USBH_HID_Init(void) {
  USBH_PNP_NOTIFICATION   PnpNotify;
  USBH_INTERFACE_MASK   * pInterfaceMask;

  if (_isInited++ == 0) {
    USBH_MEMSET(&USBH_HID_Global, 0, sizeof(USBH_HID_Global));
    // Add an plug an play notification routine
    pInterfaceMask            = &PnpNotify.InterfaceMask;
    pInterfaceMask->Mask      = USBH_INFO_MASK_CLASS;
    pInterfaceMask->Class     = USB_DEVICE_CLASS_HUMAN_INTERFACE;
    PnpNotify.pContext        = NULL;
    PnpNotify.pfPnpNotification = _OnGeneralDeviceNotification;
    USBH_HID_Global.ControlWriteTimeout = USBH_HID_WRITE_DEFAULT_TIMEOUT;
    USBH_DLIST_Init(&USBH_HID_Global.PluginList);
    USBH_HID_Global.hDevNotification = USBH_RegisterPnPNotification(&PnpNotify); // Register HID devices.
    if (NULL == USBH_HID_Global.hDevNotification) {
      USBH_WARN((USBH_MCAT_HID, "USBH_HID_Init: USBH_RegisterPnPNotification"));
      return 0;
    }
  }
  return 1;
}

/*********************************************************************
*
*       USBH_HID_Exit
*
*  Function description
*    Releases all resources, closes all handles to the USB stack and unregisters all
*    notification functions.
*/
void USBH_HID_Exit(void) {
  USBH_HID_INST * pInst;

  USBH_LOG((USBH_MCAT_HID, "USBH_HID_Exit"));
  if (--_isInited != 0) {
    return;
  }
  pInst = USBH_HID_Global.pFirst;
  while (pInst != NULL) {   // Iterate over all instances
    while (pInst->IsOpened != 0) {
      --pInst->IsOpened;
      DEC_REF_CNT(pInst);
    }
    _StopDevice(pInst);
    pInst = pInst->pNext;
  }
  if (USBH_HID_Global.hDevNotification != NULL) {
    USBH_UnregisterPnPNotification(USBH_HID_Global.hDevNotification);
    USBH_HID_Global.hDevNotification = NULL;
  }
  _RemoveAllInstances();
}

/*********************************************************************
*
*       USBH_HID_SetOnReport
*
*  Function description
*    Sets a callback to be called on every report. If a callback function is
*    set, the function USBH_HID_GetReport must not be used.
*
*  Parameters
*    pfOnReport : Callback that shall be called for every report.
*/
void USBH_HID_SetOnReport(USBH_HID_ON_REPORT * pfOnReport) {
  USBH_HID_Global.pfOnReport = pfOnReport;
}

/*********************************************************************
*
*       USBH_HID_GetNumDevices
*
*  Function description
*     Returns the number of available devices. It also retrieves
*     the information about a device.
*
*  Parameters
*    pDevInfo    :  Pointer to an array of USBH_HID_DEVICE_INFO structures.
*    NumItems    :  Number of items that pDevInfo can hold.
*
*  Return value
*    Number of devices available.
*/
int USBH_HID_GetNumDevices(USBH_HID_DEVICE_INFO * pDevInfo, U32 NumItems) {
  USBH_HID_INST * pInst;
  unsigned        i;
  int             Ret;

  Ret = 0;
  if (USBH_HID_Global.NumDevices != 0u) {
    NumItems = USBH_MIN(NumItems, USBH_HID_Global.NumDevices);
    pInst = USBH_HID_Global.pFirst;
    for (i = 0; i < NumItems; i++) {
      //
      // If the device is not in a valid state - skip it.
      //
      if (pInst->RunningState == StateRunning) {
        if (_GetDeviceInfo(pInst, pDevInfo) == USBH_STATUS_SUCCESS) {
          pDevInfo++;
          Ret++;
        }
      }
      pInst = pInst->pNext;
    }
  }
  return Ret;
}

/*********************************************************************
*
*       USBH_HID_Open
*
*  Function description
*    Opens a device given by an index.
*
*  Parameters
*    Index    : Device index.
*
*  Return value
*    != USBH_HID_INVALID_HANDLE: Handle to a HID device.
*    == USBH_HID_INVALID_HANDLE: Device not available.
*
*  Additional information
*    The index of a new connected device is provided to the callback function
*    registered with USBH_HID_AddNotification().
*/
USBH_HID_HANDLE USBH_HID_Open(unsigned Index) {
  USBH_HID_INST * pInst;

  pInst = USBH_HID_Global.pFirst;
  while (pInst != NULL) {
    if (Index == pInst->DevIndex) {
      //
      // Device found
      //
      if (INC_REF_CNT(pInst) == USBH_STATUS_SUCCESS) {
        pInst->IsOpened++;
        return pInst->Handle;
      }
      break;
    }
    pInst = pInst->pNext;
  }
  return USBH_HID_INVALID_HANDLE;
}

/*********************************************************************
*
*       USBH_HID_GetDeviceInfo
*
*  Function description
*     Retrieves information about an opened HID device.
*
*  Parameters
*    hDevice     :  Handle to an opened HID device.
*    pDevInfo    :  Pointer to a USBH_HID_DEVICE_INFO buffer.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_HID_GetDeviceInfo(USBH_HID_HANDLE hDevice, USBH_HID_DEVICE_INFO * pDevInfo) {
  USBH_HID_INST       * pInst;

  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    return _GetDeviceInfo(pInst, pDevInfo);
  }
  return USBH_STATUS_INVALID_PARAM;
}

/*********************************************************************
*
*       USBH_HID_GetReportDescriptor
*
*  Function description
*    Returns the data of a report descriptor in raw form.
*    Legacy function: Use USBH_HID_GetReportDesc() instead.
*
*  Parameters
*    hDevice            :  Handle to an opened device.
*    pReportDescriptor  :  Pointer to a buffer that will receive the report descriptor.
*    NumBytes           :  Size of the buffer in bytes. If the size of the report descriptor
*                          is greater than the buffer size, only NumBytes of the report descriptor
*                          are copied into the buffer.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_HID_GetReportDescriptor(USBH_HID_HANDLE hDevice, U8 * pReportDescriptor, unsigned NumBytes) {
  USBH_HID_INST * pInst;
  unsigned        NumBytes2Copy;

  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    if (pInst->IsOpened != 0) {
      if (pInst->pReportBufferDesc != NULL) {
        NumBytes2Copy = USBH_MIN(NumBytes, pInst->ReportDescriptorSize);
        USBH_MEMCPY(pReportDescriptor, pInst->pReportBufferDesc, NumBytes2Copy);
        return USBH_STATUS_SUCCESS;
      }
    } else {
      return USBH_STATUS_NOT_OPENED;
    }
  }
  return USBH_STATUS_INVALID_PARAM;
}

/*********************************************************************
*
*       USBH_HID_GetReportDesc
*
*  Function description
*    Returns the data of a report descriptor in raw form.
*
*  Parameters
*    hDevice            :  Handle to an opened device.
*    ppReportDescriptor :  Returns a pointer to the report descriptor which is stored in an internal
*                          data structure of the USB stack. The report descriptor must not be changed.
*                          The pointer becomes invalid after the device is closed.
*    pNumBytes          :  Returns the size of the report descriptor in bytes.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_HID_GetReportDesc(USBH_HID_HANDLE hDevice, const U8 ** ppReportDescriptor, unsigned *pNumBytes) {
  USBH_HID_INST * pInst;

  pInst = _h2p(hDevice);
  if (pInst == NULL) {
    return USBH_STATUS_INVALID_PARAM;
  }
  if (pInst->IsOpened == 0) {
    return USBH_STATUS_NOT_OPENED;
  }
  if (pInst->pReportBufferDesc == NULL) {
    return USBH_STATUS_INVALID_DESCRIPTOR;
  }
  *ppReportDescriptor = pInst->pReportBufferDesc;
  *pNumBytes          = pInst->ReportDescriptorSize;
  return USBH_STATUS_SUCCESS;
}

/*********************************************************************
*
*       USBH_HID_GetReport
*
*  Function description
*    Reads a report from a HID device.
*
*  Parameters
*    hDevice       : Handle to an opened HID device.
*    pBuffer       : Pointer to a buffer to read.
*    BufferSize    : Size of the buffer.
*    pfFunc        : [Optional] Callback function of type USBH_HID_USER_FUNC
*                    invoked when the read operation finishes (asynchronous operation).
*                    It can be the NULL pointer, the function is executed synchronously.
*    pRWContext    : [Optional] Pointer to a USBH_HID_RW_CONTEXT structure which
*                    will be filled with data after the transfer has
*                    been completed and passed as a parameter to the
*                    callback function (pfFunc).
*                    If pfFunc != NULL, this parameter is required.
*                    If pfFunc == NULL, only the member pRWContext->NumBytesTransferred is set by the function.
*
*  Return value
*    == USBH_STATUS_SUCCESS        : Success on synchronous operation (pfFunc == NULL).
*    == USBH_STATUS_PENDING        : Request was submitted successfully and the application is informed
*                                    via callback (pfFunc != NULL).
*    Any other value means error.
*/
USBH_STATUS USBH_HID_GetReport(USBH_HID_HANDLE hDevice, U8 * pBuffer, U32 BufferSize, USBH_HID_USER_FUNC * pfFunc, USBH_HID_RW_CONTEXT * pRWContext) {
  USBH_HID_INST * pInst;
  USBH_STATUS     Status;
  unsigned        i;

  Status = USBH_STATUS_INVALID_PARAM;
  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    if (pInst->IsOpened != 0) {
      if (BufferSize > pInst->MaxInTransferSize) {
        USBH_WARN((USBH_MCAT_HID_URB, "USBH_HID_GetReport BufferSize (%d) too large, max possible is %d", BufferSize, pInst->MaxInTransferSize));
        return USBH_STATUS_XFER_SIZE;
      }
      USBH_HID__ParseReportDesc(pInst, NULL, NULL);
      for (i = 0; i < pInst->NumReportInfos; i++) {
        if (BufferSize < pInst->ReportInfo[i].InputReportSize) {
          USBH_WARN((USBH_MCAT_HID_URB, "Device's Input report (%d) does not fit in user buffer size (%d).", pInst->ReportInfo[i].InputReportSize, BufferSize));
          return USBH_STATUS_INVALID_PARAM;
        }
      }
      if (pfFunc != NULL) {
        if (pRWContext == NULL) {
          return USBH_STATUS_INVALID_PARAM;
        }
        EP_INC_REF_CNT(&pInst->IntIn);
        Status = _SubmitInBuffer(pInst, pBuffer, BufferSize, pfFunc, pRWContext);
        if (Status != USBH_STATUS_PENDING) {
          EP_DEC_REF_CNT(&pInst->IntIn);
        }
      } else {
        if (pRWContext != NULL) {
          pRWContext->NumBytesTransferred = 0;
        }
        EP_INC_REF_CNT(&pInst->IntIn);
        //
        // In case the application run async operation before it is possible for the event to be set
        // because the completion routine does not differentiate between async and synchronous operation.
        // Reset the event before submitting the URB.
        //
        USBH_OS_ResetEvent(pInst->IntIn.pEvent);
        Status = _SubmitInBuffer(pInst, pBuffer, BufferSize, NULL, pRWContext);
        //
        // If the status is other than pending
        // we pass the status back to the application.
        //
        if (Status == USBH_STATUS_PENDING) {
          USBH_OS_WaitEvent(pInst->IntIn.pEvent);
          Status = pInst->IntIn.Urb.Header.Status;
          if (Status == USBH_STATUS_PENDING) {
            Status = USBH_STATUS_SUCCESS;
          }
        } else {
          EP_DEC_REF_CNT(&pInst->IntIn);
        }
      }
    } else {
      Status = USBH_STATUS_NOT_OPENED;
    }
  }
  return Status;
}

/*********************************************************************
*
*       USBH_HID_SetReport
*
*  Function description
*    Sends an output report to a HID device. This function assumes report IDs
*    are not used.
*
*  Parameters
*    hDevice       : Handle to an opened HID device.
*    pBuffer       : Pointer to a buffer containing the data to be sent.
*                    In case the device has more than one report descriptor
*                    the first byte inside the buffer must contain
*                    a valid ID matching one of the report descriptors.
*    BufferSize    : Size of the buffer.
*    pfFunc        : [Optional] Callback function of type USBH_HID_USER_FUNC
*                    invoked when the send operation finishes.
*                    It can be the NULL pointer.
*    pRWContext    : [Optional] Pointer to a USBH_HID_RW_CONTEXT structure which
*                    will be filled with data after the transfer has
*                    been completed and passed as a parameter to the
*                    pfFunc function.
*
*  Return value
*    == USBH_STATUS_SUCCESS        : Success.
*    == USBH_STATUS_PENDING        : Request was submitted and application is informed via callback.
*    Any other value means error.
*/
USBH_STATUS USBH_HID_SetReport(USBH_HID_HANDLE hDevice, const U8 * pBuffer, U32 BufferSize, USBH_HID_USER_FUNC * pfFunc, USBH_HID_RW_CONTEXT * pRWContext) {
  return _SetReport(hDevice, pBuffer, BufferSize, pfFunc, pRWContext, USBH_HID_OUTPUT_REPORT);
}

/*********************************************************************
*
*       USBH_HID_SetReportEx
*
*  Function description
*    Sends an output or feature report to a HID device. Optionally sends out a report ID.
*    Output reports are send via the OUT endpoint of the device if present,
*    or using a control request otherwise.
*
*  Parameters
*    hDevice       : Handle to an opened HID device.
*    pBuffer       : Pointer to a buffer containing the data to be sent.
*                    In case the device has more than one report descriptor
*                    the first byte inside the buffer must contain
*                    a valid ID matching one of the report descriptors.
*    BufferSize    : Size of the buffer.
*    pfFunc        : [Optional] Callback function of type USBH_HID_USER_FUNC
*                    invoked when the send operation finishes.
*                    It can be the NULL pointer.
*    pRWContext    : [Optional] Pointer to a USBH_HID_RW_CONTEXT structure which
*                    will be filled with data after the transfer has
*                    been completed and passed as a parameter to the
*                    pfOnComplete function.
*    Flags         : A bitwise OR-combination of flags
*                    * USBH_HID_USE_REPORT_ID: Enables report ID usage.
*                      The first byte in the buffer pointed to by pBuffer is used as report ID.
*                    * USBH_HID_OUTPUT_REPORT: Send an output report (default).
*                    * USBH_HID_FEATURE_REPORT: Send a feature report.
*
*  Return value
*    == USBH_STATUS_SUCCESS        : Success.
*    == USBH_STATUS_PENDING        : Request was submitted and application is informed via callback.
*    Any other value means error.
*/
USBH_STATUS USBH_HID_SetReportEx(USBH_HID_HANDLE hDevice, const U8 * pBuffer, U32 BufferSize, USBH_HID_USER_FUNC * pfFunc, USBH_HID_RW_CONTEXT * pRWContext, unsigned Flags) {
  return _SetReport(hDevice, pBuffer, BufferSize, pfFunc, pRWContext, Flags);
}

/*********************************************************************
*
*       USBH_HID__GetReportCtrl
*
*  Function description
*    Reads a report from a HID device via control request.
*/
USBH_STATUS USBH_HID__GetReportCtrl(USBH_HID_INST * pInst, U8 ReportID, unsigned Flags, U8 * pBuffer, U32 Length, U32 * pNumBytesRead) {
  USBH_STATUS   Status;
  USBH_URB    * pURB;
  unsigned      Value;

  USBH_OS_Lock(USBH_MUTEX_HID);
  if (pInst->Control.InUse != 0) {
    Status = USBH_STATUS_BUSY;
  } else {
    Status = USBH_STATUS_SUCCESS;
    pInst->Control.InUse = 1;
  }
  USBH_OS_Unlock(USBH_MUTEX_HID);
  *pNumBytesRead = 0;
  if (Status == USBH_STATUS_SUCCESS) {
    pURB = &pInst->Control.Urb;
    pURB->Header.Function                      = USBH_FUNCTION_CONTROL_REQUEST;
    pURB->Request.ControlRequest.Setup.Type    = USB_TO_HOST | USB_REQTYPE_CLASS | USB_INTERFACE_RECIPIENT;
    pURB->Request.ControlRequest.Setup.Request = 0x01u;
    Value = 0x0100uL | ReportID;
    if ((Flags & USBH_HID_FEATURE_REPORT) != 0u) {
      Value |= 0x0300u;
    }
    pURB->Request.ControlRequest.Setup.Value   = Value;
    pURB->Request.ControlRequest.pBuffer       = (U8 *)pBuffer;                  //lint !e9005 D:105[a]
    pURB->Request.ControlRequest.Setup.Index   = pInst->DevInterfaceID;
    pURB->Request.ControlRequest.Setup.Length  = Length;
    EP_INC_REF_CNT(&pInst->Control);
    pURB->Header.pContext           = pInst;
    pURB->Header.pfOnCompletion     = _OnOutCompletion;
    pURB->Header.pfOnUserCompletion = NULL;
    Status = INC_REF_CNT(pInst);
    if (Status == USBH_STATUS_SUCCESS) {
      USBH_OS_ResetEvent(pInst->Control.pEvent);
      Status = USBH_SubmitUrb(pInst->hInterface, pURB);
      if (Status != USBH_STATUS_PENDING) {
        DEC_REF_CNT(pInst);
      }
    }
    if (Status != USBH_STATUS_PENDING) {
      pInst->Control.InUse = 0;
      EP_DEC_REF_CNT(&pInst->Control);
    } else {
      if (USBH_OS_WaitEventTimed(pInst->Control.pEvent, USBH_HID_Global.ControlWriteTimeout) != USBH_OS_EVENT_SIGNALED) {
        USBH_WARN((USBH_MCAT_HID_URB, "_GetReportEP0: Operation timed out"));
        Status = _AbortEP(&pInst->Control);
        if (Status != USBH_STATUS_SUCCESS) {
          USBH_WARN((USBH_MCAT_HID_URB, "_GetReportEP0: _AbortEP failed: 0x%08x", Status));
        }
        USBH_OS_WaitEvent(pInst->Control.pEvent);
      } else {
        Status = pURB->Header.Status;
        *pNumBytesRead = pURB->Request.ControlRequest.Length;
      }
    }
  }
  return Status;
}

/*********************************************************************
*
*       USBH_HID_GetReportEP0
*
*  Function description
*    Reads a report from a HID device via control request.
*
*  Parameters
*    hDevice       : Handle to an opened HID device.
*    ReportID      : ID of the report requested from the device.
*    Flags         : * USBH_HID_INPUT_REPORT: Request for an input report (default).
*                    * USBH_HID_FEATURE_REPORT: Request for a feature report.
*    pBuffer       : Pointer to a buffer to read.
*    Length        : Requested length of the report.
*    pNumBytesRead : [O] Actual length of the report read.
*
*  Return value
*    == USBH_STATUS_SUCCESS        : Success.
*    Any other value means error.
*/
USBH_STATUS USBH_HID_GetReportEP0(USBH_HID_HANDLE hDevice, U8 ReportID, unsigned Flags, U8 * pBuffer, U32 Length, U32 * pNumBytesRead) {
  USBH_HID_INST * pInst;
  USBH_STATUS     Status;

  pInst = _h2p(hDevice);
  if (pInst == NULL) {
    Status = USBH_STATUS_INVALID_PARAM;
  } else {
    if (pInst->IsOpened != 0) {
      Status = USBH_HID__GetReportCtrl(pInst, ReportID, Flags, pBuffer, Length, pNumBytesRead);
    } else {
      Status = USBH_STATUS_NOT_OPENED;
    }
  }
  return Status;
}

/*********************************************************************
*
*       USBH_HID_CancelIo
*
*  Function description
*    Cancels any pending read/write operation.
*
*  Parameters
*    hDevice : Handle to the HID device.
*
*  Return value
*    USBH_STATUS_SUCCESS : Operation successfully canceled.
*    Any other value means error.
*/
USBH_STATUS USBH_HID_CancelIo(USBH_HID_HANDLE hDevice) {
  USBH_HID_INST * pInst;

  pInst = _h2p(hDevice);
  if (pInst == NULL) {
    return USBH_STATUS_INVALID_PARAM;
  }
  if (pInst->IsOpened == 0) {
    return USBH_STATUS_NOT_OPENED;
  }
  _CancelIO(pInst);
  return USBH_STATUS_SUCCESS;
}

/*********************************************************************
*
*       USBH_HID_Close
*
*  Function description
*    Closes a handle to opened HID device.
*
*  Parameters
*    hDevice : Handle to the opened device.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_HID_Close(USBH_HID_HANDLE hDevice) {
  USBH_HID_INST * pInst;

  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    if (pInst->IsOpened == 0) {
      return USBH_STATUS_NOT_OPENED;
    }
    pInst->IsOpened--;
    DEC_REF_CNT(pInst);
    return USBH_STATUS_SUCCESS;
  }
  return USBH_STATUS_INVALID_HANDLE;
}

/*********************************************************************
*
*       USBH_HID_RegisterNotification
*
*  Function description
*    Obsolete function, use USBH_HID_AddNotification().
*    Registers a notification callback in order to inform user about
*    adding or removing a device.
*
*  Parameters
*    pfNotification   : Pointer to a callback function of type USBH_NOTIFICATION_FUNC
*                       the emUSB-Host calls when a HID device is attached/removed.
*    pContext         : Application specific pointer. The pointer is not dereferenced by
*                       the emUSB-Host. It is passed to the callback function.
*                       Any value the application chooses is permitted, including NULL.
*/
void USBH_HID_RegisterNotification(USBH_NOTIFICATION_FUNC * pfNotification, void * pContext) {
  USBH_HID_Global.pfOnUserNotification = pfNotification;
  USBH_HID_Global.pUserNotifyContext = pContext;
}

/*********************************************************************
*
*       USBH_HID_AddNotification
*
*  Function description
*    Adds a callback in order to be notified when a device is added or removed.
*
*  Parameters
*    pHook           : Pointer to a user provided USBH_NOTIFICATION_HOOK structure, which is initialized and used
*                      by this function. The memory area must be valid, until the notification is removed.
*    pfNotification  : Pointer to a function the stack should call when a device is connected or disconnected.
*    pContext        : Pointer to a user context that is passed to the callback function.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_HID_AddNotification(USBH_NOTIFICATION_HOOK * pHook, USBH_NOTIFICATION_FUNC * pfNotification, void * pContext) {
  return USBH__AddNotification(pHook, pfNotification, pContext, &USBH_HID_Global.pFirstNotiHook, NULL);
}

/*********************************************************************
*
*       USBH_HID_RemoveNotification
*
*  Function description
*    Removes a callback added via USBH_HID_AddNotification.
*
*  Parameters
*    pHook          : Pointer to a user provided USBH_NOTIFICATION_HOOK variable.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_HID_RemoveNotification(const USBH_NOTIFICATION_HOOK * pHook) {
  return USBH__RemoveNotification(pHook, &USBH_HID_Global.pFirstNotiHook);
}

/*********************************************************************
*
*       USBH_HID_ConfigureControlWriteTimeout
*
*  Function description
*    Sets the time-out that shall be used during a SET_REPORT to the device.
*
*  Parameters
*    Timeout   : Time in ms the SetReport shall wait.
*/
void USBH_HID_ConfigureControlWriteTimeout(U32 Timeout) {
  USBH_HID_Global.ControlWriteTimeout = Timeout;
}

/*********************************************************************
*
*       USBH_HID_GetInterfaceHandle
*
*  Function description
*    Return the handle to the (open) USB interface. Can be used to
*    call USBH core functions like USBH_GetStringDescriptor().
*
*  Parameters
*    hDevice      : Handle to an open device returned by USBH_HID_Open().
*
*  Return value
*    Handle to an open interface.
*/
USBH_INTERFACE_HANDLE USBH_HID_GetInterfaceHandle(USBH_HID_HANDLE hDevice) {
  USBH_HID_INST      * pInst;

  pInst = _h2p(hDevice);
  USBH_ASSERT_PTR(pInst);
  return pInst->hInterface;
}

/*********************************************************************
*
*       USBH_HID_GetIndex
*
*  Function description
*    Return an index that can be used for call to USBH_HID_Open()
*    for a given interface ID.
*
*  Parameters
*    InterfaceID:    Id of the interface.
*
*  Return value
*    >= 0: Index of the HID interface.
*    <  0: InterfaceID not found.
*/
int USBH_HID_GetIndex(USBH_INTERFACE_ID InterfaceID) {
  USBH_HID_INST      * pInst;

  pInst = USBH_HID_Global.pFirst;
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
*       USBH_HID_RegisterPlugin
*/
void USBH_HID_RegisterPlugin(USBH_HID_DETECTION_HOOK *pHook) {
  //
  //TODO check for duplicates

  USBH_DLIST_InsertTail(&USBH_HID_Global.PluginList, &pHook->ListEntry);
}

/*********************************************************************
*
*       USBH_HID_RegisterReportHandler
*/
void USBH_HID_RegisterReportHandler(const USBH_HID_INST *pInst, USBH_HID_HANDLER_HOOK *pHook) {
  USBH_DLIST_InsertTail(&pInst->HandlerList, &pHook->ListEntry);
}

/*************************** End of file ****************************/
