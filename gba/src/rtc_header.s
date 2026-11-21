@ Wonderful's default 0xC4-byte header otherwise puts its startup copy table
@ at GPIO 0xC8. Reserve all three RTC registers, including emulator writeback.
.section .header.pad.rtc,"a",%progbits
.balign 2
.space 8
