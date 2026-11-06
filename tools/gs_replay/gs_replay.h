// Deterministic GS replay bench, capture side (introduced in PERF_003 E2), shared by the opt-in capture hook
// (capture_hook.inc) and the offline gs_replay tool. Generic: no game data.
//
// RecordingBackend wraps the reference GSCpuBackend behind GSRasterBackend (installed with
// GS::setRasterBackend). Every call is forwarded unchanged; while a capture window is open, the call, its
// arguments, its payloads (deep copies) and its observable outputs are appended to an in-memory log that is
// written once when the window closes. The recorder only observes: it never changes the calling thread's
// floating-point environment, the backend state or the call order.
//
// Hidden backend state at the window start (not visible through the interface):
// - the current transfer: the recorder tracks, since installation, the last BeginTransfer and the size of every
//   UploadImage / the bytes consumed after it (Prologue); the replayer repeats them;
// - the local->host buffer: BeginTransfer(direction 1) fills it from the VRAM of that moment and only a later
//   local->host transfer or Reset replaces it, so it can outlive other transfers. The recorder copies the VRAM
//   just before every local->host BeginTransfer and counts the bytes consumed from that buffer; if bytes are pending
//   at the window start it writes that VRAM, the command and the count (L2HState) and the replayer regenerates the
//   same buffer and read position. Pending bytes whose origin was not observed cannot be rebuilt: the window does
//   not open (the capture fails after the wait limit).
//
// Floating-point environment: the rasteriser's float arithmetic depends on the SSE rounding mode (and FTZ/DAZ),
// which differs between callers (VU1 code runs with FE_TOWARDZERO). Every call records the caller's MXCSR control
// bits, the CRT control word and fegetround() on entry (FpEnv record when it changes) and reports a change during a
// call (FpEnvChanged).
//
// File: Header, Prologue, [L2HState], InitVram, FpEnv, calls (with FpEnv and VramHash records interleaved), End
// (final VRAM), Footer (record count + FNV-1a of every byte before the footer). Structs are written as raw bytes;
// the header stores their sizes and the replayer refuses a file written with a different layout.
#pragma once

#include "runtime/gs/gs_cpu_backend.h"

#include <algorithm>
#include <cfenv>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <vector>
#include <xmmintrin.h>

namespace gsreplay
{
    inline constexpr char kMagic[8] = {'D', 'M', 'C', 'G', 'S', 'R', 'P', '1'};
    inline constexpr char kFooterMagic[8] = {'G', 'S', 'R', 'P', 'E', 'N', 'D', '1'};
    inline constexpr uint32_t kVersion = 2u;
    inline constexpr uint32_t kVramHashInterval = 16u; // VramHash after every 16 VRAM-mutating calls

    enum class Rec : uint8_t
    {
        Prologue = 1,
        InitVram = 2,
        Submit = 3,
        BeginTransfer = 4,
        Upload = 5,
        Clear = 6,
        WriteVram = 7,
        ReadVram = 8,
        Consume = 9,
        Flush = 10,
        TextureFlush = 11,
        Sync = 12,
        Present = 13,
        TransferSnapshot = 14,
        SnapshotVram = 15,
        Reset = 16,
        End = 17,
        FpEnv = 18,        // caller's floating-point controls for the following calls
        VramHash = 19,     // hash of the whole VRAM after the preceding call
        FpEnvChanged = 20, // the environment differed after a call (observation only)
        L2HState = 21,     // pending local->host buffer at the window start (prologue)
    };

    static_assert(std::is_trivially_copyable_v<GSPrimitiveBatch>);
    static_assert(std::is_trivially_copyable_v<GSTransferCommand>);
    static_assert(std::is_trivially_copyable_v<GSTransferSnapshot>);
    static_assert(std::is_trivially_copyable_v<GSContext>);
    static_assert(std::is_trivially_copyable_v<GSPresentationRequest>);

    inline uint64_t fnv1a(const void *data, size_t size, uint64_t h = 1469598103934665603ull)
    {
        const auto *p = static_cast<const uint8_t *>(data);
        for (size_t i = 0; i < size; ++i)
        {
            h ^= p[i];
            h *= 1099511628211ull;
        }
        return h;
    }

