#ifndef STORY_FORMAT_H
#define STORY_FORMAT_H

#include <cstdint>

namespace manosaba
{
// Version 3 adds structured SFX commands; logical TLV offsets survive compression.
constexpr int story_format_version = 3;
constexpr int story_block_size = 4096;
constexpr int story_text_capacity = 384;
constexpr int story_option_capacity = 160;
constexpr int story_max_options = 8;
constexpr int story_history_capacity = 384;
struct story_history_entry
{
    uint16_t pc;
    uint8_t speaker, lines;
};
enum class story_op : uint8_t
{
    say = 1,
    jump,
    branch,
    menu,
    flags,
    flag,
    backdrop,
    character,
    music,
    pause,
    end,
    visual,
    stage,
    trial,
    debate,
    trial_menu,
    test,
    variable,
    resume,
    cancel_resume,
    inventory,
    timer,
    damage,
    cutin,
    picture,
    bad_id,
    book_update,
    book_clear,
    book_reset,
    bad_end,
    title,
    sfx,
    toast,
    reenact,
    screen,
    credits,
    execution,
    map
};
// Normal menu option value byte: bit 0 marks ChoiceButtons/Adv/Bad.
// Map menus use that byte for the area ID instead.
// Debate duration: low 15 bits are frames; bit 15 retains a following Stop.
struct story_block
{
    uint32_t offset;
    uint16_t size;
    uint16_t codec; // 0 = raw, 1 = LZSS (flags MSB first, 12-bit distance, 4-bit length).
};
struct story_script
{
    const char* path;
    const char* title;
    uint32_t size;
    uint16_t first_block;
    uint16_t block_count;
    uint16_t graphics_bank;
    uint16_t mode; // 0 = regular story, 1 = trial, 2 = noncanonical operation demo.
};
struct story_color_span
{
    uint16_t first, last, history_first, history_last;
};
struct story_text_style
{
    uint16_t script, pc, end, first;
    uint8_t count, color;
};
extern const story_text_style story_text_styles[];
extern const story_color_span story_color_spans[];
extern const uint16_t story_text_colors[];
extern const int story_text_style_count;
const story_text_style* find_text_style(int script, unsigned pc, bool history = false);
struct story_item
{
    const char* id;
    const char* label;
    uint8_t kind;
};
struct trial_shake_spec
{
    uint16_t frames;
    int8_t x, y;
    uint8_t period, damping;
};
struct story_effect_entry
{
    uint16_t color, amount;
    trial_shake_spec shake;
    uint8_t fire;
};
extern const story_effect_entry story_effect_entries[];
extern const uint8_t story_effect_ids[];
// Inventory Clue/Profile payloads use count=0: the VM reads current book state.
// Map inventories carry an explicit, script-restricted item-ID list.
extern const story_item story_items[];
extern const int story_item_count;
extern const uint16_t story_common_bad_scripts[13];
extern const uint8_t story_bytes[];
extern const story_block story_blocks[];
extern const story_script story_scripts[];
extern const int story_script_count;
extern const int story_selectable_count;
extern const int story_trial_first, story_trial_count;
extern const char* const story_speakers[];
extern const char* const story_music_aliases[];
} // namespace manosaba
#endif
