# Resource formats

Addresses are **CM_HI.EXE v1.01F LE link addresses**, not file offsets.
Little-endian unless stated otherwise. `[rust: ...]` denotes prior art, not an
independent oracle. Format facts below were checked against the retail binary and
installed data; no extracted game bytes are checked in.

## RSC directories

`Xox::OpenGLOBALFILES` 0x21328 reads `u32 capacity, u32 count`, then **both**
`u32 offset[capacity]` and `u32 size[capacity]`. Payloads are uncompressed at the
container layer. Entries need not be ordered or contiguous. `count` is not a safe
upper bound for indices: RAW bank entries 252/253 exist although count is 117.
Unused slots have zero offset/size. Offset+size must fit the file and offsets must
not point into the directory. Do not infer size from the next offset.

| Retail file | Capacity | Count | Directory bytes | Loader evidence |
|---|---:|---:|---:|---|
| FXO.RSC | 384 | 313 | 3080 | 0x21431–0x214AD, offset/size arrays Xox+E3548/E3B48 |
| PRP.RSC | 256 | 179 | 2056 | 0x21522–0x2159E, E4348/E4748 |
| RAW.RSC | 256 | 117 | 2056 | 0x21613–0x21693, E4B48/E4F48 |
| HIPAK.RSC | 64 | 55 | 520 | 0x2178F–0x21805, E5348/E5448 |
| ANMH.RSC | 64 | 36 | 520 | LoadPERSONS 0x221EC–0x22270, E4148/E4248 |
| SCENE.RSC | 12 scenes × 9 sections | 12 scenes | 872 | LoadScene 0x11DF8–0x11E64; see below |

Demo also supplies DEMOANMH/DEMOANML (64/31), DEMOSCEN (12/12), FXO (384/286),
PRP (256/154), RAW (256/108), HIPAK/LOPAK (64/41). The demo scene directory is
sparse: only scene 11 is present. Capacity/count refer to directory metadata, not
an assertion that all those slots contain data.

`tools/rsc.py list FILE` validates every slot and prints absolute offsets and
explicit sizes. `extract FILE INDEX OUTPUT` writes one payload, rejecting absent
slots and out-of-range indices. SCENE.RSC and DEMOSCEN.RSC names select the
nine-section layout; renamed scene containers must retain one of these basenames.
The C `rsc_open/rsc_entry` use the same rules and borrow entry bytes until close;
`rsc_parse` also accepts caller-owned bytes. `rsc.capacity` is the flattened slot
count (108 for scenes), `rsc.count` retains the original header count.
All14 supplied retail/demo RSC files were listed successfully; extracting the
first populated entry of each matched its checked source span byte-for-byte.

Numeric tables in CM_HI.EXE are read by `cm_exe_init/cm_exe_table` (`exe.h`),
not generated into C arrays. The reader borrows the caller's file bytes, selects
the LE data object at0x90000, validates page dimensions/mappings and decodes
little-endian signed words, including words crossing physical page boundaries.
`cm_exe_bytes` exposes the same checked mapping for packed u8/u16 tables.
Relocated pointers and executable code are not part of this API.

## HIPAK, palettes, fonts and title

| Entry | Structure / consumer |
|---:|---|
| 0 | Exactly 307200 row-major palette indices, 640×480 title. LoadTitleScreen 0x1FAD0 and TitleMenu 0x54C7F–0x54CAC. |
| 1–6, 50 | 76800-byte backgrounds (320×240); LoadBackScreen 0x1FBF0 supplies the size-dependent path. |
| 7–18 | 32768 bytes each; LoadBackDrop 0x1FB24. Exact texel geometry remains open. |
| 19–24 | Variable-size console graphics; LoadConsoleGFX 0x1FC9C. Internal fields remain open. |
| 25,26,28,27,29,30,36 | Fonts 0..6 respectively; Init 0x10D6B–0x10E2D. |
| 31 | 768 bytes: 256 RGB triples, each component 0..63 (VGA DAC). RGB8 conversion is `component << 2`. LoadPalette 0x1FAA0. |
| 32 | 64×256 palette-index translation bytes, shadow levels; LoadShadowPals 0x1F910. |
| 33 | 10×256 translation bytes, bright levels; LoadTranPals 0x1F9A0. |
| 34 | 5376 bytes, fog tables; LoadFogPals 0x1FA20. |
| 35 | Pointer graphics; exact internal layout remains open. LoadPointer 0x1FBA4. |
| 37–49,52–54 | Raw 640×480 indexed screens. 53 is options background. [rust: REVERSE.md “Data Formats”, crates/game/src/rebrand.rs]; sizes independently checked. Individual screen purposes beyond these remain open. |
| 51 | 28401-byte payload; purpose/structure open. |

