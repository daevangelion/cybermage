#include "ui.h"
#include "ai.h"
#include "events.h"

#define HAND_SCALE_ONE 65536u
#define HAND_MAX_DIMENSION 1024

static int player_hand(const cm_ui *ui, world *w, uint8_t *dst,
                        int mode, int weapon, unsigned frame, ui_view view,
                        const ai_state *a, int muzzle)
{
    static const unsigned weapon_prop[10] = {13,56,55,132,133,134,135,139,140,141};
    static const unsigned sword_prop[4] = {14,136,137,138};
    const uint8_t *data;
    uint32_t size, start, end, image;
    unsigned resource, glove;
    ui_hand hand = {0};
    if (!ui->hand_tables_loaded || frame >= UI_HAND_FRAMES)
        return cm_fail("hand graphics", frame, "missing pose data or invalid frame");
    if (mode == 5) { resource = 48; glove = 1; }
    else if (mode == 1 && weapon >= 0 && weapon < 10) {
        resource = weapon_prop[weapon]; glove = (unsigned)weapon + 2;
    } else if (mode == 2 && weapon >= 0 && weapon < 4) {
        resource = sword_prop[weapon]; glove = (unsigned)weapon;
    } else return cm_fail("hand graphics", (uint32_t)mode, "unsupported player hand mode");
    image = (uint32_t)(mode == 2 ? ui->Swd_xFrame[glove][frame] :
        ui->Glv_xFrame[glove][frame]);
    if (muzzle) {
        int slot;
        if (a->pc.muzzle_frame < 0 || a->pc.muzzle_frame >= 22 ||
            a->pc.muzzle_count < 0 || a->pc.muzzle_count >= 3 || frame >= 9 ||
            a->pc.muzzle_frame > 21-a->pc.muzzle_count)
            return cm_fail("muzzle", (uint32_t)a->pc.muzzle_frame, "invalid flash frame");
        slot = a->pc.muzzle_frame+a->pc.muzzle_count;
        image = (uint32_t)ui->Glv_xFrame[glove][slot];
        hand.muzzle = 1;
    }
    if (rsc_entry(&w->props, resource, &data, &size)) return -1;
    if (size < 1442 || data[0] > 120 || image >= data[0])
        return cm_fail(w->props.path, resource, "hand frame exceeds prop package");
    start = cm_u32(data + 962 + image * 4);
    end = image + 1 < data[0] ? cm_u32(data + 962 + (image+1) * 4) : size;
    if (start < 1442 || start > end || end > size)
        return cm_fail(w->props.path, resource, "hand frame span exceeds package");
    hand.pixels.data = data + start; hand.pixels.size = end - start;
    /* LoadPROPDOSMEM0x259EB/0x259FD swaps disk width/height into TEX_H/TEX_W. */
    hand.half_width = (int32_t)cm_u32(data + 6 + image * 8);
    hand.half_height = (int32_t)cm_u32(data + 2 + image * 8);
    hand.x_off = ui->Glv_xOff[glove][frame];
    hand.y_off = ui->Glv_yOff[glove][frame];
    hand.x_pos_off = ui->Glv_xPosOff[glove][frame];
    hand.view_x = view.x; hand.view_y = view.y;
    hand.view_width = view.width; hand.view_height = view.height;
    hand.aim_x = ui->CenterAimX[glove];
    hand.aim_y = ui->CenterAimY[glove];
    if (a) {
        int64_t x = (int64_t)a->pc_aim.aim_x+a->pc_aim.render_x;
        int64_t y = (int64_t)a->pc_aim.aim_y+a->pc_aim.render_y;
        if (x < -HAND_MAX_DIMENSION || x > HAND_MAX_DIMENSION ||
            y < -HAND_MAX_DIMENSION || y > HAND_MAX_DIMENSION)
            return cm_fail("hand graphics",frame,"render offset exceeds hand limits");
        hand.aim_x = (int)x;
        hand.aim_y = (int)y;
        if (hand.aim_x < a->pc_aim.w_max_leftaimx) hand.aim_x = a->pc_aim.w_max_leftaimx;
        if (hand.aim_x > a->pc_aim.w_max_rightaimx) hand.aim_x = a->pc_aim.w_max_rightaimx;
        if (hand.aim_y < a->pc_aim.w_max_botaimy) hand.aim_y = a->pc_aim.w_max_botaimy;
        if (hand.aim_y > a->pc_aim.w_max_topaimy) hand.aim_y = a->pc_aim.w_max_topaimy;
    }
    if (muzzle) {
        hand.x_off += ui->MuzzleX[glove][frame][a->pc.muzzle_count];
        hand.y_off -= ui->MuzzleY[glove][frame][a->pc.muzzle_count];
    }
    if (mode == 2) {
        hand.sword = 1;
        hand.x_off = ui->SwordX[glove][frame];
        hand.y_off = hand.x_pos_off = hand.aim_x = hand.aim_y = 0;
    }
    if (mode == 5 && !muzzle) {
        int glow, glow_frame = a ? a->pc.glow_frame : 0;
        if (weapon < 0 || weapon >= 16 ||
            (glow = a ? a->pc.glow_set : ui->GlowSet[weapon]) < 0 ||
            glow >= 6 || glow_frame < 0 || glow_frame >= 4)
            return cm_fail("hand graphics", (uint32_t)weapon, "invalid spell glow selector");
        return ui_draw_xscale_aim(dst, &hand, ui->SpellGlow+glow*12+glow_frame*3);
    }
    return ui_draw_xscale_aim(dst, &hand, NULL);
}

