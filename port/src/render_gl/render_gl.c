#include "internal.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define GLR_KEY_WORDS 23

typedef struct {
    GLint target,origin,camera,yaw,pitch,projection,focal_y,sky_type,sky_fill,sky_fog,fog_on;
    GLint lights,curves,default_curve,frame,parity_origin,overlay_on,world_map,world_rect;
} uniforms;
typedef struct {
    GLuint index,alpha;
    uint32_t revision,source_size;
    const uint8_t *source;
} image_cache;
typedef struct {
    int32_t key[GLR_KEY_WORDS];
    unsigned valid,texture,mode;
    int32_t distance;
    int32_t frame_u,frame_v;
    uint16_t base_texture,heading;
    int8_t rotations;
    uint8_t prop_kind;
} object_cache;
typedef struct {
    cm_gl_api gl;
    int open,gles,api_loaded;
    unsigned width,height,limit,frame,static_quads,loaded;
    GLuint programs[3],vao[3],vbo[2],ibo,fbo[2],color[2],depth;
    GLuint canvas_fbo,canvas_texture;
    GLuint snapshot_program;
    GLuint maps,palette,sky,white,zero,overlay,coverage,sources,overlay_maps;
    GLuint bound[8];unsigned active;
    uniforms u[3];
    uniforms snapshot_u;
    image_cache images[GLR_TEXTURE_MAX];
    object_cache objects[CM_OBJECT_MAX];
    glr_batch batches[GLR_TEXTURE_MAX];
    uint32_t tile_lights;
    uint64_t cells[CM_MAP_LEVELS][CM_MAP_CELLS];
    uint32_t id_flags[256];uint16_t id_texture[256];
    const world *world;
    const render_state *assets;
    const uint8_t *palette_source;
    uint8_t palette_bytes[768];
    world_camera camera;
    float yaw_x,yaw_y,pitch_cos,pitch_sin;
    float vertical_focal;
    int vx,vy;unsigned vw,vh;
    GLint primary_rect[4];
    int primary_valid;
    int distance_moved;
    int32_t distance_u,distance_v;
    char driver[256];
} gl_state;
static gl_state s;
static glr_vertex tile_vertices[GLR_TILE_QUADS*4];
static GLuint indices[GLR_TILE_QUADS*6];
static uint8_t image_pixels[GLR_IMAGE_BYTES],image_alpha[GLR_IMAGE_BYTES];
static GLuint read_row[CM_GL_SIZE_MAX*4];

static int error(const char *stage)
{
    GLenum e=s.gl.GetError();return e==GL_NO_ERROR ? 0:cm_fail(stage,e,"OpenGL operation rejected");
}
static void bind(unsigned unit,GLuint texture)
{
    if (s.active!=unit) {s.gl.ActiveTexture(GL_TEXTURE0+unit);s.active=unit;}
    if (s.bound[unit]!=texture) {s.gl.BindTexture(GL_TEXTURE_2D,texture);s.bound[unit]=texture;}
}
static void texture(GLuint *id,GLint format,unsigned w,unsigned h,GLenum input,GLenum type,const void *data)
{
    if (!*id) s.gl.GenTextures(1,id);
    bind(7,*id);
    s.gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    s.gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    s.gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    s.gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    s.gl.TexImage2D(GL_TEXTURE_2D,0,format,(GLsizei)w,(GLsizei)h,0,input,type,data);
}
static void locations(GLuint p,uniforms *u)
{
#define LOCATION(field,name) u->field=s.gl.GetUniformLocation(p,name)
    LOCATION(target,"u_target");LOCATION(origin,"u_origin");LOCATION(camera,"u_camera");
    LOCATION(yaw,"u_yaw");LOCATION(pitch,"u_pitch");LOCATION(projection,"u_projection");
    LOCATION(focal_y,"u_focal_y");
    LOCATION(sky_type,"u_sky_type");LOCATION(sky_fill,"u_sky_fill");LOCATION(sky_fog,"u_sky_fog");LOCATION(fog_on,"u_fog_on");
    LOCATION(lights,"u_lights");LOCATION(curves,"u_curves");LOCATION(default_curve,"u_default_curve");
    LOCATION(frame,"u_frame");LOCATION(parity_origin,"u_parity_origin");
    LOCATION(overlay_on,"u_overlay_on");
    LOCATION(world_map,"u_world_map");LOCATION(world_rect,"u_world_rect");
#undef LOCATION
}
static void sampler(GLuint p,const char *name,int unit)
{
    s.gl.Uniform1i(s.gl.GetUniformLocation(p,name),unit);
}
static void resolve_samplers(GLuint p)
{
    sampler(p,"u_scene",0);sampler(p,"u_palette",1);sampler(p,"u_overlay",2);
    sampler(p,"u_cover",3);sampler(p,"u_overlay_maps",4);sampler(p,"u_sources",5);
}
static void attributes(unsigned vao)
{
    s.gl.BindVertexArray(s.vao[vao]);s.gl.BindBuffer(GL_ARRAY_BUFFER,s.vbo[vao]);
    s.gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER,s.ibo);
    s.gl.EnableVertexAttribArray(0);s.gl.EnableVertexAttribArray(1);
    s.gl.EnableVertexAttribArray(2);s.gl.EnableVertexAttribArray(3);
    s.gl.VertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(glr_vertex),(const void *)offsetof(glr_vertex,x));
    s.gl.VertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(glr_vertex),(const void *)offsetof(glr_vertex,u));
    s.gl.VertexAttribPointer(2,4,GL_FLOAT,GL_FALSE,sizeof(glr_vertex),(const void *)offsetof(glr_vertex,half_w));
    s.gl.VertexAttribIPointer(3,2,GL_UNSIGNED_INT,sizeof(glr_vertex),(const void *)offsetof(glr_vertex,light));
}

