/* ========================================
 *  foo_dsp_dehum - foobar2000 DSP wrapper
 *
 *  Much shorter than the declick wrapper, because the core has no latency and
 *  works in place: there is no FIFO, no chunk re-emission and no drain. What
 *  goes in comes out, so on_chunk edits the caller's chunk and keeps it.
 * ======================================== */

#include "stdafx.h"

#include "dehum_core.h"
#include "dehum_preset.h"
#include "resource.h"

#include <memory>
#include <mutex>
#include <vector>

using dehum::Channel;
using dehum::Config;
using dehum::Params;
using dehum::scoped_flush_denormals;   // FTZ; lives in the core so that every
                                       // port of it rounds the same way

namespace {

// ---------------------------------------------------------------------------

class dsp_dehum : public dsp_impl_base_t<dsp_v3> {
public:
    dsp_dehum(const dsp_preset & in) {
        m_pending = dehum_preset::parse(in);
        m_active = m_pending;
        m_cfg.compute(m_active, 44100.0);
    }

    static GUID g_get_guid() { return dehum_preset::guid(); }

    static void g_get_name(pfc::string_base & out) { out = "Dehum (line detection)"; }

    static bool g_get_default_preset(dsp_preset & out) {
        dehum_preset::make(Params::defaults(), out);
        return true;
    }

    static bool g_have_config_popup() { return true; }

    static void g_show_config_popup(const dsp_preset & data, HWND parent,
                                    dsp_preset_edit_callback & callback) {
        dehum_config_popup(data, parent, callback);
    }

    // -- dsp_v3 --------------------------------------------------------------

    bool apply_preset(const dsp_preset & preset) override {
        if (preset.get_owner() != g_get_guid()) return false;
        const Params p = dehum_preset::parse(preset);
        std::lock_guard<std::mutex> lock(m_lock);
        m_pending = p;
        return true;
    }

    // -- dsp_impl_base -------------------------------------------------------

    bool on_chunk(audio_chunk * chunk, abort_callback &) override {
        const unsigned channels = chunk->get_channels();
        const unsigned rate = chunk->get_sample_rate();
        const t_size frames = chunk->get_sample_count();
        if (channels == 0 || rate == 0 || frames == 0) return true;

        Params params;
        {
            std::lock_guard<std::mutex> lock(m_lock);
            params = m_pending;
        }

        if (channels != m_channels || rate != m_rate) {
            if (!rebuild(channels, rate, params)) return true;
        } else if (params != m_active) {
            // Only the sample rate sizes anything in this core, so every slider
            // move retunes in place: no reallocation, no reset, and no gap in
            // the audio while the user is dragging.
            Config cfg;
            cfg.compute(params, (double)rate);
            bool ok = true;
            for (size_t c = 0; c < m_chan.size(); ++c) {
                if (!m_chan[c]->retune(cfg)) { ok = false; break; }
            }
            if (!ok) {
                if (!rebuild(channels, rate, params)) return true;
            } else {
                m_cfg = cfg;
                m_active = params;
            }
        }

        audio_sample * const data = chunk->get_data();
        if (data == NULL) return true;

        {
            scoped_flush_denormals ftz;
            for (unsigned c = 0; c < channels; ++c) {
                m_chan[c]->process(data + c, frames, channels);
            }
        }
        return true;
    }

    void on_endofplayback(abort_callback &) override {
        for (size_t c = 0; c < m_chan.size(); ++c) m_chan[c]->reset();
    }

    void on_endoftrack(abort_callback &) override {}

    //! A seek. The lines are kept: the hum on the far side of a seek is the same
    //! hum, and re-acquiring it every time the user moves the playback position
    //! would mean several seconds of it coming back each time.
    void flush() override {
        for (size_t c = 0; c < m_chan.size(); ++c) m_chan[c]->flush();
    }

    double get_latency() override { return 0.0; }

    bool need_track_change_mark() override { return false; }

private:
    bool rebuild(unsigned channels, unsigned rate, const Params & params) {
        Config cfg;
        cfg.compute(params, (double)rate);

        std::vector<std::unique_ptr<Channel> > chans;
        try {
            for (unsigned c = 0; c < channels; ++c) {
                std::unique_ptr<Channel> ch(new Channel());
                ch->configure(cfg);
                chans.push_back(std::move(ch));
            }
        } catch (const std::bad_alloc &) {
            m_chan.clear();
            m_channels = 0; m_rate = 0;
            return false;
        }

        m_chan.swap(chans);
        m_cfg = cfg;
        m_channels = channels;
        m_rate = rate;
        m_active = params;
        return true;
    }

    std::mutex m_lock;
    Params m_pending;
    Params m_active;
    Config m_cfg;

    std::vector<std::unique_ptr<Channel> > m_chan;
    unsigned m_channels = 0;
    unsigned m_rate = 0;
};

static dsp_factory_t<dsp_dehum> g_dsp_dehum_factory;

} // anonymous namespace
