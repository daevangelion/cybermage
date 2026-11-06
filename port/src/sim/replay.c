#include "replay.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRACE_FIELDS_MAX (CM_TRACE_COLUMNS + 2 + CM_TRACE_RANGES_MAX)
#define TRACE_MEMORY_MAX 65536u
#define TRACE_LINE_MAX (TRACE_MEMORY_MAX * 2 + 2048u)

static int read_line(cm_file *file, uint32_t *next, const char *path,
                     char **fields, unsigned max)
{
    uint32_t start = *next, at = start;
    unsigned n = 1;
    if (at == file->size) return 0;
    fields[0] = (char *)file->data + at;
    while (at < file->size && file->data[at] != '\n') {
        uint8_t c = file->data[at];
        if (!c || c == '\r' || at - start >= TRACE_LINE_MAX) {
            *next = at;
            return cm_fail(path, at, "invalid trace line");
        }
        if (c == ',') {
            if (n == max) {
                *next = at;
                return cm_fail(path, at, "too many trace columns");
            }
            file->data[at] = 0;
            fields[n++] = (char *)file->data + at + 1;
        }
        ++at;
    }
    if (at == file->size) {
        *next = at;
        return cm_fail(path, at, "unterminated trace row");
    }
    file->data[at] = 0;
    *next = at + 1;
    return (int)n;
}

