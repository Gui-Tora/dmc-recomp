"""VU1 micro-instruction decoding shared by the GAMEPLAY_001K tools (generator, listings).

Field layout and opcode selectors mirror PS2Recomp's VU1Interpreter (ps2_vu1_core/upper/lower.cpp):
lower word at pc, upper word at pc+4; upper special selector and lower special selector are both
(w & 3) | ((w >> 4) & 0x7C).
"""

UPPER = {
    **{op: ('ADDbc', op & 3) for op in range(0x00, 0x04)}, **{op: ('SUBbc', op & 3) for op in range(0x04, 0x08)},
    **{op: ('MADDbc', op & 3) for op in range(0x08, 0x0C)}, **{op: ('MSUBbc', op & 3) for op in range(0x0C, 0x10)},
    **{op: ('MAXbc', op & 3) for op in range(0x10, 0x14)}, **{op: ('MINIbc', op & 3) for op in range(0x14, 0x18)},
    **{op: ('MULbc', op & 3) for op in range(0x18, 0x1C)},
    0x1C: ('MULq', None), 0x1D: ('MAXi', None), 0x1E: ('MULi', None), 0x1F: ('MINIi', None), 0x20: ('ADDq', None),
    0x21: ('MADDq', None), 0x22: ('ADDi', None), 0x23: ('MADDi', None), 0x24: ('SUBq', None), 0x25: ('MSUBq', None),
    0x26: ('SUBi', None), 0x27: ('MSUBi', None), 0x28: ('ADD', None), 0x29: ('MADD', None), 0x2A: ('MUL', None),
    0x2B: ('MAX', None), 0x2C: ('SUB', None), 0x2D: ('MSUB', None), 0x2E: ('OPMSUB', None), 0x2F: ('MINI', None),
}
UPPER_SPECIAL = {
    **{s: ('ADDAbc', s & 3) for s in range(0x00, 0x04)}, **{s: ('SUBAbc', s & 3) for s in range(0x04, 0x08)},
    **{s: ('MADDAbc', s & 3) for s in range(0x08, 0x0C)}, **{s: ('MSUBAbc', s & 3) for s in range(0x0C, 0x10)},
    0x10: ('ITOF0', None), 0x11: ('ITOF4', None), 0x12: ('ITOF12', None), 0x13: ('ITOF15', None),
    0x14: ('FTOI0', None), 0x15: ('FTOI4', None), 0x16: ('FTOI12', None), 0x17: ('FTOI15', None),
    **{s: ('MULAbc', s & 3) for s in range(0x18, 0x1C)}, 0x1C: ('MULAq', None), 0x1D: ('ABS', None),
    0x1E: ('MULAi', None), 0x1F: ('CLIP', None), 0x20: ('ADDAq', None), 0x21: ('MADDAq', None), 0x22: ('ADDAi', None),
    0x23: ('MADDAi', None), 0x24: ('SUBAq', None), 0x25: ('MSUBAq', None), 0x26: ('SUBAi', None),
    0x27: ('MSUBAi', None), 0x28: ('ADDA', None), 0x29: ('MADDA', None), 0x2A: ('MULA', None), 0x2C: ('SUBA', None),
    0x2D: ('MSUBA', None), 0x2E: ('OPMULA', None), 0x2F: ('NOP', None), 0x30: ('NOP', None),
}
LOWER = {0x00: 'LQ', 0x01: 'SQ', 0x04: 'ILW', 0x05: 'ISW', 0x08: 'IADDIU', 0x09: 'ISUBIU', 0x10: 'FCEQ',
         0x11: 'FCSET', 0x12: 'FCAND', 0x13: 'FCOR', 0x14: 'FSEQ', 0x15: 'FSSET', 0x16: 'FSAND', 0x17: 'FSOR',
         0x18: 'FMEQ', 0x1A: 'FMAND', 0x1B: 'FMOR', 0x1C: 'FCGET', 0x20: 'B', 0x21: 'BAL', 0x24: 'JR', 0x25: 'JALR',
         0x28: 'IBEQ', 0x29: 'IBNE', 0x2C: 'IBLTZ', 0x2D: 'IBGTZ', 0x2E: 'IBLEZ', 0x2F: 'IBGEZ'}
