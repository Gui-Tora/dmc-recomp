// Offline GS replay bench (introduced in PERF_003 E2; see README.md). Captures contain game-derived data and are
// never committed.
//
//   gs_replay selftest <workdir>            synthetic scenes through the real GS frontend, recorded and replayed
//   gs_replay replay <capture> [options]    replay a capture against GSCpuBackend and verify it (options: README.md)
//   gs_replay microbench                    VRAM accessor dispatch cost (std::function / pointer / direct)
//
// Exit codes: 0 replay identical; 1 backend divergence (first one reported); 2 capture format/integrity error
// or a prologue that cannot be rebuilt; 3 usage or I/O error.
#include "gs_replay.h"

#include "runtime/gs/gs_frontend.h"
#include "runtime/ps2_memory.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <timeapi.h>

#include "runtime/gs/ps2_gs_memory.h"

#include <array>
#include <atomic>
#include <cfenv>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

namespace
{
    using namespace gsreplay;
    using Clock = std::chrono::steady_clock;

    const char *recName(Rec r)
    {
        switch (r)
        {
        case Rec::Prologue: return "Prologue";
        case Rec::InitVram: return "InitVram";
        case Rec::Submit: return "Submit";
        case Rec::BeginTransfer: return "BeginTransfer";
        case Rec::Upload: return "Upload";
        case Rec::Clear: return "Clear";
        case Rec::WriteVram: return "WriteVram";
        case Rec::ReadVram: return "ReadVram";
        case Rec::Consume: return "Consume";
        case Rec::Flush: return "Flush";
        case Rec::TextureFlush: return "TextureFlush";
        case Rec::Sync: return "Sync";
        case Rec::Present: return "Present";
        case Rec::TransferSnapshot: return "TransferSnapshot";
        case Rec::SnapshotVram: return "SnapshotVram";
        case Rec::Reset: return "Reset";
        case Rec::End: return "End";
        case Rec::FpEnv: return "FpEnv";
        case Rec::VramHash: return "VramHash";
        case Rec::FpEnvChanged: return "FpEnvChanged";
        case Rec::L2HState: return "L2HState";
        }
        return "?";
    }

    struct Record
    {
        Rec type;
        const uint8_t *data;
        uint32_t size;
    };

    struct Capture
    {
        std::vector<uint8_t> bytes;
        Header header{};
        std::vector<Record> records;
    };

    // Format and integrity only: magic, version, struct layout, record framing, footer hash and count.
    bool parse(Capture &c, std::string &error)
    {
        const auto &b = c.bytes;
        if (b.size() < sizeof(Header) + sizeof(Footer))
            return error = "file too small", false;
        std::memcpy(&c.header, b.data(), sizeof(Header));
        const Header &h = c.header;
        if (std::memcmp(h.magic, kMagic, sizeof kMagic) != 0)
            return error = "bad magic", false;
        if (h.version != kVersion)
            return error = "unsupported version " + std::to_string(h.version), false;
        if (h.sizeBatch != sizeof(GSPrimitiveBatch) || h.sizeTransferCommand != sizeof(GSTransferCommand) ||
            h.sizeTransferSnapshot != sizeof(GSTransferSnapshot) || h.sizeContext != sizeof(GSContext) ||
            h.sizePresentationRequest != sizeof(GSPresentationRequest))
            return error = "struct layout differs from this build", false;
        Footer f;
        const size_t body = b.size() - sizeof(Footer);
        std::memcpy(&f, b.data() + body, sizeof f);
        if (std::memcmp(f.magic, kFooterMagic, sizeof kFooterMagic) != 0)
            return error = "missing footer (truncated or failed capture)", false;
        if (fnv1a(b.data(), body) != f.hash)
            return error = "integrity hash mismatch", false;
        size_t pos = sizeof(Header);
        while (pos < body)
        {
            if (body - pos < 5)
                return error = "truncated record header at " + std::to_string(pos), false;
            const Rec t = static_cast<Rec>(b[pos]);
            uint32_t n;
            std::memcpy(&n, b.data() + pos + 1, 4);
            pos += 5;
            if (n > body - pos)
                return error = "record overruns the file at " + std::to_string(pos), false;
            c.records.push_back({t, b.data() + pos, n});
            pos += n;
        }
        if (c.records.size() != f.records)
            return error = "record count mismatch", false;
        const size_t init = (c.records.size() > 1 && c.records[1].type == Rec::L2HState) ? 2u : 1u;
        if (c.records.size() < init + 2 || c.records[0].type != Rec::Prologue || c.records[init].type != Rec::InitVram ||
            c.records.back().type != Rec::End)
            return error = "missing Prologue/InitVram/End", false;
        if (c.records[init].size != h.vramSize || c.records.back().size != h.vramSize)
            return error = "VRAM record size mismatch", false;
        if (init == 2u && c.records[1].size != sizeof(L2HStateRec) + h.vramSize)
            return error = "L2HState record size mismatch", false;
        return true;
    }

    template <typename T>
    bool payload(const Record &r, T &out)
    {
        if (r.size != sizeof(T))
            return false;
        std::memcpy(&out, r.data, sizeof(T));
        return true;
    }

    bool sameSnapshot(const GSTransferSnapshot &a, const GSTransferSnapshot &b)
    {
        return a.x == b.x && a.y == b.y && a.totalPixels == b.totalPixels && a.copiedPixels == b.copiedPixels &&
               a.direction == b.direction && a.localToHostPendingBytes == b.localToHostPendingBytes;
    }

    struct Stats
    {
        std::map<std::string, uint64_t> counts;
        uint64_t uploadBytes = 0, consumeBytes = 0;
        // Pixels visited by the CPU rasteriser, recomputed from the batch exactly as DrawTriangle/DrawSprite
        // bound their loops (triangles: scissor-clipped bounding box; sprites: clipped rectangle).
        uint64_t triangleBoxPixels = 0, spritePixels = 0;
        std::map<std::string, uint64_t> pixelsBy;
    };

    int clampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

    void accountBatch(Stats &s, const GSPrimitiveBatch &b)
    {
        const GSDrawState &st = b.state;
        const auto &ctx = st.context;
        static const char *kPrim[] = {"point", "line", "linestrip", "triangle", "tristrip", "trifan", "sprite", "?"};
        const char *prim = kPrim[std::min<int>(st.prim.type, 7)];
        uint64_t pixels = 0;
        if (st.prim.type == GS_PRIM_SPRITE && b.vertexCount >= 2)
        {
            const int ofx = ctx.xyoffset.ofx >> 4, ofy = ctx.xyoffset.ofy >> 4;
            int x0 = static_cast<int>(b.vertices[0].x) - ofx, y0 = static_cast<int>(b.vertices[0].y) - ofy;
            int x1 = static_cast<int>(b.vertices[1].x) - ofx, y1 = static_cast<int>(b.vertices[1].y) - ofy;
            if (x0 > x1) std::swap(x0, x1);
            if (y0 > y1) std::swap(y0, y1);
            const int ux1 = x0 + std::max(1, x1 - x0) - 1, uy1 = y0 + std::max(1, y1 - y0) - 1;
            if (!(ux1 < ctx.scissor.x0 || x0 > ctx.scissor.x1 || uy1 < ctx.scissor.y0 || y0 > ctx.scissor.y1))
            {
                const int dx0 = clampI(x0, ctx.scissor.x0, ctx.scissor.x1), dx1 = clampI(ux1, ctx.scissor.x0, ctx.scissor.x1);
                const int dy0 = clampI(y0, ctx.scissor.y0, ctx.scissor.y1), dy1 = clampI(uy1, ctx.scissor.y0, ctx.scissor.y1);
                pixels = uint64_t(dx1 - dx0 + 1) * uint64_t(dy1 - dy0 + 1);
            }
            s.spritePixels += pixels;
        }
        else if (b.vertexCount == 3)
        {
            const int ofx = ctx.xyoffset.ofx >> 4, ofy = ctx.xyoffset.ofy >> 4;
            float xs[3], ys[3];
            for (int i = 0; i < 3; ++i)
            {
                xs[i] = b.vertices[i].x - float(ofx);
                ys[i] = b.vertices[i].y - float(ofy);
            }
            int minX = clampI(int(std::floor(std::min({xs[0], xs[1], xs[2]}))), ctx.scissor.x0, ctx.scissor.x1);
            int maxX = clampI(int(std::ceil(std::max({xs[0], xs[1], xs[2]}))), ctx.scissor.x0, ctx.scissor.x1);
            int minY = clampI(int(std::floor(std::min({ys[0], ys[1], ys[2]}))), ctx.scissor.y0, ctx.scissor.y1);
            int maxY = clampI(int(std::ceil(std::max({ys[0], ys[1], ys[2]}))), ctx.scissor.y0, ctx.scissor.y1);
            const float denom = (ys[1] - ys[2]) * (xs[0] - xs[2]) + (xs[2] - xs[1]) * (ys[0] - ys[2]);
            if (std::fabs(denom) >= 0.001f && maxX >= minX && maxY >= minY)
                pixels = uint64_t(maxX - minX + 1) * uint64_t(maxY - minY + 1);
            s.triangleBoxPixels += pixels;
        }
        s.counts[std::string("prim.") + prim]++;
        if (st.prim.tme) s.counts["tme"]++;
        if (st.prim.abe) s.counts["abe"]++;
        if (st.linearFilter && st.prim.tme) s.counts["bilinear"]++;
        s.counts["ztst." + std::to_string((ctx.test >> 17) & 3u)]++;
        s.pixelsBy["frame.psm" + std::to_string(ctx.frame.psm)] += pixels;
        if (st.prim.tme)
            s.pixelsBy["tex.psm" + std::to_string(ctx.tex0.psm)] += pixels;
        if (st.prim.abe || ctx.frame.fbmsk)
            s.pixelsBy["frame.read"] += pixels;
        if (((ctx.test >> 17) & 3u) >= 2u)
            s.pixelsBy["z.read"] += pixels;
    }

