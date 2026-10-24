.section .multiboot,"a"
.balign 4
.long 0x1badb002, 0, -0x1badb002
.section .text
.code32
.global _start
_start:
 cli
 mov $0x1f0000, %esp
 lgdt gdt_pointer
 mov $0x10, %ax
 mov %ax, %ds
 mov %ax, %es
 mov %ax, %ss
 mov $0x101000, %edi
 xor %eax, %eax
 mov $4096, %ecx
 rep stosl
 movl $0x102003, 0x101000
 movl $0x103003, 0x102000
 movl $0x104003, 0x102020
 mov $0x103000, %edi
 mov $0x83, %eax
 mov $512, %ecx
1: mov %eax, (%edi)
 add $0x200000, %eax
 add $8, %edi
 loop 1b
 movl $0x200083, 0x104000
 mov %cr4, %eax
 or $0x40620, %eax
 mov %eax, %cr4
 xor %ecx, %ecx
 mov $0x1b, %eax
 xor %edx, %edx
 xsetbv
 mov $0x101000, %eax
 mov %eax, %cr3
 mov $0xc0000080, %ecx
 rdmsr
 or $0x100, %eax
 wrmsr
 mov %cr0, %eax
 or $0x80000000, %eax
 mov %eax, %cr0
 ljmp $0x18, $0x110000
.balign 8
gdt:
 .quad 0, 0x00cf9a000000ffff, 0x00cf92000000ffff, 0x00af9a000000ffff
gdt_pointer:
 .word 31
 .long gdt
.section .payload,"a"
.incbin "payload.bin"
