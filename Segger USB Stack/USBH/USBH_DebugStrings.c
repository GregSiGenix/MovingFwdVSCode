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
Purpose     : USB host  debug strings
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
*       #define Section
*
**********************************************************************
*/
//
// Suppress "macro parameter is used in both expanded and raw forms" diagnostic.
//
#ifdef __ICCARM__
  #if   (__VER__ / 10000) >= 706
    #pragma diag_suppress=Pg004
  #endif
#endif

/*********************************************************************
*
*       USBH_HcState2Str
*
*  Function description
*/
const char * USBH_HcState2Str(HOST_CONTROLLER_STATE x) {
  return (
    USBH_ENUM_TO_STR(HC_UNKNOWN) :
    USBH_ENUM_TO_STR(HC_WORKING) :
    USBH_ENUM_TO_STR(HC_REMOVED) :
    USBH_ENUM_TO_STR(HC_SUSPEND) :
      "unknown HC state"
    );
}

/*********************************************************************
*
*       USBH_EnumState2Str
*
*  Function description
*/
const char * USBH_EnumState2Str(DEV_ENUM_STATE x) {
  return (
    USBH_ENUM_TO_STR(DEV_ENUM_IDLE):
    USBH_ENUM_TO_STR(DEV_ENUM_START):
    USBH_ENUM_TO_STR(DEV_ENUM_GET_DEVICE_DESC):
    USBH_ENUM_TO_STR(DEV_ENUM_GET_CONFIG_DESC_PART):
    USBH_ENUM_TO_STR(DEV_ENUM_GET_CONFIG_DESC):
    USBH_ENUM_TO_STR(DEV_ENUM_GET_LANG_ID):
    USBH_ENUM_TO_STR(DEV_ENUM_GET_SERIAL_DESC):
    USBH_ENUM_TO_STR(DEV_ENUM_PREP_SET_CONFIG):
    USBH_ENUM_TO_STR(DEV_ENUM_SET_CONFIGURATION):
    USBH_ENUM_TO_STR(DEV_ENUM_INIT_HUB):
      "unknown enum state"
    );
}

/*********************************************************************
*
*       USBH_HubEnumState2Str
*
*  Function description
*/
const char * USBH_HubEnumState2Str(USBH_HUB_ENUM_STATE x) {
  return (
    USBH_ENUM_TO_STR(USBH_HUB_ENUM_IDLE):
    USBH_ENUM_TO_STR(USBH_HUB_ENUM_START):
    USBH_ENUM_TO_STR(USBH_HUB_ENUM_HUB_DESC):
    USBH_ENUM_TO_STR(USBH_HUB_ENUM_SET_ALTERNATE):
    USBH_ENUM_TO_STR(USBH_HUB_ENUM_DONE):
      "unknown hub init state"
    );
}

/*********************************************************************
*
*       USBH_HubAction2Str
*/
const char * USBH_HubAction2Str(USBH_HUB_ACTION x) {
  return (
    USBH_ENUM_TO_STR(USBH_HUB_ACT_IDLE):
    USBH_ENUM_TO_STR(USBH_HUB_ACT_GET_PORT_STATUS):
    USBH_ENUM_TO_STR(USBH_HUB_ACT_POWER_UP):
    USBH_ENUM_TO_STR(USBH_HUB_ACT_POWER_DOWN):
    USBH_ENUM_TO_STR(USBH_HUB_ACT_CLR_CHANGE):
    USBH_ENUM_TO_STR(USBH_HUB_ACT_DISABLE):
    USBH_ENUM_TO_STR(USBH_HUB_ACT_RESET):
    USBH_ENUM_TO_STR(USBH_HUB_ACT_GET_DESC):
    USBH_ENUM_TO_STR(USBH_HUB_ACT_SET_ADDRESS):
      "unknown hub action"
    );
}

/*********************************************************************
*
*       USBH_PortToDo2Str
*/
const char * USBH_PortToDo2Str(U8 x) {
#define PORT_TODO_TO_STR(a)     if (((x) & USBH_PORT_DO_ ## a) != 0u) {(void)strcat(Str, #a " ");}  //lint !e9023 !e9024  N:102
  static char Str[100];
  Str[0] = '\0';
  PORT_TODO_TO_STR(UPDATE_STATUS);
  PORT_TODO_TO_STR(POWER_UP);
  PORT_TODO_TO_STR(POWER_DOWN);
  PORT_TODO_TO_STR(DELAY);
  PORT_TODO_TO_STR(DISABLE);
  PORT_TODO_TO_STR(RESET);
  return Str;
}

/*********************************************************************
*
*       USBH_PortStatus2Str
*/
const char * USBH_PortStatus2Str(U32 x) {
#define PORT_STATUS_TO_STR(a)     if (((x) & PORT_STATUS_ ## a) != 0u) {(void)strcat(Str, #a " ");}   //lint !e9023 !e9024  N:102
  static char Str[100];
  Str[0] = '\0';
  PORT_STATUS_TO_STR(CONNECT);
  PORT_STATUS_TO_STR(ENABLED);
  PORT_STATUS_TO_STR(SUSPEND);
  PORT_STATUS_TO_STR(OVER_CURRENT);
  PORT_STATUS_TO_STR(RESET);
  PORT_STATUS_TO_STR(POWER);
  PORT_STATUS_TO_STR(LOW_SPEED);
  PORT_STATUS_TO_STR(HIGH_SPEED);
  return Str;
}

