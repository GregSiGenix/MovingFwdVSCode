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
File        : USBH_FT232.c
Purpose     : API of the USB host stack
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#include "USBH_Int.h"
#include "USBH_FT232.h"
#include "USBH_BULK.h"
#include "USBH_Util.h"

/*********************************************************************
*
*       Defines, fixed
*
**********************************************************************
*/
#define USBH_FT232_DEFAULT_TIMEOUT     5000
#define USBH_FT232_NUM_DEVICES          32u // NOTE: Limited by the number of bits in DevIndexUsedMask which by now is 32

#define USBH_FT232_REMOVAL_TIMEOUT      100

#define FT232_HEADER_SIZE                2u
#define FT232_IFACE_ID                   0u // TODO: this does not work with composite devices.

#define FT232_REQUEST_RESET           0x00u // Reset the communication port
#define FT232_REQUEST_MODEMCTRL       0x01u // Set the modem control register
#define FT232_REQUEST_SETFLOWCTRL     0x02u // Set flow control options
#define FT232_REQUEST_SETBAUDRATE     0x03u // Set the baud rate
#define FT232_REQUEST_SETDATA         0x04u // Set the data characteristics of the port
#define FT232_REQUEST_GETMODEMSTAT    0x05u // Retrieve the current value of the modem status register
#define FT232_REQUEST_SETEVENTCHAR    0x06u // Set the event character
#define FT232_REQUEST_SETERRORCHAR    0x07u // Set the error character
#define FT232_REQUEST_SETLATTIMER     0x09u // Set the latency timer
#define FT232_REQUEST_GETLATTIMER     0x0Au // Return the latency timer
#define FT232_REQUEST_SETBITMODE      0x0Bu // Set a special bit mode or turn on a special function
#define FT232_REQUEST_GETBITMODE      0x0Cu // Return the current values on the data bus pins

#define FT232_POS_PARITY              0x08u
#define FT232_POS_STOP_BIT            0x0bu
#define FT232_POS_BREAK               0x0eu

#define FT232_POS_ERRORCHAR_ENABLE    0x08u
#define FT232_POS_EVENT_ENABLE        0x08u


#define FT232_DTR_BIT                 0x00u
#define FT232_RTS_BIT                 0x01u
#define FT232_DTR_ENABLE_BIT          0x08u
#define FT232_RTS_ENABLE_BIT          0x09u

typedef struct _USBH_FT232_INST {
  struct _USBH_FT232_INST     * pNext;
  USBH_BULK_HANDLE              hBulkDevice;
  I8                            IsOpened;
  U8                            DevIndex;
  U8                            BulkInEPAddr;
  I8                            Removed;
  USBH_TIMER                    RemovalTimer;
  U16                           BulkInMaxPacketSize;
  U8                            BulkOutEPAddr;
  U8                          * pInBuffer;
  USBH_FT232_HANDLE             Handle;
  U32                           ReadTimeOut;
  U32                           WriteTimeOut;
  U8                            AllowShortRead;
  U16                           DataCharateristic;
  USBH_BUFFER                   RxRingBuffer;
  USBH_INTERFACE_ID             InterfaceID;
} USBH_FT232_INST;

typedef struct {
  USBH_FT232_INST           * pFirst;
  USBH_FT232_HANDLE           NextHandle;
  USBH_NOTIFICATION_HOOK    * pFirstNotiHook;
  U32                         DefaultReadTimeOut;
  U32                         DefaultWriteTimeOut;
  U32                         DevIndexUsedMask;
  U8                          NumDevices;
} USBH_FT232_GLOBAL;

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static USBH_FT232_GLOBAL      USBH_FT232_Global;
static USBH_NOTIFICATION_HOOK _FT232_Hook;
static USBH_NOTIFICATION_HOOK _FT232_Hook_Custom;
static I8                     _isInited;

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _AllocateDevIndex()
*
*   Function description
*     Searches for an available device index which is the index
*     of the first cleared bit in the DevIndexUsedMask.
*
*   Return value
*     A device index or USBH_FT232_NUM_DEVICES in case all device indexes are allocated.
*/
static U8 _AllocateDevIndex(void) {
  U8 i;
  U32 Mask;

  Mask = 1;
  for (i = 0; i < USBH_FT232_NUM_DEVICES; ++i) {
    if ((USBH_FT232_Global.DevIndexUsedMask & Mask) == 0u) {
      USBH_FT232_Global.DevIndexUsedMask |= Mask;
      break;
    }
    Mask <<= 1;
  }
  return i;
}

/*********************************************************************
*
*       _FreeDevIndex()
*
*   Function description
*     Marks a device index as free by clearing the corresponding bit
*     in the DevIndexUsedMask.
*
*   Parameters
*     DevIndex     - Device Index that should be freed.
*/
static void _FreeDevIndex(U8 DevIndex) {
  U32 Mask;

  if (DevIndex < USBH_FT232_NUM_DEVICES) {
    Mask = (1UL << DevIndex);
    USBH_FT232_Global.DevIndexUsedMask &= ~Mask;
  }
}

/*********************************************************************
*
*       _h2p()
*/
static USBH_FT232_INST * _h2p(USBH_FT232_HANDLE Handle) {
  USBH_FT232_INST * pInst;

  if (Handle == USBH_FT232_INVALID_HANDLE) {
    return NULL;
  }
  //
  // Iterate over linked list to find an instance with matching handle. Return if found.
  //
  pInst = USBH_FT232_Global.pFirst;
  while (pInst != NULL) {
    if (pInst->Handle == Handle) {                                        // Match ?
      return pInst;
    }
    pInst = pInst->pNext;
  }
  //
  // Error handling: Device handle not found in list.
  //
  USBH_WARN((USBH_MCAT_FT232, "HANDLE: handle %d not in instance list", Handle));
  return NULL;
}

/*********************************************************************
*
*       _RemovalTimer
*/
static void _RemovalTimer(void * pContext) {
  USBH_FT232_INST * pInst;
  USBH_FT232_INST * pPrev;
  USBH_FT232_INST * pCurrent;

  pInst = USBH_CTX2PTR(USBH_FT232_INST, pContext);
  if (pInst->Removed == 0 || pInst->IsOpened != 0) {
    USBH_StartTimer(&pInst->RemovalTimer, USBH_FT232_REMOVAL_TIMEOUT);
    return;
  }
  USBH_ReleaseTimer(&pInst->RemovalTimer);
  //
  // Remove instance from list
  //
  if (pInst == USBH_FT232_Global.pFirst) {
    USBH_FT232_Global.pFirst = pInst->pNext;
  } else {
    pPrev = USBH_FT232_Global.pFirst;
    for (pCurrent = pPrev->pNext; pCurrent != NULL; pCurrent = pCurrent->pNext) {
      if (pCurrent == pInst) {
        pPrev->pNext = pInst->pNext;
        break;
      }
      pPrev = pCurrent;
    }
  }
  _FreeDevIndex(pInst->DevIndex);
  (void)USBH_BULK_Close(pInst->hBulkDevice);
  //
  // Free the memory that is used by the instance
  //
  if (pInst->pInBuffer != NULL) {
    USBH_FREE(pInst->pInBuffer);
  }
  if (pInst->RxRingBuffer.pData != NULL) {
    USBH_FREE(pInst->RxRingBuffer.pData);
  }
  USBH_FREE(pInst);
  USBH_FT232_Global.NumDevices--;
}

