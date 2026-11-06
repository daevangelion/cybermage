#include "audio.h"
#include "music_internal.h"
#include <string.h>

#define HMI_TRACKS_MAX 64
#define HMI_OFFS_MAX 512
#define HMI_EVENTS_PER_TICK_MAX 4096
#define HMI_EVENTS_MAX 1000000

typedef struct {
    uint32_t pos, end, delta;
    uint8_t running, channel, done;
} track;
typedef struct {
    uint32_t ticks;
    uint8_t channel, key, used;
} note_off;
typedef struct {
    const uint8_t *data;
    const char *file;
    uint32_t len, phase, notes, events;
    unsigned rate, count;
    int done, paused;
    track tracks[HMI_TRACKS_MAX];
    note_off offs[HMI_OFFS_MAX];
} song;
static song playing;
static unsigned out_rate;

static int reject(const song *s, uint32_t pos, const char *why)
{
    return cm_fail(s->file, pos, why);
}

static int vlq(const song *s, track *t, uint32_t *value)
{
    unsigned i;
    *value = 0;
    for (i = 0; i < 4; ++i) {
        uint8_t b;
        if (t->pos == t->end) return reject(s, t->pos, "truncated HMI variable integer");
        b = s->data[t->pos++];
        *value = (*value << 7) | (b & 127);
        if (!(b & 128)) return 0;
    }
    return reject(s, t->pos, "HMI variable integer exceeds four bytes");
}

static void send(unsigned status, unsigned a, unsigned b)
{
    synth_event(status, a, b);
}

static void clear_channel(song *s, unsigned channel)
{
    unsigned i;
    for (i = 0; i < HMI_OFFS_MAX; ++i) if (s->offs[i].used && s->offs[i].channel == channel) {
        send(0x80 | channel, s->offs[i].key, 0); s->offs[i].used = 0;
    }
    send(0xb0 | channel, 123, 0);
}

/* emit=0 checks the entire event stream without changing synth state. */
static int event(song *s, track *t, int emit)
{
    unsigned status, a = 0, b = 0, count = 0, i;
    uint32_t duration = 0;
    if (t->pos >= t->end) return reject(s, t->pos, "missing HMI end-of-track");
    status = s->data[t->pos];
    if (status & 128) {
        ++t->pos;
        if (status < 0xf0) t->running = (uint8_t)status;
    } else {
        status = t->running;
        if (!status) return reject(s, t->pos, "HMI running status before channel event");
    }
    if (status < 0xf0) {
        count = (status & 0xe0) == 0xc0 ? 1 : 2;
        if (count > t->end - t->pos) return reject(s, t->pos, "truncated MIDI message");
        a = s->data[t->pos++]; if (count == 2) b = s->data[t->pos++];
        if (a > 127 || b > 127) return reject(s, t->pos - count, "MIDI data byte has status bit");
        if ((status & 0xf0) == 0x90 && vlq(s, t, &duration)) return -1;
        if (emit) {
            status = (status & 0xf0) | t->channel;
            ++s->events;
            if ((status & 0xf0) == 0x90 && b) {
                for (i = 0; i < HMI_OFFS_MAX && s->offs[i].used; ++i) {}
                if (i == HMI_OFFS_MAX) return reject(s, t->pos, "too many simultaneous HMI note durations");
                s->offs[i].used = 1; s->offs[i].channel = t->channel;
                s->offs[i].key = (uint8_t)a; s->offs[i].ticks = duration;
                ++s->notes;
            }
            if ((status & 0xf0) == 0xb0) {
                if (a == 0x6c) clear_channel(s, t->channel);
                else if (!((a >= 0x67 && a <= 0x6b) || a == 0x77)) send(status, a, b);
            } else send(status, a, b);
        }
    } else if (status == 0xff) {
        uint32_t length;
        if (t->pos == t->end) return reject(s, t->pos, "truncated HMI meta event");
        a = s->data[t->pos++];
        if (vlq(s, t, &length)) return -1;
        if (length > t->end - t->pos) return reject(s, t->pos, "HMI meta event outside track");
        t->pos += length;
        if (a == 0x2f) {
            if (length) return reject(s, t->pos, "invalid HMI end-of-track length");
            t->done = 1;
        }
    } else if (status == 0xf0) {
        uint32_t length;
        if (t->end - t->pos < 4) return reject(s, t->pos, "truncated HMI sysex size");
        length = cm_u32(s->data + t->pos); t->pos += 4;
        if (length > t->end - t->pos) return reject(s, t->pos, "HMI sysex outside track");
        t->pos += length;
    } else return reject(s, t->pos - 1, "unsupported HMI system/branch event");
    return 0;
}

