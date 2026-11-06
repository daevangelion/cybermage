# Events, inventory, selectors and vehicles (retail CM_HI.EXE)

All addresses are LE link addresses in CM_HI.EXE. Original Watcom symbols in
labels.csv are the naming authority. No native reference game hooks are evidence.

## Scene tables

LoadEVENTS 02732C reads 256 bytes of u32 region chain heads, then a count at
section+256. Event i is section+256+72*i (index zero is the dummy). Count must
be below512. Header: u16 region, u16 next, u8 type, u8 countdown, u8 enabled,
u8 one-shot. Sixteen signed32-bit payload words follow. Runtime payload is
Xox+139D90+64*i, types +139190, countdowns +139390, enables +139590,
one-shot flags +139790, next links +139990. Type50 initializes countdown from
payload word11. Verified 02744A..0274AB. Scene1 has163 records.

LoadSWITCHES 0274F8: count at0, sixteen-byte dummy, then count records:
u32 tile; u16 off-FXO placement, on-FXO placement, control-FXO placement;
s16 linked switch; u16 key; u8 enabled; u8 state. The off/on textures are
resolved through the loaded FXO object texture; they are not texture ids on disk.
Linked positive indices copy the new state; negative indices gate activation.
Scene1 has54 switches. SwitchSearch 01AF9C uses exact tile, strict vertical
straddle of the control object's z (lower player+3000+stance, upper
player+shape_z+height), distance<8C00, signed circular heading difference<1000.

MoveWorld 037CF4 first runs scene-specific cosmetic updates, then follows only
the player's map-region chain. Player tile uses floor_z+max(shape_z,5000), not
camera z. The event chain preserves file order and can change later event state
in the same frame. Disabled events remain linked. The type dispatch at03FDAD
has90 entries indexed by type-1. Types1–5 are event toggle/position/switch/timer
controllers; type6 moves FXO z, type7 x, type8 y. Motion uses
speed*frame_delta/2 (signed integer division), not fixed40Hz catch-up.

## Inventory

InitPC010678 clears the inventory, grants recorder item120, sets power bit0,
energy/max40 (scene11:100), cash85 (scene11:60), armor six×30. Item records
at Xox+130D64 are40 bytes,40 slots: name20, type s16, icon-object s16,
console icon i32, quantity i32, kind/subtype/flags/variant bytes, value i32.
Armor/max are s16 arrays at1313C8/1313D4; ammo/max at1313E0/1313F8.

LoadALLITEMS026AD0 reads SCENE global entry101 as154-byte records. Runtime
item definition is60 bytes: name20; kind/subtype/icon/flags; disk+142 i32;
disk+146 s16 console icon; disk+32 s16; disk+24 spell byte; disk+34 value;
dice at disk150/151/152. PROP placement auxiliary s16 disk+26 is the item
definition id passed by CollObject to CollItem042700, not the PROP definition.

UseItem01A85C: kind9 fills player life to maximum and consumes one only on
change; kind11 repairs every positive armor part and consumes one only on
change. Kind14 type22 selects power5; type111 toggles flight3; other keys
activate matching switches. Kind0 type25 toggles flight1; type104 summons the
scene9 aircar; type120 opens the speech recorder unless text is active.
Consumables with positive quantities clear their type at zero; negative means
unlimited. FlyLanding01A7BC restores body shape_z+shape_z/2+1000 and clears
flight/crouch offsets. No floating point is used.

cmselect.cpp MakePCSpellList056DDC/MakePCWeaponList057154/MakePCKeyList057A0C
build sorted indices and a20-entry reverse shortcut map. Inventory has40 slots,
not20:20 is the selector display limit. Selection is a5×4 grid, bounded without
wrapping; cursor movement cannot select an empty final-row cell.

## Vehicles

SwitchSearch01B381 scans vehiclePERSON0962BC for the current scene, stopping
at the first zero. Table: scene1[84], scene3[20,195], scene9[11], scene11[55,41].
Distance must be strictly<7000, vehicle floor_z+shape_z strictly above player's
floor, player's floor+shape_z+height strictly above that point, and absolute
signed16 circular heading difference strictly<1800. Monster kind5=tank,
kind17=aircar. Other types do not enter. No debug vehicle override is used.

