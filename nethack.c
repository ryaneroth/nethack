/*
 * NetHack for the KIM-1.
 *
 * A compact roguelike written for a 6502 with 32 KB of expansion RAM,
 * following NetHack's rules and flavour: rooms and corridors, a pet,
 * unidentified potions, scrolls and wands, hunger, traps, Elbereth,
 * prayer, and the Amulet of Yendor at the bottom of the dungeon.
 *
 * Serial I/O goes through the KIM-1 monitor (INCH/OUTCH).  When an
 * sdcard6502 FAT32 card is present, levels you leave are written to it
 * (as NetHack does with its level files), the game can be saved, and the
 * help text is read from NHHELP.TXT.
 */

#include <string.h>

typedef unsigned char u8;
typedef signed char s8;
typedef unsigned int u16;

extern u8 __fastcall__ kim_getc(void);
extern void __fastcall__ kim_putc(u8 ch);
extern u8 __fastcall__ nh_sd_init(void);
extern u8 __fastcall__ nh_sd_open_file(const char *name83);
extern int __fastcall__ nh_sd_read_byte(void);
extern u8 __fastcall__ nh_sd_create_file(const char *name83);
extern u8 __fastcall__ nh_sd_write_sector(const void *buffer);
extern u8 __fastcall__ nh_sd_finish_write(unsigned long *size);
extern u8 __fastcall__ nh_sd_delete_file(const char *name83);

#define MAP_W 64
#define MAP_H 21
#define NROOM 9
#define MAXOBJ 40
#define MAXMON 16
#define NINV 20
#define MAXD 10
#define MAXXL 12
#define NONE 255
#define MAGIC 0x4E02

#ifndef BUILD_ID
#define BUILD_ID 1
#endif

/* The RAM image starts with this stamp so the ROM can tell whether the
 * matching NETHACK.BIN is already in memory. */
#pragma rodata-name (push, "RHEADER")
const u16 ram_stamp = BUILD_ID;
#pragma rodata-name (pop)

#pragma rodata-name (push, "RRODATA")

/* Terrain, in the low five bits of a map cell. */
#define T_STONE 0
#define T_HWALL 1
#define T_VWALL 2
#define T_FLOOR 3
#define T_CORR 4
#define T_DOORWAY 5
#define T_ODOOR 6
#define T_CDOOR 7
#define T_UP 8
#define T_DN 9
#define T_TRAP 10
#define NTRAP 5
#define TM 0x1F
#define F_HID 0x20
#define F_LIT 0x40
#define F_SEEN 0x80

static const char tchar[] = " -|.#.-+<>^^^^^";
static const char *const trapname[NTRAP] = {
    "a teleportation trap", "a trap door", "an arrow trap",
    "a bear trap", "a sleeping gas trap"
};

/* Object classes. */
#define C_WEP 0
#define C_ARM 1
#define C_POT 2
#define C_SCR 3
#define C_WAN 4
#define C_FOOD 5
#define C_GOLD 6
#define C_AMU 7
static const char csym[] = ")[!?/%$\"";

#define O_LSWORD 4
#define O_LEATHER 6
#define O_HELMET 10
#define O_SHIELD 11
#define O_PHEAL 12
#define O_PXHEAL 13
#define O_PGAIN 14
#define O_PCONF 15
#define O_PSLEEP 16
#define O_PSPEED 17
#define O_SIDENT 18
#define O_SENCHW 19
#define O_SENCHA 20
#define O_STELE 21
#define O_SMAP 22
#define O_SREMC 23
#define O_WSTRIKE 24
#define O_WSLEEP 25
#define O_WDIG 26
#define O_RATION 27
#define O_APPLE 28
#define O_CORPSE 29
#define O_GOLD 30
#define O_AMULET 31
#define NOBJ 32

typedef struct {
    const char *name;
    u8 cls;
    u8 val;   /* weapon damage die, armor AC, food nutrition / 10 */
    u8 prob;  /* percent chance among random objects */
} ODef;

static const ODef od[NOBJ] = {
    {"strange object", C_WEP, 0, 0},
    {"dagger", C_WEP, 4, 4},
    {"short sword", C_WEP, 6, 3},
    {"mace", C_WEP, 6, 3},
    {"long sword", C_WEP, 8, 2},
    {"two-handed sword", C_WEP, 12, 1},
    {"leather armor", C_ARM, 2, 4},
    {"ring mail", C_ARM, 3, 3},
    {"chain mail", C_ARM, 5, 2},
    {"plate mail", C_ARM, 7, 1},
    {"helmet", C_ARM, 1, 3},
    {"small shield", C_ARM, 1, 3},
    {"healing", C_POT, 0, 6},
    {"extra healing", C_POT, 0, 3},
    {"gain level", C_POT, 0, 2},
    {"confusion", C_POT, 0, 4},
    {"sleeping", C_POT, 0, 4},
    {"speed", C_POT, 0, 3},
    {"identify", C_SCR, 0, 8},
    {"enchant weapon", C_SCR, 0, 3},
    {"enchant armor", C_SCR, 0, 4},
    {"teleportation", C_SCR, 0, 3},
    {"magic mapping", C_SCR, 0, 3},
    {"remove curse", C_SCR, 0, 3},
    {"striking", C_WAN, 0, 2},
    {"sleep", C_WAN, 0, 2},
    {"digging", C_WAN, 0, 2},
    {"food ration", C_FOOD, 80, 10},
    {"apple", C_FOOD, 5, 9},
    {"corpse", C_FOOD, 0, 0},
    {"gold piece", C_GOLD, 0, 0},
    {"Amulet of Yendor", C_AMU, 0, 0}
};

static const char *const potapp[6] = {
    "ruby", "pink", "murky", "bubbly", "emerald", "golden"
};
static const char *const scrapp[6] = {
    "ZELGO MER", "JUYED AWK YACC", "NR 9", "XIXAXA XOXAXA XUXAXA",
    "PRATYAVAYAH", "ELBIB YLOH"
};
static const char *const wanapp[3] = {"oak", "iron", "glass"};

/* Monsters. */
#define MF_NODIAG 1
#define MF_POISC 2
#define MF_PARA 4
#define MF_POISA 8

typedef struct {
    const char *name;
    char sym;
    u8 lvl;
    u8 spd;
    s8 ac;
    u8 dn;
    u8 ds;
    u8 freq;
    u8 fl;
} MDef;

#define PM_KITTEN 0
#define PM_GUARD 24
#define NMON 25

static const MDef md[NMON] = {
    {"kitten", 'f', 3, 18, 6, 1, 6, 0, 0},
    {"newt", ':', 0, 6, 8, 1, 3, 5, 0},
    {"grid bug", 'x', 0, 12, 9, 1, 1, 3, MF_NODIAG},
    {"sewer rat", 'r', 0, 12, 7, 1, 3, 5, 0},
    {"jackal", 'd', 0, 12, 7, 1, 2, 5, 0},
    {"kobold", 'k', 0, 6, 10, 1, 4, 4, MF_POISC},
    {"gnome", 'G', 1, 6, 10, 1, 6, 4, 0},
    {"floating eye", 'e', 2, 1, 9, 0, 0, 3, MF_PARA},
    {"hill orc", 'o', 1, 9, 10, 1, 6, 4, 0},
    {"giant rat", 'r', 1, 10, 7, 1, 3, 4, 0},
    {"coyote", 'd', 1, 12, 7, 1, 3, 4, 0},
    {"hobbit", 'h', 1, 9, 10, 1, 6, 4, 0},
    {"giant ant", 'a', 2, 18, 3, 1, 4, 3, 0},
    {"dwarf", 'h', 2, 6, 10, 1, 8, 3, 0},
    {"homunculus", 'i', 2, 12, 6, 1, 3, 3, MF_POISC},
    {"gnome lord", 'G', 3, 8, 10, 1, 8, 3, 0},
    {"soldier ant", 'a', 3, 18, 3, 2, 4, 2, MF_POISA},
    {"dingo", 'd', 4, 16, 7, 1, 5, 2, 0},
    {"dwarf lord", 'h', 4, 6, 10, 2, 4, 2, 0},
    {"snake", 'S', 4, 15, 3, 1, 6, 2, MF_POISA | MF_POISC},
    {"gnome king", 'G', 5, 10, 10, 2, 6, 1, 0},
    {"ogre", 'O', 5, 10, 5, 2, 5, 2, 0},
    {"owlbear", 'Y', 5, 12, 5, 2, 6, 1, 0},
    {"troll", 'T', 7, 12, 4, 4, 2, 1, 0},
    {"ogre king", 'O', 9, 14, 4, 3, 5, 0, 0}
};

/* Killers that are not monsters. */
#define K_STARVE 200
#define K_ARROW 201
#define NKILL 5
static const char *const kname[NKILL] = {
    "an arrow", "a bear trap", "a wand", "a poisonous corpse",
    "a falling rock"
};
#define K_BEAR 202
#define K_WAND 203
#define K_POISON 204
#define K_ROCK 205
#pragma rodata-name (pop)

/* From here on, emit string literals in the segment of the function that
 * uses them, so the RAM-image pragmas below also move their text out of
 * ROM.  (Not for the tables above: cc65 would place an initialiser's
 * strings between the table's label and its data.  Their strings go to
 * the end-of-file literal pool, which is directed to RRODATA below.) */
#pragma local-strings (on)

#define OF_CUR 1
#define OF_KN 2
#define OF_BK 4
#define MS_TAME 1
#define MS_SLEEP 2

typedef struct { u8 x, y, typ; s8 spe; u8 qty, fl; } Obj;
typedef struct { u8 x, y, typ, hp, fl, mv; } Mon;
typedef struct { u8 x1, y1, x2, y2; } Room;

typedef struct {
    u16 gid;
    u8 depth;
    u8 map[MAP_H][MAP_W];
    Room rm[NROOM];
    Obj obj[MAXOBJ];
    Mon mon[MAXMON];
    u8 ex, ey, elb;
    char etext[12];
} Level;

typedef struct {
    u16 magic, gid, rng;
    char name[12];
    u8 x, y, xl, str, depth, maxdepth;
    int hp, maxhp, nutr;
    u16 xp, gold, turns, pray;
    u8 conf, sleep, fast, trap, hunger, killer, ansi;
    s8 ac;
    u8 wep, arm[3];
    u8 known[NOBJ], app[NOBJ];
    Obj inv[NINV];
    u8 pettyp, pethp;
} Game;

