#include "audio.h"
#include "music_internal.h"
#include "opl3.h"
#include <string.h>

#define FM_VOICES 18
#define FM_PATCHES 128
#define BNK_RECORD_SIZE 30
#define BNK_NAME_SIZE 12

typedef struct {
    uint8_t op[2][5], wave[2], connection, pitch;
} patch;
typedef struct {
    unsigned channel, key, velocity, pitch, age;
    int used, held;
    const patch *instrument;
} fm_voice;
static patch melodic[FM_PATCHES], drums[FM_PATCHES];
static fm_voice voices[FM_VOICES];
static opl3_chip chip;
static unsigned programs[16], volumes[16], expression[16], pans[16], bends[16], sustain[16];
static unsigned device_rate, master = 127, age;
static int banks_ready;
static const unsigned slots[9] = {0, 1, 2, 8, 9, 10, 16, 17, 18};

static void wr(unsigned reg, unsigned value)
{
    OPL3_WriteReg(&chip, (uint16_t)reg, (uint8_t)value);
}
static unsigned reg_channel(unsigned i) { return (i / 9) * 256 + i % 9; }
static unsigned reg_slot(unsigned i) { return (i / 9) * 256 + slots[i % 9]; }

static int bank(patch *out, const uint8_t *data, uint32_t len, const char *file)
{
    uint32_t names, records;
    unsigned i;
    if (!data || len < 28 || data[0] || data[1] || memcmp(data + 2, "ADLIB-", 6) ||
        cm_u16(data + 8) != 128 || cm_u16(data + 10) != 128)
        return cm_fail(file, 0, "not a 128-patch HMI AdLib bank");
    names = cm_u32(data + 12); records = cm_u32(data + 16);
    if (names < 28 || names > len || 128 * BNK_NAME_SIZE > len - names ||
        records > len || 128 * BNK_RECORD_SIZE > len - records)
        return cm_fail(file, 12, "HMI bank tables outside file");
    for (i = 0; i < 128; ++i) {
        unsigned index = cm_u16(data + names + i * BNK_NAME_SIZE), j;
        const uint8_t *p;
        patch *q = &out[i];
        if (index >= 128) return cm_fail(file, names + i * BNK_NAME_SIZE, "HMI bank patch index out of range");
        p = data + records + index * BNK_RECORD_SIZE;
        q->pitch = data[names + i * BNK_NAME_SIZE + 2];
        if (q->pitch > 127) return cm_fail(file, names + i * BNK_NAME_SIZE + 2, "invalid drum pitch");
        for (j = 0; j < 2; ++j) {
            const uint8_t *o = p + 2 + j * 13;
            if (o[0] > 3 || o[1] > 15 || o[3] > 15 || o[4] > 15 || o[5] > 1 ||
                o[6] > 15 || o[7] > 15 || o[8] > 63 || o[9] > 1 || o[10] > 1 || o[11] > 1)
                return cm_fail(file, records + index * BNK_RECORD_SIZE + 2 + j * 13, "invalid HMI operator field");
            q->op[j][0] = (uint8_t)(o[1] | o[11] << 4 | o[5] << 5 | o[10] << 6 | o[9] << 7);
            q->op[j][1] = (uint8_t)(o[8] | o[0] << 6);
            q->op[j][2] = (uint8_t)(o[3] << 4 | o[6]);
            q->op[j][3] = (uint8_t)(o[4] << 4 | o[7]);
            q->wave[j] = p[28 + j];
            if (q->wave[j] > 3) return cm_fail(file, records + index * BNK_RECORD_SIZE + 28 + j, "unsupported OPL2 waveform");
        }
        if (p[4] > 7 || p[14] > 1) return cm_fail(file, records + index * BNK_RECORD_SIZE + 4, "invalid HMI connection");
        q->connection = (uint8_t)(p[4] << 1 | (p[14] ^ 1));
    }
    return 0;
}

int audio_banks(const uint8_t *m, uint32_t ml, const uint8_t *d, uint32_t dl)
{
    patch next_m[128], next_d[128];
    if (bank(next_m, m, ml, "MELODIC.BNK") || bank(next_d, d, dl, "DRUM.BNK")) return -1;
    audio_song_stop();
    memcpy(melodic, next_m, sizeof melodic); memcpy(drums, next_d, sizeof drums);
    banks_ready = 1;
    return 0;
}

int synth_ready(void) { return banks_ready; }

void synth_reset(void)
{
    unsigned i;
    OPL3_Reset(&chip, device_rate ? device_rate : 48000);
    wr(0x105, 1); wr(0x01, 0x20); wr(0x104, 0); wr(0xbd, 0);
    memset(voices, 0, sizeof voices); age = 0;
    for (i = 0; i < 16; ++i) {
        programs[i] = 0; volumes[i] = 100; expression[i] = 127;
        pans[i] = 64; bends[i] = 8192; sustain[i] = 0;
    }
}
void synth_init(unsigned rate) { device_rate = rate; synth_reset(); }

