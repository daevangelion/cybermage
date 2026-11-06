#ifndef CM_AI_H
#define CM_AI_H

#include "sim.h"
#include "exe.h"
#include "ui.h"
#include "target.h"

#define AI_MONSTER_MAX 109
#define AI_MONSTER_DISK_SIZE 276
#define AI_GSX_MAX 55
#define AI_GSX_DISK_SIZE 58
#define AI_SEQUENCE_MAX 56
#define AI_SEQUENCE_DISK_SIZE 4532
#define AI_ANIM_STATES 30
#define AI_ANIM_STEPS 40
#define AI_ANIM_FRAMES 20
#define AI_FACTIONS 12
#define AI_SPELL_MAX 64
#define AI_SOUL_MAX 10
#define AI_MISSILE_MAX 12
#define AI_DYNAMIC_FIRST 2378
#define AI_PERSON_DISK_SIZE 351
#define AI_MISSILE_TYPES 53

typedef struct { uint8_t count, sides; int16_t add; } ai_dice;
typedef struct {
    int16_t gsx;
    uint8_t attack[20], unk_002C, unk_002D;
    int16_t unk_002E;
    int32_t range, turn_speed;
    uint8_t accuracy, aggression, kind, faction;
    int8_t rank[AI_FACTIONS];
    uint16_t sounds[8];
    int32_t sound_range, melee_range, unk_0060;
    int16_t unk_0064, unk_0066, unk_0068, unk_006A;
    uint8_t unk_006C;
    ai_dice melee_damage;
    int32_t fire_range, unk_0078;
    int16_t unk_007C, unk_007E;
    uint8_t fire, unk_0081, unk_0082, unk_0083;
    int16_t unk_0084;
    int32_t unk_0088, unk_008C, unk_0090;
    int16_t unk_0094, unk_0096;
    uint8_t throw_type, unk_0099, unk_009A, unk_009B;
    int16_t unk_009C;
    ai_dice unk_009E[5];
    uint8_t spells[16];
    unsigned spell_count;
    uint8_t resistance[8], flags;
    int32_t unk_00D0;
} ai_monster;

typedef struct {
    uint8_t start[AI_ANIM_STATES], step[AI_ANIM_STEPS];
    int32_t radius[AI_ANIM_FRAMES], height[AI_ANIM_FRAMES];
    int32_t unk_011C[AI_ANIM_FRAMES], unk_016C[AI_ANIM_FRAMES];
    int32_t speed[AI_ANIM_FRAMES], vertical[AI_ANIM_FRAMES];
    uint8_t image[AI_ANIM_FRAMES];
} ai_sequence;

typedef struct {
    int32_t active, object, vertical, ticks; /* +0,+4,+8,+C */
    int32_t type, target, source, frame;    /* +10,+14,+18,+1C */
    int32_t sound, speed, variant, radius; /* +20,+24,+28,+2C */
} ai_missile;
typedef struct {
    int32_t fxo, sound, volume, aim, speed;
} ai_missile_data;
typedef struct {
    int32_t active, object, prop, type, value, lifetime, frame;
} ai_soul;

typedef struct {
    int16_t pistol_angle, pistol_angoffset;
    int32_t pistol_lr, pistol_lroffset, pistol_ud, pistol_udoffset;
    int32_t pistol_fall, pistol_falloffset, pistol_xoffset, pistol_boffset;
    int32_t w_max_leftaimx, w_leftaimx, w_max_rightaimx, w_rightaimx;
    int32_t w_max_topaimy, w_topaimy, w_max_botaimy, w_botaimy;
    int32_t aim_x, aim_y, tank_aim_y; /* Xox+144ABC/144AC0/144AC4 */
    int32_t auto_enable, tank_auto;
    uint32_t reset_auto_aim_time;
    int32_t weapon_aim_step, tank_aim_step; /* Xox+144A48 / +144A50 */
    int32_t tank_udoffset, tank_ud, tank_fall, tank_falloffset;
    int32_t grenade_aim_center; /* CM_HI.EXE0x93CB4 = CenterAimY[10]. */
    unsigned hand_type; /* Xox+144A40, CenterAim table index */
    int32_t render_x, render_y; /* Xox+144AD8/+144ADC visual offsets */
} ai_pc_aim;

