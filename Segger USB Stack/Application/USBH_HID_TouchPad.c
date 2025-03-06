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
-------------------------- END-OF-HEADER -----------------------------

File    : USBH_HID_TouchPad.c
Purpose : This sample will try to enumerate a connected touch pad
          and output the 'touch' events to the terminal.

Additional information:
  Preparations:
    An HID touch capable device is necessary.

  Expected behavior:
    When the touch device is connected presses on the touch screen
    are displayed in the debug terminal.

  Sample output:
    <...>

    12:553 MainTask -  Device D0022, Event 3, Action press    @ 2705 1895
    12:558 MainTask -  Device D0022, Event 1, Action press    @ 3304 1165
    12:558 MainTask -  Device D0022, Event 2, Action press    @ 2952 1805
    12:559 MainTask -  Device D0022, Event 3, Action press    @ 2668 1905
    12:563 MainTask -  Device D0022, Event 1, Action press    @ 3259 1196
    12:563 MainTask -  Device D0022, Event 2, Action release  @ 2952 1805
    12:564 MainTask -  Device D0022, Event 3, Action press    @ 2623 1915
    12:570 MainTask -  Device D0022, Event 1, Action press    @ 3218 1223
    12:570 MainTask -  Device D0022, Event 3, Action press    @ 2577 1925
    12:576 MainTask -  Device D0022, Event 1, Action press    @ 3168 1252
    12:576 MainTask -  Device D0022, Event 3, Action press    @ 2577 1925
    12:583 MainTask -  Device D0022, Event 1, Action press    @ 3118 1280
    12:583 MainTask -  Device D0022, Event 3, Action press    @ 2577 1925
    12:589 MainTask -  Device D0022, Event 1, Action press    @ 3065 1305
    12:589 MainTask -  Device D0022, Event 3, Action press    @ 2577 1925
    12:596 MainTask -  Device D0022, Event 1, Action press    @ 3016 1329
    12:596 MainTask -  Device D0022, Event 3, Action release  @ 2577 1925

    <...>
*/

/*********************************************************************
*
*       #include section
*
**********************************************************************
*/
#include <stdio.h>
#include "RTOS.h"
#include "BSP.h"
#include "USBH.h"
#include "USBH_HID.h"
#include "SEGGER.h"

/*********************************************************************
*
*       Defines configurable
*
**********************************************************************
*/
#define MAX_DATA_ITEMS             40

#define SHOW_DETAILED_REPORT_DATA   0

/*********************************************************************
*
*       Defines non-configurable
*
**********************************************************************
*/
#define USAGE_DIGITIZER_CONTACT_COUNT  0x0D0054
#define USAGE_DIGITIZER_SCAN_TIME      0x0D0056
#define USAGE_DIGITIZER_TIP_SWITCH     0x0D0042
#define USAGE_DIGITIZER_CONTACT_ID     0x0D0051
#define USAGE_GENERIC_DESKTOP_X        0x010030
#define USAGE_GENERIC_DESKTOP_Y        0x010031
#define USAGE_GENERIC_DESKTOP_Z        0x010032

/*********************************************************************
*
*       Local data definitions
*
**********************************************************************
*/
enum {
  TASK_PRIO_APP = 150,
  TASK_PRIO_USBH_MAIN,
  TASK_PRIO_USBH_ISR
};

typedef struct {
  unsigned ID;
  unsigned Tip;
  I32      X;
  I32      Y;
  U32      DeviceType;
} HID_EVENT;

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/

#if SHOW_DETAILED_REPORT_DATA
static const char *UsageStringTab[] = {
  "Contact count      ",
  "Device Type        ",
  "Scan time          ",
  "Finger 1 Tip       ",
  "Finger 1 contact ID",
  "Finger 1 X position",
  "Finger 1 Y position",
  "Finger 1 Z position",
  "Finger 2 Tip       ",
  "Finger 2 contact ID",
  "Finger 2 X position",
  "Finger 2 Y position",
  "Finger 2 Z position",
  "Finger 3 Tip       ",
  "Finger 3 contact ID",
  "Finger 3 X position",
  "Finger 3 Y position",
  "Finger 3 Z position",
  "Finger 4 Tip       ",
  "Finger 4 contact ID",
  "Finger 4 X position",
  "Finger 4 Y position",
  "Finger 4 Z position",
  "Finger 5 Tip       ",
  "Finger 5 contact ID",
  "Finger 5 X position",
  "Finger 5 Y position",
  "Finger 5 Z position",
  "Device Type        ",
  "Device Type        ",
  "Device Type        "
};
#endif