#define E_DIED 1
#define E_QUIT 2
#define E_ESCAPED 3
#define E_WON 4
#define E_SAVED 5

Game g;     /* global (not static) so the map file shows it for test tools */
Level lev;
static u8 sd_ok;
static char mb[200];
static u8 mlen;
static char nb[52];
static u8 nbl;
static char row[MAP_W + 1];
static char line[16];
static char numbuf[6];
static u8 cur_room;
static u8 took;
static u8 redraw;
static u8 endflag;
static u8 cnt;
static u8 allow_dash;
static s8 ddx, ddy, ddz;
static u8 rx, ry;
static u8 sect[512];
static u16 sp;
static unsigned long fsize;
static char lname[] = "LEVEL01 DAT";
static const char savename[] = "NHSAVE  DAT";
static const char helpname[] = "NHHELP  TXT";

static const s8 xdir[8] = {-1, 0, 0, 1, -1, 1, -1, 1};
static const s8 ydir[8] = {0, 1, -1, 0, -1, -1, 1, 1};
static const char dirkeys[] = "hjklyubn";

static void tick(void);
static void change_level(u8 nd, u8 how);

/* ---------------------------------------------------------------- output */

static void pc(char c) { kim_putc((u8)c); }

static void ps(const char *s)
{
    while (*s) {
        pc(*s++);
    }
}

static void nl(void)
{
    pc(13);
    pc(10);
}

static char *nstr(u16 n)
{
    u8 i = 5;

    numbuf[5] = 0;
    do {
        numbuf[--i] = (char)('0' + n % 10);
        n /= 10;
    } while (n);
    return numbuf + i;
}

static void psn(int v)
{
    if (v < 0) {
        pc('-');
        v = -v;
    }
    ps(nstr((u16)v));
}

static u8 key(void)
{
    u8 c;

    do {
        c = kim_getc();
    } while (c == 13 || c == 10);
    return c;
}

static void getln(char *b, u8 max)
{
    u8 n = 0;
    u8 c;

    for (;;) {
        c = kim_getc();
        g.rng = (u16)((g.rng << 5) - g.rng + c + *(volatile u8 *)0x1706);
        if (c == 13 || (c == 10 && n)) {
            break;
        }
        if ((c == 8 || c == 127) && n) {
            --n;
        } else if (c >= 32 && c < 127 && n < max - 1) {
            b[n++] = (char)c;
        }
    }
    b[n] = 0;
    nl();
}

/* Print the message buffer, wrapping on word boundaries. */
static void mprint(void)
{
    u8 i = 0;
    u8 j;
    u8 col = 0;

    while (i < mlen) {
        j = i;
        while (j < mlen && mb[j] != ' ') {
            ++j;
        }
        if (col && col + (j - i) > 78) {
            nl();
            col = 0;
        }
        while (i < j) {
            pc(mb[i++]);
            ++col;
        }
        while (i < mlen && mb[i] == ' ') {
            if (col) {
                pc(' ');
                ++col;
            }
            ++i;
        }
    }
    mlen = 0;
}

static void mflush(void)
{
    if (mlen) {
        mprint();
        nl();
    }
}

static void more(void)
{
    ps("--More--");
    key();
    nl();
}

static void mputc(char c)
{
    if (mlen >= sizeof(mb) - 1) {
        mprint();
        pc(' ');
        more();
    }
    mb[mlen++] = c;
}

static void mput(const char *s)
{
    while (*s) {
        mputc(*s++);
    }
}

static void mnew(void)
{
    if (mlen) {
        mputc(' ');
        mputc(' ');
    }
}

static void msg(const char *s)
{
    mnew();
    mput(s);
}

static u8 yn(const char *q)
{
    u8 c;

    mflush();
    ps(q);
    ps(" [yn] ");
    c = key();
    nl();
    return c == 'y' || c == 'Y';
}

/* ------------------------------------------------------------------ random */

static u16 rnd16(void)
{
    u16 x = g.rng;

    x ^= x << 7;
    x ^= x >> 9;
    x ^= x << 8;
    g.rng = x;
    return x;
}

static u8 rn2(u8 n) { return n ? (u8)(rnd16() % n) : 0; }

static u8 rnd(u8 n) { return (u8)(rn2(n) + 1); }

static u16 dice(u8 n, u8 s)
{
    u16 t = 0;

    while (n--) {
        t += rnd(s);
    }
    return t;
}

static u8 absdiff(u8 a, u8 b) { return a > b ? a - b : b - a; }

static u8 dist(u8 x1, u8 y1, u8 x2, u8 y2)
{
    u8 dx = absdiff(x1, x2);
    u8 dy = absdiff(y1, y2);

    return dx > dy ? (u8)(dx * 2 + dy) : (u8)(dy * 2 + dx);
}

/* --------------------------------------------------------------------- map */

static u8 terr(u8 x, u8 y) { return lev.map[y][x] & TM; }

static void setterr(u8 x, u8 y, u8 t)
{
    lev.map[y][x] = (u8)((lev.map[y][x] & (F_LIT | F_SEEN)) | t);
}

static u8 passable(u8 t) { return t >= T_FLOOR && t != T_CDOOR; }

static u8 isdoor(u8 t) { return t == T_ODOOR || t == T_CDOOR; }

static Mon *mon_at(u8 x, u8 y)
{
    u8 i;
    Mon *m = lev.mon;

    for (i = 0; i < MAXMON; ++i, ++m) {
        if (m->hp && m->x == x && m->y == y) {
            return m;
        }
    }
    return 0;
}

static u8 couldsee(u8 x, u8 y)
{
    Room *r;

    if ((u8)(x - g.x + 1) <= 2 && (u8)(y - g.y + 1) <= 2) {
        return 1;
    }
    if (cur_room == NONE) {
        return 0;
    }
    r = &lev.rm[cur_room];
    return x >= r->x1 && x <= r->x2 && y >= r->y1 && y <= r->y2;
}

static void see(void)
{
    u8 i;
    u8 x;
    u8 y;
    Room *r;

    cur_room = NONE;
    if (lev.map[g.y][g.x] & F_LIT) {
        for (i = 0; i < NROOM; ++i) {
            r = &lev.rm[i];
            if (g.x >= r->x1 && g.x <= r->x2 && g.y >= r->y1 && g.y <= r->y2) {
                cur_room = i;
                for (y = r->y1; y <= r->y2; ++y) {
                    for (x = r->x1; x <= r->x2; ++x) {
                        lev.map[y][x] |= F_SEEN;
                    }
                }
            }
        }
    }
    for (i = 0; i < 8; ++i) {
        x = g.x + xdir[i];
        y = g.y + ydir[i];
        if (x < MAP_W && y < MAP_H) {
            lev.map[y][x] |= F_SEEN;
        }
    }
    lev.map[g.y][g.x] |= F_SEEN;
}

static u8 room_spot(u8 r)
{
    Room *p = &lev.rm[r];
    u8 n;

    for (n = 0; n < 20; ++n) {
        rx = p->x1 + 1 + rn2(p->x2 - p->x1 - 1);
        ry = p->y1 + 1 + rn2(p->y2 - p->y1 - 1);
        if (terr(rx, ry) == T_FLOOR && !mon_at(rx, ry) &&
            !(rx == g.x && ry == g.y)) {
            return 1;
        }
    }
    return 0;
}

static u8 find_t(u8 t)
{
    for (ry = 0; ry < MAP_H; ++ry) {
        for (rx = 0; rx < MAP_W; ++rx) {
            if (terr(rx, ry) == t) {
                return 1;
            }
        }
    }
    return 0;
}

static void tele(void)
{
    u8 n;

    for (n = 0; n < 100; ++n) {
        if (room_spot(rn2(NROOM))) {
            g.x = rx;
            g.y = ry;
            return;
        }
    }
}

/* ----------------------------------------------------------------- objects */

static Obj *new_obj(u8 typ, u8 x, u8 y)
{
    u8 i;
    Obj *o = lev.obj;

    for (i = 0; i < MAXOBJ; ++i, ++o) {
        if (!o->typ) {
            memset(o, 0, sizeof(Obj));
            o->typ = typ;
            o->x = x;
            o->y = y;
            o->qty = 1;
            return o;
        }
    }
    return 0;
}

static void init_obj(Obj *o)
{
    u8 c = od[o->typ].cls;

    if (c == C_WEP || c == C_ARM) {
        if (!rn2(8)) {
            o->fl = OF_CUR;
            o->spe = -(s8)rnd(3);
        } else if (!rn2(8)) {
            o->spe = (s8)rnd(3);
        }
    } else if (c == C_WAN) {
        o->spe = (s8)(4 + rn2(5));
    }
}

static u8 rand_otyp(void)
{
    u8 r = rn2(100);
    u8 i;

    for (i = 1; i < O_CORPSE; ++i) {
        if (r < od[i].prob) {
            return i;
        }
        r -= od[i].prob;
    }
    return O_APPLE;
}

static Obj *obj_at(u8 x, u8 y)
{
    u8 i;
    Obj *o = lev.obj;

    for (i = 0; i < MAXOBJ; ++i, ++o) {
        if (o->typ && o->x == x && o->y == y) {
            return o;
        }
    }
    return 0;
}

static void nc(char c)
{
    if (nbl < sizeof(nb) - 1) {
        nb[nbl++] = c;
    }
}

static void ns(const char *s)
{
    while (*s) {
        nc(*s++);
    }
}

