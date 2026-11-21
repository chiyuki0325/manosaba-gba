#ifndef MANOSABA_MUSIC_PLAYER_H
#define MANOSABA_MUSIC_PLAYER_H

#include "music_command.h"

namespace manosaba
{
class save_archive;
struct music_checkpoint
{
    uint32_t key=0;
    int volume=0,fade_start=0,fade_target=0,fade_total=0,fade_left=0;
    bool loop=false,stopping=false;
    uint32_t return_key=0;int return_volume=0;bool return_loop=false,return_pending=false;
    void archive(save_archive& data);
};
enum class music_result : uint8_t { started, modified, stopped, not_packed, invalid_alias };

namespace music_player
{
// Call once after bn::core::update(), before dispatching this frame's VM events.
void update(bool foreground_sfx=false);
music_result execute(const music_command& command);
void reset();
void pause();
void resume();
bool busy();
bool playing();
const char* current_alias();
uint32_t generation(); // Changes only when a new backend playback starts.
uint32_t collection_key(); // Script gain, independent of the user's master volume.
void set_master_volume(int volume_q10);
void play_test_track(bool with_intro = true);
music_checkpoint checkpoint();
void restore(const music_checkpoint& checkpoint);
}
}
#endif
