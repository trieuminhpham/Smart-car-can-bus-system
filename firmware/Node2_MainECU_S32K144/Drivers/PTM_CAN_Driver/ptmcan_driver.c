#include "ptmcan_driver.h"

/*==========================================================
 * 1. MA TRANG THAI CUA MESSAGE BUFFER TRONG CS WORD
 *==========================================================*/

#define CAN_MB_CODE_RX_INACTIVE         (0x0U)      // MB nhan khong hoat dong
#define CAN_MB_CODE_RX_BUSY             (0x1U)      // Module dang ghi de vao MB
#define CAN_MB_CODE_RX_FULL             (0x2U)      // Da nhan duoc frame moi
#define CAN_MB_CODE_RX_EMPTY            (0x4U)      // MB ranh, dang cho nhan frame
#define CAN_MB_CODE_RX_OVERRUN          (0x6U)      // Nhan frame moi de len frame cu chua doc

#define CAN_MB_CODE_TX_INACTIVE         (0x8U)      // MB truyen ranh
#define CAN_MB_CODE_TX_ABORT            (0x9U)      // Yeu cau truyen bi huy
#define CAN_MB_CODE_TX_DATA             (0xCU)      // Kich hoat truyen Data frame
#define CAN_MB_CODE_TX_REMOTE           (0x1CU)     // Kich hoat truyen remote frame

// Bang tra cuu Base Address va PCC Index cua 3 kenh CAN
static CAN_Type * const g_canBase[] = { CAN0, CAN1, CAN2 };
static const uint32_t g_pccCanIndex[] = {
    PCC_FlexCAN0_INDEX,
    PCC_FlexCAN1_INDEX,
    PCC_FlexCAN2_INDEX
};

// Luu tru con tro Callback cua tang ung dung cho tung kenh
static ptmcan_callback_t s_rxCallback[3] = { NULL, NULL, NULL };

/*==========================================================
 * 2. HAM PHU TRO (HELFER FUNCTIONS)
 *==========================================================*/

 /**
  * @brief Dao byte 32-bit (Big-Endian <-> Little-Endian)
  * Giup du lieu Byte 0 cua mang nam o byte cao nhat tren bus CAN
  */
static inline uint32_t ptmcan_swap_bytes(uint32_t val)
{
    return (((val & 0x000000FFU) << 24U) |
            ((val & 0x0000FF00U) << 8U)  |
            ((val & 0x00FF0000U) >> 8U)  |
            ((val & 0xFF000000U) >> 24U));
}

/**
 * @brief Yeu cau PTMCAN di vao Freeze Mode va cho phan cung xac nhan (FRZACK = 1)
 */
static status_t ptmcan_enter_freeze_mode (CAN_Type *base)
{
    uint32_t timeout = 100000U;

    base->MCR |= CAN_MCR_FRZ_MASK | CAN_MCR_HALT_MASK;

    while (!(base->MCR & CAN_MCR_FRZACK_MASK))
    {
        if (--timeout == 0U)
        {
            return STATUS_TIMEOUT;
        }
    }
    return STATUS_SUCCESS;
}

/**
 * @brief Thoat Freeze Mode va cho PTMCAN san sang hoat dong (FRZACK = 0, NOTRDY = 0)
 */
static status_t ptmcan_exit_freeze_mode(CAN_Type *base)
{
    uint32_t timeout = 100000U;

    base->MCR &= ~(CAN_MCR_FRZ_MASK | CAN_MCR_HALT_MASK);

    while ((base->MCR & CAN_MCR_FRZACK_MASK) || (base->MCR & CAN_MCR_NOTRDY_MASK))
    {
        if (--timeout == 0U)
        {
            return STATUS_TIMEOUT;
        }
    }
    return STATUS_SUCCESS;
}

/*==========================================================
 * 3. CAC API CHINH CUA DRIVER
 *==========================================================*/

 /**
  * @brief Ham khoi tao cho CAN
  */
status_t PTMCAN_Init(uint8_t instance, const ptmcan_user_config_t *config)
{
    if ((instance >= 3U) || (config == NULL))
    {
        return STATUS_ERROR;
    }

    CAN_Type *base = g_canBase[instance];

    // 1. Cap clock cho module FlexCAN thong qua PCC
    PCC->PCCn[g_pccCanIndex[instance]] |= PCC_PCCn_CGC_MASK;

    // 2. Chon nguon clock cho Protocol Engine (Chi duoc ghi khi MDIS = 1)
    base->MCR |= CAN_MCR_MDIS_MASK;
    base->CTRL1 = (base->CTRL1 & ~ CAN_CTRL1_CLKSRC_MASK) |
                  CAN_CTRL1_CLKSRC(config->clk_src);

    // 3. Bat module PTMCAN (MDIS = 0) va cho thoat Low-Power Mode
    base->MCR &= ~CAN_MCR_MDIS_MASK;
    while (base->MCR & CAN_MCR_LPMACK_MASK){}

    // 4. Soft Reset de xoa sach cau hinh cu
    base->MCR |= CAN_MCR_SOFTRST_MASK;
    while (base->MCR & CAN_MCR_SOFTRST_MASK){}

    // 5. Di vao freeze mode de cau hinh thanh ghi can
    if (ptmcan_enter_freeze_mode(base) != STATUS_SUCCESS)
    {
        return STATUS_TIMEOUT;
    }

    /* 6. Cau hinh MCR
     * - MAXMB = max_num_mb - 1
     * - IRMQ = 1 (Dung loc rieng tung MB qua RXIMR)
     * - SRXDIS = 1 (Tat tu nhan lai tin minh gui)
     * - AEN (Cho phep huy truyen theo cau hinh)
     */
    uint32_t maxmb_val = (config->max_num_mb > 0U) ? (config->max_num_mb - 1U) : 15U;

    base->MCR = (base->MCR & ~(CAN_MCR_MAXMB_MASK | CAN_MCR_IRMQ_MASK |
                                CAN_MCR_SRXDIS_MASK | CAN_MCR_AEN_MASK))
                | CAN_MCR_MAXMB(maxmb_val)
                | CAN_MCR_IRMQ(1U)
                | CAN_MCR_SRXDIS(1U)
                | CAN_MCR_AEN(config->enable_abort ? 1U : 0U);

    // 7. Cau hinh Bit Timing vao thanh ghi CTRL1
    base->CTRL1 = (base->CTRL1 &~ (CAN_CTRL1_PRESDIV_MASK | CAN_CTRL1_PROPSEG_MASK |
                                   CAN_CTRL1_PSEG1_MASK | CAN_CTRL1_PSEG2_MASK |
                                   CAN_CTRL1_RJW_MASK | CAN_CTRL1_LPB_MASK))
                | CAN_CTRL1_PRESDIV(config->bitrate.prediv)
                | CAN_CTRL1_PROPSEG(config->bitrate.propseg)
                | CAN_CTRL1_PSEG1(config->bitrate.pseg1)
                | CAN_CTRL1_PSEG2(config->bitrate.pseg2)
                | CAN_CTRL1_RJW(config->bitrate.rjw)
                | CAN_CTRL1_LPB(config->enable_loopback ? 1U : 0U);

    // 8. Xoa sach vung nho Message Buffers RAM (128 words tuong duong 32 MBs)
    for (uint8_t i = 0U; i < CAN_RAMn_COUNT; i++)
    {
        base->RAMn[i] = 0U;
    }

    // Tat tat ca ngat MB va xoa co ngat ton dong
    base->IMASK1 = 0x00000000U;
    base->IFLAG1 = 0xFFFFFFFFU;

    // 9. bat ngat NVIC neu nguoi dung yeu cau
    if (config->enable_rx_interrupt)
    {
        if (instance == 0U)
        {
            S32_NVIC->ISER[CAN0_ORed_0_15_MB_IRQn / 32U] = (1U << (CAN0_ORed_0_15_MB_IRQn % 32U));
            S32_NVIC->ISER[CAN0_ORed_16_31_MB_IRQn / 32U] = (1U << (CAN0_ORed_16_31_MB_IRQn % 32U));
        }
        else if (instance == 1U)
        {
            S32_NVIC->ISER[CAN1_ORed_0_15_MB_IRQn / 32U] = (1U << (CAN1_ORed_0_15_MB_IRQn % 32U));
        }
        else if (instance == 2U)
        {
            S32_NVIC->ISER[CAN2_ORed_0_15_MB_IRQn / 32U] = (1U << (CAN2_ORed_0_15_MB_IRQn % 32U));
        }
    }

    // 10. Thoat Freeze Mode de bat dau lam viec
    return ptmcan_exit_freeze_mode(base);

}

