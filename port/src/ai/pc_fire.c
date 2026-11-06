#include "combat_internal.h"
#include "audio.h"
#include "events.h"

#include <string.h>

static int signed16(unsigned value)
{
    value &= 65535;
    return value < 32768 ? (int)value : (int)value-65536;
}

static int combat_mode(const sim_state *s)
{
    return s->modern_aim ? s->previous_pc_mode : s->pc_mode;
}

static int short_table(const cm_exe *exe, uint32_t address, int16_t *dst, unsigned count)
{
    uint8_t bytes[42];
    unsigned i;
    if (count > sizeof bytes/2)
        return cm_fail(exe->path, address, "short combat table exceeds row bound");
    if (cm_exe_bytes(exe, address, bytes, count*2)) return -1;
    for (i = 0; i < count; ++i) dst[i] = (int16_t)signed16(cm_u16(bytes+i*2));
    return 0;
}

static int signed_bytes(const cm_exe *exe, uint32_t address, int16_t *dst, unsigned count)
{
    uint8_t bytes[44];
    unsigned i;
    if (count > sizeof bytes)
        return cm_fail(exe->path, address, "byte combat table exceeds row bound");
    if (cm_exe_bytes(exe, address, bytes, count)) return -1;
    for (i = 0; i < count; ++i) dst[i] = bytes[i] < 128 ? bytes[i] : (int16_t)((int)bytes[i]-256);
    return 0;
}

/* CM_HI.EXE tables named by Watcom globals. No retail table bytes in C. */
int ai_pc_load_tables(ai_state *a, const cm_exe *exe)
{
    ai_pc_rules *r = &a->pc_rules;
    ai_pc_aim *q = &a->pc_aim;
    int32_t tank[4];
    unsigned i, j;
    if (cm_exe_table(exe, 0x93c4c, r->center_aim_x, 16) ||
        cm_exe_table(exe, 0x93c8c, r->center_aim_y, 16) ||
        cm_exe_table(exe, 0x93c2c, r->air_car_trajectory, 8) ||
        cm_exe_table(exe, 0x93efc, r->ammo_type, 10) ||
        cm_exe_table(exe, 0x92690, r->spell_power, 16) ||
        cm_exe_table(exe, 0x92ea0, r->glow_set, 16) ||
        cm_exe_table(exe, 0x930b4, r->swing_damage, 21) ||
        cm_exe_table(exe, 0x93594, tank, 4)) return -1;
    for (i = 0; i < 16; ++i) {
        if (cm_exe_table(exe, 0x93ccc+i*32, r->hand_bound[i], 8)) return -1;
        for (j = 0; j < 2; ++j)
            if (cm_exe_table(exe, 0x9382c+i*64+j*32, r->trajectory[i][j], 8)) return -1;
    }
    for (i = 0; i < 9; ++i)
        if (cm_exe_table(exe, 0x93108+i*36, r->swing_mode[i], 9) ||
            cm_exe_table(exe, 0x9324c+i*36, r->mat_idx[i], 9) ||
            short_table(exe, 0x92f38+i*42, r->swing_frame[i], 21) ||
            signed_bytes(exe, 0x9356c+i*3, r->auto_s_init_mode[i], 3)) return -1;
    for (i = 0; i < 11; ++i)
        if (signed_bytes(exe, 0x93540+i*4, r->auto_s_pos[i], 4)) return -1;
    if (signed_bytes(exe, 0x93587, r->auto_s_init_pos, 11) ||
        cm_exe_bytes(exe, 0x93390, (uint8_t *)r->pos_mat, sizeof r->pos_mat)) return -1;
    q->tank_udoffset = tank[0]; q->tank_ud = tank[1];
    q->tank_fall = tank[2]; q->tank_falloffset = tank[3];
    q->grenade_aim_center = r->center_aim_y[10];
    q->auto_enable = 1; /* Xox::Init 0x1097E. */
    q->tank_auto = a->pc.auto_s_flag = 1; /* Init 0x10988..0x109A6. */
    q->hand_type = 1;
    a->pc.loop_sound = -1; /* InitPC/first scene, original +144A28. */
    a->pc.sword_hum_slot = -1; /* Supplied EXE initial SwordHum_Slot9261C. */
    a->pc.kb_fire = 0x39; a->pc.kb_fire_bit = 1; /* ReKeyDefaults 0x52644. */
    return 0;
}

/* Xox::ResetAim, CM_HI.EXE 0x12D60. */
static int reset_aim(ai_state *a, int mode)
{
    unsigned type = mode == 1 ? a->pc_aim.hand_type : mode == 5 ? 1u : 0u;
    if (type >= 16) return cm_fail("ResetAim", type, "invalid player hand type");
    a->pc_aim.aim_x = a->pc_rules.center_aim_x[type];
    a->pc_aim.aim_y = a->pc_rules.center_aim_y[type];
    return 0;
}

int ai_reset_aim(ai_state *a, const sim_state *s)
{
    return reset_aim(a, combat_mode(s));
}

static int low_word(int32_t n) { return signed16((uint32_t)n); }
static int high_word(int32_t n) { return signed16((uint32_t)n >> 16); }
static void set_low(int32_t *n, int value)
{
    *n = ai_signed32(((uint32_t)*n & UINT32_C(0xffff0000)) | ((unsigned)value & 65535));
}
static void set_high(int32_t *n, int value)
{
    *n = ai_signed32(((uint32_t)*n & 65535) | ((uint32_t)(uint16_t)value << 16));
}
static int after(uint32_t tick, uint32_t deadline)
{
    return ai_signed32(tick) > ai_signed32(deadline);
}

