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
File        : USBH_MEM.c
Purpose     : USB host, memory management
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/

#include "USBH_Int.h"
#include "USBH_Util.h"
#include "USBH_MEM.h"

/*********************************************************************
*
*       Defines
*
**********************************************************************
*/
#if USBH_DEBUG > 0
  #define USBH_MEM_MAGIC    0x8CF10EAC
#endif

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static USBH_MEM_POOL _aMemPool[2];

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _AddAreaToFreeList
*
*  Function description
*    Adds a memory area to the free list.
*
*  Parameters
*    pPool:        Pointer to the memory pool structure.
*    pMem:         Start of the memory area to be added to the free list.
*                  Must be 'MIN_BLOCK_SIZE'-aligned.
*    pEnd:         End of the memory area (pointer to the byte after the area).
*/
static void _AddAreaToFreeList(USBH_MEM_POOL *pPool, U8 *pMem, const U8 *pEnd) {
  int             i;
  U32             NumBytes;
  U32             Size;
  USBH_MEM_FREE_BLCK * p;

  NumBytes = (U32)(pEnd - pMem);                     //lint !e946 !e947 !e9033  N:100 D:103[e]
#if USBH_DEBUG
  USBH_MEMSET(pMem, 0xBB, NumBytes);
#endif
  for (i = (int)MAX_BLOCK_SIZE_INDEX; i >= 0; i--) {
    Size = MIN_BLOCK_SIZE << (unsigned)i;
    while (NumBytes >= Size) {
      NumBytes -= Size;
      p         = (USBH_MEM_FREE_BLCK *)pMem;        //lint !e9087 D:100[d]
      pMem     += Size;
      p->pNext  = pPool->apFreeList[i];
#if USBH_DEBUG
      p->Magic  = USBH_MEM_MAGIC;
#endif
      pPool->apFreeList[i] = p;
    }
  }
}

/*********************************************************************
*
*       USBH_MEM_POOL_Create
*
*  Function description
*    Creates a memory pool
*
*  Parameters
*    pPool:        Pointer to the memory pool structure to be initialized.
*    pMem:         Pointer to the memory area to be used for the pool.
*    NumBytes:     Size of the memory area in bytes.
*/
void USBH_MEM_POOL_Create(USBH_MEM_POOL * pPool, void * pMem, U32 NumBytes) {
  U32             NumBlocks;
  U8            * pMem8;
  U32             BytesToAlign;

  USBH_MEMSET(pPool, 0, sizeof(USBH_MEM_POOL));
  if (pMem == NULL) {
    return;
  }
  if (NumBytes < 2u * MIN_BLOCK_SIZE) {
    USBH_PANIC("Bad memory pool size");
  }
  pMem8 = (U8 *)pMem;                                              //lint !e9079  D:100[d]
  //
  // Make start of memory pool 'MIN_BLOCK_SIZE'-aligned
  //
  BytesToAlign = SEGGER_PTR2ADDR(pMem) & (MIN_BLOCK_SIZE - 1u);    // lint D:103[b]
  if (BytesToAlign != 0u) {
    BytesToAlign = MIN_BLOCK_SIZE - BytesToAlign;
    pMem8    += BytesToAlign;
    NumBytes -= BytesToAlign;
  }
  //
  // Calculate number of available blocks.
  //
  NumBlocks = NumBytes / (MIN_BLOCK_SIZE + 1u);
  pPool->pBaseAddr   = pMem8;
  pPool->pSizeIdxTab = pMem8 + (NumBlocks * MIN_BLOCK_SIZE);
  USBH_MEMSET(pPool->pSizeIdxTab, 0xFFu, NumBlocks);
  _AddAreaToFreeList(pPool, pMem8, pPool->pSizeIdxTab);
}

