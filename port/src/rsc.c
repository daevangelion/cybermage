#include "rsc.h"

#include <string.h>

static int scene_file(const char *path)
{
    const char *base = path;
    const char *p;
    char name[13];
    size_t n, i;
    for (p = path; *p; ++p)
        if (*p == '/' || *p == '\\') base = p + 1;
    n = strlen(base);
    if (n >= sizeof name) return 0;
    for (i = 0; i <= n; ++i)
        name[i] = base[i] >= 'a' && base[i] <= 'z' ?
                  (char)(base[i] - 'a' + 'A') : base[i];
    return !strcmp(name, "SCENE.RSC") || !strcmp(name, "DEMOSCEN.RSC");
}

/* CM_HI.EXE OpenGLOBALFILES 0x21328: parallel offset and length arrays. */
int rsc_parse(rsc *r, const uint8_t *data, uint32_t size, const char *path)
{
    uint32_t i, capacity, count, end;
    memset(r, 0, sizeof *r);
    if (size < 8)
        return cm_fail(path, 0, "truncated RSC header");
    capacity = cm_u32(data);
    count = cm_u32(data + 4);
    if (count > capacity)
        return cm_fail(path, 4, "record count exceeds capacity");
    /* LoadScene 0x11E3D: nine section-major groups, each capacity scenes. */
    if (scene_file(path)) {
        if (capacity > UINT32_MAX / 9)
            return cm_fail(path, 0, "scene directory capacity overflow");
        capacity *= 9;
    }
    if (capacity > (size - 8) / 8)
        return cm_fail(path, 0, "truncated RSC directory");
    end = 8 + capacity * 8;
    for (i = 0; i < capacity; ++i) {
        uint32_t at = 8 + i * 4;
        uint32_t offset = cm_u32(data + at);
        uint32_t length = cm_u32(data + at + capacity * 4);
        if (!offset && !length)
            continue;
        if (offset < end || offset > size)
            return cm_fail(path, at, "entry offset outside payload");
        if (length > size - offset)
            return cm_fail(path, at + capacity * 4, "entry size exceeds file");
    }
    r->data = data;
    r->size = size;
    r->capacity = capacity;
    r->count = count;
    r->path = path;
    return 0;
}

int rsc_open(rsc *r, const char *path)
{
    cm_file file = {0};
    memset(r, 0, sizeof *r);
    if (platform_file_read(&file, path))
        return -1;
    if (rsc_parse(r, file.data, file.size, path)) {
        platform_file_release(&file);
        return -1;
    }
    r->file = file;
    return 0;
}

int rsc_entry(rsc *r, unsigned idx, const uint8_t **p, uint32_t *len)
{
    uint32_t offset;
    *p = NULL;
    *len = 0;
    if (idx >= r->capacity)
        return cm_fail(r->path, 0, "entry index out of range");
    offset = cm_u32(r->data + 8 + idx * 4);
    if (!offset)
        return cm_fail(r->path, 8 + idx * 4, "empty entry");
    *p = r->data + offset;
    *len = cm_u32(r->data + 8 + r->capacity * 4 + idx * 4);
    return 0;
}

void rsc_close(rsc *r)
{
    platform_file_release(&r->file);
    memset(r, 0, sizeof *r);
}
