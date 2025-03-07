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
File        : FS_Format.c
Purpose     : Implementation of the FS_Format API function.
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
*       Public code (internal)
*
**********************************************************************
*/

/*********************************************************************
*
*       FS__Format
*
*  Function description
*    Internal version of FS_Format().
*    Formats the storage medium.
*
*  Parameters
*    pVolume       [IN]  Volume to format. NULL is not permitted.
*    pFormatInfo   [IN]  Add. optional format information.
*
*  Return value
*    ==0   OK, storage medium formatted.
*    !=0   Error code indicating the failure reason.
*/
int FS__Format(FS_VOLUME * pVolume, const FS_FORMAT_INFO * pFormatInfo) {
  int         r;
  int         Status;
  FS_DEVICE * pDevice;

  r = FS_ERRCODE_STORAGE_NOT_READY;
  pDevice = &pVolume->Partition.Device;
  FS_LOCK_DRIVER(pDevice);
  Status = FS_LB_GetStatus(pDevice);
  if (Status != FS_MEDIA_NOT_PRESENT) {
    FS__UnmountNL(pVolume);
    r = FS_LB_InitMediumIfRequired(pDevice);
    if (r == 0) {
      r = FS_FORMAT(pVolume, pFormatInfo);
    }
  } else {
    //
    // Umounting the file system when the format operation is not
    // performed does not make too much sense. However, we have to
    // do this for backward compatibility.
    //
    FS__UnmountForcedNL(pVolume);
  }
  FS_UNLOCK_DRIVER(pDevice);
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
*       FS_Format
*
*  Function description
*    Performs a high-level format.
*
*  Parameters
*    sVolumeName   Volume name.
*    pFormatInfo   [IN]  Additional format information. Can be NULL.
*
*  Return value
*    ==0   O.K., format successful.
*    !=0   Error code indicating the failure reason.
*
*  Additional information
*    The high-level format operation has to be performed once before
*    using the storage device for the first time. FS_Format() stores
*    the management information required by the file system on the
*    storage device. This means primarily the initialization of the
*    allocation table and of the root directory, as well as of the
*    BIOS Parameter Block (BPB) for a volume formatted as FAT and
*    of the Information Sector for a volume formatted as EFS.
*
*    The type of file system can be selected at compile time via
*    FS_SUPPORT_FAT and FS_SUPPORT_EFS defines. If both file systems
*    are enabled at compile time the type of file system can be
*    configured via FS_SetFSType().
*
*    There are many different ways to format a medium, even with one
*    file system. If the pFormatInfo parameter is not specified,
*    reasonable default values are used (auto-format). However,
*    FS_Format() allows fine-tuning of the parameters used. For
*    increased performance it is recommended to format the storage
*    with clusters as large as possible. The larger the cluster the
*    smaller gets the number of accesses to the allocation table
*    the file system has to perform during a read or write operation.
*    For more information about format parameters see FS_FORMAT_INFO.
*/
int FS_Format(const char * sVolumeName, const FS_FORMAT_INFO * pFormatInfo) {
  int         r;
  FS_VOLUME * pVolume;

  FS_LOCK();
  FS_PROFILE_CALL_STRING(FS_EVTID_FORMAT, sVolumeName);
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    r = FS__Format(pVolume, pFormatInfo);
  } else {
    r = FS_ERRCODE_VOLUME_NOT_FOUND;        // Error, invalid volume specified.
  }
  FS_PROFILE_END_CALL_U32(FS_EVTID_FORMAT, r);
  FS_UNLOCK();
  return r;
}

/*************************** End of file ****************************/
