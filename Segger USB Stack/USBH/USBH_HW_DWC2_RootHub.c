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
File        : USBH_HW_DWC2_RootHub.c
Purpose     : USB host implementation
-------------------------- END-OF-HEADER -----------------------------
*/
#ifdef USBH_HW_DWC2_C_

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/

/*********************************************************************
*
*       Defines
*
**********************************************************************
*/

#define DWC2_HPRT_PSPD        (3uL << 17)
#define DWC2_HPRT_PSPD_FULL   (1uL << 17)
#define DWC2_HPRT_PSPD_LOW    (2uL << 17)
#define DWC2_HPRT_PPWR_ON     (1uL << 12)
#define DWC2_HPRT_PRST        (1uL <<  8)
#define DWC2_HPRT_PSUSP       (1uL <<  7)
#define DWC2_HPRT_PRES        (1uL <<  6)
#define DWC2_HPRT_POCCHNG     (1uL <<  5)
#define DWC2_HPRT_POCA        (1uL <<  4)
#define DWC2_HPRT_PENCHNG     (1uL <<  3)
#define DWC2_HPRT_PENA        (1uL <<  2)
#define DWC2_HPRT_PCDET       (1uL <<  1)
#define DWC2_HPRT_PCSTS       (1uL <<  0)


/*********************************************************************
*
*       _DWC2_ROOTHUB_GetPortCount
*
*  Function description
*    Returns the number of root hub ports. An zero value is returned on an error.
*/
static unsigned int _DWC2_ROOTHUB_GetPortCount(USBH_HC_HANDLE hHostController) {
  USBH_USE_PARA(hHostController);
  return 1;
}

/*********************************************************************
*
*       _DWC2_ROOTHUB_GetHubStatus
*
*  Function description
*    Returns the HUB Status as defined in the USB specification 11.24.2.6
*    b0 : Local power source , where 0 -> Local power supply good
*                                    1 -> Local power supply inactive/lost.
*    b1 : Over-current,        where 0 -> No over-current condition currently exists.
*                                    1 -> A hub over-current condition exists.
*/
static U32 _DWC2_ROOTHUB_GetHubStatus(USBH_HC_HANDLE hHostController) {
  USBH_USE_PARA(hHostController);
  return 0;
}

