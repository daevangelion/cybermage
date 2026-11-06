#include "ai.h"
#include "events.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ai_state a;
static world w;
static sim_state s;
static events_state e;
static int fixed_draw_time(void *context, uint32_t site, uint32_t *ticks)
{
    const sim_state *state = context;
    if (site != 0x19377 && site != 0x193a4)
        return cm_fail("PC draw fixture", site, "unexpected timer instruction");
    *ticks = state->timer_ticks;
    return 0;
}
static const cm_draw_clock draw_clock = {&s, fixed_draw_time};
typedef struct { uint32_t compare, deadline; unsigned calls; } split_draw_time;
static int split_draw_read(void *context, uint32_t site, uint32_t *ticks)
{
    split_draw_time *clock = context;
    if ((!clock->calls && site != 0x19377) ||
        (clock->calls == 1 && site != 0x193a4) || clock->calls > 1)
        return cm_fail("PC draw fixture", site, "wrong timer read order");
    *ticks = clock->calls++ ? clock->deadline : clock->compare;
    return 0;
}
static struct {
    unsigned count;
    int overflow;
    int32_t bound[2], value[2];
} observed;

static void rng_observe(void *context, const char *file, unsigned line,
                         int bounded, int32_t bound, int32_t value,
                         const uint16_t before[3], const uint16_t after[3])
{
    (void)context; (void)file; (void)line; (void)before; (void)after;
    if (!bounded) return;
    if (observed.count == 2) { observed.overflow = 1; return; }
    observed.bound[observed.count] = bound;
    observed.value[observed.count++] = value;
}

static void observe_begin(void)
{
    observed.count = 0; observed.overflow = 0;
    observed.bound[0] = observed.bound[1] = -1;
    observed.value[0] = observed.value[1] = -1;
    w.rng.observe = rng_observe;
}

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "PC fire check line %d\n", __LINE__); return 1; } } while (0)

static int setup(const char *path)
{
    cm_file file = {0};
    cm_exe exe;
    int result;
    if (platform_file_read(&file, path)) return -1;
    result = cm_exe_init(&exe, &file, path);
    if (!result) result = ai_pc_load_tables(&a, &exe);
    platform_file_release(&file);
    if (result) return -1;
    a.tables_loaded = 1; a.events = &e;
    a.pc_sound = -1;
    w.person_count = 1; w.object_count = 2;
    w.player_object = 1; w.player_person = 0;
    w.persons[0].object = 1; w.persons[0].unk_0038 = 40 | 40<<16;
    w.objects[1].person = 0; w.objects[1].active = 1;
    world_trig_init(&w.trig);
    sim_init(&s, 300);
    s.pc_mode = s.previous_pc_mode = 5;
    s.timer_ticks = 300; s.bp_limit_flag = 2;
    return ai_pc_equip(&a, &s, &w);
}

/* These direct routine fixtures supply already-sampled logical input. */
static int pc_step(void)
{
    if (ai_pc_prepare_input(&a, &s, &w) || ai_pc_prepare_motion(&a, &s, &w)) return -1;
    return ai_pc_input(&a, &s, &w);
}

