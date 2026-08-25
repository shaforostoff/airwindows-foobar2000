/* ========================================
 *  vdjplugin - IVdjPluginBufferDsp8 wrapper
 *
 *  The readahead path, and the answer to "does VirtualDJ let a plug-in read the
 *  file ahead of the play head the way foobar2000 does".
 *
 *  It does, and more directly. foobar2000's DSP is handed chunks in order and a
 *  component that wants to be ahead has to arrange to be fed early; VirtualDJ
 *  turns it inside out. A buffer plug-in is *asked* for the song:
 *
 *      short * OnGetSongBuffer(int pos, int nb);
 *
 *  and it may call GetSongBuffer(pos, nb, &buf) for any position it likes to
 *  get the decoded audio. So the lookahead Declick needs is not a delay to be
 *  declared and compensated - it is two reads instead of one. Asked for
 *  [pos, pos+nb) we read [pos, pos+nb+latency) and hand back audio that is
 *  aligned with what was asked for, no delay at all. That is exactly the recipe
 *  in the READ AHEAD note on declick::Channel::prime(), which the foobar2000
 *  component cannot use and this one can.
 *
 *  Four things follow from the interface that are worth stating before the code.
 *
 *  1. It is 16 bit. GetSongBuffer hands back short*, so VirtualDJ's decoded
 *     song cache is 16 bit PCM and so is what we give back. That is the format
 *     78 rpm and vinyl transfers arrive in anyway, and see toShorts() in
 *     vdj_engine.h for why the way back is rounded rather than dithered.
 *
 *  2. The buffer we return must be ours. The pointer GetSongBuffer gives out
 *     is into the deck's own decoded audio - the same samples the waveform, the
 *     beatgrid and every other deck read - so processing in place there would
 *     corrupt the song rather than the playback. Everything below writes into
 *     m_serve.
 *
 *  3. `pos` is not monotonic. It follows the deck, so scratching, seeking, cue
 *     juggling and loops all arrive as jumps, forwards and backwards, at audio
 *     rate. Both cores are sequential - Declick's window and Dehum's detector
 *     are both history - so a jump cannot simply be processed where it lands.
 *     Hence the cache: recently produced output is kept, position-addressed, so
 *     scratching about inside it is free and, more to the point, *consistent* -
 *     a sample heard twice sounds the same both times. Only a jump out of the
 *     cache restarts the pipeline.
 *
 *  4. It is in the song's own timebase. IVdjPluginBufferDsp8::SampleRate is
 *     documented as "samplerate of the song", and pos counts song samples, so
 *     the pitch fader and the timestretcher are downstream of here. A record
 *     played at +8% is declicked at its own rate, on its own samples, which is
 *     the only place the maths is calibrated for.
 * ======================================== */

#ifndef VDJ_BUFFER_DSP_H
#define VDJ_BUFFER_DSP_H

#include "vdjDsp8.h"

#include "vdj_engine.h"

#include <stdint.h>

#include <algorithm>
#include <new>
#include <string.h>
#include <vector>

namespace vdj {

// ---------------------------------------------------------------------------

//! Sequential-processing budget, in seconds of audio, for the two cases where
//! one call has to do more than a block's worth of work.
//!
//! kWarmup is what a restart processes before the samples that were asked for.
//! Both cores start cold - Declick's robust noise estimate spans 30 ms and its
//! model has to converge, Dehum's analysis window is stale after a seek - and
//! output produced during that is worse than output produced once they have
//! settled. 0.25 s covers Declick's estimator eight times over.
//!
//! kMaxGap is how far forward the pipeline will be run to catch up with a jump
//! rather than restarting at it. Both are the same length on purpose, so that
//! whichever branch a jump takes, one call does about the same amount of extra
//! work: at the measured 180x realtime for Declick and 159x for Dehum, 0.25 s
//! of audio is about 1.5 ms. That is the figure to revisit if Model order is
//! wound up to 256, where it is roughly ten times as much.
const double kWarmupSec = 0.25;
const double kMaxGapSec = 0.25;

//! How much finished output is kept. Scratching inside this is free; leaving it
//! costs a restart. 4 s is about two beats either side of the play head at any
//! tempo a DJ is likely to be scratching at, and 4 s of 16 bit stereo is 700 kB.
const double kCacheSec = 4.0;

//! Input read, and output produced, per pass of the inner loop. Sized so that
//! it comfortably clears a device block without being large enough for the
//! scratch buffers to fall out of cache.
//!
//! kMinPushFrames is the floor on one slice, which is what guarantees the loop
//! in serve() makes progress even when the arithmetic says no more input is
//! owed. It is a block of Declick's analysis hop and change; anything at least
//! that big brings the next emission out.
enum { kSliceFrames = kMaxSliceFrames, kMinPushFrames = 512 };

// ---------------------------------------------------------------------------

//! Finished output, addressed by song position. A plain ring: capacity is fixed
//! at configure() time and never allocates again, and the oldest frames are
//! dropped as new ones arrive.
class OutputCache {
public:
    void reserve(size_t frames) {
        m_ring.assign(frames * kChannels, 0);
        m_cap = frames;
        m_start = m_end = 0;
    }