    // Explicit, portable description of the floating-point controls (not an opaque fenv_t).
    struct FpEnvRec
    {
        uint32_t mxcsrControl; // MXCSR & 0xFFC0: DAZ (6), exception masks (7-12), rounding control (13-14), FTZ (15)
        uint32_t crtControl;   // _control87(0, 0) on MSVC: the CRT's abstract control word (on x64 it reflects the
                               // SSE unit; the x87 control word is not readable there and MSVC x64 emits no x87 math)
        int32_t feRound;       // fegetround()
        uint32_t reserved;
    };

    inline constexpr uint32_t kMxcsrControlMask = 0xFFC0u;

    inline FpEnvRec currentFpEnv()
    {
        FpEnvRec e{};
        e.mxcsrControl = _mm_getcsr() & kMxcsrControlMask;
#if defined(_MSC_VER)
        e.crtControl = _control87(0, 0);
#endif
        e.feRound = std::fegetround();
        return e;
    }

    inline bool sameFpEnv(const FpEnvRec &a, const FpEnvRec &b)
    {
        return a.mxcsrControl == b.mxcsrControl && a.crtControl == b.crtControl && a.feRound == b.feRound;
    }

    struct Header
    {
        char magic[8];
        uint32_t version;
        uint32_t vramSize;
        uint32_t sizeBatch, sizeTransferCommand, sizeTransferSnapshot, sizeContext, sizePresentationRequest;
        uint32_t vramHashInterval;
        uint64_t startTick;
    };

    struct PrologueRec
    {
        uint8_t hasCommand;
        GSTransferCommand command;
        uint64_t uploadedBytes;
        uint64_t consumedBytes;
        GSTransferSnapshot snapshot;
        uint32_t uploadCalls; // followed by uploadCalls uint32_t sizes, one per UploadImage call
    };

    struct L2HStateRec
    {
        GSTransferCommand command; // the local->host BeginTransfer that filled the pending buffer
        uint64_t consumedBytes;    // bytes consumed from that buffer before the window
        // followed by the VRAM (vramSize bytes) just before that BeginTransfer
    };

    struct ClearRec
    {
        GSContext context;
        uint32_t rgba;
        uint8_t result;
    };

    struct VramAccessRec
    {
        uint32_t psm, base, bw, x, y, value;
    };

    struct PresentRec
    {
        GSPresentationRequest request;
        uint32_t width, height, displayFbp, sourceFbp;
        uint8_t usedPreferred;
        uint64_t pixelsHash;
        uint64_t pixelsSize;
        uint64_t vramHash;
    };

    struct SnapshotVramRec
    {
        uint64_t size;
        uint64_t hash;
    };

    struct VramHashRec
    {
        uint64_t mutatingCalls; // number of VRAM-mutating calls recorded so far
        uint64_t hash;
    };

    struct FpEnvChangedRec
    {
        uint8_t call; // Rec of the call
        FpEnvRec before;
        FpEnvRec after;
    };

    struct Footer
    {
        char magic[8];
        uint64_t records;
        uint64_t hash;
    };

    inline void fillHeader(Header &h, uint32_t vramSize, uint64_t startTick)
    {
        std::memset(&h, 0, sizeof h);
        std::memcpy(h.magic, kMagic, sizeof kMagic);
        h.version = kVersion;
        h.vramSize = vramSize;
        h.sizeBatch = sizeof(GSPrimitiveBatch);
        h.sizeTransferCommand = sizeof(GSTransferCommand);
        h.sizeTransferSnapshot = sizeof(GSTransferSnapshot);
        h.sizeContext = sizeof(GSContext);
        h.sizePresentationRequest = sizeof(GSPresentationRequest);
        h.vramHashInterval = kVramHashInterval;
        h.startTick = startTick;
    }

