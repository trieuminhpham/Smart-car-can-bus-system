#ifndef PTMCAN_DRIVER_H_
#define PTMCAN_DRIVER_H_

/*===========================================================
 *1. KHAI BAO THU VIEN
 *===========================================================*/
#include "S32K144.h"
#include <stdint.h>
#include <stdbool.h>

#ifndef STATUS_SUCCESS
typedef enum {
    STATUS_SUCCESS                 = 0x000U,
    STATUS_ERROR                   = 0x001U,
    STATUS_BUSY                    = 0x002U,
    STATUS_TIMEOUT                 = 0x003U,
    STATUS_CAN_BUFF_OUT_OF_RANGE   = 0x301U
} status_t;
#endif

/*===========================================================
 *2. CAC HANG SO DINH NGHIA
 *===========================================================*/

// Chi so cac phan cung can ho tro
#define PTMCAN_INSTANCE_0           (0U)
#define PTMCAN_INSTANCE_1           (1U)
#define PTMCAN_INSTANCE_2           (2U)

// Hang so khung truyen Classic CAN 2.0A
#define PTMCAN_MAX_PAYLOAD_SIZE     (8U)
#define PTMCAN_STD_ID_MASK          (0x7FFU)

// Cac mat na loc thuong dung cho ham PTMCAN_SetRxMask()
#define PTMCAN_MASK_EXACT_MATCH     (0x7FFU)    // So khop chinh xac 100% 11 bit ID
#define PTMCAN_MASK_ACCEPT_ALL      (0x000U)    // Bo qua so khop, chap nhan tat ca ID

// Cac co loi phan cung anh xa tu thanh ghi ESR1
#define PTMCAN_FLTCONF_MASK         CAN_ESR1_FLTCONF_MASK
#define PTMCAN_ACKERR_MASK          CAN_ESR1_ACKERR_MASK
#define PTMCAN_BIT0ERR_MASK         CAN_ESR1_BIT0ERR_MASK
#define PTMCAN_BIT1ERR_MASK         CAN_ESR1_BIT1ERR_MASK
#define PTMCAN_STUFERR_MASK         CAN_ESR1_STFERR_MASK
#define PTMCAN_CRCERR_MASK          CAN_ESR1_CRCERR_MASK
#define PTMCAN_FRMERR_MASK          CAN_ESR1_FRMERR_MASK
#define PTMCAN_BOFFINT_MASK         CAN_ESR1_BOFFINT_MASK
#define PTMCAN_ERRINT_MASK          CAN_ESR1_ERRINT_MASK

// Macro cau hinh nhanh thong so Bit Timing cho toc do 500 kbps tu nguon xung 8 MHz
#define PTMCAN_BITRATE_500KBPS_8MHz    { .prediv = 0U, .propseg = 5U, .pseg1 = 5U, .pseg2 = 2U, .rjw = 2U }

// Cau hinh 500 kbps tu Bus Clock 40 MHz (PRESDIV = 4 -> Tq = 125ns, 16 Tq/bit, Sample point = 68.75%)
#define PTMCAN_BITRATE_500KBPS_40MHz   { .prediv = 4U, .propseg = 5U, .pseg1 = 3U, .pseg2 = 4U, .rjw = 1U }

/*===========================================================
 * 3. DINH NGHIA KIEU DU LIEU
 *===========================================================*/

/**
 * @brief Nguon xung nhip cap cho khoi xu ly giao thuc CAN (CTRL1[CLKSRC])
 */
typedef enum {
    PTMCAN_CLK_SRC_OSC    = 0U,     // Chon xung thach anh ngoai SOSCDIV2_CLK
    PTMCAN_CLK_SRC_PERIPH = 1U      // Chon xung bus he thong BUS_CLK
} ptmcan_clk_source_t;

/**
 * @brief Cau truc luu 5 tham so chia thoi gian bit nap vao thanh ghi CTRL1
 * @note  Phan cung tu dong cong them 1 vao cac gia tri nay khi tinh toan
 */
typedef struct {
    uint32_t prediv;
    uint32_t propseg;
    uint32_t pseg1;
    uint32_t pseg2;
    uint32_t rjw;
} ptmcan_time_segment_t;

/**
 * @brief Cau truc cau hinh khoi tao tong the cho mot kenh FlexCAN
 */
