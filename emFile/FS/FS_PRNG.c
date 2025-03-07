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
File        : FS_PRNG.c
Purpose     : Pseudo-random number generator
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
*       Static data
*
**********************************************************************
*/
static U16 _Value;

/*********************************************************************
*
*       Public code (internal)
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_PRNG_Init
*
*  Function description
*    Sets the initial value of generator.
*/
void FS_PRNG_Init(U16 Value) {
  _Value = Value;
}

/*********************************************************************
*
*       FS_PRNG_Generate
*
*  Function description
*    Generates a pseudo-random by computing the 16-bit CRC of the previously generated value.
*/
U16 FS_PRNG_Generate(void) {
  U16 Value;

  Value  = _Value;
  if (Value == 0u) {      // A value of 0 will generate constant output.
    ++Value;
  }
  Value  = FS_CRC16_Calc((U8 *)&Value, sizeof(Value), 0);
  _Value = Value;
  return Value;
}

/*********************************************************************
*
*       FS_PRNG_Save
*/
void FS_PRNG_Save(FS_CONTEXT * pContext) {
  pContext->PRNG_Value = _Value;
}

/*********************************************************************
*
*       FS_PRNG_Restore
*/
void FS_PRNG_Restore(const FS_CONTEXT * pContext) {
  _Value = pContext->PRNG_Value;
}

/*************************** End of file ****************************/
