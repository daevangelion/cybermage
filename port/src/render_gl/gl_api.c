#include "internal.h"
#include <string.h>

int glr_api_load(cm_gl_api *api)
{
#define LOAD(result,name,args) do { \
    void *p=platform_gl_proc("gl" #name); \
    if (!p || sizeof p!=sizeof api->name) return cm_fail("GL loader",0,"missing gl" #name); \
    memcpy(&api->name,&p,sizeof p); \
} while (0);
    CM_GL_FUNCTIONS(LOAD)
#undef LOAD
    return 0;
}

static GLuint compile(cm_gl_api *gl,int gles,GLenum kind,const char *body)
{
    const char *source[]={gles ? "#version 300 es\n":"#version 330 core\n",
        "#ifdef GL_ES\nprecision highp float;\nprecision highp int;\nprecision highp usampler2D;\n#endif\n",
        kind==GL_FRAGMENT_SHADER ? glr_common_fragment():"",body};
    GLuint shader=gl->CreateShader(kind);
    GLint ok=0;
    gl->ShaderSource(shader,4,source,NULL);gl->CompileShader(shader);gl->GetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if (!ok) {
        char log[2048];log[0]=0;gl->GetShaderInfoLog(shader,sizeof log,NULL,log);log[sizeof log-1]=0;
        cm_fail("GL shader",kind,log);gl->DeleteShader(shader);return 0;
    }
    return shader;
}

GLuint glr_program(cm_gl_api *gl,int gles,const char *vertex,const char *fragment)
{
    GLuint v=compile(gl,gles,GL_VERTEX_SHADER,vertex),f,program;
    GLint ok=0;
    if (!v) return 0;
    f=compile(gl,gles,GL_FRAGMENT_SHADER,fragment);
    if (!f) {gl->DeleteShader(v);return 0;}
    program=gl->CreateProgram();gl->AttachShader(program,v);gl->AttachShader(program,f);gl->LinkProgram(program);
    gl->DeleteShader(v);gl->DeleteShader(f);gl->GetProgramiv(program,GL_LINK_STATUS,&ok);
    if (!ok) {
        char log[2048];log[0]=0;gl->GetProgramInfoLog(program,sizeof log,NULL,log);log[sizeof log-1]=0;
        cm_fail("GL program",0,log);gl->DeleteProgram(program);return 0;
    }
    return program;
}