    void restart(int64_t pos) { m_start = m_end = pos; }

    int64_t start() const { return m_start; }
    int64_t end() const { return m_end; }
    size_t  capacity() const { return m_cap; }
    bool    holds(int64_t pos, int64_t frames) const {
        return pos >= m_start && pos + frames <= m_end;
    }

    void append(const short * src, size_t frames) {
        if (m_cap == 0) return;
        // A slice longer than the whole cache can only be the tail of it; skip
        // the part that would be evicted before it was ever readable.
        if (frames > m_cap) {
            src += (frames - m_cap) * kChannels;
            m_end += (int64_t)(frames - m_cap);
            frames = m_cap;
        }
        size_t off = (size_t)(m_end % (int64_t)m_cap);
        size_t first = std::min(frames, m_cap - off);
        memcpy(&m_ring[off * kChannels], src, first * kChannels * sizeof(short));
        if (first < frames) {
            memcpy(&m_ring[0], src + first * kChannels,
                   (frames - first) * kChannels * sizeof(short));
        }
        m_end += (int64_t)frames;
        if (m_end - m_start > (int64_t)m_cap) m_start = m_end - (int64_t)m_cap;
    }

    void read(int64_t pos, size_t frames, short * dst) const {
        if (m_cap == 0) return;
        size_t off = (size_t)(pos % (int64_t)m_cap);
        size_t first = std::min(frames, m_cap - off);
        memcpy(dst, &m_ring[off * kChannels], first * kChannels * sizeof(short));
        if (first < frames) {
            memcpy(dst + first * kChannels, &m_ring[0],
                   (frames - first) * kChannels * sizeof(short));
        }
    }

    size_t heapBytes() const { return m_ring.capacity() * sizeof(short); }

private:
    std::vector<short> m_ring;
    size_t  m_cap = 0;
    int64_t m_start = 0, m_end = 0;
};

// ---------------------------------------------------------------------------

//! Runs an engine over a song that is being read out of order, and keeps the
//! result addressable by position. All of the awkwardness in this file is here;
//! the plug-in class below is thin.
template<class Engine>
class BufferPipeline {
public:
    //! Allocates. Not for the audio thread - but it is called from it anyway
    //! the first time a rate is seen, because a VirtualDJ plug-in is never told
    //! the rate in advance. Everything after that is allocation-free: the cores
    //! size their own buffers from the sample rate alone (see the buffer
    //! envelope note in either Config), so a slider move rebuilds the pipeline
    //! without touching the heap.
    bool configure(Engine & engine, double rate) {
        if (!(rate >= 1000.0)) rate = 44100.0;
        try {
            engine.reset();
            m_warmup = (int)(kWarmupSec * rate);
            m_maxGap = (int)(kMaxGapSec * rate);
            m_cache.reserve((size_t)(kCacheSec * rate) + kSliceFrames);
            m_in.assign((size_t)kSliceFrames * kChannels, 0.0);
            m_out.assign((size_t)kSliceFrames * kChannels, 0.0);
            m_stage.assign((size_t)kSliceFrames * kChannels, 0);
            m_serve.assign((size_t)kSliceFrames * kChannels, 0);
        } catch (const std::bad_alloc &) {
            return false;
        }
        m_rate = rate;
        m_primed = false;
        return true;
    }

    double rate() const { return m_rate; }
    bool ready() const { return m_rate > 0.0; }

    //! A new record on the deck. Everything learned from the last one goes -
    //! see Engine::reset() against Engine::discontinuity().
    void newTrack(Engine & engine) {
        engine.reset();
        m_primed = false;
    }

    //! The engine was rebuilt under us - a structural parameter moved - so the
    //! next request restarts and warms up rather than splicing the new settings
    //! onto the end of what the old ones produced. What the engine had learned
    //! is its own business: update() has already kept or dropped it.
    void invalidate() { m_primed = false; }

