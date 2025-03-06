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

File    : USBH_CreateInterfaceList.c
Purpose : Demonstrates usage of the USBH_CreateInterfaceList
          and USBH_GetPortInfo function.

Additional information:
  Preparations:
    None.

  Expected behavior:
    When any USB device is connected to the target detailed
    information about the device and it's connection status
    is printed into the terminal.

  Sample output:
    <...>

    0:010 USBH_isr - INIT:    INIT: USBH_ISRTask started
    2:032 USBH_Task - **** Device added [1]

    2:110 MainTask - **** Device information for interface with ID 1
    2:110 MainTask -    Device ID: 1
    2:110 MainTask -    Vendor ID: 0x1366
    2:110 MainTask -    Product ID: 0x0101
    2:110 MainTask -    BCD device version: 0x100
    2:110 MainTask -    Interface number: 0
    2:110 MainTask -    Device class: 0xFF
    2:110 MainTask -    Device subclass: 0xFF
    2:110 MainTask -    Device protocol: 0xFF
    2:110 MainTask -    Number of open handles: 0
    2:110 MainTask -    Exclusive state: 0
    2:110 MainTask -    Speed: USBH_FULL_SPEED
    2:110 MainTask -    Serial Number: 000050000012
    2:110 MainTask -       connected to port 1 of controller 0

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
static volatile unsigned   _DevChanged;

/*********************************************************************
*
*       Static Code
*
**********************************************************************
*/

/*********************************************************************
*
*       _GetPortSpeed
*
*  Function description
*    Returns the given USBH speed as a string.
*/
static const char * _GetPortSpeed(USBH_SPEED Speed) {
  switch (Speed) {
    case USBH_LOW_SPEED:
      return "LowSpeed";
    case USBH_FULL_SPEED:
      return "FullSpeed";
    case USBH_HIGH_SPEED:
      return "HighSpeed";
    default:
      break;
  }
  return "Unknown";
}

/*********************************************************************
*
*       _ShowIfaceInfo
*
*  Function description
*    Displays all available information about a connected device.
*/
static void _ShowIfaceInfo(USBH_INTERFACE_ID IfaceId) {
  USBH_INTERFACE_INFO IfaceInfo;
  USBH_STATUS Status;
  U32 LenSerialNumber;
  U8 acSerialNumber[256];
  USBH_PORT_INFO PortInfo;
  unsigned i;

  Status = USBH_GetInterfaceInfo(IfaceId, &IfaceInfo);
  if (Status == USBH_STATUS_SUCCESS) {
    Status = USBH_GetInterfaceSerial(IfaceId, sizeof(acSerialNumber), acSerialNumber, &LenSerialNumber);
    if (Status == USBH_STATUS_SUCCESS) {
      //
      // The serial number is stored in Unicode (2 byte/characters).
      // We are only interested in the first byte, as normal characters
      // will only have 1 byte.
      //
      for (i = 0; i < LenSerialNumber/2; i += 1) {
        acSerialNumber[i] = acSerialNumber[i*2];
      }
      acSerialNumber[i] = 0;
    } else {
      acSerialNumber[0] = 0;
    }
    USBH_Logf_Application("**** Device information for interface with ID %u", IfaceInfo.InterfaceId);
    USBH_Logf_Application("   Device ID: %u", IfaceInfo.DeviceId);
    USBH_Logf_Application("   Vendor ID: 0x%0.4X", IfaceInfo.VendorId);
    USBH_Logf_Application("   Product ID: 0x%0.4X", IfaceInfo.ProductId);
    USBH_Logf_Application("   BCD device version: 0x%x", IfaceInfo.bcdDevice);
    USBH_Logf_Application("   Interface number: %d", IfaceInfo.Interface);
    USBH_Logf_Application("   Device class: 0x%x", IfaceInfo.Class);
    USBH_Logf_Application("   Device subclass: 0x%x", IfaceInfo.SubClass);
    USBH_Logf_Application("   Device protocol: 0x%x", IfaceInfo.Protocol);
    USBH_Logf_Application("   Number of open handles: %u", IfaceInfo.OpenCount);
    USBH_Logf_Application("   Exclusive state: %d", IfaceInfo.ExclusiveUsed);
    USBH_Logf_Application("   Speed: %s", _GetPortSpeed(IfaceInfo.Speed));
    USBH_Logf_Application("   Serial Number: %s", acSerialNumber);
  } else {
    USBH_Warnf_Application("**** Failed to retrieve device information");
  }
  //
  // Display connection tree.
  //
  while (USBH_GetPortInfo(IfaceId, &PortInfo) == USBH_STATUS_SUCCESS) {
    if (PortInfo.IsRootHub) {
      USBH_Logf_Application("      connected to port %u of controller %u", PortInfo.PortNumber, PortInfo.HCIndex);
      break;
    }
    USBH_Logf_Application("      connected to port %u of HUB %u", PortInfo.PortNumber, PortInfo.HubDeviceId);
    IfaceId = PortInfo.HubInterfaceId;
  }
}