/*********************************************************************
*
*       _MEM_POOL_Reo
*
*  Function description
*    Reorganize free list and merge blocks if possible.
*
*  Parameters
*    pPool:        Pointer to the memory pool structure.
*/
static void _MEM_POOL_Reo(USBH_MEM_POOL * pPool) {
  unsigned i;
  unsigned j;
  unsigned NumBlocks;
  unsigned SizeIndex;
  U8     * pTab;
  U8     * pBaseAddr;

  pBaseAddr = pPool->pBaseAddr;
  if (pBaseAddr == NULL) {
    return;
  }
  USBH_OS_Lock(USBH_MUTEX_MEM);
  USBH_MEMSET(pPool->apFreeList, 0, sizeof(pPool->apFreeList));
  i = 0;
  j = 0;
  pTab      = pPool->pSizeIdxTab;
  NumBlocks = (unsigned)(pTab - pBaseAddr) / MIN_BLOCK_SIZE;    //lint !e946 !e947 !e9033  N:100 D:103[e]
  while (j < NumBlocks) {
    SizeIndex = pTab[j];
    if (SizeIndex <= MAX_BLOCK_SIZE_INDEX) {
      if (i < j) {
        _AddAreaToFreeList(pPool, pBaseAddr + i * MIN_BLOCK_SIZE, pBaseAddr + j * MIN_BLOCK_SIZE);
      }
      j += (1uL << SizeIndex);
      i = j;
      continue;
    }
    if (SizeIndex != 0xFFu) {
      USBH_PANIC("USBH_MEM: Size index table corrupted");
    }
    j++;
  }
  if (i < j) {
    _AddAreaToFreeList(pPool, pBaseAddr + i * MIN_BLOCK_SIZE, pBaseAddr + j * MIN_BLOCK_SIZE);
  }
  USBH_OS_Unlock(USBH_MUTEX_MEM);
}

