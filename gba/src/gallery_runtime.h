#pragma once
#include <stdint.h>
typedef struct gallery_runtime_profile {uint32_t active,mode,selected,page,image,ready,unlocked,upload,pan,native,phase,cell,row;} gallery_runtime_profile;
#ifdef __cplusplus
extern "C" {
#endif
extern gallery_runtime_profile gallery_runtime;
#ifdef __cplusplus
}
#endif
