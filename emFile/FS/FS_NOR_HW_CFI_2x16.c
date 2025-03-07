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
File    : FS_NOR_HW_CFI_2x16.c
Purpose : Hardware layer handling CFI compliant flash chips.
Literature:
  [1] Intel's "Common Flash Interface (CFI) and Command Sets"
      Application Note 646, April 2000

  [2] Spansion's "Common Flash Interface Version 1.4 Vendor Specific Extensions"
      Revision A, Amendment 0, March 22 - 2004

Additional information:

Comments in this file refer to Intel's "Common Flash Interface
(CFI) and Command Sets" document as the "CFI spec."

Supported devices:
Any CFI-compliant flash in 16-bit mode

-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#include "FS_Int.h"
#include "FS_NOR_Int.h"

/*********************************************************************
*
*      Defines, fixed
*
**********************************************************************
*/

/*********************************************************************
*
*       Waiting functions
*/
#define AMD_WAIT_FOR_ERASE_END(BaseAddr, StatusAddr)            _AMD_WaitForEndOfOperation((unsigned)(BaseAddr), (unsigned)(StatusAddr), (U32)FS_NOR_ERASE_TIMEOUT)
#define AMD_WAIT_FOR_WRITE_END(BaseAddr, StatusAddr)            _AMD_WaitForEndOfOperation((unsigned)(BaseAddr), (unsigned)(StatusAddr), (U32)FS_NOR_WRITE_TIMEOUT)
#define AMD_WAIT_FOR_FAST_WRITE_END(BaseAddr, StatusAddr)       _AMD_WaitForEndOfOperation((unsigned)(BaseAddr), (unsigned)(StatusAddr), (U32)FS_NOR_WRITE_TIMEOUT)

//lint -esym(773, INTEL_CLEAR_STATUS, INTEL_RESET, AMD_RESET, CFI_READ_CONFIG, AMD_WRITE_BUFFER_TO_FLASH) Expression-like macro not parenthesized.
// Rationale: these macros expand to expressions that do not require any parenthesis.

/*********************************************************************
*
*      Flash command definitions (Intel algorithm)
*/
#define INTEL_PROGRAM(BaseAddr, Addr, Data)                         \
  *SEGGER_ADDR2PTR(volatile U32 FS_NOR_FAR, BaseAddr) = 0x400040uL; \
  *SEGGER_ADDR2PTR(volatile U32 FS_NOR_FAR, Addr)     = Data

#define INTEL_READ_STATUS(BaseAddr, Stat)                           \
  *SEGGER_ADDR2PTR(volatile U32 FS_NOR_FAR, BaseAddr) = 0x700070uL; \
  (Stat) = *SEGGER_ADDR2PTR(volatile U32 FS_NOR_FAR, BaseAddr)

#define INTEL_ERASE_BLOCK(Addr)                                     \
  *SEGGER_ADDR2PTR(volatile U32 FS_NOR_FAR, Addr)     = 0x200020uL; \
  *SEGGER_ADDR2PTR(volatile U32 FS_NOR_FAR, Addr)     = 0xD000D0uL

#define INTEL_CLEAR_STATUS(BaseAddr)                                \
  *SEGGER_ADDR2PTR(volatile U32 FS_NOR_FAR, BaseAddr) = 0x500050uL

#define INTEL_RESET(BaseAddr)                                       \
  *SEGGER_ADDR2PTR(volatile U32 FS_NOR_FAR, BaseAddr) = 0xFF00FFuL

/*********************************************************************
*
*      Flash command definitions (AMD algorithm)
*/
#define AMD_WRITE_CODE(BaseAddr)                                                \
  {                                                                             \
    unsigned Addr_WRITE_CODE;                                                   \
    Addr_WRITE_CODE = (BaseAddr) + (0x555uL << 2);                              \
    *SEGGER_ADDR2PTR(volatile U32 FS_NOR_FAR, Addr_WRITE_CODE) = 0x00AA00AAuL;  \
    Addr_WRITE_CODE = (BaseAddr) + (0x2AAuL << 2);                              \
    *SEGGER_ADDR2PTR(volatile U32 FS_NOR_FAR, Addr_WRITE_CODE) = 0x00550055uL;  \
  }

