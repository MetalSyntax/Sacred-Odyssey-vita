/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2022      Rinnegatamante
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "reimpl/io.h"

#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <stdarg.h>
#include <psp2/kernel/threadmgr.h>

#ifdef USE_SCELIBC_IO
#include <libc_bridge/libc_bridge.h>
#endif

#include "utils/logger.h"
#include "utils/utils.h"
#include "utils/embedded_shaders.h"

#include <so_util/so_util.h>

extern so_module so_mod;

// Includes the following inline utilities:
// int oflags_musl_to_newlib(int flags);
// dirent64_bionic * dirent_newlib_to_bionic(struct dirent* dirent_newlib);
// void stat_newlib_to_bionic(struct stat * src, stat64_bionic * dst);
#include "reimpl/bits/_struct_converters.c"

// Minimal valid DDS: 4x4, uncompressed 32-bit RGBA (solid mid-gray), no FourCC compression --
// decodes and uploads fine on any GPU (including PS Vita's PowerVR SGX, which has no ATC
// support). Used as a stand-in for .kot textures that turn out to be ATC-compressed (see
// looks_like_atc_dds() below) -- generated with a small offline script, not hand-typed.
static const unsigned char placeholder_atc_dds_bytes[] = {
    0x44,0x44,0x53,0x20,0x7c,0x00,0x00,0x00,0x0f,0x10,0x00,0x00,0x04,0x00,0x00,0x00,
    0x04,0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x00,0x00,0x00,
    0x41,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x00,0x00,0x00,0x00,0x00,0xff,0x00,
    0x00,0xff,0x00,0x00,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0x00,0x10,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x80,0x80,0x80,0xff,0x80,0x80,0x80,0xff,0x80,0x80,0x80,0xff,0x80,0x80,0x80,0xff,
    0x80,0x80,0x80,0xff,0x80,0x80,0x80,0xff,0x80,0x80,0x80,0xff,0x80,0x80,0x80,0xff,
    0x80,0x80,0x80,0xff,0x80,0x80,0x80,0xff,0x80,0x80,0x80,0xff,0x80,0x80,0x80,0xff,
    0x80,0x80,0x80,0xff,0x80,0x80,0x80,0xff,0x80,0x80,0x80,0xff,0x80,0x80,0x80,0xff,
};

// Confirmed on real hardware (log_20260904_174749.txt: a hang, not a crash -- 300+ repeated
// fopen/fclose of the exact same data/lowtexture/wt_zhuwang_2lowpt.kot, no progress for
// minutes): this port's asset dataset embeds ATC (Adreno Texture Compression, a Qualcomm/AMD
// GPU-specific compressed format -- FourCC "ATC ", "ATCI" or "ATCA") texture data inside .kot
// files. glitch::video::CImageLoaderDDS::loadImage() explicitly RECOGNIZES all three FourCCs
// (confirmed in Ghidra: 0x20435441/0x49435441/0x41435441) and happily constructs a CImage with
// them -- the failure isn't in the .so's own DDS parser, it's downstream when vitaGL tries to
// upload that CImage to the PS Vita's PowerVR GPU, which has no ATC support at all (it natively
// speaks PVRTC). Since CTextureManager::getTexture() never caches a "permanently failed"
// placeholder for a texture whose GL upload didn't succeed, EVERY distinct scene object/material
// referencing this exact texture name independently re-opens and re-decodes the same file from
// scratch (the "wisdomtree" scene apparently has hundreds of such references) -- not a single
// infinite loop, but hundreds of slow, guaranteed-to-fail disk+decode attempts back to back,
// which is indistinguishable from a hang in practice.
// Fix: content-sniff (not filename-based -- future non-ATC .kot files must be unaffected) any
// successfully-opened .kot file for the DDS magic + an ATC FourCC at the standard header offset
// (0x54: magic[4] + the rest of DDS_HEADER's fixed 76 bytes + pixel format's dwSize/dwFlags), and
// transparently redirect to a tiny valid uncompressed DDS placeholder instead -- same "serve a
// visually-degraded-but-valid substitute" trade-off already used for missing effect shaders
// above, just for a hardware-compression incompatibility instead of a missing file.
static bool looks_like_atc_dds(FILE *f) {
    unsigned char hdr[0x58];
#ifdef USE_SCELIBC_IO
    size_t n = sceLibcBridge_fread(hdr, 1, sizeof(hdr), f);
    sceLibcBridge_fseek(f, 0, SEEK_SET);
#else
    size_t n = fread(hdr, 1, sizeof(hdr), f);
    fseek(f, 0, SEEK_SET);
#endif
    if (n < sizeof(hdr)) return false;
    if (memcmp(hdr, "DDS ", 4) != 0) return false;
    return hdr[0x54] == 'A' && hdr[0x55] == 'T' && hdr[0x56] == 'C';
}

