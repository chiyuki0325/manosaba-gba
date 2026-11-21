#ifndef MANOSABA_SFX_COMMAND_H
#define MANOSABA_SFX_COMMAND_H
#include <stdint.h>
namespace manosaba {
struct sfx_command {
    int cue=-1,volume=1024,fade_frames=0;
    bool loop=false,stop=false,ambient=false,loops_only=false;
};
}
#endif
