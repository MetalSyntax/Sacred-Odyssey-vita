/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2021      Rinnegatamante
 * Copyright (C) 2022-2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "utils/glutil.h"

#include "utils/utils.h"
#include "utils/dialog.h"
#include "utils/logger.h"

#include <so_util/so_util.h>

extern so_module so_mod;

#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/io/stat.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>

// Definido abajo (sección upload observers); se usa en los wrappers de
// viewport/FBO de arriba.
static uintptr_t gl_caller_off(void);
// Programa actual (lo mantiene glUseProgram_soloader; el blit lo restaura).
static GLuint cur_program;

// Helpers for our handling of shaders
GLboolean skip_next_compile = GL_FALSE;
char next_shader_fname[256];
void load_shader(GLuint shader, const char * string, size_t length);

// Shader objects whose source was replaced by our minimal VP/FP substitute
// (missing data/3d/effects/*.glsl -- see glShaderSource_soloader below). Used
// by glLinkProgram_soloader to recognize, once per program, that a linked
// program is running our substitute rather than the game's real effect --
// so we can log what the engine actually left in DiffuseColor/TextureMatrix0
// (both reported "unbound"/"invalid bind symbol" by the game's own material
// binder in every session log so far) instead of guessing their runtime
// value. Small fixed table: far fewer distinct SHADER OBJECTS use the
// substitute than materials that reference them.
#define MAX_SUBSTITUTED_SHADERS 32
static GLuint substituted_shaders[MAX_SUBSTITUTED_SHADERS];
static int substituted_shader_count;

static void mark_shader_substituted(GLuint shader) {
    for (int i = 0; i < substituted_shader_count; i++) {
        if (substituted_shaders[i] == shader) return;
    }
    if (substituted_shader_count < MAX_SUBSTITUTED_SHADERS) {
        substituted_shaders[substituted_shader_count++] = shader;
    }
}

static int shader_is_substituted(GLuint shader) {
    for (int i = 0; i < substituted_shader_count; i++) {
        if (substituted_shaders[i] == shader) return 1;
    }
    return 0;
}

// 2026-09-18: the generic gl_bind/gl_u4v/gl_u1i telemetry below (uloc_logged/
// u4v_logged/u1i_logged) caps out (60/25/40 calls) well before the engine
// ever reaches the MainCharacter/horse materials in a real session -- their
// MULTITEXTURED+TEXTURESKINNED programs (confirmed #34-40 in
// log_20260918_000157.txt, right after the Sampler1->unit-7 fix from the
// prior session, which DID apply but did NOT fix the reported still-black
// character/horse) never got a single [gl_bind]/[gl_u4v]/[gl_u1i] line, so
// there is no evidence yet of what THIS specific material actually binds.
// Flag shaders whose source contains both "MULTITEXTURED" and "SKINNED"
// (matches SKINNED/QUATSKINNED/TEXTURESKINNED) in glShaderSource_soloader,
// propagate shader->program via glAttachShader_soloader, and let the
// telemetry functions below log unconditionally (bypassing their caps) for
// just these specific watched programs -- targeted, not a blanket raise of
// the caps (which would spam the log for the rest of a real session).
// 2026-09-18 (tercera vuelta): con el cap en 16, log_20260918_152457.txt
// (build recien desplegado con la telemetria de [gl_draw]) confirmo el cap
// alcanzado justo en el shader #78 (16avo watch_shader() exitoso) -- shaders
// #79/#80, que son EXACTAMENTE los del programa 40 (TEXTURESKINNED, el
// material `KnightsOdyssey_hand_diffuse` que la sesion anterior identifico
// con "Unused parameter: Map__3__..."), imprimieron el log "flagged" (el
// l_info corria incondicional, sin chequear si watch_shader() realmente
// guardo el slot) pero NUNCA entraron a la tabla -- watch_shader() los
// descartaba en silencio con el array lleno. glAttachShader_soloader nunca
// vio shader_is_watched(79/80)==true, asi que el programa 40 nunca se marco
// watched, y [gl_draw] (que solo logea para watch_program_is_watched()) no
// disparo NI UNA VEZ para el material que mas hace falta ver. 16 ya no
// alcanza para una mision real (18 shaders distintos calificaron en esta
// sola sesion, con margen de sobra para que una escena mas larga pida mas)
// -- subido a 64 (256 bytes extra, nada) y watch_shader()/watch_program()
// ahora devuelven si el slot se guardo de verdad, para que el log de arriba
// dejen de mentir si el array se llena otra vez.
#define MAX_WATCHED 64
static GLuint s_watched_shaders[MAX_WATCHED];
static int s_watched_shader_count;
static GLuint s_watched_programs[MAX_WATCHED];
static int s_watched_program_count;

static int watch_shader(GLuint shader) {
    for (int i = 0; i < s_watched_shader_count; i++) if (s_watched_shaders[i] == shader) return 1;
    if (s_watched_shader_count < MAX_WATCHED) {
        s_watched_shaders[s_watched_shader_count++] = shader;
        return 1;
    }
    return 0;
}
static int shader_is_watched(GLuint shader) {
    for (int i = 0; i < s_watched_shader_count; i++) if (s_watched_shaders[i] == shader) return 1;
    return 0;
}
static int watch_program(GLuint program) {
    for (int i = 0; i < s_watched_program_count; i++) if (s_watched_programs[i] == program) return 1;
    if (s_watched_program_count < MAX_WATCHED) {
        s_watched_programs[s_watched_program_count++] = program;
        return 1;
    }
    return 0;
}
static int program_is_watched(GLuint program) {
    for (int i = 0; i < s_watched_program_count; i++) if (s_watched_programs[i] == program) return 1;
    return 0;
}

// 2026-09-19 (usuario): "bajones de FPS al luchar con el jefe cuando se cae
// por una explosion al igual que al pasar a otros stage". La telemetria
// [gl_u4v]/[gl_u1i]/[gl_u1f] de la sesion anterior (commit "diag: telemetria
// dirigida a programas MULTITEXTURED+SKINNED") logea SIN LIMITE cada llamada
// a glUniform* para CUALQUIER programa watched (personaje/caballo/jefe: los
// 13 materiales MULTITEXTURED del dataset) durante el resto de la sesion --
// cada l_info() que llega a los sinks de archivo/UDP hace un sceIoOpen+Write+
// Close y un sendto bloqueantes (ver logger.c). Un pelea contra el jefe
// dibuja al jugador+montura+jefe cada frame, cada uno con varios uniforms
// (DiffuseColor/envmapIntensity/Sampler0/Sampler1/matrices de hueso) -- eso
// es de a decenas de estas llamadas SIN throttle, 60 veces por segundo,
// mientras dura el combate. log_watched_draw_state() (mas abajo) ya limita a
// 3 logs por programa; estas 5 wrappers de glUniform* no tenian ningun cap
// equivalente. Presupuesto generoso (30 por programa) alcanza para la misma
// evidencia de diagnostico que ya se uso (confirmar Sampler1/envmapIntensity
// en los primeros draws de cada material) sin pagar el costo el resto del
// combate/mision.
#define MAX_WATCHED_UNIFORM_LOGS 30
static unsigned s_watched_uniform_log_count[MAX_WATCHED];
static int watched_uniform_log_budget(GLuint program) {
    for (int i = 0; i < s_watched_program_count; i++) {
        if (s_watched_programs[i] == program) {
            if (s_watched_uniform_log_count[i] >= MAX_WATCHED_UNIFORM_LOGS) return 0;
            s_watched_uniform_log_count[i]++;
            return 1;
        }
    }
    return 0;
}

void glAttachShader_soloader(GLuint program, GLuint shader) {
    if (shader_is_watched(shader)) {
        if (watch_program(program)) {
            l_info("[gl_watch] program %u <- watched shader %u (MULTITEXTURED+SKINNED)", program, shader);
        } else {
            l_warn("[gl_watch] program %u <- watched shader %u, but MAX_WATCHED programs already full!", program, shader);
        }
    }
    glAttachShader(program, shader);
}

static void cleanup_corrupt_gxp(void) {
    SceUID dfd = sceIoDopen(DATA_PATH "gxp");
    if (dfd >= 0) {
        SceIoDirent dir;
        while (sceIoDread(dfd, &dir) > 0) {
            char path[256];
            snprintf(path, sizeof(path), DATA_PATH "gxp/%s", dir.d_name);
            sceIoRemove(path);
        }
        sceIoDclose(dfd);
        sceIoRmdir(DATA_PATH "gxp");
        l_info("Cleaned up corrupted gxp shader cache directory.");
    }
}

void gl_preload() {
    if (!file_exists("ur0:/data/libshacccg.suprx")
        && !file_exists("ur0:/data/external/libshacccg.suprx")) {
        fatal_error("Error: libshacccg.suprx is not installed. "
                    "Google \"ShaRKBR33D\" for quick installation.");
    }

#ifdef USE_GLSL_SHADERS
    vglSetSemanticBindingMode(VGL_MODE_POSTPONED);
#endif

    cleanup_corrupt_gxp();
}

// Pantalla física real de la Vita. El engine vive en SCREEN_W/H de main.c
// (800x480, resolución de diseño original de teléfono -- confirmado en
// hardware que reportarle 960x544 directo rompe el layout de menús/UI) y
// dibuja DIRECTO sobre el framebuffer real (framebuffer 0, sin ningún FBO
// intermedio): el único ajuste es que glViewport_soloader/glScissor_soloader
// reescalan cada rect que el engine pide en su espacio 800x480 a coordenadas
// reales de 960x544 antes de pasarlo a la GPU (ver rescale_viewport_scissor
// más abajo). No hay downsample, blit ni segundo framebuffer -- es
// exclusivamente un cambio de escala en los argumentos de viewport/scissor.
#define REAL_SCREEN_W 960
#define REAL_SCREEN_H 544

void gl_init() {
    static int vgl_initialized = 0;
    if (vgl_initialized) return;
    vgl_initialized = 1;

    file_mkpath(DATA_PATH "cache/shaders/", 0777);
    vglSetShaderCachePath(DATA_PATH "cache/shaders");

    vglInitExtended(0, 960, 544, 6 * 1024 * 1024, SCE_GXM_MULTISAMPLE_4X);
}

void glBindFramebuffer_soloader(GLenum target, GLuint framebuffer) {
    // Sin FBO propio de este port: todo bind pasa directo a vitaGL. Los FBOs
    // del engine (shadow maps, render-to-texture, != 0) son enteramente suyos
    // y siguen andando igual; esto solo observa para saber si existen.
    if (framebuffer != 0) {
        static unsigned own_logged;
        if (own_logged < 5) {
            own_logged++;
            l_info("[gl_fbo] engine using own FBO %u (target=0x%x)", framebuffer, target);
        }
    }
    glBindFramebuffer(target, framebuffer);
}

void gl_swap() {
    vglSwapBuffers(GL_FALSE);
}

