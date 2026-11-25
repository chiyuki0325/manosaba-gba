#ifndef MUSIC_CATALOG_H
#define MUSIC_CATALOG_H
namespace manosaba
{
struct music_entry
{
    const char* label;
    const char* description;
    int module_id; // -1: not made yet
    int volume;    // 0..1024
    bool loop;
    int start_order;
    const char* alias;
};
extern const music_entry music_catalog[];
extern const int music_count;
extern const int music_default_index;
} // namespace manosaba
#endif
