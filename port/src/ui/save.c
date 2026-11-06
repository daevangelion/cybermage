#include "ui.h"

#include <string.h>

typedef struct {
    const uint8_t *read;
    uint8_t *write;
    uint32_t at;
} save_io;

static uint32_t word32(save_io *io, uint32_t value)
{
    if (io->read) value = cm_u32(io->read + io->at);
    else {
        uint8_t *p = io->write + io->at;
        p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
        p[2] = (uint8_t)(value >> 16); p[3] = (uint8_t)(value >> 24);
    }
    io->at += 4;
    return value;
}

static uint16_t word16(save_io *io, uint16_t value)
{
    if (io->read) value = cm_u16(io->read + io->at);
    else {
        io->write[io->at] = (uint8_t)value;
        io->write[io->at+1] = (uint8_t)(value >> 8);
    }
    io->at += 2;
    return value;
}

static int16_t signed16(uint16_t value)
{
    return value < 32768u ? (int16_t)value : (int16_t)((int32_t)value - 65536);
}

static int32_t signed32(uint32_t value)
{
    return value <= INT32_MAX ? (int32_t)value : -1 - (int32_t)(UINT32_MAX-value);
}

static void bytes(save_io *io, uint8_t *p, uint32_t count)
{
    if (io->read) memcpy(p, io->read + io->at, count);
    else memcpy(io->write + io->at, p, count);
    io->at += count;
}

static void words32(save_io *io, uint32_t *p, unsigned count)
{
    unsigned i;
    for (i = 0; i < count; ++i) p[i] = word32(io, p[i]);
}

static void words16(save_io *io, uint16_t *p, unsigned count)
{
    unsigned i;
    for (i = 0; i < count; ++i) p[i] = word16(io, p[i]);
}

int ui_game_names_read(cm_ui *ui, const uint8_t *data, uint32_t size)
{
    unsigned i;
    save_io io = {data, NULL, 0};
    if (size != 288)
        return cm_fail("GAMENAME.USR", size, "save names must be 288 bytes");
    for (i = 0; i < UI_SAVE_SLOTS; ++i)
        if (!memchr(data + i * UI_NAME_BYTES, 0, UI_NAME_BYTES))
            return cm_fail("GAMENAME.USR", i * UI_NAME_BYTES, "unterminated save name");
    bytes(&io, (uint8_t *)&ui->names[0][0], UI_SAVE_SLOTS * UI_NAME_BYTES);
    for (i = 0; i < UI_SAVE_SLOTS; ++i) ui->auto_ptr[i] = signed16(word16(&io, 0));
    for (i = 0; i < UI_SAVE_SLOTS; ++i) ui->auto_slot[i] = signed16(word16(&io, 0));
    ui->restart_game = signed32(word32(&io, 0));
    ui->restart_auto = signed32(word32(&io, 0));
    return 0;
}

int ui_game_names_write(const cm_ui *ui, uint8_t data[288])
{
    unsigned i;
    save_io io = {NULL, data, UI_SAVE_SLOTS * UI_NAME_BYTES};
    for (i = 0; i < UI_SAVE_SLOTS; ++i)
        if (!memchr(ui->names[i], 0, UI_NAME_BYTES))
            return cm_fail("GAMENAME.USR", i * UI_NAME_BYTES, "unterminated save name");
    memcpy(data, ui->names, UI_SAVE_SLOTS * UI_NAME_BYTES);
    for (i = 0; i < UI_SAVE_SLOTS; ++i) word16(&io, (uint16_t)ui->auto_ptr[i]);
    for (i = 0; i < UI_SAVE_SLOTS; ++i) word16(&io, (uint16_t)ui->auto_slot[i]);
    word32(&io, (uint32_t)ui->restart_game);
    word32(&io, (uint32_t)ui->restart_auto);
    return 0;
}

int ui_save_scene(const uint8_t *data, uint32_t size, unsigned *scene)
{
    uint32_t count, object_count;
    if (size < UI_SAVE_PREFIX)
        return cm_fail("SAVE*.DAT", size, "truncated save prefix");
    count = cm_u32(data + UI_SAVE_PREFIX - 4);
    object_count = cm_u32(data + 4);
    if (count > CM_OBJECT_MAX || object_count >= CM_OBJECT_MAX)
        return cm_fail("SAVE*.DAT", UI_SAVE_PREFIX - 4, "save pool count exceeds engine limit");
    if (size != UI_SAVE_PREFIX + count * UI_PERSON_BYTES)
        return cm_fail("SAVE*.DAT", UI_SAVE_PREFIX, "person records do not match save size");
    if (cm_u32(data) >= 12)
        return cm_fail("SAVE*.DAT", 0, "scene exceeds scene directory");
    if (cm_u32(data + 0x6DC) >= CM_OBJECT_MAX ||
        cm_u32(data + 0x6E0) >= count)
        return cm_fail("SAVE*.DAT", 0x6DC, "saved player index outside pool");
    *scene = cm_u32(data);
    return 0;
}

