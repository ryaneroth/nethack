.setcpu "6502"

.export _nh_sd_init
.export _nh_sd_open_file
.export _nh_sd_open_dungeon
.export _nh_sd_read_byte
.export _nh_sd_file_size
.export _nh_sd_create_file
.export _nh_sd_write_sector
.export _nh_sd_finish_write
.export _nh_sd_delete_file
.export _nh_sd_generation
.export _nh_sd_touch

zp_sd_address = $40
zp_sd_currentsector = $42
zp_fat32_variables = $46
fat32_workspace = $0200

.segment "CODE"

_nh_sd_init:
        lda     sd_initialized
        bne     @ready
        lda     #0
        sta     sd_generation
        sta     sd_generation+1
        ; Port A only.  The driver's via_init would also make all of port B
        ; outputs; PB7 carries the $1700 6530's timer interrupt, and on a
        ; KIM-1 with PB7 jumpered to IRQ (A-15 to E-4) an output there could
        ; hold IRQ down.  The driver does not use port B.
        lda     #PORTA_OUTPUTPINS
        sta     DDRA
        jsr     sd_init
        bcs     @failed
        lda     #0
        sta     sd_stream_enabled
        jsr     fat32_init
        bcs     @failed
        lda     #1
        sta     sd_initialized
@ready:
        lda     #0
        ldx     #0
        rts
@failed:
        lda     #1
        ldx     #0
        rts

_nh_sd_open_file:
        sta     fat32_filenamepointer
        stx     fat32_filenamepointer+1
        jsr     sd_bump_generation
        jsr     fat32_openroot
        ldx     fat32_filenamepointer
        ldy     fat32_filenamepointer+1
        jsr     fat32_finddirent
        bcs     @not_found
        ldy     #28
        lda     (zp_sd_address),y
        sta     sd_file_size
        iny
        lda     (zp_sd_address),y
        sta     sd_file_size+1
        iny
        lda     (zp_sd_address),y
        sta     sd_file_size+2
        iny
        lda     (zp_sd_address),y
        sta     sd_file_size+3
        jsr     fat32_opendirent
        jsr     fat32_readnextsector
        bcs     @not_found
        lda     #0
        ldx     #0
        rts
@not_found:
        lda     #1
        ldx     #0
        rts

_nh_sd_open_dungeon:
        lda     #<dungeon_filename
        ldx     #>dungeon_filename
        jmp     _nh_sd_open_file

_nh_sd_read_byte:
        jsr     fat32_file_readbyte
        bcs     @eof
        ldx     #0
        rts
@eof:
        lda     #$FF
        ldx     #$FF
        rts

_nh_sd_file_size:
        sta     fat32_filenamepointer
        stx     fat32_filenamepointer+1
        ldy     #0
@copy_size:
        lda     sd_file_size,y
        sta     (fat32_filenamepointer),y
        iny
        cpy     #4
        bne     @copy_size
        rts

_nh_sd_create_file:
        sta     fat32_filenamepointer
        stx     fat32_filenamepointer+1
        ldy     #0
@copy_filename:
        lda     (fat32_filenamepointer),y
        sta     sd_filename,y
        iny
        cpy     #11
        bne     @copy_filename
        lda     #<sd_filename
        sta     fat32_filenamepointer
        lda     #>sd_filename
        sta     fat32_filenamepointer+1
        jsr     sd_bump_generation
        jsr     fat32_openroot
        ldx     fat32_filenamepointer
        ldy     fat32_filenamepointer+1
        jsr     fat32_finddirent
        bcs     @allocate
        jsr     fat32_deletefile
        bcs     @failed
@allocate:
        lda     #0
        sta     fat32_lastsector
        sta     fat32_lastsector+1
        sta     fat32_lastsector+2
        sta     fat32_lastsector+3
        lda     #1
        sta     fat32_bytesremaining
        lda     #0
        sta     fat32_bytesremaining+1
        sta     fat32_bytesremaining+2
        sta     fat32_bytesremaining+3
        jsr     fat32_allocatefile
        bcc     :+
        jmp     @failed
:
        jsr     fat32_openroot
        lda     #0
        sta     fat32_bytesremaining
        sta     fat32_bytesremaining+1
        sta     fat32_bytesremaining+2
        sta     fat32_bytesremaining+3
        jsr     fat32_writedirent
        bcs     @failed
        lda     #0
        sta     sd_file_size
        sta     sd_file_size+1
        sta     sd_file_size+2
        sta     sd_file_size+3
        lda     #0
        ldx     #0
        clc
        rts
@failed:
        lda     #1
        ldx     #0
        sec
        rts

_nh_sd_write_sector:
        sta     fat32_address
        stx     fat32_address+1
        jsr     sd_bump_generation
        lda     fat32_pendingsectors
        bne     @write
        jsr     fat32_extendfile        ; next cluster, linked in
        bcs     @write_failed
@write:
        jsr     fat32_writenextsector
        bcs     @write_failed
        lda     #0
        ldx     #0
        clc
        rts
@write_failed:
        lda     #1
        ldx     #0
        sec
        rts

_nh_sd_finish_write:
        sta     fat32_filenamepointer
        stx     fat32_filenamepointer+1
        jsr     sd_bump_generation
        ldy     #0
@copy_size:
        lda     (fat32_filenamepointer),y
        sta     fat32_bytesremaining,y
        iny
        cpy     #4
        bne     @copy_size
        jsr     fat32_openroot
        ldx     #<sd_filename
        ldy     #>sd_filename
        jsr     fat32_finddirent
        bcs     @finish_failed
        ldy     #28
        lda     fat32_bytesremaining
        sta     (zp_sd_address),y
        iny
        lda     fat32_bytesremaining+1
        sta     (zp_sd_address),y
        iny
        lda     fat32_bytesremaining+2
        sta     (zp_sd_address),y
        iny
        lda     fat32_bytesremaining+3
        sta     (zp_sd_address),y
        jsr     fat32_writecurrentsector
        bcs     @finish_failed
        lda     #0
        ldx     #0
        clc
        rts
@finish_failed:
        lda     #1
        ldx     #0
        sec
        rts

_nh_sd_delete_file:
        sta     fat32_filenamepointer
        stx     fat32_filenamepointer+1
        jsr     sd_bump_generation
        jsr     fat32_openroot
        ldx     fat32_filenamepointer
        ldy     fat32_filenamepointer+1
        jsr     fat32_finddirent
        bcs     @delete_failed
        jsr     fat32_deletefile
        bcs     @delete_failed
        lda     #0
        ldx     #0
        clc
        rts
@delete_failed:
        lda     #1
        ldx     #0
        sec
        rts

_nh_sd_generation:
        lda     sd_generation
        ldx     sd_generation+1
        rts

_nh_sd_touch:
        jsr     sd_bump_generation
        lda     #0
        ldx     #0
        rts

sd_bump_generation:
        inc     sd_generation
        bne     :+
        inc     sd_generation+1
:
        rts

.include "vendor/sdcard6502/src/hwconfig.s"
.include "vendor/sdcard6502/src/libsd.s"
.include "vendor/sdcard6502/src/libfat32.s"

.segment "RODATA"
dungeon_filename:
        .byte   "NHDUNGE TXT"

.segment "BSS"
sd_file_size:
        .res    4
sd_generation:
        .res    2
sd_filename:
        .res    11
sd_initialized:
        .res    1