/*********************************************************************
*
*       USBH_HubPortResetState2Str
*
*  Function description
*/
const char * USBH_HubPortResetState2Str(USBH_HUB_PORTRESET_STATE x) {
  return (
    USBH_ENUM_TO_STR(USBH_HUB_PORTRESET_IDLE):
    USBH_ENUM_TO_STR(USBH_HUB_PORTRESET_START):
    USBH_ENUM_TO_STR(USBH_HUB_PORTRESET_RESTART):
    USBH_ENUM_TO_STR(USBH_HUB_PORTRESET_WAIT_RESTART):
    USBH_ENUM_TO_STR(USBH_HUB_PORTRESET_IS_ENABLED_0):
    USBH_ENUM_TO_STR(USBH_HUB_PORTRESET_WAIT_RESET_0):
    USBH_ENUM_TO_STR(USBH_HUB_PORTRESET_GET_DEV_DESC):
    USBH_ENUM_TO_STR(USBH_HUB_PORTRESET_IS_ENABLED_1):
    USBH_ENUM_TO_STR(USBH_HUB_PORTRESET_WAIT_RESET_1):
    USBH_ENUM_TO_STR(USBH_HUB_PORTRESET_SET_ADDRESS):
    USBH_ENUM_TO_STR(USBH_HUB_PORTRESET_START_DEVICE_ENUM):
      "unknown hub port state"
    );
}

/*********************************************************************
*
*       USBH_UrbFunction2Str
*
*  Function description
*/
const char * USBH_UrbFunction2Str(USBH_FUNCTION x) {
  return (
    USBH_ENUM_TO_STR(USBH_FUNCTION_CONTROL_REQUEST) :
    USBH_ENUM_TO_STR(USBH_FUNCTION_BULK_REQUEST) :
    USBH_ENUM_TO_STR(USBH_FUNCTION_INT_REQUEST) :
    USBH_ENUM_TO_STR(USBH_FUNCTION_ISO_REQUEST) :
    USBH_ENUM_TO_STR(USBH_FUNCTION_RESET_DEVICE) :
    USBH_ENUM_TO_STR(USBH_FUNCTION_RESET_ENDPOINT) :
    USBH_ENUM_TO_STR(USBH_FUNCTION_ABORT_ENDPOINT) :
    USBH_ENUM_TO_STR(USBH_FUNCTION_SET_INTERFACE) :
    USBH_ENUM_TO_STR(USBH_FUNCTION_SET_POWER_STATE) :
      "unknown USBH function code"
    );
}

/*********************************************************************
*
*       USBH_PortSpeed2Str
*
*  Function description
*/
const char * USBH_PortSpeed2Str(USBH_SPEED x) {
  return (
    USBH_ENUM_TO_STR(USBH_SPEED_UNKNOWN):
    USBH_ENUM_TO_STR(USBH_LOW_SPEED):
    USBH_ENUM_TO_STR(USBH_FULL_SPEED):
    USBH_ENUM_TO_STR(USBH_HIGH_SPEED):
      "unknown port speed"
    );
}

/*********************************************************************
*
*       USBH_EPType2Str
*
*  Function description
*/
const char * USBH_EPType2Str(U8 x) {
 return (
    USBH_ENUM_TO_STR(USB_EP_TYPE_CONTROL):
    USBH_ENUM_TO_STR(USB_EP_TYPE_ISO):
    USBH_ENUM_TO_STR(USB_EP_TYPE_BULK):
    USBH_ENUM_TO_STR(USB_EP_TYPE_INT):
      "unknown endpoint type"
    );
}

