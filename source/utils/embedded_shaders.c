#include "utils/embedded_shaders.h"
#include "utils/logger.h"
#include "utils/utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef DATA_PATH
#define DATA_PATH "ux0:data/sacredodyssey/"
#endif

static const char s_ProfileCOMMON_emul_VS[] =
    "attribute highp vec4 Position;\n"
    "#if defined(TEXTURED)\n"
    "attribute mediump vec2 TexCoord0;\n"
    "varying mediump vec2 vTexCoord0;\n"
    "uniform mediump mat4 TextureMatrix0;\n"
    "#endif\n"
    "#if defined(LIGHTMAP)\n"
    "attribute mediump vec2 TexCoord1;\n"
    "varying mediump vec2 vTexCoord1;\n"
    "uniform mediump mat4 TextureMatrix2;\n"
    "#endif\n"
    "attribute lowp vec4 Color0;\n"
    "varying lowp vec4 vColor0;\n"
    "uniform highp mat4 WorldViewProjectionMatrix;\n"
    // NOTA (2026-09-11, crash 1789184124): aqui HUBO bloques de hardware
    // skinning (#ifdef SKINNED/QUATSKINNED/TEXTURESKINNED con BoneMatrices[28],
    // BoneQuat0/1[28], BoneTexture, WeightMask, SkinIndices/SkinWeights). Se
    // revirtieron porque el binder del motor
    // (CMaterialRendererManager, "parameter array size mismatch") exige
    // IGUALDAD EXACTA entre el tamano del array declarado en el programa y el
    // del material -- y ese tamano deriva del bone count DE CADA MALLA
    // (mc.bdae: Bone1..23, magic_horse: Bone1..26), asi que ningun tamano
    // estatico puede coincidir con todas: el mismatch lleva al path
    // autoAddAndBindParameter, que deja [ip,#4] en basura y crashea en
    // endMaterialRenderer+0x166 (`ldrb r3,[fp,#28]`, dump 1789184124).
    // Sin los arrays el bind falla "graceful" (spam "invalid bind symbol",
    // malla en bind pose) y el juego llega a ingame -- estado 22:39 probado.
    // El fix correcto a futuro es generar el ProfileCOMMON por material con
    // su bone count (como el path procedural SProfileGLES2 de Android), no un
    // template estatico.
    "\n"
    "void main(void)\n"
    "{\n"
    "    gl_Position = WorldViewProjectionMatrix * Position;\n"
    "#if defined(TEXTURED)\n"
    "    vTexCoord0 = (TextureMatrix0 * vec4(TexCoord0, 0.0, 1.0)).xy;\n"
    "#endif\n"
    "#if defined(LIGHTMAP)\n"
    "    vTexCoord1 = (TextureMatrix2 * vec4(TexCoord1, 0.0, 1.0)).xy;\n"
    "#endif\n"
    "    vColor0 = Color0;\n"
    "}\n";