/* HandleNextFrame 0x1C73F..0x1C873, after flight and before jump movement. */
void ai_pc_hand_bob(ai_state *a, const sim_state *s)
{
    ai_pc_aim *q = &a->pc_aim;
    int32_t phase = a->pc.bob_phase;
    if (s->unk_144b1c || s->unk_144afc) {
        int32_t speed = s->unk_144aec < 0 ? ai_sub32(0, s->unk_144aec) : s->unk_144aec;
        int32_t scale = speed/32+1;
        if (s->bp_limit_flag) phase = (phase+1)%14;
        q->render_x = phase < 4 ? -phase : phase < 11 ? phase-7 : 14-phase;
        q->render_y = phase < 4 ? phase-3 : phase < 7 ? 3-phase :
                      phase < 10 ? phase-10 : 10-phase;
        if (scale > 4) scale = 4;
        q->render_x *= scale; q->render_y *= scale;
    } else {
        if (q->render_x < 0) ++q->render_x;
        else if (q->render_x > 0) --q->render_x;
        if (q->render_y < 0) ++q->render_y;
        else if (q->render_y > 0) --q->render_y;
        phase = 0;
    }
    a->pc.bob_phase = phase;
}

static void hand_axis(int32_t *axis, int direction)
{
    if (direction > 0 && *axis < 64) *axis += 64;
    else if (direction < 0 && *axis > -64) *axis -= 64;
}

/* HandleKeyboardEvents 0x12E83/0x1365B..0x13C02/0x13E1F. The hand
 * keyboard gate uses the previous frame's fire value, before ProcessInput. */
static void keyboard_hand(ai_pc_state *p, const sim_state *s, int mode)
{
    const uint8_t *k = s->kb_scan_status;
    int side;
    if (mode != 2 || p->fire != 1 || s->mouse_present || s->joystick_present) return;
    side = s->kb_side_toggle < SIM_SCAN_COUNT &&
           (k[s->kb_side_toggle] & s->kb_side_toggle_bit);
    if ((k[0x48]&5) || (k[0x4c]&1)) hand_axis(&p->key_hand_y, -1);
    else if (k[0x50]&5) hand_axis(&p->key_hand_y, 1);
    if (k[0x4d]&5) {
        if (side) hand_axis(&p->key_hand_y, 1);
        hand_axis(&p->key_hand_x, 1);
    } else if (k[0x4b]&5) {
        if (side) hand_axis(&p->key_hand_y, 1);
        hand_axis(&p->key_hand_x, -1);
    }
    if (k[0x47]&1) {
        hand_axis(&p->key_hand_y, -1); hand_axis(&p->key_hand_x, -1);
    }
    if (k[0x49]&1) {
        hand_axis(&p->key_hand_y, -1); hand_axis(&p->key_hand_x, 1);
    }
    if ((k[0x4f]&1) || (s->kb_side_left < SIM_SCAN_COUNT &&
        (k[s->kb_side_left]&s->kb_side_left_bit))) {
        hand_axis(&p->key_hand_y, 1); hand_axis(&p->key_hand_x, -1);
    }
    if ((k[0x51]&1) || (s->kb_side_right < SIM_SCAN_COUNT &&
        (k[s->kb_side_right]&s->kb_side_right_bit))) {
        hand_axis(&p->key_hand_y, 1); hand_axis(&p->key_hand_x, 1);
    }
    p->hand_x = p->key_hand_x; p->hand_y = p->key_hand_y;
}

static int equip(ai_state *a, const world *w, int mode)
{
    ai_pc_state *p = &a->pc;
    ai_pc_aim *q = &a->pc_aim;
    const events_state *e = a->events;
    const int32_t *b, *t;
    int weapon = mode == 2 ? e->hh_weapon : e->weapon;
    unsigned type;
    if (mode == 3) return reset_aim(a, mode);
    if (mode != 1 && mode != 2 && mode != 5) { p->last_mode = mode; return 0; }
    if (mode == 5) {
        if (e->spell < 0 || e->spell >= 16)
            return cm_fail("EquipWeapon", (uint32_t)e->spell, "invalid spell");
        p->glow_set = a->pc_rules.glow_set[e->spell];
    }
    if (p->last_mode == mode && p->last_weapon == weapon) return 0;
    if (mode == 1 && (weapon < 0 || weapon >= 10))
        return cm_fail("EquipWeapon", (uint32_t)weapon, "invalid weapon");
    if (mode == 2 && (weapon < 0 || weapon >= 4))
        return cm_fail("EquipHHWeapon", (uint32_t)weapon, "invalid melee weapon");
    type = mode == 1 ? (unsigned)weapon+2 : mode == 5 ? 1u : 0u;
    if (mode != 2) q->hand_type = type; /* EquipHHWeapon103A4 only selects SwordProp. */
    if (mode == 1 || mode == 5) {
        /* EquipWeapon 0x10451..0x1055F, complete bound/trajectory row. */
        b = a->pc_rules.hand_bound[type];
        q->w_max_leftaimx = b[0]; q->w_leftaimx = b[1];
        q->w_max_rightaimx = b[2]; q->w_rightaimx = b[3];
        q->w_max_topaimy = b[4]; q->w_topaimy = b[5];
        q->w_max_botaimy = b[6]; q->w_botaimy = b[7];
        t = a->pc_rules.trajectory[type][w->unk_5E7A8 != 0];
        q->pistol_angle = (int16_t)signed16((uint32_t)t[0]);
        q->pistol_angoffset = (int16_t)signed16((uint32_t)t[1]);
        q->pistol_lr = t[2]; q->pistol_lroffset = t[3];
        q->pistol_ud = t[4]; q->pistol_udoffset = t[5];
        q->pistol_fall = t[6]; q->pistol_falloffset = t[7];
        q->pistol_xoffset = a->pc_rules.center_aim_x[type];
        q->pistol_boffset = 20;
        p->muzzle_mode = p->muzzle_count = 0;
        if (mode == 5) p->glow_frame = 0; /* EquipWeapon10441, magic branch only. */
    }
    p->last_mode = mode; p->last_weapon = weapon;
    return reset_aim(a, mode);
}

/* LoadScene 0x12148/0x12155 also equips before the first game frame. */
int ai_pc_equip(ai_state *a, const sim_state *s, const world *w)
{
    if (!a->events || !a->tables_loaded)
        return cm_fail("EquipWeapon", 0, "player combat is not initialized");
    a->pc.last_mode = 0;
    /* LoadScene equips the last gun as well as the active sword. */
    if (combat_mode(s) == 2 && equip(a, w, 1)) return -1;
    return equip(a, w, combat_mode(s));
}

