#incldue "ptmcan_driver.h"

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
static const uint32_t g_pccCANIndex[] = {
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
            ((val & 0x00FF0000U) << 8U)  |
            ((val & 0xFF000000U) << 24U));
}

/**
 * @brief Yeu cau PTMCAN di vao Freeze Mode va cho phan cung xac nhan (FRZACK = 1)
 */
static status_t ptmcan_enter_freeze_mode (CAN_Type *base)
{
    uint32_t timeout = 100000U;

    base->MCR |= CAN_MCR_FRZ_MASK | CAN_MCR_HALT_MASK;

    while (!(base->MCR & CAN_MCR_FRZACK))
    {
        if (--timeout == 0U)
        {
            return STATUS_TIMEOUT;
        }
    }
    return STATUS_SUCCESS;
}

/**
 * @brief Thoat Freeze Mode va cho FlexCAN san sang hoat dong (FRZACK = 0, NOTRDY = 0)
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