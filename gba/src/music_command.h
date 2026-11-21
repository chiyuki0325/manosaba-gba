#ifndef MANOSABA_MUSIC_COMMAND_H
#define MANOSABA_MUSIC_COMMAND_H

#include <stdint.h>

namespace manosaba
{
enum music_parameter : uint16_t
{
    music_has_path = 1,
    music_has_volume = 2,
    music_has_loop = 4,
    music_has_intro = 8,
    music_has_duration = 16,
    music_has_fade_in = 32,
    music_audition = 64 // Keep the untrimmed export lead for the music DEMO.
};

struct music_command
{
    const char* path = nullptr;
    const char* intro = nullptr;
    int volume = 1024;
    int duration_frames = 0;
    int fade_in_frames = 0;
    uint16_t parameters = 0;
    bool loop = true;
    bool wait = false;
    bool stop = false;
};
}
#endif