/*********************************************************************
*
*       _CreateDevInstance
*/
static USBH_FT232_INST * _CreateDevInstance(USBH_BULK_HANDLE hDevice, const USBH_BULK_DEVICE_INFO * pDevInfo) {
  USBH_FT232_INST * pInst;
  USBH_BULK_EP_INFO EPInfo1;
  USBH_BULK_EP_INFO EPInfo2;
  USBH_STATUS       Status;

  //
  // Check if max. number of devices allowed is exceeded.
  //
  pInst = NULL;
  if ((USBH_FT232_Global.NumDevices + 1u) > USBH_FT232_NUM_DEVICES) {
    USBH_WARN((USBH_MCAT_FT232, "No instance available for creating a new FT232 device! (Increase USBH_FT232_NUM_DEVICES)"));
  } else {
    if (pDevInfo->NumEPs == 2u) {
      Status = USBH_BULK_GetEndpointInfo(hDevice, 0, &EPInfo1);
      if (Status == USBH_STATUS_SUCCESS) {
        Status = USBH_BULK_GetEndpointInfo(hDevice, 1, &EPInfo2);
        if (Status == USBH_STATUS_SUCCESS) {
          if ((EPInfo1.Type == USB_EP_TYPE_BULK) && (EPInfo2.Type == USB_EP_TYPE_BULK)) {
            pInst = (USBH_FT232_INST *)USBH_TRY_MALLOC_ZEROED(sizeof(USBH_FT232_INST));
            if (pInst != NULL) {
              if ((EPInfo1.Addr & 0x80u) == 0x80u) {
                pInst->BulkInEPAddr         = EPInfo1.Addr;
                pInst->BulkInMaxPacketSize  = EPInfo1.MaxPacketSize;
                pInst->BulkOutEPAddr        = EPInfo2.Addr;
              } else {
                pInst->BulkInEPAddr         = EPInfo2.Addr;
                pInst->BulkInMaxPacketSize  = EPInfo2.MaxPacketSize;
                pInst->BulkOutEPAddr        = EPInfo1.Addr;
              }
              pInst->Handle           = ++USBH_FT232_Global.NextHandle;
              pInst->hBulkDevice      = hDevice;
              pInst->InterfaceID      = pDevInfo->InterfaceID;
              pInst->DevIndex         = _AllocateDevIndex();
              pInst->pNext = USBH_FT232_Global.pFirst;
              USBH_FT232_Global.pFirst = pInst;
              USBH_FT232_Global.NumDevices++;
            }
          }
        }
      }
    }
  }
  return pInst;
}

/*********************************************************************
*
*       _StartDevice
*
*  Function description
*   Starts the application and is called if a USB device is connected.
*   The function uses the first interface of the device.
*
*  Parameters
*    pInst  : Pointer to the FT232 device instance.
*
*  Return value
*    == USBH_STATUS_SUCCESS: Successful.
*    != USBH_STATUS_SUCCESS: An error occurred.
*/
static USBH_STATUS _StartDevice(USBH_FT232_INST * pInst) {
  USBH_STATUS  Status;

  pInst->pInBuffer = (U8 *)USBH_TRY_MALLOC(pInst->BulkInMaxPacketSize);
  if (pInst->pInBuffer == NULL) {
    USBH_WARN((USBH_MCAT_FT232, "Buffer allocation failed."));
    Status = USBH_STATUS_RESOURCES;
  } else {
    pInst->RxRingBuffer.pData   = (U8 *)USBH_TRY_MALLOC(pInst->BulkInMaxPacketSize);
    if (pInst->RxRingBuffer.pData == NULL) {
      USBH_WARN((USBH_MTYPE_FT232, "Buffer allocation failed."));
      Status = USBH_STATUS_RESOURCES;
    } else {
      Status = USBH_STATUS_SUCCESS;
      USBH_LOG((USBH_MCAT_FT232, "Address   MaxPacketSize"));
      USBH_LOG((USBH_MCAT_FT232, "0x%02X      %5d      ", pInst->BulkInEPAddr, pInst->BulkInMaxPacketSize));
      pInst->ReadTimeOut  = USBH_FT232_Global.DefaultReadTimeOut;
      pInst->WriteTimeOut = USBH_FT232_Global.DefaultWriteTimeOut;
      pInst->RxRingBuffer.Size    = pInst->BulkInMaxPacketSize;
    }
  }
  return Status;
}

/*********************************************************************
*
*       _OnDeviceNotification
*
*/
static void _OnDeviceNotification(USBH_FT232_INST * pInst, USBH_DEVICE_EVENT Event, USBH_INTERFACE_ID InterfaceID) {
  USBH_STATUS               Status;
  USBH_NOTIFICATION_HOOK  * pHook;

  switch (Event) {
  case USBH_DEVICE_EVENT_ADD:
    USBH_LOG((USBH_MCAT_FT232, "_OnDeviceNotification: USB FT232 device detected interface ID: %u !", InterfaceID));
    // Only one device is handled from the application at the same time
    pInst->InterfaceID = InterfaceID;
    Status = _StartDevice(pInst);
    if (Status == USBH_STATUS_SUCCESS) {
      pHook = USBH_FT232_Global.pFirstNotiHook;
      while (pHook != NULL) {
        if (pHook->pfNotification != NULL) {
          pHook->pfNotification(pHook->pContext, pInst->DevIndex, Event);
        }
        pHook = pHook->pNext;
      }
    }
    break;
  case USBH_DEVICE_EVENT_REMOVE:
    if (pInst != NULL) {
      USBH_LOG((USBH_MCAT_FT232, "_OnDeviceNotification: USB FT232 device removed interface  ID: %u !", InterfaceID));
      pHook = USBH_FT232_Global.pFirstNotiHook;
      while (pHook != NULL) {
        if (pHook->pfNotification != NULL) {
          pHook->pfNotification(pHook->pContext, pInst->DevIndex, Event);
        }
        pHook = pHook->pNext;
      }
    }
    break;
  default:
    USBH_WARN((USBH_MCAT_FT232, "_OnDeviceNotification: invalid Event: %d !", Event));
    break;
  }
}

