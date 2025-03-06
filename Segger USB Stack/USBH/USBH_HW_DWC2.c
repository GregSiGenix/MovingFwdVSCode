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
Purpose     : USB host implementation
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#ifdef USBH_HW_DWC2_C_


/*********************************************************************
*
*       Defines, configurable
*
**********************************************************************
*/

#ifndef USBH_DWC2_HC_INIT_DELAY1
  #define USBH_DWC2_HC_INIT_DELAY1  100
#endif
#ifndef USBH_DWC2_HC_INIT_DELAY2
  #define USBH_DWC2_HC_INIT_DELAY2   20
#endif
#ifndef USBH_DWC2_HC_INIT_DELAY3
  #define USBH_DWC2_HC_INIT_DELAY3   50
#endif
#ifndef USBH_DWC2_HC_INIT_DELAY4
  #define USBH_DWC2_HC_INIT_DELAY4  100
#endif
#ifndef USBH_DWC2_HC_INIT_DELAY5
  #define USBH_DWC2_HC_INIT_DELAY5  100
#endif
#ifndef USBH_DWC2_HC_INIT_DELAY6
  #define USBH_DWC2_HC_INIT_DELAY6  200
#endif
#ifndef USBH_DWC2_HC_INIT_DELAY7
  #define USBH_DWC2_HC_INIT_DELAY7   50
#endif

/*********************************************************************
*
*       Defines, fixed
*
**********************************************************************
*/
#define GHWCFG2_OTG_ENABLE_IC_USB               (1 << 31)
#define GHWCFG2_DEV_TOKEN_Q_DEPTH_MASK          (0x1f << 26)
#define GHWCFG2_DEV_TOKEN_Q_DEPTH_SHIFT         26
#define GHWCFG2_HOST_PERIO_TX_Q_DEPTH_MASK      (0x3 << 24)
#define GHWCFG2_HOST_PERIO_TX_Q_DEPTH_SHIFT     24
#define GHWCFG2_NONPERIO_TX_Q_DEPTH_MASK        (0x3 << 22)
#define GHWCFG2_NONPERIO_TX_Q_DEPTH_SHIFT       22
#define GHWCFG2_MULTI_PROC_INT                  (1 << 20)
#define GHWCFG2_DYNAMIC_FIFO                    (1 << 19)
#define GHWCFG2_PERIO_EP_SUPPORTED              (1 << 18)
#define GHWCFG2_NUM_HOST_CHAN_MASK              (0xf << 14)
#define GHWCFG2_NUM_HOST_CHAN_SHIFT             14
#define GHWCFG2_NUM_DEV_EP_MASK                 (0xf << 10)
#define GHWCFG2_NUM_DEV_EP_SHIFT                10
#define GHWCFG2_FS_PHY_TYPE_MASK                (0x3 << 8)
#define GHWCFG2_FS_PHY_TYPE_SHIFT               8
#define GHWCFG2_FS_PHY_TYPE_NOT_SUPPORTED       0
#define GHWCFG2_FS_PHY_TYPE_DEDICATED           1
#define GHWCFG2_FS_PHY_TYPE_SHARED_UTMI         2
#define GHWCFG2_FS_PHY_TYPE_SHARED_ULPI         3
#define GHWCFG2_HS_PHY_TYPE_MASK                (0x3 << 6)
#define GHWCFG2_HS_PHY_TYPE_SHIFT               6
#define GHWCFG2_HS_PHY_TYPE_NOT_SUPPORTED       0
#define GHWCFG2_HS_PHY_TYPE_UTMI                1
#define GHWCFG2_HS_PHY_TYPE_ULPI                2
#define GHWCFG2_HS_PHY_TYPE_UTMI_ULPI           3
#define GHWCFG2_POINT2POINT                     (1 << 5)
#define GHWCFG2_ARCHITECTURE_MASK               (0x3 << 3)
#define GHWCFG2_ARCHITECTURE_SHIFT              3
#define GHWCFG2_SLAVE_ONLY_ARCH                 0
#define GHWCFG2_EXT_DMA_ARCH                    1
#define GHWCFG2_INT_DMA_ARCH                    2
#define GHWCFG2_OP_MODE_MASK                    (0x7 << 0)
#define GHWCFG2_OP_MODE_SHIFT                   0
#define GHWCFG2_OP_MODE_HNP_SRP_CAPABLE         0
#define GHWCFG2_OP_MODE_SRP_ONLY_CAPABLE        1
#define GHWCFG2_OP_MODE_NO_HNP_SRP_CAPABLE      2
#define GHWCFG2_OP_MODE_SRP_CAPABLE_DEVICE      3
#define GHWCFG2_OP_MODE_NO_SRP_CAPABLE_DEVICE   4
#define GHWCFG2_OP_MODE_SRP_CAPABLE_HOST        5
#define GHWCFG2_OP_MODE_NO_SRP_CAPABLE_HOST     6
#define GHWCFG2_OP_MODE_UNDEFINED               7

#define GHWCFG3_DFIFO_DEPTH_MASK                (0xffffUL << 16)
#define GHWCFG3_DFIFO_DEPTH_SHIFT               16
#define GHWCFG3_OTG_LPM_EN                      (1 << 15)
#define GHWCFG3_BC_SUPPORT                      (1 << 14)
#define GHWCFG3_OTG_ENABLE_HSIC                 (1 << 13)
#define GHWCFG3_ADP_SUPP                        (1 << 12)
#define GHWCFG3_SYNCH_RESET_TYPE                (1 << 11)
#define GHWCFG3_OPTIONAL_FEATURES               (1 << 10)
#define GHWCFG3_VENDOR_CTRL_IF                  (1 << 9)
#define GHWCFG3_I2C                             (1 << 8)
#define GHWCFG3_OTG_FUNC                        (1 << 7)
#define GHWCFG3_PACKET_SIZE_CNTR_WIDTH_MASK     (0x7 << 4)
#define GHWCFG3_PACKET_SIZE_CNTR_WIDTH_SHIFT    4
#define GHWCFG3_XFER_SIZE_CNTR_WIDTH_MASK       (0xf << 0)
#define GHWCFG3_XFER_SIZE_CNTR_WIDTH_SHIFT      0

#define GHWCFG4_DESC_DMA_DYN                    (1 << 31)
#define GHWCFG4_DESC_DMA                        (1 << 30)
#define GHWCFG4_NUM_IN_EPS_MASK                 (0xf << 26)
#define GHWCFG4_NUM_IN_EPS_SHIFT                26
#define GHWCFG4_DED_FIFO_EN                     (1 << 25)
#define GHWCFG4_DED_FIFO_SHIFT                  25
#define GHWCFG4_SESSION_END_FILT_EN             (1 << 24)
#define GHWCFG4_B_VALID_FILT_EN                 (1 << 23)
#define GHWCFG4_A_VALID_FILT_EN                 (1 << 22)
#define GHWCFG4_VBUS_VALID_FILT_EN              (1 << 21)
#define GHWCFG4_IDDIG_FILT_EN                   (1 << 20)
#define GHWCFG4_NUM_DEV_MODE_CTRL_EP_MASK       (0xf << 16)
#define GHWCFG4_NUM_DEV_MODE_CTRL_EP_SHIFT      16
#define GHWCFG4_UTMI_PHY_DATA_WIDTH_MASK        (0x3 << 14)
#define GHWCFG4_UTMI_PHY_DATA_WIDTH_SHIFT       14
#define GHWCFG4_UTMI_PHY_DATA_WIDTH_8           0
#define GHWCFG4_UTMI_PHY_DATA_WIDTH_16          1
#define GHWCFG4_UTMI_PHY_DATA_WIDTH_8_OR_16     2
#define GHWCFG4_XHIBER                          (1 << 7)
#define GHWCFG4_HIBER                           (1 << 6)
#define GHWCFG4_MIN_AHB_FREQ                    (1 << 5)
#define GHWCFG4_POWER_OPTIMIZ                   (1 << 4)
#define GHWCFG4_NUM_DEV_PERIO_IN_EP_MASK        (0xf << 0)
#define GHWCFG4_NUM_DEV_PERIO_IN_EP_SHIFT       0

#define GRXFSIZ_DEPTH_MASK              (0xffff << 0)
#define GRXFSIZ_DEPTH_SHIFT             0

/* These apply to the GNPTXFSIZ, HPTXFSIZ and DPTXFSIZN registers */
#define FIFOSIZE_DEPTH_MASK             (0xffffUL << 16)
#define FIFOSIZE_DEPTH_SHIFT            16
#define FIFOSIZE_STARTADDR_MASK         (0xffffUL << 0)
#define FIFOSIZE_STARTADDR_SHIFT        0
#define FIFOSIZE_DEPTH_GET(_x)          (((_x) >> 16) & 0xffff)

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/


/*********************************************************************
*
*       Static code
*
**********************************************************************
*/
/*********************************************************************
*
*       Devices helper functions
*
**********************************************************************
*/

