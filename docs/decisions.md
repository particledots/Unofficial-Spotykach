# decisions.md: Spotykach (unofficial)

Running file. Updated at the end of each milestone.
Last updated: 2026-10-20, end of Milestone 1 build (awaiting your test report).
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
| D6 | **Do not ship `synclock.cpp`.** Write a clean-room tick clock (internal BPM or host tempo, 48 PPQN). | Mine | Header says derived from Paul Messick's 1993-98 toolkit; no licence text in the repo; I cannot resolve its terms. (SPEC 2.3) |
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
| D18 | **Deployment target lowered from macOS 12 to macOS 11.0** (project brief said 12). CI now fails if the AU or VST3 lacks arm64 or x86_64 or has a minimum other than 11.0. | Mine, on your requirement that it run on the Intel Mac (Logic 10.7.8; macOS version 11.2 vs the 12.7 in your notes still to be confirmed) | Same floor as your Beads build. |
| D17 | No release, sharing or sale is intended (you said so on 2026-10-20). Ad-hoc signing only; no notarisation. Repo is public for now. | Yours | Answers Q1 and Q3. AGPLv3 and upstream notices still kept. |

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
| Q-engine | Unknown until read: units of Core's normalised parameters, `set_size` alt flag, Slice mono/stereo toggle. | No | I will read `generator.cpp`/`vox.cpp` before M2 and report. |

---

## Milestone log

| Milestone | Status | Notes |
|---|---|---|
| M0 | **Done, accepted (no vetoes)** | SPEC.md and this file written. Licence verified. Core compiles on desktop with fixes. Parameter list drafted. Panel mockup published (render not checked by me). |
| M1 | **Rebuilt for macOS 11 floor; see commit below. Awaiting your Logic test on both Macs.** (First build: run 39429423787, commit 0904db3, macOS 12 floor: superseded, do not test.) | AU+VST3, universal, macOS 12 target, pass-through, About with version and build id, one smoothed parameter. CI confirmed all steps succeeded. **I cannot read the CI logs or download the artifact from my sandbox (blocked), so I have not seen the architecture list, the minimum-macOS list or the auval output myself.** No error annotation was raised on the auval step, which suggests it passed, but that is inference. The zip contains `AUVAL-RESULT.txt` and `auval.log`: please open `AUVAL-RESULT.txt`. |

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
12. Credits spelling: "Litvinenko" (manual) vs "Lytvynenko" (CREDITS.md).
