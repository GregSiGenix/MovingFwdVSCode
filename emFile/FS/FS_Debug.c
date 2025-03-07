/*********************************************************************
*                     SEGGER Microcontroller GmbH                    *
*                        The Embedded Experts                        *
**********************************************************************
*                                                                    *
*       (c) 2003 - 2022  SEGGER Microcontroller GmbH                 *
*                                                                    *
*       www.segger.com     Support: support_emfile@segger.com        *
*                                                                    *
**********************************************************************
*                                                                    *
*       emFile * File system for embedded applications               *
*                                                                    *
*                                                                    *
*       Please note:                                                 *
*                                                                    *
*       Knowledge of this file may under no circumstances            *
*       be used to write a similar product for in-house use.         *
*                                                                    *
*       Thank you for your fairness !                                *
*                                                                    *
**********************************************************************
*                                                                    *
*       emFile version: V5.20.0                                      *
*                                                                    *
**********************************************************************
----------------------------------------------------------------------
Licensing information
Licensor:                 SEGGER Microcontroller Systems LLC
Licensed to:              React Health Inc, 203 Avenue A NW, Suite 300, Winter Haven FL 33881, USA
Licensed SEGGER software: emFile
License number:           FS-00855
License model:            SSL [Single Developer Single Platform Source Code License]
Licensed product:         -
Licensed platform:        STM32F4, IAR
Licensed number of seats: 1
----------------------------------------------------------------------
Support and Update Agreement (SUA)
SUA period:               2022-05-19 - 2022-11-19
Contact to extend SUA:    sales@segger.com
----------------------------------------------------------------------
File        : FS_Debug.c
Purpose     : Functions related to debugging.
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#include "FS_Int.h"

/*********************************************************************
*
*       Static const data
*
**********************************************************************
*/
static const char _acHex[16] =  {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS)
  static U32  _ErrorFilter = 0xFFFFFFFFuL;              // By default all error messages enabled.
#if FS_DEBUG_STATIC_MESSAGE_BUFFER
  static char _acBuffer[FS_DEBUG_MAX_LEN_MESSAGE];      // Static buffer used for the formatting of debug messages. By default the message buffer is allocated on the stack.
#endif // FS_DEBUG_STATIC_MESSAGE_BUFFER
#endif // (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS)
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_WARNINGS)
  static U32 _WarnFilter  = 0xFFFFFFFFu;                // By default all warning messages are enabled.
#endif // (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_WARNINGS)
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ALL)
  static U32 _LogFilter   = (U32)FS_LOG_MASK_DEFAULT;
#endif /// (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ALL)

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _AddHex
*/
static void _AddHex(U32 v, U8 Len, char ** ps) {
  char *s = *ps;

  if (Len > 8u) {
    return;
  }
  (*ps) += Len;
  **ps   = '\0';     // Make sure string is 0-terminated.
  while(Len-- != 0u) {
    *(s + Len) = _acHex[v & 15u];
    v >>= 4;
  }
}

/*********************************************************************
*
*       Public code (internal)
*
**********************************************************************
*/

/*********************************************************************
*
*       FS__AddSpaceHex
*/
void FS__AddSpaceHex(U32 v, U8 Len, char ** ps) {
  char* s = *ps;
  *s++ = ' ';
  *ps = s;
  _AddHex(v, Len, ps);
}

/*********************************************************************
*
*       FS__AddEscapedHex
*/
void FS__AddEscapedHex(U32 v, U8 Len, char ** ps) {
  char* s = *ps;
  *s++ = '\\';
  *s++ = 'x';
  *ps = s;
  _AddHex(v, Len, ps);
}

#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS)

