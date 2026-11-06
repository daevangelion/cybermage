# Bootstrap: symbols and original-code oracle

## Symbols / addresses

`tools/debug_labels.py orig/retail/CM_HI.EXE labels.csv` reads the Watcom v3
trailer. There are 1,718 globals (1,076 code, 642 data), 1,677 unique addresses;
41 same-address aliases remain in primary-label comments. `labels.csv` has dosre's
`module,addr,name,comment` columns. Module `CM_HI.EXE` means retail v1.01F, never
the demo. Overloads get an address suffix; original mangled names and source paths
are preserved. Parser layout reference: `[rust: tools/wdbg.py]`.

```
.venv/bin/dosre funcs --module CM_HI.EXE
.venv/bin/dosre dis --module CM_HI.EXE 0x68abf 18
```

Code object 1 is `0x10000 + 0x79368` bytes; data object 2 is
`0x90000 + 0x17AB80`. These are LE link addresses, not file offsets. The Rust
host uses `le::rt(link)` for runtime memory access. Runtime libraries (Watcom CRT,
HMI SOS) are labelled, not ported. Demo source fixup `0x06` is a known dosre gap.
`dosre funcs --module CM_HI.EXE` completed: 417 functions, 90,246 code bytes,
zero invalid-opcode functions. PLAYTGV.EXE is also LE (430,381 bytes, header at
file offset `0x2C80`) and is configured independently.

## Oracle usage

```
tools/oracle.sh title
tools/oracle.sh newgame
tools/oracle.sh turn
```

Requires retail assets in `orig/retail`, the ignored reference checkout at
`ref/rust` revision `6c99942342913bb18cf53df7240bc5725d4719a4`, Cargo, its locked
dependencies, and Pillow in `.venv`. The Linux release host compiled without
platform patches; initial build took 74 seconds. Cargo inputs/cache are reused
(`ref/rust/target`, normal `~/.cargo` cache). Each command runs under
`tools/run_capped.sh`, including incremental build and interpreter.

The tracked `tools/oracle-original.patch` applies **only** to the ignored reference
checkout. It adds `CM_ORIGINAL`:

- `native::install` is bypassed in its entirety (not a fragile list of hook names).
- Hook table must be empty at startup and each screenshot.
- Differential verification, realtime scheduling, native scene/vehicle overrides,
  scripted modern desktop/controller input are forbidden.
- DOS file reads do not run the reference's title-art rebranding patch.
- Screenshots use raw VGA presentation, not native world/UI composition.
- Automatic startup keyboard-release injection is disabled; the scenario supplies
  its own explicit release event.
- The headless controller-input writer and native settings-key interception are
  bypassed too. `CM_ORIGINAL` asserts that no controller scripts or hooks exist.
- The original EXE receives `-x`, its own no-sound option, so no sound-card driver
  emulation or replacement of Sounds methods is needed.

Every successful interpreter log must contain:

```
ORACLE original-x86: native::install bypassed; hooks=0; asset patches=off; raw VGA
```

This executes the original x86 game and vendor runtime, **not a DOS/4GW machine**:
DOS, DPMI, BIOS, VGA and device I/O are HLE. The headless host skips external
PLAYTGV child processes; intro/ending movie fidelity is outside this oracle.
Sound is intentionally off; the original HMI timer path still runs. Confirmed in
retail disassembly: main `0x1F764–0x1F77F` recognizes `-X`/`-x`, setting
`noSoundFlag` (`0x1E38C8`) at `0x1F743`; BootSOS `0x66666–0x6669D` then calls
original `sosTIMERInitSystem` and `sosTIMERRegisterEvent` with `EAX=40` and
`cm_sys_handler=0x67C06`. A run without `-x` failed sound hardware/config setup.
`CM_VERIFY=all` is not suitable: it still installs native functions and verification
state, while not every host hook is a differential game hook.

Outputs go to ignored `captures/<scenario>/frameNNNNNN.png`, matching `.log`, and
`run.json` (EXE/binary/patch hashes, reference revision, input script and timings).
Optional state appears as link-address hex rows in the matching log. Each capture
replays from boot into a fresh writable overlay; original assets are symlinked,
not copied or modified. Captures are RGB PNG, 640x480; original mode expands each
6-bit DAC component as `v << 2`, matching the C port's comparison convention.

