/* App-Booter: liegt bei kBooterAddr; der Kopf des Chainloaders folgt bei 0x933007F0, also hoechstens 0x7F0 Byte. */
OUTPUT_ARCH(powerpc:common)
ENTRY(_start)

SECTIONS
{
    . = 0x93300000;
    .text : { *(.text.crt0) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) *(.sdata .sdata.*) }
    .bss : { *(.bss .bss.*) *(.sbss .sbss.*) *(COMMON) }
    __booter_end = .;
    /DISCARD/ : { *(.comment) *(.eh_frame) *(.note*) *(.gnu.attributes) }
}

ASSERT(__booter_end - 0x93300000 <= 0x7F0, "App-Booter groesser als 0x7F0 Byte")
