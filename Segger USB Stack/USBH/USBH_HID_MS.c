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
File        : USBH_HID_MS.c
Purpose     : HID plugin for mouse
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
  U16 xBitPosStart;
  U16 xNumBits;
  U16 yBitPosStart;
  U16 yNumBits;
  U16 WheelBitPosStart;
  U16 WheelNumBits;
  U16 ButtonsBitPosStart;
  U16 ButtonsNumBits;
  U16 ReportId;
  U16 ReportSize;
} HID_MOUSE_INFO;

//
// Structure for every connected device handled by this plugin
//
typedef struct {
#if USBH_DEBUG > 1
  U32                         Magic;
#endif
  USBH_HID_INST             * pInst;
  USBH_HID_HANDLER_HOOK       HandlerHook;
  HID_MOUSE_INFO              MouseInfo;
} USBH_HID_MS_INST;

//
// Global data of this plugin
//
typedef struct {
  USBH_HID_ON_MOUSE_FUNC    * pfOnMouseStateChange;
  USBH_HID_DETECTION_HOOK     PluginHook;
} USBH_HID_MS_GLOBAL;

/*********************************************************************
*
*       Static const
*
**********************************************************************
*/
#if 0
static const HID_MOUSE_INFO _DefaultMouseInfo = {
   8,  // U16 xBitPosStart;
   8,  // U16 xNumBits;
  16,  // U16 yBitPosStart;
   8,  // U16 yNumBits;
  24,  // U16 WheelBitPosStart;
   8,  // U16 WheelNumBits;
   0,  // U16 ButtonsBitPosStart;
   8,  // U16 ButtonsNumBits;
   0,  // U16 ReportId;
   4,  // U16 ReportSize;
};
#endif

static const U8 _RepDescHeadMouse[]    = { 5, 1, 9, 2 };

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static USBH_HID_MS_GLOBAL USBH_HID_MS_Global;

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _ParseMouseData
*
*  Function description
*    Parse the mouse report data.
*
*  Parameters
*    pContext   : Pointer to the HID_MS instance.
*    pReport    : Report received from the device.
*    ReportLen  : Size of the report.
*    Handled    : Is != 0, if this device is already handled by another plugin.
*/
static int _ParseMouseData(void *pContext, const U8 *pReport, unsigned ReportLen, int Handled) {
  USBH_HID_MS_INST     * pInst;
  USBH_HID_INST        * pBaseInst;
  USBH_HID_MOUSE_DATA    MouseData;
  const HID_MOUSE_INFO * pMouseInfo;

  USBH_USE_PARA(Handled);
  pInst = USBH_CTX2PTR(USBH_HID_MS_INST, pContext);
  USBH_ASSERT_MAGIC(pInst, HID_MOUSE);
  pBaseInst = pInst->pInst;

  if (USBH_HID_MS_Global.pfOnMouseStateChange != NULL) {
    pMouseInfo = &pInst->MouseInfo;
    if (ReportLen > 0u && pBaseInst->ReportIDsUsed != 0u) {
      if (*pReport != pMouseInfo->ReportId) {
        return 0;
      }
      pReport++;
      ReportLen--;
    }
    if (ReportLen >= pMouseInfo->ReportSize) {
      MouseData.ButtonState = USBH_HID__GetBits      (pReport, pMouseInfo->ButtonsBitPosStart, pMouseInfo->ButtonsNumBits);
      MouseData.xChange     = USBH_HID__GetBitsSigned(pReport, pMouseInfo->xBitPosStart,       pMouseInfo->xNumBits);
      MouseData.yChange     = USBH_HID__GetBitsSigned(pReport, pMouseInfo->yBitPosStart,       pMouseInfo->yNumBits);
      MouseData.WheelChange = USBH_HID__GetBitsSigned(pReport, pMouseInfo->WheelBitPosStart,   pMouseInfo->WheelNumBits);
      MouseData.InterfaceID = pBaseInst->InterfaceID;
      USBH_HID_MS_Global.pfOnMouseStateChange(&MouseData);
    }
  }
  return 1;
}

