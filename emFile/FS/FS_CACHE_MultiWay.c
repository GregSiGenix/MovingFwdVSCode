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
File    : FS_CACHE_MultiWay.c
Purpose : Cache module for the Logical Block Layer

Additional information:
  Strategy      : Read/write cache, caching all sectors (management, directory and data) equally.
  Associativity : Multi-way, configurable.
  Limitations   : None. This cache module can be used on any device with any file system.

  The cache is subdivided in so called sets. Each set can store N sectors
  where N is the configured associativity level. The associativity level
  is a power of 2 value (2, 4, 8...) The set number where a sector
  must be stored is calculated using the formula:

    SetNo = SectorIndex % NumSets

  The replacement policy is base on a LRU (Least Recently Used) algorithm.
  Each cache block has an access count. The access count is set to 0 each time
  the corresponding sector is read/updated. At the same time, the access counts
  of the other cache blocks in the set are incremented. The cache block in a set
  with the greatest access count will be replaced.

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
*       Defines, fixed
*
**********************************************************************
*/
#define ACCESS_CNT_MAX          0xFFFFu
#define ASSOC_LEVEL_DEFAULT     2     // Default associativity level. Runtime configurable.

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
static unsigned _ld(U32 Value) {
  unsigned i;

  for (i = 0; i < 16u; i++) {
    if ((1uL << i) == Value) {
      break;
    }
  }
  return i;
}

/*********************************************************************
*
*       _SectorIndexToSetNo
*
*  Function description
*    Computes the number of the set where a sector should be stored.
*/
static U32 _SectorIndexToSetNo(const CACHE_MULTI_WAY_DATA * pCacheData, U32 SectorIndex) {
  U32 NumSets;

  NumSets = pCacheData->NumSets;
  return SectorIndex % NumSets;
}

/*********************************************************************
*
*       _GetNumSectors
*
*  Function description
*    Computes the number of sectors which can be stored in the cache.
*/
static U32 _GetNumSectors(const CACHE_MULTI_WAY_DATA * pCacheData) {
  U32 NumSets;
  U32 ldAssocLevel;
  U32 NumSectors;

  NumSets      = pCacheData->NumSets;
  ldAssocLevel = pCacheData->ldAssocLevel;
  NumSectors   = NumSets << ldAssocLevel;
  return NumSectors;
}

/*********************************************************************
*
*       _InvalidateCache
*
*  Function description
*    Marks as invalid all sectors in the cache.
*/
static void _InvalidateCache(CACHE_MULTI_WAY_DATA * pCacheData) {
  U32                          SectorSize;
  U32                          NumSectors;
  CACHE_MULTI_WAY_BLOCK_INFO * pBlockInfo;

  SectorSize = pCacheData->SectorSize;
  NumSectors = _GetNumSectors(pCacheData);
  //
  // Visit each cache block and invalidate the data.
  //
  if (NumSectors != 0u) {
    pBlockInfo = SEGGER_PTR2PTR(CACHE_MULTI_WAY_BLOCK_INFO, pCacheData + 1);                                                  // MISRA deviation D:100[d]
    do {
      pBlockInfo->SectorIndex = SECTOR_INDEX_INVALID;
      pBlockInfo->AccessCnt   = 0;
      pBlockInfo->IsDirty     = 0;
      pBlockInfo              = SEGGER_PTR2PTR(CACHE_MULTI_WAY_BLOCK_INFO, SEGGER_PTR2PTR(U8, pBlockInfo + 1) + SectorSize);  // MISRA deviation D:100[d]
    } while (--NumSectors != 0u);
  }
}