## Scenario format

Pass a built-in name or an explicit file path. Seconds are interpreter time.

```
ips 60000000
key 8 9c                 # raw set-1 release byte; primes original ISR latch
key 9 1c                 # Enter press + release
hold 16 18 e4d           # extended Right, press at 16, release at 18
capture 19
state 207473 4           # optional link address in hex, length decimal or 0x...
```

`key` sends the specified byte followed by byte|0x80, as the reference CLI does;
a release-only byte therefore sends two release bytes. `hold` supports nonextended
or `e`-prefixed extended set-1 make codes. Captures rerun the entire script up to
each capture time. The current backend supports one memory span per checkpoint.
Unrecognised directives/ranges fail closed.

## Observed captures

`tools/oracle.sh title` produced the original, unmodified title/menu with NEW
highlighted, at reported interpreter time 9.05 s, VESA mode `0x101`,
544,005,662 instructions, `_CMTimerTicks=266` (`0x010A`). The native timer-event
list is empty: timing is coming through the original HMI code/IRQ path.

`tools/oracle.sh newgame` produced the first-level view (blue room/closed door,
blue-gloved hand, original HUD) at 16.00 s, VESA `0x101`, 961,005,662 instructions,
`_CMTimerTicks=544` (`0x0220`), also with zero hooks and an empty native event list.
Both images were opened and inspected, not merely checked for file existence.

`tools/oracle.sh turn` replayed checkpoints at 16.00 and 19.00 s. The second
image shows the room turned to its corner/wall consoles rather than the initial
door-facing view; `_CMTimerTicks=664`, 1,141,005,662 instructions. The repeated
16-second checkpoint comes from an independent boot, not a restored state.
Its PNG is byte-identical to the newgame PNG (SHA-256
`72d22ccbf42f09be70a62fd120458b102a529ab79a0b16b856ad7f0be42bc3bd`).
This demonstrates repeatability for that checkpoint, not cycle-exactness or
determinism of every scenario/platform.

## Timer and RNG evidence

Verified in the **original retail binary** with dosre (using the pre-download
`/tmp/cmvet/dosre.toml` path):

- `CM_SYS_INIT` at `0x066EF4` calls `avail_mem`, then tail-jumps to `RANDOM_SEED`
  at `0x068ABF`.
- `RANDOM_SEED`: `mov ebx,0x46c; mov ax,[ebx]; mov [0x97481],ax`.
  Thus the seed source is the low BIOS tick word, not host wall time.
- `[rust: crates/game/src/native/misc.rs]` identifies the three RNG words at
  `0x9747F/0x97481/0x97483`; RANDOM at `0x068AD0` accesses the first directly.
  This oracle does not replace any RNG call.
- `BootSOS` at `0x66680` sets the HMI event rate to 40 (see original no-sound
  branch above). `[rust: REVERSE.md, Timing]`: `CM_ISR8` at `0x067C06` is this
  callback, not an IRET ISR; `_CMTimerTicks` is at `0x207473`.

The headless interpreter advances time by executed instructions / configured IPS,
2000-instruction batches; CLI inputs are delivered at 0.05-second boundaries.
PIT and BIOS time use this emulated clock (DOS date fixed to 1995-11-29), not
wall time. Same EXE/assets/config, IPS and scenario should reproduce timer/seed
ordering, but this is not cycle-exact DOSBox timing. Asynchronous screenshots can
land mid-render; exact frame-boundary capture and repeated-run equality remain
separate measurements, not assumptions. Changing IPS changes the seeded tick and
work performed between timer events.

## Physical mouse and original configuration

An oracle scenario may set the **host INT 33h driver**, never game fields:

```
mouse 11.25 304 112 0
mouse 12.25 304 112 1
mouse 12.50 304 112 0
```