int ui_draw_player_hand(const cm_ui *ui, world *w, uint8_t *dst,
                        int mode, int weapon, unsigned frame, ui_view view)
{
    return player_hand(ui,w,dst,mode,weapon,frame,view,NULL,0);
}

int ui_draw_live_hand(const cm_ui *ui, world *w, uint8_t *dst,
                       int mode, const events_state *e,
                       const ai_state *a, ui_view view)
{
    int frame = mode == 2 ? a->pc.sword_frame : a->pc_aim.weapon_aim_step;
    int weapon = mode == 2 ? e->hh_weapon : mode == 5 ? e->spell : e->weapon;
    if (mode != 1 && mode != 2 && mode != 5)
        return cm_fail("hand graphics",(uint32_t)mode,"unsupported live hand mode");
    if (frame < 0 || frame >= UI_HAND_FRAMES)
        return cm_fail("hand graphics",(uint32_t)frame,"live pose exceeds table");
    if (mode != 2 && a->pc.muzzle_mode > 0 &&
        player_hand(ui,w,dst,mode,weapon,(unsigned)frame%9,view,a,1)) return -1;
    return player_hand(ui,w,dst,mode,weapon,(unsigned)frame,view,a,0);
}

unsigned ui_cursor_pose(int aim_x, int aim_y, int pressed)
{
    /* MenuOptions4EF9E..4F008: directional3x3 pose, then pressed bank. */
    unsigned frame = pressed ? 13u : 4u;
    if (aim_y < 64) frame += 3;
    else if (aim_y > 108) frame -= 3;
    if (aim_x < -12) --frame;
    else if (aim_x > 49) ++frame;
    return frame;
}

