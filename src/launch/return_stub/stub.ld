/* Rückkehr-Stub: Code ab 0x80001800, höchstens 0x1000 Byte; danach folgt der Parameterblock bei 0x80002800. */
OUTPUT_ARCH(powerpc:common)
ENTRY(_start)

SECTIONS
{
    . = 0x80001800;
    .text : { *(.text.crt0) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) *(.sdata .sdata.*) }
    .bss : { *(.bss .bss.*) *(.sbss .sbss.*) *(COMMON) }
    __stub_end = .;
    /DISCARD/ : { *(.comment) *(.eh_frame) *(.note*) *(.gnu.attributes) }
}

ASSERT(__stub_end - 0x80001800 <= 0x1000, "Rueckkehr-Stub groesser als 0x1000 Byte")
ASSERT(SIZEOF(.bss) == 0, "Rueckkehr-Stub darf kein BSS haben (wird nicht mitkopiert)")
