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
File        : FS_NOR_HW_CFI_1x16.c
Purpose     : Low level flash layer handling CFI compliant flash chips
----------------------------------------------------------------------

Comments in this file refer to Intel's "Common Flash Interface
(CFI) and Command Sets" document as the "CFI spec."

Supported devices:
Any CFI-compliant flash in 16-bit mode

Literature:
[1] Intel's "Common Flash Interface (CFI) and Command Sets"
    Application Note 646, April 2000

[2] Spansion's "Common Flash Interface Version 1.4 Vendor Specific Extensions"
    Revision A, Amendment 0, March 22 - 2004

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
*       Defines, fixed
*
**********************************************************************
*/

/*********************************************************************
*
*       Waiting functions
*/
#if   (FS_NOR_AMD_STATUS_CHECK_TYPE == 1)
  #define AMD_WAIT_FOR_ERASE_END(BaseAddr, StatusAddr)            _AMD_WaitForEndOfOperation((unsigned)(BaseAddr), (unsigned)(StatusAddr), (U16)(1uL << AMD_STATUS_ERROR_BIT), (U32)FS_NOR_ERASE_TIMEOUT)
  #define AMD_WAIT_FOR_WRITE_END(BaseAddr, StatusAddr)            _AMD_WaitForEndOfOperation((unsigned)(BaseAddr), (unsigned)(StatusAddr), (U16)(1uL << AMD_STATUS_ERROR_BIT), (U32)FS_NOR_WRITE_TIMEOUT)
  #define AMD_WAIT_FOR_FAST_WRITE_END(BaseAddr, StatusAddr)       _AMD_WaitForEndOfOperation((unsigned)(BaseAddr), (unsigned)(StatusAddr), (U16)((1uL << AMD_STATUS_ERROR_BIT) | (1uL << AMD_STATUS_ABORT_BIT)), (U32)FS_NOR_WRITE_TIMEOUT)
#elif (FS_NOR_AMD_STATUS_CHECK_TYPE == 2)
  #define AMD_WAIT_FOR_ERASE_END(BaseAddr, StatusAddr)            _AMD_WaitForEndOfOperation((unsigned)(BaseAddr), (unsigned)(StatusAddr), (U16)(1uL << HYPERFLASH_STATUS_ESB), (U32)FS_NOR_ERASE_TIMEOUT)
  #define AMD_WAIT_FOR_WRITE_END(BaseAddr, StatusAddr)            _AMD_WaitForEndOfOperation((unsigned)(BaseAddr), (unsigned)(StatusAddr), (U16)(1uL << HYPERFLASH_STATUS_PSB), (U32)FS_NOR_WRITE_TIMEOUT)
  #define AMD_WAIT_FOR_FAST_WRITE_END(BaseAddr, StatusAddr)       _AMD_WaitForEndOfOperation((unsigned)(BaseAddr), (unsigned)(StatusAddr), (U16)(1uL << HYPERFLASH_STATUS_PSB), (U32)FS_NOR_WRITE_TIMEOUT)
#else
  #define AMD_WAIT_FOR_ERASE_END(BaseAddr, StatusAddr)            _AMD_WaitForEndOfOperation((unsigned)(BaseAddr), (unsigned)(StatusAddr), 0, (U32)FS_NOR_ERASE_TIMEOUT)
  #define AMD_WAIT_FOR_WRITE_END(BaseAddr, StatusAddr)            _AMD_WaitForEndOfOperation((unsigned)(BaseAddr), (unsigned)(StatusAddr), 0, (U32)FS_NOR_WRITE_TIMEOUT)
  #define AMD_WAIT_FOR_FAST_WRITE_END(BaseAddr, StatusAddr)       _AMD_WaitForEndOfOperation((unsigned)(BaseAddr), (unsigned)(StatusAddr), 1, (U32)FS_NOR_WRITE_TIMEOUT)
#endif

