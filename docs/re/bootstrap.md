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
