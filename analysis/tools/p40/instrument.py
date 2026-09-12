"""Install bounded observational probes; refuses dirty source or repeat install."""
from pathlib import Path
import subprocess, hashlib, json
ROOT=Path(__file__).resolve().parents[3]
V=ROOT/'vendor/PS2Recomp'
OUT=ROOT/'analysis/local/p40'
files=['ps2xRuntime/src/lib/ps2_memory.cpp','ps2xRuntime/src/lib/gs/gs_frontend.cpp']
sources={f:(V/f).read_bytes() for f in files}
for f,b in sources.items():
    baseline=subprocess.check_output(['git','-C',str(V),'show','HEAD:'+f])
    assert b.replace(b'\r\n',b'\n')==baseline.replace(b'\r\n',b'\n'),f
    (OUT/(Path(f).name+'.before')).write_bytes(b)
def replace(s,a,b):
    assert s.count(a)==1,(a,s.count(a))
    return s.replace(a,b)
s=sources[files[0]].decode().replace('\r\n','\n')
s=replace(s,'#include <atomic>','#include <atomic>\n#include <cstdio>\n#include <cstdlib>')
s=replace(s,'                    int tagsProcessed = 0;',r'''                    // P4.0 diagnostic: exact movie-chain addresses only; first two full chains.
                    static const bool p40Enabled = [] { const char* e=std::getenv("DMC_P40_GS_MOVIE_TRACE"); return e && std::strcmp(e,"1")==0; }();
                    static unsigned p40Kicks=0;
                    const bool p40Movie=p40Enabled && channelBase==0x1000A000u && (tagAddr==0x773480u || tagAddr==0x788580u);
                    const unsigned p40Kick=p40Movie ? ++p40Kicks : 0;
                    if(p40Movie && p40Kick<=256) std::fprintf(stderr,"[P40:DMA] kick=%u tadr=%08x chcr=%08x guestPicture=%u\n",p40Kick,tagAddr,chcr,loadScalar<uint32_t>(m_rdram,0x87D900u,PS2_RAM_SIZE,"p40",0x87D900u));
                    int tagsProcessed = 0;''')
s=replace(s,'                        const bool compactVifLocalTag =',r'''                        if(p40Movie && p40Kick<=2) {
                            std::fprintf(stderr,"[P40:TAG] kick=%u n=%d at=%08x raw=%016llx id=%u qwc=%u addr=%08x irq=%u spr=%u data=%08x next=%08x end=%u\n",p40Kick,tagsProcessed,currentTagAddr,(unsigned long long)tag,id,tagQwc,addr,unsigned(irq),unsigned(tag>>63),dataAddr,tagAddr,unsigned(endChain));
                            if(id==1 && tagQwc<=4) for(unsigned j=0;j<tagQwc;j++) {
                                const auto* q=m_rdram+translateAddress(dataAddr)+16*j;
                                uint64_t lo=0,hi=0; std::memcpy(&lo,q,8); std::memcpy(&hi,q+8,8);
                                std::fprintf(stderr,"[P40:QW] kick=%u at=%08x lo=%016llx hi=%016llx\n",p40Kick,dataAddr+16*j,(unsigned long long)lo,(unsigned long long)hi);
                            }
                        }
                        const bool compactVifLocalTag =''')
s=replace(s,'                    if (!chainBuf.empty())',r'''                    if(p40Movie && p40Kick<=256) std::fprintf(stderr,"[P40:DMAEND] kick=%u tags=%d bytes=%zu tadr=%08x\n",p40Kick,tagsProcessed,chainBuf.size(),tagAddr);
                    if (!chainBuf.empty())''')