    class RecordingBackend final : public GSRasterBackend
    {
    public:
        struct Options
        {
            std::string path;
            uint64_t fromTick = 0;
            uint64_t vsyncs = 1;                  // window length in VSync ticks (0 = until finish())
            size_t maxBytes = size_t(256) << 20;  // the capture fails if the log grows past this
            uint64_t maxWaitTicks = 600;          // give up if no representable start within this many ticks
            std::function<uint64_t()> tick;       // emulated VSync tick (empty: always 0)
            std::function<void(const char *)> log;
        };

        enum class State
        {
            Armed,
            Recording,
            Done,
            Failed,
        };

        explicit RecordingBackend(Options options)
            : m_options(std::move(options)), m_inner(std::make_unique<GSCpuBackend>())
        {
        }

        ~RecordingBackend() override
        {
            std::lock_guard<std::mutex> lock(m_lock);
            if (m_state == State::Recording)
                finishLocked("closed before the window ended");
        }

        // Ends an open capture now (used by the synthetic tests and at shutdown).
        bool finish()
        {
            std::lock_guard<std::mutex> lock(m_lock);
            if (m_state != State::Recording)
                return false;
            finishLocked(nullptr);
            return m_state == State::Done;
        }

        State state() const
        {
            std::lock_guard<std::mutex> lock(m_lock);
            return m_state;
        }

        std::string failure() const
        {
            std::lock_guard<std::mutex> lock(m_lock);
            return m_failure;
        }

        uint64_t envChangesDuringCalls() const
        {
            std::lock_guard<std::mutex> lock(m_lock);
            return m_envChangedCount;
        }

        void Initialize(uint8_t *vram, uint32_t vramSize) override
        {
            std::lock_guard<std::mutex> lock(m_lock);
            if (m_state == State::Recording)
                failLocked("Initialize during the capture window");
            m_vram = vram;
            m_vramSize = vramSize;
            m_inner->Initialize(vram, vramSize);
        }

        void Reset() override
        {
            const FpEnvRec env = currentFpEnv();
            std::lock_guard<std::mutex> lock(m_lock);
            pre(env);
            m_inner->Reset();
            m_hasCommand = false;
            m_uploaded = m_consumed = 0;
            m_uploadSizes.clear();
            m_haveL2H = false;
            m_l2hVram.clear();
            put(Rec::Reset, nullptr, 0);
            post(Rec::Reset, env, true);
        }

        void Submit(const GSPrimitiveBatch &batch) override
        {
            const FpEnvRec env = currentFpEnv();
            std::lock_guard<std::mutex> lock(m_lock);
            pre(env);
            m_inner->Submit(batch);
            put(Rec::Submit, &batch, sizeof batch);
            post(Rec::Submit, env, true);
        }

        void BeginTransfer(const GSTransferCommand &command) override
        {
            const FpEnvRec env = currentFpEnv();
            std::lock_guard<std::mutex> lock(m_lock);
            pre(env);
            if (command.direction == 1u && m_vram)
            {
                // The new local->host buffer is read from the VRAM of this moment.
                m_l2hVram.assign(m_vram, m_vram + m_vramSize);
                m_l2hCommand = command;
                m_l2hConsumed = 0;
                m_haveL2H = true;
            }
            m_inner->BeginTransfer(command);
            m_hasCommand = true;
            m_command = command;
            m_uploaded = m_consumed = 0;
            m_uploadSizes.clear();
            put(Rec::BeginTransfer, &command, sizeof command);
            post(Rec::BeginTransfer, env, true);
        }

        void UploadImage(const uint8_t *data, uint32_t sizeBytes) override
        {
            const FpEnvRec env = currentFpEnv();
            std::lock_guard<std::mutex> lock(m_lock);
            pre(env);
            m_inner->UploadImage(data, sizeBytes);
            if (data)
            {
                m_uploaded += sizeBytes;
                m_uploadSizes.push_back(sizeBytes);
            }
            if (recording())
            {
                const uint32_t n = data ? sizeBytes : 0u;
                begin(Rec::Upload, sizeof(uint32_t) + n);
                if (recording())
                {
                    append(&n, sizeof n);
                    if (n)
                        append(data, n);
                }
            }
            post(Rec::Upload, env, true);
        }

