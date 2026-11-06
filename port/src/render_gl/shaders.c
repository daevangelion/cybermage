#include "internal.h"

const char *glr_world_vertex(void)
{
    return
    "layout(location=0) in vec3 a_position;\n"
    "layout(location=1) in vec2 a_uv;\n"
    "layout(location=2) in vec4 a_shape;\n"
    "layout(location=3) in uvec2 a_material;\n"
    "uniform vec3 u_camera;uniform vec2 u_yaw,u_pitch,u_projection;\n"
    "out vec3 v_world;out vec2 v_uv;flat out uvec2 v_material;\n"
    "void main(){vec3 p=a_position;\n"
    "if((a_material.y&16u)!=0u){p.xy+=vec2(-u_yaw.y,u_yaw.x)*((a_uv.x*2.0-1.0)*a_shape.x-a_shape.y);"
    "p.z+=mix(a_shape.w,a_shape.z,a_uv.y);}\n"
    "vec3 d=p-u_camera;float f=dot(d.xy,u_yaw);float right=dot(d.xy,vec2(u_yaw.y,-u_yaw.x));"
    "float z=f*u_pitch.x+d.z*u_pitch.y;float up=d.z*u_pitch.x-f*u_pitch.y;"
    "const float near_plane=0.01,far_plane=128.0;"
    "float za=(far_plane+near_plane)/(far_plane-near_plane),zb=-2.0*far_plane*near_plane/(far_plane-near_plane);"
    "gl_Position=vec4(right*u_projection.x,up*u_projection.y,za*z+zb,z);"
    "v_world=p;v_uv=a_uv;v_material=a_material;}\n";
}

const char *glr_common_fragment(void)
{
    return
    "uniform ivec2 u_target,u_origin;uniform vec3 u_camera;uniform vec2 u_yaw,u_pitch;\n"
    "uniform int u_sky_type,u_sky_fill,u_sky_fog,u_fog_on;uniform usampler2D u_backdrop;\n"
    "uint sky_index(ivec2 pixel){if(u_sky_type==0||u_sky_type==3)return 0u;"
    "if(u_sky_type==2&&u_fog_on!=0)return uint(u_sky_fog);"
    "vec2 p=(vec2(pixel-u_origin)+vec2(0.5))/vec2(u_target)*2.0-1.0;"
    "float sy=p.y*0.75,sx=p.x*float(u_target.x)/float(u_target.y)*0.75;"
    "vec2 ray=u_yaw*(u_pitch.x-sy*u_pitch.y)+vec2(u_yaw.y,-u_yaw.x)*sx;"
    "float elev=(u_pitch.y+sy*u_pitch.x)/length(ray);"
    "float row=127.0-160.0*(elev+(u_sky_type==4?0.375:0.0));"
    "if(row<0.0)return uint(u_sky_fill);if(row>=128.0)return 0u;"
    "float u=fract(-atan(ray.y,ray.x)/6.283185307179586+0.15625);"
    "return texelFetch(u_backdrop,ivec2(int(u*1024.0),int(row)),0).r;}\n";
}

const char *glr_world_fragment(void)
{
    return
    "in vec3 v_world;in vec2 v_uv;flat in uvec2 v_material;layout(location=0)out uint out_index;\n"
    "uniform usampler2D u_index,u_alpha,u_maps,u_previous;"
    "uniform ivec2 u_lights[32];uniform ivec3 u_curves[16];uniform ivec3 u_default_curve;"
    "uniform int u_frame,u_parity_origin;\n"
    "int shade(int d,ivec3 c){return clamp(((d>>(c.y&31))-c.x)*c.z,0,63);}\n"
    "void main(){uint mode=v_material.y;ivec2 pixel=ivec2(gl_FragCoord.xy);"
    "if((mode&32u)!=0u){out_index=sky_index(pixel);return;}"
    "ivec2 size=textureSize(u_index,0),tc;"
    "if((mode&64u)!=0u){ivec2 t=ivec2(floor(fract(v_world.xy)*128.0));"
    "int r=int((mode>>8u)&3u),off;"
    "if(r==0)off=t.x+t.y*128;else if(r==1)off=127-t.x+t.y*128;"
    "else if(r==2)off=t.y+t.x*128;else off=t.y+(127-t.x)*128;"
    "tc=ivec2(off/size.y,off%size.y);"
    "}else tc=clamp(ivec2(floor(v_uv*vec2(size))),ivec2(0),size-1);"
    "uint index=texelFetch(u_index,tc,0).r;"
    "if(((mode&1u)!=0u&&index==0u)||((mode&2u)!=0u&&texelFetch(u_alpha,tc,0).r==0u))discard;"
    "int d=int(dot(v_world.xy-u_camera.xy,u_yaw)*65536.0),row;"
    "int light=int(v_material.x);ivec2 l=u_lights[light];ivec3 curve=light==0?u_default_curve:u_curves[light<16?light:0];"
    "int parity=(pixel.x+pixel.y+u_frame+u_parity_origin)&1;"
    "if((mode&4u)!=0u){if(parity==0)discard;index=texelFetch(u_previous,pixel,0).r;row=min(shade(d,curve)+16,63);}"
    "else if((mode&8u)!=0u)row=parity!=0?73:0;"
    "else if(light==0)row=u_fog_on!=0?74+clamp((d>>14)+2,0,16):shade(d,curve);"
    "else if(l.x==0)row=l.y;else if(l.x==1)row=74+l.y;"
    "else if(l.x<=5)row=64+l.x-2;else row=shade(d,curve);"
    "out_index=texelFetch(u_maps,ivec2(int(index),row),0).r;}\n";
}

const char *glr_screen_vertex(void)
{
    return "void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0,1);}\n";
}

const char *glr_sky_fragment(void)
{
    return "layout(location=0)out uint out_index;void main(){out_index=sky_index(ivec2(gl_FragCoord.xy));}\n";
}

const char *glr_present_fragment(void)
{
    return
    "layout(location=0)out vec4 out_color;uniform usampler2D u_scene,u_overlay,u_cover,u_overlay_maps,u_sources;"
    "uniform sampler2D u_palette;uniform vec4 u_overlay_rect;uniform int u_overlay_on;\n"
    "void main(){ivec2 p=ivec2(gl_FragCoord.xy);uint index=texelFetch(u_scene,p,0).r;"
    "vec2 local=(gl_FragCoord.xy-u_overlay_rect.xy)/u_overlay_rect.zw;"
    "if(u_overlay_on!=0&&all(greaterThanEqual(local,vec2(0)))&&all(lessThan(local,vec2(1)))){"
    "ivec2 q=clamp(ivec2(floor(vec2(local.x,1.0-local.y)*vec2(640,480))),ivec2(0),ivec2(639,479));"
    "uint op=texelFetch(u_cover,q,0).r;if(op==1u)index=texelFetch(u_overlay,q,0).r;"
    "else if(op>=2u){uint source=texelFetch(u_sources,q,0).r;ivec2 s=ivec2(int(source%640u),int(source/640u));"
    "vec2 delta=vec2(s.x-q.x,q.y-s.y)*u_overlay_rect.z/640.0;"
    "ivec2 at=clamp(ivec2(gl_FragCoord.xy+delta),ivec2(0),textureSize(u_scene,0)-1);"
    "index=texelFetch(u_overlay_maps,ivec2(int(texelFetch(u_scene,at,0).r),int(op)-2),0).r;}}"
    "out_color=texelFetch(u_palette,ivec2(int(index),0),0);}\n";
}
