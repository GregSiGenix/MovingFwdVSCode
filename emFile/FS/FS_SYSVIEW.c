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
File        : FS_SYSVIEW.c
Purpose     : Profiling instrumentation via SystemView.
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#include "FS_Int.h"

#if FS_SUPPORT_PROFILE

#include "SEGGER_SYSVIEW.h"

/*********************************************************************
*
*       Defines, fixed
*
**********************************************************************
*/
#define MAX_NUM_CHARS     32

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static SEGGER_SYSVIEW_MODULE _FS_SYSVIEW_Module = {
  NULL,                   // sModule ("M=emFile...")
  0,                      // NumEvents
  0,                      // EventOffset
  NULL,                   // pfSendModuleDesc
  NULL                    // pNext
};

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _cbRecordString
*
*  Function description
*    Records an emFile event with 2 string parameters.
*/
static void _cbRecordString(unsigned EventId, const char * pPara0) {
  U8   aPacket[SEGGER_SYSVIEW_INFO_SIZE + (MAX_NUM_CHARS + 1)];
  U8 * pPayload;

  pPayload = SEGGER_SYSVIEW_PREPARE_PACKET(aPacket);                        // Prepare the packet for SysView.
  pPayload = SEGGER_SYSVIEW_EncodeString(pPayload, pPara0, MAX_NUM_CHARS);  // Add the string to the packet.
  (void)SEGGER_SYSVIEW_SendPacket(&aPacket[0], pPayload, EventId);          // Send the packet.
}

/*********************************************************************
*
*       _cbRecordStringx2
*
*  Function description
*    Records an emFile event with 2 string parameters.
*/
static void _cbRecordStringx2(unsigned EventId, const char * pPara0, const char * pPara1) {
  U8   aPacket[SEGGER_SYSVIEW_INFO_SIZE + 2 * (MAX_NUM_CHARS + 1)];
  U8 * pPayload;

  pPayload = SEGGER_SYSVIEW_PREPARE_PACKET(aPacket);                        // Prepare the packet for SysView.
  pPayload = SEGGER_SYSVIEW_EncodeString(pPayload, pPara0, MAX_NUM_CHARS);  // Add the first string to the packet.
  pPayload = SEGGER_SYSVIEW_EncodeString(pPayload, pPara1, MAX_NUM_CHARS);  // Add the second string to the packet.
  (void)SEGGER_SYSVIEW_SendPacket(&aPacket[0], pPayload, EventId);          // Send the packet.
}

static const FS_PROFILE_API _FS_SYSVIEW_ProfileAPI = {
  SEGGER_SYSVIEW_RecordEndCall,
  SEGGER_SYSVIEW_RecordEndCallU32,
  SEGGER_SYSVIEW_RecordVoid,
  SEGGER_SYSVIEW_RecordU32,
  SEGGER_SYSVIEW_RecordU32x2,
  SEGGER_SYSVIEW_RecordU32x3,
  SEGGER_SYSVIEW_RecordU32x4,
  SEGGER_SYSVIEW_RecordU32x5,
  SEGGER_SYSVIEW_RecordU32x6,
  SEGGER_SYSVIEW_RecordU32x7,
  _cbRecordString,
  _cbRecordStringx2
};

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_SYSVIEW_Init
*
*  Function description
*    Configures emFile instrumentation. Registers emFile with SystemView
*    and sets the API functions for profiling.
*/
void FS_SYSVIEW_Init(void) {
  //
  // Register emFile with SystemView.
  // SystemView has to be initialized before.
  //
  _FS_SYSVIEW_Module.NumEvents = FS_PROFILE_GetAPIDesc(&_FS_SYSVIEW_Module.sModule);
  SEGGER_SYSVIEW_RegisterModule(&_FS_SYSVIEW_Module);
  FS_PROFILE_SetAPI(&_FS_SYSVIEW_ProfileAPI, _FS_SYSVIEW_Module.EventOffset);
}

#else   // Avoid empty module.

/*********************************************************************
*
*       FS_SYSVIEW_c
*
*  Function description
*    Dummy function to prevent compiler errors.
*/
void FS_SYSVIEW_c(void);
void FS_SYSVIEW_c(void) {}

#endif  // FS_SUPPORT_PROFILE

/*************************** End of file ****************************/