static const U32 UsageTab[] = {
  USAGE_DIGITIZER_CONTACT_COUNT,
  USBH_HID_USAGE_DEVICE_TYPE,
  USAGE_DIGITIZER_SCAN_TIME,
  USAGE_DIGITIZER_TIP_SWITCH,
  USAGE_DIGITIZER_CONTACT_ID,
  USAGE_GENERIC_DESKTOP_X,
  USAGE_GENERIC_DESKTOP_Y,
  USAGE_GENERIC_DESKTOP_Z,
  USAGE_DIGITIZER_TIP_SWITCH,
  USAGE_DIGITIZER_CONTACT_ID,
  USAGE_GENERIC_DESKTOP_X,
  USAGE_GENERIC_DESKTOP_Y,
  USAGE_GENERIC_DESKTOP_Z,
  USAGE_DIGITIZER_TIP_SWITCH,
  USAGE_DIGITIZER_CONTACT_ID,
  USAGE_GENERIC_DESKTOP_X,
  USAGE_GENERIC_DESKTOP_Y,
  USAGE_GENERIC_DESKTOP_Z,
  USAGE_DIGITIZER_TIP_SWITCH,
  USAGE_DIGITIZER_CONTACT_ID,
  USAGE_GENERIC_DESKTOP_X,
  USAGE_GENERIC_DESKTOP_Y,
  USAGE_GENERIC_DESKTOP_Z,
  USAGE_DIGITIZER_TIP_SWITCH,
  USAGE_DIGITIZER_CONTACT_ID,
  USAGE_GENERIC_DESKTOP_X,
  USAGE_GENERIC_DESKTOP_Y,
  USAGE_GENERIC_DESKTOP_Z,
  USBH_HID_USAGE_DEVICE_TYPE,
  USBH_HID_USAGE_DEVICE_TYPE,
  USBH_HID_USAGE_DEVICE_TYPE
};

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static OS_STACKPTR int         _StackMain[1536/sizeof(int)];
static OS_TASK                 _TCBMain;
static OS_STACKPTR int         _StackIsr[1276/sizeof(int)];
static OS_TASK                 _TCBIsr;
static HID_EVENT               _aHIDEvents[MAX_DATA_ITEMS];
static OS_MAILBOX              _HIDMailBox;

/*********************************************************************
*
*       Static Code
*
**********************************************************************
*/

/*********************************************************************
*
*       _Convert
*
*  Function description
*    Convert from 'Logical' to 'Physical' units.
*/
static I32 _Convert(const USBH_HID_GENERIC_DATA * pGenericData) {

  if (pGenericData->PhySigned) {
    I32 Data;

    Data = pGenericData->Value.i32;
    if (pGenericData->PhysicalMax.i32 != 0 && pGenericData->LogicalMax.i32 != 0) {
      //
      //
      //
      Data -= pGenericData->LogicalMin.i32;
      Data *= (pGenericData->PhysicalMax.i32 - pGenericData->PhysicalMin.i32);
      Data /= (pGenericData->LogicalMax.i32  - pGenericData->LogicalMin.i32);
      Data += pGenericData->PhysicalMin.i32;
    }
    return Data;
  } else {
    U32 Data;

    Data = pGenericData->Value.u32;
    if (pGenericData->PhysicalMax.u32 != 0 && pGenericData->LogicalMax.u32 != 0) {
      //
      //
      //
      Data -= pGenericData->LogicalMin.u32;
      Data *= (pGenericData->PhysicalMax.u32 - pGenericData->PhysicalMin.u32);
      Data /= (pGenericData->LogicalMax.u32  - pGenericData->LogicalMin.u32);
      Data += pGenericData->PhysicalMin.u32;
    }
    return Data;
  }
}

