#include "audio.h"
#include "exe.h"
#include "world.h"
#include <limits.h>
#include <string.h>

#define SND_SPEECH_ID 999
#define SND_PATH_MAX 1024
#define SND_MONSTER_BYTES 276
static rsc raw;
static cm_file speech_file, text_file;
static char paths[4][SND_PATH_MAX];
static uint8_t available[SND_SAMPLE_COUNT];
static uint8_t initial_samples[SND_SAMPLE_COUNT], log_messages[63][2];
static snd_slot slots[AUDIO_SAMPLE_SLOTS];
static snd_dialogue dialogue;
static int opened, enabled = 1, speech_enabled = 1;
static int digi_volume = 32767, speech_volume = 32767;
static unsigned midi_volume = 127, playlist, music_index;
static int current_song = -1, music_loaded, song_loop, speech_slot = -1;
static unsigned next_segment, linked_count, linked_index, linked_messages[4];
static int next_video, speech_override;
static char coder[24];
static int playlists[2][16];

static int asset_path(char *out, const char *dir, const char *name)
{
    size_t a, b;
    if (!dir || !name) return cm_fail("sound", 0, "missing asset path");
    a = strlen(dir); b = strlen(name);
    if (a + 1 >= SND_PATH_MAX || b >= SND_PATH_MAX - a - 1)
        return cm_fail(dir, 0, "sound asset path too long");
    memcpy(out, dir, a); out[a] = '/'; memcpy(out + a + 1, name, b + 1);
    return 0;
}

static int clamp_volume(int n) { return n < 0 ? 0 : n > 32767 ? 32767 : n; }
static int scale_volume(int n, int master)
{
    int64_t v = (int64_t)n * master / 32767;
    return v < 0 ? 0 : v > 32767 ? 32767 : (int)v;
}
static int signed16(unsigned n) { n &= 65535; return n < 32768 ? (int)n : (int)n - 65536; }

static int load_rules(void)
{
    cm_file file = {0};
    cm_exe exe;
    int32_t words[32];
    unsigned i;
    int result;
    if (platform_file_read(&file, paths[3])) return -1;
    result = cm_exe_init(&exe, &file, paths[3]);
    if (!result) result = cm_exe_table(&exe, 0x95bf4, words, 16);
    if (!result) for (i = 0; i < 32; ++i) {
        int song = signed16((uint32_t)words[i / 2] >> ((i & 1) * 16));
        if (song < -1 || song >= 38) { result = cm_fail(paths[3], 0x95bf4 + i * 2, "invalid music playlist"); break; }
        playlists[i / 16][i % 16] = song;
    }
    if (!result) result = cm_exe_table(&exe, 0x95ed8, words, 32);
    if (!result) for (i = 0; i < 126; ++i)
        log_messages[i / 2][i & 1] = (uint8_t)((uint32_t)words[i / 4] >> ((i & 3) * 8));
    if (!result) for (i = 0; i < 63; ++i)
        if (log_messages[i][0] >= 12 || !log_messages[i][1]) {
            result = cm_fail(paths[3], 0x95ed8 + i * 2, "invalid speech recorder mapping"); break;
        }
    if (!result) result = cm_exe_table(&exe, 0x95c34, words, 30);
    if (!result) for (i = 0; i < SND_SAMPLE_COUNT; ++i) {
        initial_samples[i] = (uint8_t)((uint32_t)words[i / 4] >> ((i & 3) * 8));
        if (initial_samples[i] > 1) { result = cm_fail(paths[3], 0x95c34 + i, "invalid initial sample state"); break; }
    }
    platform_file_release(&file);
    return result;
}

static int object_ok(const world *w, unsigned object)
{
    return w && w->player_object < w->object_count && object < w->object_count;
}

/* Shared angle fold from PanVolDIGI0x28050 and PanVolDistance0x17BC9.
 * Rear sounds fold across +/-90 degrees; signed16 wrap at the exact side
 * axis is intentional, then SOS pan is limited to0x2000..0xE000. */