static int checks(void)
{
    unsigned i;
    CHECK(a.pc_aim.hand_type == 1);
    CHECK(a.pc_aim.aim_x == a.pc_rules.center_aim_x[1]);
    CHECK(a.pc_aim.aim_y == a.pc_rules.center_aim_y[1]);
    s.pc_mode = 3; s.modern_aim = 1;
    a.pc_aim.aim_x = a.pc_aim.aim_y = 999;
    CHECK(!ai_reset_aim(&a, &s));
    CHECK(a.pc_aim.aim_x == a.pc_rules.center_aim_x[1]);
    CHECK(a.pc_aim.aim_y == a.pc_rules.center_aim_y[1]);
    s.modern_aim = 0;
    for (s.pc_mode = 1; s.pc_mode <= 5; s.pc_mode += 4) {
        int sign;
        for (sign = -1; sign <= 1; sign += 2) {
            a.pc.muzzle_mode = 3*sign; a.pc.muzzle_count = 0;
            for (i = 0; i < 3; ++i) {
                CHECK(!ai_pc_draw_advance(&a, &s, &w, &draw_clock));
                CHECK(a.pc.muzzle_mode == (2-(int)i)*sign);
                CHECK(a.pc.muzzle_count == (int)i+1);
            }
            CHECK(!ai_pc_draw_advance(&a, &s, &w, &draw_clock));
            CHECK(a.pc.muzzle_count == 3);
        }
    }
    s.pc_mode = 7; s.vehicle_type = 0;
    a.pc.muzzle_mode = 3; a.pc.muzzle_count = 0;
    CHECK(!ai_pc_draw_advance(&a, &s, &w, &draw_clock));
    CHECK(a.pc.muzzle_mode == 3 && !a.pc.muzzle_count);
    s.vehicle_type = 1;
    CHECK(!ai_pc_draw_advance(&a, &s, &w, &draw_clock));
    CHECK(a.pc.muzzle_mode == 2 && a.pc.muzzle_count == 1);
    s.kb_scan_status[0x1e] = 1;
    for (i = 0; i < 24; ++i) CHECK(!pc_step());
    CHECK(a.pc.air_car_spread == 0x400);
    s.kb_scan_status[0x1e] = 0; s.kb_scan_status[0x20] = 1;
    for (i = 0; i < 32; ++i) CHECK(!pc_step());
    CHECK(a.pc.air_car_spread == -0x800);
    s.kb_scan_status[0x20] = 0; s.kb_scan_status[0x1f] = 3;
    CHECK(!pc_step());
    CHECK(!a.pc.air_car_spread && !s.kb_scan_status[0x1f]);
    a.pc.air_car_spread = 0x400;
    s.kb_scan_status[0x1e] = s.kb_scan_status[0x20] = 1;
    CHECK(!pc_step());
    CHECK(a.pc.air_car_spread == 0x380);
    e.energy = 35; e.max_energy = 40; a.pc.sp_regen_timer = 0;
    s.timer_ticks = 300;
    CHECK(!ai_pc_draw_advance(&a, &s, &w, &draw_clock));
    CHECK(e.energy == 36 && a.pc.sp_regen_timer == 361);
    s.timer_ticks = 361;
    CHECK(!ai_pc_draw_advance(&a, &s, &w, &draw_clock));
    CHECK(e.energy == 36);
    s.timer_ticks = 362;
    CHECK(!ai_pc_draw_advance(&a, &s, &w, &draw_clock));
    CHECK(e.energy == 37 && a.pc.sp_regen_timer == 423);
    {
        split_draw_time time = {424, 425, 0};
        const cm_draw_clock split = {&time, split_draw_read};
        CHECK(!ai_pc_draw_advance(&a, &s, &w, &split));
        CHECK(time.calls == 2 && e.energy == 38 && a.pc.sp_regen_timer == 486);
        CHECK(a.pc.regen_interval == 61);
        time.compare = 486; time.deadline = 999; time.calls = 0;
        CHECK(!ai_pc_draw_advance(&a, &s, &w, &split));
        CHECK(time.calls == 1 && e.energy == 38 && a.pc.sp_regen_timer == 486);
        a.pc.sp_regen_timer = 423;
    }
    e.energy = e.max_energy = 999; s.timer_ticks = 424;
    CHECK(!ai_pc_draw_advance(&a, &s, &w, &draw_clock));
    CHECK(e.energy == 999 && a.pc.sp_regen_timer == 430);
    s.pc_mode = 2; s.bp_limit_flag = 0; a.pc_flash = 3;
    for (i = 0; i < 4; ++i) {
        a.pc.unk_1444EC = 2;
        CHECK(!ai_pc_draw_advance(&a, &s, &w, &draw_clock));
        CHECK(!a.pc.unk_1444EC);
        CHECK(a.pc_flash == (i < 3 ? 2-(int)i : 0));
    }
    s.running = 0; s.primary_fire = 1; s.kb_scan_status[0x39] = 1; a.pc.auto_s_flag = 0;
    CHECK(!pc_step());
    CHECK(a.pc.sword_hum_sound == 32 && a.pc.sword_hum_slot == -1);
    s.primary_fire = 0; s.running = 1; s.kb_scan_status[0x39] = 0; a.pc.key_swing_sword = 1;
    CHECK(!pc_step());
    CHECK(a.pc.fire == 1 && a.pc.sword_hum_sound == 1 && a.pc.sword_hum_slot == -1);
    s.pc_mode = 5; s.running = 1; s.kb_scan_status[0x39] = 0;
    CHECK(!pc_step());
    CHECK(a.pc.sword_hum_sound == 1 && a.pc.sword_hum_slot == -1);
    s.pc_mode = 7; s.vehicle_type = 0; s.running = 0;
    CHECK(!pc_step());
    CHECK(a.pc.sword_hum_sound == 61 && a.pc.sword_hum_slot == -1);
    {
        uint16_t before[3];
        w.persons[0].unk_0038 = 40<<16 | 19;
        w.person_count = 2; w.player_person = 1;
        w.rng.word[0] = 3000; w.rng.word[1] = 44; w.rng.word[2] = 29999;
        memcpy(before, w.rng.word, sizeof before);
        a.pc.sword_hum_slot = 0; a.pc.sword_hum_sound = 1;
        CHECK(!ai_pc_draw_advance(&a, &s, &w, &draw_clock));
        CHECK(!memcmp(before, w.rng.word, sizeof before));
        a.pc.sword_hum_slot = -1;
        CHECK(!ai_pc_draw_advance(&a, &s, &w, &draw_clock));
        CHECK(memcmp(before, w.rng.word, sizeof before));
        w.person_count = 0;
        CHECK(ai_pc_draw_advance(&a, &s, &w, &draw_clock) < 0);
        w.person_count = 1; w.player_person = 0;
    }
    /* Selector completion is immediate, including during modern RMB aiming. */
    s.pc_mode = 1; s.previous_pc_mode = 5; s.modern_aim = 1;
    e.weapon = 3;
    a.pc.glow_frame = 3;
    a.pc.fire = a.pc.cooldown = a.pc.latch = 1;
    CHECK(!ai_pc_select(&a, &s, &w, 1));
    CHECK(a.pc_aim.hand_type == 5 && a.pc.last_mode == 1 && a.pc.last_weapon == 3);
    CHECK(a.pc_aim.aim_x == a.pc_rules.center_aim_x[5]);
    CHECK(a.pc_aim.aim_y == a.pc_rules.center_aim_y[5]);
    CHECK(!a.pc.fire && !a.pc.cooldown && !a.pc.latch && a.pc.glow_frame == 3);
    s.pc_mode = 2; e.hh_weapon = 1;
    CHECK(!ai_pc_select(&a, &s, &w, 1));
    CHECK(a.pc_aim.hand_type == 5 && a.pc.last_mode == 2 && a.pc.last_weapon == 1);
    CHECK(a.pc_aim.aim_x == a.pc_rules.center_aim_x[0]);
    CHECK(a.pc_aim.aim_y == a.pc_rules.center_aim_y[0]);
    CHECK(a.pc.glow_frame == 3);
    s.pc_mode = 5; s.previous_pc_mode = 2; e.spell = 1;
    CHECK(!ai_pc_select(&a, &s, &w, 2));
    CHECK(a.pc_aim.hand_type == 1 && !a.pc.glow_frame);
    CHECK(a.pc.glow_set == a.pc_rules.glow_set[1]);
    CHECK(a.pc_aim.aim_x == a.pc_rules.center_aim_x[1]);
    CHECK(a.pc_aim.aim_y == a.pc_rules.center_aim_y[1]);
    s.previous_pc_mode = 5; e.spell = 2;
    a.pc.glow_frame = 2; a.pc.muzzle_mode = 3;
    CHECK(!ai_pc_select(&a, &s, &w, 2));
    CHECK(a.pc.glow_frame == 2 && a.pc.muzzle_mode == 3);
    CHECK(a.pc.glow_set == a.pc_rules.glow_set[2]);
    s.pc_mode = 1; a.pc_aim.hand_type = 5;
    a.pc.fire = a.pc.cooldown = a.pc.latch = 1;
    CHECK(!ai_pc_select(&a, &s, &w, 0));
    CHECK(a.pc_aim.hand_type == 5 && a.pc.glow_frame == 2 && a.pc.muzzle_mode == 3);
    CHECK(a.pc_aim.aim_x == a.pc_rules.center_aim_x[5]);
    CHECK(a.pc_aim.aim_y == a.pc_rules.center_aim_y[5]);
    CHECK(!a.pc.fire && !a.pc.cooldown && !a.pc.latch);
    puts("PC fire: equipment, selector commits, modern ResetAim, draw counters, aircar spread and energy recovery matched");
    return 0;
}

