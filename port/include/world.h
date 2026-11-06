#ifndef CM_WORLD_H
#define CM_WORLD_H

#include "rsc.h"

#define CM_MAP_SIDE 32
#define CM_MAP_CELLS 1024
#define CM_MAP_LEVELS 4
#define CM_OBJECT_MAX 2842 /* Xox parallel-array stride 0x2C68 / sizeof(u32). */
#define CM_CHAIN_NODES 12392 /* 1024 heads + 0xB1A0 / sizeof(u32), MakeOMap. */
#define CM_PANEL_MAX 1536 /* Xox+A623C - Xox+A4A3C, pointer slots. */
#define CM_TRIG_SIZE 2048
#define CM_WORLD_PATH_MAX 1024

/* Offsets in comments are original Xox offsets, not native struct offsets. */
typedef struct {
    uint32_t flags;                 /* +5E7B0 */
    int32_t x, y, z;                /* +61418, +64080, +66CE8 */
    int32_t radius, height;         /* +69950, +6C5B8 */
    int32_t floor_z;                /* +78D8C */
    int32_t unk_6F220, unk_71E88;
    int32_t corner_x[4], corner_y[4]; /* +84A48, +8FBE8 */
    uint16_t heading, target_angle; /* +7E65C, +7FC90 */
    uint16_t texture;               /* +81DDE */
    int16_t person;                 /* +7B9F4 */
    uint8_t active;                 /* +7DB42 */
    uint8_t targetable;             /* +7D028 */
    uint8_t target_mode;            /* +812C4 */
    int8_t rotations;               /* +83412 */
    uint8_t prop_kind;              /* +83F2C */
    uint8_t kind;                   /* +A3DDA: FX=0, flat=1, prop=2 */
} world_object;

/* PERSON runtime record is 0xC0 bytes on DOS (LoadPERSONS 0x222F6).
 * Unknown fields stay named by record offset. No packed/native serialization. */
typedef struct {
    int16_t object, unk_0002, type, unk_0006;
    int32_t target, distance, unk_0010;
    int16_t unk_0014, unk_0016, unk_0018, unk_001A;
    int16_t state, unk_001E, unk_0020, unk_0022;
    int32_t unk_0024;
    int16_t unk_0028, unk_002A;
    int32_t unk_002C, unk_0030;
    int16_t unk_0034, status;
    int32_t unk_0038, unk_003C, unk_0040, unk_0044;
    int32_t unk_0048, unk_004C, unk_0050, unk_0054;
    int32_t unk_0058, unk_005C, unk_0060, unk_0064;
    int32_t unk_0068, unk_006C, unk_0070, unk_0074;
    int32_t unk_0078, unk_007C, unk_0080, unk_0084;
    int32_t unk_0088, unk_008C, unk_0090, unk_0094;
    int32_t unk_0098, unk_009C, unk_00A0, unk_00A4;
    int32_t unk_00A8, unk_00AC, unk_00B0;
    int16_t unk_00B4, water, unk_00B8, unk_00BA;
    uint8_t unk_00BC, unk_00BD;
    int16_t unk_00BE;
} world_person;

typedef struct {
    int32_t u, v, z, pitch, horizon;
    uint16_t angle;
} world_camera;

typedef struct {
    uint32_t wall_sides; /* Xox+BE074: low->high bytes are tile sides. */
    uint8_t floor, ceiling; /* +C308C / +C2088 backing arrays */
    uint8_t unk_C5090, region, light;
} world_cell;

typedef struct { uint16_t next, object; } world_link;
typedef struct { world_link nodes[CM_CHAIN_NODES]; } world_chain;
typedef struct { const uint8_t *data; uint32_t size; } world_span;
typedef struct {
    uint32_t width, height;
    world_span pixels;
} world_panel;
typedef struct {
    int32_t min_x, max_x, min_y, max_y;
    uint8_t enabled;
} world_aperture;
typedef struct { int32_t sin[CM_TRIG_SIZE], cos[CM_TRIG_SIZE], tan[CM_TRIG_SIZE]; } world_trig;
typedef struct { uint16_t word[3]; } world_rng;

typedef struct world {
    unsigned scene, object_count, person_count, panel_count;
    unsigned player_object, player_person;
    int32_t unk_5E7A8;
    int32_t top_level, sky_type;
    uint16_t light_type[16];
    int16_t light_param[32];
    int32_t curve_base[16], curve_shift[16], curve_scale[16];
    world_camera camera;
    world_object objects[CM_OBJECT_MAX];
    world_person persons[CM_OBJECT_MAX];
    world_cell cells[CM_MAP_LEVELS][CM_MAP_CELLS];
    uint16_t path_mask[CM_MAP_LEVELS][CM_MAP_CELLS]; /* +C8098 */
    uint16_t inertia_mask[CM_MAP_LEVELS][CM_MAP_CELLS]; /* +C6098 */
    uint32_t map_id_flags[256]; /* +5DFA4 */
    uint16_t map_id_texture[256]; /* +5E5A4 */
    world_chain omap, flatmap;
    world_aperture apertures[96];
    world_panel panels[CM_PANEL_MAX];
    world_trig trig;
    world_rng rng;
    rsc scenes, fxo, props, animations;
    cm_file panel_file;
    world_span scene_sections[9];
    char paths[5][CM_WORLD_PATH_MAX];
} world;

/* Caller supplies zero-initialized storage; close before reusing a loaded world. */
int world_load_scene(world *w, const char *asset_dir, unsigned scene);
void world_close(world *w);
int world_load_map(world *w, const uint8_t *data, uint32_t size);
void world_make_omap(world *w);
void world_omap_insert(world *w, unsigned object);
void world_omap_delete(world *w, unsigned object);
void world_fxo_map_insert(world *w, unsigned object);
void world_fxo_map_delete(world *w, unsigned object);
void world_dblfxo_map_insert(world *w, unsigned object);
void world_dblfxo_map_delete(world *w, unsigned object);
void world_flat_map_insert(world *w, unsigned object);
void world_flat_map_delete(world *w, unsigned object);

void world_trig_init(world_trig *t);
uint16_t cm_atan64k(int32_t x1, int32_t y1, int32_t x2, int32_t y2);
int32_t cm_distance(int32_t x1, int32_t y1, int32_t x2, int32_t y2);
void cm_fixed_rotate(const world_trig *t, int32_t *x, int32_t *y,
                     int32_t cx, int32_t cy, uint16_t angle);
void world_rng_seed(world_rng *r, uint16_t bios_tick);
int32_t world_random(world_rng *r);
int32_t world_rnd(world_rng *r, int32_t bound);
int world_ray_sight(world *w, unsigned source, unsigned target, int32_t distance,
                    int32_t source_z, int32_t target_z, uint16_t angle);
int world_ray_missile(world *w, unsigned source, unsigned target,
                      int32_t distance, int32_t source_z, int32_t target_z,
                      uint16_t angle, int32_t *hit_x, int32_t *hit_y,
                      int32_t *hit_z, int32_t *hit_object);

#endif