/*********************************************************************
*
*       _UpdateNumSets
*
*  Function description
*    Computes the maximum number of sets which can be stored in the cache.
*    The value is saved to cache management data.
*
*  Return value
*    !=0      Number of sets which can be stored to cache.
*    ==0      An error occurred.
*/
static U32 _UpdateNumSets(FS_DEVICE * pDevice) {
  U32                    NumSets;
  U32                    NumSectors;
  U32                    SectorSize;
  U32                    NumBytes;
  U32                    ldAssocLevel;
  U32                    SizeofCacheData;
  U32                    SizeofBlockInfo;
  CACHE_MULTI_WAY_DATA * pCacheData;

  pCacheData = SEGGER_PTR2PTR(CACHE_MULTI_WAY_DATA, pDevice->Data.pCacheData);                                                // MISRA deviation D:100[d]
  //
  // First, check if we already computed the number of sets.
  //
  SectorSize = pCacheData->SectorSize;
  if (SectorSize != 0u) {
    return pCacheData->NumSets;   // OK, Number of sets already computed.
  }
  //
  // Ask the driver for the number of bytes in a sector.
  //
  SectorSize = FS_GetSectorSize(pDevice);
  if (SectorSize == 0u) {
    return 0;                     // Error, medium removed.
  }
  //
  // Sanity check. The cache size should be great enough to store the cache management data.
  //
  NumBytes = pCacheData->NumBytesCache;
  if (NumBytes < sizeof(CACHE_MULTI_WAY_DATA)) {
    return 0;                     // Error, cache to small.
  }
  //
  // Compute number of sets the cache is able to store.
  //
  ldAssocLevel    = pCacheData->ldAssocLevel;
  SizeofCacheData = sizeof(CACHE_MULTI_WAY_DATA);
  SizeofBlockInfo = sizeof(CACHE_MULTI_WAY_BLOCK_INFO);
  NumSectors      = (NumBytes - SizeofCacheData) / (SizeofBlockInfo + SectorSize);
  NumSets         = NumSectors >> ldAssocLevel;
  if (NumSets > 0u) {
    pCacheData->NumSets    = NumSets;
    pCacheData->SectorSize = SectorSize;
    _InvalidateCache(pCacheData);
  }
  return NumSets;
}

/*********************************************************************
*
*       _WriteIntoBlock
*
*  Function description
*    Modifies a cache block. Stores the sector data and the sector index.
*/
static void _WriteIntoBlock(CACHE_MULTI_WAY_BLOCK_INFO * pBlockInfo, U32 SectorIndex, const void * pData, U32 SectorSize, U8 IsDirty) {
  pBlockInfo->IsDirty     = (U16)IsDirty;
  pBlockInfo->SectorIndex = SectorIndex;
  FS_MEMCPY(pBlockInfo + 1, pData, SectorSize);
}

/*********************************************************************
*
*       _CleanBlock
*
*  Function description
*    Writes the sector data of a cache block to medium.
*/
static int _CleanBlock(FS_DEVICE * pDevice, const CACHE_MULTI_WAY_BLOCK_INFO * pBlockInfo) {
  int r;
  U32 SectorIndex;

  SectorIndex = pBlockInfo->SectorIndex;
  FS_DEBUG_LOG((FS_MTYPE_CACHE, "CMW: CLEAN VN: \"%s:%d:\", SI: %lu\n", pDevice->pType->pfGetName(pDevice->Data.Unit), pDevice->Data.Unit, SectorIndex));
  r = FS_LB_WriteBack(pDevice, SectorIndex, pBlockInfo + 1);
  return r;
}

/*********************************************************************
*
*       _CleanBlockIfRequired
*
*  Function description
*    Writes the sector data of a cache block to medium if it is marked as dirty.
*/
static int _CleanBlockIfRequired(FS_DEVICE * pDevice, CACHE_MULTI_WAY_BLOCK_INFO * pBlockInfo) {
  int r;

  r = 0;
  if ((pBlockInfo->SectorIndex != SECTOR_INDEX_INVALID) && (pBlockInfo->IsDirty != 0u)) {
    r = _CleanBlock(pDevice, pBlockInfo);
    if (r == 0) {
      pBlockInfo->IsDirty  = 0;
      pBlockInfo->SectorIndex = SECTOR_INDEX_INVALID;
    }
  }
  return r;
}

