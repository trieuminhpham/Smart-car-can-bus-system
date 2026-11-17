# 📘 TÀI LIỆU THANH GHI PHẦN CỨNG FLEXCAN – S32K144 (BẢN ĐÃ HIỆU CHỈNH)
**Dựa trên: S32K1xx Reference Manual – Chương FlexCAN, PCC, SCG, PORT**
**Board: NXP S32K144EVB | Compiler: S32DS | CAN0 Base Address: `0x40024000`**

---

> [!IMPORTANT]
> Bản này là bản hợp nhất và sửa lỗi từ hai tài liệu: `flexcan-registers-guide.md` và `FlexCAN_S32K144_Register_Reference.md`. Mọi vị trí bit đã được đối chiếu lại theo cấu trúc thanh ghi chuẩn của S32K1xx (khớp với các `*_MASK` / `*_SHIFT` trong `S32K144.h`). Danh sách chi tiết các chỗ đã sửa nằm ở **Phụ lục A (mục 20)**.
>
> Khuyến nghị: khi nghi ngờ một bit, đối chiếu lần cuối với `S32K144.h` (ví dụ `CAN_CTRL1_CLKSRC_MASK` phải bằng `0x2000`) hoặc Reference Manual bản bạn đang dùng.

---

## MỤC LỤC

| # | Thanh ghi | Mô tả ngắn |
|---|-----------|-----------|
| 1 | PCC[FlexCAN0] | Cấp/tắt clock giao diện cho FlexCAN0 |
| 2 | SCG – SOSCDIV / SPLLDIV | Nguồn clock hệ thống liên quan đến CAN |
| 3 | MCR | Cấu hình tổng thể module FlexCAN |
| 4 | CTRL1 | Bit timing, nguồn clock, ngắt lỗi, chế độ chẩn đoán |
| 5 | CTRL2 | Cấu hình nâng cao FIFO, Tx arbitration, ưu tiên nhận |
| 6 | TIMER | Bộ đếm thời gian tự do (và unlock MB) |
| 7 | RAMn – Message Buffer | Cấu trúc Message Buffer trong RAM |
| 8 | RXMGMASK | Mặt nạ lọc toàn cục |
| 9 | RX14MASK | Mặt nạ lọc riêng MB14 |
| 10 | RX15MASK | Mặt nạ lọc riêng MB15 |
| 11 | RXIMR[n] | Mặt nạ lọc riêng từng MB |
| 12 | IMASK1 | Bật/tắt ngắt từng MB |
| 13 | IFLAG1 | Cờ ngắt từng MB |
| 14 | ESR1 | Trạng thái và lỗi bus CAN |
| 15 | ECR | Bộ đếm lỗi TX/RX |
| 16 | PORTx_PCRn | Cấu hình chân I/O cho CAN |
| 17 | Quy trình khởi tạo | Init FlexCAN0 500 kbps |
| 18 | Bảng địa chỉ | Tóm tắt offset/địa chỉ/reset |
| 19 | Checklist debug | Khi CAN không hoạt động |
| 20 | Phụ lục A | Danh sách lỗi đã sửa |

---

## 1. PCC[FlexCAN0] – Peripheral Clock Controller

### 1.1 Tổng quan

PCC (Peripheral Clock Controller) quản lý clock giao diện (Interface Clock) cho từng ngoại vi. Mỗi ngoại vi có một entry 32-bit riêng trong mảng `PCC->PCCn[]`.

| Ngoại vi | Offset | Địa chỉ | Chỉ số `PCCn[]` |
|----------|--------|---------|-----------------|
| FlexCAN0 | `0x090` | `0x40065090` | 36 |
| FlexCAN1 | `0x094` | `0x40065094` | 37 |
| FlexCAN2 | `0x098` | `0x40065098` | 38 |

- **PCC Base Address**: `0x40065000`
- **Độ rộng**: 32 bit.
- **Reset value**: `0x8000_0000` (PR = 1, CGC = 0).

> [!NOTE]
> FlexCAN không dùng các field PCS / PCD / FRAC của PCC. Nguồn clock của Protocol Engine được chọn bằng bit `CTRL1[CLKSRC]` của chính FlexCAN (xem mục 4).

### 1.2 Cấu trúc bit

| Bit | 31 | 30 | 29:0 |
|-----|----|----|------|
| **Field** | PR | CGC | Reserved |
| **Access** | R | R/W | R |
| **Reset** | 1 | 0 | 0 |

### 1.3 Giải thích từng bit

#### Bit 31 – PR (Peripheral Present)
| Giá trị | Ý nghĩa |
|---------|---------|
| `0` | Peripheral không có trên chip |
| `1` | Peripheral có sẵn trên chip |

#### Bit 30 – CGC (Clock Gate Control)
| Giá trị | Ý nghĩa |
|---------|---------|
| `0` | Clock bị tắt – không thể đọc/ghi thanh ghi FlexCAN, tiết kiệm điện |
| `1` | Clock được cấp – truy cập và cấu hình được thanh ghi FlexCAN |

**Lưu ý:** phải đặt `CGC = 1` **trước** khi truy cập bất kỳ thanh ghi FlexCAN nào.

#### Bits 29:0 – Reserved
Luôn đọc về 0, không ghi 1.

### 1.4 Ví dụ

```c
#include "S32K144.h"

/* Cách 1: dùng header SDK */
void CAN0_EnableClock(void)
{
    PCC->PCCn[PCC_FlexCAN0_INDEX] |= PCC_PCCn_CGC_MASK;   /* = (1U << 30) */
}

void CAN0_DisableClock(void)
{
    PCC->PCCn[PCC_FlexCAN0_INDEX] &= ~PCC_PCCn_CGC_MASK;
}

/* Cách 2: bare-metal trực tiếp (PTM style) */
#define PCC_FLEXCAN0  (*(volatile uint32_t*)0x40065090U)
#define PCC_CGC_BIT   (1U << 30)

void PTM_CAN0_EnableClock(void)
{
    PCC_FLEXCAN0 |= PCC_CGC_BIT;
}
```

---

## 2. SCG – System Clock Generator (liên quan đến CAN)

### 2.1 Tổng quan

FlexCAN chọn clock cho Protocol Engine (PE clock) bằng bit `CTRL1[CLKSRC]`:
- `CLKSRC = 0`: **SOSCDIV2_CLK** (clock thạch anh ngoài sau bộ chia SOSCDIV2).
- `CLKSRC = 1`: **SYS_CLK** (System Clock, ví dụ 80 MHz ở RUN mode cấu hình chuẩn của S32DS).

| Thanh ghi | Offset | Địa chỉ | Mục đích |
|-----------|--------|---------|----------|
| `SCG_RCCR` | `0x014` | `0x40064014` | Chọn nguồn System Clock (RUN mode) |
| `SCG_SOSCDIV` | `0x104` | `0x40064104` | Bộ chia cho System OSC → tạo `SOSCDIV2_CLK` cấp cho FlexCAN |
| `SCG_SPLLDIV` | `0x604` | `0x40064604` | Bộ chia cho SPLL (clock bất đồng bộ của các ngoại vi khác) |

- **SCG Base Address**: `0x40064000`.
- Access: Read/Write, ghi 32-bit. Không ghi được nếu thanh ghi điều khiển tương ứng (`SOSCCSR`/`SPLLCSR`) đang bị khóa (LK = 1).

> [!WARNING]
> `SCG_SPLLDIV2` **không** phải là nguồn clock trực tiếp của FlexCAN. FlexCAN chỉ chọn giữa `SOSCDIV2_CLK` và `SYS_CLK`.

### 2.2 SCG_SOSCDIV – SOSC Clock Divider Register (offset 0x104)

| Bit | 31:11 | 10:8 | 7:3 | 2:0 |
|-----|-------|------|-----|-----|
| **Field** | Reserved | SOSCDIV2 | Reserved | SOSCDIV1 |
| **Access** | R | R/W | R | R/W |
| **Reset** | 0 | 000 | 0 | 000 |

- **SOSCDIV2 (bit 10:8)**: bộ chia cho `SOSCDIV2_CLK` – nguồn clock của FlexCAN khi `CLKSRC = 0`.
- **SOSCDIV1 (bit 2:0)**: bộ chia cho `SOSCDIV1_CLK` – clock bất đồng bộ cho ngoại vi khác.

| Giá trị (SOSCDIV1/2) | Hệ số chia | Output (SOSC = 8 MHz) |
|----------------------|-----------|------------------------|
| `000` | Output bị tắt | — |
| `001` | ÷1 | 8 MHz |
| `010` | ÷2 | 4 MHz |
| `011` | ÷4 | 2 MHz |
| `100` | ÷8 | 1 MHz |
| `101` | ÷16 | 500 kHz |
| `110` | ÷32 | 250 kHz |
| `111` | ÷64 | 125 kHz |

```c
/* SOSCDIV2 = ÷1 → SOSCDIV2_CLK = 8 MHz (cho FlexCAN, CLKSRC = 0) */
SCG->SOSCDIV = (SCG->SOSCDIV & ~SCG_SOSCDIV_SOSCDIV2_MASK) | SCG_SOSCDIV_SOSCDIV2(1);

/* Cấu hình cả hai bộ chia */
SCG->SOSCDIV = SCG_SOSCDIV_SOSCDIV1(1) | SCG_SOSCDIV_SOSCDIV2(1);
```

### 2.3 SCG_SPLLDIV – SPLL Clock Divider Register (offset 0x604)

Cấu trúc giống `SCG_SOSCDIV`:

| Bit | 31:11 | 10:8 | 7:3 | 2:0 |
|-----|-------|------|-----|-----|
| **Field** | Reserved | SPLLDIV2 | Reserved | SPLLDIV1 |

Mã hóa bộ chia giống bảng ở mục 2.2. `SPLLDIV1/2` tạo clock bất đồng bộ cho các ngoại vi như LPUART, LPSPI, FTM… và **không** cấp trực tiếp cho FlexCAN.

```c
SCG->SPLLDIV = SCG_SPLLDIV_SPLLDIV1(2) | SCG_SPLLDIV_SPLLDIV2(2);   /* ÷1 → mã 001, ÷2 → mã 010 */
```

---

## 3. MCR – Module Configuration Register

### 3.1 Tổng quan

MCR là thanh ghi cấu hình tổng thể của FlexCAN. Nó quyết định:
- FlexCAN bật hay tắt (`MDIS`), có ở Freeze Mode hay không (`FRZ`, `HALT`, `FRZACK`).
- Có dùng RX FIFO hay không (`RFEN`) và số Message Buffer tối đa (`MAXMB`).
- Cơ chế lọc CAN ID (`IRMQ`, `IDAM`).
- Các tính năng nâng cao: DMA, Abort, CAN FD, Pretended Networking, Wake-up, Doze.

- **Địa chỉ**: offset `0x000` → CAN0 `0x40024000`, CAN1 `0x40025000`, CAN2 `0x4002B000`.
- **Độ rộng**: 32 bit.
- **Reset value**: `0xD890_000F` (MDIS = 1, FRZ = 1, HALT = 1, NOTRDY = 1, SUPV = 1, LPMACK = 1, MAXMB = 15).

> [!NOTE]
> Sau reset `MDIS = 1`, `FRZ = 1`, `HALT = 1`. Khi xóa `MDIS` để bật module, FlexCAN tự đi vào **Freeze Mode** (cần chờ `FRZACK = 1`) và sẵn sàng để cấu hình.

### 3.2 Cấu trúc bit MCR

