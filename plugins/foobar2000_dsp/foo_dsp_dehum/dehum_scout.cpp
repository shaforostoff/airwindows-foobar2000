/* ========================================
 *  foo_dsp_dehum - track scout
 *
 *  See dehum_scout.h for why this exists.
 * ======================================== */

#include "stdafx.h"

#include "dehum_scout.h"

#include <vector>

namespace dehum_scout {

// Sixty seconds. Measured on 78 rpm tango transfers: a line the prominence
// route can see turns up inside 15 s, but one sitting down in the rumble -
// 7.8 dB prominent, well under the 16 dB threshold - only reaches the coherence
// route at 43 s, because that ratio accumulates over kCohWindowSec. Those are
// exactly the transfers this is worth doing for, so the window has to cover
// them.
const double kSecondsToRead = 60.0;

namespace {

// Decoded a chunk at a time; the decoder picks the size, this is only the
// starting capacity of the downmix buffer.
const size_t kMonoReserve = 16384;

//! Reads the opening of one track and returns what dehum settles on. Throws
//! whatever the SDK throws - unreadable file, unsupported format, aborted.
//!
//! Analysed at the file's own sample rate, which saves resampling it: a hum sits
//! at the same frequency in Hz whatever rate you look at it from, so the figures
//! transfer straight to a live Channel running at the device rate. Channel::adopt
//! converts its search range into Hz for exactly this reason.
int scan(const char * path, uint32_t subsong, dehum::Params params,
         abort_callback & abort, dehum::LineReport * out, int max) {
    service_ptr_t<input_decoder> decoder;
    input_entry::g_open_for_decoding(decoder, service_ptr_t<file>(), path, abort);

    // Not input_flag_playback: this is not the playing decoder, and the flag
    // turns on buffering behaviour meant for one.
    decoder->initialize(subsong, input_flag_simpledecode, abort);

    params.frequency = 0.0f;   // belt and braces; start() will not get here otherwise
    params.sanitize();

    dehum::Channel channel;
    bool     configured = false;
    unsigned rate       = 0;
    uint64_t wanted     = 0;
    uint64_t read       = 0;

    audio_chunk_impl chunk;

    // Hum is common mode, so one summed channel finds it for a fraction of the
    // work of running every channel through its own detector.
    std::vector<audio_sample> mono;
    mono.reserve(kMonoReserve);

    {
        dehum::scoped_flush_denormals ftz;

        for (;;) {
            // run() polls the abort callback, and so does the I/O under it, so
            // cancelling gets us out of here without waiting for the window.
            if (!decoder->run(chunk, abort)) break;

            const unsigned channels = chunk.get_channels();
            const unsigned sr       = chunk.get_sample_rate();
            size_t         frames   = chunk.get_sample_count();
            const audio_sample * const src = chunk.get_data();

            if (channels == 0 || sr == 0 || frames == 0 || src == NULL) continue;

            if (!configured) {
                dehum::Config cfg;
                cfg.compute(params, (double)sr);
                channel.configure(cfg);

                rate       = sr;
                wanted     = (uint64_t)(kSecondsToRead * (double)sr);
                configured = true;
            } else if (sr != rate) {
                // A rate change mid-stream would resize the analysis window and
                // throw away everything gathered so far. Stop with what we have.
                break;
            }

            if (read + frames > wanted) frames = (size_t)(wanted - read);
            if (frames == 0) break;

            mono.resize(frames);
            const audio_sample scale = (audio_sample)(1.0 / (double)channels);
            for (size_t f = 0; f < frames; ++f) {
                const audio_sample * const s = src + f * channels;
                audio_sample sum = 0;
                for (unsigned c = 0; c < channels; ++c) sum += s[c];
                mono[f] = sum * scale;
            }

            channel.process(mono.data(), frames, 1);

            read += frames;
            if (read >= wanted) break;
        }
    }

    if (!configured) return 0;

    int count = 0;
    channel.report(out, max, &count);
    return count;
}

//! Local files only. A stream has no opening to read ahead of, and reading one
//! over the network a second time to look for hum is not a trade worth making -
//! by the time it arrived the detector would have found the line on its own.
bool worthScouting(const char * path) {
    if (path == NULL || *path == 0) return false;
    try {
        return !filesystem::g_is_remote_or_unrecognized(path);
    } catch (...) {
        return false;
    }
}

} // anonymous namespace

// ---------------------------------------------------------------------------

Scout::Scout() : m_abort(new abort_callback_impl()), m_count(0), m_ready(false) {}

Scout::~Scout() {
    cancel();
    if (m_thread.joinable()) m_thread.join();
}

void Scout::cancel() {
    m_abort->abort();
}

std::shared_ptr<Scout> Scout::start(const char * path, uint32_t subsong,
                                    const dehum::Params & params) {
    if (!worthScouting(path)) return std::shared_ptr<Scout>();

    std::shared_ptr<Scout> scout;
    try {
        scout.reset(new Scout());
        // `this` rather than the shared_ptr on purpose: the destructor joins, so
        // the worker cannot outlive the object, and a shared_ptr held by the
        // thread the object owns would be a cycle that never unwinds.
        scout->m_thread = std::thread(&Scout::work, scout.get(),
                                      std::string(path), subsong, params);
    } catch (...) {
        // Out of memory, or no thread to be had. Scouting is an optimisation;
        // without it the detector still gets there, just later.
        return std::shared_ptr<Scout>();
    }
    return scout;
}

void Scout::work(std::string path, uint32_t subsong, dehum::Params params) {
    dehum::LineReport lines[dehum::kMaxLines];
    int count = 0;

    try {
        count = scan(path.c_str(), subsong, params, *m_abort,
                     lines, (int)dehum::kMaxLines);
    } catch (...) {
        // Unreadable, unsupported, or cancelled. Nothing to hand over, and
        // nothing worth telling the user about: the detector carries on.
        count = 0;
    }

    if (count <= 0 || m_abort->is_aborting()) return;

    try {
        // console::formatter prints itself when it goes out of scope.
        console::formatter msg;
        msg << "Dehum: scouted " << path.c_str() << " -";
        for (int i = 0; i < count; ++i) {
            msg << (i ? "," : "") << " " << pfc::format_float(lines[i].frequency, 0, 3)
                << " Hz (" << (lines[i].viaCoherence ? "coherence" : "prominence") << ")";
        }
    } catch (...) {
        // Nothing escapes a thread entry point, least of all over a log line.
    }

    for (int i = 0; i < count; ++i) m_lines[i] = lines[i];
    m_count = count;
    m_ready.store(true, std::memory_order_release);
}

int Scout::take(dehum::LineReport * out, int max) {
    if (!m_ready.load(std::memory_order_acquire)) return 0;

    int count = m_count;
    if (count > max) count = max;
    for (int i = 0; i < count; ++i) out[i] = m_lines[i];

    // Once. The lines are the Channel's business from here, and re-adopting them
    // every chunk would keep resetting the score of a line the detector had
    // already decided to let go of.
    m_ready.store(false, std::memory_order_relaxed);
    return count;
}

} // namespace dehum_scout
