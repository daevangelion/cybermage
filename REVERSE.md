# CyberMage: Darklight Awakening (DOS, 1995) — Reverse Engineering Notes

**Platform:** `skill://re/platforms/dos-mz.md`; conventions in `AGENTS.md`.

## Modules

| Module | Role | Format | File / notes |
|--------|------|--------|--------------|
| CM_HI.EXE | primary | Watcom DOS/4GW LE | `orig/retail/CM_HI.EXE`, 726,898 B, retail v1.01F |
| DEMO_HI.EXE | control build | DOS/4GW LE | `orig/demo/CMDEMO/CM_HI.EXE`; not address-compatible with retail |
| CM_LO.EXE | control build | DOS/4GW LE | demo low-resolution executable; not yet configured |
| PLAYTGV.EXE | movie player | DOS/4GW LE | 430,381 B; LE header at file `0x2C80`; retail external process |
| DOS/4GW stub | extender/runtime | MZ prefix | shipped runtime, not game code |
| DOS4GW.EXE, UNIVBE.EXE | runtime / video utility | DOS executables | shipped vendor tools, not port targets |
| SETUP.EXE | configuration utility | DOS executable | hardware configuration, not yet analyzed |
| *.RSC | asset-archive | game resource containers | see format slice findings |
| PANEL00–11.DAT | asset-archive | panel resources | see format slice findings |
| *.TGV, SPEECH.ENG | assets | movies / speech | external player / game audio |
| TEXT.MSG, STRINGS.MSG, *.BNK | assets | text / MIDI banks | strings and HMI SOS data |
| SAVE*.DAT | save state | explicit binary serialization | do not dump native structs |

## Binary Identification

### CM_HI.EXE

| Field | Value |
|-------|-------|
| Platform / CPU | DOS, i386 protected mode |
| Compiler / ABI | Watcom C/C++32; register args EAX, EDX, EBX, ECX, then stack; `this` EAX |
| File / version | `orig/retail/CM_HI.EXE`, 726,898 B, 1.01F |
| Payload | LE after DOS/4GW MZ stub; 10,435 fixups |
| Entry | link address `CM_HI.EXE:0x06C404` |
| Debug | Watcom v3 trailer, final `0x10B97` bytes; 1,076 code + 642 data globals |

## Memory Map and Address Conventions

All labels use **LE link addresses**, not file offsets or DOS linear runtime
addresses. Every address must identify its module. Object 1 starts at `0x10000`,
object 2 at `0x90000`. An `Xox+0xNNNN` value is a field offset, not a link address.
The Rust host relocates the image: use its `le::rt` for state reads, never assume
its runtime address equals the link address.

| Module | Link range (end exclusive) | Purpose |
|--------|----------------------------|---------|
| CM_HI.EXE | `0x010000–0x089368` | object 1, code; vsize `0x79368` |
| CM_HI.EXE | `0x090000–0x20AB80` | object 2, data + BSS; vsize `0x17AB80` |

## Data-Range Map

| Range | Classification | Evidence / limits |
|-------|----------------|-------------------|
| LE object 1 | code with embedded tables | ~305 KB in 504 named game functions; function boundaries need decoding |
| HMI SOS / mixing modules | vendor code | ~33 KB; label, do not port |
| Watcom CRT modules | vendor code | ~43 KB; label, do not port |
| final `0x10B97` file bytes | debug metadata | 1,718 globals at 1,677 unique link addresses |

## Key Findings

### Symbols and analysis

`tools/debug_labels.py` reads the original debug globals; `labels.csv` is the naming
authority. Overloaded readable names get an address suffix. Same-address aliases
remain in comments (all 1,718 globals retained); the first linker name is primary.
Source module and mangled name remain in every comment. Parser format reference:
`[rust: tools/wdbg.py]`; debug contents are read from the original EXE.

Named game class counts: Xox 244, Image1 84, Video 60, Sounds 16, Aui 5,
Joystick 5, Mouse 4, Keyboard 2. These are names, not a completion denominator.
dosre's direct-call discovery finds 417 functions (~18% code); seeding code globals
reaches ~71%. Unreached bytes are **not** established data/dead code.

### Oracle

See `docs/re/bootstrap.md` for commands, pure-original execution mode, input timing,
and limitations. Captures and original files are ignored; never commit them.

## Intermediate Output Files

