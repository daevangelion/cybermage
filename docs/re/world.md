# World / scene loading

Retail CM_HI.EXE v1.01F, LE link addresses. Native state is typed AoS, never an
emulated Xox allocation. Original offsets are retained in `world.h` comments.
All resources are borrowed spans owned by world RSC/platform files; close after
render/audio users release their references. No core allocations or OS calls.

## Entry points and formats

- LoadScene0x11524: scene sections0..7, fixed global definition sections96..100;
  LoadMAP0x27960, LoadFXO0x25CE4, LoadPROPS0x2449C, LoadPERSONS0x22148.
- `docs/re/formats.md` gives container and record layouts. These native loaders
  validate exact map size/dimensions, record count/stride, resource indices and
  frame spans, texture and object capacities.
- Global definitions: FX16 B (entry96), PROP1051 B (97), MONSTER276 B (98),
  GSX58 B (99), ANIMOBJ4532 B (100). Their array indices are original IDs.
- PERSON runtime record is192 B on DOS; disk placement is351 B. `type` is the
  original definition ID for AI. `monster_index` records the original compact
  loaded-type index for save serialization; `placement_index` is zero-based and
  not part of the original record. These metadata fields are not serialized.
- Geometry derives from definition and initial animation fields, not captures.
  ANIMOBJ+84/D4/124/174 arrays supply radius/height/extents, +16 sequence starts,
  +34 sequence frames and +264 image selector. GSX groups reference ANMH frames.
- Raw image payloads are not decoded or drawn. `world_texture` records their
  original half dimensions, resource/frame, geometry and packed/rotation flags
  for the renderer. Entries append; borrowed storage remains valid until close.
- AI owns runtime combat/dice initialization and22 transient missile/soul slots.
  `world_finalize_scene` follows it, reserving the original final3 view slots.
  Player motion/camera gameplay updates belong to Sim; events/switches to Events.

## Spatial maps

Original InitMap0x125D0 and MakeOMap0x1265C establish two maps, not independent
prop/FX allocations: OMap at Xox+CB0A0 (pointer+D7240), FLATMap at+D7244
(pointer+E33E4). Each has1024 tile heads plus11368 overflow nodes. Node0 owns
free-list head; node0 also has the original tile0 edge case.

OMapInsert/Delete0x584C0/581DC uses four corners in order +x+y,-x+y,-x-y,+x-y,
with radius capped at0x3800 (0x7400 if object flag8000). FXO uses one centre tile,
DBLFXO uses first two endpoints, FLAT uses centre in the separate flat pool.
Tombstoned heads keep their next links. Overflow exhaustion silently drops the
insert as original; malformed native chains fail with diagnostics.

Reference: `[rust: native/image1/lists.rs]`, original FXO/DBL/FLAT instructions
independently checked. Renderer owns sorted drawing lists, not this spatial pool.

## Math / sight

- MakeTables0x58F88 constants0x91D34..91D64; 2048 tangent/sine/cosine entries.
- FixedRotate0x596E4 uses signed64 product low32 after>>16, then wrapped add/sub.
- atan64k0x598E4 and Distance0x5F9A0 genuinely use original x87 math: native
  double atan2/sqrt reproduces observed integer outputs, not integer-to-float
  modernization of a fixed-point implementation.
- RANDOM0x68AD0/RND0x6731C retain signed16 intermediate wraps; zero-bound RND
  does not advance. Seed0x68ABF replaces only middle word with low BIOS tick;
  initial EXE words are3000,1,29999. Fresh scene startup supplies BIOS seed0.
- RaySight0x5D6D4, TileSight0x5D5A8, RayMissile0x5E5C0 use quadrant-specific
  grid crossings. Downward TileSight rechecks starting floor, not intervening
  floors. Sight allows flag40000000 without10000000; MissileSight0x5E530
  instead requires40000000 with00100000 to cross a nonzero wall.
- Original RayMissile takes start/end coordinates and returns crossing x/y/z
  plus distance, not hit-object ID. `world_ray_missile_xyz` exposes that contract;
  object-index wrapper is provided for existing AI callers. Object hit tests are
  a different collision operation, not silently folded into the ray.

## Exercised checks

`tools/run_capped.sh make -C port -f tests/world.mk B=build/world check`:
strict C99 compile succeeded; tombstone/free-list/duplicate/exhaustion vectors
passed; all retail scenes1..11 loaded. Placement object/person counts:

| Scene | Objects including sentinel | Persons | Panels |
|---:|---:|---:|---:|
|1|1088|152|77|
|2|1225|184|67|
|3|1763|196|48|
|4|1344|161|59|
|5|1668|197|69|
|6|2323|187|39|
|7|2248|134|42|
|8|1423|121|47|
|9|1067|115|71|
|10|475|45|30|
|11|1525|150|48|

`tools/world_original.py` loads unmodified retail LE objects into Unicorn and
calls the real x86 leaves. All6144 trig values and106 atan64k/Distance vectors
matched. It never uses the native reference hooks. Scene1 state comparison and
expanded ray vectors are separate checks; initial loader success is not parity.