void ui_save_init(ui_save_state *save, const world *w)
{
    memset(save, 0, sizeof *save);
    /* Xox constructor0x10184–0x10206 initializes motion counters to zero
     * and +144B00 to16. Live inventory/events are exported by their owner. */
    save->unk_6D4[2] = w->player_object;
    save->unk_6D4[3] = w->player_person;
    save->unk_6D4[19] = 16;
    if (w->player_object < CM_OBJECT_MAX)
        save->unk_6D4[27] = (uint32_t)w->camera.z -
            (uint32_t)w->objects[w->player_object].floor_z;
    memcpy(save->light_param, w->light_param, sizeof save->light_param);
    memcpy(save->light_type, w->light_type, sizeof save->light_type);
    {
        unsigned i;
        for (i = 0; i < 96; ++i) save->aperture_enabled[i] = w->apertures[i].enabled;
    }
    save->loaded = 1;
}

/* SaveGame0x50235–0x50703: fixed fields, deliberately not a native struct dump. */
static void prefix(save_io *io, ui_save_state *s)
{
    words16(io, s->unk_130D50, 870);
    words32(io, s->unk_6D4, 29);
#define FIELD(name) s->name = word32(io, s->name)
    FIELD(PCBet); FIELD(PCBetPick); FIELD(PCBetOdds); FIELD(JackPot);
    FIELD(unk_BDEB8); FIELD(unk_5DFA0);
    bytes(io, s->speechLog, 63); bytes(io, s->coderStr, 24);
    words32(io, s->unk_7B7, 9);
    words32(io, s->unk_142BFC, 1536);
    FIELD(unk_1443FC); FIELD(unk_144400);
    FIELD(textMsgNum); FIELD(textMsgScen); FIELD(textCount); FIELD(textLine);
    FIELD(textAdd); FIELD(textEnd); FIELD(textSwitch1); FIELD(textSwitch0);
    FIELD(textEvent); FIELD(textTrigger); FIELD(textObj);
    FIELD(autoDestruct); FIELD(autoDTime); FIELD(gemCountDown); FIELD(gemCountDownTimer);
#undef FIELD
}

static void object_read(save_io *io, world *w)
{
    unsigned i, k;
    for (i = 0; i < CM_OBJECT_MAX; ++i) w->objects[i].flags = word32(io, 0);
#define ARRAY32(field) for (i = 0; i < CM_OBJECT_MAX; ++i) w->objects[i].field = signed32(word32(io, 0))
    ARRAY32(x); ARRAY32(y); ARRAY32(z); ARRAY32(floor_z);
#undef ARRAY32
    for (i = 0; i < CM_OBJECT_MAX; ++i) w->objects[i].active = io->read[io->at++];
    for (i = 0; i < CM_OBJECT_MAX; ++i) w->objects[i].heading = word16(io, 0);
    for (i = 0; i < CM_OBJECT_MAX; ++i) w->objects[i].target_mode = io->read[io->at++];
    for (i = 0; i < CM_OBJECT_MAX; ++i) w->objects[i].target_angle = word16(io, 0);
    for (i = 0; i < CM_OBJECT_MAX; ++i) w->objects[i].texture = word16(io, 0);
    for (i = 0; i < CM_OBJECT_MAX; ++i) {
        unsigned value = io->read[io->at++];
        w->objects[i].rotations = (int8_t)(value < 128 ? (int)value : (int)value - 256);
    }
    for (i = 0; i < CM_OBJECT_MAX; ++i)
        for (k = 0; k < 4; ++k) w->objects[i].corner_x[k] = signed32(word32(io, 0));
    for (i = 0; i < CM_OBJECT_MAX; ++i)
        for (k = 0; k < 4; ++k) w->objects[i].corner_y[k] = signed32(word32(io, 0));
}

