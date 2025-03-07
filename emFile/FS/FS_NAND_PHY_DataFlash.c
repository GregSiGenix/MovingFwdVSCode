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
File        : FS_NAND_PHY_DataFlash.c
Purpose     : Physical layer for Atmel/Adesto DataFlash
Additional information:
  The page data is read via the internal RAM buffer of DataFlash if
  FS_NAND_SUPPORT_READ_CACHE is set to 1 which is default. This provides
  some read performance improvement if the size of the logical sector
  used by the file system is smaller than the page size of DataFlash.
  The AT45DB161E device seems to have a problem when reading via RAM buffer.
  At random time intervals the device starts returning the same data
  regardless of the page actually requested. It is recommended to
  configure the file system with FS_NAND_SUPPORT_READ_CACHE set to 0
  for this particular device.
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#include "FS_Int.h"

/*********************************************************************
*
*       Defines, fixed
*
**********************************************************************
*/

/*********************************************************************
*
*       Commands
*/
#if (FS_NAND_BLOCK_TYPE <= 1)
  #define BLOCK_ERASE                 0x50
#endif
#define PAGE_TO_BUFFER                0x53
#define BUFFER_READ                   0x54
#if (FS_NAND_BLOCK_TYPE == 2)
  #define SECTOR_ERASE                0x7C
#endif
#define WRITE_TO_BUFFER               0x84
#define BUFFER_TO_PAGE_WITHOUT_ERASE  0x88
#define READ_DEVICE_ID                0x9F
#if (FS_NAND_SUPPORT_READ_CACHE == 0)
  #define MAIN_MEMORY_PAGE_READ       0xD2
#endif
#define BUFFER_READ_FAST              0xD4
#define READ_STATUS                   0xD7

/*********************************************************************
*
*       DataFlash types
*/
#define FLASH_1MBIT                   0x03u
#define FLASH_2MBIT                   0x05u
#define FLASH_4MBIT                   0x07u
#define FLASH_8MBIT                   0x09u
#define FLASH_16MBIT                  0x0bu
#define FLASH_32MBIT                  0x0du
#define FLASH_64MBIT                  0x0fu
#define FLASH_128MBIT                 0x04u

/*********************************************************************
*
*       Misc. defines
*/
#define COMMAND_LENGTH                0x04
#define PAGE_INDEX_INVALID            0xFFFFFFFFu

/*********************************************************************
*
*       ASSERT_UNIT_NO_IS_IN_RANGE
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)                                        \
    if ((Unit) >= (U8)FS_NAND_NUM_UNITS) {                                        \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NAND_PHY_DF: Invalid unit number.")); \
      FS_X_PANIC(FS_ERRCODE_UNKNOWN_DEVICE);                                      \
    }
#else
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)
#endif

/*********************************************************************
*
*       ASSERT_HW_TYPE_IS_SET
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_HW_TYPE_IS_SET(Unit)                                          \
    if (_aInst[Unit].pHWType == NULL) {                                        \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NAND_PHY_DF: HW layer not set.")); \
      FS_X_PANIC(FS_ERRCODE_HW_LAYER_NOT_SET);                                 \
    }
#else
  #define ASSERT_HW_TYPE_IS_SET(Unit)
#endif

/*********************************************************************
*
*       Local data types
*
**********************************************************************
*/

/*********************************************************************
*
*       NAND_DF_INST
*/
typedef struct {
  U8                         ldBytesPerPage;
  U8                         ReadBufferCmd;
  U8                         ldPagesPerVPage;
  U8                         ldBytesPerPageMin;
  U16                        BytesPerPageData;
  U16                        BytesPerPageSpare;
  U16                        PagesPerBlock;
  U8                         NumBytesStatus;
#if FS_NAND_SUPPORT_READ_CACHE
  U32                        PageIndexCached;
#endif
  const FS_NAND_HW_TYPE_DF * pHWType;
} NAND_DF_INST;

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static NAND_DF_INST _aInst[FS_NAND_NUM_UNITS];

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
static U16 _ld(U32 Value) {
  U16 i;

  for (i = 0; i < 16u; i++) {
    if ((1uL << i) == Value) {
      break;
    }
  }
  return i;
}

/*********************************************************************
*
*       _Init
*/
static int _Init(U8 Unit) {
  NAND_DF_INST * pInst;
  int            r;

  pInst = &_aInst[Unit];
  r = pInst->pHWType->pfInit(Unit);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_PHY_DF: Could not initialize HW."));
  }
  return r;
}

/*********************************************************************
*
*       _EnableCS
*/
static void _EnableCS(U8 Unit) {
  NAND_DF_INST * pInst;

  pInst = &_aInst[Unit];
  pInst->pHWType->pfEnableCS(Unit);
}

/*********************************************************************
*
*       _DisableCS
*/
static void _DisableCS(U8 Unit) {
  NAND_DF_INST * pInst;

  pInst = &_aInst[Unit];
  pInst->pHWType->pfDisableCS(Unit);
}

/*********************************************************************
*
*       _Read
*/
static void _Read(U8 Unit, U8 * pData, int NumBytes) {
  NAND_DF_INST * pInst;

  pInst = &_aInst[Unit];
  pInst->pHWType->pfRead(Unit, pData, NumBytes);
}

/*********************************************************************
*
*       _Write
*/
static void _Write(U8 Unit, const U8 * pData, int NumBytes) {
  NAND_DF_INST * pInst;

  pInst = &_aInst[Unit];
  pInst->pHWType->pfWrite(Unit, pData, NumBytes);
}

/*********************************************************************
*
*       _IsDeviceReady
*
*  Function description
*    Checks if the device is ready for a new command.
*
*  Parameter
*    Status   First byte of the status register.
*
*  Return value
*    ==1      Device is ready
*    ==0      Device is not ready
*/
static int _IsDeviceReady(U8 Status) {
  if ((Status & (1u << 7)) != 0u) {
    return 1;       // Device ready.
  }
  return 0;         // Device not ready.
}

/*********************************************************************
*
*       _HasDeviceError
*
*  Function description
*    Checks if a program or an erase error occurred.
*
*  Parameter
*    Status   Second byte of the status register (if available)
*
*  Return value
*    ==1      An error occurred
*    ==0      OK, operation successful
*/
static int _HasDeviceError(U8 Status) {
  if ((Status & (1u << 5)) != 0u) {
    return 1;
  }
  return 0;
}

/*********************************************************************
*
*       _HasDeviceSpareArea
*
*  Function description
*    Checks if the DataFlash has a spare area.
*/
static int _HasDeviceSpareArea(U8 Status) {
  //
  // When bit 0 of status register is set to 1,
  // then flash does not have a spare area.
  //
  if ((Status & 1u) != 0u) {
    return 0;         // No spare area.
  }
  return 1;           // OK, the device has spare area.
}

/*********************************************************************
*
*       _GetDeviceType
*
*  Function description
*    Returns the device type which is used to determine its capacity.
*/
static U8 _GetDeviceType(U8 Status) {
  Status = (Status >> 2) & 0x0Fu;
  return Status;
}

/*********************************************************************
*
*       _SendDummyBytes
*/
static void _SendDummyBytes(U8 Unit, int NumBytes) {
  U32 DummyData;

  DummyData = 0xFFFFFFFFu;
  while (NumBytes >= 4) {
    _Write(Unit, SEGGER_PTR2PTR(U8, &DummyData), 4);
    NumBytes -= 4;
  }
  if (NumBytes != 0) {
    _Write(Unit, SEGGER_PTR2PTR(U8, &DummyData), NumBytes);
  }
}

