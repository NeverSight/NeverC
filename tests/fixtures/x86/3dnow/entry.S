.section .text.start,"ax"
.global stage64
stage64:
.ifdef I386
 .code32
 mov $0x1f0000, %esp
.else
 .code64
 mov $0x1f0000, %rsp
.endif
 mov $__bss_start, %edi
 mov $__bss_end, %ecx
 sub %edi, %ecx
 xor %eax, %eax
 rep stosb
 call guest_main
 ud2
.section .note.GNU-stack,"",@progbits