    // Diagnosis: full state of the batches whose depth test can never pass in this runtime (ZTST field 0).
    void dumpZNever(const Capture &c)
    {
        int n = 0;
        for (size_t i = 0; i < c.records.size(); ++i)
        {
            const Record &r = c.records[i];
            if (r.type != Rec::Submit)
                continue;
            GSPrimitiveBatch b;
            std::memcpy(&b, r.data, sizeof b);
            const auto &ctx = b.state.context;
            if (((ctx.test >> 17) & 3u) != 0u)
                continue;
            std::printf("record %zu prim=%u verts=%u TEST=0x%llx (ATE=%llu ATST=%llu AREF=0x%llx AFAIL=%llu DATE=%llu "
                        "ZTE=%llu ZTST=%llu) FRAME fbp=0x%x fbw=%u psm=%u fbmsk=0x%x ZBUF zbp=0x%x psm=%u zmask=%d "
                        "ALPHA=0x%llx abe=%d tme=%d fst=%d TEX0 tbp=0x%x tbw=%u psm=%u tw=%u th=%u tfx=%u tcc=%u "
                        "scissor=%u..%u,%u..%u ofs=%u,%u fba=%llu\n",
                        i, b.state.prim.type, b.vertexCount, (unsigned long long)ctx.test,
                        (unsigned long long)(ctx.test & 1), (unsigned long long)((ctx.test >> 1) & 7),
                        (unsigned long long)((ctx.test >> 4) & 0xFF), (unsigned long long)((ctx.test >> 12) & 3),
                        (unsigned long long)((ctx.test >> 14) & 1), (unsigned long long)((ctx.test >> 16) & 1),
                        (unsigned long long)((ctx.test >> 17) & 3), ctx.frame.fbp, ctx.frame.fbw, ctx.frame.psm,
                        ctx.frame.fbmsk, ctx.zbuf.zbp, ctx.zbuf.psm, ctx.zbuf.zmask ? 1 : 0,
                        (unsigned long long)ctx.alpha, b.state.prim.abe, b.state.prim.tme, b.state.prim.fst,
                        ctx.tex0.tbp0, ctx.tex0.tbw, ctx.tex0.psm, ctx.tex0.tw, ctx.tex0.th, ctx.tex0.tfx, ctx.tex0.tcc,
                        ctx.scissor.x0, ctx.scissor.x1, ctx.scissor.y0, ctx.scissor.y1, ctx.xyoffset.ofx, ctx.xyoffset.ofy,
                        (unsigned long long)ctx.fba);
            for (int v = 0; v < b.vertexCount; ++v)
                std::printf("    v%d x=%.2f y=%.2f z=%.0f rgba=%u,%u,%u,%u uv=%u,%u st=%.4f,%.4f q=%.4f\n", v,
                            b.vertices[v].x, b.vertices[v].y, b.vertices[v].z, b.vertices[v].r, b.vertices[v].g,
                            b.vertices[v].b, b.vertices[v].a, b.vertices[v].u, b.vertices[v].v, b.vertices[v].s,
                            b.vertices[v].t, b.vertices[v].q);
            if (++n >= 40)
                break;
        }
    }

    struct ReplayOptions
    {
        bool verify = true;
        bool continueAfterDivergence = false;
        long long skipRecord = -1; // negative test: drop one call record
        Stats *stats = nullptr;
        // The recorded backend may hold a stale, never-consumed local->host buffer that the prologue cannot rebuild
        // (its bytes came from the VRAM of an earlier moment). Accept a prologue that differs only in the pending
        // byte count, and report a divergence if anything observes that buffer (Consume or TransferSnapshot)
        // before a new local->host transfer replaces it.
        bool allowUnobservedStaleL2H = false;
        std::string dumpFinal; // write the replayed final VRAM here (diagnosis)
        bool ignoreFpEnv = false; // negative test: replay every call with the replayer's own environment
        // ZTE study: set TEST.ZTE = 1 on every Submit. A backend that honours ZTE then behaves exactly like one that
        // ignores it (ZTST applied, depth written), so a ZTE-correct backend must reproduce an old capture this way.
        bool forceZte = false;
        std::string dumpPresent; // write each presented frame as <prefix>_<n>.ppm
        long long dumpAfterRecord = -1; // write the VRAM right after this record to dumpAfterPath
        std::string dumpAfterPath;
        std::map<std::string, std::pair<uint64_t, double>> *timeByClass = nullptr; // Submit count and backend seconds
        // Diagnosis of a hidden dependency: VU1 execution (interpreter and compiled code) sets FE_TOWARDZERO, and
        // PATH1 draws happen inside it; CPU-DMA sprites run with the default mode. Replays non-sprite Submits under
        // FE_TOWARDZERO and sprites under FE_TONEAREST. A heuristic, not a recorded fact about each call.
        bool roundingHeuristic = false;
    };

    struct ReplayResult
    {
        int code = 0;
        std::string message;
        uint64_t divergences = 0;
        double backendSeconds = 0.0;
        double verifySeconds = 0.0;
        uint64_t vramHashesChecked = 0;
        uint64_t envChangesDuringCalls = 0; // FpEnvChanged records in the capture
        uint64_t crtMismatches = 0;         // recorded CRT control word differs from the replayer's after applying MXCSR
        std::map<std::string, uint64_t> callsByEnv;
    };

    // Saves the replay thread's MXCSR on entry and restores it on exit, whatever happens in between.
    struct MxcsrGuard
    {
        uint32_t saved = _mm_getcsr();
        ~MxcsrGuard() { _mm_setcsr(saved); }
    };

    std::string envName(const FpEnvRec &e)
    {
        static const char *kRc[] = {"nearest", "down", "up", "toward-zero"};
        std::ostringstream s;
        s << kRc[(e.mxcsrControl >> 13) & 3u] << ((e.mxcsrControl & 0x8000u) ? "+FTZ" : "")
          << ((e.mxcsrControl & 0x40u) ? "+DAZ" : "") << " (mxcsr 0x" << std::hex << e.mxcsrControl << ", crt 0x"
          << e.crtControl << ", fegetround 0x" << e.feRound << ")";
        return s.str();
    }

