// Payload SHA256: 72d18df7f2e3a996b891d7ac8d2bddda8460b3f9482f4d898f92303da8733465
.section .rodata.graphics_bytes,"a",%progbits
.balign 4
.global _ZN8manosaba14graphics_bytesE
_ZN8manosaba14graphics_bytesE:
.ifdef MANOSABA_ACTOR_BAKED
.incbin "../build-actor-baked/graphics.bin"
.else
.incbin "../assets/graphics/graphics.bin"
.endif
