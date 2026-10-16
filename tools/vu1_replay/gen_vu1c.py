"""GAMEPLAY_001K: VU1 microprogram -> C++ generator ("compiled scoreboard").

Usage: python gen_vu1c.py <code_image.bin> <out.cpp> <kind:entry_pc> [<kind:entry_pc> ...]
       kind 0 = MSCAL execute, 1 = MSCNT resume; entry_pc hex (e.g. 1:0x300)

The output reproduces PS2Recomp's VU1Interpreter for the paths reachable from each entry until the
E-bit delay slot. Instruction usage, latencies and stall rules are ported from
VU1Interpreter::decodeUpperUsage/decodeLowerUsage/calculatePairReadyCycle/markPairWrites; values from
execUpper/execLower. An entry whose reachable code contains anything outside the supported subset is
not emitted, so the caller falls back to the interpreter. The output derives from game microcode and
must stay out of version control.
"""
import struct, sys
from pathlib import Path
from vu1isa import pair, upper_name, lower_name, dest, ft, fs, fd, vit, vis, vid, imm11

CODE_SIZE = 0x4000
FMAC_LATENCY = 4
LANES = 'xyzw'


def lane_bit(c):  # laneForComponent
    return 1 << (3 - c)


def comps(mask):
    return [c for c in range(4) if mask & lane_bit(c)]


class Unsupported(Exception):
    pass


class Usage:
    def __init__(self):
        self.vf_read = []  # [(reg, lanes)] max 2 entries, merged by reg (addVfRead)
        self.vf_write = (0, 0)
        self.vi_read = set()
        self.vi_write = []  # in register order
        self.acc_read = 0
        self.acc_write = 0
        self.latency = 0
        self.vf_latency = 0
        self.vi_latency = 0
        self.pipeline = None
        self.wait_q = False
        self.delays_branch = False
        self.writes_clip = False
        self.reads_clip = False

    def add_vf_read(self, reg, lanes):
        if lanes == 0:
            return
        for i, (r, l) in enumerate(self.vf_read):
            if r == reg:
                self.vf_read[i] = (r, l | lanes)
                return
        if len(self.vf_read) < 2:
            self.vf_read.append((reg, lanes))

    def add_vf_write(self, reg, lanes):
        if reg == 0 or lanes == 0:
            return
        if self.vf_write[0] == 0:
            self.vf_write = (reg, lanes)
        elif self.vf_write[0] == reg:
            self.vf_write = (reg, self.vf_write[1] | lanes)

    def read_vi(self, r):
        if r:
            self.vi_read.add(r)

    def write_vi(self, r):
        if r and r not in self.vi_write:
            self.vi_write.append(r)
            self.vi_write.sort()

    def vf_read_lanes(self, reg):
        return sum(l for r, l in self.vf_read if r == reg)


def upper_usage(w):
    u = Usage()
    u.pipeline = 'fmac'
    u.latency = FMAC_LATENCY
    op = w & 0x3F
    d, s, t, dd = dest(w), fs(w), ft(w), fd(w)
    if op <= 0x2F:
        u.add_vf_read(s, d)
        u.add_vf_write(dd, d)
        if op <= 0x1B:
            u.add_vf_read(t, lane_bit(op & 3))
        elif op >= 0x28:
            u.add_vf_read(t, 0xE if op == 0x2E else d)
        if op in (0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x21, 0x23, 0x25, 0x27, 0x29, 0x2D, 0x2E):
            u.acc_read = d
        return u
    if op >= 0x3C:
        sp = (w & 3) | ((w >> 4) & 0x7C)
        writes_acc = sp <= 0x0F or 0x18 <= sp <= 0x1C or sp == 0x1E or 0x20 <= sp <= 0x2A or 0x2C <= sp <= 0x2E
        if writes_acc:
            u.add_vf_read(s, d)
            if sp <= 0x1B:
                u.add_vf_read(t, lane_bit(sp & 3))
            elif 0x28 <= sp <= 0x2E:
                u.add_vf_read(t, 0xE if sp == 0x2E else d)
            u.acc_write = d
            if 0x08 <= sp <= 0x0F or sp in (0x21, 0x23, 0x25, 0x27, 0x29, 0x2D):
                u.acc_read = d
        elif 0x10 <= sp <= 0x17 or sp == 0x1D:
            u.add_vf_read(s, d)
            u.add_vf_write(t, d)
        elif sp == 0x1F:
            u.add_vf_read(s, 0xE)
            u.add_vf_read(t, 0x1)
            u.writes_clip = True
        elif sp not in (0x2F, 0x30):
            raise Unsupported(f'reserved upper {w:08x}')
        return u
    raise Unsupported(f'reserved upper {w:08x}')


