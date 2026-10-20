# decisions.md: Spotykach (unofficial)

Running file. Updated at the end of each milestone.
Last updated: 2026-10-20, M5 built (awaiting your test report).
Full reasoning and evidence tags are in SPEC.md; section numbers below refer to it.

---

## Decisions

"Mine" = I made the call from the evidence; you can veto it. "Yours" = you decided.

| ID | Decision | Who | Why / basis |
|---|---|---|---|
| D1 | Plug-in is an **audio effect with MIDI input** (AU `aumf` Music Effect; VST3 effect). Stereo in (L = input A, R = input B), stereo out. | Mine | The instrument records incoming audio and takes note/CC triggers. Same arrangement as Beads. **Unverified in Logic: tested in M1.** (SPEC 9.1) |
| D2 | Framework: **JUCE via CMake**, target **8.0.16**, fallback 8.0.6 (what Beads pins). | Mine | Proven in your Beads CI; JUCE supports the macOS target. I have not built 8.0.16 yet. (SPEC 9.2) |
| D3 | Engine runs at **fixed internal 48 kHz**; resample at the plug-in boundary; bypass resampler at 48 kHz host; tick/prepare by counting 96 engine samples. | Mine | Keeps upstream sample-count constants valid, bounds memory/CPU. Alternative (scale all constants) judged more invasive. (SPEC 9.3) |
| D4 | Whole plug-in licence: **AGPLv3**. Upstream MIT files kept separate with original notices plus `THIRD-PARTY-LICENSES.txt`. | Mine, **needs your agreement before any public release** | JUCE is AGPLv3 or commercial; MIT is compatible with AGPLv3. Not legal advice. (SPEC 2.4) |
| D5 | Name: **"Unofficial Spotykach"**. README and About say not official, not affiliated with Synthux Academy, and credit Synthux Academy and the designers. | Yours (project rule) / name text mine | (SPEC 2.5) |
| D6 | **Do not ship `synclock.cpp`.** Write an independent re-implementation of its interface (not a formal clean-room: I had read the original) as a tick clock (internal BPM or host tempo, 48 PPQN). | Mine | Header says derived from Paul Messick's 1993-98 toolkit; no licence text in the repo; I cannot resolve its terms. (SPEC 2.3) |
| D7 | Default routing is **Stereo**. | Mine | Firmware leaves `_route` uninitialised; hardware default unknown. Change freely. |
| D8 | Every underlying parameter is **its own automatable parameter**; no Alt/Tap/Flux/Grit-held knob modes, no pick-up logic. | Mine | Those are front-panel constraints. (SPEC 5.1) |
| D9 | CV inputs become **automation parameters**; gate in becomes MIDI note + Trig action. No attempt to emulate jacks, calibration or LEDs. | Mine | (SPEC 5.5, 7) |
| D10 | Follow the **code** (1.5 s) over the manual (2 s) for Alt+Trig clear-hold, and note it. | Mine | Source is what actually runs. (SPEC 5.4) |
| D11 | Use the repo's spelling **"Vlad Lytvynenko"** in credits until someone confirms. | Mine | `CREDITS.md`; manual says "Litvinenko". See Q4. |
| D12 | CI triggers: **push to `main` and `workflow_dispatch` only.** No `pull_request`, no other branches. | Yours (repo rule) | Note: your Beads workflow also triggers on `master` and `pull_request`; I will not copy that. |
| D13 | Never enable `USE_DAISYSP_LGPL`. | Mine | Avoids LGPL code entering the build. (SPEC 2.2) |
| D14 | JUCE pinned to tag **`8.0.16-security`** (the exact tag name upstream uses). Built green on CI at M1. | Mine | (Oct 2026) |
| D15 | M1 contains **no upstream Spotykach code and no idle engine** (SPEC section 10 said "engine allocated but idle"). Upstream code arrives in M2 with its notices. | Mine | Keeps M1 a pure install/validate test. |
| D16 | M1 has one parameter, **Output Level** (-60..+6 dB, 20 ms smoothing), to prove automation, smoothing and saved settings before any looper code exists. | Mine | Not part of the final design; may be removed. |
| D18 | Deployment target stays **macOS 12.0** (per your brief). I briefly lowered it to 11.0 on a mistaken reading of your OS version, then reverted. Your Intel Mac is Monterey 12.7.8 with Logic 10.7.8 (confirmed by you 2026-10-20); the M4 has Logic 11.2. CI now fails if the AU or VST3 lacks arm64 or x86_64 or has a minimum other than 12.0. | Mine / confirmed by you | |
| D17 | No release, sharing or sale is intended (you said so on 2026-10-20). Ad-hoc signing only; no notarisation. Repo is public for now. | Yours | Answers Q1 and Q3. AGPLv3 and upstream notices still kept. |

