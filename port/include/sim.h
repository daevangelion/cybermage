#ifndef CM_SIM_H
#define CM_SIM_H

#include "platform.h"
#include "world.h"

#define SIM_SCAN_COUNT 128
#define SIM_FRAME_TICKS_MAX 2
#define SIM_APERTURE_MAX 96
#define SIM_PATH_RETRIES_MAX 11
#define SIM_JUMP_STEPS 16
#define SIM_NO_SURFACE (-99) /* CM_HI.EXE initial globals09260C..092618. */

struct ai_state;
struct events_state;

typedef struct {
    struct ai_state *ai;
    struct events_state *events;
    void (*collision_observe)(void *context, const world *w, unsigned object,
                               int32_t x, int32_t y);
    void *collision_context;
    int32_t pc_mode, previous_pc_mode;
    int modern_aim;
    int32_t vehicle_type, flight_mode, flight_active, flight_unk_144B48;
    int32_t unk_131418; /* Flight height above the local supporting floor. */
    int32_t flight_action, key_hand_y, fly_throttle, fly_curr_throttle;
    int32_t fire_check, fire_damage, fire_tile_panel, fire_tile_art;
    int32_t swim_check, swim_damage, swim_panel, swim_art;
    int32_t swim_loop;
    uint32_t swim_damage_time;
    uint8_t kb_scan_status[SIM_SCAN_COUNT];
    uint8_t kb_latch;
    uint8_t bios_shift, shift_lock;
    uint32_t timer_ticks, previous_ticks, bp_limit_time, bp_limit_flag;
    int32_t frame_ticks;
    int32_t replay_delta; /* -1: platform clock; otherwise recorded 0..2. */
    int32_t key_rot_rate;                 /* KeyRotRate */
    uint16_t movement_angle;               /* Xox+130D10 */
    int32_t movement_request, yaw_request; /* Xox+144B08/144B0C */
    int running; /* ProcessInputEvents running gate after primary-fire eligibility. */
    int primary_fire; /* ProcessInputEvents+144B10, sampled once by Sim. */
    int mouse_present, joystick_present; /* Xox+E3510/+E3518, classic only. */
    int32_t mouse_hand_x, mouse_hand_y; /* HandleMouseEvents1434A..143BC. */
    int32_t unk_144ae8, unk_144aec, unk_144af0, unk_144af4;
    int32_t unk_144af8, unk_144afc, unk_144b00;
    int32_t unk_144b1c, unk_144b20, unk_144b24;
    int32_t unk_144b28, unk_144b2c, unk_144b30;
    int32_t unk_144b34, unk_144b38, unk_144b3c;
    int32_t unk_144b50;
    int32_t body_height;                    /* Xox+130D1C */
    int32_t rise_dir;
    int32_t collision_level, collision_mode, support_object, no_openings;
    unsigned collision_flags;
    uint16_t save_pc_vector;
    int32_t unk_130d18;
    unsigned kb_side_toggle, kb_side_left, kb_side_right;
    unsigned kb_jump, kb_crouch, kb_look_down, kb_look_up, kb_look_center;
    unsigned kb_side_toggle_bit, kb_side_left_bit, kb_side_right_bit;
    unsigned kb_jump_bit, kb_crouch_bit, kb_look_down_bit, kb_look_up_bit, kb_look_center_bit;
} sim_state;

void sim_init(sim_state *s, uint32_t timer_ticks);
int sim_start_scene(sim_state *s, world *w);
void sim_scan(sim_state *s, uint8_t scan);
void sim_input(sim_state *s, const cm_input *input);
int sim_modern_input(sim_state *s, world *w, const cm_input *input);
int32_t sim_clock(sim_state *s, uint32_t timer_ticks);
uint16_t sim_turn(uint16_t current, uint16_t target, int32_t step);
int sim_frame(sim_state *s, world *w, const cm_input *input, uint32_t timer_ticks);
int sim_path_check(const sim_state *s, const world *w, int32_t x, int32_t y);
int sim_coll_flat(world *w, unsigned object, int32_t x, int32_t y);
int sim_coll_top_flat(sim_state *s, world *w, unsigned object, int32_t old_z, int32_t height);
int sim_coll_swim_flat(const world *w, unsigned object, int32_t panel, int32_t art);
int32_t sim_crouch_top_flat(const sim_state *s, const world *w, unsigned object,
                           int32_t adjustment, int32_t x, int32_t y);
int sim_move_person(sim_state *s, world *w, unsigned object,
                    int32_t yaw, int32_t speed, int32_t vertical);
int sim_move_monster(sim_state *s, world *w, unsigned object,
                     int32_t speed, int32_t vertical);
int sim_clear_object(sim_state *s, world *w, unsigned object);
int sim_jumble_bound(sim_state *s, world *w, unsigned object,
                     int32_t dx, int32_t dy, int collide);
/* CollFireFlat returns -1 hit, 0 clear, -2 invalid scene data. */
int sim_coll_fire_flat(world *w, unsigned object, int32_t old_z);
#define SIM_COLL_ERROR INT32_MIN
int sim_coll_fire(sim_state *s, world *w, unsigned missile);
void sim_save_export(const sim_state *s, uint32_t values[29]);
int sim_save_import(sim_state *s, const uint32_t values[29]);
int sim_save_fire_import(sim_state *s, const uint32_t values[4]);
#define SIM_AUTO_PREFIX 207798u
int sim_auto_pack(const uint8_t *save, uint32_t size, uint8_t *out,
                    uint32_t capacity, uint32_t *written);
int sim_auto_unpack(uint8_t *save, uint32_t size, const uint8_t *data,
                      uint32_t length);

#endif