        void Flush() override
        {
            const FpEnvRec env = currentFpEnv();
            std::lock_guard<std::mutex> lock(m_lock);
            pre(env);
            m_inner->Flush();
            put(Rec::Flush, nullptr, 0);
            post(Rec::Flush, env, false);
        }

        void TextureFlush() override
        {
            const FpEnvRec env = currentFpEnv();
            std::lock_guard<std::mutex> lock(m_lock);
            pre(env);
            m_inner->TextureFlush();
            put(Rec::TextureFlush, nullptr, 0);
            post(Rec::TextureFlush, env, false);
        }

        void Sync(GSSyncReason reason) override
        {
            const FpEnvRec env = currentFpEnv();
            std::lock_guard<std::mutex> lock(m_lock);
            pre(env);
            m_inner->Sync(reason);
            const uint8_t r = static_cast<uint8_t>(reason);
            put(Rec::Sync, &r, sizeof r);
            post(Rec::Sync, env, false);
        }

        PresentationFrame Present(const GSPresentationRequest &request) override
        {
            const FpEnvRec env = currentFpEnv();
            std::lock_guard<std::mutex> lock(m_lock);
            pre(env);
            PresentationFrame frame = m_inner->Present(request);
            if (recording())
            {
                PresentRec r{};
                r.request = request;
                r.width = frame.width;
                r.height = frame.height;
                r.displayFbp = frame.displayFbp;
                r.sourceFbp = frame.sourceFbp;
                r.usedPreferred = frame.usedPreferred ? 1u : 0u;
                r.pixelsHash = fnv1a(frame.pixels.data(), frame.pixels.size());
                r.pixelsSize = frame.pixels.size();
                r.vramHash = m_vram ? fnv1a(m_vram, m_vramSize) : 0u;
                put(Rec::Present, &r, sizeof r);
            }
            post(Rec::Present, env, false);
            return frame;
        }

        bool ClearFramebuffer(const GSContext &context, uint32_t rgba) override
        {
            const FpEnvRec env = currentFpEnv();
            std::lock_guard<std::mutex> lock(m_lock);
            pre(env);
            const bool result = m_inner->ClearFramebuffer(context, rgba);
            if (recording())
            {
                ClearRec r{};
                r.context = context;
                r.rgba = rgba;
                r.result = result ? 1u : 0u;
                put(Rec::Clear, &r, sizeof r);
            }
            post(Rec::Clear, env, true);
            return result;
        }

        uint32_t ConsumeLocalToHostBytes(uint8_t *dst, uint32_t maxBytes) override
        {
            const FpEnvRec env = currentFpEnv();
            std::lock_guard<std::mutex> lock(m_lock);
            pre(env);
            const uint32_t n = m_inner->ConsumeLocalToHostBytes(dst, maxBytes);
            m_consumed += n;
            m_l2hConsumed += n;
            if (recording())
            {
                begin(Rec::Consume, 2 * sizeof(uint32_t) + n);
                if (recording())
                {
                    append(&maxBytes, sizeof maxBytes);
                    append(&n, sizeof n);
                    if (n)
                        append(dst, n);
                }
            }
            post(Rec::Consume, env, false);
            return n;
        }

        uint32_t ReadVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const override
        {
            const FpEnvRec env = currentFpEnv();
            std::lock_guard<std::mutex> lock(m_lock);
            pre(env);
            const uint32_t value = m_inner->ReadVram(psm, base, bw, x, y);
            const VramAccessRec r{psm, base, bw, x, y, value};
            put(Rec::ReadVram, &r, sizeof r);
            post(Rec::ReadVram, env, false);
            return value;
        }

        void WriteVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value) override
        {
            const FpEnvRec env = currentFpEnv();
            std::lock_guard<std::mutex> lock(m_lock);
            pre(env);
            m_inner->WriteVram(psm, base, bw, x, y, value);
            const VramAccessRec r{psm, base, bw, x, y, value};
            put(Rec::WriteVram, &r, sizeof r);
            post(Rec::WriteVram, env, true);
        }