def lower_usage(w):
    u = Usage()
    if w == 0 or w == 0x8000033C:
        return u
    op = (w >> 25) & 0x7F
    t, s, it, is_, id_, d = ft(w), fs(w), vit(w), vis(w), vid(w), dest(w)
    if op == 0x00:
        u.pipeline, u.latency = 'lsu', 4; u.read_vi(is_); u.add_vf_write(t, d)
    elif op == 0x01:
        u.pipeline, u.latency = 'lsu', 1; u.read_vi(it); u.add_vf_read(s, d)
    elif op == 0x04:
        u.pipeline, u.latency = 'lsu', 4; u.read_vi(is_); u.write_vi(it)
    elif op == 0x05:
        u.pipeline, u.latency = 'lsu', 1; u.read_vi(is_); u.read_vi(it)
    elif op in (0x08, 0x09):
        u.pipeline, u.latency, u.delays_branch = 'ialu', 1, True; u.read_vi(is_); u.write_vi(it)
    elif op in (0x10, 0x12, 0x13):
        u.pipeline, u.latency, u.reads_clip = 'ialu', 1, True; u.write_vi(1)
    elif op == 0x1C:
        u.pipeline, u.latency, u.reads_clip = 'ialu', 1, True; u.write_vi(it)
    elif op == 0x20:
        u.pipeline = 'branch'
    elif op in (0x28, 0x29):
        u.pipeline = 'branch'; u.read_vi(is_); u.read_vi(it)
    elif op in (0x2C, 0x2D, 0x2E, 0x2F):
        u.pipeline = 'branch'; u.read_vi(is_)
    elif op == 0x40:
        f = w & 0x3F
        if f in (0x30, 0x31, 0x34, 0x35):
            u.pipeline, u.latency, u.delays_branch = 'ialu', 1, True; u.read_vi(is_); u.read_vi(it); u.write_vi(id_)
        elif f == 0x32:
            u.pipeline, u.latency, u.delays_branch = 'ialu', 1, True; u.read_vi(is_); u.write_vi(it)
        elif f < 0x3C:
            raise Unsupported(f'reserved lower {w:08x}')
        else:
            sp = (w & 3) | ((w >> 4) & 0x7C)
            if sp in (0x30, 0x31):
                u.pipeline, u.latency = 'fmac', 4; u.add_vf_read(s, 0xF if sp == 0x31 else d); u.add_vf_write(t, d)
            elif sp in (0x34, 0x36):
                u.pipeline, u.latency, u.vi_latency, u.delays_branch = 'lsu', 4, 1, True
                u.read_vi(is_); u.write_vi(is_); u.add_vf_write(t, d)
            elif sp in (0x35, 0x37):
                u.pipeline, u.latency, u.delays_branch = 'lsu', 1, True; u.read_vi(it); u.write_vi(it); u.add_vf_read(s, d)
            elif sp == 0x38:
                u.pipeline, u.latency = 'fdiv', 7
                u.add_vf_read(s, lane_bit((w >> 21) & 3)); u.add_vf_read(t, lane_bit((w >> 23) & 3))
            elif sp == 0x3B:
                u.pipeline, u.wait_q = 'fdiv', True
            elif sp == 0x3C:
                u.pipeline, u.latency, u.delays_branch = 'ialu', 1, True; u.add_vf_read(s, lane_bit((w >> 21) & 3)); u.write_vi(it)
            elif sp == 0x3D:
                u.pipeline, u.latency = 'fmac', 4; u.read_vi(is_); u.add_vf_write(t, d)
            elif sp == 0x3E:
                u.pipeline, u.latency = 'lsu', 4; u.read_vi(is_); u.write_vi(it)
            elif sp == 0x3F:
                u.pipeline, u.latency = 'lsu', 1; u.read_vi(is_); u.read_vi(it)
            elif sp in (0x68, 0x69):
                u.pipeline, u.latency = 'ialu', 1; u.write_vi(it)
            elif sp == 0x6C:
                u.pipeline, u.latency = 'xgkick', 2; u.read_vi(is_)
            else:
                raise Unsupported(f'lower special {sp:02x} not compiled ({w:08x})')
    else:
        raise Unsupported(f'lower op {op:02x} not compiled ({w:08x})')
    return u


SUPPORTED_UPPER = {'NOP', 'ADDbc', 'SUBbc', 'MADDbc', 'MSUBbc', 'MULbc', 'MAXbc', 'MINIbc', 'MULq', 'MULi', 'MAXi',
                   'MINIi', 'ADDq', 'ADDi', 'SUBq', 'SUBi', 'MADDq', 'MADDi', 'MSUBq', 'MSUBi', 'ADD', 'SUB', 'MUL',
                   'MADD', 'MSUB', 'MAX', 'MINI', 'ADDAbc', 'SUBAbc', 'MADDAbc', 'MSUBAbc', 'MULAbc', 'MULAq', 'MULAi',
                   'ADDAq', 'ADDAi', 'SUBAq', 'SUBAi', 'MADDAq', 'MADDAi', 'MSUBAq', 'MSUBAi', 'ADDA', 'SUBA', 'MULA',
                   'MADDA', 'MSUBA', 'ITOF0', 'ITOF4', 'ITOF12', 'ITOF15', 'FTOI0', 'FTOI4', 'FTOI12', 'FTOI15', 'ABS',
                   'CLIP'}
SUPPORTED_LOWER = {'NOP', 'LQ', 'SQ', 'ILW', 'ISW', 'IADDIU', 'ISUBIU', 'FCAND', 'FCEQ', 'FCOR', 'FCGET', 'B', 'IBEQ',
                   'IBNE', 'IBLTZ', 'IBGTZ', 'IBLEZ', 'IBGEZ', 'IADD', 'ISUB', 'IADDI', 'IAND', 'IOR', 'MOVE', 'MR32',
                   'LQI', 'SQI', 'LQD', 'SQD', 'DIV', 'WAITQ', 'MTIR', 'MFIR', 'ILWR', 'ISWR', 'XTOP', 'XITOP', 'XGKICK'}
COND_BRANCH = {'IBEQ', 'IBNE', 'IBLTZ', 'IBGTZ', 'IBLEZ', 'IBGEZ'}


