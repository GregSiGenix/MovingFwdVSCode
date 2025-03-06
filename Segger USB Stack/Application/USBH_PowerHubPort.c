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

File    : USBH_PowerHubPort.c
Purpose : Demonstrates how to switch the power state of port of
          an external hub.

Additional information:
  Preparations:
    None.

  Expected behavior:
    When HUB is connected to the target the VBUS power of the first HUB port
    is toggled 4 time between off and on (only if the hub supports power switching).
    Then the application looks for a specific device and suspends it for 5 seconds.
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

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static OS_STACKPTR int     _StackMain[1536/sizeof(int)];
static OS_TASK             _TCBMain;

static OS_STACKPTR int     _StackIsr[1536/sizeof(int)];
static OS_TASK             _TCBIsr;

/*********************************************************************
*
*       Static Code
*
**********************************************************************
*/

/*********************************************************************
*
*       _GetHubInterface
*
*  Function description
*    Return the interface ID of the first connected hub.
*    Return 0, if no hub found.
*/
static USBH_INTERFACE_ID _GetHubInterface(void) {
  USBH_INTERFACE_MASK IfaceMask;
  unsigned int IfaceCount;
  USBH_INTERFACE_LIST_HANDLE hIfaceList;
  USBH_INTERFACE_ID Ret = 0;
  USBH_INTERFACE_INFO IfaceInfo;
  unsigned int i;

  memset(&IfaceMask, 0, sizeof(IfaceMask));
  IfaceMask.Mask = USBH_INFO_MASK_HUBS | USBH_INFO_MASK_CLASS;
  IfaceMask.Class = 9; // Hub class
  hIfaceList = USBH_CreateInterfaceList(&IfaceMask, &IfaceCount);
  if (hIfaceList == NULL) {
    USBH_Logf_Application("Cannot create the interface list!");
  } else {
    for (i = 0; i < IfaceCount; ++i) {
      if (USBH_GetInterfaceInfo(USBH_GetInterfaceId(hIfaceList, i), &IfaceInfo) == USBH_STATUS_SUCCESS) {
        Ret = IfaceInfo.InterfaceId;
        USBH_Logf_Application("Found HUB");
        break;
      }
    }
  }
  //
  // Ensure the list is properly cleaned up
  //
  USBH_DestroyInterfaceList(hIfaceList);
  return Ret;
}

/*********************************************************************
*
*       _GetDeviceInterface
*
*  Function description
*    Return the interface ID of the device with given VID and PID.
*    Return 0, if no hub found.
*/
static USBH_INTERFACE_ID _GetDeviceInterface(U16 VID, U16 PID) {
  USBH_INTERFACE_MASK IfaceMask;
  unsigned int IfaceCount;
  USBH_INTERFACE_LIST_HANDLE hIfaceList;
  USBH_INTERFACE_ID Ret = 0;
  USBH_INTERFACE_INFO IfaceInfo;
  unsigned int i;

  memset(&IfaceMask, 0, sizeof(IfaceMask));
  IfaceMask.Mask = USBH_INFO_MASK_VID | USBH_INFO_MASK_PID;
  IfaceMask.VendorId = VID;
  IfaceMask.ProductId = PID;
  hIfaceList = USBH_CreateInterfaceList(&IfaceMask, &IfaceCount);
  if (hIfaceList == NULL) {
    USBH_Logf_Application("Cannot create the interface list!");
  } else {
    for (i = 0; i < IfaceCount; ++i) {
      if (USBH_GetInterfaceInfo(USBH_GetInterfaceId(hIfaceList, i), &IfaceInfo) == USBH_STATUS_SUCCESS) {
        Ret = IfaceInfo.InterfaceId;
        USBH_Logf_Application("Found device");
        break;
      }
    }
  }
  //
  // Ensure the list is properly cleaned up
  //
  USBH_DestroyInterfaceList(hIfaceList);
  return Ret;
}

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
  USBH_INTERFACE_ID HubID;
  USBH_INTERFACE_ID DevID;
  int i;
  USBH_INTERFACE_HANDLE hInterface;
  USBH_URB URB;
  USBH_PORT_INFO PortInfo;

  USBH_Init();
  OS_SetPriority(OS_GetTaskID(), TASK_PRIO_APP);                                       // This task has the lowest prio for real-time application.
                                                                                       // Tasks using emUSB-Host API should always have a lower priority than emUSB-Host main and ISR tasks.
  OS_CREATETASK(&_TCBMain, "USBH_Task", USBH_Task, TASK_PRIO_USBH_MAIN, _StackMain);   // Start USBH main task
  OS_CREATETASK(&_TCBIsr, "USBH_isr", USBH_ISRTask, TASK_PRIO_USBH_ISR, _StackIsr);    // Start USBH ISR task

  //
  // 1st test: Wait for an external hub to be connected and power up and down port 1
  // of this hub 4 times for 5 seconds each.
  // Attention: Most hubs do not support VBUS switching.
  do {
    //
    // Wait for a hub to be connected.
    //
    OS_Delay(200);
    HubID = _GetHubInterface();
  } while (HubID == 0);
  OS_Delay(1000);
  for (i = 0; i < 4; i++) {
    //
    // Power off port 1 of hub
    //
    USBH_Logf_Application("Power off");
    if (USBH_SetHubPortPower(HubID, 1, USBH_POWER_OFF) != USBH_STATUS_SUCCESS) {
      break;
    }
    BSP_SetLED(1);
    OS_Delay(5000);
    //
    // Power on port 1 of hub
    //
    USBH_Logf_Application("Power on");
    if (USBH_SetHubPortPower(HubID, 1, USBH_NORMAL_POWER) != USBH_STATUS_SUCCESS) {
      break;
    }
    BSP_ClrLED(1);
    OS_Delay(5000);
  }

  //
  // 2nd test: Wait for an device with VID=8765 and PID=1120 to be connected and suspend it for 5 seconds.
  //
  do {
    //
    // Wait for the device be connected.
    //
    OS_Delay(200);
    DevID = _GetDeviceInterface(0x8765, 0x1120);
  } while (HubID == 0);
  OS_Delay(2000);
  if (USBH_OpenInterface(DevID, 0, &hInterface) == USBH_STATUS_SUCCESS) {
    //
    // Suspend device
    //
    USBH_Logf_Application("Suspend");
    URB.Header.Function = USBH_FUNCTION_SET_POWER_STATE;
    URB.Request.SetPowerState.PowerState = USBH_SUSPEND;
    USBH_SubmitUrb(hInterface, &URB);
    BSP_SetLED(1);
    OS_Delay(100);
    if (USBH_GetPortInfo(DevID, &PortInfo) == USBH_STATUS_SUCCESS) {
      USBH_Logf_Application("Port status = %x", PortInfo.PortStatus);
    }
    OS_Delay(5000);
    //
    // Resume device
    //
    USBH_Logf_Application("Resume");
    URB.Header.Function = USBH_FUNCTION_SET_POWER_STATE;
    URB.Request.SetPowerState.PowerState = USBH_NORMAL_POWER;
    USBH_SubmitUrb(hInterface, &URB);
    BSP_ClrLED(1);
    OS_Delay(100);
    if (USBH_GetPortInfo(DevID, &PortInfo) == USBH_STATUS_SUCCESS) {
      USBH_Logf_Application("Port status = %x", PortInfo.PortStatus);
    }
    USBH_CloseInterface(hInterface);
  }

  for (;;) {
    OS_Delay(5000);
  }
}


/*************************** End of file ****************************/