    ReplayResult replay(const Capture &c, const ReplayOptions &o)
    {
        ReplayResult res;
        const MxcsrGuard guard;
        const FpEnvRec replayerEnv = currentFpEnv();
        std::string currentEnv = envName(replayerEnv);
        std::vector<uint8_t> vram(c.header.vramSize, 0u);
        GSCpuBackend backend;
        backend.Initialize(vram.data(), static_cast<uint32_t>(vram.size()));
        auto fail = [&](size_t i, const std::string &what) {
            ++res.divergences;
            if (res.code == 0)
            {
                res.code = 1;
                res.message = "record " + std::to_string(i) + " (" + recName(c.records[i].type) + "): " + what;
            }
            return !o.continueAfterDivergence;
        };
        Clock::time_point t0;
        auto startBackend = [&] { t0 = Clock::now(); };
        auto stopBackend = [&] { res.backendSeconds += std::chrono::duration<double>(Clock::now() - t0).count(); };

        // Prologue: rebuild the hidden transfer state, then restore the recorded VRAM.
        const Record &pr = c.records[0];
        PrologueRec p{};
        if (pr.size < sizeof p)
            return {2, "short prologue"};
        std::memcpy(&p, pr.data, sizeof p);
        if (pr.size != sizeof p + size_t(p.uploadCalls) * 4u)
            return {2, "prologue size mismatch"};
        const bool hasL2H = c.records[1].type == Rec::L2HState;
        if (hasL2H)
        {
            // Regenerate the pending local->host buffer from the VRAM it was read from, then its read position.
            L2HStateRec l;
            std::memcpy(&l, c.records[1].data, sizeof l);
            std::memcpy(vram.data(), c.records[1].data + sizeof l, vram.size());
            backend.BeginTransfer(l.command);
            std::vector<uint8_t> sink(1u << 16);
            uint64_t left = l.consumedBytes;
            while (left)
            {
                const uint32_t got = backend.ConsumeLocalToHostBytes(sink.data(), uint32_t(std::min<uint64_t>(left, sink.size())));
                if (!got)
                    return {2, "L2HState: cannot consume the recorded bytes"};
                left -= got;
            }
        }
        if (p.hasCommand && !(hasL2H && p.command.direction == 1u))
        {
            backend.BeginTransfer(p.command);
            std::vector<uint8_t> zeros;
            for (uint32_t i = 0; i < p.uploadCalls; ++i)
            {
                uint32_t n;
                std::memcpy(&n, pr.data + sizeof p + size_t(i) * 4u, 4);
                zeros.assign(n, 0u);
                backend.UploadImage(zeros.data(), n);
            }
            std::vector<uint8_t> sink(size_t(std::min<uint64_t>(p.consumedBytes, 1u << 26)) + 1u);
            uint64_t left = p.consumedBytes;
            while (left)
            {
                const uint32_t got = backend.ConsumeLocalToHostBytes(sink.data(), uint32_t(std::min<uint64_t>(left, sink.size())));
                if (!got)
                    break;
                left -= got;
            }
        }
        const size_t initIndex = hasL2H ? 2u : 1u;
        std::memcpy(vram.data(), c.records[initIndex].data, vram.size());
        bool staleL2H = false;
        if (const GSTransferSnapshot got = backend.GetTransferSnapshot(); !sameSnapshot(got, p.snapshot))
        {
            GSTransferSnapshot relaxed = p.snapshot;
            relaxed.localToHostPendingBytes = got.localToHostPendingBytes;
            if (o.allowUnobservedStaleL2H && sameSnapshot(got, relaxed))
                staleL2H = true;
            else
            {
            auto fmt = [](const GSTransferSnapshot &s) {
                return "{x=" + std::to_string(s.x) + " y=" + std::to_string(s.y) + " total=" + std::to_string(s.totalPixels) +
                       " copied=" + std::to_string(s.copiedPixels) + " dir=" + std::to_string(s.direction) +
                       " pending=" + std::to_string(s.localToHostPendingBytes) + "}";
            };
            return {2, "prologue: rebuilt transfer state differs from the recorded one: recorded " + fmt(p.snapshot) +
                           " rebuilt " + fmt(got) + " (hasCommand=" + std::to_string(p.hasCommand) + " dir=" +
                           std::to_string(p.command.direction) + " uploadCalls=" + std::to_string(p.uploadCalls) +
                           " uploaded=" + std::to_string(p.uploadedBytes) + " consumed=" + std::to_string(p.consumedBytes) +
                           " rrw=" + std::to_string(p.command.trxreg.rrw) + " rrh=" + std::to_string(p.command.trxreg.rrh) +
                           " dpsm=" + std::to_string(p.command.bitbltbuf.dpsm) + ")"};
            }
        }
        if (staleL2H)
            res.message = "note: stale local->host buffer of " + std::to_string(p.snapshot.localToHostPendingBytes) +
                          " bytes at the window start, not rebuilt";

        for (size_t i = initIndex + 1; i < c.records.size(); ++i)
        {
            const Record &r = c.records[i];
            if (static_cast<long long>(i) == o.skipRecord)
                continue;
            if (r.type != Rec::FpEnv && r.type != Rec::FpEnvChanged && r.type != Rec::VramHash && r.type != Rec::End)
                res.callsByEnv[std::string(recName(r.type)) + " @ " + currentEnv]++;
            if (o.dumpAfterRecord >= 0 && static_cast<long long>(i) == o.dumpAfterRecord + 1)
            {
                std::ofstream dump(o.dumpAfterPath, std::ios::binary);
                dump.write(reinterpret_cast<const char *>(vram.data()), std::streamsize(vram.size()));
            }
            switch (r.type)
            {
            case Rec::Submit:
            {
                GSPrimitiveBatch b;
                if (!payload(r, b))
                    return {2, "bad Submit record " + std::to_string(i)};
                if (o.stats)
                    accountBatch(*o.stats, b);
                if (o.forceZte)
                    b.state.context.test |= 1ull << 16;
                if (o.stats && ((b.state.context.test >> 16) & 1u) == 0u)
                {
                    o.stats->counts["zte0.ztst" + std::to_string((b.state.context.test >> 17) & 3u)]++;
                    o.stats->counts.emplace("zte0.first_record", i);
                }
                if (o.roundingHeuristic)
                    std::fesetround(b.state.prim.type == GS_PRIM_SPRITE ? FE_TONEAREST : FE_TOWARDZERO);
                const auto s0 = Clock::now();
                startBackend();
                backend.Submit(b);
                stopBackend();
                if (o.timeByClass)
                {
                    static const char *kPrim[] = {"point", "line", "linestrip", "tri", "tristrip", "trifan", "sprite", "?"};
                    std::string key = kPrim[std::min<int>(b.state.prim.type, 7)];
                    key += b.state.prim.tme ? " tex.psm" + std::to_string(b.state.context.tex0.psm) +
                                                  (b.state.linearFilter ? " bilinear" : " point")
                                            : " untextured";
                    key += b.state.prim.abe ? " abe" : "";
                    key += " ztst" + std::to_string((b.state.context.test >> 17) & 3u);
                    key += (_mm_getcsr() & 0x6000u) ? " rz" : "";
                    auto &slot = (*o.timeByClass)[key];
                    ++slot.first;
                    slot.second += std::chrono::duration<double>(Clock::now() - s0).count();
                }
                if (o.roundingHeuristic)
                    std::fesetround(FE_TONEAREST);
                break;
            }
            case Rec::BeginTransfer:
            {
                GSTransferCommand cmd;
                if (!payload(r, cmd))
                    return {2, "bad BeginTransfer record"};
                if (o.stats)
                    o.stats->counts["transfer.dir" + std::to_string(cmd.direction)]++;
                if (cmd.direction == 1u)
                    staleL2H = false;
                startBackend();
                backend.BeginTransfer(cmd);
                stopBackend();
                break;
            }
            case Rec::Upload:
            {
                uint32_t n;
                if (r.size < 4 || (std::memcpy(&n, r.data, 4), r.size != 4u + n))
                    return {2, "bad Upload record"};
                if (o.stats)
                    o.stats->uploadBytes += n;
                startBackend();
                backend.UploadImage(n ? r.data + 4 : nullptr, n);
                stopBackend();
                break;
            }
            case Rec::Clear:
            {
                ClearRec cr;
                if (!payload(r, cr))
                    return {2, "bad Clear record"};
                startBackend();
                const bool got = backend.ClearFramebuffer(cr.context, cr.rgba);
                stopBackend();
                if (o.verify && got != (cr.result != 0) && fail(i, "ClearFramebuffer result differs"))
                    return res;
                break;
            }
            case Rec::WriteVram:
            {
                VramAccessRec a;
                if (!payload(r, a))
                    return {2, "bad WriteVram record"};
                startBackend();
                backend.WriteVram(a.psm, a.base, a.bw, a.x, a.y, a.value);
                stopBackend();
                break;
            }
            case Rec::ReadVram:
            {
                VramAccessRec a;
                if (!payload(r, a))
                    return {2, "bad ReadVram record"};
                startBackend();
                const uint32_t got = backend.ReadVram(a.psm, a.base, a.bw, a.x, a.y);
                stopBackend();
                if (o.verify && got != a.value && fail(i, "ReadVram value differs"))
                    return res;
                break;
            }
            case Rec::Consume:
            {
                uint32_t maxBytes, n;
                if (r.size < 8)
                    return {2, "bad Consume record"};
                if (staleL2H && fail(i, "Consume observes the stale local->host buffer that was not rebuilt"))
                    return res;
                std::memcpy(&maxBytes, r.data, 4);
                std::memcpy(&n, r.data + 4, 4);
                if (r.size != 8u + n)
                    return {2, "bad Consume record size"};
                std::vector<uint8_t> dst(std::max<uint32_t>(maxBytes, 1u));
                startBackend();
                const uint32_t got = backend.ConsumeLocalToHostBytes(maxBytes ? dst.data() : nullptr, maxBytes);
                stopBackend();
                if (o.stats)
                    o.stats->consumeBytes += got;
                if (o.verify && (got != n || std::memcmp(dst.data(), r.data + 8, n) != 0) &&
                    fail(i, "local->host bytes differ"))
                    return res;
                break;
            }
            case Rec::Flush:
                backend.Flush();
                break;
            case Rec::TextureFlush:
                backend.TextureFlush();
                break;
            case Rec::Sync:
                if (r.size != 1)
                    return {2, "bad Sync record"};
                backend.Sync(static_cast<GSSyncReason>(r.data[0]));
                break;
            case Rec::Reset:
                backend.Reset();
                break;
            case Rec::FpEnv:
            {
                FpEnvRec e;
                if (!payload(r, e))
                    return {2, "bad FpEnv record"};
                if (!o.ignoreFpEnv)
                {
                    _mm_setcsr((_mm_getcsr() & ~kMxcsrControlMask) | (e.mxcsrControl & kMxcsrControlMask));
                    if (currentFpEnv().crtControl != e.crtControl)
                        ++res.crtMismatches; // the CRT view must follow MXCSR; a difference would be unexplained state
                }
                currentEnv = envName(e);
                break;
            }
            case Rec::FpEnvChanged:
                ++res.envChangesDuringCalls;
                break;
            case Rec::VramHash:
            {
                VramHashRec h;
                if (!payload(r, h))
                    return {2, "bad VramHash record"};
                if (o.verify)
                {
                    ++res.vramHashesChecked;
                    if (fnv1a(vram.data(), vram.size()) != h.hash)
                    {
                        // The first differing VRAM state is somewhere in the last interval of mutating calls.
                        size_t from = i;
                        uint32_t seen = 0;
                        while (from > 0 && seen < c.header.vramHashInterval)
                        {
                            --from;
                            const Rec t = c.records[from].type;
                            if (t == Rec::Submit || t == Rec::BeginTransfer || t == Rec::Upload || t == Rec::Clear ||
                                t == Rec::WriteVram || t == Rec::Reset)
                                ++seen;
                        }
                        if (fail(i, "VRAM hash differs after mutating call " + std::to_string(h.mutatingCalls) +
                                        "; first differing state within records " + std::to_string(from) + ".." +
                                        std::to_string(i - 1)))
                            return res;
                    }
                }
                break;
            }
            case Rec::Present:
            {
                PresentRec pr2;
                if (!payload(r, pr2))
                    return {2, "bad Present record"};
                if (o.stats)
                    o.stats->counts["present"]++;
                startBackend();
                const PresentationFrame f = backend.Present(pr2.request);
                stopBackend();
                if (!o.dumpPresent.empty() && f)
                {
                    static int frameIndex = 0;
                    std::ofstream ppm(o.dumpPresent + "_" + std::to_string(frameIndex++) + ".ppm", std::ios::binary);
                    ppm << "P6\n" << f.width << " " << f.height << "\n255\n";
                    for (size_t px = 0; px + 3 < f.pixels.size(); px += 4)
                        ppm.write(reinterpret_cast<const char *>(&f.pixels[px]), 3);
                }
                if (o.verify)
                {
                    const auto v0 = Clock::now();
                    const bool sameFrame = f.width == pr2.width && f.height == pr2.height &&
                                           f.displayFbp == pr2.displayFbp && f.sourceFbp == pr2.sourceFbp &&
                                           (f.usedPreferred ? 1u : 0u) == pr2.usedPreferred &&
                                           f.pixels.size() == pr2.pixelsSize &&
                                           fnv1a(f.pixels.data(), f.pixels.size()) == pr2.pixelsHash;
                    const bool sameVram = fnv1a(vram.data(), vram.size()) == pr2.vramHash;
                    res.verifySeconds += std::chrono::duration<double>(Clock::now() - v0).count();
                    if (!sameVram && fail(i, "VRAM hash differs at Present (tick " + std::to_string(pr2.request.vsyncTick) + ")"))
                        return res;
                    if (!sameFrame && fail(i, "presented frame differs"))
                        return res;
                }
                break;
            }
            case Rec::TransferSnapshot:
            {
                GSTransferSnapshot s;
                if (!payload(r, s))
                    return {2, "bad TransferSnapshot record"};
                if (staleL2H && fail(i, "TransferSnapshot observes the stale local->host buffer that was not rebuilt"))
                    return res;
                if (o.verify && !sameSnapshot(backend.GetTransferSnapshot(), s) && fail(i, "transfer snapshot differs"))
                    return res;
                break;
            }
            case Rec::SnapshotVram:
            {
                SnapshotVramRec s;
                if (!payload(r, s))
                    return {2, "bad SnapshotVram record"};
                if (o.verify)
                {
                    std::vector<uint8_t> out;
                    backend.SnapshotVram(out);
                    if ((out.size() != s.size || fnv1a(out.data(), out.size()) != s.hash) && fail(i, "VRAM snapshot differs"))
                        return res;
                }
                break;
            }
            case Rec::End:
            {
                if (!o.dumpFinal.empty())
                {
                    std::ofstream dump(o.dumpFinal, std::ios::binary);
                    dump.write(reinterpret_cast<const char *>(vram.data()), std::streamsize(vram.size()));
                }
                const auto v0 = Clock::now();
                for (size_t k = 0; k < vram.size(); ++k)
                {
                    if (vram[k] != r.data[k])
                    {
                        std::ostringstream m;
                        m << "final VRAM differs, first byte at offset 0x" << std::hex << k << " (block 0x" << (k / 256)
                          << ")";
                        fail(i, m.str());
                        break;
                    }
                }
                res.verifySeconds += std::chrono::duration<double>(Clock::now() - v0).count();
                break;
            }
            default:
                return {2, "unexpected record type " + std::to_string(int(r.type)) + " at " + std::to_string(i)};
            }
        }
        return res;
    }