SetTankMode012794 entering hides/removes the old player object at1F8000,1F8000,
changes player person/object to the chosen vehicle, sets hand mode7 and clears
stance offsets. Aircar sets flight mode2. Exiting restores on-foot object at the
vehicle's location/heading, reinserts it and invokes ClearVehicle046A78, whose
ClearObject collision displacement is owned by Sim. Both transitions reset pitch
and body shape_z+shape_z/2+1000. Physical vehicle movement remains Sim's module.

## Current verification and gaps

`tools/scenarios/events-door.txt` uses only original key input. The successful
capture approaches switch51, waits for braking to finish, presses Enter and
opens its door. Original state changes include switch51 and event134. The
capture is `captures/events-door/frames.csv`; native game hooks and vehicle
overrides are disabled by the original oracle wrapper.

Focused reducer comparison:

```
tools/run_capped.sh make -C port -f tests/events.mk B=build/Events2 check build/Events2/events_trace
tools/run_capped.sh port/build/Events2/events_trace orig/retail \
  captures/vehicle-earthmother/frames.csv captures/vehicle-earthmother/events-input.csv
```

The original door run matched252 pre-frame checkpoints and2,806,272 event/switch bytes.
The comparator supplies recorded player position, clock and key observations as
**inputs to the event reducer**. It independently initializes events from retail
scene records and computes every compared event value. No expected event output
is imported. This proves the isolated switch/door reducer scenario, not integrated
movement/AI/pixel parity. There are251 observed frame-to-frame result edges.

`test_events` also passes full-health nonconsumption, armor repair consumption,
invalid slot rejection, sorted weapon/power selection, free/full auto-slot search,
strict vehicle distance and heading acceptance, original BuyItem consumable
stacking, inventory selling, saved event511 round-trip and descending-flat
maximum4000 rider movement. Printed ERROR diagnostics are intentional negative
tests for invalid inventory slots, absent/stale prop placements, invalid actor
mappings and wrapped out-of-map coordinates.

StrictC99 focused tests and the complete desktop target linked successfully with
`B=build/Events2`. Running `port/build/Events2/test_events orig/retail` also loaded
all11 retail scenes and applied80 cosmetic ticks to each without an error.

All90 MoveWorld dispatch types now have implementations. The continuation adds
types38(03B87E),40(03BA5A),41(03BB70),42(03BC15),46(03C572),
47(03C6B7),48(03C700),49(03C760),50(03C8CA),51(03CA4E),
53(03CDEC),54(03CF26),56(03D0AA),57(03D0F3),68(03E351),
69(03E451),70(03E6D8),72(03E9B4),83(03F36D),87(03F833).
The default rejects invalid types, rather than silently skipping records.
Negative actor shape heights retain their signed level calculation.

Scene cosmetics follow037DCA..0387D9 and initialization0116F9..011D46:
panel pixel-pointer aliases preserve destination dimensions, four-phase water
uses0,1,2,1 (not0,1,2,hold), scene5 is gated by switch3, and the prop animations,
scene8 RNG bobbing, scene9 rotating prop46, and scene10 gem removal are retained.
LoadPANELS027289..0272B0 makes the original panel-to-texture map identity;
aliases read immutable panel pixels, not a previously aliased destination.
DrawConsole01942F..019822 consumes auto-destruction, including strict countdown
thresholds at092630, announcement sample sequencing and PCDeadMan on expiry.
`events_countdown` is a separate post-world console tick for the integration owner.
Selected weapon spell/slot and melee dice now travel through selector/save state;
coder generation uses original04D3B0 and saves its24 bytes explicitly.
Fixtures assert both selector equipment paths, all selected-field/coder/bet
save round-trips and countdown rebasing from a different load clock.

The added focused fixtures exercise all20 added dispatch types, signed actor
height and invalid mapping, panel alias timing, coder alphabet and countdown
threshold/expiry. These are source-grounded unit checks, not90-type runtime
oracle parity. Genuine Training tank entry and exit are now compared below;
aircar runtime parity is not claimed. The supplied retail SAVE010.DAT is the initial
scene1 position, not a vehicle save. No vehicle-debug override, coordinate patch
or fabricated save is used.

## Extended original-input reducer comparison

`events-ladder.txt` and `events-corridor.txt` retain the original first-switch
path, then recenter with C, continue forward with Control jump, and turn in the
post-door brown corridor. They were captured with the zero-hook original wrapper:

```
tools/oracle.sh vehicle-earthmother --frame-trace --event-trace \
  --trace-range x:139190:1536 --trace-range x:139d90:32768 \
  --trace-range x:142b78:128 --trace-range l:206820:8 \
  --trace-range x:14480c:8 --snapshot
tools/run_capped.sh port/build/Events2/events_trace orig/retail \
  captures/vehicle-earthmother/frames.csv captures/vehicle-earthmother/events-input.csv
```

Ladder matched425 checkpoints /14,638,700 independently computed bytes;
corridor matched592 checkpoints /20,390,848 bytes. Corridor ended in scene1,
object936/person0, x640939,y1310719,floor_z131072,heading33792. Only switch51
changed in these routes; neither reached another scene or a vehicle.
The corridor has591 observed result edges; its final row is a pre-frame state.

The later spirit/Earthmother route exposed the position sampling boundary:
feeding HandleNextFrame-entry coordinates reached event131's tile trigger one
frame late (first mismatch at frame1387). That approximation is removed.
`--event-trace` now records **original MoveWorld037CF4 entry** observations in
`events-input.csv`, keyed by frame: actual ticks, player identity, position,
shape height, heading, body/pitch, scene/mode/stance and key bytes.
`events_input` runs the independent switch/vehicle key reducer first; computed
identity/mode is checked against the observed MoveWorld identity, never imported.
Only Sim-owned position/clock inputs then feed `events_move_world`. No next-row
post-event position or event output is used as input.

With the actual boundary, `vehicle-earthmother` matched1459 checkpoints /
50,253,796 independently computed bytes, including event131 and the141/142
spirit/message sequence. There are1458 observed result edges and zero vehicle
rows. Earlier door/ladder/corridor counts above describe their historical
pre-frame-input runs, not the corrected phase's runtime evidence.

The genuine `vehicle-river` route uses original Backslash64.3 to finish
Earthmother's message, opens door143 and advances out of the cave. It matched
1650 checkpoints /56,832,600 bytes (1649 result edges), still with zero vehicle
rows. Keyboard01309A's full skip stops all speech segments. Completion links
must be consumed **after actors**, by `events_text_finish` at the original
RenderBellsAndWhistles019423/01942A phase, not immediately after MoveWorld.
The earlier event-only gate did not observe NPCs between those phases. A fresh
water-route textTrigger trace proves that immediate post-World completion
released NPC130's state3 one frame early; see `audio.md` and
`check-actor-dialogue` for the measured boundary and regression.
Its endpoint is scene1 x24.9897,y13.75,floor2,headingC000. The route has not
reached the first tank: the remaining walkthrough path is river/lifts, Mung and
his access card, the bridge gap and the tank compound. No later scene is claimed.

The comparator now checks computed player object/person identity **before**
position inputs, and optional tankFlag/PCTankHand/hand-mode observations.
It compares event type/enable bytes when recorded, loads the first recorded scene
from its retail records, and requires a computed scene request for any subsequent
scene change. No later-scene route has yet exercised that transition path.
It rejects traces missing complete loaded event-state/payload/switch-state ranges.
Controlled negatives rejected both an empty-range trace (previously accepted
with zero compared bytes) and a changed tankFlag byte at frame0; the unchanged
corridor trace still matched. The negative copies live only under ignored captures.
Deleting an idle interior MoveWorld observation and supplying a header-only
phase file both fail explicitly; only an uncompleted final frame may omit its
phase observation. Unused trailing observations also reject.

## Genuine Training tank entry

Controls' original-menu Training route earned the LOWER checkpoint in
`captures/scene11-training-lower-lift-save/saves/SAVE000.DAT`, SHA-256
`39842d7ed24bd600a3eedbd39ed3ea775217e527a1ab16d8414866e18675f0cc`.
The original then loads that unchanged save and approaches/enters the real tank
using ordinary keys. Full input schedule, import/export hashes and producer
identity are in `captures/scene11-training-tank-events/run.json`; the scripted
original save after entry is TANK, not a fabricated starting state.

```
tools/run_capped.sh make -C port -f tests/events.mk B=build/Events2 check build/Events2/events_trace
tools/run_capped.sh port/build/Events2/events_trace orig/retail \
  captures/scene11-training-tank-events/frames.csv \
  captures/scene11-training-tank-events/events-input.csv \
  --replay-load-slot 0 --user captures/Integrator-training-tank/user
```