/*********************************************************************
*
*       _GetHWParas
*
*  Function description
*    During device initialization, read various hardware configuration
*    registers and interpret the contents.
*
*  Parameters
*    pInst     : Pointer to the DWC2 instance structure.
*/
#if USBH_DEBUG > 1
static void _GetHWParas(USBH_DWC2_INST * pInst) {
  DWC2_HW_PARAMS * pHWParams = &pInst->HWParams;
  unsigned Width;
  U32 HWCfg1;
  U32 HWCfg2;
  U32 HWCfg3;
  U32 HWCfg4;
  U32 GRXFifoSize;
  U32 GNonPeriodicTXFifoSize;
  U32 HPeriodicTXFifoSize;

  /*
   * Attempt to ensure this device is really a DWC_otg Controller.
   * Read and verify the GSNPSID register contents. The value should be
   * 0x45f42xxx or 0x45f43xxx, which corresponds to either "OT2" or "OT3",
   * as in "OTG version 2.xx" or "OTG version 3.xx".
   */
  pHWParams->snpsid = pInst->pHWReg->GSNPSID;
  if ((pHWParams->snpsid & 0xFFFFF000u) != 0x4F542000u && (pHWParams->snpsid & 0xFFFFF000u) != 0x4F543000u) {
    USBH_WARN((USBH_MCAT_DRIVER, "Bad value for GSNPSID: 0x%08x", pHWParams->snpsid));
    return;
  }
  USBH_LOG((USBH_MCAT_DRIVER, "Core Release: %1x.%1x%1x%1x (snpsid=%x)", pHWParams->snpsid >> 12 & 0xf, pHWParams->snpsid >> 8 & 0xf,
                                                                   pHWParams->snpsid >> 4 & 0xf, pHWParams->snpsid & 0xf, pHWParams->snpsid));
  HWCfg1 = pInst->pHWReg->GHWCFG1;
  HWCfg2 = pInst->pHWReg->GHWCFG2;
  HWCfg3 = pInst->pHWReg->GHWCFG3;
  HWCfg4 = pInst->pHWReg->GHWCFG4;
  GRXFifoSize =pInst->pHWReg->GRXFSIZ;
  USBH_LOG((USBH_MCAT_DRIVER, "HWCFG1=%08x", HWCfg1));
  USBH_LOG((USBH_MCAT_DRIVER, "HWCFG2=%08x", HWCfg2));
  USBH_LOG((USBH_MCAT_DRIVER, "HWCFG3=%08x", HWCfg3));
  USBH_LOG((USBH_MCAT_DRIVER, "HWCFG4=%08x", HWCfg4));
  USBH_LOG((USBH_MCAT_DRIVER, "GRXFSIZ=%08x", GRXFifoSize));
  //
  // Host specific parameters, the controller has to be in host mode at this point.
  //
  GNonPeriodicTXFifoSize = pInst->pHWReg->GNPTXFSIZ;
  HPeriodicTXFifoSize = pInst->pHWReg->HPTXFSIZ;
  USBH_LOG((USBH_MCAT_DRIVER, "GNPTXFSIZ=%08x", GNonPeriodicTXFifoSize));
  USBH_LOG((USBH_MCAT_DRIVER, "HPTXFSIZ=%08x", HPeriodicTXFifoSize));
  pHWParams->host_nperio_tx_fifo_size = (GNonPeriodicTXFifoSize & FIFOSIZE_DEPTH_MASK) >> FIFOSIZE_DEPTH_SHIFT;
  pHWParams->host_perio_tx_fifo_size = (HPeriodicTXFifoSize & FIFOSIZE_DEPTH_MASK) >> FIFOSIZE_DEPTH_SHIFT;
  //
  // HWCFG1
  //
  pHWParams->dev_ep_dirs = HWCfg1;
  //
  // HWCFG2
  //
  pHWParams->op_mode               = (HWCfg2 & GHWCFG2_OP_MODE_MASK) >> GHWCFG2_OP_MODE_SHIFT;
  pHWParams->arch                  = (HWCfg2 & GHWCFG2_ARCHITECTURE_MASK) >> GHWCFG2_ARCHITECTURE_SHIFT;
  pHWParams->enable_dynamic_fifo   = !!(HWCfg2 & GHWCFG2_DYNAMIC_FIFO);
  pHWParams->host_channels         = 1 + ((HWCfg2 & GHWCFG2_NUM_HOST_CHAN_MASK) >> GHWCFG2_NUM_HOST_CHAN_SHIFT);
  pHWParams->hs_phy_type           = (HWCfg2 & GHWCFG2_HS_PHY_TYPE_MASK) >> GHWCFG2_HS_PHY_TYPE_SHIFT;
  pHWParams->fs_phy_type           = (HWCfg2 & GHWCFG2_FS_PHY_TYPE_MASK) >> GHWCFG2_FS_PHY_TYPE_SHIFT;
  pHWParams->num_dev_ep            = (HWCfg2 & GHWCFG2_NUM_DEV_EP_MASK) >> GHWCFG2_NUM_DEV_EP_SHIFT;
  pHWParams->nperio_tx_q_depth     = ((HWCfg2 & GHWCFG2_NONPERIO_TX_Q_DEPTH_MASK) >> GHWCFG2_NONPERIO_TX_Q_DEPTH_SHIFT) << 1;
  pHWParams->host_perio_tx_q_depth = ((HWCfg2 & GHWCFG2_HOST_PERIO_TX_Q_DEPTH_MASK) >> GHWCFG2_HOST_PERIO_TX_Q_DEPTH_SHIFT) << 1;
  pHWParams->dev_token_q_depth     = (HWCfg2 & GHWCFG2_DEV_TOKEN_Q_DEPTH_MASK) >> GHWCFG2_DEV_TOKEN_Q_DEPTH_SHIFT;
  //
  // HWCFG3
  //
  Width = (HWCfg3 & GHWCFG3_XFER_SIZE_CNTR_WIDTH_MASK) >> GHWCFG3_XFER_SIZE_CNTR_WIDTH_SHIFT;
  pHWParams->max_transfer_size = (1 << (Width + 11)) - 1;
  Width = (HWCfg3 & GHWCFG3_PACKET_SIZE_CNTR_WIDTH_MASK) >> GHWCFG3_PACKET_SIZE_CNTR_WIDTH_SHIFT;
  pHWParams->max_packet_count = (1 << (Width + 4)) - 1;
  pHWParams->i2c_enable = !!(HWCfg3 & GHWCFG3_I2C);
  pHWParams->total_fifo_size = (HWCfg3 & GHWCFG3_DFIFO_DEPTH_MASK) >> GHWCFG3_DFIFO_DEPTH_SHIFT;
  //
  // HWCFG4
  //
  pHWParams->en_multiple_tx_fifo = !!(HWCfg4 & GHWCFG4_DED_FIFO_EN);
  pHWParams->num_dev_perio_in_ep = (HWCfg4 & GHWCFG4_NUM_DEV_PERIO_IN_EP_MASK) >> GHWCFG4_NUM_DEV_PERIO_IN_EP_SHIFT;
  pHWParams->dma_desc_enable = !!(HWCfg4 & GHWCFG4_DESC_DMA);
  pHWParams->power_optimized = !!(HWCfg4 & GHWCFG4_POWER_OPTIMIZ);
  pHWParams->utmi_phy_data_width = (HWCfg4 & GHWCFG4_UTMI_PHY_DATA_WIDTH_MASK) >> GHWCFG4_UTMI_PHY_DATA_WIDTH_SHIFT;
  //
  // FIFO sizes
  //
  pHWParams->host_rx_fifo_size = (GRXFifoSize & GRXFSIZ_DEPTH_MASK) >> GRXFSIZ_DEPTH_SHIFT;
  USBH_LOG((USBH_MCAT_DRIVER, "Detected values from hardware:"));
  USBH_LOG((USBH_MCAT_DRIVER, "  op_mode=%d", pHWParams->op_mode));
  USBH_LOG((USBH_MCAT_DRIVER, "  arch=%d",  pHWParams->arch));
  USBH_LOG((USBH_MCAT_DRIVER, "  dma_desc_enable=%d", pHWParams->dma_desc_enable));
  USBH_LOG((USBH_MCAT_DRIVER, "  power_optimized=%d", pHWParams->power_optimized));
  USBH_LOG((USBH_MCAT_DRIVER, "  i2c_enable=%d", pHWParams->i2c_enable));
  USBH_LOG((USBH_MCAT_DRIVER, "  hs_phy_type=%d", pHWParams->hs_phy_type));
  USBH_LOG((USBH_MCAT_DRIVER, "  fs_phy_type=%d", pHWParams->fs_phy_type));
  USBH_LOG((USBH_MCAT_DRIVER, "  utmi_phy_data_width=%d", pHWParams->utmi_phy_data_width));
  USBH_LOG((USBH_MCAT_DRIVER, "  num_dev_ep=%d", pHWParams->num_dev_ep));
  USBH_LOG((USBH_MCAT_DRIVER, "  num_dev_perio_in_ep=%d", pHWParams->num_dev_perio_in_ep));
  USBH_LOG((USBH_MCAT_DRIVER, "  host_channels=%d", pHWParams->host_channels));
  USBH_LOG((USBH_MCAT_DRIVER, "  max_transfer_size=%d", pHWParams->max_transfer_size));
  USBH_LOG((USBH_MCAT_DRIVER, "  max_packet_count=%d", pHWParams->max_packet_count));
  USBH_LOG((USBH_MCAT_DRIVER, "  nperio_tx_q_depth=0x%0x", pHWParams->nperio_tx_q_depth));
  USBH_LOG((USBH_MCAT_DRIVER, "  host_perio_tx_q_depth=0x%0x",pHWParams->host_perio_tx_q_depth));
  USBH_LOG((USBH_MCAT_DRIVER, "  dev_token_q_depth=0x%0x", pHWParams->dev_token_q_depth));
  USBH_LOG((USBH_MCAT_DRIVER, "  enable_dynamic_fifo=%d", pHWParams->enable_dynamic_fifo));
  USBH_LOG((USBH_MCAT_DRIVER, "  en_multiple_tx_fifo=%d", pHWParams->en_multiple_tx_fifo));
  USBH_LOG((USBH_MCAT_DRIVER, "  total_fifo_size=%d", pHWParams->total_fifo_size));
  USBH_LOG((USBH_MCAT_DRIVER, "  host_rx_fifo_size=%d", pHWParams->host_rx_fifo_size));
  USBH_LOG((USBH_MCAT_DRIVER, "  host_nperio_tx_fifo_size=%d", pHWParams->host_nperio_tx_fifo_size));
  USBH_LOG((USBH_MCAT_DRIVER, "  host_perio_tx_fifo_size=%d", pHWParams->host_perio_tx_fifo_size));
}
#endif

