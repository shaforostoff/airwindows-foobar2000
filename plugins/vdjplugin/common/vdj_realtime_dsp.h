/* ========================================
 *  vdjplugin - IVdjPluginDsp8 wrapper
 *
 *  The live path. This is the ordinary VirtualDJ sound effect: a stereo float
 *  buffer arrives, it is processed in place, n samples out for n samples in.
 *
 *      HRESULT OnProcessSamples(float *buffer, int nb);
 *
 *  There is no lookahead here and no way to ask for one - the interface has no
 *  latency to declare and VirtualDJ has nothing to compensate it with - so a
 *  core that holds samples costs a delay, and the delay is real: it moves this
 *  deck's audio later than everything it is being mixed against. That is the
 *  whole reason the buffer wrapper next door exists.
 *
 *  Which of the two to use is therefore per core, not a preference:
 *
 *    Dehum has zero latency by construction. Its detector reads the signal but
 *    does not sit in the path, so this wrapper costs it nothing and it is the
 *    natural form - it will also run on a microphone or on the master, which a
 *    buffer plug-in cannot, because there is no song to read ahead of.
 *
 *    Declick holds Config::latency samples, 880 at 44.1 kHz and the default
 *    Max repair and Model order, so here it plays about 20 ms late. Usable on a
 *    mic, on the master, or on a deck nobody is beatmatching; on a deck that is
 *    being mixed, use DeclickBuffer instead and pay nothing.
 *
 *  prime() is what makes n-in/n-out work: feeding the core `latency` zeros up
 *  front makes available() >= n hold after every subsequent push of n, for any
 *  n and any block size, so the loop below can never come up short. See the
 *  note on declick::Channel::prime() - it is the same mechanism the VST2 port
 *  uses, and for the same reason.
 * ======================================== */

#ifndef VDJ_REALTIME_DSP_H
#define VDJ_REALTIME_DSP_H

#include "vdjDsp8.h"

#include "vdj_engine.h"

#include <algorithm>
#include <new>
#include <vector>

namespace vdj {

//! Frames per pass of the inner loop. VirtualDJ's own block is smaller than
//! this in every configuration that matters, so in practice this is one pass.
enum { kRealtimeSlice = kMaxSliceFrames };

template<class Engine>
class RealtimeDsp : public IVdjPluginDsp8 {
public:
    HRESULT VDJ_API OnLoad() override {
        m_engine.declareParameters(*this);
        return S_OK;
    }

    HRESULT VDJ_API OnGetPluginInfo(TVdjPluginInfo8 * info) override {
        info->PluginName  = Engine::liveName();
        info->Author      = "Airwindows tree (MIT)";
        info->Description = Engine::liveDescription();
        info->Version     = "1.0";
        info->Flags       = 0x00;
        info->Bitmap      = NULL;
        return S_OK;
    }

    ULONG VDJ_API Release() override { delete this; return 0; }

    //! Switched on. The pipeline is holding whatever went through it the last
    //! time, which is not audio anybody wants spliced onto the front of this.
    HRESULT VDJ_API OnStart() override {
        m_needsPrime = true;
        return S_OK;
    }

    HRESULT VDJ_API OnStop() override { return S_OK; }

    HRESULT VDJ_API OnGetParameterString(int id, char * out, int size) override {
        return m_engine.parameterString(id, out, size) ? S_OK : E_NOTIMPL;
    }

    HRESULT VDJ_API OnProcessSamples(float * buffer, int nb) override {
        if (buffer == NULL || nb <= 0) return S_OK;
        try {
            const double rate = (SampleRate > 0) ? (double)SampleRate : 44100.0;
            if (rate != m_rate) {
                if (!m_engine.setRate(rate)) return E_FAIL;
                if (!reserve()) return E_FAIL;
                m_rate = rate;
                m_needsPrime = true;
            }
            // A VirtualDJ plug-in finds out that a slider moved the same way a
            // VST does - by looking - because VirtualDJ writes straight through
            // the pointer DeclareParameterSlider was given. A rebuild punches a
            // hole in the pipeline, so it has to be re-primed.
            if (m_engine.update(rate)) m_needsPrime = true;

            if (m_needsPrime) {
                m_engine.reset();
                m_engine.drain();      // the pre-roll; see the note above
                m_needsPrime = false;
            }

            int done = 0;
            while (done < nb) {
                const size_t n = (size_t)std::min<int>(nb - done, (int)kRealtimeSlice);
                float * p = buffer + (size_t)done * kChannels;
                fromFloats(p, m_scratch.data(), n);
                m_engine.push(m_scratch.data(), n);
                // Guaranteed by the pre-roll. If it ever were not, writing
                // uninitialised scratch back would be far worse than a gap.
                if (m_engine.available() < n) {
                    std::fill(m_scratch.begin(), m_scratch.begin() + n * kChannels, 0.0);
                } else {
                    m_engine.pull(m_scratch.data(), n);
                }
                m_dither.write(m_scratch.data(), p, n);
                done += (int)n;
            }
            return S_OK;
        } catch (...) {
            return E_FAIL;
        }
    }

private:
    bool reserve() {
        try {
            m_scratch.assign((size_t)kRealtimeSlice * kChannels, 0.0);
        } catch (const std::bad_alloc &) {
            return false;
        }
        return true;
    }

    Engine m_engine;
    std::vector<double> m_scratch;
    FloatDither m_dither;
    double m_rate = 0.0;
    bool   m_needsPrime = true;
};

} // namespace vdj

#endif // VDJ_REALTIME_DSP_H
