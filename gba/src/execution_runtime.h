#ifndef MANOSABA_EXECUTION_RUNTIME_H
#define MANOSABA_EXECUTION_RUNTIME_H
#include <stdint.h>
typedef struct execution_runtime_profile {uint32_t active,ready,charge,pressed,complete,delay,frames,missed,core_ticks,max_ticks,draw_ticks,max_draw;} execution_runtime_profile;
#ifdef __cplusplus
extern "C" {extern execution_runtime_profile execution_runtime;}
#endif
#endif