static int prepare_aim(ai_state *a, const sim_state *s, int mode)
{
    ai_pc_aim *q = &a->pc_aim;
    const ai_pc_state *p = &a->pc;
    q->weapon_aim_step = 4; q->tank_aim_step = 1;
    a->pc.sword_aim_step = 4;
    /* HandleNextFrame 0x1D09E..0x1D1BB. */
    if (mode == 1 || mode == 5) {
        if (q->auto_enable) {
            if (after(s->timer_ticks, q->reset_auto_aim_time) && !p->latch && !p->cooldown)
                if (ai_reset_aim(a, s)) return -1;
        } else {
            if (q->aim_y < q->w_max_botaimy) q->aim_y = q->w_max_botaimy;
            else if (q->aim_y > q->w_max_topaimy) q->aim_y = q->w_max_topaimy;
            if (q->aim_y < q->w_botaimy) q->weapon_aim_step += 3;
            else if (q->aim_y > q->w_topaimy) q->weapon_aim_step -= 3;
            if (q->aim_x < q->w_max_leftaimx) q->aim_x = q->w_max_leftaimx;
            else if (q->aim_x > q->w_max_rightaimx) q->aim_x = q->w_max_rightaimx;
            if (q->aim_x < q->w_leftaimx) --q->weapon_aim_step;
            else if (q->aim_x > q->w_rightaimx) ++q->weapon_aim_step;
        }
    } else if (mode == 7 && !s->vehicle_type) {
        /* 1D1E2..1D229: enabled auto-aim owns this branch even while its
         * timer/latch blocks a reset; it must not fall through to manual. */
        if (q->auto_enable && q->tank_auto) {
            if (after(s->timer_ticks, q->reset_auto_aim_time) && !p->latch && !p->cooldown)
                q->tank_aim_y = 19;
        } else if (s->tank_hand && p->hand_y) {
            /* 1D22E..1D28F. SBB's carry makes negative division truncate
             * toward zero. Each nonzero direction clamps only its own edge. */
            int32_t aim = ai_add32(q->tank_aim_y, p->hand_y / 8);
            if (p->hand_y < 0 && aim < 19) aim = 19;
            else if (p->hand_y > 0 && aim > 63) aim = 63;
            q->tank_aim_y = aim;
        }
        if (q->tank_aim_y < 32) ++q->tank_aim_step;
        else if (q->tank_aim_y > 47) --q->tank_aim_step;
    }
    return 0;
}

/* ProcessInputEvents 0x147DD..0x14A3B: held input deliberately bypasses
 * the cooldown->latch copy. Semi-automatic spells require a release. */
static void fire_input(ai_state *a, const sim_state *s, world_person *person, int mode)
{
    ai_pc_state *p = &a->pc;
    int held = s->primary_fire;
    if (!held) {
        p->latch = p->cooldown;
        p->fire = p->key_swing_sword;
    } else if (mode == 2) {
        if (!p->fire) {
            set_high(&person->unk_0040, 7); set_low(&person->unk_0040, 4);
            person->unk_0044 = 0; set_low(&person->unk_0048, 0);
            person->unk_003C = 0;
            p->auto_swinging = p->key_hand_x = p->key_hand_y = 0;
            p->auto_s_time = s->timer_ticks+3;
        }
        p->fire = 1;
    } else {
        p->fire = !p->latch;
        if (mode == 3 && p->fire) p->latch = 1;
    }
    p->key_swing_sword = 0;
}

static int launch_pc(ai_state *a, sim_state *s, world *w, unsigned type, int aim)
{
    return ai_pc_launch_missile(a, s, w, type, a->pc_aim.aim_x,
                                a->pc_aim.aim_y, (int16_t)aim);
}

/* ProcessInputEvents 0x14857/0x1496A/0x14C19: create the new loop before
 * aborting a different old sample; -1 is the real muted/full-pool handle. */
static void player_loop(ai_pc_state *p, unsigned sample, int volume,
                         int first_volume, int adjust)
{
    if (p->sword_hum_slot == -1) {
        p->sword_hum_sound = (int32_t)sample;
        p->sword_hum_slot = snd_loop(sample, first_volume, 32768);
    } else if (p->sword_hum_sound != (int32_t)sample) {
        int slot = snd_loop(sample, volume, 32768);
        snd_abort(p->sword_hum_slot, (unsigned)p->sword_hum_sound);
        p->sword_hum_slot = slot; p->sword_hum_sound = (int32_t)sample;
    } else if (adjust) (void)snd_set_volume(p->sword_hum_slot, volume);
}

static void player_loops(ai_state *a, const sim_state *s, world *w, int mode)
{
    ai_pc_state *p = &a->pc;
    int keep = 0;
    /* Sim's running gate includes148C4's held-primary melee EDI clear. */
    if (s->running && (mode == 1 || mode == 2 || mode == 5)) {
        player_loop(p, 1, 32767, 32767, 0);
        keep = 1;
    } else if (mode == 2 && s->primary_fire) {
        player_loop(p, 32, 32767, 16384, 0);
        keep = 1;
    }
    if (mode == 7 && !s->vehicle_type) {
        int32_t speed = s->unk_144af8 < 0 ? ai_sub32(0, s->unk_144af8) : s->unk_144af8;
        int32_t volume = ai_add32(ai_signed32((uint32_t)speed << 7), 4096);
        player_loop(p, 61, volume, volume, 1);
        keep = 1;
    }
    if (!keep && p->sword_hum_slot != -1) {
        snd_abort(p->sword_hum_slot, (unsigned)p->sword_hum_sound);
        p->sword_hum_slot = -1;
        /* 0x14D04..0x14D41: stopping running can play a breath even at full HP. */
        if (p->sword_hum_sound == 1 && snd_done(a->pc_sound, 3))
            a->pc_sound = snd_play(3, 16384-world_rnd(&w->rng, 8192), 32768);
    }
}

static int bad_spell(ai_state *a, world *w)
{
    (void)snd_play(28, 32767, 32768);
    if (a->pc_flash < 2) a->pc_flash = world_rnd(&w->rng, 2)+2;
    return ai_damage_person(a, w, w->player_person, world_rnd(&w->rng, 4)+1, 50, 50);
}