| Bit | Tên | Access | Mô tả ngắn |
|-----|-----|--------|-----------|
| 31 | **MDIS** | R/W | Module Disable |
| 30 | **FRZ** | R/W | Freeze Enable |
| 29 | **RFEN** | R/W | Rx FIFO Enable |
| 28 | **HALT** | R/W | Halt FlexCAN |
| 27 | **NOTRDY** | R | FlexCAN Not Ready |
| 26 | **WAKMSK** | R/W | Wake Up Interrupt Mask |
| 25 | **SOFTRST** | R/W | Soft Reset |
| 24 | **FRZACK** | R | Freeze Mode Acknowledge |
| 23 | **SUPV** | R/W | Supervisor Mode |
| 22 | **SLFWAK** | R/W | Self Wake Up |
| 21 | **WRNEN** | R/W | Warning Interrupt Enable |
| 20 | **LPMACK** | R | Low-Power Mode Acknowledge |
| 19 | **WAKSRC** | R/W | Wake Up Source |
| 18 | **DOZE** | R/W | Doze Mode Enable |
| 17 | **SRXDIS** | R/W | Self Reception Disable |
| 16 | **IRMQ** | R/W | Individual Rx Masking and Queue Enable |
| 15 | **DMA** | R/W | DMA Enable |
| 14 | **PNET_EN** | R/W | Pretended Networking Enable |
| 13 | **LPRIOEN** | R/W | Local Priority Enable |
| 12 | **AEN** | R/W | Abort Enable |
| 11 | **FDEN** | R/W | CAN FD Enable |
| 10 | — | R | Reserved |
| 9:8 | **IDAM[1:0]** | R/W | ID Acceptance Mode |
| 7 | — | R | Reserved |
| 6:0 | **MAXMB[6:0]** | R/W | Number of the Last Message Buffer |

### 3.3 Giải thích từng bit

#### Nhóm A – Điều khiển hoạt động và trạng thái

**Bit 31 – MDIS (Module Disable)**

| Giá trị | Ý nghĩa |
|---------|---------|
| `0` | Enable FlexCAN – module hoạt động |
| `1` | Disable FlexCAN – ngắt clock nội bộ của Protocol Engine và Host Interface, module vào Low-Power (`LPMACK = 1` xác nhận) |

```c
CAN0->MCR |=  CAN_MCR_MDIS_MASK;    /* Disable */
CAN0->MCR &= ~CAN_MCR_MDIS_MASK;    /* Enable  */
```

**Bit 30 – FRZ (Freeze Enable)**

| Giá trị | Ý nghĩa |
|---------|---------|
| `0` | Không cho vào Freeze Mode |
| `1` | Cho phép vào Freeze Mode khi `HALT = 1` hoặc khi debug |

`FRZ` không tự đưa module vào Freeze Mode; nó chỉ cho phép khi có yêu cầu `HALT`/debug.

**Bit 29 – RFEN (Rx FIFO Enable)**

| Giá trị | Ý nghĩa |
|---------|---------|
| `0` | Rx FIFO tắt – tất cả MB là MB độc lập |
| `1` | Rx FIFO bật – MB0–MB5 là vùng FIFO, theo sau là bảng filter ID (số filter theo `CTRL2[RFFN]`; với RFFN = 0 thì MB0–MB7 bị chiếm) |

Chỉ thay đổi khi đang ở Freeze Mode.

**Bit 28 – HALT (Halt FlexCAN)**

| Giá trị | Ý nghĩa |
|---------|---------|
| `0` | Không yêu cầu Freeze |
| `1` | Yêu cầu vào Freeze Mode (cần `FRZ = 1`); FlexCAN chờ hết frame hiện tại rồi dừng |

```c
CAN0->MCR |= CAN_MCR_FRZ_MASK | CAN_MCR_HALT_MASK;
while (!(CAN0->MCR & CAN_MCR_FRZACK_MASK)) {}      /* chờ FRZACK = 1 */
```

**Bit 27 – NOTRDY (FlexCAN Not Ready)** – chỉ đọc

| Giá trị | Ý nghĩa |
|---------|---------|
| `0` | FlexCAN ở Normal Mode (hoặc Listen-Only/Loopback) |
| `1` | FlexCAN ở Freeze Mode, Disable Mode hoặc Low-Power Mode |

```c
while (CAN0->MCR & CAN_MCR_NOTRDY_MASK) {}    /* chờ module sẵn sàng sau khi thoát Freeze */
```

**Bit 26 – WAKMSK (Wake Up Interrupt Mask)**

| Giá trị | Ý nghĩa |
|---------|---------|
| `0` | Tắt ngắt Wake-up (`ESR1[WAKINT]`) |
| `1` | Bật ngắt Wake-up |

**Bit 25 – SOFTRST (Soft Reset)**

| Giá trị | Ý nghĩa |
|---------|---------|
| `0` | Không reset / reset đã hoàn tất |
| `1` | Bắt đầu Soft Reset; bit tự xóa về 0 khi xong |

Soft Reset reset state machine và các thanh ghi chịu ảnh hưởng của soft reset (không phải mọi bit của MCR).

```c
CAN0->MCR |= CAN_MCR_SOFTRST_MASK;
while (CAN0->MCR & CAN_MCR_SOFTRST_MASK) {}
```

**Bit 24 – FRZACK (Freeze Mode Acknowledge)** – chỉ đọc

| Giá trị | Ý nghĩa |
|---------|---------|
| `0` | Chưa ở Freeze Mode |
| `1` | Đã vào Freeze Mode – an toàn để cấu hình `CTRL1`, `CTRL2`, `MAXMB`, mask… |

#### Nhóm B – Quyền truy cập, cảnh báo, low-power

**Bit 23 – SUPV (Supervisor Mode)**

| Giá trị | Ý nghĩa |
|---------|---------|
| `0` | User Mode – cả User và Supervisor đều truy cập được thanh ghi |
| `1` | Supervisor Mode (mặc định sau reset) – chỉ Supervisor truy cập được |

**Bit 22 – SLFWAK (Self Wake Up)**

| Giá trị | Ý nghĩa |
|---------|---------|
| `0` | Tắt tự đánh thức |
| `1` | Bật: FlexCAN tự thoát Low-Power khi phát hiện hoạt động trên bus (dùng cùng bộ lọc wake-up) |

**Bit 21 – WRNEN (Warning Interrupt Enable)**

| Giá trị | Ý nghĩa |
|---------|---------|
| `0` | `ESR1[TWRNINT]` và `ESR1[RWRNINT]` không được set |
| `1` | Cho phép set các cờ cảnh báo khi TEC/REC ≥ 96 (còn cần `CTRL1[TWRNMSK]/[RWRNMSK]` để sinh ngắt) |

**Bit 20 – LPMACK (Low-Power Mode Acknowledge)** – chỉ đọc

| Giá trị | Ý nghĩa |
|---------|---------|
| `0` | FlexCAN không ở Low-Power Mode |
| `1` | FlexCAN đã vào Low-Power Mode (ví dụ khi `MDIS = 1`) |

**Bit 19 – WAKSRC (Wake Up Source)**

| Giá trị | Ý nghĩa |
|---------|---------|
| `0` | Wake-up dùng tín hiệu Rx trực tiếp |
| `1` | Wake-up qua bộ lọc thông thấp tích hợp (Low-pass filter) |

**Bit 18 – DOZE (Doze Mode Enable)**

| Giá trị | Ý nghĩa |
|---------|---------|
| `0` | Không hỗ trợ Doze Mode |
| `1` | Cho phép vào Low-Power khi MCU yêu cầu Doze |

**Bit 17 – SRXDIS (Self Reception Disable)**

| Giá trị | Ý nghĩa |
|---------|---------|
| `0` | Cho phép nhận lại frame do chính mình phát (self-reception) |
| `1` | Tắt self-reception |

```c
CAN0->MCR |= CAN_MCR_SRXDIS_MASK;     /* thực tế: thường tắt self-reception */
```

> [!NOTE]
> Khi dùng Loopback Mode (`CTRL1[LPB] = 1`) và muốn tự nhận frame, phải để `SRXDIS = 0`.

**Bit 16 – IRMQ (Individual Rx Masking and Queue Enable)**

| Giá trị | Ý nghĩa |
|---------|---------|
| `0` | Dùng mask toàn cục: `RXMGMASK`, `RX14MASK`, `RX15MASK` (chế độ tương thích: đọc CS word khóa MB ngay cả khi MB ở trạng thái EMPTY) |
| `1` | Dùng mask riêng `RXIMR[n]` cho từng MB và bật Rx queue |

```c
CAN0->MCR |= CAN_MCR_IRMQ_MASK;      /* khuyến nghị: dùng mask riêng cho từng MB */
```

#### Nhóm C – Tính năng nâng cao

**Bit 15 – DMA (DMA Enable)**

| Giá trị | Ý nghĩa |
|---------|---------|
| `0` | DMA tắt |
| `1` | Bật DMA để chuyển dữ liệu từ Rx FIFO (cần `RFEN = 1`) |

**Bit 14 – PNET_EN (Pretended Networking Enable)**

| Giá trị | Ý nghĩa |
|---------|---------|
| `0` | Tắt Pretended Networking |
| `1` | Bật Pretended Networking – CAN vẫn lọc frame khi MCU ngủ |

**Bit 13 – LPRIOEN (Local Priority Enable)**

| Giá trị | Ý nghĩa |
|---------|---------|
| `0` | Không dùng trường PRIO trong ID word |
| `1` | Dùng PRIO (bit 31:29 của ID word) để sắp xếp thứ tự truyền |

**Bit 12 – AEN (Abort Enable)**

| Giá trị | Ý nghĩa |
|---------|---------|
| `0` | Không cho phép abort |
| `1` | Cho phép hủy truyền bằng cách ghi CODE = `0x9` (TX ABORT) vào MB |

> [!NOTE]
> Chỉ thay đổi `AEN` khi đang ở Freeze Mode.

**Bit 11 – FDEN (CAN FD Enable)**

| Giá trị | Ý nghĩa |
|---------|---------|
| `0` | CAN 2.0 cổ điển (tối đa 8 byte) |
| `1` | CAN FD (payload đến 64 byte, cấu trúc MB thay đổi) – chỉ có trên instance hỗ trợ FD (S32K144: CAN0) |

#### Nhóm D – Bộ lọc FIFO và số MB

**Bits 9:8 – IDAM[1:0] (ID Acceptance Mode)**

Định dạng ô bảng filter khi dùng Rx FIFO (`RFEN = 1`):

| IDAM | Format | Mô tả |
|------|--------|-------|
| `00` | A | Mỗi ô chứa **1 ID đầy đủ** (Standard hoặc Extended) |
| `01` | B | Mỗi ô chứa **2 ID Standard đầy đủ** hoặc **2 ID Extended 14-bit (phần cao)** |
| `10` | C | Mỗi ô chứa **4 ID 8-bit** (so sánh 8 bit cao của ID) |
| `11` | D | **Từ chối tất cả frame** (Reject All) |

**Bits 6:0 – MAXMB[6:0] (Number of the Last Message Buffer)**

Số MB active = `MAXMB + 1`. Ví dụ `MAXMB = 15` → MB0–MB15 (16 MB).

| MAXMB | Số MB active |
|-------|-------------|
| `0x0F` | 16 (MB0–MB15) |
| `0x1F` | 32 (MB0–MB31) |

> [!NOTE]
> Số MB tối đa tùy instance: ví dụ S32K144 CAN0 có 32 MB (RAM 512 byte), CAN1/CAN2 có 16 MB. Một số chip khác (như S32K148) có đến 64 MB.

```c
CAN0->MCR = (CAN0->MCR & ~CAN_MCR_MAXMB_MASK) | CAN_MCR_MAXMB(15U);   /* 16 MB, chỉ khi ở Freeze Mode */
```

---

## 4. CTRL1 – Control Register 1

### 4.1 Tổng quan

CTRL1 chứa toàn bộ tham số **Bit Timing** (baud rate), chọn nguồn clock Protocol Engine, bật Loopback/Listen-Only và các mask ngắt lỗi.

- **Địa chỉ**: offset `0x004` → `0x40024004`.
- **Độ rộng**: 32 bit. **Reset value**: `0x0000_0000`.
- Hầu hết các field chỉ ghi được khi ở **Freeze Mode**. Riêng `CLKSRC` chỉ ghi được khi ở **Disable Mode** (`MDIS = 1`).

### 4.2 Cấu trúc bit CTRL1

