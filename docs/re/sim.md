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
With the explicit PERSON+24 input and measured actor clocks, an intermediate combat
consumer matches player HP/RNG through original pre-frame922, including the
first hit (HP39). First movement divergence is923, Y1063616/1064298;
RNG also first differs there. Target comparison now reports19 mismatches over
1298 completed frames,13 original nonempty, first926/927([] versus[1069]).
The missing producer is original035EF3..035F0B: a nonzero melee hit on the
player raises Xox+1444E8 to at least3 before035F26 DamagePerson. The native
melee path omitted that write, gating off the next frame's three-RND hit
response and sideways push. That producer was corrected in the final cohort
reported below; this intermediate checkpoint was not a target MATCH.

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
tools/run_capped.sh port/build/Sim2/cybermage --headless --data orig/retail \
  --replay captures/scene1-water/frames.csv \
  --import-undefined captures/scene1-water/scene-start.persons \
  --state-dump captures/scene1-water/Sim2-native --user out/Sim2-water-user
tools/run_capped.sh .venv/bin/python tools/compare_swim.py \
  captures/scene1-water/frames.csv captures/scene1-water/Sim2-native
```

The comparator excludes the unfinished terminal checkpoint, checks original
pre-frameN+1 against native post-frameN, and rejects dry-only or missing-frame
evidence. Its five controls cover alignment, changed RNG/swim, dry-only and
missing checkpoints.

The first full native consumer compared2032 completed frames with the explicitly
logged PERSON+24-only import and measured actor phase clocks. It did **not**
match:1214 mismatching checkpoints, first native818/original819 with HP40/38 and
RNG(1895,23161,4392)/(22354,16429,18877). Position first diverged at840
(Y1044843/1043343), and the displaced native route never reached water.
Original wet coverage remains127 frames with HP40. This is an upstream
combat-dependent failure, not successful full-route swim parity; the isolated
original reducer gates above remain separately scoped.

After the source-grounded NPC call-local melee-distance correction
(`202c976`), the full real-route consumer **matched all2033 player entry
records**. `compare_swim.py` compared2032 completed frames,127 original wet,
with **zero mismatches** in position, swim flag/stance, shared deadline, HP and
all3 RNG words. The only snapshot import was the explicitly logged PERSON+24;
actor phase clocks were measured inputs. No coordinates, HP, swim state,
deadlines or RNG words were installed from checkpoints. Scene1 has damage0:
this is no-damage water entry/stay parity, not harmful/healing-water parity.

The environmental-fire product also now uses explicit uint32 wrap for raw
EventAction82 damage payloads, matching original IMUL/LEA01CE0C/01CE1A.
The INTMAX×2 fixture failed under UBSan before the correction and passed after;
it also checks the original minimum hit flash2 and signed deadline comparison.

The lifecycle fixture uses the current explicit-view death API: it derives the
view with `ui_game_view`, installs it with `render_set_view`, and passes it to
all three death steps. The independently executed fixture passes death/menu/save
checks and its second Training boot, including the invalid-CLI negatives.

## Final simulation replay cohort

The source-grounded melee distance/flash fixes and the complete RenderBells
low-HP respiration producer resolve the remaining combat divergence. The latter
was first observed as a missing RND11 at original frame1173 (`01928B..019340`),
before NPC132 changed state; it was not corrected by adding dummy RNG calls.

The freshly built `port/build/Sim2/cybermage` completed:

- `captures/Sim2-combat-collision`: all1299 player entry records MATCH;
 1298 completed target checkpoints,13 original nonempty,zero mismatches.
- `captures/scene1-water`: all2033 player entry records MATCH;2032 completed
 water checkpoints,127 wet,zero position/swim/stance/deadline/HP/RNG mismatches.
- Both the original movement trace and `captures/bios-modifiers-movement`:
 all106 player entry records MATCH.

The complete measured BDA byte also survives all106 corrected movement inputs,
including Ctrl=4 exactly on frames71..75. This is replay input integrity, not
a claim that live native input models all BDA modifier bits.
The only snapshot import is the explicitly logged PERSON+24 input; actor phase
clocks are measured external inputs. No computed coordinates, HP, swim state,
deadlines or RNG words are replaced. Native evidence directories are the
corresponding `Sim2-final/` children.

The complete `tools/run_capped.sh make -C port B=build/Sim2 check` gate exited
zero, including the original-code weapon/respiration/swim/fire comparisons,
swim/fire UBSan checks, lifecycle and controls tests, media and all11 scene
loads. Negative-fixture ERROR lines are expected. The controls physics arena
now supplies an inactive actor state and loads the real EXE tables through an
explicit asset-directory argument; no existing motion/jump/selection assertions
or production hooks were disabled to accommodate swim handling.

### Terminal voice-handle ownership

The game loop clears `actors.pc.sword_hum_slot` after successful credits/ending
movie entry and after `snd_close`, just as death/deep-load teardown invalidates
that hardware handle. It deliberately preserves `sword_hum_sound`; original
KillSound/KillLoops writes at028719/028803 clear the slot, not the sample ID.
Ordinary pause stops only the swim loop (01BC53..01BC83), not the hum.
Movie entry already stops PCM through `snd_movie_begin`→`snd_kill(NULL)`;
the loop does not duplicate that teardown or add Audio→AI coupling.

`test_sim_loop` opens the real credits and ending assets and checks both handle
invalidation and sample-ID preservation, plus pause/death/shutdown. The movie
assertion failed before its fix (`artifact://2225`); after that fix the separate
shutdown assertion failed (`artifact://2230`). Strict app/test builds and the
complete lifecycle fixture then exited0 (`artifact://2233`), including its
existing load/save/death/Training/invalid-CLI negatives. These focused follow-on
checks are separate from the named full-game cohort above.

