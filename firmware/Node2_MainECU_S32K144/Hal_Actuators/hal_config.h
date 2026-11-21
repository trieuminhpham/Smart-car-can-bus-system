#ifndef HAL_CONFIG_H_
#define HAL_CONFIG_H_

/* Thông số xe: đo thực tế rồi sửa ở Bước 4 */
#define WHEEL_DIAMETER_CM    6.5f       /* đường kính bánh vàng */
#define WHEEL_BASE_CM        13.5f      /* khoảng cách tâm 2 bánh sau */
#define ENC_SLOTS_PER_REV    20U        /* số lỗ trên đĩa encoder */
#define ENC_DEBOUNCE_US      2000U      /* bỏ sườn đến sớm hơn 2 ms (chống rung xung) */
#define ENC_STOP_TIMEOUT_MS  300U       /* quá 300 ms không có sườn lên thì coi tốc độ = 0 */
#define HAL_TIMER_HZ         8000000UL  /* LPIT0 chạy bằng SOSCDIV2 = 8 MHz */

#endif /* HAL_CONFIG_H_ */