    // Leaf-only sampling profiler for the replay thread: every ~1 ms suspends it and records RIP (module offset, or
    // the name of the external module). Exclusive costs only; symbolized offline with the linker map.
    struct RipSampler
    {
        HANDLE target = nullptr;
        std::atomic<bool> stop{false};
        std::thread worker;
        std::map<uint64_t, uint64_t> inModule;
        std::map<std::string, uint64_t> external;
        uint64_t samples = 0;

        void start()
        {
            DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &target,
                            THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0);
            const uint64_t base = reinterpret_cast<uint64_t>(GetModuleHandleW(nullptr));
            worker = std::thread([this, base] {
                timeBeginPeriod(1);
                auto due = Clock::now();
                while (!stop.load())
                {
                    // Sleep(1) gave ~15 ms granularity here: spin to the next 1 ms tick (uses another core).
                    due += std::chrono::milliseconds(1);
                    while (Clock::now() < due && !stop.load())
                        YieldProcessor();
                    if (SuspendThread(target) == DWORD(-1))
                        continue;
                    CONTEXT ctx{};
                    ctx.ContextFlags = CONTEXT_CONTROL;
                    if (GetThreadContext(target, &ctx))
                    {
                        HMODULE mod = nullptr;
                        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                           reinterpret_cast<LPCWSTR>(ctx.Rip), &mod);
                        if (reinterpret_cast<uint64_t>(mod) == base)
                            inModule[ctx.Rip - base]++;
                        else
                        {
                            wchar_t name[MAX_PATH] = L"?";
                            if (mod)
                                GetModuleFileNameW(mod, name, MAX_PATH);
                            std::wstring w(name);
                            external[std::string(w.begin() + (w.find_last_of(L"\\/") + 1), w.end())]++;
                        }
                        ++samples;
                    }
                    ResumeThread(target);
                }
                timeEndPeriod(1);
            });
        }

        void finish(const std::string &path)
        {
            stop = true;
            worker.join();
            CloseHandle(target);
            std::ofstream out(path);
            out << "# samples " << samples << "\n";
            for (const auto &[off, n] : inModule)
                out << "+" << std::hex << off << std::dec << " " << n << "\n";
            for (const auto &[name, n] : external)
                out << "!" << name << " " << n << "\n";
        }
    };

    bool readFile(const std::string &path, std::vector<uint8_t> &out)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return false;
        out.assign(std::istreambuf_iterator<char>(in), {});
        return true;
    }

    // ---------------------------------------------------------------- synthetic scenes (selftest)

    uint64_t gifTag(uint16_t nloop, uint8_t flg, uint8_t nreg, bool eop = true)
    {
        return uint64_t(nloop & 0x7FFFu) | (eop ? (1ull << 15) : 0ull) | (uint64_t(flg & 3u) << 58) | (uint64_t(nreg & 0xFu) << 60);
    }

    void putU64(std::vector<uint8_t> &d, uint64_t v)
    {
        const size_t p = d.size();
        d.resize(p + 8);
        std::memcpy(d.data() + p, &v, 8);
    }

    uint64_t xyz(int x, int y, uint32_t z) { return uint64_t(x * 16) | (uint64_t(y * 16) << 16) | (uint64_t(z) << 32); }
    uint64_t rgbaq(uint8_t r, uint8_t g, uint8_t b, uint8_t a) { return r | (g << 8) | (b << 16) | (uint64_t(a) << 24) | (0x3F800000ull << 32); }
    uint64_t stReg(float s, float t)
    {
        uint32_t a, b;
        std::memcpy(&a, &s, 4);
        std::memcpy(&b, &t, 4);
        return a | (uint64_t(b) << 32);
    }

    // Host->local image transfer of 'data' into (dbp, dbw, dpsm) at (0, 0), w x h pixels.
    void upload(GS &gs, uint32_t dbp, uint32_t dbw, uint32_t dpsm, uint32_t w, uint32_t h, const std::vector<uint8_t> &data)
    {
        gs.writeRegister(GS_REG_BITBLTBUF, (uint64_t(dbp) << 32) | (uint64_t(dbw) << 48) | (uint64_t(dpsm) << 56));
        gs.writeRegister(GS_REG_TRXPOS, 0ull);
        gs.writeRegister(GS_REG_TRXREG, uint64_t(w) | (uint64_t(h) << 32));
        gs.writeRegister(GS_REG_TRXDIR, 0ull);
        std::vector<uint8_t> packet;
        const uint32_t qwords = uint32_t((data.size() + 15) / 16);
        putU64(packet, gifTag(uint16_t(qwords), GIF_FMT_IMAGE, 0));
        putU64(packet, 0ull);
        const size_t p = packet.size();
        packet.resize(p + size_t(qwords) * 16u, 0u);
        std::memcpy(packet.data() + p, data.data(), data.size());
        gs.processGIFPacket(packet.data(), uint32_t(packet.size()));
    }

    void frameSetup(GS &gs, uint64_t test, uint64_t alpha)
    {
        gs.writeRegister(GS_REG_FRAME_1, 0ull | (1ull << 16));                       // FBP 0, FBW 1 (64 px), CT32
        gs.writeRegister(GS_REG_ZBUF_1, 4ull);                                        // ZBP 4, Z32
        gs.writeRegister(GS_REG_SCISSOR_1, 0ull | (63ull << 16) | (0ull << 32) | (63ull << 48));
        gs.writeRegister(GS_REG_XYOFFSET_1, 0ull);
        gs.writeRegister(GS_REG_TEST_1, test);
        gs.writeRegister(GS_REG_ALPHA_1, alpha);
    }

    constexpr uint64_t kZAlways = (1ull << 16) | (1ull << 17);
    constexpr uint64_t kZGequal = (1ull << 16) | (2ull << 17);

    void sceneTriangles(GS &gs)
    {
        frameSetup(gs, kZAlways, 0);
        gs.writeRegister(GS_REG_PRIM, GS_PRIM_TRIANGLE | (1u << 3)); // Gouraud
        gs.writeRegister(GS_REG_RGBAQ, rgbaq(255, 0, 0, 128));
        gs.writeRegister(GS_REG_XYZ2, xyz(2, 2, 100));
        gs.writeRegister(GS_REG_RGBAQ, rgbaq(0, 255, 0, 128));
        gs.writeRegister(GS_REG_XYZ2, xyz(60, 8, 100));
        gs.writeRegister(GS_REG_RGBAQ, rgbaq(0, 0, 255, 128));
        gs.writeRegister(GS_REG_XYZ2, xyz(20, 58, 100));
    }

    void sceneZBuffer(GS &gs)
    {
        frameSetup(gs, kZGequal, 0);
        gs.writeRegister(GS_REG_PRIM, GS_PRIM_TRIANGLE);
        gs.writeRegister(GS_REG_RGBAQ, rgbaq(200, 200, 0, 255));
        gs.writeRegister(GS_REG_XYZ2, xyz(0, 0, 5000));
        gs.writeRegister(GS_REG_XYZ2, xyz(40, 0, 5000));
        gs.writeRegister(GS_REG_XYZ2, xyz(0, 40, 5000));
        gs.writeRegister(GS_REG_RGBAQ, rgbaq(0, 200, 200, 255));
        gs.writeRegister(GS_REG_XYZ2, xyz(10, 10, 2500)); // behind: rejected where the first one was drawn
        gs.writeRegister(GS_REG_XYZ2, xyz(63, 10, 9000));
        gs.writeRegister(GS_REG_XYZ2, xyz(10, 63, 2500));
    }

    void sceneAlphaSprite(GS &gs)
    {
        frameSetup(gs, kZAlways, 0 | (1u << 2) | (0u << 4) | (1u << 6)); // (Cs - Cd) * As + Cd
        gs.writeRegister(GS_REG_PRIM, GS_PRIM_SPRITE | (1u << 6));        // ABE
        gs.writeRegister(GS_REG_RGBAQ, rgbaq(255, 255, 255, 0x40));
        gs.writeRegister(GS_REG_XYZ2, xyz(8, 8, 0));
        gs.writeRegister(GS_REG_XYZ2, xyz(56, 40, 0));
    }

    void sceneTextureCt32(GS &gs, uint32_t seed)
    {
        std::vector<uint8_t> tex(16 * 16 * 4);
        for (size_t i = 0; i < tex.size(); ++i)
            tex[i] = uint8_t((i * 37u + seed) ^ (i >> 3));
        upload(gs, 0x200, 1, GS_PSM_CT32, 16, 16, tex);
        frameSetup(gs, kZAlways, 0);
        gs.writeRegister(GS_REG_TEXFLUSH, 0);
        gs.writeRegister(GS_REG_TEX0_1, 0x200ull | (1ull << 14) | (uint64_t(GS_PSM_CT32) << 20) | (4ull << 26) | (4ull << 30) |
                                            (1ull << 34) | (1ull << 35)); // TCC, TFX decal
        gs.writeRegister(GS_REG_TEX1_1, 1ull << 5);                     // MMAG linear
        gs.writeRegister(GS_REG_CLAMP_1, 0ull);
        gs.writeRegister(GS_REG_PRIM, GS_PRIM_SPRITE | (1u << 4) | (1u << 8)); // TME, FST
        gs.writeRegister(GS_REG_UV, 0ull);
        gs.writeRegister(GS_REG_XYZ2, xyz(30, 30, 0));
        gs.writeRegister(GS_REG_UV, (16ull * 16) | ((16ull * 16) << 16));
        gs.writeRegister(GS_REG_XYZ2, xyz(62, 62, 0));
    }

    void sceneClut(GS &gs)
    {
        std::vector<uint8_t> indices(16 * 16), clut(256 * 4);
        for (size_t i = 0; i < indices.size(); ++i)
            indices[i] = uint8_t(i * 7u + 3u);
        for (size_t i = 0; i < clut.size(); ++i)
            clut[i] = uint8_t(i * 13u + 1u);
        upload(gs, 0x280, 1, GS_PSM_T8, 16, 16, indices);
        upload(gs, 0x300, 1, GS_PSM_CT32, 16, 16, clut);
        frameSetup(gs, kZAlways, 0);
        gs.writeRegister(GS_REG_TEXFLUSH, 0);
        gs.writeRegister(GS_REG_TEX0_1, 0x280ull | (1ull << 14) | (uint64_t(GS_PSM_T8) << 20) | (4ull << 26) | (4ull << 30) |
                                            (1ull << 34) | (0ull << 35) | (0x300ull << 37) | (uint64_t(GS_PSM_CT32) << 51) |
                                            (1ull << 61)); // modulate, CLD=1
        gs.writeRegister(GS_REG_TEX1_1, 0ull);
        gs.writeRegister(GS_REG_PRIM, GS_PRIM_TRIANGLE | (1u << 3) | (1u << 4)); // ST/Q
        gs.writeRegister(GS_REG_RGBAQ, rgbaq(128, 128, 128, 128));
        gs.writeRegister(GS_REG_ST, stReg(0.0f, 0.0f));
        gs.writeRegister(GS_REG_XYZ2, xyz(0, 32, 0));
        gs.writeRegister(GS_REG_ST, stReg(1.0f, 0.0f));
        gs.writeRegister(GS_REG_XYZ2, xyz(31, 32, 0));
        gs.writeRegister(GS_REG_ST, stReg(0.0f, 1.0f));
        gs.writeRegister(GS_REG_XYZ2, xyz(0, 63, 0));
    }

    void sceneLocalToLocal(GS &gs)
    {
        gs.writeRegister(GS_REG_BITBLTBUF, 0ull | (1ull << 16) | (0ull << 24) | (0x400ull << 32) | (1ull << 48) | (0ull << 56));
        gs.writeRegister(GS_REG_TRXPOS, 4ull | (4ull << 16) | (8ull << 32) | (8ull << 48));
        gs.writeRegister(GS_REG_TRXREG, 24ull | (24ull << 32));
        gs.writeRegister(GS_REG_TRXDIR, 2ull);
    }

    std::vector<uint8_t> sceneLocalToHost(GS &gs)
    {
        gs.writeRegister(GS_REG_BITBLTBUF, 0ull | (1ull << 16) | (0ull << 24));
        gs.writeRegister(GS_REG_TRXPOS, 16ull | (16ull << 16));
        gs.writeRegister(GS_REG_TRXREG, 16ull | (16ull << 32));
        gs.writeRegister(GS_REG_TRXDIR, 1ull);
        std::vector<uint8_t> out(16 * 16 * 4);
        const uint32_t a = gs.consumeLocalToHostBytes(out.data(), 300);
        const uint32_t b = gs.consumeLocalToHostBytes(out.data() + a, uint32_t(out.size() - a));
        out.resize(a + b);
        return out;
    }

    GSPresentationRequest presentRequest(uint64_t tick)
    {
        GSPresentationRequest r{};
        r.pmode = 1ull;
        r.dispfb1 = 0ull | (1ull << 9);
        r.display1 = (63ull << 32) | (63ull << 44);
        r.vsyncTick = tick;
        return r;
    }

    // Rounding-sensitive scene: textured (bilinear, ST/Q) Gouraud triangles with sub-pixel vertices.
    void fpScene(GS &gs, uint32_t seed, int count)
    {
        uint32_t s = seed * 2654435761u + 12345u;
        auto next = [&s] { s = s * 1664525u + 1013904223u; return s >> 8; };
        frameSetup(gs, kZAlways, 0);
        gs.writeRegister(GS_REG_TEX0_1, 0x200ull | (1ull << 14) | (uint64_t(GS_PSM_CT32) << 20) | (4ull << 26) | (4ull << 30) |
                                            (1ull << 34) | (0ull << 35)); // modulate, TCC
        gs.writeRegister(GS_REG_TEX1_1, 1ull << 5);                     // MMAG linear
        gs.writeRegister(GS_REG_PRIM, GS_PRIM_TRIANGLE | (1u << 3) | (1u << 4)); // Gouraud, TME, ST/Q
        for (int t = 0; t < count; ++t)
        {
            for (int v = 0; v < 3; ++v)
            {
                gs.writeRegister(GS_REG_RGBAQ, rgbaq(uint8_t(next()), uint8_t(next()), uint8_t(next()), 0x80));
                gs.writeRegister(GS_REG_ST, stReg(float(next() % 1000) / 997.0f, float(next() % 1000) / 991.0f));
                const uint64_t x = next() % (64u * 16u), y = next() % (64u * 16u);
                gs.writeRegister(GS_REG_XYZ2, x | (y << 16) | (uint64_t(next() % 100000u) << 32));
            }
        }
    }

    std::vector<uint8_t> drawFpSceneAlone(int roundingMode, uint32_t seed)
    {
        std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
        GS gs;
        gs.init(vram.data(), uint32_t(vram.size()), nullptr);
        sceneTextureCt32(gs, 5); // uploads the texture fpScene samples (draws with the default mode)
        const int saved = std::fegetround();
        std::fesetround(roundingMode);
        fpScene(gs, seed, 40);
        std::fesetround(saved);
        return vram;
    }

    bool sameEnvState(const FpEnvRec &a, const FpEnvRec &b) { return sameFpEnv(a, b); }

    int fpAndL2HSelftest(const std::string &dir)
    {
        int failures = 0;
        auto check = [&](bool ok, const std::string &what) {
            std::cout << (ok ? "  PASS " : "  FAIL ") << what << "\n";
            failures += ok ? 0 : 1;
        };
        const FpEnvRec processEnv = currentFpEnv();

        // F1. The scene really depends on the rounding mode (otherwise the next tests prove nothing).
        const std::vector<uint8_t> nearest = drawFpSceneAlone(FE_TONEAREST, 1), toward = drawFpSceneAlone(FE_TOWARDZERO, 1);
        size_t differing = 0;
        for (size_t i = 0; i < nearest.size(); ++i)
            differing += nearest[i] != toward[i];
        check(differing != 0, "fp scene is rounding-sensitive (" + std::to_string(differing) + " VRAM bytes differ)");

        // F2. Record the scene in three environments; replay must apply each one.
        const std::string path = dir + "/selftest_fpenv.gsrp";
        std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
        GS gs;
        gs.init(vram.data(), uint32_t(vram.size()), nullptr);
        RecordingBackend::Options opt;
        opt.path = path;
        opt.vsyncs = 0;
        auto recorder = std::make_unique<RecordingBackend>(opt);
        RecordingBackend *rec = recorder.get();
        gs.setRasterBackend(std::move(recorder));
        sceneTextureCt32(gs, 5);
        fpScene(gs, 2, 30);
        const uint32_t savedCsr = _mm_getcsr();
        std::fesetround(FE_TOWARDZERO);
        fpScene(gs, 3, 30);
        std::fesetround(FE_UPWARD);
        _mm_setcsr(_mm_getcsr() | 0x8040u); // FTZ + DAZ
        fpScene(gs, 4, 30);
        _mm_setcsr(savedCsr);
        rec->Present(presentRequest(1));
        check(sameEnvState(currentFpEnv(), processEnv), "selftest restored its own environment");
        check(rec->envChangesDuringCalls() == 0, "no backend call changed the environment");
        check(rec->finish(), "fp-env capture written");
        Capture c;
        std::string err;
        const bool parsed = readFile(path, c.bytes) && parse(c, err);
        check(parsed, "fp-env capture parses" + (err.empty() ? std::string() : " (" + err + ")"));
        if (!parsed)
            return failures;
        std::map<uint32_t, uint64_t> envs;
        for (const Record &r : c.records)
            if (r.type == Rec::FpEnv)
            {
                FpEnvRec e;
                std::memcpy(&e, r.data, sizeof e);
                envs[e.mxcsrControl]++;
            }
        check(envs.size() >= 3, "three distinct environments recorded (" + std::to_string(envs.size()) + ")");
        const ReplayResult ok = replay(c, {});
        check(ok.code == 0 && ok.vramHashesChecked > 0,
              "replay with recorded environments identical (" + std::to_string(ok.vramHashesChecked) + " VRAM hashes)" +
                  (ok.message.empty() ? std::string() : " " + ok.message));
        check(sameEnvState(currentFpEnv(), processEnv), "replayer left the thread environment unchanged");
        ReplayOptions ignore;
        ignore.ignoreFpEnv = true;
        const ReplayResult bad = replay(c, ignore);
        check(bad.code == 1 && bad.message.find("VRAM hash differs after mutating call") != std::string::npos,
              "replay with the wrong environment diverges, localized: " + bad.message);
        check(sameEnvState(currentFpEnv(), processEnv), "environment unchanged after the negative replay");

        // L1. Case A: a local->host buffer half consumed before the window, then an upload (last transfer dir 0);
        // inside the window the rest is consumed and must match.
        const std::string l2hPath = dir + "/selftest_l2h.gsrp";
        std::vector<uint8_t> vram2(PS2_GS_VRAM_SIZE, 0u);
        for (size_t i = 0; i < vram2.size(); i += 64)
            vram2[i] = uint8_t(i >> 6);
        GS gs2;
        gs2.init(vram2.data(), uint32_t(vram2.size()), nullptr);
        uint64_t fakeTick = 0;
        RecordingBackend::Options opt2;
        opt2.path = l2hPath;
        opt2.vsyncs = 0;
        opt2.fromTick = 1;
        opt2.tick = [&fakeTick] { return fakeTick; };
        auto recorder2 = std::make_unique<RecordingBackend>(opt2);
        RecordingBackend *rec2 = recorder2.get();
        gs2.setRasterBackend(std::move(recorder2));
        sceneTriangles(gs2);
        gs2.writeRegister(GS_REG_BITBLTBUF, 0ull | (1ull << 16) | (0ull << 24));
        gs2.writeRegister(GS_REG_TRXPOS, 4ull | (4ull << 16));
        gs2.writeRegister(GS_REG_TRXREG, 32ull | (32ull << 32));
        gs2.writeRegister(GS_REG_TRXDIR, 1ull);
        std::vector<uint8_t> first(1000);
        const uint32_t got1 = gs2.consumeLocalToHostBytes(first.data(), uint32_t(first.size()));
        sceneTextureCt32(gs2, 9); // dir 0 transfer: the half-read buffer stays pending
        sceneAlphaSprite(gs2);    // changes the VRAM the buffer was read from
        fakeTick = 1;
        sceneZBuffer(gs2); // the window opens here
        std::vector<uint8_t> rest(32 * 32 * 4);
        const uint32_t got2 = gs2.consumeLocalToHostBytes(rest.data(), uint32_t(rest.size()));
        (void)rec2->GetTransferSnapshot();
        check(got1 == 1000u && got2 == 32u * 32u * 4u - 1000u, "local->host read split across the window start");
        check(rec2->finish(), "L2H capture written");
        Capture c2;
        err.clear();
        const bool parsed2 = readFile(l2hPath, c2.bytes) && parse(c2, err);
        check(parsed2 && c2.records[1].type == Rec::L2HState,
              "capture holds the pending local->host state" + (err.empty() ? std::string() : " (" + err + ")"));
        if (!parsed2)
            return failures;
        const ReplayResult ok2 = replay(c2, {});
        check(ok2.code == 0, "replay rebuilds the pending buffer: Consume and TransferSnapshot identical" +
                                 (ok2.message.empty() ? std::string() : " (" + ok2.message + ")"));
        return failures;
    }

    int selftest(const std::string &dir)
    {
        int failures = 0;
        auto check = [&](bool ok, const std::string &what) {
            std::cout << (ok ? "  PASS " : "  FAIL ") << what << "\n";
            failures += ok ? 0 : 1;
        };

        // 1. Record every scene through the real GS frontend with the recorder installed as raster backend.
        const std::string path = dir + "/selftest.gsrp";
        std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
        for (size_t i = 0; i < vram.size(); i += 4096)
            vram[i] = uint8_t(i >> 12); // non-zero initial VRAM
        GS gs;
        gs.init(vram.data(), uint32_t(vram.size()), nullptr);
        uint64_t fakeTick = 0;
        RecordingBackend::Options opt;
        opt.path = path;
        opt.vsyncs = 0;
        opt.fromTick = 1;                       // armed: the window opens at the first call once the tick reaches 1
        opt.tick = [&fakeTick] { return fakeTick; };
        opt.log = [](const char *m) { std::cout << "  " << m << "\n"; };
        auto recorder = std::make_unique<RecordingBackend>(opt);
        RecordingBackend *rec = recorder.get();
        gs.setRasterBackend(std::move(recorder));
        // An upload left half done before the window: the prologue must rebuild its hidden state.
        {
            GS &g = gs;
            g.writeRegister(GS_REG_BITBLTBUF, (0x600ull << 32) | (1ull << 48) | (uint64_t(GS_PSM_CT32) << 56));
            g.writeRegister(GS_REG_TRXPOS, 0ull);
            g.writeRegister(GS_REG_TRXREG, 8ull | (8ull << 32));
            g.writeRegister(GS_REG_TRXDIR, 0ull);
        }
        std::vector<uint8_t> half(8 * 4 * 4, 0x5Au);
        {
            std::vector<uint8_t> packet;
            putU64(packet, gifTag(uint16_t(half.size() / 16), GIF_FMT_IMAGE, 0));
            putU64(packet, 0ull);
            packet.insert(packet.end(), half.begin(), half.end());
            gs.processGIFPacket(packet.data(), uint32_t(packet.size()));
        }
        fakeTick = 1; // the next backend call opens the window with 32 of 64 pixels uploaded
        sceneTriangles(gs);
        rec->Present(presentRequest(1));
        sceneZBuffer(gs);
        sceneAlphaSprite(gs);
        sceneTextureCt32(gs, 11);
        sceneClut(gs);
        rec->Present(presentRequest(2));
        sceneLocalToLocal(gs);
        const std::vector<uint8_t> host = sceneLocalToHost(gs);
        gs.clearFramebufferContext(0u, 0x00000000u); // keeps the frame, exercises Clear on a different context state
        gs.WriteVram(GS_PSM_CT32, 0x700, 1, 3, 3, 0xCAFEBABEu);
        (void)gs.ReadVram(GS_PSM_CT32, 0x700, 1, 3, 3);
        std::vector<uint8_t> snap;
        rec->SnapshotVram(snap);
        (void)rec->GetTransferSnapshot();
        rec->Present(presentRequest(3));
        check(rec->state() == RecordingBackend::State::Recording, "recorder is recording");
        check(host.size() == 16u * 16u * 4u, "local->host returned the full rectangle");
        check(rec->finish(), "capture written");

        // 2. Parse and replay into a fresh backend; everything must match.
        Capture c;
        std::string err;
        const bool parsed = readFile(path, c.bytes) && parse(c, err);
        check(parsed, "capture parses" + (err.empty() ? std::string() : " (" + err + ")"));
        if (failures)
            return 1;
        std::map<Rec, uint64_t> kinds;
        for (const Record &r : c.records)
            kinds[r.type]++;
        std::cout << "  records:";
        for (const auto &[k, v] : kinds)
            std::cout << " " << recName(k) << "=" << v;
        std::cout << "\n";
        // Inside the window: 6 draws (triangle, 2 z-tested, alpha sprite, CT32 sprite, CLUT triangle); 5 transfers
        // (CT32 texture, T8 indices, CLUT, local->local, local->host) and 3 uploads; the half upload is before it.
        check(kinds[Rec::Submit] == 6 && kinds[Rec::Upload] == 3 && kinds[Rec::Consume] == 2 && kinds[Rec::Present] == 3 &&
                  kinds[Rec::BeginTransfer] == 5 && kinds[Rec::Clear] == 1 && kinds[Rec::ReadVram] == 1 &&
                  kinds[Rec::WriteVram] == 1,
              "every scene reached the backend");
        PrologueRec p{};
        std::memcpy(&p, c.records[0].data, sizeof p);
        check(p.hasCommand == 1 && p.command.direction == 0u && p.uploadCalls >= 1 && p.snapshot.direction == 0u &&
                  p.snapshot.copiedPixels == 32u && p.snapshot.totalPixels == 64u,
              "prologue holds the transfer in progress (32 of 64 pixels)");
        const ReplayResult ok = replay(c, {});
        check(ok.code == 0 && ok.divergences == 0, "replay identical" + (ok.message.empty() ? std::string() : " (" + ok.message + ")"));
        check(std::memcmp(c.records.back().data, vram.data(), vram.size()) == 0, "End record equals the live VRAM");

        // 3. Negative tests: a dropped draw must be reported as a backend divergence, a flipped byte as a format error.
        long long firstSubmit = -1;
        for (size_t i = 2; i < c.records.size() && firstSubmit < 0; ++i)
            if (c.records[i].type == Rec::Submit)
                firstSubmit = static_cast<long long>(i);
        ReplayOptions skip;
        skip.skipRecord = firstSubmit;
        const ReplayResult bad = replay(c, skip);
        check(bad.code == 1, "dropping record " + std::to_string(firstSubmit) + " is detected: " + bad.message);
        Capture corrupt;
        corrupt.bytes = c.bytes;
        corrupt.bytes[sizeof(Header) + 40] ^= 0x01u;
        err.clear();
        const bool corruptParsed = parse(corrupt, err);
        check(!corruptParsed && err == "integrity hash mismatch", "flipped byte is a format error (" + err + ")");
        failures += fpAndL2HSelftest(dir);
        std::cout << (failures ? "SELFTEST FAILED: " + std::to_string(failures) : std::string("SELFTEST OK")) << "\n";
        return failures ? 1 : 0;
    }

    int replayCommand(int argc, char **argv)
    {
        const std::string path = argv[2];
        bool stats = false, cont = false, allowStale = false, roundingHeuristic = false, ignoreFpEnv = false, byClass = false,
             forceZte = false;
        std::string dumpPresent, dumpAfterPath;
        long long dumpAfterRecord = -1;
        std::string profilePath;
        int profileRepeats = 1;
        std::string dumpFinal;
        int bench = 0;
        for (int i = 3; i < argc; ++i)
        {
            const std::string a = argv[i];
            if (a == "--stats") stats = true;
            else if (a == "--continue") cont = true;
            else if (a == "--bench" && i + 1 < argc) bench = std::atoi(argv[++i]);
            else if (a == "--allow-unobserved-stale-l2h") allowStale = true;
            else if (a == "--dump-final" && i + 1 < argc) dumpFinal = argv[++i];
            else if (a == "--rounding-heuristic") roundingHeuristic = true;
            else if (a == "--ignore-fpenv") ignoreFpEnv = true;
            else if (a == "--by-class") byClass = true;
            else if (a == "--force-zte") forceZte = true;
            else if (a == "--dump-after" && i + 2 < argc) { dumpAfterRecord = std::atoll(argv[++i]); dumpAfterPath = argv[++i]; }
            else if (a == "--dump-present" && i + 1 < argc) dumpPresent = argv[++i];
            else if (a == "--profile" && i + 2 < argc) { profilePath = argv[++i]; profileRepeats = std::atoi(argv[++i]); }
        }
        Capture c;
        if (!readFile(path, c.bytes))
            return std::cerr << "cannot read " << path << "\n", 3;
        std::string err;
        if (!parse(c, err))
            return std::cout << "FORMAT ERROR: " << err << "\n", 2;
        std::cout << "capture: " << c.records.size() << " records, " << c.bytes.size() << " bytes, start tick "
                  << c.header.startTick << "\n";
        for (int i = 3; i < argc; ++i)
            if (std::string(argv[i]) == "--dump-znever")
                return dumpZNever(c), 0;
        Stats st;
        ReplayOptions o;
        o.continueAfterDivergence = cont;
        o.stats = stats ? &st : nullptr;
        o.allowUnobservedStaleL2H = allowStale;
        o.dumpFinal = dumpFinal;
        o.roundingHeuristic = roundingHeuristic;
        o.ignoreFpEnv = ignoreFpEnv;
        o.forceZte = forceZte;
        o.dumpPresent = dumpPresent;
        o.dumpAfterRecord = dumpAfterRecord;
        o.dumpAfterPath = dumpAfterPath;
        const ReplayResult r = replay(c, o);
        if (r.code == 2)
            return std::cout << "FORMAT ERROR: " << r.message << "\n", 2;
        std::cout << (r.code == 0 ? "IDENTICAL" + (r.message.empty() ? std::string() : " (" + r.message + ")")
                                  : "DIVERGENCE: " + r.message)
                  << " (divergences " << r.divergences << ")\n";
        std::cout << "  VRAM hashes checked " << r.vramHashesChecked << "; fp-env changes during calls "
                  << r.envChangesDuringCalls << "; CRT control word differing after applying MXCSR "
                  << r.crtMismatches << "\n  calls by recorded environment:\n";
        for (const auto &[k, v] : r.callsByEnv)
            std::cout << "    " << v << "  " << k << "\n";
        if (stats)
        {
            for (const auto &[k, v] : st.counts)
                std::cout << "  " << k << " = " << v << "\n";
            std::cout << "  triangle bounding-box pixels = " << st.triangleBoxPixels << "\n  sprite pixels = " << st.spritePixels
                      << "\n  upload bytes = " << st.uploadBytes << "\n  local->host bytes = " << st.consumeBytes << "\n";
            for (const auto &[k, v] : st.pixelsBy)
                std::cout << "  pixels." << k << " = " << v << "\n";
        }
        for (int i = 0; i < bench && (r.code == 0 || (cont && r.code == 1)); ++i)
        {
            ReplayOptions fast;
            fast.verify = false;
            fast.allowUnobservedStaleL2H = allowStale;
            fast.roundingHeuristic = roundingHeuristic;
            fast.ignoreFpEnv = ignoreFpEnv;
            fast.forceZte = forceZte;
            ReplayOptions checked;
            checked.ignoreFpEnv = ignoreFpEnv;
            checked.forceZte = forceZte;
            checked.allowUnobservedStaleL2H = allowStale;
            checked.roundingHeuristic = roundingHeuristic;
            const auto t0 = Clock::now();
            const ReplayResult b0 = replay(c, fast);
            const double wall0 = std::chrono::duration<double>(Clock::now() - t0).count();
            const auto t1 = Clock::now();
            const ReplayResult b1 = replay(c, checked);
            const double wall1 = std::chrono::duration<double>(Clock::now() - t1).count();
            ReplayOptions nearest = fast;
            nearest.ignoreFpEnv = true; // timing only: every call with the replayer's round-to-nearest
            nearest.forceZte = forceZte;
            const ReplayResult b2 = replay(c, nearest);
            std::cout << "  bench " << i << ": no-verify backend " << b0.backendSeconds * 1000 << " ms (wall " << wall0 * 1000
                      << " ms); verify backend " << b1.backendSeconds * 1000 << " ms + checks " << b1.verifySeconds * 1000
                      << " ms (wall " << wall1 * 1000 << " ms); all-nearest backend " << b2.backendSeconds * 1000 << " ms\n";
        }
        if (byClass && (r.code == 0 || (cont && r.code == 1)))
        {
            std::map<std::string, std::pair<uint64_t, double>> classes;
            ReplayOptions timed;
            timed.verify = false;
            timed.allowUnobservedStaleL2H = allowStale;
            timed.timeByClass = &classes;
            const ReplayResult t = replay(c, timed);
            std::vector<std::pair<std::string, std::pair<uint64_t, double>>> rows(classes.begin(), classes.end());
            std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) { return a.second.second > b.second.second; });
            std::cout << "  Submit time by class (backend " << t.backendSeconds * 1000 << " ms total):\n";
            for (const auto &[k, v] : rows)
                std::cout << "    " << v.second * 1000 << " ms  " << v.first << " lots  " << k << "\n";
        }
        if (!profilePath.empty() && r.code == 0)
        {
            ReplayOptions timed;
            timed.verify = false;
            timed.allowUnobservedStaleL2H = allowStale;
            RipSampler sampler;
            sampler.start();
            double backend = 0.0;
            for (int k = 0; k < profileRepeats; ++k)
                backend += replay(c, timed).backendSeconds;
            sampler.finish(profilePath);
            std::cout << "  profile: " << profileRepeats << " replays, backend " << backend * 1000 << " ms, samples "
                      << sampler.samples << " -> " << profilePath << "\n";
        }
        return r.code;
    }
}