static int ranged_fire(ai_state *a, sim_state *s, world *w)
{
    ai_pc_state *p = &a->pc;
    ai_pc_aim *q = &a->pc_aim;
    events_state *e = a->events;
    int weapon = e->weapon, fired = 0, aim_advanced = 0, used_aim = 0;
    unsigned ammo;
    if (weapon < 0 || weapon >= 10)
        return cm_fail("PCFire", (uint32_t)weapon, "invalid weapon");
    /* HandleNextFrame 0x1D841..0x1D8AF. */
    if ((p->cooldown || p->latch) && (weapon == 2 || weapon == 8 || weapon == 9)) {
        q->weapon_aim_step += weapon == 8 ? 3 : 9;
        aim_advanced = 1; used_aim = weapon == 8;
    }
    if (p->cooldown) --p->cooldown;
    if (!p->fire) {
        p->shot_count = 0;
        if (p->loop_sound != -1) {
            if (!snd_done(p->loop_sound, 20)) snd_abort(p->loop_sound, 20);
            p->loop_sound = -1;
        }
    } else if (weapon >= 7) {
        events_item *item;
        if (e->weapon_slot >= EVENTS_ITEM_MAX)
            return cm_fail("PCFire", e->weapon_slot, "invalid equipped item slot");
        item = &e->items[e->weapon_slot];
        if (item->quantity > 0) {
            if (weapon == 7) {
                if (p->underwater) {
                    if (bad_spell(a, w)) return -1;
                    fired = 1;
                } else if (e->item_spell == 13 || e->item_spell == 14) {
                    world_person *pc = &w->persons[w->player_person];
                    if (e->item_spell == 13) pc->unk_0074 = ai_add32(pc->unk_0074, 200);
                    else pc->unk_005C = ai_add32(pc->unk_005C, 300);
                    (void)snd_play(27, 32767, 32768); fired = 1;
                } else if (e->item_spell == 15) {
                    world_person *pc = &w->persons[w->player_person];
                    set_low(&pc->unk_0038, high_word(pc->unk_0038));
                    (void)snd_play(27, 32767, 32768); fired = 1;
                } else {
                    if (e->item_spell < 0 || e->item_spell >= 16)
                        return cm_fail("PCFire", (uint32_t)e->item_spell, "invalid item spell");
                    fired = launch_pc(a, s, w, (unsigned)a->spell_missile[e->item_spell], 0);
                }
                if (fired > 0) p->latch = p->cooldown = 1;
            } else {
                fired = launch_pc(a, s, w, weapon == 8 ? 1u : 38u, 0);
                if (fired > 0) {
                    if (!aim_advanced) q->weapon_aim_step += weapon == 8 ? 3 : 9;
                    p->latch = 1; p->cooldown = 3; used_aim = weapon == 8;
                }
            }
            if (fired < 0) return -1;
            item->quantity -= fired;
            if (!item->quantity) {
                item->type = 0;
                s->pc_mode = 5;
                if (equip(a, w, 5)) return -1;
            }
        }
    } else {
        int type = a->pc_rules.ammo_type[weapon];
        if (type < 0 || type >= 12)
            return cm_fail("PCFire/ammo", (uint32_t)type, "invalid ammo type");
        ammo = (unsigned)type;
        if (e->ammo[ammo] > 0) {
            switch (weapon) {
            case 0:
                fired = launch_pc(a, s, w, 2, 0);
                if (fired > 0) {
                    p->latch = p->cooldown = 1;
                    if (p->muzzle_mode != 1) {
                        p->muzzle_mode = 2; p->muzzle_count = 0;
                        p->muzzle_frame = (q->weapon_aim_step%3)*2+9;
                    }
                }
                break;
            case 1:
                if (p->shot_count < 8) {
                    fired = launch_pc(a, s, w, 15, 0);
                    if (fired > 0) {
                        p->unk_1444EC = 3; ++p->shot_count; q->weapon_aim_step += 9;
                    }
                    p->latch = p->cooldown = 0;
                }
                break;
            case 2:
                fired = launch_pc(a, s, w, 14, 0);
                if (fired > 0) {
                    p->latch = 1; p->cooldown = 2; p->unk_1444EC = 2;
                    if (p->muzzle_mode != 2) {
                        p->muzzle_mode = -3; p->muzzle_count = 0; p->muzzle_frame = 18;
                    }
                }
                break;
            case 3:
                fired = launch_pc(a, s, w, 18, 0);
                if (fired > 0) {
                    p->latch = 1; p->cooldown = 2;
                    if (p->muzzle_mode != 2) {
                        p->muzzle_mode = 3; p->muzzle_count = 0;
                        p->muzzle_frame = (q->weapon_aim_step%3)*3+9;
                    }
                }
                break;
            case 4:
                fired = launch_pc(a, s, w, 20, 0);
                if (fired > 0) { p->latch = p->cooldown = 1; p->unk_1444EC = 5; }
                break;
            case 5: case 6:
                if (p->shot_count < 5) {
                    fired = launch_pc(a, s, w, weapon == 5 ? 40u : 49u, 1);
                    if (fired > 0) {
                        ++p->shot_count;
                        if ((weapon == 5 && p->muzzle_mode != 2) ||
                            (weapon == 6 && !p->muzzle_mode)) {
                            p->muzzle_mode = 3; p->muzzle_count = 0;
                            p->muzzle_frame = (weapon == 5 ? q->weapon_aim_step%3 : 0)*3+9;
                        }
                        if (weapon == 6 && p->loop_sound == -1)
                            p->loop_sound = snd_play(20, 32767, 32768);
                    }
                    p->latch = p->cooldown = 0;
                }
                break;
            }
            if (fired < 0) return -1;
            e->ammo[ammo] = (int16_t)signed16((unsigned)e->ammo[ammo]-(unsigned)fired);
        }
    }
    if (weapon == 8 && !used_aim) q->weapon_aim_step %= 3;
    return 0;
}

