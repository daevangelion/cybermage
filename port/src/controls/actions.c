#include "controls.h"
#include <math.h>
#include <string.h>

#define PI 3.14159265358979323846
#define DEAD_ZONE 0.18
#define BLOCKED 255

static int owned(uint8_t *owner, int held, unsigned context)
{
    if (!held) { *owner = 0; return 0; }
    if (!*owner) *owner = (uint8_t)(context + 1);
    return *owner == context + 1;
}

static void stick(int16_t ix, int16_t iy, double *x, double *y)
{
    double length, scale;
    *x = ix / 32768.0;
    *y = iy / 32768.0;
    length = sqrt(*x * *x + *y * *y);
    if (length <= DEAD_ZONE) { *x = *y = 0; return; }
    scale = ((length < 1 ? length : 1) - DEAD_ZONE) / ((1 - DEAD_ZONE) * length);
    *x *= scale;
    *y *= scale;
}

static int binding_down(const controls_binding *b, const uint8_t *keys,
                         const uint8_t *mouse, const uint8_t *pad, int wheel)
{
    switch (b->source) {
    case CONTROLS_KEY: return keys[b->code] != 0;
    case CONTROLS_MOUSE: return mouse[b->code] != 0;
    case CONTROLS_PAD: return pad[b->code] != 0;
    case CONTROLS_TRIGGER: return pad[21 + b->code] != 0;
    case CONTROLS_WHEEL: return b->code ? wheel < 0 : wheel > 0;
    default: return 0;
    }
}

static void output_key(cm_input *out, unsigned key, int held)
{
    if (held) out->held[key] = 1;
}