namespace
{
    // Dispatch cost of one VRAM read, isolated: the same GSMem readers called through a std::function table (as
    // GSCpuBackend does), a plain function-pointer table, and directly. Results are summed so nothing is elided.
    using ReadFn = uint32_t (*)(uint8_t *, uint32_t, uint32_t, uint32_t, uint32_t);
    using ReadStdFn = std::function<uint32_t(uint8_t *, uint32_t, uint32_t, uint32_t, uint32_t)>;

    struct StdTable
    {
        std::array<ReadStdFn, 64> f;
        uint8_t *vram;
        __declspec(noinline) uint32_t read(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const
        {
            if (!vram)
                return 0u;
            return f[psm & 0x3Fu](vram, base, bw, x, y);
        }
    };

    struct PtrTable
    {
        std::array<ReadFn, 64> f;
        uint8_t *vram;
        __declspec(noinline) uint32_t read(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const
        {
            if (!vram)
                return 0u;
            return f[psm & 0x3Fu](vram, base, bw, x, y);
        }
    };

    int microbench()
    {
        std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE);
        for (size_t i = 0; i < vram.size(); ++i)
            vram[i] = uint8_t(i * 2654435761u >> 24);
        GSCpuBackend initTables; // initializes the GSMem lookup tables
        (void)initTables;
        StdTable st{};
        PtrTable pt{};
        st.vram = pt.vram = vram.data();
        for (int i = 0; i < 64; ++i)
        {
            st.f[i] = GSMem::ReadNull;
            pt.f[i] = GSMem::ReadNull;
        }
        st.f[GS_PSM_CT32] = GSMem::ReadCT32;
        pt.f[GS_PSM_CT32] = GSMem::ReadCT32;
        st.f[GS_PSM_T8] = GSMem::ReadP8;
        pt.f[GS_PSM_T8] = GSMem::ReadP8;
        struct Case
        {
            const char *name;
            uint32_t psm, base, bw;
        } cases[] = {{"CT32 frame", GS_PSM_CT32, 0x0, 8}, {"T8 texture", GS_PSM_T8, 0x2000, 4}};
        constexpr uint32_t kN = 1u << 24;
        for (const Case &k : cases)
        {
            for (int rep = 0; rep < 3; ++rep)
            {
                uint64_t sum = 0;
                auto run = [&](auto &&fn) {
                    const auto t0 = Clock::now();
                    for (uint32_t i = 0; i < kN; ++i)
                    {
                        // row-major spans of 64 pixels, as a scanline of a sprite or triangle
                        const uint32_t x = i & 511u, y = (i >> 9) & 255u;
                        sum += fn(k.psm, k.base, k.bw, x, y);
                    }
                    return std::chrono::duration<double, std::nano>(Clock::now() - t0).count() / kN;
                };
                const double ns0 = run([&](uint32_t p, uint32_t b, uint32_t w, uint32_t x, uint32_t y) { return st.read(p, b, w, x, y); });
                const double ns1 = run([&](uint32_t p, uint32_t b, uint32_t w, uint32_t x, uint32_t y) { return pt.read(p, b, w, x, y); });
                const double ns2 = run([&](uint32_t, uint32_t b, uint32_t w, uint32_t x, uint32_t y) {
                    return k.psm == GS_PSM_CT32 ? GSMem::ReadCT32(vram.data(), b, w, x, y) : GSMem::ReadP8(vram.data(), b, w, x, y);
                });
                std::printf("%-11s rep %d: std::function %.2f ns  function pointer %.2f ns  direct %.2f ns per read (sum %llu)\n",
                            k.name, rep, ns0, ns1, ns2, (unsigned long long)(sum & 0xFFFF));
            }
        }
        return 0;
    }
}

int main(int argc, char **argv)
{
    if (argc >= 2 && std::string(argv[1]) == "microbench")
        return microbench();
    if (argc >= 3 && std::string(argv[1]) == "selftest")
        return selftest(argv[2]);
    if (argc >= 3 && std::string(argv[1]) == "replay")
        return replayCommand(argc, argv);
    std::cerr << "usage: gs_replay selftest <dir> | microbench | replay <capture> [--stats] [--bench N] [--continue] "
                 "[--allow-unobserved-stale-l2h] [--dump-final <file>] [--rounding-heuristic] [--ignore-fpenv] "
                 "[--by-class] [--force-zte] [--dump-after <record> <file>] [--dump-present <prefix>] "
                 "[--profile <file> <repeats>] [--dump-znever]\n";
    return 3;
}