static int linked_spell(ai_state *a, sim_state *s, world *w)
{
    ai_pc_state *p = &a->pc;
    const world_object *pc = &w->objects[w->player_object];
    if (!a->unk_144AE0 && w->scene == 3) {
        const world_object *target;
        int object;
        int32_t delta;
        if (w->person_count <= 82 || w->persons[82].object < 0 ||
            (unsigned)w->persons[82].object >= w->object_count)
            return cm_fail("PCFire/link", 82, "missing scene-three target");
        object = w->persons[82].object; target = &w->objects[object];
        delta = signed16(cm_atan64k(pc->x, pc->y, target->x, target->y)-pc->heading);
        if (ai_add32(target->floor_z, target->z) < ai_add32(pc->floor_z, s->body_height) &&
            ai_add32(target->floor_z, ai_add32(target->height, target->height)) > pc->floor_z &&
            cm_distance(pc->x, pc->y, target->x, target->y) < 0x8000 &&
            delta > -0x1800 && delta < 0x1800) {
            if (a->events->event_count <= 129)
                return cm_fail("PCFire/link", 129, "missing scene-three event");
            a->unk_144AE0 = 1; p->magic_link_ticks = 5;
            a->events->events[129].state = 0;
        }
    }
    if (!a->unk_144AE0) {
        if (ai_launch_missile(a, s, w, w->player_object, w->player_object, 33) < 0 ||
            bad_spell(a, w)) return -1;
        p->latch = 1;
        return 1;
    }
    if (p->magic_link_ticks) return 0;
    p->latch = 0;
    return launch_pc(a, s, w, (unsigned)a->spell_missile[a->events->spell], 0);
}

static int spell_fire(ai_state *a, sim_state *s, world *w)
{
    ai_pc_state *p = &a->pc;
    events_state *e = a->events;
    int fired, advanced = p->cooldown || p->latch;
    int cost;
    if (e->spell < 0 || e->spell >= 16)
        return cm_fail("PCFire/spell", (uint32_t)e->spell, "invalid spell");
    cost = a->pc_rules.spell_power[e->spell];
    if (cost < 0 || cost > INT16_MAX)
        return cm_fail("PCFire/spell", (uint32_t)e->spell, "invalid energy cost");
    if (advanced) {
        a->pc_aim.weapon_aim_step += 9;
        if (after(s->timer_ticks, p->spell_timer)) p->cooldown = 0;
    }
    if (!p->fire) return 0;
    if (e->energy < cost) {
        p->spell_timer = s->timer_ticks+10; p->cooldown = 1;
        return 0;
    }
    if (p->underwater) {
        if (bad_spell(a, w)) return -1;
        fired = 1;
    } else if (e->spell == 5) {
        fired = linked_spell(a, s, w);
    } else {
        fired = launch_pc(a, s, w, (unsigned)a->spell_missile[e->spell], 0);
        p->latch = 1;
    }
    if (fired < 0) return -1;
    if (fired) e->energy -= cost;
    if (!advanced) a->pc_aim.weapon_aim_step += 9;
    p->spell_timer = s->timer_ticks+20; p->cooldown = 1;
    return 0;
}

static int tank_fire(ai_state *a, sim_state *s, world *w)
{
    ai_pc_state *p = &a->pc;
    ai_pc_aim *q = &a->pc_aim;
    int fired;
    if (p->cooldown || p->latch) {
        if (!s->vehicle_type) q->tank_aim_step += 3;
        if (after(s->timer_ticks, p->tank_timer)) p->cooldown = 0;
    }
    if (!p->fire) return 0;
    if (!s->vehicle_type) {
        fired = ai_pc_launch_missile(a, s, w, 6, 0, q->tank_aim_y, 0);
        if (fired < 0) return -1;
        if (fired && p->muzzle_mode != 2) {
            p->unk_1444EC = 2; p->muzzle_mode = 3; p->muzzle_count = 0;
        }
    } else {
        int second;
        q->pistol_lroffset = -0x2600;
        q->pistol_angoffset = (int16_t)signed16((uint32_t)a->pc_rules.air_car_trajectory[1]+p->air_car_spread);
        fired = ai_pc_launch_missile(a, s, w, 43, 0, q->tank_aim_y, 0);
        if (fired < 0) return -1;
        q->pistol_lroffset = 0x2600;
        q->pistol_angoffset = (int16_t)signed16((uint32_t)a->pc_rules.air_car_trajectory[1]-p->air_car_spread);
        second = ai_pc_launch_missile(a, s, w, 43, 0, q->tank_aim_y, 0);
        if (second < 0) return -1;
        if (fired+second && p->muzzle_mode != 2) {
            p->muzzle_mode = 3; p->muzzle_count = 0;
        }
    }
    p->latch = p->cooldown = 1; p->tank_timer = s->timer_ticks+30;
    return 0;
}

static int auto_sword(ai_state *a, world *w, unsigned position, uint32_t ticks)
{
    ai_pc_state *p = &a->pc;
    int col, row, mode = p->auto_s_mode;
    if (position >= 9) return cm_fail("AutoSwordSwing", position, "invalid hand grid");
    col = (int)position%3; row = (int)position/3;
    if (p->auto_swinging) {
        if (p->auto_s_reset) {
            int x = p->reset_pos%3, y = p->reset_pos/3;
            if (p->reset_pos < 0 || p->reset_pos >= 9)
                return cm_fail("AutoSwordSwing", (uint32_t)p->reset_pos, "invalid reset grid");
            if (p->auto_update_flag) {
                col += col < x ? 1 : col > x ? -1 : 0;
                row += row < y ? 1 : row > y ? -1 : 0;
            }
            if (row*3+col == p->reset_pos) p->auto_s_reset = 0;
        } else {
            int next;
            if (mode < 0 || mode >= 11 || p->auto_swinging < 1 || p->auto_swinging > 4)
                return cm_fail("AutoSwordSwing", (uint32_t)mode, "invalid swing sequence");
            next = a->pc_rules.auto_s_pos[mode][p->auto_swinging-1];
            if (next == -1) p->auto_swinging = 0;
            else {
                if (next < 0 || next >= 9)
                    return cm_fail("AutoSwordSwing", (uint32_t)next, "invalid sequence position");
                position = (unsigned)next;
                if (p->auto_update_flag) ++p->auto_swinging;
            }
            col = (int)position%3; row = (int)position/3;
        }
    } else {
        int blunt = a->events->hh_weapon == 1 || a->events->hh_weapon == 2;
        mode = a->pc_rules.auto_s_init_mode[position][world_rnd(&w->rng, 3)];
        if (blunt && mode == 6) mode = -1;
        if (mode < 0) {
            if (mode == -1) {
                unsigned tries;
                for (tries = 0; tries < 128; ++tries) {
                    mode = world_rnd(&w->rng, 11);
                    if (!blunt || mode != 6) break;
                }
                if (tries == 128) return cm_fail("AutoSwordSwing", 0, "random selection did not terminate");
            } else mode = -mode-2;
            if (mode < 0 || mode >= 11)
                return cm_fail("AutoSwordSwing", (uint32_t)mode, "invalid initial mode");
            p->reset_pos = a->pc_rules.auto_s_init_pos[mode]; p->auto_s_reset = 1;
        }
        p->auto_swinging = 1; p->auto_s_time = ticks+3;
    }
    p->hand_x = col*64-64; p->hand_y = 64-row*64; p->auto_s_mode = mode;
    return 0;
}