class Pair:
    def __init__(self, img, pc):
        self.pc = pc
        self.lo, self.up = pair(img, pc)
        self.ibit, self.ebit = bool(self.up >> 31 & 1), bool(self.up >> 30 & 1)
        self.dbit, self.tbit = bool(self.up >> 28 & 1), bool(self.up >> 27 & 1)
        self.uname, self.ubc = upper_name(self.up)
        self.lname = 'NOP' if self.ibit else lower_name(self.lo)
        if self.dbit or self.tbit:
            raise Unsupported(f'{pc:04x}: D/T bit')
        if self.uname not in SUPPORTED_UPPER:
            raise Unsupported(f'{pc:04x}: upper {self.uname}')
        if self.lname not in SUPPORTED_LOWER:
            raise Unsupported(f'{pc:04x}: lower {self.lname}')
        self.uu = upper_usage(self.up)
        self.lu = Usage() if self.ibit else lower_usage(self.lo)
        self.is_branch = self.lname in COND_BRANCH or self.lname == 'B'
        uw = self.uu.vf_write[0]
        self.shadow = uw != 0 and (self.lu.vf_read_lanes(uw) != 0 or self.lu.vf_write[0] == uw)
        self.suppress_lower = self.shadow and self.lu.vf_write[0] == uw
        self.target = (pc + 8 + imm11(self.lo) * 8) & 0x3FFF if self.is_branch else None


def reachable(img, entry):
    seen, order, work = {}, [], [entry]
    while work:
        pc = work.pop()
        if pc in seen:
            continue
        if pc + 8 > CODE_SIZE:
            raise Unsupported(f'{pc:04x}: wraps past the end of micro memory')
        p = Pair(img, pc)
        seen[pc] = p
        order.append(pc)
        if p.ebit:
            if p.is_branch:
                raise Unsupported(f'{pc:04x}: E-bit on a branch')
            d = Pair(img, pc + 8)
            if d.is_branch or d.ebit:
                raise Unsupported(f'{pc + 8:04x}: branch/E-bit in the E-bit delay slot')
            if d.lu.delays_branch and d.lu.vi_write:
                # The interpreter would carry this pair's branch-VI backup into the next MSCNT; the
                # runtime dispatcher resynchronises the interpreter with a scheduler reset, which
                # clears it, so such programs cannot hand over to the interpreter exactly.
                raise Unsupported(f'{pc + 8:04x}: E-bit delay slot leaves a branch-VI backup')
            seen[pc + 8] = d
            continue  # program ends after the delay slot
        if p.is_branch:
            d = Pair(img, pc + 8)
            if d.is_branch or d.ebit:
                raise Unsupported(f'{pc + 8:04x}: branch/E-bit in a branch delay slot')
            work.append(pc + 8)
            work.append(p.target)
            if p.lname in COND_BRANCH:
                work.append(pc + 16)
        else:
            work.append(pc + 8)
    return seen


def f32(v):
    return f'vu1c::bitsToFloat(0x{struct.unpack("<I", struct.pack("<f", v))[0]:08x}u)'


class Emitter:
    def __init__(self):
        self.lines = []

    def __call__(self, s=''):
        self.lines.append(s)


def emit_ready(e, p):
    terms = []
    for usage in (p.uu, p.lu):
        for reg, lanes in usage.vf_read:
            if reg == 0:
                continue  # vf0 is never marked by markPairWrites
            for c in comps(lanes):
                terms.append(f'vfR[{reg}][{c}]')
        for r in sorted(usage.vi_read):
            terms.append(f'viR[{r}]')
        for c in comps(usage.acc_read):
            terms.append(f'accR[{c}]')
    for t in dict.fromkeys(terms):
        e(f'        if ({t} > cyc) cyc = {t};')
    if p.lu.pipeline == 'fdiv':  # DIV/SQRT/RSQRT and WAITQ wait for a pending Q
        e('        if (fdv.valid && fdv.ready > cyc) cyc = fdv.ready;')
    if p.lname == 'XGKICK':
        e('        xg.advance(c, cyc);')
        e('        while (xg.active) { ++cyc; xg.advance(c, cyc); }')


def q_operand(e):
    e('        if (fdv.valid && fdv.ready <= cyc) commitQ();')


ACC_ROOTS = ('MADD', 'MSUB', 'ADD', 'SUB', 'MUL')


def split_fmac(name):
    """('MADDAbc') -> ('MADD', 'bc', True); ('MULq') -> ('MUL', 'q', False)."""
    for root in ACC_ROOTS:
        if name.startswith(root):
            rest = name[len(root):]
            to_acc = rest.startswith('A')
            if to_acc:
                rest = rest[1:]
            if rest in ('', 'bc', 'q', 'i'):
                return root, rest, to_acc
    raise Unsupported(name)


def fmac_parts(name, c):
    """Float expression, exact (long double) expression and product-sum right operand for lane c."""
    root, suffix, _ = split_fmac(name)
    vs = f'vs{c}'
    o = {'bc': 'bcv', 'q': 'qv', 'i': 'iv', '': f'vt{c}'}[suffix]
    L = lambda x: f'(long double){x}'
    if root == 'ADD':
        return f'{vs} + {o}', f'{L(vs)} + {L(o)}', None
    if root == 'SUB':
        return f'{vs} - {o}', f'{L(vs)} - {L(o)}', None
    if root == 'MUL':
        return f'{vs} * {o}', f'{L(vs)} * {L(o)}', None
    if root == 'MADD':
        return f'ac{c} + {vs} * {o}', f'{L(f"ac{c}")} + {L(vs)} * {L(o)}', o
    return f'ac{c} - {vs} * {o}', f'{L(f"ac{c}")} - {L(vs)} * {L(o)}', o