void glShaderSource_soloader(GLuint shader, GLsizei count,
                             const GLchar **string, const GLint *_length) {
#ifdef DEBUG_OPENGL
    sceClibPrintf("[gl_dbg] glShaderSource<%p>(shader: %i, count: %i, string: %p, length: %p)\n", __builtin_return_address(0), shader, count, string, _length);
#endif
    if (!string) {
        l_error("<%p> Shader source string is NULL, count: %i",
                   __builtin_return_address(0), count);
        skip_next_compile = GL_TRUE;
        return;
    } else if (!*string) {
        l_error("<%p> Shader source *string is NULL, count: %i",
                   __builtin_return_address(0), count);
        skip_next_compile = GL_TRUE;
        return;
    }

    size_t total_length = 0;

    for (int i = 0; i < count; ++i) {
        if (!_length) {
            total_length += strlen(string[i]);
        } else {
            total_length += _length[i];
        }
    }

    char * str = malloc(total_length+1);
    size_t l = 0;

    for (int i = 0; i < count; ++i) {
        if (!_length) {
            memcpy(str + l, string[i], strlen(string[i]));
            l += strlen(string[i]);
        } else {
            memcpy(str + l, string[i], _length[i]);
            l += _length[i];
        }
    }
    str[total_length] = '\0';

    if (strstr(str, "MULTITEXTURED") && strstr(str, "SKINNED")) {
        if (watch_shader(shader)) {
            l_info("[gl_watch] shader #%u flagged (MULTITEXTURED+SKINNED technique)", shader);
        } else {
            l_warn("[gl_watch] shader #%u matched, but MAX_WATCHED shaders already full!", shader);
        }
    }

    // Empty shader sources (missing data/3d/effects/*.glsl + missing .obfs
    // fallback, served as 0 bytes via the FileStream::Tell/Seek NULL-FILE
    // patches) MUST NOT reach vitaGL as-is: its GLSL translator
    // (glsl_handle_globals, glsl_utils.c) does strstr(src,"{") -> NULL, then
    // strstr(NULL,"}") -> strchr(NULL) -> Data abort inside glLinkProgram
    // (dump sacredodyssey-psp2core-1789013078-...: PC in strchr via
    // glsl_handle_globals <- glsl_translator_process). Substitute a minimal
    // VALID shader so link succeeds and the engine keeps going; the material
    // renders degraded instead of killing the game. Naming follows two
    // confirmed sources, not guesses: (1) this game's own log
    // (log_20260909_224913.txt) proves the engine binds WorldViewProjection-
    // Matrix, DiffuseColor, Sampler0 and TextureMatrix0 by name ("invalid bind
    // symbol" = lookup attempted); (2) Dungeon-Hunter-2 (same Glitch engine
    // family) ships real dumped shaders using Position/Color0/TexCoord0
    // attributes and vTexCoord0/vColor0 varyings (glsl_dump/*.glsl). The
    // Position+Vertex sum covers the one uncertain name: this .so's own
    // embedded pink fallback uses `Vertex` while DH2 uses `Position` --
    // unbound attributes read (0,0,0,1), so xyz adds up to the real position
    // whichever convention this build uses (w forced to 1.0).
    {
        int only_ws = 1;
        for (size_t k = 0; k < total_length; k++) {
            if (str[k] != ' ' && str[k] != '\t' && str[k] != '\r' && str[k] != '\n') {
                only_ws = 0;
                break;
            }
        }
        // Diagnostic: what is the engine actually feeding vitaGL? (length +
        // head; full sources would spam). This answered the "empty or not?"
        // question for the 1789017897 dump (new eboot running, yet no
        // substitute fired): if len>0 here, the source is braceless-but-
        // nonempty and the trigger below (no '{') still catches it.
        {
            char head[81];
            size_t hl = total_length < 80 ? total_length : 80;
            memcpy(head, str, hl);
            head[hl] = '\0';
            for (size_t k = 0; k < hl; k++)
                if (head[k] == '\n' || head[k] == '\r') head[k] = '|';
            l_debug("glShaderSource(#%u): len=%u brace=%d head='%s'",
                    (unsigned)shader, (unsigned)total_length,
                    strchr(str, '{') != NULL, head);
        }
        // Any valid GLSL shader has at least one function body ('{'); without
        // it vitaGL's translator dereferences NULL guaranteed (see above), so
        // a braceless source can never link anyway -- substitute it too.
        if (only_ws || strchr(str, '{') == NULL) {
            static const char empty_vp_src[] =
                "attribute highp vec4 Position;\n"
                "attribute lowp vec4 Color0;\n"
                "attribute mediump vec2 TexCoord0;\n"
                "varying mediump vec2 vTexCoord0;\n"
                "varying lowp vec4 vColor0;\n"
                "uniform highp mat4 WorldViewProjectionMatrix;\n"
                "uniform mediump mat4 TextureMatrix0;\n"
                "void main(void)\n"
                "{\n"
                "    gl_Position = WorldViewProjectionMatrix * Position;\n"
                "    vColor0 = Color0;\n"
                "    vTexCoord0 = (TextureMatrix0 * vec4(TexCoord0, 0.0, 1.0)).xy;\n"
                "}\n";
            static const char empty_fp_src[] =
                "precision mediump float;\n"
                "uniform sampler2D Sampler0;\n"
                "uniform sampler2D texture;\n"
                "uniform vec4 DiffuseColor;\n"
                "varying mediump vec2 vTexCoord0;\n"
                "varying lowp vec4 vColor0;\n"
                "void main(void)\n"
                "{\n"
                "    vec4 col0 = texture2D(Sampler0, vTexCoord0);\n"
                "    vec4 col1 = texture2D(texture, vTexCoord0);\n"
                "    vec4 color = (col0.a > 0.01) ? col0 : col1;\n"
                // Guarded like the embedded UnlitTextured* shaders: an unbound
                // Color0 (reads 0) or a zero DiffuseColor must be neutral (white
                // / ignored), never a black or pink wash over the real texture
                // (loading_splash.kot). The previous unguarded
                // `color += DiffuseColor; color *= vColor0;` turned any material
                // reaching this last-resort path into a flat tint.
                "    vec4 vc = (vColor0.a > 0.01 && (vColor0.r > 0.01 || vColor0.g > 0.01 || vColor0.b > 0.01)) ? vColor0 : vec4(1.0);\n"
                "    color *= vc;\n"
                "    if (DiffuseColor.a > 0.01 && (DiffuseColor.r > 0.01 || DiffuseColor.g > 0.01 || DiffuseColor.b > 0.01)) {\n"
                "        color.rgb *= DiffuseColor.rgb;\n"
                "    }\n"
                "    gl_FragColor = color;\n"
                "}\n";
            GLint stype = 0;
            glGetShaderiv(shader, GL_SHADER_TYPE, &stype);
            const char *sub = (stype == GL_FRAGMENT_SHADER) ? empty_fp_src : empty_vp_src;
            l_warn("Empty shader source for %s #%u -- substituting minimal %s "
                   "(missing .glsl, see port_progress.md)",
                   (stype == GL_FRAGMENT_SHADER) ? "fragment" :
                   (stype == GL_VERTEX_SHADER) ? "vertex" : "unknown-type",
                   (unsigned)shader,
                   (stype == GL_FRAGMENT_SHADER) ? "FP" : "VP");
            mark_shader_substituted(shader);
            load_shader(shader, sub, strlen(sub));
            free(str);
            return;
        }
    }

    load_shader(shader, str, total_length);

    free(str);
}

