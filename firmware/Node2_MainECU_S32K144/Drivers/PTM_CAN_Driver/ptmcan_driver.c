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

#define PTMCAN_NUM_INSTANCES            (3U)
#define PTMCAN_TIMEOUT_LOOPS            (100000U)
#define PTMCAN_BUSOFF_TIMEOUT_LOOPS     (2000000U)  // >> 128 x 11 bit recessive o 500 kbps
#define PTMCAN_RX_BUSY_RETRY            (1000U)

// Bang tra cuu Base Address va PCC Index cua 3 kenh CAN
static CAN_Type * const g_canBase[PTMCAN_NUM_INSTANCES] = { CAN0, CAN1, CAN2 };
static const uint32_t g_pccCanIndex[PTMCAN_NUM_INSTANCES] = {
    PCC_FlexCAN0_INDEX,
    PCC_FlexCAN1_INDEX,
    PCC_FlexCAN2_INDEX
};

static const uint8_t g_canMaxMb[PTMCAN_NUM_INSTANCES] = { 32U, 16U, 16U };

// IRQ cua nhom MB 0..15 cua tung kenh (CAN0 con them nhom 16..31)
static const IRQn_Type g_canIrqMb0to15[PTMCAN_NUM_INSTANCES] = {
    CAN0_ORed_0_15_MB_IRQn,
    CAN1_ORed_0_15_MB_IRQn,
    CAN2_ORed_0_15_MB_IRQn
};

// Trang thai driver theo tung kenh
static uint8_t           s_maxMb[PTMCAN_NUM_INSTANCES]       = { 0U, 0U, 0U };       // 0 = chua Init
static bool              s_rxIntEnable[PTMCAN_NUM_INSTANCES] = { false, false, false };
static ptmcan_callback_t s_rxCallback[PTMCAN_NUM_INSTANCES]  = { NULL, NULL, NULL };

/*==========================================================
 * 2. HAM PHU TRO (HELFER FUNCTIONS)
 *==========================================================*/

 static inline uint8_t ptmcan_get_code(uint32_t cs)
{
    return (uint8_t)((cs >> 24U) & 0x0FU);
}

/* Kenh hop le va da Init */
static inline bool ptmcan_ready(uint8_t instance)
{
    return (instance < PTMCAN_NUM_INSTANCES) && (s_maxMb[instance] != 0U);
}

/* Kiem tra instance va chi so MB (theo so MB da cau hinh luc Init) */
static status_t ptmcan_check_mb(uint8_t instance, uint8_t mb_idx)
{
    if (instance >= PTMCAN_NUM_INSTANCES)
    {
        return STATUS_ERROR;
    }
    if (mb_idx >= s_maxMb[instance])
    {
        return STATUS_CAN_BUFF_OUT_OF_RANGE;
    }
    return STATUS_SUCCESS;
}

static void ptmcan_nvic_set_irq(IRQn_Type irq, bool enable)
{
    uint32_t n = (uint32_t)irq;

    if (enable)
    {
        S32_NVIC->ISER[n / 32U] = (1UL << (n % 32U));
    }
    else
    {
        S32_NVIC->ICER[n / 32U] = (1UL << (n % 32U));
    }
}

static void ptmcan_nvic_set(uint8_t instance, bool enable)
{
    ptmcan_nvic_set_irq(g_canIrqMb0to15[instance], enable);
    if (instance == 0U)
    {
        ptmcan_nvic_set_irq(CAN0_ORed_16_31_MB_IRQn, enable);
    }
}

/**
 * @brief Yeu cau PTMCAN di vao Freeze Mode va cho phan cung xac nhan (FRZACK = 1)
 */
