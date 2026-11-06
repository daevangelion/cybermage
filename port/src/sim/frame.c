#include "sim_internal.h"
#include "ai.h"

#define MOVE_FAST 768
#define MOVE_WALK 640
#define RUN_THRESHOLD 704
#define MOVE_ACCEL 16
#define WALK_ACCEL_MAX 48
#define RUN_ACCEL_MAX 96
#define PITCH_STEP 32
#define PITCH_LIMIT 224

static int down(const sim_state *s, unsigned scan, unsigned bit)
{
    return scan < SIM_SCAN_COUNT && (s->kb_scan_status[scan] & bit) != 0;
}

/* Xox::HandleKeyboardEvents 012DD4..013EBD; keyboard motion is sampled
 * every render pass, including passes with zero elapsed timer ticks. */
static int keyboard(sim_state *s, world *w)
{
    int side, forward, back, right, left, jump;
    uint8_t *keys = s->kb_scan_status;
    unsigned shift = (keys[0x2a] & 2) | (s->bios_shift & 2);
    unsigned rshift = (keys[0x36] & 2) | (s->bios_shift & 1);
    keys[0x2a] = shift != 0;
    keys[0x36] = rshift != 0;
    if (keys[0x36]) { keys[0x2a] |= keys[0x36]; keys[0x36] = 0; }
    if (keys[0x2a]) {
        if (!s->shift_lock) { keys[0x2a] |= 2; s->shift_lock = 1; }
    } else s->shift_lock = 0;
    if (down(s, s->kb_look_down, s->kb_look_down_bit) && w->camera.pitch < PITCH_LIMIT)
        w->camera.pitch += PITCH_STEP;
    if (down(s, s->kb_look_up, s->kb_look_up_bit) && w->camera.pitch > -PITCH_LIMIT)
        w->camera.pitch -= PITCH_STEP;
    if (down(s, s->kb_look_center, s->kb_look_center_bit * 2)) {
        keys[s->kb_look_center] = 0;
        w->camera.pitch = 0;
    }
    s->movement_request = s->yaw_request = 0;
    side = down(s, s->kb_side_toggle, s->kb_side_toggle_bit);
    forward = down(s, 0x48, 5);
    back = down(s, 0x50, 5);
    right = down(s, 0x4d, 5);
    left = down(s, 0x4b, 5);
    if (forward) { s->movement_angle = 0; s->movement_request = side ? MOVE_WALK : MOVE_FAST; }
    else if (down(s, 0x4c, 1)) { s->movement_angle = 0; s->movement_request = MOVE_WALK; }
    else if (back) { s->movement_angle = 0; s->movement_request = -MOVE_WALK; }
    if (right) {
        if (side) {
            s->movement_angle = forward ? 0xe000 : (back ? 0x2000 : 0xc000);
            if (!s->movement_request || s->movement_request > MOVE_WALK) s->movement_request = MOVE_WALK;
        } else {
            if (s->key_rot_rate > -MOVE_ACCEL) s->key_rot_rate = -MOVE_ACCEL;
            s->yaw_request = s->key_rot_rate;
            s->key_rot_rate -= s->key_rot_rate > -64 ? 16 : 32;
            if (s->key_rot_rate < -SIM_YAW_MAX) s->key_rot_rate = -SIM_YAW_MAX;
        }
    } else if (left) {
        if (side) {
            s->movement_angle = forward ? 0x2000 : (back ? 0xe000 : 0x4000);
            if (!s->movement_request || s->movement_request > MOVE_WALK) s->movement_request = MOVE_WALK;
        } else {
            if (s->key_rot_rate < MOVE_ACCEL) s->key_rot_rate = MOVE_ACCEL;
            s->yaw_request = s->key_rot_rate;
            s->key_rot_rate += s->key_rot_rate < 64 ? 16 : 32;
            if (s->key_rot_rate > SIM_YAW_MAX) s->key_rot_rate = SIM_YAW_MAX;
        }
    } else s->key_rot_rate = 0;
    if (down(s, 0x47, 1)) { s->movement_angle = 0x2000; s->movement_request = MOVE_WALK; }
    if (down(s, 0x4f, 1)) { s->movement_angle = 0xe000; s->movement_request = MOVE_WALK; }
    if (down(s, s->kb_side_left, s->kb_side_left_bit)) {
        s->movement_angle = forward ? 0x2000 : (back ? 0xe000 : 0x4000);
        if (!s->movement_request || s->movement_request > MOVE_WALK) s->movement_request = MOVE_WALK;
    }
    if (down(s, s->kb_side_right, s->kb_side_right_bit)) {
        s->movement_angle = forward ? 0xe000 : (back ? 0x2000 : 0xc000);
        if (!s->movement_request || s->movement_request > MOVE_WALK) s->movement_request = MOVE_WALK;
    }
    if (down(s, s->kb_crouch, s->kb_crouch_bit * 2)) {
        keys[s->kb_crouch] = 0;
        s->unk_144b3c = s->unk_144b3c == -1 ||
            s->unk_144b34 == (w->unk_5E7A8 ? -0x3000 : -0x4800) ? 1 : -1;
    }
    jump = down(s, s->kb_jump, s->kb_jump_bit | (s->kb_jump_bit * 2));
    if (jump) keys[s->kb_jump] &= (uint8_t)~(s->kb_jump_bit * 2);
    return jump;
}