typedef struct {
    int32_t center_aim_x[16], center_aim_y[16];
    int32_t hand_bound[16][8], trajectory[16][2][8], air_car_trajectory[8];
    int32_t ammo_type[10], spell_power[16], glow_set[16];
    int32_t swing_damage[21], swing_mode[9][9], mat_idx[9][9];
    int16_t swing_frame[9][21];
    uint8_t pos_mat[6][8][8];
    int16_t auto_s_pos[11][4], auto_s_init_mode[9][3], auto_s_init_pos[11];
} ai_pc_rules;

typedef struct {
    int32_t fire, cooldown, latch; /* Xox+144AB0/144AB4/144AB8 */
    int32_t hand_x, hand_y; /* +144AA8/+144AAC */
    int32_t bob_phase; /* +144AD4 */
    int32_t sword_aim_step, sword_frame; /* +144A44/+144A4C */
    int32_t muzzle_frame, muzzle_mode, muzzle_count; /* +144A5C/60/64 */
    int32_t underwater, shot_count, loop_sound; /* +144A24/2C/28 */
    int32_t sword_hum_slot, sword_hum_sound; /* globals9261C/1E3964 */
    int32_t glow_set, glow_frame; /* +1444F8/+1444F4 */
    int32_t unk_1444EC;
    int32_t hit_camera_z; /* Current frame's +4B170 increment from +1444FC. */
    int32_t magic_link_ticks; /* +144AE4 */
    int32_t key_hand_x, key_hand_y;
    int32_t auto_s_flag, auto_swinging, auto_s_reset, reset_pos, auto_s_mode;
    int32_t auto_update_flag;
    uint32_t auto_s_time, spell_timer, tank_timer;
    uint32_t sp_regen_timer; /* global spRegenTimer206834, survives scene loads */
    int32_t last_mode, last_weapon;
    unsigned kb_fire, kb_fire_bit;
    int32_t air_car_spread; /* ACarSpreadAngle, original signed16 */
} ai_pc_state;

typedef struct ai_state {
    struct events_state *events;
    ai_monster monsters[AI_MONSTER_MAX];
    ai_sequence sequences[AI_SEQUENCE_MAX];
    int16_t gsx_sequence[AI_GSX_MAX];
    unsigned monster_count, sequence_count, gsx_count;
    uint32_t timer_ticks, animation_deadline, fast_animation_deadline;
    int32_t acquisition_phase;
    unsigned missile_count;
    uint16_t missile_objects[CM_OBJECT_MAX];
    target_list targets;
    ai_missile missiles[AI_MISSILE_MAX];
    ai_soul souls[AI_SOUL_MAX];
    ai_missile_data missile_types[AI_MISSILE_TYPES];
    int32_t fire_missile[12], throw_missile[3], spell_missile[16];
    int32_t sword_range[3];
    ai_dice zone_dice[24][3];
    int32_t zone_radius[34][3]; /* Type 11 reads adjacent EXE rows through 33. */
    int tables_loaded;
    int32_t swim_damage, swim_check, mon_swim_damage_time;
    int32_t swim_panel, swim_art;
    int32_t difficulty;
    int32_t pc_sound, body_mark_count[6];
    int32_t gem_count_down, gem_count_down_timer;
    int32_t unk_144AE0;
    uint32_t missile_fade_time;
    int32_t pc_learn_spell, pc_flash;
    unsigned undefined_distance_person; /* Verification: 0=none; first NPC using an undefined original local. */
    int32_t undefined_distance_value; /* That actor's defined cached-distance fallback. */
    int32_t h2h_distance;
    int16_t h2h_angle;
    ai_pc_aim pc_aim;
    ai_pc_state pc;
    ai_pc_rules pc_rules;
} ai_state;

/* Shared Xox player feedback: flash+1444E8, effect state+1444EC. */
static inline void ai_pc_set_flash(ai_state *a, int32_t value, int only_if_clear)
{
    if(!only_if_clear || !a->pc_flash) a->pc_flash=value;
}
static inline void ai_pc_set_shake(ai_state *a, int32_t value)
{
    a->pc.unk_1444EC=value;
}

int ai_init(ai_state *a, world *w);
void ai_start_clock(ai_state *a, uint32_t timer_ticks);
int ai_set_difficulty(ai_state *a, world *w, unsigned difficulty);
int ai_init_pc(struct events_state *e, sim_state *s, world_rng *rng,
                const cm_exe *exe, unsigned start_scene, uint32_t timer_ticks);