/*********************************************************************
*
*       Status bits for AMD compatible devices
*/
#if (FS_NOR_AMD_STATUS_CHECK_TYPE != 2)
  #define AMD_STATUS_TOGGLE_BIT         6     // Changes value at each read during a write or an erase operation.
#endif
#if (FS_NOR_AMD_STATUS_CHECK_TYPE == 1)
  #define AMD_STATUS_ERROR_BIT          5     // Set to 1 if the write or erase operation fails. Not supported by legacy NOR flash devices.
  #define AMD_STATUS_ABORT_BIT          1     // Set to 1 if a buffered write operation has to be aborted.
#endif

/*********************************************************************
*
*      Status bits for Cypress HyperFlash compatible devices
*/
#if (FS_NOR_AMD_STATUS_CHECK_TYPE == 2)
  #define HYPERFLASH_STATUS_RDB         7     // Device Read Ready Bit
  #define HYPERFLASH_STATUS_ESB         5     // Erase Status Bit
  #define HYPERFLASH_STATUS_PSB         4     // Program Status Bit
#endif

//lint -esym(773, INTEL_RESET, INTEL_CLEAR_STATUS, AMD_RESET, AMD_WRITE_BUFFER_TO_FLASH, CFI_READ_CONFIG) Expression-like macro not parenthesized.
// Rationale: these macros expand to expressions that do not require any parenthesis.

/*********************************************************************
*
*      Flash command definitions (Intel algorithm)
*/
#define INTEL_PROGRAM(BaseAddr, Addr, Data)                   \
  *SEGGER_ADDR2PTR(volatile U16 FS_NOR_FAR, BaseAddr) = 0x40; \
  *SEGGER_ADDR2PTR(volatile U16 FS_NOR_FAR, Addr)     = Data

#define INTEL_READ_STATUS(BaseAddr, Stat)                     \
  *SEGGER_ADDR2PTR(volatile U16 FS_NOR_FAR, BaseAddr) = 0x70; \
  (Stat) = *SEGGER_ADDR2PTR(volatile U16 FS_NOR_FAR, BaseAddr)

#define INTEL_ERASE_BLOCK(Addr)                               \
  *SEGGER_ADDR2PTR(volatile U16 FS_NOR_FAR, Addr)     = 0x20; \
  *SEGGER_ADDR2PTR(volatile U16 FS_NOR_FAR, Addr)     = 0xD0

#define INTEL_CLEAR_STATUS(BaseAddr)                          \
  *SEGGER_ADDR2PTR(volatile U16 FS_NOR_FAR, BaseAddr) = 0x50

#define INTEL_RESET(BaseAddr)                                 \
  *SEGGER_ADDR2PTR(volatile U16 FS_NOR_FAR, BaseAddr) = 0xFF

#define INTEL_WAIT_UNTIL_FINISHED(pDest, Status)              \
  {                                                           \
    (Status) = *(pDest);      /* 2 reads are required. */     \
    do {                                                      \
      (Status) = *(pDest);                                    \
      FS_NOR_DELAY();                                         \
    } while (((Status) & 0x80u) == 0u);                       \
  }

#define INTEL_UNLOCK(pDest, Status)                           \
  *(pDest) = 0x60;                                            \
  *(pDest) = 0xD0;                                            \
  INTEL_WAIT_UNTIL_FINISHED(pDest, Status);                   \
  *(pDest) = 0xFF;      /* Back to "Read array" mode */

/*********************************************************************
*
*      Flash command definitions (AMD algorithm)
*/
#define AMD_WRITE_CODE(BaseAddr)                                        \
  {                                                                     \
    unsigned Addr_WRITE_CODE;                                           \
    Addr_WRITE_CODE = (BaseAddr) + (0x5555uL << 1);                     \
    *SEGGER_ADDR2PTR(volatile U16 FS_NOR_FAR, Addr_WRITE_CODE)  = 0xAA; \
    Addr_WRITE_CODE = (BaseAddr) + (0x2AAAuL << 1);                     \
    *SEGGER_ADDR2PTR(volatile U16 FS_NOR_FAR, Addr_WRITE_CODE)  = 0x55; \
  }

