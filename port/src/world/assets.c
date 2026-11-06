#include "world_internal.h"
#include <string.h>

int world_path(char *out, const char *dir, const char *name)
{
    size_t a = strlen(dir), b = strlen(name);
    if (a > CM_WORLD_PATH_MAX - 2 || b > CM_WORLD_PATH_MAX - a - 2)
        return cm_fail(name, 0, "asset path too long");
    memcpy(out, dir, a);
    out[a] = '/';
    memcpy(out + a + 1, name, b + 1);
    return 0;
}

int world_section_records(world_span s, unsigned stride, unsigned *count)
{
    uint32_t n;
    if (!s.data || s.size < stride || stride < 4)
        return cm_fail("SCENE.RSC", 0, "truncated record header");
    n = cm_u32(s.data);
    if (n >= UINT32_MAX / stride || (n + 1) * stride != s.size)
        return cm_fail("SCENE.RSC", 0, "record count/length mismatch");
    *count = n;
    return 0;
}

/* LoadPANELS 0x270C7..0x27312: two dimension arrays, explicit lengths,
 * sequential payloads. Table values are retained, not inferred from length. */
int world_load_panels(world *w)
{
    const uint8_t *p = w->panel_file.data;
    uint32_t size = w->panel_file.size, at;
    unsigned cap, count, i;
    if (size < 2) return cm_fail("PANEL", 0, "truncated panel header");
    cap = p[0]; count = p[1]; at = 2 + cap * 12u;
    if (!cap || count > cap || at > size)
        return cm_fail("PANEL", 0, "invalid panel directory");
    for (i = 0; i < count; ++i) {
        world_texture *t = &w->textures[i];
        uint32_t len = cm_u32(p + 2 + cap * 8 + i * 4);
        if (len > size - at) return cm_fail("PANEL", at, "truncated panel pixels");
        w->panels[i].height = cm_u32(p + 2 + i * 4);
        w->panels[i].width = cm_u32(p + 2 + cap * 4 + i * 4);
        w->panels[i].pixels.data = p + at;
        w->panels[i].pixels.size = len;
        t->kind = WORLD_TEX_PANEL; t->resource = i;
        t->height = (int32_t)w->panels[i].height;
        t->width = (int32_t)w->panels[i].width;
        t->pixels = w->panels[i].pixels;
        at += len;
    }
    if (at != size) return cm_fail("PANEL", at, "trailing panel payload");
    w->panel_count = w->texture_count = count;
    return 0;
}

int world_texture_package(world *w, unsigned kind, unsigned resource,
                          unsigned packed, unsigned scale, unsigned *base)
{
    const uint8_t *p;
    uint32_t len;
    unsigned i, count, rotations;
    rsc *archive = kind == WORLD_TEX_FXO ? &w->fxo :
                   kind == WORLD_TEX_PROP ? &w->props : &w->animations;
    for (i = w->panel_count; i < w->texture_count; ++i)
        if (w->textures[i].kind == kind && w->textures[i].resource == resource) {
            *base = i;
            return 0;
        }
    if (rsc_entry(archive, resource, &p, &len)) return -1;
    if (kind == WORLD_TEX_FXO) {
        if (len < 4 || world_s16(p) <= 0 || world_s16(p + 2) <= 0)
            return cm_fail(archive->path, resource, "invalid FX bitmap header");
        count = rotations = 1;
    } else {
        if (len < 1442 || !(count = p[0]) || count > 120 || !(rotations = p[1]))
            return cm_fail(archive->path, resource, "invalid sprite package");
    }
    if (count > CM_PANEL_MAX - w->texture_count)
        return cm_fail(archive->path, resource, "texture slots exhausted");
    *base = w->texture_count;
    for (i = 0; i < count; ++i) {
        world_texture *t = &w->textures[w->texture_count++];
        uint32_t start, end;
        t->kind = kind; t->resource = resource; t->frame = i;
        t->packed = (uint8_t)packed; t->rotations = (uint8_t)rotations;
        t->rotate = kind == WORLD_TEX_FXO || (!packed && resource != 7 &&
                     resource != 13 && resource != 14 && resource != 47 && resource != 48);
        if (kind == WORLD_TEX_FXO) {
            t->height = world_s16(p); t->width = world_s16(p + 2);
            start = 4; end = len;
        } else {
            t->height = world_s32(p + 2 + i * 8);
            t->width = world_s32(p + 6 + i * 8);
            start = cm_u32(p + 962 + i * 4);
            end = i + 1 < count ? cm_u32(p + 962 + (i + 1) * 4) : len;
            if (start < 1442 || end < start || end > len || t->height <= 0 || t->width <= 0)
                return cm_fail(archive->path, start, "invalid sprite frame span");
        }
        if (!scale || (uint32_t)t->height > INT32_MAX / scale ||
            (uint32_t)t->width > INT32_MAX / scale)
            return cm_fail(archive->path, start, "sprite geometry overflow");
        t->pixels.data = p + start; t->pixels.size = end - start;
        t->half_w = t->height * (int32_t)scale;
        t->base = t->width * (int32_t)scale;
    }
    return 0;
}

/* GSX records (58 bytes), four 20-image groups. ANMH packages store
 * direction frames consecutively. No texture decode or drawing occurs here. */
int world_load_gsx(world *w, unsigned id)
{
    const uint8_t *p;
    unsigned group;
    if (id >= 55 || w->gsx_definitions.size < (id + 1) * 58u)
        return cm_fail("SCENE.RSC/GSX", id, "GSX index outside database");
    if (w->gsx[id].loaded) return 0;
    p = w->gsx_definitions.data + id * 58;
    for (group = 0; group < 4; ++group) {
        int resource = world_s16(p + 2 + group * 2);
        unsigned base, frame, count, rotations;
        if (resource <= 0) continue;
        if (world_texture_package(w, WORLD_TEX_ANIM, (unsigned)resource, 1, 512, &base)) return -1;
        rotations = w->textures[base].rotations;
        count = 0;
        while (base + count < w->texture_count &&
               w->textures[base + count].kind == WORLD_TEX_ANIM &&
               w->textures[base + count].resource == (unsigned)resource) ++count;
        for (frame = 0; frame < 20 && frame * rotations < count; ++frame) {
            w->gsx[id].textures[group * 20 + frame] = (uint16_t)(base + frame * rotations);
            w->gsx[id].prop_kind[group * 20 + frame] = 0;
        }
    }
    w->gsx[id].loaded = 1;
    return 0;
}

int world_anim_frame(world *w, unsigned object, unsigned gsx, unsigned image)
{
    if (object >= CM_OBJECT_MAX || gsx >= 55 || image >= 80 || !w->gsx[gsx].loaded)
        return cm_fail("world/GSX", image, "invalid animation frame");
    w->objects[object].texture = w->gsx[gsx].textures[image];
    w->objects[object].prop_kind = w->gsx[gsx].prop_kind[image];
    return 0;
}
int world_fxo_object(const world *w, unsigned index)
{
    if (index >= w->fxo_count) return cm_fail("world/FXO", index, "invalid FXO placement");
    return w->fxo_objects[index];
}
