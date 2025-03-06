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
File        : USBH_Config_STM32F4xxHS_ST_STM32F429_Discovery.c
Purpose     : emUSB Host configuration file for the ST STM32F429 Discovery
              eval board
              It uses the OTG_HS controller in FS mode (use internal phy)
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
#include "USBH_HW_STM32F2xxHS.h"

/*********************************************************************
*
*       Defines, configurable
*
**********************************************************************
*/
#define STM32_OTG_BASE_ADDRESS 0x40040000UL
#define ALLOC_SIZE                 0x10000      // Size of memory dedicated to the stack in bytes

#define USB_ISR_ID    (77)
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
#define RCC_BASE_ADDR             ((unsigned int)(0x40023800))
#define RCC_AHB1RSTR              (*(volatile U32 *)(RCC_BASE_ADDR + 0x10))
#define RCC_AHB2RSTR              (*(volatile U32 *)(RCC_BASE_ADDR + 0x14))
#define RCC_AHB1ENR               (*(volatile U32 *)(RCC_BASE_ADDR + 0x30))
#define RCC_AHB2ENR               (*(volatile U32 *)(RCC_BASE_ADDR + 0x34))
#define RCC_AHB1LPENR             (*(volatile U32 *)(RCC_BASE_ADDR + 0x50))
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
#define GPIOA_BSRRL               (*(volatile U16 *)(GPIOA_BASE_ADDR + 0x18))
#define GPIOA_BSRRH               (*(volatile U16 *)(GPIOA_BASE_ADDR + 0x16))
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
#define GPIOB_BSRRL               (*(volatile U16 *)(GPIOB_BASE_ADDR + 0x18))
#define GPIOB_BSRRH               (*(volatile U16 *)(GPIOB_BASE_ADDR + 0x16))
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

#define GPIOE_BASE_ADDR           ((unsigned int)0x40021000)
#define GPIOE_MODER               (*(volatile U32 *)(GPIOE_BASE_ADDR + 0x00))
#define GPIOE_OTYPER              (*(volatile U32 *)(GPIOE_BASE_ADDR + 0x04))
#define GPIOE_OSPEEDR             (*(volatile U32 *)(GPIOE_BASE_ADDR + 0x08))
#define GPIOE_PUPDR               (*(volatile U32 *)(GPIOE_BASE_ADDR + 0x0C))
#define GPIOE_IDR                 (*(volatile U32 *)(GPIOE_BASE_ADDR + 0x10))
#define GPIOE_ODR                 (*(volatile U32 *)(GPIOE_BASE_ADDR + 0x14))
#define GPIOE_BSRRL               (*(volatile U16 *)(GPIOE_BASE_ADDR + 0x18))
#define GPIOE_BSRRH               (*(volatile U16 *)(GPIOE_BASE_ADDR + 0x16))
#define GPIOE_LCKR                (*(volatile U32 *)(GPIOE_BASE_ADDR + 0x1C))
#define GPIOE_AFRL                (*(volatile U32 *)(GPIOE_BASE_ADDR + 0x20))
#define GPIOE_AFRH                (*(volatile U32 *)(GPIOE_BASE_ADDR + 0x24))


#define GPIOH_BASE_ADDR           ((unsigned int)0x40021C00)
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