static int hand_stance(ai_state *a, sim_state *s, world *w)
{
    ai_pc_state *p = &a->pc;
    world_person *person = &w->persons[w->player_person];
    int old = low_word(person->unk_0040), current, matrix, x, y;
    if (!p->fire) return 0;
    p->auto_update_flag = !after(p->auto_s_time, s->timer_ticks);
    if (p->auto_update_flag) p->auto_s_time = s->timer_ticks+3;
    if (p->auto_s_flag && auto_sword(a, w, (unsigned)old, s->timer_ticks)) return -1;
    set_high(&person->unk_0040, old);
    if (high_word(person->unk_0044)) {
        if (p->auto_update_flag) set_high(&person->unk_0044, high_word(person->unk_0044)-1);
    } else {
        x = p->hand_x < -32 ? 0 : p->hand_x >= 32 ? 2 : 1;
        y = p->hand_y < -32 ? 2 : p->hand_y < 32 ? 1 : 0;
        set_low(&person->unk_0040, y*3+x);
    }
    current = low_word(person->unk_0040);
    if (old < 0 || old >= 9 || current < 0 || current >= 9)
        return cm_fail("Hand2Hand", (uint32_t)current, "invalid hand position");
    if (current != old) set_low(&person->unk_0048, a->pc_rules.mat_idx[old][current]);
    if (p->hand_x < -96) p->hand_x = -96;
    else if (p->hand_x > 95) p->hand_x = 95;
    if (p->hand_y < -96) p->hand_y = -96;
    else if (p->hand_y > 95) p->hand_y = 95;
    x = ((p->hand_x+96)%64)/8; y = ((p->hand_y+96)%64)/8;
    matrix = low_word(person->unk_0048);
    if (matrix < 0 || matrix >= 6)
        return cm_fail("Hand2Hand", (uint32_t)matrix, "invalid hand collision matrix");
    if (a->pc_rules.pos_mat[matrix][y][x]) set_low(&person->unk_0040, old);
    else set_low(&person->unk_0048, 0);
    return 0;
}

static int pc_ready(const ai_state *a, const sim_state *s, const world *w)
{
    if (!a || !s || !w || !a->events || !a->tables_loaded ||
        w->player_person >= w->person_count ||
        w->player_object >= w->object_count)
        return cm_fail("PCFire", 0, "player combat is not initialized");
    return 0;
}

/* HandleNextFrame1E4A0..1E768: commit equipment and reset the input latch
 * in this frame, not at the next Hand2Hand/update pass. */
int ai_pc_select(ai_state *a, const sim_state *s, const world *w, unsigned kind)
{
    events_state *e;
    int mode;
    if (pc_ready(a, s, w)) return -1;
    if (kind > 3) return cm_fail("selector", kind, "invalid selection kind");
    e = a->events; mode = s->pc_mode; /* Modern direct selection may still hold RMB. */
    if ((kind == 1 && mode != 1 && mode != 2) || (kind == 2 && mode != 5))
        return cm_fail("selector", (uint32_t)mode, "selected mode does not match item kind");
    if ((kind == 1 && (mode == 1 || mode == 2)) ||
        (kind == 2 && s->previous_pc_mode != 5)) {
        a->pc.last_mode = 0;
        if (equip(a, w, mode)) return -1;
    } else {
        if (kind == 2) {
            if (e->spell < 0 || e->spell >= 16)
                return cm_fail("selector/power", (uint32_t)e->spell, "invalid selected power");
            a->pc.glow_set = a->pc_rules.glow_set[e->spell];
        }
        if (reset_aim(a, mode)) return -1;
    }
    a->pc.fire = a->pc.cooldown = a->pc.latch = 0;
    a->pc.mouse_hand_flag=0;
    if(kind) a->pc.cursor_flags=3;
    return 0;
}

/* ClipGloveAim1B4EC. The caller initializes the per-frame step to four. */
void ai_pc_glove_aim(ai_state *a, int32_t x, int32_t y)
{
    if(y<20) y=20;else if(y>152) y=152;
    if(y<64) a->pc.sword_aim_step=ai_add32(a->pc.sword_aim_step,3);
    else if(y>108) a->pc.sword_aim_step=ai_sub32(a->pc.sword_aim_step,3);
    if(x<-73) x=-73;else if(x>110) x=110;
    if(x<-12) a->pc.sword_aim_step=ai_sub32(a->pc.sword_aim_step,1);
    else if(x>49) a->pc.sword_aim_step=ai_add32(a->pc.sword_aim_step,1);
    a->pc_aim.aim_x=x;a->pc_aim.aim_y=y;
}

/* ProcessInputEvents14EED/14FAA: the non-running jump-key hand mode. */
int ai_pc_hand_mode(ai_state *a, sim_state *s, const world *w, int enter)
{
    if(pc_ready(a,s,w)) return -1;
    if(enter) { s->previous_pc_mode=s->pc_mode;s->pc_mode=3; }
    else s->pc_mode=s->previous_pc_mode;
    if(reset_aim(a,s->pc_mode)) return -1;
    a->pc.fire=a->pc.cooldown=0;a->pc.latch=1;
    if(!enter) { a->pc.mouse_hand_flag=0;a->pc.cursor_flags|=1; }
    if(enter && s->mouse_present) {
        a->pc.mouse_hand_flag=1;
        platform_mouse_warp((160+a->pc_aim.aim_x)*2,(200-a->pc_aim.aim_y)*2);
    }
    return 0;
}

