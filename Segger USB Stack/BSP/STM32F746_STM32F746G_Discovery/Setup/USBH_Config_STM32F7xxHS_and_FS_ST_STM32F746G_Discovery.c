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
File        : USBH_Config_STM32F7xxHS_and_FS_ST_STM32F746G_Discovery.c
Purpose     : emUSB Host configuration file for the
              ST STM32F746G discovery eval board
              for both controllers (full-speed and high-speed)
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
#include "USBH_HW_STM32F7xxHS.h"
#include "USBH_HW_STM32F7xxFS.h"
#include "stm32f7xx.h"  // For the cache handling functions.

/*********************************************************************
*
*       Defines, configurable
*
**********************************************************************
*/
#define ALLOC_SIZE             0x9000     // Size of memory dedicated to the stack in bytes

#define USB_HS_ISR_ID          (77)
#define USB_HS_ISR_PRIO        254
#define STM32_HS_BASE_ADDRESS  0x40040000uL

#define USB_FS_ISR_ID         (67)
#define USB_FS_ISR_PRIO       254
#define STM32_FS_BASE_ADDRESS 0x50000000uL

//
// RCC
//
#define RCC_BASE_ADDR             ((unsigned int)(0x40023800))
#define RCC_CR                    (*(volatile unsigned int*)(RCC_BASE_ADDR + 0x00))
#define RCC_AHB1RSTR              (*(volatile unsigned int*)(RCC_BASE_ADDR + 0x10))
#define RCC_AHB2RSTR              (*(volatile unsigned int*)(RCC_BASE_ADDR + 0x14))
#define RCC_AHB1ENR               (*(volatile unsigned int*)(RCC_BASE_ADDR + 0x30))
#define RCC_AHB2ENR               (*(volatile unsigned int*)(RCC_BASE_ADDR + 0x34))
#define RCC_PLLSAICFGR            (*(volatile unsigned int*)(RCC_BASE_ADDR + 0x88))
#define RCC_DCKCFGR2              (*(volatile unsigned int*)(RCC_BASE_ADDR + 0x90))

//
// GPIO
//
#define GPIOA_BASE_ADDR           ((unsigned int)0x40020000)
#define GPIOA_MODER               (*(volatile U32 *)(GPIOA_BASE_ADDR + 0x00))
#define GPIOA_OTYPER              (*(volatile U32 *)(GPIOA_BASE_ADDR + 0x04))
#define GPIOA_OSPEEDR             (*(volatile U32 *)(GPIOA_BASE_ADDR + 0x08))
#define GPIOA_PUPDR               (*(volatile U32 *)(GPIOA_BASE_ADDR + 0x0C))
#define GPIOA_IDR                 (*(volatile U32 *)(GPIOA_BASE_ADDR + 0x10))
#define GPIOA_ODR                 (*(volatile U32 *)(GPIOA_BASE_ADDR + 0x14))
#define GPIOA_BSRR                (*(volatile U32 *)(GPIOA_BASE_ADDR + 0x18))
#define GPIOA_LCKR                (*(volatile U32 *)(GPIOA_BASE_ADDR + 0x1C))
#define GPIOA_AFRL                (*(volatile U32 *)(GPIOA_BASE_ADDR + 0x20))
#define GPIOA_AFRH                (*(volatile U32 *)(GPIOA_BASE_ADDR + 0x24))

#define GPIOB_BASE_ADDR           ((unsigned int)0x40020400)
#define GPIOB_MODER               (*(volatile U32 *)(GPIOB_BASE_ADDR + 0x00))
#define GPIOB_OTYPER              (*(volatile U32 *)(GPIOB_BASE_ADDR + 0x04))
#define GPIOB_OSPEEDR             (*(volatile U32 *)(GPIOB_BASE_ADDR + 0x08))
#define GPIOB_PUPDR               (*(volatile U32 *)(GPIOB_BASE_ADDR + 0x0C))
#define GPIOB_IDR                 (*(volatile U32 *)(GPIOB_BASE_ADDR + 0x10))
#define GPIOB_ODR                 (*(volatile U32 *)(GPIOB_BASE_ADDR + 0x14))
#define GPIOB_BSRR                (*(volatile U32 *)(GPIOB_BASE_ADDR + 0x18))
#define GPIOB_LCKR                (*(volatile U32 *)(GPIOB_BASE_ADDR + 0x1C))
#define GPIOB_AFRL                (*(volatile U32 *)(GPIOB_BASE_ADDR + 0x20))
#define GPIOB_AFRH                (*(volatile U32 *)(GPIOB_BASE_ADDR + 0x24))

