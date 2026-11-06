#include "replay.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define TRACE_FIELDS_MAX (CM_TRACE_COLUMNS + 2 + CM_TRACE_RANGES_MAX)
#define TRACE_MEMORY_MAX 65536u
#define TRACE_LINE_MAX (TRACE_MEMORY_MAX * 2 + 2048u)

static int line(cm_replay *r, char **fields, unsigned max)
{
    uint32_t start = r->next, at = start;
    unsigned n = 1;
    if (at == r->file.size) return 0;
    fields[0] = (char *)r->file.data + at;
    while (at < r->file.size && r->file.data[at] != '\n') {
        uint8_t c = r->file.data[at];
        if (!c || c == '\r' || at - start >= TRACE_LINE_MAX)
            return cm_fail(r->path, at, "invalid trace line");
        if (c == ',') {
            if (n == max) return cm_fail(r->path, at, "too many trace columns");
            r->file.data[at] = 0;
            fields[n++] = (char *)r->file.data + at + 1;
        }
        ++at;
    }
    if (at == r->file.size) return cm_fail(r->path, at, "unterminated trace row");
    r->file.data[at] = 0;
    r->next = at + 1;
    return (int)n;
}

static int integer(const char *text, unsigned base, int64_t min, int64_t max, int64_t *out)
{
    char *end;
    long long n;
    if (!*text || *text == '+' || *text == ' ' || *text == '\t') return -1;
    errno = 0;
    n = strtoll(text, &end, (int)base);
    if (*end || errno == ERANGE || n < min || n > max) return -1;
    *out = (int64_t)n;
    return 0;
}

static int nibble(unsigned c)
{
    if (c >= '0' && c <= '9') return (int)c - '0';
    if (c >= 'a' && c <= 'f') return (int)c - 'a' + 10;
    return -1;
}

static int hex(const char *text, uint8_t *out, uint32_t size)
{
    uint32_t i;
    if (strlen(text) != size * 2u) return -1;
    for (i = 0; i < size; ++i) {
        int a = nibble((unsigned char)text[i * 2]);
        int b = nibble((unsigned char)text[i * 2 + 1]);
        if (a < 0 || b < 0) return -1;
        if (out) out[i] = (uint8_t)(a * 16 + b);
    }
    return 0;
}

int replay_open(cm_replay *r, const char *path)
{
    char *fields[TRACE_FIELDS_MAX];
    int64_t n, address, length;
    uint32_t total = 0;
    unsigned i;
    memset(r, 0, sizeof *r);
    r->path = path;
    if (platform_file_read(&r->file, path)) return -1;
    if (line(r, fields, TRACE_FIELDS_MAX) != 2 || strcmp(fields[0], "CMTRACE1") ||
        integer(fields[1], 10, 0, CM_TRACE_RANGES_MAX, &n)) goto bad;
    r->range_count = (unsigned)n;
    for (i = 0; i < r->range_count; ++i) {
        if (line(r, fields, TRACE_FIELDS_MAX) != 4 || strcmp(fields[0], "range") ||
            (strcmp(fields[1], "x") && strcmp(fields[1], "l")) ||
            integer(fields[2], 16, 0, 0x20ab80, &address) ||
            integer(fields[3], 10, 1, TRACE_MEMORY_MAX, &length)) goto bad;
        r->ranges[i].relative = fields[1][0] == 'x';
        if (address + length > (r->ranges[i].relative ? 0x144b74 : 0x20ab80) ||
            total > TRACE_MEMORY_MAX - (uint32_t)length) goto bad;
        r->ranges[i].address = (uint32_t)address;
        r->ranges[i].length = (uint32_t)length;
        total += (uint32_t)length;
    }
    if (line(r, fields, TRACE_FIELDS_MAX) != CM_TRACE_COLUMNS + 3 ||
        strcmp(fields[0], "frame") || strcmp(fields[CM_TRACE_COLUMNS], "keys") ||
        strcmp(fields[CM_TRACE_COLUMNS + 1], "input_keys") ||
        strcmp(fields[CM_TRACE_COLUMNS + 2], "ranges...")) goto bad;
    return 0;
bad:
    platform_file_release(&r->file);
    return cm_fail(path, r->next, "invalid CMTRACE1 header");
}

