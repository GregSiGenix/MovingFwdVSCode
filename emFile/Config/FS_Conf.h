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

File    : FS_Conf.h
Purpose : Simple configuration for file system
*/

#ifndef FS_CONF_H
#define FS_CONF_H     // Avoid multiple inclusion

#define FS_DEBUG_LEVEL      1     // 0: Smallest code, 5: Full debug. See chapter 10 "Debugging" of the emFile manual.
#define FS_OS_LOCKING       1     // 0: No locking, 1: API locking, 2: Driver locking. See chapter 9 "OS integration" of the emFile manual.
                                  // The application has to provide an OS layer. Sample OS layers are provided in the
                                  // "Sample\FS\OS" folder of the emFile shipment.

#endif                // Avoid multiple inclusion

/*************************** End of file ****************************/