int ui_draw_menu_cursor(const cm_ui *ui, world *w, uint8_t *dst,
                         int aim_x, int aim_y, unsigned frame)
{
    const uint8_t *data;
    uint32_t size, image, start, end;
    ui_hand hand = {0};
    if (!ui->hand_tables_loaded || frame >= UI_HAND_FRAMES)
        return cm_fail("menu cursor", frame, "invalid cursor pose");
    /* LoadScene12187 loads PRP7 at144A84; LoadPROPDOSMEM259E0 writes
     * frame0 over that initially null slot. MenuOptions4E830 starts pose4,
     * then4EF9E..4F008 adds its directional row/column. */
    image = (uint32_t)ui->Glv_xFrame[0][frame];
    if (rsc_entry(&w->props, 7, &data, &size)) return -1;
    if (size < 1442 || data[0] > 120 || image >= data[0])
        return cm_fail(w->props.path, 7, "cursor frame exceeds package");
    start = cm_u32(data+962+image*4);
    end = image+1 < data[0] ? cm_u32(data+962+(image+1)*4) : size;
    if (start < 1442 || start > end || end > size)
        return cm_fail(w->props.path, 7, "cursor span exceeds package");
    hand.pixels.data = data+start; hand.pixels.size = end-start;
    hand.half_width = (int32_t)cm_u32(data+6+image*8);
    hand.half_height = (int32_t)cm_u32(data+2+image*8);
    hand.view_width = CM_WIDTH; hand.view_height = CM_HEIGHT;
    hand.x_off = ui->Glv_xOff[0][frame];
    hand.y_off = ui->Glv_yOff[0][frame];
    hand.x_pos_off = ui->Glv_xPosOff[0][frame];
    hand.aim_x = aim_x; hand.aim_y = aim_y; hand.tank = 1;
    return ui_draw_xscale_aim(dst, &hand, NULL);
}

/* DrawXScaleAim0x29260; DrawXScaleGlow0x29524 differs only for FD..FF.
 * Pose tables Glv_xOff/yOff/xPosOff are supplied by the game, not the renderer.
 * [rust: crates/game/src/native/cmdraw.rs] provided the packed-row guide. */
