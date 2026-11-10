# Einsprung des App-Booters: eigener Stack am Ende des reservierten Bereichs (kReservedEnd - 0x10),
# weit weg von Booter-Code (0x93300000) und argv-Block (0x93300800).

    .section .text.crt0, "ax"
    .globl _start
_start:
    lis     1, 0x933D
    ori     1, 1, 0xFFF0
    li      0, 0
    stw     0, 0(1)
    bl      booter_main
1:  b       1b
