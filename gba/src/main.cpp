#include "bn_core.h"
#include "application.h"
#include "bn_timers.h"

int main()
{
    bn::core::init();
    static BN_DATA_EWRAM_BSS manosaba::application app;
    while(true)
    {
        bn::core::update();
        unsigned begin=unsigned(bn::core::current_cpu_ticks());
        app.update();
        manosaba::graphics_performance.update_ticks=unsigned(bn::core::current_cpu_ticks())-begin;
#ifdef MANOSABA_AUDIO_RESERVE_PERCENT
        int start=bn::core::current_cpu_ticks();
        while(bn::core::current_cpu_ticks()-start < bn::timers::ticks_per_frame()*MANOSABA_AUDIO_RESERVE_PERCENT/100) {}
#endif
    }
}