/*********************************************************************
*
*       _SendCommandPara
*
*  Function description
*    Sends a command with an additional parameter to DataFlash.
*
*  Parameters
*    Unit         Specifies which DataFlash unit.
*    Command      Index of the command that shall be sent.
*    Para         Additional parameter to the command.
*    CSHandling   Indicates if _SendCommand() shall take care of CS handling.
*/
static void _SendCommandPara(U8 Unit, U8 Command, U32 Para, int CSHandling) {
  U8 aData[COMMAND_LENGTH];

  aData[0] = Command;
  aData[1] = (U8)((Para >> 16) & 0xFFu);
  aData[2] = (U8)((Para >>  8) & 0xFFu);
  aData[3] = (U8)( Para        & 0xFFu);
  if (CSHandling != 0) {
    _EnableCS(Unit);
  }
  _Write(Unit, aData, COMMAND_LENGTH);
  if (CSHandling != 0) {
    _DisableCS(Unit);
  }
}

/*********************************************************************
*
*       _ReadStatus
*
*  Function description
*    Reads the contents of the status register. On legacy devices
*    the status register is 1 byte large while on newer devices 2 bytes.
*    The second byte contains a flag indicating whether the program or erase
*    operation failed.
*
*  Parameters
*    Unit         Specifies which DataFlash unit.
*    pStatus      [OUT] Status read from DataFlash.
*    NumBytes     Number of status bytes to read.
*/
static void _ReadStatus(U8 Unit, U8 * pStatus, unsigned NumBytes) {
  U8 Command;

  Command = READ_STATUS;
  _EnableCS(Unit);
  _Write(Unit, &Command, 1);
  _Read(Unit, pStatus, (int)NumBytes);
  _DisableCS(Unit);
}

/*********************************************************************
*
*       _WaitUntilReady
*
*  Function description
*    Waits until the DataFlash unit is ready.
*
*  Parameters
*    Unit   Specifies which DataFlash unit.
*
*  Return value
*    ==0    OK, operation successful
*    !=0    An error occurred
*/
static int _WaitUntilReady(U8 Unit) {
  U8             aStatus[2];
  U8             NumBytesStatus;
  NAND_DF_INST * pInst;

  pInst = &_aInst[Unit];
  NumBytesStatus = pInst->NumBytesStatus;
  for (;;) {
    _ReadStatus(Unit, aStatus, NumBytesStatus);
    if (_IsDeviceReady(aStatus[0]) != 0) {
      if (NumBytesStatus > 1u) {
        if (_HasDeviceError(aStatus[1]) != 0) {
          return 1;         // Error, program or erase operation failed.
        }
      }
      return 0;             // Operation successful
    }
  }
}

/*********************************************************************
*
*       _GetPageIndexCached
*
*  Function description
*    Returns the index of the page stored in the internal RAM of DataFlash.
*/
static U32 _GetPageIndexCached(const NAND_DF_INST * pInst) {
  U32 PageIndex;

#if FS_NAND_SUPPORT_READ_CACHE
  PageIndex = pInst->PageIndexCached;
#else
  FS_USE_PARA(pInst);
  PageIndex = PAGE_INDEX_INVALID;
#endif
  return PageIndex;
}

/*********************************************************************
*
*       _SetPageIndexCached
*
*  Function description
*    Saves to instance the index of the page stored to internal RAM of DataFlash.
*/
static void _SetPageIndexCached(NAND_DF_INST * pInst, U32 PageIndex) {
#if FS_NAND_SUPPORT_READ_CACHE
  pInst->PageIndexCached = PageIndex;
#else
  FS_USE_PARA(pInst);
  FS_USE_PARA(PageIndex);
#endif
}

#if FS_NAND_SUPPORT_READ_CACHE

/*********************************************************************
*
*       _ReadFromBuffer
*
*  Function description
*    Read data from the internal buffer of DataFlash.
*
*  Parameters
*    Unit       Specifies which DataFlash unit.
*    Off        Byte offset within the buffer.
*    p          Pointer to a buffer to store the data.
*    NumBytes   Number of byte to read.
*/
static void _ReadFromBuffer(U8 Unit, unsigned Off, U8 * p, unsigned NumBytes) {
  _EnableCS(Unit);
  _SendCommandPara(Unit, _aInst[Unit].ReadBufferCmd, Off, 0);
  _SendDummyBytes(Unit, 1);
  _Read(Unit, p, (int)NumBytes);
  _DisableCS(Unit);
  (void)_WaitUntilReady(Unit);
}

/*********************************************************************
*
*       _ReadData
*
*  Function description
*    Reads data from DataFlash.
*    Typically this function is called to read either data from the
*    data area or from spare area or both.
*
*  Parameters
*    Unit           Specifies which DataFlash unit.
*    PageIndex      Index of the page that shall be read.
*    pData          Pointer to a buffer to store the data in - May be NULL if not needed.
*    OffData        Offset with the page to read from.
*    NumBytesData   Number of byte to read from data area.
*    pSpare         Pointer to a buffer to store the spare area data  - May be NULL if not needed.
*    OffSpare       Offset with the page to read from.
*    NumBytesSpare  Number of bytes to read from spare area.
*/
static void _ReadData(U8 Unit, U32 PageIndex, void * pData, unsigned OffData, unsigned NumBytesData, void * pSpare, unsigned OffSpare, unsigned NumBytesSpare) {
  U32            Addr;
  U8           * pData8;
  U8           * pSpare8;
  NAND_DF_INST * pInst;
  unsigned       NumBytesAtOnce;
  unsigned       BytesPerPageData;
  unsigned       BytesPerPageSpare;
  unsigned       BytesPerVPageData;
  unsigned       ldBytesPerPage;
  unsigned       ldPagesPerVPage;
  U32            PageIndexCached;

  pInst = &_aInst[Unit];
  pData8             = SEGGER_PTR2PTR(U8, pData);
  pSpare8            = SEGGER_PTR2PTR(U8, pSpare);
  BytesPerPageData   = pInst->BytesPerPageData;
  BytesPerPageSpare  = pInst->BytesPerPageSpare;
  ldBytesPerPage     = pInst->ldBytesPerPage;
  ldPagesPerVPage    = pInst->ldPagesPerVPage;
  //
  // Adjust the page number according to the size of the virtual page.
  //
  PageIndex <<= ldPagesPerVPage;
  //
  // Make the offset relative to the begin of spare area.
  //
  BytesPerVPageData = BytesPerPageData << ldPagesPerVPage;
  if (OffSpare < BytesPerVPageData) {
    OffSpare = 0;
  } else {
    OffSpare -= BytesPerVPageData;
  }
  //
  // Read data from the DataFlash and copy it to specified buffers.
  //
  PageIndexCached = pInst->PageIndexCached;
  for (;;) {
    Addr = PageIndex << (ldBytesPerPage + 1u);
    //
    // If required, read the data into the internal buffer of the DataFlash.
    //
    if (PageIndexCached != PageIndex) {
      _SendCommandPara(Unit, PAGE_TO_BUFFER, Addr, 1);
      (void)_WaitUntilReady(Unit);
      PageIndexCached = PageIndex;
    }
    //
    // Read from the data area of DataFlash internal buffer.
    //
    if (pData != NULL) {
      if (NumBytesData != 0u) {
        if (OffData < BytesPerPageData) {
          NumBytesAtOnce = BytesPerPageData - OffData;
          NumBytesAtOnce = SEGGER_MIN(NumBytesAtOnce, NumBytesData);
          _ReadFromBuffer(Unit, OffData, pData8, NumBytesAtOnce);
          NumBytesData -= NumBytesAtOnce;
          pData8       += NumBytesAtOnce;
          OffData       = 0;
        } else {
          OffData -= BytesPerPageData;
        }
      }
    } else {
      NumBytesData = 0;
    }
    //
    // Read from the spare area of DataFlash internal buffer.
    //
    if (pSpare != NULL) {
      if (NumBytesSpare != 0u) {
        if (OffSpare < BytesPerPageSpare) {
          NumBytesAtOnce = BytesPerPageSpare - OffSpare;
          NumBytesAtOnce = SEGGER_MIN(NumBytesAtOnce, NumBytesSpare);
          _ReadFromBuffer(Unit, OffSpare + BytesPerPageData, pSpare8, NumBytesAtOnce);
          NumBytesSpare -= NumBytesAtOnce;
          pSpare8       += NumBytesAtOnce;
          OffSpare       = 0;
        } else {
          OffSpare      -= BytesPerPageSpare;
        }
      }
    } else {
      NumBytesSpare = 0;
    }
    PageIndex++;
    if ((NumBytesData == 0u) && (NumBytesSpare == 0u)) {
      break;
    }
  }
  pInst->PageIndexCached = PageIndexCached;
}