static status_t ptmcan_enter_freeze_mode(CAN_Type *base)
{
    uint32_t timeout = PTMCAN_TIMEOUT_LOOPS;

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
    uint32_t timeout = PTMCAN_TIMEOUT_LOOPS;

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

/**
 * @brief Bat dau doan can Freeze Mode. Neu module da Freeze san (nguoi dung goi PTMCAN_Stop)
 *        thi giu nguyen, ham _end se khong thoat Freeze.
 */
static status_t ptmcan_cfg_begin(CAN_Type *base, bool *was_frozen)
{
    *was_frozen = ((base->MCR & CAN_MCR_FRZACK_MASK) != 0U);
    if (*was_frozen)
    {
        return STATUS_SUCCESS;
    }
    return ptmcan_enter_freeze_mode(base);
}

static status_t ptmcan_cfg_end(CAN_Type *base, bool was_frozen)
{
    if (was_frozen)
    {
        return STATUS_SUCCESS;
    }
    return ptmcan_exit_freeze_mode(base);
}

static inline bool ptmcan_is_bus_off(const CAN_Type *base)
{
    // FLTCONF: 00 = Error Active, 01 = Error Passive, 1x = Bus Off
    return (((base->ESR1 & CAN_ESR1_FLTCONF_MASK) >> CAN_ESR1_FLTCONF_SHIFT) >= 2U);
}

/*==========================================================
 * 3. CAC API CHINH CUA DRIVER
 *==========================================================*/

 /**
  * @brief Ham khoi tao cho CAN
  */
status_t PTMCAN_Init(uint8_t instance, const ptmcan_user_config_t *config)
{
    if ((instance >= PTMCAN_NUM_INSTANCES) || (config == NULL) ||
        (config->max_num_mb == 0U) || (config->max_num_mb > g_canMaxMb[instance]))
    {
        return STATUS_ERROR;
    }

    CAN_Type *base = g_canBase[instance];
    uint32_t timeout;

    s_maxMb[instance] = 0U;     // Danh dau chua san sang cho den khi Init xong

    // 1. Cap clock cho module FlexCAN thong qua PCC
    PCC->PCCn[g_pccCanIndex[instance]] |= PCC_PCCn_CGC_MASK;

    // 2. Chon nguon clock cho Protocol Engine (Chi duoc ghi khi MDIS = 1)
    base->MCR |= CAN_MCR_MDIS_MASK;
    base->CTRL1 = (base->CTRL1 & ~CAN_CTRL1_CLKSRC_MASK) |
                  CAN_CTRL1_CLKSRC(config->clk_src);

    // 3. Bat module PTMCAN (MDIS = 0) va cho thoat Low-Power Mode
    base->MCR &= ~CAN_MCR_MDIS_MASK;
    timeout = PTMCAN_TIMEOUT_LOOPS;
    while (base->MCR & CAN_MCR_LPMACK_MASK)
    {
        if (--timeout == 0U) return STATUS_TIMEOUT;    // Thuong do chua bat clock nguon (SOSC)
    }

    // 4. Soft Reset de xoa sach cau hinh cu
    base->MCR |= CAN_MCR_SOFTRST_MASK;
    timeout = PTMCAN_TIMEOUT_LOOPS;
    while (base->MCR & CAN_MCR_SOFTRST_MASK)
    {
        if (--timeout == 0U) return STATUS_TIMEOUT;
    }

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
    uint32_t maxmb_val = (uint32_t)config->max_num_mb - 1U;

    base->MCR = (base->MCR & ~(CAN_MCR_MAXMB_MASK | CAN_MCR_IRMQ_MASK |
                                CAN_MCR_SRXDIS_MASK | CAN_MCR_AEN_MASK))
                | CAN_MCR_MAXMB(maxmb_val)
                | CAN_MCR_IRMQ(1U)
                | CAN_MCR_SRXDIS(config->enable_loopback ? 0U : 1U)
                | CAN_MCR_AEN(config->enable_abort ? 1U : 0U);

    // 7. Cau hinh Bit Timing vao thanh ghi CTRL1
    base->CTRL1 = (base->CTRL1 & ~(CAN_CTRL1_PRESDIV_MASK | CAN_CTRL1_PROPSEG_MASK |
                                   CAN_CTRL1_PSEG1_MASK | CAN_CTRL1_PSEG2_MASK |
                                   CAN_CTRL1_RJW_MASK | CAN_CTRL1_LPB_MASK |
                                   CAN_CTRL1_BOFFREC_MASK))
                | CAN_CTRL1_PRESDIV(config->bitrate.prediv)
                | CAN_CTRL1_PROPSEG(config->bitrate.propseg)
                | CAN_CTRL1_PSEG1(config->bitrate.pseg1)
                | CAN_CTRL1_PSEG2(config->bitrate.pseg2)
                | CAN_CTRL1_RJW(config->bitrate.rjw)
                | CAN_CTRL1_LPB(config->enable_loopback ? 1U : 0U);

    // 8. Xoa sach vung nho Message Buffers RAM (128 words tuong duong 32 MBs)
    uint32_t ram_word_count = (uint32_t)g_canMaxMb[instance] * 4U;
    for (uint8_t i = 0U; i < ram_word_count; i++)
    {
        base->RAMn[i] = 0U;
    }
    for (uint32_t i = 0U; i < g_canMaxMb[instance]; i++)
    {
        base->RXIMR[i] = 0U;
    }

    // Tat tat ca ngat MB va xoa co ngat ton dong
    base->IMASK1 = 0x00000000U;
    base->IFLAG1 = 0xFFFFFFFFU;

    // Luu trang thai driver
    s_rxIntEnable[instance] = config->enable_rx_interrupt;
    s_maxMb[instance]       = config->max_num_mb;
    // 9. bat ngat NVIC neu nguoi dung yeu cau
    if (config->enable_rx_interrupt)
    {
        ptmcan_nvic_set(instance, true);
    }

    // 10. Thoat Freeze Mode de bat dau lam viec
    return ptmcan_exit_freeze_mode(base);
}

/**
 * @brief Ham huy cho CAN
 */
status_t PTMCAN_DeInit(uint8_t instance)
{
    if (instance >= PTMCAN_NUM_INSTANCES) return STATUS_ERROR;
    CAN_Type *base = g_canBase[instance];

    // Ngat NVIC
    ptmcan_nvic_set(instance, false);

    // Chi truy cap thanh ghi neu clock dang bat (tranh BusFault khi chua Init)
    if ((PCC->PCCn[g_pccCanIndex[instance]] & PCC_PCCn_CGC_MASK) != 0U)
    {
        base->IMASK1 = 0U;
    base->MCR |= CAN_MCR_MDIS_MASK;
    PCC->PCCn[g_pccCanIndex[instance]] &= ~PCC_PCCn_CGC_MASK;
    }

    s_maxMb[instance]       = 0U;
    s_rxIntEnable[instance] = false;

    return STATUS_SUCCESS;
}

/**
 * @brief Dung module (Freeze Mode)
 */
status_t PTMCAN_Stop(uint8_t instance)
{
    if (!ptmcan_ready(instance)) return STATUS_ERROR;
    return ptmcan_enter_freeze_mode(g_canBase[instance]);
}

/**
 * @brief Cho module chay lai
 */
status_t PTMCAN_Start(uint8_t instance)
{
    if (!ptmcan_ready(instance)) return STATUS_ERROR;
    return ptmcan_exit_freeze_mode(g_canBase[instance]);
}

/**
 * @brief Ham cau hinh Rx Message Buffer
 */
status_t PTMCAN_ConfigRxMB(uint8_t instance, uint8_t mb_idx, uint32_t msg_id)
{
    status_t st = ptmcan_check_mb(instance, mb_idx);
    if (st != STATUS_SUCCESS) return st;

    CAN_Type *base = g_canBase[instance];
    volatile uint32_t *mb = &base->RAMn[mb_idx * 4U];

    // 1. Dua MB ve INACTIVE truoc khi sua
    mb[0] = (CAN_MB_CODE_RX_INACTIVE << 24U);

    // 2. Cai dat 11 bit Standard ID (dich trai 18 bit, IDE = 0)
    mb[1] = ((msg_id & PTMCAN_STD_ID_MASK) << 18U);
    mb[2] = 0U;
    mb[3] = 0U;

    // 3. Dat mat na loc mac dinh, khop chinh xac 100% ID
    st = PTMCAN_SetRxMask(instance, mb_idx, PTMCAN_MASK_EXACT_MATCH);
    if (st != STATUS_SUCCESS) return st;

    // 4. Kich hoat MB o trang thai ranh san sangf don ban tin (RX_EMPTY = 0x4)
    base->IFLAG1 = (1UL << mb_idx);
    mb[0] = (CAN_MB_CODE_RX_EMPTY << 24U);

    // Bat co cho phep ngat cua MB nay
    if (s_rxIntEnable[instance])
    {
        base->IMASK1 |= (1UL << mb_idx);
    }

    return STATUS_SUCCESS;
}

/**
 * @brief Ham cau hinh Tx Message Buffer
 */
status_t PTMCAN_ConfigTxMB(uint8_t instance, uint8_t mb_idx)
{
    status_t st = ptmcan_check_mb(instance, mb_idx);
    if (st != STATUS_SUCCESS) return st;    
    
    CAN_Type *base = g_canBase[instance];
    volatile uint32_t *mb = &base->RAMn[mb_idx * 4U];

    base->IMASK1 &= ~(1UL << mb_idx);
    base->IFLAG1  =  (1UL << mb_idx);

    mb[0] = (CAN_MB_CODE_TX_INACTIVE << 24U);
    mb[1] = 0U;
    mb[2] = 0U;
    mb[3] = 0U;

    return STATUS_SUCCESS;
}

/**
 * @brief Cai dat mat na loc rieng (RXIMR) cho mot hop thu nhan
 */
status_t PTMCAN_SetRxMask(uint8_t instance, uint8_t mb_idx, uint32_t mask)
{
    status_t st = ptmcan_check_mb(instance, mb_idx);
    if (st != STATUS_SUCCESS) return st;

    CAN_Type *base = g_canBase[instance];
    bool was_frozen;

    // Vao Freeze Mode de ghi thanh ghi RXIMR
    if (ptmcan_cfg_begin(base, &was_frozen) != STATUS_SUCCESS)
    {
        return STATUS_TIMEOUT;
    }

    // Standard ID Mask phai nam o bits 28...18
    base->RXIMR[mb_idx] = ((mask & PTMCAN_STD_ID_MASK) << 18U);

    // Thoat Freeze Mode
    return ptmcan_cfg_end(base, was_frozen);
}

/**
 * @brief Ham gui yeu cau truyen ban tin CAN
 */
status_t PTMCAN_Transmit(uint8_t instance, uint8_t mb_idx, const ptmcan_msgbuff_t *tx_msg)
{
    status_t st = ptmcan_check_mb(instance, mb_idx);
    if (st != STATUS_SUCCESS) return st;
    if (tx_msg == NULL) return STATUS_ERROR;
    if (tx_msg->data_len > PTMCAN_MAX_PAYLOAD_SIZE) return STATUS_ERROR;

    CAN_Type *base = g_canBase[instance];
    volatile uint32_t *mb = &base->RAMn[mb_idx * 4U];

    // Kiem tra xem MB truoc do da gui xong chua
    uint8_t code = ptmcan_get_code(mb[0]);
    if (code == CAN_MB_CODE_TX_DATA)
    {
        return STATUS_BUSY;
    }
    if ((code != CAN_MB_CODE_TX_INACTIVE) && (code != CAN_MB_CODE_TX_ABORT))
    {
        return STATUS_ERROR;        // Khong phai MB truyen (chua ConfigTxMB hoac dang la MB Rx)
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

    base->IFLAG1 = (1UL << mb_idx);
    // 4. Kich hoat truyen: CODE = TX_DATA (0xC), DLC, IDE = 0, RTR = 0
    mb[0] = (CAN_MB_CODE_TX_DATA << 24U) | ((uint32_t)tx_msg->data_len << 16U);

    return STATUS_SUCCESS;
}

/**
 * @brief Ham nhan CAN
 */
status_t PTMCAN_Receive(uint8_t instance, uint8_t mb_idx, ptmcan_msgbuff_t *rx_msg)
{
    status_t st = ptmcan_check_mb(instance, mb_idx);
    if (st != STATUS_SUCCESS) return st;
    if (rx_msg == NULL) return STATUS_ERROR;

    CAN_Type *base = g_canBase[instance];
    volatile uint32_t *mb = &base->RAMn[mb_idx * 4U];

    // 0. Chua co co ngat thi khong co tin moi (CODE van la RX_FULL tu lan nhan truoc)
    if ((base->IFLAG1 & (1UL << mb_idx)) == 0U)
    {
        return PTMCAN_STATUS_NO_DATA;
    }

    // 1. Doc CS word (khoa MB), neu phan cung dang ghi (BUSY) thi doc lai
    uint32_t cs;
    uint8_t  code;
    uint32_t retry = PTMCAN_RX_BUSY_RETRY;

    do
    {
        cs   = mb[0];
        code = ptmcan_get_code(cs);
    } while (((code & CAN_MB_CODE_RX_BUSY) != 0U) && (--retry > 0U));

    if ((code & CAN_MB_CODE_RX_BUSY) != 0U)
    {
        (void)base->TIMER;                  // Mo khoa
        return STATUS_BUSY;
    }

    // Chi chap nhan RX_FULL hoac RX_OVERRUN
    if ((code != CAN_MB_CODE_RX_FULL) && (code != CAN_MB_CODE_RX_OVERRUN))
    {
        (void)base->TIMER;                  // Mo khoa
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

    // 5. Xoa co ngat IFLAG1 (Ghi 1 de xoa - W1C)
    base->IFLAG1 = (1UL << mb_idx);
    
    // 6. Doc thanh ghi timer de unlock MB
    (void)base->TIMER;

    return STATUS_SUCCESS;
}

/**
 * @brief Ham kiem tra lieu MB co ban khong?
 */
bool PTMCAN_IsMBBusy(uint8_t instance, uint8_t mb_idx)
{
    if (ptmcan_check_mb(instance, mb_idx) != STATUS_SUCCESS) return false;

    CAN_Type *base = g_canBase[instance];
    volatile uint32_t *mb = &base->RAMn[mb_idx * 4U];

    // MB ban neu code la TX_DATA (Dang cho gui)
    return (ptmcan_get_code(mb[0]) == CAN_MB_CODE_TX_DATA);
}

/**
 * @brief Ham khoa ABORT (huy truyen) cho Message Buffer
 */
status_t PTMCAN_AbortTx(uint8_t instance, uint8_t mb_idx)
{
    status_t st = ptmcan_check_mb(instance, mb_idx);
    if (st != STATUS_SUCCESS) return st;    

    CAN_Type *base = g_canBase[instance];
    volatile uint32_t *mb = &base->RAMn[mb_idx * 4U];

    if ((base->MCR & CAN_MCR_AEN_MASK) == 0U)
    {
        return STATUS_ERROR;                // Chua bat enable_abort luc Init
    }

    if (ptmcan_get_code(mb[0]) != CAN_MB_CODE_TX_DATA)
    {
        return STATUS_SUCCESS;              // Khong co gi dang xep hang
    }

    base->IFLAG1 = (1UL << mb_idx);
    mb[0] = (CAN_MB_CODE_TX_ABORT << 24U);
    
    // Cho phan cung xac nhan
    uint32_t timeout = PTMCAN_TIMEOUT_LOOPS;
    while ((base->IFLAG1 & (1UL << mb_idx)) == 0U)
    {
        if (--timeout == 0U)
        {
            return STATUS_TIMEOUT;
        }
    }
    base->IFLAG1 = (1UL << mb_idx);

    return STATUS_SUCCESS;
}

// Cac ham xu ly ngat cho PTMCAN

void PTMCAN_RegisterRxCallback(uint8_t instance, ptmcan_callback_t callback)
{
    if (instance < PTMCAN_NUM_INSTANCES)
    {
        s_rxCallback[instance] = callback;
    }
}

/**
 * @brief Trinh phuc vu ngat chung cho cac MB trong khoang [first, last] cua mot kenh
 */
static void ptmcan_irq_handler(uint8_t instance, uint8_t first, uint8_t last)
{
    CAN_Type *base = g_canBase[instance];
    uint32_t iflag = base->IFLAG1 & base->IMASK1;   // Chi quan tam cac MB co bat ngat
    ptmcan_msgbuff_t rx_frame;

    for (uint8_t mb_idx = first; mb_idx <= last; mb_idx++)
    {
        if ((iflag & (1UL << mb_idx)) == 0U)
        {
            continue;
        }

        status_t st = PTMCAN_Receive(instance, mb_idx, &rx_frame);

        if (st == STATUS_SUCCESS)
        {
            if (s_rxCallback[instance] != NULL)
            {
                s_rxCallback[instance](instance, mb_idx, &rx_frame);
            }
        }
        else if (st != STATUS_BUSY)
        {
            // Khong doc duoc: xoa co de tranh ngat bi kich hoat lap vo han
            base->IFLAG1 = (1UL << mb_idx);
        }
        // STATUS_BUSY: giu co, ngat se vao lai khi phan cung ghi xong
    }
}

void CAN0_ORed_0_15_MB_IRQHandler(void)
{
    ptmcan_irq_handler(0U, 0U, 15U);
}

void CAN0_ORed_16_31_MB_IRQHandler(void)
{
    ptmcan_irq_handler(0U, 16U, 31U);
}

void CAN1_ORed_0_15_MB_IRQHandler(void)
{
    ptmcan_irq_handler(1U, 0U, 15U);
}

void CAN2_ORed_0_15_MB_IRQHandler(void)
{
    ptmcan_irq_handler(2U, 0U, 15U);
}

// Cac ham chan doan va debug

/**
 * @brief Ham lay ma loi cua mang CAN (Bang cach doc thanh ghi ESR1)
 */
uint32_t PTMCAN_GetErrorStatus(uint8_t instance)
{
    if (!ptmcan_ready(instance)) return 0xFFFFFFFFU;
    return g_canBase[instance]->ESR1;
}

/**
 * @brief Xoa cac co loi sticky trong ESR1 (W1C)
 */
status_t PTMCAN_ClearErrorStatus(uint8_t instance, uint32_t flags)
{
    if (!ptmcan_ready(instance)) return STATUS_ERROR;
    g_canBase[instance]->ESR1 = (flags & PTMCAN_ERR_CLEARABLE_MASK);
    return STATUS_SUCCESS;
}

/**
 * @brief Ham phuc hoi Bus CAN bi OFF
 * @note  Phan cung tu phuc hoi (BOFFREC = 0) sau 128 lan 11 bit recessive lien tiep tren bus.
 *        Neu bus van loi (dut day, mat transceiver, sai toc do) se tra STATUS_TIMEOUT.
 *        Cac MB dang TX_DATA van con xep hang va se tu gui lai sau khi phuc hoi.
 */
status_t PTMCAN_RecoverBusOff(uint8_t instance)
{
    if (!ptmcan_ready(instance)) return STATUS_ERROR;
    CAN_Type *base = g_canBase[instance];

    if (!ptmcan_is_bus_off(base))
    {
        return STATUS_SUCCESS;
    }

    uint32_t timeout = PTMCAN_BUSOFF_TIMEOUT_LOOPS;
    while (ptmcan_is_bus_off(base))
    {
        if (--timeout == 0U)
        {
            return STATUS_TIMEOUT;
        }
    }

    // Xoa co ngat Bus-off (W1C)
    base->ESR1 = CAN_ESR1_BOFFINT_MASK;
    return STATUS_SUCCESS;
}

status_t PTMCAN_SetLoopbackMode(uint8_t instance, bool enable)
{
    if (!ptmcan_ready(instance)) return STATUS_ERROR;
    CAN_Type *base = g_canBase[instance];
    bool was_frozen;

    // Bat tat Loopback, yeu cau phai vao freeze mode
    if (ptmcan_cfg_begin(base, &was_frozen) != STATUS_SUCCESS)
    {
        return STATUS_TIMEOUT;
    }

    if (enable)
    {
        base->CTRL1 |= CAN_CTRL1_LPB_MASK;
        base->MCR   &= ~CAN_MCR_SRXDIS_MASK;
    }
    else
    {
        base->CTRL1 &= ~CAN_CTRL1_LPB_MASK;
        base->MCR   |= CAN_MCR_SRXDIS_MASK;
    }

    return ptmcan_cfg_end(base, was_frozen);
}