/*********************************************************************
*
*       _DWC2_CompleteUrb
*
*  Function description
*    This function is called if an URB is terminated.
*    The EP state is set to 'idle' and the user callback is called.
*    The pointer pEPInfo->pPendingUrb is used as an indicator, if the EP is busy (!=NULL) or idle (==NULL).
*    To avoid race conditions on the busy state of an EP, setting of pPendingUrb must be protected
*    (USBH_OS_DisableInterrupt() prohibits task switches).
*    pPendingUrb must be reset before the user callback is called because the callback function
*    may submit another URB on that EP and should not find the EP in busy state.
*/
static void _DWC2_CompleteUrb(USBH_DWC2_EP_INFO * pEPInfo, USBH_STATUS Status) {
  USBH_URB * pPendingUrb;

  USBH_OS_DisableInterrupt();
  pPendingUrb          = pEPInfo->pPendingUrb;
  pEPInfo->pPendingUrb = NULL;
  pEPInfo->Aborted     = 0;
  USBH_OS_EnableInterrupt();
  if (pPendingUrb != NULL) {
    USBH_LOG((USBH_MCAT_DRIVER_URB, "_DWC2_CompleteUrb: pEPInfo 0x%x length: %u!", pEPInfo->EndpointAddress, pPendingUrb->Request.BulkIntRequest.Length));
    pPendingUrb->Header.Status = Status;
    USBH_ASSERT(pPendingUrb->Header.pfOnInternalCompletion);
    pPendingUrb->Header.pfOnInternalCompletion(pPendingUrb); // Call the completion routine
  }
}

/*********************************************************************
*
*       _HandleChannels
*/
static void _HandleChannels(USBH_DWC2_INST * pInst, U32 ChannelMask) {
  USBH_DWC2_CHANNEL_INFO * pChannelInfo;
  USBH_DWC2_EP_INFO      * pEPInfo;
  USBH_DWC2_HCCHANNEL    * pHwChannel;

  pHwChannel = &pInst->pHWReg->aHChannel[0];
  pChannelInfo = &pInst->aChannelInfo[0];
  while (ChannelMask > 0u) {
    if ((ChannelMask & 1u) != 0u) {
      if (pHwChannel->HCINT != 0u) {
        if (pChannelInfo->InUse != FALSE) {
          pEPInfo = pChannelInfo->pEPInfo;
          switch (pEPInfo->EndpointType) {
          case USB_EP_TYPE_CONTROL:
            _DWC2_HandleEP0(pInst, pChannelInfo);
            break;
#if USBH_SUPPORT_ISO_TRANSFER
          case USB_EP_TYPE_ISO:
            _DWC2_HandleEPIso(pInst, pChannelInfo);
            break;
#endif
          default:
            _DWC2_HandleEPx(pInst, pChannelInfo);
            break;
          }
        } else {
          //
          // Channel issues interrupt while not in use --> clear all interrupts.
          //
          pHwChannel->HCINT = CHANNEL_MASK;
        }
      }
    }
    pHwChannel++;
    pChannelInfo++;
    ChannelMask >>= 1;
  }
}

/*********************************************************************
*
*       _SetHcFuncState
*
*  Function description
*    Changes the running state of the host controller
*/
static void _SetHcFuncState(USBH_DWC2_INST * pInst, USBH_HOST_STATE State) {
  if (State == USBH_HOST_RUNNING) {
    pInst->pHWReg->GINTMSK  = 0u
#if USBH_DWC2_USE_DMA == 0
                            | HOST_RXFLVL        // RX data available
#endif
                            | HOST_PORT_INT      // Host port interrupt mask
                            | HOST_DISC_INT      // Disconnect interrupt mask
                            | HOST_CHANNEL_INT   // Host Channel interrupt mask
                            | (1uL << 31)        // Wake up interrupt mask
                            ;
    pInst->pHWReg->HAINTMSK = ((1uL << DWC2_NUM_CHANNELS) - 1u);
    USBH_StartTimer(&pInst->ChannelCheckTimer, USBH_DWC2_CHECK_CHANNEL_INTERVAL);
  }
}

/*********************************************************************
*
*       _DWC2_ConfigureFIFO
*
*  Function description
*    Configure FIFO SRAM
*/
#ifdef USBH_DWC2_RECEIVE_FIFO_SIZE
static void _DWC2_ConfigureFIFO(const USBH_DWC2_INST * pInst) {
  for (;;) {
    pInst->pHWReg->GRXFSIZ = USBH_DWC2_RECEIVE_FIFO_SIZE;       // Rx FIFO
    //
    // Non-periodic Tx FIFO
    //
    pInst->pHWReg->GNPTXFSIZ  = (USBH_DWC2_NON_PERIODIC_TRANSMIT_FIFO_SIZE << 16) | USBH_DWC2_RECEIVE_FIFO_SIZE;
    //
    // Periodic Tx FIFO
    //
    pInst->pHWReg->HPTXFSIZ  = (USBH_DWC2_PERIODIC_TRANSMIT_FIFO_SIZE << 16) | (USBH_DWC2_RECEIVE_FIFO_SIZE + USBH_DWC2_NON_PERIODIC_TRANSMIT_FIFO_SIZE);
    //
    // Make sure the FIFOs are flushed.
    //
    pInst->pHWReg->GRSTCTL  = (0x10u << 6)    // All tx buffers
                            | (   1u << 5)    // flush tx FIFOs
                            ;
    USBH_OS_Delay(5);
    while ((pInst->pHWReg->GRSTCTL & (1u << 5)) != 0u) {
    }
    pInst->pHWReg->GRSTCTL  = (1uL << 4);    // flush rx FIFO
    USBH_OS_Delay(5);
    while ((pInst->pHWReg->GRSTCTL & (1uL << 4)) != 0u) {
    }
    while((pInst->pHWReg->GRSTCTL & (1uL << 31)) == 0u) {
    }
    if ((pInst->pHWReg->GRXFSIZ == USBH_DWC2_RECEIVE_FIFO_SIZE) &&
        (pInst->pHWReg->GNPTXFSIZ == ((USBH_DWC2_NON_PERIODIC_TRANSMIT_FIFO_SIZE << 16) | USBH_DWC2_RECEIVE_FIFO_SIZE)) &&
        (pInst->pHWReg->HPTXFSIZ == ((USBH_DWC2_PERIODIC_TRANSMIT_FIFO_SIZE << 16) | (USBH_DWC2_RECEIVE_FIFO_SIZE + USBH_DWC2_NON_PERIODIC_TRANSMIT_FIFO_SIZE)))) {
      break;
    }
    USBH_WARN((USBH_MCAT_DRIVER, "_DWC2_ConfigureFIFO: Cannot set FIFO sizes! Retrying..."));
    USBH_OS_Delay(100);
  }
}
#endif

/*********************************************************************
*
*       _DWC2_HostInit
*
*  Function description
*    Reset and initialize the hardware.
*/
#ifndef DWC2_HOST_INIT_OVERRIDE
static void _DWC2_HostInit(const USBH_DWC2_INST * pInst) {
  U32 Channel;

  //
  // Remove any settings. Especially important because the controller may be in forced device mode.
  //
  if (pInst->PhyType == 1u) {
    pInst->pHWReg->GUSBCFG = (1uL << 6);     // Internal PHY clock must be enabled before a core reset can be executed.
  } else {
    pInst->pHWReg->GUSBCFG = 0;
  }
  pInst->pHWReg->PCGCCTL = 0;                // Restart the Phy Clock
  USBH_OS_Delay(USBH_DWC2_HC_INIT_DELAY1);
  while((pInst->pHWReg->GRSTCTL & (1uL << 31)) == 0u) {
  }
  pInst->pHWReg->GRSTCTL = (1uL << 0);       // Core reset
  USBH_OS_Delay(USBH_DWC2_HC_INIT_DELAY2);
  while((pInst->pHWReg->GRSTCTL & 1u) != 0u) {
  }
  USBH_OS_Delay(USBH_DWC2_HC_INIT_DELAY3);
#if USBH_DWC2_HIGH_SPEED
  pInst->pHWReg->GUSBCFG |= (1uL << 29)       // Force the OTG controller into host mode.
                         |  (1uL << 24)       // Complement Output signal is not qualified with the Internal VBUS valid comparator
                         |  (1uL << 23)       // PHY inverts ExternalVbusIndicator signal
                         |  (1uL << 21)       // PHY uses an external V BUS valid comparator
                         |  (1uL << 20)       // PHY drives VBUS using external supply
                         ;
  USBH_OS_Delay(USBH_DWC2_HC_INIT_DELAY4);    // Wait at least 25 ms after force to host mode (some controllers need more)
  if (pInst->PhyType == 1u) {
    U32 Cfg;
    Cfg = pInst->pHWReg->GUSBCFG;
    Cfg &= ~(0x0FuL << 10);                   // Delete Timeout for calibration set to max value from the change value
    Cfg |= (1uL  <<  6)                       // Enable the internal PHY clock.
        |  (0x07uL <<  0)                     // Timeout for calibration set to max value
        |  (0x0FuL << 10)                     // Set turnaround to max value
        ;
    pInst->pHWReg->GUSBCFG  = Cfg;
    pInst->pHWReg->GCCFG   |= (1uL << 16);    // Power down deactivated ("Transceiver active")
    USBH_OS_Delay(USBH_DWC2_HC_INIT_DELAY5);
  }
#else
  pInst->pHWReg->GUSBCFG |= (1uL <<  6)       // Enable the internal PHY clock.
                         |  (1uL << 29);      // Force the OTG controller into host mode.
  USBH_OS_Delay(USBH_DWC2_HC_INIT_DELAY6);
  pInst->pHWReg->GCCFG   |= (1uL << 16)       // Power down deactivated ("Transceiver active")
                         |  (1uL << 18)       // Enable the VBUS sensing "A" device
                         |  (1uL << 19)       // Enable the VBUS sensing "B" device
                         |  (1uL << 21)       // VBUS sensing disable option
                         ;
  USBH_OS_Delay(USBH_DWC2_HC_INIT_DELAY7);
#endif
  //
  // Configure data FIFO sizes, if necessary
  //
#ifdef USBH_DWC2_RECEIVE_FIFO_SIZE
  _DWC2_ConfigureFIFO(pInst);
#endif
#if USBH_DWC2_USE_DMA
  pInst->pHWReg->GAHBCFG  = (1u << 5)         // Enable DMA
                          | (0u << 1)         // Set burst length to single
                          ;
#else
  pInst->pHWReg->GAHBCFG |= (3uL << 7);
#endif
  pInst->pHWReg->GINTMSK = 0;            // Disable all interrupts
  pInst->pHWReg->GINTSTS = 0xFFFFFFFFu;  // Clear any pending interrupts.
  /* Disable all channels interrupt Masks */
  for (Channel = 0; Channel < DWC2_NUM_CHANNELS; Channel++) {
    pInst->pHWReg->aHChannel[Channel].HCINTMSK = 0;
  }
}
#endif

