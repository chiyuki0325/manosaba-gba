#include "story_vm.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace manosaba;
static int script(const char* suffix){for(int i=0;i<story_script_count;++i)if(std::strstr(story_scripts[i].path,suffix))return i;assert(false);return 0;}
static int entry(const char* id,int kind){for(int i=0;i<book_entry_count;++i)if(book_entries[i].kind==kind && !std::strcmp(book_entries[i].id,id))return i;assert(false);return 0;}
static int candidate(story_vm& vm,const char* id){for(int i=0;i<vm.inventory_count();++i)if(!std::strcmp(story_items[vm.item_id(i)].id,id))return i;return -1;}
static void inventory(story_vm& vm,bool person=false){
    for(int n=0;n<3000;++n){
        if(vm.state()==story_state::inventory)return;
        assert(vm.state()!=story_state::end && vm.state()!=story_state::failure);
        if(vm.state()==story_state::debate && vm.link_count())vm.object(0);
        else if(vm.state()==story_state::menu)
        {
            int choice=0;
            if(person)for(int i=0;i<vm.option_count();++i)
                if(std::strstr(vm.option(i).text,"目前没有证据") || std::strstr(vm.option(i).text,"知道证据的人物"))choice=i;
            vm.choose(choice);
        }
        else vm.advance();
    }
    assert(false);
}
int main(){
    story_vm vm;vm.start(script("Act01_Chapter01_Adv04"));
    assert(vm.book_version(entry("10-1",0))==1);
    assert(vm.book_version(entry("1-1",0))==0);
    for(int i=0;i<book_entry_count;++i)if(book_entries[i].kind==1)assert(vm.book_version(i)==1);
    vm.start(script("Act01_Chapter01_Trial14"));
    assert(vm.book_version(entry("1-1",0))==2);
    inventory(vm);assert(vm.inventory_kind()==0);
    assert(candidate(vm,"10-1")>=0); // The common pen is not a chapter-1-prefixed ID.
    int first=candidate(vm,"1-7");assert(first>=0);vm.choose_item(first);
    inventory(vm);int second=candidate(vm,"1-6");assert(second>=0);vm.choose_item(second);
    inventory(vm,true);assert(vm.inventory_kind()==1 && vm.inventory_count()==13);
    assert(candidate(vm,"Warden")==-1 && candidate(vm,"Jailer")==-1);
    assert(vm.book_version(entry("Warden",1))==0 && vm.book_version(entry("Jailer",1))==0);
    int person=candidate(vm,"Miria");assert(person>=0);vm.choose_item(person);
    assert(!std::strcmp(story_items[vm.chosen_item()].id,"Miria"));
    std::puts("PASS: original book entry versions, direct starts, two evidence branches, 13-person restriction, hidden entries and submitted ID");
}
