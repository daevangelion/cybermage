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
    for (i=0;i<8;++i)
        if (cm_exe_table(&p,0x95A68+i*16,ui->view_info[i],4)) goto out;
    if (cm_exe_table(&p,0x95AE8,ui->ACarX,11) ||
        cm_exe_table(&p,0x95B14,ui->ACarY,11) ||
        cm_exe_bytes(&p,0x95B40,ui->TankBar,sizeof ui->TankBar) ||
        cm_exe_bytes(&p,0x96404,ui->slot_symbols,sizeof ui->slot_symbols)) goto out;
    if (cm_exe_table(&p,0x9610C,ui->arena_x,14) ||
        cm_exe_table(&p,0x96144,ui->arena_y,14) ||
        cm_exe_table(&p,0x961E4,ui->slot_x,24) ||
        cm_exe_table(&p,0x96244,ui->slot_y,24)) goto out;
    if (cm_exe_table(&p, 0x96008, ui->shop_stock[0], 16) ||
        cm_exe_table(&p, 0x96058, ui->shop_stock[1], 16) ||
        cm_exe_table(&p, 0x960A8, ui->shop_stock[2], 3) ||
        cm_exe_table(&p, 0x96180, ui->coder_x, 12) ||
        cm_exe_table(&p, 0x961B0, ui->coder_y, 12)) goto out;
    if (cm_exe_table(&p, 0x93C4C, ui->CenterAimX, UI_GLOVE_COUNT) ||
        cm_exe_table(&p, 0x93C8C, ui->CenterAimY, UI_GLOVE_COUNT) ||
        cm_exe_table(&p, 0x92690, ui->spellPower, 16) ||
        cm_exe_table(&p, 0x926D0, ui->spellScon, 16) ||
        cm_exe_table(&p, 0x92EA0, ui->GlowSet, 16) ||
        cm_exe_bytes(&p, 0x92E58, ui->SpellGlow, sizeof ui->SpellGlow)) goto out;
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
    for (i = 0; i < UI_GLOVE_COUNT; ++i) {
        uint8_t x[54], y[54];
        unsigned row, col;
        if (cm_exe_bytes(&p,0x94D08+i*54,x,sizeof x) ||
            cm_exe_bytes(&p,0x95068+i*54,y,sizeof y)) goto out;
        for (row=0;row<9;++row) for (col=0;col<3;++col) {
            ui->MuzzleX[i][row][col]=(int16_t)cm_u16(x+(row*3+col)*2);
            ui->MuzzleY[i][row][col]=(int16_t)cm_u16(y+(row*3+col)*2);
        }
    }
    ui->hand_tables_loaded = 1;
    status = 0;
out:
    platform_file_release(&f);
    return status;
}
