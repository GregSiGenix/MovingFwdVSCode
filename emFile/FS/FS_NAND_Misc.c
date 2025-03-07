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
File        : FS_NAND_Misc.c
Purpose     : Misc. functions related to NAND flash.
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#include "FS.h"
#include "FS_Int.h"
#include "FS_NAND_Int.h"

/*********************************************************************
*
*       Defines, fixed
*
**********************************************************************
*/

/*********************************************************************
*
*       NAND flash commands
*/
#define CMD_READ_1          0x00
#define CMD_READ_ID         0x90
#define CMD_READ_STATUS     0x70
#define CMD_READ_PARA_PAGE  0xEC
#define CMD_RESET           0xFF

/*********************************************************************
*
*       ONFI parameters
*/
#define PARA_PAGE_SIZE      256u
#define PARA_CRC_POLY       0x8005u
#define PARA_CRC_INIT       0x4F4E
#define NUM_PARA_PAGES      3

#define STATUS_READY        (1u << 6)

/*********************************************************************
*
*       ASSERT_UNIT_NO_IS_IN_RANGE
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)                                     \
    if ((Unit) >= (U8)FS_NAND_NUM_UNITS) {                                     \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NAND_PHY: Invalid unit number.")); \
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);                                     \
    }
#else
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)
#endif

/*********************************************************************
*
*       ASSERT_HW_TYPE_IS_SET
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_HW_TYPE_IS_SET(Unit)                                       \
    if (_aInst[Unit].pHWType == NULL) {                                     \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NAND_PHY: HW layer not set.")); \
      FS_X_PANIC(FS_ERRCODE_HW_LAYER_NOT_SET);                              \
    }
#else
  #define ASSERT_HW_TYPE_IS_SET(Unit)
#endif

/*********************************************************************
*
*       Local types
*
**********************************************************************
*/

/*********************************************************************
*
*       NAND_PHY_INST
*/
typedef struct {
  const FS_NAND_HW_TYPE * pHWType;
} NAND_PHY_INST;

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static NAND_PHY_INST _aInst[FS_NAND_NUM_UNITS];

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _Init_x8
*/
static void _Init_x8(U8 Unit) {
  NAND_PHY_INST * pInst;

  pInst = &_aInst[Unit];
  pInst->pHWType->pfInit_x8(Unit);
}

/*********************************************************************
*
*       _DisableCE
*/
static void _DisableCE(U8 Unit) {
  NAND_PHY_INST * pInst;

  pInst = &_aInst[Unit];
  pInst->pHWType->pfDisableCE(Unit);
}

/*********************************************************************
*
*       _EnableCE
*/
static void _EnableCE(U8 Unit) {
  NAND_PHY_INST * pInst;

  pInst = &_aInst[Unit];
  pInst->pHWType->pfEnableCE(Unit);
}

/*********************************************************************
*
*       _SetAddrMode
*/
static void _SetAddrMode(U8 Unit) {
  NAND_PHY_INST * pInst;

  pInst = &_aInst[Unit];
  pInst->pHWType->pfSetAddrMode(Unit);
}

/*********************************************************************
*
*       _SetCmdMode
*/
static void _SetCmdMode(U8 Unit) {
  NAND_PHY_INST * pInst;

  pInst = &_aInst[Unit];
  pInst->pHWType->pfSetCmdMode(Unit);
}

/*********************************************************************
*
*       _SetDataMode
*/
static void _SetDataMode(U8 Unit) {
  NAND_PHY_INST * pInst;

  pInst = &_aInst[Unit];
  pInst->pHWType->pfSetDataMode(Unit);
}

/*********************************************************************
*
*       _Read_x8
*/
static void _Read_x8(U8 Unit, void * pBuffer, unsigned NumBytes) {
  NAND_PHY_INST * pInst;

  pInst = &_aInst[Unit];
  pInst->pHWType->pfRead_x8(Unit, pBuffer, NumBytes);
}

/*********************************************************************
*
*       _Write_x8
*/
static void _Write_x8(U8 Unit, const void * pBuffer, unsigned NumBytes) {
  NAND_PHY_INST * pInst;

  pInst = &_aInst[Unit];
  pInst->pHWType->pfWrite_x8(Unit, pBuffer, NumBytes);
}

