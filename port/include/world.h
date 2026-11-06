#ifndef CM_WORLD_H
#define CM_WORLD_H

#include "rsc.h"
#include <limits.h>

#define CM_MAP_SIDE 32
#define CM_MAP_CELLS 1024
#define CM_MAP_LEVELS 4
#define CM_OBJECT_MAX 2842 /* Xox parallel-array stride 0x2C68 / sizeof(u32). */
#define CM_CHAIN_NODES 12392 /* 1024 heads + 0xB1A0 / sizeof(u32), MakeOMap. */
#define CM_PANEL_MAX 1536 /* Xox+A623C - Xox+A4A3C, pointer slots. */
#define CM_TRIG_SIZE 2048
#define CM_WORLD_PATH_MAX 1024
#define CM_WORLD_XY_MAX (CM_MAP_SIDE * 65536)
/* Retail FXO z spans -98304..294912; scene11 PROP floor reaches -8192.
 * Preserve off-map art while excluding hostile int32 extremes. */
#define CM_WORLD_Z_MIN (-CM_MAP_LEVELS * 65536)
#define CM_WORLD_Z_MAX (2 * CM_MAP_LEVELS * 65536)

static inline int world_xy_valid(int32_t value)
{
    return value>=0 && value<CM_WORLD_XY_MAX;
}
static inline int world_z_valid(int32_t value)
{
    return value>=CM_WORLD_Z_MIN && value<=CM_WORLD_Z_MAX;
}

static inline int32_t world_wrap32(uint32_t v)
{
    return v<=INT32_MAX?(int32_t)v:-1-(int32_t)(UINT32_MAX-v);
}
static inline int32_t world_add32(int32_t a, int32_t b)
{
    return world_wrap32((uint32_t)a+(uint32_t)b);
}
static inline int32_t world_sub32(int32_t a, int32_t b)
{
    return world_wrap32((uint32_t)a-(uint32_t)b);
}
static inline int32_t world_mul32(int32_t a, int32_t b)
{
    return world_wrap32((uint32_t)a*(uint32_t)b);
}

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
    int16_t unk_74AF0;              /* collision half-width */
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
    unsigned placement_index; /* scene PERSON disk record; not serialized in C0. */
    unsigned monster_index; /* original compact runtime monster slot at +4 */
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
enum { WORLD_TEX_PANEL, WORLD_TEX_FXO, WORLD_TEX_PROP, WORLD_TEX_ANIM };
typedef struct {
    unsigned kind, resource, frame;
    unsigned pixel_owner; /* aliases share original mutable-buffer identity */
    int32_t width, height; /* original TEX_W/TEX_H half dimensions */
    int32_t half_w, base, depth, frame_height;
    uint8_t rotations;
    uint8_t packed, rotate;
    uint32_t revision; /* source span changed; renderers refresh cached pixels */
    world_span pixels;
} world_texture;
typedef struct {
    uint16_t textures[80];
    uint8_t prop_kind[80];
    uint8_t loaded;
} world_gsx;
typedef struct {
    int32_t min_x, max_x, min_y, max_y;
    uint8_t enabled;
} world_aperture;
typedef struct { int32_t sin[CM_TRIG_SIZE], cos[CM_TRIG_SIZE], tan[CM_TRIG_SIZE]; } world_trig;
typedef void (*world_rng_observer)(void *context, const char *file, unsigned line,
                                   int bounded, int32_t bound, int32_t value,
                                   const uint16_t before[3], const uint16_t after[3]);
typedef struct {
    uint16_t word[3];
    world_rng_observer observe;
    void *observe_context;
} world_rng;
typedef struct {
    int32_t bias, shift, scale; /* active Xox+BDECC/BDED0/BDED4 */
    uint32_t revision;         /* native read-only renderer notification */
} world_shade;

typedef struct world {
    unsigned scene, object_count, person_count, panel_count;
    unsigned player_object, player_person;
    int32_t unk_5E7A8;
    int32_t top_level, sky_type;
    int32_t unk_130D20;
    unsigned unk_144AA4, unk_144A8C, unk_144A80;
    uint16_t light_type[32];
    int16_t light_param[32];
    int32_t curve_base[16], curve_shift[16], curve_scale[16];
    world_shade shade_override;
    int backdrop_resource;
    uint8_t sky_fill, sky_fog, fog_on;
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
    unsigned texture_count, prop_count, fxo_count;
    unsigned loaded_monster_count;
    int16_t loaded_monster_types[109];
    world_texture textures[CM_PANEL_MAX];
    world_gsx gsx[55]; /* SCENE entry99: 55 records of 58 bytes. */
    uint16_t fxo_objects[2048], fxo_textures[2048], prop_objects[2048];
    int16_t fxo_types[2048], prop_types[2048];
    uint32_t prop_placements[2048]; /* disk record index before skipped PROP records */
    world_trig trig;
    world_rng rng;
    rsc scenes, fxo, props, animations;
    cm_file panel_file;
    world_span scene_sections[9];
    world_span monster_definitions, gsx_definitions, animation_definitions;
    world_span fxo_definitions, prop_definitions;
    char paths[5][CM_WORLD_PATH_MAX];
} world;

/* Caller supplies zero-initialized storage; close before reusing a loaded world. */
int world_load_scene(world *w, const char *asset_dir, unsigned scene);
void world_close(world *w);
int world_load_map(world *w, const uint8_t *data, uint32_t size);
int world_anim_frame(world *w, unsigned object, unsigned gsx, unsigned image);
int world_load_gsx(world *w, unsigned id);
int world_set_person(world *w, unsigned person, unsigned type, unsigned state,
                      int32_t x, int32_t y, int32_t z, int32_t floor_z,
                      uint16_t heading);
int world_prop_frame(world *w, unsigned resource, unsigned image,
                      const world_texture **out);
int world_load_fxo_news(world *w, unsigned resource, unsigned placement, unsigned object);
int world_fxo_object(const world *w, unsigned index);
int world_finalize_scene(world *w);
int world_make_omap(world *w);
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
void world_rng_init(world_rng *r, uint16_t bios_tick);
int32_t world_random_at(world_rng *r, const char *file, unsigned line);
int32_t world_rnd_at(world_rng *r, int32_t bound, const char *file, unsigned line);
#define world_random(r) world_random_at((r), __FILE__, __LINE__)
#define world_rnd(r, bound) world_rnd_at((r), (bound), __FILE__, __LINE__)
int world_ray_sight(world *w, unsigned source, unsigned target, int32_t distance,
                    int32_t source_z, int32_t target_z, uint16_t angle);
int world_tile_sight(const world *w, unsigned tile, int from_level, int to_level,
                      unsigned side, unsigned target_tile, int target_level);
int world_ray_missile_xyz(world *w, int32_t x, int32_t y, int32_t z,
                          int32_t tx, int32_t ty, int32_t tz, uint16_t angle,
                          int32_t distance, int32_t *hit_x, int32_t *hit_y,
                          int32_t *hit_z, int32_t *hit_distance);
int world_ray_missile(world *w, unsigned source, unsigned target,
                      int32_t distance, int32_t source_z, int32_t target_z,
                      uint16_t angle, int32_t *hit_x, int32_t *hit_y,
                      int32_t *hit_z, int32_t *hit_distance);

#endif
