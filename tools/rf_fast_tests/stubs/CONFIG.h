#ifndef RF_FAST_HOST_CONFIG_H
#define RF_FAST_HOST_CONFIG_H
#include <stdint.h>
#define __INTERRUPT
#define __HIGH_CODE
#define ENABLE 1
#define DISABLE 0
#define TMR0_3_IT_CYC_END 1
#define TMR2_IRQn 2
uint32_t sim_clock(unsigned role);
void sim_timer(unsigned role,uint32_t delay);
void sim_cancel(unsigned role);
static inline void SYS_DisableAllIrq(uint32_t *v){*v=0;}
static inline void SYS_RecoverIrq(uint32_t v){(void)v;}
static inline void PFIC_SetPriority(int i,int p){(void)i;(void)p;}
static inline void PFIC_EnableIRQ(int i){(void)i;}
static inline uint32_t GetSysClock(void){return 1000000u;}
static inline void TMR2_Disable(void){sim_cancel(SIM_ROLE);}
static inline void TMR2_ITCfg(int e,int f){(void)e;(void)f;}
static inline void TMR2_ClearITFlag(int f){(void)f;}
static inline int TMR2_GetITFlag(int f){(void)f;return 1;}
static inline void TMR2_TimerInit(uint32_t t){sim_timer(SIM_ROLE,t);}
#endif