/*********************************************************************
*
*       USBH_MEM_POOL_Alloc
*
*  Function description
*    Allocates a memory block from a pool.
*
*  Parameters
*    pPool:        Pointer to the memory pool structure.
*    NumBytesUser: Requested size of the memory block.
*    Alignment:    Bits 0..23: Alignment of the memory block. Must be a power of 2 (or 0).
*                  Bits 24..31: Page boundary requirement: 0 means no requirement.
*                               n > 0 means: Allocated memory must not span 2K * 2^n page boundary.
*
*  Return value
*    Pointer to the allocated memory block or NULL if no memory found.
*/
void * USBH_MEM_POOL_Alloc(USBH_MEM_POOL * pPool, U32 NumBytesUser, U32 Alignment) {
  unsigned              i;
  unsigned              SizeIndex;
  USBH_MEM_FREE_BLCK  * p;
  USBH_MEM_FREE_BLCK  * pPrev;
  U8                  * pAlloc;
  U8                  * pStartFree;
  U8                  * pEndFree;
  U8                  * pAligned;
  U32                   NumBytes;
  PTR_ADDR              Aligned;
  U32                   BoundaryMask;

#if USBH_REO_FREE_MEM_LIST > 0
  if (pPool->MemReoScheduled != 0) {
    _MEM_POOL_Reo(pPool);
    pPool->MemReoScheduled = 0;
  }
#endif
  //
  // Upper 8 bits of 'Alignment' contain boundary page requirement:
  // 1 = 4K, 2 = 8K, ..., n = 2K * 2^n
  //
  BoundaryMask = Alignment >> 24;
  if (BoundaryMask != 0u) {
    BoundaryMask = 0x800uL << BoundaryMask;
    Alignment &= 0xFFFFFFuL;
  }
  --BoundaryMask;
  //
  // Check Alignment.
  //
  if (Alignment <= MIN_BLOCK_SIZE) {
    //
    // Always correct aligned.
    //
    Alignment = 0;
  } else {
    //
    // Create bit mask for alignment test.
    //
    Alignment--;
    if ((Alignment & (MIN_BLOCK_SIZE - 1u)) != (MIN_BLOCK_SIZE - 1u)) {
      USBH_PANIC("Alloc: Bad alignment");
    }
  }
  //
  // Find index in free list and calculate block size to allocate.
  //
  NumBytes = MIN_BLOCK_SIZE;
  for (SizeIndex = 0; SizeIndex <= MAX_BLOCK_SIZE_INDEX; SizeIndex++) {
    if (NumBytesUser <= NumBytes) {
      break;
    }
    NumBytes <<= 1;
  }
  //
  // Find free memory block.
  //
  USBH_OS_Lock(USBH_MUTEX_MEM);
  for (i = SizeIndex; i <= MAX_BLOCK_SIZE_INDEX; i++) {
    pPrev = NULL;
    p = pPool->apFreeList[i];
    while (p != NULL) {
#if USBH_DEBUG
      if (p->Magic != USBH_MEM_MAGIC) {
        USBH_PANIC("USBH_MEM: Free list corrupted");
      }
#endif
      pAlloc   = (U8 *)p;
      pEndFree = pAlloc + (MIN_BLOCK_SIZE << i);
      if ((SEGGER_PTR2ADDR(p) & Alignment) != 0u) {    // lint D:103[b]
        //
        // Free memory block has sufficient size but is not aligned as requested.
        // Check if the requested memory block can be placed aligned inside the free memory block.
        //
        Aligned  = (SEGGER_PTR2ADDR(p) | Alignment) + 1u;                                // lint D:103[b]
        pAligned = SEGGER_ADDR2PTR(U8, Aligned);                                         // lint D:103[b]
        if (pAligned + NumBytes > pEndFree || pAligned <= (U8 *)p) {                     //lint !e946 N:100
          goto Next;
        }
        pAlloc = pAligned;
      }
      //
      // Check for page boundary
      //
      if ((SEGGER_PTR2ADDR(pAlloc) & BoundaryMask) + NumBytes - 1u <= BoundaryMask) {    // lint D:103[b]
        goto Found;
      }
      //
      // Try to align to page boundary
      //
      Aligned  = (SEGGER_PTR2ADDR(p) | BoundaryMask) + 1u;                               // lint D:103[b]
      pAligned = SEGGER_ADDR2PTR(U8, Aligned);                                           // lint D:103[b]
      if (pAligned + NumBytes <= pEndFree && pAligned > (U8 *)p) {                       //lint !e946 N:100
        pAlloc = pAligned;
        goto Found;
      }
Next:
      //
      // No suitable memory block found, try next one in free list.
      //
      pPrev = p;
      p = p->pNext;
    }
  }
  //
  // No memory found.
  //
  USBH_WARN((USBH_MCAT_MEM, "No memory available (free mem %u, transfer mem %u, NumBytesUser %u, NumBytes %u)", USBH_MEM_GetFree(0), USBH_MEM_GetFree(1), NumBytesUser, NumBytes));
  pAlloc = NULL;
  goto End;
Found:
  //
  // Unlink block from free list.
  //
  if (pPrev != NULL) {
    pPrev->pNext = p->pNext;
  } else {
    pPool->apFreeList[i] = p->pNext;
  }
#if USBH_DEBUG
  p->Magic = 0;
#endif
  pPool->pSizeIdxTab[(U32)(pAlloc - pPool->pBaseAddr) / MIN_BLOCK_SIZE] = SizeIndex;      //lint !e946 !e947 !e9033  N:100 D:103[e]
  //
  // Store unused memory after the allocated block back to the free list.
  //
  pStartFree = pAlloc + NumBytes;
  if (pEndFree > pStartFree) {                                                            //lint !e946 N:100
    _AddAreaToFreeList(pPool, pStartFree, pEndFree);
  }
  //
  // Store unused memory before the allocated block back to the free list.
  //
  if (pAlloc > (U8 *)p) {                                                                 //lint !e946 N:100
    _AddAreaToFreeList(pPool, (U8 *)p, pAlloc);
  }
#if USBH_DEBUG
  pPool->UsedMem += NumBytes;
  if (pPool->MaxUsedMem < pPool->UsedMem) {
    pPool->MaxUsedMem = pPool->UsedMem;
  }
#endif
End:
  USBH_OS_Unlock(USBH_MUTEX_MEM);
  return pAlloc;
}