The native user SAVE000 hash was independently checked against the exact LOWER
hash above. Real UI/World/Sim/AI/Events save import initializes the reducer;
expected frame outputs are never imported. All589 pre-frame checkpoints matched,
20,287,516 independently computed event/switch/vehicle bytes, including98 vehicle
rows. There are588 observed result edges. This adds real scene11 reducer coverage
to the earlier scene1 routes, not an inter-scene-request comparison.

Enter in frame489 queues tankFlag1; original MoveWorld489 still sees the foot
player. Original HandleNextFrame01C1CB..01C1FC consumes that prior request before
keyboard01C29A, rejecting a dead selected vehicle. Entry occurs at MoveWorld490
and appears in pre-frame491: object1375/person0 becomes object1416/person41,
mode5 becomes7. The native input reducer now follows that ordering instead of
consuming a newly generated request immediately. The pending/dead-target fixture
failed before the fix, then passed without changing comparator predicates.

Captured producer SHA-256:
binary `99bd75b209f15df150a4d83d8b739fabfc5a012d4e3d524f6faa8bbb26716bb1`,
patch `411930364140c12023c77ff3dfbc026279d4f5bea98715356b8cfcae9110b4c3`.

### Queued original Tab exit and modern F

`tools/scenarios/scene11-training-tank-exit.keys` preserves the genuine LOWER
load/entry/save prefix, adds ordinary Tab at48 seconds and captures at50.
`captures/scene11-training-tank-exit/run.json` records the same exact LOWER hash
and original producer hashes above, with all five ranges and MoveWorld inputs.

```
tools/run_capped.sh port/build/Events2/events_trace orig/retail \
  captures/scene11-training-tank-exit/frames.csv \
  captures/scene11-training-tank-exit/events-input.csv \
  --replay-load-slot 0 --user captures/Integrator-training-tank/user
```

All639 checkpoints /22,009,716 independent bytes matched, including116 vehicle
rows and638 observed result edges. Exit occurs at original MoveWorld606 and
appears in pre-frame607: object1416/person41 returns to object1375/person0, mode5.
As for the other reducer comparisons, Sim-owned position observations are inputs;
this is not a full physical movement or ClearVehicle displacement parity claim.

Classic Tab0F edge mask02 queues tankFlag-1 at0131CF..0131F5. The prior request is
consumed before the following keyboard phase at01C201..01C23F; a dead vehicle
also exits automatically only while the foot player's life is positive.
Enter is not a vehicle exit alias. Enhanced F now synthesizes that same Tab
edge in `sim/modern.c`; the physical enhanced Tab automap binding is unchanged.
Fixtures cover held-vs-edge Tab, delayed exit, ignored Enter, modern F transport,
and both foot-life branches of dead-vehicle handling. The Enter regression failed
before the cutover; all fixtures and the strict complete desktop build passed.

## Full inventory and key-selector reducer proof

The genuine `vehicle-regen-menu-items` route starts from original-written
SAVE000 SHA256`f903211d8f42461a5ba5fad06325df29777d4a60d64d0222bd9b91583a7f3796`.
Fresh read-only observations are in
`captures/Standing-original-bee11c5/original/vehicle-regen-menu-items/`;
producer binary63db872a and patchad772577 are pinned in its `run.json`.
The original executable runs the same key schedule, with no scene/vehicle
override or fabricated initial save.

```
tools/run_capped.sh .venv/bin/python port/tests/test_events_trace_original.py \
  port/build/Events2/events_trace orig/retail \
  captures/Standing-original-bee11c5/original/vehicle-regen-menu-items \
  captures/earned-telecom
```

All341 checkpoints and12,361,932 independently computed bytes match.
Every row compares all1600 inventory bytes,116 equipment/status bytes
(including draw-computed regen interval),88 key-selector count/index/shared-map
bytes and PERSON0 HP. The global spRegenTimer206834 is also required and checked.
The one observed root pickup is frame114/tick547, type6/object889/source936;
F4 uses it in frame216, producing HP17→40 at pre-frame217/tick657 and removing
the item/quantity. There are47 actual ProcessSelector input rows and one real
UI_Save reset,340 paired normal draws and one Save redraw.

