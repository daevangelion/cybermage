#include "audio.h"
#include "tgv.h"
#include "world.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static world w;
static uint8_t pixels[CM_WIDTH * CM_HEIGHT], palette[768];
static int16_t pcm[2400];

static void controls(void)
{
    uint8_t definition[276] = {0};
    snd_slot state[AUDIO_SAMPLE_SLOTS];
    unsigned pan, i;
    int volume, slot;
    w.object_count = 2; w.player_object = 0; w.objects[1].x = 65536;
    assert(snd_check_song() == 1);
    assert(snd_song(31,0) == 0 && snd_check_song() == 0);
    snd_song_end(); assert(snd_check_song() == 1);
    snd_volumes(32767,32767,0); assert(snd_check_song() == 0);
    snd_volumes(32767,32767,127);
    snd_enable(0,0); assert(snd_check_song() == 0 && !snd_enabled());
    snd_enable(1,1); assert(snd_enabled());
    assert(snd_scene(&w) == 0);
    definition[0x7d] = 34;
    w.person_count = 1; w.persons[0].type = 0;
    w.monster_definitions.data = definition; w.monster_definitions.size = sizeof definition;
    assert(snd_scene(&w) == 0 && snd_play(34,32767,32768) == 0);
    snd_kill(&w); w.person_count = 0;
    assert(snd_clip(&w, 1, 131072, 32767, 0, &pan, &volume) == 1);
    assert(volume == 16383 && pan == 32768);
    assert(snd_clip(&w, 1, 65536, 32767, 100, &pan, &volume) == 0);
    snd_volumes(16384, 32767, 127);
    slot = snd_play(20, 32767, 8192); assert(slot == 0);
    snd_snapshot(state); assert(state[0].sample == 20 && state[0].volume == 16384);
    assert(!snd_done(slot, 20) && snd_done(slot, 21));
    snd_abort(slot, 21); assert(!snd_done(slot, 20));
    snd_abort(slot, 20); assert(snd_done(slot, 20));
    snd_volumes(32767, 32767, 127);
    for (i = 0; i < 16; ++i) assert(snd_loop(20, 32767, 32768) == (int)i);
    assert(snd_play(20, 32767, 32768) == -1);
    snd_kill(&w);
    for (i = 0; i < 16; ++i) assert(snd_done((int)i, 20));
    snd_enable(1, 0);
    assert(snd_message(&w, 1, 1, 0) == 0);
    assert(snd_text()->active && !snd_text()->spoken && snd_text()->line_count);
    assert(snd_text()->lines[0][0]);
    snd_message_stop(); snd_enable(1, 1);
    assert(snd_message(&w, 1, 1, 0) == 0);
    assert(snd_tick(&w) == 0);
    snd_snapshot(state); assert(state[0].sample == 999 && state[0].active);
    snd_speech_skip(); assert(snd_tick(&w) == 0);
    snd_message_stop(); snd_kill(&w);
    assert(snd_log_replay(&w, 0) == 0);
    assert(snd_text()->scene == 1 && snd_text()->message == 15 && snd_text()->object == 0);
    assert(snd_log_replay(&w, 63) == -1);
    snd_message_stop();
}

static void gain_changes(void)
{
    snd_slot state[16];
    int effect_slot;
    snd_kill(&w);
    snd_volumes(32767,32767,127);
    effect_slot = snd_loop(20,0x9fff,57344);
    assert(effect_slot == 0);
    snd_snapshot(state); assert(state[0].volume == 32767);
    snd_volumes(16384,32767,127);
    snd_snapshot(state); assert(state[0].volume == 20480 && state[0].pan == 57344);
    snd_volumes(16384,32767,127);
    snd_snapshot(state); assert(state[0].volume == 20480);
    assert(snd_message(&w,1,1,0) == 0 && snd_tick(&w) == 0);
    snd_snapshot(state); assert(state[1].sample == 999 && state[1].volume == 32767);
    snd_volumes(0,8192,127);
    snd_snapshot(state);
    assert(state[0].active && state[0].volume == 0);
    assert(state[1].active && state[1].volume == 8192);
    snd_volumes(32767,16384,127);
    snd_snapshot(state);
    assert(state[0].volume == 32767 && state[1].volume == 16384);
    assert(snd_set_panvol(0,8192,24576,0) == 0);
    snd_volumes(16384,32767,127);
    snd_snapshot(state); assert(state[0].volume == 12288 && state[0].pan == 8192);
    assert(state[1].volume == 32767);
    assert(snd_set_volume(0,8192) == 0);
    snd_volumes(32767,32767,127);
    snd_snapshot(state); assert(state[0].volume == 8192);
    snd_message_stop(); snd_kill(&w);
}

