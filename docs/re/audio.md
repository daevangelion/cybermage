# Sound, music, speech, movies

All executable addresses below are **CM_HI.EXE v1.01F LE link addresses**.
Reference guidance: [rust: crates/game/src/audio.rs], [rust: crates/game/src/music.rs],
[rust: crates/game/src/native/sound.rs], [rust: crates/game/src/tgv.rs]. Those files
are MIT prior art by lodilorenzo; this implementation uses bounded C parsers and
independent retail checks rather than importing their guest-memory/audio host.

## Original sound interface

| Function | Address | Contract |
|---|---|---|
| Sounds::BootSOS | 0x6654C | Reads seven numeric SOUNDSYS.CNF fields; selects digital/MIDI drivers; installs 40 Hz CM_ISR8 |
| CMsosSetPatchBanks | 0x66C00 | Passes melodic/percussion banks to SOS MIDI |
| CMsosPlaySample | 0x66C3C | EDX slot, EBX byte length, ECX data; stack channels,bits,volume,loops,pan; 16 slots |
| SampleStop / SampleDone | 0x66CE8 / 0x66D20 | Slot handles at 0x2072A0, IDs at 0x2072E0; done=1 when absent/finished |
| SetSampleVolume / SetSamplePanVol | 0x66D4C / 0x66D8C | Clamp signed volume to 0..32767; pan is 0..65535 |
| SampleMasterVolume | 0x66DE8 | Bare return: the game scales individual sample volumes itself |
| CMsosPlaySong / MidiMasterVolume | 0x66DEC / 0x66E3C | SOS init/start; music volume masked to seven bits, **not** /32767 |
| SongPause / Resume / Done / End | 0x66E4C / 0x66E5C / 0x66E6C / 0x66E84 | Single current game song |

Sample descriptor writes at 0x66C89 prove fixed rate **0x2B11 = 11025 Hz**.
Game effects/speech are unsigned PCM8, mono. The wrapper also describes stereo
PCM8; the core supports that and signed PCM8 for movies. `loops=0` means play
once; positive values are additional repeats, -1 repeats indefinitely. Pan has
full left gain at 0, full gain in both speakers near 32768, full right at 65535
([rust: native/sound.rs]; exact SOS pan table/rounding is not measured).

`Xox::SoundLoad` 0x28580 uses `soundadrs[]` 0x1E3D60 and the **RAW.RSC** file
handle at Xox+0xE352C. Its 256-entry parallel offset/length tables are at
Xox+0xE4B48 / Xox+0xE4F48. `Xox::PlaySONG` 0x28404 uses the same arrays at
+0xE4E48 / +0xE5248, i.e. song number 0 starts at RAW entry **192**. Retail has
38 HMI songs at entries 192..229; entry 198 equals TESTSONG.HMI. `InitSOS`
0x282D0 loads RAW entries **252/253** as melodic/drum banks (its indices60/61
are relative to the song subtable). Standalone MELODIC.BNK/DRUM.BNK have the
same format. Existing debug-global labels already name all these functions and
globals; no replacement labels are introduced.

## FM path and banks

**Use OPL FM because the shipped SB16/default configuration selects FM at port
0x388 and the game explicitly loads its shipped HMI AdLib banks.**

SOUNDSYS.CNF and SB16/SOUNDSYS.CNF are
`0xe016 0x220 1 7 0x388 1 8`; SC55/SOUNDSYS.CNF instead ends
`0x330 2 8`. The sixth field is the MIDI selector: the original jump table at
0x66530 maps 1 -> 0x666A2 -> driver **0xA009**, setting MIDIBankFlag 0x207320;
2 -> 0x666B7 -> **0xA001**, without banks. This establishes an actual FM path,
not a guessed substitute for an exclusively General MIDI game.