/*********************************************************************
*
*       _FindBlockBySectorIndex
*
*  Function description
*    Returns the cache block in a set where a given sector is stored.
*
*  Parameters
*    pCacheData     [IN]  Cache management data.
*    SetNo          Number of the set where the sector is stored.
*    SectorIndex    Index of the logical sector to look up for.
*
*  Return value
*    !=0      Pointer to the cache block where the sector is stored.
*    ==0      Sector is not stored in the set.
*/
static CACHE_MULTI_WAY_BLOCK_INFO * _FindBlockBySectorIndex(CACHE_MULTI_WAY_DATA * pCacheData, U32 SetNo, U32 SectorIndex) {
  U32                          SizeofSet;
  U32                          SectorSize;
  U32                          NumWays;
  U32                          ldAssocLevel;
  CACHE_MULTI_WAY_BLOCK_INFO * pBlockInfo;

  ldAssocLevel = pCacheData->ldAssocLevel;
  SectorSize   = pCacheData->SectorSize;
  //
  // Compute the position of the set in the cache.
  //
  SizeofSet    = (sizeof(CACHE_MULTI_WAY_BLOCK_INFO) + SectorSize) << ldAssocLevel;
  pBlockInfo   = SEGGER_PTR2PTR(CACHE_MULTI_WAY_BLOCK_INFO, SEGGER_PTR2PTR(U8, pCacheData + 1) + (SizeofSet * SetNo));        // MISRA deviation D:100[d]
  //
  // Search for the block containing the given sector number.
  //
  NumWays = 1uL << ldAssocLevel;
  do {
    if (pBlockInfo->SectorIndex == SectorIndex) {
      return pBlockInfo;
    }
    pBlockInfo = SEGGER_PTR2PTR(CACHE_MULTI_WAY_BLOCK_INFO, SEGGER_PTR2PTR(U8, pBlockInfo + 1) + SectorSize);                 // MISRA deviation D:100[d]
  } while (--NumWays != 0u);
  return NULL;
}

/*********************************************************************
*
*       _GetBlockToDiscard
*
*  Function description
*    Returns the cache block which can be discarded from a set.
*
*  Parameters
*    pCacheData     [IN]  Cache management data.
*    SetNo          Number of the set to search.
*
*  Return value
*    Pointer to the cache block to discard.
*/
static CACHE_MULTI_WAY_BLOCK_INFO * _GetBlockToDiscard(CACHE_MULTI_WAY_DATA * pCacheData, U32 SetNo) {
  U32                          SizeofSet;
  U32                          SectorSize;
  U32                          NumWays;
  U32                          ldAssocLevel;
  U16                          AccessCntMax;
  U16                          AccessCnt;
  U32                          SectorIndex;
  CACHE_MULTI_WAY_BLOCK_INFO * pBlockInfo;
  CACHE_MULTI_WAY_BLOCK_INFO * pBlockInfoLRU;

  ldAssocLevel = pCacheData->ldAssocLevel;
  SectorSize   = pCacheData->SectorSize;
  //
  // Compute the position of the set in the cache.
  //
  SizeofSet    = (sizeof(CACHE_MULTI_WAY_BLOCK_INFO) + SectorSize) << ldAssocLevel;
  pBlockInfo   = SEGGER_PTR2PTR(CACHE_MULTI_WAY_BLOCK_INFO, SEGGER_PTR2PTR(U8, pCacheData + 1) + (SizeofSet * SetNo));        // MISRA deviation D:100[d]
  //
  // Search for the block containing the least recently used sector. The access count is used for this purpose.
  // The block with the highest access count stores the LRU sector.
  //
  NumWays       = 1uL << ldAssocLevel;
  pBlockInfoLRU = pBlockInfo;
  AccessCntMax  = 0;
  do {
    SectorIndex = pBlockInfo->SectorIndex;
    if (SectorIndex == SECTOR_INDEX_INVALID) {
      pBlockInfoLRU = pBlockInfo;
      break;
    }
    AccessCnt = pBlockInfo->AccessCnt;
    if (AccessCnt > AccessCntMax) {
      AccessCntMax  = AccessCnt;
      pBlockInfoLRU = pBlockInfo;
    }
    pBlockInfo = SEGGER_PTR2PTR(CACHE_MULTI_WAY_BLOCK_INFO, SEGGER_PTR2PTR(U8, pBlockInfo + 1) + SectorSize);                 // MISRA deviation D:100[d]
  } while (--NumWays != 0u);
  return pBlockInfoLRU;
}

