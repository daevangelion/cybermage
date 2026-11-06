#include "world.h"

/* Original Image1::*MapInsert/Delete 0x581DC..0x58E94. Entries 0..1023
 * are tile heads; entry zero also owns the overflow free-list. Empty head
 * tombstones retain their next link. Exhaustion intentionally drops inserts.
 * [rust: native/image1/lists.rs], independently checked original FXO/DBL/FLAT. */
static unsigned tile_of(int32_t x, int32_t y)
{
    if (!world_xy_valid(x) || !world_xy_valid(y)) return CM_MAP_CELLS;
    return ((uint32_t)y >> 16) * CM_MAP_SIDE | ((uint32_t)x >> 16);
}

static int insert(world_chain *m, unsigned cell, unsigned obj)
{
    unsigned node, n;
    world_link *head;
    if (cell >= CM_MAP_CELLS || obj >= CM_OBJECT_MAX) {
        return cm_fail("world/map", cell, "object outside spatial map");
    }
    head = &m->nodes[cell];
    if (!head->next && !head->object) {
        head->object = (uint16_t)obj;
        return 0;
    }
    for (node = cell, n = 0; node && n < CM_CHAIN_NODES; ++n) {
        if (node >= CM_CHAIN_NODES) {
            return cm_fail("world/map", node, "invalid spatial chain");
        }
        if (m->nodes[node].object == obj) return 0;
        node = m->nodes[node].next;
    }
    if (node) {
        return cm_fail("world/map", node, "cyclic spatial chain");
    }
    if (!head->object) { head->object = (uint16_t)obj; return 0; }
    node = m->nodes[0].next;
    if (!node) return 0;
    if (node < CM_MAP_CELLS || node >= CM_CHAIN_NODES) {
        return cm_fail("world/map", node, "invalid free-list node");
    }
    m->nodes[0].next = m->nodes[node].next;
    m->nodes[0].object = 0;
    m->nodes[node].object = (uint16_t)obj;
    m->nodes[node].next = head->next;
    head->next = (uint16_t)node;
    return 0;
}

static void erase(world_chain *m, unsigned cell, unsigned obj)
{
    unsigned node = cell, prev = cell, n;
    if (cell >= CM_MAP_CELLS || obj >= CM_OBJECT_MAX) {
        (void)cm_fail("world/map", cell, "object outside spatial map");
        return;
    }
    for (n = 0; node && n < CM_CHAIN_NODES; ++n) {
        if (node >= CM_CHAIN_NODES) {
            (void)cm_fail("world/map", node, "invalid spatial chain");
            return;
        }
        if (m->nodes[node].object == obj) break;
        prev = node;
        node = m->nodes[node].next;
    }
    if (!node) return;
    if (n == CM_CHAIN_NODES) {
        (void)cm_fail("world/map", node, "cyclic spatial chain");
        return;
    }
    if (node == cell) m->nodes[node].object = 0;
    else {
        m->nodes[prev].next = m->nodes[node].next;
        m->nodes[node] = m->nodes[0];
        m->nodes[0].next = (uint16_t)node;
        m->nodes[0].object = 0;
    }
}

static int corners(world *w, unsigned obj, int add)
{
    const world_object *o;
    int32_t r, limit;
    uint32_t xp, xm, yp, ym;
    unsigned cells[4], i;
    if (obj >= CM_OBJECT_MAX) return cm_fail("world/map",obj,"invalid object");
    o = &w->objects[obj];
    limit = o->flags & 0x8000u ? 0x7400 : 0x3800;
    r = o->radius < limit ? o->radius : limit;
    xp = (uint32_t)o->x + (uint32_t)r;
    xm = (uint32_t)o->x - (uint32_t)r;
    yp = (uint32_t)o->y + (uint32_t)r;
    ym = (uint32_t)o->y - (uint32_t)r;
    cells[0] = tile_of((int32_t)xp, (int32_t)yp);
    cells[1] = tile_of((int32_t)xm, (int32_t)yp);
    cells[2] = tile_of((int32_t)xm, (int32_t)ym);
    cells[3] = tile_of((int32_t)xp, (int32_t)ym);
    for (i = 0; i < 4; ++i)
        if (add) { if (insert(&w->omap, cells[i], obj)) return -1; }
        else erase(&w->omap, cells[i], obj);
    return 0;
}