| Bit | Tên | Access | Mô tả ngắn |
|-----|-----|--------|-----------|
| 31:24 | **PRESDIV[7:0]** | R/W | Prescaler Division Factor |
| 23:22 | **RJW[1:0]** | R/W | Resynchronization Jump Width |
| 21:19 | **PSEG1[2:0]** | R/W | Phase Segment 1 |
| 18:16 | **PSEG2[2:0]** | R/W | Phase Segment 2 |
| 15 | **BOFFMSK** | R/W | Bus Off Interrupt Mask |
| 14 | **ERRMSK** | R/W | Error Interrupt Mask |
| 13 | **CLKSRC** | R/W | CAN Engine Clock Source |
| 12 | **LPB** | R/W | Loop Back Mode |
| 11 | **TWRNMSK** | R/W | Tx Warning Interrupt Mask |
| 10 | **RWRNMSK** | R/W | Rx Warning Interrupt Mask |
| 9:8 | — | R | Reserved |
| 7 | **SMP** | R/W | CAN Bit Sampling |
| 6 | **BOFFREC** | R/W | Bus Off Recovery |
| 5 | **TSYN** | R/W | Timer Sync |
| 4 | **LBUF** | R/W | Lowest Buffer Transmitted First |
| 3 | **LOM** | R/W | Listen-Only Mode |
| 2:0 | **PROPSEG[2:0]** | R/W | Propagation Segment |

### 4.3 Giải thích từng bit

#### Nhóm A – Bit Timing

> [!IMPORTANT]
> Mỗi bit CAN được chia thành các Time Quantum (Tq):
>
> `Bit Time = 1 (SyncSeg) + (PROPSEG + 1) + (PSEG1 + 1) + (PSEG2 + 1)` [Tq]
> `Tq = (PRESDIV + 1) / f_PE`
> `Baud rate = 1 / (Bit Time × Tq)`
> `Sample point = (1 + (PROPSEG + 1) + (PSEG1 + 1)) / Bit Time`

**Bits 31:24 – PRESDIV[7:0] (Prescaler Division Factor)**

Hệ số chia clock PE tạo Time Quantum: `Tq = (PRESDIV + 1) / f_PE`.

**Bits 23:22 – RJW[1:0] (Resynchronization Jump Width)**

Độ dài tối đa được kéo dài/rút ngắn khi tái đồng bộ: `RJW_thực = RJW + 1` Tq.

| RJW | Giá trị thực |
|-----|--------------|
| `00` | 1 Tq |
| `01` | 2 Tq |
| `10` | 3 Tq |
| `11` | 4 Tq |

Ràng buộc: `RJW_thực ≤ min(PSEG1_thực, PSEG2_thực)`.

**Bits 21:19 – PSEG1[2:0] (Phase Segment 1)**

Phần trước điểm lấy mẫu: `PSEG1_thực = PSEG1 + 1` Tq.

**Bits 18:16 – PSEG2[2:0] (Phase Segment 2)**

Phần sau điểm lấy mẫu: `PSEG2_thực = PSEG2 + 1` Tq (nên ≥ 2 Tq theo yêu cầu thời gian xử lý thông tin).

**Bits 2:0 – PROPSEG[2:0] (Propagation Segment)**

Bù trễ lan truyền trên bus và transceiver: `PROPSEG_thực = PROPSEG + 1` Tq.

#### Ví dụ tính Bit Timing 500 kbps

**Trường hợp 1 – `CLKSRC = 0`, `SOSCDIV2_CLK` = 8 MHz:**

```
Bit Time mục tiêu = 2 µs
PRESDIV = 0 → Tq = 1 / 8 MHz = 125 ns → 16 Tq / bit

SyncSeg      = 1 Tq
PROPSEG  = 6 → 7 Tq
PSEG1    = 3 → 4 Tq
PSEG2    = 3 → 4 Tq
Tổng = 1 + 7 + 4 + 4 = 16 Tq → 16 × 125 ns = 2 µs ✓ (500 kbps)
Sample point = (1 + 7 + 4) / 16 = 75 %
RJW = 1 (2 Tq)
```

**Trường hợp 2 – `CLKSRC = 1`, `SYS_CLK` = 80 MHz:** giữ nguyên các segment, chỉ đổi `PRESDIV = 9` (Tq = 10 / 80 MHz = 125 ns).

**Các baud rate khác (16 Tq/bit, `f_PE` = 8 MHz, cùng PROPSEG = 6, PSEG1 = 3, PSEG2 = 3):**

| Baud rate | PRESDIV |
|-----------|---------|
| 500 kbps | 0 |
| 250 kbps | 1 |
| 125 kbps | 3 |

```c
/* 500 kbps, f_PE = 8 MHz (SOSCDIV2_CLK) */
CAN0->CTRL1 = CAN_CTRL1_PRESDIV(0)   |
              CAN_CTRL1_PROPSEG(6)   |
              CAN_CTRL1_PSEG1(3)     |
              CAN_CTRL1_PSEG2(3)     |
              CAN_CTRL1_RJW(1)       |
              CAN_CTRL1_CLKSRC(0);
```

#### Nhóm B – Mask ngắt

**Bit 15 – BOFFMSK (Bus Off Interrupt Mask)**

| Giá trị | Ý nghĩa |
|---------|---------|
| `0` | Tắt ngắt Bus-Off |
| `1` | Bật ngắt khi `ESR1[BOFFINT] = 1` |

**Bit 14 – ERRMSK (Error Interrupt Mask)**

| Giá trị | Ý nghĩa |
|---------|---------|
| `0` | Tắt ngắt lỗi |
| `1` | Bật ngắt khi `ESR1[ERRINT] = 1` (có lỗi bus CAN) |

**Bit 11 – TWRNMSK (Tx Warning Interrupt Mask)**

| Giá trị | Ý nghĩa |
|---------|---------|
| `0` | Tắt ngắt Tx Warning |
| `1` | Bật ngắt khi `ESR1[TWRNINT] = 1` (TEC ≥ 96, cần `MCR[WRNEN] = 1`) |

**Bit 10 – RWRNMSK (Rx Warning Interrupt Mask)**

| Giá trị | Ý nghĩa |
|---------|---------|
| `0` | Tắt ngắt Rx Warning |
| `1` | Bật ngắt khi `ESR1[RWRNINT] = 1` (REC ≥ 96, cần `MCR[WRNEN] = 1`) |

#### Nhóm C – Nguồn clock và chế độ chẩn đoán

**Bit 13 – CLKSRC (CAN Engine Clock Source)**

| Giá trị | Nguồn clock |
|---------|------------|
| `0` | **SOSCDIV2_CLK** (clock bất đồng bộ từ thạch anh ngoài, ví dụ 8 MHz) |
| `1` | **SYS_CLK** (System Clock nội bộ, ví dụ 80 MHz) |

> [!WARNING]
> `CLKSRC` chỉ ghi được khi module ở **Disable Mode** (`MDIS = 1`). Với `CLKSRC = 0`, phải bật `SCG_SOSCDIV[SOSCDIV2]` (khác 0) để có clock.

**Bit 12 – LPB (Loop Back Mode)**

| Giá trị | Ý nghĩa |
|---------|---------|
| `0` | Hoạt động bình thường |
| `1` | Loopback: luồng bit phát được đưa nội bộ về đầu thu, pin Rx bị bỏ qua, pin Tx ở mức recessive |

Muốn nhận lại frame đã phát trong Loopback thì `MCR[SRXDIS]` phải bằng 0.

**Bit 3 – LOM (Listen-Only Mode)**

| Giá trị | Ý nghĩa |
|---------|---------|
| `0` | Chế độ hai chiều chuẩn |
| `1` | Listen-Only: chỉ nhận, không phát ACK, không phát frame, không phát Error Frame |

```c
CAN0->CTRL1 |= CAN_CTRL1_LOM_MASK;     /* monitor bus mà không ảnh hưởng giao tiếp */
```

#### Nhóm D – Lấy mẫu, phục hồi, thứ tự truyền

**Bit 7 – SMP (CAN Bit Sampling)**

| Giá trị | Ý nghĩa |
|---------|---------|
| `0` | Lấy mẫu 1 lần tại điểm lấy mẫu |
| `1` | Lấy mẫu 3 lần, lấy giá trị đa số (majority); dùng khi baud rate thấp |

**Bit 6 – BOFFREC (Bus Off Recovery)**

| Giá trị | Ý nghĩa |
|---------|---------|
| `0` | Tự động phục hồi khỏi Bus-Off theo chuẩn CAN 2.0B |
| `1` | Tắt tự động phục hồi – phần mềm phải tự điều khiển việc rời Bus-Off |

**Bit 5 – TSYN (Timer Sync)**

| Giá trị | Ý nghĩa |
|---------|---------|
| `0` | Tắt đồng bộ timer |
| `1` | Bật: `TIMER` được reset khi nhận thành công frame vào MB đầu tiên (MB0 hoặc phần tử FIFO đầu) |

**Bit 4 – LBUF (Lowest Buffer Transmitted First)**

| Giá trị | Ý nghĩa |
|---------|---------|
| `0` | MB có CAN ID ưu tiên cao nhất (ID nhỏ nhất) được gửi trước (khi `LPRIOEN = 0`) |
| `1` | MB có chỉ số nhỏ nhất được gửi trước |

---

## 5. CTRL2 – Control Register 2

### 5.1 Tổng quan

CTRL2 chứa các tùy chọn nâng cao: số filter Rx FIFO, trì hoãn trọng tài Tx, ưu tiên nhận giữa FIFO và MB, cách so sánh arbitration field, nguồn timer, ISO CAN FD, ngắt Bus-Off Done.

- **Địa chỉ**: offset `0x034` → `0x40024034`.
- **Độ rộng**: 32 bit.
- **Reset value**: `0x00B0_0000` (TASD = 22 = `0b10110`; các bit còn lại = 0).

### 5.2 Cấu trúc bit CTRL2

| Bit | Tên | Access | Mô tả ngắn |
|-----|-----|--------|-----------|
| 31 | **ERRMSK_FAST** | R/W | Mask ngắt lỗi trong data phase (CAN FD) |
| 30 | **BOFFDONEMSK** | R/W | Mask ngắt Bus-Off Done |
| 29 | **ECRWRE** | R/W | Cho phép ghi thanh ghi cấu hình ECC |
| 28 | **WRMFRZ** | R/W | Write Access to Memory in Freeze Mode |
| 27:24 | **RFFN[3:0]** | R/W | Number of Rx FIFO Filters |
| 23:19 | **TASD[4:0]** | R/W | Tx Arbitration Start Delay |
| 18 | **MRP** | R/W | Mailboxes Reception Priority |
| 17 | **RRS** | R/W | Remote Request Storing |
| 16 | **EACEN** | R/W | Entire Frame Arbitration Field Comparison Enable |
| 15 | **TIMER_SRC** | R/W | Timer Source |
| 14 | **PREXCEN** | R/W | Protocol Exception Enable |
| 13 | — | R | Reserved |
| 12 | **ISOCANFDEN** | R/W | ISO CAN FD Enable |
| 11 | **EDFLTDIS** | R/W | Edge Filter Disable |
| 10:0 | — | R | Reserved |

### 5.3 Giải thích từng bit

**Bit 31 – ERRMSK_FAST**: `0` tắt, `1` bật ngắt khi có lỗi trong data phase của frame CAN FD tốc độ cao (`ESR1[ERRINT_FAST]`).

**Bit 30 – BOFFDONEMSK**: `0` tắt, `1` bật ngắt khi hoàn tất phục hồi Bus-Off (`ESR1[BOFFDONEINT]`).

**Bit 29 – ECRWRE**: cho phép ghi thanh ghi cấu hình ECC bộ nhớ. Ứng dụng cơ bản giữ ở `0`.

**Bit 28 – WRMFRZ (Write Access to Memory in Freeze Mode)**

| Giá trị | Ý nghĩa |
|---------|---------|
| `0` | Giữ các hạn chế ghi vùng nhớ theo quy định của RM |
| `1` | Cho phép ghi tự do vào vùng RAM FlexCAN khi đang ở Freeze Mode |