void replay_close(cm_replay *r)
{
    platform_file_release(&r->file);
}

int replay_next(cm_replay *r, cm_replay_frame *frame)
{
    char *fields[TRACE_FIELDS_MAX];
    unsigned i;
    int n = line(r, fields, TRACE_FIELDS_MAX);
    if (n <= 0) return n;
    if ((unsigned)n != CM_TRACE_COLUMNS + 2 + r->range_count) goto bad;
    for (i = 0; i < CM_TRACE_COLUMNS; ++i) {
        if (integer(fields[i], 10, INT32_MIN, UINT32_MAX, &frame->values[i])) goto bad;
    }
    if (frame->values[CM_TRACE_FRAME] != r->frame ||
        frame->values[CM_TRACE_DELTA] < 0 || frame->values[CM_TRACE_DELTA] > SIM_FRAME_TICKS_MAX ||
        frame->values[CM_TRACE_OBJECT] < 0 || frame->values[CM_TRACE_OBJECT] >= CM_OBJECT_MAX ||
        frame->values[CM_TRACE_PERSON] < 0 || frame->values[CM_TRACE_PERSON] >= CM_OBJECT_MAX ||
        frame->values[CM_TRACE_HEADING] < 0 || frame->values[CM_TRACE_HEADING] > UINT16_MAX ||
        frame->values[CM_TRACE_KB_LATCH] < 0 || frame->values[CM_TRACE_KB_LATCH] > UINT8_MAX ||
        frame->values[CM_TRACE_BIOS_SHIFT] < 0 || frame->values[CM_TRACE_BIOS_SHIFT] > UINT8_MAX ||
        hex(fields[CM_TRACE_COLUMNS], frame->keys, SIM_SCAN_COUNT) ||
        hex(fields[CM_TRACE_COLUMNS + 1], frame->input_keys, SIM_SCAN_COUNT)) goto bad;
    for (i = 0; i < r->range_count; ++i) {
        frame->range_hex[i] = fields[CM_TRACE_COLUMNS + 2 + i];
        if (hex(frame->range_hex[i], NULL, r->ranges[i].length)) goto bad;
    }
    ++r->frame;
    return 1;
bad:
    return cm_fail(r->path, r->next, "invalid CMTRACE1 frame");
}

/* Replay sampled input, not a guessed make/break reconstruction. Zero raw
 * events must be supplied to sim_frame for this path. */
void replay_input(sim_state *s, const cm_replay_frame *f)
{
    memcpy(s->kb_scan_status, f->input_keys, SIM_SCAN_COUNT);
    s->kb_latch = (uint8_t)f->values[CM_TRACE_KB_LATCH];
    s->bios_shift = (uint8_t)f->values[CM_TRACE_BIOS_SHIFT] & 3;
    s->previous_ticks = (uint32_t)f->values[CM_TRACE_PREVIOUS];
    s->bp_limit_time = (uint32_t)f->values[CM_TRACE_LIMIT_TIME];
    s->replay_delta = (int32_t)f->values[CM_TRACE_DELTA];
}

const char *replay_compare(const world *w, const cm_replay_frame *f,
                            int64_t *expected, int64_t *actual)
{
    static const unsigned columns[] = {CM_TRACE_X, CM_TRACE_Y, CM_TRACE_Z, CM_TRACE_HEADING, CM_TRACE_PITCH};
    static const char *names[] = {"x", "y", "z", "heading", "pitch"};
    int64_t values[5];
    const world_object *o;
    unsigned i;
    if (w->player_object >= w->object_count) return "player_object";
    o = &w->objects[w->player_object];
    values[0] = o->x; values[1] = o->y; values[2] = o->floor_z;
    values[3] = o->heading; values[4] = w->camera.pitch;
    for (i = 0; i < 5; ++i) if (values[i] != f->values[columns[i]]) {
        *expected = f->values[columns[i]];
        *actual = values[i];
        return names[i];
    }
    return NULL;
}
