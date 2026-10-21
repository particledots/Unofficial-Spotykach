# SPEC.md: Spotykach (unofficial), Milestone 0

Status: Milestone 0 (feasibility, parameters, panel). No plug-in code exists yet.
Date: 2026-10-20.

Evidence tags used below:
- **[read]** I read it in the source, manual or licence file.
- **[ran]** I ran it in a sandbox (Linux x86-64, not a Mac).
- **[not read]** I did not examine it. Treat anything touching it as unknown.
- **[unverified]** General knowledge or inference that I did not check this session.

---

## 1. What was examined

| Item | Version |
|---|---|
| Upstream firmware | github.com/Synthux-Academy/spotykach, branch `main`, commit `7da80618c7e051189c575e766ff40594e622e910` (cloned 2026-10-20) |
| DaisySP (pinned submodule) | `599511b740f8f3a9b8db72a0642aa45b8a23c3a3` (electro-smith) |
| libDaisy (pinned submodule) | `dcd20e135f8ed21757711576b398d6a4c6526fba` (bleeptools fork) |
| Manual | "Spotykach Manual A4 Print Ready.pdf" (firmware 1.0, 2026), in the Project |
| Your Beads repo (for framework and CI patterns) | github.com/particledots/beads-plugin at `6017235` |

**Not read:** the online manual (the printed manual defers to it for routing diagrams, quantisation diagrams and alternative effects); `src/hw/*` except the buffer pool; `src/memory/*` except `settings.h` and the slot count; the internals of `generator.cpp`, `vox.cpp`, `track.cpp`, `dispatcher.h`; all LED code. Every statement about what those do is unknown.

---

## 2. Licence and attribution

### 2.1 Upstream licence: verified [read]

`LICENSE` is the MIT licence. Copyright line, verbatim:

> Copyright (c) 2026 Synthux Academy

This matches your notes. **But it is not the only copyright holder in the repository.** The files carry other notices:

| Notice | Where | Needed by a plug-in? |
|---|---|---|
| `Copyright 2024 Infrasonic Audio LLC` (MIT text) | `src/common.h`, `src/ui/voct.h`, `src/ui/color.{h,cpp}`, `src/hw/ws2812.{h,cpp}`, `src/hw/sr_165.h` | `common.h`: yes (every core file includes it). The others: no. |
| `Copyright 2026 bleeptools` (MIT text) | `src/core/hann.h`, `src/core/synclock.h` | Yes (`hann.h`). `synclock.h`: see 2.3. |
| `Copyright (c) 2020 Electrosmith, Corp.` | `src/hw/sr_165.h` | No (hardware). |
| "Derived from Nick Donaldson (Infrasonic Audio)", no licence text | `src/core/fx/echo.h` | Yes. |
| `infrasonic` namespace, no notice at all | `src/core/fx/biquad.h` | Yes. |
| "Derived from Simple Delay line", no notice | `src/core/fx/deline.h` | Yes. It is derived from DaisySP's `delayline.h` (MIT, Electrosmith 2020) [read]; the header was lost upstream, so I will restore it. |
| "Derived from Maximum MIDI Programmer's ToolKit ©1993-1998 Paul Messick", **no licence text anywhere in the repo** | `src/core/synclock.cpp` | See 2.3. |

`CREDITS.md` [read] lists: Nick Donaldson (electronics design, hardware drivers, bootloader), Rosa Schuurmans (FX and LEDs), Roey Tsemah (concept, visual and hardware design), Vlad Lytvynenko (concept, firmware design).

Spelling conflict: the printed manual says "Vlad Litvinenko"; `CREDITS.md` says "Vlad Lytvynenko". I will use the repo's spelling until someone confirms otherwise.

### 2.2 Dependencies [read]

- DaisySP `Source/` files used by the core are MIT. Some carry "Electrosmith, Corp, Emilie Gillet" notices (also MIT). The firmware uses these DaisySP symbols: `fmap fclamp pow10f fastlog10f fmax fmin fonepole SoftClip SoftLimit Oscillator WhiteNoise Overdrive Decimator SampleRateReducer Adsr`.
- DaisySP also has an LGPL folder (`DaisySP-LGPL`), compiled only if `USE_DAISYSP_LGPL` is defined. The firmware repo never defines it (grep of Makefile, src, app.cpp found nothing). **Do not enable it.**
- libDaisy is MIT. The plug-in will not use it (only a handful of symbols are stubbed; see section 4).

