#ifndef MOTOR_L298N_H_
#define MOTOR_L298N_H_

#include <stdint.h>

typedef enum { MOTOR_LEFT = 0, MOTOR_RIGHT = 1 } motor_side_t;

/* Khởi tạo PWM 1 kHz trên FTM0 (PTD15, PTD16) và chân chiều PTD0..PTD3; 2 motor đứng yên */
void   Motor_Init(void);

/* duty: -1000..1000 (phần nghìn). Dấu = chiều quay, 0 = thả trôi */
void   Motor_Set(motor_side_t side, int16_t duty);

/* Hãm ngắn mạch cả 2 motor */
void   Motor_Brake(void);

/* +1 tiến, -1 lùi: chiều của lệnh khác 0 gần nhất */
int8_t Motor_GetDir(motor_side_t side);

#endif /* MOTOR_L298N_H_ */
