; SD card files for NetHack on the KIM-1: a thin layer over the sdcard6502
; driver (vendor/sdcard6502) for the C code.  One file is open at a time.
;
;   u8  nh_sd_init(void)                 card and FAT32 volume; 0 = ready
;   u8  nh_sd_open_file(const char *)    open a file to read; 0 = opened
;   int nh_sd_read_byte(void)            the next byte, or -1 at the end
;   u8  nh_sd_create_file(const char *)  create or replace a file to write
;   u8  nh_sd_write(const void *)        write nh_sd_count bytes from it
;   u8  nh_sd_close(void)                finish writing (or reading)
;   u8  nh_sd_delete_file(const char *)  delete a file
;
; Names are paths such as "LEVEL01.DAT".  The u8 results are 0 for success
; or the driver's error code.

.setcpu "6502"

.export _nh_sd_init
.export _nh_sd_open_file
.export _nh_sd_read_byte
.export _nh_sd_create_file
.export _nh_sd_write
.export _nh_sd_close
.export _nh_sd_delete_file
.export _nh_sd_count

fat32_workspace = $0200         ; page aligned, 512 bytes

RBUF = 64                       ; read-ahead for nh_sd_read_byte

.segment "CODE"

_nh_sd_init:
        lda     sd_ready
        bne     @ready
        ; Port A only: port B's PB7 carries the $1700 6530's timer
        ; interrupt (when jumpered to IRQ), and must stay an input.
        lda     #PORTA_OUTPUTPINS
        sta     DDRA
        jsr     sd_init
        bcs     @failed
        jsr     fat32_init
        bcs     @failed
        lda     #1
        sta     sd_ready
@ready:
        lda     #0
        ldx     #0
        rts
@failed:
        lda     #1
        ldx     #0
        rts

_nh_sd_open_file:
        ldy     #FAT32_READ
open:
        sta     fat32_ptr1
        stx     fat32_ptr1+1
        lda     #0
        sta     rlen
        sta     rpos
        lda     #<handle
        ldx     #>handle
        jsr     fat32_open
        ldx     #0
        rts

_nh_sd_create_file:
        ldy     #FAT32_WRITE | FAT32_CREATE | FAT32_TRUNC
        jmp     open

_nh_sd_read_byte:
        ldy     rpos
        cpy     rlen
        bcc     @have
        ; refill the buffer
        lda     #<rbuf
        sta     fat32_ptr1
        lda     #>rbuf
        sta     fat32_ptr1+1
        lda     #RBUF
        sta     fat32_len
        lda     #0
        sta     fat32_len+1
        lda     #<handle
        ldx     #>handle
        jsr     fat32_read
        bcs     @eof
        lda     fat32_len
        beq     @eof
        sta     rlen
        ldy     #0
@have:
        lda     rbuf,y
        iny
        sty     rpos
        ldx     #0
        rts
@eof:
        lda     #$ff
        tax
        rts

_nh_sd_write:
        sta     fat32_ptr1
        stx     fat32_ptr1+1
        lda     _nh_sd_count
        sta     fat32_len
        lda     _nh_sd_count+1
        sta     fat32_len+1
        lda     #<handle
        ldx     #>handle
        jsr     fat32_write
        ldx     #0
        rts

_nh_sd_close:
        lda     #<handle
        ldx     #>handle
        jsr     fat32_close
        ldx     #0
        rts

_nh_sd_delete_file:
        sta     fat32_ptr1
        stx     fat32_ptr1+1
        jsr     fat32_delete
        ldx     #0
        rts

.include "vendor/sdcard6502/src/hwconfig.s"
.include "vendor/sdcard6502/src/libsd.s"
.include "vendor/sdcard6502/src/libfat32.s"

.segment "BSS"
handle:         .res    FAT32_FILESIZE
rbuf:           .res    RBUF
rlen:           .res    1
rpos:           .res    1
sd_ready:       .res    1
_nh_sd_count:   .res    2