/*********************************************************************
*
*       USBH_GetStatusStr
*
*  Function description
*    Converts the result status into a string.
*
*  Parameters
*    x:    Result status to convert.
*
*  Return value
*    Pointer to a string which contains the result status in text form.
*/
const char * USBH_GetStatusStr(USBH_STATUS x) {
  return (
    USBH_ENUM_TO_STR(USBH_STATUS_SUCCESS                      ) :
    USBH_ENUM_TO_STR(USBH_STATUS_CRC                          ) :
    USBH_ENUM_TO_STR(USBH_STATUS_BITSTUFFING                  ) :
    USBH_ENUM_TO_STR(USBH_STATUS_DATATOGGLE                   ) :
    USBH_ENUM_TO_STR(USBH_STATUS_STALL                        ) :
    USBH_ENUM_TO_STR(USBH_STATUS_NOTRESPONDING                ) :
    USBH_ENUM_TO_STR(USBH_STATUS_PID_CHECK                    ) :
    USBH_ENUM_TO_STR(USBH_STATUS_UNEXPECTED_PID               ) :
    USBH_ENUM_TO_STR(USBH_STATUS_DATA_OVERRUN                 ) :
    USBH_ENUM_TO_STR(USBH_STATUS_DATA_UNDERRUN                ) :
    USBH_ENUM_TO_STR(USBH_STATUS_XFER_SIZE                    ) :
    USBH_ENUM_TO_STR(USBH_STATUS_DMA_ERROR                    ) :
    USBH_ENUM_TO_STR(USBH_STATUS_BUFFER_OVERRUN               ) :
    USBH_ENUM_TO_STR(USBH_STATUS_BUFFER_UNDERRUN              ) :
    USBH_ENUM_TO_STR(USBH_STATUS_OHCI_NOT_ACCESSED1           ) :
    USBH_ENUM_TO_STR(USBH_STATUS_OHCI_NOT_ACCESSED2           ) :
    USBH_ENUM_TO_STR(USBH_STATUS_NEED_MORE_DATA               ) :
    USBH_ENUM_TO_STR(USBH_STATUS_FRAME_ERROR                  ) :
    USBH_ENUM_TO_STR(USBH_STATUS_CHANNEL_NAK                  ) :
    USBH_ENUM_TO_STR(USBH_STATUS_ERROR                        ) :
    USBH_ENUM_TO_STR(USBH_STATUS_INVALID_PARAM                ) :
    USBH_ENUM_TO_STR(USBH_STATUS_PENDING                      ) :
    USBH_ENUM_TO_STR(USBH_STATUS_DEVICE_REMOVED               ) :
    USBH_ENUM_TO_STR(USBH_STATUS_CANCELED                     ) :
    USBH_ENUM_TO_STR(USBH_STATUS_BUSY                         ) :
    USBH_ENUM_TO_STR(USBH_STATUS_NO_CHANNEL                   ) :
    USBH_ENUM_TO_STR(USBH_STATUS_INVALID_DESCRIPTOR           ) :
    USBH_ENUM_TO_STR(USBH_STATUS_ENDPOINT_HALTED              ) :
    USBH_ENUM_TO_STR(USBH_STATUS_TIMEOUT                      ) :
    USBH_ENUM_TO_STR(USBH_STATUS_PORT                         ) :
    USBH_ENUM_TO_STR(USBH_STATUS_INVALID_HANDLE               ) :
    USBH_ENUM_TO_STR(USBH_STATUS_NOT_OPENED                   ) :
    USBH_ENUM_TO_STR(USBH_STATUS_ALREADY_ADDED                ) :
    USBH_ENUM_TO_STR(USBH_STATUS_ENDPOINT_INVALID             ) :
    USBH_ENUM_TO_STR(USBH_STATUS_NOT_FOUND                    ) :
    USBH_ENUM_TO_STR(USBH_STATUS_NOT_SUPPORTED                ) :
    USBH_ENUM_TO_STR(USBH_STATUS_ISO_DISABLED                 ) :
    USBH_ENUM_TO_STR(USBH_STATUS_LENGTH                       ) :
    USBH_ENUM_TO_STR(USBH_STATUS_COMMAND_FAILED               ) :
    USBH_ENUM_TO_STR(USBH_STATUS_INTERFACE_PROTOCOL           ) :
    USBH_ENUM_TO_STR(USBH_STATUS_INTERFACE_SUB_CLASS          ) :
    USBH_ENUM_TO_STR(USBH_STATUS_WRITE_PROTECT                ) :
    USBH_ENUM_TO_STR(USBH_STATUS_INTERNAL_BUFFER_NOT_EMPTY    ) :
    USBH_ENUM_TO_STR(USBH_STATUS_MTP_OPERATION_NOT_SUPPORTED  ) :
    USBH_ENUM_TO_STR(USBH_STATUS_MEMORY                       ) :
    USBH_ENUM_TO_STR(USBH_STATUS_RESOURCES                    ) :
    USBH_ENUM_TO_STR(USBH_STATUS_BAD_RESPONSE                 ) :
      "unknown status"
    );
}

/*********************************************************************
*
*       USBH_Ep0State2Str
*
*  Function description
*    Converts the EP0 phase value to a string.
*/
const char * USBH_Ep0State2Str(USBH_EP0_PHASE x) {
  return (
    USBH_ENUM_TO_STR(ES_IDLE) :
    USBH_ENUM_TO_STR(ES_SETUP) :
    USBH_ENUM_TO_STR(ES_COPY_DATA) :
    USBH_ENUM_TO_STR(ES_DATA) :
    USBH_ENUM_TO_STR(ES_PROVIDE_HANDSHAKE) :
    USBH_ENUM_TO_STR(ES_HANDSHAKE) :
    USBH_ENUM_TO_STR(ES_ERROR) :
      "unknown enum state!"
    );
}

#ifdef __ICCARM__
  #if   (__VER__ / 10000) >= 706
    #pragma diag_default=Pg004
  #endif
#endif

/*************************** End of file ****************************/
