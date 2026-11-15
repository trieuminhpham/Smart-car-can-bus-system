# TÀI LIỆU THIẾT KẾ HỆ THỐNG XE ĐIỀU KHIỂN THÔNG MINH QUA MẠNG CAN
**Hệ thống Nhúng Ô tô Đa Node (Multi-Node Automotive Embedded System)**

---

## 1. TỔNG QUAN KIẾN TRÚC HỆ THỐNG

Hệ thống được thiết kế theo mô hình kiến trúc phân tán **E/E Architecture (Electrical/Electronic Architecture)** chuẩn công nghiệp ô tô, gồm **4 Node điều khiển độc lập** kết nối qua đường trục truyền thông **CAN Bus (tốc độ 500 kbps)**.

```
+---------------------------------------------------------------------------------------------------------+
|                                           CAN BUS (500 kbps)                                            |
|   CAN_H ===================================================================================== CAN_H     |
|   CAN_L ===================================================================================== CAN_L     |
|   (Trở đầu cuối 120 Ohm)                                                 (Trở đầu cuối 120 Ohm)         |
+---------+-------------------------+-------------------------+-------------------------+-----------------+
          |                         |                         |                         |
   +--------------+          +--------------+          +--------------+          +--------------+
   |   Node 1:    |          |   Node 2:    |          |   Node 3:    |          |   Node 4:    |
   |  Telematics  |          |  Powertrain  |          | ADAS/Safety  |          | Cluster/BCM  |
   |   Gateway    |          |   Main ECU   |          |  Active Stop |          | Body Control |
   +--------------+          +--------------+          +--------------+          +--------------+
   |    ESP32     |          |   S32K144    |          | STM32F103C8T |          | STM32F103C8T |
   +--------------+          +--------------+          +--------------+          +--------------+
   | - Web Server |          | - PTM CAN Reg|          | - Cảm biến   |          | - LCD Display|
   | - Wi-Fi AP   |          |   Driver     |          |   ToF Laser  |          | - MPU-6050   |
   | - Chọn 3 Mode|          | - L298N Drive|          | - Thuật toán |          | - Đèn LED    |
   | - Gửi telemetry         | - Đọc Encoder|          |   AEB ngắt ga|          | - Đo áp Pin  |
   |   lên Web    |          | - Cruise PID |          |   khẩn cấp   |          | - Heartbeat  |
   |              |          | - Chạy Map ảo|          |              |          |              |
   +--------------+          +--------------+          +--------------+          +--------------+
          |                         |                         |                         |
   +--------------+          +--------------+          +--------------+          +--------------+
   |  Transceiver |          |  Transceiver |          |  Transceiver |          |  Transceiver |
   | SN65HVD230   |          | SN65HVD230   |          | SN65HVD230   |          | SN65HVD230   |
   +--------------+          +--------------+          +--------------+          +--------------+
```

### 1.1. Cơ cấu truyền động (Drivetrain)
* **Cơ chế lái**: **Lái vi sai (Differential Drive)**, không sử dụng servo bẻ lái.
* **Cấu hình bánh xe**: **RWD (Rear-Wheel Drive)** – 2 bánh sau chủ động gắn động cơ DC có Encoder, 1 bánh xe mắt trâu (caster wheel) đa hướng đặt ở đầu xe phía trước chịu lực và xoay tự do $360^\circ$.
* **Nguyên lý bẻ lái**:
  * Đi thẳng: Vận tốc bánh trái = Vận tốc bánh phải ($V_L = V_R$).
  * Rẽ trái: $V_L < V_R$ (bánh phải quay nhanh hơn bánh trái).
  * Rẽ phải: $V_L > V_R$ (bánh trái quay nhanh hơn bánh phải).
  * Quay tại chỗ: Bánh trái quay lùi, bánh phải quay tiến ($V_L = -V_R$).

---

## 2. CẤU TRÚC THƯ MỤC DỰ ÁN (PROJECT REPOSITORY STRUCTURE)

