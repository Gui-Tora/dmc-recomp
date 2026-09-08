"""Compares executable ELF program-header ranges against the function ranges
exported in a Ghidra CSV (Name,Start,End,Size; End exclusive) and reports any
address ranges inside executable segments that no function covers.

Usage: python scripts/find_coverage_gaps.py <elf> <csv>
"""
import csv
import struct
import sys
from pathlib import Path

PF_X = 0x1


def elf_exec_segments(path):
    data = Path(path).read_bytes()
    e_phoff, = struct.unpack_from('<I', data, 28)
    e_phentsize, = struct.unpack_from('<H', data, 42)
    e_phnum, = struct.unpack_from('<H', data, 44)
    segments = []
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align = \
            struct.unpack_from('<IIIIIIII', data, off)
        if p_type == 1 and p_filesz > 0 and (p_flags & PF_X):
            segments.append((p_vaddr, p_vaddr + p_filesz))
    return segments


def csv_ranges(path):
    ranges = []
    with open(path, encoding='utf-8-sig', newline='') as f:
        for row in csv.DictReader(f):
            start = int(row['Start'], 0)
            end = int(row['End'], 0)
            if end > start:
                ranges.append((start, end, row['Name']))
    return ranges


def merge(ranges):
    if not ranges:
        return []
    ranges = sorted(ranges)
    merged = [list(ranges[0])]
    for s, e in ranges[1:]:
        if s <= merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], e)
        else:
            merged.append([s, e])
    return merged


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    elf_path, csv_path = sys.argv[1], sys.argv[2]

    segments = elf_exec_segments(elf_path)
    functions = csv_ranges(csv_path)
    covered = merge([(s, e) for s, e, _ in functions])

    print(f'Segmentos ejecutables en el ELF: {len(segments)}')
    for s, e in segments:
        print(f'  0x{s:08x} - 0x{e:08x} ({e - s} bytes)')
    print(f'Funciones en el CSV: {len(functions)}')
    print(f'Rangos cubiertos (fusionados): {len(covered)}')
    print()

    total_gap_bytes = 0
    gap_count = 0
    for seg_start, seg_end in segments:
        cursor = seg_start
        for c_start, c_end in covered:
            if c_end <= cursor or c_start >= seg_end:
                continue
            gap_s = cursor
            gap_e = min(c_start, seg_end)
            if gap_e > gap_s:
                gap_count += 1
                total_gap_bytes += gap_e - gap_s
                print(f'HUECO 0x{gap_s:08x} - 0x{gap_e:08x} ({gap_e - gap_s} bytes)')
            cursor = max(cursor, min(c_end, seg_end))
        if cursor < seg_end:
            gap_count += 1
            total_gap_bytes += seg_end - cursor
            print(f'HUECO 0x{cursor:08x} - 0x{seg_end:08x} ({seg_end - cursor} bytes)')

    print()
    print(f'Total huecos: {gap_count}, total bytes sin cubrir: {total_gap_bytes}')


if __name__ == '__main__':
    main()
