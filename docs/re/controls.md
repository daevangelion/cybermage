# Enhanced controls (opt-in)

`--controls modern` selects the action layer. `--controls classic` is the desktop
default; Android defaults to modern for touchscreen play.
Original frame replays use classic input; combining `--replay` and modern
controls is rejected. The modern layer does not feed WASD through the DOS IRQ latch.

## Defaults

| Action | Keyboard/mouse | SDL GameController |
|---|---|---|
| Move/strafe | WASD or arrows | Left stick |
| Run | Hold either Shift | Left-stick click |
| Look | Relative mouse | Right stick |
| Jump from rest or movement | Space | A |
| Use, switches, vehicle entry | E or Enter | X |
| Fire/cast | Left mouse | RT |
| Hold hand aim (stops on-foot motion) | Right mouse | LT |
| Next/previous selection | Wheel down/up or `]`/`[` | RB/LB |
| Weapon/power/item selector | F1/F2/F3, I for items | Y for items |
| Numbered selection in current category | 1–9, 0 | Rebindable buttons |
| Crouch | Ctrl | B |
| Centre look | R | Right-stick click |
| Fly up/down | Page Up/Page Down | D-pad up/down |
| Land / exit vehicle | F | D-pad right |
| Aircar gun narrow/widen/centre | Z/C/V | Rebindable buttons |
| Menu | Escape | Start |
| Automap | Tab or M | Back |
| Pause | P | Rebindable button |

Vehicle exit uses F/D-pad right. The adapter emits the original Tab exit edge;
it does not use Enter. Physical modern Tab remains the automap action.
Weapon, power and item selection accepts only foot combat contexts (including
held aim's underlying mode); vehicle input cannot open or confirm a selector.

Wheel and shoulder buttons cycle the last selected category, initially weapons.
Weapon/power cycles select directly; item cycles show the item selector rather
than consuming an item merely by cycling. Number keys use the original
selector shortcut ordering. Menus/shops accept arrows or left stick/D-pad,
Enter or controller A to accept, Escape/controller B/right click to back out; right stick moves the pointer
and RT clicks. A mouse or touchscreen tap retains the original pointer path.
Modal keyboard keys remain literal, including both Shift keys: save names,
shop B/S/Space and coder A–D/1–4 are not translated through gameplay actions.
In particular E and Space do not synthesize Enter while typing. Configurable
controller, mouse and touch modal actions remain active; keyboard action
bindings apply only during gameplay.

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
Held aim is transient: saves store the underlying combat mode and loads clear
the hold. Cycling an empty category while aim is held does not leave hand aim.

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
panel. Wall bytes map east/south/west/north, matching the original ray
crossings within the old tile at its +0xffff boundary. P uses the existing
pause menu rather than a separate pause screen.

## Android touch

Launch the exported `DataActivity` with the optional intent string extra
`controls=classic` to select Original keyboard semantics; it forwards the choice
to the private `GameActivity`. Without it Android uses modern controls.
The virtual left stick and six outlined
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
first-key swallowing and keyboard-only frame replays are unchanged.
Classic mouse input has separate signed DOS mickey/count fields: unlike modern
look, original `HandleMouseEvents` (`014334..014581`) uses vertical mickeys for
walking, horizontal mickeys for yaw, and right-button hold for forward movement.
Keyboard motion takes priority; classic absolute hand coordinates remain
640x240 DOS space at the driver boundary. See
[physical mouse oracle/config provenance](bootstrap.md#physical-mouse-and-original-configuration).

Dedicated physical jumping is limited to foot modes 1/2/5. In a vehicle the
modern Jump/A action is a secondary request: tank type 0 sets the canonical
`sim_state.tank_hand` (`PCTankHand`, global `206824`); release clears it.
Aircar type 1 instead restores flight throttle. These are the original
`014E6D..014ED3` / `014FF2` branches, not a vehicle-type alias or an imported
observer flag. Classic keyboard mode 7 continues to ignore Ctrl (`013E78`).
The pre-fix modern tank physically enters the foot jump table; the focused
negative control catches that behavior (`artifact://3428`).

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

The classic DOS mouse reducer also has focused tests in `test_controls_sim`:
signed-16 mickey bounds and yaw gain, direct vertical speed, keyboard priority,
button counts between polls, SDL right/middle distinction, original suppression
ordering, right-button diagonal/walk behavior, and selected-device versus
physical-driver gating. The old right-button mask fails the isolated behavioral
negative control (`artifact://3103`); the configured reducer and the existing
modern movement/look/aim/jump fixture pass in the integrated strict build
(`artifact://3297`). This unit result is not a full original mouse replay verdict.

Enhanced in-game selectors retain their modal keyboard path: Enter/controller A
confirms and Escape/right click cancels; mouse, RT and touch clicks use the
actual pointer location, including a complete tap without a preceding hover
frame. A transient semantic selector-pointer sample reuses the existing selector
geometry without enabling classic mouse/joystick routing. An idle pointer does
not undo keyboard navigation, and inputs acquired on foot cannot confirm a new
menu until released. The focused pre-fix regression demonstrates the missing
Enter confirmation (`artifact://3794`); the expanded fixture covers these
keyboard, pointer and context-ownership paths.
Modern taps hit the drawn 112×76 tile at (14+116×column,128+88×row);
gaps and space outside live tiles cannot confirm. Classic glove thresholds remain
unchanged. Rejected taps also discard their confirmation latch, so release cannot
confirm the prior selection. The modern regression taps all twenty tile centres.
Pointer motion alone is not a tap: A/Enter confirmation still accepts the
keyboard-selected entry when the moving right-stick pointer passes over a gap.

## Focused verification

```
tools/run_capped.sh make -C port -f tests/controls.mk B=build/Controls controls-check
```

The named fresh-archive gate at
`cda7722b9aba50afb57f7a62a43aad2659b2d1ea` includes the selector repair
`95ea447`. Its strict application build, all six controls targets, phase checks
and full check exit 0:
`captures/Gate-cda7722/receipt.json` and
`captures/Gate-cda7722/logs/controls-check.log`.
The real modern game smoke reports
`moved=1 looked=1 jumped=1 fired=1 mode=5`; the expanded simulation fixture,
including modal confirmation, pointer tiles and context release, passes.
The same named binary
`bc63d6da5886226a2b15dfaa4e88cc2eada474ab01fdc4845277389f60613a0e`
keeps the classic movement replay at **106/106 MATCH**, with the actor
comparison also exiting 0. This certifies that named cohort, not later edits.

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

### Recorded Android acceptance

`captures/Integrator-android/run.json` pins the modern-control emulator smoke
to source `c699ea4d97f735458793ae7794506c658b70ab4a` and the 3,696,689-byte APK
SHA-256 `64053b74fcd0f288eb00489423c7863c2696ee2da1afff333776c4463ca53b0f`.
It contains arm64-v8a/x86_64 native libraries and no bundled retail entries.
On `box-api36` (Android API 36, emulator 36.3.10.0), at 2400×1080:

- `title.png` records the title menu used for the logged tap-to-start.
- `scene-start.png` shows the left virtual stick and fire/jump/use/cycle/menu/map
  buttons in the side margins, outside the 4:3 game area.
- `scene-walk.png` shows actual forward world movement after the recorded
  virtual-stick swipe `(240,910) → (240,800)`, held 2200 ms.

These screenshots were inspected; the receipt records no process crash or
engine error. This is the pinned feature acceptance APK, not a claim that every
later engine/parity change has been rebuilt or exercised on Android.
