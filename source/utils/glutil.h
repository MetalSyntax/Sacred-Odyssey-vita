/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2021      Rinnegatamante
 * Copyright (C) 2022-2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  glutil.h
 * @brief OpenGL API initializer, related functions.
 */

#ifndef SOLOADER_GLUTIL_H
#define SOLOADER_GLUTIL_H

#include <vitaGL.h>

#ifdef __cplusplus
extern "C" {
#endif

void gl_init();

void gl_preload();

void gl_swap();

// Sin FBO propio de este port: los binds del engine pasan directo a vitaGL
// (ver glutil.c). Esto solo observa/loguea los FBOs propios del engine
// (shadow maps/render-to-texture, != 0), que siguen andando igual.
void glBindFramebuffer_soloader(GLenum target, GLuint framebuffer);

void glCompileShader_soloader(GLuint shader);

void glLinkProgram_soloader(GLuint program);

// Tracks shader->program attachment so the MULTITEXTURED+SKINNED family
// (character/horse materials, see glShaderSource_soloader's watch-flagging)
// can be identified by program id downstream, at glUniform*/glGetUniform-
// Location time -- otherwise indistinguishable from any other program once
// linked. Pure pass-through to the real glAttachShader plus bookkeeping.
void glAttachShader_soloader(GLuint program, GLuint shader);

void glShaderSource_soloader(GLuint shader, GLsizei count,
                             const GLchar **string, const GLint *_length);

// Upload-path observers (diagnostic only -- pure logging, then call through to
// vitaGL unchanged; no GL state is consumed or modified). Rate-limited inside.
void glViewport_soloader(GLint x, GLint y, GLsizei width, GLsizei height);
void glScissor_soloader(GLint x, GLint y, GLsizei width, GLsizei height);
// Bind-layer telemetry (diagnostic for black skinned meshes with correct
// geometry: world renders fine, horse/character are pure silhouettes). Pure
// logging + call-through, no state modified -- except glUseProgram, which
// only records the current program in a static for correlating the other
// two. Rate-limited inside.
GLint glGetUniformLocation_soloader(GLuint program, const GLchar *name);
void glUniform4fv_soloader(GLint location, GLsizei count, const GLfloat *value);
void glUniform1i_soloader(GLint location, GLint v0);
void glUseProgram_soloader(GLuint program);
void glTexImage2D_soloader(GLenum target, GLint level, GLint internalformat,
                           GLsizei width, GLsizei height, GLint border,
                           GLenum format, GLenum type, const GLvoid *pixels);
void glCompressedTexImage2D_soloader(GLenum target, GLint level, GLenum internalformat,
                                     GLsizei width, GLsizei height, GLint border,
                                     GLsizei imageSize, const GLvoid *data);
void glTexSubImage2D_soloader(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                              GLsizei width, GLsizei height, GLenum format,
                              GLenum type, const GLvoid *pixels);

#ifdef __cplusplus
};
#endif

#endif // SOLOADER_GLUTIL_H
