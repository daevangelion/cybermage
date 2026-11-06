#ifndef CM_REPLAY_H
#define CM_REPLAY_H
#include "sim.h"

#define CM_TRACE_RANGES_MAX 8
#define CM_TRACE_COLUMNS 23

typedef struct { unsigned relative; uint32_t address, length; } cm_trace_range;
typedef struct {
    cm_file file;
    const char *path;
    uint32_t next, frame;
    unsigned range_count;
    cm_trace_range ranges[CM_TRACE_RANGES_MAX];
} cm_replay;
typedef struct {
    int64_t values[CM_TRACE_COLUMNS];
    uint8_t keys[SIM_SCAN_COUNT], input_keys[SIM_SCAN_COUNT];
    const char *range_hex[CM_TRACE_RANGES_MAX];
} cm_replay_frame;

enum {
    CM_TRACE_FRAME, CM_TRACE_TICKS, CM_TRACE_PREVIOUS, CM_TRACE_DELTA,
    CM_TRACE_LIMIT_TIME, CM_TRACE_LIMIT_FLAG, CM_TRACE_OBJECT, CM_TRACE_PERSON,
    CM_TRACE_X, CM_TRACE_Y, CM_TRACE_Z, CM_TRACE_HEADING, CM_TRACE_PITCH,
    CM_TRACE_BODY, CM_TRACE_KB_LATCH, CM_TRACE_BIOS_SHIFT, CM_TRACE_OBJECT_Z,
    CM_TRACE_CAMERA_U, CM_TRACE_CAMERA_V, CM_TRACE_CAMERA_Z, CM_TRACE_CAMERA_ANGLE,
    CM_TRACE_HORIZON, CM_TRACE_SCENE
};

int replay_open(cm_replay *r, const char *path);
void replay_close(cm_replay *r);
/* 1 record, 0 EOF, -1 malformed. Ranges borrow ASCII-hex spans in file. */
int replay_next(cm_replay *r, cm_replay_frame *frame);
void replay_input(sim_state *s, const cm_replay_frame *f);
const char *replay_compare(const world *w, const cm_replay_frame *f,
                            int64_t *expected, int64_t *actual);
#endif