static int hand(char **argv)
{
    static const unsigned directions[8] = {0x48,0x4d,0x50,0x4b,0x47,0x49,0x4f,0x51};
    world_person *p = &w.persons[0];
    unsigned i, position = (unsigned)strtoul(argv[1], NULL, 0);
    e.hh_weapon = (int16_t)strtol(argv[0], NULL, 0);
    a.pc.auto_s_flag = (int32_t)strtol(argv[2], NULL, 0);
    if (position >= 9 || e.hh_weapon < 0 || e.hh_weapon >= 4 ||
        (a.pc.auto_s_flag != 0 && a.pc.auto_s_flag != 1)) return 2;
    for (i = 0; i < 3; ++i) w.rng.word[i] = (uint16_t)strtoul(argv[3+i], NULL, 0);
    s.pc_mode = s.previous_pc_mode = 2;
    if (ai_pc_equip(&a, &s, &w)) return 1;
    a.pc.fire = 1;
    a.pc.hand_x = ((int)position%3-1)*64;
    a.pc.hand_y = (1-(int)position/3)*64;
    p->unk_0040 = (int32_t)(position | position<<16);
    for (i = 0; i < 80; ++i) {
        s.timer_ticks = 300+i*2;
        memset(s.kb_scan_status, 0, sizeof s.kb_scan_status);
        s.kb_scan_status[0x39] = i%24 < 19;
        s.primary_fire = i%24 < 19;
        s.kb_scan_status[directions[(i/2)%8]] = 1;
        s.kb_scan_status[0x2a] = (i/8)%2;
        if (pc_step() || ai_pc_hand2hand(&a, &s, &w)) return 1;
        printf("%u %d %d %d %d %d %d %d %d %d %d %d %u %d %d %d %u %u %u %u %u %u %u\n",
            i,a.pc.fire,a.pc.cooldown,a.pc.latch,a.pc.hand_x,a.pc.hand_y,
            a.pc.sword_frame,a.pc.auto_s_flag,a.pc.auto_swinging,a.pc.auto_s_reset,
            a.pc.reset_pos,a.pc.auto_s_mode,(unsigned)a.pc.auto_s_time,
            a.pc.auto_update_flag,a.pc.key_hand_x,a.pc.key_hand_y,(unsigned)(uint32_t)p->unk_003C,
            (unsigned)(uint32_t)p->unk_0040,(unsigned)(uint32_t)p->unk_0044,
            (unsigned)(uint32_t)p->unk_0048,(unsigned)w.rng.word[0],
            (unsigned)w.rng.word[1],(unsigned)w.rng.word[2]);
    }
    return 0;
}

