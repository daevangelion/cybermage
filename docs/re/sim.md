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

`tools/run_capped.sh .venv/bin/python tools/sim_fire_original.py
port/build/Integrator/test_sim_fire` matched 120 unmodified retail x86 sweeps
(40 cardinal/diagonal/boundary angles × actor/panel/rotated-panel fixtures).
The standalone native fixture was compiled with strict C99 flags, function/data
sections and linker garbage collection from fire.c, fire_flat.c, slide.c,
world/math.c and world/sight.c. This verifies those synthetic sweep cases, not
all missile lifecycles or a retail firefight.

## Automatic scene persistence

Retail `CM_HI.EXE` AutoSave `051640..0519FF` is **not** SAVE*.DAT:
it writes `AUTO0%02u.DAT`, starts with scene, +5E7A8, +BDEB8, +5DFA0,
then object arrays; it omits player inventory, Sim words and missile/SoulFire
records. The remaining light/event/aperture/switch/fire fields and PERSON tail
follow the regular save order. PERSONs include player index zero. FindAutoSlot
`037AE8..037B12` scans ten autoSlot words for zero and returns -1 when full.
SetMonDiffLevel normalizes NPCs before writing and restores difficulty afterward.
Scene routing must not mistake a regular save for this reduced world snapshot.
MoveWorld type25 `03ACC2..03AD6C`: mode1 loads the current global autoSave
slot when nonnegative (otherwise ordinary transition); mode2 finds a free slot,
writes the **outgoing** scene and then transitions. AutoLoad
`051DE7..051E98` restores current player HP/status, installs the exit event's
destination coordinates/heading, and clears all missile/SoulFire active flags.

## Remaining integration limits

Monster boundary steering is owned by AI's `ai_monster_blocked`;
Sim provides the path/rectangle/FX/radius clipping and collision flags it reads.
The player shape/body height is initialized by `sim_start_scene` from the
LoadScene rule; no snapshot value is fitted into the port.

`main.c` now calls InitPC at boot and new game after source BIOS seeding;
ordinary scene changes save/restore player HP without clearing inventory.
StartClock initializes both actor animation deadlines. Save load imports live
Sim/Events/AI state and refreshes the player transition stash, rather than
overwriting loaded HP with an older stash. UI shops pause simulation, retain key
releases, and render their modal after the world. Live hand rendering consumes
Port's animation/aim state. Countdown expiry and player death route through the
60-tick world-only wait, HIPAK1 melt and UI_DEATH menu; endings use the real TGV
decoder. Automatic transition snapshots use their reduced AUTO byte format,
and ordinary/new-scene checkpoints write hidden SAVE010 plus GAMENAME.USR.

`--controls modern` is opt-in, reads optional `controls.cfg` from `--user`,
translates a persistent physical input struct into per-frame actions, and
keeps menu/shop/vehicle contexts separate. Original traces reject modern mode.
Classic keyboard/input/movement remains the default. The enhanced map reads
real world topology; it is not a claim of DOS AutoMap artwork parity.

Saves normalize living NPC HP to difficulty1 and restore the selected difficulty;
loads apply the current menu difficulty after overlays. AutoLoad's scene9-from4/5
event adjustments and GiveKey subtype overrides are wired without treating those
subtypes as quantities. Normal load constructs the complete scene/AI reserved
pools **before** applying saved objects and PERSONs. The lifecycle regression
caught the former data-only restore's 1088-versus1113 object-count mismatch:
saved missile objects were outside the incomplete freshly loaded pool.

Remaining fidelity limits include mouse/joystick **classic** translation.
Neither a synthetic CollFire sweep nor
the movement trace establishes full combat, shop, death or multi-scene parity.

### NPC pickup-contact investigation

