// Payload SHA256: aefd292b0611b1255983bb412e62a7d7ca635538dd7e04f9f7fb266e3d13cbe6
.section .rodata.graphics_bytes,"a",%progbits
.balign 4
.global _ZN8manosaba14graphics_bytesE
_ZN8manosaba14graphics_bytesE:
.ifdef MANOSABA_ACTOR_BAKED
.incbin "../build-actor-baked/graphics.bin"
.else
.incbin "../assets/graphics/graphics.bin"
.endif
