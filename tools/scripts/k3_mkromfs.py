#!/usr/bin/env python3
"""
tools/scripts/k3_mkromfs.py - K-Three ROMFS Disk Image Generator

Packs multiple K3 binary executables and text files into a contiguous
disk image (disk.img) readable by the K-Three Linux kernel block DMA driver.

ROMFS Disk Layout (16 words per sector):
  Sector 0: Superblock
    [0]     = Magic: 19683 (3^9)
    [1]     = Total sectors
    [2]     = Inode count
    [3..15] = Reserved (0)
  Sector 1..N: Inode Table (1 sector per file)
    [0]     = Inode ID
    [1]     = Start sector
    [2]     = Size in words
    [3..10] = 8-character filename (stored as ASCII words)
    [11..15]= Reserved
  Sector N+1..: File Data Sectors (16 words per sector, padded)
"""

import os
import sys

SECTOR_WORDS = 16
MAGIC = 19683

def read_k3_words(filepath):
    words = []
    with open(filepath, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith(';') or line.startswith('#') or line.startswith('//'):
                continue
            parts = line.split()
            for part in parts:
                try:
                    val = int(part)
                    words.append(val)
                except ValueError:
                    pass
    return words

def str_to_words(s, max_len=8):
    words = [0] * max_len
    for i, ch in enumerate(s[:max_len]):
        words[i] = ord(ch)
    return words

def build_romfs(files, out_img_path):
    inodes = []
    data_sectors = []
    current_data_sector = 1 + len(files)  # Superblock (1) + Inodes (len(files))

    for idx, (name, path) in enumerate(files, start=1):
        if not os.path.exists(path):
            print(f"Warning: {path} not found, skipping.")
            continue
        words = read_k3_words(path)
        word_count = len(words)
        
        # Pad words to multiple of 16
        padded = list(words)
        rem = len(padded) % SECTOR_WORDS
        if rem != 0:
            padded.extend([0] * (SECTOR_WORDS - rem))
            
        sector_count = len(padded) // SECTOR_WORDS
        inodes.append({
            'id': idx,
            'name': name,
            'start_sector': current_data_sector,
            'size': word_count,
            'words': padded
        })
        current_data_sector += sector_count

    # 1. Build Superblock (Sector 0)
    total_sectors = current_data_sector
    sb = [0] * SECTOR_WORDS
    sb[0] = MAGIC
    sb[1] = total_sectors
    sb[2] = len(inodes)

    # Write disk.img (word image format)
    with open(out_img_path, 'w', encoding='utf-8') as out:
        out.write("; K-Three ROMFS Disk Image\n")
        out.write(f"; Total sectors: {total_sectors}, Inodes: {len(inodes)}\n")
        
        # Write Sector 0: Superblock
        out.write("; --- Sector 0: Superblock ---\n")
        for w in sb:
            out.write(f"{w}\n")
            
        # Write Inode Sectors
        for ino in inodes:
            out.write(f"; --- Inode {ino['id']}: {ino['name']} ---\n")
            sec = [0] * SECTOR_WORDS
            sec[0] = ino['id']
            sec[1] = ino['start_sector']
            sec[2] = ino['size']
            name_words = str_to_words(ino['name'], 8)
            for i, nw in enumerate(name_words):
                sec[3 + i] = nw
            for w in sec:
                out.write(f"{w}\n")

        # Write Data Sectors
        for ino in inodes:
            out.write(f"; --- File Data: {ino['name']} (Sectors {ino['start_sector']}..) ---\n")
            for w in ino['words']:
                out.write(f"{w}\n")

    print(f"Built ROMFS image '{out_img_path}': {total_sectors} sectors, {len(inodes)} files.")

if __name__ == "__main__":
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    out_disk = os.path.join(root, "build", "disk.img")
    files_to_pack = [
        ("sh", os.path.join(root, "build", "userspace", "sh.k3")),
        ("echo", os.path.join(root, "build", "userspace", "echo.k3")),
        ("cat", os.path.join(root, "build", "userspace", "cat.k3")),
        ("kbox", os.path.join(root, "build", "userspace", "kbox.k3")),
    ]
    build_romfs(files_to_pack, out_disk)
