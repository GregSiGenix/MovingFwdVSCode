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
File        : USBH_Config_STM32H743_FS_ST_STM32H7xxI_EVAL.c
Purpose     : emUSB Host configuration file for the ST STM32 MB1246 eval board
---------------------------END-OF-HEADER------------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#include <stdlib.h>
#include "USBH.h"
#include "BSP_USB.h"
#include "USBH_HW_STM32H7xxHS.h"
#include "stm32h7xx.h"  // For the cache handling functions.
//
// Driver for MFX IO expander
//
#include "stm32h743i_eval.h"
#include "stm32h743i_eval_io.h"


/*********************************************************************
*
*       Defines, configurable
*
**********************************************************************
*/
#define STM32_OTG_BASE_ADDRESS      0x40080000uL

#define USB_RAM_ADDRESS             0x30000000     // Address of memory dedicated to the stack (SRAM1)
#define USB_RAM_SIZE                   0x20000     // Size of memory dedicated to the stack in bytes

#define USB_ISR_ID                  101
#define USB_ISR_PRIO                253

/*********************************************************************
*
*       Defines, sfrs
*
**********************************************************************
*/

//
// RCC
//
#define RCC_BASE_ADDR             0x58024400u
#define RCC_CR                    (*(volatile U32 *)(RCC_BASE_ADDR + 0x00))
#define RCC_PLLCKSELR             (*(volatile U32 *)(RCC_BASE_ADDR + 0x28))
#define RCC_PLLCFGR               (*(volatile U32 *)(RCC_BASE_ADDR + 0x2C))
#define RCC_PLL3DIVR              (*(volatile U32 *)(RCC_BASE_ADDR + 0x40))
#define RCC_D2CCIP2R              (*(volatile U32 *)(RCC_BASE_ADDR + 0x54))
#define RCC_AHB1RSTR              (*(volatile U32 *)(RCC_BASE_ADDR + 0x80))
#define RCC_AHB1ENR               (*(volatile U32 *)(RCC_BASE_ADDR + 0xD8))
#define RCC_AHB2ENR               (*(volatile U32 *)(RCC_BASE_ADDR + 0xDC))
#define RCC_AHB4ENR               (*(volatile U32 *)(RCC_BASE_ADDR + 0xE0))

//
// AXI
//
#define AXI_BASE_ADDR             0x51000000u
#define AXI_TARG7_FN_MOD_ISS_BM   (*(volatile U32 *)(AXI_BASE_ADDR + 0x1008 + 0x7000))

//
// PWR
//
#define PWR_BASE_ADDR             0x58024800u
#define PWR_CR3                   (*(volatile U32 *)(PWR_BASE_ADDR + 0xC))

//
// GPIO
//
#define GPIOA_BASE_ADDR           ((unsigned int)0x58020000)
#define GPIOA_MODER               (*(volatile U32 *)(GPIOA_BASE_ADDR + 0x00))
#define GPIOA_OTYPER              (*(volatile U32 *)(GPIOA_BASE_ADDR + 0x04))
#define GPIOA_OSPEEDR             (*(volatile U32 *)(GPIOA_BASE_ADDR + 0x08))
#define GPIOA_PUPDR               (*(volatile U32 *)(GPIOA_BASE_ADDR + 0x0C))
#define GPIOA_IDR                 (*(volatile U32 *)(GPIOA_BASE_ADDR + 0x10))
#define GPIOA_ODR                 (*(volatile U32 *)(GPIOA_BASE_ADDR + 0x14))
#define GPIOA_BSRRL               (*(volatile U16 *)(GPIOA_BASE_ADDR + 0x18))
#define GPIOA_BSRRH               (*(volatile U16 *)(GPIOA_BASE_ADDR + 0x16))
#define GPIOA_LCKR                (*(volatile U32 *)(GPIOA_BASE_ADDR + 0x1C))
#define GPIOA_AFRL                (*(volatile U32 *)(GPIOA_BASE_ADDR + 0x20))
#define GPIOA_AFRH                (*(volatile U32 *)(GPIOA_BASE_ADDR + 0x24))