### 2.3 Provenance risk for a public release

`SynClock` (the tempo/tick engine) says it is derived from Paul Messick's 1993-98 toolkit and "modified". The repo contains no licence text for that lineage, and I do not know what licence the original carries. **Recommendation: do not ship `synclock.cpp`.** The plug-in does not need it: it can generate its own 48-PPQN ticks from the host tempo or an internal BPM with new code. This removes the only provenance gap I cannot resolve. This is recorded as decision D6.

### 2.4 Licence of the plug-in as a whole

**AGPLv3 for the combined plug-in binary and its source.** Why:

1. JUCE's modules are dual-licensed AGPLv3 or a commercial JUCE licence [read: `LICENSE.md` at tags 8.0.6 and 9.0.4]. Distributing a JUCE-based plug-in without a commercial JUCE licence means the whole combined work is AGPLv3.
2. MIT code (Synthux, Infrasonic, bleeptools, Electrosmith/Gillet) may be included in an AGPLv3 combined work. The MIT notices must travel with the files [general knowledge, not legal advice].
3. The bundled Apache-2.0 AudioUnit SDK is compatible with GPLv3/AGPLv3 [unverified beyond JUCE listing it as Apache-2.0]. JUCE 8.0.6 lists its bundled VST3 SDK as "Proprietary Steinberg VST3 License/GPLv3", so the GPLv3 option applies under AGPLv3. JUCE 9.0.4's dependency inventory lists VST3 SDK 3.8.0 as MIT [read].

Practical consequences:
- Upstream MIT files stay in a clearly separate folder (`third_party/spotykach/`) with their original headers and the upstream `LICENSE` text beside them. Files I modify keep their original notices; changes are marked.
- A `THIRD-PARTY-LICENSES.txt` goes in the repo and in every download (as your Beads build does).
- Your own new code (wrapper, UI) is yours. Because it links JUCE, the distributed combination is AGPLv3. You may still dual-license your own parts later; I will not decide that for you.
- If you ever want closed-source distribution you would need a commercial JUCE licence. **I did not verify the current commercial tiers or terms** (my search returned only links; I did not read the EULA). Check juce.com before any such decision. I am not a lawyer.
- Public binaries also raise Apple signing/notarisation (ad-hoc signing, as in Beads, forces users to clear quarantine). Not needed for now; flagged in decisions.md (Q3).

### 2.5 Naming and credit

- Product name: **Unofficial Spotykach**. README and About screen state it is not an official Synthux Academy product and not affiliated with Synthux Academy.
- README and About credit Synthux Academy, Roey Tsemah, Vlad Lytvynenko, Nick Donaldson, Rosa Schuurmans, Infrasonic Audio LLC (code), Electrosmith (DaisySP), and the DaisySP-embedded Emilie Gillet code.
- The About screen shows the version number.

---

## 3. Your architecture notes versus the source

| Your note | Verdict | Detail |
|---|---|---|
| `src/core` is the DSP engine; `Core::process` takes `float**` buffers | **Mostly right** | Signature is `process(const float* const* in, float** out, size_t size)` (`core.h:59`). Input is const; always reads two input channels. |
| `src/hw` is Daisy hardware, "replace, don't port" | **Right, with one exception** | The core includes `hw/buffer.sdram.h` (`core.cpp:4`), which pulls in `card.h` and FatFS headers. So the *buffer pool* must be replaced specifically, not just the pads and ADCs. |
| `src/ui` holds the knob/pad/gesture mapping | **Right** | Plus: it also holds all default parameter values (`_init_values`, `core.ui.cpp:68-103`), the clock tick call, mode-from-switch logic, pitch snapping, and the pick-up logic. The core has no usable defaults on its own. |
| StopwatchTimer in `driver.h` | **Confirmed** | `driver.h:112`; used for the 1000 ms external-clock timeout and a 4 ms reset window (`driver.cpp:75,137`). |
| `System::GetNow` in `tempo.cpp` | **Confirmed** | `tempo.cpp:17` (tap tempo). Also `core.ui.cpp:549` (UI). |
| SDRAM buffer pool in `core.cpp` | **Confirmed** | `core.cpp:25-43`, backed by global arrays in `hw/buffer.sdram.cpp`. |
| "TIM5 timer-driven `prepare()`/clock" | **Half right** | TIM5 (`app.cpp:68-91,130`) runs `process_gate_in()`, `Core::prepare()` and LED rendering. The **clock tick is not on TIM5**: `Driver::tick()` is called from `CoreUI::tick()` (`core.ui.cpp:601-613`), which `ProcessAudio` calls once per audio block (`app.cpp:192`). At 96 samples / 48 kHz that is one tick per 2 ms, matching `_clock.init(2000, kPPQNIntern)` (`driver.cpp:32`). TIM5's effective rate comes from a code comment (500 Hz); I did not verify the STM32 clock tree. |
| "Hardcoded 48 kHz / 96-sample block" | **Right for 48 kHz, wrong for 96** | `app.cpp:116-117` sets 48000 and 96. But block size is **never used** by the core (`Driver::init` ignores `buffer_size`). 96 matters only because it equals the 2 ms clock tick at 48 kHz. The real hard-coding is a set of literal sample counts at 48 kHz (section 8.2). |
| MIT licence | **Right but incomplete** | See section 2. |

