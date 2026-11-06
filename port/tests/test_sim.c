#include "sim.h"
#include <assert.h>
#include <string.h>

static world w;

int main(void)
{
    sim_state s;
    sim_init(&s, 100);
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
    assert(w.objects[1].z == 0x5d55 && s.body_height == 0x9bff);
    w.unk_5E7A8 = 1;
    w.objects[1].z = 0x3800;
    assert(!sim_start_scene(&s, &w));
    assert(w.objects[1].z == 0x3800 && s.body_height == 0x6400);
    w.unk_5E7A8 = 0;
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
    }
    return 0;
}
