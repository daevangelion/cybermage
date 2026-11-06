#ifndef CM_CONTROLS_H
#define CM_CONTROLS_H

#include "platform.h"

#define CONTROLS_BINDINGS 3
#define CONTROLS_CONFIG_MAX 8192

typedef enum {
    CONTROLS_FOOT, CONTROLS_VEHICLE, CONTROLS_MENU, CONTROLS_SHOP,
    CONTROLS_CONTEXTS
} controls_context;

typedef enum {
    CM_CONTROL_FORWARD, CM_CONTROL_BACK, CM_CONTROL_LEFT, CM_CONTROL_RIGHT,
    CM_CONTROL_FIRE, CM_CONTROL_JUMP, CM_CONTROL_USE, CM_CONTROL_AIM,
    CM_CONTROL_NEXT, CM_CONTROL_PREV, CM_CONTROL_MENU, CM_CONTROL_MAP,
    CM_CONTROL_RUN, CM_CONTROL_CROUCH, CM_CONTROL_CENTER,
    CM_CONTROL_FLY_UP, CM_CONTROL_FLY_DOWN, CM_CONTROL_GUN_NARROW,
    CM_CONTROL_GUN_WIDEN, CM_CONTROL_GUN_CENTER, CM_CONTROL_WEAPONS,
    CM_CONTROL_POWERS, CM_CONTROL_ITEMS, CM_CONTROL_PAUSE, CM_CONTROL_LAND,
    CM_CONTROL_SLOT1, CM_CONTROL_SLOT2, CM_CONTROL_SLOT3, CM_CONTROL_SLOT4,
    CM_CONTROL_SLOT5, CM_CONTROL_SLOT6, CM_CONTROL_SLOT7, CM_CONTROL_SLOT8,
    CM_CONTROL_SLOT9, CM_CONTROL_SLOT0, CM_CONTROL_COUNT
} controls_action;

enum {
    CONTROLS_UNBOUND, CONTROLS_KEY, CONTROLS_MOUSE, CONTROLS_PAD,
    CONTROLS_TRIGGER, CONTROLS_WHEEL
};

typedef struct { uint8_t source, code; } controls_binding;
typedef struct {
    controls_binding bindings[CONTROLS_CONTEXTS][CM_CONTROL_COUNT][CONTROLS_BINDINGS];
    uint8_t key_owner[CM_KEY_COUNT], mouse_owner[8], pad_owner[23];
    uint8_t touch_owner[32], stick_owner[3];
    uint8_t output_held[CM_KEY_COUNT];
    unsigned output_mouse;
    uint64_t previous_actions;
    double yaw_fraction, pitch_fraction, mouse_yaw, mouse_pitch;
    double stick_yaw, stick_pitch;
    int pointer_x, pointer_y;
    unsigned selection_kind;
    int map_visible;
    controls_context context;
} cm_controls;

void controls_init(cm_controls *c);
int controls_parse(cm_controls *c, const uint8_t *data, uint32_t size);
int controls_load(cm_controls *c, const char *path);
void controls_frame(cm_controls *c, controls_context context,
                     const cm_input *physical, cm_input *output);
struct world;
int controls_draw_map(const cm_controls *c, const struct world *w,
                       uint8_t pixels[CM_WIDTH * CM_HEIGHT]);

#endif
