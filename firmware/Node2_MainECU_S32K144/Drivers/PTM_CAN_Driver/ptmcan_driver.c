#include "ptmcan_driver.h"

/*==========================================================
 * 1. MÃ TRẠNG THÁI CỦA MESSAGE BUFFER TRONG CS WORD
 *==========================================================*/

#define CAN_MB_CODE_RX_INACTIVE         (0x0U)      // MB nhận không hoạt động
#define CAN_MB_CODE_RX_BUSY             (0x1U)      // Module đang ghi đè vào MB
#define CAN_MB_CODE_RX_FULL             (0x2U)      // Đã nhận được frame mới
#define CAN_MB_CODE_RX_EMPTY            (0x4U)      // MB rảnh, đang chờ nhận frame
#define CAN_MB_CODE_RX_OVERRUN          (0x6U)      // Nhận frame mới đè lên frame cũ chưa đọc

#define CAN_MB_CODE_TX_INACTIVE         (0x8U)      // MB truyền rảnh
#define CAN_MB_CODE_TX_ABORT            (0x9U)      // Yêu cầu truyền bị hủy
#define CAN_MB_CODE_TX_DATA             (0xCU)      // Kích hoạt truyền Data frame
#define CAN_MB_CODE_TX_REMOTE           (0x1CU)     // Kích hoạt truyền Remote frame

// Bảng tra cứu Base Address và PCC Index của 3 kênh CAN
static CAN_Type * const g_canBase[] = { CAN0, CAN1, CAN2 };
static const uint32_t g_pccCanIndex[] = {
    PCC_FlexCAN0_INDEX,
    PCC_FlexCAN1_INDEX,
    PCC_FlexCAN2_INDEX
};

// Lưu trữ con trỏ Callback của tầng ứng dụng cho từng kênh
static ptmcan_callback_t s_rxCallback[3] = { NULL, NULL, NULL };

/*==========================================================
 * 2. HÀM PHỤ TRỢ (HELPER FUNCTIONS)
 *==========================================================*/

/**
 * @brief Đảo byte 32-bit (Big-Endian <-> Little-Endian)
 * Giúp dữ liệu Byte 0 của mảng nằm ở byte cao nhất trên bus CAN
 */
static inline uint32_t ptmcan_swap_bytes(uint32_t val)
{
    return (((val & 0x000000FFU) << 24U) |
            ((val & 0x0000FF00U) << 8U)  |
            ((val & 0x00FF0000U) >> 8U)  |
            ((val & 0xFF000000U) >> 24U));
}

/**
 * @brief Yêu cầu FlexCAN đi vào Freeze Mode và chờ phần cứng xác nhận (FRZACK = 1)
 */
static status_t ptmcan_enter_freeze_mode(CAN_Type *base)
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
 * @brief Thoát Freeze Mode và chờ FlexCAN sẵn sàng hoạt động (FRZACK = 0, NOTRDY = 0)
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
 * 3. CÁC API CHÍNH CỦA DRIVER
 *==========================================================*/

/**
 * @brief Hàm khởi tạo cho CAN
 */
