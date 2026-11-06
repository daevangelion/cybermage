#include "ui.h"
#include "ai.h"

/* CM_HI.EXE189D7: EC maps state-1 before hands;190C6: damage flash uses
 * flashcolor92F1C[state-1] after hands. TranArea5F8E0 uses HIPAK33 maps. */
int ui_draw_pc_tint(const cm_ui *ui, uint8_t *dst, const ai_state *a,
                      ui_view view, int after_hand)
{
    int state,map;
    if (view.x<0 || view.y<0 || view.width<=0 || view.height<=0 ||
        view.x>CM_WIDTH-view.width || view.y>CM_HEIGHT-view.height ||
        (after_hand!=0 && after_hand!=1))
        return cm_fail("player tint",0,"invalid primary view or tint stage");
    state=after_hand?a->pc_flash:a->pc.unk_1444EC;
    if (!state) return 0;
    if (state<0 || state>(after_hand?7:10))
        return cm_fail("player tint",(uint32_t)state,"tint state exceeds original maps");
    map=after_hand?ui->flashcolor[state-1]:state-1;
    if (map<0 || map>=10)
        return cm_fail("CM_HI.EXE",0x92F1C,"flash color exceeds transfer palettes");
    gfx_shade_world(dst,view.x,view.y,view.x+view.width-1,view.y+view.height-1,
              ui->bright+(unsigned)map*256);
    return 0;
}
