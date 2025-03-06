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
File        : USBH_MSC.c
Purpose     : USB MSD host implementation
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/

#include "USBH_MSC_Int.h"

#if USBH_USE_LEGACY_MSD == 0

/*********************************************************************
*
*       Defines
*
**********************************************************************
*/
#define USBH_MSD_REMOVAL_TIMEOUT    100

#define CBW_LENGTH                   31u
#define CSW_LENGTH                   13u
#define CMD_LENGTH                   10u    // Length of static Cmd data below

#define CBW_SIGNATURE        0x55534243u
#define CBW_POS_TAG                   4u
#define CBW_POS_LEN                   8u
#define CBW_POS_FLAG                 12u
#define CBW_POS_LUN                  13u
#define CBW_POS_CBLEN                14u

#define CSW_SIGNATURE        0x55534253u
#define CSW_POS_TAG                   4u
#define CSW_POS_STATUS               12u
#define CSW_STATUS_OK                 0u
#define CSW_STATUS_FAIL               1u

#define CMD_INQUIRY_RSP_LEN          36u

#define CMD_READ_CAPACITY_RSP_LEN     8u

#define CMD_REQUEST_SENSE_RSP_LEN    18u
//lint -esym(750,SENSE_DATA_POS_*)  D:109
#define SENSE_DATA_POS_SENSE_KEY       2
#define SENSE_DATA_POS_SENSE_CODE     12
#define SENSE_DATA_POS_SENSE_QUAL     13
#define SENSE_KEY_UNIT_ATTENTION   0x06u

#define CMD_MODE_SENSE_RSP_LEN        4u
#define MODE_DATA_POS_DEVICE_PARA     2u
#define MODE_FLAG_WRITE_PROTECTION 0x80u

#define CMD_READ10_OPCODE           0x28
#define CMD_WRITE10_OPCODE          0x2A

/*********************************************************************
*
*       Public data
*
**********************************************************************
*/
USBH_MSD_GLOBAL USBH_MSD_Global; // Global driver object

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/

static const U8 _InquiryCmd[CMD_LENGTH] = {
  0x06,                         // bCBWCBLength
  // Command
  0x12,                         // Operation code
  0x00,
  0x00,
  0x00,
  CMD_INQUIRY_RSP_LEN,          // Allocation Length
  0x00,                         // Control
  0, 0, 0                       // Pad
};

static const U8 _ReadCapacityCmd[CMD_LENGTH] = {
  0x0A,                         // bCBWCBLength
  // Command
  0x25,                         // Operation code
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,                         // Control
  0, 0, 0                       // Pad
};

static const U8 _RequestSenseCmd[CMD_LENGTH] = {
  0x0C,                         // bCBWCBLength
  // Command
  0x03,                         // Operation code
  0x00,
  0x00,
  0x00,
  CMD_REQUEST_SENSE_RSP_LEN,    // Allocation Length
  0x00,                         // Control
  0, 0, 0                       // Pad
};

static const U8 _ModeSenseCmd[CMD_LENGTH] = {
  0x06,                         // bCBWCBLength
  // Command
  0x1A,                         // Operation code
  0x00,
  0x3F,
  0x00,
  CMD_MODE_SENSE_RSP_LEN,       // Allocation Length
  0x00,                         // Control
  0, 0, 0                       // Pad
};

static const U8 _TestUnitReadyCmd[CMD_LENGTH] = {
  0x06,                         // bCBWCBLength
  // Command
  0x00,                         // Operation code
  0x00,
  0x00,
  0x00,
  0x00,                         // Allocation Length
  0x00,                         // Control
  0, 0, 0                       // Pad
};


/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _DeleteDevice
*
*  Function description
*    Deletes all units that are connected with the device and marks the
*    device object as unused by setting the driver handle to zero.
*/
static void _DeleteDevice(USBH_MSD_INST * pInst) {
  USBH_MSD_UNIT   * pUnit;
  unsigned          i;

  USBH_ASSERT_MAGIC(pInst, USBH_MSD_INST);
  USBH_LOG((USBH_MCAT_MSC, "_DeleteDevice"));
  if (NULL != pInst->hInterface) {
    USBH_CloseInterface(pInst->hInterface);
    pInst->hInterface = NULL;
  }
  if (NULL != pInst->pUrbEvent) {
    USBH_OS_FreeEvent(pInst->pUrbEvent);
  }
  if (NULL != pInst->pTempBuf) {
    USBH_FREE(pInst->pTempBuf);
  }
  USBH_ReleaseTimer(&pInst->AbortTimer);
  //
  // Free all associated units.
  //
  pUnit = pInst->aUnits;
  if (NULL != pUnit) {
    for (i = 0; i < pInst->UnitCnt; i++) {
      //
      // The Read-ahead cache needs to be invalidated.
      // Otherwise the cache thinks it has valid data.
      //
      if (USBH_MSD_Global.pCacheAPI != NULL) {
        USBH_MSD_Global.pCacheAPI->pfInvalidate(pUnit);
      }
      USBH_MSD_Global.apLogicalUnit[pUnit->Unit] = NULL;
      pUnit++;
    }
    USBH_FREE(pInst->aUnits);
  }
  USBH_MSD_Global.pDevices[pInst->DeviceIndex] = NULL;
  USBH_FREE(pInst);
}

/*********************************************************************
*
*       _RemovalTimer
*/
static void _RemovalTimer(void * pContext) {
  USBH_MSD_INST *pInst;

  pInst = USBH_CTX2PTR(USBH_MSD_INST, pContext);
  USBH_ASSERT_MAGIC(pInst, USBH_MSD_INST);
  USBH_OS_Lock(USBH_MUTEX_MSD);
  if (pInst->State == MSD_STATE_READY || pInst->State == MSD_STATE_DEAD) {
    USBH_ReleaseTimer(&pInst->RemovalTimer);
    _DeleteDevice(pInst);
  } else {
    USBH_StartTimer(&pInst->RemovalTimer, USBH_MSD_REMOVAL_TIMEOUT);
  }
  USBH_OS_Unlock(USBH_MUTEX_MSD);
}

/*********************************************************************
*
*       _AbortTimer
*/
static void _AbortTimer(void * pContext) {
  USBH_MSD_INST * pInst;
  USBH_URB      * pUrb;
  USBH_URB        AbortUrb;
  USBH_STATUS     Status;

  pInst = USBH_CTX2PTR(USBH_MSD_INST, pContext);
  USBH_ASSERT_MAGIC(pInst, USBH_MSD_INST);
  pUrb      = &pInst->Urb;
  USBH_LOG((USBH_MCAT_MSC_SCSI, "_AbortTimer: Timeout, now abort the URB"));
  if (pUrb->Header.Function == USBH_FUNCTION_BULK_REQUEST) {
    AbortUrb.Request.EndpointRequest.Endpoint = pUrb->Request.BulkIntRequest.Endpoint;
  } else {
    AbortUrb.Request.EndpointRequest.Endpoint = 0;
  }
  AbortUrb.Header.Function = USBH_FUNCTION_ABORT_ENDPOINT;
  Status = USBH_SubmitUrb(pInst->hInterface, &AbortUrb);
  if (Status != USBH_STATUS_SUCCESS) {
    USBH_WARN((USBH_MCAT_MSC_SCSI, "_AbortTimer: USBH_SubmitUrb: %s", USBH_GetStatusStr(Status)));
  }
}

/*********************************************************************
*
*       _MarkDeviceAsRemoved
*
*  Function description
*    If an device with the interfaceID exists the remove Flag is set
*    and the reference counter is decremented.
*/
static void _MarkDeviceAsRemoved(USBH_MSD_INST *pInst) {
  USBH_MSD_LUN_NOTIFICATION_FUNC * pfLunNotification = NULL;

  USBH_OS_Lock(USBH_MUTEX_MSD);
  if (pInst->Removed == FALSE) {
    pInst->Removed = TRUE;
    USBH_InitTimer(&pInst->RemovalTimer, _RemovalTimer, pInst);
    USBH_StartTimer(&pInst->RemovalTimer, USBH_MSD_REMOVAL_TIMEOUT);
    if (pInst->State >= MSD_STATE_READY) {
      pfLunNotification = USBH_MSD_Global.pfLunNotification;
    }
  }
  USBH_OS_Unlock(USBH_MUTEX_MSD);
  if (pfLunNotification != NULL) {
    pfLunNotification(USBH_MSD_Global.pContext, pInst->DeviceIndex, USBH_MSD_EVENT_REMOVE);
  }
}

/*********************************************************************
*
*       _SubStateCompleteA
*
*  Function description
*    Called on substate URB completion (substate calls from _ProcessInit)
*/
static void _SubStateCompleteA(USBH_URB * pUrb) USBH_CALLBACK_USE {
  USBH_MSD_INST     * pInst;

  pInst = USBH_CTX2PTR(USBH_MSD_INST, pUrb->Header.pContext);
  USBH_ASSERT_MAGIC(pInst, USBH_MSD_INST);
  //
  // Trigger main state machine
  //
  USBH_StartTimer(&pInst->StateTimer, 0);
}