#define GPIOC_BASE_ADDR           ((unsigned int)0x40020800)
#define GPIOC_MODER               (*(volatile U32 *)(GPIOC_BASE_ADDR + 0x00))
#define GPIOC_OTYPER              (*(volatile U32 *)(GPIOC_BASE_ADDR + 0x04))
#define GPIOC_OSPEEDR             (*(volatile U32 *)(GPIOC_BASE_ADDR + 0x08))
#define GPIOC_PUPDR               (*(volatile U32 *)(GPIOC_BASE_ADDR + 0x0C))
#define GPIOC_IDR                 (*(volatile U32 *)(GPIOC_BASE_ADDR + 0x10))
#define GPIOC_ODR                 (*(volatile U32 *)(GPIOC_BASE_ADDR + 0x14))
#define GPIOC_BSRR                (*(volatile U32 *)(GPIOC_BASE_ADDR + 0x18))
#define GPIOC_LCKR                (*(volatile U32 *)(GPIOC_BASE_ADDR + 0x1C))
#define GPIOC_AFRL                (*(volatile U32 *)(GPIOC_BASE_ADDR + 0x20))
#define GPIOC_AFRH                (*(volatile U32 *)(GPIOC_BASE_ADDR + 0x24))

#define GPIOD_BASE_ADDR           ((unsigned int)0x40020C00)
#define GPIOD_MODER               (*(volatile unsigned int*)(GPIOD_BASE_ADDR + 0x00))
#define GPIOD_BSRR                (*(volatile unsigned int*)(GPIOD_BASE_ADDR + 0x18))
#define GPIOD_AFRL                (*(volatile unsigned int*)(GPIOD_BASE_ADDR + 0x20))
#define GPIOD_AFRH                (*(volatile unsigned int*)(GPIOD_BASE_ADDR + 0x24))

#define GPIOH_BASE_ADDR           ((unsigned int)0x40021C00)
#define GPIOH_MODER               (*(volatile U32 *)(GPIOH_BASE_ADDR + 0x00))
#define GPIOH_OTYPER              (*(volatile U32 *)(GPIOH_BASE_ADDR + 0x04))
#define GPIOH_OSPEEDR             (*(volatile U32 *)(GPIOH_BASE_ADDR + 0x08))
#define GPIOH_PUPDR               (*(volatile U32 *)(GPIOH_BASE_ADDR + 0x0C))
#define GPIOH_IDR                 (*(volatile U32 *)(GPIOH_BASE_ADDR + 0x10))
#define GPIOH_ODR                 (*(volatile U32 *)(GPIOH_BASE_ADDR + 0x14))
#define GPIOH_BSRR                (*(volatile U32 *)(GPIOH_BASE_ADDR + 0x18))
#define GPIOH_LCKR                (*(volatile U32 *)(GPIOH_BASE_ADDR + 0x1C))
#define GPIOH_AFRL                (*(volatile U32 *)(GPIOH_BASE_ADDR + 0x20))
#define GPIOH_AFRH                (*(volatile U32 *)(GPIOH_BASE_ADDR + 0x24))

#define OTG_FS_GOTGCTL            (*(volatile unsigned int*)(0x50000000))

#define OTG_FS_GOTTGCTL_AVALOVAL  (1UL << 5)  // A-peripheral session valid override value
#define OTG_FS_GOTTGCTL_AVALOEN   (1UL << 4)  // A-peripheral session valid override enable
#define OTG_FS_GOTTGCTL_VBVALOVAL (1UL << 3)  // VBUS valid override value.
#define OTG_FS_GOTTGCTL_VBVALOEN  (1UL << 2)  // VBUS valid override enable.

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static U32 _aPool [ALLOC_SIZE / 4];
static U32 _HS_Index;
static U32 _FS_Index;

static void _CleanDCache(void *p, U32 NumBytes) {
  SCB_CleanDCache_by_Addr((uint32_t *)p, NumBytes);
}
static void _InvalidateDCache(void *p, U32 NumBytes) {
  SCB_InvalidateDCache_by_Addr((uint32_t *)p, NumBytes);
}

