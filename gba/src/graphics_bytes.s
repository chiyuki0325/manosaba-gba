// Payload SHA256: d2f7bd25632261706e03358cd030ac8e8bf2dd1756244db583eec40a0f5b52da
.section .rodata.graphics_bytes,"a",%progbits
.balign 4
.global _ZN8manosaba14graphics_bytesE
_ZN8manosaba14graphics_bytesE:
.ifdef MANOSABA_ACTOR_BAKED
.incbin "../build-actor-baked/graphics.bin"
.else
.incbin "../assets/graphics/graphics.bin"
.endif