static void object_write(save_io *io, const world *w)
{
    unsigned i, k;
#define ARRAY32(field) for (i = 0; i < CM_OBJECT_MAX; ++i) word32(io, (uint32_t)w->objects[i].field)
    ARRAY32(flags); ARRAY32(x); ARRAY32(y); ARRAY32(z); ARRAY32(floor_z);
#undef ARRAY32
    for (i = 0; i < CM_OBJECT_MAX; ++i) io->write[io->at++] = w->objects[i].active;
    for (i = 0; i < CM_OBJECT_MAX; ++i) word16(io, w->objects[i].heading);
    for (i = 0; i < CM_OBJECT_MAX; ++i) io->write[io->at++] = w->objects[i].target_mode;
    for (i = 0; i < CM_OBJECT_MAX; ++i) word16(io, w->objects[i].target_angle);
    for (i = 0; i < CM_OBJECT_MAX; ++i) word16(io, w->objects[i].texture);
    for (i = 0; i < CM_OBJECT_MAX; ++i) io->write[io->at++] = (uint8_t)w->objects[i].rotations;
    for (i = 0; i < CM_OBJECT_MAX; ++i)
        for (k = 0; k < 4; ++k) word32(io, (uint32_t)w->objects[i].corner_x[k]);
    for (i = 0; i < CM_OBJECT_MAX; ++i)
        for (k = 0; k < 4; ++k) word32(io, (uint32_t)w->objects[i].corner_y[k]);
}

static void suffix(save_io *io, ui_save_state *s)
{
    unsigned i;
    words32(io, s->unk_135ECC, 144); words32(io, s->unk_13610C, 70);
    for (i = 0; i < 32; ++i)
        s->light_param[i] = signed16(word16(io, (uint16_t)s->light_param[i]));
    words16(io, s->light_type, 32);
    words16(io, s->unk_139390, 256); words16(io, s->unk_139590, 256);
    words32(io, s->unk_139D90, 8192); bytes(io, s->aperture_enabled, 96);
    bytes(io, s->unk_142B78, 128); bytes(io, s->unk_1428F8, 256);
    s->FireCheck = word32(io, s->FireCheck);
    s->FireTilePanel = word32(io, s->FireTilePanel);
    s->FireTileArt = word32(io, s->FireTileArt);
    s->FireDamage = word32(io, s->FireDamage);
}

#define PERSON32A(X) X(target) X(distance) X(unk_0010)
#define PERSON16B(X) X(unk_0014) X(unk_0016) X(unk_0018) X(unk_001A) \
    X(state) X(unk_001E) X(unk_0020) X(unk_0022)
#define PERSON32B(X) X(unk_0038) X(unk_003C) X(unk_0040) X(unk_0044) \
    X(unk_0048) X(unk_004C) X(unk_0050) X(unk_0054) X(unk_0058) X(unk_005C) \
    X(unk_0060) X(unk_0064) X(unk_0068) X(unk_006C) X(unk_0070) X(unk_0074) \
    X(unk_0078) X(unk_007C) X(unk_0080) X(unk_0084) X(unk_0088) X(unk_008C) \
    X(unk_0090) X(unk_0094) X(unk_0098) X(unk_009C) X(unk_00A0) X(unk_00A4) \
    X(unk_00A8) X(unk_00AC) X(unk_00B0)

static void person(save_io *io, const world_person *p, world_person *out)
{
#define I16(name) if (io->read) out->name = signed16(word16(io, 0)); else word16(io, (uint16_t)p->name);
#define I32(name) if (io->read) out->name = signed32(word32(io, 0)); else word32(io, (uint32_t)p->name);
    I16(object)
    if (io->read) {
        out->unk_0002 = signed16(word16(io, 0));
        out->type = out->unk_0002;
        out->monster_index = word16(io, 0);
    } else {
        word16(io, (uint16_t)p->type);
        word16(io, (uint16_t)p->monster_index);
    }
    I16(unk_0006) PERSON32A(I32) PERSON16B(I16)
    I32(unk_0024) I16(unk_0028) I16(unk_002A) I32(unk_002C) I32(unk_0030)
    I16(unk_0034) I16(status) PERSON32B(I32)
    I16(unk_00B4) I16(water) I16(unk_00B8) I16(unk_00BA)
    if (io->read) {
        out->unk_00BC = io->read[io->at++];
        out->unk_00BD = io->read[io->at++];
    } else {
        io->write[io->at++] = p->unk_00BC;
        io->write[io->at++] = p->unk_00BD;
    }
    I16(unk_00BE)
#undef I16
#undef I32
}