/*********************************************************************
*
*       _SubStateCompleteB
*
*  Function description
*    Called on substate URB completion (substate calls from API functions)
*/
static void _SubStateCompleteB(USBH_URB * pUrb) USBH_CALLBACK_USE {
  USBH_MSD_INST     * pInst;

  pInst = USBH_CTX2PTR(USBH_MSD_INST, pUrb->Header.pContext);
  USBH_ASSERT_MAGIC(pInst, USBH_MSD_INST);
  //
  // Signal API function
  //
  USBH_OS_SetEvent(pInst->pUrbEvent);
}

/*********************************************************************
*
*       _GetLunStr
*/
#if USBH_SUPPORT_LOG != 0 || USBH_SUPPORT_WARN != 0
static const char *_GetLunStr(const USBH_MSD_INST * pInst) {
  static char Msg[] = "[Dev %%, LUN %%]";
  unsigned    Val;

  Val = pInst->DeviceIndex;
  Msg[5] = Val / 10 + '0';
  Msg[6] = Val % 10 + '0';
  Val = pInst->SubState.Lun;
  Msg[13] = Val / 10 + '0';
  Msg[14] = Val % 10 + '0';
  return Msg;
}
#endif

/*********************************************************************
*
*       _PrepareUrb
*/
static U32 _PrepareUrb(const USBH_MSD_INST * pInst, USBH_MSD_SUBSTATE * pSubState, USBH_URB * pUrb) {
  U32  Len;
  U32  Timeout;

  if (pSubState->Direction == 0) {
    //
    // Read
    //
    Len = USBH_MIN(pSubState->BytesToTransfer, pInst->MaxInTransferSize);
    //
    // If the buffer is a multiple of MaxPacketSize it can be used directly.
    // Otherwise pTempBuf is used to make sure that we have a buffer large
    // enough to hold MaxPacketSize packets.
    //
    if ((Len & ((U32)pInst->BulkMaxPktSize - 1u)) == 0u) {
      pSubState->ZeroCopy = 1;
      pUrb->Request.BulkIntRequest.pBuffer = pSubState->pData;
      Timeout = USBH_MSD_DATA_READ_TIMEOUT(Len);
    } else {
      //
      // Buffers which are not a multiple of MaxPacketSize are only used with the shorter commands.
      //
      Len = pInst->BulkMaxPktSize; // Size of pTempBuf.
      pUrb->Request.BulkIntRequest.pBuffer = pInst->pTempBuf;
      pSubState->ZeroCopy = 0;
      Timeout = USBH_MSD_COMMAND_TIMEOUT;
    }
    pUrb->Request.BulkIntRequest.Endpoint = pInst->BulkInEp;
    USBH_LOG((USBH_MCAT_MSC_SCSI, "_PrepareUrb: Bytes to read: %u", Len));
  } else {
    //
    // Write
    //
    Len = USBH_MIN(pSubState->BytesToTransfer, pInst->MaxOutTransferSize);
    pUrb->Request.BulkIntRequest.pBuffer = pSubState->pData;
    pUrb->Request.BulkIntRequest.Endpoint = pInst->BulkOutEp;
    Timeout = USBH_MSD_DATA_WRITE_TIMEOUT(Len);
    USBH_LOG((USBH_MCAT_MSC_SCSI, "_PrepareUrb: Bytes to write: %u", Len));
  }
  pUrb->Header.Function                 = USBH_FUNCTION_BULK_REQUEST;
  pUrb->Request.BulkIntRequest.Length   = Len;
  return Timeout;
}

/*********************************************************************
*
*       _ProcessSubState
*
*  Function description
*    State machine for SCSI commands.
*/
static void _ProcessSubState(USBH_MSD_INST * pInst) {
  USBH_MSD_SUBSTATE * pSubState;
  USBH_URB          * pUrb;
  U32                 Len;
  U32                 Timeout;
  U8                * pBuffer;
  U8                  Repeat;

  pUrb      = &pInst->Urb;
  pSubState = &pInst->SubState;
  USBH_LOG((USBH_MCAT_MSC_SCSI, "_ProcessSubState %s: Process state %u", _GetLunStr(pInst), pSubState->State));
  do {
    Repeat = 0;
    switch (pSubState->State) {                 //lint --e{9042,9090}  D:102[a]
    case MSD_SUBSTATE_START:
      pSubState->RequestSense = 0;
      //lint -fallthrough

    case MSD_SUBSTATE_REQUEST_SENSE:
      pBuffer = pInst->pTempBuf;
      USBH_StoreU32BE(pBuffer, CBW_SIGNATURE);
      USBH_StoreU32LE(pBuffer + CBW_POS_TAG, ++pInst->BlockWrapperTag);
      USBH_StoreU32LE(pBuffer + CBW_POS_LEN, pSubState->Length);
      pBuffer[CBW_POS_FLAG] = (pSubState->Direction == 0) ? 0x80u : 0x00u;
      pBuffer[CBW_POS_LUN] = pSubState->Lun;
      USBH_MEMCPY(pBuffer + CBW_POS_CBLEN, pSubState->pCmd, CMD_LENGTH);
      USBH_MEMSET(pBuffer + CBW_POS_CBLEN + CMD_LENGTH, 0, CBW_LENGTH - CBW_POS_CBLEN - CMD_LENGTH);
      pUrb->Header.Function                 = USBH_FUNCTION_BULK_REQUEST;
      pUrb->Request.BulkIntRequest.Endpoint = pInst->BulkOutEp;
      pUrb->Request.BulkIntRequest.pBuffer  = pBuffer;
      pUrb->Request.BulkIntRequest.Length   = CBW_LENGTH;
      pSubState->State = MSD_SUBSTATE_CMD_PHASE;
      USBH_LOG((USBH_MCAT_MSC_SCSI, "_ProcessSubState: Send CBW"));
      Timeout = USBH_MSD_CBW_WRITE_TIMEOUT;
      break;

    case MSD_SUBSTATE_CMD_PHASE:
      if (pUrb->Header.Status != USBH_STATUS_SUCCESS) {
        pSubState->Status = pUrb->Header.Status;
        goto End;
      }
      if (pSubState->Length == 0u) {
        //
        // Skip data phase
        //
        pSubState->State = MSD_SUBSTATE_READ_CSW;
        goto Retrigger;
      }
      pSubState->BytesToTransfer = pSubState->Length;
      pSubState->Length          = 0;
      Timeout = _PrepareUrb(pInst, pSubState, pUrb);
      pSubState->State = MSD_SUBSTATE_DATA_PHASE;
      break;

    case MSD_SUBSTATE_DATA_PHASE:
      if (pUrb->Header.Status != USBH_STATUS_SUCCESS) {
        if (pUrb->Header.Status != USBH_STATUS_STALL) {
          pSubState->Status = pUrb->Header.Status;
          goto End;
        }
        //
        // Handle Stall
        //
        pUrb->Header.Function                  = USBH_FUNCTION_RESET_ENDPOINT;
        pUrb->Request.EndpointRequest.Endpoint = (pSubState->Direction == 0) ? pInst->BulkInEp : pInst->BulkOutEp;
        Timeout = USBH_MSD_EP0_TIMEOUT;
        pSubState->State = MSD_SUBSTATE_RESET_PIPE;
        break;
      }
      //
      // Certain sticks can return 0 bytes in the data phase for certain commands
      // (e.g. MODE SENSE with the SanDisk cruzer 16 GB SDCZ6-016G).
      // Without the following exception for 13 bytes the code would read the ZLP
      // and then return here again to read "more" data, if this happened the code
      // would read the MIN(13, x) bytes CSW and interpret it as the data phase.
      //
      if (pUrb->Request.BulkIntRequest.Length == CSW_LENGTH &&
          USBH_LoadU32BE(pInst->pTempBuf) == CSW_SIGNATURE &&
          USBH_LoadU32LE(pInst->pTempBuf + CSW_POS_TAG) == pInst->BlockWrapperTag) {
        USBH_WARN((USBH_MCAT_MSC_SCSI, "_ProcessSubState %s: CSW inside data phase", _GetLunStr(pInst)));
        USBH_MEMCPY(pSubState->pData, pInst->pTempBuf, CSW_LENGTH);
        pSubState->State = MSD_SUBSTATE_STATUS_PHASE;
        Repeat = 1;
        break;
      }
      //
      // Regular data phase processing.
      //
      Len = USBH_MIN(pUrb->Request.BulkIntRequest.Length, pSubState->BytesToTransfer);
      if (pSubState->Direction == 0 && pSubState->ZeroCopy == 0) {
        USBH_MEMCPY(pSubState->pData, pInst->pTempBuf, Len);
      }
      pSubState->Length += Len;
      pSubState->pData  += Len;
      pSubState->BytesToTransfer -= Len;
      if (pSubState->BytesToTransfer != 0u) {
        //
        // More data to transfer.
        //
        Timeout = _PrepareUrb(pInst, pSubState, pUrb);
        break;
      }
      //lint -fallthrough

    case MSD_SUBSTATE_READ_CSW:
      //
      // Read CSW
      //
      pUrb->Header.Function                 = USBH_FUNCTION_BULK_REQUEST;
      pUrb->Request.BulkIntRequest.Endpoint = pInst->BulkInEp;
      pUrb->Request.BulkIntRequest.pBuffer  = pInst->pTempBuf;
      pUrb->Request.BulkIntRequest.Length   = pInst->BulkMaxPktSize;
      pSubState->State = MSD_SUBSTATE_STATUS_PHASE;
      Timeout = USBH_MSD_CSW_READ_TIMEOUT;
      break;

    case MSD_SUBSTATE_RESET_PIPE:
      if (pUrb->Header.Status != USBH_STATUS_SUCCESS) {
        pSubState->Status = pUrb->Header.Status;
        USBH_WARN((USBH_MCAT_MSC_SCSI, "_ProcessSubState %s: Clear Pipe: %s", _GetLunStr(pInst), USBH_GetStatusStr(pSubState->Status)));
        goto End;
      }
      pSubState->State = MSD_SUBSTATE_READ_CSW;
      goto Retrigger;

    case MSD_SUBSTATE_STATUS_PHASE:
      if (pUrb->Header.Status != USBH_STATUS_SUCCESS) {
        pSubState->Status = pUrb->Header.Status;
        USBH_WARN((USBH_MCAT_MSC_SCSI, "_ProcessSubState %s: Read CSW: %s", _GetLunStr(pInst), USBH_GetStatusStr(pSubState->Status)));
        goto End;
      }
      pBuffer = pInst->pTempBuf;
      if (pUrb->Request.BulkIntRequest.Length != CSW_LENGTH ||
          USBH_LoadU32BE(pBuffer) != CSW_SIGNATURE ||
          USBH_LoadU32LE(pBuffer + CSW_POS_TAG) != pInst->BlockWrapperTag ||
          pBuffer[CSW_POS_STATUS] > CSW_STATUS_FAIL) {
        pSubState->Status = USBH_STATUS_ERROR;
        USBH_WARN((USBH_MCAT_MSC_SCSI, "_ProcessSubState %s: got bad CSW", _GetLunStr(pInst)));
        goto End;
      }
      if (pSubState->RequestSense == 0) {
        if (pBuffer[CSW_POS_STATUS] == CSW_STATUS_OK) {
          pSubState->Status = USBH_STATUS_SUCCESS;
          goto End;
        }
        //
        // Restart state machine to request sense
        //
        pSubState->State = MSD_SUBSTATE_REQUEST_SENSE;
        pSubState->pCmd = _RequestSenseCmd;
        pSubState->pData = pSubState->Buff;
        pSubState->Length = CMD_REQUEST_SENSE_RSP_LEN;
        pSubState->Direction = 0;
        pSubState->RequestSense = 1;
        USBH_WARN((USBH_MCAT_MSC_SCSI, "_ProcessSubState %s: Command failed --> request sense", _GetLunStr(pInst)));
        goto Retrigger;
      }
      //
      // Request sense has been executed, pSubState->Buff contains sense data
      //
      pSubState->Sensekey = pSubState->Buff[SENSE_DATA_POS_SENSE_KEY];
      USBH_WARN((USBH_MCAT_MSC_SCSI, "_ProcessSubState %s: Sensekey/code/qualifier %x:%x:%x", _GetLunStr(pInst),
                                     pSubState->Sensekey,
                                     pSubState->Buff[SENSE_DATA_POS_SENSE_CODE],
                                     pSubState->Buff[SENSE_DATA_POS_SENSE_QUAL]));
      pSubState->Status = USBH_STATUS_COMMAND_FAILED;  // Always return error
      goto End;

    default:
      USBH_WARN((USBH_MCAT_MSC_SCSI, "_ProcessSubState: Unexpected state %u", pSubState->State));
      pSubState->Status = USBH_STATUS_ERROR;
      goto End;                                                  //lint -e{9077} D:102[a]
    }
  } while (Repeat == 1u);
  if (pInst->Removed != FALSE) {
    pSubState->Status = USBH_STATUS_DEVICE_REMOVED;
    goto End;
  }
  pSubState->Status = USBH_SubmitUrb(pInst->hInterface, pUrb);
  if (pSubState->Status == USBH_STATUS_PENDING) {
    USBH_StartTimer(&pInst->AbortTimer, Timeout);
    return;
  }
End:
  //
  // Stop state machine and trigger caller
  //
  pSubState->State = MSD_SUBSTATE_END;
Retrigger:
  pUrb->Header.pfOnCompletion(pUrb);
}

