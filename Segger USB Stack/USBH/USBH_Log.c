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
File        : USBH_Log.c
Purpose     : USB host stack log routines
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#include <stdarg.h>         //lint !e829  N:102 needed for printf-like debug logging functions
#include "USBH_Int.h"
#include "USBH_Util.h"

/*********************************************************************
*
*       Static const
*
**********************************************************************
*/
static const struct MCAT_STRINGS_t {
  U8         From;
  U8         To;
  const char Text[8];
} _aMCat2String[] = {
  { USBH_MCAT_INIT,         USBH_MCAT_INIT,        "INIT"     },
  { USBH_MCAT_DRIVER,       USBH_MCAT_DRIVER_IRQ,  "DRIVER"   },
  { USBH_MCAT_APPLICATION,  USBH_MCAT_APPLICATION, "APP"      },
  { USBH_MCAT_TIMER,        USBH_MCAT_TIMER_EX,    "TIMER"    },
  { USBH_MCAT_RHUB,         USBH_MCAT_RHUB_PORT,   "RootHUB"  },
  { USBH_MCAT_DEVICE,       USBH_MCAT_DEVICE_REF,  "Device"   },
  { USBH_MCAT_INTF,         USBH_MCAT_INTF_API,    "Intfce"   },
  { USBH_MCAT_MEM,          USBH_MCAT_MEM,         "MEM"      },
  { USBH_MCAT_HC,           USBH_MCAT_HC_REF,      "HC"       },
  { USBH_MCAT_PNP,          USBH_MCAT_PNP,         "PNP"      },
  { USBH_MCAT_URB,          USBH_MCAT_URB_QUEUE,   "URB"      },
  { USBH_MCAT_SUBST,        USBH_MCAT_SUBST,       "SUBST"    },
  { USBH_MCAT_ASSERT,       USBH_MCAT_ASSERT,      "Assert"   },
  { USBH_MCAT_HUB,          USBH_MCAT_HUB_URB,     "HUB"      },
  { USBH_MCAT_MSC,          USBH_MCAT_MSC_API,     "MSC"      },
  { USBH_MCAT_AUDIO,        USBH_MCAT_AUDIO,       "Audio"    },
  { USBH_MCAT_CCID,         USBH_MCAT_CCID,        "CCID"     },
  { USBH_MCAT_HID,          USBH_MCAT_HID_RDESC,   "HID"      },
  { USBH_MCAT_MIDI,         USBH_MCAT_MIDI,        "MIDI"     },
  { USBH_MCAT_MTP,          USBH_MCAT_MTP,         "MTP"      },
  { USBH_MCAT_CP210X,       USBH_MCAT_CP210X,      "CP210X"   },
  { USBH_MCAT_FT232,        USBH_MCAT_FT232,       "FT232"    },
  { USBH_MCAT_PRINTER,      USBH_MCAT_PRINTER,     "Printer"  },
  { USBH_MCAT_BULK,         USBH_MCAT_BULK,        "BULK"     },
  { USBH_MCAT_CDC,          USBH_MCAT_CDC,         "CDC "     },
  { USBH_MCAT_FT260,        USBH_MCAT_FT260,       "FT260"    },
  { USBH_MCAT_VIDEO,        USBH_MCAT_VIDEO,       "Video"    },
  { 0,                      0xFF,                  "??"       }    // Unknown category, must be the last entry in table.
};

