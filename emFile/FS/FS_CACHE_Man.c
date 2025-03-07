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
File    : FS_CACHE_Man.c
Purpose : Logical Block Layer, Cache module
Additional information:
  Cache Strategy: Pure read cache, caching management sectors only.
  Which sectors are management sectors is determined by the File system.
  In case of FAT, only FAT sectors are considered management sectors.
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
*    Returns the entry in the cache which stores a given sector.
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
static void _InvalidateCache(CACHE_MAN_DATA * pCacheData) {
  U32                    i;
  U32                    NumSectors;
  U32                    SectorSize;
  CACHE_MAN_BLOCK_INFO * pBlockInfo;

  NumSectors = pCacheData->NumSectors;
  SectorSize = pCacheData->SectorSize;
  pBlockInfo = SEGGER_PTR2PTR(CACHE_MAN_BLOCK_INFO, pCacheData + 1);                                    // MISRA deviation D:100[d]
  //
  // Initialize all the cache entries.
  //
  for (i = 0; i < NumSectors; i++) {
    pBlockInfo->SectorIndex = SECTOR_INDEX_INVALID;
    pBlockInfo = SEGGER_PTR2PTR(CACHE_MAN_BLOCK_INFO, SEGGER_PTR2PTR(U8, pBlockInfo + 1) + SectorSize); // MISRA deviation D:100[d]
  }
}

/*********************************************************************
*
*       _ComputeNumSectors
*/
static U32 _ComputeNumSectors(FS_DEVICE * pDevice, CACHE_MAN_DATA * pCacheData) {
  U32 NumSectors;
  U16 SectorSize;
  U32 NumBytes;

  if (pCacheData->SectorSize != 0u) {
    return pCacheData->NumSectors;
  }
  NumBytes   = pCacheData->NumBytesCache;
  SectorSize = FS_GetSectorSize(pDevice);
  if ((SectorSize == 0u) || (NumBytes < sizeof(CACHE_MAN_DATA))) {
    return 0;
  }
  //
  // Compute number of sectors in cache.
  //
  NumSectors = (NumBytes - sizeof(CACHE_MAN_DATA)) / (sizeof(CACHE_MAN_BLOCK_INFO) + SectorSize);
  if (NumSectors > 0u) {
    pCacheData->NumSectors = NumSectors;
    pCacheData->SectorSize = SectorSize;
    _InvalidateCache(pCacheData);
  }
  return NumSectors;
}

/*********************************************************************
*
*       _WriteIntoCache
*
*  Function description
*    Writes a sector to cache.
*/
static void _WriteIntoCache(CACHE_MAN_BLOCK_INFO  * pBlockInfo, U32 SectorIndex, const void * pData, U32 SectorSize) {
  pBlockInfo->SectorIndex = SectorIndex;
  FS_MEMCPY(pBlockInfo + 1, pData, SectorSize);
}

/*********************************************************************
*
*       Static code (public via callback)
*
**********************************************************************
*/

/*********************************************************************
*
*       _CacheMan_ReadFromCache
*
*  Function description
*    Read sector from cache if possible.
*
*  Return value
*    ==0    Sector found.
*    ==1    Sector not found.
*/
static int _CacheMan_ReadFromCache(FS_DEVICE * pDevice, U32 SectorIndex, void * pData, U8 SectorType) {
  U32                    Off;
  U32                    SectorSize;
  CACHE_MAN_DATA       * pCacheData;
  CACHE_MAN_BLOCK_INFO * pBlockInfo;

  FS_USE_PARA(SectorType);
  pCacheData  = SEGGER_PTR2PTR(CACHE_MAN_DATA, pDevice->Data.pCacheData);                               // MISRA deviation D:100[d]
  if (_ComputeNumSectors(pDevice, pCacheData) == 0u) {
    return 1;             // Device is not available
  }
  SectorSize = pCacheData->SectorSize;
  Off        = _GetHashCode(SectorIndex, pCacheData->NumSectors) * (sizeof(CACHE_MAN_BLOCK_INFO) + SectorSize);
  pBlockInfo = SEGGER_PTR2PTR(CACHE_MAN_BLOCK_INFO, SEGGER_PTR2PTR(U8, pCacheData + 1) + Off);          // MISRA deviation D:100[d]
  if (pBlockInfo->SectorIndex == SectorIndex) {
    FS_MEMCPY(pData, pBlockInfo + 1, SectorSize);
    return 0;             // Sector found
  }
  return 1;               // Sector not found
}

/*********************************************************************
*
*       _CacheMan_WriteCache
*
*  Function description
*    Writes sector to cache.
*
*  Return value
*    0    Not in write cache, the physical write operation still needs to be performed (Since this cache is a pure read-cache).
*/
static int _CacheMan_WriteCache(FS_DEVICE * pDevice, U32 SectorIndex, const void * pData, U8 SectorType) {
  U32                    Off;
  U32                    SectorSize;
  CACHE_MAN_DATA       * pCacheData;
  CACHE_MAN_BLOCK_INFO * pBlockInfo;

  pCacheData = SEGGER_PTR2PTR(CACHE_MAN_DATA, pDevice->Data.pCacheData);                                // MISRA deviation D:100[d]
  if (_ComputeNumSectors(pDevice, pCacheData) == 0u) {
    return 0;             // Device is not available
  }
  if (SectorType == FS_SECTOR_TYPE_MAN) {
    SectorSize  = pCacheData->SectorSize;
    Off         = _GetHashCode(SectorIndex, pCacheData->NumSectors) * (sizeof(CACHE_MAN_BLOCK_INFO) + SectorSize);
    pBlockInfo  = SEGGER_PTR2PTR(CACHE_MAN_BLOCK_INFO, SEGGER_PTR2PTR(U8, pCacheData + 1) + Off);       // MISRA deviation D:100[d]
    _WriteIntoCache(pBlockInfo, SectorIndex, pData, SectorSize);
  }
  return 0;
}

