/* ========================================
 *  Dehum - the VirtualDJ engine
 *
 *  Sliders in, dehum::Channel out. Everything VirtualDJ-shaped is here and
 *  nothing algorithmic is: the maths lives in dehum_core.{h,cpp}, which is a
 *  verbatim copy of the file foo_dsp_dehum builds - see the note at the top of
 *  that header, and ../foobar2000_dsp/scripts/sync_cores.ps1 for what keeps the
 *  copies from drifting.
 *
 *  Two classes, because the two VirtualDJ interfaces can do different amounts
 *  for this core:
 *
 *    DehumEngine        the notch bank and the detector. Zero latency, works in
 *                       place, so it fits the live interface exactly.
 *    DehumBufferEngine  the same plus a scout. Random access to the song means
 *                       the opening of the record can be analysed faster than
 *                       it plays, which takes acquisition from tens of seconds
 *                       to a few - see dehum_vdj_scout.h.
 *
 *  The scout is on the derived class rather than switched on with a flag so
 *  that the live plug-in does not carry the extra dehum::Channel it would need
 *  and never use. Every call the wrappers make is resolved statically on the
 *  Engine template parameter, so shadowing is enough; there are no virtuals.
 *
 *  The slider mappings are the ones the VST2 port uses, constant for constant -
 *  a slider position has to mean the same thing in every port or the
 *  measurements in the foobar2000 README stop describing any of them.
 * ======================================== */

#ifndef VDJ_DEHUM_ENGINE_H
#define VDJ_DEHUM_ENGINE_H

#include "vdjPlugin8.h"

#include "dehum_core.h"
#include "dehum_vdj_scout.h"
#include "vdj_engine.h"

#include <stdio.h>
#include <string.h>

#include <new>

namespace vdj {

class DehumEngine {
public:
    // -- identity ------------------------------------------------------------

    static const char * liveName() { return "Dehum"; }
    static const char * liveDescription() {
        return "Finds continuous narrowband tones - mains hum, its harmonics, and "
               "the off-frequency drones on speed-corrected disc transfers - "
               "without being told the frequency, and cancels them with a "
               "tracking notch. Zero latency, plus an optional rumble high-pass. "
               "Runs on a deck, a microphone or the master.";
    }

    static const char * bufferName() { return "Dehum Buffer"; }
    static const char * bufferDescription() {
        return "Dehum, reading the opening of the loaded song faster than it "
               "plays so the hum is found in a few seconds instead of the tens "
               "the detector needs unaided. Buffer effect: works on a loaded "
               "song, not on a live input.";
    }

    // -- parameters ----------------------------------------------------------

    enum {
        kSensitivity = 1, kBandwidth, kSearchTo, kHarmonics, kFrequency,
        kRumble, kDryWet
    };

    void declareParameters(IVdjPlugin8 & host) {
        host.DeclareParameterSlider(&m_a, kSensitivity, "Sensitivity", "Sensitv", kDefA);
        host.DeclareParameterSlider(&m_b, kBandwidth,   "Bandwidth",   "Bandwid", kDefB);
        host.DeclareParameterSlider(&m_c, kSearchTo,    "Search to",   "SrchTo",  kDefC);
        host.DeclareParameterSlider(&m_d, kHarmonics,   "Harmonics",   "Harmncs", kDefD);
        host.DeclareParameterSlider(&m_e, kFrequency,   "Frequency",   "Freq",    kDefE);
        host.DeclareParameterSlider(&m_f, kRumble,      "Rumble",      "Rumble",  kDefF);
        host.DeclareParameterSlider(&m_g, kDryWet,      "Dry/Wet",     "Dry/Wet", kDefG);
    }

