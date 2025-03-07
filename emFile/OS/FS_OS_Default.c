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

File    : FS_OS_Default.c
Purpose : Default runtime configurable OS layer for emFile.
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#include "FS.h"
#include "FS_OS.h"

/*********************************************************************
*
*       Public data
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_OS_Default
*/
const FS_OS_TYPE FS_OS_Default = {
  FS_X_OS_Lock,
  FS_X_OS_Unlock,
  FS_X_OS_Init,
#if FS_SUPPORT_DEINIT
  FS_X_OS_DeInit,
#else
  NULL,
#endif // FS_SUPPORT_DEINIT
  FS_X_OS_GetTime,
  FS_X_OS_Delay,
  FS_X_OS_Wait,
  FS_X_OS_Signal
};

/*************************** End of file ****************************/
