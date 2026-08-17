# foobar2000 DSP components

Three DSPs for restoring 78s and vinyl transfers:

| Component | What it is |
| --- | --- |
| **foo_dsp_decrackle** | A port of the Airwindows **DeCrackle** plug-in, verified **bit-identical** to the VST source. Best on stereo material. |
| **foo_dsp_declick** | An autoregressive detect-and-interpolate declicker. **Use this one for mono shellac** — see [Which one to use](#which-one-to-use). |
| **foo_dsp_dehum** | Finds continuous narrowband tones by itself and cancels them. For hum, which is a different defect from clicks and needs a different instrument. Zero latency. |

All three:

* Target **foobar2000 1.5 and later**, 32-bit and 64-bit.
* Build with **CMake**, so any Visual Studio from 2017 15.7 onward works.
* Depend on nothing beyond the foobar2000 SDK, which is downloaded
  automatically. No WTL, no ATL, no vcpkg, no 7-Zip.
* Link the CRT statically — nothing to install alongside them.

---

## Which one to use

They are not interchangeable, and the difference is structural rather than a
matter of taste.

**DeCrackle** decides a click is present by looking at `|L * R * 64|` — a
measure of correlated energy that a one-channel surface tick fails to produce.
On a **mono** transfer `L == R`, so that term collapses to `64x²`, which sits
2.6–7.3× above `|x|` on typical 78 rpm material. The detector is then either
silent or firing on 60% of samples, with nothing useful in between.

It also repairs by crossfading to a lowpassed copy of the damaged audio, so
during a click it outputs a smoothed version of the click rather than a
reconstruction of the music underneath.

**Declick** detects clicks as spikes in an autoregressive prediction residual —
which works the same whether the source is mono or stereo — and repairs them by
least-squares interpolation, reconstructing what the waveform should have been.

Rough guide: **stereo vinyl → either; mono shellac → Declick.**

**Dehum is not an alternative to either of them.** Clicks are impulsive and
broadband, hum is continuous and narrowband, and nothing that detects one will
find the other. Run Dehum alongside whichever declicker suits the material.

---

## Building a release package

```bash
powershell -ExecutionPolicy Bypass -File scripts\build_release.ps1
```

That configures and builds x86 and x64 in Release, runs the test suites, and
writes to `dist/`:

| File | What it is |
| --- | --- |
| `foo_dsp_decrackle-1.0.0.fb2k-component` | Installable component, both architectures in one file |
| `foo_dsp_declick-1.0.0.fb2k-component` | Same, for the declicker |
| `foo_dsp_dehum-1.0.0.fb2k-component` | Same, for the dehummer |
| `*-symbols.zip` | PDBs — keep these, they are what makes a foobar2000 crash report readable |

Install by dragging the `.fb2k-component` onto foobar2000, or via
**File → Preferences → Components → Install…**

Inside the archive:

```
foo_dsp_decrackle.dll        32-bit, loaded by foobar2000 1.5 – 2.x (x86)
x64/foo_dsp_decrackle.dll    64-bit, loaded by foobar2000 2.x (x64)
```

foobar2000 ignores architecture folders it does not understand, so a single
file installs correctly on every version.

### Options

```powershell
# Build only 64-bit
.\scripts\build_release.ps1 -Arch x64

# Build only one component
.\scripts\build_release.ps1 -Component foo_dsp_declick

# Windows 7 compatible build, verified before packaging (see the note below)
.\scripts\build_release.ps1 -Win7

# Assume AVX2 - only if the playback machine definitely has it
.\scripts\build_release.ps1 -InstructionSet AVX2

.\scripts\build_release.ps1 -Clean -SkipTests
```

### Targeting Windows 7

```powershell
.\scripts\build_release.ps1 -Win7
```

`-Win7` picks the newest installed MSVC toolset that still supports Windows 7
(v142, otherwise v141), and runs `scripts\check_win7.ps1` over the built DLLs
before packaging, so a component that cannot load on Windows 7 is never
produced.

The defaults already do most of the work: the `SSE2` baseline runs on anything
Windows 7 runs on, `_WIN32_WINNT=0x0601` keeps newer APIs out of reach at
compile time, and the static CRT means the Visual C++ redistributable — which
no longer installs on Windows 7 from version 14.40 onward — is not needed.

What `-Win7` adds is the toolset. From Visual Studio 2022 17.10 onward, v143
dropped Windows 7 as a supported target. v142 ships with Visual Studio 2019, or
as the *MSVC v142 – VS 2019 C++ build tools* individual component in the Visual
Studio 2022 installer; v141 ships with Visual Studio 2017. If neither is
installed, `-Win7` warns and builds with v143 anyway — the check still runs, but
the result is not something Microsoft supports.

The Windows SDK version is a red herring here. `cmake` reporting

```
-- Selecting Windows SDK version 10.0.26100.0 to target Windows 10.0.19045.
```

says which headers and import libraries are used, not which Windows versions
the binary runs on — that is decided by `_WIN32_WINNT`, the toolset and the CRT.
`-WindowsSdk 10.0.17763.0` pins an older one if you want it, but note that the
foobar2000 SDK needs `ERROR_NO_SUCH_DEVICE`, which SDK 10.0.17763.0 does not
define.

#### Checking a binary on its own

```powershell
.\scripts\check_win7.ps1 dist\foo_dsp_declick-1.0.0.fb2k-component
```

Takes DLLs, directories or `.fb2k-component` archives, and reports anything
that would stop the image from loading on Windows 7:

- a minimum OS or subsystem version above 6.1 in the PE header,
- imports from `api-ms-win-*` / `ext-ms-*` API sets, `combase.dll` or
  `shcore.dll`, none of which exist on Windows 7,
- imports of `vcruntime140.dll`, `msvcp140.dll` or `ucrtbase.dll`, i.e. a
  dynamic CRT,
- individual Windows 8/8.1/10 exports, such as the `WaitOnAddress` family a
  modern STL likes to reach for.

It is a load-time check. Functions resolved at runtime through `GetProcAddress`
are invisible to it, and so is the instruction set the code was compiled for —
an `AVX2` build loads fine on a CPU without AVX2 and then crashes.

---

## Building by hand

```bash
cmake -S . -B build/x64 -A x64
cmake --build build/x64 --config Release
```

```bash
cmake -S . -B build/x86 -A Win32
cmake --build build/x86 --config Release
```

The first configure downloads and unpacks
`https://www.foobar2000.org/downloads/SDK-2025-03-07.7z` into
`external/foobar2000_sdk` (checksum verified). CMake's bundled libarchive
reads `.7z`, so no external tool is needed.

To fetch the SDK on its own:

```bash
powershell -ExecutionPolicy Bypass -File scripts\get_sdk.ps1
```

or straight from CMake:

```bash
cmake -DFB2K_SDK_DEST=external/foobar2000_sdk -P cmake/fb2k_download_sdk.cmake
```

### CMake options

| Option | Default | Meaning |
| --- | --- | --- |
| `FB2K_SDK_DIR` | `external/foobar2000_sdk` | Where the SDK lives |
| `FB2K_SDK_AUTO_DOWNLOAD` | `ON` | Fetch it if missing |
| `FOO_DSP_STATIC_CRT` | `ON` | `/MT` instead of `/MD` |
| `FOO_DSP_LTO` | `ON` | `/GL` + `/LTCG` in Release |
| `FOO_DSP_ARCH` | `SSE2` | `SSE2`, `AVX` or `AVX2` — see [Vectorization](#vectorization); leaving it at `SSE2` costs nothing |
| `FOO_DSP_WIN32_WINNT` | `0x0601` | Minimum Windows version |
| `FOO_DSP_BUILD_TESTS` | `ON` | Build the verification harnesses |

---

## Declick parameters

Add *Declick (AR interpolation)* to the chain and press **Configure selected**.

| | |
| --- | --- |
| **Sensitivity** | The one to reach for. Raise it until the crackle goes, then back off as soon as the music starts to dull. Maps onto the trigger threshold in robust sigmas: 0 → 6.0σ, 1 → 2.5σ. |
| **Extent** | How far a detection spreads into its own tail before the repair stops. Raise it if repairs leave a residual tick behind them. |
| **Max repair** | Longest single repair. Anything longer is treated as music and left alone. 4 ms suits 78s. |
| **Passes** | A second pass catches clicks the first one uncovers; the model is refitted in between. Small but real gain, roughly 50% more CPU. |
| **Model order** | 8–256. The one control where spending CPU clearly buys quality: 128 measurably beats the default 32 and 256 beats that again. It is not the default because it costs about 3.6× and 10× the CPU respectively — but at 50× realtime, order 128 is affordable on far weaker hardware than it used to be. See [Does more CPU help?](#does-more-cpu-help). |
| **Repair depth** | How much of each click to subtract. 0 removes the calibrated fraction that adds the least error of its own; 1 replaces the damaged samples outright. See [Repair depth](#repair-depth). |
| **Dry/Wet** | 0 bypasses. |

Measured over 20 s excerpts of four 78 rpm transfers, at the default repair
depth of 0. The unprocessed originals sit at **71** impulsive events/s.

| Sensitivity | samples repaired | events/s | collateral damage | HF change | precision |
| --- | --- | --- | --- | --- | --- |
| 0.00 | 0.7 % | 39 | −60.8 dB | −0.3 dB | 0.91 |
| 0.30 | 1.5 % | 26 | −50.2 dB | −0.5 dB | 0.85 |
| **0.60** (default) | 3.4 % | 21 | −46.5 dB | −0.8 dB | 0.72 |
| 0.70 | 4.6 % | 20 | −45.4 dB | −1.0 dB | 0.66 |
| 0.85 | 7.6 % | 19 | −43.2 dB | −1.3 dB | 0.55 |
| 1.00 | 14.0 % | 18 | −41.8 dB | −1.7 dB | 0.43 |

Note how flat the right-hand end is: quadrupling the number of samples touched
(3.4 % → 14.0 %) only takes 21 events/s down to 18. Past about 0.7, sensitivity
mostly buys collateral damage. **Repair depth is the more effective lever**.

Throughput: ~180× realtime at the default, measured end to end over 60 s of
44.1 kHz mono including file I/O — well under 1 % of one core on a modern
desktop. See [Where the time goes](#where-the-time-goes-and-what-was-done-about-it).
Latency ~18 ms, reported to foobar2000 so visualisations stay in sync. Note
that `config().latency` is a buffering delay, not a shift: that many samples
must go in before the first block comes out, but the emitted stream is aligned
with the input.

Memory is **750 kB per channel** at 44.1 kHz and 1.5 MB at 192 kHz, fixed at
`configure()` and independent of the parameters — see
[Real-time safety](#real-time-safety) for why it is sized that way, and why
that figure is *lower* than what the same code used to reach at run time.

### Real-time safety

An audio callback must not allocate: `malloc` may take a lock, and a lock held
by a lower-priority thread is a dropout. `declick_rt_verify` replaces global
`operator new` and requires the count to be **zero** across the processing path,
every parameter change, `reset()`, `prime()` and `drain()`, in both access
patterns — one sample at a time as the VST does it, and whole-chunk-then-drain
as foobar2000 does.

Getting there took two fixes, neither of which was visible until counted:

* **The output FIFO** was a `std::vector` that grew by `push_back` to 65536
  samples and was then compacted with `erase()`. About a second and a half into
  *every stream* the audio thread took a **720 kB reallocation**. It is now a
  fixed-capacity ring, sized for `maxBlock + 2 * kBlock`.
* **The per-click interpolation scratch** was `assign()`ed to a length that
  varies with the run being repaired, so it grew whenever a longer click turned
  up than any seen so far — a long tail of reallocations that never quite
  stopped. It is now reserved for the worst case the detector can produce.

The buffers are sized from `Config`'s `buf*` envelope, which is derived from the
**sample rate alone** — `bufOrder` is `kMaxOrder` and `bufMaxRun` is
`Params::sanitize()`'s 20 ms ceiling. So a parameter change that resizes the
pipeline still resets it, but `configure()` reassigns every vector to the length
it already has, and neither `assign()` nor `resize()` reallocates below the
capacity it is holding. Only a sample rate change touches the heap.

Sizing for the envelope rather than the current settings sounds like it should
cost memory, and it does not:

| | after `configure()` | peak during playback |
| --- | --- | --- |
| before | 100 kB | **858 kB** |
| now | **750 kB** | 750 kB |

The old code simply reached its footprint later, on the audio thread, one
reallocation at a time. Two things keep the new figure down: the ring is 139 kB
where the grown FIFO reached 720 kB, and the Wiener buffers — 455 kB of it, and
the largest single item — are **not reserved at all** unless
`Config::wienerAlpha` is greater than zero, which by default it is not. With the
Wiener path deliberately engaged it is 1.2 MB per channel.

The refactor is bit-exact: 8 parameter/sample-rate combinations × 3 block
patterns × Wiener off and on, hashed before and after, identical.

---

## Ground truth

A **clean master transfer of the same performance** was compared to a crackly copy.

The two cannot be compared sample for sample — they are different pressings.
(For the pair used here the local waveform correlation is 0.32, though the
speed difference is a clean 328 ppm with only 8 samples of residual, which
confirms it is the same 1943 recording.) So instead:

1. Harvest real click waveforms from the crackly copy — 40 000 of them, median
   6 samples long.
2. Inject them into the clean master **at known positions**.

That gives exact ground truth: the true clean signal and the exact damaged
samples are both known, so detection and reconstruction can be scored directly
instead of by proxy.

* **Detection is not the bottleneck.** 81 % of injected clicks are found, and
  those hold **98 % of the injected energy**. Clicks above −26 dBFS: 100 %.
  Stronger than that — see [The threshold is a selector](#the-threshold-is-a-selector)
  — *perfect* detection is actively **worse** than the real detector.
* **Reconstruction is.** AR interpolation manages ~30 dB SNR over a 3-sample
  hole, 22 dB over 6, and only 8 dB over 16 — while the click itself sits about
  18 dB below the music. Past roughly 6 samples, replacing the samples was
  *worse than leaving the click alone*.

### The threshold is a selector

Replacing the detector with the true mask — perfect detection, identical repair —
makes the result **worse**:

| | repair dB | harm dB | net dB | events/s |
| --- | --- | --- | --- | --- |
| the real detector, default | 1.79 | −42.4 | +0.60 | 28 |
| perfect detection, all 3600 clicks | 0.62 | −∞ | +0.62 | 37 |
| perfect detection, top 50 % by size | 1.59 | −∞ | +1.59 | 42 |
| **perfect detection, top 25 % by size** | **2.07** | −∞ | **+2.07** | 52 |
| perfect detection, top 5 % by size | 1.59 | −∞ | +1.59 | 66 |

Repairing *fewer* clicks is three times better than repairing all of them, and
the reason is arithmetic. The median injected click is only **−20.8 dB**
relative to the local signal, while reconstruction SNR is 11.9 dB — so for a
typical click the interpolation error is about **9 dB larger than the click it
replaces**. Only the loudest fifth are worth touching at all.

So the sensitivity threshold is not merely *finding* clicks. It is **selecting
which repairs are worth making**, and it is doing that job well: the real
detector at its default reaches +0.60 dB while leaving only 28 events/s, where
the selective oracle buys +1.47 dB more fidelity at the cost of nearly doubling
the residual crackle. Both columns matter to a listener, so those are not
directly comparable — but it does mean there is at most ~1.5 dB available to any
better *ranking* of clicks, and none at all to merely finding more of them.

This is the single most useful thing the ground truth has produced, because it
bounds a whole family of proposals at once. Wavelet or STFT sub-band detection,
kurtosis or skew thresholds on sub-band energy, Bayesian inference, Markov
random fields, HMM burst models — every one of these is a way of deciding
*which samples are damaged*, so every one of them is capped by the table above.
Two further notes on that family:

* The AR prediction residual is already a **signal-adaptive whitening
  transform**, which is what makes small broadband impulses stand out against
  coloured music. A wavelet high-band is a *fixed* filter bank, so it is not
  obviously a better place to look.
* The hysteresis in `interpolate()` is already a two-state sticky chain — high
  threshold to enter, low threshold to stay, which is what **Extent** tunes. An
  HMM would be the principled version of something that is present in crude
  form, not a missing capability.
* The premise that dense crackle turns the signal into "a sequence of missing
  data gaps" does not hold at real densities. Genuine 78 rpm transfers measure
  1–2 % contaminated; injecting 1800 clicks/s only reached **5.2 %**. At 95 %
  intact this is not an inpainting problem.

### Repair depth

That last point produced the one real algorithmic change to come out of this.
A click **adds** to the music, it does not erase it, so the damaged samples
still carry the signal underneath; replacing them outright throws that away.
So the repair is **subtractive**: what gets removed is the discrepancy
`d = x − v` between the sample and the model's estimate, not the sample.

| gap length | full replacement | partial subtraction |
| --- | --- | --- |
| 1–3 samples | +7.7 dB | +8.0 dB |
| 4–6 | +3.6 dB | +5.0 dB |
| 7–10 | **−2.1 dB** | +1.6 dB |
| 11–15 | **−6.4 dB** | +0.7 dB |
| 24+ | **−10.0 dB** | ~0 dB |

With perfect detection, full replacement scored **−6.9 dB** — actively harmful.
Subtracting a fixed **0.45** of the discrepancy is what ships. The **Repair
depth** control scales from there towards outright replacement:

| depth | click reduction | collateral harm | whole-file error | events/s left |
| --- | --- | --- | --- | --- |
| **0.00** (default) | 1.79 dB | −42.4 dB | **+0.60 dB** | 28 |
| 0.25 | 2.00 dB | −40.1 dB | +0.07 dB | 21 |
| 0.50 | 1.98 dB | −38.2 dB | −0.70 dB | 17 |
| 1.00 (full replacement) | 1.29 dB | −35.4 dB | −2.52 dB | 15 |

Two things worth noting. Full replacement is worse at *removing clicks* than a
partial subtraction is (1.29 dB against 2.00 dB) — its own interpolation error
partly undoes the repair. And the clean master itself measures 27 impulsive
events per second; the default lands at 28, essentially back at that natural
level, while higher depths drive it *below* the master, which means they are
removing real musical transients. Hence the conservative default. Raise it if
you would rather trade some added error for less audible crackle — the numbers
above say 0.25–0.5 is the sensible range to explore.

#### What did not work: per-sample Wiener weighting

The subtraction fraction started life as a curve indexed by gap length (0.75
up to 6 samples, 0.25 out to 23, nothing beyond), fitted by hand to the
per-length optima above. Replacing that with something derived rather than
tabulated looked compelling: the least-squares interpolation solves `G u = −b`,
so under the model's Gaussian innovation the posterior covariance of the repair
is `σ²·G⁻¹`, and the solve has already produced the Cholesky factor of `G`.
One extra banded recursion (Takahashi's, `O(n·band²)`) yields `diag(G⁻¹)` —
the per-sample uncertainty — and the MMSE fraction to subtract is then the
Wiener gain `1 − P/d²`. That should taper the correction towards the middle of
a long run, where the estimate is worst, which a per-run constant cannot
express at all.

It was implemented, verified exact against a dense inverse (1.6e-15), and
measured. **It does not pay on this material.** Against the flat fraction at
the same cap it was worth +0.05 dB of whole-file error and made the residual
event rate *worse* by 4–7 events/s. The reason is visible in the formula: the
click is nearly always far larger than the estimate's own uncertainty, so
`d² ≫ P`, the gain saturates at 1, and only the cap ever binds. Inflating `P`
by 4× does make it gate marginal detections — collateral harm improves by up
to 2.2 dB — but that trade is strictly worse than simply lowering sensitivity,
which reaches the same fidelity with 13 fewer events/s left behind.

What *did* pay was the thing the experiment surfaced along the way: the
length-indexed curve was the problem, not the lack of a variance term. A flat
fraction beats it on every axis of both datasets, mostly because the curve gave
up entirely past 28 samples and was too timid between 7 and 23.

| | click reduction | collateral harm | whole-file error | events/s |
| --- | --- | --- | --- | --- |
| old length-indexed curve | 1.37 dB | −41.5 dB | +0.07 dB | 31 |
| **flat 0.45** (ships) | **1.79 dB** | **−42.4 dB** | **+0.60 dB** | **28** |

The machinery is still in the tree, off by default (`Config::wienerAlpha = 0`,
so the recursion is not even run) and sweepable from `declick_cli --alpha`.
Material with different click statistics — louder clicks on stereo vinyl, say —
might land somewhere else, and the code costs nothing while it is off.

#### What did not work: robust (M-estimation) AR fitting

The standard advice for AR declicking is to fit the model iteratively,
downweighting the equations where the prediction error is largest, so the fit
can "see through" the crackle instead of being dragged towards it. The fit here
does something much cruder — a single amplitude clip at 6× mean `|x|` — so this
looked like an obvious gap. It is not, and the reason is worth recording.

The ceiling was measured before implementing anything: hold detection perfect,
keep the interpolation identical, and change **only** where the coefficients
come from. Fitting on the *true clean master* is what no robust estimator can
beat.

| gap | shipped | no clip at all | IRLS Huber | IRLS reject | **oracle (clean)** |
| --- | --- | --- | --- | --- | --- |
| 1–3 | 30.6 | 30.6 | 30.6 | 30.5 | 31.3 |
| 4–6 | 26.5 | 26.5 | 26.2 | 26.1 | 26.7 |
| 7–10 | 19.5 | 19.5 | 19.2 | 19.1 | 19.9 |
| 16–23 | 9.9 | 9.9 | 9.8 | 9.7 | 10.2 |
| **all** | **11.9** | 11.9 | 11.7 | 11.7 | **12.2** |

A perfect model is worth **+0.28 dB**. Every IRLS variant tried — Huber and
Tukey weights, hard rejection, 3 iterations, cut-offs from 1σ to 4σ — came out
*behind* the shipped fit, by 0.16 to 0.33 dB.

Since the objection to that is "your ground truth is not crackly enough", the
density was raised until it was, using 31 920 real click waveforms harvested
from a heavily crackled 1938 D'Arienzo transfer:

| clicks/s | contamination | shipped | IRLS Huber | oracle | headroom |
| --- | --- | --- | --- | --- | --- |
| 60 | 0.7 % | 16.87 | 16.51 | 16.76 | −0.11 dB |
| 180 | 2.1 % | 19.36 | 18.52 | 19.60 | +0.24 dB |
| 600 | 4.7 % | 18.82 | 18.66 | 18.65 | −0.17 dB |
| 1800 | 5.2 % | 17.78 | 17.60 | 18.08 | +0.30 dB |

The headroom never clears +0.30 dB, at times it is negative (noise around
zero), and IRLS is behind at **every** density. Three reasons, all measurable:

* **The clicks are tiny, not large.** Median peak −39 dBFS on the crackly
  transfer; the injected set has median amplitude −50.8 dBFS. The shipped clip
  threshold sits at −8.5 dBFS, so **0.0 %** of damaged samples ever reach it —
  the existing "protection" is inert, which is also why *no clip at all*
  scores identically.
* **Contamination is too sparse to bias a sum over a thousand lag products.**
  Even at 5 % it barely moves the autocorrelation.
* **IRLS actively hurts because music is unpredictable too.** A high residual
  means "surprising", and piano attacks and bandoneón accents are surprising.
  Downweighting them biases the model towards the smooth part of the spectrum —
  precisely the wrong direction for reconstructing transients.

The published advice is sound; it is aimed at a different regime. Vinyl ticks
and scratches are *loud* outliers, tens of dB above the noise floor and
sometimes clipping, and there M-estimation earns its keep. Shellac crackle is
the opposite: dense but minuscule. It corrupts the samples it hits badly and
the model estimate hardly at all.

The practical conclusion is that **the AR coefficients are not the bottleneck**
and never were. Reconstruction is limited by how much information the
surrounding samples carry about a hole, not by the accuracy of the model
describing them. That is also why more model order and more context bought so
little.

The defaults are conservative. On the same four transfers (originals at 71 events/s):

| | events/s | collateral damage | HF change |
| --- | --- | --- | --- |
| default (s 0.60, depth 0) | 21 | −46.5 dB | −0.8 dB |
| s 0.60, depth 0.50 | 16 | −42.4 dB | −1.2 dB |
| s 0.80, depth 0.50 | 13 | −39.8 dB | −1.7 dB |
| s 0.80, depth 1.00 | 12 | −37.0 dB | −2.0 dB |

The ground-truth numbers above say that trade is net-negative in fidelity terms;
whether it is net-positive to your ears is a listening question, not a measurement one.

### Does more CPU help?

**Yes — model order is the one lever that clearly pays.** An earlier revision of
this section said the opposite; it was wrong, and the correction is instructive.
The reconstruction figures it quoted were right, but judging them per gap length
hid what they do end to end.

Reconstruction SNR (dB) by gap length, detection held perfect and the
interpolator identical, changing only the coefficients:

| model | 1–3 | 4–6 | 7–10 | 11–15 | 16–23 | 24–40 | all |
| --- | --- | --- | --- | --- | --- | --- | --- |
| order 32, ctx ±96 (default) | 30.6 | 26.5 | 19.5 | 13.0 | 9.9 | 6.5 | **11.9** |
| order 32, ctx ±528 | 30.6 | 26.5 | 19.5 | 13.0 | 9.9 | 6.5 | **11.9** |
| order 128, ctx ±528 | 31.2 | 27.2 | 21.8 | 16.3 | 12.7 | 9.9 | **15.0** |
| order 256, ctx ±768 | 30.3 | 27.7 | 23.5 | 19.2 | 15.7 | 14.1 | **18.3** |

Context alone is worth **+0.00 dB** — the identical first two rows are not a
copy-paste error. Order is worth up to **+6.4 dB**.

And it survives end to end. With the *real* detector's own verdict and only the
model changed:

| | repair dB | harm dB | net dB | events/s |
| --- | --- | --- | --- | --- |
| order 32 (ships), w 0.45 | 1.79 | −42.4 | +0.60 | 28 |
| order 64, w 0.45 | 2.47 | −41.2 | +0.76 | 28 |
| order 128, w 0.45 | 2.87 | −42.1 | +1.31 | 29 |
| **order 256, w 0.60** | **4.34** | **−42.8** | **+2.55** | **25** |

Order 256 is better on **all four axes at once**, and note that its best
subtraction fraction rises from 0.45 to 0.60 — exactly what the break-even
argument predicts, because a better estimate makes more of each click worth
removing.

Two things that do *not* need changing: the fit window and the context. The
window the component already has (`2*(maxRun + 3*order) + 512`) beats a fixed
4096 at every order, by 0.25–0.38 dB — music is not stationary, so a tighter
window tracks the local spectrum better. Raising **Model order** is sufficient
on its own.

#### The cost

Measured end to end over 60 s of 44.1 kHz mono including file I/O:

| order | x64 | x86 | before optimisation | 2014 MacBook Air, stereo (estimated) |
| --- | --- | --- | --- | --- |
| **32** (default) | **181×** | 156× | 84× | ~25× |
| 64 | 110× | 99× | 39× | ~15× |
| 128 | **50×** | 45× | 16× | ~7× |
| 256 | 18× | 15× | 5× | ~2.5× |

The last column is an estimate, not a measurement: a 1.4 GHz Haswell i5-4260U
is roughly 3–4× slower per core than the machine above, and stereo doubles the
work. After the optimisation below, **order 128 is viable on that machine** —
it was not before.

`kMaxOrder` is 256 and **the default stays 32**, because tripling the CPU cost
of a component is the user's call, not a silent change. If you have the
headroom, order 128 is the setting to reach for.

#### Where the time goes, and what was done about it

Profiled by the difference between a clean file and a crackly one, the
per-click interpolation is only **2 % of the run time at order 32 and 7 % at
order 256**. Almost everything is the model fit and the two prediction-residual
passes. So an earlier note here, suggesting the per-click residual be shared
across clicks and patched per gap, was aimed at the wrong 5 % and has been
dropped.

What did pay, for **2.2× at order 32 rising to 3.6× at order 256**:

* **The Hann window was recomputed every block**, at one `cos()` per sample of
  the fit window — about nine `cos()` calls per output sample at order 256. The
  fit always spans the whole sliding window, so the taper is identical every
  time; it is now built once in `configure()`. Bit-exact, since it is the same
  values from the same calls.
* **Every hot loop was a serial accumulator.** `for (k) s += a[k]*x[i-k]` is a
  chain of dependent additions, so it ran at one multiply-add per add latency —
  about four cycles — no matter what the machine could sustain. All six of them
  (signal autocorrelation, forward and backward residual, interpolation
  residual, its right-hand side, coefficient autocorrelation) now go through
  one `dot()` kernel with four independent accumulators and SSE2. Breaking the
  dependency chain is the larger half of the win; the vector width is the
  smaller.
  The forward and interpolation residuals walk the window backwards, so
  `fitModel()` also keeps `m_a` reversed in `m_arev`, which turns them into
  plain inner products.

Summation order therefore differs from a plain serial loop. The measured
consequence, over 60 s of a real transfer: **99.9965 % of output samples
bit-identical**, worst deviation **−156.5 dBFS** (an eighth of a 24-bit LSB),
rms deviation **193 dB below the signal**. Every ground-truth and
reference-transfer figure in this README is unchanged to the last digit printed.

One idea that is now *less* attractive than it looked: replacing the fit
autocorrelation with an FFT. Direct is `O(n × order)` against `O(n log n)`, but
at n = 2400 and order 256 the two land within about 10 % of each other once the
direct loop is vectorised — and the FFT would mean hand-rolling one, since the
component links nothing but COMCTL32, shared, KERNEL32 and USER32. Not worth
it.

#### What the data no longer supports

* **Long-term (pitch) prediction** was implemented and measured: **+1.53 dB**
  overall, +2.46 dB at 24–40 samples, and it never hurt. But a plain dense
  order-128 model gives **+3.07 dB at half the cost**, and beats LTP in every
  gap bucket. The reason is that an AR model of order ≥ T already represents
  periodicity — the median pitch lag selected was 225 samples, so an order-256
  model spans a whole period densely and does LTP's job more thoroughly. LTP is
  the *cheap* approximation, which is why it belongs in speech codecs, where a
  256-tap filter is unaffordable. Here it is not.
* **Sinusoidal modelling** for tonal passages. Still unimplemented, and the
  case for it is weaker than it looks: runs longer than 20 samples are 6.4 % of
  runs but **46.4 %** of the damage energy, so the long tail does matter — but
  order 256 already buys **+7.55 dB** there for far less engineering.

---

## Dehum parameters

Add *Dehum (line detection)* to the chain and press **Configure selected**.

| | |
| --- | --- |
| **Sensitivity** | How prominent a line has to be. 0 → 22 dB, 1 → 10 dB; the default 0.5 is 16 dB. Raise it if hum survives, lower it if music is being touched. |
| **Bandwidth** | Half width of each notch, 0.1–5 Hz. Wider catches a drifting line at the cost of more music around it. |
| **Search to** | Top of the range searched automatically, 40–500 Hz. The bottom is fixed at 16 Hz. |
| **Harmonics** | Multiples of each detected line to cancel as well, 1–8, default **1**. Locked to exact multiples of the fundamental, not tracked separately. Raise it for a genuine mains buzz; leave it alone otherwise, because a notch removes the coherent part at its frequency whether or not that part is hum — see below. |
| **Frequency** | 0 = detect automatically. Anything else pins the fundamental there and turns the detector off. **This is the control to use if you already know the frequency**, because it acts immediately where detection needs a few seconds. |
| **Rumble** | 4th-order Butterworth high-pass, default **40 Hz**, 0 = off. Broadband low-frequency noise is a *different defect* from hum — see below — and this is the control for it. 40 Hz is the cautious end: about 4 dB out of the 32–45 Hz band, nothing above 90 Hz touched. **60 Hz is what the rumbly reference transfer actually wants** (12.9 dB) and is the first thing to try if rumble survives. |
| **Dry/Wet** | 0 bypasses, bit-exactly. |

Latency is **zero**: the detector reads the signal but does not sit in the path,
so the component processes in place and needs no FIFO. Throughput is **159×
realtime** on x64 and 146× on x86, measured end to end over 177 s of 44.1 kHz
mono including file I/O. Memory is **2.0 MB per channel** at 44.1 kHz and 3.9 MB
at 96 kHz, fixed at `configure()` and independent of the parameters.

### The material this was calibrated on

Two 78 rpm transfers with audible hum, each paired with a **transfer of the same
performance from a different disc** that has none — the same ground-truth trick
the declicker uses. Measuring the pairs first turned out to matter, because it
showed that the two files do not have the same defect:

| ⅓-octave band | cachirulo m4a | its control | la tablada m4a | its control |
| --- | --- | --- | --- | --- |
| 32–45 Hz | **−41.0** | −59.0 | **−39.6** | −67.3 |
| 45–63 Hz | **−36.9** | −58.4 | −48.9 | −64.2 |
| 63–90 Hz | −32.0 | −42.5 | −43.4 | −56.0 |
| 125–250 Hz | −29.1 | −30.7 | −28.1 | −28.7 |

*La tablada* has a genuine hum: a single line at **41.3 Hz**, 38 dB above the
clean transfer at that bin and stable to ±0.02 Hz across the whole side.

*Cachirulo* is mostly a **different defect**. Its excess is smooth across
33–62 Hz — turntable rumble, not hum — and that is what dominates the sound.
There is also a faint coherent line at **38.94 Hz** (confirmed by a heterodyne
coherence probe: 0.51 against a local background of 0.14–0.27, and 0.16 at the
same frequency in its clean control), but it is worth almost nothing next to the
rumble:

| cachirulo, 32–45 Hz | level | gain |
| --- | --- | --- |
| original | −39.5 dB | |
| cancelling the 38.94 Hz line | −41.2 dB | 1.7 dB |
| **Rumble at 60 Hz** | **−52.4 dB** | **12.9 dB** |
| both | −53.4 dB | 13.9 dB |
| its clean control | −57.4 dB | |

Prominence cannot find that line and cannot be tuned into finding it. Its
amplitude sits at the level of the pedestal it stands on, so it is not prominent
at any baseline geometry — measured at four, including guard-banded ones, its
duty cycle above threshold is 0.0% in every case. That is what the
[coherence detector](#the-coherence-detector) exists for. **But note the size of
the prize: for this transfer, Rumble is the control that matters, not either
detector.**

Note also that neither line is at 50 or 60 Hz, and the two differ from each
other. A speed-corrected transfer moves whatever was on the disc along with the
music, so **a fixed-frequency notch is the wrong instrument here** and the
detector is not a convenience.

### Why detection is a duty cycle test, not a threshold

The obvious design — flag whatever stands out from the local spectrum — does not
work, and the reason is worth recording because it looks like it should.

Prominence measured against a ±20 Hz median baseline, over the whole of each
side:

| | at the hum line | loudest momentary peak anywhere in 16–150 Hz |
| --- | --- | --- |
| la tablada (hum) | median **19.0** dB, 5th pct 14.2 | 28.2 |
| la tablada (control) | median 0.1 dB | **21.7** |
| cachirulo (control) | — | **22.3** |

**The distributions overlap.** Catching the line continuously needs a threshold
around 14 dB; never firing on a hum-free control needs one above 22. There is no
value that does both, and an early revision that used one duly detected a
sustained bandoneón E4 at 329 Hz as hum — *in both transfers of the same piece* —
and cancelled it.

What separates them is not level but persistence. At a 16 dB threshold:

| | best duty cycle any frequency reaches | highest evidence score |
| --- | --- | --- |
| la tablada (hum) | **87.0%** at 41.05 Hz | **48** (saturated) |
| la tablada (control) | 10.2% | 16 |
| cachirulo (rumble) | 2.1% | 9 |
| cachirulo (control) | 3.3% | 13 |

So a candidate scores +1 per hop it is prominent, plus a quarter for each dB
past the threshold, and −2 per hop it is not. Anything below a two-thirds duty
cycle drifts to zero and stays there; the real line saturates. Cancellation
starts at 24, which sits in the gap with a factor of 1.5 on either side. At
1 up and 1 down instead, a 50% duty cycle is a random walk that reaches any
threshold eventually.

Two consequences worth knowing:

* **It takes about 5 seconds of a track to engage** — 1.5 s to fill the analysis
  window, then ~19 hops of evidence. That is the price of the test, and it is
  what **Frequency** is for.
* Once engaged, a line is held on much weaker evidence than it took to establish
  (6 dB lower, and a miss costs 0.25 instead of 2). Without that the line was
  acquired and dropped **ten times** over one side and removal was intermittent.
  Hum does not come and go; a gap in the evidence means the music got loud.

### The coherence detector

There is a second way in, for lines prominence cannot see. A magnitude spectrum
cannot tell a coherent tone at level X from noise at level X — only phase can —
so each candidate also gets two heterodyne integrators at the same frequency and
very different bandwidths, **0.15 Hz** and **3 Hz**. A continuous tone drives
both to the same complex amplitude; noise and separate note attacks arrive with
independent phases, so the narrow one averages them away. The ratio
`|w_narrow| / |w_wide|` is then a tonality measure that does not care how loud
the surroundings are. Its two extremes are analytic and are pinned in the tests:

| | pure tone | white noise |
| --- | --- | --- |
| measured | **0.993** | **0.222** |
| analytic | 1 | `sqrt(0.15/3)` = 0.224 |

Three things about it were not obvious, and each was found by being wrong first.

**The narrow bandwidth has to be much narrower than the notch.** A first attempt
used the notch's own 1 Hz, whose time constant is 0.16 s — shorter than a musical
note, so it tracked each note individually and scored a recurring note **0.92**,
indistinguishable from a real hum. At 0.15 Hz the time constant is 1.1 s and the
same note scores 0.41.

**The ratio has to be accumulated over tens of seconds.** Read over one hop it
returns 0.9 or better for everything on real transfers, hum-free ones included,
because turntable rumble is a slowly wandering narrowband process that is
perfectly tone-like when you only look at it for a fifth of a second. It stops
looking like one over twenty, and a hum does not. The sums therefore decay with a
20 s time constant rather than being reset per hop.

**It only works below about 80 Hz**, and that ceiling is not a tuning choice:

| | max coherence below 80 Hz | max 80–150 Hz |
| --- | --- | --- |
| la tablada (hum) | **0.571** | 0.394 |
| cachirulo (hum) | **0.475** | 0.621 ← *a bass note* |
| la tablada (control) | none | 0.378 |
| cachirulo (control) | none | 0.323 |

Below 80 Hz the separation is total: both hum transfers score high and neither
control has a single surviving coherent probe. Above it the order reverses — a
sustained B♮ at 123.5 Hz out-scores both real hums — because **a held musical
note is a coherent tone** and nothing computed from the signal can say otherwise.
Prominence still searches the whole range; only this second route is capped.

The measurement needs the frequency to about 0.15 Hz, far finer than a spectrum
can nominate, so this only became possible once the frequency tracker existed:
the nominee is parked on a probe, the tracker locks it, and the ratio is read at
the locked frequency. Candidates for it are the loudest local maxima of the low
band whether or not they are prominent — prominence decides nothing here, it only
suggests where to look.

**What it is worth, honestly.** It does what it was built to do: on the rumbly
reference it finds the 38.9 Hz line that prominence cannot, and it confirms
nothing at all on either hum-free control. But the line is so far under the
rumble that removing it moves that bin by **0.4 dB**. It will matter on material
where a buried line is stronger; it does not rescue this one, and Rumble is what
does.

### Why the window is 1.5 seconds

A coherent line's FFT peak grows with the window length while noise and music
grow with its square root, so prominence separates them 3 dB better per
doubling. Measured on the reference transfers:

| analysis window | the 41.3 Hz line | loudest music peak |
| --- | --- | --- |
| 0.37 s | 24.7 dB | 19–22 dB |
| 0.74 s | 26.8 dB | 19–22 dB |
| **1.49 s** | **28.2 dB** | 19–22 dB |

The line gains 3.5 dB, the music gains nothing. The same length gives the
0.67 Hz bins the notch has to be placed on. Hopping more often does *not* make
the detector decide sooner — successive frames overlap more, so they are that
much more correlated and the evidence counter has to be raised in step. That was
measured at a sixteenth of the window and bought nothing but FFTs.

### The notch, and why the tracker matters more than anything else

Each line is removed by heterodyning it to DC, lowpassing to recover its complex
amplitude, and subtracting it back:

```
z = x * exp(-i*theta)
w += lam * (z - w)                 lam = 2*pi*bandwidth/rate
y = x - 2*Re{w * exp(i*theta)}
```

`theta` is a deterministic phase ramp, so nothing here adapts on the signal:
it is a linear time-invariant notch that cannot ring or go unstable, and away
from the line the response is `|d|/sqrt(d² + bandwidth²)` — a partial 5 Hz away
loses **0.24 dB** at the default 1 Hz. Its depth is not infinite: heterodyning
also puts an image at −2f₀, and what the one-pole passes of that lands back on
f₀, so the floor is `bandwidth/(2·f0)`, i.e. −40 dB at 50 Hz. Measured at 0.3, 1
and 3 Hz the depths are −50.5, −40.0 and −30.5 dB, matching that to the decimal.

Because the notch is narrow, **placing it is the whole problem**. The detector's
own estimate is good to a few tenths of a hertz, and a few tenths is enough to
ruin it. So the frequency is refined from the rotation of `w`: an error `d`
makes it rotate at `d` Hz, so reading its phase advance measures `d` directly.

| starting error | without tracking | with tracking |
| --- | --- | --- |
| 0.2 Hz | −14.7 dB | — |
| 0.5 Hz | −7.2 dB | locks to 0.009 Hz, **−91.7 dB** |
| 1.2 Hz | −3.1 dB | locks to 0.003 Hz, **−38.4 dB** |

Two details that are not optional:

* **The interval has to be long.** A quarter second turns a 0.1 Hz error into
  0.157 radians. Done per block instead it is 0.004 radians, which is noise —
  and the first prototype duly random-walked the notch 2 Hz off the line and
  left the hum untouched.
* **It has to stop when there is nothing to read.** In a run-out groove the
  weight collapses to noise and the reading becomes a random walk again, so the
  weight is compared against its own recent peak and the tracker freezes below
  30% of it.

### What it does to the reference material

Median-over-time spectrum around the line, at the defaults:

| Hz | original | processed | pinned at 41.3 |
| --- | --- | --- | --- |
| 40.71 | −46.1 | −52.8 | −54.7 |
| 41.05 | −41.4 | −52.8 | −57.3 |
| **41.38** | **−39.6** | **−52.9** | **−57.1** |
| 41.72 | −42.9 | −51.9 | −53.3 |
| 42.06 | −47.4 | −52.1 | −52.9 |
| 43.40 | −55.1 | −56.3 | −56.4 |

The line is gone: what is left at 41.38 sits at the level of its neighbours.
It cannot go lower than that, because the rumble pedestal underneath it is at
about −52 — which is also why a naive "reduction at the line" figure tops out
around 13 dB no matter how good the cancellation is. Pinning the frequency does
slightly better than detecting it (the notch is exactly on the line from the
first sample) and dips below the pedestal.

On the **controls** — the hum-free transfers of the same two performances — the
detector confirms **no lines at all**, and with `Rumble` at 0 the output is
bit-identical to the input. That is the result that matters most: a restoration
tool that fires on clean material is worse than none. (At the default `Rumble` of
40 Hz the output is of course not bit-identical — the high-pass runs regardless
of what the detector decides.)

On **cachirulo**, whose defect is rumble rather than a prominent line, the
detector finds nothing and `Rumble` does the work:

| band | input | with Rumble 60 Hz | its clean control |
| --- | --- | --- | --- |
| 20–32 Hz | −52.1 | **−72.1** | −58.3 |
| 32–45 Hz | −39.5 | **−52.4** | −57.4 |
| 45–63 Hz | −36.3 | **−41.3** | −56.7 |
| 63–90 Hz | −33.2 | −33.9 | −43.7 |
| 90 Hz and up | unchanged | unchanged | |

### Harmonics you do not have cost music

**Harmonics** defaults to 1, and it used to default to 4. That was set while an
earlier design gated each harmonic by its own tonality measure; the gate was
removed and the default was not revisited, which is exactly the kind of thing a
measurement catches and reading the code does not.

Multiples of a low fundamental land in the musical register, and a notch there
removes the coherent part at its frequency whether or not that part is hum. On
the rumbly reference with two detected lines and 4 harmonics, six notches fell
between 80 and 200 Hz and took **84% of everything removed** with them — roughly
a tenth of the energy in that band — to gain 0.8 dB at the line. Dropping to one
harmonic changed the same file's removal profile completely:

| share of removed energy | 36–50 Hz | 50–80 Hz | 80–200 Hz |
| --- | --- | --- | --- |
| la tablada, 1 harmonic | **99.0%** | 0.2% | 0.5% |
| cachirulo, 4 harmonics | 4.2% | 7.5% | **84.1%** |
| cachirulo, 1 harmonic | 30.0% | 55.8% | 13.0% |

A real mains buzz does have harmonics and they are worth removing. An
off-frequency disc drone generally does not.

### Delta monitoring works for the notch, not for Rumble

`dehum_cli --delta` writes what was removed instead of what was kept, which is
the quickest way to hear whether a repair is taking music with it. That reading
is sound for the **notch** and misleading for **Rumble**, and the difference is
structural rather than a matter of degree.

What a notch removes really is confined to the line. Measured on the reference
transfer, the share of the removed energy by band:

| 36–42 Hz | 42–60 Hz | 60–200 Hz | 200 Hz–2 kHz | above 2 kHz |
| --- | --- | --- | --- | --- |
| **79.1%** | 11.1% | 7.1% | 0.32% | **0.00%** |

Rumble's delta, by contrast, contains the whole spectrum, and that is unavoidable
for any minimum-phase high-pass. What gets removed is `1 - H`, and at high
frequency `H → 1` does *not* make `1 - H` fall at the filter's own slope: for a
4th-order Butterworth the numerator's leading term is `a₃s³` against `s⁴` in the
denominator, so

```
|1 - H| ≈ a₃ · fc / f          a₃ = 2.6131
```

— **6 dB per octave whatever the order**. At a 60 Hz corner that is −16 dB at
1 kHz and −32 dB at 6 kHz, so the delta is full of music and sounds alarming.

It is phase, not amplitude. The output's magnitude response measured against its
input over a whole side:

| | 30 Hz | 60 Hz | 125 Hz | 250 Hz | 1 kHz | 6 kHz | 10 kHz |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Rumble at 60 Hz | −23.5 dB | −3.3 dB | −0.01 dB | **0.00** | **0.00** | **0.00** | **0.00** |

Nothing above 125 Hz is touched. **Judge Rumble by the result, not by its
delta**; judge the notch either way.

### What Dehum will not do

* **Nothing happens for the first few seconds of a track**, for the reason
  above. Use **Frequency** when the hum is known.
* **A hum whose fundamental is weak but whose harmonics are strong** — a buzz
  rather than a hum — will not be found by searching for the fundamental. Pin it
  with **Frequency** and raise **Harmonics**.
* **Detection is per channel.** On a stereo transfer where the hum is common to
  both, each channel finds it independently and they may engage a hop or two
  apart. Nothing has been measured about whether that is audible.
* **Coherence detection stops at 80 Hz**, because above it a sustained musical
  note is a coherent tone and scores higher than either real hum. A buried line
  above 80 Hz has to be pinned with **Frequency**.
* **Only tested on mono 78 rpm transfers**, like the declicker.

---

## DeCrackle parameters

Reachable through **Preferences → Playback → DSP Manager**; add *DeCrackle
(Airwindows)* to the active chain and press **Configure selected**.

| | |
| --- | --- |
| **Filter** | How dark the audio that replaces a click is, from bass-only to full range. Tune it to hide the transitions — full bass is not always the best setting. |
| **Window** | Width of the detection window, from very narrow to very wide. Also what the latency scales with. |
| **Thresld** | Lower catches more. Be careful about it triggering on actual music; that sounds bad. |
| **Surface** | 0 is off. Higher settings apply increasing treble filtering aimed at general surface noise in quiet passages. Not a plain lowpass — it responds to micro-crackle rather than to underlying high frequencies. |
| **Dry/Wet** | At exactly `0.000` this becomes **delta monitoring**: you hear only what is being removed. If music comes through, Thresld is too low. |

Chris Johnson's own description is in `Airwindopedia.txt` under *DeCrackle*.

Moving a slider takes effect immediately on the playing audio without
restarting the DSP (via `dsp_v3::apply_preset`), so there is no gap or click
while adjusting.

**This DSP is not zero latency.** It reports its group delay to foobar2000, so
visualisations stay in sync. At the default Window setting it is about 1.1 ms
at 44.1 kHz, growing to roughly 9 ms with Window at maximum.

---

## Performance

`decrackle_verify` reports this, best of five passes over 30 s of 44.1 kHz
stereo, Release, on a modern desktop:

| Build | | scalar | SSE2 | speedup |
| --- | --- | --- | --- | --- |
| x64 | defaults | 57.2 ms | 46.3 ms | 1.24× |
| x64 | Surface off | 45.9 ms | 34.5 ms | 1.33× |
| x86 | defaults | 90.9 ms | 80.2 ms | 1.13× |
| x86 | Surface off | 52.1 ms | 41.4 ms | 1.26× |

That is ~650× realtime for the default x64 build — 0.15 % of one core.
Memory is about 64 kB per stereo pair.

### Vectorization

The inner loop has three kinds of dependency, and only one of them is
exploitable:

* **Across samples** — fully recursive (six-pole IIRs, the `iirClick` attack
  and release ramps). Nothing to vectorize.
* **Across the six poles** — each pole feeds the next within the same sample.
  Also serial.
* **Across L and R** — completely independent apart from two cross-terms.
  **This is the 2-wide SSE2 opportunity**, and it is what `runStereoSSE2`
  exploits: L in the low lane, R in the high lane. The delay lines are already
  stored as interleaved `{l, r}` pairs, so each read is a single 16-byte load.

Because this is *lane parallelism* and not reassociation — same operations,
same order, same associativity, no FMA contraction — packed IEEE-754 doubles
round exactly like scalar ones. The harness asserts
`worst deviation vector vs. scalar path: 0.000e+00`, verified under
`/arch:SSE2`, `/arch:AVX` and `/arch:AVX2`.

Two things stay scalar because they genuinely are serial: the rectified
control band (one value derived from `L*R`, then six serial poles) and
`sin()`, of which there are two per sample when Surface is engaged. MSVC does
not ship a packed `sin`, and a polynomial approximation would forfeit
bit-exactness for a couple of percent — not a trade worth making. That is why
the Surface-off column shows the larger speedup.

**AVX and AVX2 buy nothing here.** A 256-bit register holds four doubles but
there are only two lanes of real parallelism, and the sample-to-sample
recursion rules out processing two samples at once. Measured, AVX2 lands
within noise of SSE2. FMA would change results (single rounding instead of
two) and break the bit-exactness guarantee for no measurable gain. `SSE2` is
therefore the default and there is no reason to change it.

### Other differences from a straight transcription of the VST

* **Coefficients are computed once per parameter change**, not once per buffer.
  The VST does ten `pow()` calls per `processReplacing` call.
* **`pow(x, 3.0)` becomes `x*x*x`** in the click detector, twice per sample.
* **`pow(2, expon + 62)` and `frexpf` in the dither** become bit manipulation
  on the float's exponent field. Exact, no libm call.
* **The Surface block is skipped entirely when Surface is 0**, which is where
  the two `sin()` calls per sample live. The VST computes them regardless and
  then discards the result.
* **The L and R delay lines are interleaved**, halving the number of cache
  lines the inner loop touches — they are always read at identical indices.
* **Dead code removed**: `prevSampleL/R` and `prevSurfaceL/R` are computed by
  the VST but never used.
* **Flush-to-zero is enabled** for the duration of each chunk (FTZ only; DAZ
  lives in a bit that early SSE2 parts treat as reserved).

None of these change the output — see below.

`decrackle_core.cpp` keeps a plain scalar implementation alongside the SSE2
one. It is the portable fallback, it is what the single-channel path uses, and
the test harness runs both against the Airwindows source so neither can drift.

---

## Verification

```bash
ctest --test-dir build/x64 -C Release --output-on-failure
```

**`decrackle_verify`** compares the port against
`tests/decrackle_reference.h`, a verbatim copy of the Airwindows VST source
used as an oracle. Across 12 parameter/sample-rate combinations × 5 signal
types, fed in 1024-sample chunks so buffer boundaries are exercised, the worst
deviation is **0.000e+00** — bit-identical, on both x86 and x64, for both the
SSE2 and the scalar path. It also:

* feeds NaN, ±infinity, `1e30` and denormals in and checks the output stays
  finite and that clean audio afterwards comes back clean;
* asserts the SSE2 and scalar paths agree to the bit;
* sweeps every parameter across 21 steps × 14 sample rates from 1 kHz to
  20 MHz, checking buffer indices stay in bounds and nothing diverges;
* checks the single-channel path against a duplicated-stereo run;
* reports throughput.

**`declick_verify`** checks the declicker's linear algebra against dense brute
force — this is the part where a subtle error yields plausible but wrong audio
rather than an obvious failure. `levinson()` against a Gaussian-elimination
solve of the Yule–Walker system (worst deviation **5.6e-16**),
`solveBandedToeplitz()` against dense elimination (**3.8e-15**), and
`bandedInverseDiagonal()` against a full dense inverse (**1.6e-15**), over
random positive-definite systems across the whole order and run-length range.
The single-missing-sample posterior variance is pinned separately against its
closed form `1/Σaₖ²`, which anchors the scale of the rest. It also covers
streaming properties end to end: silence in, silence out; clean audio passes
through untouched; injected clicks come out 26 dB smaller; NaN, ±infinity and
denormals produce nothing non-finite and the stream recovers afterwards;
dry/wet 0 is a bit-exact bypass; and the latency contract holds in both
directions (no output before `latency` samples are fed, output immediately
after).

**`declick_rt_verify`** counts heap allocations on the processing path and
requires zero — see [Real-time safety](#real-time-safety) for what it caught.
It also records the one documented exception (a push larger than
`Config::maxBlock`) as a positive assertion rather than leaving it implicit, and
prints the per-channel footprint so a regression in that shows up in the log.

**`declick_vst_verify`** holds the WinVST port to the same maths — see
[Sharing a core with the other plug-in formats](#sharing-a-core-with-the-other-plug-in-formats).
The central check drives `declick::Channel` directly with the same `Config` and
the same per-sample push/pull; the plug-in's `processDoubleReplacing` output is
**bit-identical**, worst deviation **0.000e+00**, on both x86 and x64. If that
ever stops being true, one of the two wrappers has grown DSP of its own. It also
covers the parts a VST has to get right on its own: that block patterns from
`1/1/1/1` to `1024/64/4096/1` all yield the *same* stream, which is what proves
the pre-roll arithmetic (get it wrong and the core zero-fills mid-stream); that
the declared latency is the real one in both directions; that a Sensitivity move
retunes without renegotiating latency or leaving a gap while a Model order move
does the opposite; dry/wet 0 as a bit-exact bypass; `resume()` starting from
silence rather than the previous take; hostile input; the slider-to-core
mappings; and the preset chunk, including pinning out-of-range stored values.
Built against `tests/vst2_stub`, so it needs no SDK — with the caveat noted
above about what that does not establish.

**`dehum_verify`** checks the dehummer against independent references rather
than against itself. The tonality ratio is pinned at both of its analytic
extremes — 1 for a pure tone, `sqrt(narrow/wide)` for noise — which catches a
wrong coefficient, a swapped pair or an accumulator measuring the wrong thing;
measured 0.993 and 0.222 against 1 and 0.224. The hand-rolled FFT is pinned by requiring tones placed on
known bin centres to be detected there to within 0.15 Hz — a transposed
butterfly, a wrong twiddle sign or a bad real-input unpack all move the peak. The
notch depth is checked against its *analytic* floor `bandwidth/(2·f0)` rather
than an arbitrary threshold, so it fails if the lowpass coefficient is wrong in
either direction; measured −50.5, −40.0 and −30.5 dB at 0.3, 1 and 3 Hz against
predictions of −50.5, −40.0 and −30.5. The frequency tracker is driven from four
starting offsets up to 1.2 Hz and required to converge within 0.1 Hz. The rumble
filter is compared with the closed-form Butterworth response at five
frequencies, to within 1.5 dB. It also covers: detection and removal end to end
on synthetic hum, with the removed signal required to be **100% the line
itself**; clean material with recurring notes, where nothing may be detected and
the output must come back bit-identical; dry/wet 0 as a bit-exact bypass; the
same output for block sizes from 1 to 65536; NaN, ±infinity, `1e30` and denormals
in with finite audio out and full recovery afterwards; six sample rates from
8 kHz to 192 kHz; and that every parameter move can be retuned live while a
sample rate change cannot.

**`dehum_rt_verify`** counts heap allocations on the processing path and requires
zero — a sharper question here than for the declicker, because a whole
FFT-based detector runs on the audio thread. It covers 37 s of steady
processing, a 65536-sample block, 4096 single-sample calls, 21 live parameter
changes, `flush()`, `reset()` and manual mode, and prints the footprint before
and after so a regression shows up in the log. All zero, 2073 kB either side.

**`dehum_vst_verify`** holds the WinVST dehummer to the same maths — see
[Sharing a core with the other plug-in formats](#sharing-a-core-with-the-other-plug-in-formats).
Its central check drives `dehum::Channel` with the same `Config` and requires the
plug-in's `processDoubleReplacing` output to be **bit-identical**, worst deviation
**0.000e+00**. It also covers what this wrapper has to get right on its own: that
block sizes from 1 to 4096 — including 1025, one past the scratch buffer it
chunks through — all give the *same* stream; that the declared latency is zero
and that **nothing** renegotiates it, for any parameter or across a sample rate
change; that all seven sliders retune live without a gap; that `resume()` keeps
what the detector learned rather than re-acquiring; that the float path is the
double path plus dither to within a float ULP; hostile input; dry/wet 0 as a
bit-exact bypass; the slider mappings against `Params::defaults()`, including the
off positions on Freq and Rumble; and the preset chunk.

**`preset_roundtrip`** saves each component's parameters to a `dsp_preset` and
reads them back, checking every field individually with values chosen so that a
field read out of position cannot pass by accident. It also covers the
version-1 declick layout (written before **Repair depth** existed, still
loadable, depth falls back to its default), a preset owned by a different GUID,
and a truncated payload. This test exists because `depth` was once written by
`make()` and skipped by `parse()`, which silently shifted every field after it
— saved chains came back with **Dry/Wet at 0**, i.e. bypassed. A dialog that
looks right and a DSP that runs will not catch that; only a save/load cycle
will. Like the smoke test it needs `shared.dll` and skips itself when no
matching-architecture foobar2000 is installed; the logic under test is
architecture-independent, so one architecture is enough.

**`component_smoke`** loads the built DLL exactly as foobar2000 does — through
`foobar2000_get_interface()` and the service factory list — then registers the
DSP, instantiates it from a preset and pushes audio through it: format changes
mid-stream (stereo → mono → 5.1 → 192 kHz), `flush()`, live preset changes, and
truncated/empty stored presets. It also opens the configuration dialog for
real, moves a slider, and checks that the live update fires and that Cancel
restores the original preset. It skips itself if no matching-architecture
foobar2000 is installed (it needs `shared.dll`); set `FOOBAR2000_DIR` to point
at one.

To eyeball the dialog without installing anything:

```bash
build\x64\tests\Release\component_smoke.exe ^
  build\x64\foo_dsp_decrackle\Release\foo_dsp_decrackle.dll ^
  --screenshot dialog.bmp
```

### Deliberate behavioural differences

Two places where the port does not follow the VST, both to avoid producing
garbage:

1. **Filter coefficients are clamped to ≤ 1.0.** `filterOut` and `filterRef`
   are divided by `sampleRate / 44100`, so below roughly 22.7 kHz they exceed 1
   and the recursion diverges to infinity. The VST has the same flaw; it just
   never gets handed a 22 kHz file. At 44.1 kHz and above the unclamped values
   never exceed 0.52, so the clamp cannot alter normal playback.
2. **Input is sanitised.** Samples that are NaN, infinite or above `1e30` are
   replaced with silence before they can lodge themselves in the recursive
   state. Real audio is untouched. Delay-line indices are additionally clamped
   into range, which only ever matters at absurd sample-rate/Window
   combinations.

The dither generator is also seeded with fixed constants instead of the VST's
`rand()`, so the same file renders to the same bits every time — which matters
for a player that can also be used as a converter.

---

## How multichannel is handled

DeCrackle's click detector correlates left against right, so channels are fed
to it in their natural stereo pairs (front L/R, side L/R, back L/R, and so on),
read out of the chunk's channel map. Channels with no partner — centre, LFE,
back centre — go through a degenerate single-channel path where the cross
detector collapses to `x²`. A chunk with an unrecognised channel map falls back
to pairing in interleave order.

---

## Layout

```
CMakeLists.txt                    top level: options, toolchain flags
cmake/
  fb2k_download_sdk.cmake         SDK download + unpack, also runnable with -P
  fb2k_sdk.cmake                  builds pfc + SDK + component client
  fb2k_find_runtime.cmake         locates an installed foobar2000 for the smoke test
scripts/
  build_release.ps1               the release build + packaging entry point
  check_win7.ps1                  reads a built DLL's PE headers and imports for Windows 7 compatibility
  get_sdk.ps1                     wrapper around fb2k_download_sdk.cmake
  sync_cores.ps1                  mirrors the cores out to the other plug-in formats
foo_dsp_decrackle/
  decrackle_core.{h,cpp}          the DSP (scalar + SSE2); no foobar2000 or Win32 dependency
  dsp_decrackle.cpp               the foobar2000 DSP service
  decrackle_preset.{h,cpp}        preset serialisation
  config_dialog.cpp               plain Win32 configuration dialog
  component.cpp                   DECLARE_COMPONENT_VERSION
  foo_dsp_decrackle.rc            dialog template + version resource
foo_dsp_declick/                  same layout, declick_core.{h,cpp} etc.
foo_dsp_dehum/                    same layout, dehum_core.{h,cpp} etc.
tools/
  decrackle_cli.cpp               offline WAV in / WAV out, for sweeps
  declick_cli.cpp                 ditto for the declicker
  dehum_cli.cpp                   ditto for the dehummer; also reports the lines
                                  it found and where the tracker put them
  wav_io.h                        minimal WAV reader/writer
tests/
  decrackle_reference.h           verbatim Airwindows VST source, used as an oracle
  decrackle_verify.cpp            correctness, robustness, throughput
  declick_verify.cpp              AR linear algebra vs. dense brute force
  declick_rt_verify.cpp           counts audio-thread allocations, requires zero
  declick_vst_verify.cpp          the WinVST port vs. the core it shares
  dehum_verify.cpp                FFT, notch, tracker and detector vs. references
  dehum_rt_verify.cpp             ditto for the dehummer's audio thread
  dehum_vst_verify.cpp            the WinVST dehummer vs. the core it shares
  vst2_stub/audioeffectx.h        our own minimal VST 2.4 declarations, so the above
                                  builds without Steinberg's SDK
  preset_roundtrip.cpp            parameters survive save/load, both components
  component_smoke.cpp             loads a DLL through the real SDK plumbing
external/                         the downloaded SDK (git-ignored)
dist/                             release artefacts (git-ignored)
```

`decrackle_core.{h,cpp}` deliberately knows nothing about foobar2000, VST or
Win32, which is what lets the test harness compare it against the original
source directly.

---

## Sharing a core with the other plug-in formats

Two of the cores have a second consumer: **`plugins/WinVST/Declick`** and
**`plugins/WinVST/Dehum`**, VST2 builds of the same algorithms. They compile
`declick_core.{h,cpp}` and `dehum_core.{h,cpp}` — not reimplementations of them,
and not translations. There is exactly one copy of each piece of maths in this
repository that anything is allowed to diverge from, and it is the one under
`foo_dsp_declick/` or `foo_dsp_dehum/`.

The VST folder holds a **byte-identical copy** rather than reaching across the
tree for this one. That is not laziness: an Airwindows WinVST folder has to
stand on its own, because the build is "drag the plug-in's files into
VSTProject and press build" (`plugins/AirwindowsWinVSTTemplate.txt`) and the
folder that gets committed is the folder that was dragged. A `..\..\` include
path would break the moment anyone followed those instructions.

So the copies are mechanical and checked:

```powershell
.\scripts\sync_cores.ps1          # push the canonical core out to every format
.\scripts\sync_cores.ps1 -Check   # compare only, non-zero exit on any drift
```

`build_release.ps1` runs `-Check` before it configures anything, so a component
whose maths no longer matches the VST's cannot be packaged. Adding a format, or a
core, is one entry in `$mirrors`.

**Edit the copy in `foo_dsp_declick/`, never a mirror.** A mirror edit is not
merged, it is overwritten.

### What the Declick VST wrapper adds, and why none of it is in the core

| | |
| --- | --- |
| **Pre-roll** | A VST must return n samples for every n it is given, and the core holds `config().latency` samples of lookahead. `Channel::prime()` feeds it that many zeros up front, after which `available() >= n` holds for *any* block size, so the wrapper is a plain one-in-one-out loop with no FIFO of its own and no risk of the core zero-filling mid-stream. Those zeros are the reported delay. |
| **`setInitialDelay` / `getGetTailSize`** | foobar2000 is told the latency through `get_latency()` and flushes with `on_endofplayback()`. A VST needs the equivalent two, and `ioChanged()` when Max repair or Model order changes it. |
| **`Channel::retune()`** | foobar2000 has no automation, so rebuilding the pipeline on a preset change is acceptable there. A DAW moves sliders while audio runs, and `configure()` reallocates, which resets. `retune()` swaps in a config that needs the same buffers — everything except Max repair and Model order — with no discontinuity. |
| **Dither** | Airwindows house style, on the 32-bit float path only. |

### The Dehum VST wrapper is shorter, and mostly by subtraction

Nearly everything on the list above is a consequence of Declick's lookahead.
Dehum has none — the detector reads the signal but does not sit in the path — so
its wrapper needs none of it:

| | |
| --- | --- |
| **No pre-roll, no FIFO** | The core works in place and returns what it was given, so the wrapper is a copy, a `process()` call and a dither. |
| **No latency to declare** | `setInitialDelay(0)` once in the constructor and nothing after it. `dehum_vst_verify` asserts `ioChanged()` is **never** called, for any parameter and across a sample rate change — the exact opposite of what it asserts for Declick. |
| **No tail** | Nothing to keep pulling for at the end of an offline bounce, so no `getGetTailSize()`. |
| **Every parameter retunes live** | Only the sample rate sizes anything in this core, so all seven sliders move without a rebuild, a reset or a gap. Declick can only manage five of its seven. |
| **A scratch buffer** | The one thing it adds. The DSP is done in double while the float path dithers on the way out, so the doubles need somewhere to live: a fixed 1024-sample member, chunked, because the audio thread must not allocate. Splitting a buffer across chunks is undetectable to the core, which is what the block size checks establish — including a deliberate 1025, one past the scratch. |
| **`resume()` flushes rather than resets** | The analysis window is stale across a transport jump and the integrators hold a discontinuity, but the hum on the far side is the same hum. Forgetting the lines would cost several seconds of it every time the user hit play; the test measures the hum still 34.9 dB down within 2 s of `resume()`. |

The one thing that moved *into* the core is the flush-to-zero guard, which used
to be a local class in `dsp_declick.cpp`. FTZ changes results in the last bits,
so it is part of the numerical contract rather than an optimisation, and two
ports that disagree about it are not comparable. It is now
`declick::scoped_flush_denormals` and both wrappers hold one.

### Checking that they agree

`processDoubleReplacing` is left undithered — that is the standard Airwindows
arrangement, and it also makes it the path to compare. **`declick_vst_verify`**
and **`dehum_vst_verify`** drive `declick::Channel` and `dehum::Channel`
directly with the same `Config`, and require each VST's 64-bit output to match to
the bit. Both report **0.000e+00**. They run on every build, on both
architectures, and need no SDK. See [Verification](#verification).

Steinberg's `vst2.x` sources are not redistributable and are not here, so the
VST itself cannot be built from a clean checkout — see
`plugins/AirwindowsWinVSTTemplate.txt`. `tests/vst2_stub/audioeffectx.h` stands
in for the parts of the API the Airwindows pattern touches, which is enough to
run the DSP but **not** enough to prove the plug-in compiles in the real
VSTProject. That still takes a build there.

---

## Known limitations

* **Dehum needs a few seconds of a track before it acts**, its detection is per
  channel, and a buzz whose fundamental is weak will not be found by searching
  for the fundamental — see [What Dehum will not do](#what-dehum-will-not-do).
* **No dark mode.** The configuration dialogs are plain Win32. Following
  foobar2000 2.x's dark mode means `fb2k::CDarkModeHooks`, which pulls in
  libPPUI and WTL — WTL is not in the SDK archive and would have to be
  downloaded separately. The dialogs were kept dependency-free instead.
* **DeCrackle does not flush its tail.** Like the VST, the last few
  milliseconds sitting in its delay line at end of playback are not emitted.
  Flushing them would add samples to the stream and break gapless playback.
  (Declick does flush, so its stream length is preserved.)
* **Declick's C++ core is not a line-by-line port of the Python prototype** in
  `scratchpad/tune/`. It is an independent implementation of the same method
  with a different block and noise-estimate structure; the two agree on roughly
  the same clicks but not sample for sample. The C++ version is the one that
  was calibrated, and the figures above are its own.
* **Declick has not been evaluated on stereo vinyl**, only on mono 78s. It
  should work — the detector is per-channel and format-agnostic — but the
  thresholds were tuned on shellac.
* **Declick's audio thread can still allocate in one case:** a caller pushing
  more than `Config::maxBlock` — 16384 samples — between pulls. That grows the
  output ring once, and then never again. Neither wrapper comes near it.
  Everything else is reserved in `configure()`; see
  [Real-time safety](#real-time-safety).
* **A format change rebuilds Declick's channels from `on_chunk`.** A different
  channel count or sample rate mid-stream constructs new `Channel` objects,
  which allocates, on whatever thread foobar2000 called it from. It happens at
  track boundaries rather than during steady playback, and foobar2000's own
  `insert_chunk()` allocates on every chunk regardless, so the DSP is not the
  binding constraint there. The VST has no equivalent path: it reconfigures in
  place.
* **ARM64EC is wired up in CMake but untested.**

---

## Licence

The DeCrackle algorithm is © Chris Johnson / Airwindows, MIT licensed — see
`LICENSE` at the repository root and <https://www.airwindows.com/>. The
foobar2000 SDK is covered by its own licence, included in the downloaded
archive as `sdk-license.txt`; it is not redistributed here.
