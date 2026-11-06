#include "controls.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char *const action_names[CM_CONTROL_COUNT] = {
    "forward", "back", "left", "right", "fire", "jump", "use", "aim",
    "next", "prev", "menu", "map", "run", "crouch", "center",
    "fly_up", "fly_down", "gun_narrow", "gun_widen", "gun_center",
    "weapons", "powers", "items", "pause", "land", "slot1", "slot2",
    "slot3", "slot4", "slot5", "slot6", "slot7", "slot8", "slot9", "slot0"
};

static void bind(cm_controls *c, unsigned ctx, unsigned action, unsigned slot,
                  unsigned source, unsigned code)
{
    c->bindings[ctx][action][slot].source = (uint8_t)source;
    c->bindings[ctx][action][slot].code = (uint8_t)code;
}

void controls_init(cm_controls *c)
{
    unsigned ctx, i;
    memset(c, 0, sizeof *c);
    c->mouse_yaw = 10;
    c->mouse_pitch = 0.5;
    c->stick_yaw = 1024;
    c->stick_pitch = 8;
    c->pointer_x = 320;
    c->pointer_y = 240;
    c->selection_kind = 1;
    for (ctx = 0; ctx < CONTROLS_CONTEXTS; ++ctx) {
#define B(a,s,code) bind(c, ctx, CM_CONTROL_##a, 0, CONTROLS_##s, code)
#define B2(a,s,code) bind(c, ctx, CM_CONTROL_##a, 1, CONTROLS_##s, code)
#define B3(a,s,code) bind(c, ctx, CM_CONTROL_##a, 2, CONTROLS_##s, code)
        B(FORWARD, KEY, 0x11); B2(FORWARD, KEY, 0x48); B3(FORWARD, PAD, 11);
        B(BACK, KEY, 0x1f); B2(BACK, KEY, 0x50); B3(BACK, PAD, 12);
        B(LEFT, KEY, 0x1e); B2(LEFT, KEY, 0x4b); B3(LEFT, PAD, 13);
        B(RIGHT, KEY, 0x20); B2(RIGHT, KEY, 0x4d); B3(RIGHT, PAD, 14);
        B(FIRE, MOUSE, 0); B2(FIRE, TRIGGER, 1);
        B(JUMP, KEY, 0x39); B2(JUMP, PAD, 0);
        B(USE, KEY, 0x12); B2(USE, PAD, 2); B3(USE, KEY, 0x1c);
        B(AIM, MOUSE, 2); B2(AIM, TRIGGER, 0);
        B(NEXT, WHEEL, 1); B2(NEXT, PAD, 10); B3(NEXT, KEY, 0x1b);
        B(PREV, WHEEL, 0); B2(PREV, PAD, 9); B3(PREV, KEY, 0x1a);
        B(MENU, KEY, 1); B2(MENU, PAD, 6);
        B(MAP, KEY, 0x0f); B2(MAP, KEY, 0x32); B3(MAP, PAD, 4);
        B(RUN, KEY, 0x2a); B2(RUN, PAD, 7); B3(RUN, KEY, 0x36);
        B(CROUCH, KEY, 0x1d); B2(CROUCH, PAD, 1);
        B(CENTER, KEY, 0x13); B2(CENTER, PAD, 8);
        B(FLY_UP, KEY, 0x49); B(FLY_DOWN, KEY, 0x51);
        B(GUN_NARROW, KEY, 0x2c); B(GUN_WIDEN, KEY, 0x2e);
        B(GUN_CENTER, KEY, 0x2f);
        B(WEAPONS, KEY, 0x3b); B(POWERS, KEY, 0x3c);
        B(ITEMS, KEY, 0x3d); B2(ITEMS, KEY, 0x17); B3(ITEMS, PAD, 3);
        B(PAUSE, KEY, 0x19); B(LAND, KEY, 0x21);
        for (i = 0; i < 10; ++i) bind(c, ctx, CM_CONTROL_SLOT1 + i, 0, CONTROLS_KEY, 2 + i);
        if (ctx == CONTROLS_FOOT || ctx == CONTROLS_VEHICLE) {
            bind(c, ctx, CM_CONTROL_FORWARD, 2, CONTROLS_UNBOUND, 0);
            bind(c, ctx, CM_CONTROL_BACK, 2, CONTROLS_UNBOUND, 0);
            B2(FLY_UP, PAD, 11); B2(FLY_DOWN, PAD, 12);
            B2(LAND, PAD, 14);
        }
#undef B
#undef B2
#undef B3
    }
}