LOWER_DIRECT = {0x30: 'IADD', 0x31: 'ISUB', 0x32: 'IADDI', 0x34: 'IAND', 0x35: 'IOR'}
LOWER_SPECIAL = {0x30: 'MOVE', 0x31: 'MR32', 0x34: 'LQI', 0x35: 'SQI', 0x36: 'LQD', 0x37: 'SQD', 0x38: 'DIV',
                 0x39: 'SQRT', 0x3A: 'RSQRT', 0x3B: 'WAITQ', 0x3C: 'MTIR', 0x3D: 'MFIR', 0x3E: 'ILWR', 0x3F: 'ISWR',
                 0x40: 'RNEXT', 0x41: 'RGET', 0x42: 'RINIT', 0x43: 'RXOR', 0x64: 'MFP', 0x68: 'XTOP', 0x69: 'XITOP',
                 0x6C: 'XGKICK', 0x70: 'ESADD', 0x71: 'ERSADD', 0x72: 'ELENG', 0x73: 'ERLENG', 0x74: 'EATANxy',
                 0x75: 'EATANxz', 0x76: 'ESUM', 0x77: 'ERSQRT', 0x78: 'ESQRT', 0x79: 'ESIN', 0x7A: 'ERCPR',
                 0x7B: 'WAITP', 0x7C: 'EATAN', 0x7D: 'EEXP'}
BRANCHES = {'B', 'BAL', 'JR', 'JALR', 'IBEQ', 'IBNE', 'IBLTZ', 'IBGTZ', 'IBLEZ', 'IBGEZ'}


def dest(w): return (w >> 21) & 0xF
def ft(w): return (w >> 16) & 0x1F
def fs(w): return (w >> 11) & 0x1F
def fd(w): return (w >> 6) & 0x1F
def vit(w): return (w >> 16) & 0xF
def vis(w): return (w >> 11) & 0xF
def vid(w): return (w >> 6) & 0xF


def imm11(w):
    v = w & 0x7FF
    return v - 0x800 if v & 0x400 else v


def upper_name(w):
    op = w & 0x3F
    if op >= 0x3C:
        return UPPER_SPECIAL.get((w & 3) | ((w >> 4) & 0x7C), ('RESERVED', None))
    return UPPER.get(op, ('RESERVED', None))


def lower_name(w):
    if w == 0 or w == 0x8000033C:
        return 'NOP'
    op = (w >> 25) & 0x7F
    if op != 0x40:
        return LOWER.get(op, 'RESERVED')
    f = w & 0x3F
    if f < 0x3C:
        return LOWER_DIRECT.get(f, 'RESERVED')
    return LOWER_SPECIAL.get((w & 3) | ((w >> 4) & 0x7C), 'RESERVED')


def pair(img, pc):
    return int.from_bytes(img[pc:pc + 4], 'little'), int.from_bytes(img[pc + 4:pc + 8], 'little')


def describe(img, pc):
    lo, up = pair(img, pc)
    un, bc = upper_name(up)
    bits = ''.join(c for c, b in (('I', 31), ('E', 30), ('M', 29), ('D', 28), ('T', 27)) if (up >> b) & 1)
    u = f"{un}{'xyzw'[bc] if bc is not None else ''}.{dest(up):x} fd{fd(up)} fs{fs(up)} ft{ft(up)}"
    if (up >> 31) & 1:
        import struct
        l = f"I={struct.unpack('<f', lo.to_bytes(4, 'little'))[0]!r}"
    else:
        ln = lower_name(lo)
        l = f"{ln} dest{dest(lo):x} ft/it{ft(lo)} fs/is{fs(lo)} id{vid(lo)} imm11={imm11(lo)}"
    return f"{pc:04x}: [{bits:5}] {u:40} | {l}"