#if USBH_DEBUG > 1
//
// Mapping from legacy MType to MCategory used by legacy Set/AddLog/WarnFilter() functions
//
static const U8 _aMType2MCategory[][4] = {
/* USBH_MTYPE_INIT          */  { USBH_MCAT_INIT,          USBH_MCAT_ASSERT,        0xFF,                    0xFF                     },
/* USBH_MTYPE_CORE          */  { USBH_MCAT_HC,            USBH_MCAT_ASSERT,        0xFF,                    0xFF                     },
/* USBH_MTYPE_TIMER         */  { USBH_MCAT_TIMER,         USBH_MCAT_TIMER_EX,      0xFF,                    0xFF                     },
/* USBH_MTYPE_DRIVER        */  { USBH_MCAT_DRIVER,        USBH_MCAT_DRIVER_URB,    USBH_MCAT_DRIVER_EP,     USBH_MCAT_DRIVER_PORT    },
/* USBH_MTYPE_MEM           */  { USBH_MCAT_MEM,           0xFF,                    0xFF,                    0xFF                     },
/* USBH_MTYPE_URB           */  { USBH_MCAT_URB,           USBH_MCAT_SUBST,         USBH_MCAT_URB_QUEUE,     0xFF                     },
/* USBH_MTYPE_OHCI          */  { USBH_MCAT_DRIVER,        USBH_MCAT_DRIVER_URB,    USBH_MCAT_DRIVER_EP,     USBH_MCAT_DRIVER_PORT    },
/* unused                   */  { 0xFF,                    0xFF,                    0xFF,                    0xFF                     },
/* USBH_MTYPE_PNP           */  { USBH_MCAT_PNP,           0xFF,                    0xFF,                    0xFF                     },
/* USBH_MTYPE_DEVICE        */  { USBH_MCAT_DEVICE,        USBH_MCAT_DEVICE_ENUM,   USBH_MCAT_INTF,          USBH_MCAT_INTF_API       },
/* USBH_MTYPE_RHUB          */  { USBH_MCAT_RHUB,          USBH_MCAT_RHUB_SM,       USBH_MCAT_RHUB_PORT,     0xFF                     },
/* USBH_MTYPE_HUB           */  { USBH_MCAT_HUB,           USBH_MCAT_HUB_SM,        USBH_MCAT_HUB_URB,       0xFF                     },
/* USBH_MTYPE_MSD           */  { USBH_MCAT_MSC,           USBH_MCAT_MSC_API,       0xFF,                    0xFF                     },
/* USBH_MTYPE_MSD_INTERN    */  { USBH_MCAT_MSC_SM,        USBH_MCAT_MSC_SCSI,      0xFF,                    0xFF                     },
/* USBH_MTYPE_MSD_PHYS      */  { 0xFF,                    0xFF,                    0xFF,                    0xFF                     },
/* USBH_MTYPE_HID           */  { USBH_MCAT_HID,           USBH_MCAT_HID_URB,       USBH_MCAT_HID_RDESC,     0xFF                     },
/* USBH_MTYPE_PRINTER_CLASS */  { USBH_MCAT_PRINTER,       0xFF,                    0xFF,                    0xFF                     },
/* USBH_MTYPE_CDC           */  { USBH_MCAT_CDC,           0xFF,                    0xFF,                    0xFF                     },
/* USBH_MTYPE_FT232         */  { USBH_MCAT_FT232,         0xFF,                    0xFF,                    0xFF                     },
/* USBH_MTYPE_APPLICATION   */  { USBH_MCAT_APPLICATION,   0xFF,                    0xFF,                    0xFF                     },
/* USBH_MTYPE_UBD           */  { USBH_MCAT_URB,           0xFF,                    0xFF,                    0xFF                     },
/* USBH_MTYPE_MTP           */  { USBH_MCAT_MTP,           0xFF,                    0xFF,                    0xFF                     },
/* unused                   */  { 0xFF,                    0xFF,                    0xFF,                    0xFF                     },
/* USBH_MTYPE_BULK          */  { USBH_MCAT_BULK,          0xFF,                    0xFF,                    0xFF                     },
/* USBH_MTYPE_LAN           */  { 0xFF,                    0xFF,                    0xFF,                    0xFF                     },
/* USBH_MTYPE_CCID          */  { USBH_MCAT_CCID,          0xFF,                    0xFF,                    0xFF                     },
/* USBH_MTYPE_AUDIO         */  { USBH_MCAT_AUDIO,         0xFF,                    0xFF,                    0xFF                     },
/* USBH_MTYPE_MIDI          */  { USBH_MCAT_MIDI,          0xFF,                    0xFF,                    0xFF                     },
/* USBH_MTYPE_CP210X        */  { USBH_MCAT_CP210X,        0xFF,                    0xFF,                    0xFF                     },
/* USBH_MTYPE_WLAN          */  { 0xFF,                    0xFF,                    0xFF,                    0xFF                     }
};
#endif

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/

