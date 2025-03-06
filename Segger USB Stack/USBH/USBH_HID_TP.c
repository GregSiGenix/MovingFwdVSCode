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
File        : USBH_HID_TP.c
Purpose     : HID plugin for touch pad
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
//
// Structure for every connected device handled by this plugin
//
typedef struct {
#if USBH_DEBUG > 1
  U32                         Magic;
#endif
  USBH_HID_INST             * pInst;
  U16                         NumGenericInfos;
  USBH_HID_HANDLER_HOOK       HandlerHook;
  USBH_HID_GENERIC_DATA       GenericInfo[1];
} USBH_HID_TP_INST;

//
// Global data of this plugin
//
typedef struct {
  USBH_HID_ON_GENERIC_FUNC  * pfOnGenericEvent;
  U16                         NumGenericUsages;
  const U32                 * pGenericUsages;
  USBH_HID_DETECTION_HOOK     PluginHook;
} USBH_HID_TP_GLOBAL;

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
static USBH_HID_TP_GLOBAL USBH_HID_TP_Global;

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _ParseGenericData
*
*  Function description
*    Parse the report data for given usages.
*
*  Parameters
*    pContext   : Pointer to the HID_TP instance.
*    pReport    : Report received from the device.
*    ReportLen  : Size of the report.
*    Handled    : Is != 0, if this device is already handled by another plugin.
*/
static int _ParseGenericData(void *pContext, const U8 *pReport, unsigned ReportLen, int Handled) {
  USBH_HID_TP_INST     * pInst;
  USBH_HID_INST        * pBaseInst;
  unsigned               i;
  int                    Found = 0;
  U8                     ReportID = 0;
  USBH_HID_GENERIC_DATA *pInfo;

  if (Handled != 0) {
    return 0;
  }
  pInst = USBH_CTX2PTR(USBH_HID_TP_INST, pContext);
  USBH_ASSERT_MAGIC(pInst, HID_GENERIC);
  pBaseInst = pInst->pInst;
  if (ReportLen > 0u && pBaseInst->ReportIDsUsed != 0u) {
    ReportID = *pReport++;
    ReportLen--;
  }
  pInfo = pInst->GenericInfo;
  for (i = 0; i < pInst->NumGenericInfos; i++) {
    pInfo->Valid = 0;
    if (pInfo->Usage != 0u &&
        (ReportID == 0u || ReportID == pInfo->ReportID) &&
        (unsigned)pInfo->BitPosStart + pInfo->NumBits <= 8u * ReportLen) {
      //
      // Report contains value for this usage, extract value.
      //
      if (pInfo->Signed != 0u) {
        pInfo->Value.i32 = USBH_HID__GetBitsSigned(pReport, pInfo->BitPosStart, pInfo->NumBits);
      } else {
        pInfo->Value.u32 = USBH_HID__GetBits      (pReport, pInfo->BitPosStart, pInfo->NumBits);
      }
      pInfo->Valid = 1;
      Found        = 1;
    }
    pInfo++;
  }
  if (Found != 0) {
    USBH_HID_TP_Global.pfOnGenericEvent(pBaseInst->InterfaceID, pInst->NumGenericInfos, pInst->GenericInfo);
  }
  return Found;
}

/*********************************************************************
*
*       _FindGenericInfo
*
*  Function description
*    Called from report descriptor parser for each field element.
*    Checks for given usages items.
*
*  Parameters
*    Flag        : Bit 0 = 0: Input item
*                  Bit 0 = 1: Output item
*                  Bit 1 = 0: Array item
*                  Bit 1 = 1: Variable item
*    pField      : Field info.
*/
static void _FindGenericInfo(unsigned Flag, const HID_FIELD_INFO *pField) {
  USBH_HID_TP_INST     * pInst;
  U32      Usage;
  unsigned i;
  unsigned j;
  unsigned BitPosStart;
  USBH_HID_GENERIC_DATA *pInfo;

  if (Flag != 2u) {
    //
    // Only interested in IN report variable fields
    //
    return;
  }
  pInst = USBH_CTX2PTR(USBH_HID_TP_INST, pField->pContext);
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
    pInfo       = pInst->GenericInfo;
    for (j = 0; j < pInst->NumGenericInfos; j++) {
      if (USBH_HID_TP_Global.pGenericUsages[j] == Usage && pInfo->Usage == 0u) {
        pInfo->Usage       = Usage;
        pInfo->BitPosStart = BitPosStart;
        pInfo->NumBits     = pField->RptSize;
        pInfo->ReportID    = pField->ReportId;
        pInfo->Signed      = pField->Signed;
        pInfo->LogicalMin  = pField->LogicalMin;
        pInfo->LogicalMax  = pField->LogicalMax;
        pInfo->PhysicalMin = pField->PhysicalMin;
        pInfo->PhysicalMax = pField->PhysicalMax;
        pInfo->PhySigned   = pField->PhySigned;
        USBH_LOG((USBH_MCAT_HID_RDESC, "_FindGenericInfo: Usage %x: off=%u, bits=%u, reportID=%u", Usage, BitPosStart, pInfo->NumBits, pInfo->ReportID));
        break;
      }
      pInfo++;
    }
  }
  //
  // Store application usages
  //
  i = pInst->NumGenericInfos;
  pInfo = pInst->GenericInfo + i;
  for (j = i; j-- > 0u;) {
    pInfo--;
    if (USBH_HID_TP_Global.pGenericUsages[j] == USBH_HID_USAGE_DEVICE_TYPE) {
      if (pInfo->Usage == 0u) {
        i = j;
      } else {
        if (pInfo->ReportID == pField->ReportId) {
          pInfo->LogicalMin.u32 = pField->AppUsage;
          USBH_LOG((USBH_MCAT_HID_RDESC, "_FindGenericInfo: AppUsage %x, reportID=%u, idx=%u, upd", pField->AppUsage, pInfo->ReportID, j));
          return;
        }
      }
    }
  }
  if (i < pInst->NumGenericInfos) {
    pInfo = pInst->GenericInfo + i;
    pInfo->Usage          = USBH_HID_USAGE_DEVICE_TYPE;
    pInfo->LogicalMin.u32 = pField->AppUsage;
    pInfo->ReportID       = pField->ReportId;
    USBH_LOG((USBH_MCAT_HID_RDESC, "_FindGenericInfo: AppUsage %x, reportID=%u, idx=%u", pField->AppUsage, pInfo->ReportID, i));
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
  USBH_HID_TP_INST * pInst;

  pInst = USBH_CTX2PTR(USBH_HID_TP_INST, pContext);
  USBH_ASSERT_MAGIC(pInst, HID_GENERIC);
  USBH_FREE(pInst);
}

