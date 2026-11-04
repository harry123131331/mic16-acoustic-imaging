.section .rodata.cached_reference,"a",%progbits
.balign 64
.global _binary_cached_reference_f32_bin_start
.global _binary_cached_reference_f32_bin_end
_binary_cached_reference_f32_bin_start:
.incbin "cached_reference_f32.bin"
_binary_cached_reference_f32_bin_end:
