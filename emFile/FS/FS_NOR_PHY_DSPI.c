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
File        : FS_NOR_PHY_DSPI.c
Purpose     : Universal physical layer for NOR flash devices that uses
              direct SPI access.
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
*       ASSERT_UNIT_NO_IS_IN_RANGE
*/
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL)
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)                                         \
    if ((Unit) >= (U8)FS_NOR_NUM_UNITS) {                                          \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "NOR_PHY_DSPI: Invalid unit number.")); \
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);                                         \
    }
#else
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)
#endif

/*********************************************************************
*
*       Local types
*
**********************************************************************
*/

/*********************************************************************
*
*       PHY_DSPI_INFO
*/
typedef struct {
  const FS_NOR_PHY_TYPE  * pPhyType;
  void                  (* pfSetHWType)(U8 Unit, const FS_NOR_HW_TYPE_SPI * pHWType);
} PHY_DSPI_INFO;

/*********************************************************************
*
*       Static const data
*
**********************************************************************
*/

/*********************************************************************
*
*       _apPhyType
*
*  This is the list of physical layers which are checked. The first one in the list which works is used.
*/
static const PHY_DSPI_INFO _aPhyList[] = {
  {&FS_NOR_PHY_ST_M25, FS_NOR_SPI_SetHWType},
  {&FS_NOR_PHY_SFDP,   FS_NOR_SFDP_SetHWType},
  {(const FS_NOR_PHY_TYPE *)NULL, NULL}           // end-of-list
};

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static const FS_NOR_PHY_TYPE * _apPhyType[FS_NOR_NUM_UNITS];

/*********************************************************************
*
*       Static code (public via callback)
*
**********************************************************************
*/

/*********************************************************************
*
*       _PHY_WriteOff
*/
static int _PHY_WriteOff(U8 Unit, U32 Off, const void * pData, U32 NumBytes) {
  int                     r;
  const FS_NOR_PHY_TYPE * pPhyType;

  r = 1;        // Set to indicate an error.
  pPhyType = _apPhyType[Unit];
  if (pPhyType != NULL) {
    r = pPhyType->pfWriteOff(Unit, Off, pData, NumBytes);
  }
  return r;
}

/*********************************************************************
*
*       _PHY_ReadOff
*/
static int _PHY_ReadOff(U8 Unit, void * pData, U32 Off, U32 NumBytes) {
  int                     r;
  const FS_NOR_PHY_TYPE * pPhyType;

  r = 1;        // Set to indicate an error.
  pPhyType = _apPhyType[Unit];
  if (pPhyType != NULL) {
    r = pPhyType->pfReadOff(Unit, pData, Off, NumBytes);
  }
  return r;
}

/*********************************************************************
*
*       _PHY_EraseSector
*/
static int _PHY_EraseSector(U8 Unit, unsigned SectorIndex) {
  int                     r;
  const FS_NOR_PHY_TYPE * pPhyType;

  r = 1;        // Set to indicate an error.
  pPhyType = _apPhyType[Unit];
  if (pPhyType != NULL) {
    r = pPhyType->pfEraseSector(Unit, SectorIndex);
  }
  return r;
}

/*********************************************************************
*
*       _PHY_GetSectorInfo
*/
static void _PHY_GetSectorInfo(U8 Unit, unsigned SectorIndex, U32 * pOff, U32 * pNumBytes) {
  const FS_NOR_PHY_TYPE * pPhyType;

  pPhyType = _apPhyType[Unit];
  if (pPhyType != NULL) {
    pPhyType->pfGetSectorInfo(Unit, SectorIndex, pOff, pNumBytes);
  }
}

/*********************************************************************
*
*       _PHY_GetNumSectors
*/
static int _PHY_GetNumSectors(U8 Unit) {
  int                     r;
  const FS_NOR_PHY_TYPE * pPhyType;
  const PHY_DSPI_INFO   * pPhyInfo;
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS)
  U32                     ErrorFilter;
#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS

  pPhyType = _apPhyType[Unit];
  if (pPhyType != NULL) {
    r = pPhyType->pfGetNumSectors(Unit);
  } else {
    //
    // This is the first function that tries to access the NOR flash
    // after the file system initialization. The identification of the
    // NOR flash device is done here since the NOR physical layers
    // do not have a specific initialization function.
    //
    pPhyInfo = _aPhyList;
    for (;;) {
      pPhyType = pPhyInfo->pPhyType;
      if (pPhyType == NULL) {
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "NOR_PHY_DSPI: Could not identify NOR flash."));
        r = 0;          // Error, end of the list reached.
        break;
      }
      //
      // We disable temporarily the error messages during the identification
      // to avoid confusing the user.
      //
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS)
      ErrorFilter = FS__GetErrorFilterNL();
      FS__SetErrorFilterNL(ErrorFilter & ~FS_MTYPE_DRIVER);
