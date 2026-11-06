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
| target | Implemented independently of pixels | `make -C port -f tests/render.mk B=build/render-test scene check`:packed/masked/centre-column eligibility | Full combat target-order oracle ([AI](docs/re/ai.md)) |
| render_sw | Implemented; two scoped retail views | Render gate and `tools/compare_frames.py --rect 120,90,400,300`:zero differences outside hand footprints | Moving props, other scenes, painter ties/extreme pitch ([render](docs/re/render.md)) |
| render_gl | Enhanced backend present; integration gates in progress | `tests/render_gl.mk` provides mesh/decoder/offscreen scene gates; no completed runtime result claimed here | CLI/presentation integration and GPU acceptance |
| classic input/Sim | Integrated; movement trace MATCH | `make -C port B=build/Integrator check`; executable `--replay captures/movement/frames.csv`:106 frames MATCH;2400-frame keyed smoke exits0 | Classic mouse/joystick, broader collision/flight/vehicle oracle ([Sim](docs/re/sim.md)) |
| modern controls | Opt-in action/touch/controller layer | `make -C port -f tests/controls.mk B=build/Controls controls-check`:six transport/game/physics gates pass | Physical-controller usability; Android runtime gate separate ([controls](docs/re/controls.md)) |
| save/load | Codec and live subsystem bridges integrated | UI gate:retail SAVE010 all246045 bytes and GAMENAME all288 bytes round-trip; `test_sim_loop`:normal/AUTO/scene/death/difficulty pass | Full original-save playthrough and demo format ([UI](docs/re/ui.md),[Sim](docs/re/sim.md)) |
| Events/inventory/vehicles | All90 event dispatches implemented | `tests/events.mk` gate; `events_trace` matches door252, ladder425, corridor592 checkpoints | Reducer positions are inputs; no integrated multi-scene/vehicle route parity ([Events](docs/re/events.md)) |
| AI/NPC combat | Implemented; actor/RNG comparison still diverges | `tests/ai.mk`; `tools/compare_actors.py` / `tools/compare_rng.py` expose first divergence | Undefined PERSON+24 deviation; physical steering divergence; no full firefight parity ([AI](docs/re/ai.md)) |
| player fire/projectiles | Integrated; scoped spell-flight parity | `tests/projectiles.mk` gate; `tools/compare_projectiles.py`:616 checkpoints/17 active match; CollFire120 original sweeps match | All52 missile paths, full melee/firearm/vehicle and post-draw counter parity ([AI](docs/re/ai.md)) |
| UI/HUD/shops | Integrated; six named pixel comparisons match | `tests/ui.mk` gate; title/options/HUD/composed-newgame/pause comparisons0 differences | Shops/vehicles/live weapon variants need original traces; settings/arena work landing ([UI](docs/re/ui.md)) |
| timer/RNG/fixed math | Implemented integral semantics | World gate:6144 trig values,106 angle/distance,60 RNG,16 rotation vectors match; movement trace MATCH | Whole-game RNG call order still under comparison |
| audio/speech/movies | Integrated native PCM/OPL/HMI/TGV | `make -C port -f tests/audio.mk check`:38 songs validate; first30 frames of three movies match FFmpeg | Not exact SOS driver/mixer/voice-allocation parity ([audio](docs/re/audio.md)) |
| platform/Android | SDL desktop and full-game dual-ABI APK verified | `port/android/build.sh` from frozen HEADc699ea4; API36 emulator:touch NEW→scene1→virtual-stick walking, no crash | Physical-device/long-play acceptance; [APK hash/screenshots](port/android/README.md) |

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
- [ ] Resolve first NPC physical-steering divergence without masking actor/RNG comparisons.
- [ ] Extend original combat/melee/vehicle/shop and multi-scene route comparisons.
- [ ] Compare additional scenes/moving geometry/extreme pitch and live UI variants.

### Port Fixes

- [ ] Close full-game Android, GPU presentation and newly landing integration gates.
- [ ] Complete a continuous original-cross-checked game playthrough.

### Documentation

- [ ] Record architecture graphs once execution paths have been traced.