/*********************************************************************
*
*       _HostInit
*
*  Function description
*    Is called in the pContext of USBH_AddHostController make a
*    basic initialization of the hardware, reset the hardware,
*    setup internal lists, leave the host in the state  UBB_HOST_RESET
*/
static USBH_STATUS _HostInit(USBH_HC_HANDLE hHostController, USBH_ROOT_HUB_NOTIFICATION_FUNC * pfUbdRootHubNotification, void * pRootHubNotificationContext) {
  USBH_DWC2_INST  * pInst;

  USBH_LOG((USBH_MCAT_DRIVER, "_HostInit!"));
  pInst = USBH_HDL2PTR(USBH_DWC2_INST, hHostController);
  USBH_DWC2_IS_DEV_VALID(pInst);
  _DWC2_DisableInterrupts(pInst);
  _DWC2_HostInit(pInst);
#if USBH_DEBUG > 1
  _GetHWParas(pInst);
#endif
  pInst->pfUbdRootHubNotification = pfUbdRootHubNotification;
  pInst->pRootHubNotificationContext = pRootHubNotificationContext;
#if USBH_DWC2_USE_DMA
  pInst->MaxTransferSize = USBH_DWC2_DEFAULT_TRANSFER_BUFF_SIZE;
#else
  pInst->MaxTransferSize = USBH_DWC2_MAX_TRANSFER_SIZE;
#endif
  _DWC2_EnableInterrupts(pInst);
  return USBH_STATUS_SUCCESS;
}

/*********************************************************************
*
*       _HostExit
*
*  Function description
*    Is the last call on this interface. It is called after all USBH_URB's are returned,
*    all endpoints are released and no further reference to the host controller exists.
*    In this call the host controller driver can check that all lists (USBH_URB's, Endpoints)
*    are empty and delete all resources, disable interrupts. The HC state
*    is USBH_HOST_RESET if this function is called.
*/
static USBH_STATUS _HostExit(USBH_HC_HANDLE hHostController) {
  USBH_DWC2_INST      * pInst;

  USBH_LOG((USBH_MCAT_DRIVER, "_HostExit!"));
  pInst = USBH_HDL2PTR(USBH_DWC2_INST, hHostController);
  USBH_DWC2_IS_DEV_VALID(pInst);
  //
  // Disable the USB interrupts globally
  //
  _DWC2_DisableInterrupts(pInst);
  pInst->pHWReg->GINTMSK = 0;             // Disable all interrupts
  pInst->pHWReg->GINTSTS  = 0xFFFFFFFFu;  // Clear any pending interrupts.
  pInst->pHWReg->GOTGINT  = 0xFFFFFFFFu;  // Clear any pending USB_OTG Interrupts
  USBH_ReleaseTimer(&pInst->ChannelCheckTimer);
  pInst->pHWReg->GCCFG  &= ~(1UL << 16);  // Power off PHY
  USBH_FREE(pInst);
  return USBH_STATUS_SUCCESS;
}

/*********************************************************************
*
*       _SetHcState
*
*  Function description
*    Set the state of the HC
*/
static USBH_STATUS _SetHcState(USBH_HC_HANDLE hHostController, USBH_HOST_STATE HostState) {
  USBH_DWC2_INST  * pInst;

  USBH_LOG((USBH_MCAT_DRIVER, "_SetHcState: HostState:%d!", HostState));
  pInst = USBH_HDL2PTR(USBH_DWC2_INST, hHostController);
  USBH_DWC2_IS_DEV_VALID(pInst);
  _SetHcFuncState(pInst, HostState);
  return USBH_STATUS_SUCCESS;
}

/*********************************************************************
*
*       _OnSOFSplt
*/
#if USBH_DWC2_SUPPORT_SPLIT_TRANSACTIONS
static void _OnSOFSplt(USBH_DWC2_INST * pInst) {
  U32                      ChannelMask;
  USBH_DWC2_HCCHANNEL    * pHwChannel;

  ChannelMask = pInst->CompleteChannelMask;
  if (ChannelMask == 0u) {
    //
    // Early return if nothing to do.
    //
    return;
  }
  pInst->SOFNotUsedCount = 0;
  pHwChannel = &pInst->pHWReg->aHChannel[0];
  while (ChannelMask != 0u) {
    if ((ChannelMask & 1u) != 0u) {
      pHwChannel->HCCHAR |= HCCHAR_CHENA;
    }
    ChannelMask >>= 1;
    pHwChannel++;
  }
  pInst->CompleteChannelMask = 0;
}
#endif

/*********************************************************************
*
*       _OnChannelCheck
*/
static void _OnChannelCheck(void * pContext) {
  unsigned                 Channel;
  USBH_DWC2_HCCHANNEL    * pHwChannel;
  USBH_DWC2_CHANNEL_INFO * pChannelInfo;
  USBH_DWC2_EP_INFO      * pEPInfo;
  USBH_DWC2_INST         * pInst = USBH_CTX2PTR(USBH_DWC2_INST, pContext);

#if 0
  U32                      ChannelInt;

  ChannelInt = 0;
  //
  // Temporary disable the interrupt
  //
  _DWC2_DisableInterrupts(pInst);
  pHwChannel = &pInst->pHWReg->aHChannel[0];
  for (Channel = 0; Channel < DWC2_NUM_CHANNELS; Channel++) {
    U32 Temp;

    Temp = pHwChannel->HCINT;
    if (Temp & pHwChannel->HCINTMSK) {
      ChannelInt |= (1u << Channel);
    }
    pHwChannel++;
  }
  ChannelInt &= ~(pInst->pHWReg->HAINT);
  ChannelInt &= pInst->UsedChannelMask;
  if (ChannelInt) {
    USBH_WARN((USBH_MCAT_DRIVER_IRQ, "_OnChannelCheck: Handle missing interrupts!"));
    _HandleChannels(pInst, ChannelInt);
  }
  //
  // Restore
  //
  _DWC2_EnableInterrupts(pInst);
#endif
  //
  // Check for channels that hang to be aborted or disabled.
  // (Sometimes the controller does not process a channel disable request)
  //
  pHwChannel   = &pInst->pHWReg->aHChannel[0];
  pChannelInfo = &pInst->aChannelInfo[0];
  for (Channel = 0; Channel < DWC2_NUM_CHANNELS; Channel++) {
    USBH_OS_DisableInterrupt();
    if (pChannelInfo->InUse != FALSE) {
      pEPInfo = pChannelInfo->pEPInfo;
      EP_VALID(pEPInfo);
      if (pEPInfo->Aborted != 0u) {
        if (pEPInfo->Aborted >= 3u) {
          pInst->ResetDelayCount = 3;
          if (pEPInfo->Aborted < 6u && (pHwChannel->HCCHAR & HCCHAR_CHENA) != 0u) {
            //
            // Re-trigger channel halt
            //
            pHwChannel->HCCHAR |= (HCCHAR_CHDIS | HCCHAR_CHENA);
            USBH_WARN((USBH_MCAT_DRIVER, "_OnChannelCheck: Re-trigger channel halt on %d!", Channel));
          } else {
            //
            // Channel did not change to 'disabled' state after some retries.
            // Force URB to finish now.
            //
            _DWC2_CHANNEL_DeAllocate(pInst, pChannelInfo);
            USBH_OS_EnableInterrupt();
            USBH_WARN((USBH_MCAT_DRIVER, "_OnChannelCheck: Force URB to finish (%u)", Channel));
            _DWC2_CompleteUrb(pEPInfo, USBH_STATUS_CANCELED);
            USBH_OS_DisableInterrupt();
          }
        }
        pEPInfo->Aborted++;
      }
    } else {
      if ((pHwChannel->HCCHAR & HCCHAR_CHENA) != 0u) {
        //
        // Channel was deallocated and should be disabled.
        //
        pHwChannel->HCCHAR = HCCHAR_CHDIS | HCCHAR_CHENA;
        if (pChannelInfo->ErrorCount < 20u) {
          if (++pChannelInfo->ErrorCount == 20u) {
            USBH_WARN((USBH_MCAT_DRIVER, "_OnChannelCheck: channel %d is dead!", Channel));
          } else {
            USBH_WARN((USBH_MCAT_DRIVER, "_OnChannelCheck: Re-trigger channel disable on %d!", Channel));
          }
        }
        if (pInst->ResetDelayCount == 0u) {
          pInst->ResetDelayCount = 3;
        }
      }
    }
    USBH_OS_EnableInterrupt();
    pHwChannel++;
    pChannelInfo++;
  }
  //
  // Check for port disconnect which may make a controller reset necessary.
  //
  _DWC2_DisableInterrupts(pInst);
  if (pInst->DisconnectDetect != FALSE) {
    if ((_DWC2_ROOTHUB_GetPortStatus(pInst, 0) & PORT_STATUS_CONNECT) != 0u) {     //lint !e632
      //
      // Device still connected -> Clear interrupt flag.
      //
      pInst->DisconnectDetect = FALSE;
    } else {
      if (pInst->ResetDelayCount != 0u) {
        if (--pInst->ResetDelayCount == 0u) {
          //
          // Some channels are blocked after a disconnect.
          // Reset and re-initialize the controller.
          //
          USBH_WARN((USBH_MCAT_DRIVER, "_OnChannelCheck: Reset controller"));
          pInst->pHWReg->GINTMSK = 0;             // Disable all interrupts
          pInst->pHWReg->GINTSTS = 0xFFFFFFFFu;   // Clear any pending interrupts.
          pInst->pHWReg->GOTGINT = 0xFFFFFFFFu;   // Clear any pending USB_OTG Interrupts
          _DWC2_HostInit(pInst);
          _SetHcFuncState(pInst, USBH_HOST_RUNNING);
          _DWC2_ROOTHUB_SetPortPower(pInst, 0, 1);                               //lint !e632
          pInst->ResetDelayCount = 0;
        }
      }
    }
  }
  _DWC2_EnableInterrupts(pInst);
  USBH_StartTimer(&pInst->ChannelCheckTimer, USBH_DWC2_CHECK_CHANNEL_INTERVAL);
}

