#include "ai.h"
#include "events.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static world w;
static ai_state a;
static sim_state simulation;
static ui_save_state save;
static events_state player;

int main(int argc, char **argv)
{
    uint8_t monster[AI_MONSTER_DISK_SIZE] = {0};
    uint8_t sequence[AI_SEQUENCE_DISK_SIZE] = {0};
    ai_monster m;
    ai_sequence s;
    unsigned i;
    assert(ai_monster_decode(&m, monster, sizeof(monster) - 1) == -1);
    assert(ai_sequence_decode(&s, sequence, sizeof(sequence) - 1) == -1);
    monster[0xE7] = 2;
    monster[0xE8] = 6;
    monster[0xE9] = 0xFE;
    monster[0xEA] = 0xFF;
    monster[0x16] = 7;
    monster[0x1D] = 255;
    assert(ai_monster_decode(&m, monster, sizeof(monster)) == 0);
    assert(m.melee_damage.count == 2 && m.melee_damage.sides == 6);
    assert(m.melee_damage.add == -2 && m.faction == 7 && m.rank[0] == -1);
    sequence[0x16] = AI_ANIM_STEPS;
    assert(ai_sequence_decode(&s, sequence, sizeof(sequence)) == -1);
    w.object_count = 4; w.person_count = 3; w.player_object = 1;
    w.objects[1].person = 0; w.objects[2].person = 1; w.objects[3].person = 2;
    w.persons[0].type = 0; w.persons[1].type = 1; w.persons[2].type = 2;
    a.monster_count = 3;
    a.monsters[0].rank[0] = 1; a.monsters[1].rank[0] = 1;
    a.monsters[2].rank[0] = 0;
    a.targets.count = 3;
    a.targets.objects[0] = 1; a.targets.objects[1] = 2; a.targets.objects[2] = 3;
    assert(ai_set_auto_target(&a, &w) == 2);
    w.persons[1].status = 2;
    assert(ai_set_auto_target(&a, &w) == 1);
    w.persons[0].status = 2;
    assert(ai_set_auto_target(&a, &w) == -1);
    for (i = 1; i < w.object_count; ++i) w.objects[i].active = 1;
    w.objects[2].flags = UINT32_C(0x01000000);
    w.objects[3].flags = UINT32_C(0x00000400);
    ai_make_missile_list(&a, &w);
    assert(a.missile_count == 1 && a.missile_objects[0] == 1);
    w.persons[1].object = 2; w.persons[1].status = 0;
    w.persons[2].object = 3; w.persons[2].status = 2;
    w.objects[1].x = w.objects[1].y = 0x18000;
    w.objects[1].height = w.objects[2].height = 0x2000;
    w.objects[2].x = 0x1A000; w.objects[2].y = 0x18000;
    w.objects[1].heading = cm_atan64k(w.objects[1].x, w.objects[1].y,
                                     w.objects[2].x, w.objects[2].y);
    assert(ai_h2h_target(&a, &simulation, &w, 1, 0) == 1);
    assert(a.h2h_angle == 0 && a.h2h_distance < 0x6000);
    w.persons[1].status = 2;
    assert(ai_h2h_target(&a, &simulation, &w, 1, 0) == 0);
    if (argc >= 2) {
        const uint8_t *p;
        uint32_t size;
        assert(rsc_open(&w.scenes, argv[1]) == 0);
        assert(rsc_entry(&w.scenes, 98, &p, &size) == 0);
        assert(size == AI_MONSTER_MAX * AI_MONSTER_DISK_SIZE);
        for (i = 0; i < AI_MONSTER_MAX; ++i)
            assert(ai_monster_decode(&m, p + i * AI_MONSTER_DISK_SIZE,
                                     AI_MONSTER_DISK_SIZE) == 0);
        assert(rsc_entry(&w.scenes, 100, &p, &size) == 0);
        assert(size == AI_SEQUENCE_MAX * AI_SEQUENCE_DISK_SIZE);
        for (i = 0; i < AI_SEQUENCE_MAX; ++i)
            assert(ai_sequence_decode(&s, p + i * AI_SEQUENCE_DISK_SIZE,
                                      AI_SEQUENCE_DISK_SIZE) == 0);
        puts("MONSTER=109 ANIMOBJ=56");
        rsc_close(&w.scenes);
    }
    if (argc == 3) {
        char path[CM_WORLD_PATH_MAX];
        cm_file file = {0};
        cm_exe exe;
        int n = snprintf(path, sizeof(path), "%s/CM_HI.EXE", argv[2]);
        assert(n >= 0 && (size_t)n < sizeof(path));
        assert(platform_file_read(&file, path) == 0);
        assert(cm_exe_init(&exe, &file, path) == 0);
        assert(ai_load_tables(&a, argv[2]) == 0);
        assert(a.tables_loaded && a.missile_types[1].fxo == 3);
        assert(a.missile_types[2].speed == 0x40000 && a.fire_missile[0] == 2);
        world_rng_init(&w.rng, 44);
        assert(ai_init_pc(&player, &simulation, &w.rng, &exe, 1, 0) == 0);
        assert(ai_init_pc(&player, &simulation, &w.rng, &exe, 1, 400) == 0);
        assert(w.rng.word[0] == 3438 && w.rng.word[1] == 28802 && w.rng.word[2] == 6207);
        assert(player.PCBetPick == -1 && player.JackPot == 10000);
        assert(player.items[0].type == 120 && player.items[0].quantity == 0);
        assert(player.max_ammo[0] == 250 && player.energy == 40 && player.cash == 85);
        assert(player.SAVELife == 40 && player.SAVELifeMax == 40 && simulation.pc_mode == 5);
        ai_restore_pc_info(&player, &w);
        assert((int16_t)w.persons[0].unk_0038 == 40 && w.persons[0].status == 0);
        assert(ai_init_pc(&player, &simulation, &w.rng, &exe, 11, 500) == 0);
        assert(player.SAVELife == 100 && player.energy == 100 && player.cash == 60);
        platform_file_release(&file);
        puts("Original executable combat tables loaded");
    }
    w.object_count = 64;
    for (i = 0; i < AI_MISSILE_MAX; ++i) {
        a.missiles[i].object = (int32_t)i + 1;
        a.missiles[i].speed = (int32_t)i * 100;
    }
    for (i = 0; i < AI_SOUL_MAX; ++i) {
        a.souls[i].object = (int32_t)i + 13;
        a.souls[i].fxo = (int32_t)i + 20;
    }
    ai_save_export(&a, &save);
    a.missiles[3].speed = -999;
    assert(ai_save_import(&a, &w, &save) == 0);
    assert(a.missiles[3].speed == 300 && w.object_count == 64 && w.fxo_count == 30);
    save.unk_135ECC[1] = 64;
    assert(ai_save_import(&a, &w, &save) == -1);
    a.difficulty = 1;
    w.person_count = 3;
    w.persons[1].status = 0;
    w.persons[1].unk_0038 = (int32_t)(20u << 16 | 10u);
    w.persons[2].status = 2;
    w.persons[2].unk_0038 = (int32_t)(20u << 16);
    assert(ai_set_difficulty(&a, &w, 2) == 0);
    assert((int16_t)w.persons[1].unk_0038 == 21 &&
           (int16_t)((uint32_t)w.persons[1].unk_0038 >> 16) == 41);
    assert(ai_set_difficulty(&a, &w, 0) == 0);
    assert((int16_t)w.persons[1].unk_0038 == 5 &&
           (int16_t)((uint32_t)w.persons[1].unk_0038 >> 16) == 10);
    assert(w.persons[2].unk_0038 == (int32_t)(20u << 16));
    assert(ai_set_difficulty(&a, &w, 3) == -1);
    ai_start_clock(&a, 300);
    assert(a.animation_deadline == 300 && a.fast_animation_deadline == 300);
    puts("AI data tests passed");
    return 0;
}
