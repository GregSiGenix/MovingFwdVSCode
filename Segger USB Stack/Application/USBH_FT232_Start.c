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

File    : USBH_FT232_Start.c
Purpose : This sample demonstrates emUSB-Host's ability to handle FTDI
          serial-to-USB adapters.

Additional information:
  Preparations:
    The sample can be easily tested by connecting two serial
    adapters together with a null modem cable or adapter.
    One device should be connected to the target running emUSB-Host,
    the other side be connected to a PC.
    Any data which is sent from the PC (e.g. via a terminal emulation
    program) will be sent back by this sample.
    The terminal program should be configured to 115200 baud.

  Expected behavior:
    The sample will enumerate FTDI devices and after printing
    information about the connected device will echo back any
    received data.

  Sample output:
    Target:
    <...>

    3:213 MainTask - Vendor  Id = 0x0403
    3:213 MainTask - Product Id = 0x6001
    3:213 MainTask - bcdDevice  = 0x0600

    <...>
    PC Terminal program:
    Echoes whatever is sent.
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
#include "USBH_FT232.h"

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

static OS_STACKPTR int     _StackIsr[1024/sizeof(int)];
static OS_TASK             _TCBIsr;
static volatile char       _DevIsReady;
static I8                  _DevIndex;

static USBH_NOTIFICATION_HOOK _Hook;

/*********************************************************************
*
*       Static Code
*
**********************************************************************
*/

/*********************************************************************
*
*       _OnDevReady
*
*  Function description
*    Called by the application task (MainTask) when a FT232 device is
*    plugged in.
*    It will show the device information of the device/FT232 interface and will then send
*    the string "Hello, starting echo server" to the FT232 interface.
*    From there it will send back what it has previously received in a loop
*    until the device is removed or an error occurred.
*/
static void _OnDevReady(void) {
  USBH_FT232_HANDLE      hDevice;
  USBH_FT232_DEVICE_INFO DeviceInfo;
  U8                     acData[64] = {0};
  U32                    NumBytes;
  USBH_STATUS            Status;

  memset(&DeviceInfo, 0, sizeof(DeviceInfo));
  //
  // Open the device, the device index is retrieved from the notification callback.
  //
  hDevice = USBH_FT232_Open(_DevIndex);
  if (hDevice != USBH_FT232_INVALID_HANDLE) {
    //
    // Configure the FT232 device
    //
    USBH_FT232_AllowShortRead(hDevice, 1);
    USBH_FT232_SetTimeouts(hDevice, 300, 300);
    USBH_FT232_SetBaudRate(hDevice, USBH_FT232_BAUD_115200);
    USBH_FT232_SetDataCharacteristics(hDevice, USBH_FT232_BITS_8, USBH_FT232_STOP_BITS_1, USBH_FT232_PARITY_NONE);
    //
    // Retrieve the information about the FT232 device
    // and print them out.
    //
    USBH_FT232_GetDeviceInfo(hDevice, &DeviceInfo);
    USBH_Logf_Application("Vendor  Id = 0x%0.4X", DeviceInfo.VendorId);
    USBH_Logf_Application("Product Id = 0x%0.4X", DeviceInfo.ProductId);
    USBH_Logf_Application("bcdDevice  = 0x%0.4X", DeviceInfo.bcdDevice);
    //
    // Send out an information that the echo server is started.
    //
    strcat((char *)acData, "Hello, starting echo server\n");
    USBH_FT232_Write(hDevice, acData, 28, &NumBytes);
    memset(&acData[0], 0, sizeof(acData));
    //
    // Do the echo server
    //
    do {
      //
      // Receive the data
      //
      Status = USBH_FT232_Read(hDevice, acData, sizeof(acData), &NumBytes);
      //
      // Timeout is allowed, since we may have received some data
      // otherwise stop the echo server
      //
      if ((Status != USBH_STATUS_SUCCESS) && (Status != USBH_STATUS_TIMEOUT)) {
        USBH_Logf_Application("Error occurred during reading from device");
        break;
      }
      //
      //  In case we have received some data, send it back.
      //
      if (NumBytes) {
        acData[NumBytes] = 0;
        USBH_Logf_Application("Received: \"%s\"",(char *)acData);
        Status = USBH_FT232_Write(hDevice, acData, NumBytes, &NumBytes);
        //
        // Did an error occur, we stop the echo server
        //
        if ((Status != USBH_STATUS_SUCCESS) && (Status != USBH_STATUS_TIMEOUT)) {
          USBH_Logf_Application("Error occurred during writing to device\n");
          break;
        }
      }
    } while (1);
    //
    //  Close the device
    //
    USBH_FT232_Close(hDevice);
  }
  //
  // Wait until the device is removed.
  //
  while (_DevIsReady) {
    OS_Delay(100);
  }
}

/*********************************************************************
*
*       _cbOnAddRemoveDevice
*
*  Function description
*    Callback, called when a device is added or removed.
*    Called in the context of the USBH_Task.
*    The functionality in this routine should not block
*/
static void _cbOnAddRemoveDevice(void * pContext, U8 DevIndex, USBH_DEVICE_EVENT Event) {
  (void)pContext;
  switch (Event) {
  case USBH_DEVICE_EVENT_ADD:
    USBH_Logf_Application("**** Device added [%d]", DevIndex);
    _DevIndex = DevIndex;
    _DevIsReady = 1;
    break;
  case USBH_DEVICE_EVENT_REMOVE:
    USBH_Logf_Application("**** Device removed [%d]", DevIndex);
    _DevIsReady = 0;
    _DevIndex   = -1;
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
  USBH_Init();
  OS_SetPriority(OS_GetTaskID(), TASK_PRIO_APP);                                       // This task has the lowest prio for real-time application.
                                                                                       // Tasks using emUSB-Host API should always have a lower priority than emUSB-Host main and ISR tasks.
  OS_CREATETASK(&_TCBMain, "USBH_Task", USBH_Task, TASK_PRIO_USBH_MAIN, _StackMain);   // Start USBH main task
  OS_CREATETASK(&_TCBIsr, "USBH_isr", USBH_ISRTask, TASK_PRIO_USBH_ISR, _StackIsr);    // Start USBH ISR task
  USBH_FT232_Init();
  USBH_FT232_AddNotification(&_Hook, _cbOnAddRemoveDevice, NULL);
  while (1) {
    BSP_ToggleLED(1);
    OS_Delay(100);
    if (_DevIsReady) {
      _OnDevReady();
    }
  }
}

/*************************** End of file ****************************/