#define AMD_PROGRAM(BaseAddr)                                                   \
  {                                                                             \
    unsigned Addr_PROGRAM;                                                      \
    Addr_PROGRAM = (BaseAddr) + (0x555uL << 2);                                 \
    AMD_WRITE_CODE(BaseAddr);                                                   \
    *SEGGER_ADDR2PTR(volatile U32 FS_NOR_FAR, Addr_PROGRAM) = 0x00A000A0uL;     \
  }

#define AMD_BLOCK_ERASE(BaseAddr, SectorAddr)                                   \
  {                                                                             \
    unsigned Addr_BLOCK_ERASE;                                                  \
    AMD_WRITE_CODE(BaseAddr);                                                   \
    Addr_BLOCK_ERASE = (BaseAddr) + (0x555uL << 2);                             \
    *SEGGER_ADDR2PTR(volatile U32 FS_NOR_FAR, Addr_BLOCK_ERASE) = 0x00800080uL; \
    AMD_WRITE_CODE(BaseAddr);                                                   \
    *SEGGER_ADDR2PTR(volatile U32 FS_NOR_FAR, SectorAddr) = 0x00300030uL;       \
  }

#define AMD_RESET(BaseAddr)                                                     \
  *SEGGER_ADDR2PTR(volatile U32 FS_NOR_FAR, BaseAddr) = 0x00F000F0uL

#if (FS_NOR_AMD_WRITE_BUFFER_SIZE != 0)
  #define AMD_LOAD_BUFFER(BaseAddr, SectorAddr, NumItems)                                \
    {                                                                                    \
      U32 AMD_LOAD_BUFFER_NumItems;                                                      \
      AMD_LOAD_BUFFER_NumItems  = (U32)((NumItems) - 1u);                                \
      AMD_LOAD_BUFFER_NumItems |= AMD_LOAD_BUFFER_NumItems << 16;                        \
      AMD_WRITE_CODE(BaseAddr);                                                          \
      *SEGGER_ADDR2PTR(volatile U32 FS_NOR_FAR, SectorAddr) = 0x00250025uL;              \
      *SEGGER_ADDR2PTR(volatile U32 FS_NOR_FAR, SectorAddr) = AMD_LOAD_BUFFER_NumItems;  \
    }

  #define AMD_WRITE_BUFFER_TO_FLASH(SectorAddr)                                          \
    *SEGGER_ADDR2PTR(volatile U32 FS_NOR_FAR, SectorAddr) = 0x00290029uL
#endif // FS_NOR_AMD_WRITE_BUFFER_SIZE != 0

#define AMD_ABORT_BUFFERED_WRITE(BaseAddr)                                               \
  {                                                                                      \
    unsigned Addr_ABORT_BUFFERED_WRITE;                                                  \
    Addr_ABORT_BUFFERED_WRITE = (BaseAddr) + (0x555uL << 2);                             \
    AMD_WRITE_CODE(BaseAddr);                                                            \
    *SEGGER_ADDR2PTR(volatile U32 FS_NOR_FAR, Addr_ABORT_BUFFERED_WRITE) = 0x00F000F0uL; \
  }

/*********************************************************************
*
*      Flash command definitions (CFI)
*
*  Notes:
*    (1) To determine if a device is CFI capable, we have to write 0x98
*        (CFI query) at address 0x55 (CFI spec, page 4).
*/
#define CFI_READ_CONFIG(BaseAddr)                                             \
  {                                                                           \
    unsigned Addr_READ_CONFIG;                                                \
    Addr_READ_CONFIG = (BaseAddr) + (0x55uL << 2);                            \
    *SEGGER_ADDR2PTR(volatile U32 FS_NOR_FAR, Addr_READ_CONFIG) = 0x980098uL; \
  }

//
// Writing 0xFF puts the device into read array mode (CFI spec, page 15).
//
#define CFI_RESET(BaseAddr)                                         \
  *SEGGER_ADDR2PTR(volatile U32 FS_NOR_FAR, BaseAddr) = 0xFF00FFuL; \
  *SEGGER_ADDR2PTR(volatile U32 FS_NOR_FAR, BaseAddr) = 0xF000F0uL