    //! The whole of OnGetSongBuffer. Returns `nb` frames of processed audio at
    //! `pos`, in a buffer this object owns, valid until the next call.
    const short * serve(Engine & engine, SongSource & src, int pos, int nb) {
        if (nb <= 0) return m_serve.data();
        if ((size_t)nb * kChannels > m_serve.size()) {
            // Only reachable if VirtualDJ asks for more in one go than
            // kSliceFrames, which nothing observed does. Grow once and keep it:
            // a request cannot be served out of a buffer too small for it, and
            // it cannot be served out of a cache that would evict the front of
            // it before the back had been produced either.
            try {
                m_serve.assign((size_t)nb * kChannels, 0);
                const size_t need = (size_t)nb + (size_t)m_warmup + kSliceFrames;
                if (m_cache.capacity() < need) m_cache.reserve(need);
            } catch (const std::bad_alloc &) {
                return NULL;
            }
            m_primed = false;   // reserve() empties the ring
        }

        const int64_t want = (int64_t)pos;
        const int64_t wantEnd = want + nb;

        if (!m_primed || want < m_cache.start() || want > m_cache.end() + m_maxGap) {
            restart(engine, want);
        }

        // Everything from the cache's end up to what was asked for has to go
        // through the engine in order, whether or not anybody will hear it.
        //
        // Bounded rather than "until it is done" on purpose: this runs on the
        // audio thread, and a core that had somehow stopped producing would
        // otherwise spin it forever. The bound is what the arithmetic says the
        // loop needs plus slack, so reaching it means something is wrong and a
        // gap in the audio is the right way to find out.
        int guard = (int)((wantEnd - m_cache.end() + engine.lookahead())
                          / (int64_t)kSliceFrames) + 4;
        while (m_cache.end() < wantEnd && guard-- > 0) {
            advance(engine, src, wantEnd);
        }

        if (!m_cache.holds(want, nb)) {
            // Could not be produced - out of memory, or a request so far past
            // the end of the song that even the silence substitution ran out.
            // Silence beats stale audio from somewhere else in the record.
            memset(m_serve.data(), 0, (size_t)nb * kChannels * sizeof(short));
            return m_serve.data();
        }

        m_cache.read(want, (size_t)nb, m_serve.data());

        // Off the critical path: give the engine its look at the song. Dehum
        // uses this to read the opening of the record and find the hum before
        // the detector could have; Declick's is empty.
        engine.scout(src, (size_t)nb);

        return m_serve.data();
    }

    size_t heapBytes() const {
        return m_cache.heapBytes()
             + (m_in.capacity() + m_out.capacity()) * sizeof(double)
             + (m_stage.capacity() + m_serve.capacity()) * sizeof(short);
    }

private:
    //! Start the pipeline again, `m_warmup` frames before the audio that was
    //! asked for so that the cores are settled by the time it matters. The
    //! warm-up output is not thrown away - it goes into the cache like anything
    //! else, which is what makes scratching back over a seek point free.
    void restart(Engine & engine, int64_t pos) {
        int64_t from = pos - m_warmup;
        if (from < 0) from = 0;
        engine.discontinuity();
        m_inPos = from;
        m_cache.restart(from);
        m_primed = true;
    }

    //! One slice: read input, push it, take whatever came out, cache it.
    void advance(Engine & engine, SongSource & src, int64_t wantEnd) {
        // How much input is still owed for the output that is still owed. The
        // engine holds lookahead() frames, so the input runs that far ahead of
        // the output - this is the whole of the readahead, and there is no
        // arithmetic about latency anywhere else.
        //
        // It is an upper bound rather than an exact figure: Declick emits in
        // blocks of 512, so the input it actually needs to be ahead by lands
        // somewhere below Config::latency. Over-reading is free - the surplus
        // output goes in the cache and is that much less to do next time -
        // whereas under-reading would stall the loop above, so the floor below
        // guarantees the slice makes progress either way.
        const int64_t needIn = wantEnd + engine.lookahead() - m_inPos;
        size_t frames = (size_t)std::min<int64_t>(
            std::max<int64_t>(needIn, (int64_t)kMinPushFrames), (int64_t)kSliceFrames);

        const short * raw = NULL;
        if (src.read((int)m_inPos, (int)frames, &raw) && raw != NULL) {
            fromShorts(raw, m_in.data(), frames);
        } else {
            // Past the end of the song, or not decoded yet. Zeros are what
            // Channel::drain() feeds for the same purpose: they push the tail
            // of the real audio out of the pipeline instead of stranding it.
            std::fill(m_in.begin(), m_in.begin() + frames * kChannels, 0.0);
        }

        engine.push(m_in.data(), frames);
        m_inPos += (int64_t)frames;

        // Nothing to take on the first slices after a restart, while the engine
        // is still filling its lookahead. m_inPos is monotonic, so the caller's
        // loop gets there.
        for (;;) {
            size_t k = engine.available();
            if (k == 0) break;
            if (k > (size_t)kSliceFrames) k = (size_t)kSliceFrames;
            engine.pull(m_out.data(), k);
            toShorts(m_out.data(), m_stage.data(), k);
            m_cache.append(m_stage.data(), k);
        }
    }

