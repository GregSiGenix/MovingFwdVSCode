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

#ifdef USBH_HW_DWC2_C_
/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static USBH_CHECK_ADDRESS_FUNC *_pfCheckValidDMAAddress;

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _Delay
*
 */
static void _Delay(volatile unsigned NumLoops) {
  USBH_ASSERT(NumLoops > 0);
  while (--NumLoops != 0u) {
  }
}

/*********************************************************************
*
*       USBH_DWC2_CHANNEL_Open
*/
static void _DWC2_CHANNEL_Open(USBH_DWC2_INST * pInst, USBH_DWC2_CHANNEL_INFO * pChannelInfo) {
  U32                   IsEPInDir;
  U32                   IsLowSpeed;
  U32                   IntMask;
  USBH_DWC2_HCCHANNEL * pChannel;
  USBH_DWC2_EP_INFO   * pEPInfo;

  _DWC2_DisableInterrupts(pInst);
  pChannel = pChannelInfo->pHWChannel;
  pChannel->HCINT = CHANNEL_MASK;
  pEPInfo  = pChannelInfo->pEPInfo;
  IsEPInDir = (pEPInfo->EndpointAddress & 0x80uL);
  //
  // Enable channel interrupts required for this transfer.
  //
  switch (pEPInfo->EndpointType) {
  case USB_EP_TYPE_INT:
    if (IsEPInDir != 0u) {
      IntMask = CHANNEL_CHH
              | CHANNEL_DTERR
              | CHANNEL_AHBERR
              ;
    } else {
      IntMask = CHANNEL_CHH
              | CHANNEL_AHBERR
              ;
    }
    break;
  case USB_EP_TYPE_ISO:
#if USBH_SUPPORT_ISO_TRANSFER
    if (IsEPInDir != 0u) {
      IntMask = CHANNEL_TXERR
              | CHANNEL_FRMOR
              | CHANNEL_BBERR
              | CHANNEL_DTERR
              | CHANNEL_CHH
              | CHANNEL_AHBERR
              ;
    } else {
      IntMask = CHANNEL_TXERR
              | CHANNEL_FRMOR
              | CHANNEL_CHH
              | CHANNEL_AHBERR
              ;
    }
#else
    USBH_WARN((USBH_MCAT_DRIVER_EP, "_DWC2_CHANNEL_Open: Bad endpoint type"));
    IntMask = 0;
#endif
    break;
  default:    // BULK and CONTROL
    if (IsEPInDir != 0u) {
      IntMask = CHANNEL_CHH
              | CHANNEL_NAK
              | CHANNEL_DTERR
              | CHANNEL_AHBERR
              ;
    } else {
      IntMask = CHANNEL_CHH
              | CHANNEL_NAK
              | CHANNEL_NYET
              | CHANNEL_AHBERR
              ;
    }
    break;
  }
  pChannel->HCINTMSK = IntMask;
  USBH_ASSERT(pEPInfo->MaxPacketSize != 0);
#if USBH_DWC2_SUPPORT_SPLIT_TRANSACTIONS
  pChannel->HCSPLIT = 0;
  pChannelInfo->UseSplitTransactions = 0;
  //
  // Update the split control register, this is only needed when talking to a low/full speed device which is connected
  // via a high speed hub.
  //
  if (pEPInfo->Speed != USBH_HIGH_SPEED) {
    USBH_HUB_PORT * pHubPort;
    pHubPort = USBH_HUB_GetHighSpeedHub(pEPInfo->pPendingUrb->Header.pDevice->pParentPort);
    if (pHubPort != NULL) {
      pChannel->HCSPLIT = ((U32)pHubPort->HubPortNumber << 0)
                        | ((U32)pHubPort->pExtHub->pHubDevice->UsbAddress << 7)
                        | SPLIT_ENABLE             // Enable split transaction
                        | SPLIT_XACTPOS_ALL        // Send the complete packet
                        ;
      pChannelInfo->UseSplitTransactions = 1;
    }
  }
#endif
  //
  // Program the HCCHARn register with the endpoint characteristics for
  // the current transfer.
  //
  if (IsEPInDir != 0u) {
    IsEPInDir = (1uL << 15);
  }
  IsLowSpeed = (pEPInfo->Speed == USBH_LOW_SPEED) ? (1uL << 17) : 0u;
  pChannel->HCCHAR = (pEPInfo->MaxPacketSize                  <<  0)
                   | (((U32)pEPInfo->EndpointAddress & 0x0Fu) << 11)
                   |  IsEPInDir
                   |  IsLowSpeed
                   | ((U32)pEPInfo->EndpointType              << 18)
                   | (1uL                                     << 20) // MCNT: Multicount
                   | ((U32)pEPInfo->DeviceAddress             << 22)
                   ;
  _DWC2_EnableInterrupts(pInst);
}