/*********************************************************************
*
*       _RunSubStateMachine
*
*  Function description
*    Run state machine for SCSI commands.
*/
static USBH_STATUS _RunSubStateMachine(USBH_MSD_INST * pInst) {
  USBH_MSD_SUBSTATE * pSubState;

  pInst->Urb.Header.pfOnCompletion = _SubStateCompleteB;
  pInst->Urb.Header.pContext       = pInst;
  USBH_OS_ResetEvent(pInst->pUrbEvent);
  pSubState = &pInst->SubState;
  pSubState->State = MSD_SUBSTATE_START;
  for (;;) {
    _ProcessSubState(pInst);
    if (pSubState->Status != USBH_STATUS_PENDING) {
      break;
    }
    USBH_OS_WaitEvent(pInst->pUrbEvent);
    USBH_CancelTimer(&pInst->AbortTimer);
  }
  return pSubState->Status;
}

/*********************************************************************
*
*       _InitStateComplete
*
*  Function description
*    Called on URB completion (URBs from _ProcessInit)
*/
static void _InitStateComplete(USBH_URB * pUrb) USBH_CALLBACK_USE {
  USBH_MSD_INST     * pInst;

  pInst = USBH_CTX2PTR(USBH_MSD_INST, pUrb->Header.pContext);
  USBH_ASSERT_MAGIC(pInst, USBH_MSD_INST);
  //
  // Switch to timer context
  //
  USBH_StartTimer(&pInst->StateTimer, 0);
}

