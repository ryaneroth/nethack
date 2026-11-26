"""List the root directory of a test FAT32 image, follow each file's cluster
chain, and report overlaps and the level-file headers (gid, depth)."""
import struct
import sys

img = open(sys.argv[1], "rb").read()
SEC = 512
part = struct.unpack_from("<I", img, 446 + 8)[0]
bs = part * SEC
spc = img[bs + 13]
rsv = struct.unpack_from("<H", img, bs + 14)[0]
nfat = img[bs + 16]
fatsz = struct.unpack_from("<I", img, bs + 36)[0]
root = struct.unpack_from("<I", img, bs + 44)[0]
fat0 = (part + rsv) * SEC
data0 = part + rsv + nfat * fatsz


def fat(c, n=0):
    return struct.unpack_from("<I", img, fat0 + n * fatsz * SEC + c * 4)[0] & 0x0FFFFFFF


def cl(c):
    off = (data0 + (c - 2) * spc) * SEC
    return img[off:off + spc * SEC]


def chain(c):
    out = []
    while 2 <= c < 0x0FFFFFF8 and len(out) < 10000:
        out.append(c)
        c = fat(c)
    return out


rootdata = b"".join(cl(c) for c in chain(root))
owner = {}
print("root chain", chain(root))
for i in range(0, len(rootdata), 32):
    e = rootdata[i:i + 32]
    if e[0] == 0:
        print(f"slot {i // 32}: end")
        break
    name = e[0:11].decode("latin-1")
    first = struct.unpack_from("<H", e, 20)[0] << 16 | struct.unpack_from("<H", e, 26)[0]
    size = struct.unpack_from("<I", e, 28)[0]
    if e[0] == 0xE5:
        print(f"slot {i // 32}: deleted {name!r} first={first}")
        continue
    ch = chain(first)
    need = -(-size // (spc * SEC))
    data = b"".join(cl(c) for c in ch)[:size]
    info = ""
    if name.startswith("LEVEL"):
        gid, depth = struct.unpack_from("<HB", data, 0)
        info = f"hdr gid={gid} depth={depth}"
    print(f"slot {i // 32}: {name!r} first={first} size={size} clusters={len(ch)} "
          f"need={need} {info}")
    for c in ch:
        if c in owner:
            print(f"   OVERLAP cluster {c} also in {owner[c]}")
        owner[c] = name
mism = [c for c in range(2, 600) if fat(c, 0) != fat(c, 1)]
print("FAT copies differ at clusters:", mism[:20])