| D19 | M2 engine: core in zeroed per-instance memory (one calloc arena, about 32 MB per instance for two 42 s source buffers plus delay/slice/track buffers). Upstream patched minimally and every change listed in `third_party/spotykach/NOTICE.md`. | Mine | Firmware relies on zero-initialised statics and the SDRAM singleton. |
| D20 | M2 resampler: own 32-tap windowed-sinc, 512 phases, linear phase interpolation. Reported plug-in latency is 0 at 48 kHz and about 29-117 host samples at other rates (measured equal to reported within 1 sample). Not tuned for audio quality; **sound quality unknown until you listen**. | Mine | |
| D21 | M2 buttons REC / PLAY / REV are automatable bool parameters acting on the 0 to 1 edge; the window sends 1 then 0 after 80 ms. | Mine | Lets automation or MIDI-learn trigger them later. |
| D22 | M2 pitch parameter shows speed and semitones using the firmware mapping (0.5 = 1x, 1.0 = 4x, 0 = stopped), read from `generator.cpp`. Below 0.5 it is linear speed, so the semitone display is not linear in the knob. | Mine | |
| D24 | Resizable window requested by you for M3: default size unchanged (640x470 for the current one-deck layout), aspect ratio locked, size saved in plug-in state. Min/max range is my proposal (about 50% to 200%); your reply was "default as is, no change". | Yours (request) / mine (range) | Headless test only; look and feel must be checked by you in Logic. |
| D25 | M3 scope (from your list): both decks, routing, A/B crossfade, trig, envelope, plus resizable window. Also added "record from the other deck" (the firmware's internal source) because the core needs it for Deck B to be useful as a resampler, and Pan Speed / Pan Range because Generative Stereo uses them. Slice, Drift, FX, modulators, clock, MIDI are still later milestones. | Mine | Firmware: `core.ui.cpp` `_toggle_record`, `_trigger`, `_process_switches` (route), `CTRL_CROSSFADE`. |
| D26 | Parameter ids: Deck A keeps its M2 ids (so M2 sessions load); Deck B uses a `b_` prefix. New: crossfade (default 0.5), route (Double Mono / Stereo / Generative Stereo, default Stereo), panSpeed (1.0), panRange (0.6; defaults from `_init_values`), trig and recInternal buttons per deck. **Behaviour change vs M2:** crossfade default 0.5 (M2 was effectively Deck A only), so Deck A is now at about 0.75 gain with the crossfade centred. I did not verify the hardware knob's resting position. | Mine | Square-law crossfade in `xfade.h`. |
| D27 | Double Mono: left input goes to Deck A and right input to Deck B, outputs left = A, right = B. Stereo and Generative Stereo feed both decks the same stereo input. | Mine | Read from `core.cpp` `Core::process`. |
| D28 | Window: base 640x620, aspect ratio locked, resizable from 75% to 200% (I chose 75% as the minimum because labels become unreadable below it; you said keep the default as is). Width is stored in the plug-in state and restored. Rendered headlessly on Linux only; I have not seen it on macOS. | Mine / yours | |
| D29 | M4 pitch usability (your request): per-deck **Pitch Snap** = Off / Steps (the firmware's list: -24, -12, -7, 0, +7, +12, +24 semitones) / Semitones (whole semitones -24..+24, knob centre = 0). Free mode is unchanged. The pitch knob's text shows the pitch the engine will play. In snap modes there is no "stopped" position. | Mine | Firmware `kSpeedSteps` and `snapped_speed()`; the firmware did this only while Alt was held. |
| D30 | M4 modes: per-deck Mode Reel / Slice / Drift with Slice Offset (Slice), Envelope Length and Window (Drift). **Drift is silent while Envelope is below about 5 %** (core suspends the voice: `Vox::set_shape`); the window says so. Slice mono/stereo is NOT exposed: the firmware stores it in a process-wide singleton (`Config::dynamic()`), which would be shared between plug-in instances. | Mine | |
| D31 | M4 tempo: Tempo (20-250 BPM) and Tempo Source Internal / Host (default Host). With Host: tempo follows the host; while the host plays, the clock is phase-locked to the host's beat position (24 PPQN ticks derived from host PPQ), locking on the next whole beat; if the host stops or jumps, the internal clock keeps running at the host tempo. Beats, not bars: launch quantise counts from the beat the lock was taken on. **Lock accuracy is unmeasured against a real host; verify by ear.** Launch Quantise list from the firmware (1/16, 1/4, 2/4 ... 16/4). Click Level added. Not in M4: tap tempo, fit-tempo-to-loop, external clock jack. | Mine | |
| D32 | M5 Grit: one set of controls (Intensity, Mix, Character) per deck applies to ALL three Grit modes (Drive / Reduce / Filter); the firmware keeps separate values per mode and uses pick-up logic on a switch. Resolves Q-grit differently from my M0 suggestion: three sets of knobs would be unreadable in a plug-in. Consequences: Filter Intensity is a bipolar cutoff (centre = open, left = low-pass, right = high-pass), so the shared Intensity value means different things per mode; Character is the Filter's resonance and does nothing in Drive/Reduce. | Mine (veto welcome) | `fx.grit.cpp`, `filter.cpp` |
| D33 | M5 Flux Clocked mode follows the engine tempo. **Deviation from the firmware:** `Flux::set_tempo_bpm` has no caller anywhere in the upstream repo, so on the hardware Clocked delay appears to stay at 120 BPM (my reading of the source; not tested on hardware). | Mine | Test: at 60 and 90 BPM a 1/4-note echo lands at 1.000 s and 0.667 s. |
| D34 | M5 Flux On and Grit On are latching toggles (firmware: pad held = momentary, Alt+pad = latch). Both decks have an FX page button in the deck header; the page shown is not remembered. | Mine | |
| D35 | **Modulators are not in M5.** Upstream, the modulator output is only written to the hardware Mod-out jack (`app.cpp`); nothing inside the core uses it. In a plug-in it does nothing audible unless it is routed to a parameter. See Q5/Q9. | Mine | |
| D23 | CI now runs a blocking headless `engine_test` and a blocking editor smoke test (opens the real editor, renders it, presses the buttons, runs audio at 44.1/48/96 kHz). The smoke test passed on the macOS runner (this resolves the M1 "unverified that it works on a headless runner" proposal). It does not prove the window looks right or that Logic accepts it. | Mine | |

---

## Open questions

| ID | Question | Blocks? | My suggestion |
|---|---|---|---|
| ~~Q1~~ | ANSWERED: repo is `particledots/Unofficial-Spotykach` (public). | - | `particledots/Unofficial-Spotykach`, **public**, so Actions minutes are free; tell me if you want it private. Steps are in my message to you. After creating it, I need push access via the Claude app's repo connection. |
| Q2 | Default clock source: Internal or Host Sync? | No (M4) | Host Sync. |
| ~~Q3~~ | ANSWERED: no public release, no sharing, no sale. (Original question: will you release it publicly? Affects signing and notarisation (ad-hoc signing means users must clear quarantine; notarising needs a paid Apple Developer account, **fee not verified by me**). | No (M8) | Decide before M8.) | - |
| Q4 | Which spelling of Vlad's surname should be credited? | No | Ask Synthux if releasing publicly. |
| Q5 | Should the Mod out jack and Gate out have any plug-in equivalent (e.g. internal modulator assignable to a plug-in parameter), or are they dropped? | No (M5) | Drop for now. |
| Q6 | Tapes (6 tapes × 6 tracks on SD): what should this be in a project (e.g. saved with the project, or exportable WAV)? | No (M8) | Save loops inside the plug-in state; discuss at M8. |
| Q-grit | Grit keeps separate values per mode (Drive/Reduce/Filter): expose all three sets, or one set that follows the current mode? | No (M5) | One set per mode, to match the firmware. |
| Q7 | Envelope at minimum has a hard attack (firmware behaviour). Should I add an optional short "Declick" fade (a few ms, off by default or on by default?) so the minimum is not abrupt? It would differ from the hardware. | No | Add as a per-deck option, **on** by default, if you find the hard edge unpleasant. Your call. |
| ~~Q8~~ | ANSWERED: no change needed; Semitones mode already covers -3, -5 etc. | - | - |
| Q9 | Modulators (Follow / LFO per deck) only drive the hardware Mod-out jack. A plug-in has no jack. Options: (a) leave them out; (b) add a patch-bay style routing, Mod A/B assignable to a deck's Position, Size, Pitch, Mix or the crossfade (like patching Mod out into the CV inputs; this is NOT firmware behaviour but is how the hardware is meant to be used). | No (M6) | (b) if you want them, at small scope: one target and one amount per modulator. Your call. |
| Q-engine | Slice mode: mono/stereo toggle and `set_size` alt flag still unread. | No (M3+) | Read before Slice work. |

---

## Milestone log

| Milestone | Status | Notes |
|---|---|---|
| M0 | **Done, accepted (no vetoes)** | SPEC.md and this file written. Licence verified. Core compiles on desktop with fixes. Parameter list drafted. Panel mockup published (render not checked by me). |
| M1 | **Fixed build 0f92d6e (run 39484308800, CI green incl. arch+minimum-OS check). Awaiting your Logic test on both Macs.** Build 8b83286 CRASHED Logic 10.7.8 on the Intel Mac (macOS 12.7.6) when the plug-in window opened: my bug (setSize() in the editor constructor ran resized() before the About panel was created; null dereference). `auval` passed on the Intel Mac for 8b83286 but does not open the window, so it cannot catch this. Superseded builds (do not test): 0904db3, 48e54e3, e6eceaa, 1c047cc, 8b83286. M4 (macOS 26.5, Logic 11.2.2) was not tested on 8b83286 since it would crash the same way. |
| M1 result | **Accepted.** Build 0f92d6e passed all 6 checks on both Macs. | |
| M2 | **Accepted 2026-10-20: all 7 checks WORKED on both Macs (build 9a1881e, run 39489948406).** CI: engine_test, arch+minimum-OS check, editor smoke test, auval all passed. Scope: Deck A, Reel mode only (record, play, reverse, position, size, pitch, envelope, mix, feedback, output level). Tested only headlessly: Reel/Deck A with Stereo route; Slice, Drift, Deck B, FX, modulators, external clock are untested. Sound quality unverified. | |
| M3 | **ACCEPTED 2026-10-20 (v0.2.1, build 39a0088): window size and project recall retested OK on both Macs; Double Mono confirmed.** Earlier detail: your test (build 9a0b1e8, both Macs): items 1, 3-8 worked ("sounding brilliant"; item 7 "i think so", not confirmed with distinct L/R sources). Item 2/9 FAILED: window size was not remembered. Cause (reproduced headlessly): `setResizeLimits()` in the editor constructor triggers `resized()` at size 0, which overwrote the stored width before it was read. Fixed in v0.2.1 (read first, store only after construction) and the smoke test now closes and reopens the editor and checks the size. Loops are not saved with the project: expected, planned for M8 (D-loop persistence).** Earlier status: built, awaiting test. Engine tests pass for both decks, crossfade, Double Mono, record-from-other-deck and trig. Not verified: how Generative Stereo sounds (smoke tested for finite output only), whether Trig sounds right on a playing loop, macOS appearance. |
| M4 | **ACCEPTED 2026-10-20 (v0.3.1, build c410bc2): semitones, snap-off at -7, steps, Slice with TRIG and Slice Offset all work on both Macs; Drift now clearly grainy ("reminds me of the spray effect on Instruo arbhar"). Open: whether the Steps list should also include +/-3 and +/-5 (see Q8); declick (Q7) unanswered.** Earlier detail: your test (v0.3.0, build 0a8f359, both Macs): 1, 2 ("perfect and extremely helpful"), 7-10 worked. Item 3 not answered. Item 4 FAILED: turning snap off left the knob near, not at, the snapped pitch (e.g. -7 st), because the knob kept its own position. Item 5: question unclear to you (explained). Item 6: Drift only sounds grainy at low Size, not very grain-like (explained; firmware logic, not compared with hardware). Fix for item 4 in v0.3.1: with snap on the knob itself jumps to the snapped position; turning snap off leaves the knob at the pitch that was playing. UI logic untested headlessly (timer-driven); engine mapping unit-tested.** Earlier status: built, awaiting your Logic test. Headless: Slice and Drift record and play audibly, pitch snap gives exact octave and fifth, host sync locks and follows tempo, no allocations, clean under ASan/UBSan. Not verified: how Slice and Drift sound compared with the hardware, host-lock accuracy, macOS appearance. |
| M5 | **Built; awaiting your Logic test.** Scope: Flux and Grit on both decks, FX page in the window. Headless: free-mode echo lands within 1 ms of the set time, clocked echo follows tempo, every Grit mode changes the sound, no allocations, clean under ASan/UBSan (one benign upstream DaisySP shift warning suppressed in my local run). Not verified: how it sounds compared with the hardware; macOS appearance. M6 plan: modulators (if Q9 says yes) and the sequencer; M7: MIDI and automation inputs; M8: loop persistence, polish, notices review. |

---

## Lessons / process

- `auval` does not open the plug-in window. A green CI and a passing `auval` do not prove the editor works. A window-open test (a headless editor smoke test in CI, or opening it in Logic) is needed for every milestone that changes the UI. Proposal: add an editor smoke test to CI in M2 (unverified that it works on a headless runner).
- Intel Mac details: macOS 12.7.6 (crash report), Logic 10.7.8 (build 5817), MacBook Pro (MacBookPro11,2). Your earlier notes said 12.7; the crash report says 12.7.6.

---

- Running the engine tests on x86 caught a crash that ARM hides: `Generator::apply_dimensions()` takes a modulus by zero in Slice mode on an empty deck (ARM returns 0, x86 raises SIGFPE and would have crashed Logic on the Intel Mac). Patched, and recorded in `third_party/spotykach/NOTICE.md`. Lesson: test on x86 and under UBSan, not only on the M4.
- Your envelope observation: at minimum (0) the firmware envelope is OFF, so the playback edges are hard. The attack only becomes a ramp above about a third of the knob (`ADEnvelope::_make_shape`). I have not changed it; see the question below.

---

## Upstream issues found (kept for a possible report to Synthux)

1. `fx.flux.cpp:83`: initialised variable-length array; invalid in Clang.
2. `fx/deline.h`: derived from DaisySP `delayline.h` but copyright header missing.
3. `core/synclock.cpp`: Paul Messick toolkit lineage, no licence text in repo.
4. `fx/echo.h`, `fx/biquad.h`, `lutsinosc.h`: missing or partial licence headers.
5. Uninitialised members in `Core` (`_route`, `_click_mix`, `_mix`, `_mix_mod`); relies on zeroed static memory.
6. `Panner` calls `std::random_device` on the audio thread.
7. Missing standard `#include`s in several core headers (ARM headers hide it).
8. `softswitch.h`: fixed 191-step fade against a 4 ms constant; only right at 48 kHz.
9. Unused third ("undo") source buffer is allocated.
10. MIDI CC on a non-matching channel passes `Deck::Count` as a deck index (`core.ui.cpp:710-717`).
11. Manual vs code: Alt+Trig hold 2 s vs 1.5 s; Grit "Mix" described as overall gain but implemented as dry/wet; Grit Position (character) and Alt+Mix (feedback) and Tap+Cycle/Glow on deck B (pan speed/range) not documented.
13. `generator.cpp:144` modulus by zero on an empty deck (see Lessons).
14. `Flux::set_tempo_bpm` is never called, so Clocked delay is fixed at 120 BPM (see D33).
12. Credits spelling: "Litvinenko" (manual) vs "Lytvynenko" (CREDITS.md).
