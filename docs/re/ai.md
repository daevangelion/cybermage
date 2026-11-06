# AI and combat — CM_HI.EXE retail 1.01F

Authority: `labels.csv`; addresses below are LE link addresses. Reference guide:
[rust: crates/game/src/native/simulation.rs]. The Rust UpdateObjects hook still
executes original x86 AI states; it is not a complete native implementation.

| Original | Address | Contract / evidence |
|---|---:|---|
| SightPerson | 0x02A9B0 | PERSON indices; clear target first; dead status 2 excluded; invisible excluded only for positive range; nonpositive range becomes 0x70000; strict range and front-half-plane tests, then RaySight==1 |
| FindTarget | 0x02AB30 | PERSON order; faction priority starts at 1; rank >= previous AND distance < previous; excludes status 2, states 9/10, hidden; front-half-plane and RaySight==1; writes target +8 and range +C |
| Turn | 0x02AD6C | Signed 16-bit circular angles; halfway tie wraps at >=0x8000; clamp step to target |
| MakeMissileList | 0x02DBA8 | objects 1..<signed object count; active, flags byte3 &3==0, byte1 &4==0 |
| SetAutoTarget | 0x02F150 | Reverse candidate order; object PERSON index at Xox+0x7B9F4; reject PERSON status 2; MONSTER signed byte +0x3C must be >0 |
| TrackTarget | 0x0344E0 | atan64k(source,target), target-mode=2; optional +/-0x1555 chosen by unsigned angle-target-heading difference <0x8000 |
| UpdatePCObject | 0x034488 | hidden +0x5C decrements on animation tick; object flags byte2 bit1 cleared at zero; aura +0x74 decremented when nonzero |
| UpdateObjects | 0x0348C4 | PERSONs 1..count; animation ticks when timer > deadline, then deadline=timer+4 (kind9: +3; kind17 every frame); player effects first; acquisition rotates PERSON index mod10 each call |
| HandleNextFrame hit response | 0x01CF71..0x01D09E | nonzero PCFlash consumes RND4096, RND8192, RND2; vertical view kick, lateral JumbleBound at heading±quarter-turn; no bp gate |
| RenderBells health/energy state | 0x01928B..0x0193B2 | PERSON0 low-HP chance RND(HP/2+2), actual sound3 check/volume RND8192 and shared PCSound; real running-hum gate; then energy recovery |

## Layout used by combat

PERSON: object s16 +0; definition s16 +4; target i32 +8; target distance i32
+C; player distance i32 +10; animation reset s16 +16; animation-end s16 +18;
attack type s16 +1A; state s16 +1C; cooldown s16 +1E; accuracy timer s16 +20;
accuracy deadline i32 +24; movement mode s16 +28; last target i32 +2C; lost-target
frames i32 +30; attack repeats s16 +34; life status s16 +36; melee posture s16
+3C; posture ticks s16 +44; hidden i32 +5C; bob i32 +60, velocity +64,
direction +68; aura i32 +74; recoil i32 +A4/+A8/+AC/+B0; recoil angle u16 +B4;
water u16 +B6; animation state u8 +BC, cursor u8 +BD.

## Target visibility

Reproduce the1299-row combat capture with its measured seed/clock sidecars and
the explicit approved undefined-field input, never coordinates or target results:

```
tools/run_capped.sh make -C port B=build/AimFix
tools/run_capped.sh port/build/AimFix/cybermage --headless --data orig/retail --bios-seed 44 --replay captures/Sim2-combat-collision/frames.csv --import-undefined captures/Sim2-combat-collision/scene-start.persons --state-dump captures/AimFix-target-combat/native --user out/AimFix-combat-user
tools/run_capped.sh .venv/bin/python tools/compare_targets.py captures/Sim2-combat-collision/frames.csv captures/AimFix-target-combat/native
```

