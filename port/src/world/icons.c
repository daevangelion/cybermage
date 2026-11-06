#include "world_internal.h"

/* Item definitions use the PRP resource byte at+22. Borrow only frame0 for
 * DrawSCONObject/DrawShopObject; do not append render textures while drawing. */
int world_load_prop_icons(world *w)
{
    const uint8_t *items;
    uint32_t size,count,i;
    if (!w->prop_definitions.data || w->prop_definitions.size<1051)
        return cm_fail("SCENE.RSC/PROP",world_section_offset(w,97),
                       "truncated icon prop definitions");
    if (rsc_entry(&w->scenes,101,&items,&size)) return -1;
    if (size<154 || (count=cm_u32(items))>=172 || count>(size-154)/154)
        return cm_fail("SCENE.RSC/ITEM",world_section_offset(w,101),"invalid item definitions");
    for (i=1;i<=count;++i) {
        unsigned resource=items[i*154+22],type,frames;
        const uint8_t *def,*package;
        uint32_t length,start,end;
        world_prop_icon *icon;
        if (!resource || w->prop_icons[resource].pixels.data) continue;
        for (type=1;type<w->prop_definitions.size/1051;++type)
            if (cm_u16(w->prop_definitions.data+type*1051+28)==resource) break;
        if (type>=w->prop_definitions.size/1051)
            return cm_fail("SCENE.RSC/ITEM",world_section_offset(w,101)+i*154+22,
                           "icon resource has no prop definition");
        def=w->prop_definitions.data+type*1051;
        if (rsc_entry(&w->props,resource,&package,&length)) return -1;
        if (length<1442 || !(frames=package[0]) || frames>120 || !package[1])
            return cm_fail(w->props.path,resource,"invalid icon prop package");
        start=cm_u32(package+962);
        end=frames>1?cm_u32(package+966):length;
        icon=&w->prop_icons[resource];
        icon->height=world_s32(package+2);icon->width=world_s32(package+6);
        if (start<1442 || end<start || end>length ||
            icon->width<=0 || icon->width>1024 || icon->height<=0 || icon->height>1024)
            return cm_fail(w->props.path,start,"invalid icon frame span or dimensions");
        icon->packed=cm_u32(def+20)!=0;
        icon->pixels.data=package+start;icon->pixels.size=end-start;
    }
    return 0;
}