/*********************************************************************
*
*       _UpdateBlockAccessCnt
*
*  Function description
*    It modifies the access count of all blocks in a set.
*    First, it sets the access count of the block storing the given sector to 0.
*    Then the access counts of other blocks in the set are incremented.
*    The LRU sector will have the highest access count.
*
*  Parameters
*    pCacheData       [IN]  Cache management data.
*    SetNo            Number of the set to search.
*    SectorIndexLRU   Index of the last recently used logical sector.
*/
static void _UpdateBlockAccessCnt(CACHE_MULTI_WAY_DATA * pCacheData, U32 SetNo, U32 SectorIndexLRU) {
  U32                          SizeofSet;
  U32                          SectorSize;
  U32                          NumWays;
  U32                          ldAssocLevel;
  U16                          AccessCnt;
  U32                          SectorIndex;
  CACHE_MULTI_WAY_BLOCK_INFO * pBlockInfo;

  ldAssocLevel = pCacheData->ldAssocLevel;
  SectorSize   = pCacheData->SectorSize;
  //
  // Compute the position of the set in the cache.
  //
  SizeofSet    = (sizeof(CACHE_MULTI_WAY_BLOCK_INFO) + SectorSize) << ldAssocLevel;
  pBlockInfo   = SEGGER_PTR2PTR(CACHE_MULTI_WAY_BLOCK_INFO, SEGGER_PTR2PTR(U8, pCacheData + 1) + (SizeofSet * SetNo));        // MISRA deviation D:100[d]
  //
  // Search for the block containing the least recently used sector. The access count is used for this purpose.
  // The block with the highest access count stores the LRU sector.
  //
  NumWays = 1uL << ldAssocLevel;
  do {
    SectorIndex  = pBlockInfo->SectorIndex;
    if (SectorIndex != SECTOR_INDEX_INVALID) {
      AccessCnt = pBlockInfo->AccessCnt;
      if (SectorIndex == SectorIndexLRU) {
        AccessCnt = 0;
      } else {
        if (AccessCnt < ACCESS_CNT_MAX) {
          ++AccessCnt;
        }
      }
      pBlockInfo->AccessCnt = AccessCnt;
    }
    pBlockInfo = SEGGER_PTR2PTR(CACHE_MULTI_WAY_BLOCK_INFO, SEGGER_PTR2PTR(U8, pBlockInfo + 1) + SectorSize);                 // MISRA deviation D:100[d]
  } while (--NumWays != 0u);
}

/*********************************************************************
*
*       _SetMode
*
*  Function description
*    Sets the cache strategy for the given type of sectors.
*/
static int _SetMode(const FS_DEVICE * pDevice, const CACHE_MODE * pCacheMode) {
  unsigned               i;
  CACHE_MULTI_WAY_DATA * pCacheData;
  U32                    TypeMask;

  if (pCacheMode == NULL) {
    return -1;                  // Error, invalid parameter.
  }
  pCacheData = SEGGER_PTR2PTR(CACHE_MULTI_WAY_DATA, pDevice->Data.pCacheData);                                                // MISRA deviation D:100[d]
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
  U32                          NumSectors;
  U32                          SectorSize;
  CACHE_MULTI_WAY_DATA       * pCacheData;
  CACHE_MULTI_WAY_BLOCK_INFO * pBlockInfo;
  int                          r;
  int                          Result;

  pCacheData = SEGGER_PTR2PTR(CACHE_MULTI_WAY_DATA, pDevice->Data.pCacheData);                                                // MISRA deviation D:100[d]
  SectorSize = pCacheData->SectorSize;
  NumSectors = _GetNumSectors(pCacheData);
  r          = 0;
  if (NumSectors != 0u) {
    pBlockInfo = SEGGER_PTR2PTR(CACHE_MULTI_WAY_BLOCK_INFO, SEGGER_PTR2PTR(U8, pCacheData + 1));                              // MISRA deviation D:100[d]
    do {
      Result = _CleanBlockIfRequired(pDevice, pBlockInfo);
      if (Result != 0) {
        r = Result;
      }
      pBlockInfo = SEGGER_PTR2PTR(CACHE_MULTI_WAY_BLOCK_INFO, SEGGER_PTR2PTR(U8, pBlockInfo + 1) + SectorSize);               // MISRA deviation D:100[d]
    } while (--NumSectors != 0u);
  }
  return r;
}

