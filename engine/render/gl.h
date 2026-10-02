// The OpenGL 3.3 core functions the renderer uses, loaded through SDL.
#pragma once

#include <SDL3/SDL_opengl.h>

#define CC_GL_FUNCTIONS(X)                                                              \
    X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)                                      \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)                                      \
    X(PFNGLGENBUFFERSPROC, glGenBuffers)                                                \
    X(PFNGLBINDBUFFERPROC, glBindBuffer)                                                \
    X(PFNGLBUFFERDATAPROC, glBufferData)                                                \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer)                              \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray)                      \
    X(PFNGLCREATESHADERPROC, glCreateShader)                                            \
    X(PFNGLSHADERSOURCEPROC, glShaderSource)                                            \
    X(PFNGLCOMPILESHADERPROC, glCompileShader)                                          \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv)                                              \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                                    \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                                          \
    X(PFNGLATTACHSHADERPROC, glAttachShader)                                            \
    X(PFNGLBINDATTRIBLOCATIONPROC, glBindAttribLocation)                                \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram)                                              \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv)                                            \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)                                  \
    X(PFNGLUSEPROGRAMPROC, glUseProgram)                                                \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)                                \
    X(PFNGLUNIFORM1IPROC, glUniform1i)                                                  \
    X(PFNGLUNIFORM1FPROC, glUniform1f)                                                  \
    X(PFNGLUNIFORM2FPROC, glUniform2f)                                                  \
    X(PFNGLUNIFORM3FPROC, glUniform3f)                                                  \
    X(PFNGLUNIFORM4FPROC, glUniform4f)                                                  \
    X(PFNGLACTIVETEXTUREPROC, glActiveTexture)                                          \
    X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate)                                  \
    X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers)                                      \
    X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer)                                      \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D)                            \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus)                        \
    X(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers)                                    \
    X(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer)                                    \
    X(PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC, glRenderbufferStorageMultisample)        \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer)                      \
    X(PFNGLBLITFRAMEBUFFERPROC, glBlitFramebuffer)                                      \
    X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers)                                \
    X(PFNGLDELETERENDERBUFFERSPROC, glDeleteRenderbuffers)                              \
    X(PFNGLGENERATEMIPMAPPROC, glGenerateMipmap)

namespace gl {

#define CC_GL_DECLARE(type, name) extern type name;
CC_GL_FUNCTIONS(CC_GL_DECLARE)
#undef CC_GL_DECLARE

// Loads the functions above; call with a current context. Returns false if
// any is missing.
bool load();

}  // namespace gl
