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
File        : USBH_Util.c
Purpose     : USB helper functions
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#include "USBH_Util.h"
#include "USBH_Int.h"

/*********************************************************************
*
*       Defines, fixed
*
**********************************************************************
*/
#define USBH_NUMBITS_IN_U32  32u

/*********************************************************************
*
*       Global data
*
**********************************************************************
*/
#if USBH_DEBUG > 1
U32 USBH_XXLogTab[128];
U32 USBH_XXLogCnt;
#endif

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _CalcBitsSet
*
*  Function description
*    Calculates the number ones set in a U32 value.
*
*  Parameters
*    x     : Value to examine.
*
*  Return value
*    Number of bit set.
*/
static unsigned _CalcBitsSet(unsigned int x) {
  x -= ((x >> 1) & 0x55555555u);
  x = (((x >> 2) & 0x33333333u) + (x & 0x33333333u));
  x = (((x >> 4) + x) & 0x0F0F0F0Fu);
  x += (x >> 8);
  x += (x >> 16);
  return (x & 0x0000003Fu);
}

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       USBH_BITFIELD_CalcNumBitsUsed
*
*  Function description
*    Computes the number of bits used to store the given value.
*
*  Parameters
*    Value : Value to count the bits in.
*
*  Return value
*    Number of bits counted.
*/
unsigned USBH_BITFIELD_CalcNumBitsUsed(U32 Value) {
  unsigned r;

  r = 0;
  do {
    r++;
    Value >>= 1;
  } while (Value != 0u);
  return r;
}

/*********************************************************************
*
*      USBH_BITFIELD_ReadEntry
*
*  Function description
*    Reads a single entry of <NumBits> from the bit field
*
*  Parameters
*    pBase   : Pointer to the start of the bit field buffer.
*    Index   : Index of the value inside the bit field.
*    NumBits : The size of the entry in bits.
*
*  Return value
*    The computed entry value.
*/
U32 USBH_BITFIELD_ReadEntry(const U8 * pBase, U32 Index, unsigned NumBits) {
  U32 v;
  U32 Off;
  U32 OffEnd;
  U32 Mask;
  U32 BitOff;
  U32 i;

  BitOff = Index * NumBits;
  Off    = BitOff >> 3;
  OffEnd = (BitOff + NumBits - 1u) >> 3;
  pBase += Off;
  i = OffEnd - Off;
  //
  // Read data little endian
  //
  v = *pBase++;
  if (i != 0u) {
    unsigned Shift = 0;
    do {
      Shift += 8u;
      v     |= (U32)*pBase++ << Shift;
    } while (--i != 0u);
  }
  //
  // Shift, mask & return result
  //
  v    >>= (BitOff & 7u);
  Mask   = (1uL << NumBits) - 1u;
  v &= Mask;
  return v;
}

/*********************************************************************
*
*      USBH_BITFIELD_WriteEntry
*
*  Function description
*    Writes a single entry of <NumBits> into the bit field.
*
*  Parameters
*    pBase   : Pointer to the start of the bit field buffer.
*    Index   : Index of the value inside the bit field.
*    NumBits : The size of the entry in bits.
*    v       : Value to write.
*/
void USBH_BITFIELD_WriteEntry(U8 * pBase, U32 Index, unsigned NumBits, U32 v) {
  U32   Mask;
  U8  * p;
  U32   u;
  U32   BitOff;

  BitOff = Index * NumBits;
  p      = (U8 *)pBase + (BitOff >> 3);
  Mask   = (1uL << NumBits) - 1u;
  Mask <<= (BitOff & 7u);
  v    <<= (BitOff & 7u);
  //
  // Read, mask, or and write data little endian byte by byte.
  //
  do {
    u  = *p;
    u &= ~Mask;
    u |= v;
    *p = (U8)u;
    p++;
    Mask  >>= 8;
    v     >>= 8;
  } while (Mask != 0u);
}