Dự án được tổ chức theo mô hình quản lý tập trung (Monorepo), phân định ranh giới rõ ràng giữa các Node, hợp đồng dữ liệu dùng chung (`01_Common`) và phân tách giữa driver CAN tự viết (`PTM_CAN_Driver`) với driver tham khảo của S32DS SDK (`Example_CAN_Driver`):

```
Final_Project_Smart_Car/
├── 00_Docs/                                    <-- Tài liệu thiết kế & sơ đồ kiến trúc web
│   ├── CAN_BUS_CAR_SYSTEM_DESIGN.md            <-- Tài liệu đặc tả hệ thống hoàn chỉnh
│   └── can_network_architecture.html           <-- Giao diện web mô phỏng trực quan 6 tab
│
├── 01_Common/                                  <-- Hợp đồng dữ liệu dùng chung cho cả 4 Node
│   └── CAN_Messages.h                          <-- Định nghĩa ID, cấu trúc struct từng bản tin
│
├── 02_Node1_Gateway_ESP32/                     <-- Mã nguồn Node 1 (ESP32 Web Gateway & TWAI)
│   ├── src/                                    <-- Web Server, kết nối Wi-Fi, cầu nối CAN
│   └── data/                                   <-- Giao diện Web điều khiển trên điện thoại
│
├── 03_Node2_MainECU_S32K144/                   <-- Mã nguồn Node 2 (NXP S32K144 Main Powertrain)
│   ├── Example_CAN_Driver/                     <-- Driver CAN mẫu sinh từ S32DS SDK (để tham khảo)
│   │   ├── flexcan/
│   │   │   ├── flexcan_driver.c
│   │   │   ├── flexcan_hw_access.c / .h
│   │   │   └── flexcan_irq.c / .h
│   │   └── inc/
│   │       └── flexcan_driver.h
│   │
│   ├── PTM_CAN_Driver/                         <-- [TRỌNG TÂM] Driver FlexCAN tự viết mức thanh ghi
│   │   ├── ptm_can.c                           <-- Xử lý thanh ghi PCC, MCR, CTRL1, RAMn
│   │   └── ptm_can.h                           <-- Cung cấp các generic API cho Main/App gọi
│   │
│   ├── Hal_Actuators/                          <-- Điều khiển Driver L298N & đọc xung Encoder
│   │   ├── motor_l298n.c / .h
│   │   └── encoder.c / .h
│   │
│   └── App/                                    <-- Tầng ứng dụng (PID Cruise, Map ảo, AEB Failsafe)
│       ├── main.c
│       ├── pid_cruise.c / .h
│       └── virtual_map.c / .h
│
├── 04_Node3_ADAS_STM32/                        <-- Mã nguồn Node 3 (STM32 ToF Safety & AEB)
│   ├── Core/                                   <-- I2C Cảm biến ToF VL53L0X, XSHUT
│   └── Drivers/                                <-- bxCAN 500 kbps bắn bản tin khẩn 0x050
│
└── 05_Node4_Cluster_STM32/                     <-- Mã nguồn Node 4 (STM32 Cluster, BCM, IMU)
    ├── Core/                                   <-- Màn hình LCD 2004, MPU-6050, ADC đo Pin
    └── Drivers/                                <-- bxCAN 500 kbps, Heartbeat Monitor 4 node
```

---

## 3. DANH MỤC LINH KIỆN VÀ PHÂN BỔ PHẦN CỨNG (BOM)

Tất cả các vi điều khiển đều sử dụng **CAN Transceiver rời (External Transceiver - SN65HVD230 3.3V)** để đồng bộ mức điện áp và bảo vệ chip MCU.

