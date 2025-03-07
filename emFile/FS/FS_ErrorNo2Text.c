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
File        : FS_ErrorNo2Text.c
Purpose     : Implementation of functions which convert an error code to a text.
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

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_ErrorNo2Text
*
*  Function description
*    Returns a human-readable text description of an API error code.
*
*  Parameters
*    ErrCode      Error code for which the text
*                 description has to be returned.
*
*  Return value
*    The text description as 0-terminated ASCII string.
*
*  Additional information
*    For a list of supported error codes refer to \ref{Error codes}.
*    If the error code is not known FS_ErrorNo2Text() returns the
*    "Unknown error" string. The error status of an opened file
*    handle can be queried via FS_FError(). Most of the API functions
*    of the file system also return one of the defined codes in case
*    of an error. This error code can be passed to FS_ErrorNo2Text()
*    to get a human-readable text description.
*/
const char * FS_ErrorNo2Text(int ErrCode) {
  const char * pText;

  switch (ErrCode) {
  case FS_ERRCODE_OK:
    pText = "No error";
    break;
  case FS_ERRCODE_EOF:
    pText = "End of file reached";
    break;
  case FS_ERRCODE_PATH_TOO_LONG:
    pText = "Path too long";
    break;
  case FS_ERRCODE_INVALID_PARA:
    pText = "Invalid parameter";
    break;
  case FS_ERRCODE_WRITE_ONLY_FILE:
    pText = "File is write only";
    break;
  case FS_ERRCODE_READ_ONLY_FILE:
    pText = "File is read only";
    break;
  case FS_ERRCODE_READ_FAILURE:
    pText = "Read error";
    break;
  case FS_ERRCODE_WRITE_FAILURE:
    pText = "Write error";
    break;
  case FS_ERRCODE_FILE_IS_OPEN:
    pText = "File is open";
    break;
  case FS_ERRCODE_PATH_NOT_FOUND:
    pText = "Path not found";
    break;
  case FS_ERRCODE_FILE_DIR_EXISTS:
    pText = "File/dir exists";
    break;
  case FS_ERRCODE_NOT_A_FILE:
    pText = "Not a file";
    break;
  case FS_ERRCODE_TOO_MANY_FILES_OPEN:
    pText = "Too many files open";
    break;
  case FS_ERRCODE_INVALID_FILE_HANDLE:
    pText = "Invalid file handle";
    break;
  case FS_ERRCODE_VOLUME_NOT_FOUND:
    pText = "Volume not found";
    break;
  case FS_ERRCODE_READ_ONLY_VOLUME:
    pText = "Read-only volume";
    break;
  case FS_ERRCODE_VOLUME_NOT_MOUNTED:
    pText = "Volume not mounted";
    break;
  case FS_ERRCODE_NOT_A_DIR:
    pText = "Not a directory";
    break;
  case FS_ERRCODE_FILE_DIR_NOT_FOUND:
    pText = "File/dir not found";
    break;
  case FS_ERRCODE_NOT_SUPPORTED:
    pText = "Feature not supported";
    break;
  case FS_ERRCODE_CLUSTER_NOT_FREE:
    pText = "Cluster not free";
    break;
  case FS_ERRCODE_INVALID_CLUSTER_CHAIN:
    pText = "Invalid cluster chain";
    break;
  case FS_ERRCODE_STORAGE_NOT_PRESENT:
    pText = "Storage not present";
    break;
  case FS_ERRCODE_BUFFER_NOT_AVAILABLE:
    pText = "No buffer available";
    break;
  case FS_ERRCODE_STORAGE_TOO_SMALL:
    pText = "Storage too small";
    break;
  case FS_ERRCODE_STORAGE_NOT_READY:
    pText = "Storage not ready";
    break;
  case FS_ERRCODE_BUFFER_TOO_SMALL:
    pText = "Buffer too small";
    break;
  case FS_ERRCODE_INVALID_FS_FORMAT:
    pText = "Invalid FS format";
    break;
  case FS_ERRCODE_INVALID_FS_TYPE:
    pText = "Invalid FS type";
    break;
  case FS_ERRCODE_FILENAME_TOO_LONG:
    pText = "File name too long";
    break;
  case FS_ERRCODE_VERIFY_FAILURE:
    pText = "Verify error";
    break;
  case FS_ERRCODE_VOLUME_FULL:
    pText = "Disk full";
    break;
  case FS_ERRCODE_DIR_NOT_EMPTY:
    pText = "Directory not empty";
    break;
  case FS_ERRCODE_IOCTL_FAILURE:
    pText = "I/O error";
    break;
  case FS_ERRCODE_INVALID_MBR:
    pText = "Invalid MBR";
    break;
  case FS_ERRCODE_OUT_OF_MEMORY:
    pText = "Not enough memory";
    break;
  case FS_ERRCODE_UNKNOWN_DEVICE:
    pText = "No storage device";
    break;
  case FS_ERRCODE_ASSERT_FAILURE:
    pText = "Assert failure";
    break;
  case FS_ERRCODE_TOO_MANY_TRANSACTIONS_OPEN:
    pText = "Too many transactions open";
    break;
  case FS_ERRCODE_NO_OPEN_TRANSACTION:
    pText = "No open transaction";
    break;
  case FS_ERRCODE_INIT_FAILURE:
    pText = "Init error";
    break;
  case FS_ERRCODE_FILE_TOO_LARGE:
    pText = "File too large";
    break;
  case FS_ERRCODE_HW_LAYER_NOT_SET:
    pText = "HW layer not set";
    break;
  case FS_ERRCODE_INVALID_USAGE:
    pText = "Invalid usage";
    break;
  case FS_ERRCODE_TOO_MANY_INSTANCES:
    pText = "Too many instances";
    break;
  case FS_ERRCODE_TRANSACTION_ABORTED:
    pText = "Transaction aborted";
    break;
  case FS_ERRCODE_INVALID_CHAR:
    pText = "Invalid character";
    break;
  case FS_ERRCODE_INVALID_GPT:
    pText = "Invalid GPT";
    break;
  default:
    pText = "Unknown error";
    break;
  }
  return pText;
}

