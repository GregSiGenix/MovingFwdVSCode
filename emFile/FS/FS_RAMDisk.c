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
File        : FS_RAMDisk.c
Purpose     : Driver using RAM as storage.
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#include "FS_Int.h"
#ifdef _WIN32
  #include <windows.h>
#endif // _WIN32

/*********************************************************************
*
*        Local data types
*
**********************************************************************
*/
typedef struct {
  U8 FS_HUGE * pData;
  U32          NumSectors;
  U8           ldBytesPerSector;
} RAMDISK_INST;

/*********************************************************************
*
*        Static data
*
**********************************************************************
*/
static RAMDISK_INST _aInst[FS_RAMDISK_NUM_UNITS];
static U8           _NumUnits = 0;
#ifdef _WIN32
  static int        _ReadDelay;
  static int        _WriteDelay;
#endif

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _ld
*/
static U8 _ld(U32 Value) {
  U8 i;

  for (i = 0; i < 16u; i++) {
    if ((1uL << i) == Value) {
      break;
    }
  }
  return i;
}

/*********************************************************************
*
*       _GetInst
*/
static RAMDISK_INST * _GetInst(U8 Unit) {
  RAMDISK_INST * pInst;

  FS_DEBUG_ASSERT(FS_MTYPE_DRIVER, Unit < (U8)FS_RAMDISK_NUM_UNITS);
  pInst = NULL;
  if (Unit < (U8)FS_RAMDISK_NUM_UNITS) {
    pInst = &_aInst[Unit];
  }
  return pInst;
}

/*********************************************************************
*
*       Static code (public via callback)
*
**********************************************************************
*/

/*********************************************************************
*
*       _RAM_GetStatus
*
*  Function description
*    FS driver function. Get status of the RAM disk.
*
*  Parameters
*    Unit   Device number.
*
*  Return value
*    FS_MEDIA_STATE_UNKNOWN   Media state is unknown
*    FS_MEDIA_NOT_PRESENT     Media is not present
*    FS_MEDIA_IS_PRESENT      Media is present
*/
static int _RAM_GetStatus(U8 Unit) {
  RAMDISK_INST * pInst;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return FS_MEDIA_STATE_UNKNOWN;
  }
  if (pInst->pData != NULL) {
    return FS_MEDIA_IS_PRESENT;
  }
  return FS_MEDIA_NOT_PRESENT;
}

/*********************************************************************
*
*       _RAM_Read
*
*  Function description
*    FS driver function. Reads the contents of consecutive sectors from the RAM disk.
*
*  Parameters
*    Unit         Device number.
*    SectorIndex  Sector to be read from the device.
*    pBuffer      Pointer to buffer for storing the data.
*    NumSectors   Number of sectors to be read.
*
*  Return value
*    ==0        Sectors have been read and copied to pBuffer.
*    < 0        An error occurred.
*/
static int _RAM_Read(U8 Unit, U32 SectorIndex, void * pBuffer, U32 NumSectors) {
  RAMDISK_INST *         pInst;
  U8           * FS_HUGE pData;
  U8                     ldBytesPerSector;
  U32                    NumBytes;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;                   // Error, could not find instance.
  }
  if ((SectorIndex + NumSectors) > pInst->NumSectors) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "RAM: _RAM_Read: Sector out of range."));
    return 1;                   // Error, invalid sector range.
  }
  pData            = pInst->pData;
  ldBytesPerSector = pInst->ldBytesPerSector;
  pData    += SectorIndex << ldBytesPerSector;
  NumBytes  = NumSectors << ldBytesPerSector;
  FS_MEMCPY(pBuffer, SEGGER_PTR2PTR(void, pData), NumBytes);
#ifdef _WIN32
  Sleep((DWORD)_ReadDelay);
#endif
  return 0;
}

/*********************************************************************
*
*       _RAM_Write
*
*  Function description
*    FS driver function. Write the contents of consecutive sectors.
*
*  Parameters
*    Unit         Device number.
*    SectorIndex  First sector to be written to the device.
*    pBuffer      Pointer to buffer for holding the data.
*    NumSectors   Number of sectors to be written to the device.
*    RepeatSame   It set to 1 the same data has to be written to all sectors.
*                 In this case pBuffer points to the contents of a single sector.
*
*  Return value
*    ==0        O.K.: Sectors have been written to device.
*    < 0        An error occurred.
*/
static int _RAM_Write(U8 Unit, U32 SectorIndex, const void * pBuffer, U32 NumSectors, U8 RepeatSame) {
  RAMDISK_INST *         pInst;
  U8           * FS_HUGE pData;
  U8                     ldBytesPerSector;
  U32                    BytesPerSector;
  U32                    NumBytes;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;                   // Error, could not find instance.
  }
  if ((SectorIndex + NumSectors) > pInst->NumSectors) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "RAM: _RAM_Write: Sector out of range."));
    return 1;                   // Error, invalid sector range.
  }
  pData            = pInst->pData;
  ldBytesPerSector = pInst->ldBytesPerSector;
  BytesPerSector   = 1uL << ldBytesPerSector;
  pData += SectorIndex << ldBytesPerSector;
  if (RepeatSame != 0u) {
    do {
      FS_MEMCPY(SEGGER_PTR2PTR(void, pData), pBuffer, BytesPerSector);
      pData += BytesPerSector;
    } while (--NumSectors != 0u);
  } else {
    NumBytes = NumSectors << ldBytesPerSector;
    FS_MEMCPY(SEGGER_PTR2PTR(void, pData), pBuffer, NumBytes);
  }