### Font file header (14 bytes)

`FONT_LOAD` 0x66F00 copies these bytes to a 32-byte descriptor at
`_font_table` 0x20732F + font_id×32. Descriptor +0 is the allocated bitmap,
+18..31 the disk header. Disk fields:

| Offset | Type | Meaning |
|---:|---|---|
| 0 | u16 | maximum width (observed in all retail font entries) |
| 2 | u16 | height; descriptor +20, TEXT_STR 0x6763B |
| 4 | u16 | monochrome/color flag; descriptor +22, branch at 0x67711 |
| 6 | u16 | glyph count; observed 128 for fonts 0–4/6, 10 for numeric font 5 |
| 8 | s16 | extra horizontal spacing; descriptor +26, TEXT_LEN 0x675A8 |
| 10 | u16 | unknown (zero in these retail entries) |
| 12 | u16 | glyph stride; descriptor +30, TEXT_LEN 0x675B4 |

Glyph `ch` is at `14 + ch*stride`: `u16 width`, then `width*height` row-major
bytes; the remaining stride is padding. The title font (entry26) has max width24,
height32, color flag1, count128, spacing0, stride770. `TEXT_STR/TEXT_CH`
0x675D4/0x67665 handle several drawing modes: FF background, FE shadow;
color-font visible pixels keep their stored palette index. The port's raw blit
skips FE/FF; title text uses original mode0, FF transparent and FE→index1.
It does not yet implement every monochrome/remap TEXT_* mode.
`gfx_text_width(font,text,&width)` returns status separately from its signed
width (negative spacing can legally produce width−1). Character count and
cumulative pixel advance are bounded to20480; accumulation uses int64_t before
narrowing. This is a hostile-input draw bound, not a replacement font metric.

### Title composition

`Xox::TitleMenu` 0x54C20, drawing 0x555BA–0x55644; constants
0x95CB0–0x95D3C. Six inclusive boxes: x=32..210,230..408,428..606;
y=366..398 and412..444. Selected starts at NEW, translated using bright table2;
others use shadow table22. Text uses font1, centered at x=122,320,518,
y=370/416, preserving baked glyph colors. STRINGS.MSG strings140..145:
NEW, LOAD, OPTIONS, TRAINING, CREDITS, QUIT. No replacement system font or
rebranded art is used.

`tools/run_port.sh title` builds and emits `out/port/title/frame000000.ppm`,
`.idx` (307200 row-major indices), `.pal` (768 RGB8 bytes). PPM is P6 RGB8.
`--headless --frames N --dump DIR --keys FILE` uses 40 Hz frame numbers;
input lines are `FRAME down|up HEX_DOS_SCAN`, sorted, with `#` comments.
Raw ordered IRQ9 bytes (including E0, break bytes and repeated makes) are retained
in `cm_input.raw_scans`; convenience held/pressed/released arrays are also exposed.
`FRAME raw HEX_BYTE` injects an exact byte; down/up accept normal scan01..7F or
extended E001..E07F. Bad numeric fields, NULs, out-of-order input and overflowing
per-frame queues fail explicitly. `platform_ticks(frame)` is frame in headless
mode and elapsed SDL time×40/1000 otherwise. SDL RWops also provides save writes
and `.state` text dumps without adding OS calls to game code.
`platform_user_dir()` is a separately allocated SDL preference directory,
created once during platform initialization and released on shutdown; asset
directories are not implicitly writable save targets. `platform_directory`
creates explicit parent directories, while `platform_file_exists` distinguishes
a regular file, ENOENT and other I/O failures for optional user files.

Title-only snapshot c8affad, independently reviewed by UI, produced **0/307200
mismatched RGB pixels** against Bootstrap's unhooked-original-x86 capture:
`tools/compare_frames.py out/port/title/frame000000.ppm captures/title/frame000000.png
--diff out/diff/title.png`. These commands and files are local verification
artifacts; no screenshots derived from retail assets are committed. Later game/menu
integration belongs to the UI/Sim slices; this comparison establishes the title
composition, not gameplay parity.

## SCENE.RSC / DEMOSCEN.RSC

Header: u32 scene capacity (12), u32 scene count; then **108 offsets and 108
sizes**, each section-major (`section*12+scene`). LoadScene 0x11E3D/0x11E53
reads 432 bytes twice. `rsc_entry` uses these flattened indices. Section order:

| Section | Loader | Initial structure / record stride |
|---:|---|---|
| 0 | LoadMAP 0x27960 | u16 width32,height32,layers4; see below |
| 1 | LoadPROPS 0x2449C | u32 count at start; 30-byte dummy/header record, then count×30 bytes; seek+30 at0x2452F and read30 at0x2456A |
| 2 | LoadFXO 0x25CE4 | same count/header30/records30; 0x25D59/0x25D92 |
| 3 | LoadPERSONS 0x22294 | u32 count; 351-byte header/dummy plus count×351; 0x222EC/0x22332. Person active/type s16 at+0x159 tested at0x22346; other fields not fully mapped. |
| 4 | LoadEVENTS 0x2732C | first256 bytes index/flags, then count at+256 within a72-byte dummy/header record, followed by events; 0x27381/0x273A0. Field semantics open. |
| 5 | LoadSWITCHES 0x274F8 | count u32,16-byte dummy/header, count×16 records; original limit count<128 |
| 6 | LoadPASS 0x27680 | count u32,18-byte dummy/header, count×18 records; original limit count<96 |
| 7 | LoadLIGHTS 0x277C4 | count u32, then count×4 records; original limit count<32 |
| 8 | global databases, not scene-local records | slots0–5 used; slot2 monster definitions276 bytes each (LoadMONSTER0x231EF), slot3 GSX definitions58 bytes each (LoadGSX0x237ED), slot4 animation-object definitions4532 bytes each (LoadANIMOBJ0x23DF5). Exact other subtable definitions open. |

Map section is exactly54790 bytes for every retail scene: u16 dimensions at0;
512 bytes into Xox+5E5A4 at+6;1024 bytes into Xox+5DFA4 at+518;
4096×u32 map tiles at+1542 (Xox+BE074); then4096 interleaved five-byte
attributes at+17926, scattered to Xox+C408C/C3088/C5090/C6094/CB09C;
then two8192-byte arrays at+38406/+46598 into Xox+C6098/C8098.
Evidence0x27980–0x27BD7. No decompressor is called. Semantic names of the five
attributes and two tail arrays remain open; do not collapse them into guessed tiles.

Prop placement records copy +0/+4/+8 dwords into world coordinate arrays
Xox+61418/64080/66CE8 (0x245D3–0x24601), +0x14 s16 selects prop/resource,
+0x1A s16 auxiliary (0x24590/0x245AA); other fields open. FX placements copy
+0/+4/+8 similarly; s16 +0x18 selects FX resource (0x25DB4). Presence predicates
are original signed words at prop+0x14 and FX+0x18; zero/nonpositive entries are
skipped. These are disk records, not the larger runtime objects.

## PANELnn.DAT

`LoadPANELS`0x26F98, header read0x270C7–0x270F7: u8 capacity,u8 count,
then three capacity×u32 tables. First two populate Xox+A7A3C and+A623C;
third contains byte lengths for payloads (0x271CC–0x27278). Only count entries
are instantiated. Payloads follow in sequence, read verbatim (0x272C0–0x272F0).
Geometry/storage flags in the first two tables and panel texel orientation remain
open; there is no container decompressor on this path. French/German variants
PANEL02/05/09.FDT/GDT are selected by language in0x27149 onward.

## Sprite packages and FX bitmaps

PRP entries use `u8 frame_count,u8 rotations`, then120 interleaved
`u32 half_width,u32 half_height` pairs, then120 u32 frame offsets (`BM`).
The fixed header is1442 bytes (2+960+480). LoadPROPDATA0x250F0–0x251E7
reads exactly these fields, retaining only frame_count dimensions. Frame offsets
are entry-relative; the first is1442. A frame normally spans to the next frame's
offset, with the last ending at the RSC entry length.

ANMH entries use the same header: LoadANIMDATA0x24007 reads frame_count and
requires≤120;0x2414A requires rotations=8;0x24191–0x2420B reads dimensions
and BM offsets. Frame-selection logic may share or skip directional frames.
These are bitmap packages; the separate animation-object/monster definitions
reside in SCENE's final group, not in the ANMH container directory.

Variable frame lengths prove an encoded sprite representation on the compressed
object path: ANMH entry1 first frame is18249 bytes for half-dimensions78×110,
not34320 raw pixels. PRP entry1 first frame is3284 bytes at17×92 rather than6256.
The exact column/span encoding remains to the renderer slice; no guessed
decompressor is shipped here. The alternate raw prop path0x25293 reads
`4*half_width*half_height` bytes and calls RotateObjL90 except original resource
IDs7,13,14,47,48. This is a loader-selected representation, not RSC compression.

FXO bitmap entries start with `s16 half_width,s16 half_height`; LoadFXODATA
0x26995–0x269E7. The usual path reads `(2*w)*(2*h)` raw palette indices
(0x26A22–0x26A48), then RotateObjL90. Examples independently observed:
entry2 is80×80 half-dimensions and25604 bytes, entry3 is80×10 and3204 bytes.
Resource12 takes a special path0x26A7E whose representation is not mapped here.

