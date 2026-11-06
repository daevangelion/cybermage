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

uint32_t world_section_offset(const world *w, unsigned section)
{
    unsigned entry=section<8 ? section*12+w->scene:section;
    return w->scenes.data ? cm_u32(w->scenes.data+8+entry*4):0;
}

int world_section_records(world_span s, unsigned stride, const char *path,
                            uint32_t offset, unsigned *count)
{
    uint32_t n;
    if (!s.data || s.size < stride || stride < 4)
        return cm_fail(path, offset, "truncated record header");
    n = cm_u32(s.data);
    if (n >= UINT32_MAX / stride || (n + 1) * stride != s.size)
        return cm_fail(path, offset, "record count/length mismatch");
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
        if (!w->panels[i].width || !w->panels[i].height ||
            w->panels[i].width > INT32_MAX || w->panels[i].height > INT32_MAX)
            return cm_fail("PANEL",2+i*4,"invalid panel half dimensions");
        w->panels[i].pixels.data = p + at;
        w->panels[i].pixels.size = len;
        t->kind = WORLD_TEX_PANEL; t->resource = i;
        t->pixel_owner = i+1;
        t->revision = 1;
        t->height = (int32_t)w->panels[i].height;
        t->width = (int32_t)w->panels[i].width;
        t->pixels = w->panels[i].pixels;
        at += len;
    }
    if (at != size) return cm_fail("PANEL", at, "trailing panel payload");
    w->panel_count = w->texture_count = count;
    return 0;
}

