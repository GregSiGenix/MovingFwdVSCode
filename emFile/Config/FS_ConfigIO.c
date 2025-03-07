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
-------------------------- END-OF-HEADER -----------------------------

File    : FS_ConfigIO.c
Purpose : I/O configuration routines for file system
*/

/*********************************************************************
*
*       #include section
*
**********************************************************************
*/
#include <stdio.h>
#include "FS.h"
#include "stm32h7xx_hal.h"


#if defined (__CROSSWORKS_ARM)
  #include "__putchar.h"
#endif

/*********************************************************************
*
*       Defines, configurable
*
**********************************************************************
*/
#ifndef   USE_RTT
  #define USE_RTT             0   // I/O via SEGGER's Real Time Transfer: https://www.segger.com/jlink-real-time-terminal.html
#endif

#ifndef   USE_SYSTEMVIEW
  #define USE_SYSTEMVIEW      0   // I/O via SEGGER's System View: https://www.segger.com/systemview.html
#endif

#ifndef   USE_DCC
  #define USE_DCC             0   // I/O via SEGGER's J-Link DCC.
#endif

#ifndef   USE_EMBOS_VIEW
  #define USE_EMBOS_VIEW      0   // I/O via SEGGER's embOSView PC tool.
#endif

#ifndef   USE_USART6
  #define USE_USART6           1   // I/O via USART6
#endif

/*********************************************************************
*
*       #include section, conditional
*/
#if USE_RTT
  #include "SEGGER_RTT.h"
#elif USE_SYSTEMVIEW
  #include "SEGGER_SYSVIEW.h"
#elif USE_DCC
  #include "JLINKDCC.h"
#elif USE_EMBOS_VIEW
  #include "RTOS.h"
#endif

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _puts
*
*  Function description
*    Sends a text message to host system.
*
*  Parameters
*    s    Text message to be sent (0-terminated).
*
*  Additional information
*    This function is a local (static) replacement for puts().
*    The reason why puts() is not used is that it always appends a
*    newline character, which conflicts with the formatting of debug
*    messages.
*/
static void _puts(const char * s) {
#if USE_EMBOS_VIEW
  OS_SendString(s);
#elif USE_RTT
  SEGGER_RTT_WriteString(0, s);
#elif USE_SYSTEMVIEW
  SEGGER_SYSVIEW_Print(s);

#else
  for (;;) {
    char c;

    c = *s++;
    if (c == 0) {
      break;
    }
#if USE_DCC
    JLINKDCC_SendChar(c);
#else
    putchar(c);
#endif
  }
#endif
}

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_X_Panic
*
*  Function description
*    Handles an assertion error.
*
*  Parameters
*    ErrorCode      Identifies the type of error.
*
*  Additional information
*    This function is called by the file system only when the sources
*    are built with FS_DEBUG_LEVEL set to a value greater than or equal
*    to FS_DEBUG_LEVEL_CHECK_PARA. FS_X_Panic() is called only on a
*    critical error condition such as insufficient memory or invalid
*    parameter.
*
*    Refer to \ref{Error codes} for permitted values for ErrorCode.
*/
void FS_X_Panic(int ErrorCode) {
  char               ac[20];
  SEGGER_BUFFER_DESC BufferDesc;

  BufferDesc.pBuffer    = ac;
  BufferDesc.BufferSize = sizeof(ac);
  BufferDesc.Cnt        = 0;
  SEGGER_PrintInt(&BufferDesc, ErrorCode, 10, 0);
  _puts("FS panic: ");
  _puts(ac);
  _puts("\n");
  while (1) {
    ;
  }
}

/*********************************************************************
*
*       FS_X_Log
*
*  Function description
*    API function called when a trace debug message is generated.
*
*  Parameters
*    s    Generated text debug message (0-terminated). It can never be NULL.
*
*  Additional information
*    This function is optional. It is required to be present only when
*    the file system is built with FS_DEBUG_LEVEL set to a value greater
*    than or equal to FS_DEBUG_LEVEL_LOG_ALL. The generated text message
*    terminated by a newline character as required.
*/
void FS_X_Log(const char * s) {
  _puts(s);
}

/*********************************************************************
*
*       FS_X_Warn
*
*  Function description
*    API function called to report a warning debug message.
*
*  Parameters
*    s    Reported text debug message (0-terminated). It can never be NULL.
*
*  Additional information
*    This function is optional. It is required to be present only when
*    the file system is built with FS_DEBUG_LEVEL set to a value greater
*    than or equal to FS_DEBUG_LEVEL_LOG_WARNINGS. The generated text message
*    is not terminated by a newline character.
*/
void FS_X_Warn(const char * s) {
  _puts("FS warning: ");
  _puts(s);
  _puts("\n");
}

/*********************************************************************
*
*       FS_X_ErrorOut
*
*  Function description
*    API function called to report an error debug message.
*
*  Parameters
*    s    Reported text debug message (0-terminated). It can never be NULL.
*
*  Additional information
*    This function is optional. It is required to be present only when
*    the file system is built with FS_DEBUG_LEVEL set to a value greater
*    than or equal to FS_DEBUG_LEVEL_LOG_ERRORS. The generated text message
*    is not terminated by a newline character.
*/
void FS_X_ErrorOut(const char * s) {
  _puts("FS error: ");
  _puts(s);
  _puts("\n");
}

/*************************** End of file ****************************/