**Bits 27:24 – RFFN[3:0] (Number of Rx FIFO Filters)**

Chỉ có hiệu lực khi `MCR[RFEN] = 1`. Số filter = `8 × (RFFN + 1)`. MB cuối cùng bị FIFO + bảng filter chiếm (với payload 8 byte, `FDEN = 0`) = `7 + 2 × RFFN`.

| RFFN | Số filter | MB bị chiếm |
|------|-----------|-------------|
| `0000` | 8 | MB0–MB7 |
| `0001` | 16 | MB0–MB9 |
| `0010` | 24 | MB0–MB11 |
| `0011` | 32 | MB0–MB13 |
| … | … | … |
| `1111` | 128 | MB0–MB37 |

**Bits 23:19 – TASD[4:0] (Tx Arbitration Start Delay)**

Số chu kỳ (theo Tq) trì hoãn trước khi bắt đầu trọng tài chọn MB để gửi, giúp CPU kịp cập nhật MB. Giá trị reset/thường dùng: `22` (0x16).

**Bit 18 – MRP (Mailboxes Reception Priority)**

| Giá trị | Ý nghĩa |
|---------|---------|
| `0` | So khớp ID bắt đầu từ Rx FIFO rồi mới đến các MB |
| `1` | So khớp ID bắt đầu từ các MB rồi mới đến Rx FIFO |

**Bit 17 – RRS (Remote Request Storing)**

| Giá trị | Ý nghĩa |
|---------|---------|
| `0` | Remote Request Frame nhận được sẽ được trả lời tự động bằng Data Frame (MB có CODE = RANSWER) |
| `1` | Remote Request Frame nhận được được lưu vào Rx MB như một frame bình thường |

**Bit 16 – EACEN (Entire Frame Arbitration Field Comparison Enable)**

| Giá trị | Ý nghĩa |
|---------|---------|
| `0` | Khi so khớp Rx MB: so cả IDE, không so RTR |
| `1` | So khớp cả bit RTR và IDE (mask RTR/IDE nằm ở bit 31/30 của các thanh ghi mask) |

**Bit 15 – TIMER_SRC (Timer Source)**

| Giá trị | Ý nghĩa |
|---------|---------|
| `0` | `TIMER` 16-bit chạy theo CAN bit clock |
| `1` | `TIMER` chạy theo external time tick |

**Bit 14 – PREXCEN (Protocol Exception Enable)**: `0` tắt, `1` bật xử lý Protocol Exception (CAN FD).

**Bit 12 – ISOCANFDEN (ISO CAN FD Enable)**: `0` dùng CAN FD non-ISO (Bosch), `1` dùng ISO CAN FD.

**Bit 11 – EDFLTDIS (Edge Filter Disable)**: `0` bật bộ lọc cạnh (lọc nhiễu trên đường Rx), `1` tắt bộ lọc cạnh.

---

## 6. TIMER – Free-Running Timer

### 6.1 Tổng quan

`TIMER` là bộ đếm 16-bit chạy liên tục (clock theo CAN bit clock khi `CTRL2[TIMER_SRC] = 0`). Hai vai trò chính:
1. Nguồn **timestamp** cho mỗi Message Buffer khi gửi/nhận.
2. **Đọc `TIMER` để unlock MB** đã bị khóa khi đọc CS word.

- **Địa chỉ**: offset `0x008` → `0x40024008`.
- **Độ rộng**: 32 bit (16 bit thấp hoạt động). **Reset value**: `0x0000_0000`.

### 6.2 Cấu trúc bit

| Bit | 31:16 | 15:0 |
|-----|-------|------|
| **Field** | Reserved | TIMER |
| **Access** | R | R/W |

### 6.3 Giải thích

**Bits 15:0 – TIMER**
- Tự động tăng, mỗi tick theo một bit time (khi `TIMER_SRC = 0`).
- Đọc → giá trị hiện tại; **ghi** → nạp giá trị được ghi vào bộ đếm (muốn về 0 thì ghi 0).
- Có thể bị reset bởi `CTRL1[TSYN]` khi nhận frame vào MB đồng bộ.

```c
uint16_t ts = (uint16_t)(CAN0->TIMER & CAN_TIMER_TIMER_MASK);   /* đọc timestamp hiện tại */
CAN0->TIMER = 0U;                                               /* nạp giá trị 0 */
```

### 6.4 Cơ chế Unlock Message Buffer

Khi CPU đọc một Rx MB:
1. Đọc `CS` word → MB bị **lock**, FlexCAN không ghi frame mới vào MB đó.
2. Đọc `ID` và các word dữ liệu.
3. Xóa cờ `IFLAG1[n]` (ghi 1).
4. **Đọc `TIMER`** → MB được **unlock**.

```c
volatile uint32_t *mb = &CAN0->RAMn[mb_idx * 4U];
uint32_t cs    = mb[0];        /* lock MB */
uint32_t id    = mb[1];
uint32_t data0 = mb[2];
uint32_t data1 = mb[3];
CAN0->IFLAG1 = (1U << mb_idx); /* xóa cờ (W1C) */
(void)CAN0->TIMER;             /* unlock MB */
```

> [!WARNING]
> Quên đọc `TIMER` sau khi đọc MB có thể khiến MB bị giữ lock, FlexCAN không nạp được frame mới vào MB đó.

---

## 7. RAMn – Message Buffer Structure

### 7.1 Tổng quan

`RAMn` là vùng RAM nội bộ của FlexCAN, chia thành các **Message Buffer (MB)**. Với CAN 2.0 (payload ≤ 8 byte) mỗi MB gồm 4 word 32-bit (16 byte).

- **Địa chỉ bắt đầu**: offset `0x080` → `0x40024080`.
- **Kích thước**: S32K144 CAN0 có RAM 512 byte (`0x080`–`0x27F`) = 128 word = 32 MB. CAN1/CAN2 có 16 MB.

```
MB(n):  RAMn[n*4 + 0] = CS   (Control & Status)
        RAMn[n*4 + 1] = ID
        RAMn[n*4 + 2] = Data Byte 0..3
        RAMn[n*4 + 3] = Data Byte 4..7

MB0 @ 0x40024080, MB1 @ 0x40024090, ... (mỗi MB cách nhau 0x10)
```

### 7.2 Word 0 – CS (Control and Status)

| Bit | 31 | 30 | 29 | 28 | 27:24 | 23 | 22 | 21 | 20 | 19:16 | 15:0 |
|-----|----|----|----|----|-------|----|----|----|----|-------|------|
| **Field** | EDL | BRS | ESI | — | CODE | — | SRR | IDE | RTR | DLC | TIME STAMP |
| **Access** | R/W | R/W | R | R | R/W | R | R/W | R/W | R/W | R/W | R |

| Bits | Tên | Mô tả |
|------|-----|-------|
| 31 | **EDL** | Extended Data Length (CAN FD) |
| 30 | **BRS** | Bit Rate Switch (CAN FD) |
| 29 | **ESI** | Error State Indicator (CAN FD) |
| 27:24 | **CODE** | Mã trạng thái MB (bảng bên dưới) |
| 22 | **SRR** | Substitute Remote Request (frame Extended: luôn ghi 1) |
| 21 | **IDE** | `0` = Standard ID 11-bit, `1` = Extended ID 29-bit |
| 20 | **RTR** | `0` = Data Frame, `1` = Remote Frame |
| 19:16 | **DLC** | Data Length Code (0–8; 9–15 dành cho CAN FD) |
| 15:0 | **TIME STAMP** | Giá trị `TIMER` lúc frame được gửi/nhận |

#### CODE cho Rx Message Buffer

| CODE | Trạng thái | Mô tả |
|------|-----------|-------|
| `0x0` | RX INACTIVE | MB không hoạt động |
| `0x4` | **RX EMPTY** | Sẵn sàng, đang chờ frame |
| `0x2` | **RX FULL** | Vừa nhận frame mới, CPU đọc được |
| `0x6` | RX OVERRUN | Frame mới đè lên frame chưa đọc |
| `0x1` | RX BUSY (cờ) | FlexCAN đang cập nhật MB (có thể đi cùng FULL/OVERRUN) – CPU phải đọc lại |
| `0xA` | RX RANSWER | Tự trả lời Remote Request |

#### CODE cho Tx Message Buffer

| CODE | Trạng thái | Mô tả |
|------|-----------|-------|
| `0x8` | **TX INACTIVE** | MB chờ, chưa kích hoạt gửi |
| `0x9` | TX ABORT | Yêu cầu hủy gửi (cần `MCR[AEN] = 1`) |
| `0xC` | **TX DATA** | Gửi 1 lần: Data Frame nếu `RTR = 0`, **Remote Frame nếu `RTR = 1`**; xong MB về INACTIVE |
| `0xE` | TX TANSWER | Trả lời Remote Request khi nhận frame khớp ID |

### 7.3 Word 1 – ID

| Bit | 31:29 | 28:18 | 17:0 |
|-----|-------|-------|------|
| **Field** | PRIO | ID_STD (hoặc ID[28:18]) | ID_EXT (ID[17:0]) |

- **PRIO[2:0]**: dùng khi `MCR[LPRIOEN] = 1`.
- **Standard Frame (IDE = 0)**: ID 11-bit nằm ở bit 28:18.
- **Extended Frame (IDE = 1)**: ID 29-bit nằm ở bit 28:0.

```c
mb[1] = (0x123U << 18) & 0x1FFC0000U;    /* Standard ID 0x123 */
mb[1] = 0x12345678U & 0x1FFFFFFFU;       /* Extended ID 0x12345678 (đặt IDE = 1 trong CS) */
```

### 7.4 Word 2 & 3 – Data

- Word 2: Data Byte 0–3 (Byte 0 ở bit 31:24, Byte 3 ở bit 7:0).
- Word 3: Data Byte 4–7 (Byte 4 ở bit 31:24, Byte 7 ở bit 7:0).

> [!WARNING]
> S32K144 (Cortex-M4) là Little-Endian, nhưng MB lưu dữ liệu theo thứ tự byte đầu tiên ở bit cao nhất của word. Nên đóng/gỡ byte bằng phép dịch bit như ví dụ dưới (tránh ép kiểu con trỏ `uint8_t*` sang `uint32_t*`).

```c
static inline uint32_t PTM_PackBE32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}
```

### 7.5 Ví dụ cấu hình và gửi/nhận

```c
/* ============= CẤU HÌNH TX MAILBOX ============= */
void PTM_CAN0_ConfigTxMB(uint8_t mb_idx)
{
    volatile uint32_t *mb = &CAN0->RAMn[mb_idx * 4U];

    mb[0] = (0x8U << 24);      /* CODE = TX INACTIVE */
    mb[1] = 0U;
    mb[2] = 0U;
    mb[3] = 0U;
}

/* ============= GỬI FRAME (Standard ID) ============= */
void PTM_CAN0_Transmit(uint8_t mb_idx, uint32_t id, uint8_t dlc, const uint8_t *data)
{
    volatile uint32_t *mb = &CAN0->RAMn[mb_idx * 4U];

    mb[0] = (0x8U << 24);                              /* 1. TX INACTIVE */
    mb[1] = (id << 18) & 0x1FFC0000U;                  /* 2. Standard ID */

    if (data != NULL) {                                /* 3. Dữ liệu */
        uint8_t buf[8] = {0};
        for (uint8_t i = 0U; (i < dlc) && (i < 8U); i++) { buf[i] = data[i]; }
        mb[2] = PTM_PackBE32(&buf[0]);
        mb[3] = PTM_PackBE32(&buf[4]);
    }

    mb[0] = (0xCU << 24) | ((uint32_t)dlc << 16);      /* 4. CODE = TX DATA, IDE = 0 → kích hoạt gửi */
}

/* ============= CẤU HÌNH RX MAILBOX ============= */
void PTM_CAN0_ConfigRxMB(uint8_t mb_idx, uint32_t filter_id)
{
    volatile uint32_t *mb = &CAN0->RAMn[mb_idx * 4U];

    mb[0] = 0U;                                        /* 1. RX INACTIVE */
    mb[1] = 0U;
    mb[2] = 0U;
    mb[3] = 0U;

    mb[1] = (filter_id << 18) & 0x1FFC0000U;           /* 2. ID filter */
    CAN0->RXIMR[mb_idx] = 0x1FFC0000U;                 /* 3. Mask: so khớp đủ 11-bit (cần IRMQ = 1) */

    mb[0] = (0x4U << 24);                              /* 4. CODE = RX EMPTY */
}
```