/*********************************************************************
*
*       USBH_BITFIELD_CalcSize
*
*  Function description
*    Returns the size of bit field in bytes.
*
*  Parameters
*    NumItems    : Number of values in the bit field.
*    BitsPerItem : Size of each item in bits.
*
*  Return value
*    Size of the bit field in bytes.
*/
unsigned USBH_BITFIELD_CalcSize(U32 NumItems, unsigned BitsPerItem) {
  unsigned v;
  v =  NumItems * BitsPerItem;  // Compute the number of bits used for storage
  v = (v + 7u) >> 3;            // Convert into bytes
  return v;
}

/*********************************************************************
*
*       USBH_CountLeadingZeros
*
*  Function description
*    Returns the number of leading zeros in a 32-bit value.
*
*  Parameters
*    Value : Value to count the zeros in.
*
*  Return value
*    Computed number of leading zeros.
*/
unsigned USBH_CountLeadingZeros(U32 Value) {
  Value |= (Value >> 1);
  Value |= (Value >> 2);
  Value |= (Value >> 4);
  Value |= (Value >> 8);
  Value |= (Value >> 16);
  return(USBH_NUMBITS_IN_U32 - _CalcBitsSet(Value));
}

/*********************************************************************
*
*       USBH_ReadReg8
*/
U8 USBH_ReadReg8(const U8 * pAddr) {
  U8     r;
  r  = *(pAddr);
  return r;
}

/*********************************************************************
*
*       USBH_ReadReg16
*/
U16 USBH_ReadReg16(const U16 * pAddr) {
  U16     r;
  r   = * pAddr;
  return r;
}

/*********************************************************************
*
*       USBH_WriteReg32
*/
void USBH_WriteReg32(U8 * pAddr, U32 Value) {
  * ((U32 *)pAddr) = Value;                     //lint !e9087 D:100[c]  32-Bit hardware register access
}

/*********************************************************************
*
*       USBH_ReadReg32
*/
U32 USBH_ReadReg32(const U8 * pAddr) {
  U32 r;
  r = * ((const U32 *)pAddr);                   //lint !e9087 D:100[c]  32-Bit hardware register access
  return r;
}

/*********************************************************************
*
*       USBH_LoadU32LE
*/
U32 USBH_LoadU32LE(const U8 * pData) {
  U32 r;
  r  = *pData++;
  r |= (U32)*pData++ << 8;
  r |= (U32)*pData++ << 16;
  r |= (U32)*pData   << 24;
  return r;
}

/*********************************************************************
*
*       USBH_LoadU32BE
*/
U32 USBH_LoadU32BE(const U8 * pData) {
  U32 r;
  r = * pData++;
  r = (r << 8) | * pData++;
  r = (r << 8) | * pData++;
  r = (r << 8) | * pData;
  return r;
}

/*********************************************************************
*
*       USBH_LoadU16BE
*
*  Notes
*    This function does not use a U16 as a return value because
*    this causes some compilers to produce code where the value
*    is additionally shifted (unnecessary overhead).
*/
unsigned USBH_LoadU16BE(const U8 * pData) {
  unsigned r;
  r = * pData++;
  r = (r << 8) | * pData;
  return r;
}

/*********************************************************************
*
*       USBH_LoadU16LE
*
*  Notes
*    This function does not use a U16 as a return value because
*    this causes some compilers to produce code where the value
*    is additionally shifted (unnecessary overhead).
*/
unsigned USBH_LoadU16LE(const U8 * pData) {
  unsigned r;
  r  = *pData++;
  r |= ((unsigned)(*pData) << 8);
  return r;
}

/*********************************************************************
*
*       USBH_LoadU24LE
*/
U32 USBH_LoadU24LE(const U8 * pData) {
  unsigned r;
  r  = *pData++;
  r |= ((unsigned)(*pData++) << 8);
  r |= ((unsigned)(*pData) << 16);
  return r;
}