FMAC_NAMES = {'ADDbc', 'SUBbc', 'MADDbc', 'MSUBbc', 'MULbc', 'MULq', 'MULi', 'ADDq', 'ADDi', 'SUBq', 'SUBi', 'MADDq',
              'MADDi', 'MSUBq', 'MSUBi', 'ADD', 'SUB', 'MUL', 'MADD', 'MSUB', 'ADDAbc', 'SUBAbc', 'MADDAbc', 'MSUBAbc',
              'MULAbc', 'MULAq', 'MULAi', 'ADDAq', 'ADDAi', 'SUBAq', 'SUBAi', 'MADDAq', 'MADDAi', 'MSUBAq', 'MSUBAi',
              'ADDA', 'SUBA', 'MULA', 'MADDA', 'MSUBA'}


def emit_upper(e, p):
    """Compute the upper result into temporaries uN[4]; returns (target, reg, lanes) or None."""
    n, w = p.uname, p.up
    d, s, t, dd = dest(w), fs(w), ft(w), fd(w)
    if n == 'NOP':
        return None
    if n in FMAC_NAMES:
        _, suffix, to_acc = split_fmac(n)
        lanes = comps(d)
        if not lanes:
            # dest 0: result and flags are discarded by the interpreter (updateFmacFlags returns early)
            return None
        uses_q = suffix == 'q'
        uses_i = suffix == 'i'
        if uses_q:
            q_operand(e)
            e('        const float qv = vu1c::nrm(s_q);')
        if uses_i:
            e('        const float iv = vu1c::nrm(s_i);')
        if suffix == 'bc':
            e(f'        const float bcv = vu1c::nrm(vf[{t}][{p.ubc}]);')
        e('        uint32_t mac = 0, cur = 0, extra = 0;')
        for c in lanes:
            e(f'        const float vs{c} = vu1c::nrm(vf[{s}][{c}]);')
            if suffix == '':
                e(f'        const float vt{c} = vu1c::nrm(vf[{t}][{c}]);')
            fx, ex, right = fmac_parts(n, c)
            if 'ac' in fx:
                e(f'        const float ac{c} = vu1c::nrm(acc[{c}]);')
            e(f'        float u{c} = {fx};')
            e(f'        {{ const uint32_t f = vu1c::exactFlags(u{c}, {ex}); mac |= vu1c::macBits({lane_bit(c)}u, f); cur |= f; }}')
            if right is not None:
                e(f'        extra |= vu1c::productSticky(vs{c}, {right});')
        e('        fl.issue(mac, cur, extra);')
        e(f'        if (cyc + {FMAC_LATENCY} > pend) pend = cyc + {FMAC_LATENCY};')
        return ('acc', None, d) if to_acc else ('vf', dd, d)
    if n in ('MAXbc', 'MINIbc', 'MAXi', 'MINIi', 'MAX', 'MINI'):
        lanes = comps(d)
        if not lanes:
            return None
        cmp = '>' if n.startswith('MAX') else '<'
        if n.endswith('bc'):
            e(f'        const float bcv = vu1c::nrm(vf[{t}][{p.ubc}]);')
        if n.endswith('i'):
            e('        const float iv = vu1c::nrm(s_i);')
        for c in lanes:
            e(f'        const float vs{c} = vu1c::nrm(vf[{s}][{c}]);')
            o = 'bcv' if n.endswith('bc') else 'iv' if n.endswith('i') else f'vt{c}'
            if o.startswith('vt'):
                e(f'        const float vt{c} = vu1c::nrm(vf[{t}][{c}]);')
            e(f'        float u{c} = (vs{c} {cmp} {o}) ? vs{c} : {o};')
        return ('vf', dd, d)
    if n.startswith('ITOF'):
        scale = {'ITOF0': None, 'ITOF4': '16.0f', 'ITOF12': '4096.0f', 'ITOF15': '32768.0f'}[n]
        for c in comps(d):
            e(f'        int32_t iv{c}; std::memcpy(&iv{c}, &vf[{s}][{c}], 4);')
            e(f'        float u{c} = static_cast<float>(iv{c}){" / " + scale if scale else ""};')
        return ('vf', t, d) if comps(d) else None
    if n.startswith('FTOI'):
        scale = {'FTOI0': '1.0f', 'FTOI4': '16.0f', 'FTOI12': '4096.0f', 'FTOI15': '32768.0f'}[n]
        for c in comps(d):
            e(f'        float u{c}; {{ const int32_t i = vu1c::floatToInt(vu1c::nrm(vf[{s}][{c}]), {scale}); std::memcpy(&u{c}, &i, 4); }}')
        return ('vf', t, d) if comps(d) else None
    if n == 'ABS':
        for c in comps(d):
            e(f'        float u{c} = std::fabs(vu1c::nrm(vf[{s}][{c}]));')
        return ('vf', t, d) if comps(d) else None
    if n == 'CLIP':
        e(f'        {{ const uint32_t wb = vu1c::floatToBits(vf[{t}][3]);')
        e('          const int32_t lim = (wb & 0x7F800000u) != 0u ? static_cast<int32_t>(wb & 0x7FFFFFFFu) : 0x007FFFFF;')
        e('          uint32_t cf = 0;')
        for c, (pos, neg) in enumerate(((0x01, 0x02), (0x04, 0x08), (0x10, 0x20))):
            e(f'          if (vu1c::clipExceeds(vf[{s}][{c}], 0u, lim)) cf |= 0x{pos:02x}u;')
            e(f'          if (vu1c::clipExceeds(vf[{s}][{c}], 0x80000000u, lim)) cf |= 0x{neg:02x}u;')
        e('          workingClip = ((workingClip << 6) | (cf & 0x3Fu)) & 0xFFFFFFu;')
        e(f'          if (!clipq.push(cyc + {FMAC_LATENCY}, workingClip)) return vu1c::Fault; }}')
        e(f'        if (cyc + {FMAC_LATENCY} > pend) pend = cyc + {FMAC_LATENCY};')
        return None
    raise Unsupported(n)


