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
File        : USBH_MSD_Int.h
Purpose     : MSD API of the USB host stack
-------------------------- END-OF-HEADER -----------------------------
*/

#ifndef USBH_MSD_INT_H_
#define USBH_MSD_INT_H_

#include "SEGGER.h"
#include "USBH_MSD.h"
#include "USBH_Int.h"

#if defined(__cplusplus)
  extern "C" {                 // Make sure we have C-declarations in C++ programs
#endif


#define SCSI_6BYTE_COMMAND_LENGTH   6u
#define SCSI_10BYTE_COMMAND_LENGTH 10u
// Attention: all SCSI commands are in big endian byte order !!!
// Commands implemented by all SCSI device servers:
// SC_INQUIRY
// SC_REQUEST_SENSE
// SC_SEND_DIAGNOSTIC
// SC_TEST_UNIT_READY
// SCSI commands used from RBS devices
#define SC_TEST_UNIT_READY                             (0x00u)
#define SC_REQUEST_SENSE                               (0x03u)
#define SC_INQUIRY                                     (0x12u)
// Inquiry command parameter
#define STANDARD_INQUIRY_DATA_LENGTH                   (0x24u)
#define INQUIRY_ENABLE_PRODUCT_DATA                    (0x01u)
#define INQUIRY_ENABLE_COMMAND_SUPPORT                 (0x02u)
// mode page related defines
#define SC_MODE_SENSE_6                                (0x1Au)
#define SC_MODE_SENSE_10                               (0x5Au)
// Command block offset for the page parameter
#define PAGE_CODE_OFFSET                               2u
#define MODE_WRITE_PROTECT_OFFSET                      2u
#define MODE_WRITE_PROTECT_MASK                        0x80u
#define MODE_SENSE_PARAMETER_LENGTH                    0xC0u
#define SC_MODE_PARAMETER_HEADER_LENGTH_6              4u
#define SC_MODE_PARAMETER_HEADER_LENGTH_10             8u
// Common for both headers
#define MODE_PARAMETER_HEADER_DATA_LENGTH_OFS          0u
// 6 byte Sense mode header
#define MODE_PARAMETER_HEADER_MEDIUM_TYPE_OFS_6        1u
#define MODE_PARAMETER_HEADER_DEVICE_PARAM_OFS_6       2u
#define MODE_PARAMETER_HEADER_BLOCK_DESC_LENGTH_OFS_6  3u
// 10 byte Sense mode header
#define MODE_PARAMETER_HEADER_MEDIUM_TYPE_OFS_10       2u
#define MODE_PARAMETER_HEADER_DEVICE_PARAM_OFS_10      3u
#define MODE_PARAMETER_HEADER_BLOCK_DESC_LENGTH_OFS_10 6u

// Mode parameter struct, used to convert mode parameter(6) and mode parameter(10) header in this format
typedef struct {
  U16 DataLength; // Data length member of the received mode parameter header
  U8  MediumType;
  U8  DeviceParameter;
  U16 BlockDescriptorLength;
  U16 DataOffset; // Offset in data buffer where the mode pages parameter or the block descriptors (if available) begins
} MODE_PARAMETER_HEADER;

// Mode Sense/Select page constants.
#define MODE_PAGE_ERROR_RECOVERY        0x01u
#define MODE_PAGE_DISCONNECT            0x02u
#define MODE_PAGE_FORMAT_DEVICE         0x03u
#define MODE_PAGE_RIGID_GEOMETRY        0x04u
#define MODE_PAGE_FLEXIBILE             0x05u // disk
#define MODE_PAGE_WRITE_PARAMETERS      0x05u // cdrom
#define MODE_PAGE_VERIFY_ERROR          0x07u
#define MODE_PAGE_CACHING               0x08u
#define MODE_PAGE_PERIPHERAL            0x09u
#define MODE_PAGE_CONTROL               0x0Au
#define MODE_PAGE_MEDIUM_TYPES          0x0Bu
#define MODE_PAGE_NOTCH_PARTITION       0x0Cu
#define MODE_PAGE_CD_AUDIO_CONTROL      0x0Eu
#define MODE_PAGE_DATA_COMPRESS         0x0Fu
#define MODE_PAGE_DEVICE_CONFIG         0x10u
#define MODE_PAGE_MEDIUM_PARTITION      0x11u
#define MODE_PAGE_CDVD_FEATURE_SET      0x18u
#define MODE_PAGE_POWER_CONDITION       0x1Au
#define MODE_PAGE_FAULT_REPORTING       0x1Cu
#define MODE_PAGE_CDVD_INACTIVITY       0x1Du // cdrom
#define MODE_PAGE_ELEMENT_ADDRESS       0x1Du
#define MODE_PAGE_TRANSPORT_GEOMETRY    0x1Eu
#define MODE_PAGE_DEVICE_CAPABILITIES   0x1Fu
#define MODE_PAGE_CAPABILITIES          0x2Au // cdrom
#define MODE_SENSE_RETURN_ALL_PAGES     0x3fu
#define MODE_SENSE_CURRENT_VALUES       0x00u
#define MODE_SENSE_CHANGEABLE_VALUES    0x40u
#define MODE_SENSE_DEFAULT_VAULES       0x80u
#define MODE_SENSE_SAVED_VALUES         0xc0u
#define SC_START_STOP_UNIT                  (0x1bu)
#define SC_SEND_DIAGNOSTIC                  (0x1du)
#define SC_READ_FORMAT_CAPACITY             (0x23u)
#define SC_READ_FORMAT_CAPACITY_DATA_LENGTH (0xfcu)
#define SC_READ_CAPACITY                    (0x25u)
// Read capacity command parameter
#define READ_CAPACITY_CMD_LENGTH            (10u)
#define SC_READ_10                          (0x28u)
#define SC_WRITE_10                         (0x2au)

// tidy  NOLINTBEGIN

// Standard 6 byte command
typedef struct {
  U8  Cmd;        // 0-command
  U8  MSBAddress; // 1-Reserved Bits and MS bits
  U16 LSBAddress; // 2,3
  U8  Length;     // 4
  U8  Control;    // 5-always the last byte
} SCSI_6BYTE_READ_WRITE_CMD;

#define SC_6BYTE_CMD_MAX_SECTORS   0xff
#define SC_6BYTE_CMD_MAX_ADDRESS   0xffffff

typedef struct {
  U8 Cmd; // 0-command
  U8 Index1;
  U8 Index2;
  U8 Index3;
  U8 Length;  // 4
  U8 Control; // 5-always the last byte
} SCSI_6BYTE_CMD;

typedef struct {
  U8  Cmd;      // 0-command
  U8  Service;  // 1-Reserved Bits and MS bits
  U32 Address; // 2,3,4,5
  U8  Reserved; // 6
  U16 Length;  // 7,8
  U8  Control;  // 9-always the last byte
} SCSI_10BYTE_CMD;

#define SC_10BYTE_CMD_MAX_SECTORS   0x0000FFFFu
#define RD_CAPACITY_DATA_LENGTH     8u

// Read capacity
typedef struct {
  U32 MaxBlockAddress;
  U32 BlockLength;
} RD_CAPACITY_DATA;

// Sense command parameter
#define SCS_DISABLE_BLOCK_DESC     (0x08u)
#define SCS_CURRENT_PARAMETER      (0u)
#define SCS_CHANGEABLE_PARAMETER   (1 << 6)
#define SCS_DEFAULT_PARAMETER      (2 << 6)
#define SCS_SAVED_PARAMETER        (3 << 6)
// Sense codes
#define SS_SENSE_NO_SENSE         0x00u
#define SS_SENSE_RECOVERED_ERROR  0x01u
#define SS_SENSE_NOT_READY        0x02u
#define SS_SENSE_MEDIUM_ERROR     0x03u
#define SS_SENSE_HARDWARE_ERROR   0x04u
#define SS_SENSE_ILLEGAL_REQUEST  0x05u
#define SS_SENSE_UNIT_ATTENTION   0x06u
#define SS_SENSE_DATA_PROTECT     0x07u
#define SS_SENSE_BLANK_CHECK      0x08u
#define SS_SENSE_UNIQUE           0x09u
#define SS_SENSE_COPY_ABORTED     0x0Au
#define SS_SENSE_ABORTED_COMMAND  0x0Bu
#define SS_SENSE_EQUAL            0x0Cu
#define SS_SENSE_VOL_OVERFLOW     0x0Du
#define SS_SENSE_MISCOMPARE       0x0Eu
#define SS_SENSE_RESERVED         0x0Fu
// Additional tape bit
#define SS_ILLEGAL_LENGTH         0x20u
#define SS_EOM                    0x40u
#define SS_FILE_MARK              0x80u
// Additional Sense codes
#define SS_ADSENSE_NO_SENSE           0x00u
#define SS_ADSENSE_LUN_NOT_READY      0x04u
#define SS_ADSENSE_TRACK_ERROR        0x14u
#define SS_ADSENSE_SEEK_ERROR         0x15u
#define SS_ADSENSE_REC_DATA_NOECC     0x17u
#define SS_ADSENSE_REC_DATA_ECC       0x18u
#define SS_ADSENSE_ILLEGAL_COMMAND    0x20u
#define SS_ADSENSE_ILLEGAL_BLOCK      0x21u
#define SS_ADSENSE_INVALID_CDB        0x24u
#define SS_ADSENSE_INVALID_LUN        0x25u
#define SS_ADWRITE_PROTECT            0x27u
#define SS_ADSENSE_MEDIUM_CHANGED     0x28u
#define SS_ADSENSE_BUS_RESET          0x29u
#define SS_ADSENSE_INVALID_MEDIA      0x30u
#define SS_ADSENSE_NO_MEDIA_IN_DEVICE 0x3Au
#define SS_ADSENSE_POSITION_ERROR     0x3Bu
#define SS_ADSENSE_FAILURE_PREDICTION_THRESHOLD_EXCEEDED 0x5Du
#define SS_FAILURE_PREDICTION_THRESHOLD_EXCEEDED         SS_ADSENSE_FAILURE_PREDICTION_THRESHOLD_EXCEEDED
#define SS_ADSENSE_COPY_PROTECTION_FAILURE               0x6fu
#define SS_ADSENSE_VENDOR_UNIQUE                         0x80u
#define SS_ADSENSE_MUSIC_AREA                            0xA0u
#define SS_ADSENSE_DATA_AREA                             0xA1u
#define SS_ADSENSE_VOLUME_OVERFLOW                       0xA7u
// SS_ADSENSE_LUN_NOT_READY (0x04) qualifiers
#define SS_SENSEQ_CAUSE_NOT_REPORTABLE         0x00u
#define SS_SENSEQ_BECOMING_READY               0x01u
#define SS_SENSEQ_INIT_COMMAND_REQUIRED        0x02u
#define SS_SENSEQ_MANUAL_INTERVENTION_REQUIRED 0x03u
#define SS_SENSEQ_FORMAT_IN_PROGRESS           0x04u
#define SS_SENSEQ_OPERATION_IN_PROGRESS        0x07u
// SS_ADSENSE_NO_SENSE (0x00) qualifiers
#define SS_SENSEQ_FILEMARK_DETECTED            0x01u
#define SS_SENSEQ_END_OF_MEDIA_DETECTED        0x02u
#define SS_SENSEQ_SETMARK_DETECTED             0x03u
#define SS_SENSEQ_BEGINNING_OF_MEDIA_DETECTED  0x04u
// SS_ADSENSE_ILLEGAL_BLOCK (0x21) qualifiers
#define SS_SENSEQ_ILLEGAL_ELEMENT_ADDR         0x01u
// SS_ADSENSE_POSITION_ERROR (0x3b) qualifiers
#define SS_SENSEQ_DESTINATION_FULL             0x0du
#define SS_SENSEQ_SOURCE_EMPTY                 0x0eu
// SS_ADSENSE_INVALID_MEDIA (0x30) qualifiers
#define SS_SENSEQ_INCOMPATIBLE_MEDIA_INSTALLED 0x00u
#define SS_SENSEQ_UNKNOWN_FORMAT               0x01u
#define SS_SENSEQ_INCOMPATIBLE_FORMAT          0x02u
// SS_ADSENSE_COPY_PROTECTION_FAILURE (0x6f) qualifiers
#define SS_SENSEQ_AUTHENTICATION_FAILURE                          0x00u
#define SS_SENSEQ_KEY_NOT_PRESENT                                 0x01u
#define SS_SENSEQ_KEY_NOT_ESTABLISHED                             0x02u
#define SS_SENSEQ_READ_OF_SCRAMBLED_SECTOR_WITHOUT_AUTHENTICATION 0x03u
#define SS_SENSEQ_MEDIA_CODE_MISMATCHED_TO_LOGICAL_UNIT           0x04u
#define SS_SENSEQ_LOGICAL_UNIT_RESET_COUNT_ERROR                  0x05u
// length of standard Sense answer
#define STANDARD_SENSE_LENGTH   (18u)

// 18 byte standard Sense data struct
typedef struct {
  U8  ResponseCode;   //only 0x70 is supported
  U8  Obsolete;       //1
  U8  Sensekey;       //2
  U32 Info;           //3,4,5,6
  U8  AddLength;      //7
  U32 Cmdspecific;    //8,9,10,11
  U8  Sensecode;      //12
  U8  Sensequalifier; //13
  U8  Unitcode;       //14
  U8  Keyspecific1;   //15
  U8  Keyspecific2;   //16
  U8  Keyspecific3;   //17
} STANDARD_SENSE_DATA;

// START STOP UNIT command parameter
#define STARTSTOP_PWR_INDEX    (4)
// Do not change the power condition
#define STARTSTOP_PWR_NO_CHANGE (0)
// Power state defines
#define STARTSTOP_PWR_ACTIVE  (1)
#define STARTSTOP_PWR_IDLE    (2)
#define STARTSTOP_PWR_STANDBY (3)
#define STARTSTOP_PWR_SLEEP   (4)
// Make the device ready for use
#define STARTSTOP_PWR_START (0x01)
// Byte length of the returned inquiry data
#define STANDARD_INQUIRY_LENGTH (96)

typedef enum {
  Standard,
  Productpage,
  CommandSupport
} INQUIRY_SELECT;

// First four bytes of the inquiry response page
typedef struct {
  U8   DeviceType;
  U8   RMB;
  U8   Version;
  U8   ResponseFormat;
  U8   AddLength;
  U8   Sccs;
  U16  Flags;
  U8   aVendorIdentification[8];
  U8   aProductIdentification[16];
  U8   aRevision[4];
} INQUIRY_STANDARD_RESPONSE;

// Device type
// 00h     direct access device (e.g. UHD floppy disk)
// 01h     sequential access device (e.g. magnetic tape)
// 02-03h  Reserved
// 04h     write once device (e.g. WORM optical disk)
// 05h     CD-ROM device
// 06h     Reserved
// 07h     optical memory device (e.g. optical disks (not CD))
// 08h-1Eh Reserved
// 1Fh     unknown or no device type

// Inquiry page device type
#define INQUIRY_DIRECT_DEVICE                     0x00u
#define INQUIRY_SEQ_DEVICE                        0x01u
#define INQUIRY_PRINTER_DEVICE                    0x02u
#define INQUIRY_PROCESSOR_DEVICE                  0x03u
#define INQUIRY_WRITE_ONCE_DEVICE                 0x04u
#define INQUIRY_CD_ROM_DEVICE                     0x05u
#define INQUIRY_SCANNER_DEVICE                    0x06u
#define INQUIRY_NON_CD_OPTICAL_DEVICE             0x07u
#define INQUIRY_MEDIUM_CHANGER_DEVICE             0x08u
#define INQUIRY_COMMUNICATIONS_DEVICE             0x09u
// 0x0A - 0x0B Defined by ASC IT8 (Graphic arts pre-press devices)
#define INQUIRY_STORAGE_ARRAY_CONTROLLER_DEVICE   0x0Cu
#define INQUIRY_ENCLOSURE_SERVICES_DEVICE         0x0Du
#define INQUIRY_SIMPLIFIED_DIRECT_DEVICE          0x0Eu
#define INQUIRY_OPTICAL_CARD_READER_WRITER_DEVICE 0x0Fu
#define INQUIRY_OBJECT_BASED_STORAGE_DEVICE       0x11u

#define INQUIRY_DEVICE_TYPE_MASK                  0x1Fu

// INQUIRY ANSI version
// 0h      The device might or might not comply to an ANSI approved standard.
// 1h      The device complies to ANSI X3.131-1986 (SCSI-1).
// 2h      The device complies to this version of SCSI. This code is Reserved to designate this standard upon approval by ANSI.
// 3h - 7h Reserved

#define INQUIRY_VERSION_MASK         0x07u
#define ANSI_VERSION_MIGHT_UFI       0u
#define ANSI_VERSION_SCSI_1          1u
#define ANSI_VERSION_SCSI_2          2u
#define ANSI_VERSION_SCSI_3_SPC      3u
#define ANSI_VERSION_SCSI_3_SPC_2    4u
#define ANSI_VERSION_SCSI_3_SPC_3_4  5u

#define INQUIRY_REMOVE_MEDIA_MASK        0x80u
#define INQUIRY_RESPONSE_FORMAT_MASK     0x0Fu
#define INQUIRY_RESPONSE_SCSI_1          0u
#define INQUIRY_RESPONSE_MIGTH_UFI       1u
#define INQUIRY_RESPONSE_IN_THIS_VERISON 2u

typedef struct {                              // USBH_MSD_UNIT describes a logical unit of a device
  struct _USBH_MSD_INST *   pInst;            // Pointer to the device, if NULL then the unit is invalid
  U8                        Lun;              // Used to address the device in the transport layer
  STANDARD_SENSE_DATA       Sense;            // Store the last Sense code from the device
  U16                       BytesPerSector;   // Size of a sector (logical block) in bytes, if zero the field is invalid
  U32                       MaxSectorAddress;
  INQUIRY_STANDARD_RESPONSE InquiryData;
  MODE_PARAMETER_HEADER     ModeParamHeader;
  I32                       LastTestUnitReadyTime;
} USBH_MSD_UNIT;


/*********************************************************************
*
*       Protocol layer interface
*
**********************************************************************
*/

typedef USBH_STATUS USBH_MSD_PL_READ_SECTORS        (USBH_MSD_UNIT * pUnit, U32   SectorAddress,       U8  * pBuf, U16 NumSectors); // returns: 0 for success, other values for errors
typedef USBH_STATUS USBH_MSD_PL_WRITE_SECTORS       (USBH_MSD_UNIT * pUnit, U32   SectorAddress, const U8  * pBuf, U16 NumSectors); // returns: 0 for success, other values for errors


/*********************************************************************
*
*       Device object
*
**********************************************************************
*/

typedef struct _USBH_MSD_INST {
  struct _USBH_MSD_INST         * pNext;
#if USBH_DEBUG > 1
  U32                             Magic;
#endif
  volatile int                    RefCnt;                          // Reference counter, see also INC_REF_CT, DEC_REF_CT
  USBH_BOOL                       Removed;                         // Set if the device is removed if the error recovery routine fails
  USBH_BOOL                       WaitForRemoval;                  // Set if RemovalTimer is active
  U8                              DeviceIndex;                     // 0-based device index
  USBH_MSD_UNIT                 * apUnit[USBH_MSD_MAX_UNITS];      // Pointer to units
  unsigned                        UnitCnt;                         // Maximum units of this device
  USBH_INTERFACE_ID               InterfaceID;
  USBH_INTERFACE_HANDLE           hInterface;                      // UBD driver interface
  USBH_OS_EVENT_OBJ             * pUrbEvent;                       // Event for synchronous URB requests
  USBH_URB                        ControlUrb;                      // Control endpoint
  USBH_URB                        Urb;                             // Data endpoint
  USBH_URB                        AbortUrb;                        // Abort
  U8                            * pTempBuf;
  // Private Data
  int                             bInterfaceNumber;                // Zero based interface number of the current used USB Mass Storage interface
  int                             bNumInterfaces;                  // Number of interfaces supported by the device.
  // Transport layer
  U8                              BulkInEp;
  U16                             BulkMaxPktSize;
  U8                              BulkOutEp;
  U32                             BlockWrapperTag;                 // Tag is used for the bulk only command and status wrapper
  unsigned                        ErrorCount;
  USBH_TIMER                      RemovalTimer;
  USBH_BOOL                       IsReady;
  U32                             MaxOutTransferSize;
  U32                             MaxInTransferSize;
} USBH_MSD_INST;

typedef struct {
  USBH_MSD_PL_READ_SECTORS    * pfReadSectors;
  USBH_MSD_PL_WRITE_SECTORS   * pfWriteSectors;
  void  (*pfInvalidate)(USBH_MSD_UNIT * pUnit);
} USBH_MSD_CACHE_API;

/*********************************************************************
*
*       Driver object
*
**********************************************************************
*/

typedef struct {
  USBH_MSD_INST                  * pFirst;
  U8                               NumDevices;
  USBH_MSD_UNIT                  * apLogicalUnit[USBH_MSD_MAX_UNITS];     // Maximum number of logical units of all connected USB Mass Storage devices
  USBH_NOTIFICATION_HANDLE         hPnPNotify;
  USBH_MSD_LUN_NOTIFICATION_FUNC * pfLunNotification;        // This user callback function is called if a new logical unit(s) is found
  void                           * pContext;                 // LunNotification context
  USBH_MSD_CACHE_API             * pCacheAPI;
  U32                              DevIndexUsedMask;
  U8                               IsInited;
  U8                               NumLUNs;
} USBH_MSD_GLOBAL;

extern USBH_MSD_GLOBAL USBH_MSD_Global;

/*********************************************************************
*
*       Defines and macros
*
**********************************************************************
*/

// if CSW_ALSO_VALID_IF_LENGTH_EQUAL_OR_GREATER is set to a none zero value then the received
// CSW block is also valid if the length is greater as the CSW length but all other bytes are ok.
#define CSW_ALSO_VALID_IF_LENGTH_EQUAL_OR_GREATER 1
// Maximum count for repeating a reading or writing command with a bulk-only USB Mass Storage reset
// command between the transfer, if the maximum is reached a USB bus reset is done with a set configuration request
#define BULK_ONLY_MAX_RETRY                       3u
#define CBW_SIGNATURE FOUR_CHAR_ULONG             ('U','S','B','C')
#define USB_BULK_IN_FLAG                          0x80u
#define CSW_SIGNATURE FOUR_CHAR_ULONG             ('U','S','B','S')
#define CSW_STATUS_GOOD                           0u
#define CSW_STATUS_FAIL                           1u
#define CSW_STATUS_PHASE_ERROR                    2u
// Bulk only class specific requests
#define BULK_ONLY_RESET_REQ                       0xFFu
#define BULK_ONLY_GETLUN_REQ                      0xFEu
#define BULK_ONLY_GETLUN_LENGTH                   1u    //length in bytes of BULK_ONLY_GETLUN_REQ
#define CBW_FLAG_READ                             0x80u
#define CBW_FLAG_WRITE                            0x00u
// Command block wrapper field length
#define CBW_LENGTH                                31u
#define COMMAND_WRAPPER_CDB_OFFSET                15u
#define COMMAND_WRAPPER_CDB_FIELD_LENGTH          16u
#define COMMAND_WRAPPER_FLAGS_OFFSET              12u

typedef struct {
  U32 Signature;          //  0: Contains 'USBC'
  U32 Tag;                //  4: Unique per command id
  U32 DataTransferLength; //  8: Size of the data
  U8  Flags;              // 12: Direction in bit 7
  U8  Lun;                // 13: LUN (normally 0)
  U8  Length;             // 14: Length of CDB, <= MAX_COMMAND_SIZE
  U8  CDB[16];            // 15: Command data block
} COMMAND_BLOCK_WRAPPER;

#define CSW_LENGTH                        13u
#define STATUS_WRAPPER_STATUS_OFFSET      12u

typedef struct {
  U32 Signature; // 0: Signature, should be 'USBS'
  U32 Tag;       // 4: Tag, same as original command
  U32 Residue;   // 8: The difference between the amount of data expected (as stated in cbw->DataTransferLength)  and the actual amount of data processed by the device
  U8  Status;    //12: Status 0:GOOD 1:FAILED 2:Phase Error(repeat the command)
} COMMAND_STATUS_WRAPPER;

void         USBH_MSD_ConvStandardSense(const U8 * pBuffer, STANDARD_SENSE_DATA * pSense);
void         USBH_MSD_ConvModeParameterHeader(MODE_PARAMETER_HEADER * pModeHeader, const U8 * pBuffer, USBH_BOOL IsModeSense6 /* true if mode Sense(6) command data is used, else mode Sense(10) is used */);

// Sends the init sequence to a device that supports the transparent SCSI protocol
USBH_STATUS  USBH_MSD_PHY_InitSequence    (USBH_MSD_INST * pInst);
USBH_BOOL    USBH_MSD_PHY_IsWriteProtected(const USBH_MSD_UNIT * pUnit); // Checks if the specified unit is write protected.


//
// Internal API
//
USBH_STATUS USBH_MSD__ReadSectorsNoCache(const USBH_MSD_UNIT * pUnit, U32 SectorAddress, U8 * pData, U16 Sectors);
USBH_STATUS USBH_MSD__WriteSectorsNoCache(const USBH_MSD_UNIT * pUnit, U32 SectorAddress, const U8 * pData, U16 Sectors);
USBH_STATUS USBH_MSD__RequestSense(USBH_MSD_UNIT * pUnit);
#if defined(__cplusplus)
  }
#endif

// tidy  NOLINTEND

#endif // USBH_MSD_INT_H_

/*************************** End of file ****************************/
