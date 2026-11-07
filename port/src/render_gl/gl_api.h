#ifndef CM_GL_API_H
#define CM_GL_API_H
#ifdef __ANDROID__
#include <GLES3/gl3.h>
#define CM_GL_CALL GL_APIENTRY
#else
#include <GL/glcorearb.h>
#define CM_GL_CALL APIENTRY
#endif

#define CM_GL_FUNCTIONS(X) \
X(const GLubyte *,GetString,(GLenum)) \
X(void,GetIntegerv,(GLenum,GLint *)) \
X(GLenum,GetError,(void)) \
X(void,GenTextures,(GLsizei,GLuint *)) \
X(void,DeleteTextures,(GLsizei,const GLuint *)) \
X(void,BindTexture,(GLenum,GLuint)) \
X(void,TexImage2D,(GLenum,GLint,GLint,GLsizei,GLsizei,GLint,GLenum,GLenum,const void *)) \
X(void,TexSubImage2D,(GLenum,GLint,GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,const void *)) \
X(void,TexParameteri,(GLenum,GLenum,GLint)) \
X(void,ActiveTexture,(GLenum)) \
X(void,PixelStorei,(GLenum,GLint)) \
X(GLuint,CreateShader,(GLenum)) \
X(void,ShaderSource,(GLuint,GLsizei,const GLchar *const *,const GLint *)) \
X(void,CompileShader,(GLuint)) \
X(void,GetShaderiv,(GLuint,GLenum,GLint *)) \
X(void,GetShaderInfoLog,(GLuint,GLsizei,GLsizei *,GLchar *)) \
X(void,DeleteShader,(GLuint)) \
X(GLuint,CreateProgram,(void)) \
X(void,AttachShader,(GLuint,GLuint)) \
X(void,LinkProgram,(GLuint)) \
X(void,GetProgramiv,(GLuint,GLenum,GLint *)) \
X(void,GetProgramInfoLog,(GLuint,GLsizei,GLsizei *,GLchar *)) \
X(void,DeleteProgram,(GLuint)) \
X(void,UseProgram,(GLuint)) \
X(GLint,GetUniformLocation,(GLuint,const GLchar *)) \
X(void,Uniform1i,(GLint,GLint)) \
X(void,Uniform1f,(GLint,GLfloat)) \
X(void,Uniform2i,(GLint,GLint,GLint)) \
X(void,Uniform2f,(GLint,GLfloat,GLfloat)) \
X(void,Uniform2iv,(GLint,GLsizei,const GLint *)) \
X(void,Uniform3f,(GLint,GLfloat,GLfloat,GLfloat)) \
X(void,Uniform4iv,(GLint,GLsizei,const GLint *)) \
X(void,Uniform3iv,(GLint,GLsizei,const GLint *)) \
X(void,GenBuffers,(GLsizei,GLuint *)) \
X(void,BindBuffer,(GLenum,GLuint)) \
X(void,BufferData,(GLenum,GLsizeiptr,const void *,GLenum)) \
X(void,BufferSubData,(GLenum,GLintptr,GLsizeiptr,const void *)) \
X(void,DeleteBuffers,(GLsizei,const GLuint *)) \
X(void,GenVertexArrays,(GLsizei,GLuint *)) \
X(void,BindVertexArray,(GLuint)) \
X(void,DeleteVertexArrays,(GLsizei,const GLuint *)) \
X(void,EnableVertexAttribArray,(GLuint)) \
X(void,VertexAttribPointer,(GLuint,GLint,GLenum,GLboolean,GLsizei,const void *)) \
X(void,VertexAttribIPointer,(GLuint,GLint,GLenum,GLsizei,const void *)) \
X(void,GenFramebuffers,(GLsizei,GLuint *)) \
X(void,BindFramebuffer,(GLenum,GLuint)) \
X(void,FramebufferTexture2D,(GLenum,GLenum,GLenum,GLuint,GLint)) \
X(GLenum,CheckFramebufferStatus,(GLenum)) \
X(void,DeleteFramebuffers,(GLsizei,const GLuint *)) \
X(void,GenRenderbuffers,(GLsizei,GLuint *)) \
X(void,BindRenderbuffer,(GLenum,GLuint)) \
X(void,RenderbufferStorage,(GLenum,GLenum,GLsizei,GLsizei)) \
X(void,FramebufferRenderbuffer,(GLenum,GLenum,GLenum,GLuint)) \
X(void,DeleteRenderbuffers,(GLsizei,const GLuint *)) \
X(void,Viewport,(GLint,GLint,GLsizei,GLsizei)) \
X(void,Scissor,(GLint,GLint,GLsizei,GLsizei)) \
X(void,Enable,(GLenum)) \
X(void,Disable,(GLenum)) \
X(void,ClearBufferuiv,(GLenum,GLint,const GLuint *)) \
X(void,ClearBufferfv,(GLenum,GLint,const GLfloat *)) \
X(void,DrawArrays,(GLenum,GLint,GLsizei)) \
X(void,DrawElements,(GLenum,GLsizei,GLenum,const void *)) \
X(void,ReadPixels,(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void *)) \
X(void,ReadBuffer,(GLenum)) \
X(void,DepthMask,(GLboolean)) \
X(void,DepthFunc,(GLenum)) \
X(void,PolygonOffset,(GLfloat,GLfloat)) \
X(void,BlitFramebuffer,(GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLbitfield,GLenum))

typedef struct {
#define CM_GL_MEMBER(result,name,args) result (CM_GL_CALL *name) args;
CM_GL_FUNCTIONS(CM_GL_MEMBER)
#undef CM_GL_MEMBER
} cm_gl_api;
int glr_api_load(cm_gl_api *api);
#endif
