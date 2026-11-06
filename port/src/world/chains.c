#include "world.h"

/* Original Image1::*MapInsert/Delete 0x581DC..0x58E94. Entries 0..1023
 * are tile heads; entry zero also owns the overflow free-list. Empty head
 * tombstones retain their next link. Exhaustion intentionally drops inserts.
 * [rust: native/image1/lists.rs], independently checked original FXO/DBL/FLAT. */
static unsigned tile_of(int32_t x, int32_t y)
{
    return ((uint32_t)y >> 16) * CM_MAP_SIDE | (((uint32_t)x >> 16) & 31u);
}

static void insert(world_chain *m, unsigned cell, unsigned obj)
{
    unsigned node, n;
    world_link *head;
    if (cell >= CM_MAP_CELLS || obj >= CM_OBJECT_MAX) {
        (void)cm_fail("world/map", cell, "object outside spatial map");
        return;
    }
    head = &m->nodes[cell];
    if (!head->next && !head->object) {
        head->object = (uint16_t)obj;
        return;
    }
    for (node = cell, n = 0; node && n < CM_CHAIN_NODES; ++n) {
        if (node >= CM_CHAIN_NODES) {
            (void)cm_fail("world/map", node, "invalid spatial chain");
            return;
        }
        if (m->nodes[node].object == obj) return;
        node = m->nodes[node].next;
    }
    if (node) {
        (void)cm_fail("world/map", node, "cyclic spatial chain");
        return;
    }
    if (!head->object) { head->object = (uint16_t)obj; return; }
    node = m->nodes[0].next;
    if (!node) return;
    if (node < CM_MAP_CELLS || node >= CM_CHAIN_NODES) {
        (void)cm_fail("world/map", node, "invalid free-list node");
        return;
    }
    m->nodes[0].next = m->nodes[node].next;
    m->nodes[0].object = 0;
    m->nodes[node].object = (uint16_t)obj;
    m->nodes[node].next = head->next;
    head->next = (uint16_t)node;
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

static void corners(world *w, unsigned obj, int add)
{
    const world_object *o;
    int32_t r, limit;
    uint32_t xp, xm, yp, ym;
    unsigned cells[4], i;
    if (obj >= CM_OBJECT_MAX) return;
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
        if (add) insert(&w->omap, cells[i], obj);
        else erase(&w->omap, cells[i], obj);
}

void world_omap_insert(world *w, unsigned obj) { corners(w, obj, 1); }
void world_omap_delete(world *w, unsigned obj) { corners(w, obj, 0); }

static void single(world *w, unsigned obj, int flat, int add)
{
    world_chain *m = flat ? &w->flatmap : &w->omap;
    unsigned cell;
    if (obj >= CM_OBJECT_MAX) return;
    cell = tile_of(w->objects[obj].x, w->objects[obj].y);
    if (add) insert(m, cell, obj); else erase(m, cell, obj);
}
void world_fxo_map_insert(world *w, unsigned obj) { single(w, obj, 0, 1); }
void world_fxo_map_delete(world *w, unsigned obj) { single(w, obj, 0, 0); }
void world_flat_map_insert(world *w, unsigned obj) { single(w, obj, 1, 1); }
void world_flat_map_delete(world *w, unsigned obj) { single(w, obj, 1, 0); }

static void double_fx(world *w, unsigned obj, int add)
{
    unsigned i;
    if (obj >= CM_OBJECT_MAX) return;
    for (i = 0; i < 2; ++i) {
        unsigned cell = tile_of(w->objects[obj].corner_x[i], w->objects[obj].corner_y[i]);
        if (add) insert(&w->omap, cell, obj); else erase(&w->omap, cell, obj);
    }
}
void world_dblfxo_map_insert(world *w, unsigned obj) { double_fx(w, obj, 1); }
void world_dblfxo_map_delete(world *w, unsigned obj) { double_fx(w, obj, 0); }

void world_make_omap(world *w)
{
    unsigned i;
    for (i = 0; i < CM_CHAIN_NODES; ++i) {
        uint16_t next = i < CM_MAP_CELLS ? 0 : (uint16_t)((i + 1) % CM_CHAIN_NODES);
        w->omap.nodes[i].next = w->flatmap.nodes[i].next = next;
        w->omap.nodes[i].object = w->flatmap.nodes[i].object = 0;
    }
    w->omap.nodes[0].next = w->flatmap.nodes[0].next = CM_MAP_CELLS;
    for (i = 1; i < w->object_count; ++i) {
        const world_object *o = &w->objects[i];
        if (!o->active) continue;
        if (o->flags & 0x02000000u) world_flat_map_insert(w, i);
        else if (o->flags & 0x08000000u) {
            if (o->flags & 0x4000u) world_dblfxo_map_insert(w, i);
            else world_fxo_map_insert(w, i);
        } else world_omap_insert(w, i);
    }
}