---

## 8. RXMGMASK – Rx Mailboxes Global Mask Register

### 8.1 Tổng quan

`RXMGMASK` là mặt nạ toàn cục. Khi `MCR[IRMQ] = 0`, tất cả Rx MB (trừ MB14 và MB15, có mask riêng) dùng chung mask này để lọc ID.

- **Địa chỉ**: offset `0x010` → `0x40024010`.
- **Độ rộng**: 32 bit. **Reset value**: `0xFFFF_FFFF` (so khớp chính xác mọi bit).
- Chỉ ghi được khi ở Freeze Mode.

### 8.2 Cấu trúc bit

| Bit | 31 | 30 | 29 | 28:18 | 17:0 |
|-----|----|----|----|-------|------|
| **Field** | RTR mask | IDE mask | — | MG[28:18] (STD ID) | MG[17:0] (EXT ID) |

Bit 31 (RTR) và bit 30 (IDE) chỉ có ý nghĩa khi dùng kiểm tra RTR/IDE theo `CTRL2[EACEN] = 1`.

### 8.3 Giải thích

| Giá trị bit mask | Ý nghĩa |
|------------------|---------|
| `0` | Don't care – bit ID tương ứng được bỏ qua |
| `1` | Bit ID tương ứng của frame nhận phải khớp với ID trong MB |

```c
/* Khớp chính xác 11-bit Standard ID */
CAN0->RXMGMASK = 0x1FFC0000U;

/* Nhận tất cả */
CAN0->RXMGMASK = 0x00000000U;

/* Chỉ so 8 bit cao của Standard ID (bỏ qua 3 bit thấp: 0x100..0x107 đều qua lọc) */
CAN0->RXMGMASK = 0x1FE00000U;

/* So 7 bit cao (0x100..0x10F đều qua lọc) */
CAN0->RXMGMASK = (0x7F0U << 18);
```

> [!NOTE]
> Khi `IRMQ = 1`, `RXMGMASK` không còn tác dụng cho MB (dùng `RXIMR[n]`). Khi `RFEN = 1`, bộ lọc Rx FIFO dùng `RXFGMASK` (offset `0x048`) nếu `IRMQ = 0`, hoặc `RXIMR` nếu `IRMQ = 1`.

---

## 9. RX14MASK – Rx Mailbox 14 Mask

- **Địa chỉ**: offset `0x014` → `0x40024014`. **Reset value**: `0xFFFF_FFFF`.
- Cấu trúc bit **giống hệt** `RXMGMASK`; chỉ áp dụng cho **MB14** khi `IRMQ = 0`.

```c
CAN0->RX14MASK = 0x00000000U;    /* MB14 nhận tất cả */
CAN0->RX14MASK = 0x1FFC0000U;    /* MB14 khớp chính xác Standard ID 11-bit */
```

> [!TIP]
> Khi `IRMQ = 1`, `RX14MASK` không có tác dụng; dùng `RXIMR[14]`.

---

## 10. RX15MASK – Rx Mailbox 15 Mask

- **Địa chỉ**: offset `0x018` → `0x40024018`. **Reset value**: `0xFFFF_FFFF`.
- Cấu trúc bit giống `RXMGMASK`; chỉ áp dụng cho **MB15** khi `IRMQ = 0`.

```c
CAN0->RX15MASK = 0x1FFC0000U;    /* MB15 khớp chính xác Standard ID 11-bit */
```

---

## 11. RXIMR[n] – Rx Individual Mask Registers

### 11.1 Tổng quan

Mảng `RXIMR[n]` cung cấp mask riêng cho từng MB khi `MCR[IRMQ] = 1`.

- **Địa chỉ**: offset `0x880 + n × 4` → `0x40024880 + n × 4`. Với S32K144 CAN0, `n = 0…31` (`RXIMR[31]` @ `0x400248FC`).
- **Độ rộng**: 32 bit. **Reset value**: `0xFFFF_FFFF`.

### 11.2 Cấu trúc bit

| Bit | 31 | 30 | 29 | 28:0 |
|-----|----|----|----|------|
| **Field** | RTR mask | IDE mask | — | MI[28:0] |

- **Standard Frame**: MI[28:18] lọc 11-bit Standard ID.
- **Extended Frame**: MI[28:0] lọc 29-bit Extended ID.
- Bit 31 (RTR) và bit 30 (IDE): có tác dụng khi `CTRL2[EACEN] = 1` (so cả RTR và IDE).

### 11.3 Logic lọc

| Bit mask | Ý nghĩa |
|----------|---------|
| `1` | Bit ID phải khớp giữa ID trong MB và frame nhận |
| `0` | Don't care |

```
Frame được nhận ⇔ ((MB_ID XOR Frame_ID) AND RXIMR[n]) == 0
```

### 11.4 Ví dụ

```c
/* 1. Nhận chính xác Standard ID 0x100 */
CAN0->RAMn[mb * 4U + 1U] = (0x100U << 18) & 0x1FFC0000U;
CAN0->RXIMR[mb] = 0x1FFC0000U;

/* 2. Nhận dải 0x100–0x107 (bỏ qua 3 bit thấp) */
CAN0->RAMn[mb * 4U + 1U] = (0x100U << 18) & 0x1FFC0000U;
CAN0->RXIMR[mb] = 0x1FE00000U;

/* 3. Nhận tất cả */
CAN0->RXIMR[mb] = 0x00000000U;

/* 4. Nhận chính xác Extended ID 0x1FFFFFFF (IDE = 1 trong CS của MB) */
CAN0->RAMn[mb * 4U + 1U] = 0x1FFFFFFFU;
CAN0->RXIMR[mb] = 0x1FFFFFFFU;

/* 5. MB5 chỉ nhận đúng Standard ID 0x555 */
CAN0->MCR |= CAN_MCR_IRMQ_MASK;
CAN0->RAMn[5U * 4U + 1U] = (0x555U << 18) & 0x1FFC0000U;
CAN0->RXIMR[5] = 0x1FFC0000U;
```

> [!TIP]
> Khi `IRMQ = 1` và `RFEN = 1`, `RXIMR[i]` áp dụng cho phần tử filter thứ i của Rx FIFO (với các phần tử nằm trong bảng filter), không phải cho MB.

---

## 12. IMASK1 – Interrupt Masks 1 Register

- **Địa chỉ**: offset `0x028` → `0x40024028`.
- **Độ rộng**: 32 bit. **Reset value**: `0x0000_0000`.

| Bit | 31 … 0 |
|-----|--------|
| **Field** | BUF31TO0M (bit n ↔ MB n) |
| **Access** | R/W |

| BUFnM | Ý nghĩa |
|-------|---------|
| `0` | Tắt ngắt cho MB n |
| `1` | Bật ngắt cho MB n: khi `IFLAG1[BUFnI] = 1`, ngắt được gửi tới NVIC |

```c
CAN0->IMASK1 = (1U << 0) | (1U << 8);     /* bật ngắt MB0 (RX) và MB8 (TX) */
CAN0->IMASK1 |=  (1U << mb_idx);           /* bật 1 MB */
CAN0->IMASK1 &= ~(1U << mb_idx);           /* tắt 1 MB */
CAN0->IMASK1 = 0U;                         /* tắt tất cả */
```

> [!NOTE]
> S32K144 CAN0 có 32 MB nên chỉ cần `IMASK1`/`IFLAG1`. Chip có nhiều MB hơn có thêm `IMASK2/IFLAG2` (MB32–63).

---

## 13. IFLAG1 – Interrupt Flags 1 Register

- **Địa chỉ**: offset `0x030` → `0x40024030`.
- **Độ rộng**: 32 bit. **Reset value**: `0x0000_0000`.
- **Cơ chế xóa**: **W1C** (Write 1 to Clear).

| BUFnI | Ý nghĩa |
|-------|---------|
| `0` | Chưa có sự kiện ở MB n |
| `1` | MB n đã gửi xong hoặc vừa nhận frame mới |

#### Khi `MCR[RFEN] = 1` (Rx FIFO):
- **Bit 5** (`BUF5I`): Frames Available in Rx FIFO.
- **Bit 6** (`BUF6I`): Rx FIFO Warning (gần đầy).
- **Bit 7** (`BUF7I`): Rx FIFO Overflow.

```c
CAN0->IFLAG1 = (1U << mb_idx);        /* xóa đúng cờ MB mb_idx: dùng "=" (ghi 1), không dùng "|=" */
```

> [!CAUTION]
> Ghi `0` không có tác dụng; để xóa phải ghi `1`. Tránh ghi `0xFFFFFFFF` trong ISR vì sẽ xóa cả các cờ chưa kịp xử lý (chỉ chấp nhận khi khởi tạo).

### ISR mẫu (MB0 = RX, MB8 = TX)

```c
void CAN0_ORed_0_15_MB_IRQHandler(void)
{
    uint32_t flag = CAN0->IFLAG1;

    if (flag & (1U << 0))                              /* MB0: nhận xong */
    {
        volatile uint32_t *mb = &CAN0->RAMn[0];
        uint32_t cs  = mb[0];                          /* lock MB */
        uint32_t id  = (mb[1] >> 18) & 0x7FFU;         /* Standard ID 11-bit */
        uint8_t  dlc = (uint8_t)((cs >> 16) & 0xFU);
        uint32_t d0  = mb[2];
        uint32_t d1  = mb[3];
        (void)id; (void)dlc; (void)d0; (void)d1;

        CAN0->IFLAG1 = (1U << 0);                      /* xóa cờ MB0 */
        (void)CAN0->TIMER;                             /* unlock MB0 */
    }

    if (flag & (1U << 8))                              /* MB8: gửi xong */
    {
        CAN0->IFLAG1 = (1U << 8);
    }
}
```

---

## 14. ESR1 – Error and Status 1 Register

### 14.1 Tổng quan

ESR1 phản ánh trạng thái bus CAN và các lỗi (Bit/ACK/CRC/Form/Stuff), cảnh báo, Bus-Off, cùng các cờ ngắt tương ứng.

- **Địa chỉ**: offset `0x020` → `0x40024020`.
- **Độ rộng**: 32 bit. **Reset value**: `0x0000_0000`.

### 14.2 Cấu trúc bit ESR1

| Bit | Tên | Access | Mô tả |
|-----|-----|--------|-------|
| 31 | **BIT1ERR_FAST** | R | Lỗi Bit1 trong data phase (CAN FD) |
| 30 | **BIT0ERR_FAST** | R | Lỗi Bit0 trong data phase (CAN FD) |
| 29 | — | R | Reserved |
| 28 | **CRCERR_FAST** | R | Lỗi CRC trong data phase (CAN FD) |
| 27 | **FRMERR_FAST** | R | Lỗi Form trong data phase (CAN FD) |
| 26 | **STFERR_FAST** | R | Lỗi Stuffing trong data phase (CAN FD) |
| 25:22 | — | R | Reserved |
| 21 | **ERROVR** | W1C | Error Overrun (lỗi mới trong khi cờ chưa xóa) |
| 20 | **ERRINT_FAST** | W1C | Cờ ngắt lỗi trong data phase (CAN FD) |
| 19 | **BOFFDONEINT** | W1C | Cờ ngắt Bus-Off Done |
| 18 | **SYNCH** | R | FlexCAN đã đồng bộ với bus |
| 17 | **TWRNINT** | W1C | Cờ ngắt Tx Warning |
| 16 | **RWRNINT** | W1C | Cờ ngắt Rx Warning |
| 15 | **BIT1ERR** | W1C | Lỗi Bit 1 |
| 14 | **BIT0ERR** | W1C | Lỗi Bit 0 |
| 13 | **ACKERR** | W1C | Lỗi ACK |
| 12 | **CRCERR** | W1C | Lỗi CRC |
| 11 | **FRMERR** | W1C | Lỗi Form |
| 10 | **STFERR** | W1C | Lỗi Stuffing |
| 9 | **TXWRN** | R | Cảnh báo Tx (TEC ≥ 96) |
| 8 | **RXWRN** | R | Cảnh báo Rx (REC ≥ 96) |
| 7 | **IDLE** | R | Bus đang Idle |
| 6 | **TX** | R | FlexCAN đang phát |
| 5:4 | **FLTCONF[1:0]** | R | Fault Confinement State |
| 3 | **RX** | R | FlexCAN đang nhận |
| 2 | **BOFFINT** | W1C | Cờ ngắt Bus-Off |
| 1 | **ERRINT** | W1C | Cờ ngắt lỗi tổng hợp |
| 0 | **WAKINT** | W1C | Cờ ngắt Wake-up |

