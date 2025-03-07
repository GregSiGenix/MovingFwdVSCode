/*********************************************************************
*                   (c) SEGGER Microcontroller GmbH                  *
*                        The Embedded Experts                        *
*                           www.segger.com                           *
**********************************************************************

----------------------------------------------------------------------
Licensing information

Licensor:                 SEGGER Microcontroller Systems LLC
Licensed to:              React Health Inc., 203 Avenue A NW, Suite 300, Winter Haven FL 33881, USA
Licensed SEGGER software: emFile
License number:           FS-00855
License model:            SSL [Single Developer Single Platform Source Code License]
Licensed product:         -
Licensed platform:        STM32F4, IAR
Licensed number of seats: 1
-------------------------- END-OF-HEADER -----------------------------

File    : FS_MMC_HW_CM_STM32H735_Morpheus.c
Purpose : Hardware layer for MMC/SD driver in CardMode, for the React Health Morpheus board.
Literature:
  [1] SD Specifications Part 1 Physical Layer Simplified Specification Version 2.00
    (\\fileserver\Techinfo\Company\SDCard_org\SDCardPhysicalLayerSpec_V200.pdf)
  [2] RM0468 Reference manual STM32H723/733, STM32H725/735 and STM32H730 Value line advanced Arm-based 32-bit MCUs
    (\\fileserver\Techinfo\Company\ST\MCU\STM32\STM32H7\rm0468-stm32h723733-stm32h725735-and-stm32h730-value-line-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
  [3] Datasheet STM32H735xG
    (\\fileserver\Techinfo\Company\ST\MCU\STM32\STM32H7\DS13312_STM32H735xx_rev2.pdf)
  [4] UM2679 User manual Discovery kit with STM32H735IG MCU
    (\\fileserver\Techinfo\Company\ST\MCU\STM32\STM32H7\EvalBoard\STM32H735G_DISCO_UM.pdf)
  [5] Schematics STM32H735IG-DK
    (\\fileserver\Techinfo\Company\ST\MCU\STM32\STM32H7\EvalBoard\STM32H735G_DISCO_Schematics.pdf)
  [6] STM32H723xx STM32H730xx STM32H733xx STM32H725xx STM32H735xx Errata sheet
    (\\fileserver\Techinfo\Company\ST\MCU\STM32\STM32H7\ES0491_rev7_STM32H72xx_73xx.pdf)
*/

/*********************************************************************
*
*       #include section
*
**********************************************************************
*/
#include "FS.h"
#include "stm32h7xx_hal.h"
#include "main.h" //for SDIO1_CD pin defines

/*********************************************************************
*
*       Defines, configurable
*
**********************************************************************
*/
#ifndef   FS_MMC_HW_CM_SDMMC_CLK
  #define FS_MMC_HW_CM_SDMMC_CLK            200000        // Clock frequency of SDMMC module (in kHz)
#endif

#ifndef   FS_MMC_HW_CM_MAX_SD_CLK                         // KDS-TODO increase this to 50000 for new board
  #define FS_MMC_HW_CM_MAX_SD_CLK           50000         // Maximum clock frequency supplied to SD card (in kHz)
#endif

#ifndef   FS_MMC_HW_CM_POWER_GOOD_DELAY
  #define FS_MMC_HW_CM_POWER_GOOD_DELAY     50            // Number of  milliseconds to wait for the power supply of SD card to become ready
#endif

#ifndef   FS_MMC_HW_CM_CPU_CLK
  #define FS_MMC_HW_CM_CPU_CLK              400000000     // CPU clock frequency
#endif

#ifndef   FS_MMC_HW_CM_AHB_CLK
  #define FS_MMC_HW_CM_AHB_CLK              (FS_MMC_HW_CM_CPU_CLK / 2)   // Frequency of the AHB clock supplied to SDMMC peripheral (in Hz)
#endif

#ifndef   FS_MMC_HW_CM_CYCLES_PER_1MS
  #define FS_MMC_HW_CM_CYCLES_PER_1MS       400000        // Number of software cycles required to generate a 1ms delay
#endif

#ifndef   FS_MMC_HW_CM_USE_OS
  #define FS_MMC_HW_CM_USE_OS               1             // Enables or disables the event-driven operation.
                                                          // Permitted values:
                                                          //   0 - polling via CPU
                                                          //   1 - event-driven using embOS
                                                          //   2 - event-driven using other RTOS
#endif

/*********************************************************************
*
*       #include section, conditional
*
**********************************************************************
*/
#if FS_MMC_HW_CM_USE_OS
  #include "stm32h7xx.h"
  #include "FS_OS.h"
#if (FS_MMC_HW_CM_USE_OS == 1)
  #include "RTOS.h"
#endif // FS_MMC_HW_CM_USE_OS == 1
#endif // FS_MMC_HW_CM_USE_OS

/*********************************************************************
*
*       Defines, non-configurable
*
**********************************************************************
*/

/*********************************************************************
*
*       SDMMC interface registers
*/
#define SDMMC_BASE_ADDR              0x52007000uL
#define SDMMC_POWER                  (*(volatile U32 *)(SDMMC_BASE_ADDR + 0x00))
#define SDMMC_CLKCR                  (*(volatile U32 *)(SDMMC_BASE_ADDR + 0x04))
#define SDMMC_ARGR                   (*(volatile U32 *)(SDMMC_BASE_ADDR + 0x08))
#define SDMMC_CMDR                   (*(volatile U32 *)(SDMMC_BASE_ADDR + 0x0C))
#define SDMMC_RESPCMDR               (*(volatile U32 *)(SDMMC_BASE_ADDR + 0x10))
#define SDMMC_RESP1R                 (*(volatile U32 *)(SDMMC_BASE_ADDR + 0x14))
#define SDMMC_RESP2R                 (*(volatile U32 *)(SDMMC_BASE_ADDR + 0x18))
#define SDMMC_RESP3R                 (*(volatile U32 *)(SDMMC_BASE_ADDR + 0x1C))
#define SDMMC_RESP4R                 (*(volatile U32 *)(SDMMC_BASE_ADDR + 0x20))
#define SDMMC_DTIMER                 (*(volatile U32 *)(SDMMC_BASE_ADDR + 0x24))
#define SDMMC_DLENR                  (*(volatile U32 *)(SDMMC_BASE_ADDR + 0x28))
#define SDMMC_DCTRLR                 (*(volatile U32 *)(SDMMC_BASE_ADDR + 0x2C))
#define SDMMC_DCNTR                  (*(volatile U32 *)(SDMMC_BASE_ADDR + 0x30))
#define SDMMC_STAR                   (*(volatile U32 *)(SDMMC_BASE_ADDR + 0x34))
#define SDMMC_ICR                    (*(volatile U32 *)(SDMMC_BASE_ADDR + 0x38))
#define SDMMC_MASKR                  (*(volatile U32 *)(SDMMC_BASE_ADDR + 0x3C))
#define SDMMC_ACKTIMER               (*(volatile U32 *)(SDMMC_BASE_ADDR + 0x40))
#define SDMMC_IDMACTRLR              (*(volatile U32 *)(SDMMC_BASE_ADDR + 0x50))
#define SDMMC_IDMABSIZER             (*(volatile U32 *)(SDMMC_BASE_ADDR + 0x54))
#define SDMMC_IDMABASE0R             (*(volatile U32 *)(SDMMC_BASE_ADDR + 0x58))
#define SDMMC_IDMABASE1R             (*(volatile U32 *)(SDMMC_BASE_ADDR + 0x5C))
#define SDMMC_FIFOR                  (*(volatile U32 *)(SDMMC_BASE_ADDR + 0x80))

