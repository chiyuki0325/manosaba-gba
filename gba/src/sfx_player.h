#ifndef MANOSABA_SFX_PLAYER_H
#define MANOSABA_SFX_PLAYER_H
#include "sfx_command.h"
namespace manosaba {
class save_archive;
namespace sfx_player {
int find(const char* alias);
void execute(const sfx_command& command);
void update(int frames=1);
void reset();
void archive(save_archive& data);
int active_voices();
bool foreground_active();
int current_cue();
void set_master_volume(int q10);
}
}
#endif
