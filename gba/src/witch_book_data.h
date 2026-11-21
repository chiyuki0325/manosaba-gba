#ifndef WITCH_BOOK_DATA_H
#define WITCH_BOOK_DATA_H
#include <cstdint>
namespace manosaba {
constexpr int book_entry_count=168;
struct book_entry {const char* id;uint16_t story_id,first;uint8_t versions,kind;uint16_t color;};
struct book_text {const char* name;const char* compact;const char* detail;};
extern const book_entry book_entries[book_entry_count];
extern const book_text book_texts[];
extern const uint8_t book_initial_states[][book_entry_count];
extern const uint8_t book_initial_chapters[];
}
#endif