/*********************************************************************
*
*       Reset and clock control registers
*/
#define RCC_BASE_ADDR                 0x58024400uL
#define RCC_D1CCIPR                   (*(volatile U32 *)(RCC_BASE_ADDR + 0x04C))
#define RCC_AHB3RSTR                  (*(volatile U32 *)(RCC_BASE_ADDR + 0x07C))

/*********************************************************************
*
*       Reset and clock bits for the peripherals used by the driver
*/
#define AHB4ENR_GPIOCEN_BIT           2
#define AHB4ENR_GPIODEN_BIT           3
#define AHB4ENR_GPIOFEN_BIT           5
#define AHB3ENR_SDMMC1EN_BIT          16
#define AHB3RSTR_SDMMC1RST_BIT        16

/*********************************************************************
*
*       SDMMC status register
*/
#define STAR_CCRCFAIL_BIT             0
#define STAR_DCRCFAIL_BIT             1
#define STAR_CTIMEOUT_BIT             2
#define STAR_DTIMEOUT_BIT             3
#define STAR_TXUNDERR_BIT             4
#define STAR_RXOVERR_BIT              5
#define STAR_CMDREND_BIT              6
#define STAR_CMDSENT_BIT              7
#define STAR_DATAEND_BIT              8
#define STAR_DPSMACT_BIT              12
#define STAR_CPSMACT_BIT              13
#define STAR_TXFIFOHE_BIT             14
#define STAR_RXFIFOHF_BIT             15
#define STAR_TXFIFOF_BIT              16
#define STAR_RXFIFOF_BIT              17
#define STAR_TXFIFOE_BIT              18
#define STAR_RXFIFOE_BIT              19
#define STAR_BUSYD0_BIT               20
#define STAR_BUSY0END_BIT             21

/*********************************************************************
*
*       SDMMC data control register
*/
#define DCTRLR_DTEN_BIT               0
#define DCTRLR_DTDIR_BIT              1
#define DCTRLR_DBLOCKSIZE_BIT         4uL

/*********************************************************************
*
*       SDMMC clock control register
*/
#define CLKCR_CLKDIV_BIT              0
#define CLKCR_CLKDIV_MASK             0x3FFuL
#define CLKCR_CLKDIV_MAX              CLKCR_CLKDIV_MASK
#define CLKCR_PWRSAV_BIT              12
#define CLKCR_WIDBUS_MASK             3uL
#define CLKCR_WIDBUS_4BIT             1uL
#define CLKCR_WIDBUS_8BIT             2uL
#define CLKCR_WIDBUS_BIT              14
#define CLKCR_HWFC_EN_BIT             17

/*********************************************************************
*
*       SDMMC command register
*/
#define CMDR_CMDINDEX_MASK            0x3FuL
#define CMDR_CMDINDEX_BIT             0
#define CMDR_CMDTRANS_BIT             6
#define CMDR_WAITRESP_SHORT           1uL
#define CMDR_WAITRESP_SHORT_NO_CRC    2uL
#define CMDR_WAITRESP_LONG            3uL
#define CMDR_WAITRESP_BIT             8uL
#define CMDR_CPSMEN_BIT               12

/*********************************************************************
*
*       SDMMC interrupt control register
*/
#define ICR_CCRCFAILC_BIT             0
#define ICR_DCRCFAILC_BIT             1
#define ICR_CTIMEOUTC_BIT             2
#define ICR_DTIMEOUTC_BIT             3
#define ICR_TXUNDERRC_BIT             4
#define ICR_RXOVERRC_BIT              5
#define ICR_CMDRENDC_BIT              6
#define ICR_CMDSENTC_BIT              7
#define ICR_DATAENDC_BIT              8
#define ICR_DBCKENDC_BIT              10
#define ICR_ABORTC_BIT                11
#define ICR_BUSY0ENDC_BIT             21
#define ICR_MASK_STATIC               ((1uL << ICR_CCRCFAILC_BIT) | \
                                       (1uL << ICR_DCRCFAILC_BIT) | \
                                       (1uL << ICR_CTIMEOUTC_BIT) | \
                                       (1uL << ICR_DTIMEOUTC_BIT) | \
                                       (1uL << ICR_TXUNDERRC_BIT) | \
                                       (1uL << ICR_RXOVERRC_BIT)  | \
                                       (1uL << ICR_CMDRENDC_BIT)  | \
                                       (1uL << ICR_CMDSENTC_BIT)  | \
                                       (1uL << ICR_DATAENDC_BIT)  | \
                                       (1uL << ICR_DBCKENDC_BIT)  | \
                                       (1uL << ICR_ABORTC_BIT)    | \
                                       (1uL << ICR_BUSY0ENDC_BIT))

/*********************************************************************
*
*       SDMMC interrupt mask register
*/
#define MASK_CCRCFAILIE_BIT           0
#define MASK_DCRCFAILIE_BIT           1
#define MASK_CTIMEOUTIE_BIT           2
#define MASK_DTIMEOUTIE_BIT           3
#define MASK_TXUNDERRIE_BIT           4
#define MASK_RXOVERRIE_BIT            5
#define MASK_CMDRENDIE_BIT            6
#define MASK_CMDSENTIE_BIT            7
#define MASK_DATAENDIE_BIT            8
#define MASK_DBCKENDIE_BIT            10
#define MASK_TXFIFOHEIE_BIT           14
#define MASK_RXFIFOHFIE_BIT           15
#define MASK_TXFIFOFIE_BIT            16
#define MASK_RXFIFOFIE_BIT            17
#define MASK_TXFIFOEIE_BIT            18
#define MASK_RXFIFOEIE_BIT            19
#define MASK_BUSY0IE_BIT              20
#define MASK_BUSY0ENDIE_BIT           21
#define MASK_ALL                      ((1uL << MASK_CCRCFAILIE_BIT)  | \
                                       (1uL << MASK_DCRCFAILIE_BIT)  | \
                                       (1uL << MASK_CTIMEOUTIE_BIT)  | \
                                       (1uL << MASK_DTIMEOUTIE_BIT)  | \
                                       (1uL << MASK_TXUNDERRIE_BIT)  | \
                                       (1uL << MASK_RXOVERRIE_BIT)   | \
                                       (1uL << MASK_CMDRENDIE_BIT)   | \
                                       (1uL << MASK_CMDSENTIE_BIT)   | \
                                       (1uL << MASK_DATAENDIE_BIT)   | \
                                       (1uL << MASK_DBCKENDIE_BIT)   | \
                                       (1uL << MASK_RXFIFOHFIE_BIT)  | \
                                       (1uL << MASK_TXFIFOFIE_BIT)   | \
                                       (1uL << MASK_RXFIFOFIE_BIT)   | \
                                       (1uL << MASK_RXFIFOEIE_BIT)   | \
                                       (1uL << MASK_BUSY0ENDIE_BIT))

