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
File        : USBH_HID_Int.h
Purpose     : Internal header file of the HID module
-------------------------- END-OF-HEADER -----------------------------
*/

#ifndef USBH_HID_INT_H__
#define USBH_HID_INT_H__

#include "USBH_HID.h"
#include "USBH_Int.h"

#if defined(__cplusplus)
  extern "C" {                 // Make sure we have C-declarations in C++ programs
#endif

/*********************************************************************
*
*       Defines, configurable
*
**********************************************************************
*/
#ifndef USBH_HID_MAX_USAGES
  #define USBH_HID_MAX_USAGES               32u
#endif


/*********************************************************************
*
*       Defines, fixed
*
**********************************************************************
*/
//
// Usage pages
//
//lint -esym(750,USBH_HID_USAGE_PAGE_*)  D:109
#define USBH_HID_USAGE_PAGE_UNDEFINED              0x0000UL
#define USBH_HID_USAGE_PAGE_GENERIC_DESKTOP        0x0001UL
#define USBH_HID_USAGE_PAGE_SIMULATION             0x0002UL
#define USBH_HID_USAGE_PAGE_VR_CONTROLS            0x0003UL
#define USBH_HID_USAGE_PAGE_SPORTS_CONTROLS        0x0004UL
#define USBH_HID_USAGE_PAGE_GAMING_CONTROLS        0x0005UL
#define USBH_HID_USAGE_PAGE_KEYBOARD               0x0007UL
#define USBH_HID_USAGE_PAGE_LEDS                   0x0008UL
#define USBH_HID_USAGE_PAGE_BUTTON                 0x0009UL
#define USBH_HID_USAGE_PAGE_ORDINALS               0x000AUL
#define USBH_HID_USAGE_PAGE_TELEPHONY              0x000BUL
#define USBH_HID_USAGE_PAGE_CONSUMER               0x000CUL
#define USBH_HID_USAGE_PAGE_DIGITIZERS             0x000DUL
#define USBH_HID_USAGE_PAGE_PHYSICAL_IFACE         0x000EUL
#define USBH_HID_USAGE_PAGE_UNICODE                0x0010UL
#define USBH_HID_USAGE_PAGE_ALPHANUM_DISPLAY       0x0014UL
#define USBH_HID_USAGE_PAGE_MONITOR                0x0080UL
#define USBH_HID_USAGE_PAGE_MONITOR_ENUM_VAL       0x0081UL
#define USBH_HID_USAGE_PAGE_VESA_VC                0x0082UL
#define USBH_HID_USAGE_PAGE_VESA_CMD               0x0083UL
#define USBH_HID_USAGE_PAGE_POWER                  0x0084UL
#define USBH_HID_USAGE_PAGE_BATTERY_SYSTEM         0x0085UL
#define USBH_HID_USAGE_PAGE_BARCODE_SCANNER        0x008BUL
#define USBH_HID_USAGE_PAGE_SCALE                  0x008CUL
#define USBH_HID_USAGE_PAGE_CAMERA_CONTROL         0x0090UL
#define USBH_HID_USAGE_PAGE_ARCADE                 0x0091UL
#define USBH_HID_USAGE_PAGE_MICROSOFT              0xFF00UL

//
// Usages, generic desktop
//
//lint -esym(750,USBH_HID_USAGE_GENDESK_*)  D:109
#define USBH_HID_USAGE_GENDESK_POINTER             0x0001UL
#define USBH_HID_USAGE_GENDESK_MOUSE               0x0002UL
#define USBH_HID_USAGE_GENDESK_JOYSTICK            0x0004UL
#define USBH_HID_USAGE_GENDESK_GAME_PAD            0x0005UL
#define USBH_HID_USAGE_GENDESK_KEYBOARD            0x0006UL
#define USBH_HID_USAGE_GENDESK_KEYPAD              0x0007UL
#define USBH_HID_USAGE_GENDESK_X                   0x0030UL
#define USBH_HID_USAGE_GENDESK_Y                   0x0031UL
#define USBH_HID_USAGE_GENDESK_Z                   0x0032UL
#define USBH_HID_USAGE_GENDESK_RX                  0x0033UL
#define USBH_HID_USAGE_GENDESK_RY                  0x0034UL
#define USBH_HID_USAGE_GENDESK_RZ                  0x0035UL
#define USBH_HID_USAGE_GENDESK_SLIDER              0x0036UL
#define USBH_HID_USAGE_GENDESK_DIAL                0x0037UL
#define USBH_HID_USAGE_GENDESK_WHEEL               0x0038UL
#define USBH_HID_USAGE_GENDESK_HAT_SWITCH          0x0039UL
#define USBH_HID_USAGE_GENDESK_COUNTED_BUFFER      0x003AUL
#define USBH_HID_USAGE_GENDESK_BYTE_COUNT          0x003BUL
#define USBH_HID_USAGE_GENDESK_MOTION_WAKEUP       0x003CUL
#define USBH_HID_USAGE_GENDESK_VX                  0x0040UL
#define USBH_HID_USAGE_GENDESK_VY                  0x0041UL
#define USBH_HID_USAGE_GENDESK_VZ                  0x0042UL
#define USBH_HID_USAGE_GENDESK_VBRX                0x0043UL
#define USBH_HID_USAGE_GENDESK_VBRY                0x0044UL
#define USBH_HID_USAGE_GENDESK_VBRZ                0x0045UL
#define USBH_HID_USAGE_GENDESK_VNO                 0x0046UL
#define USBH_HID_USAGE_GENDESK_TWHEEL              0x0048UL
#define USBH_HID_USAGE_GENDESK_SYSTEM_CONTROL      0x0080UL
#define USBH_HID_USAGE_GENDESK_SYSTEM_POWER_DOWN   0x0081UL
#define USBH_HID_USAGE_GENDESK_SYSTEM_SLEEP        0x0082UL
#define USBH_HID_USAGE_GENDESK_SYSTEM_WAKEUP       0x0083UL
#define USBH_HID_USAGE_GENDESK_SYSTEM_CONTEXT_MENU 0x0084UL
#define USBH_HID_USAGE_GENDESK_SYSTEM_MAIN_MENU    0x0085UL
#define USBH_HID_USAGE_GENDESK_SYSTEM_APP_MENU     0x0086UL
#define USBH_HID_USAGE_GENDESK_SYSTEM_MENU_HELP    0x0087UL
#define USBH_HID_USAGE_GENDESK_SYSTEM_MENU_EXIT    0x0088UL
#define USBH_HID_USAGE_GENDESK_SYSTEM_MENU_SELECT  0x0089UL
#define USBH_HID_USAGE_GENDESK_SYSTEM_MENU_RIGHT   0x008AUL
#define USBH_HID_USAGE_GENDESK_SYSTEM_MENU_LEFT    0x008BUL
#define USBH_HID_USAGE_GENDESK_SYSTEM_MENU_UP      0x008CUL
#define USBH_HID_USAGE_GENDESK_SYSTEM_MENU_DOWN    0x008DUL
#define USBH_HID_USAGE_GENDESK_APPLE_EJECT         0x00B8UL

//
// Usages, generic desktop
//
//lint -esym(750,USBH_HID_USAGE_CONSUMER_*)  D:109
#define USBH_HID_USAGE_CONSUMER_VOLUME_INC        0x00E9UL
#define USBH_HID_USAGE_CONSUMER_VOLUME_DEC        0x00EAUL
#define USBH_HID_USAGE_CONSUMER_MUTE              0x00E2UL
#define USBH_HID_USAGE_CONSUMER_PLAY_PAUSE        0x00CDUL
#define USBH_HID_USAGE_CONSUMER_SCAN_NEXT_TRACK   0x00B5UL
#define USBH_HID_USAGE_CONSUMER_SCAN_PREV_TRACK   0x00B6UL
#define USBH_HID_USAGE_CONSUMER_REPEAT            0x00BCUL
#define USBH_HID_USAGE_CONSUMER_RANDOM_PLAY       0x00B9UL

#define USBH_HID_USAGE_TYPE(Page, Usage) (((Page) << 16) | (Usage))


#define GET_HID_PLUGIN_FROM_ENTRY(pListEntry)      STRUCT_BASE_POINTER(pListEntry, USBH_HID_DETECTION_HOOK, ListEntry)
#define GET_HID_HANDLER_FROM_ENTRY(pListEntry)     STRUCT_BASE_POINTER(pListEntry, USBH_HID_HANDLER_HOOK, ListEntry)

#define HID_PLUGIN_MAGIC           FOUR_CHAR_ULONG('H','I','D','P')
#define HID_HANDLER_MAGIC          FOUR_CHAR_ULONG('H','I','D','H')

#define HID_KEYBOARD_MAGIC         FOUR_CHAR_ULONG('H','I','D','K')
#define HID_GENERIC_MAGIC          FOUR_CHAR_ULONG('H','I','D','T')
#define HID_MOUSE_MAGIC            FOUR_CHAR_ULONG('H','I','D','M')
#define HID_FT260_MAGIC            FOUR_CHAR_ULONG('H','I','D','F')

/*********************************************************************
*
*       Types
*
**********************************************************************
*/
typedef struct _USBH_HID_INST USBH_HID_INST;

//lint -esym(9058, _USBH_HID_INST)  N:100

typedef enum {
  StateInit = 1,    // Set during device initialization
  StateStop,        // Device is removed.
  StateError,       // Application/Hardware error, the device has to be removed.
  StateRunning      // Working state.
} USBH_HID_STATE;

typedef struct {
  U8                    EPAddr;
  I8                    InUse;
  U16                   MaxPacketSize;
  USBH_URB              Urb;
  USBH_OS_EVENT_OBJ   * pEvent;
  unsigned              RefCount;
  U8                    AbortFlag;
  USBH_URB              AbortUrb;
  USBH_INTERFACE_HANDLE hInterface;
} HID_EP_DATA;

struct _USBH_HID_INST {
  USBH_HID_INST               * pNext;
  USBH_HID_STATE                RunningState;
  U8                            DevInterfaceID;
  I8                            WasNotified;
  I8                            IsOpened;
  USBH_INTERFACE_ID             InterfaceID;
  USBH_INTERFACE_HANDLE         hInterface;
  USBH_TIMER                    RemovalTimer;
  HID_EP_DATA                   Control;
  HID_EP_DATA                   IntIn;
  HID_EP_DATA                   IntOut;
  U32                           MaxOutTransferSize;
  U32                           MaxInTransferSize;
  int                           ReadErrorCount;
  U32                           RefCnt;
  U8                          * pReportBufferDesc;
  U8                          * pInBuffer;
  U8                          * pOutBuffer;
  U16                           ReportDescriptorSize;
  U16                           IntErrCnt;
  USBH_TIME                     LastIntErr;
  USBH_DLIST                    HandlerList;
  U8                            DeviceType;
  U8                            PollIntEP;
  USBH_HID_HANDLE               Handle;
  USBH_HID_REPORT_INFO          ReportInfo[USBH_HID_MAX_REPORTS];
  U8                            NumReportInfos;
  U8                            ReportIDsUsed;
  U8                            DevIndex;          // Device name that is used in order to open the device from outside.
  I8                            IgnoreReportParseWarning;
};

typedef struct {
  U32             InRptLen;
  U32             OutRptLen;
  U16             RptSize;
  U16             RptCount;
  U8              ReportId;
  U8              Signed;
  U16             NumUsages;
  U32             UsageMin;
  U32             UsageMax;
  U32             UsagePage;
  U32             Usage[USBH_HID_MAX_USAGES];
  USBH_ANY_SIGNED LogicalMin;
  USBH_ANY_SIGNED LogicalMax;
  USBH_ANY_SIGNED PhysicalMin;
  USBH_ANY_SIGNED PhysicalMax;
  U8              PhySigned;
  U32             AppUsage;
  void          * pContext;
} HID_FIELD_INFO;

typedef void _CHECK_REPORT_DESC_FUNC(unsigned Flag, const HID_FIELD_INFO *pField);

/*********************************************************************
*
*       USBH_HID_DETECTION_CB
*
*  Description
*    Function called on enumeration of a new device.
*
*  Parameters
*    pInst : Pointer to a HID instance.
*    Event : Device event.
*/
typedef void (USBH_HID_DETECTION_CB)    (USBH_HID_INST * pInst);

/*********************************************************************
*
*       USBH_HID_REPORT_HANDLER
*
*  Description
*    Function called for every report received.
*
*  Parameters
*    pContext : Pointer to private structure of the plugin.
*    pReport  : Report data.
*    Len      : Size of the report in bytes.
*    Handled  : When set (!= 0) the data have been already handled by another HID plug-in
*               based on the same device type.
*
*  Return value
*    != 0                          - Report has     been handled by this callback routine.
*    == 0                          - Report has not been handled by this callback routine.
*/
typedef int (USBH_HID_REPORT_HANDLER)    (void *pContext, const U8 *pReport, unsigned Len, int Handled);

/*********************************************************************
*
*       USBH_HID_REMOVAL_HANDLER
*
*  Description
*    Function called if a device was removed.
*
*  Parameters
*    pContext : Pointer to private structure of the plugin.
*/
typedef void (USBH_HID_REMOVAL_HANDLER)    (void *pContext);

/*********************************************************************
*
*       USBH_HID_DETECTION_HOOK
*
*  Description
*    Use to register plugins.
*/
typedef struct {
  USBH_DLIST                 ListEntry;        // For linked list of all plugings
  USBH_HID_DETECTION_CB    * pDetect;
#if USBH_DEBUG > 1
  U32                        Magic;
#endif
} USBH_HID_DETECTION_HOOK;

/*********************************************************************
*
*       USBH_HID_HANDLER_HOOK
*
*  Description
*    Use to register plugins.
*/
typedef struct {
  USBH_DLIST                 ListEntry;        // For linked list of all handler
  void                     * pContext;
  USBH_HID_REPORT_HANDLER  * pHandler;
  USBH_HID_REMOVAL_HANDLER * pRemove;
#if USBH_DEBUG > 1
  U32                        Magic;
#endif
} USBH_HID_HANDLER_HOOK;


/*********************************************************************
*
*       Functions
*
**********************************************************************
*/

U32         USBH_HID__GetBits(const U8 * pData, unsigned FirstBit, unsigned NumBits);
I32         USBH_HID__GetBitsSigned(const U8 * pData, unsigned FirstBit, unsigned NumBits);
USBH_STATUS USBH_HID__SubmitOutBuffer(USBH_HID_INST * pInst, const U8 * pBuffer, U32 NumBytes, USBH_HID_USER_FUNC * pfUser, USBH_HID_RW_CONTEXT * pRWContext, unsigned Flags);
USBH_STATUS USBH_HID__SubmitOut(USBH_HID_INST * pInst, const U8 * pBuffer, U32 NumBytes);
void        USBH_HID__ParseReportDesc(USBH_HID_INST * pInst, _CHECK_REPORT_DESC_FUNC *pCheckFunc, void *pContext);
USBH_STATUS USBH_HID__GetReportCtrl(USBH_HID_INST * pInst, U8 ReportID, unsigned Flags, U8 * pBuffer, U32 Length, U32 * pNumBytesRead);

void USBH_HID_RegisterPlugin(USBH_HID_DETECTION_HOOK *pHook);
void USBH_HID_RegisterReportHandler(const USBH_HID_INST *pInst, USBH_HID_HANDLER_HOOK *pHook);


#if defined(__cplusplus)
  }
#endif

#endif // USBH_HID_INT_H__

/*************************** End of file ****************************/