static const char *get_placeholder_atc_dds_path(void) {
    static const char path[] = DATA_PATH "placeholder_atc.dds";
    FILE *chk = fopen(path, "rb");
    if (chk) {
        fclose(chk);
        return path;
    }
    FILE *w = fopen(path, "wb");
    if (w) {
        fwrite(placeholder_atc_dds_bytes, 1, sizeof(placeholder_atc_dds_bytes), w);
        fclose(w);
        l_info("Created placeholder ATC-DDS substitute: %s", path);
    }
    return path;
}


typedef enum {
    ATC_RGB = 1,
    ATC_EXPLICIT_ALPHA = 3,
    ATC_INTERPOLATED_ALPHA = 5,
} ATITCDecodeFlag;

void atitc_decode(uint8_t *encodeData, uint8_t *decodeData, const int pixelsWidth, const int pixelsHeight, ATITCDecodeFlag decodeFlag);

static const char *get_decoded_atc_dds_path(const char *original_path, FILE *f, char *out_path) {
    unsigned char hdr[128];
#ifdef USE_SCELIBC_IO
    sceLibcBridge_fseek(f, 0, SEEK_SET);
    size_t n = sceLibcBridge_fread(hdr, 1, 128, f);
#else
    fseek(f, 0, SEEK_SET);
    size_t n = fread(hdr, 1, 128, f);
#endif

    if (n < 128) return get_placeholder_atc_dds_path();

    uint32_t height = *(uint32_t*)&hdr[12];
    uint32_t width = *(uint32_t*)&hdr[16];
    uint32_t mipmap_count = *(uint32_t*)&hdr[28];
    uint32_t fourcc = *(uint32_t*)&hdr[84];

    ATITCDecodeFlag flag;
    if (fourcc == 0x20435441) { // "ATC "
        flag = ATC_RGB;
    } else if (fourcc == 0x41435441) { // "ATCA"
        flag = ATC_EXPLICIT_ALPHA;
    } else if (fourcc == 0x49435441) { // "ATCI"
        flag = ATC_INTERPOLATED_ALPHA;
    } else {
        return get_placeholder_atc_dds_path();
    }

    char *hash = str_sha1sum(original_path, 0);
    if (!hash) return get_placeholder_atc_dds_path();
    // "atc1-" prefix versions the cache: decodes written by older/buggy revisions
    // (placeholder-era single file, or headers with compressed dwFlags) are never
    // reused -- a stale corrupt decode would otherwise be served forever via the
    // file_exists() fast path below with no way to invalidate it from the Vita.
    snprintf(out_path, 256, DATA_PATH "cache/atc1-%s.dds", hash);
    free(hash);

    if (file_exists(out_path)) {
        return out_path;
    }

    file_mkpath(DATA_PATH "cache/", 0777);

#ifdef USE_SCELIBC_IO
    sceLibcBridge_fseek(f, 0, SEEK_END);
    size_t file_size = sceLibcBridge_ftell(f);
    sceLibcBridge_fseek(f, 0, SEEK_SET);
#else
    fseek(f, 0, SEEK_END);
    size_t file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
#endif

    uint8_t *file_data = malloc(file_size);
    if (!file_data) return get_placeholder_atc_dds_path();
    
#ifdef USE_SCELIBC_IO
    sceLibcBridge_fread(file_data, 1, file_size, f);
#else
    fread(file_data, 1, file_size, f);
#endif

    size_t uncompressed_size = 0;
    uint32_t w = width, h = height;
    int mips_to_decode = (mipmap_count == 0) ? 1 : mipmap_count;
    for (int i = 0; i < mips_to_decode; i++) {
        uncompressed_size += w * h * 4;
        w /= 2; if (w == 0) w = 1;
        h /= 2; if (h == 0) h = 1;
    }

    uint8_t *uncompressed_data = malloc(uncompressed_size);
    if (!uncompressed_data) {
        free(file_data);
        return get_placeholder_atc_dds_path();
    }

    uint8_t *src = file_data + 128;
    uint8_t *dst = uncompressed_data;
    int decoded_mips = 0;

    w = width; h = height;
    for (int i = 0; i < mips_to_decode; i++) {
        uint32_t bw = (w + 3) / 4;
        uint32_t bh = (h + 3) / 4;
        if (bw == 0) bw = 1;
        if (bh == 0) bh = 1;
        
        size_t compressed_mip_size = bw * bh * ((flag == ATC_RGB) ? 8 : 16);

        if (src + compressed_mip_size > file_data + file_size) {
            break;
        }


        uint32_t dec_w = (w < 4) ? 4 : w;
        uint32_t dec_h = (h < 4) ? 4 : h;
        
        if (w < 4 || h < 4) {
            uint8_t tmp[64];
            atitc_decode(src, tmp, dec_w, dec_h, flag);
            // Copy the w*h pixels from the 4x4 block
            for (uint32_t y = 0; y < h; y++) {
                memcpy(dst + y * w * 4, tmp + y * dec_w * 4, w * 4);
            }
        } else {
            atitc_decode(src, dst, w, h, flag);
        }

        src += compressed_mip_size;
        dst += w * h * 4;
        decoded_mips++;
        w /= 2; if (w == 0) w = 1;
        h /= 2; if (h == 0) h = 1;
    }

    if (decoded_mips == 0) {
        // Truncated file: not even the top mip was decodable. Writing a
        // header-only DDS would just move the failure into the engine's DDS
        // parser (and its retry loop), so serve the placeholder instead.
        free(uncompressed_data);
        free(file_data);
        return get_placeholder_atc_dds_path();
    }

    unsigned char new_hdr[128];
    memcpy(new_hdr, hdr, 128);
    *(uint32_t*)&new_hdr[20] = width * 4;
    // The pixel data below is UNCOMPRESSED RGBA, but memcpy() carried over the
    // original compressed header's dwFlags (DDSD_LINEARSIZE, set for ATC) and
    // mip count. A DDS parser that trusts dwFlags/size fields would misread the
    // texture (pitch vs linear size, or more mips than bytes present after an
    // early break above), fail the load, and -- given this engine's retry
    // behavior -- spin. Rewrite both to describe what was actually written:
    // DDSD_CAPS|HEIGHT|WIDTH|PITCH|PIXELFORMAT (+MIPMAPCOUNT only if >1 mip),
    // with dwMipMapCount clamped to the mips really decoded.
    {
        uint32_t new_flags = 0x1 | 0x2 | 0x4 | 0x8 | 0x1000; // CAPS|H|W|PITCH|PF
        if (decoded_mips > 1) new_flags |= 0x20000; // DDSD_MIPMAPCOUNT
        *(uint32_t*)&new_hdr[8] = new_flags;
        *(uint32_t*)&new_hdr[28] = (uint32_t)decoded_mips;
    }
    new_hdr[80] = 0x41;
    new_hdr[81] = 0x00;
    new_hdr[82] = 0x00;
    new_hdr[83] = 0x00;
    *(uint32_t*)&new_hdr[84] = 0;
    *(uint32_t*)&new_hdr[88] = 32;
    *(uint32_t*)&new_hdr[92] = 0x00FF0000;
    *(uint32_t*)&new_hdr[96] = 0x0000FF00;
    *(uint32_t*)&new_hdr[100] = 0x000000FF;
    *(uint32_t*)&new_hdr[104] = 0xFF000000;

    FILE *out = fopen(out_path, "wb");
    if (out) {
        fwrite(new_hdr, 1, 128, out);
        fwrite(uncompressed_data, 1, dst - uncompressed_data, out);
        fclose(out);
        l_info("Decoded ATC texture %s -> %s (%ux%u, fourcc=%.4s, mips %d->%d, %u bytes)",
               original_path, out_path, width, height, (const char *)&fourcc,
               mips_to_decode, decoded_mips, (unsigned)(dst - uncompressed_data));
    }

    free(uncompressed_data);
    free(file_data);

    return out_path;
}