Draws use the canonical StatusMonitor and PC draw-state producers with every
actual conditional clock read. The earlier fixed frame-clock approximation
failed at frame67; unconditional HUD advancement then failed at selector
opening169. The shared selector monitor predicate fixes that actual game bug;
neither timer rows nor energy/status bytes are skipped.

Seventeen positive/negative controls reject missing/wrong draw clocks, an altered
conditional deadline read, changed inventory/equipment/interval/
selector bytes, HP, partial spans, malformed or late pickup inputs, missing
selector rows and routing inconsistent with the real CMDATA setting.
A counterfactual pointer only routes after a counterfactual Mouse setting;
it is not presented as original hardware evidence. The genuine route has
mouse0/joystick0. Unit tests compare the byte getter against the real SAVE
exporters and exercise the real pickup/use API.

This is an isolated reducer/shared-draw-state gate, not integrated AI/RNG,
movement or pixels. Twelve bytes at131418 (Sim flight height and two guest
PERSON pointers) are explicitly excluded. Weapon/power selector count/index
blocks are unverified.
The twelve unverified link-address bytes are PC-owned spellTimer206828,
waveTimer20682C and waveFrame206830, not guest pointers. The tool prints these
and the Sim/guest-pointer exclusions separately. It rejects dedicated P/pause:
HandleKeyboardEvents013F6A..013FED shifts Events-owned deadlines on resume,
which this reducer does not model. The observed GUI Save redraw is a different
path. Death/shop/mid-trace Load likewise require the full game-loop gate,
rather than silently dropping those frames. An unpaired terminal checkpoint
does not invent draw clock inputs.

PCTankHand206824 now compares canonical `sim_state.tank_hand`, not vehicle type
at Xox130D14. SetTankMode0127A3 resets that flag; the transition fixture starts
it nonzero to ensure the reset is real. Tank secondary-input processing belongs
to Sim's source-derived14E6D/14FF2 path.

## Review corrections

Compact runtime props now retain their source disk record index and cached item
auxiliary; CollItem uses the copied item value and ReScaleProp the disk index,
rather than treating skipped disk records as runtime slots.
Fixtures cover a skipped leading record, the correct item/shape z and stale
record rejection. Inactive countdown save intervals are zero; active intervals
remain relative to the supplied save/load clock (original05014D/051515).
Inventory/weapon/switch save errors name SAVE file offsets, not Xox addresses.
Shared unsigned wrap helpers now cover event motion, plane/corner arithmetic,
lighting, position/shape transforms, timer subtraction and vehicle tile/body
calculations. Boundary fixtures cover INT32 wrapping and out-of-map rejection.

CollObject043878 tests runtime PROP+6 before entering CollItem: zero takes the
generic collision branch0438C4, not the item path. LoadPROPS0245AA copies this
word from disk+26. `world.prop_items` retains it for both collision eligibility
and item lookup. Calling the item adapter on scenery is a non-consuming no-op;
malformed nonzero definitions and zero-valued bundle grants still reject.
The zero-auxiliary regression failed with the reported CollItem0 error before
the fix and asserts unchanged object activity, inventory, notification and RNG.
All11 retail scene loads check cached auxiliary words against source records.

Player feedback at Xox+1444E8/+1444EC is AI-owned: event46 teleport, event87
drain and scene10 gem effects use the shared `ai.h` flash/effect-state accessors.
The duplicate Events fields are removed. Far drain sets flash2 only when clear;
near drain clears flash and replaces the effect state with RND(5)+1. These
transient fields are absent from the explicit SAVE codec, so no save bytes change.
Fixtures assert teleport feedback, both far-flash cases, near feedback and both
gem flicker/expiry paths against the state actually consumed by player effects.
The common crusher damage helper also raises that AI flash state to at least3,
preserving higher values and leaving the PCSound sample handle alone.
All four original branches agree (037423,0376AB,037886,037A9C); fixtures exercise
sideways/wall/descending-flat/rising-flat crushers and both flash cases.