### 14.3 Giải thích chi tiết

#### Bits 5:4 – FLTCONF[1:0] (Fault Confinement State)

| FLTCONF | Trạng thái | Điều kiện |
|---------|-----------|-----------|
| `00` | **Error Active** – bình thường | TEC < 128 và REC < 128 |
| `01` | **Error Passive** | TEC ≥ 128 hoặc REC ≥ 128 |
| `1x` | **Bus Off** | TEC > 255 |

#### Nhóm lỗi chi tiết (bit 15:10)

| Bit | Cờ | Ý nghĩa khi = 1 |
|-----|----|-----------------|
| 15 | BIT1ERR | Phát bit recessive (1) nhưng đọc lại bus là dominant (0) |
| 14 | BIT0ERR | Phát bit dominant (0) nhưng đọc lại bus là recessive (1) |
| 13 | ACKERR | Không có node nào xác nhận (ACK) frame đã phát |
| 12 | CRCERR | CRC tính được không khớp CRC nhận |
| 11 | FRMERR | Sai cấu trúc frame (CRC delimiter, ACK delimiter, EOF) |
| 10 | STFERR | Vi phạm bit stuffing (hơn 5 bit liên tiếp cùng mức) |

#### Nhóm trạng thái và cảnh báo

| Bit | Tên | Ý nghĩa khi = 1 |
|-----|-----|-----------------|
| 9 | TXWRN | TEC ≥ 96 |
| 8 | RXWRN | REC ≥ 96 |
| 7 | IDLE | Bus rảnh |
| 6 | TX | Đang phát |
| 3 | RX | Đang nhận |
| 18 | SYNCH | FlexCAN đã đồng bộ với bus CAN |

#### Nhóm cờ ngắt (W1C)

| Bit | Cờ | Ý nghĩa |
|-----|----|---------|
| 17 | TWRNINT | TEC vượt ngưỡng 96 (chỉ set khi `MCR[WRNEN] = 1`) |
| 16 | RWRNINT | REC vượt ngưỡng 96 (chỉ set khi `MCR[WRNEN] = 1`) |
| 2 | BOFFINT | Đã vào Bus-Off |
| 1 | ERRINT | Phát hiện lỗi bus (một trong các cờ lỗi 15:10) |
| 0 | WAKINT | Đánh thức từ Low-Power bởi hoạt động bus |
| 19 | BOFFDONEINT | Đã phục hồi xong khỏi Bus-Off |

### 14.4 Ví dụ xử lý lỗi

```c
/* Bus-Off, Tx/Rx Warning thường nằm ở vector CAN0_ORed_IRQHandler */
void CAN0_ORed_IRQHandler(void)
{
    uint32_t esr = CAN0->ESR1;

    if (esr & CAN_ESR1_BOFFINT_MASK)
    {
        CAN0->ESR1 = CAN_ESR1_BOFFINT_MASK;      /* xóa cờ (W1C) */
        /* Xử lý Bus-Off: phục hồi / reset FlexCAN */
    }
    if (esr & CAN_ESR1_TWRNINT_MASK) { CAN0->ESR1 = CAN_ESR1_TWRNINT_MASK; }
    if (esr & CAN_ESR1_RWRNINT_MASK) { CAN0->ESR1 = CAN_ESR1_RWRNINT_MASK; }
}

/* Lỗi bus CAN thường nằm ở vector CAN0_Error_IRQHandler */
void CAN0_Error_IRQHandler(void)
{
    uint32_t esr = CAN0->ESR1;

    if (esr & CAN_ESR1_ACKERR_MASK)
    {
        /* Không có node ACK → kiểm tra transceiver / node còn lại / terminator */
    }
    if (esr & CAN_ESR1_STFERR_MASK)
    {
        /* Stuff error → nhiễu bus hoặc baud rate sai */
    }

    uint8_t flt = (uint8_t)((esr >> 4) & 0x3U);   /* FLTCONF */
    if (flt >= 2U)
    {
        /* Bus-Off state */
    }

    CAN0->ESR1 = esr & (CAN_ESR1_ERRINT_MASK | CAN_ESR1_BIT1ERR_MASK | CAN_ESR1_BIT0ERR_MASK |
                        CAN_ESR1_ACKERR_MASK | CAN_ESR1_CRCERR_MASK | CAN_ESR1_FRMERR_MASK |
                        CAN_ESR1_STFERR_MASK);    /* xóa các cờ đã xử lý (W1C) */
}
```

> [!NOTE]
> Tên vector và nhóm ngắt (ORed / Error / Wake-up) cần đối chiếu với bảng interrupt vector của S32K144 và file startup trong project.

---

## 15. ECR – Error Counter Register

### 15.1 Tổng quan

ECR chứa các bộ đếm lỗi theo ISO 11898-1.

- **Địa chỉ**: offset `0x01C` → `0x4002401C`.
- **Độ rộng**: 32 bit. **Reset value**: `0x0000_0000`. Access: R.

### 15.2 Cấu trúc bit

| Bit | 31:24 | 23:16 | 15:8 | 7:0 |
|-----|-------|-------|------|-----|
| **Field** | RXERRCNT_FAST | TXERRCNT_FAST | RXERRCNT | TXERRCNT |
| **Access** | R | R | R | R |

### 15.3 Giải thích

- **TXERRCNT[7:0]** (bit 7:0): bộ đếm lỗi truyền (TEC).
- **RXERRCNT[15:8]** (bit 15:8): bộ đếm lỗi nhận (REC).
- **TXERRCNT_FAST[23:16]**, **RXERRCNT_FAST[31:24]**: bộ đếm lỗi trong data phase của CAN FD.

| Sự kiện | Thay đổi TEC |
|---------|-------------|
| Phát lỗi | +8 |
| Phát thành công | −1 |
| TEC ≥ 128 | Error Passive |
| TEC > 255 | Bus-Off |

| Sự kiện | Thay đổi REC |
|---------|-------------|
| Nhận lỗi | +1 |
| Nhận thành công | −1 |
| REC ≥ 128 | Error Passive |
| (REC không bao giờ gây Bus-Off) | — |

**Thang trạng thái lỗi:**

```
TEC/REC   0 – 95  : Error Active  (phát Active Error Frame)
TEC/REC  96 – 127 : Error Warning (TXWRN/RXWRN = 1)
TEC/REC 128 – 255 : Error Passive (phát Passive Error Frame)
TEC       > 255   : Bus-Off       (tách khỏi bus)
```

```c
void CAN0_PrintErrorCounters(void)
{
    uint32_t ecr = CAN0->ECR;
    uint8_t tec = (uint8_t)( ecr        & 0xFFU);   /* bit 7:0  */
    uint8_t rec = (uint8_t)((ecr >> 8U) & 0xFFU);   /* bit 15:8 */

    uint8_t flt = (uint8_t)((CAN0->ESR1 >> 4U) & 0x3U);   /* FLTCONF */
    const char *state;
    if (flt >= 2U)                          { state = "BUS-OFF"; }
    else if (tec >= 128U || rec >= 128U)    { state = "ERROR PASSIVE"; }
    else if (tec >= 96U  || rec >= 96U)     { state = "ERROR WARNING"; }
    else                                    { state = "ERROR ACTIVE (normal)"; }
    (void)state;
}
```

---

## 16. PORTx_PCRn – Pin Control Register (cấu hình chân CAN)

### 16.1 Tổng quan

`PORTx_PCRn` cấu hình chức năng (mux), pull, drive strength… của từng chân.

| Chân | Chức năng | PORT | Địa chỉ PCR |
|------|-----------|------|-------------|
| PTE4 | CAN0_RX (ALT5) | PORTE | `0x40049010` |
| PTE5 | CAN0_TX (ALT5) | PORTE | `0x40049014` |

- **PORTE Base Address**: `0x40049000`. Địa chỉ `PCR[n] = PORTE_Base + n × 4`.
- Clock cho PORTE: `PCC->PCCn[PCC_PORTE_INDEX]` (offset `0x134`, chỉ số 77) phải được bật.
- Cần tra bảng pin multiplexing của S32K144 trong Reference Manual để chọn chân/ALT khác cho CAN0.

### 16.2 Cấu trúc bit PCRn

| Bit | Tên | Access | Mô tả |
|-----|-----|--------|-------|
| 31:25 | — | R | Reserved |
| 24 | **ISF** | W1C | Interrupt Status Flag |
| 23:20 | — | R | Reserved |
| 19:16 | **IRQC[3:0]** | R/W | Interrupt Configuration |
| 15 | **LK** | R/W | Lock Register |
| 14:11 | — | R | Reserved |
| 10:8 | **MUX[2:0]** | R/W | Pin Multiplexing Control |
| 7 | — | R | Reserved |
| 6 | **DSE** | R/W | Drive Strength Enable |
| 5 | — | R | Reserved |
| 4 | **PFE** | R/W | Passive Filter Enable |
| 3:2 | — | R | Reserved |
| 1 | **PE** | R/W | Pull Enable |
| 0 | **PS** | R/W | Pull Select |

> [!NOTE]
> PCR của S32K1xx không có các bit SRE (Slew Rate) và ODE (Open Drain) như dòng Kinetis. `DSE` và `PFE` chỉ có hiệu lực trên các chân có hỗ trợ tính năng đó (tra Data Sheet / RM).

### 16.3 Giải thích từng bit

#### Bits 10:8 – MUX[2:0]

| MUX | Chức năng |
|-----|-----------|
| `000` | Pin Disabled (Analog) |
| `001` | ALT1 – GPIO |
| `010` | ALT2 |
| `011` | ALT3 |
| `100` | ALT4 |
| `101` | **ALT5 – CAN0_RX / CAN0_TX trên PTE4 / PTE5** |
| `110` | ALT6 |
| `111` | ALT7 |

Trích bảng pin mux (S32K144):

| Pin | ALT0 | ALT1 | ALT5 |
|-----|------|------|------|
| PTE4 | Disabled | GPIO | **CAN0_RX** |
| PTE5 | Disabled | GPIO | **CAN0_TX** |

#### Bit 6 – DSE (Drive Strength Enable)

| Giá trị | Ý nghĩa |
|---------|---------|
| `0` | Drive strength thấp |
| `1` | Drive strength cao (tùy chọn cho chân TX, nếu chân hỗ trợ) |

#### Bit 4 – PFE (Passive Filter Enable)

| Giá trị | Ý nghĩa |
|---------|---------|
| `0` | Tắt bộ lọc passive |
| `1` | Bật bộ lọc passive trên ngõ vào (hữu ích cho chân CAN_RX để giảm nhiễu), nếu chân hỗ trợ |

#### Bit 1 – PE (Pull Enable) / Bit 0 – PS (Pull Select)

| PE | PS | Ý nghĩa |
|----|----|---------|
| `0` | x | Tắt điện trở kéo nội |
| `1` | `0` | Bật Pull-Down |
| `1` | `1` | Bật Pull-Up |

