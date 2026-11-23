#ifndef MANOSABA_SAVE_CHECKPOINT_H
#define MANOSABA_SAVE_CHECKPOINT_H
#include "stage_format.h"
#include "witch_book_data.h"
namespace manosaba {
constexpr int save_checkpoint_version=5;
constexpr int save_vm_checkpoint_bytes=9+12+16+book_entry_count+1+56+10+9+65;
constexpr int save_stage_checkpoint_bytes=stage_entity_count*(4*int(stage_property::count)+7)+1;
constexpr int save_audio_checkpoint_bytes=9+2*9;
constexpr int save_checkpoint_bytes=save_vm_checkpoint_bytes+save_stage_checkpoint_bytes+save_audio_checkpoint_bytes;
constexpr uint8_t save_cursor=1,save_stage_pending=2,save_failure=4;
static_assert(save_checkpoint_bytes<=1280);
static_assert(256+2*save_checkpoint_bytes<=3072);
}
#endif
