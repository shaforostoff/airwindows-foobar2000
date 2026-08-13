/* ========================================
 *  foo_dsp_decrackle - foobar2000 DSP wrapper around the Airwindows
 *  DeCrackle algorithm.
 * ======================================== */

#include "stdafx.h"

#include "decrackle_core.h"
#include "decrackle_preset.h"
#include "resource.h"

#include <memory>
#include <mutex>
#include <vector>

#if defined(_M_IX86) || defined(_M_X64)
#include <xmmintrin.h>
#endif

using airwindows::DeCrackleCoeffs;
using airwindows::DeCracklePair;
using airwindows::DeCrackleParams;

namespace {

// ---------------------------------------------------------------------------
// Flush-to-zero for the duration of one chunk.
//
// Only the FTZ bit is touched: DAZ lives in a bit that early SSE2 parts
// (Willamette / Northwood P4) treat as reserved, and writing it there raises
// #GP. FTZ alone is enough here - the only way this algorithm can produce a
// denormal is by cubing a near-cancelled difference.
// ---------------------------------------------------------------------------
class scoped_flush_denormals {
public:
    scoped_flush_denormals(const scoped_flush_denormals &) = delete;
    void operator=(const scoped_flush_denormals &) = delete;

#if defined(_M_IX86) || defined(_M_X64)
    scoped_flush_denormals() : m_saved(_mm_getcsr()) {
        if ((m_saved & 0x8000u) == 0u) _mm_setcsr(m_saved | 0x8000u);
    }
    ~scoped_flush_denormals() {
        if ((m_saved & 0x8000u) == 0u) _mm_setcsr(m_saved);
    }
private:
    unsigned m_saved;
#else
    // ARM64 flushes denormals by default under Windows.
    scoped_flush_denormals() {}
#endif
};

// ---------------------------------------------------------------------------
// Channel pairing
//
// DeCrackle's click detector correlates left against right, so channels are
// fed to it in their natural stereo pairs rather than in raw interleave order.
// Anything without a partner (centre, LFE, back centre, ...) is run through the
// degenerate single-channel path.
// ---------------------------------------------------------------------------
struct channel_slot {
    unsigned left;          // index into the interleaved frame
    unsigned right;         // == left when mono
    bool     mono;
};

const unsigned k_stereo_partners[][2] = {
    { audio_chunk::channel_front_left,        audio_chunk::channel_front_right        },
    { audio_chunk::channel_front_center_left, audio_chunk::channel_front_center_right },
    { audio_chunk::channel_side_left,         audio_chunk::channel_side_right         },
    { audio_chunk::channel_back_left,         audio_chunk::channel_back_right         },
    { audio_chunk::channel_top_front_left,    audio_chunk::channel_top_front_right    },
    { audio_chunk::channel_top_back_left,     audio_chunk::channel_top_back_right     },
};

//! Index of `flag` within the interleaved frame, or ~0u if not present.
unsigned channel_index(unsigned config, unsigned flag) {
    if ((config & flag) == 0u) return ~0u;
    unsigned idx = 0;
    for (unsigned bit = 1u; bit < flag; bit <<= 1) {
        if (config & bit) ++idx;
    }
    return idx;
}

void build_slots(unsigned channels, unsigned config, std::vector<channel_slot> & out) {
    out.clear();
    if (channels == 0) return;
    out.reserve((channels + 1) / 2);

    // pfc::countBits on the config must agree with the reported channel count,
    // otherwise the map is not trustworthy and we fall back to plain pairing.
    unsigned mapped = 0;
    for (unsigned bit = 0; bit < audio_chunk::defined_channel_count; ++bit) {
        if (config & (1u << bit)) ++mapped;
    }

    if (mapped == channels) {
        std::vector<bool> taken(channels, false);
        for (size_t i = 0; i < sizeof(k_stereo_partners) / sizeof(k_stereo_partners[0]); ++i) {
            const unsigned l = channel_index(config, k_stereo_partners[i][0]);
            const unsigned r = channel_index(config, k_stereo_partners[i][1]);
            if (l == ~0u || r == ~0u) continue;
            channel_slot s;
            s.left = l; s.right = r; s.mono = false;
            out.push_back(s);
            taken[l] = true; taken[r] = true;
        }
        for (unsigned c = 0; c < channels; ++c) {
            if (taken[c]) continue;
            channel_slot s;
            s.left = c; s.right = c; s.mono = true;
            out.push_back(s);
        }
        return;
    }

    // Unknown map: pair them off in interleave order.
    unsigned c = 0;
    for (; c + 1 < channels; c += 2) {
        channel_slot s;
        s.left = c; s.right = c + 1; s.mono = false;
        out.push_back(s);
    }
    if (c < channels) {
        channel_slot s;
        s.left = c; s.right = c; s.mono = true;
        out.push_back(s);
    }
}

// ---------------------------------------------------------------------------

class dsp_decrackle : public dsp_impl_base_t<dsp_v3> {
public:
    dsp_decrackle(const dsp_preset & in) {
        m_pendingParams = decrackle_preset::parse(in);
        m_activeParams  = m_pendingParams;
        m_coeffs.compute(m_activeParams, 44100.0);
    }

