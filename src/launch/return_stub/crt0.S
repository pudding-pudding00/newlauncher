# Rückkehr-Stub (M20, Spec 4): libogc springt bei exit() nach 0x80001800, wenn bei +4 "STUBHAXX" steht.
# Interrupts aus, eigener Stack in MEM2 (wie der App-Booter), dann stub_main.

    .section .text.crt0, "ax"
    .globl _start
_start:
    b       1f
    .ascii  "STUBHAXX"
    # Lücke bis Offset 0xB0, nie ausgeführt: Dolphin hängt an 0x800018A8 (Gecko-Einsprung) fest eine
    # HLE-Funktion, die bei jedem Durchlauf das Wort bei 0x80001800 hochzählt und so den Stub beschädigt.
    .org    0xB0
1:  mfmsr   3
    rlwinm  3, 3, 0, 17, 15     # MSR[EE] löschen
    mtmsr   3
    isync
    lis     1, 0x933D
    ori     1, 1, 0xFFF0
    li      0, 0
    stw     0, 0(1)
    bl      stub_main
2:  b       2b