/* Build an object's name in nb; art adds "a", "an", "the" or a count. */
static char *oname(Obj *o, u8 art)
{
    const ODef *d = &od[o->typ];
    u8 t = o->typ;
    u8 pl = o->qty > 1;
    u8 k = g.known[t];
    const char *pre;
    u8 len;

    nbl = 0;
    if ((o->fl & (OF_BK | OF_CUR)) == (OF_BK | OF_CUR)) {
        ns("cursed ");
    }
    switch (d->cls) {
    case C_WEP:
    case C_ARM:
        if (o->fl & OF_KN) {
            nc(o->spe < 0 ? '-' : '+');
            ns(nstr(o->spe < 0 ? -o->spe : o->spe));
            nc(' ');
        }
        ns(d->name);
        if (pl) {
            nc('s');
        }
        break;
    case C_POT:
        if (k) {
            ns(pl ? "potions of " : "potion of ");
            ns(d->name);
        } else {
            ns(potapp[g.app[t]]);
            ns(pl ? " potions" : " potion");
        }
        break;
    case C_SCR:
        ns(pl ? "scrolls" : "scroll");
        if (k) {
            ns(" of ");
            ns(d->name);
        } else {
            ns(" labeled ");
            ns(scrapp[g.app[t]]);
        }
        break;
    case C_WAN:
        if (k) {
            ns("wand of ");
            ns(d->name);
        } else {
            ns(wanapp[g.app[t]]);
            ns(" wand");
        }
        if (o->fl & OF_KN) {
            ns(" (0:");
            ns(nstr(o->spe));
            nc(')');
        }
        break;
    case C_FOOD:
        if (t == O_CORPSE) {
            ns(md[o->spe].name);
            ns(" corpse");
        } else {
            ns(d->name);
        }
        if (pl) {
            nc('s');
        }
        break;
    case C_GOLD:
        ns(d->name);
        if (pl) {
            nc('s');
        }
        break;
    default:
        ns(d->name);
    }
    nb[nbl] = 0;
    if (art) {
        if (pl) {
            pre = nstr(o->qty);
        } else if (d->cls == C_AMU) {
            pre = "the";
        } else if (strchr("aeiou", nb[0])) {
            pre = "an";
        } else {
            pre = "a";
        }
        len = (u8)strlen(pre);
        if (nbl + len + 1 < sizeof(nb)) {
            memmove(nb + len + 1, nb, nbl + 1);
            memcpy(nb, pre, len);
            nb[len] = ' ';
            nbl += len + 1;
        }
    }
    return nb;
}

static u8 worn(u8 k)
{
    return k != NONE && (g.arm[0] == k || g.arm[1] == k || g.arm[2] == k);
}

static u8 aslot(u8 typ)
{
    return typ == O_HELMET ? 1 : typ == O_SHIELD ? 2 : 0;
}

static void calc_ac(void)
{
    s8 a = 10;
    u8 i;
    u8 k;

    for (i = 0; i < 3; ++i) {
        k = g.arm[i];
        if (k != NONE) {
            a -= (s8)(od[g.inv[k].typ].val + g.inv[k].spe);
        }
    }
    g.ac = a;
}

static u8 has_amulet(void)
{
    u8 i;

    for (i = 0; i < NINV; ++i) {
        if (g.inv[i].typ == O_AMULET) {
            return 1;
        }
    }
    return 0;
}

static void useup(Obj *o)
{
    if (o->qty > 1) {
        --o->qty;
    } else {
        o->typ = 0;
    }
}

static u8 mergeable(Obj *o)
{
    u8 c = od[o->typ].cls;

    return (c == C_POT || c == C_SCR || c == C_FOOD) && o->typ != O_CORPSE;
}

static u8 addinv(Obj *o)
{
    u8 i;
    Obj *p;

    if (mergeable(o)) {
        for (i = 0; i < NINV; ++i) {
            p = &g.inv[i];
            if (p->typ == o->typ && p->spe == o->spe && p->fl == o->fl &&
                p->qty + o->qty < 250) {
                p->qty += o->qty;
                return i;
            }
        }
    }
    for (i = 0; i < NINV; ++i) {
        if (!g.inv[i].typ) {
            g.inv[i] = *o;
            return i;
        }
    }
    return NONE;
}

static void say_inv(u8 k)
{
    mnew();
    mputc((char)('a' + k));
    mput(" - ");
    mput(oname(&g.inv[k], 1));
    if (k == g.wep) {
        mput(" (weapon in hand)");
    }
    mputc('.');
}

static void show_inv(u8 mask)
{
    u8 i;
    u8 n = 0;
    Obj *o;

    mflush();
    for (i = 0; i < NINV; ++i) {
        o = &g.inv[i];
        if (o->typ && (mask & (1 << od[o->typ].cls))) {
            pc((char)('a' + i));
            ps(" - ");
            ps(oname(o, 1));
            if (i == g.wep) {
                ps(" (weapon in hand)");
            }
            if (worn(i)) {
                ps(" (being worn)");
            }
            nl();
            ++n;
        }
    }
    if (!n) {
        ps("Not carrying anything.");
        nl();
    }
}

static u8 getobj(const char *verb, u8 mask)
{
    u8 i;
    u8 c;
    u8 n = 0;

    for (i = 0; i < NINV; ++i) {
        if (g.inv[i].typ && (mask & (1 << od[g.inv[i].typ].cls))) {
            ++n;
        }
    }
    if (!n && !allow_dash) {
        msg("You don't have anything to ");
        mput(verb);
        mputc('.');
        return NONE;
    }
    mflush();
    for (;;) {
        ps("What do you want to ");
        ps(verb);
        ps("? [");
        if (allow_dash) {
            pc('-');
        }
        for (i = 0; i < NINV; ++i) {
            if (g.inv[i].typ && (mask & (1 << od[g.inv[i].typ].cls))) {
                pc((char)('a' + i));
            }
        }
        ps(" or ?*] ");
        c = key();
        nl();
        if (c == '?' || c == '*') {
            show_inv(c == '*' ? 0xFF : mask);
            continue;
        }
        if (c == '-' && allow_dash) {
            return 254;
        }
        if (c >= 'a' && c < 'a' + NINV && g.inv[c - 'a'].typ) {
            c -= 'a';
            if (!(mask & (1 << od[g.inv[c].typ].cls))) {
                msg("That is a silly thing to ");
                mput(verb);
                mputc('.');
                return NONE;
            }
            return c;
        }
        if (c == 27 || c == ' ') {
            msg("Never mind.");
        } else {
            msg("You don't have that object.");
        }
        return NONE;
    }
}

/* ---------------------------------------------------------------- monsters */

static void mname(Mon *m, u8 cap)
{
    if (m->fl & MS_TAME) {
        mput(cap ? "Your " : "your ");
    } else {
        mput(cap ? "The " : "the ");
    }
    mput(md[m->typ].name);
}

static Mon *mkmon(u8 typ, u8 x, u8 y)
{
    u8 i;
    u8 l = md[typ].lvl;
    Mon *m = lev.mon;

    for (i = 0; i < MAXMON; ++i, ++m) {
        if (!m->hp) {
            m->x = x;
            m->y = y;
            m->typ = typ;
            m->hp = l ? (u8)dice(l, 8) : rnd(4);
            m->fl = 0;
            m->mv = 0;
            return m;
        }
    }
    return 0;
}

static u8 pickmon(void)
{
    u8 maxl = (u8)((g.depth + g.xl) / 2);
    u8 minl = (u8)(g.depth / 6);
    u8 tot = 0;
    u8 i;
    u8 r;
    const MDef *d;

    for (i = 1, d = md + 1; i < PM_GUARD; ++i, ++d) {
        if (d->lvl >= minl && d->lvl <= maxl) {
            tot += d->freq;
        }
    }
    r = rn2(tot);
    for (i = 1, d = md + 1; i < PM_GUARD; ++i, ++d) {
        if (d->lvl >= minl && d->lvl <= maxl) {
            if (r < d->freq) {
                return i;
            }
            r -= d->freq;
        }
    }
    return 1;
}

static void spawn(void)
{
    if (room_spot(rn2(NROOM)) && !couldsee(rx, ry)) {
        mkmon(pickmon(), rx, ry);
    }
}

static void mdie(Mon *m)
{
    Obj *o;

    m->hp = 0;
    if (!rn2(2)) {
        o = new_obj(O_CORPSE, m->x, m->y);
        if (o) {
            o->spe = (s8)m->typ;
        }
    }
}

static u16 newuexp(u8 l)
{
    return l < 10 ? 10u << l : 10000u << (l - 10);
}

static void levelup(void)
{
    u8 h;

    while (g.xl < MAXXL && g.xp >= newuexp(g.xl)) {
        ++g.xl;
        h = rnd(8) + 1;
        g.maxhp += h;
        g.hp += h;
        msg("Welcome to experience level ");
        mput(nstr(g.xl));
        mputc('.');
    }
}

static void ukill(Mon *m)
{
    const MDef *d = &md[m->typ];

    g.xp += 1 + d->lvl * d->lvl + (d->spd >= 18 ? 5 : d->spd > 12 ? 3 : 0);
    mdie(m);
    levelup();
}

static void hurt(u16 dmg, u8 killer)
{
    g.hp -= (int)dmg;
    if (g.hp <= 0) {
        g.killer = killer;
    }
}

static void uattack(Mon *m)
{
    const MDef *d = &md[m->typ];
    Obj *w = g.wep != NONE ? &g.inv[g.wep] : 0;
    int tmp;
    int dmg;

    if (lev.ex == g.x && lev.ey == g.y) {
        lev.ex = NONE;
    }
    tmp = 1 + d->ac + g.xl + (g.str < 6 ? -2 : g.str < 8 ? -1 : g.str > 16);
    if (w) {
        tmp += w->spe;
    }
    if (m->fl & MS_SLEEP) {
        tmp += 2;
    }
    m->fl &= ~MS_SLEEP;
    mnew();
    if (tmp > rnd(20)) {
        dmg = w ? rnd(od[w->typ].val) + w->spe : rnd(2);
        dmg += g.str < 6 ? -1 : g.str < 16 ? 0 : g.str < 18 ? 1 : 2;
        if (dmg < 1) {
            dmg = 1;
        }
        if (dmg >= m->hp) {
            mput("You kill ");
            mname(m, 0);
            mputc('!');
            ukill(m);
        } else {
            m->hp -= (u8)dmg;
            mput("You hit ");
            mname(m, 0);
            mputc('.');
            if (d->fl & MF_PARA) {
                msg("You are frozen by ");
                mname(m, 0);
                mput("'s gaze!");
                g.sleep = 10 + rnd(20);
            }
        }
    } else {
        mput("You miss ");
        mname(m, 0);
        mputc('.');
    }
}

static void mattacku(Mon *m)
{
    const MDef *d = &md[m->typ];
    int tmp;
    int dmg;

    tmp = 10 + d->lvl + (g.ac >= 0 ? g.ac : -(int)rnd((u8)-g.ac));
    mnew();
    mname(m, 1);
    if (tmp > rnd(20)) {
        mput(" hits!");
        dmg = (int)dice(d->dn, d->ds);
        if (g.ac < 0) {
            dmg -= rnd((u8)-g.ac);
            if (dmg < 1) {
                dmg = 1;
            }
        }
        if ((d->fl & MF_POISA) && !rn2(5) && g.str > 3) {
            msg("You feel weaker!");
            --g.str;
        }
        hurt((u16)dmg, m->typ);
    } else {
        mput(" misses.");
    }
}

