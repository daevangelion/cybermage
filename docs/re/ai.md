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

Save restore uses data-only `ai_load_scene_definitions` and `ai_load_tables`,
then `ai_save_import`, never `ai_init`: saved object pools already exist.
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