static const SEGGER_CACHE_CONFIG _CacheConfig = {
  32,                            // CacheLineSize of CPU
  NULL,                          // pfDMB
  _CleanDCache,                  // pfClean
  _InvalidateDCache              // pfInvalidate
};

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
*       _HS_InitUSBHw
*
*/
static void _HS_InitUSBHw(void) {
  RCC_AHB1ENR |= 0
              | (1 <<  7)  // GPIOHEN: IO port H clock enable
              | (1 <<  2)  // GPIOCEN: IO port C clock enable
              | (1 <<  1)  // GPIOBEN: IO port B clock enable
              | (1 <<  0)  // GPIOAEN: IO port A clock enable
              ;
  //
  // ULPI data pins
  // PA3 (OTG_HS_ULPI alternate function, DATA0)
  //
  GPIOA_MODER    =   (GPIOA_MODER  & ~(3UL  <<  6)) | (2UL  <<  6);
  GPIOA_OTYPER  &=  ~(1UL  <<  3);
  GPIOA_OSPEEDR |=   (3UL  <<  6);
  GPIOA_PUPDR   &=  ~(3UL  <<  6);
  GPIOA_AFRL     =   (GPIOA_AFRL  & ~(15UL << 12)) | (10UL << 12);
  //
  //PB0, PB1 (OTG_HS_ULPI alternate function, DATA1, DATA2)
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
  // PC2 (OTG_HS_ULPI alternate function, DIR)
  //
  GPIOC_MODER    =   (GPIOC_MODER & ~(3UL  <<   4)) | (2UL  <<  4);
  GPIOC_OSPEEDR |=   (3UL  <<  4);
  GPIOC_AFRL     =   (GPIOC_AFRL  & ~(15UL <<   8)) | (10UL <<  8);
  //
  // PH4 (OTG_HS_ULPI alternate function, NXT)
  GPIOH_MODER    =   (GPIOH_MODER & ~(3UL  <<   8)) | (2UL  <<  8);
  GPIOH_OSPEEDR |=   (3UL  <<  8);
  GPIOH_AFRL     =   (GPIOH_AFRL  & ~(15UL <<  16)) | (10UL << 16);
  //
  // PA5 (OTG_HS_ULPI alternate function, CLOCK)
  GPIOA_MODER    =   (GPIOA_MODER & ~(3UL  <<  10)) | (2UL  << 10);
  GPIOA_OSPEEDR |=   (3UL  << 10);
  GPIOA_AFRL     =   (GPIOA_AFRL  & ~(15UL <<  20)) | (10UL << 20);
  //
  //  Enable clock for OTG_HS and OTGHS_ULPI
  //
  RCC_AHB1ENR    |=  (3UL << 29);
  USBH_OS_Delay(10);
  //
  // Reset OTGHS clock
  RCC_AHB1RSTR   |=  (1UL << 29);
  USBH_OS_Delay(10);
  RCC_AHB1RSTR   &= ~(1UL << 29);
  USBH_OS_Delay(10);
}

/*********************************************************************
*
*       _HS_ISR
*
*  Function description
*/
static void _HS_ISR(void) {
  USBH_ServiceISR(_HS_Index);
}

/*********************************************************************
*
*       _InitUSBHw
*
*/
static void _FS_InitUSBHw(void) {
  U32 v;

  //
  // Configure the 48 Mhz Clock, we assume to have a 25 MHz crystal + PLLM Divider of 25
  // With this we can configure the PLLSAI to 192 MHz in order to get a proper 48 Mhz out of it.
  //
  RCC_CR &= ~(1UL << 28);      // Disable PLLSAI
  while ((RCC_CR & (1UL << 29)) != 0);   // Wait until the PLLSAI is disabled
  //
  // Update the PLLSAI Configuration register:
  // Use N = 192; P = 4, which result in Freq[in] * N / P = 1 MHz * 192 / 4 = 48 MHz
  // which is the clock that we need.
  // Q and R are not touched these value are needed for other peripherals.
  //
  v = RCC_PLLSAICFGR;
  v &= ~((0x1FFUL << 6) | (0x3UL << 16));  // Clear bits
  v |= (192 << 6) | (1 << 16);
  RCC_PLLSAICFGR = v;
  RCC_CR |= (1UL << 28);      // Enable PLLSAI
  while ((RCC_CR & (1UL << 29)) == 0);   // Wait until the PLLSAI is ready
  //
  // Use PLLSAI as CLK48 clock source
  //
  RCC_DCKCFGR2 |= (1U << 27);
  //
  // Set the dedicated port pins
  //
  RCC_AHB1ENR |= 0
               | (1 <<  3)  // GPIOCEN: IO port D clock enable
               | (1 <<  0)  // GPIOAEN: IO port A clock enable
               ;
  RCC_AHB2ENR |= 0
              | (1 <<  7)  // OTGFSEN: Enable USB OTG FS clock enable
              ;
  //
  // Set PA10 (OTG_FS_ID) as alternate function
  //
  v           = GPIOA_MODER;
  v          &= ~(0x3uL << (2 * 10));
  v          |=  (0x2uL << (2 * 10));
  GPIOA_MODER = v;
  v           = GPIOA_AFRH;
  v          &= ~(0xFuL << (4 * 2));
  v          |=  (0xAuL << (4 * 2));
  GPIOA_AFRH  = v;
  //
  // Set PA11 (OTG_FS_DM) as alternate function
  //
  v           = GPIOA_MODER;
  v          &= ~(0x3uL << (2 * 11));
  v          |=  (0x2uL << (2 * 11));
  GPIOA_MODER = v;
  v           = GPIOA_AFRH;
  v          &= ~(0xFuL << (4 * 3));
  v          |=  (0xAuL << (4 * 3));
  GPIOA_AFRH  = v;
  //
  // Set PA12 (OTG_FS_DP) as alternate function
  //
  v           = GPIOA_MODER;
  v          &= ~(0x3uL << (2 * 12));
  v          |=  (0x2uL << (2 * 12));
  GPIOA_MODER = v;
  v           = GPIOA_AFRH;
  v          &= ~(0xFuL << (4 * 4));
  v          |=  (0xAuL << (4 * 4));
  GPIOA_AFRH  = v;
  //
  // Set PD5 (OTG_FS_PowerSwitchOn) as general purpose output mode
  //
  v           = GPIOD_MODER;
  v          &= ~(0x3uL << (2 * 5));
  v          |=  (0x1uL << (2 * 5));
  GPIOD_BSRR  = ((1ul << 5) <<  0); // Set pin high to disable VBUS.
  GPIOD_MODER = v;
  OTG_FS_GOTGCTL |= 0
                 |  OTG_FS_GOTTGCTL_AVALOVAL
                 |  OTG_FS_GOTTGCTL_AVALOEN
                 |  OTG_FS_GOTTGCTL_VBVALOVAL
                 |  OTG_FS_GOTTGCTL_VBVALOEN;
}