static void mhitm(Mon *a, Mon *df)
{
    const MDef *ad = &md[a->typ];
    u8 vis = couldsee(a->x, a->y) || couldsee(df->x, df->y);
    u16 dmg;

    df->fl &= ~MS_SLEEP;
    if (md[df->typ].ac + 10 + ad->lvl > rnd(20)) {
        dmg = dice(ad->dn, ad->ds);
        if (dmg >= df->hp) {
            if (vis) {
                mnew();
                mname(a, 1);
                mput(" kills ");
                mname(df, 0);
                mputc('!');
            } else if (df->fl & MS_TAME) {
                msg("You have a sad feeling for a moment, then it passes.");
            }
            mdie(df);
            return;
        }
        df->hp -= (u8)dmg;
        if (vis) {
            mnew();
            mname(a, 1);
            mput(" hits ");
            mname(df, 0);
            mputc('.');
        }
    } else if (vis) {
        mnew();
        mname(a, 1);
        mput(" misses ");
        mname(df, 0);
        mputc('.');
    }
}

static u8 m_ok(Mon *m, u8 x, u8 y, u8 diag)
{
    u8 t;

    if (x >= MAP_W || y >= MAP_H) {
        return 0;
    }
    t = terr(x, y);
    if (!passable(t) && t != T_CDOOR) {
        return 0;
    }
    if (diag && ((md[m->typ].fl & MF_NODIAG) || isdoor(t) ||
                 isdoor(terr(m->x, m->y)))) {
        return 0;
    }
    if (x == g.x && y == g.y) {
        return 0;
    }
    return mon_at(x, y) == 0;
}

static void m_step(Mon *m, u8 gx, u8 gy, u8 flee)
{
    u8 i;
    u8 best = NONE;
    u8 bd;
    u8 d;
    u8 nx;
    u8 ny;

    bd = dist(m->x, m->y, gx, gy);
    if (!rn2(5)) {
        i = rn2(8);
        if (m_ok(m, m->x + xdir[i], m->y + ydir[i], i > 3)) {
            best = i;
        }
    } else {
        for (i = 0; i < 8; ++i) {
            nx = m->x + xdir[i];
            ny = m->y + ydir[i];
            if (!m_ok(m, nx, ny, i > 3)) {
                continue;
            }
            d = dist(nx, ny, gx, gy);
            if (flee ? d > bd : d < bd) {
                bd = d;
                best = i;
            }
        }
    }
    if (best == NONE) {
        return;
    }
    nx = m->x + xdir[best];
    ny = m->y + ydir[best];
    if (terr(nx, ny) == T_CDOOR) {
        setterr(nx, ny, T_ODOOR);
        if (couldsee(nx, ny)) {
            msg("You see a door open.");
        }
        return;
    }
    m->x = nx;
    m->y = ny;
}

static Mon *adjacent_to(Mon *m, u8 tame)
{
    u8 i;
    Mon *o = lev.mon;

    for (i = 0; i < MAXMON; ++i, ++o) {
        if (o->hp && o != m && (o->fl & MS_TAME) == tame &&
            absdiff(o->x, m->x) <= 1 && absdiff(o->y, m->y) <= 1) {
            return o;
        }
    }
    return 0;
}

static void pet_act(Mon *m)
{
    Mon *h = adjacent_to(m, 0);

    if (h && !(md[h->typ].fl & MF_PARA)) {
        mhitm(m, h);
    } else if (dist(m->x, m->y, g.x, g.y) > 4) {
        m_step(m, g.x, g.y, 0);
    } else if (rn2(2)) {
        m_step(m, rn2(MAP_W), rn2(MAP_H), 0);
    }
}

static void mact(Mon *m)
{
    const MDef *d = &md[m->typ];
    u8 adx;
    u8 ady;
    u8 scared;
    Mon *p;

    if (m->fl & MS_SLEEP) {
        if (couldsee(m->x, m->y) && !rn2(3)) {
            m->fl &= ~MS_SLEEP;
        }
        return;
    }
    if (m->fl & MS_TAME) {
        pet_act(m);
        return;
    }
    adx = absdiff(m->x, g.x);
    ady = absdiff(m->y, g.y);
    scared = lev.elb && lev.ex == g.x && lev.ey == g.y;
    if (adx <= 1 && ady <= 1 && !scared && d->dn &&
        (!(d->fl & MF_NODIAG) || !adx || !ady)) {
        mattacku(m);
        return;
    }
    p = adjacent_to(m, MS_TAME);
    if (p && d->dn && rn2(2)) {
        mhitm(m, p);
        return;
    }
    if (adx > 20 || ady > 10) {
        m_step(m, rn2(MAP_W), rn2(MAP_H), 0);
    } else {
        m_step(m, g.x, g.y, scared);
    }
}

static void mon_turns(void)
{
    u8 i;
    Mon *m = lev.mon;

    for (i = 0; i < MAXMON; ++i, ++m) {
        if (!m->hp) {
            continue;
        }
        m->mv += md[m->typ].spd;
        while (m->mv >= 12 && m->hp && g.hp > 0 && !endflag) {
            m->mv -= 12;
            mact(m);
        }
    }
}

static u8 hostile_near(void)
{
    u8 i;
    Mon *m = lev.mon;

    for (i = 0; i < MAXMON; ++i, ++m) {
        if (m->hp && !(m->fl & MS_TAME) && couldsee(m->x, m->y)) {
            return 1;
        }
    }
    return 0;
}

#pragma code-name (push, "RCODE")
#pragma rodata-name (push, "RRODATA")

/* ------------------------------------------------------- level generation */

static void dig(u8 x0, u8 y0, u8 x1, u8 y1)
{
    for (;;) {
        if (terr(x0, y0) == T_STONE) {
            lev.map[y0][x0] = T_CORR;
        }
        if (x0 == x1 && y0 == y1) {
            return;
        }
        if (x0 < x1) {
            ++x0;
        } else if (x0 > x1) {
            --x0;
        } else if (y0 < y1) {
            ++y0;
        } else {
            --y0;
        }
    }
}

static void mkdoor(u8 x, u8 y)
{
    u8 t = terr(x, y);
    u8 k;

    if (t == T_HWALL || t == T_VWALL) {
        k = rn2(3);
        setterr(x, y, k == 0 ? T_DOORWAY : k == 1 ? T_ODOOR : T_CDOOR);
    }
}

static void join(u8 a, u8 b)
{
    Room *A = &lev.rm[a];
    Room *B = &lev.rm[b];
    u8 pa;
    u8 pb;
    u8 m;

    if (b == a + 1) {
        pa = A->y1 + 1 + rn2(A->y2 - A->y1 - 1);
        pb = B->y1 + 1 + rn2(B->y2 - B->y1 - 1);
        m = A->x2 + 1 + rn2(B->x1 - A->x2 - 1);
        mkdoor(A->x2, pa);
        mkdoor(B->x1, pb);
        dig(A->x2 + 1, pa, m, pa);
        dig(m, pa, m, pb);
        dig(m, pb, B->x1 - 1, pb);
    } else {
        pa = A->x1 + 1 + rn2(A->x2 - A->x1 - 1);
        pb = B->x1 + 1 + rn2(B->x2 - B->x1 - 1);
        m = A->y2 + 1 + rn2(B->y1 - A->y2 - 1);
        mkdoor(pa, A->y2);
        mkdoor(pb, B->y1);
        dig(pa, A->y2 + 1, pa, m);
        dig(pa, m, pb, m);
        dig(pb, m, pb, B->y1 - 1);
    }
}

static u8 neighbor(u8 a, u8 dir)
{
    u8 col = a % 3;

    if (dir == 0) {
        return col < 2 ? a + 1 : NONE;
    }
    if (dir == 1) {
        return col ? a - 1 : NONE;
    }
    if (dir == 2) {
        return a < 6 ? a + 3 : NONE;
    }
    return a > 2 ? a - 3 : NONE;
}

static void gen_level(void)
{
    u8 r;
    u8 x;
    u8 y;
    u8 w;
    u8 h;
    u8 c;
    u8 lit;
    u8 a;
    u8 b;
    u8 n;
    u8 ru;
    u8 rd;
    u8 conn[NROOM];
    Room *p;
    Obj *o;
    Mon *m;

    memset(&lev, 0, sizeof(lev));
    lev.gid = g.gid;
    lev.depth = g.depth;
    lev.ex = NONE;
    for (r = 0, p = lev.rm; r < NROOM; ++r, ++p) {
        w = 5 + rn2(10);
        h = 3 + rn2(4);
        p->x1 = (r % 3) * 21 + rn2(21 - w);
        p->y1 = (r / 3) * 7 + rn2(7 - h);
        p->x2 = p->x1 + w - 1;
        p->y2 = p->y1 + h - 1;
        lit = rn2(g.depth + 2) < 4 ? F_LIT : 0;
        for (y = p->y1; y <= p->y2; ++y) {
            for (x = p->x1; x <= p->x2; ++x) {
                c = (y == p->y1 || y == p->y2) ? T_HWALL :
                    (x == p->x1 || x == p->x2) ? T_VWALL : T_FLOOR;
                lev.map[y][x] = c | lit;
            }
        }
        conn[r] = 0;
    }
    conn[0] = 1;
    n = 1;
    while (n < NROOM) {
        a = rn2(NROOM);
        b = neighbor(a, rn2(4));
        if (!conn[a] || b == NONE || conn[b]) {
            continue;
        }
        conn[b] = 1;
        ++n;
        join(a < b ? a : b, a < b ? b : a);
    }
    for (n = 0; n < 3; ++n) {
        a = rn2(NROOM);
        b = neighbor(a, rn2(4));
        if (b != NONE) {
            join(a < b ? a : b, a < b ? b : a);
        }
    }

    ru = rn2(NROOM);
    do {
        rd = rn2(NROOM);
    } while (rd == ru);
    room_spot(ru);
    setterr(rx, ry, T_UP);
    room_spot(rd);
    if (g.depth < MAXD) {
        setterr(rx, ry, T_DN);
    } else {
        new_obj(O_AMULET, rx, ry);
        m = mkmon(PM_GUARD, rx, ry);
        if (m) {
            m->fl = MS_SLEEP;
        }
    }
    for (r = 0; r < NROOM; ++r) {
        if (!rn2(3) && room_spot(r)) {
            o = new_obj(rand_otyp(), rx, ry);
            if (o) {
                init_obj(o);
            }
        }
        if (!rn2(3) && room_spot(r)) {
            o = new_obj(O_GOLD, rx, ry);
            if (o) {
                o->qty = rnd((u8)(g.depth * 20 + 10));
            }
        }
        if (r == ru) {
            continue;
        }
        if (!rn2(3) && room_spot(r)) {
            m = mkmon(pickmon(), rx, ry);
            if (m && rn2(2)) {
                m->fl = MS_SLEEP;
            }
        }
        if (rn2(10) < 1 + g.depth / 3 && room_spot(r)) {
            c = rn2(NTRAP);
            if (c == 1 && g.depth >= MAXD) {
                c = 2;
            }
            lev.map[ry][rx] = (lev.map[ry][rx] & F_LIT) | (T_TRAP + c) | F_HID;
        }
    }
}