static U32 _MsgFilter[2][(USBH_MCAT_MAX + 31u) / 32u] = {
  { (1uL << USBH_MCAT_INIT) | (1uL << USBH_MCAT_APPLICATION), 0 },    // Default log messages
  { 0xFFFFFFFFu, 0xFFFFFFFFu }                                        // Default warning messages (all)
};

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _LogV
*
*  Function description
*    Displays log information depending on the enabled message types.
*/
static void _LogV(int Warn, U32 Type, const char *sFormat, va_list ParamList) {
  const struct MCAT_STRINGS_t *pStrings;
  unsigned Len;
  char     ac[USBH_LOG_BUFFER_SIZE];
  //
  // Filter message. If logging for this type of message is not enabled, do  nothing.
  //
  if (Type >= USBH_MCAT_MAX || (_MsgFilter[Warn][Type / 32u] & (1uL << (Type % 32u))) == 0u) {
    return;
  }
  pStrings = _aMCat2String;
  Type &= 0xFFuL;
  while (Type < pStrings->From || Type > pStrings->To) {
    pStrings++;
  }
  Len = USBH_STRLEN(pStrings->Text);
  USBH_MEMCPY(ac, pStrings->Text, Len);
  ac[Len] = ':';
  ac[Len + 1u] = ' ';
  //
  // Replace place holders (%d, %x etc) by values and call output routine.
  //
  (void)SEGGER_vsnprintf(&ac[Len + 2u], sizeof(ac) - Len - 2u, sFormat, ParamList);
  if (Warn != 0) {
    USBH_Warn(ac);
  } else {
    USBH_Log(ac);
  }
}

/*********************************************************************
*
*       _MapMsgFilter
*
*  Function description
*    Sets new message filter from legacy Set/AddLog/WarnFilter() functions.
*/
#if USBH_DEBUG > 1
static void _MapMsgFilter(unsigned Mode, U32 Type) {
  unsigned Index;

  for (Index = 0; Index < SEGGER_COUNTOF(_aMType2MCategory); Index++) {
    if ((Type & 1u) != 0u) {
      USBH_ConfigMsgFilter(Mode, sizeof(_aMType2MCategory[0]), _aMType2MCategory[Index]);
    }
    Type >>= 1;
  }
}
#endif

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       USBH_ConfigMsgFilter
*
*  Function description
*    Sets a mask that defines which logging or warning message should be logged.
*    Logging messages are only available in debug builds of emUSB-Host.
*
*  Parameters
*    Mode:          Mode to configure message filter:
*                   * USBH_LOG_FILTER_SET:      Set message categories in log filter.
*                   * USBH_LOG_FILTER_SET_ALL:  Enable all log messages (parameter pCategories is ignored).
*                   * USBH_LOG_FILTER_ADD:      Add message categories to log filter.
*                   * USBH_LOG_FILTER_CLR:      Clear message categories in log filter.
*                   * USBH_WARN_FILTER_SET:     Set message categories in warning filter.
*                   * USBH_WARN_FILTER_SET_ALL: Enable all warning messages (parameter pCategories is ignored).
*                   * USBH_WARN_FILTER_ADD:     Add message categories to warning filter.
*                   * USBH_WARN_FILTER_CLR:     Clear message categories in warning filter.
*    NumCategories: Number of messages categories contained in the array pCategories.
*    pCategories:   Pointer to array of NumCategories messages categories that should be configured.
*
*  Additional information
*    Should be called from USBH_X_Config().
*    By default, the log message category USBH_MCAT_INIT and
*    all warning messages are enabled.
*
*    Please note that the more logging is enabled, the more the timing
*    of the application is influenced.
*    For available message types see the USBH_MCAT_... definitions in USBH.h.
*
*    Please note that enabling all log messages is not
*    necessary, nor is it advised as it will influence the timing greatly.
*/
void USBH_ConfigMsgFilter(unsigned Mode, unsigned NumCategories, const U8 * pCategories) {
#if USBH_DEBUG > 1
  U32    * pFilter;
  unsigned Category;

  if ((Mode & USBH_WARN_FILTER_FLAG) != 0u) {
    pFilter = _MsgFilter[1];
  } else {
    pFilter = _MsgFilter[0];
  }
  switch (Mode & ~USBH_WARN_FILTER_FLAG) {
  case USBH_LOG_FILTER_SET:
    USBH_MEMSET(pFilter, 0, sizeof(_MsgFilter[0]));
    //lint -fallthrough
  case USBH_LOG_FILTER_ADD:
    while (NumCategories-- != 0u) {
      Category = *pCategories++;
      if (Category < USBH_MCAT_MAX) {
        pFilter[Category / 32u] |= (1u << (Category % 32u));
      }
    }
    break;
  case USBH_LOG_FILTER_SET_ALL:
    USBH_MEMSET(pFilter, 0xFFu, sizeof(_MsgFilter[0]));
    //lint -fallthrough
  case USBH_LOG_FILTER_CLR:
    while (NumCategories-- != 0u) {
      Category = *pCategories++;
      if (Category < USBH_MCAT_MAX) {
        pFilter[Category / 32u] &= ~(1u << (Category % 32u));
      }
    }
    break;
  }
#else
  USBH_USE_PARA(Mode);
  USBH_USE_PARA(NumCategories);
  USBH_USE_PARA(pCategories);
#endif
}