/**
 * @brief Ham huy cho CAN
 */
status_t PTMCAN_DeInit(uint8_t instance)
{
    if (instance >= 3U) return STATUS_ERROR;
    CAN_Type *base = g_canBase[instance];

    // Ngat NVIC
    if (instance == 0U)
    {
        S32_NVIC->ICER[CAN0_ORed_0_15_MB_IRQn / 32U] = (1U << (CAN0_ORed_0_15_MB_IRQn % 32U));
        S32_NVIC->ICER[CAN0_ORed_16_31_MB_IRQn / 32U] = (1U << (CAN0_ORed_16_31_MB_IRQn % 32U));
    }
    else if (instance == 1U)
    {
        S32_NVIC->ICER[CAN1_ORed_0_15_MB_IRQn / 32U] = (1U << (CAN1_ORed_0_15_MB_IRQn % 32U));
    }
    else if (instance == 2U)
    {
        S32_NVIC->ICER[CAN2_ORed_0_15_MB_IRQn / 32U] = (1U << (CAN2_ORed_0_15_MB_IRQn % 32U));
    }

    // Tat module (MDIS = 1) va tat clock PCC
    base->MCR |= CAN_MCR_MDIS_MASK;
    PCC->PCCn[g_pccCanIndex[instance]] &= ~PCC_PCCn_CGC_MASK;

    return STATUS_SUCCESS;
}

/**
 * @brief Ham cau hinh Rx Message Buffer
 */
status_t PTMCAN_ConfigRxMB(uint8_t instance, uint8_t mb_idx, uint32_t msg_id)
{
    if ((instance >= 3U) || (mb_idx >=32U)) return STATUS_ERROR;
    CAN_Type *base = g_canBase[instance];
    volatile uint32_t *mb = &base->RAMn[mb_idx * 4U];

    // 1. Dua MB ve INACTIVE truoc khi sua
    mb[0] = (CAN_MB_CODE_RX_INACTIVE << 24U);

    // 2. Cai dat 11 bit Standard ID (dich trai 18 bit, IDE = 0)
    mb[1] = ((msg_id & PTMCAN_STD_ID_MASK) << 18U);

    // 3. Dat mat na loc mac dinh, khop chinh xac 100% ID
    PTMCAN_SetRxMask(instance, mb_idx, PTMCAN_MASK_EXACT_MATCH);

    // 4. Kich hoat MB o trang thai ranh san sangf don ban tin (RX_EMPTY = 0x4)
    mb[0] = (CAN_MB_CODE_RX_EMPTY << 24U);

    // Bat co cho phep ngat cua MB nay
    base->IMASK1 |= (1U << mb_idx);

    return STATUS_SUCCESS;
}

/**
 * @brief Ham cau hinh Tx Message Buffer
 */
status_t PTMCAN_ConfigTxMB(uint8_t instance, uint8_t mb_idx)
{
    if ((instance >= 3U) || (mb_idx >= 32U)) return STATUS_ERROR;
    CAN_Type *base = g_canBase[instance];
    volatile uint32_t *mb = &base->RAMn[mb_idx * 4U];

    mb[0] = (CAN_MB_CODE_TX_INACTIVE << 24U);
    mb[1] = 0U;
    mb[2] = 0U;
    mb[3] = 0U;

    base->IMASK1 &= ~(1U << mb_idx);

    return STATUS_SUCCESS;
}