void glCompileShader_soloader(GLuint shader) {
#ifdef DEBUG_OPENGL
    sceClibPrintf("[gl_dbg] glCompileShader<%p>(shader: %i)\n", __builtin_return_address(0), shader);
#endif

#ifndef USE_GXP_SHADERS
    if (!skip_next_compile) {
        l_debug("glCompileShader(%i): compiling...", shader);
        glCompileShader(shader);
        l_debug("glCompileShader(%i): done", shader);
    }
    skip_next_compile = GL_FALSE;
#endif
}

// Fix for the MainMenu visual bug reported 2026-09-10 (blank/white
// background behind the logo, no menu animation, "zoom raro"): the whole
// mainmenu2 3D scene's materials (UnlitVertexColor/UnlitOneTextureAnd-
// VertexColor/UnlitTextured/...) run on the glShaderSource_soloader
// substitute above because their real .glsl is missing from every known
// dataset (confirmed, see port_progress.md). The engine logs "unbound
// parameter TextureMatrix0" for every one of these materials, every run --
// per GLSL's zero-initialized-uniform rule, an unbound uniform sits at 0 for
// the program's whole life, so TextureMatrix0 is an all-zero 4x4 matrix.
// That is NOT "no transform" (identity) -- multiplying any TexCoord0 by a
// zero matrix always yields (0,0), i.e. every fragment of every material
// stuck on this substitute samples the SAME single texel of its texture
// (typically its top-left corner) stretched across the whole surface. That
// is exactly "zoom raro": a texture atlas or full background image reduced
// to one corner pixel blown up to fill the mesh. vitaGL has no
// glGetUniformfv/iv to read this back and prove the exact stored value, but
// the zero-vs-identity distinction here isn't a guess -- there is no GLSL
// rule that would leave a genuinely unbound uniform as anything BUT 0, and
// identity is the only value that can only help (0 guarantees the bug just
// described; identity makes the substitute behave as a correct passthrough
// whenever TexCoord0 already arrives pre-transformed, which is the common
// case for baked/pre-atlased UVs). Set it explicitly right after link so it
// can never be left at the GLSL-mandated zero.
void glLinkProgram_soloader(GLuint program) {
    // vglSetSemanticBindingMode(VGL_MODE_POSTPONED) (gl_preload()) means the
    // real GLSL->GXM translation and ShaccCg/vitashark compilation happen
    // HERE, not in glCompileShader -- this is the actual heavy-lifting step
    // for a shader that's never been compiled on this console before.
    l_debug("glLinkProgram(%i): linking (real GLSL translation happens "
            "here with VGL_MODE_POSTPONED)...", program);
    glLinkProgram(program);
    l_debug("glLinkProgram(%i): done", program);

    static const char *s_texmats[] = { "TextureMatrix0", "TextureMatrix1", "TextureMatrix2" };
    static const float identity4x4[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };
    GLint prev_program = -1;
    for (size_t i = 0; i < sizeof(s_texmats) / sizeof(s_texmats[0]); i++) {
        GLint loc = glGetUniformLocation(program, s_texmats[i]);
        if (loc >= 0) {
            if (prev_program < 0) {
                glGetIntegerv(GL_CURRENT_PROGRAM, &prev_program);
                glUseProgram(program);
            }
            glUniformMatrix4fv(loc, 1, GL_FALSE, identity4x4);
            l_info("[program_init] program %u: initialized %s to identity", program, s_texmats[i]);
        }
    }
    // Force texture1/texture2/Sampler1 away from texture unit 0 by default.
    // GLSL leaves an unset sampler uniform at its default value (0), same
    // unit "texture"/Sampler0 itself uses -- if a material only ever sets
    // ONE of a shader's secondary-texture uniforms (see embedded_shaders.c:
    // UnlitMultiTexturedFP's texture1/texture2, and ProfileCOMMON_emul_FS's
    // Sampler1 for MULTITEXTURED materials, used by mc.bdae/magic_horse* --
    // confirmed black-character/black-horse root cause, 2026-09-17: Sampler1
    // defaulting to unit 0 makes `tex1 = texture2D(Sampler1, vTexCoord0)`
    // read the SAME diffuse image `color` already holds, pass the ">0.03
    // signal" guard trivially since it's real image data, and square the
    // color against itself via `color *= tex1` -- a severe, wrong darkening
    // that reads as "black" for most non-pure-white textures, not the
    // "just skip it" behavior the guard is meant to provide), the other
    // one would silently sample that same image. Unit 7 is guaranteed to
    // exist (GLES2 requires >=8 image units) and nothing in this port ever
    // binds a real texture there, so it reads back (0,0,0,0) until/unless
    // the material's own binder overwrites it.
    static const char *s_second_tex_units[] = { "texture1", "texture2", "Sampler1" };
    for (size_t i = 0; i < sizeof(s_second_tex_units) / sizeof(s_second_tex_units[0]); i++) {
        GLint loc = glGetUniformLocation(program, s_second_tex_units[i]);
        if (loc >= 0) {
            if (prev_program < 0) {
                glGetIntegerv(GL_CURRENT_PROGRAM, &prev_program);
                glUseProgram(program);
            }
            glUniform1i(loc, 7);
            l_info("[program_init] program %u: defaulted %s to empty unit 7", program, s_second_tex_units[i]);
        }
    }

    if (prev_program >= 0) {
        glUseProgram((GLuint)prev_program);
    }
}

