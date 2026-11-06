#!/usr/bin/env python3
"""Render the C port's HMI/PCM/TGV decoders; no independent decoding oracle.

Build: make -C port -f tests/audio.mk all
Examples:
  tools/cm_media.py song orig/retail/TESTSONG.HMI out/test.wav --banks orig/retail
  tools/cm_media.py pcm orig/retail/SPEECH.ENG out/speech.wav --offset N --length N
  tools/cm_media.py movie orig/retail/ECM.TGV out/ecm --frames 30
"""
import argparse
import array
import ctypes as C
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
import wave

ROOT = Path(__file__).resolve().parents[1]
U8 = C.c_uint8
U32 = C.c_uint32
UINT = C.c_uint
PTR = C.POINTER(U8)


class Error(C.Structure):
    _fields_ = [('file', C.c_char_p), ('offset', U32), ('message', C.c_char_p)]


class Movie(C.Structure):
    _fields_ = [('data', PTR), ('file', C.c_char_p), ('len', U32), ('pos', U32),
                ('width', UINT), ('height', UINT), ('current', UINT),
                ('audio_rate', UINT), ('audio_channels', UINT), ('audio', PTR),
                ('audio_len', U32), ('palette', U8 * 768),
                ('pixels', (U8 * (640 * 480)) * 2)]


def library(path=None):
    lib = C.CDLL(str(path or ROOT / 'port/build/audio/libcm_media.so'))
    specs = {
        'audio_init': ([UINT], C.c_int),
        'audio_banks': ([PTR, U32, PTR, U32], C.c_int),
        'audio_song': ([PTR, U32, C.c_char_p], C.c_int),
        'audio_song_done': ([], C.c_int),
        'audio_note_ons': ([], U32), 'audio_midi_events': ([], U32),
        'audio_mix': ([C.POINTER(C.c_int16), UINT], None),
        'audio_play_pcm': ([UINT, PTR, U32, UINT, UINT, C.c_int, C.c_int, C.c_int, UINT], C.c_int),
        'audio_speech_span': ([PTR, U32, U32, U32, C.POINTER(PTR)], C.c_int),
        'audio_speech_entry': ([PTR, U32, UINT, UINT, UINT, C.POINTER(PTR),
                                C.POINTER(U32), C.POINTER(C.c_int)], C.c_int),
        'cm_last_error': ([], C.POINTER(Error)),
        'cm_media_clear_error': ([], None),
        'tgv_open': ([C.POINTER(Movie), PTR, U32, C.c_char_p], C.c_int),
        'tgv_next': ([C.POINTER(Movie)], C.c_int),
    }
    for name, (args, ret) in specs.items():
        fn = getattr(lib, name); fn.argtypes = args; fn.restype = ret
    return lib


def checked(lib, result):
    if result < 0:
        e = lib.cm_last_error().contents
        raise ValueError(f'{e.file.decode() if e.file else "media"}:0x{e.offset:x}: {e.message.decode()}')
    return result


def span(path, entry=None):
    data = Path(path).read_bytes()
    if entry is not None:
        if len(data) < 8:
            raise ValueError('truncated RSC header')
        capacity, used = struct.unpack_from('<II', data)
        if not 0 <= entry < capacity or used > capacity or 8 + capacity * 8 > len(data):
            raise ValueError('invalid RSC entry/table')
        off = struct.unpack_from('<I', data, 8 + entry * 4)[0]
        length = struct.unpack_from('<I', data, 8 + capacity * 4 + entry * 4)[0]
        if off < 8 + capacity * 8 or off > len(data) or length > len(data) - off:
            raise ValueError('RSC entry outside file')
        data = data[off:off+length]
    if len(data) > 0xffffffff:
        raise ValueError('file exceeds 32-bit resource limit')
    return (U8 * len(data)).from_buffer_copy(data)


def stats(samples):
    a = array.array('h', samples)
    if sys.byteorder != 'little':
        a.byteswap()
    return {'samples': len(a), 'peak': max(map(abs, a), default=0),
            'rms': round(math.sqrt(sum(x*x for x in a) / max(len(a), 1)), 2),
            'clipped': sum(x in (-32768, 32767) for x in a),
            'sha256_pcm': hashlib.sha256(samples).hexdigest()}


def render_song(lib, path, output, banks, seconds=300, rate=48000, entry=None):
    lib.cm_media_clear_error()
    checked(lib, lib.audio_init(rate))
    m = span(Path(banks) / 'MELODIC.BNK'); d = span(Path(banks) / 'DRUM.BNK')
    checked(lib, lib.audio_banks(m, len(m), d, len(d)))
    data = span(path, entry); filename = str(path).encode()
    checked(lib, lib.audio_song(data, len(data), filename))
    block = (C.c_int16 * 2048)()
    digest = hashlib.sha256(); total = peak = square = clipped = 0
    with wave.open(str(output), 'wb') as wav:
        wav.setparams((2, 2, rate, 0, 'NONE', 'not compressed'))
        left = int(seconds * rate)
        while left and not lib.audio_song_done():
            n = min(1024, left); lib.audio_mix(block, n)
            raw = C.string_at(block, n * 4)
            if sys.byteorder != 'little':
                a = array.array('h', raw); a.byteswap(); raw = a.tobytes()
            wav.writeframesraw(raw); digest.update(raw)
            for x in block[:n * 2]:
                peak = max(peak, abs(x)); square += x*x
                clipped += x in (-32768, 32767)
            total += n * 2; left -= n
    e = lib.cm_last_error().contents
    if e.message:
        raise ValueError(f'HMI callback failed: {e.message.decode()}')
    result = {'file': str(path), 'entry': entry, 'seconds': round(total / (2 * rate), 3),
              'finished': bool(lib.audio_song_done()), 'note_ons': lib.audio_note_ons(),
              'midi_events': lib.audio_midi_events(), 'peak': peak,
              'rms': round(math.sqrt(square / max(total, 1)), 2),
              'clipped': clipped, 'sha256_pcm': digest.hexdigest()}
    if not result['note_ons'] or peak == 0:
        raise ValueError(f'silent song: {result}')
    return result