#else

/*********************************************************************
*
*       _ReadFromMemory
*
*  Function description
*    Read data directly from the main memory of DataFlash bypassing the internal buffers.
*
*  Parameters
*    Unit           Specifies which DataFlash unit.
*    PageIndex      Index of the page to read from.
*    Off            Byte offset within the page.
*    pMain          Pointer to a buffer to store the data from the main area.
*    NumBytesMain   Number of bytes to read from the main area.
*    pSpare         Pointer to a buffer to store the data from the spare area.
*    NumBytesSpare  Number of bytes to read from the spare area.
*/
static void _ReadFromMemory(U8 Unit, unsigned PageIndex, unsigned Off, U8 * pMain, unsigned NumBytesMain, U8 * pSpare, unsigned NumBytesSpare) {
  U32            Addr;
  unsigned       ldBytesPerPage;
  NAND_DF_INST * pInst;
  U32            Mask;

  pInst = &_aInst[Unit];
  ldBytesPerPage = (unsigned)pInst->ldBytesPerPage + 1u;  // + 1 because of the spare area.
  Mask  = (1uL << ldBytesPerPage) - 1u;
  Addr  = (U32)PageIndex << ldBytesPerPage;
  Addr |= Mask & Off;
  _EnableCS(Unit);
  _SendCommandPara(Unit, MAIN_MEMORY_PAGE_READ, Addr, 0);
  _SendDummyBytes(Unit, 4);                               // 4 dummy bytes after the address are required.
  if (pMain != NULL) {
    _Read(Unit, pMain, (int)NumBytesMain);
  }
  if (pSpare != NULL) {
    _Read(Unit, pSpare, (int)NumBytesSpare);
  }
  _DisableCS(Unit);
}

/*********************************************************************
*
*       _ReadData
*
*  Function description
*    Reads data from DataFlash.
*    Typically this function is called to read either data from the
*    data area or from spare area or both.
*
*  Parameters
*    Unit           Specifies which DataFlash unit.
*    PageIndex      Index of the page that shall be read.
*    pData          Pointer to a buffer to store the data in - May be NULL if not needed.
*    OffData        Offset with the page to read from.
*    NumBytesData   Number of byte to read from data area.
*    pSpare         Pointer to a buffer to store the spare area data  - May be NULL if not needed.
*    OffSpare       Offset with the page to read from.
*    NumBytesSpare  Number of bytes to read from spare area.
*/
static void _ReadData(U8 Unit, U32 PageIndex, void * pData, unsigned OffData, unsigned NumBytesData, void * pSpare, unsigned OffSpare, unsigned NumBytesSpare) {
  U8           * pData8;
  U8           * pSpare8;
  NAND_DF_INST * pInst;
  unsigned       NumBytesAtOnce;
  unsigned       BytesPerPageData;
  unsigned       BytesPerPageSpare;
  unsigned       BytesPerVPageData;
  unsigned       ldPagesPerVPage;
  unsigned       OffDataRead;
  U8           * pDataRead;
  unsigned       NumBytesDataRead;
  unsigned       OffSpareRead;
  U8           * pSpareRead;
  unsigned       NumBytesSpareRead;

  pInst = &_aInst[Unit];
  pData8             = SEGGER_PTR2PTR(U8, pData);
  pSpare8            = SEGGER_PTR2PTR(U8, pSpare);
  BytesPerPageData   = pInst->BytesPerPageData;
  BytesPerPageSpare  = pInst->BytesPerPageSpare;
  ldPagesPerVPage    = pInst->ldPagesPerVPage;
  //
  // Adjust the page number according to the size of the virtual page.
  //
  PageIndex <<= ldPagesPerVPage;
  //
  // Make the offset relative to the begin of spare area.
  //
  BytesPerVPageData = BytesPerPageData << ldPagesPerVPage;
  if (OffSpare < BytesPerVPageData) {
    OffSpare = 0;
  } else {
    OffSpare -= BytesPerVPageData;
  }
  //
  // Read data from the DataFlash and copy it to specified buffers.
  //
  for (;;) {
    //
    // Determine the offset and the number of bytes to be read from the data area of DataFlash.
    //
    OffDataRead      = 0;
    pDataRead        = NULL;
    NumBytesDataRead = 0;
    if (pData != NULL) {
      if (NumBytesData != 0u) {
        if (OffData < BytesPerPageData) {
          NumBytesAtOnce    = BytesPerPageData - OffData;
          NumBytesAtOnce    = SEGGER_MIN(NumBytesAtOnce, NumBytesData);
          pDataRead         = pData8;
          OffDataRead       = OffData;
          NumBytesDataRead  = NumBytesAtOnce;
          NumBytesData     -= NumBytesAtOnce;
          pData8           += NumBytesAtOnce;
          OffData           = 0;
        } else {
          OffData          -= BytesPerPageData;
        }
      }
    }
    //
    // Determine the offset and the number of bytes to be read from the spare area of DataFlash.
    //
    OffSpareRead      = 0;
    pSpareRead        = NULL;
    NumBytesSpareRead = 0;
    if (pSpare != NULL) {
      if (NumBytesSpare != 0u) {
        if (OffSpare < BytesPerPageSpare) {
          NumBytesAtOnce     = BytesPerPageSpare - OffSpare;
          NumBytesAtOnce     = SEGGER_MIN(NumBytesAtOnce, NumBytesSpare);
          OffSpareRead       = OffSpare + BytesPerPageData;
          pSpareRead         = pSpare8;
          NumBytesSpareRead  = NumBytesAtOnce;
          NumBytesSpare     -= NumBytesAtOnce;
          pSpare8           += NumBytesAtOnce;
          OffSpare           = 0;
        } else {
          OffSpare          -= BytesPerPageSpare;
        }
      }
    }
    //
    // For performance reasons, try to read the data and the spare area using a single read command.
    //
    if ((OffDataRead + NumBytesDataRead) == OffSpareRead) {
      _ReadFromMemory(Unit, PageIndex, OffDataRead, pDataRead, NumBytesDataRead, pSpareRead, NumBytesSpareRead);
    } else {
      if (pDataRead != NULL) {
        _ReadFromMemory(Unit, PageIndex, OffDataRead,  pDataRead, NumBytesDataRead, NULL, 0);
      }
      if (pSpareRead != NULL) {
        _ReadFromMemory(Unit, PageIndex, OffSpareRead, NULL, 0, pSpareRead, NumBytesSpareRead);
      }
    }
    PageIndex++;
    if ((NumBytesData == 0u) && (NumBytesSpare == 0u)) {
      break;
    }
  }
}

#endif  // FS_NAND_SUPPORT_READ_CACHE

/*********************************************************************
*
*       _ReadDeviceId
*
*  Function description
*    Reads the manufacturer and device identification.
*/
static void _ReadDeviceId(U8 Unit, U8 * pData, unsigned NumBytes) {
  U8 Cmd;

  Cmd = READ_DEVICE_ID;
  _EnableCS(Unit);
  _Write(Unit, &Cmd, (int)sizeof(Cmd));
  _Read(Unit, pData, (int)NumBytes);
  _DisableCS(Unit);
}