/*********************************************************************
*
*       _SetAssocLevel
*
*  Function description
*    Modifies the associativity level of the cache.
*/
static int _SetAssocLevel(FS_DEVICE * pDevice, U32 AssocLevel) {
  int                    r;
  U32                    NumSets;
  CACHE_MULTI_WAY_DATA * pCacheData;

  r = 0;
  pCacheData = SEGGER_PTR2PTR(CACHE_MULTI_WAY_DATA, pDevice->Data.pCacheData);                                                // MISRA deviation D:100[d]
  pCacheData->ldAssocLevel = _ld(AssocLevel);
  pCacheData->SectorSize   = 0;   // Force the update of the number of sets.
  //
  // Update the number of sets in the cache management data.
  //
  NumSets = _UpdateNumSets(pDevice);
  if (NumSets == 0u) {
    r = 1;                        // Error, the cache has to be large enough to hold at least one set.
  }
  return r;
}

/*********************************************************************
*
*       _RemoveFromCache
*
*  Function description
*    Invalidates the data of a range of sectors in the cache.
*    This function does not write dirty data to medium. Data of dirty entries is discarded.
*    Typ. called when files and directories are removed.
*/
static void _RemoveFromCache(const FS_DEVICE * pDevice, U32 FirstSector, U32 NumSectors) {
  U32                          NumSectorsInCache;
  U32                          SectorIndex;
  U32                          SetNo;
  U32                          LastSector;
  U32                          SectorSize;
  CACHE_MULTI_WAY_DATA       * pCacheData;
  CACHE_MULTI_WAY_BLOCK_INFO * pBlockInfo;

  pCacheData        = SEGGER_PTR2PTR(CACHE_MULTI_WAY_DATA, pDevice->Data.pCacheData);                                         // MISRA deviation D:100[d]
  NumSectorsInCache = _GetNumSectors(pCacheData);
  LastSector        = FirstSector + NumSectors - 1u;
  SectorSize        = pCacheData->SectorSize;
  if (NumSectorsInCache != 0u) {
    //
    // Use the most efficient way to search for sectors in the cache.
    //
    if (NumSectors > NumSectorsInCache) {
      //
      // Loop through all sectors in the cache and remove the ones included in the given range.
      //
      pBlockInfo = SEGGER_PTR2PTR(CACHE_MULTI_WAY_BLOCK_INFO, pCacheData + 1);                                                // MISRA deviation D:100[d]
      do {
        SectorIndex = pBlockInfo->SectorIndex;
        if (SectorIndex != SECTOR_INDEX_INVALID) {
          if ((SectorIndex >= FirstSector) && (SectorIndex <= LastSector)) {
            pBlockInfo->SectorIndex = SECTOR_INDEX_INVALID;
            pBlockInfo->AccessCnt   = 0;
            pBlockInfo->IsDirty     = 0;
            FS_DEBUG_LOG((FS_MTYPE_CACHE, "CMW: REMOVE VN: \"%s:%d:\" SI: %lu\n", pDevice->pType->pfGetName(pDevice->Data.Unit), pDevice->Data.Unit, pBlockInfo->SectorIndex));
          }
        }
        pBlockInfo = SEGGER_PTR2PTR(CACHE_MULTI_WAY_BLOCK_INFO, SEGGER_PTR2PTR(U8, pBlockInfo + 1) + SectorSize);             // MISRA deviation D:100[d]
      } while (--NumSectorsInCache != 0u);
    } else {
      //
      // Take each sector form the range of sectors to be removed.
      //
      for (SectorIndex = FirstSector; SectorIndex <= LastSector; ++SectorIndex) {
        SetNo      = _SectorIndexToSetNo(pCacheData, SectorIndex);
        pBlockInfo = _FindBlockBySectorIndex(pCacheData, SetNo, SectorIndex);
        if (pBlockInfo != NULL) {
          pBlockInfo->SectorIndex = SECTOR_INDEX_INVALID;
          pBlockInfo->IsDirty     = 0;
          pBlockInfo->AccessCnt   = 0;
        }
      }
    }
  }
}

/*********************************************************************
*
*       Static code (callbacks)
*
**********************************************************************
*/