static int regen(char **argv)
{
    unsigned i;
    long maximum = strtol(argv[0], NULL, 0), energy = strtol(argv[1], NULL, 0);
    if (maximum < -32768 || maximum > 32767 || energy < -32768 || energy > 32767)
        return 2;
    e.max_energy = (int32_t)maximum; e.energy = (int32_t)energy;
    a.pc.sp_regen_timer = (uint32_t)strtoul(argv[2], NULL, 0);
    for (i = 0; i < 80; ++i) {
        s.timer_ticks = 300+i*2+i/5;
        CHECK(!ai_pc_draw_advance(&a, &s, &w, &draw_clock));
        printf("%u %d %u\n", i, e.energy, (unsigned)a.pc.sp_regen_timer);
    }
    return 0;
}

static int bob(char **argv)
{
    unsigned i;
    int moving = (int)strtol(argv[4], NULL, 0);
    a.pc.bob_phase = (int32_t)strtol(argv[0], NULL, 0);
    s.unk_144aec = (int32_t)strtol(argv[1], NULL, 0);
    a.pc_aim.render_x = (int32_t)strtol(argv[2], NULL, 0);
    a.pc_aim.render_y = (int32_t)strtol(argv[3], NULL, 0);
    if (a.pc.bob_phase < 0 || a.pc.bob_phase >= 14 || moving < 0 || moving > 1)
        return 2;
    for (i = 0; i < 80; ++i) {
        s.unk_144afc = moving ? i%24 < 16 : i%24 >= 4 && i%24 < 20;
        s.unk_144b1c = i%16 == 12;
        s.bp_limit_flag = i%3 ? 2 : 0;
        ai_pc_hand_bob(&a, &s);
        printf("%u %d %d %d\n", i, a.pc.bob_phase, a.pc_aim.render_x, a.pc_aim.render_y);
    }
    return 0;
}

