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
File    : FS_CACHE_RW.c
Purpose : Logical Block Layer, Cache module
Additional information:
  Cache Strategy: Read / write cache, caching all sectors equally.
  This cache module can be used on any device with any file system.
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#include "FS_Int.h"

#if FS_SUPPORT_CACHE

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _GetHashCode
*
*  Function description
*    Calculates hash code, based on sector number and number of sectors in cache.
*/
static U32 _GetHashCode(U32 SectorIndex, U32 NumSectorIndices) {
  return SectorIndex % NumSectorIndices;
}

/*********************************************************************
*
*       _InvalidateCache
*
*  Function description
*    Invalidates all data in cache.
*/
static void _InvalidateCache(CACHE_RW_DATA * pCacheData) {
  U32                   i;
  U32                   NumSectors;
  CACHE_RW_BLOCK_INFO * pBlockInfo;
  U32                   SectorSize;

  NumSectors = pCacheData->NumSectors;
  SectorSize = pCacheData->SectorSize;
  pBlockInfo = SEGGER_PTR2PTR(CACHE_RW_BLOCK_INFO, pCacheData + 1);                                                           // MISRA deviation D:100[d]
  //
  // Initialize all the cache entries.
  //
  for (i = 0; i < NumSectors; i++) {
    pBlockInfo->SectorIndex = SECTOR_INDEX_INVALID;
    pBlockInfo->IsDirty     = 0;
    pBlockInfo              = SEGGER_PTR2PTR(CACHE_RW_BLOCK_INFO, SEGGER_PTR2PTR(U8, pBlockInfo + 1) + SectorSize);           // MISRA deviation D:100[d]
  }
}

/*********************************************************************
*
*       _ComputeNumSectors
*/
static U32 _ComputeNumSectors(FS_DEVICE * pDevice, CACHE_RW_DATA * pCacheData) {
  U32 NumSectors;
  U16 SectorSize;
  U32 NumBytes;

  if (pCacheData->SectorSize != 0u) {
    return pCacheData->NumSectors;
  }
  NumBytes   = pCacheData->NumBytesCache;
  SectorSize = FS_GetSectorSize(pDevice);
  if ((SectorSize == 0u) || (NumBytes < sizeof(CACHE_RW_DATA))) {
    return 0;
  }
  //
  // Compute number of sectors in cache
  //
  NumSectors = (NumBytes - sizeof(CACHE_RW_DATA)) / (sizeof(CACHE_RW_BLOCK_INFO) + SectorSize);
  if (NumSectors > 0u) {
    pCacheData->NumSectors = NumSectors;
    pCacheData->SectorSize = SectorSize;
    _InvalidateCache(pCacheData);
  }
  return NumSectors;
}

/*********************************************************************
*
*       _CleanBlock
*
*  Function description
*    Writes the sector data of a cache block to medium.
*/
static int _CleanBlock(FS_DEVICE * pDevice, const CACHE_RW_BLOCK_INFO * pBlockInfo) {
  int r;
  U32 SectorIndex;

  SectorIndex = pBlockInfo->SectorIndex;
  FS_DEBUG_LOG((FS_MTYPE_CACHE, "CRW: CLEAN VN: \"%s:%d:\" SI: %lu\n", pDevice->pType->pfGetName(pDevice->Data.Unit), pDevice->Data.Unit, SectorIndex));
  r = FS_LB_WriteBack(pDevice, SectorIndex, pBlockInfo + 1);
  return r;
}

/*********************************************************************
*
*       _WriteIntoCache
*
*  Function description
*    Writes a sector to cache.
*/
static void _WriteIntoCache(CACHE_RW_BLOCK_INFO * pBlockInfo, U32 SectorIndex, const void * pData, U32 SectorSize) {
  pBlockInfo->SectorIndex = SectorIndex;
  FS_MEMCPY(pBlockInfo + 1, pData, SectorSize);
}