/*********************************************************************
*
*       _CacheMultiWay_ReadFromCache
*
*  Function description
*    Reads one sector from cache. If not found the function returns an error.
*
*  Return value
*    !=0      Sector not found
*    ==0      Sector found
*/
static int _CacheMultiWay_ReadFromCache(FS_DEVICE * pDevice, U32 SectorIndex, void * pData, U8 SectorType) {
  U32                          SectorSize;
  U32                          NumSets;
  U32                          SetNo;
  CACHE_MULTI_WAY_DATA       * pCacheData;
  CACHE_MULTI_WAY_BLOCK_INFO * pBlockInfo;

  FS_USE_PARA(SectorType);
  pCacheData = SEGGER_PTR2PTR(CACHE_MULTI_WAY_DATA, pDevice->Data.pCacheData);                                                // MISRA deviation D:100[d]
  NumSets    = _UpdateNumSets(pDevice);
  if (NumSets == 0u) {
    return 1;                          // Error, storage device is not available.
  }
  SectorSize = pCacheData->SectorSize;
  SetNo      = _SectorIndexToSetNo(pCacheData, SectorIndex);
  pBlockInfo = _FindBlockBySectorIndex(pCacheData, SetNo, SectorIndex);
  if (pBlockInfo != NULL) {
    _UpdateBlockAccessCnt(pCacheData, SetNo, SectorIndex);
    FS_MEMCPY(pData, pBlockInfo + 1, SectorSize);
    return 0;                         // OK, sector found.
  }
  return 1;                           // Error, sector not found.
}

/*********************************************************************
*
*       _CacheMultiWay_UpdateCache
*
*  Function description
*    Updates a sector in cache. Called after a READ operation to update the cache.
*
*  Return value
*    ==0      Sector updated.
*    !=0      An error occurred.
*/
static int _CacheMultiWay_UpdateCache(FS_DEVICE * pDevice, U32 SectorIndex, const void * pData, U8 SectorType) {
  U32                          SectorSize;
  unsigned                     CacheMode;
  U32                          NumSets;
  U32                          SetNo;
  CACHE_MULTI_WAY_DATA       * pCacheData;
  CACHE_MULTI_WAY_BLOCK_INFO * pBlockInfo;
  int                          r;

  pCacheData = SEGGER_PTR2PTR(CACHE_MULTI_WAY_DATA, pDevice->Data.pCacheData);                                                // MISRA deviation D:100[d]
  CacheMode  = pCacheData->aCacheMode[SectorType];
  NumSets    = _UpdateNumSets(pDevice);
  if (NumSets == 0u) {
    return 1;                                   // Error, device is not available.
  }
  r = 0;                                        // Set to indicate success.
  if ((CacheMode & FS_CACHE_MODE_R) != 0u) {    // Is the read cache active for this type of sector ?
    SectorSize = pCacheData->SectorSize;
    SetNo      = _SectorIndexToSetNo(pCacheData, SectorIndex);
    pBlockInfo = _FindBlockBySectorIndex(pCacheData, SetNo, SectorIndex);
    if (pBlockInfo == NULL) {
      //
      // If the sector is not in cache, find a block in the corresponding set where we can store it.
      //
      pBlockInfo = _GetBlockToDiscard(pCacheData, SetNo);
      //
      // If we replace an other, dirty sector, we need to write it out first.
      //
      if (pBlockInfo->SectorIndex != SectorIndex) {
        r = _CleanBlockIfRequired(pDevice, pBlockInfo);
      }
    }
    _WriteIntoBlock(pBlockInfo, SectorIndex, pData, SectorSize, 0);
    _UpdateBlockAccessCnt(pCacheData, SetNo, SectorIndex);
  }
  return r;
}