/*********************************************************************
*
*       FS_ErrorOutf
*/
void FS_ErrorOutf(U32 Type, const char * sFormat, ...) {
  va_list   ParamList;
  char    * pBuffer;
#if (FS_DEBUG_STATIC_MESSAGE_BUFFER == 0)
  char      acBuffer[FS_DEBUG_MAX_LEN_MESSAGE];
#endif // FS_DEBUG_STATIC_MESSAGE_BUFFER == 0

  //lint --e{438} -esym(530, ParamList) N:102.

  //
  // Filter message. If logging for this type of message is not enabled, do nothing.
  //
  if ((Type & _ErrorFilter) != 0u) {
#if (FS_DEBUG_STATIC_MESSAGE_BUFFER == 0)
    pBuffer = acBuffer;
#else
    pBuffer = _acBuffer;
#endif // FS_DEBUG_STATIC_MESSAGE_BUFFER == 0
    //
    // Replace place holders (%d, %x etc) by values and call output routine.
    //
    va_start(ParamList, sFormat);       //lint !e586 macro 'va_start' is deprecated N:102. Reason: this is the only way we handle a variable argument list.
    (void)FS_VSNPRINTF(pBuffer, FS_DEBUG_MAX_LEN_MESSAGE, sFormat, ParamList);
    va_end(ParamList);                  //lint !e586 macro 'va_end' is deprecated N:102. Reason: this is the only way we handle a variable argument list.
    FS_X_ErrorOut(pBuffer);
  }
}

/*********************************************************************
*
*       FS__SetErrorFilterNL
*
*  Function description
*    Enables and disables error debug messages (non-locking version).
*/
void FS__SetErrorFilterNL(U32 FilterMask) {
  _ErrorFilter = FilterMask;
}

/*********************************************************************
*
*       FS__GetErrorFilterNL
*
*  Function description
*    Queries activation status of error debug messages (non-locking version).
*/
U32 FS__GetErrorFilterNL(void) {
  U32 r;

  r = _ErrorFilter;
  return r;
}

#endif // (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS)

#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_WARNINGS)

/*********************************************************************
*
*       FS_Warnf
*/
void FS_Warnf(U32 Type, const char * sFormat, ...) {
  va_list   ParamList;
  char    * pBuffer;
#if (FS_DEBUG_STATIC_MESSAGE_BUFFER == 0)
  char      acBuffer[FS_DEBUG_MAX_LEN_MESSAGE];
#endif // FS_DEBUG_STATIC_MESSAGE_BUFFER == 0

  //lint --e{438} -esym(530, ParamList) N:102.

  //
  // Filter message. If logging for this type of message is not enabled, do  nothing.
  //
  if ((Type & _WarnFilter) != 0u) {
#if (FS_DEBUG_STATIC_MESSAGE_BUFFER == 0)
    pBuffer = acBuffer;
#else
    pBuffer = _acBuffer;
#endif // FS_DEBUG_STATIC_MESSAGE_BUFFER == 0
    //
    // Replace place holders (%d, %x etc) by values and call output routine.
    //
    va_start(ParamList, sFormat);   //lint !e586 macro 'va_start' is deprecated N:102. Reason: this is the only way we handle a variable argument list.
    (void)FS_VSNPRINTF(pBuffer, FS_DEBUG_MAX_LEN_MESSAGE, sFormat, ParamList);
    va_end(ParamList);              //lint !e586 macro 'va_end' is deprecated N:102. Reason: this is the only way we handle a variable argument list.
    FS_X_Warn(pBuffer);
  }
}

#endif // (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_WARNINGS)

#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ALL)

/*********************************************************************
*
*       FS_Logf
*/
void FS_Logf(U32 Type, const char * sFormat, ...) {
  va_list   ParamList;
  char    * pBuffer;
#if (FS_DEBUG_STATIC_MESSAGE_BUFFER == 0)
  char      acBuffer[FS_DEBUG_MAX_LEN_MESSAGE];
#endif // FS_DEBUG_STATIC_MESSAGE_BUFFER == 0

  //lint --e{438} -esym(530, ParamList) N:102.

  //
  // Filter message. If logging for this type of message is not enabled, do  nothing.
  //
  if ((Type & _LogFilter) != 0u) {
#if (FS_DEBUG_STATIC_MESSAGE_BUFFER == 0)
    pBuffer = acBuffer;
#else
    pBuffer = _acBuffer;
#endif // FS_DEBUG_STATIC_MESSAGE_BUFFER == 0
    //
    // Replace place holders (%d, %x etc) by values and call output routine.
    //
    va_start(ParamList, sFormat);       //lint !e586 macro 'va_start' is deprecated N:102. Reason: this is the only way we handle a variable argument list.
    (void)FS_VSNPRINTF(pBuffer, FS_DEBUG_MAX_LEN_MESSAGE, sFormat, ParamList);
    va_end(ParamList);                  //lint !e586 macro 'va_end' is deprecated N:102. Reason: this is the only way we handle a variable argument list.
    FS_X_Log(pBuffer);
  }
}

