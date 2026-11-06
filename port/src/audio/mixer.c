#include "audio.h"
#include "music_internal.h"
#include <string.h>

typedef struct {
    const uint8_t *data;
    uint32_t frames, index, phase;
    unsigned rate, channels, pan;
    int signed_pcm, volume, loops;
} sample;

static sample samples[AUDIO_SAMPLE_SLOTS];
static unsigned output_rate;
#define STREAM_CHUNKS_MAX 64
typedef struct { const uint8_t *data; uint32_t frames; } stream_chunk;
static stream_chunk stream_chunks[STREAM_CHUNKS_MAX];
static unsigned stream_head, stream_count, stream_rate, stream_channels;
static uint32_t stream_index, stream_phase;

int audio_init(unsigned rate)
{
    if (rate < 8000 || rate > 192000)
        return cm_fail("audio", 0, "unsupported device sample rate");
    output_rate = rate;
    memset(samples, 0, sizeof samples);
    audio_stream_stop();
    music_init(rate);
    return 0;
}

int audio_play_pcm(unsigned slot, const uint8_t *data, uint32_t len,
                   unsigned rate, unsigned channels, int signed_pcm,
                   int volume, int loops, unsigned pan)
{
    sample *s;
    if (!output_rate || slot >= AUDIO_SAMPLE_SLOTS || !data || !len ||
        rate < 4000 || rate > 192000 || (channels != 1 && channels != 2) ||
        len % channels || loops < -1 || pan > 65535)
        return cm_fail("PCM", 0, "invalid sample format or slot");
    s = &samples[slot];
    s->data = data; s->frames = len / channels; s->index = 0; s->phase = 0;
    s->rate = rate; s->channels = channels; s->signed_pcm = signed_pcm != 0;
    s->volume = volume < 0 ? 0 : volume > AUDIO_VOLUME_MAX ? AUDIO_VOLUME_MAX : volume;
    s->loops = loops; s->pan = pan;
    return 0;
}

/* CM_HI.EXE 0x66C3C fixes sample rate at 0x2B11, i.e. 11025 Hz. */
int audio_play_sample(unsigned slot, const uint8_t *data, uint32_t len,
                      unsigned channels, unsigned bits, int volume,
                      int loops, unsigned pan)
{
    if (bits != 8)
        return cm_fail("PCM", 0, "only 8-bit PCM is supported");
    return audio_play_pcm(slot, data, len, AUDIO_SAMPLE_RATE, channels, 0,
                          volume, loops, pan);
}

void audio_sample_stop(unsigned slot)
{
    if (slot < AUDIO_SAMPLE_SLOTS) samples[slot].data = NULL;
}

int audio_sample_done(unsigned slot)
{
    return slot >= AUDIO_SAMPLE_SLOTS || !samples[slot].data;
}

int audio_sample_panvol(unsigned slot, unsigned pan, int volume)
{
    if (slot >= AUDIO_SAMPLE_SLOTS || pan > 65535)
        return cm_fail("PCM", slot, "invalid sample slot or pan");
    samples[slot].pan = pan;
    samples[slot].volume = volume < 0 ? 0 : volume > AUDIO_VOLUME_MAX ? AUDIO_VOLUME_MAX : volume;
    return 0;
}

static int pcm(uint8_t b, int sign)
{
    return (sign ? (b < 128 ? (int)b : (int)b - 256) : (int)b - 128) * 256;
}

static int16_t clip(int32_t n)
{
    return (int16_t)(n < -32768 ? -32768 : n > 32767 ? 32767 : n);
}

void audio_stream_stop(void)
{
    stream_head = stream_count = stream_rate = stream_channels = 0;
    stream_index = stream_phase = 0;
}

int audio_stream_pending(void) { return stream_count != 0; }

int audio_stream_queue(const uint8_t *data, uint32_t len, unsigned rate,
                        unsigned channels)
{
    unsigned at;
    if (!output_rate || !data || !len || rate < 4000 || rate > 192000 ||
        (channels != 1 && channels != 2) || len % channels ||
        stream_count == STREAM_CHUNKS_MAX ||
        (stream_rate && (rate != stream_rate || channels != stream_channels)))
        return cm_fail("movie PCM", 0, "invalid/full movie PCM queue");
    stream_rate = rate; stream_channels = channels;
    at = (stream_head + stream_count) % STREAM_CHUNKS_MAX;
    stream_chunks[at].data = data; stream_chunks[at].frames = len / channels;
    ++stream_count;
    return 0;
}

static void stream_advance(void)
{
    while (stream_count && stream_index >= stream_chunks[stream_head].frames) {
        stream_index -= stream_chunks[stream_head].frames;
        stream_head = (stream_head + 1) % STREAM_CHUNKS_MAX; --stream_count;
    }
}

static void stream_frame(int32_t *left, int32_t *right)
{
    const stream_chunk *s;
    stream_advance();
    if (!stream_count) return;
    s = &stream_chunks[stream_head];
    *left += pcm(s->data[stream_index * stream_channels], 1);
    *right += pcm(s->data[stream_index * stream_channels + stream_channels - 1], 1);
    stream_phase += stream_rate;
    stream_index += stream_phase / output_rate;
    stream_phase %= output_rate;
    stream_advance();
    /* Carry sub-sample phase across chunk boundaries, including temporary
     * empty queues. Movie scheduling must not drop/resample chunk edges. */
}

void audio_mix(int16_t *out, unsigned frames)
{
    unsigned f, n;
    for (f = 0; f < frames; ++f) {
        int32_t left = 0, right = 0;
        music_frame(&left, &right);
        stream_frame(&left, &right);
        for (n = 0; n < AUDIO_SAMPLE_SLOTS; ++n) {
            sample *s = &samples[n];
            uint32_t advance;
            int l, r, gain_l, gain_r;
            if (!s->data) continue;
            l = pcm(s->data[s->index * s->channels], s->signed_pcm);
            r = pcm(s->data[s->index * s->channels + s->channels - 1], s->signed_pcm);
            /* SOS pan has full gain in both speakers at centre, not -3 dB. */
            gain_l = s->pan <= AUDIO_PAN_CENTRE ? 32767 : (int)(65535 - s->pan);
            gain_r = s->pan >= 32767 ? 32767 : (int)s->pan;
            left += (int32_t)((int64_t)l * s->volume * gain_l / (32767 * 32767));
            right += (int32_t)((int64_t)r * s->volume * gain_r / (32767 * 32767));
            s->phase += s->rate;
            advance = s->phase / output_rate;
            s->phase %= output_rate;
            s->index += advance;
            while (s->index >= s->frames) {
                if (!s->loops) { s->data = NULL; break; }
                if (s->loops > 0) --s->loops;
                s->index -= s->frames;
            }
        }
        out[f * 2] = clip(left); out[f * 2 + 1] = clip(right);
    }
}

int audio_speech_span(const uint8_t *data, uint32_t len,
                      uint32_t offset, uint32_t length, const uint8_t **out)
{
    if (!data || !out || !length || offset > len || length > len - offset)
        return cm_fail("SPEECH.ENG", offset, "speech slice outside file");
    *out = data + offset;
    return 0;
}