#define GPIOB_BASE_ADDR           ((unsigned int)0x58020400)
#define GPIOB_MODER               (*(volatile U32 *)(GPIOB_BASE_ADDR + 0x00))
#define GPIOB_OTYPER              (*(volatile U32 *)(GPIOB_BASE_ADDR + 0x04))
#define GPIOB_OSPEEDR             (*(volatile U32 *)(GPIOB_BASE_ADDR + 0x08))
#define GPIOB_PUPDR               (*(volatile U32 *)(GPIOB_BASE_ADDR + 0x0C))
#define GPIOB_IDR                 (*(volatile U32 *)(GPIOB_BASE_ADDR + 0x10))
#define GPIOB_ODR                 (*(volatile U32 *)(GPIOB_BASE_ADDR + 0x14))
#define GPIOB_BSRRL               (*(volatile U16 *)(GPIOB_BASE_ADDR + 0x18))
#define GPIOB_BSRRH               (*(volatile U16 *)(GPIOB_BASE_ADDR + 0x16))
#define GPIOB_LCKR                (*(volatile U32 *)(GPIOB_BASE_ADDR + 0x1C))
#define GPIOB_AFRL                (*(volatile U32 *)(GPIOB_BASE_ADDR + 0x20))
#define GPIOB_AFRH                (*(volatile U32 *)(GPIOB_BASE_ADDR + 0x24))

#define GPIOC_BASE_ADDR           ((unsigned int)0x58020800)
#define GPIOC_MODER               (*(volatile U32 *)(GPIOC_BASE_ADDR + 0x00))
#define GPIOC_OTYPER              (*(volatile U32 *)(GPIOC_BASE_ADDR + 0x04))
#define GPIOC_OSPEEDR             (*(volatile U32 *)(GPIOC_BASE_ADDR + 0x08))
#define GPIOC_PUPDR               (*(volatile U32 *)(GPIOC_BASE_ADDR + 0x0C))
#define GPIOC_IDR                 (*(volatile U32 *)(GPIOC_BASE_ADDR + 0x10))
#define GPIOC_ODR                 (*(volatile U32 *)(GPIOC_BASE_ADDR + 0x14))
#define GPIOC_BSRRL               (*(volatile U16 *)(GPIOC_BASE_ADDR + 0x18))
#define GPIOC_BSRRH               (*(volatile U16 *)(GPIOC_BASE_ADDR + 0x16))
#define GPIOC_LCKR                (*(volatile U32 *)(GPIOC_BASE_ADDR + 0x1C))
#define GPIOC_AFRL                (*(volatile U32 *)(GPIOC_BASE_ADDR + 0x20))
#define GPIOC_AFRH                (*(volatile U32 *)(GPIOC_BASE_ADDR + 0x24))

#define GPIOH_BASE_ADDR           ((unsigned int)0x58021C00)
#define GPIOH_MODER               (*(volatile U32 *)(GPIOH_BASE_ADDR + 0x00))
#define GPIOH_OTYPER              (*(volatile U32 *)(GPIOH_BASE_ADDR + 0x04))
#define GPIOH_OSPEEDR             (*(volatile U32 *)(GPIOH_BASE_ADDR + 0x08))
#define GPIOH_PUPDR               (*(volatile U32 *)(GPIOH_BASE_ADDR + 0x0C))
#define GPIOH_IDR                 (*(volatile U32 *)(GPIOH_BASE_ADDR + 0x10))
#define GPIOH_ODR                 (*(volatile U32 *)(GPIOH_BASE_ADDR + 0x14))
#define GPIOH_BSRRL               (*(volatile U16 *)(GPIOH_BASE_ADDR + 0x18))
#define GPIOH_BSRRH               (*(volatile U16 *)(GPIOH_BASE_ADDR + 0x16))
#define GPIOH_LCKR                (*(volatile U32 *)(GPIOH_BASE_ADDR + 0x1C))
#define GPIOH_AFRL                (*(volatile U32 *)(GPIOH_BASE_ADDR + 0x20))
#define GPIOH_AFRH                (*(volatile U32 *)(GPIOH_BASE_ADDR + 0x24))

#define GPIOI_BASE_ADDR           ((unsigned int)0x58022000)
#define GPIOI_MODER               (*(volatile U32 *)(GPIOI_BASE_ADDR + 0x00))
#define GPIOI_OTYPER              (*(volatile U32 *)(GPIOI_BASE_ADDR + 0x04))
#define GPIOI_OSPEEDR             (*(volatile U32 *)(GPIOI_BASE_ADDR + 0x08))
#define GPIOI_PUPDR               (*(volatile U32 *)(GPIOI_BASE_ADDR + 0x0C))
#define GPIOI_IDR                 (*(volatile U32 *)(GPIOI_BASE_ADDR + 0x10))
#define GPIOI_ODR                 (*(volatile U32 *)(GPIOI_BASE_ADDR + 0x14))
#define GPIOI_BSRRL               (*(volatile U16 *)(GPIOI_BASE_ADDR + 0x18))
#define GPIOI_BSRRH               (*(volatile U16 *)(GPIOI_BASE_ADDR + 0x16))
#define GPIOI_LCKR                (*(volatile U32 *)(GPIOI_BASE_ADDR + 0x1C))
#define GPIOI_AFRL                (*(volatile U32 *)(GPIOI_BASE_ADDR + 0x20))
#define GPIOI_AFRH                (*(volatile U32 *)(GPIOI_BASE_ADDR + 0x24))



