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
File        : USBH_MSD.c
Purpose     : Legacy USB MSD host implementation
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/

#include "USBH_MSD_Int.h"
#include "USBH_Util.h"

#if USBH_USE_LEGACY_MSD

/*********************************************************************
*
*       Defines, configurable
*
**********************************************************************
*/

#ifndef USBH_MSD_MAX_DEVICES
  #define USBH_MSD_MAX_DEVICES            32u // Limited to 32 because use of a bit mask in an U32
#endif

#ifndef USBH_MSD_CSW_READ_TIMEOUT
  #define USBH_MSD_CSW_READ_TIMEOUT       10000 // Set Command Status Wrapper timeout to 10 sec to be compatible to Windows.
#endif

#ifndef USBH_MSD_REMOVAL_TIMEOUT
  #define USBH_MSD_REMOVAL_TIMEOUT        100
#endif

#ifndef USBH_MSD_TEST_UNIT_READY_DELAY
  #define USBH_MSD_TEST_UNIT_READY_DELAY  5000  // Given in ms.
#endif

#ifndef USBH_MSD_READ_CAP_MAX_RETRIES
  #define USBH_MSD_READ_CAP_MAX_RETRIES   20u
#endif

//
// Specifies the maximum time in milliseconds, for reading all bytes with the bulk read operation.
//
#ifndef USBH_MSD_READ_TIMEOUT
  #define USBH_MSD_READ_TIMEOUT         10000u
#endif
//
// Specifies the maximum time, in milliseconds, for writing all bytes with the bulk write operation.
//
#ifndef USBH_MSD_WRITE_TIMEOUT
  #define USBH_MSD_WRITE_TIMEOUT        10000u
#endif
//
// Must be a multiple of the maximum packet length for bulk data endpoints.
// That are 64 bytes for a USB 1.1 device and 512 bytes for a USB 2.0 high speed device.
//
#ifndef USBH_MSD_MAX_TRANSFER_SIZE
  #define USBH_MSD_MAX_TRANSFER_SIZE    (64u * 1024u) // [bytes]
#endif
//
// Specifies the default sector size in bytes to be used for reading and writing.
//
#ifndef USBH_MSD_DEFAULT_SECTOR_SIZE
  #define USBH_MSD_DEFAULT_SECTOR_SIZE  512u
#endif

/*********************************************************************
*
*       Defines, fixed
*
**********************************************************************
*/
//
// Constants in the Class Interface Descriptor
// for USB Mass Storage devices
//
#define MASS_STORAGE_CLASS  0x08u
#define PROTOCOL_BULK_ONLY  0x50u // Bulk only
#define SUBCLASS_6          0x06u // Transparent SCSI, that can be used as SUBCLASS_RBC

/*********************************************************************
*
*       Defines, function-replacement
*
**********************************************************************
*/

#if (USBH_DEBUG > 1)
  #define USBH_MSD_PLW_PRINT_INQUIRYDATA(data)  _PlWPrintInquiryData(data)
#endif

//
// Trace helpers
//
#if (USBH_DEBUG > 1)

typedef struct _STATUS_TEXT_TABLE {
  int          Id;
  const char * sText;
} STATUS_TEXT_TABLE;

// String table for printing Status messages
static STATUS_TEXT_TABLE const _aDevTypeTable [] = {
  { INQUIRY_DIRECT_DEVICE,         "Direct Device" },
  { INQUIRY_SEQ_DEVICE,            "Sequential-access device (streamer)" },
  { INQUIRY_WRITE_ONCE_DEVICE,     "WriteOnce device" },
  { INQUIRY_CD_ROM_DEVICE,         "CD-ROM/DVD" },
  { INQUIRY_NON_CD_OPTICAL_DEVICE, "Optical memory device" }
};

// String table for printing Status messages
static STATUS_TEXT_TABLE const _aVersionTable [] = {
  { ANSI_VERSION_MIGHT_UFI,      "ANSI_VERSION_MIGHT_COMPLY with UFI" },
  { ANSI_VERSION_SCSI_1,         "ANSI_VERSION_SCSI_1" },
  { ANSI_VERSION_SCSI_2,         "ANSI_VERSION_SCSI_2" },
  { ANSI_VERSION_SCSI_3_SPC,     "ANSI_VERSION_SCSI_3_SPC" },
  { ANSI_VERSION_SCSI_3_SPC_2,   "ANSI_VERSION_SCSI_3_SPC_2" },
  { ANSI_VERSION_SCSI_3_SPC_3_4, "ANSI_VERSION_SCSI_3_SPC_3_4" }
};

// String table for printing Status messages
static STATUS_TEXT_TABLE const _aResponseFormatTable [] = {
  { INQUIRY_RESPONSE_SCSI_1,          "INQUIRY_RESPONSE_SCSI_1"},
  { INQUIRY_RESPONSE_IN_THIS_VERISON, "INQUIRY_RESPONSE_IN_THIS_VERISON"},
  { INQUIRY_RESPONSE_MIGTH_UFI,       "INQUIRY_RESPONSE_MIGHT_UFI"}
};
#endif



/*********************************************************************
*
*       Public data
*
**********************************************************************
*/
USBH_MSD_GLOBAL USBH_MSD_Global; // Global driver object

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _FillCBW
*
*  Function description
*    Initialize the complete command block without copying the command bytes
*/
static void _FillCBW(COMMAND_BLOCK_WRAPPER * pCBW, U32 Tag, U32 DataLength, U8 Flags, U8 Lun, U8 CommandLength)  {
  pCBW->Signature          = CBW_SIGNATURE;
  pCBW->Tag                = Tag;
  pCBW->Flags              = Flags;
  pCBW->Lun                = Lun;
  pCBW->DataTransferLength = DataLength;
  pCBW->Length             = CommandLength;
}

/*********************************************************************
*
*       _IsCSWValidAndMeaningful
*
*  Function description
*    Checks if the command Status block is valid and meaningful
*/
static USBH_BOOL _IsCSWValidAndMeaningful(const USBH_MSD_INST * pInst, const COMMAND_BLOCK_WRAPPER * pCBW, const COMMAND_STATUS_WRAPPER * pCSW, const U32 CSWlength) {
  USBH_ASSERT_MAGIC(pInst, USBH_MSD_INST);
  if (CSWlength < CSW_LENGTH) {
    USBH_WARN((USBH_MCAT_MSC, "IsCSWValid: invalid CSW length: %u",CSWlength));
    return FALSE;                       // False
  }
  if (pCSW->Signature != CSW_SIGNATURE) {

#if (USBH_DEBUG > 1)
    if (CSWlength == CSW_LENGTH) {  // Prevents debug messages if test a regular data block
      USBH_WARN((USBH_MCAT_MSC, "IsCSWValid: invalid CSW signature: 0x%08X",pCSW->Signature));
    }
#endif

    return FALSE;
  }
  if (pCSW->Tag != pInst->BlockWrapperTag) {

#if (USBH_DEBUG > 1)
    if (CSWlength == CSW_LENGTH) {  // Prevent debug messages if test a regular data block
      USBH_WARN((USBH_MCAT_MSC, "IsCSWValid: invalid Tag sent:0x%08x rcv:0x%08x", pCBW->Tag,pCSW->Tag));
    }
#endif

    return FALSE;
  }
  if (2u == pCSW->Status) {          // CSW is valid
    return TRUE;
  }
  if (2u > pCSW->Status && pCSW->Residue <= pCBW->DataTransferLength) {
    return TRUE;
  }
  return FALSE;
}

/*********************************************************************
*
*       _WriteTag
*
*  Function description
*    Writes a tag beginning with offset 4 of the command block wrapper in little endian byte order
*/
static void _WriteTag(USBH_MSD_INST * pInst, U8 * pCBWBuffer) {
  USBH_ASSERT_MAGIC(pInst, USBH_MSD_INST);
  USBH_ASSERT_PTR  (pCBWBuffer);
  pInst->BlockWrapperTag++;    // LSB
  *(pCBWBuffer + 4) = (U8) pInst->BlockWrapperTag;
  *(pCBWBuffer + 5) = (U8)(pInst->BlockWrapperTag >> 8);
  *(pCBWBuffer + 6) = (U8)(pInst->BlockWrapperTag >> 16);
  *(pCBWBuffer + 7) = (U8)(pInst->BlockWrapperTag >> 24);
}

/*********************************************************************
*
*       _ConvBufferToStatusWrapper
*
*  Function description
*    Converts a byte buffer to a structure of type COMMAND_STATUS_WRAPPER.
*    This function is independent from the byte order of the processor.
*    The buffer is in little endian byte format.
*
*  Return value
*    USBH_STATUS_SUCCESS for success,
*    USBH_STATUS_ERROR for error
*/
static USBH_STATUS _ConvBufferToStatusWrapper(const U8 * pBuffer, unsigned Length, COMMAND_STATUS_WRAPPER * pCsw) {
  USBH_ASSERT_PTR(pBuffer);
  if (Length < CSW_LENGTH) {
    return USBH_STATUS_LENGTH;
  }
  pCsw->Signature = USBH_LoadU32LE(pBuffer);
  pCsw->Tag       = USBH_LoadU32LE(pBuffer + 4);  //  4: tag Same as original command
  pCsw->Residue   = USBH_LoadU32LE(pBuffer + 8);  //  8: residue, amount of bytes not transferred
  pCsw->Status    = *(pBuffer + 12);              // 12: Status
  return USBH_STATUS_SUCCESS;
}