/*********************************************************************
*
*       _WriteCmd
*
*  Function description
*    Sends a command to NAND flash.
*/
static void _WriteCmd(U8 Unit, U8 Cmd) {
  _SetCmdMode(Unit);
  _Write_x8(Unit, &Cmd, 1);
}

/*********************************************************************
*
*       _WriteAddrByte
*
*   Function description
*     Sends an address byte to NAND flash.
*/
static void _WriteAddrByte(U8 Unit, U8 Addr) {
  _SetAddrMode(Unit);
  _Write_x8(Unit, &Addr, 1);
}

/*********************************************************************
*
*       _ReadData8
*
*  Function description
*    Reads data bytes from NAND flash.
*/
static void _ReadData8(U8 Unit, void * pData, unsigned NumBytes) {
  _SetDataMode(Unit);
  _Read_x8(Unit, pData, NumBytes);
}

/*********************************************************************
*
*       _ReadStatus
*
*  Function description
*    Executes the READ STATUS command.
*/
static U8 _ReadStatus(U8 Unit) {
  U8 Status;

  _WriteCmd(Unit, CMD_READ_STATUS);
  _ReadData8(Unit, &Status, 1);
  return Status;
}

/*********************************************************************
*
*       _Reset
*
*  Function description
*    Executes the RESET command.
*/
static void _Reset(U8 Unit) {
  _WriteCmd(Unit, CMD_RESET);
}

/*********************************************************************
*
*       _ReadId
*
*  Function description
*    Executes the READ ID command.
*/
static void _ReadId(U8 Unit, U8 * pId, U32 NumBytes) {
  _WriteCmd(Unit, CMD_READ_ID);
  _WriteAddrByte(Unit, 0);    // Identification information is stored at address 0.
  _ReadData8(Unit, pId, NumBytes);
}

/*********************************************************************
*
*       _WaitForReady
*
*  Function description
*    Wait for NAND device to become ready.
*/
static void _WaitForReady(U8 Unit) {
  U8 Status;

  do {
    Status = _ReadStatus(Unit);
  } while ((Status & STATUS_READY) == 0u);
}

/*********************************************************************
*
*       _IsONFISupported
*
*  Function description
*    Checks if the device supports ONFI.
*    An ONFI compatible device returns "ONFI" ASCII string
*    when executing a READ ID operation from address 0x20
*
*  Return value
*    ==0    ONFI not supported.
*    !=0    ONFI supported.
*/
static int _IsONFISupported(U8 Unit) {
  int r;
  U8  aId[4];

  r = 0;    // assuming ONFI is not supported
  _WriteCmd(Unit, CMD_READ_ID);
  _WriteAddrByte(Unit, 0x20);
  _ReadData8(Unit, &aId[0], sizeof(aId));
  if ((aId[0] == (U8)'O') &&
      (aId[1] == (U8)'N') &&
      (aId[2] == (U8)'F') &&
      (aId[3] == (U8)'I')) {
    r = 1;
  }
  return r;
}

/*********************************************************************
*
*       _ReadONFIPara
*
*  Function description
*    Reads the ONFI parameter page.
*    A page has 256 bytes. The integrity of information is checked using CRC.
*
*  Parameters
*    Unit     Device unit number.
*    pPara    [OUT] Information read from the parameter page. Must be at least 256 bytes long.
*
*  Return value
*    ==0    ONFI parameters read.
*    !=0    An error occurred.
*/
static int _ReadONFIPara(U8 Unit, void * pPara) {
  int   r;
  U16   crcRead;
  U16   crcCalc;
  U8  * p;
  U32   NumBytesPara;
  int   i;

  r = 1;        // No parameter page found, yet.
  _WriteCmd(Unit, CMD_READ_PARA_PAGE);
  _WriteAddrByte(Unit, 0);
  _WaitForReady(Unit);
  _WriteCmd(Unit, CMD_READ_1);    // Switch back to read mode. _WaitForReady() function changed it to status mode.
  p = SEGGER_PTR2PTR(U8, pPara);
  //
  // Several identical parameter pages are stored in a device.
  // Read from the first one which stores valid information.
  //
  for (i = 0; i < NUM_PARA_PAGES; ++i) {
    _ReadData8(Unit, p, PARA_PAGE_SIZE);
    NumBytesPara = PARA_PAGE_SIZE - sizeof(crcRead);    // CRC is stored on the last 2 bytes of the parameter page.
    //
    // Validate the parameters by checking the CRC.
    //
    crcRead = FS_LoadU16LE(&p[NumBytesPara]);
    crcCalc = FS_CRC16_CalcBitByBit(p, NumBytesPara, PARA_CRC_INIT, PARA_CRC_POLY);
    if (crcRead != crcCalc) {
      continue;
    }
    //
    // CRC is valid, now check the signature.
    //
    if ((p[0] == (U8)'O') &&
        (p[1] == (U8)'N') &&
        (p[2] == (U8)'F') &&
        (p[3] == (U8)'I')) {
      r = 0;
      break;                      // Found a valid parameter page.
    }
  }
  return r;
}

