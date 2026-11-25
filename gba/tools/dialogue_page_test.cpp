#include "dialogue_page.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace manosaba;
static void expect(bool value, const char* message)
{
    if (!value)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
static void released(advance_button& button, int frames = 10)
{
    while (frames--)
        expect(!button.update(false, true), "release never advances");
}
int main()
{
    dialogue_page page;
    const char* text = "甲乙丙\n丁戊己\n庚辛";
    page.begin(text, 0, graphical_dialogue);
    expect(page.visible() == 0 && !page.complete(), "page begins unrevealed");
    expect(page.end() == int(std::strstr(text, "庚") - text), "measure the complete page before drawing");
    expect(page.tick() == reveal_step::append && page.visible() == 3 && page.fading(),
           "one full UTF-8 character appears");
    expect(page.tick() == reveal_step::brighten && page.visible() == 3 && !page.fading(),
           "brightening cannot advance dialogue");
    page.finish();
    expect(page.complete() && page.visible() == page.end(), "A fills only the current page");
    int next = page.end();
    page.begin(text, next, graphical_dialogue);
    expect(page.start() == next && page.end() == int(std::strlen(text)), "next page starts exactly at its boundary");
    int previous = page.visible();
    for (int frame = 0; !page.complete(); ++frame)
    {
        expect(frame < 30, "bounded reveal");
        page.tick();
        expect(page.visible() >= previous, "reveal never rewinds");
        expect((static_cast<unsigned char>(text[page.visible()]) & 0xc0) != 0x80, "never expose partial UTF-8");
        previous = page.visible();
    }

    page.begin("甲\ue000乙", 0, dialogue_layout{0, 0, 24, 12});
    expect(page.end() == 3, "direction cell includes its right padding when wrapping");
    page.begin("甲\ue000乙", 0, dialogue_layout{0, 0, 25, 12});
    expect(page.end() == 6, "direction reserves 12px plus 1px right padding");
    page.begin("\ue003\ue004", 0, dialogue_layout{0, 0, 26, 12});
    expect(page.end() == 6, "combined D-pad glyphs keep the same 13px advance");

    const char* split = "可是，她被无形的锁链束缚着，\r就像个囚犯一样，连捂紧耳朵也做不\r到。";
    page.begin(split, 0, graphical_dialogue);
    expect(split[page.end()], "source line arrangement retains its third line by default");
    page.begin(split, 0, graphical_dialogue, false);
    expect(!split[page.end()], "folding soft breaks fits the full 32-character sentence in one page");
    page.begin("甲\n乙\n丙", 0, graphical_dialogue, false);
    expect(page.end() == 8, "forced short-line emphasis still paginates with source breaks disabled");
    page.begin("ab\rcd", 0, dialogue_layout{0, 0, 24, 12}, false);
    expect(page.end() == 4, "Latin words retain a separating space when soft breaks fold");

    advance_button button;
    sentence_guard guard;
    guard.restart();
    for (int frame = 0; frame < 17; ++frame)
    {
        expect(!guard.ready(), "sentence guard blocks early rapid advance");
        guard.tick();
    }
    expect(!guard.ready(), "sentence guard lasts 18 readable frames");
    guard.tick();
    expect(guard.ready(), "sentence guard eventually releases");
    guard.restart();
    expect(!guard.ready(), "manual reveal restarts the sentence guard");
    button.reset();
    expect(!button.update(true, true), "entry press cannot advance a new page");
    released(button);
    int accepted = 0;
    for (int frame = 0; frame < 300; ++frame)
        accepted += button.update(true, true);
    expect(accepted == 1, "a long hold accepts exactly one action");
    button.update(false, true);
    expect(!button.update(true, true), "one-frame key bounce is ignored");
    released(button);
    expect(button.update(true, true), "release and a fresh press are accepted");
    for (int frame = 0; frame < 30; ++frame)
        expect(!button.update(false, false), "loading blocks input");
    expect(!button.update(true, false), "loading press is consumed");
    for (int frame = 0; frame < 30; ++frame)
        expect(!button.update(true, true), "held loading press is not queued");
    released(button);
    expect(button.update(true, true), "fresh press after loading works");

    page.begin("甲乙丙", 0, graphical_dialogue);
    button.reset();
    released(button);
    int dialogue = 0;
    for (int frame = 0; frame < 300; ++frame)
    {
        if (button.update(true, true))
        {
            if (!page.complete())
                page.finish();
            else
                ++dialogue;
        }
    }
    expect(dialogue == 0 && page.complete(), "one A press finishes reveal without advancing the VM");
    released(button);
    for (int frame = 0; frame < 300; ++frame)
        if (button.update(true, true))
            ++dialogue;
    expect(dialogue == 1, "the next physical press advances exactly once");
    std::puts("PASS: UTF-8 reveal, pagination, held keys, bounce, loading and one-action A semantics");
}
