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
File        : USBH_InterfaceList.c
Purpose     : USB Host implementation
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/

#include "USBH_Int.h"

/*********************************************************************
*
*       Defines, fixed
*
**********************************************************************
*/

#define MAX_NUM_INTERFACES    28u

/*********************************************************************
*
*       Types
*
**********************************************************************
*/
// The interface list object
typedef struct _USBH_INTERFACE_LIST {
#if (USBH_DEBUG > 1)
  U32                Magic;
#endif
  unsigned int       InterfaceCount;                    // Number of entries in InterfaceIDs[]
  USBH_INTERFACE_ID  InterfaceIDs[MAX_NUM_INTERFACES];  // Array of interface IDs.
} INTERFACE_LIST;

//lint -esym(9045, struct _USBH_INTERFACE_LIST) N:100  lint doesn't understand that this typedef is local

/*********************************************************************
*
*       USBH_CreateInterfaceList
*
*  Function description
*    Generates a list of available interfaces matching a given criteria.
*
*  Parameters
*    pInterfaceMask:     Pointer to a caller provided structure, that
*                        allows to select interfaces to be included in the list.
*                        If this pointer is NULL all available interfaces are returned.
*    pInterfaceCount:    Pointer to a variable that receives the number of interfaces
*                        in the list created.
*
*  Return value
*    On success it returns a handle to the interface list.
*    In case of an error it returns NULL.
*
*  Additional information
*    The generated interface list is stored in the emUSB-Host and must be deleted by a
*    call to USBH_DestroyInterfaceList(). The list contains a snapshot of interfaces
*    available at the point of time where the function is called. This enables the application
*    to have a fixed relation between the index and a USB interface in a list. The list
*    is not updated if a device is removed or connected. A new list must be created to
*    capture the current available interfaces. Hub devices are not added to the list!
*/
USBH_INTERFACE_LIST_HANDLE USBH_CreateInterfaceList(const USBH_INTERFACE_MASK * pInterfaceMask, unsigned int * pInterfaceCount) {
  unsigned               NumHC;
  unsigned               i;
  INTERFACE_LIST       * pList;
  USBH_DLIST           * pInterfaceEntry;
  USBH_DLIST           * pDevEntry;
  USBH_HOST_CONTROLLER * pHostController;
  USB_DEVICE           * pUsbDev;
  USB_INTERFACE        * pInterface;
  USB_DEV_STATE          MinState = DEV_STATE_WORKING;
  USBH_BOOL              IncludeHubInterfaces = FALSE;

  USBH_LOG((USBH_MCAT_INTF_API, "USBH_CreateInterfaceList"));
  if (pInterfaceMask != NULL) {
    if ((pInterfaceMask->Mask & USBH_INFO_MASK_REMOVED) != 0u) {
      MinState = DEV_STATE_REMOVED;
    }
    if ((pInterfaceMask->Mask & USBH_INFO_MASK_HUBS) != 0u) {
      IncludeHubInterfaces = TRUE;
    }
  }
  pList = (INTERFACE_LIST *)USBH_TRY_MALLOC_ZEROED(sizeof(INTERFACE_LIST));
  if (NULL == pList) {
    USBH_WARN((USBH_MCAT_INTF_API, "USBH_CreateInterfaceList: No memory"));
    return NULL;
  }
  USBH_IFDBG(pList->Magic = INTERFACE_LIST_MAGIC);
  NumHC = USBH_Global.HostControllerCount;
  for (i = 0; i < NumHC; i++) {       // Search in all host controller
    pHostController = USBH_Global.aHostController[i];
    USBH_ASSERT_MAGIC(pHostController, USBH_HOST_CONTROLLER);
    USBH_LockDeviceList(pHostController);
    pDevEntry = USBH_DLIST_GetNext(&pHostController->DeviceList);
    while (pDevEntry != &pHostController->DeviceList) {                       // Search in all devices
      pUsbDev       = GET_USB_DEVICE_FROM_ENTRY(pDevEntry);
      USBH_ASSERT_MAGIC(pUsbDev, USB_DEVICE);
      if (pUsbDev->RefCount != 0 && pUsbDev->State >= MinState) {
        pInterfaceEntry = USBH_DLIST_GetNext(&pUsbDev->UsbInterfaceList);         // For each interface
        while (pInterfaceEntry != &pUsbDev->UsbInterfaceList) {
          pInterface = GET_USB_INTERFACE_FROM_ENTRY(pInterfaceEntry);
          USBH_ASSERT_MAGIC(pInterface, USB_INTERFACE);
          if (USBH_STATUS_SUCCESS == USBH_CompareUsbInterface(pInterface, pInterfaceMask, IncludeHubInterfaces)) {
            if (pList->InterfaceCount < MAX_NUM_INTERFACES) {
              pList->InterfaceIDs[pList->InterfaceCount++] = pInterface->InterfaceId;
            }
          }
          pInterfaceEntry = USBH_DLIST_GetNext(pInterfaceEntry);
        }
      }
      pDevEntry = USBH_DLIST_GetNext(pDevEntry);
    }
    USBH_UnlockDeviceList(pHostController);
  }
  *pInterfaceCount = pList->InterfaceCount;
  USBH_LOG((USBH_MCAT_INTF_API, "USBH_CreateInterfaceList returned interfaces: %u!",*pInterfaceCount));
  return pList;
}

/*********************************************************************
*
*       USBH_DestroyInterfaceList
*
*  Function description
*    Destroy a device list created by USBH_CreateInterfaceList() and free the related resources.
*
*  Parameters
*    hInterfaceList:     Valid handle to a interface list, returned by USBH_CreateInterfaceList().
*/
void USBH_DestroyInterfaceList(USBH_INTERFACE_LIST_HANDLE hInterfaceList) {
  INTERFACE_LIST * pList;

  pList = hInterfaceList;
  USBH_ASSERT_MAGIC(pList, INTERFACE_LIST);
  USBH_IFDBG(pList->Magic = 0);
  USBH_FREE(pList);
}

/*********************************************************************
*
*       USBH_GetInterfaceId
*
*  Function description
*    Returns the interface id for a specified interface.
*
*  Parameters
*    hInterfaceList:     Valid handle to a interface list, returned by USBH_CreateInterfaceList().
*    Index:              Specifies the zero based index for an interface in the list.
*
*  Return value
*    On success the interface Id for the interface specified by Index is returned. If the
*    interface index does not exist the function returns 0.
*
*  Additional information
*    The interface ID identifies a USB interface as long as the device is connected to the
*    host. If the device is removed and re-connected a new interface ID is assigned. The
*    interface ID is even valid if the interface list is deleted. The function can return an
*    interface ID even if the device is removed between the call to the function
*    USBH_CreateInterfaceList() and the call to this function. If this is the case, the
*    function USBH_OpenInterface() fails.
*/
USBH_INTERFACE_ID USBH_GetInterfaceId(USBH_INTERFACE_LIST_HANDLE hInterfaceList, unsigned int Index) {
  INTERFACE_LIST * pList;

  USBH_ASSERT(NULL != hInterfaceList);
  pList = hInterfaceList;
  USBH_ASSERT_MAGIC(pList, INTERFACE_LIST);
  if (Index >= pList->InterfaceCount) {
    USBH_WARN((USBH_MCAT_INTF_API, "USBH_GetInterfaceId: Index does not exist!"));
    return 0;
  }
  return pList->InterfaceIDs[Index];
}

/*************************** End of file ****************************/
