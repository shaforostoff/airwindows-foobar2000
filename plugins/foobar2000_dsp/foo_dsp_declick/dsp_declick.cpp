/* ========================================
 *  foo_dsp_declick - foobar2000 DSP wrapper
 * ======================================== */

#include "stdafx.h"

#include "declick_core.h"
#include "declick_preset.h"
#include "resource.h"

#include <memory>
#include <mutex>
#include <vector>

using declick::Channel;
using declick::Config;
using declick::Params;
using declick::scoped_flush_denormals;   // FTZ; lives in the core so that every
                                         // port of it rounds the same way

namespace {

// ---------------------------------------------------------------------------

class dsp_declick : public dsp_impl_base_t<dsp_v3> {
public:
    dsp_declick(const dsp_preset & in) {
        m_pending = declick_preset::parse(in);
        m_active = m_pending;
        m_cfg.compute(m_active, 44100.0);
    }

    static GUID g_get_guid() { return declick_preset::guid(); }

    static void g_get_name(pfc::string_base & out) { out = "Declick (AR interpolation)"; }

    static bool g_get_default_preset(dsp_preset & out) {
        declick_preset::make(Params::defaults(), out);
        return true;
    }

    static bool g_have_config_popup() { return true; }

    static void g_show_config_popup(const dsp_preset & data, HWND parent,
                                    dsp_preset_edit_callback & callback) {
        declick_config_popup(data, parent, callback);
    }

    // -- dsp_v3 --------------------------------------------------------------

    bool apply_preset(const dsp_preset & preset) override {
        if (preset.get_owner() != g_get_guid()) return false;
        const Params p = declick_preset::parse(preset);
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

        const unsigned config = chunk->get_channel_config();
        if (channels != m_channels || rate != m_rate || params != m_active) {
            // A parameter change resizes the internal buffers, so the pipeline
            // has to be rebuilt either way; flush what is in it first so the
            // audio already in flight is not simply dropped.
            if (!reconfigure(channels, rate, config, params)) return true;
        }

        audio_sample * const data = chunk->get_data();
        if (data == NULL) return true;

        {
            scoped_flush_denormals ftz;
            for (unsigned c = 0; c < channels; ++c) {
                m_chan[c]->push(data + c, frames, channels);
            }
        }

        return emit(frames);
    }

    void on_endofplayback(abort_callback &) override {
        if (m_chan.empty()) return;
        {
            scoped_flush_denormals ftz;
            for (size_t c = 0; c < m_chan.size(); ++c) m_chan[c]->drain();
        }
        emit(0);
        for (size_t c = 0; c < m_chan.size(); ++c) m_chan[c]->reset();
    }

    void on_endoftrack(abort_callback &) override {}

    void flush() override {
        for (size_t c = 0; c < m_chan.size(); ++c) m_chan[c]->reset();
    }

    double get_latency() override {
        if (m_rate == 0) return 0.0;
        return (double)m_cfg.latency / (double)m_rate;
    }

    bool need_track_change_mark() override { return false; }

private:
    //! Moves whatever the pipeline has finished into the chunk list. Returns
    //! false if the caller's chunk should be dropped (nothing ready yet).
    bool emit(t_size /*consumed*/) {
        if (m_chan.empty()) return true;
        size_t avail = m_chan[0]->available();
        if (avail == 0) return false;      // still filling; drop the input chunk

        const unsigned channels = m_channels;
        while (avail > 0) {
            const size_t n = (avail > 16384) ? 16384 : avail;
            audio_chunk * out = insert_chunk(n * channels);
            if (out == NULL) break;
            out->set_sample_rate(m_rate);
            out->set_channels(channels, m_config);
            out->set_data_size(n * channels);
            out->set_sample_count(n);
            audio_sample * p = out->get_data();
            for (unsigned c = 0; c < channels; ++c) {
                m_chan[c]->pull(p + c, n, channels);
            }
            avail = m_chan[0]->available();
        }
        return false;    // our own chunks carry the audio; drop the original
    }

    bool reconfigure(unsigned channels, unsigned rate, unsigned config,
                     const Params & params) {
        // Push out anything still held before the buffers are rebuilt.
        if (!m_chan.empty()) {
            scoped_flush_denormals ftz;
            for (size_t c = 0; c < m_chan.size(); ++c) m_chan[c]->drain();
            emit(0);
        }

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
        m_config = config;
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
    unsigned m_config = 0;
};

static dsp_factory_t<dsp_declick> g_dsp_declick_factory;

} // anonymous namespace
