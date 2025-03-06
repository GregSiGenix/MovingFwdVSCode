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
File        : USBH_HID_KB.c
Purpose     : HID plugin for simple keyboard handling
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
*       Defines
*
**********************************************************************
*/

#define USBH_HID_OLD_STATE_NUMBYTES                     8u

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
  USBH_DLIST                  ListEntry;
  U8                          LedState;
  U8                          KeyboardReportId;
  U8                          NewLedState;         // Must immediately follow KeyboardReportId !!!
  U8                          aOldState[USBH_HID_OLD_STATE_NUMBYTES];
  USBH_HID_HANDLER_HOOK       HandlerHook;
} USBH_HID_KB_INST;

//
// Global data of this plugin
//
typedef struct {
  USBH_DLIST                  List;                // List of devices (USBH_HID_KB_INST)
  USBH_HID_ON_KEYBOARD_FUNC * pfOnKeyStateChange;
  USBH_HID_DETECTION_HOOK     PluginHook;
  U8                          AllowLEDUpdate;
} USBH_HID_KB_GLOBAL;


#define GET_HID_KB_INST_FROM_ENTRY(pListEntry)     STRUCT_BASE_POINTER(pListEntry, USBH_HID_KB_INST, ListEntry)

/*********************************************************************
*
*       Static const
*
**********************************************************************
*/

static const U8 _RepDescHeadKeyboard[] = { 5, 1, 9, 6 };

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static USBH_HID_KB_GLOBAL USBH_HID_KB_Global;

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _h2p()
*/
static USBH_HID_KB_INST * _h2p(USBH_HID_HANDLE Handle) {
  USBH_HID_KB_INST * pInst;
  USBH_DLIST       * pEntry;

  if (Handle == 0u) {
    return NULL;
  }
  //
  // Iterate over linked list to find an instance with matching handle. Return if found.
  //
  pEntry = USBH_DLIST_GetNext(&USBH_HID_KB_Global.List);
  while (pEntry != &USBH_HID_KB_Global.List) {
    pInst = GET_HID_KB_INST_FROM_ENTRY(pEntry);
    USBH_ASSERT_MAGIC(pInst, HID_KEYBOARD);
    if (pInst->pInst->Handle == Handle) {
      return pInst;
    }
    pEntry = USBH_DLIST_GetNext(pEntry);
  }
  //
  // Error handling: Device handle not found in list.
  //
  USBH_WARN((USBH_MCAT_HID, "HID: Invalid handle %u", Handle));
  return NULL;
}

/*********************************************************************
*
*       _UpdateKeyState
*
*  Function description
*   Sends a notification the user application in order to information of
*   of a change of the keyboard status.
*/
static void _UpdateKeyState(const USBH_HID_INST * pInst, unsigned Code, unsigned Value) {
  USBH_HID_KEYBOARD_DATA   KeyData;

  KeyData.Code        = Code;
  KeyData.Value       = Value;
  KeyData.InterfaceID = pInst->InterfaceID;
  if (USBH_HID_KB_Global.pfOnKeyStateChange != NULL) {
    USBH_HID_KB_Global.pfOnKeyStateChange(&KeyData);
  }
}

/*********************************************************************
*
*       _IsValueInArray
*
*  Function description
*    Checks whether a value is in the specified array.
*/
static int _IsValueInArray(const U8 * p, U8 Val, unsigned NumItems) {
  unsigned i;
  for (i = 0; i < NumItems; i++) {
    if (*p++ == Val) {
      return 1;
    }
  }
  return 0;
}