The independent `target` module supplies ordered clipped projected props in the
original 640x480 framebuffer coordinates, without pixels or world writes.
`target_query` takes an explicit original view rectangle and drawing camera;
it reads world texture extents/trig, never `render.h` or renderer caches. Its
shared `vis` RayCast/CheckTile/CheckObjects pass preserves generated-wall/flat ordering,
cell countdowns and sprite rotation/near clipping. It includes only original
PACK/INVIS/AURA modes with targetable==1 and inclusive left<=320<=right; MASK is
excluded by visibility. SetAutoTarget scans this list in reverse. The MONSTER
signed byte at +0x3C is not its kind byte at +0x3A or faction byte at +0x3B.

## Native implementation

`ai_init` decodes MONSTER/GSX/ANIMOBJ from SCENE entries 98/99/100, initializes
PERSON HP/loot in original order, reserves twelve missiles then ten SoulFire
objects. `ai_load_tables` reads numeric combat tables from the user's CM_HI.EXE
through `exe.h`; no extracted table blob is bundled. Call it after `ai_init`.
World finalizes its three extra objects afterward. Set `a.events` after init.

`objects.c` implements the original state dispatch 0..14, attack preparation,
attack completion, pain, corpse/drop and special scene transitions.
`blocked.c` implements NewMONBound's steering half 0x46507..0x46A74; Sim owns
physical clipping. `move.c` owns animation/bob/recoil and DragonFly, delegating
physical movement to Sim. H2HTarget 0x19EA4 returns PERSON indices (zero on miss),
unlike SetAutoTarget's object indices (minus one on miss).