/*********************************************************************
*
*       _FreeLuns
*
*  Function description
*    Frees the unit resources of the device.
*/
static void _FreeLuns(USBH_MSD_INST * pInst) {
  USBH_MSD_UNIT * pUnit;
  unsigned InstanceUnitCnt;
  unsigned i;
  unsigned j;

  USBH_ASSERT_MAGIC(pInst, USBH_MSD_INST);
  USBH_LOG((USBH_MCAT_MSC, "_FreeLuns Luns: %d", pInst->UnitCnt));
  InstanceUnitCnt = pInst->UnitCnt;
  for (i = 0; i < pInst->UnitCnt; i++) { // invalidate the unit object
    //
    // Remove unit from the global list.
    //
    for (j = 0; j < USBH_MSD_MAX_UNITS; j++) {
      pUnit = USBH_MSD_Global.apLogicalUnit[j];
      if (pUnit == pInst->apUnit[i]) {
        USBH_MSD_Global.apLogicalUnit[j] = (USBH_MSD_UNIT *)NULL;
        //
        // The Read-ahead cache needs to be invalidated.
        // Otherwise the cache thinks it has valid data.
        //
        if (USBH_MSD_Global.pCacheAPI != NULL) {
          USBH_MSD_Global.pCacheAPI->pfInvalidate(pInst->apUnit[i]);
        }
        USBH_MSD_Global.NumLUNs--;
        InstanceUnitCnt--;
        USBH_FREE(pUnit);
        pInst->apUnit[i] = NULL;
        break;
      }
    }
  }
  //
  // It is possible for LUNs to have been allocated, but for them
  // not to be inside the global list. This can occur when the device
  // failed the initialization procedure.
  //
  if (InstanceUnitCnt != 0u) {
    for (i = 0; i < USBH_MSD_MAX_UNITS; i++) {
      if (pInst->apUnit[i] != NULL) {
        USBH_FREE(pInst->apUnit[i]);
        pInst->apUnit[i] = NULL;
      }
    }
  }
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
static void _RemoveInstanceFromList(const USBH_MSD_INST * pInst) {
  USBH_MSD_INST * pPrev;
  USBH_MSD_INST * pCurrent;

  USBH_ASSERT_MAGIC(pInst, USBH_MSD_INST);
  if (pInst == USBH_MSD_Global.pFirst) {
    USBH_MSD_Global.pFirst = USBH_MSD_Global.pFirst->pNext;
  } else {
    pPrev = USBH_MSD_Global.pFirst;
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
*       _DeleteDevice
*
*  Function description
*    Deletes all units that are connected with the device and marks the
*    device object as unused by setting the driver handle to zero.
*/
static void _DeleteDevice(USBH_MSD_INST * pInst) {
  USBH_ASSERT_MAGIC(pInst, USBH_MSD_INST);
  USBH_LOG((USBH_MCAT_MSC, "USBH_MSD_DeleteDevice"));
  if (NULL != pInst->hInterface) {
    USBH_CloseInterface(pInst->hInterface);
    pInst->hInterface = NULL;
  }
  if (NULL != pInst->pUrbEvent) {
    USBH_OS_FreeEvent(pInst->pUrbEvent);
    pInst->pUrbEvent = NULL;
  }
  //
  // Remove instance from list
  //
  _RemoveInstanceFromList(pInst);
  //
  // Free all associated units.
  //
  _FreeLuns(pInst);
  if (NULL != pInst->pTempBuf) {
    USBH_FREE(pInst->pTempBuf);
  }
  USBH_FREE(pInst);
}

/*********************************************************************
*
*       _IncRefCnt
*/
static void _IncRefCnt(USBH_MSD_INST * pInst) {
  USBH_ASSERT_MAGIC(pInst, USBH_MSD_INST);
  USBH_OS_Lock(USBH_MUTEX_DEVICE);
  pInst->RefCnt++;
  USBH_OS_Unlock(USBH_MUTEX_DEVICE);
  USBH_LOG((USBH_MCAT_MSC, "_IncRefCnt: %d ",pInst->RefCnt));
}

/*********************************************************************
*
*       _DecRefCnt
*/
static void _DecRefCnt(USBH_MSD_INST * pInst) {
  int RefCnt;

  USBH_ASSERT_MAGIC(pInst, USBH_MSD_INST);
  USBH_OS_Lock(USBH_MUTEX_DEVICE);
  RefCnt = --(pInst->RefCnt);
  USBH_OS_Unlock(USBH_MUTEX_DEVICE);
  if (RefCnt < 0) {
    USBH_PANIC("USBH MSD RefCnt < 0");
  }
  USBH_LOG((USBH_MCAT_MSC, "_DecRefCnt: %d ", RefCnt));
}

/*********************************************************************
*
*       _UsbDeviceReset
*
*  Function description
*    Send a reset URB to a device.
*    The reset URB will trigger a device removal and a subsequent
*    re-enumeration.
*
*  Parameters
*    pInst   : Pointer to a MSD device object.
*/
static void _UsbDeviceReset(const USBH_MSD_INST * pInst) {
  USBH_STATUS   Status;
  USBH_URB      Urb;

  USBH_ASSERT_MAGIC(pInst, USBH_MSD_INST);
  USBH_ASSERT_PTR(pInst->hInterface);
  Urb.Header.Function = USBH_FUNCTION_RESET_DEVICE;
  Urb.Header.pfOnCompletion = NULL;
  Status = USBH_SubmitUrb(pInst->hInterface, &Urb); // No need to call _SubmitUrbAndWait because USBH_FUNCTION_RESET_DEVICE never returns with USBH_STATUS_PENDING
  if (Status != USBH_STATUS_SUCCESS) {
    USBH_WARN((USBH_MCAT_MSC, "_UsbDeviceReset: USBH_SubmitUrb: Status = %s", USBH_GetStatusStr(Status)));
  }
}

/*********************************************************************
*
*       _OnSubmitUrbCompletion
*/
static void _OnSubmitUrbCompletion(USBH_URB * pUrb) USBH_CALLBACK_USE {
  USBH_MSD_INST * pInst;

  pInst = USBH_CTX2PTR(USBH_MSD_INST, pUrb->Header.pContext);
  USBH_ASSERT_MAGIC(pInst, USBH_MSD_INST);
  USBH_LOG((USBH_MCAT_MSC, "_OnSubmitUrbCompletion URB Status = %s", USBH_GetStatusStr(pUrb->Header.Status)));
  if (pInst->pUrbEvent != NULL) {
    USBH_OS_SetEvent(pInst->pUrbEvent);
  }
  _DecRefCnt(pInst);
}

/*********************************************************************
*
*       _WaitEventTimed
*/
static int _WaitEventTimed(const USBH_MSD_INST * pInst, U32 Timeout) {
#if USBH_URB_QUEUE_SIZE != 0u
  while (Timeout > USBH_URB_QUEUE_RETRY_INTV) {
    if (USBH_OS_WaitEventTimed(pInst->pUrbEvent, USBH_URB_QUEUE_RETRY_INTV) == USBH_OS_EVENT_SIGNALED) {
      return USBH_OS_EVENT_SIGNALED;
    }
    Timeout -= USBH_URB_QUEUE_RETRY_INTV;
    USBH_RetryRequestIntf(pInst->hInterface);
  }
#endif
  return USBH_OS_WaitEventTimed(pInst->pUrbEvent, Timeout);
}

/*********************************************************************
*
*       _SubmitUrbAndWait
*
*  Function description
*    Submits an URB to the USB bus driver synchronous, it uses the
*    OS event functions. On successful completion the URB Status is returned!
*/
static USBH_STATUS _SubmitUrbAndWait(USBH_MSD_INST * pInst, USBH_URB * pUrb, U32 Timeout) {
  USBH_STATUS Status;
  int         EventStatus;

  USBH_ASSERT_MAGIC(pInst, USBH_MSD_INST);
  USBH_ASSERT(NULL != pInst->hInterface);
  USBH_ASSERT_PTR(pInst->pUrbEvent);
  USBH_LOG((USBH_MCAT_MSC, "_SubmitUrbAndWait"));
  pUrb->Header.pfOnCompletion = _OnSubmitUrbCompletion;
  pUrb->Header.pContext       = pInst;
  //
  // If we have reached the max number of errors the device is marked for reset via _UsbDeviceReset
  // All API functions must return with an error at this point.
  //
  if (pInst->ErrorCount >= BULK_ONLY_MAX_RETRY) {
    return USBH_STATUS_ERROR;
  }
  USBH_OS_ResetEvent(pInst->pUrbEvent);
  _IncRefCnt(pInst); // RefCnt is decremented in _OnSubmitUrbCompletion or below upon error.
  Status = USBH_SubmitUrb(pInst->hInterface, pUrb);
  if (Status != USBH_STATUS_PENDING) {
    _DecRefCnt(pInst);
    USBH_LOG((USBH_MCAT_MSC, "_SubmitUrbAndWait: USBH_SubmitUrb Status: %s", USBH_GetStatusStr(Status)));
  } else {                                // Pending URB
    EventStatus = _WaitEventTimed(pInst, Timeout);
    if (EventStatus != USBH_OS_EVENT_SIGNALED) {
      if (pInst->IsReady == TRUE) {
        USBH_URB * pAbortUrb = &pInst->AbortUrb;
        USBH_LOG((USBH_MCAT_MSC, "_SubmitUrbAndWait: timeout Status: 0x%08x, now abort the URB!",EventStatus));
        USBH_ZERO_MEMORY(pAbortUrb, sizeof(USBH_URB));
        switch (pUrb->Header.Function) {    // Not signaled abort and wait infinite
        case USBH_FUNCTION_BULK_REQUEST:
        case USBH_FUNCTION_INT_REQUEST:
          pAbortUrb->Request.EndpointRequest.Endpoint = pUrb->Request.BulkIntRequest.Endpoint;
          break;
        case USBH_FUNCTION_CONTROL_REQUEST:
        case USBH_FUNCTION_RESET_ENDPOINT:
          pAbortUrb->Request.EndpointRequest.Endpoint = 0;
          break;
        default:
          //
          // This should not happen unless the URB was somehow corrupted.
          //
          USBH_WARN((USBH_MCAT_MSC, "_SubmitUrbAndWait: invalid URB function: %d", pUrb->Header.Function));
          return USBH_STATUS_ERROR;       //lint -e{9077}  D:102[a]
        }
        USBH_WARN((USBH_MCAT_MSC, "_SubmitUrbAndWait: Abort Ep: 0x%x", pAbortUrb->Request.EndpointRequest.Endpoint));
        pAbortUrb->Header.Function = USBH_FUNCTION_ABORT_ENDPOINT;
        _IncRefCnt(pInst);
        Status = USBH_SubmitUrb(pInst->hInterface, pAbortUrb);
        if (Status != USBH_STATUS_SUCCESS) {
          USBH_WARN((USBH_MCAT_MSC, "_SubmitUrbAndWait: USBH_FUNCTION_ABORT_ENDPOINT st: %s", USBH_GetStatusStr(Status)));
        } else {
          //
          // Abort URB sent out successfully, set the return status to timeout.
          //
          Status = USBH_STATUS_TIMEOUT;
          USBH_OS_WaitEvent(pInst->pUrbEvent);
        }
        _DecRefCnt(pInst);
      } else {
        //
        // When IsReady is not set we are still inside the initialization phase.
        // The initialization is done from timer context, therefore we must
        // not use USBH_OS_WaitEvent which would block.
        // Instead we reset the device.
        //
        _UsbDeviceReset(pInst);
        Status = USBH_STATUS_DEVICE_REMOVED;
      }
      //
      // Tricky:
      // _DecRefCnt is not called here or after _UsbDeviceReset
      // because the original URB's completion routine is always called!
      //
    } else {
      //
      // In case the event was signaled the status is retrieved from the URB.
      //
      Status = pUrb->Header.Status;
      if (Status != USBH_STATUS_SUCCESS) {
        USBH_LOG((USBH_MCAT_MSC, "_SubmitUrbAndWait: URB Status: %s", USBH_GetStatusStr(Status)));
      }
    }
  }
  return Status;
}

/*********************************************************************
*
*       _ResetPipe
*
*  Function description
*    Resets a specific endpoint for a given device.
*
*  Parameters
*    pInst    : Pointer to a MSD device object.
*    EndPoint : Endpoint number and direction.
*
*  Return value
*    USBH_STATUS_SUCCESS : The endpoint was reset successfully.
*    Other value         : An error occurred.
*/
static USBH_STATUS _ResetPipe(USBH_MSD_INST * pInst, U8 EndPoint) {
  USBH_STATUS   Status;
  USBH_URB    * pUrb;

  USBH_ASSERT_MAGIC(pInst, USBH_MSD_INST);
  USBH_ASSERT_PTR  (pInst->hInterface);
  pUrb                                   = &pInst->ControlUrb;
  pUrb->Header.Function                  = USBH_FUNCTION_RESET_ENDPOINT;
  pUrb->Header.pfOnCompletion            = NULL;
  pUrb->Request.EndpointRequest.Endpoint = EndPoint;
  Status                                 = _SubmitUrbAndWait(pInst, pUrb, USBH_MSD_EP0_TIMEOUT); // On error this URB is not aborted
  if (Status != USBH_STATUS_SUCCESS) { // Reset pipe does not wait
    USBH_WARN((USBH_MCAT_MSC, "_ResetPipe: USBH_SubmitUrb Status = %s", USBH_GetStatusStr(Status)));
    Status = USBH_STATUS_ERROR;
  }
  return Status;
}

/*********************************************************************
*
*       _SetupRequest
*
*  Function description
*    Synchronous vendor request
*
*  Parameters
*    pInst   : Pointer to a MSD device object.
*    pUrb    : IN: pUrb.Request.ControlRequest.Setup, other URB fields undefined / OUT: Status.
*    pBuffer : IN: -  OUT: valid pointer or NULL.
*    pLength : IN: -  OUT: transferred bytes.
*    Timeout : Timeout in milliseconds.
*/
static USBH_STATUS _SetupRequest(USBH_MSD_INST * pInst, USBH_URB * pUrb, U8 * pBuffer, U32 * pLength, U32 Timeout) {
  USBH_STATUS                             Status;

  USBH_ASSERT_MAGIC(pInst, USBH_MSD_INST);
  *pLength                              = 0;  // Clear returned pLength
  pUrb->Header.Function                 = USBH_FUNCTION_CONTROL_REQUEST;
  pUrb->Request.ControlRequest.pBuffer  = pBuffer;
  Status                                = _SubmitUrbAndWait(pInst, pUrb, Timeout);
  if (Status != USBH_STATUS_SUCCESS) {
    USBH_WARN((USBH_MCAT_MSC, "_SetupRequest: Status = %s", USBH_GetStatusStr(Status)));
  } else {
    *pLength = pUrb->Request.ControlRequest.Length;
  }
  return Status;
}

/*********************************************************************
*
*       _ReadSync
*
*  Function description
*    _ReadSync reads all bytes to buffer via Bulk IN transfers.
*    Transactions are performed in chunks of no more than pInst->MaxInTransferSize
*/
static USBH_STATUS _ReadSync(USBH_MSD_INST * pInst, U8 * pBuffer, U32 * pLength, U32 Timeout, USBH_BOOL DataPhaseFlag, USBH_BOOL SectorDataFlag) {
  U32           remainingLength;
  U32           rdLength;
  U8          * pBuf;
  USBH_STATUS   Status = USBH_STATUS_SUCCESS;
  USBH_URB    * pUrb;

  USBH_ASSERT_MAGIC(pInst, USBH_MSD_INST);
  USBH_ASSERT_PTR  (pBuffer);
  USBH_ASSERT_PTR  (pLength);
  // Unused param, if needed for later use
  (void)DataPhaseFlag;
  (void)SectorDataFlag;
  USBH_LOG((USBH_MCAT_MSC, "_ReadSync Ep: %u,length: %4lu",(int)pInst->BulkInEp,*pLength));
  if (pInst->Removed == TRUE) {
    USBH_WARN((USBH_MCAT_MSC, "_ReadSync: Device removed!"));
    return USBH_STATUS_DEVICE_REMOVED;
  }
  pBuf                                  = pBuffer;
  remainingLength                       = *pLength;
  *pLength                              = 0;
  pUrb                                  = &pInst->Urb;
  pUrb->Header.Function                 = USBH_FUNCTION_BULK_REQUEST;
  pUrb->Request.BulkIntRequest.Endpoint = pInst->BulkInEp;
  while (remainingLength != 0u) {                         // Remaining buffer
    rdLength                            = USBH_MIN(remainingLength, pInst->MaxInTransferSize);
    pUrb->Request.BulkIntRequest.pBuffer = pBuf;
    pUrb->Request.BulkIntRequest.Length = rdLength;
    USBH_LOG((USBH_MCAT_MSC, "_ReadSync: DlReadSync bytes to read: %4lu",rdLength));
    Status                              = _SubmitUrbAndWait(pInst, pUrb, Timeout);
    rdLength                            = pUrb->Request.BulkIntRequest.Length;
    if (Status != USBH_STATUS_SUCCESS) {                                   // On error stops and discard data
      USBH_LOG((USBH_MCAT_MSC, "_ReadSync: _SubmitUrbAndWait: length: %u Status: %s", rdLength, USBH_GetStatusStr(Status)));
      break;
    } else {                                        // On success
      remainingLength -= rdLength;
      *pLength        += rdLength;
      if ((rdLength == 0u) || ((rdLength % pInst->BulkMaxPktSize) != 0u)) { // A short packet was received
        USBH_LOG((USBH_MCAT_MSC, "INFO _ReadSync: short packet with length %u received!", rdLength));
        break;
      }
      pBuf += rdLength;                // Adjust destination
    }
  }
  USBH_LOG((USBH_MCAT_MSC, "_ReadSync: returned length: %u ",*pLength));
  return Status;
}

/*********************************************************************
*
*       _WriteSync
*
*  Function description
*    _WriteSync writes all bytes to device via Bulk OUT transfers.
*    Transactions are performed in chunks of no more than pInst->MaxOutTransferSize.
*/
static USBH_STATUS _WriteSync(USBH_MSD_INST * pInst, const U8 * pBuffer, U32 * pLength, U32 Timeout, USBH_BOOL DataPhaseFlag, USBH_BOOL SectorDataFlag) {
  U32           RemainingLength;
  U32           WrittenLength;
  U32           OldLength;
  USBH_STATUS   Status;
  USBH_URB    * pUrb;

  // Unused param
  (void)DataPhaseFlag;
  (void)SectorDataFlag;
  USBH_ASSERT_MAGIC(pInst, USBH_MSD_INST);
  USBH_ASSERT_PTR  (pBuffer);
  USBH_ASSERT_PTR  (pLength);
  USBH_LOG((USBH_MCAT_MSC, "_WriteSync Ep: %4u,length: %4lu",pInst->BulkOutEp, *pLength));
  if (pInst->Removed == TRUE) {
    USBH_WARN((USBH_MCAT_MSC, "_WriteSync: Device removed!"));
    return USBH_STATUS_INVALID_PARAM;
  }
  RemainingLength                       = *pLength;
  pUrb                                  = &pInst->Urb;
  pUrb->Header.Function                 = USBH_FUNCTION_BULK_REQUEST;
  pUrb->Request.BulkIntRequest.Endpoint = pInst->BulkOutEp;
  do {
    WrittenLength = USBH_MIN(RemainingLength, pInst->MaxOutTransferSize);
    OldLength = WrittenLength;
    pUrb->Request.BulkIntRequest.pBuffer = (U8 *)pBuffer;  //lint !e9005 D:105[a]
    USBH_LOG((USBH_MCAT_MSC, "consider: DlWriteSync bytes to write: %4lu",WrittenLength));
    pUrb->Request.BulkIntRequest.Length  = WrittenLength;
    Status                               = _SubmitUrbAndWait(pInst, pUrb, Timeout);
    if (Status != USBH_STATUS_SUCCESS) {
      USBH_WARN((USBH_MCAT_MSC, "_WriteSync: _SubmitUrbAndWait: Status = %s", USBH_GetStatusStr(Status)));
      break;
    }
    USBH_LOG((USBH_MCAT_MSC, "_WriteSync: %4lu written", WrittenLength));
    if (WrittenLength != OldLength) {
      USBH_WARN((USBH_MCAT_MSC, "DlWriteSync: Not all bytes written"));
      break;
    }
    RemainingLength -= WrittenLength;
    pBuffer         += WrittenLength;  // Adjust source
  } while (RemainingLength != 0u);
  *pLength  -= RemainingLength;        // Does not consider the last write
  USBH_LOG((USBH_MCAT_MSC, "_WriteSync returned length: %4lu",*pLength));
  return Status;
}

/*********************************************************************
*
*       _ReadCSW
*
*  Function description
*    Reads the command Status block from the device and
*    checks if the Status block is valid and meaningful.
*    If the USB device stalls on the IN pipe
*    the endpoint is reset and the CSW is read again.
*
*  Return value
*    USBH_STATUS_SUCCESS         On success.
*    USBH_STATUS_COMMAND_FAILED  The command failed, check the sense data.
*    USBH_STATUS_ERROR           No command Status block received or Status block with a phase error.
*/
static USBH_STATUS _ReadCSW(USBH_MSD_INST * pInst, const COMMAND_BLOCK_WRAPPER * pCBW, COMMAND_STATUS_WRAPPER * pCSW) {
  USBH_STATUS   Status;
  U32           length;
  U8          * pBuf;
  int           i;

  USBH_ASSERT_MAGIC(pInst, USBH_MSD_INST);
  USBH_ASSERT_PTR  (pCBW);
  USBH_ASSERT_PTR  (pCSW);
  if (pInst->Removed == TRUE) {
    USBH_WARN((USBH_MCAT_MSC, "_ReadCSW: Device removed!"));
    return USBH_STATUS_INVALID_PARAM;
  }
  Status = USBH_STATUS_ERROR;
  pBuf   = pInst->pTempBuf;
  i      = 2;
  length = 0;         // If the first Status block read fails (no timeout error) then read a second time

  while (i != 0) {
    length = pInst->BulkMaxPktSize;
    Status = _ReadSync(pInst, pBuf, &length, USBH_MSD_CSW_READ_TIMEOUT, FALSE, FALSE);
    if (Status == USBH_STATUS_SUCCESS) {    // Success
      break;
    } else {          // Error
      USBH_WARN((USBH_MCAT_MSC, "_ReadCSW: _ReadSync: %s!", USBH_GetStatusStr(Status)));
      if (Status == USBH_STATUS_TIMEOUT) { // Timeout
        break;
      } else {        // On all other errors reset the pipe an try it again to read CSW
        Status = _ResetPipe(pInst, pInst->BulkInEp);
        if (Status != USBH_STATUS_SUCCESS) { // Reset error, break
          USBH_WARN((USBH_MCAT_MSC, "_ReadCSW: _ResetPipe: %s", USBH_GetStatusStr(Status)));
          break;
        }
      } // Try to read again the CSW
    }
    i--;
  }
  if (Status == USBH_STATUS_SUCCESS) {                                                // On success
    if (length == CSW_LENGTH) {
      if (_ConvBufferToStatusWrapper(pBuf, length, pCSW) == USBH_STATUS_SUCCESS) {   // Check CSW
        if (_IsCSWValidAndMeaningful(pInst, pCBW, pCSW, length) != TRUE) {
          USBH_WARN((USBH_MCAT_MSC, "_ReadCSW: IsCSWValidandMeaningful: %s", USBH_GetStatusStr(Status)));
          Status = USBH_STATUS_ERROR;
        }
      } else {
        USBH_WARN((USBH_MCAT_MSC, "_ReadCSW: _ConvBufferToStatusWrapper %s", USBH_GetStatusStr(Status)));
      }
    } else {
      USBH_WARN((USBH_MCAT_MSC, "_ReadCSW: invalid length: %u",length));
      Status = USBH_STATUS_ERROR;
    }
  }
  return Status;
}

/*********************************************************************
*
*       _BULKONLY_GetMaxLUN
*
*  Function description
*    see USBH_MSD_TL_GETMAX_LUN_INTERFACE
*
*  Return value
*    TBD
*/
static USBH_STATUS _BULKONLY_GetMaxLUN(USBH_MSD_INST * pInst, unsigned * maxLunIndex) {
  U32                 Length;
  USBH_SETUP_PACKET * pSetup;
  USBH_STATUS         Status;
  USBH_URB          * pUrb;
  U8                  c;

  USBH_ASSERT_MAGIC(pInst, USBH_MSD_INST);
  USBH_LOG((USBH_MCAT_MSC, "_BULKONLY_GetMaxLUN "));
  *maxLunIndex = 0; // default value
  if (pInst->Removed == TRUE) {
    USBH_WARN((USBH_MCAT_MSC, "GetMaxLUN: Device removed!"));
    return USBH_STATUS_DEVICE_REMOVED;
  }
  pUrb = &pInst->Urb;
  pSetup = &pUrb->Request.ControlRequest.Setup;
  pSetup->Type = USB_REQTYPE_CLASS | USB_INTERFACE_RECIPIENT | USB_IN_DIRECTION;
  pSetup->Request = BULK_ONLY_GETLUN_REQ;
  pSetup->Index = (U16)pInst->bInterfaceNumber;
  pSetup->Value = 0;
  pSetup->Length = BULK_ONLY_GETLUN_LENGTH; // Length is one byte
  Status = _SetupRequest(pInst, pUrb, &c, &Length, USBH_MSD_EP0_TIMEOUT);
  if (Status == USBH_STATUS_SUCCESS) {
    if (Length != BULK_ONLY_GETLUN_LENGTH) {
      USBH_WARN((USBH_MCAT_MSC, "GetMaxLUN: invalid Length received: %d", Length));
    } else {
      *maxLunIndex = c;
    }
  } else {
    USBH_WARN((USBH_MCAT_MSC, "_BULKONLY_GetMaxLUN Status = %s", USBH_GetStatusStr(Status)));
  }
  return Status;
}

/*********************************************************************
*
*       _ConvCommandBlockWrapper
*
*  Function description
*    _ConvCommandBlockWrapper copies the CBW structure to a byte pBuffer.
*    This function is independent from the byte order of the processor.
*    The pBuffer is in little endian byte format.
*    The minimum length of pBuffer must be CBW_LENGTH.
*/
static void _ConvCommandBlockWrapper(const COMMAND_BLOCK_WRAPPER * cbw, U8 * Buffer) {
  USBH_ASSERT_PTR(Buffer);
  USBH_ASSERT_PTR(cbw);
  USBH_StoreU32LE(Buffer,     cbw->Signature);          // index 0:Signature
  USBH_StoreU32LE(Buffer + 4, cbw->Tag);                // index:4 Tag
  USBH_StoreU32LE(Buffer + 8, cbw->DataTransferLength); // index:8 DataTransferLength
  Buffer[12] = cbw->Flags;
  Buffer[13] = cbw->Lun;
  Buffer[14] = cbw->Length;
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

  if (DevIndex < USBH_MSD_MAX_DEVICES) {
    Mask = (1UL << DevIndex);
    USBH_MSD_Global.DevIndexUsedMask &= ~Mask;
  }
}

/*********************************************************************
*
*       _RemovalTimer
*/
static void _RemovalTimer(void * pContext) {
  USBH_MSD_INST *pInst;

  USBH_OS_Lock(USBH_MUTEX_MSD);
  pInst = USBH_CTX2PTR(USBH_MSD_INST, pContext);
  if (pInst->RefCnt != 0) {
    USBH_OS_Unlock(USBH_MUTEX_MSD);
    USBH_StartTimer(&pInst->RemovalTimer, USBH_MSD_REMOVAL_TIMEOUT);
    return;
  }
  _FreeDevIndex(pInst->DeviceIndex);
  USBH_ReleaseTimer(&pInst->RemovalTimer);
  USBH_MSD_Global.NumDevices--;
  _DeleteDevice(pInst);
  USBH_OS_Unlock(USBH_MUTEX_MSD);
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
  if (USBH_MSD_Global.pfLunNotification && pInst->IsReady == TRUE) {
    USBH_MSD_Global.pfLunNotification(USBH_MSD_Global.pContext, pInst->DeviceIndex, USBH_MSD_EVENT_REMOVE);
  }
  USBH_OS_Lock(USBH_MUTEX_MSD);
  pInst->Removed = TRUE;
  if (pInst->WaitForRemoval == FALSE) {
    pInst->WaitForRemoval = TRUE;
    USBH_InitTimer(&pInst->RemovalTimer, _RemovalTimer, pInst);
    USBH_StartTimer(&pInst->RemovalTimer, USBH_MSD_REMOVAL_TIMEOUT);
    _DecRefCnt(pInst);
  }
  USBH_OS_Unlock(USBH_MUTEX_MSD);
}

/*********************************************************************
*
*       _SendCommandWriteData
*
*  Function description
*    Sends a complete MSD command (command, data and status stage).
*    Data is written from the device to the host.
*
*  Parameters
*    pUnit          : Pointer to the MSD unit.
*    pCmdBuffer     : Command pBuffer, must contain a valid device command.
*    CmdLength      : Size of command pBuffer, valid values:1-16.
*    pDataBuffer    : Transfer pBuffer.
*    pDataLength    : IN: length of pDataBuffer; OUT: transferred bytes.
*    Timeout        : Timeout in milliseconds.
*    SectorDataFlag : This parameter is not used at the moment.
*
*  Return value
*    USBH_STATUS_LENGTH : False command length or data length.
*    USBH_STATUS_COMMAND_FAILED : The device could not interpret the command.
*/
static USBH_STATUS _SendCommandWriteData(const USBH_MSD_UNIT * pUnit, const U8 * pCmdBuffer, U8 CmdLength, const U8 * pDataBuffer, U32 * pDataLength, U32 Timeout, USBH_BOOL SectorDataFlag) {
  COMMAND_BLOCK_WRAPPER    cbw; // Stores the request until completion
  COMMAND_STATUS_WRAPPER   csw;
  USBH_MSD_INST          * pInst;
  USBH_STATUS              Status;
  U8                     * pCBWBuffer;
  U32                      Length;
  U32                      DataLength;

  USBH_ASSERT(pUnit       != NULL);
  USBH_ASSERT(pCmdBuffer  != NULL);
  USBH_ASSERT(pDataLength != NULL);
  pInst = pUnit->pInst; // Get the pointer to the device
  if (pInst == NULL) {
    USBH_WARN((USBH_MCAT_MSC, "_SendCommandWriteData: Unit does not have a valid pInst!"));
    return USBH_STATUS_INVALID_PARAM;
  }
  USBH_ASSERT_MAGIC(pInst, USBH_MSD_INST);
  pCBWBuffer  = pInst->pTempBuf;
  if ((CmdLength == 0u) || (CmdLength > COMMAND_WRAPPER_CDB_FIELD_LENGTH)) {
    USBH_WARN((USBH_MCAT_MSC, "[Dev %d Lun %d] _SendCommandWriteData: CmdLength: %u", pUnit->pInst->DeviceIndex, pUnit->Lun, CmdLength));
    return USBH_STATUS_LENGTH;
  }
  DataLength    = *pDataLength;
  Status        = USBH_STATUS_ERROR;
  for (; ;) {
    if (Status == USBH_STATUS_DEVICE_REMOVED) {
      return Status;
    } else if (Status != USBH_STATUS_SUCCESS) {
      if (pInst->ErrorCount == BULK_ONLY_MAX_RETRY) {
        pInst->ErrorCount++; // Make sure _UsbDeviceReset is only called once.
        _UsbDeviceReset(pInst);
        return Status;
      } else if (pInst->ErrorCount > BULK_ONLY_MAX_RETRY) {
        return Status;
      } else {
        //
        // MISRA comment
        //
      }
    } else {
      //
      // MISRA comment
      //
    }
    //
    // COMMAND PHASE
    //
    USBH_ZERO_MEMORY(pCBWBuffer, CBW_LENGTH);
    _FillCBW(&cbw, 0, *pDataLength, CBW_FLAG_WRITE, pUnit->Lun, CmdLength);      // Setup the command block wrapper
    _ConvCommandBlockWrapper(&cbw, pCBWBuffer);                                  // Convert the command wrapper to a CBW pBuffer
    USBH_MEMCPY(&pCBWBuffer[COMMAND_WRAPPER_CDB_OFFSET], pCmdBuffer, CmdLength);
    *pDataLength  = 0;
    Length = CBW_LENGTH;
    _WriteTag(pInst, pCBWBuffer);
    Status = _WriteSync(pInst, pCBWBuffer, &Length, USBH_MSD_WRITE_TIMEOUT, FALSE, SectorDataFlag);
    if (Status != USBH_STATUS_SUCCESS) {
      pInst->ErrorCount++;
      USBH_WARN((USBH_MCAT_MSC, "[Dev %d Lun %d] _SendCommandWriteData: Command Phase: Status = %s", pUnit->pInst->DeviceIndex, pUnit->Lun, USBH_GetStatusStr(Status)));
      if (Status == USBH_STATUS_STALL) {
        USBH_LOG((USBH_MCAT_MSC, "[Dev %d Lun %d] DlResetPipe Ep-address: %u", pUnit->pInst->DeviceIndex, pUnit->Lun, pInst->BulkOutEp));
        Status = _ResetPipe(pInst, pInst->BulkOutEp);
        if (Status != USBH_STATUS_SUCCESS) {      // Reset error
          USBH_WARN((USBH_MCAT_MSC, "[Dev %d Lun %d] _SendCommandWriteData: _ResetPipe!", pUnit->pInst->DeviceIndex, pUnit->Lun));
          return Status;
        }
      }
      continue;
    } else {
      pInst->ErrorCount = 0;
    }
    //
    // DATA PHASE Bulk OUT
    //
    if (DataLength != 0u) {
      USBH_ASSERT(pDataBuffer != NULL);
      Length = DataLength;
      Status = _WriteSync(pInst, pDataBuffer, &Length, Timeout, TRUE, SectorDataFlag);
      if (Status != USBH_STATUS_SUCCESS) {          // Error
        pInst->ErrorCount++;
        USBH_WARN((USBH_MCAT_MSC, "[Dev %d Lun %d] _SendCommandWriteData: Data OUT Phase", pUnit->pInst->DeviceIndex, pUnit->Lun));
        if (Status == USBH_STATUS_STALL) {
          USBH_LOG((USBH_MCAT_MSC, "[Dev %d Lun %d] DlResetPipe Ep-address: %u", pUnit->pInst->DeviceIndex, pUnit->Lun, pInst->BulkOutEp));
          Status = _ResetPipe(pInst, pInst->BulkOutEp);
          if (Status != USBH_STATUS_SUCCESS) {      // Reset error
            USBH_WARN((USBH_MCAT_MSC, "[Dev %d Lun %d] _SendCommandWriteData: _ResetPipe!", pUnit->pInst->DeviceIndex, pUnit->Lun));
            return Status;
          }
        } else {
          USBH_WARN((USBH_MCAT_MSC, "[Dev %d Lun %d] _SendCommandWriteData data: other error!", pUnit->pInst->DeviceIndex, pUnit->Lun));
          continue;
        }
      } else {
        pInst->ErrorCount = 0;
      }
    }
    //
    // STATUS PHASE
    //
    Status = _ReadCSW(pInst, &cbw, &csw);
    if (Status != USBH_STATUS_SUCCESS) {         // Success
      pInst->ErrorCount++;
    } else {
      pInst->ErrorCount = 0;
      if (csw.Status != CSW_STATUS_PHASE_ERROR) {
        if (csw.Residue != 0u) { // This is not implemented in the same way from vendors!!
          *pDataLength  = cbw.DataTransferLength - csw.Residue;
        } else {
          *pDataLength  = Length;
        }

        if (csw.Status == CSW_STATUS_FAIL) {
          Status = USBH_STATUS_COMMAND_FAILED;
        } else {           // On success
          if (*pDataLength != Length) {
            USBH_WARN((USBH_MCAT_MSC, "[Dev %d Lun %d] _SendCommandWriteData: invalid Residue!", pUnit->pInst->DeviceIndex, pUnit->Lun));
          }
        }
        break;
      }
    }
  }
  return Status;
}

/*********************************************************************
*
*       _SendCommandReadData
*
*  Function description
*    Sends a complete MSD command (command, data and status stage).
*    Data is written from the host to the device.
*
*  Parameters
*    pUnit          : Pointer to the MSD unit.
*    pCmdBuffer     : Command pBuffer, must contain a valid device command.
*    CmdLength      : Size of command pBuffer, valid values:1-16.
*    pDataBuffer    : Transfer pBuffer.
*    pDataLength    : IN: Length of pDataBuffer; OUT: transferred bytes.
*    Timeout        : Timeout in milliseconds.
*    SectorDataFlag : This parameter is not used at the moment.
*    phadDataPhase  : [OUT] Pointer to a flag which is set to 1 if
*                     a data phase occurred during the MSC transfer. Can be NULL.
*
*  Return value
*    USBH_STATUS_LENGTH : False command Length or data Length.
*    USBH_STATUS_COMMAND_FAILED : The device could not interpret the command.
*/
static USBH_STATUS _SendCommandReadData(const USBH_MSD_UNIT * pUnit, const U8 * pCmdBuffer, U8 CmdLength, U8 * pDataBuffer, U32 * pDataLength, U32 Timeout, USBH_BOOL SectorDataFlag, U8 * phadDataPhase) {
  COMMAND_BLOCK_WRAPPER    cbw; // stores the request until completion
  COMMAND_STATUS_WRAPPER   csw;
  U32                      Length;
  U32                      DataLength;
  USBH_STATUS              Status;
  USBH_MSD_INST          * pInst;
  U8                     * pBuf;
  U8                       TempBufferUsed;

  USBH_ASSERT(pUnit       != NULL);
  USBH_ASSERT(pCmdBuffer  != NULL);
  USBH_ASSERT(pDataBuffer != NULL);
  USBH_ASSERT(pDataLength != NULL);
  pInst                     = pUnit->pInst;
  if (pInst == NULL) {
    USBH_WARN((USBH_MCAT_MSC, "_SendCommandReadData: Device removed!"));
    return USBH_STATUS_INVALID_PARAM;
  }
  USBH_ASSERT_MAGIC(pInst, USBH_MSD_INST);
  if ((CmdLength == 0u) || (CmdLength > COMMAND_WRAPPER_CDB_FIELD_LENGTH)) {
    USBH_WARN((USBH_MCAT_MSC, "_SendCommandReadData: CmdLength: %u",CmdLength));
    return USBH_STATUS_INVALID_PARAM;
  }
  if (phadDataPhase != NULL) {
    *phadDataPhase = 0;
  }
  Status        = USBH_STATUS_ERROR;
  DataLength    = *pDataLength;
  for (; ;) {
    if (Status == USBH_STATUS_DEVICE_REMOVED) {
      return Status;
    } else if (Status != USBH_STATUS_SUCCESS) {
      if (pInst->ErrorCount == BULK_ONLY_MAX_RETRY) {
        pInst->ErrorCount++; // Make sure _UsbDeviceReset is only called once.
        _UsbDeviceReset(pInst);
        return Status;
      } else if (pInst->ErrorCount > BULK_ONLY_MAX_RETRY) {
        return Status;
      } else {
        //
        // MISRA comment
        //
      }
    } else {
      //
      // MISRA comment
      //
    }
    //
    // COMMAND PHASE
    //
    pBuf = pInst->pTempBuf;
    USBH_ZERO_MEMORY(pBuf, CBW_LENGTH);
    _FillCBW(&cbw, 0, DataLength, CBW_FLAG_READ, pUnit->Lun, CmdLength);
    _ConvCommandBlockWrapper(&cbw, pBuf); // Convert the structure CBW to a CBW pBuffer and attach pCmdBuffer
    USBH_MEMCPY(&pBuf[COMMAND_WRAPPER_CDB_OFFSET], pCmdBuffer, CmdLength);
    *pDataLength  = 0;
    Length = CBW_LENGTH;
    _WriteTag(pInst, pBuf);
    Status = _WriteSync(pInst, pBuf, &Length, USBH_MSD_WRITE_TIMEOUT, FALSE, SectorDataFlag);
    if (Status != USBH_STATUS_SUCCESS) {
      pInst->ErrorCount++;
      USBH_WARN((USBH_MCAT_MSC, "_SendCommandReadData: Command Phase"));
      continue;
    }
    //
    // DATA PHASE
    //
    if (DataLength != 0u) {                            // DataLength always contains the original Length
      //
      // If the buffer is a multiple of MaxPacketSize it can be used directly.
      // Otherwise pTempBuf is used to make sure that we have a buffer large
      // enough to hold MaxPacketSize packets.
      //
      if ((DataLength % pInst->BulkMaxPktSize) == 0u) {
        pBuf = pDataBuffer;
        Length = DataLength;
        TempBufferUsed = 0;
      } else {
#if (USBH_DEBUG > 1)
        //
        // Buffers which are not a multiple of MaxPacketSize are only used with the shorter commands.
        // This should not happen.
        //
        if (DataLength > pInst->BulkMaxPktSize) {
          USBH_ASSERT0;
        }
#endif
        Length = pInst->BulkMaxPktSize; // Size of pTempBuf.
        TempBufferUsed = 1;
      }
      Status = _ReadSync(pInst, pBuf, &Length, Timeout, TRUE, SectorDataFlag);
      if (Status != USBH_STATUS_SUCCESS) {                              // Error
        pInst->ErrorCount++;
        USBH_LOG((USBH_MCAT_MSC, "_SendCommandReadData: Data IN Phase failed"));
        if (Status == USBH_STATUS_STALL) {        // Reset the IN pipe
          USBH_LOG((USBH_MCAT_MSC, "DlResetPipe Ep-address: %u",pInst->BulkInEp));
          Status = _ResetPipe(pInst, pInst->BulkInEp);
          if (Status != USBH_STATUS_SUCCESS) {                          // Reset error
            USBH_WARN((USBH_MCAT_MSC, "_SendCommandReadData: reset error! %s", USBH_GetStatusStr(Status)));
            return Status;
          }
        } else {
          USBH_WARN((USBH_MCAT_MSC, "_SendCommandReadData data: other error (%s), try error recovery!", USBH_GetStatusStr(Status)));
          continue;
        }
      } else {
        //
        // Receiving less data than requested is OK. This is handled by the status phase.
        // Check if we received a CSW instead of data.
        //
        if ((Length % pInst->BulkMaxPktSize) == CSW_LENGTH) {              // Last data packet Length is CSW_LENGTH, check command Status
          if (_ConvBufferToStatusWrapper(pBuf + Length - CSW_LENGTH, Length, &csw) == USBH_STATUS_SUCCESS) {
            if (_IsCSWValidAndMeaningful(pInst, &cbw, &csw, CSW_LENGTH) == TRUE) { // device has stopped the data transfer by sending an CSW
              // This occurs if the toggle bit is not reset after USB clear feature endpoint halt!
              USBH_WARN((USBH_MCAT_MSC, "_SendCommandReadData: device breaks the data phase by sending a CSW: CSW-Status: %d!", (int)csw.Status));
              if (csw.Status != CSW_STATUS_PHASE_ERROR) {                // No phase error
                if (csw.Residue != 0u) { // This is not implemented in the same way from vendors!
                  *pDataLength  = cbw.DataTransferLength - csw.Residue;
                } else {
                  *pDataLength  = Length - CSW_LENGTH;                    // CSW_LENGTH because CSW sent at the end of the pBuffer
                }
                if (csw.Status == CSW_STATUS_FAIL) {
                  Status = USBH_STATUS_COMMAND_FAILED;
                } else {                                                 // on success
                  if (*pDataLength != Length - CSW_LENGTH) {
                    USBH_WARN((USBH_MCAT_MSC, "_SendCommandReadData: invalid Residue!"));
                  }
                }
                break; // This breaks the for loop: indirect return!
              }
              continue; // Repeat all
            }
          }
        } else {
          //
          // When pTempBuf was used, copy result to original buffer.
          // Only copy as much as requested, even if the device sends more.
          // Length error handling is done later via the Length variable.
          //
          if (Length != 0u && phadDataPhase != NULL) {
            *phadDataPhase = 1; // Length zero means no data phase for us.
          }
          if (TempBufferUsed == 1u) {
            Length = USBH_MIN(Length, DataLength);
            if (Length != 0u) {
              USBH_MEMCPY(pDataBuffer, pBuf, Length);
            }
          }
        }
      }
    }
    //
    // STATUS PHASE
    //
    Status = _ReadCSW(pInst, &cbw, &csw);
    if (Status != USBH_STATUS_SUCCESS) {
      pInst->ErrorCount++;
    } else {
      //
      // Reset error count only upon status stage completion.
      //
      pInst->ErrorCount = 0;
      if (csw.Status != CSW_STATUS_PHASE_ERROR) { // no phase error
        if (csw.Residue != 0u) { // This is not implemented in the same way from vendors!
          USBH_WARN((USBH_MCAT_MSC, "_SendCommandReadData: invalid Residue! Expected:0 rcv:%d!", csw.Residue));
        }
        *pDataLength  = Length;
        if (csw.Status == CSW_STATUS_FAIL) {
          Status = USBH_STATUS_COMMAND_FAILED;
        }
        break; // Return
      }
    }
  }
  return Status;
}

/*********************************************************************
*
*       _Inquiry
*
*  Function description
*    Returns product pData from the device.
*
*  Parameters
*    pUnit:   Pointer to the unit object.
*    pData:   OUT: pData.
*    pLength: OUT: Length of valid bytes in pData.
*    Select:  Select the returned pData format.
*    CmdPage: Specify the page number, not used if the parameter Select is equal Standard.
*/
static USBH_STATUS _Inquiry(const USBH_MSD_UNIT * pUnit, U8 * pData, U32 * pLength, INQUIRY_SELECT Select, U8 CmdPage) {
  U32            Length;
  SCSI_6BYTE_CMD aCommand;
  USBH_STATUS    Status;

  USBH_LOG((USBH_MCAT_MSC, "MSD SC6: _Inquiry"));
  USBH_ASSERT_PTR(pData);
  USBH_ASSERT_PTR(pLength);
  if (pUnit->pInst == NULL) {
    USBH_WARN((USBH_MCAT_MSC, "MSD SC6: _Inquiry: Device removed!"));
    return USBH_STATUS_INVALID_PARAM;
  }
  USBH_ASSERT_MAGIC(pUnit->pInst, USBH_MSD_INST);
  USBH_ASSERT_PTR(pUnit->pInst->hInterface);
  USBH_ZERO_STRUCT(aCommand);
  aCommand.Cmd    = SC_INQUIRY;
  aCommand.Length = STANDARD_INQUIRY_DATA_LENGTH;
  Length          = STANDARD_INQUIRY_DATA_LENGTH;
  *pLength        = 0;
  Status          = USBH_STATUS_SUCCESS;
  switch (Select) {
  case Standard:
    break;
  case Productpage:
    aCommand.Index1 = INQUIRY_ENABLE_PRODUCT_DATA;
    aCommand.Index2 = CmdPage;
    break;
  case CommandSupport:
    aCommand.Index1 = INQUIRY_ENABLE_COMMAND_SUPPORT;
    aCommand.Index2 = CmdPage;
    break;
  default:
    USBH_WARN((USBH_MCAT_MSC, "MSD SC6: _Inquiry: invalid value for parameter Select!"));
    Status = USBH_STATUS_INVALID_PARAM;
    break;
  }
  if (Status != USBH_STATUS_SUCCESS) { // On error
    return Status;
  }
  Status = _SendCommandReadData(pUnit, (U8 * ) &aCommand, sizeof(aCommand), pData, &Length, USBH_MSD_COMMAND_TIMEOUT, FALSE, NULL);
  if (Status != USBH_STATUS_SUCCESS) {
    USBH_WARN((USBH_MCAT_MSC, "MSD SC6: _Inquiry failed: %s", USBH_GetStatusStr(Status)));
  } else {
    *pLength = Length;
  }
  return Status;
}

/*********************************************************************
*
*       _Conv10ByteCommand
*
*  Function description
*    Returns a 10 byte aCommand descriptor block
*    IN: valid pointer; OUT: pData in descriptor
*/
static void _Conv10ByteCommand(U8 OpCode, U32 Address, U16 Length, U8 * pCommand) {
  USBH_ASSERT_PTR(pCommand);
  USBH_ZERO_MEMORY(pCommand, SCSI_10BYTE_COMMAND_LENGTH);
  pCommand[0] = OpCode;
  USBH_StoreU32BE(&pCommand[2], Address); // Address
  USBH_StoreU16BE(&pCommand[7], Length);  // TransferLength
}

/*********************************************************************
*
*       _ConvReadCapacity
*
*  Function description
*    USBH_MSD_ConvReadCapacity returns values taken from the received
*    SC_READ_CAPACITY command data block. This function is independent
*    from the byte order of the processor.
*
*  Parameters
*    pData            : Received SC_READ_CAPACITY data block.
*    Length           : Length of data block.
*    pMaxBlockAddress : [IN] Valid pointer to a U32.
*                       [OUT] The last possible block address.
*    pBlockLength     : [IN] Valid pointer to a U32.
*                       [OUT] The number of bytes per sector.
*
*  Return value
*    int
*/
static USBH_STATUS _ConvReadCapacity(const U8 * pData, U16 Length, U32 * pMaxBlockAddress, U32 * pBlockLength) {
  USBH_ASSERT_PTR(pData);
  USBH_ASSERT_PTR(pMaxBlockAddress);
  USBH_ASSERT_PTR(pBlockLength);
  if (Length < RD_CAPACITY_DATA_LENGTH) {
    return USBH_STATUS_ERROR;
  }
  *pMaxBlockAddress = USBH_LoadU32BE(pData);     // Last possible block address
  *pBlockLength = USBH_LoadU32BE(pData + 4); // Number of bytes per sector
  return USBH_STATUS_SUCCESS;
}


/*********************************************************************
*
*       _ReadCapacity
*
*  Function description
*    Sends a standard READ CAPACITY aCommand to the device.
*    The result is stored in the parameters.
*
*  Parameters
*    pUnit:             Pointer to the unit object.
*    pMaxSectorAddress: Last sector address.
*    pBytesPerSector:   Sector size in bytes
*/
static USBH_STATUS _ReadCapacity(const USBH_MSD_UNIT * pUnit, U32 * pMaxSectorAddress, U16 * pBytesPerSector) {
  U32           Length;
  U8            aCommand[SCSI_10BYTE_COMMAND_LENGTH];
  USBH_STATUS   Status;
  U8            acBuf[RD_CAPACITY_DATA_LENGTH];

  USBH_LOG((USBH_MCAT_MSC, "MSD SC6: _ReadCapacity"));
  USBH_ASSERT_PTR(pMaxSectorAddress);
  USBH_ASSERT_PTR(pBytesPerSector);
  if (pUnit->pInst == NULL) {
    USBH_WARN((USBH_MCAT_MSC, "MSD SC6: _ReadCapacity: Device removed!"));
    return USBH_STATUS_INVALID_PARAM;
  }
  USBH_ASSERT_MAGIC(pUnit->pInst, USBH_MSD_INST);
  USBH_ASSERT_PTR(pUnit->pInst->hInterface);
  *pMaxSectorAddress = 0;
  *pBytesPerSector   = 0;
  Length             = RD_CAPACITY_DATA_LENGTH;
  // The Length field in the SCSI aCommand must be zero
  _Conv10ByteCommand(SC_READ_CAPACITY, 0, 0, aCommand);
  Status = _SendCommandReadData(pUnit, aCommand, (U8)sizeof(aCommand), acBuf, &Length, USBH_MSD_COMMAND_TIMEOUT, FALSE, NULL);
  if (Status != USBH_STATUS_SUCCESS) {
    USBH_WARN((USBH_MCAT_MSC, "MSD SC6: _ReadCapacity failed, Error=%s", USBH_GetStatusStr(Status)));
  } else { // On success
    U32 BytesPerSector;

    if (_ConvReadCapacity(acBuf, (U16)Length, pMaxSectorAddress, &BytesPerSector) != USBH_STATUS_SUCCESS) {
      USBH_WARN((USBH_MCAT_MSC, "MSD SC6: _ReadCapacity: Length: %u",Length));
    } else {
      *pBytesPerSector = (U16)BytesPerSector;
    }
  }
  return Status;
}

/*********************************************************************
*
*       _TestUnitReady
*
*  Function description
*    Checks if the device is ready, if the command fails, a sense command is generated.
*/
static USBH_STATUS _TestUnitReady(USBH_MSD_UNIT * pUnit) {
  U32            Length;
  SCSI_6BYTE_CMD Command;
  USBH_STATUS    Status;
  USBH_STATUS    StatusSense;

  USBH_LOG((USBH_MCAT_MSC, "MSD SC6: _TestUnitReady"));
  USBH_ZERO_STRUCT(Command);
  Command.Cmd = SC_TEST_UNIT_READY;
  Length = 0;
  Status = _SendCommandWriteData(pUnit, (U8 *) &Command, sizeof(Command), (U8 * )NULL, &Length, USBH_MSD_COMMAND_TIMEOUT, FALSE);
  if ((Status != USBH_STATUS_SUCCESS) && (Status != USBH_STATUS_DEVICE_REMOVED)) {
    StatusSense = USBH_MSD__RequestSense(pUnit);
    //
    // When TestUnitReady returns a "not ready" state and RequestSense returns SenseKey "unit attention" a new medium has been inserted. (e.g. SD card)
    // Unit data has to be invalidated.
    //
    if (StatusSense == USBH_STATUS_SUCCESS) {
      if (pUnit->Sense.Sensekey == SS_SENSE_UNIT_ATTENTION) {
        pUnit->BytesPerSector = 0;
        pUnit->MaxSectorAddress = 0;
        USBH_MEMSET(&pUnit->ModeParamHeader, 0, sizeof(MODE_PARAMETER_HEADER));
      }
      USBH_WARN((USBH_MCAT_MSC, "MSD SC6: _TestUnitReady [LUN %d, DevIndex %d]: Command failed: 0x%8x:0x%8x:0x%8x", pUnit->Lun, pUnit->pInst->DeviceIndex, pUnit->Sense.Sensekey, pUnit->Sense.Sensecode, pUnit->Sense.Sensequalifier));
    }
#if (USBH_DEBUG > 1)
     else {
      USBH_WARN((USBH_MCAT_MSC, "MSD SC6: _TestUnitReady [LUN %d, DevIndex %d]: USBH_MSD__RequestSense failed: %s", USBH_GetStatusStr(StatusSense)));
    }
#endif
  }
  return Status;
}

/*********************************************************************
*
*       _ModeSense
*
*  Function description
*    Returns some parameters of the device
*
*  Parameters
*    pUnit:   Pointer to the unit object.
*    pData:   pData Buffer
*    pLength: IN: max. length in bytes of pData; OUT: received length
*    pHeader: Converted mode parameter header values located at the beginning of the pData Buffer
*    Page:    Command page.
*    PageControlCode: Command page control code.
*/
static USBH_STATUS _ModeSense(const USBH_MSD_UNIT * pUnit, U8 * pData, U8 * pLength, MODE_PARAMETER_HEADER * pHeader, U8 Page, U8 PageControlCode) {
  U32            Length;
  SCSI_6BYTE_CMD Command; // Byte array, no converting is needed
  USBH_STATUS    Status;

  USBH_LOG((USBH_MCAT_MSC, "MSD SC6: _ModeSense"));
  USBH_ASSERT_PTR(pData);
  USBH_ASSERT_PTR(pLength);
  USBH_ASSERT_PTR(pHeader);
  if (pUnit->pInst == NULL) {
    USBH_WARN((USBH_MCAT_MSC, "MSD SC6: _ModeSense: Device removed!"));
    return USBH_STATUS_INVALID_PARAM;
  }
  USBH_ASSERT_MAGIC(pUnit->pInst, USBH_MSD_INST);
  USBH_ASSERT_PTR(pUnit->pInst->hInterface);
  USBH_ZERO_STRUCT(Command);
  Command.Cmd = SC_MODE_SENSE_6;
  Command.Index1 = (U8)((pUnit->Lun << 5) & 0xE0u);
  Command.Index2 = (U8)(Page | PageControlCode);
  Length         = *pLength;
  Command.Length = *pLength;
  *pLength       = 0;
  Status = _SendCommandReadData(pUnit, (U8 * ) &Command, sizeof(Command), pData, &Length, USBH_MSD_COMMAND_TIMEOUT, FALSE, NULL);
  if (Status != USBH_STATUS_SUCCESS) {
    USBH_WARN((USBH_MCAT_MSC, "MSD SC6: _ModeSense: failed, Error=%s", USBH_GetStatusStr(Status)));
  } else {
    if (Length < SC_MODE_PARAMETER_HEADER_LENGTH_6 || Length > 0xFFu) {
      return USBH_STATUS_LENGTH;
    } else {
      * pLength = (U8)Length;
      USBH_MSD_ConvModeParameterHeader(pHeader, pData, TRUE); // TRUE=6-byte command
    }
  }
  return Status;
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
*     A device index or USBH_MSD_MAX_DEVICES in case all device indexes are allocated.
*/
static U8 _AllocateDevIndex(void) {
  U8 i;
  U32 Mask;

  USBH_OS_Lock(USBH_MUTEX_MSD);
  Mask = 1;
  for (i = 0; i < USBH_MSD_MAX_DEVICES; ++i) {
    if ((USBH_MSD_Global.DevIndexUsedMask & Mask) == 0u) {
      USBH_MSD_Global.DevIndexUsedMask |= Mask;
      break;
    }
    Mask <<= 1;
  }
  USBH_OS_Unlock(USBH_MUTEX_MSD);
  return i;
}

/*********************************************************************
*
*       _AllocLuns
*
*  Function description
*    Allocates logical units, saves the unit pointer in the device object.
*
*  Parameters
*    pInst  : Pointer to a USB device.
*    NumLUN : Number of units to allocate.
*
*  Return values
*    0 on success
*    other values on error
*/
static USBH_STATUS _AllocLuns(USBH_MSD_INST * pInst, unsigned NumLUN) {
  unsigned        i;
  USBH_STATUS     Status;
  USBH_MSD_UNIT * pUnit;

  USBH_ASSERT_MAGIC(pInst, USBH_MSD_INST);
  Status = USBH_STATUS_SUCCESS;
  //
  // Allocate units and save it in the device object
  //
  USBH_ASSERT(0 == pInst->UnitCnt);
  USBH_OS_Lock(USBH_MUTEX_MSD);
  for (i = 0; i < NumLUN; i++) {
    pUnit = (USBH_MSD_UNIT *)USBH_TRY_MALLOC_ZEROED(sizeof(USBH_MSD_UNIT));
    if (pUnit != NULL) {
      pUnit->pInst = pInst;
      pUnit->Lun = (U8)pInst->UnitCnt;       // Start with LUN number zero
      pUnit->BytesPerSector = USBH_MSD_DEFAULT_SECTOR_SIZE;
      //
      // Set the last TestUnitReady timeout.
      // This will make sure TestUnitReady is sent before any other commands.
      //
      pUnit->LastTestUnitReadyTime = (I32)USBH_OS_GetTime32() - (USBH_MSD_TEST_UNIT_READY_DELAY + 1);
      pInst->apUnit[pInst->UnitCnt] = pUnit;     // Save units also in the device
      pInst->UnitCnt++;
    } else {
      USBH_WARN((USBH_MCAT_MSC, "_AllocLuns: Unit could not be allocated."));
      //
      // TODO this does not take into account that some of the units may have been allocated.
      //
      Status = USBH_STATUS_MEMORY;
      break;
    }
  }
  USBH_OS_Unlock(USBH_MUTEX_MSD);
  return Status;
}

/*********************************************************************
*
*       _InitDevice
*
*  Function description
*    Makes an basic initialization of the USBH MSD device object.
*    Physical transfer buffers are allocated if needed.
*/
static USBH_STATUS _InitDevice(USBH_MSD_INST * pInst, USBH_INTERFACE_ID interfaceID) {
  USBH_STATUS Status;

  Status = USBH_STATUS_SUCCESS;
  USBH_LOG((USBH_MCAT_MSC, "USBH_MSD_InitDevObject"));
  USBH_IFDBG(pInst->Magic      = USBH_MSD_INST_MAGIC);
  pInst->IsReady               = FALSE;
  pInst->InterfaceID           = interfaceID;
  pInst->RefCnt                = 1; // Initial reference
  pInst->pTempBuf = (U8 *)USBH_TRY_MALLOC(pInst->BulkMaxPktSize);
  if (NULL == pInst->pTempBuf) {
    Status = USBH_STATUS_MEMORY;
    USBH_WARN((USBH_MCAT_MSC, "_InitDevice: Could not allocate EP0 transfer pBuf!"));
  }
  return Status;
}


/*********************************************************************
*
*       _NewDevice
*
*  Function description
*    Allocates USBH MSD device object and makes an basic initialization. Set
*    the reference counter to one.No unit is available and all function
*    pointers to protocol and transport layer functions are NULL.
*
*  Parameters
*    InterfaceId : In the debug version InterfaceId is checked if in use.
*/
static USBH_MSD_INST * _NewDevice(USBH_INTERFACE_ID InterfaceId) {
  USBH_MSD_INST * pInst;
  USBH_STATUS     Status;

  //
  // Check if max. number of devices allowed is exceeded.
  //
  if ((USBH_MSD_Global.NumDevices + 1u) > USBH_MSD_MAX_DEVICES) {
    USBH_WARN((USBH_MCAT_MSC, "No instance available for creating a new MSD device! (Increase USBH_MSD_MAX_DEVICES)"));
    return NULL;
  }
  //
  // Perform the actual allocation.
  //
  pInst = (USBH_MSD_INST *)USBH_TRY_MALLOC_ZEROED(sizeof(USBH_MSD_INST));
  if (pInst != NULL) {
    USBH_OS_Lock(USBH_MUTEX_MSD);
    pInst->DeviceIndex      = _AllocateDevIndex();
    Status = _InitDevice(pInst, InterfaceId);
    if (Status != USBH_STATUS_SUCCESS) { // On error
      USBH_WARN((USBH_MCAT_MSC, "_NewDevice: _InitDevice failed (%s)!", USBH_GetStatusStr(Status)));
      _FreeDevIndex(pInst->DeviceIndex);
      USBH_FREE(pInst);
      pInst = NULL;
    } else {
      pInst->pNext = USBH_MSD_Global.pFirst;
      USBH_MSD_Global.pFirst = pInst;
      USBH_MSD_Global.NumDevices++;
    }
    USBH_OS_Unlock(USBH_MUTEX_MSD);
  }
  return pInst;
}

/*********************************************************************
*
*       _GetAndSaveEndpointInformation
*
*  Function description
*    Retrieves the MSD relevant information (MaxPacketSize and address)
*    from both bulk endpoint descriptors.
*
*  Parameters
*    pInst   : Pointer to a MSD device object.
*
*  Return value
*    USBH_STATUS_SUCCESS : The device was reset successfully.
*    Other value         : An error occurred.
*/
static USBH_STATUS _GetAndSaveEndpointInformation(USBH_MSD_INST * pInst) {
  USBH_STATUS   Status;
  USBH_EP_MASK  EpMask;
  unsigned      Count;
  U8            Desc[USB_ENDPOINT_DESCRIPTOR_LENGTH];

  USBH_ASSERT_MAGIC(pInst, USBH_MSD_INST);
  // Get bulk IN endpoint
  EpMask.Mask      = USBH_EP_MASK_DIRECTION | USBH_EP_MASK_TYPE;
  EpMask.Direction = USB_TO_HOST;
  EpMask.Type      = USB_EP_TYPE_BULK;
  Count             = sizeof(Desc);
  Status            = USBH_GetEndpointDescriptor(pInst->hInterface, 0, &EpMask, Desc, &Count);
  if (Status != USBH_STATUS_SUCCESS || Count != USB_ENDPOINT_DESCRIPTOR_LENGTH) {
    USBH_WARN((USBH_MCAT_MSC, "Failed to get BulkEP In (%s)", USBH_GetStatusStr(Status)));
    return Status;
  }
  // Save information
  pInst->BulkMaxPktSize = USBH_LoadU16LE(&Desc[USB_EP_DESC_PACKET_SIZE_OFS]);
  pInst->BulkInEp       = Desc[USB_EP_DESC_ADDRESS_OFS];
  // Use previous mask change direction to bulk OUT
  EpMask.Direction = 0;
  Count = sizeof(Desc);
  Status           = USBH_GetEndpointDescriptor(pInst->hInterface, 0, &EpMask, Desc, &Count);
  if (Status != USBH_STATUS_SUCCESS) {
    USBH_WARN((USBH_MCAT_MSC, "Failed to get BulkEP Out (%s)", USBH_GetStatusStr(Status)));
    return Status;
  }
  if (pInst->BulkMaxPktSize != USBH_LoadU16LE(&Desc[USB_EP_DESC_PACKET_SIZE_OFS])) {
    USBH_WARN((USBH_MCAT_MSC, "USBH_MSD_GetAndSaveProtocolEndpoints: different max.packet sizes between ep: 0x%x and ep: 0x%x", pInst->BulkInEp, Desc[USB_EP_DESC_ADDRESS_OFS]));
    return USBH_STATUS_LENGTH;
  }
  pInst->BulkOutEp = Desc[USB_EP_DESC_ADDRESS_OFS];
  return USBH_STATUS_SUCCESS;
}

/*********************************************************************
*
*       _ValidateInterface
*
*  Function description
*    Check whether the given interface is a valid MSD interface.
*
*  Parameters
*    pInfo : Pointer to a structure holding the interface data.
*
*  Return value
*    USBH_STATUS_SUCCESS : The given interface is a valid MSD interface.
*    Other value         : An error occurred/Interface not valid.
*/
static USBH_STATUS _ValidateInterface(const USBH_INTERFACE_INFO * pInfo) {
  USBH_STATUS Status;

  Status = USBH_STATUS_SUCCESS;
  if (pInfo->Class != MASS_STORAGE_CLASS) {
    USBH_WARN((USBH_MCAT_MSC, ": USBH_MSD_CheckInterface: Invalid device class: %u",(unsigned int)pInfo->Class));
    Status = USBH_STATUS_ERROR;
    goto Exit;
  }
  if (pInfo->SubClass != SUBCLASS_6) {
    USBH_WARN((USBH_MCAT_MSC, "USBH_MSD_CheckInterface: Invalid sub class: %u",(unsigned int)pInfo->SubClass));
    Status = USBH_STATUS_INTERFACE_SUB_CLASS;
    goto Exit;
  }
  if (pInfo->Protocol != PROTOCOL_BULK_ONLY) {
    USBH_WARN((USBH_MCAT_MSC, "USBH_MSD_CheckInterface: Invalid interface protocol: %u",(unsigned int)pInfo->Protocol));
    Status = USBH_STATUS_INTERFACE_PROTOCOL;
  }
Exit:
  return Status;
}

/*********************************************************************
*
*       _CheckAndOpenInterface
*
*  Function description
*    _CheckAndOpenInterface checks if the interface contains an valid
*    USB mass storage class interface.
*
*  Return value
*    !0: error
*     0: no error
*/
static USBH_STATUS _CheckAndOpenInterface(USBH_MSD_INST * pInst) {
  USBH_STATUS         Status;
  USBH_INTERFACE_INFO InterFaceInfo;

  USBH_ASSERT_MAGIC(pInst, USBH_MSD_INST);
  USBH_LOG((USBH_MCAT_MSC, "_CheckAndOpenInterface"));
  if (pInst->Removed == TRUE) {
    USBH_WARN((USBH_MCAT_MSC, "_CheckAndOpenInterface: Device removed!"));
    return USBH_STATUS_INVALID_PARAM;
  }
  Status = USBH_GetInterfaceInfo(pInst->InterfaceID, &InterFaceInfo);
  if (USBH_STATUS_SUCCESS != Status) {
    USBH_WARN((USBH_MCAT_MSC, "_CheckAndOpenInterface: interface info failed %s", USBH_GetStatusStr(Status)));
    return Status;
  }
  Status = _ValidateInterface(&InterFaceInfo);
  if (USBH_STATUS_SUCCESS != Status) {
    USBH_WARN((USBH_MCAT_MSC, "_CheckAndOpenInterface: invalid mass storage interface %s", USBH_GetStatusStr(Status)));
    return Status;
  } else {
    //
    // Set the device interface ID (not the emUSB-Host interface ID) in the
    // device instance. This is especially important for GetMAXLUN as it requires
    // the correct device interface number.
    //
    pInst->bInterfaceNumber = InterFaceInfo.Interface;
  }
  Status = USBH_OpenInterface(pInst->InterfaceID, 0, &pInst->hInterface); // Open interface exclusive
  if (Status != USBH_STATUS_SUCCESS) {
    USBH_WARN((USBH_MCAT_MSC, "_CheckAndOpenInterface: USBH_OpenInterface Status = %s", USBH_GetStatusStr(Status)));
    return Status;
  }
  Status = _GetAndSaveEndpointInformation(pInst);                         // Save endpoint information
  if (Status != USBH_STATUS_SUCCESS) {                                                            // Error
    USBH_WARN((USBH_MCAT_MSC, "_CheckAndOpenInterface: USBH_MSD_GetAndSaveProtocolEndpoints!"));
    return Status;
  }
  return Status;
}

/*********************************************************************
*
*       _StartDevice
*
*  Function description
*    First configures the device by a call to DlInitDevice, then queries
*    the number of LUNs for the device, after that it allocates the LUNs
*    of the device and finally initializes the device.
*/
static USBH_STATUS _StartDevice(USBH_MSD_INST * pInst) {
  USBH_STATUS Status;
  unsigned    NumLuns;
  unsigned    MaxLun;
  unsigned    i;
  unsigned    j;

  USBH_ASSERT_MAGIC(pInst, USBH_MSD_INST);
  USBH_LOG((USBH_MCAT_MSC, "_StartDevice IN-Ep: 0x%x Out-Ep: 0x%x",pInst->BulkInEp,pInst->BulkOutEp));
  if (pInst->Removed == TRUE) {
    USBH_WARN((USBH_MCAT_MSC, "_StartDevice: Device removed!"));
    return USBH_STATUS_INVALID_PARAM;
  }
  Status = _BULKONLY_GetMaxLUN(pInst, &MaxLun);
  if (Status != USBH_STATUS_SUCCESS) {                                       // On error
    if (Status == USBH_STATUS_STALL) {                // stall is allowed
      MaxLun = 0u;
    } else {
      USBH_WARN((USBH_MCAT_MSC, "_StartDevice: GetMaxLUN: Status = %s", USBH_GetStatusStr(Status)));
      return Status;
    }
  }
  NumLuns = MaxLun + 1u; // MaxLun == 0 means one LUN.
  if (USBH_MSD_Global.NumLUNs + MaxLun >= USBH_MSD_MAX_UNITS) {
    USBH_WARN((USBH_MCAT_MSC, "_StartDevice: Error: Allocated LUNs %d, new device has %d LUNs, USBH_MSD_MAX_UNITS exceeded.", USBH_MSD_Global.NumLUNs, MaxLun));
    Status = USBH_STATUS_ERROR;
  } else {
    Status = _AllocLuns(pInst, NumLuns);                  // Allocate the logical units for this device
    if (Status != USBH_STATUS_SUCCESS) {
      USBH_WARN((USBH_MCAT_MSC, "_StartDevice: no LUN resources"));
      return Status;
    }
    Status = USBH_MSD_PHY_InitSequence(pInst); // Initialize the device with a protocol specific sequence
    if (Status == USBH_STATUS_SUCCESS) {
      //
      // Initialization complete, add units to the global unit array.
      //
      USBH_MSD_Global.NumLUNs += pInst->UnitCnt;
      j = 0;
      for (i = 0u; i < USBH_MSD_MAX_UNITS; i++) {
        if (USBH_MSD_Global.apLogicalUnit[i] == NULL) {
          USBH_MSD_Global.apLogicalUnit[i] = pInst->apUnit[j];
          j++;
        }
        if (pInst->apUnit[j] == NULL) {
          break;
        }
      }
    }
  }
  return Status;
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
*
*  Return value
*    == USBH_STATUS_SUCCESS: TestUnitReady successfully completed.
*    != USBH_STATUS_SUCCESS: An error occurred.
*/
static USBH_STATUS _SendTestUnitReadyIfNecessary(USBH_MSD_UNIT * pUnit) {
  USBH_MSD_INST * pInst;
  USBH_STATUS Status;
  I32 t;

  t = (I32)USBH_OS_GetTime32();
  Status = USBH_STATUS_SUCCESS;
  if ((t - pUnit->LastTestUnitReadyTime) >= USBH_MSD_TEST_UNIT_READY_DELAY) {
    pInst = pUnit->pInst;
    USBH_ASSERT_MAGIC(pInst, USBH_MSD_INST);
    _IncRefCnt(pInst);
    Status = _TestUnitReady(pUnit);
    _DecRefCnt(pInst);
    //
    // In case the TestUnitReady did not succeed we do not update the timeout
    // because the medium is not ready and consecutive TestUnitReady commands are necessary.
    //
    if (Status == USBH_STATUS_SUCCESS) {
      pUnit->LastTestUnitReadyTime = t;
    }
  }
  return Status;
}

/*********************************************************************
*
*       _AddDevice
*
*  Function description
*    Adds a USB mass storage interface to the library.
*/
static USBH_STATUS _AddDevice(USBH_INTERFACE_ID InterfaceID) {
  USBH_STATUS     Status;
  USBH_MSD_INST * pInst;

  USBH_LOG((USBH_MCAT_MSC, "_AddDevice:"));
  pInst = _NewDevice(InterfaceID);        // Allocate device, REFCT=1
  if (pInst == NULL) {
    USBH_WARN((USBH_MCAT_MSC, "_AddDevice: USBH_MSD_AllocDevice new device could not be allocated!"));
    return USBH_STATUS_RESOURCES;
  }
  Status = _CheckAndOpenInterface(pInst); // Check the interface descriptor and save endpoint information.
  if (Status != USBH_STATUS_SUCCESS) {
    USBH_WARN((USBH_MCAT_MSC, "_AddDevice:_CheckAndOpenInterface Status = %s", USBH_GetStatusStr(Status)));
    goto Exit;
  }
  Status = USBH_GetMaxTransferSize(pInst->hInterface, pInst->BulkOutEp, &pInst->MaxOutTransferSize);
  if (Status != USBH_STATUS_SUCCESS) {
    goto Exit;
  }
  pInst->MaxOutTransferSize = USBH_MIN(pInst->MaxOutTransferSize, USBH_MSD_MAX_TRANSFER_SIZE);
  Status = USBH_GetMaxTransferSize(pInst->hInterface, pInst->BulkInEp, &pInst->MaxInTransferSize);
  if (Status != USBH_STATUS_SUCCESS) {
    goto Exit;
  }
  pInst->MaxInTransferSize = USBH_MIN(pInst->MaxInTransferSize, USBH_MSD_MAX_TRANSFER_SIZE);
  if (CSW_LENGTH > pInst->BulkMaxPktSize) {
    USBH_WARN((USBH_MCAT_MSC, "Invalid MaxPacketSize %d", pInst->BulkMaxPktSize));
    Status = USBH_STATUS_LENGTH;
    goto Exit;
  }
  pInst->pUrbEvent = USBH_OS_AllocEvent();
  if (pInst->pUrbEvent == NULL) {
    USBH_WARN((USBH_MCAT_MSC, "_AddDevice: USBH_OS_AllocEvent"));
    goto Exit;
  }
  //
  // Surround _StartDevice with refcounts because if a command fails during initialization
  // we do not want the removal timer to delete pInst while we are still in the middle
  // of the initialization.
  //
  _IncRefCnt(pInst);
  Status = _StartDevice(pInst);           // Retrieve information of the mass storage device and save it
  _DecRefCnt(pInst);
  if (Status != USBH_STATUS_SUCCESS) {                            // Operation failed
    USBH_WARN((USBH_MCAT_MSC, "_AddDevice: _StartDevice:Invalid device! Status = %s", USBH_GetStatusStr(Status)));
    goto Exit;
  }
  pInst->IsReady = TRUE;
  //
  // Call the USBH MSD notification function
  //
  if (USBH_MSD_Global.pfLunNotification != NULL) {
    USBH_MSD_Global.pfLunNotification(USBH_MSD_Global.pContext, pInst->DeviceIndex, USBH_MSD_EVENT_ADD);
  }
Exit:
  if (Status == USBH_STATUS_SUCCESS) {
    USBH_LOG((USBH_MCAT_MSC, "_AddDevice success! LUNs: %d",pInst->UnitCnt));
  }
  return Status;
}

/*********************************************************************
*
*       _OnDeviceNotify
*
*  Function description
*    Called if a USB Mass storage interface is found.
*/
static void _OnDeviceNotify(void * Context, USBH_PNP_EVENT Event, USBH_INTERFACE_ID InterfaceID) {
  USBH_STATUS Status;
  USBH_MSD_INST * pInst;
  USBH_BOOL       Found;

  USBH_USE_PARA(Context);
  Found = FALSE;
  switch (Event) {
  case USBH_ADD_DEVICE:
    USBH_LOG((USBH_MCAT_MSC, "_OnDeviceNotify: USBH_ADD_DEVICE InterfaceId: %u !",InterfaceID));
    Status = _AddDevice(InterfaceID);
    if ((Status != USBH_STATUS_DEVICE_REMOVED) && (Status != USBH_STATUS_SUCCESS)) {
      if (USBH_MSD_Global.pfLunNotification != NULL) {
        USBH_MSD_Global.pfLunNotification(USBH_MSD_Global.pContext, 0xff, USBH_MSD_EVENT_ERROR);
      }
    }
    break;
  case USBH_REMOVE_DEVICE:
    USBH_LOG((USBH_MCAT_MSC, "_OnDeviceNotify: USBH_REMOVE_DEVICE InterfaceId: %u !",InterfaceID));
      pInst = USBH_MSD_Global.pFirst;
    while (pInst != NULL) {   // Iterate over all instances
      if (pInst->InterfaceID == InterfaceID) {
        Found = TRUE;
        break;
      }
      pInst = pInst->pNext;
    }
    if (Found == TRUE) {
      _MarkDeviceAsRemoved(pInst);
    } else {
      USBH_WARN((USBH_MCAT_MSC, "_MarkDeviceAsRemoved: no device found!"));
    }
    break;
  default:
    // Do nothing.
    break;
  }
}

/*********************************************************************
*
*       _Id2Text
*/
#if (USBH_DEBUG > 1)
static const char * _Id2Text(int Id, const STATUS_TEXT_TABLE * pTable, unsigned NumItems) {
  unsigned i;
  for (i = 0; i < NumItems; i++) {
    if (pTable->Id == Id) {
      return pTable->sText;
    }
    pTable++;
  }
  return "";
}

/*********************************************************************
*
*       _PlWPrintInquiryData
*/
static void _PlWPrintInquiryData(INQUIRY_STANDARD_RESPONSE * data) {
  USBH_LOG((USBH_MCAT_MSC, "Inquiry type: %s", _Id2Text((data->DeviceType & INQUIRY_DEVICE_TYPE_MASK), _aDevTypeTable, SEGGER_COUNTOF(_aDevTypeTable))));
  if (data->RMB & INQUIRY_REMOVE_MEDIA_MASK) {                            // If device is removable
    USBH_LOG((USBH_MCAT_MSC, "Inquiry data:    Medium is removeable!"));
  }
  USBH_LOG((USBH_MCAT_MSC, "Inquiry version:", _Id2Text((data->Version & INQUIRY_VERSION_MASK), _aVersionTable, SEGGER_COUNTOF(_aVersionTable))));
  USBH_LOG((USBH_MCAT_MSC, "Format:", _Id2Text((data->ResponseFormat & INQUIRY_RESPONSE_FORMAT_MASK), _aResponseFormatTable, SEGGER_COUNTOF(_aResponseFormatTable))));
}
#endif

/*********************************************************************
*
*       _CheckInquiryData
*
*  Function description
*    Checks whether the device (USB floppy, direct memory access and hard disk)
*    can be handled by us.
*
*  Return value
*    == TRUE     :  Device can be handled by us
*    == FALSE    :  Device is a type that can not be handled by us.
*/
static USBH_BOOL _CheckInquiryData(const INQUIRY_STANDARD_RESPONSE * pData) {
  if ((pData->DeviceType & INQUIRY_DEVICE_TYPE_MASK) != INQUIRY_DIRECT_DEVICE) { // No direct access device
    return FALSE;
  } else {
    return TRUE;
  }
}

/*********************************************************************
*
*       _InquiryDevice
*
*  Function description
*    Sends the standard INQUIRY command to the device and checks important parameters.
*    The device must be a direct access device.
*
*  Return value
*    == 0        :  Success
*    != 0        :  Error
*/
static USBH_STATUS _InquiryDevice(const USBH_MSD_INST * pInst) {
  unsigned    i;
  USBH_STATUS Status;
  U32         NumBytesRead;
  U8          acBuf[STANDARD_INQUIRY_DATA_LENGTH];

  USBH_ASSERT_MAGIC(pInst, USBH_MSD_INST);
  Status = USBH_STATUS_ERROR;
  for (i = 0; i < pInst->UnitCnt; i++) { // Call all units
    Status = _Inquiry(pInst->apUnit[i], &acBuf[0], &NumBytesRead, Standard, 0);
    if (Status == USBH_STATUS_SUCCESS) {
      if (NumBytesRead != STANDARD_INQUIRY_DATA_LENGTH) {
        Status = USBH_STATUS_LENGTH;
      }
    }
    if (Status != USBH_STATUS_SUCCESS) { // On error
      USBH_WARN((USBH_MCAT_MSC, "MSD: _InquiryDevice: LUN: %u, error: %s", pInst->apUnit[i]->Lun, USBH_GetStatusStr(Status)));
    } else { // Success, store parameters in the pUnit
      USBH_MEMCPY(&pInst->apUnit[i]->InquiryData, &acBuf[0], sizeof(pInst->apUnit[i]->InquiryData));
#if (USBH_DEBUG > 1)
      USBH_MSD_PLW_PRINT_INQUIRYDATA(&pInst->apUnit[i]->InquiryData);
      if (!_CheckInquiryData(&pInst->apUnit[i]->InquiryData)) {
        unsigned DeviceType;
        //
        // This LUN can not be handled by us since it is not a direct access device (e.g. CDROM/DVD)
        //
        DeviceType = pInst->apUnit[i]->InquiryData.DeviceType & INQUIRY_DEVICE_TYPE_MASK;
        USBH_USE_PARA(DeviceType);
        USBH_WARN((USBH_MCAT_MSC, "MSD: Device can not be handled, device type %s is not supported!", _Id2Text(DeviceType, _aDevTypeTable, SEGGER_COUNTOF(_aDevTypeTable))));
      }
#endif
    }
  }
  return Status;
}

/*********************************************************************
*
*       _ReadLunCapacity
*
*  Function description
*    Executes a READ CAPACITY command on all logical units of the device.
*
*  Return value
*    == 0        :  Success (sector Size and max sector address successfully obtained)
*    != 0        :  Error
*/
static USBH_STATUS _ReadLunCapacity(USBH_MSD_INST * pInst) {
  unsigned        i;
  unsigned        j;
  USBH_STATUS     Status;

  USBH_ASSERT_MAGIC(pInst, USBH_MSD_INST);
  Status      = USBH_STATUS_ERROR;
  _IncRefCnt(pInst);
  for (i = 0; i < pInst->UnitCnt; i++) {
    if (_CheckInquiryData(&pInst->apUnit[i]->InquiryData) == TRUE) {
      if (pInst->apUnit[i]->pInst == pInst) {
        for (j = 0; j < USBH_MSD_READ_CAP_MAX_RETRIES; j++) {
          Status = _ReadCapacity(pInst->apUnit[i], &pInst->apUnit[i]->MaxSectorAddress, &pInst->apUnit[i]->BytesPerSector); // Read the capacity of the logical unit
          if (Status != USBH_STATUS_SUCCESS) {
            if (Status == USBH_STATUS_DEVICE_REMOVED) {
              _DecRefCnt(pInst);
              return Status;
            }
            //
            // Tricky: Error status is overwritten by RequestSense status.
            // This results in this function succeeding even if it can not retrieve the capacity.
            // This allows devices without an actual storage medium (e.g. empty CD-ROM drive, empty card reader)
            // to be enumerated and unit structures to be created. When the user inserts the storage medium at
            // a later point in time functions accessing the unit will succeed.
            // This function still fails when USBH_MSD__RequestSense returns an error.
            //
            Status = USBH_MSD__RequestSense(pInst->apUnit[i]);
            if (Status == USBH_STATUS_SUCCESS) {
              if (pInst->apUnit[i]->Sense.Sensekey == SS_SENSE_UNIT_ATTENTION) {
                USBH_WARN((USBH_MCAT_MSC, "MSD: Lun %d is not ready SS_SENSE_UNIT_ATTENTION", i));
                USBH_OS_Delay(1000);
              } else {
                USBH_WARN((USBH_MCAT_MSC, "MSD: Lun %d is not ready, sense key %d", i, pInst->apUnit[i]->Sense.Sensekey));
                USBH_OS_Delay(10);
              }
            } else if (Status == USBH_STATUS_DEVICE_REMOVED) {
              _DecRefCnt(pInst);
              return Status;
            } else {
              //
              // MISRA comment
              //
            }
            USBH_WARN((USBH_MCAT_MSC, "MSD: _ReadLunCapacity: LUN: %d, Status=%s ", pInst->apUnit[i]->Lun, USBH_GetStatusStr(Status)));
          } else {
            USBH_LOG((USBH_MCAT_MSC, "INFO _ReadLunCapacity LUN: %u max. sector address: %u bytes per sector: %d", pInst->apUnit[i]->Lun, pInst->apUnit[i]->MaxSectorAddress, (int)pInst->apUnit[i]->BytesPerSector));
            break;
          }
        }
      }
    }
  }
  _DecRefCnt(pInst);
  return Status;
}

/*********************************************************************
*
*       _CheckModeParameters
*
*  Function description
*    Sends the SCSI command MODE SENSE with the parameter MODE_SENSE_RETURN_ALL_PAGES to get
*    all supported parameters of all pages. Only the mode parameter ModeHeader is stored in the
*    unit object of the device. This ModeHeader is used to detect if the unit is write protected.
*
*  Return value
*    == 0        :  Success
*    != 0        :  Error
*/
static USBH_STATUS _CheckModeParameters(USBH_MSD_INST * pInst) {
  unsigned                i;
  USBH_STATUS             Status;
  U8                      Size;
  U8                      Buffer[MODE_SENSE_PARAMETER_LENGTH];
  MODE_PARAMETER_HEADER   ModeHeader;

  USBH_ASSERT_MAGIC(pInst, USBH_MSD_INST);
  Status = USBH_STATUS_ERROR;
  _IncRefCnt(pInst);
  for (i = 0; i < pInst->UnitCnt; i++) { // Call all units
    if (_CheckInquiryData(&pInst->apUnit[i]->InquiryData) == TRUE) {
      if (pInst->apUnit[i]->pInst == pInst) {
        Size = sizeof(ModeHeader);
        Status = _ModeSense(pInst->apUnit[i], &Buffer[0], &Size, &ModeHeader, MODE_SENSE_RETURN_ALL_PAGES, 0);
        if (Status != USBH_STATUS_SUCCESS) { // On error
          if (Status == USBH_STATUS_DEVICE_REMOVED) {
            _DecRefCnt(pInst);
            return Status;
          }
          USBH_WARN((USBH_MCAT_MSC, "MSD: _CheckModeParameters: ModeSense, Sensekey: %d", pInst->apUnit[i]->Sense.Sensekey));
          Status = USBH_MSD__RequestSense(pInst->apUnit[i]);
          if (Status != USBH_STATUS_SUCCESS) {
            USBH_WARN((USBH_MCAT_MSC, "MSD: _CheckModeParameters: USBH_MSD__RequestSense failed %s", USBH_GetStatusStr(Status)));
          }
          break;
        } else { // On success, copy the received ModeHeader to the device object!
          USBH_MEMCPY(&pInst->apUnit[i]->ModeParamHeader, &ModeHeader, sizeof(ModeHeader));
        }
      }
    }
  }
  _DecRefCnt(pInst);
  return Status;
}

/*********************************************************************
*
*       _GetUnitPtr
*
*  Function description
*    Get a USBH_MSD_UNIT pointer and increase ref count.
*    Lock + increase ref count to make sure the USBH task does not free the unit.
*    Calling function is responsible for decreasing ref count.
*
*  Return value
*    == NULL :  No valid unit pointer for this unit index.
*    != NULL :  Found valid unit pointer.
*/
static USBH_MSD_UNIT * _GetUnitPtr(U8 Unit) {
  USBH_MSD_UNIT * pUnit;

  if (Unit >= USBH_MSD_MAX_UNITS) {
    return NULL;
  }
  USBH_OS_Lock(USBH_MUTEX_MSD);
  pUnit = USBH_MSD_Global.apLogicalUnit[Unit];
  if (pUnit != NULL) {
    USBH_ASSERT_MAGIC(pUnit->pInst, USBH_MSD_INST);
    _IncRefCnt(pUnit->pInst);
  }
  USBH_OS_Unlock(USBH_MUTEX_MSD);
  return pUnit;
}

/*********************************************************************
*
*       Public code, internal functions
*
**********************************************************************
*/

/*********************************************************************
*
*       USBH_MSD_ConvStandardSense
*
*  Function description
*    USBH_MSD_ConvStandardSense fills out STANDARD_SENSE_DATA with the
*    received SC_REQUEST_SENSE command data. This function is independent
*    from the byte order of the processor.
*
*  Parameters
*    pBuffer : Pointer to the sense data buffer.
*    pSense  : IN: valid pointer; OUT: sense data.
*/
void USBH_MSD_ConvStandardSense(const U8 * pBuffer, STANDARD_SENSE_DATA * pSense) {
  USBH_ASSERT_PTR(pBuffer);
  USBH_ASSERT_PTR(pSense);
  pSense->ResponseCode = pBuffer[0];
  pSense->Obsolete = pBuffer[1];
  pSense->Sensekey = pBuffer[2];
  pSense->Info = USBH_LoadU32BE(&pBuffer[3]);
  pSense->AddLength = pBuffer[7];
  pSense->Cmdspecific = USBH_LoadU32BE(&pBuffer[8]);
  pSense->Sensecode = pBuffer[12];
  pSense->Sensequalifier = pBuffer[13];
  pSense->Unitcode = pBuffer[14];
  pSense->Keyspecific1 = pBuffer[15];
  pSense->Keyspecific2 = pBuffer[16];
  pSense->Keyspecific3 = pBuffer[17];
  //
  // Any additional sense bytes are ignored.
  //
  USBH_LOG((USBH_MCAT_MSC, "USBH_MSD_ConvStandardSense code: 0x%x, sense key: 0x%x, ASC: 0x%x, ASCQ: 0x%x ", pSense->ResponseCode, pSense->Sensekey & 0xf, pSense->Sensecode, pSense->Sensequalifier));
}

/*********************************************************************
*
*       USBH_MSD_ConvModeParameterHeader
*
*  Function description
*    Converts received sense mode data to a structure of type MODE_PARAMETER_HEADER.
*
*  Parameters
*    pModeHeader  : Pointer to the structure to write data into.
*    pBuffer      : Pointer to a buffer from which data will be read.
*    IsModeSense6 : True if mode sense(6) command data is used, else mode sense(10) is used.
*/
void USBH_MSD_ConvModeParameterHeader(MODE_PARAMETER_HEADER * pModeHeader, const U8 * pBuffer, USBH_BOOL IsModeSense6) {
  USBH_ASSERT_PTR(pModeHeader);
  USBH_ASSERT_PTR(pBuffer);
  if (IsModeSense6 == TRUE) { // Mode sense(6)
    pModeHeader->DataLength = pBuffer[MODE_PARAMETER_HEADER_DATA_LENGTH_OFS];                           // One byte
    pModeHeader->MediumType = pBuffer[MODE_PARAMETER_HEADER_MEDIUM_TYPE_OFS_6];
    pModeHeader->DeviceParameter = pBuffer[MODE_PARAMETER_HEADER_DEVICE_PARAM_OFS_6];
    pModeHeader->BlockDescriptorLength = pBuffer[MODE_PARAMETER_HEADER_BLOCK_DESC_LENGTH_OFS_6];
    pModeHeader->DataOffset = MODE_PARAMETER_HEADER_BLOCK_DESC_LENGTH_OFS_6 + 1u;
  } else {            // Mode sense(10)
    pModeHeader->DataLength = USBH_LoadU16BE(pBuffer);                                                 // Data pLength
    pModeHeader->MediumType = pBuffer[MODE_PARAMETER_HEADER_MEDIUM_TYPE_OFS_10];
    pModeHeader->DeviceParameter = pBuffer[MODE_PARAMETER_HEADER_DEVICE_PARAM_OFS_10];
    pModeHeader->BlockDescriptorLength = USBH_LoadU16BE(&pBuffer[MODE_PARAMETER_HEADER_BLOCK_DESC_LENGTH_OFS_10]); // Data pLength
    pModeHeader->DataOffset = MODE_PARAMETER_HEADER_BLOCK_DESC_LENGTH_OFS_6 + 2u;                        // Because the pLength is a 16 bit value
  }
}

/*********************************************************************
*
*       USBH_MSD_PHY_InitSequence
*
*  Function description
*    Sends the init sequence to a device that supports the transparent SCSI protocol
*
*  Return value
*    == 0        :  Success
*    != 0        :  Error
*/
USBH_STATUS USBH_MSD_PHY_InitSequence(USBH_MSD_INST * pInst) {
  USBH_STATUS Status;

  USBH_ASSERT_MAGIC(pInst, USBH_MSD_INST);
  Status = _InquiryDevice(pInst);
  if (Status != USBH_STATUS_SUCCESS) {
    USBH_WARN((USBH_MCAT_MSC, "MSD: USBH_MSD_PHY_InitSequence: _InquiryDevice!"));
    goto Exit;
  }
  Status = _ReadLunCapacity(pInst);     // Query the capacity for all LUNS of this device
  if (Status != USBH_STATUS_SUCCESS) {
    USBH_WARN((USBH_MCAT_MSC, "MSD: USBH_MSD_PHY_InitSequence: _ReadLunCapacity!"));
    goto Exit;
  }
  Status = _CheckModeParameters(pInst); // Check mode parameters
  if (Status != USBH_STATUS_SUCCESS) {
    USBH_WARN((USBH_MCAT_MSC, "MSD: USBH_MSD_PHY_InitSequence: _CheckModeParameters!"));
    goto Exit;
  }
Exit:
  return Status;
}

/*********************************************************************
*
*       USBH_MSD_PHY_IsWriteProtected
*
*  Function description
*    Checks if the specified unit is write protected.
*
*  Parameters
*
*  Return value
*    == TRUE     :  Drive is     write protected
*    == FALSE    :  Drive is not write protected
*/
USBH_BOOL USBH_MSD_PHY_IsWriteProtected(const USBH_MSD_UNIT * pUnit) {
  if ((pUnit->ModeParamHeader.DeviceParameter & MODE_WRITE_PROTECT_MASK) != 0u) {
    return TRUE;
  } else {
    return FALSE;
  }
}

/*********************************************************************
*
*       USBH_MSD__ReadSectorsNoCache
*
*  Function description
*    Reads sectors from a device. The maximum number of sectors that can be read at once is 127!
*
*  Parameters
*    pUnit:          Pointer to a structure that contains the LUN.
*    SectorAddress:  Sector address of the first sector.
*    pData:          pData Buffer.
*    Sectors:        Number of contiguous logical blocks to read, max. 127.
*/
USBH_STATUS USBH_MSD__ReadSectorsNoCache(const USBH_MSD_UNIT * pUnit, U32 SectorAddress, U8 * pData, U16 Sectors) {
  U32         length;
  U32         oldLength;
  U8          aCmd[SCSI_10BYTE_COMMAND_LENGTH];
  USBH_STATUS Status;

  USBH_LOG((USBH_MCAT_MSC, "MSD SC6: USBH_MSD__ReadSectorsNoCache: address: %u, sectors: %u", SectorAddress, Sectors));
  USBH_ASSERT_PTR(pData);
  USBH_ASSERT(Sectors);
  if (pUnit->pInst == NULL) {
    USBH_WARN((USBH_MCAT_MSC, "MSD SC6: USBH_MSD__ReadSectorsNoCache: Device removed!"));
    return USBH_STATUS_INVALID_PARAM;
  }
  USBH_ASSERT_MAGIC(pUnit->pInst, USBH_MSD_INST);
  USBH_ASSERT_PTR(pUnit->pInst->hInterface);
  if (SectorAddress > pUnit->MaxSectorAddress) {
    USBH_WARN((USBH_MCAT_MSC, "MSD SC6: USBH_MSD__ReadSectorsNoCache: invalid sector address! max. address: %u, used address: %u", pUnit->MaxSectorAddress, SectorAddress));
    return USBH_STATUS_INVALID_PARAM;
  }
  length = (U32)Sectors * pUnit->BytesPerSector;
  oldLength = length;
  _Conv10ByteCommand(SC_READ_10, SectorAddress, Sectors, aCmd);
  Status = _SendCommandReadData(pUnit, (U8 *)&aCmd[0], sizeof(aCmd), pData, &length, USBH_MSD_READ_TIMEOUT + Sectors * 10uL, TRUE, NULL);
  if (Status != USBH_STATUS_SUCCESS) {
    USBH_WARN((USBH_MCAT_MSC, "MSD SC6: USBH_MSD__ReadSectorsNoCache failed, Error=%s", USBH_GetStatusStr(Status)));
  } else {
    if (length != oldLength) { // Not all sectors read
      USBH_WARN((USBH_MCAT_MSC, "MSD SC6: USBH_MSD__ReadSectorsNoCache: bytes to read: %u, bytes read: %u", oldLength, length));
      Status = USBH_STATUS_LENGTH;
    } else {
      USBH_LOG((USBH_MCAT_MSC, "MSD SC6: USBH_MSD__ReadSectorsNoCache: bytes read: %u", length));
    }
  }
  return Status;
}

/*********************************************************************
*
*       USBH_MSD__WriteSectorsNoCache
*
*  Function description
*    Writes sectors to a device. The maximum number of sectors that can be written at once is 127!
*
*  Parameters
*    pUnit:          Pointer to a structure that contains the LUN.
*    SectorAddress:  Sector address of the first sector.
*    pData:          pData Buffer.
*    Sectors:        Number of contiguous logical blocks written to the device.
*/
USBH_STATUS USBH_MSD__WriteSectorsNoCache(const USBH_MSD_UNIT * pUnit, U32 SectorAddress, const U8 * pData, U16 Sectors) {
  U32         Length;
  U32         OldLength;
  U8          aCommand[SCSI_10BYTE_COMMAND_LENGTH];
  USBH_STATUS Status;

  USBH_LOG((USBH_MCAT_MSC, "MSD SC6: USBH_MSD__WriteSectorsNoCache: address: %u, sectors: %u", SectorAddress, Sectors));
  USBH_ASSERT_PTR(pData);
  USBH_ASSERT(Sectors);
  if (SectorAddress > pUnit->MaxSectorAddress) {
    USBH_WARN((USBH_MCAT_MSC, "MSD SC6: USBH_MSD__WriteSectorsNoCache: invalid sector address! max. address: %u, used address: %u", pUnit->MaxSectorAddress, SectorAddress));
    return USBH_STATUS_INVALID_PARAM;
  }
  if (USBH_MSD_PHY_IsWriteProtected(pUnit) == TRUE) {                 // Check if unit is write protected
    return USBH_STATUS_WRITE_PROTECT;
  }
  Length = (U32)Sectors * pUnit->BytesPerSector; // pLength = sectors * bytes per sector
  OldLength = Length;
  _Conv10ByteCommand(SC_WRITE_10, SectorAddress, Sectors, aCommand);
  Status = _SendCommandWriteData(pUnit, aCommand, sizeof(aCommand), pData, &Length, USBH_MSD_WRITE_TIMEOUT + Sectors * 10uL, TRUE);
  if (Status != USBH_STATUS_SUCCESS) {
    USBH_WARN((USBH_MCAT_MSC, "MSD SC6: USBH_MSD__WriteSectorsNoCache failed, Error=%s", USBH_GetStatusStr(Status)));
  } else {
    if (Length != OldLength) { // Error, the device must write all bytes
      USBH_WARN((USBH_MCAT_MSC, "MSD SC6: USBH_MSD__WriteSectorsNoCache: bytes to write: %u, bytes written: %u", OldLength, Length));
      Status = USBH_STATUS_LENGTH;
    } else {
      USBH_LOG((USBH_MCAT_MSC, "MSD SC6: USBH_MSD__WriteSectorsNoCache: bytes written: %u", Length));
    }
  }
  return Status;
}

/*********************************************************************
*
*       USBH_MSD__RequestSense
*
*  Function description
*    Issues a REQUEST SENSE command to receive
*    the sense pData for the last requested command.
*    If the application client issues a command other than REQUEST
*    SENSE, the sense pData for the last command is lost.
*
*  Return value
*    ==0:  For success, sense pData is copied to structure pUnit->sense
*    !=0:  For error
*/
USBH_STATUS USBH_MSD__RequestSense(USBH_MSD_UNIT * pUnit) {
  U32               Length;
  SCSI_6BYTE_CMD    Buffer; // Byte array
  USBH_STATUS       Status;
  U8                SenseBuffer[STANDARD_SENSE_LENGTH];
  USBH_MSD_INST   * pInst;
  U8                hadDataPhase;

  USBH_LOG((USBH_MCAT_MSC, "MSD SC6: USBH_MSD__RequestSense"));
  pInst = pUnit->pInst;
  if (pInst == NULL) {
    USBH_WARN((USBH_MCAT_MSC, "MSD SC6: USBH_MSD__RequestSense: Device removed!"));
    return USBH_STATUS_INVALID_PARAM;
  }
  USBH_ASSERT_MAGIC(pInst, USBH_MSD_INST);
  USBH_ASSERT_PTR(pInst->hInterface);
  Length = STANDARD_SENSE_LENGTH;
  USBH_ZERO_STRUCT(Buffer);
  Buffer.Cmd = SC_REQUEST_SENSE;
  Buffer.Length = (U8)Length;
  Status = _SendCommandReadData(pUnit, (U8 *)&Buffer, sizeof(Buffer), &SenseBuffer[0], &Length, USBH_MSD_COMMAND_TIMEOUT, FALSE, &hadDataPhase);
  if (Status != USBH_STATUS_SUCCESS) {                   // On error
    pUnit->Sense.ResponseCode = 0; // invalidate the sense pData
    USBH_WARN((USBH_MCAT_MSC, "MSD SC6: USBH_MSD_RequestSense failed, Error=%s", USBH_GetStatusStr(Status)));
  } else {
    if (Length < STANDARD_SENSE_LENGTH) {
      Status = USBH_STATUS_ERROR;
      USBH_WARN((USBH_MCAT_MSC, "MSD SC6: USBH_MSD_RequestSense failed, Length %d instead of %d", Length, STANDARD_SENSE_LENGTH));
    } else {
      if (hadDataPhase != 0u) {
        USBH_MSD_ConvStandardSense(&SenseBuffer[0], &pUnit->Sense);
      } else {
        Status = USBH_STATUS_ERROR;
        USBH_WARN((USBH_MCAT_MSC, "MSD SC6: USBH_MSD_RequestSense failed, no data stage received."));
      }
    }
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
  USBH_MSD_Global.IsInited = 1;
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

  //
  // 1. Un-register all PnP notifications of the device driver.
  // 2. Release all USBH MSD device  resources and delete the device.
  //
  if (USBH_MSD_Global.hPnPNotify != NULL) {
    USBH_UnregisterPnPNotification(USBH_MSD_Global.hPnPNotify);
    USBH_MSD_Global.hPnPNotify = NULL;
  }
  pInst = USBH_MSD_Global.pFirst;
  while (pInst != NULL) {   // Iterate over all instances
    _MarkDeviceAsRemoved(pInst);
    pInst = pInst->pNext;
  }
  USBH_MSD_Global.IsInited = 0;
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

  USBH_LOG((USBH_MCAT_MSC, "USBH_MSD_ReadSectors: address: %u, sectors: %u",SectorAddress,NumSectors));
  Status = USBH_STATUS_SUCCESS;
  pUnit = _GetUnitPtr(Unit);
  if (pUnit != NULL) {
    USBH_ASSERT_MAGIC(pUnit->pInst, USBH_MSD_INST);
    USBH_ASSERT(NumSectors);
    if (pUnit->pInst->Removed == TRUE) {
      USBH_WARN((USBH_MCAT_MSC, "USBH_MSD_ReadSectors: device removed!"));
      Status = USBH_STATUS_DEVICE_REMOVED;
    }
    if (Status == USBH_STATUS_SUCCESS) {
      Status = _SendTestUnitReadyIfNecessary(pUnit);
      if (Status == USBH_STATUS_SUCCESS) {
        USBH_ASSERT_PTR(pBuffer);
        if (USBH_MSD_Global.pCacheAPI != NULL) {
          Status = USBH_MSD_Global.pCacheAPI->pfReadSectors(pUnit, SectorAddress, pBuffer, (U16)NumSectors); // Read from the device with the correct protocol layer
        } else {
          Status = USBH_MSD__ReadSectorsNoCache(pUnit, SectorAddress, pBuffer, (U16)NumSectors); // Read from the device with the correct protocol layer
        }
        if (Status == USBH_STATUS_COMMAND_FAILED) {
          if (USBH_MSD__RequestSense(pUnit) == USBH_STATUS_SUCCESS) {
            USBH_WARN((USBH_MCAT_MSC, "USBH_MSD_ReadSectors failed, SenseKey = 0x%08x", pUnit->Sense.Sensekey));
          }
          //
          // Set test unit ready time back to trigger the test unit ready command.
          //
          pUnit->LastTestUnitReadyTime = (I32)USBH_OS_GetTime32() - (USBH_MSD_TEST_UNIT_READY_DELAY + 1);
        } else if (Status != USBH_STATUS_SUCCESS) {
          USBH_WARN((USBH_MCAT_MSC, "USBH_MSD_ReadSectors: Status %s", USBH_GetStatusStr(Status)));
          //
          // Set test unit ready time back to trigger the test unit ready command.
          //
          pUnit->LastTestUnitReadyTime = (I32)USBH_OS_GetTime32() - (USBH_MSD_TEST_UNIT_READY_DELAY + 1);
        } else {
          //
          // Update test unit ready time. Even though we did not run TestUnitReady explicitly
          // a successful read means that all is good with the medium, therefore a TestUnitReady is not necessary.
          //
          pUnit->LastTestUnitReadyTime = (I32)USBH_OS_GetTime32();
        }
      }
    }
    _DecRefCnt(pUnit->pInst);
  } else {
    Status = USBH_STATUS_DEVICE_REMOVED;
  }
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

  USBH_LOG((USBH_MCAT_MSC, "USBH_MSD_WriteSectors: address: %u, sectors: %u", SectorAddress, NumSectors));
  Status = USBH_STATUS_SUCCESS;
  pUnit = _GetUnitPtr(Unit);
  if (pUnit != NULL) {
    USBH_ASSERT_MAGIC(pUnit->pInst, USBH_MSD_INST);
    USBH_ASSERT(NumSectors);
    if (pUnit->pInst->Removed == TRUE) {
      USBH_WARN((USBH_MCAT_MSC, "USBH_MSD_WriteSectors: device removed!"));
      Status = USBH_STATUS_DEVICE_REMOVED;
    }
    if (Status == USBH_STATUS_SUCCESS) {
      Status = _SendTestUnitReadyIfNecessary(pUnit);
      if (Status == USBH_STATUS_SUCCESS) {
        USBH_ASSERT_PTR(pBuffer);
        if (USBH_MSD_Global.pCacheAPI != NULL) {
          Status = USBH_MSD_Global.pCacheAPI->pfWriteSectors(pUnit, SectorAddress, pBuffer, (U16)NumSectors); // Read from the device with the correct protocol layer
        } else {
          Status = USBH_MSD__WriteSectorsNoCache(pUnit, SectorAddress, pBuffer, (U16)NumSectors); // Read from the device with the correct protocol layer
        }
        if (Status == USBH_STATUS_COMMAND_FAILED) {
          if (USBH_MSD__RequestSense(pUnit) == USBH_STATUS_SUCCESS) {
            USBH_WARN((USBH_MCAT_MSC, "USBH_MSD_WriteSectors failed, SenseCode = 0x%08x", pUnit->Sense.Sensekey));
          }
          //
          // Set test unit ready time back to trigger the test unit ready command.
          //
          pUnit->LastTestUnitReadyTime = (I32)USBH_OS_GetTime32() - (USBH_MSD_TEST_UNIT_READY_DELAY + 1);
        } else if (Status != USBH_STATUS_SUCCESS) {
          USBH_WARN((USBH_MCAT_MSC, "USBH_MSD_WriteSectors: Status %s", USBH_GetStatusStr(Status)));
          //
          // Set test unit ready time back to trigger the test unit ready command.
          //
          pUnit->LastTestUnitReadyTime = (I32)USBH_OS_GetTime32() - (USBH_MSD_TEST_UNIT_READY_DELAY + 1);
        } else {
          //
          // Update test unit ready time. Even though we did not run TestUnitReady explicitly
          // a successful write means that all is good with the medium, therefore a TestUnitReady is not necessary.
          //
          pUnit->LastTestUnitReadyTime = (I32)USBH_OS_GetTime32();
        }

      }
    }
    _DecRefCnt(pUnit->pInst);
  } else {
    Status = USBH_STATUS_DEVICE_REMOVED;
  }
  return Status;
}

/*********************************************************************
*
*       USBH_MSD_GetStatus
*
*  Function description
*    Checks the Status of a device. Therefore it calls USBH_MSD_GetUnit to
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

  pUnit = _GetUnitPtr(Unit);
  if (pUnit != NULL) {
    //
    // Set test unit ready time back to trigger the test unit ready command.
    // Checking BytesPerSector and MaxSectorAddress makes sure that the command
    // is _not_ sent before the MSD device is properly initialized.
    //
    if (pUnit->BytesPerSector != 0u && pUnit->MaxSectorAddress != 0u) {
      pUnit->LastTestUnitReadyTime = (I32)USBH_OS_GetTime32() - (USBH_MSD_TEST_UNIT_READY_DELAY + 1);
      Status = _SendTestUnitReadyIfNecessary(pUnit);
      if (Status == USBH_STATUS_SUCCESS) {
        USBH_ASSERT_MAGIC(pUnit->pInst, USBH_MSD_INST);
        if (pUnit->pInst->Removed == TRUE) {
          USBH_WARN((USBH_MCAT_MSC, "USBH_MSD_GetStatus: device removed!"));
          Status = USBH_STATUS_DEVICE_REMOVED;
        }
        if (pUnit->pInst->IsReady == FALSE) {
          Status = USBH_STATUS_DEVICE_REMOVED;
        }
      }
    } else {
      Status = USBH_STATUS_ERROR;
    }
    _DecRefCnt(pUnit->pInst);
  } else {
    Status = USBH_STATUS_DEVICE_REMOVED;
  }
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
  USBH_INTERFACE_INFO   IFaceInfo;
  USBH_MSD_UNIT       * pUnit;
  USBH_STATUS           Status;

  USBH_ASSERT_PTR(pInfo);
  Status = USBH_STATUS_SUCCESS;
  pUnit = _GetUnitPtr(Unit);
  if (pUnit != NULL) {
    USBH_ASSERT_MAGIC(pUnit->pInst, USBH_MSD_INST);
    if (pUnit->pInst->Removed == TRUE) {
      USBH_WARN((USBH_MCAT_MSC, "USBH_MSD_GetUnitInfo: device removed!"));
      Status = USBH_STATUS_DEVICE_REMOVED;
    }
    if (Status == USBH_STATUS_SUCCESS) {
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
            Status = _ReadCapacity(pUnit, &pUnit->MaxSectorAddress, &pUnit->BytesPerSector);
          }
        }
        if (Status == USBH_STATUS_SUCCESS) {
          pInfo->WriteProtectFlag = (0u != (pUnit->ModeParamHeader.DeviceParameter & MODE_WRITE_PROTECT_MASK)) ? 1 : 0; //lint !e9031  N:105
          pInfo->BytesPerSector = pUnit->BytesPerSector;
          pInfo->TotalSectors = pUnit->MaxSectorAddress + 1u;
          USBH_MEMCPY(&pInfo->acVendorName[0], &pUnit->InquiryData.aVendorIdentification[0], sizeof(pUnit->InquiryData.aVendorIdentification));
          USBH_MEMCPY(&pInfo->acProductName[0], &pUnit->InquiryData.aProductIdentification[0], sizeof(pUnit->InquiryData.aProductIdentification));
          USBH_MEMCPY(&pInfo->acRevision[0], &pUnit->InquiryData.aRevision[0], sizeof(pUnit->InquiryData.aRevision));
        }
        pInfo->VendorId = IFaceInfo.VendorId;
        pInfo->ProductId = IFaceInfo.ProductId;
      }
    }
    _DecRefCnt(pUnit->pInst);
  } else {
    Status = USBH_STATUS_DEVICE_REMOVED;
  }
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
*    The mask corresponds to the unit IDs. E.g. a mask of 0x0000000C
*    means unit ID 2 and unit ID 3 are available for the device.
*/
USBH_STATUS USBH_MSD_GetUnits(U8 DevIndex, U32 *pUnitMask) {
  U32               UnitMask;
  USBH_MSD_UNIT   * pUnit;
  USBH_MSD_INST   * pInst;
  unsigned          i;
  USBH_BOOL         Found;

  Found = FALSE;
  *pUnitMask = 0;
  USBH_OS_Lock(USBH_MUTEX_DEVICE);
  pInst = USBH_MSD_Global.pFirst;
  while (pInst != NULL) {   // Iterate over all instances
    if (pInst->DeviceIndex == DevIndex) {
      if (pInst->IsReady == FALSE) {
        //
        // Device found but has not been initialized yet.
        //
        break;
      }
      Found = TRUE;
      UnitMask = 0;
      for (i = 0; i < USBH_MSD_MAX_UNITS; i++) {
        if (USBH_MSD_Global.apLogicalUnit[i] != NULL) {
          pUnit = USBH_MSD_Global.apLogicalUnit[i];
          if (pUnit->pInst == pInst) {
            UnitMask |= (1uL << i);
          }
        }
      }
      *pUnitMask = UnitMask;
      break;
    }
    pInst = pInst->pNext;
  }
  USBH_OS_Unlock(USBH_MUTEX_DEVICE);
  if (Found == TRUE) {
    return USBH_STATUS_SUCCESS;
  } else {
    return USBH_STATUS_INVALID_PARAM;
  }
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
  USBH_MSD_UNIT * pUnit;
  USBH_STATUS Status;

  USBH_ASSERT_PTR(pPortInfo);
  pUnit = _GetUnitPtr(Unit);
  if (pUnit != NULL) {
    USBH_OS_Lock(USBH_MUTEX_DEVICE);
    USBH_ASSERT_MAGIC(pUnit->pInst, USBH_MSD_INST);
    if (pUnit->pInst->Removed == TRUE) {
      USBH_WARN((USBH_MCAT_MSC, "USBH_MSD_GetPortInfo: device removed!"));
      Status = USBH_STATUS_DEVICE_REMOVED;
    } else {
      Status = USBH_GetPortInfo(pUnit->pInst->InterfaceID, pPortInfo);
    }
    USBH_OS_Unlock(USBH_MUTEX_DEVICE);
    _DecRefCnt(pUnit->pInst);
  } else {
    Status = USBH_STATUS_DEVICE_REMOVED;
  }
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