/*********************************************************************
*
*       _DWC2_ROOTHUB_GetPortStatus
*
*  Function description
*    One based index of the port / return the port Status as
*    defined in the USB specification 11.24.2.7
*    BitPos
*    0       Current Connect Status: (PORT_CONNECTION) This field reflects whether or not a device is currently connected to this port.
*                                      0 = No device is present.
*                                      1 = A device is present on this port.
*
*    1       Port Enabled/Disabled:  (PORT_ENABLE) Ports can be enabled by the USB System Software only.  Ports
*                                     can be disabled by either a fault condition (disconnect event or other fault condition)
*                                     or by the USB System Software.
*                                      0 = Port is disabled.
*                                      1 = Port is enabled.
*
*    2       Suspend:                (PORT_SUSPEND) This field indicates whether or not the device on this port is suspended.
*                                     Setting this field causes the device to suspend by not propagating bus traffic downstream.
*                                     This field may be reset by a request or by resume signaling from the device attached to the port.
*                                      0 = Not suspended.
*                                      1 = Suspended or resuming.
*
*    3       Over-current:           (PORT_OVER_CURRENT) If the hub reports over-current conditions on a per-port basis, this field will
*                                     indicate that the current drain on the port exceeds the specified maximum.
*                                      0 = All no over-current condition exists on this port.
*                                      1 = An over-current condition exists on this port.
*    4       Reset:                  (PORT_RESET) This field is set when the host wishes to reset the attached device.  It remains set
*                                     until the reset signaling is turned off by the hub.
*                                      0 = Reset signaling not asserted.
*                                      1 = Reset signaling asserted.
*
*    5-7     Reserved                 These bits return 0 when read.
*
*    8       Port Power:              (PORT_POWER) This field reflects a port's logical, power control state.  Because hubs can
*                                      implement different methods of port power switching, this field may or may not represent
*                                      whether power is applied to the port. The device descriptor reports the type of power
*                                      switching implemented by the hub.
*                                       0 = This port is in the Powered-off state.
*                                       1 = This port is not in the Powered-off state.
*
*    9       Low-Speed Device Attached:  (PORT_LOW_SPEED) This is relevant only if a device is attached.
*                                       0 = Full-speed or High-speed device attached to this port (determined by bit 10).
*                                       1 = Low-speed device attached to this port.
*
*   10       High-speed Device Attached:  (PORT_HIGH_SPEED) This is relevant only if a device is attached.
*                                       0 = Full-speed device attached to this port.
*                                       1 = High-speed device attached to this port.
*
*   11       Port Test Mode:          (PORT_TEST) This field reflects the Status of the port's test mode.  Software uses the
*                                      SetPortFeature() and ClearPortFeature() requests to manipulate the port test mode.
*                                       0 = This port is not in the Port Test Mode.
*                                       1 = This port is in Port Test Mode.
*
*   12       Port Indicator Control:  (PORT_INDICATOR) This field is set to reflect software control of the port indicator.
*                                       0 = Port indicator displays default colors.
*                                       1 = Port indicator displays software controlled color.
*   13-15    Reserved                    These bits return 0 when read.
*/
static U32  _DWC2_ROOTHUB_GetPortStatus(USBH_HC_HANDLE hHostController, U8  Port) {
  USBH_DWC2_INST * pInst;
  U32              Status;
  U32              PortStatus;

  USBH_USE_PARA(Port);
  pInst = USBH_HDL2PTR(USBH_DWC2_INST, hHostController);
  USBH_DWC2_IS_DEV_VALID(pInst);
  Status  = pInst->pHWReg->HPRT;
  PortStatus = 0;
  if ((Status & DWC2_HPRT_PCSTS) != 0u) {
    PortStatus |= PORT_STATUS_CONNECT;
  }
  if ((Status & DWC2_HPRT_PCDET) != 0u) {
    pInst->pHWReg->HPRT |= (1uL << 1);
  }
  if ((Status & DWC2_HPRT_PENA) != 0u) {
    PortStatus |= PORT_STATUS_ENABLED;
  }
  if ((Status & DWC2_HPRT_POCA) != 0u) {
    PortStatus |= PORT_STATUS_OVER_CURRENT;
  }
  if ((Status & DWC2_HPRT_PSUSP) != 0u) {
    PortStatus |= PORT_STATUS_SUSPEND;
  }
  if ((Status & DWC2_HPRT_PRST) != 0u) {
    PortStatus |= PORT_STATUS_RESET;
  }
  if ((Status & DWC2_HPRT_PPWR_ON) != 0u) {
    PortStatus |= PORT_STATUS_POWER;
  }
  if ((Status & DWC2_HPRT_PSPD_LOW) != 0u) {
    PortStatus |= PORT_STATUS_LOW_SPEED;
  }
#if USBH_DWC2_HIGH_SPEED
  if ((Status & DWC2_HPRT_PSPD) == 0u) {
    PortStatus |= PORT_STATUS_HIGH_SPEED;
  }
#endif
  return PortStatus;
}

/*********************************************************************
*
*       _DWC2_ROOTHUB_SetPortPower
*
*  Function description
*    one based index of the port / 1 to turn the power on or 0 for off
*/
static void _DWC2_ROOTHUB_SetPortPower(USBH_HC_HANDLE hHostController, U8  Port, U8 PowerOn) {
  USBH_DWC2_INST * pInst;

  pInst = USBH_HDL2PTR(USBH_DWC2_INST, hHostController);
  USBH_DWC2_IS_DEV_VALID(pInst);
  USBH_USE_PARA(Port);
  if (PowerOn != 0u) {
    if ((pInst->pHWReg->HPRT & DWC2_HPRT_PPWR_ON) == 0u) {
      pInst->pHWReg->HPRT |= DWC2_HPRT_PPWR_ON;
    }
  }  else {
    pInst->pHWReg->HPRT &= ~DWC2_HPRT_PPWR_ON;
  }
}

/*********************************************************************
*
*       _DWC2_ROOTHUB_ResetPort
*
*  Function description
*    One based index of the port
*/
static void _DWC2_ROOTHUB_ResetPort(USBH_HC_HANDLE hHostController, U8  Port) {
  USBH_DWC2_INST * pInst;
  U32 HPort;

  USBH_USE_PARA(Port);
  pInst = USBH_HDL2PTR(USBH_DWC2_INST, hHostController);
  USBH_DWC2_IS_DEV_VALID(pInst);
  HPort = pInst->pHWReg->HPRT;
  HPort &= ~DWC2_HPRT_PENA;
  HPort |= DWC2_HPRT_PRST;
  pInst->pHWReg->HPRT = HPort;
  USBH_OS_Delay(15);
  pInst->pHWReg->HPRT &= ~DWC2_HPRT_PRST;
  pInst->pfUbdRootHubNotification(pInst->pRootHubNotificationContext, 2);
}