def vi_read(reg):
    return '0' if reg == 0 else f'vi[{reg}]'


def emit_lower(e, p):
    """Execute the lower instruction against the pre-pair state. Returns a dict describing writes."""
    n, w = p.lname, p.lo
    out = {}
    t, s, it, is_, id_, d = ft(w), fs(w), vit(w), vis(w), vid(w), dest(w)
    mask = 'dmask'
    if n == 'NOP':
        return out
    if n in ('LQ', 'LQI', 'LQD'):
        if n == 'LQ':
            e(f'        const uint32_t la = (static_cast<uint32_t>(static_cast<int32_t>({vi_read(is_)} + ({imm11(w)}))) * 16u) & {mask};')
        elif n == 'LQI':
            e(f'        const uint32_t la = (static_cast<uint32_t>(static_cast<uint16_t>({vi_read(is_)})) * 16u) & {mask};')
            if is_:
                out['vi'] = (is_, f'static_cast<int16_t>(vi[{is_}] + 1)')
        else:
            if is_:
                out['vi'] = (is_, f'static_cast<int16_t>(vi[{is_}] - 1)')
            e(f'        const uint32_t la = (static_cast<uint32_t>(static_cast<uint16_t>({"static_cast<int16_t>(vi[%d] - 1)" % is_ if is_ else "0"})) * 16u) & {mask};')
        e('        float lv[4]; std::memcpy(lv, c.data + la, 16);')
        out['vf'] = (t, d, 'lv')
        return out
    if n in ('SQ', 'SQI', 'SQD', 'ISW', 'ISWR'):
        if n == 'SQ':
            e(f'        const uint32_t sa = (static_cast<uint32_t>(static_cast<int32_t>({vi_read(it)} + ({imm11(w)}))) * 16u) & {mask};')
        elif n == 'SQI':
            e(f'        const uint32_t sa = (static_cast<uint32_t>(static_cast<uint16_t>({vi_read(it)})) * 16u) & {mask};')
            if it:
                out['vi'] = (it, f'static_cast<int16_t>(vi[{it}] + 1)')
        elif n == 'SQD':
            if it:
                out['vi'] = (it, f'static_cast<int16_t>(vi[{it}] - 1)')
            e(f'        const uint32_t sa = (static_cast<uint32_t>(static_cast<uint16_t>({"static_cast<int16_t>(vi[%d] - 1)" % it if it else "0"})) * 16u) & {mask};')
        elif n == 'ISW':
            e(f'        const uint32_t sa = (static_cast<uint32_t>(static_cast<int32_t>({vi_read(is_)} + ({imm11(w)}))) * 16u) & {mask};')
        else:
            e(f'        const uint32_t sa = (static_cast<uint32_t>(static_cast<uint16_t>({vi_read(is_)})) * 16u) & {mask};')
        if n in ('ISW', 'ISWR'):
            e(f'        const uint32_t sv = static_cast<uint32_t>(static_cast<uint16_t>({vi_read(it)} & 0xFFFF));')
            e('        const uint32_t sw[4] = {sv, sv, sv, sv};')
        else:
            e(f'        uint32_t sw[4]; std::memcpy(sw, vf[{s}], 16);')
        out['store'] = d
        return out
    if n in ('ILW', 'ILWR'):
        comp = 0 if d & 8 else 1 if d & 4 else 2 if d & 2 else 3
        if n == 'ILW':
            e(f'        const uint32_t la = (static_cast<uint32_t>(static_cast<int32_t>({vi_read(is_)} + ({imm11(w)}))) * 16u) & {mask};')
        else:
            e(f'        const uint32_t la = (static_cast<uint32_t>(static_cast<uint16_t>({vi_read(is_)})) * 16u) & {mask};')
        e(f'        uint32_t lw; std::memcpy(&lw, c.data + la + {comp * 4}, 4);')
        if it:
            out['vi'] = (it, 'static_cast<int32_t>(static_cast<int16_t>(lw & 0xFFFF))')
        return out
    if n in ('IADDIU', 'ISUBIU'):
        imm = (w & 0x7FF) | ((w >> 10) & 0x7800)
        if it:
            out['vi'] = (it, f'static_cast<int16_t>({vi_read(is_)} {"+" if n == "IADDIU" else "-"} {imm})')
        return out
    if n in ('IADD', 'ISUB', 'IAND', 'IOR'):
        o = {'IADD': '+', 'ISUB': '-', 'IAND': '&', 'IOR': '|'}[n]
        expr = f'({vi_read(is_)} {o} {vi_read(it)})'
        if id_:
            out['vi'] = (id_, f'static_cast<int16_t>{expr}' if n in ('IADD', 'ISUB') else expr)
        return out
    if n == 'IADDI':
        imm5 = ((w >> 6) & 0x1F)
        imm5 = imm5 - 32 if imm5 & 0x10 else imm5
        if it:
            out['vi'] = (it, f'static_cast<int16_t>({vi_read(is_)} + ({imm5}))')
        return out
    if n in ('MOVE', 'MR32'):
        if n == 'MOVE':
            e(f'        float lv[4]; std::memcpy(lv, vf[{s}], 16);')
        else:
            e(f'        const float lv[4] = {{vf[{s}][1], vf[{s}][2], vf[{s}][3], vf[{s}][0]}};')
        out['vf'] = (t, d, 'lv')
        return out
    if n == 'MFIR':
        e(f'        float lv[4]; {{ const int32_t v = static_cast<int32_t>(static_cast<int16_t>({vi_read(is_)} & 0xFFFF)); std::memcpy(&lv[0], &v, 4); lv[1] = lv[2] = lv[3] = lv[0]; }}')
        out['vf'] = (t, d, 'lv')
        return out
    if n == 'MTIR':
        comp = (w >> 21) & 3
        if it:
            out['vi'] = (it, f'static_cast<int32_t>(static_cast<int16_t>(vu1c::floatToBits(vf[{s}][{comp}]) & 0xFFFF))')
        return out
    if n == 'DIV':
        fsf, ftf = (w >> 21) & 3, (w >> 23) & 3
        e(f'        if (fdv.valid) commitQ();')
        e(f'        {{ const float num = vu1c::nrm(vf[{s}][{fsf}]); const float den = vu1c::nrm(vf[{t}][{ftf}]);')
        e('          uint32_t di = 0; float r = 0.0f;')
        e('          if (den == 0.0f) { di = num == 0.0f ? 0x10u : 0x20u; r = std::signbit(num) != std::signbit(den) ? -std::numeric_limits<float>::max() : std::numeric_limits<float>::max(); }')
        e('          else r = num / den;')
        e('          r = vu1c::nrmResult(r);')
        e('          fdv.valid = true; fdv.ready = cyc + 7; fdv.value = vu1c::nrmResult(r); fdv.di = di & 0x30u; }')
        e('        if (cyc + 7 > pend) pend = cyc + 7;')
        return out
    if n == 'WAITQ':
        return out
    if n in ('FCAND', 'FCEQ', 'FCOR', 'FCGET'):
        e('        clipq.commit(cyc, s_clip);')
        imm = w & 0xFFFFFF
        if n == 'FCAND':
            out['vi'] = (1, f'((s_clip & 0x{imm:06x}u) != 0u) ? 1 : 0')
        elif n == 'FCEQ':
            out['vi'] = (1, f'((s_clip & 0xFFFFFFu) == 0x{imm:06x}u) ? 1 : 0')
        elif n == 'FCOR':
            out['vi'] = (1, f'((s_clip | 0x{imm:06x}u) == 0xFFFFFFu) ? 1 : 0')
        elif it:
            out['vi'] = (it, 'static_cast<int32_t>(s_clip & 0x0FFFu)')
        return out
    if n in ('XTOP', 'XITOP'):
        if it:
            out['vi'] = (it, f'static_cast<int32_t>(c.{"top" if n == "XTOP" else "itop"} & 0x3FFu)')
        return out
    if n == 'XGKICK':
        out['xgkick'] = f'static_cast<uint32_t>(static_cast<uint16_t>({vi_read(is_)}))'
        return out
    if n in COND_BRANCH or n == 'B':
        rb = lambda r: '0' if r == 0 else f'((bkValid && bkReg == {r}) ? bkVal : vi[{r}])'
        cond = {'B': 'true',
                'IBEQ': f'static_cast<int16_t>({rb(is_)}) == static_cast<int16_t>({rb(it)})',
                'IBNE': f'static_cast<int16_t>({rb(is_)}) != static_cast<int16_t>({rb(it)})',
                'IBLTZ': f'static_cast<int16_t>({rb(is_)}) < 0', 'IBGTZ': f'static_cast<int16_t>({rb(is_)}) > 0',
                'IBLEZ': f'static_cast<int16_t>({rb(is_)}) <= 0', 'IBGEZ': f'static_cast<int16_t>({rb(is_)}) >= 0'}[n]
        out['branch'] = cond
        return out
    raise Unsupported(n)


