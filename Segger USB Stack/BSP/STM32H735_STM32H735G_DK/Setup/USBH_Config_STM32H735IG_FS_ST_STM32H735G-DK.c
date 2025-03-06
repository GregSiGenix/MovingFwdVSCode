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
Purpose     : emUSB Host configuration file for STM32H735G-DK board.
              There different revision where the OTG_PWR_ON is 
              different. It seems that Rev A02 is different to 
              Rev B02/C02.
              Therefore a define (IS_BOARD_REV_A02)
              is available to switch between these revisions.                                  
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
#define USB_RAM_SIZE                   0x08000     // Size of memory dedicated to the stack in bytes
#define USB_ISR_PRIO                   254
#define IS_BOARD_REV_A02               0

/*********************************************************************
*
*       Defines, fixed
*
**********************************************************************
*/
//
#define STM32_OTG_BASE_ADDRESS      0x40040000uL
#if IS_BOARD_REV_A02
#define OTG_PWR_ON_PIN              (0x02)
#else
#define OTG_PWR_ON_PIN              (0x05)
#endif
/*********************************************************************
*
*       Defines, sfr
*
**********************************************************************
*/
#define GPV_BASE_ADDR        (0x51000000uL)
#define AXI_TARG7_FN_MOD   (*(volatile U32 *)(GPV_BASE_ADDR + 0x1108 + 0x1000 * 7))


/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
//
// Please make sure that this memory is located either in the AXI memory or in the FMC (external memory) 
// located memory region.
// The TCM/ICM memory region can only be accessed by the CPU and thus is not accessible by the internal 
// DMA controller of the USB controller.
//
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
  // Configure IO's
  //
  RCC->AHB4ENR |= 0
              | (1 <<  0)  // GPIOAEN: IO port A clock enable
              | (1 <<  7)  // GPIOHEN: IO port H clock enable
              ;
  //
  // PA10: USB_ID
  //
  GPIOA->MODER    =   (GPIOA->MODER  & ~(3UL  <<  20)) | (2UL  <<  20);
  GPIOA->OTYPER  |=   (1UL  <<  10);
  GPIOA->OSPEEDR |=   (3UL  <<  20);
  GPIOA->PUPDR    =   (GPIOA->PUPDR & ~(3UL  <<  20)) | (1UL << 20);
  GPIOA->AFR[1]   =   (GPIOA->AFR[1]  & ~(15UL << 8)) | (10UL << 8);
  //
  // PA11: USB_DM
  //
  GPIOA->MODER    =   (GPIOA->MODER  & ~(3UL  <<  22)) | (2UL  <<  22);
  GPIOA->OTYPER  &=  ~(1UL  <<  11);
  GPIOA->OSPEEDR |=   (3UL  <<  22);
  GPIOA->PUPDR   &=  ~(3UL  <<  22);
  GPIOA->AFR[1]   =   (GPIOA->AFR[1]  & ~(15UL << 12)) | (10UL << 12);
  //
  // PA12: USB_DP
  //
  GPIOA->MODER    =   (GPIOA->MODER  & ~(3UL  <<  24)) | (2UL  <<  24);
  GPIOA->OTYPER  &=  ~(1UL  <<  12);
  GPIOA->OSPEEDR |=   (3UL  <<  24);
  GPIOA->PUPDR   &=  ~(3UL  <<  24);
  GPIOA->AFR[1]   =   (GPIOA->AFR[1]  & ~(15UL << 16)) | (10UL << 16);
  //
  // Set PH(3|5) to output, high to turn on VBUS
  //
  GPIOH->MODER  =    (GPIOH->MODER  & ~(3UL  <<  (OTG_PWR_ON_PIN * 2))) | (1UL  <<  (OTG_PWR_ON_PIN * 2));
  GPIOH->BSRR   =    (1u << OTG_PWR_ON_PIN);
  //
  // Configure PLL3 to 48 MHz (assuming that external 8 MHz crystal is used).
  //
  RCC->CR &= ~(1uL << 28);
  Tmp = RCC->PLLCKSELR & ~((0x3Fu << 20) | 3u);
  Tmp |= ((25uL << 20) | 2u);                               // Set DIVM = 25
  RCC->PLLCKSELR = Tmp;
  USBH_OS_Delay(2);
  if (((RCC->PLLCKSELR ^ Tmp) & ((0x3Fu << 20) | 3u)) != 0) {
    //
    // Fatal: PLL already configured, can't be reconfigured, Stop.
    //
    for (;;) {}
  }
  RCC->PLLCFGR  &= ~0xF00;          // clear bits 8 to 11
  RCC->PLLCFGR  |= (0uL << 10)      // set input range 1 to 8 MHz
               |  (1uL << 23);     // PLL3 Q output enable
  RCC->PLL3DIVR &= ~0x1FFu;
  RCC->PLL3DIVR |= 191u;             // Set DIVN to 192
  RCC->PLL3DIVR &= ~(0x7FuL << 16);
  RCC->PLL3DIVR |= (3uL << 16);     // Set DIVQ to 3
  //
  // Enable PLL3
  //
  RCC->CR |= (1uL << 28);
  while ((RCC->CR & (1uL << 29)) == 0) {
  }
  //
  // Set USB clock selector to PLL3
  //
  RCC->D2CCIP2R |= (2uL << 20);
  //
  // Enable clock for OTG_HS1
  //
  RCC->AHB1ENR    |=  RCC_AHB1ENR_USB1OTGHSEN;
  USBH_OS_Delay(10);
  //
  // Reset USB clock
  //
  RCC->AHB1RSTR   |=  RCC_AHB1RSTR_USB1OTGHSRST;
  USBH_OS_Delay(10);
  RCC->AHB1RSTR   &= ~RCC_AHB1RSTR_USB1OTGHSRST;
  USBH_OS_Delay(40);
  //
  // Enable voltage level detector for transceiver
  //
  PWR->CR3 |= (1uL << 24);
  //
  // Workaround to avoid AXI SRAM corruption (see STM32H753xI Errata sheet Rev. 2, November 2017)
  //
  AXI_TARG7_FN_MOD |= 1;
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
*       _OnPortPowerControl
*/
static void _OnPortPowerControl(U32 HostControllerIndex, U8 Port, U8 PowerOn) {
  USBH_USE_PARA(HostControllerIndex);
  USBH_USE_PARA(Port);

  if (PowerOn == 1) {
    GPIOH->BSRR = (1u << OTG_PWR_ON_PIN);
  } else {
    GPIOH->BSRR = ((1u << OTG_PWR_ON_PIN) << 16);
  }
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
  RCC->AHB2ENR |= (1u << 29);                               // Enable SRAM1, where memory pool resides.
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
  BSP_USBH_InstallISR_Ex(OTG_HS_IRQn, _ISR, USB_ISR_PRIO);
}

/********************************* EOF ******************************/