#define AMD_PROGRAM(BaseAddr)                                           \
  {                                                                     \
    unsigned Addr_PROGRAM;                                              \
    AMD_WRITE_CODE(BaseAddr);                                           \
    Addr_PROGRAM = (BaseAddr) + (0x5555UL << 1);                        \
    *SEGGER_ADDR2PTR(volatile U16 FS_NOR_FAR, Addr_PROGRAM) = 0xA0;     \
  }

#define AMD_ERASE_BLOCK(BaseAddr, SectorAddr)                           \
  {                                                                     \
    unsigned Addr_ERASE_BLOCK;                                          \
    AMD_WRITE_CODE(BaseAddr);                                           \
    Addr_ERASE_BLOCK = (BaseAddr) + (0x5555UL << 1);                    \
    *SEGGER_ADDR2PTR(volatile U16 FS_NOR_FAR, Addr_ERASE_BLOCK) = 0x80; \
    AMD_WRITE_CODE(BaseAddr);                                           \
    *SEGGER_ADDR2PTR(volatile U16 FS_NOR_FAR, SectorAddr) = 0x30;       \
  }

#define AMD_RESET(BaseAddr)                                             \
  *SEGGER_ADDR2PTR(volatile U16 FS_NOR_FAR, BaseAddr) = 0xF0

#if (FS_NOR_AMD_WRITE_BUFFER_SIZE != 0)
  #define AMD_LOAD_BUFFER(BaseAddr, SectorAddr, NumItems)                           \
    AMD_WRITE_CODE(BaseAddr);                                                       \
    *SEGGER_ADDR2PTR(volatile U16 FS_NOR_FAR, SectorAddr) = 0x25;                   \
    *SEGGER_ADDR2PTR(volatile U16 FS_NOR_FAR, SectorAddr) = (U16)((NumItems) - 1u)  \

  #define AMD_WRITE_BUFFER_TO_FLASH(SectorAddr)                                     \
    *SEGGER_ADDR2PTR(volatile U16 FS_NOR_FAR, SectorAddr) = 0x29
#endif // FS_NOR_AMD_WRITE_BUFFER_SIZE != 0

#if (FS_NOR_AMD_STATUS_CHECK_TYPE == 2)
  #define AMD_READ_STATUS(BaseAddr, Stat)                                         \
    {                                                                             \
      unsigned Addr_READ_STATUS;                                                  \
      Addr_READ_STATUS = (BaseAddr) + (0x0555UL << 1);                            \
      *SEGGER_ADDR2PTR(volatile U16 FS_NOR_FAR, Addr_READ_STATUS) = 0x70;         \
      (Stat) = *SEGGER_ADDR2PTR(volatile U16 FS_NOR_FAR, (BaseAddr));             \
    }
#endif // FS_NOR_AMD_STATUS_CHECK_TYPE == 2

#define AMD_ABORT_BUFFERED_WRITE(BaseAddr)                                        \
  {                                                                               \
    unsigned Addr_ABORT_BUFFERED_WRITE;                                           \
    Addr_ABORT_BUFFERED_WRITE = (BaseAddr) + (0x0555UL << 1);                     \
    AMD_WRITE_CODE(BaseAddr);                                                     \
    *SEGGER_ADDR2PTR(volatile U16 FS_NOR_FAR, Addr_ABORT_BUFFERED_WRITE) = 0xF0;  \
  }

/*********************************************************************
*
*      Flash command definitions (CFI)
*
*    Notes:
*      1   To determine if a device is CFI capable, we have to write 0x98
*          (CFI query) at address 0x55 (CFI spec, page 4).
*/
#define CFI_READ_CONFIG(BaseAddr)                                       \
  {                                                                     \
    unsigned Addr_READ_CONFIG;                                          \
    Addr_READ_CONFIG = (BaseAddr) + (0x55uL << 1);                      \
    *SEGGER_ADDR2PTR(volatile U16 FS_NOR_FAR, Addr_READ_CONFIG) = 0x98; \
  }