static void acceleration(sim_state *s, int maximum)
{
    int32_t request = s->movement_request, previous = s->unk_144ae8;
    if (s->unk_144b1c) {
        s->unk_144b00 = MOVE_ACCEL;
        s->unk_144ae8 = s->unk_144b24;
    } else if (!request) s->unk_144b00 = MOVE_ACCEL;
    else if (request > 0 && request > previous) {
        if (request > s->unk_144b00 || request / 2 > previous)
            s->movement_request = s->unk_144b00;
        if (s->movement_request > previous) {
            s->unk_144b00 += MOVE_ACCEL;
            if (s->unk_144b00 > maximum) s->unk_144b00 = maximum;
        }
        s->unk_144ae8 = s->movement_request;
        if (!s->unk_144afc) s->unk_144afc = 1;
    } else if (request < 0 && request < previous) {
        if (request < -s->unk_144b00 || request / 2 < previous)
            s->movement_request = -s->unk_144b00;
        if (s->movement_request < previous) {
            s->unk_144b00 += MOVE_ACCEL;
            if (s->unk_144b00 > maximum) s->unk_144b00 = maximum;
        }
        s->unk_144ae8 = s->movement_request;
        if (!s->unk_144afc) s->unk_144afc = 1;
    }
    if (!s->unk_144b1c) s->unk_144af0 = s->yaw_request;
}

/* HandleNextFrame 01C331..01C3E9: requested momentum decays only after
 * current speed reaches it. With no request, momentum is not reset. */
static void momentum(sim_state *s)
{
    int32_t wanted = s->unk_144ae8, ticks = s->frame_ticks;
    if (wanted > 0) {
        s->unk_144af8 += (wanted * ticks) >> 2;
        if (s->unk_144af8 >= wanted) {
            s->unk_144af8 = wanted;
            s->unk_144ae8 -= ((wanted * ticks) >> 4) + ticks;
            if (s->unk_144ae8 < 0) s->unk_144ae8 = 0;
        }
        s->unk_144aec = s->unk_144af8;
    } else if (wanted < 0) {
        s->unk_144af8 += (wanted * ticks) >> 2;
        if (s->unk_144af8 <= wanted) {
            s->unk_144af8 = wanted;
            s->unk_144ae8 -= ((wanted * ticks) >> 4) - ticks;
            if (s->unk_144ae8 > 0) s->unk_144ae8 = 0;
        }
        s->unk_144aec = s->unk_144af8;
    }
}

static void jump_step(sim_state *s)
{
    /* Retail jumptable 0963C0, indexed by one-based +144B1C. */
    static const int32_t jumptable[SIM_JUMP_STEPS] = {
        8192,6144,4096,3072,2048,1024,0,-1024,
        -2048,-3072,-4096,-6144,-8192,-65536,-65536,-65536
    };
    unsigned index = (unsigned)s->unk_144b1c - 1;
    if (index >= SIM_JUMP_STEPS) index = SIM_JUMP_STEPS - 1;
    if (!s->unk_144b28) {
        s->unk_144b28 = jumptable[index];
        ++s->unk_144b1c;
    } else {
        s->unk_144b28 += jumptable[index] * s->frame_ticks / 2;
        if (s->bp_limit_flag) ++s->unk_144b1c;
    }
    if (s->unk_144b28 <= 0) {
        s->unk_144b28 = s->unk_144b1c = s->unk_144af4 = 0;
        s->unk_144b30 = s->unk_144b3c = 1;
    } else s->unk_144af4 = s->unk_144b28;
}

static void body(sim_state *s, const world *w, const world_object *o)
{
    int32_t step = w->unk_5E7A8 ? 0x4cc : 0x800;
    if (s->unk_144b3c == 1) {
        s->unk_144b34 += step;
        if (s->unk_144b34 >= 0) { s->unk_144b34 = 0; s->unk_144b3c = 0; }
    } else if (s->unk_144b3c == -1) {
        int32_t minimum = w->unk_5E7A8 ? -0x3000 : -0x4800;
        s->unk_144b34 -= step;
        if (s->unk_144b34 < minimum) { s->unk_144b34 = minimum; s->unk_144b3c = 0; }
    }
    s->body_height = o->z + o->z / 2 + 0x1000 + s->unk_144b34;
}