/*********************************************************************
*
*       _FindMouseInfo
*
*  Function description
*    Called from report descriptor parser for each field element.
*    Checks for mouse items.
*
*  Parameters
*    Flag        : Bit 0 = 0: Input item
*                  Bit 0 = 1: Output item
*                  Bit 1 = 0: Array item
*                  Bit 1 = 1: Variable item
*    pField      : Field info.
*/
static void _FindMouseInfo(unsigned Flag, const HID_FIELD_INFO *pField) {
  U32      Usage;
  unsigned i;
  unsigned BitPosStart;
  unsigned Size;
  USBH_HID_MS_INST * pInst;

  if ((Flag & 1u) != 0u) {
    //
    // Only interested in IN report
    //
    return;
  }
  pInst = USBH_CTX2PTR(USBH_HID_MS_INST, pField->pContext);
  USBH_ASSERT_MAGIC(pInst, HID_MOUSE);
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
//    USBH_LOG((-1, "+++ Report ID = %x, Usage = %x", pField->ReportId, Usage));
    BitPosStart = pField->InRptLen + i * pField->RptSize;
    Size        = (pField->InRptLen + (unsigned)pField->RptCount * pField->RptSize + 7u) >> 3;
    switch (Usage) {
      case USBH_HID_USAGE_TYPE(USBH_HID_USAGE_PAGE_GENERIC_DESKTOP, USBH_HID_USAGE_GENDESK_X):
        pInst->MouseInfo.xBitPosStart = BitPosStart;
        pInst->MouseInfo.xNumBits     = pField->RptSize;
        pInst->MouseInfo.ReportId     = pField->ReportId;
//        USBH_LOG((-1, "+++  X      off=%u, bits=%u", pInst->MouseInfo.xBitPosStart, pInst->MouseInfo.xNumBits));
        break;
      case USBH_HID_USAGE_TYPE(USBH_HID_USAGE_PAGE_GENERIC_DESKTOP, USBH_HID_USAGE_GENDESK_Y):
        pInst->MouseInfo.yBitPosStart = BitPosStart;
        pInst->MouseInfo.yNumBits     = pField->RptSize;
//        USBH_LOG((-1, "+++  Y      off=%u, bits=%u", pInst->MouseInfo.yBitPosStart, pInst->MouseInfo.yNumBits));
        break;
      case USBH_HID_USAGE_TYPE(USBH_HID_USAGE_PAGE_GENERIC_DESKTOP, USBH_HID_USAGE_GENDESK_WHEEL):
        pInst->MouseInfo.WheelBitPosStart = BitPosStart;
        pInst->MouseInfo.WheelNumBits     = pField->RptSize;
//        USBH_LOG((-1, "+++  Wheel  off=%u, bits=%u", pInst->MouseInfo.WheelBitPosStart, pInst->MouseInfo.WheelNumBits));
        break;
      case USBH_HID_USAGE_TYPE(USBH_HID_USAGE_PAGE_BUTTON, 1u):
        pInst->MouseInfo.ButtonsBitPosStart = BitPosStart;
        pInst->MouseInfo.ButtonsNumBits     = pField->RptSize * pField->RptCount;
//        USBH_LOG((-1, "+++  Button off=%u, bits=%u", pInst->MouseInfo.ButtonsBitPosStart, pInst->MouseInfo.ButtonsNumBits));
        break;
      default:
        Size = 0;
        break;
    }
    if (pInst->MouseInfo.ReportSize < Size) {
      pInst->MouseInfo.ReportSize = Size;
    }
  }
}

/*********************************************************************
*
*       _Detect
*/
static int _Detect(const USBH_HID_INST * pInst) {
  const U8   * pDesc;
  unsigned     Length;

  if (USBH_GetInterfaceDescriptorPtr(pInst->hInterface, 0, &pDesc, &Length) != USBH_STATUS_SUCCESS) {
    return 0;
  }
#if USBH_HID_DISABLE_INTERFACE_PROTOCOL_CHECK == 0
  if (pDesc[7] == HID_DEVICE_MOUSE_PROTOCOL) { //bInterfaceProtocol
    return 1;
  }
#endif
  if (USBH_MEMCMP(pInst->pReportBufferDesc, _RepDescHeadMouse, sizeof(_RepDescHeadMouse)) == 0) {
    return 1;
  }
  return 0;
}

