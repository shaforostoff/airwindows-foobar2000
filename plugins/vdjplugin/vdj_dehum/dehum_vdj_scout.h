/* ========================================
 *  Dehum - VirtualDJ track scout
 *
 *  Dehum's detector will not commit to a line until it has several seconds of
 *  steady evidence, and on the transfers this exists for that is not a few
 *  seconds but the better part of a minute: see the acquisition note at the top
 *  of dehum_core.h. A line the prominence route can see arrives about 9 s in,
 *  one that only the coherence route can reach at 43 s. All of it is time the
 *  record plays with the hum still in it.
 *
 *  A buffer plug-in does not have to spend it, because GetSongBuffer will hand
 *  over any part of the decoded song on request. So a second, throwaway
 *  dehum::Channel is run over the *opening* of the record while the deck plays,
 *  faster than the deck plays it, and what it finds goes to
 *  Channel::adopt() - which starts those lines confirmed and leaves the live
 *  detector running, so it still tracks them, still drops them if the evidence
 *  is not really there, and can still find others.
 *
 *  Deliberately per record: each one carries its own hum, so the scout is
 *  rewound for every track and publishes exactly once.
 *
 *  Where this differs from the foobar2000 scout, which does the same job:
 *
 *    - No thread, and no copy of the audio. foo_dsp_dehum has to open the file
 *      and decode it a second time, which is worth a worker thread and a
 *      64-second staging buffer. Here the samples are already decoded and one
 *      call away, so the work is done on the caller's thread in slices, and the
 *      only buffer is one slice long.
 *
 *    - Budgeted rather than as-fast-as-possible. The caller says how many
 *      frames it has just served and the scout does kSpeedup times that many,
 *      so scouting runs at a fixed multiple of playback whatever block size
 *      VirtualDJ is using: 60 s of record covered inside 8 s of playing it, for
 *      a fixed fraction of a core rather than a spike. Dehum measures 159x
 *      realtime, so kSpeedup of 8 is about 5% of one core while it runs, and
 *      nothing once it has finished.
 * ======================================== */

#ifndef VDJ_DEHUM_SCOUT_H
#define VDJ_DEHUM_SCOUT_H

#include "dehum_core.h"
#include "vdj_engine.h"

#include <algorithm>
#include <new>
#include <vector>

namespace vdj {

//! Seconds of the opening to read. Sixty, the same figure and for the same
//! reason as the foobar2000 scout: the coherence route accumulates its ratio
//! over dehum::kCohWindowSec, so the lines that most need scouting are exactly
//! the ones that need most of this window.
const double kScoutSeconds = 60.0;

//! How much less than that is still worth acting on - and also how soon the
//! scout is allowed to stop early, once it has actually confirmed a line.
//! Below this the prominence route has barely had time to confirm anything and
//! the coherence route none at all, so there would be nothing to hand over.
const double kScoutMinSeconds = 10.0;

//! Scouting speed as a multiple of playback.
const int kScoutSpeedup = 8;

//! Frames per read, and the ceiling on one call's worth of work regardless of
//! how much audio was served - so a host that hands over a very long buffer
//! gets a slower scout rather than a stall.
enum { kScoutSlice = 4096, kScoutMaxPerCall = 16384 };

class DehumScout {
public:
    //! Allocates: one dehum::Channel plus a slice. Called only when the sample
    //! rate changes, like everything else here that touches the heap.
    bool begin(const dehum::Params & params, double rate) {
        if (!(rate >= 1000.0)) rate = 44100.0;
        dehum::Params p = params;
        // Belt and braces. A pinned frequency turns the search off, so there
        // would be nothing to find, and adopt() refuses lines while one is set
        // anyway - newTrack() below declines to run in that case.
        p.frequency = 0.0f;
        p.sanitize();

        dehum::Config cfg;
        cfg.compute(p, rate);
        try {
            m_ch.configure(cfg);
            m_slice.assign((size_t)kScoutSlice, 0.0);
        } catch (const std::bad_alloc &) {
            m_usable = false;
            return false;
        }
        m_cfg = cfg;
        m_rate = rate;
        m_usable = true;
        m_running = false;
        m_ready = false;
        return true;
    }

