#include "save_rtc.h"
namespace manosaba {
namespace {
volatile uint16_t* const pins=reinterpret_cast<volatile uint16_t*>(0x080000c4);
volatile uint16_t* const direction=reinterpret_cast<volatile uint16_t*>(0x080000c6);
volatile uint16_t* const enabled=reinterpret_cast<volatile uint16_t*>(0x080000c8);
void read(unsigned command,uint8_t* bytes,int count)
{
    *enabled=1;*direction=7;*pins=1;*pins=5;
    for(int bit=7;bit>=0;--bit)
    {
        unsigned data=4|((command>>bit&1)<<1);
        *pins=uint16_t(data);*pins=uint16_t(data);*pins=uint16_t(data);*pins=uint16_t(data|1);
    }
    *direction=5;
    for(int byte=0;byte<count;++byte)
    {
        bytes[byte]=0;
        for(int bit=0;bit<8;++bit)
        {*pins=4;*pins=4;*pins=4;*pins=4;*pins=5;bytes[byte]|=uint8_t((*pins>>1&1)<<bit);}
    }
    *pins=1;*pins=1;*enabled=0;
}
int decimal(uint8_t value){return (value&15)<10 && (value>>4)<10 ? (value&15)+(value>>4)*10 : -1;}
}
save_timestamp save_rtc_now()
{
    uint8_t status=0,bytes[7];read(0x63,&status,1);
    if(status&0x95)return {};
    read(0x65,bytes,7);
    int year=decimal(bytes[0]),month=decimal(bytes[1]),day=decimal(bytes[2]),week=decimal(bytes[3]);
    int hour=decimal(bytes[4]&0x3f),minute=decimal(bytes[5]),second=decimal(bytes[6]);
    if(!(status&0x40)){if(hour<1 || hour>12)return {};hour=hour%12+((bytes[4]&0x80) ? 12 : 0);}
    static constexpr uint8_t days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(year<0 || month<1 || month>12 || day<1 || day>days[month-1]+int(month==2 && year%4==0) || week<0 || week>6 || hour<0 || hour>23 || minute<0 || minute>59 || second<0 || second>59)return {};
    return {uint8_t(year),uint8_t(month),uint8_t(day),uint8_t(hour),uint8_t(minute),uint8_t(second),uint8_t(week),1};
}
}