modified={files[0]:s}
s=sources[files[1]].decode().replace('\r\n','\n')
s=replace(s,'#include <atomic>','#include <atomic>\n#include <cstdlib>\n#include <chrono>')
s=replace(s,'    static constexpr uint32_t kHostFrameWidth = 640u;',r'''    // P4.0 diagnostic only: no change to GS state, packet flow, or backend.
    bool p40Enabled() { static const bool on=[] { const char* e=std::getenv("DMC_P40_GS_MOVIE_TRACE"); return e && std::strcmp(e,"1")==0; }(); return on; }
    std::atomic<unsigned> p40Movies{0};
    thread_local bool p40MoviePacket=false;
    uint64_t p40Hash(const uint8_t* p,size_t n) { uint64_t h=14695981039346656037ull; for(size_t i=0;i<n;i++) h=(h^p[i])*1099511628211ull; return h; }
    unsigned long long p40ms() { return (unsigned long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
    bool p40Detailed() { const unsigned n=p40Movies.load(); return n && (n<=4 || (n<=256 && n%32==0)); }
    static constexpr uint32_t kHostFrameWidth = 640u;''')
s=replace(s,'    uint32_t offset = 0;\n    while (offset + 16 <= sizeBytes)',r'''    const bool p40This=p40Enabled() && sizeBytes==919600u && loadLE64(data)==0x1000000000000002ull;
    if(p40This) ++p40Movies;
    p40MoviePacket=p40This;
    struct P40Guard { ~P40Guard(){p40MoviePacket=false;} } p40Guard;
    if(p40This && p40Detailed()) std::fprintf(stderr,"[P40:GIF] movie=%u ms=%llu bytes=%u hash=%016llx\n",p40Movies.load(),p40ms(),sizeBytes,(unsigned long long)p40Hash(data,sizeBytes));
    uint32_t offset = 0;
    while (offset + 16 <= sizeBytes)''')
s=replace(s,'    recordRegisterDebugEventUnlocked(regAddr, value);',r'''    if(p40MoviePacket && p40Detailed()) std::fprintf(stderr,"[P40:REG] movie=%u reg=%02x value=%016llx\n",p40Movies.load(),unsigned(regAddr),(unsigned long long)value);
    recordRegisterDebugEventUnlocked(regAddr, value);''')
s=replace(s,'        m_backend->Submit(batch);',r'''        if(p40Enabled() && p40Movies.load()) {
            static unsigned count=0; ++count;
            if(count<=100 || (p40Detailed() && count<=10000)) {
                const auto& c=batch.state.context; const auto& p=batch.state.prim;
                std::fprintf(stderr,"[P40:DRAW] n=%u movie=%u ms=%llu prim=%u tme=%u abe=%u fst=%u ctxt=%u frame=%x/%u/%u mask=%08x tex=%x/%u/%u wh=%u/%u tcc=%u tfx=%u tex1=%llx clamp=%llx alpha=%llx test=%llx z=%x/%u/%u xyoff=%u,%u vertices=%u\n",count,p40Movies.load(),p40ms(),unsigned(p.type),p.tme,p.abe,p.fst,p.ctxt,c.frame.fbp,c.frame.fbw,c.frame.psm,c.frame.fbmsk,c.tex0.tbp0,c.tex0.tbw,c.tex0.psm,c.tex0.tw,c.tex0.th,c.tex0.tcc,c.tex0.tfx,(unsigned long long)c.tex1,(unsigned long long)c.clamp,(unsigned long long)c.alpha,(unsigned long long)c.test,c.zbuf.zbp,c.zbuf.psm,c.zbuf.zmask,c.xyoffset.ofx,c.xyoffset.ofy,batch.vertexCount);
                for(unsigned i=0;i<batch.vertexCount;i++) {const auto& v=batch.vertices[i]; std::fprintf(stderr,"[P40:VTX] n=%u i=%u xy=%g,%g uv=%u,%u st=%g,%g rgba=%u,%u,%u,%u\n",count,i,v.x,v.y,v.u,v.v,v.s,v.t,v.r,v.g,v.b,v.a);}
            }
        }
        m_backend->Submit(batch);''')
