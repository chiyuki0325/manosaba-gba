/* Host-only route search using the production VM and compiled script data.
 * Private state is inspected only for cycle detection; no game code is linked
 * into the native input driver and no VM state is injected into the ROM. */
#include "story_format.h"
#include "story_layout.h"
#include "witch_book_data.h"
#include "music_command.h"
#define private public
#include "story_vm.h"
#undef private
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>
using namespace manosaba;

enum
{
    advance_action,
    skip_action,
    object_action,
    choice_action,
    item_action
};
struct step
{
    int script, state, action, arg, kind;
    unsigned pc;
    std::string text;
};
static std::vector<step> route;
static std::unordered_set<std::string> seen;
static int entry, nodes, limit = 150000;
static unsigned longest;
static story_vm terminal;
static bool chapter_scope;
static std::string chapter_prefix;
static std::string key(const story_vm& vm)
{
    std::string out;
    auto add = [&](const auto& value) { out.append(reinterpret_cast<const char*>(&value), sizeof(value)); };
    add(vm._script);
    add(vm._pc);
    add(vm._state);
    add(vm._trial_flags);
    add(vm._chosen_item);
    add(vm._flags);
    add(vm._area);
    add(vm._direct);
    add(vm._return_valid);
    if (vm._return_valid)
    {
        add(vm._return.script);
        add(vm._return.pc);
    }
    out.append(reinterpret_cast<const char*>(vm._book_versions), sizeof(vm._book_versions));
    return out;
}
static bool finish(const story_vm& vm)
{
    if (vm.script() != entry)
    {
        if (vm.trial())
            return vm.script() > entry &&
                   (!chapter_scope ||
                    std::strncmp(story_scripts[vm.script()].path, chapter_prefix.c_str(), chapter_prefix.size()));
        return std::strstr(story_scripts[vm.script()].path, "_Adv") != nullptr;
    }
    return vm.state() == story_state::end && !vm.bad_end();
}
static void record(const story_vm& vm, int action, int arg = 0)
{
    const char* context = action == choice_action ? vm.option(arg).text
                          : action == item_action ? story_items[vm.item_id(arg)].label
                                                  : vm.text();
    route.push_back({vm.script(), int(vm.state()), action, arg, vm.inventory_kind(), vm.position(), context});
}
static bool search(story_vm& vm, int depth = 0)
{
    if (depth > 500 || ++nodes > limit)
        return false;
    size_t begin = route.size();
    for (int work = 0; work < 4000; ++work)
    {
        if (vm.hp() < 10 || (vm.script() != entry && !vm.trial() && !finish(vm)))
            break;
        if (finish(vm))
        {
            terminal = vm;
            return true;
        }
        if (!seen.insert(key(vm)).second)
            break;
        if (route.size() > longest)
            longest = unsigned(route.size());
        if (vm.state() == story_state::debate && !vm.link_count())
        {
            // A statement without an objection has no branch to search.
            record(vm, skip_action);
            vm.skip_debate();
            continue;
        }
        if (vm.state() == story_state::debate || vm.state() == story_state::menu ||
            vm.state() == story_state::inventory)
        {
            size_t branch = route.size();
            int count = vm.state() == story_state::debate ? vm.link_count() + 1
                        : vm.state() == story_state::menu ? vm.option_count()
                                                          : vm.inventory_count();
            for (int i = 0; i < count; ++i)
            {
                auto next = std::make_unique<story_vm>(vm);
                if (vm.state() == story_state::debate)
                {
                    if (i < vm.link_count())
                    {
                        record(vm, object_action, i);
                        next->object(i);
                    }
                    else
                    {
                        record(vm, skip_action);
                        next->skip_debate();
                    }
                }
                else if (vm.state() == story_state::menu)
                {
                    if (vm.objection_menu() && (vm.option(i).value & 128))
                        continue;
                    record(vm, choice_action, i);
                    next->choose(i);
                }
                else
                {
                    record(vm, item_action, i);
                    next->choose_item(i);
                }
                if (search(*next, depth + 1))
                    return true;
                route.resize(branch);
            }
            break;
        }
        if (vm.state() == story_state::text || vm.state() == story_state::pause)
            record(vm, advance_action);
        if (vm.state() == story_state::camera)
            vm.tick_camera(vm.camera_wait());
        vm.advance();
    }
    route.resize(begin);
    return false;
}
#ifndef MANOSABA_TRIAL_ROUTE_LIBRARY
static void quote(const char* text)
{
    putchar('"');
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p)
    {
        if (*p == '"' || *p == '\\')
        {
            putchar('\\');
            putchar(*p);
        }
        else if (*p < 32)
            printf("\\u%04x", *p);
        else
            putchar(*p);
    }
    putchar('"');
}
int main(int argc, char** argv)
{
    if (argc > 1)
        limit = std::atoi(argv[1]);
    chapter_scope = argc > 2 && !std::strcmp(argv[2], "chapter");
    for (entry = story_trial_first; entry < story_trial_first + story_trial_count; ++entry)
    {
        std::string path = story_scripts[entry].path;
        if (chapter_scope && path.substr(path.size() - 8) != "_Trial00")
            continue;
        if (argc > 3)
        {
            bool selected = false;
            for (int i = 3; i < argc; ++i)
                selected |= path.find(argv[i]) != std::string::npos;
            if (!selected)
                continue;
        }
        chapter_prefix = path.substr(0, path.rfind("_Trial")) + "_Trial";
        route.clear();
        seen.clear();
        nodes = 0;
        longest = 0;
        story_vm vm{};
        vm.start(entry);
        bool solved = search(vm);
        printf("{\"entry\":%d,\"offset\":%d,\"path\":", entry, entry - story_trial_first);
        quote(story_scripts[entry].path);
        printf(",\"solved\":%s,\"nodes\":%d,\"visited\":%zu,\"longest\":%u,\"steps\":[", solved ? "true" : "false",
               nodes, seen.size(), longest);
        for (size_t i = 0; i < route.size(); ++i)
        {
            const auto& s = route[i];
            if (i)
                putchar(',');
            printf("{\"script\":%d,\"pc\":%u,\"state\":%d,\"action\":%d,\"arg\":%d,\"kind\":%d,\"text\":", s.script,
                   s.pc, s.state, s.action, s.arg, s.kind);
            quote(s.text.c_str());
            putchar('}');
        }
        printf("]");
        if (solved)
            printf(",\"terminal\":{\"script\":%d,\"pc\":%u,\"state\":%d}", terminal.script(), terminal.position(),
                   int(terminal.state()));
        puts("}");
        fflush(stdout);
    }
}
#endif