def emit_pair(e, p, img, pairs, order_next, is_delay_of, entry_set):
    pc = p.pc
    e(f'L_{pc:04x}: // {p.uname}{"xyzw"[p.ubc] if p.ubc is not None else ""} | {p.lname}{" [I]" if p.ibit else ""}{" [E]" if p.ebit else ""}')
    e('    {')
    emit_ready(e, p)
    e('        if (cyc >= c.maxCycles) return vu1c::Budget;')
    ut = emit_upper(e, p)
    # Upper writes to vf0 are applied immediately by the interpreter (never reverted), so a lower
    # instruction in the same pair observes them; every other upper write is deferred.
    if ut and ut[0] == 'vf' and ut[1] == 0:
        for cc in comps(ut[2]):
            e(f'        vf[0][{cc}] = u{cc};')
        vf0_dirty = True
    else:
        vf0_dirty = False
    lw = emit_lower(e, p)
    if p.ibit:
        e(f'        s_i = vu1c::nrm({f32(struct.unpack("<f", p.lo.to_bytes(4, "little"))[0])});')
    # branch condition is evaluated before VI writes of this pair are recorded
    if 'branch' in lw:
        e(f'        const bool taken = {lw["branch"]};')
    # --- apply writes (program order: lower, then upper; a lower VF write to the upper's register is dropped)
    if 'vf' in lw:
        reg, dmask_, src = lw['vf']
        if not p.suppress_lower:
            for cc in comps(dmask_):
                e(f'        vf[{reg}][{cc}] = {src}[{cc}];')
            if reg == 0:
                vf0_dirty = True
    if 'store' in lw:
        e('        xg.advance(c, cyc);')
        for cc in comps(lw['store']):
            e(f'        std::memcpy(c.data + sa + {cc * 4}, &sw[{cc}], 4);')
        e('        if (cyc + 1 > pend) pend = cyc + 1;')
    if 'xgkick' in lw:
        e(f'        xg.start({lw["xgkick"]}, cyc, c.dataSize);')
    if ut:
        kind, reg, dm = ut
        if kind == 'acc':
            for cc in comps(dm):
                e(f'        acc[{cc}] = u{cc};')
        elif reg != 0:
            for cc in comps(dm):
                e(f'        vf[{reg}][{cc}] = u{cc};')
    # VI write (first register in the usage mask) and branch backup bookkeeping
    if 'vi' in lw:
        r, expr = lw['vi']
        rec = p.lu.delays_branch and r in p.lu.vi_write
        if rec:
            e(f'        const int32_t oldVi = vi[{r}];')
        e(f'        vi[{r}] = static_cast<int32_t>(static_cast<int16_t>({expr}));')
        if rec:
            e(f'        bkValid = true; bkReg = {r}; bkVal = oldVi;')
        else:
            e('        bkValid = false;')
    else:
        e('        bkValid = false;')
    # ready stamps (markPairWrites)
    lwr, lwl = p.lu.vf_write
    if lwr and not p.suppress_lower:
        lat = p.lu.vf_latency or p.lu.latency
        for cc in comps(lwl):
            e(f'        vfR[{lwr}][{cc}] = cyc + {lat};')
        e(f'        if (cyc + {lat} > pend) pend = cyc + {lat};')
    uwr, uwl = p.uu.vf_write
    if uwr:
        for cc in comps(uwl):
            e(f'        vfR[{uwr}][{cc}] = cyc + {FMAC_LATENCY};')
        e(f'        if (cyc + {FMAC_LATENCY} > pend) pend = cyc + {FMAC_LATENCY};')
    for r in p.lu.vi_write:
        lat = p.lu.vi_latency or p.lu.latency
        e(f'        viR[{r}] = cyc + {lat};')
        e(f'        if (cyc + {lat} > pend) pend = cyc + {lat};')
    if p.uu.acc_write:
        for cc in comps(p.uu.acc_write):
            e(f'        accR[{cc}] = cyc + 1;')
        e('        if (cyc + 1 > pend) pend = cyc + 1;')
    if vf0_dirty or pc in entry_set:
        e('        vf[0][0] = 0.0f; vf[0][1] = 0.0f; vf[0][2] = 0.0f; vf[0][3] = 1.0f; vi[0] = 0;')
    e('        ++cyc;')
    # control flow
    if p.is_branch:
        e(f'        if (taken) {{ brPending = true; brTarget = 0x{p.target:04x}u; }}')
    nxt = pc + 8
    if pc in is_delay_of:
        b = pairs[is_delay_of[pc]]
        back = b.target <= is_delay_of[pc]
        e(f'        if (brPending) {{ brPending = false; s_branchTarget = 0x{b.target:04x}u; s_branchDelay = 0u; tookBranch = true;')
        if back:
            e('            if (cyc >= c.maxCycles) return vu1c::Budget;')
        e(f'            goto L_{b.target:04x}; }}')
    # interpreter: a pending E-bit (set by the previous pair) ends the program after this pair;
    # this pair's own E-bit only arms it
    e(f'        if (eSeen) {{ endPc = 0x{(nxt & 0x3FFF):04x}u; goto L_END; }}')
    if p.ebit:
        e('        eSeen = true;')
    if nxt not in pairs:
        # only the E-bit delay slot has no compiled successor; reaching it without a pending E-bit
        # leaves the compiled region, so the partial result is discarded and the interpreter runs
        e('        return vu1c::Fault;')
    elif order_next != nxt:
        e(f'        goto L_{nxt:04x};')
    e('    }')


