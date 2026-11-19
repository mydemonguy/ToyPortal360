/* The script `xex-link link-script --manifest tools/forge/xex.toml` writes,
 * reproduced here so the image can be linked and checked without running
 * xex-link. Code starts one 4 KB page above the load address 0x9B000000. */
ENTRY(_start)

SECTIONS {
    . = 0x9b001000;
    .text : {
        *(.text._start)
        *(.text .text.*)
    }
    . = ALIGN(4);
    .xthunks : { *(.xthunks .xthunks.*) }

    . = ALIGN(0x1000);
    .rodata : { *(.rodata .rodata.*) }

    . = ALIGN(0x1000);
    .init_array : { KEEP(*(.init_array .init_array.*)) }
    .data : { *(.data .data.*) *(.sdata .sdata.*) }
    .xvars : { *(.xvars .xvars.*) }

    . = ALIGN(0x1000);
    .bss : { *(.bss .bss.*) *(.sbss .sbss.*) *(COMMON) }

    . = ALIGN(4);
    .xexports : { KEEP(*(.xexports)) }

    /DISCARD/ : { *(.eh_frame) *(.eh_frame_hdr) *(.comment) *(.note .note.*) }
}