#### Bit 15 – LK (Lock Register)
`0` cho phép ghi tiếp PCR, `1` khóa các bit PCR đến khi reset.

#### Bits 19:16 – IRQC[3:0] (Interrupt Configuration)
Chỉ dùng khi chân ở chế độ GPIO ngắt. Với chân CAN để `0000` (tắt).

| IRQC | Chế độ |
|------|--------|
| `0000` | Tắt ngắt/DMA |
| `0001` | DMA request cạnh lên |
| `0010` | DMA request cạnh xuống |
| `0011` | DMA request cả hai cạnh |
| `1000` | Ngắt khi mức 0 |
| `1001` | Ngắt cạnh lên |
| `1010` | Ngắt cạnh xuống |
| `1011` | Ngắt cả hai cạnh |
| `1100` | Ngắt khi mức 1 |

#### Bit 24 – ISF (Interrupt Status Flag)
`1` = đã có sự kiện ngắt GPIO, ghi `1` để xóa (W1C). Không liên quan chức năng CAN.

### 16.4 Ví dụ

```c
void PTM_CAN0_ConfigPins(void)
{
    PCC->PCCn[PCC_PORTE_INDEX] |= PCC_PCCn_CGC_MASK;      /* clock cho PORTE */

    PORTE->PCR[4] = PORT_PCR_MUX(5) | PORT_PCR_PFE(1);    /* PTE4 = CAN0_RX */
    PORTE->PCR[5] = PORT_PCR_MUX(5) | PORT_PCR_DSE(1);    /* PTE5 = CAN0_TX */
}
```

---

## 17. Quy trình khởi tạo FlexCAN0 đầy đủ (500 kbps)

Ví dụ dưới dùng `CLKSRC = 0` (SOSCDIV2_CLK = 8 MHz), PRESDIV = 0, 16 Tq/bit.

```c
void PTM_CAN0_Init_500kbps(void)
{
    /* ===== BƯỚC 1: Pin ===== */
    PCC->PCCn[PCC_PORTE_INDEX] |= PCC_PCCn_CGC_MASK;
    PORTE->PCR[4] = PORT_PCR_MUX(5) | PORT_PCR_PFE(1);    /* CAN0_RX */
    PORTE->PCR[5] = PORT_PCR_MUX(5) | PORT_PCR_DSE(1);    /* CAN0_TX */

    /* ===== BƯỚC 2: Clock cho FlexCAN0 và SOSCDIV2 ===== */
    PCC->PCCn[PCC_FlexCAN0_INDEX] |= PCC_PCCn_CGC_MASK;
    SCG->SOSCDIV = (SCG->SOSCDIV & ~SCG_SOSCDIV_SOSCDIV2_MASK) | SCG_SOSCDIV_SOSCDIV2(1);

    /* ===== BƯỚC 3: Chọn CLKSRC khi đang Disable (MDIS = 1) ===== */
    CAN0->MCR |= CAN_MCR_MDIS_MASK;
    while (!(CAN0->MCR & CAN_MCR_LPMACK_MASK)) {}
    CAN0->CTRL1 &= ~CAN_CTRL1_CLKSRC_MASK;                /* 0 = SOSCDIV2_CLK */

    /* ===== BƯỚC 4: Enable module (MDIS = 0) ===== */
    CAN0->MCR &= ~CAN_MCR_MDIS_MASK;
    while (CAN0->MCR & CAN_MCR_LPMACK_MASK) {}

    /* ===== BƯỚC 5: Soft Reset ===== */
    CAN0->MCR |= CAN_MCR_SOFTRST_MASK;
    while (CAN0->MCR & CAN_MCR_SOFTRST_MASK) {}

    /* ===== BƯỚC 6: Vào Freeze Mode ===== */
    CAN0->MCR |= CAN_MCR_FRZ_MASK | CAN_MCR_HALT_MASK;
    while (!(CAN0->MCR & CAN_MCR_FRZACK_MASK)) {}

    /* ===== BƯỚC 7: Cấu hình MCR ===== */
    CAN0->MCR = (CAN0->MCR & ~(CAN_MCR_MAXMB_MASK |
                                CAN_MCR_SRXDIS_MASK |
                                CAN_MCR_IRMQ_MASK   |
                                CAN_MCR_AEN_MASK))
              | CAN_MCR_MAXMB(15)      /* 16 MB (MB0–MB15) */
              | CAN_MCR_SRXDIS(1)      /* tắt self-reception */
              | CAN_MCR_IRMQ(1)        /* mask riêng RXIMR */
              | CAN_MCR_AEN(1);        /* cho phép abort TX */

    /* ===== BƯỚC 8: Bit timing 500 kbps @ 8 MHz =====
       PRESDIV=0 → Tq = 125 ns
       1 + (6+1) + (3+1) + (3+1) = 16 Tq → 2 µs → 500 kbps, sample point 75 % */
    CAN0->CTRL1 = CAN_CTRL1_CLKSRC(0) |
                  CAN_CTRL1_PRESDIV(0) |
                  CAN_CTRL1_PROPSEG(6) |
                  CAN_CTRL1_PSEG1(3)   |
                  CAN_CTRL1_PSEG2(3)   |
                  CAN_CTRL1_RJW(1);        /* ngắt Bus-Off/Error đang tắt (polling) */

    /* ===== BƯỚC 9: Xóa RAM Message Buffer (128 word = 512 byte) ===== */
    for (uint32_t i = 0U; i < 128U; i++) {
        CAN0->RAMn[i] = 0U;
    }

    /* ===== BƯỚC 10: Mask ===== */
    CAN0->RXMGMASK = 0x00000000U;
    CAN0->RX14MASK = 0x00000000U;
    CAN0->RX15MASK = 0x00000000U;

    /* ===== BƯỚC 11: Tắt ngắt MB, xóa cờ (W1C) ===== */
    CAN0->IMASK1 = 0x00000000U;
    CAN0->IFLAG1 = 0xFFFFFFFFU;      /* khi khởi tạo: chủ động xóa mọi cờ */
    CAN0->ESR1   = 0xFFFFFFFFU;

    /* ===== BƯỚC 12: Thoát Freeze Mode ===== */
    CAN0->MCR &= ~(CAN_MCR_FRZ_MASK | CAN_MCR_HALT_MASK);
    while (CAN0->MCR & CAN_MCR_FRZACK_MASK) {}
    while (CAN0->MCR & CAN_MCR_NOTRDY_MASK) {}
}
```

> [!NOTE]
> Nếu dùng `CLKSRC = 1` (SYS_CLK = 80 MHz) thì bỏ bước bật `SOSCDIV2`, đặt `CLKSRC(1)` ở Bước 3 và `PRESDIV(9)` ở Bước 8 để giữ Tq = 125 ns.

---

## 18. Bảng tóm tắt địa chỉ thanh ghi FlexCAN0

| Thanh ghi | Offset | Địa chỉ tuyệt đối | Reset | Ghi chú |
|-----------|--------|-------------------|-------|---------|
| MCR | 0x000 | 0x40024000 | 0xD890000F | Module Configuration |
| CTRL1 | 0x004 | 0x40024004 | 0x00000000 | Bit timing, clock source |
| TIMER | 0x008 | 0x40024008 | 0x00000000 | Free-Running Timer (đọc để unlock MB) |
| RXMGMASK | 0x010 | 0x40024010 | 0xFFFFFFFF | Global Rx Mask |
| RX14MASK | 0x014 | 0x40024014 | 0xFFFFFFFF | MB14 Mask |
| RX15MASK | 0x018 | 0x40024018 | 0xFFFFFFFF | MB15 Mask |
| ECR | 0x01C | 0x4002401C | 0x00000000 | Error Counters |
| ESR1 | 0x020 | 0x40024020 | 0x00000000 | Error and Status 1 |
| IMASK1 | 0x028 | 0x40024028 | 0x00000000 | Interrupt Mask MB0–31 |
| IFLAG1 | 0x030 | 0x40024030 | 0x00000000 | Interrupt Flags MB0–31 |
| CTRL2 | 0x034 | 0x40024034 | 0x00B00000 | Advanced Control |
| RXFGMASK | 0x048 | 0x40024048 | 0xFFFFFFFF | Rx FIFO Global Mask |
| RAMn[0] | 0x080 | 0x40024080 | — | MB0 CS Word |
| RAMn[127] | 0x27C | 0x4002427C | — | Word cuối vùng MB (CAN0 S32K144, 32 MB) |
| RXIMR[0] | 0x880 | 0x40024880 | 0xFFFFFFFF | Individual Mask MB0 |
| RXIMR[31] | 0x8FC | 0x400248FC | 0xFFFFFFFF | Individual Mask MB31 |

---

## 19. Checklist debug CAN không hoạt động

```
□ 1. PCC[FlexCAN0] có CGC (bit 30) = 1 không?
□ 2. PCC[PORTE] có được bật không?
□ 3. PTE4/PTE5 có set MUX = 5 (ALT5) không?
□ 4. CTRL1.CLKSRC (bit 13) đúng không? (0 = SOSCDIV2_CLK, 1 = SYS_CLK)
     Nếu CLKSRC = 0: SCG_SOSCDIV[SOSCDIV2] đã khác 0 chưa?
□ 5. Bit timing có phù hợp baud rate không?
     Bit Time = 1 + (PROPSEG+1) + (PSEG1+1) + (PSEG2+1) [Tq]
□ 6. FRZACK = 1 (đang ở Freeze Mode) khi ghi CTRL1, MAXMB, mask?
□ 7. Sau khi thoát Freeze: FRZACK = 0 và NOTRDY = 0?
□ 8. ESR1.ACKERR = 1? → không có node ACK / transceiver chưa cấp nguồn / chưa có terminator
□ 9. ESR1.STFERR = 1? → baud rate sai hoặc nhiễu bus
□ 10. ECR.TXERRCNT (bit 7:0) tăng liên tục? → lỗi vật lý: ngắn mạch, thiếu 120 Ω, sai baud rate
□ 11. RXIMR[mb] đã set đúng chưa (0x1FFC0000 cho khớp chính xác Standard ID)? MCR.IRMQ = 1?
□ 12. MB Rx có CODE = RX_EMPTY (0x4)? Sau khi đọc đã unlock bằng cách đọc TIMER chưa?
□ 13. IFLAG1 có được xóa bằng cách ghi 1 (W1C) chưa?
□ 14. CAN transceiver có được cấp nguồn đúng (3.3 V hoặc 5 V tùy loại) và GND chung chưa?
□ 15. CAN_H / CAN_L có bị hoán đổi không? (dominant: CAN_H > CAN_L)
□ 16. Nếu dùng Rx FIFO: RFFN và IDAM đã khớp thiết kế? MB độc lập có nằm sau vùng FIFO + bảng filter không?
```

---

## 20. Phụ lục A – Danh sách các lỗi đã sửa

Cột "Nguồn lỗi": **Ref** = `FlexCAN_S32K144_Register_Reference.md` (bản cũ), **Guide** = `flexcan-registers-guide.md`.

### A.1 PCC / SCG

| # | Nguồn lỗi | Nội dung sai | Đã sửa thành |
|---|-----------|--------------|--------------|
| 1 | Ref | `CGC` ở bit 31 (ví dụ code lại dùng `1U << 30`, tự mâu thuẫn) | `PR` = bit 31, **`CGC` = bit 30** |
| 2 | Guide | `PCC_FlexCAN2` offset `0x0AC` | **`0x098`** (chỉ số 38) |
| 3 | Ref | `SCG_SPLLDIV` tại `0x40064504` | **`0x40064604`** (offset `0x604`) |
| 4 | Ref | Nguồn clock FlexCAN là "bus clock SPLLDIV2, 40 MHz" | `CLKSRC = 1` là **SYS_CLK**; FlexCAN không dùng SPLLDIV2 |
| 5 | Guide | Bảng `SCG_SOSCDIV` thiếu `SOSCDIV1` | Bổ sung **SOSCDIV1 = bit 2:0** |
| 6 | Ref | Sơ đồ ASCII ghi SOSCDIV2 ở bit 15:8 | **bit 10:8** |

