/*
 * SD file-layer regression test for the KIM-1 build.
 *
 * Creates ten files, re-creates several of them out of order (the
 * delete-and-reuse pattern the game uses for level and save files), then
 * checks that every file is still present with the right size and contents.
 * Prints "SD FILES PASSED" or the first failure.
 */

typedef unsigned char u8;
typedef unsigned int u16;

extern void __fastcall__ kim_putc(u8 ch);
extern u8 __fastcall__ nh_sd_init(void);
extern u8 __fastcall__ nh_sd_open_file(const char *path);
extern int __fastcall__ nh_sd_read_byte(void);
extern u8 __fastcall__ nh_sd_create_file(const char *path);
extern u8 __fastcall__ nh_sd_write(const void *buffer);
extern u8 __fastcall__ nh_sd_close(void);
extern u16 nh_sd_count;

#define NFILES 10
#define SIZE 1734 /* the size of a level file: four sectors */

static u8 sect[512];
static char name[] = "TESTF00.DAT";
static u8 version[NFILES];

static void ps(const char *s)
{
    while (*s) {
        kim_putc((u8)*s++);
    }
}

static void fail(const char *what, u8 file)
{
    ps("SD FILES FAILED: ");
    ps(what);
    ps(" file ");
    kim_putc((u8)('0' + file));
    kim_putc(13);
    kim_putc(10);
}

static u8 pattern(u8 file, u16 i)
{
    return (u8)(file * 37 + version[file] * 101 + i * 7 + (i >> 8));
}

static void set_name(u8 file)
{
    name[6] = (char)('0' + file);
}

static u8 write_file(u8 file)
{
    u16 i;
    u16 n = 0;

    set_name(file);
    if (nh_sd_create_file(name)) {
        return 0;
    }
    for (i = 0; i < SIZE; ++i) {
        sect[n++] = pattern(file, i);
        if (n == sizeof(sect)) {
            nh_sd_count = n;
            if (nh_sd_write(sect)) {
                return 0;
            }
            n = 0;
        }
    }
    if (n) {
        nh_sd_count = n;
        if (nh_sd_write(sect)) {
            return 0;
        }
    }
    return nh_sd_close() == 0;
}

static u8 check_file(u8 file)
{
    u16 i;
    int c;

    set_name(file);
    if (nh_sd_open_file(name)) {
        fail("missing", file);
        return 0;
    }
    for (i = 0; i < SIZE; ++i) {
        c = nh_sd_read_byte();
        if (c < 0) {
            fail("short", file);
            return 0;
        }
        if ((u8)c != pattern(file, i)) {
            fail("wrong data in", file);
            return 0;
        }
    }
    if (nh_sd_read_byte() >= 0) {
        fail("long", file);
        return 0;
    }
    return 1;
}

int main(void)
{
    static const u8 rewrite[] = {2, 4, 0, 9, 4, 5, 2, 7, 1, 2};
    u8 i;

    if (nh_sd_init()) {
        ps("SD FILES FAILED: no card\r\n");
        return 0;
    }
    for (i = 0; i < NFILES; ++i) {
        if (!write_file(i)) {
            fail("cannot create", i);
            return 0;
        }
    }
    for (i = 0; i < sizeof(rewrite); ++i) {
        ++version[rewrite[i]];
        if (!write_file(rewrite[i])) {
            fail("cannot re-create", rewrite[i]);
            return 0;
        }
    }
    for (i = 0; i < NFILES; ++i) {
        if (!check_file(i)) {
            return 0;
        }
    }
    ps("SD FILES PASSED\r\n");
    return 0;
}