Both retail banks are 5404 bytes: version0.0, `ADLIB-` at +2, used/stored
counts128 at +8/+10, name-table offset28 at +12, patch-table offset1564 at +16.
Each 12-byte name row: u16 patch index, u8 percussion pitch, 9-byte name. The
pitch byte is **not an unused-patch flag**. Each 30-byte patch:

- +0/+1 unused; modulator's 13 fields at +2; carrier's at +15.
- Operator fields: KSL,multiplier,feedback/unused,attack,sustain,sustain-flag,
  decay,release,total-level,tremolo,vibrato,KSR,connection/unused.
- Feedback is patch+4; connection at +14 is 1=FM,0=additive (inverse OPL bit).
- Waveforms at +28/+29. Drum MIDI key selects bank row; row pitch controls
  oscillator note.

Format cross-reference (not copied implementation):
https://github.com/Wohlstand/OPL3BankEditor/blob/master/Specifications/HMI%20BNK%20Format.txt
Retail bank bytes were read and both banks pass the field/range decoder.

`port/src/audio/opl3.c/.h` are unmodified **Nuked OPL3 1.8**, revision
`765ec962e473aeb767e4cba74ffdc8f588ffbfe8`, LGPL-2.1-or-later, copyright
2013–2020 Nuke.YKT. Source: https://github.com/nukeykt/Nuked-OPL3 . Full licence
is beside them in COPYING.Nuked-OPL3. This separate module contains no allocation
or file I/O. Binary distributions must preserve LGPL notices and provide the
corresponding source/relinking arrangements; the rest of the port is not a
relicensing of this module.

## HMI songs

`CMsosPlaySong` 0x66DEC calls `sosMIDIInitSong` 0x772AD. The latter strcmp at
0x772B9 uses literal 0x91EB8, **HMI-MIDISONG061595**. This is the actual game
format; older **HMIMIDIP/HMP** is not used by TESTSONG or any of the38 RAW songs
and is deliberately rejected, as are unsupported branch/system events.

Song fields: u16 tick rate at +0xD4; u16 track count +0xE4; u32 relative track
table +0xE8. Each `HMI-MIDITRACK` has u32 relative event offset +0x57 and channel
at +0x7B (low4 bits). MIDI deltas/durations are big-endian seven-bit VLQs.
Channel messages use running status; note-ons append a VLQ duration. FF meta
lengths use VLQ; F0 SysEx lengths are u32 LE. Meta tempo does not replace the
song's fixed SOS timer rate.

Original `sosMIDIProcessSong` 0x77657: due note-offs precede tracks; old nonzero
track delta decrements without processing; when a delta was zero, process events
until a freshly read delta is nonzero and decrement that new delta once in the
same tick (0x7771F..0x77787). A duration N fires its off when the predecrement
counter is zero, not at the first decrement to zero. Track order is file order.
Last-track termination stops remaining notes. Port timing uses an integer sample
rate accumulator; no wall-clock scheduling, allocation or floats in our core.

Limits: 64 tracks, 512 pending duration offs, 4096 events/tick. The complete
track streams are checked before replacing a song. Unsupported/truncated data
fails through cm_fail(file,offset,message), never default bytes. Failed song/bank
loads preserve the previous song/banks. Device/control callers must serialize
against the mixer. Runtime budget/dependency overflow stops music and records
an error without calling cm_fail from the audio callback; the main thread
reports it via audio_song_error under the device lock. snd_tick and the host
renderer already do this. Regression fixtures exercise both runtime limits.

## SPEECH.ENG

Not a headerless PCM blob, and TEXT.MSG is **not** its audio index.
`OpenGLOBALFILES` 0x219B6 reads u32 directory byte size, then twelve u32 scene
start-message indices, then that many directory bytes into Xox+0xE5914.
Retail directory size7822; directory begins at file offset52.

