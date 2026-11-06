#include "audio.h"
#include "platform.h"
#include "tgv.h"
#include <string.h>

static cm_file movie_file;
static tgv decoder;
static char movie_path[1024];
static uint32_t start_tick, frame_count;
static int playing, ended;

void snd_movie_end(void)
{
    platform_audio_lock(); audio_stream_stop(); platform_audio_unlock();
    platform_file_release(&movie_file); playing = ended = 0;
}
int snd_movie_active(void) { return playing; }

/* CM_HI.EXE IntroEpilogue0x1F4EC:1=intro,2=credits,otherwise=ending. */
int snd_movie_begin(const char *dir, unsigned kind, unsigned language, uint32_t tick)
{
    static const char *const names[3][3] = {
        {"ECM.TGV", "FCM.TGV", "GCM.TGV"},
        {"CREDENG.TGV", "CREDFRE.TGV", "CREDGER.TGV"},
        {"CMENDE.TGV", "CMENDF.TGV", "CMENDG.TGV"}
    };
    const char *name;
    size_t a, b;
    if (!dir || kind < SND_MOVIE_INTRO || kind > SND_MOVIE_ENDING || language > 2)
        return cm_fail("IntroEpilogue", 0, "invalid movie/language");
    a = strlen(dir); name = names[kind - 1][language]; b = strlen(name);
    if (a + 1 >= sizeof movie_path || b >= sizeof movie_path - a - 1)
        return cm_fail(dir, 0, "movie path too long");
    snd_movie_end();
    memcpy(movie_path, dir, a); movie_path[a] = '/'; memcpy(movie_path + a + 1, name, b + 1);
    if (platform_file_read(&movie_file, movie_path)) return -1;
    if (tgv_open(&decoder, movie_file.data, movie_file.size, movie_path)) { snd_movie_end(); return -1; }
    snd_kill(NULL); start_tick = tick; frame_count = 0; playing = 1;
    return 0;
}

static void present(uint8_t *pixels, uint8_t *palette)
{
    unsigned scale_x = CM_WIDTH / decoder.width, scale_y = CM_HEIGHT / decoder.height;
    unsigned scale = scale_x < scale_y ? scale_x : scale_y;
    unsigned x, y, ox = (CM_WIDTH - decoder.width * scale) / 2;
    unsigned oy = (CM_HEIGHT - decoder.height * scale) / 2;
    const uint8_t *src = tgv_pixels(&decoder);
    memset(pixels, 0, CM_WIDTH * CM_HEIGHT); memcpy(palette, decoder.palette, 768);
    for (y = 0; y < decoder.height * scale; ++y)
        for (x = 0; x < decoder.width * scale; ++x)
            pixels[(oy + y) * CM_WIDTH + ox + x] = src[(y / scale) * decoder.width + x / scale];
}

/* No wall clock or device calls here: UI/Sim supplies its40 Hz clock, presents
 * the returned frame, and consumes movie audio through the existing callback.
 * Return1=active,0=finished/skipped,-1=decoder/queue failure. */
int snd_movie_step(uint32_t tick, int skip, uint8_t pixels[CM_WIDTH * CM_HEIGHT],
                    uint8_t palette[768])
{
    uint32_t elapsed = tick - start_tick;
    unsigned budget = 128;
    int changed = 0;
    if (!playing) return 0;
    if (!pixels || !palette) return cm_fail(movie_path, 0, "missing movie output buffers");
    if (skip) { snd_movie_end(); return 0; }
    while (!ended && (uint64_t)frame_count * CM_TICK_HZ <= (uint64_t)elapsed * TGV_FPS) {
        int kind;
        if (!budget--) { snd_movie_end(); return cm_fail(movie_path, decoder.pos, "movie catch-up limit exceeded"); }
        kind = tgv_next(&decoder);
        if (kind < 0) { snd_movie_end(); return -1; }
        if (kind == TGV_END) { ended = 1; break; }
        if (kind == TGV_AUDIO) {
            int result;
            platform_audio_lock();
            result = audio_stream_queue(decoder.audio, decoder.audio_len,
                                         decoder.audio_rate, decoder.audio_channels);
            platform_audio_unlock();
            if (result) { snd_movie_end(); return -1; }
        } else { ++frame_count; changed = 1; }
    }
    /* Queue the PCM following the displayed frame before waiting for the next
     * video deadline. Otherwise a40 Hz caller can underrun every15 Hz chunk. */
    while (!ended && decoder.len - decoder.pos >= 8 &&
           !memcmp(decoder.data + decoder.pos, "1SNd", 4)) {
        int result = tgv_next(&decoder);
        if (result < 0) { snd_movie_end(); return -1; }
        if (result != TGV_AUDIO) { snd_movie_end(); return cm_fail(movie_path, decoder.pos, "expected movie PCM chunk"); }
        platform_audio_lock();
        result = audio_stream_queue(decoder.audio, decoder.audio_len,
                                     decoder.audio_rate, decoder.audio_channels);
        platform_audio_unlock();
        if (result) { snd_movie_end(); return -1; }
    }
    if (changed) present(pixels, palette);
    if (ended) {
        int pending;
        platform_audio_lock(); pending = audio_stream_pending(); platform_audio_unlock();
        if (!pending) { snd_movie_end(); return 0; }
    }
    return 1;
}