// Reopen-storm detector (diagnostic for the LiveArea-splash hang): the engine
// re-resolves + re-opens the same data/lowtexture/*.kot hundreds of times
// without ever reaching frame 60. Counting consecutive opens of the same path
// with timestamps answers the one question static analysis cannot: is each
// iteration SLOW (software transcode per referencing object, finite -- the fix
// is patience/pre-transcoding, not a patch) or FAST (a true retry loop --
// needs the offending branch NOPed like ASprite::Load's 0x20f43c)? The logged
// caller .so offset (runtime addr minus so_mod.text_base) identifies the exact
// looping call site for objdump. Our own generated files (cache/, placeholder)
// are excluded so the redirect's inner open doesn't reset the streak.
// Best-effort without locks (fopen can race threads).
static char storm_path[256];
static unsigned storm_count;
static uint64_t storm_start_ms;

// Storm read accounting: which stage rejects the file per iteration? For FILEs
// opened while a storm is underway (see storm_count gate at the add points),
// accumulate fread() bytes and report the total at fclose, plus the reader's
// .so offsets on the first reads. Whole-file totals => full parse then reject
// (empty-table retry class, patchable branch nearby); header-only totals =>
// early reject (magic/size/count gate, serve-side fix). Ring of 8 is plenty:
// the storm is strictly sequential (open -> read -> close per iteration).
#define STORM_RING_N 8
static FILE *storm_ring_file[STORM_RING_N];
static size_t storm_ring_bytes[STORM_RING_N];
static unsigned storm_ring_reads[STORM_RING_N];
static unsigned storm_fread_logged;