/*********************************************************************
*
*       _ProcessInit
*
*  Function description
*    State machine to initialize all LUNs of a MSD device.
*/
static void _ProcessInit(void *pContext) {
  USBH_MSD_INST     * pInst;
  USBH_URB          * pUrb;
  USBH_SETUP_PACKET * pSetup;
  USBH_STATUS         Status;
  unsigned            NumLUNs;
  USBH_MSD_UNIT     * pUnit;
  unsigned            i;
  USBH_MSD_SUBSTATE * pSubState;
  U32                 Delay;

  pInst = USBH_CTX2PTR(USBH_MSD_INST, pContext);
  USBH_ASSERT_MAGIC(pInst, USBH_MSD_INST);
  USBH_CancelTimer(&pInst->AbortTimer);
  if (pInst->Removed != FALSE) {
    pInst->State = MSD_STATE_DEAD;
    goto End;
  }
  pUrb = &pInst->Urb;
  USBH_LOG((USBH_MCAT_MSC_SM, "_ProcessInit: Process state %u", pInst->State));
  switch (pInst->State) {                        //lint --e{9042,9090}  D:102[a]
  case MSD_STATE_START:
    pInst->ErrorCount = 0;

    //lint -fallthrough
  case MSD_STATE_GET_MAX_LUN_RETRY:
    //
    // Read MaxLUN
    //
    pSetup = &pUrb->Request.ControlRequest.Setup;
    pSetup->Type = USB_REQTYPE_CLASS | USB_INTERFACE_RECIPIENT | USB_IN_DIRECTION;
    pSetup->Request = BULK_ONLY_GETLUN_REQ;
    pSetup->Index = (U16)pInst->bInterfaceNumber;
    pSetup->Value = 0;
    pSetup->Length = BULK_ONLY_GETLUN_LENGTH;
    pUrb->Header.Function                 = USBH_FUNCTION_CONTROL_REQUEST;
    pUrb->Request.ControlRequest.pBuffer  = pInst->pTempBuf;
    pUrb->Header.pfOnCompletion           = _InitStateComplete;
    pUrb->Header.pContext                 = pInst;
    pInst->State = MSD_STATE_GET_MAX_LUN;
    Status = USBH_SubmitUrb(pInst->hInterface, pUrb);
    if (Status == USBH_STATUS_PENDING) {
      USBH_StartTimer(&pInst->AbortTimer, USBH_MSD_EP0_TIMEOUT);
      return;
    }
    USBH_LOG((USBH_MCAT_MSC_SM, "_ProcessInit: USBH_SubmitUrb Status: %s", USBH_GetStatusStr(Status)));
    break;

  case MSD_STATE_GET_MAX_LUN:
    if (pUrb->Header.Status == USBH_STATUS_STALL) {
      NumLUNs = 1;
    } else {
      if (pUrb->Header.Status != USBH_STATUS_SUCCESS || pUrb->Request.ControlRequest.Length != 1u) {
        USBH_WARN((USBH_MCAT_MSC_SM, "_ProcessInit: GET_MAX_LUN: %s", USBH_GetStatusStr(pUrb->Header.Status)));
        if (++pInst->ErrorCount >= 3u) {
          break;
        }
        pInst->State = MSD_STATE_GET_MAX_LUN_RETRY;
        Delay = 10;
        goto Retrigger;
      }
      NumLUNs = (unsigned)*pInst->pTempBuf + 1u;
      if (NumLUNs > USBH_MSD_MAX_UNITS) {
        NumLUNs = USBH_MSD_MAX_UNITS;
      }
    }
    pInst->NumLUNs = NumLUNs;
    pInst->aUnits = (USBH_MSD_UNIT *)USBH_TRY_MALLOC_ZEROED(NumLUNs * sizeof(USBH_MSD_UNIT));
    if (pInst->aUnits == NULL) {
      USBH_WARN((USBH_MCAT_MSC_SM, "_ProcessInit: aUnits could not be allocated."));
      break;
    }
    //lint -fallthrough

  case MSD_STATE_INIT_LUNS:
    pUnit = &pInst->aUnits[pInst->UnitCnt];
    //
    // Initialize Unit data structure
    //
    pUnit->pInst = pInst;
    pUnit->Lun   = pInst->UnitCnt;
    //
    // Start sub state machine for Inquiry command
    //
    pUrb->Header.pfOnCompletion = _SubStateCompleteA;
    pUrb->Header.pContext       = pInst;
    pInst->State = MSD_STATE_INQUIRY;
    pSubState = &pInst->SubState;
    pSubState->pCmd = _InquiryCmd;
    pSubState->pData = (U8 *)&pUnit->InquiryData;
    pSubState->Length = sizeof(INQUIRY_STANDARD_RESPONSE);
    pSubState->Direction = 0;
    pSubState->Lun = pInst->UnitCnt;
    pSubState->State = MSD_SUBSTATE_START;
    _ProcessSubState(pInst);
    return;

  case MSD_STATE_INQUIRY:
    if (pInst->SubState.Status == USBH_STATUS_PENDING) {
      //
      // Sub state machine not yet finished
      //
      _ProcessSubState(pInst);
      return;
    }
    if (pInst->SubState.Status != USBH_STATUS_SUCCESS || pInst->SubState.Length != sizeof(INQUIRY_STANDARD_RESPONSE)) {
      USBH_WARN((USBH_MCAT_MSC_SM, "_ProcessInit %s: Inquiry: %s", _GetLunStr(pInst), USBH_GetStatusStr(pInst->SubState.Status)));
      break;
    }
    pInst->ErrorCount = 0;
    pInst->ReadyWaitTimeout = USBH_TIME_CALC_EXPIRATION(USBH_MSD_MAX_READY_WAIT_TIME);
    //lint -fallthrough

  case MSD_STATE_TST_UNIT_RDY_RETRY:
    //
    // Start sub state machine for TestUnitReady command
    //
    pInst->State = MSD_STATE_TST_UNIT_RDY;
    pSubState = &pInst->SubState;
    pSubState->pCmd = _TestUnitReadyCmd;
    pSubState->pData = NULL;
    pSubState->Length = 0;
    pSubState->Direction = 1;
    pSubState->Lun = pInst->UnitCnt;
    pSubState->State = MSD_SUBSTATE_START;
    _ProcessSubState(pInst);
    return;

  case MSD_STATE_TST_UNIT_RDY:
    pSubState = &pInst->SubState;
    if (pSubState->Status == USBH_STATUS_PENDING) {
      //
      // Sub state machine not yet finished
      //
      _ProcessSubState(pInst);
      return;
    }
    if (pSubState->Status != USBH_STATUS_SUCCESS) {
      USBH_WARN((USBH_MCAT_MSC_SM, "_ProcessInit %s: TestUnitReady %s", _GetLunStr(pInst), USBH_GetStatusStr(pSubState->Status)));
      if (pSubState->Status != USBH_STATUS_COMMAND_FAILED) {
        break;
      }
      if (pSubState->Sensekey == SENSE_KEY_UNIT_ATTENTION) {
        Delay  = 100;
      } else {
        pInst->ErrorCount++;
        Delay  = 10;
      }
      if (pInst->ErrorCount < USBH_MSD_MAX_TEST_READY_RETRIES &&
          !USBH_TIME_IS_EXPIRED(pInst->ReadyWaitTimeout)) {
        //
        // Retry TestUnitReady
        //
        pInst->State = MSD_STATE_TST_UNIT_RDY_RETRY;
        goto Retrigger;
      }
      USBH_WARN((USBH_MCAT_MSC_SM, "_ProcessInit %s: TestUnitReady/ReadCapacity failed finaly, continue with MaxSectorAddress = 0",
                                        _GetLunStr(pInst)));
      pInst->State = MSD_STATE_LUN_FINISHED;
      Delay = 10;
      goto Retrigger;
    }
    //
    // Start sub state machine for ReadCapacity command
    //
    pInst->State = MSD_STATE_READ_CAPACITY;
    pSubState = &pInst->SubState;
    pSubState->pCmd = _ReadCapacityCmd;
    pSubState->pData = pSubState->Buff;
    pSubState->Length = CMD_READ_CAPACITY_RSP_LEN;
    pSubState->Direction = 0;
    pSubState->Lun = pInst->UnitCnt;
    pSubState->State = MSD_SUBSTATE_START;
    _ProcessSubState(pInst);
    return;

  case MSD_STATE_READ_CAPACITY:
    pSubState = &pInst->SubState;
    if (pSubState->Status == USBH_STATUS_PENDING) {
      //
      // Sub state machine not yet finished
      //
      _ProcessSubState(pInst);
      return;
    }
    if (pSubState->Status != USBH_STATUS_SUCCESS || pSubState->Length != CMD_READ_CAPACITY_RSP_LEN) {
      USBH_WARN((USBH_MCAT_MSC_SM, "_ProcessInit %s: ReadCapacity: %s", _GetLunStr(pInst), USBH_GetStatusStr(pSubState->Status)));
      if (pSubState->Status != USBH_STATUS_COMMAND_FAILED) {
        break;
      }
      if (++pInst->ErrorCount < USBH_MSD_MAX_TEST_READY_RETRIES) {
        //
        // Retry TestUnitReady
        //
        pInst->State = MSD_STATE_TST_UNIT_RDY_RETRY;
        Delay = 100;
        goto Retrigger;
      }
      USBH_WARN((USBH_MCAT_MSC_SM, "_ProcessInit %s: ReadCapacity failed finaly, continue with MaxSectorAddress = 0", _GetLunStr(pInst)));
    } else {
      pUnit = &pInst->aUnits[pInst->UnitCnt];
      pUnit->MaxSectorAddress = USBH_LoadU32BE(pSubState->Buff);     // Last possible block address
      pUnit->BytesPerSector   = USBH_LoadU32BE(pSubState->Buff + 4); // Number of bytes per sector
      pUnit->NextTestUnitReadyTime = USBH_TIME_CALC_EXPIRATION(USBH_MSD_TEST_UNIT_READY_DELAY);
      pUnit->NextTestUnitReadyValid = 1;
    }
    //
    // Start sub state machine for ModeSense command
    //
    pInst->State = MSD_STATE_MODE_SENSE;
    pSubState = &pInst->SubState;
    pSubState->pCmd = _ModeSenseCmd;
    pSubState->pData = pSubState->Buff;
    pSubState->Length = CMD_MODE_SENSE_RSP_LEN;
    pSubState->Direction = 0;
    pSubState->Lun = pInst->UnitCnt;
    pSubState->State = MSD_SUBSTATE_START;
    _ProcessSubState(pInst);
    return;

  case MSD_STATE_MODE_SENSE:
    pSubState = &pInst->SubState;
    if (pSubState->Status == USBH_STATUS_PENDING) {
      //
      // Sub state machine not yet finished
      //
      _ProcessSubState(pInst);
      return;
    }
    if (pSubState->Status != USBH_STATUS_COMMAND_FAILED) {
      if (pSubState->Status != USBH_STATUS_SUCCESS) {
        USBH_WARN((USBH_MCAT_MSC_SM, "_ProcessInit %s: ModeSense: %s", _GetLunStr(pInst), USBH_GetStatusStr(pSubState->Status)));
        break;
      }
      if (pSubState->Length <= MODE_DATA_POS_DEVICE_PARA) {
        USBH_WARN((USBH_MCAT_MSC_SM, "_ProcessInit %s: ModeSense: length %d invalid", _GetLunStr(pInst), pSubState->Length));
      } else {
        if ((pSubState->Buff[MODE_DATA_POS_DEVICE_PARA] & MODE_FLAG_WRITE_PROTECTION) != 0u) {
          pInst->aUnits[pInst->UnitCnt].WriteProtect = 1;
        }
      }
    }
    //lint -fallthrough

  case MSD_STATE_LUN_FINISHED:
    //
    // Find free global Unit number
    //
    pUnit = &pInst->aUnits[pInst->UnitCnt];
    USBH_OS_Lock(USBH_MUTEX_MSD);
    for (i = 0; i < USBH_MSD_MAX_UNITS; i++) {
      if (USBH_MSD_Global.apLogicalUnit[i] == NULL) {
        USBH_MSD_Global.apLogicalUnit[i] = pUnit;
        pUnit->Unit = i;
        break;
      }
    }
    USBH_OS_Unlock(USBH_MUTEX_MSD);
    if (i == USBH_MSD_MAX_UNITS) {
      USBH_WARN((USBH_MCAT_MSC_SM, "_ProcessInit: Too many LUNs (USBH_MSD_MAX_UNITS)"));
      break;
    }
    USBH_LOG((USBH_MCAT_MSC_SM, "_ProcessInit: LUN %u initialized successfully", pInst->UnitCnt));
    if (++pInst->UnitCnt < pInst->NumLUNs) {
      //
      // Process next LUN
      //
      pInst->State = MSD_STATE_INIT_LUNS;
      Delay = 1;
      goto Retrigger;
    }
    //
    // All LUNs handled, device is now initialized
    //
    pInst->State = MSD_STATE_READY;
    //
    // Call the USBH MSD notification function
    //
    if (USBH_MSD_Global.pfLunNotification != NULL) {
      USBH_MSD_Global.pfLunNotification(USBH_MSD_Global.pContext, pInst->DeviceIndex, USBH_MSD_EVENT_ADD);
    }
    goto End;

  default:
    USBH_WARN((USBH_MCAT_MSC_SM, "_ProcessInit: Unexpected state %u", pInst->State));
    break;
  }
  pInst->State = MSD_STATE_DEAD;
  if (USBH_MSD_Global.pfLunNotification != NULL) {
    USBH_MSD_Global.pfLunNotification(USBH_MSD_Global.pContext, 0xFFu, USBH_MSD_EVENT_ERROR);
  }
End:
  //
  // Stop state machine
  //
  USBH_ReleaseTimer(&pInst->StateTimer);
  return;
Retrigger:
  USBH_StartTimer(&pInst->StateTimer, Delay);
}

