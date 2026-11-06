#!/usr/bin/env python3
"""Retail-dependent checks; skip clearly when the separately supplied data is absent."""
import ctypes as C
import hashlib
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import cm_media as media

DATA = ROOT / 'orig/retail'


def hmi_note_count(data):
    """Independent file-order event walk, not the C sequencer's timed walk."""
    def vlq(p):
        v = 0
        while True:
            b = data[p]; p += 1; v = v * 128 + (b & 127)
            if b < 128:
                return v, p
    notes = 0
    count = struct.unpack_from('<H', data, 0xe4)[0]
    table = struct.unpack_from('<I', data, 0xe8)[0]
    for i in range(count):
        start = struct.unpack_from('<I', data, table + i*4)[0]
        p = start + struct.unpack_from('<I', data, start + 0x57)[0]
        running = None
        while True:
            _, p = vlq(p)
            if data[p] >= 128:
                st = data[p]; p += 1
                if st < 240:
                    running = st
            else:
                st = running
            if st < 240:
                size = 1 if st & 0xf0 in (0xc0, 0xd0) else 2
                if st & 0xf0 == 0x90:
                    notes += data[p+1] != 0
                p += size
                if st & 0xf0 == 0x90:
                    _, p = vlq(p)
            elif st == 0xff:
                typ = data[p]; p += 1; n, p = vlq(p); p += n
                if typ == 0x2f:
                    break
            elif st == 0xf0:
                n = struct.unpack_from('<I', data, p)[0]; p += 4+n
            else:
                raise ValueError(f'unsupported system event {st:x}')
    return notes


@unittest.skipUnless((DATA / 'ECM.TGV').exists(), 'retail data supplied separately')
class RetailMedia(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = media.library(Path(sys.argv[1]) if len(sys.argv) > 1 else None)

    def test_hmi_render_and_independent_note_counts(self):
        for filename, entry, count, digest in [
            ('TESTSONG.HMI', None, 1599, '6a3640bfc132691e06397ea3fa1b90c057f8f05ebc09f366c523b2b46317fce8'),
            ('RAW.RSC', 192, 4908, '4c0827c5edbba88be45cb1cbdd4ef78efc105d51c41a0fe155f17618b3381338'),
        ]:
            with self.subTest(file=filename), tempfile.TemporaryDirectory() as tmp:
                song = media.span(DATA / filename, entry)
                self.assertEqual(hmi_note_count(bytes(song)), count)
                result = media.render_song(self.lib, DATA / filename, Path(tmp) / 'song.wav', DATA, entry=entry)
                self.assertTrue(result['finished']); self.assertEqual(result['note_ons'], count)
                self.assertGreater(result['rms'], 200); self.assertEqual(result['clipped'], 0)
                self.assertEqual(result['sha256_pcm'], digest)

    def test_all_38_game_songs_validate(self):
        lib = self.lib; lib.audio_init(48000)
        m = media.span(DATA / 'MELODIC.BNK'); d = media.span(DATA / 'DRUM.BNK')
        media.checked(lib, lib.audio_banks(m, len(m), d, len(d)))
        for entry in range(192, 230):
            with self.subTest(entry=entry):
                song = media.span(DATA / 'RAW.RSC', entry)
                media.checked(lib, lib.audio_song(song, len(song), b'RAW.RSC'))
                self.assertGreater(hmi_note_count(bytes(song)), 0)
                lib.audio_song_stop()

    def test_speech_and_sample_checksums(self):
        with tempfile.TemporaryDirectory() as tmp:
            speech = media.render_speech(self.lib, DATA / 'SPEECH.ENG', Path(tmp) / 'speech.wav')
            self.assertEqual(speech['offset'], 7874); self.assertEqual(speech['length'], 110241)
            self.assertEqual(speech['sha256_pcm'], 'dd726841fc960512d9aac0a84042fc4046c1abc74682942c407e9071607b093a')
            sample = media.render_pcm(self.lib, DATA / 'TESTDIGI.RAW', Path(tmp) / 'test.wav')
            self.assertEqual(sample['sha256_pcm'], '9d583dfa8e1e6c9d19fb480374e4248c568579735acec45b7748117eaa44b9fd')

    def test_complete_movies_and_ffmpeg_first_30(self):
        lib = self.lib
        for filename in ('ECM.TGV', 'CMENDE.TGV', 'CREDENG.TGV'):
            with self.subTest(file=filename):
                data = media.span(DATA / filename); movie = media.Movie(); rgb_hashes = []
                media.checked(lib, lib.tgv_open(C.byref(movie), data, len(data), filename.encode()))
                frames = audio_bytes = 0
                while True:
                    kind = media.checked(lib, lib.tgv_next(C.byref(movie)))
                    if kind == 0:
                        break
                    if kind == 2:
                        expected_audio = (22254, 1) if filename == 'CREDENG.TGV' else (22050, 2)
                        self.assertEqual((movie.audio_rate, movie.audio_channels), expected_audio)
                        audio_bytes += movie.audio_len
                    else:
                        if frames < 30:
                            pal = bytes(movie.palette)
                            rgb = b''.join(pal[p*3:p*3+3] for p in movie.pixels[movie.current][:movie.width*movie.height])
                            rgb_hashes.append(hashlib.md5(rgb).hexdigest())
                        frames += 1
                self.assertGreater(frames, 30); self.assertGreater(audio_bytes, 0)
                print(f'{filename}: {frames} frames, {audio_bytes} audio bytes')
                if shutil.which('ffmpeg'):
                    output = subprocess.check_output([
                        'ffmpeg', '-v', 'error', '-threads', '1', '-i', str(DATA / filename),
                        '-frames:v', '30', '-an', '-pix_fmt', 'rgb24', '-f', 'framemd5', '-'], text=True)
                    oracle = [line.rsplit(',', 1)[1].strip() for line in output.splitlines() if line and not line.startswith('#')]
                    self.assertEqual(rgb_hashes, oracle)
                else:
                    self.skipTest('ffmpeg not installed: independent movie comparison unavailable')


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0]], verbosity=2)