static int storm_ring_find(FILE *f) {
    for (int i = 0; i < STORM_RING_N; i++)
        if (storm_ring_file[i] == f) return i;
    return -1;
}

static void storm_ring_add(FILE *f) {
    static unsigned next = 0;
    storm_ring_file[next] = f;
    storm_ring_bytes[next] = 0;
    storm_ring_reads[next] = 0;
    next = (next + 1) % STORM_RING_N;
}

static bool storm_active_for(const char *filename) {
    return storm_count >= 20 && strcmp(filename, storm_path) == 0;
}

size_t fread_soloader(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t n;
#ifdef USE_SCELIBC_IO
    n = sceLibcBridge_fread(ptr, size, nmemb, stream);
#else
    n = fread(ptr, size, nmemb, stream);
#endif
    int i = storm_ring_find(stream);
    if (i >= 0) {
        storm_ring_bytes[i] += n * size;
        storm_ring_reads[i]++;
        if (storm_fread_logged < 12 || (storm_fread_logged % 200) == 0) {
            uintptr_t ra = (uintptr_t)__builtin_return_address(0);
            uintptr_t off = (so_mod.text_base && ra > so_mod.text_base)
                                ? ra - so_mod.text_base : ra;
            (void)ra; (void)off; // logs compile out in non-Debug builds
            l_info("[fread_acct] storm read #%u: %u x %u = %u bytes "
                   "(file total %u in %u reads, reader .so offset 0x%x)",
                   storm_fread_logged, (unsigned)size, (unsigned)nmemb,
                   (unsigned)(n * size), (unsigned)storm_ring_bytes[i],
                   storm_ring_reads[i], (unsigned)off);
        }
        storm_fread_logged++;
    }
    return n;
}