#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS
      r = pPhyType->pfGetNumSectors(Unit);
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS)
      FS__SetErrorFilterNL(ErrorFilter);
#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS
      if (r != 0) {
        _apPhyType[Unit] = pPhyType;
        break;          // OK, NOR flash identified.
      }
      ++pPhyInfo;
    }
  }
  return r;
}

/*********************************************************************
*
*       _PHY_Configure
*/
static void _PHY_Configure(U8 Unit, U32 BaseAddr, U32 StartAddr, U32 NumBytes) {
  const FS_NOR_PHY_TYPE * pPhyType;
  const PHY_DSPI_INFO   * pPhyInfo;

  //
  // pfConfigure() is called at the initialization of the file system
  // when the NOR physical layer is assigned to NOR driver. At this point
  // the actual NOR physical layer has not been identified yet therefor
  // he have to call pfConfigure() of each NOR physical sector in the list.
  // This is OK since pfConfigure() does not access the NOR flash device.
  //
  pPhyInfo = _aPhyList;
  for (;;) {
    pPhyType = pPhyInfo->pPhyType;
    if (pPhyType == NULL) {
      break;          // OK, end of the list reached.
    }
    pPhyType->pfConfigure(Unit, BaseAddr, StartAddr, NumBytes);
    ++pPhyInfo;
  }
}

/*********************************************************************
*
*       _PHY_OnSelectPhy
*/
static void _PHY_OnSelectPhy(U8 Unit) {
  const FS_NOR_PHY_TYPE * pPhyType;
  const PHY_DSPI_INFO   * pPhyInfo;

  //
  // pfOnSelectPhy() is called at the initialization of the file system
  // when the NOR physical layer is assigned to NOR driver. At this point
  // the actual NOR physical layer has not been identified yet therefor
  // he have to call pfOnSelectPhy() of each NOR physical sector in the list.
  // This is OK since pfOnSelectPhy() does not access the NOR flash device.
  //
  pPhyInfo = _aPhyList;
  for (;;) {
    pPhyType = pPhyInfo->pPhyType;
    if (pPhyType == NULL) {
      break;          // OK, end of the list reached.
    }
    pPhyType->pfOnSelectPhy(Unit);
    ++pPhyInfo;
  }
}

/*********************************************************************
*
*       _PHY_DeInit
*/
static void _PHY_DeInit(U8 Unit) {
  const FS_NOR_PHY_TYPE * pPhyType;

  pPhyType = _apPhyType[Unit];
  if (pPhyType != NULL) {
    pPhyType->pfDeInit(Unit);
  }
}

/*********************************************************************
*
*       Public const data
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_NOR_PHY_DSPI
*/
const FS_NOR_PHY_TYPE FS_NOR_PHY_DSPI = {
  _PHY_WriteOff,
  _PHY_ReadOff,
  _PHY_EraseSector,
  _PHY_GetSectorInfo,
  _PHY_GetNumSectors,
  _PHY_Configure,
  _PHY_OnSelectPhy,
  _PHY_DeInit,
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
*       FS_NOR_DSPI_SetHWType
*
*  Function description
*    Configures the HW access routines.
*
*  Parameters
*    Unit           Index of the physical layer (0-based).
*    pHWType        Hardware access routines. Cannot be NULL.
*
*  Additional information
*    This function is mandatory and it has to be called once for each
*    instance of the physical layer.
*/
void FS_NOR_DSPI_SetHWType(U8 Unit, const FS_NOR_HW_TYPE_SPI * pHWType) {
  const FS_NOR_PHY_TYPE * pPhyType;
  const PHY_DSPI_INFO   * pPhyInfo;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  if (Unit < (U8)FS_NOR_NUM_UNITS) {
    //
    // Configure the same HW layer for all the physical layers in the list.
    //
    pPhyInfo = _aPhyList;
    for (;;) {
      pPhyType = pPhyInfo->pPhyType;
      if (pPhyType == NULL) {
        break;          // OK, end of the list reached.
      }
      pPhyInfo->pfSetHWType(Unit, pHWType);
      ++pPhyInfo;
    }
  }
}

/*************************** End of file ****************************/