void world_omap_insert(world *w, unsigned obj) { (void)corners(w, obj, 1); }
void world_omap_delete(world *w, unsigned obj) { (void)corners(w, obj, 0); }

static int single(world *w, unsigned obj, int flat, int add)
{
    world_chain *m = flat ? &w->flatmap : &w->omap;
    unsigned cell;
    if (obj >= CM_OBJECT_MAX) return cm_fail("world/map",obj,"invalid object");
    cell = tile_of(w->objects[obj].x, w->objects[obj].y);
    if (add) return insert(m, cell, obj);
    erase(m, cell, obj);
    return 0;
}
void world_fxo_map_insert(world *w, unsigned obj) { (void)single(w, obj, 0, 1); }
void world_fxo_map_delete(world *w, unsigned obj) { (void)single(w, obj, 0, 0); }
void world_flat_map_insert(world *w, unsigned obj) { (void)single(w, obj, 1, 1); }
void world_flat_map_delete(world *w, unsigned obj) { (void)single(w, obj, 1, 0); }

static int double_fx(world *w, unsigned obj, int add)
{
    unsigned i;
    if (obj >= CM_OBJECT_MAX) return cm_fail("world/map",obj,"invalid object");
    for (i = 0; i < 2; ++i) {
        unsigned cell = tile_of(w->objects[obj].corner_x[i], w->objects[obj].corner_y[i]);
        if (add) { if (insert(&w->omap, cell, obj)) return -1; }
        else erase(&w->omap, cell, obj);
    }
    return 0;
}
void world_dblfxo_map_insert(world *w, unsigned obj) { (void)double_fx(w, obj, 1); }
void world_dblfxo_map_delete(world *w, unsigned obj) { (void)double_fx(w, obj, 0); }

int world_make_omap(world *w)
{
    unsigned i;
    if (w->object_count > CM_OBJECT_MAX || w->unk_144AA4 >= CM_OBJECT_MAX ||
        w->unk_144A8C >= CM_OBJECT_MAX || w->unk_144A80 >= CM_OBJECT_MAX)
        return cm_fail("world/map",w->object_count,"invalid spatial object limits");
    for (i = 0; i < CM_CHAIN_NODES; ++i) {
        uint16_t next = i < CM_MAP_CELLS ? 0 : (uint16_t)((i + 1) % CM_CHAIN_NODES);
        w->omap.nodes[i].next = w->flatmap.nodes[i].next = next;
        w->omap.nodes[i].object = w->flatmap.nodes[i].object = 0;
    }
    w->omap.nodes[0].next = w->flatmap.nodes[0].next = CM_MAP_CELLS;
    w->objects[w->unk_144AA4].active = 0;
    w->objects[w->unk_144A8C].active = 0;
    w->objects[w->unk_144A80].active = 0;
    for (i = 1; i < w->object_count; ++i) {
        const world_object *o = &w->objects[i];
        if (!o->active) continue;
        if (!world_xy_valid(o->x) || !world_xy_valid(o->y) ||
            !world_z_valid(o->z) || !world_z_valid(o->floor_z))
            return cm_fail("world/map",i,"object coordinate outside world bounds");
        if (o->flags & 0x02000000u) { if (single(w, i, 1, 1)) return -1; }
        else if (o->flags & 0x08000000u) {
            if (o->flags & 0x4000u) { if (double_fx(w, i, 1)) return -1; }
            else if (single(w, i, 0, 1)) return -1;
        } else if (corners(w, i, 1)) return -1;
    }
    return 0;
}
