// Host SRAM model: interrupt writes at each byte and cold-mount the result.
#include "save_store.h"
#include "sram_driver.h"
#include <array>
#include <cstdio>
#include <cassert>
#include <cstring>
#include <stdexcept>
using namespace manosaba;
static std::array<uint8_t,32768> ram;
static unsigned writes=0,cut_at=~0u;
namespace manosaba::sram_driver {
void read(unsigned at,void* out,unsigned n){assert(at+n<=ram.size());std::memcpy(out,ram.data()+at,n);}
void write(unsigned at,const void* in,unsigned n){assert(at+n<=ram.size());for(unsigned i=0;i<n;++i){if(writes==cut_at)throw 1;ram[at+i]=static_cast<const uint8_t*>(in)[i];++writes;}}
}
static void finish(save_store& s){for(int n=0;n<20000 && s.busy();++n)s.step();assert(!s.busy() && !s.failed());}
static void write_record(save_store& s,int slot,uint8_t fill,unsigned n){uint8_t page[save_page_size];std::memset(page,fill,sizeof(page));save_metadata m{};m.script=slot;assert(s.begin_write(slot,page,n,0,m));finish(s);}
int main(){ram.fill(255);save_store s;assert(s.mount(17));for(int slot=0;slot<=save_global_slot;++slot)write_record(s,slot,slot+1,slot==save_global_slot ? 120 : slot==save_retry_slot ? 1200 : 2816);
uint8_t data[3072];for(int slot=0;slot<=save_global_slot;++slot){assert(s.begin_read(slot,data));finish(s);assert(data[256]==slot+1);}
auto baseline=ram;for(int target: {3,9,10}){
 ram=baseline;writes=0;save_store x;assert(x.mount(17));write_record(x,target,99,target==10 ? 120 : target==9 ? 1200 : 2816);unsigned count=writes;
 for(unsigned cut=0;cut<=count;++cut){ram=baseline;writes=0;cut_at=cut;try{save_store w;w.mount(17);write_record(w,target,99,target==10 ? 120 : target==9 ? 1200 : 2816);}catch(int){}cut_at=~0u;save_store r;assert(r.mount(17));assert(r.begin_read(target,data));finish(r);uint8_t value=data[256];assert(value==target+1 || value==99);for(unsigned i=0;i<r.length();++i)assert(data[256+i]==value);}
 printf("power cuts slot=%d bytes=%u PASS\n",target,count);
}
ram=baseline;save_store r;r.mount(17);write_record(r,2,55,2816);assert(r.begin_read(2,data));finish(r);assert(data[256]==55);
assert(!r.begin_write(0,data,2817,0,{}));assert(!r.begin_write(9,data,1281,0,{}));save_store incompatible;incompatible.mount(18);assert(incompatible.status(0)==save_slot_status::incompatible);assert(incompatible.status(10)==save_slot_status::valid);puts("PASS: 32768B SRAM, all ten slots, full 3KiB records, compact global pages, scratch recovery at every write byte, and size limits");}