#pragma rodata-name (pop)
#pragma code-name (pop)

/* --------------------------------------------------------------- SD files */

static u8 wr_bytes(const u8 *p, u16 n)
{
    while (n--) {
        sect[sp++] = *p++;
        ++fsize;
        if (sp == sizeof(sect)) {
            if (nh_sd_write_sector(sect)) {
                return 0;
            }
            sp = 0;
        }
    }
    return 1;
}

static u8 wr_file(const char *name, u8 with_game)
{
    sp = 0;
    fsize = 0;
    if (nh_sd_create_file(name)) {
        return 0;
    }
    if (with_game && !wr_bytes((const u8 *)&g, sizeof(g))) {
        return 0;
    }
    if (!wr_bytes((const u8 *)&lev, sizeof(lev))) {
        return 0;
    }
    if (sp) {
        while (sp < sizeof(sect)) {
            sect[sp++] = 0;
        }
        if (nh_sd_write_sector(sect)) {
            return 0;
        }
    }
    return nh_sd_finish_write(&fsize) == 0;
}

static u8 rd_bytes(u8 *p, u16 n)
{
    int c;

    while (n--) {
        c = nh_sd_read_byte();
        if (c < 0) {
            return 0;
        }
        *p++ = (u8)c;
    }
    return 1;
}

static void set_lname(void)
{
    lname[5] = (char)('0' + g.depth / 10);
    lname[6] = (char)('0' + g.depth % 10);
}

static void save_level(void)
{
    if (sd_ok) {
        set_lname();
        if (!wr_file(lname, 0)) {
            msg("You hear the SD card complain.");
        }
    }
}

static u8 load_level(void)
{
    if (!sd_ok) {
        return 0;
    }
    set_lname();
    return !nh_sd_open_file(lname) && rd_bytes((u8 *)&lev, sizeof(lev)) &&
           lev.gid == g.gid && lev.depth == g.depth;
}

static u8 restore(void)
{
    if (!sd_ok || nh_sd_open_file(savename)) {
        return 0;
    }
    if (!rd_bytes((u8 *)&g, sizeof(g)) || g.magic != MAGIC ||
        !rd_bytes((u8 *)&lev, sizeof(lev))) {
        return 0;
    }
    nh_sd_delete_file(savename);
    return 1;
}

/* ----------------------------------------------------------------- display */

static const char *rank(void)
{
    return g.xl < 3 ? "Stripling" : g.xl < 6 ? "Skirmisher" :
           g.xl < 10 ? "Fighter" : "Warrior";
}

static void status(void)
{
    static const char *const hs[4] = {"", " Hungry", " Weak", " Fainting"};

    ps(g.name);
    ps(" the ");
    ps(rank());
    ps("  St:");
    ps(nstr(g.str));
    ps(" AC:");
    psn(g.ac);
    ps(" Au:");
    ps(nstr(g.gold));
    nl();
    ps("Dlvl:");
    ps(nstr(g.depth));
    ps(" HP:");
    psn(g.hp);
    pc('(');
    psn(g.maxhp);
    ps(") Xp:");
    ps(nstr(g.xl));
    pc('/');
    ps(nstr(g.xp));
    ps(" T:");
    ps(nstr(g.turns));
    ps(hs[g.hunger]);
    if (g.conf) {
        ps(" Conf");
    }
    if (g.fast) {
        ps(" Fast");
    }
    nl();
}

static void draw(void)
{
    u8 x;
    u8 y;
    u8 c;
    u8 t;
    u8 i;
    Obj *o;
    Mon *m;

    see();
    if (g.ansi) {
        ps("\x1b[H\x1b[2J");
    } else {
        nl();
    }
    if (mlen) {
        mflush();
    } else if (g.ansi) {
        nl();
    }
    for (y = 0; y < MAP_H; ++y) {
        for (x = 0; x < MAP_W; ++x) {
            c = lev.map[y][x];
            if (!(c & F_SEEN)) {
                row[x] = ' ';
                continue;
            }
            t = (c & F_HID) ? T_FLOOR : c & TM;
            row[x] = tchar[t];
            if (t == T_ODOOR && ((x && terr(x - 1, y) == T_HWALL) ||
                                 terr(x + 1, y) == T_HWALL)) {
                row[x] = '|';
            }
        }
        for (i = 0, o = lev.obj; i < MAXOBJ; ++i, ++o) {
            if (o->typ && o->y == y && (lev.map[y][o->x] & F_SEEN)) {
                row[o->x] = csym[od[o->typ].cls];
            }
        }
        for (i = 0, m = lev.mon; i < MAXMON; ++i, ++m) {
            if (m->hp && m->y == y && couldsee(m->x, y)) {
                row[m->x] = md[m->typ].sym;
            }
        }
        if (y == g.y) {
            row[g.x] = '@';
        }
        x = MAP_W;
        while (x && row[x - 1] == ' ') {
            --x;
        }
        row[x] = 0;
        ps(row);
        nl();
    }
    status();
}

#pragma code-name (push, "RCODE")
#pragma rodata-name (push, "RRODATA")

static void help(void)
{
    u8 n = 0;
    int c;

    mflush();
    if (sd_ok && !nh_sd_open_file(helpname)) {
        while ((c = nh_sd_read_byte()) >= 0) {
            if (c == 10) {
                nl();
                if (++n == 20) {
                    n = 0;
                    more();
                }
            } else if (c != 13) {
                pc((char)c);
            }
        }
    } else {
        ps("hjklyubn move (HJKL.. run)  , pick up  d drop  i inventory");
        nl();
        ps("w wield  W wear  T take off  q quaff  r read  e eat  z zap");
        nl();
        ps("s search  . rest  : look  < > stairs  E engrave  #pray");
        nl();
        ps("S save  Q quit  O ANSI screen  ^R redraw.  Put NHHELP.TXT on");
        nl();
        ps("the SD card for full help.");
        nl();
    }
    more();
    redraw = 1;
}

#pragma rodata-name (pop)
#pragma code-name (pop)

/* ----------------------------------------------------------------- actions */

static void trap(u8 k)
{
    lev.map[g.y][g.x] &= ~F_HID;
    switch (k) {
    case 0:
        msg("You feel a wrenching sensation.");
        tele();
        break;
    case 1:
        msg("A trap door opens up under you!");
        change_level(g.depth + 1, 2);
        break;
    case 2:
        msg("An arrow shoots out at you!");
        if (rn2(2)) {
            msg("You are hit by an arrow!");
            hurt(rnd(6), K_ARROW);
        } else {
            msg("The arrow misses you.");
        }
        break;
    case 3:
        msg("A bear trap closes on your foot!");
        g.trap = 4 + rn2(4);
        hurt(dice(2, 4), K_BEAR);
        break;
    default:
        msg("A cloud of gas puts you to sleep!");
        g.sleep += rnd(25);
    }
}

static void arrive(void)
{
    u8 t = terr(g.x, g.y);
    u8 i;
    u8 n = 0;
    Obj *o;
    Obj *last = 0;

    if (t >= T_TRAP) {
        trap(t - T_TRAP);
        return;
    }
    for (i = 0, o = lev.obj; i < MAXOBJ; ++i, ++o) {
        if (o->typ && o->x == g.x && o->y == g.y) {
            if (o->typ == O_GOLD) {
                g.gold += o->qty;
                mnew();
                mput(nstr(o->qty));
                mput(o->qty > 1 ? " gold pieces." : " gold piece.");
                o->typ = 0;
            } else {
                ++n;
                last = o;
            }
        }
    }
    if (n == 1) {
        msg("You see here ");
        mput(oname(last, 1));
        mputc('.');
    } else if (n) {
        msg("There are several objects here.");
    }
    if (lev.ex == g.x && lev.ey == g.y) {
        msg("Something is written here in the dust.");
    }
}

static u8 move_u(s8 dx, s8 dy)
{
    u8 nx;
    u8 ny;
    u8 t;
    u8 k;
    Mon *m;

    if (g.trap) {
        msg("You are caught in a bear trap.");
        --g.trap;
        return 1;
    }
    if (g.conf && !rn2(4)) {
        k = rn2(8);
        dx = xdir[k];
        dy = ydir[k];
    }
    nx = g.x + dx;
    ny = g.y + dy;
    if (nx >= MAP_W || ny >= MAP_H) {
        return 0;
    }
    t = terr(nx, ny);
    if (dx && dy && (isdoor(t) || isdoor(terr(g.x, g.y)))) {
        msg("You can't move diagonally into or out of a doorway.");
        return 0;
    }
    m = mon_at(nx, ny);
    if (m) {
        if (m->fl & MS_TAME) {
            m->x = g.x;
            m->y = g.y;
            g.x = nx;
            g.y = ny;
            msg("You swap places with ");
            mname(m, 0);
            mputc('.');
            arrive();
        } else {
            uattack(m);
        }
        return 1;
    }
    if (t == T_CDOOR) {
        if (rn2(4)) {
            msg("The door opens.");
            setterr(nx, ny, T_ODOOR);
        } else {
            msg("This door is stuck.");
        }
        return 1;
    }
    if (!passable(t)) {
        return 0;
    }
    if (t >= T_TRAP && !(lev.map[ny][nx] & F_HID) &&
        !yn("Really step onto that trap?")) {
        return 0;
    }
    g.x = nx;
    g.y = ny;
    arrive();
    return 1;
}