void render_gl_close(void)
{
    unsigned i;
    if (!s.open) return;
    if (s.api_loaded) {
        for (i=0;i<3;++i) if (s.programs[i]) s.gl.DeleteProgram(s.programs[i]);
        if (s.snapshot_program) s.gl.DeleteProgram(s.snapshot_program);
        for (i=0;i<GLR_TEXTURE_MAX;++i) {
            if (s.images[i].index) s.gl.DeleteTextures(1,&s.images[i].index);
            if (s.images[i].alpha) s.gl.DeleteTextures(1,&s.images[i].alpha);
        }
        s.gl.DeleteTextures(2,s.color);s.gl.DeleteRenderbuffers(1,&s.depth);s.gl.DeleteFramebuffers(2,s.fbo);
        s.gl.DeleteTextures(1,&s.canvas_texture);s.gl.DeleteFramebuffers(1,&s.canvas_fbo);
        s.gl.DeleteTextures(1,&s.maps);s.gl.DeleteTextures(1,&s.palette);s.gl.DeleteTextures(1,&s.sky);
        s.gl.DeleteTextures(1,&s.white);s.gl.DeleteTextures(1,&s.zero);s.gl.DeleteTextures(1,&s.overlay);
        s.gl.DeleteTextures(1,&s.coverage);s.gl.DeleteTextures(1,&s.sources);s.gl.DeleteTextures(1,&s.overlay_maps);
        s.gl.DeleteVertexArrays(3,s.vao);s.gl.DeleteBuffers(2,s.vbo);s.gl.DeleteBuffers(1,&s.ibo);
    }
    if (s.open) platform_gl_close();
    memset(&s,0,sizeof s);
}

int render_gl_resize(unsigned width,unsigned height)
{
    unsigned i;
    const GLuint zero[4]={0,0,0,0};
    if (!s.open || !width || !height || width>s.limit || height>s.limit || width>CM_GL_SIZE_MAX || height>CM_GL_SIZE_MAX)
        return cm_fail("GL resize",0,"unsupported framebuffer size");
    if (width==s.width && height==s.height) return 0;
    for (i=0;i<2;++i) {
        texture(&s.color[i],GL_R8UI,width,height,GL_RED_INTEGER,GL_UNSIGNED_BYTE,NULL);
        s.gl.BindFramebuffer(GL_FRAMEBUFFER,s.fbo[i]);
        s.gl.FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,s.color[i],0);
        if (!i) {
            s.gl.BindRenderbuffer(GL_RENDERBUFFER,s.depth);
            s.gl.RenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,(GLsizei)width,(GLsizei)height);
            s.gl.FramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,s.depth);
        }
        if (s.gl.CheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)
            return cm_fail("GL framebuffer",i,"integer framebuffer incomplete");
        s.gl.Disable(GL_SCISSOR_TEST);s.gl.ClearBufferuiv(GL_COLOR,0,zero);
    }
    s.width=width;s.height=height;s.primary_valid=0;return error("GL resize");
}

int render_gl_clear(uint8_t index)
{
    const GLuint color[4]={index,0,0,0};
    const GLfloat depth=1;
    if (!s.open) return cm_fail("GL clear",0,"context not open");
    s.primary_valid=0;
    s.gl.BindFramebuffer(GL_FRAMEBUFFER,s.fbo[0]);s.gl.Disable(GL_SCISSOR_TEST);
    s.gl.DepthMask(GL_TRUE);s.gl.ClearBufferuiv(GL_COLOR,0,color);s.gl.ClearBufferfv(GL_DEPTH,0,&depth);
    return error("GL clear");
}