Directory: six-byte message records `(u32 segment_table_offset,u16 segment_count)`;
the first segment-table offset/6 gives message count (retail242). Segment-table
offsets are relative to the directory, while each ten-byte segment is
`(u32 absolute_PCM_offset,u32 PCM_length,i16 link)`.
`PlaySpeechMessage` 0x1786C indexes `scene_start + message - 1`, then
`segment - 1`. Scenes are0..11; message/segment are1-based. Link is an original
object/video/script reference, not sound encoding. Original speech buffer maximum
is0x20000 bytes. First scene1/message1/segment1 is file offset7874, length110241,
link0, unsigned mono PCM8/11025Hz. `audio_speech_entry` returns a borrowed span
and the link; it does not execute gameplay linkage.

## EA TGV

Chunk header: four-byte tag, u32 LE total size including eight-byte header.
`kVGT`: u16 width/height at +0/+2, palette count at +6; 12-byte header,
RGB888 palette, then RefPack/LZ77 keyframe. Decoder checks declared output
size and every literal/backreference. `fVGT`: u16 motion/raw/packed counts and
index-bit-width at +0/+2/+4/+6; 12-byte header; signed10-bit xy vectors, rounded
to a32-bit boundary; raw16-byte4x4 blocks; packed blocks with four colours and
16 two-bit selectors in reversed pixel order; then per4x4-block indices.
Motion reads the preceding frame, never partly reconstructed current pixels.

`1SNh`: 32-byte EACS header followed by initial PCM; `1SNd`: subsequent PCM;
`1SNe`: audio end. EACS sample rate+4, bytes/sample+8, channels+9,
compression+10. Shipped tracks are signed PCM8, no compression. ECM/CMENDE are
22050Hz stereo; CREDENG is **22254Hz mono**, not stereo22050. All video is
320x240,15fps. The C decoder returns palette indices+RGB palette or a borrowed
audio span. Caller owns two640x480 maximum frame buffers; no dynamic codebooks.
Malformed motion/codebook indices reject rather than retaining old pixels.

## Commands and observed acceptance

```
tools/run_capped.sh make -C port -f tests/audio.mk check all

tools/run_capped.sh .venv/bin/python tools/cm_media.py song \
  orig/retail/TESTSONG.HMI out/audio/testsong.wav --banks orig/retail
tools/run_capped.sh .venv/bin/python tools/cm_media.py song \
  orig/retail/RAW.RSC out/audio/song192.wav --entry 192 --banks orig/retail
tools/run_capped.sh .venv/bin/python tools/cm_media.py speech \
  orig/retail/SPEECH.ENG out/audio/speech.wav
tools/run_capped.sh .venv/bin/python tools/cm_media.py pcm \
  orig/retail/TESTDIGI.RAW out/audio/testdigi.wav
tools/run_capped.sh .venv/bin/python tools/cm_media.py movie \
  orig/retail/ECM.TGV out/audio/ecm --frames 30
```

| Render | Duration | Note-ons / MIDI events | Peak / RMS (PCM16) | Clipping |
|---|---:|---:|---:|---:|
| TESTSONG.HMI |122.155s|1599 /1702|3244 /288.20|0|
| RAW song192 |229.717s|4908 /5912|17558 /1842.31|0|
| speech1/1/1 |110241 input samples|—|32768 /5484.86|2 full-negative output samples already present in source|
| TESTDIGI.RAW |18040 input samples|—|32512 /8384.59|0|

Listenable check was **objective waveform and event checks**, not a claim of
human listening: nonzero time-varying PCM, above RMS/peaks, zero music clipping,
and note-on counts equal an independent Python file-order MIDI-event walk.
Pinned PCM SHA256s live in port/tests/test_media.py. All38 in-game songs validate.

First30 ECM PNGs were actually opened as out/audio/ecm/contact.png: black screen,
then the centred blue **ORIGIN / WE CREATE WORLDS** mark fades in cleanly and
holds; no broken blocks, palette corruption, or displaced glyphs. Full ECM and
CMENDE decoded1816 and1685 frames. Their first30 RGB frames match FFmpeg's
independent TGV decoder **byte-for-byte**. CREDENG decoded525 frames and778896
PCM bytes; its first30 RGB frames also match FFmpeg byte-for-byte.
`make -C port -f tests/audio.mk check` completed with the C fixture checks and
all four retail tests passing (19.237s). An earlier test assumed stereo22050
for credits; inspecting its real EACS/ffprobe format corrected that assumption,
not the already general decoder.