/*********************************************************************
*
*       _ParseKeyboardData
*
*  Function description
*    Checks whether a change of the previously stored keyboard information
*    have been changed. If so, the call back is called in order to inform
*    user application about the change.
*/
static int _ParseKeyboardData(void *pContext, const U8 * pNewState, unsigned ReportLen, int Handled) {
  USBH_HID_KB_INST * pInst;
  USBH_HID_INST    * pBaseInst;
  USBH_STATUS        Status;
  unsigned           i;

  USBH_USE_PARA(Handled);
  pInst = USBH_CTX2PTR(USBH_HID_KB_INST, pContext);
  USBH_ASSERT_MAGIC(pInst, HID_KEYBOARD);
  pBaseInst = pInst->pInst;

  pInst->NewLedState = pInst->LedState;
  if (ReportLen > 0u && pBaseInst->ReportIDsUsed != 0u) {
    if (*pNewState != pInst->KeyboardReportId) {
      return 0;
    }
    pNewState++;
    ReportLen--;
  }
  for (i = 0; i < 8u; i++) {
    if (((pNewState[0] >> i) & 1u) != ((pInst->aOldState[0] >> i) & 1u)) {
      _UpdateKeyState(pBaseInst, 0xE0u + i, ((unsigned)pNewState[0] >> i) & 1uL);
    }
  }
  for (i = 2; i < 8u && i < ReportLen; i++) {
    if (pInst->aOldState[i] > 3u && (_IsValueInArray(pNewState + 2, pInst->aOldState[i], 6) == 0)) {
      _UpdateKeyState(pBaseInst, pInst->aOldState[i], 0);
    }
    if (pNewState[i] > 3u && _IsValueInArray(pInst->aOldState + 2, pNewState[i], 6) == 0) {
      _UpdateKeyState(pBaseInst, pNewState[i], 1);
      //  Update
      if (pNewState[i] == 0x39u) {
        pInst->NewLedState ^= (1u << 1);
      }
      if (pNewState[i] == 0x47u) {
        pInst->NewLedState ^= (1u << 2);
      }
      if (pNewState[i] == 0x53u) {
        pInst->NewLedState ^= (1u << 0);
      }
    }
  }
  if (pInst->LedState != pInst->NewLedState) {
    if (USBH_HID_KB_Global.AllowLEDUpdate != 0u) {
      if (pBaseInst->ReportIDsUsed != 0u) {
        Status = USBH_HID__SubmitOutBuffer(pBaseInst, &pInst->KeyboardReportId, 2, NULL, NULL, USBH_HID_USE_REPORT_ID);
      } else {
        Status = USBH_HID__SubmitOutBuffer(pBaseInst, &pInst->NewLedState, 1, NULL, NULL, 0);
      }
      if (Status == USBH_STATUS_PENDING) {
        //
        // Can not wait for completion because this runs in a timer context.
        //
        pInst->LedState = pInst->NewLedState;
      }
    } else {
      USBH_LOG((USBH_MCAT_HID_URB, "Update LED state of the keyboard has been disabled."));
    }
  }
  USBH_MEMCPY(&pInst->aOldState[0], pNewState, USBH_MIN(ReportLen, USBH_HID_OLD_STATE_NUMBYTES));
  return 1;
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
  if (pDesc[7] == HID_DEVICE_KEYBOARD_PROTOCOL) { //bInterfaceProtocol
    return 1;
  }
#endif
  if (USBH_MEMCMP(pInst->pReportBufferDesc, _RepDescHeadKeyboard, sizeof(_RepDescHeadKeyboard)) == 0) {
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
  USBH_HID_KB_INST * pInst;

  pInst = USBH_CTX2PTR(USBH_HID_KB_INST, pContext);
  USBH_ASSERT_MAGIC(pInst, HID_KEYBOARD);
  USBH_DLIST_RemoveEntry(&pInst->ListEntry);
  USBH_FREE(pInst);
}

/*********************************************************************
*
*       _CreateInst
*/
static USBH_HID_KB_INST * _CreateInst(USBH_HID_INST * pInst) {
  USBH_HID_KB_INST *p;

  p = (USBH_HID_KB_INST *)USBH_TRY_MALLOC_ZEROED(sizeof(USBH_HID_KB_INST));
  if (p != NULL) {
    USBH_IFDBG(p->Magic = HID_KEYBOARD_MAGIC);
    p->pInst = pInst;
    p->HandlerHook.pContext = p;
    p->HandlerHook.pHandler = _ParseKeyboardData;
    p->HandlerHook.pRemove  = _RemoveInst;
    USBH_IFDBG(p->HandlerHook.Magic = HID_HANDLER_MAGIC);
    USBH_DLIST_InsertTail(&USBH_HID_KB_Global.List, &p->ListEntry);
    USBH_HID_RegisterReportHandler(pInst, &p->HandlerHook);
    pInst->PollIntEP   = 1;
    pInst->DeviceType |= USBH_HID_KEYBOARD;
  } else {
    USBH_WARN((USBH_MCAT_HID, "HID_KB: _CreateInst: No memory"));
  }
  return p;
}

/*********************************************************************
*
*       _SimpleDetectKB
*
*  Function description
*    Simple detection of a keyboard with a single report.
*/
static void _SimpleDetectKB(USBH_HID_INST * pInst) {
  if (_Detect(pInst) != 0) {
    USBH_LOG((USBH_MCAT_HID, "HID: Keyboard detected"));
    (void)_CreateInst(pInst);
  }
}

/*********************************************************************
*
*       _FindKeyboardInfo
*
*  Function description
*    Called from report descriptor parser for each field element.
*    Checks for keyboard items.
*
*  Parameters
*    Flag        : Bit 0 = 0: Input item
*                  Bit 0 = 1: Output item
*                  Bit 1 = 0: Array item
*                  Bit 1 = 1: Variable item
*    pField      : Field info.
*/
static void _FindKeyboardInfo(unsigned Flag, const HID_FIELD_INFO *pField) {
  U32                Usage;
  USBH_HID_KB_INST * pInst;

  if (pField->UsageMax != 0u) {
    Usage = pField->UsageMin;
  } else {
    Usage = pField->Usage[0];
  }
  if ((Flag & 3u) == 0u &&
      (Usage >> 16) == USBH_HID_USAGE_PAGE_KEYBOARD) {
    pInst = USBH_CTX2PTR(USBH_HID_KB_INST, pField->pContext);
    USBH_ASSERT_MAGIC(pInst, HID_KEYBOARD);
    pInst->KeyboardReportId = pField->ReportId;
  }
}

/*********************************************************************
*
*       _DetectKB
*
*  Function description
*    Detection of a keyboard with multiple reports.
*/
static void _DetectKB(USBH_HID_INST * pInst) {
  USBH_HID_KB_INST *p;

  if (_Detect(pInst) != 0) {
    USBH_LOG((USBH_MCAT_HID, "HID: Keyboard detected"));
    p = _CreateInst(pInst);
    if (p == NULL) {
      return;
    }
    p->KeyboardReportId = 0;
    USBH_HID__ParseReportDesc(pInst, _FindKeyboardInfo, p);
  }
}

/*********************************************************************
*
*       _SetOnKeyboardStateChange
*/
static void _SetOnKeyboardStateChange(USBH_HID_ON_KEYBOARD_FUNC * pfOnChange, USBH_HID_DETECTION_CB * pfDetect) {
  USBH_MEMSET(&USBH_HID_KB_Global, 0, sizeof(USBH_HID_KB_Global));
  USBH_HID_KB_Global.pfOnKeyStateChange = pfOnChange;
  USBH_HID_KB_Global.PluginHook.pDetect = pfDetect;
  USBH_IFDBG(USBH_HID_KB_Global.PluginHook.Magic = HID_PLUGIN_MAGIC);
  USBH_DLIST_Init(&USBH_HID_KB_Global.List);
  USBH_HID_RegisterPlugin(&USBH_HID_KB_Global.PluginHook);
}

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       USBH_HID_SetOnKeyboardStateChange
*
*  Function description
*    Sets a callback to be called in case of keyboard events.
*    Handles all keyboards that do not use report ids. These are all
*    keyboards that can be used in boot mode (with a PC BIOS).
*
*  Parameters
*    pfOnChange : Callback that shall be called when a keyboard change notification is available.
*/
void USBH_HID_SetOnKeyboardStateChange(USBH_HID_ON_KEYBOARD_FUNC * pfOnChange) {
  _SetOnKeyboardStateChange(pfOnChange, _SimpleDetectKB);
}

/*********************************************************************
*
*       USBH_HID_SetOnExKeyboardStateChange
*
*  Function description
*    Sets a callback to be called in case of keyboard events.
*    Handles also keyboards that use report ids.
*    In contrast to the function USBH_HID_SetOnKeyboardStateChange(), some
*    unusual Apple keyboards are supported, too.
*
*  Parameters
*    pfOnChange : Callback that shall be called when a keyboard change notification is available.
*/
void USBH_HID_SetOnExKeyboardStateChange(USBH_HID_ON_KEYBOARD_FUNC * pfOnChange) {
  _SetOnKeyboardStateChange(pfOnChange, _DetectKB);
}

/*********************************************************************
*
*       USBH_HID_SetIndicators
*
*  Function description
*    Sets the indicators (usually LEDs) on a keyboard.
*
*  Parameters
*    hDevice      : Handle to the opened device.
*    IndicatorMask: Binary mask of the following items
*                   + USBH_HID_IND_NUM_LOCK
*                   + USBH_HID_IND_CAPS_LOCK
*                   + USBH_HID_IND_SCROLL_LOCK
*                   + USBH_HID_IND_COMPOSE
*                   + USBH_HID_IND_KANA
*                   + USBH_HID_IND_SHIFT
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_HID_SetIndicators(USBH_HID_HANDLE hDevice, U8 IndicatorMask) {
  USBH_STATUS         Status;
  USBH_HID_KB_INST  * pInst;

  pInst = _h2p(hDevice);
  if (pInst == NULL) {
    return USBH_STATUS_INVALID_HANDLE;
  }
  Status = USBH_HID__SubmitOut(pInst->pInst, &IndicatorMask, 1);
  if (Status == USBH_STATUS_SUCCESS) {
    pInst->LedState = IndicatorMask;
  }
  return Status;
}

/*********************************************************************
*
*       USBH_HID_GetIndicators
*
*  Function description
*    Retrieves the indicator (LED) status.
*
*  Parameters
*    hDevice         : Handle to the opened device.
*    pIndicatorMask  : Binary mask of the following items
*                      + USBH_HID_IND_NUM_LOCK
*                      + USBH_HID_IND_CAPS_LOCK
*                      + USBH_HID_IND_SCROLL_LOCK
*                      + USBH_HID_IND_COMPOSE
*                      + USBH_HID_IND_KANA
*                      + USBH_HID_IND_SHIFT
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_HID_GetIndicators(USBH_HID_HANDLE hDevice, U8 * pIndicatorMask) {
  USBH_STATUS        Status;
  USBH_HID_KB_INST * pInst;

  Status = USBH_STATUS_INVALID_HANDLE;
  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    if (pInst->pInst->IsOpened == 0) {
      Status = USBH_STATUS_NOT_OPENED;
    } else {
      *pIndicatorMask = pInst->LedState;
      Status = USBH_STATUS_SUCCESS;
    }
  }
  return Status;
}

/*********************************************************************
*
*       USBH_HID_ConfigureAllowLEDUpdate
*
*  Function description
*    Sets whether the keyboard LED should be updated or not.
*    (Default is disabled).
*
*  Parameters
*    AllowLEDUpdate:   * 0 - Disable LED Update.
*                      * 1 - Allow   LED Update.
*/
void USBH_HID_ConfigureAllowLEDUpdate(unsigned AllowLEDUpdate) {
  USBH_HID_KB_Global.AllowLEDUpdate = AllowLEDUpdate;
}

/*************************** End of file ****************************/