The first actor steering comparison differs at frame20/object970 before RNG
diverges: original NewMONBound enters with collision count/flags0/0, native1/2.
CollObject `043863..0438C2` gives targetable2 props their own branch:
PROP runtime+6 (disk placement+26, LoadPROPS0245AA) must be nonzero;
player contact calls CollItem, NPC item kind16 is ignored, and other NPC
contacts pass the candidate **object** to ObjNxNy. The generic branch
`0438C4..0438D2` also passes the candidate object: its push at0438CB shifts
the candidate saved at `[esp+4]` to `[esp+8]` before0438CE reads it.
Both read-only observers matched all22
entry fields at that call: proposal1000124,884972 and only node431→object877,
a kind15 cash prop. The source-correct branch now uses the object, skips NPC
kind16 contacts, and leaves aux0 decorations on the generic branch.
`test_sim_collision` failed the original flags0 expectation before this fix
and subsequently passed its cash/kind16/decoration regressions.
`sim_state.collision_observe/context` is a normally-null
read-only diagnostic seam; it performs no core I/O or state substitution.

The generic branch's former chain-index argument independently caused combat
frame795: decoration918 clips X to944128-7680-7680-1=928767.
The isolated exact-contact fixture failed before the candidate-object fix and
passed afterward. The original1299-row combat replay then first diverged at
frame885 (Y expected1061517, native1061226), rather than795. Target comparison
still reports21 mismatches across1298 completed frames (13 original nonempty),
first native917/original918, empty versus[1069]. This is not a target MATCH.

## Integrated verification (Integrator)

The strict full `make -C port B=build/Integrator`, followed by
`make -C port B=build/Integrator check`, passed. The gate includes actual
main-loop InitPC/HP/inventory initialization, normal save/load, difficulty
normalization, ordinary and AUTO scene roundtrips, death wait/melt/menu, and
GiveKey fresh-slot/full-inventory tests. It also runs the six modern control
tests, 120 original CollFire sweeps, media/resource bounds and all eleven scene
loads plus original math/sight vectors. Negative-fixture ERROR lines are expected;
the command exits zero.

The newly linked executable then completed all 106 movement trace records with
`player_state=MATCH`, writing computed combat/player state beneath
`captures/movement/Integrator-state`. No recorded position/RNG/HP was installed.

The keyed headless smoke completed 2400 frames (60 seconds at40Hz), starting
at the title, selecting new game and walking/turning/firing in scene1, with
exit0 and no crash. It dumped all2400 indexed/palette/RGB frames and2399
game-state records (frame0 is the title). Computed state contains237 distinct
XY positions,30 headings and42 frames with active missiles; energy decreased
from40 to20. The final state is frame2399/tick2399. Inspected RGB checkpoints
show the real rendered scene and HUD, not empty frames.

Evidence: `captures/Integrator-smoke/run.json`, selected
`frame000000.png`, `frame000001.png`, `frame000120.png`, `frame002399.png`,
and all computed `state/*.state`. The tested binary SHA-256 is
`ebfdb62296e9c1ef8469e390f7b3d854b1a53e29c6fb51b0c8f12f2fefdb3bb6`.
After verification,7200 job-owned raw `.ppm/.idx/.pal` files
(2,950,999,200 bytes) were removed to respect the disk limit; the selected PNGs
and state/evidence remain. Builds reused the isolated `port/build/Integrator`
objects and existing SDL/Python/original-asset inputs without downloads.

Interactive desktop smoke was unavailable: both DISPLAY and WAYLAND_DISPLAY
are unset. The headless SDL input/control tests are not described as an
interactive desktop run.

Review regressions: normal load preserves the ISR latch/BIOS Shift flags and
updates restart_game only on success. RELOAD selects the validated restart slot
(including hidden10), and a failed requested load retains its menu/selection.
The real title-menu Training route selects scene11 (100HP/energy,60cash).
`test_sim_loop` caught the latch loss and stale restart slot before their fixes;
the extended lifecycle/menu tests subsequently passed, including missing-save
retention and Training through scripted title-menu input.

Malformed saved motion/mode/stance/body/swim words now fail at individual
`0x6D4+4*i` offsets; fire fields fail at `0x34F09..0x34F15`.
Physical map/maximum-input bounds keep subsequent signed arithmetic defined.
These checks run before replacing a scene. A later failed overlay/import
disarms the incomplete world and retains a Load menu returning to title, not
Resume of partially loaded state. Hostile values and preflight/deep failure
are covered by `test_sim` and the actual-main lifecycle regression.