/*********************************************************************
*
*       _DetectTP
*
*  Function description
*    Detection of a generic input device.
*/
static void _DetectTP(USBH_HID_INST * p) {
  USBH_HID_TP_INST      *pInst;
  unsigned               i;
  USBH_HID_GENERIC_DATA *pInfo;
  int                    Found;

  USBH_ASSERT(USBH_HID_TP_Global.NumGenericUsages > 0);
  pInst = (USBH_HID_TP_INST *)USBH_TRY_MALLOC_ZEROED(sizeof(USBH_HID_TP_INST) +
                                                     (USBH_HID_TP_Global.NumGenericUsages - 1uL) * sizeof(USBH_HID_GENERIC_DATA));
  if (pInst == NULL) {
    USBH_WARN((USBH_MCAT_HID, "HID_TP: _DetectTP: No memory"));
    return;
  }
  USBH_IFDBG(pInst->Magic = HID_GENERIC_MAGIC);
  pInst->pInst = p;
  pInst->HandlerHook.pContext = pInst;
  pInst->HandlerHook.pHandler = _ParseGenericData;
  pInst->HandlerHook.pRemove  = _RemoveInst;
  USBH_IFDBG(pInst->HandlerHook.Magic = HID_HANDLER_MAGIC);
  pInst->NumGenericInfos = USBH_HID_TP_Global.NumGenericUsages;
  USBH_HID__ParseReportDesc(p, _FindGenericInfo, pInst);
  pInfo = pInst->GenericInfo;
  Found = 0;
  for (i = 0; i < pInst->NumGenericInfos; i++) {
    if (pInfo->Usage != 0u) {
      Found = 1;
    }
    pInfo++;
  }
  if (Found != 0) {
    USBH_HID_RegisterReportHandler(p, &pInst->HandlerHook);
    p->PollIntEP   = 1;
    p->DeviceType |= USBH_HID_TOUCHPAD;
    USBH_LOG((USBH_MCAT_HID, "HID: Touchpad detected"));
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
*       USBH_HID_SetOnGenericEvent
*
*  Function description
*    Sets a callback to be called in case of generic HID events.
*
*  Parameters
*    NumUsages : Number of usage codes provided by the caller.
*    pUsages   : List of usage codes of fields from the report to be monitored.
*                Each usage code must contain the Usage Page in the high order 16 bits
*                and the Usage ID in the the low order 16 bits.
*                pUsages must point to a static memory area that remains valid until the USBH_HID module is shut down.
*    pfOnEvent : Callback that shall be called when a report is received that contains at least one field
*                with usage code from the list.
*/
void USBH_HID_SetOnGenericEvent(U32 NumUsages, const U32 *pUsages, USBH_HID_ON_GENERIC_FUNC * pfOnEvent) {
  USBH_MEMSET(&USBH_HID_TP_Global, 0, sizeof(USBH_HID_TP_Global));
  USBH_HID_TP_Global.pfOnGenericEvent   = pfOnEvent;
  USBH_HID_TP_Global.NumGenericUsages   = NumUsages;
  USBH_HID_TP_Global.pGenericUsages     = pUsages;
  USBH_HID_TP_Global.PluginHook.pDetect = _DetectTP;
  USBH_IFDBG(USBH_HID_TP_Global.PluginHook.Magic = HID_PLUGIN_MAGIC);
  USBH_HID_RegisterPlugin(&USBH_HID_TP_Global.PluginHook);
}

/*************************** End of file ****************************/