static char *trim(char *s)
{
    size_t n;
    while (isspace((unsigned char)*s)) ++s;
    n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
    return s;
}

static int parse_binding(char *s, controls_binding *b)
{
    char *colon = strchr(s, ':'), *end;
    unsigned long code, limit;
    if (!strcmp(s, "none")) { b->source = b->code = 0; return 0; }
    if (!colon) return -1;
    *colon++ = 0;
    if (!strcmp(s, "key")) { b->source = CONTROLS_KEY; limit = CM_KEY_COUNT - 1; }
    else if (!strcmp(s, "mouse")) { b->source = CONTROLS_MOUSE; limit = 7; }
    else if (!strcmp(s, "pad")) { b->source = CONTROLS_PAD; limit = 20; }
    else if (!strcmp(s, "trigger")) { b->source = CONTROLS_TRIGGER; limit = 1; }
    else if (!strcmp(s, "wheel")) { b->source = CONTROLS_WHEEL; limit = 1; }
    else return -1;
    if (!isdigit((unsigned char)*colon)) return -1;
    code = strtoul(colon, &end, 10);
    if (*end || code > limit) return -1;
    b->code = (uint8_t)code;
    return 0;
}

int controls_parse(cm_controls *c, const uint8_t *data, uint32_t size)
{
    static const char *const contexts[] = {"foot", "vehicle", "menu", "shop"};
    cm_controls next = *c;
    uint32_t at = 0;
    if (size > CONTROLS_CONFIG_MAX) return cm_fail("controls.cfg", 0, "configuration exceeds 8192 bytes");
    while (at < size) {
        char line[256], *s, *eq, *dot, *comment;
        unsigned n = 0, ctx, action, slot = 0;
        uint32_t offset = at;
        while (at < size && data[at] != '\n') {
            if (n + 1 == sizeof line || !data[at])
                return cm_fail("controls.cfg", at, "line too long or NUL in configuration");
            line[n++] = (char)data[at++];
        }
        if (at < size) ++at;
        line[n] = 0;
        comment = strchr(line, '#');
        if (comment) *comment = 0;
        s = trim(line);
        if (!*s) continue;
        eq = strchr(s, '=');
        if (!eq) goto bad;
        *eq++ = 0;
        s = trim(s); eq = trim(eq);
        dot = strchr(s, '.');
        if (!dot) {
            double value;
            char *end;
            value = strtod(eq, &end);
            if (end == eq || *end || !isfinite(value) || value < -100 || value > 100) goto bad;
            if (!strcmp(s, "mouse_yaw")) next.mouse_yaw = value;
            else if (!strcmp(s, "mouse_pitch")) next.mouse_pitch = value;
            else goto bad;
            continue;
        }
        *dot++ = 0;
        for (ctx = 0; ctx < CONTROLS_CONTEXTS; ++ctx) if (!strcmp(s, contexts[ctx])) break;
        for (action = 0; action < CM_CONTROL_COUNT; ++action) if (!strcmp(dot, action_names[action])) break;
        if (ctx == CONTROLS_CONTEXTS || action == CM_CONTROL_COUNT) goto bad;
        memset(next.bindings[ctx][action], 0, sizeof next.bindings[ctx][action]);
        while (*eq) {
            char *comma = strchr(eq, ',');
            if (comma) *comma++ = 0;
            if (slot == CONTROLS_BINDINGS || parse_binding(trim(eq), &next.bindings[ctx][action][slot++])) goto bad;
            if (!comma) break;
            eq = trim(comma);
            if (!*eq) goto bad;
        }
        if (!slot) goto bad;
        continue;
bad:
        return cm_fail("controls.cfg", offset, "expected context.action = source:decimal[,source:decimal] or mouse_yaw/mouse_pitch = number");
    }
    *c = next;
    return 0;
}