static void run(u8 d)
{
    u8 n;
    u8 i;
    u8 k;
    u8 found;
    u8 px;
    u8 py;
    u8 x;
    u8 y;

    for (n = 0; n < 80; ++n) {
        px = g.x;
        py = g.y;
        if (!move_u(xdir[d], ydir[d])) {
            /* Follow a corridor around a bend. */
            if (n == 0 || mlen || terr(g.x, g.y) != T_CORR) {
                break;
            }
            found = 0;
            for (i = 0; i < 8; ++i) {
                x = g.x + xdir[i];
                y = g.y + ydir[i];
                if (x < MAP_W && y < MAP_H && passable(terr(x, y)) &&
                    (xdir[i] != -xdir[d] || ydir[i] != -ydir[d]) &&
                    absdiff(x, g.x - xdir[d]) + absdiff(y, g.y - ydir[d]) > 1) {
                    ++found;
                    k = i;
                }
            }
            if (found != 1) {
                break;
            }
            d = k;
            continue;
        }
        tick();
        see();
        if (px == g.x && py == g.y) {
            break;
        }
        if (g.hp <= 0 || mlen || g.sleep || endflag || hostile_near()) {
            break;
        }
        k = terr(g.x, g.y);
        if ((k != T_FLOOR && k != T_CORR) || obj_at(g.x, g.y)) {
            break;
        }
    }
    redraw = 1;
}

static void search(void)
{
    u8 i;
    u8 x;
    u8 y;
    u8 *c;

    for (i = 0; i < 8; ++i) {
        x = g.x + xdir[i];
        y = g.y + ydir[i];
        if (x >= MAP_W || y >= MAP_H) {
            continue;
        }
        c = &lev.map[y][x];
        if ((*c & F_HID) && !rn2(3)) {
            *c &= ~F_HID;
            msg("You find ");
            mput(trapname[(*c & TM) - T_TRAP]);
            mputc('.');
        }
    }
}

static void rest(u8 srch)
{
    u8 n = cnt ? cnt : 1;

    while (n--) {
        if (srch) {
            search();
        }
        tick();
        see();
        if (g.hp <= 0 || mlen || endflag || g.sleep || hostile_near()) {
            break;
        }
    }
    redraw = 1;
}

static void pickup(void)
{
    u8 i;
    u8 k;
    u8 n = 0;
    Obj *o;

    for (i = 0, o = lev.obj; i < MAXOBJ; ++i, ++o) {
        if (o->typ && o->x == g.x && o->y == g.y) {
            k = addinv(o);
            if (k == NONE) {
                msg("You have no room for ");
                mput(oname(o, 1));
                mputc('.');
                break;
            }
            o->typ = 0;
            say_inv(k);
            ++n;
        }
    }
    if (n) {
        took = 1;
    } else if (!mlen) {
        msg("There is nothing here to pick up.");
    }
}

#pragma code-name (push, "RCODE")
#pragma rodata-name (push, "RRODATA")

static void drop(void)
{
    u8 k = getobj("drop", 0xFF);
    Obj *o;
    Obj *f;

    if (k == NONE) {
        return;
    }
    o = &g.inv[k];
    if (worn(k)) {
        msg("You cannot drop something you are wearing.");
        return;
    }
    if (k == g.wep) {
        if (o->fl & OF_CUR) {
            o->fl |= OF_BK;
            msg("Your weapon is welded to your hand!");
            return;
        }
        g.wep = NONE;
    }
    f = new_obj(1, g.x, g.y);
    if (!f) {
        msg("There is no room here.");
        return;
    }
    *f = *o;
    f->x = g.x;
    f->y = g.y;
    msg("You drop ");
    mput(oname(o, 1));
    mputc('.');
    o->typ = 0;
    took = 1;
}

static void wield(void)
{
    u8 k;
    Obj *o;

    allow_dash = 1;
    k = getobj("wield", 1 << C_WEP);
    allow_dash = 0;
    if (k == NONE) {
        return;
    }
    if (g.wep != NONE && (g.inv[g.wep].fl & OF_CUR)) {
        g.inv[g.wep].fl |= OF_BK;
        msg("Your weapon is welded to your hand!");
        return;
    }
    took = 1;
    if (k == 254) {
        g.wep = NONE;
        msg("You are empty handed.");
        return;
    }
    g.wep = k;
    o = &g.inv[k];
    say_inv(k);
    if (o->fl & OF_CUR) {
        o->fl |= OF_BK;
        msg("It welds itself to your hand!");
    }
}

static void wear(void)
{
    u8 k = getobj("wear", 1 << C_ARM);
    u8 s;
    Obj *o;

    if (k == NONE) {
        return;
    }
    o = &g.inv[k];
    s = aslot(o->typ);
    if (g.arm[s] != NONE) {
        msg(g.arm[s] == k ? "You are already wearing that!" :
                            "You are already wearing something there.");
        return;
    }
    g.arm[s] = k;
    o->fl |= OF_KN;
    calc_ac();
    msg("You are now wearing ");
    mput(oname(o, 1));
    mputc('.');
    took = 1;
}

static void takeoff(void)
{
    u8 k;
    Obj *o;

    if (g.arm[0] == NONE && g.arm[1] == NONE && g.arm[2] == NONE) {
        msg("You are not wearing anything.");
        return;
    }
    k = getobj("take off", 1 << C_ARM);
    if (k == NONE) {
        return;
    }
    o = &g.inv[k];
    if (!worn(k)) {
        msg("You are not wearing that.");
        return;
    }
    if (o->fl & OF_CUR) {
        o->fl |= OF_BK;
        msg("You can't.  It is cursed.");
        return;
    }
    g.arm[aslot(o->typ)] = NONE;
    calc_ac();
    msg("You were wearing ");
    mput(oname(o, 1));
    mputc('.');
    took = 1;
}

static void heal(u8 n, u8 up)
{
    g.hp += n;
    if (g.hp > g.maxhp) {
        g.maxhp += up;
        g.hp = g.maxhp;
    }
}

static void quaff(void)
{
    u8 k = getobj("drink", 1 << C_POT);
    u8 t;

    if (k == NONE) {
        return;
    }
    t = g.inv[k].typ;
    useup(&g.inv[k]);
    took = 1;
    g.known[t] = 1;
    switch (t) {
    case O_PHEAL:
        msg("You feel better.");
        heal((u8)dice(6, 4), 1);
        break;
    case O_PXHEAL:
        msg("You feel much better.");
        heal((u8)dice(6, 8), 2);
        break;
    case O_PGAIN:
        msg("You feel more experienced.");
        if (g.xl < MAXXL) {
            g.xp = newuexp(g.xl);
            levelup();
        }
        break;
    case O_PCONF:
        msg("Huh, What?  Where am I?");
        g.conf += 15 + rn2(8);
        break;
    case O_PSLEEP:
        msg("You suddenly fall asleep!");
        g.sleep += rnd(15);
        break;
    default:
        msg("You are suddenly moving faster.");
        g.fast += 100 + rn2(50);
    }
}

static void identify(void)
{
    u8 i;
    u8 k;
    Obj *o;

    if (!rn2(5)) {
        msg("You feel self-knowledgeable...");
        for (i = 0, o = g.inv; i < NINV; ++i, ++o) {
            if (o->typ) {
                g.known[o->typ] = 1;
                o->fl |= OF_KN | OF_BK;
            }
        }
        return;
    }
    k = getobj("identify", 0xFF);
    if (k != NONE) {
        o = &g.inv[k];
        g.known[o->typ] = 1;
        o->fl |= OF_KN | OF_BK;
        say_inv(k);
    }
}

static void readscr(void)
{
    u8 k = getobj("read", 1 << C_SCR);
    u8 t;
    u8 i;
    u8 n;
    u8 x;
    u8 y;
    Obj *o;

    if (k == NONE) {
        return;
    }
    t = g.inv[k].typ;
    useup(&g.inv[k]);
    took = 1;
    g.known[t] = 1;
    switch (t) {
    case O_SIDENT:
        msg("This is an identify scroll.");
        identify();
        break;
    case O_SENCHW:
        if (g.wep == NONE) {
            g.known[t] = 0;
            msg("Your hands twitch.");
            break;
        }
        o = &g.inv[g.wep];
        if (o->spe < 9) {
            ++o->spe;
        }
        o->fl &= ~OF_CUR;
        msg("Your weapon glows blue for a moment.");
        break;
    case O_SENCHA:
        n = 0;
        for (i = 0; i < 3; ++i) {
            if (g.arm[i] != NONE) {
                ++n;
            }
        }
        if (!n) {
            g.known[t] = 0;
            msg("Your skin glows then fades.");
            break;
        }
        n = rn2(n);
        for (i = 0; i < 3; ++i) {
            if (g.arm[i] != NONE && !n--) {
                o = &g.inv[g.arm[i]];
                if (o->spe < 9) {
                    ++o->spe;
                }
                o->fl &= ~OF_CUR;
                msg("Your ");
                mput(od[o->typ].name);
                mput(" glows silver for a moment.");
            }
        }
        calc_ac();
        break;
    case O_STELE:
        tele();
        break;
    case O_SMAP:
        msg("A map coalesces in your mind!");
        for (y = 0; y < MAP_H; ++y) {
            for (x = 0; x < MAP_W; ++x) {
                if (terr(x, y) != T_STONE) {
                    lev.map[y][x] |= F_SEEN;
                }
            }
        }
        break;
    default:
        msg("You feel like someone is helping you.");
        for (i = 0, o = g.inv; i < NINV; ++i, ++o) {
            if (i == g.wep || worn(i)) {
                o->fl &= ~OF_CUR;
            }
        }
    }
}