/*********************************************************************
*
*       USBH_SetLogFilter
*
*  Function description
*    Sets a mask that defines which logging message should be logged.
*    Logging messages are only available in debug builds of emUSB-Host.
*
*  Parameters
*    FilterMask : Specifies which logging messages should be displayed.
*
*  Additional information
*    Should be called from USBH_X_Config(). By default, the filter
*    condition USBH_MTYPE_INIT is set.
*
*    Please note that the more logging is enabled, the more the timing
*    of the application is influenced.
*    For available message types see chapter Message types.
*
*    Please note that enabling all log messages (0xffffffff) is not
*    necessary, nor is it advised as it will influence the timing greatly.
*/
void USBH_SetLogFilter(U32 FilterMask) {
#if USBH_DEBUG > 1
  USBH_ConfigMsgFilter(USBH_LOG_FILTER_SET, 0, NULL);
  _MapMsgFilter(USBH_LOG_FILTER_ADD, FilterMask);
#else
  USBH_USE_PARA(FilterMask);
#endif
}

/*********************************************************************
*
*       USBH_AddLogFilter
*
*  Function description
*    Adds an additional filter condition to the mask which specifies
*    the logging messages that should be displayed.
*
*  Parameters
*    FilterMask : Specifies which logging messages should be added to the filter mask.
*
*  Additional information
*    This function can also be used to remove a filter condition which was set before.
*    It adds/removes the specified filter to/from the filter mask via a disjunction.
*    For available message types see chapter Message types.
*    Please note that enabling all log messages (0xffffffff) is not necessary,
*    nor is it advised as it will influence the timing greatly.
*/
void USBH_AddLogFilter(U32 FilterMask) {
#if USBH_DEBUG > 1
  _MapMsgFilter(USBH_LOG_FILTER_ADD, FilterMask);
#else
  USBH_USE_PARA(FilterMask);
#endif
}

/*********************************************************************
*
*       USBH_SetWarnFilter
*
*  Function description
*    Adds an additional filter condition to the mask which specifies
*    the warning messages that should be displayed.
*
*  Parameters
*    FilterMask : Specifies which warning messages should be added to the filter mask.
*
*  Additional information
*    This function can also be used to remove a filter condition which
*    was set before. It adds/removes the specified filter to/from the
*    filter mask via a disjunction.
*    For available message types see chapter Message types.
*/
void USBH_SetWarnFilter(U32 FilterMask) {
#if USBH_DEBUG > 1
  USBH_ConfigMsgFilter(USBH_WARN_FILTER_SET, 0, NULL);
  _MapMsgFilter(USBH_WARN_FILTER_ADD, FilterMask);
#else
  USBH_USE_PARA(FilterMask);
#endif
}