| Nhóm | Tên linh kiện | Số lượng | Thông số / Chuẩn kết nối | Ghi chú vai trò |
| :--- | :--- | :---: | :--- | :--- |
| **MCU** | ESP32-WROOM-32 | 1 | 2.4GHz Wi-Fi/BLE, 3.3V logic | **Node 1**: Web Gateway & Telematics |
| | NXP S32K144 EVB | 1 | ARM Cortex-M4F, chuẩn Automotive | **Node 2**: Main ECU Powertrain & PTM CAN Driver |
| | STM32F103C8T6 (Blue Pill) | 2 | ARM Cortex-M3, 72 MHz | **Node 3** (ADAS ToF) và **Node 4** (Cluster/BCM) |
| **CAN Bus**| Module SN65HVD230 / VP230 | 4 | Chuẩn 3.3V, tốc độ tới 1 Mbps | Module Transceiver rời cho cả 4 Node |
| | Điện trở $120\Omega$ (1/4W) | 2 | Sai số 1% | Đặt ở 2 đầu xa nhất của đường bus |
| **Truyền động** | Động cơ DC giảm tốc kèm Encoder | 2 | Động cơ GA25 hoặc TT-Motor có Encoder Hall/Optical | Truyền động 2 bánh sau |
| | Driver động cơ L298N | 1 | Cầu H kép, dòng tải đỉnh 2A/kênh | Điều khiển chiều và PWM cho 2 motor |
| | Bánh xe mắt trâu (Caster Wheel) | 1 | Bi kim loại xoay $360^\circ$ | Lắp ở đầu trước xe (chịu tải, xoay tự do) |
| **Cảm biến**| Cảm biến ToF (VL53L0X hoặc VL53L1X) | 2 – 3 | Laser ToF, giao tiếp I2C | Gắn phía trước / hai góc đầu xe (ADAS) |
| | Cảm biến IMU MPU-6050 | 1 | 3 trục Gia tốc + 3 trục Con quay, I2C | Lắp trên Node 4: Đo dốc & Cảm biến va chạm |
| | Mạch chia áp đo Pin | 1 | Trở $10k\Omega$ và $2.2k\Omega$ | Hạ áp Pin 11.1V về dải $< 3.3V$ cho ADC STM32 |
| **Hiển thị & BCM** | Màn hình LCD 2004 hoặc OLED 0.96" | 1 | Giao tiếp I2C hoặc SPI | Gắn trên Node 4 (Đồng hồ táp-lô) |
| | Dàn LED báo hiệu (Đỏ, Vàng, Trắng) | 1 bộ | Kèm điện trở hạn dòng $330\Omega$ | Đèn phanh, đèn hazard, xi-nhan, pha |
| | Còi chíp (Buzzer 5V) | 1 | Điều khiển qua Transistor NPN | Báo động khi phanh gấp / va chạm |
| **Nguồn điện** | Pin Li-ion 3S (11.1V - 12.6V) | 1 | Dung lượng từ 2000mAh - 3000mAh | Nguồn tổng |
| | Mạch hạ áp Buck LM2596 / XL4015 | 2 | 1 module ra 5V (3A), 1 module ra 7.4V/9V | Tách riêng nguồn Logic MCU và Motor |

---

## 4. QUY HOẠCH MA TRẬN BẢN TIN CAN (CAN MESSAGE MATRIX)

Sử dụng chuẩn **CAN 2.0A (Standard Identifier 11-bit)**, tốc độ **500 kbps**.
* **Độ ưu tiên bus**: CAN ID càng nhỏ $\rightarrow$ Độ ưu tiên càng cao khi trọng tài bus (Arbitration).
* Các bản tin khẩn cấp (Phanh AEB, Va chạm Crash) chiếm các dải ID từ `0x000` đến `0x0FF`.