static int breath(char **argv)
{
    unsigned i;
    int hp = (int)strtol(argv[0], NULL, 0), max = (int)strtol(argv[1], NULL, 0);
    int slot = (int)strtol(argv[2], NULL, 0), sample = (int)strtol(argv[3], NULL, 0);
    if (hp < -32768 || hp > 32767 || max < -32768 || max > 32767 ||
        slot < -1 || slot > 15 || sample < 0 || sample > 255) return 2;
    w.persons[0].unk_0038 = (int32_t)((uint32_t)(uint16_t)max<<16 | (uint16_t)hp);
    w.person_count = 2; w.player_person = 1; /* Must still read PERSON0. */
    a.pc.sword_hum_slot = slot; a.pc.sword_hum_sound = sample;
    for (i = 0; i < 3; ++i) w.rng.word[i] = (uint16_t)strtoul(argv[4+i], NULL, 0);
    for (i = 0; i < 80; ++i) {
        s.timer_ticks = 300+i*2;
        observe_begin();
        CHECK(!ai_pc_draw_advance(&a, &s, &w, &draw_clock));
        CHECK(!observed.overflow);
        printf("%u %u %u %u %d %u %d %d %d %d\n", i, (unsigned)w.rng.word[0],
            (unsigned)w.rng.word[1], (unsigned)w.rng.word[2], a.pc_sound,
            observed.count, observed.bound[0], observed.value[0], observed.bound[1], observed.value[1]);
    }
    return 0;
}

static int hum(char **argv)
{
    unsigned i;
    int mode = (int)strtol(argv[0], NULL, 0), primary = (int)strtol(argv[1], NULL, 0);
    int pulse = (int)strtol(argv[2], NULL, 0), running = (int)strtol(argv[3], NULL, 0);
    int vehicle = (int)strtol(argv[4], NULL, 0);
    if ((mode != 1 && mode != 2 && mode != 5 && mode != 7) ||
        primary < 0 || primary > 1 || pulse < 0 || pulse > 1 ||
        running < 0 || running > 1 || vehicle < 0 || vehicle > 1) return 2;
    s.pc_mode = s.previous_pc_mode = mode; s.vehicle_type = vehicle;
    s.primary_fire = primary; s.running = running; a.pc.auto_s_flag = 0; a.pc.fire = 1;
    a.pc.sword_hum_slot = (int32_t)strtol(argv[5], NULL, 0);
    a.pc.sword_hum_sound = (int32_t)strtol(argv[6], NULL, 0);
    w.persons[0].unk_0040 = 4 | 4<<16;
    for (i = 0; i < 3; ++i) w.rng.word[i] = (uint16_t)strtoul(argv[7+i], NULL, 0);
    for (i = 0; i < 80; ++i) {
        s.timer_ticks = 300+i*2; a.pc.key_swing_sword = pulse;
        observe_begin();
        CHECK(!pc_step());
        CHECK(!observed.overflow);
        printf("%u %d %d %d %d %u %u %u %u %d %d %d %d\n", i, a.pc.fire,
            a.pc.sword_hum_slot, a.pc.sword_hum_sound, a.pc_sound,
            (unsigned)w.rng.word[0], (unsigned)w.rng.word[1], (unsigned)w.rng.word[2],
            observed.count, observed.bound[0], observed.value[0], observed.bound[1], observed.value[1]);
    }
    return 0;
}