typedef struct {
    uint8_t                max_num_mb;
    ptmcan_clk_source_t    clk_src;
    ptmcan_time_segment_t  bitrate;
    bool                   enable_loopback;
    bool                   enable_abort;
    bool                   enable_rx_interrupt;
} ptmcan_user_config_t;

/**
 * @brief Cau truc dai dien cho mot khung ban tin Classic CAN 2.0A
 */
typedef struct {
    uint32_t msg_id;
    uint8_t data_len;
    uint8_t data[PTMCAN_MAX_PAYLOAD_SIZE];
} ptmcan_msgbuff_t;

/**
 * @brief Kiểu con trỏ hàm Callback thông báo lên tầng ứng dụng khi có bản tin nhận xong trong ngắt
 * @param instance  Kênh CAN vừa nhận được tin (0, 1 hoặc 2)
 * @param mb_idx    Chỉ số hộp thư MB vừa nhận được tin
 * @param rx_msg    Con trỏ hằng trỏ tới gói tin vừa giải mã (tầng ứng dụng chỉ đọc, không được sửa)
 */
typedef void (*ptmcan_callback_t)(uint8_t instance, uint8_t mb_idx, const ptmcan_msgbuff_t *rx_msg);


/*===========================================================
 * 4. KHAI BAO CAC HAM GIAO TIEP
 *===========================================================*/

/* --- 1. Cau hinh & Khoi tao --- */

/**
 * @brief Khoi tao phan cung FlexCAN (cap clock PCC, chon CLKSRC, cai Baudrate, xoa RAMn, bat ngat NVIC)
 */
status_t PTMCAN_Init(uint8_t instance, const ptmcan_user_config_t *config);

/**
 * @brief Tat hoan toan khoi FlexCAN, ngat NVIC va ngat clock PCC
 */
status_t PTMCAN_DeInit(uint8_t instance);

/**
 * @brief Chuan bi mot hop thu (MB) de nhan tin
 */
status_t PTMCAN_ConfigRxMB(uint8_t instance, uint8_t mb_idx, uint32_t msg_id);
 
/**
 * @brief Chuan bi mot hop thu (MB) de truyen tin
 */
status_t PTMCAN_ConfigTxMB(uint8_t instance, uint8_t mb_idx);

/**
 * @brief Cai dat mat na loc rieng (RXIMR) cho mot hop thu nhan
 */
status_t PTMCAN_SetRxMask(uint8_t instance, uint8_t mb_idx, uint32_t mask);
 

/* --- 2. Truyen nhan du lieu --- */

/**
 * @brief Dong goi va kich hoat truyen mot khung ban tin CAN qua hop thu da cau hinh TX
 */
status_t PTMCAN_Transmit(uint8_t instance, uint8_t mb_idx, const ptmcan_msgbuff_t *tx_msg);

/**
 * @brief Doc ban tin nhan duoc tu hop thu RX (Tu dong xu ly Ngat hoac Polling theo cau hinh)
 */
status_t PTMCAN_Receive(uint8_t instance, uint8_t mb_idx, ptmcan_msgbuff_t *rx_msg);

/**
 * @brief Kiem tra hop thu dang ban hay ranh
 */
bool PTMCAN_IsMBBusy(uint8_t instance, uint8_t mb_idx);

/**
 * @brief Yeu cau huy ngang mot ban tin dang xep hang cho gui
 */
status_t PTMCAN_AbortTx(uint8_t instance, uint8_t mb_idx);


/* --- 3. Su kien & Ngat --- */

/**
 * @brief Dang ky ham xu ly su kien cua tang ung dung
 */
void PTMCAN_RegisterRxCallback(uint8_t instance, ptmcan_callback_t callback);


/* --- 4. An toan & Chan doan --- */

/**
 * @brief Doc thanh ghi trang thai loi ESR1 cua phan cung
 */
uint32_t PTMCAN_GetErrorStatus(uint8_t instance);

/**
 * @brief Kiem tra trang thai Bus-off
 */
status_t PTMCAN_RecoverBusOff(uint8_t instance);


/* --- 5.Kiem thu va Debug --- */

/**
 * @brief Bat hoac tat che do Loop-Back noi bo luc dang chay
 */
status_t PTMCAN_SetLoopbackMode(uint8_t instance, bool enable);

#endif 