/**
 * @brief Cai dat mat na loc rieng (RXIMR) cho mot hop thu nhan
 */
status_t PTMCAN_SetRxMask(uint8_t instance, uint8_t mb_idx, uint32_t mask)
{
    if ((instance >= 3U) || (mb_idx >= 32U)) return STATUS_ERROR;
    CAN_Type *base = g_canBase[instance];

    // Vao Freeze Mode de ghi thanh ghi RXIMR
    if (ptmcan_enter_freeze_mode(base) != STATUS_SUCCESS)
    {
        return STATUS_TIMEOUT;
    }

    // Standard ID Mask phai nam o bits 28...18
    base->RXIMR[mb_idx] = ((mask & PTMCAN_STD_ID_MASK) << 18U);

    // Thoat Freeze Mode
    return ptmcan_exit_freeze_mode(base);
}

/**
 * @brief Ham gui yeu cau truyen ban tin CAN
 */
status_t PTMCAN_Transmit(uint8_t instance, uint8_t mb_idx, const ptmcan_msgbuff_t *tx_msg)
{
    if ((instance >= 3U) || (mb_idx >= 32U) || (tx_msg == NULL)) return STATUS_ERROR;
    if (tx_msg->data_len > PTMCAN_MAX_PAYLOAD_SIZE) return STATUS_ERROR;

    CAN_Type *base = g_canBase[instance];
    volatile uint32_t *mb = &base->RAMn[mb_idx * 4U];

    // Kiem tra xem MB truoc do da gui xong chua
    if (PTMCAN_IsMBBusy(instance, mb_idx))
    {
        return STATUS_BUSY;
    }

    // 1. Dat CODE = INACTIVE truoc khi cap nhat noi dung
    mb[0] = (CAN_MB_CODE_TX_INACTIVE << 24U);

    // 2. Cai dat ID (Standard 11-bits)
    mb[1] = ((tx_msg->msg_id & PTMCAN_STD_ID_MASK) << 18U);

    // 3. Nap du lieu Data payload (dao byte de tuong thich Big-Endian bus)
    uint32_t data_w0 = 0U;
    uint32_t data_w1 = 0U;

    for (uint8_t i = 0U; i < 4U; i++)
    {
        if (i < tx_msg->data_len)
        {
            data_w0 |= ((uint32_t)tx_msg->data[i] << (24U - (i * 8U)));
        }
    }
    for (uint8_t i = 4U; i < 8U; i++)
    {
        if (i < tx_msg->data_len)
        {
            data_w1 |= ((uint32_t)tx_msg->data[i] << (24U - ((i - 4U) * 8U)));
        }
    }
    mb[2] = data_w0;
    mb[3] = data_w1;

    base->IFLAG1 = (1U << mb_idx);
    // 4. Kich hoat truyen: CODE = TX_DATA (0xC), DLC, IDE = 0, RTR = 0
    mb[0] = (CAN_MB_CODE_TX_DATA << 24U) | ((uint32_t)tx_msg->data_len << 16U);

    return STATUS_SUCCESS;
}

/**
 * @brief Ham nhan CAN
 */
