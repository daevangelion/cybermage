#include "exe.h"

#include <string.h>

#define LE_HEADER_BYTES 132u
#define LE_OBJECT_BYTES 24u
#define LE_DATA_BASE 0x90000u

int cm_exe_init(cm_exe *e, const cm_file *f, const char *path)
{
    uint32_t le;
    uint64_t object, map;
    memset(e, 0, sizeof *e);
    e->path = path;
    if (f->size < 64 || f->data[0] != 'M' || f->data[1] != 'Z')
        return cm_fail(path, 0, "missing DOS executable header");
    le = cm_u32(f->data + 60);
    if (le > f->size || f->size - le < LE_HEADER_BYTES ||
        f->data[le] != 'L' || f->data[le+1] != 'E' ||
        f->data[le+2] || f->data[le+3] || cm_u32(f->data+le+68) < 2)
        return cm_fail(path, le, "missing little-endian LE data object");
    object = (uint64_t)le + cm_u32(f->data+le+64) + LE_OBJECT_BYTES;
    if (object + LE_OBJECT_BYTES > f->size ||
        cm_u32(f->data+object+4) != LE_DATA_BASE)
        return cm_fail(path, le+64, "unsupported game data object");
    e->base = LE_DATA_BASE;
    e->virtual_size = cm_u32(f->data+object);
    e->page_size = cm_u32(f->data+le+40);
    e->first_page = cm_u32(f->data+object+12);
    e->page_count = cm_u32(f->data+object+16);
    e->total_pages = cm_u32(f->data+le+20);
    e->data_pages = cm_u32(f->data+le+128);
    map = (uint64_t)le + cm_u32(f->data+le+72);
    if (e->virtual_size > UINT32_MAX-e->base || e->page_size < 4 ||
        e->page_size > 65536 || !e->first_page || !e->page_count ||
        e->first_page > e->total_pages ||
        e->page_count > e->total_pages-e->first_page+1 ||
        map + (uint64_t)e->total_pages*4 > f->size || e->data_pages > f->size)
        return cm_fail(path, le+40, "invalid LE page dimensions");
    e->data = f->data;
    e->size = f->size;
    e->path = path;
    e->page_map = (uint32_t)map;
    return 0;
}

static int page_span(const cm_exe *e, uint32_t offset,
                     const uint8_t **data, uint32_t *size)
{
    uint32_t page = offset/e->page_size, within = offset%e->page_size;
    uint64_t entry, at;
    uint32_t physical, available;
    if (page >= e->page_count)
        return cm_fail(e->path, e->base+offset, "table exceeds initialized LE object");
    entry = (uint64_t)e->page_map + (e->first_page+page-1u)*4u;
    physical = (uint32_t)e->data[entry]<<16 |
               (uint32_t)e->data[entry+1]<<8 | e->data[entry+2];
    if (!physical || physical > e->total_pages || e->data[entry+3])
        return cm_fail(e->path, (uint32_t)entry, "unsupported LE data page");
    at = (uint64_t)e->data_pages + (physical-1u)*(uint64_t)e->page_size + within;
    if (at >= e->size)
        return cm_fail(e->path, (uint32_t)entry, "LE data page exceeds file");
    available = e->page_size-within;
    if (available > e->size-at) available = (uint32_t)(e->size-at);
    *data = e->data+at;
    *size = available;
    return 0;
}

static int table_range(const cm_exe *e, uint32_t link, uint64_t size)
{
    if (!e->data || link < e->base || link-e->base > e->virtual_size ||
        size > e->virtual_size-(link-e->base))
        return cm_fail(e->path, link, "table exceeds LE data object");
    return 0;
}

int cm_exe_bytes(const cm_exe *e, uint32_t link, uint8_t *dst, uint32_t size)
{
    uint32_t offset = link-e->base;
    if (table_range(e, link, size)) return -1;
    while (size) {
        const uint8_t *p;
        uint32_t n;
        if (page_span(e, offset, &p, &n)) return -1;
        if (n > size) n = size;
        memcpy(dst, p, n);
        dst += n; offset += n; size -= n;
    }
    return 0;
}

int cm_exe_table(const cm_exe *e, uint32_t link, int32_t *dst, unsigned count)
{
    uint32_t offset = link-e->base;
    if (table_range(e, link, (uint64_t)count*4)) return -1;
    while (count) {
        const uint8_t *p;
        uint32_t available, n, i;
        uint8_t word[4];
        if (page_span(e, offset, &p, &available)) return -1;
        n = available/4;
        if (!n) {
            if (cm_exe_bytes(e, e->base+offset, word, 4)) return -1;
            p = word;
            n = 1;
        }
        if (n > count) n = count;
        for (i = 0; i < n; ++i) {
            uint32_t value = cm_u32(p+i*4);
            *dst++ = value <= INT32_MAX ? (int32_t)value :
                     -1-(int32_t)(UINT32_MAX-value);
        }
        offset += n*4; count -= n;
    }
    return 0;
}