## Deviations / boundaries

- Native PCM nearest-neighbour rate conversion, fixed integer stereo summation,
  hard clipping; no claim of exact Sound Blaster/SOS interpolation or pan rounding.
- Real OPL emulation and retail patch timbres, but the replacement MIDI driver
  uses18 two-operator voices, oldest-note stealing, linear operator attenuation,
  coarse OPL stereo pan and a two-semitone pitch-bend range. Exact HMI driver
  allocation/velocity curves are unmeasured; this is not an SC55 match.
- Polyphonic/channel pressure has no OPL modulation mapping. SysEx is parsed and
  skipped (no FM SysEx implementation). HMI branch/loop extensions reject;
  no shipped song needs them. Game-level song looping lives in snd_tick.
- platform_sdl owns the device and calls audio_mix(int16_t*,unsigned
  stereo_frames); audio_init gets its actual rate. Low-level audio_* calls
  require the caller's device lock; game-side snd_* controls lock internally.
  Core code calls no SDL or OS functions.
- All generated WAVs/PNGs and retail data stay ignored, never committed.

## Game-side API

`src/audio/game.c` owns RAW.RSC, SPEECH.ENG and TEXT.MSG through platform file
operations. Call snd_open(asset_dir) after platform_init; snd_scene(world) after
loading actors; snd_tick(world) once per game frame; snd_close before platform
shutdown. Core sample/music bytes remain borrowed from these owned files.
The public header exposes snd_play/snd_loop, snd_object/snd_clip,
snd_done/snd_abort, pan/volume controls, song/loop controls, dialogue and slot
snapshots. No emulated Xox struct or offset-indexed game state was added.

- PlayDIGI0x27CE0/LoopDIGI0x27E5C choose the **first free** of16 slots, no voice
  stealing. Scale requested volume by DIGI_MASTER_VOL/32767 before clamping;
  skip nonpositive effects. Loops use -1. Sample IDs survive slot completion,
  so CheckSound0x28680 and AbortSound0x286AC compare **slot and sample ID**.
- Speech uses ID999, clamps requested volume before SPK_MASTER_VOL scaling,
  and can play at zero master volume so dialogue still progresses.
- ClipPanVol0x280C0/PanVolDIGI0x27F2C use world player/object XY, original
  distance and angle helpers. With range R>0, reject distance>=R; volume is
  `((R-distance)>>4)*max/(R>>4)` (zero shifted denominator yields INT32_MAX,
  as the Watcom multiply/divide helper does). With no range, volume is
  `max + max/8 - (distance>>4)*max/32767`. Clamp against max, then min, then
  32767. Rear angles fold across +/-0x4000; double with **signed16 wrap**,
  subtract from0x8000 and clamp pan to0x2000..0xE000. The side-axis wrap is
  preserved, not replaced by a sine pan law.
- PanVolDistance0x17B80 uses volume `0x8FFE-(distance>>4)`, bounded
  0x0FFF..0x7FFF. Original full-volume exceptions: scene9/message21,
  scene3/messages6,7, scene8/messages7,15, and scene10; scene8/message15 also
  forces centre pan. These are original branches0x17C71..0x17CDC.
- Static soundDataLoad0x95C34 enables0..33 except4,9,11,12. snd_scene also
  enables monster-definition sample words+0x52/+0x54/+0x56 and five words
  +0xCC..+0xD4; class10 enables111/112. Evidence: LoadPERSONS0x226CC..0x22717,
  LoadMONSTER0x23349..0x23400. A later spawn can use snd_sample_load.