| CAN ID (Hex) | Tên bản tin | Node gửi | Chu kỳ / Trigger | DLC | Cấu trúc dữ liệu chi tiết |
| :---: | :--- | :---: | :---: | :---: | :--- |
| **`0x020`** | `CRASH_EVENT` | Node 4 (Cluster) | Event (Khi va chạm) | 2 | **B0**: Cờ va chạm (`0xAA`: Xảy ra va chạm mạnh)<br>**B1**: Trọng lượng gia tốc đỉnh (đơn vị: $0.1g$) |
| **`0x050`** | `AEB_TRIGGER` | Node 3 (ADAS) | Event (Khi cản $< 15\text{cm}$) | 2 | **B0**: Lệnh phanh khẩn cấp (`0x01`: Phanh ngắt motor, `0x00`: Bình thường)<br>**B1**: Khoảng cách vật cản gần nhất (cm) |
| **`0x100`** | `MANUAL_CONTROL` | Node 1 (Gateway) | 50 ms | 3 | **B0**: Tốc độ nền mong muốn ($V_{target}$: 0 – 100%)<br>**B1**: Hệ số bẻ lái rẽ ($Steer$: -100 [Trái] đến +100 [Phải])<br>**B2**: Hướng chạy (`0`: Stop, `1`: Tiến, `2`: Lùi) |
| **`0x120`** | `AUTO_MODE_CMD` | Node 1 (Gateway) | Event | 2 | **B0**: Chế độ lái (`0`: Manual, `1`: Cruise Control, `2`: Map ảo tự hành)<br>**B1**: ID của Lộ trình Map ảo (Route 1, Route 2, ...) |
| **`0x130`** | `CRUISE_SET` | Node 1 (Gateway) | Event | 2 | **B0**: Tốc độ Cruise cài đặt (đơn vị cm/s, ví dụ: 25, 30, 40)<br>**B1**: Enable/Disable Flag (`1`/`0`) |
| **`0x200`** | `ODOMETRY_SPEED` | Node 2 (S32K) | 50 ms | 4 | **B0-B1**: Vận tốc trung bình thực tế từ Encoder (cm/s, Int16)<br>**B2-B3**: Tổng quãng đường di chuyển (cm, Uint16) |
| **`0x250`** | `DISTANCE_OBSTACLE` | Node 3 (ADAS) | 100 ms | 3 | **B0**: Khoảng cách ToF Trước (cm)<br>**B1**: Khoảng cách ToF Trái (cm)<br>**B2**: Khoảng cách ToF Phải (cm) |
| **`0x300`** | `INCLINE_DATA` | Node 4 (Cluster) | 50 ms | 2 | **B0-B1**: Góc dốc Pitch (độ $\times 10$, ví dụ $+150 = +15.0^\circ$ lên dốc, $-80 = -8.0^\circ$ xuống dốc) |
| **`0x350`** | `BATTERY_STATUS` | Node 4 (Cluster) | 500 ms | 2 | **B0**: Điện áp tức thời (đơn vị: $0.1V$, ví dụ: $118 = 11.8V$)<br>**B1**: Phần trăm pin ước lượng ($0 - 100\%$) |
| **`0x700`** | `HEARTBEAT_GW` | Node 1 (Gateway) | 200 ms | 1 | **B0**: Mã trạng thái Node 1 (`0x00`: Hoạt động bình thường) |
| **`0x701`** | `HEARTBEAT_ECU` | Node 2 (S32K) | 200 ms | 1 | **B0**: Mã trạng thái Node 2 (`0x00`: Hoạt động bình thường) |
| **`0x702`** | `HEARTBEAT_ADAS` | Node 3 (ADAS) | 200 ms | 1 | **B0**: Mã trạng thái Node 3 (`0x00`: Bình thường, `0x01`: Lỗi cảm biến ToF) |
| **`0x703`** | `HEARTBEAT_BCM` | Node 4 (Cluster) | 200 ms | 1 | **B0**: Mã trạng thái Node 4 (`0x00`: Bình thường, `0x01`: Lỗi IMU) |

---

## 5. CẤU TRÚC 3 CHẾ ĐỘ LÁI & CÁC TÍNH NĂNG ĐIỀU KHIỂN

Hệ thống cung cấp **3 chế độ lái** chuyển đổi linh hoạt qua bản tin CAN `0x120` từ giao diện Web, tất cả đều được giám sát bởi lớp an toàn khẩn cấp **AEB** và **Crash Detection**:

### 5.1. Chế độ 1: Manual Control (Lái tay vi sai)
* Nhận lệnh từ bản tin CAN `0x100` với tốc độ nền $V_{target}$ và hệ số rẽ $Steer \in [-100, 100]$.
* S32K144 tính toán cấp PWM cho 2 kênh của Driver L298N:
  $$\Delta V = V_{target} \times \frac{|Steer|}{100}$$
  * **Khi rẽ trái ($Steer < 0$)**: $PWM_{Left} = V_{target} - \Delta V, \quad PWM_{Right} = V_{target} + \Delta V$
  * **Khi rẽ phải ($Steer > 0$)**: $PWM_{Left} = V_{target} + \Delta V, \quad PWM_{Right} = V_{target} - \Delta V$