FILE * fopen_soloader(const char * filename, const char * mode) {
    if (strcmp(filename, "/proc/cpuinfo") == 0) {
        return fopen_soloader("app0:/cpuinfo", mode);
    } else if (strcmp(filename, "/proc/meminfo") == 0) {
        return fopen_soloader("app0:/meminfo", mode);
    }

    bool storm_internal = (strstr(filename, "/cache/") != NULL ||
                           strstr(filename, "placeholder_atc.dds") != NULL);
    if (!storm_internal && mode) {
        uint64_t now = current_timestamp_ms();
        if (storm_count == 0 || strcmp(filename, storm_path) != 0) {
            snprintf(storm_path, sizeof(storm_path), "%s", filename);
            storm_count = 1;
            storm_start_ms = now;
        } else {
            storm_count++;
            if ((storm_count % 50) == 0) {
                uintptr_t ra = (uintptr_t)__builtin_return_address(0);
                uintptr_t off = (so_mod.text_base && ra > so_mod.text_base)
                                    ? ra - so_mod.text_base : ra;
                // (void) casts: l_warn/l_debug compile out in non-Debug builds.
                (void)ra; (void)off;
                l_warn("[fopen_storm] %s opened %u consecutive times in %llu ms "
                       "(caller .so offset 0x%x)",
                       filename, storm_count, (unsigned long long)(now - storm_start_ms),
                       (unsigned)off);
            }
        }
    }

#ifdef USE_SCELIBC_IO
    FILE* ret = sceLibcBridge_fopen(filename, mode);
#else
    FILE* ret = fopen(filename, mode);
#endif

    // Redirección ATC (transcodificar un .kot ATC a un DDS RGBA en cache/):
    // DESACTIVADA por defecto desde el rebase a v1.0.6.
    //
    // Se escribió persiguiendo el cuelgue de wt_zhuwang_2lowpt.kot, que al final
    // no tenía nada que ver con el formato de la textura: era el desfase de ids de
    // recurso entre el .so v1.0.3 y el dataset v1.0.1 (ver Fase 9 en
    // port_progress.md). Dejarla activa ahora sería caro y sin contrapartida: las
    // 1569 texturas .kot del dataset v1.0.6 son TODAS ATC ("ATCA" en el FourCC del
    // offset 0x54), así que cada una pagaría SHA1 + decode por software + escritura
    // de caché en el almacenamiento de la Vita la primera vez que se usa. Antes
    // sólo UN archivo llegaba hasta aquí (la carga nunca pasaba de la fuente).
    //
    // Y no hace falta: vitaGL decodifica ATC por software desde el commit upstream
    // 0c1f75d6 (GL_ATC_RGB_AMD / GL_ATC_RGBA_EXPLICIT_ALPHA_AMD /
    // GL_ATC_RGBA_INTERPOLATED_ALPHA_AMD, con el mismo atitc_decode de cocos2d-x que
    // usa este archivo), y CImageLoaderDDS del motor reconoce los tres FourCC ATC.
    //
    // Todo el pipeline (looks_like_atc_dds / get_decoded_atc_dds_path / caché
    // versionada) se conserva intacto: poner esto a true lo reactiva sin más cambios.
    static const bool atc_redirect_enabled = false;

    // NOTE: read-mode only (mode[0] == 'r'). Sniffing write-mode streams would
    // fread() a write-only FILE (fails, harmless) but must never redirect a
    // file the engine is creating -- a redirected "wb" would write the game's
    // output over our cache/placeholder instead of its real destination.
    if (atc_redirect_enabled &&
        ret && mode && mode[0] == 'r' && strstr(filename, ".kot") != NULL &&
        strstr(filename, "placeholder_atc.dds") == NULL &&
        looks_like_atc_dds(ret)) {
        char placeholder_buf[256];
        const char *placeholder = get_decoded_atc_dds_path(filename, ret, placeholder_buf);
#ifdef USE_SCELIBC_IO
        sceLibcBridge_fclose(ret);
#else
        fclose(ret);
#endif
        // Rate-limited: an unthrottled WARN here adds two lines per storm
        // iteration and buries the [fopen_storm] summary above. First sighting
        // of each path always logs (with caller offset); reminders every 100.
        if (storm_count <= 2 || (storm_count % 100) == 0) {
            uintptr_t ra = (uintptr_t)__builtin_return_address(0);
            uintptr_t off = (so_mod.text_base && ra > so_mod.text_base)
                                ? ra - so_mod.text_base : ra;
            (void)ra; (void)off; // l_warn compiles out in non-Debug builds
            l_warn("ATC-compressed DDS texture %s is not supported by the Vita GPU -- "
                   "redirecting to decoded cache %s (caller .so offset 0x%x)",
                   filename, placeholder, (unsigned)off);
        }
        ret = fopen_soloader(placeholder, mode);
        l_debug("fopen(%s, %s): %p", filename, mode, ret);
        // Track the FILE the game actually reads (the redirect target), keyed
        // to the outer storm path, so fread accounting below sees every byte.
        if (ret && storm_active_for(filename))
            storm_ring_add(ret);
        return ret;
    }

    // Missing data/3d/effects/*.glsl are deliberately NOT redirected to any
    // fallback file (same discipline as the Asphalt-5 / Shadow-Guardian /
    // Dungeon-Hunter-2 ports, whose io.c has no shader fallback and show real
    // graphics). The dataset ships no raw .glsl sources (verified: effects/
    // only holds CustomEffect.bdae/DefaultEffects.bdae in both the v1.0.6
    // agmod data and the v1.0.1 Adreno data; the 31 .obfs files are all <3 KB
    // config stubs with no shader sources inside), so on Android this same
    // fopen() also fails and the engine falls through to its procedural
    // SProfileGLES2 path. Serving a static pink substitute here ("Pink Bad
    // Shader" source, byte-identical to the engine's own embedded
    // createEmptyShader fallback found in libsacredodyssey.so) makes the
    // engine believe an override file exists and compile THAT instead -- every
    // material in the game then binds the pink program, which has no
    // DiffuseColor/Sampler0/TextureMatrix0 uniforms, producing the all-pink
    // screen plus the "invalid bind symbol" / "Unused parameter" spam seen in
    // log_20260909_224913.txt. Let it fail; do not reintroduce a shader
    // fallback without new evidence.
    if (!ret && strncmp(filename, "app0:", 5) != 0) {
        // Logo de Gameloft del Splash: el motor pide "gameloft_3x_tga" (variante
        // para pantallas grandes) pero el dataset v1.0.6 solo trae gameloft[_2x][_kr]
        // (Res.array confirma que no existe ningun gameloft_3x_*). Sin esto,
        // getTexture() devuelve NULL y SplashState::Draw2D salta el dibujo (ver
        // parche en patch.c): splash negro sin logo. Se redirige al 2x real, que
        // SÍ existe en el dataset. Solo lectura; el target no se llama igual,
        // asi que no hay recursion. Si el engine luego pide un .sprite hermano
        // y falla, cae al comportamiento actual (skip sin crash).
        if (!ret && mode && mode[0] == 'r') {
            const char *slash = strrchr(filename, '/');
            const char *base = slash ? slash + 1 : filename;
            if (strcmp(base, "gameloft_3x_tga") == 0) {
                static const char logo_path[] =
                    DATA_PATH "GloftSOHP/data/2d/sprites/High_Quality/gameloft_2x.kot";
                l_warn("Missing splash logo %s -- redirecting to %s", filename, logo_path);
                ret = fopen_soloader(logo_path, mode);
            } else if (strcmp(base, "KnightsOdyssey_hand2_diffuse.tga") == 0) {
                // La mano del personaje en mc.bdae referencia
                // "KnightsOdyssey_hand2_diffuse.tga" (el motor la pide como
                // "/./KnightsOdyssey_hand2_diffuse.tga" y luego como
                // "./KnightsOdyssey_hand2_diffuse.tga"; log_20260911_223927.txt
                // lineas 3225-3230: "Missing file" + "Could not find texture
                // file" en ambas), pero ningun dataset conocido la trae (el
                // .zip v1.0.6 solo tiene KnightsOdyssey_hand_diffuse.kot).
                // Sin esto el material de la mano queda sin textura (errores
                // de textura visibles en el personaje). Se redirige a la
                // textura de mano que SI existe. Solo lectura y sin recursion
                // (el target no se llama igual).
                static const char hand_path[] =
                    DATA_PATH "GloftSOHP/data/3d/objects/MainCharacter/KnightsOdyssey_hand_diffuse.kot";
                l_warn("Missing hand texture %s -- redirecting to %s", filename, hand_path);
                ret = fopen_soloader(hand_path, mode);
            } else if (strstr(filename, "data/3d/effects/") != NULL && strstr(base, ".glsl") != NULL) {
                // Servir el shader embebido en el eboot (misma tabla que
                // ensure_embedded_shaders_installed() instala al arranque).
                // Es la red que atrapa el caso en que la instalacion inicial
                // no pudo escribir (p. ej. effects/ aun no existia): sin esto
                // el motor compilaba fuente vacia y la vista de carga se
                // pintaba de rosa en vez de mostrar loading_splash.kot.
                size_t emb_len = 0;
                const char *emb_src = get_embedded_shader(base, &emb_len);
                if (emb_src && emb_len > 0 && strncmp(filename, "app0:", 5) != 0) {
                    file_mkpath(filename, 0777);
                    FILE *w = fopen(filename, "wb");
                    if (w) {
                        fwrite(emb_src, 1, emb_len, w);
                        fclose(w);
                        l_info("Installed on-demand embedded shader %s (%u bytes)",
                               filename, (unsigned)emb_len);
                        ret = fopen_soloader(filename, mode);
                    }
                }
                // Ultimo recurso: shaders empaquetados en el .vpk (requiere
                // haber reinstalado el .vpk completo, no solo el eboot).
                if (!ret) {
                    char app0_shader[256];
                    snprintf(app0_shader, sizeof(app0_shader), "app0:/shaders/%s", base);
                    ret = fopen_soloader(app0_shader, mode);
                    if (ret) {
                        l_info("Redirected missing shader %s -> %s", filename, app0_shader);
                    }
                }
            }
        }
        // NOTE: glsl.config is deliberately NOT redirected here. Confirmed on
        // real hardware (comparing log_20260903_234632.txt against every log
        // after this redirect was added) that glitch::video::
        // CGLSLShaderManager::initAdditionalConfig() already handles this file
        // being missing (NULL) just fine and proceeds straight to compiling
        // shaders -- the "does not return" tag Ghidra puts on that branch is a
        // decompiler artifact, not a real hang. Serving an EMPTY substitute
        // file instead routes the engine through its "file exists" path
        // (size/alloc/read/second cleanup call) that normally never executes
        // for this project's data set -- THAT path is what actually hangs
        // (confirmed: every run with this redirect stops right after opening
        // the substitute file and never reaches glCompileShader; every run
        // without it reaches glCompileShader fine with glsl.config staying
        // NULL). Do not reintroduce this redirect without new evidence.
    }

    if (ret)
        l_debug("fopen(%s, %s): %p", filename, mode, ret);
    else
        l_warn("fopen(%s, %s): %p", filename, mode, ret);

    // Non-redirected storm files (any future non-ATC .kot, or the path after
    // the redirect is disabled): track the game-visible FILE the same way.
    if (ret && !storm_internal && storm_active_for(filename))
        storm_ring_add(ret);

    return ret;
}

