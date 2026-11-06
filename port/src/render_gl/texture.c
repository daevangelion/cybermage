#include "internal.h"
#include <limits.h>
#include <string.h>

static uint16_t word(const uint8_t *p) {return (uint16_t)(p[0]|(unsigned)p[1]<<8);}
static uint32_t dword(const uint8_t *p) {return word(p)|(uint32_t)word(p+2)<<16;}

/* Canonical GPU rows are transposed from the software column-major bytes.
 * Packed coverage is separate: opaque run texel0 is NOT transparent. */
int glr_texture_decode(const render_texture *t,int packed,uint8_t *pixels,
                       uint8_t *alpha,size_t capacity)
{
    unsigned width,height,x,y;
    if (!t->data || t->width<=0 || t->height<=0 ||
        t->width>(int)GLR_IMAGE_SIDE/2 || t->height>(int)GLR_IMAGE_SIDE/2)
        return cm_fail("GL texture",0,"texture dimensions exceed decode bound");
    width=(unsigned)t->height*2;height=(unsigned)t->width*2;
    if (height>capacity/width) return cm_fail("GL texture",0,"decode destination too small");
    if (!packed) {
        if (height>t->size/width) return cm_fail("GL texture",0,"plain pixels truncated");
        for (x=0;x<width;++x) for (y=0;y<height;++y) {
            pixels[y*width+x]=t->data[x*height+y];alpha[y*width+x]=255;
        }
        return 0;
    }
    if (width>t->size/4) return cm_fail("GL texture",0,"column table truncated");
    memset(alpha,0,(size_t)width*height);memset(pixels,0,(size_t)width*height);
    for (x=0;x<width;++x) {
        uint32_t at=dword(t->data+x*4);
        y=0;
        if (at<width*4) return cm_fail("GL texture",at,"column overlaps table");
        while (y<height) {
            int n;
            unsigned count,k;
            if (at>t->size || t->size-at<2) return cm_fail("GL texture",at,"run header truncated");
            n=(int16_t)word(t->data+at);at+=2;
            if (!n || n==INT16_MIN) return cm_fail("GL texture",at-2,"invalid run length");
            count=(unsigned)(n<0 ? -n:n);
            if (n>0 && count>t->size-at) return cm_fail("GL texture",at,"opaque run truncated");
            for (k=0;k<count && y<height;++k,++y) if (n>0) {
                pixels[y*width+x]=t->data[at+k];alpha[y*width+x]=255;
            }
            if (n>0) at+=count;
        }
    }
    return 0;
}