/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
#ifndef USB_RAM_ADDRESS
  static U32 _aMemPool[USB_RAM_SIZE / 4];
  #define USB_RAM_ADDRESS _aMemPool
#endif

//
// Define categories of debug log messages that should be printed.
// For possible categories, see USBH_MCAT_... definitions in USBH.h
//
static const U8 _LogCategories[] = {
  USBH_MCAT_INIT,
  USBH_MCAT_APPLICATION
};

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

static void _CleanDCache(void *p, unsigned long NumBytes) {
  SCB_CleanDCache_by_Addr((uint32_t *)p, NumBytes);
}
static void _InvalidateDCache(void *p, unsigned long NumBytes) {
  SCB_InvalidateDCache_by_Addr((uint32_t *)p, NumBytes);
}

static const SEGGER_CACHE_CONFIG _CacheConfig = {
  32,                            // CacheLineSize of CPU
  NULL,                          // pfDMB
  _CleanDCache,                  // pfClean
  _InvalidateDCache              // pfInvalidate
};

/*********************************************************************
*
*       _InitUSBHw
*
*/
static void _InitUSBHw(void) {
  U32                Tmp;

  //
  // Configure MFX IO expander for OTG FS VBUS switch
  //
  BSP_IO_Init();
  BSP_IO_ConfigPin(OTG_FS1_POWER_SWITCH_PIN, IO_MODE_OUTPUT);
  //
  // Configure IO's
  //
  RCC_AHB4ENR |= 0
              | (1 <<  0)  // GPIOAEN: IO port A clock enable
              ;
  //
  //  PA10: USB_ID
  //
  GPIOA_MODER    =   (GPIOA_MODER  & ~(3UL  <<  20)) | (2UL  <<  20);
  GPIOA_OTYPER  |=   (1UL  <<  10);
  GPIOA_OSPEEDR |=   (3UL  <<  20);
  GPIOA_PUPDR    =   (GPIOA_PUPDR & ~(3UL  <<  20)) | (1UL << 20);
  GPIOA_AFRH     =   (GPIOA_AFRH  & ~(15UL << 8)) | (10UL << 8);
  //
  //  PA11: USB_DM
  //
  GPIOA_MODER    =   (GPIOA_MODER  & ~(3UL  <<  22)) | (2UL  <<  22);
  GPIOA_OTYPER  &=  ~(1UL  <<  11);
  GPIOA_OSPEEDR |=   (3UL  <<  22);
  GPIOA_PUPDR   &=  ~(3UL  <<  22);
  GPIOA_AFRH     =   (GPIOA_AFRH  & ~(15UL << 12)) | (10UL << 12);
  //
  //  PA12: USB_DP
  //
  GPIOA_MODER    =   (GPIOA_MODER  & ~(3UL  <<  24)) | (2UL  <<  24);
  GPIOA_OTYPER  &=  ~(1UL  <<  12);
  GPIOA_OSPEEDR |=   (3UL  <<  24);
  GPIOA_PUPDR   &=  ~(3UL  <<  24);
  GPIOA_AFRH     =   (GPIOA_AFRH  & ~(15UL << 16)) | (10UL << 16);
  //
  // Configure PLL3 to 48 MHz (assuming that external 25 MHz crystal is used).
  //
  RCC_CR &= ~(1uL << 28);
  Tmp = RCC_PLLCKSELR & ~((0x3Fu << 20) | 3u);
  Tmp |= ((5uL << 20) | 2u);                               // Set DIVM = 5
  RCC_PLLCKSELR = Tmp;
  USBH_OS_Delay(2);
  if (((RCC_PLLCKSELR ^ Tmp) & ((0x3Fu << 20) | 3u)) != 0) {
    //
    // Fatal: PLL already configured, can't be reconfigured, Stop.
    //
    for (;;) {}
  }
  RCC_PLLCFGR  &= ~0xF00;          // clear bits 8 to 11
  RCC_PLLCFGR  |= (2uL << 10)      // set input range 4 to 8 MHz
               |  (1uL << 23);     // PLL3 Q output enable
  RCC_PLL3DIVR &= ~0x1FFu;
  RCC_PLL3DIVR |= 95u;             // Set DIVN to 96
  RCC_PLL3DIVR &= ~(0x7FuL << 16);
  RCC_PLL3DIVR |= (9uL << 16);     // Set DIVQ to 10
  //
  // Enable PLL3
  //
  RCC_CR |= (1uL << 28);
  while ((RCC_CR & (1uL << 29)) == 0) {
  }
  //
  // Set USB clock selector to PLL3
  //
  RCC_D2CCIP2R |= (2uL << 20);
  //
  //  Enable clock for OTG_HS2
  //
  RCC_AHB1ENR    |=  (1uL << 27);
  USBH_OS_Delay(100);
  //
  // Reset USB clock
  //
  RCC_AHB1RSTR   |=  (1uL << 27);
  USBH_OS_Delay(100);
  RCC_AHB1RSTR   &= ~(1uL << 27);
  USBH_OS_Delay(400);
  //
  // Enable voltage level detector for transceiver
  //
  PWR_CR3 |= (1u << 24);
  //
  // Workaround to avoid AXI SRAM corruption (see STM32H753xI Errata sheet Rev. 2, November 2017)
  // According to ST this errata has been fixed with chip revisions X and V.
  // If you are using one of the newer chips you can remove the following line.
  //
  AXI_TARG7_FN_MOD_ISS_BM |= 1;
}