/*********************************************************************
*
*       _FS_ISR
*
*  Function description
*/
static void _FS_ISR(void) {
  USBH_ServiceISR(_FS_Index);
}

/*********************************************************************
*
*       _OnPortPowerControl
*/
static void _OnPortPowerControl(U32 HostControllerIndex, U8 Port, U8 PowerOn) {
  USBH_USE_PARA(HostControllerIndex);
  USBH_USE_PARA(Port);
  if (PowerOn) {
    GPIOD_BSRR  = ((1ul << 5) << 16); // Set pin low to enable VBUS.
  } else {
    GPIOD_BSRR  = ((1ul << 5) <<  0); // Set pin high to disable VBUS.
  }
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
  USBH_AssignMemory(&_aPool[0], ALLOC_SIZE);    // Assigning memory should be the first thing
  // USBH_ConfigSupportExternalHubs (1);           // Default values: The hub module is disabled, this is done to save memory.
  // USBH_ConfigPowerOnGoodTime     (300);         // Default values: 300 ms wait time before the host starts communicating with a device.
  //
  // Define log and warn filter
  // Note: The terminal I/O emulation affects the timing
  // of your communication, since the debugger stops the target
  // for every terminal I/O unless you use RTT!
  //
  USBH_ConfigMsgFilter(USBH_WARN_FILTER_SET_ALL, 0, NULL);           // Output all warnings.
  USBH_ConfigMsgFilter(USBH_LOG_FILTER_SET, sizeof(_LogCategories), _LogCategories);
  _HS_InitUSBHw();
  USBH_SetCacheConfig(&_CacheConfig, sizeof(_CacheConfig));  // Set cache configuration for USBH stack.
  _HS_Index = USBH_STM32F7_HS_Add((void*)STM32_HS_BASE_ADDRESS);
  //USBH_ConfigTransferBufferSize(0, 0x4000);
  BSP_USBH_InstallISR_Ex(USB_HS_ISR_ID, _HS_ISR, USB_HS_ISR_PRIO);
  //
  // Initialize full speed controller
  //
  _FS_InitUSBHw();
  _FS_Index = USBH_STM32F7_FS_Add((void*)STM32_FS_BASE_ADDRESS);
  //
  //  Please uncomment this function when using OTG functionality on the FS port.
  //  Otherwise the VBUS power-on will be permanently on and will cause
  //  OTG to detect a session where no session is available.
  //
  // USBH_SetOnSetPortPower(_OnPortPowerControl);                 // This function sets a callback which allows to control VBUS-Power of a USB port.
  _OnPortPowerControl(0, 0, 1);                                // Enable power on USB port
  BSP_USBH_InstallISR_Ex(USB_FS_ISR_ID, _FS_ISR, USB_FS_ISR_PRIO);

}
/*************************** End of file ****************************/