static void eat_obj(Obj *o)
{
    u8 t = o->typ;
    u8 turns = 1;
    int n;
    u8 q = o->qty;
    const MDef *d = &md[o->spe];

    if (t == O_RATION) {
        n = 800;
        turns = 5;
    } else if (t == O_APPLE) {
        n = 50;
    } else {
        n = (d->lvl + 1) * 50;
        turns = 3;
    }
    o->qty = 1;
    msg("You finish eating ");
    mput(oname(o, 1));
    mputc('.');
    o->qty = q;
    if (t == O_CORPSE && (d->fl & MF_POISC)) {
        msg("Ecch - that must have been poisonous!");
        if (!rn2(4) && g.str > 3) {
            msg("You feel weaker!");
            --g.str;
        } else {
            hurt(rnd(15), K_POISON);
        }
    }
    useup(o);
    g.nutr += n;
    if (g.nutr > 2000) {
        g.nutr = 2000;
        msg("You're having a hard time getting all of it down.");
    }
    g.hunger = 0;
    took = 1;
    while (--turns && g.hp > 0 && !endflag) {
        tick();
    }
}

static void eat(void)
{
    u8 k;
    Obj *o;

    o = obj_at(g.x, g.y);
    if (o && od[o->typ].cls == C_FOOD) {
        mflush();
        ps("There is ");
        ps(oname(o, 1));
        if (yn(" here; eat it?")) {
            eat_obj(o);
            return;
        }
    }
    k = getobj("eat", 1 << C_FOOD);
    if (k != NONE) {
        eat_obj(&g.inv[k]);
    }
}

static u8 getdir(void)
{
    u8 c;
    u8 i;

    mflush();
    ps("In what direction? ");
    c = key();
    nl();
    ddx = ddy = ddz = 0;
    if (c == '<') {
        ddz = -1;
        return 1;
    }
    if (c == '>') {
        ddz = 1;
        return 1;
    }
    if (c == '.' || c == 's') {
        ddz = 2;
        return 1;
    }
    for (i = 0; i < 8; ++i) {
        if (c == dirkeys[i]) {
            ddx = xdir[i];
            ddy = ydir[i];
            return 1;
        }
    }
    return 0;
}

static void bolt(u8 typ)
{
    u8 x = g.x;
    u8 y = g.y;
    u8 n;
    u8 t;
    u16 dmg;
    Mon *m;

    for (n = 0; n < 12; ++n) {
        x += ddx;
        y += ddy;
        if (x >= MAP_W || y >= MAP_H) {
            return;
        }
        t = terr(x, y);
        if (typ == O_WDIG) {
            if (t == T_STONE) {
                lev.map[y][x] = T_CORR;
            } else if (t == T_HWALL || t == T_VWALL || isdoor(t)) {
                setterr(x, y, T_DOORWAY);
            }
            continue;
        }
        if (t == T_CDOOR && typ == O_WSTRIKE) {
            msg("The door crashes open!");
            setterr(x, y, T_DOORWAY);
            return;
        }
        if (!passable(t)) {
            return;
        }
        m = mon_at(x, y);
        if (!m) {
            continue;
        }
        mnew();
        if (typ == O_WSLEEP) {
            m->fl |= MS_SLEEP;
            mput("The sleep ray hits ");
            mname(m, 0);
            mputc('.');
            continue;
        }
        g.known[typ] = 1;
        if (rnd(20) < 10 + md[m->typ].ac) {
            mput("The wand hits ");
            mname(m, 0);
            mputc('!');
            dmg = dice(2, 12);
            if (dmg >= m->hp) {
                msg("You kill ");
                mname(m, 0);
                mputc('!');
                ukill(m);
            } else {
                m->hp -= (u8)dmg;
                m->fl &= ~MS_SLEEP;
            }
        } else {
            mput("The wand misses ");
            mname(m, 0);
            mputc('.');
        }
        return;
    }
}

static void zap(void)
{
    u8 k = getobj("zap", 1 << C_WAN);
    u8 t;
    Obj *o;

    if (k == NONE) {
        return;
    }
    o = &g.inv[k];
    t = o->typ;
    if (!getdir()) {
        msg("What a strange direction!");
        return;
    }
    took = 1;
    if (!o->spe) {
        msg("Nothing happens.");
        return;
    }
    --o->spe;
    if (t != O_WSTRIKE) {
        g.known[t] = 1;
    }
    if (!ddz) {
        bolt(t);
        return;
    }
    if (t == O_WSTRIKE) {
        if (ddz == 2) {
            g.known[t] = 1;
            msg("You bash yourself!");
            hurt(dice(2, 12), K_WAND);
        } else {
            msg("Boing!");
        }
    } else if (t == O_WSLEEP) {
        if (ddz == 2) {
            msg("The sleep ray hits you!");
            g.sleep += rnd(50);
        } else {
            msg("The bugs on the floor stop moving!");
        }
    } else if (ddz < 0) {
        msg("You loosen a rock from the ceiling.  It falls on your head!");
        hurt(rnd(6), K_ROCK);
    } else if (g.depth >= MAXD) {
        msg("The floor here is too hard to dig in.");
    } else if (terr(g.x, g.y) == T_UP || terr(g.x, g.y) == T_DN) {
        msg("The stairs are too hard to dig in.");
    } else {
        msg("You dig a hole through the floor.  You fall through...");
        change_level(g.depth + 1, 2);
    }
}

static void look(void)
{
    u8 t = terr(g.x, g.y);
    u8 i;
    u8 n = 0;
    Obj *o;

    if (t == T_UP || t == T_DN) {
        msg(t == T_UP ? "There is a staircase up here." :
                        "There is a staircase down here.");
    } else if (t >= T_TRAP) {
        msg("There is ");
        mput(trapname[t - T_TRAP]);
        mput(" here.");
    }
    if (lev.ex == g.x && lev.ey == g.y) {
        msg("Something is written here in the dust.  You read: \"");
        mput(lev.etext);
        mput("\".");
    }
    for (i = 0, o = lev.obj; i < MAXOBJ; ++i, ++o) {
        if (o->typ && o->x == g.x && o->y == g.y) {
            if (!n++) {
                msg("Things that are here:");
            }
            mnew();
            mput(oname(o, 1));
            mputc('.');
        }
    }
    if (!mlen) {
        msg("You see no objects here.");
    }
}

static void engrave(void)
{
    u8 i;
    static const char elb[] = "elbereth";

    mflush();
    ps("What do you want to write in the dust here? ");
    getln(lev.etext, sizeof(lev.etext));
    if (!lev.etext[0]) {
        msg("Never mind.");
        return;
    }
    lev.ex = g.x;
    lev.ey = g.y;
    lev.elb = 1;
    for (i = 0; i < 9; ++i) {
        if ((lev.etext[i] | 0x20) != elb[i] && !(i == 8 && !lev.etext[i])) {
            lev.elb = 0;
        }
    }
    msg("You write in the dust with your fingertip.");
    took = 1;
}

static void pray(void)
{
    u8 trouble = g.hp < 6 || g.hp * 7 < g.maxhp || g.nutr < 50;

    if (!yn("Are you sure you want to pray?")) {
        return;
    }
    took = 1;
    msg("You begin praying to Tyr.");
    if (g.pray < (trouble ? 200 : 100)) {
        msg("You feel a hopeful feeling.");
        if (trouble) {
            if (g.nutr < 50) {
                g.nutr = 900;
                g.hunger = 0;
                msg("Your stomach feels content.");
            }
            if (g.hp < g.maxhp) {
                g.hp = g.maxhp;
                msg("You feel much better.");
            }
        } else {
            msg("You feel that Tyr is well-pleased.");
        }
        g.pray = 300 + rn2(200);
    } else {
        msg("You feel that Tyr is displeased.");
    }
}

static u8 save_game(void)
{
    if (!sd_ok) {
        msg("You cannot save without an SD card.");
        return 0;
    }
    if (!yn("Really save?")) {
        return 0;
    }
    ps("Saving...");
    g.magic = MAGIC;
    if (!wr_file(savename, 1)) {
        nl();
        msg("Save failed.");
        return 0;
    }
    ps(" done.");
    nl();
    return 1;
}

#pragma rodata-name (pop)
#pragma code-name (pop)

static void stairs(u8 down)
{
    u8 t = terr(g.x, g.y);

    if (down) {
        if (t != T_DN) {
            msg("You can't go down here.");
            return;
        }
        change_level(g.depth + 1, 0);
    } else {
        if (t != T_UP) {
            msg("You can't go up here.");
            return;
        }
        if (g.depth == 1) {
            if (has_amulet()) {
                endflag = E_WON;
            } else if (yn("Are you sure you want to leave the dungeon?")) {
                endflag = E_ESCAPED;
            }
            return;
        }
        change_level(g.depth - 1, 1);
    }
    took = 1;
}

static void do_cmd(void)
{
    u8 c;
    u8 i;

    cnt = 0;
    c = key();
    while (c >= '0' && c <= '9') {
        cnt = cnt * 10 + (c - '0');
        if (cnt > 200) {
            cnt = 200;
        }
        c = key();
    }
    for (i = 0; i < 8; ++i) {
        if (c == dirkeys[i]) {
            took = move_u(xdir[i], ydir[i]);
            return;
        }
        if (c == dirkeys[i] - 32) {
            run(i);
            return;
        }
    }
    switch (c) {
    case '.':
    case 's':
        rest(c == 's');
        break;
    case ',':
        pickup();
        break;
    case 'i':
        show_inv(0xFF);
        more();
        redraw = 1;
        break;
    case 'd':
        drop();
        break;
    case 'w':
        wield();
        break;
    case 'W':
        wear();
        break;
    case 'T':
        takeoff();
        break;
    case 'q':
        quaff();
        break;
    case 'r':
        readscr();
        break;
    case 'e':
        eat();
        break;
    case 'z':
        zap();
        break;
    case ':':
        look();
        break;
    case '<':
    case '>':
        stairs(c == '>');
        break;
    case 'E':
        engrave();
        break;
    case '#':
        mflush();
        pc('#');
        getln(line, sizeof(line));
        if (!strcmp(line, "pray")) {
            pray();
        } else {
            msg("Unknown extended command.");
        }
        break;
    case 'S':
        if (save_game()) {
            endflag = E_SAVED;
        }
        break;
    case 'Q':
        if (yn("Really quit?")) {
            endflag = E_QUIT;
        }
        break;
    case '?':
        help();
        break;
    case 'O':
        g.ansi ^= 1;
        msg(g.ansi ? "ANSI screen mode on." : "ANSI screen mode off.");
        redraw = 1;
        break;
    case 18:
    case 12:
        redraw = 1;
        break;
    default:
        msg("Unknown command.  Press ? for help.");
    }
}

/* -------------------------------------------------------------------- turns */