def generate(img, entries, name):
    pairs = {}
    compiled, rejected = [], []
    for kind, pc in entries:
        try:
            pairs.update(reachable(img, pc))
            compiled.append((kind, pc))
        except Unsupported as ex:
            rejected.append((kind, pc, str(ex)))
    order = sorted(pairs)
    is_delay_of = {pc + 8: pc for pc, p in pairs.items() if p.is_branch}
    entry_set = {pc for _, pc in compiled}
    e = Emitter()
    e('// Generated by tools/vu1_replay/gen_vu1c.py from game microcode. Do not commit.')
    e('#include "vu/ps2_vu1_native.h"')
    e('')
    e(f'static int {name}(vu1c::Ctx &c)')
    e('{')
    e('    VU1State &st = *c.st;')
    e('    const uint32_t entry = c.kind == 0 ? (c.startPC & 0x3FFFu) : st.pc;')
    e('    switch (entry)')
    e('    {')
    for kind, pc in compiled:
        e(f'    case 0x{pc:04x}u: break;')
    e('    default: return vu1c::UnsupportedEntry;')
    e('    }')
    e('    if (c.kind != 0 && (st.ebit || st.haltAfterDelaySlot || st.branchPending))')
    e('        return vu1c::Precondition;')
    e('    if (c.kind == 0)')
    e('    {')
    e('        st.pc = c.startPC & 0x3FFFu; st.ebit = false; st.haltAfterDelaySlot = false;')
    e('        st.branchPending = false; st.branchTarget = 0; st.branchDelay = 0;')
    e('        st.vf[0][0] = 0.0f; st.vf[0][1] = 0.0f; st.vf[0][2] = 0.0f; st.vf[0][3] = 1.0f;')
    e('    }')
    e('    st.top = c.top; st.itop = c.itop; st.stoppedByD = false; st.stoppedByT = false;')
    e('    const vu1c::RoundingScope rounding;')
    e('    float vf[32][4]; std::memcpy(vf, st.vf, sizeof(vf));')
    e('    int32_t vi[16]; std::memcpy(vi, st.vi, sizeof(vi));')
    e('    float acc[4]; std::memcpy(acc, st.acc, sizeof(acc));')
    e('    float s_q = st.q, s_i = st.i; uint32_t s_clip = st.clip, workingClip = st.clip;')
    e('    uint32_t s_branchTarget = st.branchTarget, s_branchDelay = st.branchDelay;')
    e('    uint64_t vfR[32][4] = {}; uint64_t viR[16] = {}; uint64_t accR[4] = {};')
    e('    uint64_t cyc = 0, pend = 0; uint32_t endPc = 0; const uint32_t dmask = c.dataSize - 1u;')
    e('    bool bkValid = false, brPending = false, eSeen = false, tookBranch = false; uint8_t bkReg = 0; int32_t bkVal = 0; uint32_t brTarget = 0;')
    e('    vu1c::Fdiv fdv; vu1c::ClipQueue clipq; vu1c::FmacFlags fl; vu1c::Xgkick xg;')
    e('    uint32_t s_status = st.status;')
    e('    auto commitQ = [&]() { s_q = fdv.value; s_status = (s_status & 0xFCFu) | fdv.di | (fdv.di << 6); fdv.valid = false; };')
    e('    (void)brTarget; (void)tookBranch;')
    e('    switch (st.pc)')
    e('    {')
    for kind, pc in compiled:
        e(f'    case 0x{pc:04x}u: goto L_{pc:04x};')
    e('    default: return vu1c::UnsupportedEntry;')
    e('    }')
    for i, pc in enumerate(order):
        nxt = order[i + 1] if i + 1 < len(order) else None
        emit_pair(e, pairs[pc], img, pairs, nxt, is_delay_of, entry_set)
    e('L_END:')
    e('    {')
    e('        xg.advance(c, cyc);')
    e('        uint64_t end = cyc > pend ? cyc : pend;')
    e('        if (xg.active) { const uint64_t f = xg.drain(c); if (f > end) end = f; }')
    e('        if (xg.fault) return vu1c::Fault;')
    e('        if (fdv.valid) commitQ();')
    e('        clipq.commit(~0ull, s_clip);')
    e('        if (fl.any) { st.mac = fl.lastMac; s_status = (s_status & 0xFF0u) | fl.lastCur | ((fl.sticky & 0xFu) << 6) | (s_status & 0x3C0u); }')
    e('        std::memcpy(st.vf, vf, sizeof(vf)); std::memcpy(st.vi, vi, sizeof(vi)); std::memcpy(st.acc, acc, sizeof(acc));')
    e('        st.q = s_q; st.i = s_i; st.clip = s_clip; st.status = s_status;')
    e('        st.branchTarget = s_branchTarget; st.branchDelay = s_branchDelay; st.branchPending = false;')
    e('        st.pc = endPc; st.ebit = false; st.haltAfterDelaySlot = false;')
    e('        st.cycles += end;')
    e('        return vu1c::Ok;')
    e('    }')
    e('}')
    return e.lines, compiled, rejected