static int line(cm_replay *r, char **fields, unsigned max)
{
    return read_line(&r->file, &r->next, r->path, fields, max);
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

/* seed.csv is the trace format's named companion, sampled before the
 * original RANDOM_SEED write. Checkpoints must never provide replacement RNG. */
static int seed_metadata(cm_replay *r)
{
    static const char header[] = "bios_tick,icount,time\n";
    static const char filename[] = "seed.csv";
    const char *slash = strrchr(r->path, '/');
    size_t prefix = slash ? (size_t)(slash - r->path) + 1 : 0;
    char path[1024], text[256];
    cm_file file = {0};
    int64_t seed, instructions;
    double seconds;
    char *first, *second, *end;
    int exists, conversion_error;
    if (prefix + sizeof filename > sizeof path)
        return cm_fail(r->path, 0, "trace metadata path too long");
    memcpy(path, r->path, prefix);
    memcpy(path + prefix, filename, sizeof filename);
    exists = platform_file_exists(path);
    if (exists <= 0) return exists < 0 ? -1 : 0;
    if (platform_file_read(&file, path)) return -1;
    if (file.size >= sizeof text || file.size < sizeof header ||
        memcmp(file.data, header, sizeof header - 1) || memchr(file.data, 0, file.size)) {
        platform_file_release(&file);
        return cm_fail(r->path, 0, "invalid trace seed metadata");
    }
    memcpy(text, file.data, file.size);
    text[file.size] = 0;
    platform_file_release(&file);
    first = strchr(text + sizeof header - 1, ',');
    second = first ? strchr(first + 1, ',') : NULL;
    if (!first || !second)
        return cm_fail(r->path, 0, "invalid trace BIOS seed sample");
    *first = *second = 0;
    errno = 0;
    seconds = strtod(second + 1, &end);
    conversion_error = errno;
    if (integer(text + sizeof header - 1, 10, 0, UINT16_MAX, &seed) ||
        integer(first + 1, 10, 0, INT64_MAX, &instructions) ||
        end == second + 1 || *end != '\n' || end[1] ||
        conversion_error == ERANGE || !isfinite(seconds) || seconds < 0)
        return cm_fail(r->path, 0, "invalid trace BIOS seed sample");
    r->bios_seed = (uint16_t)seed;
    r->has_bios_seed = 1;
    return 0;
}

static int actor_clock_metadata(cm_replay *r)
{
    const char *slash = strrchr(r->path, '/');
    size_t prefix = slash ? (size_t)(slash - r->path) + 1 : 0;
    static const char filename[] = "actor-clocks.csv";
    char *path = r->actor_clock_path, *fields[3];
    int exists, n;
    if (prefix + sizeof filename > sizeof r->actor_clock_path)
        return cm_fail(r->path, 0, "actor clock metadata path too long");
    memcpy(path, r->path, prefix);
    memcpy(path + prefix, filename, sizeof filename);
    exists = platform_file_exists(path);
    if (exists <= 0) return exists;
    if (platform_file_read(&r->actor_clocks, path)) return -1;
    n = read_line(&r->actor_clocks, &r->actor_clock_next, path, fields, 3);
    if (n < 0) return -1;
    if (n != 3 ||
        strcmp(fields[0], "frame") || strcmp(fields[1], "fast_ticks") ||
        strcmp(fields[2], "normal_ticks"))
        return cm_fail(path, 0, "invalid actor clock header");
    return 0;
}

static int trace_metadata(cm_replay *r, cm_file *file, char *path, size_t capacity,
                          uint32_t *next, const char *name, const char *const *header,
                          unsigned count)
{
    const char *slash = strrchr(r->path, '/');
    size_t prefix = slash ? (size_t)(slash - r->path) + 1 : 0;
    char *fields[14];
    unsigned i;
    int exists, n;
    if (prefix + strlen(name) + 1 > capacity)
        return cm_fail(r->path, 0, "draw metadata path too long");
    memcpy(path, r->path, prefix);
    strcpy(path + prefix, name);
    exists = platform_file_exists(path);
    if (exists <= 0) return exists < 0 ? -1 : 0;
    if (platform_file_read(file, path)) return -1;
    n = read_line(file, next, path, fields, count);
    if (n < 0) return -1;
    if ((unsigned)n != count) return cm_fail(path, 0, "invalid input metadata header");
    for (i = 0; i < count; ++i)
        if (strcmp(fields[i], header[i])) return cm_fail(path, 0, "invalid input metadata header");
    return 0;
}

static int draw_peek(cm_replay *r)
{
    char *fields[5];
    int64_t word[5];
    unsigned i;
    int n;
    if (r->has_draw_pending) return 1;
    n = read_line(&r->draw_clocks, &r->draw_clock_next, r->draw_clock_path, fields, 5);
    if (n <= 0) return n;
    if (n != 5) goto bad;
    for (i = 0; i < 5; ++i)
        if (integer(fields[i], i == 3 ? 16 : 10, 0, UINT32_MAX, &word[i])) goto bad;
    if (word[2] > CM_DRAW_DEATH ||
        (word[3] != 0x16391 && word[3] != 0x19377 && word[3] != 0x193a4)) goto bad;
    r->draw_pending.frame = (uint32_t)word[0];
    r->draw_pending.draw = (uint32_t)word[1];
    r->draw_pending.phase = (uint32_t)word[2];
    r->draw_pending.site = (uint32_t)word[3];
    r->draw_pending.ticks = (uint32_t)word[4];
    r->has_draw_pending = 1;
    return 1;
bad:
    return cm_fail(r->draw_clock_path, r->draw_clock_next, "invalid draw clock row");
}

static int ui_peek(cm_replay *r)
{
    char *fields[5];
    int64_t word[4];
    unsigned i;
    int n;
    if (r->has_ui_pending) return 1;
    n = read_line(&r->ui_actions, &r->ui_action_next, r->ui_action_path, fields, 5);
    if (n <= 0) return n;
    if (n != 5) goto bad;
    for (i = 0; i < 4; ++i)
        if (integer(fields[i], 10, 0, UINT32_MAX, &word[i])) goto bad;
    if (word[2] < 1 || word[2] > 3 || word[3] > 10 ||
        hex(fields[4], r->ui_pending.name, 24) ||
        !memchr(r->ui_pending.name, 0, 24)) goto bad;
    if (word[3] == 10)
        for (i = 0; i < 24; ++i) if (r->ui_pending.name[i]) goto bad;
    r->ui_pending.frame = (uint32_t)word[0];
    r->ui_pending.ticks = (uint32_t)word[1];
    r->ui_pending.action = (uint32_t)word[2];
    r->ui_pending.slot = (uint32_t)word[3];
    r->has_ui_pending = 1;
    return 1;
bad:
    return cm_fail(r->ui_action_path, r->ui_action_next, "invalid UI action input");
}

int replay_draw_begin(cm_replay *r, uint32_t frame, unsigned phase)
{
    if (r->draw_active || phase > CM_DRAW_DEATH)
        return cm_fail(r->draw_clock_path, r->draw_clock_next, "overlapping draw phase");
    /* No next original checkpoint exists for the last native frame. Do not
     * invent its conditional timer inputs or claim its draw as verified. */
    if (r->next == r->file.size) return 0;
    if (r->draw_frame != frame) r->has_draw_frame = 0;
    r->draw_index = r->has_draw_frame ? r->draw_index + 1 : 0;
    r->draw_frame = frame; r->draw_phase = phase;
    r->draw_active = 1; r->draw_reads = 0;
    return 1;
}

static int draw_failure(cm_replay *r, const char *reason)
{
    /* Diagnostics already have process-global lifetime through cm_last_error. */
    static char message[192];
    snprintf(message, sizeof message, "frame=%u: %s", (unsigned)r->draw_frame, reason);
    return cm_fail(r->draw_clock_path, r->draw_frame, message);
}

int replay_draw_clock(cm_replay *r, uint32_t site, uint32_t *ticks)
{
    int n;
    if (!r->draw_clocks.data)
        return draw_failure(r, "missing measured draw clock file");
    n = draw_peek(r);
    if (n < 0) return -1;
    if (!r->draw_active || !n || r->draw_pending.frame != r->draw_frame ||
        r->draw_pending.draw != r->draw_index || r->draw_pending.phase != r->draw_phase ||
        r->draw_pending.site != site) {
        static char detail[160];
        snprintf(detail, sizeof detail,
            "wanted draw=%u phase=%u pc=%x; observed frame=%u draw=%u phase=%u pc=%x",
            (unsigned)r->draw_index, (unsigned)r->draw_phase, (unsigned)site,
            (unsigned)r->draw_pending.frame, (unsigned)r->draw_pending.draw,
            (unsigned)r->draw_pending.phase, (unsigned)r->draw_pending.site);
        return draw_failure(r, detail);
    }
    *ticks = r->draw_pending.ticks;
    r->has_draw_pending = 0;
    ++r->draw_reads;
    return 0;
}

int replay_draw_end(cm_replay *r)
{
    int n;
    if (!r->draw_active) return cm_fail(r->draw_clock_path, 0, "draw was not started");
    n = draw_peek(r);
    if (n < 0) return -1;
    if (n && r->draw_pending.frame == r->draw_frame &&
        r->draw_pending.draw == r->draw_index && r->draw_pending.phase == r->draw_phase)
        return draw_failure(r, "unconsumed timer read");
    if (r->draw_reads) r->has_draw_frame = 1;
    else if (r->has_draw_frame) --r->draw_index;
    r->draw_active = 0;
    return 0;
}

int replay_ui_next(cm_replay *r, uint32_t frame, cm_replay_ui *action)
{
    int n = ui_peek(r);
    if (n < 0) return -1;
    if (r->next == r->file.size) return 0; /* Unpaired terminal checkpoint. */
    if (n && r->ui_pending.frame < frame)
        return cm_fail(r->ui_action_path, r->ui_action_next, "missed UI action input");
    if (!n || r->ui_pending.frame > frame) return 0;
    *action = r->ui_pending;
    r->has_ui_pending = 0;
    return 1;
}

static int mouse_peek(cm_replay *r)
{
    char *fields[14];
    int64_t word[14];
    unsigned i;
    int n;
    if (r->has_mouse_pending) return 1;
    n = read_line(&r->mouse_inputs, &r->mouse_input_next, r->mouse_input_path, fields, 14);
    if (n <= 0) return n;
    if (n != 14) goto bad;
    for (i = 0; i < 14; ++i) {
        int64_t minimum = i == 1 ? -1 : (i == 4 || i == 5 || i == 7 || i == 8) ?
            INT32_MIN : 0;
        int64_t maximum = i == 0 ? INT64_MAX : (i == 4 || i == 5 || i == 7 || i == 8) ?
            INT32_MAX : UINT32_MAX;
        if (integer(fields[i], i == 3 ? 16 : 10, minimum, maximum, &word[i])) goto bad;
    }
    if ((uint64_t)word[0] != r->mouse_sequence || word[4] < INT16_MIN ||
        word[4] > INT16_MAX || word[5] < INT16_MIN || word[5] > INT16_MAX ||
        word[6] > 7 || word[9] > UINT16_MAX || word[10] > UINT16_MAX) goto bad;
    r->mouse_pending.frame = word[1];
    r->mouse_pending.ticks = (uint32_t)word[2];
    r->mouse_pending.caller = (uint32_t)word[3];
    r->mouse_pending.value.x = (int32_t)word[4];
    r->mouse_pending.value.y = (int32_t)word[5];
    r->mouse_pending.value.buttons = (uint16_t)word[6];
    r->mouse_pending.value.mickey_x = (int32_t)word[7];
    r->mouse_pending.value.mickey_y = (int32_t)word[8];
    r->mouse_pending.value.press_count[0] = (uint16_t)word[9];
    r->mouse_pending.value.press_count[1] = (uint16_t)word[10];
    /* Columns11..13 are original routing audits, not native state inputs. */
    ++r->mouse_sequence;
    r->has_mouse_pending = 1;
    return 1;
bad:
    return cm_fail(r->mouse_input_path, r->mouse_input_next, "invalid hardware mouse input");
}

static int mouse_pre_caller(uint32_t caller)
{
    return caller == 0x4e1ef || caller == 0x54196 || caller == 0x1ed2d ||
           caller == 0x1bb72 || caller == 0x1c16e;
}

static int16_t mouse_word(int32_t value)
{
    uint16_t bits = (uint16_t)(uint32_t)value;
    return bits <= INT16_MAX ? (int16_t)bits : (int16_t)((int)bits - 65536);
}

static void mouse_consume(cm_replay *r)
{
    r->mouse_cached = r->mouse_pending.value;
    r->has_mouse_cached = 1;
    r->has_mouse_pending = 0;
}

static void mouse_apply(const cm_replay *r, cm_input *input)
{
    const cm_replay_mouse_value *value = &r->mouse_cached;
    unsigned buttons;
    if (!r->has_mouse_cached) return;
    buttons = (value->buttons & 1u) | (value->buttons & 2u) << 1 |
              (value->buttons & 4u) >> 1;
    input->mouse_released = input->mouse_buttons & ~buttons;
    input->mouse_x = value->x;
    input->mouse_y = value->y * 2;
    input->mouse_buttons = buttons;
    input->mouse_pressed = (value->press_count[0] ? 1u : 0u) |
                           (value->press_count[1] ? 4u : 0u);
    input->mouse_press_count[0] = value->press_count[0];
    input->mouse_press_count[1] = value->press_count[1];
    input->mouse_mickey_x = mouse_word(value->mickey_x);
    input->mouse_mickey_y = mouse_word(value->mickey_y);
}

static int mouse_failure(cm_replay *r, uint32_t frame, const char *reason)
{
    static char message[160];
    snprintf(message, sizeof message, "frame=%u: %s", (unsigned)frame, reason);
    return cm_fail(r->mouse_input_path, frame, message);
}

int replay_mouse_input(cm_replay *r, uint32_t frame, int required, cm_input *input)
{
    int n;
    if (required && !r->mouse_inputs.data)
        return mouse_failure(r, frame, "missing measured mouse input file");
    while ((n = mouse_peek(r)) > 0) {
        if (r->mouse_pending.frame > frame ||
            (r->mouse_pending.frame == frame && r->mouse_pending.caller == 0x57f38)) break;
        if ((r->mouse_pending.caller == 0x57f38 && r->mouse_pending.frame != -1) ||
            (r->mouse_pending.caller != 0x57f38 &&
             !mouse_pre_caller(r->mouse_pending.caller)))
            return mouse_failure(r, frame, "missed or unsupported ReadStatus poll");
        mouse_consume(r); /* Execute each UI/bootstrap poll; never OR counters. */
    }
    if (n < 0) return -1;
    if (required && !r->has_mouse_cached)
        return mouse_failure(r, frame, "missing initial ReadStatus input");
    mouse_apply(r, input);
    return 0;
}

int replay_mouse_poll(cm_replay *r, uint32_t frame, cm_input *input)
{
    int n = mouse_peek(r);
    if (n < 0) return -1;
    if (!n || r->mouse_pending.frame != frame || r->mouse_pending.caller != 0x57f38)
        return mouse_failure(r, frame, "missing draw ReadStatus input");
    mouse_consume(r);
    mouse_apply(r, input);
    return 0;
}

static int death_next(cm_replay *r, uint32_t *frame, uint32_t *site, uint32_t *value)
{
    char *fields[3];
    int64_t word[3];
    unsigned i;
    int n = read_line(&r->death_inputs, &r->death_input_next, r->death_input_path, fields, 3);
    if (n <= 0) return n;
    if (n != 3) goto bad;
    for (i = 0; i < 3; ++i)
        if (integer(fields[i], i == 1 ? 16 : 10, 0, UINT32_MAX, &word[i])) goto bad;
    if (word[1] != 0x153fd && word[1] != 0x15403 &&
        word[1] != 0x1543f && word[1] != 0x1555a) goto bad;
    if (word[1] == 0x1555a && word[2] > UINT8_MAX) goto bad;
    *frame = (uint32_t)word[0]; *site = (uint32_t)word[1]; *value = (uint32_t)word[2];
    return 1;
bad:
    return cm_fail(r->death_input_path, r->death_input_next, "invalid death timer/key input");
}

int replay_death_read(cm_replay *r, uint32_t frame, uint32_t site, uint32_t *value)
{
    static char message[160];
    uint32_t observed_frame, observed_site;
    int n = death_next(r, &observed_frame, &observed_site, value);
    if (n < 0) return -1;
    if (!n || observed_frame != frame || observed_site != site) {
        snprintf(message, sizeof message, "frame=%u: missing or wrong death input at %x",
                 (unsigned)frame, (unsigned)site);
        return cm_fail(r->death_input_path, frame, message);
    }
    return 0;
}

int replay_open(cm_replay *r, const char *path)
{
    static const char *const draw_header[5] = {"frame","draw","phase","pc","ticks"};
    static const char *const ui_header[5] = {"frame","ticks","action","slot","name_hex24"};
    static const char *const mouse_header[14] = {"sequence","frame","ticks","caller",
        "x","y","buttons","mickey_x","mickey_y","press_left","press_right",
        "in_device","mouse_present","joystick_present"};
    static const char *const death_header[3] = {"frame","pc","value"};
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
    if (seed_metadata(r) || actor_clock_metadata(r) ||
        trace_metadata(r, &r->draw_clocks, r->draw_clock_path, sizeof r->draw_clock_path,
                       &r->draw_clock_next, "draw-clocks.csv", draw_header, 5) ||
        trace_metadata(r, &r->ui_actions, r->ui_action_path, sizeof r->ui_action_path,
                       &r->ui_action_next, "ui-actions.csv", ui_header, 5) ||
        trace_metadata(r, &r->mouse_inputs, r->mouse_input_path, sizeof r->mouse_input_path,
                       &r->mouse_input_next, "mouse-input.csv", mouse_header, 14) ||
        trace_metadata(r, &r->death_inputs, r->death_input_path, sizeof r->death_input_path,
                       &r->death_input_next, "death-inputs.csv", death_header, 3)) {
        replay_close(r);
        return -1;
    }
    return 0;
bad:
    replay_close(r);
    return cm_fail(path, r->next, "invalid CMTRACE1 header");
}

void replay_close(cm_replay *r)
{
    platform_file_release(&r->file);
    platform_file_release(&r->actor_clocks);
    platform_file_release(&r->draw_clocks);
    platform_file_release(&r->ui_actions);
    platform_file_release(&r->mouse_inputs);
    platform_file_release(&r->death_inputs);
}

int replay_next(cm_replay *r, cm_replay_frame *frame)
{
    char *fields[TRACE_FIELDS_MAX];
    unsigned i;
    int n = line(r, fields, TRACE_FIELDS_MAX);
    if (n <= 0) {
        uint32_t frame, site, value;
        if (n < 0) goto bad;
        if (r->actor_clock_next != r->actor_clocks.size)
            return cm_fail(r->actor_clock_path, r->actor_clock_next, "extra actor clock rows");
        while ((n = draw_peek(r)) > 0) {
            if (!r->frame || r->draw_pending.frame != r->frame - 1)
                return cm_fail(r->draw_clock_path, r->draw_clock_next, "extra or missed draw clocks");
            r->has_draw_pending = 0;
        }
        if (n < 0) return -1;
        while ((n = ui_peek(r)) > 0) {
            if (!r->frame || r->ui_pending.frame != r->frame - 1)
                return cm_fail(r->ui_action_path, r->ui_action_next, "extra or missed UI inputs");
            r->has_ui_pending = 0;
        }
        if (n < 0) return -1;
        while ((n = mouse_peek(r)) > 0) {
            if (!r->frame || r->mouse_pending.frame != r->frame - 1)
                return mouse_failure(r, r->frame, "extra or missed mouse inputs");
            r->has_mouse_pending = 0;
        }
        if (n < 0) return -1;
        while ((n = death_next(r, &frame, &site, &value)) > 0)
            if (!r->frame || frame != r->frame - 1)
                return cm_fail(r->death_input_path, frame, "extra or missed death inputs");
        if (n < 0) return -1;
        return 0;
    }
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
    frame->has_actor_clocks = 0;
    frame->normal_actor_ticks = frame->fast_actor_ticks = 0;
    if (!r->actor_clocks.data && r->next != r->file.size)
        return cm_fail(r->actor_clock_path, r->frame, "missing measured actor clock file");
    if (r->actor_clocks.data) {
        char *clocks[3];
        int64_t clock_frame, fast, normal;
        uint32_t start = r->actor_clock_next;
        n = read_line(&r->actor_clocks, &r->actor_clock_next, r->actor_clock_path, clocks, 3);
        if (n < 0) return -1;
        if (!n && r->next == r->file.size) {
            /* The final pre-frame checkpoint may stop before UpdateObjects. */
        } else {
            if (n != 3 || integer(clocks[0], 10, 0, UINT32_MAX, &clock_frame) ||
                integer(clocks[1], 10, 0, UINT32_MAX, &fast) ||
                integer(clocks[2], 10, 0, UINT32_MAX, &normal) ||
                clock_frame != frame->values[CM_TRACE_FRAME])
                return cm_fail(r->actor_clock_path, start, "invalid or missing actor clock row");
            frame->fast_actor_ticks = (uint32_t)fast;
            frame->normal_actor_ticks = (uint32_t)normal;
            frame->has_actor_clocks = 1;
        }
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
    s->bios_shift = (uint8_t)f->values[CM_TRACE_BIOS_SHIFT];
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