/*********************************************************************
*
*       _cbOnAddRemoveDevice
*/
static void _cbOnAddRemoveDevice (void * pContext, U8 DevIndex, USBH_DEVICE_EVENT Event) {
  USBH_FT232_INST            * pInst;
  USBH_STATUS                  Status;
  USBH_BULK_HANDLE             hDevice;
  USBH_BULK_DEVICE_INFO        DevInfo;
  USBH_BOOL                    Found;

  pInst = NULL;
  USBH_USE_PARA(pContext);
  switch (Event) {
  case USBH_DEVICE_EVENT_ADD:
    hDevice = USBH_BULK_Open(DevIndex);
    if (hDevice != USBH_BULK_INVALID_HANDLE) {
      Status = USBH_BULK_GetDeviceInfo(hDevice, &DevInfo);
      if (Status == USBH_STATUS_SUCCESS) {
        pInst = _CreateDevInstance(hDevice, &DevInfo);
        if (pInst != NULL) {
          pInst->DevIndex = DevIndex;
          _OnDeviceNotification(pInst, Event, pInst->InterfaceID);
        } else {
          USBH_WARN((USBH_MCAT_FT232, "_cbOnAddRemoveDevice: device instance not created!"));
        }
      } else {
        USBH_WARN((USBH_MCAT_FT232, "_cbOnAddRemoveDevice: USBH_BULK_GetDeviceInfo failed!"));
      }
    } else {
      USBH_WARN((USBH_MCAT_FT232, "_cbOnAddRemoveDevice: USBH_BULK_Open failed!"));
    }
    break;
  case USBH_DEVICE_EVENT_REMOVE:
    pInst = USBH_FT232_Global.pFirst;
    Found = FALSE;
    while (pInst != NULL) {   // Iterate over all instances
      if (pInst->DevIndex == DevIndex) {
        Found = TRUE;
        pInst->Removed = 1;
        _OnDeviceNotification(pInst, Event, pInst->InterfaceID);
        USBH_InitTimer(&pInst->RemovalTimer, _RemovalTimer, pInst);
        USBH_StartTimer(&pInst->RemovalTimer, USBH_FT232_REMOVAL_TIMEOUT);
        break;
      }
      pInst = pInst->pNext;
    }
    if (Found == FALSE) {
      USBH_WARN((USBH_MCAT_FT232, "_cbOnAddRemoveDevice: pInst not found for notified interface!"));
    }
    break;
  default:
    // Should never happen
    break;
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
*       USBH_FT232_Init
*
*  Function description
*    Initializes and registers the FT232 device driver with emUSB-Host.
*
*  Return value
*    == 1:  Success.
*    == 0: Could not register FT232 device driver.
*/
U8 USBH_FT232_Init(void) {
  USBH_INTERFACE_MASK   InterfaceMask;
  USBH_STATUS           Status;

  USBH_LOG((USBH_MCAT_FT232, "USBH_FT232_Init"));
  if (_isInited == 0) {
    USBH_MEMSET(&USBH_FT232_Global, 0, sizeof(USBH_FT232_Global));
    USBH_FT232_Global.DefaultReadTimeOut  = USBH_FT232_DEFAULT_TIMEOUT;
    USBH_FT232_Global.DefaultWriteTimeOut = USBH_FT232_DEFAULT_TIMEOUT;
    // Add a plug an play notification routine
    InterfaceMask.Mask        = USBH_INFO_MASK_VID;
    InterfaceMask.VendorId    = 0x0403; // FTDI vendor ID
    Status = USBH_BULK_Init(NULL);
    if (Status != USBH_STATUS_SUCCESS) {
      return 0;
    }
    Status = USBH_BULK_AddNotification(&_FT232_Hook, _cbOnAddRemoveDevice, NULL, &InterfaceMask);
    if (Status != USBH_STATUS_SUCCESS) {
      USBH_WARN((USBH_MCAT_FT232, "USBH_FT232_Init: USBH_BULK_AddNotification failed"));
      return 0;
    }
    _isInited++;
  }
  return 1;
}

/*********************************************************************
*
*       USBH_FT232_Exit
*
*  Function description
*    Unregisters and de-initializes the FT232 device driver from emUSB-Host.
*
*  Additional information
*    Before this function is called any notifications added via
*    USBH_FT232_AddNotification() must be removed
*    via USBH_FT232_RemoveNotification().
*    This function will release resources that were used by this device
*    driver. It has to be called if the application is closed. This has
*    to be called before USBH_Exit() is called. No more functions of
*    this module may be called after calling USBH_FT232_Exit(). The
*    only exception is USBH_FT232_Init(), which would in turn
*    reinitialize the module and allows further calls.
*/
void USBH_FT232_Exit(void) {
  USBH_FT232_INST * pInst;

  USBH_LOG((USBH_MCAT_FT232, "USBH_FT232_Exit"));
  _isInited--;
  if (_isInited == 0) {
    (void)USBH_BULK_RemoveNotification(&_FT232_Hook);
    (void)USBH_BULK_RemoveNotification(&_FT232_Hook_Custom);
    pInst = USBH_FT232_Global.pFirst;
    while (pInst != NULL) {   // Iterate over all instances
      pInst->Removed = 1;
      pInst->IsOpened = 0;
      pInst = pInst->pNext;
    }
    USBH_BULK_Exit();
  }
}

/*********************************************************************
*
*       USBH_FT232_Open
*
*  Function description
*    Opens a device given by an index.
*
*  Parameters
*    Index   : Index of the device that shall be opened.
*              In general this means: the first connected device is 0,
*              second device is 1 etc.
*
*  Return value
*    != USBH_FT232_INVALID_HANDLE     : Handle to the device.
*    == USBH_FT232_INVALID_HANDLE     : Device could not be opened (removed or not available).
*/
USBH_FT232_HANDLE USBH_FT232_Open(unsigned Index) {
  USBH_FT232_INST * pInst;
  USBH_FT232_HANDLE Handle;

  Handle = USBH_FT232_INVALID_HANDLE;
  for (pInst = USBH_FT232_Global.pFirst; pInst != NULL; pInst = pInst->pNext) {
    if (pInst->DevIndex == Index && pInst->Removed == 0) {
      Handle = pInst->Handle;
      pInst->IsOpened++;
      break;
    }
  }
  return Handle;
}

/*********************************************************************
*
*       USBH_FT232_Close
*
*  Function description
*    Closes a handle to an opened device.
*
*  Parameters
*    hDevice    :  Handle to a opened device.
*
*  Return value
*    == USBH_STATUS_SUCCESS: Successful.
*    != USBH_STATUS_SUCCESS: An error occurred.
*/
USBH_STATUS USBH_FT232_Close(USBH_FT232_HANDLE hDevice) {
  USBH_FT232_INST * pInst;

  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    if (pInst->IsOpened == 0) {
      return USBH_STATUS_NOT_OPENED;
    } else {
      pInst->IsOpened--;
      return USBH_STATUS_SUCCESS;
    }
  }
  return USBH_STATUS_DEVICE_REMOVED;
}

/*********************************************************************
*
*       USBH_FT232_Write
*
*  Function description
*    Writes data to the FT232 device.
*
*  Parameters
*    hDevice          : Handle to the opened device.
*    pData            : [IN] Pointer to data to be sent.
*    NumBytes         : Number of bytes to write to the device.
*    pNumBytesWritten : [OUT] Pointer to a variable which receives the number
*                       of bytes written to the device.
*
*  Return value
*    == USBH_STATUS_SUCCESS: Successful.
*    != USBH_STATUS_SUCCESS: An error occurred.
*/
USBH_STATUS USBH_FT232_Write(USBH_FT232_HANDLE hDevice, const U8 * pData, U32 NumBytes, U32 * pNumBytesWritten) {
  USBH_FT232_INST * pInst;
  USBH_STATUS       Status;

  pInst = _h2p(hDevice);
  if (pInst == NULL) {
    Status = USBH_STATUS_INVALID_HANDLE;
  } else {
    if (pInst->IsOpened == 0) {
      return USBH_STATUS_NOT_OPENED;
    }
    if (pInst->Removed != 0) {
      return USBH_STATUS_DEVICE_REMOVED;
    }
    Status = USBH_BULK_Write(pInst->hBulkDevice, pInst->BulkOutEPAddr, pData, NumBytes, pNumBytesWritten, pInst->WriteTimeOut);
    if (Status != USBH_STATUS_SUCCESS) {
      USBH_WARN((USBH_MCAT_FT232, "USBH_FT232_Write failed, Status = %s", USBH_GetStatusStr(Status)));
    }
  }
  return Status;
}

/*********************************************************************
*
*       USBH_FT232_Read
*
*  Function description
*    Reads data from the FT232 device.
*
*  Parameters
*    hDevice        : Handle to the opened device.
*    pData          : Pointer to a buffer to store the read data.
*    NumBytes       : Number of bytes to be read from the device.
*    pNumBytesRead  : [OUT] Pointer to a variable which receives the number
*                     of bytes read from the device.
*
*  Return value
*    == USBH_STATUS_SUCCESS: Successful.
*    != USBH_STATUS_SUCCESS: An error occurred.
*
*  Additional information
*    USBH_FT232_Read() always returns the number of bytes read in
*    pNumBytesRead. This function does not return until NumBytes bytes
*    have been read into the buffer unless short read mode is enabled.
*    This allows USBH_FT232_Read() to return when either data have been
*    read from the queue or as soon as some data have been read from
*    the device. The number of bytes in the receive queue can be
*    determined by calling USBH_FT232_GetQueueStatus(), and passed to
*    USBH_FT232_Read() as NumBytes so that the function reads the data
*    and returns immediately. When a read timeout value has been
*    specified in a previous call to USBH_FT232_SetTimeouts(),
*    USBH_FT232_Read() returns when the timer expires or NumBytes have
*    been read, whichever occurs first. If the timeout occurs,
*    USBH_FT232_Read() reads available data into the buffer and
*    returns USBH_STATUS_TIMEOUT. An application should use
*    the function return value and pNumBytesRead when processing
*    the buffer. If the return value is USBH_STATUS_SUCCESS, and
*    pNumBytesRead is equal to NumBytes then USBH_FT232_Read has
*    completed normally. If the return value is USBH_STATUS_TIMEOUT,
*    pNumBytesRead may be less or even 0, in any case, pData will be
*    filled with pNumBytesRead. Any other return value suggests an
*    error in the parameters of the function, or a fatal error like a
*    USB disconnect.
*/
USBH_STATUS USBH_FT232_Read(USBH_FT232_HANDLE hDevice, U8 * pData, U32 NumBytes, U32 * pNumBytesRead) {
  USBH_FT232_INST * pInst;
  USBH_STATUS       Status;
  U32               NumBytesRead;
  U32               NumBytes2Copy;
  U32               NumBytesTransfered;
  USBH_TIME         ExpiredTime;

  pInst = _h2p(hDevice);
  if (pInst == NULL) {
    Status = USBH_STATUS_INVALID_HANDLE;
  } else {
    if (pInst->IsOpened == 0) {
      return USBH_STATUS_NOT_OPENED;
    }
    if (pInst->Removed != 0) {
      return USBH_STATUS_DEVICE_REMOVED;
    }
    //
    // Check internal buffer first.
    //
    NumBytesTransfered = USBH_BUFFER_Read(&pInst->RxRingBuffer, pData, NumBytes);
    NumBytes          -= NumBytesTransfered;
    pData             += NumBytesTransfered;
    if (pNumBytesRead != NULL) {
      *pNumBytesRead   = NumBytesTransfered;
    }
    if (NumBytes == 0u) {
      //
      // Read request satisfied from the internal buffer.
      //
      Status = USBH_STATUS_SUCCESS;
    } else {
      ExpiredTime = USBH_TIME_CALC_EXPIRATION(pInst->ReadTimeOut);
      do {
        //
        // Check for timeout.
        //
        if (pInst->ReadTimeOut != 0u  && USBH_TIME_IS_EXPIRED(ExpiredTime)) {
          Status = USBH_STATUS_TIMEOUT;
          break;
        } else {
          //
          // Read a single packet from the device.
          //
          Status = USBH_BULK_Receive(pInst->hBulkDevice, pInst->BulkInEPAddr, pInst->pInBuffer, &NumBytesRead, USBH_FT232_DEFAULT_TIMEOUT);
          if (Status == USBH_STATUS_SUCCESS) {
            //
            // In case we have more than 2 byte received, we have some application data
            // First 2 byte are modem and line status bytes.
            //
            if (NumBytesRead != FT232_HEADER_SIZE) {
              NumBytesRead -= FT232_HEADER_SIZE;
              NumBytes2Copy = USBH_MIN(NumBytesRead, NumBytes);
              USBH_MEMCPY(pData, &pInst->pInBuffer[FT232_HEADER_SIZE], NumBytes2Copy);
              if (pNumBytesRead != NULL) {
                *pNumBytesRead += NumBytes2Copy;
              }
              pData          += NumBytes2Copy;
              NumBytes       -= NumBytes2Copy;
              NumBytesRead   -= NumBytes2Copy;
              if (NumBytesRead != 0u) {
                USBH_BUFFER_Write(&pInst->RxRingBuffer, &pInst->pInBuffer[FT232_HEADER_SIZE + NumBytes2Copy], NumBytesRead);
              }
              if (pInst->AllowShortRead != 0u) {
                break;
              }
            } else {
              //
              // Same as the FT232 PC driver, we would wait for approx. 15 ms before retrying.
              // Otherwise we would achieve a high CPU load since the device always answers with the 2 status bytes.
              //
              USBH_OS_Delay(15);
            }
          }
        }
      } while (NumBytes > 0u && Status == USBH_STATUS_SUCCESS);
    }
  }
  if (Status != USBH_STATUS_SUCCESS && Status != USBH_STATUS_TIMEOUT) {
    USBH_WARN((USBH_MCAT_FT232, "USBH_FT232_Read failed, Status = %s", USBH_GetStatusStr(Status)));
  }
  return Status;
}

/*********************************************************************
*
*       USBH_FT232_RegisterNotification
*
*  Function description
*    This function is deprecated, please use function USBH_FT232_AddNotification!
*    Sets a callback in order to be notified when a device is added or removed.
*
*  Parameters
*    pfNotification  : Pointer to a function the stack should call when a device is connected or disconnected.
*    pContext        : Pointer to a user context that is passed to the callback function.
*
*  Additional information
*    This function is deprecated, please use function USBH_FT232_AddNotification.
*/
void USBH_FT232_RegisterNotification(USBH_NOTIFICATION_FUNC * pfNotification, void * pContext) {
  static USBH_NOTIFICATION_HOOK _Hook;
  (void)USBH_FT232_AddNotification(&_Hook, pfNotification, pContext);
}

/*********************************************************************
*
*       USBH_FT232_AddNotification
*
*  Function description
*    Adds a callback in order to be notified when a device is added or removed.
*
*  Parameters
*    pHook           : Pointer to a user provided USBH_NOTIFICATION_HOOK variable.
*    pfNotification  : Pointer to a function the stack should call when a device is connected or disconnected.
*    pContext        : Pointer to a user context that is passed to the callback function.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_FT232_AddNotification(USBH_NOTIFICATION_HOOK * pHook, USBH_NOTIFICATION_FUNC * pfNotification, void * pContext) {
  return USBH__AddNotification(pHook, pfNotification, pContext, &USBH_FT232_Global.pFirstNotiHook, NULL);
}

/*********************************************************************
*
*       USBH_FT232_RemoveNotification
*
*  Function description
*    Removes a callback added via USBH_FT232_AddNotification.
*
*  Parameters
*    pHook          : Pointer to a user provided USBH_NOTIFICATION_HOOK variable.
*
*  Return value
*    USBH_STATUS_SUCCESS on success or error code on failure.
*/
USBH_STATUS USBH_FT232_RemoveNotification(const USBH_NOTIFICATION_HOOK * pHook) {
  return USBH__RemoveNotification(pHook, &USBH_FT232_Global.pFirstNotiHook);
}

/*********************************************************************
*
*       USBH_FT232_AddCustomDeviceMask
*
*  Function description
*    This function allows the FT232 module to receive notifications
*    about devices which do not present themselves with FTDI's
*    vendor ID (0x0403).
*
*  Parameters
*    pVendorIds   : Array of vendor IDs.
*    pProductIds  : Array of product IDs.
*    NumIds       : Number of elements in both arrays, each index
*                   in both arrays is used as a pair to create a filter.
*
*  Return value
*    == USBH_STATUS_SUCCESS: Success.
*    == USBH_STATUS_ERROR:   Notification could not be registered.
*/
USBH_STATUS USBH_FT232_AddCustomDeviceMask(const U16 * pVendorIds, const U16 * pProductIds, U16 NumIds) {
  USBH_INTERFACE_MASK InterfaceMask;
  USBH_STATUS         Status;

  //
  // Remove old notification if one was already registered.
  //
  (void)USBH_BULK_RemoveNotification(&_FT232_Hook_Custom);
  //
  // Add new user notification.
  //
  InterfaceMask.Mask        = USBH_INFO_MASK_VID_ARRAY | USBH_INFO_MASK_PID_ARRAY;
  InterfaceMask.pVendorIds  = pVendorIds;
  InterfaceMask.pProductIds = pProductIds;
  InterfaceMask.NumIds      = NumIds;
  Status = USBH_BULK_AddNotification(&_FT232_Hook_Custom, _cbOnAddRemoveDevice, NULL, &InterfaceMask);
  if (Status != USBH_STATUS_SUCCESS) {
    USBH_WARN((USBH_MCAT_FT232, "USBH_FT232_AddCustomDeviceMask: USBH_BULK_AddNotification failed %s", USBH_GetStatusStr(Status)));
  }
  return Status;
}

/*********************************************************************
*
*       USBH_FT232_ResetDevice
*
*  Function description
*    Resets the FT232 device
*
*  Parameters
*    hDevice : Handle to the opened device.
*
*  Return value
*    == USBH_STATUS_SUCCESS: Successful.
*    != USBH_STATUS_SUCCESS: An error occurred.
*/
USBH_STATUS USBH_FT232_ResetDevice(USBH_FT232_HANDLE hDevice) {
  USBH_FT232_INST * pInst;
  USBH_STATUS       Status;
  U32               Len;

  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    if (pInst->IsOpened == 0) {
      return USBH_STATUS_NOT_OPENED;
    }
    if (pInst->Removed != 0) {
      return USBH_STATUS_DEVICE_REMOVED;
    }
    Len = 0;
    Status = USBH_BULK_SetupRequest(pInst->hBulkDevice, USB_REQTYPE_VENDOR | USB_DEVICE_RECIPIENT, FT232_REQUEST_RESET, 0, FT232_IFACE_ID, NULL, &Len, USBH_FT232_EP0_TIMEOUT);
    return Status;
  }
  return USBH_STATUS_INVALID_HANDLE;
}