def render_pcm(lib, path, output, offset=0, length=None, rate=11025, channels=1, signed=False, entry=None):
    data = span(path, entry); length = len(data) - offset if length is None else length
    pointer = PTR()
    checked(lib, lib.audio_speech_span(data, len(data), offset, length, C.byref(pointer)))
    checked(lib, lib.audio_init(rate))
    checked(lib, lib.audio_play_pcm(0, pointer, length, rate, channels, signed, 32767, 0, 32768))
    result = bytearray(); block = (C.c_int16 * 2048)()
    left = length // channels
    while left:
        n = min(left, 1024); lib.audio_mix(block, n)
        raw = C.string_at(block, n * 4)
        if sys.byteorder != 'little':
            a = array.array('h', raw); a.byteswap(); raw = a.tobytes()
        result.extend(raw); left -= n
    with wave.open(str(output), 'wb') as wav:
        wav.setparams((2, 2, rate, 0, 'NONE', 'not compressed')); wav.writeframes(result)
    return {'file': str(path), 'offset': offset, 'length': length, **stats(result)}


def render_speech(lib, path, output, scene=1, message=1, segment=1):
    data = span(path); pointer = PTR(); length = U32(); link = C.c_int()
    checked(lib, lib.audio_speech_entry(data, len(data), scene, message, segment,
                                       C.byref(pointer), C.byref(length), C.byref(link)))
    offset = C.cast(pointer, C.c_void_p).value - C.addressof(data)
    result = render_pcm(lib, path, output, offset=offset, length=length.value)
    return {**result, 'scene': scene, 'message': message, 'segment': segment, 'link': link.value}


def dump_movie(lib, path, output, frames=30):
    from PIL import Image
    output = Path(output); output.mkdir(parents=True, exist_ok=True)
    data = span(path); movie = Movie(); filename = str(path).encode()
    checked(lib, lib.tgv_open(C.byref(movie), data, len(data), filename))
    count = 0; audio = bytearray(); hashes = []; audio_format = None; thumbnails = []
    while count < frames:
        kind = checked(lib, lib.tgv_next(C.byref(movie)))
        if kind == 0:
            break
        if kind == 2:
            fmt = movie.audio_rate, movie.audio_channels
            if audio_format is not None and audio_format != fmt:
                raise ValueError('movie changed audio format')
            audio_format = fmt
            # WAV PCM8 is unsigned; EACS PCM8 is signed.
            audio.extend(b ^ 128 for b in C.string_at(movie.audio, movie.audio_len))
        else:
            pixels = bytes(movie.pixels[movie.current][:movie.width * movie.height])
            image = Image.frombytes('P', (movie.width, movie.height), pixels)
            image.putpalette(bytes(movie.palette)); image.save(output / f'{count:04}.png')
            if count < 30:
                thumbnails.append(image.convert('RGB').resize((160, 120)))
            hashes.append(hashlib.sha256(pixels + bytes(movie.palette)).hexdigest())
            count += 1
    if audio_format:
        with wave.open(str(output / 'audio.wav'), 'wb') as wav:
            wav.setparams((audio_format[1], 1, audio_format[0], 0, 'NONE', 'not compressed'))
            wav.writeframes(audio)
    result = {'file': str(path), 'frames': count, 'width': movie.width, 'height': movie.height,
              'audio_format': audio_format, 'audio_bytes': len(audio), 'frame_sha256': hashes}
    if thumbnails:
        sheet = Image.new('RGB', (960, ((len(thumbnails) + 5) // 6) * 120))
        for i, image in enumerate(thumbnails):
            sheet.paste(image, ((i % 6) * 160, (i // 6) * 120))
        sheet.save(output / 'contact.png')
    (output / 'checksums.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', type=Path)
    sub = parser.add_subparsers(dest='command', required=True)
    song = sub.add_parser('song'); song.add_argument('--banks', type=Path, required=True)
    song.add_argument('--seconds', type=float, default=300)
    song.add_argument('--entry', type=int)
    pcm = sub.add_parser('pcm'); pcm.add_argument('--offset', type=lambda s: int(s, 0), default=0)
    pcm.add_argument('--length', type=lambda s: int(s, 0)); pcm.add_argument('--rate', type=int, default=11025)
    pcm.add_argument('--channels', type=int, default=1); pcm.add_argument('--signed', action='store_true')
    pcm.add_argument('--entry', type=int)
    speech = sub.add_parser('speech')
    speech.add_argument('--scene', type=int, default=1)
    speech.add_argument('--message', type=int, default=1)
    speech.add_argument('--segment', type=int, default=1)
    movie = sub.add_parser('movie'); movie.add_argument('--frames', type=int, default=30)
    for p in (song, pcm, speech, movie):
        p.add_argument('path', type=Path); p.add_argument('output', type=Path)
    args = vars(parser.parse_args()); lib = library(args.pop('library')); command = args.pop('command')
    args['output'].parent.mkdir(parents=True, exist_ok=True)
    result = {'song': render_song, 'pcm': render_pcm, 'speech': render_speech,
              'movie': dump_movie}[command](lib, **args)
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