/*********************************************************************
*
*       USBH_AddWarnFilter
*
*  Function description
*    Adds an additional filter condition to the mask which specifies
*    the warning messages that should be displayed.
*
*  Parameters
*    FilterMask : Specifies which warning messages should be added to the filter mask.
*
*  Additional information
*    This function can also be used to remove a filter condition which
*    was set before. It adds/removes the specified filter to/from the
*    filter mask via a disjunction.
*    For available message types see chapter Message types.
*/
void USBH_AddWarnFilter(U32 FilterMask) {
#if USBH_DEBUG > 1
  _MapMsgFilter(USBH_WARN_FILTER_ADD, FilterMask);
#else
  USBH_USE_PARA(FilterMask);
#endif
}

/*********************************************************************
*
*       USBH_Logf
*
*  Function description
*    Displays log information depending on the enabled message types.
*
*  Parameters
*    Type     : Message type to log.
*    sFormat  : Message string with optional format specifiers.
*/
void USBH_Logf(U32 Type, const char * sFormat, ...) {
  //lint --e{530,586) dealing with 'va_start' and 'ParamList'  N:102
  va_list ParamList;
  va_start(ParamList, sFormat);
  _LogV(0, Type, sFormat, ParamList);
  va_end(ParamList);
}

/*********************************************************************
*
*       USBH_Logf_Application
*
*  Function description
*    Displays application log information.
*
*  Parameters
*    sFormat  : Message string with optional format specifiers.
*/
void USBH_Logf_Application(const char * sFormat, ...) {
  //lint --e{530,586) dealing with 'va_start' and 'ParamList'  N:102
  va_list ParamList;
  va_start(ParamList, sFormat);
  _LogV(0, USBH_MCAT_APPLICATION, sFormat, ParamList);
  va_end(ParamList);
}

/*********************************************************************
*
*       USBH_Warnf
*
*  Function description
*    Displays warning information depending on the enabled message types.
*
*  Parameters
*    Type     : Message type to log.
*    sFormat  : Message string with optional format specifiers.
*/
void USBH_Warnf(U32 Type, const char * sFormat, ...) {
  //lint --e{530,586) dealing with 'va_start' and 'ParamList'  N:102
  va_list ParamList;
  va_start(ParamList, sFormat);
  _LogV(1, Type, sFormat, ParamList);
  va_end(ParamList);
}

/*********************************************************************
*
*       USBH_Warnf_Application
*
*  Function description
*    Displays application warning information.
*
*  Parameters
*    sFormat  : Message string with optional format specifiers.
*/
void USBH_Warnf_Application(const char * sFormat, ...) {
  //lint --e{530,586) dealing with 'va_start' and 'ParamList'  N:102
  va_list ParamList;
  va_start(ParamList, sFormat);
  _LogV(1, USBH_MCAT_APPLICATION, sFormat, ParamList);
  va_end(ParamList);
}

/*********************************************************************
*
*       USBH_sprintf_Application
*
*  Function description
*    A simple sprintf replacement.
*
*  Parameters
*    pBuffer     : Pointer to a user provided buffer.
*    BufferSize  : Size of the buffer in bytes.
*    sFormat     : Message string with optional format specifiers.
*/
void USBH_sprintf_Application(char * pBuffer, unsigned BufferSize, const char * sFormat, ...) {
  //lint --e{530,586) dealing with 'va_start' and 'ParamList'  N:102
  va_list ParamList;
  //
  // Replace place holders (%d, %x etc) by values and call output routine.
  //
  va_start(ParamList, sFormat);
  (void)SEGGER_vsnprintf(pBuffer, BufferSize, sFormat, ParamList);
  va_end(ParamList);
}

/*************************** End of file ****************************/
