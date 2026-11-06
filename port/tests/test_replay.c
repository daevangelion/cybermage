#include "replay.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static char trace[4096], clocks[512];
static const char *clock_input;

int platform_file_exists(const char *path)
{
    return strstr(path, "actor-clocks.csv") != NULL && clock_input != NULL;
}

int platform_file_read(cm_file *file, const char *path)
{
    if (strstr(path, "actor-clocks.csv")) {
        if (!clock_input) return -1;
        memcpy(clocks, clock_input, strlen(clock_input) + 1);
        file->data = (uint8_t *)clocks;
        file->size = (uint32_t)strlen(clocks);
    } else {
        file->data = (uint8_t *)trace;
        file->size = (uint32_t)strlen(trace);
    }
    return 0;
}
void platform_file_release(cm_file *file) { memset(file, 0, sizeof *file); }

static void fixture(void)
{
    unsigned frame, column;
    size_t at;
    memset(trace, 0, sizeof trace);
    at = (size_t)snprintf(trace, sizeof trace,
        "CMTRACE1,0\nframe,ticks,previous,delta,limit_time,limit_flag,object,person,"
        "x,y,z,heading,pitch,body,kblatch,bios_shift,object_z,camera_u,camera_v,"
        "camera_z,camera_angle,horizon,scene,keys,input_keys,ranges...\n");
    for (frame = 0; frame < 3; ++frame) {
        at += (size_t)snprintf(trace + at, sizeof trace - at, "%u", frame);
        for (column = 1; column < CM_TRACE_COLUMNS; ++column)
            at += (size_t)snprintf(trace + at, sizeof trace - at, ",0");
        trace[at++] = ',';
        memset(trace + at, '0', 256); at += 256;
        trace[at++] = ',';
        memset(trace + at, '0', 256); at += 256;
        trace[at++] = '\n';
    }
}

int main(void)
{
    cm_replay r;
    cm_replay_frame f;
    sim_state s = {0};
    fixture();
    clock_input = "frame,fast_ticks,normal_ticks\n0,10,11\n";
    assert(!replay_open(&r, "frames.csv"));
    assert(replay_next(&r, &f) == 1);
    /* A missing middle-frame sample is not a license to invent phase clocks. */
    assert(replay_next(&r, &f) == -1);
    assert(!strcmp(cm_last_error()->file, "actor-clocks.csv"));
    assert(cm_last_error()->offset == strlen(clock_input));
    replay_close(&r);
    fixture();
    clock_input = "frame,fast_ticks,normal_ticks\n0,10,11\n1,12,13\n";
    assert(!replay_open(&r, "frames.csv"));
    assert(replay_next(&r, &f) == 1 && f.has_actor_clocks);
    assert(f.fast_actor_ticks == 10 && f.normal_actor_ticks == 11);
    assert(replay_next(&r, &f) == 1 && f.has_actor_clocks);
    assert(f.fast_actor_ticks == 12 && f.normal_actor_ticks == 13);
    assert(replay_next(&r, &f) == 1 && !f.has_actor_clocks);
    assert(!replay_next(&r, &f));
    replay_close(&r);
    fixture();
    clock_input = "frame,fast_ticks,normal_ticks\n0,10,11\n2,12,13\n";
    assert(!replay_open(&r, "frames.csv"));
    assert(replay_next(&r, &f) == 1);
    assert(replay_next(&r, &f) == -1);
    replay_close(&r);
    fixture();
    clock_input = "frame,fast_ticks,normal_ticks\n0,10,11\n1,12,13\n2,14,15\n3,16,17\n";
    assert(!replay_open(&r, "frames.csv"));
    assert(replay_next(&r, &f) == 1);
    assert(replay_next(&r, &f) == 1);
    assert(replay_next(&r, &f) == 1);
    assert(replay_next(&r, &f) == -1);
    replay_close(&r);
    fixture();
    clock_input = "frame,fast_ticks,normal_ticks\n0,10,4294967296\n";
    assert(!replay_open(&r, "frames.csv"));
    assert(replay_next(&r, &f) == -1);
    replay_close(&r);
    fixture();
    clock_input = NULL;
    assert(!replay_open(&r, "frames.csv"));
    assert(replay_next(&r, &f) == 1 && !f.has_actor_clocks);
    assert(replay_next(&r, &f) == 1);
    assert(replay_next(&r, &f) == 1);
    assert(!replay_next(&r, &f));
    replay_close(&r);
    fixture();
    clock_input = "frame,normal_ticks,fast_ticks\n";
    assert(replay_open(&r, "frames.csv") == -1);
    assert(!strcmp(cm_last_error()->file, "actor-clocks.csv"));
    fixture();
    clock_input = "frame,fast_ticks,normal_ticks\n0,1,2,3\n";
    assert(!replay_open(&r, "frames.csv"));
    assert(replay_next(&r, &f) == -1);
    assert(!strcmp(cm_last_error()->file, "actor-clocks.csv"));
    fixture();
    clock_input = "frame,fast_ticks,normal_ticks\n0,1,2\r\n";
    assert(!replay_open(&r, "frames.csv"));
    assert(replay_next(&r, &f) == -1);
    assert(!strcmp(cm_last_error()->file, "actor-clocks.csv"));
    fixture();
    /* Keep only the first trace row; its simulation need not have completed. */
    *strchr(strchr(strchr(trace, '\n') + 1, '\n') + 1, '\n') = '\0';
    strcat(trace, "\n");
    clock_input = "frame,fast_ticks,normal_ticks\n";
    assert(!replay_open(&r, "frames.csv"));
    assert(replay_next(&r, &f) == 1 && !f.has_actor_clocks);
    assert(!replay_next(&r, &f));
    replay_close(&r);
    f.values[CM_TRACE_BIOS_SHIFT] = 4; /* Original BDA Ctrl, not a Shift bit. */
    replay_input(&s, &f);
    assert(s.bios_shift == 4);
    return 0;
}