/*********************************************************************
*
*       Public code (internal)
*
**********************************************************************
*/

/*********************************************************************
*
*       FS__NAND_IsONFISupported
*
*  Function description
*    Checks whether the NAND flash supports ONFI.
*
*  Parameters
*    Unit     Index of HW layer connected to NAND flash.
*    pHWType  HW layer to be used.
*
*  Return value
*    ==0    ONFI not supported.
*    !=0    NAND flash supports ONFI.
*/
int FS__NAND_IsONFISupported(U8 Unit, const FS_NAND_HW_TYPE * pHWType) {
  int                     r;
  const FS_NAND_HW_TYPE * pHWTypeOld;

  pHWTypeOld = NULL;
  if (pHWType != NULL) {
    pHWTypeOld = _aInst[Unit].pHWType;
    _aInst[Unit].pHWType = pHWType;
  }
  r = _IsONFISupported(Unit);
  if (pHWType != NULL) {
    _aInst[Unit].pHWType = pHWTypeOld;
  }
  return r;
}

/*********************************************************************
*
*       FS__NAND_ReadONFIPara
*
*  Function description
*    Reads the ONFI parameter page from a NAND flash.
*
*  Parameters
*    Unit       Index of HW layer connected to NAND flash.
*    pHWType    Type of the hardware layer to be used for the data transfer.
*    pPara      [OUT] Data of ONFI parameter page read from NAND flash.
*               The size of the buffer must be at least 256 bytes.
*
*  Return value
*    ==0    ONFI parameters read.
*    !=0    ONFI is not supported by the NAND flash.
*/
int FS__NAND_ReadONFIPara(U8 Unit, const FS_NAND_HW_TYPE * pHWType, void * pPara) {
  int                     r;
  const FS_NAND_HW_TYPE * pHWTypeOld;

  pHWTypeOld = NULL;
  if (pHWType != NULL) {
    pHWTypeOld = _aInst[Unit].pHWType;
    _aInst[Unit].pHWType = pHWType;
  }
  r = _ReadONFIPara(Unit, pPara);
  if (pHWType != NULL) {
    _aInst[Unit].pHWType = pHWTypeOld;
  }
  return r;
}

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_NAND_PHY_ReadDeviceId
*
*  Function description
*    Returns the id information stored in a NAND flash device.
*
*  Parameters
*    Unit       Index of HW layer connected to NAND flash.
*    pId        [OUT] Identification data read from NAND flash.
*    NumBytes   Number of bytes to read.
*
*  Return value
*    ==0    OK, id information read.
*    !=0    An error occurred.
*
*  Additional information
*    FS_NAND_PHY_ReadDeviceId() executes the READ ID command to read
*    the id information from the NAND flash device. NumBytes specifies
*    the number of bytes to be read. Refer to the data sheet of the
*    NAND flash device for additional information about the meaning of
*    the data returned by the NAND flash device. Typically, the first
*    byte stores the manufactured id while the second byte provides
*    information about the organization of the NAND flash device.
*
*    It is permitted to call FS_NAND_PHY_ReadONFIPara()from FS_X_AddDevices()
*    since it does not require for the file system to be fully initialized and
*    it invokes only functions of the NAND hardware layer. No instance of NAND
*    driver is required to invoke this function.
*
*    Typical usage is to determine at runtime the type of NAND driver to be
*    used for the connected NAND flash device.
*/
int FS_NAND_PHY_ReadDeviceId(U8 Unit, U8 * pId, U32 NumBytes) {
  int r;

  r = 1;          // Set to indicate an error.
  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  if (Unit < (U8)FS_NAND_NUM_UNITS) {
    ASSERT_HW_TYPE_IS_SET(Unit);
    //
    // Initialize the HW layer.
    //
    _Init_x8(Unit);
    //
    // Enable device.
    //
    _EnableCE(Unit);
    //
    // NAND device must be reset before we can communicate with it.
    //
    _Reset(Unit);
    _WaitForReady(Unit);
    _ReadId(Unit, pId, NumBytes);
    //
    // Disable device.
    //
    _DisableCE(Unit);
    r = 0;
  }
  return r;
}