/*********************************************************************
*
*       _SetMode
*
*  Function description
*    Sets the mode for the give type of sectors.
*/
static int _SetMode(const FS_DEVICE * pDevice, const CACHE_MODE * pCacheMode) {
  unsigned        i;
  CACHE_RW_DATA * pCacheData;
  U32             TypeMask;

  if (pCacheMode == NULL) {
    return -1;                // Error, invalid parameter.
  }
  pCacheData = SEGGER_PTR2PTR(CACHE_RW_DATA, pDevice->Data.pCacheData);                                                       // MISRA deviation D:100[d]
  for (i = 0; i < FS_SECTOR_TYPE_COUNT; i++) {
    TypeMask = 1uL << i;
    if ((TypeMask & (U32)pCacheMode->TypeMask) != 0u) {
      pCacheData->aCacheMode[i] = (U8)pCacheMode->ModeMask;
    }
  }
  return 0;
}

/*********************************************************************
*
*       _Clean
*
*  Function description
*    Writes out all dirty sectors from cache.
*/
static int _Clean(FS_DEVICE * pDevice) {
  U32                   i;
  U32                   NumSectors;
  CACHE_RW_DATA       * pCacheData;
  CACHE_RW_BLOCK_INFO * pBlockInfo;
  U32                   SectorSize;
  U32                   SizeOfCacheBlock;
  int                   r;
  int                   Result;

  pCacheData       = SEGGER_PTR2PTR(CACHE_RW_DATA, pDevice->Data.pCacheData);                                                 // MISRA deviation D:100[d]
  NumSectors       = pCacheData->NumSectors;
  SectorSize       = pCacheData->SectorSize;
  SizeOfCacheBlock = sizeof(CACHE_RW_BLOCK_INFO) + SectorSize;
  r                = 0;
  for (i = 0; i < NumSectors; i++) {
    pBlockInfo = SEGGER_PTR2PTR(CACHE_RW_BLOCK_INFO, SEGGER_PTR2PTR(U8, pCacheData + 1) + (i * SizeOfCacheBlock));            // MISRA deviation D:100[d]
    if (pBlockInfo->IsDirty != 0u) {
      Result = _CleanBlock(pDevice, pBlockInfo);
      if (Result != 0) {
        r = Result;
      }
      pBlockInfo->IsDirty = 0;
    }
  }
  return r;
}

/*********************************************************************
*
*       _RemoveFromCache
*
*  Function description
*    Make sure that this sector is not in cache.
*    This functions does not write dirty data; even dirty entries can be removed
*
*  Notes
*    (1) What for ?
*        This can be useful (and important as well to maintain cache coherency).
*        The function is called whenever clusters (data or directory) are freed.
*/
static void _RemoveFromCache(const FS_DEVICE * pDevice, U32 FirstSector, U32 NumSectors) {
  U32                   HashCode;
  U32                   Off;
  U32                   SectorIndex;
  U32                   NumSectorsInCache;
  U32                   iSector;
  CACHE_RW_BLOCK_INFO * pBlockInfo;
  CACHE_RW_DATA       * pCacheData;

  pCacheData = SEGGER_PTR2PTR(CACHE_RW_DATA, pDevice->Data.pCacheData);                                                       // MISRA deviation D:100[d]
  NumSectorsInCache = pCacheData->NumSectors;
  if (NumSectorsInCache != 0u) {
    for (iSector = 0; iSector < NumSectors; iSector++) {
      SectorIndex = FirstSector + iSector;
      HashCode = _GetHashCode(SectorIndex, NumSectorsInCache);
      Off = HashCode * (sizeof(CACHE_RW_BLOCK_INFO) + pCacheData->SectorSize);
      pBlockInfo = SEGGER_PTR2PTR(CACHE_RW_BLOCK_INFO, SEGGER_PTR2PTR(U8, pCacheData + 1) + Off);                             // MISRA deviation D:100[d]
      if (pBlockInfo->SectorIndex == SectorIndex) {
        FS_DEBUG_LOG((FS_MTYPE_CACHE, "CRW: REMOVE VN: \"%s:%d\", SI: %lu\n", pDevice->pType->pfGetName(pDevice->Data.Unit), pDevice->Data.Unit, pBlockInfo->SectorIndex));
        pBlockInfo->SectorIndex = SECTOR_INDEX_INVALID;
        pBlockInfo->IsDirty = 0;
      }
    }
  }
}

