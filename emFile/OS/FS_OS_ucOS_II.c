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
-------------------------- END-OF-HEADER -----------------------------

File    : FS_OS_ucOS_ii.c
Purpose : uCOS OS Layer for the file system.
*/

/*
*********************************************************************************************************
*                                             Micrium, Inc.
*                                         949 Crestview Circle
*                                        Weston,  FL 33327-1848
*
*                                         OS Layer for uC/FS
*
*                                   (c) Copyright 2003, Micrium, Inc.
*                                          All rights reserved.
*
* Filename    : FS_OS_ucOS_ii.c
* Programmers : Jean J. Labrosse
*********************************************************************************************************
*/

/*
*********************************************************************************************************
*                                            INCLUDE FILES
*********************************************************************************************************
*/

#include  "FS_Int.h"
#include  "FS_OS.h"
#include  "ucos_ii.h"

/*
*********************************************************************************************************
*                                         MACROS, DEFAULTS
*********************************************************************************************************
*/

/*
*********************************************************************************************************
*                                       LOCAL GLOBAL VARIABLES
*********************************************************************************************************
*/

static  OS_EVENT  **FS_SemPtrs;
static  char        NumLocks;
/*
*********************************************************************************************************
*                                         Initialize OS Resources
*********************************************************************************************************
*/

void  FS_X_OS_Init (unsigned nlocks)
{
    unsigned    i;
    OS_EVENT  **p_sem;


    FS_SemPtrs = (OS_EVENT  **)FS_AllocZeroed(nlocks * sizeof(OS_EVENT *));
    p_sem      =  FS_SemPtrs;

    for (i = 0; i < nlocks; i++) {
       *p_sem   = OSSemCreate(1);
        p_sem  += 1;
    }

    NumLocks = nlocks;
}

/*
*********************************************************************************************************
*                                         Deinitialize OS Resources
*********************************************************************************************************
*/

void  FS_X_OS_DeInit (void)
{
    unsigned    i;
    OS_EVENT  **p_sem;
    INT8U       err;


    p_sem       =  FS_SemPtrs;

    for (i = 0; i < NumLocks; i++) {
       OSSemDel(*p_sem, OS_DEL_ALWAYS, &err);
       p_sem  += 1;
    }
}


/*
*********************************************************************************************************
*                                         Unlock a file system operation
*********************************************************************************************************
*/

void  FS_X_OS_Unlock (unsigned index)
{
    OS_EVENT  *p_sem;


    if (FS_SemPtrs == 0) {
        return;
    }

    p_sem = *(FS_SemPtrs + index);
    if (p_sem) {
        OSSemPost(p_sem);
    }
}

/*
*********************************************************************************************************
*                                         Unlock a file system operation
*********************************************************************************************************
*/

void  FS_X_OS_Lock (unsigned index)
{
    INT8U       err;
    OS_EVENT  *p_sem;


    if (FS_SemPtrs == 0) {
        return;
    }

    p_sem = *(FS_SemPtrs + index);
    if (p_sem) {
        OSSemPend(p_sem, 0, &err);
    }
}


/*************************** End of file ****************************/