/*********************************************************************
*
*       _AddDevice
*
*  Function description
*    Adds a USB mass storage interface to the library and
*    start state machine for initialization.
*/
static int _AddDevice(USBH_INTERFACE_ID InterfaceID) {
  USBH_STATUS     Status;
  USBH_MSD_INST * pInst;
  unsigned        DeviceIndex;
  USBH_EP_MASK    EpMask;
  unsigned        Len;
  U8              Desc[USB_ENDPOINT_DESCRIPTOR_LENGTH];
  USBH_INTERFACE_INFO InterFaceInfo;

  for (DeviceIndex = 0; DeviceIndex < USBH_MSD_MAX_DEVICES; DeviceIndex++) {
    if (USBH_MSD_Global.pDevices[DeviceIndex] == NULL) {
      break;
    }
  }
  if (DeviceIndex == USBH_MSD_MAX_DEVICES) {
    USBH_WARN((USBH_MCAT_MSC, "_AddDevice: Too much interfaces (USBH_MSD_MAX_DEVICES)"));
    return 1;
  }
  pInst = (USBH_MSD_INST *)USBH_TRY_MALLOC_ZEROED(sizeof(USBH_MSD_INST));
  if (pInst == NULL) {
    USBH_WARN((USBH_MCAT_MSC, "_AddDevice: No memory"));
    return 1;
  }
  USBH_IFDBG(pInst->Magic = USBH_MSD_INST_MAGIC);
  pInst->InterfaceID = InterfaceID;
  Status = USBH_GetInterfaceInfo(InterfaceID, &InterFaceInfo);
  if (USBH_STATUS_SUCCESS != Status) {
    USBH_WARN((USBH_MCAT_MSC, "_AddDevice: interface info failed %s", USBH_GetStatusStr(Status)));
    goto Fail;
  }
  if (InterFaceInfo.Class != MASS_STORAGE_CLASS ||
      InterFaceInfo.SubClass != SUBCLASS_6 ||
      InterFaceInfo.Protocol != PROTOCOL_BULK_ONLY) {
    USBH_WARN((USBH_MCAT_MSC, "_AddDevice: Invalid device class/sub class/protocol: %u/%u/%u",
                              InterFaceInfo.Class, InterFaceInfo.SubClass, InterFaceInfo.Protocol));
    goto Fail;
  }
  //
  // Set the device interface ID (not the emUSB-Host interface ID) in the
  // device instance. This is especially important for GetMAXLUN as it requires
  // the correct device interface number.
  //
  pInst->bInterfaceNumber = InterFaceInfo.Interface;

  Status = USBH_OpenInterface(InterfaceID, 0, &pInst->hInterface); // Open interface exclusive
  if (Status != USBH_STATUS_SUCCESS) {
    USBH_WARN((USBH_MCAT_MSC, "_AddDevice: USBH_OpenInterface Status = %s", USBH_GetStatusStr(Status)));
    goto Fail;
  }
  // Get bulk IN endpoint
  EpMask.Mask      = USBH_EP_MASK_DIRECTION | USBH_EP_MASK_TYPE;
  EpMask.Direction = USB_TO_HOST;
  EpMask.Type      = USB_EP_TYPE_BULK;
  Len              = sizeof(Desc);
  Status           = USBH_GetEndpointDescriptor(pInst->hInterface, 0, &EpMask, Desc, &Len);
  if (Status != USBH_STATUS_SUCCESS || Len != USB_ENDPOINT_DESCRIPTOR_LENGTH) {
    USBH_WARN((USBH_MCAT_MSC, "Failed to get BulkEP In (%s)", USBH_GetStatusStr(Status)));
    goto Fail;
  }
  // Save information
  pInst->BulkMaxPktSize = USBH_LoadU16LE(&Desc[USB_EP_DESC_PACKET_SIZE_OFS]);
  pInst->BulkInEp       = Desc[USB_EP_DESC_ADDRESS_OFS];
  // Use previous mask change direction to bulk OUT
  EpMask.Direction = 0;
  Len              = sizeof(Desc);
  Status           = USBH_GetEndpointDescriptor(pInst->hInterface, 0, &EpMask, Desc, &Len);
  if (Status != USBH_STATUS_SUCCESS) {
    USBH_WARN((USBH_MCAT_MSC, "Failed to get BulkEP Out (%s)", USBH_GetStatusStr(Status)));
    goto Fail;
  }
  if (pInst->BulkMaxPktSize != USBH_LoadU16LE(&Desc[USB_EP_DESC_PACKET_SIZE_OFS])) {
    USBH_WARN((USBH_MCAT_MSC, "_AddDevice: different max.packet sizes between ep: 0x%x and ep: 0x%x", pInst->BulkInEp, Desc[USB_EP_DESC_ADDRESS_OFS]));
    goto Fail;
  }
  pInst->BulkOutEp = Desc[USB_EP_DESC_ADDRESS_OFS];
  pInst->pTempBuf = (U8 *)USBH_TRY_MALLOC(pInst->BulkMaxPktSize);
  if (NULL == pInst->pTempBuf) {
    USBH_WARN((USBH_MCAT_MSC, "_AddDevice: Could not allocate transfer buffer"));
    goto Fail;
  }
  Status = USBH_GetMaxTransferSize(pInst->hInterface, pInst->BulkOutEp, &pInst->MaxOutTransferSize);
  if (Status != USBH_STATUS_SUCCESS) {
    goto Fail;
  }
  Status = USBH_GetMaxTransferSize(pInst->hInterface, pInst->BulkInEp, &pInst->MaxInTransferSize);
  if (Status != USBH_STATUS_SUCCESS) {
    goto Fail;
  }
  pInst->pUrbEvent = USBH_OS_AllocEvent();
  if (pInst->pUrbEvent == NULL) {
    USBH_WARN((USBH_MCAT_MSC, "_AddDevice: USBH_OS_AllocEvent"));
    goto Fail;
  }
  USBH_InitTimer(&pInst->AbortTimer, _AbortTimer, pInst);
  pInst->DeviceIndex = DeviceIndex;
  USBH_MSD_Global.pDevices[DeviceIndex] = pInst;
  //
  // Trigger state machine
  //
  pInst->State = MSD_STATE_START;
  USBH_InitTimer(&pInst->StateTimer, _ProcessInit, pInst);
  USBH_StartTimer(&pInst->StateTimer, 1);
  return 0;

Fail:
  USBH_FREE(pInst);
  return 1;
}