The fields are emulated seconds, X `0..639`, Y `0..239`, and the DOS button
mask `0..3` (left=1, right=2). Events are delivered at the same 0.05-second
boundaries as keys. Absolute position changes accumulate driver mickeys;
make/break edges update the INT 33h press/release counters. The original's
AX=3/5/6/0B calls read and consume that device state. The CLI equivalent is
`--raw-mouse TIME:X:Y:BUTTONS`, available only with `--headless`.

Mouse input is **not enabled by injecting the original routing flag**.
`mouse-config.keys` uses title O → Options → Input Device → Right → Enter →
Escape. The original `OptInputSlider` (`053538`) chooses device 1;
`054167..054182` asks `Mouse::Installed` before setting Xox+E3510, and the
normal options exit calls the original configuration writer (`05422D`).

```
tools/oracle.sh tools/scenarios/mouse-config.keys --frame-trace
tools/oracle.sh tools/scenarios/mouse-movement.keys --frame-trace \
  --import-config captures/mouse-config
```

The wrapper captures the original final `CMDATA.CNF` bytes beside `run.json`,
with `config_file` and `config_sha256`. `--import-config DIR` copies only that
file into each clean writable overlay and records its SHA-256 per checkpoint
in `imported_config`; it neither edits settings nor loads a game. An unchanged
imported configuration may be captured again with the same hash. Seed the
port's `--user` directory with those original bytes when comparing mouse
routing. Save import/export remains separate and does not copy configuration.

With `--frame-trace`, `mouse-input.csv` observes original `Mouse::ReadStatus`
at `06BB24` **before** its INT 33h polls:

```
sequence,frame,ticks,caller,x,y,buttons,mickey_x,mickey_y,press_left,press_right,in_device,mouse_present,joystick_present
```

Keep all polls in sequence, including several polls per game frame. AX=0B
consumes the mickeys and AX=5 consumes press counts, so absolute coordinates
alone cannot reproduce the input. Accumulators are recorded before the BIOS
truncates them to signed 16-bit return values. The final three columns audit
the original's routing; **they are not replay input and must not be written
back into game state**. Native transport converts DOS Y to 640x480 input Y×2
and DOS right bit 2 to SDL right bit 4, but preserves DOS mickey/count semantics
in dedicated input fields rather than feeding enhanced relative-look deltas.

Observed producer `48bd0c88cf40899a7c39d73399259860f26acdc2d80a0d9439d4fee509319ff4`
with patch `962e4411bd2d1c549db75a1241b7f876b674015ef34e44e4c0029af947166e4b`:

- `captures/mouse-config/CMDATA.CNF` is the original-written 172-byte file,
  SHA-256 `a40f5572b7b99fde71274bfa54fccc5d24a16c6f684a2e96dfb42424b44b0b7d`;
  its device DWORD at offset 4 is 1. The first observed poll returns through
  `054196`, with original `inDevice=1`, Xox mouse flag 65535 and joystick flag 0.
- `captures/mouse-movement` imports exactly those bytes. Its 154 pre-frame
  records and 921 ordered polls show position `(819200,1753088,131072)` →
  `(769376,1589249,131072)`, heading 49152 → 47488, and a real jump peaking at
  Z=152320. One left press count and one right press count are observed.
- Normal gameplay polls return through `057F38` during drawing, **after** that
  frame's movement. For example poll frame 16 has mickeys `(140,70)`; pre-frame
  17 still has the old pose, and pre-frame 18 contains the resulting turn/move.
  Do not apply that draw poll to frame 16's earlier input phase.

The required default title/new-game/movement/door-event recaptures are recorded
in `captures/raw-mouse/comparison.json`: all four are unchanged against the
corrected BIOS-modifier producer `99bd75b2…`. Title has a pixel baseline only;
the other three match whole frame CSVs and pixels. This compares against the
previous **corrected** modifier trace, not the already-explained obsolete
movement trace whose Shift byte was always zero:

```
.venv/bin/python tools/check_oracle_bda.py --label raw-mouse \
  --baseline-receipt captures/bios-modifiers/comparison.json
tools/run_capped.sh .venv/bin/python tools/test_oracle.py
```

The fourteen Python scenario/save/config/baseline/trust unit tests pass. The
joint producer's three Rust mouse tests cover bounded script arguments,
INT 33h consumable counters/mickeys, guest warps and programmed driver bounds.
These are hardware and transport observations, not a claim that the native
mouse replay comparison has passed.

