# Software renderer

CM_HI.EXE link addresses; names from Watcom globals in labels.csv. Reference
implementation consulted: [rust: crates/game/src/native/image1.rs] and its
image1/{project,raycast,levels,lists,objects,wall,props}.rs. Our C uses typed world
geometry and private scratch rather than the reference host's guest-memory ABI.

| Original | Address | C / evidence |
|---|---:|---|
| MakeRayAngles | 58E94 | column angles, symmetric pairs; atan and +0.5 then truncation; outermost pair gets an extra step (58F15) |
| MakeTables | 58F88 | world.trig; quarter-tangent sampled at half step; full-circle sin/cos |
| RenderInit | 59874 | view rect, width/2 focal; framebuffer stride remains 640 |
| CameraInit | 597EC | snap u/v to 256-unit centres: (position & ~255)+128 |
| CamVSCam | 5A1D0 | Euclidean distance, descending; equality inserts before (5A2E7) |
| TListInsert | 5B58C | descending unsigned key: steps<<24, abs(camera_level-level)<<16, ray order |
| CreateWall | 5B77C | generated slots start 0x94A, limit 0xB1A; half-length/height 0x8000 |
| CreateFlat / CreateTopFlat | 5BAB8 / 5BB88 | tile-centred, half-size0x8000; ceiling z=((level+1)<<16)-1 |
| CheckTile / CheckAbove / CheckBelow | 5C5C8 / 5C028 / 5C2FC | per-side/per-level masks, 18-crossing ray budget |
| RayCast | 5CBF8 | centre-out left/right ray order, asymmetric grid ties |
| FogDistance | 5F984 | clamp((distance>>14)+2,0,16) |
| ShadeDistance | 5F94C | clamp(((distance>>shift)-bias)*scale,0,63) |
| DrawWall / DrawMaskWall / DrawPackWall | 5FADC / 603AC / 60C70 | column-major half-dimension textures |
| DrawMaskProp / DrawPackProp / DrawPackInvis / DrawPackAura | 61960 / 61DC0 / 62368 / 627E8 | packed run headers i16; invis/aura checkerboard parity |
| RWCPlot / NewPoint / FindPoint | 62C28 / 62CB0 / 64B4C | 32-bit wrap projection; near cut1024, flat clipping at479 horizon-relative rows |
| DrawFlat / DrawMaskFlat / DrawTopFlat | 63760 / 63D9C / 64534 | 128x128 texture spans, wrap32 perspective coordinates |
| RenderObject / RenderScene / Render | 65A60 / 65B98 / 65D34 | cell countdown avoids repeated dynamic-object draw |
| LoadPANELS | 26F98 | u8 table count, u8 active count; i32 tex_h[],tex_w[],size[]; concatenated pixels (27215..27329) |
| LoadShadowPals / LoadTranPals / LoadFogPals | 1F910 / 1F9A0 / 1FA20 | HIPAK entries32/33/34, 64/10/21 maps of256 bytes (10C71..10C90) |

## Interfaces and ownership

`render_visibility(w,c,out)` builds the same ordered scene lists and projects
props without a framebuffer. `render_view(w,c,fb)` paints those lists. Neither
writes world state. AI owns centre-column target collection: original packed
prop drawers append only after x/y clipping succeeds, targetable byte7D028 is1,
and clipped x0+left<=320<=x0+right. Mask props never append.

Renderer state includes view rectangle, integral projection/trig-derived steps,
angle tables, temporary generated geometry, cell/object lists, colormap pointers,
and borrowed framebuffer. No per-frame allocation. The original pointer tables
become bounded arrays and borrowed spans, not emulated offsets.

`render_paint(w,fb)` consumes the exact prepared command list, without another
ray/projection pass; callers must keep the world unchanged between visibility
and paint. `render_draw` is the checked build-and-paint convenience entry.
Texture metadata belongs to world. Appended texture entries are decoded once
on demand; packed resources/panels are borrowed and plain rotated images use a
bounded16MiB pool. PANEL directories have no encoding tag and mix raw/packed
images; the selected drawer, not payload-length guessing, validates their bytes.
`LoadBackDrop1FB24` interleaves four consecutive256x128 HIPAK resources into
one1024x128 backdrop. No renderer code calls SDL or the OS.
Texture source revisions/aliases refresh existing entries as well as appended
ones; rotated replacements reuse their original decoded storage. Live fog and
revisioned active-shade overrides are read from world at frame preparation,
without renderer writes back into game state.

