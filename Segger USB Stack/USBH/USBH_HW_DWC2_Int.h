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
File        : USBH_HW_DWC2_Int.h
Purpose     : Header for the Synopsys DWC2 FullSpeed emUSB Host driver
-------------------------- END-OF-HEADER -----------------------------
*/

#ifndef USBH_HW_DWC2_H_
#define USBH_HW_DWC2_H_

#include "USBH_Int.h"

#if defined(__cplusplus)
  extern "C" {                 // Make sure we have C-declarations in C++ programs
#endif

#ifndef   USBH_DWC2_CHECK_CHANNEL_INTERVAL
  #define USBH_DWC2_CHECK_CHANNEL_INTERVAL  100  // Check every 100ms whether everything is ok.
#endif
#ifndef   USBH_DWC2_NUM_RETRIES
  #define USBH_DWC2_NUM_RETRIES               3u // Number of retires of failed transmissions.
#endif

#define DWC2_INVALID_CHANNEL              0xFFu
#define USBH_DWC2_HCCHANNEL_MAX_CHANNELS  24
#define USBH_DWC2_MAX_USB_ADDRESS         0x7Fu        // Last USB address that can be used is 0x7f (127)

/**
 * struct dwc2_hw_params - Autodetected parameters.
 *
 * These parameters are the various parameters read from hardware
 * registers during initialization. They typically contain the best
 * supported or maximum value that can be configured in the
 * corresponding dwc2_core_params value.
 *
 * The values that are not in dwc2_core_params are documented below.
 *
 * @op_mode             Mode of Operation
 *                       0 - HNP- and SRP-Capable OTG (Host & Device)
 *                       1 - SRP-Capable OTG (Host & Device)
 *                       2 - Non-HNP and Non-SRP Capable OTG (Host & Device)
 *                       3 - SRP-Capable Device
 *                       4 - Non-OTG Device
 *                       5 - SRP-Capable Host
 *                       6 - Non-OTG Host
 * @arch                Architecture
 *                       0 - Slave only
 *                       1 - External DMA
 *                       2 - Internal DMA
 * @power_optimized     Are power optimizations enabled?
 * @num_dev_ep          Number of device endpoints available
 * @num_dev_perio_in_ep Number of device periodic IN endpoints
 *                      available
 * @dev_token_q_depth   Device Mode IN Token Sequence Learning Queue
 *                      Depth
 *                       0 to 30
 * @host_perio_tx_q_depth
 *                      Host Mode Periodic Request Queue Depth
 *                       2, 4 or 8
 * @nperio_tx_q_depth
 *                      Non-Periodic Request Queue Depth
 *                       2, 4 or 8
 * @hs_phy_type         High-speed PHY interface type
 *                       0 - High-speed interface not supported
 *                       1 - UTMI+
 *                       2 - ULPI
 *                       3 - UTMI+ and ULPI
 * @fs_phy_type         Full-speed PHY interface type
 *                       0 - Full speed interface not supported
 *                       1 - Dedicated full speed interface
 *                       2 - FS pins shared with UTMI+ pins
 *                       3 - FS pins shared with ULPI pins
 * @total_fifo_size:    Total internal RAM for FIFOs (bytes)
 * @utmi_phy_data_width UTMI+ PHY data width
 *                       0 - 8 bits
 *                       1 - 16 bits
 *                       2 - 8 or 16 bits
 * @snpsid:             Value from SNPSID register
 * @dev_ep_dirs:        Direction of device endpoints (GHWCFG1)
 */
typedef struct {
  unsigned op_mode:3;
  unsigned arch:2;
  unsigned dma_desc_enable:1;
  unsigned dma_desc_fs_enable:1;
  unsigned enable_dynamic_fifo:1;
  unsigned en_multiple_tx_fifo:1;
  unsigned host_rx_fifo_size:16;
  unsigned host_nperio_tx_fifo_size:16;
  unsigned dev_nperio_tx_fifo_size:16;
  unsigned host_perio_tx_fifo_size:16;
  unsigned nperio_tx_q_depth:3;
  unsigned host_perio_tx_q_depth:3;
  unsigned dev_token_q_depth:5;
  unsigned max_transfer_size:26;
  unsigned max_packet_count:11;
  unsigned host_channels:5;
  unsigned hs_phy_type:2;
  unsigned fs_phy_type:2;
  unsigned i2c_enable:1;
  unsigned num_dev_ep:4;
  unsigned num_dev_perio_in_ep:4;
  unsigned total_fifo_size:16;
  unsigned power_optimized:1;
  unsigned utmi_phy_data_width:2;
  U32 snpsid;
  U32 dev_ep_dirs;
} DWC2_HW_PARAMS;

typedef struct {
  volatile U32  HCCHAR;      // DWC2 host channel characteristics register
  volatile U32  HCSPLIT;
  volatile U32  HCINT;       // DWC2 host channel interrupt register
  volatile U32  HCINTMSK;    // DWC2 host channel interrupt mask register
  volatile U32  HCTSIZ;      // DWC2 host channel transfer size register
  volatile U32  HCDMA;       // OTG_HS host channel DMA address
  volatile U32  aReserved[2];
} USBH_DWC2_HCCHANNEL;

typedef struct {
  volatile U32        GOTGCTL;
  volatile U32        GOTGINT;
  volatile U32        GAHBCFG;
  volatile U32        GUSBCFG;
  volatile U32        GRSTCTL;
  volatile U32        GINTSTS;
  volatile U32        GINTMSK;
  volatile U32        GRXSTSR;
  volatile U32        GRXSTSP;
  volatile U32        GRXFSIZ;
  volatile U32        GNPTXFSIZ;
  volatile U32        GNPTXSTS;
  volatile U32        GI2CCTL;
  volatile U32        GPVNDCTL;
  volatile U32        GCCFG;
  volatile U32        CID;
  volatile U32        GSNPSID;
  volatile U32        GHWCFG1;
  volatile U32        GHWCFG2;
  volatile U32        GHWCFG3;
  volatile U32        GHWCFG4;
  volatile U32        GLPMCFG;
  volatile U32        GPWRDN;
  volatile U32        GDFIFOCFG;
  volatile U32        ADPCTL;
  volatile U32        aReserved0[0x27];
  volatile U32        HPTXFSIZ;
  volatile U32        aReserved2[0xbf];
  volatile U32        HCFG;                // DWC2 host configuration register
  volatile U32        HFIR;                // DWC2 Host frame interval register
  volatile U32        HFNUM;               // DWC2 host frame number/frame time remaining register
  volatile U32        aReserved3[1];
  volatile U32        HPTXSTS;             // Host periodic transmit FIFO/queue Status register
  volatile U32        HAINT;               // DWC2 Host all channels interrupt register
  volatile U32        HAINTMSK;            // DWC2 host all channels interrupt mask register
  volatile U32        aReserved4[0x09];
  volatile U32        HPRT;                // DWC2 host port control and Status register
  volatile U32        aReserved5[0x2f];
  USBH_DWC2_HCCHANNEL aHChannel[USBH_DWC2_HCCHANNEL_MAX_CHANNELS];
  volatile U32        aReserved6[0x180];   // DWC2 USB Device registers
  volatile U32        PCGCCTL;             // DWC2 power and clock gating control register
} USBH_DWC2_HWREGS;

#define DWC2_FIFO_OFF    (0x1000u / sizeof(U32))

#define START_OF_FRAME_INT   (1UL <<  3) // Start Of Frame
#define HOST_RXFLVL          (1UL <<  4) // RxFIFO non-empty
#define HOST_NPTXFE          (1UL <<  5) // Non-periodic TxFIFO empty
#define HOST_IPXFR           (1UL << 21) // Incomplete periodic transfer mask
#define HOST_PORT_INT        (1UL << 24) // USB host port
#define HOST_CHANNEL_INT     (1UL << 25) // Host channel
#define HOST_PTXFE           (1UL << 26) // Periodic TxFIFO empty
#define HOST_DISC_INT        (1UL << 29) // Disconnect detected

#define HCCHAR_CHENA         (1UL << 31) // Channel enable
#define HCCHAR_CHDIS         (1UL << 30) // Channel disable
#define HCCHAR_ODDFRM        (1UL << 29) // Odd frame

#define CHANNEL_DTERR        (1UL << 10) // Data toggle error
#define CHANNEL_FRMOR        (1UL <<  9) // Frame overrun
#define CHANNEL_BBERR        (1UL <<  8) // Babble error
#define CHANNEL_TXERR        (1UL <<  7) // Transaction error Indicates one of the following errors occurred on the USB.
                                         // CRC check failure
                                         // Timeout
                                         // Bit stuff error
                                         // False EOP
#define CHANNEL_NYET         (1UL <<  6) // NYET response received/transmitted interrupt
#define CHANNEL_ACK          (1UL <<  5) // ACK response received/transmitted interrupt
#define CHANNEL_NAK          (1UL <<  4) // NAK response received interrupt
#define CHANNEL_STALL        (1UL <<  3) // STALL response received interrupt
#define CHANNEL_AHBERR       (1UL <<  2) // AHB error interrupt
#define CHANNEL_CHH          (1UL <<  1) // Channel halted
                                         // Indicates the transfer completed abnormally either because of any USB transaction error or in
                                         // response to disable request by the application.
#define CHANNEL_XFRC         (1UL <<  0) // Transfer completed Transfer completed normally without any errors.

#if USBH_DWC2_USE_DMA > 0
  #define CHANNEL_MASK            0x7FFu // all interrupt bits
#else
  #define CHANNEL_MASK            0x7BBu // all interrupt bits
#endif

#define DATA_PID_DATA0               0u
#define DATA_PID_DATA1               2u
#define DATA_PID_DATA2               1u
#define DATA_PID_MDATA               3u
#define DATA_PID_SETUP               3u

#define STATUS_IN_PACKET_RECEIVED    (2) // IN data packet received
#define STATUS_XFER_COMP             (3) // IN transfer completed (triggers an interrupt)
#define STATUS_DATA_TOGGLE_ERROR     (5) // Data toggle error (triggers an interrupt)
#define STATUS_CHANNEL_HALTED        (7) // Channel halted (triggers an interrupt)

#define SPLIT_ENABLE                 (1uL << 31)
#define SPLIT_XACTPOS_ALL            (3uL << 14)
#define SPLIT_COMPLETE               (1uL << 16)

#define PCKCNT_FROM_HCTSIZ(x)        (((x) >> 19) & 0x3FFu)
#define XFRSIZ_FROM_HCTSIZ(x)        ((x) & 0x7FFFFu)

#define EP_VALID(pEP)                                      USBH_ASSERT_MAGIC(pEP, USBH_DWC2_EP_INFO)
#define GET_EPINFO_FROM_ENTRY_DWC2(pListEntry)             STRUCT_BASE_POINTER(pListEntry, USBH_DWC2_EP_INFO, ListEntry)
#define USBH_DWC2_INST_MAGIC                               FOUR_CHAR_ULONG('D','W','C','2')
#define USBH_DWC2_EP_INFO_MAGIC                            FOUR_CHAR_ULONG('D','W','E','P')

#define USBH_DWC2_IS_DEV_VALID(pInst)                      USBH_ASSERT(USBH_IS_PTR_VALID(pInst, USBH_DWC2_INST))

typedef struct {
  USBH_BOOL                    InUse;
  U8                           EndpointAddress;
  U8                           ErrorCount;
  U8                           TransferDone;
  struct _USBH_DWC2_EP_INFO  * pEPInfo;
  USBH_DWC2_HCCHANNEL        * pHWChannel;
  U32                          NumBytes2Transfer;    // Is decremented during transfer.
  U32                          NumBytesTransferred;  // Is incremented during transfer.
  U32                          NumBytesPushed;       // For IN EPs it means 'NumBytesPopped'
  U32                          NumBytesTotal;        // Fix during transfer.
  U8                           ToBePushed;
  USBH_BOOL                    TimerInUse;
#if USBH_DWC2_SUPPORT_SPLIT_TRANSACTIONS
  I8                           UseSplitTransactions;
  U8                           NYETCount;
#endif
  U8                           Channel;
  USBH_STATUS                  Status;
  U8                         * pBuffer;
  USBH_TIMER                   IntervalTimer;
} USBH_DWC2_CHANNEL_INFO;

typedef struct { // The global driver object. The object is cleared in the function USBH_HostInit!
  USBH_DWC2_HWREGS                * pHWReg;                // Register base address
  volatile U32                    * pFifoRegBase;
  USBH_HOST_CONTROLLER            * pHostController;
  USBH_ROOT_HUB_NOTIFICATION_FUNC * pfUbdRootHubNotification;
  void                            * pRootHubNotificationContext;
  USBH_TIMER                        ChannelCheckTimer;
  U32                               UsedChannelMask;
#if USBH_DWC2_USE_DMA == 0
  U32                               ReStartChannelMask;     // Channels to be restarted after received a NAK
#endif
  U8                                PhyType;
  USBH_BOOL                         DisconnectDetect;
  I16                               DICnt;
  U8                                ResetDelayCount;
#if USBH_DWC2_SUPPORT_SPLIT_TRANSACTIONS
  U8                                StartSplitDelay;        // Delay between 'Start Splits'
  U8                                LastChannelStarted;
  U32                               StartChannelMask;       // Channels scheduled for a start split transaction.
  U32                               CompleteChannelMask;    // Channels scheduled for a complete split transaction.
  U32                               SOFNotUsedCount;
#endif
  U32                               MaxTransferSize;
  USBH_DWC2_CHANNEL_INFO            aChannelInfo[DWC2_NUM_CHANNELS];
#if USBH_DEBUG > 1
  U32                               Magic;
  DWC2_HW_PARAMS                    HWParams;
#endif
} USBH_DWC2_INST;

typedef struct _USBH_DWC2_EP_INFO {
  U8                                EndpointType;
  U8                                DeviceAddress;
  U8                                EndpointAddress;
  U8                                NextDataPid;
  U16                               MaxPacketSize;
  U16                               IntervalTime;   // in ms
  USBH_SPEED                        Speed;
  USBH_EP0_PHASE                    Phase;          // Control EP only
  U8                                Channel;
  U8                                Aborted;
  USBH_DWC2_INST                  * pInst;
  U8                              * pBuffer;
  U32                               BuffSize;
  I8                                UseReadBuff;
  USBH_BOOL                         ReleaseInProgress;
  //
  // Used for ISO EPs
  //
  U16                               BuffReadySize[2];
  I8                                BuffReadyList[2];
  I8                                BuffWaitList[2];      // Queue of buffers to be processed by the application:
                                                          // IN: Must be acked, OUT: Must be filled.
  I8                                BuffBusy;             // Indicates which buffer is currently used for data transfer:
                                                          // 1: First buffer, 2: Second buffer, 0: None (Idle)
  I8                                FirstTimeData;
#if USBH_DWC2_USE_DMA == 0
  U32                               aSetup[2];      // Control EP only (U32 for alignment)
#endif
  USBH_URB                        * pPendingUrb;
  USBH_RELEASE_EP_COMPLETION_FUNC * pfOnReleaseCompletion;
  void                            * pReleaseContext;
  USBH_TIMER                        RemovalTimer;
#if (USBH_DEBUG > 1)
  U32                               Magic;
#endif
} USBH_DWC2_EP_INFO;


static USBH_DWC2_CHANNEL_INFO  * _DWC2_CHANNEL_Allocate     (USBH_DWC2_INST * pInst, USBH_DWC2_EP_INFO * pEP);
static void                      _DWC2_CHANNEL_StartTransfer(USBH_DWC2_INST * pInst, USBH_DWC2_CHANNEL_INFO * pChannelInfo);
static void                      _DWC2_CHANNEL_DeAllocate   (USBH_DWC2_INST * pInst, USBH_DWC2_CHANNEL_INFO * pChannel);
static void                      _DWC2_CompleteUrb          (USBH_DWC2_EP_INFO * pEPInfo, USBH_STATUS Status);
static void                      _DWC2_DisableInterrupts    (USBH_DWC2_INST * pInst);
static void                      _DWC2_EnableInterrupts     (USBH_DWC2_INST * pInst);
#ifdef USBH_DWC2_RECEIVE_FIFO_SIZE
static void                      _DWC2_ConfigureFIFO        (const USBH_DWC2_INST * pInst);
#endif
#if USBH_SUPPORT_ISO_TRANSFER
static void                      _DWC2_StartISO(USBH_DWC2_INST * pInst, USBH_DWC2_EP_INFO * pEP, USBH_DWC2_CHANNEL_INFO * pChannelInfo);
#endif

#if defined(__cplusplus)
  }
#endif

#endif // USBH_HW_DWC2_H_

/*************************** End of file ****************************/