/*********************************************************************
*
*       USBH_StoreU32BE
*/
void USBH_StoreU32BE(U8 * p, U32 v) {
  *  p      = (U8)((v >> 24) & 255u);
  * (p + 1) = (U8)((v >> 16) & 255u);
  * (p + 2) = (U8)((v >> 8)  & 255u);
  * (p + 3) = (U8)( v        & 255u);
}

/*********************************************************************
*
*       USBH_StoreU32LE
*/
void USBH_StoreU32LE(U8 * p, U32 v) {
  * p++ = (U8)v;
  v >>= 8;
  * p++ = (U8)v;
  v >>= 8;
  * p++ = (U8)v;
  v >>= 8;
  * p   = (U8)v;
}

/*********************************************************************
*
*       USBH_StoreU24LE
*/
void USBH_StoreU24LE(U8 * p, U32 v) {
  * p++ = (U8)v;
  v >>= 8;
  * p++ = (U8)v;
  v >>= 8;
  * p   = (U8)v;
}

/*********************************************************************
*
*       USBH_StoreU16BE
*/
void USBH_StoreU16BE(U8 * p, unsigned v) {
  * (p + 0) = (U8)((v >> 8) & 255u);
  * (p + 1) = (U8)( v       & 255u);
}

/*********************************************************************
*
*       USBH_StoreU16LE
*/
void USBH_StoreU16LE(U8 * p, unsigned v) {
  * p++ = (U8)v;
  v >>= 8;
  * p   = (U8)v;
}

/*********************************************************************
*
*       USBH_SwapU32
*/
U32 USBH_SwapU32(U32 v) {
  U32 r;
  r =  ((v << 0)  >> 24) << 0;
  r |= ((v << 8)  >> 24) << 8;
  r |= ((v << 16) >> 24) << 16;
  r |= ((v << 24) >> 24) << 24;
  return r;
}

/*********************************************************************
*
*       USBH_DLIST_Init
*
*  Function description
*    Initializes a USBH_DLIST element. The link pointers
*    points to the structure itself. This element represents
*    an empty USBH_DLIST.
*    Each list head has to be initialized by this function.
*
*  Parameters
*    pListHead    : Pointer to a structure of type USBH_DLIST.
*/
void USBH_DLIST_Init(USBH_DLIST * pListHead) {
  pListHead->pPrev = pListHead;
  pListHead->pNext = pListHead;
}

/*********************************************************************
*
*       USBH_DLIST_IsEmpty
*
*  Function description
*    Checks whether the list is empty.
*
*  Parameters
*    pListHead    : Pointer to the list head.
*
*  Return value
*    1 (TRUE)  : if the list is empty.
*    0 (FALSE) : otherwise.
*/
int USBH_DLIST_IsEmpty(const USBH_DLIST * pListHead) {
  return (pListHead->pNext == pListHead) ? 1 : 0;        //lint !e9031  N:105
}

/*********************************************************************
*
*       USBH_DLIST_Contains1Item
*
*  Function description
*    Checks whether the list contains exactly one item.
*
*  Parameters
*    pList    : Pointer to a list.
*
*  Return value
*    1 (TRUE)  : if the list contains one item.
*    0 (FALSE) : otherwise.
*/
int USBH_DLIST_Contains1Item(const USBH_DLIST * pList) {
  return (USBH_DLIST_IsEmpty(pList) == 0 && pList->pNext == pList->pPrev) ? 1 : 0;   //lint !e9031  N:105
}

/*********************************************************************
*
*       USBH_DLIST_GetPrev
*
*  Function description
*    Returns a pointer to the predecessor.
*
*  Parameters
*    pEntry    : Pointer to a list entry.
*
*  Return value
*    Pointer to the predecessor of pEntry.
*/
USBH_DLIST * USBH_DLIST_GetPrev(const USBH_DLIST * pEntry) {
  return (pEntry)->pPrev;
}

