// Payload SHA256: 9defd7ae54f28151d1431dec911ad8ae8b3c46abafe49a7ae2d8b50a47520919
.section .rodata.graphics_bytes,"a",%progbits
.balign 4
.global _ZN8manosaba14graphics_bytesE
_ZN8manosaba14graphics_bytesE:
.ifdef MANOSABA_ACTOR_BAKED
.incbin "../build-actor-baked/graphics.bin"
.else
.incbin "../assets/graphics/graphics.bin"
.endif