    static GUID g_get_guid() { return decrackle_preset::guid(); }

    static void g_get_name(pfc::string_base & out) { out = "DeCrackle (Airwindows)"; }

    static bool g_get_default_preset(dsp_preset & out) {
        decrackle_preset::make(DeCrackleParams::defaults(), out);
        return true;
    }

    static bool g_have_config_popup() { return true; }

    static void g_show_config_popup(const dsp_preset & data, HWND parent,
                                    dsp_preset_edit_callback & callback) {
        decrackle_config_popup(data, parent, callback);
    }

    // -- dsp_v3 --------------------------------------------------------------

    bool apply_preset(const dsp_preset & preset) override {
        if (preset.get_owner() != g_get_guid()) return false;
        const DeCrackleParams p = decrackle_preset::parse(preset);
        std::lock_guard<std::mutex> lock(m_paramLock);
        m_pendingParams = p;
        return true;    // no need to tear the DSP down; picked up next chunk
    }

    // -- dsp_impl_base -------------------------------------------------------

    bool on_chunk(audio_chunk * chunk, abort_callback &) override {
        const unsigned channels = chunk->get_channels();
        const unsigned rate     = chunk->get_sample_rate();
        const t_size   frames   = chunk->get_sample_count();
        if (channels == 0 || rate == 0 || frames == 0) return true;

        DeCrackleParams params;
        {
            std::lock_guard<std::mutex> lock(m_paramLock);
            params = m_pendingParams;
        }

        const unsigned config = chunk->get_channel_config();
        if (channels != m_channels || rate != m_rate || config != m_config) {
            if (!configure(channels, rate, config, params)) return true;
        } else if (params != m_activeParams) {
            updateCoefficients(params);
        }

        audio_sample * const data = chunk->get_data();
        if (data == NULL) return true;

        {
            scoped_flush_denormals ftz;
            for (size_t i = 0; i < m_slots.size(); ++i) {
                const channel_slot & s = m_slots[i];
                DeCracklePair * state = m_state[i].get();
                if (s.mono) {
                    state->processMono(m_coeffs, data + s.left, channels, frames);
                } else {
                    state->processStereo(m_coeffs, data + s.left, data + s.right,
                                         channels, frames);
                }
            }
        }

        // Cheap insurance: if anything managed to go non-finite, start over
        // rather than letting it sit in the recursive state forever.
        for (size_t i = 0; i < m_state.size(); ++i) {
            if (!m_state[i]->stateIsFinite()) m_state[i]->reset();
        }

        return true;
    }

    void on_endofplayback(abort_callback &) override {}
    void on_endoftrack(abort_callback &) override {}

    void flush() override {
        for (size_t i = 0; i < m_state.size(); ++i) m_state[i]->reset();
    }

    double get_latency() override {
        if (m_rate == 0) return 0.0;
        return (double)m_coeffs.latencySamples / (double)m_rate;
    }

    bool need_track_change_mark() override { return false; }

private:
    //! Returns false if the new format could not be set up (out of memory), in
    //! which case the chunk is passed through untouched.
    bool configure(unsigned channels, unsigned rate, unsigned config,
                   const DeCrackleParams & params) {
        std::vector<channel_slot> slots;
        build_slots(channels, config, slots);

        std::vector<std::unique_ptr<DeCracklePair> > state;
        try {
            state.reserve(slots.size());
            for (size_t i = 0; i < slots.size(); ++i) {
                state.push_back(std::unique_ptr<DeCracklePair>(new DeCracklePair()));
            }
        } catch (const std::bad_alloc &) {
            m_channels = 0; m_rate = 0; m_config = 0;
            m_slots.clear();
            m_state.clear();
            return false;
        }

        m_slots.swap(slots);
        m_state.swap(state);
        m_channels = channels;
        m_rate     = rate;
        m_config   = config;
        m_activeParams = params;
        m_coeffs.compute(params, (double)rate);
        return true;
    }

    void updateCoefficients(const DeCrackleParams & params) {
        m_activeParams = params;
        m_coeffs.compute(params, (double)m_rate);
        // The window may have shrunk; keep every write cursor legal.
        for (size_t i = 0; i < m_state.size(); ++i) {
            m_state[i]->clampCount(m_coeffs.adjDelay);
        }
    }

    std::mutex      m_paramLock;
    DeCrackleParams m_pendingParams;
    DeCrackleParams m_activeParams;
    DeCrackleCoeffs m_coeffs;

    std::vector<channel_slot>                     m_slots;
    std::vector<std::unique_ptr<DeCracklePair> >  m_state;
    unsigned m_channels = 0;
    unsigned m_rate     = 0;
    unsigned m_config   = 0;
};

static dsp_factory_t<dsp_decrackle> g_dsp_decrackle_factory;

} // anonymous namespace
