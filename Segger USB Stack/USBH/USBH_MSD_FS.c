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
Purpose : Implementation of an interface between the USB Host library
          and the emFile file system.
-------------------------- END-OF-HEADER -----------------------------
*/
/*********************************************************************
*
*       #include section
*
**********************************************************************
*/
#include "FS.h"
#include "USBH_Int.h"
#include "USBH_MSD.h"
#include "USBH_MSD_FS.h"

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static unsigned _NumUnits;

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _GetDriverName
*/
static const char * _GetDriverName(U8 Unit) {
  FS_USE_PARA(Unit);
  return "msd";
}

/*********************************************************************
*
*       _ReadSectors
*
*  Description:
*    Reads one or more sectors from the medium.
*
*  Parameters
*    Unit            : Device number.
*    SectorIndex     : SectorIndex to be read from the device.
*    pBuffer         : Pointer to buffer for storing the data.
*    NumSectors      : Number of sectors
*
*  Return value
*    ==0             : SectorIndex has been read and copied to pBuffer.
*    < 0             : An error has occurred.
*/
static int _ReadSectors(U8 Unit, U32 SectorIndex, void * pBuffer, U32 NumSectors) {
  USBH_MSD_UNIT_INFO  Info;
  USBH_STATUS         Status;
  U32                 NumSectorsAtOnce;
  U8                * pBuf;

  //
  // Limit the number of sectors as some USB sticks are not able
  // to read more sectors with a single MSD read command.
  //
  if (NumSectors > USBH_MSD_MAX_SECTORS_AT_ONCE) {
    Status = USBH_MSD_GetUnitInfo(Unit, &Info);
    pBuf = SEGGER_PTR2PTR(U8, pBuffer);                   // lint D:100[e]
    if (Status == USBH_STATUS_SUCCESS) {
      do {
        NumSectorsAtOnce = SEGGER_MIN(USBH_MSD_MAX_SECTORS_AT_ONCE, NumSectors);
        Status = USBH_MSD_ReadSectors(Unit, SectorIndex, NumSectorsAtOnce, pBuf);
        NumSectors  -= NumSectorsAtOnce;
        SectorIndex += NumSectorsAtOnce;
        pBuf = pBuf + NumSectorsAtOnce * Info.BytesPerSector;
      } while (NumSectors != 0u && Status == USBH_STATUS_SUCCESS);
    }
  } else {
    Status = USBH_MSD_ReadSectors(Unit, SectorIndex, NumSectors, USBH_U8PTR(pBuffer));
  }
  if (Status != USBH_STATUS_SUCCESS) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "ERROR _ReadSectors status: 0x%08x(%s)\n", Status, USBH_GetStatusStr(Status)));
    return -1;
  }
  return 0;
}

/*********************************************************************
*
*       _WriteSectors
*
*  Description:
*  FS driver function. Writes a sector to the medium.
*
*  Parameters
*    Unit           : Device number.
*    SectorIndex    : SectorIndex to be written on the device.
*    pBuffer        : Pointer to a buffer containing the data to be written.
*    NumSectors     : Number of sectors
*    RepeatSame     : When this flag is set the sector contained in the buffer
*                     pointed to by pBuffer will be written NumSectors times
*                     to the device starting at SectorIndex.
*
*  Return value
*    ==0            : SectorIndex has been written to the device.
*    < 0            : An error has occurred.
*/
static int _WriteSectors(U8 Unit, U32 SectorIndex, const void * pBuffer, U32 NumSectors, U8 RepeatSame) {
  USBH_MSD_UNIT_INFO  Info;
  USBH_STATUS         Status;
  U32                 NumSectorsAtOnce;
  const U8          * pBuf;

  if (RepeatSame != 0u) {
    do {
      Status = USBH_MSD_WriteSectors(Unit, SectorIndex++, 1, (const U8*)pBuffer);       //lint !e9079  D:100[e]
      NumSectors--;
    } while (NumSectors != 0u && Status == USBH_STATUS_SUCCESS);
  } else {
    //
    // Limit the number of sectors as some USB sticks are not able
    // to write more sectors with a single MSD write command.
    //
    if (NumSectors > USBH_MSD_MAX_SECTORS_AT_ONCE) {
      Status = USBH_MSD_GetUnitInfo(Unit, &Info);
      pBuf = (const U8 *)pBuffer;                                                       //lint !e9079  D:100[e]
      if (Status == USBH_STATUS_SUCCESS) {
        do {
          NumSectorsAtOnce = SEGGER_MIN(USBH_MSD_MAX_SECTORS_AT_ONCE, NumSectors);
          Status = USBH_MSD_WriteSectors(Unit, SectorIndex, NumSectorsAtOnce, pBuf);
          NumSectors  -= NumSectorsAtOnce;
          SectorIndex += NumSectorsAtOnce;
          pBuf = pBuf + NumSectorsAtOnce * Info.BytesPerSector;
        } while (NumSectors != 0u && Status == USBH_STATUS_SUCCESS);
      }
    } else {
      Status = USBH_MSD_WriteSectors(Unit, SectorIndex, NumSectors, (const U8*)pBuffer);   //lint !e9079  D:100[e]
    }
  }
  if (Status != USBH_STATUS_SUCCESS) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "ERROR _WriteSectors status: 0x%08x(%s)\n", Status, USBH_GetStatusStr(Status)));
    return -1;
  }
  return 0;
}

