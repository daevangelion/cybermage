#include "ai.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

static ai_state a;

static int number(const char *text, int minimum, int maximum, int *value)
{
    char *end;
    long n;
    errno = 0;
    n = strtol(text, &end, 10);
    if (errno || end == text || *end || n < minimum || n > maximum) return -1;
    *value = (int)n;
    return 0;
}

int main(int argc, char **argv)
{
    int previous, difficulty, monster, value[3];
    unsigned i;
    if (argc != 7 || number(argv[1], 0, 2, &previous) ||
        number(argv[2], 0, 2, &difficulty) || number(argv[3], -1, 2, &monster))
        return 2;
    for (i = 0; i < 3; ++i)
        if (number(argv[i + 4], INT16_MIN, INT16_MAX, &value[i])) return 2;
    a.monster_count = 3;
    a.difficulty = 1;
    a.attack_difficulty = previous;
    for (i = 0; i < a.monster_count; ++i) {
        a.monsters[i].unk_0066 = (int16_t)value[0];
        a.monsters[i].unk_007C = (int16_t)value[1];
        a.monsters[i].unk_0094 = (int16_t)value[2];
    }
    if (monster < 0) {
        if (ai_set_attack_difficulty(&a, (unsigned)difficulty) ||
            a.attack_difficulty != difficulty || a.difficulty != 1) return 1;
    } else if (ai_set_monster_attack_ignore(&a, monster, (unsigned)previous,
                                            (unsigned)difficulty)) return 1;
    for (i = 0; i < a.monster_count; ++i)
        printf("%d %d %d%c", a.monsters[i].unk_0066, a.monsters[i].unk_007C,
               a.monsters[i].unk_0094, i + 1 == a.monster_count ? '\n' : ' ');
    return 0;
}