#define GPIOI_BASE_ADDR           ((unsigned int)0x40022000)
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
static U32 _aPool[((ALLOC_SIZE) / 4)];             // Memory area used by the stack

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
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       _InitUSBHw
*
*/
static void _InitUSBHw(void) {
  unsigned v;

  RCC_AHB1ENR |= 0
              | (1 <<  1)  // GPIOBEN: IO port B clock enable
              | (1 <<  2)  // GPIOCEN: IO port C clock enable
              ;
  //
  // PB12..15 (OTG_HS_ internal alternate function, OTG_HS_DP, OTG_HS_DM, OTG_HS_ID)
  //
  GPIOB_MODER    =   (GPIOB_MODER  & ~(0xFFUL << 24)) | (0xA6UL << 24);
  GPIOB_OTYPER  &=  ~(0x0FUL << 12);
  GPIOB_OSPEEDR |=   (0xFFUL << 24);
  GPIOB_PUPDR   &=  ~(0xFFUL << 24);
  GPIOB_AFRH     =   (GPIOB_AFRH  & ~(0xFF0FUL << 16)) | (0xCC0CUL << 16);
  //
  // STM32(Forum)
  // https://community.st.com/thread/21124?commentID=47701#comment-47701
  //
  // When using the OTG_HS peripheral with the internal FS PHY in combination with the
  // WFI instruction to enter SLEEP mode (workaround for a silicon bug).
  // It is necessary to perform the following operations on the RCC_AHB1LPENR register:
  // 1) Clear bit  OTGHSULPILPEN in register  AHB1LPENR
  // 2) Set   bit  OTGHSLPEN     in register  AHB1LPENR (already set by default)
  //
  //
  RCC_AHB1LPENR &= ~(1UL << 30);
  //
  //  Enable clock for OTG_HS and OTGHS_ULPI
  //
  RCC_AHB1ENR    |=  (1UL << 29);
  USBH_OS_Delay(100);
  //
  // Reset OTGHS clock
  RCC_AHB1RSTR   |=  (1UL << 29);
  USBH_OS_Delay(100);
  RCC_AHB1RSTR   &= ~(1UL << 29);
  USBH_OS_Delay(400);
  //
  // Set PC4 (USB_PWR) as output.
  //
  GPIOC_BSRR  = (0x1uL << 4);
  v           = GPIOC_MODER;
  v          &= ~(0x3uL << (2 * 4));
  v          |=  (0x1uL << (2 * 4));
  GPIOC_MODER = v;
  v           = GPIOC_ODR;
  v          &= ~(0x3uL << (2 * 4));
  v          |=  (0x0uL << (2 * 4));
  GPIOC_ODR = v;
}


/*********************************************************************
*
*       _OnPortPowerControl
*/
static void _OnPortPowerControl(U32 HostControllerIndex, U8 Port, U8 PowerOn) {
  USBH_USE_PARA(HostControllerIndex);
  USBH_USE_PARA(Port);
  if (PowerOn) {
    GPIOC_BSRR = (0x1uL << (4 + 16));
  } else {
    GPIOC_BSRR  = (0x1uL << 4);    // Set pin high to disable VBUS.
  }
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
  // CCM data RAM is not allowed.
  // This RAM can only be accessed by the CPU
  //
  if ((U32)p >= 0x10000000 && (U32)p < 0x20000000) {
    return 1;
  }
  return 0;
}

/*********************************************************************
*
*       USBH_X_Config
*
*  Function description
*/
void USBH_X_Config(void) {
  USBH_AssignMemory((void *)&_aPool[0], ALLOC_SIZE);    // Assigning memory should be the first thing
  // USBH_ConfigSupportExternalHubs (0);           // Default values: The hub module is disabled, this is done to save memory.
  // USBH_ConfigPowerOnGoodTime     (300);         // Default values: 300 ms wait time before the host starts communicating with a device.
  //
  // Define log and warn filter
  // Note: The terminal I/O emulation affects the timing
  // of your communication, since the debugger stops the target
  // for every terminal I/O unless you use RTT!
  //
  USBH_ConfigMsgFilter(USBH_WARN_FILTER_SET_ALL, 0, NULL);           // Output all warnings.
  USBH_ConfigMsgFilter(USBH_LOG_FILTER_SET, sizeof(_LogCategories), _LogCategories);
  _InitUSBHw();
  USBH_STM32F2_HS_AddEx((void*)STM32_OTG_BASE_ADDRESS, 1);
  USBH_STM32F2_HS_SetCheckAddress(_CheckForValidDMAAddress);
  //
  //  Please uncomment this function when using OTG functionality.
  //  Otherwise the VBUS power-on will be permanently on and will cause
  //  OTG to detect a session where no session is available.
  //
  // USBH_SetOnSetPortPower(_OnPortPowerControl);                 // This function sets a callback which allows to control VBUS-Power of a USB port.
  //
  // Delay is necessary before VBUS is powered on
  // if a device is already connected during start-up.
  //
  USBH_OS_Delay(50);
  _OnPortPowerControl(0, 0, 1);                                // Enable power on USB port
  BSP_USBH_InstallISR_Ex(USB_ISR_ID, _ISR, USB_ISR_PRIO);
}
/*************************** End of file ****************************/
