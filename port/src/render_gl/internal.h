#ifndef CM_RENDER_GL_INTERNAL_H
#define CM_RENDER_GL_INTERNAL_H
#include "render_gl.h"
#include "gl_api.h"

#define GLR_TEXTURE_MAX (CM_RENDER_TEXTURES*2+1)
#define GLR_TILE_QUADS (CM_MAP_LEVELS*CM_MAP_CELLS*6)
#define GLR_IMAGE_SIDE 2048u
#define GLR_IMAGE_BYTES (GLR_IMAGE_SIDE*GLR_IMAGE_SIDE)

enum {GLR_MASK=1,GLR_PACK=2,GLR_INVIS=4,GLR_AURA=8,
      GLR_BILLBOARD=16,GLR_COLORLESS=32,GLR_FLAT=64,GLR_MIRROR=2048};
enum {GLR_ORIG_FLAT=0x02000000,GLR_ORIG_WALL=0x04000000,
      GLR_ORIG_FXO=0x08000000,GLR_ORIG_PROP=0x20000000,
      GLR_ORIG_TRANSPARENT=0x40000000,GLR_ORIG_PACK=0x00080000,
      GLR_ORIG_FLOOR=0x00400000,GLR_ORIG_TOP=0x00800000};

typedef struct {
    float x,y,z,u,v;
    float half_w,depth,bottom,top;
    uint32_t light,mode;
} glr_vertex;
typedef struct {unsigned first,count;} glr_batch;

int glr_texture_decode(const render_texture *texture,int packed,
                       uint8_t *pixels,uint8_t *alpha,size_t capacity);
void glr_wall_quad(glr_vertex out[4],const world_object *o,const world_trig *trig,
                    int32_t half_columns,unsigned light,unsigned mode);
void glr_flat_quad(glr_vertex out[4],const world_object *o,int top,unsigned light,unsigned mode);
void glr_prop_quad(glr_vertex out[4],const world_object *o,const render_texture *t,
                   unsigned light,unsigned mode);
int glr_build_tiles(const world *w,glr_vertex *vertices,unsigned capacity,
                    glr_batch batches[GLR_TEXTURE_MAX],unsigned *quads);
GLuint glr_program(cm_gl_api *gl,int gles,const char *vertex,const char *fragment,int indexed);
const char *glr_world_vertex(void);
const char *glr_common_fragment(void);
const char *glr_world_fragment(void);
const char *glr_screen_vertex(void);
const char *glr_sky_fragment(void);
const char *glr_present_fragment(void);
#endif
