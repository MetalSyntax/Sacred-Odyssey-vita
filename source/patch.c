/*
 * Copyright (C) 2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  patch.c
 * @brief Patching some of the .so internal functions or bridging them to native
 *        for better compatibility.
 */

#include <kubridge.h>
#include <so_util/so_util.h>
#include "utils/logger.h"
#include "utils/glutil.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

extern so_module so_mod;

static char **m_gAppPath_ptr = NULL;

static void initPath_hook(void) {
    if (m_gAppPath_ptr) {
        if (!*m_gAppPath_ptr) {
            *m_gAppPath_ptr = (char *)malloc(512);
        }
        // El motor espera la misma estructura que en Android
        // (/sdcard/gameloft/games/GloftSOHP/), donde GloftSOHP/ es la carpeta
        // que contiene data/*.obfs, intro/logo.m4v, etc.
        snprintf(*m_gAppPath_ptr, 512, "%sGloftSOHP/", DATA_PATH);
        chdir(*m_gAppPath_ptr);
        l_info("initPath hook: m_gAppPath set to %s", *m_gAppPath_ptr);
    }
}

// Helpers de diagnóstico rect eliminados (2026-09-12): cumplieron su misión
// (probar que el dest fullscreen SÍ llegaba al draw call).
//
// NOTA (2026-09-12, revertido el mismo día): hubo una receta intermedia
// ("Asphalt") que corría el engine en un FBO offscreen de 800x480 y hacía un
// blit de upscale a 960x544 en gl_swap() -- ELIMINADA a pedido explícito
// (solo reescalado de viewport/scissor, sin ningún FBO real en este port; ver
// glutil.c/gl_swap()). El engine vuelve a dibujar DIRECTO sobre el
// framebuffer real; glViewport_soloader/glScissor_soloader (glutil.c)
// reescalan 800x480 -> 960x544. Los parches de rect forzado a 960
// (SplashState/LoadingState::Draw2D, clip de ASprite, ASprite::SetScale) que
// esta nota decía "obsoletos por el blit" pueden volver a hacer falta sin el
// FBO -- pendiente de confirmar en consola si el splash/loading vuelve a
// verse recortado a 800px de ancho, y de restaurarlos si es así.

static void license_validate_stub(bool param_1) {
    (void)param_1;
    uintptr_t r4_val = 0;
    __asm__ volatile("mov %0, r4" : "=r"(r4_val));
    l_debug("ALicenseCheck_ValidateLicense bypassed (caller r4=%p)", (void *)r4_val);
}

static int license_validate_native_stub(void) {
    l_debug("ALicenseCheck::ValidateNative bypassed -> returning 1");
    return 1;
}

static void license_init_stub(void *env, void *clazz) {
    (void)env;
    (void)clazz;
    l_info("ALicenseCheck_InitLicense bypassed");
}

static void license_load_config_stub(void) {
    l_info("ALicenseCheck::LoadConfig bypassed");
}

// Llamado desde el trampolin de World::LoadMap (ver so_patch_v106, sitio
// 0x2171aa): el do/while de "World loading: game objects" carga ~100+
// objetos en una sola pasada sin ceder el control al loop principal, asi que
// ningun gl_swap() ocurre hasta que termina -- confirmado en
// log_20260912_020011.txt (fps cae a 2.2-2.5 exactamente durante
// "LoadWorld()"/justo tras "MainCharacter::SaveAll!"). Throttle a 1 de cada 4
// llamadas (cada game object dispara esto una vez) para no pagar un
// vglSwapBuffers de mas por objeto. El juego tiene su propia pantalla/barra
// de carga (se sigue redibujando con cada swap); esto solo asegura que ese
// redibujado en si ocurra durante la carga en vez de quedar bloqueado hasta
// que el do/while completo termine. NO reduce el tiempo total de carga.
static void world_load_yield(void) {
    static unsigned call_count = 0;
    call_count++;
    if ((call_count % 4) != 0) return;
    gl_swap();
}