status_t PTMCAN_Init(uint8_t instance, const ptmcan_user_config_t *config)
{
    if ((instance >= 3U) || (config == NULL))
    {
        return STATUS_ERROR;
    }

    CAN_Type *base = g_canBase[instance];

    // 1. Cấp clock cho module FlexCAN thông qua PCC
    PCC->PCCn[g_pccCanIndex[instance]] |= PCC_PCCn_CGC_MASK;

    // 2. Chọn nguồn clock cho Protocol Engine (Chỉ được ghi khi MDIS = 1)
    base->MCR |= CAN_MCR_MDIS_MASK;
    base->CTRL1 = (base->CTRL1 & ~CAN_CTRL1_CLKSRC_MASK) |
                  CAN_CTRL1_CLKSRC(config->clk_src);

    // 3. Bật module FlexCAN (MDIS = 0) và chờ thoát Low-Power Mode
    base->MCR &= ~CAN_MCR_MDIS_MASK;
    while (base->MCR & CAN_MCR_LPMACK_MASK) {}

    // 4. Soft Reset để xóa sạch cấu hình cũ
    base->MCR |= CAN_MCR_SOFTRST_MASK;
    while (base->MCR & CAN_MCR_SOFTRST_MASK) {}

    // 5. Đi vào Freeze Mode để cấu hình thanh ghi CAN
    if (ptmcan_enter_freeze_mode(base) != STATUS_SUCCESS)
    {
        return STATUS_TIMEOUT;
    }

    /* 6. Cấu hình MCR
     * - MAXMB = max_num_mb - 1
     * - IRMQ = 1 (Dùng lọc riêng từng MB qua RXIMR)
     * - SRXDIS = 1 (Tắt tự nhận lại tin mình gửi)
     * - AEN (Cho phép hủy truyền theo cấu hình)
     */
    uint32_t maxmb_val = (config->max_num_mb > 0U) ? (config->max_num_mb - 1U) : 15U;

    base->MCR = (base->MCR & ~(CAN_MCR_MAXMB_MASK | CAN_MCR_IRMQ_MASK |
                               CAN_MCR_SRXDIS_MASK | CAN_MCR_AEN_MASK))
              | CAN_MCR_MAXMB(maxmb_val)
              | CAN_MCR_IRMQ(1U)
              | CAN_MCR_SRXDIS(1U)
              | CAN_MCR_AEN(config->enable_abort ? 1U : 0U);

    // 7. Cấu hình Bit Timing vào thanh ghi CTRL1
    base->CTRL1 = (base->CTRL1 & ~(CAN_CTRL1_PRESDIV_MASK | CAN_CTRL1_PROPSEG_MASK |
                                  CAN_CTRL1_PSEG1_MASK   | CAN_CTRL1_PSEG2_MASK   |
                                  CAN_CTRL1_RJW_MASK     | CAN_CTRL1_LPB_MASK))
                | CAN_CTRL1_PRESDIV(config->bitrate.prediv)
                | CAN_CTRL1_PROPSEG(config->bitrate.propseg)
                | CAN_CTRL1_PSEG1(config->bitrate.pseg1)
                | CAN_CTRL1_PSEG2(config->bitrate.pseg2)
                | CAN_CTRL1_RJW(config->bitrate.rjw)
                | CAN_CTRL1_LPB(config->enable_loopback ? 1U : 0U);

    // 8. Xóa sạch vùng nhớ Message Buffers RAM (128 words tương đương 32 MBs)
    for (uint8_t i = 0U; i < CAN_RAMn_COUNT; i++)
    {
        base->RAMn[i] = 0U;
    }

    // Tắt tất cả ngắt MB và xóa cờ ngắt tồn đọng
    base->IMASK1 = 0x00000000U;
    base->IFLAG1 = 0xFFFFFFFFU;

    // 9. Bật ngắt NVIC nếu người dùng yêu cầu
    if (config->enable_rx_interrupt)
    {
        if (instance == 0U)
        {
            S32_NVIC->ISER[CAN0_ORed_0_15_MB_IRQn / 32U] = (1U << (CAN0_ORed_0_15_MB_IRQn % 32U));
            S32_NVIC->ISER[CAN0_ORed_16_31_MB_IRQn / 32U] = (1U << (CAN0_ORed_16_31_MB_IRQn % 32U));
        }
    }

    // 10. Thoát Freeze Mode để bắt đầu làm việc
    return ptmcan_exit_freeze_mode(base);
}

/**
 * @brief Hàm hủy cho CAN
 */
status_t PTMCAN_DeInit(uint8_t instance)
{
    if (instance >= 3U) return STATUS_ERROR;
    CAN_Type *base = g_canBase[instance];

    // Ngắt NVIC
    if (instance == 0U)
    {
        S32_NVIC->ICER[CAN0_ORed_0_15_MB_IRQn / 32U] = (1U << (CAN0_ORed_0_15_MB_IRQn % 32U));
        S32_NVIC->ICER[CAN0_ORed_16_31_MB_IRQn / 32U] = (1U << (CAN0_ORed_16_31_MB_IRQn % 32U));
    }

    // Tắt module (MDIS = 1) và cắt clock PCC
    base->MCR |= CAN_MCR_MDIS_MASK;
    PCC->PCCn[g_pccCanIndex[instance]] &= ~PCC_PCCn_CGC_MASK;

    return STATUS_SUCCESS;
}

/**
 * @brief Hàm cấu hình Rx Message Buffer
 */
status_t PTMCAN_ConfigRxMB(uint8_t instance, uint8_t mb_idx, uint32_t msg_id)
{
    if ((instance >= 3U) || (mb_idx >= 32U)) return STATUS_ERROR;
    CAN_Type *base = g_canBase[instance];
    volatile uint32_t *mb = &base->RAMn[mb_idx * 4U];

    // 1. Đưa MB về INACTIVE trước khi sửa
    mb[0] = (CAN_MB_CODE_RX_INACTIVE << 24U);

    // 2. Cài đặt 11-bit Standard ID (dịch trái 18 bit, IDE = 0)
    mb[1] = ((msg_id & PTMCAN_STD_ID_MASK) << 18U);

    // 3. Đặt mặt nạ lọc mặc định: Khớp chính xác 100% ID
    PTMCAN_SetRxMask(instance, mb_idx, PTMCAN_MASK_EXACT_MATCH);

    // 4. Kích hoạt MB ở trạng thái rảnh sẵn sàng đón bản tin (RX_EMPTY = 0x4)
    mb[0] = (CAN_MB_CODE_RX_EMPTY << 24U);

    // Bật cờ cho phép ngắt của MB này
    base->IMASK1 |= (1U << mb_idx);

    return STATUS_SUCCESS;
}

