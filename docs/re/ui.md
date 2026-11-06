# UI and save serialization

All addresses below are retail `CM_HI.EXE` LE link addresses.

| Original | Address | Port / evidence |
|---|---:|---|
| TitleMenu | 0x54C20 | HIPAK0; font1/HIPAK26; strings140..145; inclusive boxes32..210/230..408/428..606,366..398/412..444. Draw0x555BA–0x55644. |
| MenuOptions | 0x4E690 | Render view0 then six shaded boxes, font1 centered; selected fixed-map2, others shadow22,0x4F0AD–0x4F15E. Table0x95E48 centers,0x95E60 y,0x95E78/0x95EA8 endpoints. |
| FileMenu | 0x4F3F4 | mode0 save,1 load; ten manual slots;24-byte name records. World background rendered before overlay. |
| OptOptions | 0x53DDC | HIPAK53; ten rows x46,y146 increasing22; additional22 after rows2,5,7,8.0x53ECD–0x54002. |
| SaveGame | 0x5007C | Sequential writes0x50235–0x50949; no header/version tag. Scalar words/dwords little-endian; final person count determines variable tail,192 bytes/person. |
| LoadGame | 0x50AD4 | Requires original scene load then overlays saved fields; native pointer values must not be serialized. |
| DrawMonitor | 0x2A37C | Transparent palette index0; width×height row-major pixels, screen pitch640. [rust: crates/game/src/native/cmdraw.rs]; binary cross-check pending. |
| DrawHGFXBitMap | 0x2A624 | Opaque screen blit. [rust: crates/game/src/native/cmdraw.rs]. |
| DrawXScaleAim | 0x29260 | Signed packed runs, per-row offset table; integer16.16 sampling. [rust: crates/game/src/native/cmdraw.rs]. |
| RenderBellsAndWhistles | 0x1899C | Only primary view receives HUD; hand before monitors; vehicle HUD7 selects TankMonitor/ACarMonitor; map0x19100+, status/cash0x191A8+. |
| StatusMonitor / CashMonitor | 0x15E3C / 0x15D54 | Numeric/bar layout investigation. |

## GAMENAME.USR

SaveGame0x50A24–0x50A83 writes exactly288 bytes:10×24-byte gameName records;10×u16 autoPtr;10×u16 autoSlot;s32 restartGame;s32 restartAuto. Names are NUL-terminated DOS text with remaining bytes retained. Slot10 is hidden restart state, not an eleventh name. Save names use `SAVE0%02u.DAT` (original modifies positions5/6 of template0x91B8A).

## Scope notes

Original title asset/font/shading implementation reused from Port slice (see formats.md). Captures remain ignored, no original asset bytes are committed. UI core borrows resources and performs no allocation. Save format investigations preserve unknown fields by their Xox offset rather than inventing semantics.

## SAVE*.DAT byte layout

There is no magic, version, padding or native-pointer header. Retail supplied
`SAVE010.DAT` is246045 bytes:216861 fixed bytes plus152×192-byte PERSON records.
The following offsets were calculated from the original write sequence and
checked by decoding into typed world objects/persons and re-encoding every byte.

| File offset | Bytes | Original source |
|---:|---:|---|
| 0 | 4 | scene, Xox+130CEC |
| 4 | 4 | Xox+5E7A8 |
| 8 | 1740 | Xox+130D50..13141B, player inventory/status |
| 0x6D4 | 116 | 29 discontiguous Xox dwords, listed below |
| 0x748 | 16 | PCBet,PCBetPick,PCBetOdds,JackPot |
| 0x758 | 8 | Xox+BDEB8,+5DFA0 |
| 0x760 | 63 | speechLog |
| 0x79F | 24 | coderStr |
| 0x7B7 | 36 | +BDECC,+BDED0,+BDED4,+142BF8,+BE05C/+60/+64/+68/+6C |
| 0x7DB | 6144 | Xox+142BFC |
| 0x1FDB | 8 | Xox+1443FC,+144400 |
| 0x1FE3 | 60 | textMsgNum/Scen,Count,Line,Add,End,Switch1/0,Event,Trigger,Obj;autoDestruct,remaining autoDTime,gemCountDown,remaining gem timer |
| 0x201F | 11368 | object flags,2842×u32 |
| 0x4C87 | 11368 | object x |
| 0x78EF | 11368 | object y |
| 0xA557 | 11368 | object z |
| 0xD1BF | 11368 | object floor_z |
| 0xFE27 | 2842 | active u8 |
| 0x10941 | 5684 | heading u16 |
| 0x11F75 | 2842 | target_mode u8 |
| 0x12A8F | 5684 | target_angle u16 |
| 0x140C3 | 5684 | texture u16 |
| 0x156F7 | 2842 | rotations s8 |
| 0x16211 | 45472 | corner_x,2842×4×s32 |
| 0x213B1 | 45472 | corner_y |
| 0x2C551 | 576 | Xox+135ECC |
| 0x2C791 | 280 | Xox+13610C |
| 0x2C8A9 | 64 | light_param[32],Xox+A48F4 |
| 0x2C8E9 | 64 | light_type[32],Xox+A4934 |
| 0x2C929 | 512 | Xox+139390 |
| 0x2CB29 | 512 | Xox+139590 |
| 0x2CD29 | 32768 | event runtime,Xox+139D90 |
| 0x34D29 | 96 | aperture enabled bytes,Xox+142390 |
| 0x34D89 | 128 | switch state,Xox+142B78 |
| 0x34E09 | 256 | Xox+1428F8 |
| 0x34F09 | 16 | FireCheck,FireTilePanel,FireTileArt,FireDamage |
| 0x34F19 | 4 | PERSON count |
| 0x34F1D | count×192 | explicit PERSON record fields |

