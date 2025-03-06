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
File        : USBH_HID_RC.c
Purpose     : HID plugin for remote control
-------------------------- END-OF-HEADER -----------------------------
*/
/*********************************************************************
*
*       #include section
*
**********************************************************************
*/
#include "USBH_Int.h"
#include "USBH_HID_Int.h"
#include "USBH_Util.h"

/*********************************************************************
*
*       Data structures
*
**********************************************************************
*/
typedef struct {
  U16 ReportId;
  U16 ReportSize;
  U16 VolumeIncrementBitPosStart;
  U16 VolumeIncrementNumBits;
  U16 VolumeDecrementBitPosStart;
  U16 VolumeDecrementNumBits;
  U16 MuteBitPosStart;
  U16 MuteNumBits;
  U16 PlayPauseBitPosStart;
  U16 PlayPauseNumBits;
  U16 ScanNextTrackBitPosStart;
  U16 ScanNextTrackNumBits;
  U16 ScanPreviousTrackBitPosStart;
  U16 ScanPreviousTrackNumBits;
  U16 RepeatBitPosStart;
  U16 RepeatNumBits;
  U16 RandomPlayBitPosStart;
  U16 RandomPlayNumBits;
} HID_RC_INFO;

//
// Structure for every connected device handled by this plugin
//
typedef struct {
#if USBH_DEBUG > 1
  U32                         Magic;
#endif
  USBH_HID_INST             * pInst;
  USBH_HID_HANDLER_HOOK       HandlerHook;
  HID_RC_INFO                 RCInfo;
  U8                          RCInfoFound;
} USBH_HID_RC_INST;

//
// Global data of this plugin
//
typedef struct {
  USBH_HID_ON_RC_FUNC       * pfOnStateChange;
  USBH_HID_DETECTION_HOOK     PluginHook;
} USBH_HID_RC_GLOBAL;

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static USBH_HID_RC_GLOBAL USBH_HID_RC_Global;

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _ParseRCData
*
*  Function description
*    Parse the remote control report data.
*
*  Parameters
*    pContext   : Pointer to the HID_RC instance.
*    pReport    : Report received from the device.
*    ReportLen  : Size of the report.
*    Handled    : Is != 0, if this device is already handled by another plugin.
*/
static int _ParseRCData(void *pContext, const U8 *pReport, unsigned ReportLen, int Handled) {
  USBH_HID_RC_INST      * pInst;
  USBH_HID_INST         * pBaseInst;
  USBH_HID_RC_DATA        RCData;
  const HID_RC_INFO     * pRCInfo;

  USBH_USE_PARA(Handled);
  pInst = USBH_CTX2PTR(USBH_HID_RC_INST, pContext);
  USBH_ASSERT_MAGIC(pInst, HID_GENERIC);
  pBaseInst = pInst->pInst;

  if (USBH_HID_RC_Global.pfOnStateChange != NULL) {
    pRCInfo = &pInst->RCInfo;
    if (ReportLen > 0u && pBaseInst->ReportIDsUsed != 0u) {
      if (*pReport != pRCInfo->ReportId) {
        return 0;
      }
      pReport++;
      ReportLen--;
    }
    if (ReportLen >= pRCInfo->ReportSize) {
      RCData.VolumeIncrement    = USBH_HID__GetBits(pReport, pRCInfo->VolumeIncrementBitPosStart, pRCInfo->VolumeIncrementNumBits);
      RCData.VolumeDecrement    = USBH_HID__GetBits(pReport, pRCInfo->VolumeDecrementBitPosStart, pRCInfo->VolumeDecrementNumBits);
      RCData.Mute               = USBH_HID__GetBits(pReport, pRCInfo->MuteBitPosStart, pRCInfo->MuteNumBits);
      RCData.PlayPause          = USBH_HID__GetBits(pReport, pRCInfo->PlayPauseBitPosStart, pRCInfo->PlayPauseNumBits);
      RCData.ScanNextTrack      = USBH_HID__GetBits(pReport, pRCInfo->ScanNextTrackBitPosStart, pRCInfo->ScanNextTrackNumBits);
      RCData.ScanPreviousTrack  = USBH_HID__GetBits(pReport, pRCInfo->ScanPreviousTrackBitPosStart, pRCInfo->ScanPreviousTrackNumBits);
      RCData.Repeat             = USBH_HID__GetBits(pReport, pRCInfo->RepeatBitPosStart, pRCInfo->RepeatNumBits);
      RCData.RandomPlay         = USBH_HID__GetBits(pReport, pRCInfo->RandomPlayBitPosStart, pRCInfo->RandomPlayNumBits);
      RCData.InterfaceID = pBaseInst->InterfaceID;
      USBH_HID_RC_Global.pfOnStateChange(&RCData);
    }
  }
  return 1;
}

