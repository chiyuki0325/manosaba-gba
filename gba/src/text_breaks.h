#pragma once
namespace manosaba {
inline bool break_word(unsigned char c)
{return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_';}
// Source soft breaks between Latin words still separate those words when folded.
inline bool soft_break_space(const char* at)
{return break_word(static_cast<unsigned char>(at[-1])) && break_word(static_cast<unsigned char>(at[1]));}
}
