ENTRY(_start)
SECTIONS { . = 0x100000; .text : { *(.multiboot) *(.text) } . = 0x110000; .payload : { *(.payload) } }