static unsigned object_pan(const world *w, unsigned object)
{
    const world_object *p = &w->objects[w->player_object], *o = &w->objects[object];
    unsigned angle = (uint16_t)(cm_atan64k(p->x, p->y, o->x, o->y) - p->heading);
    int delta = signed16(angle), pan;
    if (delta > 16384) angle = 32768u - angle;
    else if (delta < -16384) angle = (unsigned)(-32768) - angle;
    pan = 32768 - signed16(angle * 2);
    return pan < 8192 ? 8192u : pan > 57344 ? 57344u : (unsigned)pan;
}

int snd_clip(const world *w, unsigned object, int32_t range,
              int maximum, int minimum, unsigned *pan, int *volume)
{
    const world_object *p, *o;
    int32_t distance;
    int64_t value;
    if (!object_ok(w, object) || !pan || !volume || range < 0 || maximum < 0 || minimum < 0 || minimum > maximum)
        return cm_fail("ClipPanVol", object, "invalid object/range/volume");
    p = &w->objects[w->player_object]; o = &w->objects[object];
    distance = cm_distance(p->x, p->y, o->x, o->y);
    if (distance < 0) return cm_fail("ClipPanVol", object, "distance outside signed range");
    if (range) {
        if (distance >= range) return 0;
        value = (range >> 4) ? (int64_t)((range - distance) >> 4) * maximum / (range >> 4) : INT32_MAX;
    } else value = (int64_t)maximum + maximum / 8 - (int64_t)(distance >> 4) * maximum / 32767;
    if (value > maximum) value = maximum;
    else if (value < minimum) value = minimum;
    if (value <= 0) return 0;
    *volume = value > 32767 ? 32767 : (int)value; *pan = object_pan(w, object);
    return 1;
}

int snd_speech_panvol(const world *w, unsigned object, unsigned scene,
                      unsigned message, unsigned *pan, int *volume)
{
    const world_object *p, *o;
    int32_t v;
    if (!object_ok(w, object) || !pan || !volume)
        return cm_fail("PanVolDistance", object, "invalid speech object");
    p = &w->objects[w->player_object]; o = &w->objects[object];
    v = 36862 - (cm_distance(p->x, p->y, o->x, o->y) >> 4);
    *volume = v < 4095 ? 4095 : v > 32767 ? 32767 : v; *pan = object_pan(w, object);
    if ((scene == 9 && message == 21) || (scene == 3 && (message == 6 || message == 7)) ||
        (scene == 8 && (message == 7 || message == 15)) || scene == 10) *volume = 32767;
    if (scene == 8 && message == 15) *pan = 32768;
    return 0;
}

static void reset_slots(void)
{
    unsigned i;
    for (i = 0; i < AUDIO_SAMPLE_SLOTS; ++i) {
        audio_sample_stop(i); slots[i].active = 0;
    }
    speech_slot = -1;
}

int snd_open(const char *dir)
{
    const uint8_t *m, *d;
    uint32_t ml, dl;
    snd_close();
    if (asset_path(paths[0], dir, "RAW.RSC") || asset_path(paths[1], dir, "SPEECH.ENG") ||
        asset_path(paths[2], dir, "TEXT.MSG") || asset_path(paths[3], dir, "CM_HI.EXE") ||
        load_rules()) return -1;
    if (rsc_open(&raw, paths[0]) || platform_file_read(&speech_file, paths[1]) ||
        platform_file_read(&text_file, paths[2]) || rsc_entry(&raw, 252, &m, &ml) ||
        rsc_entry(&raw, 253, &d, &dl)) { snd_close(); return -1; }
    platform_audio_lock();
    if (audio_banks(m, ml, d, dl)) { platform_audio_unlock(); snd_close(); return -1; }
    audio_midi_volume(midi_volume); opened = 1; playlist = music_index = 0;
    current_song = -1; music_loaded = song_loop = 0;
    platform_audio_unlock();
    memset(available, 0, sizeof available);
    for (ml = 0; ml < SND_SAMPLE_COUNT; ++ml) if (initial_samples[ml])
        if (snd_sample_load(ml)) { snd_close(); return -1; }
    return 0;
}

void snd_close(void)
{
    platform_audio_lock(); reset_slots(); audio_song_stop(); audio_stream_stop();
    opened = music_loaded = 0; next_segment = linked_count = linked_index = 0;
    dialogue.active = 0; dialogue.line_count = 0;
    platform_audio_unlock();
    rsc_close(&raw); platform_file_release(&speech_file); platform_file_release(&text_file);
}

