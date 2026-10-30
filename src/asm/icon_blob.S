    .section .rodata
    .balign 16
    .global ps5tm_icon_elf
ps5tm_icon_elf:
    .incbin ICON_BLOB_PATH
    .global ps5tm_icon_elf_end
ps5tm_icon_elf_end:
    .byte 0
    .section .note.GNU-stack,"",@progbits
