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
File        : USBH_HC.c
Purpose     : USB host implementation
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#include "USBH_Int.h"
#include "USBH_Util.h"

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _HC_ServicePorts
*
*  Function description
*    Check and service all ports (RootHub and external HUB)
*
*  Parameters
*    pContext : Pointer to a host controller object.
*/
static void _HC_ServicePorts(void * pContext) {
  USBH_HOST_CONTROLLER * pHostController;

  pHostController = USBH_CTX2PTR(USBH_HOST_CONTROLLER, pContext);
  USBH_ASSERT_MAGIC(pHostController, USBH_HOST_CONTROLLER);
  if (pHostController->State < HC_WORKING) {
    return;
  }
  USBH_ROOTHUB_ServicePorts(&pHostController->RootHub);
  if (NULL == pHostController->pActivePortReset &&
      USBH_Global.pExtHubApi != NULL) {
    USBH_Global.pExtHubApi->pfServiceAll(pHostController);
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
*       USBH_HcIncRef
*
*  Function description
*    Increment reference count for the USBH_HOST_CONTROLLER object.
*/
void USBH_HcIncRef(USBH_HOST_CONTROLLER *pHostController
#if USBH_DEBUG > 1
                    , const char *sFile, unsigned Line
#endif
                    ) {
  USBH_OS_Lock(USBH_MUTEX_DEVICE);
  pHostController->RefCount++;
  USBH_OS_Unlock(USBH_MUTEX_DEVICE);
  USBH_LOG((USBH_MCAT_HC_REF, "USBH_HC_INC_REF RefCount is %d %s(%d)", pHostController->RefCount, sFile, Line));
}

/*********************************************************************
*
*       USBH_HcDecRef
*
*  Function description
*    Decrement reference count for the USBH_HOST_CONTROLLER object.
*/
void USBH_HcDecRef(USBH_HOST_CONTROLLER *pHostController
#if USBH_DEBUG > 1
                    , const char *sFile, unsigned Line
#endif
                    ) {
  USBH_OS_Lock(USBH_MUTEX_DEVICE);
  pHostController->RefCount--;
  USBH_OS_Unlock(USBH_MUTEX_DEVICE);
  if (pHostController->RefCount >= 0) {
    USBH_LOG((USBH_MCAT_HC_REF, "USBH_HC_DEC_REF RefCount is %d %s(%d)", pHostController->RefCount, sFile, Line));
  }
  if (pHostController->RefCount <  0) {
    USBH_PANIC("USBH_HC_DEC_REF RefCount less than 0");
  }
}

/*********************************************************************
*
*       USBH_AddHostController
*
*  Function description
*    Add a host controller to the USB stack and initialize the driver.
*
*  Parameters
*    pDriver:         Pointer to the driver API structure.
*    pPrvData:        Pointer to the drivers private data structure.
*    MaxUsbAddress:   Maximum USB address that can be handled by the driver.
*                     A value of 0 means, that the driver will select a USB address for
*                     each new device.
*    pIndex:          [OUT] Index of the host controller within the USB stack.
*
*  Return value
*    Pointer to the host controller structure or NULL on error.
*/
USBH_HOST_CONTROLLER * USBH_AddHostController(const USBH_HOST_DRIVER * pDriver, void *pPrvData, U8 MaxUsbAddress, U32 *pIndex) {
  void                 * pContext;
  USBH_HOST_CONTROLLER * pHost;
  USBH_STATUS            Status;

  USBH_LOG((USBH_MCAT_HC, "USBH_AddHostController!"));
  *pIndex = 0xFFFFFFFFu;
  if (USBH_Global.HostControllerCount >= USBH_MAX_NUM_HOST_CONTROLLERS) {
    USBH_PANIC("Too many host controllers, increase USBH_MAX_NUM_HOST_CONTROLLERS");
  }
  pHost = (USBH_HOST_CONTROLLER *)USBH_MALLOC_ZEROED(sizeof(USBH_HOST_CONTROLLER));
  USBH_IFDBG(pHost->Magic = USBH_HOST_CONTROLLER_MAGIC);
  // Set the host controller driver function interface
  pHost->pDriver  = pDriver;
  pHost->pPrvData = (USBH_HC_HANDLE)pPrvData;
  USBH_DLIST_Init(&pHost->DeviceList);
  USBH_ROOTHUB_Init(pHost);
  pHost->MaxAddress = MaxUsbAddress;
  pHost->NextFreeAddress = 1;
  pContext = &pHost->RootHub;
  Status  = pDriver->pfHostInit(pHost->pPrvData, USBH_ROOTHUB_OnNotification, pContext); // Initialize the host and enable all interrupts
  if (Status != USBH_STATUS_SUCCESS) {
    USBH_WARN((USBH_MCAT_HC, "USBH_AddHostController: pfHostInit %s", USBH_GetStatusStr(Status)));
    USBH_PANIC("USB Controller can't be initialized");
    USBH_ROOTHUB_Release(&pHost->RootHub);
    // Delete the Host
    USBH_FREE(pHost);
    return NULL;
  }
  pHost->Index = USBH_Global.HostControllerCount;
  *pIndex      = USBH_Global.HostControllerCount;
  USBH_Global.aHostController[USBH_Global.HostControllerCount++] = pHost;
  USBH_InitTimer(&pHost->PortServiceTimer, _HC_ServicePorts, pHost);
#if USBH_URB_QUEUE_SIZE != 0u
  USBH_InitTimer(&pHost->QueueRetryTimer, USBH_RetryRequestTmr, pHost);
#endif
  return pHost;
}

/*********************************************************************
*
*       USBH_RemoveHostController
*
*  Function description
*/
void USBH_RemoveHostController(USBH_HOST_CONTROLLER * pHostController) {
  const USBH_HOST_DRIVER      * pDriver;
  USBH_DLIST                  * pList;
  USB_DEVICE                  * pUsbDevice;
  unsigned                      NumPorts;
  unsigned                      i;

  USBH_LOG((USBH_MCAT_HC, "USBH_RemoveHostController!"));
  USBH_ASSERT_MAGIC(pHostController, USBH_HOST_CONTROLLER);
  pDriver = pHostController->pDriver;
  pHostController->State = HC_REMOVED;
  (void)pDriver->pfSetHcState(pHostController->pPrvData, USBH_HOST_RESET);     // Stop the host controller
  USBH_LockDeviceList(pHostController);
  pList = USBH_DLIST_GetNext(&pHostController->DeviceList);                    // Mark all devices as removed
  while (pList != &pHostController->DeviceList) {
    pUsbDevice = GET_USB_DEVICE_FROM_ENTRY(pList);
    USBH_ASSERT_MAGIC(pUsbDevice, USB_DEVICE);
    pList = USBH_DLIST_GetNext(pList);
    USBH_MarkDeviceAsRemoved(pUsbDevice);
  }
  USBH_UnlockDeviceList(pHostController);
  for (i = 0; i < 4u; i++) {
    if (pHostController->RootEndpoints[i] != NULL) {
      (void)USBH_AbortEndpoint(pHostController, pHostController->RootEndpoints[i]);
    }
  }
  //
  // Turn off all ports.
  //
  NumPorts = pDriver->pfGetPortCount(pHostController->pPrvData);
  for (i = 1; i <= NumPorts; i++) { // Port index is 1-based.
    //
    // Call the user callback if available.
    //
    if (USBH_Global.pfOnSetPortPower != NULL) {
      USBH_Global.pfOnSetPortPower(pHostController->Index, i, 0);
    }
    pDriver->pfSetPortPower(pHostController->pPrvData, i, 0);
    //
    // If the hardware is not able to switch the power, the port must be at least disabled.
    //
    pDriver->pfDisablePort(pHostController->pPrvData, i);
  }
  //
  // Wait for RootEP activities to be finished.
  //
  while (pHostController->ActivePortReset != 0u) {
    USBH_OS_SignalNetEvent();
    USBH_OS_Delay(10);
  }
  //
  // Release root EPs
  //
  for (i = 0; i < 4u; i++) {
    if (pHostController->RootEndpoints[i] != NULL) {
      USBH_HC_INC_REF(pHostController);
      pDriver->pfReleaseEndpoint(pHostController->RootEndpoints[i], USBH_DefaultReleaseEpCompletion, pHostController);
    }
  }
  //
  // Wait for all operations on the host controller to compete.
  //
  while (pHostController->RefCount > 0) {
    USBH_OS_SignalNetEvent();
    USBH_OS_Delay(10);
  }
  USBH_ROOTHUB_Release(&pHostController->RootHub);                         // Release the root hub
  USBH_ReleaseTimer(&pHostController->PortServiceTimer);
#if USBH_URB_QUEUE_SIZE != 0u
  USBH_ReleaseTimer(&pHostController->QueueRetryTimer);
#endif
  (void)pHostController->pDriver->pfHostExit(pHostController->pPrvData);   // Inform the HC driver that everything is released
}

/*********************************************************************
*
*       USBH_AddUsbDevice
*
*  Function description
*    Adds a device object into the list of devices managed by
*    the host controller which is responsible for the port
*    through which the device was connected.
*
*  Parameters
*    pDevice : Pointer to the USB device object.
*/
void USBH_AddUsbDevice(USB_DEVICE * pDevice) {
  USBH_HOST_CONTROLLER * pHost;
  USBH_ASSERT_MAGIC(pDevice, USB_DEVICE);
  //
  // Set the port pointer to the device, now hub notify and root hub notify function can detect a device
  // on a port and now it is allowed to call UbdUdevMarkParentAndChildDevicesAsRemoved!!!. State machines
  // checks the port state at the entry point and delete self a not complete enumerated device!
  //
  pDevice->State                = DEV_STATE_WORKING;
  pDevice->pParentPort->pDevice = pDevice;
  pHost                         = pDevice->pHostController;
  USBH_ASSERT_MAGIC(pHost, USBH_HOST_CONTROLLER);
  USBH_HC_INC_REF(pHost);
  USBH_OS_Lock(USBH_MUTEX_DEVICE);
  USBH_DLIST_InsertTail(&pHost->DeviceList, &pDevice->ListEntry);
  USBH_OS_Unlock(USBH_MUTEX_DEVICE);
  USBH_LOG((USBH_MCAT_DEVICE,  "Added Dev: USB addr: %u Id:%u speed: %s parent port: %u",
                               pDevice->UsbAddress,
                               pDevice->DeviceId,
                               USBH_PortSpeed2Str(pDevice->DeviceSpeed),
                               pDevice->pParentPort->HubPortNumber));
}

/*********************************************************************
*
*       USBH_LockDeviceList
*
*  Function description
*    Lock loop through all devices of a host controller.
*/
void USBH_LockDeviceList(USBH_HOST_CONTROLLER *pHost) {
  USBH_OS_Lock(USBH_MUTEX_DEVICE);
  pHost->DeviceListLckCnt++;
  USBH_OS_Unlock(USBH_MUTEX_DEVICE);
}

/*********************************************************************
*
*       USBH_UnlockDeviceList
*
*  Function description
*    Unlock after loop through all devices.
*/
void USBH_UnlockDeviceList(USBH_HOST_CONTROLLER *pHost) {
  USBH_OS_Lock(USBH_MUTEX_DEVICE);
  USBH_ASSERT(pHost->DeviceListLckCnt > 0);
  pHost->DeviceListLckCnt--;
  USBH_OS_Unlock(USBH_MUTEX_DEVICE);
}

/*********************************************************************
*
*       USBH_CleanupDeviceList
*
*  Function description
*    Remove detached devices from the list.
*/
void USBH_CleanupDeviceList(void) {
  USB_DEVICE           * pDevice;
  USB_DEVICE           * pUSBDev;
  USBH_DLIST           * pDevEntry;
  USBH_HOST_CONTROLLER * pHost;
  unsigned               NumHC;
  unsigned               i;

  pDevice   = NULL;
  NumHC = USBH_Global.HostControllerCount;
  for (i = 0; i < NumHC; i++) {       // Search in all host controller
    pHost = USBH_Global.aHostController[i];
    USBH_ASSERT_MAGIC(pHost, USBH_HOST_CONTROLLER);
    pDevEntry = USBH_DLIST_GetNext(&pHost->DeviceList);
    while (pDevEntry != &pHost->DeviceList) { // Search in all devices
      pUSBDev = GET_USB_DEVICE_FROM_ENTRY(pDevEntry);
      USBH_ASSERT_MAGIC(pUSBDev, USB_DEVICE);
      if (pUSBDev->State == DEV_STATE_REMOVED && pUSBDev->RefCount == 0) {
        pDevice = pUSBDev;
      }
      pDevEntry = USBH_DLIST_GetNext(pDevEntry);
    }
  }
  if (pDevice != NULL) {
    pHost = pDevice->pHostController;
    USBH_OS_Lock(USBH_MUTEX_DEVICE);
    if (pHost->DeviceListLckCnt == 0u) {
      //
      // Remove device from linked list.
      //
      USBH_DLIST_RemoveEntry(&pDevice->ListEntry);
      USBH_OS_Unlock(USBH_MUTEX_DEVICE);
      //
      // Delete device object.
      //
      USBH_DeleteDevice(pDevice);
      USBH_HC_DEC_REF(pHost);
    } else {
      USBH_OS_Unlock(USBH_MUTEX_DEVICE);
    }
  }
}

/*********************************************************************
*
*       USBH_DefaultReleaseEpCompletion
*
*  Function description
*    This callback is called when an endpoint is released.
*
*  Parameters
*    pContext : Context passed to this completion callback.
*/
void USBH_DefaultReleaseEpCompletion(void * pContext) {
  USBH_HOST_CONTROLLER * pHostController = USBH_CTX2PTR(USBH_HOST_CONTROLLER, pContext);
  USBH_ASSERT_MAGIC(pHostController, USBH_HOST_CONTROLLER);
  USBH_HC_DEC_REF(pHostController);
  USBH_MEM_ScheduleReo();         //lint !e522  N:100
}

/*********************************************************************
*
*       USBH_StartHostController
*
*  Function description
*    Start up the host controller.
*
*  Parameters
*    pHostController : Handle for the host controller object.
*/
void USBH_StartHostController(USBH_HOST_CONTROLLER *pHostController) {
  USBH_STATUS                   Status;
  const USBH_HOST_DRIVER      * pDriver;
  USBH_IOCTL_PARA               IoctlPara;
  int                           i;
  static const USBH_SPEED       Speed[] = { USBH_LOW_SPEED, USBH_FULL_SPEED, USBH_HIGH_SPEED, USBH_SUPER_SPEED };
  static const U16              MaxPacketsize[] = { 8, 8, 64, 512 };

  USBH_ASSERT_MAGIC(pHostController, USBH_HOST_CONTROLLER);
  //
  // Set default values, if not set by the drivers ioctl function
  //
  USBH_MEMSET(&IoctlPara, 0, sizeof(IoctlPara));
  IoctlPara.u.Caps.MaxSpeed = USBH_HIGH_SPEED;
  pDriver = pHostController->pDriver;
  if (pDriver->pfIoctl != NULL) {
    (void)pDriver->pfIoctl(pHostController->pPrvData, USBH_IOCTL_FUNC_GET_CAPABILITIES, &IoctlPara);
  }
  pHostController->Caps = IoctlPara.u.Caps;
  for (i = 0; i < (int)IoctlPara.u.Caps.MaxSpeed; i++) {
    //
    // Create the required endpoints to make the communication on EP0.
    //
    pHostController->RootEndpoints[i] = pDriver->pfAddEndpoint(pHostController->pPrvData,
                                                               USB_EP_TYPE_CONTROL, 0, 0,
                                                               MaxPacketsize[i], 0, Speed[i]);
    if (pHostController->RootEndpoints[i] == NULL) {
      USBH_WARN((USBH_MCAT_HC, "USBH_StartHostController:pfAddEndpoint %d failed!", i));
    }
  }
  Status = pDriver->pfSetHcState(pHostController->pPrvData, USBH_HOST_RUNNING); // Turn on the host controller
  if (Status != USBH_STATUS_SUCCESS) {
    USBH_WARN((USBH_MCAT_HC, "USBH_StartHostController:pfSetHcState failed %08x", Status));
    return;
  }
  pHostController->State = HC_WORKING;                            // Update the host controller state to working
  USBH_ROOTHUB_InitPorts(&pHostController->RootHub);     // Start the enumeration of the complete bus
  USBH_HC_ServicePorts(pHostController);
}

/*********************************************************************
*
*       USBH_GetUsbAddress
*
*  Function description
*    Retrieves a free USB address. This function is called during
*    the enumeration.
*
*  Parameters
*    pHostController : Pointer to the host controller object.
*
*  Return value
*    != 0  : Success, this is a valid and free USB address.
*    0     : A free USB address is not available.
*/
U8 USBH_GetUsbAddress(USBH_HOST_CONTROLLER * pHostController) {
  unsigned i;
  unsigned LastAddress;
  U32     *pField;
  U32      Mask;

  LastAddress = pHostController->MaxAddress;
  if (LastAddress == 0u) {
    //
    // The driver will choose an address, so we return a dummy value here.
    //
    return 0xFF;
  }
  for (i = pHostController->NextFreeAddress; i <= LastAddress; i++) {
    pField = &pHostController->UsbAddressUsed[i >> 5];
    Mask   = 1uL << (i & 0x1Fu);
    if ((*pField & Mask) == 0u) {
      goto Found;
    }
  }
  for (i = 1; i < pHostController->NextFreeAddress; i++) {
    pField = &pHostController->UsbAddressUsed[i >> 5];
    Mask   = 1uL << (i & 0x1Fu);
    if ((*pField & Mask) == 0u) {
      goto Found;
    }
  }
  USBH_WARN((USBH_MCAT_DEVICE, "FATAL USBH_GetUsbAddress failed. No free USB address available!"));
  return 0;
Found:
  *pField |= Mask;
  pHostController->NextFreeAddress = i + 1u;
  if (pHostController->NextFreeAddress > LastAddress) {
    pHostController->NextFreeAddress = 1;
  }
  return i;
}

/*********************************************************************
*
*       USBH_FreeUsbAddress
*
*  Function description
*    Frees a USB address so that it can be used again by another device.
*    This function is called when a device object is deleted (usually
*    because the device was disconnected from the host).
*
*  Parameters
*    pHostController : Pointer to a host controller object.
*    Address         : The USB address which should be freed.
*/
void USBH_FreeUsbAddress(USBH_HOST_CONTROLLER * pHostController, U8 Address) {
  U32 Mask;

  USBH_ASSERT_MAGIC(pHostController, USBH_HOST_CONTROLLER);
  if (pHostController->MaxAddress != 0u) {
    USBH_ASSERT(Address <= pHostController->MaxAddress);
    Mask = 1uL << (Address & 0x1Fu);
    pHostController->UsbAddressUsed[Address >> 5] &= ~Mask;
  }
}

/*********************************************************************
*
*       USBH_HC_ServicePorts
*
*  Function description
*    Check and service all ports (RootHub and external HUB)
*
*  Parameters
*    pHostController : Pointer to a host controller object.
*/
void USBH_HC_ServicePorts(USBH_HOST_CONTROLLER * pHostController) {
  USBH_ASSERT_MAGIC(pHostController, USBH_HOST_CONTROLLER);
  //
  // Always execute 'ServicePorts' from time context.
  //
  USBH_StartTimer(&pHostController->PortServiceTimer, 0);
}

/*********************************************************************
*
*       USBH_HCM_AllocContiguousMemory
*
*  Function description
*    Allocates contiguous memory and checks the returned alignment
*    of the physical addresses.
*
*  Parameters
*    NumBytes    : Size of the memory area in bytes.
*    Alignment   : Number of bytes for alignment of each physical item
*                  in the memory.
*    ppVirtAddr  : Pointer to a pointer to a U32 to store the virtual
*                  address which is used by the CPU.
*    pPhyAddr    : Pointer to a U32 to store the physical address
*                  which is used by the USB controller.
*
*  Return value
*    USBH_STATUS_SUCCESS           : Allocation successful.
*    USBH_STATUS_MEMORY            : Not enough memory.
*/
USBH_STATUS USBH_HCM_AllocContiguousMemory(U32 NumBytes, U32 Alignment, void ** ppVirtAddr, PTR_ADDR * pPhyAddr) {
  void         * pMemArea;
  PTR_ADDR       PhyAddr;

  pMemArea = USBH_TRY_MALLOC_XFERMEM(NumBytes, Alignment);
  if (pMemArea == NULL) {
    return USBH_STATUS_MEMORY;
  }
  if (!USBH_IS_ALIGNED(SEGGER_PTR2ADDR(pMemArea), Alignment)) {   // lint D:103[b]
    USBH_WARN((USBH_MCAT_INIT, "ERROR _AllocContiguousMemory: Alignment error: virt. addr: 0x%lx!", pMemArea));
    USBH_PANIC("Memory alignment");
  }
  PhyAddr = USBH_V2P(pMemArea);
  if (PhyAddr == 0u) {
    USBH_PANIC("ERROR _AllocContiguousMemory: USBH_V2P: return NULL!");
  }
  if (!USBH_IS_ALIGNED(PhyAddr, Alignment)) { // Alignment error.
    USBH_WARN((USBH_MCAT_INIT, "ERROR _AllocContiguousMemory: Alignment error: phys. addr: 0x%lx!", *pPhyAddr));
    USBH_PANIC("Memory alignment");
  }
  *ppVirtAddr = pMemArea;
  *pPhyAddr   = PhyAddr;
  return USBH_STATUS_SUCCESS;
}

/*********************************************************************
*
*       USBH_ClaimActivePortReset
*
*  Function description
*    Claim port reset activities for a hub port.
*    Port reset should only processed for one port at a time.
*
*  Parameters
*    pHost     : Host controller.
*
*  Return value
*    == 0 : Claimed successfully.
*    != 0 : Could not claim. Port reset already in progress for another port.
*/
unsigned USBH_ClaimActivePortReset(USBH_HOST_CONTROLLER * pHost) {
  unsigned Ret;

  USBH_ASSERT_MAGIC(pHost, USBH_HOST_CONTROLLER);
  Ret = pHost->ActivePortReset;
  if (Ret == 0u) {
#if USBH_DELAY_BETWEEN_ENUMERATIONS > 0
    //
    // Serial enumerations (with delay)
    //
    I32  tDiff;

    tDiff = USBH_TimeDiff(pHost->NextPossibleEnum, USBH_OS_GetTime32());
    if (tDiff > 0 && tDiff <= USBH_DELAY_BETWEEN_ENUMERATIONS) {
      USBH_StartTimer(&pHost->PortServiceTimer, (U32)tDiff);
      Ret = 1;
    } else {
      pHost->ActivePortReset = 1u;
    }
#else
    //
    // Parallel enumerations
    //
    pHost->ActivePortReset = 1u;
#endif
  }
  USBH_LOG((USBH_MCAT_RHUB_SM, "ClaimPortReset %s: %x", (Ret == 0) ? "ok" : "fail", pHost->ActivePortReset));
  return Ret;
}

/*********************************************************************
*
*       USBH_ReleaseActivePortReset
*
*  Function description
*    Release port reset activities.
*    Port reset should only processed for one port at a time.
*
*  Parameters
*    pHost     : Host controller.
*/
void USBH_ReleaseActivePortReset(USBH_HOST_CONTROLLER * pHost) {
  USBH_ASSERT_MAGIC(pHost, USBH_HOST_CONTROLLER);
  USBH_ASSERT((pHost->ActivePortReset & 1u) != 0);
  pHost->ActivePortReset &= ~1u;
#if USBH_DELAY_BETWEEN_ENUMERATIONS > 0
  pHost->NextPossibleEnum = USBH_TIME_CALC_EXPIRATION(USBH_DELAY_BETWEEN_ENUMERATIONS);
#endif
  USBH_LOG((USBH_MCAT_RHUB_SM, "ReleasePortReset: %x", pHost->ActivePortReset));
  USBH_StartTimer(&pHost->PortServiceTimer, 0);
}

/*********************************************************************
*
*       USBH_ClaimActiveEnumeration
*
*  Function description
*    Claim port enumeration activities for a device.
*
*  Parameters
*    pHost     : Host controller.
*/
void USBH_ClaimActiveEnumeration(USBH_HOST_CONTROLLER * pHost) {
  USBH_ASSERT_MAGIC(pHost, USBH_HOST_CONTROLLER);
#if USBH_DELAY_BETWEEN_ENUMERATIONS > 0
  pHost->ActivePortReset += 2u;
#else
  USBH_USE_PARA(pHost);
#endif
  USBH_LOG((USBH_MCAT_DEVICE_ENUM, "ClaimEnumeration: %x", pHost->ActivePortReset));
}

/*********************************************************************
*
*       USBH_ReleaseActiveEnumeration
*
*  Function description
*    Release port enumeration activities.
*
*  Parameters
*    pHost     : Host controller.
*/
void USBH_ReleaseActiveEnumeration(USBH_HOST_CONTROLLER * pHost) {
  USBH_ASSERT_MAGIC(pHost, USBH_HOST_CONTROLLER);
#if USBH_DELAY_BETWEEN_ENUMERATIONS > 0
  USBH_ASSERT((pHost->ActivePortReset & 0xFE) != 0);
  pHost->ActivePortReset -= 2u;
  pHost->NextPossibleEnum = USBH_TIME_CALC_EXPIRATION(USBH_DELAY_BETWEEN_ENUMERATIONS);
#endif
  USBH_StartTimer(&pHost->PortServiceTimer, 0);
  USBH_LOG((USBH_MCAT_DEVICE_ENUM, "ReleaseEnumeration: %x", pHost->ActivePortReset));
}

/*********************************************************************
*
*       USBH_SetRootPortPower
*
*  Function description
*    Set port of the root hub to a given power state.
*
*    The application must ensure that no transaction is pending on the port
*    before setting it into suspend state.
*
*  Parameters
*    HCIndex:  Index of the host controller.
*    Port:     Port number of the roothub. Ports are counted starting with 1.
*              if set to 0, the new state is set to all ports of the root hub.
*    State:    New power state of the port.
*/
void USBH_SetRootPortPower(U32 HCIndex, U8 Port, USBH_POWER_STATE State) {
  USBH_HOST_CONTROLLER   * pHost;
  const USBH_HOST_DRIVER * pDriver;
  unsigned                 PortCount;
  unsigned                 i;
  U32                      PortStatus;

  pHost = USBH_HCIndex2Inst(HCIndex);
  if (pHost == NULL) {
    return;
  }
  pDriver = pHost->pDriver;
  PortCount = pDriver->pfGetPortCount(pHost->pPrvData);
  for (i = 1; i <= PortCount; i++) {
    if (Port != 0u && Port != i) {
      continue;
    }
    switch (State) {
      case USBH_NORMAL_POWER:
        PortStatus = pDriver->pfGetPortStatus(pHost->pPrvData, i);
        if ((PortStatus & PORT_STATUS_SUSPEND) != 0u) {
          //
          // Resume from suspend.
          //
          pDriver->pfSetPortSuspend(pHost->pPrvData, i, USBH_PORT_POWER_RUNNING);
        } else {
          //
          // Power up port.
          //
          if (USBH_Global.pfOnSetPortPower != NULL) {
            USBH_Global.pfOnSetPortPower(HCIndex, i, 1);
          }
          pDriver->pfSetPortPower(pHost->pPrvData, i, 1);
        }
        break;
      case USBH_SUSPEND:
        pDriver->pfSetPortSuspend(pHost->pPrvData, i, USBH_PORT_POWER_SUSPEND);
        break;
      case USBH_POWER_OFF:
        //
        // Power down port.
        //
        pDriver->pfSetPortPower(pHost->pPrvData, i, 0);
        if (USBH_Global.pfOnSetPortPower != NULL) {
          USBH_Global.pfOnSetPortPower(HCIndex, i, 0);
        }
        break;
      default:
        // Ignore command.
        break;
    }
  }
  USBH_HC_ServicePorts(pHost);
}

/*********************************************************************
*
*       USBH_GetNumRootPortConnections
*
*  Function description
*    Determine how many devices are directly connected to the host controllers
*    root hub ports. All physically connected devices are counted,
*    irrespective of the identification or enumeration of these devices.
*    Devices connected via a hub are not counted.
*
*  Parameters
*    HCIndex:  Index of the host controller.
*
*  Return value
*    Number of devices physically connected to the host controllers
*    root hub ports.
*/
unsigned USBH_GetNumRootPortConnections(U32 HCIndex) {
  USBH_HOST_CONTROLLER   * pHost;
  const USBH_HOST_DRIVER * pDriver;
  unsigned                 PortCount;
  unsigned                 i;
  U32                      PortStatus;
  unsigned                 NumConnections;

  NumConnections = 0;
  pHost = USBH_HCIndex2Inst(HCIndex);
  if (pHost != NULL) {
    pDriver = pHost->pDriver;
    PortCount = pDriver->pfGetPortCount(pHost->pPrvData);
    for (i = 1; i <= PortCount; i++) {
      PortStatus = pDriver->pfGetPortStatus(pHost->pPrvData, i);
      if ((PortStatus & PORT_STATUS_CONNECT) != 0u) {
        NumConnections++;
      }
    }
  }
  return NumConnections;
}

/*************************** End of file ****************************/