/*********************************************************************
*
*       SDMMC data length register
*/
#define DLENR_DATALENGTH_MASK         0x01FFFFFFuL

/*********************************************************************
*
*       Misc. defines
*/
#define POWER_ON                      3uL
#define MAX_NUM_BLOCKS                65535
#define FIFO_SIZE                     (16 * 4)
#define WAIT_TIMEOUT                  1000      // Maximum number of milliseconds to wait for an operation to finish.
#define WAIT_TIMEOUT_CYCLES           (WAIT_TIMEOUT * FS_MMC_HW_CM_CYCLES_PER_1MS)
#define SDMMC_PRIO                    15
#define D1CCIPR_SDMMCSEL_BIT          16

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static U16              _BlockSize;
static U16              _NumBlocks;
static void           * _pBuffer;
static U32              _NumLoopsWriteRegDelay;
static U8               _RepeatSame;
static U8               _IsR1Busy;
#if FS_MMC_HW_CM_USE_OS
  static volatile U32   _StatusSDMMC;
  static U8             _IsIntEnabled;
#endif // FS_MMC_HW_CM_USE_OS

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _ld
*/
static U16 _ld(U32 Value) {
  U16 i;

  for (i = 0; i < 16; i++) {
    if ((1uL << i) == Value) {
      break;
    }
  }
  return i;
}

/*********************************************************************
*
*       _Delay1ms
*
*  Function description
*    Blocks the program execution for about 1ms.
*
*  Additional information
*    The number of loops has to be adjusted according to the CPU speed.
*/
static void _Delay1ms(void) {
#if FS_MMC_HW_CM_USE_OS
  FS_X_OS_Delay(1);
#else
  volatile unsigned NumLoops;

  NumLoops = FS_MMC_HW_CM_CYCLES_PER_1MS;
  do {
    ;
  } while(--NumLoops);
#endif // FS_MMC_HW_CM_USE_OS
}

/*********************************************************************
*
*       _GetStatus
*/
static U32 _GetStatus(void) {
  U32 Status;

#if FS_MMC_HW_CM_USE_OS
  if (_IsIntEnabled) {
    Status = _StatusSDMMC;
  } else
#endif // FS_MMC_HW_CM_USE_OS
  {
    Status = SDMMC_STAR;
  }
  return Status;
}

/*********************************************************************
*
*       _WaitForEventIfRequired
*/
static int _WaitForEventIfRequired(void) {
  int r;

  r = 0;
#if FS_MMC_HW_CM_USE_OS
  if (_IsIntEnabled) {
    r = FS_X_OS_Wait(WAIT_TIMEOUT);
  }
#endif // FS_MMC_HW_CM_USE_OS
  return r;
}

#if FS_MMC_HW_CM_USE_OS

/*********************************************************************
*
*       _EnableInt
*/
static void _EnableInt(void) {
  if (_IsIntEnabled == 0) {
    NVIC_SetPriority(SDMMC1_IRQn, SDMMC_PRIO);
    NVIC_EnableIRQ(SDMMC1_IRQn);
    _IsIntEnabled = 1;
  }
}

/*********************************************************************
*
*       _DisableInt
*/
static void _DisableInt(void) {
  if (_IsIntEnabled != 0) {
    NVIC_DisableIRQ(SDMMC1_IRQn);
    _IsIntEnabled = 0;
  }
}

#endif // FS_MMC_HW_CM_USE_OS

/*********************************************************************
*
*       _WriteRegDelayed
*
*  Function description
*    Waits before writing to a register as specified in the datasheet:
*    "At least seven sdmmc_hclk clock periods are needed between two write accesses to this register."
*/
static void _WriteRegDelayed(volatile U32 * pAddr, U32 Value) {
  volatile U32 NumLoops;

  NumLoops = _NumLoopsWriteRegDelay;
  if (NumLoops) {
    do {
      ;
    } while (--NumLoops);
    *pAddr = Value;
  }
}

/*********************************************************************
*
*       _Reset
*/
static void _Reset(void) {
  U32 clkcr;
  U32 dtimer;

#if FS_MMC_HW_CM_USE_OS
  _DisableInt();
  _StatusSDMMC = 0;
#endif
  //
  // Save register values that have to be restored later.
  //
  clkcr  = SDMMC_CLKCR;
  dtimer = SDMMC_DTIMER;
  //
  // Reset SDMMC peripheral.
  //
  RCC_AHB3RSTR |=   1uL << AHB3RSTR_SDMMC1RST_BIT;
  RCC_AHB3RSTR &= ~(1uL << AHB3RSTR_SDMMC1RST_BIT);
  //
  // Initialize the SDMMC unit.
  //
  SDMMC_POWER  = 0;                           // Switch off the power to SD card.
  SDMMC_CLKCR  = 1uL << CLKCR_PWRSAV_BIT;     // Disable the clock to SD card.
  SDMMC_ARGR   = 0;
  SDMMC_CMDR   = 0;
  SDMMC_DTIMER = 0;
  SDMMC_DLENR  = 0;
  SDMMC_DCTRLR = 0;
  SDMMC_ICR    = 0xFFFFFFFF;                  // Clear all interrupts
  SDMMC_MASKR  = 0;
  _WriteRegDelayed(&SDMMC_POWER, POWER_ON);   // Enable the power to SD card. Take into account the time between the write operations.
  _WriteRegDelayed(&SDMMC_CLKCR, clkcr);
  SDMMC_DTIMER = dtimer;
#if FS_MMC_HW_CM_USE_OS
  //
  // Unmask the interrupt sources and re-enable the interrupt.
  //
  SDMMC_MASKR  = MASK_ALL;
#endif // FS_MMC_HW_CM_USE_OS
}

/*********************************************************************
*
*       _WaitForInactive
*/
static void _WaitForInactive(void) {
  U32 TimeOut;
  U32 Status;

  TimeOut = WAIT_TIMEOUT_CYCLES;
  while (1) {
    Status = _GetStatus();
    if (((Status & (1uL << STAR_CPSMACT_BIT)) == 0) &&
        ((Status & (1uL << STAR_DPSMACT_BIT)) == 0) &&
        ((Status & (1uL << STAR_BUSYD0_BIT)) == 0)) {
      break;
    }
    if (--TimeOut == 0) {
      _Reset();
      break;                            // Error, timeout expired.
    }
    if (_WaitForEventIfRequired()) {
      _Reset();
      break;                            // Error, timeout expired.
    }
  }
}

/*********************************************************************
*
*       _WaitEndOfCommand
*/
static int _WaitEndOfCommand(void) {
  int r;
  U32 Status;
  U32 TimeOut;

  //
  // Wait for the command to be sent.
  //
  TimeOut = WAIT_TIMEOUT_CYCLES;
  while (1) {
    Status = _GetStatus();
    if (Status & (1uL << STAR_CMDSENT_BIT)) {
      r = FS_MMC_CARD_NO_ERROR;
      break;
    }
    if (Status & (1uL << STAR_CCRCFAIL_BIT)) {
      r = FS_MMC_CARD_RESPONSE_CRC_ERROR;
      break;
    }
    if (Status & (1uL << STAR_CTIMEOUT_BIT)) {
      r = FS_MMC_CARD_RESPONSE_TIMEOUT;
      break;
    }
    if (--TimeOut == 0) {
      r = FS_MMC_CARD_RESPONSE_TIMEOUT;
      break;                                // Error, timeout expired.
    }
    if (_WaitForEventIfRequired()) {
      r = FS_MMC_CARD_RESPONSE_TIMEOUT;
      break;                                // Error, timeout expired.
    }
  }
  return r;
}