/*********************************************************************
*
*       _OnSOF
*
*  Function description
*    Start next scheduled start split transaction (Round Robin).
*/
#if USBH_DWC2_SUPPORT_SPLIT_TRANSACTIONS
static void _OnSOF(USBH_DWC2_INST * pInst) {
  U32      ChannelMask;
  U32      Channels;
  unsigned Channel;
  USBH_DWC2_CHANNEL_INFO * pChannelInfo;

  if (pInst->StartSplitDelay != 0u) {
    //
    // Delay not yet expired. Wait for next SOF.
    //
    pInst->StartSplitDelay--;
    return;
  }
  Channels = pInst->StartChannelMask;
  if (Channels == 0u) {
    //
    // Disable SOF interrupt if not needed for some time.
    //
    if (++pInst->SOFNotUsedCount > 4u * 8u) {            // 4 ms
      pInst->SOFNotUsedCount = 0;
      pInst->pHWReg->GINTMSK &= ~START_OF_FRAME_INT;
    }
    return;
  }
  pInst->SOFNotUsedCount = 0;
  Channel                = pInst->LastChannelStarted;
  ChannelMask            = (1uL << Channel);
  //
  // Find next channel to be started.
  //
  do {
    Channel--;
    ChannelMask >>= 1;
    if (ChannelMask == 0u) {
      ChannelMask = (1uL << (DWC2_NUM_CHANNELS - 1u));
      Channel     = DWC2_NUM_CHANNELS - 1u;
    }
  } while ((Channels & ChannelMask) == 0u);
  pChannelInfo = &pInst->aChannelInfo[Channel];
  _DWC2_CHANNEL_Open(pInst, pChannelInfo);
  _DWC2_CHANNEL_StartTransfer(pInst, pChannelInfo);
  pInst->StartChannelMask &= ~ChannelMask;
  pInst->LastChannelStarted = Channel;
}
#endif

/*********************************************************************
*
*       _ScheduleSplit
*/
#if USBH_DWC2_SUPPORT_SPLIT_TRANSACTIONS
static void _ScheduleSplit(USBH_DWC2_INST * pInst, U32 Channel) {
  U32 Mask;

  pInst->StartChannelMask |= (1uL << Channel);
  Mask = pInst->pHWReg->GINTMSK;
  if ((Mask & START_OF_FRAME_INT) == 0u) {
    pInst->pHWReg->GINTSTS = START_OF_FRAME_INT;
    pInst->pHWReg->GINTMSK = Mask | START_OF_FRAME_INT;
  }
}
#endif

/*********************************************************************
*
*       _DWC2_CHANNEL_ScheduleTransfer
*/
static void _DWC2_CHANNEL_ScheduleTransfer(USBH_DWC2_INST * pInst, USBH_DWC2_CHANNEL_INFO * pChannelInfo) {
#if USBH_DWC2_SUPPORT_SPLIT_TRANSACTIONS
  if (pChannelInfo->UseSplitTransactions != 0) {
    if (pInst->StartChannelMask != 0u || pInst->StartSplitDelay != 0u) {
      USBH_OS_DisableInterrupt();
      _ScheduleSplit(pInst, pChannelInfo->Channel);
      USBH_OS_EnableInterrupt();
      return;
    }
  }
#else
  USBH_USE_PARA(pChannelInfo);
#endif
  _DWC2_CHANNEL_StartTransfer(pInst, pChannelInfo);
}

/*********************************************************************
*
*       _OnChannelRestart
*
*  Function description
*    Timer callback.
*    Restarts an Interrupt Transfer after a given timeout.
*
*  Parameters
*    pContext     : Pointer to the ChannelInfo.
*/
static void _OnChannelRestart(void * pContext) {
  USBH_DWC2_CHANNEL_INFO * pChannelInfo;
  USBH_DWC2_EP_INFO      * pEPInfo;
  USBH_DWC2_INST         * pInst;

  pChannelInfo = USBH_CTX2PTR(USBH_DWC2_CHANNEL_INFO, pContext);
  pEPInfo      = pChannelInfo->pEPInfo;
  pInst        = pEPInfo->pInst;
  pChannelInfo->TransferDone = 0;
  _DWC2_CHANNEL_Open(pInst, pChannelInfo);
  _DWC2_CHANNEL_ScheduleTransfer(pInst, pChannelInfo);
}