/*********************************************************************
*
*       _OnDevReady
*
*  Function description
*    Called by the application task (MainTask) when a device is plugged in.
*    It calls _ShowIfaceInfo with the corresponding interface ID.
*/
static void _OnDevReady(void) {
  USBH_INTERFACE_MASK IfaceMask;
  unsigned int IfaceCount;
  USBH_INTERFACE_LIST_HANDLE hIfaceList;

  memset(&IfaceMask, 0, sizeof(IfaceMask));
  IfaceMask.Mask = USBH_INFO_MASK_HUBS;
  //
  // An interface mask of 0es will result in all devices being included
  // in the list. A user can filter for specific devices by choosing
  // a different mask. A detailed description of the USBH_INTERFACE_MASK
  // can be found in the USBH documentation.
  //
  // Example:
  //
  //IfaceMask.Mask = USBH_INFO_MASK_VID | USBH_INFO_MASK_PID;
  //IfaceMask.VendorId = 0x1234;
  //IfaceMask.ProductId = 0x6789;
  //
  hIfaceList = USBH_CreateInterfaceList(&IfaceMask, &IfaceCount);

  if (hIfaceList == NULL) {
    USBH_Logf_Application("Cannot create the interface list!");
  } else {
    if (IfaceCount == 0) {
      USBH_Logf_Application("No devices found.");
    } else {
      unsigned int i;
      USBH_INTERFACE_ID IfaceId;
      //
      // Traverse the list of devices and display information about each of them
      //
      for (i = 0; i < IfaceCount; ++i) {
        //
        // An interface is addressed by its Id
        //
        IfaceId = USBH_GetInterfaceId(hIfaceList, i);
        _ShowIfaceInfo(IfaceId);
      }
    }
  }
  //
  // Ensure the list is properly cleaned up
  //
  USBH_DestroyInterfaceList(hIfaceList);
}

/*********************************************************************
*
*       _cbOnAddRemoveDevice
*
*  Function description
*    Callback, called when a device is attached or detached.
*    Called in the context of the USBH_Task.
*    The functionality in this routine should not block!
*/
static void _cbOnAddRemoveDevice(void * pContext, USBH_PNP_EVENT Event, USBH_INTERFACE_ID InterfaceId) {
  (void)pContext;
  switch (Event) {
  case USBH_ADD_DEVICE:
    USBH_Logf_Application("**** Device added [%d]\n", InterfaceId);
    _DevChanged = 1;
    break;
  case USBH_REMOVE_DEVICE:
    USBH_Logf_Application("**** Device removed [%d]\n", InterfaceId);
    _DevChanged = 1;
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
  USBH_PNP_NOTIFICATION pPnPNotification;

  USBH_Init();
  OS_SetPriority(OS_GetTaskID(), TASK_PRIO_APP);                                       // This task has the lowest prio for real-time application.
                                                                                       // Tasks using emUSB-Host API should always have a lower priority than emUSB-Host main and ISR tasks.
  OS_CREATETASK(&_TCBMain, "USBH_Task", USBH_Task, TASK_PRIO_USBH_MAIN, _StackMain);   // Start USBH main task
  OS_CREATETASK(&_TCBIsr, "USBH_isr", USBH_ISRTask, TASK_PRIO_USBH_ISR, _StackIsr);    // Start USBH ISR task
  //
  // Register a call back function which will be called when devices are
  // attached or detached.
  //
  memset(&pPnPNotification, 0, sizeof(pPnPNotification));
  pPnPNotification.pfPnpNotification = _cbOnAddRemoveDevice;
  USBH_RegisterPnPNotification(&pPnPNotification);

  while (1) {
    BSP_ToggleLED(1);
    OS_Delay(100);
    if (_DevChanged) {
      _DevChanged = 0;
      _OnDevReady();
    }
  }
}


/*************************** End of file ****************************/
