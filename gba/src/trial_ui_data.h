#ifndef TRIAL_UI_DATA_H
#define TRIAL_UI_DATA_H
#include "ui_picture.h"
#include "label_art.h"
namespace manosaba
{
extern const uint8_t trial_ui_bytes[];
extern const ui_picture trial_choice_pictures[3];
extern const uint8_t trial_history_remap[16];
extern const uint32_t* const trial_bubbles[10];
extern const uint32_t* const trial_cancel_bubbles[2];
extern const label_art trial_badges[11];
extern const uint8_t trial_label_inks[16], trial_badge_colors[11];
extern const label_art trial_clock;
extern const uint8_t trial_clock_choice_inks[16];
struct trial_evidence_asset
{
    const char* id;
    ui_picture image;
    uint8_t x, y, width, height;
};
extern const trial_evidence_asset trial_evidence_pictures[];
extern const int trial_evidence_count;
} // namespace manosaba
#endif