`ui_save_state.unk_6D4` indices0..28 correspond, in order, to:
14480C,144810,130D04,130D08,130D0C,130D14,144AB0,144AB4,144AB8,
144AE0,144AE4,144A24,4B000,144ABC,144AC0,144AC4,144AE8,144AF8,
144AFC,144B00,144B1C,144B24,144B28,144B2C,144B40,144B30,144B34,
130D1C,144B50.

PERSON+2 is the original monster definition id; +4 is the compact runtime
monster index. The C world keeps those as `type` and `monster_index`, not the
native struct layout. PERSON+38/+3A are current/max HP. Xox+131410/+131412
are current/max energy. Xox+130D1C is body/eye height, **not pitch**.

Scene-dependent unsaved geometry/resources load before overlays; object chains
are rebuilt. Events/AI/Sim import/export their own live fields through this
explicit staging struct. New-game unknown fields are initialized zero and are
not claimed to reproduce every original constructor value. NPC difficulty
normalization from original SaveGame/LoadGame remains an integration concern.

## HUD, hands and text

- Constructor0x1026A–0x10333 fixes monitors at(10,436),(424,436),cash(264,448).
  HIPAK20 has13 images after an84-byte header: u16 count/max20,20 u16 widths,
  20 u16 heights, then contiguous row-major bitmaps.
- StatusMonitor0x15F08/0x16088 draws two-row waveforms using
  `offset=(phase-1)*(26*current/max)-waveform_width+24`, clipping to124 pixels.
  HPCount cycles0..7 **after** drawing only when timer>BlipTime; next deadline
  is timer+4(0x16391). Numeric font6 is colored: preserve its palette bytes.
- Armor position arrays0x95BC0/0x95BD8 and armorColor0x962AC are used directly.
- Hand PRP packages:1442-byte header,120 interleaved u32 dimension pairs,
  120 u32 frame offsets. LoadPROPDOSMEM0x259EB/0x259FD puts disk width into
  TEX_H and disk height into TEX_W. PRP48 is the power glove. InitPC0x1077A
  initializes+144A90/+144A94 to47/7. LoadScene12187 loads the resource at
  +144A94 (PRP7) into the texture base at+144A84. MenuOptions4E830 uses
  base pose4;4EF9E..4F008 adds the directional row/column. Initial aim(63,133)
  selects pose2 and PRP7 frame2. LoadPROPDOSMEM259E0 writes
  frame0 over the initially null texture: there is **no reserved null slot**
  and no frame-index subtraction. DrawXTankAim298B6 clamps top to0.
  The new cursor API takes live aim and pose; `ui_cursor_pose` implements the
  directional3x3 open/pressed banks. Pause4F178 adds the press-animation
  displacement (normally0), not the menu mode: the preceding height push
  shifts the stack argument. Initial draw aim remains(63,133).
  Mode1 draws firearms,2 draws swords,5 draws powers,
  3/4 draw selectors(0x18948 dispatch table).
- Glv_xOff/yOff/xPosOff,xFrame,Swd_xFrame,SwordX,CenterAimX/Y,SpellGlow and
  GlowSet are read as numeric data from the user's retail CM_HI.EXE through
  `cm_exe`; no executable code is run and no extracted tables are bundled.
- TextMessage0x1824F displays complete four-line pages, not typed characters;
  @ begins a page. Delay is(page line count+1)×textMsgDelay(0x1891F). Text uses
  font2/color232,28-pixel line spacing, corner glyphs24..27. Linked gameplay
  directives remain Events/Audio-owned. Speech mode does not display these pages.
- SpeechRecorder0x54508 filters visited logs21,22,42,50,51,57;30 entries/page,
  two columns at22/330,y140 with22 spacing. Audio owns index→message replay.
- SCON icons are HIPAK35: u16 count/max33 plus33×56×38 bytes. DrawSCONBitMap
  0x56A60 doubles pixels, translates zero texels with shadow32 and duplicates
  the translated first scanline onto the second. Selectors use five columns,
  x14+116×column,y128+88×row; selected outline113×77 inclusive.

## Shops