### A.2 MCR

| # | Nguồn lỗi | Nội dung sai | Đã sửa thành |
|---|-----------|--------------|--------------|
| 7 | Guide | Reset value `0x5980_000F` | **`0xD890_000F`** |
| 8 | Guide + Ref | Bit 26, 22, 19, 18 ghi là Reserved | **WAKMSK (26), SLFWAK (22), WAKSRC (19), DOZE (18)** |
| 9 | Ref | IDAM Format D = "tắt lọc, chấp nhận tất cả" | **Từ chối tất cả frame** |
| 10 | Guide | IDAM Format B = "2 ID 14-bit Standard" | **2 ID Standard đầy đủ hoặc 2 ID Extended 14-bit cao** |
| 11 | Guide | RFEN: "MB0–MB7 là FIFO" chung chung | FIFO = MB0–MB5 + bảng filter (MB0–MB7 khi RFFN = 0) |

### A.3 CTRL1

| # | Nguồn lỗi | Nội dung sai | Đã sửa thành |
|---|-----------|--------------|--------------|
| 12 | Guide | `PROPSEG` ở bit 15:13 | **bit 2:0** |
| 13 | Guide | `CLKSRC` bit 10 | **bit 13** |
| 14 | Guide | `LPB` bit 9 | **bit 12** |
| 15 | Guide | `ERRMSK` xuất hiện 2 lần (bit 11 và bit 3) | **Chỉ một lần, bit 14** |
| 16 | Guide | `BOFFMSK` bit 4 | **bit 15** |
| 17 | Guide | `LOM` bit 2 | **bit 3** |
| 18 | Guide | `TWRNMSK` bit 8, `RWRNMSK` bit 7, `SMP` bit 6 | **TWRNMSK = 11, RWRNMSK = 10, SMP = 7** |
| 19 | Guide | Có bit `LAK` (12); thiếu `BOFFREC`, `TSYN`, `LBUF` | Xóa `LAK`; bổ sung **BOFFREC (6), TSYN (5), LBUF (4)**; bit 9:8 là Reserved |
| 20 | Ref | `PSEG1` ở 18:16, `PSEG2` ở 21:19 (hoán đổi) | **PSEG1 = 21:19, PSEG2 = 18:16** |
| 21 | Ref | `PROPSEG` bit 3:0 | **bit 2:0** (3 bit) |
| 22 | Ref | `CLKSRC` bit 8 | **bit 13** (bit 9:8 là Reserved) |
| 23 | Ref | `BOFFINT` bit 6 = "Bus Off Interrupt Enable" | Bit 6 là **BOFFREC** (Bus Off Recovery); mask ngắt Bus-Off là `BOFFMSK` bit 15 |
| 24 | Ref | `LBUF` bit 11 | **bit 4** |
| 25 | Ref | `WERN` bit 10, `RWRNMSK` bit 13, `TWRNMSK` bit 12 | **RWRNMSK = 10, TWRNMSK = 11**; không có `WERN` |
| 26 | Ref | `LOM` bit 4; thiếu `ERRMSK`, `LPB` | **LOM = bit 3**; bổ sung ERRMSK (14), LPB (12) |
| 27 | Ref | Công thức `Bit Time = 1 × (1 + PROPSEG + PSEG1 + PSEG2)` | `1 + (PROPSEG+1) + (PSEG1+1) + (PSEG2+1)` |
| 28 | Ref | Ví dụ bit timing tính sai, mâu thuẫn giữa mục 4 và mục 17 | Ví dụ mới: 8 MHz, PRESDIV = 0, 6/3/3, 16 Tq, sample point 75 % |
| 29 | Ref | Nói `CLKSRC` đặt "khi MDIS = 1 hoặc FRZACK = 1" | `CLKSRC` chỉ ghi được khi **Disable Mode** |

### A.4 CTRL2

| # | Nguồn lỗi | Nội dung sai | Đã sửa thành |
|---|-----------|--------------|--------------|
| 30 | Guide | `RFFN` ở bit 17:14 | **bit 27:24** |
| 31 | Guide | `TASD` ở bit 13:11 (3 bit) | **bit 23:19** (5 bit) |
| 32 | Guide | `MRP` ở bit 10:8 (3 bit) | **bit 18** (1 bit) |
| 33 | Guide | `EACEN` bit 7 | **bit 16** |
| 34 | Guide | Reset value `0x0010_0000` | **`0x00B0_0000`** (TASD = 22) |
| 35 | Ref | `WRMFRZ` bit 31 | **bit 28** |
| 36 | Ref | `TASD` bit 27:23, `RFFN` bit 22:19 | **TASD = 23:19, RFFN = 27:24** |
| 37 | Ref | `ERSVD` (bit 9), `BOFFRECQ` (bit 8) | Không tồn tại ở CTRL2; Bus-Off recovery là **`CTRL1[BOFFREC]` bit 6** (0 = tự động, đảo nghĩa so với bản cũ) |
| 38 | Ref | Công thức MB bị chiếm "5 + RFFN × 2" | **7 + 2 × RFFN** (MB cuối bị chiếm) |
| 39 | Cả hai | Thiếu RRS, TIMER_SRC, PREXCEN, ISOCANFDEN, EDFLTDIS, BOFFDONEMSK, ERRMSK_FAST, ECRWRE | Bổ sung đầy đủ |

### A.5 TIMER / RAMn / Mask

| # | Nguồn lỗi | Nội dung sai | Đã sửa thành |
|---|-----------|--------------|--------------|
| 40 | Ref | `TIMER` là Read Only | **R/W** |
| 41 | Guide | "Ghi bất kỳ giá trị nào sẽ reset TIMER về 0" | Ghi sẽ **nạp giá trị được ghi** |
| 42 | Guide | Thiếu cơ chế **đọc TIMER để unlock MB** | Bổ sung (mục 6.4) |
| 43 | Guide | `CODE` ở bit 31:24; vùng RAM `0x080–0x47F` (64 MB) | **CODE = bit 27:24**; S32K144 CAN0 là `0x080–0x27F` (32 MB) |
| 44 | Ref | `TX REMOTE` = CODE `0x1C` | **CODE `0xC` với `RTR = 1`** |
| 45 | Guide | Bit RTR/IDE của mask nằm ở bit 30/29 | **RTR = bit 31, IDE = bit 30** |
| 46 | Ref | Ví dụ mask 8 bit cao là `0x1FF80000` (thực tế là 10 bit) | **`0x1FE00000`** |
| 47 | Guide | `0x1FFF_FFFF` (không hợp lệ trong C) và mask cho MB5 chưa đúng | `0x1FFC0000U` cho Standard ID khớp chính xác |
| 48 | Ref | RX14MASK/RX15MASK "dùng cho FIFO filter khi RFEN = 1" | Khi RFEN = 1: FIFO dùng **RXFGMASK** (IRMQ = 0) hoặc **RXIMR** (IRMQ = 1) |
| 49 | Ref | Mục 13: cảnh báo không ghi `0xFFFFFFFF` nhưng Init lại ghi | Làm rõ: chỉ chấp nhận khi khởi tạo |

### A.6 ESR1 / ECR

| # | Nguồn lỗi | Nội dung sai | Đã sửa thành |
|---|-----------|--------------|--------------|
| 50 | Guide | Cờ lỗi lệch 1 bit: BIT1ERR 14, BIT0ERR 13, ACKERR 12, CRCERR 11, FRMERR 10, STFERR 9, TXWRN 8, RXWRN 7, IDLE 6 | **BIT1ERR 15, BIT0ERR 14, ACKERR 13, CRCERR 12, FRMERR 11, STFERR 10, TXWRN 9, RXWRN 8, IDLE 7** |
| 51 | Guide | `TX` bit 5, `RX` bit 4, `FLTCONF` bit 3:2 | **TX = 6, RX = 3, FLTCONF = 5:4** |
| 52 | Guide | `BOFFINT` vừa là bit 2 vừa nằm lẫn với FLTCONF; SYNCH/TWRNINT/RWRNINT sai chỗ | **BOFFINT = 2, ERRINT = 1, WAKINT = 0, SYNCH = 18, TWRNINT = 17, RWRNINT = 16** |
| 53 | Ref | `FLTCONF` bit 20 (bảng), `BOFFDONE` bit 4, `BOFFINT` bit 3, `SYNCH` bit 2, `TWRNINT` bit 1, `RWRNINT` bit 0 | Sửa theo bảng ở mục 14.2 (FLTCONF = 5:4, BOFFINT = 2, SYNCH = 18, TWRNINT = 17, RWRNINT = 16) |
| 54 | Cả hai | Thiếu `BOFFDONEINT`, `ERRINT_FAST`, `ERROVR` và các cờ `*_FAST` | Bổ sung |
| 55 | Ref | Code Bus-Off dùng `ESR1 & (0x3 << 4)` coi mọi FLTCONF ≠ 0 là Bus-Off | Bus-Off ⇔ `FLTCONF ≥ 2` (`1x`) |
| 56 | Ref | Xử lý Bus-Off trong `CAN0_Error_IRQHandler` | Bus-Off/Warning ở `CAN0_ORed_IRQHandler`, lỗi bus ở `CAN0_Error_IRQHandler` (cần đối chiếu file startup) |
| 57 | Ref | `RXERRCNT` ở bit 23:16, `TXERRCNT_FAST` ở 15:8; code `ecr >> 16` | **TXERRCNT = 7:0, RXERRCNT = 15:8, TXERRCNT_FAST = 23:16, RXERRCNT_FAST = 31:24**; code `ecr >> 8` |
| 58 | Guide | "TXERRCNT > 255" (bộ đếm 8-bit không đọc được giá trị này) | Bus-Off khi TEC vượt 255 (xem `ESR1[FLTCONF]`) |

### A.7 PORTx_PCRn

| # | Nguồn lỗi | Nội dung sai | Đã sửa thành |
|---|-----------|--------------|--------------|
| 59 | Ref | `MUX` ở 15:8, `DSE` bit 10, `ODE` bit 9, `PFE` bit 6, `SRE` bit 4, `LK` bit 16 | **MUX = 10:8, DSE = 6, PFE = 4, LK = 15**; S32K1xx không có `ODE`, `SRE` |
| 60 | Ref | Bảng MUX ghi ALT4 cho CAN0 nhưng ví dụ dùng ALT5 | **ALT5** cho PTE4/PTE5 |
| 61 | Guide | `PTC6`/`PTC7` là CAN0 ALT2 | Đã loại bỏ (trên S32K144 đây không phải chân CAN0; cần tra bảng pin mux) |
| 62 | Ref | PFE "lọc xung nhiễu < 10 ns" | Bỏ con số cụ thể; mô tả chung |

### A.8 Khác

| # | Nguồn lỗi | Nội dung sai | Đã sửa thành |
|---|-----------|--------------|--------------|
| 63 | Ref | Checklist: "FRZACK = 0 trước khi cấu hình CTRL1" | Phải **FRZACK = 1** (đang ở Freeze) |
| 64 | Ref | Checklist chỉ định transceiver SN65HVD230 | Nêu chung theo loại transceiver, cấp nguồn đúng |
| 65 | Ref | Quy trình đọc MB: đọc TIMER trước khi xóa cờ | Thứ tự: đọc CS → đọc dữ liệu → xóa cờ → đọc TIMER |
| 66 | Ref | `swap32(*(uint32_t*)&data[0])` (aliasing/unaligned) | Dùng `PTM_PackBE32()` bằng phép dịch bit |

---

*Tài liệu được hợp nhất và hiệu chỉnh từ `flexcan-registers-guide.md` và `FlexCAN_S32K144_Register_Reference.md`, đối chiếu theo cấu trúc thanh ghi S32K1xx (FlexCAN / PCC / SCG / PORT).*

*Phiên bản: 1.1 (hiệu chỉnh) | Board: NXP S32K144EVB | Mục tiêu: PTM_CAN_Driver bare-metal*
