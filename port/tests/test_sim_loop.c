#define main cybermage_main
#include "../src/main.c"
#undef main
#include <assert.h>

/* Exercise the actual orchestration, not a duplicated loader. */
int main(void)
{
    static const char *argv[] = {"cybermage", "--headless", "--data", "../orig/retail",
        "--user", "build/Integrator/loop-user", "--keys", "../tools/scenarios/port-movement.keys",
        "--frames", "4", "--bios-seed", "44"};
    assert(!cybermage_main((int)(sizeof argv / sizeof argv[0]), (char **)argv));
    assert(events.items[0].type == 120);
    assert(events.energy == 40 && events.cash == 85);
    assert(simulation.pc_mode == 5);
    assert((uint16_t)game.persons[0].unk_0038 == 40);
    assert((uint16_t)((uint32_t)game.persons[0].unk_0038 >> 16) == 40);
    return 0;
}
