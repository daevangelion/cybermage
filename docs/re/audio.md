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
against the mixer.

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
  no shipped song needs them. Game-level song looping is the caller's job.
- Device acquisition, movie clock/presentation, game event dispatch and loaded
  resource lifetime remain outside these pure decoders. platform_sdl calls
  audio_mix(int16_t*,unsigned stereo_frames); audio_init gets the actual device
  rate. Game/SDL caller must lock controls against its callback. These APIs do
  not add guest-machine addresses or SDL calls to core code.
- All generated WAVs/PNGs and retail data stay ignored, never committed.