/* HandleNextFrame 01C950..01CAC0; walking bob continues through its
 * return-to-zero phase after movement input is released. */
static void rise(sim_state *s)
{
    int32_t maximum, increment, speed = s->unk_144aec;
    if (!s->unk_144afc) { s->unk_144b38 = 0; return; }
    if (s->unk_144afc == 1) { s->rise_dir = 1; s->unk_144afc = 2; }
    if (speed < 0) speed = -speed;
    maximum = speed * 34;
    if (maximum > 0x1100) maximum = 0x1100;
    if (s->unk_144b34 < 0) maximum /= 2;
    else {
        if (s->flight_mode) maximum += 1000;
        if (maximum < 1000) maximum = 1000;
    }
    increment = maximum * s->frame_ticks / 4;
    if (!increment) increment = 128;
    if (s->rise_dir) {
        if (s->unk_144b38 < 0 && s->unk_144b38 + increment >= 0) {
            s->unk_144afc = s->unk_144b38 = 0;
        } else {
            s->unk_144b38 += increment;
            if (s->unk_144b38 > maximum) {
                s->unk_144b38 = maximum;
                s->rise_dir = 0;
            }
        }
    } else {
        s->unk_144b38 -= increment;
        if (s->unk_144b38 < -maximum) {
            s->unk_144b38 = -maximum;
            s->rise_dir = 1;
        }
    }
}

/* HandleNextFrame 01C402..01C73F, takeoff[] at0963A0. */
static int flight(sim_state *s, world *w, world_object *o)
{
    static const int32_t takeoff[8] = {256,512,1024,2048,3072,4096,6144,8192};
    int32_t maximum = w->unk_130D20 - SIM_QUARTER_TILE;
    if (!s->flight_mode) return 0;
    if (s->flight_active) {
        if (s->flight_active < 1 || s->flight_active > 8)
            return cm_fail("flight", (uint32_t)s->flight_active, "invalid takeoff index");
        s->unk_131418 += takeoff[s->flight_active - 1];
        if (++s->flight_active > 8) s->flight_active = 0;
        s->flight_unk_144B48 = s->unk_144aec = 8;
    } else {
        if (s->flight_action >= 2) {
            if (s->key_hand_y < 0) {
                int32_t old_z = o->floor_z;
                if (sim_coll_flat(w, w->player_object, o->x, o->y)) return -1;
                s->unk_131418 = old_z > o->floor_z ? old_z - o->floor_z : 0;
                o->floor_z = old_z;
            }
            s->unk_131418 += s->key_hand_y * (s->flight_mode == 2 ? 192 : 128);
        }
        if (s->flight_action == 2) s->unk_144aec = s->flight_unk_144B48;
        else if (!s->flight_action || s->flight_action == 3) {
            int32_t speed = s->unk_144aec;
            if (s->flight_mode == 2 && speed == 48) speed *= 3;
            else if (speed > 32) speed *= 2;
            s->flight_unk_144B48 = s->unk_144aec = speed;
            s->fly_throttle = s->unk_144ae8;
            s->fly_curr_throttle = s->unk_144af8;
        }
    }
    if (s->flight_mode == 1 || s->flight_mode == 3) {
        if (s->unk_131418 <= 0x800) s->unk_131418 = 0x800;
        if (s->flight_mode == 1 && s->unk_131418 > 0xa000) s->unk_131418 = 0xa000;
        if (s->flight_mode == 3 && s->unk_131418 > maximum) s->unk_131418 = maximum;
        if (!s->unk_144afc) s->unk_144afc = 1;
    } else if (s->unk_131418 <= 0) {
        s->unk_131418 = s->unk_144afc = 0;
    } else {
        if (s->unk_131418 > maximum) s->unk_131418 = maximum;
        if (!s->unk_144afc) s->unk_144afc = 1;
    }
    return 0;
}