/*********************************************************************
*
*       USBH_MEM_POOL_Free
*
*  Function description
*    Frees a memory block, putting it back into the pool.
*    The memory must have been allocated from this pool before.
*
*  Parameters
*    pPool:        Pointer to the memory pool structure.
*    p:            Pointer to the memory block to be freed.
*/
void USBH_MEM_POOL_Free(USBH_MEM_POOL * pPool, U8 *p) {
  unsigned              i;
  USBH_MEM_FREE_BLCK  * pFree;
  unsigned              SizeIndex;

  i = (unsigned)(p - pPool->pBaseAddr) / MIN_BLOCK_SIZE;        //lint !e946 !e947 !e9033  N:100 D:103[e]
  SizeIndex = pPool->pSizeIdxTab[i];
#if USBH_DEBUG
  if (SizeIndex > MAX_BLOCK_SIZE_INDEX) {
    USBH_PANIC("_MEM_POOL_Free: Bad pointer");
  }
  USBH_MEMSET(p, 0xCC, MIN_BLOCK_SIZE << SizeIndex);
#endif
  pPool->pSizeIdxTab[i] = 0xFF;
  pFree = (USBH_MEM_FREE_BLCK *)p;             //lint !e9087  D:100[d]
  USBH_OS_Lock(USBH_MUTEX_MEM);
  pFree->pNext = pPool->apFreeList[SizeIndex];
  pPool->apFreeList[SizeIndex] = pFree;
#if USBH_DEBUG
  pFree->Magic = USBH_MEM_MAGIC;
  pPool->UsedMem -= (MIN_BLOCK_SIZE << SizeIndex);
#endif
  USBH_OS_Unlock(USBH_MUTEX_MEM);
}

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       USBH_MEM_Panic
*
*  Function description
*    Called by the USB host stack, if memory allocation fails during initialization.
*    The function halts the system, indicating a fatal error.
*    After successful initialization using USBH_Init(), USBH_MEM_Panic is never called.
*
*  Additional information
*    An application may implement its own USBH_MEM_Panic() function, when setting
*    '#define USBH_USE_APP_MEM_PANIC 1' in USB_Conf.h.
*/
#if USBH_USE_APP_MEM_PANIC == 0
void USBH_MEM_Panic(void) {                       //lint -esym(522,USBH_MEM_Panic) Is not dead code  N:100
  USBH_PANIC("No memory available");
  USBH_HALT;
}
#endif

/*********************************************************************
*
*       USBH_TryMalloc
*
*  Function description
*    Tries to allocate a memory block.
*    Failures are permitted and return NULL pointer.
*
*  Parameters
*    Size:    Requested size of the memory block.
*    sFunc:   Function name (C-string), for debugging only.
*    sFile:   File name (C-string), for debugging only.
*    Line:    Line number in source file, for debugging only.
*/
void * USBH_TryMalloc(U32 Size
#if USBH_MEM_TRACE
                     , const char * sFunc, const char * sFile, int Line
#endif
                     ) {
  void * p;

  if (_aMemPool[0].pBaseAddr == NULL) {
    USBH_PANIC("No memory was assigned to standard memory pool");
  }
  if (Size == 0u) {
#if USBH_MEM_TRACE
    USBH_WARN((USBH_MCAT_MEM, "USBH_MEM Alloc with zero bytes, called in %s, from %s@%d", sFunc, USBH_Basename(sFile), Line));
#else
    USBH_WARN((USBH_MCAT_MEM, "USBH_MEM Alloc with zero bytes."));
#endif
  }
  p = USBH_MEM_POOL_Alloc(&_aMemPool[0], Size, 1);
#if USBH_MEM_TRACE
  if (p) {
    USBH_LOG((USBH_MCAT_MEM, "USBH_MEM[0x%x] Alloc(%u), called in %s, from %s@%d", p, Size, sFunc, USBH_Basename(sFile), Line));
  } else {
    USBH_WARN((USBH_MCAT_MEM, "USBH_MEM[FAIL] Alloc(%u), called in %s, from %s@%d", Size, sFunc, USBH_Basename(sFile), Line));
  }
#endif
  return p;
}

/*********************************************************************
*
*       USBH_Malloc
*
*  Function description
*    Tries to allocate a memory block.
*    Failure is NOT permitted and causes panic.
*
*  Parameters
*    Size:    Requested size of the memory block.
*    sFunc:   Function name (C-string), for debugging only.
*    sFile:   File name (C-string), for debugging only.
*    Line:    Line number in source file, for debugging only.
*/
void * USBH_Malloc(U32 Size
#if USBH_MEM_TRACE
                     , const char * sFunc, const char * sFile, int Line
#endif
                     ) {
  void * p;

#if USBH_MEM_TRACE
  p = USBH_TryMalloc(Size, sFunc, sFile, Line);
#else
  p = USBH_TryMalloc(Size);
#endif
  if (p == NULL) {
    USBH_MEM_Panic();
  }
  return p;
}