static int create_objects(unsigned width,unsigned height)
{
    unsigned i,drawable_width,drawable_height;
    GLint limit;
    const GLubyte *driver,*version;
    const uint8_t white=255,zero=0;
    s.open=1;s.active=8;
    if (glr_api_load(&s.gl)) {render_gl_close();return -1;}
    s.api_loaded=1;
    driver=s.gl.GetString(GL_RENDERER);version=s.gl.GetString(GL_VERSION);
    if (!driver || !version) {cm_fail("GL",0,"context has no renderer/version string");render_gl_close();return -1;}
    snprintf(s.driver,sizeof s.driver,"%s / %s",(const char *)driver,(const char *)version);
    s.gl.GetIntegerv(GL_MAX_TEXTURE_SIZE,&limit);s.limit=limit>0 ? (unsigned)limit:0;
    s.programs[0]=glr_program(&s.gl,s.gles,glr_world_vertex(),glr_world_fragment(),0);
    s.programs[1]=glr_program(&s.gl,s.gles,glr_screen_vertex(),glr_sky_fragment(),0);
    s.programs[2]=glr_program(&s.gl,s.gles,glr_screen_vertex(),glr_present_fragment(),0);
    if (!s.programs[0] || !s.programs[1] || !s.programs[2]) {render_gl_close();return -1;}
    for (i=0;i<3;++i) {
        locations(s.programs[i],s.u+i);s.gl.UseProgram(s.programs[i]);sampler(s.programs[i],"u_backdrop",4);
    }
    s.gl.UseProgram(s.programs[0]);sampler(s.programs[0],"u_index",0);sampler(s.programs[0],"u_alpha",1);
    sampler(s.programs[0],"u_maps",2);sampler(s.programs[0],"u_previous",3);
    s.gl.UseProgram(s.programs[2]);resolve_samplers(s.programs[2]);
    s.gl.PixelStorei(GL_UNPACK_ALIGNMENT,1);s.gl.PixelStorei(GL_PACK_ALIGNMENT,1);
    s.gl.GenFramebuffers(2,s.fbo);s.gl.GenRenderbuffers(1,&s.depth);
    s.gl.GenVertexArrays(3,s.vao);s.gl.GenBuffers(2,s.vbo);s.gl.GenBuffers(1,&s.ibo);
    for (i=0;i<GLR_TILE_QUADS;++i) {
        unsigned k=i*6,v=i*4;indices[k]=v;indices[k+1]=v+1;indices[k+2]=v+2;
        indices[k+3]=v+2;indices[k+4]=v+3;indices[k+5]=v;
    }
    s.gl.BindVertexArray(s.vao[0]);
    s.gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER,s.ibo);
    s.gl.BufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof indices,indices,GL_STATIC_DRAW);
    attributes(0);attributes(1);
    s.gl.BufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(CM_OBJECT_MAX*4*sizeof(glr_vertex)),NULL,GL_DYNAMIC_DRAW);
    texture(&s.white,GL_R8UI,1,1,GL_RED_INTEGER,GL_UNSIGNED_BYTE,&white);
    texture(&s.zero,GL_R8UI,1,1,GL_RED_INTEGER,GL_UNSIGNED_BYTE,&zero);
    texture(&s.sky,GL_R8UI,1,1,GL_RED_INTEGER,GL_UNSIGNED_BYTE,&zero);
    texture(&s.overlay,GL_R8UI,CM_WIDTH,CM_HEIGHT,GL_RED_INTEGER,GL_UNSIGNED_BYTE,NULL);
    texture(&s.coverage,GL_R8UI,CM_WIDTH,CM_HEIGHT,GL_RED_INTEGER,GL_UNSIGNED_BYTE,NULL);
    texture(&s.sources,GL_R32UI,CM_WIDTH,CM_HEIGHT,GL_RED_INTEGER,GL_UNSIGNED_INT,NULL);
    texture(&s.overlay_maps,GL_R8UI,256,CM_OVERLAY_MAPS,GL_RED_INTEGER,GL_UNSIGNED_BYTE,NULL);
    s.gl.Disable(GL_BLEND);s.gl.Disable(GL_DITHER);s.gl.DepthFunc(GL_LESS);
    platform_gl_size(&drawable_width,&drawable_height);
    if (drawable_width && drawable_height) {width=drawable_width;height=drawable_height;}
    if (render_gl_resize(width,height) || error("GL initialization")) {render_gl_close();return -1;}
    return 0;
}

int render_gl_open(int headless,unsigned width,unsigned height)
{
    if (s.open) return cm_fail("GL",0,"renderer already open");
    if (platform_gl_open(headless,width,height,&s.gles)) return -1;
    return create_objects(width,height);
}

int render_gl_reset(void)
{
    const world *w=s.world;
    const render_state *assets=s.assets;
    unsigned width=s.width,height=s.height,loaded=s.loaded;
    unsigned frame=s.frame;
    int gles=s.gles;
    const uint8_t *palette=s.palette_source;
    if (!s.open) return cm_fail("GL reset",0,"context not open");
    /* Context names are already invalid. Never delete them in the new context. */
    memset(&s,0,sizeof s);
    s.gles=gles;
    if (create_objects(width,height)) return -1;
    s.frame=frame;
    if (loaded) return render_gl_load(w,assets,palette);
    return palette ? render_gl_palette(palette):0;
}

static int ensure_image(unsigned variant)
{
    unsigned id=variant/2,packed=variant&1,width,height;
    image_cache *cache;
    const render_texture *t;
    if (id==CM_RENDER_TEXTURES) return 0;
    if (id>=s.world->texture_count) return cm_fail("GL texture",id,"texture index outside world");
    cache=s.images+variant;t=s.assets->textures+id;
    if (cache->index && cache->revision==t->revision && cache->source==t->source && cache->source_size==t->source_size) return 0;
    if (glr_texture_decode(t,(int)packed,image_pixels,image_alpha,sizeof image_pixels)) return -1;
    width=(unsigned)t->height*2;height=(unsigned)t->width*2;
    texture(&cache->index,GL_R8UI,width,height,GL_RED_INTEGER,GL_UNSIGNED_BYTE,image_pixels);
    if (packed) texture(&cache->alpha,GL_R8UI,width,height,GL_RED_INTEGER,GL_UNSIGNED_BYTE,image_alpha);
    cache->revision=t->revision;cache->source=t->source;cache->source_size=t->source_size;
    return error("GL texture upload");
}
static int bind_image(unsigned variant)
{
    if (ensure_image(variant)) return -1;
    bind(0,variant/2==CM_RENDER_TEXTURES ? s.zero:s.images[variant].index);
    bind(1,(variant&1) ? s.images[variant].alpha:s.white);return 0;
}
static uint64_t cell_key(const world_cell *c)
{
    return c->wall_sides|(uint64_t)c->floor<<32|(uint64_t)c->ceiling<<40|(uint64_t)c->light<<48;
}
static int static_mesh(const world *w,int force)
{
    unsigned l,t,i;int changed=force;
    if (memcmp(w->map_id_flags,s.id_flags,sizeof s.id_flags) || memcmp(w->map_id_texture,s.id_texture,sizeof s.id_texture)) changed=1;
    for (l=0;l<CM_MAP_LEVELS && !changed;++l) for (t=0;t<CM_MAP_CELLS;++t)
        if (cell_key(&w->cells[l][t])!=s.cells[l][t]) {changed=1;break;}
    if (!changed) return 0;
    if (glr_build_tiles(w,tile_vertices,GLR_TILE_QUADS,s.batches,&s.static_quads)) return -1;
    s.tile_lights = 0;
    for (i=0;i<s.static_quads;++i) s.tile_lights |= UINT32_C(1) << tile_vertices[i*4].light;
    s.gl.BindBuffer(GL_ARRAY_BUFFER,s.vbo[0]);
    s.gl.BufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(s.static_quads*4*sizeof(glr_vertex)),tile_vertices,GL_STATIC_DRAW);
    for (i=0;i<GLR_TEXTURE_MAX;++i) if (s.batches[i].count && ensure_image(i)) return -1;
    for (l=0;l<CM_MAP_LEVELS;++l) for (t=0;t<CM_MAP_CELLS;++t) s.cells[l][t]=cell_key(&w->cells[l][t]);
    memcpy(s.id_flags,w->map_id_flags,sizeof s.id_flags);memcpy(s.id_texture,w->map_id_texture,sizeof s.id_texture);
    return error("GL static mesh");
}

