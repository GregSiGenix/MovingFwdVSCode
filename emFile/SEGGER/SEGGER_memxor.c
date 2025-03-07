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
File    : SEGGER_memxor.c
Purpose : Exclusive-or blocks, quickly.
Revision: $Rev: 9275 $
--------  END-OF-HEADER  ---------------------------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/

#include "SEGGER.h"

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       SEGGER_memxor()
*
*  Function description
*    Exclusive-or a block of memory from Src to Dest, i.e.
*    pDest ^= pSrc.
*
*  Parameters
*    pDest[ByteCnt] - Original data and result
*    pSrc[ByteCnt]  - Array to combine with pDest using xor.
*/
void SEGGER_memxor(void *pDest, const void *pSrc, unsigned ByteCnt) {
  U8       *pD;
  const U8 *pS;
  //
  // Manual loop unrolling.
  //
  pD = (U8 *)pDest;
  pS = (const U8 *)pSrc;
  while (ByteCnt >= 16) {
    *pD++ ^= *pS++; *pD++ ^= *pS++; *pD++ ^= *pS++; *pD++ ^= *pS++;
    *pD++ ^= *pS++; *pD++ ^= *pS++; *pD++ ^= *pS++; *pD++ ^= *pS++;
    *pD++ ^= *pS++; *pD++ ^= *pS++; *pD++ ^= *pS++; *pD++ ^= *pS++;
    *pD++ ^= *pS++; *pD++ ^= *pS++; *pD++ ^= *pS++; *pD++ ^= *pS++;
    ByteCnt -= 16;
  }
  //
  // Last block, if any, handled by looping.
  //
  while (ByteCnt > 0) {
    *pD++ ^= *pS++;
    --ByteCnt;
  }
}

/****** End Of File *************************************************/