// Raw-offset patches for libsacredodyssey.so v1.0.6 (Thumb-2). Same discipline
// as the v103 section below: every site is verified (expected original bytes)
// before writing anything, so a future binary that shifts layout fails safe
// (skip + WARN) instead of corrupting code.
static void so_patch_v106(void) {
    // Fix deterministic Data abort right after "Load game graphml init"
    // (dumps sacredodyssey-psp2core-1788587087-... and -1788591714-...: identical
    // PC 0x7a086 / LR 0x7a079 in .dynstr string data, R1=0xffff, no CustomEffect
    // file access, no "- Error - File not found" print).
    // Chain (v106 true disassembly + v106 Ghidra pseudo-C, cross-checked):
    //   KnightOdyssey::Init -> EffectManager::GetInstance() (fine, ctor already
    //   ran during renderer init) -> InitSpecialEffectMateiral() @ 0x1bf6e4 ->
    //   CColladaDatabaseC1("CustomEffect.bdae") @ 0x29dc24 (stores
    //   CResFileManager::load() result unchecked; the load resolves silently
    //   with nothing usable -- the map lookup never reaches the filesystem)
    //   -> constructEffect(char*) @ 0x29e78c -> getEffect @ 0x29e3d4 (miss-safe,
    //   returns NULL) -> constructEffect(SEffect*) @ 0x29e7b4, which does
    //     29e7b8: ldr r1, [r1, #4]
    //     29e7c0: ldr r2, [r1, #0]      ; no NULL check
    //     29e7ca: ldr.w ip, [r2, #20]
    //     29e7d0: blx ip                ; call through garbage -> .dynstr
    //   with a dead factory/object chain (same "unchecked miss" family as the
    //   v103 ShowItemEffectProxy 0xffff and FileManager::_Load fixes).
    // Fix: skip the whole function (none of its 5 special effects gets
    // registered -- water/magma/fade materials absent, visuals degraded, game
    // continues; a later QueryFXMaterial() miss without its own check would be
    // the NEXT bug, same recipe). 2-byte hot-patch at entry, replacing the
    // FIRST halfword of `stmdb sp!,{r4-r9,sl,lr}` (encoding e92d 47f0: hw1
    // 0xe92d at 0x1bf6e4 holds the opcode, hw2 0x47f0 the register list) with
    // `bx lr` (0x4770): nothing pushed yet, so returning immediately is
    // stack-balanced. The leftover second halfword never executes.
    volatile uint16_t *entry =
        (volatile uint16_t *)(so_mod.text_base + 0x1bf6e4);
    if (*entry != 0xe92d) {
        l_warn("v106 InitSpecialEffectMateiral entry mismatch (found 0x%04x, want 0xe92d) "
               "-- skipping whole-function skip", *entry);
        return;
    }
    {
        uint16_t bx_lr = 0x4770;
        kuKernelCpuUnrestrictedMemcpy((void *)entry, &bx_lr, sizeof(bx_lr));
    }
    l_info("Patched v106 InitSpecialEffectMateiral whole-function skip (0x1bf6e4 -> bx lr, "
           "read-back 0x%04x)", *entry);

    // Port del fix v103 de ShowItemEffectProxy (misma familia "parámetro 0xffff sin
    // chequear"). En v106 el ctor está en 0x1c0f78 y el sitio es distinto:
    //   1c1074: bl getParameterID(6, 0)   ; r0 = id, 0xffff si falta
    //   1c1078: mov r1, r0
    //   ...
    //   1c1080: cmp r2, r1                ; num_params vs id
    //   1c1082: bhi 1c1130                ; presente -> usa tabla
    //   1c1084: movs r3, #0               ; ausente -> NULL ...
    //   1c1086: ldrb r3, [r3, #6]         ; ... y dereferencia NULL+6
    // Fix: `b 0x1c10a8` (salta el setParameter, continúa el ctor con el pará-
    // metro por defecto) + `nop` de relleno. Son 4 bytes porque el `b.n`
    // incondicional de 16 bits solo cubre 2 (un `b.w` con este offset caería en
    // la codificación ambigua T3/T4 y se decodificaría como `beq.w`).
    {
        static const uint8_t skip[] = { 0x10, 0xe0, 0x00, 0xbf }; // b 0x1c10a8; nop
        volatile uint32_t *site =
            (volatile uint32_t *)(so_mod.text_base + 0x1c1084);
        if (*site != 0x799b2300) { // movs r3,#0; ldrb r3,[r3,#6]
            l_warn("v106 ShowItem ctor site mismatch (found 0x%08x, want 0x799b2300) "
                   "-- skipping param skip", *site);
            return;
        }
        kuKernelCpuUnrestrictedMemcpy((void *)site, skip, sizeof(skip));
        l_info("Patched v106 ShowItemEffectProxy ctor missing-param skip "
               "(0x1c1084 -> b 0x1c10a8; nop, read-back 0x%08x)", *site);
    }

    // Fix del crash justo despues de "Could not find texture file: gameloft_3x_tga"
    // (log_20260905_033521.txt: esa es la ULTIMA linea util antes de que el log se
    // corte, con AMBOS parches v106 ya aplicados y read-backs OK -- ver lineas 14-16
    // de ese log: el juego supero "Load game graphml init", "Material init", cargo
    // fuentes, entro a "Go to state: Splash" y murio pidiendo el logo de Gameloft).
    // Cadena confirmada con pseudo-C v106 + desensamblado Thumb-2 real (no adivinada):
    //   - Res.array del dataset v106 solo trae gameloft_tga / gameloft_2x_tga (indices
    //     139-142, .kot validos) -- NO existe ningun gameloft_3x_tga en esos 4090
    //     entries. El miss es genuino, misma familia "asset faltante" de Fase 8.
    //   - SplashState::FocusGain() @ 0x211834 maneja el miss SIN crashear: con
    //     viewport width 0x3c0 (960, nuestra res) llama a getTexture() y guarda el
    //     resultado (NULL) en this+0x10 con sus guards de refcount, luego llama a
    //     ALicenseCheck_ValidateLicense(false) (nuestro stub, ultima linea del log)
    //     y retorna limpio. Por eso el log llega hasta ahi.
    //   - SplashState::Draw2D() @ 0x211cf4 (Thumb-2) chequea el MATERIAL (this+0x14,
    //     0x211d14/0x211d16 -> beq 0x211da2) pero NUNCA la textura (this+0x10):
    //       211d48: ldr r0, [r4, #16]  ; r0 = textura (NULL por el miss)
    //       211d4a: ldr r3, [r0, #0]   ; CRASH: desreferencia NULL (vtable)
    //       211d4c: ldr r3, [r3, #24]  ; vtable+0x18 (GetSize-like)
    //       211d4e: blx r3
    //     (pseudo-C v106 linea ~99580: `(**(code **)(**(int **)(in_r0+0x10)+0x18))()`.)
    // Fix: trampolin que replica el chequeo que el codigo original ya hace para el
    // material, reutilizando su misma continuacion "sin dibujo" (0x211da2: avanza el
    // contador de frames -- el splash sigue temporizando via Update() y avanza de
    // estado solo, sin logo pero sin crash). Registros/flags seguros: r4 (this) no
    // se toca antes del sitio; r0 se recarga, r3 se replica; el cmp/beq pisa flags
    // pero ni 0x211d4c (siguiente uso: tst.w en 0x211d6e, que los fija el mismo) ni
    // 0x211da2 (ldr,ldr,adds: los fija el mismo) dependen de los de entrada -- mismo
    // estado que el camino legitimo "material NULL" (beq 0x211d16 -> 0x211da2).
    // Trampolin en 0x346388: cuerpo muerto de ALicenseCheck::LoadConfig (hookeada en
    // so_patch(): hook_arm pisa 8 bytes en 0x346380-0x346387 con LDR PC+literal, el
    // cuerpo ARM de 100+ bytes jamas ejecuta). Opcodes verificados ensamblando con
    // arm-vita-eabi-as/-ld -Ttext=<direccion> (misma disciplina que los dos bloques
    // v106 anteriores), no calculados a mano:
    //     346388: ldr r0, [r4, #16]  (0x6920, replay)
    //     34638a: cmp r0, #0         (0x2800)
    //     34638c: beq.n 0x346394     (0xd002, -> nullpath)
    //     34638e: ldr r3, [r0, #0]   (0x6803, replay)
    //     346390: b.w 0x211d4c       (0xf6cb 0xbcdc, resume)
    //     346394: b.w 0x211da2       (0xf6cb 0xbd05, nullpath)
    // Sitio 0x211d48 (4 bytes: ldr r0 + ldr r3 = 0x68036920) -> b.w 0x346388
    // (halfwords {0xf134, 0xbb1e} = uint32 LE 0xbb1ef134 -- OJO: NO 0x1ebb34f1,
    // que es la misma pareja con los halfwords cruzados y ejecuta como
    // `adds r4,#241; subs r3,r7,#2` corrompiendo r4/r3 y cayendo a 0x211d4c:
    // crash real -1788742592 con PC=0x211d4c por ese error, ver port_progress).
    {
        static const uint16_t tramp[] = {
            0x6920, 0x2800, 0xd002, 0x6803, 0xf6cb, 0xbcdc, 0xf6cb, 0xbd05
        };
        static const uint32_t br = 0xbb1ef134; // b.w 0x346388
        volatile uint32_t *site =
            (volatile uint32_t *)(so_mod.text_base + 0x211d48);
        if (*site != 0x68036920u) { // ldr r0,[r4,#16]; ldr r3,[r0,#0]
            l_warn("v106 SplashState::Draw2D texture site mismatch (found 0x%08x, want 0x68036920) "
                   "-- skipping NULL-texture skip", *site);
            return;
        }
        kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x346388),
                                       tramp, sizeof(tramp));
        kuKernelCpuUnrestrictedMemcpy((void *)site, &br, sizeof(br));
        l_info("Patched v106 SplashState::Draw2D NULL-texture skip "
               "(0x211d48 -> b.w 0x346388, read-back 0x%08x)", *site);
    }

    // Fix del Data abort en CParticleSystemSceneNode::init() +0x30 (dump
    // sacredodyssey-psp2core-1788750008-...: PC offset 0x2c371c, R0=0, R1=6).
    // Septimo sitio de la misma familia "parametro 0xffff sin chequear"
    // (ShowItem, DefaultEffectProxy, ...): el loop sobre emisores llama a
    // getParameterID(...,6,0); si falta (caso real: materiales con el Pink
    // fallback, sin uniforms propios) el `bls 0x2c371a` salta al camino que
    // pone r0=0... y 0x2c371c hace `ldr r3,[r0,#0]` SIN chequear -> crash.
    //   2c371a: movs r0, #0   ; missing -> NULL
    //   2c371c: ldr r3, [r0, #0] ; CRASH
    //   2c371e: cbz r3, 2c3722
    //   2c3720: adds r3, #4
    // Fix: se pisan 4 bytes (ldr+cbz) con `b.w 0x346398` y el trampolin replica
    // la secuencia exacta, solo que con r0==0 produce r3=0 (el mismo estado que
    // el camino legitimo "[r0]==0" via cbz) en vez de crashear:
    //     346398: movs r3, #0
    //     34639a: cmp r0, #0
    //     34639c: beq.w 0x3463aa
    //     3463a0: ldr r3, [r0, #0]
    //     3463a2: cmp r3, #0
    //     3463a4: beq.w 0x3463aa
    //     3463a8: adds r3, #4
    //     3463aa: b.w 0x2c3722
    // (el `cbz` original no se puede replicar tal cual: su rango de +-200B no
    // llega a 0x2c3722 desde el trampolin, por eso cmp+beq.w.) Opcodes
    // verificados ensamblando con arm-vita-eabi-as/-ld -Ttext=<direccion>.
    // Trampolin en 0x346398: siguiente hueco libre del cuerpo muerto de
    // ALicenseCheck::LoadConfig (el de Splash ocupa 0x346388-0x346397; el pool
    // de literales arranca en 0x346434, asi que 0x346398+22 bytes entra holgado).
    {
        static const uint16_t tramp[] = {
            0x2300, 0x2800, 0xf000, 0x8005, 0x6803, 0x2b00,
            0xf000, 0x8001, 0x3304, 0xf77d, 0xb9ba
        };
        static const uint32_t br = 0xbe3cf082; // b.w 0x346398 desde 0x2c371c
        volatile uint32_t *site =
            (volatile uint32_t *)(so_mod.text_base + 0x2c371c);
        if (*site != 0xb1036803u) { // ldr r3,[r0,#0]; cbz r3,#+6
            l_warn("v106 CParticleSystemSceneNode::init site mismatch (found 0x%08x, want 0xb1036803) "
                   "-- skipping NULL-param skip", *site);
            return;
        }
        kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x346398),
                                      tramp, sizeof(tramp));
        kuKernelCpuUnrestrictedMemcpy((void *)site, &br, sizeof(br));
        l_info("Patched v106 CParticleSystemSceneNode::init NULL-param skip "
               "(0x2c371c -> b.w 0x346398, read-back 0x%08x)", *site);
    }

    // Fix del Data abort en ftell(NULL) dentro de FileStream::Tell() (dump
    // sacredodyssey-psp2core-1789012303-...: PC en SceLibc, R0=R4=0, pila con
    // _malloc_r/_free_r del loader y retornos Thumb del .so a 0x1c4486
    // (FileStream::Tell), 0x1c4520 (FileStream::Size), 0x1fcc5e
    // (CustomReadFile::getSize) y 0x29b218
    // (CGLSLShaderManager::createShader)). Octavo sitio de la misma familia
    // "recurso faltante sin chequear": el log hermano (log_20260909_235138.txt,
    // 242 lineas, sin rosa porque el fallback PinkBadShader ya se elimino)
    // muestra la secuencia exacta -- fopen(UnlitVertexColorVP.glsl) falla,
    // el motor intenta su fallback propio data/2181175739.obfs, TAMBIEN falla
    // (el dataset no lo trae: los 31 .obfs son stubs de <3 KB), imprime
    // "ERROR: unable to open the file ...", y AUN ASI construye un FileStream
    // / CustomReadFile alrededor del FILE* NULL sin chequear IsValid().
    // createShader() llama a getSize() -> Size() -> Tell(), que hace
    //   1c447e: ldr r3, [r0, #8]
    //   1c4480: ldr r0, [r3, #0]  ; FILE* (NULL por el miss)
    //   1c4482: blx ftell         ; CRASH en libc con R0=0
    // Y aunque Tell sobreviviera, Size() seguiria con Seek(END) sobre el mismo
    // FILE* NULL (0x1c444c: identica secuencia ldr/ldr + blx fseek) -> crash
    // gemelo. Ambos sitios comparten el mismo patron de 4 bytes, asi que se
    // parchean los dos con la misma receta: `b.w` al trampolin, que replica la
    // secuencia exacta pero con FILE*==NULL devuelve 0 (posicion/tamano 0,
    // SEEK fingido como exito) en vez de crashear. Con Size()==0, createShader
    // lee 0 bytes, compila fuente vacia (error logueado, sin crash) y el motor
    // usa SU propio rosa interno solo para ese material (degradado local, no
    // pantalla completa) -- mismo trade-off aceptado para otros assets
    // faltantes. Devolver 0 y NO -1 es deliberado: con -1, Size() retornaria -1
    // y el `strb [sl,fp]` de createShader escribiria un byte ANTES del buffer
    // (corrupcion de heap); con 0 el NUL cae dentro del byte allocado.
    // Trampolines en 0x3463ae (Seek, 18 bytes: 0x3463ae-0x3463bf) y 0x3463c0
    // (Tell, 18 bytes: 0x3463c0-0x3463d1): siguiente hueco libre del cuerpo
    // muerto de ALicenseCheck::LoadConfig (el de CParticleSystem ocupa
    // 0x346398-0x3463ad; el pool de literales arranca en 0x346434). Opcodes
    // verificados ensamblando con arm-vita-eabi-as/-ld -Ttext=<direccion> y
    // desensamblando el .bin result (`-Mforce-thumb`), no calculados a mano:
    //     ldr r3, [r0, #8]  (0x6883, replay)
    //     ldr r0, [r3, #0]  (0x6818, replay)
    //     cmp r0, #0        (0x2800)
    //     beq.n nullpath    (0xd001)
    //     b.w <resume>      (resume = blx fseek/ftell original)
    //     nullpath: movs r0, #0 (0x2000, tamano 0 / SEEK ok)
    //     b.w <return>      (return = pop {r4,pc} original)
    {
        static const uint16_t seek_tramp[] = {
            0x6883, 0x6818, 0x2800, 0xd001, 0xf67e, 0xb84b, 0x2000, 0xf67e,
            0xb845
        }; // resume b.w 0x1c4450, return b.w 0x1c444a
        static const uint32_t seek_br = 0xbfaff181; // b.w 0x3463ae desde 0x1c444c
        volatile uint32_t *seek_site =
            (volatile uint32_t *)(so_mod.text_base + 0x1c444c);
        if (*seek_site != 0x68186883u) { // ldr r3,[r0,#8]; ldr r0,[r3,#0]
            l_warn("v106 FileStream::Seek site mismatch (found 0x%08x, want 0x68186883) "
                   "-- skipping NULL-FILE skip", *seek_site);
            return;
        }
        kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x3463ae),
                                      seek_tramp, sizeof(seek_tramp));
        kuKernelCpuUnrestrictedMemcpy((void *)seek_site, &seek_br, sizeof(seek_br));
        l_info("Patched v106 FileStream::Seek NULL-FILE skip "
               "(0x1c444c -> b.w 0x3463ae, read-back 0x%08x)", *seek_site);
    }

    {
        static const uint16_t tell_tramp[] = {
            0x6883, 0x6818, 0x2800, 0xd001, 0xf67e, 0xb85b, 0x2000, 0xf67e,
            0xb855
        }; // resume b.w 0x1c4482, return b.w 0x1c447c
        static const uint32_t tell_br = 0xbf9ff181; // b.w 0x3463c0 desde 0x1c447e
        volatile uint32_t *tell_site =
            (volatile uint32_t *)(so_mod.text_base + 0x1c447e);
        if (*tell_site != 0x68186883u) { // ldr r3,[r0,#8]; ldr r0,[r3,#0]
            l_warn("v106 FileStream::Tell site mismatch (found 0x%08x, want 0x68186883) "
                   "-- skipping NULL-FILE skip", *tell_site);
            return;
        }
        kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x3463c0),
                                      tell_tramp, sizeof(tell_tramp));
        kuKernelCpuUnrestrictedMemcpy((void *)tell_site, &tell_br, sizeof(tell_br));
        l_info("Patched v106 FileStream::Tell NULL-FILE skip "
               "(0x1c447e -> b.w 0x3463c0, read-back 0x%08x)", *tell_site);
    }

    // Fix del crash en CMaterial::allocate() llamado desde _EnableWaterEffect()
    // durante LoadWorld() (dump sacredodyssey-psp2core-1789107753-...: PC 0x2765be,
    // LR 0x1ddd21, R0=0, R1=sp+...).
    // Causa: InitSpecialEffectMateiral() fue salteado con bx lr en 0x1bf6e4, por lo
    // que EffectManager no tiene registrados los efectos especiales ("water", "magma",
    // "solid", etc.). Cuando SceneObject::EnableSpecialEffect() recorre el mapa 3D
    // buscando nodos de agua o lava, EffectManager::QueryFXMaterial() retorna NULL en
    // el intrusive_ptr, y CMaterial::allocate dereferencia NULL+0x24 (ldr r0, [r0, #36]).
    // Fix: Saltear SceneObject::EnableSpecialEffect(), _EnableWaterEffect,
    // _EnableLavaEffect y _switchMaterialToSolid con `bx lr` (0x4770) en sus entradas.
    {
        volatile uint16_t *site =
            (volatile uint16_t *)(so_mod.text_base + 0x1ddf1c);
        if (*site == 0xb570) { // push {r4, r5, r6, lr}
            uint16_t bx_lr = 0x4770;
            kuKernelCpuUnrestrictedMemcpy((void *)site, &bx_lr, sizeof(bx_lr));
            l_info("Patched v106 SceneObject::EnableSpecialEffect skip (0x1ddf1c -> bx lr)");
        } else {
            l_warn("v106 SceneObject::EnableSpecialEffect entry mismatch (found 0x%04x, want 0xb570)", *site);
        }
    }
    {
        volatile uint16_t *site =
            (volatile uint16_t *)(so_mod.text_base + 0x1ddb94);
        if (*site == 0xe92d) { // stmdb sp!, {r4-r8, sl, lr}
            uint16_t bx_lr = 0x4770;
            kuKernelCpuUnrestrictedMemcpy((void *)site, &bx_lr, sizeof(bx_lr));
            l_info("Patched v106 _EnableWaterEffect skip (0x1ddb94 -> bx lr)");
        }
    }
    {
        volatile uint16_t *site =
            (volatile uint16_t *)(so_mod.text_base + 0x1ddd48);
        if (*site == 0xe92d) { // stmdb sp!, {r4-r9, sl, lr}
            uint16_t bx_lr = 0x4770;
            kuKernelCpuUnrestrictedMemcpy((void *)site, &bx_lr, sizeof(bx_lr));
            l_info("Patched v106 _EnableLavaEffect skip (0x1ddd48 -> bx lr)");
        }
    }
    {
        volatile uint16_t *site =
            (volatile uint16_t *)(so_mod.text_base + 0x1ddf7c);
        if (*site == 0xe92d) { // stmdb sp!, ...
            uint16_t bx_lr = 0x4770;
            kuKernelCpuUnrestrictedMemcpy((void *)site, &bx_lr, sizeof(bx_lr));
            l_info("Patched v106 _switchMaterialToSolid skip (0x1ddf7c -> bx lr)");
        }
    }

    // Fix del crash en CMaterial::allocate() llamado desde FadeOutElement::switchMaterial()
    // durante una cutscene (dump sacredodyssey-psp2core-1789177098-...: PC 0x2765be [misma
    // instruccion que el crash de LoadWorld ya arreglado arriba, ldr r0,[r0,#36] con r0=0],
    // LR 0x1c25de -> FadeOutElement::switchMaterial+0xe2, pila con
    // EffectManager::QueryFXMaterial+0x14). Log de esa corrida (log_20260911_213514.txt)
    // confirma que la carga del mundo llego a "Finish Loading Map"/"MC Created!" y avanzo
    // hasta el dialogo con la chica (CutScenePrologue, talk_woman_unknown_2x) antes de
    // crashear -- el fade-out del personaje/escena en esa cutscene es lo que dispara
    // FadeOutElement::switchMaterial().
    // Misma causa raiz que los 4 sitios de arriba: InitSpecialEffectMateiral() (0x1bf6e4)
    // fue salteado, asi que EffectManager no tiene registrado el material "fade" que este
    // sexto sitio busca via QueryFXMaterial(); el resultado NULL llega sin chequear a
    // CMaterial::allocate(), que desreferencia [r0+36] y crashea. El propio comentario del
    // fix de InitSpecialEffectMateiral (arriba) ya anticipaba esto ("fade materials
    // absent... una futura falla de QueryFXMaterial() sin su propio chequeo seria el
    // PROXIMO bug, misma receta").
    // Fix: mismo patron -- `bx lr` (0x4770) en la entrada de FadeOutElement::switchMaterial
    // (0x1c24fc, stmdb sp!,{r4-r9,sl,fp,lr} = 0xe92d), saltando la funcion entera. El unico
    // otro efecto de esta funcion (recorrer los scene nodes hijos) es puramente para
    // propagar el mismo fade a hijos -- se pierde el efecto visual de fundido, no hay
    // crash ni bloqueo.
    {
        volatile uint16_t *site =
            (volatile uint16_t *)(so_mod.text_base + 0x1c24fc);
        if (*site == 0xe92d) { // stmdb sp!, {r4-r9, sl, fp, lr}
            uint16_t bx_lr = 0x4770;
            kuKernelCpuUnrestrictedMemcpy((void *)site, &bx_lr, sizeof(bx_lr));
            l_info("Patched v106 FadeOutElement::switchMaterial skip (0x1c24fc -> bx lr)");
        } else {
            l_warn("v106 FadeOutElement::switchMaterial entry mismatch (found 0x%04x, want 0xe92d) "
                   "-- skipping whole-function skip", *site);
        }
    }

    // NOTA (2026-09-12): los parches de escalado a 960x544 de
    // SplashState/LoadingState, el clip 800->960 de ASprite y los trampolines
    // diag_rect se habían eliminado durante la receta Asphalt (FBO offscreen
    // 800x480 + blit en gl_swap(), que estiraba el frame completo por su
    // cuenta). Esa receta FBO se revirtió el mismo día a pedido explícito
    // (solo reescalado de viewport/scissor, sin FBO real -- ver glutil.c). Sin
    // el blit, el splash/loading puede volver a verse recortado a 800px de
    // ancho -- pendiente confirmar en consola y restaurar esos parches
    // puntuales (960 en SplashState/LoadingState::Draw2D + clip de ASprite)
    // si el recorte reaparece; los diag_rect NO hace falta traerlos de vuelta
    // (ya cumplieron su misión de diagnóstico).
    (void)0;

    // Fix de la sensacion de "cuelgue" durante autoguardado/transicion de stage
    // (log_20260912_020011.txt: [render_diag] cae a fps=2.2-2.5 exactamente
    // durante "--------LoadWorld() world N!" y justo despues de
    // "------------MainCharacter::SaveAll!"). Causa ya diagnosticada en la
    // Fase 12 (port_progress.md): World::LoadMap() caso 1 ("World loading:
    // game objects") es un do/while (confirmado en el .so real,
    // _ZN5World7LoadMapEv 0x217192-0x2171b0) que llama a
    // GameObjectManager::Load(int,int) (0x1d8d38) para cada uno de los ~100+
    // game objects del mundo EN UNA SOLA PASADA, sin ceder el control al loop
    // principal -- ningun gl_swap() puede ocurrir hasta que el do/while
    // completo termine:
    //   217192: movs r5, #0                  ; i = 0
    //   217194: ldr r2, [r4, #0]              ; loop:
    //   217196: ldr r3, [r3, #84]
    //   21719c: ldr.w r1, [r3, r5, lsl #2]
    //   2171a0: adds r5, #1                   ; i++
    //   2171a2: ldr r0, [r2, #44]
    //   2171a4: ldr r2, [r4, #4]
    //   2171a6: bl 0x1d8d38                    ; GameObjectManager::Load(int,int)
    //   2171aa: ldr r3, [r4, #72]             ; <- sitio parcheado (4 bytes)
    //   2171ac: ldr r2, [r3, #80]
    //   2171ae: cmp r2, r5                    ; i < count?
    //   2171b0: bgt.n 217194                  ; si, repetir
    // Fix: trampolin en la cueva muerta de ALicenseCheck::LoadConfig (mismo
    // cuerpo que los fixes v106 de arriba, siguiente hueco libre tras el de
    // FileStream::Tell en 0x3463c0-0x3463d1; el pool de literales del cuerpo
    // muerto arranca en 0x346434) que replica las DOS instrucciones pisadas
    // (para no romper la comparacion del loop) y ademas llama a
    // world_load_yield() -- primer trampolin de este port que llama desde el
    // .so a codigo C del LOADER (no solo a otro punto del .so), patron nuevo
    // que la Fase 12 dejo pendiente de decision. Opcodes verificados
    // ensamblando con arm-vita-eabi-as/-ld -Ttext=<direccion> (misma
    // disciplina que el resto de los parches v106), no calculados a mano; la
    // palabra final (offset +0x14 del cave) es un placeholder que se pisa en
    // tiempo de ejecucion con la direccion real de world_load_yield() -- es
    // la unica forma de resolverla, ya que el loader no tiene una direccion
    // fija conocida en tiempo de ensamblado del trampolin.
    //     3463d4: ldr r3, [r4, #72]   (replay)
    //     3463d6: ldr r2, [r3, #80]   (replay)
    //     3463d8: push {r0-r5, lr}
    //     3463da: ldr r3, [pc, #12]   ; -> 3463e8 (placeholder, parcheado en runtime)
    //     3463dc: blx r3
    //     3463de: pop {r0-r5, lr}
    //     3463e2: b.w 0x2171ae        ; resume
    //     3463e8: .word <direccion de world_load_yield, parcheada en runtime>
    {
        static const uint16_t tramp[] = {
            0x6ca3, 0x6d1a, 0xb53f, 0x4b03, 0x4798, 0xe8bd,
            0x403f, 0xf6d0, 0xbee4, 0x46c0, 0x0000, 0x0000
        };
        static const uint32_t br = 0xb913f12f; // b.w 0x3463d4 desde 0x2171aa
        volatile uint32_t *site =
            (volatile uint32_t *)(so_mod.text_base + 0x2171aa);
        if (*site != 0x6d1a6ca3u) { // ldr r3,[r4,#72]; ldr r2,[r3,#80]
            l_warn("v106 World::LoadMap game-object loop site mismatch (found 0x%08x, "
                   "want 0x6d1a6ca3) -- skipping load-yield patch", *site);
            return;
        }
        kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x3463d4),
                                      tramp, sizeof(tramp));
        uint32_t cb_addr = (uint32_t)(uintptr_t)&world_load_yield;
        kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x3463d4 + 0x14),
                                      &cb_addr, sizeof(cb_addr));
        kuKernelCpuUnrestrictedMemcpy((void *)site, &br, sizeof(br));
        l_info("Patched v106 World::LoadMap game-object loop yield "
               "(0x2171aa -> b.w 0x3463d4, callback=0x%08x, read-back 0x%08x)",
               cb_addr, *site);
    }
}