The final producer additionally canonicalizes a scripted `-0.0` timestamp to
`0.0`, so equal-time make/release events retain their declaration order under
the timestamp sort. Its regression failed before the parser correction; all
four Rust mouse tests and the trusted release build then passed
(`artifact://3310`). The complete patch still contains all six existing
oracle members, including the draw/menu observers. Final binary:
`63db872af4a47c820456176cc5945f4581107ea3748443565e6383e4725fd91a`;
patch:
`ad772577b40a93d3bc9b5b5cd3eaead6b5cbe9ae3d4bdd7bc169698c801fe4ea`.
`captures/Controls-mouse-producer/profile.json` records every source-member hash.
The earlier `48bd…/962e…` captures above retain their own producer identities.

## Frame traces

```
tools/oracle.sh newgame --frame-trace --trace-range x:e33e8:24 --snapshot
tools/oracle.sh movement --frame-trace --trace-range x:144ae8:136
port/build/cybermage --headless --data orig/retail --replay captures/movement/frames.csv
```

`--frame-trace` writes `captures/<scenario>/frames.csv` on the final checkpoint
run. The observer checks original instruction addresses before executing them;
it installs **no hooks**, patches no guest instructions, and changes no guest
registers, memory, time or input. Entry is `CM_HI.EXE:01B9DC`
(`Xox::HandleNextFrame`); state in row N is **before** frame N. Row N+1
therefore checks frame N's result. The final row may represent a frame still in
progress when the scenario stops; do not interpret it as a completed result.

The first line is `CMTRACE1,<range-count>`; zero to eight lines follow:
`range,x|l,<hex-address>,<decimal-length>`. `x` means offset from the actual
Xox `this`; `l` means LE link address translated by the oracle loader.
At most 65,536 range bytes may be recorded per frame. The next line names the
columns; subsequent lines contain decimal integers then lowercase ASCII hex:

| Columns | Meaning |
|---|---|
| frame,ticks,previous,delta | sequential frame index; entry `_CMTimerTicks`; Xox+144B68; actual clamped +144B6C sampled at 01BA1F |
| limit_time,limit_flag | entry bp_LimitTime; computed elapsed >=2 gate |
| object,person | player object/person indices |
| x,y,z,heading,pitch,body | player +61418/+64080/+78D8C/+7E65C, +E33F8, +130D1C |
| kblatch,bios_shift | ISR latch 97462 and BIOS flags 417, sampled with input |
| object_z | player +66CE8 (shape/height parameter, distinct from floor_z) |
| camera_u,camera_v,camera_z,camera_angle,horizon,scene | +E33E8/+E33EC/+E33F0/+E33F4/+E33FC/+130CEC |
| keys,input_keys | 128-byte `_kbScanStatus` snapshots: frame entry and actual HandleKeyboardEvents entry (012DD4) |
| remaining columns | raw memory spans, in the header's range order |

The separate input sample matters: an IRQ can deliver a key after frame entry
but before input processing. The replay consumes `input_keys`, not a fabricated
make/break sequence reconstructed from held keys. Elapsed simulation ticks are
replayed per render; do not turn the trace into a fixed-step catch-up loop.
Replays compare the port's **unmodified computed** player state before each
step and report the first divergent frame/field/expected/actual value. Recorded
coordinates must never be copied over computed simulation results to obtain
a passing comparison.

`--snapshot` additionally writes `scene-start.xox` (0x144B80 bytes from the
actual Xox) and `scene-start.persons` at the first frame entry. PERSON format:
eight-byte `CMPERSON`, little-endian u32 count, then count records of u32 index,
u32 runtime pointer, and 0xC0 original bytes. This is diagnostic evidence, not
a native struct serialization or a replacement for loading the retail scene.
All trace/snapshot files remain ignored under `captures/`.

`--actor-trace` (with `--frame-trace`) also writes `actors.csv`, one row per
PERSON at each frame entry: object coordinates/heading, PERSON state/status/HP,
target, animation/cursor, object active flag, and the three original RNG words.
`seed.csv` records `bios_tick,icount,time` at original RANDOM_SEED entry
`CM_HI.EXE:068ABF`, before it copies BIOS word `0x46C`; this is a source
measurement, not a seed inferred from a later checkpoint.

