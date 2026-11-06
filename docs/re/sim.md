# Simulation (CM_HI.EXE retail)

| Function | Link address | Contract |
|---|---:|---|
| HandleKeyboardEvents | 012DD4 | scan states to movement/yaw/pitch and contextual jump |
| HandleMouseEvents / HandleJoystickEvents | 014334 / 014584 | fill unset keyboard actions |
| ProcessInputEvents | 014754 | acceleration limiter; contextual jump 014D5E..014E63 |
| HandleNextFrame | 01B9DC | one simulation pass per rendered frame |
| StartClock | 01ED00 | snapshot timer into +144B60/+144B64/+144B68 |
| Turn | 02AD6C | signed-word shortest heading path, half-turn >= tie |
| MoveObject / UpdatePCObject | 033ED0 / 034488 | actor animation/effects; AI slice |
| CrouchTopFlat / CollTopFlat | 0417C4 / 041978 | body adjustment / one-unit ceiling clearance |
| CollFlat / CollSwimFlat | 041B30 / 041C4C | floor tile plus inclusive linked-flat bounds |
| CollObject / FixNxNy | 0435EC / 044090 | nearby objects / oriented FX frame |
| ObjNxNy / PathCheck / NewNxNy | 044B00 / 044EF4 / 044FC8 | object / quarter-cell corrections |
| NewPCBound / MonsterBound | 045AB8 / 045DA8 | bounded path correction then object collision |
| MovePerson / MoveMonster | 046D30 / 047B90 | player / actor physical motion |
| CM_ISR9 / CM_ISR8 | 067B7B / 067C06 | raw keyboard bytes / 40 Hz timer |

## Clock

Original disassembly 01B9EA..01BA49: each HandleNextFrame reads
`_CMTimerTicks` (207473), subtracts Xox+144B68, stores +144B6C. A nonzero
delta updates the prior snapshot, then clamps positive deltas to two. It does
**not** run a fixed-tick catch-up loop. Zero-tick frames still process input.
`bp_limit_flag` (1E3944) is independently elapsed ticks since `bp_LimitTime`
(1E3948), zero unless >=2; a nonzero value updates that snapshot. Animation
and jump-table advancement use this gate, not just the frame delta.

MovePerson yaw 046F2E: clamp yaw to +/-128; heading adds
`yaw * 16 * frame_ticks / 2` (signed division). Horizontal speed clamps to
+/-256; distance is `speed * 96 * frame_ticks / 2`, multiplied by the signed
16.16 trig entry selected by unsigned heading >>5. The horizontal movement
angle +130D10 is temporarily added to heading, then restored.

## Classic keyboard

CM_ISR9 067B7B..067BE5 writes the previous latch byte to a make's scan slot,
then resets latch to 3. Latch starts zero: first ordinary make is swallowed.
Any byte >=E0 sets latch 12; breaks AND the slot with 10 and reset latch 3.
Thus ordinary held/latched states are 3/2 and extended held/latched 12/8.
Both shift make and break only prime latch 3; BIOS modifier flags are separate.
Do not merge normal and extended keys or discard repeat makes. Movement arrows
test mask 5 (held), so a release-only latched arrow does not move the player.

Keyboard yaw ramps per **frame**, not tick: Right first requests -16, then
-32,-48,-64,-96,-128; Left is positive. The request uses the prior KeyRotRate
before advancing it (0137D5..013941). Pitch changes by 32 per input pass, bounded
by pre-update +/-224 comparisons (013121..0131AE).

## Floors / openings

Flat list nodes are {u16 next,u16 object}; head is tile x+(y<<5), with tile zero
terminating immediately. Floor search starts at (floor_z+4800)>>16 and descends
to first nonzero floor panel or level zero. Floors accept z strictly below head
and strictly above current floor. Swimming accepts equal-height flats and
uses the last accepted flat's texture to override the tile panel. Top-flat
height is player +130D1C, other object's height*2, plus argument, minimum 5800;
clamp is `ceiling-height-1`. `[rust: native/simulation.rs]` agrees with the retail
routines; original instructions were re-read, not substituted by native hooks.

PathCheck uses signed arithmetic shifts and signed %4 for quarter-cell indices.
MovePerson's blocked-origin snapping instead first divides by 4000 (signed
truncation toward zero), then clamps quadrant 0 to 4000 and 3 to BFFF.
Active apertures use inclusive centre-coordinate bounds. The original's
small-step doorway blockage is retained; do not apply the reference's optional
1000-depth extension (`[rust: REVERSE.md, Open doorway / small-step collision]`).

`jumptable` 0963C0 (decoded from retail):
8192,6144,4096,3072,2048,1024,0,-1024,-2048,-3072,-4096,-6144,-8192,
-65536,-65536,-65536. Index n reads table[n-1] (the assembly base is 0963BC).
First jump frame writes the entry directly; later frames accumulate
`entry*min(frame_ticks,2)/2`, advancing index only on bp_limit_flag.

## Verification

`tools/run_capped.sh cc -std=c99 -pedantic -Wall -Wextra -Werror -Iport/include
port/tests/test_sim.c port/tests/media_error.c port/src/sim/input.c
port/src/sim/flat.c port/src/sim/fire_flat.c -o port/build/Sim-test` and running
that executable passed first-key swallow, ordinary/extended make-break, shift
latch, zero/one/clamped frame delta, independent animation gate, heading wrap,
inclusive flat edges, water texture override, ceiling one-unit clearance,
aperture centre bounds, invalid map/chain rejection and projectile flat clipping.
These are unit checks, not an oracle player-trace parity claim.

The integrated executable linked with strict C99 flags in `B=build/Sim`.
`--replay captures/movement/frames.csv --state-dump
captures/movement/port-state` completed all 106 original frame records, reporting
the first divergence at frame 57: x expected 814483, actual 814376. State-range
comparison isolated the cause: the original clears +144AEC/+144AF0/+144AF4 at
01C244..01C258 before sampling each frame, whereas the initial C implementation
reused speed 2 after momentum reached zero. That reset is now implemented from
the instructions; it is not a coordinate correction. The subsequent verification
was blocked during linking by the concurrent original-EXE table-reader cutover
(`cm_exe_init`/`cm_exe_table` undefined); corrected replay parity is not claimed.

## Inherited Sim checkpoint (Integrator)

The inherited Sim sources compile with the strict C99 flags in
`B=build/Integrator`. The initial full build was blocked by concurrent Events
work (`talk_texture` and `monitor` unused in `events/world.c`). The existing
`port/build/Sim/cybermage` completed the 106-frame movement trace with
`player_state=MATCH`; this is evidence for that binary, not the blocked new link.

The checkpoint includes player-height initialization, the CollFire sweep and
original-code fixture driver, flight controls/motion, explicit Sim/Events/AI save
bridges, `--user` storage and GAMENAME.USR, missile/SoulFire-before-movement
ordering, player fire/H2H callsites, and BIOS seed metadata. Final integrated
verification below supersedes the older investigation results above.

## Remaining integration limits

Mouse/joystick action translation is not complete. Monster boundary steering is owned by AI's `ai_monster_blocked`;
Sim provides the path/rectangle/FX/radius clipping and collision flags it reads.
The player shape/body height is initialized by `sim_start_scene` from the
LoadScene rule; no snapshot value is fitted into the port.

`main.c` integrates UI, audio, events, actors, rendering and original-frame replay.
Its remaining main-loop integration work is InitPC/restore ordering, modal shop
and death hooks, and scene transitions.
