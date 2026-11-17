#ifndef PTMCAN_DRIVER_H_
#define PTMCAN_DRIVER_H_

/* ============================================================================
 * 1. THƯ VIỆN NỀN (INCLUDES)
 * ============================================================================ */
#include "S32K144.h"    /* Định nghĩa địa chỉ thanh ghi và các bit mask của chip S32K144 */
#include "status.h"     /* Kiểu trả về chuẩn status_t của S32 SDK (STATUS_SUCCESS, STATUS_BUSY...) */
#include <stdint.h>     /* Các kiểu số nguyên chuẩn: uint8_t, uint32_t... */
#include <stdbool.h>    /* Kiểu logic: bool, true, false */

/* ============================================================================
 * 2. CÁC HẰNG SỐ ĐỊNH NGHĨA (DEFINITIONS & MACROS)
 * ============================================================================ */

/* Chỉ số các kênh FlexCAN trên S32K144 (truyền vào tham số 'instance') */
#define PTMCAN_INSTANCE_0               (0U)        /**< CAN0: Hỗ trợ tối đa 32 Message Buffers */
#define PTMCAN_INSTANCE_1               (1U)        /**< CAN1: Hỗ trợ tối đa 16 Message Buffers */
#define PTMCAN_INSTANCE_2               (2U)        /**< CAN2: Hỗ trợ tối đa 16 Message Buffers */

/* Hằng số khung truyền Classic CAN 2.0A (Standard ID 11-bit) */
#define PTMCAN_MAX_PAYLOAD_SIZE         (8U)        /**< Tối đa 8 byte dữ liệu cho Classic CAN */
#define PTMCAN_STD_ID_MASK              (0x7FFU)    /**< Mặt nạ 11-bit cho Standard ID (0x000 -> 0x7FF) */

/* Các giá trị mặt nạ lọc chuẩn dùng cho hàm PTMCAN_SetRxMask() */
#define PTMCAN_MASK_EXACT_MATCH         (0x7FFU)    /**< So khớp chính xác 100% cả 11 bit của ID */
#define PTMCAN_MASK_ACCEPT_ALL          (0x000U)    /**< Bỏ qua so khớp ID -> Nhận mọi gói tin trên bus */

/* Các cờ lỗi phần cứng ánh xạ từ thanh ghi ESR1 (dùng cho PTMCAN_GetErrorStatus) */
#define PTMCAN_ERR_BUSOFF_MASK          CAN_ESR1_FLTCONF_MASK   /**< Trạng thái lỗi đường truyền (giá trị >= 2 là Bus-Off) */
#define PTMCAN_ERR_ACK_MASK             CAN_ESR1_ACKERR_MASK    /**< Lỗi không có node nào phản hồi bit ACK */
#define PTMCAN_ERR_BIT0_MASK            CAN_ESR1_BIT0ERR_MASK   /**< Lỗi phát bit 0 nhưng đọc về bit 1 */
#define PTMCAN_ERR_BIT1_MASK            CAN_ESR1_BIT1ERR_MASK   /**< Lỗi phát bit 1 nhưng đọc về bit 0 */
#define PTMCAN_ERR_STUFF_MASK           CAN_ESR1_STFERR_MASK    /**< Lỗi nhồi bit (quá 5 bit giống nhau liên tiếp) */
#define PTMCAN_ERR_CRC_MASK             CAN_ESR1_CRCERR_MASK    /**< Lỗi sai mã kiểm tra CRC */
#define PTMCAN_ERR_FORM_MASK            CAN_ESR1_FRMERR_MASK    /**< Lỗi sai cấu trúc khung truyền CAN */

/* ============================================================================
 * 3. ĐỊNH NGHĨA KIỂU DỮ LIỆU (DATA TYPES)
 * ============================================================================ */

/**
 * @brief Nguồn xung nhịp cấp cho khối xử lý giao thức CAN (CTRL1[CLKSRC]).
 */