Actor traces additionally write `actor-clocks.csv` with
`frame,fast_ticks,normal_ticks`, the actual `_CMTimerTicks` reads at
`0348D0` and `0348F7`. Native replay consumes that named companion as external
clock input, never replacement actor state or deadlines. Missing middle rows,
duplicate/out-of-order frames, overflowing values and extra samples fail closed;
only the terminal pre-frame checkpoint may lack an unfinished UpdateObjects
sample. Older captures without the companion retain the frame-entry clock
approximation, not measured actor-clock evidence. CMTRACE1 has no producer-version
marker, so a deleted companion cannot be distinguished from an older capture.

Replay preserves the entire measured BDA `bios_shift` byte. Keyboard movement
tests Shift bits0/1 itself; replay must not discard Ctrl bit2 (the corrected
movement fixture holds it on frames71..75).

`--rng-trace` logs original RND/RANDOM caller addresses, frame, bound and RNG
words; `--steering-trace` logs NewMONBound decision inputs at `0x46507`.
`--collision-object N` logs CollObject entry (`0x435EC`) proposals, source
geometry and every candidate in its four OMap chains to `collision.csv`.
Candidate zero/node zero is the entry marker, not a contact. `--watch LINK_HEX`
uses the host's existing read-only byte write-watch; heap addresses are runtime
minus relocation `0x400000`. All observers preserve guest registers, memory,
instruction/time accounting and the zero-hook original execution contract.

The wrapper verifies the complete source checkout before building: it accepts
only pristine pinned HEAD (then applies the patch) or exactly HEAD plus the
patch, including the new observer source. Staged changes, unrelated edits,
untracked files, extra hunks in an allowed file, changed file types/modes and
modified observer code are rejected. A temporary Git index constructs the
expected patched tree without changing the reference's index.
`tools/run_capped.sh .venv/bin/python tools/test_oracle.py` exercises pristine
application, idempotency and the unrelated/untracked/observer/staged negatives
(four tests passed).

The initial observer captures were superseded after finding an unconditional
headless `gamepad::controller_input` call outside `native::install`: it wrote
Xox input-routing fields every 50 ms even with no native hooks installed.
After gating that writer off, `newgame` and `movement` were re-captured from
boot. Current traces contain 117 and 106 frame-entry records respectively.
The movement trace includes changed x/y, heading C000→A800 and a jump with
floor_z 131072..152319. These demonstrate original scenario coverage, not a
claim that the C replay matches.

### MoveWorld input boundary

`--event-trace` requires `--frame-trace` and writes `events-input.csv`.
The read-only observer samples original CM_HI.EXE037CF4 before its first opcode,
using EAX as `this`, and associates the row with the current HandleNextFrame
index. It records actual `_CMTimerTicks`, object/person, x/y/floor_z/shape_z,
heading/body/pitch, scene/current/previous hand mode, stance and key bytes.
It installs no hook and changes no guest instruction, state, input or time.
`run.json` names the additional file and retains producer binary/patch hashes.

This boundary matters: MoveWorld follows Sim, so HandleNextFrame's earlier
position is not generally its input. The Events comparator requires both files,
checks independently computed switch/vehicle identity before accepting the
Sim-owned position observations, and compares event outputs at the next
pre-frame checkpoint. See `docs/re/events.md` for the Earthmother route result.

### Inventory and selector inputs

`--item-trace` writes `items-input.csv` with
`frame,ticks,type,object,source`. CollItem042700 is sampled only for the four
direct CollObject callers (return addresses43898/43B0A/43D7E/43FF0), excluding
recursive bundle grants. Type/object come from EDX/EBX; source is the current
player object. The reducer independently checks the prop's cached disk item
definition, then calls its real pickup implementation.

