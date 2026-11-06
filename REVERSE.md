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

| Subsystem | Status | Evidence |
|-----------|--------|----------|
| world | not started | map/object/FX ownership |
| sight | not started | pixel-free grid queries |
| target | not started | original centre-column targeting visibility |
| render_sw | not started | faithful 640x480x8 reference |
| input | not started | per-tick classic action state |
| save/load | not started | original SAVE*.DAT byte format |
| platform | in progress | SDL-only OS/file adapter; Port slice |
| timer/RNG/fixed math | not started | exact integral semantics |
| audio | in progress | Audio slice findings |

## Verification Checklist

- [ ] Ph3: 3+ functions cross-checked against execution traces
- [ ] Ph4: 5+ sprites/tiles visually compared to original
- [ ] Ph5: key structure confirmed by emulator state reads
- [ ] Ph6: full game session played without major logic gaps
- [ ] Ph7: 3+ scenarios match frames + checkpoint state; strict C99 build clean

## Reference Resources

- Original retail v1.01F executable and assets (local, ignored).
- `ref/rust`: lodilorenzo/cybermage-darklight-re-awakening; reference, not authority.
  Check individual file licences before adaptation; cite `[rust: path]` and re-check
  behaviour against the original binary.

## Next Tasks

### RE Investigation

- [x] Ph1: identify retail LE layout and import Watcom globals.
- [x] Original-code oracle: title, new game and turning frames; zero native game hooks.
- [ ] world: map tiles, panels, flats, FX/object maps and fixed-point coordinates.
- [ ] sight: RaySight, TileSight, RayMissile grid stepping.
- [ ] target: SetAutoTarget and original sprite candidate visibility list.
- [ ] render_sw: image1.cpp / scale ASM rasterization and palette pipeline.
- [ ] input: tick actions and classic key-state semantics.
- [ ] save/load: original byte serialization and subsystem ownership.
- [ ] platform: VESA/keyboard/files/audio service boundaries.
- [ ] timer/RNG/fixed math: seed source, 40 Hz behaviour, integer wrap.

### Port Fixes

- [ ] Integrate reviewed resource/title and audio slice deliverables.

### Documentation

- [ ] Record architecture graphs once execution paths have been traced.