/*********************************************************************
*
*       USBH_DLIST_RemoveEntry
*
*  Function description
*    Detaches one element from the list.
*    Calling this function on an empty list results in undefined behaviour.
*
*  Parameters
*    pEntry    : Pointer to the element to be detached.
*/
void USBH_DLIST_RemoveEntry(USBH_DLIST * pEntry) {
  pEntry->pPrev->pNext = pEntry->pNext;
  pEntry->pNext->pPrev = pEntry->pPrev;
  pEntry->pPrev        = pEntry;
  pEntry->pNext        = pEntry;
}

/*********************************************************************
*
*       USBH_DLIST_RemoveHead
*
*  Function description
*    Detaches the first element from the list.
*    Calling this function on an empty list results in undefined behaviour.
*
*  Parameters
*    pListHead : Pointer to the list head.
*    ppEntry   : Address of a pointer to the detached element.
*/
void USBH_DLIST_RemoveHead(const USBH_DLIST * pListHead, USBH_DLIST ** ppEntry) {
  *(ppEntry) = pListHead->pNext;
  USBH_DLIST_RemoveEntry(*(ppEntry));
}

/*********************************************************************
*
*       USBH_DLIST_RemoveTail
*
*  Function description
*    Detaches the last element from the list.
*    Calling this function on an empty list results in undefined behaviour.
*
*  Parameters
*    pListHead : Pointer to the list head.
*    ppEntry   : Address of a pointer to the detached element.
*/
void USBH_DLIST_RemoveTail(const USBH_DLIST * pListHead, USBH_DLIST ** ppEntry) {
  *ppEntry = pListHead->pPrev;
  USBH_DLIST_RemoveEntry(*ppEntry);
}

/*********************************************************************
*
*       USBH_DLIST_InsertEntry
*
*  Function description
*    Inserts an element into a list.
*    pNewEntry is inserted after pEntry,
*    i. e. pNewEntry becomes the successor of pEntry.
*
*  Parameters
*    pEntry    : Pointer to the element after which the new entry is to be inserted.
*    pNewEntry : Pointer to the element to be inserted.
*/
void USBH_DLIST_InsertEntry(USBH_DLIST * pEntry, USBH_DLIST * pNewEntry) {
  pNewEntry->pNext     = pEntry->pNext;
  pNewEntry->pPrev     = pEntry;
  pEntry->pNext->pPrev = pNewEntry;
  pEntry->pNext        = pNewEntry;
}

/*********************************************************************
*
*       USBH_DLIST_InsertHead
*
*  Function description
*    Inserts an element at the beginning of a list.
*    pEntry becomes the first list entry.
*
*  Parameters
*    pListHead : Pointer to the list head.
*    pEntry    : Pointer to the element to be inserted.
*/
void USBH_DLIST_InsertHead(USBH_DLIST * pListHead, USBH_DLIST * pEntry) {
  USBH_DLIST_InsertEntry(pListHead, pEntry);
}

/*********************************************************************
*
*       USBH_DLIST_InsertTail
*
*  Function description
*    Inserts an element at the end of a list.
*    pEntry becomes the last list entry.
*
*  Parameters
*    pListHead : Pointer to the list head.
*    pEntry    : Pointer to the element to be inserted.
*/
void USBH_DLIST_InsertTail(const USBH_DLIST * pListHead, USBH_DLIST * pEntry) {
  USBH_DLIST_InsertEntry(pListHead->pPrev, pEntry);
}

/*********************************************************************
*
*       USBH_DLIST_Append
*
*  Function description
*    Concatenates two lists.
*    The first element of List becomes the successor
*    of the last element of ListHead.
*
*  Parameters
*    pListHead : Pointer to the list head of the first list.
*    pList     : Pointer to the list head of the second list.
*/
void USBH_DLIST_Append(USBH_DLIST * pListHead, USBH_DLIST * pList) {
  USBH_DLIST * pWorkList  = pList;
  USBH_DLIST * pListTail  = pListHead->pPrev;

  pListTail->pNext        = pWorkList;
  pWorkList->pPrev->pNext = pListHead;
  pListHead->pPrev        = pWorkList->pPrev;
  pWorkList->pPrev        = pListTail;
}