**Additional Daisy touchpoints not in your notes:** `irq.queue.h` (`ScopedIrqBlocker`, used by deck commands), `common.h` (`#include <daisy.h>`, logging, `DSY_CLAMP`), `expose.h` (debug singleton), `Hold` and `MValue` (UI helpers, `StopwatchTimer`), and `Config::dynamic()` (SD-card config singleton).

---

## 4. Does `src/core` compile on a desktop?

**Short answer: yes, with small, known changes.** [ran]

Setup (all in a throw-away sandbox, nothing added to any repo):
- Real DaisySP at the pinned commit, built from source.
- libDaisy replaced by a ~40-line stub providing only: `StopwatchTimer`, `System::GetNow`, `ScopedIrqBlocker`, `Logger`, `DSY_CLAMP`, `FLT_FMT`, empty `SdmmcHandler`/`FatFSInterface`/`FIL`, and `DSY_SDRAM_BSS` as empty. The real libDaisy headers cannot be used on a desktop (they need the STM32 HAL).

Results:

| Test | Result |
|---|---|
| All 27 `.cpp` in `src/core`, `src/core/fx`, plus `hw/buffer.sdram.cpp`: GCC 13, C++17 | **27 of 27 compile** (needed forced `<cstdint> <cstddef> <cmath>`; see below) |
| Same, Clang 18 | **26 of 27**. One hard error: `fx.flux.cpp:83`, an *initialised variable-length array* (`Mode modes[mode_count] = {...}` with a non-constant count). Apple Clang would fail the CI build here. One-line fix. |
| Link against real DaisySP and drive `Core` from a test harness | Links and runs. |
| Harness at 48k/96, 44.1k/512, 96k/256, 192k/64 | No crash, **no NaN**. Record (detector-gated), stop, and loop playback produced audio at all four settings (non-zero output on essentially every playback sample). Ran roughly 10× to 70× faster than real time on a shared sandbox: **not a CPU budget measurement**. |
| Core started from heap memory pre-filled with `0x00` or `0xA5` | Identical, correct output. |
| Core started from heap memory pre-filled with `0xFF` | **NaN and silence.** See 8.1. |

What the compile test **does not** show:
- Only Reel mode on Deck A, DoubleMono route, was exercised. Slice, Drift, Deck B, FX, modulators, the panner, generative routing, the trig sequencer: **not run**.
- Running at other sample rates does not mean the *timing* is right at those rates: see 8.2.
- It says nothing about sound quality.

Header hygiene: several core headers (`tempo.h`, `synclock.h`, `detector.h`, `lutsinosc.h`) use `uint8_t`, `size_t`, `std::sin` etc. without including the right standard headers; ARM's toolchain pulls them in transitively. In the plug-in we force-include them (same trick your Beads CMake already uses).

---

## 5. Parameter list

Source: `src/ui/core.ui.cpp` (and `core.ui.pads.cpp`, `config.h`). Line numbers refer to upstream commit `7da8061`.

### 5.1 Design principle

On the hardware, each knob does several jobs depending on which pad is held (Alt, Flux, Grit, Tap). That is a front-panel constraint. **In the plug-in, every underlying value gets its own host-automatable parameter.** The firmware's pick-up logic (`MValue`) and the red LED offset indicator are not needed: plug-in controls are absolute.

All values below are normalised 0..1 at the Core API unless stated. **I have not yet read how Core converts them to real units (semitones, seconds, grain length).** Display units are decided in Milestone 2 after reading `generator.cpp` and `vox.cpp`.