/*********************************************************************
*
*       _GetFrameNumber
*
*  Function description
*    Returns the frame number as a 16 bit value
*/
static U32 _GetFrameNumber(USBH_HC_HANDLE hHostController) {
  USBH_DWC2_INST    * pInst;

  pInst = USBH_HDL2PTR(USBH_DWC2_INST, hHostController);
  return pInst->pHWReg->HFNUM & 0xFFFFu;
}

/*********************************************************************
*
*       _AddEndpoint
*
*  Function description
*    Returns an endpoint Handle for a new created endpoint.
*
*  Parameters
*    hHostController : Handle of the host controller.
*    EndpointType    : Type of the endpoint, one of USB_EP_TYPE_CONTROL, ...
*    DeviceAddress   : Device address, 0 is allowed.
*    EndpointAddress : Endpoint address with direction bit.
*    MaxPacketSize   : Maximum transfer FIFO size in the host controller for that endpoint.
*    IntervalTime    : Interval time in or the NAK rate if this is a USB high speed bulk endpoint (in micro frames).
*    Speed           : The speed of the endpoint.
*
*  Return value
*    USBH_HC_EP_HANDLE
*/
static USBH_HC_EP_HANDLE _AddEndpoint(USBH_HC_HANDLE hHostController, U8 EndpointType, U8 DeviceAddress, U8 EndpointAddress, U16 MaxPacketSize, U16 IntervalTime, USBH_SPEED Speed) {
  USBH_DWC2_INST    * pInst;
  USBH_DWC2_EP_INFO * pEPInfo;

  USBH_LOG((USBH_MCAT_DRIVER_EP, "_AddEndpoint: Dev.Addr: %u, EpAddr: 0x%x max.Fifo size: %u", DeviceAddress, EndpointAddress, MaxPacketSize));
  pInst = USBH_HDL2PTR(USBH_DWC2_INST, hHostController);
  USBH_DWC2_IS_DEV_VALID(pInst);
  IntervalTime >>= 3;                    // We only support interval in ms resolution
  if (IntervalTime == 0u) {
    IntervalTime = 1;
  }
  if (EndpointType == USB_EP_TYPE_ISO) {
#if USBH_SUPPORT_ISO_TRANSFER != 0
  #ifdef USBH_DWC2_RECEIVE_FIFO_SIZE
    U16  Tmp;
    //
    // Calculate maximum packet size supported by the FIFO configuration (in bytes!)
    //
    if ((EndpointAddress & 0x80u) == 0u) {
      Tmp = 4u * USBH_DWC2_PERIODIC_TRANSMIT_FIFO_SIZE;
    } else {
      Tmp = 4u * (USBH_DWC2_RECEIVE_FIFO_SIZE - 3u);
    }
    if (MaxPacketSize > Tmp) {
      USBH_WARN((USBH_MCAT_DRIVER_EP, "_AddEndpoint: Unsupported ISO EP: Size %u", MaxPacketSize));
      return NULL;
    }
  #endif
    if (IntervalTime != 1u) {
      USBH_WARN((USBH_MCAT_DRIVER_EP, "_AddEndpoint: Unsupported ISO EP: interval %u", IntervalTime));
      return NULL;
    }
#else
    USBH_WARN((USBH_MCAT_DRIVER_EP, "_AddEndpoint: Invalid endpoint (ISO)"));
    return NULL;
#endif
  }
  #ifdef USBH_DWC2_RECEIVE_FIFO_SIZE
  if (EndpointType == USB_EP_TYPE_INT) {
    U16  Tmp;
    //
    // Calculate maximum packet size supported by the FIFO configuration (in bytes!)
    //
    if ((EndpointAddress & 0x80u) == 0u) {
      Tmp = 4u * USBH_DWC2_PERIODIC_TRANSMIT_FIFO_SIZE;
    } else {
      Tmp = 4u * (USBH_DWC2_RECEIVE_FIFO_SIZE - 3u);
    }
    if (MaxPacketSize > Tmp) {
      USBH_WARN((USBH_MCAT_DRIVER_EP, "_AddEndpoint: Unsupported INT EP: Size %u", MaxPacketSize));
      return NULL;
    }
  }
#endif
  pEPInfo = (USBH_DWC2_EP_INFO *)USBH_TRY_MALLOC_ZEROED(sizeof(USBH_DWC2_EP_INFO));
  if (pEPInfo == NULL) {
    USBH_WARN((USBH_MCAT_DRIVER_EP, "_AddEndpoint: Malloc!"));
    return NULL;
  }
  pEPInfo->EndpointType    = EndpointType;
  pEPInfo->pInst           = pInst;       // Backward pointer to the device
  pEPInfo->DeviceAddress   = DeviceAddress;
  pEPInfo->EndpointAddress = EndpointAddress;
  pEPInfo->MaxPacketSize   = MaxPacketSize;
  pEPInfo->Speed           = Speed;
  pEPInfo->NextDataPid     = DATA_PID_DATA0;
  pEPInfo->Channel         = DWC2_INVALID_CHANNEL;
  pEPInfo->Phase           = ES_IDLE;    // Init setup state machine
  pEPInfo->IntervalTime    = IntervalTime;
  USBH_IFDBG(pEPInfo->Magic = USBH_DWC2_EP_INFO_MAGIC);
  return pEPInfo;     //lint !e632
}

/*********************************************************************
*
*       _DWC2_OnRemoveEPTimer
*
*  Function description
*    Called if an endpoints has been removed.
*/
static void _DWC2_OnRemoveEPTimer(void * pContext) {
  USBH_DWC2_EP_INFO               * pEPInfo;
  USBH_RELEASE_EP_COMPLETION_FUNC * pfCompletion;
  void                            * pCompContext;

  pEPInfo = USBH_CTX2PTR(USBH_DWC2_EP_INFO, pContext);
  EP_VALID(pEPInfo);
  USBH_ReleaseTimer(&pEPInfo->RemovalTimer);
  pfCompletion = pEPInfo->pfOnReleaseCompletion;
  pCompContext = pEPInfo->pReleaseContext;
  if (pEPInfo->pBuffer != NULL) {
    USBH_FREE(pEPInfo->pBuffer);
  }
  USBH_FREE(pEPInfo);
  if(pfCompletion != NULL) {  // Call the completion routine, attention: runs in this context
    pfCompletion(pCompContext);
  }
}

/*********************************************************************
*
*       _ReleaseEndpoint
*
*  Function description
*    Releases that endpoint. This function returns immediately.
*    If the Completion function is called the endpoint is removed.
*
*  Parameters
*    hEndPoint    -
*    pfReleaseEpCompletion    -
*    pContext    -
*/
static void _ReleaseEndpoint(USBH_HC_EP_HANDLE hEndPoint, USBH_RELEASE_EP_COMPLETION_FUNC * pfReleaseEpCompletion, void * pContext) {
  USBH_DWC2_EP_INFO * pEPInfo;

  if (NULL == hEndPoint) {
    USBH_WARN((USBH_MCAT_DRIVER_EP, "_ReleaseEndpoint: invalid hEndPoint!"));
    return;
  }
  pEPInfo = USBH_HDL2PTR(USBH_DWC2_EP_INFO, hEndPoint);
  EP_VALID(pEPInfo);
  USBH_ASSERT(pEPInfo->pPendingUrb == NULL);
  USBH_LOG((USBH_MCAT_DRIVER_EP, "_ReleaseEndpoint 0x%x!", pEPInfo->EndpointAddress));
  pEPInfo->pReleaseContext       = pContext;
  pEPInfo->pfOnReleaseCompletion = pfReleaseEpCompletion;
  if (pEPInfo->ReleaseInProgress != FALSE) {
    USBH_WARN((USBH_MCAT_DRIVER_EP, "_ReleaseEndpoint: Endpoint already released, return!"));
    return;
  }
  pEPInfo->ReleaseInProgress = TRUE;
  USBH_InitTimer(&pEPInfo->RemovalTimer, _DWC2_OnRemoveEPTimer, pEPInfo);
  USBH_StartTimer(&pEPInfo->RemovalTimer, USBH_EP_STOP_DELAY_TIME);
}

