#include "S32K144.h"
#include "hal_config.h"
#include "encoder.h"

#define PIN_LEFT        0U        /* PTA0 */
#define PIN_RIGHT       6U        /* PTA6 */
#define ENC_PIN_MASK    ((1UL << PIN_LEFT) | (1UL << PIN_RIGHT))
#define TICKS_PER_US    (HAL_TIMER_HZ / 1000000UL)
#define DEBOUNCE_TICKS  ((uint32_t)ENC_DEBOUNCE_US * TICKS_PER_US)
#define STOP_TICKS      ((uint32_t)ENC_STOP_TIMEOUT_MS * 1000UL * TICKS_PER_US)
#define PI_F            3.14159265f
#define CM_PER_SLOT     (PI_F * WHEEL_DIAMETER_CM / (float)ENC_SLOTS_PER_REV)   /* 1 lỗ + 1 nan */
#define CM_PER_EDGE     (CM_PER_SLOT / 2.0f)

typedef struct {
    volatile int32_t  edges;      /* số sườn có dấu */
    volatile uint32_t lastEdge;   /* thời điểm sườn hợp lệ gần nhất (tick) */
    volatile uint32_t lastRise;   /* thời điểm sườn lên gần nhất (tick) */
    volatile uint32_t period;     /* số tick giữa 2 sườn lên; 0 = chưa đo được */
    volatile uint8_t  level;      /* mức chân đã chấp nhận gần nhất */
} enc_t;

static enc_t s_enc[2];

static uint32_t now_ticks(void)
{
    return 0xFFFFFFFFUL - LPIT0->TMR[1].CVAL;        /* LPIT đếm xuống -> đổi thành đếm lên */
}

void Encoder_Init(void)
{
    /* LPIT0 kênh 1: bộ đếm 32 bit chạy tự do, 8 MHz (SOSCDIV2) */
    if ((PCC->PCCn[PCC_LPIT_INDEX] & PCC_PCCn_CGC_MASK) == 0U) {
        PCC->PCCn[PCC_LPIT_INDEX] = PCC_PCCn_PCS(1);
        PCC->PCCn[PCC_LPIT_INDEX] |= PCC_PCCn_CGC_MASK;
    }
    LPIT0->MCR |= LPIT_MCR_M_CEN_MASK;
    LPIT0->TMR[1].TVAL  = 0xFFFFFFFFUL;
    LPIT0->TMR[1].TCTRL = LPIT_TMR_TCTRL_T_EN_MASK;

    /* PTA0, PTA6: GPIO vào, pull-up, lọc thụ động, ngắt ở cả 2 sườn */
    PCC->PCCn[PCC_PORTA_INDEX] |= PCC_PCCn_CGC_MASK;
    PORTA->PCR[PIN_LEFT]  = PORT_PCR_MUX(1) | PORT_PCR_PE_MASK | PORT_PCR_PS_MASK
                          | PORT_PCR_PFE_MASK | PORT_PCR_IRQC(0xB);
    PORTA->PCR[PIN_RIGHT] = PORT_PCR_MUX(1) | PORT_PCR_PE_MASK | PORT_PCR_PS_MASK
                          | PORT_PCR_PFE_MASK | PORT_PCR_IRQC(0xB);
    PTA->PDDR &= ~(uint32_t)ENC_PIN_MASK;

    s_enc[MOTOR_LEFT].level  = (uint8_t)((PTA->PDIR >> PIN_LEFT) & 1UL);
    s_enc[MOTOR_RIGHT].level = (uint8_t)((PTA->PDIR >> PIN_RIGHT) & 1UL);
    PORTA->ISFR = ENC_PIN_MASK;                      /* ghi 1 để xoá cờ */
    S32_NVIC->ICPR[PORTA_IRQn / 32U] = 1UL << (PORTA_IRQn % 32U);
    S32_NVIC->ISER[PORTA_IRQn / 32U] = 1UL << (PORTA_IRQn % 32U);
}

static void enc_edge(enc_t *e, uint8_t level, int8_t dir, uint32_t t)
{
    if (level == e->level) {
        return;                                       /* cặp xung nhiễu: bỏ */
    }
    if ((t - e->lastEdge) < DEBOUNCE_TICKS) {
        return;                                       /* đến quá sớm sau sườn trước: bỏ */
    }
    e->level    = level;
    e->lastEdge = t;
    e->edges   += dir;
    if (level != 0U) {                                /* sườn lên: đo thời gian của 1 lỗ */
        e->period   = t - e->lastRise;
        e->lastRise = t;
    }
}

void PORTA_IRQHandler(void)
{
    const uint32_t t   = now_ticks();
    const uint32_t isf = PORTA->ISFR;
    const uint32_t in  = PTA->PDIR;

    PORTA->ISFR = isf;
    if ((isf & (1UL << PIN_LEFT)) != 0U) {
        enc_edge(&s_enc[MOTOR_LEFT], (uint8_t)((in >> PIN_LEFT) & 1UL), Motor_GetDir(MOTOR_LEFT), t);
    }
    if ((isf & (1UL << PIN_RIGHT)) != 0U) {
        enc_edge(&s_enc[MOTOR_RIGHT], (uint8_t)((in >> PIN_RIGHT) & 1UL), Motor_GetDir(MOTOR_RIGHT), t);
    }
}

int32_t Encoder_GetEdges(motor_side_t side)
{
    return s_enc[side].edges;
}

float Encoder_GetDistanceCm(motor_side_t side)
{
    return (float)s_enc[side].edges * CM_PER_EDGE;
}

float Encoder_GetSpeedCmS(motor_side_t side)
{
    const uint32_t sinceRise = now_ticks() - s_enc[side].lastRise;
    uint32_t period = s_enc[side].period;
    float v;

    if ((period == 0U) || (sinceRise > STOP_TICKS)) {
        return 0.0f;                                  /* coi như đứng yên */
    }
    if (sinceRise > period) {
        period = sinceRise;                           /* đang chậm lại: dùng thời gian đã chờ */
    }
    v = CM_PER_SLOT * (float)HAL_TIMER_HZ / (float)period;
    return (Motor_GetDir(side) > 0) ? v : -v;
}

float Encoder_GetHeadingDeg(void)
{
    const float dl = Encoder_GetDistanceCm(MOTOR_LEFT);
    const float dr = Encoder_GetDistanceCm(MOTOR_RIGHT);
    return (dr - dl) / WHEEL_BASE_CM * (180.0f / PI_F);
}

void Encoder_Reset(void)
{
    s_enc[MOTOR_LEFT].edges  = 0;
    s_enc[MOTOR_RIGHT].edges = 0;
}