/*********************************************************************
*
*       _CacheMultiWay_WriteIntoCache
*
*  Function description
*    Writes a sector into cache.
*
*  Return value
*    ==0    Not  in write cache, the physical write operation still needs to be performed.
*    ==1    Data in write cache, the physical write operation does not need to be performed.
*/
static int _CacheMultiWay_WriteIntoCache(FS_DEVICE * pDevice, U32 SectorIndex, const void * pData, U8 SectorType) {
  U32                          SectorSize;
  U32                          NumSets;
  U32                          SetNo;
  unsigned                     CacheMode;
  U8                           IsWriteRequired;
  U8                           IsDirty;
  CACHE_MULTI_WAY_DATA       * pCacheData;
  CACHE_MULTI_WAY_BLOCK_INFO * pBlockInfo;
  int                          r;

  pCacheData = SEGGER_PTR2PTR(CACHE_MULTI_WAY_DATA, pDevice->Data.pCacheData);                                                // MISRA deviation D:100[d]
  NumSets    = _UpdateNumSets(pDevice);
  if (NumSets == 0u) {
    return 0;                                   // Error, device is not available.
  }
  CacheMode  = pCacheData->aCacheMode[SectorType];
  SectorSize = pCacheData->SectorSize;
  SetNo      = _SectorIndexToSetNo(pCacheData, SectorIndex);
  pBlockInfo = _FindBlockBySectorIndex(pCacheData, SetNo, SectorIndex);
  IsDirty    = 0;
  IsWriteRequired = 0;
  if ((CacheMode & FS_CACHE_MODE_W) != 0u) {    // Is the write cache active for this type of sector ?
    IsWriteRequired = 1;
  } else {
    if (pBlockInfo != NULL) {                   // Sector already in cache ?
      IsWriteRequired = 1;
    }
  }
  if (IsWriteRequired != 0u) {
    if (pBlockInfo == NULL) {
      //
      // If the sector is not in cache, find a block in the corresponding set where we can store it.
      //
      pBlockInfo = _GetBlockToDiscard(pCacheData, SetNo);
      //
      // If we replace an other, dirty sector, we need to write it out.
      //
      if (pBlockInfo->SectorIndex != SectorIndex) {
        r = _CleanBlockIfRequired(pDevice, pBlockInfo);
        if (r != 0) {
          return 0;                             // TBD: Improve the error handling.
        }
      }
    }
    if ((CacheMode & FS_CACHE_MODE_D) != 0u) {  // Delayed write allowed cache on for this type of sector ?
      IsDirty = 1;
    }
    _WriteIntoBlock(pBlockInfo, SectorIndex, pData, SectorSize, IsDirty);
    _UpdateBlockAccessCnt(pCacheData, SetNo, SectorIndex);
  }
  if (IsDirty != 0u) {
    return 1;                                   // Write is delayed (data in cache) and does not need to be performed.
  }
  return 0;                                     // Write still needs to be performed.
}

/*********************************************************************
*
*       _CacheMultiWay_InvalidateCache
*
*  Function description
*    Invalidates all the sectors in the cache.
*/
static void _CacheMultiWay_InvalidateCache(void * p) {
  CACHE_MULTI_WAY_DATA * pCacheData;

  pCacheData = SEGGER_PTR2PTR(CACHE_MULTI_WAY_DATA, p);                                                                       // MISRA deviation D:100[d]
  _InvalidateCache(pCacheData);
  pCacheData->NumSets = 0;
  pCacheData->SectorSize = 0;
}