/*********************************************************************
*
*       _AbortEndpoint
*
*  Function description
*    Complete all pending requests. This function returns immediately.
*    But the USBH_URB's may completed delayed, if the hardware require this.
*
*  Parameters
*    hEndPoint    -
*
*  Return value
*    USBH_STATUS       -
*/
static USBH_STATUS _AbortEndpoint(USBH_HC_EP_HANDLE hEndPoint) {
  USBH_DWC2_EP_INFO      * pEP;
  USBH_DWC2_INST         * pInst;
  U8                       Channel;

  pEP = USBH_HDL2PTR(USBH_DWC2_EP_INFO, hEndPoint);
  EP_VALID(pEP);
  pInst    = pEP->pInst;
  USBH_DWC2_IS_DEV_VALID(pInst);
  USBH_LOG((USBH_MCAT_DRIVER_URB, "_AbortEndpoint!"));
  USBH_OS_DisableInterrupt();
  if (pEP->Aborted != 0u || pEP->pPendingUrb == NULL) { // Already aborted
    goto Done;
  }
  pEP->Aborted = 1;
  Channel = pEP->Channel;
  if (Channel != DWC2_INVALID_CHANNEL) {
    _DWC2_AbortURB(pInst, pEP, Channel);
  }
Done:
  USBH_OS_EnableInterrupt();
  return USBH_STATUS_SUCCESS;
}

/*********************************************************************
*
*       _ResetEndpoint
*
*  Function description
*    Resets the data toggle bit to 0. The USB stack takes care
*    that this function is called only if no pending USBH_URB
*    for this EP is scheduled.
*/
static USBH_STATUS _ResetEndpoint(USBH_HC_EP_HANDLE hEndPoint) {
  USBH_DWC2_EP_INFO  * pEPInfo;
  USBH_STATUS          Status;

  pEPInfo = USBH_HDL2PTR(USBH_DWC2_EP_INFO, hEndPoint);
  EP_VALID(pEPInfo);
  switch (pEPInfo->EndpointType) {
  case USB_EP_TYPE_BULK:
  case USB_EP_TYPE_INT:
    USBH_LOG((USBH_MCAT_DRIVER_EP, "_ResetEndpoint: DevAddr.:%u pEPInfo: 0x%x !", pEPInfo->DeviceAddress, pEPInfo->EndpointAddress));
    if (pEPInfo->pPendingUrb != NULL) {
      USBH_WARN((USBH_MCAT_DRIVER_EP, "_ResetEndpoint: Pending URBs!"));
    }
    pEPInfo->NextDataPid = DATA_PID_DATA0;
    Status = USBH_STATUS_SUCCESS;
    break;
  default:
    USBH_LOG((USBH_MCAT_DRIVER_EP, "_ResetEndpoint: invalid endpoint type: %u!", pEPInfo->EndpointType));
    Status = USBH_STATUS_INVALID_PARAM;
    break;
  }
  return Status;
}

/*********************************************************************
*
*       _Ioctl
*
*  Function description
*    IO control function.
*/
static USBH_STATUS _Ioctl(USBH_HC_HANDLE hHostController, unsigned Func, USBH_IOCTL_PARA *pParam) {
  USBH_STATUS           Ret;
  USBH_DWC2_INST      * pInst;
  U32                   Value;

  pInst = USBH_HDL2PTR(USBH_DWC2_INST, hHostController);
  USBH_DWC2_IS_DEV_VALID(pInst);
  switch (Func) {
  case USBH_IOCTL_FUNC_GET_MAX_TRANSFER_SIZE:
    //
    // Returns the maximum transfer size supported by the driver for an URB.
    //
    pParam->u.MaxTransferSize.Size = pInst->MaxTransferSize;
    Ret = USBH_STATUS_SUCCESS;
    break;
  case USBH_IOCTL_FUNC_CONF_MAX_XFER_BUFF_SIZE:
    //
    // Set size for endpoint transfer buffers.
    //
    Value = pParam->u.MaxTransferSize.Size;
    if ((Value & 0x1FFu) != 0u || Value > USBH_DWC2_MAX_TRANSFER_SIZE) {
      Ret = USBH_STATUS_INVALID_PARAM;
      break;
    }
    pInst->MaxTransferSize = Value;
    Ret = USBH_STATUS_SUCCESS;
    break;
  default:
    Ret = USBH_STATUS_INVALID_PARAM;
    break;
  }
  return Ret;
}

/*********************************************************************
*
*       _DWC2_AddUrbIso
*
*  Function description
*    Adds an ISO endpoint request
*/
#if USBH_SUPPORT_ISO_TRANSFER
static USBH_STATUS _DWC2_AddUrbIso(USBH_DWC2_EP_INFO * pEP, USBH_URB * pUrb) {
  USBH_DWC2_CHANNEL_INFO * pChannelInfo;
  USBH_DWC2_INST         * pInst;
  U32                      PacketSize;
  USBH_STATUS              Status;

  EP_VALID(pEP);
  USBH_LOG((USBH_MCAT_DRIVER_URB, "_DWC2_AddUrbIso: EP: 0x%x!", pEP->EndpointAddress));
  pEP->Channel = DWC2_INVALID_CHANNEL;
  USBH_OS_Lock(USBH_MUTEX_DRIVER);
  if (pEP->pPendingUrb == NULL) {
    pEP->pPendingUrb = pUrb;
    Status = USBH_STATUS_SUCCESS;
  } else {
    Status = USBH_STATUS_BUSY;
  }
  USBH_OS_Unlock(USBH_MUTEX_DRIVER);
  if (Status != USBH_STATUS_SUCCESS) {
    return Status;
  }
  pInst = pEP->pInst;
  USBH_DWC2_IS_DEV_VALID(pInst);
  if (pEP->pBuffer == NULL) {
#if defined(USBH_DWC2_CACHE_LINE_SIZE) && USBH_DWC2_USE_DMA != 0
    PacketSize = ((U32)pEP->MaxPacketSize + USBH_DWC2_CACHE_LINE_SIZE - 1uL) & ~(USBH_DWC2_CACHE_LINE_SIZE - 1uL);
    pEP->pBuffer = (U8 *)USBH_TRY_MALLOC_XFERMEM(2u * PacketSize, USBH_DWC2_CACHE_LINE_SIZE);
    USBH_CacheConfig.pfInvalidate(pEP->pBuffer, PacketSize);
#else
    PacketSize = (pEP->MaxPacketSize + 3uL) & ~3uL;
    pEP->pBuffer = (U8 *)USBH_TRY_MALLOC_XFERMEM(2u * PacketSize, 4);
#endif
    if (pEP->pBuffer == NULL) {
      USBH_WARN((USBH_MCAT_DRIVER_URB, "_DWC2_AddUrbIso: No resources for buffer!"));
      return USBH_STATUS_MEMORY;
    }
    pEP->BuffSize = PacketSize;
  }
  pChannelInfo = _DWC2_CHANNEL_Allocate(pInst, pEP);
  if (pChannelInfo == NULL) {
    pEP->pPendingUrb = NULL;
    return USBH_STATUS_NO_CHANNEL;
  }
  pUrb->Request.IsoRequest.NBuffers = 2;
  pEP->BuffBusy                     = 0;
  pChannelInfo->EndpointAddress     = pEP->EndpointAddress;
  if ((pEP->EndpointAddress & 0x80u) != 0u) {
    pEP->BuffReadyList[0] = 1;
    pEP->BuffReadyList[1] = 2;
    pEP->BuffWaitList[0]  = 0;
    pEP->BuffWaitList[1]  = 0;
    _DWC2_CHANNEL_Open(pInst, pChannelInfo);
#if USBH_DWC2_SUPPORT_SPLIT_TRANSACTIONS
    if (pChannelInfo->UseSplitTransactions != 0) {
      _DWC2_CHANNEL_DeAllocate(pInst, pChannelInfo);
      pEP->pPendingUrb = NULL;
      USBH_WARN((USBH_MCAT_DRIVER_URB, "_DWC2_AddUrbIso: Split transactions not supported for ISO transfers"));
      return USBH_STATUS_NOT_SUPPORTED;
    }
#endif
    _DWC2_StartISO(pInst, pEP, pChannelInfo);
  } else {
    pEP->BuffReadyList[0] = 0;
    pEP->BuffReadyList[1] = 0;
    pEP->BuffWaitList[0]  = 1;
    pEP->BuffWaitList[1]  = 2;
    pEP->FirstTimeData    = 1;
  }
  return USBH_STATUS_PENDING;
}
#endif

/*********************************************************************
*
*       _SubmitRequest
*
*  Function description
*    Submit a request to the HC. If USBH_STATUS_PENDING is returned
*    the request is in the queue and the completion routine is
*    called later.
*/
static USBH_STATUS _SubmitRequest(USBH_HC_EP_HANDLE hEndPoint, USBH_URB * pUrb) {
  USBH_STATUS   Status;
  USBH_DWC2_EP_INFO    * pEPInfo;

  pUrb->Header.Status  = USBH_STATUS_PENDING;
  USBH_ASSERT(hEndPoint != NULL);
  pEPInfo = USBH_HDL2PTR(USBH_DWC2_EP_INFO, hEndPoint);
  EP_VALID(pEPInfo);
  switch (pUrb->Header.Function) {
  case USBH_FUNCTION_CONTROL_REQUEST:
    USBH_LOG((USBH_MCAT_DRIVER_URB, "_SubmitRequest: control request!"));
    // Get the endpoint and add this request
    Status = _DWC2_AddUrb2EP0(pEPInfo, pUrb);
    if (Status != USBH_STATUS_PENDING) {
      USBH_WARN((USBH_MCAT_DRIVER_URB, "_SubmitRequest: _AddUrb2EP0 %s", USBH_GetStatusStr(Status)));
    }
    break;
  case USBH_FUNCTION_BULK_REQUEST:
  case USBH_FUNCTION_INT_REQUEST:
    USBH_LOG((USBH_MCAT_DRIVER_URB, "_SubmitRequest: pEPInfo: 0x%x length: %u!", pEPInfo->EndpointAddress, pUrb->Request.BulkIntRequest.Length));
    Status = _DWC2_AddUrb2EPx(pEPInfo, pUrb);
    if (Status != USBH_STATUS_PENDING) {
      USBH_WARN((USBH_MCAT_DRIVER_URB, "_SubmitRequest: _AddUrb2EPx!"));
    }
    break;
#if USBH_SUPPORT_ISO_TRANSFER
  case USBH_FUNCTION_ISO_REQUEST:
    Status = _DWC2_AddUrbIso(pEPInfo, pUrb);
    break;
#endif
  default:
    Status = USBH_STATUS_ERROR;
    USBH_WARN((USBH_MCAT_DRIVER_URB, "_SubmitRequest: invalid USBH_URB function type!"));
    break;
  }
  return Status;
}