status_t PTMCAN_Receive(uint8_t instance, uint8_t mb_idx, ptmcan_msgbuff_t *rx_msg)
{
    if ((instance >= 3U) || (mb_idx >= 32U) || (rx_msg == NULL)) return STATUS_ERROR;

    CAN_Type *base = g_canBase[instance];
    volatile uint32_t *mb = &base->RAMn[mb_idx * 4U];

    // 1. Doc CS word (Thao tac nay khoa MB lai)
    uint32_t cs = mb[0];
    uint8_t code = (uint8_t)((cs >> 24U) & 0x0FU);

    // Kiem tra xem MB co nhan tin moi khong (RX_FULL = 0x2 hoac RX_OVERRUN = 0x6)
    if ((code != CAN_MB_CODE_RX_FULL) && (code != CAN_MB_CODE_RX_OVERRUN))
    {
        return STATUS_ERROR;
    }

    // 2. Doc CAN ID
    rx_msg->msg_id = (mb[1] >> 18U) & PTMCAN_STD_ID_MASK;

    // 3. Doc do dai du lieu (DLC)
    rx_msg->data_len = (uint8_t)((cs >> 16U) & 0x0FU);
    if (rx_msg->data_len > PTMCAN_MAX_PAYLOAD_SIZE)
    {
        rx_msg->data_len = PTMCAN_MAX_PAYLOAD_SIZE;
    }

    // 4. Doc du lieu ra mang (Giai ma Big-Endian)
    uint32_t data_w0 = mb[2];
    uint32_t data_w1 = mb[3];

    for (uint8_t i = 0U; i < 4U; i++)
    {
        rx_msg->data[i] = (uint8_t)((data_w0 >> (24U - (i * 8U))) & 0xFFU);
    }
    for (uint8_t i = 4U; i < 8U; i++)
    {
        rx_msg->data[i] = (uint8_t)((data_w1 >> (24U - ((i - 4U) * 8U))) & 0xFFU);
    }

    // 5. Doc thanh ghi timer de unlock MB
    (void)base->TIMER;

    // 6. Xoa co ngat IFLAG1 (Ghi 1 de xoa - W1C)
    base->IFLAG1 = (1U << mb_idx);

    return STATUS_SUCCESS;
}

/**
 * @brief Ham kiem tra lieu MB co ban khong?
 */
bool PTMCAN_IsMBBusy(uint8_t instance, uint8_t mb_idx)
{
    if ((instance >= 3U) || (mb_idx >= 32U)) return false;
    CAN_Type *base = g_canBase[instance];
    volatile uint32_t *mb = &base->RAMn[mb_idx * 4U];

    uint8_t code = (uint8_t)((mb[0] >> 24U) & 0x0FU);

    // MB ban neu code la TX_DATA (Dang cho gui)
    return (code == CAN_MB_CODE_TX_DATA);
}

/**
 * @brief Ham khoa ABORT cho Message Buffer
 */
status_t PTMCAN_AbortTx(uint8_t instance, uint8_t mb_idx)
{
    if ((instance >= 3U) || (mb_idx >= 32U)) return STATUS_ERROR;
    CAN_Type *base = g_canBase[instance];
    volatile uint32_t *mb = &base->RAMn[mb_idx * 4U];

    // Ghi ma ABORT (0x9) vao truong code cua MB
    mb[0] = (CAN_MB_CODE_TX_ABORT << 24U);

    return STATUS_SUCCESS;
}

// Cac ham xu ly ngat cho PTMCAN

void PTMCAN_RegisterRxCallback(uint8_t instance, ptmcan_callback_t callback)
{
    if (instance < 3U)
    {
        s_rxCallback[instance] = callback;
    }
}

/**
 * @brief Trinh phuc vu ngat chung cho cac MB tu 0->15 cua PTMCAN0
 */
void CAN0_ORed_0_15_MB_IRQHandler(void)
{
    uint32_t iflag = CAN0->IFLAG1 & CAN0->IMASK1; // Chi quan tam cac MB co bat ngat
    ptmcan_msgbuff_t rx_frame;

    for (uint8_t mb_idx = 0U; mb_idx < 16U; mb_idx++)
    {
        if (iflag & (1U << mb_idx))
        {
            // Thu doc du lieu tu mb
            if (PTMCAN_Receive(0U, mb_idx, &rx_frame) == STATUS_SUCCESS)
            {
                // Bao len Callback tang ung dung
                if (s_rxCallback[0] != NULL)
                {
                    s_rxCallback[0](0U, mb_idx, &rx_frame);
                }
            }
        }
    }
}