static void tick(void)
{
    u8 h;

    if (g.fast && !rn2(3)) {
        return;
    }
    mon_turns();
    if (g.hp <= 0 || endflag) {
        return;
    }
    ++g.turns;
    if (g.pray) {
        --g.pray;
    }
    --g.nutr;
    h = g.nutr > 150 ? 0 : g.nutr >= 50 ? 1 : g.nutr >= 0 ? 2 : 3;
    if (h > g.hunger) {
        msg(h == 1 ? "You are beginning to feel hungry." :
            h == 2 ? "You are beginning to feel weak." :
                     "You are fainting from lack of food.");
    }
    g.hunger = h;
    if (g.nutr < 0) {
        if (!g.sleep && !rn2(10)) {
            msg("You faint from lack of food.");
            g.sleep = rnd(10);
        }
        if (g.nutr < -250) {
            msg("You die from starvation.");
            g.hp = 0;
            g.killer = K_STARVE;
            return;
        }
    }
    if (g.hp < g.maxhp &&
        (g.xl >= 10 || g.turns % (42 / (g.xl + 2) + 1) == 0)) {
        ++g.hp;
    }
    if (g.conf && !--g.conf) {
        msg("You feel less confused now.");
    }
    if (g.fast && !--g.fast) {
        msg("You feel yourself slowing down.");
    }
    if (!rn2(70)) {
        spawn();
    }
}

static void place_pet(u8 typ, u8 hp)
{
    u8 i;
    u8 x;
    u8 y;
    Mon *m;

    for (i = 0; i < 8; ++i) {
        x = g.x + xdir[i];
        y = g.y + ydir[i];
        if (x < MAP_W && y < MAP_H && passable(terr(x, y)) && !mon_at(x, y)) {
            m = mkmon(typ, x, y);
            if (m) {
                m->hp = hp;
                m->fl = MS_TAME;
            }
            return;
        }
    }
}

static void change_level(u8 nd, u8 how)
{
    u8 i;
    Mon *m;

    g.pethp = 0;
    for (i = 0, m = lev.mon; i < MAXMON; ++i, ++m) {
        if (m->hp && (m->fl & MS_TAME) && absdiff(m->x, g.x) <= 1 &&
            absdiff(m->y, g.y) <= 1) {
            g.pettyp = m->typ;
            g.pethp = m->hp;
            m->hp = 0;
        }
    }
    save_level();
    g.depth = nd;
    if (nd > g.maxdepth) {
        g.maxdepth = nd;
    }
    if (!load_level()) {
        gen_level();
    }
    if (how < 2 && find_t(how ? T_DN : T_UP)) {
        g.x = rx;
        g.y = ry;
    } else {
        tele();
    }
    m = mon_at(g.x, g.y);
    if (m && room_spot(rn2(NROOM))) {
        m->x = rx;
        m->y = ry;
    }
    if (g.pethp) {
        place_pet(g.pettyp, g.pethp);
        g.pethp = 0;
    }
    redraw = 1;
}

#pragma code-name (push, "RCODE")
#pragma rodata-name (push, "RRODATA")

static void shuffle(u8 first, u8 n)
{
    u8 i;
    u8 j;
    u8 t;

    for (i = 0; i < n; ++i) {
        g.app[first + i] = i;
    }
    for (i = n - 1; i; --i) {
        j = rn2(i + 1);
        t = g.app[first + i];
        g.app[first + i] = g.app[first + j];
        g.app[first + j] = t;
    }
}

static void give(u8 typ, s8 spe, u8 qty)
{
    Obj o;

    memset(&o, 0, sizeof(o));
    o.typ = typ;
    o.spe = spe;
    o.qty = qty;
    o.fl = OF_KN | OF_BK;
    addinv(&o);
}

static void new_game(void)
{
    u16 seed = g.rng;

    memset(&g, 0, sizeof(g));
    g.rng = seed ^ 0x5A17;
    mflush();
    ps("Who are you? ");
    getln(g.name, sizeof(g.name));
    if (!g.name[0]) {
        strcpy(g.name, "Agent");
    }
    if (!g.rng) {
        g.rng = 0xACE1;
    }
    g.ansi = yn("Does your terminal support ANSI screen clearing?");
    g.gid = rnd16();
    g.hp = g.maxhp = 16;
    g.str = 16;
    g.xl = 1;
    g.nutr = 900;
    g.pray = 300;
    g.depth = g.maxdepth = 1;
    g.arm[1] = g.arm[2] = NONE;
    g.known[O_RATION] = g.known[O_APPLE] = g.known[O_CORPSE] = 1;
    g.known[O_GOLD] = g.known[O_AMULET] = 1;
    for (seed = 0; seed < NOBJ; ++seed) {
        if (od[seed].cls <= C_ARM) {
            g.known[seed] = 1;
        }
    }
    shuffle(O_PHEAL, 6);
    shuffle(O_SIDENT, 6);
    shuffle(O_WSTRIKE, 3);
    give(O_LSWORD, 1, 1);
    give(O_LEATHER, 0, 1);
    give(O_RATION, 0, 1);
    give(O_APPLE, 0, 2);
    g.wep = 0;
    g.arm[0] = 1;
    calc_ac();
    gen_level();
    find_t(T_UP);
    g.x = rx;
    g.y = ry;
    place_pet(PM_KITTEN, 12);
    msg("Hello ");
    mput(g.name);
    mput(", welcome to NetHack!  You are a neutral human Stripling.");
    msg("Bring the Amulet of Yendor up from dungeon level 10.");
}

static void tomb_line(const char *s)
{
    u8 n = (u8)strlen(s);
    u8 i;

    if (n > 18) {
        n = 18;
    }
    ps("                  |");
    for (i = 0; i < 18; ++i) {
        pc(i >= (18 - n) / 2 && i < (18 - n) / 2 + n ? s[i - (18 - n) / 2] : ' ');
    }
    pc('|');
    nl();
}

static u8 end_game(u8 how)
{
    char gold[12];

    mflush();
    if (how == E_DIED) {
        ps("You die...");
        nl();
        more();
        ps("                       ----------");
        nl();
        ps("                      /          \\");
        nl();
        ps("                     /    REST    \\");
        nl();
        ps("                    /      IN      \\");
        nl();
        ps("                   /     PEACE      \\");
        nl();
        ps("                  /                  \\");
        nl();
        tomb_line(g.name);
        strcpy(gold, nstr(g.gold));
        strcat(gold, " Au");
        tomb_line(gold);
        if (g.killer == K_STARVE) {
            tomb_line("died of");
            tomb_line("starvation");
        } else {
            tomb_line("killed by");
            if (g.killer < NMON) {
                nbl = 0;
                ns(strchr("aeiou", md[g.killer].name[0]) ? "an " : "a ");
                ns(md[g.killer].name);
                nb[nbl] = 0;
                tomb_line(nb);
            } else {
                tomb_line(kname[g.killer - K_ARROW]);
            }
        }
        tomb_line("");
        tomb_line("");
        ps("                 *|     *  *  *      | *");
        nl();
        ps("        _________)/\\\\_//(\\/(/\\)/\\//\\/|_)_______");
        nl();
        nl();
    }
    if (how == E_SAVED) {
        return 0;
    }
    if (how == E_WON) {
        ps("You escape the dungeon with the Amulet of Yendor!");
        nl();
        ps("You are a hero of legend!");
    } else {
        ps("Goodbye ");
        ps(g.name);
        ps(" the ");
        ps(rank());
        ps("...");
        nl();
        ps(how == E_DIED ? "You died" : how == E_QUIT ? "You quit" :
                                                        "You left the dungeon");
        ps(" on dungeon level ");
        ps(nstr(g.depth));
    }
    nl();
    ps("You had ");
    ps(nstr(g.gold));
    ps(" gold, reached experience level ");
    ps(nstr(g.xl));
    ps(" and dungeon level ");
    ps(nstr(g.maxdepth));
    ps(", in ");
    ps(nstr(g.turns));
    ps(" turns.");
    nl();
    return yn("Play again?");
}

#pragma rodata-name (pop)
#pragma code-name (pop)

static u8 play(void)
{
    u8 e;

    redraw = 1;
    for (;;) {
        if (g.hp <= 0) {
            return end_game(E_DIED);
        }
        if (endflag) {
            e = endflag;
            endflag = 0;
            return end_game(e);
        }
        if (g.sleep) {
            --g.sleep;
            tick();
            if (!g.sleep && g.hp > 0) {
                msg("You can move again.");
            }
            continue;
        }
        if (redraw || took || g.ansi) {
            draw();
        } else if (mlen) {
            mflush();
        }
        took = 0;
        redraw = 0;
        do_cmd();
        if (took) {
            tick();
        }
    }
}

/* Load the RAM half of the program from the SD card unless it is already
 * in memory (for example, loaded by paper tape). */
static const char imgname[] = "NETHACK BIN";
extern u8 _DATA_RUN__[];
extern u8 _DATA_SIZE__[];

static u8 load_ram(void)
{
    u8 *p = (u8 *)0x2000;
    u16 n = (u16)_DATA_RUN__ + (u16)_DATA_SIZE__ - 0x2000;
    int c;

    if (ram_stamp == BUILD_ID) {
        return 1;
    }
    if (!sd_ok || nh_sd_open_file(imgname)) {
        return 0;
    }
    ps("Loading NETHACK.BIN...");
    while (n--) {
        c = nh_sd_read_byte();
        if (c < 0) {
            break;
        }
        *p++ = (u8)c;
    }
    nl();
    return ram_stamp == BUILD_ID;
}

int main(void)
{
    ps("NetHack for the KIM-1");
    nl();
    sd_ok = nh_sd_init() == 0;
    if (!load_ram()) {
        ps(sd_ok ? "NETHACK.BIN is missing or does not match this ROM." :
                   "No SD card, and the RAM image is not loaded.");
        nl();
        return 0;
    }
    ps(sd_ok ? "SD card ready." :
               "No SD card: no saving, and levels are not kept.");
    nl();
    g.rng = 0xACE1;
    for (;;) {
        if (restore()) {
            msg("Hello ");
            mput(g.name);
            mput(", welcome back to NetHack!");
        } else {
            new_game();
        }
        if (!play()) {
            return 0;
        }
    }
}

/* Pooled literals from the tables at the top are emitted here. */
#pragma rodata-name ("RRODATA")