        void SnapshotVram(std::vector<uint8_t> &out) const override
        {
            const FpEnvRec env = currentFpEnv();
            std::lock_guard<std::mutex> lock(m_lock);
            pre(env);
            m_inner->SnapshotVram(out);
            if (recording())
            {
                const SnapshotVramRec r{out.size(), fnv1a(out.data(), out.size())};
                put(Rec::SnapshotVram, &r, sizeof r);
            }
            post(Rec::SnapshotVram, env, false);
        }

        GSTransferSnapshot GetTransferSnapshot() const override
        {
            const FpEnvRec env = currentFpEnv();
            std::lock_guard<std::mutex> lock(m_lock);
            pre(env);
            const GSTransferSnapshot s = m_inner->GetTransferSnapshot();
            put(Rec::TransferSnapshot, &s, sizeof s);
            post(Rec::TransferSnapshot, env, false);
            return s;
        }

    private:
        bool recording() const { return m_state == State::Recording; }

        uint64_t tick() const { return m_options.tick ? m_options.tick() : 0u; }

        void log(const std::string &message) const
        {
            if (m_options.log)
                m_options.log(message.c_str());
        }

        // Called with m_lock held, before forwarding a call: opens or closes the window at a call boundary, then
        // records the caller's floating-point controls if they changed.
        void pre(const FpEnvRec &env) const
        {
            if (m_state == State::Armed && m_vram)
            {
                const uint64_t now = tick();
                if (now < m_options.fromTick)
                    return;
                const GSTransferSnapshot s = m_inner->GetTransferSnapshot();
                // Pending local->host bytes, whatever the last transfer direction: rebuildable only if this recorder
                // saw the BeginTransfer that filled the buffer (case A); otherwise wait (case C).
                if (s.localToHostPendingBytes != 0u && !m_haveL2H)
                {
                    if (now - m_options.fromTick > m_options.maxWaitTicks)
                        failLocked("pending local->host bytes of unobserved origin during the whole wait limit");
                    return;
                }
                startLocked(now, s);
            }
            else if (m_state == State::Recording && m_options.vsyncs != 0u &&
                     tick() >= m_startTick + m_options.vsyncs)
            {
                finishLocked(nullptr);
            }
            if (recording() && (!m_envRecorded || !sameFpEnv(env, m_lastEnv)))
            {
                put(Rec::FpEnv, &env, sizeof env);
                m_lastEnv = env;
                m_envRecorded = true;
            }
        }

        // Called with m_lock held, after forwarding a call: detects a change of the environment during the call and
        // writes the periodic VRAM hash.
        void post(Rec call, const FpEnvRec &before, bool mutatesVram) const
        {
            const FpEnvRec after = currentFpEnv();
            if (!sameFpEnv(before, after))
            {
                ++m_envChangedCount;
                if (recording())
                {
                    const FpEnvChangedRec r{static_cast<uint8_t>(call), before, after};
                    put(Rec::FpEnvChanged, &r, sizeof r);
                }
            }
            if (mutatesVram && recording() && ++m_mutatingCalls % kVramHashInterval == 0u)
            {
                const VramHashRec r{m_mutatingCalls, fnv1a(m_vram, m_vramSize)};
                put(Rec::VramHash, &r, sizeof r);
            }
        }