/*********************************************************************
*
*       _ServiceISR
*/
static int _ServiceISR(USBH_HC_HANDLE hHostController) {
  U32 Status;
  USBH_DWC2_INST * pInst;

  pInst = USBH_HDL2PTR(USBH_DWC2_INST, hHostController);
  USBH_DWC2_IS_DEV_VALID(pInst);
  //
  // Check whether we have received an interrupt despite the fact
  // that USB interrupt is disabled globally.
  // This seems to occur every 20~35k interrupts and seems to be a
  // silicon bug. In such a case the interrupt is ignored.
  //
  if ((pInst->pHWReg->GAHBCFG & (1uL << 0)) != 0u) {
    Status  = pInst->pHWReg->GINTSTS;
    Status &= pInst->pHWReg->GINTMSK;
    if (Status != 0u) {
      //
      //  Disable the master interrupt.
      //
      if (pInst->DICnt++ == 0) {
        pInst->pHWReg->GAHBCFG &=  ~(1uL << 0);
      }
      return 1;
    }
  }
  return 0;
}

/*********************************************************************
*
*       _ProcessInterrupt
*
*  Function description
*    Is normally called within a task to handle all relevant
*    interrupt sources.
*/
static void _ProcessInterrupt(USBH_HC_HANDLE hHostController) {
  U32 Status;
  USBH_DWC2_INST * pInst;

  pInst = USBH_HDL2PTR(USBH_DWC2_INST, hHostController);
  USBH_DWC2_IS_DEV_VALID(pInst);
  Status  = pInst->pHWReg->GINTSTS;
  Status &= pInst->pHWReg->GINTMSK;
  //
  // Handle SOF int
  //
  if ((Status & START_OF_FRAME_INT) != 0u) {
    pInst->pHWReg->GINTSTS = START_OF_FRAME_INT;
#if USBH_DWC2_SUPPORT_SPLIT_TRANSACTIONS
    _OnSOFSplt(pInst);
    _OnSOF(pInst);
#endif
#if USBH_DWC2_USE_DMA == 0
    _OnSOF(pInst);
#endif
  }
#if USBH_DWC2_USE_DMA == 0
  if ((Status & HOST_RXFLVL) != 0u) {
    U32 RxStatusPop;

    RxStatusPop = pInst->pHWReg->GRXSTSP;
    if (RxStatusPop != 0u) {
      _OnRx(pInst, RxStatusPop);
    }
  }
  if ((Status & HOST_NPTXFE) != 0u) {
    USBH_OS_Lock(USBH_MUTEX_DRIVER);
    pInst->pHWReg->GINTSTS = HOST_NPTXFE;
    if (_FillTXFIFOs(pInst, &pInst->pHWReg->GNPTXSTS, 1) == 0) {
      pInst->pHWReg->GINTMSK &= ~(HOST_NPTXFE);      // Disable non-periodic TxFIFO empty interrupt
    }
    USBH_OS_Unlock(USBH_MUTEX_DRIVER);
  }
  if ((Status & HOST_PTXFE) != 0u) {
    USBH_OS_Lock(USBH_MUTEX_DRIVER);
    pInst->pHWReg->GINTSTS = HOST_PTXFE;
    if (_FillTXFIFOs(pInst, &pInst->pHWReg->HPTXSTS, 2) == 0) {
      pInst->pHWReg->GINTMSK &= ~(HOST_PTXFE);       // Disable periodic TxFIFO empty interrupt
    }
    USBH_OS_Unlock(USBH_MUTEX_DRIVER);
  }
#endif
  if ((Status & HOST_CHANNEL_INT) != 0u) {
    U32 ChannelInt;

    ChannelInt    = pInst->pHWReg->HAINT;
    ChannelInt   &= pInst->pHWReg->HAINTMSK;
    _HandleChannels(pInst, ChannelInt);
  }
  //
  // Handle USB port status int
  //
  if ((Status & HOST_PORT_INT) != 0u) {
    _DWC2_ROOTHUB_HandlePortInt(pInst);
  }
  if ((Status & HOST_DISC_INT) != 0u) {
    pInst->pHWReg->GINTSTS = HOST_DISC_INT;
    pInst->DisconnectDetect = TRUE;
    _DWC2_ROOTHUB_HandlePortInt(pInst);
  }
  //
  // Enable master interrupt
  //
  _DWC2_EnableInterrupts(pInst);
}

/*********************************************************************
*
*       _DWC2_CreateController
*
*  Function description
*    Allocates all needed resources for a host controller device object and calls
*    USBH_AddHostController to link this driver to the next upper driver object.
*
*  Return value a valid Handle or NULL on error.
*/
static USBH_DWC2_INST * _DWC2_CreateController(PTR_ADDR BaseAddress) {
  USBH_DWC2_INST * pHostControllerDev;

  USBH_LOG((USBH_MCAT_DRIVER, "_DWC2_CreateController: BaseAddress: 0x%lx ", BaseAddress));
  USBH_ASSERT(BaseAddress != 0);
  pHostControllerDev = (USBH_DWC2_INST *)USBH_MALLOC_ZEROED(sizeof(USBH_DWC2_INST));
  USBH_IFDBG(pHostControllerDev->Magic = USBH_DWC2_INST_MAGIC);
  pHostControllerDev->pHWReg = (USBH_DWC2_HWREGS *)BaseAddress;                 //lint !e923 !e9078  D:103[a]
  pHostControllerDev->pFifoRegBase = (volatile U32 *)(BaseAddress + 0x1000u);   //lint !e923 !e9078 !e9033 D:103[a]
  USBH_InitTimer(&pHostControllerDev->ChannelCheckTimer, _OnChannelCheck, pHostControllerDev);
  return pHostControllerDev;
}

/*********************************************************************
*
*       _DWC2_CHANNEL_Allocate
*/
static USBH_DWC2_CHANNEL_INFO * _DWC2_CHANNEL_Allocate(USBH_DWC2_INST * pInst, USBH_DWC2_EP_INFO * pEP) {
  unsigned Channel;
  USBH_DWC2_CHANNEL_INFO * pChannelInfo;
  USBH_DWC2_HCCHANNEL    * pHwChannel;

  if (pEP->EndpointType == USB_EP_TYPE_CONTROL) {
    Channel = 0;
    pChannelInfo = &pInst->aChannelInfo[0];
  } else {
    Channel = 1;
    pChannelInfo = &pInst->aChannelInfo[1];
  }
  pHwChannel   = &pInst->pHWReg->aHChannel[Channel];
  USBH_OS_Lock(USBH_MUTEX_DRIVER);
  for (; Channel < DWC2_NUM_CHANNELS; Channel++) {
    if (pChannelInfo->InUse == FALSE && (pHwChannel->HCCHAR & HCCHAR_CHENA) == 0u) {
      pChannelInfo->InUse   = TRUE;
      pChannelInfo->pEPInfo = pEP;
      pChannelInfo->Channel = Channel;
      pChannelInfo->pHWChannel = &pInst->pHWReg->aHChannel[Channel];
      pHwChannel->HCINT = CHANNEL_MASK;
      pInst->UsedChannelMask |= (1uL << Channel);
      pEP->Channel = Channel;
      USBH_OS_Unlock(USBH_MUTEX_DRIVER);
      return pChannelInfo;
    }
    pChannelInfo++;
    pHwChannel++;
  }
  USBH_OS_Unlock(USBH_MUTEX_DRIVER);
  USBH_WARN((USBH_MCAT_DRIVER_EP, "_DWC2_CHANNEL_Allocate: No free channels!"));
  return NULL;
}

/*********************************************************************
*
*       _DWC2_CHANNEL_DeAllocate
*/
static void _DWC2_CHANNEL_DeAllocate(USBH_DWC2_INST * pInst, USBH_DWC2_CHANNEL_INFO * pChannel) {
  pChannel->pHWChannel->HCINTMSK = 0;
  pChannel->NumBytes2Transfer    = 0;
  pChannel->NumBytesTransferred  = 0;
  pChannel->ToBePushed           = 0;
  pInst->UsedChannelMask        &= ~(1uL << pChannel->Channel);
#if USBH_DWC2_USE_DMA == 0
  pInst->ReStartChannelMask &= pInst->UsedChannelMask;
#endif
#if USBH_DWC2_SUPPORT_SPLIT_TRANSACTIONS
  pInst->StartChannelMask &= pInst->UsedChannelMask;
#endif
  if (pChannel->TimerInUse != FALSE) {
    USBH_ReleaseTimer(&pChannel->IntervalTimer);
    pChannel->TimerInUse = FALSE;
  }
  pChannel->InUse        = FALSE;
}