static int texture_package(world *w, unsigned kind, unsigned resource,
                            unsigned packed, unsigned scale,
                            const uint8_t *selected, unsigned *base)
{
    const uint8_t *p;
    uint32_t len;
    unsigned i, count, rotations, needed;
    rsc *archive = kind == WORLD_TEX_FXO ? &w->fxo :
                   kind == WORLD_TEX_PROP ? &w->props : &w->animations;
    for (i = w->panel_count; kind != WORLD_TEX_ANIM && i < w->texture_count; ++i)
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
    needed = count;
    if (selected) {
        needed = 0;
        for (i = 0; i < count; ++i) needed += selected[i] != 0;
    }
    if (needed > CM_PANEL_MAX - w->texture_count)
        return cm_fail(archive->path, resource, "texture slots exhausted");
    *base = w->texture_count;
    for (i = 0; i < count; ++i) {
        world_texture *t;
        uint32_t start, end;
        if (selected && !selected[i]) continue;
        t = &w->textures[w->texture_count++];
        t->kind = kind; t->resource = resource; t->frame = i;
        t->pixel_owner = w->texture_count;
        t->revision = 1;
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
    /* LoadPROPDATA 0x25731..0x25877 duplicates existing pointer/dimension
     * slots, not bitmap bytes: resource12/89/91 gains16 slots,42/43 gains8. */
    if (kind == WORLD_TEX_PROP &&
        (resource == 12 || resource == 89 || resource == 91 ||
         resource == 42 || resource == 43)) {
        unsigned extra = resource == 42 || resource == 43 ? 8 : 16;
        if (count < extra || extra > CM_PANEL_MAX - w->texture_count)
            return cm_fail(archive->path, resource, "invalid directional alias slots");
        for (i = 0; i < count; ++i)
            w->textures[*base + i].rotations = (uint8_t)(extra * 2);
        for (i = 0; i < extra; ++i) {
            w->textures[w->texture_count] = w->textures[w->texture_count - extra];
            ++w->texture_count;
        }
    }
    return 0;
}

int world_texture_package(world *w, unsigned kind, unsigned resource,
                          unsigned packed, unsigned scale, unsigned *base)
{
    return texture_package(w,kind,resource,packed,scale,0,base);
}

/* GSX records (58 bytes), four 20-image groups. ANMH packages store
 * direction frames consecutively. No texture decode or drawing occurs here. */
int world_load_gsx_flags(world *w, unsigned id, unsigned mode)
{
    const uint8_t *p, *sequence;
    unsigned group, sequence_id, percent, geometry_scale, offset_scale;
    if (id >= 55 || w->gsx_definitions.size < (id + 1) * 58u)
        return cm_fail("SCENE.RSC/GSX", id, "GSX index outside database");
    if (w->gsx[id].loaded) return 0;
    p = w->gsx_definitions.data + id * 58;
    sequence_id = cm_u16(p);
    if (sequence_id >= w->animation_definitions.size / 4532)
        return cm_fail("SCENE.RSC/GSX", id, "invalid animation geometry");
    sequence = w->animation_definitions.data + sequence_id * 4532;
    percent = cm_u16(sequence + 12);
    if (!percent) percent = 100;
    /* Retail high-resolution ANMH: truncate percent*2/3 before scaling.
     * Original LoadGSX0x23CB6..23CE9; offsets scale without this reduction. */
    geometry_scale = (percent * 2 / 3) * 512 / 100;
    offset_scale = percent * 512 / 100;
    for (group = 0; group < 4; ++group) {
        int resource = world_s16(p + 2 + group * 2);
        const uint8_t *package;
        uint32_t package_size;
        uint8_t selected[120];
        unsigned base, frame, prior, raw_count, reuse = 55;
        if (resource <= 0) continue;
        /* LoadGSX reuses texture indices only for the same ANIMOBJ; a new
         * sequence needs separate geometry slots even when pixels are shared. */
        for (prior = 0; prior < 55; ++prior) {
            const uint8_t *other = w->gsx_definitions.data + prior * 58;
            if (!w->gsx[prior].loaded ||
                world_s16(other + 2 + group * 2) != resource) continue;
            if (reuse == 55 || w->gsx[prior].textures[group*20] <
                               w->gsx[reuse].textures[group*20]) reuse = prior;
            if (world_s16(other) != world_s16(p)) continue;
            for (frame = 0; frame < 20; ++frame) {
                w->gsx[id].textures[group * 20 + frame] = w->gsx[prior].textures[group * 20 + frame];
                w->gsx[id].prop_kind[group * 20 + frame] = w->gsx[prior].prop_kind[group * 20 + frame];
            }
            break;
        }
        if (prior < 55) continue;
        if (reuse < 55) {
            unsigned old_base = w->gsx[reuse].textures[group*20], n = 0;
            for (frame = 0; frame < 20; ++frame)
                n += w->gsx[reuse].prop_kind[group*20+frame];
            if (n > CM_PANEL_MAX - w->texture_count)
                return cm_fail("ANMH.RSC",resource,"aliased texture slots exhausted");
            base = w->texture_count;
            /* Original0x23B52..23C08 shares pixel/dimension pointers, rescales
             * half-width/base, but COPIES the previous depth/height offsets. */
            for (frame = 0; frame < n; ++frame) {
                world_texture *t = &w->textures[w->texture_count++];
                int64_t half_w, height;
                *t = w->textures[old_base+frame];
                half_w = (int64_t)t->height * geometry_scale;
                height = (int64_t)t->width * geometry_scale;
                if (half_w > INT32_MAX || height > INT32_MAX)
                    return cm_fail("ANMH.RSC",resource,"aliased geometry overflow");
                t->half_w = (int32_t)half_w; t->base = (int32_t)height;
            }
            for (frame = 0; frame < 20; ++frame) {
                unsigned slot = group*20+frame;
                w->gsx[id].prop_kind[slot] = w->gsx[reuse].prop_kind[slot];
                if (w->gsx[id].prop_kind[slot])
                    w->gsx[id].textures[slot] = (uint16_t)(base + w->gsx[reuse].textures[slot] - old_base);
            }
            continue;
        }
        if (rsc_entry(&w->animations,(unsigned)resource,&package,&package_size)) return -1;
        if (package_size < 1442 || !(raw_count=package[0]) || raw_count > 120 ||
            package[1] != 8 || raw_count % 8 || (mode & 4u && raw_count < 16))
            return cm_fail("ANMH.RSC", resource, "invalid animation direction groups");
        for (frame = 0; frame < raw_count; ++frame) {
            /* LoadANIMDATA0x240A0..0x24125: last group becomes1 direction,
             * penultimate becomes3 (directions0,1,7), compacting texture IDs. */
            selected[frame] = 1;
            if ((mode & 2u) && frame >= raw_count - 8 && frame != raw_count - 1) selected[frame] = 0;
            if ((mode & 4u) && frame >= raw_count - 14 && frame < raw_count - 9) selected[frame] = 0;
        }
        if (texture_package(w,WORLD_TEX_ANIM,(unsigned)resource,1,
                            geometry_scale,selected,&base)) return -1;
        for (frame = base; frame < w->texture_count; ++frame) {
            unsigned raw_frame = w->textures[frame].frame;
            unsigned image = raw_frame / 8, image_slot = group * 20 + image;
            {
                int64_t depth = (int64_t)world_s32(sequence + 0x2B4 + raw_frame * 4) * offset_scale;
                int64_t height = (int64_t)world_s32(sequence + 0xA34 + raw_frame * 4) * offset_scale;
                if (depth < INT32_MIN || depth > INT32_MAX || height < INT32_MIN || height > INT32_MAX)
                    return cm_fail("SCENE.RSC/GSX", id, "animation extent overflow");
                w->textures[frame].depth = (int32_t)depth;
                w->textures[frame].frame_height = (int32_t)height;
            }
            if (!(raw_frame % 8) || ((mode & 2u) && raw_frame == raw_count - 1)) {
                w->gsx[id].textures[image_slot] = (uint16_t)frame;
                w->gsx[id].prop_kind[image_slot] = (uint8_t)(
                    (mode & 2u) && image + 1 == raw_count / 8 ? 1 :
                    (mode & 4u) && image + 2 == raw_count / 8 ? 3 : 8);
            }
        }
    }
    w->gsx[id].loaded = 1;
    return 0;
}

int world_load_gsx(world *w, unsigned id)
{
    unsigned type;
    for (type = 1; type < w->monster_definitions.size / 276; ++type) {
        const uint8_t *mon = w->monster_definitions.data + type * 276;
        if (world_s16(mon + 0x17) == (int)id)
            return world_load_gsx_flags(w,id,mon[0x113] & 6u);
    }
    return world_load_gsx_flags(w,id,0);
}

int world_prop_texture(world *w, const uint8_t *def, unsigned *base)
{
    unsigned resource = cm_u16(def + 28), percent = cm_u16(def + 30);
    unsigned scale = (percent ? percent : 100) * 512u / 100u;
    unsigned previous = w->texture_count, i;
    if (world_texture_package(w,WORLD_TEX_PROP,resource,cm_u32(def+20)!=0,scale,base))
        return -1;
    if (*base < previous) return 0; /* original loaded-resource geometry cache */
    for (i = *base; i < w->texture_count; ++i) {
        world_texture *t = &w->textures[i];
        int32_t depth = world_s32(def + 0x43 + t->frame * 8);
        int32_t height = world_s32(def + 0x47 + t->frame * 8);
        int64_t d = (int64_t)depth * scale, h = (int64_t)height * scale;
        if (d < INT32_MIN || d > INT32_MAX || h < INT32_MIN || h > INT32_MAX)
            return cm_fail("SCENE.RSC/PROP", resource, "sprite extent overflow");
        t->depth = (int32_t)d; t->frame_height = (int32_t)h;
    }
    return 0;
}

int world_prop_frame(world *w, unsigned resource, unsigned image,
                      const world_texture **out)
{
    unsigned base, type;
    if (!out) return cm_fail("world/PROP", resource, "missing frame output");
    for (base = w->panel_count; base < w->texture_count; ++base)
        if (w->textures[base].kind == WORLD_TEX_PROP &&
            w->textures[base].resource == resource) break;
    if (base == w->texture_count) {
        for (type = 1; type < w->prop_definitions.size / 1051; ++type) {
            const uint8_t *def = w->prop_definitions.data + type * 1051;
            if (cm_u16(def + 28) != resource) continue;
            if (world_prop_texture(w,def,&base)) return -1;
            break;
        }
        if (type == w->prop_definitions.size / 1051)
            return cm_fail("world/PROP", resource, "no definition for icon resource");
    }
    if (image >= w->texture_count - base ||
        w->textures[base+image].kind != WORLD_TEX_PROP ||
        w->textures[base+image].resource != resource)
        return cm_fail("world/PROP", image, "frame outside prop resource");
    *out = &w->textures[base+image];
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

/* LoadFXONEWS0x27BE8 changes the existing definition image, including every
 * alias of its pixel buffer. Native spans replace the original in-place copy. */
int world_load_fxo_news(world *w, unsigned resource, unsigned placement, unsigned object)
{
    const uint8_t *p;
    uint32_t len;
    unsigned texture, i, owner;
    uint64_t expected;
    if (placement >= w->fxo_count || object >= w->object_count)
        return cm_fail("world/FXONEWS",placement,"invalid monitor object");
    texture = w->fxo_textures[placement];
    if (texture >= w->texture_count)
        return cm_fail("world/FXONEWS",texture,"invalid definition texture");
    if (rsc_entry(&w->fxo,resource,&p,&len)) return -1;
    expected = (uint64_t)(uint32_t)w->textures[texture].width *
               (uint32_t)w->textures[texture].height * 4;
    if (len < 4 || expected != len - 4)
        return cm_fail(w->fxo.path,resource,"monitor bitmap length differs from template");
    owner = w->textures[texture].pixel_owner;
    for (i = 0; i < w->texture_count; ++i) {
        world_texture *t = &w->textures[i];
        if (t->pixel_owner != owner) continue;
        t->kind = WORLD_TEX_FXO; t->resource = resource; t->frame = 0;
        t->pixels.data = p+4; t->pixels.size = len-4;
        t->packed = 0; t->rotate = 1; ++t->revision;
    }
    w->objects[object].texture = (uint16_t)texture;
    return 0;
}