/*********************************************************************
*
*       _FindRCInfo
*
*  Function description
*    Called from report descriptor parser for each field element.
*    Checks for remote control items.
*
*  Parameters
*    Flag        : Bit 0 = 0: Input item
*                  Bit 0 = 1: Output item
*                  Bit 1 = 0: Array item
*                  Bit 1 = 1: Variable item
*    pField      : Field info.
*/
static void _FindRCInfo(unsigned Flag, const HID_FIELD_INFO *pField) {
  U32      Usage;
  unsigned i;
  unsigned BitPosStart;
  unsigned Size;
  USBH_HID_RC_INST * pInst;

  if ((Flag & 1u) != 0u) {
    //
    // Only interested in IN report
    //
    return;
  }
  pInst = USBH_CTX2PTR(USBH_HID_RC_INST, pField->pContext);
  USBH_ASSERT_MAGIC(pInst, HID_GENERIC);
  //
  // To avoid long loops for broken devices that report a large distance between UsageMin and UsageMax,
  // we limit the loops to 256, which is always sufficient for real HID devices.
  //
  for (i = 0; i < 256u; i++) {
    if (pField->UsageMax != 0u) {
      //
      // We have a range of usages
      //
      Usage = pField->UsageMin + i;
      if (Usage > pField->UsageMax) {
        break;
      }
    } else {
      //
      // We have a list of usages
      //
      if (i >= pField->NumUsages) {
        break;
      }
      Usage = pField->Usage[i];     //lint !e661 !e662  N:100
    }
    //USBH_LOG((-1, "+++ Report ID = %x, Usage = %x", pField->ReportId, Usage));
    BitPosStart = pField->InRptLen + i * pField->RptSize;
    Size        = (pField->InRptLen + (unsigned)pField->RptCount * pField->RptSize + 7u) >> 3;
    switch (Usage) {
      case USBH_HID_USAGE_TYPE(USBH_HID_USAGE_PAGE_CONSUMER, USBH_HID_USAGE_CONSUMER_VOLUME_INC):
        pInst->RCInfo.VolumeIncrementBitPosStart = BitPosStart;
        pInst->RCInfo.VolumeIncrementNumBits     = pField->RptSize;
        pInst->RCInfo.ReportId     = pField->ReportId;
        pInst->RCInfoFound = 1;
        //USBH_LOG((-1, "+++  Volume Inc      off=%u", pInst->RCInfo.VolumeIncrementBitPosStart));
        break;
      case USBH_HID_USAGE_TYPE(USBH_HID_USAGE_PAGE_CONSUMER, USBH_HID_USAGE_CONSUMER_VOLUME_DEC):
        pInst->RCInfo.VolumeDecrementBitPosStart = BitPosStart;
        pInst->RCInfo.VolumeDecrementNumBits     = pField->RptSize;
        pInst->RCInfo.ReportId                   = pField->ReportId;
        pInst->RCInfoFound = 1;
        //USBH_LOG((-1, "+++  Volume Dec      off=%u", pInst->RCInfo.VolumeDecrementBitPosStart));
        break;
      case USBH_HID_USAGE_TYPE(USBH_HID_USAGE_PAGE_CONSUMER, USBH_HID_USAGE_CONSUMER_MUTE):
        pInst->RCInfo.MuteBitPosStart = BitPosStart;
        pInst->RCInfo.MuteNumBits     = pField->RptSize;
        pInst->RCInfo.ReportId        = pField->ReportId;
        pInst->RCInfoFound = 1;
        //USBH_LOG((-1, "+++  Mute            off=%u, bits=%u", pInst->RCInfo.MuteBitPosStart, pInst->RCInfo.MuteNumBits));
        break;
      case USBH_HID_USAGE_TYPE(USBH_HID_USAGE_PAGE_CONSUMER, USBH_HID_USAGE_CONSUMER_PLAY_PAUSE):
        pInst->RCInfo.PlayPauseBitPosStart = BitPosStart;
        pInst->RCInfo.PlayPauseNumBits     = pField->RptSize;
        pInst->RCInfoFound = 1;
        //USBH_LOG((-1, "+++  Play/Pause      off=%u", pInst->RCInfo.PlayPauseBitPosStart));
        break;
      case USBH_HID_USAGE_TYPE(USBH_HID_USAGE_PAGE_CONSUMER, USBH_HID_USAGE_CONSUMER_SCAN_NEXT_TRACK):
        pInst->RCInfo.ScanNextTrackBitPosStart = BitPosStart;
        pInst->RCInfo.ScanNextTrackNumBits     = pField->RptSize;
        pInst->RCInfoFound = 1;
        //USBH_LOG((-1, "+++  Scan next track off=%u", pInst->RCInfo.ScanNextTrackBitPosStart));
        break;
      case USBH_HID_USAGE_TYPE(USBH_HID_USAGE_PAGE_CONSUMER, USBH_HID_USAGE_CONSUMER_SCAN_PREV_TRACK):
        pInst->RCInfo.ScanPreviousTrackBitPosStart = BitPosStart;
        pInst->RCInfo.ScanPreviousTrackNumBits     = pField->RptSize;
        pInst->RCInfoFound = 1;
        //USBH_LOG((-1, "+++  Scan prev track off=%u", pInst->RCInfo.ScanPreviousTrackBitPosStart));
        break;
      case USBH_HID_USAGE_TYPE(USBH_HID_USAGE_PAGE_CONSUMER, USBH_HID_USAGE_CONSUMER_REPEAT):
        pInst->RCInfo.RepeatBitPosStart = BitPosStart;
        pInst->RCInfo.RepeatNumBits     = pField->RptSize;
        pInst->RCInfoFound = 1;
        //USBH_LOG((-1, "+++  Repeat          off=%u", pInst->RCInfo.RepeatBitPosStart));
        break;
      case USBH_HID_USAGE_TYPE(USBH_HID_USAGE_PAGE_CONSUMER, USBH_HID_USAGE_CONSUMER_RANDOM_PLAY):
        pInst->RCInfo.RandomPlayBitPosStart = BitPosStart;
        pInst->RCInfo.RandomPlayNumBits     = pField->RptSize;
        pInst->RCInfoFound = 1;
        //USBH_LOG((-1, "+++  Random play     off=%u", pInst->RCInfo.RandomPlayBitPosStart));
        break;
      default:
        Size = 0;
        break;
    }
    if (pInst->RCInfo.ReportSize < Size) {
      pInst->RCInfo.ReportSize = Size;
    }
  }
}