    //! A new record. `pinned` is the live Params::frequency: with a frequency
    //! pinned by hand there is nothing to search for, so the scout stays idle
    //! and costs nothing.
    void newTrack(float pinned) {
        m_running = false;
        m_ready = false;
        m_count = 0;
        m_pos = 0;
        m_failures = 0;
        if (!m_usable || pinned > 0.0f) return;
        m_ch.reset();
        m_wanted = (int64_t)(kScoutSeconds * m_rate);
        m_least  = (int64_t)(kScoutMinSeconds * m_rate);
        m_running = true;
    }

    //! One call's worth of scouting. `served` is how many frames the caller
    //! just handed to VirtualDJ, which is what the budget is scaled from.
    void feed(SongSource & src, size_t served) {
        if (!m_running) return;

        int64_t budget = (int64_t)served * kScoutSpeedup;
        if (budget > (int64_t)kScoutMaxPerCall) budget = (int64_t)kScoutMaxPerCall;

        dehum::scoped_flush_denormals ftz;

        while (budget > 0 && m_pos < m_wanted) {
            const int64_t room = std::min<int64_t>(budget, (int64_t)kScoutSlice);
            const size_t n = (size_t)std::min<int64_t>(room, m_wanted - m_pos);

            const short * raw = NULL;
            if (!src.read((int)m_pos, (int)n, &raw) || raw == NULL) {
                // End of a short record, or a part of it VirtualDJ has not
                // decoded yet. Either way stop trying rather than hammering the
                // read every call for the rest of the track.
                if (++m_failures >= kMaxFailures) finish();
                return;
            }
            m_failures = 0;

            // Hum is common mode, so one summed channel finds it for half the
            // work of running both - the same reason the foobar2000 scout
            // downmixes. The live channels still cancel per side.
            for (size_t f = 0; f < n; ++f) {
                m_slice[f] = 0.5 * ((double)raw[f * kChannels + 0]
                                  + (double)raw[f * kChannels + 1]) * (1.0 / 32768.0);
            }
            m_ch.process(m_slice.data(), n, 1);

            m_pos += (int64_t)n;
            budget -= (int64_t)n;

            // Stop as soon as there is something to hand over and enough of the
            // record has been read to trust it. The 60 s window exists for the
            // coherence route, which needs most of it; a line the prominence
            // route can see turns up in the first ten and there is nothing to be
            // gained by reading the other fifty.
            //
            // Which matters for more than time. Every one of these reads goes
            // through GetSongBuffer, and GetSongBuffer is not a file - it is
            // whatever is upstream in the chain, which may be another buffer
            // plug-in doing real work at a quite different position. Reading
            // less is the only part of that cost this side can control. See
            // kRestartCooldownSec in ../common/vdj_buffer_dsp.h for the other
            // half.
            if (m_pos >= m_least && m_ch.lineCount() > 0) {
                finish();
                return;
            }
        }

        if (m_pos >= m_wanted) finish();
    }

    //! Hands the result over once. Zero every call until the scout finishes and
    //! zero again afterwards, so the caller can poll it per buffer without
    //! having to remember that it already did - re-adopting every call would
    //! keep resetting the score of a line the live detector had decided to let
    //! go of.
    int take(dehum::LineReport * out, int max) {
        if (!m_ready) return 0;
        m_ready = false;
        int count = m_count;
        if (count > max) count = max;
        for (int i = 0; i < count; ++i) out[i] = m_lines[i];
        return count;
    }

    bool running() const { return m_running; }
    int64_t framesRead() const { return m_pos; }
    size_t heapBytes() const { return m_ch.heapBytes() + m_slice.capacity() * sizeof(double); }

private:
    enum { kMaxFailures = 8 };

    void finish() {
        m_running = false;
        m_count = 0;
        if (m_pos >= m_least) {
            m_ch.report(m_lines, (int)dehum::kMaxLines, &m_count);
        }
        m_ready = (m_count > 0);
    }

    dehum::Channel m_ch;
    dehum::Config  m_cfg;
    std::vector<double> m_slice;

    dehum::LineReport m_lines[dehum::kMaxLines];
    int     m_count = 0;
    double  m_rate = 0.0;
    int64_t m_pos = 0;
    int64_t m_wanted = 0;
    int64_t m_least = 0;
    int     m_failures = 0;
    bool    m_usable = false;
    bool    m_running = false;
    bool    m_ready = false;
};

} // namespace vdj

#endif // VDJ_DEHUM_SCOUT_H