/*********************************************************************
*
*       _DWC2_CHANNEL_StartTransfer
*/
static void _DWC2_CHANNEL_StartTransfer(USBH_DWC2_INST * pInst, USBH_DWC2_CHANNEL_INFO * pChannelInfo) {
  unsigned NumPackets;
  unsigned MaxPacketSize;
  U32 Temp;
  USBH_DWC2_HCCHANNEL * pHwChannel;
  USBH_DWC2_EP_INFO * pEPInfo;
  U32 NumBytes2Transfer;

  pEPInfo      = pChannelInfo->pEPInfo;
  if (pEPInfo->EndpointType == USB_EP_TYPE_INT && pChannelInfo->TimerInUse == FALSE) {
    USBH_InitTimer(&pChannelInfo->IntervalTimer, _OnChannelRestart, pChannelInfo);
    pChannelInfo->TimerInUse = TRUE;
    USBH_StartTimer(&pChannelInfo->IntervalTimer, pEPInfo->IntervalTime);
    return;
  }
  MaxPacketSize = pEPInfo->MaxPacketSize;
  if (pEPInfo->EndpointType == USB_EP_TYPE_INT || pEPInfo->EndpointType == USB_EP_TYPE_ISO) {
    if ((pEPInfo->EndpointAddress & 0x80u) != 0u) {
      NumBytes2Transfer = MaxPacketSize;
    } else {
      NumBytes2Transfer = pChannelInfo->NumBytesTotal - pChannelInfo->NumBytesTransferred;
      if (NumBytes2Transfer > MaxPacketSize) {
        NumBytes2Transfer = MaxPacketSize;
      }
    }
    pChannelInfo->NumBytesPushed = NumBytes2Transfer;
    NumPackets = 1;
  } else {
    //
    // Calculate the expected number of packets for the transfer.
    //
    NumBytes2Transfer = pChannelInfo->NumBytes2Transfer;
    if (NumBytes2Transfer > 0u) {
      NumPackets = (NumBytes2Transfer + MaxPacketSize - 1u) / MaxPacketSize;
    } else {
      NumPackets = 1;
    }
    if ((pEPInfo->EndpointAddress & 0x80u) != 0u) {
      NumBytes2Transfer = NumPackets * MaxPacketSize;
    }
  }
  pChannelInfo->NumBytes2Transfer = NumBytes2Transfer;
  //
  // Clear all pending interrupts
  //
  pHwChannel = pChannelInfo->pHWChannel;
  _DWC2_DisableInterrupts(pInst);
  pHwChannel->HCINT = CHANNEL_MASK;
#if USBH_DWC2_SUPPORT_SPLIT_TRANSACTIONS
  if (pChannelInfo->UseSplitTransactions != 0) {
    //
    // With split transactions we can only transfer one packet at a time.
    //
    if (NumPackets > 1u) {
      NumPackets        = 1;
      NumBytes2Transfer = MaxPacketSize;
    }
    pChannelInfo->NumBytesPushed = NumBytes2Transfer;
    pInst->StartSplitDelay = 5;
  }
#endif
  //
  // Write the XFRSIZ, PKTCNT and DPID fields.
  //
  pHwChannel->HCTSIZ = (NumBytes2Transfer         <<  0)
                     | (NumPackets                << 19)
                     | ((U32)pEPInfo->NextDataPid << 29)
                     ;
  pHwChannel->HCDMA  = SEGGER_PTR2ADDR(pChannelInfo->pBuffer);    // lint D:103[a]
  //
  // Delay is necessary in order to allow the controller to prepare hardware
  //
  _Delay(4);
  //
  // Set the correct even/odd frame bit.
  //
  Temp = pHwChannel->HCCHAR;
  //
  // Set the correct even/odd frame bit for INT and ISO EPs.
  // If we are currently in an odd frame, then start the transfer in the next even frame.
  // And vice versa.
  //
  if ((Temp & (1uL << 18)) != 0u) { // Is EP INT or ISO?
#if USBH_DWC2_SUPPORT_SPLIT_TRANSACTIONS
    if (pChannelInfo->UseSplitTransactions != 0) {
      Temp |= HCCHAR_ODDFRM;
    } else
#endif
    {
      if ((pInst->pHWReg->HFNUM & 1u) == 0u) {
        Temp |= HCCHAR_ODDFRM;
      } else {
        Temp &= ~HCCHAR_ODDFRM;
      }
    }
  }
  //
  // Enable channel.
  //
  Temp &= ~HCCHAR_CHDIS;
  Temp |=  HCCHAR_CHENA
       |   (1uL << 20)
       ;
  pHwChannel->HCCHAR = Temp;
  _DWC2_EnableInterrupts(pInst);
}

/*********************************************************************
*
*       _DWC2_CHANNEL_Disable
*/
static void _DWC2_CHANNEL_Disable(const USBH_DWC2_CHANNEL_INFO * pChannelInfo) {
  U32  HCCharReg;
  USBH_DWC2_HCCHANNEL *pHWChannel;

  pHWChannel = pChannelInfo->pHWChannel;
  HCCharReg = pHWChannel->HCCHAR;
  //
  // Check if the channel is enabled, in this case disable the channel,
  // otherwise do nothing as the channel is already disabled.
  //
  if ((HCCharReg & HCCHAR_CHENA) == 0u) {
    return;
  }
  HCCharReg |= HCCHAR_CHDIS;
  HCCharReg &= ~HCCHAR_ODDFRM; // Remove ODDFRM, this seems to be necessary for an Abort URB
  pHWChannel->HCCHAR = HCCharReg;
  pHWChannel->HCINT  = (CHANNEL_MASK ^ CHANNEL_CHH);  // clear all interrupts, except CHH
}

