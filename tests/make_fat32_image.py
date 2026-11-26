#!/usr/bin/env python3
"""Create a minimal FAT32 card image for the KIM SD filesystem smoke test."""

import struct
import sys

SECTOR_SIZE = 512
PARTITION_START = 2048
RESERVED_SECTORS = 32
FAT_SECTORS = 1024
FAT_COUNT = 2
ROOT_CLUSTER = 2


def write_at(image, sector, data):
    image.seek(sector * SECTOR_SIZE)
    image.write(data)


def main(path, sectors_per_cluster, files=()):
    partition_sectors = 131072 if sectors_per_cluster == 1 else 135168
    data_sectors = (partition_sectors - RESERVED_SECTORS
                    - FAT_COUNT * FAT_SECTORS)
    if data_sectors // sectors_per_cluster < 65525:
        raise ValueError("image would not meet the FAT32 cluster-count minimum")
    total_sectors = PARTITION_START + partition_sectors
    with open(path, "wb") as image:
        image.truncate(total_sectors * SECTOR_SIZE)

        mbr = bytearray(SECTOR_SIZE)
        entry = 446
        mbr[entry + 4] = 0x0C
        struct.pack_into("<II", mbr, entry + 8,
                         PARTITION_START, partition_sectors)
        mbr[510:512] = b"\x55\xaa"
        write_at(image, 0, mbr)

        boot = bytearray(SECTOR_SIZE)
        boot[0:3] = b"\xeb\x58\x90"
        boot[3:11] = b"KIM1TEST"
        struct.pack_into("<H", boot, 11, SECTOR_SIZE)
        boot[13] = sectors_per_cluster
        struct.pack_into("<H", boot, 14, RESERVED_SECTORS)
        boot[16] = FAT_COUNT
        struct.pack_into("<H", boot, 17, 0)
        struct.pack_into("<H", boot, 19, 0)
        boot[21] = 0xF8
        struct.pack_into("<H", boot, 22, 0)
        struct.pack_into("<H", boot, 24, 63)
        struct.pack_into("<H", boot, 26, 255)
        struct.pack_into("<I", boot, 28, PARTITION_START)
        struct.pack_into("<I", boot, 32, partition_sectors)
        struct.pack_into("<I", boot, 36, FAT_SECTORS)
        struct.pack_into("<H", boot, 40, 0)
        struct.pack_into("<H", boot, 42, 0)
        struct.pack_into("<I", boot, 44, ROOT_CLUSTER)
        struct.pack_into("<H", boot, 48, 1)
        struct.pack_into("<H", boot, 50, 6)
        boot[64] = 0x80
        boot[66] = 0x29
        struct.pack_into("<I", boot, 67, 0x4B494D31)
        boot[71:82] = b"KIM1 TEST  "
        boot[82:90] = b"FAT32   "
        boot[510:512] = b"\x55\xaa"
        write_at(image, PARTITION_START, boot)

        fsinfo = bytearray(SECTOR_SIZE)
        struct.pack_into("<I", fsinfo, 0, 0x41615252)
        struct.pack_into("<I", fsinfo, 484, 0x61417272)
        struct.pack_into("<I", fsinfo, 488,
                         data_sectors // sectors_per_cluster - 1)
        struct.pack_into("<I", fsinfo, 492, 3)
        struct.pack_into("<I", fsinfo, 508, 0xAA550000)
        write_at(image, PARTITION_START + 1, fsinfo)

        fat = bytearray(FAT_SECTORS * SECTOR_SIZE)
        struct.pack_into("<I", fat, 0, 0x0FFFFFF8)
        struct.pack_into("<I", fat, 4, 0x0FFFFFFF)
        struct.pack_into("<I", fat, ROOT_CLUSTER * 4, 0x0FFFFFFF)

        # Optionally place files in the root directory, each in a
        # contiguous cluster chain starting after the root cluster.
        data_start = PARTITION_START + RESERVED_SECTORS + FAT_COUNT * FAT_SECTORS
        cluster_bytes = sectors_per_cluster * SECTOR_SIZE
        root = bytearray(cluster_bytes)
        next_cluster = ROOT_CLUSTER + 1
        for slot, file_path in enumerate(files):
            data = open(file_path, "rb").read()
            count = max(1, -(-len(data) // cluster_bytes))
            first = next_cluster
            for i in range(count):
                cluster = first + i
                link = cluster + 1 if i < count - 1 else 0x0FFFFFFF
                struct.pack_into("<I", fat, cluster * 4, link)
                chunk = data[i * cluster_bytes:(i + 1) * cluster_bytes]
                write_at(image, data_start + (cluster - 2) * sectors_per_cluster,
                         chunk)
            next_cluster += count
            entry = bytearray(32)
            entry[0:11] = short_name(file_path)
            entry[11] = 0x20
            struct.pack_into("<H", entry, 20, first >> 16)
            struct.pack_into("<H", entry, 26, first & 0xFFFF)
            struct.pack_into("<I", entry, 28, len(data))
            root[slot * 32:(slot + 1) * 32] = entry
        if files:
            write_at(image, data_start, root)
            struct.pack_into("<I", fsinfo, 488,
                             data_sectors // sectors_per_cluster - next_cluster + 1)
            struct.pack_into("<I", fsinfo, 492, next_cluster)
            write_at(image, PARTITION_START + 1, fsinfo)

        fat_start = PARTITION_START + RESERVED_SECTORS
        for index in range(FAT_COUNT):
            write_at(image, fat_start + index * FAT_SECTORS, fat)


def short_name(file_path):
    """Return the space-padded 11-byte FAT 8.3 name for a file path."""
    name = file_path.replace("\\", "/").rsplit("/", 1)[-1].upper()
    base, _, ext = name.partition(".")
    if not base or len(base) > 8 or len(ext) > 3:
        raise SystemExit(f"{file_path}: not a valid 8.3 file name")
    return (base.ljust(8) + ext.ljust(3)).encode("ascii")


if __name__ == "__main__":
    args = sys.argv[1:]
    cluster_size = 1
    add = []
    while len(args) > 1:
        if args[-2] == "--sectors-per-cluster":
            cluster_size = int(args[-1])
        elif args[-2] == "--add":
            add.insert(0, args[-1])
        else:
            break
        args = args[:-2]
    if len(args) != 1:
        raise SystemExit(
            "usage: make_fat32_image.py OUTPUT.img "
            "[--sectors-per-cluster 1|2] [--add FILE]..."
        )
    if cluster_size not in (1, 2):
        raise SystemExit("sectors per cluster must be 1 or 2")
    main(args[0], cluster_size, add)