int open_soloader(const char * path, int oflag, ...) {
    if (strcmp(path, "/proc/cpuinfo") == 0) {
        return open_soloader("app0:/cpuinfo", oflag);
    } else if (strcmp(path, "/proc/meminfo") == 0) {
        return open_soloader("app0:/meminfo", oflag);
    }

    mode_t mode = 0666;
    if (((oflag & BIONIC_O_CREAT) == BIONIC_O_CREAT) ||
        ((oflag & BIONIC_O_TMPFILE) == BIONIC_O_TMPFILE)) {
        va_list args;
        va_start(args, oflag);
        mode = (mode_t)(va_arg(args, int));
        va_end(args);
    }

    oflag = oflags_bionic_to_newlib(oflag);
    int ret = open(path, oflag, mode);
    if (ret >= 0)
        l_debug("open(%s, %x): %i", path, oflag, ret);
    else
        l_warn("open(%s, %x): %i", path, oflag, ret);
    return ret;
}

int fstat_soloader(int fd, stat64_bionic * buf) {
    struct stat st;
    int res = fstat(fd, &st);

    if (res == 0)
        stat_newlib_to_bionic(&st, buf);

    l_debug("fstat(%i): %i", fd, res);
    return res;
}

int stat_soloader(const char * path, stat64_bionic * buf) {
    struct stat st;
    int res = stat(path, &st);

    if (res == 0)
        stat_newlib_to_bionic(&st, buf);

    l_debug("stat(%s): %i", path, res);
    return res;
}