| File | Contents |
|------|----------|
| labels.csv | retail symbols, LE link addresses, original source/name provenance |
| dosre.toml | code module paths and types |
| docs/re/*.md | subsystem findings, evidence, open questions |
| captures/ | local oracle frames/logs/state; ignored |

## Port Progress

Run heavy commands below through `tools/run_capped.sh`. Evidence scope and
known gaps are deliberate; implementation is not full-game parity.

| Subsystem | Status | Evidence command / result | Known gaps |
|-----------|--------|---------------------------|------------|
| resources/formats | Implemented retail loaders/title | `make -C port check`; `tools/compare_frames.py out/port/title/frame000000.ppm captures/title/frame000000.png`:0/307200 | Demo/encoding variants; not every TEXT mode ([formats](docs/re/formats.md)) |
| world | Implemented; scene1 checkpoint parity | `make -C port -f tests/world.mk B=build/world check`; `tools/world_compare.py port/build/world/test_world captures/newgame/scene-start.xox --persons captures/newgame/scene-start.persons`:106640 values match | Other scenes load, but lack equivalent state comparisons ([world](docs/re/world.md)) |
| sight | Implemented pixel-free queries | World gate:40 original RaySight/RayMissile vectors match | Broader retail routes/vertical edge coverage |
| target | Original combat order MATCH, conditional input scope below | `tools/compare_targets.py`:1298 completed checkpoints, all13 original nonempty, zero mismatches | Broader scenes/combat routes ([AI](docs/re/ai.md)) |
| render_sw | Implemented; scoped complete newgame frame exact | Render gate and `tools/compare_frames.py`:completed newgame frame115 differs0/307200; renderer-only views exact outside hand footprints | Moving props, other scenes, painter ties/extreme pitch ([render](docs/re/render.md)) |
| render_gl | Opt-in enhanced backend integrated | `tests/render_gl.mk check check-gl check-gl-es scene scene-es`; real640/1920 app replays MATCH; title/HUD exact against SW | Not DOS-raster-exact; broader scene/device acceptance ([render](docs/re/render.md)) |
| classic input/Sim | Named155-scenario gate:155 actor/RNG MATCH,155 player MATCH | Both106-row movement traces,1299 combat,2033 water/127 wet, original mouse154 and real death/restart/load routes MATCH | No remaining observed divergence in this cohort; legacy joystick and broader flight/multi-scene routes unverified ([Sim](docs/re/sim.md)) |
| modern controls | Desktop opt-in; Android default action/touch/controller layer | `make -C port -f tests/controls.mk B=build/Controls controls-check`; Android virtual-stick walking verified | Physical-controller/long-play usability ([controls](docs/re/controls.md)) |
| save/load | Codec, live bridges and genuine saved-start replay integrated | Retail SAVE010246045 bytes/GAMENAME288 round-trip; `--replay-load-slot 0`:earned Training94 and water94 player records MATCH | Full original-save playthrough and demo format ([UI](docs/re/ui.md),[Sim](docs/re/sim.md)) |
| Events/inventory/vehicles | All90 dispatches; genuine tank entry/exit reducer MATCH | `tests/events.mk`; saved-start `events_trace`:Training639 frames/22,009,716 event bytes/116 vehicle rows MATCH; water94/3,237,736 bytes | Reducer consumes measured Sim phase inputs; full vehicle/multi-scene gameplay not established ([Events](docs/re/events.md)) |
| AI/NPC combat | Conditional combat and genuine saved-fight parity | Named gate:197296 combat actor checkpoints and62550 earned Training stair-fight checkpoints; Scene2 state14 turret re-gated MATCH | Normal PERSON+24 defined-zero deviation and broader full-game coverage ([AI](docs/re/ai.md)) |
| player fire/projectiles | Integrated; scoped original weapon/draw/respiration parity | `tests/pcfire.mk`, `tests/projectiles.mk`;193 fire frames/772 missile checkpoints and5040 respiration draws match; CollFire120 original sweeps | Broader integrated selector/firearm/vehicle and missile-path coverage ([AI](docs/re/ai.md),[PC fire](docs/re/pc_fire.md)) |
| UI/HUD/shops | Integrated; scoped named pixel comparisons match | `tests/ui.mk` gate; title/options/HUD/composed-newgame/pause comparisons0 differences | Broader shop/vehicle/live-weapon/arena runtime comparisons ([UI](docs/re/ui.md)) |
| timer/RNG/fixed math | Implemented integral semantics | World gate:6144 trig values,106 angle/distance,60 RNG,16 rotation vectors match; movement trace MATCH | Whole-game RNG call order still under comparison |
| audio/speech/movies | Integrated native PCM/OPL/HMI/TGV | `make -C port -f tests/audio.mk check`:38 songs validate; first30 frames of three movies match FFmpeg | Not exact SOS driver/mixer/voice-allocation parity ([audio](docs/re/audio.md)) |
| platform/Android | Full-engine APK and opt-in Android GL runtime verified | Frozen20117a3 x86_64 APK:API36/GLES3 `--es renderer gl` and SW title→NEW→scene1→modern touch walking, no crash; earlier dual-ABI gate atc699ea4 | Physical-device/long-play acceptance; no measured FPS logging ([APK/screenshots/logs](port/android/README.md)) |

Named integrated gate: commit `ae2792acce6e8b19a93471f3ee61dd73e43d0389`,
isolated `git archive` snapshot, strict build and complete `make -C port check`
and separate five-group phase/six-target controls gates exit0. Receipt
[gate-ae2792a.json](docs/re/gate-ae2792a.json) pins binary SHA256
`e05c88457b1dfda7acdbbf5f0242232c8b1c14b80fe70e1f1289dd2a366e3543`
and the full148-scenario gate plus seven original continuations/witnesses:
155/155 full actor/RNG MATCH,155/155 player MATCH,42,015 original state
records and6,350,481 actor checkpoints. The included save-before-death69 and
failed-hop17 regressions add no continuous-survival or decoder-acquisition
claim. The archive includes Review2 through87e5f55, Events2's deferred
ordinary-pickup correction, and the headless rehearsal cadence option.
Actual-clock replay ignores that planning option; cadence3 forecasts are
not original-survival or parity evidence. All four originally
reported divergences, type59 spawn heading, and weapon-room RNG71 are now
fixed and re-gated. Original CollItem435AA..435C9 queues pickups;
CollObject4404C..44086 removes them after all four tiles, preserving pickup903
and its DoDice12650/RND1. Kind17 bundles intentionally remain immediate.
No RNG-padding or saved-start PERSON+24 workaround is used. Actual draw-clock,
UI-action and death-controller inputs are used; missing reads fail closed.
Fresh per-scenario users load the actual original Normal or Easy configuration;
earlier mismatched-config native survival forecasts are not parity evidence.
The external CollectionChamber Scene2 slot1 turret mismatch is now closed.
Audio's original state14 correction preserves nonzero-cooldown/no-target jumps
to Move36EF1, per-launch signed burst handling and reload animation1, eliminating
the spurious native RND6/animation5 tail. All109/113/105-record load/doorway
traces and the758-record groundstreet now fully match. The exact original BP
value and original RND call sequence were not measured; source proof covers
either BP outcome without inference. Numeric Scene9's public-start first bend
and aperture28 descent also match; historical Temple filenames do not establish
level identity. Neither external fixture establishes our sequential campaign
ancestry; actual package configuration and slot numbers are preserved without
SAVE renaming or undefined-state adapters. Raw Scene9 control2's actual
original and native saved switch2 byte both change0→1; this is not an
all-save-byte parity claim or scene-exit claim.
Three additional Scene2 slot2 branches match311/114/231 records. The first
returns outdoors after failed stairs, the second faces a door jamb after
switch28, and only the third genuinely crosses the westdoor atHP77; none
establishes a cashfloor/basement objective or scene exit.
The later339-record Scene2 upstairs segment genuinely reaches floor2 atHP63,
but forecast-only cash pickup is not original progress. Raw Scene9's586-record
upper-gate attempt stays atZ0 throughout; its save nameUPPER and native forecast
do not prove a climb. Actual-input replay matches that failed alignment fully.
No campaign exit or full-game completion is claimed, and future concurrent
HEAD changes are not certified. The earlier
[147-route](docs/re/gate-8df96e0.json),
[140-route](docs/re/gate-c2a3a0c.json),
[126-route](docs/re/gate-5e32231.json),
[112-route](docs/re/gate-cda7722.json),
[92-route](docs/re/gate-f0186a3.json) and
[76-route](docs/re/gate-5bc4fde.json) baselines remain historical evidence.
Boot comparisons import only the explicitly approved original undefined PERSON+24
word; saved-start comparisons use the real SAVE without that adapter.
Measured input/actor/draw clocks are external inputs; no coordinates, HP,
RNG, deadlines or target outputs are installed. Corrected movement inputs retain
all106 measured BDA bytes, including Ctrl=4 on frames71..75. This named receipt
does not silently certify later concurrent HEAD changes.

Later incremental route gates:
[gate-4461e87.json](docs/re/gate-4461e87.json) retains the earlier8/10 full
actor/RNG and9/10 player matches. After the definition-difficulty migration,
[gate-c1018ed.json](docs/re/gate-c1018ed.json) fully matches all10 upper-hall,
door, decoder and lift segments:1544 state records and233168 actor checkpoints.
The prior door5 NPC120/RNG130 and lift13 NPC123/RNG236 divergences are closed
in that named cohort. Actual saved switches5/38 reach1 after their real uses;
the F5/F6 decoder-case246045-byte save outputs are byte-identical to the
originals. This is not an all-save-byte parity claim for the other segments.
These earlier incremental results do not replace the consolidated counts above.

## Verification Checklist

- [x] Ph3: 3+ functions cross-checked against original execution (world math/rays and CollFire gates above).
- [ ] Ph4: 5+ individually identified sprites/tiles visually compared to original; whole-frame UI/view matches are not that census.
- [x] Ph5: key structures confirmed by original state reads (scene1 maps/objects/PERSON fields and both12392-node pools;106640 values match).
- [ ] Ph6: full game session played without major logic gaps
- [ ] Ph7: 3+ integrated scenarios match both frames and checkpoint state; strict build/check and movement MATCH pass, but do not alone close this combined gate.

### Replaying earned saves

`--replay-load-slot N --user DIR --replay TRACE` initializes through the real
SAVE codec before comparing/stepping record0, rather than calling NewGame.
The oracle trace must start from its own Load of the identical original-written
SAVE bytes. Copy original export directories into a writable test user directory;
never overwrite the earned originals. Slots are0..10 and both `--user` and
`--replay` are required. `events_trace DATA TRACE EVENTS-INPUT` accepts the same
`--replay-load-slot N --user DIR` initializer; its event/vehicle outputs are still
compared before applying the separately measured MoveWorld phase inputs.

Verified: original Training SAVE000 (`f9cd8fb7…`)→original Load trace
`captures/scene11-training-load/frames.csv`:94 frames player-state MATCH.
Earned water SAVE000 (`9564cbee…`)→`captures/vehicle-water-resume/frames.csv`:
94 frames MATCH; the saved-start event reducer matches94 frames/3,237,736
computed event bytes. Copies and full hashes are in
`captures/Integrator-load-{training,water}/run.json`; earned originals were
not modified. These are saved-route checks, not a full playthrough.

```
port/build/Integrator/cybermage --headless --data orig/retail --user COPIED_SAVES --replay ORIGINAL_LOAD/frames.csv --replay-load-slot 0
port/build/Integrator-events/events_trace orig/retail ORIGINAL_LOAD/frames.csv ORIGINAL_LOAD/events-input.csv --replay-load-slot 0 --user COPIED_SAVES
```

`--import-undefined PERSONS` is a separate, logged, replay-only verification
adapter for the explicitly approved original-uninitialized PERSON+24 field.
It validates the whole snapshot's actor identities before writing that field
alone; no coordinates, HP, RNG or deadlines are imported. It cannot be combined
with saved-start replay (the real SAVE already owns those PERSON bytes).
Original actor phase clocks may come from the strict `actor-clocks.csv`
companion; missing interior samples are errors, not invented clock values.

## Reference Resources

- Original retail v1.01F executable and assets (local, ignored).
- `ref/rust`: lodilorenzo/cybermage-darklight-re-awakening; reference, not authority.
  Check individual file licences before adaptation; cite `[rust: path]` and re-check
  behaviour against the original binary.

## Next Tasks

### RE Investigation

- [x] Ph1: identify retail LE layout and import Watcom globals.
- [x] Original-code oracle: title, new game and turning frames; zero native game hooks.
- [x] Resolve first NPC physical-steering/combat divergence without masking: named combat checkpoint gate above.
- [ ] Extend original combat/melee/vehicle/shop and multi-scene route comparisons.
- [ ] Compare additional scenes/moving geometry/extreme pitch and live UI variants.

### Port Fixes

- [x] Verify scoped full-engine Android title/newgame/walking and desktop GL presentation, with limits above.
- [ ] Complete a continuous original-cross-checked game playthrough.

### Documentation

- [ ] Record architecture graphs once execution paths have been traced.