/*********************************************************************
*
*       _CacheMan_InvalidateCache
*
*  Function description
*    Invalidates all data in cache.
*/
static void _CacheMan_InvalidateCache(void * p) {
  CACHE_MAN_DATA * pCacheData;

  pCacheData = SEGGER_PTR2PTR(CACHE_MAN_DATA, p);                                                       // MISRA deviation D:100[d]
  _InvalidateCache(pCacheData);
  pCacheData->NumSectors = 0;
  pCacheData->SectorSize = 0;
}

/*********************************************************************
*
*       _CacheMan_Command
*
*  Function description
*    Executes a cache command.
*
*  Return value
*    ==0   Command executed.
*    !=0   An error occurred.
*/
static int _CacheMan_Command(FS_DEVICE * pDevice, int Cmd, void *p) {
  int r;

  FS_USE_PARA(pDevice);
  r  = -1;
  switch (Cmd) {
  case FS_CMD_CACHE_INVALIDATE:
    _CacheMan_InvalidateCache(p);
    r = 0;
    break;
  case FS_CMD_CACHE_GET_TYPE:
    {
      FS_CACHE_TYPE * pCacheType;

      pCacheType = SEGGER_PTR2PTR(FS_CACHE_TYPE, p);                                                    // MISRA deviation D:100[f]
      if (pCacheType != NULL) {
        *pCacheType = FS_CacheMan_Init;
        r = 0;
      }
    }
    break;
  case FS_CMD_CACHE_FREE_SECTORS:
    r = 0;
    break;
  case FS_CMD_CACHE_GET_NUM_SECTORS:
    {
      U32              NumSectors;
      U32            * pNumSectors;
      CACHE_MAN_DATA * pCacheData;

      pCacheData  = SEGGER_PTR2PTR(CACHE_MAN_DATA, pDevice->Data.pCacheData);                           // MISRA deviation D:100[d]
      pNumSectors = SEGGER_PTR2PTR(U32, p);                                                             // MISRA deviation D:100[f]
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
*       _CacheManAPI
*/
static const FS_CACHE_API _CacheManAPI = {
  _CacheMan_ReadFromCache,
  _CacheMan_WriteCache,
  _CacheMan_InvalidateCache,
  _CacheMan_Command,
  _CacheMan_WriteCache,
};

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_CacheMan_Init
*
*   Function description
*     Initializes the cache.
*/
U32 FS_CacheMan_Init(FS_DEVICE * pDevice, void * pData, I32 NumBytes) {
  FS_DEVICE_DATA * pDevData;
  U32              NumSectors;
  CACHE_MAN_DATA * pCacheData;
  U8             * pData8;
  U32              NumBytesCache;

  //
  // Sanity checks.
  //
  FS_DEBUG_ASSERT(FS_MTYPE_API, sizeof(CACHE_MAN_DATA)       == FS_SIZEOF_CACHE_MAN_DATA);              //lint !e506 !e774 Constant value Boolean and Boolean within 'if' always evaluates to False N:102.
  FS_DEBUG_ASSERT(FS_MTYPE_API, sizeof(CACHE_MAN_BLOCK_INFO) == FS_SIZEOF_CACHE_MAN_BLOCK_INFO);        //lint !e506 !e774 Constant value Boolean and Boolean within 'if' always evaluates to False N:102.
  //
  // Fill local variables.
  //
  pDevData      = &pDevice->Data;
  pData8        = SEGGER_PTR2PTR(U8, pData);                                                            // MISRA deviation D:100[d]
  NumBytesCache = (U32)NumBytes;
  //
  // Align pointer to a 32bit boundary
  //
  if ((SEGGER_PTR2ADDR(pData8) & 3u) != 0u) {                                                           // MISRA deviation D:103[b]
    NumBytesCache -= 4u - (SEGGER_PTR2ADDR(pData8) & 3u);                                               // MISRA deviation D:103[b]
    pData8        += 4u - (SEGGER_PTR2ADDR(pData8) & 3u);                                               // MISRA deviation D:103[b]
  }
  //
  // If less memory is available as we need to hold the
  // management structure, we leave everything as it is.
  // A cache module is then not attached to the device.
  //
  if (NumBytesCache < sizeof(CACHE_MAN_DATA)) {
    return 0;
  }
  pCacheData = SEGGER_PTR2PTR(CACHE_MAN_DATA, pData8);                                                  // MISRA deviation D:100[d]
  FS_MEMSET(pCacheData, 0, sizeof(CACHE_MAN_DATA));
  pDevData->pCacheAPI       = &_CacheManAPI;
  pDevData->pCacheData      = pCacheData;
  pCacheData->NumBytesCache = NumBytesCache;
  NumSectors = _ComputeNumSectors(pDevice, pCacheData);
  return NumSectors;
}
#else

/*********************************************************************
*
*       CacheMan_c
*
*  Function description
*    Dummy function to prevent compiler errors.
*/
void CacheMan_c(void);
void CacheMan_c(void) {}

#endif // FS_SUPPORT_CACHE

/*************************** End of file ****************************/