int fclose_soloader(FILE * f) {
    int i = storm_ring_find(f);
    if (i >= 0) {
        l_info("[fread_acct] storm file closed (%s): %u bytes in %u reads",
               storm_path, (unsigned)storm_ring_bytes[i], storm_ring_reads[i]);
        storm_ring_file[i] = NULL;
        storm_ring_bytes[i] = 0;
        storm_ring_reads[i] = 0;
    }
#ifdef USE_SCELIBC_IO
    int ret = sceLibcBridge_fclose(f);
#else
    int ret = fclose(f);
#endif

    l_debug("fclose(%p): %i", f, ret);
    return ret;
}

int close_soloader(int fd) {
    int ret = close(fd);
    l_debug("close(%i): %i", fd, ret);
    return ret;
}

DIR* opendir_soloader(char* _pathname) {
    DIR* ret = opendir(_pathname);
    l_debug("opendir(\"%s\"): %p", _pathname, ret);
    return ret;
}

struct dirent64_bionic * readdir_soloader(DIR * dir) {
    static struct dirent64_bionic dirent_tmp;

    struct dirent* ret = readdir(dir);
    l_debug("readdir(%p): %p", dir, ret);

    if (ret) {
        dirent64_bionic* entry_tmp = dirent_newlib_to_bionic(ret);
        memcpy(&dirent_tmp, entry_tmp, sizeof(dirent64_bionic));
        free(entry_tmp);
        return &dirent_tmp;
    }