/*********************************************************************
*
*       _CacheMultiWay_Command
*
*  Function description
*    Executes a command on the cache.
*
*  Return value
*    ==0      Command executed.
*    !=0      An error occurred.
*/
static int _CacheMultiWay_Command(FS_DEVICE * pDevice, int Cmd, void * p) {
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
    _CacheMultiWay_InvalidateCache(p);
    r = 0;
    break;
  case FS_CMD_CACHE_SET_ASSOC_LEVEL:
    {
      U32   AssocLevel;
      U32 * pAssocLevel;

      pAssocLevel = SEGGER_PTR2PTR(U32, p);                                                                                   // MISRA deviation D:100[f]
      if (pAssocLevel != NULL) {
        AssocLevel = *pAssocLevel;
        r = _SetAssocLevel(pDevice, AssocLevel);
      }
    }
    break;
  case FS_CMD_CACHE_GET_NUM_SECTORS:
    {
      U32                    NumSectors;
      U32                  * pNumSectors;
      CACHE_MULTI_WAY_DATA * pCacheData;

      pCacheData  = SEGGER_PTR2PTR(CACHE_MULTI_WAY_DATA, pDevice->Data.pCacheData);                                           // MISRA deviation D:100[d]
      pNumSectors = SEGGER_PTR2PTR(U32, p);                                                                                   // MISRA deviation D:100[f]
      if (pNumSectors != NULL) {
        NumSectors   = _GetNumSectors(pCacheData);
        *pNumSectors = NumSectors;
        r = 0;
      }
    }
    break;
  case FS_CMD_CACHE_FREE_SECTORS:
    {
      CACHE_FREE * pCacheFree;
      U32          FirstSector;
      U32          NumSectors;

      pCacheFree  = SEGGER_PTR2PTR(CACHE_FREE, p);                                                                            // MISRA deviation D:100[f]
      if (pCacheFree != NULL) {
        FirstSector = pCacheFree->FirstSector;
        NumSectors  = pCacheFree->NumSectors;
        _RemoveFromCache(pDevice, FirstSector, NumSectors);
      }
      r = 0;
    }
    break;
  case FS_CMD_CACHE_GET_TYPE:
    {
      FS_CACHE_TYPE * pCacheType;

      pCacheType = SEGGER_PTR2PTR(FS_CACHE_TYPE, p);                                                                          // MISRA deviation D:100[f]
      if (pCacheType != NULL) {
        *pCacheType = FS_CacheMultiWay_Init;
      }
      r = 0;
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
*       _CacheMultiWayAPI
*/
static const FS_CACHE_API _CacheMultiWayAPI = {
  _CacheMultiWay_ReadFromCache,
  _CacheMultiWay_UpdateCache,
  _CacheMultiWay_InvalidateCache,
  _CacheMultiWay_Command,
  _CacheMultiWay_WriteIntoCache
};

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_CacheMultiWay_Init
*
*  Function description
*    Initializes the cache.
*
*  Return value
*    Returns the number of cache blocks (Number of sectors that can be cached)
*/
U32 FS_CacheMultiWay_Init(FS_DEVICE * pDevice, void * pData, I32 NumBytes) {
  U32                     NumSectors;
  U32                     NumSets;
  U8                    * pData8;
  U16                     ldAssocLevel;
  FS_DEVICE_DATA        * pDevData;
  CACHE_MULTI_WAY_DATA  * pCacheData;
  U32                     NumBytesCache;

  //
  // Sanity checks.
  //
  FS_DEBUG_ASSERT(FS_MTYPE_API, sizeof(CACHE_MULTI_WAY_DATA)       == FS_SIZEOF_CACHE_MULTI_WAY_DATA);                        //lint !e506 !e774 Constant value Boolean and Boolean within 'if' always evaluates to False N:102.
  FS_DEBUG_ASSERT(FS_MTYPE_API, sizeof(CACHE_MULTI_WAY_BLOCK_INFO) == FS_SIZEOF_CACHE_MULTI_WAY_BLOCK_INFO);                  //lint !e506 !e774 Constant value Boolean and Boolean within 'if' always evaluates to False N:102.
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
  // If less memory is available as we need to hold the management structure, we leave everything as it is.
  // A cache module is then not attached to the device.
  //
  if (NumBytesCache < sizeof(CACHE_MULTI_WAY_DATA)) {
    return 0;               // Error, not enough memory to store the cache management data.
  }
  pCacheData = SEGGER_PTR2PTR(CACHE_MULTI_WAY_DATA, pData8);                                                                  // MISRA deviation D:100[d]
  FS_MEMSET(pCacheData, 0, sizeof(CACHE_MULTI_WAY_DATA));
  ldAssocLevel              = (U16)_ld(ASSOC_LEVEL_DEFAULT);
  pDevData->pCacheAPI       = &_CacheMultiWayAPI;
  pDevData->pCacheData      = pCacheData;
  pCacheData->NumBytesCache = NumBytesCache;
  pCacheData->ldAssocLevel  = ldAssocLevel;
  NumSets                   = _UpdateNumSets(pDevice);
  NumSectors                = NumSets << ldAssocLevel;
  return NumSectors;
}

#else

/*********************************************************************
*
*       CacheMultiWay_c
*
*  Function description
*    Dummy function to prevent compiler errors.
*/
void CacheMultiWay_c(void);
void CacheMultiWay_c(void) {}

#endif /* FS_SUPPORT_CACHE */

/*************************** End of file ****************************/