/*********************************************************************
*
*       USBH_FT232_GetDeviceInfo
*
*  Function description
*    Retrieves the information about the FT232 device.
*
*  Parameters
*    hDevice    : Handle to the opened device.
*    pDevInfo   : [OUT] Pointer to a USBH_FT232_DEVICE_INFO structure
*                 to store information related to the device.
*
*  Return value
*    == USBH_STATUS_SUCCESS: Successful.
*    != USBH_STATUS_SUCCESS: An error occurred.
*/
USBH_STATUS USBH_FT232_GetDeviceInfo(USBH_FT232_HANDLE hDevice, USBH_FT232_DEVICE_INFO * pDevInfo) {
  USBH_FT232_INST       * pInst;
  USBH_INTERFACE_INFO   InterFaceInfo;
  USBH_STATUS           Status;

  Status = USBH_STATUS_INVALID_HANDLE;
  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    if (pInst->IsOpened == 0) {
      return USBH_STATUS_NOT_OPENED;
    }
    if (pInst->Removed != 0) {
      return USBH_STATUS_DEVICE_REMOVED;
    }
    USBH_ASSERT_PTR(pDevInfo);
    Status = USBH_GetInterfaceInfo(pInst->InterfaceID, &InterFaceInfo);
    if (Status == USBH_STATUS_SUCCESS) {
      pDevInfo->VendorId = InterFaceInfo.VendorId;
      pDevInfo->ProductId = InterFaceInfo.ProductId;
      pDevInfo->bcdDevice = InterFaceInfo.bcdDevice;
      pDevInfo->Speed = InterFaceInfo.Speed;
      pDevInfo->MaxPacketSize = pInst->BulkInMaxPacketSize;
    }
  }
  return Status;
}

