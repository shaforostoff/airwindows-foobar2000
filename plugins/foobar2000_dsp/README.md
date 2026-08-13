# foobar2000 DSP components

Two DSPs for restoring 78s and vinyl transfers:

| Component | What it is |
| --- | --- |
| **foo_dsp_decrackle** | A port of the Airwindows **DeCrackle** plug-in, verified **bit-identical** to the VST source. Best on stereo material. |
| **foo_dsp_declick** | An autoregressive detect-and-interpolate declicker. **Use this one for mono shellac** — see [Which one to use](#which-one-to-use). |

Both:

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
| **Model order** | Higher follows complex material more closely at some CPU cost. 32 suits most 78s. See [Does more CPU help?](#does-more-cpu-help) — mostly it does not. |
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

Throughput: ~80× realtime at the default, measured end to end over 60 s of
44.1 kHz mono including file I/O — about 1 % of one core on a modern desktop.
Latency ~18 ms, reported to foobar2000 so visualisations stay in sync. Note
that `config().latency` is a buffering delay, not a shift: that many samples
must go in before the first block comes out, but the emitted stream is aligned
with the input.

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
* **Reconstruction is.** AR interpolation manages ~30 dB SNR over a 3-sample
  hole, 22 dB over 6, and only 8 dB over 16 — while the click itself sits about
  18 dB below the music. Past roughly 6 samples, replacing the samples was
  *worse than leaving the click alone*.

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

Barely, on this axis. Reconstruction SNR (dB) for a hole of N samples:

| model | 3 | 6 | 10 | 16 | 24 | 40 |
| --- | --- | --- | --- | --- | --- | --- |
| order 32, ctx ±96 (default) | 30.0 | 22.1 | 14.3 | 8.5 | 5.6 | 4.0 |
| order 64, ctx ±192 | 29.8 | 22.4 | 16.4 | 9.8 | 6.6 | 5.4 |
| order 128, ctx ±512 | 28.8 | 22.2 | 16.9 | 10.2 | 8.5 | 7.0 |

Quadrupling the model order buys **nothing** at short gaps (it is fractionally
worse), about **+3 dB** at 10–24 samples, and does not move the break-even
point far enough to matter: end to end with perfect detection, every model
above preferred the same 8-sample cap and scored within 0.03 dB of the others.
Context beyond 3× the model order buys *literally zero* — the identical rows
for ctx ±96 vs ±256 and ±192 vs ±512 are not a copy-paste error.

So there is no point spending CPU on a bigger AR model. What the data suggests
would actually help, none of which is implemented:

* **Long-term (pitch) prediction.** Music is quasi-periodic; bridging a long
  gap from the previous pitch period is the standard way to do it, and plain
  short-term AR cannot.
* **Subtractive repair** for longer damage — estimating the impulse and
  subtracting it, rather than discarding the samples. The blend curve is a
  crude approximation of this.
* **Sinusoidal modelling** for tonal passages.

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
foo_dsp_decrackle/
  decrackle_core.{h,cpp}          the DSP (scalar + SSE2); no foobar2000 or Win32 dependency
  dsp_decrackle.cpp               the foobar2000 DSP service
  decrackle_preset.{h,cpp}        preset serialisation
  config_dialog.cpp               plain Win32 configuration dialog
  component.cpp                   DECLARE_COMPONENT_VERSION
  foo_dsp_decrackle.rc            dialog template + version resource
foo_dsp_declick/                  same layout, declick_core.{h,cpp} etc.
tools/
  decrackle_cli.cpp               offline WAV in / WAV out, for sweeps
  declick_cli.cpp                 ditto for the declicker
  wav_io.h                        minimal WAV reader/writer
tests/
  decrackle_reference.h           verbatim Airwindows VST source, used as an oracle
  decrackle_verify.cpp            correctness, robustness, throughput
  declick_verify.cpp              AR linear algebra vs. dense brute force
  preset_roundtrip.cpp            parameters survive save/load, both components
  component_smoke.cpp             loads a DLL through the real SDK plumbing
external/                         the downloaded SDK (git-ignored)
dist/                             release artefacts (git-ignored)
```

`decrackle_core.{h,cpp}` deliberately knows nothing about foobar2000, VST or
Win32, which is what lets the test harness compare it against the original
source directly.

---

## Known limitations

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
* **ARM64EC is wired up in CMake but untested.**

---

## Licence

The DeCrackle algorithm is © Chris Johnson / Airwindows, MIT licensed — see
`LICENSE` at the repository root and <https://www.airwindows.com/>. The
foobar2000 SDK is covered by its own licence, included in the downloaded
archive as `sdk-license.txt`; it is not redistributed here.
