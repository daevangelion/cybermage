#include "internal.h"
#include <string.h>

#define R_DECODE_BYTES (16u*1024u*1024u)
static uint8_t decoded[R_DECODE_BYTES];
static uint8_t sky_pixels[1024*128];

void render_unload(void)
{
    render_state *s=render_get_state();
    memset(s->textures,0,sizeof s->textures);
    s->textures_loaded=s->decoded_bytes=0;
    s->shade_override_revision=0;
    s->visible_world=NULL;
    s->shade_maps=s->fixed_maps=s->fog_maps=NULL;
    s->backdrop.data=NULL;s->backdrop.size=0;
}

static int colormaps(rsc *hipak,unsigned entry,unsigned minimum,const uint8_t **out)
{
    uint32_t size;
    if (rsc_entry(hipak,entry,out,&size)) return -1;
    if (size<minimum) return cm_fail(hipak->path,entry,"renderer colormap block truncated");
    return 0;
}

int r_sync_textures(const world *w,render_state *s)
{
    unsigned i,used=s->decoded_bytes;
    if (w->texture_count>CM_RENDER_TEXTURES) return r_fail(s,"texture table full",w->texture_count);
    if (w->texture_count<s->textures_loaded) return r_fail(s,"texture table changed without reload",w->texture_count);
    for (i=0;i<w->texture_count;++i) {
        const world_texture *in=&w->textures[i];
        render_texture *out=&s->textures[i];
        uint32_t width,height,bytes;
        if (i<s->textures_loaded && out->revision==in->revision &&
            out->source==in->pixels.data && out->source_size==in->pixels.size) continue;
        if (in->width<=0 || in->height<=0 || in->width>16384 || in->height>16384 || !in->pixels.data)
            return r_fail(s,"invalid texture descriptor",i);
        width=(uint32_t)in->height*2;height=(uint32_t)in->width*2;
        out->data=in->pixels.data;out->size=in->pixels.size;
        out->width=in->width;out->height=in->height;
        out->frame_half_w=in->half_w;out->frame_base=in->base;
        out->frame_depth=in->depth;out->frame_height=in->frame_height;
        /* PANEL contains both raw and packed images. Its directory has no
         * encoding tag: map/object flags select the drawer, which bounds
         * checks the borrowed span. LoadPANELS never transforms its bytes. */
        if (in->kind==WORLD_TEX_PANEL) {
            out->revision=in->revision;out->source=in->pixels.data;out->source_size=in->pixels.size;
            if (i>=s->textures_loaded) s->textures_loaded=i+1;
            continue;
        }
        if (in->packed) {
            unsigned x;
            if (width>out->size/4) return r_fail(s,"packed column table truncated",i);
            for (x=0;x<width;++x) {
                uint32_t at=r_u32(out->data+x*4);
                if (at<width*4 || at>out->size || out->size-at<2)
                    return r_fail(s,"packed column offset invalid",i);
            }
        } else {
            if (width>UINT32_MAX/height) return r_fail(s,"texture size overflow",i);
            bytes=width*height;
            if (bytes>out->size) return r_fail(s,"plain texture truncated",i);
            if (in->rotate) {
                unsigned x,y;
                if (bytes>out->decoded_capacity) {
                    if (bytes>R_DECODE_BYTES-used) return r_fail(s,"decoded texture pool full",i);
                    out->decoded_offset=used;out->decoded_capacity=bytes;used+=bytes;
                }
                out->data=decoded+out->decoded_offset;out->size=bytes;
                /* RotateObjL9066390 = transpose + reverse columns. */
                for (x=0;x<width;++x) for (y=0;y<height;++y)
                    decoded[out->decoded_offset+x*height+y]=in->pixels.data[y*width+width-1-x];
            }
        }
        if (i>=s->textures_loaded) s->textures_loaded=i+1;
        s->decoded_bytes=used;
        out->revision=in->revision;out->source=in->pixels.data;out->source_size=in->pixels.size;
    }
    return 0;
}

int render_sync_textures(const world *w)
{
    return r_sync_textures(w,render_get_state());
}

int render_load(const world *w,rsc *hipak)
{
    render_state *s=render_get_state();
    render_unload();
    if (!w || !hipak) return cm_fail("render_sw",0,"missing render resources");
    if (colormaps(hipak,32,R_SHADE_MAPS*R_COLORS,&s->shade_maps) ||
        colormaps(hipak,33,R_FIXED_MAPS*R_COLORS,&s->fixed_maps) ||
        colormaps(hipak,34,R_FOG_MAPS*R_COLORS,&s->fog_maps)) return -1;
    s->curve=0;s->shade_bias=w->curve_base[0];
    s->shade_shift=w->curve_shift[0];s->shade_scale=w->curve_scale[0];
    s->sky_fill=w->sky_fill;s->sky_fog=w->sky_fog;s->fog_on=w->fog_on;
    if (w->backdrop_resource>=0 && w->sky_type) {
        unsigned strip,row;
        /* LoadBackDrop1FB24 interleaves four256x128 HIPAK entries. */
        for (strip=0;strip<4;++strip) {
            const uint8_t *pixels;
            uint32_t size;
            if (rsc_entry(hipak,(unsigned)w->backdrop_resource+strip,&pixels,&size)) return -1;
            if (size<256*128) return r_fail(s,"backdrop strip truncated",strip);
            for (row=0;row<128;++row)
                memcpy(sky_pixels+row*1024+strip*256,pixels+row*256,256);
        }
        s->backdrop.data=sky_pixels;s->backdrop.size=sizeof sky_pixels;
    }
    if (r_sync_textures(w,s)) return -1;
    if (!s->width) return render_set_view(120,90,400,300);
    return 0;
}