#ifdef _WIN32
  Sleep((DWORD)_WriteDelay);
#endif
  return 0;
}

/*********************************************************************
*
*       _RAM_IoCtl
*
*  Function description
*    FS driver function. Execute device command.
*
*  Parameters
*    Unit         Device number.
*    Cmd          Command to be executed.
*    Aux          Parameter depending on command.
*    pBuffer      Pointer to a buffer used for the command.
*
*  Return value
*    Command specific. In general a negative value means an error.
*/
static int _RAM_IoCtl(U8 Unit, I32 Cmd, I32 Aux, void * pBuffer) {
  RAMDISK_INST * pInst;
  FS_DEV_INFO  * pInfo;

  FS_USE_PARA(Aux);
  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return -1;              // Error, could not get driver instance.
  }
  switch (Cmd) {
  case FS_CMD_GET_DEVINFO:
    if (pBuffer == NULL) {
      return -1;            // Error, no buffer has been specified.
    }
    pInfo = SEGGER_PTR2PTR(FS_DEV_INFO, pBuffer);
    pInfo->NumSectors      = pInst->NumSectors;
    pInfo->BytesPerSector  = (U16)(1uL << pInst->ldBytesPerSector);
    break;
#ifdef _WIN32
  case FS_CMD_SET_DELAY:
    _ReadDelay  = Aux;
    _WriteDelay = (int)SEGGER_PTR2ADDR(pBuffer);
    break;
#endif
#if FS_SUPPORT_DEINIT
  case FS_CMD_DEINIT:
    _NumUnits--;
    break;
#endif
  default:
    //
    // Command not supported.
    //
    break;
  }
  return 0;
}

/*********************************************************************
*
*       _RAM_AddDevice
*/
static int _RAM_AddDevice(void) {
  if (_NumUnits >= (U8)FS_RAMDISK_NUM_UNITS) {
    return -1;                      // Error, too many instances.
  }
  return (int)_NumUnits++;
}

/*********************************************************************
*
*       _RAM_GetNumUnits
*/
static int _RAM_GetNumUnits(void) {
  return (int)_NumUnits;
}

/*********************************************************************
*
*       _RAM_GetDriverName
*/
static const char * _RAM_GetDriverName(U8 Unit) {
  FS_USE_PARA(Unit);
  return "ram";
}

/*********************************************************************
*
*       Public data
*
**********************************************************************
*/

const FS_DEVICE_TYPE FS_RAMDISK_Driver = {
  _RAM_GetDriverName,
  _RAM_AddDevice,
  _RAM_Read,
  _RAM_Write,
  _RAM_IoCtl,
  NULL,
  _RAM_GetStatus,
  _RAM_GetNumUnits
};

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_RAMDISK_Configure
*
*  Function description
*    Configures an instance of RAM disk driver.
*
*  Parameters
*    Unit             Index of the driver instance (0-based).
*    pData            Memory area to be used as storage. It cannot be NULL.
*    BytesPerSector   Number of bytes in a logical sector.
*    NumSectors       Number of logical sectors in the storage.
*
*  Additional information
*    The application has to call this function for each instance
*    of the RAM disk driver it adds to file system. Unit identifies
*    the instance of the RAM disk driver. The instance of the RAM
*    disk driver added first to file system has the index 0,
*    the second instance has the index 1, and so on.
*
*    pData has to point to a memory region that is at least
*    BytesPerSector * NumSectors bytes large. The memory region can by
*    located on any system memory that is accessible by CPU.
*    BytesPerSector has to be a power of 2 value.
*/
void FS_RAMDISK_Configure(U8 Unit, void * pData, U16 BytesPerSector, U32 NumSectors) {
  RAMDISK_INST * pInst;

  FS_DEBUG_ASSERT(FS_MTYPE_DRIVER, Unit < (U8)FS_RAMDISK_NUM_UNITS);
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    pInst->pData            = SEGGER_PTR2PTR(U8, pData);
    pInst->NumSectors       = NumSectors;
    pInst->ldBytesPerSector = _ld(BytesPerSector);
  }
}

/*************************** End of file ****************************/