int render_gl_palette(const uint8_t palette[768])
{
    int changed;
    if (!s.open || !palette) return cm_fail("GL palette",0,"missing context or palette");
    changed=!s.palette || memcmp(palette,s.palette_bytes,768);
    if (!s.palette) texture(&s.palette,GL_RGB8,256,1,GL_RGB,GL_UNSIGNED_BYTE,palette);
    else if (changed) {
        bind(7,s.palette);s.gl.TexSubImage2D(GL_TEXTURE_2D,0,0,0,256,1,GL_RGB,GL_UNSIGNED_BYTE,palette);
    }
    s.palette_source=palette;
    if (changed) memcpy(s.palette_bytes,palette,768);
    return error("GL palette");
}

int render_gl_load(const world *w,const render_state *assets,const uint8_t palette[768])
{
    unsigned i;
    if (!s.open || !w || !assets || !assets->shade_maps || !assets->fixed_maps || !assets->fog_maps ||
        w->object_count>CM_OBJECT_MAX || w->texture_count>CM_RENDER_TEXTURES)
        return cm_fail("GL load",0,"missing context or shared indexed assets");
    s.world=w;s.assets=assets;s.loaded=0;
    memset(s.objects,0,sizeof s.objects);
    texture(&s.maps,GL_R8UI,256,95,GL_RED_INTEGER,GL_UNSIGNED_BYTE,NULL);bind(7,s.maps);
    s.gl.TexSubImage2D(GL_TEXTURE_2D,0,0,0,256,64,GL_RED_INTEGER,GL_UNSIGNED_BYTE,assets->shade_maps);
    s.gl.TexSubImage2D(GL_TEXTURE_2D,0,0,64,256,10,GL_RED_INTEGER,GL_UNSIGNED_BYTE,assets->fixed_maps);
    s.gl.TexSubImage2D(GL_TEXTURE_2D,0,0,74,256,21,GL_RED_INTEGER,GL_UNSIGNED_BYTE,assets->fog_maps);
    if (render_gl_palette(palette)) return -1;
    if (assets->backdrop.data) texture(&s.sky,GL_R8UI,1024,128,GL_RED_INTEGER,GL_UNSIGNED_BYTE,assets->backdrop.data);
    for (i=0;i<GLR_TEXTURE_MAX;++i) {s.images[i].source=NULL;s.images[i].source_size=0;}
    if (static_mesh(w,1)) return -1;
    for (i=0;i<w->texture_count;++i) if (w->textures[i].kind!=WORLD_TEX_PANEL)
        if (ensure_image(i*2+(w->textures[i].packed ? 1u:0u))) return -1;
    s.loaded=1;return error("GL indexed assets");
}

static void view_uniforms(unsigned program,const world *w)
{
    uniforms *u=s.u+program;
    s.gl.UseProgram(s.programs[program]);
    s.gl.Uniform2i(u->target,(GLint)s.vw,(GLint)s.vh);s.gl.Uniform2i(u->origin,s.vx,s.vy);
    s.gl.Uniform3f(u->camera,(float)s.camera.u/65536,(float)s.camera.v/65536,(float)s.camera.z/65536);
    s.gl.Uniform2f(u->yaw,s.yaw_x,s.yaw_y);s.gl.Uniform2f(u->pitch,s.pitch_cos,s.pitch_sin);
    s.gl.Uniform2f(u->projection,s.vertical_focal*(float)s.vh/(float)s.vw,s.vertical_focal);
    s.gl.Uniform1f(u->focal_y,s.vertical_focal);
    s.gl.Uniform1i(u->sky_type,w->sky_type);s.gl.Uniform1i(u->sky_fill,w->sky_fill);
    s.gl.Uniform1i(u->sky_fog,w->sky_fog);s.gl.Uniform1i(u->fog_on,w->fog_on);
}
static int light_valid(const world *w,unsigned i)
{
    unsigned type;
    int param;
    if (!i) return 0; /* Light0 always uses the default distance curve. */
    if (i>=32) return cm_fail("GL light",i,"light index outside table");
    type=w->light_type[i];param=w->light_param[i];
    if (type>6 || (type==6 && i>=16) || (type==0 && (param<0 || param>=64)) ||
        (type==1 && (param<0 || param>=21)))
        return cm_fail("GL light",i,"invalid original colormap descriptor");
    return 0;
}