/*********************************************************************
*
*       USBH_FT232_SetTimeouts
*
*  Function description
*    Sets up the timeouts the host waits until the
*    data transfer will be aborted for a specific FT232 device.
*
*  Parameters
*    hDevice       : Handle to the opened device.
*    ReadTimeout   : Read time-out given in ms.
*    WriteTimeout  : Write time-out given in ms.
*
*  Return value
*    == USBH_STATUS_SUCCESS: Successful.
*    != USBH_STATUS_SUCCESS: An error occurred.
*/
USBH_STATUS USBH_FT232_SetTimeouts(USBH_FT232_HANDLE hDevice, U32 ReadTimeout, U32 WriteTimeout) {
  USBH_FT232_INST * pInst;
  USBH_STATUS       Status;

  Status = USBH_STATUS_INVALID_HANDLE;
  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    if (pInst->Removed != 0) {
      Status = USBH_STATUS_DEVICE_REMOVED;
    } else {
      pInst->ReadTimeOut = ReadTimeout;
      pInst->WriteTimeOut = WriteTimeout;
      Status = USBH_STATUS_SUCCESS;
    }
  }
  return Status;
}

/*********************************************************************
*
*       USBH_FT232_AllowShortRead
*
*  Function description
*    The configuration function allows to let the read function to
*    return as soon as data are available.
*
*  Parameters
*    hDevice        : Handle to the opened device.
*    AllowShortRead : Define whether short read mode shall be used or not.
*                     + 1 - Allow short read.
*                     + 0 - Short read mode disabled.
*
*  Return value
*    == USBH_STATUS_SUCCESS: Successful.
*    != USBH_STATUS_SUCCESS: An error occurred.
*
*  Additional information
*    USBH_FT232_AllowShortRead() sets the USBH_FT232_Read() into
*    a special mode - short read mode. When this mode is enabled,
*    the function returns as soon as any data has been read from
*    the device. This allows the application to read data where
*    the number of bytes to read is undefined. To disable this mode,
*    AllowShortRead should be set to 0.
*/
USBH_STATUS USBH_FT232_AllowShortRead(USBH_FT232_HANDLE hDevice, U8 AllowShortRead) {
  USBH_FT232_INST * pInst;

  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    if (pInst->Removed != 0) {
      return USBH_STATUS_DEVICE_REMOVED;
    } else {
      pInst->AllowShortRead = AllowShortRead;
      return USBH_STATUS_SUCCESS;
    }
  }
  return USBH_STATUS_INVALID_HANDLE;
}

/*********************************************************************
*
*       USBH_FT232_SetBaudRate
*
*  Function description
*    Sets the baud rate for the opened device.
*
*  Parameters
*    hDevice  : Handle to the opened device.
*    BaudRate : Baudrate to set.
*
*  Return value
*    == USBH_STATUS_SUCCESS: Successful.
*    != USBH_STATUS_SUCCESS: An error occurred.
*/
USBH_STATUS USBH_FT232_SetBaudRate(USBH_FT232_HANDLE hDevice, U32 BaudRate) {
  USBH_FT232_INST * pInst;
  U32               DivTmp;
  U32               SubDiv;
  U16               wValue;
  U16               wIndex;
  USBH_STATUS       Status;
  U32               Len;

  Status = USBH_STATUS_INVALID_HANDLE;
  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    if (pInst->IsOpened == 0) {
      return USBH_STATUS_NOT_OPENED;
    }
    if (pInst->Removed != 0) {
      return USBH_STATUS_DEVICE_REMOVED;
    }
    DivTmp = 3000000u * 8u / BaudRate;
    SubDiv = DivTmp & 0x7u;
    DivTmp >>= 3;
    wValue = (U16)DivTmp;
    wIndex = 0;
    switch (SubDiv) {
    case 1:
      wValue |= (1uL << 14) | (1uL << 15);
      break;
    case 2:
      wValue |= (1uL << 15);
      break;
    case 3:
      wIndex |= 1u;
      break;
    case 4:
      wValue |= (1uL << 14);
      break;
    case 5:
      wIndex |= 1u;
      wValue |= (1uL << 14);
      break;
    case 6:
      wIndex |= 1u;
      wValue |= (1uL << 15);
      break;
    case 7:
      wIndex |= 1u;
      wValue |= (1uL << 15) | (1uL << 14);
      break;
    default:
      // Do nothing.
      break;
    }
    Len = 0;
    Status = USBH_BULK_SetupRequest(pInst->hBulkDevice, USB_REQTYPE_VENDOR | USB_DEVICE_RECIPIENT, FT232_REQUEST_SETBAUDRATE, wValue, wIndex, NULL, &Len, USBH_FT232_EP0_TIMEOUT);
  }
  return Status;
}

/*********************************************************************
*
*       USBH_FT232_SetDataCharacteristics
*
*  Function description
*    Setups the serial communication with the given characteristics.
*
*  Parameters
*    hDevice  : Handle to the opened device.
*    Length   : Number of bits per word.
*               Must be either USBH_FT232_BITS_8 or USBH_FT232_BITS_7.
*    StopBits : Number of stop bits.
*               Must be USBH_FT232_STOP_BITS_1 or USBH_FT232_STOP_BITS_2.
*    Parity   : Parity - must be one of the following values:
*               + USBH_FT232_PARITY_NONE
*               + USBH_FT232_PARITY_ODD
*               + USBH_FT232_PARITY_EVEN
*               + USBH_FT232_PARITY_MARK
*               + USBH_FT232_PARITY_SPACE
*
*  Return value
*    == USBH_STATUS_SUCCESS: Successful.
*    != USBH_STATUS_SUCCESS: An error occurred.
*/
USBH_STATUS USBH_FT232_SetDataCharacteristics(USBH_FT232_HANDLE hDevice, U8  Length, U8 StopBits,  U8 Parity) {
  USBH_FT232_INST * pInst;
  U16               wValue;
  USBH_STATUS       Status;
  U32               Len;

  Status = USBH_STATUS_INVALID_HANDLE;
  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    if (pInst->IsOpened == 0) {
      return USBH_STATUS_NOT_OPENED;
    }
    if (pInst->Removed != 0) {
      return USBH_STATUS_DEVICE_REMOVED;
    }
    USBH_ASSERT(((Length == 7) || (Length == 8)));
    USBH_ASSERT((StopBits <= 1));
    USBH_ASSERT((Parity <= 4));

    Length   &= 0x0Fu;
    StopBits &= 0x01u;
    Parity   &= 0x07u;
    pInst->DataCharateristic &= (1uL << FT232_POS_BREAK);
    pInst->DataCharateristic  = (U16)(Length | ((U16)Parity << FT232_POS_PARITY) | ((U16)StopBits << FT232_POS_STOP_BIT));
    wValue = pInst->DataCharateristic;
    Len = 0;
    Status = USBH_BULK_SetupRequest(pInst->hBulkDevice, USB_REQTYPE_VENDOR | USB_DEVICE_RECIPIENT, FT232_REQUEST_SETDATA, wValue, FT232_IFACE_ID, NULL, &Len, USBH_FT232_EP0_TIMEOUT);
  }
  return Status;
}