static const char s_ProfileCOMMON_emul_FS[] =
    "precision mediump float;\n"
    "\n"
    "#if defined(TEXTURED)\n"
    "uniform sampler2D Sampler0;\n"
    "uniform sampler2D texture;\n"
    "varying mediump vec2 vTexCoord0;\n"
    "#endif\n"
    "\n"
    "#if defined(MULTITEXTURED)\n"
    "uniform sampler2D Sampler1;\n"
    "#endif\n"
    "\n"
    "#if defined(LIGHTMAP)\n"
    "uniform sampler2D Sampler2;\n"
    "varying mediump vec2 vTexCoord1;\n"
    "#endif\n"
    "\n"
    "uniform vec4 DiffuseColor;\n"
    "\n"
    "#if defined(LIGHTING)\n"
    "uniform vec4 ambientcolor;\n"
    "uniform vec4 specularcolor;\n"
    "uniform vec4 emissioncolor;\n"
    "uniform float shininess;\n"
    "#endif\n"
    "\n"
    "void main(void)\n"
    "{\n"
    "#if defined(TEXTURED)\n"
    "    vec4 color = texture2D(Sampler0, vTexCoord0);\n"
    "#if defined(MULTITEXTURED)\n"
    // Guarded like vColor0/DiffuseColor below: la segunda textura de un
    // material MULTITEXTURED no siempre llega enlazada en este port (ver
    // "invalid bind symbol: texture1/texture2" + "Unused parameter" en los
    // Unlit* hermanos, y envmapIntensity sin enlazar en el programa 41
    // MULTITEXTURED|TEXTURESKINNED de la montura). Muestrear una unidad sin
    // textura completa devuelve (0,0,0,1) en GLES2, y el `color *= tex1` sin
    // guarda convertiria TODO el material en negro -- exactamente la montura
    // "completamente negra" reportada. Solo se multiplica si tex1 trae senal.
    "    vec4 tex1 = texture2D(Sampler1, vTexCoord0);\n"
    "    if ((tex1.r + tex1.g + tex1.b) > 0.03) {\n"
    "        color *= tex1;\n"
    "    }\n"
    "#endif\n"
    "#if defined(LIGHTMAP)\n"
    "    vec4 light = texture2D(Sampler2, vTexCoord1);\n"
    "    color.rgb *= light.rgb * 2.0;\n"
    "#endif\n"
    "#else\n"
    "    vec4 color = vec4(1.0, 1.0, 1.0, 1.0);\n"
    "#endif\n"
    "    if (DiffuseColor.a > 0.01 && (DiffuseColor.r > 0.01 || DiffuseColor.g > 0.01 || DiffuseColor.b > 0.01)) {\n"
    "        color.rgb *= DiffuseColor.rgb;\n"
    "    }\n"
    "    gl_FragColor = color;\n"
    "}\n";

static const char s_UnlitOneTextureAndVertexColorVP[] =
    "attribute highp vec4 Position;\n"
    "attribute lowp vec4 Color0;\n"
    "attribute mediump vec2 TexCoord0;\n"
    "uniform highp mat4 WorldViewProjectionMatrix;\n"
    "uniform mediump mat4 TextureMatrix0;\n"
    "varying mediump vec2 vTexCoord0;\n"
    "varying lowp vec4 vColor0;\n"
    "\n"
    "void main(void)\n"
    "{\n"
    "    gl_Position = WorldViewProjectionMatrix * Position;\n"
    "    vColor0 = Color0;\n"
    "    vTexCoord0 = (TextureMatrix0 * vec4(TexCoord0, 0.0, 1.0)).xy;\n"
    "}\n";

static const char s_UnlitTexturedFP[] =
    "precision mediump float;\n"
    "uniform sampler2D texture;\n"
    "uniform vec4 DiffuseColor;\n"
    "varying mediump vec2 vTexCoord0;\n"
    "varying lowp vec4 vColor0;\n"
    "\n"
    "void main(void)\n"
    "{\n"
    "    vec4 color = texture2D(texture, vTexCoord0);\n"
    "    vec4 vc = (vColor0.a > 0.01 && (vColor0.r > 0.01 || vColor0.g > 0.01 || vColor0.b > 0.01)) ? vColor0 : vec4(1.0);\n"
    "    color *= vc;\n"
    "    gl_FragColor = color;\n"
    "}\n";

static const char s_UnlitTexturedBlendTextureAlphaFP[] =
    "precision mediump float;\n"
    "uniform sampler2D texture;\n"
    "uniform vec4 DiffuseColor;\n"
    "varying mediump vec2 vTexCoord0;\n"
    "varying lowp vec4 vColor0;\n"
    "\n"
    "void main(void)\n"
    "{\n"
    "    vec4 color = texture2D(texture, vTexCoord0);\n"
    "    vec4 vc = (vColor0.a > 0.01 && (vColor0.r > 0.01 || vColor0.g > 0.01 || vColor0.b > 0.01)) ? vColor0 : vec4(1.0);\n"
    "    color *= vc;\n"
    "    gl_FragColor = color;\n"
    "}\n";

