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

Render supplies ordered clipped projected props at the original 640x480
projection, without pixels or world writes. `target_query` includes only original
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
SoulFire records and gem countdown; Events owns event/inventory serialization.

## Actor oracle

```
tools/oracle.sh combat-actors --frame-trace --actor-trace --snapshot
make -C port -f tests/ai.mk B=build/AI build/AI/ai_trace check
port/build/AI/ai_trace orig/retail captures/combat-actors/frames.csv BIOS_TICK
.venv/bin/python tools/compare_actors.py ORIGINAL.csv NATIVE.csv
```

The native harness loads the original assets independently; it does not install
checkpoint coordinates, HP, actor state or RNG state. Its replay currently steps
Sim then UpdateObjects; it is an actor-isolation harness, not a complete
replacement for the game frame's missile/event/player-fire ordering.

First measured comparison: 23,560 actor checkpoints / 155 frames. First divergence
is frame 0, PERSON 0 HP (original 40, native 22); first NPC divergence is frame 0,
PERSON 1 HP (11 versus 13). Original RNG words are (26876,18426,3810), native
(8925,0,871). The native loader then seeded the middle word with zero, which is
invariant under the multiplicative RNG; initial conditions therefore already
differ before frame 0. This is a failed parity check, not evidence of matching
RNG calls or verified combat rules. `seed.csv` now measures the original BIOS
source at RANDOM_SEED entry 0x68ABF before its write; do not fit a seed from the
later actor checkpoint.

The source observer measured BIOS tick 44 at instruction 145,715,835,
interpreter time 2.4285943666692638 s (`captures/combat-actors/seed.csv`).
Replaying with that explicit input still diverges at frame 0: player HP 40/16,
NPC 1 HP 11/16, RNG (26876,18426,3810)/(8925,23868,871).
Starting from the documented initial words (3000,44,29999), the recurrence reaches
the original checkpoint after 489 advances and native after 487: two pre-frame
RANDOM calls are missing. Their callers remain unidentified; no dummy calls or
checkpoint state overrides were added. Both traces contain 151 active NPCs,
37 changing position, and zero NPCs losing HP. This scenario exercises movement,
not a demonstrated firefight.

The call-site observer resolves those two missing calls: the original starts with
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

Observed:155 frames, native player-state MATCH,616 projectile checkpoints
compared,17 active checkpoints, types9 and10, **no field divergence**.
Original pre-frameN+1 is compared with native post-frameN; the terminal native
frame has no next original checkpoint and is excluded. Active slot fields
checked exactly: state/type/animation/lifetime/vertical/speed/target and
XY/floor-Z/heading. Inactive slots compare active state only (stale payloads
are not live game state). The geometry spans cover the12 original pool objects
1088..1099; native state dumps currently expose the first4 slots.

This proves the captured spell-flight/impact path, not all52 dispatch types or
full-firefight parity. The earlier actor trace above has no damaged NPCs and
must not be presented as projectile verification. Broader firefights need
all12 native missile records and PERSON HP/armor/RNG checkpoints.

## Player input / hand checkpoint

`pc_fire.c` now supplies real `ai_pc_input` (before world/NPC updates) and
`ai_pc_hand2hand` (after NPC updates), plus the supplied-EXE PC table loader and
ResetAim. It implements the original weapon0..9, spell0..15 and vehicle launch
dispatch, ammo/item/energy costs, semi-auto release latch, cooldown/aim/muzzle
state, scene3 linking spell, automatic sword sequences, swing matrices, blocking,
melee damage and attached impact missiles. Functions and fields are detailed in
`pc_fire.md`; all numeric arrays come from the user's executable. The current
shared tree linked cleanly with strict C99 through `make -C port B=build/Port`.

This is a compile-clean integration checkpoint, not a parity/completion claim.
Remaining validation/work owned by Port: compare the newly captured
`captures/pc-fire/frames.csv` (194 original frames) against native hand/weapon and
missile state; verify punch/sword transitions against original execution; finish
the original render-side visual counter advance as a separate post-presentation
state step; wire underwater and vehicle spread state producers. The original
fire trace has a genuine Space press/hold/release and one spell launch: energy
40→35, held latch suppressing repeats until release. Do not claim full combat
parity from the clean link or substitute recorded state for native state.