/*********************************************************************
*
*       _IsLegacyDevice
*
*  Function description
*    Checks if the DataFlash is a legacy device. This information
*    is typically used to decide how large the status register is.
*/
static int _IsLegacyDevice(U8 Unit) {
  U8 aId[4];

  //
  // It seems that the only way we can identify a current DataFlash device
  // (for example AT45DB641E) is to look at the byte 4 of the manufacturer
  // and device id information. This byte is set to 1 on current DataFlash
  // devices and to 0 on the legacy DataFlash devices (for example AT45DB642D).
  //
  _ReadDeviceId(Unit, aId, sizeof(aId));
  if (aId[3] != 0u) {
    return 0;   // Current device
  }
  return 1;     // Legacy device
}

/*********************************************************************
*
*       _WriteToBuffer
*
*  Function description
*    Writes data to the internal buffer of DataFlash.
*
*  Parameters
*    Unit       Specifies which DataFlash unit.
*    Off        Bytes offset within the buffer.
*    p          Pointer to a buffer to store the data.
*    NumBytes   Number of byte to read.
*/
static void _WriteToBuffer(U8 Unit, unsigned Off, const U8 * p, unsigned NumBytes) {
  _EnableCS(Unit);
  _SendCommandPara(Unit, WRITE_TO_BUFFER, Off, 0);
  _Write(Unit, p, (int)NumBytes);
  _DisableCS(Unit);
  (void)_WaitUntilReady(Unit);
}

/*********************************************************************
*
*       _WriteData
*
*  Function description
*    Writes data to DataFlash.
*
*  Parameters
*    Unit           Specifies which DataFlash unit.
*    PageIndex      Index of the page that shall be written.
*    pData          Pointer to a buffer that holds the data area data.
*    OffData        Offset with the page to write to.
*    NumBytesData   Number of byte to write to data area.
*    pSpare         Pointer to a buffer that holds the spare area data.
*    OffSpare       Offset with the page to write to.
*    NumBytesSpare  Number of bytes to write to spare area.
*
*  Return value
*    ==0    OK, write operation succeeded.
*    !=0    An error occurred.
*/
static int _WriteData(U8 Unit, U32 PageIndex, const void * pData, unsigned OffData, unsigned NumBytesData, const void * pSpare, unsigned OffSpare, unsigned NumBytesSpare) {
  U32            Addr;
  const U8     * pData8;
  const U8     * pSpare8;
  NAND_DF_INST * pInst;
  unsigned       NumBytesAtOnce;
  unsigned       BytesPerPageData;
  unsigned       BytesPerPageSpare;
  unsigned       BytesPerVPageData;
  unsigned       ldBytesPerPage;
  unsigned       ldPagesPerVPage;
  int            r;
  U32            PageIndexCached;

  pInst = &_aInst[Unit];
  pData8             = SEGGER_CONSTPTR2PTR(const U8, pData);
  pSpare8            = SEGGER_CONSTPTR2PTR(const U8, pSpare);
  BytesPerPageData   = pInst->BytesPerPageData;
  BytesPerPageSpare  = pInst->BytesPerPageSpare;
  ldBytesPerPage     = pInst->ldBytesPerPage;
  ldPagesPerVPage    = pInst->ldPagesPerVPage;
  //
  // Adjust the page number according to the size of the virtual page.
  //
  PageIndex <<= ldPagesPerVPage;
  //
  // Make the offset relative to begin of spare area.
  //
  BytesPerVPageData = BytesPerPageData << ldPagesPerVPage;
  if (OffSpare < BytesPerVPageData) {
    OffSpare = 0;
  } else {
    OffSpare -= BytesPerVPageData;
  }
  //
  // Write data to DataFlash from the specified buffers.
  //
  PageIndexCached = _GetPageIndexCached(pInst);
  for (;;) {
    Addr = PageIndex << (ldBytesPerPage + 1u);
    //
    // Read the data into the internal buffer of the DataFlash.
    //
    if (PageIndexCached != PageIndex) {
      _SendCommandPara(Unit, PAGE_TO_BUFFER, Addr, 1);
      (void)_WaitUntilReady(Unit);
      PageIndexCached = PageIndex;
    }
    //
    // Write to data area of the DataFlash internal buffer.
    //
    if (pData != NULL) {
      if (NumBytesData != 0u) {
        if (OffData < BytesPerPageData) {
          NumBytesAtOnce = BytesPerPageData - OffData;
          NumBytesAtOnce = SEGGER_MIN(NumBytesAtOnce, NumBytesData);
          _WriteToBuffer(Unit, OffData, pData8, NumBytesAtOnce);
          NumBytesData -= NumBytesAtOnce;
          pData8       += NumBytesAtOnce;
          OffData       = 0;
        } else {
          OffData      -= BytesPerPageData;
        }
      }
    }
    //
    // Write to spare area of the DataFlash internal buffer.
    //
    if (pSpare != NULL) {
      if (NumBytesSpare != 0u) {
        if (OffSpare < BytesPerPageSpare) {
          NumBytesAtOnce = BytesPerPageSpare - OffSpare;
          NumBytesAtOnce = SEGGER_MIN(NumBytesAtOnce, NumBytesSpare);
          _WriteToBuffer(Unit, OffSpare + BytesPerPageData, pSpare8, NumBytesAtOnce);
          NumBytesSpare -= NumBytesAtOnce;
          pSpare8       += NumBytesAtOnce;
          OffSpare       = 0;
        } else {
          OffSpare      -= BytesPerPageSpare;
        }
      }
    }
    //
    // Write the data back to page.
    //
    _SendCommandPara(Unit, BUFFER_TO_PAGE_WITHOUT_ERASE, Addr, 1);
    r = _WaitUntilReady(Unit);
    if (r != 0) {
      PageIndexCached = PAGE_INDEX_INVALID;
      break;
    }
    PageIndex++;
    if ((NumBytesData == 0u) && (NumBytesSpare == 0u)) {
      break;
    }
  }
  _SetPageIndexCached(pInst, PageIndexCached);
  return r;
}

#if (FS_SUPPORT_TEST != 0) && (FS_NAND_BLOCK_TYPE == 2)

/*********************************************************************
*
*       _EnableSpareArea
*
*  Function description
*    Enables the spare are of DataFlash (256 + 8 = 264 byte page size).
*
*  Parameters
*    Unit       Specifies which DataFlash unit.
*/
static int _EnableSpareArea(U8 Unit) {
  U8  abCmd[] = {0x3D, 0x2A, 0x80, 0xA7};
  U8  Status;
  int r;

  _EnableCS(Unit);
  _Write(Unit, abCmd, (int)sizeof(abCmd));
  _DisableCS(Unit);
  r = _WaitUntilReady(Unit);
  if (r == 0) {
    Status = 0;
    _ReadStatus(Unit, &Status, (int)sizeof(Status));
    if (_HasDeviceSpareArea(Status) == 0) {
      r = 1;
    }
  }
  return r;
}

#endif // FS_SUPPORT_TEST

/*********************************************************************
*
*       Static code (public via callback)
*
**********************************************************************
*/