int snd_sample_load(unsigned sample)
{
    const uint8_t *p;
    uint32_t len;
    if (!opened || sample >= SND_SAMPLE_COUNT)
        return cm_fail("SoundLoad", sample, "invalid sample or unopened RAW.RSC");
    if (rsc_entry(&raw, sample, &p, &len)) return -1;
    if (!len) return cm_fail(raw.path, sample, "empty PCM sample");
    available[sample] = 1;
    return 0;
}

int snd_scene(const world *w)
{
    unsigned i, j;
    if (!opened || !w || w->person_count > CM_OBJECT_MAX)
        return cm_fail("sound scene", 0, "invalid scene or unopened sound system");
    platform_audio_lock(); reset_slots(); platform_audio_unlock();
    memset(available, 0, sizeof available);
    /* Static soundDataLoad0x95C34; scene definitions add their own samples. */
    for (i = 0; i < SND_SAMPLE_COUNT; ++i) if (initial_samples[i])
        if (snd_sample_load(i)) return -1;
    for (i = 0; i < w->person_count; ++i) {
        int type = w->persons[i].type;
        const uint8_t *d;
        if (type < 0 || (unsigned)type >= w->monster_definitions.size / SND_MONSTER_BYTES)
            return cm_fail("monster definitions", i, "sound actor type outside definitions");
        d = w->monster_definitions.data + (unsigned)type * SND_MONSTER_BYTES;
        for (j = 0; j < 3; ++j) {
            unsigned sample = cm_u16(d + 0x52 + j * 2);
            if (snd_sample_load(sample)) return -1;
        }
        for (j = 0; j < 5; ++j) {
            unsigned sample = cm_u16(d + 0xcc + j * 2);
            if (sample && snd_sample_load(sample)) return -1;
        }
        if (d[0x15] == 10 && (snd_sample_load(111) || snd_sample_load(112))) return -1;
    }
    return 0;
}

void snd_enable(int on, int spoken)
{
    platform_audio_lock(); enabled = on != 0; speech_enabled = spoken != 0;
    if (!enabled) { reset_slots(); audio_song_stop(); music_loaded = 0; }
    platform_audio_unlock();
}
void snd_volumes(int digi, int speech, unsigned midi)
{
    platform_audio_lock(); digi_volume = clamp_volume(digi); speech_volume = clamp_volume(speech);
    midi_volume = midi & 127; audio_midi_volume(midi_volume); platform_audio_unlock();
}

static int start_sample(unsigned id, const uint8_t *data, uint32_t len, int volume,
                         unsigned pan, int loops)
{
    unsigned i;
    if (!enabled) return -1;
    for (i = 0; i < AUDIO_SAMPLE_SLOTS; ++i) if (audio_sample_done(i)) break;
    if (i == AUDIO_SAMPLE_SLOTS) return -1;
    if (audio_play_sample(i, data, len, 1, 8, volume, loops, pan)) return -1;
    slots[i].sample = (int)id; slots[i].volume = volume; slots[i].pan = pan;
    slots[i].loops = loops; slots[i].active = 1;
    return (int)i;
}

static int play(unsigned sample, int volume, unsigned pan, int loops)
{
    const uint8_t *p;
    uint32_t len;
    int result;
    if (!opened || !enabled || sample >= SND_SAMPLE_COUNT || !available[sample]) return -1;
    if (pan > 65535) return cm_fail("PlayDIGI", sample, "pan outside 16-bit range");
    if (rsc_entry(&raw, sample, &p, &len)) return -1;
    platform_audio_lock();
    volume = scale_volume(volume, digi_volume);
    result = volume > 0 ? start_sample(sample, p, len, volume, pan, loops) : -1;
    platform_audio_unlock();
    return result;
}
int snd_play(unsigned sample, int volume, unsigned pan) { return play(sample, volume, pan, 0); }
int snd_loop(unsigned sample, int volume, unsigned pan) { return play(sample, volume, pan, -1); }
int snd_object(const world *w, unsigned sample, unsigned object, int32_t range, int maximum, int minimum)
{
    unsigned pan;
    int volume, result = snd_clip(w, object, range, maximum, minimum, &pan, &volume);
    return result <= 0 ? -1 : snd_play(sample, volume, pan);
}