/*********************************************************************
*
*       Static code (public via callback)
*
**********************************************************************
*/

/*********************************************************************
*
*       _CacheRW_ReadFromCache
*
*  Function description
*    Read sector from cache if possible.
*
*  Return value
*    ==1    Sector not found.
*    ==0    Sector found.
*/
static int _CacheRW_ReadFromCache(FS_DEVICE * pDevice, U32 SectorIndex, void * pData, U8 SectorType) {
  U32                   Off;
  U32                   SectorSize;
  CACHE_RW_DATA       * pCacheData;
  CACHE_RW_BLOCK_INFO * pBlockInfo;

  FS_USE_PARA(SectorType);
  pCacheData = SEGGER_PTR2PTR(CACHE_RW_DATA, pDevice->Data.pCacheData);                                                       // MISRA deviation D:100[d]
  if (_ComputeNumSectors(pDevice, pCacheData) == 0u) {
    return 1;               // Device is not available
  }
  SectorSize  = pCacheData->SectorSize;
  Off         = _GetHashCode(SectorIndex, pCacheData->NumSectors) * (sizeof(CACHE_RW_BLOCK_INFO) + SectorSize);
  pBlockInfo  = SEGGER_PTR2PTR(CACHE_RW_BLOCK_INFO, SEGGER_PTR2PTR(U8, pCacheData + 1) + Off);                                // MISRA deviation D:100[d]
  if (pBlockInfo->SectorIndex == SectorIndex) {
    FS_MEMCPY(pData, pBlockInfo + 1, SectorSize);
    return 0;               // Sector found
  }
  return 1;                 // Sector not found
}

/*********************************************************************
*
*       _CacheRW_UpdateCache
*
*  Function description
*    Updates a sector in cache.
*    Called after a READ operation to update the cache.
*    This means that the sector can not be in the cache.
*
*  Return value
*    ==0    Not in write cache, the physical write operation still needs to be performed (Since this cache is a pure read-cache).
*/
static int _CacheRW_UpdateCache(FS_DEVICE * pDevice, U32 SectorIndex, const void * pData, U8 SectorType) {
  U32                   Off;
  U32                   SectorSize;
  unsigned              CacheMode;
  CACHE_RW_DATA       * pCacheData;
  CACHE_RW_BLOCK_INFO * pBlockInfo;
  int                   r;

  pCacheData = SEGGER_PTR2PTR(CACHE_RW_DATA, pDevice->Data.pCacheData);                                                       // MISRA deviation D:100[d]
  CacheMode  = pCacheData->aCacheMode[SectorType];
  if (_ComputeNumSectors(pDevice, pCacheData) == 0u) {
    return 0;                                     // Device is not available
  }
  r = 0;                                          // Set to indicate success.
  if ((CacheMode & FS_CACHE_MODE_R) != 0u) {      // Read cache is on for this type of sector.
    SectorSize  = pCacheData->SectorSize;
    Off         = _GetHashCode(SectorIndex, pCacheData->NumSectors) * (sizeof(CACHE_RW_BLOCK_INFO) + SectorSize);
    pBlockInfo  = SEGGER_PTR2PTR(CACHE_RW_BLOCK_INFO, SEGGER_PTR2PTR(U8, pCacheData + 1) + Off);                              // MISRA deviation D:100[d]
    //
    // If we replace an other, dirty sector, we need to write it out.
    //
    if ((pBlockInfo->SectorIndex != SectorIndex) && (pBlockInfo->IsDirty != 0u)) {
      r = _CleanBlock(pDevice, pBlockInfo);
    }
    _WriteIntoCache(pBlockInfo, SectorIndex, pData, SectorSize);
    pBlockInfo->IsDirty = 0;
  }
  return r;
}