`--selector-trace` writes `selectors-input.csv` with
`frame,ticks,action,mouse,joystick,hand_x,hand_y`. Action0 samples the device
flags (+E3510/+E3518) and virtual hand coordinates at ProcessSelector01B580.
It records no selector count, index, map, inventory or HP outputs. Actions1/2
are completed external UI save/load operations, observed before the original
six count/index resets at050A8F/0515F8; their device fields are zero.
The often-cited050AC3/05162C are displacement bytes, not instruction boundaries.
Initial pre-frame load is handled by the real save codec, not a replayed reset.
Enter and shortcut confirmation still call ProcessSelector; only a newly
opening selector or an early Escape cancellation lacks that action0 row.

Both options require `--frame-trace`. All observations remain read-only and
use the same source/patch/binary provenance checks as MoveWorld observations.

### Standing-gate recaptures

`tools/recapture_gate.py` mechanically recaptures a manifest's `routes[].route`
from each existing capture's recorded scenario, trace ranges and hash-checked
original save imports. It never overwrites that capture. Supply an isolated
named source archive with `orig/`, `.venv/` and `ref/` linked to their cached
inputs, an empty cohort directory and the twelve core scenarios first:

```
tools/run_capped.sh .venv/bin/python tools/recapture_gate.py \
  --root /absolute/project --source /absolute/named-source \
  --cohort /absolute/new-cohort --manifest /absolute/manifest.json
```

The tool verifies/builds the original producer once, then freezes its executable
and observer patch. An already frozen producer may instead be selected with
`--oracle BINARY --oracle-receipt ORIGINAL_RUN_JSON`; both binary/patch hashes
and the reference revision must match that receipt. Each of three workers runs
through `run_capped.sh` using that immutable executable, never a concurrent
reference rebuild. A barrier completes the twelve core captures before routes.

Every new capture retains actual draw-clock/UI-action observations plus actor,
RNG, item and selector inputs. `pairing.json` compares every old frame column,
and hashes old/new actors, actor clocks, seed and event-input companions where
available. Differences are recorded, not suppressed or presented as identity.
`recapture-results.json` records each worker's exit and log. Only the separate
named native replay gate can establish parity; a successful recapture is not
a native MATCH. Delete raw images only after that native scenario passes.
Use repeated `--only ROUTE` arguments for a narrow retry in a **new** cohort
directory. The complete manifest is still validated; the selected subset is
recorded explicitly and is not a claim that every manifest route was recaptured.
Existing capture directories and receipts are never overwritten.

### Draw clocks and menu actions

Every new `--frame-trace` also writes `draw-clocks.csv`:
`frame,draw,phase,pc,ticks`. Draw ordinals start at0 per frame; phases0/1/2
identify the ordinary primary HUD, SaveGame render, or LoadGame render.
The observer reads the timer at the actual executed instructions:
StatusMonitor016391 (one sample reused for its deadline), energy compare019377,
and conditional energy deadline0193A4. It never fabricates a second read.
Replay consumes the same ordered stream through `replay_draw_begin/clock/end`;
missing, wrong-phase or unconsumed reads fail closed. The unmatched terminal
native draw is not executed or claimed as verified. Old clockless datasets
must be recaptured for integrated draw replay.

`ui-actions.csv` contains `frame,ticks,action,slot,name_hex24`. Read-only
SaveGame05007C/LoadGame050AD4 entry observations are limited to the actual
HandleNextFrame menu callers; slot is EDX and the name is the user's24-byte
gameName input (slot10 has no name). This is separate from the later selector
reset marker: SaveGame0500B7 calls Image1::Render **before** serialization.
The port uses the same world/HUD pipeline for that draw and an ordinary draw,
including actual status, breathing, regeneration, dialogue and countdown effects.
No RNG words, positions, inventory or deadlines are installed from observations.

The paired `Port-save-clock` fixture has317 frame entries. Frame286 selects
slot0 at tick1056, draws the save HUD at1058, then the ordinary HUD at1060.
Its actor, actor-clock and seed files are byte-identical to the earlier
`Port-first-telecom-rng` capture. The native lifecycle matches48,032 actor
checkpoints across316 completed frames, including RNG.

The separate [physical mouse/config boundary](#physical-mouse-and-original-configuration)
defines ordered `mouse-input.csv` polls and original-written configuration
provenance. Its routing audit columns are never installed as game inputs.