typedef enum {
    PTMCAN_CLK_SRC_OSC    = 0U,     /**< Xung thạch anh ngoại SOSCDIV2_CLK (Khuyên dùng - độ chính xác cao) */
    PTMCAN_CLK_SRC_PERIPH = 1U      /**< Xung bus hệ thống BUS_CLK (Tỉ lệ 1:1 với bus thanh ghi) */
} ptmcan_clk_source_t;

/**
 * @brief Cấu trúc lưu 5 tham số chia thời gian bit (Bit Timing) nạp vào thanh ghi CTRL1.
 * @note  Phần cứng tự động cộng thêm 1 vào các giá trị này khi tính toán.
 */
typedef struct {
    uint32_t presdiv;   /**< Bộ chia tần số cơ sở (0..255) -> Tq_clk = f_CANCLK / (presdiv + 1) */
    uint32_t propseg;   /**< Đoạn bù trễ truyền dẫn (0..7) -> Số Tq = propseg + 1 */
    uint32_t pseg1;     /**< Đoạn pha 1 trước điểm lấy mẫu (0..7) -> Số Tq = pseg1 + 1 */
    uint32_t pseg2;     /**< Đoạn pha 2 sau điểm lấy mẫu (0..7) -> Số Tq = pseg2 + 1 */
    uint32_t rjw;       /**< Độ rộng nhảy tái đồng bộ (0..3) -> Số Tq = rjw + 1 */
} ptmcan_time_segment_t;

/**
 * @brief Cấu trúc cấu hình khởi tạo tổng thể cho một kênh FlexCAN.
 */
typedef struct {
    uint8_t               max_num_mb;           /**< Tổng số MB kích hoạt (VD: dùng MB0..MB4 thì điền 5U) */
    ptmcan_clk_source_t   clk_src;              /**< Nguồn clock cấp cho Protocol Engine (OSC hoặc PERIPH) */
    ptmcan_time_segment_t bitrate;              /**< Thông số định thời bit do tầng ứng dụng tính toán */
    bool                  enable_loopback;      /**< true: Chế độ tự phát tự thu nội bộ; false: Chạy bus thật */
    bool                  enable_abort;         /**< true: Bật tính năng hủy gói TX (MCR[AEN]=1); false: Tắt */
    bool                  enable_rx_interrupt;  /**< true: Nhận qua Ngắt (ISR); false: Nhận qua Polling */
} ptmcan_user_config_t;

/**
 * @brief Cấu trúc đại diện cho một khung bản tin Classic CAN 2.0A.
 */
typedef struct {
    uint32_t msg_id;                        /**< Mã định danh Standard ID 11-bit (0x000 -> 0x7FF) */
    uint8_t  data_len;                      /**< Độ dài dữ liệu thực tế tính theo byte (0 -> 8) */
    uint8_t  data[PTMCAN_MAX_PAYLOAD_SIZE]; /**< Mảng chứa tối đa 8 byte dữ liệu (Big-Endian trên bus) */
} ptmcan_msgbuff_t;

/**
 * @brief Kiểu con trỏ hàm Callback thông báo lên tầng ứng dụng khi có bản tin nhận xong trong ngắt.
 *
 * @param instance  Kênh CAN vừa nhận được tin (0, 1 hoặc 2).
 * @param mb_idx    Chỉ số hộp thư MB vừa nhận được tin.
 * @param rx_msg    Con trỏ hằng trỏ tới gói tin vừa giải mã (chỉ đọc).
 */
typedef void (*ptmcan_callback_t)(uint8_t instance, uint8_t mb_idx, const ptmcan_msgbuff_t *rx_msg);

/* ============================================================================
 * 4. KHAI BÁO CÁC HÀM GIAO TIẾP (API FUNCTION PROTOTYPES - 13 HÀM)
 * ============================================================================ */

/* --- Nhóm 1: Cấu hình & Khởi động --- */

