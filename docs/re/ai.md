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