/*********************************************************************
*
*       _RemoveInst
*
*  Function description
*    Remove instance.
*/
static void _RemoveInst(void *pContext) {
  USBH_HID_RC_INST * pInst;

  pInst = USBH_CTX2PTR(USBH_HID_RC_INST, pContext);
  USBH_ASSERT_MAGIC(pInst, HID_GENERIC);
  USBH_FREE(pInst);
}

/*********************************************************************
*
*       _CreateInst
*/
static USBH_HID_RC_INST * _CreateInst(USBH_HID_INST * pInst) {
  USBH_HID_RC_INST *p;

  p = (USBH_HID_RC_INST *)USBH_TRY_MALLOC_ZEROED(sizeof(USBH_HID_RC_INST));
  if (p != NULL) {
    USBH_IFDBG(p->Magic = HID_GENERIC_MAGIC);
    p->pInst = pInst;
    p->HandlerHook.pContext = p;
    p->HandlerHook.pHandler = _ParseRCData;
    p->HandlerHook.pRemove  = _RemoveInst;
    USBH_IFDBG(p->HandlerHook.Magic = HID_HANDLER_MAGIC);
    USBH_HID_RegisterReportHandler(pInst, &p->HandlerHook);
    pInst->PollIntEP   = 1;
    pInst->DeviceType |= USBH_HID_RC;
  } else {
    USBH_WARN((USBH_MCAT_HID, "HID_RC: _CreateInst: No memory"));
  }
  return p;
}