int snd_done(int slot, unsigned sample)
{
    int result;
    platform_audio_lock();
    result = !enabled || slot < 0 || slot >= AUDIO_SAMPLE_SLOTS ||
             slots[slot].sample != (int)sample || audio_sample_done((unsigned)slot);
    platform_audio_unlock(); return result;
}
void snd_abort(int slot, unsigned sample)
{
    platform_audio_lock();
    if (slot >= 0 && slot < AUDIO_SAMPLE_SLOTS && slots[slot].sample == (int)sample) {
        audio_sample_stop((unsigned)slot); slots[slot].active = 0;
    }
    platform_audio_unlock();
}
int snd_set_panvol(int slot, unsigned pan, int volume, int spoken)
{
    int result;
    if (slot < 0 || slot >= AUDIO_SAMPLE_SLOTS) return 0;
    platform_audio_lock(); volume = scale_volume(volume, spoken ? speech_volume : digi_volume);
    result = audio_sample_panvol((unsigned)slot, pan, volume);
    if (!result) { slots[slot].pan = pan; slots[slot].volume = volume; }
    platform_audio_unlock(); return result;
}
int snd_set_volume(int slot, int volume)
{
    int result;
    if (slot < 0 || slot >= AUDIO_SAMPLE_SLOTS) return 0;
    platform_audio_lock(); volume = scale_volume(volume, digi_volume);
    result = audio_sample_panvol((unsigned)slot, slots[slot].pan, volume);
    if (!result) slots[slot].volume = volume;
    platform_audio_unlock(); return result;
}
void snd_snapshot(snd_slot out[AUDIO_SAMPLE_SLOTS])
{
    unsigned i;
    platform_audio_lock();
    for (i = 0; i < AUDIO_SAMPLE_SLOTS; ++i) { out[i] = slots[i]; out[i].active = !audio_sample_done(i); }
    platform_audio_unlock();
}

static void actor_slots_clear(world *w, int stop)
{
    unsigned i;
    if (!w) return;
    for (i = 0; i < w->person_count; ++i) {
        int a = w->persons[i].unk_004C, b = w->persons[i].unk_0050;
        if (stop && a >= 0 && a < AUDIO_SAMPLE_SLOTS) audio_sample_stop((unsigned)a);
        if (stop && b >= 0 && b < AUDIO_SAMPLE_SLOTS) audio_sample_stop((unsigned)b);
        w->persons[i].unk_004C = w->persons[i].unk_0050 = -1;
    }
}
void snd_kill(world *w)
{
    platform_audio_lock(); reset_slots(); actor_slots_clear(w, 0); audio_song_stop(); music_loaded = 0;
    dialogue.active = 0; next_segment = linked_count = 0; platform_audio_unlock();
}
int snd_kill_loops(world *w, int transition)
{
    unsigned i;
    platform_audio_lock();
    for (i = 0; i < AUDIO_SAMPLE_SLOTS; ++i)
        if (slots[i].loops || slots[i].sample == SND_SPEECH_ID) audio_sample_stop(i);
    actor_slots_clear(w, 1); audio_song_stop(); music_loaded = song_loop = 0;
    speech_slot = -1; platform_audio_unlock();
    return transition ? snd_song(playlist ? 16 : 31, 0) : 0;
}

static int song_start(int number)
{
    const uint8_t *p;
    uint32_t len;
    if (number < 0 || number >= 38) return cm_fail("PlaySONG", (uint32_t)number, "song outside RAW.RSC song table");
    if (rsc_entry(&raw, 192 + (unsigned)number, &p, &len) || audio_song(p, len, raw.path)) return -1;
    current_song = number; music_loaded = 1; return 0;
}
int snd_song(int number, int loop)
{
    int result = 0;
    if (!opened || !enabled) return 0;
    platform_audio_lock();
    result = song_start(number); if (!result) song_loop = loop != 0;
    platform_audio_unlock(); return result;
}
int snd_song_default(int loop)
{
    return snd_song(playlists[playlist][music_index], loop);
}
void snd_song_end(void)
{
    platform_audio_lock(); audio_song_stop(); music_loaded = 0; platform_audio_unlock();
}
void snd_song_pause(int paused)
{
    platform_audio_lock(); audio_song_pause(paused); platform_audio_unlock();
}