s=replace(s,'    if (m_backend)\n        m_backend->UploadImage(data, sizeBytes);',r'''    if (m_backend)
        m_backend->UploadImage(data, sizeBytes);
    if(p40MoviePacket && p40Detailed() && m_backend && m_bitbltbuf.dpsm==0 && sizeBytes==28672u) {
        uint64_t host=p40Hash(data,sizeBytes),gs=14695981039346656037ull; unsigned mismatch=0;
        for(unsigned i=0;i<sizeBytes/4;i++) {
            uint32_t v=m_backend->ReadVram(0,m_bitbltbuf.dbp,m_bitbltbuf.dbw,m_trxpos.dsax+i%m_trxreg.rrw,m_trxpos.dsay+i/m_trxreg.rrw),expected=0;
            std::memcpy(&expected,data+i*4,4); mismatch+=(v!=expected);
            for(unsigned k=0;k<4;k++) gs=(gs^uint8_t(v>>(8*k)))*1099511628211ull;
        }
        const auto snap=m_backend->GetTransferSnapshot();
        std::fprintf(stderr,"[P40:GSBUF] movie=%u dbp=%x dbw=%u psm=%u xy=%u,%u wh=%u,%u bytes=%u copied=%u direction=%u input=%016llx gs=%016llx mismatch=%u\n",p40Movies.load(),m_bitbltbuf.dbp,m_bitbltbuf.dbw,m_bitbltbuf.dpsm,m_trxpos.dsax,m_trxpos.dsay,m_trxreg.rrw,m_trxreg.rrh,sizeBytes,snap.copiedPixels,snap.direction,(unsigned long long)host,(unsigned long long)gs,mismatch);
    }''')
s=replace(s,'        request = buildPresentationRequestUnlocked();',r'''        request = buildPresentationRequestUnlocked();
        if(p40Enabled() && p40Movies.load()) {
            static unsigned count=0; ++count;
            if(count<=180) std::fprintf(stderr,"[P40:DISPLAY] n=%u movie=%u ms=%llu tick=%llu pmode=%llx smode2=%llx dispfb1=%llx display1=%llx dispfb2=%llx display2=%llx csr=%llx imr=%llx siglblid=%llx ctx=%x,%x preferred=%u/%x/%x\n",count,p40Movies.load(),p40ms(),(unsigned long long)request.vsyncTick,(unsigned long long)request.pmode,(unsigned long long)request.smode2,(unsigned long long)request.dispfb1,(unsigned long long)request.display1,(unsigned long long)request.dispfb2,(unsigned long long)request.display2,(unsigned long long)m_privRegs->csr,(unsigned long long)m_privRegs->imr,(unsigned long long)m_privRegs->siglblid,request.contextFrames[0].fbp,request.contextFrames[1].fbp,request.hasPreferredSource,request.preferredSource.fbp,request.preferredDestFbp);
        }''')
s=replace(s,'    const bool hasFrame = static_cast<bool>(frame);',r'''    if(p40Enabled() && p40Movies.load()) {
        static unsigned count=0; ++count;
        if(count<=180) std::fprintf(stderr,"[P40:PRESENT] n=%u movie=%u ms=%llu display=%x source=%x wh=%u,%u preferred=%u bytes=%zu hash=%016llx\n",count,p40Movies.load(),p40ms(),frame.displayFbp,frame.sourceFbp,frame.width,frame.height,frame.usedPreferred,frame.pixels.size(),(unsigned long long)p40Hash(frame.pixels.data(),frame.pixels.size()));
    }
    const bool hasFrame = static_cast<bool>(frame);''')
modified[files[1]]=s
for f,s in modified.items():
    assert (V/f).read_bytes()==sources[f],'Concurrent change: '+f
for f,s in modified.items(): (V/f).write_text(s,encoding='utf-8',newline='\n')
(OUT/'source_baseline.json').write_text(json.dumps({f:hashlib.sha256(b).hexdigest() for f,b in sources.items()},indent=2))
