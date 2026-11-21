#include "music_room_data.h"
#include <cassert>
#include <cstdio>
using namespace manosaba;
int main()
{
    bool bits[64]={};
    for(int i=0;i<music_room_count;++i)
    {
        const auto& track=music_room_tracks[i];assert(track.bit<64 && !bits[track.bit]);bits[track.bit]=true;
        assert(track.bit!=46 && *track.title);
    }
    auto key=[](unsigned id,unsigned variant=1){return id|(variant<<12);};
    assert(music_collection_bit(key(12))==11 && music_collection_bit(key(16))==11);
    assert(music_collection_bit(key(13))==12 && music_collection_bit(key(14))==12);
    assert(music_collection_bit(key(27))==26 && music_collection_bit(key(27,2))==49 && music_collection_bit(key(27,3))==49);
    assert(music_collection_bit(key(2)|(1u<<25))==1);
    assert(music_collection_bit(key(37,1))==36 && music_collection_bit(key(37,2))==36);
    assert(music_collection_bit(key(4)|(1u<<24))==51);
    assert(music_collection_bit(key(47))<0 && music_collection_bit(key(18))==17 && music_collection_bit(key(3)|(1u<<26))<0);
    std::puts("PASS: 49 approved music-room tracks, stable bits, shared aliases, separate 027 textures, hidden 047/ringtone");
}