// --- Upload-path observers -------------------------------------------------
// Which texture-upload entry point the engine uses per .kot iteration (and
// with what format/dims) discriminates the failure stage without touching
// behavior: glCompressedTexImage2D with an ATC enum + our UNCOMPRESSED served
// bytes would mean vitaGL transcodes garbage (but succeeds); glTexImage2D
// RGBA 128x128 would mean the file parses fine and the failure is downstream
// (no negative cache); NO upload call at all would mean the engine rejects
// the served file during parse (size/FourCC/mips) before ever reaching GL.
// Caller .so offset identifies the uploader for objdump, same as [fopen_storm].
static unsigned tex_upload_count;
static unsigned ctex_upload_count;
static unsigned sub_upload_count;

static uintptr_t gl_caller_off(void) {
    uintptr_t ra = (uintptr_t)__builtin_return_address(0);
    return (so_mod.text_base && ra > so_mod.text_base) ? ra - so_mod.text_base : ra;
}

// --- Bind-layer telemetry ----------------------------------------------------
// Black-horse triage (2026-09-11, screenshots hf/224923 + log 234654):
// skinned meshes have CORRECT geometry but pure-black fragments while the
// static world is fine, with the same FS template. Every texture fopen
// succeeds, so the failure is in GL bind state per material: which sampler
// goes to which unit, and which vec4 (DiffuseColor/envmapIntensity) values
// get uploaded. These wrappers only observe.
static unsigned uloc_logged;
static unsigned u4v_logged;
static unsigned u1i_logged;

void glUseProgram_soloader(GLuint program) {
    cur_program = program;
    glUseProgram(program);
}

static int interesting_uniform(const char *name) {
    if (!name) return 0;
    // 2026-09-18: el binder nombra sus params por material
    // ("Map__3__KnightsOdyssey_hand_diffuse.tga_-sampler", minusculas):
    // el match anterior ("Sampler" con mayuscula) los dejaba pasar sin
    // loguear ni una vez. Se matchea tambien "sampler" y "Map__".
    return strstr(name, "Sampler") != NULL || strstr(name, "sampler") != NULL ||
            strstr(name, "Map__") != NULL || strstr(name, "Diffuse") != NULL ||
            strstr(name, "envmap") != NULL || strstr(name, "TextureMatrix") != NULL ||
            strstr(name, "Bone") != NULL || strstr(name, "Weight") != NULL ||
            strstr(name, "texture") != NULL;
}

GLint glGetUniformLocation_soloader(GLuint program, const GLchar *name) {
    GLint loc = glGetUniformLocation(program, name);
    if (name && interesting_uniform(name) && (uloc_logged < 60 || program_is_watched(program))) {
        if (!program_is_watched(program)) uloc_logged++;
        l_info("[gl_bind] prog=%u %s -> %d", program, name, loc);
    }
    return loc;
}

// vitaGL doesn't implement glGetUniformfv/glGetUniformiv (no uniform
// readback API at all -- confirmed absent from vitaGL.h), so the draw-time
// telemetry below can't ask the GPU "what value does this uniform hold right
// now". Instead, remember which (program, location) pairs a glUniform4fv/1i
// call has EVER touched for a watched program -- small fixed table, these
// programs only have a handful of distinct uniform locations.
#define MAX_SEEN_UNIFORMS 32
static GLint s_seen_uniform_prog[MAX_SEEN_UNIFORMS];
static GLint s_seen_uniform_loc[MAX_SEEN_UNIFORMS];
static int s_seen_uniform_count;

static void mark_uniform_set(GLuint program, GLint location) {
    for (int i = 0; i < s_seen_uniform_count; i++) {
        if (s_seen_uniform_prog[i] == (GLint)program && s_seen_uniform_loc[i] == location) return;
    }
    if (s_seen_uniform_count < MAX_SEEN_UNIFORMS) {
        s_seen_uniform_prog[s_seen_uniform_count] = (GLint)program;
        s_seen_uniform_loc[s_seen_uniform_count] = location;
        s_seen_uniform_count++;
    }
}

static int uniform_was_ever_set(GLuint program, GLint location) {
    if (location < 0) return -1; // uniform doesn't exist in this program at all
    for (int i = 0; i < s_seen_uniform_count; i++) {
        if (s_seen_uniform_prog[i] == (GLint)program && s_seen_uniform_loc[i] == location) return 1;
    }
    return 0;
}

void glUniform4fv_soloader(GLint location, GLsizei count, const GLfloat *value) {
    // vec4 uploads are DiffuseColor / material colors / bone rows: log the
    // first ones per run (with current program for correlation) to catch a
    // materialeo uploading black. Watched programs (see watch_program())
    // always log, uncapped -- the generic cap exhausts long before the
    // character/horse materials link in a real session.
    if (value && count >= 1 && (u4v_logged < 25 || watched_uniform_log_budget(cur_program))) {
        if (!program_is_watched(cur_program)) u4v_logged++;
        l_info("[gl_u4v] prog=%u loc=%d count=%d v=(%.3f,%.3f,%.3f,%.3f)",
               cur_program, location, count, value[0], value[1], value[2], value[3]);
    }
    if (program_is_watched(cur_program)) mark_uniform_set(cur_program, location);
    glUniform4fv(location, count, value);
}

void glUniform1i_soloader(GLint location, GLint v0) {
    // Sampler->unit assignments (+ BoneTexture unit for TEXTURESKINNED).
    if (u1i_logged < 40 || watched_uniform_log_budget(cur_program)) {
        if (!program_is_watched(cur_program)) u1i_logged++;
        l_info("[gl_u1i] prog=%u loc=%d unit=%d", cur_program, location, v0);
    }
    if (program_is_watched(cur_program)) mark_uniform_set(cur_program, location);
    glUniform1i(location, v0);
}

// 2026-09-18: el binder del motor tambien puede usar las variantes
// vector/escalar (1iv para samplers, 1f para envmapIntensity, 4f para
// DiffuseColor) -- antes pasaban en crudo sin marcar ever_set, asi que el
// [gl_draw] reportaba "nunca seteado" aunque el bind fuera real. Mismo
// tratamiento que sus hermanas 1i/4fv: log + marca + call-through.
void glUniform1iv_soloader(GLint location, GLsizei count, const GLint *value) {
    if ((value && count >= 1 && (u1i_logged < 40 || watched_uniform_log_budget(cur_program)))) {
        if (!program_is_watched(cur_program)) u1i_logged++;
        l_info("[gl_u1i] prog=%u loc=%d unit=%d (1iv,count=%d)", cur_program, location, value[0], count);
    }
    if (program_is_watched(cur_program)) mark_uniform_set(cur_program, location);
    glUniform1iv(location, count, value);
}