### 5.2. Chế độ 2: Cruise Control kết hợp Bù dốc (Hill Compensation)
* **Vòng lặp kín PID**: Encoder đọc xung mỗi chu kỳ $T = 20\text{ms}$ để tính vận tốc thực tế $V_{meas}$. S32K144 tự động điều chỉnh PWM để duy trì tốc độ cài đặt từ CAN `0x130`.
* **Bù dốc bằng MPU-6050**:
  * Node 4 đọc góc dốc Pitch từ MPU-6050 gửi qua bản tin `0x300`.
  * Khi xe lên dốc ($\theta_{pitch} > 0$), S32K144 cộng thêm đại lượng Feedforward bù tụt ga:
    $$u_{FF} = K_{pitch} \cdot \sin(\theta_{pitch})$$
  * Thuật toán PID hoàn chỉnh:
    $$PWM = K_p \cdot e(t) + K_i \int e(t)dt + K_d \frac{de(t)}{dt} + u_{FF}$$

### 5.3. Chế độ 3: Map ảo tự hành tích hợp Cruise Control (Virtual Waypoint Navigation)
* Lộ trình được định nghĩa sẵn dưới dạng các đoạn đường thẳng và góc quay (Waypoint Path), ví dụ: `[Tiến 150cm] -> [Quay phải 90°] -> [Tiến 100cm] -> [Dừng]`.
* **Tích hợp Cruise Control**: Xe chạy thẳng với tốc độ Cruise ổn định (30 cm/s), dùng Encoder đo chính xác quãng đường (Odometry).
* **Bẻ góc vi sai bằng Gyroscope**: S32K144 quay 2 bánh ngược chiều nhau đến khi góc Yaw từ MPU-6050 đạt đủ $90^\circ$ thì dừng và chạy tiếp đoạn thẳng.
* **Can thiệp an toàn AEB**: Nếu gặp cản $< 15\text{cm}$ trong khi đang tự hành, xe tự động **PAUSE** lộ trình và dừng khẩn; khi cản rời đi, xe tự động **RESUME** chạy tiếp phần đường còn lại.

### 5.4. Cảm biến va chạm túi khí giả lập (Crash Detection)
* MPU-6050 liên tục đo gia tốc dọc trục xe ($a_x$). Khi đâm va cứng khiến gia tốc âm đột ngột $a_x < -2.5g$ ($\approx -24.5 \text{ m/s}^2$):
  1. Node 4 phát ngay lập tức bản tin ưu tiên cao nhất `0x020: CRASH_EVENT`.
  2. Node 2 (S32K144) bắt được `0x020` $\rightarrow$ Cắt hoàn toàn chân Enable của L298N, hãm ngắn mạch 2 motor dừng xe tại chỗ.
  3. Node 4 nhấp nháy còi chíp và đèn Hazard liên tục, hiển thị `"CRASH IMPACT!"` lên màn hình LCD.

### 5.5. Cơ chế Giám sát mạng CAN (Network Management / Heartbeat)
* Cả 4 node định kỳ phát bản tin Heartbeat `0x700` đến `0x703` mỗi $200\text{ms}$.
* Node 4 (LCD) và Node 1 (Web) duy trì 4 timer giám sát. Nếu sau $600\text{ms}$ không nhận được nhịp tim từ node nào, node đó bị đánh dấu là **OFFLINE/FAIL**.
* **Failsafe**: Nếu S32K144 mất kết nối với Gateway hoặc Node ADAS quá 500ms, xe tự động hạ tốc độ về 0 an toàn.

---

## 6. PHÂN CÔNG NHIỆM VỤ 5 THÀNH VIÊN TRONG DỰ ÁN

