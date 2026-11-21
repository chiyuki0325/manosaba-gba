#ifndef DIALOGUE_HISTORY_RUNTIME_H
#define DIALOGUE_HISTORY_RUNTIME_H
#include <stdint.h>
typedef struct dialogue_history_profile {
    uint32_t open,count,first,rows,ready,script,line_left,total_left;
} dialogue_history_profile;
#ifdef __cplusplus
extern "C" {extern dialogue_history_profile dialogue_history_runtime;}
#endif
#endif