#endif // (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ALL)

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS)

/*********************************************************************
*
*       FS_SetErrorFilter
*
*  Function description
*    Enables and disables error debug messages.
*
*  Parameters
*    FilterMask   Specifies the message types.
*
*  Additional information
*    FS_AddErrorFilter() can be used to enable and disable a specified
*    set of debug message types of the error class. The debug message
*    types that have the bit set to 1 in FilterMask are enabled while
*    the other debug message types are disabled.
*
*    This function is optional and is available only when the file
*    system is built with FS_DEBUG_LEVEL set to a value equal or
*    greater than FS_DEBUG_LEVEL_LOG_ERRORS.
*
*    FileMask is specified by or-ing one or more message types described
*    at \ref{Debug message types}.
*/
void FS_SetErrorFilter(U32 FilterMask) {
  FS_LOCK();
  FS_LOCK_SYS();
  _ErrorFilter = FilterMask;
  FS_UNLOCK_SYS();
  FS_UNLOCK();
}

/*********************************************************************
*
*       FS_AddErrorFilter
*
*  Function description
*    Enables error debug messages.
*
*  Parameters
*    FilterMask   Specifies the message types to be enabled.
*
*  Additional information
*    FS_AddErrorFilter() can be used to enable a specified set of
*    debug message types of the error class.
*
*    This function is optional and is available only when the file
*    system is built with FS_DEBUG_LEVEL set to a value equal or
*    greater than FS_DEBUG_LEVEL_LOG_ERRORS.
*
*    FileMask is specified by or-ing one or more message types described
*    at \ref{Debug message types}.
*/
void FS_AddErrorFilter(U32 FilterMask) {
  FS_LOCK();
  FS_LOCK_SYS();
  _ErrorFilter |= FilterMask;
  FS_UNLOCK_SYS();
  FS_UNLOCK();
}

/*********************************************************************
*
*       FS_GetErrorFilter
*
*  Function description
*    Queries activation status of error debug messages.
*
*  Return value
*    Value indicating the activation status for all debug message
*    types of the error class.
*
*  Additional information
*    This function is optional and is available only when the file
*    system is built with FS_DEBUG_LEVEL set to a value equal or
*    greater than FS_DEBUG_LEVEL_LOG_ERRORS.
*
*    The return value is specified by or-ing one or more message types
*    described at \ref{Debug message types}.
*/
U32 FS_GetErrorFilter(void) {
  U32 r;

  FS_LOCK();
  FS_LOCK_SYS();
  r = _ErrorFilter;
  FS_UNLOCK_SYS();
  FS_UNLOCK();
  return r;
}

#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS

#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_WARNINGS)

/*********************************************************************
*
*       FS_SetWarnFilter
*
*  Function description
*    Enables and disables warning debug messages.
*
*  Parameters
*    FilterMask   Specifies the message types.
*
*  Additional information
*    FS_SetWarnFilter() can be used to enable and disable a specified
*    set of debug message types of the warning class. The debug message
*    types that have the bit set to 1 in FilterMask are enabled while
*    the other debug message types are disabled.
*
*    This function is optional and is available only when the file
*    system is built with FS_DEBUG_LEVEL set to a value equal or
*    greater than FS_DEBUG_LEVEL_LOG_WARNINGS.
*
*    FileMask is specified by or-ing one or more message types described
*    at \ref{Debug message types}.
*/
void FS_SetWarnFilter(U32 FilterMask) {
  FS_LOCK();
  FS_LOCK_SYS();
  _WarnFilter = FilterMask;
  FS_UNLOCK_SYS();
  FS_UNLOCK();
}