/* ProcessInputEvents0x14754 precedes acceleration, water and hit response. */
int ai_pc_prepare_input(ai_state *a, sim_state *s, world *w)
{
    int mode;
    ai_pc_state *p;
    if (pc_ready(a, s, w)) return -1;
    mode = combat_mode(s); p = &a->pc;
    if (mode!=3 && equip(a, w, mode)) return -1;
    if(mode==3) { s->movement_request=0;s->flight_action=1; }
    /* HandleNextFrame 0x1BA49: the four glow frames use the 2-tick gate. */
    if (s->bp_limit_flag) p->glow_frame = (p->glow_frame+1)%4;
    if (mode == 7 && s->vehicle_type) {
        /* HandleKeyboardEvents 0x13208..0x13276. S is consumed; A/D held. */
        if (s->kb_scan_status[0x1f]) {
            s->kb_scan_status[0x1f] = 0;
            p->air_car_spread = 0;
        }
        if (s->kb_scan_status[0x1e]&1) {
            p->air_car_spread += 128;
            if (p->air_car_spread > 0x400) p->air_car_spread = 0x400;
        }
        if (s->kb_scan_status[0x20]&1) {
            p->air_car_spread -= 128;
            if (p->air_car_spread < -0x800) p->air_car_spread = -0x800;
        }
    }
    keyboard_hand(p, s, mode);
    if(s->mouse_present && p->mouse_hand_flag) {
        p->hand_x=s->mouse_hand_x;p->hand_y=s->mouse_hand_y;
    }
    fire_input(a, s, &w->persons[w->player_person], mode);
    player_loops(a, s, w, mode);
    return 0;
}

/* 0x1CF71..0x1D6xx: hit/aim/sword state precedes every MovePerson branch. */
int ai_pc_prepare_motion(ai_state *a, sim_state *s, world *w)
{
    int mode;
    if (pc_ready(a, s, w)) return -1;
    mode = combat_mode(s);
    if (ai_pc_hit_response(a, s, w) || prepare_aim(a, s, mode) ||
        (mode == 2 && hand_stance(a, s, w))) return -1;
    if(mode==3 && !a->events->selector_kind && !a->events->selector_request) {
        int32_t x=a->pc_aim.aim_x,y=a->pc_aim.aim_y;
        int32_t hx=a->pc.hand_x,hy=a->pc.hand_y;
        if(s->mouse_present) { x=hx;y=ai_sub32(100,hy); }
        else {
            if(hx<-8) x=ai_add32(x,ai_add32(hx,8)/2);
            else if(hx>8) x=ai_add32(x,ai_sub32(hx,8)/2);
            if(hy<-8) y=ai_add32(y,ai_add32(hy,8)/2);
            else if(hy>8) y=ai_add32(y,ai_sub32(hy,8)/2);
        }
        ai_pc_glove_aim(a,x,y);
    }
    return 0;
}

/* Actual PCLaunchMissile dispatch follows movement at0x1D841..0x1E781. */
int ai_pc_input(ai_state *a, sim_state *s, world *w)
{
    int mode;
    ai_pc_state *p;
    if (pc_ready(a, s, w)) return -1;
    mode = combat_mode(s); p = &a->pc;
    if(mode==3 && (p->fire || p->latch))
        p->sword_aim_step=ai_add32(p->sword_aim_step,9); /*1E445*/
    if ((mode == 1 && ranged_fire(a, s, w)) ||
        (mode == 5 && spell_fire(a, s, w)) ||
        (mode == 7 && tank_fire(a, s, w))) return -1;
    /* HandleNextFrame 0x1E781: delayed linking missiles precede NPC updates. */
    if (p->magic_link_ticks) {
        int object;
        if (w->person_count <= 82 || (object = w->persons[82].object) < 0 ||
            (unsigned)object >= w->object_count)
            return cm_fail("PCFire/link", 82, "invalid linked target");
        if (ai_launch_missile(a, s, w, w->player_object, (unsigned)object, 33) < 0) return -1;
        if ((p->magic_link_ticks%2) &&
            ai_launch_missile(a, s, w, w->player_object, w->player_object, 33) < 0) return -1;
        --p->magic_link_ticks;
    }
    return 0;
}

/* RenderBells: defer hand counters until after the read-only HUD, then run
 * low-HP respiration 0x1928B..0x19340 and energy recovery 0x19340..0x193B2. */
int ai_pc_draw_advance(ai_state *a, const sim_state *s, world *w,
                        const cm_draw_clock *clock)
{
    ai_pc_state *p;
    events_state *e;
    uint32_t ticks;
    int mode, maximum, interval, hp, max_hp;
    if (!a || !s || !w || !a->events || !w->person_count || !clock || !clock->read)
        return cm_fail("RenderBells", 0x1928b, "missing player state");
    p = &a->pc; e = a->events; mode = combat_mode(s);
    /* RenderBells 0x18A05/0x190F1: per-draw effects, not timer-gated. */
    p->unk_1444EC = 0;
    if (a->pc_flash) a->pc_flash = ai_sub32(a->pc_flash, 1);
    /* The hand was drawn before StatusMonitor and respiration. */
    if ((mode == 1 || mode == 5 || (mode == 7 && s->vehicle_type)) &&
        p->muzzle_mode && !(mode == 7 && p->muzzle_mode < 0)) {
        p->muzzle_count = ai_add32(p->muzzle_count, 1);
        p->muzzle_mode += p->muzzle_mode > 0 ? -1 : 1;
    }
    /* PERSON0 is the foot body even while player_person names a vehicle.
     * Death dispatch is owned by the main loop. Chance precedes CheckSound. */
    hp = low_word(w->persons[0].unk_0038);
    max_hp = high_word(w->persons[0].unk_0038);
    if (hp > 0 && hp < max_hp/2 &&
        (p->sword_hum_slot == -1 || p->sword_hum_sound != 1) &&
        !world_rnd(&w->rng, hp/2+2) && snd_done(a->pc_sound, 3))
        a->pc_sound = snd_play(3, 16384-world_rnd(&w->rng, 8192), 32768);
    maximum = signed16((uint32_t)e->max_energy);
    interval = 65-maximum/10;
    if (interval < 6) interval = 6;
    p->regen_interval = interval; /* 19362..1936D, including full energy. */
    if (clock->read(clock->context, 0x19377, &ticks)) return -1;
    if (after(ticks, p->sp_regen_timer)) {
        if (signed16((uint32_t)e->energy) < maximum)
            e->energy = signed16((uint32_t)e->energy+1);
        if (clock->read(clock->context, 0x193a4, &ticks)) return -1;
        p->sp_regen_timer = ticks+(uint32_t)interval;
    }
    return 0;
}

