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
File        : USBH_MSD_ReadAheadCache.c
Purpose     : USB host implementation
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/

#include "USBH_MSD.h"
#if USBH_USE_LEGACY_MSD
  #include "USBH_MSD_Int.h"
#else
  #include "USBH_MSC_Int.h"
#endif

/*********************************************************************
*
*       Defines
*
**********************************************************************
*/
#define NUM_SECTORS_TO_READ_AHEAD    8

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
typedef struct {
  U32             StartSector;
  USBH_MSD_UNIT * pUnit;
  U8            * paSectorBuffer;
  U8            * paUserSectorBuffer;
  U32             UserSectorBufferSize;
} USBH_MSD_READ_AHEAD_INST;

static USBH_MSD_READ_AHEAD_INST _Inst = {0xFFFFFFFFu, NULL, NULL, NULL, 0};

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _Invalidate
*
*  Function description
*    Invalidates the internal sector buffer.
*/
static void _Invalidate(USBH_MSD_UNIT * pUnit) {
  (void)pUnit;
  _Inst.StartSector = 0xFFFFFFFFu;
  _Inst.pUnit = NULL;
}

/*********************************************************************
*
*       _CacheReadSectors
*
*  Function description
*    Checks whether the data can be read from the read ahead cache
*    or shall be read from the MSD device. In case we shall read from
*    the MSD device we read at least NUM_SECTORS_TO_READ_AHEAD and store
*    the additional read sector in the cache.
*    If more than NUM_SECTORS_TO_READ_AHEAD shall be read we simply ignore this
*/
static USBH_STATUS _CacheReadSectors(USBH_MSD_UNIT * pUnit, U32 SectorAddress, U8 * pBuffer, U16 NumSectors) {
  USBH_STATUS       Status;
  U32               FirstSector;
  U32               LastSector;
  U32               StartSectorOff;
  U16               NumSectorsInBuffer;
  U16               NumSectors2Read;
  U16               BytesPerSector;

  //
  // Always invalidate the cache when the unit changes.
  //
  if (pUnit != _Inst.pUnit) {
    _Invalidate(pUnit);
  }
  Status = USBH_STATUS_ERROR;
  BytesPerSector = pUnit->BytesPerSector;
  if (_Inst.paUserSectorBuffer != NULL) {
    NumSectorsInBuffer = (U16)(_Inst.UserSectorBufferSize / BytesPerSector);
  } else {
    NumSectorsInBuffer = NUM_SECTORS_TO_READ_AHEAD;
  }
  if (_Inst.paSectorBuffer == NULL) {
    //
    // Did the user provide a buffer for us?
    // If yes - use it, if not - allocate one.
    //
    if (_Inst.paUserSectorBuffer != NULL) {
      if (NumSectorsInBuffer > 0u) {
        _Inst.paSectorBuffer = _Inst.paUserSectorBuffer;
      }
    } else {
      _Inst.paSectorBuffer = (U8*)USBH_TRY_MALLOC((U32)NumSectorsInBuffer * BytesPerSector);
      USBH_ASSERT(_Inst.paSectorBuffer != NULL);
      if (_Inst.paSectorBuffer == NULL) {
        return Status;
      }
    }
  }
  FirstSector = _Inst.StartSector;
  LastSector = _Inst.StartSector + NumSectorsInBuffer - 1u;
  for (;;) {
    if (NumSectors > NumSectorsInBuffer) {
      Status = USBH_MSD__ReadSectorsNoCache(pUnit, SectorAddress, pBuffer, (U16)NumSectors);
#if USBH_USE_LEGACY_MSD
      if (Status == USBH_STATUS_COMMAND_FAILED) {
        if (USBH_MSD__RequestSense(pUnit) == USBH_STATUS_SUCCESS) {
          USBH_WARN((USBH_MCAT_MSC_API, "MSD: USBH_MSD_ReadSectors failed, SenseCode = 0x%08x", pUnit->Sense.Sensekey));
        }
      }
#endif
      if (Status != USBH_STATUS_SUCCESS) {
        USBH_WARN((USBH_MCAT_MSC_API, "MSD: USBH_MSD_ReadSectors: Status %s", USBH_GetStatusStr(Status)));
      }
      return Status;
    } else if ((FirstSector == 0xFFFFFFFFu) || (FirstSector > SectorAddress) || (LastSector < SectorAddress)) {
      Status = USBH_MSD__ReadSectorsNoCache(pUnit, SectorAddress, _Inst.paSectorBuffer, NumSectorsInBuffer);
      if (Status != USBH_STATUS_SUCCESS) {
        return Status;
      }
      _Inst.StartSector = SectorAddress;
      FirstSector = SectorAddress;
      LastSector = SectorAddress + NumSectorsInBuffer - 1u;
      _Inst.pUnit = pUnit;
    } else {
      //
      // MISRA comment
      //
    }
    if (pUnit != _Inst.pUnit || NumSectors > NumSectorsInBuffer) {
      break;
    }
    if (SectorAddress >= FirstSector && (SectorAddress <= LastSector)) {
      NumSectors2Read = USBH_MIN(NumSectors, NumSectorsInBuffer);
      StartSectorOff = SectorAddress - FirstSector;
      USBH_MEMCPY(pBuffer, &_Inst.paSectorBuffer[StartSectorOff * BytesPerSector], (U32)NumSectors2Read * BytesPerSector);
      NumSectors -= NumSectors2Read;
    }
    if (NumSectors == 0u) {
      return USBH_STATUS_SUCCESS;
    }
  }
  return Status;
}