/*********************************************************************
*
*       USBH_FT232_SetFlowControl
*
*  Function description
*    This function sets the flow control for the device.
*
*  Parameters
*    hDevice     : Handle to the opened device.
*    FlowControl : Must be one of the following values:
*                  + USBH_FT232_FLOW_NONE
*                  + USBH_FT232_FLOW_RTS_CTS
*                  + USBH_FT232_FLOW_DTR_DSR
*                  + USBH_FT232_FLOW_XON_XOFF
*    XonChar     : Character used to signal Xon.
*                  Only used if flow control is FT_FLOW_XON_XOFF.
*    XoffChar    : Character used to signal Xoff.  Only used if
*                  flow control is FT_FLOW_XON_XOFF.
*
*  Return value
*    == USBH_STATUS_SUCCESS: Successful.
*    != USBH_STATUS_SUCCESS: An error occurred.
*/
USBH_STATUS USBH_FT232_SetFlowControl(USBH_FT232_HANDLE hDevice, U16 FlowControl, U8 XonChar, U8 XoffChar) {
  USBH_FT232_INST * pInst;
  U16               wValue;
  U16               wIndex;
  USBH_STATUS       Status;
  U32               Len;

  Status = USBH_STATUS_INVALID_HANDLE;
  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    if (pInst->IsOpened == 0) {
      return USBH_STATUS_NOT_OPENED;
    }
    if (pInst->Removed != 0) {
      return USBH_STATUS_DEVICE_REMOVED;
    }
    USBH_ASSERT((FlowControl == USBH_FT232_FLOW_NONE) || (FlowControl == USBH_FT232_FLOW_RTS_CTS) ||
      (FlowControl == USBH_FT232_FLOW_DTR_DSR) || (FlowControl == USBH_FT232_FLOW_XON_XOFF));
    wValue  = (XonChar)
            | ((U16)XoffChar << 8)
            ;
    wIndex  = (FT232_IFACE_ID & 0xFFu)
            | (FlowControl)
            ;
    Len = 0;
    Status = USBH_BULK_SetupRequest(pInst->hBulkDevice, USB_REQTYPE_VENDOR | USB_DEVICE_RECIPIENT, FT232_REQUEST_SETFLOWCTRL, wValue, wIndex, NULL, &Len, USBH_FT232_EP0_TIMEOUT);
  }
  return Status;
}

/*********************************************************************
*
*       USBH_FT232_SetDtr
*
*  Function description
*    Sets the Data Terminal Ready (DTR) control signal.
*
*  Parameters
*    hDevice  : Handle to the opened device.
*
*  Return value
*    == USBH_STATUS_SUCCESS: Successful.
*    != USBH_STATUS_SUCCESS: An error occurred.
*/
USBH_STATUS USBH_FT232_SetDtr(USBH_FT232_HANDLE hDevice) {
  USBH_FT232_INST * pInst;
  U16               wValue;
  USBH_STATUS       Status;
  U32               Len;

  Status = USBH_STATUS_INVALID_HANDLE;
  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    if (pInst->IsOpened == 0) {
      return USBH_STATUS_NOT_OPENED;
    }
    if (pInst->Removed != 0) {
      return USBH_STATUS_DEVICE_REMOVED;
    }
    wValue  = (1uL << FT232_DTR_BIT)
            | (1uL << FT232_DTR_ENABLE_BIT)
            ;
    Len = 0;
    Status = USBH_BULK_SetupRequest(pInst->hBulkDevice, USB_REQTYPE_VENDOR | USB_DEVICE_RECIPIENT, FT232_REQUEST_MODEMCTRL, wValue, FT232_IFACE_ID, NULL, &Len, USBH_FT232_EP0_TIMEOUT);
  }
  return Status;
}
/*********************************************************************
*
*       USBH_FT232_ClrDtr
*
*  Function description
*    Clears the Data Terminal Ready (DTR) control signal.
*
*  Parameters
*    hDevice  : Handle to the opened device.
*
*  Return value
*    == USBH_STATUS_SUCCESS: Successful.
*    != USBH_STATUS_SUCCESS: An error occurred.
*/
USBH_STATUS USBH_FT232_ClrDtr(USBH_FT232_HANDLE hDevice) {
  USBH_FT232_INST * pInst;
  U16               wValue;
  USBH_STATUS       Status;
  U32               Len;

  Status = USBH_STATUS_INVALID_HANDLE;
  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    if (pInst->IsOpened == 0) {
      return USBH_STATUS_NOT_OPENED;
    }
    if (pInst->Removed != 0) {
      return USBH_STATUS_DEVICE_REMOVED;
    }
    wValue  = (0uL << FT232_DTR_BIT)
            | (1uL << FT232_DTR_ENABLE_BIT)
            ;
    Len = 0;
    Status = USBH_BULK_SetupRequest(pInst->hBulkDevice, USB_REQTYPE_VENDOR | USB_DEVICE_RECIPIENT, FT232_REQUEST_MODEMCTRL, wValue, FT232_IFACE_ID, NULL, &Len, USBH_FT232_EP0_TIMEOUT);
  }
  return Status;
}

/*********************************************************************
*
*       USBH_FT232_SetRts
*
*  Function description
*    Sets the Request To Send (RTS) control signal.
*
*  Parameters
*    hDevice  : Handle to the opened device.
*
*  Return value
*    == USBH_STATUS_SUCCESS: Successful.
*    != USBH_STATUS_SUCCESS: An error occurred.
*/
USBH_STATUS USBH_FT232_SetRts(USBH_FT232_HANDLE hDevice) {
  USBH_FT232_INST * pInst;
  U16               wValue;
  USBH_STATUS       Status;
  U32               Len;

  Status = USBH_STATUS_INVALID_HANDLE;
  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    if (pInst->IsOpened == 0) {
      return USBH_STATUS_NOT_OPENED;
    }
    if (pInst->Removed != 0) {
      return USBH_STATUS_DEVICE_REMOVED;
    }
    wValue  = (1uL << FT232_RTS_BIT)
            | (1uL << FT232_RTS_ENABLE_BIT)
            ;
    Len = 0;
    Status = USBH_BULK_SetupRequest(pInst->hBulkDevice, USB_REQTYPE_VENDOR | USB_DEVICE_RECIPIENT, FT232_REQUEST_MODEMCTRL, wValue, FT232_IFACE_ID, NULL, &Len, USBH_FT232_EP0_TIMEOUT);
  }
  return Status;
}

/*********************************************************************
*
*       USBH_FT232_ClrRts
*
*  Function description
*    Clears the Request To Send (RTS) control signal.
*
*  Parameters
*    hDevice  : Handle to the opened device.
*
*  Return value
*    == USBH_STATUS_SUCCESS: Successful.
*    != USBH_STATUS_SUCCESS: An error occurred.
*/
USBH_STATUS USBH_FT232_ClrRts(USBH_FT232_HANDLE hDevice) {
  USBH_FT232_INST * pInst;
  U16               wValue;
  USBH_STATUS       Status;
  U32               Len;

  Status = USBH_STATUS_INVALID_HANDLE;
  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    if (pInst->IsOpened == 0) {
      return USBH_STATUS_NOT_OPENED;
    }
    if (pInst->Removed != 0) {
      return USBH_STATUS_DEVICE_REMOVED;
    }
    wValue  = (0uL << FT232_RTS_BIT)
            | (1uL << FT232_RTS_ENABLE_BIT)
            ;
    Len = 0;
    Status = USBH_BULK_SetupRequest(pInst->hBulkDevice, USB_REQTYPE_VENDOR | USB_DEVICE_RECIPIENT, FT232_REQUEST_MODEMCTRL, wValue, FT232_IFACE_ID, NULL, &Len, USBH_FT232_EP0_TIMEOUT);
  }
  return Status;
}

/*********************************************************************
*
*       USBH_FT232_GetModemStatus
*
*  Function description
*    Gets the modem status and line status from the device.
*
*  Parameters
*    hDevice      : Handle to the opened device.
*    pModemStatus : Pointer to a variable of type U32 which receives the modem
*                   status and line status from the device.
*
*  Return value
*    == USBH_STATUS_SUCCESS: Successful.
*    != USBH_STATUS_SUCCESS: An error occurred.
*
*  Additional information
*    The least significant byte of the pModemStatus value holds the modem status.
*    The line status is held in the second least significant byte of the pModemStatus value.
*    The modem status is bit-mapped as follows:
*    * Clear To Send       (CTS) = 0x10
*    * Data Set Ready      (DSR) = 0x20
*    * Ring Indicator      (RI)  = 0x40
*    * Data Carrier Detect (DCD) = 0x80
*    The line status is bit-mapped as follows:
*    * Overrun Error       (OE)  = 0x02
*    * Parity Error        (PE)  = 0x04
*    * Framing Error       (FE)  = 0x08
*    * Break Interrupt     (BI)  = 0x10
*    * TxHolding register empty  = 0x20
*    * TxEmpty                   = 0x40
*/
USBH_STATUS USBH_FT232_GetModemStatus(USBH_FT232_HANDLE hDevice, U32 * pModemStatus) {
  U16               ModemStatus;
  USBH_FT232_INST * pInst;
  USBH_STATUS       Status;
  U32               Len;

  Status = USBH_STATUS_INVALID_HANDLE;
  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    if (pInst->IsOpened == 0) {
      return USBH_STATUS_NOT_OPENED;
    }
    if (pInst->Removed != 0) {
      return USBH_STATUS_DEVICE_REMOVED;
    }
    Len = 2;
    Status = USBH_BULK_SetupRequest(pInst->hBulkDevice, USB_TO_HOST | USB_REQTYPE_VENDOR | USB_DEVICE_RECIPIENT, FT232_REQUEST_GETMODEMSTAT, 0, FT232_IFACE_ID, &ModemStatus, &Len, USBH_FT232_EP0_TIMEOUT);
    if (Status == USBH_STATUS_SUCCESS) {
      *pModemStatus = ModemStatus;
    }
  }
  return Status;
}

