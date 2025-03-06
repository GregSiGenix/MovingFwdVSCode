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

File    : USBH_HID_RemoteControl.c
Purpose : This sample is designed to present emUSBH's capability to
          enumerate Human Interface Devices and handle the input
          data accordingly.
          This sample will try to enumerate a connected
          remote control device and will print the control changes
          to the terminal.

Additional information:
  Preparations:
    None.

  Expected behavior:
    When a remote control device is connected the pressed keys will be displayed in the terminal.

  Sample output:
    <...>
    1:845 USBH_Task - APP: **** Device added [0]
    3:669 MainTask - APP: RC: vol inc: 0, vol dec: 1, mute: 0, play/pause: 0 next track: 0, prev track: 0, repeat: 0, random play: 0
    3:671 MainTask - APP: RC: vol inc: 0, vol dec: 0, mute: 0, play/pause: 0 next track: 0, prev track: 0, repeat: 0, random play: 0
    4:328 MainTask - APP: RC: vol inc: 1, vol dec: 0, mute: 0, play/pause: 0 next track: 0, prev track: 0, repeat: 0, random play: 0
    4:330 MainTask - APP: RC: vol inc: 0, vol dec: 0, mute: 0, play/pause: 0 next track: 0, prev track: 0, repeat: 0, random play: 0
    <...>
*/

/*********************************************************************
*
*       #include section
*
**********************************************************************
*/
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
#define MAX_DATA_ITEMS        10

/*********************************************************************
*
*       Defines non-configurable
*
**********************************************************************
*/
#define RC_EVENT       (1 << 0)
#define KEYBOARD_EVENT    (1 << 1)

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
  USBH_HID_RC_DATA  Data;
  U8                Event;
}  HID_EVENT;

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static OS_STACKPTR int _StackMain[1536/sizeof(int)];
static OS_TASK         _TCBMain;
static OS_STACKPTR int _StackIsr[1276/sizeof(int)];
static OS_TASK         _TCBIsr;
static HID_EVENT       _aHIDEvents[MAX_DATA_ITEMS];
static OS_MAILBOX      _HIDMailBox;

/*********************************************************************
*
*       Static Code
*
**********************************************************************
*/

/*********************************************************************
*
*       _OnRCChange
*
*  Function description
*    Callback, called from the USBH task when a remote control event occurs.
*/
static void _OnRCChange(USBH_HID_RC_DATA  * pRCData) {
  HID_EVENT  HidEvent;

  HidEvent.Event = RC_EVENT;
  HidEvent.Data  = *pRCData;
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
  static USBH_NOTIFICATION_HOOK Hook;
  HID_EVENT  HidEvent;

  USBH_Init();
  OS_SetPriority(OS_GetTaskID(), TASK_PRIO_APP);                                       // This task has the lowest prio for real-time application.
                                                                                       // Tasks using emUSB-Host API should always have a lower priority than emUSB-Host main and ISR tasks.
  OS_CREATETASK(&_TCBMain, "USBH_Task", USBH_Task, TASK_PRIO_USBH_MAIN, _StackMain);   // Start USBH main task
  OS_CREATETASK(&_TCBIsr, "USBH_isr", USBH_ISRTask, TASK_PRIO_USBH_ISR, _StackIsr);    // Start USBH ISR task
  USBH_HID_Init();
  USBH_HID_SetOnRCStateChange(_OnRCChange);
  USBH_HID_AddNotification(&Hook, _OnDevNotify, NULL);
  //
  // Create mailbox to store the HID events
  //
  OS_CREATEMB(&_HIDMailBox, sizeof(HID_EVENT), MAX_DATA_ITEMS, &_aHIDEvents);
  while (1) {
    BSP_ToggleLED(1);
    //
    // Get data from the mailbox, print information according to the event type.
    //
    OS_GetMail(&_HIDMailBox, &HidEvent);
    if ((HidEvent.Event & (RC_EVENT)) == RC_EVENT) {
      HidEvent.Event &= ~(RC_EVENT);
      USBH_Logf_Application("RC: vol inc: %d, vol dec: %d, mute: %d, play/pause: %d"
                            " next track: %d, prev track: %d, repeat: %d, random play: %d",
                            HidEvent.Data.VolumeIncrement, HidEvent.Data.VolumeDecrement,
                            HidEvent.Data.Mute, HidEvent.Data.PlayPause, HidEvent.Data.ScanNextTrack,
                            HidEvent.Data.ScanPreviousTrack, HidEvent.Data.Repeat, HidEvent.Data.RandomPlay);
    }
  }
}

/*************************** End of file ****************************/
