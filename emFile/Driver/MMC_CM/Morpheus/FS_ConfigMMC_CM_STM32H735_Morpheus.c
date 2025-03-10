/*********************************************************************
*                   (c) SEGGER Microcontroller GmbH                  *
*                        The Embedded Experts                        *
*                           www.segger.com                           *
**********************************************************************

----------------------------------------------------------------------
Licensing information

Licensor:                 SEGGER Microcontroller Systems LLC
Licensed to:              React Health Inc., 203 Avenue A NW, Suite 300, Winter Haven FL 33881, USA
Licensed SEGGER software: emFile
License number:           FS-00855
License model:            SSL [Single Developer Single Platform Source Code License]
Licensed product:         -
Licensed platform:        STM32F4, IAR
Licensed number of seats: 1
-------------------------- END-OF-HEADER -----------------------------

File    : FS_ConfigMMC_CM_STM32H735_Morpheus.c
Purpose : Configuration functions for FS with MMC/SD card mode driver
*/

/*********************************************************************
*
*       #include section
*
**********************************************************************
*/
#include "FS.h"
#include "FS_MMC_HW_CM_STM32H735_Morpheus.h"

/*********************************************************************
*
*       Defines, configurable
*
**********************************************************************
*/
#ifndef   ALLOC_SIZE
  #define ALLOC_SIZE                  0x2500      // Size defined in bytes.
#endif

#ifndef   ALLOW_4BIT_MODE
  #define ALLOW_4BIT_MODE             1           // Selects the number of data lines.
#endif

#ifndef   ALLOW_SPEED_MODE_HIGH
  #define ALLOW_SPEED_MODE_HIGH       1           // Selects the communication speed.
#endif

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/

/*********************************************************************
*
*       Memory pool used for semi-dynamic allocation
*/
#ifdef __ICCARM__
  #pragma location="FS_RAM"
  static __no_init U32 _aMemBlock[ALLOC_SIZE / 4];
#endif
#ifdef __CC_ARM
  static U32 _aMemBlock[ALLOC_SIZE / 4] __attribute__ ((section ("FS_RAM"), zero_init));
#endif
#if (!defined(__ICCARM__) && !defined(__CC_ARM))
  static U32 _aMemBlock[ALLOC_SIZE / 4];
#endif

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_X_AddDevices
*
*  Function description
*    This function is called by the FS during FS_Init().
*    It is supposed to add all devices, using primarily FS_AddDevice().
*
*  Note
*    (1) Other API functions may NOT be called, since this function is called
*        during initialization. The devices are not yet ready at this point.
*/
void FS_X_AddDevices(void) {
  //
  // Give file system memory to work with.
  //
  FS_AssignMemory(&_aMemBlock[0], sizeof(_aMemBlock));
  //
  // Add and configure the driver.
  //
  FS_AddDevice(&FS_MMC_CM_Driver);
  FS_MMC_CM_Allow4bitMode(0, ALLOW_4BIT_MODE);
  FS_MMC_CM_AllowHighSpeedMode(0, ALLOW_SPEED_MODE_HIGH);
  FS_MMC_CM_SetHWType(0, &FS_MMC_HW_CM_STM32H735_Morpheus);
  //
  // Configure the file system for fast write operations.
  //
#if FS_SUPPORT_FILE_BUFFER
  FS_ConfigFileBufferDefault(512, FS_FILE_BUFFER_WRITE);
#endif
  FS_SetFileWriteMode(FS_WRITEMODE_FAST);
}

/*********************************************************************
*
*       FS_X_GetTimeDate
*
*  Function description
*    Current time and date in a format suitable for the file system.
*
*  Additional information
*    Bit 0-4:   2-second count (0-29)
*    Bit 5-10:  Minutes (0-59)
*    Bit 11-15: Hours (0-23)
*    Bit 16-20: Day of month (1-31)
*    Bit 21-24: Month of year (1-12)
*    Bit 25-31: Count of years from 1980 (0-127)
*/
U32 FS_X_GetTimeDate(void) {
  U32 r;
  U16 Sec, Min, Hour;
  U16 Day, Month, Year;

  Sec   = 0;        // 0 based.  Valid range: 0..59
  Min   = 0;        // 0 based.  Valid range: 0..59
  Hour  = 0;        // 0 based.  Valid range: 0..23
  Day   = 1;        // 1 based.    Means that 1 is 1. Valid range is 1..31 (depending on month)
  Month = 1;        // 1 based.    Means that January is 1. Valid range is 1..12.
  Year  = 0;        // 1980 based. Means that 2007 would be 27.
  r   = Sec / 2 + (Min << 5) + (Hour  << 11);
  r  |= (U32)(Day + (Month << 5) + (Year  << 9)) << 16;
  return r;
}

/*************************** End of file ****************************/
