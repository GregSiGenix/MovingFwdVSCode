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

File    : FS_ConfigNOR_BM_SPIFI_STM32H735_Morpheus.c
Purpose : Configuration file for serial NOR flash connected via SPI.
*/

/*********************************************************************
*
*       #include section
*
**********************************************************************
*/
#include "FS.h"
#include "FS_NOR_HW_SPI_STM32H735_Morpheus.h"

/*********************************************************************
*
*       Defines, configurable
*
**********************************************************************
*/
#ifndef   ALLOC_SIZE
  #define ALLOC_SIZE          0x2000          // Size of memory dedicated to the file system. This value should be fine tuned according for your system.
#endif

#ifndef   NOR_BASE_ADDR
  #define NOR_BASE_ADDR       0x00000000      // Base address of the NOR flash device to be used as storage.
#endif

#ifndef   NOR_START_ADDR
  #define NOR_START_ADDR      0x00000000      // Start address of the first sector be used as storage. If the entire chip is used for file system, it is identical to the base address.
#endif

#ifndef   NOR_SIZE
  #define NOR_SIZE            0x00800000      // Number of bytes to be used for storage
#endif

#ifndef   LOG_SECTOR_SIZE
  #define LOG_SECTOR_SIZE     512             // Logical sector size
#endif

/*********************************************************************
*
*       Static const data
*
**********************************************************************
*/

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/

/*********************************************************************
*
*       Memory pool used for semi-dynamic allocation.
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
  // Configure the size of the logical sector and activate the file buffering.
  //
  FS_SetMaxSectorSize(LOG_SECTOR_SIZE);
#if FS_SUPPORT_FILE_BUFFER
  FS_ConfigFileBufferDefault(LOG_SECTOR_SIZE, FS_FILE_BUFFER_WRITE);
#endif
  //
  // Add and configure the NOR driver.
  //
  FS_AddDevice(&FS_NOR_BM_Driver);
  FS_NOR_BM_SetPhyType(0, &FS_NOR_PHY_SFDP);
  FS_NOR_BM_Configure(0, NOR_BASE_ADDR, NOR_START_ADDR, NOR_SIZE);
  FS_NOR_BM_SetSectorSize(0, LOG_SECTOR_SIZE);
#if FS_NOR_VERIFY_ERASE
  FS_NOR_BM_SetEraseVerification(0, 0);
#endif // FS_NOR_VERIFY_ERASE
#if FS_NOR_VERIFY_WRITE
  FS_NOR_BM_SetWriteVerification(0, 0);
#endif // FS_NOR_VERIFY_WRITE
  //
  // Configure the NOR physical layer.
  //
  FS_NOR_SFDP_SetHWType(0, &FS_NOR_HW_SPI_STM32H735_Morpheus);
  FS_NOR_SFDP_SetDeviceList(0, &FS_NOR_SPI_DeviceListWinbond);
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