int ui_draw_xscale_aim(uint8_t *dst, const ui_hand *h, const uint8_t *glow)
{
    int width, rows, top, left, y;
    uint32_t ustep, vstep, v = 0, source_width, source_rows;
    if (h->half_width <= 0 || h->half_width > HAND_MAX_DIMENSION ||
        h->half_height <= 0 || h->half_height > HAND_MAX_DIMENSION ||
        h->view_width <= 0 || h->view_width > CM_WIDTH ||
        h->view_height <= 0 || h->view_height > CM_HEIGHT ||
        h->view_x < 0 || h->view_x > CM_WIDTH-h->view_width ||
        h->view_y < 0 || h->view_y > CM_HEIGHT-h->view_height ||
        h->x_off < -HAND_MAX_DIMENSION || h->x_off > HAND_MAX_DIMENSION ||
        h->y_off < -HAND_MAX_DIMENSION || h->y_off > HAND_MAX_DIMENSION ||
        h->x_pos_off < -HAND_MAX_DIMENSION || h->x_pos_off > HAND_MAX_DIMENSION ||
        h->aim_x < -HAND_MAX_DIMENSION || h->aim_x > HAND_MAX_DIMENSION ||
        h->aim_y < -HAND_MAX_DIMENSION || h->aim_y > HAND_MAX_DIMENSION)
        return cm_fail("hand graphics", 0, "hand pose outside engine limits");
    source_width = (unsigned)h->half_height * 2;
    source_rows = (unsigned)h->half_width * 2;
    width = h->view_width * (int)source_width / CM_WIDTH * 2;
    rows = h->view_width * (int)source_rows / CM_WIDTH * 2;
    if (width <= 0 || rows <= 0 || source_rows > h->pixels.size / 4)
        return cm_fail("hand graphics", 0, "truncated hand row table");
    ustep = source_width * HAND_SCALE_ONE / (unsigned)width;
    vstep = source_rows * HAND_SCALE_ONE / (unsigned)rows;
    if (!h->tank && !h->muzzle && rows > h->view_height) {
        v = vstep * (unsigned)(rows-h->view_height);
        rows = h->view_height;
    }
    top = (h->y_off * 2 + CM_HEIGHT - h->aim_y * 2) * h->view_width / CM_WIDTH;
    if (h->muzzle) {
        if (top >= h->view_height) return 0;
        if (top < 0) top = 0;
    } else if (top < (h->tank ? 0 : h->view_height-rows))
        top = h->tank ? 0 : h->view_height-rows;
    else if (top > h->view_height-4) top = h->view_height-4;
    left = (h->x_off * 2 + h->aim_x * 2 + CM_WIDTH/2 - h->x_pos_off * 2) *
        h->view_width / CM_WIDTH;
    if (h->muzzle && left < 0) left = 0;
    if (h->sword) {
        left = (h->x_off * h->view_width / CM_WIDTH + h->aim_x) * 2;
        top = h->view_height - rows - h->aim_y;
    }
    for (y = top; y < h->view_height; ++y) {
        uint32_t at, logical = 0, u = 0, row = v / HAND_SCALE_ONE;
        int x = 0;
        if (h->muzzle && y-top >= rows) break;
        if (row >= source_rows)
            return cm_fail("hand graphics", row * 4, "scaled row exceeds hand table");
        at = cm_u32(h->pixels.data + row * 4);
        if (at < source_rows * 4 || at > h->pixels.size)
            return cm_fail("hand graphics", row * 4, "hand row points outside payload");
        while (x < width) {
            int run;
            unsigned count;
            uint32_t end;
            if (at > h->pixels.size || h->pixels.size-at < 2)
                return cm_fail("hand graphics", at, "truncated packed run");
            run = cm_u16(h->pixels.data + at);
            if (run >= 32768) run -= 65536;
            at += 2;
            if (run == 0 || run == -32768)
                return cm_fail("hand graphics", at-2, "invalid packed run length");
            count = (unsigned)(run < 0 ? -run : run);
            if (count > source_width-logical ||
                (run > 0 && count > h->pixels.size-at))
                return cm_fail("hand graphics", at-2, "packed run exceeds hand row");
            end = logical + count;
            while (x < width && u / HAND_SCALE_ONE < end) {
                int dx = h->view_x + left + x, dy = h->view_y + y;
                if (run > 0 && dx >= h->view_x && dx < h->view_x+h->view_width &&
                    dy >= h->view_y && dy < h->view_y+h->view_height) {
                    uint8_t c = h->pixels.data[at + u / HAND_SCALE_ONE-logical];
                    if (glow && c > 252) {
                        c = glow[255-c];
                        if (c) gfx_store(dst+dy * CM_WIDTH + dx,c);
                    } else gfx_store(dst+dy * CM_WIDTH + dx,c);
                }
                ++x;
                u += ustep;
            }
            logical = end;
            if (run > 0) at += count;
        }
        v += vstep;
    }
    return 0;
}

/* PCDeath0x153EE initializes all columns to7. Each melt pass0x1547C–0x15534
 * samples480 columns with RND(640), shifts8 rows, and reveals background. */
int ui_pc_death(uint8_t *dst, const uint8_t *background,
                unsigned columns[CM_WIDTH], world_rng *rng)
{
    unsigned sample;
    int finished = 0;
    for (sample = 0; sample < CM_WIDTH; ++sample)
        if ((columns[sample] & 7u) != 7u || columns[sample] > CM_HEIGHT + 7u)
            return cm_fail("PCDeath", sample, "invalid melt column state");
    for (sample = 0; sample < CM_HEIGHT; ++sample) {
        unsigned x = (unsigned)world_rnd(rng, CM_WIDTH), at = columns[x];
        int y;
        if (at >= CM_HEIGHT) { finished = 1; continue; }
        for (y = CM_HEIGHT-1; y > (int)at; --y)
            gfx_store(dst+y * CM_WIDTH + x,dst[(y-8) * CM_WIDTH + x]);
        for (y = (int)at; y >= 0 && y > (int)at-8; --y)
            gfx_store(dst+y * CM_WIDTH + x,background[y * CM_WIDTH + x]);
        columns[x] += 8;
    }
    return finished;
}