`ui_shop_step` consumes Events requests and pauses simulation with a modal
return value. `ui_shop_draw` draws the merchant background and selection.
Merchant kinds1/5/6 now grant through Events and deduct only after success;
selling uses the original eligible inventory kinds and partial-charge pricing.
The focused retail-asset test buys stock item5, verifies cash/inventory, refuses
a duplicate without debit, sells it for half-price, and closes the modal.
Arena/coder/slot requests still fail explicitly rather than becoming merchants;
their rules and frontend remain unimplemented at this checkpoint.

- HandleNextFrame0x1BF7D–0x1BFE2: ShopFlag1/5/6→LoadShopScreen(HIPAK4);
  2→arena(HIPAK5);3→coder(HIPAK6);4→slot machine(HIPAK50).
- Widget banks:HIPAK19 shop,21 arena,22 coder,23 slots,24 vehicles; same84-byte
  bank header as console graphics.
- SetShopItems0x48698: stock1 reads16 dwords0x96008; stock5 reads16 at0x96058;
  stock6 reads3 at0x960A8. Stock6 sell mode instead uses definitions107..109.
- Buy0x4A0BA: require cash>=definition runtime+18 (`events_definition.unk_0018`),
  call BuyItem0x41F80, deduct cost **only if it grants**. Events now exposes
  `events_buy_item`(1 granted,0 refused,-1 error),cash_add and inventory_sell.
- Sell list0x49F43/0x4878C: kinds5,6,7,9,11;kind0 only if type!=104 and cost>0.
  Kinds9/11 consume one quantity; others remove the whole item. Selling scanner
  kind7 clears its enabled flag. Price normally cost/2;kind6 subtype>7 scales
  that half-cost by quantity/(dice_count×sides+add), integer division.
  Stock6 instead trades5 maximum HP for half the definition cost, only if
  maximum HP>5, then sets current HP to new maximum(0x49E37).
- Shop components center atx104,318,532,y202(0x4942E/table0x960F8).
  Background320×240 is doubled. Arrow widgets at(6,145)/(284,145),sell/buy/exit
  at(72,158)/(136,158)/(188,158), in320-space; selection lamps x76/183/290,y60.
- Events exposes shop_requested{kind,screen,arg,switch_index}; event type26
  produces it. Coder event/slot mapping, arena rules, slot odds/reels and modal
  routing still need implementation, not invented defaults.

`tools/ui_state.py` inspects widget dimensions, supplied STRINGS.MSG and one
original trace range. Fresh ui-pause original-x86 trace at144A40..144ACF shows
power hand_type1, weapon_aim_step4, aim(-56,65) after startup. The old static
probe's pose0 was not the original's settled pose. Tests now use pose4.

## Exercised verification and open acceptance

Commands run through `tools/run_capped.sh`; output directory `build/UI` is isolated.

```
make -C port -f tests/ui.mk B=build/UI check
make -C port B=build/UI
port/build/UI/test_ui out/port/ui captures/render-newgame/frame000000.idx
tools/oracle.sh ui-options
tools/oracle.sh ui-pause
tools/oracle.sh ui-hud
```

The strict C99 full link and focused probe exited0. Expected negative-test
diagnostics reject truncated GAMENAME, truncated PERSON tail, and undersized
save output. The original retail SAVE010 scene1 loads into typed world state;
player936,152 persons,x/heading and every246045 re-encoded bytes were checked.
Changing live player x changed its serialized coordinate. GAMENAME.USR re-encodes
all288 bytes exactly. This is codec/state evidence, **not a full loaded-game
playthrough** or proof that every subsystem's live save bridge is complete.

Fresh RGB comparisons, each pixel differing in any channel counted once:

| Port input/composition | Oracle | Mismatch |
|---|---|---:|
| title,out/port/ui/frame000000.ppm | captures/title/frame000000.png | 0/307200 |
| options,frame000001.ppm | captures/ui-options/frame000000.png | 0/307200 |
| left HUD rectangle[10,436)–[216,470),frame000002.ppm | captures/ui-hud/frame000000.png | 0/7004 |
| right HUD rectangle[424,436)–[630,470),frame000002.ppm | same | 0/7004 |
| software-renderer world+UI backdrop+static power hand+HUD,frame000003.ppm | captures/newgame/frame000000.png | 7115/307200 |
| world+pause boxes+PRP7 directional cursor,frame000004.ppm | captures/ui-pause/frame000000.png | 0/307200 |

The HUD probe uses original SAVE010 health/armor/energy fields and exercises
phases0..2; the independent original HUD checkpoint reports HPCount2. No HUD
color/pixel constants came from the captures. The world-only input to composed
frames is Render's **port-generated** indexed frame, not an oracle screenshot.
The7115 gameplay differences lie inside the120000-pixel view(hand pose); pause
now matches exactly with the corrected cursor resource, directional pose, and
unpressed aim(63,133). Individual
selector/message/death/weapon variants compile but lack oracle parity tests.

Still open: shops/arena/slot/coder frontend and rules,routing; automap; tank/car
monitors; exact live aim/bob/glow/muzzle animation; death-melt main-loop wiring;
options remapping/calibration and all settings effects/config persistence;
full original-save gameplay validation; demo SAVE010 layout verification.