/*********************************************************************
*
*       USBH_FT232_SetChars
*
*  Function description
*    Sets the special characters for the device.
*
*  Parameters
*    hDevice           : Handle to the opened device.
*    EventChar         : Eventc character.
*    EventCharEnabled  : 0, if event character disabled, non-zero otherwise.
*    ErrorChar         : Error character.
*    ErrorCharEnabled  : 0, if error character disabled, non-zero otherwise.
*
*  Return value
*    == USBH_STATUS_SUCCESS: Successful.
*    != USBH_STATUS_SUCCESS: An error occurred.
*
*  Additional information
*    This function allows to insert special characters in the data
*    stream to represent events triggering or errors occurring.
*/
USBH_STATUS USBH_FT232_SetChars(USBH_FT232_HANDLE hDevice, U8 EventChar, U8 EventCharEnabled,  U8 ErrorChar, U8 ErrorCharEnabled) {
  U16               wValue;
  USBH_STATUS       Status;
  USBH_FT232_INST * pInst;
  U32               Len;

  Status = USBH_STATUS_INVALID_HANDLE;
  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    if (pInst->IsOpened == 0) {
      return USBH_STATUS_NOT_OPENED;
    }
    if (pInst->Removed != 0) {
      return USBH_STATUS_DEVICE_REMOVED;
    }
    wValue = 0;
    if (EventCharEnabled != 0u) {
      wValue  = (1uL << FT232_POS_EVENT_ENABLE)
              | EventChar;
    }
    Len = 0;
    Status = USBH_BULK_SetupRequest(pInst->hBulkDevice, USB_REQTYPE_VENDOR | USB_DEVICE_RECIPIENT, FT232_REQUEST_SETEVENTCHAR, wValue, FT232_IFACE_ID, NULL, &Len, USBH_FT232_EP0_TIMEOUT);
    if (Status == USBH_STATUS_SUCCESS) {
      wValue = 0;
      if (ErrorCharEnabled != 0u) {
        wValue = (1uL << FT232_POS_ERRORCHAR_ENABLE)
                | ErrorChar;
      }
      Len = 0;
      Status = USBH_BULK_SetupRequest(pInst->hBulkDevice, USB_REQTYPE_VENDOR | USB_DEVICE_RECIPIENT, FT232_REQUEST_SETERRORCHAR, wValue, FT232_IFACE_ID, NULL, &Len, USBH_FT232_EP0_TIMEOUT);
    }
  }
  return Status;
}

/*********************************************************************
*
*       USBH_FT232_Purge
*
*  Function description
*    Purges receive and transmit buffers in the device.
*
*  Parameters
*    hDevice : Handle to the opened device.
*    Mask    : Combination of USBH_FT232_PURGE_RX and USBH_FT232_FT_PURGE_TX.
*
*  Return value
*    == USBH_STATUS_SUCCESS: Successful.
*    != USBH_STATUS_SUCCESS: An error occurred.
*/
USBH_STATUS USBH_FT232_Purge(USBH_FT232_HANDLE hDevice, U32 Mask) {
  U16               wValue;
  USBH_STATUS       Status;
  USBH_FT232_INST * pInst;
  U32               Len;

  Status = USBH_STATUS_INVALID_HANDLE;
  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    if (pInst->IsOpened == 0) {
      return USBH_STATUS_NOT_OPENED;
    }
    if (pInst->Removed != 0) {
      return USBH_STATUS_DEVICE_REMOVED;
    }
    if ((Mask & (USBH_FT232_PURGE_RX | USBH_FT232_PURGE_TX)) == (USBH_FT232_PURGE_RX | USBH_FT232_PURGE_TX)) {
      wValue = 0;
    } else {
      wValue = (U8)Mask;
    }
    Len = 0;
    Status = USBH_BULK_SetupRequest(pInst->hBulkDevice, USB_REQTYPE_VENDOR | USB_DEVICE_RECIPIENT, FT232_REQUEST_RESET, wValue, FT232_IFACE_ID, NULL, &Len, USBH_FT232_EP0_TIMEOUT);
  }
  return Status;
}

/*********************************************************************
*
*       USBH_FT232_GetQueueStatus
*
*  Function description
*    Gets the number of bytes in the receive queue.
*
*  Parameters
*    hDevice  : Handle to the opened device.
*    pRxBytes : Pointer to a variable of type U32 which receives the number
*               of bytes in the receive queue.
*
*  Return value
*    == USBH_STATUS_SUCCESS: Successful.
*    != USBH_STATUS_SUCCESS: An error occurred.
*/
USBH_STATUS USBH_FT232_GetQueueStatus(USBH_FT232_HANDLE hDevice, U32 * pRxBytes) {
  USBH_FT232_INST * pInst;
  USBH_STATUS       Status;

  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    if (pInst->IsOpened == 0) {
      return USBH_STATUS_NOT_OPENED;
    }
    if (pInst->Removed != 0) {
      return USBH_STATUS_DEVICE_REMOVED;
    }
    Status = USBH_STATUS_SUCCESS;
    *pRxBytes = pInst->RxRingBuffer.NumBytesIn;
  } else {
    Status = USBH_STATUS_INVALID_HANDLE;
  }
  return Status;
}

/*********************************************************************
*
*       USBH_FT232_SetBreakOn
*
*  Function description
*    Sets the BREAK condition for the device.
*
*  Parameters
*    hDevice  : Handle to the opened device.
*
*  Return value
*    == USBH_STATUS_SUCCESS: Successful.
*    != USBH_STATUS_SUCCESS: An error occurred.
*/
USBH_STATUS USBH_FT232_SetBreakOn(USBH_FT232_HANDLE hDevice) {
  USBH_FT232_INST * pInst;
  U16               wValue;
  USBH_STATUS       Status;
  U32               Len;

  Status = USBH_STATUS_INVALID_HANDLE;
  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    if (pInst->IsOpened == 0) {
      return USBH_STATUS_NOT_OPENED;
    }
    if (pInst->Removed != 0) {
      return USBH_STATUS_DEVICE_REMOVED;
    }
    wValue = pInst->DataCharateristic | (U16)(1uL << FT232_POS_BREAK);
    Len = 0;
    Status = USBH_BULK_SetupRequest(pInst->hBulkDevice, USB_REQTYPE_VENDOR | USB_DEVICE_RECIPIENT, FT232_REQUEST_SETDATA, wValue, FT232_IFACE_ID, NULL, &Len, USBH_FT232_EP0_TIMEOUT);
  }
  return Status;
}

/*********************************************************************
*
*       USBH_FT232_SetBreakOff
*
*  Function description
*    Resets the BREAK condition for the device.
*
*  Parameters
*    hDevice  : Handle to the opened device.
*
*  Return value
*    == USBH_STATUS_SUCCESS: Successful.
*    != USBH_STATUS_SUCCESS: An error occurred.
*/
USBH_STATUS USBH_FT232_SetBreakOff(USBH_FT232_HANDLE hDevice) {
  USBH_FT232_INST * pInst;
  U16               wValue;
  USBH_STATUS       Status;
  U32               Len;

  Status = USBH_STATUS_INVALID_HANDLE;
  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    if (pInst->IsOpened == 0) {
      return USBH_STATUS_NOT_OPENED;
    }
    if (pInst->Removed != 0) {
      return USBH_STATUS_DEVICE_REMOVED;
    }
    wValue = pInst->DataCharateristic;
    Len = 0;
    Status = USBH_BULK_SetupRequest(pInst->hBulkDevice, USB_REQTYPE_VENDOR | USB_DEVICE_RECIPIENT, FT232_REQUEST_SETDATA, wValue, FT232_IFACE_ID, NULL, &Len, USBH_FT232_EP0_TIMEOUT);
  }
  return Status;
}

