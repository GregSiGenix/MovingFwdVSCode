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
File        : USBH_URB.c
Purpose     : USB Host transfer buffer memory pool
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
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _SubStateTimerRoutine
*
*  Function description
*    Timer routine, timer is always started
*/
static void _SubStateTimerRoutine(void * pContext) {
  URB_SUB_STATE * pSubState;
  USBH_STATUS     Status;

  USBH_ASSERT_PTR(pContext);
  pSubState = USBH_CTX2PTR(URB_SUB_STATE, pContext);
  if (pSubState->TimerCancelFlag != FALSE) {
    pSubState->TimerCancelFlag = FALSE;
    USBH_LOG((USBH_MCAT_SUBST, "_SubStateTimerRoutine: Canceled"));
    return;
  }
  USBH_LOG((USBH_MCAT_SUBST, "_SubStateTimerRoutine: State = %u", pSubState->State));
  switch (pSubState->State) {
  case USBH_SUBSTATE_IDLE:
    break;
  case USBH_SUBSTATE_TIMER:
    if (NULL != pSubState->pDevRefCnt) {
      USBH_DEC_REF(pSubState->pDevRefCnt);
    }
    pSubState->State = USBH_SUBSTATE_IDLE;
    pSubState->pfCallback(pSubState->pContext);
    break;
  case USBH_SUBSTATE_TIMERURB:
    USBH_ASSERT_PTR(pSubState->pUrb);
    USBH_LOG((USBH_MCAT_URB, "_SubStateTimerRoutine: [UID %u] timed out -> abort", pSubState->pUrb->UID));
    pSubState->State = USBH_SUBSTATE_TIMEOUT_PENDING_URB;
    Status           = USBH_AbortEndpoint(pSubState->pHostController, *pSubState->phEP);
    if (Status != USBH_STATUS_SUCCESS) {
      USBH_WARN((USBH_MCAT_SUBST, "_SubStateTimerRoutine: AbortEndpoint failed %s", USBH_GetStatusStr(Status)));
      // On error call the callback routine and set a timeout error
      pSubState->pUrb->Header.Status = USBH_STATUS_TIMEOUT;
      pSubState->pUrb                = NULL;
      pSubState->State               = USBH_SUBSTATE_IDLE;
      if (NULL != pSubState->pDevRefCnt) {
        USBH_DEC_REF(pSubState->pDevRefCnt);
      }
#if USBH_URB_QUEUE_SIZE != 0u
      USBH_RetryRequest(pSubState->pHostController);
#endif
      pSubState->pfCallback(pSubState->pContext);
    }
    break;
  case USBH_SUBSTATE_COMPLETE:
    pSubState->State = USBH_SUBSTATE_IDLE;
    pSubState->pUrb  = NULL;
    pSubState->pfCallback(pSubState->pContext);
    break;
  default:
    USBH_WARN((USBH_MCAT_SUBST, "_SubStateTimerRoutine: invalid state: %d!", pSubState->State));
    break;
  }
}

/*********************************************************************
*
*       _OnSubStateCompletion
*
*  Function description
*    USBH_URB completion routine! Called after call of USBH_URB_SubStateSubmitRequest!
*/
static void _OnSubStateCompletion(USBH_URB * pUrb) USBH_CALLBACK_USE {
  URB_SUB_STATE * pSubState;
  USBH_ASSERT_PTR(pUrb);
  pSubState = USBH_CTX2PTR(URB_SUB_STATE, pUrb->Header.pInternalContext);
  USBH_ASSERT_PTR(pSubState);
  USBH_ASSERT_PTR(pSubState->pContext);
  USBH_ASSERT_PTR(pSubState->pUrb);
  USBH_ASSERT_PTR(pSubState->pfCallback);
  USBH_LOG((USBH_MCAT_SUBST, "_OnSubStateCompletion: state:%d, [UID %u] complete, %d, Status = %s",
                             pSubState->State, pUrb->UID, pUrb->Header.Function, USBH_GetStatusStr(pUrb->Header.Status)));
  if (NULL != pSubState->pDevRefCnt) {
    USBH_DEC_REF(pSubState->pDevRefCnt);
  }
  switch (pSubState->State) {
  case USBH_SUBSTATE_IDLE:
    break;
  case USBH_SUBSTATE_TIMEOUT_PENDING_URB:
  case USBH_SUBSTATE_TIMERURB:
    //
    // pfCallback must be called within the timer context
    //
    pSubState->State = USBH_SUBSTATE_COMPLETE;
    USBH_StartTimer(&pSubState->Timer, 0);
    break;
  default:
    USBH_WARN((USBH_MCAT_SUBST, "_OnSubStateCompletion: invalid state: %", pSubState->State));
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
*       USBH_URB_SubStateInit
*
*  Function description
*    Object initialization, used for embedded objects.
*/
void USBH_URB_SubStateInit(URB_SUB_STATE * pSubState, USBH_HOST_CONTROLLER * pHostController, USBH_HC_EP_HANDLE * phEP, USBH_SUBSTATE_FUNC * pfRoutine, void * pContext) {
  USBH_LOG((USBH_MCAT_SUBST, "USBH_URB_SubStateInit"));
  USBH_ASSERT_PTR(pSubState);
  USBH_ASSERT_PTR(pfRoutine);
  USBH_ASSERT_PTR(pHostController);
  USBH_ZERO_MEMORY(pSubState, sizeof(URB_SUB_STATE));
  USBH_InitTimer(&pSubState->Timer, _SubStateTimerRoutine, pSubState);
  pSubState->pHostController = pHostController;
  pSubState->phEP            = phEP;
  pSubState->pContext        = pContext;
  pSubState->pfCallback      = pfRoutine;
}

/*********************************************************************
*
*       USBH_URB_SubStateExit
*
*  Function description
*    Must be called if an embedded object is released
*/
void USBH_URB_SubStateExit(URB_SUB_STATE * pSubState) {
  USBH_LOG((USBH_MCAT_SUBST, "USBH_URB_SubStateExit"));
  USBH_ASSERT_PTR(pSubState);
  pSubState->TimerCancelFlag = TRUE;
  USBH_ReleaseTimer(&pSubState->Timer);
}

/*********************************************************************
*
*       USBH_URB_SubStateSubmitRequest
*
*  Function description
*    Submits a USBH_URB with timeout
*/
USBH_STATUS USBH_URB_SubStateSubmitRequest(URB_SUB_STATE * pSubState, USBH_URB * pUrb, U32 Timeout, USB_DEVICE * pDevRefCnt) {
  USBH_STATUS Status;
  USBH_LOG((USBH_MCAT_SUBST, "USBH_URB_SubStateSubmitRequest, timeout:%u",Timeout));
  USBH_ASSERT_PTR(pSubState);
  USBH_ASSERT_PTR(pUrb);
  USBH_ASSERT(NULL == pSubState->pUrb);
  USBH_ASSERT_PTR(pSubState->pHostController);
  USBH_ASSERT(*pSubState->phEP != 0);
  pUrb->Header.pfOnInternalCompletion = _OnSubStateCompletion;
  pUrb->Header.pInternalContext    = pSubState;
  pSubState->pUrb = pUrb;
  //
  // Check whether the device was set, else set it.
  //
  if (pUrb->Header.pDevice == NULL) {
    if (pDevRefCnt == NULL) {
      USBH_WARN((USBH_MCAT_SUBST, "pDevice was not set in the URB"));
    }
    pUrb->Header.pDevice = pDevRefCnt;
  }
  // Setup a timeout
  pSubState->TimerCancelFlag      = FALSE;
  USBH_StartTimer(&pSubState->Timer, Timeout);
  pSubState->State                = USBH_SUBSTATE_TIMERURB;
  if (NULL != pDevRefCnt) {
    pSubState->pDevRefCnt = pDevRefCnt;
    Status = USBH_INC_REF(pDevRefCnt);
  } else {
    pSubState->pDevRefCnt = NULL;
    Status = USBH_STATUS_SUCCESS;
  }
  if (Status == USBH_STATUS_SUCCESS) {
#if USBH_DEBUG > 1
    USBH_OS_Lock(USBH_MUTEX_DEVICE);
    pUrb->UID = ++USBH_Global.URBUniqueID;
    USBH_OS_Unlock(USBH_MUTEX_DEVICE);
#endif
    USBH_LOG((USBH_MCAT_URB, "[UID %u] Submit Ctrl", pUrb->UID));
    Status = USBH_SubmitRequest(pSubState->pHostController, *pSubState->phEP, pUrb);
  }
  if (Status != USBH_STATUS_PENDING) {
    USBH_WARN((USBH_MCAT_SUBST, "USBH_URB_SubStateSubmitRequest: SubmitRequest failed %s", USBH_GetStatusStr(Status)));
    // Cancel the timer and return
    pSubState->State           = USBH_SUBSTATE_IDLE;
    pSubState->TimerCancelFlag = TRUE;
    USBH_CancelTimer(&pSubState->Timer);
    pSubState->pUrb             = NULL;
    if (NULL != pDevRefCnt) {
      USBH_DEC_REF(pDevRefCnt);
    }
  }
  return Status;
}

/*********************************************************************
*
*       USBH_URB_SubStateWait
*
*  Function description
*    Starts an timer an wait for completion
*/
void USBH_URB_SubStateWait(URB_SUB_STATE * pSubState, U32 Timeout, USB_DEVICE * pDevRefCnt) {
  USBH_LOG((USBH_MCAT_SUBST, "USBH_URB_SubStateWait timeout:%u", Timeout));
  USBH_ASSERT_PTR(pSubState);
  if (NULL != pDevRefCnt) {
    if (USBH_INC_REF(pDevRefCnt) != USBH_STATUS_SUCCESS) {
      pDevRefCnt = NULL;
    }
  }
  pSubState->pDevRefCnt = pDevRefCnt;
  pSubState->State = USBH_SUBSTATE_TIMER;
  // Wait for timeout
  pSubState->TimerCancelFlag = FALSE;
  USBH_StartTimer(&pSubState->Timer, Timeout);
}

/*********************************************************************
*
*       USBH_SubmitRequest
*
*  Function description
*    Use URB queue when submitting a request to the driver.
*/
#if USBH_URB_QUEUE_SIZE != 0u
USBH_STATUS USBH_SubmitRequest(USBH_HOST_CONTROLLER * pHost, USBH_HC_EP_HANDLE hEndPoint, USBH_URB * pUrb) {
  USBH_STATUS           Status;
  USBH_URB_QUEUE_ENTRY *p;

  Status = pHost->pDriver->pfSubmitRequest(hEndPoint, pUrb);
  if (Status == USBH_STATUS_NO_CHANNEL && pUrb->Header.Function != USBH_FUNCTION_INT_REQUEST) {
    USBH_OS_Lock(USBH_MUTEX_DEVICE);
    if (pHost->NumQueueItems < USBH_URB_QUEUE_SIZE) {
      p = &pHost->UrbQueue[(pHost->FirstQueueItem + pHost->NumQueueItems) % USBH_URB_QUEUE_SIZE];
      p->pUrb = pUrb;
      p->hEndPoint = hEndPoint;
      pHost->NumQueueItems++;
      Status = USBH_STATUS_PENDING;
    }
    USBH_OS_Unlock(USBH_MUTEX_DEVICE);
    if (Status == USBH_STATUS_PENDING) {
      USBH_LOG((USBH_MCAT_URB_QUEUE, "URB queued for %p", hEndPoint));
    }
    USBH_StartTimer(&pHost->QueueRetryTimer, USBH_URB_QUEUE_RETRY_INTV);
  }
  return Status;
}

/*********************************************************************
*
*       USBH_AbortEndpoint
*
*  Function description
*    Use URB queue when aborting a request.
*/
USBH_STATUS USBH_AbortEndpoint(USBH_HOST_CONTROLLER * pHost, USBH_HC_EP_HANDLE hEndPoint) {
  unsigned               i;
  USBH_URB             * pUrb;
  USBH_URB_QUEUE_ENTRY * p;

  if (pHost->NumQueueItems != 0u) {
    p = pHost->UrbQueue;
    USBH_OS_Lock(USBH_MUTEX_DEVICE);
    for (i = 0; i < USBH_URB_QUEUE_SIZE; i++) {
      if (p->hEndPoint == hEndPoint) {
        p->hEndPoint = NULL;
        pUrb = p->pUrb;
        USBH_OS_Unlock(USBH_MUTEX_DEVICE);
        pUrb->Header.Status = USBH_STATUS_CANCELED;
        pUrb->Header.pfOnInternalCompletion(pUrb);
        return USBH_STATUS_SUCCESS;
      }
      p++;
    }
    USBH_OS_Unlock(USBH_MUTEX_DEVICE);
  }
  return pHost->pDriver->pfAbortEndpoint(hEndPoint);
}

/*********************************************************************
*
*       USBH_RetryRequest
*
*  Function description
*    Retry queued requests.
*/
void USBH_RetryRequest(USBH_HOST_CONTROLLER * pHost) {
  USBH_URB             * pUrb;
  USBH_HC_EP_HANDLE      hEndPoint;
  USBH_STATUS            Status;
  USBH_URB_QUEUE_ENTRY * p;

  USBH_OS_Lock(USBH_MUTEX_DEVICE);
  do {
    if (pHost->NumQueueItems == 0u) {
      USBH_OS_Unlock(USBH_MUTEX_DEVICE);
      return;
    }
    p = &pHost->UrbQueue[pHost->FirstQueueItem];
    pHost->FirstQueueItem = (pHost->FirstQueueItem + 1u) % USBH_URB_QUEUE_SIZE;
    pHost->NumQueueItems--;
  } while(p->hEndPoint == NULL);
  pUrb = p->pUrb;
  hEndPoint = p->hEndPoint;
  p->hEndPoint = NULL;
  USBH_OS_Unlock(USBH_MUTEX_DEVICE);
  //
  // Retry URB
  //
  USBH_LOG((USBH_MCAT_URB_QUEUE, "Retry queued URB for %p", hEndPoint));
  Status = USBH_SubmitRequest(pHost, hEndPoint, pUrb);
  if (Status != USBH_STATUS_PENDING) {
    pUrb->Header.Status = Status;
    pUrb->Header.pfOnInternalCompletion(pUrb);
  }
  USBH_StartTimer(&pHost->QueueRetryTimer, USBH_URB_QUEUE_RETRY_INTV);
}

/*********************************************************************
*
*       USBH_RetryRequestTmr
*
*  Function description
*    Retry queued requests (timer entry).
*/
void USBH_RetryRequestTmr(void * pContext) {
  USBH_HOST_CONTROLLER * pHost;

  pHost = USBH_CTX2PTR(USBH_HOST_CONTROLLER, pContext);
  USBH_ASSERT_MAGIC(pHost, USBH_HOST_CONTROLLER);
  USBH_RetryRequest(pHost);
}

/*********************************************************************
*
*       USBH_RetryRequestIntf
*
*  Function description
*    Retry queued requests (from interface context).
*/
void USBH_RetryRequestIntf(USBH_INTERFACE_HANDLE hInterface) {
  USB_INTERFACE * pInterface;

  pInterface = hInterface;
  USBH_ASSERT_MAGIC(pInterface, USB_INTERFACE);
  USBH_RetryRequest(pInterface->pDevice->pHostController);
}

#endif  /* USBH_URB_QUEUE_SIZE */

/*************************** End of file ****************************/
