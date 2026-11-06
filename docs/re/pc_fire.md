# Player firing and hand state

Retail CM_HI.EXE v1.01F LE link addresses. Gameplay only; UI consumes state.

- HandleNextFrame mode dispatch0x1D44E: mode1 firearm0x1D841, mode5 spell0x1DF9E,
  mode7 vehicle0x1E301. Mode2 hand stance is the movement branch0x1D466;
  Hand2Hand runs separately after MoveWorld/UpdateObjects at0x1E825.
- Phase contracts: `ai_pc_prepare_input` runs after keyboard sampling/running
  eligibility, before acceleration/water; `ai_pc_prepare_motion` runs hit response,
  aim and sword stance after body calculations but before MovePerson. Late
  `ai_pc_input` only launches weapons/spells/vehicles/linking missiles.
- ProcessInputEvents0x1481E copies cooldown144AB4 to latch144AB8 before deriving
  fire144AB0. Keyboard fire is Space(scan39), mask1; HandleKeyboardEvents0x13E39
  consumes the bit2 edge but preserves held bit1. Defaults0x52644.
  Sim samples keyboard primary144B10 once. With no primary, the consumer accepts
  the logical keySwingSword206848 pulse; it is not interchangeable with primary.
  At provider checkpoint5a3c5b3 this pulse is consumer-only: selector producers
  0x134B8/0x134F9/0x13544/0x13627 are not yet connected. Manually supplied pulse
  tests verify the consumer, not a live keyboard-to-selector path.
  Mode3 confirm sources (Enter0x12F9F and kbHand0x12FD7, which clear keys and
  bypass0x13E39) and classic Mouse0x144A6/Joystick0x146F9 remain separate input
  producers; keyboard sampling alone is not evidence for those paths.
- ResetAim0x12D60: mode1 uses CenterAim[hand_type144A40], mode5 index1, all others0.
- EquipWeapon0x1040C: hand_type=weapon+2 unless mode5(index1); PCHandBound16x8
  and WeaponTrajectory16x2x8 are supplied executable tables, not compiled assets.
  `ai_pc_equip` also runs at scene initialization/save restore, matching
  LoadScene0x12148/0x12155 before the first game frame.
- Selector commits0x1E4A0..0x1E768 call `ai_pc_select` after assigning the selected
  raw mode/item, before clearing the selector. It equips and resets aim in that
  same frame, then clears fire/cooldown/latch. Modern RMB's previous effective
  mode must not override the new selection. EquipHHWeapon0x103A4 preserves glove
  type; EquipWeapon resets glow phase only in its mode5 branch0x10441.
  Selecting another power from mode5 changes glow set without resetting phase.
  Focused native checks cover gun/melee/power/cancel commits and raw-mode aiming;
  the full selector/hotkey consumer and its original-x86 fixtures belong to Events.
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
  Call `ai_pc_draw_advance(a,s,w,clock)` after the read-only HUD, before dialogue.
  Glow frame advances modulo4 at0x1BA49 when bp_limit_flag is nonzero.
- One-frame tint1444EC clears at0x18A05; damage flash1444E8 decrements at0x190F1
  after its viewport tint. Both use every draw, including zero-tick draws, rather
  than bp_limit_flag. UI reads/draws the effects before this state-only step.
- RenderBells0x19340..0x193B2 recovers one energy when ticks is strictly greater
  than global spRegenTimer206834. The interval is max(6,65-signed16(max_energy)/10);
  the deadline advances even at full energy. It survives scene/NewGame/save loads.
  StatusMonitor0x19286 precedes recovery, so the HUD sees the increment next frame.
  The clock is read at19377 for comparison and, only when due, again at193A4
  for the new deadline; an IRQ may separate those reads. The canonical
  `pc.regen_interval`(+131414) is recomputed on every draw and saved explicitly
  in prefix words866/867. Status blip time uses its separate16391 input through
  `ui_status_advance`, shared with pixel-free verification.
  SaveGame0500B7 invokes the same render/HUD pipeline once before encoding;
  it is not an artificial extra RNG request. `draw-clocks.csv` and actual
  slot/name `ui-actions.csv` inputs drive replay; missing timing inputs fail
  closed at the actual timer read, with the frame and missing filename.
  Declaring a draw phase does not require clock files when that consumer
  performs no clock-dependent operation. No timestamp is fabricated.
  See `bootstrap.md` for the frame286 save fixture.