/*********************************************************************
*
*       _PHY_Read
*
*  Function description
*    Reads data from a complete or a part of a page.
*    This function is used to read either main memory or spare area.
*
*  Return value
*    ==0      Data successfully read.
*    !=0      An error has occurred.
*/
static int _PHY_Read(U8 Unit, U32 PageIndex, void * pBuffer, unsigned Off, unsigned NumBytes) {
  unsigned       OffSpare;
  unsigned       ldPagesPerVPage;
  NAND_DF_INST * pInst;
  U8           * pSpare;
  unsigned       NumBytesSpare;

  pInst = &_aInst[Unit];
  ldPagesPerVPage = pInst->ldPagesPerVPage;
  //
  // Update the byte offset of the spare area according to the size of the configured virtual page.
  //
  OffSpare   = pInst->BytesPerPageData;
  OffSpare >>= ldPagesPerVPage;
  //
  // Read the data.
  //
  if (Off >= OffSpare) {
    _ReadData(Unit, PageIndex, NULL, 0, 0, pBuffer, Off, NumBytes);
  } else {
    if ((Off + NumBytes) <= OffSpare) {
      _ReadData(Unit, PageIndex, pBuffer, Off, NumBytes, NULL, 0, 0);
    } else {
      NumBytesSpare  = (Off + NumBytes) - OffSpare;
      NumBytes      -= NumBytesSpare;
      pSpare         = SEGGER_PTR2PTR(U8, pBuffer) + NumBytes;
      _ReadData(Unit, PageIndex, pBuffer, Off, NumBytes, SEGGER_PTR2PTR(void, pSpare), OffSpare, NumBytesSpare);
    }
  }
  return 0;
}

/*********************************************************************
*
*       _PHY_ReadEx
*
*  Function description
*    Reads data from 2 parts of a page.
*    Typically used to read data and spare area at the same time.
*
*  Return value
*    ==0      Data successfully read.
*    !=0      An error has occurred.
*/
static int _PHY_ReadEx(U8 Unit, U32 PageIndex, void * pBuffer0, unsigned Off0, unsigned NumBytes0, void * pBuffer1, unsigned Off1, unsigned NumBytes1) {
  _ReadData(Unit, PageIndex, pBuffer0, Off0, NumBytes0, pBuffer1, Off1, NumBytes1);
  return 0;
}

/*********************************************************************
*
*       _PHY_Write
*
*  Function description
*    Writes data into a complete or a part of a page.
*    This code is identical for main memory and spare area; the spare area
*    is located right after the main area.
*
*  Return value
*    ==0    Data successfully written.
*    !=0    An error has occurred.
*/
static int _PHY_Write(U8 Unit, U32 PageIndex, const void * pBuffer, unsigned Off, unsigned NumBytes) {
  unsigned       OffSpare;
  unsigned       ldPagesPerVPage;
  NAND_DF_INST * pInst;
  int            r;

  pInst = &_aInst[Unit];
  ldPagesPerVPage = pInst->ldPagesPerVPage;
  //
  // Update the byte offset of the spare area according to the size of the configured virtual page.
  //
  OffSpare   = pInst->BytesPerPageData;
  OffSpare >>= ldPagesPerVPage;
  //
  // Write the data.
  //
  if (Off < OffSpare) {
    r = _WriteData(Unit, PageIndex, pBuffer, Off, NumBytes, NULL, 0, 0);
  } else {
    r = _WriteData(Unit, PageIndex, NULL, 0, 0, pBuffer, Off, NumBytes);
  }
  return r;
}

/*********************************************************************
*
*       _PHY_WriteEx
*
*  Function description
*    Writes data to 2 parts of a page.
*    Typically used to write data and spare area at the same time.
*
*  Return value
*    ==0    Data successfully written.
*    !=0    An error has occurred.
*/
static int _PHY_WriteEx(U8 Unit, U32 PageIndex, const void * pBuffer0, unsigned Off0, unsigned NumBytes0, const void * pBuffer1, unsigned Off1, unsigned NumBytes1) {
  int r;

  r = _WriteData(Unit, PageIndex, pBuffer0, Off0, NumBytes0, pBuffer1, Off1, NumBytes1);
  return r;
}

#if (FS_NAND_BLOCK_TYPE == 2)     // Use DataFlash sectors as blocks.

/*********************************************************************
*
*       _PHY_EraseBlock
*
*  Function description
*    Erases a single block of the DataFlash.
*    On DataFlash devices there are 3 different erase sizes which can be used:
*    Page-wise   -> One page is erased (The size of one page depends on the device density)
*    Block-wise  -> One block is erased. A block consists of 8 pages
*    Sector-wise -> One sector is erased. A sector consists of multiple blocks (How many blocks build a sector depends on the device density)
*
*    We use the erase-sector command in order to erase a DataFlash sector, since on ATMEL DataFlashes cumulative erasing/programming
*    actions within one sector has an influence on the data of other pages within the same sector.
*
*    \\fileserver\techinfo\Company\Atmel\DataFLASH\AT45DB161D_doc3500.pdf
*    11.3 AutoPage Rewrite
*    Each page within a sector must be updated/rewritten at least once
*    within every 10,000 cumulative page erase/program operations in that sector.
*
*  Parameters
*    Unit              Driver index.
*    FirstPageIndex    Index of first page in the block that shall be erased.
*
*  Return value
*    ==0    Block successfully erased
*    ==1    An error has occurred
*/
static int _PHY_EraseBlock(U8 Unit, U32 FirstPageIndex) {
  NAND_DF_INST * pInst;
  U32            Addr;
  unsigned       ldBytesPerPage;
  unsigned       ldPagesPerVPage;
  int            r;

  pInst = &_aInst[Unit];
  ldBytesPerPage  = pInst->ldBytesPerPage;
  ldPagesPerVPage = pInst->ldPagesPerVPage;
  //
  // Update the page index according to the size of virtual page.
  //
  FirstPageIndex <<= ldPagesPerVPage;
  //
  // Block 0 needs special treatment, since it sub-divided into 2 sectors 0a (8 pages) and 0b (248 pages)
  //
  Addr = FirstPageIndex << (ldBytesPerPage + 1u);
  if (Addr == 0u) {
    _SendCommandPara(Unit, SECTOR_ERASE, 0, 1);
    r = _WaitUntilReady(Unit);
    if (r == 0) {
      _SendCommandPara(Unit, SECTOR_ERASE, 1uL << (ldBytesPerPage + 4u), 1);
      r = _WaitUntilReady(Unit);
    }
  } else {
    _SendCommandPara(Unit, SECTOR_ERASE, Addr, 1);
    r = _WaitUntilReady(Unit);
  }
  _SetPageIndexCached(pInst, PAGE_INDEX_INVALID);
  return r;
}

/*********************************************************************
*
*       _PHY_InitGetDeviceInfo
*
*  Function description
*    Initializes hardware layer, resets DataFlash flash and tries to identify it.
*    If the DataFlash flash can be handled, the device information is filled.
*
*  Return value
*    ==0    O.K., device can be handled.
*    !=0    Error: device can not be handled.
*/
static int _PHY_InitGetDeviceInfo(U8 Unit, FS_NAND_DEVICE_INFO * pDevInfo) {
  U8             Status;
  U16            NumBlocks;
  U16            BytesPerPage;
  U16            PagesPerBlock;
  unsigned       ldBytesPerPage;
  unsigned       ldPagesPerVPage;
  unsigned       ldBytesPerPageMin;
  NAND_DF_INST * pInst;
  int            r;
  U8             NumBytesStatus;
  U8             ReadBufferCmd;
  unsigned       Type;
  int            IsLegacyDevice;

  pInst = &_aInst[Unit];
  ASSERT_HW_TYPE_IS_SET(Unit);
  r = _Init(Unit);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_PHY_DF: _PHY_InitGetDeviceInfo: HW reports error."));
    return 1;
  }
  _ReadStatus(Unit, &Status, 1);  // The DataFlash type is stored on the fist byte of status register.
  r              = 0;             // Set to indicate success.
  NumBytesStatus = 1;             // The status register on legacy DataFlash devices is 1 byte large.
  ReadBufferCmd  = BUFFER_READ;   // Per default use the slow read command.
  PagesPerBlock  = 0;
  NumBlocks      = 0;
  BytesPerPage   = 0;
#if FS_SUPPORT_TEST
  IsLegacyDevice = 1;
