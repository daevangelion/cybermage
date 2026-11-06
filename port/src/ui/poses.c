#include "ui.h"
#include "exe.h"

#include <stdio.h>

#define UI_PATH_BYTES 1024

/* Numeric alignment/frame tables are read as data from the supplied executable;
 * original game bytes are never generated into or bundled with the toolkit.
 * LE pages: [rust: crates/game/src/le.rs], checked with dosre.tables. */
int ui_load_hand_tables(cm_ui *ui, const char *asset_dir)
{
    char path[UI_PATH_BYTES];
    cm_file f = {0};
    cm_exe p;
    unsigned i;
    int status = -1, n = snprintf(path, sizeof path, "%s/CM_HI.EXE", asset_dir);
    if (n < 0 || (size_t)n >= sizeof path)
        return cm_fail(asset_dir, 0, "hand table path too long");
    if (platform_file_read(&f, path)) return -1;
    if (cm_exe_init(&p, &f, path)) goto out;
    if (cm_exe_table(&p, 0x93C4C, ui->CenterAimX, UI_GLOVE_COUNT) ||
        cm_exe_table(&p, 0x93C8C, ui->CenterAimY, UI_GLOVE_COUNT) ||
        cm_exe_table(&p, 0x92690, ui->spellPower, 16) ||
        cm_exe_table(&p, 0x926D0, ui->spellScon, 16)) goto out;
    for (i = 0; i < UI_GLOVE_COUNT; ++i) {
        uint32_t offset = i * UI_HAND_FRAMES * 4;
        if (cm_exe_table(&p, 0x93F88+offset, ui->Glv_xOff[i], UI_HAND_FRAMES) ||
            cm_exe_table(&p, 0x94408+offset, ui->Glv_yOff[i], UI_HAND_FRAMES) ||
            cm_exe_table(&p, 0x94888+offset, ui->Glv_xPosOff[i], UI_HAND_FRAMES) ||
            cm_exe_table(&p, 0x953C8+i*22*4, ui->Glv_xFrame[i], 22)) goto out;
    }
    for (i = 0; i < 4; ++i)
        if (cm_exe_table(&p, 0x95948+i*UI_HAND_FRAMES*4, ui->Swd_xFrame[i], UI_HAND_FRAMES) ||
            cm_exe_table(&p, 0x935B0+i*UI_HAND_FRAMES*4, ui->SwordX[i], UI_HAND_FRAMES)) goto out;
    ui->hand_tables_loaded = 1;
    status = 0;
out:
    platform_file_release(&f);
    return status;
}
