"""Independent ELF/translation byte cross-check and movie DMA/GS layout derivation.
No game execution, generated edits, assets exported, or dependency installation.
"""
from pathlib import Path
import struct, hashlib, zlib, re, json
R=Path(__file__).resolve().parents[3]; O=R/'analysis/local/p40'
b=(R/'original/SLES_503.58').read_bytes()
assert hashlib.sha256(b).hexdigest()=='d0753a6b3b2f00802a50758a872d8cf051725aa31c839aa8894eee30ce58bab4'
u=lambda f,o:struct.unpack_from('<'+f,b,o)
shoff=u('I',32)[0]; shsize,shnum=u('HH',46)
sections=[u('10I',shoff+i*shsize) for i in range(shnum)]
def offset(pc):
    for s in sections:
        if s[1]!=8 and s[3]<=pc<s[3]+s[5]:return s[4]+pc-s[3]
    raise ValueError(hex(pc))
evidence=[]
for name,addr in [('Movie_set_loadimage3',0x47aaa0),('Movie_loadimage',0x3fe590),('MpegMovieDecode',0x47a760)]:
    t=(R/f'analysis/local/symtabfirst/generated/{name}_0x{addr:x}.cpp').read_text()
    words={int(a,16):int(w,16) for a,w in re.findall(r'// 0x([0-9a-f]+): 0x([0-9a-f]+)',t)}
    for a,w in words.items(): assert u('I',offset(a))[0]==w,(hex(a),hex(w))
    evidence.append(dict(name=name,entry=hex(addr),words_checked=len(words),end=hex(max(words)+4)))
block=[[0,1,4,5,16,17,20,21],[2,3,6,7,18,19,22,23],[8,9,12,13,24,25,28,29],[10,11,14,15,26,27,30,31]]
column=[[0,1,4,5,8,9,12,13],[2,3,6,7,10,11,14,15],[16,17,20,21,24,25,28,29],[18,19,22,23,26,27,30,31],[32,33,36,37,40,41,44,45],[34,35,38,39,42,43,46,47],[48,49,52,53,56,57,60,61],[50,51,54,55,58,59,62,63]]
def gsaddr(dbp,x,y):return dbp*256+((y//32)*8+x//64)*8192+block[(y//8)%4][(x//8)%8]*256+column[y%8][x%8]*4
slots=[]
for slot,base in enumerate([0x773480,0x788580]):
    dbp=slot*0x1400
    tags=[dict(at=base,id=1,qwc=3,addr=0,data=base+16,next=base+64)]
    packets=[dict(at=base+16,lo=0x1000000000000002,hi=14,kind='PACKED',nloop=2,nreg=1,eop=0,pre=0,prim=0,regs=[[0x50,(8<<48)|(dbp<<32)],[0x52,(448<<32)|16]])]
    bands=[]; alladdr=set()
    for i in range(32):
        tag=base+0x40+i*0x60; src=0x79d680+i*0x7000
        tags.extend([dict(at=tag,id=1,qwc=4,addr=0,data=tag+16,next=tag+80),dict(at=tag+80,id=3,qwc=1792,addr=src,data=src,next=tag+96)])
        packets.extend([dict(at=tag+16,lo=0x1000000000000002,hi=14,kind='PACKED',nloop=2,nreg=1,eop=0,pre=0,prim=0,regs=[[0x51,(16*i)<<32],[0x53,0]]),dict(at=tag+64,lo=(2<<58)|(int(i==31)<<15)|1792,hi=0,kind='IMAGE',nloop=1792,nreg_raw=0,eop=int(i==31),pre=0,prim=0,source=src,bytes=28672)])
        addresses={gsaddr(dbp,16*i+x,y) for y in range(448) for x in range(16)}
        assert len(addresses)==7168 and not alladdr.intersection(addresses)
        alladdr.update(addresses)
        bands.append(dict(i=i,source=hex(src),end=hex(src+0x7000),x0=16*i,x1=16*i+15,y0=0,y1=447,gs_min=hex(min(addresses)),gs_max_exclusive=hex(max(addresses)+4),pages=[(dbp//32)+(i//4)+8*j for j in range(14)],blocks_in_each_page=[block[y][2*(i%4)+x] for y in range(4) for x in range(2)]))
    tags.append(dict(at=base+0xc40,id=7,qwc=0,addr=0,data=base+0xc50,next=base+0xc40))
    for t in tags:t.update(irq=0,spr=0,raw=hex((t['addr']<<32)|(t['id']<<28)|t['qwc']))
    assert len(tags)==66 and sum(t['qwc']*16 for t in tags)==919600
    assert len(alladdr)*4==917504 and max(alladdr)+4-min(alladdr)==917504
    slots.append(dict(slot=slot,tadr=hex(base),chcr='0x105',dbp=hex(dbp),bitbltbuf=hex((8<<48)|(dbp<<32)),trxreg=hex((448<<32)|16),tags=tags,giftags=packets,bands=bands))
out=dict(elf_sha256=hashlib.sha256(b).hexdigest(),crc32=f'{zlib.crc32(b):08X}',entry=hex(u('I',24)[0]),evidence=evidence,slots=slots)
(O/'reconstruction.json').write_text(json.dumps(out,indent=2))
lines=['# Independent movie-chain reconstruction','',str(evidence),'','All IRQ=SPR=0. NEXT here is the next DMAtag address, not the NEXT tag type.','', '|slot|tag address|ID|QWC|ADDR|payload address|next tag|','|---|---|---|---|---|---|---|']
for s in slots:
    for t in s['tags']:lines.append(f"|{s['slot']}|{t['at']:08X}|{t['id']}|{t['qwc']}|{t['addr']:08X}|{t['data']:08X}|{t['next']:08X}|")
lines+=['','|band|guest begin|guest end exclusive|destination X|Y|slot 0 VRAM pages|blocks per page|','|---|---|---|---|---|---|---|']
for t in slots[0]['bands']:lines.append(f"|{t['i']}|{t['source']}|{t['end']}|{t['x0']}..{t['x1']}|0..447|{','.join(map(str,t['pages']))}|{','.join(map(str,t['blocks_in_each_page']))}|")
(O/'tables.md').write_text('\n'.join(lines)+'\n')
print(json.dumps(dict(evidence=evidence,tags_per_slot=66,image_bytes=917504,total_bytes=919600,guest_end=hex(0x79d680+917504))))