### 5.2 Per deck (A and B identical)

| Parameter | Default | Core call / source | Hardware gesture and notes |
|---|---|---|---|
| Mode | Reel | `deck.set_mode()` (`core.ui.cpp:502-512`) | 3-way switch: Reel / Slice / Drift. Default is not defined by Core (`Mode::None`, `deck.cpp`); Reel chosen. Must be set before arming (see 8.1). |
| Position | 0.0 | `voxs().set_start` (`:132`, knob `:341-348`) | |
| Slice offset | 0.0 | `voxs().set_start_offset_interval` (`:133`) | Hardware: Alt + position, **only when the deck has cue points** (`has_cue()`). Not in the printed manual's text. |
| Size | 1.0 | `voxs().set_size(value, alt)` (`:134`, knob `:314-339`) | The `alt` flag's meaning is **[not read]** (`generator.cpp`). Cheat sheet says Alt+Size in Slice mode switches mono/stereo. |
| Pitch | 0.5 | Slice: `set_pitch`; Reel/Drift: `set_speed` (`:136-144`) | Manual: in Slice mode pitch shifts without changing speed. Neutral at 0.5. |
| Pitch snap | off | snaps to `kSpeedSteps` {−24, −12, −7, 0, +7, +12, +24} semitones (`core.ui.h:25-33`, `core.ui.cpp:9-14,138-140`) | Hardware: hold Alt while turning pitch. |
| Envelope | 0.0 (off) | `voxs().set_shape` (`:135`) | Manual: off at 0, then punchy ramp-down, ramp-up-ramp-down, ramp-up. |
| Envelope length | 1.0 | `voxs().set_env_size` (`:146`) | **Drift mode only** (Alt+envelope, `:351-354`). |
| Window (grain size) | 0.2 | `voxs().set_win_size` (`:147`) | **Drift mode only** (Alt+size, `:331-334`). |
| Mix (input to deck) | 0.5 | `deck.set_inout_mix` (`:149`) | 0 = input only, 1 = deck only (manual). |
| Feedback (overdub level) | 0.95 (`kDefaultFeedback`, `config.h:74`) | `deck.set_feedback` (`:150`) | Hardware: Alt + mix. **Not in the printed manual or cheat sheet.** Core clamps `value*1.05` to 0..1 and zeroes below 0.02 (`deck.cpp`). |
| Slice mono/stereo | stereo | `Config::is_slice_mono` | Cheat sheet: Alt+Size in Slice mode. How it is toggled is **[not read]**. |
| Flux on | off | `fx().set_flux_on` / `toggle_flux_lock` (`pads.cpp:18-25`) | Hardware: pad held = momentary; Alt+pad = latch. |
| Flux time | 0.5 | `flux().set_time_norm` (`:153`) | Free: 0.01 to 2 s (`fx.flux.cpp:48`). Clocked: 8 note divisions 1/32 to 4/4 (`:33-42`). Hardware: Flux + pitch. |
| Flux feedback | 0.5 | `flux().set_fb_norm` (`:152`) | Hardware: Flux + position. |
| Flux mix | 0.7 | `flux().set_mix_norm` (`:155`) | −40..0 dB (`fx.flux.cpp:77`). Hardware: Flux + mix. |
| Flux mode | Free | `flux().switch_mode` (`:575`) | Free / Clocked. Hardware: Flux + Tap. |
| Grit on | off | `fx().set_grit_on` / `toggle_grit_lock` | As Flux. |
| Grit intensity | 0.2 | `grit().set_intensity` (`:154`) | Hardware: Grit + pitch. |
| Grit character | 0.0 | `grit().set_character` (`:157`) | **Only does anything in Filter mode (it is the Q)** (`fx.grit.cpp:46-60`). Hardware: Grit + position. **Not in the manual.** |
| Grit mix | ≈0.33 (`fx.drive.cpp:23`; getter not confirmed) | `grit().set_mix` (`:156`) | Code treats it as a dry/wet stage (`fx.drive.cpp:39-48`). The manual calls it "overall gain". |
| Grit mode | Drive | `grit().switch_mode` (`:563`) | Drive / Reduce / Filter. **Each mode keeps its own intensity/mix/character**; the UI re-reads them on switch (`:565-567`). See Q-grit. |
| Modulator source | Follow | `mod.set_type` / `set_lfo_type` (`:491-527`) | **Differs per deck:** A = Follow / Square / Random; B = Follow / Saw / Sine. Manual says only "several LFO waveshapes". |
| Mod cycle (speed) | 0.3 | `mod.set_speed_norm(v, sync)` (`:159`) | LFO 0.01 to 12 Hz (`config.h:70-71`). |
| Mod glow (amount) | 0.0 | `mod.set_amp_norm` (`:160`) | |
| Mod LFO sync | off | `set_speed_norm(..., sync=Alt)` | Hardware: Alt + cycle. Divisions `{0.5,1,2,4,8,16,32,48,64}` (`modulator.h:202`). |
| Pos/size CV target | both | `set_start_mod_on` / `set_size_mod_on` (`:515-516,542-543`) | 3-way switch: position / both / size. **It selects what the CV input drives, not what the internal modulator drives.** |
| Playback / record / trigger | actions | see 5.4 | |