/*********************************************************************
*
*       USBH_FT232_SetLatencyTimer
*
*  Function description
*    The latency timer controls the timeout for the FTDI device to transfer
*    data from the FT232 interface to the USB interface.
*    The FTDI device transfers data from the FT232 to the USB interface
*    when it receives 62 bytes over FT232 (one full packet with 2 status bytes)
*    or when the latency timeout elapses.
*
*  Parameters
*    hDevice  : Handle to the opened device.
*    Latency  : Required value, in milliseconds, of latency timer.
*               Valid range is 2 - 255.
*
*  Return value
*    == USBH_STATUS_SUCCESS: Successful.
*    != USBH_STATUS_SUCCESS: An error occurred.
*
*  Additional information
*    In the FT8U232AM and FT8U245AM devices, the receive buffer timeout
*    that is used to flush remaining data from the receive buffer was
*    fixed at 16 ms. Therefore this function cannot be used with these
*    devices. In all other FTDI devices, this timeout is programmable
*    and can be set at 1 ms intervals between 2ms and 255 ms.
*    This allows the device to be better optimized for protocols
*    requiring faster response times from short data packets.
*/
USBH_STATUS USBH_FT232_SetLatencyTimer(USBH_FT232_HANDLE hDevice, U8 Latency) {
  USBH_FT232_INST * pInst;
  USBH_STATUS       Status;
  U32               Len;

  Status = USBH_STATUS_INVALID_HANDLE;
  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    if (pInst->IsOpened == 0) {
      return USBH_STATUS_NOT_OPENED;
    }
    if (pInst->Removed != 0) {
      return USBH_STATUS_DEVICE_REMOVED;
    }
    Len = 0;
    Status = USBH_BULK_SetupRequest(pInst->hBulkDevice, USB_REQTYPE_VENDOR | USB_DEVICE_RECIPIENT, FT232_REQUEST_SETLATTIMER, Latency, FT232_IFACE_ID, NULL, &Len, USBH_FT232_EP0_TIMEOUT);
  }
  return Status;
}

/*********************************************************************
*
*       USBH_FT232_GetLatencyTimer
*
*  Function description
*    Get the current value of the latency timer.
*
*  Parameters
*    hDevice  : Handle to the opened device.
*    pLatency : Pointer to a value which receives the device latency setting.
*
*  Return value
*    == USBH_STATUS_SUCCESS: Successful.
*    != USBH_STATUS_SUCCESS: An error occurred.
*
*  Additional information
*    Please refer to USBH_FT232_SetLatencyTimer() for more information
*    about the latency timer.
*/
USBH_STATUS USBH_FT232_GetLatencyTimer(USBH_FT232_HANDLE hDevice, U8 * pLatency) {
  USBH_FT232_INST * pInst;
  USBH_STATUS       Status;
  U32               Len;

  Status = USBH_STATUS_INVALID_HANDLE;
  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    if (pInst->IsOpened == 0) {
      return USBH_STATUS_NOT_OPENED;
    }
    if (pInst->Removed != 0) {
      return USBH_STATUS_DEVICE_REMOVED;
    }
    Len = 1;
    Status = USBH_BULK_SetupRequest(pInst->hBulkDevice, USB_TO_HOST | USB_REQTYPE_VENDOR | USB_DEVICE_RECIPIENT, FT232_REQUEST_GETLATTIMER, 0, 0, pLatency, &Len, USBH_FT232_EP0_TIMEOUT);
  }
  return Status;
}

/*********************************************************************
*
*       USBH_FT232_SetBitMode
*
*  Function description
*    Enables different chip modes.
*
*  Parameters
*    hDevice  : Handle to the opened device.
*    Mask     : Required value for bit mode mask. This sets up which bits
*               are inputs and outputs.
*               A bit value of 0 sets the corresponding pin to an input.
*               A bit value of 1 sets the corresponding pin to an output.
*               In the case of CBUS Bit Bang, the upper nibble of this value controls
*               which pins are inputs and outputs, while the lower nibble controls
*               which of the outputs are high and low.
*    Enable   : Mode value. Can be one of the following values:
*               * 0x00 = Reset
*               * 0x01 = Asynchronous Bit Bang
*               * 0x02 = MPSSE (FT2232, FT2232H, FT4232H and FT232H devices only)
*               * 0x04 = Synchronous Bit Bang (FT232R, FT245R, FT2232,
*                        FT2232H, FT4232H and FT232H devices only)
*               * 0x08 = MCU Host Bus Emulation Mode (FT2232, FT2232H,
*                        FT4232H and FT232H devices only)
*               * 0x10 = Fast Opto-Isolated Serial Mode (FT2232, FT2232H,
*                        FT4232H and FT232H devices only)
*               * 0x20 = CBUS Bit Bang Mode (FT232R and FT232H devices only)
*               * 0x40 = Single Channel Synchronous 245 FIFO Mode (FT2232H
*                        and FT232H devices only).
*
*  Return value
*    == USBH_STATUS_SUCCESS: Successful.
*    != USBH_STATUS_SUCCESS: An error occurred.
*
*  Additional information
*    For further information please refer to the HW-reference manuals
*    and application note on the FTDI website.
*/
USBH_STATUS USBH_FT232_SetBitMode(USBH_FT232_HANDLE hDevice, U8 Mask, U8 Enable) {
  USBH_FT232_INST * pInst;
  USBH_STATUS       Status;
  U32               Len;

  Status = USBH_STATUS_INVALID_HANDLE;
  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    if (pInst->IsOpened == 0) {
      return USBH_STATUS_NOT_OPENED;
    }
    if (pInst->Removed != 0) {
      return USBH_STATUS_DEVICE_REMOVED;
    }
    Len = 0;
    Status = USBH_BULK_SetupRequest(pInst->hBulkDevice, USB_REQTYPE_VENDOR | USB_DEVICE_RECIPIENT, FT232_REQUEST_SETBITMODE, (U16)(Mask | ((U16)Enable << 8)), 0, NULL, &Len, USBH_FT232_EP0_TIMEOUT);
  }
  return Status;
}

/*********************************************************************
*
*       USBH_FT232_GetBitMode
*
*  Function description
*    Returns the current values on the data bus pins.
*    This function does NOT return the configured mode.
*
*  Parameters
*    hDevice  : Handle to the opened device.
*    pMode    : Pointer to a U8 variable to store the current value.
*
*  Return value
*    == USBH_STATUS_SUCCESS: Successful.
*    != USBH_STATUS_SUCCESS: An error occurred.
*/
USBH_STATUS USBH_FT232_GetBitMode(USBH_FT232_HANDLE hDevice, U8 * pMode) {
  USBH_FT232_INST * pInst;
  USBH_STATUS       Status;
  U32               Len;

  Status = USBH_STATUS_INVALID_HANDLE;
  pInst = _h2p(hDevice);
  if (pInst != NULL) {
    if (pInst->IsOpened == 0) {
      return USBH_STATUS_NOT_OPENED;
    }
    if (pInst->Removed != 0) {
      return USBH_STATUS_DEVICE_REMOVED;
    }
    Len = 1;
    Status = USBH_BULK_SetupRequest(pInst->hBulkDevice, USB_TO_HOST | USB_REQTYPE_VENDOR | USB_DEVICE_RECIPIENT, FT232_REQUEST_GETBITMODE, 0, 0, pMode, &Len, USBH_FT232_EP0_TIMEOUT);
  }
  return Status;
}

/*********************************************************************
*
*       USBH_FT232_ConfigureDefaultTimeout
*
*  Function description
*    Sets the default read and write timeout that shall be used when
*    a new device is connected.
*
*  Parameters
*    ReadTimeout    : Default read timeout given in ms.
*    WriteTimeout   : Default write timeout given in ms.
*
*  Additional information
*    The function shall be called after USBH_FT232_Init() has been
*    called, otherwise the behavior is undefined.
*/
void USBH_FT232_ConfigureDefaultTimeout(U32 ReadTimeout, U32 WriteTimeout) {
  USBH_FT232_Global.DefaultReadTimeOut = ReadTimeout;
  USBH_FT232_Global.DefaultWriteTimeOut = WriteTimeout;
}

/*********************************************************************
*
*       USBH_FT232_GetInterfaceHandle
*
*  Function description
*    Return the handle to the (open) USB interface. Can be used to
*    call USBH core functions like USBH_GetStringDescriptor().
*
*  Parameters
*    hDevice      : Handle to an open device returned by USBH_FT232_Open().
*
*  Return value
*    Handle to an open interface.
*/
USBH_INTERFACE_HANDLE USBH_FT232_GetInterfaceHandle(USBH_FT232_HANDLE hDevice) {
  USBH_FT232_INST      * pInst;

  pInst = _h2p(hDevice);
  USBH_ASSERT_PTR(pInst);
  return USBH_BULK_GetInterfaceHandle(pInst->hBulkDevice);
}

/*************************** End of file ****************************/