static int lights(const world *w)
{
    GLint light[32][2],curve[16][3],def[3];unsigned i;
    for (i=0;i<32;++i) {
        if ((s.tile_lights & (UINT32_C(1)<<i)) && light_valid(w,i)) return -1;
        light[i][0]=(GLint)w->light_type[i];light[i][1]=w->light_param[i];
    }
    for (i=0;i<16;++i) {curve[i][0]=w->curve_base[i];curve[i][1]=w->curve_shift[i];curve[i][2]=w->curve_scale[i];}
    def[0]=w->shade_override.revision ? w->shade_override.bias:w->curve_base[0];
    def[1]=w->shade_override.revision ? w->shade_override.shift:w->curve_shift[0];
    def[2]=w->shade_override.revision ? w->shade_override.scale:w->curve_scale[0];
    s.gl.Uniform2iv(s.u[0].lights,32,&light[0][0]);s.gl.Uniform3iv(s.u[0].curves,16,&curve[0][0]);
    s.gl.Uniform3iv(s.u[0].default_curve,1,def);s.gl.Uniform1i(s.u[0].frame,(GLint)(s.frame&1));return 0;
}

static int object_mesh(const world *w,unsigned id)
{
    const world_object *o=w->objects+id;object_cache *cache=s.objects+id;
    int32_t key[GLR_KEY_WORDS];glr_vertex quad[4];
    unsigned mode=0,light,texture=o->texture,i,k=0,top=0,level;
    int prop=(o->flags&GLR_ORIG_PROP)!=0,position_changed;
    const render_texture *t=NULL;
    if (!o->active || id==w->player_object || !(o->flags&(GLR_ORIG_FXO|GLR_ORIG_WALL|GLR_ORIG_FLAT|GLR_ORIG_PROP))) {cache->valid=0;return 0;}
    if (o->x<0 || o->y<0 || o->x>=32*65536 || o->y>=32*65536) {cache->valid=0;return 0;}
    position_changed=!cache->valid || cache->key[0]!=o->x || cache->key[1]!=o->y;
    if (prop) {
        int frame;
        if (cache->valid && !position_changed && cache->base_texture==o->texture && cache->heading==o->heading &&
            cache->rotations==o->rotations && cache->prop_kind==o->prop_kind &&
            cache->frame_u==s.camera.u && cache->frame_v==s.camera.v) frame=(int)(cache->texture/2);
        else {
            frame=render_prop_texture(&s.camera,o);
            cache->base_texture=o->texture;cache->heading=o->heading;
            cache->rotations=o->rotations;cache->prop_kind=o->prop_kind;
            cache->frame_u=s.camera.u;cache->frame_v=s.camera.v;
        }
        if (frame<0 || (unsigned)frame>=w->texture_count) return cm_fail("GL prop",id,"invalid animation frame");
        texture=(unsigned)frame;t=s.assets->textures+texture;
        mode=(o->flags&GLR_ORIG_PACK) ? GLR_PACK:GLR_MASK;
        if (o->flags&0x2000u) mode|=GLR_AURA;
        else if (o->flags&0x20000u) mode|=GLR_INVIS;
        level=(int64_t)o->floor_z+o->z<0 ? 0u:(unsigned)((int64_t)o->floor_z+o->z)>>16;
    } else {
        if (o->flags&GLR_ORIG_FLAT) {
            top=o->z>s.camera.z;
            if ((top && !(o->flags&GLR_ORIG_TOP)) || (!top && !(o->flags&GLR_ORIG_FLOOR)) || o->z==s.camera.z) {cache->valid=0;return 0;}
            mode=GLR_FLAT|(top ? 128u:0u);
        }
        if ((o->flags&GLR_ORIG_TRANSPARENT) && !top)
            mode|=(o->flags&GLR_ORIG_PACK) && !(mode&GLR_FLAT) ? GLR_PACK:GLR_MASK;
        level=(unsigned)o->z>>16;
    }
    if (level>=CM_MAP_LEVELS) level=CM_MAP_LEVELS-1;
    light=w->cells[level][((unsigned)o->y>>16)*32+((unsigned)o->x>>16)].light;
    if (light>=32 || texture>=w->texture_count) return cm_fail("GL object",id,"invalid light or texture");
    if (light_valid(w,light)) return -1;
    key[k++]=o->x;key[k++]=o->y;key[k++]=o->z;key[k++]=o->floor_z;
    key[k++]=o->radius;key[k++]=o->height;key[k++]=o->heading;key[k++]=(int32_t)o->flags;
    key[k++]=(int32_t)texture;key[k++]=(int32_t)light;key[k++]=(int32_t)mode;
    for (i=0;i<4;++i) {key[k++]=o->corner_x[i];key[k++]=o->corner_y[i];}
    key[k++]=t ? t->frame_half_w:0;key[k++]=t ? t->frame_depth:0;
    key[k++]=t ? t->frame_base:0;key[k++]=t ? t->frame_height:0;
    if (!cache->valid || memcmp(cache->key,key,sizeof key)) {
        if (prop) glr_prop_quad(quad,o,t,light,mode);
        else if (mode&GLR_FLAT) glr_flat_quad(quad,o,(int)top,light,mode);
        else {
            if (o->radius<=0 || o->height<=0 || s.assets->textures[texture].height<=0)
                return cm_fail("GL wall",id,"invalid wall geometry");
            glr_wall_quad(quad,o,&w->trig,s.assets->textures[texture].height,light,mode);
        }
        s.gl.BindBuffer(GL_ARRAY_BUFFER,s.vbo[1]);
        s.gl.BufferSubData(GL_ARRAY_BUFFER,(GLintptr)(id*4*sizeof(glr_vertex)),sizeof quad,quad);
        memcpy(cache->key,key,sizeof key);cache->valid=1;cache->mode=mode;
        cache->texture=texture*2+((mode&GLR_PACK) ? 1u:0u);
    }
    if (prop && (position_changed || s.distance_moved))
        cache->distance=cm_distance(s.camera.u,s.camera.v,o->x,o->y);
    return 0;
}
static int draw_object(unsigned id)
{
    object_cache *o=s.objects+id;
    int clipped_top=0;
    if (bind_image(o->texture)) return -1;
    if (o->mode&GLR_INVIS) {
        s.gl.BindFramebuffer(GL_READ_FRAMEBUFFER,s.fbo[0]);s.gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER,s.fbo[1]);
        s.gl.BlitFramebuffer(s.vx,s.vy,s.vx+(int)s.vw,s.vy+(int)s.vh,s.vx,s.vy,s.vx+(int)s.vw,s.vy+(int)s.vh,GL_COLOR_BUFFER_BIT,GL_NEAREST);
        s.gl.BindFramebuffer(GL_FRAMEBUFFER,s.fbo[0]);bind(3,s.color[1]);s.gl.DepthMask(GL_FALSE);
    }
    if (o->mode&(GLR_INVIS|GLR_AURA)) {
        const world_object *object=s.world->objects+id;
        const render_texture *t=s.assets->textures+o->texture/2;
        float dx=(float)((int64_t)object->x-s.camera.u)/65536,dy=(float)((int64_t)object->y-s.camera.v)/65536;
        float z=((float)object->floor_z+object->z-s.camera.z+
            ((object->flags&0x1000u) ? 0:t->frame_base)-t->frame_height)/65536;
        float f=dx*s.yaw_x+dy*s.yaw_y,d=f*s.pitch_cos+z*s.pitch_sin;
        if (d>0) {
            float up=z*s.pitch_cos-f*s.pitch_sin;
            float first=ceilf((float)s.vh/2-up/d*(float)s.vh*s.vertical_focal/2-0.5f);
            clipped_top=first<0 ? 0:first>(float)s.vh ? (int)s.vh:(int)first;
        }
    }
    s.gl.Uniform1i(s.u[0].parity_origin,((int)s.vh+s.vy-s.vx-clipped_top)&1);
    s.gl.DrawElements(GL_TRIANGLES,6,GL_UNSIGNED_INT,(const void *)(uintptr_t)(id*6*sizeof(GLuint)));
    if (o->mode&GLR_INVIS) s.gl.DepthMask(GL_TRUE);
    return 0;
}