static int parse(song *s, const uint8_t *data, uint32_t len, const char *file)
{
    uint32_t table;
    unsigned i;
    memset(s, 0, sizeof *s);
    s->data = data; s->file = file; s->len = len;
    if (!data || len < 0xec || memcmp(data, "HMI-MIDISONG061595", 18))
        return reject(s, 0, "not an HMI-MIDISONG061595 song");
    s->rate = cm_u16(data + 0xd4); s->count = cm_u16(data + 0xe4);
    table = cm_u32(data + 0xe8);
    if (!s->rate || s->rate > 1000 || !s->count || s->count > HMI_TRACKS_MAX ||
        table < 0xec || table > len || s->count * 4 > len - table)
        return reject(s, 0xd4, "invalid HMI rate or track table");
    for (i = 0; i < s->count; ++i) {
        uint32_t at = cm_u32(data + table + i * 4), start;
        track *t = &s->tracks[i];
        unsigned j;
        if (at > len || len - at < 0x80 || memcmp(data + at, "HMI-MIDITRACK", 13))
            return reject(s, at, "invalid HMI track header");
        start = cm_u32(data + at + 0x57);
        if (start < 0x80 || start >= len - at) return reject(s, at + 0x57, "HMI event offset outside track");
        t->pos = at + start; t->end = len;
        for (j = 0; j < s->count; ++j) {
            uint32_t next = cm_u32(data + table + j * 4);
            if (next > at && next < t->end) t->end = next;
            if (j != i && next == at) return reject(s, at, "duplicate HMI track");
        }
        if (t->pos >= t->end) return reject(s, t->pos, "overlapping HMI tracks");
        t->channel = (uint8_t)(cm_u16(data + at + 0x7b) & 15);
        if (vlq(s, t, &t->delta)) return -1;
        {
            track check = *t;
            for (j = 0; !check.done && j < HMI_EVENTS_MAX; ++j) {
                uint32_t delta;
                if (event(s, &check, 0)) return -1;
                if (!check.done && vlq(s, &check, &delta)) return -1;
            }
            if (!check.done) return reject(s, check.pos, "HMI event limit exceeded");
        }
    }
    return 0;
}

int audio_song(const uint8_t *data, uint32_t len, const char *file)
{
    song next;
    if (!out_rate) return cm_fail(file, 0, "audio_init must precede song playback");
    if (!synth_ready()) return cm_fail(file, 0, "HMI instrument banks must be loaded before playback");
    if (parse(&next, data, len, file)) return -1;
    synth_reset(); playing = next;
    return 0;
}

void audio_song_stop(void)
{
    playing.done = 1; synth_reset();
}
void audio_song_pause(int paused)
{
    unsigned i;
    playing.paused = paused != 0;
    if (paused) for (i = 0; i < 16; ++i) clear_channel(&playing, i);
}
int audio_song_done(void) { return playing.done; }
void audio_midi_volume(unsigned volume) { synth_volume(volume & 127); }
uint32_t audio_note_ons(void) { return playing.notes; }
uint32_t audio_midi_events(void) { return playing.events; }

void music_init(unsigned rate)
{
    out_rate = rate; memset(&playing, 0, sizeof playing); playing.done = 1;
    synth_init(rate);
}

static int tick(song *s)
{
    unsigned i, active = 0, budget = HMI_EVENTS_PER_TICK_MAX;
    for (i = 0; i < HMI_OFFS_MAX; ++i) if (s->offs[i].used) {
        if (!s->offs[i].ticks) {
            send(0x80 | s->offs[i].channel, s->offs[i].key, 0); s->offs[i].used = 0;
        } else --s->offs[i].ticks;
    }
    for (i = 0; i < s->count; ++i) {
        track *t = &s->tracks[i];
        if (t->done) continue;
        if (t->delta) --t->delta;
        else do {
            if (!budget--) return reject(s, t->pos, "HMI per-tick event limit exceeded");
            if (event(s, t, 1)) return -1;
            if (t->done) break;
            if (vlq(s, t, &t->delta)) return -1;
            if (t->delta) { --t->delta; break; }
        } while (!t->done);
        if (!t->done) active = 1;
    }
    if (!active) {
        for (i = 0; i < 16; ++i) clear_channel(s, i);
        s->done = 1;
    }
    return 0;
}

void music_frame(int32_t *left, int32_t *right)
{
    if (!playing.done && !playing.paused) {
        playing.phase += playing.rate;
        if (playing.phase >= out_rate) {
            playing.phase -= out_rate;
            if (tick(&playing)) audio_song_stop();
        }
    }
    synth_frame(left, right);
}