/*********************************************************************
*
*       USBH_DLIST_MoveList
*
*  Parameters
*    pSrcHead : Source list header.
*    pDstHead : Destination list header.
*/
void USBH_DLIST_MoveList(const USBH_DLIST * pSrcHead, USBH_DLIST * pDstHead) {
  if (USBH_DLIST_IsEmpty(pSrcHead) != 0) {
    USBH_DLIST_Init(pDstHead);
  } else {
    pDstHead->pNext = pSrcHead->pNext;
    pDstHead->pPrev = pSrcHead->pPrev;
    pDstHead->pPrev->pNext = pDstHead;
    pDstHead->pNext->pPrev = pDstHead;
  }
}

/*********************************************************************
*
*       USBH_DLIST_Move
*
*  Function description
*    Moves an item from the previous list to another list.
*
*  Parameters
*    pHead : Pointer to a linked list into which pItem should be inserted.
*    pItem : Pointer to a USBH_DLIST element.
*/
void USBH_DLIST_Move(USBH_DLIST * pHead, USBH_DLIST * pItem) {
  USBH_DLIST_RemoveEntry(pItem);
  USBH_DLIST_Append(pHead, pItem);
}

/*********************************************************************
*
*       USBH_BUFFER_Init
*
*  Function description
*    Sets starting values for a ring buffer structure.
*
*  Parameters
*    pBuffer  : Pointer to a BUFFER structure.
*    pData    : Pointer to the data buffer.
*    NumBytes : Size of the data buffer.
*/
void USBH_BUFFER_Init(USBH_BUFFER * pBuffer, void * pData, U32 NumBytes) {
  pBuffer->pData      = USBH_U8PTR(pData);
  pBuffer->Size       = NumBytes;
  pBuffer->NumBytesIn = 0;
  pBuffer->RdPos      = 0;
}

/*********************************************************************
*
*       USBH_BUFFER_Read
*
*  Function description
*    Read data from USB EP
*
*  Return value
*    Number of bytes read
*/
unsigned USBH_BUFFER_Read(USBH_BUFFER * pBuffer, U8 * pData, unsigned NumBytesReq) {
  unsigned EndPos;
  unsigned NumBytesAtOnce;
  unsigned NumBytesTransfered;

  NumBytesTransfered = 0;
  while (NumBytesReq != 0u && pBuffer->NumBytesIn != 0u) {
    EndPos =  pBuffer->RdPos + pBuffer->NumBytesIn;
    if (EndPos > pBuffer->Size) {
      EndPos = pBuffer->Size;
    }
    NumBytesAtOnce = EndPos - pBuffer->RdPos;
    NumBytesAtOnce = USBH_MIN(NumBytesAtOnce, NumBytesReq);
    USBH_MEMCPY(pData, pBuffer->pData + pBuffer->RdPos, NumBytesAtOnce);
    NumBytesReq         -= NumBytesAtOnce;
    pBuffer->NumBytesIn -= NumBytesAtOnce;
    NumBytesTransfered  += NumBytesAtOnce;
    pData               += NumBytesAtOnce;
    pBuffer->RdPos      += NumBytesAtOnce;
    if (pBuffer->RdPos == pBuffer->Size) {
      pBuffer->RdPos = 0;
    }
  }
  //
  // Optimization for speed: If buffer is empty, read position is reset.
  //
  if (pBuffer->NumBytesIn == 0u) {
    pBuffer->RdPos = 0;
  }
  return NumBytesTransfered;
}

