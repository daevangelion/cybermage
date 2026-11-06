# Enhanced controls (opt-in)

`--controls modern` selects the action layer. `--controls classic` is the default.
Original frame replays use classic input; combining `--replay` and modern
controls is rejected. The modern layer does not feed WASD through the DOS IRQ latch.

## Defaults

| Action | Keyboard/mouse | SDL GameController |
|---|---|---|
| Move/strafe | WASD or arrows | Left stick |
| Run | Hold either Shift | Left-stick click |
| Look | Relative mouse | Right stick |
| Jump from rest or movement | Space | A |
| Use, switches, vehicle entry/exit | E or Enter | X |
| Fire/cast | Left mouse | RT |
| Hold hand aim (stops on-foot motion) | Right mouse | LT |
| Next/previous selection | Wheel down/up or `]`/`[` | RB/LB |
| Weapon/power/item selector | F1/F2/F3, I for items | Y for items |
| Numbered selection in current category | 1–9, 0 | Rebindable buttons |
| Crouch | Ctrl | B |
| Centre look | R | Right-stick click |
| Fly up/down | Page Up/Page Down | D-pad up/down |
| Land | F | D-pad right |
| Aircar gun narrow/widen/centre | Z/C/V | Rebindable buttons |
| Menu | Escape | Start |
| Automap | Tab or M | Back |
| Pause | P | Rebindable button |

Wheel and shoulder buttons cycle the last selected category, initially weapons.
Weapon/power cycles select directly; item cycles show the item selector rather
than consuming an item merely by cycling. Number keys use the original
selector shortcut ordering. Menus/shops accept WASD/arrows or left stick/D-pad,
A/Space/Enter to accept, B/right click to back out; right stick moves the pointer
and RT clicks. A mouse or touchscreen tap retains the original pointer path.

Movement uses a radial 18% controller dead zone, rescaled to full throw. Combined
keyboard/stick/touch motion is normalised, so diagonals are not faster. Walking
outputs 640, running 768. The adapter scales this magnitude into the original
walking/running momentum caps (48/96) before the existing acceleration reducer,
so partial stick deflection remains analog instead of saturating the request.
Relative mouse heading is -10 angle units/pixel and pitch +0.5 units/pixel;
right-stick full throw requests 1024 heading units and 8 pitch units per frame.
Fractional look is retained between frames; each sampled delta is consumed
once. The adapter clamps pitch to [-224,224]. These are modern control choices,
not claims of an original DOS mouse match.

Digital sources and sticks acquire a context when first pressed/deflected and
keep that context until released/centred. Holding fire while opening a menu
cannot click the menu. A new game/menu binding cannot consume a key held in
another context. Multiple devices combine actions without one releasing the
other's input. Focus loss releases inputs and relative capture; controller
removal releases only controller input, leaving keyboard/mouse available.
Inputs held at focus loss are suppressed until their release/neutral event;
stale key repeats on focus regain cannot resume walking or turn into clicks.

## Rebinding

Create `controls.cfg` in the directory selected by `--user DIR` (otherwise the
platform user directory). Missing files use the defaults. Parsing is bounded to
8192 bytes and 255 bytes per line; invalid settings fail with a file offset.
The entire configuration is rejected atomically, not partially applied.

```
# DOS set-1 key numbers below are DECIMAL, not hexadecimal.
foot.forward = key:17, pad:11
foot.jump = key:57, pad:0
foot.use = key:18, pad:2
foot.aim = mouse:2, trigger:0
vehicle.gun_narrow = key:44, pad:9
vehicle.gun_widen = key:46, pad:10
menu.use = key:28, pad:0
shop.use = key:28, pad:0
mouse_yaw = 10
mouse_pitch = -0.5  # negative inverts vertical look
```

Each `context.action` line replaces that action's bindings in that context,
with up to three comma-separated sources; `none` unbinds it. Contexts are
`foot`, `vehicle`, `menu`, `shop`. Actions are `forward`, `back`, `left`,
`right`, `fire`, `jump`, `use`, `aim`, `next`, `prev`, `menu`, `map`, `run`,
`crouch`, `center`, `fly_up`, `fly_down`, `gun_narrow`, `gun_widen`,
`gun_center`, `weapons`, `powers`, `items`, `pause`, `land`, and
`slot1`–`slot9`, `slot0`.