static int prop_before(unsigned a,unsigned b)
{
    return s.objects[a].distance>s.objects[b].distance ||
        (s.objects[a].distance==s.objects[b].distance && a<b);
}
static void sort_props(unsigned *order,unsigned count)
{
    unsigned gap,i;
    for (gap=count/2;gap;gap/=2) for (i=gap;i<count;++i) {
        unsigned value=order[i],j=i;
        while (j>=gap && prop_before(value,order[j-gap])) {order[j]=order[j-gap];j-=gap;}
        order[j]=value;
    }
}

int render_gl_draw_view(const world *w,const world_camera *camera,const cm_gl_view *view)
{
    unsigned i,n=0,order[CM_OBJECT_MAX];const GLuint zero[4]={0,0,0,0};GLfloat one=1;
    unsigned width,height;
    int x,y;
    float pitch;
    if (!view) return cm_fail("GL view",0,"missing viewport");
    x=view->x;y=view->y;width=view->width;height=view->height;
    if (!s.loaded || !w || !camera || w!=s.world || w->object_count>CM_OBJECT_MAX ||
        view->reference_width<2 || view->reference_height<2 ||
        view->reference_width>CM_GL_SIZE_MAX || view->reference_height>CM_GL_SIZE_MAX ||
        x<0 || y<0 || !width || !height || width>s.width || height>s.height ||
        (unsigned)x>s.width-width || (unsigned)y>s.height-height) return cm_fail("GL view",0,"invalid world or view rectangle");
    if (render_sync_textures(w) || static_mesh(w,0)) return -1;
    ++s.frame;
    s.camera=*camera;s.vx=x;s.vy=(int)s.height-y-(int)height;s.vw=width;s.vh=height;
    s.camera.u=(camera->u&~255)+128;s.camera.v=(camera->v&~255)+128;
    s.distance_moved=s.distance_u!=s.camera.u || s.distance_v!=s.camera.v;
    if (!view->preserve_outside) {
        s.primary_rect[0]=s.vx;s.primary_rect[1]=s.vy;
        s.primary_rect[2]=(GLint)width;s.primary_rect[3]=(GLint)height;s.primary_valid=1;
    }
    s.vertical_focal=2.0f*(float)(view->reference_width/2)/(float)view->reference_height;
    s.yaw_x=(float)w->trig.cos[camera->angle>>5]/65536;s.yaw_y=(float)w->trig.sin[camera->angle>>5]/65536;
    pitch=-atanf((float)camera->pitch/320);s.pitch_cos=cosf(pitch);s.pitch_sin=sinf(pitch);
    s.gl.BindFramebuffer(GL_FRAMEBUFFER,s.fbo[0]);s.gl.Viewport(s.vx,s.vy,(GLsizei)width,(GLsizei)height);
    if (view->preserve_outside) {
        s.gl.Enable(GL_SCISSOR_TEST);s.gl.Scissor(s.vx,s.vy,(GLsizei)width,(GLsizei)height);
    } else s.gl.Disable(GL_SCISSOR_TEST);
    s.gl.DepthMask(GL_TRUE);s.gl.ClearBufferuiv(GL_COLOR,0,zero);s.gl.ClearBufferfv(GL_DEPTH,0,&one);
    s.gl.Enable(GL_SCISSOR_TEST);s.gl.Scissor(s.vx,s.vy,(GLsizei)width,(GLsizei)height);
    s.gl.Disable(GL_DEPTH_TEST);s.gl.Disable(GL_CULL_FACE);s.gl.BindVertexArray(s.vao[2]);
    view_uniforms(1,w);bind(4,s.sky);s.gl.DrawArrays(GL_TRIANGLES,0,3);
    view_uniforms(0,w);if (lights(w)) return -1;
    bind(2,s.maps);bind(3,s.color[1]);bind(4,s.sky);s.gl.Enable(GL_DEPTH_TEST);s.gl.Enable(GL_CULL_FACE);
    s.gl.BindVertexArray(s.vao[0]);
    for (i=0;i<GLR_TEXTURE_MAX;++i) if (s.batches[i].count) {
        if (bind_image(i)) return -1;
        s.gl.DrawElements(GL_TRIANGLES,(GLsizei)(s.batches[i].count*6),GL_UNSIGNED_INT,(const void *)(uintptr_t)(s.batches[i].first*6*sizeof(GLuint)));
    }
    s.gl.BindVertexArray(s.vao[1]);s.gl.Disable(GL_CULL_FACE);
    for (i=1;i<w->object_count;++i) {
        if (object_mesh(w,i)) return -1;
        if (!s.objects[i].valid) continue;
        if (w->objects[i].flags&GLR_ORIG_PROP) {
            order[n++]=i;
        } else if (draw_object(i)) return -1;
    }
    sort_props(order,n);
    for (i=0;i<n;++i) if (draw_object(order[i])) return -1;
    s.distance_u=s.camera.u;s.distance_v=s.camera.v;
    s.gl.Disable(GL_SCISSOR_TEST);return error("GL world draw");
}
int render_gl_draw(const world *w,const world_camera *camera)
{
    const cm_gl_view view={0,0,s.width,s.height,CM_WIDTH,CM_HEIGHT,0};
    return render_gl_draw_view(w,camera,&view);
}