/*********************************************************************
*
*       _SubmitEP0
*/
static USBH_STATUS _SubmitEP0(USBH_DWC2_INST * pInst, USBH_DWC2_EP_INFO * pEPInfo, U8 * pBuffer, U32 NumBytes2Transfer, U8 DataPid) {
  USBH_DWC2_CHANNEL_INFO * pChannelInfo;

  USBH_ASSERT((SEGGER_PTR2ADDR(pBuffer) & 3u) == 0u);         // lint D:103[b]
  pChannelInfo = _DWC2_CHANNEL_Allocate(pInst, pEPInfo);
  if (pChannelInfo != NULL) {
    pChannelInfo->NumBytes2Transfer   = NumBytes2Transfer;
#if USBH_DWC2_SUPPORT_SPLIT_TRANSACTIONS
    pChannelInfo->NumBytesTotal       = NumBytes2Transfer;
#endif
    pChannelInfo->NumBytesTransferred = 0;
    pChannelInfo->pBuffer             = pBuffer;
    pChannelInfo->EndpointAddress     = pEPInfo->EndpointAddress;
    pChannelInfo->ErrorCount          = 0;
    pChannelInfo->TransferDone        = 0;
    _DWC2_CHANNEL_Open(pInst, pChannelInfo);
    pEPInfo->NextDataPid = DataPid;
    _DWC2_CHANNEL_ScheduleTransfer(pInst, pChannelInfo);
    return USBH_STATUS_PENDING;
  }
  return USBH_STATUS_NO_CHANNEL;
}

/*********************************************************************
*
*       _OnEP0
*
*  Function description
*    This handles the generic control/setup
*/
static void _OnEP0(USBH_DWC2_INST * pInst, USBH_DWC2_CHANNEL_INFO * pChannelInfo, USBH_STATUS UrbStatus) {
  USBH_CONTROL_REQUEST * pUrbRequest;
  USBH_DWC2_EP_INFO    * pEPInfo;
  U8                     InDirFlag; // True if the data direction points at the host
  U32                    SetupDataLength;
  USBH_EP0_PHASE         OldState;
  U8                   * pBuffer;
  U32                    NumBytesInBuffer;
  int                    IsInDir;
  U8                     DataPid = 0;
  U32                    Transferred;
  int                    CompleteFlag = 0;
  USBH_STATUS            Status;

  pEPInfo = pChannelInfo->pEPInfo;
  // Update Transferred data phase length and Status
  USBH_ASSERT_PTR(pEPInfo);
  USBH_ASSERT_PTR(pEPInfo->pPendingUrb);
  if (pEPInfo->pPendingUrb == NULL) {
    pEPInfo->Phase = ES_ERROR;
    _DWC2_CHANNEL_DeAllocate(pInst, pChannelInfo);
    return;
  }
  pUrbRequest = &pEPInfo->pPendingUrb->Request.ControlRequest;
  InDirFlag   = pUrbRequest->Setup.Type & USB_TO_HOST;
  OldState    = pEPInfo->Phase;
  Transferred = pChannelInfo->NumBytesTransferred;
  //
  //  Disable the channel
  //
  pEPInfo->Channel = DWC2_INVALID_CHANNEL;
  _DWC2_CHANNEL_DeAllocate(pInst, pChannelInfo);
  //
  //  If there was an error, go into the error state.
  //
  if (UrbStatus != USBH_STATUS_SUCCESS) { // On error
    pEPInfo->Phase = ES_ERROR;
  }
  //
  //  Shall we abort, because the above layer reports us to abort.
  //
  if (pEPInfo->Aborted != 0u) {
     // Endpoint is aborted, complete aborted URB
     _DWC2_CompleteUrb(pEPInfo, USBH_STATUS_CANCELED);
     return;
  }
  pBuffer            = NULL;
  NumBytesInBuffer   = 0;
  SetupDataLength    = pUrbRequest->Setup.Length;
  if (pEPInfo->Phase == ES_SETUP && SetupDataLength == 0u) { // Check phases
    pEPInfo->Phase = ES_PROVIDE_HANDSHAKE;              // No data goto provide handshake phase
  }
  IsInDir = 1;                                      // Default value
  //
  //  Initial phase
  //
  switch (pEPInfo->Phase) {
  case ES_SETUP:
    //
    // End of pSetup and pSetup length unequal zero!
    // Enter the data phase
    //
    NumBytesInBuffer = SetupDataLength;
    pEPInfo->Phase   = ES_DATA;
    DataPid          = DATA_PID_DATA1;            // Set TD mask and PID, send the packet
    //
    // Use transfer buffer
    //
    pBuffer = pEPInfo->pBuffer;
    if (InDirFlag != 0u) {
      IsInDir = 1;
#ifdef USBH_DWC2_CACHE_LINE_SIZE
      USBH_CacheConfig.pfInvalidate(pBuffer, SetupDataLength);
#endif
    } else {
      IsInDir = 0;
      USBH_MEMCPY(pBuffer, pUrbRequest->pBuffer, SetupDataLength);
#ifdef USBH_DWC2_CACHE_LINE_SIZE
      USBH_CacheConfig.pfClean(pBuffer, SetupDataLength);
#endif
    }
    pEPInfo->UseReadBuff = IsInDir;
    break;
  case ES_DATA:
    pUrbRequest->Length = Transferred;
    if (pEPInfo->UseReadBuff != 0) {
#ifdef USBH_DWC2_CACHE_LINE_SIZE
      USBH_CacheConfig.pfInvalidate(pEPInfo->pBuffer, Transferred);
#endif
      USBH_MEMCPY(pUrbRequest->pBuffer, pEPInfo->pBuffer, Transferred);
    }
    //lint -fallthrough
    //lint -e{9090} D:102[b]
  case ES_PROVIDE_HANDSHAKE:
    DataPid = DATA_PID_DATA1;
    if (InDirFlag == 0u || 0u == SetupDataLength) {
      IsInDir = 1;
    } else {
      IsInDir = 0;
    }
    pEPInfo->Phase = ES_HANDSHAKE;
    break;
  case ES_HANDSHAKE: // End of handshake phase
    CompleteFlag    = 1;
    pEPInfo->Phase  = ES_IDLE;
    break;
  case ES_ERROR:
    if (ES_DATA == OldState) { // Last state was data phase update buffers
      pUrbRequest->Length = Transferred;
    }
    CompleteFlag = 1;
    pEPInfo->Phase = ES_IDLE;
    break;
  case ES_IDLE:
    break;
  default:
     // MISRA dummy comment
     break;
  }
  if (CompleteFlag != 0) {
    _DWC2_CompleteUrb(pEPInfo, UrbStatus);
  } else {
    //
    // Update the EP relevant data
    //
    if (IsInDir != 0) {
      pEPInfo->EndpointAddress |= 0x80u;
    } else {
      pEPInfo->EndpointAddress &= ~0x80u;
    }
    pEPInfo->NextDataPid = DataPid;
    //
    // Submit the next data packet
    //
    Status = _SubmitEP0(pInst, pEPInfo, pBuffer, NumBytesInBuffer, DataPid);
    if (Status != USBH_STATUS_PENDING) { // On error
      _DWC2_CompleteUrb(pEPInfo, Status);
    }
  }
}