#endif // FS_SUPPORT_TEST
  Type = _GetDeviceType(Status);
  switch (Type) {
  case FLASH_1MBIT:
    BytesPerPage  = 256;
    PagesPerBlock = 256;      // 64-KB sectors
    NumBlocks     = 2;        // 128-KB Total
    break;
  case FLASH_2MBIT:
    BytesPerPage  = 256;
    PagesPerBlock = 256;      // 64-KB "sectors"
    NumBlocks     = 4;        // 256-KB Total
    break;
  case FLASH_4MBIT:
    BytesPerPage  = 256;
    PagesPerBlock = 256;      // 64-KB "sectors"
    NumBlocks     = 8;        // 512-KB Total
    break;
  case FLASH_8MBIT:
    BytesPerPage  = 256;
    PagesPerBlock = 256;      // 64-KB "sectors"
    NumBlocks     = 16;       // 1-MB Total
    break;
  case FLASH_16MBIT:
    BytesPerPage  = 512;
    PagesPerBlock = 256;      // 128-KB "sectors"
    NumBlocks     = 16;       // 2-MB Total
    break;
  case FLASH_32MBIT:
    BytesPerPage   = 512;
    PagesPerBlock  = 128;     // 64-KB "sectors"
    NumBlocks      = 64;      // 4-MB total
    IsLegacyDevice = _IsLegacyDevice(Unit);
    if (IsLegacyDevice == 0) {
      NumBytesStatus = 2;
    }
    break;
  case FLASH_64MBIT:
    IsLegacyDevice = _IsLegacyDevice(Unit);
    if (IsLegacyDevice == 0) {
      BytesPerPage   = 256;
      PagesPerBlock  = 1024;  // 256-KB "sectors"
      NumBlocks      = 32;    // 8-MB total
      NumBytesStatus = 2;
    } else {
      BytesPerPage   = 1024;
      PagesPerBlock  = 256;   // 256-KB "sectors"
      NumBlocks      = 32;    // 8-MB total
    }
    ReadBufferCmd   = BUFFER_READ_FAST;
    break;
  case FLASH_128MBIT:
    BytesPerPage   = 1024;
    PagesPerBlock  = 32;
    NumBlocks      = 512;
    ReadBufferCmd  = BUFFER_READ_FAST;
    break;
  default:
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_PHY_DF: _PHY_InitGetDeviceInfo: Could not identify device (Status: 0x%02x).", Status));
    r = 1;
    break;
  }
  if (r != 0) {
    return 1;                 // Error, could not identify DataFlash device.
  }
  ldBytesPerPage           = _ld(BytesPerPage);
  pInst->ldBytesPerPage    = (U8)ldBytesPerPage;
  pInst->PagesPerBlock     = PagesPerBlock;
  pInst->BytesPerPageData  = BytesPerPage;
  pInst->BytesPerPageSpare = BytesPerPage >> 5;   // Spare area size is always: PageSize in bytes / 32
  pInst->ReadBufferCmd     = ReadBufferCmd;
  pInst->NumBytesStatus    = NumBytesStatus;
  ldPagesPerVPage          = 0;
  ldBytesPerPageMin        = pInst->ldBytesPerPageMin;
  if (ldBytesPerPageMin > ldBytesPerPage) {
    //
    // Calculate the number of physical pages in a virtual page.
    //
    ldPagesPerVPage = ldBytesPerPageMin - ldBytesPerPage;
  }
  pInst->ldPagesPerVPage = (U8)ldPagesPerVPage;
  _SetPageIndexCached(pInst, PAGE_INDEX_INVALID);
  if (_HasDeviceSpareArea(Status) == 0) {
#if FS_SUPPORT_TEST
    //
    // Newer 64 MBit (i.e. Adesto) DataFlash devices support page size configuration.
    // Try to configure the standard page size (i.e. 264 bytes).
    //
    if ((Type == FLASH_64MBIT) && (IsLegacyDevice == 0)) {
      r = _EnableSpareArea(Unit);
    }
    if (r != 0)
#endif // FS_SUPPORT_TEST
    {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_PHY_DF: _PHY_InitGetDeviceInfo: \"Power of 2\" mode (with no spare area) is not supported."));
      return 1;
    }
  }
  if (pDevInfo != NULL) {
    unsigned BPP_Shift;
    unsigned PPB_Shift;

    BPP_Shift = ldBytesPerPage;
    PPB_Shift = _ld(PagesPerBlock);
    //
    // Updated the number of bytes in a page and the number of pages
    // in a block according to the configured minimum page size.
    //
    BPP_Shift += ldPagesPerVPage;
    PPB_Shift -= ldPagesPerVPage;
    pDevInfo->BPP_Shift           = (U8)BPP_Shift;
    pDevInfo->PPB_Shift           = (U8)PPB_Shift;
    pDevInfo->NumBlocks           = NumBlocks;
    pDevInfo->DataBusWidth        = 1;
    pDevInfo->BadBlockMarkingType = FS_NAND_BAD_BLOCK_MARKING_TYPE_FPS;
  }
  //
  // Wait for DataFlash to finish the last operation.
  //
  (void)_WaitUntilReady(Unit);
  return 0;
}

#endif // (FS_NAND_BLOCK_TYPE == 2)

#if (FS_NAND_BLOCK_TYPE == 1)     // 8 or 4 DataFlash blocks are used as storage block.

/*********************************************************************
*
*       _PHY_EraseBlock
*
*  Function description
*    Erases a single block of DataFlash.
*
*  Return value
*    ==0      Data successfully transferred.
*    !=1      An error has occurred.
*/
static int _PHY_EraseBlock(U8 Unit, U32 FirstPageIndex) {
  NAND_DF_INST * pInst;
  unsigned       i;
  unsigned       ldBytesPerPage;
  unsigned       ldPagesPerVPage;
  int            r;

  r = 0;
  pInst = &_aInst[Unit];
  ldBytesPerPage  = pInst->ldBytesPerPage;
  ldPagesPerVPage = pInst->ldPagesPerVPage;
  //
  // Update the page index according to the size of virtual page.
  //
  FirstPageIndex <<= ldPagesPerVPage;
  //
  // Erase 8 pages at a time since there is no real block erase.
  //
  for (i = 0; i < ((unsigned)pInst->PagesPerBlock >> 3); i++) {
    U32 Addr;

    Addr = (FirstPageIndex + (i << 3)) << (ldBytesPerPage + 1u);
    _SendCommandPara(Unit, BLOCK_ERASE, Addr, 1);
    r = _WaitUntilReady(Unit);
    if (r != 0) {
      break;        // Error, erase operation failed.
    }
  }
  _SetPageIndexCached(pInst, PAGE_INDEX_INVALID);
  return r;
}