// Identidad del build del .so cargado.
//
// Todos los parches por offset CRUDO de so_patch() (los kuKernelCpuUnrestrictedMemcpy
// contra so_mod.text_base + 0x00...) fueron calculados desensamblando
// libsacredodyssey.so v1.0.3-PowerVR y valen SOLO para ese binario: escribirlos
// sobre cualquier otro build machaca instrucciones arbitrarias en direcciones que
// allí significan otra cosa (el v1.0.6, por ejemplo, está compilado en Thumb-2 y
// tiene el .text completamente distinto). Un fallo así no da un error claro sino
// corrupción silenciosa, así que se comprueba la identidad ANTES de escribir nada.
//
// La sonda es la dirección de initPath() relativa a text_base -- distinta en cada
// versión, y ya se resuelve por símbolo más abajo, así que no cuesta nada.
// El bit 0 se enmascara porque en un build Thumb el st_value del símbolo lo trae
// puesto (así es como hook_addr() distingue Thumb de ARM).
#define SO_BUILD_V103_POWERVR 0x001b9f60u  // v1.0.3-PowerVR (ARM)
#define SO_BUILD_V106         0x001bad44u  // v1.0.6 (Thumb-2)

static uint32_t so_build_id(void) {
    uintptr_t sym = so_symbol(&so_mod, "_Z8initPathv");
    if (!sym) return 0;
    return (uint32_t)((sym - so_mod.text_base) & ~(uintptr_t)1);
}

