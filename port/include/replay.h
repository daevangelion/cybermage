#ifndef CM_REPLAY_H
#define CM_REPLAY_H
#include "sim.h"

#define CM_TRACE_RANGES_MAX 8
#define CM_TRACE_COLUMNS 23

typedef struct { unsigned relative; uint32_t address, length; } cm_trace_range;
enum { CM_DRAW_NORMAL, CM_DRAW_SAVE, CM_DRAW_LOAD, CM_DRAW_DEATH };
typedef struct {
    uint32_t frame, draw, phase, site, ticks;
} cm_replay_draw;
typedef struct {
    uint32_t frame, ticks, action, slot;
    uint8_t name[24];
} cm_replay_ui;
typedef struct {
    int32_t x, y, mickey_x, mickey_y;
    uint16_t buttons, press_count[2];
} cm_replay_mouse_value;
typedef struct {
    int64_t frame;
    uint32_t ticks, caller;
    cm_replay_mouse_value value;
} cm_replay_mouse;
typedef struct {
    cm_file file, actor_clocks, draw_clocks, ui_actions, mouse_inputs, death_inputs;
    const char *path;
    char actor_clock_path[1024];
    char draw_clock_path[1024], ui_action_path[1024];
    char mouse_input_path[1024];
    char death_input_path[1024];
    uint32_t next, frame;
    uint32_t actor_clock_next;
    uint32_t draw_clock_next, ui_action_next;
    uint32_t mouse_input_next;
    uint32_t death_input_next;
    uint64_t mouse_sequence;
    cm_replay_draw draw_pending;
    cm_replay_ui ui_pending;
    cm_replay_mouse mouse_pending;
    cm_replay_mouse_value mouse_cached;
    int has_mouse_pending, has_mouse_cached;
    uint32_t draw_frame, draw_index, draw_phase;
    int has_draw_pending, has_ui_pending, draw_active, has_draw_frame, draw_reads;
    unsigned range_count;
    uint16_t bios_seed;
    int has_bios_seed;
    cm_trace_range ranges[CM_TRACE_RANGES_MAX];
} cm_replay;
typedef struct {
    int64_t values[CM_TRACE_COLUMNS];
    uint8_t keys[SIM_SCAN_COUNT], input_keys[SIM_SCAN_COUNT];
    const char *range_hex[CM_TRACE_RANGES_MAX];
    /* Only has_actor_clocks makes these measured inputs. Otherwise both are
     * the legacy frame-entry approximation, not actor-clock parity evidence. */
    uint32_t normal_actor_ticks, fast_actor_ticks;
    int has_actor_clocks;
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
/* Exact read-only draw inputs. begin returns1, or0 for the unpaired terminal
 * frame. No frame-tick replacement for missing measured clocks. */
int replay_draw_begin(cm_replay *r, uint32_t frame, unsigned phase);
int replay_draw_clock(cm_replay *r, uint32_t site, uint32_t *ticks);
int replay_draw_end(cm_replay *r);
/* Completed menu inputs before this frame's simulation:1 row,0 none,-1 bad. */
int replay_ui_next(cm_replay *r, uint32_t frame, cm_replay_ui *action);
/* ReadStatus returns are cached until the next real poll, like DOS.
 * required comes from actual CMDATA input-device selection, never audits. */
int replay_mouse_input(cm_replay *r, uint32_t frame, int required, cm_input *input);
int replay_mouse_poll(cm_replay *r, uint32_t frame, cm_input *input);
/* PCDeath's actual timer loads and raw Escape byte, not its RNG/branch outputs. */
int replay_death_read(cm_replay *r, uint32_t frame, uint32_t site, uint32_t *value);
void replay_input(sim_state *s, const cm_replay_frame *f);
const char *replay_compare(const world *w, const cm_replay_frame *f,
                            int64_t *expected, int64_t *actual);
#endif
