# Native port architecture

Core state is typed, caller-owned C99 storage. `world` owns scene resources,
objects, PERSONs, maps, sight and integer math; `sim` owns movement and classic
input; `ai` owns actor/combat decisions; `events` owns triggers and inventory;
renderers read world state. SDL and OS access remain behind the platform adapter.
User-file writes (CMDATA, GAMENAME, SAVE and AUTO) use a same-directory temporary
file, checked write/flush/fsync/close, then atomic rename. A failed write does not truncate the
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
Reset reloads a world only while shared colormaps are live; a disarmed failed
load can resume its Load/Title UI without stale-scene reload. The SDL adapter
also replaces its tracked context handle with SDL's current handle on reset.

`vis` owns the pixel-free original ray traversal and ordered scene lists.
`vis_project`, `vis_new_point` and `vis_prop_texture` are also the single
original projection/near-plane/frame-selection implementations consumed by the
software renderer, pixel-free target query and GL sprite frame selection.
The renderer has no separate target-visibility API. Tests inspect its actual
paint command stream; save and hand tests call the live overlay/hand paths.
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

## Enhanced deviations

Desktop defaults to `--renderer sw --controls classic`. Android deliberately
defaults to software rendering **and modern controls**, because a touchscreen
cannot drive the DOS keyboard layout. Renderer and input choices are independent;
neither changes the simulation RNG, collision, combat rules or target-query view.
Original replay requires classic controls. Enhanced visuals are not DOS pixel
parity evidence.

| Deviation | Activation / flag | Original-mode equivalent |
|---|---|---|
| GPU renderer | `--renderer gl`; Android `--es renderer gl` | `--renderer sw` (default), indexed 640×480 software rasterization. GL uses indexed textures/maps but different raster coverage/depth. |
| Hor+ widescreen and arbitrary output resolution | `--renderer gl`, desktop `--width N --height N`; Android uses drawable size | Software keeps a centred 4:3 canvas. GL extends horizontal world coverage at the original vertical focal length; HUD/menu layout stays 4:3. Gameplay targeting remains the original projection. |
| True pitch | `--renderer gl` (including classic look keys) | Software shifts the horizon; GL rotates the view in 3D. Both consume the same bounded simulation pitch. |
| Modern controls and free look | `--controls modern`; optional controls.cfg under `--user DIR` | `--controls classic` uses CMDATA scan/mask pairs, IRQ9 make/break/first-key semantics and the configured DOS mouse/joystick reducer. Modern mode instead supplies WASD, relative look, analog/gamepad input, dedicated grounded jump, held aim, crouch/land actions and per-context bindings. |
| Android default / touch controls | Default Android launcher; opt out with `--es controls classic` | The desktop default remains classic. Android modern mode adds virtual sticks/buttons, multi-touch look and literal modal taps; classic is available for a hardware keyboard. |
| Selectors and direct cycling | `--controls modern` | Classic glove-hotspot/IRQ selector navigation and shortcuts. Modern mode adds wheel/shoulder/category/number actions and direct tile-rectangle tap confirmation without the DOS glove hotspot transform. |
| Automap live-topology overlay | `--controls modern`, Map action (Tab/M or controller Back) | DOS AutoMap has its own artwork/reveal rules; this overlay is not that artwork. Classic retains the CMDATA AutoMap state toggle/save word, not this Enhanced overlay. Modern Map never emits the vehicle-exit key. |
| SAF import / app-private storage | Android launcher **Import folder** | Desktop `--data DIR` reads an already installed retail folder; Android may likewise receive files through adb. SAF copies only chosen top-level files, with transactional synced replacement and no bundled assets or broad-storage permission. |

Platform-only adaptations are always enabled: checked atomic user-file replacement
rather than DOS in-place writes; lifecycle-safe GL reload and paused suspend clocks;
SDL window scaling/letterboxing, device hotplug/focus release, OS audio and app user
directories (`--user DIR` overrides storage). Their DOS equivalents are persistent
devices, one running process/clock and its writable install directory. There is no
flag to reintroduce torn saves, stale GL handles or time spent suspended.

Verification controls (`--headless`, `--keys`, `--frames`, `--dump`,
`--state-dump`, `--bios-seed`, `--replay`, `--replay-load-slot`,
`--import-undefined`) are explicit test inputs/outputs, not Enhanced gameplay modes.
They never install expected player/actor outputs. The unavoidable original
undefined-memory choices below apply to both presentations; no normal-play switch
claims to emulate allocator or uninitialized-stack history.
`--ticks-per-frame N` (default1, range1..8) advances the headless planning clock
by N 40Hz ticks per rendered frame. Key scripts remain frame-indexed; movement
still uses the original two-tick delta cap while absolute AI/event clocks see
the selected cadence. Use3 or4 for port-first route planning under original-like
render load. Windowed runs ignore the option, and `--replay` always ignores it
and keeps the measured frame/actor/draw clocks.

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