### 5.3 Global

| Parameter | Default | Source | Notes |
|---|---|---|---|
| Routing | Stereo (**my choice**) | `core.set_route` (`:487-489`) | Dual-mono / Stereo / Generative. `Core::_route` is **uninitialised** in the constructor; the UI sets it every cycle. |
| Deck A/B mix | 0.5 | `core.set_mix` (`:467-468`) | In dual-mono, this crossfade attenuates each output (output A = left, output B = right). |
| Tempo (internal) | 120 BPM | `driver.set_tempo_norm` (`:182`), range 20 to 250 (`tempo.h:29-30`) | |
| Clock source | Internal | `driver.toggle_source` (`:559`) | Hardware: Internal / 4-PPQN clock jack / MIDI (24 PPQN). Plug-in: Internal / Host. |
| Launch quantise | 1/4 | `driver.set_key_tick_interval_norm` (`:186`) | 17 steps: 1/16, 1/4, 2/4 ... 16/4 (4 bars) (`driver.h:18-36`). Slice mode. |
| Click level | 0.0 | `core.set_click_mix` (`:185`) | Hardware: Tap + glow A. |
| Pan speed | 1.0 | `panner().set_speed` (`:183`) | Generative route. Hardware: Tap + cycle **B**. **Not in the manual.** |
| Pan range | 0.6 | `panner().set_range` (`:184`) | Hardware: Tap + glow **B**. **Not in the manual.** |
| In A / In B / Out A / Out B trim | 0 dB | **Not in the firmware** | Replace analog trimmers. Plug-in addition. |

### 5.4 Actions (momentary triggers; automatable)

Per deck: **Play** (fwd), **Reverse**, **Record in** (Alt+Play: arm; starts on detected sound in Reel/Drift, quantised in Slice), **Record from other deck** (Alt+Reverse), **Trig** (loop from current settings), **Sequencer arm** (Alt+Trig; tap a rhythm; Alt stops), **Sequencer clear** (hold). Global: **Tap tempo**; **Fit tempo to loop** (Tap + size, Slice mode, `_set_tempo_by_size`, `:619-625`).

Discrepancy: the manual says hold Alt+Trig **2 seconds** to clear; the code uses **1500 ms** (`core.ui.h:173`, `Hold<1500>`). I will follow the code and note it.

### 5.5 Automation inputs (replace the CV jacks)

Hardware CV inputs become host-automatable parameters: per deck **Mix CV** (`inout_mix_mod_in`), **Pos/size CV** (`set_start_mod` and `set_size_mod`), **V/Oct** (`pitch_speed_mod_in`, ±5 octaves via `SpeedMap<60>`), and global **Crossfade CV** (`core.mix_mod_in`) (`core.ui.cpp:203-243`). These are **not** hardware-calibrated; the calibration code is dropped.

### 5.6 MIDI (from the firmware, reuse as the default map)

[read: `core.midi.cpp`, `config.h:80-105`, `core.ui.cpp:680-709`]
- Note-on on the deck's channel (defaults: ch 1 = A, ch 2 = B) triggers the loop, pitched by `note − 60` semitones.
- CCs: 3 crossfade, 14 rec ext, 15 rec int, 20 start, 21 offset, 22 size, 23 env, 24 pitch, 25 mix, 26 feedback, 27 env size, 28 window, 85 fwd, 86 rev, 89 mod cycle, 90 mod glow, 102/103/104 grit on/intensity/mix, 105/106/107/108 flux on/time/feedback/mix, 109 grit character.
- MIDI clock and Start/Continue/Stop are honoured (Start calls `driver.reset()`; play/stop only if enabled per deck in config).
- Not mapped on the hardware: CC for Slice mono/stereo, mode, route, pitch snap.