| Thành viên | Vị trí / Vai trò | Trọng tâm công việc | Sản phẩm bàn giao chính |
| :--- | :--- | :--- | :--- |
| **Thành viên 1 (Bạn)** | **CAN Network Architect & Low-Level Driver Engineer** | • Thiết kế toàn bộ giao thức CAN, ma trận bản tin `CAN_Messages.h`<br>• **Viết `PTM_CAN_Driver` mức thanh ghi trên S32K144** (PCC, Freeze mode, Bit Timing 500kbps, Mailbox RAM)<br>• Cung cấp generic API cho `main.c`<br>• Hỗ trợ MPU-6050 và tích hợp CAN lên Main Board | • File `CAN_Messages.h`<br>• Thư viện `PTM_CAN_Driver` (chạy độc lập, không dùng S32DS SDK)<br>• Test thông bus 500 kbps |
| **Thành viên 2** | **Powertrain & Autonomous Lead** | • Lập trình tầng App trên S32K144 (gọi API từ PTM_CAN_Driver)<br>• Điều khiển L298N (Lái vi sai PWM/DIR)<br>• Đọc Encoder Hall, thuật toán PID Cruise Control & Bù dốc<br>• Lập trình State Machine chạy Map ảo tự hành | • Module `Hal_Actuators` và `App`<br>• Xe chạy mượt 3 mode lái, dừng chuẩn xác khi nhận cờ AEB |
| **Thành viên 3** | **Safety & ADAS Engineer** | • Lập trình Node 3 (STM32F103 ADAS)<br>• Đọc 2-3 cảm biến ToF VL53L0X qua I2C (dùng chân XSHUT đổi địa chỉ)<br>• Thuật toán lọc khoảng cách, phát hiện cản $< 15\text{cm}$<br>• Cấu hình bxCAN bắn bản tin khẩn `0x050: AEB_TRIGGER` | • Firmware Node 3<br>• Bản tin phanh `0x050` phát với độ trễ $< 5\text{ms}$ khi đưa tay vào đầu xe |
| **Thành viên 4** | **Cluster, BCM & IMU Engineer** | • Lập trình Node 4 (STM32F103 Cluster/BCM)<br>• Đọc MPU-6050: Bộ lọc bù tính Pitch bù dốc và ngắt va chạm Crash ($a_x < -2.5g$)<br>• Hiển thị màn hình táp-lô LCD 2004 / OLED<br>• Bộ đếm Heartbeat Monitor báo lỗi node offline lên LCD<br>• Đọc ADC đo % Pin, điều khiển LED xi-nhan, phanh, còi | • Firmware Node 4<br>• Màn hình táp-lô cập nhật realtime, cảnh báo va chạm và nhịp tim mạng |
| **Thành viên 5** | **Gateway, Web & Hardware Lead** | • Lập trình Node 1 (ESP32 Web Gateway & TWAI Driver)<br>• Phát Wi-Fi AP, lập trình giao diện Web trên điện thoại chọn 3 mode<br>• Cầu nối chuyển đổi Web $\leftrightarrow$ CAN Bus<br>• Phụ trách phần cứng: Lắp ráp khung xe, đi dây CAN xoắn đôi + 2 trở $120\Omega$, mạch nguồn Buck cách ly | • Firmware Node 1 + Giao diện Web<br>• Bộ khung xe hoàn chỉnh, nguồn ổn định, dây bus CAN chuẩn |

---

## 7. ĐẶC TẢ MODULE `PTM_CAN_Driver` CHO S32K144

Module `PTM_CAN_Driver` nằm trong thư mục `03_Node2_MainECU_S32K144/PTM_CAN_Driver`, được viết hoàn toàn ở mức thanh ghi để thay thế driver mẫu trong `Example_CAN_Driver`.