/*********************************************************************
*
*       USBH_BUFFER_Write
*
*  Function description
*    Write data into ring buffer
*/
void USBH_BUFFER_Write(USBH_BUFFER * pBuffer, const U8 * pData, unsigned NumBytes) {
  unsigned WrPos;
  unsigned EndPos;
  unsigned NumBytesFree;
  unsigned NumBytesAtOnce;

  for(;;) {
    //
    // Check if there is still something to do
    //
    NumBytesFree = pBuffer->Size - pBuffer->NumBytesIn;
    if (NumBytes == 0u) {
      break;                                        // We are done !
    }
    if (NumBytesFree == 0u) {
      USBH_PANIC("RX buffer overflow. More bytes received than the buffer can hold");
      break;                                        // Error ...
    }
    //
    // Compute number of bytes to copy at once
    //
    WrPos = pBuffer->RdPos + pBuffer->NumBytesIn;
    if (WrPos >= pBuffer->Size) {
      WrPos -= pBuffer->Size;
    }
    EndPos =  WrPos + NumBytes;
    if (EndPos > pBuffer->Size) {
      EndPos = pBuffer->Size;
    }
    NumBytesAtOnce = EndPos - WrPos;
    NumBytesAtOnce = USBH_MIN(NumBytesAtOnce, NumBytes);
    //
    // Copy
    //
    USBH_MEMCPY(pBuffer->pData + WrPos, pData, NumBytesAtOnce);
    //
    // Update variables
    //
    NumBytes            -= NumBytesAtOnce;
    pBuffer->NumBytesIn += NumBytesAtOnce;
    pData               += NumBytesAtOnce;
  }
}

/*********************************************************************
*
*       USBH_Basename
*
*  Function description
*    Returns base filename from a path.
*
*  Parameters
*    pPath:   Pointer to file path.
*
*  Return value
*    Pointer to base name of the file (points into pPath).
*/
const char * USBH_Basename(const char *pPath) {
  const char *pBasename = pPath;

  while (*pPath != '\0') {
    if (*pPath == '/' || *pPath == '\\') {
      pBasename = pPath + 1;
    }
    pPath++;
  }
  return pBasename;
}

/*********************************************************************
*
*       USBH_LogHexDump
*
*  Function description
*    Print data as hex dump to debug output.
*
*  Parameters
*    Type     : Message type to log.
*    Len      : Len of data.
*    pvData   : Pointer to data to be output.
*/
void USBH_LogHexDump(U32 Type, U32 Len, const void *pvData) {
  char Buff[16*3+1];
  char *p;
  int  Cnt;
  unsigned Addr;
  const U8 *pData = (const U8 *)pvData;           //lint !e9079  D:100[e]
  static const char HexDigit[] = "0123456789ABCDEF";

  p = Buff;
  Cnt = 16;
  Addr = 0;
  while (Len > 0u) {
    *p++ = HexDigit[*pData >> 4];
    *p++ = HexDigit[*pData & 0xFu];
    pData++;
    Len--;
    *p++ = ' ';
    if (--Cnt == 0 || Len == 0u) {
      *p = '\0';
      USBH_USE_PARA(Type);
      USBH_USE_PARA(Addr);
      USBH_LOG((Type, "%03x0  %s", Addr, Buff));
      p = Buff;
      Cnt = 16;
      Addr++;
    }
  }
}

/*********************************************************************
*
*       USBH_XXLog
*
*  Function description
*    Stores data into a dedicated log buffer.
*
*  Parameters
*    Tag      : Tag to identify the data.
*    Data     : Data to be stored.
*/
#if USBH_DEBUG > 1
void USBH_XXLog(unsigned Tag, unsigned Data) {
  USBH_XXLogTab[USBH_XXLogCnt] = (Tag << 28) | Data;
  USBH_XXLogCnt = (USBH_XXLogCnt + 1u) % SEGGER_COUNTOF(USBH_XXLogTab);
  USBH_XXLogTab[USBH_XXLogCnt] = 0xFFFFFFFFu;
}
#endif

/*************************** End of file ****************************/
