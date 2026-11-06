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
oracle parity. Vehicle entry/exit still needs the real-oracle route comparison;
the supplied retail SAVE010.DAT is the initial scene1 position, not a vehicle
save. No vehicle-debug override, coordinate patch or fabricated save is used.

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
rows. Keyboard01309A's full skip stops all speech segments; completion links
are consumed after MoveWorld, matching the original console phase.
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

## Review corrections

Compact runtime props now retain their source disk record index; CollItem and
ReScaleProp use it rather than treating skipped disk records as runtime slots.
Fixtures cover a skipped leading record, the correct item/shape z and stale
record rejection. Inactive countdown save intervals are zero; active intervals
remain relative to the supplied save/load clock (original05014D/051515).
Inventory/weapon/switch save errors name SAVE file offsets, not Xox addresses.
Shared unsigned wrap helpers now cover event motion, plane/corner arithmetic,
lighting, position/shape transforms, timer subtraction and vehicle tile/body
calculations. Boundary fixtures cover INT32 wrapping and out-of-map rejection.

Integration contracts: `events_init` now resets scene metadata, **preserving**
player inventory/status. AI's `ai_init_pc` owns original boot/new-game
initialization and its RNG calls. `events_load_tables` reads numeric monitor
tables from CM_HI.EXE once. The main-loop owner consumes `scene_requested` and
`ending_requested`; UI consumes `shop_requested`, recorder and item notifications.
`events_save_import/export` explicitly bridge inventory/status/event/switch
runtime to UI's original byte codec. Runtime table/state fields never serialize
native struct padding. Hostile bundle recursion and repeated empty random draws
fail at named finite limits rather than hanging.
