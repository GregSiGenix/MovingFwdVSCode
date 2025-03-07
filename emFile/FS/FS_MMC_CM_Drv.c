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
File        : FS_MMC_CM_Drv.c
Purpose     : File system generic MMC/SD card mode driver
Literature  :
  [1] The MultiMediaCard System Specification Version 3.2
    (\\fileserver\techinfo\Company\MMCA.org\mmcsys-version_3-2-SEGGER-Microcontroller.pdf)
  [2] SD Specifications Part 1 Physical Layer Specification Version 2.00
    ("\\fileserver\techinfo\Company\SDCard_org\Copyrighted_0812\Part 01 Physical Layer\Part 1 Physical Layer Specification Ver2.00 Final 060509.pdf")
  [3] Embedded MultiMediaCard (eMMC) eMMC/Card Product Standard, High Capacity, including Reliable Write, Boot, and Sleep Modes (MMCA, 4.3) JESD84-A43
    (\\fileserver\techinfo\Company\MMCA.org\mmc_v4_3.pdf)
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
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)                                    \
    if ((Unit) >= (U8)FS_MMC_NUM_UNITS) {                                     \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "MMC_CM: Invalid unit number."));  \
      FS_X_PANIC(FS_ERRCODE_INVALID_PARA);                                    \
    }
#else
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)
#endif

/*********************************************************************
*
*       ASSERT_HW_TYPE_IS_SET
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_HW_TYPE_IS_SET(pInst)                                            \
    if ((pInst)->pHWType == NULL) {                                               \
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER,  "MMC_CM: HW layer type is not set.")); \
      FS_X_PANIC(FS_ERRCODE_HW_LAYER_NOT_SET);                                    \
    }
#else
  #define ASSERT_HW_TYPE_IS_SET(pInst)
#endif

/*********************************************************************
*
*       FREE_BUFFER
*/
#if (FS_SUPPORT_FAT != 0) || (FS_SUPPORT_EFS != 0)
  #define FREE_BUFFER(ppBuffer)           _FreeBuffer(ppBuffer)
#else
  #define FREE_BUFFER(ppBuffer)
#endif

/*********************************************************************
*
*       Sanity checks
*/
#if (FS_MMC_SUPPORT_MMC == 0) && (FS_MMC_SUPPORT_SD == 0)
  #error FS_MMC_SUPPORT_MMC or FS_MMC_SUPPORT_SD has to be set to 1
#endif

/*********************************************************************
*
*       Sector size
*/
#define BYTES_PER_SECTOR_SHIFT            9       // Fixed in the SD and MMC specifications to 512 bytes
#define BYTES_PER_SECTOR                  (1uL << BYTES_PER_SECTOR_SHIFT)

/*********************************************************************
*
*       Command definitions common to MMC and SD storage devices
*/
#define CMD_GO_IDLE_STATE                 0u
#define CMD_ALL_SEND_CID                  2u
#define CMD_SWITCH                        6u      // Only for MMC cards
#define CMD_SELECT_CARD                   7u
#define CMD_SEND_EXT_CSD                  8u      // Only for MMCplus cards
#define CMD_SEND_CSD                      9u
#define CMD_SEND_CID                      10u
#define CMD_STOP_TRANSMISSION             12u
#define CMD_SEND_STATUS                   13u
#define CMD_BUSTEST_R                     14u     // Only for MMCplus cards
#define CMD_SET_BLOCKLEN                  16u
#define CMD_READ_SINGLE_BLOCK             17u
#define CMD_READ_MULTIPLE_BLOCKS          18u
#define CMD_BUSTEST_W                     19u     // Only for MMCplus cards
#define CMD_SEND_TUNING_BLOCK_SD          19u     // Only for SD cards
#define CMD_SEND_TUNING_BLOCK_MMC         21u     // Only for eMMC devices
#define CMD_SET_BLOCK_COUNT               23u     // Only for eMMC devices
#define CMD_WRITE_BLOCK                   24u
#define CMD_WRITE_MULTIPLE_BLOCKS         25u
#define CMD_LOCK_UNLOCK                   42u
#define CMD_APP_CMD                       55u

/*********************************************************************
*
*       Command definitions only for SD cards
*/
#if FS_MMC_SUPPORT_SD
  #define CMD_SEND_RELATIVE_ADDR          3u
  #define CMD_SWITCH_FUNC                 6u
  #define CMD_SEND_IF_COND                8u
#if FS_MMC_SUPPORT_UHS
  #define CMD_VOLTAGE_SWITCH              11u
#endif // FS_MMC_SUPPORT_UHS
#endif // FS_MMC_SUPPORT_SD

/*********************************************************************
*
*       Command definitions only for MMC storage devices
*/
#if FS_MMC_SUPPORT_MMC
  #define CMD_SEND_OP_COND                1u
  #define CMD_SET_RELATIVE_ADDR           3u
#if FS_MMC_SUPPORT_POWER_SAVE
  #define CMD_SLEEP_AWAKE                 5u
#endif // FS_MMC_SUPPORT_POWER_SAVE
  #define CMD_ERASE_GROUP_START           35u
  #define CMD_ERASE_GROUP_END             36u
  #define CMD_ERASE_MMC                   38u
#endif // FS_MMC_SUPPORT_MMC

/*********************************************************************
*
*       Advanced command definitions
*/
#if FS_MMC_SUPPORT_SD
  #define ACMD_SET_BUS_WIDTH              6u
  #define ACMD_SD_STATUS                  13u
  #define ACMD_SET_WR_BLK_ERASE_COUNT     23u
  #define ACMD_SD_SEND_OP_COND            41u
#if FS_MMC_DISABLE_DAT3_PULLUP
  #define ACMD_SET_CLR_CARD_DETECT        42u
#endif // FS_MMC_DISABLE_DAT3_PULLUP
  #define ACMD_SEND_SCR                   51u
#endif // FS_MMC_SUPPORT_SD

/*********************************************************************
*
*       Operation conditions register (OCR)
*/
#if FS_MMC_SUPPORT_SD
  #define OCR_S18A_SHIFT                  0    // Offset in byte 1 of response
#endif // FS_MMC_SUPPORT_SD
#define OCR_CCS_SHIFT                     6    // Offset in byte 1 of response
#define OCR_READY_SHIFT                   7    // Offset in byte 1 of response
#if FS_MMC_SUPPORT_MMC
  #define OCR_1V7_1V9_SHIFT               7    // Offset in byte 4 of response
#endif

/*********************************************************************
*
*       Command argument
*/
#if FS_MMC_SUPPORT_SD
  #define ARG_BUS_WIDTH_1BIT              0u
  #define ARG_BUS_WIDTH_4BIT              2u
  #define ARG_S18R_SHIFT                  24u
  #define ARG_VHS_2V7_3V6                 1u
  #define ARG_VHS_SHIFT                   8u
  #define ARG_VHS_MASK                    0xFuL
#endif
#define ARG_RCA_SHIFT                     16u
#define ARG_HCS_SHIFT                     30u
#if FS_MMC_SUPPORT_MMC
  #define ARG_BUSY_SHIFT                  31u
#endif // FS_MMC_SUPPORT_MMC
#define ARG_RELIABLE_WRITE_SHIFT          31u
#if FS_MMC_SUPPORT_MMC
  #define ARG_ERASE_IS_SECURE_SHIFT       31u
  #define ARG_ERASE_FORCE_GC_SHIFT        15u
  #define ARG_ERASE_MARK_SHIFT            0u
#if FS_MMC_SUPPORT_POWER_SAVE
  #define ARG_SLEEP_AWAKE_SHIFT           15u
#endif // FS_MMC_SUPPORT_POWER_SAVE
#endif // FS_MMC_SUPPORT_MMC

/*********************************************************************
*
*       Number of bytes in responses
*/
#if FS_MMC_SUPPORT_SD
  #define NUM_BYTES_SWITCH_RESP           64u
  #define NUM_BYTES_SD_STATUS             64u   // Number of bytes in the SD Status (SSR) register
  #define NUM_BYTES_R6                    6u    // Number of bytes sent in a R6 response
  #define NUM_BYTES_R7                    6u    // Number of bytes sent in a R7 response
#endif // FS_MMC_SUPPORT_SD
#define NUM_BYTES_EXT_CSD                 512u  // Size in bytes of the EXT_CSD register
#define NUM_BYTES_SCR                     8u    // Number of bytes in the SCR register
#define NUM_BYTES_LOCK_UNLOCK             36u   // Number of bytes in a LOCK/UNLOCK data transfer
#define NUM_BYTES_R2                      17u   // Number of bytes sent in a R2 response
#if FS_MMC_SUPPORT_UHS
  #define NUM_BYTES_TUNING_BLOCK_4BIT     64    // Number of bytes in a tuning block for a 4 bit bus.
  #define NUM_BYTES_TUNING_BLOCK_8BIT     128   // Number of bytes in a tuning block for a 8 bit bus.
#if FS_MMC_SUPPORT_MMC
  #define NUM_BYTES_TUNING_BLOCK          NUM_BYTES_TUNING_BLOCK_8BIT
#else
  #define NUM_BYTES_TUNING_BLOCK          NUM_BYTES_TUNING_BLOCK_4BIT
#endif // FS_MMC_SUPPORT_MMC
#endif // FS_MMC_SUPPORT_UHS

/*********************************************************************
*
*       CMD_SWITCH_FUNC
*/
#if FS_MMC_SUPPORT_SD
  #define FUNC_GROUP_ACCESS_MODE          0
  #define FUNC_GROUP_MAX                  6u
  #define LD_NUM_BITS_FUNC_SUPPORT        4
  #define LD_NUM_BITS_FUNC_BUSY           4
  #define LD_NUM_BITS_FUNC_RESULT         2
  #define BIT_OFF_FUNC_SUPPORT            400u
  #define BIT_OFF_FUNC_BUSY               272u
  #define BIT_OFF_FUNC_RESULT             376u
  #define ACCESS_MODE_HIGH_SPEED          1u
#if FS_MMC_SUPPORT_UHS
  #define FUNC_GROUP_DRIVER_STRENGTH      2
  #define ACCESS_MODE_SDR50               2u
  #define ACCESS_MODE_SDR104              3u
  #define ACCESS_MODE_DDR50               4u
#endif // FS_MMC_SUPPORT_UHS
#endif // FS_MMC_SUPPORT_SD

/*********************************************************************
*
*       CMD_LOCK
*/
#define LOCK_ERASE_SHIFT                  3
#define LOCK_LOCK_SHIFT                   2
#define LOCK_CLR_PWD_SHIFT                1
#define LOCK_SET_PWD_SHIFT                0

/*********************************************************************
*
*       Specification versions
*/
#if FS_MMC_SUPPORT_SD
  #define SD_SPEC_VER_200                 2u
#endif // FS_MMC_SUPPORT_SD
#if FS_MMC_SUPPORT_MMC
  #define MMC_SPEC_VER_4                  4uL
#endif // FS_MMC_SUPPORT_MMC

/*********************************************************************
*
*       Error flags in the card status
*/
#define STATUS_OUT_OF_RANGE_SHIFT         31
#define STATUS_ADDRESS_ERROR_SHIFT        30
#define STATUS_BLOCK_LEN_ERROR_SHIFT      29
#define STATUS_ERASE_SEQ_ERROR_SHIFT      28
#define STATUS_ERASE_PARAM_SHIFT          27
#define STATUS_WP_VIOLATION_SHIFT         26
#define STATUS_CARD_IS_LOCKED_SHIFT       25
#define STATUS_LOCK_UNLOCK_FAILED_SHIFT   24
#define STATUS_COM_CRC_ERROR_SHIFT        23
#define STATUS_ILLEGAL_COMMAND_SHIFT      22
#define STATUS_CARD_ECC_FAILED_SHIFT      21
#define STATUS_CC_ERROR_SHIFT             20
#define STATUS_ERROR_SHIFT                19
#define STATUS_CSD_OVERWRITE_SHIFT        16
#define STATUS_WP_ERASE_SKIP_SHIFT        15
#define STATUS_AKE_SEQ_ERROR_SHIFT        3
#define STATUS_ERROR_MASK                 ((1uL << STATUS_OUT_OF_RANGE_SHIFT)       | \
                                           (1uL << STATUS_ADDRESS_ERROR_SHIFT)      | \
                                           (1uL << STATUS_BLOCK_LEN_ERROR_SHIFT)    | \
                                           (1uL << STATUS_ERASE_SEQ_ERROR_SHIFT)    | \
                                           (1uL << STATUS_ERASE_PARAM_SHIFT)        | \
                                           (1uL << STATUS_WP_VIOLATION_SHIFT)       | \
                                           (1uL << STATUS_LOCK_UNLOCK_FAILED_SHIFT) | \
                                           (1uL << STATUS_COM_CRC_ERROR_SHIFT)      | \
                                           (1uL << STATUS_ILLEGAL_COMMAND_SHIFT)    | \
                                           (1uL << STATUS_CARD_ECC_FAILED_SHIFT)    | \
                                           (1uL << STATUS_CC_ERROR_SHIFT)           | \
                                           (1uL << STATUS_ERROR_SHIFT)              | \
                                           (1uL << STATUS_CSD_OVERWRITE_SHIFT)      | \
                                           (1uL << STATUS_WP_ERASE_SKIP_SHIFT)      | \
                                           (1uL << STATUS_AKE_SEQ_ERROR_SHIFT))

/*********************************************************************
*
*       Current state in card status
*/
#define CARD_STATE_MASK                   0xFu
#define CARD_STATE_STBY                   3u
#define CARD_STATE_TRAN                   4u
#define CARD_STATE_DATA                   5u    //lint -esym(750, CARD_STATE_DATA) local macro not referenced. Rationale: kept for future reference.
#define CARD_STATE_RCV                    6u
#define CARD_STATE_PRG                    7u
#define CARD_STATE_BTST                   9u    //lint -esym(750, CARD_STATE_BTST) local macro not referenced. Rationale: kept for future reference.
#define CARD_STATE_SLP                    10u   //lint -esym(750, CARD_STATE_SLP) local macro not referenced. Rationale: kept for future reference.

/*********************************************************************
*
*       Retry counts for command execution
*/
#if FS_MMC_SUPPORT_SD
  #define NUM_RETRIES_SWITCH              100
  #define NUM_RETRIES_RCA                 10
  #define NUM_RETRIES_IF_COND             3
#endif // FS_MMC_SUPPORT_SD
#define NUM_RETRIES_IDENTIFY_SD           2000                            // Maximum OCR request retries. This value makes sure that we retry at least 1 second (@ 400kHz) as recommended in the SD specification.
#if FS_MMC_SUPPORT_MMC
  #define NUM_RETRIES_IDENTIFY_MMC        (NUM_RETRIES_IDENTIFY_SD * 2)   // For MMC devices we have to send twice as much requests since these are normal and not application requests such is the case with SD cards.
#endif // FS_MMC_SUPPORT_MMC
#define NUM_RETRIES_CMD                   5
#define NUM_RETRIES_DATA_READ             5
#define NUM_RETRIES_INIT                  5
#define NUM_RETRIES_GO_IDLE               10
#if FS_MMC_SUPPORT_UHS
  #define NUM_RETRIES_TUNING              10
#endif // FS_MMC_SUPPORT_UHS

/*********************************************************************
*
*       Extended CSD register
*/
#if FS_MMC_SUPPORT_MMC
  #define OFF_EXT_CSD_CACHE_CTRL          33
  #define OFF_EXT_CSD_BUS_WIDTH           183
#if FS_MMC_SUPPORT_UHS
  #define OFF_EXT_CSD_STROBE_SUPPORT      184
#endif
  #define OFF_EXT_CSD_HS_TIMING           185
#if FS_MMC_SUPPORT_UHS
  #define OFF_EXT_CSD_DRIVER_STRENGTH     197
#endif
  #define OFF_EXT_CSD_CACHE_SIZE          249
  #define EXT_CSD_BUS_WIDTH_1BIT          0
  #define EXT_CSD_BUS_WIDTH_4BIT          1
  #define EXT_CSD_BUS_WIDTH_8BIT          2
  #define EXT_CSD_BUS_WIDTH_4BIT_DDR      5
  #define EXT_CSD_BUS_WIDTH_8BIT_DDR      6
  #define EXT_CSD_BUS_WIDTH_8BIT_DDR_ES   134
  #define EXT_CSD_HS_TIMING_HIGH_SPEED    1
#if FS_MMC_SUPPORT_UHS
  #define EXT_CSD_HS_TIMING_HS200         2
  #define EXT_CSD_HS_TIMING_HS400         3
  #define EXT_CSD_CARD_TYPE_HS_DDR_SHIFT  2   // Max 52 MHz, dual data rate, 1.8 V or 3.3 V signaling
  #define EXT_CSD_CARD_TYPE_HS200_SHIFT   4   // Max 200 MHz, single data rate, 1.8 V signaling
  #define EXT_CSD_CARD_TYPE_HS400_SHIFT   6   // Max 200 MHz, dual data rate, 1.8 V signaling
#endif
#endif // FS_MMC_SUPPORT_MMC
#define OFF_EXT_WR_REL_PARAM              166
#define OFF_EXT_CSD_CARD_TYPE             196
#define OFF_EXT_CSD_SEC_COUNT             212
#define EXT_CSD_CARD_TYPE_26MHZ_SHIFT     0
#define EXT_CSD_CARD_TYPE_52MHZ_SHIFT     1
#define EN_REL_WR_SHIFT                   2
#define SWITCH_ACCESS_WRITE_BYTE          3

/*********************************************************************
*
*       Voltage ranges and levels for I/O signaling
*/
#define VOLTAGE_RANGE_HIGH                0x00FF8000uL
#if FS_MMC_SUPPORT_MMC
  #define VOLTAGE_RANGE_LOW               0x00000080uL
#endif // FS_MMC_SUPPORT_MMC
#if FS_MMC_SUPPORT_UHS
  #define VOLTAGE_LEVEL_1V8_MV            1800
#endif

/*********************************************************************
*
*       Default values
*/
#define DEFAULT_RESPONSE_TIMEOUT          0xFF
#define DEFAULT_READ_DATA_TIMEOUT         0xFFFFFFFFuL
#if FS_MMC_SUPPORT_SD
  #define DEFAULT_VOLTAGE_RANGE_SD        ARG_VHS_2V7_3V6
  #define DEFAULT_CHECK_PATTERN           0xAAu
  #define DEFAULT_VOLTAGE_RANGE           VOLTAGE_RANGE_HIGH
#endif
#define DEFAULT_RCA_DESELECT              0u
#define DEFAULT_HC_SUPPORT                1
#if FS_MMC_SUPPORT_MMC
#if FS_MMC_SUPPORT_UHS
  #define DEFAULT_VOLTAGE_RANGE_MMC       (VOLTAGE_RANGE_HIGH | VOLTAGE_RANGE_LOW)
#else
  #define DEFAULT_VOLTAGE_RANGE_MMC       VOLTAGE_RANGE_HIGH
#endif
  #define DEFAULT_MMC_RCA                 1
#endif // FS_MMC_SUPPORT_MMC
#define DEFAULT_STARTUP_FREQ_KHZ          400         // Max. startup frequency.
#define DEFAULT_VOLTAGE_LEVEL_MV          3300u
#if FS_MMC_SUPPORT_UHS
  #define DEFAULT_MIN_LOW_VOLTAGE_MV      1700u
  #define DEFAULT_MAX_LOW_VOLTAGE_MV      1950u
#endif // FS_MMC_SUPPORT_UHS

/*********************************************************************
*
*       CSD register access macros
*/
#define CSD_STRUCTURE(pCSD)               _GetFromCSD(pCSD, 126, 127)
#if FS_MMC_SUPPORT_MMC
  #define CSD_SPEC_VERS(pCSD)             _GetFromCSD(pCSD, 122, 125)
#endif // FS_MMC_SUPPORT_MMC
#if FS_MMC_SUPPORT_SD
  #define CSD_CCC_CLASSES(pCSD)           _GetFromCSD(pCSD,  84, 95)
#endif // FS_MMC_SUPPORT_SD
#define CSD_WRITE_PROTECT(pCSD)           _GetFromCSD(pCSD, 12, 13)
#define CSD_C_SIZE_MULT(pCSD)             _GetFromCSD(pCSD, 47, 49)
#define CSD_C_SIZE(pCSD)                  _GetFromCSD(pCSD, 62, 73)
#define CSD_READ_BL_LEN(pCSD)             _GetFromCSD(pCSD, 80, 83)
#define CSD_TRAN_SPEED(pCSD)              ((pCSD)->aData[3])          // Same as, but more efficient than: _GetFromCSD(pCSD,  96, 103)
#define CSD_C_SIZE_V2(pCSD)               _GetFromCSD(pCSD, 48, 69)

/*********************************************************************
*
*       SCR register
*/
#if FS_MMC_SUPPORT_SD
  #define BUS_WIDTH_4BIT_SHIFT            2
  #define SCR_SD_SPEC(pSCR)               (U8)_GetBits(SEGGER_CONSTPTR2PTR(const U8, pSCR), 56, 59, NUM_BYTES_SCR)
  #define SCR_SD_BUS_WIDTHS(pSCR)         (U8)_GetBits(SEGGER_CONSTPTR2PTR(const U8, pSCR), 48, 51, NUM_BYTES_SCR)
#endif // #if FS_MMC_SUPPORT_MMC
#define SCR_SD_CMD23_SUPPORT(pSCR)        (U8)_GetBits(SEGGER_CONSTPTR2PTR(const U8, pSCR), 33, 33, NUM_BYTES_SCR)

/*********************************************************************
*
*       Types of write burst operations
*/
#define BURST_TYPE_NORMAL                 0u    // Sectors with different contents
#define BURST_TYPE_REPEAT                 1u    // Sectors with same content
#define BURST_TYPE_FILL                   2u    // Sectors with same content and the same 32-bit data pattern

/*********************************************************************
*
*       Maximum clock frequencies for MMC
*/
#define MAX_FREQ_MMC_DS_KHZ               20000uL
#define MAX_FREQ_MMC_HS_KHZ               52000uL
#define MAX_FREQ_MMC_HS_LEGACY_KHZ        26000uL
#if FS_MMC_SUPPORT_UHS
  #define MAX_FREQ_MMC_HS_DDR_KHZ         52000uL
  #define MAX_FREQ_MMC_HS400_KHZ          200000uL
  #define MAX_FREQ_MMC_HS200_KHZ          200000uL
#endif

/*********************************************************************
*
*       Statistics
*/
#if FS_MMC_ENABLE_STATS
  #define IF_STATS(Exp) Exp
#else
  #define IF_STATS(Exp)
#endif

/*********************************************************************
*
*       Local types
*
**********************************************************************
*/

//lint -esym(754, CSD_RESPONSE::Dummy, CID_RESPONSE::Dummy, CID_RESPONSE::aData) local struct member not referenced. Rationale: these members are used only for alignment.

/*********************************************************************
*
*       CSD_RESPONSE
*/
typedef struct {
  U8 Dummy;           // The HW layer stores here the command index (not used by the driver)
  U8 aData[16];       // CSD size is 127-bit wide including the 7-bit CRC
} CSD_RESPONSE;

/*********************************************************************
*
*       CID_RESPONSE
*/
typedef struct {
  U8 Dummy;           // The HW layer stores here the command index (not used by the driver)
  U8 aData[16];       // CSD size is 127-bit wide including the 7-bit CRC
} CID_RESPONSE;

/*********************************************************************
*
*       CARD_STATUS
*/
typedef struct {
  U8 aStatus[6];      // Card status answer is 48-bit wide
} CARD_STATUS;

/*********************************************************************
*
*       OCR_RESPONSE
*/
typedef struct {
  U8 aOCR[6];         // OCR answer is 48-bit wide
} OCR_RESPONSE;

/*********************************************************************
*
*       CMD_INFO
*/
typedef struct {
  U8  Index;          // Command index
  U8  IsAppCmd;       // Set to 1 for an application command
  U16 NextStateMask;  // The card is expected to transition to these states after the execution of this command (bit mask)
  U16 Flags;          // Command execution flags (see FS_MMC_CMD_FLAG_... in MMC_SD_CardMode_X_HW.h)
  U32 Arg;            // Command argument
} CMD_INFO;

/*********************************************************************
*
*       DATA_INFO
*/
typedef struct {
  U8     BusWidth;
  U16    BytesPerBlock;
  U32    NumBlocks;
  void * pBuffer;
} DATA_INFO;

/*********************************************************************
*
*       MMC_CM_INST
*/
typedef struct {
  const FS_MMC_HW_TYPE_CM * pHWType;                        // Routines for the hardware access.
  U32                       Freq_kHz;                       // Clock frequency supplied to the storage device.
  U32                       NumSectors;                     // Total number of logical sectors in the storage device.
  U32                       StartSector;                    // Index of the first logical sector to be used as storage.
  U32                       MaxNumSectors;                  // Limits the maximum number of logical sectors that can be used as storage.
#if FS_MMC_ENABLE_STATS
  FS_MMC_STAT_COUNTERS      StatCounters;                   // Statistical counters.
#endif // FS_MMC_ENABLE_STATS
  U16                       Rca;                            // Address that identifies the storage device on the bus.
  U16                       MaxReadBurst;                   // Maximum number of logical sectors that can be read at once.
  U16                       MaxWriteBurst;                  // Maximum number of logical sectors that can be written at once.
  U16                       MaxWriteBurstRepeat;            // Maximum number of logical sectors with identical data that can be written at once.
  U16                       MaxWriteBurstFill;              // Maximum number of logical sectors filled with the same 32-bit pattern that can be written at once.
  U16                       VoltageLevel;                   // Current voltage level of the I/O lines in mV.
  U8                        IsInited;                       // Set to 1 if the driver instance is initialized.
  U8                        Unit;                           // Index of the driver instance (0-based)
  U8                        HasError;                       // Set to 1 if an error occurred during the data exchange.
  U8                        CardType;                       // Type of the storage device (SD card or MMC device)
  U8                        BusWidth;                       // Number of data lines used for the data transfer.
  U8                        IsWriteProtected;               // Set to 1 if the data on the storage device cannot be changed.
  U8                        Is4bitModeAllowed;              // Set to 1 if the data transfer via 4 data lines is permitted.
  U8                        Is8bitModeAllowed;              // Set to 1 if the data transfer via 8 data lines is permitted.
  U8                        IsHSModeAllowed;                // Set to 1 if clock frequencies greater than 25 MHz for SD cards and 26 MHz for MMC devices are permitted.
  U8                        IsHighCapacity;                 // Set to 1 if the capacity of the storage device is >= 2 GBytes.
  U8                        IsHWInited;                     // Set to 1 if the hardware layer is initialized.
  U8                        AccessMode;                     // Current access mode.
  U8                        IsReliableWriteAllowed;         // Specified is using a fail-safe write operation is allowed for MMC devices.
  U8                        IsReliableWriteActive;          // Set to 1 if a fail-safe operation is used to write the data to an MMC device.
  U8                        IsBufferedWriteAllowed;         // Set to 1 if data can be send to storage device while a write operation is still in progress.
  U8                        IsCloseEndedRWSupported;        // Set to 1 if a data transfer does not have to be stopped using CMD12
#if FS_MMC_SUPPORT_MMC
  U8                        IsCacheActivationAllowed;       // Set to 1 if the data cache of an eMMC device can be enabled.
  U8                        IsCacheEnabled;                 // Set to 1 if the data cache of the eMMC device was enabled by the driver.
#endif // FS_MMC_SUPPORT_MMC
#if FS_MMC_SUPPORT_POWER_SAVE
#if FS_MMC_SUPPORT_MMC
  U8                        IsPowerSaveModeActive;          // Indicates if the MMC device is in low power mode.
#endif // FS_MMC_SUPPORT_MMC
  U8                        IsPowerSaveModeAllowed;         // Specifies if switching of MMC devices to low power mode is allowed.
#endif // FS_MMC_SUPPORT_POWER_SAVE
#if FS_MMC_SUPPORT_UHS
#if FS_MMC_SUPPORT_SD
  U8                        IsAccessModeDDR50Allowed;       // Specifies if using the DDR50 access mode for SD cards is allowed.
  U8                        IsAccessModeSDR50Allowed;       // Specifies if using the SDR50 access mode for SD cards is allowed.
  U8                        IsAccessModeSDR104Allowed;      // Specifies if using the SDR104 access mode for SD cards is allowed.
  U8                        IsSDR50TuningRequested;         // Specifies if the sampling point has to be tuned for the SDR50 access mode.
  U8                        IsSDR104TuningRequested;        // Specifies if the sampling point has to be tuned for the SDR104 access mode.
#endif // FS_MMC_SUPPORT_SD
#if FS_MMC_SUPPORT_MMC
  U8                        IsAccessModeHS_DDRAllowed;      // Specifies if using the High Speed DDR access mode for MMC devices is allowed.
  U8                        IsAccessModeHS200Allowed;       // Specifies if using the HS200 access mode for MMC devices is allowed.
  U8                        IsAccessModeHS400Allowed;       // Specifies if using the HS400 access mode for MMC devices is allowed.
  U8                        IsHS200TuningRequested;         // Specifies if the sampling point has to be tuned for the HS200 access mode.
  U8                        IsEnhancedStrobeAllowed;        // Specified if the enhanced strobe feature should be used for the HS400 access mode.
  U8                        IsEnhancedStrobeActive;         // Set to 1 if the data strobe in HS400 access mode is used for the data as well as for the response.
#endif // FS_MMC_SUPPORT_MMC
  U8                        IsVoltageLevel1V8Allowed;       // Specifies if using 1.8 V voltage level for the I/O lines is allowed.
  U8                        DriverStrengthRequested;        // Specifies the output driving strength configured by the application.
  U8                        DriverStrengthActive;           // Specifies the output driving strength in use. It can be different than DriverStrengthRequested if the MMC/SD device does not support it.
#endif // FS_MMC_SUPPORT_UHS
} MMC_CM_INST;

/*********************************************************************
*
*       Static const data
*
**********************************************************************
*/

/*********************************************************************
*
*       Communication frequency unit
*
*  Notes
*     (1) The values in the array are divided by 10 since the factor in _aFactorSD[]
*         and _aFactorMMC[] is multiplied by 10 to eliminate the fractional part.
*/
static const U16 _aUnit[8] = {
  10,     // 0: 100 kHz (not used acc. to SD and MMC standards)
  100,    // 1: 1 MHz   (not used)
  1000,   // 2: 10 MHz  (used for SD and MMC cards)
  10000,  // 3: 100 MHz (not used)
  1,      // 4: Reserved
  1,      // 5: Reserved
  1,      // 6: Reserved
  1       // 7: Reserved
};

/*********************************************************************
*
*       Communication speed factor for SD cards
*
*  Notes
*     (1) The values in the arrays are multiplied by 10 to eliminate the fractional part.
*/
static const U8 _aFactorSD[16] = {
  0,    // 0: reserved - not supported
  10,   // 1
  12,   // 2
  13,   // 3
  15,   // 4
  20,   // 5
  25,   // 6
  30,   // 7
  35,   // 8
  40,   // 9
  45,   // 10
  50,   // 11
  55,   // 12
  60,   // 13
  65,   // 14
  80    // 15
};

/*********************************************************************
*
*       Communication speed factor for MMC
*
*  Essentially the same as those in _aFactorSD[] with only two exceptions:
*
*     Value   SD      MMCplus
*     6       25kHz   26kHz
*     11      50kHz   52kHz
*
*  Notes
*     (1) The values in the arrays are multiplied by 10 to eliminate the fractional part.
*/
static const U8 _aFactorMMC[16] = {
  0,    // 0: reserved - not supported
  10,   // 1
  12,   // 2
  13,   // 3
  15,   // 4
  20,   // 5
  26,   // 6
  30,   // 7
  35,   // 8
  40,   // 9
  45,   // 10
  52,   // 11
  55,   // 12
  60,   // 13
  65,   // 14
  80    // 15
};

#if FS_MMC_SUPPORT_UHS

/*********************************************************************
*
*       Tuning data returned by SD cards and eMMC devices on an 4 bit bus
*/
static const U8 _abTuningBlock4Bit[NUM_BYTES_TUNING_BLOCK_4BIT] = {
  0xFFu, 0x0Fu, 0xFFu, 0x00u, 0xFFu, 0xCCu, 0xC3u, 0xCCu,
  0xC3u, 0x3Cu, 0xCCu, 0xFFu, 0xFEu, 0xFFu, 0xFEu, 0xEFu,
  0xFFu, 0xDFu, 0xFFu, 0xDDu, 0xFFu, 0xFBu, 0xFFu, 0xFBu,
  0xBFu, 0xFFu, 0x7Fu, 0xFFu, 0x77u, 0xF7u, 0xBDu, 0xEFu,
  0xFFu, 0xF0u, 0xFFu, 0xF0u, 0x0Fu, 0xFCu, 0xCCu, 0x3Cu,
  0xCCu, 0x33u, 0xCCu, 0xCFu, 0xFFu, 0xEFu, 0xFFu, 0xEEu,
  0xFFu, 0xFDu, 0xFFu, 0xFDu, 0xDFu, 0xFFu, 0xBFu, 0xFFu,
  0xBBu, 0xFFu, 0xF7u, 0xFFu, 0xF7u, 0x7Fu, 0x7Bu, 0xDEu
};

#if FS_MMC_SUPPORT_MMC

/*********************************************************************
*
*       Tuning data returned by eMMC devices on an 8 bit bus
*/
static const U8 _abTuningBlock8Bit[NUM_BYTES_TUNING_BLOCK_8BIT] = {
  0xFF, 0xFFu, 0x00, 0xFFu, 0xFF, 0xFFu, 0x00, 0x00u, 0xFF, 0xFFu, 0xCC, 0xCCu, 0xCC, 0x33u, 0xCC, 0xCCu,
  0xCC, 0x33u, 0x33, 0xCCu, 0xCC, 0xCCu, 0xFF, 0xFFu, 0xFF, 0xEEu, 0xFF, 0xFFu, 0xFF, 0xEEu, 0xEE, 0xFFu,
  0xFF, 0xFFu, 0xDD, 0xFFu, 0xFF, 0xFFu, 0xDD, 0xDDu, 0xFF, 0xFFu, 0xFF, 0xBBu, 0xFF, 0xFFu, 0xFF, 0xBBu,
  0xBB, 0xFFu, 0xFF, 0xFFu, 0x77, 0xFFu, 0xFF, 0xFFu, 0x77, 0x77u, 0xFF, 0x77u, 0xBB, 0xDDu, 0xEE, 0xFFu,
  0xFF, 0xFFu, 0xFF, 0x00u, 0xFF, 0xFFu, 0xFF, 0x00u, 0x00, 0xFFu, 0xFF, 0xCCu, 0xCC, 0xCCu, 0x33, 0xCCu,
  0xCC, 0xCCu, 0x33, 0x33u, 0xCC, 0xCCu, 0xCC, 0xFFu, 0xFF, 0xFFu, 0xEE, 0xFFu, 0xFF, 0xFFu, 0xEE, 0xEEu,
  0xFF, 0xFFu, 0xFF, 0xDDu, 0xFF, 0xFFu, 0xFF, 0xDDu, 0xDD, 0xFFu, 0xFF, 0xFFu, 0xBB, 0xFFu, 0xFF, 0xFFu,
  0xBB, 0xBBu, 0xFF, 0xFFu, 0xFF, 0x77u, 0xFF, 0xFFu, 0xFF, 0x77u, 0x77, 0xFFu, 0x77, 0xBBu, 0xDD, 0xEEu
};

#endif // FS_MMC_SUPPORT_MMC

#endif // FS_MMC_SUPPORT_UHS

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static MMC_CM_INST * _apInst[FS_MMC_NUM_UNITS];
static U8            _NumUnits = 0;

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _GetBits
*
*  Function description
*    Returns a value from the bit field.
*/
static U32 _GetBits(const U8 * pData, unsigned FirstBit, unsigned LastBit, unsigned NumBytesAvailable) {
  unsigned Off;
  unsigned OffLast;
  unsigned NumBytes;
  U32      Data;

  Off      = FirstBit / 8u;
  OffLast  = LastBit / 8u;
  NumBytes = (OffLast - Off) + 1u;
  Off      = (NumBytesAvailable - 1u) - OffLast;            // Bytes are reversed in CSD
  Data = 0;
  //
  // Read data into 32 bits
  //
  do {
    Data <<= 8;
    Data |= pData[Off++];
  } while (--NumBytes != 0u);
  //
  // Shift and mask result
  //
  Data >>= (FirstBit & 7u);
  Data &= (2uL << (LastBit - FirstBit)) - 1uL;              // Mask out bits that are outside of given bit range
  return Data;
}

/*********************************************************************
*
*       _GetFromCSD
*
*  Function description
*    Returns a value from the CSD field. These values are stored in
*    a 128 bit array; the bit-indices documented in [1]: 5.3 CSD register, page 69
*    can be used as parameters when calling the function
*/
static U32 _GetFromCSD(const CSD_RESPONSE * pCSD, unsigned FirstBit, unsigned LastBit) {
  U32 Data;

  Data = _GetBits(pCSD->aData, FirstBit, LastBit, sizeof(pCSD->aData));
  return Data;
}

/*********************************************************************
*
*       _CalcReadDataTimeOut
*
*  Function description
*    Computes the number of clock cycles the SD controller should
*    wait for the arrival of data.
*
*  Parameters
*    TimeOut    Configured timeout in ms.
*    ClockFreq  Frequency of the SD clock in kHz.
*
*  Return value
*    Number of clock cycles the SD controller has to wait.
*/
static U32 _CalcReadDataTimeOut(U32 TimeOut, U32 ClockFreq) {
  U32 NumClockCycles;

  NumClockCycles = DEFAULT_READ_DATA_TIMEOUT;
  if (TimeOut != 0u) {
    U32 nsPerClock;

    nsPerClock = 1000uL * 1000uL / ClockFreq;
    NumClockCycles = TimeOut * 1000uL * 1000uL / nsPerClock;
  }
  return NumClockCycles;
}

/*********************************************************************
*
*       _GetFreeMem
*
*  Function description
*    Allocates a temporarily buffer from the memory pool assigned to file system.
*/
static U32 * _GetFreeMem(U32 NumBytes) {
  U32 * p;
  I32   NumBytesFree;

  NumBytesFree = 0;
  p = SEGGER_PTR2PTR(U32, FS_GetFreeMem(&NumBytesFree));
  if (p != NULL) {
    if ((U32)NumBytesFree < NumBytes) {
      p = NULL;         // Error, could not allocate the requested number of bytes.
    }
  }
  return p;
}

/*********************************************************************
*
*       _AllocBuffer
*
*  Function description
*    Allocates a temporary buffer. The memory is allocated
*    either from a sector buffer or from the unused space
*    in the memory pool.
*/
static U32 * _AllocBuffer(unsigned NumBytes) {
  U32 * pBuffer;

  //
  // Try using a sector buffer if a file system is used.
  //
#if (FS_SUPPORT_FAT != 0) || (FS_SUPPORT_EFS != 0)
  pBuffer = NULL;
  if (FS_Global.MaxSectorSize >= NumBytes) {
    pBuffer = SEGGER_PTR2PTR(U32, FS__AllocSectorBuffer());
  }
  if (pBuffer == NULL)
#endif
  {
    //
    // No sector buffer available, try using un-allocated memory.
    //
    pBuffer = _GetFreeMem(NumBytes);
  }
  return pBuffer;
}

#if (FS_SUPPORT_FAT != 0) || (FS_SUPPORT_EFS != 0)

/*********************************************************************
*
*       _FreeBuffer
*
*  Function description
*    Frees the memory allocated for the temporary buffer.
*/
static void _FreeBuffer(U32 ** ppBuffer) {
  U32 * pBuffer;

  if (ppBuffer != NULL) {
    pBuffer = *ppBuffer;
    if (pBuffer != NULL) {
      FS__FreeSectorBuffer(pBuffer);
      pBuffer = NULL;
    }
    *ppBuffer = pBuffer;
  }
}

#endif // (FS_SUPPORT_FAT != 0) || (FS_SUPPORT_EFS != 0)

#if FS_MMC_SUPPORT_UHS

/*********************************************************************
*
*       _IsLowVoltageLevelAllowed
*
*  Function description
*    Verifies if switching to a voltage level lower than 3.3 V of
*    the I/O lines is permitted.
*/
static int _IsLowVoltageLevelAllowed(const MMC_CM_INST * pInst) {
  const FS_MMC_HW_TYPE_CM * pHWType;

  //
  // The hardware layer has to provide a function for switching
  // the voltage level of the I/O signals to 1.8 V
  //
  pHWType = pInst->pHWType;
  if (pHWType == NULL) {
    return 0;
  }
  if (pHWType->pfSetVoltage == NULL) {
    return 0;
  }
  //
  // The application has to explicitly request the activation of
  // the low voltage level via FS_MMC_CM_AllowVoltageLevel1V8().
  //
  if (pInst->IsVoltageLevel1V8Allowed != 0u) {
    return 1;
  }
  return 0;
}

#endif // FS_MMC_SUPPORT_UHS

/*********************************************************************
*
*       _InitHW
*/
static void _InitHW(const MMC_CM_INST * pInst) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWType->pfInitHW(Unit);
}

/*********************************************************************
*
*       _Delay
*/
static void _Delay(const MMC_CM_INST * pInst, int ms) {
  pInst->pHWType->pfDelay(ms);
}

/*********************************************************************
*
*       _IsPresent
*/
static int _IsPresent(const MMC_CM_INST * pInst) {
  U8  Unit;
  int Status;

  Unit = pInst->Unit;
  Status = pInst->pHWType->pfIsPresent(Unit);
  return Status;
}

/*********************************************************************
*
*       _IsWriteProtected
*/
static int _IsWriteProtected(const MMC_CM_INST * pInst) {
  U8  Unit;
  int Status;

  Unit = pInst->Unit;
  Status = pInst->pHWType->pfIsWriteProtected(Unit);
  return Status;
}

/*********************************************************************
*
*       _SetMaxSpeed
*/
static U32 _SetMaxSpeed(const MMC_CM_INST * pInst, U32 Freq_kHz, unsigned ClkFlags) {
  U8 Unit;

  Unit = pInst->Unit;
  if (pInst->pHWType->pfSetMaxClock != NULL) {
    Freq_kHz = pInst->pHWType->pfSetMaxClock(Unit, Freq_kHz, ClkFlags);
  } else {
    Freq_kHz = pInst->pHWType->pfSetMaxSpeed(Unit, (U16)Freq_kHz);
  }
  return Freq_kHz;
}

/*********************************************************************
*
*       _SetResponseTimeOut
*/
static void _SetResponseTimeOut(const MMC_CM_INST * pInst, U32 Value) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWType->pfSetResponseTimeOut(Unit, Value);
}

/*********************************************************************
*
*       _SetReadDataTimeOut
*/
static void _SetReadDataTimeOut(const MMC_CM_INST * pInst, U32 Value) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWType->pfSetReadDataTimeOut(Unit, Value);
}

/*********************************************************************
*
*       _SendCmd
*/
static void _SendCmd(MMC_CM_INST * pInst, const CMD_INFO * pCmdInfo, unsigned ResponseType) {   //lint -efunc(818, _SendCmd) Pointer parameter 'pInst' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory]. Rationale: Not possible because we have to be able to update the statistical counters in debug builds.
  U8       Unit;
  unsigned Index;
  unsigned Flags;
  U32      Arg;

  Unit  = pInst->Unit;
  Index = pCmdInfo->Index;
  Flags = pCmdInfo->Flags;
  Arg   = pCmdInfo->Arg;
  pInst->pHWType->pfSendCmd(Unit, Index, Flags, ResponseType, Arg);
  IF_STATS(pInst->StatCounters.CmdExecCnt++);
}

/*********************************************************************
*
*       _GetResponse
*
*  Function description
*    Reads the response data from SD controller.
*
*  Return value
*    ==0    OK, response received
*    !=0    An error occurred
*/
static int _GetResponse(const MMC_CM_INST * pInst, void * pBuffer, U32 Size) {
  int r;
  U8  Unit;

  Unit = pInst->Unit;
  r = pInst->pHWType->pfGetResponse(Unit, pBuffer, Size);
  return r;
}

/*********************************************************************
*
*       _ReadData
*/
static int _ReadData(const MMC_CM_INST * pInst, const DATA_INFO * pDataInfo) {
  int        r;
  U8         Unit;
  void     * pBuffer;
  unsigned   BytesPerBlock;
  U32        NumBlocks;

  Unit          = pInst->Unit;
  BytesPerBlock = pDataInfo->BytesPerBlock;
  NumBlocks     = pDataInfo->NumBlocks;
  pBuffer       = pDataInfo->pBuffer;
  r = pInst->pHWType->pfReadData(Unit, pBuffer, BytesPerBlock, NumBlocks);
  return r;
}

/*********************************************************************
*
*       _WriteData
*/
static int _WriteData(const MMC_CM_INST * pInst, const DATA_INFO * pDataInfo) {
  int        r;
  U8         Unit;
  void     * pBuffer;
  unsigned   BytesPerBlock;
  U32        NumBlocks;

  Unit          = pInst->Unit;
  BytesPerBlock = pDataInfo->BytesPerBlock;
  NumBlocks     = pDataInfo->NumBlocks;
  pBuffer       = pDataInfo->pBuffer;
  r = pInst->pHWType->pfWriteData(Unit, pBuffer, BytesPerBlock, NumBlocks);
  return r;
}

/*********************************************************************
*
*       _SetDataPointer
*/
static void _SetDataPointer(const MMC_CM_INST * pInst, const void * p) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWType->pfSetDataPointer(Unit, p);
}

/*********************************************************************
*
*       _SetBlockLen
*/
static void _SetBlockLen(const MMC_CM_INST * pInst, U16 BlockSize) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWType->pfSetHWBlockLen(Unit, BlockSize);
}

/*********************************************************************
*
*       _SetNumBlocks
*/
static void _SetNumBlocks(const MMC_CM_INST * pInst, U16 NumBlocks) {
  U8 Unit;

  Unit = pInst->Unit;
  pInst->pHWType->pfSetHWNumBlocks(Unit, NumBlocks);
}

/*********************************************************************
*
*       _GetMaxReadBurst
*/
static U16 _GetMaxReadBurst(const MMC_CM_INST * pInst) {
  U16 r;
  U8  Unit;

  Unit = pInst->Unit;
  r = pInst->pHWType->pfGetMaxReadBurst(Unit);
  return r;
}

/*********************************************************************
*
*       _GetMaxWriteBurst
*/
static U16 _GetMaxWriteBurst(const MMC_CM_INST * pInst) {
  U16 r;
  U8  Unit;

  Unit = pInst->Unit;
  r = pInst->pHWType->pfGetMaxWriteBurst(Unit);
  return r;
}

/*********************************************************************
*
*       _GetMaxWriteBurstRepeat
*/
static U16 _GetMaxWriteBurstRepeat(const MMC_CM_INST * pInst) {
  U16 r;
  U8  Unit;

  r = 0;                  // Set to indicate that the feature is not supported.
  Unit = pInst->Unit;
  if (pInst->pHWType->pfGetMaxWriteBurstRepeat != NULL) {
    r = pInst->pHWType->pfGetMaxWriteBurstRepeat(Unit);
  }
  return r;
}

/*********************************************************************
*
*       _GetMaxWriteBurstFill
*/
static U16 _GetMaxWriteBurstFill(const MMC_CM_INST * pInst) {
  U16 r;
  U8  Unit;

  r = 0;                  // Set to indicate that the feature is not supported.
  Unit = pInst->Unit;
  if (pInst->pHWType->pfGetMaxWriteBurstFill != NULL) {
    r = pInst->pHWType->pfGetMaxWriteBurstFill(Unit);
  }
  return r;
}

#if FS_MMC_SUPPORT_UHS

/*********************************************************************
*
*       _SetVoltage
*/
static int _SetVoltage(const MMC_CM_INST * pInst, U16 VMin, U16 VMax, int IsSDCard) {
  int r;
  U8  Unit;

  r = 1;                  // Set to indicate error.
  Unit = pInst->Unit;
  if (pInst->pHWType->pfSetVoltage != NULL) {
    r = pInst->pHWType->pfSetVoltage(Unit, VMin, VMax, IsSDCard);
  }
  return r;
}

/*********************************************************************
*
*       _GetVoltage
*/
static U16 _GetVoltage(const MMC_CM_INST * pInst) {
  U16 r;
  U8  Unit;

  r = 0;                  // Set to indicate error.
  Unit = pInst->Unit;
  if (pInst->pHWType->pfGetVoltage != NULL) {
    r = pInst->pHWType->pfGetVoltage(Unit);
  }
  return r;
}

/*********************************************************************
*
*       _EnableTuning
*/
static int _EnableTuning(const MMC_CM_INST * pInst) {
  U16 r;
  U8  Unit;

  r    = 0;
  Unit = pInst->Unit;
  if (pInst->pHWType->pfEnableTuning != NULL) {
    r = pInst->pHWType->pfEnableTuning(Unit);
  }
  return r;
}

/*********************************************************************
*
*       _DisableTuning
*/
static int _DisableTuning(const MMC_CM_INST * pInst, int IsError) {
  U16 r;
  U8  Unit;

  r    = 0;
  Unit = pInst->Unit;
  if (pInst->pHWType->pfDisableTuning != NULL) {
    r = pInst->pHWType->pfDisableTuning(Unit, IsError);
  }
  return r;
}

/*********************************************************************
*
*       _StartTuning
*/
static int _StartTuning(const MMC_CM_INST * pInst, unsigned Step) {
  U16 r;
  U8  Unit;

  r    = 0;
  Unit = pInst->Unit;
  if (pInst->pHWType->pfStartTuning != NULL) {
    r = pInst->pHWType->pfStartTuning(Unit, Step);
  }
  return r;
}

/*********************************************************************
*
*       _GetMaxTunings
*/
static U16 _GetMaxTunings(const MMC_CM_INST * pInst) {
  U16 r;
  U8  Unit;

  r    = 0;
  Unit = pInst->Unit;
  if (pInst->pHWType->pfGetMaxTunings != NULL) {
    r = pInst->pHWType->pfGetMaxTunings(Unit);
  }
  return r;
}

/*********************************************************************
*
*       _IsTuningSupported
*/
static int _IsTuningSupported(const MMC_CM_INST * pInst) {
  int r;

  r = 0;
  if (pInst->pHWType->pfEnableTuning != NULL) {
    r = 1;
  }
  return r;
}

#endif // FS_MMC_SUPPORT_UHS

/*********************************************************************
*
*      _InitHWIfRequired
*/
static void _InitHWIfRequired(MMC_CM_INST * pInst) {
  if (pInst->IsHWInited == 0u) {
    _InitHW(pInst);
    pInst->IsHWInited = 1;
  }
}

/*********************************************************************
*
*      _GetCardErrors
*
*  Function description
*    Returns the error flags from the card status.
*/
static U32 _GetCardErrors(const CARD_STATUS * pCardStatus) {
  U32 ErrorFlags;

  ErrorFlags  = FS_LoadU32BE(&pCardStatus->aStatus[1]);
  ErrorFlags &= STATUS_ERROR_MASK;
  return ErrorFlags;
}

/*********************************************************************
*
*      _GetCardCurrentState
*
*  Function description
*    Returns the current state of the card.
*/
static unsigned _GetCardCurrentState(const CARD_STATUS * pCardStatus) {
  unsigned CurrentState;

  CurrentState  = (unsigned)pCardStatus->aStatus[3] >> 1;     // The CURRENT_STATE field of card status is stored in bits 9-12.
  CurrentState &= CARD_STATE_MASK;
  return CurrentState;
}

/*********************************************************************
*
*      _IsCardReady
*
*  Function description
*    Checks if the card is ready to accept a new command.
*/
static int _IsCardReady(const CARD_STATUS * pCardStatus) {
  int r;

  r = 0;  // The card is not ready, yet.
  if ((pCardStatus->aStatus[3] & 1u) != 0u) {                 // Check the READY_FOR_DATA flag (bit 8 in card status)
    r = 1;
  }
  return r;
}

#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ALL

/*********************************************************************
*
*      _IsAppCmd
*
*  Function description
*    Checks if the card is expecting an advanced command.
*/
static int _IsAppCmd(const CARD_STATUS * pCardStatus) {
  int r;

  r = 0;  // The card does not expect an advanced command.
  if (pCardStatus != NULL) {
    if ((pCardStatus->aStatus[4] & 0x20u) != 0u) {            // Check the APP_CMD flag (bit 5 in card status)
      r = 1;
    }
  }
  return r;
}

#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS

/*********************************************************************
*
*       _IsHighCapacityCard
*
*  Function description
*    Checks for high capacity (> 2GB) card.
*
*  Return value
*    ==1    High capacity card
*    ==0    Standard capacity card
*/
static int _IsHighCapacityCard(const OCR_RESPONSE * pOCR) {
  int r;

  r = 0;      // High capacity is not supported.
  if ((pOCR->aOCR[1] & (1u << OCR_CCS_SHIFT)) != 0u) {
    r = 1;    // High capacity (> 2GB) card.
  }
  return r;
}

#if FS_MMC_SUPPORT_MMC

/*********************************************************************
*
*       _IsDualVoltageDevice
*
*  Function description
*    Checks if the MMC device supports 1.8 V signaling.
*
*  Return value
*    ==1    Device supports 1.8 V signaling.
*    ==0    Device does not support 1.8 V signaling.
*/
static int _IsDualVoltageDevice(const OCR_RESPONSE * pOCR) {
  int r;

  r = 0;      // Not a dual voltage device.
  if ((pOCR->aOCR[4] & (1u << OCR_1V7_1V9_SHIFT)) != 0u) {
    r = 1;    // This is a dual voltage device.
  }
  return r;
}

#endif // FS_MMC_SUPPORT_MMC

#if FS_MMC_SUPPORT_SD

/*********************************************************************
*
*       _Is1V8Accepted
*
*  Function description
*    Checks if the SD card can use 1.8 V signaling.
*
*  Return value
*    ==1    Can use 1.8 V signaling.
*    ==0    Cannot use 1.8 V signaling.
*/
static int _Is1V8Accepted(const OCR_RESPONSE * pOCR) {
  int r;

  r = 0;      // High capacity is not supported.
  if ((pOCR->aOCR[1] & (1u << OCR_S18A_SHIFT)) != 0u) {
    r = 1;    // High capacity (> 2GB) card.
  }
  return r;
}

#endif // FS_MMC_SUPPORT_SD

/*********************************************************************
*
*       _IsCardPoweredUp
*
*  Function description
*    Checks if the power-up process of the card has finished.
*
*  Return value
*    ==1    Card is powered-up
*    ==0    Power-up procedure still running
*/
static int _IsCardPoweredUp(const OCR_RESPONSE * pOCR) {
  int r;

  r = 0;      // Power up in progress.
  if ((pOCR->aOCR[1] & (1u << OCR_READY_SHIFT)) != 0u) {
    r = 1;    // Power up sequence is finished.
  }
  return r;
}

/*********************************************************************
*
*      _IsCardLocked
*
*  Function description
*    Checks in the card status if the card is locked.
*/
static int _IsCardLocked(const CARD_STATUS * pCardStatus) {
  U32 CardStatus;

  CardStatus = FS_LoadU32BE(&pCardStatus->aStatus[1]);
  if ((CardStatus & (1uL << STATUS_CARD_IS_LOCKED_SHIFT)) != 0u) {
    return 1;       // Card is locked.
  }
  return 0;         // Card is not locked.
}

/*********************************************************************
*
*      _ExecCmd
*
*  Function description
*    Executes a command that do not expects a response.
*/
static void _ExecCmd(MMC_CM_INST * pInst, const CMD_INFO * pCmdInfo) {
  _SendCmd(pInst, pCmdInfo, FS_MMC_RESPONSE_FORMAT_NONE);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: CMD%d Flg: 0x%x, Arg: 0x%x\n", (int)pCmdInfo->Index,
                                                                         pCmdInfo->Flags,
                                                                         pCmdInfo->Arg));
}

/*********************************************************************
*
*       _ExecCmdR1
*
*  Function description
*    Executes the command that expects an R1 response.
*
*  Return value
*    ==0    OK, command executed successfully
*    > 0    Error, host controller reports a failure
*    < 0    Error, card reports a failure or it has been removed
*
*  Notes
*      The R1 response is returned in pCardStatus and it has the following format:
*      Bit range  Stored to             Description
*      --------------------------------------------
*      47         pCardStatus[0].7      Start bit
*      46         pCardStatus[0].6      Transmission bit
*      40-45      pCardStatus[0].0-5    Command index
*      39         pCardStatus[1].7      OUT_OF_RANGE
*      38         pCardStatus[1].6      ADDRESS_ERROR
*      37         pCardStatus[1].5      BLOCK_LEN_ERROR
*      36         pCardStatus[1].4      ERASE_SEQ_ERROR
*      35         pCardStatus[1].3      ERASE_PARAM
*      34         pCardStatus[1].2      WP_VIOLATION
*      33         pCardStatus[1].1      CARD_IS_LOCKED
*      32         pCardStatus[1].0      LOCK_UNLOCK_FAILED
*      31         pCardStatus[2].7      COM_CRC_ERROR
*      30         pCardStatus[2].6      ILLEGAL_COMMAND
*      29         pCardStatus[2].5      CARD_ECC_FAILED
*      28         pCardStatus[2].4      CC_ERROR
*      27         pCardStatus[2].3      ERROR
*      26         pCardStatus[2].2      UNDERRUN  (only for MMC cards)
*      25         pCardStatus[2].1      OVERRUN   (only for MMC cards)
*      24         pCardStatus[2].0      CSD_OVERWRITE
*      23         pCardStatus[3].7      WP_ERASE_SKIP
*      22         pCardStatus[3].6      CARD_ECC_DISABLED
*      21         pCardStatus[3].5      ERASE_RESET
*      17-20      pCardStatus[3].1-4    CURRENT_STATE
*      16         pCardStatus[3].0      READY_FOR_DATA
*      14-15      pCardStatus[4].6-7    Reserved
*      13         pCardStatus[4].5      APP_CMD
*      12         pCardStatus[4].4      Reserved
*      11         pCardStatus[4].3      AKE_SEQ_ERROR
*      8-10       pCardStatus[4].0-2    Reserved
*      1-7        pCardStatus[5].1-7    CRC
*      0          pCardStatus[5].0      End bit
*/
static int _ExecCmdR1(MMC_CM_INST * pInst, const CMD_INFO * pCmdInfo, CARD_STATUS * pCardStatus, int NumRetries) {
  int r;
  U32 CardErrors;

  FS_MEMSET(pCardStatus, 0, sizeof(*pCardStatus));
  for (;;) {
    _SendCmd(pInst, pCmdInfo, FS_MMC_RESPONSE_FORMAT_R1);
    r = _GetResponse(pInst, pCardStatus, sizeof(*pCardStatus));
    FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: %s%d Flg: 0x%x, Arg: 0x%x, ", pCmdInfo->IsAppCmd != 0u ? "ACMD" : "CMD",
                                                                          (int)pCmdInfo->Index,
                                                                          pCmdInfo->Flags,
                                                                          pCmdInfo->Arg));
    FS_DEBUG_LOG((FS_MTYPE_DRIVER, "Res: %d, Err: 0x%x, Stat: %d, "
                                   "ACmd: %d, Rdy: %d, Rtry: %d\n", r,
                                                                    _GetCardErrors(pCardStatus),
                                                                    _GetCardCurrentState(pCardStatus),
                                                                    _IsAppCmd(pCardStatus),
                                                                    _IsCardReady(pCardStatus),
                                                                    NumRetries));
    if (r == 0) {
      CardErrors = _GetCardErrors(pCardStatus);
      if (CardErrors == 0u) {
        r = 0;              // OK, command completed successfully.
        break;
      }
    }
    if (_IsPresent(pInst) == 0) {
      r = -1;
      FS_DEBUG_WARN((FS_MTYPE_DRIVER, "MMC_CM: _ExecCmdR1: The card has been removed."));
      break;                // The card has been removed.
    }
    if (NumRetries == 0) {
      r = -1;
      break;                // Error, could not send command.
    }
    --NumRetries;
  }
  return r;
}

/*********************************************************************
*
*      _ExecCmdR2
*
*  Function description
*    Sends a command and receives an R2 format response.
*
*  Return value
*    ==0    OK, command executed successfully
*    > 0    Error, host controller reports a failure
*    < 0    Error, card reports a failure or it has been removed
*/
static int _ExecCmdR2(MMC_CM_INST * pInst, const CMD_INFO * pCmdInfo, void * pData, int NumRetries) {
  int r;

  FS_MEMSET(pData, 0, NUM_BYTES_R2);
  for (;;) {
    _SendCmd(pInst, pCmdInfo, FS_MMC_RESPONSE_FORMAT_R2);
    r = _GetResponse(pInst, pData, NUM_BYTES_R2);
    FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: CMD%d Flg: 0x%x, Arg: 0x%x, Res: %d\n", (int)pCmdInfo->Index,
                                                                                    pCmdInfo->Flags,
                                                                                    pCmdInfo->Arg,
                                                                                    r));
    if (r == 0) {
      break;                // OK, command completed successfully.
    }
    if (_IsPresent(pInst) == 0) {
      r = -1;
      FS_DEBUG_WARN((FS_MTYPE_DRIVER, "MMC_CM: _ExecCmdR2: The card has been removed."));
      break;                // The card has been removed.
    }
    if (NumRetries == 0) {
      r = -1;
      break;                // Error, could not send command.
    }
    --NumRetries;
  }
  return r;
}

/*********************************************************************
*
*       _ExecCmdR3
*
*  Function description
*    Sends a regular command and receives an R3 response.
*
*  Parameters
*    pInst        Driver instance.
*    pCmdInfo     Information about the command to be executed.
*    pOCR         [OUT] Contents of the OCR register.
*    NumRetries   Maximum number of command execution retries in case of an error.
*
*  Return value
*    ==0    OK, command executed successfully.
*    > 0    Error, host controller reports a failure.
*    < 0    Error, card reports a failure or it has been removed.
*
*  Additional information
*      The R3 response is returned in pOCR and it has the following format:
*      Bit range  Stored to       Description
*      ------------------------------------------
*      47         pOCR[0].7       Start bit
*      46         pOCR[0].6       Transmission bit
*      40-45      pOCR[0].0-5     Reserved
*      39         pOCR[1].7       Card ready
*      38         pOCR[1].6       High capacity card (> 2GB)
*      32-37      pOCR[1].0-5     Reserved
*      24-31      pOCR[2].0-7     Voltage range: 2.8V-3.6V
*      23         pOCR[3].7       Voltage range: 2.7V-2.8V
*      8-22       pOCR[3].6-      Reserved
*                 pOCR[4].7
*      1-7        pOCR[5].1-7     CRC
*      0          pOCR[5].0       End bit
*/
static int _ExecCmdR3(MMC_CM_INST * pInst, const CMD_INFO * pCmdInfo, OCR_RESPONSE * pOCR, int NumRetries) {
  int r;

  FS_MEMSET(pOCR, 0, sizeof(*pOCR));
  for (;;) {
    _SendCmd(pInst, pCmdInfo, FS_MMC_RESPONSE_FORMAT_R3);
    r = _GetResponse(pInst, pOCR, sizeof(*pOCR));
    FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: %s%d Flg: 0x%x, Arg: 0x%x, ", pCmdInfo->IsAppCmd != 0u ? "ACMD" : "CMD",
                                                                          (int)pCmdInfo->Index,
                                                                          pCmdInfo->Flags,
                                                                          pCmdInfo->Arg));
    FS_DEBUG_LOG((FS_MTYPE_DRIVER, "Res: %d, OCR: %02x %02x %02x %02x %02x %02x\n", r,
                                                                                    pOCR->aOCR[0],
                                                                                    pOCR->aOCR[1],
                                                                                    pOCR->aOCR[2],
                                                                                    pOCR->aOCR[3],
                                                                                    pOCR->aOCR[4],
                                                                                    pOCR->aOCR[5]));
    if (r == 0) {
      break;                // OK, command completed successfully.
    }
    if (_IsPresent(pInst) == 0) {
      r = -1;
      FS_DEBUG_WARN((FS_MTYPE_DRIVER, "MMC_CM: _ExecCmdR3: The card has been removed."));
      break;                // The card has been removed.
    }
    if (NumRetries == 0) {
      r = -1;
      break;                // Error, could not send command.
    }
    --NumRetries;
  }
  return r;
}

#if FS_MMC_SUPPORT_SD

/*********************************************************************
*
*       _ExecCmdR6
*
*  Function description
*    Sends a regular command and receives an R3 response.
*
*  Parameters
*    pInst        Driver instance.
*    pCmdInfo     Information about the command to be executed.
*    pResData     [OUT] Response data.
*
*  Return value
*    ==0    OK, command executed successfully.
*    > 0    Error, host controller reports a failure.
*
*  Additional information
*      The R6 response is returned in pResponse and it has the following format:
*      Bit range  Stored to           Description
*      ------------------------------------------
*      47         pResData[0].7       Start bit
*      46         pResData[0].6       Transmission bit
*      40-45      pResData[0].0-5     Command index
*      24-39      pResData[1].0       New published RCA
*      23         pResData[3].7       COM_CRC_ERROR
*      22         pResData[3].6       ILLEGAL_COMMAND
*      21         pResData[3].5       ERROR
*      17-20      pResData[3].1-4     CURRENT_STATE
*      16         pResData[3].0       READY_FOR_DATA
*      14-15      pResData[4].6-7     Reserved
*      13         pResData[4].5       APP_CMD
*      12         pResData[4].4       Reserved
*      11         pResData[4].3       AKE_SEQ_ERROR
*      8-10       pResData[4].0-2     Reserved
*      1-7        pResData[5].1-7     CRC
*      0          pResData[5].0       End bit
*/
static int _ExecCmdR6(MMC_CM_INST * pInst, const CMD_INFO * pCmdInfo, U8 * pResData) {
  int r;

  FS_MEMSET(pResData, 0, NUM_BYTES_R6);
  _SendCmd(pInst, pCmdInfo, FS_MMC_RESPONSE_FORMAT_R6);
  r = _GetResponse(pInst, pResData, NUM_BYTES_R6);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: CMD%d Flg: 0x%x, Arg: 0x%x, ", (int)pCmdInfo->Index,
                                                                         pCmdInfo->Flags,
                                                                         pCmdInfo->Arg));
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "Res: %d, Data: %02x %02x %02x %02x %02x %02x\n", r,
                                                                                   pResData[0],
                                                                                   pResData[1],
                                                                                   pResData[2],
                                                                                   pResData[3],
                                                                                   pResData[4],
                                                                                   pResData[5]));
  return r;
}

/*********************************************************************
*
*       _ExecCmdR7
*
*  Function description
*    Sends a command that expects an R7 response.
*
*  Parameters
*    pInst        Driver instance.
*    pCmdInfo     Information about the command to be executed.
*    pResData     [OUT] Response data.
*
*  Return value
*    ==0    OK, command executed successfully.
*    > 0    Error, host controller reports a failure.
*
*  Additional information
*      The R7 response is returned in pResData and it has the following format:
*      Bit range  Stored to          Description
*      -----------------------------------------
*      47         pResData[0].7      Start bit
*      46         pResData[0].6      Transmission bit
*      40-45      pResData[0].0-5    Command index
*      20-39      pResData[1].0-     Reserved
*                 pResData[3].4
*      16-19      pResData[3].0-3    Voltage accepted
*      8-15       pResData[4]        Echo-back of check pattern
*      1-7        pResData[5].1-7    CRC
*      0          pResData[5].0      End bit
*/
static int _ExecCmdR7(MMC_CM_INST * pInst, const CMD_INFO * pCmdInfo, U8 * pResData) {
  int r;

  FS_MEMSET(pResData, 0, NUM_BYTES_R7);
  _SendCmd(pInst, pCmdInfo, FS_MMC_RESPONSE_FORMAT_R7);
  r = _GetResponse(pInst, pResData, NUM_BYTES_R7);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: CMD%d Flg: 0x%x, Arg: 0x%x, ", (int)pCmdInfo->Index,
                                                                         pCmdInfo->Flags,
                                                                         pCmdInfo->Arg));
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "Res: %d, Data: %02x %02x %02x %02x %02x %02x\n", r,
                                                                                   pResData[0],
                                                                                   pResData[1],
                                                                                   pResData[2],
                                                                                   pResData[3],
                                                                                   pResData[4],
                                                                                   pResData[5]));
  return r;
}

#endif  // FS_MMC_SUPPORT_SD

/*********************************************************************
*
*       _ExecAppCmdR1
*
*  Function description
*    Sends an advanced command and receives an R1 response.
*
*  Return value
*    ==0    OK, command executed successfully.
*    > 0    Error, host controller reports a failure.
*    < 0    Error, card reports a failure or it has been removed.
*/
static int _ExecAppCmdR1(MMC_CM_INST * pInst, const CMD_INFO * pCmdInfo, CARD_STATUS * pCardStatus, int NumRetries) {
  int      r;
  CMD_INFO CmdInfo;

  FS_MEMSET(&CmdInfo, 0, sizeof(CmdInfo));
  CmdInfo.Index = CMD_APP_CMD;
  CmdInfo.Arg   = (U32)pInst->Rca << ARG_RCA_SHIFT;
  r = _ExecCmdR1(pInst, &CmdInfo, pCardStatus, NUM_RETRIES_CMD);
  if (r == 0) {
    r = _ExecCmdR1(pInst, pCmdInfo, pCardStatus, NumRetries);
  }
  return r;
}

#if FS_MMC_SUPPORT_SD

/*********************************************************************
*
*       _ExecAppCmdR3
*
*  Function description
*    Sends an advanced command and receives an R3 response.
*
*  Return value
*    ==0    OK, command executed successfully
*    > 0    Error, host controller reports a failure
*    < 0    Error, card reports a failure or it has been removed
*/
static int _ExecAppCmdR3(MMC_CM_INST * pInst, const CMD_INFO * pCmdInfo, OCR_RESPONSE * pOCR, CARD_STATUS * pCardStatus, int NumRetries) {
  int      r;
  CMD_INFO CmdInfo;

  FS_MEMSET(pOCR, 0, sizeof(*pOCR));
  FS_MEMSET(&CmdInfo, 0, sizeof(CmdInfo));
  CmdInfo.Index = CMD_APP_CMD;
  CmdInfo.Arg   = (U32)pInst->Rca << ARG_RCA_SHIFT;
  r = _ExecCmdR1(pInst, &CmdInfo, pCardStatus, NUM_RETRIES_CMD);
  if (r == 0) {
    r = _ExecCmdR3(pInst, pCmdInfo, pOCR, NumRetries);
  }
  return r;
}

#endif  // FS_MMC_SUPPORT_SD

/*********************************************************************
*
*      _ExecCmdR1WithStateTransition
*
*  Function description
*    Sends a command that expects an R1 response. The command makes
*    the card move to an other internal state.
*
*  Parameters
*    pInst            Driver instance.
*    pCmdInfo         Information about the command to be executed.
*    pCardStatus      Status of the card returned in R1 response.
*
*  Return value
*    ==0    OK, command executed successfully.
*    > 0    Error, host controller reports a failure.
*    < 0    Error, card reports a failure or it has been removed.
*/
static int _ExecCmdR1WithStateTransition(MMC_CM_INST * pInst, const CMD_INFO * pCmdInfo, CARD_STATUS * pCardStatus) {
  int      r;
  unsigned StateCurrent;
  U32      CardErrors;
  int      NumRetries;
  CMD_INFO CmdInfo;

  NumRetries = NUM_RETRIES_CMD;
  //
  // Loop for command retry.
  //
  for (;;) {
    if (pCmdInfo->IsAppCmd != 0u) {
      r = _ExecAppCmdR1(pInst, pCmdInfo, pCardStatus, 0);       // Do not perform any command retries in _ExecAppCmdR1().
    } else {
      r = _ExecCmdR1(pInst, pCmdInfo, pCardStatus, 0);          // Do not perform any command retries in _ExecCmdR1().
    }
    if (r < 0) {
      break;                                                    // Quit the retry loop if the card has been removed.
    }
    if (r > 0) {
      //
      // In case of a communication error, read the card status to check if we can recover from this error.
      //
      FS_MEMSET(&CmdInfo, 0, sizeof(CmdInfo));
      CmdInfo.Index = CMD_SEND_STATUS;
      CmdInfo.Arg   = (U32)pInst->Rca << ARG_RCA_SHIFT;
      r = _ExecCmdR1(pInst, &CmdInfo, pCardStatus, 0);          // Do not perform any command retries in _ExecCmdR1().
      if (r < 0) {
        break;              // Quit the retry loop if the card has been removed.
      }
    }
    if (r == 0) {
      CardErrors = _GetCardErrors(pCardStatus);
      if (CardErrors == 0u) {
        r = 0;              // OK, command successfully executed. The card is now in Sending-data State.
        //
        // On higher debug levels check if the card moved to the requested command.
        //
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL)
        if (pCmdInfo->NextStateMask != 0u) {                    // Is at least one state specified?
          FS_MEMSET(&CmdInfo, 0, sizeof(CmdInfo));
          CmdInfo.Index = CMD_SEND_STATUS;
          CmdInfo.Arg   = (U32)pInst->Rca << ARG_RCA_SHIFT;
          r = _ExecCmdR1(pInst, &CmdInfo, pCardStatus, 0);      // Do not perform any command retries in _ExecCmdR1().
          if (r == 0) {
            StateCurrent = _GetCardCurrentState(pCardStatus);
            if (((1uL << StateCurrent) & pCmdInfo->NextStateMask) == 0u) {
              FS_DEBUG_WARN((FS_MTYPE_DRIVER, "MMC_CM: _ExecCmdR1WithStateTransition: Card did not switch to 0x%x. Current state is %d.", pCmdInfo->NextStateMask, StateCurrent));
              r = -1;       // Error, card did not report any error and it did not moved to requested state.
            }
          }
        }
#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
        break;
      }
    }
    //
    // Check if the card accepted the command.
    //
    if (pCmdInfo->NextStateMask != 0u) {                        // Is at least one state specified?
      StateCurrent = _GetCardCurrentState(pCardStatus);
      if (((1uL << StateCurrent) & pCmdInfo->NextStateMask) != 0u) {
        r = 0;
        break;                // OK, the card switched to requested state.
      }
    }
    FS_DEBUG_WARN((FS_MTYPE_DRIVER, "MMC_CM: _ExecCmdR1WithStateTransition: Could not execute command. %d retries left.", NumRetries));
    if (NumRetries == 0) {
      r = -1;               // Error, the card does not respond.
      break;
    }
    --NumRetries;
  }
  return r;
}

static int _StopTransmissionIfRequired(MMC_CM_INST * pInst, CARD_STATUS * pCardStatus);     // Forward declaration

/*********************************************************************
*
*      _ExecCmdR1WithDataRead
*
*  Function description
*    Sends a command that expects an R1 response and reads data from the card.
*
*  Parameters
*    pInst            Driver instance.
*    pCmdInfo         Information about the command to be executed.
*    pDataInfo        Information about the data to be read from card.
*    pCardStatus      Status of the card returned in R1 response.
*    NumRetries       Maximum number of times to retry the command execution in case of a failure.
*
*  Return value
*    ==0    OK, command executed successfully.
*    > 0    Error, host controller reports a failure.
*    < 0    Error, card reports a failure or it has been removed.
*/
static int _ExecCmdR1WithDataRead(MMC_CM_INST * pInst, CMD_INFO * pCmdInfo, const DATA_INFO * pDataInfo, CARD_STATUS * pCardStatus, int NumRetries) {
  int      r;
  int      rStop;
  unsigned CmdFlags;
  int      BusWidth;

  BusWidth = (int)pDataInfo->BusWidth;
  if (BusWidth == 0) {
    BusWidth = (int)pInst->BusWidth;
  }
  CmdFlags = FS_MMC_CMD_FLAG_DATATRANSFER;
  if (BusWidth == 4) {
    CmdFlags |= FS_MMC_CMD_FLAG_USE_SD4MODE;
  } else {
    if (BusWidth == 8) {
      CmdFlags |= FS_MMC_CMD_FLAG_USE_MMC8MODE;
    }
  }
  pCmdInfo->Flags |= (U16)CmdFlags;
  _SetNumBlocks(pInst, (U16)pDataInfo->NumBlocks);
  _SetBlockLen(pInst, pDataInfo->BytesPerBlock);
  _SetDataPointer(pInst, pDataInfo->pBuffer);
  //
  // Retry the command until it succeeds or a timeout occurs.
  //
  for (;;) {
    r = _ExecCmdR1WithStateTransition(pInst, pCmdInfo, pCardStatus);
    if (r < 0) {
      break;          // Quit the retry loop if the card has been removed or the card reports permanent errors.
    }
    if (r == 0) {
      //
      // OK, command sent. Read the data from card.
      //
      r = _ReadData(pInst, pDataInfo);
      FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: READ_DATA BytesPerBlock: %d, NumBlocks: %d, Res: %d\n", (int)pDataInfo->BytesPerBlock,
                                                                                                      (int)pDataInfo->NumBlocks,
                                                                                                      r));
      if (r == 0) {
        break;        // OK, data read successfully.
      }
      //
      // Do not perform any error recovery for these commands.
      //
      if (   (pCmdInfo->Index == CMD_BUSTEST_R)
          || (pCmdInfo->Index == CMD_SEND_TUNING_BLOCK_SD)
          || (pCmdInfo->Index == CMD_SEND_TUNING_BLOCK_MMC)) {
        break;
      }
      //
      // In case of a read error, try to put the card back in Transfer state.
      //
      rStop = _StopTransmissionIfRequired(pInst, pCardStatus);
      if (rStop != 0) {
        break;        // Error, could not put the card in Transfer state.
      }
    }
    FS_DEBUG_WARN((FS_MTYPE_DRIVER, "MMC_CM: _ExecCmdR1WithDataRead: Could not execute command (%d). %d retries left.", r, NumRetries));
    if (NumRetries == 0) {
      break;
    }
    --NumRetries;
  }
  return r;
}

/*********************************************************************
*
*      _ExecCmdR1WithDataWrite
*
*  Function description
*    Sends a command that expects an R1 response and writes data to the card.
*
*  Parameters
*    pInst            Driver instance.
*    pCmdInfo         Information about the command to be executed.
*    pDataInfo        Information about the data to be transferred to card.
*    pCardStatus      Status of the card returned in R1 response.
*    NumRetries       Maximum number of times to retry the command execution in case of a failure.
*
*  Return value
*    ==0    OK, command executed successfully.
*    > 0    Error, host controller reports a failure.
*    < 0    Error, card reports a failure or it has been removed.
*/
static int _ExecCmdR1WithDataWrite(MMC_CM_INST * pInst, CMD_INFO * pCmdInfo, const DATA_INFO * pDataInfo, CARD_STATUS * pCardStatus, int NumRetries) {
  int      r;
  int      rStop;
  unsigned CmdFlags;
  int      BusWidth;

  BusWidth = (int)pDataInfo->BusWidth;
  if (BusWidth == 0) {
    BusWidth = (int)pInst->BusWidth;
  }
  CmdFlags = 0u
           | FS_MMC_CMD_FLAG_DATATRANSFER
           | FS_MMC_CMD_FLAG_WRITETRANSFER
           ;
  if (BusWidth == 4) {
    CmdFlags |= FS_MMC_CMD_FLAG_USE_SD4MODE;
  } else {
    if (BusWidth == 8) {
      CmdFlags |= FS_MMC_CMD_FLAG_USE_MMC8MODE;
    }
  }
  pCmdInfo->Flags |= (U16)CmdFlags;
  _SetNumBlocks(pInst, (U16)pDataInfo->NumBlocks);
  _SetBlockLen(pInst, pDataInfo->BytesPerBlock);
  _SetDataPointer(pInst, pDataInfo->pBuffer);
  //
  // Retry the command until it succeeds or a timeout occurs.
  //
  for (;;) {
    r = _ExecCmdR1WithStateTransition(pInst, pCmdInfo, pCardStatus);
    if (r < 0) {
      break;          // Quit the retry loop if the card has been removed or the card reports permanent errors.
    }
    if (r == 0) {
      //
      // OK, command sent. Read the data from card.
      //
      r = _WriteData(pInst, pDataInfo);
      FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: WRITE_DATA BytesPerBlock: %d, NumBlocks: %d, Res: %d\n", (int)pDataInfo->BytesPerBlock,
                                                                                                       (int)pDataInfo->NumBlocks,
                                                                                                       r));
      if (r == 0) {
        break;        // OK, data written successfully.
      }
      //
      // Do not perform any error recovery for these commands.
      //
      if (pCmdInfo->Index == CMD_BUSTEST_W) {
        break;
      }
      //
      // In case of a read error, try to put the card back in Transfer state.
      //
      rStop = _StopTransmissionIfRequired(pInst, pCardStatus);
      if (rStop != 0) {
        break;        // Error, could not put the card in Transfer state.
      }
    }
    FS_DEBUG_WARN((FS_MTYPE_DRIVER, "MMC_CM: _ExecCmdR1WithDataWrite: Could not execute command (%d). %d retries left.", r, NumRetries));
    if (NumRetries == 0) {
      break;
    }
    --NumRetries;
  }
  return r;
}

/*********************************************************************
*
*       _ExecGoIdleState
*
*  Function description
*    Executes the command GO_IDLE_STATE (CMD0). This is the first
*    command send to a card before identification and initialization.
*    The card does not answer to this command.
*/
static void _ExecGoIdleState(MMC_CM_INST * pInst) {
  CMD_INFO CmdInfo;
  int      NumRetries;

  FS_MEMSET(&CmdInfo, 0, sizeof(CmdInfo));
  CmdInfo.Index = CMD_GO_IDLE_STATE;
  CmdInfo.Flags = (U16)FS_MMC_CMD_FLAG_INITIALIZE;
  //
  // Resend the command several times to make sure that the card receives it.
  //
  NumRetries = NUM_RETRIES_GO_IDLE;
  do {
    _ExecCmd(pInst, &CmdInfo);
  } while (--NumRetries != 0);
  _Delay(pInst, 10);        // TBD: This delay is probably not required anymore.
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: GO_IDLE_STATE\n"));
}

#if FS_MMC_SUPPORT_MMC

/*********************************************************************
*
*       _ExecSendOpCond
*
*  Function description
*    Executes the command SEND_OP_COND (CMD1). This command is sent in
*    order to initialize MMC cards.
*
*  Parameters
*    pInst          Driver instance.
*    VRange         Voltage range as bit mask.
*    IsHCSupported  Set to 1 if high capacity cards (> 2GB) are supported.
*    pOCR           [OUT] Contents of the OCR register (R3 response).
*
*  Return value
*    ==0    OK, command executed successfully.
*    > 0    Error, host controller reports a failure.
*    < 0    Error, card reports a failure or it has been removed.
*/
static int _ExecSendOpCond(MMC_CM_INST * pInst, U32 VRange, int IsHCSupported, OCR_RESPONSE * pOCR) {
  int      r;
  U32      Arg;
  CMD_INFO CmdInfo;

  FS_MEMSET(&CmdInfo, 0, sizeof(CmdInfo));
  Arg = VRange;
  if (IsHCSupported != 0) {           // Support for cards > 2GB
    Arg |= 0uL
        |  (1uL << ARG_HCS_SHIFT)
        |  (1uL << ARG_BUSY_SHIFT)    // According to MMC specification this bit has to be set to 1.
        ;
  }
  CmdInfo.Index = CMD_SEND_OP_COND;
  CmdInfo.Arg   = Arg;
  r = _ExecCmdR3(pInst, &CmdInfo, pOCR, 0);     // 0 since the command retries are performed in the caller.
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: SEND_OP_COND VHost: 0x%x, HCS: %d, ", VRange,
                                                                                IsHCSupported));
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "Res: %d, VCard: 0x%x, CCS: %d, IsPwUp: %d\n", r,
                                                                                ((U32)pOCR->aOCR[2] << 16) | ((U32)pOCR->aOCR[3] << 8) | (U32)pOCR->aOCR[4],
                                                                                _IsHighCapacityCard(pOCR),
                                                                                _IsCardPoweredUp(pOCR)));
  return r;
}

#endif // FS_MMC_SUPPORT_MMC

/*********************************************************************
*
*       _ExecAllSendCID
*
*  Function description
*    Executes the command ALL_SEND_CID (CMD2). This command ask the
*    card to send its id.
*
*  Parameters
*    pInst      Driver instance.
*    pCardId    [OUT] Card identification.
*/
static int _ExecAllSendCID(MMC_CM_INST * pInst, U8 * pCardId) {
  int      r;
  CMD_INFO CmdInfo;

  FS_MEMSET(&CmdInfo, 0, sizeof(CmdInfo));
  CmdInfo.Index = CMD_ALL_SEND_CID;
  r = _ExecCmdR2(pInst, &CmdInfo, pCardId, 0);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: ALL_SEND_CID Res: %d\n", r));
  return r;
}

#if FS_MMC_SUPPORT_SD

/*********************************************************************
*
*      _ExecSendRelativeAddrSD
*
*  Function description
*    Executes the command SET_RELATIVE_ADDR (CMD3) for SD cards.
*    The SD card responds with a relative address. For each
*    execution a different address is returned.
*/
static int _ExecSendRelativeAddrSD(MMC_CM_INST * pInst, unsigned * pRCA, U8 * pResData) {
  int      r;
  unsigned rca;
  CMD_INFO CmdInfo;

  rca = 0;
  FS_MEMSET(&CmdInfo, 0, sizeof(CmdInfo));
  CmdInfo.Index = CMD_SEND_RELATIVE_ADDR;
  r = _ExecCmdR6(pInst, &CmdInfo, pResData);
  if (r == 0) {
    rca = 0u
          | ((unsigned)pResData[1] << 8)
          |  (unsigned)pResData[2]
          ;
  }
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: SEND_RELATIVE_ADDR Res: %d, RCA: %d\n", r, (int)rca));
  if (pRCA != NULL) {
    *pRCA = rca;
  }
  return r;
}

#endif  // FS_MMC_SUPPORT_SD

#if FS_MMC_SUPPORT_MMC

/*********************************************************************
*
*      _ExecSetRelativeAddrMMC
*
*  Function description
*    Executes the command SET_RELATIVE_ADDR (CMD3) for MMC cards.
*    The host uses this command to assign a relative address to card.
*
*  Return value
*    ==0    OK, command executed successfully.
*    > 0    Error, host controller reports a failure.
*    < 0    Error, card reports a failure or it has been removed.
*/
static int _ExecSetRelativeAddrMMC(MMC_CM_INST * pInst, unsigned rca, CARD_STATUS * pCardStatus) {
  int      r;
  CMD_INFO CmdInfo;

  FS_MEMSET(pCardStatus, 0, sizeof(*pCardStatus));
  FS_MEMSET(&CmdInfo, 0, sizeof(CmdInfo));
  CmdInfo.Index = CMD_SET_RELATIVE_ADDR;
  CmdInfo.Arg   = (U32)rca << ARG_RCA_SHIFT;
  r = _ExecCmdR1(pInst, &CmdInfo, pCardStatus, 0);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: SET_RELATIVE_ADDR RCA: %d, Res: %d\n", (int)rca, r));
  return r;
}

#endif  // FS_MMC_SUPPORT_MMC

#if FS_MMC_SUPPORT_SD

/*********************************************************************
*
*       _ExecSwitchFunc
*
*  Function description
*    Executes the command SWITCH_FUNC (CMD6) for SD cards. The host uses
*    this command in order to activate additional functions in the SD card.
*
*  Parameters
*    pInst        Driver instance.
*    Mode         Operation mode (0 - query, 1 - set).
*    GroupIndex   Index of the function group to be accessed (0-based).
*    Value        Function value.
*    pResp        [OUT] Response of the SD card to CMD6.
*    pCardStatus  [OUT] Status of the SD card.
*
*  Return value
*    ==0    OK, command executed successfully.
*    > 0    Error, host controller reports a failure.
*    < 0    Error, card reports a failure.
*
*  Additional information
*    GroupIndex is the value of the group number as defined in the SD specification minus 1.
*/
static int _ExecSwitchFunc(MMC_CM_INST * pInst, int Mode, int GroupIndex, U8 Value, U32 * pResp, CARD_STATUS * pCardStatus) {
  int       r;
  CMD_INFO  CmdInfo;
  DATA_INFO DataInfo;
  U32       Arg;

  FS_MEMSET(&CmdInfo, 0, sizeof(CmdInfo));
  FS_MEMSET(&DataInfo, 0, sizeof(DataInfo));
  FS_MEMSET(pResp, 0, NUM_BYTES_SWITCH_RESP);
  Arg  = (U32)Mode << 31 | 0x00FFFFFFuL;
  Arg &= ~(0x0FuL << ((U32)GroupIndex << 2));
  Arg |= (U32)Value << ((U32)GroupIndex << 2);
  CmdInfo.Index          = CMD_SWITCH_FUNC;
  CmdInfo.Arg            = Arg;
  DataInfo.BytesPerBlock = NUM_BYTES_SWITCH_RESP;
  DataInfo.NumBlocks     = 1;
  DataInfo.pBuffer       = pResp;
  r = _ExecCmdR1WithDataRead(pInst, &CmdInfo, &DataInfo, pCardStatus, NUM_RETRIES_DATA_READ);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: SWITCH_FUNC Mode: %d, GroupIndex: %d, Value: %d, Res: %d\n", Mode,
                                                                                                       GroupIndex,
                                                                                                       (int)Value,
                                                                                                       r));
  return r;
}

/*********************************************************************
*
*       _ExecSendIFCond
*
*  Function description
*    Executes the command SEND_IF_COND (CMD8). The host uses the command
*    in order to check if the SD card supports the current supplied voltage
*    and to enable additional functionality to some existing commands.
*    This command is recognized only by the SD cards that comply to
*    SD specification version >= 2.00. The other SD cards and the MMC cards
*    do not respond to this command.
*
*  Parameters
*    pInst          Driver instance.
*    VRange         Voltage supplied by host.
*    CheckPattern   Check pattern to be send to card.
*    pResData       [OUT] Data of R7 response.
*
*  Return value
*    ==0    OK, command executed successfully.
*    > 0    Error, host controller reports a failure.
*/
static int _ExecSendIFCond(MMC_CM_INST * pInst, unsigned VRange, unsigned CheckPattern, U8 * pResData) {
  int      r;
  CMD_INFO CmdInfo;

  FS_MEMSET(&CmdInfo, 0, sizeof(CmdInfo));
  CmdInfo.Index = CMD_SEND_IF_COND;
  CmdInfo.Arg   = 0u
                | (U32)VRange << ARG_VHS_SHIFT      // This is the voltage supplied by host.
                | (U32)CheckPattern                 // This is the check pattern which must be echoed back by the card.
                ;
  r = _ExecCmdR7(pInst, &CmdInfo, pResData);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: SEND_IF_COND VHS: 0x%x, ChkPatOut: 0x%x ", VRange,
                                                                                     CheckPattern));
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "Res: %d, VCA: 0x%x, ChkPatIn: 0x%x\n", r,
                                                                         (unsigned)pResData[3] & ARG_VHS_MASK,
                                                                         (unsigned)pResData[4]));
  return r;
}

#endif  // FS_MMC_SUPPORT_SD

/*********************************************************************
*
*       _ExecSwitch
*
*  Function description
*    Executes the command SWITCH (CMD6) for MMC cards.
*    This command can be used by the host to change the contents of EXT_CSD register.
*
*  Return value
*    ==0    OK, command executed successfully.
*    > 0    Error, host controller reports a failure.
*    < 0    Error, card reports a failure.
*/
static int _ExecSwitch(MMC_CM_INST * pInst, int AccessType, int Index, int Value, CARD_STATUS * pCardStatus) {
  int      r;
  CMD_INFO CmdInfo;

  FS_MEMSET(&CmdInfo, 0, sizeof(CmdInfo));
  CmdInfo.Index = CMD_SWITCH;
  CmdInfo.Flags = (U16)FS_MMC_CMD_FLAG_SETBUSY;
  CmdInfo.Arg   = 0u
                | (U32)AccessType << 24
                | (U32)Index      << 16
                | (U32)Value      << 8
                ;
  r = _ExecCmdR1(pInst, &CmdInfo, pCardStatus, NUM_RETRIES_CMD);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: SWITCH AccType: %d, Idx: %d, Val: %d, Res: %d\n", AccessType,
                                                                                            Index,
                                                                                            Value,
                                                                                            r));
  return r;
}

/*********************************************************************
*
*       _ExecSendExtCSD
*
*  Function description
*    Executes the command SEND_EXT_CSD (CMD8). The host uses the command
*    in order to read the contents of EXT_CSD register from MMC cards.
*
*  Parameters
*    pInst          Driver instance.
*    BusWidth       Number of data lines to use for the data transfer.
*    pExtCSD        [OUT] Contents of EXT_CSD register.
*    pCardStatus    [OUT] Contents of status register.
*
*  Return value
*    ==0    OK, command executed successfully.
*    > 0    Error, host controller reports a failure.
*    < 0    Error, card reports a failure.
*/
static int _ExecSendExtCSD(MMC_CM_INST * pInst, int BusWidth, U32 * pExtCSD, CARD_STATUS * pCardStatus) {
  int       r;
  CMD_INFO  CmdInfo;
  DATA_INFO DataInfo;

  FS_MEMSET(&CmdInfo, 0, sizeof(CmdInfo));
  FS_MEMSET(&DataInfo, 0, sizeof(DataInfo));
  FS_MEMSET(pExtCSD, 0, NUM_BYTES_EXT_CSD);
  CmdInfo.Index          = CMD_SEND_EXT_CSD;
  DataInfo.BusWidth      = (U8)BusWidth;
  DataInfo.BytesPerBlock = NUM_BYTES_EXT_CSD;
  DataInfo.NumBlocks     = 1;
  DataInfo.pBuffer       = pExtCSD;
  r = _ExecCmdR1WithDataRead(pInst, &CmdInfo, &DataInfo, pCardStatus, NUM_RETRIES_DATA_READ);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: SEND_EXT_CSD BusWidth: %d, Res: %d\n", BusWidth, r));
  return r;
}

/*********************************************************************
*
*       _ExecSelectCard
*
*  Function description
*    Executes the command SELECT_CARD (CMD9). The host uses the command
*    to switch the card to/from Transfer State.
*
*  Parameters
*    pInst          Driver instance.
*    rca            Relative address of card.
*    pCardStatus    [OUT] Contents of status register.
*
*  Return value
*    ==0    OK, command executed successfully.
*    > 0    Error, host controller reports a failure.
*    < 0    Error, card reports a failure.
*/
static int _ExecSelectCard(MMC_CM_INST * pInst, unsigned rca, CARD_STATUS * pCardStatus) {
  int      r;
  int      IsSelect;
  CMD_INFO CmdInfo;
  unsigned NextStateMask;

  FS_MEMSET(&CmdInfo, 0, sizeof(CmdInfo));
  r        = 0;         // Set to indicate success.
  IsSelect = 1;
  if (rca == DEFAULT_RCA_DESELECT) {
    IsSelect = 0;
  }
  NextStateMask = 0u
                | (1uL << CARD_STATE_TRAN)
                | (1uL << CARD_STATE_PRG)
                ;
  CmdInfo.Index = CMD_SELECT_CARD;
  CmdInfo.Arg   = (U32)rca << ARG_RCA_SHIFT;
  if (IsSelect != 0) {
    CmdInfo.Flags         = (U16)FS_MMC_CMD_FLAG_SETBUSY;
    CmdInfo.NextStateMask = (U16)NextStateMask;
    r = _ExecCmdR1WithStateTransition(pInst, &CmdInfo, pCardStatus);
  } else {
    _ExecCmd(pInst, &CmdInfo);
  }
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: SELECT_CARD RCA: %d, Res: %d\n", (int)rca, r));
  return r;
}

/*********************************************************************
*
*       _ExecSendCSD
*
*  Function description
*    Executes the SEND_CSD (CMD9) command. The card responds by sending
*    the contents of the Card-Specific Data register. The command
*    is accepted only in Stand-by State.
*
*  Parameters
*    pInst    Driver instance.
*    pCSD     [OUT] Contents of CSD register.
*
*  Return value
*    ==0    CSD register has been read.
*    !=0    An error has occurred.
*/
static int _ExecSendCSD(MMC_CM_INST * pInst, CSD_RESPONSE * pCSD) {
  int      r;
  unsigned rca;
  CMD_INFO CmdInfo;

  FS_DEBUG_ASSERT(FS_MTYPE_DRIVER, sizeof(*pCSD) == NUM_BYTES_R2);      //lint !e506 !e774 Constant value Boolean and Boolean within 'if' always evaluates to False. Rationale: sanity checks that are performed only in debug builds.
  FS_MEMSET(&CmdInfo, 0, sizeof(CmdInfo));
  rca = pInst->Rca;
  CmdInfo.Index = CMD_SEND_CSD;
  CmdInfo.Arg   = (U32)rca << ARG_RCA_SHIFT;
  r = _ExecCmdR2(pInst, &CmdInfo, pCSD, NUM_RETRIES_CMD);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: SEND_CSD RCA: %d, Res: %d\n", (int)rca, r));
  return r;
}

/*********************************************************************
*
*       _ExecSendCID
*
*  Function description
*    Executes the SEND_CID (CMD10) command. The card responds by sending
*    the contents of the Card Identification register. The command
*    is accepted only in Stand-by State.
*
*  Parameters
*    pInst    Driver instance.
*    pCID     [OUT] Contents of CID register.
*
*  Return value
*    ==0    CID register has been read.
*    !=0    An error has occurred.
*/
static int _ExecSendCID(MMC_CM_INST * pInst, CID_RESPONSE * pCID) {
  int      r;
  unsigned rca;
  CMD_INFO CmdInfo;

  FS_DEBUG_ASSERT(FS_MTYPE_DRIVER, sizeof(*pCID) == NUM_BYTES_R2);    //lint !e506 !e774 Constant value Boolean and Boolean within 'if' always evaluates to False. Rationale: sanity checks that are performed only in debug builds.
  FS_MEMSET(&CmdInfo, 0, sizeof(CmdInfo));
  rca = pInst->Rca;
  CmdInfo.Index = CMD_SEND_CID;
  CmdInfo.Arg   = (U32)rca << ARG_RCA_SHIFT;
  r = _ExecCmdR2(pInst, &CmdInfo, pCID, NUM_RETRIES_CMD);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: SEND_CID RCA: %d, Res: %d\n", (int)rca, r));
  return r;
}

/*********************************************************************
*
*       _ExecStopTransmission
*
*  Function description
*    Executes the STOP_TRAN (CMD12) command. The device stops sending
*    or receiving data.
*
*  Parameters
*    pInst          Driver instance.
*    pCardStatus    [OUT] Contents of status register.
*
*  Return value
*    ==0    Data transfer stopped.
*    !=0    An error has occurred.
*/
static int _ExecStopTransmission(MMC_CM_INST * pInst, CARD_STATUS * pCardStatus) {
  int      r;
  CMD_INFO CmdInfo;
  unsigned Flags;
  unsigned NextStateMask;

  FS_MEMSET(&CmdInfo, 0, sizeof(CmdInfo));
  Flags         = 0u
                | FS_MMC_CMD_FLAG_SETBUSY
                | FS_MMC_CMD_FLAG_STOP_TRANS
                ;
  NextStateMask = 0u
                | (1u << CARD_STATE_TRAN)
                | (1u << CARD_STATE_PRG)
                ;
  CmdInfo.Index         = CMD_STOP_TRANSMISSION;
  CmdInfo.Flags         = (U16)Flags;
  CmdInfo.NextStateMask = (U16)NextStateMask;
  r = _ExecCmdR1WithStateTransition(pInst, &CmdInfo, pCardStatus);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: STOP_TRANSMISSION Res: %d\n", r));
  return r;
}

/*********************************************************************
*
*      _ExecSendStatus
*
*  Function description
*    Executes the SEND_STATUS (CMD13) command. The response to this
*    command is the card internal status.
*
*  Return value
*    ==0    OK, command executed successfully.
*    > 0    Error, host controller reports a failure.
*    < 0    Error, card reports a failure.
*/
static int _ExecSendStatus(MMC_CM_INST * pInst, CARD_STATUS * pCardStatus) {
  int      r;
  unsigned rca;
  CMD_INFO CmdInfo;

  FS_MEMSET(&CmdInfo, 0, sizeof(CmdInfo));
  rca = pInst->Rca;
  CmdInfo.Index = CMD_SEND_STATUS;
  CmdInfo.Arg   = (U32)rca << ARG_RCA_SHIFT;
  r = _ExecCmdR1(pInst, &CmdInfo, pCardStatus, NUM_RETRIES_CMD);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: SEND_STATUS RCA: %d, Res: %d\n", (int)rca, r));
  return r;
}

/*********************************************************************
*
*       _ExecSetBlockLen
*
*  Function description
*    Executes the command SET_BLOCKLEN (CMD16). This command
*    sets the number of bytes transfered to/from the card
*    in a single data block.
*    The caller has to make sure that the card is in Transfer State.
*
*  Return value
*    ==0    OK, command executed successfully.
*    > 0    Error, host controller reports a failure.
*    < 0    Error, card reports a failure.
*/
static int _ExecSetBlockLen(MMC_CM_INST * pInst, unsigned NumBytes, CARD_STATUS * pCardStatus) {
  int      r;
  CMD_INFO CmdInfo;

  FS_MEMSET(&CmdInfo, 0, sizeof(CmdInfo));
  CmdInfo.Index = CMD_SET_BLOCKLEN;
  CmdInfo.Arg   = NumBytes;
  r = _ExecCmdR1(pInst, &CmdInfo, pCardStatus, NUM_RETRIES_CMD);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: SET_BLOCKLEN NumBytes: %d, Res: %d\n", (int)NumBytes, r));
  return r;
}

/*********************************************************************
*
*       _ExecReadSingleBlock
*
*  Function description
*    Executes the command READ_SINGLE_BLOCK (CMD17). This command
*    transfers one sector from card to host.
*    The caller has to make sure that the card is in Transfer State.
*
*  Return value
*    ==0    OK, command executed successfully.
*    > 0    Error, host controller reports a failure.
*    < 0    Error, card reports a failure.
*/
static int _ExecReadSingleBlock(MMC_CM_INST * pInst, U32 SectorIndex, U32 * pData, CARD_STATUS * pCardStatus) {
  int       r;
  U32       Arg;
  CMD_INFO  CmdInfo;
  DATA_INFO DataInfo;

  FS_MEMSET(&CmdInfo, 0, sizeof(CmdInfo));
  FS_MEMSET(&DataInfo, 0, sizeof(DataInfo));
  Arg = SectorIndex;
  if (pInst->IsHighCapacity == 0u) {
    Arg <<= BYTES_PER_SECTOR_SHIFT;     // Use the byte address for standard capacity cards (<= 2GB).
  }
  CmdInfo.Index          = CMD_READ_SINGLE_BLOCK;
  CmdInfo.Arg            = Arg;
  DataInfo.BytesPerBlock = (U16)BYTES_PER_SECTOR;
  DataInfo.NumBlocks     = 1;
  DataInfo.pBuffer       = pData;
  r = _ExecCmdR1WithDataRead(pInst, &CmdInfo, &DataInfo, pCardStatus, 0);   // Retries have to be done in the caller function.
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: READ_SINGLE_BLOCK SectorIndex: %lu, Res: %d\n", SectorIndex, r));
  return r;
}

/*********************************************************************
*
*       _ExecReadMultipleBlocks
*
*  Function description
*    Executes the command READ_MULTIPLE_BLOCKS (CMD18). This command
*    transfers one or more sector from card to host.
*    The caller has to make sure that the card is in Transfer State.
*
*  Return value
*    ==0    OK, command executed successfully.
*    > 0    Error, host controller reports a failure.
*    < 0    Error, card reports a failure.
*/
static int _ExecReadMultipleBlocks(MMC_CM_INST * pInst, U32 SectorIndex, U32 * pData, unsigned NumSectors, CARD_STATUS * pCardStatus) {
  int       r;
  U32       Arg;
  CMD_INFO  CmdInfo;
  DATA_INFO DataInfo;

  FS_MEMSET(&CmdInfo, 0, sizeof(CmdInfo));
  FS_MEMSET(&DataInfo, 0, sizeof(DataInfo));
  Arg = SectorIndex;
  if (pInst->IsHighCapacity == 0u) {
    Arg <<= BYTES_PER_SECTOR_SHIFT;     // Use the byte address for standard capacity cards (<= 2GB).
  }
  CmdInfo.Index          = CMD_READ_MULTIPLE_BLOCKS;
  CmdInfo.Arg            = Arg;
  DataInfo.BytesPerBlock = (U16)BYTES_PER_SECTOR;
  DataInfo.NumBlocks     = NumSectors;
  DataInfo.pBuffer       = pData;
  r = _ExecCmdR1WithDataRead(pInst, &CmdInfo, &DataInfo, pCardStatus, 0);    // Retries have to be done in the caller function.
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: READ_MULTIPLE_BLOCKS SectorIndex: %lu, NumSectors: %u, Res: %d\n", SectorIndex, NumSectors, r));
  return r;
}

#if (FS_MMC_SUPPORT_MMC != 0) && (FS_MMC_TEST_BUS_WIDTH != 0)

/*********************************************************************
*
*      _ExecBusTestR
*
*  Function description
*    Executes the BUSTEST_R (CMD14) command. This command is used
*    for testing the width of the data bus.
*
*  Return value
*    ==0    OK, command executed successfully.
*    > 0    Error, host controller reports a failure.
*    < 0    Error, card reports a failure.
*/
static int _ExecBusTestR(MMC_CM_INST * pInst, U32 * pPattern, unsigned NumBytes, int BusWidth, CARD_STATUS * pCardStatus) {
  int       r;
  CMD_INFO  CmdInfo;
  DATA_INFO DataInfo;

  FS_MEMSET(&CmdInfo, 0, sizeof(CmdInfo));
  FS_MEMSET(&DataInfo, 0, sizeof(DataInfo));
  FS_MEMSET(pPattern, 0, NumBytes);
  CmdInfo.Index          = CMD_BUSTEST_R;
  CmdInfo.Flags          = (U16)FS_MMC_CMD_FLAG_NO_CRC_CHECK;
  DataInfo.BusWidth      = (U8)BusWidth;
  DataInfo.BytesPerBlock = (U16)NumBytes;
  DataInfo.NumBlocks     = 1;
  DataInfo.pBuffer       = pPattern;
  r = _ExecCmdR1WithDataRead(pInst, &CmdInfo, &DataInfo, pCardStatus, 0);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: BUSTEST_R BusWidth: %d, NumBytes: %d, Res: %d\n", BusWidth,
                                                                                            (int)NumBytes,
                                                                                            r));
  return r;
}

/*********************************************************************
*
*      _ExecBusTestW
*
*  Function description
*    Executes the BUSTEST_W (CMD19) command. This command is used
*    for testing the width of the data bus.
*
*  Return value
*    ==0    OK, command executed successfully.
*    > 0    Error, host controller reports a failure.
*    < 0    Error, card reports a failure.
*/
static int _ExecBusTestW(MMC_CM_INST * pInst, const U32 * pPattern, unsigned NumBytes, int BusWidth, CARD_STATUS * pCardStatus) {
  int       r;
  CMD_INFO  CmdInfo;
  DATA_INFO DataInfo;

  FS_MEMSET(&CmdInfo, 0, sizeof(CmdInfo));
  FS_MEMSET(&DataInfo, 0, sizeof(DataInfo));
  CmdInfo.Index          = CMD_BUSTEST_W;
  CmdInfo.Flags          = (U16)FS_MMC_CMD_FLAG_NO_CRC_CHECK;
  DataInfo.BusWidth      = (U8)BusWidth;
  DataInfo.BytesPerBlock = (U16)NumBytes;
  DataInfo.NumBlocks     = 1;
  DataInfo.pBuffer       = SEGGER_PTR2PTR(void, pPattern);          //lint !e9005 attempt to cast away const/volatile from a pointer or reference [MISRA 2012 Rule 11.8, required]. Rationale: the pBuffer structure member is used for read as well as write operations.
  r = _ExecCmdR1WithDataWrite(pInst, &CmdInfo, &DataInfo, pCardStatus, 0);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: BUSTEST_W BusWidth: %d, NumBytes: %d, Res: %d\n", BusWidth,
                                                                                            (int)NumBytes,
                                                                                            r));
  return r;
}

#endif  // FS_MMC_SUPPORT_MMC && FS_MMC_TEST_BUS_WIDTH

/*********************************************************************
*
*       _ExecSetBlockCount
*
*  Function description
*    Executes the command SET_BLOCK_COUNT (CMD23). This command
*    sets the number of sectors to be transfered to/from the card
*    in a single write/read operation.
*    The caller has to make sure that the card is in Transfer State.
*
*  Return value
*    ==0    OK, command executed successfully.
*    > 0    Error, host controller reports a failure.
*    < 0    Error, card reports a failure.
*/
static int _ExecSetBlockCount(MMC_CM_INST * pInst, unsigned NumBlocks, int IsReliableWrite, CARD_STATUS * pCardStatus) {
  int      r;
  CMD_INFO CmdInfo;
  U32      Arg;

  FS_MEMSET(&CmdInfo, 0, sizeof(CmdInfo));
  Arg = NumBlocks;          // TBD: Add assertion for NumBlocks <= 0xFFFF
  if (IsReliableWrite != 0) {
    Arg |= 1uL << ARG_RELIABLE_WRITE_SHIFT;
  }
  CmdInfo.Index = CMD_SET_BLOCK_COUNT;
  CmdInfo.Arg   = Arg;
  r = _ExecCmdR1(pInst, &CmdInfo, pCardStatus, NUM_RETRIES_CMD);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: SET_BLOCK_COUNT NumBlocks: %d, IsRelWr: %d, Res: %d\n", (int)NumBlocks, IsReliableWrite, r));
  return r;
}

/*********************************************************************
*
*       _ExecWriteBlock
*
*  Function description
*    Executes the command WRITE_BLOCK (CMD24). This command
*    transfers one sector from host to card.
*    The caller has to make sure that the card is in Transfer State.
*
*  Return value
*    ==0    OK, command executed successfully.
*    > 0    Error, host controller reports a failure.
*    < 0    Error, card reports a failure.
*/
static int _ExecWriteBlock(MMC_CM_INST * pInst, U32 SectorIndex, const U32 * pData, CARD_STATUS * pCardStatus) {
  int       r;
  U32       Arg;
  CMD_INFO  CmdInfo;
  DATA_INFO DataInfo;

  FS_MEMSET(&CmdInfo, 0, sizeof(CmdInfo));
  FS_MEMSET(&DataInfo, 0, sizeof(DataInfo));
  Arg = SectorIndex;
  if (pInst->IsHighCapacity == 0u) {
    Arg <<= BYTES_PER_SECTOR_SHIFT;     // Use the byte address for standard capacity cards (<= 2GB).
  }
  CmdInfo.Index          = CMD_WRITE_BLOCK;
  CmdInfo.Arg            = Arg;
  DataInfo.BytesPerBlock = (U16)BYTES_PER_SECTOR;
  DataInfo.NumBlocks     = 1;
  DataInfo.pBuffer       = SEGGER_PTR2PTR(void, pData);                       //lint !e9005 attempt to cast away const/volatile from a pointer or reference [MISRA 2012 Rule 11.8, required]. Rationale: the pBuffer structure member is used for read as well as write operations.
  r = _ExecCmdR1WithDataWrite(pInst, &CmdInfo, &DataInfo, pCardStatus, 0);    // Retries have to be done in the caller.
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: WRITE_BLOCK SectorIndex: %lu, Res: %d\n", SectorIndex, r));
  return r;
}

/*********************************************************************
*
*       _ExecWriteMultipleBlocks
*
*  Function description
*    Executes the command WRITE_MULTIPLE_BLOCKS (CMD25). This command
*    transfers one or more sector from host to card.
*    The caller has to make sure that the card is in Transfer State
*    when calling this function.
*
*  Return value
*    ==0    OK, command executed successfully.
*    > 0    Error, host controller reports a failure.
*    < 0    Error, card reports a failure.
*/
static int _ExecWriteMultipleBlocks(MMC_CM_INST * pInst, U32 SectorIndex, const U32 * pData, unsigned NumSectors, U8 BurstType, CARD_STATUS * pCardStatus) {
  int       r;
  U32       Arg;
  CMD_INFO  CmdInfo;
  DATA_INFO DataInfo;
  U16       CmdFlags;

  FS_MEMSET(&CmdInfo, 0, sizeof(CmdInfo));
  FS_MEMSET(&DataInfo, 0, sizeof(DataInfo));
  Arg = SectorIndex;
  if (pInst->IsHighCapacity == 0u) {
    Arg <<= BYTES_PER_SECTOR_SHIFT;     // Use the byte address for standard capacity cards (<= 2GB).
  }
  switch (BurstType) {
  case BURST_TYPE_REPEAT:
    CmdFlags = (U16)FS_MMC_CMD_FLAG_WRITE_BURST_REPEAT;
    break;
  case BURST_TYPE_FILL:
    CmdFlags = (U16)FS_MMC_CMD_FLAG_WRITE_BURST_FILL;
    break;
  default:
    CmdFlags = 0;
    break;
  }
  CmdInfo.Index          = CMD_WRITE_MULTIPLE_BLOCKS;
  CmdInfo.Arg            = Arg;
  CmdInfo.Flags          = CmdFlags;
  DataInfo.BytesPerBlock = (U16)BYTES_PER_SECTOR;
  DataInfo.NumBlocks     = NumSectors;
  DataInfo.pBuffer       = SEGGER_PTR2PTR(void, pData);                       //lint !e9005 attempt to cast away const/volatile from a pointer or reference [MISRA 2012 Rule 11.8, required]. Rationale: the pBuffer structure member is used for read as well as write operations.
  r = _ExecCmdR1WithDataWrite(pInst, &CmdInfo, &DataInfo, pCardStatus, 0);    // Retries have to be done in the caller.
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: WRITE_MULTIPLE_BLOCKS SectorIndex: %lu, NumSectors: %u, Res: %d\n", SectorIndex, NumSectors, r));
  return r;
}

#if FS_MMC_SUPPORT_MMC

/*********************************************************************
*
*      _ExecEraseGroupStart
*
*  Function description
*    Executes the ERASE_GROUP_START (CMD35) command.
*
*  Return value
*    ==0    OK, command executed successfully.
*    > 0    Error, host controller reports a failure.
*    < 0    Error, card reports a failure.
*/
static int _ExecEraseGroupStart(MMC_CM_INST * pInst, U32 SectorIndex, CARD_STATUS * pCardStatus) {
  int      r;
  U32      Arg;
  CMD_INFO CmdInfo;

  FS_MEMSET(&CmdInfo, 0, sizeof(CmdInfo));
  Arg = SectorIndex;
  if (pInst->IsHighCapacity == 0u) {
    Arg <<= BYTES_PER_SECTOR_SHIFT;
  }
  CmdInfo.Index = CMD_ERASE_GROUP_START;
  CmdInfo.Arg   = Arg;
  r = _ExecCmdR1(pInst, &CmdInfo, pCardStatus, NUM_RETRIES_CMD);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: ERASE_GROUP_START SectorIndex: %lu, Res: %d\n", SectorIndex, r));
  return r;
}

/*********************************************************************
*
*      _ExecEraseGroupEnd
*
*  Function description
*    Executes the ERASE_GROUP_END (CMD36) command.
*
*  Return value
*    ==0    OK, command executed successfully.
*    > 0    Error, host controller reports a failure.
*    < 0    Error, card reports a failure.
*/
static int _ExecEraseGroupEnd(MMC_CM_INST * pInst, U32 SectorIndex, CARD_STATUS * pCardStatus) {
  int      r;
  U32      Arg;
  CMD_INFO CmdInfo;

  FS_MEMSET(&CmdInfo, 0, sizeof(CmdInfo));
  Arg = SectorIndex;
  if (pInst->IsHighCapacity == 0u) {
    Arg <<= BYTES_PER_SECTOR_SHIFT;
  }
  CmdInfo.Index = CMD_ERASE_GROUP_END;
  CmdInfo.Arg   = Arg;
  r = _ExecCmdR1(pInst, &CmdInfo, pCardStatus, NUM_RETRIES_CMD);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: ERASE_GROUP_END SectorIndex: %lu, Res: %d\n", SectorIndex, r));
  return r;
}

/*********************************************************************
*
*      _ExecEraseMMC
*
*  Function description
*    Executes the ERASE (CMD38) command for MMC.
*
*  Return value
*    ==0    OK, command executed successfully.
*    > 0    Error, host controller reports a failure.
*    < 0    Error, card reports a failure.
*/
static int _ExecEraseMMC(MMC_CM_INST * pInst, int IsSecure, int ForceGarbageCollect, int MarkForErase, CARD_STATUS * pCardStatus) {
  int      r;
  U32      Arg;
  CMD_INFO CmdInfo;

  FS_MEMSET(pCardStatus, 0, sizeof(*pCardStatus));
  FS_MEMSET(&CmdInfo, 0, sizeof(CmdInfo));
  Arg = 0;
  if (IsSecure != 0) {
    Arg |= 1uL << ARG_ERASE_IS_SECURE_SHIFT;
  }
  if (ForceGarbageCollect != 0) {
    Arg |= 1uL << ARG_ERASE_FORCE_GC_SHIFT;
  }
  if (MarkForErase != 0) {
    Arg |= 1uL << ARG_ERASE_MARK_SHIFT;
  }
  CmdInfo.Index = CMD_ERASE_MMC;
  CmdInfo.Flags = (U16)FS_MMC_CMD_FLAG_SETBUSY;
  CmdInfo.Arg   = Arg;
  r = _ExecCmdR1(pInst, &CmdInfo, pCardStatus, NUM_RETRIES_CMD);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: ERASE IsSecure: %d, ForceGC: %d, MarkForErase: %d, Res: %d\n", IsSecure,
                                                                                                         ForceGarbageCollect,
                                                                                                         MarkForErase,
                                                                                                         r));
  return r;
}

#endif  // FS_MMC_SUPPORT_MMC

/*********************************************************************
*
*      _ExecLockUnlock
*
*  Function description
*    Executes the LOCK_UNLOCK (CMD42) command.
*    The caller has to make sure that the card is in Transfer State.
*
*  Return value
*    ==0    OK, command executed successfully.
*    > 0    Error, host controller reports a failure.
*    < 0    Error, card reports a failure.
*/
static int _ExecLockUnlock(MMC_CM_INST * pInst,
                           int           DoErase,
                           int           DoLock,
                           int           DoClrPass,
                           int           DoSetPass,
                           unsigned      NewPassLen,
                           const U8    * pNewPass,
                           unsigned      OldPassLen,
                           const U8    * pOldPass,
                           CARD_STATUS * pCardStatus) {
  int        r;
  U32        aData[NUM_BYTES_LOCK_UNLOCK / 4u];    // 32-bit aligned for faster DMA transfers.
  U8       * pData8;
  unsigned   LockFlags;
  unsigned   PassLen;
  CMD_INFO   CmdInfo;
  DATA_INFO  DataInfo;

  FS_MEMSET(aData, 0, sizeof(aData));
  FS_MEMSET(&CmdInfo, 0, sizeof(CmdInfo));
  FS_MEMSET(&DataInfo, 0, sizeof(DataInfo));
  LockFlags = 0;
  if (DoErase != 0) {
    LockFlags |= 1uL << LOCK_ERASE_SHIFT;
  }
  if (DoLock != 0) {
    LockFlags |= 1uL << LOCK_LOCK_SHIFT;
  }
  if (DoClrPass != 0) {
    LockFlags |= 1uL << LOCK_CLR_PWD_SHIFT;
  }
  if (DoSetPass != 0) {
    LockFlags |= 1uL << LOCK_SET_PWD_SHIFT;
  }
  PassLen = NewPassLen + OldPassLen;
  pData8 = SEGGER_PTR2PTR(U8, aData);
  *pData8++ = (U8)LockFlags;
  *pData8++ = (U8)PassLen;
  if ((NewPassLen != 0u) && (pNewPass != NULL)) {
    FS_MEMCPY(pData8, pNewPass, NewPassLen);
    if ((OldPassLen != 0u) && (pOldPass != NULL)) {
      pData8 += NewPassLen;
      FS_MEMCPY(pData8, pOldPass, OldPassLen);
    }
  }
  CmdInfo.Index          = CMD_LOCK_UNLOCK;
  DataInfo.BytesPerBlock = NUM_BYTES_LOCK_UNLOCK;
  DataInfo.NumBlocks     = 1;
  DataInfo.pBuffer       = aData;
  r = _ExecCmdR1WithDataWrite(pInst, &CmdInfo, &DataInfo, pCardStatus, 0);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: LOCK_UNLOCK DoErase: %d, DoLock: %d, DoSetPass: %d, ", DoErase,
                                                                                                 DoLock,
                                                                                                 DoSetPass));
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "DoClrPass: %d, NewPassLen: %d, OldPassLen: %d, Res: %d IsLocked: %d\n", DoClrPass,
                                                                                                          (int)NewPassLen,
                                                                                                          (int)OldPassLen,
                                                                                                          r,
                                                                                                          _IsCardLocked(pCardStatus)));
  return r;
}

#if FS_MMC_SUPPORT_SD

/*********************************************************************
*
*       _ExecSetBusWidth
*
*  Function description
*    Executes the command SET_BUS_WIDTH (ACMD6). This command is sent in
*    order to change the number of data lines the SD card uses for data transfers.
*
*  Return value
*    ==0    OK, command executed successfully.
*    > 0    Error, host controller reports a failure.
*    < 0    Error, card reports a failure or it has been removed.
*/
static int _ExecSetBusWidth(MMC_CM_INST * pInst, int BusWidth, CARD_STATUS * pCardStatus) {
  int      r;
  U32      Arg;
  CMD_INFO CmdInfo;

  FS_MEMSET(pCardStatus, 0, sizeof(*pCardStatus));
  FS_MEMSET(&CmdInfo, 0, sizeof(CmdInfo));
  switch (BusWidth) {
  default:
    // through
  case 1:
    Arg = ARG_BUS_WIDTH_1BIT;
    break;
  case 4:
    Arg = ARG_BUS_WIDTH_4BIT;
    break;
  }
  CmdInfo.Index    = ACMD_SET_BUS_WIDTH;
  CmdInfo.IsAppCmd = 1;
  CmdInfo.Arg      = Arg;
  r = _ExecAppCmdR1(pInst, &CmdInfo, pCardStatus, NUM_RETRIES_CMD);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: SET_BUS_WIDTH BusWidth: %d, Res: %d\n", BusWidth, r));
  return r;
}

/*********************************************************************
*
*       _ExecSDStatus
*
*  Function description
*    Executes the command SD_STATUS (ACMD13). This command is sent in
*    order to read the proprietary features of an SD card.
*
*  Return value
*    ==0    OK, command executed successfully.
*    > 0    Error, host controller reports a failure.
*    < 0    Error, card reports a failure or it has been removed.
*/
static int _ExecSDStatus(MMC_CM_INST * pInst, int BusWidth, U32 * pSDStatus, CARD_STATUS * pCardStatus) {
  int        r;
  CMD_INFO   CmdInfo;
  DATA_INFO  DataInfo;

  FS_MEMSET(&CmdInfo, 0, sizeof(CmdInfo));
  FS_MEMSET(&DataInfo, 0, sizeof(DataInfo));
  FS_MEMSET(pSDStatus, 0, NUM_BYTES_SD_STATUS);
  CmdInfo.Index          = ACMD_SD_STATUS;
  CmdInfo.IsAppCmd       = 1;
  DataInfo.BusWidth      = (U8)BusWidth;
  DataInfo.BytesPerBlock = NUM_BYTES_SD_STATUS;
  DataInfo.NumBlocks     = 1;
  DataInfo.pBuffer       = pSDStatus;
  r = _ExecCmdR1WithDataRead(pInst, &CmdInfo, &DataInfo, pCardStatus, NUM_RETRIES_DATA_READ);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: SD_STATUS BusWidth: %d, Res: %d\n", BusWidth, r));
  return r;
}

/*********************************************************************
*
*       _ExecSetWrBlkEraseCount
*
*  Function description
*    Executes the command SET_WR_BLK_ERASE_COUNT (ACMD23).
*
*  Return value
*    ==0    OK, command executed successfully.
*    > 0    Error, host controller reports a failure.
*    < 0    Error, card reports a failure or it has been removed.
*/
static int _ExecSetWrBlkEraseCount(MMC_CM_INST * pInst, U32 NumSectors, CARD_STATUS * pCardStatus) {
  int      r;
  CMD_INFO CmdInfo;

  FS_MEMSET(&CmdInfo, 0, sizeof(CmdInfo));
  CmdInfo.Index    = ACMD_SET_WR_BLK_ERASE_COUNT;
  CmdInfo.Arg      = NumSectors;
  CmdInfo.IsAppCmd = 1;
  r = _ExecAppCmdR1(pInst, &CmdInfo, pCardStatus, NUM_RETRIES_CMD);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: SET_WR_BLK_ERASE_COUNT NumSectors: %lu, Res: %d\n", NumSectors, r));
  return r;
}

/*********************************************************************
*
*       _ExecSendOpCondAdv
*
*  Function description
*    Executes the command SD_SEND_OP_COND (ACMD41). This command is sent in
*    order to initialize SD cards.
*
*  Return value
*    ==0    OK, command executed successfully.
*    > 0    Error, host controller reports a failure.
*    < 0    Error, card reports a failure.
*/
static int _ExecSendOpCondAdv(MMC_CM_INST * pInst, U32 VRange, int IsHCSupported, int Is1V8Requested, OCR_RESPONSE * pOCR, CARD_STATUS * pCardStatus) {
  int      r;
  U32      Arg;
  CMD_INFO CmdInfo;

  FS_MEMSET(&CmdInfo, 0, sizeof(CmdInfo));
  Arg = VRange;
  if (IsHCSupported != 0) {                                       // Support for cards > 2GB?
    Arg |= 1uL << ARG_HCS_SHIFT;
  }
  if (Is1V8Requested != 0) {                                      // Switch to 1.8 V signaling?
    Arg |= 1uL << ARG_S18R_SHIFT;
  }
  CmdInfo.Index    = ACMD_SD_SEND_OP_COND;
  CmdInfo.IsAppCmd = 1;
  CmdInfo.Arg      = Arg;
  r = _ExecAppCmdR3(pInst, &CmdInfo, pOCR, pCardStatus, 0);       // Command retries are performed outside of this function.
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: SD_SEND_OP_COND VHost: 0x%x, HCS: %d, S18R: %d, ", VRange,
                                                                                             IsHCSupported,
                                                                                             Is1V8Requested));
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "Res: %d, VCard: 0x%x, CCS: %d, S18A: %d, IsPwUp: %d\n", r,
                                                                                ((U32)pOCR->aOCR[2] << 16) | ((U32)pOCR->aOCR[3] << 8) | (U32)pOCR->aOCR[4],
                                                                                _IsHighCapacityCard(pOCR),
                                                                                _Is1V8Accepted(pOCR),
                                                                                _IsCardPoweredUp(pOCR)));
  return r;
}

/*********************************************************************
*
*       _ExecSendSCR
*
*  Function description
*    Executes the command SEND_SCR (ACMD51). This command reads
*    the contents of the SD Configuration Register (SCR).
*
*  Return value
*    ==0    OK, command executed successfully.
*    > 0    Error, host controller reports a failure.
*    < 0    Error, card reports a failure.
*/
static int _ExecSendSCR(MMC_CM_INST * pInst, U32 * pSCR, CARD_STATUS * pCardStatus) {
  int       r;
  CMD_INFO  CmdInfo;
  DATA_INFO DataInfo;

  FS_MEMSET(&CmdInfo, 0, sizeof(CmdInfo));
  FS_MEMSET(&DataInfo, 0, sizeof(DataInfo));
  FS_MEMSET(pSCR, 0, NUM_BYTES_SCR);
  CmdInfo.Index          = ACMD_SEND_SCR;
  CmdInfo.IsAppCmd       = 1;
  DataInfo.BytesPerBlock = NUM_BYTES_SCR;
  DataInfo.NumBlocks     = 1;
  DataInfo.pBuffer       = pSCR;
  r = _ExecCmdR1WithDataRead(pInst, &CmdInfo, &DataInfo, pCardStatus, NUM_RETRIES_DATA_READ);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: SEND_SCR Res: %d\n", r));
  return r;
}

#endif  // FS_MMC_SUPPORT_SD

#if (FS_MMC_SUPPORT_MMC != 0) && (FS_MMC_SUPPORT_POWER_SAVE != 0)

/*********************************************************************
*
*       _ExecSleepAwake
*
*  Function description
*    Executes the SLEEP_AWAKE (CMD5) command. As a response to this
*    command the enters or exits Sleep State. In the Sleep State
*    the device consumes less power.
*
*  Parameters
*    pInst          Driver instance.
*    EnterSleep     Set to 1 to enter Sleep State or to 0 to exit Sleep State.
*    pCardStatus    [OUT] Contents of status register.
*
*  Return value
*    ==0    Command executed successfully.
*    !=0    An error has occurred.
*/
static int _ExecSleepAwake(MMC_CM_INST * pInst, U8 EnterSleep, CARD_STATUS * pCardStatus) {
  int      r;
  CMD_INFO CmdInfo;
  unsigned rca;

  FS_MEMSET(&CmdInfo, 0, sizeof(CmdInfo));
  rca = pInst->Rca;
  CmdInfo.Index  = CMD_SLEEP_AWAKE;
  CmdInfo.Flags  = (U16)FS_MMC_CMD_FLAG_SETBUSY;
  CmdInfo.Arg    = (U32)rca << ARG_RCA_SHIFT;
  if (EnterSleep != 0u) {
    CmdInfo.Arg |= 1uL << ARG_SLEEP_AWAKE_SHIFT;
  }
  r = _ExecCmdR1(pInst, &CmdInfo, pCardStatus, NUM_RETRIES_CMD);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: SLEEP_AWAKE RCA: %u, EnterSleep: %d, Res: %d\n", rca, EnterSleep, r));
  return r;
}

#endif  // FS_MMC_SUPPORT_MMC && FS_MMC_SUPPORT_POWER_SAVE

#if (FS_MMC_SUPPORT_SD != 0) && (FS_MMC_SUPPORT_UHS != 0)

/*********************************************************************
*
*       _ExecVoltageSwitch
*
*  Function description
*    Executes the VOLTAGE_SWITCH (CMD11) command that requests an SD
*    card to change the signaling of the I/O lines to 1.8 V.
*
*  Parameters
*    pInst          Driver instance.
*    pCardStatus    [OUT] Contents of status register.
*
*  Return value
*    ==0    Command executed successfully.
*    !=0    An error occurred.
*/
static int _ExecVoltageSwitch(MMC_CM_INST * pInst, CARD_STATUS * pCardStatus) {
  int      r;
  CMD_INFO CmdInfo;
  unsigned Flags;

  FS_MEMSET(&CmdInfo, 0, sizeof(CmdInfo));
  Flags = 0u
        | FS_MMC_CMD_FLAG_SWITCH_VOLTAGE
        ;
  CmdInfo.Index  = CMD_VOLTAGE_SWITCH;
  CmdInfo.Flags  = (U16)Flags;
  r = _ExecCmdR1(pInst, &CmdInfo, pCardStatus, NUM_RETRIES_CMD);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: VOLTAGE_SWITCH Res: %d\n", r));
  return r;
}

/*********************************************************************
*
*       _ExecSendTuningBlockSD
*
*  Function description
*    Executes the command SEND_TUNING_BLOCK (CMD19). This command reads
*    the contents of a predefined data block that can be used to calculate
*    the delayed required to correctly sample the data received from SD card.
*
*  Return value
*    ==0    OK, command executed successfully.
*    > 0    Error, host controller reports a failure.
*    < 0    Error, card reports a failure.
*/
static int _ExecSendTuningBlockSD(MMC_CM_INST * pInst, U32 * pTuningBlock, CARD_STATUS * pCardStatus) {
  int       r;
  CMD_INFO  CmdInfo;
  DATA_INFO DataInfo;

  FS_MEMSET(&CmdInfo, 0, sizeof(CmdInfo));
  FS_MEMSET(&DataInfo, 0, sizeof(DataInfo));
  FS_MEMSET(pTuningBlock, 0, NUM_BYTES_TUNING_BLOCK_4BIT);
  CmdInfo.Index          = CMD_SEND_TUNING_BLOCK_SD;
  DataInfo.BytesPerBlock = NUM_BYTES_TUNING_BLOCK_4BIT;
  DataInfo.NumBlocks     = 1;
  DataInfo.pBuffer       = pTuningBlock;
  r = _ExecCmdR1WithDataRead(pInst, &CmdInfo, &DataInfo, pCardStatus, 0);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: SEND_TUNING_BLOCK Res: %d\n", r));
  return r;
}

#endif // FS_MMC_SUPPORT_SD != 0 && FS_MMC_SUPPORT_UHS != 0

#if (FS_MMC_SUPPORT_MMC != 0) && (FS_MMC_SUPPORT_UHS != 0)

/*********************************************************************
*
*       _ExecSendTuningBlockMMC
*
*  Function description
*    Executes the command SEND_TUNING_BLOCK (CMD21). This command reads
*    the contents of a predefined data block that can be used to calculate
*    the delayed required to correctly sample the data received from MMC device.
*
*  Return value
*    ==0    OK, command executed successfully.
*    > 0    Error, host controller reports a failure.
*    < 0    Error, card reports a failure.
*/
static int _ExecSendTuningBlockMMC(MMC_CM_INST * pInst, U32 * pTuningBlock, unsigned NumBytes, CARD_STATUS * pCardStatus) {
  int       r;
  CMD_INFO  CmdInfo;
  DATA_INFO DataInfo;

  FS_MEMSET(&CmdInfo, 0, sizeof(CmdInfo));
  FS_MEMSET(&DataInfo, 0, sizeof(DataInfo));
  FS_MEMSET(pTuningBlock, 0, NumBytes);
  CmdInfo.Index          = CMD_SEND_TUNING_BLOCK_MMC;
  DataInfo.BytesPerBlock = NumBytes;
  DataInfo.NumBlocks     = 1;
  DataInfo.pBuffer       = pTuningBlock;
  r = _ExecCmdR1WithDataRead(pInst, &CmdInfo, &DataInfo, pCardStatus, 0);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: SEND_TUNING_BLOCK NumBytes: %d, Res: %d\n", (int)NumBytes, r));
  return r;
}

#endif // FS_MMC_SUPPORT_SD != 0 && FS_MMC_SUPPORT_UHS != 0

#if (FS_MMC_SUPPORT_SD != 0) && (FS_MMC_DISABLE_DAT3_PULLUP != 0)

/*********************************************************************
*
*       _ExecSetClrCardDetect
*
*  Function description
*    Executes the command ACMD_SET_CLR_CARD_DETECT (ACMD42).
*
*  Parameters
*    pInst        Driver instance.
*    OnOff        Indicates if the pull-up has to be enabled or disabled.
*    pCardStatus  [OUT] Internal status of the SD card.
*
*  Return value
*    ==0    OK, command executed successfully.
*    > 0    Error, host controller reports a failure.
*    < 0    Error, card reports a failure or it has been removed.
*/
static int _ExecSetClrCardDetect(MMC_CM_INST * pInst, int OnOff, CARD_STATUS * pCardStatus) {
  int      r;
  CMD_INFO CmdInfo;

  FS_MEMSET(&CmdInfo, 0, sizeof(CmdInfo));
  CmdInfo.Index    = ACMD_SET_CLR_CARD_DETECT;
  CmdInfo.Arg      = (U32)OnOff;
  CmdInfo.IsAppCmd = 1;
  r = _ExecAppCmdR1(pInst, &CmdInfo, pCardStatus, NUM_RETRIES_CMD);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: SET_CLR_CARD_DETECT OnOff: %d, Res: %d\n", OnOff, r));
  return r;
}

#endif // (FS_MMC_SUPPORT_SD != 0) && (FS_MMC_DISABLE_DAT3_PULLUP != 0)

/*********************************************************************
*
*       _StopTransmissionIfRequired
*
*  Function description
*    Stops the a read or write operation if the card is not in Transfer data state.
*/
static int _StopTransmissionIfRequired(MMC_CM_INST * pInst, CARD_STATUS * pCardStatus) {
  int      r;
  unsigned CurrentState;

  r = _ExecSendStatus(pInst, pCardStatus);
  if (r == 0) {
    CurrentState = _GetCardCurrentState(pCardStatus);
    if (CurrentState != CARD_STATE_TRAN) {
      r = _ExecStopTransmission(pInst, pCardStatus);
    }
  }
  return r;
}

#if FS_MMC_SUPPORT_SD

/*********************************************************************
*
*       _CheckIFCond
*
*  Function description
*    Checks if the card supports the voltage supplied by the host.
*
*  Return value
*    ==0    The card supports the voltage supplied by the host.
*    !=0    The card does not support the supply voltage or it does not recognize the command.
*/
static int _CheckIFCond(MMC_CM_INST * pInst, U8 * pResData) {
  int r;
  U32 VRange;
  U8  CheckPattern;
  int NumRetries;

  NumRetries = NUM_RETRIES_IF_COND;
  for (;;) {
    r = _ExecSendIFCond(pInst, DEFAULT_VOLTAGE_RANGE_SD, DEFAULT_CHECK_PATTERN, pResData);
    if (r == FS_MMC_CARD_RESPONSE_TIMEOUT) {
      r = 1;
      break;              // The card does not support the command.
    }
    VRange       = pResData[3] & ARG_VHS_MASK;      // Supply voltage accepted by the card.
    CheckPattern = pResData[4];                     // Check pattern echoed back by the card.
    if (VRange != DEFAULT_VOLTAGE_RANGE_SD) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _CheckIFCond: The card does not support the supplied voltage.\n"));
      r = 1;
      break;              // The card does not support the supplied voltage.
    }
    if (CheckPattern == DEFAULT_CHECK_PATTERN) {
      r = 0;              // OK, the card supports the supplied voltage.
      break;
    }
    if (_IsPresent(pInst) == 0) {
      FS_DEBUG_WARN((FS_MTYPE_DRIVER, "MMC_CM: _CheckIFCond: The card has been removed.\n"));
      r = 1;
      break;                                // The card has been removed.
    }
    //
    // Communication error. The returned pattern does not match. Retry as recommended in SD specification.
    //
    if (NumRetries == 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _CheckIFCond: Communication error.\n"));
      r = 1;
      break;              // Could not get information from card.
    }
    NumRetries--;
  }
  return r;
}

/*********************************************************************
*
*       _InitSDCard
*
*  Function description
*    Checks if an SD card is inserted.
*
*  Parameters
*    pInst            Driver instance.
*    IsCardV2         Set to 1 if an SD card which supports a version > 2.00 of the SD specification.
*    pIs1V8Supported  [IN] Specifies if the SD card has to be queried if it supports 1.8 V signaling  (1 - yes, 0 - no).
*                     [OUT] Indicates if the SD card supports 1.8 V signaling (1 - yes, 0 - no).
*    pCardStatus      [OUT] Status register.
*
*  Return value
*    ==0    SD card found.
*    ==1    A different card is inserted.
*/
static int _InitSDCard(MMC_CM_INST * pInst, int IsCardV2, int * pIs1V8Supported, CARD_STATUS * pCardStatus) {
  OCR_RESPONSE ocr;
  int          NumRetries;
  int          r;
  int          IsHCSupported;
  int          Is1V8Requested;

  NumRetries    = NUM_RETRIES_IDENTIFY_SD;
  IsHCSupported = 0;
  if (IsCardV2 != 0) {
    IsHCSupported = DEFAULT_HC_SUPPORT;
  }
  Is1V8Requested = 0;
  if (pIs1V8Supported != NULL) {
    Is1V8Requested = *pIs1V8Supported;
  }
  //
  // The SD card is initialized by sending the advanced command SD_SEND_OP_COND (ACMD41)
  // which reads the Operation Conditions Register (OCR).
  // MMC cards do not support this command and will ignore it.
  //
  for (;;) {
    r = _ExecSendOpCondAdv(pInst, DEFAULT_VOLTAGE_RANGE, IsHCSupported, Is1V8Requested, &ocr, pCardStatus);
    if (r != 0) {
      r = 1;
      break;                                // Not an SD card.
    }
    if (_IsCardPoweredUp(&ocr) != 0) {
      if (IsCardV2 != 0) {
        if (_IsHighCapacityCard(&ocr) != 0) {
          pInst->IsHighCapacity = 1;        // SDHC (> 2GB) card found.
        }
      }
      if (pIs1V8Supported != NULL) {
        *pIs1V8Supported = _Is1V8Accepted(&ocr);
      }
      break;                                // OK, found an SD card.
    }
    r = _IsPresent(pInst);
    if (r == 0) {
      FS_DEBUG_WARN((FS_MTYPE_DRIVER, "MMC_CM: _InitSDCard: Card has been removed.\n"));
      r = 1;
      break;                                // The card has been removed.
    }
    if (NumRetries == 0) {
      r = 1;
      break;                                // This is not an SD card.
    }
    NumRetries--;
  }
  return r;
}

#endif  // FS_MMC_SUPPORT_SD

#if FS_MMC_SUPPORT_MMC

/*********************************************************************
*
*       _InitMMCCard
*
*  Function description
*    Checks if an MMC card inserted and initialize it.
*
*  Parameters
*    pInst            Driver instance.
*    pIs1V8Supported [IN] Specifies that the host switched to 1.8 V signaling (1 - yes, 0 - no).
*                    [OUT] Indicates if the MMC device supports 1.8 V signaling (1 - yes, 0 - no).
*
*  Return value
*    ==0    OK, MMC device found.
*    ==1    This is not an MMC device.
*/
static int _InitMMCCard(MMC_CM_INST * pInst, int * pIs1V8Supported) {
  OCR_RESPONSE ocr;
  int          NumRetries;
  int          r;
  U32          VRange;

  NumRetries = NUM_RETRIES_IDENTIFY_MMC;
  //
  // The MMC card is initialized by sending the SEMD_OP_COND (CMD1) command.
  //
  VRange = DEFAULT_VOLTAGE_RANGE_MMC;
  if (pIs1V8Supported != NULL) {
    if (*pIs1V8Supported != 0) {
      VRange = VOLTAGE_RANGE_LOW;
    }
  }
  for (;;) {
    r = _ExecSendOpCond(pInst, VRange, DEFAULT_HC_SUPPORT, &ocr);
    if (r == 0) {
      if (_IsCardPoweredUp(&ocr) != 0) {
        //
        // Check if a high capacity MMC card is inserted.
        //
        if (_IsHighCapacityCard(&ocr) != 0) {
          pInst->IsHighCapacity = 1;        // This is a high capacity MMC card (> 2GB)
        }
        if (pIs1V8Supported != NULL) {
          if (*pIs1V8Supported != 0) {
            *pIs1V8Supported = _IsDualVoltageDevice(&ocr);
          }
        }
        break;                              // OK, found an MMC card.
      }
    } else {
      if ((NUM_RETRIES_IDENTIFY_MMC - NumRetries) >= FS_MMC_NUM_RETRIES) {
        r = 1;                              // Error, the MMC device does not respond.
        break;
      }
    }
    r = _IsPresent(pInst);
    if (r == 0) {
      FS_DEBUG_WARN((FS_MTYPE_DRIVER, "MMC_CM: _InitMMCCard: Card has been removed.\n"));
      r = 1;
      break;                                // The card has been removed.
    }
    if (NumRetries == 0) {
      r = 1;
      break;                                // This is not an MMC card.
    }
    NumRetries--;
  }
  return r;
}

#endif    // FS_MMC_SUPPORT_MMC

/*********************************************************************
*
*       _IdentifyInitCard
*
*  Function description
*    Identifies and initializes the inserted card.
*
*  Return value
*    Type of the card found (FS_MMC_CARD_TYPE_...)
*/
static int _IdentifyInitCard(MMC_CM_INST * pInst, int * pIs1V8Supported, CARD_STATUS * pCardStatus) {
  int r;
  int CardType;

  CardType = FS_MMC_CARD_TYPE_UNKNOWN;
#if (FS_MMC_SUPPORT_SD != 0) && (FS_MMC_SUPPORT_MMC != 0)
  r = _CheckIFCond(pInst, SEGGER_PTR2PTR(U8, pCardStatus));
  if (r == 0) {
    //
    // Found a card that complies with the SD specification V2.00.
    //
    r = _InitSDCard(pInst, 1, pIs1V8Supported, pCardStatus);
    if (r == 0) {
      CardType = FS_MMC_CARD_TYPE_SD;
    }
  } else {
    //
    // Found a card that does not comply with the SD specification V2.00.
    //
    r = _InitSDCard(pInst, 0, pIs1V8Supported, pCardStatus);
    if (r == 0) {
      CardType = FS_MMC_CARD_TYPE_SD;
    } else {
      //
      // Found an MMC card or eMMC device.
      //
      r = _InitMMCCard(pInst, pIs1V8Supported);
      if (r == 0) {
        CardType = FS_MMC_CARD_TYPE_MMC;
      }
    }
  }
#endif // FS_MMC_SUPPORT_SD != 0 && FS_MMC_SUPPORT_MMC != 0
#if (FS_MMC_SUPPORT_SD != 0) && (FS_MMC_SUPPORT_MMC == 0)
  r = _CheckIFCond(pInst, SEGGER_PTR2PTR(U8, pCardStatus));
  if (r == 0) {
    //
    // Found a card that complies with the SD specification V2.00.
    //
    r = _InitSDCard(pInst, 1, pIs1V8Supported, pCardStatus);
    if (r == 0) {
      CardType = FS_MMC_CARD_TYPE_SD;
    }
  } else {
    //
    // Found a card that does not comply with the SD specification V2.00.
    //
    r = _InitSDCard(pInst, 0, pIs1V8Supported, pCardStatus);
    if (r == 0) {
      CardType = FS_MMC_CARD_TYPE_SD;
    }
  }
#endif // FS_MMC_SUPPORT_SD != 0 && FS_MMC_SUPPORT_MMC == 0
#if (FS_MMC_SUPPORT_SD == 0) && (FS_MMC_SUPPORT_MMC != 0)
  FS_USE_PARA(pCardStatus);
  r = _InitMMCCard(pInst, pIs1V8Supported);
  if (r == 0) {
    CardType = FS_MMC_CARD_TYPE_MMC;
  }
#endif // FS_MMC_SUPPORT_SD == 0 && FS_MMC_SUPPORT_MMC != 0
  return CardType;
}

/*********************************************************************
*
*      _SetRCA
*
*  Function description
*    Set the relative address of the card. Different procedures are
*    applied for SD and MMC cards. The SD card generates a RCA and
*    sends it to host. For MMC the host assigns a relative address to card.
*/
static int _SetRCA(MMC_CM_INST * pInst, CARD_STATUS * pCardStatus) {
  int CardType;
  int r;

  CardType = (int)pInst->CardType;
  switch (CardType) {
#if FS_MMC_SUPPORT_SD
  case FS_MMC_CARD_TYPE_SD:
    {
      unsigned rca;
      int      NumRetries;

      rca = 0;
      //
      // Loop until we get an address different than the address
      // used for deselecting the SD cards.
      //
      NumRetries = NUM_RETRIES_RCA;
      for (;;) {
        //
        // A SD card tells us its RCA (relative card address).
        //
        r = _ExecSendRelativeAddrSD(pInst, &rca, (U8 *)pCardStatus);
        if (r == 0) {
          if (rca != DEFAULT_RCA_DESELECT) {
            pInst->Rca = (U16)rca;
            break;
          }
        }
        r = _IsPresent(pInst);
        if (r == 0) {
          FS_DEBUG_WARN((FS_MTYPE_DRIVER, "MMC_CM: _SetRCA: Card has been removed.\n"));
          r = 1;
          break;                                // The card has been removed.
        }
        if (NumRetries == 0) {
          r = 1;
          break;                                // This is not an MMC card.
        }
        NumRetries--;
      }
    }
    break;
#endif  // FS_MMC_SUPPORT_SD
#if FS_MMC_SUPPORT_MMC
  case FS_MMC_CARD_TYPE_MMC:
    //
    // The host provides a RCA (relative card address) to MMC cards.
    //
    r = _ExecSetRelativeAddrMMC(pInst, DEFAULT_MMC_RCA, pCardStatus);
    if (r < 0) {
      U32 CardErrors;

      CardErrors = _GetCardErrors(pCardStatus);
      if ((CardErrors & (1uL << STATUS_ILLEGAL_COMMAND_SHIFT)) != 0u) {
        r = 0;            // The SD_SEND_OP_COND was sent before to identify SD cards. This is an illegal command for MMC cards. Ignore this error.
      }
    }
    if (r == 0) {
      pInst->Rca = (U16)DEFAULT_MMC_RCA;
    }
    break;
#endif        // FS_MMC_SUPPORT_MMC
  default:
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _SetRCA: Invalid card type %d.", CardType));
    r = 1;
    break;
  }
  return r;
}

/*********************************************************************
*
*       _WriteExtCSDByte
*
*  Function description
*    Modifies a byte on the EXT_CSD register of a MMCplus card.
*/
static int _WriteExtCSDByte(MMC_CM_INST * pInst, int Index, int Value, CARD_STATUS * pCardStatus) {
  int r;

  r = _ExecSwitch(pInst, SWITCH_ACCESS_WRITE_BYTE, Index, Value, pCardStatus);
  return r;
}

/*********************************************************************
*
*       _WaitForCardReady
*
*  Function description
*    Waits for the card to become ready for accepting data from host.
*
*  Parameters
*    pInst        Driver instance.
*    pCardStatus  [IN]  Card status of the last executed command in the caller.
*                 [OUT] Card status of the last executed command in this function.
*
*  Return value
*    ==0      OK, card is ready.
*    !=0      An error occurred.
*/
static int _WaitForCardReady(MMC_CM_INST * pInst, CARD_STATUS * pCardStatus) {
  int r;
  U32 TimeOut;
  U32 CardErrors;

  TimeOut = FS_MMC_WAIT_READY_TIMEOUT;
  //
  // Check for error in the previously executed command.
  //
  CardErrors = _GetCardErrors(pCardStatus);
  if (CardErrors != 0u) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _WaitForCardReady: Card reports error(s) 0x%x.", CardErrors));
    return 1;         // Error, card failure.
  }
  //
  // Check if the card is ready to receive data.
  //
  if (_IsCardReady(pCardStatus) != 0) {
    return 0;         // OK, the card is ready.
  }
  //
  // Loop to wait for card ready.
  //
  for (;;) {
    //
    // Quit the waiting loop if the card has been removed.
    //
    if (_IsPresent(pInst) == 0) {
      FS_DEBUG_WARN((FS_MTYPE_DRIVER, "MMC_CM: _WaitForCardReady: Card has been removed."));
      r = 1;
      break;                      // Error, card has been removed.
    }
    //
    // Read the status from card.
    //
    r = _ExecSendStatus(pInst, pCardStatus);
    if (r > 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _WaitForCardReady: Card does not respond."));
      r = 1;
      break;                      // Error, could not get status from card.
    }
    //
    // Check for card errors.
    //
    if (r < 0) {
      CardErrors = _GetCardErrors(pCardStatus);
      if (CardErrors != 0u) {
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _WaitForCardReady: Card reports error(s) 0x%x.", CardErrors));
        r = 1;
        break;                    // Error, card failure.
      }
    }
    //
    // Check if the card is ready to receive data.
    //
    if (_IsCardReady(pCardStatus) != 0) {
      r = 0;
      break;                      // OK, the card is ready.
    }
    if (TimeOut == 0u) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _WaitForCardReady: Timeout expired."));
      r = 1;
      break;                      // Error, timeout expired.
    }
    --TimeOut;
  }
  if (r != 0) {
    pInst->HasError = 1;
  }
  return r;
}

/*********************************************************************
*
*       _WaitForCardIdle
*
*  Function description
*    Waits for the card to finish any internal processing.
*
*  Parameters
*    pInst            Driver instance.
*    pCardStatus      [IN]  Card status of the last executed command in the caller.
*                     [OUT] Card status of the last executed command in this function.
*
*  Return value
*    ==0      OK, card state entered.
*    !=0      An error occurred.
*/
static int _WaitForCardIdle(MMC_CM_INST * pInst, CARD_STATUS * pCardStatus) {
  int      r;
  U32      TimeOut;
  U32      CardErrors;
  unsigned CurrentState;

  TimeOut = FS_MMC_WAIT_READY_TIMEOUT;
  //
  // Check for error in the previously executed command.
  //
  CardErrors = _GetCardErrors(pCardStatus);
  if (CardErrors != 0u) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _WaitForCardIdle: Card reports error(s) 0x%x.", CardErrors));
    return 1;         // Error, card failure.
  }
  //
  // Check the card state.
  //
  CurrentState = _GetCardCurrentState(pCardStatus);
  if ((CurrentState == CARD_STATE_STBY) || (CurrentState == CARD_STATE_TRAN)) {
    return 0;         // OK, the card enter the requested state.
  }
  //
  // Loop until the card enters the requested state or an error occurs.
  //
  for (;;) {
    //
    // Quit the waiting loop if the card has been removed.
    //
    if (_IsPresent(pInst) == 0) {
      FS_DEBUG_WARN((FS_MTYPE_DRIVER, "MMC_CM: _WaitForCardIdle: Card has been removed."));
      r = 1;
      break;                      // Error, card has been removed.
    }
    //
    // Read the status from card.
    //
    r = _ExecSendStatus(pInst, pCardStatus);
    if (r > 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _WaitForCardIdle: Card does not respond."));
      r = 1;
      break;                      // Error, could not get status from card.
    }
    //
    // Check for card errors.
    //
    if (r < 0) {
      CardErrors = _GetCardErrors(pCardStatus);
      if (CardErrors != 0u) {
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _WaitForCardIdle: Card reports error(s) 0x%x.", CardErrors));
        r = 1;
        break;                    // Error, card failure.
      }
    }
    //
    // Check the card state.
    //
    CurrentState = _GetCardCurrentState(pCardStatus);
    if ((CurrentState == CARD_STATE_STBY) || (CurrentState == CARD_STATE_TRAN)) {
      r = 0;
      break;                      // OK, the card entered the requested state.
    }
    if (TimeOut == 0u) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _WaitForCardIdle: Timeout expired."));
      r = 1;
      break;                      // Error, timeout expired.
    }
    --TimeOut;
  }
  if (r != 0) {
    pInst->HasError = 1;
  }
  return r;
}

/*********************************************************************
*
*       _WaitForCardState
*
*  Function description
*    Waits for the card to enter the specified state.
*
*  Parameters
*    pInst            Driver instance.
*    pCardStatus      [IN]  Card status of the last executed command in the caller.
*                     [OUT] Card status of the last executed command in this function.
*    RequestedState   State of the card to wait for.
*
*  Return value
*    ==0      OK, card state entered.
*    !=0      An error occurred.
*/
static int _WaitForCardState(MMC_CM_INST * pInst, CARD_STATUS * pCardStatus, unsigned RequestedState) {
  int      r;
  U32      TimeOut;
  U32      CardErrors;
  unsigned CurrentState;

  TimeOut = FS_MMC_WAIT_READY_TIMEOUT;
  //
  // Check for error in the previously executed command.
  //
  CardErrors = _GetCardErrors(pCardStatus);
  if (CardErrors != 0u) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _WaitForCardState: Card reports error(s) 0x%x.", CardErrors));
    return 1;         // Error, card failure.
  }
  //
  // Check the card state.
  //
  CurrentState = _GetCardCurrentState(pCardStatus);
  if (CurrentState == RequestedState) {
    return 0;         // OK, the card enter the requested state.
  }
  //
  // Loop until the card enters the requested state or an error occurs.
  //
  for (;;) {
    //
    // Quit the waiting loop if the card has been removed.
    //
    if (_IsPresent(pInst) == 0) {
      FS_DEBUG_WARN((FS_MTYPE_DRIVER, "MMC_CM: _WaitForCardState: Card has been removed."));
      r = 1;
      break;                      // Error, card has been removed.
    }
    //
    // Read the status from card.
    //
    r = _ExecSendStatus(pInst, pCardStatus);
    if (r > 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _WaitForCardState: Card does not respond."));
      r = 1;
      break;                      // Error, could not get status from card.
    }
    //
    // Check for card errors.
    //
    if (r < 0) {
      CardErrors = _GetCardErrors(pCardStatus);
      if (CardErrors != 0u) {
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _WaitForCardState: Card reports error(s) 0x%x.", CardErrors));
        r = 1;
        break;                    // Error, card failure.
      }
    }
    //
    // Check the card state.
    //
    CurrentState = _GetCardCurrentState(pCardStatus);
    if (CurrentState == RequestedState) {
      r = 0;
      break;                      // OK, the card entered the requested state.
    }
    if (TimeOut == 0u) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _WaitForCardState: Timeout expired."));
      r = 1;
      break;                      // Error, timeout expired.
    }
    --TimeOut;
  }
  if (r != 0) {
    pInst->HasError = 1;
  }
  return r;
}

/*********************************************************************
*
*       _SelectCard
*
*  Function description
*    Requests the card to move to Transfer state.
*/
static int _SelectCard(MMC_CM_INST * pInst, CARD_STATUS * pCardStatus) {
  int      r;
  unsigned rca;
  unsigned TimeOut;

  rca = pInst->Rca;
  //
  // Try to select the card.
  //
  TimeOut = FS_MMC_SELECT_CARD_TIMEOUT;
  for (;;) {
    //
    // Quit the waiting loop if the card has been removed.
    //
    r = _IsPresent(pInst);
    if (r == 0) {
      FS_DEBUG_WARN((FS_MTYPE_DRIVER, "MMC_CM: _SelectCard: Card has been removed."));
      r = 1;
      pInst->HasError = 1;
      break;          // Error, card has been removed.
    }
    r = _ExecSelectCard(pInst, rca, pCardStatus);
    if (r == 0) {
      break;          // OK, the card selected.
    }
    if (TimeOut == 0u) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _SelectCard: Timeout expired."));
      break;          // Error, could not select card.
    }
    --TimeOut;
  }
  return r;
}

/*********************************************************************
*
*       _SelectCardIfRequired
*
*  Function description
*    Checks the card status and requests the card to move to Transfer state
*    if the card is in a different state.
*
*  Return value
*    ==0        OK, card is in Transfer state.
*    !=0        An error occurred.
*/
static int _SelectCardIfRequired(MMC_CM_INST * pInst, CARD_STATUS * pCardStatus) {
  int      r;
  unsigned CurrentState;

  r = _ExecSendStatus(pInst, pCardStatus);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _SelectCardIfRequired: Could not get card status."));
    return r;
  }
  CurrentState = _GetCardCurrentState(pCardStatus);
  if ((CurrentState == CARD_STATE_TRAN) ||
      (CurrentState == CARD_STATE_RCV)  ||
      (CurrentState == CARD_STATE_PRG)) {
    return 0;             // Card already in Transfer state. Nothing to do.
  }
  r = _SelectCard(pInst, pCardStatus);
  return r;
}

/*********************************************************************
*
*       _SelectCardWithBusyWait
*
*  Function description
*    Requests the card to move to Transfer state and waits for
*    the card to become ready for data transfer.
*/
static int _SelectCardWithBusyWait(MMC_CM_INST * pInst, CARD_STATUS * pCardStatus) {
  int      r;

  r = _SelectCardIfRequired(pInst, pCardStatus);
  if (r == 0) {
    //
    // OK, the card is selected. Wait for the card to become ready.
    //
    r = _WaitForCardReady(pInst, pCardStatus);
  }
  return r;
}

/*********************************************************************
*
*       _DeSelectCard
*
*  Function description
*    Requests the card to move to Stand-by State.
*
*  Return value
*    ==0        OK, card is in Stand-by State
*    !=0        An error occurred
*/
static int _DeSelectCard(MMC_CM_INST * pInst, CARD_STATUS * pCardStatus) {
  int      r;
  unsigned rca;
  unsigned TimeOut;
  unsigned CurrentState;

  rca = DEFAULT_RCA_DESELECT;
  //
  // Try to select the card.
  //
  TimeOut = FS_MMC_SELECT_CARD_TIMEOUT;
  for (;;) {
    //
    // Quit the waiting loop if the card has been removed.
    //
    r = _IsPresent(pInst);
    if (r == 0) {
      FS_DEBUG_WARN((FS_MTYPE_DRIVER, "MMC_CM: _DeSelectCard: Card has been removed."));
      r = 1;
      pInst->HasError = 1;
      break;          // Error, card has been removed.
    }
    (void)_ExecSelectCard(pInst, rca, pCardStatus);
    r = _ExecSendStatus(pInst, pCardStatus);
    if (r == 0) {
      CurrentState = _GetCardCurrentState(pCardStatus);
      if (CurrentState == CARD_STATE_STBY) {
        break;        // OK, the card selected.
      }
    }
    if (TimeOut == 0u) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _DeSelectCard: Timeout expired."));
      break;          // Error, could not select card.
    }
    --TimeOut;
  }
  return r;
}

/*********************************************************************
*
*       _DeSelectCardIfRequired
*
*  Function description
*    Checks the card status and requests the card to move to Stand-by State
*    if the card is in a different state.
*
*  Return value
*    ==0        OK, card is in Stand-by State
*    !=0        An error occurred
*/
static int _DeSelectCardIfRequired(MMC_CM_INST * pInst, CARD_STATUS * pCardStatus) {
  int      r;
  unsigned CurrentState;

  r = _ExecSendStatus(pInst, pCardStatus);
  if (r != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _DeSelectCardIfRequired: Could not get card status."));
    return r;
  }
  CurrentState = _GetCardCurrentState(pCardStatus);
  if (CurrentState == CARD_STATE_STBY) {
    return 0;             // Card already in Stand-by State. Nothing to do.
  }
  r = _DeSelectCard(pInst, pCardStatus);
  return r;
}

/*********************************************************************
*
*       _EnterPowerSaveModeIfRequired
*
*  Function description
*    Puts the device to sleep to save power.
*/
static int _EnterPowerSaveModeIfRequired(MMC_CM_INST * pInst, CARD_STATUS * pCardStatus) {
  int r;

  r = 0;              // Set to indicate success.
#if FS_MMC_SUPPORT_POWER_SAVE
  if (pInst->IsPowerSaveModeAllowed != 0u) {
    switch (pInst->CardType) {
#if FS_MMC_SUPPORT_MMC
    case FS_MMC_CARD_TYPE_MMC:
      if (pInst->IsPowerSaveModeActive == 0u) {
        r = _DeSelectCardIfRequired(pInst, pCardStatus);
        if (r == 0) {
          r = _ExecSleepAwake(pInst, 1, pCardStatus);
          if (r == 0) {
            pInst->IsPowerSaveModeActive = 1;
          }
        }
      }
      break;
#endif // FS_MMC_SUPPORT_MMC
#if FS_MMC_SUPPORT_SD
    case FS_MMC_CARD_TYPE_SD:
      r = _DeSelectCardIfRequired(pInst, pCardStatus);
      break;
#endif // FS_MMC_SUPPORT_SD
    case FS_MMC_CARD_TYPE_UNKNOWN:
      //lint through
    default:
      r = 1;              // Error invalid card type.
      break;
    }
  }
#else
  FS_USE_PARA(pInst);
  FS_USE_PARA(pCardStatus);
#endif  // FS_MMC_SUPPORT_POWER_SAVE
  return r;
}

/*********************************************************************
*
*       _LeavePowerSaveModeIfRequired
*
*  Function description
*    Wakes up the device.
*/
static int _LeavePowerSaveModeIfRequired(MMC_CM_INST * pInst, CARD_STATUS * pCardStatus) {
  int r;

  r = 0;              // Set to indicate success.
#if (FS_MMC_SUPPORT_MMC != 0) && (FS_MMC_SUPPORT_POWER_SAVE != 0)
  if (pInst->CardType == (U8)FS_MMC_CARD_TYPE_MMC) {      // This feature is supported only by MMC devices.
    if (pInst->IsPowerSaveModeActive != 0u) {
      r = _ExecSleepAwake(pInst, 0, pCardStatus);
      if (r == 0) {
        pInst->IsPowerSaveModeActive = 0;
      }
    }
  }
#else
  FS_USE_PARA(pInst);
  FS_USE_PARA(pCardStatus);
#endif  // FS_MMC_SUPPORT_MMC != 0 && FS_MMC_SUPPORT_POWER_SAVE != 0
  return r;
}

#if FS_MMC_SUPPORT_SD

/*********************************************************************
*
*       _GetFuncSupport
*
*  Function description
*    Returns the flags indicating which function within group is supported.
*
*  Parameters
*    GroupIndex     Index of the function group (0-based).
*    pSwitchResp    Data received as response to CMD6.
*
*  Return value
*    Status flags.
*/
static unsigned _GetFuncSupport(unsigned GroupIndex, const U32 * pSwitchResp) {
  unsigned Status;
  unsigned BitOffFirst;
  unsigned BitOffLast;

  Status = 0;
  if (GroupIndex < FUNC_GROUP_MAX) {
    BitOffFirst = BIT_OFF_FUNC_SUPPORT + (GroupIndex << LD_NUM_BITS_FUNC_SUPPORT);
    BitOffLast  = BitOffFirst + ((1u << LD_NUM_BITS_FUNC_SUPPORT) - 1u);
    Status = _GetBits((const U8 *)pSwitchResp, BitOffFirst, BitOffLast, NUM_BYTES_SWITCH_RESP);
  }
  return Status;
}

/*********************************************************************
*
*       _GetFuncBusy
*
*  Function description
*    Returns the flags indicating which function within group is busy.
*
*  Parameters
*    GroupIndex     Index of the function group (0-based).
*    pSwitchResp    Data received as response to CMD6.
*
*  Return value
*    Status flags.
*/
static unsigned _GetFuncBusy(unsigned GroupIndex, const U32 * pSwitchResp) {
  unsigned Status;
  unsigned BitOffFirst;
  unsigned BitOffLast;

  Status = 0;
  if (GroupIndex < FUNC_GROUP_MAX) {
    BitOffFirst = BIT_OFF_FUNC_BUSY + (GroupIndex << LD_NUM_BITS_FUNC_BUSY);
    BitOffLast  = BitOffFirst + ((1u << LD_NUM_BITS_FUNC_BUSY) - 1u);
    Status = _GetBits((const U8 *)pSwitchResp, BitOffFirst, BitOffLast, NUM_BYTES_SWITCH_RESP);
  }
  return Status;
}

/*********************************************************************
*
*       _GetFuncResult
*
*  Function description
*    Returns the flags indicating the execution result of a function within a group.
*
*  Parameters
*    GroupIndex     Index of the function group (0-based).
*    pSwitchResp    Data received as response to CMD6.
*
*  Return value
*    Status flags.
*/
static unsigned _GetFuncResult(unsigned GroupIndex, const U32 * pSwitchResp) {
  unsigned Status;
  unsigned BitOffFirst;
  unsigned BitOffLast;

  Status = 0;
  if (GroupIndex < FUNC_GROUP_MAX) {
    BitOffFirst = BIT_OFF_FUNC_RESULT + (GroupIndex << LD_NUM_BITS_FUNC_RESULT);
    BitOffLast  = BitOffFirst + ((1u << LD_NUM_BITS_FUNC_RESULT) - 1u);
    Status = _GetBits((const U8 *)pSwitchResp, BitOffFirst, BitOffLast, NUM_BYTES_SWITCH_RESP);
  }
  return Status;
}

/*********************************************************************
*
*       _CheckWaitFunc
*
*  Function description
*    Checks if a card function is supported  and waits for that
*    function to become ready.
*
*  Parameters
*    pInst          Driver instance.
*    GroupIndex     Index of the function group.
*    Value          Function value to be checked.
*    pCardStatus    [OUT] Status returned by the card.
*
*  Return values
*    ==1    Card supports the function is ready and the function is ready.
*    ==0    Card supports does not support the specified function.
*    < 0    An error occurred.
*/
static int _CheckWaitFunc(MMC_CM_INST * pInst, unsigned GroupIndex, unsigned Value, CARD_STATUS * pCardStatus) {
  int        r;
  int        NumRetries;
  U32      * pSwitchResp;
  unsigned   SuppFunc;
  unsigned   BusyStatus;

  pSwitchResp = _AllocBuffer(NUM_BYTES_SWITCH_RESP);
  if (pSwitchResp == NULL) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _CheckWaitFunc: Could not allocate buffer."));
    return -1;                    // Error, could not allocate buffer memory.
  }
  //
  // We have to retry here in order to wait for the function to become ready.
  //
  NumRetries = NUM_RETRIES_SWITCH;
  for (;;) {
    r = _ExecSwitchFunc(pInst, 0, (int)GroupIndex, (U8)Value, pSwitchResp, pCardStatus);     // 0 means query.
    if (r == 0) {
      SuppFunc   = _GetFuncSupport(GroupIndex, pSwitchResp);
      BusyStatus = _GetFuncBusy(GroupIndex, pSwitchResp);
      if ((SuppFunc & (1uL << Value)) == 0u) {
        break;                    // OK, the card does not support the specified function.
      }
      if ((BusyStatus & (1uL << Value)) == 0u) {
        r = 1;                    // OK, the function is supported and is not busy.
        break;
      }
    }
    if (_IsPresent(pInst) == 0) {
      FS_DEBUG_WARN((FS_MTYPE_DRIVER, "MMC_CM: _CheckWaitFunc: Card has been removed."));
      r = -1;
      break;                      // Error, card has been removed.
    }
    if (NumRetries == 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _CheckWaitFunc: Timeout expired."));
      r = -1;
      break;                      // Error, timeout expired.
    }
    --NumRetries;
  }
  FREE_BUFFER(&pSwitchResp);
  return r;
}

/*********************************************************************
*
*       _SwitchFunc
*
*  Function description
*    Requests the card to switch to the specified function.
*
*  Parameters
*    pInst        Driver instance.
*    GroupIndex   Index of the function group to be changed.
*    Value        Function value to be set.
*    pCardStatus    [OUT] Status returned by the card.
*
*  Return values
*    ==0    Card successfully switched to high speed mode
*    !=0    An error occurred
*/
static int _SwitchFunc(MMC_CM_INST * pInst, unsigned GroupIndex, unsigned Value, CARD_STATUS * pCardStatus) {
  int        r;
  U32      * pSwitchResp;
  unsigned   Result;

  pSwitchResp = _AllocBuffer(NUM_BYTES_SWITCH_RESP);
  if (pSwitchResp == NULL) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _SwitchFunc: Could not allocate buffer."));
    return 1;             // Error, could not allocate buffer.
  }
  r = _ExecSwitchFunc(pInst, 1, (int)GroupIndex, (U8)Value, pSwitchResp, pCardStatus);       // 1 means set.
  if (r == 0) {
    Result = _GetFuncResult(GroupIndex, pSwitchResp);
    //
    // If the card switches successfully to high-speed mode, it will respond with the function number.
    //
    if (Result == Value) {
      r = 0;                  // Card successfully switched to high speed mode.
    }
  }
  FREE_BUFFER(&pSwitchResp);
  return r;
}

/*********************************************************************
*
*       _SwitchToHSModeSD
*
*  Function description
*    Tells the SD card to change the timing of the signals to high speed mode.
*
*  Return values
*    ==1    Card switched to high speed mode
*    ==0    Card stays in standard speed mode
*    < 0    An error occurred
*/
static int _SwitchToHSModeSD(MMC_CM_INST * pInst, const U32 * pSCR, CARD_STATUS * pCardStatus) {
  int r;
  U8  SpecVersion;

  //
  // Check the version of the SD specification supported by the card.
  // Only SD cards that conform to a version of SD specification > 2.00
  // can be switched to high speed mode.
  //
  SpecVersion = SCR_SD_SPEC(pSCR);
  if (SpecVersion < SD_SPEC_VER_200) {
    FS_DEBUG_WARN((FS_MTYPE_DRIVER, "MMC_CM: _SwitchToHSModeSD: High speed mode is not supported by this card."));
    return 0;               // OK, the card does not support the high speed mode.
  }
  //
  // Check if the high speed mode is supported by the card.
  //
  r = _CheckWaitFunc(pInst, FUNC_GROUP_ACCESS_MODE, ACCESS_MODE_HIGH_SPEED, pCardStatus);
  if (r <= 0) {
    return r;               // Error or card does not support the high speed mode.
  }
  //
  // High speed mode is supported. Try to switch card to high speed mode.
  //
  r = _SwitchFunc(pInst, FUNC_GROUP_ACCESS_MODE, ACCESS_MODE_HIGH_SPEED, pCardStatus);
  if (r != 0) {
    r = -1;                 // Error, could not switch to high speed mode.
  } else {
    r = 1;                  // OK, card switched to high speed mode.
  }
  return r;
}

#endif  // FS_MMC_SUPPORT_SD

#if FS_MMC_SUPPORT_MMC

/*********************************************************************
*
*       _SetBusWidthMMC
*
*  Function description
*    Configures the number of data lines an eMMC device uses for the data transfer.
*
*  Return value
*    > 0    Configured bus width in bits.
*    ==0    An error occurred.
*/
static int _SetBusWidthMMC(MMC_CM_INST * pInst, int BusWidth, unsigned ClkFlags, CARD_STATUS * pCardStatus) {
  int r;
  int Result;
  int Value;

  r = 0;                  // Set to indicate an error.
  switch (BusWidth) {
  default:
  case 1:
    Value = EXT_CSD_BUS_WIDTH_1BIT;
    break;
  case 4:
    Value = EXT_CSD_BUS_WIDTH_4BIT;
    if ((ClkFlags & FS_MMC_CLK_FLAG_DDR_MODE) != 0u) {
      Value = EXT_CSD_BUS_WIDTH_4BIT_DDR;
    }
    break;
  case 8:
    Value = EXT_CSD_BUS_WIDTH_8BIT;
    if ((ClkFlags & FS_MMC_CLK_FLAG_DDR_MODE) != 0u) {
      if ((ClkFlags & FS_MMC_CLK_FLAG_ENHANCED_STROBE) != 0u) {
        Value = EXT_CSD_BUS_WIDTH_8BIT_DDR_ES;
      } else {
        Value = EXT_CSD_BUS_WIDTH_8BIT_DDR;
      }
    }
    break;
  }
  Result = _WriteExtCSDByte(pInst, OFF_EXT_CSD_BUS_WIDTH, Value, pCardStatus);
  if (Result == 0) {
    //
    // We need to wait here for the card to switch
    // the bus width as stated in [3] chapter 7.5.1
    //
    Result = _WaitForCardReady(pInst, pCardStatus);
    if (Result == 0) {
      r = BusWidth;     // OK, changed the with of the bus in the card.
    }
  }
  return r;
}

/*********************************************************************
*
*       _SetAccessModeMMC
*
*  Function description
*    Configures the access mode and the driver strength of an eMMC device.
*
*  Return value
*    ==0    OK, access mode configured.
*    !=0    An error occurred.
*/
static int _SetAccessModeMMC(MMC_CM_INST * pInst, int AccessMode, CARD_STATUS * pCardStatus) {
  int      r;
  unsigned Value;
#if FS_MMC_SUPPORT_UHS
  unsigned DriverStrength;
#endif // FS_MMC_SUPPORT_UHS

  Value          = (unsigned)AccessMode;
#if FS_MMC_SUPPORT_UHS
  DriverStrength = pInst->DriverStrengthRequested;
  Value         |= DriverStrength << 4;
#endif // FS_MMC_SUPPORT_UHS
  r = _WriteExtCSDByte(pInst, OFF_EXT_CSD_HS_TIMING, (int)Value, pCardStatus);
#if FS_MMC_SUPPORT_UHS
  if (r == 0) {
    pInst->DriverStrengthActive = (U8)DriverStrength;
  }
#endif // FS_MMC_SUPPORT_UHS
  return r;
}

#endif  // FS_MMC_SUPPORT_MMC

#if FS_MMC_SUPPORT_SD

/*********************************************************************
*
*      _IsClass10Card
*
*  Function description
*    Checks if the SD card supports the features of the class 10
*    as defined by the SD specification.
*
*  Parameters
*    pCSD     [IN] Contents of the CSD card register.
*
*  Return value
*    ==0      Not a class 10 SD card.
*    ==1      Class 10 SD card.
*
*  Additional information
*    Typically, this function is used to check if the SD card
*    supports CMD6 that is used to enable optional functionality.
*/
static int _IsClass10Card(const CSD_RESPONSE * pCSD) {
  U32 CCCSupported;
  int r;

  r = 0;        // This is not a class 10 card.
  CCCSupported = CSD_CCC_CLASSES(pCSD);
  if ((CCCSupported & (1uL << 10)) != 0u) {
    r = 1;      // This is a class 10 card.
  }
  return r;
}

#endif // FS_MMC_SUPPORT_SD

/*********************************************************************
*
*      _SwitchToAccessModeHSIfSupported
*
*  Function description
*    Requests the card to enable the data transfer at higher clock frequencies.
*    - SD cards  > 25 MHz
*    - MMC cards > 26 MHz
*
*  Return value
*    ==1      Card switched to high speed mode
*    ==0      Card communicates in standard speed mode
*    < 0      An error occurred
*/
static int _SwitchToAccessModeHSIfSupported(MMC_CM_INST * pInst, CSD_RESPONSE * pCSD, const U32 * pSCR, CARD_STATUS * pCardStatus) {      //lint -efunc(818, _SwitchToAccessModeHSIfSupported) Pointer parameter 'pCSD' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory]. Rationale: not possible because the data of the CSD register is read from SD card and stored to pCSD.
  int r;
  int Result;
  int IsHSModeAllowed;
  int CardType;

  r               = 0;          // Card works in default speed mode.
  IsHSModeAllowed = (int)pInst->IsHSModeAllowed;
  CardType        = (int)pInst->CardType;
  if (IsHSModeAllowed != 0) {
    switch (CardType) {
#if FS_MMC_SUPPORT_SD
    case FS_MMC_CARD_TYPE_SD:
      {
        //
        // The command used for switching to high speed mode
        // is implemented only by SD cards that support class
        // 10 commands.
        //
        if (_IsClass10Card(pCSD) != 0) {
          //
          // The command for switching in high speed mode is accepted
          // only when the SD card is in Transfer State.
          //
          Result = _SelectCardWithBusyWait(pInst, pCardStatus);
          if (Result == 0) {
            r = _SwitchToHSModeSD(pInst, pSCR, pCardStatus);
          } else {
            r = -1;             // Error while selecting the card.
          }
          if (r == 1) {         // Card switched to high speed mode?
            r = -1;             // Set to indicate error.
            Result = _DeSelectCard(pInst, pCardStatus);
            if (Result == 0) {
              //
              // We have to read the CSD register again because the SD card updates
              // the TRAN_SPEED entry to reflect the new maximum speed.
              //
              Result = _ExecSendCSD(pInst, pCSD);
              if (Result == 0) {
                r = 1;          // OK, card switched to high speed mode.
              }
            }
          }
        }
      }
      break;
#endif  // FS_MMC_SUPPORT_SD
#if FS_MMC_SUPPORT_MMC
    case FS_MMC_CARD_TYPE_MMC:
      FS_USE_PARA(pSCR);
      {
        U32 SpecVersion;

        //
        // Only MMCplus cards and eMMC devices can operate in high speed mode.
        //
        SpecVersion = CSD_SPEC_VERS(pCSD);
        if (SpecVersion >= MMC_SPEC_VER_4) {
          r = -1;             // Set to indicate an error.
          Result = _SelectCardWithBusyWait(pInst, pCardStatus);
          if (Result == 0) {
            Result = _SetAccessModeMMC(pInst, EXT_CSD_HS_TIMING_HIGH_SPEED, pCardStatus);
            if (Result == 0) {
              r = 1;          // OK, card switched to high speed mode.
            }
          }
        }
      }
      break;
#endif      // FS_MMC_SUPPORT_MMC
    default:
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _SwitchToAccessModeHSIfSupported: Invalid card type %d.\n", CardType));
      r = -1;
      break;
    }
  }
  return r;
}

#if FS_MMC_SUPPORT_MMC

/*********************************************************************
*
*       _Test4bitBus
*
*  Function description
*    Tests if the data bus is 4 bits wide as described on [1] section A.8.3
*
*  Return values
*    ==0    Data bus is 4 bits wide
*    ==1    Data bus is not 4 bits wide or an error occurred
*/
static int _Test4bitBus(MMC_CM_INST * pInst, CARD_STATUS * pCardStatus) {
#if FS_MMC_TEST_BUS_WIDTH
  U32   TestPattern;
  U32   CardReply;
  int   r;
  U8  * pTestPattern;
  U8  * pCardReply;

  TestPattern   = 0;
  CardReply     = 0;
  pTestPattern  = (U8 *)&TestPattern;
  pCardReply    = (U8 *)&CardReply;
  *pTestPattern = 0xA5;
  r = _ExecBusTestW(pInst, &TestPattern, sizeof(TestPattern), 4, pCardStatus);
  if (r < 0) {
    return 1;           // Error, could not send the test pattern to card.
  }
  //
  // The standard [1] says that we have to wait here at least Nrc=8 clock cycles.
  //
  _Delay(pInst, 1);
  r = _ExecBusTestR(pInst, &CardReply, sizeof(CardReply), 4, pCardStatus);
  if (r < 0) {
    return 1;           // Error, could not receive the test pattern from card.
  }
  if ((*pTestPattern ^ *pCardReply) == 0xFFu) {
    return 0;           // OK, the data bus is 4-bits wide.
  }
  return 1;             // Data bus is not 4-bits wide.
#else
  FS_USE_PARA(pInst);
  FS_USE_PARA(pCardStatus);
  return 0;
#endif  // FS_MMC_TEST_BUS_WIDTH
}

/*********************************************************************
*
*       _Test8bitBus
*
*  Function description
*    Tests if the data bus is 8 bits wide as described on [1] section A.8.3
*
*  Return values
*    ==0   Data bus is 8 bits wide
*    !=0   Data bus is not 8 bits wide or an error occurred
*/
static int _Test8bitBus(MMC_CM_INST * pInst, CARD_STATUS * pCardStatus) {
#if FS_MMC_TEST_BUS_WIDTH
  U32   aTestPattern[2];
  U32   aCardReply[2];
  int   r;
  U8  * pTestPattern;
  U8  * pCardReply;

  FS_MEMSET(aTestPattern, 0, sizeof(aTestPattern));
  FS_MEMSET(aCardReply, 0, sizeof(aCardReply));
  pTestPattern        = (U8 *)aTestPattern;
  pCardReply          = (U8 *)aCardReply;
  *pTestPattern       = 0xAA;
  *(pTestPattern + 1) = 0x55;
  r = _ExecBusTestW(pInst, aTestPattern, sizeof(aTestPattern), 8, pCardStatus);
  if (r < 0) {
    return 1;           // Error, could not send the test pattern.
  }
  //
  // The standard [1] says that we have to wait here at least Nrc=8 clock cycles.
  //
  _Delay(pInst, 1);
  r = _ExecBusTestR(pInst, aCardReply, sizeof(aCardReply), 8, pCardStatus);
  if (r < 0) {
    return 1;           // Error, could not send the test pattern.
  }
  if (((*pTestPattern ^ *pCardReply) == 0xFFu) &&
      ((*(pTestPattern + 1) ^ *(pCardReply + 1)) == 0xFFu)) {
    return 0;           // OK, the data bus is 8-bit wide.
  }
  return 1;             // The data bus is not 8-bit wide.
#else
  FS_USE_PARA(pInst);
  FS_USE_PARA(pCardStatus);
  return 0;
#endif  // FS_MMC_TEST_BUS_WIDTH
}

#endif  // FS_MMC_SUPPORT_MMC

/*********************************************************************
*
*      _UnlockForced
*
*  Function description
*    Unlocks an SD card by erasing all the data on it.
*/
static int _UnlockForced(MMC_CM_INST * pInst, CARD_STATUS * pCardStatus) {
  int r;

  //
  // Set the number of bytes to be transferred.
  //
  r = _ExecSetBlockLen(pInst, NUM_BYTES_LOCK_UNLOCK, pCardStatus);
  if (r != 0) {
    return 1;         // Error, invalid response or card reports error.
  }
  //
  // Perform the lock/unlock operation.
  //
  r = _ExecLockUnlock(pInst, 1, 0, 0, 0, 0, NULL, 0, NULL, pCardStatus);
  if (r != 0) {
    return 1;         // Error, unlock operation failed
  }
  //
  // Restore the block length for the read/write operations.
  //
  r = _ExecSetBlockLen(pInst, BYTES_PER_SECTOR, pCardStatus);
  if (r != 0) {
    return 1;         // Error, invalid response or card reports error.
  }
  //
  // Get the status from card to check if the operation succeeded.
  //
  r = _ExecSendStatus(pInst, pCardStatus);
  if (r != 0) {
    return 1;         // Error, invalid response or card reports error.
  }
  return 0;           // OK, command sent.
}

#if FS_MMC_SUPPORT_MMC

/*********************************************************************
*
*      _EraseMMC
*
*  Function description
*    Erases the contents of the specified sectors on a MMC card.
*
*  Parameters
*    pInst          Driver instance.
*    StartSector    Index of the first sector to be erased.
*    NumSectors     Number of sectors to be erased.
*    MarkForErase   Set to 1 if a trim operation should be performed instead of erase.
*    pCardStatus    [OUT] Card status.
*
*  Return value
*    ==0      OK, sectors erased.
*    !=0      An error occurred.
*/
static int _EraseMMC(MMC_CM_INST * pInst, U32 StartSector, U32 NumSectors, int MarkForErase, CARD_STATUS * pCardStatus) {
  U32 StartAddr;
  U32 EndAddr;
  int r;

  //
  // Determine the range of sectors to be erased.
  //
  StartAddr = StartSector;
  EndAddr   = StartSector + NumSectors - 1u;
  //
  // Set the start address of the block to be erased.
  //
  r = _ExecEraseGroupStart(pInst, StartAddr, pCardStatus);
  if (r != 0) {
    return 1;         // Error, invalid response or card reports error.
  }
  //
  // Set the end address of the block to be erased.
  //
  r = _ExecEraseGroupEnd(pInst, EndAddr, pCardStatus);
  if (r != 0) {
    return 1;         // Error, invalid response or card reports error.
  }
  //
  // Start the erase operation.
  //
  r = _ExecEraseMMC(pInst, 0, 0, MarkForErase, pCardStatus);
  if (r != 0) {
    return 1;         // Error, invalid response or card reports error.
  }
  //
  // Wait for the erase operation to finish. The HW layer should block while the D0 data line is 0.
  //
  r = _ExecSendStatus(pInst, pCardStatus);
  if (r != 0) {
    return 1;         // Error, invalid response or card reports error.
  }
  return 0;           // OK, sectors erased.
}

#endif  // FS_MMC_SUPPORT_MMC

/*********************************************************************
*
*      _Erase
*
*  Function description
*    Erases sector contents.
*
*  Notes
*     (1) SD cards are not supported yet.
*/
static int _Erase(MMC_CM_INST * pInst, U32 StartSector, U32 NumSectors, CARD_STATUS * pCardStatus) {      //lint -efunc(818, _Erase) Pointer parameter 'pInst' could be declared as pointing to const. Rationale: the driver instance has to be writtable when the support for MMC devices is enabled.
  int r;
  int CardType;

  CardType = (int)pInst->CardType;
  switch (CardType) {
#if FS_MMC_SUPPORT_SD
  case FS_MMC_CARD_TYPE_SD:
    FS_USE_PARA(StartSector);
    FS_USE_PARA(NumSectors);
    FS_USE_PARA(pCardStatus);
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _Erase: Operation not supported for SD cards."));
    r = 1;        // Error, this operation not supported for SD cards, yet.
    break;
#endif  // FS_MMC_SUPPORT_SD
#if FS_MMC_SUPPORT_MMC
  case FS_MMC_CARD_TYPE_MMC:
    r = _EraseMMC(pInst, StartSector, NumSectors, 0, pCardStatus);        // 0 tells the device to perform an erase operation.
    break;
#endif    // FS_MMC_SUPPORT_MMC
  default:
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _Erase: Invalid card type %d.", CardType));
    r = 1;
    break;
  }
  return r;
}

#if FS_MMC_SUPPORT_TRIM

/*********************************************************************
*
*      _Trim
*
*  Function description
*    Marks the sectors as not in use.
*
*  Notes
*     (1) SD cards are not supported.
*/
static int _Trim(MMC_CM_INST * pInst, U32 StartSector, U32 NumSectors, CARD_STATUS * pCardStatus) {
  int r;
  int CardType;

  CardType = (int)pInst->CardType;
  switch (CardType) {
#if FS_MMC_SUPPORT_SD
  case FS_MMC_CARD_TYPE_SD:
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _Trim: Operation not supported for SD cards."));
    r = 1;        // Error, this operation not supported for SD cards.
    break;
#endif  // FS_MMC_SUPPORT_SD
#if FS_MMC_SUPPORT_MMC
  case FS_MMC_CARD_TYPE_MMC:
    r = _EraseMMC(pInst, StartSector, NumSectors, 1, pCardStatus);        // 1 tells the device to perform a trim operation.
    break;
#endif    // FS_MMC_SUPPORT_MMC
  default:
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _Trim: Invalid card type %d.", CardType));
    r = 1;
    break;
  }
  return r;
}

#endif  // FS_MMC_SUPPORT_TRIM

/*********************************************************************
*
*       _SetTransferSpeed
*
*  Function description
*    Configures the transfer speed in the hardware layer.
*
*  Parameters
*    pInst        Driver instance.
*    Freq_kHz     Clock frequency in kHz.
*    ClkFlags     Additional clock generation options.
*
*  Return value
*    !=0        OK, current clock frequency in kHz.
*    ==0        An error occurred.
*/
static U32 _SetTransferSpeed(const MMC_CM_INST * pInst, U32 Freq_kHz, unsigned ClkFlags) {
  U32 FreqAct_kHz;
  U32 TimeOut;

  FreqAct_kHz = _SetMaxSpeed(pInst, Freq_kHz, ClkFlags);
  if (FreqAct_kHz == 0u) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _SetTransferSpeed: Could not set clock frequency."));
  }
  TimeOut = _CalcReadDataTimeOut(FS_MMC_READ_DATA_TIMEOUT, Freq_kHz);
  _SetReadDataTimeOut(pInst, TimeOut);
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: SPEED Req: %u kHz, Act: %u kHz, ClkFlags: 0x%X\n", Freq_kHz, FreqAct_kHz, ClkFlags));
  return FreqAct_kHz;
}

/*********************************************************************
*
*       _ApplyPara
*
*  Function description
*    Configures the driver according to information read from device.
*
*  Parameters
*    pInst    Driver instance.
*    pCSD     [IN]  Data of CSD register.
*    pExtCSD  [IN]  Data EXT_CSD register (only for MMCplus cards and eMMC devices).
*    pSCR     [IN]  Data of SCR register.
*
*  Return value
*    ==0      OK, HW set up acc. to register contents.
*    !=0      An error occurred.
*/
static int _ApplyPara(MMC_CM_INST * pInst, const CSD_RESPONSE * pCSD, const U8 * pExtCSD, const U32 * pSCR) {
  unsigned Index;
  unsigned TranSpeed;
  U32      Factor;
  U32      Freq_kHz;
  U32      NumSectors;
  unsigned CSDVersion;
  unsigned CardTypeMMC;
  int      CardType;
  int      IsHighCapacity;
  int      IsWriteProtected;
  int      r;
  U16      TimeValue;
  int      IsReliableWriteActive;
  int      IsCloseEndedRWSupported;
  unsigned ClkFlags;
  unsigned AccessMode;

  CardType         = (int)pInst->CardType;
  IsHighCapacity   = (int)pInst->IsHighCapacity;
  IsWriteProtected = 0;
  ClkFlags         = 0;
  AccessMode       = pInst->AccessMode;
  //
  // CSD version is only checked for SD card. MMC cards have almost the same
  // CSD structure as SD V1 cards.
  //
  CSDVersion = 0;
  if (CardType == FS_MMC_CARD_TYPE_SD) {
    CSDVersion = CSD_STRUCTURE(pCSD);
  }
  //
  // Calculate maximum communication speed supported by the card.
  //
  if (pExtCSD != NULL) {
    //
    // For MMCplus cards the maximum speed is specified in the CARD_TYPE field
    // of the EXT_CSD register.
    //
    CardTypeMMC = pExtCSD[OFF_EXT_CSD_CARD_TYPE];
    //
    // Choose the highest clock frequency supported by the card.
    //
    Freq_kHz = MAX_FREQ_MMC_DS_KHZ;             // Maximum frequency of a card that is not operating in high-speed mode.
    switch (AccessMode) {
#if FS_MMC_SUPPORT_UHS
    case FS_MMC_ACCESS_MODE_HS400:
      Freq_kHz = MAX_FREQ_MMC_HS400_KHZ;
      ClkFlags = 0
               | FS_MMC_CLK_FLAG_DDR_MODE
               | FS_MMC_CLK_FLAG_STROBE_MODE
               ;
      if (pInst->IsEnhancedStrobeActive != 0u) {
        ClkFlags |= FS_MMC_CLK_FLAG_ENHANCED_STROBE;
      }
      break;
    case FS_MMC_ACCESS_MODE_HS200:
      Freq_kHz = MAX_FREQ_MMC_HS200_KHZ;
      break;
    case FS_MMC_ACCESS_MODE_HS_DDR:
      Freq_kHz = MAX_FREQ_MMC_HS_DDR_KHZ;
      ClkFlags = FS_MMC_CLK_FLAG_DDR_MODE;
      break;
#endif // FS_MMC_SUPPORT_UHS
    case  FS_MMC_ACCESS_MODE_HS:
      if ((CardTypeMMC & (1uL << EXT_CSD_CARD_TYPE_52MHZ_SHIFT)) != 0u) {
        Freq_kHz = MAX_FREQ_MMC_HS_KHZ;
      } else {
        if ((CardTypeMMC & (1uL << EXT_CSD_CARD_TYPE_26MHZ_SHIFT)) != 0u) {
          Freq_kHz = MAX_FREQ_MMC_HS_LEGACY_KHZ;
        }
      }
      break;
    default:
      //
      // Use the default clock frequency for any other access mode.
      //
      break;
    }
  } else {
    TranSpeed = CSD_TRAN_SPEED(pCSD);
    Index     = TranSpeed & 0x03u;
    Freq_kHz  = _aUnit[Index];
    Index     = ((TranSpeed & 0x78u) >> 3);   // Filter frequency bits.
    //
    // Different multiplication factors are used for MMC and SD cards.
    //
    if (CardType == FS_MMC_CARD_TYPE_SD) {
      TimeValue = _aFactorSD[Index];
    } else {
      TimeValue = _aFactorMMC[Index];
    }
    Freq_kHz *= TimeValue;
    //
    // The SD card reports the same maximum frequency for SDR50 and DDR50
    // even when they are actually different. Therefore we have to reduce
    // the frequency to a half here if the DDR mode is active.
    //
    if (AccessMode == FS_MMC_ACCESS_MODE_DDR50) {
      Freq_kHz >>= 1;
      ClkFlags   = FS_MMC_CLK_FLAG_DDR_MODE;
    }
  }
  //
  // Configure the maximum communication speed supported by the card.
  //
  Freq_kHz = _SetTransferSpeed(pInst, Freq_kHz, ClkFlags);
  if (Freq_kHz == 0u) {
    return 1;
  }
  pInst->Freq_kHz = Freq_kHz;
  r          = 0;            // Set to indicate success.
  NumSectors = 0;
  if (CSDVersion == 0u) {
    //
    // Calculate number of sectors available on the medium.
    // The high capacity MMC cards encode the capacity as a number of 512 byte
    // sectors in the EXT_CSD register. This is a 4 byte field.
    //
    if (pExtCSD && (CardType == FS_MMC_CARD_TYPE_MMC) && (IsHighCapacity != 0)) {
      NumSectors  = (U32)pExtCSD[OFF_EXT_CSD_SEC_COUNT];
      NumSectors |= (U32)pExtCSD[OFF_EXT_CSD_SEC_COUNT + 1] << 8;
      NumSectors |= (U32)pExtCSD[OFF_EXT_CSD_SEC_COUNT + 2] << 16;
      NumSectors |= (U32)pExtCSD[OFF_EXT_CSD_SEC_COUNT + 3] << 24;
    } else {
      Factor      = (1uL << CSD_READ_BL_LEN(pCSD)) >> BYTES_PER_SECTOR_SHIFT;
      Factor     *= 1uL << (CSD_C_SIZE_MULT(pCSD) + 2u);
      NumSectors  = CSD_C_SIZE(pCSD) + 1u;
      NumSectors *= Factor;
    }
    IsWriteProtected = (int)CSD_WRITE_PROTECT(pCSD);
    if (IsWriteProtected == 0) {
      if (CardType != FS_MMC_CARD_TYPE_MMC) {
        if (_IsWriteProtected(pInst) != 0) {
          IsWriteProtected = 1;
        }
      }
    }
  } else if (CSDVersion == 1u) {    // Newer SD V2 cards.
    //
    // Calculate number of sectors available on the medium
    //
    NumSectors = (CSD_C_SIZE_V2(pCSD) + 1u) << 10;
    IsWriteProtected = (int)CSD_WRITE_PROTECT(pCSD);
    if (IsWriteProtected == 0) {
      if (_IsWriteProtected(pInst) != 0) {
        IsWriteProtected = 1;
      }
    }
  } else {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _ApplyCSD: Unsupported CSD version."));
    r = 1;
  }
  //
  // Check if reliable write operation should be activated.
  //
  IsReliableWriteActive = 0;
  if (pExtCSD != NULL) {
    if ((pExtCSD[OFF_EXT_WR_REL_PARAM] & (1u << EN_REL_WR_SHIFT)) != 0u) {
      if (pInst->IsReliableWriteAllowed != 0u) {
        IsReliableWriteActive = 1;
      }
    }
  }
  //
  // Check if an close-ended read and write operations are supported.
  //
  IsCloseEndedRWSupported = 1;                  // Close-ended operations are supported by all modern MMC devices.
  if (CardType == FS_MMC_CARD_TYPE_SD) {        // Not all the SD cards support close-ended read and write operations.
    if (pSCR != NULL) {
      IsCloseEndedRWSupported = (int)SCR_SD_CMD23_SUPPORT(pSCR);
    }
  } else {
    if ((CardType == FS_MMC_CARD_TYPE_MMC) && (pExtCSD == NULL)) {
      IsCloseEndedRWSupported = 0;              // Old MMC cards do not support this feature.
    }
  }
  //
  // Store calculated values to driver instance.
  //
  pInst->IsWriteProtected        = (U8)IsWriteProtected;
  pInst->NumSectors              = NumSectors;
  pInst->IsReliableWriteActive   = (U8)IsReliableWriteActive;
  pInst->IsCloseEndedRWSupported = (U8)IsCloseEndedRWSupported;
  return r;
}

#if FS_MMC_SUPPORT_MMC

/*********************************************************************
*
*       _ReadExtCSDIfRequired
*
*  Function description
*    Reads the contents of the EXT_CSD register if the connected
*    storage medium is an MMC card. The function makes sure that
*    the card is put to Transfer State before executing the actual
*    command.
*
*  Parameters
*    pInst        Driver instance.
*    ppExtCSD     [OUT] Pointer to a dynamically allocated buffer
*                 which stores the contents of the EXT_CSD register.
*                 The memory must be freed using FREE_BUFFER().
*    pCardStatus  [OUT] Card current status.
*
*  Return value
*    ==0      OK, register read.
*    !=0      An error occurred.
*/
static int _ReadExtCSDIfRequired(MMC_CM_INST * pInst, U32 ** ppExtCSD, CARD_STATUS * pCardStatus) {
  int   r;
  int   CardType;
  U32 * pExtCSD;

  *ppExtCSD = NULL;
  CardType = (int)pInst->CardType;
  if (CardType != FS_MMC_CARD_TYPE_MMC) {
    return 0;             // OK, not an MMC card.
  }
  pExtCSD = _AllocBuffer(NUM_BYTES_EXT_CSD);
  if (pExtCSD == NULL) {
    return 1;             // Error, could not allocate buffer.
  }
  r = _SelectCardWithBusyWait(pInst, pCardStatus);
  if (r == 0) {
    r = _ExecSendExtCSD(pInst, 0, pExtCSD, pCardStatus);      // 0 means that _ExecSendExtCSD() should has to select the correct bus width.
  }
  if (r != 0) {
    FREE_BUFFER(&pExtCSD);
  } else {
    *ppExtCSD = pExtCSD;
  }
  return r;
}

#endif // FS_MMC_SUPPORT_MMC

#if FS_MMC_SUPPORT_SD

/*********************************************************************
*
*       _SetBusWidthSD
*
*  Function description
*    Configures the width of the communication bus in the SD card.
*
*  Parameters
*    pInst        Driver instance.
*    BusWidth     Width of the data bus in bits. Valid values are 1 and 4.
*    pSCR         [IN]  Contents of the SCR register.
*    pCardStatus  [OUT] Internal status of the card.
*
*  Return value
*    > 0    Configured bus width in bits.
*    ==0    An error occurred.
*/
static int _SetBusWidthSD(MMC_CM_INST * pInst, int BusWidth, const U32 * pSCR, CARD_STATUS * pCardStatus) {
  int   r;
  int   Result;
  U8    BusWidthsSupported;
  U32 * pSDStatus;

  r = 1;              // Per default perform data transfer via 1 line.
  if (_IsCardLocked(pCardStatus) != 0) {
    return r;         // A card in locked state does not respond to some of the commands below.
  }
  if (BusWidth == 4) {
    //
    // Check if the SD card supports 4-bit mode.
    //
    BusWidthsSupported = SCR_SD_BUS_WIDTHS(pSCR);
    if ((BusWidthsSupported & (1u << BUS_WIDTH_4BIT_SHIFT)) == 0u) {
      return r;       // 4-bit mode not supported. Perform data transfer via 1 data line.
    }
  }
  //
  // Try to put the card into 4-bit mode.
  //
  Result = _ExecSetBusWidth(pInst, BusWidth, pCardStatus);
  if (Result != 0) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _SetBusWidthSD: Could not set bus width."));
    return 0;         // Error, could not read SCR.
  }
  _Delay(pInst, 10);  // Give time the card to switch the bus width.
  pSDStatus = _AllocBuffer(NUM_BYTES_SD_STATUS);
  if (pSDStatus == NULL) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _SetBusWidthSD: Could not allocate buffer."));
    return 0;         // Error, could not allocate buffer.
  }
  //
  // Read the SD status to check if SD 4-bit mode is working.
  //
  r = _ExecSDStatus(pInst, 4, pSDStatus, pCardStatus);      // 4 is the number of data lines.
  if (r == 0) {
    FREE_BUFFER(&pSDStatus);
    return 4;         // OK, switched successfully to 4-bit mode.
  }
  //
  // Switch to 4-bit mode failed. Read again the SD status in 1-bit mode to check if the card responds.
  //
  r = _ExecSDStatus(pInst, 1, pSDStatus, pCardStatus);      // 1 is the number of data lines.
  if (r == 0) {
    FREE_BUFFER(&pSDStatus);
    return 1;         // OK, card works in 1-bit mode.
  }
  FREE_BUFFER(&pSDStatus);
  return 0;           // Error, the switching sequence failed.
}

#endif  // FS_MMC_SUPPORT_SD

/*********************************************************************
*
*       _SetBusModeIfSupported
*
*  Function description
*    Configures the card to transfer the data via more than one data line.
*
*  Parameters
*    pInst        Driver instance.
*    pCSD         [IN] Contents of the CSD register.
*    pSCR         [IN] Contents of the SCR register.
*    pCardStatus  [OUT] Operational status of the storage device.
*
*  Return value
*    > 0    Number of data lines used for the data transfer.
*    ==0    An error occurred.
*/
static int _SetBusModeIfSupported(MMC_CM_INST * pInst, const CSD_RESPONSE * pCSD, const U32 * pSCR, CARD_STATUS * pCardStatus) {
  int r;
  int Result;
  int CardType;
  int Is4bitModeAllowed;

  r             = 1;        // Assume only 1 data line is used for transfer.
  CardType      = (int)pInst->CardType;
  Is4bitModeAllowed = (int)pInst->Is4bitModeAllowed;
  switch (CardType) {
#if FS_MMC_SUPPORT_SD
  case FS_MMC_CARD_TYPE_SD:
    FS_USE_PARA(pCSD);
    if (Is4bitModeAllowed != 0) {
      Result = _SelectCardWithBusyWait(pInst, pCardStatus);
      if (Result == 0) {
#if FS_MMC_DISABLE_DAT3_PULLUP
        Result = _ExecSetClrCardDetect(pInst, 0, pCardStatus);
        if (Result == 0)
#endif // FS_MMC_DISABLE_DAT3_PULLUP
        {
          //
          // Try to switch the card to 4-bit mode.
          //
          r = _SetBusWidthSD(pInst, 4, pSCR, pCardStatus);
        }
      } else {
        r = 0;              // Error, could not select SD card.
      }
    }
    break;
#endif  // FS_MMC_SUPPORT_SD
#if FS_MMC_SUPPORT_MMC
  case FS_MMC_CARD_TYPE_MMC:
    FS_USE_PARA(pSCR);
    {
      int Is8bitModeAllowed;
      U32 SpecVersion;

      Is8bitModeAllowed = (int)pInst->Is8bitModeAllowed;
      //
      // Only MMCplus cards can communicate via 4 and 8 bit data lines.
      //
      SpecVersion = CSD_SPEC_VERS(pCSD);
      if (SpecVersion >= MMC_SPEC_VER_4) {
        if ((Is4bitModeAllowed != 0) || (Is8bitModeAllowed != 0)) {
          Result = _SelectCardWithBusyWait(pInst, pCardStatus);
          if (Result == 0) {
            if (Is8bitModeAllowed != 0) {
              //
              // Try to set the card in 8-bit mode.
              //
              Result = _Test8bitBus(pInst, pCardStatus);
              if (Result == 0) {
                r = _SetBusWidthMMC(pInst, 8, 0, pCardStatus);
              }
            }
            //
            // Try to set the card in 4-bit mode if the 8-bit mode failed or is not supported.
            //
            if (r < 4) {
              if (Is4bitModeAllowed != 0) {
                Result = _Test4bitBus(pInst, pCardStatus);
                if (Result == 0) {
                  r = _SetBusWidthMMC(pInst, 4, 0, pCardStatus);
                }
              }
            }
          } else {
            r = 0;            // Error, could not select MMC card.
          }
        }
      }
    }
    break;
#endif      // FS_MMC_SUPPORT_MMC
  default:
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _SetBusModeIfSupported: Invalid card type %d.\n", CardType));
    r = 0;
    break;
  }
  return r;
}

#if FS_MMC_SUPPORT_MMC

/*********************************************************************
*
*       _EnableCacheIfRequired
*
*  Function description
*    Enables the cache of the eMMC device if supported and not active.
*
*  Parameters
*    pInst        Driver instance.
*    pExtCSD      [IN] Contents of the extended CSD register.
*    pCardStatus  [OUT] Contents of the status register.
*
*  Return value
*    ==0    Cache enabled or not supported.
*    !=0    An error occurred.
*/
static int _EnableCacheIfRequired(MMC_CM_INST * pInst, const U8 * pExtCSD, CARD_STATUS * pCardStatus) {
  int r;
  U8  IsCacheEnabled;
  U32 CacheSize;
  int CardType;
  int IsCacheActivationAllowed;

  r                        = 0;                      // Set to indicate success.
  IsCacheActivationAllowed = (int)pInst->IsCacheActivationAllowed;
  IsCacheEnabled           = 0;
  if (IsCacheActivationAllowed != 0) {
    CardType = (int)pInst->CardType;
    if (CardType == FS_MMC_CARD_TYPE_MMC) {
      if (pExtCSD != NULL) {
        CacheSize = FS_LoadU32LE(pExtCSD + OFF_EXT_CSD_CACHE_SIZE);
        if (CacheSize != 0u) {
          IsCacheEnabled = *(pExtCSD + OFF_EXT_CSD_CACHE_CTRL);
          if (IsCacheEnabled == 0u) {
            r = _WriteExtCSDByte(pInst, OFF_EXT_CSD_CACHE_CTRL, 1, pCardStatus);      // 1 means that the cache has to be enabled.
            if (r == 0) {
              IsCacheEnabled = 1;
            }
          }
        }
      }
    }
  }
  pInst->IsCacheEnabled = IsCacheEnabled;
  return r;
}

/*********************************************************************
*
*       _DisableCacheIfRequired
*
*  Function description
*    Disables the cache of the eMMC device if supported and active.
*
*  Parameters
*    pInst        Driver instance.
*    pCardStatus  [OUT] Contents of the status register.
*
*  Return value
*    ==0    Cache disabled or not supported.
*    !=0    An error occurred.
*/
static int _DisableCacheIfRequired(MMC_CM_INST * pInst, CARD_STATUS * pCardStatus) {
  int r;
  int IsCacheEnabled;
  int CardType;
  int IsCacheActivationAllowed;

  r                        = 0;                      // Set to indicate success.
  IsCacheActivationAllowed = (int)pInst->IsCacheActivationAllowed;
  IsCacheEnabled           = (int)pInst->IsCacheEnabled;
  if (IsCacheActivationAllowed != 0) {
    if (IsCacheEnabled != 0) {
      CardType = (int)pInst->CardType;
      if (CardType == FS_MMC_CARD_TYPE_MMC) {
        r = _WriteExtCSDByte(pInst, OFF_EXT_CSD_CACHE_CTRL, 0, pCardStatus);      // 0 means that the cache has to be disabled.
        if (r == 0) {
          IsCacheEnabled = 0;
        }
      }
    }
  }
  pInst->IsCacheEnabled = (U8)IsCacheEnabled;
  return r;
}

#endif // FS_MMC_SUPPORT_MMC

#if FS_MMC_SUPPORT_SD

/*********************************************************************
*
*       _ReadSCRIfRequired
*
*  Function description
*    Reads the contents of SCR register of an SD card.
*
*  Parameters
*    pInst        Driver instance.
*    pSCR         [OUT] Register contents (at least 8 bytes).
*    pCardStatus  Status returned by the card.
*
*  Return value
*    ==0    Register read or not supported.
*    !=0    An error occurred.
*
*  Additional information
*    The SD card does not have to be selected.
*/
static int _ReadSCRIfRequired(MMC_CM_INST * pInst, U32 * pSCR, CARD_STATUS * pCardStatus) {
  int CardType;
  int r;
  int Result;

  r        = 0;
  CardType = (int)pInst->CardType;
  if (CardType == FS_MMC_CARD_TYPE_SD) {
    if (pSCR != NULL) {
      r = _SelectCardWithBusyWait(pInst, pCardStatus);
      if (r == 0) {
        r = _ExecSendSCR(pInst, pSCR, pCardStatus);
        Result = _DeSelectCard(pInst, pCardStatus);
        if (Result != 0) {
          r = 1;                      // Error, could not deselect card.
        }
      }
    }
  }
  return r;
}

#if FS_MMC_DISABLE_DAT3_PULLUP

/*********************************************************************
*
*       _EnableDAT3PullUpIfRequired
*
*  Function description
*    Enables the internal pull-up of the DAT3 signal.
*
*  Parameters
*    pInst        Driver instance.
*    pCardStatus  [OUT] Operating status of the SD card.
*
*  Return value
*    ==0    OK, pull-up enabled.
*    !=0    An error occurred.
*/
static int _EnableDAT3PullUpIfRequired(MMC_CM_INST * pInst, CARD_STATUS * pCardStatus) {
  int CardType;
  int r;
  int Result;

  r        = 0;
  CardType = (int)pInst->CardType;
  if (CardType == FS_MMC_CARD_TYPE_SD) {
    if (pInst->Is4bitModeAllowed != 0u) {
      r = _SelectCardWithBusyWait(pInst, pCardStatus);
      if (r == 0) {
        r = _ExecSetClrCardDetect(pInst, 1, pCardStatus);
        Result = _DeSelectCard(pInst, pCardStatus);
        if (Result != 0) {
          r = 1;                      // Error, could not deselect card.
        }
      }
    }
  }
  return r;
}

#endif // FS_MMC_DISABLE_DAT3_PULLUP

#endif // FS_MMC_SUPPORT_SD

/*********************************************************************
*
*       _IsFixedSectorSize
*
*  Function description
*    Checks if a data block has a fixed size.
*
*  Return value
*    ==1    Fixed size.
*    ==0    Variable size.
*
*  Additional information
*    This function is used for checking if the size of a data block
*    exchanged with the device has to be explicitly configured at
*    the initialization.
*/
static int _IsFixedSectorSize(const MMC_CM_INST * pInst) {
  int CardType;
  unsigned AccessMode;

  CardType   = (int)pInst->CardType;
  AccessMode = pInst->AccessMode;
  if (CardType == FS_MMC_CARD_TYPE_MMC) {
    if (   (AccessMode == FS_MMC_ACCESS_MODE_HS_DDR)
        || (AccessMode == FS_MMC_ACCESS_MODE_HS400)) {
      return 1;                                           // It is not allowed to set the block size in these access modes.
    }
  }
  return 0;
}

#if FS_MMC_SUPPORT_UHS

/*********************************************************************
*
*       _IsEnhancedStrobeSupported
*
*  Function description
*    Checks if the eMMC device supports the enhanced strobe mode.
*
*  Parameters
*    pExtCSD      [IN] The contents of the Extended CSD register.
*
*  Return value
*    ==1    The enhanced strobe mode is supported.
*    ==0    The enhanced strobe mode is not supported.
*/
static int _IsEnhancedStrobeSupported(const U8 * pExtCSD) {
  int r;

  r = 0;        // Not supported
  if (pExtCSD[OFF_EXT_CSD_STROBE_SUPPORT] != 0) {
    r = 1;      // Supported;
  }
  return r;
}

/*********************************************************************
*
*       _Is1V8Active
*
*  Function description
*    Checks if the voltage level of I/O lines is 1.8 V.
*
*  Parameters
*    pInst          Driver instance.
*
*  Return value
*    ==1    The voltage level of the I/O lines is set to 1.8 V.
*    ==0    The voltage level of the I/O lines is not set to 1.8 V.
*/
static int _Is1V8Active(MMC_CM_INST * pInst) {
  U16 VCur;
  int r;

  r    = 0;
  VCur = _GetVoltage(pInst);
  if (   (VCur >= DEFAULT_MIN_LOW_VOLTAGE_MV)
      && (VCur <= DEFAULT_MAX_LOW_VOLTAGE_MV)) {
    r = 1;
  }
  return r;
}

/*********************************************************************
*
*       _SwitchToLowVoltage
*
*  Function description
*    Changes the voltage level of I/O lines to 1.8 V.
*
*  Parameters
*    pInst          Driver instance.
*    pCardStatus    [OUT] Status returned by the card.
*
*  Return value
*    ==0    OK, voltage level switched.
*    !=0    An error occurred.
*/
static int _SwitchToLowVoltage(MMC_CM_INST * pInst, CARD_STATUS * pCardStatus) {
  int r;
  int CardType;
  U16 VMin;
  U16 VMax;

  CardType = (int)pInst->CardType;
  VMin     = DEFAULT_MIN_LOW_VOLTAGE_MV;
  VMax     = DEFAULT_MAX_LOW_VOLTAGE_MV;
  switch (CardType) {
#if FS_MMC_SUPPORT_SD
  case FS_MMC_CARD_TYPE_SD:
    r = _ExecVoltageSwitch(pInst, pCardStatus);
    if (r == 0) {
      r = _SetVoltage(pInst, VMin, VMax, 1);
    }
    break;
#endif  // FS_MMC_SUPPORT_SD
#if FS_MMC_SUPPORT_MMC
  case FS_MMC_CARD_TYPE_MMC:
    FS_USE_PARA(pCardStatus);
    r = _SetVoltage(pInst, VMin, VMax, 0);
    break;
#endif      // FS_MMC_SUPPORT_MMC
  default:
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _SwitchToLowVoltage: Invalid card type %d.\n", CardType));
    r = 1;
    break;
  }
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: SET_VOLTAGE VMin: %d mV, VMax: %d mV, r: %d\n", VMin, VMax, r));
  return r;
}

#if FS_MMC_SUPPORT_SD

/*********************************************************************
*
*      _IsAccessModeUHSSupported
*
*  Function description
*    Checks if the SD card supports any UHS data access mode (SDR50, DDR50 or SRD104)
*
*  Parameters
*    pInst          Driver instance.
*    pCSD           [IN] Contents of the CSD register.
*    pSCR           [IN] Contents of the SCR register.
*    pCardStatus    [OUT] Status returned by the SD card.
*
*  Return value
*    ==1      UHS access mode supported.
*    ==0      An error occurred or UHS access mode not supported.
*/
static int _IsAccessModeUHSSupported(MMC_CM_INST * pInst, const CSD_RESPONSE * pCSD, const U32 * pSCR, CARD_STATUS * pCardStatus) {
  int r;
  int Result;
  U8  SpecVersion;

  //
  // The command used for checking for UHS is implemented only
  // by the SD cards that support class 10 commands.
  //
  if (_IsClass10Card(pCSD) == 0) {
    return 0;
  }
  //
  // The command for switching in high speed mode is accepted
  // only when the SD card is in Transfer State.
  //
  SpecVersion = SCR_SD_SPEC(pSCR);
  if (SpecVersion < SD_SPEC_VER_200) {
    return 0;
  }
  r = 0;                    // The SD card does not support UHS.
  //
  // The SD card accepts the query only when it is selected.
  //
  Result = _SelectCardWithBusyWait(pInst, pCardStatus);
  if (Result == 0) {
    //
    // Check if any of the UHS access modes is supported by the SD card.
    //
    Result = _CheckWaitFunc(pInst, FUNC_GROUP_ACCESS_MODE, ACCESS_MODE_SDR50, pCardStatus);
    if (Result == 1) {
      r = 1;                // UHS access mode is supported.
      goto Done;
    }
    Result = _CheckWaitFunc(pInst, FUNC_GROUP_ACCESS_MODE, ACCESS_MODE_DDR50, pCardStatus);
    if (Result == 1) {
      r = 1;                // UHS access mode is supported.
      goto Done;
    }
    Result = _CheckWaitFunc(pInst, FUNC_GROUP_ACCESS_MODE, ACCESS_MODE_SDR104, pCardStatus);
    if (Result == 1) {
      r = 1;                // UHS access mode is supported.
      goto Done;
    }
  }
Done:
  (void)_DeSelectCard(pInst, pCardStatus);
  return r;
}

/*********************************************************************
*
*       _SwitchToAccessMode
*
*  Function description
*    Requests the SD card to change the way it transfers the data.
*
*  Parameters
*    pInst        Driver instance.
*    AccessMode   Type of access mode to be set. Permitted values
*                 are FS_MMC_ACCESS_MODE_...
*    pCardStatus  [OUT] Status returned by the SD card.
*
*  Return values
*    ==1    Card switched to new access mode.
*    ==0    Card stays in previous access mode.
*    < 0    An error occurred.
*/
static int _SwitchToAccessMode(MMC_CM_INST * pInst, unsigned AccessMode, CARD_STATUS * pCardStatus) {
  int r;

  //
  // Check if the access mode is supported by the card.
  //
  r = _CheckWaitFunc(pInst, FUNC_GROUP_ACCESS_MODE, AccessMode, pCardStatus);
  if (r <= 0) {
    return r;               // Error or card does not support the access mode.
  }
  //
  // The access mode is supported. Try to switch card to the specified access mode.
  //
  r = _SwitchFunc(pInst, FUNC_GROUP_ACCESS_MODE, AccessMode, pCardStatus);
  if (r != 0) {
    r = -1;                 // Error, could not switch to the specified access mode.
  } else {
    r = 1;                  // OK, card switched to specified access mode.
  }
  return r;
}

/*********************************************************************
*
*      _SwitchToAccessModeSDR104IfSupported
*
*  Function description
*    Requests the SD card to enable the data transfer at clock
*    frequencies up to 208 MHz.
*
*  Return value
*    ==1      Card switched to SDR104 speed mode.
*    ==0      Card communicates in the previous speed mode.
*    < 0      An error occurred.
*/
static int _SwitchToAccessModeSDR104IfSupported(MMC_CM_INST * pInst, CSD_RESPONSE * pCSD, const U32 * pSCR, CARD_STATUS * pCardStatus) {      //lint -efunc(818, _SwitchToAccessModeSDR104IfSupported) Pointer parameter 'pCSD' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory]. Rationale: not possible because the data of the CSD register is read from SD card and stored to pCSD.
  int r;
  int Result;
  U8  SpecVersion;

  if (pInst->IsAccessModeSDR104Allowed == 0u) {
    return 0;                                                       // This access mode is not permitted.
  }
  if (pInst->IsVoltageLevel1V8Allowed == 0u) {
    return 0;                                                       // This access mode is not permitted.
  }
  if (pInst->VoltageLevel > VOLTAGE_LEVEL_1V8_MV) {
    FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: REJECT_SDR104 Reason: Invalid voltage.\n"));
    return 0;                                                       // This access mode requires I/O signaling of either 1.8 or 1.2 V.
  }
  //
  // The command used for switching to high speed mode
  // is implemented only by SD cards that support class
  // 10 commands.
  //
  if (_IsClass10Card(pCSD) == 0) {
    FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: REJECT_SDR104 Reason: Invalid card class.\n"));
    return 0;                                                       // This access mode is not supported.
  }
  //
  // Check the version of the SD specification supported by the card.
  // Only SD cards that conform to a version of SD specification > 2.00
  // can be switched to high speed mode.
  //
  SpecVersion = SCR_SD_SPEC(pSCR);
  if (SpecVersion < SD_SPEC_VER_200) {
    FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: REJECT_SDR104 Reason: Invalid CSD version.\n"));
    return 0;                                                       // This access mode is not supported.
  }
  //
  // The command for switching in high speed mode is accepted
  // only when the SD card is in Transfer State.
  //
  Result = _SelectCardWithBusyWait(pInst, pCardStatus);
  if (Result == 0) {
    r = _SwitchToAccessMode(pInst, ACCESS_MODE_SDR104, pCardStatus);
  } else {
    r = -1;                                                         // Error while selecting the card.
  }
  if (r == 1) {                                                     // Card switched to high speed mode?
    r = -1;                                                         // Set to indicate error.
    Result = _DeSelectCard(pInst, pCardStatus);
    if (Result == 0) {
      //
      // We have to read the CSD register again because the SD card updates
      // the TRAN_SPEED entry to reflect the new maximum speed.
      //
      Result = _ExecSendCSD(pInst, pCSD);
      if (Result == 0) {
        r = 1;                                                    // OK, card switched to high speed mode.
      }
    }
  }
  return r;
}

/*********************************************************************
*
*      _SwitchToAccessModeDDR50IfSupported
*
*  Function description
*    Requests the SD card to enable the data transfer at clock
*    frequencies up to 50 MHz with the data being sampled on both
*    clock transitions.
*
*  Return value
*    ==1      Card switched to DDR50 speed mode.
*    ==0      Card communicates in the previous speed mode.
*    < 0      An error occurred.
*/
static int _SwitchToAccessModeDDR50IfSupported(MMC_CM_INST * pInst, CSD_RESPONSE * pCSD, const U32 * pSCR, CARD_STATUS * pCardStatus) {      //lint -efunc(818, _SwitchToAccessModeDDR50IfSupported) Pointer parameter 'pCSD' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory]. Rationale: not possible because the data of the CSD register is read from SD card and stored to pCSD.
  int r;
  int Result;
  U8  SpecVersion;

  if (pInst->IsAccessModeDDR50Allowed == 0u) {
    return 0;                                                       // This access mode is not permitted.
  }
  if (pInst->IsVoltageLevel1V8Allowed == 0u) {
    return 0;                                                       // This access mode is not permitted.
  }
  if (pInst->Is4bitModeAllowed == 0u) {
    return 0;                                         // This access mode is not permitted.
  }
  if (pInst->BusWidth < 4u) {
    FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: REJECT_DDR50 Reason: Invalid bus width.\n"));
    return 0;                                                       // This access mode requires a bus width of 4 data lines.
  }
  if (pInst->VoltageLevel > VOLTAGE_LEVEL_1V8_MV) {
    FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: REJECT_DDR50 Reason: Invalid voltage.\n"));
    return 0;                                                       // This access mode requires I/O signaling of either 1.8 or 1.2 V.
  }
  //
  // The command used for switching to high speed mode
  // is implemented only by SD cards that support class
  // 10 commands.
  //
  if (_IsClass10Card(pCSD) == 0) {
    FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: REJECT_DDR50 Reason: Invalid card class.\n"));
    return 0;                                                       // This access mode is not supported.
  }
  //
  // Check the version of the SD specification supported by the card.
  // Only SD cards that conform to a version of SD specification > 2.00
  // can be switched to high speed mode.
  //
  SpecVersion = SCR_SD_SPEC(pSCR);
  if (SpecVersion < SD_SPEC_VER_200) {
    FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: REJECT_DDR50 Reason: Invalid CSD version.\n"));
    return 0;                                                       // This access mode is not supported.
  }
  //
  // The command for switching in high speed mode is accepted
  // only when the SD card is in Transfer State.
  //
  Result = _SelectCardWithBusyWait(pInst, pCardStatus);
  if (Result == 0) {
    r = _SwitchToAccessMode(pInst, ACCESS_MODE_DDR50, pCardStatus);
  } else {
    r = -1;             // Error while selecting the card.
  }
  if (r == 1) {         // Card switched to high speed mode?
    r = -1;             // Set to indicate error.
    Result = _DeSelectCard(pInst, pCardStatus);
    if (Result == 0) {
      //
      // We have to read the CSD register again because the SD card updates
      // the TRAN_SPEED entry to reflect the new maximum speed.
      //
      Result = _ExecSendCSD(pInst, pCSD);
      if (Result == 0) {
        r = 1;          // OK, card switched to high speed mode.
      }
    }
  }
  return r;
}

/*********************************************************************
*
*      _SwitchToAccessModeSDR50IfSupported
*
*  Function description
*    Requests the SD card to enable the data transfer at clock
*    frequencies up to 100 MHz.
*
*  Return value
*    ==1      Card switched to SDR50 speed mode.
*    ==0      Card communicates in the previous speed mode.
*    < 0      An error occurred.
*/
static int _SwitchToAccessModeSDR50IfSupported(MMC_CM_INST * pInst, CSD_RESPONSE * pCSD, const U32 * pSCR, CARD_STATUS * pCardStatus) {      //lint -efunc(818, _SwitchToAccessModeSDR50IfSupported) Pointer parameter 'pCSD' could be declared as pointing to const [MISRA 2012 Rule 8.13, advisory]. Rationale: not possible because the data of the CSD register is read from SD card and stored to pCSD.
  int r;
  int Result;
  U8  SpecVersion;

  if (pInst->IsAccessModeSDR50Allowed == 0u) {
    return 0;                                                       // This access mode is not permitted.
  }
  if (pInst->IsVoltageLevel1V8Allowed == 0u) {
    return 0;                                                       // This access mode is not permitted.
  }
  if (pInst->VoltageLevel > VOLTAGE_LEVEL_1V8_MV) {
    FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: REJECT_SDR50 Reason: Invalid voltage.\n"));
    return 0;                                                       // This access mode requires I/O signaling of either 1.8 or 1.2 V.
  }
  //
  // The command used for switching to high speed mode
  // is implemented only by SD cards that support class
  // 10 commands.
  //
  if (_IsClass10Card(pCSD) == 0) {
    FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: REJECT_SDR50 Reason: Invalid card class.\n"));
    return 0;                                                       // This access mode is not supported.
  }
  //
  // Check the version of the SD specification supported by the card.
  // Only SD cards that conform to a version of SD specification > 2.00
  // can be switched to high speed mode.
  //
  SpecVersion = SCR_SD_SPEC(pSCR);
  if (SpecVersion < SD_SPEC_VER_200) {
    FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: REJECT_SDR50 Reason: Invalid CSD version.\n"));
    return 0;                                                       // This access mode is not supported.
  }
  //
  // The command for switching in high speed mode is accepted
  // only when the SD card is in Transfer State.
  //
  Result = _SelectCardWithBusyWait(pInst, pCardStatus);
  if (Result == 0) {
    r = _SwitchToAccessMode(pInst, ACCESS_MODE_SDR50, pCardStatus);
  } else {
    r = -1;             // Error while selecting the card.
  }
  if (r == 1) {         // Card switched to high speed mode?
    r = -1;             // Set to indicate error.
    Result = _DeSelectCard(pInst, pCardStatus);
    if (Result == 0) {
      //
      // We have to read the CSD register again because the SD card updates
      // the TRAN_SPEED entry to reflect the new maximum speed.
      //
      Result = _ExecSendCSD(pInst, pCSD);
      if (Result == 0) {
        r = 1;          // OK, card switched to high speed mode.
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _SwitchToDriverStrength
*
*  Function description
*    Requests the SD card to change the strength of the output driver.
*
*  Parameters
*    pInst            Driver instance.
*    DriverStrength   Type of output driver. Permitted values
*                     are FS_MMC_DRIVER_STRENGTH_...
*    pCardStatus      [OUT] Status returned by the SD card.
*
*  Return values
*    ==1    Card switched to new driver strength.
*    ==0    Card works with previous driver strength.
*    < 0    An error occurred.
*/
static int _SwitchToDriverStrength(MMC_CM_INST * pInst, unsigned DriverStrength, CARD_STATUS * pCardStatus) {
  int r;

  //
  // Check if the driver strength is supported by the card.
  //
  r = _CheckWaitFunc(pInst, FUNC_GROUP_DRIVER_STRENGTH, DriverStrength, pCardStatus);
  if (r <= 0) {
    return r;               // Error or card does not support the driver strength.
  }
  //
  // The driver strength is supported. Try to switch card to the specified driver strength.
  //
  r = _SwitchFunc(pInst, FUNC_GROUP_DRIVER_STRENGTH, DriverStrength, pCardStatus);
  if (r != 0) {
    r = -1;                 // Error, could not switch to the specified driver strength.
  } else {
    r = 1;                  // OK, card switched to specified driver strength.
  }
  return r;
}

/*********************************************************************
*
*       _SetDriverStrengthIfSupported
*
*  Function description
*    Configures the output driving strength of the MMC/SD device.
*
*  Parameters
*    pInst        Driver instance.
*    pCardStatus  Status returned by the MMC/SD device.
*
*  Return value
*    >= 0   OK, configured driver strength.
*    <  0   An error occurred.
*
*  Additional information
*    The driver strength to be configured is taken from the DriverStrengthRequested
*    member of the driver instance.
*/
static int _SetDriverStrengthIfSupported(MMC_CM_INST * pInst, CARD_STATUS * pCardStatus) {
  int      r;
  int      Result;
  unsigned DriverStrength;

  r              = -1;      // Set to indicate an error.
  DriverStrength = pInst->DriverStrengthRequested;
  if (DriverStrength != 0u) {
    Result = _SelectCardWithBusyWait(pInst, pCardStatus);
    if (Result == 0) {
      r = _SwitchToDriverStrength(pInst, DriverStrength, pCardStatus);
      Result = _DeSelectCard(pInst, pCardStatus);
      if (Result != 0) {
        r = -1;           // Error, could not deselect SD card.
      }
    }
  } else {
    r = 0;                // OK, change not requested.
  }
  return r;
}

#endif // FS_MMC_SUPPORT_SD

#if FS_MMC_SUPPORT_MMC

/*********************************************************************
*
*      _SwitchToAccessModeHS400IfSupported
*
*  Function description
*    Requests the MMC device to enable the data transfer at clock
*    frequencies up to 200 MHz with the data being sampled on both
*    clock edges.
*
*  Return value
*    ==1      MMC device switched to high speed mode.
*    ==0      MMC device communicates using the previous speed mode.
*    < 0      An error occurred.
*
*  Additional information
*    The selection of HS400 access mode strobe feature consists of
*    the following steps as defined in the MMC specification:
*    1) Set timing to high speed.
*    2) Set clock frequency to a value smaller than or equal to 52 MHz.
*    3) Set the bus width to 8-bit with DDR.
*    4) Optionally, read the driver strength.
*    5) Set timing to HS400.
*    6) Check that the device is ready.
*    7) Set clock frequency to a value smaller than or equal to 200 MHz.
*/
static int _SwitchToAccessModeHS400IfSupported(MMC_CM_INST * pInst, int IsEnhancedStrobe, CSD_RESPONSE * pCSD, const U8 * pExtCSD, CARD_STATUS * pCardStatus) {
  int      r;
  int      Result;
  U32      SpecVersion;
  unsigned CardType;
  int      BusWidth;
  unsigned ClkFlags;
  U32      Freq_kHz;

  if (pInst->IsAccessModeHS400Allowed == 0u) {
    return 0;                                                       // This access mode is not permitted.
  }
  if (pInst->IsVoltageLevel1V8Allowed == 0u) {
    return 0;                                                       // This access mode is not permitted.
  }
  if (pInst->Is8bitModeAllowed == 0u) {
    return 0;                                                       // This access mode is not permitted.
  }
  if (IsEnhancedStrobe != 0) {
    if (pInst->IsEnhancedStrobeAllowed == 0u) {
      return 0;                                                     // Enhanced strobe mode is not allowed.
    }
  }
  BusWidth = (int)pInst->BusWidth;
  if (BusWidth < 8) {
    FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: REJECT_HS400 Reason: Invalid bus width.\n"));
    return 0;                                                       // This access mode requires data transfer via 8 lines.
  }
  if (pInst->VoltageLevel > VOLTAGE_LEVEL_1V8_MV) {
    FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: REJECT_HS400 Reason: Invalid voltage.\n"));
    return 0;                                                       // This access mode requires I/O signaling of either 1.8 or 1.2 V.
  }
  if (pExtCSD == NULL) {
    FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: REJECT_HS400 Reason: Invalid Extended CSD register.\n"));
    return 0;                                                       // We need the information stored to Extended CSD register in order to check if the MMC device supports the access mode.
  }
  SpecVersion = CSD_SPEC_VERS(pCSD);
  if (SpecVersion < MMC_SPEC_VER_4) {
    FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: REJECT_HS400 Reason: Invalid version.\n"));
    return 0;                                                       // The MMC device does not support this access mode.
  }
  CardType = pExtCSD[OFF_EXT_CSD_CARD_TYPE];
  if ((CardType & (1uL << EXT_CSD_CARD_TYPE_HS400_SHIFT)) == 0u) {
    FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: REJECT_HS400 Reason: Not supported.\n"));
    return 0;                                                       // The MMC device does not support this access mode.
  }
  if (IsEnhancedStrobe != 0) {
    if (_IsEnhancedStrobeSupported(pExtCSD) != 0) {
      FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: REJECT_HS400 Reason: Enhanced strobe not supported.\n"));
      return 0;                                                     // The MMC device does not support this access mode.
    }
  }
  r = -1;                                                           // Set to indicate an error.
  Result = _SelectCardWithBusyWait(pInst, pCardStatus);
  if (Result == 0) {
    Result = _SetAccessModeMMC(pInst, EXT_CSD_HS_TIMING_HIGH_SPEED, pCardStatus);
    if (Result == 0) {
      ClkFlags = 0;
      Freq_kHz = _SetTransferSpeed(pInst, MAX_FREQ_MMC_HS_KHZ, ClkFlags);
      if (Freq_kHz != 0u) {
        ClkFlags = 0u
                 | FS_MMC_CLK_FLAG_DDR_MODE
                 | FS_MMC_CLK_FLAG_STROBE_MODE
                 ;
        if (IsEnhancedStrobe != 0) {
          ClkFlags |= FS_MMC_CLK_FLAG_ENHANCED_STROBE;
        }
        Result = _SetBusWidthMMC(pInst, BusWidth, ClkFlags, pCardStatus);
        if (Result == BusWidth) {
          Result = _SetAccessModeMMC(pInst, EXT_CSD_HS_TIMING_HS400, pCardStatus);
          if (Result == 0) {
            Result = _WaitForCardReady(pInst, pCardStatus);
            if (Result == 0) {
              Freq_kHz = _SetTransferSpeed(pInst, MAX_FREQ_MMC_HS400_KHZ, ClkFlags);
              if (Freq_kHz != 0u) {
                pInst->Freq_kHz               = Freq_kHz;
                pInst->IsEnhancedStrobeActive = (U8)IsEnhancedStrobe;
                r = 1;                                              // OK, card switched to high speed mode.
              }
            }
          }
        }
      }
    }
  }
  return r;
}

/*********************************************************************
*
*      _SwitchToAccessModeHS200IfSupported
*
*  Function description
*    Requests the MMC device to enable the data transfer at clock
*    frequencies up to 200 MHz.
*
*  Return value
*    ==1      MMC device switched to high speed mode.
*    ==0      MMC device communicates using the previous speed mode.
*    < 0      An error occurred.
*
*  Additional information
*    The tuning for the HS400 access mode has to be performed in HS200 access mode
*    if the use of enhanced strobe mode is not allowed. If the enhanced strobe mode
*    is allowed and the eMMC device supports it then the tuning is no longer required
*    and we can switch directly to HS400.
*    For these reasons we also check for the HS400 access mode in this function.
*/
static int _SwitchToAccessModeHS200IfSupported(MMC_CM_INST * pInst, CSD_RESPONSE * pCSD, const U8 * pExtCSD, CARD_STATUS * pCardStatus) {
  int      r;
  int      Result;
  U32      SpecVersion;
  unsigned CardType;

  if ((pInst->IsAccessModeHS200Allowed == 0u) && (pInst->IsAccessModeHS400Allowed == 0u)) {
    return 0;                                               // This access mode is not permitted.
  }
  if (pInst->IsVoltageLevel1V8Allowed == 0u) {
    return 0;                                               // This access mode is not permitted.
  }
  if (pInst->IsAccessModeHS200Allowed != 0u) {
    if ((pInst->Is4bitModeAllowed == 0u) && (pInst->Is8bitModeAllowed == 0u)) {
      return 0;                                             // This access mode is not permitted.
    }
    if (pInst->BusWidth < 4) {
      FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: REJECT_HS200 Reason: Invalid bus width.\n"));
      return 0;                                             // This access mode requires data transfer via either 4 or 8 lines.
    }
  }
  if (pInst->IsAccessModeHS400Allowed != 0u) {
    if (pInst->Is8bitModeAllowed == 0u) {
      return 0;                                             // This access mode is not permitted.
    }
    if (pInst->BusWidth < 8) {
      FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: REJECT_HS200 Reason: Invalid bus width.\n"));
      return 0;                                             // This access mode requires data transfer via either 4 or 8 lines.
    }
  }
  if (pInst->VoltageLevel > VOLTAGE_LEVEL_1V8_MV) {
    FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: REJECT_HS200 Reason: Invalid voltage.\n"));
    return 0;                                               // This access mode requires I/O signaling of either 1.8 or 1.2 V.
  }
  if (pExtCSD == NULL) {
    FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: REJECT_HS200 Reason: Invalid Extended CSD register.\n"));
    return 0;                                               // We need the information stored to Extended CSD register in order to check if the MMC device supports the access mode.
  }
  SpecVersion = CSD_SPEC_VERS(pCSD);
  if (SpecVersion < MMC_SPEC_VER_4) {
    FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: REJECT_HS200 Reason: Invalid version.\n"));
    return 0;                                               // The MMC device does not support this access mode.
  }
  CardType = pExtCSD[OFF_EXT_CSD_CARD_TYPE];
  if ((CardType & (1uL << EXT_CSD_CARD_TYPE_HS200_SHIFT)) == 0u) {
    FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: REJECT_HS200 Reason: Not supported.\n"));
    return 0;                                               // The MMC device does not support this access mode.
  }
  r = -1;                                                   // Set to indicate an error.
  Result = _SelectCardWithBusyWait(pInst, pCardStatus);     // Select the MMC device
  if (Result == 0) {
    Result = _SetAccessModeMMC(pInst, EXT_CSD_HS_TIMING_HS200, pCardStatus);
    if (Result == 0) {
      Result = _WaitForCardReady(pInst, pCardStatus);       // Wait for the MMC device to become ready.
      if (Result == 0) {
        r = 1;                                              // OK, card switched to high speed mode.
      }
    }
  }
  return r;
}

/*********************************************************************
*
*      _SwitchToAccessModeHS_DDRIfSupported
*
*  Function description
*    Requests the MMC device to enable the data transfer at clock
*    frequencies up to 52 MHz with the data being sampled on both
*    clock edges.
*
*  Return value
*    ==1      MMC device switched to high speed mode.
*    ==0      MMC device communicates using the previous speed mode.
*    < 0      An error occurred.
*/
static int _SwitchToAccessModeHS_DDRIfSupported(MMC_CM_INST * pInst, CSD_RESPONSE * pCSD, const U8 * pExtCSD, CARD_STATUS * pCardStatus) {
  int      r;
  int      Result;
  U32      SpecVersion;
  unsigned CardType;
  int      BusWidth;

  if (pInst->IsAccessModeHS_DDRAllowed == 0u) {
    return 0;                                                       // This access mode is not permitted.
  }
  if ((pInst->Is4bitModeAllowed == 0u) && (pInst->Is8bitModeAllowed == 0u)) {
    return 0;                                                       // This access mode is not permitted.
  }
  BusWidth = (int)pInst->BusWidth;
  if (BusWidth < 4) {
    FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: REJECT_HS_DDR Reason: Invalid bus width.\n"));
    return 0;                                                       // This access mode requires data transfer via either 4 or 8 lines.
  }
  if (pExtCSD == NULL) {
    FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: REJECT_HS_DDR Reason: Invalid Extended CSD register.\n"));
    return 0;                                                       // We need the information stored to Extended CSD register in order to check if the MMC device supports the access mode.
  }
  SpecVersion = CSD_SPEC_VERS(pCSD);
  if (SpecVersion < MMC_SPEC_VER_4) {
    FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: REJECT_HS_DDR Reason: Invalid version.\n"));
    return 0;                                                       // The MMC device does not support this access mode.
  }
  CardType = pExtCSD[OFF_EXT_CSD_CARD_TYPE];
  if ((CardType & (1uL << EXT_CSD_CARD_TYPE_HS_DDR_SHIFT)) == 0u) {
    FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: REJECT_HS_DDR Reason: Not supported.\n"));
    return 0;                                                       // The MMC device does not support this access mode.
  }
  r = -1;                                                           // Set to indicate an error.
  Result = _SelectCardWithBusyWait(pInst, pCardStatus);
  if (Result == 0) {
    Result = _SetAccessModeMMC(pInst, EXT_CSD_HS_TIMING_HIGH_SPEED, pCardStatus);
    if (Result == 0) {
      Result = _SetBusWidthMMC(pInst, BusWidth, FS_MMC_CLK_FLAG_DDR_MODE, pCardStatus);
      if (Result == BusWidth) {
        r = 1;                                                      // OK, card switched to high speed mode.
      }
    }
  }
  return r;
}

/*********************************************************************
*
*      _IsDriverStrengthSupported
*
*  Function description
*    Checks if the requested driver strength is supported by the eMMC device.
*
*  Parameters
*    pInst        Driver instance.
*    pExtCSD      Contents of the Extended CSD register.
*
*  Return value
*    ==1      The driver strength is supported.
*    ==0      The driver strength is not supported.
*/
static int _IsDriverStrengthSupported(MMC_CM_INST * pInst, const U32 * pExtCSD) {
  unsigned   Supported;
  unsigned   Requested;
  const U8 * pData8;

  if (pExtCSD == NULL) {
    return 0;                 // Not supported.
  }
  pData8 = SEGGER_CONSTPTR2PTR(const U8, pExtCSD);
  Supported = pData8[OFF_EXT_CSD_DRIVER_STRENGTH];
  Requested = pInst->DriverStrengthRequested;
  if ((Supported & (1uL << Requested)) != 0u) {
    return 1;                 // Supported.
  }
  return 0;                   // Not supported.
}

#endif  // FS_MMC_SUPPORT_MMC

/*********************************************************************
*
*      _ReadTuningBlock
*
*  Function description
*    Requests the card to return the tuning block.
*
*  Return value
*    > 0      OK, failed to read the tuning block.
*    ==0      OK, tuning block read successfully.
*    < 0      An error occurred during the tuning.
*/
static int _ReadTuningBlock(MMC_CM_INST * pInst, unsigned TuningIndex, CARD_STATUS * pCardStatus) {
  int        r;
  int        Result;
  U32        aTuningBlock[NUM_BYTES_TUNING_BLOCK / 4];
  int        CardType;
  unsigned   NumBytes;
  const U8 * pTuningBlock;

  NumBytes     = 0;
  pTuningBlock = NULL;
  //
  // Start the tuning step.
  //
  Result = _StartTuning(pInst, TuningIndex);
  if (Result != 0) {
    return -1;                                  // Error, could not start tuning step.
  }
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: TUNING_START Index: %d\n", TuningIndex));
  CardType = (int)pInst->CardType;
  FS_MEMSET(aTuningBlock, 0, sizeof(aTuningBlock));
  //
  // Read the tuning block.
  //
  switch (CardType) {
#if FS_MMC_SUPPORT_SD
  case FS_MMC_CARD_TYPE_SD:
    r = _ExecSendTuningBlockSD(pInst, aTuningBlock, pCardStatus);
    if (r == 0) {
      NumBytes     = NUM_BYTES_TUNING_BLOCK_4BIT;
      pTuningBlock = _abTuningBlock4Bit;
      //
      // Verify that the data in the tuning block is correct.
      //
      Result = FS_MEMCMP(aTuningBlock, pTuningBlock, NumBytes);
      if (Result != 0) {
        r = 1;                                    // Error, the data in the tuning block does not match.
      }
    } else {
      r = 1;
    }
    break;
#endif // FS_MMC_SUPPORT_SD
#if FS_MMC_SUPPORT_MMC
  case FS_MMC_CARD_TYPE_MMC:
    {

      NumBytes     = NUM_BYTES_TUNING_BLOCK_4BIT;
      pTuningBlock = _abTuningBlock4Bit;
      if (pInst->BusWidth == 8) {
        NumBytes     = NUM_BYTES_TUNING_BLOCK_8BIT;
        pTuningBlock = _abTuningBlock8Bit;
      }
      r = _ExecSendTuningBlockMMC(pInst, aTuningBlock, NumBytes, pCardStatus);
      if (r == 0) {
        //
        // Verify that the data in the tuning block is correct.
        //
        Result = FS_MEMCMP(aTuningBlock, pTuningBlock, NumBytes);
        if (Result != 0) {
          r = 1;                                    // Error, the data in the tuning block does not match.
        }
      } else {
        r = 1;
      }
      break;
    }
#endif // FS_MMC_SUPPORT_MMC
  default:
    r = -1;                                     // Error, unknown card type.
    break;
  }
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: TUNING_END r: %d\n", r));
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ALL)
  {
    unsigned   NumBytesRem;
    char       ac[3 + 1];
    char     * s;
    const U8 * pData;

    if (NumBytes != 0) {
      FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: TUNING_END Act:"));
      NumBytesRem = NumBytes;
      pData       = SEGGER_CONSTPTR2PTR(const U8, aTuningBlock);
      do {
        s = ac;
        FS__AddSpaceHex((U32)(*pData), 2, &s);
        FS_DEBUG_LOG((FS_MTYPE_DRIVER, "%s", ac));
        ++pData;
      } while (--NumBytesRem != 0u);
      FS_DEBUG_LOG((FS_MTYPE_DRIVER, "\n"));
      FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: TUNING_END Exp:"));
      NumBytesRem = NumBytes;
      pData       = pTuningBlock;
      do {
        s = ac;
        FS__AddSpaceHex((U32)(*pData), 2, &s);
        FS_DEBUG_LOG((FS_MTYPE_DRIVER, "%s", ac));
        ++pData;
      } while (--NumBytesRem != 0u);
      FS_DEBUG_LOG((FS_MTYPE_DRIVER, "\n"));
    }
  }
#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ALL
  return r;
}

/*********************************************************************
*
*      _TuneSamplingPointIfRequired
*
*  Function description
*    Calculates the delay required to correctly sample the data received from card.
*
*  Return value
*    ==0      OK, sampling point calculated or tuning not required.
*    !=0      An error occurred.
*/
static int _TuneSamplingPointIfRequired(MMC_CM_INST * pInst, CARD_STATUS * pCardStatus) {
  int      r;
  unsigned AccessMode;
  int      IsRequired;
  unsigned NumTunings;
  unsigned TuningIndex;
  unsigned TuningIndexFirst;
  unsigned TuningIndexLast;
  int      Result;
  int      CardType;
  int      NumRetries;

  r          = 0;       // Set to indicate success.
  IsRequired = 0;
  AccessMode = pInst->AccessMode;
  CardType   = (int)pInst->CardType;
  switch (CardType) {
#if FS_MMC_SUPPORT_SD
  case FS_MMC_CARD_TYPE_SD:
    if (AccessMode == FS_MMC_ACCESS_MODE_SDR104) {
        IsRequired = pInst->IsSDR104TuningRequested;
    } else {
      if (AccessMode == FS_MMC_ACCESS_MODE_SDR50) {
        IsRequired = pInst->IsSDR50TuningRequested;
      }
    }
    break;
#endif // FS_MMC_SUPPORT_SD
#if FS_MMC_SUPPORT_MMC
  case FS_MMC_CARD_TYPE_MMC:
    //
    // The tuning for the HS400 access mode is performed in HS200 access mode.
    //
    if (AccessMode == FS_MMC_ACCESS_MODE_HS200) {
      IsRequired = pInst->IsHS200TuningRequested;
    }
    break;
#endif // FS_MMC_SUPPORT_MMC
  default:
    IsRequired = 0;
    r          = 1;                                     // Error, invalid card type.
    break;
  }
  if (IsRequired != 0) {
    if (_IsTuningSupported(pInst) == 0) {
      IsRequired = 0;
      r          = 1;                                   // Error, tuning is not supported by the hardware layer.
    }
  }
  if (IsRequired != 0) {
    r = _EnableTuning(pInst);
    if (r == 0) {
      NumTunings = _GetMaxTunings(pInst);
      FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: TUNING_ENABLE NumTunings: %d\n", NumTunings));
      if (NumTunings != 0u) {
        //
        // Perform the tuning in the software by reading
        // the tuning block for each tuning step.
        //
        TuningIndexFirst = NumTunings;
        TuningIndexLast  = NumTunings;
        for (TuningIndex = 0; TuningIndex < NumTunings; ++TuningIndex) {
          Result = _ReadTuningBlock(pInst, TuningIndex, pCardStatus);
          if (Result < 0) {
            r = 1;
            break;
          }
          if (Result == 0) {
            //
            // Remember the step of the first successful read operation.
            //
            if (TuningIndexFirst == NumTunings) {
              TuningIndexFirst = TuningIndex;
            }
          } else {
            if (TuningIndexFirst != NumTunings) {
              //
              // Remember the step of the last successful read operation.
              //
              if (TuningIndexLast == NumTunings) {
                TuningIndexLast = TuningIndex - 1u;
                break;
              }
            }
          }
        }
        if (r == 0) {
          if (TuningIndexFirst != NumTunings) {
            if (TuningIndexLast == NumTunings) {
              TuningIndexLast = TuningIndex - 1u;
            }
            //
            // Set the sampling point in the middle of the calculated interval.
            //
            TuningIndex = (TuningIndexFirst + TuningIndexLast) / 2u;
            FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: TUNING_SELECT Index: %d (%d/%d)\n", TuningIndex, TuningIndexFirst, TuningIndexLast));
            r          = 1;                   // Set to indicate error.
            NumRetries = NUM_RETRIES_TUNING;
            for (;;) {
              Result = _ReadTuningBlock(pInst, TuningIndex, pCardStatus);
              if (Result == 0) {
                r = 0;                        // OK, the calculated sampling point works.
                break;
              }
              if (NumRetries == 0) {
                break;                        // Error, maximum number of retries has been reached.
              }
              --NumRetries;
            }
          } else {
            r = 1;                            // Error, could not find sampling point.
          }
        }
      }
      Result = _DisableTuning(pInst, r);
      if (Result != 0) {
        r = 1;
      }
      FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: TUNING_DISABLE r: %d\n", r));
    } else {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _TuneSamplingPointIfRequired: Could not enable tuning."));
    }
  }
  return r;
}

#endif // FS_MMC_SUPPORT_UHS

/*********************************************************************
*
*       _Init
*
*  Function description
*    MMC/SD driver internal function.
*    Initialize the SD host controller and MMC/SD card
*    contents.
*
*  Parameters
*    pInst      Driver instance.
*
*  Return value
*    ==0        Initialization was successful.
*    !=0        An error has occurred.
*/
static int _Init(MMC_CM_INST * pInst) {
  int            IsPresent;
  int            r;
  CSD_RESPONSE   csd;
  CARD_STATUS    CardStatus;
  U32          * pExtCSD;
  U32            Freq_kHz;
  int            IsBusModeError;
  int            CardType;
  unsigned       AccessMode;
  int            IsAccessModeHSError;
  int            NumRetries;
  U32            NumSectors;
  U32            MaxNumSectors;
  U32            StartSector;
#if FS_MMC_SUPPORT_SD
  U32            aSCR[NUM_BYTES_SCR / 4u];     // 32-bit aligned for faster DMA transfer.
#endif // FS_MMC_SUPPORT_SD
  U32          * pSCR;
  int            Is1V8Supported;
#if FS_MMC_SUPPORT_UHS
#if FS_MMC_SUPPORT_SD
  int            IsAccessModeSDR104Error;
  int            IsAccessModeDDR50Error;
  int            IsAccessModeSDR50Error;
  int            IsDriverStrengthError;
#endif // FS_MMC_SUPPORT_SD
#if FS_MMC_SUPPORT_MMC
  int            IsAccessModeHS400Error;
  int            IsAccessModeHS200Error;
  int            IsAccessModeHS_DDRError;
#endif // FS_MMC_SUPPORT_MMC
  int            IsVoltageSwitchError;
#endif // FS_MMC_SUPPORT_UHS

  pExtCSD             = NULL;
  IsBusModeError      = 0;
  IsAccessModeHSError = 0;
  NumRetries          = NUM_RETRIES_INIT + 1;      // + 1 for the case that the number of retries is set to 0.
  Is1V8Supported      = 0;
  FS_MEMSET(&CardStatus, 0, sizeof(CardStatus));
#if FS_MMC_SUPPORT_SD
  FS_MEMSET(aSCR, 0, sizeof(aSCR));
  pSCR = aSCR;
#else
  pSCR = NULL;
#endif // FS_MMC_SUPPORT_SD
#if FS_MMC_SUPPORT_UHS
#if FS_MMC_SUPPORT_SD
  IsAccessModeSDR104Error = 0;
  IsAccessModeDDR50Error  = 0;
  IsAccessModeSDR50Error  = 0;
  IsDriverStrengthError   = 0;
#endif // FS_MMC_SUPPORT_SD
#if FS_MMC_SUPPORT_MMC
  IsAccessModeHS400Error  = 0;
  IsAccessModeHS200Error  = 0;
  IsAccessModeHS_DDRError = 0;
#endif // FS_MMC_SUPPORT_MMC
  IsVoltageSwitchError    = 0;
#endif // FS_MMC_SUPPORT_UHS
  for (;;) {
    FREE_BUFFER(&pExtCSD);          // Make sure that the read buffer is free for the following operations.
    if (NumRetries-- == 0) {
      r = 1;                        // Error, could not initialize the card after the number of specified retries.
      break;
    }
    //
    // Initialize the driver instance with default values.
    //
    pInst->HasError            = 0;
    pInst->CardType            = FS_MMC_CARD_TYPE_UNKNOWN;
    pInst->BusWidth            = 1;    // At startup use only one data line (DAT0)
    pInst->IsWriteProtected    = 0;
    pInst->Rca                 = 0;
    pInst->NumSectors          = 0;
    pInst->MaxWriteBurst       = _GetMaxWriteBurst(pInst);
    pInst->MaxWriteBurstRepeat = _GetMaxWriteBurstRepeat(pInst);
    pInst->MaxWriteBurstFill   = _GetMaxWriteBurstFill(pInst);
    pInst->MaxReadBurst        = _GetMaxReadBurst(pInst);
    pInst->IsHighCapacity      = 0;
    _InitHWIfRequired(pInst);
    IsPresent = _IsPresent(pInst);
    if (IsPresent == 0) {
      FS_DEBUG_WARN((FS_MTYPE_DRIVER, "MMC_CM: _Init: Card has been removed."));
      r = 1;                        // Error, the card not present.
      break;
    }
    //
    // Configure the timeout for the command response.
    //
    _SetResponseTimeOut(pInst, DEFAULT_RESPONSE_TIMEOUT);
    //
    // Configure the communication speed and the data transfer timeout.
    //
    Freq_kHz = _SetTransferSpeed(pInst, DEFAULT_STARTUP_FREQ_KHZ, 0);
    if (Freq_kHz == 0u) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _Init: Could not set clock frequency."));
      r = 1;                        // Error, the HW does not support the transfer speed or the clock is not configured correctly.
      break;
    }
    pInst->Freq_kHz = Freq_kHz;
    //
    // Set all cards to Idle state. The cards do not respond to this command
    //
    _ExecGoIdleState(pInst);
    //
    // Identify and initialize the inserted card.
    //
#if FS_MMC_SUPPORT_UHS
    Is1V8Supported = _IsLowVoltageLevelAllowed(pInst);
#endif // FS_MMC_SUPPORT_UHS
    CardType = _IdentifyInitCard(pInst, &Is1V8Supported, &CardStatus);
    if (CardType == FS_MMC_CARD_TYPE_UNKNOWN) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _Init: Could not identify card."));
      r = 1;                        // Error, the card can not be identified.
      break;
    }
    pInst->CardType     = (U8)CardType;
    pInst->VoltageLevel = DEFAULT_VOLTAGE_LEVEL_MV;
#if FS_MMC_SUPPORT_UHS
    //
    // Switch the voltage of the I/O lines to 1.8 V if required.
    //
    if (IsVoltageSwitchError == 0) {
      if (Is1V8Supported != 0) {
        if (_Is1V8Active(pInst) == 0) {
          r = _SwitchToLowVoltage(pInst, &CardStatus);
          if (r != 0) {
            IsVoltageSwitchError = 1;
            FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _Init: Could not switch voltage level."));
            continue;                     // Error, command failed. Retry card initialization.
          }
#if FS_MMC_SUPPORT_MMC
          if (pInst->CardType == FS_MMC_CARD_TYPE_MMC) {
            //
            // Reinitialize the MMC device
            //
            _ExecGoIdleState(pInst);
            r = _InitMMCCard(pInst, &Is1V8Supported);
            if (r != 0) {
              IsVoltageSwitchError = 1;
              FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _Init: Could not reinit card."));
              continue;                   // Error, the MMC device cannot be reinitialized.
            }
          }
#endif // FS_MMC_SUPPORT_MMC
        }
        pInst->VoltageLevel = VOLTAGE_LEVEL_1V8_MV;
      }
    }
#endif // FS_MMC_SUPPORT_UHS
    //
    // Request the CID from the MMC/SD card and move to Identification State.
    //
    r = _ExecAllSendCID(pInst, (U8 *)&csd);     // The card id is not required. Use the CSD buffer to save stack space.
    if (r != 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _Init: Could not read the card id."));
      continue;                     // Error, command failed. Retry card initialization.
    }
    //
    // Set the relative address of this card (only 1 card is currently supported).
    // After execution of this operation the card is moved to Stand-by State.
    //
    r = _SetRCA(pInst, &CardStatus);
    if (r != 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _Init: Could not set relative address."));
      continue;                     // Error, command failed. Retry card initialization.
    }
    //
    // Read the contents of the SCR register in order to check
    // if the SD card supports the high speed mode and 4-bit data transfers.
    //
#if FS_MMC_SUPPORT_SD
    r = _ReadSCRIfRequired(pInst, pSCR, &CardStatus);
    if (r != 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _Init: Could not read SCR."));
      continue;
    }
#endif // FS_MMC_SUPPORT_SD
    //
    // Reads the Card-Specific Data register which contains information
    // about the capabilities of the card.
    //
    r = _ExecSendCSD(pInst, &csd);
    if (r != 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _Init: Failed to read CSD register."));
      continue;                     // Error, command failed. Retry card initialization.
    }
#if (FS_MMC_SUPPORT_UHS != 0) && (FS_MMC_SUPPORT_SD != 0)
    //
    // An SD card reports that a switch to 1.8 V signaling is not required on a hardware
    // that is not able to power cycle the SD card on initialization after the SD card
    // was switched to 1.8 V signaling. It seems that the only way to check if the SD card
    // is currently using 1.8 V signaling is to query the support UHS access mode
    // via CMD6. We check here for this condition and if true we enable the 1.8 V signaling
    // of the SD host controller.
    //
    if (IsVoltageSwitchError == 0) {        // No previous voltage switch error?
      if (Is1V8Supported == 0) {            // Did SD card report that a switch to 1.8 V signaling is not accepted?
        if (_Is1V8Active(pInst) == 0) {     // Is the host signaling 3.3 V?
          Is1V8Supported = _IsLowVoltageLevelAllowed(pInst);
          if (Is1V8Supported != 0) {        // Is at least one UHS access mode enabled?
            if (_IsAccessModeUHSSupported(pInst, &csd, pSCR, &CardStatus) != 0) {
              r = _SetVoltage(pInst, DEFAULT_MIN_LOW_VOLTAGE_MV, DEFAULT_MAX_LOW_VOLTAGE_MV, 0);      // 0 means: set the voltage without performing a handshake.
              if (r != 0) {
                IsVoltageSwitchError = 1;
                FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _Init: Could not switch voltage level."));
                continue;                     // Error, command failed. Retry card initialization.
              }
              pInst->VoltageLevel = VOLTAGE_LEVEL_1V8_MV;
            }
          }
        }
      }
    }
#endif // FS_MMC_SUPPORT_UHS != 0 && FS_MMC_SUPPORT_SD != 0
    //
    // Read the EXT_CSD register and process the information from CSD and EXT_CSD registers.
    //
#if FS_MMC_SUPPORT_MMC
    (void)_ReadExtCSDIfRequired(pInst, &pExtCSD, &CardStatus);
#if FS_MMC_SUPPORT_UHS
    //
    // Validate the requested driver strength and fall back
    // to the default driver strength if not supported.
    //
    if (_IsDriverStrengthSupported(pInst, pExtCSD) == 0) {
      pInst->DriverStrengthRequested = 0;     // Set the default driver strength.
    }
#endif // FS_MMC_SUPPORT_UHS
#endif // FS_MMC_SUPPORT_MMC
    //
    // Try to switch to 4-bit or 8-bit mode if the mode is supported by the card.
    //
    if (IsBusModeError == 0) {
      r = _SetBusModeIfSupported(pInst, &csd, pSCR, &CardStatus);
      if (r == 0) {
        IsBusModeError = 1;
        continue;                   // Reinitialize the card without support for 4- or 8-bit mode.
      }
      pInst->BusWidth = (U8)r;
    }
    //
    // Configure the access mode (i.e. bus speed mode).
    //
    AccessMode = FS_MMC_ACCESS_MODE_DS;
#if FS_MMC_SUPPORT_UHS
#if FS_MMC_SUPPORT_SD
    if (CardType == FS_MMC_CARD_TYPE_SD) {
      if (IsAccessModeSDR104Error == 0) {
        r = _SwitchToAccessModeSDR104IfSupported(pInst, &csd, pSCR, &CardStatus);
        if (r < 0) {
          IsAccessModeSDR104Error = 1; // Try to reinitialize the card without this access mode.
          continue;
        }
        if (r == 1) {
          AccessMode = FS_MMC_ACCESS_MODE_SDR104;
        }
      }
      if (r == 0) {
        if (IsAccessModeDDR50Error == 0) {
          r = _SwitchToAccessModeDDR50IfSupported(pInst, &csd, pSCR, &CardStatus);
          if (r < 0) {
            IsAccessModeDDR50Error = 1; // Try to reinitialize the card without this access mode.
            continue;
          }
          if (r == 1) {
            AccessMode = FS_MMC_ACCESS_MODE_DDR50;
          }
        }
      }
      if (r == 0) {
        if (IsAccessModeSDR50Error == 0) {
          r = _SwitchToAccessModeSDR50IfSupported(pInst, &csd, pSCR, &CardStatus);
          if (r < 0) {
            IsAccessModeSDR50Error = 1; // Try to reinitialize the card without this access mode.
            continue;
          }
          if (r == 1) {
            AccessMode = FS_MMC_ACCESS_MODE_SDR50;
          }
        }
      }
    }
#endif //FS_MMC_SUPPORT_SD
#if FS_MMC_SUPPORT_MMC
    if (CardType == FS_MMC_CARD_TYPE_MMC) {
      if (IsAccessModeHS400Error == 0) {
        r = _SwitchToAccessModeHS400IfSupported(pInst, 1, &csd, (U8 *)pExtCSD, &CardStatus);      // 1 means enhanced strobe mode.
        if (r < 0) {
          IsAccessModeHS400Error = 1; // Try to reinitialize the card without this access mode.
          continue;
        }
        if (r == 1) {
          AccessMode = FS_MMC_ACCESS_MODE_HS400;
        }
        if (r == 0) {
          if (IsAccessModeHS200Error == 0) {
            r = _SwitchToAccessModeHS200IfSupported(pInst, &csd, (U8 *)pExtCSD, &CardStatus);
            if (r < 0) {
              IsAccessModeHS200Error = 1; // Try to reinitialize the card without this access mode.
              continue;
            }
            if (r == 1) {
              AccessMode = FS_MMC_ACCESS_MODE_HS200;
            }
          }
          if (r == 0) {
            if (IsAccessModeHS_DDRError == 0) {
              r = _SwitchToAccessModeHS_DDRIfSupported(pInst, &csd, (U8 *)pExtCSD, &CardStatus);
              if (r < 0) {
                IsAccessModeHS_DDRError = 1; // Try to reinitialize the card without this access mode.
                continue;
              }
              if (r == 1) {
                AccessMode = FS_MMC_ACCESS_MODE_HS_DDR;
              }
            }
          }
        }
      }
    }
#endif // FS_MMC_SUPPORT_MMC
    if (r == 0)
#endif // FS_MMC_SUPPORT_UHS
    {
      //
      // Try to configure the card in high-speed mode.
      //
      if (IsAccessModeHSError == 0) {
        r = _SwitchToAccessModeHSIfSupported(pInst, &csd, pSCR, &CardStatus);
        if (r < 0) {
          IsAccessModeHSError = 1;          // Try to reinitialize the card without high-speed support.
          continue;
        }
        if (r == 1) {
          AccessMode = FS_MMC_ACCESS_MODE_HS;
        }
      }
    }
    pInst->AccessMode = (U8)AccessMode;
#if (FS_MMC_SUPPORT_UHS != 0) && (FS_MMC_SUPPORT_SD != 0)
    if (CardType == FS_MMC_CARD_TYPE_SD) {
      //
      // Set the output driver strength of the storage device.
      //
      if (IsDriverStrengthError == 0) {
        r = _SetDriverStrengthIfSupported(pInst, &CardStatus);
        if (r < 0) {
          IsDriverStrengthError = 1;
          continue;                   // Reinitialize the card without setting the driver strength.
        }
        pInst->DriverStrengthActive = (U8)r;
      }
    }
#endif // FS_MMC_SUPPORT_UHS != 0 && FS_MMC_SUPPORT_SD != 0
    r = _SelectCardWithBusyWait(pInst, &CardStatus);
    if (r != 0) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _Init: Could not select card."));
      r = 1;
      break;
    }
    r = _ApplyPara(pInst, &csd, (U8 *)pExtCSD, pSCR);
    if (r != 0) {
      r = 1;
      break;
    }
#if FS_MMC_SUPPORT_UHS
    //
    // Tune the data sampling point.
    //
    r = _TuneSamplingPointIfRequired(pInst, &CardStatus);
    if (r != 0) {
#if FS_MMC_SUPPORT_SD
      if (AccessMode == FS_MMC_ACCESS_MODE_SDR104) {
        IsAccessModeSDR104Error = 1;
      }
      if (AccessMode == FS_MMC_ACCESS_MODE_SDR50) {
        IsAccessModeSDR50Error = 1;
      }
#endif // FS_MMC_SUPPORT_SD
#if FS_MMC_SUPPORT_MMC
      if (AccessMode == FS_MMC_ACCESS_MODE_HS200) {
        IsAccessModeHS200Error = 1;
      }
#endif // FS_MMC_SUPPORT_MMC
      continue;                             // Tunning operation failed. Try with a different clock frequency.
    }
#if FS_MMC_SUPPORT_MMC
    if (IsAccessModeHS400Error == 0) {
      //
      // HS400 access mode without enhanced strobe requires tuning that is performed in HS200 access mode.
      //
      if (AccessMode == FS_MMC_ACCESS_MODE_HS200) {
        r = _SwitchToAccessModeHS400IfSupported(pInst, 0, &csd, (U8 *)pExtCSD, &CardStatus);      // 0 means without enhanced strobe mode.
        if (r < 0) {
          IsAccessModeHS400Error = 1;       // Try to reinitialize the card without this access mode.
          continue;
        }
        if (r == 1) {
          pInst->AccessMode = FS_MMC_ACCESS_MODE_HS400;
        }
      }
    }
#endif // FS_MMC_SUPPORT_MMC
#endif // FS_MMC_SUPPORT_UHS
#if FS_MMC_SUPPORT_MMC
    //
    // Enable the cache of MMC devices to improve performance.
    //
    r = _EnableCacheIfRequired(pInst, (U8 *)pExtCSD, &CardStatus);
    FREE_BUFFER(&pExtCSD);              // After this point, the information stored in the EXT_CSD register is not required anymore.
    if (r != 0) {
      r = 1;
      break;
    }
#endif // FS_MMC_SUPPORT_MMC
    //
    // Set the number of bytes in a data transfer block.
    //
    if (_IsFixedSectorSize(pInst) == 0) {
      r = _ExecSetBlockLen(pInst, BYTES_PER_SECTOR, &CardStatus);
      if (r != 0) {
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _Init: Could not set block length."));
        r = 1;                      // Less sectors available than configured by application.
        break;
      }
    }
    //
    // Check the number of sectors configured for storage.
    //
    NumSectors    = pInst->NumSectors;
    MaxNumSectors = pInst->MaxNumSectors;
    StartSector   = pInst->StartSector;
    if (NumSectors <= StartSector) {
      FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _Init: Invalid start sector index."));
      r = 1;                        // Less sectors available than configured by application.
      break;
    }
    NumSectors -= StartSector;
    if (MaxNumSectors != 0u) {      // Is an upper limit for the number of sectors configured?
      if (NumSectors > MaxNumSectors) {
        NumSectors = MaxNumSectors;
      }
    }
    pInst->NumSectors  = NumSectors;
    pInst->StartSector = StartSector;
    pInst->IsInited    = 1;
    r = 0;                          // OK, card successfully initialized.
    break;
  }
  FREE_BUFFER(&pExtCSD);            // Free any allocated memory.
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, "MMC_CM: INIT CardType: %d, AccessMode: %d, VoltageLevel: %d.%d V",
    pInst->CardType, pInst->AccessMode, pInst->VoltageLevel / 1000u, pInst->VoltageLevel % 1000u));
  FS_DEBUG_LOG((FS_MTYPE_DRIVER, ", BusWidth: %d bit(s), NumSectors: %d, StartSector: %d, r: %d\n",
    pInst->BusWidth, pInst->NumSectors, pInst->StartSector, r));
  return r;
}

/*********************************************************************
*
*       _PrepareWriteMultiple
*
*  Function description
*    Makes preparations for a write multiple operation.
*
*  Additional information
*    The function informs the SD card about the number of blocks (i.e sectors)
*    the SD host wants write in the next operation. Some SD cards can
*    use this information to improve the write performance.
*
*    This function is used to configure how the MMC device has to perform
*    an operation that writes multiple blocks (i.e. sectors) at once.
*    In the emFile versions up to and including 4.04e the data transfer
*    is started by the SD / MMC driver using a WRITE_MULTIPLE_BLOCK (CMD24) command.
*    As response, the SD / MMC device starts receiving the data sent by
*    the SD / MMC host controller with each SD / MMC clock. The data transfer
*    is ended when the SD / MMC driver issues a STOP_TRANSMISSION (CMD12) command.
*    This type of data transfer is called open-ended because the SD / MMC device
*    does not know in advance how many data blocks have to be transferred.
*
*    The default behavior has been changed in the newer emFile versions
*    (greater than 4.04e) to use close-ended read operations. That is
*    the SD / MMC driver specifies the number of blocks to be transferred
*    before starting the write multiple operation by using the SET_BLOCK_COUNT (CMD23)
*    command. Using this type of operation helps increase the write performance.
*
*    All MMC devices support close-ended write multiple operations.
*    Support for close-ended write operations multiple is optional for SD cards.
*    If the SD card does not support close-ended write multiple operations,
*    an open-ended write multiple operation is used instead.
*/
static int _PrepareWriteMultiple(MMC_CM_INST * pInst, U32 NumSectors, int * pIsWriteOpenEnded, CARD_STATUS * pCardStatus) {
  int r;
  int CardType;

  FS_USE_PARA(NumSectors);
  FS_USE_PARA(pCardStatus);
  FS_USE_PARA(pIsWriteOpenEnded);
  r = 0;                                      // Set to indicate success.
  CardType = (int)pInst->CardType;
  if (CardType == FS_MMC_CARD_TYPE_SD) {      // This feature is supported only by SD cards.
#if FS_MMC_SUPPORT_SD
    if (pInst->IsCloseEndedRWSupported != 0u) {
      r = _ExecSetBlockCount(pInst, NumSectors, 0, pCardStatus);
      if (r == 0) {
        *pIsWriteOpenEnded = 0;               // No STOP_TRANSMISSION command is required at the end of data transfer.
      }
    } else {
      r = _ExecSetWrBlkEraseCount(pInst, NumSectors, pCardStatus);
    }
#endif  // FS_MMC_SUPPORT_SD
  } else {
#if FS_MMC_SUPPORT_MMC
    if (pInst->IsCloseEndedRWSupported != 0u) {
      int IsReliableWrite;

      IsReliableWrite = (int)pInst->IsReliableWriteActive;
      r = _ExecSetBlockCount(pInst, NumSectors, IsReliableWrite, pCardStatus);
      if (r == 0) {
        *pIsWriteOpenEnded = 0;               // No STOP_TRANSMISSION command is required at the end of data transfer.
      }
    }
#endif  // FS_MMC_SUPPORT_MMC
  }
  return r;
}

/*********************************************************************
*
*       _PrepareReadMultiple
*
*  Function description
*    Makes preparations for a read multiple operation.
*
*  Additional information
*    This function can be used to configure how an SD / MMC device has to perform
*    an operation that reads multiple blocks (i.e. sectors) at once.
*    In the emFile versions up to and including 4.04e the data transfer
*    is started by the SD / MMC driver using a READ_MULTIPLE_BLOCK (CMD18) command.
*    As response, the SD / MMC device starts sending the data to SD / MMC
*    host controller with each SD / MMC clock. The data transfer is ended
*    when the SD / MMC driver issues a STOP_TRANSMISSION (CMD12) command.
*    This type of data transfer is called open-ended because the SD / MMC device
*    does not know in advance how many data blocks have to be transferred.
*
*    The default behavior has been changed in the newer emFile versions
*    (greater than 4.04e) to use close-ended read operations. That is
*    the SD / MMC driver specifies the number of blocks to be transferred
*    before starting the read multiple operation by using the SET_BLOCK_COUNT (CMD23)
*    command. Using this type of operation helps increase the read performance.
*
*    All MMC devices support close-ended read multiple operations.
*    Support for close-ended read multiple operations is only mandatory
*    for UHS104 SD cards. If the SD card does not support close-ended
*    read multiple operations, an open-ended read multiple operation
*    is used instead.
*/
static int _PrepareReadMultiple(MMC_CM_INST * pInst, U32 NumSectors, int * pIsReadOpenEnded, CARD_STATUS * pCardStatus) {
  int r;
  int CardType;

  FS_USE_PARA(NumSectors);
  FS_USE_PARA(pCardStatus);
  FS_USE_PARA(pIsReadOpenEnded);
  r = 0;                                      // Set to indicate success.
  CardType = (int)pInst->CardType;
  if (CardType == FS_MMC_CARD_TYPE_SD) {      // This feature is supported only by SD cards.
#if FS_MMC_SUPPORT_SD
    if (pInst->IsCloseEndedRWSupported != 0u) {
      r = _ExecSetBlockCount(pInst, NumSectors, 0, pCardStatus);
      if (r == 0) {
        *pIsReadOpenEnded = 0;                // No STOP_TRANSMISSION command is required at the end of data transfer.
      }
    }
#endif  // FS_MMC_SUPPORT_SD
  } else {
#if FS_MMC_SUPPORT_MMC
    r = _ExecSetBlockCount(pInst, NumSectors, 0, pCardStatus);
    if (r == 0) {
      *pIsReadOpenEnded = 0;                  // No STOP_TRANSMISSION command is required at the end of data transfer.
    }
#endif  // FS_MMC_SUPPORT_MMC
  }
  return r;
}

/*********************************************************************
*
*       _ReadSectors
*
*  Function description
*    Reads the number of specified sectors from the SD/MMC card.
*/
static int _ReadSectors(MMC_CM_INST * pInst, U32 SectorIndex, void * pData, U32 NumSectors, unsigned MaxReadBurst) {
  int           r;
  CARD_STATUS   CardStatus;
  U32           NumSectorsAtOnce;
  U8          * pData8;
  int           Result;
  int           IsReadOpenEnded;

  FS_MEMSET(&CardStatus, 0, sizeof(CardStatus));
  pData8 = SEGGER_PTR2PTR(U8, pData);
  r = _LeavePowerSaveModeIfRequired(pInst, &CardStatus);
  if (r == 0) {
    //
    // Data can be read from card only when the card is in Transfer state.
    //
    r = _SelectCardIfRequired(pInst, &CardStatus);
    if (r == 0) {
      //
      // Stay in this loop until all the sector data is read or an error occurs.
      //
      do {
        //
        // Wait for the card to finish a previous write operation.
        //
        r = _WaitForCardReady(pInst, &CardStatus);
        if (r != 0) {
          break;          // Error, card reports busy.
        }
        NumSectorsAtOnce = SEGGER_MIN(NumSectors, MaxReadBurst);
        if (NumSectorsAtOnce == 1u) {
          IsReadOpenEnded = 0;
          //
          // Read one sector at a time.
          //
          r = _ExecReadSingleBlock(pInst, SectorIndex, SEGGER_PTR2PTR(U32, pData8), &CardStatus);
        } else {
          //
          // Read more than 1 sector at a time.
          //
          IsReadOpenEnded = 1;
          r = _PrepareReadMultiple(pInst, NumSectorsAtOnce, &IsReadOpenEnded, &CardStatus);
          if (r == 0) {
            r = _ExecReadMultipleBlocks(pInst, SectorIndex, SEGGER_PTR2PTR(U32, pData8), NumSectorsAtOnce, &CardStatus);
          }
        }
        //
        // Request the card to return to Transfer state.
        //
        if (IsReadOpenEnded != 0) {
          Result = _ExecStopTransmission(pInst, &CardStatus);
          if (Result != 0) {
            r = Result;
          }
        }
        if (r != 0) {
          FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _ReadSectors: Could not read %lu sector(s) from sector index %lu.", NumSectorsAtOnce, SectorIndex));
          break;          // Error, could not transfer data.
        }
        NumSectors  -= NumSectorsAtOnce;
        SectorIndex += NumSectorsAtOnce;
        pData8      += NumSectorsAtOnce << BYTES_PER_SECTOR_SHIFT;
      } while (NumSectors != 0u);
    }
    Result = _EnterPowerSaveModeIfRequired(pInst, &CardStatus);
    if (Result != 0) {
      r = Result;
    }
  }
  return r;
}

/*********************************************************************
*
*       _ReadSectorsWithRetry
*
*  Function description
*    Reads one ore more sectors from storage medium.
*    The read operation is executed again in case of an error.
*/
static int _ReadSectorsWithRetry(MMC_CM_INST * pInst, U32 SectorIndex, U8 * pBuffer, U32 NumSectors) {
  int      r;
  int      NumRetries;
  unsigned MaxReadBurst;

  r            = 1;             // Set to indicate an error.
  MaxReadBurst = pInst->MaxReadBurst;
  NumRetries   = FS_MMC_NUM_RETRIES;
  for (;;) {
    if (pInst->HasError != 0u) {
      break;
    }
    r = _ReadSectors(pInst, SectorIndex, pBuffer, NumSectors, MaxReadBurst);
    if (r == 0) {
      IF_STATS(pInst->StatCounters.ReadSectorCnt += NumSectors);
      break;                    // OK, data read
    }
    if (NumRetries == 0) {
      break;                    // An error occurred and maximum number of retries has been reached.
    }
    --NumRetries;
    if (MaxReadBurst != 1u) {
      FS_DEBUG_WARN((FS_MTYPE_DRIVER, "MMC_CM: _ReadSectorsWithRetry: Falling back to single sector read mode."));
      MaxReadBurst = 1;         // Fall back to single sector read.
    }
    IF_STATS(pInst->StatCounters.ReadErrorCnt++);
  }
  if ((NumRetries < FS_MMC_NUM_RETRIES) && (MaxReadBurst == 1u)) {
    FS_DEBUG_WARN((FS_MTYPE_DRIVER, "MMC_CM: _ReadSectorsWithRetry: Restore multiple sector read mode."));
  }
  return r;
}

/*********************************************************************
*
*       _WriteSectors
*
*  Function description
*    Writes the specified number of sectors to SD/MMC card.
*/
static int _WriteSectors(MMC_CM_INST * pInst, U32 SectorIndex, const void * pData, U32 NumSectors, U8 BurstType, unsigned MaxWriteBurst) {
  int           r;
  int           Result;
  CARD_STATUS   CardStatus;
  const U8    * pData8;
  U32           NumSectorsAtOnce;
  int           IsWriteOpenEnded;

  FS_MEMSET(&CardStatus, 0, sizeof(CardStatus));
  pData8 = SEGGER_CONSTPTR2PTR(const U8, pData);
  r = _LeavePowerSaveModeIfRequired(pInst, &CardStatus);
  if (r == 0) {
    //
    // Data can be written to card only when the card is in Transfer state.
    //
    r = _SelectCardIfRequired(pInst, &CardStatus);
    if (r == 0) {
      //
      // Stay in this loop until all the sector data is written or an error occurs.
      //
      do {
        //
        // Wait for the card to be ready to accept data from host.
        //
        r = _WaitForCardReady(pInst, &CardStatus);
        if (r != 0) {
          break;            // Error, card reports busy
        }
        //
        // Determine the number of sectors to be written at once.
        //
        NumSectorsAtOnce = SEGGER_MIN(NumSectors, MaxWriteBurst);
        //
        // If the support for buffered write is disabled, wait here for the card
        // to finish the previous write operation.
        //
        if (pInst->IsBufferedWriteAllowed == 0u) {
          r = _WaitForCardState(pInst, &CardStatus, CARD_STATE_TRAN);
          if (r != 0) {
            break;          // Error, card did not enter the data transfer state.
          }
          //
          // Write only one sector at once to make sure that the write operations are not buffered by the card.
          //
          NumSectorsAtOnce = 1;
        }
        //
        // Write the sector data to card.
        //
        if (NumSectorsAtOnce == 1u) {
          IsWriteOpenEnded = 0;
          //
          // Write one sector at a time.
          //
          r = _ExecWriteBlock(pInst, SectorIndex, SEGGER_CONSTPTR2PTR(const U32, pData8), &CardStatus);
        } else {
          IsWriteOpenEnded = 1;
          //
          // Prepares the storage device for a write operation.
          // For SD cards the sectors are pre-erased to increase performance.
          // For MMC devices a reliable write is started if required.
          //
          r = _PrepareWriteMultiple(pInst, NumSectorsAtOnce, &IsWriteOpenEnded, &CardStatus);
          if (r == 0) {
            //
            // Write more than one sector at a time.
            //
            r = _ExecWriteMultipleBlocks(pInst, SectorIndex, SEGGER_CONSTPTR2PTR(const U32, pData8), NumSectorsAtOnce, BurstType, &CardStatus);
          }
        }
        //
        // Request the card to write the buffered data (if any) and return to Transfer state.
        //
        if (IsWriteOpenEnded != 0) {
          Result = _ExecStopTransmission(pInst, &CardStatus);
          if (Result != 0) {
            r = Result;
          }
        }
        if (r != 0) {
          FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _WriteSectors: Could not write %lu sector(s) to sector index %lu.", NumSectorsAtOnce, SectorIndex));
          break;          // Error, could not transfer data.
        }
        //
        // Clear the card ready flag. Some eMMC devices (for example Micron MTFC2GMDEA) will
        // report the correct ready status only after the card status register is explicitly read via SEND_STATUS command.
        //
        FS_MEMSET(&CardStatus, 0, sizeof(CardStatus));
        //
        // Update the number of sectors written.
        //
        NumSectors  -= NumSectorsAtOnce;
        SectorIndex += NumSectorsAtOnce;
        if (BurstType == BURST_TYPE_NORMAL) {
          pData8    += NumSectorsAtOnce << BYTES_PER_SECTOR_SHIFT;
        }
      } while (NumSectors != 0u);
    }
    Result = _EnterPowerSaveModeIfRequired(pInst, &CardStatus);
    if (Result != 0) {
      r = Result;
    }
  }
  return r;
}

/*********************************************************************
*
*       _UnmountForced
*
*  Function description
*    Marks the storage device as not initialized.
*
*  Parameters
*    pInst   Driver instance.
*/
static void _UnmountForced(MMC_CM_INST * pInst) {
  pInst->IsInited   = 0;
  pInst->IsHWInited = 0;
}

/*********************************************************************
*
*       _Unmount
*
*  Function description
*    Marks the storage device as not initialized.
*
*  Parameters
*    pInst   Driver instance.
*
*  Additional information
*    This function waits for the storage device to complete the
*    last operation. In addition, it performs some clean up such
*    as enabling the internal pull-up on the DAT3 line and disabling
*    the internal cache of an eMMC device.
*/
static void _Unmount(MMC_CM_INST * pInst) {
  CARD_STATUS CardStatus;

  if (pInst->IsInited != 0u) {
    FS_MEMSET(&CardStatus, 0, sizeof(CardStatus));
#if (FS_MMC_SUPPORT_SD != 0) && (FS_MMC_DISABLE_DAT3_PULLUP != 0)
    (void)_EnableDAT3PullUpIfRequired(pInst, &CardStatus);
#endif // (FS_MMC_SUPPORT_SD != 0) && (FS_MMC_DISABLE_DAT3_PULLUP != 0)
    (void)_WaitForCardIdle(pInst, &CardStatus);
#if FS_MMC_SUPPORT_MMC
    (void)_DisableCacheIfRequired(pInst, &CardStatus);
#endif // FS_MMC_SUPPORT_MMC
  }
  _UnmountForced(pInst);
}

/*********************************************************************
*
*      _AllocInstIfRequired
*
*  Function description
*    Allocates memory for a driver instance.
*
*  Parameters
*    Unit       Index of the driver instance.
*
*  Return value
*    !=NULL     OK, driver instance allocated.
*    ==NULL     An error occurred.
*/
static MMC_CM_INST * _AllocInstIfRequired(U8 Unit) {
  MMC_CM_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  pInst = NULL;
  if (Unit < (U8)FS_MMC_NUM_UNITS) {
    pInst = _apInst[Unit];
    if (pInst == NULL) {
      pInst = SEGGER_PTR2PTR(MMC_CM_INST, FS_ALLOC_ZEROED((I32)sizeof(MMC_CM_INST), "MMC_CM_INST"));
      if (pInst != NULL) {
        _apInst[Unit] = pInst;
        pInst->Unit                     = Unit;
        pInst->IsBufferedWriteAllowed   = 1;
#if FS_MMC_SUPPORT_UHS
#if FS_MMC_SUPPORT_MMC
        pInst->IsHS200TuningRequested   = 1;
#endif // FS_MMC_SUPPORT_MMC
#if FS_MMC_SUPPORT_SD
        pInst->IsSDR104TuningRequested  = 1;
#endif // FS_MMC_SUPPORT_SD
#endif // FS_MMC_SUPPORT_UHS
#if FS_MMC_SUPPORT_MMC
        pInst->IsCacheActivationAllowed = 1;
#endif // FS_MMC_SUPPORT_MMC
      }
    }
  }
  return pInst;
}

/*********************************************************************
*
*      _GetInst
*
*  Function description
*    Returns a driver instance by its index.
*
*  Parameters
*    Unit       Index of the driver instance.
*
*  Return value
*    !=NULL     OK, driver instance returned.
*    ==NULL     An error occurred.
*/
static MMC_CM_INST * _GetInst(U8 Unit) {
  MMC_CM_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  pInst = NULL;
  if (Unit < (U8)FS_MMC_NUM_UNITS) {
    pInst = _apInst[Unit];
  }
  return pInst;
}

/*********************************************************************
*
*      _InitIfRequired
*/
static int _InitIfRequired(MMC_CM_INST * pInst) {
  int r;

  r = 0;      // Set to indicate success.
  if (pInst->IsInited == 0u) {
    r = _Init(pInst);
  }
  return r;
}

/*********************************************************************
*
*      _ContainsSamePattern
*/
static int _ContainsSamePattern(const void * pData) {
  const U32 * pData32;
  unsigned    NumWords;
  U32         Pattern;

  pData32  = SEGGER_CONSTPTR2PTR(const U32, pData);
  NumWords = 1u << (BYTES_PER_SECTOR_SHIFT - 2);    // -2 since we process 4 bytes at a time.
  Pattern  = *pData32++;
  --NumWords;
  do {
    if (Pattern != *pData32++) {
      return 0;                     // Different data patterns.
    }
  } while (--NumWords != 0u);
  return 1;                         // Same data pattern.
}

/*********************************************************************
*
*       Static code (public via callback)
*
**********************************************************************
*/

/*********************************************************************
*
*       _MMC_GetStatus
*
*  Function description
*    Gets the status of the storage media.
*
*  Parameters
*    Unit   Unit number.
*
*  Return value
*    FS_MEDIA_STATE_UNKNOWN   State of the storage media is unknown.
*    FS_MEDIA_NOT_PRESENT     Card is not present.
*    FS_MEDIA_IS_PRESENT      Card is present.
*/
static int _MMC_GetStatus(U8 Unit) {
  MMC_CM_INST * pInst;
  int           Status;

  Status = FS_MEDIA_STATE_UNKNOWN;
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    ASSERT_HW_TYPE_IS_SET(pInst);
    _InitHWIfRequired(pInst);
    Status = _IsPresent(pInst);     // Get the status from HW.
  }
  return Status;
}

/*********************************************************************
*
*       _MMC_IoCtl
*
*  Function description
*    Executes a device command.
*
*  Parameters
*    Unit      Device index.
*    Cmd       Command to be executed.
*    Aux       Parameter depending on command.
*    pBuffer   Pointer to a buffer used for the command.
*
*  Return value
*    Command specific. In general a negative value means an error.
*/
static int _MMC_IoCtl(U8 Unit, I32 Cmd, I32 Aux, void * pBuffer) {
  FS_DEV_INFO * pDevInfo;
  MMC_CM_INST * pInst;
  int           r;

  FS_USE_PARA(Aux);
  r     = -1;                     // Set to indicate an error.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    ASSERT_HW_TYPE_IS_SET(pInst);
    switch (Cmd) {
    case FS_CMD_UNMOUNT:
      _Unmount(pInst);
      r = 0;
      break;
    case FS_CMD_UNMOUNT_FORCED:
      _UnmountForced(pInst);
      r = 0;
      break;
    case FS_CMD_GET_DEVINFO:      // Get general device information.
      if (pInst->HasError == 0u) {
        r = 0;
        if (pInst->IsInited == 0u) {
          r = _Init(pInst);
        }
        if (r == 0) {
          pDevInfo = SEGGER_PTR2PTR(FS_DEV_INFO, pBuffer);
          pDevInfo->BytesPerSector = (U16)BYTES_PER_SECTOR;
          pDevInfo->NumSectors     = pInst->NumSectors;
        }
      }
      break;
    case FS_CMD_FREE_SECTORS:
#if FS_MMC_SUPPORT_TRIM
      {
        U32         StartSector;
        U32         NumSectors;
        CARD_STATUS CardStatus;

        StartSector = (U32)Aux;
        NumSectors  = *SEGGER_PTR2PTR(U32, pBuffer);
        r = _SelectCardWithBusyWait(pInst, &CardStatus);
        if (r == 0) {
          r = _Trim(pInst, StartSector, NumSectors, &CardStatus);
        }
      }
#else
      //
      // Return OK even if we do nothing here in order to
      // prevent that the file system reports an error.
      //
      r = 0;
#endif  // FS_MMC_SUPPORT_TRIM
      break;
#if FS_SUPPORT_DEINIT
    case FS_CMD_DEINIT:
      FS_FREE(pInst);
      _apInst[Unit] = NULL;
      _NumUnits--;
      r = 0;
      break;
#endif  // FS_SUPPORT_DEINIT
    default:
      //
      // Error, command not supported.
      //
      break;
    }
  }
  return r;
}

/*********************************************************************
*
*       _MMC_Write
*
*  Function description
*    Write one ore more sectors to the media.
*
*  Parameters
*    Unit         Device index number.
*    SectorIndex  Sector to be written to the device.
*    pData        Pointer to data to be stored.
*    NumSectors   Number of sectors to be transferred.
*    RepeatSame   Shall be the same data written.
*
*  Return value
*    ==0    Sector has been written to the device.
*    !=0    An error has occurred.
*/
static int _MMC_Write(U8 Unit, U32 SectorIndex, const void * pData, U32 NumSectors, U8 RepeatSame) {
  MMC_CM_INST * pInst;
  int           r;
  int           NumRetries;
  unsigned      MaxWriteBurst;
  unsigned      MaxWriteBurstRepeat;
  unsigned      MaxWriteBurstFill;
  U8            BurstType;

  r     = 1;                // Set to indicate error.
  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;               // Error, could not get driver instance.
  }
  if (pInst->IsInited == 0u) {
    FS_DEBUG_WARN((FS_MTYPE_DRIVER, "MMC_CM: _MMC_Write: Card is not initialized."));
  } else {
    ASSERT_HW_TYPE_IS_SET(pInst);
    MaxWriteBurst = pInst->MaxWriteBurst;
    BurstType     = BURST_TYPE_NORMAL;
    if (RepeatSame != 0u) {
      //
      // The same data has to be written to all sectors.
      //
      MaxWriteBurst       = 1;    // Per default we write only one sector at a time.
      MaxWriteBurstRepeat = pInst->MaxWriteBurstRepeat;
      MaxWriteBurstFill   = pInst->MaxWriteBurstFill;
      BurstType           = BURST_TYPE_REPEAT;
      if (MaxWriteBurstRepeat != 0u) {
        MaxWriteBurst = MaxWriteBurstRepeat;
      }
      if (MaxWriteBurstFill > MaxWriteBurst) {
        if (_ContainsSamePattern(pData) != 0) {
          //
          // Write the same 32-bit value to all sectors.
          //
          MaxWriteBurst = MaxWriteBurstFill;
          BurstType     = BURST_TYPE_FILL;
        }
      }
    }
    NumRetries   = FS_MMC_NUM_RETRIES;
    SectorIndex += pInst->StartSector;
    for (;;) {
      if (pInst->IsWriteProtected != 0u) {
        FS_DEBUG_WARN((FS_MTYPE_DRIVER, "MMC_CM: _MMC_Write: Card is write protected."));
        break;                    // Error, the card is write protected.
      }
      if (pInst->HasError != 0u) {
        break;                    // Error, the card has to be remounted.
      }
      r = _WriteSectors(pInst, SectorIndex, pData, NumSectors, BurstType, MaxWriteBurst);
      if (r == 0) {
        IF_STATS(pInst->StatCounters.WriteSectorCnt += NumSectors);
        break;                    // OK, data written.
      }
      if (NumRetries == 0) {
        break;                    // Error, maximum number of retries has been reached.
      }
      --NumRetries;
      if (MaxWriteBurst != 1u) {
        FS_DEBUG_WARN((FS_MTYPE_DRIVER, "MMC_CM: _MMC_Write: Falling back to single sector write mode."));
        MaxWriteBurst = 1;        // Fall back to single sector write.
      }
      IF_STATS(pInst->StatCounters.WriteErrorCnt++);
    }
    if (NumRetries < FS_MMC_NUM_RETRIES) {
      if (MaxWriteBurst == 1u) {
        FS_DEBUG_WARN((FS_MTYPE_DRIVER, "MMC_CM: _MMC_Write: Restore multiple sector write mode."));
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _MMC_WriteRO
*
*  Function description
*    Returns an error when called to indicate that the write operation
*    is not allowed.
*
*  Parameters
*    Unit         Device index number.
*    SectorIndex  Sector to be written to the device.
*    pData        Pointer to data to be stored.
*    NumSectors   Number of sectors to be transferred.
*    RepeatSame   Shall be the same data written.
*
*  Return value
*    !=0    An error has occurred.
*/
static int _MMC_WriteRO(U8 Unit, U32 SectorIndex, const void * pData, U32 NumSectors, U8 RepeatSame) {
  FS_USE_PARA(Unit);
  FS_USE_PARA(SectorIndex);
  FS_USE_PARA(pData);
  FS_USE_PARA(NumSectors);
  FS_USE_PARA(RepeatSame);
  FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "MMC_CM: _MMC_WriteRO: Operation not supported."));
  return 1;
}

/*********************************************************************
*
*       _MMC_Read
*
*  Function description
*    Reads one or more sectors from the SD/MMC card.
*
*  Parameters
*    Unit         Device index number.
*    SectorIndex  Sector to be read from the device.
*    p            Pointer to buffer to be stored.
*    NumSectors   Number of sectors to be transferred.
*
*  Return value
*    ==0    All sectors have been read.
*    !=0    An error has occurred.
*/
static int _MMC_Read(U8 Unit, U32 SectorIndex, void * p, U32 NumSectors) {
  MMC_CM_INST * pInst;
  int           r;
  U8          * pBuffer;
  U32           NumSectorsAtOnce;

  r     = 1;                // Set to indicate error.
  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;               // Error, could not get driver instance.
  }
  if (pInst->IsInited == 0u) {
    FS_DEBUG_WARN((FS_MTYPE_DRIVER, "MMC_CM: _MMC_Read: Card is not initialized."));
  } else {
    ASSERT_HW_TYPE_IS_SET(pInst);
    SectorIndex += pInst->StartSector;
    pBuffer = SEGGER_PTR2PTR(U8, p);
    //
    // Workaround for some SD cards that report an error
    // when a multiple read operation ends on a last sector.
    //
    NumSectorsAtOnce = NumSectors;
#if FS_MMC_READ_SINGLE_LAST_SECTOR
    {
      U32 NumSectorsTotal;

      NumSectorsTotal = pInst->NumSectors;
      if ((NumSectors > 1u) && ((SectorIndex + NumSectors) >= NumSectorsTotal)) {
        if (NumSectorsAtOnce < (U32)FS_MMC_READ_SINGLE_LAST_SECTOR) {
          NumSectorsAtOnce = 0;
        } else {
          NumSectorsAtOnce -= (U32)FS_MMC_READ_SINGLE_LAST_SECTOR;
        }
      }
    }
#endif // FS_MMC_READ_SINGLE_LAST_SECTOR
    r = 0;
    if (NumSectorsAtOnce != 0u) {
      r = _ReadSectorsWithRetry(pInst, SectorIndex, pBuffer, NumSectorsAtOnce);
    }
    if (r == 0) {
      NumSectors -= NumSectorsAtOnce;
      if (NumSectors != 0u) {
        SectorIndex += NumSectorsAtOnce;
        pBuffer     += NumSectorsAtOnce * BYTES_PER_SECTOR;
        //
        // Perform single read operations.
        //
        do {
          r = _ReadSectorsWithRetry(pInst, SectorIndex, pBuffer, 1);
          ++SectorIndex;
          pBuffer += BYTES_PER_SECTOR;
        } while (--NumSectors != 0u);
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _MMC_InitMedium
*
*  Function description
*    Initialize the card.
*
*  Parameters
*    Unit  Unit number.
*
*  Return value
*    ==0    Card initialized and ready for operation.
*    !=0    An error has occurred.
*/
static int _MMC_InitMedium(U8 Unit) {
  int r;
  MMC_CM_INST * pInst;

  r = 1;                        // Set to indicate an error.
  pInst = _apInst[Unit];
  if (pInst != NULL) {
    ASSERT_HW_TYPE_IS_SET(pInst);
    r = _InitIfRequired(pInst);
  }
  return r;
}

/*********************************************************************
*
*       _MMC_AddDevice
*
*  Function description
*    Initializes the low-level driver object.
*
*  Return value
*    >=0      Instance created, Unit no.
*    < 0      Error, could not add device
*/
static int _MMC_AddDevice(void) {
  U8            Unit;
  MMC_CM_INST * pInst;

  if (_NumUnits >= (U8)FS_MMC_NUM_UNITS) {
    return -1;                          // Error, too many driver instances.
  }
  Unit = _NumUnits;
  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return -1;                          // Error, could not allocate driver instance.
  }
  _NumUnits++;
  return (int)Unit;
}

/*********************************************************************
*
*       _MMC_GetNumUnits
*/
static int _MMC_GetNumUnits(void) {
  return (int)_NumUnits;
}

/*********************************************************************
*
*       _MMC_GetDriverName
*/
static const char * _MMC_GetDriverName(U8 Unit) {
  FS_USE_PARA(Unit);
  return "mmc";
}

/*********************************************************************
*
*       Public data
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_MMC_CM_Driver
*/
const FS_DEVICE_TYPE FS_MMC_CM_Driver = {
  _MMC_GetDriverName,
  _MMC_AddDevice,
  _MMC_Read,
  _MMC_Write,
  _MMC_IoCtl,
  _MMC_InitMedium,
  _MMC_GetStatus,
  _MMC_GetNumUnits
};

/*********************************************************************
*
*       FS_MMC_CM_RO_Driver
*/
const FS_DEVICE_TYPE FS_MMC_CM_RO_Driver = {
  _MMC_GetDriverName,
  _MMC_AddDevice,
  _MMC_Read,
  _MMC_WriteRO,
  _MMC_IoCtl,
  _MMC_InitMedium,
  _MMC_GetStatus,
  _MMC_GetNumUnits
};

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_MMC_CM_Allow4bitMode
*
*  Function description
*    Allows the driver to exchange the data via 4 lines.
*
*  Parameters
*    Unit     Index of the driver instance (0-based).
*    OnOff    Specifies the permission status.
*             * 0 Data is exchanged via 1 line.
*             * 1 Data is exchanged via 4 lines.
*
*  Additional information
*    This function is optional. By default, the 4-bit mode is disabled
*    which means that the Card Mode MMC/SD driver exchanges data via only
*    one data line. Using 4-bit mode can help increase the performance
*    of the data transfer. The 4-bit mode is used for the data transfer
*    only if the connected MMC/SD card supports it which is typically the
*    case with all modern cards. If not then the Card Mode MMC/SD driver
*    falls back to 1-bit mode.
*
*    The application can query the actual number of data lines used by
*    the Card Mode MMC/SD driver for the data transfer by evaluating the
*    value of BusWith member of the FS_MMC_CARD_INFO structure returned
*    by FS_MMC_CM_GetCardInfo().
*
*    An application is permitted to call this function only
*    at the file system initialization in FS_X_AddDevices().
*/
void FS_MMC_CM_Allow4bitMode(U8 Unit, U8 OnOff) {
  MMC_CM_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->Is4bitModeAllowed = OnOff;
  }
}

/*********************************************************************
*
*       FS_MMC_CM_Allow8bitMode
*
*  Function description
*    Allows the driver to exchange the data via 8 data lines.
*
*  Parameters
*    Unit     Index of the driver instance (0-based).
*    OnOff    Specifies the permission status.
*             * 0 Data is exchanged via 1 or 4 lines.
*             * 1 Data is exchanged via 8 lines.
*
*  Additional information
*    This function is optional. By default, the 8-bit mode is disabled
*    which means that the Card Mode MMC/SD driver exchanges data via only
*    one data line. Using 8-bit mode can help increase the performance
*    of the data transfer. The 8-bit mode is used for the data transfer
*    only if the connected MMC/SD card supports it. If not then the Card
*    mode MMC/SD driver falls back to either 4- or 1-bit mode. Only MMC
*    devices support the 8-bit mode. The SD cards are not able to transfer
*    the data via 8-bit lines.
*
*    The application can query the actual number of data lines used by
*    the Card Mode MMC/SD driver for the data transfer by evaluating the
*    value of BusWith member of the FS_MMC_CARD_INFO structure returned
*    by FS_MMC_CM_GetCardInfo().
*
*    An application is permitted to call this function only
*    at the file system initialization in FS_X_AddDevices().
*/
void FS_MMC_CM_Allow8bitMode(U8 Unit, U8 OnOff) {
  MMC_CM_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->Is8bitModeAllowed = OnOff;
  }
}

/*********************************************************************
*
*       FS_MMC_CM_AllowHighSpeedMode
*
*  Function description
*    Allows the driver to exchange the data in high speed mode.
*
*  Parameters
*    Unit     Index of the driver instance (0-based).
*    OnOff    Specifies the permission status.
*             * 0   Use standard speed mode.
*             * 1   Use high speed mode.
*
*  Additional information
*    This function is optional. The application can use this function
*    to request the Card Mode MMC/SD driver to use the highest clock
*    frequency supported by the used MMC/SD card. The standard clock
*    frequency supported by an SD card is 25 MHz and 26 MHz by an MMC device.
*    This is the clock frequency used by the Card Mode MMC/SD driver
*    after the initialization of the MMC/SD card. However, most of the
*    modern SD cards and MMC devices are able to exchange the data at
*    higher clock frequencies up to 50 MHz for SD cards and 52 MHz for MMC
*    devices. This high speed mode has to be explicitly enabled in the
*    SD card or MMC device after initialization. The Card mode SD/MMC driver
*    automatically enables the high speed mode in the SD card or MMC device
*    if FS_MMC_CM_AllowHighSpeedMode() is called with OnOff set to 1 and
*    the used SD card or MMC device actually supported.
*
*    The application can check if the Card Mode MMC/SD driver is actually
*    using the high speed mode for the data transfer by evaluating the
*    IsHighSpeedMode member of the FS_MMC_CARD_INFO structure returned
*    by FS_MMC_CM_GetCardInfo().
*
*    The high speed mode can be used only if the SD/MMC host controller
*    supports it. The availability of this functionality is not checked
*    by the Card Mode MMC/SD driver.
*
*    An application is permitted to call this function only
*    at the file system initialization in FS_X_AddDevices().
*/
void FS_MMC_CM_AllowHighSpeedMode(U8 Unit, U8 OnOff) {
  MMC_CM_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->IsHSModeAllowed = OnOff;
  }
}

/*********************************************************************
*
*      FS_MMC_CM_GetCardId
*
*  Function description
*    Returns the identification data of the SD/MMC device.
*
*  Parameters
*    Unit     Index of the driver instance (0-based).
*    pCardId  [OUT] Card identification data.
*
*  Return value
*    ==0    CardId has been read.
*    !=0    An error has occurred.
*
*  Additional information
*    This function is optional. The application can call this
*    function to get the information stored in the CID register
*    of an MMC or SD card. The CID register stores information which
*    can be used to uniquely identify the card such as serial number,
*    product name, manufacturer id, etc. For more information about
*    the information stored in this register refer to SD or MMC specification.
*    The data of the CID register starts at byte offset 1
*    in pCardId->aData.
*/
int FS_MMC_CM_GetCardId(U8 Unit, FS_MMC_CARD_ID * pCardId) {
  int            r;
  MMC_CM_INST  * pInst;
  CARD_STATUS    CardStatus;
  CID_RESPONSE   cid;
  int            Result;

  if (pCardId == NULL) {
    return 1;       // Error, invalid parameter.
  }
  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;       // Error, could not allocate driver instance.
  }
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return 1;       // Error, could not initialize the SD/MMC card.
  }
  r = _LeavePowerSaveModeIfRequired(pInst, &CardStatus);
  if (r != 0) {
    return 1;
  }
  r = _DeSelectCardIfRequired(pInst, &CardStatus);
  if (r == 0) {
    FS_MEMSET(&cid, 0, sizeof(cid));
    r = _ExecSendCID(pInst, &cid);
    if (r == 0) {
      FS_MEMCPY(pCardId, &cid, sizeof(*pCardId));
    }
  }
  Result = _EnterPowerSaveModeIfRequired(pInst, &CardStatus);
  if (r == 0) {
    r = Result;
  }
  return r;
}

/*********************************************************************
*
*      FS_MMC_CM_UnlockCardForced
*
*  Function description
*    Unlocks an SD card.
*
*  Parameters
*    Unit     Index of the driver instance (0-based).
*
*  Return value
*    ==0    OK, the SD card has been erased.
*    !=0    An error has occurred.
*
*  Additional information
*    This function is optional. SD cards can be locked with a password
*    in order to prevent inadvertent access to sensitive data. It is
*    not possible to access the data on a locked SD card without knowing
*    the locking password. The application can use FS_MMC_CM_UnlockCardForced()
*    to make locked SD card accessible again. The unlocking operation erases
*    all the data stored on the SD card including the lock password.
*/
int FS_MMC_CM_UnlockCardForced(U8 Unit) {
  int           r;
  MMC_CM_INST * pInst;
  CARD_STATUS   CardStatus;
  int           Result;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;       // Error, could not allocate driver instance.
  }
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return 1;       // Error, could not initialize the card.
  }
  r = _LeavePowerSaveModeIfRequired(pInst, &CardStatus);
  if (r != 0) {
    return 1;
  }
  r = _SelectCardWithBusyWait(pInst, &CardStatus);
  if (r == 0) {
    r = _UnlockForced(pInst, &CardStatus);
    if (r == 0) {
      r = _IsCardLocked(&CardStatus);
      if (r != 0) {
        r = 1;          // Error, card is still locked.
      }
    }
  }
  Result = _EnterPowerSaveModeIfRequired(pInst, &CardStatus);
  if (r == 0) {
    r = Result;
  }
  return r;
}

/*********************************************************************
*
*      FS_MMC_CM_Erase
*
*  Function description
*    Erases the contents of one or more logical sectors.
*
*  Parameters
*    Unit         Index of the driver instance (0-based).
*    StartSector  Index of the first sector to be erased.
*    NumSectors   Number of sector to be erased.
*
*  Return value
*    ==0    Sectors erased.
*    !=0    An error has occurred.
*
*  Additional information
*    This function is optional. The application can use it to set the
*    contents of the specified logical sectors to a predefined value.
*    The erase operation sets all the bits in the specified logical
*    sectors either to 1 or to 0. The actual value is implementation
*    defined in the EXT_CSD register.
*
*    The erase operation is supported only for MMC devices.
*/
int FS_MMC_CM_Erase(U8 Unit, U32 StartSector, U32 NumSectors) {
  int           r;
  MMC_CM_INST * pInst;
  CARD_STATUS   CardStatus;
  int           Result;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;       // Error, could not allocate driver instance.
  }
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return 1;       // Error, could not initialize the card.
  }
  r = _LeavePowerSaveModeIfRequired(pInst, &CardStatus);
  if (r != 0) {
    return 1;
  }
  r = _SelectCardWithBusyWait(pInst, &CardStatus);
  if (r == 0) {
    StartSector += pInst->StartSector;
    r = _Erase(pInst, StartSector, NumSectors, &CardStatus);
  }
  Result = _EnterPowerSaveModeIfRequired(pInst, &CardStatus);
  if (r == 0) {
    r = Result;
  }
  return r;
}

/*********************************************************************
*
*      FS_MMC_CM_SetHWType
*
*  Function description
*    Configures the HW access routines.
*
*  Parameters
*    Unit         Index of the driver instance (0-based).
*    pHWType      [IN] Specifies the type of the hardware layer to be used
*                 to communicate with the storage device.
*
*  Additional information
*    This function is mandatory. It has to be called in FS_X_AddDevices()
*    once for each instance of the card mode SD/MMC driver. The driver
*    instance is identified by the Unit parameter.
*/
void FS_MMC_CM_SetHWType(U8 Unit, const FS_MMC_HW_TYPE_CM * pHWType) {
  MMC_CM_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->pHWType = pHWType;
  }
}

/*********************************************************************
*
*      FS_MMC_CM_GetCardInfo
*
*  Function description
*    This function returns information about the SD/MMC device.
*
*  Parameters
*    Unit         Index of the driver instance (0-based).
*    pCardInfo    [OUT] Information about the device.
*
*  Return value
*    ==0    Card information returned.
*    !=0    An error has occurred.
*
*  Additional information
*    This function is optional. It can be used to get information about the
*    type of the storage card used, about how many data lines are used for
*    the data transfer, etc.
*/
int FS_MMC_CM_GetCardInfo(U8 Unit, FS_MMC_CARD_INFO * pCardInfo) {
  int           r;
  MMC_CM_INST * pInst;

  if (pCardInfo == NULL) {
    return 1;       // Error, invalid parameter.
  }
  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;       // Error, could not allocate driver instance.
  }
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return 1;       // Error, could not initialize the SD/MMC card.
  }
  pCardInfo->BusWidth         = pInst->BusWidth;
  pCardInfo->BytesPerSector   = (U16)BYTES_PER_SECTOR;
  pCardInfo->CardType         = pInst->CardType;
  pCardInfo->IsHighSpeedMode  = (pInst->AccessMode == FS_MMC_ACCESS_MODE_HS) ? 1u : 0u;
  pCardInfo->IsWriteProtected = pInst->IsWriteProtected;
  pCardInfo->NumSectors       = pInst->NumSectors;
  pCardInfo->AccessMode       = pInst->AccessMode;
  pCardInfo->VoltageLevel     = pInst->VoltageLevel;
  pCardInfo->ClockFreq        = pInst->Freq_kHz * 1000uL;
#if FS_MMC_SUPPORT_UHS
  pCardInfo->DriverStrength   = pInst->DriverStrengthActive;
#else
  pCardInfo->DriverStrength   = 0;
#endif // FS_MMC_SUPPORT_UHS
  return 0;         // OK, information returned.
}

#if FS_MMC_ENABLE_STATS

/*********************************************************************
*
*       FS_MMC_CM_GetStatCounters
*
*  Function description
*    Returns the value of statistical counters.
*
*  Parameters
*    Unit       Index of the driver instance (0-based).
*    pStat      [OUT] Current value of statistical counters.
*
*  Additional information
*    This function is optional. The Card mode SD/MMC driver collects
*    statistics about the number of internal operations such as the
*    number of logical sectors read or written by the file system layer.
*    The application can use FS_MMC_CM_GetStatCounters() to get the
*    current value of these counters. The statistical counters are
*    automatically set to 0 when the storage device is mounted or when
*    the application calls FS_MMC_CM_ResetStatCounters().
*
*    The statistical counters are available only when the file system
*    is compiled with FS_DEBUG_LEVEL greater than or equal to
*    FS_DEBUG_LEVEL_CHECK_ALL or with FS_MMC_ENABLE_STATS set to 1.
*/
void FS_MMC_CM_GetStatCounters(U8 Unit, FS_MMC_STAT_COUNTERS * pStat) {
  MMC_CM_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    if (pStat != NULL) {
      *pStat = pInst->StatCounters;     // Struct copy
    }
  } else {
    FS_MEMSET(pStat, 0, sizeof(FS_MMC_STAT_COUNTERS));
  }
}

/*********************************************************************
*
*       FS_MMC_CM_ResetStatCounters
*
*  Function description
*    Sets to 0 all statistical counters.
*
*  Parameters
*    Unit       Index of the driver instance (0-based).
*
*  Additional information
*    This function is optional. The statistical counters are
*    automatically set to 0 when the storage device is mounted.
*    The application can use FS_MMC_CM_ResetStatCounters() at any
*    time during the file system operation. The statistical counters
*    can be queried via FS_MMC_CM_GetStatCounters().
*
*    The statistical counters are available only when the file system
*    is compiled with FS_DEBUG_LEVEL greater than or equal to
*    FS_DEBUG_LEVEL_CHECK_ALL or with FS_MMC_ENABLE_STATS set to 1.
*/
void FS_MMC_CM_ResetStatCounters(U8 Unit) {
  MMC_CM_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    FS_MEMSET(&pInst->StatCounters, 0, sizeof(FS_MMC_STAT_COUNTERS));
  }
}

#endif // FS_MMC_ENABLE_STATS

/*********************************************************************
*
*      FS_MMC_CM_ReadExtCSD
*
*  Function description
*    Reads the contents of the EXT_CSD register of an MMC device.
*
*  Parameters
*    Unit       Index of the driver instance (0-based).
*    pBuffer    [OUT] Contents of the EXT_CSD register.
*
*  Return value
*    ==0    Register contents returned.
*    !=0    An error has occurred.
*
*  Additional information
*    This function is optional. For more information about the contents
*    of the EXT_CSD register refer to the MMC specification.
*    The contents of the EXT_CSD register can be modified via
*    FS_MMC_CM_WriteExtCSD().
*
*    pBuffer has to be 512 at least bytes large.
*/
int FS_MMC_CM_ReadExtCSD(U8 Unit, U32 * pBuffer) {
  int           r;
  MMC_CM_INST * pInst;
  CARD_STATUS   CardStatus;
  int           Result;

  if (pBuffer == NULL) {
    return 1;       // Error, invalid parameter.
  }
  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;       // Error, could not allocate driver instance.
  }
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return 1;       // Error, could not initialize the SD/MMC card.
  }
  r = _LeavePowerSaveModeIfRequired(pInst, &CardStatus);
  if (r != 0) {
    return 1;
  }
  r = _SelectCardWithBusyWait(pInst, &CardStatus);
  if (r == 0) {
    r = _ExecSendExtCSD(pInst, 0, pBuffer, &CardStatus);      // 0 means that _ExecSendExtCSD() should choose the correct bus width.
  }
  Result = _EnterPowerSaveModeIfRequired(pInst, &CardStatus);
  if (r == 0) {
    r = Result;
  }
  return r;
}

/*********************************************************************
*
*      FS_MMC_CM_WriteExtCSD
*
*  Function description
*    Writes to the EXT_CSD register of the MMC device.
*
*  Parameters
*    Unit       Index of the driver instance (0-based).
*    Off        Byte offset in the EXT_CSD register.
*    pData      [IN] Register contents.
*    NumBytes   Number of bytes in pData buffer.
*
*  Return value
*    ==0    Register contents returned
*    !=0    An error has occurred
*
*  Additional information
*    This function is optional. Only the byte range 0-191 of the EXT_CSD
*    is modifiable. For more information about the contents of the EXT_CSD
*    register refer to the MMC specification. The contents of the EXT_CSD
*    register can be read via FS_MMC_CM_ReadExtCSD().
*/
int FS_MMC_CM_WriteExtCSD(U8 Unit, unsigned Off, const U8 * pData, unsigned NumBytes) {
  int           r;
  MMC_CM_INST * pInst;
  CARD_STATUS   CardStatus;
  int           Result;

  if (pData == NULL) {
    return 1;       // Error, invalid parameter.
  }
  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;       // Error, could not allocate driver instance.
  }
  r = _InitIfRequired(pInst);
  if (r != 0) {
    return 1;       // Error, could not initialize the card.
  }
  r = _LeavePowerSaveModeIfRequired(pInst, &CardStatus);
  if (r != 0) {
    return 1;
  }
  r = _SelectCardWithBusyWait(pInst, &CardStatus);
  if (r == 0) {
    if (NumBytes != 0u) {
      do {
        r = _WriteExtCSDByte(pInst, (int)Off++, (int)*pData++, &CardStatus);
        if (r != 0) {
          break;
        }
      } while (--NumBytes != 0u);
    }
  }
  Result = _EnterPowerSaveModeIfRequired(pInst, &CardStatus);
  if (r == 0) {
    r = Result;
  }
  return r;
}

/*********************************************************************
*
*       FS_MMC_CM_AllowReliableWrite
*
*  Function description
*    Allows the driver to use reliable write operations for MMC devices.
*
*  Parameters
*    Unit       Index of the driver instance (0-based).
*    OnOff      Specifies the permission status.
*               * 0   Reliable write operation is disabled (default).
*               * 1   Reliable write operation is enabled.
*
*  Additional information
*    This function is optional. A reliable write operation makes sure
*    that the sector data is not corrupted in case of a unexpected reset.
*    MMC devices compliant with the version 4.3 or newer of the MMC specification
*    support a fail-safe write feature which makes sure that the old data
*    remains unchanged until the new data is successfully programmed.
*    Using this type of write operation the data remains valid in case of an
*    unexpected reset which improves the data reliability. The support for this
*    feature is optional and the Card Mode MMC/SD driver activates it only if
*    the used MMC device actually supports it and it FS_MMC_CM_AllowReliableWrite()
*    has been called with OnOff set to 1.
*
*    Please note that enabling the reliable write feature can possibly reduce
*    the write performance.
*
*    An application is permitted to call this function only
*    at the file system initialization in FS_X_AddDevices().
*/
void FS_MMC_CM_AllowReliableWrite(U8 Unit, U8 OnOff) {
  MMC_CM_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->IsReliableWriteAllowed = OnOff;
  }
}

/*********************************************************************
*
*       FS_MMC_CM_AllowBufferedWrite
*
*  Function description
*    Enables / disables the write buffering.
*
*  Parameters
*    Unit       Index of the driver instance (0-based).
*    OnOff      Specifies if the buffered write operation should be enabled or not.
*               * 0   Buffered write operation is disabled.
*               * 1   BUffered write operation is enabled.
*
*  Additional information
*    SD and MMC storage devices can perform write operations in parallel
*    to receiving data from the host by queuing write requests. This feature is
*    used by the driver in order to achieve the highest write performance possible.
*    In case of a power fail the hardware has to prevent that the write operation
*    is interrupted by powering the storage device until the write queue is emptied.
*    The time it takes the storage device to empty the queue is not predictable
*    and it can take from a few hundreds o milliseconds to a few seconds to complete.
*    This function allows the application to disable the buffered write and thus
*    reduce the time required to supply the storage device at power fail.
*    With the write buffering disabled the driver writes only one sector at time
*    and it waits for the previous write sector operation to complete.
*
*    Disabling the write buffering can considerably reduce the write performance.
*    Most of the industrial grade SD and MMC storage devices are fail safe
*    so that disabling the write buffering is not required. For more information
*    consult the data sheet of your storage device.
*
*    An application is permitted to call this function only
*    at the file system initialization in FS_X_AddDevices().
*/
void FS_MMC_CM_AllowBufferedWrite(U8 Unit, U8 OnOff) {
  MMC_CM_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->IsBufferedWriteAllowed = OnOff;
  }
}

/*********************************************************************
*
*       FS_MMC_CM_SetSectorRange
*
*  Function description
*    Configures an area for data storage.
*
*  Parameters
*    Unit           Index of the driver instance (0-based).
*    StartSector    Index of the first sector to be used as storage.
*    MaxNumSectors  Maximum number of sectors to be used as storage.
*
*  Additional information
*    This function is optional. It allows an application to use only
*    a specific area of an SD/MMC storage device as storage. By default
*    the Card Mode MMC/SD driver uses the entire available space as storage.
*
*    StartSector is relative to the beginning of the SD/MMC storage device.
*    For example, if StartSector is set to 3 than the sectors with
*    the indexes 0, 1, and 2 are not used for storage.
*    The initialization of SD/MMC storage device fails if StartSector
*    is out of range.
*
*    If MaxNumSectors is set to 0 the Card mode SD/MMC driver uses for storage
*    the remaining sectors starting from StartSector. If MaxNumSectors
*    is larger than the available number or sectors the actual number
*    of sectors used for storage is limited to the number of sectors
*    available.
*/
void FS_MMC_CM_SetSectorRange(U8 Unit, U32 StartSector, U32 MaxNumSectors) {
  MMC_CM_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->StartSector   = StartSector;
    pInst->MaxNumSectors = MaxNumSectors;
  }
}

#if FS_MMC_SUPPORT_POWER_SAVE

/*********************************************************************
*
*       FS_MMC_CM_AllowPowerSaveMode
*
*  Function description
*    Configures if the driver has to request the eMMC to save power.
*
*  Parameters
*    Unit       Index of the driver instance (0-based).
*    OnOff      Specifies if the MMC device has to be put to sleep between data transfers
*               * 0  The MMC device remains active.
*               * 1  The MMC device is put to sleep.
*
*  Return value
*    ==0  OK, feature configured.
*    !=0  An error occurred.
*
*  Additional information
*    This function is optional and active only if the sources
*    are compiled with the FS_MMC_SUPPORT_POWER_SAVE set to 1.
*/
int FS_MMC_CM_AllowPowerSaveMode(U8 Unit, U8 OnOff) {
  MMC_CM_INST * pInst;
  int           r;

  r = FS_ERRCODE_INVALID_PARA;
  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->IsPowerSaveModeAllowed = OnOff;
    r = FS_ERRCODE_OK;
  }
  return r;
}

/*********************************************************************
*
*       FS_MMC_CM_EnterPowerSaveMode
*
*  Function description
*    Puts the MMC to sleep in order to save power.
*
*  Parameters
*    Unit       Index of the driver instance (0-based).
*
*  Return value
*    ==0    The eMMC device is in Sleep state.
*    !=0    An error occurred.
*
*  Additional information
*    This function is optional. It can be used to explicitly put the
*    eMMC device into Sleep state in order to reduce power consumption.
*
*    This function is active only if the sources are compiled
*    with the define FS_MMC_SUPPORT_POWER_SAVE set to 1.
*/
int FS_MMC_CM_EnterPowerSaveMode(U8 Unit) {
  MMC_CM_INST * pInst;
  CARD_STATUS   CardStatus;
  int           r;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return 1;       // Error, could not allocate driver instance.
  }
  r = _InitIfRequired(pInst);
  if (r == 0) {
    r = _EnterPowerSaveModeIfRequired(pInst, &CardStatus);
  }
  return r;
}

#endif // FS_MMC_SUPPORT_POWER_SAVE

#if FS_MMC_SUPPORT_UHS

#if FS_MMC_SUPPORT_SD

/*********************************************************************
*
*       FS_MMC_CM_AllowAccessModeDDR50
*
*  Function description
*    Allows the driver to exchange the data with an SD card using DDR50 access mode.
*
*  Parameters
*    Unit     Index of the driver instance (0-based).
*    OnOff    Specifies the permission status.
*             * 0   Use another access mode.
*             * 1   Use DDR50 access mode.
*
*  Additional information
*    This function is optional. The application can use it to request the Card Mode
*    MMC/SD driver to exchange data with an SD card on both clock edges and at a
*    clock frequency of maximum 50 MHz. The voltage level of the I/O lines used
*    by this access mode is 1.8 V. The support for the 1.8 V voltage level can be enabled
*    vis FS_MMC_CM_AllowVoltageLevel1V8(). The DDR50 access mode is used only if the
*    connected SD card supports it.
*
*    The application can check if the Card Mode MMC/SD driver is actually
*    using the DDR50 access mode for the data transfer by evaluating the
*    AccessMode member of the FS_MMC_CARD_INFO structure returned
*    by FS_MMC_CM_GetCardInfo().
*
*    The DDR50 access mode can be used only if the SD/MMC host controller
*    supports it. The availability of this functionality is not checked
*    by the Card Mode MMC/SD driver.
*
*    An application is permitted to call this function only
*    at the file system initialization in FS_X_AddDevices().
*
*    FS_MMC_CM_AllowAccessModeDDR50() is available only when the file system
*    is built with FS_MMC_SUPPORT_UHS and FS_MMC_SUPPORT_SD set to 1.
*/
void FS_MMC_CM_AllowAccessModeDDR50(U8 Unit, U8 OnOff) {
  MMC_CM_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->IsAccessModeDDR50Allowed = OnOff;
  }
}

/*********************************************************************
*
*       FS_MMC_CM_AllowAccessModeSDR50
*
*  Function description
*    Allows the driver to exchange the data with an SD card using SDR50 access mode.
*
*  Parameters
*    Unit     Index of the driver instance (0-based).
*    OnOff    Specifies the permission status.
*             * 0   Use another access mode.
*             * 1   Use SDR50 access mode.
*
*  Additional information
*    This function is optional. The application can use it to request the Card Mode
*    MMC/SD driver to exchange data with an SD card on a single clock edge and at a
*    clock frequency of maximum 100 MHz. The voltage level of the I/O lines used
*    by this access mode is 1.8 V. The support for the 1.8 V voltage level can be enabled
*    vis FS_MMC_CM_AllowVoltageLevel1V8(). The SDR50 access mode is used only if the
*    connected SD card supports it.
*
*    The application can check if the Card Mode MMC/SD driver is actually
*    using the SDR50 access mode for the data transfer by evaluating the
*    AccessMode member of the FS_MMC_CARD_INFO structure returned
*    by FS_MMC_CM_GetCardInfo().
*
*    The SDR50 access mode can be used only if the SD/MMC host controller
*    supports it. The availability of this functionality is not checked
*    by the Card Mode MMC/SD driver.
*
*    An application is permitted to call this function only
*    at the file system initialization in FS_X_AddDevices().
*
*    FS_MMC_CM_AllowAccessModeSDR50() is available only when the file system
*    is built with FS_MMC_SUPPORT_UHS and FS_MMC_SUPPORT_SD set to 1.
*/
void FS_MMC_CM_AllowAccessModeSDR50(U8 Unit, U8 OnOff) {
  MMC_CM_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->IsAccessModeSDR50Allowed = OnOff;
  }
}

/*********************************************************************
*
*       FS_MMC_CM_AllowAccessModeSDR104
*
*  Function description
*    Allows the driver to exchange the data with an SD card using SDR104 access mode.
*
*  Parameters
*    Unit     Index of the driver instance (0-based).
*    OnOff    Specifies the permission status.
*             * 0   Use another access mode.
*             * 1   Use SDR104 access mode.
*
*  Additional information
*    This function is optional. The application can use it to request the Card Mode
*    MMC/SD driver to exchange data with an SD card on a single clock edge and at a
*    clock frequency of maximum 208 MHz. The voltage level of the I/O lines used
*    by this access mode is 1.8 V. The support for the 1.8 V voltage level can be enabled
*    vis FS_MMC_CM_AllowVoltageLevel1V8(). The SDR104 access mode is used only if the
*    connected SD card supports it.
*
*    The application can check if the Card Mode MMC/SD driver is actually
*    using the SDR104 access mode for the data transfer by evaluating the
*    AccessMode member of the FS_MMC_CARD_INFO structure returned
*    by FS_MMC_CM_GetCardInfo().
*
*    The SDR104 access mode can be used only if the SD/MMC host controller
*    supports it. The availability of this functionality is not checked
*    by the Card Mode MMC/SD driver.
*
*    An application is permitted to call this function only
*    at the file system initialization in FS_X_AddDevices().
*
*    FS_MMC_CM_AllowAccessModeSDR104() is available only when the file system
*    is built with FS_MMC_SUPPORT_UHS and FS_MMC_SUPPORT_SD set to 1.
*/
void FS_MMC_CM_AllowAccessModeSDR104(U8 Unit, U8 OnOff) {
  MMC_CM_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->IsAccessModeSDR104Allowed = OnOff;
  }
}

/*********************************************************************
*
*       FS_MMC_CM_SetSDR50Tuning
*
*  Function description
*    Enables or disables the tuning for the SDR50 access mode.
*
*  Parameters
*    Unit     Index of the driver instance (0-based).
*    OnOff    Specifies the tuning activation status.
*             * 0   Tuning is not performed.
*             * 1   Tuning is performed.
*
*  Additional information
*    This function is optional. It gives the application the ability to
*    select if a tuning procedure is performed at the initialization of
*    the SD card that is exchanging the data in SDR50 access mode.
*    The tuning procedure is required in order to determine the correct
*    sampling point of the data that is received from the SD card.
*    By default, the tuning procedure is disabled for the SDR50 access mode.
*
*    An application is permitted to call this function only
*    at the file system initialization in FS_X_AddDevices().
*
*    FS_MMC_CM_SetSDR50Tuning() is available only when the file system
*    is built with FS_MMC_SUPPORT_UHS and FS_MMC_SUPPORT_SD set to 1.
*/
void FS_MMC_CM_SetSDR50Tuning(U8 Unit, U8 OnOff) {
  MMC_CM_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->IsSDR50TuningRequested = OnOff;
  }
}

/*********************************************************************
*
*       FS_MMC_CM_SetSDR104Tuning
*
*  Function description
*    Enables or disables the tuning for the SDR104 access mode.
*
*  Parameters
*    Unit     Index of the driver instance (0-based).
*    OnOff    Specifies the tuning activation status.
*             * 0   Tuning is not performed.
*             * 1   Tuning is performed.
*
*  Additional information
*    This function is optional. It gives the application the ability to
*    select if a tuning procedure is performed at the initialization of
*    the SD card that is exchanging the data in SDR104 access mode.
*    The tuning procedure is required in order to determine the correct
*    sampling point of the data that is received from the SD card.
*    By default, the tuning procedure is enabled for the SDR104 access mode
*    as required by the SD Specification.
*
*    An application is permitted to call this function only
*    at the file system initialization in FS_X_AddDevices().
*
*    FS_MMC_CM_SetSDR104Tuning() is available only when the file system
*    is built with FS_MMC_SUPPORT_UHS and FS_MMC_SUPPORT_SD set to 1.
*/
void FS_MMC_CM_SetSDR104Tuning(U8 Unit, U8 OnOff) {
  MMC_CM_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->IsSDR104TuningRequested = OnOff;
  }
}

#endif // FS_MMC_SUPPORT_SD

#if FS_MMC_SUPPORT_MMC

/*********************************************************************
*
*       FS_MMC_CM_AllowAccessModeHS_DDR
*
*  Function description
*    Allows the driver to exchange the data with an MMC device using
*    the High Speed DDR access mode.
*
*  Parameters
*    Unit     Index of the driver instance (0-based).
*    OnOff    Specifies the permission status.
*             * 0   Use another access mode.
*             * 1   Use High Speed DRR access mode.
*
*  Additional information
*    This function is optional. The application can use it to request the Card Mode
*    MMC/SD driver to exchange data with an MMC device on both clock edges and at a
*    clock frequency of maximum 52 MHz. The voltage level of the I/O lines used
*    by this access mode can be either 3.3 V or 1.8 V. The support for the 1.8 V voltage
*    level can be enabled vis FS_MMC_CM_AllowVoltageLevel1V8(). The 3.3 V voltage
*    level is enabled by default. The High Speed DDR access mode is used only if
*    the connected MMC device supports it.
*
*    The application can check if the Card Mode MMC/SD driver is actually
*    using the high speed DDR access mode for the data transfer by evaluating the
*    AccessMode member of the FS_MMC_CARD_INFO structure returned
*    by FS_MMC_CM_GetCardInfo().
*
*    The High Speed DDR access mode can be used only if the SD/MMC host controller
*    supports it. The availability of this functionality is not checked
*    by the Card Mode MMC/SD driver.
*
*    An application is permitted to call this function only
*    at the file system initialization in FS_X_AddDevices().
*
*    FS_MMC_CM_AllowAccessModeHS_DDR() is available only when the file system
*    is built with FS_MMC_SUPPORT_UHS and FS_MMC_SUPPORT_MMC set to 1.
*/
void FS_MMC_CM_AllowAccessModeHS_DDR(U8 Unit, U8 OnOff) {
  MMC_CM_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->IsAccessModeHS_DDRAllowed = OnOff;
  }
}

/*********************************************************************
*
*       FS_MMC_CM_AllowAccessModeHS200
*
*  Function description
*    Allows the driver to exchange the data with an MMC device using the HS200 access mode.
*
*  Parameters
*    Unit     Index of the driver instance (0-based).
*    OnOff    Specifies the permission status.
*             * 0   Use another access mode.
*             * 1   Use HS200 access mode.
*
*  Additional information
*    This function is optional. The application can use it to request the Card Mode
*    MMC/SD driver to exchange data with an MMC device on a single clock edge and at a
*    clock frequency of maximum 200 MHz. The voltage level of the I/O lines used
*    by this access mode can be either 1.8 V. The support for the 1.8 V voltage
*    level can be enabled via FS_MMC_CM_AllowVoltageLevel1V8(). The HS200 access
*    mode is used only if the connected MMC device supports it. In this access mode
*    the data is transferred either via 4 or 8 lines. The data transfer via 4 and 8
*    data lines can be enabled via FS_MMC_CM_Allow4bitMode() and FS_MMC_CM_Allow8bitMode()
*    respectively.
*
*    The application can check if the Card Mode MMC/SD driver is actually
*    using the HS200 access mode for the data transfer by evaluating the
*    AccessMode member of the FS_MMC_CARD_INFO structure returned
*    by FS_MMC_CM_GetCardInfo().
*
*    The HS200 access mode can be used only if the SD/MMC host controller
*    supports it. The availability of this functionality is not checked
*    by the Card Mode MMC/SD driver.
*
*    An application is permitted to call this function only
*    at the file system initialization in FS_X_AddDevices().
*
*    FS_MMC_CM_AllowAccessModeHS200() is available only when the file system
*    is built with FS_MMC_SUPPORT_UHS and FS_MMC_SUPPORT_MMC set to 1.
*/
void FS_MMC_CM_AllowAccessModeHS200(U8 Unit, U8 OnOff) {
  MMC_CM_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->IsAccessModeHS200Allowed = OnOff;
  }
}

/*********************************************************************
*
*       FS_MMC_CM_AllowAccessModeHS400
*
*  Function description
*    Allows the driver to exchange the data with an eMMC device using the HS400 access mode.
*
*  Parameters
*    Unit     Index of the driver instance (0-based).
*    OnOff    Specifies the permission status.
*             * 0   Use another access mode.
*             * 1   Use HS400 access mode.
*
*  Additional information
*    This function is optional. The application can use it to request the Card Mode
*    MMC/SD driver to exchange data with an MMC device on both clock edges and at a
*    clock frequency of maximum 200 MHz. The voltage level of the I/O lines used
*    by this access mode can be either 1.8 V. The support for the 1.8 V voltage
*    level can be enabled via FS_MMC_CM_AllowVoltageLevel1V8(). The HS400 access
*    mode is used only if the connected MMC device supports it. In this access mode
*    the data is always transferred via 8 lines. The data transfer via 8 data lines
*    can be enabled via FS_MMC_CM_Allow8bitMode().
*
*    The application can check if the Card Mode MMC/SD driver is actually
*    using the HS400 access mode for the data transfer by evaluating the
*    AccessMode member of the FS_MMC_CARD_INFO structure returned
*    by FS_MMC_CM_GetCardInfo().
*
*    The HS400 access mode can be used only if the SD/MMC host controller
*    supports it. The availability of this functionality is not checked
*    by the Card Mode MMC/SD driver.
*
*    An application is permitted to call this function only
*    at the file system initialization in FS_X_AddDevices().
*
*    FS_MMC_CM_AllowAccessModeHS400() is available only when the file system
*    is built with FS_MMC_SUPPORT_UHS and FS_MMC_SUPPORT_MMC set to 1.
*/
void FS_MMC_CM_AllowAccessModeHS400(U8 Unit, U8 OnOff) {
  MMC_CM_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->IsAccessModeHS400Allowed = OnOff;
  }
}

/*********************************************************************
*
*       FS_MMC_CM_SetHS200Tuning
*
*  Function description
*    Enables or disables the tuning for the HS200 access mode.
*
*  Parameters
*    Unit     Index of the driver instance (0-based).
*    OnOff    Specifies the tuning activation status.
*             * 0   Tuning is not performed.
*             * 1   Tuning is performed.
*
*  Additional information
*    This function is optional. It gives the application the ability to
*    select if a tuning procedure is performed at the initialization of
*    an MMC device that is exchanging the data in HS200 access mode.
*    The tuning procedure is required in order to determine the correct
*    sampling point of the data that is received from the SD card.
*    By default, the tuning procedure is enabled for the HS200 access mode.
*
*    An application is permitted to call this function only
*    at the file system initialization in FS_X_AddDevices().
*
*    FS_MMC_CM_SetHS200Tuning() is available only when the file system
*    is built with FS_MMC_SUPPORT_UHS and FS_MMC_SUPPORT_MMC set to 1.
*/
void FS_MMC_CM_SetHS200Tuning(U8 Unit, U8 OnOff) {
  MMC_CM_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->IsHS200TuningRequested = OnOff;
  }
}

#endif // FS_MMC_SUPPORT_MMC

/*********************************************************************
*
*       FS_MMC_CM_AllowVoltageLevel1V8
*
*  Function description
*    Allows the driver to user 1.8 V signaling on the I/O lines.
*
*  Parameters
*    Unit     Index of the driver instance (0-based).
*    OnOff    Specifies the permission status.
*             * 0   1.8 V signaling is not allowed.
*             * 1   1.8 V signaling is allowed.
*
*  Additional information
*    This function is optional. It gives the application the ability
*    to configure if the Card Mode MMC/SD driver can exchange the data
*    with an SD card or MMC device using 1.8 V voltage level on the
*    I/O lines. This voltage level is required for the ultra high speed
*    access modes.
*
*    An application is permitted to call this function only
*    at the file system initialization in FS_X_AddDevices().
*
*    FS_MMC_CM_AllowVoltageLevel1V8() is available only when the
*    file system is built with FS_MMC_SUPPORT_UHS set to 1.
*/
void FS_MMC_CM_AllowVoltageLevel1V8(U8 Unit, U8 OnOff) {
  MMC_CM_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->IsVoltageLevel1V8Allowed = OnOff;
  }
}

/*********************************************************************
*
*       FS_MMC_CM_SetDriverStrength
*
*  Function description
*    Configures the output driving strength of the MMC/SD device.
*
*  Parameters
*    Unit             Index of the driver instance (0-based).
*    DriverStrength   Specifies the output driving strength.
*
*  Additional information
*    This function is optional. It gives the application the ability
*    to configure if the output driving strength  of the MMC/SD device.
*    Refer to \ref{Driver strength types} for permitted values for DriverStrength.
*    The specified driver strength is used only if the MMC/SD device
*    actually supports it. If the MMC/SD device does not support the
*    specified driver strength then the default driver strength is used.
*
*    The actual driver strength can be queried via FS_MMC_CM_GetCardInfo().
*    The value is stored in the FS_MMC_CARD_INFO::DriverStrength member
*    of the returned data structure.
*
*    An application is permitted to call this function only
*    at the file system initialization in FS_X_AddDevices().
*
*    FS_MMC_CM_SetDriverStrength() is available only when the
*    file system is built with FS_MMC_SUPPORT_UHS set to 1.
*/
void FS_MMC_CM_SetDriverStrength(U8 Unit, unsigned DriverStrength) {
  MMC_CM_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->DriverStrengthRequested = (U8)DriverStrength;
  }
}

#endif // FS_MMC_SUPPORT_UHS

#if FS_MMC_SUPPORT_MMC

/*********************************************************************
*
*       FS_MMC_CM_AllowCacheActivation
*
*  Function description
*    Allows the driver to activate the data cache of an eMMC device.
*
*  Parameters
*    Unit     Index of the driver instance (0-based).
*    OnOff    Specifies the permission status.
*             * 0   Cache activation is not allowed.
*             * 1   Cache activation is allowed.
*
*  Additional information
*    This function is optional. It can be used to configure if
*    the Card Mode MMC/SD driver is allowed to enable the data cache
*    of an eMMC device. The data cache is activated only if supported
*    by the eMMC device. By default, the Card Mode MMC/SD driver
*    activates the data cache for improved read and write performance.
*    With the data cache enabled, the fail-safety of the file system
*    can no longer be guaranteed.
*
*    An application is permitted to call this function only
*    at the file system initialization in FS_X_AddDevices().
*
*    FS_MMC_CM_AllowCacheActivation() is available only when the
*    file system is built with FS_MMC_SUPPORT_MMC set to 1.
*/
void FS_MMC_CM_AllowCacheActivation(U8 Unit, U8 OnOff) {
  MMC_CM_INST * pInst;

  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    pInst->IsCacheActivationAllowed = OnOff;
  }
}

#endif // FS_MMC_SUPPORT_MMC

/*************************** End of file ****************************/