/*********************************************************************
*
*       _CheckChannelError
*/
static USBH_STATUS _CheckChannelError(U32 Status,
                                      USBH_DWC2_CHANNEL_INFO * pChannelInfo,
                                      USBH_DWC2_HCCHANNEL * pHwChannel) {
  USBH_STATUS Ret = USBH_STATUS_SUCCESS;

  if ((Status & CHANNEL_DTERR) != 0u) {
    pHwChannel->HCINT = CHANNEL_DTERR;
    USBH_WARN((USBH_MCAT_DRIVER_IRQ, "_CheckChannelError: Data toogle error"));
    Ret = USBH_STATUS_DATATOGGLE;
  }
  if ((Status & CHANNEL_AHBERR) != 0u) {
    pHwChannel->HCINT = CHANNEL_AHBERR;
    USBH_WARN((USBH_MCAT_DRIVER_IRQ, "_CheckChannelError: DMA error"));
    Ret = USBH_STATUS_DMA_ERROR;
  }
  if ((Status & CHANNEL_BBERR) != 0u) {
    pHwChannel->HCINT = CHANNEL_BBERR;
    USBH_WARN((USBH_MCAT_DRIVER_IRQ, "_CheckChannelError: Babble error"));
    Ret = USBH_STATUS_DATA_OVERRUN;
  }
  if ((Status & CHANNEL_STALL) != 0u) {
    pHwChannel->HCINT = CHANNEL_STALL;
    Ret = USBH_STATUS_STALL;
  }
  if ((Status & CHANNEL_TXERR) != 0u) {
    pHwChannel->HCINT = CHANNEL_TXERR;
    Ret = USBH_STATUS_NOTRESPONDING;
  }
  if (Ret == USBH_STATUS_SUCCESS) {
    USBH_WARN((USBH_MCAT_DRIVER_IRQ, "_CheckChannelError: Unexpected interrupt status %x (HCCHAR %x)", Status, pHwChannel->HCCHAR));
    pHwChannel->HCINT = Status;
    Ret = USBH_STATUS_NOTRESPONDING;
  }
  if ((Status & CHANNEL_CHH) == 0u) {
    pChannelInfo->TransferDone = 1;
    pChannelInfo->Status       = Ret;
    _DWC2_CHANNEL_Disable(pChannelInfo);
    Ret = USBH_STATUS_SUCCESS;
  }
  return Ret;
}

