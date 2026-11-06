#include "ai.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static world w;
static ai_state a;

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
    if (argc == 2) {
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
    puts("AI data tests passed");
    return 0;
}