        void startLocked(uint64_t now, const GSTransferSnapshot &snapshot) const
        {
            m_log.clear();
            m_records = 0;
            m_mutatingCalls = 0;
            m_envRecorded = false;
            m_startTick = now;
            Header h;
            fillHeader(h, m_vramSize, now);
            append(&h, sizeof h);
            PrologueRec p{};
            p.hasCommand = m_hasCommand ? 1u : 0u;
            p.command = m_command;
            p.uploadedBytes = m_uploaded;
            p.consumedBytes = m_consumed;
            p.snapshot = snapshot;
            p.uploadCalls = static_cast<uint32_t>(m_uploadSizes.size());
            m_state = State::Recording;
            begin(Rec::Prologue, sizeof p + m_uploadSizes.size() * sizeof(uint32_t));
            append(&p, sizeof p);
            if (!m_uploadSizes.empty())
                append(m_uploadSizes.data(), m_uploadSizes.size() * sizeof(uint32_t));
            if (snapshot.localToHostPendingBytes != 0u)
            {
                const L2HStateRec l{m_l2hCommand, m_l2hConsumed};
                begin(Rec::L2HState, sizeof l + m_l2hVram.size());
                append(&l, sizeof l);
                append(m_l2hVram.data(), m_l2hVram.size());
            }
            put(Rec::InitVram, m_vram, m_vramSize);
            log("[GSREPLAY] capture started at tick " + std::to_string(now) +
                (snapshot.localToHostPendingBytes ? " with " + std::to_string(snapshot.localToHostPendingBytes) +
                                                        " pending local->host bytes (rebuildable)"
                                                  : std::string()));
        }

        void finishLocked(const char *note) const
        {
            put(Rec::End, m_vram, m_vramSize);
            Footer f{};
            std::memcpy(f.magic, kFooterMagic, sizeof kFooterMagic);
            f.records = m_records;
            f.hash = fnv1a(m_log.data(), m_log.size());
            append(&f, sizeof f);
            m_state = State::Done;
            std::FILE *file = std::fopen(m_options.path.c_str(), "wb");
            if (!file || std::fwrite(m_log.data(), 1, m_log.size(), file) != m_log.size())
                failLocked("cannot write " + m_options.path);
            if (file)
                std::fclose(file);
            if (m_state == State::Done)
                log("[GSREPLAY] capture written: " + std::to_string(m_records) + " records, " +
                    std::to_string(m_log.size()) + " bytes, ticks " + std::to_string(m_startTick) + ".." +
                    std::to_string(tick()) + ", fp-env changes during calls " + std::to_string(m_envChangedCount) +
                    (note ? std::string(" (") + note + ")" : std::string()));
            m_log.clear();
            m_log.shrink_to_fit();
        }

        void failLocked(const std::string &reason) const
        {
            m_state = State::Failed;
            m_failure = reason;
            m_log.clear();
            m_log.shrink_to_fit();
            log("[GSREPLAY] capture failed: " + reason);
        }

        void append(const void *data, size_t size) const
        {
            const size_t pos = m_log.size();
            m_log.resize(pos + size);
            std::memcpy(m_log.data() + pos, data, size);
        }

        void begin(Rec type, size_t payload) const
        {
            const uint8_t t = static_cast<uint8_t>(type);
            const uint32_t n = static_cast<uint32_t>(payload);
            append(&t, sizeof t);
            append(&n, sizeof n);
            ++m_records;
            if (m_log.size() + payload > m_options.maxBytes)
                failLocked("capture size limit reached");
        }

        void put(Rec type, const void *payload, size_t size) const
        {
            if (!recording())
                return;
            begin(type, size);
            if (recording() && size)
                append(payload, size);
        }

        Options m_options;
        std::unique_ptr<GSCpuBackend> m_inner;
        mutable std::mutex m_lock;
        mutable State m_state = State::Armed;
        mutable std::string m_failure;
        mutable std::vector<uint8_t> m_log;
        mutable uint64_t m_records = 0;
        mutable uint64_t m_startTick = 0;
        mutable uint64_t m_mutatingCalls = 0;
        mutable uint64_t m_envChangedCount = 0;
        mutable FpEnvRec m_lastEnv{};
        mutable bool m_envRecorded = false;
        uint8_t *m_vram = nullptr;
        uint32_t m_vramSize = 0;
        bool m_hasCommand = false;
        GSTransferCommand m_command{};
        uint64_t m_uploaded = 0;
        uint64_t m_consumed = 0;
        std::vector<uint32_t> m_uploadSizes;
        bool m_haveL2H = false;
        GSTransferCommand m_l2hCommand{};
        uint64_t m_l2hConsumed = 0;
        std::vector<uint8_t> m_l2hVram;
    };
}
