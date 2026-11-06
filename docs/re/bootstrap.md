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
