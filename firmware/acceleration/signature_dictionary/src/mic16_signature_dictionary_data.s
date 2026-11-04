/* 只读冻结几何参数，载入开销属于启动，不是每窗音频计算。 */
.section .rodata.m16_signature_dictionary,"a",%progbits
.balign 64
.global m16_signature_dictionary_values
.type m16_signature_dictionary_values, %object
m16_signature_dictionary_values:
.incbin "M:/build/parameters/signature_dictionary_20261022/values.bin"
.global m16_signature_dictionary_end
m16_signature_dictionary_end:
.size m16_signature_dictionary_values, .-m16_signature_dictionary_values
