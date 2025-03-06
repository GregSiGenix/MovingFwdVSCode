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
File        : USBH_HW_DWC2_BulkIntIso_DMA.c
Purpose     : USB host implementation
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#ifdef USBH_HW_DWC2_C_


/*********************************************************************
*
*       _HandleChannelEPInt
*/
static int _HandleChannelEPInt(USBH_DWC2_INST * pInst, USBH_DWC2_CHANNEL_INFO * pChannelInfo, USBH_STATUS * pUrbStatus) {
  USBH_DWC2_HCCHANNEL    * pHwChannel;
  U32                      Status;
  USBH_DWC2_EP_INFO      * pEPInfo;
  USBH_STATUS              UrbStatus;
  U32                      NumBytesTransferred;

  pHwChannel   = pChannelInfo->pHWChannel;
  Status       = pHwChannel->HCINT;
  pEPInfo      = pChannelInfo->pEPInfo;
  if ((Status & CHANNEL_CHH) != 0u) {
    pHwChannel->HCINT = CHANNEL_MASK;
    pEPInfo->NextDataPid = (U8)((pHwChannel->HCTSIZ >> 29) & 0x3u);
    if ((Status & CHANNEL_XFRC) != 0u) {
      if ((pEPInfo->EndpointAddress & 0x80u) != 0u) {
        NumBytesTransferred = pChannelInfo->NumBytesPushed - XFRSIZ_FROM_HCTSIZ(pHwChannel->HCTSIZ);
      } else {
        //
        //  For OUT EP's the controller does not update HCTSIZ.
        //
        NumBytesTransferred = pChannelInfo->NumBytesPushed;
      }
      pChannelInfo->NumBytesTransferred += NumBytesTransferred;
      pChannelInfo->pBuffer             += NumBytesTransferred;
      if (NumBytesTransferred == pEPInfo->MaxPacketSize &&
          pChannelInfo->NumBytesTransferred < pChannelInfo->NumBytesTotal) {
        //
        // Request next packet.
        //
        _DWC2_CHANNEL_StartTransfer(pInst, pChannelInfo);
        goto Done;
      }
      *pUrbStatus = USBH_STATUS_SUCCESS;
      return 1;
    }
    if (pChannelInfo->TransferDone != 0u) {
      *pUrbStatus = pChannelInfo->Status;
      return 1;
    }
    if ((Status & (CHANNEL_NAK | CHANNEL_NYET | CHANNEL_FRMOR)) != 0u) {
      USBH_StartTimer(&pChannelInfo->IntervalTimer, pChannelInfo->pEPInfo->IntervalTime);
      goto Done;
    }
  }
  UrbStatus = _CheckChannelError(Status, pChannelInfo, pHwChannel);
  if (UrbStatus != USBH_STATUS_SUCCESS) {
    *pUrbStatus = UrbStatus;
    pHwChannel->HCINT = CHANNEL_MASK;
    return 1;
  }
Done:
  return 0;
}

/*********************************************************************
*
*       _DWC2_HandleEPx
*/
static void _DWC2_HandleEPx(USBH_DWC2_INST * pInst, USBH_DWC2_CHANNEL_INFO * pChannelInfo) {
  USBH_DWC2_EP_INFO      * pEPInfo;
  USBH_URB               * pUrb;
  USBH_STATUS              UrbStatus;
  int                      Done;

  pEPInfo      = pChannelInfo->pEPInfo;
  if (pEPInfo->Aborted != 0u) {     // USBH_URB must be canceled
    pChannelInfo->TransferDone = 1;
    pChannelInfo->Status = USBH_STATUS_CANCELED;
  }
#if USBH_DWC2_SUPPORT_SPLIT_TRANSACTIONS
  if (pChannelInfo->UseSplitTransactions != 0) {
    Done = _HandleChannelSplt(pInst, pChannelInfo, &UrbStatus, pEPInfo->EndpointType);
  } else
#endif
  {
    if (pEPInfo->EndpointType == USB_EP_TYPE_INT) {
      Done = _HandleChannelEPInt(pInst, pChannelInfo, &UrbStatus);
    } else {
      Done = _HandleChannelEP(pChannelInfo, &UrbStatus);
    }
  }
  if (Done != 0) {
    USBH_LOG((USBH_MCAT_DRIVER_URB, "_DWC2_HandleEPx: NumBytesTransfer = %d", pChannelInfo->NumBytesTransferred));
    pUrb = pEPInfo->pPendingUrb;
    pUrb->Request.BulkIntRequest.Length = pChannelInfo->NumBytesTransferred;
#ifdef USBH_DWC2_CACHE_LINE_SIZE
    if (pEPInfo->UseReadBuff != 0) {
      USBH_CacheConfig.pfInvalidate(pEPInfo->pBuffer, pChannelInfo->NumBytesTransferred);
      USBH_MEMCPY(pUrb->Request.BulkIntRequest.pBuffer, pEPInfo->pBuffer, pChannelInfo->NumBytesTransferred);
    } else {
      if ((pEPInfo->EndpointAddress & 0x80u) != 0u) {
        USBH_CacheConfig.pfInvalidate(pUrb->Request.BulkIntRequest.pBuffer, pChannelInfo->NumBytesTransferred);
      }
    }
#else
    if (pEPInfo->UseReadBuff != 0) {
      USBH_MEMCPY(pUrb->Request.BulkIntRequest.pBuffer, pEPInfo->pBuffer, pChannelInfo->NumBytesTransferred);
    }
#endif
    _DWC2_CHANNEL_DeAllocate(pInst, pChannelInfo);
    _DWC2_CompleteUrb(pEPInfo, UrbStatus);
  }
}