static int hand_frame(ai_state *a, world_person *person)
{
    int position = low_word(person->unk_0040);
    int state = high_word(person->unk_0044) ? high_word(person->unk_0048) : low_word(person->unk_003C);
    if (position < 0 || position >= 9 || state < 0 || state >= 21)
        return cm_fail("Hand2Hand", (uint32_t)state, "invalid swing frame");
    a->pc.sword_frame = a->pc_rules.swing_frame[position][state];
    return 0;
}

/* Xox::Hand2Hand 0x1A298..0x1A6C1, called AFTER UpdateObjects. */
int ai_pc_hand2hand(ai_state *a, sim_state *s, world *w)
{
    world_person *p;
    int current, previous, state, old_state;
    int mode = combat_mode(s);
    if (mode != 2) return 0;
    if (!a->pc.fire) { a->pc.sword_frame = 8; return 0; }
    if (w->player_person >= w->person_count || !a->events)
        return cm_fail("Hand2Hand", w->player_person, "invalid player");
    a->pc_aim.render_x = a->pc_aim.render_y = 0;
    a->pc.bob_phase = 0;
    p = &w->persons[w->player_person];
    current = low_word(p->unk_0040); previous = high_word(p->unk_0040);
    if (current < 0 || current >= 9 || previous < 0 || previous >= 9)
        return cm_fail("Hand2Hand", (uint32_t)current, "invalid hand grid");
    state = low_word(p->unk_003C);
    if (current == previous) {
        if (state) {
            int delay = low_word(p->unk_0044);
            if (delay) {
                if (a->pc.auto_update_flag) set_low(&p->unk_0044, delay-1);
            } else {
                set_high(&p->unk_003C, 0);
                set_low(&p->unk_003C, state == 6 ? 0 : 6);
                if (state != 6) set_low(&p->unk_0044, 4);
            }
        }
        return hand_frame(a, p);
    }
    old_state = state;
    set_high(&p->unk_003C, old_state);
    state = a->pc_rules.swing_mode[previous][current];
    if ((a->events->hh_weapon == 1 || a->events->hh_weapon == 2) && state == 4) state = 0;
    switch (state) {
    case 7: state = old_state == 1 ? 15 : 6; break;
    case 8: state = old_state == 5 ? 19 : 6; break;
    case 9: state = old_state == 1 ? 15 : 14; break;
    case 10: state = old_state == 4 ? 18 : 0; break;
    case 11: state = old_state == 18 ? 0 : 6; break;
    case 12: state = old_state == 2 ? 16 : 6; break;
    case 13: state = old_state == 3 ? 17 : 6; break;
    }
    set_low(&p->unk_003C, state);
    if (hand_frame(a, p)) return -1;
    set_low(&p->unk_0044, 4);
    if (state >= 14) {
        world_person *target;
        int found, kind;
        unsigned sword = state == 18 || state == 16 || state == 17 ? 1u : (unsigned)current%3;
        (void)snd_play(33, 32767, 32768);
        found = ai_h2h_target(a, s, w, (unsigned)p->object, sword);
        if (found < 0) return -1;
        if (!found) { set_low(&p->unk_003C, 6); return 0; }
        if ((unsigned)found >= w->person_count)
            return cm_fail("Hand2Hand", (uint32_t)found, "invalid melee target");
        target = &w->persons[found]; kind = ai_person_kind(a, w, (unsigned)found);
        if (kind == 6 || kind == 5 || kind == 14) set_low(&target->unk_003C, 6);
        if (low_word(target->unk_003C)) {
            (void)snd_play(31, 32767, 32768);
            if (ai_face_shooter(a, w, w->player_object, target->object)) return -1;
            set_low(&p->unk_003C, 6); set_low(&p->unk_0044, 4);
            if (low_word(target->unk_003C) == 6) set_low(&target->unk_003C, 0);
            else { set_low(&target->unk_003C, 6); set_low(&target->unk_0044, 1); }
        } else {
            uint32_t packed;
            ai_dice dice;
            int damage, aim = signed16(w->objects[w->player_object].heading+(uint16_t)a->h2h_angle);
            if (state < 0 || state >= 21)
                return cm_fail("Hand2Hand", (uint32_t)state, "invalid swing damage");
            if (ai_pc_launch_missile(a, s, w, 4, target->object,
                                     a->h2h_distance, (int16_t)aim) < 0) return -1;
            packed = (a->events->hh_dice & UINT32_C(0xffffff00)) |
                     ((uint32_t)a->pc_rules.swing_damage[state] & 255);
            a->events->hh_dice = packed;
            dice.count = (uint8_t)packed; dice.sides = (uint8_t)(packed >> 8);
            dice.add = (int16_t)signed16(packed >> 16);
            damage = ai_do_dice(&w->rng, dice); if (damage < 1) damage = 1;
            if (ai_damage_monster(a, w, (unsigned)found, damage, w->player_object)) return -1;
            target->unk_001E = 2;
            if (ai_face_shooter(a, w, w->player_object, target->object)) return -1;
            set_high(&p->unk_0048, state);
            set_high(&p->unk_0044, state == 18 ? 8 : 4);
            set_low(&p->unk_003C, 0);
            set_low(&target->unk_003C, 6); set_low(&target->unk_0044, 4);
        }
    }
    return 0;
}
