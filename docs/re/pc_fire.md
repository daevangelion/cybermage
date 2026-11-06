# Player firing and hand state

Retail CM_HI.EXE v1.01F LE link addresses. Gameplay only; UI consumes state.

- HandleNextFrame mode dispatch0x1D44E: mode1 firearm0x1D841, mode5 spell0x1DF9E,
  mode7 vehicle0x1E301. Mode2 hand stance is the movement branch0x1D466;
  Hand2Hand runs separately after MoveWorld/UpdateObjects at0x1E825.
- ProcessInputEvents0x1481E copies cooldown144AB4 to latch144AB8 before deriving
  fire144AB0. Keyboard fire is Space(scan39), mask1; HandleKeyboardEvents0x13E39
  consumes the bit2 edge but preserves held bit1. Defaults0x52644.
- ResetAim0x12D60: mode1 uses CenterAim[hand_type144A40], mode5 index1, all others0.
- EquipWeapon0x1040C: hand_type=weapon+2 unless mode5(index1); PCHandBound16x8
  and WeaponTrajectory16x2x8 are supplied executable tables, not compiled assets.
  `ai_pc_equip` also runs at scene initialization/save restore, matching
  LoadScene0x12148/0x12155 before the first game frame.
- AutoSwordSwing0x1A07C uses autoSPos11x4 signed bytes, autoSInitMode9x3,
  autoSInitPos11 and timer-gated3-tick steps. No pixels are involved.
- Keyboard hand steering0x12E83/0x1365B..0x13C02/0x13E1F uses the previous
  fire value, bounded64-unit grid steps and the original arrow/diagonal/Shift order.
- Hand bob0x1C73F..0x1C873 runs after flight, before jump processing. Its14-step
  waveform scales with signed forward momentum and decays toward zero while idle.
  An active sword swing clears the phase and visual offsets at0x1E7FF..0x1E81B.
- Hit response0x1CF71..0x1D09E runs before aim preparation: `pc_hit.c` owns its
  three real random draws, fixed-point lateral push and current-frame camera-Z kick.
- Hand2Hand0x1A298 uses SwingFrame9x21 i16, swingMode9x9 i32, MatIdx9x9 i32,
  PosMat6x8x8 bytes and SwingDamage21 i32. PERSON3C/3E are current/previous
  swing;40/42 current/previous hand grid;44/46 impact/stun counters;48/4A matrix
  and last-impact swing. Existing typed PERSON storage preserves these packed
  words explicitly, never by casting the original byte layout.
- PCLaunchMissile0x2F1C4 is Audio's implementation: args type, screen aim_x,
  screen aim_y, short special aim. It returns launched1/full0/error−1.
  Inventory, energy costs, gun cooldown, glove and muzzle state belong here.
- Released melee uses frame8 at0x1ECB8, not frame0. Modern aiming keeps the
  previous combat mode while the existing input machinery uses mode3.
- RenderBells0x18BA4/0x18D73 advances positive/negative muzzle sequences after
  drawing; DrawACarMonitor0x157AC does the same for aircar positive sequences.
  Call `ai_pc_draw_advance` after hand drawing but before HUD composition.
  Glow frame advances modulo4 at0x1BA49 when bp_limit_flag is nonzero.
- One-frame tint1444EC clears at0x18A05; damage flash1444E8 decrements at0x190F1
  after its viewport tint. Both use every draw, including zero-tick draws, rather
  than bp_limit_flag. UI reads/draws the effects before this state-only step.
- RenderBells0x19340..0x193B2 recovers one energy when ticks is strictly greater
  than global spRegenTimer206834. The interval is max(6,65-signed16(max_energy)/10);
  the deadline advances even at full energy. It survives scene/NewGame/save loads.
  This state-only step must precede HUD reads so the increment is visible immediately.
- Aircar spread0x13208..0x13276: A held adds128, capped1024; D held subtracts128,
  floored−2048; S clears the spread and consumes its scan byte.
- Underwater backfire reads144A24; Events action72 produces it at0x3E9D7/0x3E9FA
  from event parameters. It is a nonzero test, not a validated boolean.

Table bytes are read from the user's CM_HI.EXE via shared exe.h; no original
numeric-table blobs are bundled. Oracle verification uses frame-entry traces,
so rowN+1 checks the result of rowN.

Verification:

```
tools/run_capped.sh make -C port B=build/Port pc-fire-check
tools/oracle.sh pc-fire --frame-trace --actor-trace --rng-trace \
  --trace-range x:1444f4:1524 --trace-range x:1313a8:112 \
  --trace-range x:135ecc:576 --trace-range x:62518:48 \
  --trace-range x:65180:48 --trace-range x:79e8c:48 \
  --trace-range x:7eedc:24 --snapshot
tools/run_capped.sh port/build/Port/cybermage --headless --data orig/retail \
  --user out/Port-fire-user --bios-seed 44 --replay captures/pc-fire/frames.csv \
  --state-dump captures/pc-fire/native
.venv/bin/python tools/compare_pc_fire.py captures/pc-fire/frames.csv captures/pc-fire/native
```

Observed strict-build tests:17,280 original-x86 hand frames (four melee weapons,
nine initial grids, manual/automatic, three RNG seeds, eight directions/Shift and
press/hold/release);2,400 energy-recovery frames;7,040 hand-bob frames including
signed speed extremes and idle decay. They execute unmodified original routines
and inline basic blocks, not a second Python implementation of their rules.

The genuine scene1 fire replay has193 completed frames,22 PC fields/frame and772
missile checkpoints(17 active): no field divergence. It exercises Starbolt9/impact10,
40→35 energy expenditure, timed recovery and the held-fire release latch. The
terminal native frame has no next original entry and is excluded. Input replay
provides clock/keys and measured BIOS seed, never recorded coordinates or hand state.

This is not all-weapons/full-firefight parity. The direct hand fixtures have no NPC
targets; genuine earned-weapon pickup/punch verification is separate. Legacy mouse/
tank hand activity still needs its input bridge; bob and keyboard producers are live.