static void movies(const char *data)
{
    unsigned kind, tick;
    for (kind = SND_MOVIE_INTRO; kind <= SND_MOVIE_ENDING; ++kind) {
        assert(snd_movie_begin(data, kind, 0, 100) == 0);
        for (tick = 100; tick < 100 + 180 * CM_TICK_HZ; ++tick) {
            int result = snd_movie_step(tick, 0, pixels, palette);
            assert(result >= 0);
            if (!result) break;
            audio_mix(pcm, 1200); /*40 Hz headless callback equivalent at48000. */
        }
        assert(tick < 100 + 180 * CM_TICK_HZ && !snd_movie_active());
        assert(snd_movie_begin(data, kind, 0, 100) == 0);
        assert(snd_movie_step(100, 0, pixels, palette) == 1);
        assert(snd_movie_step(tick, 1, pixels, palette) == 0);
        assert(!snd_movie_active() && !audio_stream_pending());
    }
}

static int32_t s32(uint32_t n)
{
    return n <= 0x7fffffffu ? (int32_t)n : -1 - (int32_t)(0xffffffffu - n);
}

static void original_snapshot(const char *path)
{
    cm_file snapshot = {0};
    unsigned i, chosen = 0;
    static const unsigned samples[] = {0,3,20,33};
    assert(platform_file_read(&snapshot, path) == 0 && snapshot.size >= 0x144b80);
    memset(&w, 0, sizeof w); w.object_count = CM_OBJECT_MAX;
    w.player_object = cm_u32(snapshot.data + 0x130d04);
    assert(w.player_object < w.object_count);
    for (i = 0; i < CM_OBJECT_MAX; ++i) {
        w.objects[i].x = s32(cm_u32(snapshot.data + 0x61418 + i * 4));
        w.objects[i].y = s32(cm_u32(snapshot.data + 0x64080 + i * 4));
        w.objects[i].heading = cm_u16(snapshot.data + 0x7e65c + i * 2);
        w.objects[i].active = snapshot.data[0x7db42 + i];
    }
    assert(snd_scene(&w) == 0);
    for (i = 0; i < CM_OBJECT_MAX && chosen < 4; ++i) if (w.objects[i].active && i != w.player_object) {
        unsigned j, turn;
        uint16_t heading = w.objects[w.player_object].heading;
        for (turn = 0; turn < 4; ++turn) for (j = 0; j < 2; ++j) {
            snd_slot table[16];
            int slot;
            int32_t range = j ? 0x400000 : 0;
            w.objects[w.player_object].heading = (uint16_t)(heading + turn * 16384);
            snd_kill(&w);
            slot = snd_object(&w, samples[chosen], i, range, 32767, 4095);
            assert(slot == 0);
            snd_snapshot(table);
            printf("{\"sample\":%u,\"object\":%u,\"range\":%d,\"maximum\":32767,\"minimum\":4095,"
                   "\"heading\":%u,\"slot\":%d,\"pan\":%u,\"volume\":%d,\"loops\":%d}\n",
                   samples[chosen], i, range, w.objects[w.player_object].heading,
                   slot, table[slot].pan, table[slot].volume, table[slot].loops);
        }
        w.objects[w.player_object].heading = heading;
        ++chosen;
    }
    assert(chosen == 4);
    platform_file_release(&snapshot);
}

int main(int argc, char **argv)
{
    assert(argc >= 2);
    assert(platform_init(1) == 0);
    assert(snd_open(argv[1]) == 0);
    controls(); gain_changes(); movies(argv[1]);
    if (argc >= 3) original_snapshot(argv[2]);
    snd_close(); platform_shutdown();
    fprintf(stderr, "game audio: slot/ID/master volume, dialogue, all3 movie playback/skip checks passed\n");
    return 0;
}
