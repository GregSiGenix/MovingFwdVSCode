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

File    : USBH_CDC_Start.c
Purpose : Shows emUSB-Host CDC API usage. The sample shows a simple
          echo server.

Additional information:
  Preparations:
    This sample requires a USB Communications Device Class
    compatible device.
    For testing purposes a second target can be programmed with
    emUSB-Device's "USB_CDC_Echo.c" sample.

  Expected behavior:
    When a CDC device is connected the Vendor and Product IDs
    are displayed. If the device performs communication
    the received data is shown in the terminal window.

  Sample output:
    <...>

    0:010 USBH_isr - INIT:    INIT: USBH_ISRTask started
    8:970 USBH_Task - **** Device added [0]

    9:010 MainTask - Vendor  Id = 0x8765
    9:010 MainTask - Product Id = 0x1111
    9:010 MainTask - Serial no. = 1
    9:010 MainTask - Speed      = FullSpeed
    9:010 MainTask - Started communication...
    9:010 MainTask - Received: "Hello, starting echo server"
    9:011 MainTask - Received: "Hello, starting echo server"

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
#include "USBH_CDC.h"

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
static OS_STACKPTR int     _StackMain[2048/sizeof(int)];
static OS_TASK             _TCBMain;

static OS_STACKPTR int     _StackIsr[1536/sizeof(int)];
static OS_TASK             _TCBIsr;
static volatile char       _DevIsReady;
static I8                  _DevIndex;
static U8                  _acData[512];

static USBH_NOTIFICATION_HOOK _Hook;


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
*       _OnSerialStateChange
*
*  Function description
*    Callback for serial state changes.
*    This callback is only executed when USBH_CDC_IGNORE_INT_EP is not set.
*/
static void _OnSerialStateChange(USBH_CDC_HANDLE hDevice, USBH_CDC_SERIALSTATE * pSerialState){
  USBH_Logf_Application("Serial state change for device %d RxCarrier: %d, TxCarrier: %d, Break: %d, RingSignal: %d, Framing: %d, Parity: %d, OverRun: %d.",
                        hDevice,
                        pSerialState->bRxCarrier,
                        pSerialState->bTxCarrier,
                        pSerialState->bBreak,
                        pSerialState->bRingSignal,
                        pSerialState->bFraming,
                        pSerialState->bParity,
                        pSerialState->bOverRun);
}

/*********************************************************************
*
*       _OnDevReady
*
*  Function description
*    Called by the application task (MainTask) when a USB device with a CDC-ACM is
*    plugged in.
*    It will show the device information of the device/CDC interface and will then send
*    the string "Hello, starting echo server" to the CDC-ACM interface.
*    From there it will send back what it has previously received in a loop
*    until the device is removed or an error occurred.
*/
static void _OnDevReady(void) {
  USBH_CDC_HANDLE      hDevice;
  USBH_CDC_DEVICE_INFO DeviceInfo;
  U32                  NumBytes;
  USBH_STATUS          Status;

  memset(&DeviceInfo, 0, sizeof(DeviceInfo));
  //
  // Open the device, the device index is retrieved from the notification callback.
  //
  hDevice = USBH_CDC_Open(_DevIndex);
  if (hDevice != USBH_CDC_INVALID_HANDLE) {
    //
    // Configure the CDC device.
    //
    USBH_CDC_SetTimeouts(hDevice, 50, 50);
    USBH_CDC_AllowShortRead(hDevice, 1);
    USBH_CDC_SetCommParas(hDevice, USBH_CDC_BAUD_115200, USBH_CDC_BITS_8, USBH_CDC_STOP_BITS_1, USBH_CDC_PARITY_NONE);
    //
    // For the serial state callback to work USBH_CDC_IGNORE_INT_EP flag must not be set.
    //
    USBH_CDC_SetOnSerialStateChange(hDevice, _OnSerialStateChange);
    //
    // Retrieve the information about the CDC device
    // and print them out.
    //
    USBH_CDC_GetDeviceInfo(hDevice, &DeviceInfo);
    USBH_Logf_Application("Vendor  Id = 0x%0.4X", DeviceInfo.VendorId);
    USBH_Logf_Application("Product Id = 0x%0.4X", DeviceInfo.ProductId);
    USBH_CDC_GetSerialNumber(hDevice, sizeof(_acData), _acData, &NumBytes);
    _acData[NumBytes] = 0;
    USBH_Logf_Application("Serial no. = %s", _acData);
    USBH_Logf_Application("Speed      = %s", _GetPortSpeed(DeviceInfo.Speed));
    //
    // Send out an information that the echo server is started.
    //
    USBH_CDC_Write(hDevice, (const U8 *)"Hello, starting echo server", 27, &NumBytes);
    USBH_Logf_Application("Started communication...");
    //
    // Do the echo server.
    //
    do {
      //
      // Receive the data.
      //
      Status = USBH_CDC_Read(hDevice, _acData, sizeof(_acData), &NumBytes);
      //
      // Timeout is also allowed, since we may have received some data
      // otherwise stop the echo server.
      //
      if ((Status != USBH_STATUS_SUCCESS) && (Status != USBH_STATUS_TIMEOUT)) {
        USBH_Logf_Application("Error occurred during reading from device");
        break;
      }
      //
      //  In case we have received some data, send them back.
      //
      if (NumBytes) {
        _acData[NumBytes] = 0;
        USBH_Logf_Application("Received: \"%s\"",(char *)_acData);
        Status = USBH_CDC_Write(hDevice, _acData, NumBytes, &NumBytes);
        //
        // Did an error occur, we stop the echo server
        //
        if ((Status != USBH_STATUS_SUCCESS) && (Status != USBH_STATUS_TIMEOUT)) {
          USBH_Logf_Application("Error occurred during writing to device");
          break;
        }
      }
    } while (1);
    //
    //  Close the device
    //
    USBH_CDC_Close(hDevice);
  }
  //
  // Wait until CDC is removed.
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
*    The functionality in this routine should not block!
*/
static void _cbOnAddRemoveDevice(void * pContext, U8 DevIndex, USBH_DEVICE_EVENT Event) {
  (void)pContext;
  switch (Event) {
  case USBH_DEVICE_EVENT_ADD:
    USBH_Logf_Application("**** Device added [%d]\n", DevIndex);
    _DevIndex = DevIndex;
    _DevIsReady = 1;
    break;
  case USBH_DEVICE_EVENT_REMOVE:
    USBH_Logf_Application("**** Device removed [%d]\n", DevIndex);
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
  USBH_CDC_Init();
  //
  // The following function call makes sure that even "broken" devices or host controllers are working.
  // Some devices return invalid information in the CDC descriptors regarding the control and data interface.
  // In order to allow those device we disable the interface check and allow any devices.
  // (-> set the USBH_CDC_DISABLE_INTERFACE_CHECK flag)
  // Furthermore some USB host controller are unable to process multiple read/write transactions
  // simultaneously (eg. KinetisFS host controller).
  // For that case we disable the INTERRUPT EP transaction.
  // Be aware that asynchronous operations on such host controllers will not work properly when processing more than one.
  //
  USBH_CDC_SetConfigFlags(USBH_CDC_IGNORE_INT_EP | USBH_CDC_DISABLE_INTERFACE_CHECK);
  USBH_CDC_AddNotification(&_Hook, _cbOnAddRemoveDevice, NULL);
  while (1) {
    BSP_ToggleLED(1);
    OS_Delay(100);
    if (_DevIsReady) {
      _OnDevReady();
    }
  }
}

/*************************** End of file ****************************/