## Player input and movement phase boundaries

Original keyboard013E39 samples primary fire into144B10 once and consumes
the configured scan's edge bit. ProcessInputEvents01476C/0147DD only admits
running in foot modes1/2/5; held primary melee clears that eligibility at0148C4
before the48/96 acceleration limit is selected. The independent206848
keySwing_Sword pulse does not clear it. Vehicle mode7 never gets the96 cap.

Input/hand/loop processing belongs before acceleration and movement.
The body adjustment is followed by hit shake and aim at01CF71..01D1C0.
JumbleBound01D07D therefore uses the old heading, before MovePerson calls
at01D437/01D62E/01D67B/01D707/01D743/01D792/01D825 apply this frame's yaw.
Sim calls the explicit early `ai_pc_prepare_input` and pre-motion
`ai_pc_prepare_motion` phases; the late PC update must not repeat either.

The named7b427a9 Training guard replay first differed at frame30 despite exact
RNG through all138 completed checkpoints. Measured push3943 and side0 with the
old heading52992 produce(-3679,-1420), exactly the original endpoint
(213330,2010170). Applying yaw first gives heading50944 and(-3885,-675),
exactly the incorrect native endpoint(213124,2010915). This establishes phase
order, not a fitted coordinate correction.

The same named cohort's earned tank-west replay first differs at frame8:
X353536/352768. Original forward displacement2688 is `(48/2+4)*96`;
native3456 is `(64/2+4)*96`, from wrongly allowing mode7 to accelerate past48.
`test_sim_phases` exercises pre-yaw hit/RNG, held versus pulsed melee, the
vehicle acceleration cap and single-sampling of a non-default fire binding.
The swing-pulse case supplies explicit input metadata and is consumer-only
coverage; it does not establish selector/keyboard pulse-producer liveness.

Pending vehicle changes are consumed by `events_frame_begin` at01C1CB,
after missile/SoulFire updates and before keyboard processing. Sim binds its
cached player object/PERSON only after this call. Applying the transition in
late MoveWorld input incorrectly shakes/moves the old foot actor in the
activation frame, then exposes an unmodified vehicle. On namedfa893d7 this
first differed at Training tank-entry frame491, Y1900378/1900544, with matching
RNG through555. The pending transition must not run again in late input.

Vehicle keyboard013D67..013E1F always resets the movement angle to0 and
writes yaw directly to-128 for Right or+128 for Left, with Right taking
priority. It does not read/update foot KeyRotRate, does not strafe under
Shift, and skips dedicated foot strafe/crouch/jump keys. The forward speed
request still honors the side-toggle walk modifier. Tank pitch keys are
skipped by01310B..01311B; other vehicle subtypes retain the pitch controls.
`test_sim_phases vehicle` failed before this dispatch was restored and passes
fixed-yaw, retained-foot-rate, simultaneous-direction, ignored-foot-key and
tank-versus-other-vehicle pitch cases.
F4's menuFlag paths0135C3/013D79 are separate keyboard producers and are not
implemented by this vehicle-motion dispatch.