/* TEXT.MSG: 52-byte scene header, eight-byte message records, then a u16
 * line count, u16 offsets relative to the following string block. */
static int text_load(unsigned scene, unsigned message)
{
    const uint8_t *d = text_file.data, *strings;
    uint32_t directory, count, first, end, offset, len, string_len;
    unsigned i, lines;
    if (text_file.size < 60 || scene >= 12 || !message)
        return cm_fail(paths[2], 0, "invalid text message request");
    directory = cm_u32(d);
    if (directory % 8 || directory > text_file.size - 52)
        return cm_fail(paths[2], 0, "invalid text directory");
    count = directory / 8; first = cm_u32(d + 4 + scene * 4);
    end = scene == 11 ? count : cm_u32(d + 8 + scene * 4);
    if (first > end || end > count || message > end - first)
        return cm_fail(paths[2], 4 + scene * 4, "text message outside scene");
    offset = cm_u32(d + 52 + (first + message - 1) * 8);
    len = cm_u32(d + 56 + (first + message - 1) * 8);
    if (offset < 52 + directory || offset > text_file.size || len > text_file.size - offset || len < 2)
        return cm_fail(paths[2], offset, "text message outside file");
    lines = cm_u16(d + offset);
    if (lines > SND_TEXT_LINES_MAX || 2 + lines * 2 > len || len - 2 - lines * 2 > 8192)
        return cm_fail(paths[2], offset, "text line/string buffer limit exceeded");
    strings = d + offset + 2 + lines * 2; string_len = len - 2 - lines * 2;
    for (i = 0; i < lines; ++i) {
        unsigned start = cm_u16(d + offset + 2 + i * 2);
        if (start >= string_len || !memchr(strings + start, 0, string_len - start))
            return cm_fail(paths[2], offset + 2 + i * 2, "text line outside string block");
        dialogue.lines[i] = (const char *)strings + start;
    }
    dialogue.line_count = lines; return 0;
}

static int speech_segment(const world *w, unsigned scene, unsigned message, unsigned segment, int linked)
{
    const uint8_t *pcm;
    uint32_t len;
    unsigned pan = 32768, first, at, count;
    int volume = 32767, link, slot;
    if (audio_speech_entry(speech_file.data, speech_file.size, scene, message, segment, &pcm, &len, &link)) return -1;
    dialogue.link = link;
    if (link <= -100) {
        unsigned row = (unsigned)(-link - 100), base;
        if (row >= 3) return cm_fail(paths[1], 0, "invalid code-linked speech row");
        base = 3 + row * 9;
        for (at = 0; at < 4; ++at) {
            unsigned char c = (unsigned char)coder[row * 8 + at];
            linked_messages[at] = c >= 'A' && c <= 'D' ? base + c - 'A' :
                                  c >= '1' && c <= '4' ? base + 4 + c - '1' : base + at;
        }
        linked_count = 4; linked_index = 0;
    } else if (link > 0 && !speech_override) {
        if ((unsigned)link >= w->person_count) return cm_fail(paths[1], 0, "speech actor outside scene");
        if (w->persons[link].status == 2) { if (!linked) next_segment = 0; return 0; }
        dialogue.object = w->persons[link].object;
    } else if (link < 0) next_video = -link;
    if (dialogue.object > 0 && snd_speech_panvol(w, (unsigned)dialogue.object,
                                               dialogue.scene, dialogue.message, &pan, &volume)) return -1;
    slot = start_sample(SND_SPEECH_ID, pcm, len, scale_volume(clamp_volume(volume), speech_volume), pan, 0);
    if (slot < 0) return 1; /* Full SOS slot table retries this segment. */
    speech_slot = slot;
    first = cm_u32(speech_file.data + 4 + scene * 4);
    at = 52 + (first + message - 1) * 6; count = cm_u16(speech_file.data + at + 4);
    if (!linked) next_segment = segment < count ? segment + 1 : 0;
    return 0;
}