void ai_save_pc_info(struct events_state *e, const world *w);
void ai_restore_pc_info(const struct events_state *e, world *w);
int ai_load_tables(ai_state *a, const char *asset_dir);
int ai_pc_load_tables(ai_state *a, const cm_exe *exe);
int ai_reset_aim(ai_state *a, const sim_state *s);
int ai_pc_equip(ai_state *a, const sim_state *s, const world *w);
void ai_pc_hand_bob(ai_state *a, const sim_state *s);
int ai_pc_input(ai_state *a, sim_state *s, world *w);
int ai_pc_hit_response(ai_state *a, sim_state *s, world *w);
int ai_pc_hand2hand(ai_state *a, sim_state *s, world *w);
int ai_pc_draw_advance(ai_state *a, const sim_state *s, world *w);
int ai_load_scene_definitions(ai_state *a, world *w);
int ai_save_import(ai_state *a, world *w, const ui_save_state *save);
void ai_save_export(const ai_state *a, ui_save_state *save);
/* Replay-only, caller opts in and logs provenance; imports PERSON +0x24 only. */
int ai_import_person_undefined(world *w, const uint8_t *data, uint32_t size);
int ai_init_persons(ai_state *a, world *w);
int ai_set_person(ai_state *a, world *w, unsigned person, unsigned type,
                   unsigned state, int32_t x, int32_t y, int32_t z,
                   int32_t floor_z, uint16_t heading);
/* Original samples IRQ0 separately for the fast and normal animation gates. */
int ai_update_objects(ai_state *a, sim_state *s, world *w,
                       uint32_t normal_ticks, uint32_t fast_ticks);
int ai_monster_blocked(ai_state *a, sim_state *s, world *w, unsigned object,
                       unsigned collisions);
int ai_init_missiles(ai_state *a, world *w);
int ai_init_soul_fire(ai_state *a, world *w);
int ai_make_soul_fire(ai_state *a, world *w, unsigned source, int32_t type,
                      int32_t value, int32_t lifetime, int on_floor);
void ai_update_soul_fire(ai_state *a, sim_state *s, world *w);
int ai_monster_decode(ai_monster *m, const uint8_t *data, uint32_t size);
int ai_sequence_decode(ai_sequence *s, const uint8_t *data, uint32_t size);
int32_t ai_do_dice(world_rng *rng, ai_dice d);
int ai_person_kind(const ai_state *a, const world *w, unsigned person);
int32_t ai_set_auto_target(const ai_state *a, const world *w);
int32_t ai_h2h_target(ai_state *a, const sim_state *s, const world *w,
                      unsigned source, unsigned sword);
void ai_make_missile_list(ai_state *a, const world *w);
void ai_track_target(world *w, unsigned object, unsigned other, int offset);
int ai_sight_person(const ai_state *a, sim_state *s, world *w,
                    unsigned source, unsigned target, int32_t range);
int ai_find_target(const ai_state *a, sim_state *s, world *w,
                   unsigned source, int32_t range);
void ai_update_pc_object(world *w, int tick);
int ai_move_object(ai_state *a, sim_state *s, world *w, unsigned person,
                   int tick, int moving);
int ai_dragon_fly(ai_state *a, sim_state *s, world *w, unsigned person,
                  unsigned target);
int ai_face_shooter(const ai_state *a, world *w, unsigned shooter, int32_t target);
int ai_blast_physics(const ai_state *a, world *w, unsigned target,
                     unsigned source, int32_t force);
int ai_damage_monster(ai_state *a, world *w, unsigned person,
                      int32_t damage, int32_t shooter);
int ai_damage_person(ai_state *a, world *w, unsigned person, int32_t damage,
                     int32_t armor_percent, int32_t erosion_percent);
int ai_launch_missile(ai_state *a, sim_state *s, world *w, unsigned source,
                      unsigned target, unsigned type);

int ai_pc_launch_missile(ai_state *a, sim_state *s, world *w, unsigned type,
                         int32_t aim_x, int32_t aim_y, int16_t aim);
int ai_update_missile(ai_state *a, sim_state *s, world *w);
int ai_freeze_monster(ai_state *a, sim_state *s, world *w, unsigned object,
                       unsigned tier, unsigned dice_row);
int ai_radial_damage(ai_state *a, sim_state *s, world *w, unsigned source,
                      unsigned radius_zone, int32_t shooter, unsigned dice_row,
                      unsigned resistance, int32_t force);
int ai_pc_zone_radius(const world *w, unsigned source, unsigned tile);
int ai_pc_radial_damage(ai_state *a, sim_state *s, world *w, unsigned source,
                         unsigned radius_zone, unsigned dice_row, int double_hit,
                         int32_t armor, int32_t erosion);

#endif