/*********************************************************************
*
*       _CacheRW_WriteCache
*
*  Function description
*    Writes a sector into cache.
*
*  Return value
*    ==0    Not  in write cache, the physical write operation still needs to be performed.
*    ==1    Data in write cache, the physical write operation does not need to be performed.
*/
static int _CacheRW_WriteCache(FS_DEVICE * pDevice, U32 SectorIndex, const void * pData, U8 SectorType) {
  U32                   Off;
  U32                   SectorSize;
  unsigned              CacheMode;
  int                   IsWriteRequired;
  CACHE_RW_DATA       * pCacheData;
  CACHE_RW_BLOCK_INFO * pBlockInfo;
  int                   r;

  pCacheData = SEGGER_PTR2PTR(CACHE_RW_DATA, pDevice->Data.pCacheData);                                                       // MISRA deviation D:100[d]
  if (_ComputeNumSectors(pDevice, pCacheData) == 0u) {
    return 0;                                       // Device is not available
  }
  CacheMode       = pCacheData->aCacheMode[SectorType];
  SectorSize      = pCacheData->SectorSize;
  Off             = _GetHashCode(SectorIndex, pCacheData->NumSectors) * (sizeof(CACHE_RW_BLOCK_INFO) + SectorSize);
  pBlockInfo      = SEGGER_PTR2PTR(CACHE_RW_BLOCK_INFO, SEGGER_PTR2PTR(U8, pCacheData + 1) + Off);                            // MISRA deviation D:100[d]
  IsWriteRequired = 0;
  if ((CacheMode & FS_CACHE_MODE_W) != 0u) {        // Write cache on for this type of sector ?
    IsWriteRequired = 1;
  } else {
    if (pBlockInfo->SectorIndex == SectorIndex) {   // Sector already in cache ?
      IsWriteRequired = 1;                          // Update required !
    }
  }
  if (IsWriteRequired != 0) {
    //
    // If we replace an other, dirty sector, we need to write it out
    //
    if ((pBlockInfo->IsDirty != 0u) && (pBlockInfo->SectorIndex != SectorIndex)) {
      r = _CleanBlock(pDevice, pBlockInfo);
      if (r != 0) {
        return 0;                                   // TBD: Improve the error handling.
      }
    }
    pBlockInfo->IsDirty = 0;
    _WriteIntoCache(pBlockInfo, SectorIndex, pData, SectorSize);
  }
  if ((CacheMode & FS_CACHE_MODE_D) != 0u) {        // Delayed write allowed cache on for this type of sector ?
    pBlockInfo->IsDirty = 1;
    return 1;                                       // Write is delayed (data in cache) and does not need to be performed
  }
  return 0;                                         // Write still needs to be performed.
}

/*********************************************************************
*
*       _CacheRW_InvalidateCache
*
*  Function description
*    Invalidates all data in cache.
*/
static void _CacheRW_InvalidateCache(void * p) {
  CACHE_RW_DATA * pCacheData;

  pCacheData = SEGGER_PTR2PTR(CACHE_RW_DATA, p);                                                                              // MISRA deviation D:100[d]
  _InvalidateCache(pCacheData);
  pCacheData->NumSectors = 0;
  pCacheData->SectorSize = 0;
}

/*********************************************************************
*
*       _CacheRW_Command
*
*  Function description
*    Executes a cache command.
*
*  Return value
*    ==0    Command executed
*    !=0    An error occurred
*/
static int _CacheRW_Command(FS_DEVICE * pDevice, int Cmd, void * p) {
  int r;

  r = -1;
  switch (Cmd) {
  case FS_CMD_CACHE_CLEAN:
    r = _Clean(pDevice);
    break;
  case FS_CMD_CACHE_SET_MODE:
    r = _SetMode(pDevice, SEGGER_PTR2PTR(CACHE_MODE, p));                                                                     // MISRA deviation D:100[f]
    break;
  case FS_CMD_CACHE_INVALIDATE:
    _CacheRW_InvalidateCache(p);
    r = 0;
    break;
  case FS_CMD_CACHE_GET_TYPE:
    {
      FS_CACHE_TYPE * pCacheType;

      pCacheType = SEGGER_PTR2PTR(FS_CACHE_TYPE, p);                                                                          // MISRA deviation D:100[f]
      if (pCacheType != NULL) {
        *pCacheType = FS_CacheRW_Init;
        r = 0;
      }
    }
    break;
  case FS_CMD_CACHE_FREE_SECTORS:
    {
      CACHE_FREE * pCacheFree;

      pCacheFree = SEGGER_PTR2PTR(CACHE_FREE, p);                                                                             // MISRA deviation D:100[f]
      if (pCacheFree != NULL) {
        _RemoveFromCache(pDevice, pCacheFree->FirstSector, pCacheFree->NumSectors);
      }
      r = 0;
    }
    break;
  case FS_CMD_CACHE_GET_NUM_SECTORS:
    {
      U32             NumSectors;
      U32           * pNumSectors;
      CACHE_RW_DATA * pCacheData;

      pCacheData  = SEGGER_PTR2PTR(CACHE_RW_DATA, pDevice->Data.pCacheData);                                                  // MISRA deviation D:100[d]
      pNumSectors = SEGGER_PTR2PTR(U32, p);                                                                                   // MISRA deviation D:100[f]
      if (pNumSectors != NULL) {
        NumSectors   = pCacheData->NumSectors;
        *pNumSectors = NumSectors;
        r = 0;
      }
    }
    break;
  default:
    //
    // Invalid command.
    //
    break;
  }
  return r;
}