/*********************************************************************
*
*       _DWC2_AddUrb2EPx
*
*  Function description
*    Adds an bulk or interrupt endpoint request
*/
static USBH_STATUS _DWC2_AddUrb2EPx(USBH_DWC2_EP_INFO * pEP, USBH_URB * pUrb) {
  USBH_DWC2_CHANNEL_INFO * pChannelInfo;
  USBH_DWC2_INST         * pInst;
  U32                      NumBytes2Transfer;
  USBH_STATUS              Status;
  U8                     * pBuffer;

  EP_VALID(pEP);
  USBH_LOG((USBH_MCAT_DRIVER_URB, "_DWC2_AddUrb2EPx: pEPInfo: 0x%x!", pEP->EndpointAddress));
  pEP->Channel = DWC2_INVALID_CHANNEL;
  USBH_OS_Lock(USBH_MUTEX_DRIVER);
  if (pEP->pPendingUrb == NULL) {
    pEP->pPendingUrb = pUrb;
    Status = USBH_STATUS_SUCCESS;
  } else {
    Status = USBH_STATUS_BUSY;
  }
  USBH_OS_Unlock(USBH_MUTEX_DRIVER);
  if (Status != USBH_STATUS_SUCCESS) {
    return Status;
  }
  pInst = pEP->pInst;
  USBH_DWC2_IS_DEV_VALID(pInst);
  NumBytes2Transfer = pUrb->Request.BulkIntRequest.Length;
  if (NumBytes2Transfer > pInst->MaxTransferSize) {
    return USBH_STATUS_XFER_SIZE;
  }
  pEP->UseReadBuff = 0;
  //
  // Zerocopy DMA transfer can be done only if:
  // (1) buffer address is U32 aligned
  // (2) buffer is cache aligned (on systems with cache)
  // (3) buffer points to an area valid for DMA access (flash memory cannot be accessed on some systems)
  //
  if (
#ifdef USBH_DWC2_CACHE_LINE_SIZE
      (SEGGER_PTR2ADDR(pUrb->Request.BulkIntRequest.pBuffer) & (USBH_DWC2_CACHE_LINE_SIZE - 1u)) != 0u ||   // lint D:103[b]
      (NumBytes2Transfer & (USBH_DWC2_CACHE_LINE_SIZE - 1u)) != 0u ||
#else
      (SEGGER_PTR2ADDR(pUrb->Request.BulkIntRequest.pBuffer) & 3u) != 0u ||                                 // lint D:103[b]
#endif
      //lint --e(9007)  N:100
      (_pfCheckValidDMAAddress != NULL && _pfCheckValidDMAAddress(pUrb->Request.BulkIntRequest.pBuffer) != 0)
     ) {
    U32 BuffSize;
    //
    // User buffer is not valid, so we have to use an internal buffer for the DMA transfer.
    //
    BuffSize = ((NumBytes2Transfer + pEP->MaxPacketSize - 1u) / pEP->MaxPacketSize) * pEP->MaxPacketSize;
    if (BuffSize > pEP->BuffSize) {
      if (pEP->pBuffer != NULL) {
        USBH_FREE(pEP->pBuffer);
        pEP->BuffSize = 0;
      }
#ifdef USBH_DWC2_CACHE_LINE_SIZE
      pEP->pBuffer = (U8 *)USBH_TRY_MALLOC_XFERMEM(BuffSize, USBH_DWC2_CACHE_LINE_SIZE);
#else
      pEP->pBuffer = (U8 *)USBH_TRY_MALLOC_XFERMEM(BuffSize, 4);
#endif
      if (pEP->pBuffer == NULL) {
        pEP->pPendingUrb = NULL;
        return USBH_STATUS_MEMORY;
      }
      pEP->BuffSize = BuffSize;
    }
    pBuffer = pEP->pBuffer;
    if ((pEP->EndpointAddress & 0x80u) != 0u) {
      pEP->UseReadBuff = 1;
    } else {
      USBH_MEMCPY(pBuffer, pUrb->Request.BulkIntRequest.pBuffer, NumBytes2Transfer);
    }
  } else {
    pBuffer = USBH_U8PTR(pUrb->Request.BulkIntRequest.pBuffer);
  }
#ifdef USBH_DWC2_CACHE_LINE_SIZE
  USBH_CacheConfig.pfClean(pBuffer, NumBytes2Transfer);
#endif
  pChannelInfo = _DWC2_CHANNEL_Allocate(pInst, pEP);
  if (pChannelInfo == NULL) {
    pEP->pPendingUrb = NULL;
    return USBH_STATUS_NO_CHANNEL;
  }
  pChannelInfo->NumBytes2Transfer   = NumBytes2Transfer;
  pChannelInfo->NumBytesTotal       = NumBytes2Transfer;
  pChannelInfo->NumBytesTransferred = 0;
  pChannelInfo->ErrorCount          = 0;
  pChannelInfo->TransferDone        = 0;
  pChannelInfo->Status              = USBH_STATUS_SUCCESS;
  pChannelInfo->pBuffer             = pBuffer;
  pChannelInfo->EndpointAddress     = pEP->EndpointAddress;
  USBH_LOG((USBH_MCAT_DRIVER_URB, "_DWC2_AddUrb2EPx: Channel = %d, EPAddr = 0x%x, NumBytes2Transfer = 0x%x", pChannelInfo->Channel, pChannelInfo->EndpointAddress, NumBytes2Transfer));
  _DWC2_CHANNEL_Open(pInst, pChannelInfo);
  _DWC2_CHANNEL_ScheduleTransfer(pInst, pChannelInfo);
  return USBH_STATUS_PENDING;
}