static int resolve_overlay(const cm_overlay *overlay,GLuint program,const uniforms *u,
                           GLuint framebuffer,unsigned width,unsigned height)
{
    if (overlay) {
        if (overlay->error || !overlay->pixels || overlay->map_count>CM_OVERLAY_MAPS ||
            (overlay->world_map && (overlay->world_map<2 || (unsigned)overlay->world_map-2>=overlay->map_count)))
            return cm_fail("GL overlay",0,"invalid overlay recording");
        bind(7,s.overlay);s.gl.TexSubImage2D(GL_TEXTURE_2D,0,0,0,CM_WIDTH,CM_HEIGHT,GL_RED_INTEGER,GL_UNSIGNED_BYTE,overlay->pixels);
        bind(7,s.coverage);s.gl.TexSubImage2D(GL_TEXTURE_2D,0,0,0,CM_WIDTH,CM_HEIGHT,GL_RED_INTEGER,GL_UNSIGNED_BYTE,overlay->coverage);
        if (overlay->map_count) {
            bind(7,s.overlay_maps);s.gl.TexSubImage2D(GL_TEXTURE_2D,0,0,0,256,(GLsizei)overlay->map_count,GL_RED_INTEGER,GL_UNSIGNED_BYTE,overlay->maps);
            bind(7,s.sources);s.gl.TexSubImage2D(GL_TEXTURE_2D,0,0,0,CM_WIDTH,CM_HEIGHT,GL_RED_INTEGER,GL_UNSIGNED_INT,overlay->source);
        }
    }
    s.gl.BindFramebuffer(GL_FRAMEBUFFER,framebuffer);s.gl.Viewport(0,0,(GLsizei)width,(GLsizei)height);
    s.gl.Disable(GL_DEPTH_TEST);s.gl.Disable(GL_CULL_FACE);s.gl.Disable(GL_SCISSOR_TEST);
    s.gl.UseProgram(program);s.gl.BindVertexArray(s.vao[2]);
    s.gl.Uniform2i(u->target,(GLint)s.width,(GLint)s.height);
    s.gl.Uniform1i(u->overlay_on,overlay!=NULL);
    s.gl.Uniform1i(u->world_map,overlay && s.primary_valid ? overlay->world_map:0);
    s.gl.Uniform4iv(u->world_rect,1,s.primary_rect);
    bind(0,s.color[0]);bind(1,s.palette);bind(2,s.overlay);bind(3,s.coverage);bind(4,s.overlay_maps);bind(5,s.sources);
    s.gl.DrawArrays(GL_TRIANGLES,0,3);
    return 0;
}