/*********************************************************************
*
*       _CacheRW_API
*/
static const FS_CACHE_API _CacheRW_API = {
  _CacheRW_ReadFromCache,
  _CacheRW_UpdateCache,
  _CacheRW_InvalidateCache,
  _CacheRW_Command,
  _CacheRW_WriteCache
};

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_CacheRW_Init
*
*  Function description
*    Initializes the cache.
*
*  Return value
*    Number of cache blocks (Number of sectors that can be cached)
*/
U32 FS_CacheRW_Init(FS_DEVICE * pDevice, void * pData, I32 NumBytes) {
  FS_DEVICE_DATA * pDevData;
  U32              NumSectors;
  CACHE_RW_DATA  * pCacheData;
  U8             * pData8;
  U32              NumBytesCache;

  //
  // Sanity checks.
  //
  FS_DEBUG_ASSERT(FS_MTYPE_API, sizeof(CACHE_RW_DATA)       == FS_SIZEOF_CACHE_RW_DATA);                                      //lint !e506 !e774 Constant value Boolean and Boolean within 'if' always evaluates to False N:102.
  FS_DEBUG_ASSERT(FS_MTYPE_API, sizeof(CACHE_RW_BLOCK_INFO) == FS_SIZEOF_CACHE_RW_BLOCK_INFO);                                //lint !e506 !e774 Constant value Boolean and Boolean within 'if' always evaluates to False N:102.
  //
  // Fill local variables.
  //
  pDevData      = &pDevice->Data;
  pData8        = SEGGER_PTR2PTR(U8, pData);                                                                                  // MISRA deviation D:100[d]
  NumBytesCache = (U32)NumBytes;
  //
  // Align pointer to a 32bit boundary
  //
  if ((SEGGER_PTR2ADDR(pData8) & 3u) != 0u) {                                                                                 // MISRA deviation D:103[b]
    NumBytesCache -= 4u - (SEGGER_PTR2ADDR(pData8) & 3u);                                                                     // MISRA deviation D:103[b]
    pData8        += 4u - (SEGGER_PTR2ADDR(pData8) & 3u);                                                                     // MISRA deviation D:103[b]
  }
  //
  // If less memory is available as we need to hold the
  // management structure, we leave everything as it is.
  // A cache module is then not attached to the device.
  //
  if (NumBytesCache < sizeof(CACHE_RW_DATA)) {
    return 0;
  }
  pCacheData = SEGGER_PTR2PTR(CACHE_RW_DATA, pData8);                                                                         // MISRA deviation D:100[d]
  FS_MEMSET(pCacheData, 0, sizeof(CACHE_RW_DATA));
  pDevData->pCacheAPI       = &_CacheRW_API;
  pDevData->pCacheData      = pCacheData;
  pCacheData->NumBytesCache = NumBytesCache;
  NumSectors = _ComputeNumSectors(pDevice, pCacheData);
  return NumSectors;
}
#else

/*********************************************************************
*
*       CacheRW_c
*
*  Function description
*    Dummy function to prevent compiler errors.
*/
void CacheRW_c(void);
void CacheRW_c(void) {}

#endif // FS_SUPPORT_CACHE

/*************************** End of file ****************************/
