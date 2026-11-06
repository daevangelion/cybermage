#include "../src/sim/sim_internal.h"
#include "ai.h"
#include "audio.h"
#include "events.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static world w;
static ai_state a;
static events_state e;
static sim_state s;
static cm_input input;

static void arena(const char *data)
{
    unsigned i;
    memset(&w, 0, sizeof w); memset(&a, 0, sizeof a);
    memset(&e, 0, sizeof e); memset(&input, 0, sizeof input);
    assert(!ai_load_tables(&a, data));
    sim_init(&s, 0);
    s.ai = &a; s.events = &e; a.events = &e; e.actors = &a;
    a.difficulty = 1; a.pc_sound = -1;
    w.scene = 2; w.top_level = 3; w.unk_130D20 = 0x3ffff;
    w.object_count = 2; w.person_count = 1; w.player_object = 1;
    w.persons[0].object = 1; w.persons[0].unk_0038 = 40 * 65536 + 40;
    w.objects[1].x = w.objects[1].y = 16 * 65536 + 32768;
    w.objects[1].z = 0x6000; w.objects[1].height = 0x2000;
    w.objects[1].radius = 0x1000; w.objects[1].unk_74AF0 = 0x1e00;
    w.objects[1].active = w.objects[1].targetable = 1;
    for (i = 0; i < CM_MAP_CELLS; ++i) {
        w.cells[0][i].floor = 1;
        w.cells[3][i].ceiling = 1;
    }
    world_trig_init(&w.trig); world_rng_init(&w.rng, 44);
    assert(!world_make_omap(&w));
    assert(!sim_start_scene(&s, &w));
    snd_enable(0, 0);
}

static void cap(const char *data)
{
    arena(data);
    s.pc_mode = 2;
    s.unk_144b00 = 80; s.unk_144ae8 = 64;
    s.kb_scan_status[0x48] = 12; s.kb_scan_status[0x39] = 3;
    /* ProcessInputEvents 0148C4 clears running eligibility for held primary
     * melee before 014835 chooses its 48/96 acceleration ceiling. */
    assert(!sim_frame(&s, &w, &input, 2));
    assert(s.unk_144b00 == 48 && !s.running);
    assert(s.primary_fire && a.pc.fire && s.kb_scan_status[0x39] == 1);
    arena(data);
    s.pc_mode = 2;
    s.unk_144b00 = 80; s.unk_144ae8 = 64;
    s.kb_scan_status[0x48] = 12;
    /* Consumer-only metadata: the selector producer is a separate slice. */
    a.pc.key_swing_sword = 1;
    assert(!sim_frame(&s, &w, &input, 2));
    assert(s.unk_144b00 == 96 && s.running && !s.primary_fire && a.pc.fire);
    arena(data);
    s.pc_mode = 7;
    s.unk_144b00 = 80; s.unk_144ae8 = 64;
    s.kb_scan_status[0x48] = 12;
    assert(!sim_frame(&s, &w, &input, 2));
    assert(s.unk_144b00 == 48 && !s.running);
    arena(data);
    a.pc.kb_fire = 0x21;
    s.kb_scan_status[0x21] = 2; s.kb_scan_status[0x39] = 3;
    assert(!sim_frame(&s, &w, &input, 2));
    assert(s.primary_fire && a.pc.fire && !s.kb_scan_status[0x21]);
    assert(s.kb_scan_status[0x39] == 3);
    arena(data);
    s.ai = NULL; s.primary_fire = 1; s.kb_scan_status[0x39] = 3;
    assert(!sim_frame(&s, &w, &input, 2));
    assert(!s.primary_fire && s.kb_scan_status[0x39] == 3);
    arena(data);
    s.pc_mode = 5; s.unk_144b34 = -0x3000; s.unk_144b3c = 0;
    s.body_height = 0x7000;
    s.kb_scan_status[0x48] = 12;
    /* ProcessInputEvents01485D: running requests standing even after a
     * forced crouch has reached its minimum and cleared the stance request. */
    assert(!sim_frame(&s, &w, &input, 2));
    assert(s.running && s.unk_144b3c == 1 && s.unk_144b34 == -0x2800);
    arena(data);
    s.pc_mode = 2; s.unk_144b34 = -0x3000; s.unk_144b3c = 0;
    s.body_height = 0x7000;
    s.kb_scan_status[0x48] = 12; s.kb_scan_status[0x39] = 3;
    assert(!sim_frame(&s, &w, &input, 2));
    assert(!s.running && !s.unk_144b3c && s.unk_144b34 == -0x3000);
    puts("sim primary/cap/binding checks and explicit pulse consumer checks matched");
}