- Low-HP respiration0x1928B..0x19340 reads PERSON0, including while driving.
  Positive HP below signed maxHP/2 and the real SwordHum gate consume
  RND(HP/2+2) before CheckSound(PCSound,3). A zero chance result and completed
  sound consume RND(8192), play sample3 at volume16384−RND and store PCSound.
  Muted playback still consumes the real random calls.
- ProcessInputEvents0x14857/0x1496A/0x14C19 maintains the actual shared hum
  slot/sample: running1, held melee32, tank61 with momentum-derived volume.
  Running precedes melee only after the real eligibility gate: held primary in
  mode2 clears EDI at0x148C4, but keySwingSword alone does not.
  Switching starts the new loop before aborting the old one. Stop-running breath
  at0x14D04..0x14D41 uses the same PCSound handle. KillSound/KillLoops clear the
  slot only; modal event26 stops hum and swim at0x3AAA6..0x3AAE2. Pause stops
  only swim(0x1BC60..0x1BC79), not hum.
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
The low-HP respiration block additionally matches5,040 original-x86 draws across
PERSON0/vehicle selection, death/half-health edges, hum gating and three RNG seeds.
The oracle's real noSoundFlag option is set; chance/volume RND and PlayDIGI still run.
Another3,120 original ProcessInputEvents frames match hum slot/sample, fire, PCSound
and exact RND bounds/results/state for primary-versus-swing-pulse, run/melee,
tank/aircar and stop-running breath. Read-only Unicorn observers record RND entry
and actual return values without replacing instructions or registers. These direct
loop fixtures take a prepared running gate; Sim's canonical sampling/cap fixture is
separate.

The genuine scene1 fire replay at8d59b10 has193 completed frames,22 PC fields/frame
and2,316 missile checkpoints across all12 slots(17 active): no field divergence.
The strict-build executable also reports player-state MATCH. It exercises Starbolt9/impact10,
40→35 energy expenditure, timed recovery and the held-fire release latch. The
terminal native frame has no next original entry and is excluded. Input replay
provides clock/keys and measured BIOS seed, never recorded coordinates or hand state.
The dump/comparators require all12 missile slots; old four-slot state directories
fail closed and are not retroactively twelve-slot evidence. Nine synthetic
projectile-trace controls pass, including an active slot11 mutation and a missing
slot11 record. The added pool-size control failed4!=12 before the cutover.

This is not all-weapons/full-firefight parity. The direct hand fixtures have no NPC
targets; genuine earned-weapon pickup/punch verification is separate. Legacy mouse/
tank hand activity still needs its input bridge; bob and keyboard producers are live.

The original-only `vehicle-powerblade` scenario loads the genuinely earned
`captures/earned-water` slot0, follows the river, stands/vaults onto the dry
outcropping, picks up item4, then uses F1/F1 and Space. The 34-second capture has448
frame entries, inventory120+4, mode2 and changing sword frames7/4/8. It does not
inject coordinates, inventory or hand state. This is an original pickup/punch
oracle, not yet a claim of native replay parity:

```
tools/oracle.sh vehicle-powerblade --import-saves captures/earned-water \
  --frame-trace --actor-trace --rng-trace \
  --trace-range x:1444e8:1536 --trace-range x:130d64:1600 \
  --trace-range x:1313a4:128 --trace-range x:135ecc:576 \
  --trace-range x:62518:48 --trace-range x:65180:48 \
  --trace-range x:79e8c:48 --trace-range x:7eedc:24 --snapshot
```