---

## 6. Panel mockup

Published as a Design artifact: **"Spotykach Unofficial Panel"** (private to you). A static mockup, 1440 × 1040. I have not looked at it rendered; please tell me if anything looks broken.

Layout, left to right (Deck B mirrored, as on the hardware's "reverse layouts"):

```
+------------------------- SPOTYKACH [UNOFFICIAL]  ... v0.0.0 [About] -------------------------+
| DECK A  [REEL|SLICE|DRIFT] |   ROUTING [dual mono|stereo|generative]  | DECK B [REEL|SLICE|DRIFT] |
|  (loop ring)  [pad grid]   |   A/B MIX fader                         |  [pad grid] (loop ring)   |
|  Position Size Pitch Env Mix|   CLOCK [internal|host] 120.0 BPM [TAP] |  ... mirrored ...        |
|  Feedback Window EnvLen ... |   launch quantise, click                |                          |
|  [FLUX]        [GRIT]       |   GENERATIVE PAN (speed, range)         |  [FLUX]        [GRIT]    |
|  MODULATION                 |   LEVELS (in A/B, out A/B)              |  MODULATION              |
|  automation inputs (CV)     |   TAPES (not designed yet)              |  automation inputs (CV)  |
+--------------------------------------------------------------------------------------------+
```

Notes:
- Mode colours (amber for Reel, violet for Drift) are **my placeholders**. The manual says the LED ring changes colour per mode but does not say which colours.
- The ring shows an illustrative loop and playheads. The firmware has 3 voices per deck (`Generator::kVoxCount = 3`), hence up to three playhead dots.
- Knob values shown are the firmware defaults from section 5.
- Controls that only apply in Drift or Slice are tagged; they stay visible so nothing moves when you change mode.
- Hardware-only things are absent by design: LED red-offset indicator, pick-up, gate jacks, headphone volume, calibration.

---

## 7. What the source does not define, or a plug-in cannot do

**Cannot exist in a plug-in (replace or drop):**
- All jacks: audio in/out and their trimmers, headphones, clock in/out, gate in/out, mod out (0 to 5 V), CV inputs. CV inputs become automation parameters (5.5). **Gate in** becomes MIDI note-on and a Trig action. **Gate out and Mod out** have no destination in a DAW; whether to offer an internal mod-to-parameter assignment is a design question (Q5).
- Daisy memory: 64 MB SDRAM and fixed global arrays (`hw/buffer.sdram.cpp`). Replace with per-instance allocation.
- SD card tapes (6 tapes × 6 tracks) and WAV read/write (`src/memory/*`, `src/hw/card.*`). The hardware reads/writes 48 kHz WAV only (`hw/card.cpp:49`, `memory/wav.cpp:119`). Replacement is project state; not designed (Q6).
- Calibration, bootloader/DFU, QSPI settings, LED ring and its colours, pad touch sensing, the Alt/Tap/Flux/Grit hold logic, pick-up.

**Not defined by the source:**
- Default values live in the UI layer and in DSP constructors, not in `Core` (section 3).
- `Core::_route`, `_click_mix`, `_mix`, `_mix_mod` are never initialised in `Core::Core()` (`core.cpp:11-17`). The hardware relies on zero-initialised static memory.
- Nothing in the firmware speaks to a **host transport or host tempo**. The closest model is its MIDI clock handling (24 PPQN, Start resets the divider). How the plug-in maps Logic's transport is **my design to propose**, not something to port.
- How loops are stored in a project, how big they may be, and what "Save/Recall" means in a DAW.
- Behaviour defined in the **online manual** (routing diagrams per mode, quantisation diagrams): not available to me.

---

## 8. Hazards found in the upstream code that the plug-in must handle

### 8.1 Initialisation and instances
1. **Zero-init dependence** [ran]: the engine misbehaves if its memory starts as NaN-valued garbage. Plan: allocate the engine in zeroed memory (mimicking the hardware's `.bss`), construct it there, then set every un-initialised member explicitly (`set_route`, `set_mix`, `set_click_mix`, `mix_mod_in`).
2. **`Core::init` uses function-local `static` buffer pointers** (`core.cpp:28-43`) and the buffers come from a global singleton pool with only one set of arrays (`SDRAMBuffer::pool()`). A second plug-in instance would reuse the first one's memory. Both must become per-instance. `Config::dynamic()` and `Expose::values()` are also global singletons.
3. **Callbacks must always be set**: `Driver` calls `_on_quarter` and `_on_clock_out` (`std::function`) unconditionally; unset, it throws `bad_function_call` [ran].
4. **Set the mode first**: `set_mode` is deferred to `process_commands()`; arming while the mode is still `None` silently arms nothing (`deck.cpp:200-203`) [ran].
5. **Apply the full default set** from section 5 before first use [ran]. Without it, playback was silent.
6. Two upstream `static` locals on the audio path: `static Event e = make_event()` (`deck.cpp:521`) and `hann.cpp:21` (initialised at load, fine). The first needs pre-initialising so it never locks on the audio thread.

### 8.2 48 kHz hard-coding (literal sample counts that assume 48 kHz) [read]
`config.h:57-67` (record fade 192, window slope 960, default window 2880, slice slope 192, slice min), `detector.h:9` (480), `generator.h:26` (max spread 144000), `vox.cpp:164` (`+96`), `vox.cpp:276` (`2880 + norm*21120`), `fx.flux.h:12-13` (`48000 × 5` s delay buffer, a compile-time template size), `buffer.cpp:26` (`_cut_switch.init(48000)`), `softswitch.h:106,111,118` (**a fixed 191-step fade against a 4 ms coefficient: wrong at any other rate; would click**), `hw/buffer.sdram.h:15` (buffer length). The 2 ms clock tick (`kPPQNIntern = 48`, `init(2000, 48)`) is time-based and fine as long as it is called every 2 ms.
**This list was found by grep; it may be incomplete.** An audit is part of whichever engine strategy we choose (section 9.3).

### 8.3 Real-time safety [read]
- **No allocation, locking or I/O found in the core** (grep for `new/malloc/vector/mutex/printf/fopen`: none).
- `Panner` uses `std::random_device` on the audio thread (`panner.cpp:89-105`); that may hit the OS entropy source. Replace with a cheap PRNG.
- `Deck` commands use `IrqQueue` with `ScopedIrqBlocker` (`irq.queue.h`). In the plug-in all Core calls happen on the audio thread only, which is what the firmware's own comment requires (`deck.h:72-76`), so this becomes a plain queue or a lock-free SPSC queue.
- `std::function` use is fine on the audio thread (calls do not allocate); assignment happens at init.

### 8.4 Other
- `Flux::switch_mode` uses an initialised VLA: **hard error on Clang** (section 4).
- `core.midi.cpp:_process_cc`: a CC on a non-matching channel passes `Deck::Count` as the deck index to handlers that index two-element arrays (`core.ui.cpp:710-717`). We write our own MIDI routing, so this is for awareness only.
- Unused third source buffer ("Undo", `buffer.sdram.cpp:48,102-109`): allocated, but no code requests it (`sourceBuffer()` is called twice, `core.cpp:28`). The plug-in allocates two.

---

## 9. Recommendations

### 9.1 Plug-in type: Music Effect (AU `aumf`, a "MIDI-controlled effect"), VST3 effect with MIDI input

Why: the instrument is built around recording incoming audio (the Mix control blends input and deck), so it needs audio input. It also needs MIDI note-on for triggering and CC for control. Your Beads project already uses this arrangement (`AU_MAIN_TYPE kAudioUnitType_MusicEffect`, `NEEDS_MIDI_INPUT TRUE`), so the Logic behaviour (inserted in a channel strip's MIDI-controlled-effects slot) is familiar to you.

**What you must test (Milestone 1):** that Logic offers it in that slot, that it passes `auval`, and that audio and MIDI both arrive. I cannot confirm Logic's behaviour from here.

Inputs and outputs for now: main stereo input, left = input A, right = input B (mirrors the hardware's two mono inputs); stereo output. Second stereo output for per-deck outs, and a sidechain input for B, are later options.

### 9.2 Framework: JUCE, built with CMake on GitHub Actions

- **JUCE 8.0.16** (latest 8.x; 12 Oct 2026; its changelog adds malformed-string protection [read]), with **8.0.6 as fallback** (what your Beads project pins). JUCE 9.0.4 also exists and bundles the MIT-licensed VST3 SDK, but I have not built with it, so I recommend staying on 8.x for now. I could not read your Beads CI status, so "proven" means only that Beads uses this setup and you have told me those builds worked.
- JUCE supports a macOS 10.11 deployment target [read: README], so macOS 12 and universal binaries are fine on its side. Whether the CI produces a clean arm64 + x86_64 build is for the CI to show.
- Licence consequence: **AGPLv3 for the whole plug-in** (section 2.4), or a paid commercial JUCE licence (unverified terms).
- I did not evaluate other frameworks this session (e.g. iPlug2) and have not checked their licences. Switching would cost you a proven setup for no benefit I can demonstrate.

### 9.3 Engine and sample-rate strategy

**Recommended: run the engine at a fixed internal 48 kHz and resample at the plug-in boundary, bypassing the resampler when the host is already at 48 kHz.** The engine's sample-count constants (section 8.2) then stay correct, memory and CPU stay bounded whatever the host rate (at a 192 kHz host the native-rate approach would need roughly 4× the memory: about 130 MB for loops per instance, from source constants, not measured), and the sound stays as the designers tuned it. `prepareToPlay` changes of rate do not need to reallocate or clear loops.

- The 2 ms tick: count engine samples and call `driver.tick(false)` and `core.prepare()` every 96 engine samples (2 ms), splitting host blocks at those boundaries. No FIFO is needed at 48 kHz, so **zero added latency** there. At other host rates only the resampler's latency is added.
- The 48 kHz / 96-sample assumptions stay inside the engine wrapper and never appear in host-facing behaviour. This is how I read your rule "resample to the host's sample rate and block size".
- Cost: bandwidth above 24 kHz is lost at high host rates (it already is on the hardware), and there are two conversions at non-48 kHz hosts. The alternative is to scale every constant in 8.2 to the host rate, which is invasive and easy to get subtly wrong.
- Your existing `Resampler.h` in the Beads repo can be reviewed for reuse. I have not reviewed it.

I will build it this way unless you tell me otherwise (decision D3).

### 9.4 Threading, smoothing, memory
- All Core calls on the audio thread. Parameters are read as atomics and applied at each 2 ms tick.
- Every parameter passes a wrapper smoother before it reaches the core; discrete changes (mode, route, toggles) rely on the core's own soft switches. Whether that is enough for each parameter is something we find out by ear (you report "sounds wrong").
- Memory per instance, from source constants: 2 source buffers × 42 s × 48 000 frames × 8 bytes ≈ 32.3 MB, plus ≈ 3.8 MB of delay buffers: **about 36 MB**. Allocated once, never on the audio thread.

### 9.5 CI (all building on GitHub Actions)
- macOS runner, `cmake` with `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"` and `CMAKE_OSX_DEPLOYMENT_TARGET=12.0`.
- **Triggers: `push` on `main` and `workflow_dispatch` only.** No pull requests, no other branches. (Your Beads workflow also triggers on `pull_request` and on `master`; I will not copy that.)
- Headless engine test, then AU + VST3 build, ad-hoc signing, packaging with `THIRD-PARTY-LICENSES.txt` and install instructions.
- I will try to run `auval` on the runner, but whether the runner can do that is **untested**; your `auval`/Logic validation remains the real test.

---

## 10. Milestone outline (indicative; each ends with an installable build)

| # | Ends with |
|---|---|
| M1 | Installable shell: loads in Logic as an effect, passes `auval`, About screen with credits and version, pass-through audio, engine allocated but idle. CI workflow. |
| M2 | Deck A, Reel mode: record, play, reverse, position/size/pitch/mix. Resampler and tick wiring proved. |
| M3 | Both decks, routing, A/B mix, trig, envelope. |
| M4 | Slice and Drift modes, tempo/clock, host sync, launch quantise. |
| M5 | Flux, Grit, modulators, sequencer, feedback/overdub. |
| M6 | MIDI map (firmware's), automation inputs. |
| M7 | UI to the mockup; plug-in state save. |
| M8 | Loop files (LOAD/SAVE) and persistence by file link, trims (done in v0.7.0, see decisions D53-D58). |
| M9 | BUILT (v0.8.0): window redesign after the original mockup (D45, D59), licence/credits/About review (D60). Awaiting test. |

Order and scope will change as the code teaches us things.

---

## 11. Open questions

See `decisions.md` (Q1 onwards). **Only Q1 blocks Milestone 1.**
