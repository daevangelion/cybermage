#include "events.h"

static int select_action(sim_state *s, world *w, const cm_input *in)
{
    events_state *e = s->events;
    events_selector *q;
    unsigned i, index = 0, kind;
    if (!e) return 0;
    if (!in->cycle && !in->select_slot && !in->select_open) {
        if (e->selector_kind) {
            if (events_keyboard(e, s, s->ai, w)) return -1;
            if (e->selector_kind && (in->mouse_pressed & 1)) e->selector_confirm = 1;
        }
        return 0;
    }
    kind = e->selector_kind && !in->select_open ? e->selector_kind : in->select_kind;
    if (kind < 1 || kind > 3) kind = 1;
    q=&e->selectors[kind-1];
    if (!e->selector_kind || in->select_open) {
        if (!e->selector_kind && !s->modern_aim) s->previous_pc_mode=s->pc_mode;
        if (events_make_selector(e, kind)) return -1;
        for (i = 0; i < q->count; ++i) {
            unsigned slot = (unsigned)e->selector_items[i];
            if ((kind == 2 && (int32_t)slot == e->spell) ||
                (kind == 1 && e->items[slot].subtype ==
                    (s->previous_pc_mode == 2 ? e->hh_weapon : e->weapon))) index = i;
        }
        q->index = index;
    }
    if (!q->count) {
        e->selector_kind = 0;
        s->pc_mode = s->previous_pc_mode;
        return 0;
    }
    if (in->select_open) { s->pc_mode = 3; s->modern_aim = 0; return 0; }
    if (in->select_slot) {
        unsigned key = in->select_slot - 1;
        if (key >= EVENTS_SELECTOR_MAX || e->selector_keys[key] < 0) return 0;
        q->index = (unsigned)e->selector_keys[key];
    } else if (in->cycle > 0) q->index = (q->index + 1) % q->count;
    else q->index = (q->index + q->count - 1) % q->count;
    if (kind == 3 && !in->select_slot) { s->pc_mode = 3; return 0; }
    s->kb_scan_status[0x1c] = 2;
    if (events_keyboard(e,s,s->ai,w) || events_process_selector(e, s, w)) return -1;
    if (s->modern_aim) {
        s->previous_pc_mode = s->pc_mode;
        s->pc_mode = 3;
    }
    return 0;
}

/* Enhanced transport only. Classic HandleKeyboardEvents remains unchanged. */
int sim_modern_input(sim_state *s, world *w, const cm_input *in)
{
    int64_t pitch;
    int selector;
    world_object *o = &w->objects[w->player_object];
    if (select_action(s, w, in)) return -1;
    selector = s->events && s->events->selector_kind;
    if (!in->aim && s->modern_aim) {
        if (s->pc_mode == 3 && !selector) s->pc_mode = s->previous_pc_mode;
        s->modern_aim = 0;
    }
    if (in->aim && !selector && !s->modern_aim &&
        (s->pc_mode == 1 || s->pc_mode == 2 || s->pc_mode == 5)) {
        s->previous_pc_mode = s->pc_mode;
        s->pc_mode = 3;
        s->modern_aim = 1;
    }
    s->movement_angle = in->move_angle;
    s->movement_request = s->modern_aim || selector ? 0 : in->move_speed;
    if (s->modern_aim || selector) s->unk_144ae8 = s->unk_144af8 = 0;
    s->yaw_request = 0;
    if (s->movement_request < 0) s->movement_request = 0;
    if (s->movement_request > 768) s->movement_request = 768;
    /* Classic request values accelerate to walking/running momentum caps.
     * Modern analog magnitude scales that cap, not the oversized request. */
    s->movement_request = s->movement_request * (in->run ? 96 : 48) /
                         (in->run ? 768 : 640);
    if (!selector) {
        o->heading = (uint16_t)((uint32_t)o->heading + (uint32_t)in->look_yaw);
        pitch = (int64_t)w->camera.pitch + in->look_pitch;
        w->camera.pitch = pitch < -224 ? -224 : pitch > 224 ? 224 : (int32_t)pitch;
    }
    if (in->pressed[0x13]) w->camera.pitch = 0;
    if (in->pressed[0x52])
        s->unk_144b3c = s->unk_144b3c == -1 ||
            s->unk_144b34 == (w->unk_5E7A8 ? -0x3000 : -0x4800) ? 1 : -1;
    if (in->pressed[0x21]) {
        if (s->pc_mode == 7) s->kb_scan_status[0x0f] = 2;
        else if (s->flight_mode) events_fly_landing(s, o);
    }
    s->kb_scan_status[0x11] = in->fly > 0;
    s->kb_scan_status[0x2d] = in->fly < 0;
    return !selector && !s->modern_aim && in->jump;
}