/*********************************************************************
*
*       FS_AddWarnFilter
*
*  Function description
*    Enables warning debug messages.
*
*  Parameters
*    FilterMask   Specifies the message types to be enabled.
*
*  Additional information
*    FS_AddWarnFilter() can be used to enable a specified set of
*    debug message types of the warning class.
*
*    This function is optional and is available only when the file
*    system is built with FS_DEBUG_LEVEL set to a value equal or
*    greater than FS_DEBUG_LEVEL_LOG_WARNINGS.
*
*    FileMask is specified by or-ing one or more message types described
*    at \ref{Debug message types}.
*/
void FS_AddWarnFilter(U32 FilterMask) {
  FS_LOCK();
  FS_LOCK_SYS();
  _WarnFilter |= FilterMask;
  FS_UNLOCK_SYS();
  FS_UNLOCK();
}

/*********************************************************************
*
*       FS_GetWarnFilter
*
*  Function description
*    Queries activation status of warning debug messages.
*
*  Return value
*    Value indicating the activation status for all debug message
*    types of the warning class.
*
*  Additional information
*    This function is optional and is available only when the file
*    system is built with FS_DEBUG_LEVEL set to a value equal or
*    greater than FS_DEBUG_LEVEL_LOG_WARNINGS.
*
*    The return value is specified by or-ing one or more message types
*    described at \ref{Debug message types}.
*/
U32 FS_GetWarnFilter(void) {
  U32 r;

  FS_LOCK();
  FS_LOCK_SYS();
  r = _WarnFilter;
  FS_UNLOCK_SYS();
  FS_UNLOCK();
  return r;
}

#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_WARNINGS

#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ALL)

/*********************************************************************
*
*       FS_SetLogFilter
*
*  Function description
*    Enables and disables trace debug messages.
*
*  Parameters
*    FilterMask   Specifies the message types.
*
*  Additional information
*    FS_SetLogFilter() can be used to enable and disable a specified
*    set of debug message types of the trace class. The debug message
*    types that have the bit set to 1 in FilterMask are enabled while
*    the other debug message types are disabled.
*
*    This function is optional and is available only when the file
*    system is built with FS_DEBUG_LEVEL set to a value equal or
*    greater than FS_DEBUG_LEVEL_LOG_ALL.
*
*    FileMask is specified by or-ing one or more message types described
*    at \ref{Debug message types}.
*/
void FS_SetLogFilter(U32 FilterMask) {
  FS_LOCK();
  FS_LOCK_SYS();
  _LogFilter = FilterMask;
  FS_UNLOCK_SYS();
  FS_UNLOCK();
}

/*********************************************************************
*
*       FS_AddLogFilter
*
*  Function description
*    Enables trace debug messages.
*
*  Parameters
*    FilterMask   Specifies the message types to be enabled.
*
*  Additional information
*    FS_AddLogFilter() can be used to enable a specified set of
*    debug message types of the trace class.
*
*    This function is optional and is available only when the file
*    system is built with FS_DEBUG_LEVEL set to a value equal or
*    greater than FS_DEBUG_LEVEL_LOG_ALL.
*
*    FileMask is specified by or-ing one or more message types
*    described at \ref{Debug message types}.
*/
void FS_AddLogFilter(U32 FilterMask) {
  FS_LOCK();
  FS_LOCK_SYS();
  _LogFilter |= FilterMask;
  FS_UNLOCK_SYS();
  FS_UNLOCK();
}

/*********************************************************************
*
*       FS_GetLogFilter
*
*  Function description
*    Queries activation status of trace debug messages.
*
*  Return value
*    Value indicating the activation status for all debug message
*    types of the trace class.
*
*  Additional information
*    This function is optional and is available only when the file
*    system is built with FS_DEBUG_LEVEL set to a value equal or
*    greater than FS_DEBUG_LEVEL_LOG_ALL.
*
*    The return value is specified by or-ing one or more message types
*    described at \ref{Debug message types}.
*/
U32 FS_GetLogFilter(void) {
  U32 r;

  FS_LOCK();
  FS_LOCK_SYS();
  r = _LogFilter;
  FS_UNLOCK_SYS();
  FS_UNLOCK();
  return r;
}

#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ALL

/*************************** End of file ****************************/
