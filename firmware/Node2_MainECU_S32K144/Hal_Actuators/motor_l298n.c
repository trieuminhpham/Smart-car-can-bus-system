#include "S32K144.h"
#include "motor_l298n.h"

#define PWM_MOD   7999U          /* 8 MHz / 8000 = PWM 1 kHz */
#define DIR_MASK  0xFUL          /* PTD0..PTD3 = IN1..IN4 */

static volatile int8_t s_dir[2] = { 1, 1 };

void Motor_Init(void)
{
    uint32_t p;

    PCC->PCCn[PCC_PORTD_INDEX] |= PCC_PCCn_CGC_MASK;
    for (p = 0U; p < 4U; p++) {
        PORTD->PCR[p] = PORT_PCR_MUX(1);              /* IN1..IN4: GPIO (PTD3 thôi làm NMI) */
    }
    PTD->PCOR  = DIR_MASK;
    PTD->PDDR |= DIR_MASK;
    PORTD->PCR[15] = PORT_PCR_MUX(2);                 /* FTM0_CH0 -> ENA */
    PORTD->PCR[16] = PORT_PCR_MUX(2);                 /* FTM0_CH1 -> ENB */

    PCC->PCCn[PCC_FTM0_INDEX] &= ~PCC_PCCn_CGC_MASK;
    PCC->PCCn[PCC_FTM0_INDEX] |= PCC_PCCn_PCS(1);    /* nguồn SOSCDIV1 = 8 MHz */
    PCC->PCCn[PCC_FTM0_INDEX] |= PCC_PCCn_CGC_MASK;
    FTM0->MODE |= FTM_MODE_WPDIS_MASK;
    FTM0->SC = FTM_SC_PWMEN0_MASK | FTM_SC_PWMEN1_MASK | FTM_SC_PS(0);
    FTM0->COMBINE = 0U;
    FTM0->POL = 0U;
    FTM0->CNTIN = 0U;
    FTM0->MOD = PWM_MOD;
    FTM0->CONTROLS[0].CnSC = FTM_CnSC_MSB_MASK | FTM_CnSC_ELSB_MASK;  /* PWM cạnh, mức cao */
    FTM0->CONTROLS[1].CnSC = FTM_CnSC_MSB_MASK | FTM_CnSC_ELSB_MASK;
    FTM0->CONTROLS[0].CnV = 0U;
    FTM0->CONTROLS[1].CnV = 0U;
    FTM0->SC |= FTM_SC_CLKS(3);                       /* bắt đầu đếm bằng clock chọn ở PCC */
}

void Motor_Set(motor_side_t side, int16_t duty)
{
    const uint32_t in_a = (side == MOTOR_LEFT) ? (1UL << 0) : (1UL << 2);
    const uint32_t in_b = (side == MOTOR_LEFT) ? (1UL << 1) : (1UL << 3);
    uint32_t mag;

    if (duty > 1000)  { duty = 1000; }
    if (duty < -1000) { duty = -1000; }

    if (duty > 0) {
        s_dir[side] = 1;
        PTD->PCOR = in_b;
        PTD->PSOR = in_a;
        mag = (uint32_t)duty;
    } else if (duty < 0) {
        s_dir[side] = -1;
        PTD->PCOR = in_a;
        PTD->PSOR = in_b;
        mag = (uint32_t)(-duty);
    } else {
        PTD->PCOR = in_a | in_b;
        mag = 0U;                                     /* EN = 0: thả trôi */
    }
    FTM0->CONTROLS[side].CnV = (mag * (PWM_MOD + 1U)) / 1000U;   /* 1000 ‰ -> CnV > MOD = 100% */
}

void Motor_Brake(void)
{
    PTD->PSOR = DIR_MASK;                             /* IN1 = IN2, IN3 = IN4 */
    FTM0->CONTROLS[0].CnV = PWM_MOD + 1U;             /* EN = 100%: hãm ngắn mạch */
    FTM0->CONTROLS[1].CnV = PWM_MOD + 1U;
}

int8_t Motor_GetDir(motor_side_t side)
{
    return s_dir[side];
}
