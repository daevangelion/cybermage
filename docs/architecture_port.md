# Native port architecture

Core state is typed, caller-owned C99 storage. `world` owns scene resources,
objects, PERSONs, maps, sight and integer math; `sim` owns movement and classic
input; `ai` owns actor/combat decisions; `events` owns triggers and inventory;
renderers read world state. SDL and OS access remain behind the platform adapter.
User-file writes (CMDATA, GAMENAME, SAVE and AUTO) use a same-directory temporary
file, checked write/close, then atomic rename. A failed write does not truncate the
old file; temporary files are removed on write, close and rename failures.
SDL application background/foreground events pause the platform epoch at the
event-watch boundary, including time blocked inside Android's poll. Simulation
deadlines and frame pacing exclude suspension, not merely movement deltas.
Original replay fails closed before starting a movie, whose modal frames have no
CMTRACE clock/input contract. Every executed actor frame requires measured
actor-clocks.csv entries; only the unexecuted terminal checkpoint may omit them.
GL validates light descriptors only when tile or object geometry uses them.
Light0 ignores its descriptor and selects the default distance curve, as software
does; event-written unused descriptors do not abort presentation.
SDL_RENDER_DEVICE_RESET reaches both control modes. The GL renderer discards all
lost-context names and geometry/image/revision caches without deleting invalid
names, recreates programs/buffers/framebuffers, then reloads caller-owned indexed
assets and palette before drawing. The runtime test destroys/recreates a real GL
context and checks the same scene pixel after reset.

`vis` owns the pixel-free original ray traversal and ordered scene lists.
Dependency direction is `render_sw → vis ← target → world`; neither `vis` nor
`target` includes a renderer header or owns pixels. Targeting reads explicit
original-view coordinates and world sprite extents. The game collects the
centre-column candidates after camera follow; the next frame's player firing
consumes them, regardless of the chosen presentation backend.

## Original-to-native combat entries

| CM_HI.EXE original | Native |
|---|---|
| InitPC 0x10678; RestorePCInfo 0x1B8F8 | ai_init_pc; ai_restore_pc_info |
| StartClock 0x1ED00 | sim clock baseline + ai_start_clock |
| UpdateObjects 0x348C4 | ai_update_objects |
| NewMONBound steering 0x46507..0x46A74 | ai_monster_blocked |
| SetAutoTarget 0x2F150 | target_query + ai_set_auto_target |
| RayCast 0x5CBF8; CheckTile 0x5C5C8; CheckObjects | vis_build_lists |
| CameraInit 0x597EC; MakeRayAngles 0x58E94 | vis_prepare |

## Deliberate original undefined-memory deviation

Normal play initializes PERSON `unk_0024` (the accuracy decision countdown) to
zero. Original LoadPERSONS allocates an uninitialized 0xC0-byte record at
0x222FB, initializes surrounding fields at 0x22832..0x228EE, but never initializes
+0x24. Its value can therefore be leftover sprite pixels from the Watcom heap.

Measured source: original actor 23's +0x24 is runtime 0x968E10..0x968E13.
Read-only write-watches in `captures/combat-actors/frame000000.log` show the
first writes C8/0E/0E/C8 at original FlipObjDiagonal 0x66343. That function
allocates a width*height scratch buffer at 0x662ED, transposes pixels, copies them
back, and frees the scratch at 0x66380. LoadPERSONS subsequently reuses it.
UpdateObjects first decrements that stale word at 0x34E59. Reproducing this would
couple AI to historical allocator reuse and rendering resource scratch storage;
the native core does not emulate that accidental dependency.

For verification only, `tools/ai_trace.c --import-undefined ORIGINAL.persons`
may import exactly PERSON +0x24 for each actor from the original post-load
snapshot. It validates actor count/identity, logs the import, and copies no HP,
coordinates, RNG state or other fields. This flag is off by default and absent
from the game runtime. Actor and RNG comparators remain strict: no downstream
fields or calls are masked. A flagged replay is conditional equivalence under
that explicit initial-state input, not proof that normal-play initialization
matches the original's undefined heap contents.

### Attack range before the first admitted target

UpdateObjects035240 writes an admitted target's PERSON+0C distance into one
call-local stack slot, reused by later actors. State4's lost-target restoration
at035C18 does not refresh it. Native preserves this lifetime with a scalar copy:
after an admitted target, a later restored-target melee attack uses that preceding
distance, even if the preceding PERSON record subsequently changes.

Before the first such write in a call, the original instead reads uninitialized
stack bytes. Normal play does **not** emulate stack/heap history or abort:
that melee attack uses its own actor's cached PERSON+0C distance. This is a
deliberate deviation only for the undefined initial value, not permission to
replace the defined cross-actor carry. The fallback does not mark the original
slot initialized; further affected actors use their respective cached distance
until a real admitted-target assignment.

`ai_state.undefined_distance_person` is reset to0 at UpdateObjects entry;
the first actual fallback read records its NPC ID and
`undefined_distance_value`. NPC IDs start at1, so person0 means no fallback
was observed in that call. These fields are diagnostics only. No HP, position,
RNG or replay output is substituted. `check-melee-distance` checks the original
defined-value cases, scalar copying, diagnostic reset and nonfatal fallback.

## Event reducers and ownership

| CM_HI.EXE original | Native |
|---|---|
| LoadEVENTS 0x2732C; LoadSWITCHES 0x274F8 | events_init |
| MoveWorld 0x37CF4, 90-type table at0x37B8C | events_step, private dispatch |
| LoadScene cosmetics 0x116F9..0x11D46; MoveWorld 0x37DCA..0x387D9 | events_scene_cosmetics; events_cosmetics |
| DrawConsole auto-destruction 0x1942F..0x19822 | events_countdown |
| SwitchSearch 0x1AF9C; SetTankMode 0x12794 | events_switch_search; events_set_tank_mode |
| MakeCoderStr 0x4D3B0 | MoveWorld type56, events_state.coderStr |
| Save/load inventory/event arrays | events_save_export; events_save_import |

`events_step` combines `events_input` and `events_move_world`; the former handles
key/vehicle interaction and the latter starts at the original post-Sim boundary.
It reads the current simulation clock/key state and mutates caller-owned
events, inventory and world objects. It reports scene/shop/ending requests rather
than calling the game loop or UI. `events_countdown` runs in the console phase
after world/actor work and signals `pc_dead_man` on expiry; the integrator owns
death presentation. Water aliases change world texture pixel spans/revisions,
not destination geometry; both renderers remain read-only consumers.

`port/tests/events_trace.c` isolates event reduction: recorded position/clock/key
observations are inputs, not reproduced movement. Event/switch results and
vehicle identity are independently computed. Position/clock inputs are observed
at original MoveWorld entry, not inferred from the following frame's outputs.
Complete event-state/payload/switch
ranges are mandatory. Optional vehicle flags and hand mode are strict outputs;
foot combat/input modes outside Events are not claimed as generally reproduced.
The genuine routes and the runtime evidence ceiling are in `docs/re/events.md`.