/*********************************************************************
*
*       _OnTouchPadChange
*
*  Function description
*    Callback, called from the USBH task when a generic HID event occurs.
*/
static void _OnTouchPadChange(USBH_INTERFACE_ID InterfaceID, unsigned NumGenericInfos, const USBH_HID_GENERIC_DATA * pGenericData) {
  unsigned   i;
  unsigned   ContactCount;
  HID_EVENT  HidEvent;

#if SHOW_DETAILED_REPORT_DATA
  USBH_Logf_Application("Event from %u", InterfaceID);
  //
  // Show report data in detail
  //
  for (i = 0; i < NumGenericInfos; i++) {
    if (pGenericData[i].Valid) {
      if (pGenericData[i].Signed) {
        USBH_Logf_Application(" %s = %d", UsageStringTab[i], pGenericData[i].Value.i32);
      } else {
        USBH_Logf_Application(" %s = %u", UsageStringTab[i], pGenericData[i].Value.u32);
      }
    }
  }
#else
  (void)InterfaceID;
#endif
  //
  // First check for multi finger touch screen.
  //
  memset(&HidEvent, 0, sizeof(HidEvent));
  if (pGenericData->Valid) {
    //
    // If USAGE_DIGITIZER_CONTACT_COUNT is present in the report, we assume
    // that we have a multi finger touch screen.
    //
    if (pGenericData[1].Valid) {
      HidEvent.DeviceType = pGenericData[1].LogicalMin.u32;
    }
    ContactCount = pGenericData->Value.u32;
    for (i = 0; i < NumGenericInfos && ContactCount > 0; i++) {
      if (pGenericData->Usage == USAGE_DIGITIZER_TIP_SWITCH && pGenericData->Valid != 0) {
        HidEvent.Tip = (pGenericData->Value.u32 & 1u) + 1;
        if (pGenericData[1].Valid) {
          HidEvent.ID = pGenericData[1].Value.u32;
        }
        if (pGenericData[2].Valid) {
          HidEvent.X = _Convert(&pGenericData[2]);
        }
        if (pGenericData[3].Valid) {
          HidEvent.Y = _Convert(&pGenericData[3]);
        }
        ContactCount--;
        OS_PutMailCond(&_HIDMailBox, &HidEvent);
      }
      pGenericData++;
    }
    return;
  }
  //
  // Otherwise the device only reports one pair of (X,Y) coordinates.
  // Find the relevant fields in the array.
  //
  for (i = 0; i < NumGenericInfos; i++) {
    if (pGenericData->Valid != 0) {
      switch (pGenericData->Usage) {
      case USAGE_DIGITIZER_TIP_SWITCH:
        HidEvent.Tip = (pGenericData->Value.u32 & 1u) + 1;
        break;
      case USAGE_DIGITIZER_CONTACT_ID:
        HidEvent.ID = pGenericData->Value.u32;
        break;
      case USAGE_GENERIC_DESKTOP_X:
        HidEvent.X = _Convert(pGenericData);
        break;
      case USAGE_GENERIC_DESKTOP_Y:
        HidEvent.Y = _Convert(pGenericData);
        break;
      case USBH_HID_USAGE_DEVICE_TYPE:
        HidEvent.DeviceType = pGenericData->LogicalMin.u32;
        break;
      }
    }
    pGenericData++;
  }
  OS_PutMailCond(&_HIDMailBox, &HidEvent);
}

/*********************************************************************
*
*       _OnDevNotify
*
*  Function description
*    Callback, called when a device is added or removed.
*    Called in the context of the USBH_Task.
*    The functionality in this routine should not block!
*/
static void _OnDevNotify(void * pContext, U8 DevIndex, USBH_DEVICE_EVENT Event) {
  (void)pContext;
  switch (Event) {
  case USBH_DEVICE_EVENT_ADD:
    USBH_Logf_Application("**** Device added [%d]", DevIndex);
    break;
  case USBH_DEVICE_EVENT_REMOVE:
    USBH_Logf_Application("**** Device removed [%d]", DevIndex);
    break;
  default:;   // Should never happen
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
*       MainTask
*/
#ifdef __cplusplus
extern "C" {     /* Make sure we have C-declarations in C++ programs */
#endif
void MainTask(void);
#ifdef __cplusplus
}
#endif
void MainTask(void) {
  HID_EVENT  HidEvent;
  static const char *pAction[] = { "--", "release", "press  " };

  USBH_Init();
  OS_SetPriority(OS_GetTaskID(), TASK_PRIO_APP);                                       // This task has the lowest prio for real-time application.
                                                                                       // Tasks using emUSB-Host API should always have a lower priority than emUSB-Host main and ISR tasks.
  OS_CREATETASK(&_TCBMain, "USBH_Task", USBH_Task, TASK_PRIO_USBH_MAIN, _StackMain);   // Start USBH main task
  OS_CREATETASK(&_TCBIsr, "USBH_isr", USBH_ISRTask, TASK_PRIO_USBH_ISR, _StackIsr);    // Start USBH ISR task
  //
  // Create mailbox to store the HID events
  //
  OS_CREATEMB(&_HIDMailBox, sizeof(HID_EVENT), MAX_DATA_ITEMS, &_aHIDEvents);
  USBH_HID_Init();
  USBH_HID_RegisterNotification(_OnDevNotify, NULL);
  USBH_HID_SetOnGenericEvent(SEGGER_COUNTOF(UsageTab), UsageTab, _OnTouchPadChange);
  while (1) {
    BSP_ToggleLED(1);
    //
    // Get data from the mailbox, print information according to the event type.
    //
    OS_GetMail(&_HIDMailBox, &HidEvent);
    USBH_Logf_Application(" Device %x, Event %u, Action %s  @ %d %d", HidEvent.DeviceType, HidEvent.ID, pAction[HidEvent.Tip], HidEvent.X, HidEvent.Y);
  }
}
/*************************** End of file ****************************/