int render_gl_present(const cm_overlay *overlay)
{
    cm_rect rectangles[CM_TOUCH_RECTS_MAX];
    unsigned count,i;
    if (!s.open || !s.palette_source) return cm_fail("GL present",0,"context/palette not loaded");
    if (memcmp(s.palette_source,s.palette_bytes,768)) {
        bind(7,s.palette);s.gl.TexSubImage2D(GL_TEXTURE_2D,0,0,0,256,1,GL_RGB,GL_UNSIGNED_BYTE,s.palette_source);
        memcpy(s.palette_bytes,s.palette_source,768);
    }
    if (resolve_overlay(overlay,s.programs[2],s.u+2,0,s.width,s.height)) return -1;
    count=platform_touch_rects((int)s.width,(int)s.height,rectangles);
    if (count) {
        const GLfloat color[4]={160.0f/255,220.0f/255,220.0f/255,1};
        s.gl.Enable(GL_SCISSOR_TEST);
        for (i=0;i<count;++i) {
            const cm_rect *r=rectangles+i;
            s.gl.Scissor(r->x,(int)s.height-r->y-r->h,r->w,r->h);
            s.gl.ClearBufferfv(GL_COLOR,0,color);
        }
        s.gl.Disable(GL_SCISSOR_TEST);
    }
    return error("GL palette and overlay resolve");
}
static int read_indexed(GLuint framebuffer,uint8_t *pixels,unsigned width,unsigned height,unsigned stride)
{
    GLint format=GL_RED_INTEGER,type=GL_UNSIGNED_BYTE;
    unsigned x,y;
    s.gl.BindFramebuffer(GL_FRAMEBUFFER,framebuffer);s.gl.ReadBuffer(GL_COLOR_ATTACHMENT0);
    if (s.gles) {
        s.gl.GetIntegerv(GL_IMPLEMENTATION_COLOR_READ_FORMAT,&format);
        s.gl.GetIntegerv(GL_IMPLEMENTATION_COLOR_READ_TYPE,&type);
    }
    if (format==GL_RED_INTEGER && type==GL_UNSIGNED_BYTE) {
        s.gl.PixelStorei(GL_PACK_ROW_LENGTH,(GLint)stride);
        s.gl.ReadPixels(0,0,(GLsizei)width,(GLsizei)height,GL_RED_INTEGER,GL_UNSIGNED_BYTE,pixels);
        s.gl.PixelStorei(GL_PACK_ROW_LENGTH,0);
    } else {
        /* ES3 guarantees RGBA_INTEGER/UNSIGNED_INT for unsigned attachments.
         * Bound row scratch avoids a native-resolution staging allocation. */
        s.gl.PixelStorei(GL_PACK_ROW_LENGTH,0);
        for (y=0;y<height;++y) {
            s.gl.ReadPixels(0,(GLint)y,(GLsizei)width,1,GL_RGBA_INTEGER,GL_UNSIGNED_INT,read_row);
            for (x=0;x<width;++x) pixels[(size_t)y*stride+x]=(uint8_t)read_row[x*4];
        }
    }
    return error("GL indexed readback");
}
int render_gl_readback(uint8_t *pixels,size_t bytes,unsigned stride)
{
    if (!s.open || !pixels || stride<s.width || s.height>SIZE_MAX/stride || bytes<(size_t)(s.height-1)*stride+s.width)
        return cm_fail("GL readback",0,"indexed destination too small");
    return read_indexed(s.fbo[0],pixels,s.width,s.height,stride);
}
int render_gl_read_rgba(uint8_t *pixels,size_t bytes)
{
    if (!s.open || !pixels || (size_t)s.width*s.height>bytes/4) return cm_fail("GL readback",0,"RGBA destination too small");
    s.gl.BindFramebuffer(GL_FRAMEBUFFER,0);s.gl.ReadPixels(0,0,(GLsizei)s.width,(GLsizei)s.height,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    return error("GL resolved readback");
}

int render_gl_read_canvas(const cm_overlay *overlay,uint8_t pixels[CM_WIDTH*CM_HEIGHT])
{
    unsigned x,y;
    GLuint source=s.fbo[0];
    if (!s.open || !pixels) return cm_fail("GL canvas",0,"context not open or missing destination");
    if (overlay || s.width!=CM_WIDTH || s.height!=CM_HEIGHT) {
        if (!s.snapshot_program) {
            s.snapshot_program=glr_program(&s.gl,s.gles,glr_screen_vertex(),glr_present_fragment(),1);
            if (!s.snapshot_program) return -1;
            locations(s.snapshot_program,&s.snapshot_u);
            s.gl.UseProgram(s.snapshot_program);resolve_samplers(s.snapshot_program);
        }
        source=s.fbo[1];
        if (s.width!=CM_WIDTH || s.height!=CM_HEIGHT) {
            if (!s.canvas_fbo) {
                s.gl.GenFramebuffers(1,&s.canvas_fbo);
                texture(&s.canvas_texture,GL_R8UI,CM_WIDTH,CM_HEIGHT,GL_RED_INTEGER,GL_UNSIGNED_BYTE,NULL);
                s.gl.BindFramebuffer(GL_FRAMEBUFFER,s.canvas_fbo);
                s.gl.FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,s.canvas_texture,0);
                if (s.gl.CheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)
                    return cm_fail("GL canvas",0,"indexed canvas framebuffer incomplete");
            }
            source=s.canvas_fbo;
        }
        if (resolve_overlay(overlay,s.snapshot_program,&s.snapshot_u,source,CM_WIDTH,CM_HEIGHT) ||
            error("GL indexed snapshot resolve")) return -1;
    }
    if (read_indexed(source,pixels,CM_WIDTH,CM_HEIGHT,CM_WIDTH)) return -1;
    for (y=0;y<CM_HEIGHT/2;++y) for (x=0;x<CM_WIDTH;++x) {
        uint8_t byte=pixels[y*CM_WIDTH+x];
        pixels[y*CM_WIDTH+x]=pixels[(CM_HEIGHT-1-y)*CM_WIDTH+x];
        pixels[(CM_HEIGHT-1-y)*CM_WIDTH+x]=byte;
    }
    return 0;
}
void render_gl_dimensions(unsigned *width,unsigned *height) {*width=s.width;*height=s.height;}
const char *render_gl_driver(void) {return s.driver;}