HandleKeyboardEvents01340D..013548 consumes F1–F12 and digits1–8 as twenty
selector shortcuts, clearing every scanned press and choosing the last one.
Enter takes precedence and clears keyboard state. MakePCKeyList057D5E writes
assigned shortcut slots without resetting the remaining map. Fixtures exercise
all twenty mappings, simultaneous presses, unmapped keys, Enter priority and
F4 Regen Pak use (HP17→40, item6 quantity1→0). Selection completion/cancellation
uses AI's original equip/aim/fire-reset provider rather than changing equipment
words alone. Active and newly opened selectors still proceed through MoveWorld.

Counts/indices are independent for weapons (+144814/+144818), powers
(+14491C/+144920) and keys (+144974/+144978). The temporary item/order lists
and twenty-entry map at+14497C are shared. MakeWeapon057165 and MakeSpell056DED
clear that map to-1; MakeKey does not. A stale F4 entry after consuming the last
Regen Pak is legal: signed index<0 or index>=current key count skips only
UseItem at01E4D4..01E4F0, while mode/aim/cursor/fire/cooldown still close/reset.
The regression drives consumption, rebuild and stale confirmation, not a
fabricated expected index. Weapon/power and item-id bounds remain checked.

ProcessSelector01B580 ports keyboard navigation, mouse hover thresholds and
ragged-last-row clamping, joystick debounce, grid aim, ClipGloveAim01B4EC and
pointer warps. Classic mouse coordinates/device availability pass through the
existing physical input struct; modern relative aim stays separate. Fixtures
exercise all20 grid positions, every mouse threshold edge, ragged lists and
released E0 key edges. The genuine regeneration fixture has **mouse0/joystick0**,
so it does not establish hardware mouse or joystick parity.

The non-running jump-key hand path is ProcessInputEvents014D47..014F1F, not a
Ctrl special case: running jumps; otherwise a grounded foot player enters
mode3 without the extra running-eligibility restrictions. Release014F75 restores
the single Sim-owned previous mode (+144810), resets aim and leaves latch1.
Mode3 skips foot keyboard motion and preserves active selectors. Aim clipping,
mouse-hand/cursor flags and late sword-step+9 use the original player fields.
The separate Events copy of previous mode has been removed.

SaveGame050A8F..050AC1 and LoadGame0515F8..05162A perform exactly six writes:
three counts0 and three indices-1. `events_reset_selectors` makes only these
writes; native phase flags are initialized separately on scene initialization.
Main calls it after successful durable save/name writes and successful load
import, never on a failed operation. A real-main fixture drives `menu_input`,
Sim, player input and Events: F1/F2/F3 open selectors, F5/F7 stay inside the
active selector, Escape cancels before pause, and OS quit still works.
Outside selectors the file-menu shortcuts remain available; Help uses original
keyHELP=53 decimal (scan35 hex; data0962B8, handler014026), not F1.
F4 opens the regular pause menu only outside selectors (0135C3).

UI capture regression attribution: isolated4983343 passes the complete retail
UI suite;69261c3 first fails its old tint-coverage assertion. That commit
intentionally records world-dependent Hor+ canvas pixels outside the bounded
CPU tint rectangle. The fixture now distinguishes untouched world coverage
(LUT) from an explicitly opaque backdrop pixel (replace), verifies both tint
stages compose, and still requires outside CPU pixels to remain unchanged.
No renderer/tint implementation was weakened to satisfy the old assertion.
The same frozen checkpoint runs the real application through a held-Ctrl
generic mode3 frame with no lists: that valid inactive selector draws nothing,
while unknown selector kinds still fail. This catches the kind0 rendering
fatal reported by the genuine blade replay, rather than testing replay alone.

`events_frame_begin` owns the prior vehicle-request/dead-vehicle transition at
01C1CB..01C23F, after missile/soul updates and before keyboard/motion. Late
`events_input` cannot consume a request: fixtures explicitly distinguish those
phases. Sim owns the early call and refreshes cached player pointers afterward.

Integration contracts: `events_init` now resets scene metadata, **preserving**
player inventory/status. AI's `ai_init_pc` owns original boot/new-game
initialization and its RNG calls. `events_load_tables` reads numeric monitor
tables from CM_HI.EXE once. The main-loop owner consumes `scene_requested` and
`ending_requested`; UI consumes `shop_requested`, recorder and item notifications.
`events_save_import/export` explicitly bridge inventory/status/event/switch
runtime to UI's original byte codec. Runtime table/state fields never serialize
native struct padding. Hostile bundle recursion and repeated empty random draws
fail at named finite limits rather than hanging.
