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
  `ai_pc_draw_advance` runs after hand drawing but before HUD composition.
  Glow frame advances modulo4 at0x1BA49 when bp_limit_flag is nonzero.
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

Checkpoint: strict host linking and focused equipment/modern ResetAim/draw-counter/
aircar-spread checks pass. The original hand differential and scene1 firing replay
are still being completed; this is not a combat-parity claim. Manual hand/tank input
and walking-bob producers are still under investigation.