void so_patch(void) {
    m_gAppPath_ptr = (char **)so_symbol(&so_mod, "m_gAppPath");

    uintptr_t initPath_sym = so_symbol(&so_mod, "_Z8initPathv");
    if (initPath_sym) {
        hook_addr(initPath_sym, (uintptr_t)&initPath_hook);
        l_info("Hooked _Z8initPathv successfully");
    } else {
        l_warn("Could not find _Z8initPathv to hook");
    }

    uintptr_t license_sym = so_symbol(&so_mod, "ALicenseCheck_ValidateLicense");
    if (license_sym) {
        hook_addr(license_sym, (uintptr_t)&license_validate_stub);
        l_info("Hooked ALicenseCheck_ValidateLicense successfully");
    }

    uintptr_t license_native_sym = so_symbol(&so_mod, "_ZN13ALicenseCheck14ValidateNativeEv");
    if (license_native_sym) {
        hook_addr(license_native_sym, (uintptr_t)&license_validate_native_stub);
        l_info("Hooked _ZN13ALicenseCheck14ValidateNativeEv successfully");
    }

    uintptr_t license_init_sym = so_symbol(&so_mod, "ALicenseCheck_InitLicense");
    if (license_init_sym) {
        hook_addr(license_init_sym, (uintptr_t)&license_init_stub);
        l_info("Hooked ALicenseCheck_InitLicense successfully");
    }

    uintptr_t license_init_cpp = so_symbol(&so_mod, "_ZN13ALicenseCheck4InitEP7_JNIEnvP7_jclass");
    if (license_init_cpp) {
        hook_addr(license_init_cpp, (uintptr_t)&license_init_stub);
        l_info("Hooked _ZN13ALicenseCheck4InitEP7_JNIEnvP7_jclass successfully");
    }

    uintptr_t license_load_cfg = so_symbol(&so_mod, "_ZN13ALicenseCheck10LoadConfigEv");
    if (license_load_cfg) {
        hook_addr(license_load_cfg, (uintptr_t)&license_load_config_stub);
        l_info("Hooked _ZN13ALicenseCheck10LoadConfigEv successfully");
    }

    uintptr_t license_val_srv = so_symbol(&so_mod, "_ZN13ALicenseCheck14ValidateServerEb");
    if (license_val_srv) {
        hook_addr(license_val_srv, (uintptr_t)&license_validate_stub);
        l_info("Hooked _ZN13ALicenseCheck14ValidateServerEb successfully");
    }

    // A partir de aquí, TODO son parches por offset crudo válidos únicamente para
    // v1.0.3-PowerVR (ver so_build_id() arriba). Los hooks por símbolo de arriba sí
    // son portables entre builds y ya se aplicaron.
    uint32_t build = so_build_id();
    if (build != SO_BUILD_V103_POWERVR) {
        if (build == SO_BUILD_V106) {
            l_info("Detected libsacredodyssey.so v1.0.6 (initPath @ 0x%08x, Thumb-2)", build);
            so_patch_v106();
        } else {
            l_warn("Unknown libsacredodyssey.so build (initPath @ 0x%08x): "
                   "skipping ALL raw-offset patches to avoid corrupting the binary", build);
        }
        return;
    }
    l_info("Detected libsacredodyssey.so v1.0.3-PowerVR: applying raw-offset patches");

    // Fix Data abort in ShowItemEffectProxy::ShowItemEffectProxy() (PC: 0x001c2bbc).
    // When getParameterID(6, 0) returns 0xffff (parameter not found), the original code did:
    //   1c2bac: cmp r2, r1        ; r2 = num_params, r1 = 0xffff
    //   1c2bb0: movls r3, #0      ; r3 = 0
    //   1c2bbc: ldrb r3, [r3, #6] ; CRASH: Data abort dereferencing NULL + 6!
    // We patch 0x001c2bb0 from "movls r3, #0" (0x93a03000) to "bls 0x1c2bec" (0x9a00000d)
    // which safely jumps to 0x001c2bec (skipping setParameter) when the parameter is missing.
    uint32_t patch_show_item = 0x9a00000d;
    kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x001c2bb0), &patch_show_item, sizeof(uint32_t));
    l_info("Patched ShowItemEffectProxy missing parameter crash (0x001c2bb0 -> bls 0x1c2bec)");

    // Fix Data abort in Application::Update() (PC: 0x001bdf84).
    // Original code in Application::Update():
    //   1bdf80: ldr r3, [r3]          ; r3 = Gameplay::s_instance (NULL in menus / startup!)
    //   1bdf84: ldr r3, [r3, #32]     ; CRASH: Data abort dereferencing NULL + 32!
    //   1bdf88: cmp r3, #0
    //   1bdf8c: beq 1bdfac
    // We insert a trampoline into the unused ALicenseCheck::CallJNIFuncChar region (0x003dd010)
    // that safely checks:
    //   if (!Gameplay::s_instance || !Gameplay::s_instance->m_player) goto 1bdfac;
    // and checks the timeScale if valid before returning to 1be018 or 1bdfac.
    // In 0x001bdf80, we replace "ldr r3, [r3]" with "b 0x003dd010" (0xea087c22).
    static const uint32_t patch_update_trampoline[] = {
        0xe5933000, // ldr r3, [r3]
        0xe3530000, // cmp r3, #0
        0x0af783e3, // beq 0x1bdfac
        0xe5933020, // ldr r3, [r3, #32]
        0xe3530000, // cmp r3, #0
        0x0af783e0, // beq 0x1bdfac
        0xeeb77a00, // vmov.f32 s14, #112 (1.0f)
        0xe30120fc, // movw r2, #4348
        0xe0822003, // add r2, r2, r3
        0xedd27a00, // vldr s15, [r2]
        0xeef47a47, // vcmp.f32 s15, s14
        0xeef1fa10, // vmrs APSR_nzcv, fpscr
        0x1af783f4, // bne 0x1be018
        0xeaf783d8, // b 0x1bdfac
    };
    kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x003dd010), patch_update_trampoline, sizeof(patch_update_trampoline));

    uint32_t patch_app_update = 0xea087c22; // b 0x003dd010
    kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x001bdf80), &patch_app_update, sizeof(uint32_t));
    l_info("Patched Application::Update Gameplay::s_instance NULL check (0x001bdf80 -> b 0x003dd010)");

    // Fix Data abort in Application::_Draw(int) (PC: 0x001bd104).
    // Original code:
    //   1bd100: ldr r3, [r2]          ; r3 = Gameplay::s_instance (NULL in menus / startup!)
    //   1bd104: ldrb sl, [r3, #36]    ; CRASH: Data abort dereferencing NULL + 36!
    //   1bd108: cmp sl, #0
    //   1bd10c: beq 1bd210
    // We insert a trampoline into unused region (0x003dd050):
    //   mov sl, #0
    //   cmp r3, #0
    //   ldrneb sl, [r3, #36]
    //   cmp sl, #0
    //   beq 0x1bd210
    //   b 0x1bd110
    // In 0x001bd104, we replace "ldrb sl, [r3, #36]" with "b 0x003dd050" (0xea087fd1).
    static const uint32_t patch_draw_trampoline[] = {
        0xe3a0a000, // mov sl, #0
        0xe3530000, // cmp r3, #0
        0x15d3a024, // ldrbne sl, [r3, #36]
        0xe35a0000, // cmp sl, #0
        0x0af7806a, // beq 0x1bd210
        0xeaf78029, // b 0x1bd110
    };
    kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x003dd050), patch_draw_trampoline, sizeof(patch_draw_trampoline));

    uint32_t patch_app_draw = 0xea087fd1; // b 0x003dd050
    kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x001bd104), &patch_app_draw, sizeof(uint32_t));
    l_info("Patched Application::_Draw Gameplay::s_instance NULL check (0x001bd104 -> b 0x003dd050)");

    // Fix prospective Data abort in Application::Draw2D() (0x001bdc14 - 0x001bdc20).
    // Original code:
    //   1bdc14: ldr r3, [r3]          ; r3 = Gameplay::s_instance
    //   1bdc18: ldr r3, [r3, #32]     ; CRASH: if Gameplay::s_instance is NULL!
    //   1bdc1c: cmp r3, #0
    //   1bdc20: beq 1bdc70
    // We insert a trampoline into unused region (0x003dd070):
    //   ldr r3, [r3]
    //   cmp r3, #0
    //   beq 0x1bdc70
    //   ldr r3, [r3, #32]
    //   cmp r3, #0
    //   beq 0x1bdc70
    //   b 0x1bdc24
    // In 0x001bdc14, we replace "ldr r3, [r3]" with "b 0x003dd070" (0xea087d15).
    static const uint32_t patch_draw2d_trampoline[] = {
        0xe5933000, // ldr r3, [r3]
        0xe3530000, // cmp r3, #0
        0x0af782fc, // beq 0x1bdc70
        0xe5933020, // ldr r3, [r3, #32]
        0xe3530000, // cmp r3, #0
        0x0af782f9, // beq 0x1bdc70
        0xeaf782e5, // b 0x1bdc24
    };
    kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x003dd070), patch_draw2d_trampoline, sizeof(patch_draw2d_trampoline));

    uint32_t patch_app_draw2d = 0xea087d15; // b 0x003dd070
    kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x001bdc14), &patch_app_draw2d, sizeof(uint32_t));
    l_info("Patched Application::Draw2D Gameplay::s_instance NULL check (0x001bdc14 -> b 0x003dd070)");

    // Fix Data abort in EffectManager::update(int) (PC: 0x001bf494).
    // Application::_Draw() calls EffectManager::update() unconditionally on every single
    // frame (confirmed at both of its call sites in the decompiled C, lines ~4979/33216),
    // even before Gameplay::s_instance exists (main menu / splash / any non-gameplay state).
    // Same recurring pattern as Application::Update/_Draw/Draw2D above, just a different
    // field offset (+0x20 instead of +0x24/+0x20 dereferenced through a different register).
    // Original code (ARM mode, not Thumb -- confirmed by disassembling both ways):
    //   1bf488: ldr fp, [pc, #820]    ; GOT offset of &Gameplay::s_instance
    //   1bf48c: ldr r3, [r8, fp]      ; r3 = &Gameplay::s_instance
    //   1bf490: ldr r3, [r3]          ; r3 = Gameplay::s_instance (NULL in menus / startup!)
    //   1bf494: ldr r4, [r3, #32]     ; CRASH: Data abort dereferencing NULL + 32!
    //   1bf498: cmp r4, #0
    //   1bf49c: beq 1bf6c8
    // IMPORTANT (found via a follow-up crash on real hardware after the first version of this
    // fix): 0x1bf6c8 is NOT a plain "do nothing" landing pad -- it's the entry of a loop that
    // uses r4 as its loop counter/index (`add r0, r4, r4, lsl #3; ...; bl Ghost::Destroy`),
    // and in the legitimate case (Gameplay::s_instance non-NULL, m_player == 0) r4 already
    // holds 0 at that point because it's the SAME r4 that was just compared/branched on. The
    // first version of this trampoline jumped to 0x1bf6c8 WITHOUT ever setting r4, leaving it
    // as leftover garbage from before the function's entry, which made the loop compute a
    // garbage pointer and crash inside Ghost::Destroy() (confirmed: LR pointed right after the
    // `bl 1be954 <Ghost::Destroy>` at 0x1bf6d8, PC crashed on Ghost::Destroy's first
    // dereference of its `this`-like r0 argument). Fixed by explicitly zeroing r4 on the
    // NULL-instance path before jumping, using condition codes (moveq/ldrne) so the Z flag from
    // "cmp r3, #0" stays valid through the final "beq" -- MOV/LDR never touch the flags.
    // r4 is not used for anything else before this point, so when Gameplay::s_instance is NULL
    // we can safely jump straight to 0x1bf6c8 with r4 forced to 0 (the same state the legitimate
    // "field is 0" path would have left it in) instead of computing r4 from a NULL pointer.
    // We insert a trampoline into the same unused ALicenseCheck::CallJNIFuncChar dead-code
    // region as the patches above, right after the Draw2D trampoline (0x003dd090):
    //   cmp   r3, #0
    //   moveq r4, #0
    //   ldrne r4, [r3, #32]
    //   beq   0x1bf6c8
    //   b     0x1bf498
    // In 0x001bf494, we replace "ldr r4, [r3, #32]" with "b 0x003dd090" (0xea0876fd).
    static const uint32_t patch_effectmgr_update_trampoline[] = {
        0xe3530000, // cmp r3, #0
        0x03a04000, // moveq r4, #0
        0x15934020, // ldrne r4, [r3, #32]
        0x0af78989, // beq 0x1bf6c8
        0xeaf788fc, // b 0x1bf498
    };
    kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x003dd090), patch_effectmgr_update_trampoline, sizeof(patch_effectmgr_update_trampoline));

    uint32_t patch_effectmgr_update = 0xea0876fd; // b 0x003dd090
    kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x001bf494), &patch_effectmgr_update, sizeof(uint32_t));
    l_info("Patched EffectManager::update Gameplay::s_instance NULL check (0x001bf494 -> b 0x003dd090)");

    // Fix Data abort in DefaultEffectProxy::render() (PC: 0x001c26bc).
    // 6th confirmed site of the same recurring bug (Application::Update/_Draw/Draw2D,
    // EffectManager::update above): code dereferences Gameplay::s_instance->m_player
    // (offset +0x20/32, same field as all the previous fixes) without checking whether
    // Gameplay::s_instance itself is non-NULL first. EffectManager renders its active
    // IEffectProxy list every frame regardless of gameplay state, so this is reachable from
    // the main menu/splash exactly like EffectManager::update was.
    // Original code:
    //   1c26b0: ldr r3, [pc, #40]  ; GOT offset of &Gameplay::s_instance
    //   1c26b4: ldr r3, [r4, r3]   ; r3 = &Gameplay::s_instance
    //   1c26b8: ldr r3, [r3]      ; r3 = Gameplay::s_instance (NULL in menus/splash!)
    //   1c26bc: ldr r3, [r3, #32] ; CRASH: Data abort dereferencing NULL + 32!
    //   1c26c0: cmp r3, #0
    //   1c26c4: popeq {r4, pc}    ; already returns early when m_player == 0 -- we just need
    //                             ; to reach here with r3 == 0 instead of crashing above it.
    // Unlike the EffectManager::update fix, no other register needs seeding here: r3 is the
    // only thing 0x1c26c0 reads, so leaving it at 0 (its value coming in from the NULL check)
    // reproduces exactly the "no player" early-return path.
    // Trampoline placed right after the EffectManager::update fix, in the still-unused (dead,
    // ALicenseCheck::LoadConfig is hooked and unreachable -- see so_patch() above) region at
    // 0x003dd0a4:
    //   cmp r3, #0
    //   ldrne r3, [r3, #32]
    //   b 0x1c26c0
    // In 0x001c26bc, we replace "ldr r3, [r3, #32]" with "b 0x003dd0a4" (0xea086a78).
    // Opcodes verified by assembling with arm-vita-eabi-as/-ld -Ttext=<addr> rather than
    // hand-computing encodings, after the EffectManager::update trampoline shipped with a
    // register-seeding bug on its first attempt.
    //
    // NOTE -- suspected sibling NOT patched yet (no confirmed crash): NightEffectProxy::render()
    // (0x001c3740, also reached via GameNightEffectProxy::render's plain tail-jump at 0x1bec68)
    // has what looks like the exact same GOT-load-of-Gameplay::s_instance-then-deref-+32 pattern
    // at 0x001c37a8-0x001c37b8, but it's guarded inside a "for each queued respawn id" loop that
    // only runs when that proxy's internal vector is non-empty -- so it may not be reachable yet
    // from an idle main menu. If a future crash lands there, apply the same fix.
    static const uint32_t patch_defaulteffectproxy_render_trampoline[] = {
        0xe3530000, // cmp r3, #0
        0x15933020, // ldrne r3, [r3, #32]
        0xeaf79583, // b 0x1c26c0
    };
    kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x003dd0a4), patch_defaulteffectproxy_render_trampoline, sizeof(patch_defaulteffectproxy_render_trampoline));

    uint32_t patch_defaulteffectproxy_render = 0xea086a78; // b 0x003dd0a4
    kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x001c26bc), &patch_defaulteffectproxy_render, sizeof(uint32_t));
    l_info("Patched DefaultEffectProxy::render Gameplay::s_instance NULL check (0x001c26bc -> b 0x003dd0a4)");

    // Fix Data abort in FileManager::_Load(int, int*) on an out-of-range resource id
    // (crash PC inside the LOADER's own newlib -- strlen()/_svfprintf_r() -- not the .so;
    // R0 = 0xfffffff8, LR = _svfprintf_r, R9 = 0x98412890 ("%s%s" constant + word-alignment
    // padding). Confirmed byte-for-byte IDENTICAL across 3 separate .psp2dmp captures this
    // session (sacredodyssey-psp2core-1788539515-..., -1788543426-..., -1788553248-...) --
    // same PC/LR/R0/R1/R9/R10/R11/R12 every single time, i.e. fully deterministic, not stack
    // garbage. In all 3 logs the very last line before the crash is
    // `[DEBUG] sprintf_soloader: fmt="%s%s"` (source/reimpl/fmt.c's unconditional trace,
    // added to chase this exact bug), immediately after StringMgr::SetLanguage() successfully
    // loads "data/texts/text.EN.lang" for the first time (`Application::LoadLanguage()` ->
    // `StringMgr::SetLanguage()` then `CFontMgr::LoadFonts()` -> `CFont::LoadFontTable(id)`).
    // This is the first run in the whole session to get that far, which is why the bug was
    // never hit before.
    //
    // Root cause, found by disassembling FileManager::_Load (ARM mode) instead of trusting
    // Ghidra's pseudo-C at face value:
    //   1cb6c4: subs ip, r1, #0        ; ip = param_1 (resource id)
    //   ...
    //   1cb6d8: blt 1cb764             ; if (param_1 < 0) goto negative_check
    //   1cb6dc: ldr r5, [r0, #44]      ; <-- fallthrough for param_1 >= 0: NO UPPER BOUND CHECK
    //   1cb6e0: add r4, r5, ip, lsl #4 ; r4 = &table[param_1]     (16-byte entries)
    //   ...                            ; (eventually reads names_table[param_1] and hands it
    //                                  ; to Application::GetResourcePath() as the resource name)
    //   1cb764: ldr r1, [r0]           ; r1 = *this
    //   1cb768: ldr r1, [r1]           ; r1 = count (**this)
    //   1cb76c: cmp ip, r1             ; compare param_1 (still negative here) against count
    //   1cb770: movge r0, #0           ; intended "not found" -> r0 = 0 ... but UNREACHABLE:
    //   1cb774: blt 1cb6dc             ; a negative param_1 is *always* < a non-negative count,
    //                                  ; so this branch is *always* taken, jumping right back
    //                                  ; into the "load using param_1 as index" path WITH
    //                                  ; param_1 STILL NEGATIVE (reads names_table[-1], one
    //                                  ; entry before the array). Ghidra's pseudo-C renders this
    //                                  ; exact bug as `if ((param_1 < 0) && (count <= param_1))
    //                                  ; return 0;` -- an `&&` that should have been `||`
    //                                  ; (De Morgan slip negating "0 <= id < count"), same class
    //                                  ; of original-Gameloft logic mistake as
    //                                  ; ShowItemEffectProxy's `movls r3, #0` fixed above.
    //   1cb778: b 1cb708
    // Net effect: FileManager::_Load() has NO effective bounds checking in either direction --
    // a negative id (e.g. FileManager::_GetId()'s "not found" sentinel, -1, for a resource
    // name missing from this port's repacked/incomplete asset set) or a fixed positive id that
    // is simply >= this port's resource count (e.g. one of CFont::LoadFontTable()'s hardcoded
    // font-table ids) both read `*(char **)(names_table + id*8)` out of bounds and hand
    // whatever garbage comes back to Application::GetResourcePath(), whose very first
    // instruction is `sprintf(buf, "%s%s", uVar1, garbage_name)` -- crashing when the loader's
    // newlib strlen()s the garbage %s pointer. Could not pin down from static analysis alone
    // which exact resource id triggers it first (font-table id vs. some other _Load caller),
    // but the fix below is correct for every caller since it enforces the bound the original
    // code already intended to enforce, not a guess specific to one call site.
    // Fix: a trampoline in the same dead ALicenseCheck::LoadConfig region as the fixes above,
    // right after the DefaultEffectProxy::render one (0x003dd0b0 -- well within LoadConfig's
    // 0xd0-byte hooked/unreachable body, which runs through 0x003dd174), that correctly
    // rejects BOTH negative ids and ids >= count before either ever reaches the array read:
    //   ldr r1, [r0]        ; r1 = *this
    //   ldr r1, [r1]        ; r1 = count
    //   cmp ip, #0
    //   blt return_zero     ; id < 0 -> reject
    //   cmp ip, r1
    //   bge return_zero     ; id >= count -> reject
    //   b 0x1cb6dc          ; valid -> continue the original "load" path
    // return_zero:
    //   mov r0, #0
    //   b 0x1cb708          ; original epilogue (add sp, #12; pop {r4, r5, pc})
    // In 0x001cb6d8, we replace "blt 0x1cb764" with "b 0x003dd0b0" (0xea084674).
    // Opcodes verified by assembling with arm-vita-eabi-as/-ld -Ttext=<addr> (same discipline
    // as every trampoline above) rather than hand-computed.
    static const uint32_t patch_filemanager_load_trampoline[] = {
        0xe5901000, // ldr r1, [r0]
        0xe5911000, // ldr r1, [r1]
        0xe35c0000, // cmp ip, #0
        0xba000002, // blt 0x3dd0cc (return_zero)
        0xe15c0001, // cmp ip, r1
        0xaa000000, // bge 0x3dd0cc (return_zero)
        0xeaf7b983, // b 0x1cb6dc
        0xe3a00000, // mov r0, #0
        0xeaf7b98c, // b 0x1cb708
    };
    kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x003dd0b0), patch_filemanager_load_trampoline, sizeof(patch_filemanager_load_trampoline));

    uint32_t patch_filemanager_load = 0xea084674; // b 0x003dd0b0
    kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x001cb6d8), &patch_filemanager_load, sizeof(uint32_t));
    l_info("Patched FileManager::_Load out-of-range resource id crash (0x001cb6d8 -> b 0x003dd0b0)");

    // Fix Data abort in Structs::SpriteTextureEntry::Read(DataStream&) (PC: 0x0020d548).
    // Confirmed with dump sacredodyssey-psp2core-1788557033-...: R0=0x7c, R5=0, R7=0 (NULL) at
    // the crash instruction -- the function reads an element count via DataStream::ReadInt(),
    // allocates count*4 bytes with operator new[] (`_Znaj`) WITHOUT checking the result for
    // NULL, and immediately writes into it. In this run, ASprite::Load(int)'s one-time
    // ResStream(0xfa5) load ended up resolving (via FileManager::_GetDvdName -> Application::
    // GetResourcePath -> FileStream::Open) to a raw DDS texture file
    // (data/lowtexture/wt_zhuwang_2lowpt.kot, confirmed by reading its real bytes: magic
    // "DDS " = 0x20534444 as the first little-endian int, "dwSize" = 0x7c as the second) instead
    // of the small binary sprite-entry table this reader expects -- a resource-id/name-table
    // mismatch in this port's rebuilt asset set, not something fixable in code. The DDS magic
    // read as an element count (0x20534444) makes `count << 2` an ~2 GB allocation request,
    // which this engine's custom operator new[] correctly fails (returns NULL) rather than
    // aborting -- but the caller never checks that, and crashes writing the second ReadInt()
    // (0x7c, the DDS header's real dwSize field) into address NULL+0.
    // Original code, two structurally identical unchecked-allocation blocks in the same
    // function (first array + count at this+0/+4, second array + count at this+8/+0xc):
    //   Block 1:
    //     20d510: bl DataStream::ReadInt        ; r0 = count1
    //     20d514: str r0, [r4]                  ; this->count1 = count1
    //     20d518: lsl r0, r0, #2
    //     20d51c: bl operator_new[]              ; r0 = malloc(count1*4), maybe NULL
    //     20d520: ldr r3, [r4]                   ; r3 = count1 (reload)
    //     20d524: cmp r3, #0                     ; flags used by the "ble" below
    //     20d528: mov r7, r0                     ; r7 = array1 (maybe NULL!)
    //     20d52c: str r0, [r4, #4]               ; this->array1 = r7
    //     20d530: ble 0x20d55c                   ; only guards count1 <= 0, NOT r7 == NULL
    //     ...loop...
    //     20d548: str r0, [r7, r5, lsl #2]       ; CRASH here when r7 == NULL
    //   Block 2 (same pattern, this+8/+0xc, ends the function -- no more code after it):
    //     20d56c: bl operator_new[]
    //     20d578: mov r7, r0                     ; r7 = array2 (maybe NULL!)
    //     20d580: pople {r4,r5,r6,r7,r8,pc}      ; only guards count2 <= 0
    //     20d584: mov r5, #0
    //     20d590: str r0, [r7, r5, lsl #2]       ; would crash identically if ever reached
    // Fix: two trampolines right after the FileManager::_Load one, in the same dead
    // ALicenseCheck::LoadConfig region (0x003dd0d4 and 0x003dd0f4 -- both still well inside
    // LoadConfig's hooked/unreachable body, which runs through 0x003dd174):
    //   Block 1 -- replace "ble 0x20d55c" at 0x0020d530 with "b 0x003dd0d4":
    //     cmp r7, #0
    //     beq alloc_failed
    //     cmp r3, #0
    //     ble 0x20d55c        ; original behavior (count1 <= 0)
    //     b 0x20d534          ; original behavior (count1 > 0, alloc ok -> run the loop)
    //     alloc_failed:
    //     mov r3, #0
    //     str r3, [r4]        ; count1 = 0, so the caller's "if (0 < count1)" skips safely
    //     b 0x20d55c
    //   Block 2 -- replace "mov r5, #0" at 0x0020d584 with "b 0x003dd0f4":
    //     cmp r7, #0
    //     moveq r3, #0
    //     streq r3, [r4, #8]  ; count2 = 0, for consistency if anything else reads it later
    //     popeq {r4,r5,r6,r7,r8,pc}  ; alloc failed -> return early, same regs as original path
    //     mov r5, #0          ; original instruction we replaced
    //     b 0x20d588          ; continue into the loop normally (alloc ok)
    // Opcodes verified by assembling with arm-vita-eabi-as/-ld -Ttext=<addr>, same discipline as
    // every trampoline above.
    static const uint32_t patch_spritetextureentry_read_trampoline1[] = {
        0xe3570000, // cmp r7, #0
        0x0a000002, // beq 0x3dd0e8 (alloc_failed)
        0xe3530000, // cmp r3, #0
        0xdaf8c11d, // ble 0x20d55c
        0xeaf8c112, // b 0x20d534
        0xe3a03000, // alloc_failed: mov r3, #0
        0xe5843000, // str r3, [r4]
        0xeaf8c119, // b 0x20d55c
    };
    kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x003dd0d4), patch_spritetextureentry_read_trampoline1, sizeof(patch_spritetextureentry_read_trampoline1));

    uint32_t patch_spritetextureentry_read1 = 0xea073ee7; // b 0x003dd0d4
    kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x0020d530), &patch_spritetextureentry_read1, sizeof(uint32_t));

    static const uint32_t patch_spritetextureentry_read_trampoline2[] = {
        0xe3570000, // cmp r7, #0
        0x03a03000, // moveq r3, #0
        0x05843008, // streq r3, [r4, #8]
        0x08bd81f0, // popeq {r4, r5, r6, r7, r8, pc}
        0xe3a05000, // mov r5, #0
        0xeaf8c11e, // b 0x20d588
    };
    kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x003dd0f4), patch_spritetextureentry_read_trampoline2, sizeof(patch_spritetextureentry_read_trampoline2));

    uint32_t patch_spritetextureentry_read2 = 0xea073eda; // b 0x003dd0f4
    kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x0020d584), &patch_spritetextureentry_read2, sizeof(uint32_t));

    l_info("Patched Structs::SpriteTextureEntry::Read unchecked allocation crash (0x0020d530 -> b 0x003dd0d4, 0x0020d584 -> b 0x003dd0f4)");

    // Fix infinite hang in ASprite::Load(int) (0x0020f43c), a SELF-INFLICTED regression from
    // the SpriteTextureEntry::Read fix above. Confirmed with log_20260904_173928.txt: after the
    // fix above made a corrupted/mismatched resource load as "0 entries" instead of crashing,
    // the game got stuck reopening/re-reading the exact same file
    // (data/lowtexture/wt_zhuwang_2lowpt.kot) forever -- LiveArea splash never got past its
    // spinner, no crash, no .psp2dmp (a hang, same signature as the earlier glsl.config saga).
    // Root cause, confirmed in real ARM disassembly of ASprite::Load(int) (0x0020ed7c-0x0020f778,
    // the only place in the whole .so that references resource id 0xfa5 -- verified with
    // `objdump -d | grep 1fa5`, a single hit): this function has a one-time-init guard around
    // populating a static sprite-id map (`s_TexIds`) from ResStream(0xfa5) ->
    // Structs::SpriteTextureEntry::Read(). After processing, it checks whether the map ended up
    // with at least one entry (a static counter incremented once per successful insert, reused
    // as a "did this ever succeed" flag) and, if it's STILL zero, jumps back to the very top of
    // the function to retry the ENTIRE fixed ResStream(0xfa5) load from scratch:
    //   20f42c: ldr r0, [sp, #8]
    //   20f430: add r3, r6, r0
    //   20f434: ldr r3, [r3, #16]     ; r3 = the static "at least one entry ever inserted" flag
    //   20f438: cmp r3, #0
    //   20f43c: beq 0x20efac         ; still empty -> retry the whole fixed load, forever
    //   20f440: b 0x20efd4           ; (unconditional) the real "proceed with this ASprite
    //                                ; instance" continuation, used as-is by the fast path when
    //                                ; the one-time init had already succeeded on an earlier call
    // Since resource id 0xfa5 is a hardcoded constant (not something that varies across retries),
    // a corrupted/mismatched resource for it can never succeed no matter how many times this
    // loops -- there was never a legitimate reason for this retry to run more than once even
    // when everything works, since a single ResStream/DataStream read is deterministic given the
    // same fixed input. Fix: NOP out the "beq 0x20efac" at 0x0020f43c. This does not change
    // behavior at all in the working/success case (the branch was already never taken there,
    // since the map gets populated on the first pass) -- it only removes the infinite-retry path
    // for the case this port's asset set can't actually satisfy, falling through into the
    // already-existing unconditional "proceed anyway" branch right below it (0x20f440) instead
    // of looping. The net effect (combined with the SpriteTextureEntry::Read fix above) is that
    // `s_TexIds` simply stays empty for this port's dataset, same "no fidelity but no crash/hang"
    // trade-off already accepted for other genuinely-missing/mismatched assets this session.
    uint32_t patch_asprite_load_nop = 0xe320f000; // nop
    kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x0020f43c), &patch_asprite_load_nop, sizeof(uint32_t));
    l_info("Patched ASprite::Load infinite retry hang (0x0020f43c -> nop)");
}