### Sprite world extents

`LoadPROPOBJ`0x24D66 reads a1051-byte definition from SCENE group8 slot1.
Definition+0x14 nonzero sets object flag0x80000 (0x24E17), selecting the
packed-object branch in LoadPROPDATA0x25212. Definition+0x1E is a u16 size
percentage (zero becomes100). Scale is `(percent<<9)/100`, integer division.
At0x24FBA–0x25012, per loaded frame:

- Xox+A923C = half_width×scale; Xox+AAA3C = half_height×scale.
- Xox+AC23C = s32(definition+0x43+8×loadFlag[frame])×scale.
- Xox+ADA3C = s32(definition+0x47+8×loadFlag[frame])×scale.

`LoadGSX`0x23CA6–0x23D6D derives actor scale from animation-object runtime
u16+0xC; hires mode first replaces percent by `(2*percent)/3`, then applies
the same512/100 factor. Half-width/height extents use that factor; offsets copy
runtime animation-object arrays+0x270/+0x450. `LoadANIMOBJ`0x23F07–0x23F55
initializes those arrays from4532-byte definition fields+0x2B4/+0xA34,
multiplied by its `(percent<<9)/100` (zero percent→100). Frame sharing/size
rescale exceptions remain documented by the renderer/world slices.

## Messages, names, audio and auxiliary files

| File family | Known / open |
|---|---|
| STRINGS.MSG (FRN/GER variants) | Sequential NUL-terminated byte strings, no header. LoadStrings0x1FE00–0x201CF builds pointer groups; title group140..145. Encoding outside ASCII not yet established. |
| CMNAME.MSG, ITNAME.MSG | Name lists loaded by LoadGameNames0x201D0; exact splitting/count tables remain open. |
| TEXT.MSG / DEMOTEXT.MSG | Not an RSC: OpenGLOBALFILES0x218A5 reads u32 blob length,12×u32 scene indices, then the blob. Retail blob length1936. Message/speech record interpretation belongs to Audio slice (docs/re/audio.md). |
| SPEECH.ENG / DEMOSPK.ENG | OpenGLOBALFILES0x219B6 uses analogous u32 size/12 scene offsets/header52; PCM message spans decoded by Audio slice. See docs/re/audio.md rather than treating the whole file as one WAV. |
| RAW.RSC | RSC container, sample/music and patch-bank entries. Original InitSOS/PlaySONG add192 to song/bank selectors; banks252/253. Internal audio formats: docs/re/audio.md. |
| ANMH/ANML.RSC | RSC container of sprite packages, described above. Game definition records are separate in SCENE.RSC. |
| FXO.RSC, PRP.RSC | RSC containers of FX bitmaps/prop sprite packages, described above. Graphics consumed by LoadFIXOBJ0x26610/LoadFXODATA0x26910 and LoadPROPOBJ0x24D38/LoadPROPDATA0x25078. |
| *.TGV (ECM,CMEND*,CRED*) | EA movie containers; Audio slice docs/re/audio.md covers chunks/video/audio. |
| DRUM.BNK, MELODIC.BNK, TESTSONG.HMI, TESTDIGI.RAW | HMI patch banks/sequence/PCM diagnostics. See Audio slice docs. |
| SAVE010.DAT / SAVE*.DAT | Original state serialization. SAVE010 is supplied retail data, not a port-created save. Full ABI/field mapping belongs to UI; this slice does not claim a save codec. |
| GAMENAME.USR | Save-name storage (288 bytes supplied); parsing/slot fields open. |
| CMLANG.CNF | Language selection, read by LoadSetup0x1EFE8; exact text grammar open. |
| CMDATA.CNF, CMPATH.CNF | Asset/config paths (not game resources); ReadConfigFile0x204EC/LoadSetup. Exact text grammar open. |
| SOUNDSYS.CNF (including SB16/SC55 copies) | HMI device configuration; Audio slice maps it. |
| CM_HI/CM_LO.EXE | Watcom DOS/4GW LE program images, not asset archives. See architecture_exe.md. |
| PLAYTGV.EXE, SETUP.EXE, DINSTALL.EXE | Separate DOS executables; player/configurator/demo installer, not resource containers. |
| DOS4GW.EXE, HMIDET/HMIDRV/HMIMDRV.386, UNIVBE.EXE | Shipped runtimes/drivers; label, do not port. |
| CM.BAT, run.bat | Host launch scripts, not game resource formats. |

No unverified decompressor or guessed field layout is exposed as a working parser.
The checked title path only needs HIPAK, STRINGS, palette/font/translation data.