static int tank(void)
{
    int vehicle, enabled, automatic, hand, aim, y, latch, cooldown, count;
    unsigned now, deadline;
    while ((count = scanf("%d %d %d %d %d %d %u %u %d %d",
            &vehicle, &enabled, &automatic, &hand, &aim, &y, &now, &deadline,
            &latch, &cooldown)) == 10) {
        s.pc_mode = s.previous_pc_mode = 7;
        s.vehicle_type = vehicle; s.tank_hand = hand; s.timer_ticks = now;
        a.pc_aim.auto_enable = enabled; a.pc_aim.tank_auto = automatic;
        a.pc_aim.tank_aim_y = aim; a.pc_aim.reset_auto_aim_time = deadline;
        a.pc.hand_y = y; a.pc.latch = latch; a.pc.cooldown = cooldown;
        CHECK(!ai_pc_prepare_motion(&a, &s, &w));
        printf("%d %d\n", a.pc_aim.tank_aim_y, a.pc_aim.tank_aim_step);
    }
    return count == EOF ? 0 : 2;
}

static int melt(char **argv)
{
    static uint8_t image[CM_WIDTH * CM_HEIGHT], background[CM_WIDTH * CM_HEIGHT];
    unsigned columns[CM_WIDTH];
    unsigned i, pass, pattern = (unsigned)strtoul(argv[3], NULL, 0);
    if (pattern > 2) return 2;
    for (i = 0; i < 3; ++i) w.rng.word[i] = (uint16_t)strtoul(argv[i], NULL, 0);
    for (i = 0; i < sizeof image; ++i) {
        image[i] = (uint8_t)(i * 13 + (i / CM_WIDTH) * 7);
        background[i] = (uint8_t)(i * 29 + i / CM_WIDTH);
    }
    for (i = 0; i < CM_WIDTH; ++i)
        columns[i] = pattern == 0 ? 7u : pattern == 1 ? 7u + (i % 61) * 8 : CM_HEIGHT + 7u;
    for (pass = 0; pass < 4; ++pass) {
        int result = ui_pc_death(image, background, columns, &w.rng);
        if (result < 0) return 1;
        printf("%d %u %u %u", result, (unsigned)w.rng.word[0],
               (unsigned)w.rng.word[1], (unsigned)w.rng.word[2]);
        for (i = 0; i < CM_WIDTH; ++i) printf(" %u", columns[i]);
        putchar('\n');
        if (fwrite(image, 1, sizeof image, stdout) != sizeof image) return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 2 && (argc != 3 || strcmp(argv[2], "tank")) &&
        (argc != 9 || strcmp(argv[2], "hand")) &&
        (argc != 6 || strcmp(argv[2], "regen")) &&
        (argc != 8 || strcmp(argv[2], "bob")) &&
        (argc != 10 || strcmp(argv[2], "breath")) &&
        (argc != 13 || strcmp(argv[2], "hum")) &&
        (argc != 7 || strcmp(argv[2], "melt"))) return 2;
    if (setup(argv[1])) return 1;
    if (argc == 2) return checks();
    if (argc == 3) return tank();
    if (argc == 7) return melt(argv+3);
    if (argc == 9) return hand(argv+3);
    if (argc == 10) return breath(argv+3);
    if (argc == 13) return hum(argv+3);
    return argc == 8 ? bob(argv+3) : regen(argv+3);
}