//
// Writing 0xFF puts the device into read array mode (CFI spec, page 15).
//
#define CFI_RESET(BaseAddr)                                                           \
  *SEGGER_ADDR2PTR(volatile U16 FS_NOR_FAR, BaseAddr) = 0xFF;                         \
  *SEGGER_ADDR2PTR(volatile U16 FS_NOR_FAR, BaseAddr) = 0xF0

//
// Some flashes do not comply to the CFI specification reg. the command sequence,
// but contain CFI information. Such as SST SST39xxxx NOR flash series.
//
#define CFI_READ_CONFIG_NON_COMPLIANT(BaseAddr)                                       \
  {                                                                                   \
    unsigned Addr_READ_CONFIG_NON_COMPLIANT;                                          \
    Addr_READ_CONFIG_NON_COMPLIANT = (BaseAddr) + (0x5555uL << 1);                    \
    AMD_WRITE_CODE(BaseAddr);                                                         \
    *SEGGER_ADDR2PTR(volatile U16 FS_NOR_FAR, Addr_READ_CONFIG_NON_COMPLIANT) = 0x98; \
  }

/*********************************************************************
*
*      ASSERT_IS_DATA_ALIGNED
*/
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL)
  #define ASSERT_IS_DATA_ALIGNED(DestAddr, pSrc)                                      \
    if ((((DestAddr) & 1u) != 0u) || ((SEGGER_PTR2ADDR(pSrc) & 1u) != 0u)) {          \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR_CFI_1x16: Data is not aligned."));    \
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);                                            \
    }
#else
  #define ASSERT_IS_DATA_ALIGNED(DestAddr, pSrc)
#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL

/*********************************************************************
*
*      Static data
*
**********************************************************************
*/
static U8 _IsCFICompliant;
static U8 _IsInited;

/*********************************************************************
*
*      Static code
*
**********************************************************************
*/

#if (FS_NOR_INTEL_WRITE_BUFFER_SIZE != 0)

/*********************************************************************
*
*      _INTEL_WritePage
*/
static int _INTEL_WritePage(U8 Unit, unsigned BaseAddr, unsigned SectorAddr, unsigned DestAddr, const U16 FS_NOR_FAR * pSrc, unsigned NumItems) {
  int                       IsUnprotected;
  U16                       Status;
  volatile U16 FS_NOR_FAR * pDest;
  unsigned                  i;

  FS_USE_PARA(Unit);
  FS_USE_PARA(BaseAddr);
  FS_USE_PARA(SectorAddr);
  IsUnprotected = 0;
  pDest = SEGGER_ADDR2PTR(volatile U16 FS_NOR_FAR, DestAddr);
  for (;;) {
    *pDest = 0xE8;                        // Write to buffer command
    INTEL_WAIT_UNTIL_FINISHED(pDest, Status);
    //
    // Write 16 items to buffer
    //
    *pDest = (U16)(NumItems - 1u);
    for (i = 0; i < NumItems; i++) {
      *(pDest + i) = *pSrc++;
    }
    //
    // Start programming
    //
    *pDest = 0xD0;
    INTEL_WAIT_UNTIL_FINISHED(pDest, Status);
    //
    // Check if error occurred
    //
    Status &= 0x7Eu;
    if (Status != 0u) {
      *pDest = 0x50;                      // Clear status register
      *pDest = 0xFF;                      // Back to "Read array" mode
      if ((Status & (1uL << 1)) != 0u) {
        if (IsUnprotected == 0) {
          INTEL_UNLOCK(pDest, Status);
          IsUnprotected = 1;
          continue;
        }
      }
      return -(int)Status;
    }
    *pDest = 0xFF;                        // Back to "Read array" mode
    return 0;
  }
}

#endif // FS_NOR_INTEL_WRITE_BUFFER_SIZE != 0

/*********************************************************************
*
*      _INTEL_EraseSector
*
*  Function description
*    Erases a physical sector.
*
*  Return value
*    ==0    O.K., sector is erased
*    !=0    Error, sector may not be erased
*/
static int _INTEL_EraseSector(U8 Unit, unsigned BaseAddr, unsigned SectorAddr) {
  volatile U16 FS_NOR_FAR * pDest;
  U16                       Status;
  int                       IsUnprotected;

  FS_USE_PARA(Unit);
  IsUnprotected = 0;
  pDest = SEGGER_ADDR2PTR(volatile U16 FS_NOR_FAR, SectorAddr);
  //
  // Start erase operation.
  //
  for (;;) {
    FS_NOR_DI();
    INTEL_ERASE_BLOCK(pDest);
    INTEL_WAIT_UNTIL_FINISHED(pDest, Status);
    FS_NOR_EI();
    //
    // Check if error occurred.
    //
    Status &= 0x7Eu;
    if (Status != 0u) {
      *pDest = 0x50;                      // Clear status register
      *pDest = 0xFF;                      // Back to "Read array" mode
      if ((Status & (1uL << 1)) != 0u) {
        if (IsUnprotected == 0) {
          FS_NOR_DI();
          INTEL_UNLOCK(pDest, Status);
          FS_NOR_EI();
          IsUnprotected = 1;
          continue;
        }
      }
      return -(int)Status;
    }
    FS_NOR_DI();
    INTEL_RESET(BaseAddr);                // Back to "Read array" mode
    FS_NOR_EI();
    return 0;
  }
}

/*********************************************************************
*
*      _INTEL_Write
*/
static int _INTEL_Write(U8 Unit, unsigned BaseAddr, unsigned SectorAddr, unsigned DestAddr, const U16 FS_NOR_FAR * pSrc, unsigned NumItems) {
  U16                       Status;
  volatile U16 FS_NOR_FAR * pDest;
  const U16    FS_NOR_FAR * pSrc16;
  U16                       Data16;

  FS_USE_PARA(Unit);
  FS_USE_PARA(SectorAddr);
  pSrc16 = SEGGER_CONSTPTR2PTR(const U16 FS_NOR_FAR, pSrc);
  pDest  = SEGGER_ADDR2PTR(volatile U16 FS_NOR_FAR, DestAddr);
  while (NumItems > 0u) {
    Data16 = *pSrc16;
    FS_NOR_DI();
    INTEL_UNLOCK(pDest, Status);
    INTEL_PROGRAM(BaseAddr, pDest, Data16);
    do {
      INTEL_READ_STATUS(BaseAddr, Status);
    } while ((Status & (1uL << 7)) == 0u);      // Wait till flash is no longer busy.
    INTEL_READ_STATUS(BaseAddr, Status);
    INTEL_RESET(BaseAddr);
    INTEL_CLEAR_STATUS(BaseAddr);
    INTEL_RESET(BaseAddr);
    FS_NOR_EI();
    if ((Status & 0x1Au) != 0u) {
      return 1;
    }
    if (*pDest != Data16) {
      return 1;
    }
    NumItems--;
    ++pDest;
    ++pSrc16;
  }
  return 0;
}

#if (FS_NOR_INTEL_WRITE_BUFFER_SIZE != 0)

/*********************************************************************
*
*      _INTEL_WriteFast
*/
static int _INTEL_WriteFast(U8 Unit, unsigned BaseAddr, unsigned SectorAddr, unsigned DestAddr, const U16 FS_NOR_FAR * pSrc, unsigned NumItems) {
  const U16 FS_NOR_FAR * pSrc16;
  unsigned               NumItems2Write;
  int                    r;

  FS_USE_PARA(Unit);
  pSrc16 = SEGGER_CONSTPTR2PTR(const U16 FS_NOR_FAR, pSrc);
  while (NumItems > 0u) {
    if ((DestAddr & ((unsigned)FS_NOR_INTEL_WRITE_BUFFER_SIZE - 1u)) != 0u) {
      NumItems2Write = ((unsigned)FS_NOR_INTEL_WRITE_BUFFER_SIZE - (DestAddr & ((unsigned)FS_NOR_INTEL_WRITE_BUFFER_SIZE - 1u))) >> 1;
      NumItems2Write = SEGGER_MIN(NumItems2Write, NumItems);
      FS_NOR_DI();
      r = _INTEL_WritePage(Unit, BaseAddr, SectorAddr, DestAddr, pSrc16, NumItems2Write);
      if (r != 0) {
        FS_NOR_EI();
        return 1;
      }
      FS_NOR_EI();
      pSrc16   += NumItems2Write;
      NumItems -= NumItems2Write;
      DestAddr += ((unsigned)NumItems2Write << 1);
    }
    if (NumItems != 0u) {
      do {
        NumItems2Write = SEGGER_MIN(NumItems, ((unsigned)FS_NOR_INTEL_WRITE_BUFFER_SIZE >> 1));
        FS_NOR_DI();
        r = _INTEL_WritePage(Unit, BaseAddr, SectorAddr, DestAddr, pSrc16, NumItems2Write);
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

#endif // FS_NOR_INTEL_WRITE_BUFFER_SIZE != 0

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
*    ErrorMask        Bit mask that indicates which error bits of the status
*                     register have to be checked.
*    TimeOut          Maximum number of software cycles to wait for the operation to finish.
*                     0 means wait indefinitely.
*
*  Return value
*    ==0    O.K., phy. sector is erased or data programmed.
*    !=0    An error occurred.
*/
static int _AMD_WaitForEndOfOperation(unsigned BaseAddr, unsigned StatusAddr, U16 ErrorMask, U32 TimeOut) {
#if (FS_NOR_AMD_STATUS_CHECK_TYPE == 2)
  U16 Status;

  FS_USE_PARA(StatusAddr);
  //
  // Waits for the end of operation as required by Cypress HyperFlash.
  //
  for (;;) {
    AMD_READ_STATUS(BaseAddr, Status);
    //
    // Check if the operation finished.
    //
    if ((Status & (1uL << HYPERFLASH_STATUS_RDB)) != 0u) {
      if ((Status & ErrorMask) != 0u) {
        return 1;                     // Error, operation failed.
      }
      break;
    }
    if (TimeOut != 0u) {
      if (--TimeOut == 0u) {
        return 1;                     // Error, timeout expired.
      }
    }
    FS_NOR_DELAY();
  }
#else
  unsigned                        d0;
  unsigned                        d1;
  volatile const U16 FS_NOR_FAR * pStatus;

  FS_USE_PARA(BaseAddr);
  pStatus = SEGGER_ADDR2PTR(volatile const U16 FS_NOR_FAR, StatusAddr);
  for (;;) {
    d0 = *pStatus;
    d1 = *pStatus;
    //
    // Bit 6 toggles as long as the operation is still in progress.
    //
    if (((d0 ^ d1) & (1uL << AMD_STATUS_TOGGLE_BIT)) == 0u) {
      break;                          // Operation finished successfully.
    }
    if (ErrorMask != 0u) {
      if ((d0 & ErrorMask) != 0u) {   // Check for program / erase errors.
        d0 = *pStatus;
        d1 = *pStatus;
        //
        // Bit 6 toggles as long as the operation is still in progress.
        //
        if (((d0 ^ d1) & (1uL << AMD_STATUS_TOGGLE_BIT)) == 0u) {
          break;                      // Operation finished successfully.
        }
        return 1;                     // Error, could not program or erase.
      }
    }
    if (TimeOut != 0u) {
      if (--TimeOut == 0u) {
        return 1;                     // Error, timeout expired.
      }
    }
    FS_NOR_DELAY();
  }
#endif
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
  volatile const U16 FS_NOR_FAR * pSrc16;
  volatile U16 FS_NOR_FAR       * pDest;
  volatile U16 FS_NOR_FAR       * pStatus;
  unsigned                        i;
  int                             r;
  unsigned                        Addr;

  FS_USE_PARA(Unit);
  FS_USE_PARA(SectorAddr);
  pSrc16 = (volatile const U16 FS_NOR_FAR *)pSrc;   //lint !e740 !e826 !e9079 !e9087
  pDest  = SEGGER_ADDR2PTR(volatile U16 FS_NOR_FAR, DestAddr);
  //
  // Write data to internal buffer of NOR flash.
  //
  AMD_LOAD_BUFFER(BaseAddr, SectorAddr, NumItems);  // "Write to Buffer" command
  for (i = 0; i < NumItems; i++) {
    *(pDest + i) = *(pSrc16 + i);
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
*      _AMD_EraseSector
*
*  Function description
*    Erases one physical sector.
*
*  Return value
*    ==0    O.K., sector is erased
*    !=0    Error, sector may not be erased
*/
static int _AMD_EraseSector(U8 Unit, unsigned BaseAddr, unsigned SectorAddr) {
  volatile U16 FS_NOR_FAR * pB;
  int                       r;

  FS_USE_PARA(Unit);
  pB = SEGGER_ADDR2PTR(volatile U16 FS_NOR_FAR, SectorAddr);
  FS_NOR_DI();
  AMD_ERASE_BLOCK(BaseAddr, SectorAddr);
  r = AMD_WAIT_FOR_ERASE_END(BaseAddr, SectorAddr);
  if (r == 0) {
    if (*pB != 0xFFFFu) {
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
*    Writes data into NOR flash.
*
*  Return value
*    ==0    O.K., data has been written
*    !=0    An error occurred
*/
static int _AMD_Write(U8 Unit, unsigned BaseAddr, unsigned SectorAddr, unsigned DestAddr, const U16 FS_NOR_FAR * pSrc, unsigned NumItems) {
  const U16    FS_NOR_FAR * pSrc16;
  volatile U16 FS_NOR_FAR * pDest;
  U16                       Data16;
  int                       r;
  unsigned                  Addr;

  FS_USE_PARA(Unit);
  FS_USE_PARA(SectorAddr);
  ASSERT_IS_DATA_ALIGNED(DestAddr, pSrc);
  pSrc16 = SEGGER_CONSTPTR2PTR(const U16 FS_NOR_FAR, pSrc);
  pDest  = SEGGER_ADDR2PTR(volatile U16 FS_NOR_FAR, DestAddr);
  FS_NOR_DI();
  AMD_RESET(BaseAddr);
  FS_NOR_EI();
  while (NumItems > 0u) {
    Data16 = *pSrc16;
    FS_NOR_DI();
    AMD_PROGRAM(BaseAddr);
    *pDest = Data16;
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
    // Check the result of the write operation.
    //
    if (*pDest != Data16) {
      AMD_RESET(BaseAddr);
      FS_NOR_EI();
      return 1;               // Error, data does not match.
    }
    FS_NOR_EI();
    NumItems--;
    ++pDest;
    ++pSrc16;
  }
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

  FS_USE_PARA(Unit);
  ASSERT_IS_DATA_ALIGNED(DestAddr, pSrc);
  pSrc16 = SEGGER_CONSTPTR2PTR(const U16 FS_NOR_FAR, pSrc);
  FS_NOR_DI();
  AMD_RESET(BaseAddr);
  FS_NOR_EI();
  while (NumItems > 0u) {
    if ((DestAddr & ((unsigned)FS_NOR_AMD_WRITE_BUFFER_SIZE - 1u)) != 0u) {
      NumItems2Write = ((unsigned)FS_NOR_AMD_WRITE_BUFFER_SIZE - (DestAddr & ((unsigned)FS_NOR_AMD_WRITE_BUFFER_SIZE - 1u))) >> 1;
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
        NumItems2Write = SEGGER_MIN(NumItems, ((unsigned)FS_NOR_AMD_WRITE_BUFFER_SIZE >> 1));
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
*    Reads data from the specified address in NOR flash.
*/
static int _Read(U8 Unit, void * pDest, unsigned SrcAddr, U32 NumBytes) {
  FS_USE_PARA(Unit);
  FS_MEMCPY(pDest, SEGGER_ADDR2PTR(const void, SrcAddr), NumBytes);
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
*      FS_NOR_CFI_ReadCFI_1x16
*
*  Function description
*    Reads CFI data from hardware into buffer.
*    Note that every 16-bit value from flash contains just a single byte.
*/
void FS_NOR_CFI_ReadCFI_1x16(U8 Unit, U32 BaseAddr, U32 Off, U8 * pData, unsigned NumItems) {
  volatile U16 FS_NOR_FAR * pAddr;
  int                       r;
  unsigned                  Addr;

  FS_USE_PARA(Unit);
  //
  // We initially need to check whether the flash is fully CFI compliant.
  //
  if (_IsInited == 0u) {
    U8 aData[3];

    FS_MEMSET(aData, 0, sizeof(aData));
    Addr = BaseAddr + (0x10uL << 1);                // We retrieve the 'QRY' information from offset 0x10 (x16).
    pAddr = SEGGER_ADDR2PTR(volatile U16 FS_NOR_FAR, Addr);
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
    aData[0] = (U8)*pAddr++;
    aData[1] = (U8)*pAddr++;
    aData[2] = (U8)*pAddr++;
    CFI_RESET(BaseAddr);
    FS_NOR_EI();
    if (   (aData[0] == (U8)'Q')
        && (aData[1] == (U8)'R')
        && (aData[2] == (U8)'Y')) {
        _IsCFICompliant = 1;
    }
    _IsInited = 1;
  }
  Addr = BaseAddr + (Off << 1);
  pAddr = SEGGER_ADDR2PTR(volatile U16 FS_NOR_FAR, Addr);
  FS_NOR_DI();
  //
  // Write the correct CFI-query sequence
  //
  if (_IsCFICompliant != 0u) {
    CFI_READ_CONFIG(BaseAddr);
  } else {
    CFI_READ_CONFIG_NON_COMPLIANT(BaseAddr);
  }
  //
  // Read the data
  //
  do {
    *pData++ = (U8)*pAddr++;    // Only the low byte of the CFI data is relevant.
  } while (--NumItems != 0u);
  //
  // Perform a reset, which means, return from read CFI mode to normal read mode.
  //
  CFI_RESET(BaseAddr);
  FS_NOR_EI();
}

/*********************************************************************
*
*      Public data
*
**********************************************************************
*/
const FS_NOR_PROGRAM_HW FS_NOR_Program_Intel_1x16 = {
  _Read,
  _INTEL_EraseSector,
  _INTEL_Write
};

const FS_NOR_PROGRAM_HW FS_NOR_Program_AMD_1x16 = {
  _Read,
  _AMD_EraseSector,
  _AMD_Write
};

const FS_NOR_PROGRAM_HW FS_NOR_Program_IntelFast_1x16 = {
  _Read,
  _INTEL_EraseSector,
#if (FS_NOR_INTEL_WRITE_BUFFER_SIZE != 0)   // Just for testing purposes.
  _INTEL_WriteFast
#else
  _INTEL_Write
#endif // FS_NOR_INTEL_WRITE_BUFFER_SIZE != 0
};

const FS_NOR_PROGRAM_HW FS_NOR_Program_AMDFast_1x16 = {
  _Read,
  _AMD_EraseSector,
#if (FS_NOR_AMD_WRITE_BUFFER_SIZE != 0)     // Just for testing purposes.
  _AMD_WriteFast
#else
  _AMD_Write
#endif // FS_NOR_AMD_WRITE_BUFFER_SIZE != 0
};

/*************************** End of file ****************************/