void CAN0_ORed_16_31_MB_IRQHandler(void)
{
    uint32_t iflag = CAN0->IFLAG1 & CAN0->IMASK1; // Chi quan tam cac MB co bat ngat
    ptmcan_msgbuff_t rx_frame;

    for (uint8_t mb_idx = 16U; mb_idx < 32U; mb_idx++)
    {
        if (iflag & (1U << mb_idx))
        {
            // Thu doc du lieu tu mb
            if (PTMCAN_Receive(0U, mb_idx, &rx_frame) == STATUS_SUCCESS)
            {
                // Bao len Callback tang ung dung
                if (s_rxCallback[0] != NULL)
                {
                    s_rxCallback[0](0U, mb_idx, &rx_frame);
                }
            }
        }
    }
}

/**
 * @brief Trinh phuc vu ngat chung cho cac MB tu 0->15 cua PTMCAN1
 */
void CAN1_ORed_0_15_MB_IRQHandler(void)
{
    uint32_t iflag = CAN1->IFLAG1 & CAN1->IMASK1; // Chi quan tam cac MB co bat ngat
    ptmcan_msgbuff_t rx_frame;

    for (uint8_t mb_idx = 0U; mb_idx < 16U; mb_idx++)
    {
        if (iflag & (1U << mb_idx))
        {
            // Thu doc du lieu tu mb
            if (PTMCAN_Receive(1U, mb_idx, &rx_frame) == STATUS_SUCCESS)
            {
                // Bao len Callback tang ung dung
                if (s_rxCallback[1] != NULL)
                {
                    s_rxCallback[1](1U, mb_idx, &rx_frame);
                }
            }
        }
    }
}

/**
 * @brief Trinh phuc vu ngat chung cho cac MB tu 0->15 cua PTMCAN2
 */
void CAN2_ORed_0_15_MB_IRQHandler(void)
{
    uint32_t iflag = CAN2->IFLAG1 & CAN2->IMASK1; // Chi quan tam cac MB co bat ngat
    ptmcan_msgbuff_t rx_frame;

    for (uint8_t mb_idx = 0U; mb_idx < 16U; mb_idx++)
    {
        if (iflag & (1U << mb_idx))
        {
            // Thu doc du lieu tu mb
            if (PTMCAN_Receive(2U, mb_idx, &rx_frame) == STATUS_SUCCESS)
            {
                // Bao len Callback tang ung dung
                if (s_rxCallback[2] != NULL)
                {
                    s_rxCallback[2](2U, mb_idx, &rx_frame);
                }
            }
        }
    }
}

// Cac ham chan doan va debug

/**
 * @brief Ham lay ma loi cua mang CAN (Bang cach doc thanh ghi ESR1)
 */
uint32_t PTMCAN_GetErrorStatus(uint8_t instance)
{
    if (instance >= 3U) return 0xFFFFFFFFU;
    return g_canBase[instance]->ESR1;
}

/**
 * @brief Ham phuc hoi Bus CAN bi OFF
 */
status_t PTMCAN_RecoverBusOff(uint8_t instance)
{
    if (instance >= 3U) return STATUS_ERROR;
    CAN_Type *base = g_canBase[instance];

    // Xoa co BOFFINT trong ESR1 (W1C)
    base->ESR1 = CAN_ESR1_BOFFINT_MASK;

    // Tam thoi chuyen sang Freeze Mode roi quay lai Normal Mode de reset trang thai bus
    if (ptmcan_enter_freeze_mode(base) != STATUS_SUCCESS)
    {
        return STATUS_TIMEOUT;
    }
    return ptmcan_exit_freeze_mode(base);
}

status_t PTMCAN_SetLoopbackMode(uint8_t instance, bool enable)
{
    if (instance >= 3U) return STATUS_ERROR;
    CAN_Type *base = g_canBase[instance];

    // Bat tat Loopback, yeu cau phai vao freeze mode
    if (ptmcan_enter_freeze_mode(base) != STATUS_SUCCESS)
    {
        return STATUS_TIMEOUT;
    }

    if (enable)
    {
        base->CTRL1 |= CAN_CTRL1_LPB_MASK;
    }
    else
    {
        base->CTRL1 &= ~CAN_CTRL1_LPB_MASK;
    }

    return ptmcan_exit_freeze_mode(base);
}
