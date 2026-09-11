"""Read-only comparison of P3.11 snapshots. Offsets from this ELF's guest code."""
import hashlib,json,struct,sys
from pathlib import Path
d=Path(sys.argv[1]);a=(d/'preinit.bin').read_bytes();b=(d/'getpicture.bin').read_bytes()
assert len(a)==len(b)==32*1024*1024
state=0x87D680
rd=lambda data,offset:struct.unpack_from('<I',data,state+offset)[0]
out={'run':d.name,'state':hex(state),'mpeg':hex(state+0x278),'fields':{},'buffers':{}}
fields={4:'PSS base',8:'PSS write cursor',12:'PSS available bytes',0x10:'PSS capacity',0x14:'producer remaining',0x18:'demux remaining',0x1c:'viBuf base',0x20:'viBuf capacity',0x28:'video bytes total',0x64:'DMA tags base',0x68:'DMA tag count',0x74:'caller resets',0x9c:'caller resets',0x2c0:'viBuf block cursor',0x2c4:'viBuf queued blocks',0x2c8:'viBuf pending bytes',0x2fc:'movie flags'}
for o,name in fields.items():out['fields'][hex(o)]={'name':name,'pre':rd(a,o),'getpicture':rd(b,o)}
for name,bo,so in [('pss',4,0x10),('video_es',0x1c,0x20)]:
    base=rd(a,bo)&0x1fffffff;size=rd(a,so)
    assert 0<=base< len(a) and 0<=size<=len(a)-base
    x=a[base:base+size];y=b[base:base+size]
    out['buffers'][name]={'base':hex(base),'size':size,'same':x==y,'sha256_pre':hashlib.sha256(x).hexdigest(),'sha256_getpicture':hashlib.sha256(y).hexdigest(),'start_codes':{hex(k):x.count(bytes([0,0,1,k])) for k in (0,0xb3,0xb7,0xba,0xbd,0xe0)},'first64_pre':x[:64].hex()}
    (d/f'{name}_preinit.bin').write_bytes(x)
out['state_changed_words']=[{'offset':hex(o),'pre':rd(a,o),'getpicture':rd(b,o)} for o in range(0,0x300,4) if rd(a,o)!=rd(b,o)]
(d/'ram_analysis.json').write_text(json.dumps(out,indent=2))
print(json.dumps(out,indent=2))