Default view:400x300 at(120,90), original ConWinInfo entry2 (95A68). Full framebuffer
is640x480 indexed8; presentation/DAC conversion belongs to platform/UI.

## Scene1 verification

Original-camera columns come from the zero-hook frame observer, not a camera
fitted to pixels. Newgame:(819328,1753216,166911), angle49152; turn:same position,
angle41984; pitch0, horizon150. `render_scene` loads the retail scene, initializes
actors, finalizes object maps, then substitutes only that explicit test camera.
It does not import oracle geometry, lists, light tables, framebuffer or pixels.

```
tools/run_capped.sh make -C port -f tests/render.mk B=build/render-test scene check
tools/run_capped.sh port/build/render-test/render_scene orig/retail captures/render-newgame 819328 1753216 166911 49152 0
tools/run_capped.sh port/build/render-test/render_scene orig/retail captures/render-turn 819328 1753216 166911 41984 0
.venv/bin/python tools/compare_frames.py captures/render-newgame/frame000000.ppm captures/newgame/frame000000.png --rect 120,90,400,300 --diff captures/render-newgame/diff.png
.venv/bin/python tools/compare_frames.py captures/render-turn/frame000000.ppm captures/turn/frame000001.png --rect 120,90,400,300 --diff captures/render-turn/diff.png
```

Observed whole-view mismatches: **newgame6315/120000; turn6114/120000**.
Every difference lies in the original blue-glove/weapon overlay, which this
renderer-only harness deliberately does not draw (UI owns it): newgame bounding
box absolute[203,310,322,390), turn[203,311,321,390). **Zero mismatches outside
those footprints.** Pixels hidden behind the original weapon are not verified
by this comparison. Compare tools correctly return status1 for the overlay
differences, not a falsely passing tolerance.

Focused tests cover all four DDA quadrants, integer angle/shade/fog boundaries,
whole-room wall coverage, floor/ceiling projection, world immutability, packed
versus masked target eligibility, opaque/transparent packed runs, invisibility
and aura parity, rotation, appended texture decoding, and malformed/truncated
resources. Malformed input fails closed rather than reproducing guest out-of-
bounds reads or infinite packed-run walks.

Other scenes, moving props, exact painter tie cases and extreme pitch have not
been oracle-compared. The near-zero claim is limited to the two named views;
synthetic primitive tests are not retail-parity evidence for those other cases.

## Enhanced GL backend

`render_gl.h` / `src/render_gl` are an opt-in presentation backend. SDL owns
context creation/procedure lookup/swap; ES300 and desktop330 use the same GLSL
bodies with a version prefix. World and shared decoded asset spans are read
only. This backend never supplies sight or target-query results.

Tile faces are batched by texture/encoding; changed map geometry rebuilds that
mesh. Dynamic geometry uses per-object visual keys and buffer-subrange updates.
Billboards expand along horizontal camera-right and world-up in the vertex
shader, so pitch does not tilt actors. Projection uses true pitch and fixed
vertical FOV (original4:3 focal/height ratio), expanding horizontally at wider
aspects. `render_gl_draw_view` supports scissored secondary views.

R8UI images and separate packed-run coverage retain opaque paletteindex0.
The indexed R8UI framebuffer is shaded through the original64/10/21 colormap
rows, then palette-resolved. Invisibility reads a copied indexed background;
aura selects the original checkerboard maps. UI capture records replace,
colormap composition and source-coordinate copy operations, preserving
opaque0/255 and sampled/shaded world pixels without GPU readback.

`tests/render_gl.mk` provides pure mesh/coverage/decoder tests and a real
offscreen scene executable. Its comparison uses the same loaded world and
camera for GL and SW, not imported oracle geometry or fitted pixels.