/**
 * @brief Hàm cấu hình Tx Message Buffer
 */
status_t PTMCAN_ConfigTxMB(uint8_t instance, uint8_t mb_idx)
{
    if ((instance >= 3U) || (mb_idx >= 32U)) return STATUS_ERROR;
    CAN_Type *base = g_canBase[instance];
    volatile uint32_t *mb = &base->RAMn[mb_idx * 4U];

    // Cấu hình MB sẵn sàng truyền: CODE = TX_INACTIVE (0x8)
    mb[0] = (CAN_MB_CODE_TX_INACTIVE << 24U);
    mb[1] = 0U;
    mb[2] = 0U;
    mb[3] = 0U;

    // Tắt ngắt nhận cho MB này
    base->IMASK1 &= ~(1U << mb_idx);

    return STATUS_SUCCESS;
}

/**
 * @brief Cài đặt mặt nạ lọc riêng (RXIMR) cho một hộp thư nhận
 */
status_t PTMCAN_SetRxMask(uint8_t instance, uint8_t mb_idx, uint32_t mask)
{
    if ((instance >= 3U) || (mb_idx >= 32U)) return STATUS_ERROR;
    CAN_Type *base = g_canBase[instance];

    // Vào Freeze Mode để ghi thanh ghi RXIMR
    if (ptmcan_enter_freeze_mode(base) != STATUS_SUCCESS)
    {
        return STATUS_TIMEOUT;
    }

    // Standard ID Mask phải nằm ở bits 28..18
    base->RXIMR[mb_idx] = ((mask & PTMCAN_STD_ID_MASK) << 18U);

    // Thoát Freeze Mode
    return ptmcan_exit_freeze_mode(base);
}

/**
 * @brief Hàm gửi yêu cầu truyền bản tin CAN
 */
status_t PTMCAN_Transmit(uint8_t instance, uint8_t mb_idx, const ptmcan_msgbuff_t *tx_msg)
{
    if ((instance >= 3U) || (mb_idx >= 32U) || (tx_msg == NULL)) return STATUS_ERROR;
    if (tx_msg->data_len > PTMCAN_MAX_PAYLOAD_SIZE) return STATUS_ERROR;

    CAN_Type *base = g_canBase[instance];
    volatile uint32_t *mb = &base->RAMn[mb_idx * 4U];

    // Kiểm tra xem MB trước đó đã gửi xong chưa
    if (PTMCAN_IsMBBusy(instance, mb_idx))
    {
        return STATUS_BUSY;
    }

    // 1. Đặt CODE = INACTIVE trước khi cập nhật nội dung
    mb[0] = (CAN_MB_CODE_TX_INACTIVE << 24U);

    // 2. Cài đặt ID (Standard 11-bit)
    mb[1] = ((tx_msg->msg_id & PTMCAN_STD_ID_MASK) << 18U);

    // 3. Nạp dữ liệu Data payload (đảo byte để tương thích Big-Endian bus)
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

    // 4. Kích hoạt truyền: CODE = TX_DATA (0xC), DLC, IDE = 0, RTR = 0
    mb[0] = (CAN_MB_CODE_TX_DATA << 24U) | ((uint32_t)tx_msg->data_len << 16U);

    return STATUS_SUCCESS;
}

/**
 * @brief Hàm nhận bản tin CAN từ hộp thư
 */
status_t PTMCAN_Receive(uint8_t instance, uint8_t mb_idx, ptmcan_msgbuff_t *rx_msg)
{
    if ((instance >= 3U) || (mb_idx >= 32U) || (rx_msg == NULL)) return STATUS_ERROR;

    CAN_Type *base = g_canBase[instance];
    volatile uint32_t *mb = &base->RAMn[mb_idx * 4U];

    // 1. Đọc CS word (Thao tác này khóa MB lại)
    uint32_t cs = mb[0];
    uint8_t code = (uint8_t)((cs >> 24U) & 0x0FU);

    // Kiểm tra xem MB có nhận tin mới không (RX_FULL = 0x2 hoặc RX_OVERRUN = 0x6)
    if ((code != CAN_MB_CODE_RX_FULL) && (code != CAN_MB_CODE_RX_OVERRUN))
    {
        return STATUS_ERROR;
    }

    // 2. Đọc CAN ID
    rx_msg->msg_id = (mb[1] >> 18U) & PTMCAN_STD_ID_MASK;

    // 3. Đọc độ dài dữ liệu (DLC)
    rx_msg->data_len = (uint8_t)((cs >> 16U) & 0x0FU);
    if (rx_msg->data_len > PTMCAN_MAX_PAYLOAD_SIZE)
    {
        rx_msg->data_len = PTMCAN_MAX_PAYLOAD_SIZE;
    }

    // 4. Đọc dữ liệu ra mảng (Giải mã Big-Endian)
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

    // 5. Đọc thanh ghi TIMER để UNLOCK Message Buffer!
    (void)base->TIMER;

    // 6. Xóa cờ ngắt IFLAG1 (Ghi 1 để xóa - W1C)
    base->IFLAG1 = (1U << mb_idx);

    return STATUS_SUCCESS;
}

