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
File        : USBH_Config_STM32H743_HS_ST_STM32H7xxI_EVAL.c
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

/*********************************************************************
*
*       Defines, configurable
*
**********************************************************************
*/
#define STM32_OTG_BASE_ADDRESS      0x40040000uL

#define USB_RAM_ADDRESS             0x30000000     // Address of memory dedicated to the stack (SRAM1)
#define USB_RAM_SIZE                   0x20000     // Size of memory dedicated to the stack in bytes

#define USB_ISR_ID     77
#define USB_ISR_PRIO  254

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
#define RCC_APB4ENR               (*(volatile U32 *)(RCC_BASE_ADDR + 0xF4))

//
// AXI
//
#define AXI_BASE_ADDR             0x51000000u
#define AXI_TARG7_FN_MOD_ISS_BM   (*(volatile U32 *)(AXI_BASE_ADDR + 0x1008 + 0x7000))

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
  RCC_AHB4ENR |= 0
              | (1 <<  8)  // GPIOIEN: IO port I clock enable
              | (1 <<  7)  // GPIOHEN: IO port H clock enable
              | (1 <<  2)  // GPIOCEN: IO port C clock enable
              | (1 <<  1)  // GPIOBEN: IO port B clock enable
              | (1 <<  0)  // GPIOAEN: IO port A clock enable
              ;
  //
  // UPLI data pins
  // PA3 (OTG_HS_ULPI alternate function, DATA0)
  //
  GPIOA_MODER    =   (GPIOA_MODER  & ~(3UL  <<  6)) | (2UL  <<  6);
  GPIOA_OTYPER  &=  ~(1UL  <<  3);
  GPIOA_OSPEEDR |=   (3UL  <<  6);
  GPIOA_PUPDR   &=  ~(3UL  <<  6);
  GPIOA_AFRL     =   (GPIOA_AFRL  & ~(15UL << 12)) | (10UL << 12);
  //
  // PB0, PB1 (OTG_HS_ULPI alternate function, DATA1, DATA2)
  //
  GPIOB_MODER    =   (GPIOB_MODER  & ~(15UL <<  0)) | (10UL <<  0);
  GPIOB_OTYPER  &=  ~(3UL  <<  0);
  GPIOB_OSPEEDR |=   (15UL <<  0);
  GPIOB_PUPDR   &=  ~(15UL <<  0);
  GPIOB_AFRL     =   (GPIOB_AFRL  & ~(0xFFUL <<  0)) | (0xAA <<  0);
  //
  // PB10..13 (OTG_HS_ULPI alternate function, DATA3 to DATA6)
  //
  GPIOB_MODER    =   (GPIOB_MODER  & ~(0xFFUL << 20)) | (0xAA << 20);
  GPIOB_OTYPER  &=  ~(15UL << 10);
  GPIOB_OSPEEDR |=   (0xFFUL << 20);
  GPIOB_PUPDR   &=  ~(0xFFUL << 20);
  GPIOB_AFRH     =   (GPIOB_AFRH  & ~(0xFFFFUL << 8)) | (0xAAAA << 8);
  //
  // PB5 (OTG_HS_ULPI alternate function, DATA7)
  //
  GPIOB_MODER    =   (GPIOB_MODER  & ~(3UL  <<  10)) | (2UL  <<  10);
  GPIOB_OTYPER  &=  ~(1UL  <<  5);
  GPIOB_OSPEEDR |=   (3UL  << 10);
  GPIOB_PUPDR   &=  ~(3UL  << 10);
  GPIOB_AFRL     =   (GPIOB_AFRL  & ~(15UL <<  20)) | (10UL <<  20);
  //
  // ULPI control pins
  // PC0 (OTG_HS_ULPI alternate function, STP)
  //
  GPIOC_MODER    =   (GPIOC_MODER & ~(3UL  <<   0)) | (2UL  <<  0);
  GPIOC_OSPEEDR |=   (3UL  <<  0);
  GPIOC_AFRL     =   (GPIOC_AFRL  & ~(15UL <<   0)) | (10UL <<  0);
  //
  // PI11 (OTG_HS_ULPI alternate function, DIR)
  //
  GPIOI_MODER    =   (GPIOI_MODER & ~(3UL  <<  22)) | (2UL  << 22);
  GPIOI_OSPEEDR |=   (3UL  << 22);
  GPIOI_AFRH     =   (GPIOI_AFRH  & ~(15UL <<  12)) | (10UL << 12);
  //
  // PH4 (OTG_HS_ULPI alternate function, NXT)
  //
  GPIOH_MODER    =   (GPIOH_MODER & ~(3UL  <<   8)) | (2UL  <<  8);
  GPIOH_OSPEEDR |=   (3UL  <<  8);
  GPIOH_AFRL     =   (GPIOH_AFRL  & ~(15UL <<  16)) | (10UL << 16);
  //
  // PA5 (OTG_HS_ULPI alternate function, CLOCK)
  //
  GPIOA_MODER    =   (GPIOA_MODER & ~(3UL  <<  10)) | (2UL  << 10);
  GPIOA_OSPEEDR |=   (3UL  << 10);
  GPIOA_AFRL     =   (GPIOA_AFRL  & ~(15UL <<  20)) | (10UL << 20);
  //
  //  Enable clock for OTG_HS and OTGHS_ULPI
  //
  RCC_AHB1ENR    |=  (3uL << 25);
  USBH_OS_Delay(100);
  //
  // Reset OTG_HS clock
  //
  RCC_AHB1RSTR   |=  (1uL << 25);
  USBH_OS_Delay(100);
  RCC_AHB1RSTR   &= ~(1uL << 25);
  USBH_OS_Delay(400);
  //
  // Workaround to avoid AXI SRAM corruption (see STM32H753xI Errata sheet Rev. 2, November 2017)
  // According to ST this errata has been fixed with chip revisions X and V.
  // If you are using one of the newer chips you can remove the following line.
  //
  AXI_TARG7_FN_MOD_ISS_BM |= 1;
}

/*********************************************************************
*
*       _ISR
*
*  Function description
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
  // USBH_ConfigSupportExternalHubs (1);           // Default values: The hub module is disabled, this is done to save memory.
  // USBH_ConfigPowerOnGoodTime     (300);         // Default values: 300 ms wait time before the hosts starts communicating with the device.
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
  USBH_STM32H7_HS_Add((void*)STM32_OTG_BASE_ADDRESS);
  USBH_STM32H7_HS_SetCheckAddress(_CheckForValidDMAAddress);
  BSP_USBH_InstallISR_Ex(USB_ISR_ID, _ISR, USB_ISR_PRIO);
}

/********************************* EOF ******************************/