/*********************************************************************
*
*       _OnDeviceNotify
*
*  Function description
*    Called if a USB Mass storage interface is found.
*/
static void _OnDeviceNotify(void * Context, USBH_PNP_EVENT Event, USBH_INTERFACE_ID InterfaceID) {
  unsigned        i;
  USBH_MSD_INST * pInst;

  USBH_USE_PARA(Context);
  if (Event == USBH_ADD_DEVICE) {
    USBH_LOG((USBH_MCAT_MSC, "_OnDeviceNotify: USBH_ADD_DEVICE InterfaceId: %u !",InterfaceID));
    if (_AddDevice(InterfaceID) != 0 && USBH_MSD_Global.pfLunNotification != NULL) {
      USBH_MSD_Global.pfLunNotification(USBH_MSD_Global.pContext, 0xFFu, USBH_MSD_EVENT_ERROR);
    }
  } else {
    USBH_LOG((USBH_MCAT_MSC, "_OnDeviceNotify: USBH_REMOVE_DEVICE InterfaceId: %u !",InterfaceID));
    for (i = 0; i < USBH_MSD_MAX_DEVICES; i++) {
      pInst = USBH_MSD_Global.pDevices[i];
      if (pInst != NULL && pInst->InterfaceID == InterfaceID) {
        _MarkDeviceAsRemoved(pInst);
        break;
      }
    }
    if (i == USBH_MSD_MAX_DEVICES) {
      USBH_WARN((USBH_MCAT_MSC, "_OnDeviceNotify: no device found!"));
    }
  }
}

/*********************************************************************
*
*       _FindUnit
*
*  Function description
*    Get a USBH_MSD_UNIT pointer and set device busy.
*    Calling function is responsible for setting device back to READY or DEAD.
*/
static USBH_STATUS _FindUnit(U8 Unit, USBH_MSD_UNIT **ppUnit) {
  USBH_MSD_UNIT * pUnit;
  USBH_MSD_INST * pInst;
  MSD_STATE       State;

  if (Unit >= USBH_MSD_MAX_UNITS) {
    return USBH_STATUS_INVALID_PARAM;
  }
  USBH_OS_Lock(USBH_MUTEX_MSD);
  State = MSD_STATE_DEAD;
  pUnit = USBH_MSD_Global.apLogicalUnit[Unit];
  if (pUnit != NULL) {
    pInst = pUnit->pInst;
    USBH_ASSERT_MAGIC(pInst, USBH_MSD_INST);
    if (pInst->Removed == FALSE) {
      State = pInst->State;
    }
    if (State == MSD_STATE_READY) {
      pInst->State = MSD_STATE_BUSY;
    }
  }
  USBH_OS_Unlock(USBH_MUTEX_MSD);
  if (State == MSD_STATE_READY) {
    *ppUnit = pUnit;
    return USBH_STATUS_SUCCESS;
  }
  if (State == MSD_STATE_DEAD) {
    return USBH_STATUS_DEVICE_REMOVED;
  }
  return USBH_STATUS_BUSY;
}


/*********************************************************************
*
*       _SendTestUnitReadyIfNecessary
*
*  Function description
*    This function sends out a TestUnitReady command to the MSD if
*    the time between now and the last successful command is greater
*    than USBH_MSD_TEST_UNIT_READY_DELAY.
*
*  Parameters
*    pUnit : Pointer to the storage medium structure.
*/
static USBH_STATUS _SendTestUnitReadyIfNecessary(USBH_MSD_UNIT * pUnit) {
  USBH_MSD_INST     * pInst;
  USBH_STATUS         Status;
  USBH_TIME           Now;
  USBH_MSD_SUBSTATE * pSubState;

  Now = USBH_OS_GetTime32();
  if (pUnit->NextTestUnitReadyValid != 0 && USBH_TimeDiff(Now, pUnit->NextTestUnitReadyTime) < 0) {
    return USBH_STATUS_SUCCESS;
  }
  pInst = pUnit->pInst;
  USBH_ASSERT_MAGIC(pInst, USBH_MSD_INST);
  //
  // Start sub state machine for TestUnitReady command
  //
  pSubState = &pInst->SubState;
  pSubState->pCmd = _TestUnitReadyCmd;
  pSubState->pData = NULL;
  pSubState->Length = 0;
  pSubState->Direction = 1;
  pSubState->Lun = pUnit->Lun;
  Status = _RunSubStateMachine(pInst);
  if (Status == USBH_STATUS_COMMAND_FAILED) {
    if (pSubState->Sensekey == SENSE_KEY_UNIT_ATTENTION) {
      pUnit->BytesPerSector   = 0;
      pUnit->MaxSectorAddress = 0;
      pUnit->WriteProtect     = 0;
    }
    USBH_WARN((USBH_MCAT_MSC, "_TestUnitReady %s failed", _GetLunStr(pInst)));
  }
  //
  // In case the TestUnitReady did not succeed we do not update the timeout
  // because the medium is not ready and consecutive TestUnitReady commands are necessary.
  //
  if (Status == USBH_STATUS_SUCCESS) {
    pUnit->NextTestUnitReadyTime = USBH_TimeAdd(Now, USBH_MSD_TEST_UNIT_READY_DELAY);
    pUnit->NextTestUnitReadyValid = 1;
  }
  return Status;
}

/*********************************************************************
*
*       _RdWrSectorsNoCache
*
*  Function description
*    Reads or writes sectors from / to a device.
*
*  Parameters
*    pUnit:          Pointer to a structure that contains the LUN.
*    Direction:      0=Read, 1=Write
*    Opcode:         SCSI command opcode.
*    SectorAddress:  Sector address of the first sector.
*    pData:          pData Buffer.
*    Sectors:        Number of contiguous logical blocks.
*/
static USBH_STATUS _RdWrSectorsNoCache(const USBH_MSD_UNIT * pUnit, int Direction, U8 Opcode, U32 SectorAddress, U8 * pData, U32 Sectors) {
  U32                 Len;
  USBH_STATUS         Status;
  USBH_MSD_INST     * pInst;
  USBH_MSD_SUBSTATE * pSubState;
  U8                  aCmd[CMD_LENGTH];
#if USBH_SUPPORT_LOG != 0 || USBH_SUPPORT_WARN != 0
  const char *pFkt = (Direction == 0) ? "Read" : "Write";
#endif

  USBH_LOG((USBH_MCAT_MSC_API, "%sSectorsNoCache: address: %u, sectors: %u", pFkt, SectorAddress, Sectors));
  USBH_ASSERT_PTR(pData);
  USBH_ASSERT(Sectors != 0u);
  if (SectorAddress + (U32)Sectors - 1u > pUnit->MaxSectorAddress) {
    USBH_WARN((USBH_MCAT_MSC_API, "%sSectorsNoCache %s: invalid sector address! max. address: %u, used address: %u + &u",
                                  pFkt, _GetLunStr(pUnit->pInst),
                                  pUnit->MaxSectorAddress, SectorAddress, Sectors));
    return USBH_STATUS_INVALID_PARAM;
  }
  Len = (U32)Sectors * pUnit->BytesPerSector;
  //
  // Start sub state machine for READ command
  //
  aCmd[0] = 10;
  aCmd[1] = Opcode;
  aCmd[2] = 0;
  USBH_StoreU32BE(aCmd + 3, SectorAddress);
  aCmd[7] = 0;
  USBH_StoreU16BE(aCmd + 8, Sectors);
  pInst = pUnit->pInst;
  USBH_ASSERT_MAGIC(pInst, USBH_MSD_INST);
  USBH_ASSERT(pInst->State == MSD_STATE_BUSY);
  pSubState = &pInst->SubState;
  pSubState->pCmd = aCmd;
  pSubState->pData = pData;
  pSubState->Length = Len;
  pSubState->Direction = Direction;
  pSubState->Lun = pUnit->Lun;
  Status = _RunSubStateMachine(pInst);
  if (Status == USBH_STATUS_SUCCESS && pSubState->Length != Len) {
    Status = USBH_STATUS_LENGTH;
  }
  if (Status != USBH_STATUS_SUCCESS) {
    if (Status == USBH_STATUS_COMMAND_FAILED) {
      USBH_WARN((USBH_MCAT_MSC_API, "%sSectorsNoCache %s failed, SenseKey = 0x%x", pFkt, _GetLunStr(pInst), pSubState->Sensekey));
    } else {
      USBH_WARN((USBH_MCAT_MSC_API, "%sSectorsNoCache %s failed: %s", pFkt, _GetLunStr(pInst), USBH_GetStatusStr(Status)));
    }
  }
  return Status;
}