/*********************************************************************
*
*       _DetectRC
*
*  Function description
*    Detection of a remote control device.
*/
static void _DetectRC(USBH_HID_INST * p) {
  USBH_HID_RC_INST *pInst;

  pInst = _CreateInst(p);
  if (pInst == NULL) {
    return;
  }
  USBH_MEMSET(&pInst->RCInfo, 0, sizeof(pInst->RCInfo));
  USBH_HID__ParseReportDesc(p, _FindRCInfo, pInst);
  //
  // If we did not find any RC usages - remove the instance again.
  //
  if (pInst->RCInfoFound == 0u) {
    _RemoveInst(pInst);
  } else {
    USBH_LOG((USBH_MCAT_HID_RDESC, "Parsed remote control info, Report ID = %x, Size = %u", pInst->RCInfo.ReportId, pInst->RCInfo.ReportSize));
    USBH_LOG((USBH_MCAT_HID_RDESC, "  Volume inc      off=%u, bits=%u", pInst->RCInfo.VolumeIncrementBitPosStart, 1));
    USBH_LOG((USBH_MCAT_HID_RDESC, "  Volume dec      off=%u, bits=%u", pInst->RCInfo.VolumeDecrementBitPosStart, 1));
    USBH_LOG((USBH_MCAT_HID_RDESC, "  Mute            off=%u, bits=%u", pInst->RCInfo.MuteBitPosStart, pInst->RCInfo.MuteNumBits));
    USBH_LOG((USBH_MCAT_HID_RDESC, "  Play/Pause      off=%u, bits=%u", pInst->RCInfo.PlayPauseBitPosStart, 1));
    USBH_LOG((USBH_MCAT_HID_RDESC, "  Scan next track off=%u, bits=%u", pInst->RCInfo.ScanNextTrackBitPosStart, 1));
    USBH_LOG((USBH_MCAT_HID_RDESC, "  Scan prev track off=%u, bits=%u", pInst->RCInfo.ScanPreviousTrackBitPosStart, 1));
    USBH_LOG((USBH_MCAT_HID_RDESC, "  Repeat          off=%u, bits=%u", pInst->RCInfo.RepeatBitPosStart, 1));
    USBH_LOG((USBH_MCAT_HID_RDESC, "  Random play     off=%u, bits=%u", pInst->RCInfo.RandomPlayBitPosStart, 1));
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
*       USBH_HID_SetOnRCStateChange
*
*  Function description
*    Sets a callback to be called in case of remote control events.
*    Remote control interfaces are often a part of an USB audio device,
*    the HID interface is used to tell the host about changes in volume,
*    mute, for music track control and similar.
*
*  Parameters
*    pfOnChange : Callback that shall be called when a remote control change notification is available.
*/
void USBH_HID_SetOnRCStateChange(USBH_HID_ON_RC_FUNC * pfOnChange) {
  USBH_MEMSET(&USBH_HID_RC_Global, 0, sizeof(USBH_HID_RC_Global));
  USBH_HID_RC_Global.pfOnStateChange = pfOnChange;
  USBH_HID_RC_Global.PluginHook.pDetect = _DetectRC;
  USBH_IFDBG(USBH_HID_RC_Global.PluginHook.Magic = HID_PLUGIN_MAGIC);
  USBH_HID_RegisterPlugin(&USBH_HID_RC_Global.PluginHook);
}

/*************************** End of file ****************************/
