#include "world_internal.h"
#include <string.h>

void world_close(world *w)
{
    if (!w) return;
    rsc_close(&w->scenes); rsc_close(&w->fxo);
    rsc_close(&w->props); rsc_close(&w->animations);
    platform_file_release(&w->panel_file);
    w->object_count = w->person_count = w->panel_count = w->texture_count = 0;
}

static int span_at(rsc *r, unsigned index, world_span *s)
{
    return rsc_entry(r, index, &s->data, &s->size);
}

/* LoadPASS 0x27680: slot0 sentinel, 18-byte dummy followed by rectangles. */
static int load_passages(world *w)
{
    world_span span = w->scene_sections[6];
    unsigned count, i;
    if (world_section_records(span,18,"SCENE.RSC/PASS",world_section_offset(w,6),&count)) return -1;
    if (count >= 96) return cm_fail("SCENE.RSC/PASS", count, "too many passages");
    for (i = 0; i < count; ++i) {
        const uint8_t *p = span.data + (i + 1) * 18;
        world_aperture *a = &w->apertures[i + 1];
        a->min_x = world_s32(p); a->min_y = world_s32(p + 4);
        a->max_x = world_s32(p + 8); a->max_y = world_s32(p + 12);
        a->enabled = p[16];
    }
    return 0;
}

static int load_lights(world *w)
{
    world_span s = w->scene_sections[7];
    unsigned count, i;
    if (!s.data || s.size < 4) return cm_fail("SCENE.RSC/LIGHTS", 0, "truncated light count");
    count = cm_u32(s.data);
    if (count >= 32 || s.size != (count + 1) * 4)
        return cm_fail("SCENE.RSC/LIGHTS", count, "invalid light count");
    for (i = 1; i <= count; ++i) {
        const uint8_t *p = s.data + i * 4;
        w->light_param[i] = world_s16(p);
        w->light_type[i] = cm_u16(p + 2);
        if (w->light_type[i] == 6) {
            unsigned v = cm_u16(p);
            if (i >= 16) return cm_fail("SCENE.RSC/LIGHTS", i, "light curve index outside table");
            w->curve_base[i] = (int32_t)(v >> 8);
            w->curve_shift[i] = (int32_t)((v % 256) / 8);
            w->curve_scale[i] = (int32_t)(v % 8); /* original 0x2791F IDIV8 remainder */
        }
    }
    return 0;
}

/* Original LoadScene switch 0x116A8, scene data is independent of pixels. */
static void environment(world *w)
{
    static const int sky[12] = {0,0,1,2,0,0,0,3,0,1,0,1};
    static const int backdrop[12] = {-1,-1,7,11,-1,-1,-1,15,15,7,-1,11};
    unsigned i;
    w->sky_type = sky[w->scene];
    w->backdrop_resource = backdrop[w->scene];
    w->unk_5E7A8 = w->scene == 2 || w->scene == 3 || w->scene == 7 ||
                   w->scene == 9 || w->scene == 11;
    w->sky_fill = w->scene == 3 || w->scene == 11 ? 19 : 0;
    w->fog_on = w->scene == 3;
    w->curve_base[0] = 6; w->curve_shift[0] = 14; w->curve_scale[0] = 1;
    w->shade_override.bias = 6; w->shade_override.shift = 14;
    w->shade_override.scale = 1; w->shade_override.revision = 1;
    for (i = 0; i < CM_OBJECT_MAX; ++i) w->objects[i].unk_74AF0 = 0x1E00;
}

int world_load_scene(world *w, const char *dir, unsigned scene)
{
    static const char *names[4] = {"SCENE.RSC","FXO.RSC","PRP.RSC","ANMH.RSC"};
    rsc *archives[4];
    char panel[] = "PANEL00.DAT";
    world_rng rng;
    unsigned i;
    if (!w || !dir || scene < 1 || scene > 11)
        return cm_fail("SCENE.RSC", scene, "scene must be in 1..11");
    if (w->scenes.data || w->fxo.data || w->props.data || w->animations.data)
        return cm_fail("world", scene, "close loaded world before reusing it");
    rng = w->rng;
    if (!rng.word[0] && !rng.word[1] && !rng.word[2]) {
        world_rng_init(&rng, 1); /* EXE initial state; caller supplies BIOS seed. */
    }
    memset(w, 0, sizeof(*w));
    w->rng = rng; w->scene = scene; w->object_count = 1;
    (void)world_rnd(&w->rng, 4); /* LoadScene 0x11532: betting odds. */
    archives[0] = &w->scenes; archives[1] = &w->fxo;
    archives[2] = &w->props; archives[3] = &w->animations;
    for (i = 0; i < 4; ++i) {
        if (world_path(w->paths[i], dir, names[i]) || rsc_open(archives[i], w->paths[i])) goto fail;
    }
    for (i = 0; i < 8; ++i)
        if (span_at(&w->scenes, i * 12 + scene, &w->scene_sections[i])) goto fail;
    if (span_at(&w->scenes, 96, &w->fxo_definitions) ||
        span_at(&w->scenes, 97, &w->prop_definitions) ||
        span_at(&w->scenes, 98, &w->monster_definitions) ||
        span_at(&w->scenes, 99, &w->gsx_definitions) ||
        span_at(&w->scenes, 100, &w->animation_definitions)) goto fail;
    panel[5] = (char)('0' + scene / 10); panel[6] = (char)('0' + scene % 10);
    if (world_path(w->paths[4], dir, panel) || platform_file_read(&w->panel_file, w->paths[4])) goto fail;
    world_trig_init(&w->trig);
    environment(w);
    if (world_load_map(w, w->scene_sections[0].data, w->scene_sections[0].size) ||
        load_passages(w) || load_lights(w) || world_load_panels(w) ||
        world_load_placements(w) || world_load_persons(w)) goto fail;
    /* LoadScene 0x11F4E..0x11F7B overrides the player's animation origin. */
    w->objects[w->player_object].z = w->unk_5E7A8 ? 0x3800 : 0x5D55;
    if (world_make_omap(w)) goto fail;
    return 0;
fail:
    world_close(w);
    return -1;
}

/* Call after AI has reserved the original 12 missile and10 soul-fire slots.
 * LoadScene 0x120E8..0x1211F reserves the final three temporary view objects. */
int world_finalize_scene(world *w)
{
    if (w->object_count > 2378 - 3)
        return cm_fail("world", w->object_count, "temporary object slots exhausted");
    w->unk_144AA4 = w->object_count++;
    w->unk_144A8C = w->object_count++;
    w->unk_144A80 = w->object_count++;
    return world_make_omap(w);
}
