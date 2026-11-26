.setcpu "6502"

.export _kim_getc
.export _kim_putc

.segment "CODE"

_kim_getc:
        jsr     $1E5A
        rts

_kim_putc:
        jsr     $1EA0
        rts
