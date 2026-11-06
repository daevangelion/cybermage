#ifndef CM_AUDIO_H
#define CM_AUDIO_H

#include "cm.h"

#define AUDIO_SAMPLE_SLOTS 16
#define AUDIO_SAMPLE_RATE 11025
#define AUDIO_VOLUME_MAX 32767
#define AUDIO_PAN_CENTRE 32768
#define AUDIO_MIDI_VOLUME_MAX 127

/* Borrowed PCM/song/bank bytes must live until stopped. Callers serialize
 * control calls against audio_mix (SDL_LockAudioDevice on the platform). */
int audio_init(unsigned rate);
int audio_play_sample(unsigned slot, const uint8_t *data, uint32_t len,
                      unsigned channels, unsigned bits, int volume,
                      int loops, unsigned pan);
int audio_play_pcm(unsigned slot, const uint8_t *data, uint32_t len,
                   unsigned rate, unsigned channels, int signed_pcm,
                   int volume, int loops, unsigned pan);
void audio_sample_stop(unsigned slot);
int audio_sample_done(unsigned slot);
int audio_sample_panvol(unsigned slot, unsigned pan, int volume);
void audio_mix(int16_t *out, unsigned frames);

/* HMI banks use the shipped HMI two-operator FM patches. */
int audio_banks(const uint8_t *melodic, uint32_t melodic_len,
                const uint8_t *drums, uint32_t drums_len);
int audio_song(const uint8_t *data, uint32_t len, const char *file);
void audio_song_stop(void);
void audio_song_pause(int paused);
int audio_song_done(void);
/* Main-thread control call (under the device lock), never from audio_mix. */
int audio_song_error(void);
void audio_midi_volume(unsigned volume);
uint32_t audio_note_ons(void);
uint32_t audio_midi_events(void);

/* Movie PCM keeps borrowed chunk spans queued, with phase continuous at
 * chunk boundaries. These controls obey the same callback-lock contract. */
int audio_stream_queue(const uint8_t *data, uint32_t len, unsigned rate,
                        unsigned channels);
void audio_stream_stop(void);
int audio_stream_pending(void);

/* SPEECH.ENG contains its own scene/message/segment directory. Messages and
 * segments are one-based; scenes are 0..11. link is the signed original
 * object/video/script reference, not part of the PCM. */
int audio_speech_entry(const uint8_t *data, uint32_t len, unsigned scene,
                       unsigned message, unsigned segment, const uint8_t **out,
                       uint32_t *length, int *link);
int audio_speech_span(const uint8_t *data, uint32_t len,
                      uint32_t offset, uint32_t length, const uint8_t **out);

struct world;
#define SND_SAMPLE_COUNT 117
#define SND_TEXT_LINES_MAX 256

typedef struct {
    int sample, volume, loops;
    unsigned pan;
    int active;
} snd_slot;
typedef struct {
    const char *lines[SND_TEXT_LINES_MAX];
    unsigned line_count, scene, message, revision;
    int object, link, active, spoken;
} snd_dialogue;

/* Game-side resource owner. platform_init/audio_init precede snd_open.
 * All control functions below lock the device internally. */
int snd_open(const char *asset_dir);
void snd_close(void);
int snd_scene(const struct world *w);
int snd_sample_load(unsigned sample);
void snd_enable(int enabled, int speech_enabled);
void snd_volumes(int digital, int speech, unsigned midi);
int snd_play(unsigned sample, int volume, unsigned pan);
int snd_loop(unsigned sample, int volume, unsigned pan);
int snd_object(const struct world *w, unsigned sample, unsigned object,
                int32_t range, int max_volume, int min_volume);
int snd_clip(const struct world *w, unsigned object, int32_t range,
              int max_volume, int min_volume, unsigned *pan, int *volume);
int snd_speech_panvol(const struct world *w, unsigned object, unsigned scene,
                      unsigned message, unsigned *pan, int *volume);
int snd_done(int slot, unsigned sample);
void snd_abort(int slot, unsigned sample);
int snd_set_panvol(int slot, unsigned pan, int volume, int speech);
int snd_set_volume(int slot, int volume);
void snd_kill(struct world *w);
int snd_kill_loops(struct world *w, int play_transition);
void snd_snapshot(snd_slot out[AUDIO_SAMPLE_SLOTS]);
int snd_song(int number, int loop);
int snd_song_default(int loop);
void snd_song_end(void);
void snd_song_pause(int paused);
int snd_tick(struct world *w);
int snd_message(const struct world *w, unsigned scene, unsigned message,
                 int object);
int snd_log_replay(const struct world *w, unsigned log_index);
void snd_message_stop(void);
void snd_speech_skip(void);
void snd_speech_override(int enabled);
const snd_dialogue *snd_text(void);
int snd_take_video(void);
void snd_code(const char code[24]);

#endif