CollFire's zero-tick contact regression reproduced signed overflow at
`fire.c:207` under UBSan (`INT32_MAX + 32768` from the original zero-divisor
sentinel). Sweep position/height additions now use explicit unsigned32 wrap.
The zero-tick inside-target test and all120 original-x86 sweep cases pass
under UBSan with recovery disabled; `make check` includes the zero-tick
sanitizer regression. This preserves the original wrapped result rather than
silently skipping zero-tick simulation.

### Swim rule recovery

LoadScene defaults SwimCheck0, then enables scenes0/1/4/6/7/8/9/11.
The respective(panel,art,damage) triples are(22,21,0),(45,44,0),(36,35,3),
(37,36,0),(30,29,-1),(32,31,2),(64,60,0),(32,31,0), from
`0116CF..0116E8`, `011729..011755`, `01186C..01188D`,
`011911..011969`, `011997..0119A9`, `011A70..011B19`,
`011BAE..011BD6`, `011D08..011D30`.

HandleNextFrame `01CAC0..01CD80` runs water contact before environmental fire.
Contact is excluded while jumping, flying or in a vehicle. Entry plays sample4
and sets the shared swim/fire damage deadline to ticks+20+RND40. Contact sets
swim/bob flags and the original -4800/-3000 stance. Positive damage calls
DamagePerson((1+RND6)*damage,100,125); negative damage heals injured on-foot
PERSON0 by RND10, clipped to maximum, with sample27. Both reset the deadline
to ticks+40+RND60. Exit clears swim and requests standing. Moving eligible
hands maintain loop4; stopping checks sample3, draws its random volume and
aborts loop4. These are executable rules, not capture-derived constants.
The sample3 handle is AI's `pc_sound` (Xox+144A3C), shared with player pain;
the swim loop alone owns144B54. The sound-enabled shared-slot regression failed
with the private splash slot and passed after its removal.

The reducer is implemented in `sim/swim.c`, called before the fire block;
scene setup feeds the same four Swim* values to AI. `test_sim` first rejected
the missing scene0 water configuration. `test_sim_swim` then failed the absent
player-water transition before implementation. Both pass now, including exact
entry/heal RNG words/deadlines, crouch/swim entry and exit, healing and harmful
water with real retail audio/tables. `test_sim_collision` and hostile-save
regressions passed in the same focused gate. Original multi-frame water parity
remains a separate Sim2 oracle gate.

`tools/run_capped.sh .venv/bin/python tools/sim_swim_original.py
port/build/Sim2/test_sim_swim_original` compared160 frames of the unmodified
retail `01CAC0..01CD80` block with `sim_swim_step`:117 wet frames and7 entries,
including jump/flight/vehicle exclusions and wet/dry transitions. Swim flag,
stance, bob request, body height, entry deadline and all3 RNG words matched
at every frame; HP remained40 because scene1 water damage is zero. The sound-off
fixture does not cover the conditional splash RNG or harmful/healing water.
This uses reduced scene1 geometry with real water panel45 at
(22,18) and dry panel81 at(26,18), explicit BIOS seed44 and the oracle's no-sound
input. It is an isolated rule comparison, not a boot/playthrough-water claim.
Passing argument7 runs the same160-frame reduced fixture with scene7's30/29
panel/art, healing damage-1, alternate body flag and initial HP30/40. It also
matched all frames, including native original RND10 healing and RND60 deadline
resets. Positive damage and enabled-sound shared-slot tests remain native-only
regressions, not original-executed parity claims.

The original boot route `tools/scenarios/scene1-water.txt` reaches the upper
river through ordinary keys (including the cave dialogue and diagonal-frame
maze). `captures/scene1-water/frames.csv` has2033 entry records;127 have swim1,
frames1906..2032. It records the swim/body span, shared global deadline, RNG,
actor HP and actor phase clocks, with `scene-start.persons` for the explicitly
approved undefined-word input. Compare computed native state with:

```
tools/run_capped.sh .venv/bin/python tools/compare_swim.py \
  captures/scene1-water/frames.csv captures/scene1-water/Sim2-native
```

The comparator excludes the unfinished terminal checkpoint, checks original
pre-frameN+1 against native post-frameN, and rejects dry-only or missing-frame
evidence. Its five controls cover alignment, changed RNG/swim, dry-only and
missing checkpoints.