/*********************************************************************
*
*       _WaitEndOfResponse
*/
static int _WaitEndOfResponse(void) {
  int r;
  U32 Status;
  U32 TimeOut;

  //
  // Wait for the response to be received.
  //
  TimeOut = WAIT_TIMEOUT_CYCLES;
  while (1) {
    Status = _GetStatus();
    if (Status & (1uL << STAR_CMDREND_BIT)) {
      r = FS_MMC_CARD_NO_ERROR;
      break;
    }
    if (Status & (1uL << STAR_CCRCFAIL_BIT)) {
      r = FS_MMC_CARD_RESPONSE_CRC_ERROR;
      break;
    }
    if (Status & (1uL << STAR_CTIMEOUT_BIT)) {
      r = FS_MMC_CARD_RESPONSE_TIMEOUT;
      break;
    }
    if (--TimeOut == 0) {
      r = FS_MMC_CARD_RESPONSE_TIMEOUT;     // Error, timeout expired.
      break;
    }
    if (_WaitForEventIfRequired()) {
      r = FS_MMC_CARD_RESPONSE_TIMEOUT;
      break;                                // Error, timeout expired.
    }
  }
  if (r == FS_MMC_CARD_NO_ERROR) {
    //
    // Check and wait for the card to become ready.
    //
    if (_IsR1Busy) {
      Status = SDMMC_STAR;
      if (Status & (1uL << STAR_BUSYD0_BIT)) {
        TimeOut = WAIT_TIMEOUT_CYCLES;
        while (1) {
          Status = _GetStatus();
          if (Status & (1uL << STAR_BUSY0END_BIT)) {
            break;
          }
          if (Status & (1uL << STAR_DTIMEOUT_BIT)) {
            r = FS_MMC_CARD_RESPONSE_TIMEOUT;
            break;
          }
          if (--TimeOut == 0) {
            r = FS_MMC_CARD_RESPONSE_TIMEOUT;
            break;
          }
          if (_WaitForEventIfRequired()) {
            r = FS_MMC_CARD_RESPONSE_TIMEOUT;
            break;                                // Error, timeout expired.
          }
        }
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _WaitForRxDone
*
*  Function description
*    Waits for the read data transfer to end.
*/
static int _WaitForRxDone(void) {
  int r;
  U32 Status;
  U32 TimeOut;

  TimeOut = WAIT_TIMEOUT_CYCLES;
  while (1) {
    Status = _GetStatus();
    if (Status & (1uL << STAR_DATAEND_BIT)) {
      r = FS_MMC_CARD_NO_ERROR;
      break;
    }
    if (Status & (1uL << STAR_DCRCFAIL_BIT)) {
      r = FS_MMC_CARD_READ_CRC_ERROR;
      break;
    }
    if (Status & (1uL << STAR_DTIMEOUT_BIT)) {
      r = FS_MMC_CARD_READ_TIMEOUT;
      break;
    }
    if (Status & (1uL << STAR_RXOVERR_BIT)) {
      r = FS_MMC_CARD_READ_GENERIC_ERROR;
      break;
    }
    if (--TimeOut == 0) {
      r = FS_MMC_CARD_WRITE_GENERIC_ERROR;
      break;
    }
    if (_WaitForEventIfRequired()) {
      r = FS_MMC_CARD_WRITE_GENERIC_ERROR;
      break;                                // Error, timeout expired.
    }
  }
  return r;
}

/*********************************************************************
*
*       _WaitForRxReady
*
*  Function description
*    Waits for data to be stored in the RX FIFO.
*/
static int _WaitForRxReady(void) {
  int r;
  U32 Status;
  U32 TimeOut;

  TimeOut = WAIT_TIMEOUT_CYCLES;
  while (1) {
    Status = _GetStatus();
    if (Status & (1uL << STAR_RXFIFOF_BIT)) {
      r = FS_MMC_CARD_NO_ERROR;
      break;
    }
    if ((Status & (1uL << STAR_RXFIFOE_BIT)) == 0) {
      r = FS_MMC_CARD_NO_ERROR;
      break;
    }
    if (Status & (1uL << STAR_RXFIFOHF_BIT)) {
      r = FS_MMC_CARD_NO_ERROR;
      break;
    }
    if (Status & (1uL << STAR_DATAEND_BIT)) {
      r = FS_MMC_CARD_NO_ERROR;
      break;
    }
    if (Status & (1uL << STAR_DCRCFAIL_BIT)) {
      r = FS_MMC_CARD_READ_CRC_ERROR;
      break;
    }
    if (Status & (1uL << STAR_DTIMEOUT_BIT)) {
      r = FS_MMC_CARD_READ_TIMEOUT;
      break;
    }
    if (Status & (1uL << STAR_RXOVERR_BIT)) {
      r = FS_MMC_CARD_READ_GENERIC_ERROR;
      break;
    }
    if (--TimeOut == 0) {
      r = FS_MMC_CARD_READ_TIMEOUT;
      break;
    }
    if (_WaitForEventIfRequired()) {
      r = FS_MMC_CARD_READ_TIMEOUT;
      break;                                // Error, timeout expired.
    }
  }
  return r;
}

/*********************************************************************
*
*       _ReadData
*
*  Function description
*    Receives data from SD card using the CPU (i.e. without DMA).
*/
static int _ReadData(void) {
  int   r;
  U32   NumBytes;
  U32   Status;
  U8  * pData8;
  U32   Data32;
  U32 * pData32;

  NumBytes = _BlockSize * _NumBlocks;
  pData8   = (U8 *)_pBuffer;
  while (1) {
    r = _WaitForRxReady();
    if (r) {
      break;
    }
    //
    // Try to read the entire FIFO at once for better performance.
    //
    Status = SDMMC_STAR;
    if (   Status & ((1uL << STAR_RXFIFOF_BIT))
        && (NumBytes >= FIFO_SIZE)
        && (((U32)pData8 & 3) == 0)) {
       pData32 = (U32 *)(void *)pData8;
       *pData32++ = SDMMC_FIFOR;
       *pData32++ = SDMMC_FIFOR;
       *pData32++ = SDMMC_FIFOR;
       *pData32++ = SDMMC_FIFOR;
       *pData32++ = SDMMC_FIFOR;
       *pData32++ = SDMMC_FIFOR;
       *pData32++ = SDMMC_FIFOR;
       *pData32++ = SDMMC_FIFOR;
       *pData32++ = SDMMC_FIFOR;
       *pData32++ = SDMMC_FIFOR;
       *pData32++ = SDMMC_FIFOR;
       *pData32++ = SDMMC_FIFOR;
       *pData32++ = SDMMC_FIFOR;
       *pData32++ = SDMMC_FIFOR;
       *pData32++ = SDMMC_FIFOR;
       *pData32++ = SDMMC_FIFOR;
       NumBytes -= FIFO_SIZE;
       pData8   += FIFO_SIZE;
    } else {
      //
      // Read from FIFO until empty.
      //
      while (1) {
        Status = SDMMC_STAR;
        if (Status & (1uL << STAR_RXFIFOE_BIT)) {
          break;
        }
        Data32 = SDMMC_FIFOR;
        if (NumBytes == 3) {
          *pData8++ = (U8)Data32;
          *pData8++ = (U8)(Data32 >> 8);
          *pData8++ = (U8)(Data32 >> 16);
          NumBytes -= 3;
        } else {
          if (NumBytes == 2) {
            *pData8++ = (U8)Data32;
            *pData8++ = (U8)(Data32 >> 8);
            NumBytes -= 2;
          } else {
            if (NumBytes == 1) {
              *pData8++ = (U8)Data32;
              --NumBytes;
            } else {
              *pData8++ = (U8)Data32;
              *pData8++ = (U8)(Data32 >> 8);
              *pData8++ = (U8)(Data32 >> 16);
              *pData8++ = (U8)(Data32 >> 24);
              NumBytes -= 4;
            }
          }
        }
        if (NumBytes == 0) {
          break;
        }
      }
    }
    if (NumBytes == 0) {
      break;
    }
  }
  if (r == 0) {
    r = _WaitForRxDone();
  }
  return r;
}

/*********************************************************************
*
*       _WaitForTxReady
*
*  Function description
*    Waits for free space in the TX FIFO.
*/
static int _WaitForTxReady(void) {
  int r;
  U32 Status;
  U32 TimeOut;

  TimeOut = WAIT_TIMEOUT_CYCLES;
  while (1) {
    Status = _GetStatus();
    if (Status & (1uL << STAR_TXFIFOHE_BIT)) {
      r = FS_MMC_CARD_NO_ERROR;
      break;
    }
    if (Status & (1uL << STAR_TXFIFOE_BIT)) {
      r = FS_MMC_CARD_NO_ERROR;
      break;
    }
    if ((Status & (1uL << STAR_TXFIFOF_BIT)) == 0) {
      r = FS_MMC_CARD_NO_ERROR;
      break;
    }
    if (Status & (1uL << STAR_DCRCFAIL_BIT)) {
      r = FS_MMC_CARD_WRITE_CRC_ERROR;
      break;
    }
    if (Status & (1uL << STAR_DTIMEOUT_BIT)) {
      r = FS_MMC_CARD_WRITE_GENERIC_ERROR;
      break;
    }
    if (Status & (1uL << STAR_TXUNDERR_BIT)) {
      r = FS_MMC_CARD_WRITE_GENERIC_ERROR;
      break;
    }
    if (--TimeOut == 0) {
      r = FS_MMC_CARD_WRITE_GENERIC_ERROR;
      break;
    }
    if (_WaitForEventIfRequired()) {
      r = FS_MMC_CARD_WRITE_GENERIC_ERROR;
      break;                                // Error, timeout expired.
    }
  }
  return r;
}

/*********************************************************************
*
*       _WaitForTxDone
*
*  Function description
*    Waits for the write data transfer to end.
*/
static int _WaitForTxDone(void) {
  int r;
  U32 Status;
  U32 TimeOut;

  TimeOut = WAIT_TIMEOUT_CYCLES;
  while (1) {
    Status = _GetStatus();
    if (Status & (1uL << STAR_DATAEND_BIT)) {
      r = FS_MMC_CARD_NO_ERROR;
      break;
    }
    if (Status & (1uL << STAR_DCRCFAIL_BIT)) {
      r = FS_MMC_CARD_WRITE_CRC_ERROR;
      break;
    }
    if (Status & (1uL << STAR_DTIMEOUT_BIT)) {
      r = FS_MMC_CARD_WRITE_GENERIC_ERROR;
      break;
    }
    if (Status & (1uL << STAR_TXUNDERR_BIT)) {
      r = FS_MMC_CARD_WRITE_GENERIC_ERROR;
      break;
    }
    if (--TimeOut == 0) {
      r = FS_MMC_CARD_WRITE_GENERIC_ERROR;
      break;
    }
    if (_WaitForEventIfRequired()) {
      r = FS_MMC_CARD_WRITE_GENERIC_ERROR;
      break;                                // Error, timeout expired.
    }
  }
  return r;
}

/*********************************************************************
*
*       _WriteData
*
*  Function description
*    Sends data to SD card using the CPU (i.e. without DMA).
*/
static int _WriteData(void) {
  int   r;
  U32   NumBytes;
  U32   Status;
  U8  * pData8;
  U32   Data32;
  U32   NumBlocks;
  U32 * pData32;

  NumBlocks = _NumBlocks;
  pData8    = (U8 *)_pBuffer;
  while (1) {
    //
    // Send one block at a time.
    //
    NumBytes = _BlockSize;
    while (1) {
      r = _WaitForTxReady();
      if (r) {
        break;
      }
      //
      // Try to fill up the FIFO at once for better performance.
      //
      Status = SDMMC_STAR;
      if (   (Status & (1uL << STAR_TXFIFOE_BIT))
          && (NumBytes >= FIFO_SIZE)
          && (((U32)pData8 & 3) == 0)) {
        pData32 = (U32 *)(void *)pData8;
        SDMMC_FIFOR = *pData32++;
        SDMMC_FIFOR = *pData32++;
        SDMMC_FIFOR = *pData32++;
        SDMMC_FIFOR = *pData32++;
        SDMMC_FIFOR = *pData32++;
        SDMMC_FIFOR = *pData32++;
        SDMMC_FIFOR = *pData32++;
        SDMMC_FIFOR = *pData32++;
        SDMMC_FIFOR = *pData32++;
        SDMMC_FIFOR = *pData32++;
        SDMMC_FIFOR = *pData32++;
        SDMMC_FIFOR = *pData32++;
        SDMMC_FIFOR = *pData32++;
        SDMMC_FIFOR = *pData32++;
        SDMMC_FIFOR = *pData32++;
        SDMMC_FIFOR = *pData32++;
        NumBytes -= FIFO_SIZE;
        pData8   += FIFO_SIZE;
      } else {
        //
        // Write to FIFO word by word until we will it up.
        //
        while (1) {
          Status = SDMMC_STAR;
          if (Status & (1uL << STAR_TXFIFOF_BIT)) {
            break;
          }
          if (NumBytes == 3) {
            Data32  = (U32)*pData8++;
            Data32 |= (U32)*pData8++ << 8;
            Data32 |= (U32)*pData8++ << 16;
            NumBytes -= 3;
          } else {
            if (NumBytes == 2) {
              Data32  = (U32)*pData8++;
              Data32 |= (U32)*pData8++ << 8;
              NumBytes -= 2;
            } else {
              if (NumBytes == 1) {
                Data32 = (U32)*pData8++;
                --NumBytes;
              } else {
                Data32  = (U32)*pData8++;
                Data32 |= (U32)*pData8++ << 8;
                Data32 |= (U32)*pData8++ << 16;
                Data32 |= (U32)*pData8++ << 24;
                NumBytes -= 4;
              }
            }
          }
          SDMMC_FIFOR = Data32;
          if (NumBytes == 0) {
            break;
          }
        }
      }
      if (NumBytes == 0) {
        break;
      }
    }
    if (_RepeatSame) {
      pData8 = (U8 *)_pBuffer;
    }
    if (--NumBlocks == 0) {
      break;
    }
  }
  if (r == 0) {
    r = _WaitForTxDone();
  }
  return r;
}

#if FS_MMC_HW_CM_USE_OS

/**********************************************************
*
*       SDMMC1_IRQHandler
*
*  Function description
*    Handles the SDMMC interrupt.
*/
#ifdef __cplusplus
extern "C" {                                  // Prevent function name mangling with C++ compilers.
#endif
void SDMMC1_IRQHandler(void);
#ifdef __cplusplus
}
#endif
void SDMMC1_IRQHandler(void) {
  U32 Status;

#if (FS_MMC_HW_CM_USE_OS == 1)
  OS_EnterNestableInterrupt();                // Inform embOS that interrupt code is running.
#endif // FS_MMC_HW_CM_USE_OS == 1
  Status        = SDMMC_STAR;
  //
  // Save the status to a static variable and check it in the task.
  //
  SDMMC_ICR     = Status & ICR_MASK_STATIC;   // Clear the static flags to suppress further interrupts.
  _StatusSDMMC &= ICR_MASK_STATIC;            // Clear the dynamic flags
  _StatusSDMMC |= Status;
  FS_X_OS_Signal();                           // Wake up the task.
#if (FS_MMC_HW_CM_USE_OS == 1)
  OS_LeaveNestableInterrupt();                // Inform embOS that interrupt code is left.
#endif // FS_MMC_HW_CM_USE_OS == 1
}

#endif // FS_MMC_HW_CM_USE_OS

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       _HW_Init
*
*  Function description
*    Initialize the SD / MMC host controller.
*
*  Parameters
*    Unit     Index of the SD / MMC host controller (0-based).
*/
static void _HW_Init(U8 Unit) {
  unsigned ms;
  U32      Delay_ns;
  U32      Inst_ns;

  FS_USE_PARA(Unit);
  //
  // Configure the SDMMC and the other peripherals.
  //
  //_ConfigureClocks();
  //_EnablePeripherals();
  //_ConfigurePins();
  //
  // Utilize HAL function generated by CubeMX
  //
  SD_HandleTypeDef hsd1;
  hsd1.Instance = SDMMC1;
  HAL_SD_MspInit(&hsd1);
  //
  // Calculate the number of software loops to delay before two write operations to the same SDMMC register.
  //
  Delay_ns = ((1000000000uL + ((FS_MMC_HW_CM_AHB_CLK) - 1)) / FS_MMC_HW_CM_AHB_CLK) * 7;    // 7 AHB clocks have to elapse between two write operations to the same register.
  Inst_ns  = (1000000000uL + (FS_MMC_HW_CM_CPU_CLK - 1)) / FS_MMC_HW_CM_CPU_CLK;
  _NumLoopsWriteRegDelay = Delay_ns / Inst_ns;
  //
  // Initialize SDMMC
  //
  SDMMC_POWER  = 0;                           // Switch off the power to SD card.
  SDMMC_CLKCR  = 1uL << CLKCR_PWRSAV_BIT;     // Disable the clock to SD card.
  SDMMC_ARGR   = 0;
  SDMMC_CMDR   = 0;
  SDMMC_DTIMER = 0;
  SDMMC_DLENR  = 0;
  SDMMC_DCTRLR = 0;
  SDMMC_ICR    = 0xFFFFFFFF;                  // Clear all interrupts
  SDMMC_MASKR  = 0;
  _WriteRegDelayed(&SDMMC_POWER, POWER_ON);   // Enable the power to SD card. Take into account the time between the write operations.
#if FS_MMC_HW_CM_USE_OS
  //
  // Unmask the interrupt sources.
  //
  SDMMC_MASKR  = MASK_ALL;
  //
  // Set the priority and enable the interrupts.
  //
  _EnableInt();
#endif // FS_MMC_HW_CM_USE_OS
  //
  // Wait for the power supply of the SD card to become ready.
  //
  ms = FS_MMC_HW_CM_POWER_GOOD_DELAY;
  while (ms--) {
    _Delay1ms();
  }
}

/*********************************************************************
*
*       _HW_Delay
*
*  Function description
*    Blocks the execution for the specified time.
*
*  Parameters
*    ms   Number of milliseconds to delay.
*/
static void _HW_Delay(int ms) {
  while (ms--) {
    _Delay1ms();
  }
}

/*********************************************************************
*
*       _HW_IsPresent
*
*  Function description
*    Returns the state of the media.
*
*  Parameters
*    Unit     Index of the SD / MMC host controller (0-based).
*
*  Return value
*    FS_MEDIA_STATE_UNKNOWN     The state of the storage device is not known.
*    FS_MEDIA_NOT_PRESENT       The storage device is not present.
*    FS_MEDIA_IS_PRESENT        The storage device is present.
*
*  Additional information
*    If the state is unknown, the function has to FS_MEDIA_STATE_UNKNOWN
*    and the higher layers of the file system will try to figure out
*    if a storage device is present or not.
*/
static int _HW_IsPresent(U8 Unit) {
  int Status;

  FS_USE_PARA(Unit);
  Status = FS_MEDIA_IS_PRESENT;
  if (HAL_GPIO_ReadPin(SD_CD_GPIO_Port, SD_CD_Pin)==GPIO_PIN_SET) {
    Status = FS_MEDIA_NOT_PRESENT;
  }
  return Status;
}

/*********************************************************************
*
*       _HW_IsWriteProtected
*
*  Function description
*    Returns whether card is write protected or not.
*
*  Parameters
*    Unit     Index of the SD / MMC host controller (0-based).
*
*  Return value
*    ==0      The SD / MMC card is writable.
*    ==1      The SD / MMC card is write protected.
*/
static int _HW_IsWriteProtected(U8 Unit) {
  FS_USE_PARA(Unit);
  return 0;                             // The micro SD cards do not have a write protect switch.
}

/*********************************************************************
*
*       _HW_SetMaxSpeed
*
*  Function description
*    Sets the frequency of the MMC/SD card controller.
*    The frequency is given in kHz.
*
*  Parameters
*    Unit     Index of the SD / MMC host controller (0-based).
*
*  Additional information
*    This function is called two times:
*    1. During card initialization
*       Initialize the frequency to not more than 400kHz.
*
*    2. After card initialization
*       The CSD register of card is read and the max frequency
*       the card can operate is determined.
*       [In most cases: MMC cards 20MHz, SD cards 25MHz]
*/
static U16 _HW_SetMaxSpeed(U8 Unit, U16 Freq) {
  U32 Fact;
  U32 Div;
  U32 v;

  FS_USE_PARA(Unit);
  if (Freq > FS_MMC_HW_CM_MAX_SD_CLK) {
    Freq = FS_MMC_HW_CM_MAX_SD_CLK;
  }
  _WaitForInactive();
  Div  = 0;
  Fact = 1;
  while ((Freq * Fact) < FS_MMC_HW_CM_SDMMC_CLK) {
    if (Fact == 1) {
      Fact = 2;
    } else {
      Fact += 2;
    }
    if (++Div == CLKCR_CLKDIV_MAX) {
      break;
    }
  }
  v = SDMMC_CLKCR;
  v &= ~(CLKCR_CLKDIV_MASK << CLKCR_CLKDIV_BIT);
  v |= 0
    |  Div
    |  (1uL << CLKCR_PWRSAV_BIT)
    |  (1uL << CLKCR_HWFC_EN_BIT)
    ;
  _WriteRegDelayed(&SDMMC_CLKCR, v);
  Freq = (U16)(FS_MMC_HW_CM_SDMMC_CLK / (Div * 2));
  return Freq;
}

/*********************************************************************
*
*       _HW_SetResponseTimeOut
*
*  Function description
*    Sets the response time out value given in MMC/SD card cycles.
*
*  Parameters
*    Unit     Index of the SD / MMC host controller (0-based).
*
*  Notes
*    (1) The SD host controller has only a short and a long timeout.
*        We always set the long timeout before we send a command.
*/
static void _HW_SetResponseTimeOut(U8 Unit, U32 Value) {
  FS_USE_PARA(Unit);
  FS_USE_PARA(Value);
  //
  // The response timeout is fixed in hardware
  //
}

/*********************************************************************
*
*       _HW_SetReadDataTimeOut
*
*  Function description
*    Sets the read data time out value given in MMC/SD card cycles.
*
*  Parameters
*    Unit     Index of the SD / MMC host controller (0-based).
*/
static void _HW_SetReadDataTimeOut(U8 Unit, U32 Value) {
  FS_USE_PARA(Unit);
  SDMMC_DTIMER = Value;
}

/*********************************************************************
*
*       _HW_SendCmd
*
*  Function description
*    Sends a command to the card.
*
*  Parameters
*    Unit           Index of the SD / MMC host controller (0-based).
*    Cmd            Command number according to [1]
*    CmdFlags       Additional information about the command to execute
*    ResponseType   Type of response as defined in [1]
*    Arg            Command parameter
*/
static void _HW_SendCmd(U8 Unit, unsigned Cmd, unsigned CmdFlags, unsigned ResponseType, U32 Arg) {
  U32 cmdr;
  U32 dlenr;
  U32 dctrlr;
  U32 icr;
  U32 NumBytes;
  U32 v;
  int r;

  FS_USE_PARA(Unit);
  _WaitForInactive();
  _RepeatSame = 0;
  if (CmdFlags & FS_MMC_CMD_FLAG_WRITE_BURST_FILL) {
    _RepeatSame = 1;
  }
  _IsR1Busy = 0;
  if (CmdFlags & FS_MMC_CMD_FLAG_SETBUSY) {
    _IsR1Busy = 1;
  }
  cmdr = 0
       | ((Cmd & CMDR_CMDINDEX_MASK) << CMDR_CMDINDEX_BIT)
       | (1uL << CMDR_CPSMEN_BIT)
       ;
  switch (ResponseType) {
  case FS_MMC_RESPONSE_FORMAT_R3:
    cmdr |= CMDR_WAITRESP_SHORT_NO_CRC << CMDR_WAITRESP_BIT;
    break;
  case FS_MMC_RESPONSE_FORMAT_R1:
    cmdr |= CMDR_WAITRESP_SHORT << CMDR_WAITRESP_BIT;
    break;
  case FS_MMC_RESPONSE_FORMAT_R2:
    cmdr |= CMDR_WAITRESP_LONG << CMDR_WAITRESP_BIT;
    break;
  default:
    break;
  }
  v  = SDMMC_CLKCR;
  v &= ~(CLKCR_WIDBUS_MASK << CLKCR_WIDBUS_BIT);
  if (CmdFlags & FS_MMC_CMD_FLAG_USE_SD4MODE) {   // 4 bit mode?
    v |= CLKCR_WIDBUS_4BIT << CLKCR_WIDBUS_BIT;
  }
  SDMMC_CLKCR = v;
  dctrlr = 0;
  dlenr  = 0;
  icr    = 0;
  if (CmdFlags & FS_MMC_CMD_FLAG_DATATRANSFER) {
    cmdr |= 1uL << CMDR_CMDTRANS_BIT;
    icr  |= 0
         | (1uL << ICR_DCRCFAILC_BIT)
         | (1uL << ICR_DTIMEOUTC_BIT)
         | (1uL << ICR_TXUNDERRC_BIT)
         | (1uL << ICR_DATAENDC_BIT)
         | (1uL << ICR_DBCKENDC_BIT)
         | (1uL << ICR_RXOVERRC_BIT)
         ;
    NumBytes = _BlockSize * _NumBlocks;
    dlenr  = NumBytes & DLENR_DATALENGTH_MASK;
    dctrlr = 0
           | (_ld(_BlockSize) << DCTRLR_DBLOCKSIZE_BIT)
           ;
    if ((CmdFlags & FS_MMC_CMD_FLAG_WRITETRANSFER) == 0) {
      dctrlr |= 0
             | (1uL << DCTRLR_DTDIR_BIT)
             ;
    }
  }
  //
  // Clear pending status flags
  //
  icr |= 0
      | (1uL << ICR_CCRCFAILC_BIT)
      | (1uL << ICR_CTIMEOUTC_BIT)
      | (1uL << ICR_CMDRENDC_BIT)
      | (1uL << ICR_CMDSENTC_BIT)
      | (1uL << ICR_BUSY0ENDC_BIT)
      ;
  //
  // Execute the command.
  //
  SDMMC_ICR    = icr;
#if FS_MMC_HW_CM_USE_OS
  {
    U8 IsDataRead;

    IsDataRead = 0;
    if (CmdFlags & FS_MMC_CMD_FLAG_DATATRANSFER) {
      if ((CmdFlags & FS_MMC_CMD_FLAG_WRITETRANSFER) == 0) {
        IsDataRead = 1;
      }
    }
    //
    // We cannot use interrupts when reading data from SD card using CPU
    // because the SDMMC controller does not provide any flag to indicate
    // that data is present in the internal FIFO.
    //
    if (IsDataRead) {
      _DisableInt();
    } else {
      _EnableInt();
    }
    _StatusSDMMC = 0;
  }
#endif // FS_MMC_HW_CM_USE_OS
  SDMMC_DLENR  = dlenr;
  SDMMC_DCTRLR = dctrlr;
  SDMMC_ARGR   = Arg;
  SDMMC_CMDR   = cmdr;
  if((CmdFlags & FS_MMC_CMD_FLAG_INITIALIZE)
     || (ResponseType == FS_MMC_RESPONSE_FORMAT_NONE)) {
    r = _WaitEndOfCommand();                                // Wait here for commands without response to finish.
    if (r) {
      _Reset();
    }
  }
}

/*********************************************************************
*
*       _HW_GetResponse
*
*  Function description
*    Receives the responses that was sent by the card after
*    a command was sent to the card.
*
*  Parameters
*    Unit     Index of the SD / MMC host controller (0-based).
*    pBuffer  User allocated buffer where the response is stored.
*    Size     Size of the buffer in bytes
*
*  Return values
*     FS_MMC_CARD_NO_ERROR                Success
*     FS_MMC_CARD_RESPONSE_CRC_ERROR      CRC error in response
*     FS_MMC_CARD_RESPONSE_TIMEOUT        No response received
*     FS_MMC_CARD_RESPONSE_GENERIC_ERROR  Any other error
*
*  Notes
*     (1) The response data has to be stored at byte offset 1 since
*         the controller does not provide the first byte of response.
*/
static int _HW_GetResponse(U8 Unit, void * pBuffer, U32 Size) {
  U8           * p;
  int            NumBytes;
  volatile U32 * pReg;
  U32            Data32;
  U32            NumWords;
  int            r;

  FS_USE_PARA(Unit);
  p        = (U8 *)pBuffer;
  NumBytes = (int)Size;
  r = _WaitEndOfResponse();
  if (r) {
    _Reset();
  } else {
    *p++ = (U8)SDMMC_RESPCMDR;
    NumBytes--;
    pReg = (volatile U32 *)(&SDMMC_RESP1R);
    NumWords = (U32)NumBytes >> 2;
    if (NumWords) {
      do {
        Data32 = *pReg++;
        *p++ = (U8)(Data32 >> 24);
        *p++ = (U8)(Data32 >> 16);
        *p++ = (U8)(Data32 >> 8);
        *p++ = (U8)Data32;
        NumBytes -= 4;
      } while (--NumWords);
    }
    if (NumBytes == 3) {
      Data32 = *pReg;
      *p++ = (U8)(Data32 >> 24);
      *p++ = (U8)(Data32 >> 16);
      *p++ = (U8)(Data32 >> 8);
    }
    if (NumBytes == 2) {
      Data32 = *pReg;
      *p++ = (U8)(Data32 >> 24);
      *p++ = (U8)(Data32 >> 16);
    }
    if (NumBytes == 1) {
      Data32 = *pReg;
      *p++ = (U8)(Data32 >> 24);
    }
  }
  return r;
}

/*********************************************************************
*
*       _HW_ReadData
*
*  Function description
*    Reads data from the card using the SD / MMC host controller.
*
*  Return values
*    FS_MMC_CARD_NO_ERROR             Success
*    FS_MMC_CARD_READ_CRC_ERROR       CRC error in received data
*    FS_MMC_CARD_READ_TIMEOUT         No data received
*    FS_MMC_CARD_READ_GENERIC_ERROR   Any other error
*/
static int _HW_ReadData(U8 Unit, void * pBuffer, unsigned NumBytes, unsigned NumBlocks) {
  int r;

  FS_USE_PARA(Unit);
  FS_USE_PARA(pBuffer);
  FS_USE_PARA(NumBytes);
  FS_USE_PARA(NumBlocks);
  r = _ReadData();
  if (r) {
    _Reset();
  }
  return r;
}

/*********************************************************************
*
*       _HW_WriteData
*
*  Function description
*    Writes the data to SD / MMC card using the SD / MMC host controller.
*
*  Return values
*    FS_MMC_CARD_NO_ERROR      Success
*    FS_MMC_CARD_READ_TIMEOUT  No data received
*/
static int _HW_WriteData(U8 Unit, const void * pBuffer, unsigned NumBytes, unsigned NumBlocks) {
  int r;

  FS_USE_PARA(Unit);
  FS_USE_PARA(pBuffer);
  FS_USE_PARA(NumBytes);
  FS_USE_PARA(NumBlocks);
  r = _WriteData();
  if (r) {
    _Reset();
  }
  return r;
}

/*********************************************************************
*
*       _HW_SetDataPointer
*
*  Function description
*    Tells the hardware layer where to read data from  or write data to.
*    Some SD host controllers require the address of the data buffer
*    before sending the command to the card, eg. programming the DMA.
*    In most cases this function can be left empty.
*
*  Parameters
*    Unit     Index of the SD / MMC host controller (0-based).
*    p        Data buffer.
*/
static void _HW_SetDataPointer(U8 Unit, const void * p) {
  FS_USE_PARA(Unit);
  _pBuffer = (void *)p; // cast const away as this buffer is used also for storing the data from card
}

/*********************************************************************
*
*       _HW_SetBlockLen
*
*  Function description
*    Sets the block size (sector size) that has to be transferred.
*
*  Parameters
*    Unit     Index of the SD / MMC host controller (0-based).
*/
static void _HW_SetBlockLen(U8 Unit, U16 BlockSize) {
  FS_USE_PARA(Unit);
  _BlockSize = BlockSize;
}

/*********************************************************************
*
*       _HW_SetNumBlocks
*
*  Function description
*    Sets the number of blocks (sectors) to be transferred.
*
*  Parameters
*    Unit     Index of the SD / MMC host controller (0-based).
*/
static void _HW_SetNumBlocks(U8 Unit, U16 NumBlocks) {
  FS_USE_PARA(Unit);
  _NumBlocks = NumBlocks;
}

/*********************************************************************
*
*       _HW_GetMaxReadBurst
*
*  Function description
*    Returns the number of block (sectors) that can be read at once
*    with a single READ_MULTIPLE_SECTOR command.
*
*  Parameters
*    Unit     Index of the SD / MMC host controller (0-based).
*
*  Return value
*    Number of sectors that can be read at once.
*/
static U16 _HW_GetMaxReadBurst(U8 Unit) {
  FS_USE_PARA(Unit);
  return (U16)MAX_NUM_BLOCKS;
}

/*********************************************************************
*
*       _HW_GetMaxWriteBurst
*
*  Function description
*    Returns the number of block (sectors) that can be written at once
*    with a single WRITE_MULTIPLE_SECTOR command.
*
*  Parameters
*    Unit     Index of the SD / MMC host controller (0-based).
*
*  Return value
*    Number of sectors that can be written at once.
*/
static U16 _HW_GetMaxWriteBurst(U8 Unit) {
  FS_USE_PARA(Unit);
  return (U16)MAX_NUM_BLOCKS;
}

/*********************************************************************
*
*       _HW_GetMaxWriteBurstFill
*
*  Function description
*    Returns the number of block (sectors) that can be written at once
*    with a single WRITE_MULTIPLE_SECTOR command. The contents of the
*    sectors is filled with the same 32-bit pattern.
*
*  Parameters
*    Unit     Index of the SD / MMC host controller (0-based).
*
*  Return value
*    Number of sectors that can be written at once.
*/
static U16 _HW_GetMaxWriteBurstFill(U8 Unit) {
  FS_USE_PARA(Unit);
  return (U16)MAX_NUM_BLOCKS;
}

/*********************************************************************
*
*       Global data
*
**********************************************************************
*/
const FS_MMC_HW_TYPE_CM FS_MMC_HW_CM_STM32H735_Morpheus = {
  _HW_Init,
  _HW_Delay,
  _HW_IsPresent,
  _HW_IsWriteProtected,
  _HW_SetMaxSpeed,
  _HW_SetResponseTimeOut,
  _HW_SetReadDataTimeOut,
  _HW_SendCmd,
  _HW_GetResponse,
  _HW_ReadData,
  _HW_WriteData,
  _HW_SetDataPointer,
  _HW_SetBlockLen,
  _HW_SetNumBlocks,
  _HW_GetMaxReadBurst,
  _HW_GetMaxWriteBurst,
  NULL,
  _HW_GetMaxWriteBurstFill,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL
};

/*************************** End of file ****************************/
