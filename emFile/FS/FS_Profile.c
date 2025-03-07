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
File        : FS_Profile.c
Purpose     : Profiling instrumentation.
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

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_PROFILE_GetAPIDesc
*
*  Function description
*    Retrieves the description string of the profiling API and the
*    number of API functions available.
*
*  Parameters
*    psDesc     Pointer to the pointer of the description string. Can be NULL.
*
*  Return value
*    Number of API functions.
*
*  Additional information
*    The psDesc string has the following format:
*      "M=emFile,<ID> <Name> <ParameterFormat>"
*    Example: "M=emFile,0 FS_FOpen pFileName=%s pMode=%s,1 FS_Write pFile=%u pData=%u NumBytes=%u"
*/
U32 FS_PROFILE_GetAPIDesc(const char ** psDesc) {
  if (psDesc != NULL) {
    *psDesc = "M=emFile,V=1";
  }
  return FS_NUM_EVTIDS;
}

/*********************************************************************
*
*       FS_PROFILE_SetAPI
*
*  Function description
*    Configures the profiling API functions to be used.
*
*  Parameters
*    pAPI         Pointer to trace API structure.
*    IdOffset     Offset to be added to all generated event ids.
*/
void FS_PROFILE_SetAPI(const FS_PROFILE_API * pAPI, U32 IdOffset) {
  FS_Global.Profile.pAPI      = pAPI;
  FS_Global.Profile.IdOffset = IdOffset;
}

#else   // Avoid empty module.

/*********************************************************************
*
*       FS_PROFILE_c
*
*  Function description
*    Dummy function to prevent compiler errors.
*/
void FS_PROFILE_c(void);
void FS_PROFILE_c(void) {}

#endif  // FS_SUPPORT_PROFILE

/*************************** End of file ****************************/