/*********************************************************************
*
*       USBH_MallocZeroed
*
*  Function description
*    Allocates zeroed memory blocks.
*    Failure is NOT permitted and causes panic.
*
*  Parameters
*    Size:    Requested size of the memory block.
*    sFunc:   Function name (C-string), for debugging only.
*    sFile:   File name (C-string), for debugging only.
*    Line:    Line number in source file, for debugging only.
*/
void * USBH_MallocZeroed(U32 Size
#if USBH_MEM_TRACE
                     , const char * sFunc, const char * sFile, int Line
#endif
                     ) {
  void * p;

#if USBH_MEM_TRACE
  p = USBH_Malloc(Size, sFunc, sFile, Line);
#else
  p = USBH_Malloc(Size);
#endif
  USBH_MEMSET(p, 0, Size);
  return p;
}

/*********************************************************************
*
*       USBH_TryMallocZeroed
*
*  Function description
*    Allocates zeroed memory blocks.
*    Failures are permitted and return NULL pointer.
*
*  Parameters
*    Size:    Requested size of the memory block.
*    sFunc:   Function name (C-string), for debugging only.
*    sFile:   File name (C-string), for debugging only.
*    Line:    Line number in source file, for debugging only.
*/
void * USBH_TryMallocZeroed(U32 Size
#if USBH_MEM_TRACE
                     , const char * sFunc, const char * sFile, int Line
#endif
                     ) {
  void * p;

#if USBH_MEM_TRACE
  p = USBH_TryMalloc(Size, sFunc, sFile, Line);
#else
  p = USBH_TryMalloc(Size);
#endif
  if (p != NULL) {
    USBH_MEMSET(p, 0, Size);
  }
  return p;
}

/*********************************************************************
*
*       USBH_Free
*
*  Function description
*    Deallocates or frees a memory block.
*
*  Parameters
*    pMemBlock:    Pointer to the memory area to be freed.
*                  Must be previously returned by any of the malloc functions.
*    sFunc:   Function name (C-string), for debugging only.
*    sFile:   File name (C-string), for debugging only.
*    Line:    Line number in source file, for debugging only.
*/
void USBH_Free(void * pMemBlock
#if USBH_MEM_TRACE
                     , const char * sFunc, const char * sFile, int Line
#endif
                     ) {
  U8              * pMem;
  USBH_MEM_POOL   * pPool;
  unsigned          iPool;

#if USBH_MEM_TRACE
  USBH_LOG((USBH_MCAT_MEM, "USBH_MEM[%p] Free, called in %s, from %s@%d", pMemBlock, sFunc, USBH_Basename(sFile), Line));
#endif
  if ((SEGGER_PTR2ADDR(pMemBlock) & (MIN_BLOCK_SIZE - 1u)) != 0u) {     // lint D:103[b]
    USBH_PANIC("USBH_Free(): Bad pointer");
  }
  //
  //  Iterate over all memory pools and check, from which pool, this memory pool was allocated.
  //
  pMem = (U8 *)pMemBlock;                                                                               //lint !e9079  D:100[d]
  for (iPool = 0; iPool < SEGGER_COUNTOF(_aMemPool); iPool++) {
    pPool = &_aMemPool[iPool];
    if (pPool->pBaseAddr != NULL && SEGGER_PTR2ADDR(pMem) >= SEGGER_PTR2ADDR(pPool->pBaseAddr) &&       // lint D:103[c]
                                    SEGGER_PTR2ADDR(pMem) < SEGGER_PTR2ADDR(pPool->pSizeIdxTab)) {      // lint D:103[c]
      USBH_MEM_POOL_Free(pPool, pMem);
      return;
    }
  }
  USBH_PANIC("USBH_Free(): Bad pointer");
}

