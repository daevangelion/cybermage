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

Table bytes are read from the user's CM_HI.EXE via shared exe.h; no original
numeric-table blobs are bundled. Oracle verification uses frame-entry traces,
so rowN+1 checks the result of rowN. Rendering cannot update combat state.