/**
 * @brief Khởi tạo phần cứng FlexCAN (cấp clock PCC, chọn CLKSRC, cài Baudrate, xóa RAMn, cấu hình NVIC).
 *
 * @param instance  Chỉ số kênh FlexCAN (0, 1 hoặc 2).
 * @param config    Con trỏ trỏ tới cấu trúc cấu hình của người dùng.
 * @return status_t STATUS_SUCCESS nếu thành công; STATUS_TIMEOUT hoặc STATUS_ERROR nếu thất bại.
 */
status_t PTMCAN_Init(uint8_t instance, const ptmcan_user_config_t *config);

/**
 * @brief Tắt hoàn toàn khối FlexCAN, ngắt NVIC và ngắt clock PCC để tiết kiệm năng lượng.
 *
 * @param instance  Chỉ số kênh FlexCAN (0, 1 hoặc 2).
 * @return status_t STATUS_SUCCESS nếu thành công; STATUS_TIMEOUT hoặc STATUS_ERROR nếu thất bại.
 */
status_t PTMCAN_DeInit(uint8_t instance);

/**
 * @brief Chuẩn bị một hộp thư (MB) chuyên dùng để truyền dữ liệu (đưa về trạng thái TX INACTIVE 0x8).
 *
 * @param instance  Chỉ số kênh FlexCAN (0, 1 hoặc 2).
 * @param mb_idx    Chỉ số hộp thư muốn cấu hình làm TX.
 * @return status_t STATUS_SUCCESS nếu hợp lệ; STATUS_ERROR nếu vượt quá số MB cho phép.
 */
status_t PTMCAN_ConfigTxMB(uint8_t instance, uint8_t mb_idx);

/**
 * @brief Đăng ký và kích hoạt một hộp thư (MB) nhận bản tin theo ID cố định (mặc định mask = 0x7FF).
 *
 * @param instance  Chỉ số kênh FlexCAN (0, 1 hoặc 2).
 * @param mb_idx    Chỉ số hộp thư muốn cấu hình làm RX.
 * @param msg_id    Standard ID 11-bit cần lọc nhận.
 * @return status_t STATUS_SUCCESS nếu thành công; STATUS_ERROR nếu sai tham số.
 */
status_t PTMCAN_ConfigRxMB(uint8_t instance, uint8_t mb_idx, uint32_t msg_id);

/**
 * @brief Cài đặt mặt nạ lọc riêng (RXIMR) cho một hộp thư nhận (tự động vào/thoát Freeze Mode).
 *
 * @param instance  Chỉ số kênh FlexCAN (0, 1 hoặc 2).
 * @param mb_idx    Chỉ số hộp thư RX cần đổi mặt nạ.
 * @param mask      Giá trị mặt nạ 11-bit (1: bắt buộc khớp bit ID; 0: bỏ qua bit ID).
 * @return status_t STATUS_SUCCESS nếu thành công; STATUS_TIMEOUT hoặc STATUS_ERROR nếu lỗi.
 */
status_t PTMCAN_SetRxMask(uint8_t instance, uint8_t mb_idx, uint32_t mask);


/* --- Nhóm 2: Truyền nhận dữ liệu --- */

/**
 * @brief Đóng gói và kích hoạt truyền một khung bản tin CAN qua hộp thư đã cấu hình TX.
 *
 * @param instance  Chỉ số kênh FlexCAN (0, 1 hoặc 2).
 * @param mb_idx    Chỉ số hộp thư TX.
 * @param tx_msg    Con trỏ trỏ tới bản tin cần gửi.
 * @return status_t STATUS_SUCCESS nếu kích hoạt gửi thành công; STATUS_BUSY nếu MB đang bận gửi gói trước.
 */
status_t PTMCAN_Transmit(uint8_t instance, uint8_t mb_idx, const ptmcan_msgbuff_t *tx_msg);