/*********************************************************************
*
*       _IoCtl
*
*  Description:
*    Executes a device command.
*
*  Parameters
*    Unit         : Device number.
*    Cmd          : Command to be executed.
*    Aux          : Parameter depending on command.
*    pBuffer      : Pointer to a buffer used for the command.
*
*  Return value
*    This function is used to execute device specific commands.
*    In the file fs_api.h you can find the I/O commands that are currently
*    implemented. If the higher layers don't know the command, they
*    send it to the next lower. Your driver does not have to
*    implement one of these commands. Only if automatic formatting
*    is used or user routines need to get the size of the medium,
*    FS_CMD_GET_DEVINFO must be implemented.
*/
static int _IoCtl(U8 Unit, I32 Cmd, I32 Aux, void * pBuffer) {
  FS_DEV_INFO        * pInfo;
  USBH_MSD_UNIT_INFO   Info;
  USBH_STATUS          Status;

  FS_USE_PARA(Aux);
  switch (Cmd) {
  case FS_CMD_GET_DEVINFO:
    if (pBuffer == NULL) {
      return -1;
    }
    pInfo                  = SEGGER_PTR2PTR(FS_DEV_INFO, pBuffer); // The parameter pBuffer contains the pointer to the structure, lint D:100[a]
    pInfo->NumHeads        = 0;                      // Relevant only for mechanical drives
    pInfo->SectorsPerTrack = 0;                      // Relevant only for mechanical drives
    Status = USBH_MSD_GetUnitInfo(Unit, &Info);
    if (Status != USBH_STATUS_SUCCESS) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "ERROR _IoCtl: no device information: 0x%08x (%s)\n", Status, USBH_GetStatusStr(Status)));
      return -1;
    } else {
      pInfo->BytesPerSector = Info.BytesPerSector;
      pInfo->NumSectors     = Info.TotalSectors;
      FS_DEBUG_LOG((FS_MTYPE_DRIVER, "INFO _IoCtl: bytes per sector: %d total sectors: %d\n", pInfo->BytesPerSector, pInfo->NumSectors));
    }
    break;
#if FS_SUPPORT_DEINIT
  case FS_CMD_DEINIT:
    _NumUnits--;
    break;
#endif
  default:
    // Do nothing.
    break;
  }
  return 0; // Return zero if no problems have occurred.
}

/*********************************************************************
*
*       _GetStatus
*
*  Description:
*    FS driver function. Gets status of the device. This function is also
*    used to initialize the device and to detect a media change.
*
*  Parameters
*    Unit                       : Device number.
*
*  Return values:
*    FS_MEDIA_IS_PRESENT         : Device okay and ready for operation.
*    FS_MEDIA_NOT_PRESENT
*/
static int _GetStatus(U8 Unit) {
  USBH_STATUS Status;

  Status = USBH_MSD_GetStatus(Unit);
  if (Status != USBH_STATUS_SUCCESS) {
    // unit not ready
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "ERROR _GetStatus: device not ready, USBH MSD Status: 0x%08x (%s)\n", Status, USBH_GetStatusStr(Status)));
    return FS_MEDIA_NOT_PRESENT;
  }
  return FS_MEDIA_IS_PRESENT;
}

/*********************************************************************
*
*       _InitMedium
*/
static int _InitMedium(U8 Unit) {
  FS_USE_PARA(Unit);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "INFO:_InitMedium:  unit:%d\n", (int)Unit));
  return FS_ERR_OK;
}

/*********************************************************************
*
*       _GetNumUnits
*/
static int _GetNumUnits(void) {
  return _NumUnits;
}

/*********************************************************************
*
*       _AddDevice
*/
static int _AddDevice(void) {
  if (_NumUnits >= USBH_MSD_MAX_UNITS) {
    return -1;
  }
  return _NumUnits++;
}

/*********************************************************************
*
*       Global variables
*
**********************************************************************
*/
const FS_DEVICE_TYPE USBH_MSD_FS_Driver = {
  _GetDriverName,
  _AddDevice,
  _ReadSectors,                       // Device read sector
  _WriteSectors,                      // Device write sector
  _IoCtl,                             // IO control interface
  _InitMedium,                        // not used, only for debugging
  _GetStatus,                         // Device status
  _GetNumUnits
};

/*************************** End of file ****************************/