/*********************************************************************
*
*       USBH_AssignMemory
*
*  Function description
*    Assigns a memory area that will be used by the memory management
*    functions for allocating memory.
*    This function must be called in the initialization phase.
*
*  Parameters
*    pMem:         Pointer to the memory area.
*    NumBytes:     Size of the memory area in bytes.
*
*  Additional information
*    emUSB-Host comes with its own dynamic memory allocator optimized for its needs.
*    This function is used to set up up a memory area for the heap. The best place to
*    call it is in the USBH_X_Config() function.
*
*    For some USB host controllers additionally a separate memory heap for DMA memory
*    must be provided by calling USBH_AssignTransferMemory().
*/
void USBH_AssignMemory(void * pMem, U32 NumBytes) {
  USBH_MEM_POOL_Create(&_aMemPool[0], pMem, NumBytes);
}

/*********************************************************************
*
*       USBH_AssignTransferMemory
*
*  Function description
*    Assigns a memory area for a heap that will be used for allocating DMA memory.
*    This function must be called in the initialization phase.
*
*    The memory area provided to this function must fulfill the following requirements:
*    * Not cachable/bufferable.
*    * Fast access to avoid timeouts.
*    * USB-Host controller must have full read/write access.
*    * Cache aligned
*
*    If the physical address is not equal to the virtual address of the memory area
*    (address translation by an MMU), additionally a mapping function must be
*    installed using USBH_Config_SetV2PHandler().
*
*  Parameters
*    pMem:         Pointer to the memory area (virtual address).
*    NumBytes:     Size of the memory area in bytes.
*
*  Additional information
*    Use of this function is required only in systems in which "normal" default memory
*    does not fulfill all of these criteria.
*    In simple microcontroller systems without cache, MMU and external RAM, use of this
*    function is not required. If no transfer memory is assigned, memory assigned with
*    USBH_AssignMemory() is used instead.
*/
void USBH_AssignTransferMemory(void * pMem, U32 NumBytes) {
  USBH_MEM_POOL_Create(&_aMemPool[1], pMem, NumBytes);
}

/*********************************************************************
*
*       USBH_TryAllocTransferMemory
*
*  Function description
*    Allocates a block of memory which can be used for transfers.
*
*  Parameters
*    NumBytes:     Requested size of the memory block.
*    Alignment:    Alignment of the memory block. Must be a power of 2 (or 0).
*    sFunc:   Function name (C-string), for debugging only.
*    sFile:   File name (C-string), for debugging only.
*    Line:    Line number in source file, for debugging only.
*
*  Return value
*    Pointer to the allocated memory block or NULL if no memory found.
*/
void * USBH_TryAllocTransferMemory(U32 NumBytes, unsigned Alignment
#if USBH_MEM_TRACE
                     , const char * sFunc, const char * sFile, int Line
#endif
                     ) {
  void          * r;
  USBH_MEM_POOL * pPool;
  //
  // Allocate memory from transfer pool. If no pool has been defined for transfer memory,
  // we assume that regular memory can be used.
  //
  if (_aMemPool[1].pBaseAddr != NULL) {
    pPool = &_aMemPool[1];
  } else {
    pPool = &_aMemPool[0];
  }
  r = USBH_MEM_POOL_Alloc(pPool, NumBytes, Alignment);
#if USBH_MEM_TRACE
  if (r) {
    USBH_LOG((USBH_MCAT_MEM, "USBH_MEM[0x%x] Alloc(%u), called in %s, from %s@%d", r, NumBytes, sFunc, USBH_Basename(sFile), Line));
  } else {
    USBH_WARN((USBH_MCAT_MEM, "USBH_MEM[FAIL] Alloc(%u), called in %s, from %s@%d", NumBytes, sFunc, USBH_Basename(sFile), Line));
  }
#endif
  return r;
}

