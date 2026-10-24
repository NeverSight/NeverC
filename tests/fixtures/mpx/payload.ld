ENTRY(stage64)
SECTIONS { . = 0x110000; .text : { *(.text.start) *(.text*) } .rodata : { *(.rodata*) } .data : { *(.data*) } .bss : { __bss_start = .; *(.bss*) *(COMMON) __bss_end = .; } /DISCARD/ : { *(.eh_frame*) *(.comment*) *(.note*) } }
