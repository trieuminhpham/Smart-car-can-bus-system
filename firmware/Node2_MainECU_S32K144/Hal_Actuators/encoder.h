#ifndef ENCODER_H_
#define ENCODER_H_

#include <stdint.h>
#include "motor_l298n.h"

/* Gọi sau Motor_Init. Dùng ngắt PORTA (PTA0, PTA6) và LPIT0 kênh 1 */
void    Encoder_Init(void);

/* Số sườn có dấu: tiến cộng, lùi trừ (chiều lấy theo lệnh motor) */
int32_t Encoder_GetEdges(motor_side_t side);

/* Quãng đường có dấu (cm), tính từ lần Reset */
float   Encoder_GetDistanceCm(motor_side_t side);

/* Tốc độ có dấu (cm/s); 0 khi đứng yên */
float   Encoder_GetSpeedCmS(motor_side_t side);

/* Góc quay từ lần Reset (độ), dương = quay trái */
float   Encoder_GetHeadingDeg(void);

/* Xoá quãng đường và góc */
void    Encoder_Reset(void);

#endif /* ENCODER_H_ */