/*********************************************************************
*
*      ASSERT_IS_DATA_ALIGNED
*/
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL)
  #define ASSERT_IS_DATA_ALIGNED(DestAddr, pSrc, NumItems)                                                \
    if ((((DestAddr) & 3u) != 0u) || ((SEGGER_PTR2ADDR(pSrc) & 3u) != 0u) || (((NumItems) & 1u) != 0u)) { \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR_CFI_2x16: Data is not aligned."));                        \
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);                                                                \
    }
#else
  #define ASSERT_IS_DATA_ALIGNED(DestAddr, pSrc, NumItems)
#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL

/*********************************************************************
*
*      Static code
*
**********************************************************************
*/

/*********************************************************************
*
*      _AMD_WaitForEndOfOperation
*
*  Function description
*    Polls the NOR flash for the end of an erase or a program operation.
*
*  Parameters
*    BaseAddr         Address of the status register.
*    StatusAddr       Address of the status register.
*    TimeOut          Maximum number of software cycles to wait for the operation to finish.
*                     0 means wait indefinitely.
*
*  Return value
*    ==0    O.K., phy. sector is erased or data programmed.
*    !=0    An error occurred.
*/
static int _AMD_WaitForEndOfOperation(unsigned BaseAddr, unsigned StatusAddr, U32 TimeOut) {
  U32                             d0;
  U32                             d1;
  volatile const U32 FS_NOR_FAR * pStatus;

  FS_USE_PARA(BaseAddr);
  pStatus = SEGGER_ADDR2PTR(volatile const U32 FS_NOR_FAR, StatusAddr);
  for (;;) {
    d0 = *pStatus;
    d1 = *pStatus;
    //
    // Bit 6 toggles as long as the operation is still in progress.
    //
    if (((d0 ^ d1) & 0x00400040uL) == 0u) {
      break;                          // Operation finished successfully.
    }
    if (TimeOut != 0u) {
      if (--TimeOut == 0u) {
        return 1;                     // Error, timeout expired.
      }
    }
    FS_NOR_DELAY();
  }
  return 0;                           // OK, sector erased or data programmed successfully.
}

#if (FS_NOR_AMD_WRITE_BUFFER_SIZE != 0)

  /*********************************************************************
*
*      _AMD_WritePage
*
*  Function description
*    Writes a complete page in one program cycle a page is in general 32 byte wide.
*
*  Return value
*    ==0    O.K., sector is erased
*    !=0    Error, sector may not be written correctly.
*/
static int _AMD_WritePage(U8 Unit, unsigned BaseAddr, unsigned SectorAddr, unsigned DestAddr, const U16 FS_NOR_FAR * pSrc, unsigned NumItems) {
  volatile const U32 FS_NOR_FAR * pSrc32;
  volatile U32 FS_NOR_FAR       * pDest;
  volatile U32 FS_NOR_FAR       * pStatus;
  unsigned                        i;
  int                             r;
  unsigned                        Addr;

  FS_USE_PARA(Unit);
  FS_USE_PARA(SectorAddr);
  pSrc32 = (volatile const U32 FS_NOR_FAR *)pSrc;   //lint !e740 !e826 !e9079 !e9087
  pDest  = SEGGER_ADDR2PTR(volatile U32 FS_NOR_FAR, DestAddr);
  NumItems >>= 1;           // Convert into  number of 32-bit units
  //
  // Write data to internal buffer of NOR flash.
  //
  AMD_LOAD_BUFFER(BaseAddr, SectorAddr, NumItems);  // "Write to Buffer" command
  for (i = 0; i < NumItems; i++) {
    *(pDest + i) = *(pSrc32 + i);
  }
  //
  // Write the data from the internal buffer to memory array.
  //
  AMD_WRITE_BUFFER_TO_FLASH(SectorAddr);            // "Program Buffer to Flash" command
  //
  // Wait for flash operation to finish by reading status and testing toggle bit.
  //
  pStatus = pDest + (NumItems - 1u);               // Monitor last address to be programmed
  Addr    = (unsigned)SEGGER_PTR2ADDR(pStatus);
  r = AMD_WAIT_FOR_FAST_WRITE_END(BaseAddr, Addr);
  if (r != 0) {
    //
    // After an error the NOR flash remains in command mode
    // and a reset is required to put it back into read array mode.
    //
    AMD_ABORT_BUFFERED_WRITE(BaseAddr);
    AMD_RESET(BaseAddr);
  }
  return r;
}

#endif // FS_NOR_AMD_WRITE_BUFFER_SIZE != 0

/*********************************************************************
*
*      Static code (public via callback)
*
**********************************************************************
*/

/*********************************************************************
*
*      _INTEL_EraseSector
*
*  Function description
*    Erases 1 physical sector.
*
*  Return value
*    ==0    O.K., sector is erased
*    !=0    Error, sector may not be erased
*/
static int _INTEL_EraseSector(U8 Unit, unsigned BaseAddr, unsigned SectorAddr) {
  U32                       Status;
  volatile U32 FS_NOR_FAR * pB;

  FS_USE_PARA(Unit);
  pB = SEGGER_ADDR2PTR(volatile U32 FS_NOR_FAR, SectorAddr);
  FS_NOR_DI();                              // Disable interrupts, to be on the safe side.
  INTEL_ERASE_BLOCK(pB);
  do {
    INTEL_READ_STATUS(BaseAddr, Status);
    FS_NOR_DELAY();
  } while ((Status & 0x800080uL) == 0u);    // Wait till flash is no longer busy.
  INTEL_RESET(BaseAddr);
  INTEL_CLEAR_STATUS(BaseAddr);
  FS_NOR_EI();                              // Enable interrupts.
  if ((Status & 0x3A003AuL) != 0u) {
    return 1;
  }
  return 0;
}

/*********************************************************************
*
*      _INTEL_Write
*/
static int _INTEL_Write(U8 Unit, unsigned BaseAddr, unsigned SectorAddr, unsigned DestAddr, const U16 FS_NOR_FAR * pSrc, unsigned NumItems) {
  U32                       Status;
  volatile U32 FS_NOR_FAR * pDest;
  const U32 FS_NOR_FAR    * pSrc32;
  U32                       Data32;

  FS_USE_PARA(Unit);
  FS_USE_PARA(SectorAddr);
  pSrc32 = SEGGER_ADDR2PTR(const U32 FS_NOR_FAR, pSrc);
  pDest  = SEGGER_ADDR2PTR(volatile U32 FS_NOR_FAR, DestAddr);
  NumItems >>= 1;                             // Convert into  number of 32-bit units
  do {
    Data32 = *pSrc32;
    FS_NOR_DI();                              // Disable interrupts, to be on the safe side.
    INTEL_PROGRAM(BaseAddr, pDest, Data32);
    do {
      INTEL_READ_STATUS(BaseAddr, Status);
      FS_NOR_DELAY();
    } while ((Status & 0x800080uL) == 0u);    // Wait till flash is no longer busy.
    INTEL_READ_STATUS(BaseAddr, Status);
    INTEL_RESET(BaseAddr);
    INTEL_CLEAR_STATUS(BaseAddr);
    INTEL_RESET(BaseAddr);
    FS_NOR_EI();                              // Enable interrupts.
    if ((Status & 0x1A001AuL) != 0u) {
      return 1;
    }
    if (*pDest != Data32) {
      return 1;
    }
    ++pDest;
    ++pSrc32;
  } while (--NumItems != 0u);
  return 0;
}

/*********************************************************************
*
*      _AMD_EraseSector
*
*  Function description
*    Erases 1 physical sector.
*
*  Return value
*    ==0    O.K., sector is erased
*    !=0    Error, sector may not be erased
*/
static int _AMD_EraseSector(U8 Unit, unsigned BaseAddr, unsigned SectorAddr) {
  volatile U32 FS_NOR_FAR * pB;
  int                       r;

  FS_USE_PARA(Unit);
  pB = SEGGER_ADDR2PTR(volatile U32 FS_NOR_FAR, SectorAddr);
  FS_NOR_DI();
  AMD_BLOCK_ERASE(BaseAddr, SectorAddr);
  r = AMD_WAIT_FOR_ERASE_END(BaseAddr, SectorAddr);
  if (r == 0) {
    if (*pB != 0xFFFFFFFFuL) {
      r = 1;                  // Error, phy. sector was not erased correctly.
    }
  }
  if (r != 0) {
    //
    // After an error the NOR flash remains in command mode
    // and a reset is required to put it back into read array mode.
    //
    AMD_RESET(BaseAddr);
  }
  FS_NOR_EI();
  return r;
}

/*********************************************************************
*
*      _AMD_Write
*
*  Function description
*    Writes data to NOR flash.
*
*  Return value
*    ==0    O.K., data has been written
*    !=0    An error occurred
*/
static int _AMD_Write(U8 Unit, unsigned BaseAddr, unsigned SectorAddr, unsigned DestAddr, const U16 FS_NOR_FAR * pSrc, unsigned NumItems) {
  U32          FS_NOR_FAR * pSrc32;
  volatile U32 FS_NOR_FAR * pDest;
  U32                       Data32;
  unsigned                  Addr;
  int                       r;

  FS_USE_PARA(Unit);
  FS_USE_PARA(SectorAddr);
  ASSERT_IS_DATA_ALIGNED(DestAddr, pSrc, NumItems);
  pSrc32 = SEGGER_ADDR2PTR(U32 FS_NOR_FAR, pSrc);
  pDest  = SEGGER_ADDR2PTR(volatile U32 FS_NOR_FAR, DestAddr);
  FS_NOR_DI();
  AMD_RESET(BaseAddr);
  FS_NOR_EI();
  NumItems >>= 1;           // Convert into  number of 32-bit units
  do {
    Data32 = *pSrc32;
    FS_NOR_DI();
    AMD_PROGRAM(BaseAddr);
    *pDest = Data32;
    //
    // Wait for operation to finish.
    //
    Addr = (unsigned)SEGGER_PTR2ADDR(pDest);
    r = AMD_WAIT_FOR_WRITE_END(BaseAddr, Addr);
    if (r != 0) {
      AMD_RESET(BaseAddr);
      FS_NOR_EI();
      return 1;               // Error, write operation failed.
    }
    //
    // Check the result of write operation.
    //
    if (*pDest != Data32) {
      AMD_RESET(BaseAddr);
      FS_NOR_EI();
      return 1;
    }
    FS_NOR_EI();
    ++pDest;
    ++pSrc32;
  } while (--NumItems != 0u);
  return 0;
}

#if (FS_NOR_AMD_WRITE_BUFFER_SIZE != 0)

/*********************************************************************
*
*      _AMD_WriteFast
*
*  Function description
*    Writes data into NOR flash using the write buffer.
*
*  Return value
*    ==0     O.K., data has been written
*    !=0     An error occurred
*/
static int _AMD_WriteFast(U8 Unit, unsigned BaseAddr, unsigned SectorAddr, unsigned DestAddr, const U16 FS_NOR_FAR * pSrc, unsigned NumItems) {
  const U16 FS_NOR_FAR * pSrc16;
  unsigned               NumItems2Write;
  int                    r;
  unsigned               BufferSize;

  FS_USE_PARA(Unit);
  ASSERT_IS_DATA_ALIGNED(DestAddr, pSrc, NumItems);
  pSrc16 = SEGGER_CONSTPTR2PTR(const U16 FS_NOR_FAR, pSrc);
  FS_NOR_DI();
  AMD_RESET(BaseAddr);
  FS_NOR_EI();
  BufferSize = (unsigned)FS_NOR_AMD_WRITE_BUFFER_SIZE << 1;       // We write to both buffers at the same time.
  while (NumItems > 0u) {
    if ((DestAddr & (BufferSize - 1u)) != 0u) {
      NumItems2Write = (BufferSize - (DestAddr & (BufferSize - 1u))) >> 1;
      NumItems2Write = SEGGER_MIN(NumItems2Write, NumItems);
      FS_NOR_DI();
      r = _AMD_WritePage(Unit, BaseAddr, SectorAddr, DestAddr, pSrc16, NumItems2Write);
      if (r != 0) {
        FS_NOR_EI();
        return 1;
      }
      FS_NOR_EI();
      pSrc16   += NumItems2Write;
      NumItems -= NumItems2Write;
      DestAddr += NumItems2Write << 1;
    }
    if (NumItems != 0u) {
      do {
        NumItems2Write = SEGGER_MIN(NumItems, (BufferSize >> 1));
        FS_NOR_DI();
        r = _AMD_WritePage(Unit, BaseAddr, SectorAddr, DestAddr, pSrc16, NumItems2Write);
        if (r != 0) {
          FS_NOR_EI();
          return 1;
        }
        FS_NOR_EI();
        pSrc16   += NumItems2Write;
        NumItems -= NumItems2Write;
        DestAddr += ((unsigned)NumItems2Write << 1);
      } while (NumItems != 0u);
    }
  }
  return 0;
}

#endif // FS_NOR_AMD_WRITE_BUFFER_SIZE != 0

/*********************************************************************
*
*       _Read
*
*  Function description
*    Reads data from the given address of the NOR flash.
*/
static int _Read(U8 Unit, void * pDest, unsigned Addr, U32 NumBytes) {
  FS_USE_PARA(Unit);
  FS_MEMCPY(pDest, SEGGER_ADDR2PTR(const void, Addr), NumBytes);
  return 0;
}

/*********************************************************************
*
*      Public code
*
**********************************************************************
*/

/*********************************************************************
*
*      FS_NOR_CFI_ReadCFI_2x16
*
*  Function description
*    Reads CFI data from hardware into buffer.
*    Note that every 16-bit value from flash contains just a single byte.
*
*  Notes
*    (1) Dual flash
*        We consider the CFI info of the flash at the even addresses (A1 == 0) only, assuming that the 2 flashes are identical.
*/
void FS_NOR_CFI_ReadCFI_2x16(U8 Unit, U32 BaseAddr, U32 Off, U8 * pData, unsigned NumItems) {
  volatile U32 FS_NOR_FAR * pAddr;
  unsigned                  Addr;
  int                       r;

  FS_USE_PARA(Unit);
  Addr = BaseAddr + (Off << 2);
  pAddr = SEGGER_ADDR2PTR(volatile U32 FS_NOR_FAR, Addr);
  FS_NOR_DI();
  //
  // The NOR flash outputs the status register during a write or erase operation.
  // We have to wait here for the operation to complete otherwise the NOR flash
  // device does not switch to read CFI mode and the identification fails.
  //
  r = AMD_WAIT_FOR_ERASE_END(BaseAddr, Addr);
  if (r != 0) {
    AMD_RESET(BaseAddr);
  }
  //
  // Request the NOR flash device to switch to read CFI mode.
  //
  CFI_READ_CONFIG(BaseAddr);
  //
  // The NOR flash starts output the status register if a buffered write operation is interrupted
  // by the request to switch to read CFI mode. In this case the bit 1 of the status register is
  // set indicating that the write operation has to be aborted. If the write operation is not
  // aborted the identification of the NOR flash fails.
  //
  r = AMD_WAIT_FOR_FAST_WRITE_END(BaseAddr, BaseAddr);
  if (r != 0) {
    AMD_ABORT_BUFFERED_WRITE(BaseAddr);
    AMD_RESET(BaseAddr);
    CFI_READ_CONFIG(BaseAddr);
  }
  do {
    *pData++ = (U8)*pAddr++;  // Only the low byte of the CFI data is relevant
  } while(--NumItems != 0u);
  CFI_RESET(BaseAddr);
  FS_NOR_EI();
}

/*********************************************************************
*
*      Public data
*
**********************************************************************
*/
const FS_NOR_PROGRAM_HW FS_NOR_Program_Intel_2x16 = {
  _Read,
  _INTEL_EraseSector,
  _INTEL_Write
};

const FS_NOR_PROGRAM_HW FS_NOR_Program_AMD_2x16 = {
  _Read,
  _AMD_EraseSector,
  _AMD_Write
};

const FS_NOR_PROGRAM_HW FS_NOR_Program_AMDFast_2x16 = {
  _Read,
  _AMD_EraseSector,
#if (FS_NOR_AMD_WRITE_BUFFER_SIZE != 0)         // Just for testing purposes
  _AMD_WriteFast
#else
  _AMD_Write
#endif // FS_NOR_AMD_WRITE_BUFFER_SIZE != 0
};

/*************************** End of file ****************************/