/*********************************************************************
*
*       _DWC2_AbortURB
*
*  Function description
*    Complete all pending requests. This function returns immediately.
*    But the USBH_URB's may completed delayed, if the hardware require this.
*
*  Note: Interrupts must be disabled (USBH_OS_DisableInterrupt), when calling this function.
*/
static void _DWC2_AbortURB(USBH_DWC2_INST * pInst, USBH_DWC2_EP_INFO * pEP, U8 Channel) {
  USBH_DWC2_CHANNEL_INFO * pChannelInfo;
#if USBH_DWC2_SUPPORT_SPLIT_TRANSACTIONS != 0
  U32                      ChannelMask;
#endif

  pChannelInfo = &pInst->aChannelInfo[Channel];
#if USBH_DWC2_SUPPORT_SPLIT_TRANSACTIONS
  ChannelMask = (1uL << Channel);
  if ((pInst->StartChannelMask & ChannelMask) != 0u) {
    pInst->StartChannelMask &= ~ChannelMask;
    if (pEP->EndpointType == USB_EP_TYPE_BULK) {
      pEP->pPendingUrb->Request.BulkIntRequest.Length = pChannelInfo->NumBytesTransferred;
    }
    goto Complete;
  }
#endif
  switch (pEP->EndpointType) {
  default:   // USB_EP_TYPE_BULK + USB_EP_TYPE_CONTROL
    if (pChannelInfo->UseSplitTransactions == 0) {
      _DWC2_CHANNEL_Disable(pChannelInfo);
#if USBH_SUPPORT_HUB_CLEAR_TT_BUFFER
    } else {
      USBH_Global.pExtHubApi->pfClearTTBuffer(USBH_HUB_GetHighSpeedHub(pEP->pPendingUrb->Header.pDevice->pParentPort),
                                              pEP->EndpointAddress, pEP->DeviceAddress, pEP->EndpointType);
#endif
    }
    return;
    //lint -e{9090}  N:100
  case USB_EP_TYPE_INT:
    if (pChannelInfo->TimerInUse == FALSE || USBH_IsTimerActive(&pChannelInfo->IntervalTimer) == 0) {
      return;
    }
    USBH_CancelTimer(&pChannelInfo->IntervalTimer);
    pEP->pPendingUrb->Request.BulkIntRequest.Length = 0;
    break;
#if USBH_SUPPORT_ISO_TRANSFER
  case USB_EP_TYPE_ISO:
    if (pEP->BuffBusy != 0) {
      return;
    }
    break;
#endif
  }
Complete:
  _DWC2_CHANNEL_DeAllocate(pInst, pChannelInfo);
  USBH_OS_EnableInterrupt();
  _DWC2_CompleteUrb(pEP, USBH_STATUS_CANCELED);
  USBH_OS_DisableInterrupt();
}