/*********************************************************************
*
*       _CacheWriteSectors
*
*  Function description
*    It simply invalidate the read ahead cache and writes
*    sectors to a USB MSD device.
*/
static USBH_STATUS _CacheWriteSectors(USBH_MSD_UNIT * pUnit, U32 SectorAddress, const U8 * pBuffer, U16 NumSectors) USBH_API_USE {
  USBH_STATUS       Status;

  _Inst.StartSector = 0xFFFFFFFFu;
  Status = USBH_MSD__WriteSectorsNoCache(pUnit, SectorAddress, pBuffer, (U16)NumSectors);
  return Status;
}

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/

static USBH_MSD_CACHE_API _ReadAheadCacheAPI = {
  _CacheReadSectors,
  _CacheWriteSectors,
  _Invalidate
};


/*********************************************************************
*
*       Public code
*
**********************************************************************
*/


/*********************************************************************
*
*       USBH_MSD_UseAheadCache
*
*  Function description
*    Enables the read-ahead-cache functionality.
*
*  Parameters
*    OnOff: 1 : on, 0 - off.
*
*  Additional information
*    The read-ahead-cache is a functionality which makes sure that read
*    accesses to an MSD will always read a minimal amount of sectors
*    (normally at least four). The rest of the sectors which have not
*    been requested directly will be stored in a cache and subsequent
*    reads will be supplied with data from the cache instead of the
*    actual device.
*
*    This functionality is mainly used as a workaround for certain MSD
*    devices which crash when single sectors are being read directly
*    from the device too often. Enabling the cache will cause a slight
*    drop in performance, but will make sure that all MSD devices which
*    are affected by the aforementioned issue do not crash. Unless
*    USBH_MSD_SetAheadBuffer() was used before calling this function
*    with a "1" as parameter the function will try to allocate a buffer
*    for eight sectors (4096 bytes) from the emUSB-Host memory pool.
*/
void USBH_MSD_UseAheadCache(int OnOff) {
  USBH_LOG((USBH_MCAT_MSC, "MSD: USBH_MSD_UseAheadCache: cache %s", (OnOff) ? "on" : "off"));
  if (OnOff != 0) {
    USBH_MSD_Global.pCacheAPI = &_ReadAheadCacheAPI;
  } else {
    USBH_MSD_Global.pCacheAPI = NULL;
  }
}

/*********************************************************************
*
*       USBH_MSD_SetAheadBuffer
*
*  Function description
*    Sets a user provided buffer for the read-ahead-cache functionality.
*
*  Parameters
*    pAheadBuf : Pointer to a USBH_MSD_AHEAD_BUFFER structure
*                which holds the buffer information.
*
*  Additional information
*    This function has to be called before enabling the read-ahead-cache
*    with USBH_MSD_UseAheadCache(). The buffer should have space for
*    at least four sectors (2048 bytes), but eight sectors (4096 bytes)
*    are suggested for better performance. The buffer size must be a
*    multiple of 512.
*/
void USBH_MSD_SetAheadBuffer(const USBH_MSD_AHEAD_BUFFER * pAheadBuf) {
  _Inst.paUserSectorBuffer = pAheadBuf->pBuffer;
  _Inst.UserSectorBufferSize = pAheadBuf->Size;
}

/*************************** End of file ****************************/
