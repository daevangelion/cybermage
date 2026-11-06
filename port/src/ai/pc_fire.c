#include "combat_internal.h"
#include "audio.h"
#include "events.h"

#include <string.h>

static int signed16(unsigned value)
{
    value &= 65535;
    return value < 32768 ? (int)value : (int)value-65536;
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
    q->hand_type = 1;
    a->pc.loop_sound = -1; /* InitPC/first scene, original +144A28. */
    a->pc.kb_fire = 0x39; a->pc.kb_fire_bit = 1; /* ReKeyDefaults 0x52644. */
    return 0;
}

/* Xox::ResetAim, CM_HI.EXE 0x12D60. */
int ai_reset_aim(ai_state *a, const sim_state *s)
{
    unsigned type = s->pc_mode == 1 ? a->pc_aim.hand_type : s->pc_mode == 5 ? 1u : 0u;
    if (type >= 16) return cm_fail("ResetAim", type, "invalid player hand type");
    a->pc_aim.aim_x = a->pc_rules.center_aim_x[type];
    a->pc_aim.aim_y = a->pc_rules.center_aim_y[type];
    return 0;
}