    return NULL;
}

int readdir_r_soloader(DIR * dirp, dirent64_bionic * entry,
                       dirent64_bionic ** result) {
    struct dirent dirent_tmp;
    struct dirent * pdirent_tmp;

    int ret = readdir_r(dirp, &dirent_tmp, &pdirent_tmp);

    if (ret == 0) {
        dirent64_bionic* entry_tmp = dirent_newlib_to_bionic(&dirent_tmp);
        memcpy(entry, entry_tmp, sizeof(dirent64_bionic));
        *result = (pdirent_tmp != NULL) ? entry : NULL;
        free(entry_tmp);
    }

    l_debug("readdir_r(%p, %p, %p): %i", dirp, entry, result, ret);
    return ret;
}

int closedir_soloader(DIR * dir) {
    int ret = closedir(dir);
    l_debug("closedir(%p): %i", dir, ret);
    return ret;
}

int fcntl_soloader(int fd, int cmd, ...) {
    l_warn("fcntl(%i, %i, ...): not implemented", fd, cmd);
    return 0;
}

int ioctl_soloader(int fd, int request, ...) {
    l_warn("ioctl(%i, %i, ...): not implemented", fd, request);
    return 0;
}

int fsync_soloader(int fd) {
    int ret = fsync(fd);
    l_debug("fsync(%i): %i", fd, ret);
    return ret;
}
