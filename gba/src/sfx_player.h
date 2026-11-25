#ifndef MANOSABA_SFX_PLAYER_H
#define MANOSABA_SFX_PLAYER_H
#include "sfx_command.h"
namespace manosaba
{
class save_archive;
struct sfx_checkpoint
{
    struct sound
    {
        int cue = -1, volume = 0;
        bool loop = false;
    } sounds[2];
    void archive(save_archive& data);
    void legacy_archive(save_archive& data);
};
namespace sfx_player
{
int find(const char* alias);
void execute(const sfx_command& command);
void update(int frames = 1);
void reset();
sfx_checkpoint checkpoint();
void restore(const sfx_checkpoint& saved);
int active_voices();
bool foreground_active();
int current_cue();
void set_master_volume(int q10);
} // namespace sfx_player
} // namespace manosaba
#endif