/*********************************************************************
*
*       _RemoveInst
*
*  Function description
*    Remove instance.
*/
static void _RemoveInst(void *pContext) {
  USBH_HID_MS_INST * pInst;

  pInst = USBH_CTX2PTR(USBH_HID_MS_INST, pContext);
  USBH_ASSERT_MAGIC(pInst, HID_MOUSE);
  USBH_FREE(pInst);
}

/*********************************************************************
*
*       _CreateInst
*/
static USBH_HID_MS_INST * _CreateInst(USBH_HID_INST * pInst) {
  USBH_HID_MS_INST *p;

  p = (USBH_HID_MS_INST *)USBH_TRY_MALLOC_ZEROED(sizeof(USBH_HID_MS_INST));
  if (p != NULL) {
    USBH_IFDBG(p->Magic = HID_MOUSE_MAGIC);
    p->pInst = pInst;
    p->HandlerHook.pContext = p;
    p->HandlerHook.pHandler = _ParseMouseData;
    p->HandlerHook.pRemove  = _RemoveInst;
    USBH_IFDBG(p->HandlerHook.Magic = HID_HANDLER_MAGIC);
    USBH_HID_RegisterReportHandler(pInst, &p->HandlerHook);
    pInst->PollIntEP   = 1;
    pInst->DeviceType |= USBH_HID_MOUSE;
  } else {
    USBH_WARN((USBH_MCAT_HID, "HID_MS: _CreateInst: No memory"));
  }
  return p;
}

/*********************************************************************
*
*       _DetectMS
*
*  Function description
*    Detection of a mouse device.
*/
static void _DetectMS(USBH_HID_INST * p) {
  USBH_HID_MS_INST *pInst;

  if (_Detect(p) != 0) {
    USBH_LOG((USBH_MCAT_HID, "HID: Mouse detected"));
    pInst = _CreateInst(p);
    if (pInst == NULL) {
      return;
    }
    USBH_MEMSET(&pInst->MouseInfo, 0, sizeof(pInst->MouseInfo));
    USBH_HID__ParseReportDesc(p, _FindMouseInfo, pInst);
    USBH_LOG((USBH_MCAT_HID_RDESC, "Parsed mouse info, Report ID = %x, Size = %u", pInst->MouseInfo.ReportId, pInst->MouseInfo.ReportSize));
    USBH_LOG((USBH_MCAT_HID_RDESC, "  Button off=%u, bits=%u", pInst->MouseInfo.ButtonsBitPosStart, pInst->MouseInfo.ButtonsNumBits));
    USBH_LOG((USBH_MCAT_HID_RDESC, "  X      off=%u, bits=%u", pInst->MouseInfo.xBitPosStart, pInst->MouseInfo.xNumBits));
    USBH_LOG((USBH_MCAT_HID_RDESC, "  Y      off=%u, bits=%u", pInst->MouseInfo.yBitPosStart, pInst->MouseInfo.yNumBits));
    USBH_LOG((USBH_MCAT_HID_RDESC, "  Wheel  off=%u, bits=%u", pInst->MouseInfo.WheelBitPosStart, pInst->MouseInfo.WheelNumBits));
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
*       USBH_HID_SetOnMouseStateChange
*
*  Function description
*     Sets a callback to be called in case of mouse events.
*
*  Parameters
*    pfOnChange : Callback that shall be called when a mouse change notification is available.
*/
void USBH_HID_SetOnMouseStateChange(USBH_HID_ON_MOUSE_FUNC * pfOnChange) {
  USBH_MEMSET(&USBH_HID_MS_Global, 0, sizeof(USBH_HID_MS_Global));
  USBH_HID_MS_Global.pfOnMouseStateChange = pfOnChange;
  USBH_HID_MS_Global.PluginHook.pDetect = _DetectMS;
  USBH_IFDBG(USBH_HID_MS_Global.PluginHook.Magic = HID_PLUGIN_MAGIC);
  USBH_HID_RegisterPlugin(&USBH_HID_MS_Global.PluginHook);
}

/*************************** End of file ****************************/
