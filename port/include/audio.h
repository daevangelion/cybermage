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
void audio_midi_volume(unsigned volume);
uint32_t audio_note_ons(void);
uint32_t audio_midi_events(void);

/* SPEECH.ENG contains its own scene/message/segment directory. Messages and
 * segments are one-based; scenes are 0..11. link is the signed original
 * object/video/script reference, not part of the PCM. */
int audio_speech_entry(const uint8_t *data, uint32_t len, unsigned scene,
                       unsigned message, unsigned segment, const uint8_t **out,
                       uint32_t *length, int *link);
int audio_speech_span(const uint8_t *data, uint32_t len,
                      uint32_t offset, uint32_t length, const uint8_t **out);

#endif