void controls_frame(cm_controls *c, controls_context context,
                     const cm_input *physical, cm_input *output)
{
    const cm_input *in = physical;
    uint8_t keys[CM_KEY_COUNT], mouse[8], pad[23];
    uint64_t actions = 0, edges;
    double mx = 0, my = 0, lx = 0, ly = 0, rx = 0, ry = 0, length;
    unsigned i, j;
    int gameplay = context == CONTROLS_FOOT || context == CONTROLS_VEHICLE;
    int touch_move;
    memset(output, 0, sizeof *output);
    output->modern = 1;
    output->quit = in->quit;
    output->mouse_x = in->mouse_x;
    output->mouse_y = in->mouse_y;
    if (context >= CONTROLS_CONTEXTS) context = CONTROLS_MENU;
    if (in->focus_lost) {
        memset(c->key_owner, BLOCKED, sizeof c->key_owner);
        memset(c->mouse_owner, BLOCKED, sizeof c->mouse_owner);
        memset(c->pad_owner, BLOCKED, sizeof c->pad_owner);
        memset(c->touch_owner, BLOCKED, sizeof c->touch_owner);
        memset(c->stick_owner, BLOCKED, sizeof c->stick_owner);
    }
    if (c->context != context || in->focus_lost) {
        c->yaw_fraction = c->pitch_fraction = 0;
        c->previous_actions = 0;
    }
    c->context = context;
    for (i = 0; i < CM_KEY_COUNT; ++i) {
        if (!in->focus_lost && in->pressed[i] && in->released[i]) c->key_owner[i] = 0;
        keys[i] = (uint8_t)owned(&c->key_owner[i], in->held[i] || in->pressed[i], context);
    }
    for (i = 0; i < 8; ++i) {
        unsigned bit = 1u << i;
        if (!in->focus_lost && (in->mouse_pressed & in->mouse_released & bit)) c->mouse_owner[i] = 0;
        mouse[i] = (uint8_t)owned(&c->mouse_owner[i], ((in->mouse_buttons | in->mouse_pressed) & bit) != 0, context);
    }
    for (i = 0; i < 23; ++i) {
        int held = !in->pad_disconnected && (i < 21 ?
            ((in->pad_buttons | in->pad_pressed) & (1u << i)) != 0 : in->pad_axes[i - 17] > 8192);
        if (!in->focus_lost && i < 21 && (in->pad_pressed & in->pad_released & (1u << i))) c->pad_owner[i] = 0;
        pad[i] = (uint8_t)owned(&c->pad_owner[i], held, context);
    }
    for (i = 0; i < CM_CONTROL_COUNT; ++i) {
        for (j = 0; j < CONTROLS_BINDINGS; ++j)
            if (binding_down(&c->bindings[context][i][j], keys, mouse, pad, in->mouse_wheel))
                actions |= UINT64_C(1) << i;
        if (i < 32 && owned(&c->touch_owner[i], ((in->touch_buttons | in->touch_pressed) & (1u << i)) != 0, context))
            actions |= UINT64_C(1) << i;
    }
    if (!in->pad_disconnected) {
        stick(in->pad_axes[0], in->pad_axes[1], &lx, &ly);
        stick(in->pad_axes[2], in->pad_axes[3], &rx, &ry);
    }
    if (!owned(&c->stick_owner[0], lx != 0 || ly != 0, context)) lx = ly = 0;
    if (!owned(&c->stick_owner[1], rx != 0 || ry != 0, context)) rx = ry = 0;
    touch_move = owned(&c->stick_owner[2], in->touch_move_x || in->touch_move_y, context);
    edges = actions & ~c->previous_actions;
    if (in->mouse_wheel) for (i = 0; i < CM_CONTROL_COUNT; ++i)
        for (j = 0; j < CONTROLS_BINDINGS; ++j)
            if (c->bindings[context][i][j].source == CONTROLS_WHEEL &&
                binding_down(&c->bindings[context][i][j], keys, mouse, pad, in->mouse_wheel))
                edges |= UINT64_C(1) << i;
    c->previous_actions = actions;
#define HELD(a) ((actions & (UINT64_C(1) << CM_CONTROL_##a)) != 0)
#define EDGE(a) ((edges & (UINT64_C(1) << CM_CONTROL_##a)) != 0)
    if (EDGE(WEAPONS)) c->selection_kind = 1;
    if (EDGE(POWERS)) c->selection_kind = 2;
    if (EDGE(ITEMS)) c->selection_kind = 3;
    output->select_kind = (uint8_t)c->selection_kind;
    output->select_open = (uint8_t)(EDGE(WEAPONS) || EDGE(POWERS) || EDGE(ITEMS));
    if (gameplay) {
        if (EDGE(MAP)) c->map_visible = !c->map_visible;
        mx = (double)HELD(RIGHT) - HELD(LEFT) + lx +
            (touch_move ? in->touch_move_x / 32768.0 : 0);
        my = (double)HELD(FORWARD) - HELD(BACK) - ly -
            (touch_move ? in->touch_move_y / 32768.0 : 0);
        if (in->focus_lost) mx = my = 0;
        length = sqrt(mx * mx + my * my);
        if (length > 0) {
            output->move_speed = (int32_t)((length < 1 ? length : 1) * (HELD(RUN) ? 768 : 640) + 0.5);
            output->move_angle = (uint16_t)(int32_t)(atan2(-mx, my) * (32768.0 / PI));
        }
        if (!in->focus_lost) {
            c->yaw_fraction -= (in->mouse_dx + (double)in->touch_look_dx) * c->mouse_yaw + rx * c->stick_yaw;
            c->pitch_fraction += (in->mouse_dy + (double)in->touch_look_dy) * c->mouse_pitch + ry * c->stick_pitch;
        }
        /* Bound conversion even for synthetic/platform extreme deltas. */
        if (c->yaw_fraction > 32767) c->yaw_fraction = 32767;
        if (c->yaw_fraction < -32767) c->yaw_fraction = -32767;
        if (c->pitch_fraction > 448) c->pitch_fraction = 448;
        if (c->pitch_fraction < -448) c->pitch_fraction = -448;
        output->look_yaw = (int32_t)c->yaw_fraction;
        output->look_pitch = (int32_t)c->pitch_fraction;
        c->yaw_fraction -= output->look_yaw;
        c->pitch_fraction -= output->look_pitch;
        output->jump = (uint8_t)HELD(JUMP);
        output->aim = (uint8_t)HELD(AIM);
        output->run = (uint8_t)HELD(RUN);
        output->fly = (int8_t)((int)HELD(FLY_UP) - HELD(FLY_DOWN));
        output->cycle = (int8_t)((int)EDGE(NEXT) - EDGE(PREV));
        for (i = CM_CONTROL_SLOT1; i <= CM_CONTROL_SLOT0; ++i)
            if (edges & (UINT64_C(1) << i)) output->select_slot = (uint8_t)(1 + i - CM_CONTROL_SLOT1);
        output_key(output, 0x39, HELD(FIRE));
        output_key(output, 0x1c, EDGE(USE));
        output_key(output, 0x52, EDGE(CROUCH));
        output_key(output, 0x13, EDGE(CENTER));
        output_key(output, 0x1e, HELD(GUN_NARROW));
        output_key(output, 0x20, HELD(GUN_WIDEN));
        output_key(output, 0x1f, EDGE(GUN_CENTER));
        output_key(output, 0x21, EDGE(LAND));
    } else {
        int old_x = c->pointer_x, old_y = c->pointer_y;
        output_key(output, 0x48, HELD(FORWARD) || ly < -0.5);
        output_key(output, 0x50, HELD(BACK) || ly > 0.5);
        output_key(output, 0x4b, HELD(LEFT) || lx < -0.5 || in->mouse_wheel > 0);
        output_key(output, 0x4d, HELD(RIGHT) || lx > 0.5 || in->mouse_wheel < 0);
        output_key(output, 0x1c, EDGE(USE) || EDGE(JUMP));
        output_key(output, 1, EDGE(AIM) || EDGE(CROUCH));
        for (i = CM_CONTROL_SLOT1; i <= CM_CONTROL_SLOT0; ++i)
            if (edges & (UINT64_C(1) << i))
                output->select_slot = (uint8_t)(1 + i - CM_CONTROL_SLOT1);
        for (i = 0; i < 8; ++i) if (mouse[i]) output->mouse_buttons |= 1u << i;
        if (HELD(FIRE)) output->mouse_buttons |= 1;
        if (HELD(AIM)) output->mouse_buttons |= 4;
        if (rx || ry) {
            c->pointer_x += (int)(rx * 12);
            c->pointer_y += (int)(ry * 12);
        } else if (in->mouse_dx || in->mouse_dy || in->mouse_pressed) {
            c->pointer_x = in->mouse_x;
            c->pointer_y = in->mouse_y;
        }
        if (c->pointer_x < 0) c->pointer_x = 0;
        if (c->pointer_x > 639) c->pointer_x = 639;
        if (c->pointer_y < 0) c->pointer_y = 0;
        if (c->pointer_y > 479) c->pointer_y = 479;
        output->mouse_x = c->pointer_x;
        output->mouse_y = c->pointer_y;
        output->mouse_dx = c->pointer_x - old_x;
        output->mouse_dy = c->pointer_y - old_y;
    }
    output_key(output, 1, EDGE(MENU) || EDGE(PAUSE));
    output_key(output, 0x0f, EDGE(MAP));
    output_key(output, 0x19, EDGE(PAUSE));
    /* Text entry and confirmation retain literal keys, with context ownership. */
    if (!gameplay) {
        static const uint8_t literal[] = {0x0e,0x15,0x31,0x1c,1};
        for (i = 0; i < sizeof literal; ++i) output_key(output, literal[i], keys[literal[i]]);
        for (i = 2; i <= 11; ++i) output_key(output, i, keys[i]);
    }
    for (i = 0; i < CM_KEY_COUNT; ++i) {
        output->pressed[i] = (uint8_t)(output->held[i] && !c->output_held[i]);
        output->released[i] = (uint8_t)(!output->held[i] && c->output_held[i]);
        c->output_held[i] = output->held[i];
    }
    output->mouse_pressed = output->mouse_buttons & ~c->output_mouse;
    output->mouse_released = c->output_mouse & ~output->mouse_buttons;
    c->output_mouse = output->mouse_buttons;
#undef HELD
#undef EDGE
}