/*********************************************************************
*
*       _PHY_InitGetDeviceInfo
*
*  Function description
*    Initializes hardware layer, resets NAND flash and tries to identify the NAND flash.
*    If the NAND flash can be handled, Device Info is filled.
*
*  Return value
*    ==0      O.K., device can be handled.
*    !=0      Error: device can not be handled.
*/
static int _PHY_InitGetDeviceInfo(U8 Unit, FS_NAND_DEVICE_INFO * pDevInfo) {
  U8             Status;
  U16            NumBlocks;
  U16            BytesPerPage;
  U16            PagesPerBlock;
  unsigned       ldBytesPerPage;
  unsigned       ldPagesPerVPage;
  unsigned       ldBytesPerPageMin;
  NAND_DF_INST * pInst;
  int            r;
  U8             NumBytesStatus;
  U8             ReadBufferCmd;
  U8             Type;
  int            IsLegacyDevice;

  pInst = &_aInst[Unit];
  ASSERT_HW_TYPE_IS_SET(Unit);
  r = _Init(Unit);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_PHY_DF: _PHY_InitGetDeviceInfo: HW reports error."));
    return 1;
  }
  _ReadStatus(Unit, &Status, 1);      // The DataFlash type is stored on the fist byte of status register.
  //
  // In the following the original PagesPerBlock and NumBlocks of ATMEL data-flashes
  // have been modified in order to reduce maintenance effort.
  // For example on the 32 MBit devices a block contains of 8 pages and the whole device contains 1024 blocks.
  // Since in emFile block-wise management is done and many small blocks generate much maintenance effort
  // (and needs a lot of RAM to hold management data for each block),
  // we decided to merge 4 or 8 real blocks to one, for emFile block-management.
  // The underlying functions such as _PHY_EraseBlock() are designed to deal with this merged blocks,
  // so no customer-specific adaption is necessary.
  //
  if (_HasDeviceSpareArea(Status) == 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_PHY_DF: _PHY_InitGetDeviceInfo: DataFlash in \"Power of 2\" mode (with no spare area) is not supported."));
    return 1;
  }
  r              = 0;             // Set to indicate success.
  NumBytesStatus = 1;             // The status register on legacy DataFlash devices is 1 byte large.
  ReadBufferCmd  = BUFFER_READ;   // Per default use the slow read command.
  BytesPerPage   = 0;
  PagesPerBlock  = 0;
  NumBlocks      = 0;
  Type = _GetDeviceType(Status);
  switch (Type) {
  case FLASH_1MBIT:
    BytesPerPage  = 256;
    PagesPerBlock = 64;           // 8 blocks per group.
    NumBlocks     = 8;
    break;
  case FLASH_2MBIT:
    BytesPerPage  = 256;
    PagesPerBlock = 64;           // 8 blocks per group.
    NumBlocks     = 16;
    break;
  case FLASH_4MBIT:
    BytesPerPage  = 256;
    PagesPerBlock = 64;           // 8 blocks per group.
    NumBlocks     = 32;
    break;
  case FLASH_8MBIT:
    BytesPerPage  = 256;
    PagesPerBlock = 64;           // 8 blocks per group.
    NumBlocks     = 64;
    break;
  case FLASH_16MBIT:
    BytesPerPage  = 512;
    PagesPerBlock = 32;           // 4 blocks per group.
    NumBlocks     = 128;
    break;
  case FLASH_32MBIT:
    BytesPerPage  = 512;
    PagesPerBlock = 32;           // 4 blocks per group.
    NumBlocks     = 256;
    break;
  case FLASH_64MBIT:
    IsLegacyDevice = _IsLegacyDevice(Unit);
    if (IsLegacyDevice == 0) {
      BytesPerPage   = 256;
      PagesPerBlock  = 32;        // 4 blocks per group.
      NumBlocks      = 1024;
      NumBytesStatus = 2;
    } else {
      BytesPerPage  = 1024;
      PagesPerBlock = 32;         // 4 blocks per group.
      NumBlocks     = 256;
    }
    ReadBufferCmd = BUFFER_READ_FAST;
    break;
  case FLASH_128MBIT:
    BytesPerPage  = 1024;
    PagesPerBlock = 32;           // 4 blocks per group.
    NumBlocks     = 512;
    ReadBufferCmd = BUFFER_READ_FAST;
    break;
  default:
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_PHY_DF: _PHY_InitGetDeviceInfo: Could not identify device (Status: 0x%02x).", Status));
    r = 1;
    break;
  }
  if (r != 0) {
    return 1;           // Error, could not identify DataFlash device.
  }
  ldBytesPerPage           = _ld(BytesPerPage);
  pInst->ldBytesPerPage    = (U8)ldBytesPerPage;
  pInst->PagesPerBlock     = PagesPerBlock;
  pInst->BytesPerPageData  = BytesPerPage;
  pInst->BytesPerPageSpare = BytesPerPage >> 5;
  pInst->ReadBufferCmd     = ReadBufferCmd;
  pInst->NumBytesStatus    = NumBytesStatus;
  ldPagesPerVPage          = 0;
  ldBytesPerPageMin        = pInst->ldBytesPerPageMin;
  if (ldBytesPerPageMin > ldBytesPerPage) {
    //
    // Calculate the number of physical pages in a virtual page.
    //
    ldPagesPerVPage = ldBytesPerPageMin - ldBytesPerPage;
  }
  pInst->ldPagesPerVPage = (U8)ldPagesPerVPage;
  _SetPageIndexCached(pInst, PAGE_INDEX_INVALID);
  if (pDevInfo != NULL) {
    unsigned BPP_Shift;
    unsigned PPB_Shift;

    BPP_Shift = ldBytesPerPage;
    PPB_Shift = _ld(PagesPerBlock);
    //
    // Updated the number of bytes in a page and the number of pages
    // in a block according to the configured minimum page size.
    //
    BPP_Shift += ldPagesPerVPage;
    PPB_Shift -= ldPagesPerVPage;
    pDevInfo->BPP_Shift           = (U8)BPP_Shift;
    pDevInfo->PPB_Shift           = (U8)PPB_Shift;
    pDevInfo->NumBlocks           = NumBlocks;
    pDevInfo->DataBusWidth        = 1;
    pDevInfo->BadBlockMarkingType = FS_NAND_BAD_BLOCK_MARKING_TYPE_FPS;
  }
  //
  // Wait for DataFlash to finish the last operation.
  //
  (void)_WaitUntilReady(Unit);
  return 0;
}

#endif // (FS_NAND_BLOCK_TYPE == 1)

#if (FS_NAND_BLOCK_TYPE == 0)     // 1 DataFlash block is used as storage block.

/*********************************************************************
*
*       _PHY_EraseBlock
*
*  Function description
*    Erases a single block of DataFlash.
*
*  Return value
*    ==0      Data successfully transferred.
*    !=1      An error has occurred.
*/
static int _PHY_EraseBlock(U8 Unit, U32 FirstPageIndex) {
  unsigned       ldBytesPerPage;
  unsigned       ldPagesPerVPage;
  NAND_DF_INST * pInst;
  U32            Addr;
  int            r;

  pInst = &_aInst[Unit];
  ldBytesPerPage  = pInst->ldBytesPerPage;
  ldPagesPerVPage = pInst->ldPagesPerVPage;
  //
  // Update the page index according to the size of virtual page.
  //
  FirstPageIndex <<= ldPagesPerVPage;
  Addr = FirstPageIndex << (ldBytesPerPage + 1u);
  _SendCommandPara(Unit, BLOCK_ERASE, Addr, 1);
  r = _WaitUntilReady(Unit);
  _SetPageIndexCached(pInst, PAGE_INDEX_INVALID);
  return r;
}