void glUniform1f_soloader(GLint location, GLfloat v0) {
    // Típicamente envmapIntensity en materiales MULTITEXTURED (personaje,
    // montura): loguear el valor confirma que el aditivo nuevo recibe la
    // intensidad real del material en vez del default GL 0.0.
    if (u1i_logged < 40 || watched_uniform_log_budget(cur_program)) {
        if (!program_is_watched(cur_program)) u1i_logged++;
        l_info("[gl_u1f] prog=%u loc=%d v=%.3f", cur_program, location, v0);
    }
    if (program_is_watched(cur_program)) mark_uniform_set(cur_program, location);
    glUniform1f(location, v0);
}

void glUniform4f_soloader(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) {
    if (u4v_logged < 25 || watched_uniform_log_budget(cur_program)) {
        if (!program_is_watched(cur_program)) u4v_logged++;
        l_info("[gl_u4v] prog=%u loc=%d count=1 v=(%.3f,%.3f,%.3f,%.3f) (4f)",
               cur_program, location, v0, v1, v2, v3);
    }
    if (program_is_watched(cur_program)) mark_uniform_set(cur_program, location);
    glUniform4f(location, v0, v1, v2, v3);
}

// 2026-09-18 (segunda vuelta): [gl_u4v]/[gl_u1i] arriba nunca dispararon NI
// UNA VEZ para los programas 34-40 en TODA la sesion (grep -c "gl_u4v\]\|
// gl_u1i\]" contra log_20260918_011317.txt = 6/12 total, ninguno prog>=17) --
// pese a que el personaje/caballo SI se dibujan en pantalla ("silueta" negra
// pero con forma correcta, confirmado por el usuario). Conclusion: el binder
// de parametros del motor jamas llama glUniform4fv/1i para este material en
// absoluto (no es un problema de nombre-no-matchea como Sampler1/Map__N__ --
// eso a lo sumo explicaria UN parametro faltante, no CERO llamadas para TODO
// el material, incluido DiffuseColor que si resuelve su location por nombre
// en [gl_bind]). La telemetria pasiva (observar los SET calls) se agoto sin
// evidencia. Este bloque en cambio CONSULTA el estado real en el momento del
// draw call. vitaGL no implementa glGetUniformfv/iv (sin API de lectura de
// uniforms -- confirmado ausente de vitaGL.h, el link fallaba por simbolo
// indefinido), asi que en vez de leer el valor actual de la GPU, mark_uniform_set()
// (arriba) registra cada (program,location) que un glUniform4fv/1i real haya
// tocado; uniform_was_ever_set() consulta ese registro en el draw call --
// suficiente para responder "esto quedo en su default GL de fabrica (0) o
// alguna vez se seteo", sin necesitar leerlo de vuelta. Cubre tambien el
// texture binding real (que unidad esta activa, que textura hay bindeada en
// las primeras unidades) porque glActiveTexture/glBindTexture nunca se
// hookearon: si Sampler0 vale 0 (su default GL sin setear) pero la unidad 0
// NO tiene la textura difusa correcta bindeada en el momento del draw, el
// resultado es negro sin que ninguna de las dos mitades (uniform o bind) sea
// "el culpable" por si sola.
static GLuint s_active_texture_unit = GL_TEXTURE0;
#define MAX_TRACKED_TEX_UNITS 8
static GLuint s_bound_texture[MAX_TRACKED_TEX_UNITS];

void glActiveTexture_soloader(GLenum texture) {
    unsigned idx = texture - GL_TEXTURE0;
    if (idx < MAX_TRACKED_TEX_UNITS) s_active_texture_unit = texture;
    glActiveTexture(texture);
}

void glBindTexture_soloader(GLenum target, GLuint texture) {
    unsigned idx = s_active_texture_unit - GL_TEXTURE0;
    if (target == GL_TEXTURE_2D && idx < MAX_TRACKED_TEX_UNITS) {
        s_bound_texture[idx] = texture;
    }
    glBindTexture(target, texture);
}

#define MAX_DRAW_LOGGED_PROGRAMS 16
static GLuint s_draw_logged_program[MAX_DRAW_LOGGED_PROGRAMS];
static unsigned s_draw_logged_count[MAX_DRAW_LOGGED_PROGRAMS];
static int s_draw_logged_program_n;

static void log_watched_draw_state(const char *caller) {
    if (!program_is_watched(cur_program)) return;

    int slot = -1;
    for (int i = 0; i < s_draw_logged_program_n; i++) {
        if (s_draw_logged_program[i] == cur_program) { slot = i; break; }
    }
    if (slot < 0) {
        if (s_draw_logged_program_n >= MAX_DRAW_LOGGED_PROGRAMS) return;
        slot = s_draw_logged_program_n++;
        s_draw_logged_program[slot] = cur_program;
        s_draw_logged_count[slot] = 0;
    }
    // 3 logs por programa alcanzan para confirmar si el valor es estable
    // entre draws sin inundar el log en una mission entera.
    if (s_draw_logged_count[slot] >= 3) return;
    s_draw_logged_count[slot]++;

    GLint diffuse_loc = glGetUniformLocation(cur_program, "DiffuseColor");
    GLint sampler0_loc = glGetUniformLocation(cur_program, "Sampler0");
    // -1 = uniform doesn't exist in this program; 0 = exists but glUniform*
    // never touched it (still holds GL's zero-initialized default); 1 = some
    // glUniform4fv/1i call set it at least once (see mark_uniform_set above).
    int diffuse_set = uniform_was_ever_set(cur_program, diffuse_loc);
    int sampler0_set = uniform_was_ever_set(cur_program, sampler0_loc);

    l_info("[gl_draw] %s prog=%u DiffuseColor(loc=%d,ever_set=%d) "
           "Sampler0(loc=%d,ever_set=%d) active_unit=%u tex@unit0=%u tex@unit1=%u",
           caller, cur_program, diffuse_loc, diffuse_set,
           sampler0_loc, sampler0_set, s_active_texture_unit - GL_TEXTURE0,
           s_bound_texture[0], s_bound_texture[1]);
}