    bool parameterString(int id, char * out, int size) const {
        const dehum::Params p = params();
        switch (id) {
            case kSensitivity: snprintf(out, size, "%.0f %%", pinParameter(m_a) * 100.0); return true;
            case kBandwidth:   snprintf(out, size, "%.2f Hz", (double)p.bandwidth);       return true;
            case kSearchTo:    snprintf(out, size, "%.0f Hz", (double)p.searchTo);        return true;
            case kHarmonics:   snprintf(out, size, "%d", p.harmonics);                    return true;
            case kFrequency:
                if (p.frequency > 0.0f) snprintf(out, size, "%.1f Hz", (double)p.frequency);
                else                    snprintf(out, size, "auto");
                return true;
            case kRumble:
                if (p.rumbleHz > 0.0f) snprintf(out, size, "%.0f Hz", (double)p.rumbleHz);
                else                   snprintf(out, size, "off");
                return true;
            case kDryWet:      snprintf(out, size, "%.0f %%", pinParameter(m_g) * 100.0); return true;
            default: return false;
        }
    }

    //! Sliders to core units, identical to Dehum::paramsFromControls() in the
    //! VST2 port. Frequency and Rumble have an off position at the bottom rather
    //! than a range that reaches zero, hence withOffPosition().
    dehum::Params params() const {
        dehum::Params p = dehum::Params::defaults();
        p.sensitivity = pinParameter(m_a);
        p.bandwidth   = 0.1f + pinParameter(m_b) * 4.9f;             // 0.1 .. 5 Hz
        p.searchTo    = 40.0f + pinParameter(m_c) * 460.0f;          // 40 .. 500 Hz
        p.harmonics   = 1 + (int)(pinParameter(m_d) * 7.999f);       // 1 .. 8
        p.frequency   = withOffPosition(pinParameter(m_e), 10.0f, 500.0f);  // 0 = auto
        p.rumbleHz    = withOffPosition(pinParameter(m_f), 10.0f, 200.0f);  // 0 = off
        p.dryWet      = pinParameter(m_g);
        p.sanitize();
        return p;
    }

    // -- pipeline ------------------------------------------------------------

    bool setRate(double rate) {
        try {
            rebuild(rate, params());
        } catch (const std::bad_alloc &) {
            m_configured = false;
            return false;
        }
        return true;
    }

    //! True if the pipeline was rebuilt. In this core only the sample rate sizes
    //! anything, so every slider move takes the retune path and dragging one
    //! produces no gap at all - a better deal than Declick gets.
    bool update(double rate) {
        const dehum::Params p = params();
        if (m_configured && rate == m_rate && p == m_active) return false;

        dehum::Config next;
        next.compute(p, rate);

        if (m_configured && rate == m_rate && next.structurallyEquals(m_cfg)
            && m_chan[0].retune(next) && m_chan[1].retune(next)) {
            m_cfg = next;
            m_active = p;
            return false;
        }

        try {
            rebuild(rate, p);
        } catch (const std::bad_alloc &) {
            m_configured = false;
        }
        return true;
    }

    //! A new record. Everything the detector learned belongs to the last one:
    //! the lines, the scores, and the analysis window they were found in.
    void reset() {
        m_fifo.clear();
        if (!m_configured) return;
        m_chan[0].reset();
        m_chan[1].reset();
    }

    //! A seek. Unlike Declick this is not the same as reset(): the analysis
    //! window is stale and goes, but the hum on the far side of a seek is the
    //! same hum, and re-acquiring it every time the DJ moves the play position -
    //! or scratches out of the cache - would be worse than doing nothing.
    void discontinuity() {
        m_fifo.clear();
        if (!m_configured) return;
        m_chan[0].flush();
        m_chan[1].flush();
    }

    //! None. The detector reads the signal but does not sit in the path, so what
    //! goes in comes out aligned - which is why this core is at home on the live
    //! interface and Declick is not.
    int lookahead() const { return 0; }

    void push(const double * in, size_t frames) {
        if (!m_configured) {
            m_fifo.push(in, frames);
            return;
        }
        if (frames > m_work.size() / kChannels) frames = m_work.size() / kChannels;
        memcpy(m_work.data(), in, frames * kChannels * sizeof(double));
        {
            dehum::scoped_flush_denormals ftz;
            // In place, interleaved, one call per side. Splitting a buffer
            // across several of these is invisible to the core - process() is a
            // per-sample loop with its own hop counter - which is what
            // dehum_verify's block size invariance check establishes.
            m_chan[0].process(m_work.data() + 0, frames, kChannels);
            m_chan[1].process(m_work.data() + 1, frames, kChannels);
        }
        m_fifo.push(m_work.data(), frames);
    }