/**
 * @brief Hàm kiểm tra MB có đang bận truyền không
 */
bool PTMCAN_IsMBBusy(uint8_t instance, uint8_t mb_idx)
{
    if ((instance >= 3U) || (mb_idx >= 32U)) return false;
    CAN_Type *base = g_canBase[instance];
    volatile uint32_t *mb = &base->RAMn[mb_idx * 4U];

    uint8_t code = (uint8_t)((mb[0] >> 24U) & 0x0FU);

    // MB bận nếu code là TX_DATA (Đang chờ gửi)
    return (code == CAN_MB_CODE_TX_DATA);
}

/**
 * @brief Hàm hủy truyền cho Message Buffer
 */
status_t PTMCAN_AbortTx(uint8_t instance, uint8_t mb_idx)
{
    if ((instance >= 3U) || (mb_idx >= 32U)) return STATUS_ERROR;
    CAN_Type *base = g_canBase[instance];
    volatile uint32_t *mb = &base->RAMn[mb_idx * 4U];

    // Ghi mã ABORT (0x9) vào trường CODE của MB
    mb[0] = (CAN_MB_CODE_TX_ABORT << 24U);

    return STATUS_SUCCESS;
}

// Các hàm xử lý ngắt cho PTMCAN
void PTMCAN_RegisterRxCallback(uint8_t instance, ptmcan_callback_t callback)
{
    if (instance < 3U)
    {
        s_rxCallback[instance] = callback;
    }
}

/**
 * @brief Trình phục vụ ngắt chung cho các MB từ 0->15 của FlexCAN0
 */
void CAN0_ORed_0_15_MB_IRQHandler(void)
{
    uint32_t iflag = CAN0->IFLAG1 & CAN0->IMASK1; // Chỉ quan tâm các MB có bật ngắt
    ptmcan_msgbuff_t rx_frame;

    for (uint8_t mb_idx = 0U; mb_idx < 16U; mb_idx++)
    {
        if (iflag & (1U << mb_idx))
        {
            // Thử đọc dữ liệu từ MB
            if (PTMCAN_Receive(0U, mb_idx, &rx_frame) == STATUS_SUCCESS)
            {
                // Báo lên Callback tầng ứng dụng
                if (s_rxCallback[0] != NULL)
                {
                    s_rxCallback[0](0U, mb_idx, &rx_frame);
                }
            }
        }
    }
}

/**
 * @brief Trình phục vụ ngắt chung cho các MB từ 16->31 của FlexCAN0
 */
void CAN0_ORed_16_31_MB_IRQHandler(void)
{
    uint32_t iflag = CAN0->IFLAG1 & CAN0->IMASK1; // Chỉ quan tâm các MB có bật ngắt
    ptmcan_msgbuff_t rx_frame;

    for (uint8_t mb_idx = 16U; mb_idx < 32U; mb_idx++)
    {
        if (iflag & (1U << mb_idx))
        {
            // Thử đọc dữ liệu từ MB
            if (PTMCAN_Receive(0U, mb_idx, &rx_frame) == STATUS_SUCCESS)
            {
                // Báo lên Callback tầng ứng dụng
                if (s_rxCallback[0] != NULL)
                {
                    s_rxCallback[0](0U, mb_idx, &rx_frame);
                }
            }
        }
    }
}

// Các hàm chẩn đoán và debug

/**
 * @brief Hàm lấy mã lỗi của mạng CAN (Bằng cách đọc thanh ghi ESR1)
 */
uint32_t PTMCAN_GetErrorStatus(uint8_t instance)
{
    if (instance >= 3U) return 0xFFFFFFFFU;
    return g_canBase[instance]->ESR1;
}

/**
 * @brief Hàm phục hồi Bus CAN bị OFF
 */
status_t PTMCAN_RecoverBusOff(uint8_t instance)
{
    if (instance >= 3U) return STATUS_ERROR;
    CAN_Type *base = g_canBase[instance];

    // Xóa cờ BOFFINT trong ESR1 (W1C)
    base->ESR1 = CAN_ESR1_BOFFINT_MASK;

    // Tạm thời chuyển sang Freeze Mode rồi quay lại Normal Mode để reset trạng thái bus
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

    // Bật/tắt Loopback yêu cầu phải vào Freeze Mode
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