int sim_frame(sim_state *s, world *w, const cm_input *input, uint32_t timer_ticks)
{
    world_object *o;
    world_person *p;
    int jump, can_jump, running;
    if (w->player_object >= w->object_count || w->player_person >= w->person_count)
        return cm_fail("simulation", w->player_object, "invalid player indices");
    o = &w->objects[w->player_object];
    p = &w->persons[w->player_person];
    sim_input(s, input);
    sim_clock(s, timer_ticks);
    if (s->replay_delta >= 0) s->frame_ticks = s->replay_delta;
    /* HandleNextFrame 01C1B5/01C1BC, before player movement and firing.
     * Shots created later by player/NPC input wait until the next frame. */
    if (s->ai) {
        s->ai->timer_ticks = timer_ticks;
        if (ai_update_missile(s->ai, s, w)) return -1;
        ai_update_soul_fire(s->ai, s, w);
    }
    /* HandleNextFrame 01C244..01C258 clears frame actions, not inertia.
     * In particular, zero momentum must not reuse last frame's speed. */
    s->unk_144aec = s->unk_144af0 = s->unk_144af4 = 0;
    jump = input->modern ? sim_modern_input(s, w, input) : keyboard(s, w);
    if (jump < 0) return -1;
    if (!jump) s->unk_144b20 = 0;
    can_jump = !s->flight_mode && !s->unk_144b20 && !s->unk_144b1c && !s->unk_144b30 &&
               !s->unk_144b50 && s->unk_144b3c != -1;
    running = can_jump && (input->modern ?
        input->run && s->movement_request > 0 : s->movement_request > RUN_THRESHOLD);
    s->running = running;
    acceleration(s, running ? RUN_ACCEL_MAX : WALK_ACCEL_MAX);
    s->flight_action = 0;
    if (s->flight_mode) {
        if (down(s, 0x11, 1)) {
            if (s->key_hand_y < 8) s->key_hand_y = 8;
            s->key_hand_y *= 2;
            if (s->key_hand_y > 96) s->key_hand_y = 96;
            s->flight_action = 3;
        }
        if (down(s, 0x2d, 1)) {
            if (s->key_hand_y > -8) s->key_hand_y = -8;
            s->key_hand_y *= 2;
            if (s->key_hand_y < -96) s->key_hand_y = -96;
            s->flight_action = 3;
        }
        if (jump) {
            s->flight_action = 2;
            s->unk_144ae8 = s->fly_throttle;
            s->unk_144af8 = s->fly_curr_throttle;
        }
    }
    if (jump && (running || (input->modern && can_jump))) {
        s->unk_144b34 = 0;
        s->body_height = o->z + o->z / 2 + 0x1000;
        s->unk_144b3c = 0;
        s->unk_144b1c = 4 - s->unk_144b00 / 24;
        if (s->unk_144b1c < 1) s->unk_144b1c = 1;
        if (s->unk_144b1c > 4) s->unk_144b1c = 4;
        s->unk_144b20 = 1;
        s->unk_144b28 = 0;
        s->unk_144b2c = p->unk_007C ? p->unk_0080 : o->floor_z;
        p->unk_007C = 0;
        s->unk_144b24 = s->unk_144ae8;
    } else if (!input->modern && jump && can_jump && !s->flight_mode &&
               (s->pc_mode == 1 || s->pc_mode == 2 || s->pc_mode == 5)) {
        s->previous_pc_mode = s->pc_mode;
        s->pc_mode = 3;
    } else if (!input->modern && !jump && s->pc_mode == 3) {
        s->pc_mode = s->previous_pc_mode;
    }
    momentum(s);
    if (flight(s, w, o)) return -1;
    if (s->ai) ai_pc_hand_bob(s->ai, s);
    if (s->unk_144b1c) jump_step(s);
    else rise(s);
    if (sim_swim_step(s, w)) return -1;
    /* HandleNextFrame 01CD80..01CE39; environmental fire uses the same
     * floor/flat contact query as swimming, not projectile CollFire. */
    if (s->fire_check && !s->unk_144b1c && !s->flight_mode && s->pc_mode != 7) {
        int touching = sim_coll_swim_flat(w, w->player_object,
                                          s->fire_tile_panel, s->fire_tile_art);
        if (touching < 0) return -1;
        if (touching && s->fire_damage &&
            sim_wrap32(timer_ticks) > sim_wrap32(s->swim_damage_time)) {
            int32_t damage;
            if (!s->ai) return cm_fail("FireDamage", w->player_person, "missing actor state");
            /* 01CDE7 raises the same hit flash as swim damage. */
            if (s->ai->pc_flash < 2) s->ai->pc_flash = 2;
            damage = sim_wrap32((uint32_t)s->fire_damage *
                (uint32_t)(1 + world_rnd(&w->rng, 6)));
            if (ai_damage_person(s->ai, w, w->player_person, damage, 100, 125)) return -1;
            s->swim_damage_time = timer_ticks + 40 + (uint32_t)world_rnd(&w->rng, 60);
        }
    }
    body(s, w, o);
    if (s->pc_mode == 7 && s->vehicle_type != 1) {
        if (s->unk_144aec) s->unk_144aec = s->unk_144aec / 2 + 4;
        if (s->unk_144af0 > 0) s->unk_144af0 = s->unk_144af0 / 4 + 4;
        else if (s->unk_144af0 < 0) s->unk_144af0 = s->unk_144af0 / 4 - 4;
    }
    world_omap_delete(w, w->player_object);
    if (sim_move_person(s, w, w->player_object, s->unk_144af0,
                         s->unk_144aec, s->unk_144af4)) return -1;
    world_omap_insert(w, w->player_object);
    return 0;
}