/*********************************************************************
*
*       _OnPortPowerControl
*/
static void _OnPortPowerControl(U32 HostControllerIndex, U8 Port, U8 PowerOn) {
  USBH_USE_PARA(HostControllerIndex);
  USBH_USE_PARA(Port);

  if (PowerOn == 0) {
    BSP_IO_WritePin(OTG_FS1_POWER_SWITCH_PIN, BSP_IO_PIN_RESET);
  }
  else {
    BSP_IO_WritePin(OTG_FS1_POWER_SWITCH_PIN, BSP_IO_PIN_SET);
  }
}

/*********************************************************************
*
*       _ISR
*/
static void _ISR(void) {
  USBH_ServiceISR(0);
}

/*********************************************************************
*
*       _CheckForValidDMAAddress
*
*  Function description
*    Checks is a memory location can be used for DMA transfers.
*
*  Parameters
*    p:  Pointer to the memory location to be checked.
*
*  Return value
*    0:  Valid address for DMA use.
*    1:  Address not allowed for DMA.
*/
static int _CheckForValidDMAAddress(const void *p) {
  //
  // DTCM RAM can't be used by DMA
  //
  if ((U32)p < 0x24000000 && (U32)p >= 0x20000000) {
    return 1;
  }
  return 0;
}

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       USBH_X_Config
*
*  Function description
*/
void USBH_X_Config(void) {
  //
  // Assigning memory should be the first thing
  //
  RCC_AHB2ENR |= (1u << 29);                               // Enable SRAM1, where memory pool resides.
  USBH_AssignMemory((void *)USB_RAM_ADDRESS, USB_RAM_SIZE);
  // USBH_ConfigSupportExternalHubs(1);            // Default values: The hub module is disabled, this is done to save memory.
  // USBH_ConfigPowerOnGoodTime    (300);          // Default values: 300 ms wait time before the hosts starts communicating with the device.
  //
  // Define log and warn filter
  // Note: The terminal I/O emulation affects the timing
  // of your communication, since the debugger stops the target
  // for every terminal I/O unless you use RTT!
  //
  USBH_ConfigMsgFilter(USBH_WARN_FILTER_SET_ALL, 0, NULL);           // Output all warnings.
  USBH_ConfigMsgFilter(USBH_LOG_FILTER_SET, sizeof(_LogCategories), _LogCategories);
  _InitUSBHw();
  USBH_SetCacheConfig(&_CacheConfig, sizeof(_CacheConfig));  // Set cache configuration for USBH stack.
  USBH_STM32H7_HS_AddEx((void*)STM32_OTG_BASE_ADDRESS, 1);
  USBH_STM32H7_HS_SetCheckAddress(_CheckForValidDMAAddress);
  //
  //  Please uncomment this function when using OTG functionality.
  //  Otherwise the VBUS power-on will be permanently on and will cause
  //  OTG to detect a session where no session is available.
  //
  // USBH_SetOnSetPortPower(_OnPortPowerControl);                 // This function sets a callback in order to allow to control the power on option of a port.
  _OnPortPowerControl(0, 0, 1);                                // Enable Power on for port
  BSP_USBH_InstallISR_Ex(USB_ISR_ID, _ISR, USB_ISR_PRIO);
}

/********************************* EOF ******************************/
