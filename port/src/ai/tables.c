#include "ai.h"
#include "exe.h"

#include <stdio.h>

/* Original numeric tables, CM_HI.EXE link addresses from labels.csv. */
int ai_load_tables(ai_state *a, const char *asset_dir)
{
    char path[CM_WORLD_PATH_MAX];
    cm_file file = {0};
    cm_exe exe;
    int32_t row[5];
    unsigned i, j;
    int result = -1;
    int n = snprintf(path, sizeof(path), "%s/CM_HI.EXE", asset_dir);
    a->tables_loaded = 0;
    if (n < 0 || (size_t)n >= sizeof(path))
        return cm_fail(asset_dir, 0, "combat table path too long");
    if (platform_file_read(&file, path)) return -1;
    if (cm_exe_init(&exe, &file, path)) goto out;
    for (i = 0; i < AI_MISSILE_TYPES; ++i) {
        ai_missile_data *m = &a->missile_types[i];
        if (cm_exe_table(&exe, 0x929F4 + i * 20, row, 5)) goto out;
        m->fxo = row[0]; m->sound = row[1]; m->volume = row[2];
        m->aim = row[3]; m->speed = row[4];
    }
    if (cm_exe_table(&exe, 0x92EE0, a->fire_missile, 12) ||
        cm_exe_table(&exe, 0x92F10, a->throw_missile, 3) ||
        cm_exe_table(&exe, 0x92E18, a->spell_missile, 16) ||
        cm_exe_table(&exe, 0x935A4, a->sword_range, 3)) goto out;
    for (i = 0; i < 24; ++i) {
        if (cm_exe_table(&exe, 0x936D0 + i * 12, row, 3)) goto out;
        for (j = 0; j < 3; ++j) {
            uint32_t packed = (uint32_t)row[j];
            a->zone_dice[i][j].count = (uint8_t)packed;
            a->zone_dice[i][j].sides = (uint8_t)(packed >> 8);
            a->zone_dice[i][j].add = (int16_t)(packed >> 16);
        }
    }
    for (i = 0; i < 34; ++i)
        if (cm_exe_table(&exe, 0x937F0 + i * 12, a->zone_radius[i], 3)) goto out;
    if (ai_pc_load_tables(a, &exe)) goto out;
    a->tables_loaded = 1;
    result = 0;
out:
    platform_file_release(&file);
    return result;
}