static void hit(const char *data)
{
    world_rng expected;
    int32_t push, x, y;
    unsigned side, angle;
    arena(data);
    w.objects[1].heading = 0xcf00;
    s.key_rot_rate = -128; s.kb_scan_status[0x4d] = 12;
    a.pc_flash = 1;
    expected = w.rng;
    (void)world_rnd(&expected, 4096);
    push = world_rnd(&expected, 8192) - 4096;
    side = (unsigned)world_rnd(&expected, 2);
    angle = (uint16_t)(w.objects[1].heading + (side ? 0x4000 : 0xc000)) >> 5;
    x = w.objects[1].x + sim_mul16(push, w.trig.cos[angle]);
    y = w.objects[1].y + sim_mul16(push, w.trig.sin[angle]);
    /* Original JumbleBound 01D07D precedes MovePerson 01D437..01D825:
     * the push uses the pre-yaw heading even when this frame turns. */
    assert(!sim_frame(&s, &w, &input, 2));
    assert(w.objects[1].heading == 0xc700);
    assert(w.objects[1].x == x && w.objects[1].y == y);
    assert(!memcmp(w.rng.word, expected.word, sizeof expected.word));
    puts("sim player hit uses pre-motion heading and exact RNG order");
}

static void vehicle(const char *data)
{
    int32_t x, y;
    arena(data);
    s.pc_mode = 7; s.key_rot_rate = 64;
    w.objects[1].heading = 0x8000;
    x = w.objects[1].x; y = w.objects[1].y;
    s.bios_shift = 2; s.kb_scan_status[0x2a] = 3;
    s.kb_scan_status[0x4b] = 12;
    assert(!sim_frame(&s, &w, &input, 2));
    /* Vehicle keyboard013D67..013E1F ignores side-toggle for yaw and
     * writes +128 directly; it never reads or advances KeyRotRate. */
    assert(w.objects[1].heading == 0x8240 && s.key_rot_rate == 64);
    assert(w.objects[1].x == x && w.objects[1].y == y);
    arena(data);
    s.pc_mode = 7; s.key_rot_rate = 64;
    w.objects[1].heading = 0x8000;
    s.kb_scan_status[0x4b] = s.kb_scan_status[0x4d] = 12;
    assert(!sim_frame(&s, &w, &input, 2));
    assert(w.objects[1].heading == 0x7dc0 && s.key_rot_rate == 64);
    arena(data);
    s.pc_mode = 7; s.key_rot_rate = 64;
    x = w.objects[1].x; y = w.objects[1].y;
    s.kb_scan_status[0x2c] = s.kb_scan_status[0x2e] = 3;
    s.kb_scan_status[0x47] = s.kb_scan_status[0x4f] = 3;
    s.kb_scan_status[0x1d] = s.kb_scan_status[0x52] = 3;
    assert(!sim_frame(&s, &w, &input, 2));
    assert(w.objects[1].x == x && w.objects[1].y == y && s.key_rot_rate == 64);
    assert(!s.unk_144b1c && !s.unk_144b34);
    arena(data);
    s.pc_mode = 7; s.kb_scan_status[s.kb_look_down] = 3;
    assert(!sim_frame(&s, &w, &input, 2));
    assert(!w.camera.pitch);
    arena(data);
    s.pc_mode = 7; s.vehicle_type = 1;
    s.kb_scan_status[s.kb_look_down] = 3;
    assert(!sim_frame(&s, &w, &input, 2));
    assert(w.camera.pitch == 32);
    puts("sim vehicle fixed yaw, ignored foot keys and pitch context matched");
}

static void keypad(const char *data)
{
    static const unsigned key[] = {0x47, 0x49, 0x4f, 0x51};
    static const uint16_t angle[] = {0x2000, 0xe000, 0x4000, 0xc000};
    unsigned i;
    for (i = 0; i < sizeof key / sizeof key[0]; ++i) {
        arena(data); s.pc_mode = 5;
        s.kb_scan_status[key[i]] = 3;
        assert(!sim_frame(&s, &w, &input, 2));
        /* 013962/0139A9 diagonal7/9; 013AC3/013B63 side1/3. */
        assert(s.movement_angle == angle[i] && s.unk_144aec > 0);
        assert(!w.objects[1].heading && !w.camera.pitch);
    }
    arena(data); s.pc_mode = 5;
    s.kb_scan_status[0x4f] = s.kb_scan_status[0x48] = 3;
    assert(!sim_frame(&s, &w, &input, 2));
    assert(s.movement_angle == 0x2000);
    arena(data); s.pc_mode = 5;
    for (i = 0; i < sizeof key / sizeof key[0]; ++i) s.kb_scan_status[key[i]] = 3;
    assert(!sim_frame(&s, &w, &input, 2));
    assert(s.movement_angle == 0xc000);
    arena(data); s.pc_mode = 7;
    for (i = 0; i < sizeof key / sizeof key[0]; ++i) s.kb_scan_status[key[i]] = 3;
    assert(!sim_frame(&s, &w, &input, 2));
    assert(!s.unk_144aec && !w.objects[1].heading && !w.camera.pitch);
    puts("sim keypad diagonal7/9 and side1/3 source mappings matched");
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    if (!strcmp(argv[2], "cap")) cap(argv[1]);
    else if (!strcmp(argv[2], "hit")) hit(argv[1]);
    else if (!strcmp(argv[2], "vehicle")) vehicle(argv[1]);
    else if (!strcmp(argv[2], "keypad")) keypad(argv[1]);
    else return 2;
    return 0;
}
