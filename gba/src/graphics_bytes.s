// Payload SHA256: 66f01f56580fac304e9b4552a349575a71166198fd4fca33ee914e8d8f0bff75
.section .rodata.graphics_bytes,"a",%progbits
.balign 4
.global _ZN8manosaba14graphics_bytesE
_ZN8manosaba14graphics_bytesE:
.ifdef MANOSABA_ACTOR_BAKED
.incbin "../build-actor-baked/graphics.bin"
.else
.incbin "../assets/graphics/graphics.bin"
.endif