/*********************************************************************
*
*       USBH_AllocTransferMemory
*
*  Function description
*    Allocates a block of memory which can be used for transfers.
*    Failure is NOT permitted and causes panic.
*
*  Parameters
*    NumBytes:     Requested size of the memory block.
*    Alignment:    Alignment of the memory block. Must be a power of 2 (or 0).
*    sFunc:   Function name (C-string), for debugging only.
*    sFile:   File name (C-string), for debugging only.
*    Line:    Line number in source file, for debugging only.
*
*  Return value
*    Pointer to the allocated memory block.
*/
void * USBH_AllocTransferMemory(U32 NumBytes, unsigned Alignment
#if USBH_MEM_TRACE
                     , const char * sFunc, const char * sFile, int Line
#endif
                     ) {
  void     * r;

#if USBH_MEM_TRACE
  r = USBH_TryAllocTransferMemory(NumBytes, Alignment, sFunc, sFile, Line);
#else
  r = USBH_TryAllocTransferMemory(NumBytes, Alignment);
#endif
  if (r == NULL) {
    USBH_MEM_Panic();
  }
  return r;
}

/*********************************************************************
*
*       USBH_MEM_GetFree
*
*  Function description
*    Returns free memory of memory pool in bytes.
*
*  Parameters
*    Idx:          Index of memory pool.
*                  * 0 - normal memory
*                  * 1 - transfer memory.
*
*  Return value
*    Number of free bytes in memory pool.
*/
U32 USBH_MEM_GetFree(int Idx) {
  unsigned i;
  unsigned Cnt;
  U32 FreeMem;
  U32 BlockSize;
  USBH_MEM_FREE_BLCK * p;

  FreeMem   = 0;
  BlockSize = MIN_BLOCK_SIZE;
  for (i = 0; i <= MAX_BLOCK_SIZE_INDEX; i++) {
    Cnt = 0;
    for (p = _aMemPool[Idx].apFreeList[i]; p != NULL; p = p->pNext) {
      FreeMem += BlockSize;
      Cnt++;
    }
    if (Cnt != 0u) {
      USBH_LOG((USBH_MCAT_MEM, "FreeMem[%d]: %u x %u", Idx, Cnt, BlockSize));
    }
    BlockSize <<= 1;
  }
  return FreeMem;
}

/*********************************************************************
*
*       USBH_MEM_GetUsed
*
*  Function description
*    Returns free memory of memory pool in bytes.
*
*  Parameters
*    Idx:          Index of memory pool.
*                  * 0 - normal memory
*                  * 1 - transfer memory.
*
*  Return value
*    Number of allocated bytes in memory pool.
*/
U32 USBH_MEM_GetUsed(int Idx) {
  return (U32)(_aMemPool[Idx].pSizeIdxTab - _aMemPool[Idx].pBaseAddr) - USBH_MEM_GetFree(Idx);    //lint !e946 !e947 !e9033  N:100 D:103[e]
}

/*********************************************************************
*
*       USBH_MEM_GetMaxUsed
*
*  Function description
*    Returns the maximum used memory since initialization of the memory pool.
*
*  Parameters
*    Idx:          Index of memory pool.
*                  * 0 - normal memory
*                  * 1 - transfer memory.
*
*  Return value
*    Maximum used memory in bytes.
*
*  Additional information
*    This function only works in a debug configuration of emUSB-Host.
*    If compiled as release configuration, this function always returns 0.
*/
U32 USBH_MEM_GetMaxUsed(int Idx) {
  U32 Ret;

#if USBH_DEBUG
  Ret = _aMemPool[Idx].MaxUsedMem;
  Ret += Ret / MIN_BLOCK_SIZE;
#else
  //
  // Function not supported in release build.
  //
  USBH_USE_PARA(Idx);
  Ret = 0;
#endif
  return Ret;
}

/*********************************************************************
*
*       USBH_MEM_ReoFree
*
*  Function description
*    Reorganize free list and merge blocks if possible.
*
*  Parameters
*    Idx:          Index of memory pool (0 or 1)
*/
void USBH_MEM_ReoFree(int Idx) {
  _MEM_POOL_Reo(&_aMemPool[Idx]);
}

/*********************************************************************
*
*       USBH_MEM_ScheduleReo
*
*  Function description
*    Schedule reorganization of free memory list.
*/
void USBH_MEM_ScheduleReo(void) {
#if USBH_REO_FREE_MEM_LIST > 0
  _aMemPool[0].MemReoScheduled = 1;
  _aMemPool[1].MemReoScheduled = 1;
#endif
}

/*************************** End of file ****************************/
