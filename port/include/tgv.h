#ifndef CM_TGV_H
#define CM_TGV_H

#include "cm.h"

#define TGV_WIDTH_MAX 640
#define TGV_HEIGHT_MAX 480
#define TGV_PIXELS_MAX (TGV_WIDTH_MAX * TGV_HEIGHT_MAX)
#define TGV_FPS 15
#define TGV_END 0
#define TGV_FRAME 1
#define TGV_AUDIO 2

/* Caller owns this workspace; pixels remain valid until the next frame. */
typedef struct {
    const uint8_t *data;
    const char *file;
    uint32_t len, pos;
    unsigned width, height, current;
    unsigned audio_rate, audio_channels;
    const uint8_t *audio;
    uint32_t audio_len;
    uint8_t palette[768];
    uint8_t pixels[2][TGV_PIXELS_MAX];
} tgv;

int tgv_open(tgv *movie, const uint8_t *data, uint32_t len, const char *file);
/* Returns TGV_FRAME/TGV_AUDIO/TGV_END or -1 (cm_last_error). EACS audio is
 * signed 8-bit interleaved PCM at movie->audio_rate/channels. */
int tgv_next(tgv *movie);
const uint8_t *tgv_pixels(const tgv *movie);

#define SND_MOVIE_INTRO 1
#define SND_MOVIE_CREDITS 2
#define SND_MOVIE_ENDING 3
/* IntroEpilogue: language 0=English,1=French,2=German. Presentation stays
 * with the caller; tick is the game's absolute 40 Hz tick count. */
int snd_movie_begin(const char *asset_dir, unsigned kind, unsigned language,
                     uint32_t tick);
int snd_movie_step(uint32_t tick, int skip, uint8_t pixels[CM_WIDTH * CM_HEIGHT],
                    uint8_t palette[768]);
void snd_movie_end(void);
int snd_movie_active(void);

#endif