/*********************************************************************
*
*       _DWC2_HandleEPIso
*/
#if USBH_SUPPORT_ISO_TRANSFER
static void _DWC2_HandleEPIso(USBH_DWC2_INST * pInst, USBH_DWC2_CHANNEL_INFO * pChannelInfo) {
  U32                      IntStatus;
  USBH_DWC2_HCCHANNEL    * pHwChannel;
  USBH_STATUS              Status;
  USBH_DWC2_EP_INFO      * pEPInfo;
  USBH_URB               * pUrb;
  U8                     * pData;

  pHwChannel   = pChannelInfo->pHWChannel;
  IntStatus    = pHwChannel->HCINT;
  IntStatus   &= pHwChannel->HCINTMSK;
  if ((IntStatus & CHANNEL_CHH) == 0u) {
    return;
  }
  pEPInfo = pChannelInfo->pEPInfo;
  pChannelInfo->Status = USBH_STATUS_SUCCESS;
  if ((IntStatus & (CHANNEL_TXERR | CHANNEL_BBERR | CHANNEL_DTERR | CHANNEL_STALL | CHANNEL_FRMOR)) != 0u) {
    pHwChannel->HCINT = (CHANNEL_TXERR | CHANNEL_BBERR | CHANNEL_DTERR | CHANNEL_STALL | CHANNEL_FRMOR);
    if ((IntStatus & CHANNEL_BBERR) != 0u) {
      pChannelInfo->Status = USBH_STATUS_DATA_OVERRUN;
    } else if ((IntStatus & CHANNEL_FRMOR) != 0u) {
      pChannelInfo->Status = USBH_STATUS_FRAME_ERROR;
    } else {
      pChannelInfo->Status = USBH_STATUS_NOTRESPONDING;
    }
  }
  pChannelInfo->NumBytesTransferred = pChannelInfo->NumBytes2Transfer;
  if ((pEPInfo->EndpointAddress & 0x80u) != 0u) {
    pChannelInfo->NumBytesTransferred -= XFRSIZ_FROM_HCTSIZ(pHwChannel->HCTSIZ);
  }
  pHwChannel->HCINT = CHANNEL_CHH | CHANNEL_XFRC;
  pUrb = pEPInfo->pPendingUrb;
  if (pUrb == NULL) {
    return;
  }
  Status = pChannelInfo->Status;
  if (Status != USBH_STATUS_SUCCESS) {
    goto End;
  }
  USBH_OS_Lock(USBH_MUTEX_DRIVER);
  if (pEPInfo->BuffWaitList[0] == 0) {
    pEPInfo->BuffWaitList[0] = pEPInfo->BuffBusy;
  } else {
    pEPInfo->BuffWaitList[1] = pEPInfo->BuffBusy;
  }
  pUrb->Header.Status             = USBH_STATUS_SUCCESS;
  pUrb->Request.IsoRequest.Status = Status;
  pUrb->Request.IsoRequest.Length = pChannelInfo->NumBytesTransferred;
  pData                           = pEPInfo->pBuffer;
  if (pEPInfo->BuffBusy == 2) {
    pData += pEPInfo->BuffSize;
  }
  pUrb->Request.IsoRequest.pData  = pData;
#ifdef USBH_DWC2_CACHE_LINE_SIZE
  if ((pEPInfo->EndpointAddress & 0x80u) != 0u) {
    USBH_CacheConfig.pfInvalidate(pData, pChannelInfo->NumBytesTransferred);
  }
#endif
  pEPInfo->BuffBusy = 0;
  if (pEPInfo->Aborted != 0u) {
    Status = USBH_STATUS_CANCELED;
  } else {
    if (pEPInfo->BuffReadyList[0] != 0) {
      _DWC2_StartISO(pInst, pEPInfo, pChannelInfo);
    }
  }
  USBH_OS_Unlock(USBH_MUTEX_DRIVER);
End:
  if (Status != USBH_STATUS_SUCCESS) {
    _DWC2_CHANNEL_DeAllocate(pInst, pChannelInfo);
    _DWC2_CompleteUrb(pEPInfo, Status);
    return;
  }
  USBH_ASSERT(pUrb->Header.pfOnInternalCompletion);
  pUrb->Header.pfOnInternalCompletion(pUrb);
}
#endif /* USBH_SUPPORT_ISO_TRANSFER */

#else
/*********************************************************************
*
*       USBH_HW_DWC2_BulkIntIso_DMA_c
*
*  Function description
*    Dummy function to avoid problems with certain compilers which
*    can not handle empty object files.
*/
void USBH_HW_DWC2_BulkIntIso_DMA_c(void);
void USBH_HW_DWC2_BulkIntIso_DMA_c(void) {
}

#endif  // _USBH_HW_DWC2_BULKINTISO_C_
/*************************** End of file ****************************/
