#include "sim.h"
#include "ui.h"
#include <assert.h>
#include <string.h>

static world w;
static uint8_t normal_save[UI_SAVE_PREFIX + UI_PERSON_BYTES];
static uint8_t auto_save[SIM_AUTO_PREFIX + UI_PERSON_BYTES];
static uint8_t restored_save[sizeof normal_save];

int main(void)
{
    sim_state s;
    sim_init(&s, 100);
    {
        cm_input modern = {0};
        modern.modern = 1;
        modern.held[0x39] = modern.pressed[0x39] = 1;
        sim_input(&s, &modern);
        assert(s.kb_scan_status[0x39] == 3);
        modern.pressed[0x39] = 0;
        sim_input(&s, &modern);
        assert(s.kb_scan_status[0x39] == 1);
        modern.held[0x39] = 0;
        sim_input(&s, &modern);
        assert(s.kb_scan_status[0x39] == 0);
        assert(s.kb_latch == 0);
    }
    sim_scan(&s, 0x1c);
    assert(s.kb_scan_status[0x1c] == 0); /* first make is swallowed */
    sim_scan(&s, 0x9c);
    sim_scan(&s, 0x1c);
    assert(s.kb_scan_status[0x1c] == 3);
    sim_scan(&s, 0x9c);
    assert(s.kb_scan_status[0x1c] == 2);
    sim_scan(&s, 0xe0);
    sim_scan(&s, 0x4d);
    assert(s.kb_scan_status[0x4d] == 12);
    sim_scan(&s, 0xe0);
    sim_scan(&s, 0xcd);
    assert(s.kb_scan_status[0x4d] == 8);
    sim_scan(&s, 0x2a);
    assert(s.kb_scan_status[0x2a] == 0 && s.kb_latch == 3);
    assert(sim_clock(&s, 100) == 0);
    assert(sim_clock(&s, 101) == 1 && !s.bp_limit_flag);
    assert(sim_clock(&s, 106) == 2 && s.bp_limit_flag == 6);
    assert(sim_clock(&s, 106) == 0 && !s.bp_limit_flag);
    assert(sim_turn(0xfff0, 0x10, 16) == 0);
    assert(sim_turn(0x10, 0xfff0, 16) == 0);
    assert(sim_turn(0, 8, 16) == 8);
    w.top_level = 3;
    w.object_count = 3;
    w.player_object = 1;
    w.objects[1].z = 0x5d55;
    assert(!sim_start_scene(&s, &w));
    assert(s.swim_check == 1 && s.swim_panel == 22 && s.swim_art == 21);
    assert(w.objects[1].z == 0x5d55 && s.body_height == 0x9bff);
    w.unk_5E7A8 = 1;
    w.objects[1].z = 0x3800;
    assert(!sim_start_scene(&s, &w));
    assert(w.objects[1].z == 0x3800 && s.body_height == 0x6400);
    w.unk_5E7A8 = 0;
    w.scene = 4;
    assert(!sim_start_scene(&s, &w));
    assert(s.swim_check == 1 && s.swim_damage == 3 && s.swim_panel == 36 && s.swim_art == 35);
    w.scene = 7;
    assert(!sim_start_scene(&s, &w));
    assert(s.swim_damage == -1 && s.swim_panel == 30 && s.swim_art == 29);
    w.scene = 2;
    assert(!sim_start_scene(&s, &w) && !s.swim_check);
    w.objects[1].x = 0x18000;
    w.objects[1].y = 0x18000;
    w.objects[1].floor_z = 0x10000;
    w.cells[0][33].floor = 1;
    w.flatmap.nodes[33].object = 2;
    w.objects[2].x = 0x18000;
    w.objects[2].y = 0x18000;
    w.objects[2].z = 0x11000;
    w.objects[2].radius = w.objects[2].height = 0x1000;
    w.objects[2].flags = 0x00040000;
    w.objects[2].texture = 7;
    assert(!sim_coll_flat(&w, 1, 0x17000, 0x18000)); /* inclusive edge */
    assert(w.objects[1].floor_z == 0x11000);
    assert(sim_coll_swim_flat(&w, 1, 1, 7) == 1);
    assert(sim_coll_swim_flat(&w, 1, 1, 8) == 0);
    w.objects[2].flags = 0x00800000;
    w.objects[2].z = 0x16000;
    w.objects[1].floor_z = 0x11000;
    s.body_height = 0x6000;
    assert(sim_coll_top_flat(&s, &w, 1, 0x10000, 0x1100) == 1);
    assert(w.objects[1].floor_z == 0xeeff); /* top-height-1 */
    s.collision_level = 1;
    s.collision_mode = 0;
    w.path_mask[1][33] = 1u << 10;
    assert(sim_path_check(&s, &w, 0x18000, 0x18000) == 1);
    w.cells[1][33].region = 7;
    w.apertures[7].enabled = 1;
    w.apertures[7].min_x = w.apertures[7].max_x = 0x18000;
    w.apertures[7].min_y = w.apertures[7].max_y = 0x18000;
    assert(sim_path_check(&s, &w, 0x18000, 0x18000) == 0);
    s.no_openings = 1;
    assert(sim_path_check(&s, &w, 0x18000, 0x18000) == 1);
    assert(sim_path_check(&s, &w, -1, 0x18000) == 1);
    w.objects[2].z = 0x10000;
    w.objects[2].flags = 0; /* CollFireFlat does not filter flat flags. */
    w.objects[1].floor_z = 0x10000;
    w.objects[1].z = 0x1000;
    assert(sim_coll_fire_flat(&w, 1, 0x10000) == -1);
    assert(w.objects[1].floor_z == 0x10400);
    w.flatmap.nodes[33].next = CM_CHAIN_NODES;
    assert(sim_coll_flat(&w, 1, 0x18000, 0x18000) == -1);
    {
        sim_state loaded;
        uint32_t values[29] = {0};
        s.pc_mode = 5; s.previous_pc_mode = 2;
        s.unk_144ae8 = -640; s.unk_144b34 = -0x4800;
        s.unk_144b1c = 4; s.unk_144b28 = 8192;
        values[4] = 0xabcdef12;
        sim_save_export(&s, values);
        sim_init(&loaded, 123);
        assert(!sim_save_import(&loaded, values));
        assert(loaded.pc_mode == 5 && loaded.previous_pc_mode == 2);
        assert(loaded.unk_144ae8 == -640 && loaded.unk_144b34 == -0x4800);
        assert(loaded.unk_144b1c == 4 && loaded.unk_144b28 == 8192);
        assert(values[4] == 0xabcdef12 && loaded.previous_ticks == 123);
        values[0] = 8;
        assert(sim_save_import(&loaded, values) == -1);
        values[0] = 5;
        {
            static const unsigned indices[] = {5,16,17,18,19,21,22,23,25,26,27,28};
            unsigned i;
            for (i = 0; i < sizeof indices / sizeof indices[0]; ++i) {
                unsigned index = indices[i];
                uint32_t before = values[index];
                values[index] = INT32_MAX;
                assert(sim_save_import(&loaded, values) == -1);
                assert(cm_last_error()->offset == 0x6d4 + 4 * index);
                values[index] = before;
            }
        }
    }
    {
        uint32_t fire[4] = {1, 43, 40, 8};
        assert(!sim_save_fire_import(&s, fire));
        assert(s.fire_check == 1 && s.fire_damage == 8 && s.fire_tile_panel == 43);
        fire[3] = INT32_MAX;
        assert(sim_save_fire_import(&s, fire) == -1);
        assert(cm_last_error()->offset == 0x34f15 && s.fire_damage == 8);
        fire[3] = 8;
        fire[0] = 2;
        assert(sim_save_fire_import(&s, fire) == -1);
        assert(cm_last_error()->offset == 0x34f09);
        fire[0] = 0;
        fire[1] = fire[2] = (uint32_t)SIM_NO_SURFACE;
        assert(!sim_save_fire_import(&s, fire));
        assert(s.fire_tile_panel == SIM_NO_SURFACE && s.fire_tile_art == SIM_NO_SURFACE);
        fire[1] = (uint32_t)(SIM_NO_SURFACE + 1);
        assert(sim_save_fire_import(&s, fire) == -1 && cm_last_error()->offset == 0x34f0d);
    }
    {
        unsigned i;
        uint32_t size;
        for (i = 0; i < sizeof normal_save; ++i) normal_save[i] = (uint8_t)(i % 251);
        memset(normal_save, 0, 4);
        memset(normal_save + UI_SAVE_PREFIX - 4, 0, 4);
        normal_save[UI_SAVE_PREFIX - 4] = 1;
        assert(!sim_auto_pack(normal_save, sizeof normal_save, auto_save,
                              sizeof auto_save, &size));
        assert(size == sizeof auto_save);
        assert(!memcmp(auto_save, normal_save, 8));
        assert(!memcmp(auto_save + 8, normal_save + 0x758, 8));
        assert(!memcmp(auto_save + 16, normal_save + 0x201f, 0x2c551 - 0x201f));
        memcpy(restored_save, normal_save, sizeof normal_save);
        restored_save[8] = 123; /* AUTO must not restore inventory. */
        restored_save[0x201f] = 0;
        assert(!sim_auto_unpack(restored_save, sizeof restored_save, auto_save, size));
        assert(restored_save[8] == 123);
        assert(restored_save[0x201f] == normal_save[0x201f]);
        assert(sim_auto_unpack(restored_save, sizeof restored_save, auto_save, size - 1));
        auto_save[0] = 1;
        assert(sim_auto_unpack(restored_save, sizeof restored_save, auto_save, size));
        auto_save[0] = 0;
        auto_save[SIM_AUTO_PREFIX - 4] = 2;
        assert(sim_auto_unpack(restored_save, sizeof restored_save, auto_save, size));
        assert(sim_auto_pack(normal_save, sizeof normal_save, auto_save,
                             sizeof auto_save - 1, &size));
    }
    return 0;
}
