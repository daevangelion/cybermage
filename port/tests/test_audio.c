#include "audio.h"
#include "tgv.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static cm_error error;
int cm_fail(const char *file, uint32_t offset, const char *message)
{
    error.file = file; error.offset = offset; error.message = message;
    return -1;
}
const cm_error *cm_last_error(void) { return &error; }

static void put32(uint8_t *p, uint32_t n)
{
    unsigned i;
    for (i = 0; i < 4; ++i) p[i] = (uint8_t)(n >> (8 * i));
}

static void samples(void)
{
    static const uint8_t pcm[] = {255, 0, 128};
    const uint8_t *pointer;
    int16_t out[16];
    assert(audio_init(11025) == 0);
    assert(audio_play_sample(0, pcm, 3, 1, 8, 32767, 0, 0) == 0);
    audio_mix(out, 4);
    assert(out[0] == 32512 && out[1] == 0);
    assert(out[2] == -32768 && out[3] == 0);
    assert(out[4] == 0 && out[6] == 0 && audio_sample_done(0));
    assert(audio_play_sample(0, pcm, 1, 1, 8, 32767, 1, 65535) == 0);
    audio_mix(out, 3);
    assert(out[0] == 0 && out[1] == 32512 && out[3] == 32512);
    assert(out[5] == 0 && audio_sample_done(0));
    assert(audio_play_sample(16, pcm, 3, 1, 8, 1, 0, 0) == -1);
    assert(audio_play_sample(0, pcm, 3, 1, 16, 1, 0, 0) == -1);
    assert(audio_play_sample(0, pcm, 3, 1, 8, 1, -2, 0) == -1);
    assert(audio_speech_span(pcm, 3, 2, 2, &pointer) == -1);
    assert(audio_init(48000) == 0);
}

static void song(void)
{
    uint8_t hmi[512] = {0};
    uint8_t bank[5404] = {0};
    int16_t out[2048];
    unsigned i;
    int loud = 0;
    memcpy(bank + 2, "ADLIB-", 6); bank[8] = 128; bank[10] = 128;
    put32(bank + 12, 28); put32(bank + 16, 1564);
    for (i = 0; i < 128; ++i) {
        bank[28 + i * 12] = (uint8_t)i;
        bank[28 + i * 12 + 2] = 60;
        bank[1564 + i * 30 + 3] = 1;
        bank[1564 + i * 30 + 5] = 15;
        bank[1564 + i * 30 + 9] = 8;
        bank[1564 + i * 30 + 10] = 24;
        bank[1564 + i * 30 + 14] = 1;
        bank[1564 + i * 30 + 16] = 1;
        bank[1564 + i * 30 + 18] = 15;
        bank[1564 + i * 30 + 22] = 8;
    }
    assert(audio_banks(bank, sizeof bank, bank, sizeof bank) == 0);
    memcpy(hmi, "HMI-MIDISONG061595", 18);
    hmi[0xd4] = 120; hmi[0xe4] = 1; put32(hmi + 0xe8, 0x100);
    put32(hmi + 0x100, 0x104);
    memcpy(hmi + 0x104, "HMI-MIDITRACK", 13);
    put32(hmi + 0x104 + 0x57, 0x80);
    memcpy(hmi + 0x184, "\0\x90\x3c\x64\x10\x20\xff\x2f\0", 9);
    assert(audio_song(hmi, 0x18d, "fixture.hmi") == 0);
    audio_mix(out, 1024);
    for (i = 0; i < 2048; ++i) if (out[i] != 0) loud = 1;
    assert(loud && audio_note_ons() == 1 && audio_midi_events() == 1);
    audio_song_stop();
    assert(audio_song_done());
    hmi[0x18a] = 0x55;
    assert(audio_song(hmi, 0x18d, "bad.hmi") == -1);
    assert(audio_song(hmi, 200, "short.hmi") == -1);
    bank[1564 + 3] = 255;
    assert(audio_banks(bank, sizeof bank, bank, sizeof bank) == -1);
}

static void speech(void)
{
    uint8_t data[71] = {0};
    const uint8_t *pcm;
    uint32_t len;
    int link;
    unsigned i;
    put32(data, 16);
    for (i = 2; i < 12; ++i) put32(data + 4 + i * 4, 1);
    put32(data + 52, 6); data[56] = 1;
    put32(data + 58, 68); put32(data + 62, 3);
    data[66] = 0xfe; data[67] = 0xff;
    data[68] = 128; data[69] = 255; data[70] = 0;
    assert(audio_speech_entry(data, sizeof data, 1, 1, 1, &pcm, &len, &link) == 0);
    assert(pcm == data + 68 && len == 3 && link == -2);
    assert(audio_speech_entry(data, sizeof data, 0, 1, 1, &pcm, &len, &link) == -1);
    assert(audio_speech_entry(data, sizeof data, 1, 1, 2, &pcm, &len, &link) == -1);
    put32(data + 62, 4);
    assert(audio_speech_entry(data, sizeof data, 1, 1, 1, &pcm, &len, &link) == -1);
}

static void movie(void)
{
    static tgv m;
    uint8_t data[80] = {0};
    unsigned i, p = 8;
    memcpy(data, "kVGT", 4);
    data[p] = 4; data[p + 2] = 4; data[p + 6] = 1;
    p += 12; data[p++] = 255; data[p++] = 0; data[p++] = 0;
    data[p++] = 0x10; data[p++] = 0xfb;
    data[p++] = 0; data[p++] = 0; data[p++] = 16;
    data[p++] = 0xe3;
    for (i = 0; i < 16; ++i) data[p++] = 0;
    data[p++] = 0xfc;
    put32(data + 4, p);
    assert(tgv_open(&m, data, p, "key.tgv") == 0);
    assert(tgv_next(&m) == TGV_FRAME);
    assert(m.width == 4 && m.height == 4 && m.palette[0] == 255);
    assert(tgv_pixels(&m)[15] == 0 && tgv_next(&m) == TGV_END);
    assert(tgv_open(&m, data, p - 1, "short.tgv") == 0);
    assert(tgv_next(&m) == -1);
    data[8 + 12 + 3 + 5] = 0;
    assert(tgv_open(&m, data, p, "backref.tgv") == 0);
    assert(tgv_next(&m) == -1);
}

int main(void)
{
    samples(); song(); speech(); movie();
    puts("audio: PCM/pan/loop, HMI timing/rejects, TGV key/rejects passed");
    return 0;
}