Save restore builds the original scene and pools before overlaying saved fields.
When those pools are already constructed, data-only `ai_load_scene_definitions`
and `ai_load_tables` may precede `ai_save_import`. Never run `ai_init` after the
saved PERSON/object overlay: it would reroll attributes and append extra pools.
The explicit save bridge covers the twelve 48-byte missile records, ten 28-byte
SoulFire records, remaining gem countdown and AI-owned `unk_6D4` words6..15;
Events owns event/inventory serialization. Import requires the object/PROP slots
already assigned by scene initialization, never remaps either pool, and
rejects invalid animation frames, negative SoulFire lifetimes and active missile
types outside1..52 at absolute save-file offsets. Gem deadlines use the current
`ai_state.timer_ticks` at import/export, with modulo32-bit arithmetic.
Fire/cooldown/latch and link counters are bounded on restore; underwater remains
an arbitrary signed EventsAction72 payload, not a boolean. Aim X/Y retain their
signed32 range (the original's consumers wrap arithmetic); tank aim is0..63.

`InitSoulFire`0x2DC18 allocates ten runtime **PROP** records, not FXOs:
0x2DC68 reads PROP count `Xox+130D38`, 0x2DCCB stores the descriptor at
`Xox+131BCC`, and object.person plus SoulFire record+8 receive that PROP index.
Only descriptor+4 (object) and +6 (pickup item, initially0) are initialized.
`SetSoulFire`0x2DD28 looks up PROP13 or30 as its art template.
`MakeSoulFire` writes the short pickup item at PROP+6 while preserving the
complete32-bit value in the SoulFire record. The native pools and strict save
identity checks follow those associations; FXO mappings/count are unchanged.

LoadGame051596..0515B3 reconstructs PROP+6 from the low word of each
**positive-active** saved Soul record's value, after reading the pool. Native
`ai_save_import` now performs that same mirror only after all validation;
nonpositive slots retain their prior PROP word. Without it, the real HALLT
checkpoint restored Soul1538/PROP475/value46 but left its pickup word0.
Road NPC64's correct first movement candidate(213603,1573858) was then pushed
toY1573887 by ObjNxNy against that Soul. Item46's kind16 must instead let NPCs
pass (original043878..0438B2); no movement coefficient or RNG change is needed.

`check-soul-load` in `tests/projectiles.mk` compares ten signed-active/low-word
cases with unmodified original05158B..0515B9, counts the actual word writes,
and checks NPC pass-through, unchanged FXO ownership and rejection atomicity.
The optional `tools/soul_load_original.py NATIVE --save SAVE000.DAT --data DIR`
check decodes a supplied genuine save through the normal codec and runs the
same original post-read fixup. The HALLT checkpoint yields one active item46;
no new original-state input or serialized field was added.

Death state8 samples recoil PERSON+A4/B0 **before** MoveObject
(036B98..036BB0). A call that finishes recoil still exits that actor update;
SoulFire may be emitted only on the following update. The genuine Training
fight originally exposed a one-frame-early native soul: original1537 first
active at checkpoint323 with lifetime40, native formerly active at322 with39.
That caused an unintended early type46 pickup and extra RNG, not an item-dice
exception. The corrected guard preserves the actual producer ordering.
The existing corpse attachment PERSON+A0 also indexes the runtime PROP table
at036C1B; it is not an FXO placement. Focused controls reject both old behaviors.

### Target-list checkpoint evidence

`tools/compare_targets.py` compares the original `Image1+4B00C` count and
`+4B010` u16 candidates, in order, against native `--state-dump` target lines.
Capture with `--frame-trace --trace-range x:4b00c:324`; original pre-frameN+1
corresponds to native post-frameN. It rejects missing/truncated spans, malformed
counts, missing checkpoints and reordered targets. Seven synthetic controls
exercise those rules; synthetic matches are not retail parity.

Measured with separately supplied retail assets and BIOS seed44:

| Capture | Completed frames compared | Original nonempty | Target-list mismatches |
|---|---:|---:|---:|
| `captures/AimFix-target-newgame` | 116 | 0 | 0 |
| `captures/Sim2-combat-collision` (named7b427a9 consumer) | 1298 | 13 | 0 |

The named7b427a9 combat replay matches all1299 player-position rows and all1298
completed target-list checkpoints, including all13 original nonempty lists.
The unmatched terminal native state has no original next-frame checkpoint and
is excluded explicitly. This uses measured clock inputs and the approved
PERSON+24 undefined-field input, never coordinates or target-list injection.
Receipt: `captures/Sim2-gate-7b427a9.json`, binary SHA256
`d68735198d408283d0c351de68bcfdd7aee7eab3a882ff5d9c99191530caedbb`.

## Actor oracle

```
tools/oracle.sh combat-actors --frame-trace --actor-trace --rng-trace --snapshot
make -C port -f tests/ai.mk B=build/AI build/AI/ai_trace check
port/build/AI/ai_trace orig/retail captures/combat-actors/frames.csv BIOS_TICK \
  --import-undefined captures/combat-actors/scene-start.persons \
  --rng-trace captures/combat-actors/native-rng.csv
.venv/bin/python tools/compare_actors.py ORIGINAL.csv NATIVE.csv
```

The native harness loads the original assets independently; it does not install
checkpoint coordinates, HP or RNG state. Its replay steps Sim (including
missile/SoulFire updates), player fire, events, UpdateObjects, Hand2Hand and the
pixel-free end-frame target query. The one undefined-field input is explicit
and documented below.
It is the compact default-view, uninterrupted-New-Game diagnostic; UI/save
action ledgers are rejected explicitly. Full-game, saved-game, configuration
and mouse cases use the canonical executable's `--state-dump`, not a second
menu/save implementation inside the harness. Both consumers require the actual
draw-clock ledger and use the shared StatusMonitor/PC draw-state rules.
Fresh ledger-enabled `Standing-original-bee11c5/original/newgame` verifies this
adapter:17,784 actor checkpoints/117 pre-frame states match, and all3,060 logical
RNG calls through completed frame115 match. The callback consumes actual
StatusMonitor16391 and PC19377/193A4 sites, with no frame-time substitution.

`seed.csv` measures the BIOS source at RANDOM_SEED0x68ABF before its write,
not a seed fitted from a later RNG checkpoint. The observed tick is44 at
instruction145,715,835, interpreter time2.4285943666692638s.
Initial words are(3000,44,29999); the scene-1 pre-frame checkpoint is after489
real advances. The call-site observer identified the two formerly omitted
InitPC calls: the original starts with
`InitPC` return address 0x10698 twice, then LoadScene 0x11541. Both InitPC calls
compute actual betting odds (`RND(4)+2`) and initialize player inventory/armor;
they are the boot call 0x10FBA and new-game call 0x11058/0x110D7.
`pc_init.c` ports that lifecycle and SavePCInfo/RestorePCInfo
(0x1B8CC/0x1B8F8). LoadScene explicitly restores saved player HP/status at
0x11FBC..0x11FE5 after randomizing all PERSONs. No artificial RNG advances are used.

`--rng-trace` writes every original RND/RANDOM call's return address, frame,
bound and pre-call words. Native `world_rnd`/`world_random` tag their C call site
through an optional observer; the core neither allocates nor performs I/O.
`tools/compare_rng.py` reports the first mismatching operation/bound/words and
both caller identities, filtering only the original RND wrapper's nested RANDOM
record (return 0x67329) to avoid double-counting.

Full-game `--state-dump` also emits every PERSON as `actor=I actor_*` rows,
sharing that frame's root RNG words. Compare these post-frame states to the
original next-frame entry with:

```
.venv/bin/python tools/compare_actors.py ORIGINAL/actors.csv NATIVE_STATE_DIR --native-post-frame
```

This explicitly excludes original frame0 and the unmatched native terminal
frame, never an observed completed checkpoint. Missing/extra frames, actors,
fields and duplicate records fail. Ten synthetic controls include deliberate HP
and RNG corruption; an original-undefined diagnostic never masks either.
The annotated original stack-distance deviation is in architecture_port.md.
Both native actor/RNG traces report the affected PERSON and cached value;
comparison failures remain failures and diagnostic context is not assumed causal.

The approved `--import-undefined` verification input is limited to PERSON +0x24;
normal play still starts it at zero (architecture_port.md). With real InitPC,
RestorePCInfo and StartClock initialization, the next measured causal difference
is physical collision, not an AI rule: frame 20, object 970, proposed
(1000124,884972). Original/native CollObject entry CSVs match every field:
source floor/z/height/radius = 0/12288/19456/7680, and the sole candidate is
OMap node 431 → object 877, cash item 17 (kind 15), at (1015808,884736),
floor/z/height/radius = 0/6400/6400/7680, flags 0x60080800.
Original steering then receives collision-count/flags 0/0, native receives 1/2.
This isolates Sim's NPC pickup contact branch; no candidate coordinates or
collision outcomes are substituted by the harness.

After the Sim pickup correction, all checkpoints through frame 153 match.
First difference is frame 154 PERSON 2's animation cursor, with
the causal steering mismatch at frame 153 PERSON 69/object 1005. Its old
coordinates match, but the native proposes a 6144-unit step versus original
256, with heights 24780 versus 23936: an animation gate, not collision geometry.
The original RNG observer already sees tick 321 at frame 10 where the frame-entry
input/native sees 320. At frame 153, original normal deadline is 620, native 624,
although both then read current tick 620.

IRQ0 can advance between HandleNextFrame and UpdateObjects, and even between
the latter's fast read at 0x348D0 and normal read at 0x348F7. `--actor-trace`
therefore also records `actor-clocks.csv` with `frame,fast_ticks,normal_ticks`.
These are external clock inputs, not deadlines, gate outcomes or actor state.
The shared replay parser reads this sidecar next to `frames.csv`; both the
main consumer and actor harness use its measured normal/fast fields, not
separate clock parsers or a nonexistent main-game clock option. Malformed,
reordered, missing or extra completed-frame samples fail. The actor harness
logs the metadata path, or warns that absent metadata means only a frame-entry
approximation, not actor-clock parity evidence.
The unstepped terminal checkpoint may have no completed clock pair.
The AI API accepts separate normal/fast ticks in original fast-then-normal
evaluation order. Focused tests cover an IRQ between reads and strict `>`
deadline comparisons; no synthetic RNG calls or deadline overrides are used.

Measured clock-aligned receipt: `captures/combat-clock` matches all 23,560
actor checkpoints across 155 frames (positions, animation/state, HP and RNG).
`compare_rng.py --through-frame 153` matches initialization plus all 3,864
logical RNG calls in completed frames 0..153, including bound and all three
pre-call words. This uses the explicitly logged undefined-field input above;
151 NPCs are active and 37 move, but none loses HP. It is not a firefight verdict.

The complete `captures/AI-combat-rng` script subsequently matches all197,448
actor checkpoints across1299 pre-frame states and all29,998 logical RNG calls
through completed frame1297. Original states are identical to the earlier
Sim2 combat capture. The named7b427a9 full-game consumer independently matches
197,296 actor/RNG checkpoints across1298 completed frames, with no divergence.

`combat-fight.txt` opens the scene-1 door, jumps through the corridor, reaches
hostile actors and turns while firing. The initial spell is semi-automatic:
the scenario supplies genuine Space make/break pulses rather than a held key,
which otherwise emits only one shot. The earlier held-fire capture damages the
player but no NPC and is not a successful firefight-parity gate.
Starter SpellPower[0] is5, from the separately supplied executable at0x92690;
firing before turning spent the40 starting energy without hitting a monster.
The scenario now saves that energy until49s, when the turn exposes enemies.

The first clock-aligned pulsed-fire capture (1254 frames) matches actor/RNG
state through frame896, including the player's first HP loss40→38.
At frame896 the original then calls RND4096 from0x1CF88 while native formerly
went directly to NPC steering RND512; frame897 positions first differ.
`pc_hit.c` ports the missing HandleNextFrame feedback block, including
arithmetic-right-shift fixed products and the physical sideways displacement.
Its vertical spill is used only inside this original block, so the native stores
only the current-frame camera increment. RenderBells separately consumes the
one-frame+1444EC effect and decrements PCFlash once per draw, including headless
replay; neither drawing transition is conditioned on bp_limit_flag.

### Earned-save firefight

`scene11-training-landing-save.keys` creates the checkpoint through the original
Save menus; `scene11-training-stair-fight.keys` loads it and fires real
semi-automatic pulses. No save bytes or actor state are synthesized.
The observed original trace has418 frames/150 actors: PC HP96→42,
PERSON42 is killed and PERSON77 falls to2HP. First PC damage is checkpoint31;
both NPCs first lose HP at301.
Named5bc4fde closes this case: all418 player records and62,550 actor/RNG
checkpoints across417 completed frames match, with no first divergence.
This uses the genuine35fe37fd save, **no** undefined-field import. Primary
receipt: `gate-5bc4fde.json`; native SHA256
`8ab2bb6ecc860082db89cd6489d34b24c0d9a383625668ba4c8f79476cfb3d98`.

```
tools/run_capped.sh tools/oracle.sh tools/scenarios/scene11-training-landing-save.keys \
  --export-saves captures/scene11-training-landing-save/saves
tools/run_capped.sh tools/oracle.sh tools/scenarios/scene11-training-stair-fight.keys \
  --import-saves captures/scene11-training-landing-save/saves \
  --frame-trace --actor-trace --rng-trace --snapshot
mkdir -p out/AI-earned-fight-user
cp -n captures/scene11-training-landing-save/saves/SAVE000.DAT \
  captures/scene11-training-landing-save/saves/SAVE010.DAT \
  captures/scene11-training-landing-save/saves/GAMENAME.USR out/AI-earned-fight-user/
tools/run_capped.sh port/build/AI/cybermage --headless --data orig/retail --bios-seed 44 \
  --user out/AI-earned-fight-user --replay-load-slot 0 \
  --replay captures/scene11-training-stair-fight/frames.csv \
  --state-dump captures/AI-earned-fight/native
tools/run_capped.sh .venv/bin/python tools/compare_actors.py \
  captures/scene11-training-stair-fight/actors.csv \
  captures/AI-earned-fight/native --native-post-frame
```

Do not add `--import-undefined`: the real save already owns its PERSON+24 values.
The measured SAVE000.DAT SHA256 is
`35fe37fd46ca59810669dd4e6ba91ad78be37f20e549566ac8e52d06ec26fa15`.

## Player projectiles and radial effects

`pc_launch.c` implements CM_HI.EXE `PCLaunchMissile`0x2F1C4..0x3092C.
Its two long arguments after type are **screen aim X/Y**, not damage/target.
The final short enables spread; type4 instead reuses X as an attachment object
and the short as reverse heading. Player allocation scans all12 slots; NPC
LaunchMissile reserves0..3. Return1 means launched,0 means full,-1 is malformed
state/data. Inventory costs, input cadence and weapon animation belong to
Port's pc_fire.c, not this helper.

MissileData and trajectory/aim constants come from the separately supplied EXE.
No missile, radius or dice payload is compiled into this implementation.
Gun muzzle forward offset0x2800 and perpendicular hand offset are applied
separately, using original fixed-point trig products. Vertical muzzle position
includes crouch and `(camera_pitch/2)*64` with signed truncation before multiply.
Manual/auto grenade and mine ballistics, attachment sparks, tank shots, normal
projectile texture phases and auto-aim feedback follow their separate original
branches. Original invisible launch types are2,18,40,43,49; UpdateMissile makes
them active before their first movement.

`missile.c` implements UpdateMissile0x30B38..0x33ECE, all52 jump-table entries
at0x30A68: fly/impact states, animated explosions, mine arming/proximity,
attached sparks/DOT, freeze/vortex loops, firestorm/ring children, homing children
and spell acquisition. Types36,37,48 have the original empty dispatch entries,
not invented projectile behavior. The original MakeMissileList runs first;
each active object is removed from the map before its update and reinserted
only on the original surviving paths. Later allocated slots can update in the
same frame. Explosion radial damage happens once on active1, then active2 only
animates. Some lifetimes are per rendered frame; others use bp_limit_flag.
The10-tick MissileFadeTime pulse controls periodic damage, not animation.

Sim owns CollFire0x40050: swept position, target, vertical/heading attachment
changes and0/-1/positive-object result; SIM_COLL_ERROR is separate. CollFire
does not deal damage. For example a type2 shot hits, becomes state3, then on
its following update deals RND(6)+5 to its attached NPC target and becomes4.
Player direct hit damage and radial flashes are processed by UpdateMissile.

`radial.c` implements FreezeMonster0x2B3C0, RadialDamage0x2B484,
PCZoneRadius0x2D478 and PCRadialDamage0x2D838. Radial arguments are source
object, radius-zone index, shooter object, ZoneDice row, resistance index and
blast force. The player variant takes source, zone, dice row, double-hit flag,
armor percentage and erosion percentage, returning raw rolled damage.

The neighborhood is centre, one horizontal neighbor, one vertical neighbor,
then diagonal (at most4 cells, not a circular nearest-target search). Aperture
exceptions apply to the first boundary tests; diagonals use the next cell's
wall mask directly. The horizontal visit changes the wall-mask cursor used
for the subsequent vertical test: this original ordering quirk is preserved.
Objects can occur in multiple cell chains and are **not deduplicated**, preserving
damage and RNG ordering. Three strict XY/Z boxes determine dice tier; monster
resistance scales by `(100-resistance)/100` before damage/impulse/freeze.
Player crouch lowers a nonzero tier by one; vehicle mode7's doubled tier3 roll
is decided before that reduction.

Important original out-of-table read: nominal zoneRadius0x937F0 has5 rows,
but type11's live fire calls RadialDamage0x321A0 with its remaining lifetime
as the zone index. MissileData type11 speed8192 initializes that lifetime to
`0x40000/8192+1 = 33`. The loader therefore reads34 numeric rows at0x937F0,
including the adjacent original WeaponTrajectory bytes, and the consumer permits
indices0..33. This reproduces the executable's actual reads instead of clamping
to5, fabricating a radius, or making normal fire fail.

Focused regression (observed passing, strict C99):

```
tools/run_capped.sh make -C port -f tests/projectiles.mk B=build/AudioProjectile check-projectiles
```

It uses the real supplied EXE tables with explicit synthetic world inputs to
check radial dice/resistance/freeze, player muzzle/slot exhaustion, and single
impact damage/state3-to4. The manual-muzzle fixture explicitly disables autoaim;
a separate no-target fixture checks the original autoaim reset.
Six Python comparator controls cover post-frame alignment, a changed active
field, inactive stale payloads, missing frames, truncated spans and no-activity
rejection.

`tools/scenarios/projectile-fire.txt` holds original Space(scan39) for1.5s,
then releases it. With unchanged original assets and measured BIOS seed44:

```
tools/oracle.sh projectile-fire --frame-trace --snapshot \
  --trace-range x:135ecc:576 --trace-range x:62518:48 \
  --trace-range x:65180:48 --trace-range x:79e8c:48 \
  --trace-range x:7eedc:24
tools/run_capped.sh make -C port B=build/AudioProjectile
tools/run_capped.sh port/build/AudioProjectile/cybermage --headless \
  --data orig/retail --replay captures/projectile-fire/frames.csv \
  --state-dump captures/projectile-fire/native --user out/audio-projectile-user
tools/run_capped.sh .venv/bin/python tools/compare_projectiles.py \
  captures/projectile-fire/frames.csv captures/projectile-fire/native
```

Historical four-slot receipt:155 frames, native player-state MATCH,616 projectile
checkpoints compared,17 active checkpoints, types9 and10, **no field divergence**.
Original pre-frameN+1 is compared with native post-frameN; the terminal native
frame has no next original checkpoint and is excluded. Active slot fields
checked exactly: state/type/animation/lifetime/vertical/speed/target and
XY/floor-Z/heading. Inactive slots compare active state only (stale payloads
are not live game state). The geometry spans cover the12 original pool objects
1088..1099. Current native dumps and the comparator require **all12** pool slots,
including NPC-eligible slots0..3 and player allocations across0..11; old four-slot
native directories fail closed. The616-checkpoint receipt is not retroactively
twelve-slot evidence.

New named8d59b10 runtime receipt:194 player-state rows MATCH;193 completed
frames compare22 PC fields each and2,316 missile checkpoints across all12 slots
(17 active), with no field differences. States are in
`captures/pc-fire/Port-8d59b10`; source addresses, commands and limits are in
`pc_fire.md`. This covers Starbolt9/impact10, not all52 dispatch types.

This proves the captured spell-flight/impact path, not all52 dispatch types or
full-firefight parity. The earlier actor trace above has no damaged NPCs and
must not be presented as projectile verification. Broader firefights use the
complete12-slot format together with PERSON HP/armor/RNG checkpoints.

### Lost-target melee range lifetime

Water route's first HP/RNG divergence at native frame818 (tick1583) was NPC133
melee, not a projectile or swimming. A native damage-entry breakpoint showed
`actor_attack → DamagePerson(0,3,100,100)`. The first wrong RNG request was
melee RND(2), where the original requested the attack-end RND(100), with the
same pre-call words17871/21011/26122.

Original035240 copies PERSON+0C to call-local `[esp+28]` only for an admitted
target. If a later NPC loses its target,035C18 restores its last target but
does not refresh that distance: melee035DD2 uses the preceding admitted NPC's
distance. The port incorrectly initialized it from each NPC's own stale distance
(15517 for the failing actor). `objects.c` now preserves the original call-local
lifetime. No player-damage or RNG suppression was added.

```
tools/run_capped.sh make -C port -f tests/projectiles.mk \
  B=build/AudioProjectile check-melee-distance
```

The two-NPC fixture executes unmodified original UpdateObjects, attack decisions,
DamagePerson and RNG, intercepting visibility/movement/tracking/device boundaries
identically on both sides. Before the fix, prior distance60000 gave original
HP40/RNG1895,23161,4392 versus native HP37/RNG22354,16429,18877.
After the fix, all8 distance/flash comparisons match, including exact range
boundary30720 and accepted30719/10000. The previous actor's PERSON distance is
deliberately changed by the movement boundary after the original copies it,
checking scalar lifetime rather than an alias to that record.

If restored-target melee occurs before any admitted target has written the
call-local distance, normal play uses that actor's own cached PERSON distance,
without aborting or emulating the original stack. It records the first such
NPC/value in `undefined_distance_person/value` for that call. This fallback does
not set the original-distance-valid flag; later affected NPCs still use their
own cache until a real035240 write. See `architecture_port.md` for the explicit
undefined-memory deviation.

The nonzero player-melee hit path also promotes signed PCFlash to at least3
before DamagePerson, exactly035F02..035F26. It preserves values3 and4 and adds
no RNG call; negative/0/2 promotion is covered by the same retail-x86 fixture.

## Player input / hand checkpoint

`pc_fire.c` supplies `ai_pc_prepare_input` before acceleration/water,
`ai_pc_prepare_motion` (hit response, aim, hand stance) before MovePerson,
late `ai_pc_input` for actual firing before world/NPC updates, and
`ai_pc_hand2hand` after NPC updates, plus the supplied-EXE PC table loader and
ResetAim. It implements the original weapon0..9, spell0..15 and vehicle launch
dispatch, ammo/item/energy costs, semi-auto release latch, cooldown/aim/muzzle
state, scene3 linking spell, automatic sword sequences, swing matrices, blocking,
melee damage and attached impact missiles. Functions and fields are detailed in
`pc_fire.md`; all numeric arrays come from the user's executable. The current
shared tree linked cleanly with strict C99 through `make -C port B=build/Port`.

The historical scene1 fire replay matches193 completed frames:22 hand/weapon/energy
fields per frame and772 four-slot missile checkpoints(17 active), with no field
divergence. Current dumps/comparison require12 slots; the772 count remains the
older four-slot receipt, not a new twelve-slot runtime verdict.
The newer named8d59b10 receipt above supplies the actual2,316-checkpoint
twelve-slot runtime verdict.
Original-x86 differential fixtures additionally match17,280 hand frames,
2,400 energy-recovery frames and7,040 hand-bob frames. Commands, source addresses,
checked inputs and limitations are in `pc_fire.md`.

`ai_pc_select` commits HandleNextFrame1E4A0..1E768 selection state immediately:
EquipWeapon/EquipHHWeapon and ResetAim use the **new raw mode**, not the previous
effective mode temporarily retained by modern aiming. It clears fire/cooldown/
latch. EquipHHWeapon103A4 preserves the glove type; EquipWeapon10441 resets glow
phase only when entering power mode5. Power-to-power selection changes the glow
set while preserving its phase. Previous mode is the single original144810
state, not an independent selector copy.

Visual counters may advance after the read-only HUD; original low-HP respiration
0x1928B..0x19340 and energy regeneration0x19340..0x193B2 run after StatusMonitor.
Keyboard hand and walking-bob
producers are live; underwater comes from Events action72 and aircar spread from
the original A/D/S rules. This proves default Starbolt/release-latch/energy recovery,
projectile flight/impact and direct hand-state mechanics, not all weapons or a
full firefight. The direct hand fixtures have no NPC targets; genuine earned-weapon
pickup/punch verification and the legacy mouse/tank hand-activity bridge remain
separate. Recorded coordinates/hand state never initialize the port replay.