int snd_message(const world *w, unsigned scene, unsigned message, int object)
{
    if (!opened || !w || scene >= 12 || object < 0 || (unsigned)object >= w->object_count)
        return cm_fail("LoadTextMessage", message, "invalid message object or sound system");
    if (text_load(scene, message)) return -1;
    snd_message_stop();
    dialogue.scene = scene; dialogue.message = message; dialogue.object = object;
    dialogue.active = 1; dialogue.spoken = speech_enabled && enabled; dialogue.link = 0;
    speech_override = 0;
    ++dialogue.revision; next_segment = dialogue.spoken ? 1 : 0;
    return 0;
}
/* CM_HI.EXE speechLogData0x95ED8: recorder slot -> scene/message. */
int snd_log_replay(const world *w, unsigned index)
{
    int result;
    if (!opened || index >= 63) return cm_fail("SpeechRecorder", index, "recording outside speech log");
    result = snd_message(w, log_messages[index][0], log_messages[index][1], 0);
    if (!result) speech_override = 1;
    return result;
}
void snd_message_stop(void)
{
    platform_audio_lock();
    if (speech_slot >= 0 && slots[speech_slot].sample == SND_SPEECH_ID) audio_sample_stop((unsigned)speech_slot);
    speech_slot = -1; next_segment = linked_count = linked_index = 0;
    dialogue.active = 0; platform_audio_unlock();
}
void snd_speech_skip(void)
{
    platform_audio_lock();
    if (speech_slot >= 0) audio_sample_stop((unsigned)speech_slot);
    platform_audio_unlock();
}
const snd_dialogue *snd_text(void) { return &dialogue; }
int snd_take_video(void) { int result = next_video; next_video = 0; return result; }
void snd_code(const char code[24]) { memcpy(coder, code, sizeof coder); }
void snd_speech_override(int on) { speech_override = on != 0; }

int snd_tick(world *w)
{
    int result = 0;
    if (!opened || !w) return 0;
    platform_audio_lock();
    if (audio_song_error()) { platform_audio_unlock(); return -1; }
    if (enabled && midi_volume) {
        if (!music_loaded || audio_song_done()) {
            if (!song_loop || !music_loaded) {
                ++music_index;
                if (music_index >= 16 || playlists[playlist][music_index] < 0) {
                    playlist ^= 1; music_index = 0;
                }
                current_song = playlists[playlist][music_index];
            }
            result = song_start(current_song);
        }
    } else if (music_loaded) { audio_song_stop(); music_loaded = 0; }
    if (!result && dialogue.active && dialogue.spoken) {
        int waiting = speech_slot >= 0 && !audio_sample_done((unsigned)speech_slot);
        if (!speech_override && dialogue.object > 0 && (unsigned)dialogue.object < w->object_count) {
            const world_object *o = &w->objects[dialogue.object];
            if (o->targetable == 1 && o->person >= 0 && (unsigned)o->person < w->person_count &&
                (unsigned)o->person != w->player_person && w->persons[o->person].status == 2 &&
                !(w->scene == 9 && o->person == 22)) {
                if (speech_slot >= 0) audio_sample_stop((unsigned)speech_slot);
                next_segment = linked_count = 0; waiting = 0;
            }
        }
        if (waiting && dialogue.object > 0) {
            unsigned pan; int volume;
            result = snd_speech_panvol(w, (unsigned)dialogue.object, dialogue.scene, dialogue.message, &pan, &volume);
            if (!result) {
                volume = scale_volume(volume, speech_volume);
                result = audio_sample_panvol((unsigned)speech_slot, pan, volume);
                slots[speech_slot].pan = pan; slots[speech_slot].volume = volume;
            }
        } else if (!waiting) {
            speech_slot = -1;
            if (linked_count) {
                result = speech_segment(w, 4, linked_messages[linked_index], 1, 1);
                if (!result) { ++linked_index; --linked_count; }
            } else if (next_segment) result = speech_segment(w, dialogue.scene, dialogue.message, next_segment, 0);
            else { dialogue.active = 0; ++dialogue.revision; }
            if (result == 1) result = 0;
        }
    }
    platform_audio_unlock(); return result;
}