def fnv1a64(data):
    """FNV-1a 64 of a whole 16 KiB micro-memory image (the runtime dispatcher's program key)."""
    hv = 1469598103934665603
    for b in data:
        hv = ((hv ^ b) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return hv


def write_cpp(img, entries, out, support='vu/ps2_vu1_native.h'):
    """Generate the C++ for `entries` [(kind, pc)] of image `img` into `out`; returns (compiled, rejected)."""
    hv = fnv1a64(img)
    name = f'vu1c_{hv:016x}'
    lines, compiled, rejected = generate(img, entries, name)
    lines = [l if l != '#include "vu/ps2_vu1_native.h"' else f'#include "{support}"' for l in lines]
    lines.append('')
    lines.append(f'static const vu1c::Program {name}_programs[] = {{')
    for kind, pc in compiled:
        lines.append(f'    {{0x{hv:016x}ull, {kind}u, 0x{pc:04x}u, &{name}}},')
    lines.append('};')
    lines.append(f'static const vu1c::Registrar {name}_registrar({name}_programs, {len(compiled)});')
    Path(out).write_text('\n'.join(lines) + '\n', encoding='utf-8')
    return compiled, rejected


def main():
    img = Path(sys.argv[1]).read_bytes()
    out = Path(sys.argv[2])
    entries = []
    support = 'vu/ps2_vu1_native.h'
    for a in sys.argv[3:]:
        if a.startswith('--support-include='):
            support = a.split('=', 1)[1]
            continue
        k, pc = a.split(':')
        entries.append((int(k), int(pc, 16)))
    compiled, rejected = write_cpp(img, entries, out, support)
    print(f'image {fnv1a64(img):016x}: compiled {[(k, hex(p)) for k, p in compiled]} rejected {rejected}')


if __name__ == '__main__':
    main()