/**
 * @brief Đọc bản tin nhận được từ hộp thư RX (tự động xử lý theo chế độ Ngắt hoặc Polling đã cấu hình).
 *        - Chế độ Ngắt: Lấy bản tin mới nhất từ bộ đệm phần mềm do ISR cập nhật.
 *        - Chế độ Polling: Kiểm tra trực tiếp cờ IFLAG1 và đọc dữ liệu từ thanh ghi phần cứng.
 *
 * @param instance  Chỉ số kênh FlexCAN (0, 1 hoặc 2).
 * @param mb_idx    Chỉ số hộp thư RX cần đọc.
 * @param rx_msg    Con trỏ vùng nhớ lưu bản tin đọc ra.
 * @return status_t STATUS_SUCCESS nếu có bản tin mới; STATUS_BUSY nếu chưa có dữ liệu mới.
 */
status_t PTMCAN_Receive(uint8_t instance, uint8_t mb_idx, ptmcan_msgbuff_t *rx_msg);

/**
 * @brief Kiểm tra hộp thư đang bận hay rảnh (TX: đang chờ phát CODE=0xC; RX: bit BUSY=1 đang chép RAM).
 *
 * @param instance  Chỉ số kênh FlexCAN (0, 1 hoặc 2).
 * @param mb_idx    Chỉ số hộp thư cần kiểm tra.
 * @return bool     true nếu hộp thư đang bận; false nếu rảnh.
 */
bool PTMCAN_IsMBBusy(uint8_t instance, uint8_t mb_idx);

/**
 * @brief Yêu cầu hủy ngang một bản tin đang xếp hàng chờ gửi (yêu cầu enable_abort = true khi Init).
 *
 * @param instance  Chỉ số kênh FlexCAN (0, 1 hoặc 2).
 * @param mb_idx    Chỉ số hộp thư TX cần hủy lệnh truyền.
 * @return status_t STATUS_SUCCESS nếu đã ghi mã hủy; STATUS_ERROR nếu tính năng Abort chưa được bật.
 */
status_t PTMCAN_AbortTx(uint8_t instance, uint8_t mb_idx);


/* --- Nhóm 3: Sự kiện & Ngắt --- */

/**
 * @brief Đăng ký hàm xử lý sự kiện của tầng ứng dụng (được gọi tự động mỗi khi có ngắt nhận RX).
 *
 * @param instance  Chỉ số kênh FlexCAN (0, 1 hoặc 2).
 * @param callback  Con trỏ hàm callback (truyền NULL để hủy đăng ký).
 */
void PTMCAN_RegisterRxCallback(uint8_t instance, ptmcan_callback_t callback);


/* --- Nhóm 4: An toàn & Chẩn đoán --- */

/**
 * @brief Đọc thanh ghi trạng thái lỗi ESR1 của phần cứng.
 *
 * @param instance  Chỉ số kênh FlexCAN (0, 1 hoặc 2).
 * @return uint32_t Giá trị thanh ghi ESR1 (kết hợp với các macro PTMCAN_ERR_xxx_MASK để lọc lỗi).
 */
uint32_t PTMCAN_GetErrorStatus(uint8_t instance);

/**
 * @brief Kiểm tra trạng thái Bus-Off (FLTCONF >= 2) và thực hiện quy trình phục hồi kết nối mạng.
 *
 * @param instance  Chỉ số kênh FlexCAN (0, 1 hoặc 2).
 * @return status_t STATUS_SUCCESS nếu hoạt động bình thường hoặc đã kích hoạt phục hồi; STATUS_ERROR nếu lỗi tham số.
 */
status_t PTMCAN_RecoverBusOff(uint8_t instance);


/* --- Nhóm 5: Kiểm thử (Debug) --- */

/**
 * @brief Bật hoặc tắt chế độ Loop-Back nội bộ lúc đang chạy (tự động xử lý Freeze Mode và bit SRXDIS).
 *
 * @param instance  Chỉ số kênh FlexCAN (0, 1 hoặc 2).
 * @param enable    true để bật Loop-Back; false để quay về Normal Mode.
 * @return status_t STATUS_SUCCESS nếu chuyển chế độ thành công; STATUS_TIMEOUT nếu không vào/thoát được Freeze Mode.
 */
status_t PTMCAN_SetLoopbackMode(uint8_t instance, bool enable);

#endif /* PTMCAN_DRIVER_H_ */