/*********************************************************************
*
*       _HandleChannelEP
*/
static int _HandleChannelEP(USBH_DWC2_CHANNEL_INFO * pChannelInfo, USBH_STATUS * pUrbStatus) {
  USBH_DWC2_HCCHANNEL    * pHwChannel;
  U32                      Status;
  USBH_DWC2_EP_INFO      * pEPInfo;
  USBH_STATUS              UrbStatus;

  pHwChannel   = pChannelInfo->pHWChannel;
  Status       = pHwChannel->HCINT;
  pEPInfo      = pChannelInfo->pEPInfo;
  if ((Status & CHANNEL_CHH) != 0u) {
    pHwChannel->HCINT = CHANNEL_MASK;
    pEPInfo->NextDataPid = (U8)((pHwChannel->HCTSIZ >> 29) & 0x3u);
    if ((Status & CHANNEL_XFRC) != 0u) {
      if ((pEPInfo->EndpointAddress & 0x80u) != 0u) {
        pChannelInfo->NumBytesTransferred += pChannelInfo->NumBytes2Transfer - XFRSIZ_FROM_HCTSIZ(pHwChannel->HCTSIZ);
      } else {
        //
        //  For OUT EP's the controller does not update HCTSIZ.
        //
        pChannelInfo->NumBytesTransferred += pChannelInfo->NumBytes2Transfer;
      }
      *pUrbStatus = USBH_STATUS_SUCCESS;
      return 1;
    }
    if (pChannelInfo->TransferDone != 0u) {
      *pUrbStatus = pChannelInfo->Status;
      if ((pEPInfo->EndpointAddress & 0x80u) != 0u) {
        pChannelInfo->NumBytesTransferred += pChannelInfo->NumBytes2Transfer - XFRSIZ_FROM_HCTSIZ(pHwChannel->HCTSIZ);
      }
      return 1;
    }
  } else {
    if ((Status & (CHANNEL_NAK | CHANNEL_NYET)) != 0u) {
      pHwChannel->HCINT = (CHANNEL_NAK | CHANNEL_NYET);
      if (pEPInfo->EndpointType == USB_EP_TYPE_BULK) {
        //
        // NAK + NYET interrupts are not needed for this endpoint, so reduce interrupt load.
        //
        pHwChannel->HCINTMSK &= ~(CHANNEL_NAK | CHANNEL_NYET);
      }
      goto Done;
    }
  }
  if ((Status & pHwChannel->HCINTMSK) == 0u) {
    //
    // Sometimes the interrupt routine is called on control EPs without cause.
    // We return if no relevant interrupt bits are set.
    //
    goto Done;
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
*       _HandleChannelSplt
*/
#if USBH_DWC2_SUPPORT_SPLIT_TRANSACTIONS
static int _HandleChannelSplt(USBH_DWC2_INST * pInst, USBH_DWC2_CHANNEL_INFO * pChannelInfo, USBH_STATUS * pUrbStatus, U8 EndpointType) {
  USBH_DWC2_EP_INFO      * pEPInfo;
  USBH_DWC2_HCCHANNEL    * pHwChannel;
  USBH_STATUS              UrbStatus;
  U32                      Status;
  U32                      Split;

  pHwChannel   = pChannelInfo->pHWChannel;
  pEPInfo      = pChannelInfo->pEPInfo;
  Status       = pHwChannel->HCINT;
  if ((Status & CHANNEL_CHH) != 0u) {
    pHwChannel->HCINT = CHANNEL_CHH;
    Split = pHwChannel->HCSPLIT;
    if ((Split & SPLIT_COMPLETE) == 0u) {
      //
      // Start split transaction in progress.
      //
      if ((Status & CHANNEL_TXERR) != 0u) {
        if (pChannelInfo->ErrorCount >= 3u) {
          *pUrbStatus = USBH_STATUS_NOTRESPONDING;
          return 1;
        }
        pChannelInfo->ErrorCount++;
        //
        // Retry after next SOF.
        //
        _ScheduleSplit(pInst, pChannelInfo->Channel);
        goto Done;
      }
      pChannelInfo->ErrorCount = 0;
      if ((Status & CHANNEL_ACK) != 0u) {
        U32 Mask;

        pHwChannel->HCINT = CHANNEL_ACK;
        //
        // Complete split transaction.
        //
        pHwChannel->HCSPLIT = Split | SPLIT_COMPLETE;
        if ((pInst->pHWReg->HFNUM & 1u) == 0u && EndpointType == USB_EP_TYPE_INT) {
          //
          // We are in an even frame and have an INT transfer.
          // In this case we can enable the complete split transfer immediately,
          // which will be executed in the next (odd) frame.
          //
          pHwChannel->HCCHAR |= HCCHAR_CHENA;
        } else {
          //
          // CSPLIT is started in the next SOF interrupt.
          // For interrupt EPs: Because the 'odd-frame' bit is set in HCCHAR, the CSPLIT will be executed
          // in the frame after next (leaving one delay frame between, necessary for the HUB).
          //
          pInst->CompleteChannelMask |= (1uL << pChannelInfo->Channel);
          Mask = pInst->pHWReg->GINTMSK;
          if ((Mask & START_OF_FRAME_INT) == 0u) {
            pInst->pHWReg->GINTSTS = START_OF_FRAME_INT;
            pInst->pHWReg->GINTMSK = Mask | START_OF_FRAME_INT;
          }
        }
        pChannelInfo->NYETCount = 0;
        goto Done;
      }
      if ((Status & CHANNEL_NAK) != 0u || Status == CHANNEL_CHH) {
        pHwChannel->HCINT = CHANNEL_NAK;
        //
        // Retry after next SOF.
        //
        _ScheduleSplit(pInst, pChannelInfo->Channel);
        goto Done;
      }
    } else {
      //
      // Complete split transaction in progress.
      //
      pHwChannel->HCINT = CHANNEL_CHH;
      if ((Status & CHANNEL_TXERR) != 0u) {
        if (pChannelInfo->TransferDone != 0u) {
          *pUrbStatus = pChannelInfo->Status;
          return 1;
        }
        if (pChannelInfo->ErrorCount >= 3u) {
          *pUrbStatus = USBH_STATUS_NOTRESPONDING;
          return 1;
        }
        pChannelInfo->ErrorCount++;
        //
        // According to the USB specification the 'complete split' should be retried here:
        //    pHwChannel->HCCHAR |= HCCHAR_CHENA;
        // But this would kill the channel completely in case of a disconnect.
        // Therefore we restart the hole transaction after the next SOF.
        //
        _ScheduleSplit(pInst, pChannelInfo->Channel);
        goto Done;
      }
      if ((Status & CHANNEL_NYET) != 0u) {
        pHwChannel->HCINT = CHANNEL_NYET;
        if(++pChannelInfo->NYETCount < 3u) {
          //
          // Retry 'complete split'.
          //
          if (EndpointType == USB_EP_TYPE_INT) {
            U32 Temp;
            //
            // To execute the CSPLIT in the next frame, we toggle the 'odd-frame' bit.
            //
            Temp = pHwChannel->HCCHAR;
            Temp ^= HCCHAR_ODDFRM;         // toggle 'odd-frame'
            pHwChannel->HCCHAR = Temp | HCCHAR_CHENA;
          } else {
            pInst->CompleteChannelMask |= (1uL << pChannelInfo->Channel);
          }
          goto Done;
        }
        //
        // Too many NYETs, give up.
        //
        *pUrbStatus = USBH_STATUS_NOTRESPONDING;
        return 1;
      }
      if ((Status & CHANNEL_NAK) != 0u) {
        pHwChannel->HCINT = CHANNEL_NAK;
        if (pChannelInfo->TransferDone != 0u) {
          *pUrbStatus = pChannelInfo->Status;
          return 1;
        }
        switch (EndpointType) {
        case USB_EP_TYPE_INT:
          //
          // Retry after next interval.
          //
          USBH_StartTimer(&pChannelInfo->IntervalTimer, pEPInfo->IntervalTime);
          pChannelInfo->ErrorCount = 0;
          break;
        case USB_EP_TYPE_CONTROL:
          if (pEPInfo->Phase == ES_SETUP) {
            //
            // NAK on setup request is an error.
            //
            if (++pChannelInfo->ErrorCount > 3u) {
              *pUrbStatus = USBH_STATUS_NOTRESPONDING;
              return 1;
            }
            //
            // Retry hole split transaction.
            //
            _ScheduleSplit(pInst, pChannelInfo->Channel);
            break;
          }
          //lint -fallthrough
          //lint -e{9090} D:102[b]
        default:
          //
          // Retry hole split transaction.
          //
          pChannelInfo->ErrorCount = 0;
          _ScheduleSplit(pInst, pChannelInfo->Channel);
          break;
        }
        goto Done;
      }
      if ((Status & CHANNEL_XFRC) != 0u) {
        U32 BytesTransfered;

        pEPInfo->NextDataPid = (U8)((pHwChannel->HCTSIZ >> 29) & 0x3u);
        BytesTransfered = pChannelInfo->NumBytesPushed;
        if ((pEPInfo->EndpointAddress & 0x80u) != 0u) {
          BytesTransfered -= XFRSIZ_FROM_HCTSIZ(pHwChannel->HCTSIZ);    // For OUT EP's the controller does not update HCTSIZ.
        }
        pChannelInfo->NumBytesTransferred += BytesTransfered;
        if (BytesTransfered < pEPInfo->MaxPacketSize || pChannelInfo->NumBytesTransferred >= pChannelInfo->NumBytesTotal) {
          *pUrbStatus = USBH_STATUS_SUCCESS;
          return 1;
        }
        if (pChannelInfo->TransferDone != 0u) {
          *pUrbStatus = pChannelInfo->Status;
          return 1;
        }
        //
        // More packets to transfer -> restart channel.
        //
        pChannelInfo->NumBytes2Transfer -= BytesTransfered;
        pChannelInfo->pBuffer           += BytesTransfered;
        _DWC2_CHANNEL_Open(pInst, pChannelInfo);
        _DWC2_CHANNEL_ScheduleTransfer(pInst, pChannelInfo);
        goto Done;
      }
    }
  }
  if ((Status & (CHANNEL_NAK | CHANNEL_ACK | CHANNEL_NYET)) != 0u) {
    pHwChannel->HCINT = (CHANNEL_NAK | CHANNEL_ACK | CHANNEL_NYET);
    goto Done;
  }
  UrbStatus = _CheckChannelError(Status, pChannelInfo, pHwChannel);
  if (UrbStatus != USBH_STATUS_SUCCESS) {
    *pUrbStatus = UrbStatus;
    return 1;
  }
Done:
  return 0;
}
#endif

/*********************************************************************
*
*       _DWC2_AddUrb2EP0
*
*  Function description
*    Adds a control endpoint request.
*
*  Return value
*    USBH_STATUS_PENDING on success
*    other values are errors
*/
static USBH_STATUS _DWC2_AddUrb2EP0(USBH_DWC2_EP_INFO * pEPInfo, USBH_URB * pUrb) {
  USBH_CONTROL_REQUEST * pUrbRequest;
  USBH_STATUS            Status;
  USBH_DWC2_INST       * pInst;
  U8                   * pBuffer;
  U32                    Len;

  EP_VALID(pEPInfo);
  USBH_ASSERT(pUrb != NULL);
  pUrbRequest         = &pUrb->Request.ControlRequest;
  pUrbRequest->Length = 0;
  pEPInfo->Channel = DWC2_INVALID_CHANNEL;
  USBH_OS_Lock(USBH_MUTEX_DRIVER);
  if (pEPInfo->pPendingUrb == NULL) {
    pEPInfo->pPendingUrb = pUrb;
    Status = USBH_STATUS_SUCCESS;
  } else {
    Status = USBH_STATUS_BUSY;
  }
  USBH_OS_Unlock(USBH_MUTEX_DRIVER);
  if (Status == USBH_STATUS_SUCCESS) {
    pInst = pEPInfo->pInst;
    USBH_DWC2_IS_DEV_VALID(pInst);
    pEPInfo->EndpointAddress = 0;
    pEPInfo->Phase           = ES_SETUP;
    //
    // Use transfer buffer
    //
    Len = USBH_MAX(pUrb->Request.ControlRequest.Setup.Length, 8uL);
    if (Len > pEPInfo->BuffSize) {
      Len = ((Len + pEPInfo->MaxPacketSize - 1u) / pEPInfo->MaxPacketSize) * pEPInfo->MaxPacketSize;
#ifdef USBH_DWC2_CACHE_LINE_SIZE
      Len = (Len + USBH_DWC2_CACHE_LINE_SIZE - 1u) & ~(USBH_DWC2_CACHE_LINE_SIZE - 1u);
#endif
      if (pEPInfo->pBuffer != NULL) {
        USBH_FREE(pEPInfo->pBuffer);
        pEPInfo->BuffSize = 0;
      }
#ifdef USBH_DWC2_CACHE_LINE_SIZE
      pEPInfo->pBuffer = (U8 *)USBH_TRY_MALLOC_XFERMEM(Len, USBH_DWC2_CACHE_LINE_SIZE);
#else
      pEPInfo->pBuffer = (U8 *)USBH_TRY_MALLOC_XFERMEM(Len, 4);
#endif
      if (pEPInfo->pBuffer == NULL) {
        return USBH_STATUS_MEMORY;
      }
      pEPInfo->BuffSize = Len;
    }
    pBuffer = pEPInfo->pBuffer;
    USBH__ConvSetupPacketToBuffer(&pUrb->Request.ControlRequest.Setup, pBuffer);
#ifdef USBH_DWC2_CACHE_LINE_SIZE
    USBH_CacheConfig.pfClean(pBuffer, 8);
#endif
    Status = _SubmitEP0(pInst, pEPInfo, pBuffer, 8, DATA_PID_SETUP);
    if (Status != USBH_STATUS_PENDING) { // On error
      USBH_WARN((USBH_MCAT_DRIVER_URB, "_DWC2_AddUrb2EP0: _SubmitEP0: %s", USBH_GetStatusStr(Status)));
      pEPInfo->pPendingUrb = NULL;
    }
  }
  return Status;
}

/*********************************************************************
*
*       _DWC2_HandleEP0
*
*  Function description
*    Handles the control EP handling.
*
*  Parameters
*    pInst    : Pointer to the STM32 instance
*    Channel  : Channel which handles the control EP.
*/
static void _DWC2_HandleEP0(USBH_DWC2_INST * pInst, USBH_DWC2_CHANNEL_INFO * pChannelInfo) {
  USBH_DWC2_EP_INFO      * pEPInfo;
  USBH_STATUS              UrbStatus;
  int                      Done;

  pEPInfo      = pChannelInfo->pEPInfo;
  //
  //  If an abort was sent by upper level, cancel the transaction
  //
  if (pEPInfo->Aborted != 0u) {     // USBH_URB must be canceled
    pChannelInfo->TransferDone = 1;
    pChannelInfo->Status = USBH_STATUS_CANCELED;
  }
  //
  //  First handle the lower operation of the channel
  //
#if USBH_DWC2_SUPPORT_SPLIT_TRANSACTIONS
  if (pChannelInfo->UseSplitTransactions != 0) {
    Done = _HandleChannelSplt(pInst, pChannelInfo, &UrbStatus, USB_EP_TYPE_CONTROL);
  } else
#endif
  {
    Done = _HandleChannelEP(pChannelInfo, &UrbStatus);
  }
  if (Done != 0) {
    //
    //  When the lower part has finished its operation, handle the upper layer
    //  operation.
    //  This handles DATA and STATUS stage of the transfer.
    //
    _OnEP0(pInst, pChannelInfo, UrbStatus);
  }
}

#else

/*********************************************************************
*
*       USBH_HW_DWC2_EPControl_DMA_c
*
*  Function description
*    Dummy function to avoid problems with certain compilers which
*    can not handle empty object files.
*/
void USBH_HW_DWC2_EPControl_DMA_c(void);
void USBH_HW_DWC2_EPControl_DMA_c(void) {
}

#endif  // _USBH_HW_DWC2_EPCONTROL_C_
/*************************** End of file ****************************/
