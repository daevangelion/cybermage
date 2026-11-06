#include "internal.h"

/* scalevs.ASM/scalevms.ASM: source column advances in16.16, destination
 * top to bottom at640-byte pitch. Packed headers count bytes as well. */
int r_column(render_state *s,const render_texture *tex,uint32_t base,
             int x,int y,int count,uint32_t step,uint32_t pos,
             const uint8_t *map,int mode)
{
    int done=0;
    uint32_t off=base;
    uint8_t *dst;
    if (count<=0) return 0;
    if (x<0 || x>=s->width || y<0 || y>s->height-count || !map || !tex->data || !s->framebuffer)
        return r_fail(s,"invalid column bounds",base);
    dst=s->framebuffer+(s->y+y)*CM_RENDER_WIDTH+s->x+x;
    if (mode<R_COLUMN_PACK) {
        for (done=0;done<count;++done) {
            uint32_t at=pos>>16;
            uint8_t pixel;
            if (base>=tex->size || at>=tex->size-base)
                return r_fail(s,"texture column truncated",base);
            pixel=tex->data[base+at];
            if (mode==R_COLUMN_SOLID || pixel) *dst=map[pixel];
            dst+=CM_RENDER_WIDTH; pos+=step;
        }
        return 0;
    }
    if (!step) return r_fail(s,"zero packed column scale",base);
    while (done<count) {
        int header,n,k;
        int32_t len,rem;
        uint32_t start,run;
        if (off>tex->size || tex->size-off<2)
            return r_fail(s,"packed run header truncated",off);
        header=(int16_t)r_u16(tex->data+off);
        if (!header || header==INT16_MIN)
            return r_fail(s,"invalid packed run length",off);
        start=(off-base+2)<<16;
        pos+=UINT32_C(0x20000);
        len=header<0 ? -header : header;
        off+=2;
        if (header>0) {
            if ((uint32_t)len>tex->size-off)
                return r_fail(s,"packed opaque run truncated",off);
            off+=(unsigned)len;
        }
        run=(uint32_t)len<<16;
        rem=(int32_t)(run-(pos-start));
        if (rem<=0) { if (header<0) pos-=run; continue; }
        n=(int)(((uint32_t)rem-1)/step)+1;
        n=r_min(n,count-done);
        if (header>0) {
            for (k=0;k<n;++k) {
                uint32_t at=pos>>16;
                unsigned parity=(s->parity+(unsigned)x+1+(unsigned)(done+k))&1;
                if (base>=tex->size || at>=tex->size-base)
                    return r_fail(s,"packed texel truncated",base);
                if (mode==R_COLUMN_PACK) *dst=map[tex->data[base+at]];
                else if (mode==R_COLUMN_INVIS) { if (parity) *dst=map[*dst]; }
                else *dst=(parity ? s->fixed_maps+9*256 : s->shade_maps)[tex->data[base+at]];
                pos+=step; dst+=CM_RENDER_WIDTH;
            }
        } else {
            pos+=step*(uint32_t)n-run;
            dst+=n*CM_RENDER_WIDTH;
        }
        done+=n;
    }
    return 0;
}
