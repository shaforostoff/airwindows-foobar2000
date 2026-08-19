/* ========================================
 *  foo_dsp_dehum - track scout
 *
 *  Dehum's detector needs several seconds of steady evidence before it will
 *  commit to a line, and on the transfers this component exists for that is not
 *  a few seconds but the better part of a minute: see the note on acquisition
 *  at the top of dehum_core.h. A line the prominence route can see arrives at
 *  about 9 s, one that only the coherence route can reach at 43 s. All of that
 *  is time the record plays with the hum still in it.
 *
 *  A file player does not have to spend it. The whole file is on disk, so the
 *  opening of it can be read on a worker thread while the first seconds play,
 *  and what that finds handed to Channel::adopt() - which starts the lines
 *  confirmed and leaves the detector running, so it still tracks them, still
 *  drops them if the evidence is not really there, and can still find others.
 *
 *  Deliberately per track: each record carries its own hum, so a Scout is
 *  created fresh for every track and publishes exactly once. Nothing is ever
 *  carried from one track to the next.
 * ======================================== */

#ifndef FOO_DSP_DEHUM_SCOUT_H
#define FOO_DSP_DEHUM_SCOUT_H

#include "dehum_core.h"

#include <atomic>
#include <memory>
#include <string>
#include <thread>

// SDK, forward declared so this header does not have to pull it in.
namespace foobar2000_io { class abort_callback_impl; }

namespace dehum_scout {

//! How much of the opening of a file the worker reads. Not generous: the
//! coherence route accumulates its ratio over kCohWindowSec, so the lines that
//! most need scouting are exactly the ones that need most of this window. Costs
//! roughly a second and a half of one core.
extern const double kSecondsToRead;

//! One scout, for one track. Start it with start(), poll it with take(), and
//! let go of it when the track ends - the destructor cancels the worker and
//! waits for it, so nothing outlives the object.
class Scout {
public:
    //! Reads the opening of `path` on a worker thread. `subsong` is the index
    //! from the track's playable_location, so cuesheet tracks and multi-subsong
    //! formats land on the right audio.
    //!
    //! `params` is used as the live DSP has it, minus the pinned frequency: a
    //! scout is only started when the frequency is left on automatic, since a
    //! frequency the user pinned by hand outranks anything found by searching
    //! (and adopt() would refuse it anyway).
    //!
    //! Returns null if the location is not worth scouting - a stream, or a
    //! protocol the analysis could not read faster than it plays.
    static std::shared_ptr<Scout> start(const char * path, uint32_t subsong,
                                        const dehum::Params & params);

    ~Scout();

    Scout(const Scout &) = delete;
    Scout & operator=(const Scout &) = delete;

    //! Tell the worker to stop. Returns immediately; the wait is in the
    //! destructor. Safe to call more than once.
    void cancel();

    //! Hands over the result once, if there is one, and returns how many lines
    //! were written. Zero every time until the worker finishes, and zero again
    //! afterwards - so the caller can poll this per chunk and adopt whatever
    //! turns up without having to remember that it already did.
    //!
    //! Cheap enough for the audio thread: an acquire load in the common case,
    //! and no lock at all, because a Scout publishes exactly once.
    int take(dehum::LineReport * out, int max);

private:
    Scout();

    void work(std::string path, uint32_t subsong, dehum::Params params);

    std::unique_ptr<foobar2000_io::abort_callback_impl> m_abort;
    std::thread m_thread;

    //! Written by the worker before m_ready is set, read by the consumer after
    //! it sees m_ready. The release/acquire pair on m_ready is what publishes
    //! them; nothing else touches either field.
    dehum::LineReport m_lines[dehum::kMaxLines];
    int m_count;
    std::atomic<bool> m_ready;
};

} // namespace dehum_scout

#endif // FOO_DSP_DEHUM_SCOUT_H