/*********************************************************************
*
*       _DWC2_ROOTHUB_DisablePort
*
*  Function description
*    One based index of the port / Disable the port, no requests and SOF's are issued on this port
*/
static void _DWC2_ROOTHUB_DisablePort(USBH_HC_HANDLE hHostController, U8  Port) {
  USBH_DWC2_INST * pInst;

  USBH_USE_PARA(Port);
  pInst = USBH_HDL2PTR(USBH_DWC2_INST, hHostController);
  USBH_DWC2_IS_DEV_VALID(pInst);
  pInst->pHWReg->HPRT |= DWC2_HPRT_PENA;      // PENA bit is 'write 1 to clear'
}

/*********************************************************************
*
*       _DWC2_ROOTHUB_SetPortSuspend
*
*  Function description
*    One based index of the port / Switch the port power between running and suspend
*/
static void _DWC2_ROOTHUB_SetPortSuspend(USBH_HC_HANDLE hHostController, U8  Port, USBH_PORT_POWER_STATE State) {
  USBH_DWC2_INST * pInst;
  U32              v;

  USBH_USE_PARA(Port);
  pInst = USBH_HDL2PTR(USBH_DWC2_INST, hHostController);
  USBH_DWC2_IS_DEV_VALID(pInst);
  if (State == USBH_PORT_POWER_RUNNING) {
    v = pInst->pHWReg->HPRT & ~DWC2_HPRT_PENA;           // Don't clear the PENA bit by writing 1 to it !!
    pInst->pHWReg->HPRT = v | DWC2_HPRT_PRES;
    USBH_OS_Delay(21);
    pInst->pHWReg->HPRT &= ~(DWC2_HPRT_PENA | DWC2_HPRT_PRES);
    USBH_OS_Delay(10);
  } else if (State == USBH_PORT_POWER_SUSPEND) {
    v = pInst->pHWReg->HPRT & ~DWC2_HPRT_PENA;           // Don't clear the PENA bit by writing 1 to it !!
    pInst->pHWReg->HPRT = v | DWC2_HPRT_PSUSP;
  } else {
    // MISRA dummy comment
    USBH_WARN((USBH_MCAT_DRIVER_PORT, "_DWC2_ROOTHUB_SetPortSuspend: Unknown power state"));
  }
}

/*********************************************************************
*
*       _DWC2_ROOTHUB_HandlePortInt
*/
static void _DWC2_ROOTHUB_HandlePortInt(USBH_DWC2_INST * pInst) USBH_API_USE {
  U32 Port;
  volatile int i;

  Port = pInst->pHWReg->HPRT;
  //
  // Clear all interrupts. Don't clear the PENA bit by writing 1 to it !!
  //
  pInst->pHWReg->HPRT = Port & ~DWC2_HPRT_PENA;
  for (i = 0; i < 1000; i++) {
  }
  pInst->pfUbdRootHubNotification(pInst->pRootHubNotificationContext, 2);
  if ((Port & DWC2_HPRT_PENCHNG) != 0u) {
    if (pInst->PhyType == 1u) {
      if ((Port & DWC2_HPRT_PENA) != 0u) {
        U32    PortSpeed;

        PortSpeed = Port & (0x3uL << 17);
        if (PortSpeed == DWC2_HPRT_PSPD_LOW) {
          pInst->pHWReg->HCFG     = (pInst->pHWReg->HCFG & ~(3uL)) | (2uL << 0);       // Use the internal LS PHY
          pInst->pHWReg->HFIR     = 6000;            // Use 6 MHz as frame interval
        } else {
          pInst->pHWReg->HCFG     = (pInst->pHWReg->HCFG & ~(3uL)) | (1uL << 0);       // Use the internal FS PHY
          pInst->pHWReg->HFIR     = 48000;            // Use 48 MHz as frame interval
        }
      }
    }
  }
}

#else
/*********************************************************************
*
*       USBH_HW_DWC2_RootHub_c
*
*  Function description
*    Dummy function to avoid problems with certain compilers which
*    can not handle empty object files.
*/
void USBH_HW_DWC2_RootHub_c(void);
void USBH_HW_DWC2_RootHub_c(void) {
}

#endif  // _USBH_HW_DWC2_ROOTHUB_C_

/*************************** End of file ****************************/