Sources: `key:0..127` (DOS set-1 scans), `mouse:0..7` (left=0, middle=1,
right=2), `pad:0..20` (SDL GameController button enumeration), `trigger:0..1`
(LT/RT), `wheel:0..1` (up/down). Common pad numbers: A=0, B=1, X=2, Y=3,
Back=4, Start=6, L3=7, R3=8, LB=9, RB=10, D-pad up/down/left/right=11/12/13/14.
`mouse_yaw` and `mouse_pitch` accept finite values in [-100,100].

Automap is a modern live-topology overlay, not a reproduction of DOS AutoMap
artwork or its exploration-reveal rules. It draws the current height level's
wall cells and the actual player position/heading in a 192×192 upper-right
panel. P uses the existing pause menu rather than a separate pause screen.

## Android touch

Launch the exported `DataActivity` with the optional intent string extra
`controls=modern`; it forwards the choice to the private `GameActivity`.
Without it Android stays classic. The virtual left stick and six outlined
buttons use the black bars outside the 4:3 game area where space permits.
Otherwise they occupy lower corners. **F/J/U/W/M/A** mean fire, jump, use,
next weapon/category entry, menu and hold aim. A right-side swipe looks.
Each finger keeps its original stick/look/button/menu role until lifted;
buttons support simultaneous move/look/fire/jump. Menus retain tap-to-click,
including a complete down/up tap between two simulation frames.

## Integration and provenance

`controls_frame` consumes raw `cm_input` into a separate semantic `cm_input`.
The persistent physical state must not be overwritten by semantic output.
`sim_modern_input` writes movement angle/request, heading and camera pitch;
the existing frame reducer supplies acceleration, collision and the original
jump-table setup. Jump and held hand aim are distinct, unlike classic Ctrl's
Forward+Ctrl jump versus stationary hand-screen fallback. Classic IRQ bytes,
first-key swallowing and original frame replay transport are unchanged.

Original input fields and jump physics: `docs/re/sim.md`, CM_HI.EXE
`HandleKeyboardEvents` 012DD4, `ProcessInputEvents` 014754 (jump setup
014D5E..014E63), `MovePerson` 046D30. Selectors/use/vehicles:
`docs/re/events.md`, cmselect.cpp 056DDC/057154/057A0C, `SwitchSearch` 01AF9C.
Prior-art guidance is credited to `[rust: docs/CONTROLS.md]` and
`[rust: REVERSE.md, Desktop controls and asset-free release; Gamepad-only
revision]`: fractional mouse look, 640x240 DOS versus 640x480 UI pointer space,
context ownership and dedicated grounded jump. No XInput dependency is used.
SDL controller added events carry a device index; removed events carry an
instance ID: <https://wiki.libsdl.org/SDL2/SDL_ControllerDeviceEvent>.

## Focused verification

```
tools/run_capped.sh make -C port -f tests/controls.mk B=build/Controls controls-check
```

The pure action unit test, SDL virtual GameController headless smoke, full
`sim_frame` analog/jump fixture, live-map test, SDL dummy-video multi-touch test
and integrated new-game synthetic-input smoke have run successfully under
strict C99 warnings. The integrated smoke uses the actual `--controls modern`
main loop and observes changed world position, heading/pitch, active jump and
the player's fire reducer. It writes an actual `--user/controls.cfg` rebinding
forward to T and verifies that request before injecting controller motion.
The simulation fixture verifies full walking
displacement is twice half-stick displacement, and running is twice walking.
It also exercises weapon cycling, selector category changes and shortcuts,
non-consuming item cycling, item use and the existing flight-landing reducer.
The tests also exercise normalisation, fractional look, context
release, rebinding rejection, keyboard/mouse/controller movement/look/jump/fire,
controller disconnect, focus loss, menu taps, simultaneous gameplay fingers and
an overlay pixel outside the 4:3 area. Menu mouse/controller hover motion,
successive wheel detents, vehicle key translation and sub-game-pixel swipe
accumulation have regression tests. The platform exports bounded touch-overlay
rectangle geometry for either presenter; headless/classic/menu paths produce none.
These are action/transport tests, not a
physical-controller usability or classic replay parity claim. The integrated
build, original movement replay and Android emulator capture are separate gates.