/*********************************************************************
*
*       FS_NAND_PHY_ReadONFIPara
*
*  Function description
*    Reads the ONFI parameters from a NAND flash.
*
*  Parameters
*    Unit       Index of HW layer connected to NAND flash.
*    pPara      [OUT] Data of ONFI parameter page read from NAND flash.
*               This parameter can be set to NULL.
*
*  Return value
*    ==0    ONFI parameters read.
*    !=0    ONFI is not supported by the NAND flash.
*
*  Additional information
*    Refer to the data sheet of the NAND flash device for a description
*    of the data layout of the returned ONFI parameters.
*
*    This function can be used to read the ONFI parameter stored in a NAND flash.
*    It is permitted to call FS_NAND_PHY_ReadONFIPara()from FS_X_AddDevices()
*    since it does not require for the file system to be fully initialized and
*    it invokes only functions of the NAND hardware layer. No instance of NAND
*    driver is required to invoke this function.
*
*    FS_NAND_PHY_ReadONFIPara() can also be used to check if the NAND flash device
*    is ONFI compliant by setting pPara to NULL.
*
*    The size of the buffer passed via pParam must be at least 256 bytes large.
*/
int FS_NAND_PHY_ReadONFIPara(U8 Unit, void * pPara) {
  int r;

  r = 1;        // Set to indicate an error.
  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  if (Unit < (U8)FS_NAND_NUM_UNITS) {
    ASSERT_HW_TYPE_IS_SET(Unit);
    //
    // Initialize the HW layer.
    //
    _Init_x8(Unit);
    //
    // Enable device.
    //
    _EnableCE(Unit);
    //
    // NAND device must be reset before we can communicate with it.
    //
    _Reset(Unit);
    _WaitForReady(Unit);
    if (FS__NAND_IsONFISupported(Unit, NULL) != 0) {
      r = 0;
      if (pPara != NULL) {
        //
        // Call the private function to do the reading.
        //
        r = FS__NAND_ReadONFIPara(Unit, NULL, pPara);
      }
    }
    //
    // Disable device.
    //
    _DisableCE(Unit);
  }
  return r;
}

/*********************************************************************
*
*       FS_NAND_PHY_SetHWType
*
*  Function description
*    Configures the hardware access routines for FS_NAND_PHY_ReadDeviceId()
*    and FS_NAND_PHY_ReadONFIPara().
*
*  Parameters
*    Unit       Index of the physical layer instance (0-based)
*    pHWType    Type of the hardware layer to use. Cannot be NULL.
*
*  Additional information
*    This function is mandatory if the application calls either FS_NAND_PHY_ReadDeviceId()
*    or FS_NAND_PHY_ReadONFIPara(). FS_NAND_PHY_SetHWType() has to be called once
*    in FS_X_AddDevices() for every different Unit number passed to FS_NAND_PHY_ReadDeviceId()
*    or FS_NAND_PHY_ReadONFIPara().
*/
void FS_NAND_PHY_SetHWType(U8 Unit, const FS_NAND_HW_TYPE * pHWType) {
  NAND_PHY_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  if (Unit < (U8)FS_NAND_NUM_UNITS) {
    pInst = &_aInst[Unit];
    pInst->pHWType = pHWType;
  }
}

/*************************** End of file ****************************/
