#ifndef MANOSABA_MUSIC_BACKEND_H
#define MANOSABA_MUSIC_BACKEND_H

namespace manosaba::music_backend
{
void start(int module, int volume_q10, bool loop, int order);
void stop();
void set_volume(int volume_q10);
void set_loop(bool loop);
void update();
void pause();
void resume();
bool playing();
bool looping();
}
#endif