- snd_kill clears all voices and PERSON+0x4C/+0x50 handles; snd_kill_loops
  clears looping/speech/actor handles and optionally plays transition31 for
  playlist0,16 for playlist1 (KillLoops0x287B0).

Background music is **not a scene-number lookup**: two playlists at0x95BF4
continue across scenes, alternating when their -1 terminator is reached.
HandleNextFrame0x1BED3..0x1BF57 advances that list when CheckSONG reports done;
script events0x3ED0F..0x3ED65 replace the current song and looping flag.
snd_song(number,loop) implements the explicit script replacement. snd_tick
performs normal completion/playlist advance and repeats a scripted loop.
Initialization0x10F59 sets index0/current=-1; the port omits the invalid -1
resource request and reaches the same first valid playlist request18 on its
first tick. Muting MIDI stops the song without advancing while muted.

### Dialogue and movies

LoadTextMessage0x17750 selects spoken dialogue when enabled; otherwise it reads
TEXT.MSG's independent directory. Each text message has u16 line count, that
many u16 offsets, then NUL-terminated strings. Offsets are relative to this
string block. snd_text exposes borrowed lines, scene/message, speaker object,
link, active/spoken flag and revision. UI owns typing/timing/rendering and
completion-trigger application. snd_message sets the request; snd_tick starts
or retries the next speech segment when the previous slot finishes; moving
speakers update pan/volume each frame. snd_message_stop is KillSpeechText;
snd_speech_skip stops only the current segment. The speech-log override is
explicitly settable. Dead speakers cancel, including original scene9/person22
exception. Positive speech links select a PERSON object; negative links>-100
are exposed once via snd_take_video, matching original videoChain assignment.
Code links-100..-102 queue the four scene4 messages from the24-byte coder string
(A-D versus1-4, original branch0x178E5..0x17992); snd_code supplies that state.
snd_log_replay(world,index) maps the63 recorder entries through the original
speechLogData table0x95ED8 and enables narration override; the mapping is not
stored in SPEECH.ENG/TEXT.MSG. A normal snd_message clears that override.
These mappings, initial sample flags and music playlists are read from the
separately supplied CM_HI.EXE through the shared numeric LE table reader at
snd_open; no retail rule-table payload is bundled in the C sources.

snd_movie_begin/step/end in movie.c replace IntroEpilogue0x1F4EC. Original
selector **1=intro,2=credits,other=ending**, with English/French/German filenames.
Caller supplies absolute40 Hz ticks and skip input; step returns640x480 indexed
frames/palette without presenting or reading a host clock. Audio chunks enter
a64-span borrowed PCM queue consumed by audio_mix, with phase preserved across
chunk boundaries. The next audio chunk is prefetched before the next video
deadline, preventing40 Hz polling from underrunning15 Hz movie chunks. Stop,
skip and EOF drain/clear audio before releasing the movie file. All3 movies
have been stepped completely to EOF with headless48000 Hz mixing, then reopened
and skipped to test ownership and queue teardown. Headless callers must advance
audio_mix explicitly; the interactive SDL callback already does so.

```
tools/run_capped.sh make -C port -f tests/audio.mk check-game
```

This also runs tools/sound_oracle.py:32 requests use **original scene1 snapshot
coordinates**, four source objects, samples0/3/20/33, four listener headings
and ranged/non-ranged attenuation. Unicorn executes original PanVolDIGI
CM_HI.EXE:0x27F2C, including original x87 distance/atan/CRT code. Only SampleDone
(free-slot result) and final CMsosPlaySample0x66C3C device boundary are intercepted.
Sample,slot,pan,volume,loops match all32 native requests. These are isolated
helper-call comparisons in a real scene state, **not a claim that all32 requests
were naturally emitted during a gameplay recording**. JSON evidence is ignored
at out/audio/game-sound-oracle.json. C tests additionally cover slot exhaustion,
ID-protected abort, master volume, text/spoken triggering, PCM chunk boundaries,
and full movie completion/skip.
