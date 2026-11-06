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
tools/run_capped.sh make -C port -f tests/events.mk B=build/Events check build/Events/events_trace
tools/run_capped.sh port/build/Events/events_trace orig/retail captures/events-door/frames.csv
```

The latest run matched252 pre-frame checkpoints and2,806,272 event/switch bytes.
The comparator supplies recorded player position, clock and key observations as
**inputs to the event reducer**. It independently initializes events from retail
scene records and computes every compared event value. No expected event output
is imported. This proves the isolated switch/door reducer scenario, not integrated
movement/AI/pixel parity. There are251 observed frame-to-frame result edges.

`test_events` also passes full-health nonconsumption, armor repair consumption,
invalid slot rejection, sorted weapon/power selection, free/full auto-slot search,
strict vehicle distance and heading acceptance, original BuyItem consumable
stacking, inventory selling, saved event511 round-trip and descending-flat
maximum4000 rider movement. The two printed ERROR diagnostics are intentional
negative tests (invalid inventory slot and absent prop placement).

All event sources compiled under strictC99. An earlier integrated build linked;
the latest integrated link is blocked outside this slice by missing
`ai_pc_input` and `ai_pc_hand2hand`. The focused event tests link and run against
the current other subsystem sources.

**This slice is not acceptance-complete.** MoveWorld dispatch types
38,40,41,42,46,47,48,49,50,51,53,54,56,57,68,69,70,72,83,87 still reject
explicitly; scene-specific water/candle updates and auto-destruction consumption
are not ported. Every type present in the retail scene1 event table has a
dispatch implementation, but only the initial real switch/door path is compared.
Vehicle entry/exit has unit coverage, **not** the required real-oracle vehicle
entry trace. The attempted real route only reached the post-door corridor.
The supplied retail SAVE010.DAT is the initial scene1 position, not a vehicle
save. No vehicle-debug override, coordinate patch or fabricated save was used.

Integration contracts: `events_init` now resets scene metadata, **preserving**
player inventory/status. AI's `ai_init_pc` owns original boot/new-game
initialization and its RNG calls. `events_load_tables` reads numeric monitor
tables from CM_HI.EXE once. The main-loop owner consumes `scene_requested` and
`ending_requested`; UI consumes `shop_requested`, recorder and item notifications.
`events_save_import/export` explicitly bridge inventory/status/event/switch
runtime to UI's original byte codec. Runtime table/state fields never serialize
native struct padding. Hostile bundle recursion and repeated empty random draws
fail at named finite limits rather than hanging.