/*********************************************************************
*
*       _PHY_InitGetDeviceInfo
*
*  Function description
*    Initializes hardware layer, resets NAND flash and tries to identify the NAND flash.
*    If the NAND flash can be handled, Device Info is filled.
*
*  Return value
*    ==0      O.K., device can be handled.
*    !=0      Error: device can not be handled.
*/
static int _PHY_InitGetDeviceInfo(U8 Unit, FS_NAND_DEVICE_INFO * pDevInfo) {
  U8             Status;
  U16            NumBlocks;
  U16            BytesPerPage;
  U16            PagesPerBlock;
  unsigned       ldBytesPerPage;
  unsigned       ldPagesPerVPage;
  unsigned       ldBytesPerPageMin;
  NAND_DF_INST * pInst;
  int            r;
  U8             NumBytesStatus;
  U8             ReadBufferCmd;
  U8             Type;
  int            IsLegacyDevice;

  pInst = &_aInst[Unit];
  ASSERT_HW_TYPE_IS_SET(Unit);
  r = _Init(Unit);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_PHY_DF: _PHY_InitGetDeviceInfo: HW reports error."));
    return 1;
  }
  _ReadStatus(Unit, &Status, 1);      // The DataFlash type is stored on the fist byte of status register.
  if (_HasDeviceSpareArea(Status) == 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_PHY_DF: _PHY_InitGetDeviceInfo: \"Power of 2\" mode (with no spare area) is not supported."));
    return 1;
  }
  r              = 0;             // Set to indicate success.
  NumBytesStatus = 1;             // The status register on legacy DataFlash devices is 1 byte large.
  ReadBufferCmd  = BUFFER_READ;   // Per default use the slow read command.
  BytesPerPage   = 0;
  PagesPerBlock  = 0;
  NumBlocks      = 0;
  Type = _GetDeviceType(Status);
  switch (Type) {
  case FLASH_1MBIT:
    BytesPerPage  = 256;
    PagesPerBlock = 8;
    NumBlocks     = 64;
    break;
  case FLASH_2MBIT:
    BytesPerPage  = 256;
    PagesPerBlock = 8;
    NumBlocks     = 128;
    break;
  case FLASH_4MBIT:
    BytesPerPage  = 256;
    PagesPerBlock = 8;
    NumBlocks     = 256;
    break;
  case FLASH_8MBIT:
    BytesPerPage  = 256;
    PagesPerBlock = 8;
    NumBlocks     = 512;
    break;
  case FLASH_16MBIT:
    BytesPerPage  = 512;
    PagesPerBlock = 8;
    NumBlocks     = 512;
    if (_IsLegacyDevice(Unit) == 0) {
      NumBytesStatus = 2;         // On current DataFlash device the status register is 2 bytes large.
    }
    break;
  case FLASH_32MBIT:
    BytesPerPage  = 512;
    PagesPerBlock = 8;
    NumBlocks     = 1024;
    break;
  case FLASH_64MBIT:
    IsLegacyDevice = _IsLegacyDevice(Unit);
    if (IsLegacyDevice == 0) {
      BytesPerPage   = 256;
      PagesPerBlock  = 8;
      NumBlocks      = 4096;
      NumBytesStatus = 2;
    } else {
      BytesPerPage  = 1024;
      PagesPerBlock = 8;
      NumBlocks     = 1024;
    }
    pInst->ReadBufferCmd = BUFFER_READ_FAST;
    break;
  case FLASH_128MBIT:
    BytesPerPage  = 1024;
    PagesPerBlock = 8;
    NumBlocks     = 2048;
    pInst->ReadBufferCmd = BUFFER_READ_FAST;
    break;
  default:
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NAND_PHY_DF: _PHY_InitGetDeviceInfo: Could not identify device (Status: 0x%02x).", Status));
    r = 1;
    break;
  }
  if (r != 0) {
    return 1;             // Error, could not identify DataFlash device.
  }
  ldBytesPerPage           = _ld(BytesPerPage);
  pInst->ldBytesPerPage    = (U8)ldBytesPerPage;
  pInst->PagesPerBlock     = PagesPerBlock;
  pInst->BytesPerPageData  = BytesPerPage;
  pInst->BytesPerPageSpare = BytesPerPage >> 5;
  pInst->ReadBufferCmd     = ReadBufferCmd;
  pInst->NumBytesStatus    = NumBytesStatus;
  ldPagesPerVPage          = 0;
  ldBytesPerPageMin        = pInst->ldBytesPerPageMin;
  if (ldBytesPerPageMin > ldBytesPerPage) {
    //
    // Calculate the number of physical pages in a virtual page.
    //
    ldPagesPerVPage = ldBytesPerPageMin - ldBytesPerPage;
  }
  pInst->ldPagesPerVPage = (U8)ldPagesPerVPage;
  _SetPageIndexCached(pInst, PAGE_INDEX_INVALID);
  if (pDevInfo != NULL) {
    unsigned BPP_Shift;
    unsigned PPB_Shift;

    BPP_Shift = ldBytesPerPage;
    PPB_Shift = _ld(PagesPerBlock);
    //
    // Updated the number of bytes in a page and the number of pages
    // in a block according to the configured minimum page size.
    //
    BPP_Shift += ldPagesPerVPage;
    PPB_Shift -= ldPagesPerVPage;
    pDevInfo->BPP_Shift           = (U8)BPP_Shift;
    pDevInfo->PPB_Shift           = (U8)PPB_Shift;
    pDevInfo->NumBlocks           = NumBlocks;
    pDevInfo->DataBusWidth        = 1;
    pDevInfo->BadBlockMarkingType = FS_NAND_BAD_BLOCK_MARKING_TYPE_FPS;
  }
  //
  // Wait for DataFlash to finish the last operation.
  //
  (void)_WaitUntilReady(Unit);
  return 0;
}

#endif // (FS_NAND_BLOCK_TYPE == 0)

/*********************************************************************
*
*       _PHY_IsWP
*
*  Function description
*    Checks if the device is write protected.
*    This is done by reading bit 7 of the status register.
*    Typical reason for write protection is that either the supply voltage is too low
*    or the /WP-pin is active (low)
*
*  Return value
*    < 0    An error occurred
*    ==0    Not write protected
*    > 0    Write protected
*/
static int _PHY_IsWP(U8 Unit) {
  FS_USE_PARA(Unit);
  return 0;
}

/*********************************************************************
*
*       Public const data
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_NAND_PHY_DataFlash
*/
const FS_NAND_PHY_TYPE FS_NAND_PHY_DataFlash = {
  _PHY_EraseBlock,
  _PHY_InitGetDeviceInfo,
  _PHY_IsWP,
  _PHY_Read,
  _PHY_ReadEx,
  _PHY_Write,
  _PHY_WriteEx,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL
};

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_NAND_DF_EraseChip
*
*  Function description
*    Erases the entire device.
*
*  Parameters
*    Unit       Index of the physical layer (0-based)
*
*  Additional information
*    This function is optional. It sets all the bits of the DataFlash
*    memory to 1. All the data stored on the DataFlash memory is lost.
*/
void FS_NAND_DF_EraseChip(U8 Unit) {
  FS_NAND_DEVICE_INFO  DevInfo;
  U32                  i;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  if (_PHY_InitGetDeviceInfo(Unit, &DevInfo) == 0) {
    for (i = 0; i < DevInfo.NumBlocks; i++) {
      U32 PageIndex;

      PageIndex = i << DevInfo.PPB_Shift;
      (void)_PHY_EraseBlock(Unit, PageIndex);
    }
  }
}

/*********************************************************************
*
*       FS_NAND_DF_SetMinPageSize
*
*  Function description
*    Configures the required minimum page size.
*
*  Parameters
*    Unit       Index of the physical layer (0-based)
*    NumBytes   Page size in bytes.
*
*  Additional information
*    This function is optional. The application can use it to request
*    a minimum page size to work with. If the size of the physical page
*    is smaller than the specified value then then adjacent physical
*    pages are grouped together into one virtual page that is presented
*    as a single page to the SLC1 NAND driver. This is required when
*    the size of a physical page is smaller than 512 bytes which is
*    the minimum sector size the SLC1 NAND driver can work with.
*    NumBytes has to be a power of 2 value.
*/
void FS_NAND_DF_SetMinPageSize(U8 Unit, U32 NumBytes) {
  NAND_DF_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  if (Unit < (U8)FS_NAND_NUM_UNITS) {
    pInst = &_aInst[Unit];
    pInst->ldBytesPerPageMin = (U8)_ld(NumBytes);
  }
}

/*********************************************************************
*
*       FS_NAND_DF_SetHWType
*
*  Function description
*    Configures the hardware access routines.
*
*  Parameters
*    Unit       Index of the physical layer (0-based)
*    pHWType    Table of hardware routines.
*
*  Additional information
*    This function is mandatory and it has to be called once for
*    each used instance of the physical layer.
*/
void FS_NAND_DF_SetHWType(U8 Unit, const FS_NAND_HW_TYPE_DF * pHWType) {
  NAND_DF_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  if (Unit < (U8)FS_NAND_NUM_UNITS) {
    pInst = &_aInst[Unit];
    pInst->pHWType = pHWType;
  }
}

/*************************** End of file ****************************/