    size_t available() const { return m_fifo.available(); }
    void   pull(double * out, size_t frames) { m_fifo.pull(out, frames); }

    //! Nothing is held, so there is nothing to run out. The realtime wrapper
    //! calls this as a pre-roll and correctly gets a no-op.
    void drain() {}

    void scout(SongSource &, size_t) {}

protected:
    void rebuild(double rate, const dehum::Params & p) {
        dehum::Config next;
        next.compute(p, rate);
        m_chan[0].configure(next);
        m_chan[1].configure(next);
        m_work.assign((size_t)kMaxSliceFrames * kChannels, 0.0);
        m_fifo.reserve((size_t)kMaxSliceFrames * 2);
        m_cfg = next;
        m_active = p;
        m_rate = rate;
        m_configured = true;
    }

    void adoptLines(const dehum::LineReport * lines, int count) {
        if (!m_configured || count <= 0) return;
        m_chan[0].adopt(lines, count);
        m_chan[1].adopt(lines, count);
    }

    bool configured() const { return m_configured; }
    double rate() const { return m_rate; }

private:
    // Slider positions for the calibrated defaults dehum::Params ships. Kept as
    // named constants because two of them are not round numbers - Bandwidth and
    // Rumble are linear in Hz, so their default Hz values land mid-slider - and
    // a literal would be a silent way for the ports to disagree about what
    // "default" means.
    static constexpr float kDefA = 0.5f;                   // Sensitivity -> 16 dB
    static constexpr float kDefB = 0.9f / 4.9f;            // Bandwidth -> 1.00 Hz
    static constexpr float kDefC = 60.0f / 460.0f;         // Search to -> 100 Hz
    static constexpr float kDefD = 0.0f;                   // Harmonics -> 1
    static constexpr float kDefE = 0.0f;                   // Frequency -> auto
    static constexpr float kDefF = 0.02f + (57.0f / 190.0f) * 0.98f;  // Rumble -> 67 Hz
    static constexpr float kDefG = 1.0f;                   // Dry/Wet

    float m_a = kDefA, m_b = kDefB, m_c = kDefC, m_d = kDefD;
    float m_e = kDefE, m_f = kDefF, m_g = kDefG;

    dehum::Channel m_chan[kChannels];
    dehum::Config  m_cfg;
    dehum::Params  m_active = dehum::Params::defaults();
    std::vector<double> m_work;   //!< the core works in place; this is the place
    Fifo   m_fifo;
    double m_rate = 0.0;
    bool   m_configured = false;
};

// ---------------------------------------------------------------------------

//! Dehum with a scout. See dehum_vdj_scout.h for what it does and why it is
//! worth doing; this class is only the wiring.
class DehumBufferEngine : public DehumEngine {
public:
    bool setRate(double rate) {
        if (!DehumEngine::setRate(rate)) return false;
        // Failing to get a scout is not failing to get a dehummer: without it
        // the detector still finds the line, just later.
        m_scout.begin(params(), rate);
        return true;
    }

    //! A new record, which is the only thing a scout is per. Rewound here rather
    //! than in discontinuity(): a seek is the same record, and re-reading its
    //! opening every time the DJ moves the play position would be work for
    //! nothing. BufferPipeline::configure() calls this too, so the scout is
    //! always started before the first buffer is served.
    void reset() {
        DehumEngine::reset();
        m_scout.newTrack(params().frequency);
    }

    void scout(SongSource & src, size_t served) {
        if (!configured()) return;

        dehum::LineReport lines[dehum::kMaxLines];
        const int count = m_scout.take(lines, (int)dehum::kMaxLines);
        if (count > 0) adoptLines(lines, count);

        m_scout.feed(src, served);
    }

private:
    DehumScout m_scout;
};

} // namespace vdj

#endif // VDJ_DEHUM_ENGINE_H