/*********************************************************************
*
*       FS_CheckDisk_ErrCode2Text
*
*  Function description
*    Returns a human-readable text description of a disk checking
*    error code.
*
*  Parameters
*    ErrCode      Error code for which the text
*                 description has to be returned.
*
*  Return value
*    Text description as 0-terminated string.
*
*  Additional information
*    This function can be invoked inside the callback
*    for FS_CheckDisk() to format the error information
*    in human-readable format. The text description includes
*    format specifiers for the printf() family of functions that
*    can be used to show additional information about the
*    file system error.
*
*    Refer to FS_CheckDisk() for more information about how
*    to use this function.
*/
const char * FS_CheckDisk_ErrCode2Text(int ErrCode) {
  const char * sFormat = NULL;
  switch (ErrCode) {
  case FS_CHECKDISK_ERRCODE_0FILE:
    sFormat = "Cluster chain starting on cluster %d assigned to file of zero size.";
    break;
  case FS_CHECKDISK_ERRCODE_SHORTEN_CLUSTER:
    sFormat = "Need to shorten cluster chain on cluster %d.";
    break;
  case FS_CHECKDISK_ERRCODE_CROSSLINKED_CLUSTER:
    sFormat = "Cluster %d is cross-linked (used for multiple files / directories) FileId: %d:%d.";
    break;
  case FS_CHECKDISK_ERRCODE_FEW_CLUSTER:
    sFormat = "Too few clusters allocated to file.";
    break;
  case FS_CHECKDISK_ERRCODE_CLUSTER_UNUSED:
    sFormat = "Cluster %d is marked as used, but not assigned to any file or directory.";
    break;
  case FS_CHECKDISK_ERRCODE_CLUSTER_NOT_EOC:
    sFormat = "Cluster %d is not marked as end-of-chain.";
    break;
  case FS_CHECKDISK_ERRCODE_INVALID_CLUSTER:
    sFormat = "Cluster %d is not a valid cluster.";
    break;
  case FS_CHECKDISK_ERRCODE_INVALID_DIRECTORY_ENTRY:
    sFormat = "Invalid directory entry found.";
    break;
  case FS_CHECKDISK_ERRCODE_SECTOR_NOT_IN_USE:
    sFormat = "Sector %d is marked as not used on the storage.";
    break;
  case FS_CHECKDISK_ERRCODE_INVALID_FILE:
    sFormat = "Invalid file found.";
    break;
  default:
    sFormat = "Unknown error.";
    break;
  }
  return sFormat;
}

/*************************** End of file ****************************/
