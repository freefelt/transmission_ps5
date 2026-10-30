    .section .rodata
    .balign 16
    .global ps5tm_daemon_elf
ps5tm_daemon_elf:
    .incbin BLOB_PATH
    .global ps5tm_daemon_elf_end
ps5tm_daemon_elf_end:
    .byte 0
    .section .note.GNU-stack,"",@progbits