static const char s_UnlitMultiTexturedFP[] =
    "precision mediump float;\n"
    "uniform sampler2D texture;\n"
    "uniform sampler2D texture1;\n"
    "uniform sampler2D texture2;\n"
    "varying mediump vec2 vTexCoord0;\n"
    "varying lowp vec4 vColor0;\n"
    "\n"
    "void main(void)\n"
    "{\n"
    // Sesion 2026-09-15: esta uniform se llamaba solo "texture1", pero el
    // binder de ESE material pedia literalmente "texture2" -> se renombro.
    // Sesion 2026-09-16: log_20260916_203851.txt volvio a mostrar "invalid
    // bind symbol: texture1"/"Unused parameter: texture1" (3 materiales
    // distintos: mainmenu2/camglow, TalkIcons/icon_group02, y uno mas
    // temprano en boot) -- es decir, DISTINTOS materiales piden distintos
    // nombres literales para su segunda textura segun como se autoro el
    // efecto original ("texture1" vs "texture2"), no hay un nombre unico
    // correcto. Declarar ambos cubre a cualquiera de los dos sin repetir
    // esta persecucion de nombre cada vez que aparece un material nuevo.
    // Guarda de senal igual que en ProfileCOMMON MULTITEXTURED: una unidad
    // sin textura completa devuelve (0,0,0,1) y multiplicar a ciegas
    // ennegreceria el sprite. glLinkProgram_soloader (glutil.c) fuerza el
    // valor por defecto de texture1/texture2 a una unidad de textura vacia
    // (no la 0, que ya usa "texture") para que el que NO reciba dato real del
    // material lea (0,0,0,0) -- sin esto, ambos por defecto en la unidad 0
    // leerian la MISMA imagen que "texture" y la guarda de senal los dejaria
    // pasar igual, multiplicando el color por si mismo (oscurecimiento
    // incorrecto en vez de simplemente no aplicar segunda textura).
    "    vec4 color = texture2D(texture, vTexCoord0);\n"
    "    vec4 tex1 = texture2D(texture1, vTexCoord0);\n"
    "    if ((tex1.r + tex1.g + tex1.b) > 0.03) {\n"
    "        color *= tex1;\n"
    "    }\n"
    "    vec4 tex2 = texture2D(texture2, vTexCoord0);\n"
    "    if ((tex2.r + tex2.g + tex2.b) > 0.03) {\n"
    "        color *= tex2;\n"
    "    }\n"
    "    vec4 vc = (vColor0.a > 0.01 && (vColor0.r > 0.01 || vColor0.g > 0.01 || vColor0.b > 0.01)) ? vColor0 : vec4(1.0);\n"
    "    color *= vc;\n"
    "    gl_FragColor = color;\n"
    "}\n";

static const char s_UnlitVertexColorVP[] =
    "attribute highp vec4 Position;\n"
    "attribute lowp vec4 Color0;\n"
    "uniform highp mat4 WorldViewProjectionMatrix;\n"
    "varying lowp vec4 vColor0;\n"
    "\n"
    "void main(void)\n"
    "{\n"
    "    gl_Position = WorldViewProjectionMatrix * Position;\n"
    "    vColor0 = Color0;\n"
    "}\n";

static const char s_UnlitVertexColorFP[] =
    "precision mediump float;\n"
    "varying lowp vec4 vColor0;\n"
    "\n"
    "void main(void)\n"
    "{\n"
    "    gl_FragColor = vColor0;\n"
    "}\n";

static const char s_UnlitMaterialColorVP[] =
    "attribute highp vec4 Position;\n"
    "uniform highp mat4 WorldViewProjectionMatrix;\n"
    "\n"
    "void main(void)\n"
    "{\n"
    "    gl_Position = WorldViewProjectionMatrix * Position;\n"
    "}\n";

static const char s_UnlitMaterialColorFP[] =
    "precision mediump float;\n"
    "uniform vec4 DiffuseColor;\n"
    "\n"
    "void main(void)\n"
    "{\n"
    "    gl_FragColor = (DiffuseColor.a > 0.01) ? DiffuseColor : vec4(1.0, 1.0, 1.0, 1.0);\n"
    "}\n";

typedef struct {
    const char *name;
    const char *source;
} EmbeddedShaderEntry;

