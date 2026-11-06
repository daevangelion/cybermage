#include "replay.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static char trace[4096], clocks[512], draw_data[2048], ui_data[512], mouse_data[4096], death_data[512];
static const char *clock_input, *draw_input, *ui_input, *mouse_input, *death_input;

int platform_file_exists(const char *path)
{
    if (strstr(path, "actor-clocks.csv")) return clock_input != NULL;
    if (strstr(path, "draw-clocks.csv")) return draw_input != NULL;
    if (strstr(path, "ui-actions.csv")) return ui_input != NULL;
    if (strstr(path, "mouse-input.csv")) return mouse_input != NULL;
    if (strstr(path, "death-inputs.csv")) return death_input != NULL;
    return 0;
}

int platform_file_read(cm_file *file, const char *path)
{
    if (strstr(path, "actor-clocks.csv")) {
        if (!clock_input) return -1;
        memcpy(clocks, clock_input, strlen(clock_input) + 1);
        file->data = (uint8_t *)clocks;
        file->size = (uint32_t)strlen(clocks);
    } else if (strstr(path, "draw-clocks.csv")) {
        if (!draw_input) return -1;
        memcpy(draw_data, draw_input, strlen(draw_input) + 1);
        file->data = (uint8_t *)draw_data;
        file->size = (uint32_t)strlen(draw_data);
    } else if (strstr(path, "ui-actions.csv")) {
        if (!ui_input) return -1;
        memcpy(ui_data, ui_input, strlen(ui_input) + 1);
        file->data = (uint8_t *)ui_data;
        file->size = (uint32_t)strlen(ui_data);
    } else if (strstr(path, "mouse-input.csv")) {
        if (!mouse_input) return -1;
        memcpy(mouse_data, mouse_input, strlen(mouse_input) + 1);
        file->data = (uint8_t *)mouse_data;
        file->size = (uint32_t)strlen(mouse_data);
    } else if (strstr(path, "death-inputs.csv")) {
        if (!death_input) return -1;
        memcpy(death_data, death_input, strlen(death_input) + 1);
        file->data = (uint8_t *)death_data;
        file->size = (uint32_t)strlen(death_data);
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
    draw_input = ui_input = mouse_input = death_input = NULL;
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

static void draw_controls(void)
{
    static const char ui_header[] = "frame,ticks,action,slot,name_hex24\n";
    cm_replay r;
    cm_replay_frame frame;
    cm_replay_ui action;
    uint32_t ticks;
    fixture(); clock_input = NULL;
    draw_input = "frame,draw,phase,pc,ticks\n"
        "0,0,1,16391,10\n0,0,1,19377,11\n0,0,1,193a4,12\n"
        "0,1,0,19377,13\n1,0,0,19377,14\n";
    ui_input = "frame,ticks,action,slot,name_hex24\n"
        "0,10,1,0,414200000000000000000000000000000000000000000000\n"
        "0,11,2,2,000000000000000000000000000000000000000000000000\n"
        "0,12,3,10,000000000000000000000000000000000000000000000000\n";
    assert(!replay_open(&r, "frames.csv") && replay_next(&r, &frame) == 1);
    assert(replay_ui_next(&r, 0, &action) == 1 && action.slot == 0 &&
           action.ticks == 10 && !strcmp((char *)action.name, "AB"));
    assert(replay_ui_next(&r, 0, &action) == 1 && action.action == 2 && action.slot == 2);
    assert(replay_ui_next(&r, 0, &action) == 1 && action.action == 3 && action.slot == 10);
    assert(!replay_ui_next(&r, 0, &action));
    assert(replay_draw_begin(&r, 0, CM_DRAW_NORMAL) == 1);
    assert(!replay_draw_end(&r)); /* No clock read: no observed ordinal consumed. */
    assert(replay_draw_begin(&r, 0, CM_DRAW_SAVE) == 1);
    assert(!replay_draw_clock(&r, 0x16391, &ticks) && ticks == 10);
    assert(!replay_draw_clock(&r, 0x19377, &ticks) && ticks == 11);
    assert(!replay_draw_clock(&r, 0x193a4, &ticks) && ticks == 12);
    assert(!replay_draw_end(&r));
    assert(replay_draw_begin(&r, 0, CM_DRAW_LOAD) == 1);
    assert(!replay_draw_end(&r));
    assert(replay_draw_begin(&r, 0, CM_DRAW_NORMAL) == 1);
    assert(!replay_draw_clock(&r, 0x19377, &ticks) && ticks == 13);
    assert(!replay_draw_end(&r));
    assert(replay_next(&r, &frame) == 1);
    assert(replay_draw_begin(&r, 1, CM_DRAW_NORMAL) == 1);
    assert(!replay_draw_clock(&r, 0x19377, &ticks) && ticks == 14);
    assert(!replay_draw_end(&r));
    assert(replay_next(&r, &frame) == 1);
    assert(!replay_draw_begin(&r, 2, CM_DRAW_NORMAL));
    assert(!replay_next(&r, &frame));
    replay_close(&r);
    fixture();
    draw_input = "frame,draw,phase,pc,ticks\n0,0,0,19377,10\n0,0,0,193a4,11\n";
    ui_input = ui_header;
    assert(!replay_open(&r, "frames.csv") && replay_next(&r, &frame) == 1);
    assert(replay_draw_begin(&r, 0, CM_DRAW_NORMAL) == 1);
    assert(!replay_draw_clock(&r, 0x19377, &ticks));
    assert(replay_draw_end(&r) == -1); /* A real conditional read was omitted. */
    replay_close(&r);
    fixture();
    draw_input = "frame,draw,phase,pc,ticks\n0,0,0,19377,10\n";
    ui_input = ui_header;
    assert(!replay_open(&r, "frames.csv") && replay_next(&r, &frame) == 1);
    assert(replay_draw_begin(&r, 0, CM_DRAW_SAVE) == 1);
    assert(replay_draw_clock(&r, 0x19377, &ticks) == -1);
    assert(strstr(cm_last_error()->message, "frame=0"));
    replay_close(&r);
    fixture();
    ui_input = "frame,ticks,action,slot,name_hex24\n"
        "0,10,1,11,000000000000000000000000000000000000000000000000\n";
    assert(!replay_open(&r, "frames.csv") && replay_next(&r, &frame) == 1);
    assert(replay_ui_next(&r, 0, &action) == -1);
    replay_close(&r);
    fixture();
    ui_input = "frame,ticks,action,slot,name_hex24\n"
        "0,10,4,0,000000000000000000000000000000000000000000000000\n";
    assert(!replay_open(&r, "frames.csv") && replay_next(&r, &frame) == 1);
    assert(replay_ui_next(&r, 0, &action) == -1);
    replay_close(&r);
    fixture();
    assert(!replay_open(&r, "frames.csv") && replay_next(&r, &frame) == 1);
    /* Old data is valid for consumers which perform no clock-dependent draw. */
    assert(replay_draw_begin(&r, 0, CM_DRAW_NORMAL) == 1);
    assert(!replay_draw_end(&r));
    assert(replay_next(&r, &frame) == 1);
    assert(replay_draw_begin(&r, 1, CM_DRAW_SAVE) == 1);
    /* The first actual clock use must fail, identifying both frame and file. */
    assert(replay_draw_clock(&r, 0x19377, &ticks) == -1);
    assert(!strcmp(cm_last_error()->file, "draw-clocks.csv"));
    assert(strstr(cm_last_error()->message, "frame=1"));
    assert(strstr(cm_last_error()->message, "missing measured draw clock file"));
    replay_close(&r);
}

static void mouse_controls(void)
{
    static const char header[] = "sequence,frame,ticks,caller,x,y,buttons,mickey_x,mickey_y,press_left,press_right,in_device,mouse_present,joystick_present\n";
    cm_replay r;
    cm_replay_frame frame;
    cm_input input = {0};
    fixture(); clock_input = NULL;
    mouse_input = "sequence,frame,ticks,caller,x,y,buttons,mickey_x,mickey_y,press_left,press_right,in_device,mouse_present,joystick_present\n"
        "0,-1,10,54196,320,100,1,0,0,1,0,0,0,1\n"
        "1,0,11,4e1ef,180,50,0,-2,-3,0,0,2,0,65535\n"
        "2,0,12,57f38,320,120,2,140,70,0,1,2,0,65535\n"
        "3,1,13,57f38,200,110,1,-16,-8,2,0,0,0,0\n";
    input.mouse_present = 77; input.joystick_present = 88;
    assert(!replay_open(&r, "frames.csv") && replay_next(&r, &frame) == 1);
    assert(!replay_mouse_input(&r, 0, 1, &input));
    assert(input.mouse_x == 180 && input.mouse_y == 100);
    assert(input.mouse_mickey_x == -2 && input.mouse_mickey_y == -3);
    assert(!input.mouse_buttons && !input.mouse_press_count[0]); /* Not ORed UI clicks. */
    assert(input.mouse_present == 77 && input.joystick_present == 88); /* Audits never installed. */
    assert(!replay_mouse_poll(&r, 0, &input));
    assert(input.mouse_x == 320 && input.mouse_y == 240 && input.mouse_buttons == 4);
    assert(input.mouse_mickey_x == 140 && input.mouse_mickey_y == 70);
    assert(input.mouse_press_count[1] == 1);
    assert(replay_next(&r, &frame) == 1);
    assert(!replay_mouse_input(&r, 1, 1, &input));
    assert(input.mouse_mickey_x == 140 && input.mouse_mickey_y == 70); /* Previous draw's poll. */
    assert(!replay_mouse_poll(&r, 1, &input));
    assert(input.mouse_buttons == 1 && input.mouse_press_count[0] == 2);
    assert(input.mouse_mickey_x == -16 && input.mouse_mickey_y == -8);
    assert(replay_next(&r, &frame) == 1 && !replay_next(&r, &frame));
    replay_close(&r);
    fixture();
    assert(!replay_open(&r, "frames.csv") && replay_next(&r, &frame) == 1);
    assert(!replay_mouse_input(&r, 0, 0, &input));
    assert(replay_mouse_input(&r, 0, 1, &input) == -1);
    assert(!strcmp(cm_last_error()->file, "mouse-input.csv"));
    replay_close(&r);
    fixture();
    mouse_input = header;
    assert(!replay_open(&r, "frames.csv") && replay_next(&r, &frame) == 1);
    assert(replay_mouse_input(&r, 0, 1, &input) == -1);
    replay_close(&r);
    fixture();
    mouse_input = "sequence,frame,ticks,caller,x,y,buttons,mickey_x,mickey_y,press_left,press_right,in_device,mouse_present,joystick_present\n"
        "0,-1,10,54196,320,100,0,32768,65535,0,0,1,1,0\n";
    assert(!replay_open(&r, "frames.csv") && replay_next(&r, &frame) == 1);
    assert(!replay_mouse_input(&r, 0, 1, &input));
    assert(input.mouse_mickey_x == INT16_MIN && input.mouse_mickey_y == -1);
    assert(replay_mouse_poll(&r, 0, &input) == -1);
    replay_close(&r);
}

static void death_controls(void)
{
    cm_replay r;
    cm_replay_frame frame;
    uint32_t value;
    fixture(); clock_input = NULL;
    death_input = "frame,pc,value\n0,153fd,10\n0,15403,11\n0,1543f,72\n0,1555a,3\n";
    assert(!replay_open(&r, "frames.csv") && replay_next(&r, &frame) == 1);
    assert(!replay_death_read(&r, 0, 0x153fd, &value) && value == 10);
    assert(!replay_death_read(&r, 0, 0x15403, &value) && value == 11);
    assert(!replay_death_read(&r, 0, 0x1543f, &value) && value == 72);
    assert(!replay_death_read(&r, 0, 0x1555a, &value) && value == 3);
    replay_close(&r);
    fixture();
    assert(!replay_open(&r, "frames.csv") && replay_next(&r, &frame) == 1);
    assert(replay_death_read(&r, 0, 0x153fd, &value) == -1);
    assert(!strcmp(cm_last_error()->file, "death-inputs.csv"));
    replay_close(&r);
    fixture();
    death_input = "frame,pc,value\n0,153fd,10\n";
    assert(!replay_open(&r, "frames.csv") && replay_next(&r, &frame) == 1);
    assert(replay_death_read(&r, 0, 0x15403, &value) == -1);
    replay_close(&r);
    fixture();
    death_input = "frame,pc,value\n0,1555a,256\n";
    assert(!replay_open(&r, "frames.csv") && replay_next(&r, &frame) == 1);
    assert(replay_death_read(&r, 0, 0x1555a, &value) == -1);
    replay_close(&r);
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
    draw_controls();
    mouse_controls();
    death_controls();
    return 0;
}
