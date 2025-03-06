/*********************************************************************
*                   (c) SEGGER Microcontroller GmbH                  *
*                        The Embedded Experts                        *
**********************************************************************
*                                                                    *
*       (c) 2003 - 2022     SEGGER Microcontroller GmbH              *
*                                                                    *
*       www.segger.com     Support: www.segger.com/ticket            *
*                                                                    *
**********************************************************************
*                                                                    *
*       emUSB-Host * USB Host stack for embedded applications        *
*                                                                    *
*       Please note: Knowledge of this file may under no             *
*       circumstances be used to write a similar product.            *
*       Thank you for your fairness !                                *
*                                                                    *
**********************************************************************
*                                                                    *
*       emUSB-Host version: V2.36.1                                  *
*                                                                    *
**********************************************************************
----------------------------------------------------------------------
Licensing information
Licensor:                 SEGGER Microcontroller Systems LLC
Licensed to:              React Health, Inc., 203 Avenue A NW, Suite 300, Winter Haven FL 33881, USA
Licensed SEGGER software: emUSB-Host
License number:           USBH-00304
License model:            SSL [Single Developer Single Platform Source Code License]
Licensed product:         -
Licensed platform:        STM32F4, IAR
Licensed number of seats: 1
----------------------------------------------------------------------
Support and Update Agreement (SUA)
SUA period:               2022-05-19 - 2022-11-19
Contact to extend SUA:    sales@segger.com
----------------------------------------------------------------------
Purpose     : MSC internal (USB host stack)
-------------------------- END-OF-HEADER -----------------------------
*/

#ifndef USBH_MSC_INT_H_
#define USBH_MSC_INT_H_

#include "USBH_MSD.h"
#include "USBH_Int.h"

/*********************************************************************
*
*       Defines, fixed
*
**********************************************************************
*/
// Constants in the Class Interface Descriptor
// for USB Mass Storage devices
//
#define MASS_STORAGE_CLASS             0x08u
#define PROTOCOL_BULK_ONLY             0x50u // Bulk only
#define SUBCLASS_6                     0x06u // Transparent SCSI, that can be used as SUBCLASS_RBC

// Bulk only class specific requests
#define BULK_ONLY_RESET_REQ            0xFFu
#define BULK_ONLY_GETLUN_REQ           0xFEu
#define BULK_ONLY_GETLUN_LENGTH           1u

/*********************************************************************
*
*       Types
*
**********************************************************************
*/
//
// Device states
//
//lint -strong(AXJ, MSD_STATE)
typedef enum {
  MSD_STATE_START,                //  --+
  MSD_STATE_GET_MAX_LUN_RETRY,    //    |
  MSD_STATE_GET_MAX_LUN,          //    |
  MSD_STATE_INIT_LUNS,            //    |
  MSD_STATE_TST_UNIT_RDY_RETRY,   //    |
  MSD_STATE_TST_UNIT_RDY,         //    +--- Used by device initialization state machine
  MSD_STATE_INQUIRY,              //    |
  MSD_STATE_READ_CAPACITY,        //    |
  MSD_STATE_MODE_SENSE,           //    |
  MSD_STATE_LUN_FINISHED,         //  --+
  MSD_STATE_DEAD,                 // Error occurred during initialization, device not usable
  MSD_STATE_READY,                // Ready and idle
  MSD_STATE_BUSY                  // Read / Write in progress by API function
} MSD_STATE;

//
// States for SCSI sub state machine
//
//lint -strong(AXJ, MSD_SUBSTATE)
typedef enum {
  MSD_SUBSTATE_START,
  MSD_SUBSTATE_REQUEST_SENSE,
  MSD_SUBSTATE_CMD_PHASE,
  MSD_SUBSTATE_DATA_PHASE,
  MSD_SUBSTATE_RESET_PIPE,
  MSD_SUBSTATE_READ_CSW,
  MSD_SUBSTATE_STATUS_PHASE,
  MSD_SUBSTATE_END
} MSD_SUBSTATE;


/*********************************************************************
*
*       Unit object
*
**********************************************************************
*/
typedef struct {
  U8   DeviceType;
  U8   RMB;
  U8   Version;
  U8   ResponseFormat;
  U8   AddLength;
  U8   Sccs;
  U8   Flags[2];
  U8   aVendorIdentification[8];
  U8   aProductIdentification[16];
  U8   aRevision[4];
} INQUIRY_STANDARD_RESPONSE;


typedef struct {                              // USBH_MSD_UNIT describes a logical unit of a device
  struct _USBH_MSD_INST *   pInst;            // Pointer to the device, if NULL then the unit is invalid
  U8                        Lun;              // Used to address the device in the transport layer
  U8                        Unit;             // Index into apLogicalUnit[]
  U16                       BytesPerSector;   // Size of a sector (logical block) in bytes, if zero the field is invalid
  U32                       MaxSectorAddress;
  USBH_TIME                 NextTestUnitReadyTime;
  I8                        NextTestUnitReadyValid;
  I8                        WriteProtect;
  INQUIRY_STANDARD_RESPONSE InquiryData;
} USBH_MSD_UNIT;