static const EmbeddedShaderEntry s_embedded_shaders[] = {
    { "ProfileCOMMON_emul_VS.glsl", s_ProfileCOMMON_emul_VS },
    { "ProfileCOMMON_emul_FS.glsl", s_ProfileCOMMON_emul_FS },
    { "UnlitOneTextureAndVertexColorVP.glsl", s_UnlitOneTextureAndVertexColorVP },
    { "UnlitTexturedFP.glsl", s_UnlitTexturedFP },
    { "UnlitTexturedBlendTextureAlphaFP.glsl", s_UnlitTexturedBlendTextureAlphaFP },
    { "UnlitMultiTexturedFP.glsl", s_UnlitMultiTexturedFP },
    { "UnlitVertexColorVP.glsl", s_UnlitVertexColorVP },
    { "UnlitVertexColorFP.glsl", s_UnlitVertexColorFP },
    { "UnlitMaterialColorVP.glsl", s_UnlitMaterialColorVP },
    { "UnlitMaterialColorFP.glsl", s_UnlitMaterialColorFP },
};

#define NUM_EMBEDDED_SHADERS (sizeof(s_embedded_shaders) / sizeof(s_embedded_shaders[0]))

const char *get_embedded_shader(const char *name, size_t *out_len) {
    if (!name) return NULL;
    const char *base = strrchr(name, '/');
    base = base ? base + 1 : name;

    for (size_t i = 0; i < NUM_EMBEDDED_SHADERS; i++) {
        if (strcmp(base, s_embedded_shaders[i].name) == 0) {
            if (out_len) {
                *out_len = strlen(s_embedded_shaders[i].source);
            }
            return s_embedded_shaders[i].source;
        }
    }
    return NULL;
}

void ensure_embedded_shaders_installed(void) {
    char target_dir[256];
    snprintf(target_dir, sizeof(target_dir), "%sGloftSOHP/data/3d/effects", DATA_PATH);
    // file_mkpath (no mkdir): crea GloftSOHP/data/3d/ intermedios si faltan.
    // Con mkdir() plano la instalacion fallaba en silencio en instalaciones
    // limpias y el motor se quedaba sin .glsl -> pantalla rosada.
    file_mkpath(target_dir, 0777);

    for (size_t i = 0; i < NUM_EMBEDDED_SHADERS; i++) {
        char dst_path[256];
        snprintf(dst_path, sizeof(dst_path), "%s/%s", target_dir, s_embedded_shaders[i].name);

        size_t src_len = strlen(s_embedded_shaders[i].source);
        int need_write = 1;

        // Comparar CONTENIDO, no solo tamano. Un fix de shader que solo
        // renombra una uniform (ej. "texture1" -> "texture2", sesion
        // 2026-09-15) no cambia src_len un solo byte: el chequeo anterior
        // (solo tamano) lo consideraba "ya instalado" y dejaba el .glsl
        // viejo/roto en ux0:data para siempre, sin importar cuantas veces se
        // recompilara y redeployara el eboot -- confirmado en consola: el log
        // de la sesion siguiente seguia mostrando exactamente el mismo
        // "invalid bind symbol: texture2" que el fix debia eliminar.
        FILE *chk = fopen(dst_path, "rb");
        if (chk) {
            fseek(chk, 0, SEEK_END);
            long sz = ftell(chk);
            if (sz == (long)src_len) {
                fseek(chk, 0, SEEK_SET);
                char *buf = (char *)malloc(src_len);
                if (buf) {
                    size_t read_n = fread(buf, 1, src_len, chk);
                    if (read_n == src_len && memcmp(buf, s_embedded_shaders[i].source, src_len) == 0) {
                        need_write = 0;
                    }
                    free(buf);
                }
            }
            fclose(chk);
        }

        if (need_write) {
            FILE *dst = fopen(dst_path, "wb");
            if (dst) {
                fwrite(s_embedded_shaders[i].source, 1, src_len, dst);
                fclose(dst);
                l_info("Installed embedded shader: %s (%u bytes)", dst_path, (unsigned)src_len);
            } else {
                l_warn("Failed to write embedded shader: %s", dst_path);
            }
        }
    }
}