### 7.1. Giao diện API cung cấp cho Main (`ptm_can.h`)
```c
#ifndef PTM_CAN_H
#define PTM_CAN_H

#include <stdint.h>

typedef enum {
    PTM_CAN_BAUD_250K,
    PTM_CAN_BAUD_500K,
    PTM_CAN_BAUD_1M
} PTM_CAN_Baudrate_t;

/* Khởi tạo FlexCAN0 mức thanh ghi (Clock, Freeze mode, Bit timing) */
void PTM_CAN0_Init(PTM_CAN_Baudrate_t baudrate);

/* Cấu hình 1 Mailbox làm bộ nhận RX (cho phép main chọn lọc ID nào, có bật ngắt không) */
void PTM_CAN0_ConfigRxMB(uint8_t mb_idx, uint32_t filter_id, uint8_t enable_irq);

/* Cấu hình 1 Mailbox làm bộ phát TX */
void PTM_CAN0_ConfigTxMB(uint8_t mb_idx);

/* Gửi bản tin CAN qua Mailbox TX */
uint8_t PTM_CAN0_Transmit(uint8_t mb_idx, uint32_t id, uint8_t dlc, const uint8_t *data);

/* Đọc dữ liệu từ Mailbox RX (Polling) */
uint8_t PTM_CAN0_Receive(uint8_t mb_idx, uint8_t *data);

#endif /* PTM_CAN_H */
```

### 7.2. Chiến lược tích hợp trong S32DS không bị ghi đè
* Dùng **S32DS Configuration Tool** chỉ để sinh code khởi tạo **Clock (SPLL 80MHz/40MHz)** và **Pin Muxing (PTE4/PTE5)**.
* **KHÔNG add component FlexCAN** trong S32DS Configuration Tool.
* Đặt toàn bộ mã nguồn driver tự viết vào thư mục `PTM_CAN_Driver/`. Thư mục này nằm ngoài thư mục `Generated_Code/` nên S32DS sẽ không bao giờ ghi đè lên file của bạn khi người dùng bấm "Update Code".

---

## 8. SƠ ĐỒ ĐẤU NỐI PHẦN CỨNG CHI TIẾT (PINOUT & WIRING)

### 8.1. Node 1: ESP32 (Gateway)
* `GPIO 5` $\rightarrow$ CAN Transceiver `CTX`
* `GPIO 4` $\rightarrow$ CAN Transceiver `CRX`
* `VCC / GND` $\rightarrow$ Nguồn 3.3V / GND

### 8.2. Node 2: NXP S32K144 (Main ECU)
* `PTE5` $\rightarrow$ CAN Transceiver `CTX` (CAN0_TX)
* `PTE4` $\rightarrow$ CAN Transceiver `CRX` (CAN0_RX)
* `PTD0`, `PTD1` $\rightarrow$ L298N `IN1`, `IN2` (Hướng Motor Trái)
* `PTD2`, `PTD3` $\rightarrow$ L298N `IN3`, `IN4` (Hướng Motor Phải)
* `PTD15` (FTM Ch0) $\rightarrow$ L298N `ENA` (PWM Motor Trái)
* `PTD16` (FTM Ch1) $\rightarrow$ L298N `ENB` (PWM Motor Phải)
* `PTA0`, `PTA1` $\rightarrow$ Đọc Encoder Motor Trái (Kênh A, B)
* `PTA6`, `PTA7` $\rightarrow$ Đọc Encoder Motor Phải (Kênh A, B)

### 8.3. Node 3: STM32F103 (ADAS)
* `PB9` $\rightarrow$ CAN Transceiver `CTX` (CAN_TX remap)
* `PB8` $\rightarrow$ CAN Transceiver `CRX` (CAN_RX remap)
* `PB6 (SCL)`, `PB7 (SDA)` $\rightarrow$ Chân I2C của Cảm biến ToF VL53L0X
* `PA4`, `PA5` $\rightarrow$ Chân `XSHUT` chọn địa chỉ I2C cho các ToF

### 8.4. Node 4: STM32F103 (Cluster / BCM)
* `PB9` $\rightarrow$ CAN Transceiver `CTX`
* `PB8` $\rightarrow$ CAN Transceiver `CRX`
* `PB10 (SCL)`, `PB11 (SDA)` $\rightarrow$ Màn hình LCD 2004 I2C + MPU-6050
* `PA0` (ADC1_IN0) $\rightarrow$ Điểm giữa của Cầu phân áp đo Pin
* `PA1`, `PA2` $\rightarrow$ Transistor lái LED Xi-nhan Trái / Phải
* `PA3` $\rightarrow$ LED Đèn Phanh (Brake Light)
* `PA4` $\rightarrow$ Còi chíp Buzzer cảnh báo