/*********************************************************************
*
*       _DWC2_DisableInterrupts
*
*  Function description
*    Disables the USB interrupts if necessary.
*    If we are in the interrupt handling task USB interrupts
*    are already disabled - it is not necessary to do it once more.
*    So we only have to handle this when the CPU is not
*    in the Interrupt handling task.
*
*  Parameters
*    pInst   : Pointer to the host driver instance.
*/
static void _DWC2_DisableInterrupts(USBH_DWC2_INST * pInst) {
  //
  // Make this operation atomic
  //
  USBH_OS_DisableInterrupt();
  //
  // The enable interrupt values are only saved
  // with the first call of _DWC2_DisableInterrupts()
  //
  if (pInst->DICnt++ == 0) {
    pInst->pHWReg->GAHBCFG &=  ~(1uL << 0);
  }
  USBH_OS_EnableInterrupt();
}

/*********************************************************************
*
*       _DWC2_EnableInterrupts
*
*  Function description
*    Enables the USB interrupts if necessary.
*    If we are in the interrupt handling task USB interrupts
*    are already disabled - it is not necessary to do it once more.
*    So we only have to handle this when the CPU is not
*    in the Interrupt handling task.
*
*  Parameters
*    pInst   : Pointer to the host driver instance.
*/
static void _DWC2_EnableInterrupts(USBH_DWC2_INST * pInst) {
  //
  // Make this operation atomic
  //
  USBH_OS_DisableInterrupt();
  //
  // Make sure this routine is re-entrant
  //
  --pInst->DICnt;
  if (pInst->DICnt < 0) {
    USBH_PANIC("USBH_DWC2 Driver: _DWC2_EnableInterrupts called multiple times without _DWC2_DisableInterrupts()");
  }
  if (pInst->DICnt == 0) {
    //
    // The enable interrupt values are only restored
    // with the last call of _DWC2_EnableInterrupts()
    //
    pInst->pHWReg->GAHBCFG |= (1uL << 0);
  }
  USBH_OS_EnableInterrupt();
}

/*********************************************************************
*
*       _DWC2_StartISO
*/
#if USBH_SUPPORT_ISO_TRANSFER
static void _DWC2_StartISO(USBH_DWC2_INST * pInst, USBH_DWC2_EP_INFO * pEP, USBH_DWC2_CHANNEL_INFO * pChannelInfo) {
  //
  // Get next buffer from ready list
  //
  pEP->BuffBusy                     = pEP->BuffReadyList[0];
  pEP->BuffReadyList[0]             = pEP->BuffReadyList[1];
  pEP->BuffReadyList[1]             = 0;
  pChannelInfo->NumBytesPushed      = 0;
  pChannelInfo->NumBytesTransferred = 0;
  pChannelInfo->pBuffer             = pEP->pBuffer;
  if (pEP->BuffBusy == 2) {
    pChannelInfo->pBuffer += pEP->BuffSize;
  }
  if ((pEP->EndpointAddress & 0x80u) == 0u) {
    pChannelInfo->NumBytesTotal     = pEP->BuffReadySize[0];
    pEP->BuffReadySize[0]           = pEP->BuffReadySize[1];
  }
  pChannelInfo->Status = USBH_STATUS_SUCCESS;
  _DWC2_CHANNEL_StartTransfer(pInst, pChannelInfo);
}
#endif

/*********************************************************************
*
*       _IsoDataCtrl
*
*  Function description
*    Acknowledge IN data or provide OUT data for ISO EPs.
*/
#if USBH_SUPPORT_ISO_TRANSFER
static USBH_STATUS _IsoDataCtrl(USBH_HC_EP_HANDLE hEndPoint, USBH_ISO_DATA_CTRL *pIsoData) {
  USBH_DWC2_EP_INFO    * pEPInfo;
  I8                     BuffNo;
  USBH_STATUS            Status;
  U8                   * pBuffer;
  U32                    Length;
  U8                     IsInDir;
  USBH_DWC2_CHANNEL_INFO * pChannelInfo;

  pEPInfo = USBH_HDL2PTR(USBH_DWC2_EP_INFO, hEndPoint);
  EP_VALID(pEPInfo);
  USBH_OS_Lock(USBH_MUTEX_DRIVER);
  if (pEPInfo->pPendingUrb == NULL) {
    Status = USBH_STATUS_INVALID_PARAM;
    goto Done;
  }
  Length = 0;
  IsInDir = pEPInfo->EndpointAddress & 0x80u;
  if (IsInDir == 0u) {
    Length = pIsoData->Length + pIsoData->Length2;
    if (Length > pEPInfo->MaxPacketSize) {
      Status = USBH_STATUS_LENGTH;
      goto Done;
    }
  }
  BuffNo = pEPInfo->BuffWaitList[0];
  if (BuffNo == 0 || pEPInfo->BuffReadyList[1] != 0) {
    Status = USBH_STATUS_BUSY;
    goto Done;
  }
  //
  // Remove buffer from wait list
  //
  pEPInfo->BuffWaitList[0] = pEPInfo->BuffWaitList[1];
  pEPInfo->BuffWaitList[1] = 0;
  //
  // Append buffer to ready list
  //
  if (pEPInfo->BuffReadyList[0] == 0) {
    pEPInfo->BuffReadyList[0] = BuffNo;
    pEPInfo->BuffReadySize[0] = Length;
  } else {
    pEPInfo->BuffReadyList[1] = BuffNo;
    pEPInfo->BuffReadySize[1] = Length;
  }
  pBuffer = pEPInfo->pBuffer;
  if (BuffNo == 2) {
    pBuffer += pEPInfo->BuffSize;
  }
  pIsoData->pBuffer = pBuffer;
  Status = USBH_STATUS_SUCCESS;
  if (IsInDir == 0u) {
    USBH_MEMCPY(pBuffer, pIsoData->pData, pIsoData->Length);
    if (pIsoData->Length2 != 0u) {
      USBH_MEMCPY(pBuffer + pIsoData->Length, pIsoData->pData2, pIsoData->Length2);
    }
#if defined(USBH_DWC2_CACHE_LINE_SIZE) && USBH_DWC2_USE_DMA != 0
    USBH_CacheConfig.pfClean(pBuffer, Length);
#endif
    if (pEPInfo->FirstTimeData != 0) {
      pEPInfo->FirstTimeData = 0;
      pChannelInfo = &pEPInfo->pInst->aChannelInfo[pEPInfo->Channel];
      _DWC2_CHANNEL_Open(pEPInfo->pInst, pChannelInfo);
      Status = USBH_STATUS_NEED_MORE_DATA;
#if USBH_DWC2_SUPPORT_SPLIT_TRANSACTIONS
      if (pChannelInfo->UseSplitTransactions != 0) {
        USBH_WARN((USBH_MCAT_DRIVER_URB, "_IsoDataCtrl: Split transactions not supported for ISO transfers"));
        Status = USBH_STATUS_NOT_SUPPORTED;
      }
#endif
      goto Done;
    }
    if (pEPInfo->BuffWaitList[0] != 0) {
      Status = USBH_STATUS_NEED_MORE_DATA;
    }
  }
  if (pEPInfo->BuffBusy == 0) {
    pChannelInfo = &pEPInfo->pInst->aChannelInfo[pEPInfo->Channel];
    _DWC2_StartISO(pEPInfo->pInst, pEPInfo, pChannelInfo);
  }
Done:
  USBH_OS_Unlock(USBH_MUTEX_DRIVER);
  return Status;
}
#endif /* USBH_SUPPORT_ISO_TRANSFER */

static const USBH_HOST_DRIVER _DWC2_Driver = {
  _HostInit,
  _HostExit,
  _SetHcState,
  _GetFrameNumber,
  _AddEndpoint,
  _ReleaseEndpoint,
  _AbortEndpoint,
  _ResetEndpoint,
  _SubmitRequest,
  _DWC2_ROOTHUB_GetPortCount,
  _DWC2_ROOTHUB_GetHubStatus,
  _DWC2_ROOTHUB_GetPortStatus,
  _DWC2_ROOTHUB_SetPortPower,
  _DWC2_ROOTHUB_ResetPort,
  _DWC2_ROOTHUB_DisablePort,
  _DWC2_ROOTHUB_SetPortSuspend,
  _ServiceISR,
  _ProcessInterrupt,
  _Ioctl,
#if USBH_SUPPORT_ISO_TRANSFER
  _IsoDataCtrl
#else
  NULL
#endif
};

/*********************************************************************
*
*       _DWC2_Add
*
*  Function description
*/
static U32 _DWC2_Add(void * pBase, U8 PhyType) {
  USBH_DWC2_INST * pInst;
  U32              HCIndex;

  USBH_LOG((USBH_MCAT_DRIVER, "_DWC2_Add!"));
  pInst = _DWC2_CreateController(SEGGER_PTR2ADDR(pBase));   // lint D:103[a]
  pInst->PhyType = PhyType;
  pInst->pHostController = USBH_AddHostController(&_DWC2_Driver, pInst, USBH_DWC2_MAX_USB_ADDRESS, &HCIndex);
#ifdef USBH_DWC2_CACHE_LINE_SIZE
  if (USBH_CacheConfig.pfClean == NULL ||
      USBH_CacheConfig.pfInvalidate == NULL ||
      USBH_CacheConfig.CacheLineSize != USBH_DWC2_CACHE_LINE_SIZE) {
    USBH_PANIC("Bad cache configuration");
  }
#endif
  return HCIndex;
}

#else

/*********************************************************************
*
*       USBH_HW_DWC2_c
*
*  Function description
*    Dummy function to avoid problems with certain compilers which
*    can not handle empty object files.
*/
void USBH_HW_DWC2_c(void);
void USBH_HW_DWC2_c(void) {
}

#endif /* USBH_HW_DWC2_C_ */

/********************************* EOF*******************************/