void glDrawArrays_soloader(GLenum mode, GLint first, GLsizei count) {
    log_watched_draw_state("glDrawArrays");
    glDrawArrays(mode, first, count);
}

void glDrawElements_soloader(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices) {
    log_watched_draw_state("glDrawElements");
    glDrawElements(mode, count, type, indices);
}

// --- Viewport/scissor rescale (sin FBO) -----------------------------------
// El engine reporta/dibuja en su resolución de diseño original de teléfono
// (800x480, SCREEN_W/H de main.c), pero la Vita presenta a 960x544 reales.
// Sin ningún framebuffer intermedio, la única forma de que el frame llene la
// pantalla real es reescalar cada glViewport/glScissor que el engine pide en
// su espacio 800x480 a coordenadas 960x544 antes de pasarlas a la GPU -- el
// engine sigue dibujando DIRECTO sobre el framebuffer real, más grande de lo
// que él cree.
// Guard NARROW a propósito: los viewports de FBOs propios del engine
// (shadow maps/render-to-texture, típicamente cuadrados o mucho más chicos
// que la pantalla) NO deben reescalarse -- reescalarlos con un factor
// distinto en X e Y los deformaría. Solo se remapea un rect que se parece al
// viewport principal del engine: ancho >= 700 (descarta sub-regiones
// chicas) y ancho != alto (descarta los cuadrados de shadow map/RTT).
static unsigned viewport_logged;
static unsigned scissor_logged;

static void rescale_viewport_scissor(GLint x, GLint y, GLsizei w, GLsizei h,
                                     GLint *ox, GLint *oy, GLsizei *ow, GLsizei *oh) {
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    if (w < 700 || w == h) {
        // No es el viewport principal del engine (800x480/800x408 en
        // origen): pasa intacto (FBO/shadow-map propio del engine, o una
        // sub-región chica).
        *ox = x; *oy = y; *ow = w; *oh = h;
        return;
    }
    // Engine-space (800x480) -> pantalla real (960x544). Multiplicar antes
    // de dividir para no perder precisión.
    GLint rx = (x * REAL_SCREEN_W) / 800;
    GLint ry = (y * REAL_SCREEN_H) / 480;
    GLsizei rw = (w * REAL_SCREEN_W) / 800;
    GLsizei rh = (h * REAL_SCREEN_H) / 480;
    if (rx > REAL_SCREEN_W) rx = REAL_SCREEN_W;
    if (ry > REAL_SCREEN_H) ry = REAL_SCREEN_H;
    if (rw > REAL_SCREEN_W - rx) rw = REAL_SCREEN_W - rx;
    if (rh > REAL_SCREEN_H - ry) rh = REAL_SCREEN_H - ry;
    *ox = rx; *oy = ry; *ow = rw; *oh = rh;
}

void glViewport_soloader(GLint x, GLint y, GLsizei width, GLsizei height) {
    if (viewport_logged < 10) {
        uintptr_t off = gl_caller_off();
        (void)off; // l_info compiles out in non-Debug builds
        l_info("[gl_viewport] #%u: x=%d y=%d w=%d h=%d (caller 0x%x)",
               viewport_logged + 1, x, y, width, height, (unsigned)off);
        viewport_logged++;
    }
    GLint rx, ry;
    GLsizei rw, rh;
    rescale_viewport_scissor(x, y, width, height, &rx, &ry, &rw, &rh);
    glViewport(rx, ry, rw, rh);
}

void glScissor_soloader(GLint x, GLint y, GLsizei width, GLsizei height) {
    if (scissor_logged < 10) {
        uintptr_t off = gl_caller_off();
        (void)off;
        l_info("[gl_scissor] #%u: x=%d y=%d w=%d h=%d (caller 0x%x)",
               scissor_logged + 1, x, y, width, height, (unsigned)off);
        scissor_logged++;
    }
    GLint rx, ry;
    GLsizei rw, rh;
    rescale_viewport_scissor(x, y, width, height, &rx, &ry, &rw, &rh);
    glScissor(rx, ry, rw, rh);
}

void glTexImage2D_soloader(GLenum target, GLint level, GLint internalformat,
                           GLsizei width, GLsizei height, GLint border,
                           GLenum format, GLenum type, const GLvoid *pixels) {
    tex_upload_count++;
    if (tex_upload_count <= 12 || (tex_upload_count % 200) == 0) {
        uintptr_t off = gl_caller_off();
        (void)off; // l_info compiles out in non-Debug builds
        l_info("[gl_upload] glTexImage2D #%u: target=0x%x level=%d internal=0x%x "
               "%dx%d border=%d format=0x%x type=0x%x pixels=%p (caller 0x%x)",
               tex_upload_count, target, level, internalformat,
               width, height, border, format, type, pixels, (unsigned)off);
    }
    glTexImage2D(target, level, internalformat, width, height, border,
                 format, type, pixels);
}

void glCompressedTexImage2D_soloader(GLenum target, GLint level, GLenum internalformat,
                                     GLsizei width, GLsizei height, GLint border,
                                     GLsizei imageSize, const GLvoid *data) {
    ctex_upload_count++;
    if (ctex_upload_count <= 12 || (ctex_upload_count % 200) == 0) {
        uintptr_t off = gl_caller_off();
        (void)off;
        l_info("[gl_upload] glCompressedTexImage2D #%u: target=0x%x level=%d internal=0x%x "
               "%dx%d border=%d imageSize=%d data=%p (caller 0x%x)",
               ctex_upload_count, target, level, internalformat,
               width, height, border, imageSize, data, (unsigned)off);
    }
    glCompressedTexImage2D(target, level, internalformat, width, height, border,
                           imageSize, data);
}