/*********************************************************************
*
*       _ReadCapacity
*
*  Function description
*    Reads capacity and mode sense information from the device.
*/
static USBH_STATUS _ReadCapacity(USBH_MSD_UNIT * pUnit) {
  USBH_MSD_SUBSTATE * pSubState;
  USBH_STATUS         Status;
  //
  // Start sub state machine for ReadCapacity command
  //
  pSubState = &pUnit->pInst->SubState;
  pSubState->pCmd = _ReadCapacityCmd;
  pSubState->pData = pSubState->Buff;
  pSubState->Length = CMD_READ_CAPACITY_RSP_LEN;
  pSubState->Direction = 0;
  pSubState->Lun = pUnit->Lun;
  Status = _RunSubStateMachine(pUnit->pInst);
  if (Status == USBH_STATUS_SUCCESS && pSubState->Length == CMD_READ_CAPACITY_RSP_LEN) {
    pUnit->MaxSectorAddress = USBH_LoadU32BE(pSubState->Buff);     // Last possible block address
    pUnit->BytesPerSector   = USBH_LoadU32BE(pSubState->Buff + 4); // Number of bytes per sector
  }
  //
  // Start sub state machine for ModeSense command
  //
  pSubState->pCmd = _ModeSenseCmd;
  pSubState->pData = pSubState->Buff;
  pSubState->Length = CMD_MODE_SENSE_RSP_LEN;
  if (_RunSubStateMachine(pUnit->pInst) == USBH_STATUS_SUCCESS &&
      pSubState->Length > MODE_DATA_POS_DEVICE_PARA &&
      (pSubState->Buff[MODE_DATA_POS_DEVICE_PARA] & MODE_FLAG_WRITE_PROTECTION) != 0u) {
    pUnit->WriteProtect = 1;
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
*       USBH_MSD_Init
*
*  Function description
*    Initializes the USB Mass Storage Class Driver.
*
*  Parameters
*    pfLunNotification : Pointer to a function that shall be called when a new device notification is received. The function is called when a device is attached and ready or when it is removed.
*    pContext          : Pointer to a context that should be passed to pfLunNotification.
*
*  Return values
*    == 1: Success.
*    == 0: Initialization failed.
*
*  Additional information
*    Performs basic initialization of the library. Has to be called
*    before any other library function is called.
*/
int USBH_MSD_Init(USBH_MSD_LUN_NOTIFICATION_FUNC * pfLunNotification, void * pContext) {
  USBH_PNP_NOTIFICATION PnPNotify;
  USBH_INTERFACE_MASK   PnPNotifyMask;

  USBH_MEMSET(&USBH_MSD_Global, 0, sizeof(USBH_MSD_Global));
  USBH_MEMSET(&PnPNotifyMask, 0, sizeof(USBH_INTERFACE_MASK));
  PnPNotifyMask.Mask     = USBH_INFO_MASK_CLASS | USBH_INFO_MASK_PROTOCOL;
  PnPNotifyMask.Class    = MASS_STORAGE_CLASS;
  PnPNotifyMask.Protocol = PROTOCOL_BULK_ONLY;
  //
  // Set the callback and its context.
  //
  USBH_MSD_Global.pfLunNotification    = pfLunNotification;
  USBH_MSD_Global.pContext             = pContext;
  //
  // Add an plug an play notification routine
  //
  PnPNotify.pContext          = NULL;
  PnPNotify.InterfaceMask     = PnPNotifyMask;
  PnPNotify.pfPnpNotification = _OnDeviceNotify;
  USBH_MSD_Global.hPnPNotify  = USBH_RegisterPnPNotification(&PnPNotify);
  if (NULL == USBH_MSD_Global.hPnPNotify) {
    USBH_WARN((USBH_MCAT_MSC, "USBH_MSD_Init: Failed to register the MSD notification"));
    return 0;
  }
  return 1; // On success
}

/*********************************************************************
*
*       USBH_MSD_Exit
*
*  Function description
*    Releases all resources, closes all handles to the USB bus
*    driver and un-register all notification functions. Has to be called
*    if the application is closed before the USBH_Exit is called.
*/
void USBH_MSD_Exit(void) {
  USBH_MSD_INST * pInst;
  unsigned        i;

  //
  // 1. Un-register all PnP notifications of the device driver.
  // 2. Release all USBH MSD device resources and delete the device.
  //
  if (USBH_MSD_Global.hPnPNotify != NULL) {
    USBH_UnregisterPnPNotification(USBH_MSD_Global.hPnPNotify);
    USBH_MSD_Global.hPnPNotify = NULL;
  }
  for (i = 0; i < USBH_MSD_MAX_DEVICES; i++) {
    pInst = USBH_MSD_Global.pDevices[i];
    if (pInst != NULL) {
      _MarkDeviceAsRemoved(pInst);
    }
  }
}

/*********************************************************************
*
*       USBH_MSD__ReadSectorsNoCache
*
*  Function description
*    Reads sectors from a device. Internal function.
*
*  Parameters
*    pUnit:          Pointer to a structure that contains the LUN.
*    SectorAddress:  Sector address of the first sector.
*    pData:          pData Buffer.
*    Sectors:        Number of contiguous logical blocks to read.
*/
USBH_STATUS USBH_MSD__ReadSectorsNoCache(const USBH_MSD_UNIT * pUnit, U32 SectorAddress, U8 * pData, U32 Sectors) {
  return _RdWrSectorsNoCache(pUnit, 0, CMD_READ10_OPCODE, SectorAddress, pData, Sectors);
}

/*********************************************************************
*
*       USBH_MSD__WriteSectorsNoCache
*
*  Function description
*    Writes sectors to a device.
*
*  Parameters
*    pUnit:          Pointer to a structure that contains the LUN.
*    SectorAddress:  Sector address of the first sector.
*    pData:          pData Buffer.
*    Sectors:        Number of contiguous logical blocks written to the device.
*/
USBH_STATUS USBH_MSD__WriteSectorsNoCache(const USBH_MSD_UNIT * pUnit, U32 SectorAddress, const U8 * pData, U32 Sectors) {
  if (pUnit->WriteProtect != 0) {
    return USBH_STATUS_WRITE_PROTECT;
  }
  return _RdWrSectorsNoCache(pUnit, 1, CMD_WRITE10_OPCODE, SectorAddress, (U8 *)pData, Sectors);   //lint !e9005 D:105[a]
}

/*********************************************************************
*
*       USBH_MSD_ReadSectors
*
*  Function description
*    Reads sectors from a USB Mass Storage device. To read file and
*    folders use the file system functions. This function allows to read
*    sectors raw.
*
*  Parameters
*    Unit:          0-based Unit Id. See USBH_MSD_GetUnits().
*    SectorAddress: Index of the first sector to read.
*                   The first sector has the index 0.
*    NumSectors:    Number of sectors to read.
*    pBuffer:       Pointer to a caller allocated buffer.
*
*  Return value
*    == USBH_STATUS_SUCCESS: Sectors successfully read.
*    != USBH_STATUS_SUCCESS: An error occurred.
*/
USBH_STATUS USBH_MSD_ReadSectors(U8 Unit, U32 SectorAddress, U32 NumSectors, U8 * pBuffer) {
  USBH_MSD_UNIT   * pUnit;
  USBH_STATUS       Status;

  USBH_ASSERT(NumSectors != 0);
  Status = _FindUnit(Unit, &pUnit);
  if (Status != USBH_STATUS_SUCCESS) {
    return Status;
  }
  Status = _SendTestUnitReadyIfNecessary(pUnit);
  if (Status == USBH_STATUS_SUCCESS) {
    if (USBH_MSD_Global.pCacheAPI != NULL) {
      Status = USBH_MSD_Global.pCacheAPI->pfReadSectors(pUnit, SectorAddress, pBuffer, (U16)NumSectors); // Read from the device with the correct protocol layer
    } else {
      Status = USBH_MSD__ReadSectorsNoCache(pUnit, SectorAddress, pBuffer, NumSectors); // Read from the device with the correct protocol layer
    }
    if (Status != USBH_STATUS_SUCCESS) {
      USBH_WARN((USBH_MCAT_MSC_API, "USBH_MSD_ReadSectors: Status %s", USBH_GetStatusStr(Status)));
      //
      // Invalidate test unit ready time to trigger the test unit ready command.
      //
      pUnit->NextTestUnitReadyValid = 0;
    } else {
      //
      // Update test unit ready time. Even though we did not run TestUnitReady explicitly
      // a successful read means that all is good with the medium, therefore a TestUnitReady is not necessary.
      //
      pUnit->NextTestUnitReadyTime = USBH_TIME_CALC_EXPIRATION(USBH_MSD_TEST_UNIT_READY_DELAY);
    }
  }
  pUnit->pInst->State = MSD_STATE_READY;
  return Status;
}


/*********************************************************************
*
*       USBH_MSD_WriteSectors
*
*  Function description
*    Writes sectors to a USB Mass Storage device. To write files and
*    folders use the file system functions. This function allows to write
*    sectors raw.
*
*  Parameters
*    Unit:          0-based Unit Id. See USBH_MSD_GetUnits().
*    SectorAddress: Index of the first sector to write.
*                   The first sector has the index 0.
*    NumSectors:    Number of sectors to write.
*    pBuffer:       Pointer to the data.
*
*  Return value
*    == USBH_STATUS_SUCCESS: Sectors successfully written.
*    != USBH_STATUS_SUCCESS: An error occurred.
*/
USBH_STATUS USBH_MSD_WriteSectors(U8 Unit, U32 SectorAddress, U32 NumSectors, const U8 * pBuffer) {
  USBH_MSD_UNIT   * pUnit;
  USBH_STATUS       Status;

  USBH_ASSERT(NumSectors != 0);
  Status = _FindUnit(Unit, &pUnit);
  if (Status != USBH_STATUS_SUCCESS) {
    return Status;
  }
  Status = _SendTestUnitReadyIfNecessary(pUnit);
  if (Status == USBH_STATUS_SUCCESS) {
    if (USBH_MSD_Global.pCacheAPI != NULL) {
      Status = USBH_MSD_Global.pCacheAPI->pfWriteSectors(pUnit, SectorAddress, pBuffer, (U16)NumSectors); // Read from the device with the correct protocol layer
    } else {
      Status = USBH_MSD__WriteSectorsNoCache(pUnit, SectorAddress, pBuffer, NumSectors); // Read from the device with the correct protocol layer
    }
    if (Status != USBH_STATUS_SUCCESS) {
      USBH_WARN((USBH_MCAT_MSC_API, "USBH_MSD_WriteSectors: Status %s", USBH_GetStatusStr(Status)));
      //
      // Invalidate test unit ready time to trigger the test unit ready command.
      //
      pUnit->NextTestUnitReadyValid = 0;
    } else {
      //
      // Update test unit ready time. Even though we did not run TestUnitReady explicitly
      // a successful read means that all is good with the medium, therefore a TestUnitReady is not necessary.
      //
      pUnit->NextTestUnitReadyTime = USBH_TIME_CALC_EXPIRATION(USBH_MSD_TEST_UNIT_READY_DELAY);
    }
  }
  pUnit->pInst->State = MSD_STATE_READY;
  return Status;
}

/*********************************************************************
*
*       USBH_MSD_GetStatus
*
*  Function description
*    Checks the Status of a device. Therefore it performs a "Test Unit Ready" command to
*    test if the device is still connected and if a logical unit is assigned.
*
*  Parameters
*    Unit: 0-based Unit Id. See USBH_MSD_GetUnits().
*
*  Return value
*    == USBH_STATUS_SUCCESS: Device is ready for operation.
*    != USBH_STATUS_SUCCESS: An error occurred.
*/
USBH_STATUS USBH_MSD_GetStatus(U8 Unit) {
  USBH_MSD_UNIT  * pUnit;
  USBH_STATUS      Status;

  Status = _FindUnit(Unit, &pUnit);
  if (Status != USBH_STATUS_SUCCESS) {
    return Status;
  }
  //
  // Invalidate test unit ready time to trigger the test unit ready command.
  //
  pUnit->NextTestUnitReadyValid = 0;
  Status = _SendTestUnitReadyIfNecessary(pUnit);
  if (Status == USBH_STATUS_SUCCESS && pUnit->MaxSectorAddress == 0u) {
    //
    // If the number of sectors is zero the medium was most likely not inserted into the device upon enumeration.
    // Try to retrieve capacity values.
    //
    Status = _ReadCapacity(pUnit);
  }
  pUnit->pInst->State = MSD_STATE_READY;
  return Status;
}

/*********************************************************************
*
*       USBH_MSD_GetUnitInfo
*
*  Function description
*    Returns basic information about the logical unit (LUN).
*
*  Parameters
*    Unit:  0-based Unit Id. See USBH_MSD_GetUnits().
*    pInfo: [OUT] Pointer to a caller provided structure of type USBH_MSD_UNIT_INFO.
*           It receives the information about the LUN in case of success.
*
*  Return value
*    == USBH_STATUS_SUCCESS: Device is ready for operation.
*    != USBH_STATUS_SUCCESS: An error occurred.
*/
USBH_STATUS USBH_MSD_GetUnitInfo(U8 Unit, USBH_MSD_UNIT_INFO * pInfo) {
  USBH_MSD_UNIT     * pUnit;
  USBH_STATUS         Status;
  USBH_INTERFACE_INFO IFaceInfo;

  USBH_ASSERT_PTR(pInfo);
  Status = _FindUnit(Unit, &pUnit);
  if (Status != USBH_STATUS_SUCCESS) {
    return Status;
  }
  Status = USBH_GetInterfaceInfo(pUnit->pInst->InterfaceID, &IFaceInfo);
  if (Status == USBH_STATUS_SUCCESS) {
    USBH_MEMSET(pInfo, 0, sizeof(USBH_MSD_UNIT_INFO));
    //
    // If the number of sectors is zero the medium was most likely not inserted into the device upon enumeration.
    // Try to retrieve capacity values.
    //
    if (pUnit->MaxSectorAddress == 0u) {
      Status = _SendTestUnitReadyIfNecessary(pUnit);
      if (Status == USBH_STATUS_SUCCESS) {
        Status = _ReadCapacity(pUnit);
      }
    }
    if (Status == USBH_STATUS_SUCCESS) {
      pInfo->WriteProtectFlag = pUnit->WriteProtect;
      pInfo->BytesPerSector = pUnit->BytesPerSector;
      pInfo->TotalSectors = pUnit->MaxSectorAddress + 1u;
    }
    USBH_MEMCPY(pInfo->acVendorName, pUnit->InquiryData.aVendorIdentification, sizeof(pUnit->InquiryData.aVendorIdentification));
    USBH_MEMCPY(pInfo->acProductName, pUnit->InquiryData.aProductIdentification, sizeof(pUnit->InquiryData.aProductIdentification));
    USBH_MEMCPY(pInfo->acRevision, pUnit->InquiryData.aRevision, sizeof(pUnit->InquiryData.aRevision));
    pInfo->VendorId = IFaceInfo.VendorId;
    pInfo->ProductId = IFaceInfo.ProductId;
  }
  pUnit->pInst->State = MSD_STATE_READY;
  return Status;
}

/*********************************************************************
*
*       USBH_MSD_GetUnits
*
*  Function description
*    Returns available units for a device.
*
*  Parameters
*    DevIndex:  Index of the MSD device returned by USBH_MSD_LUN_NOTIFICATION_FUNC.
*    pUnitMask: [OUT] Pointer to a U32 variable which will receive the LUN mask.
*
*  Return value
*    == USBH_STATUS_SUCCESS: Device is ready for operation.
*    != USBH_STATUS_SUCCESS: An error occurred.
*
*  Additional information
*    The mask corresponds to the unit IDs. The device has unit ID n, if bit n of the mask is set.
*    E.g. a mask of 0x0000000C means unit ID 2 and unit ID 3 are available for the device.
*/
USBH_STATUS USBH_MSD_GetUnits(U8 DevIndex, U32 *pUnitMask) {
  U32               UnitMask;
  USBH_MSD_UNIT   * pUnit;
  USBH_MSD_INST   * pInst;
  unsigned          i;

  *pUnitMask = 0;
  if (DevIndex < USBH_MSD_MAX_DEVICES) {
    pInst = USBH_MSD_Global.pDevices[DevIndex];
    if (pInst != NULL) {
      pUnit    = pInst->aUnits;
      UnitMask = 0;
      for (i = 0; i < pInst->UnitCnt; i++) {
        UnitMask |= (1uL << pUnit->Unit);
        pUnit++;
      }
      *pUnitMask = UnitMask;
      return USBH_STATUS_SUCCESS;
    }
  }
  return USBH_STATUS_INVALID_PARAM;
}

/*********************************************************************
*
*       USBH_MSD_GetPortInfo
*
*  Function description
*    Retrieves the port information about a USB MSC device using a unit ID.
*
*  Parameters
*    Unit:      0-based Unit Id. See USBH_MSD_GetUnits().
*    pPortInfo: [OUT] Pointer to a caller provided structure of type USBH_PORT_INFO.
*               It receives the information about the LUN in case of success.
*
*  Return value
*    == USBH_STATUS_SUCCESS: Success, pPortInfo contains valid port information.
*    != USBH_STATUS_SUCCESS: An error occurred.
*/
USBH_STATUS USBH_MSD_GetPortInfo(U8 Unit, USBH_PORT_INFO * pPortInfo) {
  USBH_MSD_UNIT     * pUnit;
  USBH_STATUS         Status;

  USBH_ASSERT_PTR(pPortInfo);
  Status = _FindUnit(Unit, &pUnit);
  if (Status != USBH_STATUS_SUCCESS) {
    return Status;
  }
  Status = USBH_GetPortInfo(pUnit->pInst->InterfaceID, pPortInfo);
  pUnit->pInst->State = MSD_STATE_READY;
  return Status;
}

/*********************************************************************
*
*       USBH_MSD_SetNotification
*
*  Function description
*    Set a callback once a new MSD device is connected and initialized.
*
*  Parameters
*    pfLunNotification : Pointer to a function that shall be called when a new device notification is received. The function is called when a device is attached and ready or when it is removed.
*    pContext          : Pointer to a context that should be passed to pfLunNotification.
*
*/
void USBH_MSD_SetNotification(USBH_MSD_LUN_NOTIFICATION_FUNC * pfLunNotification, void * pContext) {
  //
  // Set the callback and its context.
  //
  USBH_MSD_Global.pfLunNotification = pfLunNotification;
  USBH_MSD_Global.pContext = pContext;
}

#else /* USBH_USE_LEGACY_MSD */

/*********************************************************************
*
*       USBH_MSD_Dummy
*
*  Function description
*    Dummy function to avoid problems with certain compilers which
*    can not handle empty object files.
*/
void USBH_MSD_Dummy(void);
void USBH_MSD_Dummy(void) {
}

#endif /* USBH_USE_LEGACY_MSD */

/*************************** End of file ****************************/