static void pitch_voice(unsigned i, int on)
{
    static const unsigned fnum[13] = {345,365,387,410,435,460,488,517,548,580,615,652,690};
    fm_voice *v = &voices[i];
    int scaled = (int)v->pitch * 8192 + ((int)bends[v->channel] - 8192) * 2;
    unsigned key, fraction, f, r = reg_channel(i);
    int block;
    if (scaled < 0) scaled = 0;
    if (scaled > 127 * 8192) scaled = 127 * 8192;
    key = (unsigned)scaled / 8192; fraction = (unsigned)scaled % 8192;
    f = fnum[key % 12] + (fnum[key % 12 + 1] - fnum[key % 12]) * fraction / 8192;
    block = (int)(key / 12) - 1;
    if (block < 0) { f >>= 1; block = 0; }
    if (block > 7) { f <<= block - 7; block = 7; }
    if (f > 1023) f = 1023;
    wr(0xa0 + r, f & 255); wr(0xb0 + r, (f >> 8) | (unsigned)block << 2 | (on ? 32 : 0));
}

static void level_voice(unsigned i)
{
    fm_voice *v = &voices[i];
    unsigned op, ch = v->channel, r = reg_slot(i), gain;
    gain = v->velocity * volumes[ch] * expression[ch] / (127 * 127);
    for (op = 0; op < 2; ++op) {
        unsigned level = v->instrument->op[op][1], tl = level & 63;
        /* Carrier (and both operators for additive patches) follows CC7/11.
         * Linear level scaling is intentional; DOS driver curve is unmeasured. */
        if (op || (v->instrument->connection & 1)) tl = 63 - (63 - tl) * gain / 127;
        wr(0x40 + r + op * 3, (level & 192) | tl);
    }
    wr(0xc0 + reg_channel(i), v->instrument->connection |
       (pans[ch] < 32 ? 0x10 : pans[ch] > 95 ? 0x20 : 0x30));
}

void synth_volume(unsigned volume) { master = volume; }

static void off(unsigned i)
{
    pitch_voice(i, 0); voices[i].used = 0; voices[i].held = 0;
}

void synth_event(unsigned status, unsigned a, unsigned b)
{
    unsigned ch = status & 15, type = status & 0xf0, i;
    if (type == 0x80 || (type == 0x90 && !b)) {
        for (i = 0; i < FM_VOICES; ++i) if (voices[i].used && voices[i].channel == ch && voices[i].key == a) {
            if (sustain[ch]) voices[i].held = 1; else off(i);
        }
    } else if (type == 0x90 && banks_ready) {
        unsigned selected = FM_VOICES;
        const patch *p = ch == 9 ? &drums[a] : &melodic[programs[ch]];
        for (i = 0; i < FM_VOICES; ++i) if (!voices[i].used) { selected = i; break; }
        if (selected == FM_VOICES) {
            selected = 0;
            for (i = 1; i < FM_VOICES; ++i) if (voices[i].age < voices[selected].age) selected = i;
        }
        i = selected; wr(0xb0 + reg_channel(i), 0);
        voices[i].used = 1; voices[i].held = 0; voices[i].channel = ch;
        voices[i].key = a; voices[i].velocity = b; voices[i].instrument = p;
        voices[i].pitch = ch == 9 ? p->pitch : a; voices[i].age = ++age;
        for (a = 0; a < 2; ++a) {
            unsigned r = reg_slot(i) + a * 3;
            wr(0x20 + r, p->op[a][0]); wr(0x60 + r, p->op[a][2]);
            wr(0x80 + r, p->op[a][3]); wr(0xe0 + r, p->wave[a]);
        }
        level_voice(i); pitch_voice(i, 1);
    } else if (type == 0xc0) programs[ch] = a;
    else if (type == 0xe0) {
        bends[ch] = a | b << 7;
        for (i = 0; i < FM_VOICES; ++i) if (voices[i].used && voices[i].channel == ch) pitch_voice(i, 1);
    } else if (type == 0xb0) {
        if (a == 7) volumes[ch] = b;
        else if (a == 11) expression[ch] = b;
        else if (a == 10) pans[ch] = b;
        else if (a == 64) {
            sustain[ch] = b >= 64;
            if (!sustain[ch]) for (i = 0; i < FM_VOICES; ++i)
                if (voices[i].used && voices[i].channel == ch && voices[i].held) off(i);
        } else if (a == 120 || a == 123) {
            for (i = 0; i < FM_VOICES; ++i) if (voices[i].used && voices[i].channel == ch) off(i);
        } else if (a == 121) {
            expression[ch] = 127; bends[ch] = 8192; sustain[ch] = 0;
            for (i = 0; i < FM_VOICES; ++i) if (voices[i].used && voices[i].channel == ch) {
                if (voices[i].held) off(i); else pitch_voice(i, 1);
            }
        }
        for (i = 0; i < FM_VOICES; ++i) if (voices[i].used && voices[i].channel == ch) level_voice(i);
    }
}

void synth_frame(int32_t *left, int32_t *right)
{
    int16_t out[2];
    OPL3_GenerateResampled(&chip, out);
    *left += (int32_t)out[0] * (int)master / 127;
    *right += (int32_t)out[1] * (int)master / 127;
}