int ui_save_overlay(ui_save_state *save, world *w, const uint8_t *data,
                      uint32_t size)
{
    unsigned scene, i;
    save_io io = {data, NULL, 0};
    if (ui_save_scene(data, size, &scene)) return -1;
    if (scene != w->scene)
        return cm_fail("SAVE*.DAT", 0, "overlay scene differs from loaded scene");
    word32(&io, 0);
    w->unk_5E7A8 = signed32(word32(&io, 0));
    prefix(&io, save);
    object_read(&io, w);
    for (i=1;i<CM_OBJECT_MAX;++i) {
        const world_object *o=&w->objects[i];
        uint32_t offset=!world_xy_valid(o->x)?0x4C87:
            !world_xy_valid(o->y)?0x78EF:!world_z_valid(o->z)?0xA557:
            !world_z_valid(o->floor_z)?0xD1BF:0;
        if (offset) return cm_fail("SAVE*.DAT",offset+i*4,
                                   "saved object coordinate outside world bounds");
    }
    suffix(&io, save);
    w->person_count = word32(&io, 0);
    for (i = 0; i < w->person_count; ++i) person(&io, NULL, &w->persons[i]);
    w->player_object = save->unk_6D4[2];
    w->player_person = save->unk_6D4[3];
    if (!w->player_object || w->player_object>=w->object_count)
        return cm_fail("SAVE*.DAT",0x6DC,"saved player object outside loaded scene");
    for (i=0;i<w->person_count;++i) {
        const world_person *p=&w->persons[i];
        if (p->object<=0 || (unsigned)p->object>=w->object_count ||
            p->type<0 || (unsigned)p->type>=w->monster_definitions.size/276 ||
            p->monster_index>=w->loaded_monster_count ||
            (i==w->player_person && (unsigned)p->object!=w->player_object))
            return cm_fail("SAVE*.DAT",UI_SAVE_PREFIX+i*UI_PERSON_BYTES,
                           "invalid saved person object, monster or player link");
    }
    w->camera.u = w->objects[w->player_object].x;
    w->camera.v = w->objects[w->player_object].y;
    w->camera.z = signed32((uint32_t)w->objects[w->player_object].floor_z +
        save->unk_6D4[27]);
    w->camera.angle = w->objects[w->player_object].heading;
    memcpy(w->light_param, save->light_param, sizeof save->light_param);
    memcpy(w->light_type, save->light_type, sizeof save->light_type);
    for (i = 0; i < 96; ++i) w->apertures[i].enabled = save->aperture_enabled[i];
    if (world_make_omap(w)) return -1;
    save->loaded = 1;
    return io.at == size ? 0 : cm_fail("SAVE*.DAT", io.at, "save layout size disagreement");
}

int ui_save_game(ui_save_state *save, const world *w, uint8_t *data,
                 uint32_t capacity, uint32_t *size)
{
    unsigned i;
    uint32_t need;
    save_io io = {NULL, data, 0};
    if (!save->loaded)
        return cm_fail("SAVE*.DAT", 0, "save requires initialized original subsystem state");
    if (w->person_count > CM_OBJECT_MAX || w->player_object >= CM_OBJECT_MAX ||
        w->player_person >= w->person_count || w->scene >= 12)
        return cm_fail("SAVE*.DAT", 0, "world indices exceed save limits");
    need = UI_SAVE_PREFIX + w->person_count * UI_PERSON_BYTES;
    if (capacity < need)
        return cm_fail("SAVE*.DAT", capacity, "save output buffer too small");
    save->unk_6D4[2] = w->player_object;
    save->unk_6D4[3] = w->player_person;
    memcpy(save->light_param, w->light_param, sizeof save->light_param);
    memcpy(save->light_type, w->light_type, sizeof save->light_type);
    for (i = 0; i < 96; ++i) save->aperture_enabled[i] = w->apertures[i].enabled;
    word32(&io, w->scene); word32(&io, (uint32_t)w->unk_5E7A8);
    prefix(&io, save); object_write(&io, w); suffix(&io, save);
    word32(&io, w->person_count);
    for (i = 0; i < w->person_count; ++i) person(&io, &w->persons[i], NULL);
    *size = io.at;
    return io.at == need ? 0 : cm_fail("SAVE*.DAT", io.at, "save encoder size disagreement");
}
