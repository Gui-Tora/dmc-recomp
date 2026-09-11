"""Read-only ELF32 symbol/word/call evidence. No dependencies or generation."""
import hashlib, json, struct, sys, zlib
from pathlib import Path

p = Path(__file__).resolve().parents[3]
b = (p / 'original/SLES_503.58').read_bytes()
assert b[:6] == b'\x7fELF\x01\x01'
u = lambda fmt, off: struct.unpack_from('<' + fmt, b, off)
entry, phoff, shoff = u('III', 24)
phsize, phnum, shsize, shnum, shstr = u('HHHHH', 42)
sections = [u('10I', shoff+i*shsize) for i in range(shnum)]
symbols = []
for s in sections:
    if s[1] != 2: continue
    st = sections[s[6]]
    strings = b[st[4]:st[4]+st[5]]
    for o in range(s[4], s[4]+s[5], s[9]):
        n,v,size,info,other,idx = u('IIIBBH', o)
        name = strings[n:].split(b'\0',1)[0].decode(errors='replace')
        symbols.append(dict(name=name,addr=v,size=size,type=info&15,section=idx))
def owner(pc):
    return [s['name'] for s in symbols if s['type']==2 and s['addr'] <= pc < s['addr']+s['size']]
def off(pc):
    for s in sections:
        if s[1] != 8 and s[3] <= pc < s[3]+s[5]: return s[4]+pc-s[3]
    raise ValueError(hex(pc))
as_text='--text' in sys.argv
targets = [a for a in sys.argv[1:] if a!='--text'] or ['sceMpegInit']
regs=['zero','at','v0','v1','a0','a1','a2','a3','t0','t1','t2','t3','t4','t5','t6','t7','s0','s1','s2','s3','s4','s5','s6','s7','t8','t9','k0','k1','gp','sp','fp','ra']
def disasm(w,pc):
    op=w>>26;rs=regs[(w>>21)&31];rt=regs[(w>>16)&31];rd=regs[(w>>11)&31];imm=w&65535;simm=imm if imm<32768 else imm-65536
    if not w:return 'nop'
    if op in (2,3):return ('jal' if op==3 else 'j')+f' 0x{(((pc+4)&0xf0000000)|((w&0x3ffffff)<<2)):08X}'
    if op==15:return f'lui ${rt},0x{imm:04X}'
    if op in (9,13):return f'{"addiu" if op==9 else "ori"} ${rt},${rs},{simm if op==9 else hex(imm)}'
    if op in (0x23,0x2b,0x37,0x3f,0x1e,0x1f):return f'{ {0x23:"lw",0x2b:"sw",0x37:"ld",0x3f:"sd",0x1e:"lq",0x1f:"sq"}[op]} ${rt},{simm}(${rs})'
    if op==0 and w&63 in (0x24,0x25,0x2d,0x2b):return f'{ {0x24:"and",0x25:"or",0x2d:"daddu",0x2b:"sltu"}[w&63]} ${rd},${rs},${rt}'
    if op==0 and w&63==8:return f'jr ${rs}'
    if op==1 and ((w>>16)&31) in (0,1):return f'{"bltz" if ((w>>16)&31)==0 else "bgez"} ${rs},0x{pc+4+simm*4:08X}'
    return f'.word 0x{w:08X} (not decoded by this minimal reader)'
out = dict(sha256=hashlib.sha256(b).hexdigest(),crc32=f'{zlib.crc32(b):08X}',entry=hex(entry),functions=[])
for name in targets:
    for sym in symbols:
        if sym['name'] != name or sym['type'] != 2: continue
        a,size=sym['addr'],sym['size']
        words=[]
        for pc in range(a,a+size,4):
            w=u('I',off(pc))[0]
            row=dict(pc=hex(pc),word=f'{w:08x}',instruction=disasm(w,pc))
            if w>>26 in (2,3):
                dst=((pc+4)&0xf0000000)|((w&0x3ffffff)<<2)
                row.update(target=hex(dst),symbol=owner(dst))
            words.append(row)
        callers=[]
        for s in sections:
            if not s[2]&4 or s[1]==8: continue
            for o in range(s[4],s[4]+s[5]-3,4):
                w=u('I',o)[0]; pc=s[3]+o-s[4]
                if w>>26 in (2,3) and (((pc+4)&0xf0000000)|((w&0x3ffffff)<<2))==a:
                    callers.append(dict(pc=hex(pc),kind='jal' if w>>26==3 else 'j',owner=owner(pc)))
        out['functions'].append(dict(symbol=sym,end=hex(a+size),bytes=b[off(a):off(a)+size].hex(),words=words,direct_callers=callers))
if as_text:
    print(out['sha256'],out['crc32'],out['entry'])
    for f in out['functions']:
        print(f['symbol']['name'],hex(f['symbol']['addr']),f['end'])
        for row in f['words']:print(row['pc'],row['word'],row['instruction'],','.join(row.get('symbol',[])))
        print('DIRECT CALLERS',f['direct_callers'])
else:print(json.dumps(out,indent=2))