/*********************************************************************
*
*       Device object
*
**********************************************************************
*/

typedef struct {
  U8                            * pData;             // Pointer to data buffer, must be set by the caller
  U32                             Length;            // Length of data to be read / written, must be set by the caller
  const U8                      * pCmd;              // Command block, must be set by the caller
  U32                             BytesToTransfer;   // Internal use by the state machine
  U8                              Lun;               // Must be set by the caller
  I8                              Direction;         // 0 = Read, 1 = Write, must be set by the caller
  I8                              ZeroCopy;          // Internal use by the state machine
  MSD_SUBSTATE                    State;             // Must be initialized to MSD_SUBSTATE_START
  USBH_STATUS                     Status;            // Final result state (if != PENDING), set by the state machine
  U8                              Sensekey;          // Set by the state machine if Status == USBH_STATUS_COMMAND_FAILED
  I8                              RequestSense;      // Internal use by the state machine
  U8                              Buff[18];          // Temp. buffer for sense data, capacity, mode sense, ...
} USBH_MSD_SUBSTATE;


typedef struct _USBH_MSD_INST {
#if USBH_DEBUG > 1
  U32                             Magic;
#endif
  MSD_STATE                       State;
  USBH_BOOL                       Removed;                         // Set if the device is removed if the error recovery routine fails
  U8                              DeviceIndex;                     // 0-based device index
  U8                              UnitCnt;
  USBH_MSD_UNIT                 * aUnits;                          // Pointer to array of units
  USBH_INTERFACE_ID               InterfaceID;
  USBH_INTERFACE_HANDLE           hInterface;                      // UBD driver interface
  USBH_OS_EVENT_OBJ             * pUrbEvent;                       // Event for synchronous URB requests
  U8                            * pTempBuf;
  U16                             BulkMaxPktSize;
  U8                              BulkInEp;
  U8                              BulkOutEp;
  U8                              bInterfaceNumber;                // Zero based interface number of the current used USB Mass Storage interface
  U8                              ErrorCount;                      // Only used for initialization
  U8                              NumLUNs;                         // Only used for initialization, before UnitCnt is set.
  USBH_TIMER                      StateTimer;                      // State machine for initialization is run via this timer
  USBH_TIME                       ReadyWaitTimeout;                // Only used for initialization
  U32                             BlockWrapperTag;                 // Tag is used for the bulk only command and status wrapper
  USBH_TIMER                      RemovalTimer;
  USBH_TIMER                      AbortTimer;
  U32                             MaxOutTransferSize;
  U32                             MaxInTransferSize;
  USBH_URB                        Urb;
  USBH_MSD_SUBSTATE               SubState;
} USBH_MSD_INST;


typedef struct {
  USBH_STATUS (*pfReadSectors)(USBH_MSD_UNIT * pUnit, U32 SectorAddress, U8 * pBuf, U16 NumSectors);
  USBH_STATUS (*pfWriteSectors)(USBH_MSD_UNIT * pUnit, U32 SectorAddress, const U8 * pBuf, U16 NumSectors);
  void  (*pfInvalidate)(USBH_MSD_UNIT * pUnit);
} USBH_MSD_CACHE_API;


/*********************************************************************
*
*       Driver object
*
**********************************************************************
*/
typedef struct {
  USBH_MSD_UNIT                  * apLogicalUnit[USBH_MSD_MAX_UNITS];     // Maximum number of logical units of all connected USB Mass Storage devices
  USBH_MSD_INST                  * pDevices[USBH_MSD_MAX_DEVICES];
  USBH_NOTIFICATION_HANDLE         hPnPNotify;
  USBH_MSD_LUN_NOTIFICATION_FUNC * pfLunNotification;        // This user callback function is called if a new logical unit(s) is found
  void                           * pContext;                 // LunNotification context
  USBH_MSD_CACHE_API             * pCacheAPI;
} USBH_MSD_GLOBAL;

extern USBH_MSD_GLOBAL USBH_MSD_Global;

//
// Internal API
//
USBH_STATUS USBH_MSD__ReadSectorsNoCache(const USBH_MSD_UNIT * pUnit, U32 SectorAddress, U8 * pData, U32 Sectors);
USBH_STATUS USBH_MSD__WriteSectorsNoCache(const USBH_MSD_UNIT * pUnit, U32 SectorAddress, const U8 * pData, U32 Sectors);

#endif // USBH_MSC_INT_H_

/*************************** End of file ****************************/