    OutputCache m_cache;
    std::vector<double> m_in, m_out;
    std::vector<short>  m_stage;   //!< engine output, quantised, before caching
    std::vector<short>  m_serve;   //!< what OnGetSongBuffer hands back

    double  m_rate = 0.0;
    int     m_warmup = 0;
    int     m_maxGap = 0;
    int64_t m_inPos = 0;           //!< next song position to be fed in
    bool    m_primed = false;
};

// ---------------------------------------------------------------------------

//! The plug-in itself.
template<class Engine>
class BufferDsp : public IVdjPluginBufferDsp8, public SongSource {
public:
    HRESULT VDJ_API OnLoad() override {
        m_engine.declareParameters(*this);
        return S_OK;
    }

    HRESULT VDJ_API OnGetPluginInfo(TVdjPluginInfo8 * info) override {
        info->PluginName  = Engine::bufferName();
        info->Author      = "Airwindows tree (MIT)";
        info->Description = Engine::bufferDescription();
        info->Version     = "1.0";
        info->Flags       = 0x00;
        info->Bitmap      = NULL;
        return S_OK;
    }

    ULONG VDJ_API Release() override { delete this; return 0; }

    HRESULT VDJ_API OnStart() override {
        // Switched on. Whatever is in the pipeline is from the last time it was
        // on, which may be a different record.
        m_pipeline.newTrack(m_engine);
        m_track[0] = 0;
        return S_OK;
    }

    HRESULT VDJ_API OnStop() override { return S_OK; }

    HRESULT VDJ_API OnGetParameterString(int id, char * out, int size) override {
        return m_engine.parameterString(id, out, size) ? S_OK : E_NOTIMPL;
    }

    short * VDJ_API OnGetSongBuffer(int pos, int nb) override {
        // Non-finite input is silenced inside both cores and every buffer here
        // is fixed size, so the only thing that can throw is the first
        // configure() at a new sample rate. Nothing may cross back into
        // VirtualDJ.
        try {
            const double rate = (SampleRate > 0) ? (double)SampleRate : 44100.0;
            if (!m_pipeline.ready() || rate != m_pipeline.rate()) {
                if (!m_engine.setRate(rate)) return NULL;
                if (!m_pipeline.configure(m_engine, rate)) return NULL;
            }
            if (m_engine.update(rate)) m_pipeline.invalidate();
            checkTrack();

            const short * out = m_pipeline.serve(m_engine, *this, pos, nb);
            return const_cast<short *>(out);
        } catch (...) {
            return NULL;
        }
    }

    // -- SongSource ----------------------------------------------------------

    bool read(int pos, int nb, const short ** out) override {
        if (pos < 0 || nb <= 0) return false;
        short * buf = NULL;
        if (GetSongBuffer(pos, nb, &buf) != S_OK || buf == NULL) return false;
        *out = buf;
        return true;
    }

private:
    //! Notices a new record on the deck.
    //!
    //! There is no callback for it - OnStart and OnStop are about the effect
    //! being switched on, not about the deck being loaded - so this polls the
    //! deck's file path, which is what the SDK forum recommends for exactly
    //! this: call GetStringInfo from OnProcessSamples and compare against the
    //! last value. Once per buffer, not once per sample.
    //!
    //! It matters most to Dehum, whose detector has learned this record's hum
    //! and must not carry it into the next one, and to its scout, which is
    //! per-record by construction. If the query is unsupported the path comes
    //! back empty and this degrades to doing nothing - the pipeline still
    //! restarts on the position jump that a new track produces, so the audio is
    //! correct either way; only the learned state would linger.
    void checkTrack() {
        char path[512];
        path[0] = 0;
        if (GetStringInfo("get_filepath", path, (int)sizeof(path)) != S_OK) return;
        path[sizeof(path) - 1] = 0;
        if (path[0] == 0) return;
        if (strncmp(path, m_track, sizeof(m_track)) == 0) return;

#if defined(_MSC_VER)
        strncpy_s(m_track, path, sizeof(m_track) - 1);
#else
        strncpy(m_track, path, sizeof(m_track) - 1);
        m_track[sizeof(m_track) - 1] = 0;
#endif
        m_pipeline.newTrack(m_engine);
    }

    Engine m_engine;
    BufferPipeline<Engine> m_pipeline;
    char m_track[512] = { 0 };
};

} // namespace vdj

#endif // VDJ_BUFFER_DSP_H
