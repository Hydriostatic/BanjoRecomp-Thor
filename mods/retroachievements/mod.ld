/* Mod code and data are relocated by the mod loader, so the base address is only nominal. */
MEMORY {
    modram : ORIGIN = 0x81000000, LENGTH = 64M
}

SECTIONS {
    .ctors         : { *(.ctors*) *(.init_array*) } >modram
    .dtors         : { *(.dtors*)                 } >modram
    .text          : { *(.text*)                  } >modram
    .recomp_patch  : { *(.recomp_patch*)          } >modram
    .recomp_force_patch : { *(.recomp_force_patch*) } >modram
    .recomp_export : { *(.recomp_export*)         } >modram
    .recomp_event  : { *(.recomp_event*)          } >modram
    .rodata        : { *(.rodata*)                } >modram
    .data          : { *(.data*)                  } >modram
    .bss  (NOLOAD) : { *(.bss*) *(COMMON)         } >modram

    /* .recomp_import.*, .recomp_hook.* and .recomp_callback.* sections aren't listed so that the
       linker keeps each one under its own name; RecompModTool reads the names. */

    .reloc 0 : { *(.reloc*) }
    .symtab 0 : { *(.symtab) }
    .strtab 0 : { *(.strtab) }
    .shstrtab 0 : { *(.shstrtab) }

    /DISCARD/ : { *(.MIPS.*) *(.comment) *(.mdebug.*) *(.pdr) *(.reginfo) *(.eh_frame) *(.gnu.attributes) }
}