void glTexSubImage2D_soloader(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                              GLsizei width, GLsizei height, GLenum format,
                              GLenum type, const GLvoid *pixels) {
    sub_upload_count++;
    if (sub_upload_count <= 12 || (sub_upload_count % 200) == 0) {
        uintptr_t off = gl_caller_off();
        (void)off;
        l_info("[gl_upload] glTexSubImage2D #%u: target=0x%x level=%d off=%d,%d "
               "%dx%d format=0x%x type=0x%x pixels=%p (caller 0x%x)",
               sub_upload_count, target, level, xoffset, yoffset,
               width, height, format, type, pixels, (unsigned)off);
    }
    glTexSubImage2D(target, level, xoffset, yoffset, width, height,
                    format, type, pixels);
}

#if defined(USE_GLSL_SHADERS) && defined(DUMP_COMPILED_SHADERS)
void load_shader(GLuint shader, const char * string, size_t length) {
    char* sha_name = str_sha1sum(string, length);

    char gxp_path[256];
    snprintf(gxp_path, sizeof(gxp_path), DATA_PATH"gxp/%s.gxp", sha_name);

    if (file_exists(gxp_path)) {
        uint8_t *buffer;
        size_t size;

        file_load(gxp_path, &buffer, &size);

        glShaderBinary(1, &shader, 0, buffer, (int32_t) size);

        free(buffer);
        skip_next_compile = GL_TRUE;
    } else {
        glShaderSource(shader, 1, &string, &length);
        strcpy(next_shader_fname, gxp_path);
    }

    free(sha_name);
}
#elif defined(USE_GLSL_SHADERS)
void load_shader(GLuint shader, const char * string, size_t length) {
    glShaderSource(shader, 1, &string, &length);
}
#elif defined(USE_CG_SHADERS) && defined(DUMP_COMPILED_SHADERS)
void load_shader(GLuint shader, const char * string, size_t length) {
    char* sha_name = str_sha1sum(string, length);

    char gxp_path[256];
    char cg_path[256];
    snprintf(gxp_path, sizeof(gxp_path), DATA_PATH"gxp/%s.gxp", sha_name);
    snprintf(cg_path, sizeof(cg_path), DATA_PATH"cg/%s.cg", sha_name);

    if (file_exists(gxp_path)) {
        uint8_t *buffer;
        size_t size;

        file_load(gxp_path, &buffer, &size);

        glShaderBinary(1, &shader, 0, buffer, (int32_t) size);

        free(buffer);
        skip_next_compile = GL_TRUE;
    } else if (file_exists(cg_path)) {
        char *buffer;
        size_t size;

        file_load(cg_path, (uint8_t **) &buffer, &size);

        glShaderSource(shader, 1, &string, &size);
        strcpy(next_shader_fname, gxp_path);

        free(buffer);
        skip_next_compile = GL_FALSE;
    } else {
        l_warn("Encountered an untranslated shader %s, saving GLSL "
               "and using a dummy shader.", sha_name);

        char glsl_path[256];
        snprintf(glsl_path, sizeof(glsl_path), DATA_PATH"glsl/%s.glsl", sha_name);
        file_mkpath(glsl_path, 0777);
        file_save(glsl_path, (const uint8_t *) string, length);

        if (strstr(string, "gl_FragColor")) {
            const char *dummy_shader = "float4 main() { return float4(1.0,1.0,1.0,1.0); }";
            int32_t dummy_shader_len = (int32_t) strlen(dummy_shader);
            glShaderSource(shader, 1, &dummy_shader, &dummy_shader_len);
        } else {
            const char *dummy_shader = "void main(float4 out gl_Position : POSITION ) { gl_Position = float4(1.0,1.0,1.0,1.0); }";
            int32_t dummy_shader_len = (int32_t) strlen(dummy_shader);
            glShaderSource(shader, 1, &dummy_shader, &dummy_shader_len);
        }

        skip_next_compile = GL_FALSE;
    }

    free(sha_name);
}
#elif defined(USE_CG_SHADERS) || defined(USE_GXP_SHADERS)
void load_shader(GLuint shader, const char * string, size_t length) {
    char* sha_name = str_sha1sum(string, length);

    char path[256];
#ifdef USE_CG_SHADERS
    snprintf(path, sizeof(path), DATA_PATH"cg/%s.cg", sha_name);
#else
    snprintf(path, sizeof(path), DATA_PATH"gxp/%s.gxp", sha_name);
#endif

    if (file_exists(path)) {
#ifdef USE_CG_SHADERS
        char *buffer;
        size_t size;

        file_load(path, (uint8_t **) &buffer, &size);

        glShaderSource(shader, 1, &string, &size);

        free(buffer);
#else
        uint8_t *buffer;
        size_t size;

        file_load(path, &buffer, &size);

        glShaderBinary(1, &shader, 0, buffer, (int32_t) size);

        free(buffer);
#endif
    } else {
        l_warn("Encountered an untranslated shader %s, saving GLSL "
               "and using a dummy shader.", sha_name);

        char glsl_path[256];
        snprintf(glsl_path, sizeof(glsl_path), DATA_PATH"glsl/%s.glsl", sha_name);
        file_mkpath(glsl_path, 0777);
        file_save(glsl_path, (const uint8_t *) string, length);

        if (strstr(string, "gl_FragColor")) {
            const char *dummy_shader = "float4 main() { return float4(1.0,1.0,1.0,1.0); }";
            int32_t dummy_shader_len = (int32_t) strlen(dummy_shader);
            glShaderSource(shader, 1, &dummy_shader, &dummy_shader_len);
        } else {
            const char *dummy_shader = "void main(float4 out gl_Position : POSITION ) { gl_Position = float4(1.0,1.0,1.0,1.0); }";
            int32_t dummy_shader_len = (int32_t) strlen(dummy_shader);
            glShaderSource(shader, 1, &dummy_shader, &dummy_shader_len);
        }
    }

    free(sha_name);
}
#else
#error "Define one of (USE_GLSL_SHADERS, USE_CG_SHADERS, USE_GXP_SHADERS)"
#endif
