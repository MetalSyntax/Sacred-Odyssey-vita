#include "controls.h"
#include "utils/logger.h"

#include <so_util/so_util.h>
#include <psp2/io/fcntl.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

extern so_module so_mod;

// JNI bindings passed from main
static so_touch_fn s_nativeTouch = NULL;
static so_key_fn   s_nativeKeyDown = NULL;
static so_key_fn   s_nativeKeyUp = NULL;
static JNIEnv     *s_jniEnv = NULL;

// Global pad state accessible across loader
SceCtrlData g_pad;
static uint32_t s_old_buttons = 0;
static uint32_t s_current_buttons = 0;

// Global pointer to Hud::s_pInstance in libsacredodyssey.so
static uintptr_t *s_hud_s_pInstance_ptr = NULL;

// AnimObject::SetAlpha(int) -- __thiscall, `this` in r0 like any other ARM
// AAPCS call. Symbol confirmed present (T, not just a UND JNI stub) via
// `arm-vita-eabi-nm -C --defined-only libsacredodyssey.so` on all three
// extracted copies of the real .so in this repo's working tree, at the same
// mangled name regardless of the exact build's address -- same class of
// lookup this file already relies on for _ZN3Hud11s_pInstanceE and the two
// Get_MovePad_AxisValues hooks. Used to dim (not fully hide) the on-screen
// virtual buttons the physical controls now duplicate: unlike HudWidget's
// own "visible" flag (offset 0x18, HudWidget::SetVisible),
// HudWidget::CollideTouchPoint() only gates hit-testing on offsets 0x18/0x19
// (confirmed directly in its decompiled body) and never reads alpha, so
// dimming via this call keeps real-touch AND our own synthetic taps working
// on these buttons while making them visually unobtrusive.
static void (*s_animobject_set_alpha)(uintptr_t anim_obj, int alpha) = NULL;

// HudWidget::FindWidgetByName(const char*) -- __thiscall, resuelve un widget
// HIJO por nombre buscando dentro del subárbol de `this` (confirmado en el
// pseudo-C: `HudWidget::UpdateTouchInfo` la usa así, `FindWidgetByName((HudWidget*)param_1,
// "button_YesYes")`, buscando SOLO entre los hijos de `param_1`). Símbolo
// confirmado presente (`_ZN9HudWidget16FindWidgetByNameEPKc`) vía `nm -D`
// sobre el .so real. Usado para llegar a los sub-widgets de un widget GRUPO
// (ej. "status_healthGroup") sin adivinar offsets de hijo a mano -- la misma
// clase de error que causó el problema de la "espada" (offset equivocado).
static uintptr_t (*s_hudwidget_find_widget_by_name)(uintptr_t this_widget, const char *name) = NULL;

// 2026-09-19 (usuario): "el joystick derecho no hace nada para mover la
// camara a diferencia del izquierdo". Causa real (confirmada en el pseudo-C
// de Ghidra de libsacredodyssey_v106, no adivinada): el update de la camara
// (funcion decompilada como "Update(int)" sobre lo que resuelve a
// CameraManager, linea ~102416 de out_ghidra.c) NO llama a
// CameraRotatePad::Get_MovePad_AxisValues() todos los frames como si hace
// MoveState::Update() con HudMovePad::Get_MovePad_AxisValues() (ese si es
// incondicional, por eso el stick izquierdo/movimiento SIEMPRE funciono) --
// la camara la llama solo `if (*(char*)(widget + 0x1a) != 0)`, donde
// `widget` es el mismo puntero que HUD_OFFSET_CAMERAPAD ya resuelve desde
// Hud::s_pInstance (confirmado: `*(Hud**)(Gameplay::s_instance+0x28)` se usa
// en decenas de sitios como Hud::AddHudMessage/PushIGM, o sea
// Gameplay::s_instance+0x28 ES Hud::s_pInstance; +8 es exactamente
// HUD_OFFSET_CAMERAPAD). Ese byte en +0x1a es "hay un touch real encima de
// este widget ahora mismo" -- lo pone HudWidget::UpdateTouchInfo() (llamado
// desde CameraRotatePad::UpdateTouchInfo, que es lo que hookeamos abajo) en
// base a HudEngine::GetTouchPointInfo(), y se resetea a 0 cada frame sin un
// dedo real encima. Sin un dedo tocando esa zona de pantalla, el gate nunca
// pasa y nuestro propio hook de Get_MovePad_AxisValues (mas abajo) JAMAS se
// ejecuta para el stick fisico -- de ahi el "no hace nada" (no es un
// problema de sensibilidad/deadzone, la funcion ni se llama).
// Fix: hookear CameraRotatePad::UpdateTouchInfo (NO Get_MovePad_AxisValues,
// que ya esta bien) para, DESPUES de dejar que la logica real de touch siga
// funcionando igual que siempre (se llama a HudWidget::UpdateTouchInfo, la
// clase base, exactamente como el codigo original), forzar ese mismo byte a
// 1 si el stick derecho fisico esta deflectado este frame -- as[i] el gate
// pasa y el motor SI llama a Get_MovePad_AxisValues, que ya sabe leer el
// stick fisico via el "dedo virtual" (s_vcam_cur_x/y mas abajo). No compite
// con el touch real: si hay un dedo real, el byte ya queda en 1 de por si.
static void (*s_hudwidget_update_touch_info)(uintptr_t this_ptr, int dt) = NULL;
static bool s_cam_stick_deflected = false;

// Toggled by the L+R combo (see controls_update) to temporarily bring the
// dimmed virtual HUD back to full opacity.
static bool s_hud_full_opacity = false;

// Touch slot manager (max 5 slots, strictly complying with psvita-porting input_handling reference)
#define MAX_TOUCH_SLOTS 5
static int s_slotHwId[MAX_TOUCH_SLOTS] = {-1, -1, -1, -1, -1};
static int s_lastX[MAX_TOUCH_SLOTS]    = {-1, -1, -1, -1, -1};
static int s_lastY[MAX_TOUCH_SLOTS]    = {-1, -1, -1, -1, -1};

// Structure for internal frame touch reports (hardware touches + synthetic button touches)
#define MAX_REPORTS 16
struct InternalTouchReport {
    int id;
    int x;
    int y;
};

// Offsets inside Hud (from decompiled _ZN3Hud14InitHudWidgetsEv)
enum HudWidgetOffset {
    HUD_OFFSET_MOVEPAD       = 4,
    HUD_OFFSET_CAMERAPAD     = 8,
    HUD_OFFSET_ACTION        = 16,  // button_action
    HUD_OFFSET_ATTACK        = 20,  // button_attack
    HUD_OFFSET_PICK_BOMB     = 24,  // button_pickUpBomb
    HUD_OFFSET_PUSH_BOX      = 28,  // button_pushBox
    HUD_OFFSET_TALK_NPC      = 32,  // button_talkToNPC
    HUD_OFFSET_OPEN_TREASURE = 36,  // button_openTreasure
    HUD_OFFSET_ROTATE_MIRROR = 40,  // button_rotateMirror
    HUD_OFFSET_DEFENSE       = 44,  // button_defense
    HUD_OFFSET_BLOCK         = 48,  // button_block
    HUD_OFFSET_SWITCH_WEAPON = 52,  // button_switchWeapon
    HUD_OFFSET_SWORD         = 56,  // button_sword
    HUD_OFFSET_IRON_EAGLE    = 60,  // button_ironEagle
    HUD_OFFSET_IRON_FIST     = 64,  // button_ironFist
    HUD_OFFSET_IRON_CHAIN    = 68,  // button_ironChain
    HUD_OFFSET_SWITCH_MENU   = 72,  // button_swichWeaponWithMenu
    HUD_OFFSET_TARGET_CROSS  = 76,  // target_cross
    HUD_OFFSET_CUTSCENE      = 80,  // HUD_CutScene
    HUD_OFFSET_CHANGE_HORSE  = 88,  // button_ChangeToHorse
    HUD_OFFSET_CHANGE_RUN    = 92,  // button_ChangeToRun
    HUD_OFFSET_HEALTH_GROUP  = 96,  // status_healthGroup (health bar at x=112, y=24)
    HUD_OFFSET_TUTORIAL_DLG  = 104, // button_HudTurtorialDialogInGame
    HUD_OFFSET_MINI_MAP      = 108, // Mini_Map
    HUD_OFFSET_SYS_IGM       = 112, // button_toSysIGM (pause / system menu button at x=20, y=77)
    HUD_OFFSET_IGM           = 116, // button_toIGM (Ayden circular portrait at x=25, y=22, opens in-game menu)
};

// Guard against dangling/garbage pointers in the Hud widget table.
#define MIN_PLAUSIBLE_PTR 0x10000000u
static bool is_plausible_ptr(uintptr_t p) {
    return p >= MIN_PLAUSIBLE_PTR && (p & 0x3) == 0;
}

static bool is_widget_active(uintptr_t widget, float *out_x, float *out_y) {
    if (!widget || !is_plausible_ptr(widget)) return false;
    // widget + 0x18 is visible flag, widget + 0x19 is active flag in HudWidget
    uint8_t visible = *(uint8_t *)(widget + 0x18);
    uint8_t active  = *(uint8_t *)(widget + 0x19);
    if (!visible && !active) return false;

    // In HudWidget::GetAbsolutePosition:
    // x = widget[0xdc] + widget[0x114], y = widget[0xe0] + widget[0x118]
    float x = *(float *)(widget + 0xdc) + *(float *)(widget + 0x114);
    float y = *(float *)(widget + 0xe0) + *(float *)(widget + 0x118);

    if (isnan(x) || isnan(y) || x < 0.0f || x >= SCREEN_W || y < 0.0f || y >= SCREEN_H) {
        return false;
    }

    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
    return true;
}

static bool get_widget_pos(enum HudWidgetOffset offset, float *out_x, float *out_y) {
    if (!s_hud_s_pInstance_ptr || !*s_hud_s_pInstance_ptr) return false;
    uintptr_t hud = *s_hud_s_pInstance_ptr;
    if (!hud || !is_plausible_ptr(hud)) return false;
    uintptr_t widget = *(uintptr_t *)(hud + offset);
    return is_widget_active(widget, out_x, out_y);
}

// Dim a HUD widget's sprite alpha WITHOUT touching its visible/active flags,
// so it stays fully touch-functional (see s_animobject_set_alpha's comment
// above) while becoming visually unobtrusive now that physical controls
// duplicate it. HudWidget::Activate/DeActivate (decompiled) confirm offset
// +4 holds the widget's AnimObject* (`*(AnimObject **)(in_r0 + 4)`).
//
// IMPORTANT correction (user feedback, 2026-09-17): touching the on-screen
// movement joystick was ALREADY confirmed non-functional before ANY of this
// hide/dim code existed (see the 2026-09-16 session that first added
// hide_widget() for HUD_OFFSET_MOVEPAD -- "el usuario confirmó que tocarlo en
// pantalla no hace nada"). So for the joystick specifically, switching from
// hide (visible=0) to dim (alpha-only) does NOT "restore" or "preserve" any
// working touch behavior -- there wasn't any to preserve. The alpha approach
// still matters for the ACTION buttons (attack/shield/horse/map/menu), whose
// touch DOES matter (it's the exact mechanism the physical-button synthetic
// taps rely on to register a hit). Don't assume dim_widget "fixed" anything
// for the joystick's touch -- it didn't need fixing, it was never there.
//
// Also: never independently confirmed on-console that HUD_OFFSET_MOVEPAD's
// widget/AnimObject pointers actually resolve (vs. silently no-op'ing every
// frame via the is_plausible_ptr guards below) -- logged once per offset the
// first time this runs so the next log settles it instead of assuming.
// 2026-09-19 (usuario): "si puedes hazlos mas claros" -- 18 (~7%) dejaba los
// iconos atenuados casi invisibles; subido a ~16% para que sigan de fondo
// (los controles fisicos los duplican) pero se puedan distinguir a simple
// vista en vez de parecer apagados del todo.
// 2026-09-20 (usuario, log_20260920_142756.txt): "la opacidad no esta al 1%"
// -- la sesion anterior interpreto mal el pedido como "+1 punto porcentual"
// (40 -> 43, ~16% -> ~17%). El pedido real es que el valor EN REPOSO sea
// ~1% (casi invisible), y el L+R de mas abajo la lleve a 100% como
// alternativa. 3/255 = ~1.2%, el entero mas cercano a 1% sin ser 0 (0
// dejaria el sprite totalmente invisible; mantenemos un resto minimo
// perceptible/depurable en pantalla).
#define DIM_ALPHA 3 // ~1% opacity (0-255 scale, confirmed via ASprite::SetAlpha/AnimObject::SetAlpha call sites using 0/0xff as the full range)
static int8_t s_dim_logged[128] = {0}; // 0=not yet logged, 1=logged-resolved, 2=logged-unresolved
static uintptr_t s_dim_logged_hud = 0; // Hud instance the latch above belongs to (see below)
static void dim_widget(enum HudWidgetOffset offset, int alpha) {
    bool resolved = false;
    uintptr_t anim = 0;

    if (s_hud_s_pInstance_ptr && *s_hud_s_pInstance_ptr) {
        uintptr_t hud = *s_hud_s_pInstance_ptr;
        // 2026-09-20 (log_20260920_170443.txt): el motor reconstruye la tabla
        // de widgets al cambiar de estado (Splash/Menu/Loading/Mission) -- el
        // latch de s_dim_logged quedaba fijado con el layout del PRIMER Hud
        // (casi todo "NOT resolved" en splash) y nunca re-reportaba en
        // mision, ocultando que layout corre en gameplay real. Se resetea el
        // latch cada vez que cambia la instancia de Hud (solo el log; el
        // alpha se sigue aplicando cada frame igual que antes).
        if (hud != s_dim_logged_hud) {
            memset(s_dim_logged, 0, sizeof(s_dim_logged));
            s_dim_logged_hud = hud;
        }
        if (hud && is_plausible_ptr(hud)) {
            uintptr_t widget = *(uintptr_t *)(hud + offset);
            if (widget && is_plausible_ptr(widget)) {
                anim = *(uintptr_t *)(widget + 4);
                resolved = anim && is_plausible_ptr(anim);
            }
        }
    }

    int idx = (int)offset;
    if (idx >= 0 && idx < (int)(sizeof(s_dim_logged) / sizeof(s_dim_logged[0])) && s_dim_logged[idx] == 0) {
        s_dim_logged[idx] = resolved ? 1 : 2;
        l_info("[controls] dim_widget(offset=%d): %s", idx,
               resolved ? "widget/AnimObject resolved, alpha applied" : "NOT resolved -- stays at default opacity");
    }

    if (resolved && s_animobject_set_alpha) {
        s_animobject_set_alpha(anim, alpha);
    }
}

// 2026-09-19 (usuario, log_20260919_211800.txt): "veo el menu, pero falta el
// personaje sobre el menu y el mini mapa de la derecha y la espada sobre el
// mini mapa" -- forzar alpha=255 (dim_widget arriba) para
// HEALTH_GROUP/MINI_MAP/SWORD/SYS_IGM no bastó: solo SYS_IGM (menú) se ve.
// dim_widget() nunca toca "visible"/"active" (offsets +0x18/+0x19,
// confirmados en is_widget_active() mas abajo) a propósito -- para no
// interferir con el estado propio del motor. Pero eso significa que si el
// motor decide, por lo que sea (progreso de tutorial, estado del nivel...),
// que un widget está con visible=0, ningún alpha lo va a hacer aparecer. El
// menú SÍ se ve porque probablemente arranca visible=1 por defecto; los
// otros tres no. Fix: para estos 4 widgets ESPECÍFICOS (los únicos que el
// usuario pidió mostrar siempre) forzar visible=1 Y active=1 ademas del
// alpha, cada frame, para garantizar que se dibujen sin importar en que
// estado los deje el motor. NO se aplica al resto del HUD (dim_widget solo
// alpha, como hasta ahora) -- son casos puntuales pedidos explícitamente.
// Returns the resolved widget pointer (or 0) so callers like
// force_health_portrait_shown() below can reach into its children without
// re-resolving Hud::s_pInstance a second time.
static uintptr_t force_widget_shown(enum HudWidgetOffset offset) {
    if (!s_hud_s_pInstance_ptr || !*s_hud_s_pInstance_ptr) return 0;
    uintptr_t hud = *s_hud_s_pInstance_ptr;
    if (!hud || !is_plausible_ptr(hud)) return 0;
    uintptr_t widget = *(uintptr_t *)(hud + offset);
    if (!widget || !is_plausible_ptr(widget)) return 0;
    *(uint8_t *)(widget + 0x18) = 1; // visible
    *(uint8_t *)(widget + 0x19) = 1; // active
    dim_widget(offset, 255);
    return widget;
}

// 2026-09-20 (usuario, log_20260920_005427.txt): "El personaje al lado de la
// vida y sobre el boton del menu sigue oculto" -- pese a force_widget_shown()
// en HUD_OFFSET_HEALTH_GROUP (status_healthGroup). Causa probable: el string
// dump de data/menus/HUDs/Hud.array (`strings` sobre el .array real) no trae
// ningún widget "portrait"/"face"/"avatar" -- "status_healthGroup" es un
// widget GRUPO cuyos hijos reales son "health_bg" (el marco/retrato de
// fondo -- lo más probable candidato al "personaje" que describe el
// usuario) y "health" (la barra en sí). Mismo patrón que el joystick
// (base + knob hijo en +0x160), pero acá se resuelve por NOMBRE vía
// HudWidget::FindWidgetByName en vez de adivinar un offset de hijo a mano
// -- la lección de la "espada" (offset equivocado) de la sesión anterior.
// 2026-09-20 (usuario, log_20260920_142756.txt): "el icono del personaje
// sigue con la opacidad baja" -- persiste pese a force_named_child_shown().
// El log confirmado (dim_widget) muestra que HEALTH_GROUP (offset 96) nunca
// resuelve un AnimObject propio -- esperable, es un widget GRUPO sin sprite
// propio -- pero eso NO dice nada sobre si el HIJO "health_bg" se encuentra
// y se fuerza. No había ningún log en esta función; sin eso, cualquier
// arreglo siguiente vuelve a ser adivinar (misma trampa que la "espada" de
// offset equivocado). Se agrega logging una sola vez por nombre, igual
// patron que s_dim_logged, para que el próximo log real confirme si
// FindWidgetByName encuentra "health_bg"/"health" o no -- de eso depende el
// siguiente paso (nombre incorrecto vs. otra causa).
static int8_t s_named_child_logged[4] = {0};
static uintptr_t s_named_child_logged_parent = 0; // parent the latch belongs to (reset per Hud rebuild, igual que s_dim_logged)
static void force_named_child_shown(uintptr_t parent_widget, const char *name) {
    if (!parent_widget || !is_plausible_ptr(parent_widget) || !s_hudwidget_find_widget_by_name) return;
    if (parent_widget != s_named_child_logged_parent) {
        memset(s_named_child_logged, 0, sizeof(s_named_child_logged));
        s_named_child_logged_parent = parent_widget;
    }
    uintptr_t child = s_hudwidget_find_widget_by_name(parent_widget, name);

    int idx = (strcmp(name, "health_bg") == 0) ? 0 : 1;
    if (idx >= 0 && idx < (int)(sizeof(s_named_child_logged) / sizeof(s_named_child_logged[0])) && s_named_child_logged[idx] == 0) {
        s_named_child_logged[idx] = 1;
        bool resolved = child && is_plausible_ptr(child);
        l_info("[controls] force_named_child_shown(name=%s): %s", name, resolved ? "found, visible+active+alpha forced" : "NOT FOUND -- FindWidgetByName returned nothing");
    }

    if (!child || !is_plausible_ptr(child)) return;
    *(uint8_t *)(child + 0x18) = 1; // visible
    *(uint8_t *)(child + 0x19) = 1; // active
    uintptr_t anim = *(uintptr_t *)(child + 4);
    if (anim && is_plausible_ptr(anim) && s_animobject_set_alpha) {
        s_animobject_set_alpha(anim, 255);
    }
}

// The movement joystick graphic is TWO widgets: the outer base (HudWidget at
// Hud+HUD_OFFSET_MOVEPAD, what dim_widget() above targets) and a separate
// knob CHILD at base+0x160 (confirmed real, already read from in
// hook_HudMovePad_Get_MovePad_AxisValues below for its position). Dimming
// only the base left the knob at full opacity -- reported as "el joystick
// sigue visible" -- so both need their own AnimObject dimmed independently.
static void dim_movepad(int alpha) {
    if (!s_hud_s_pInstance_ptr || !*s_hud_s_pInstance_ptr) return;
    uintptr_t hud = *s_hud_s_pInstance_ptr;
    if (!hud || !is_plausible_ptr(hud)) return;
    uintptr_t base = *(uintptr_t *)(hud + HUD_OFFSET_MOVEPAD);
    if (!base || !is_plausible_ptr(base)) return;

    if (s_animobject_set_alpha) {
        uintptr_t base_anim = *(uintptr_t *)(base + 4);
        if (base_anim && is_plausible_ptr(base_anim)) s_animobject_set_alpha(base_anim, alpha);

        uintptr_t knob = *(uintptr_t *)(base + 0x160);
        if (knob && is_plausible_ptr(knob)) {
            uintptr_t knob_anim = *(uintptr_t *)(knob + 4);
            if (knob_anim && is_plausible_ptr(knob_anim)) s_animobject_set_alpha(knob_anim, alpha);
        }
    }
}

// 2026-09-20 (log_20260920_170443.txt): "sigue sin aparecer el retrato a
// menos que haga aparecer todos los controles" (L+R). Ese log prueba que
// health_bg/health SI se encuentran y se fuerzan (visible+active+alpha 255)
// cada frame... y aun asi el retrato no se ve en reposo. Pero L+R SOLO
// cambia el alpha de la lista atenuada (dim_widget con hud_alpha) -- lista
// en la que health_bg/health NO estan. Conclusion: la opacidad final del
// retrato depende de un widget DE LA LISTA ATENUADA -- o el mapa
// offset->nombre (verificado contra UN solo layout de InitHudWidgets) no
// vale en el Hud de mision y algun slot atenuado contiene arte del
// retrato, o dos slots comparten el mismo AnimObject (atenuar uno atenua
// ambos). En vez de adivinar cual, se vuelca la tabla COMPLETA (32 slots:
// puntero, visible, active, posicion con la misma formula de
// is_widget_active, y AnimObject) una vez por cada instancia distinta de
// Hud -- con eso el proximo log muestra de frente que slot esta en la
// posicion del retrato (arriba-izquierda) y si algun AnimObject esta
// compartido. Solo diagnostico (l_info, debug-gated); no toca estado.
static uintptr_t s_dumped_hud[8] = {0};
static void dump_hud_table(uintptr_t hud) {
    if (!hud || !is_plausible_ptr(hud)) return;
    for (int i = 0; i < 8; i++) {
        if (s_dumped_hud[i] == hud) return; // ya volcado
    }
    for (int i = 0; i < 7; i++) s_dumped_hud[i] = s_dumped_hud[i + 1];
    s_dumped_hud[7] = hud;
    l_info("[controls] hud_table dump (instance=0x%08x):", (unsigned int)hud);
    for (int off = 0; off < 128; off += 4) {
        uintptr_t widget = *(uintptr_t *)(hud + off);
        if (!widget || !is_plausible_ptr(widget)) {
            l_info("[controls] hud_table: off=%d empty", off);
            continue;
        }
        uint8_t vis = *(uint8_t *)(widget + 0x18);
        uint8_t act = *(uint8_t *)(widget + 0x19);
        float x = *(float *)(widget + 0xdc) + *(float *)(widget + 0x114);
        float y = *(float *)(widget + 0xe0) + *(float *)(widget + 0x118);
        uintptr_t anim = *(uintptr_t *)(widget + 4);
        if (!anim || !is_plausible_ptr(anim)) anim = 0;
        l_info("[controls] hud_table: off=%d ptr=0x%08x vis=%u act=%u x=%.0f y=%.0f anim=0x%08x",
               off, (unsigned int)widget, vis, act, x, y, (unsigned int)anim);
    }
}

// Hook for HudMovePad::Get_MovePad_AxisValues (Left Analog Stick & D-Pad)
static void hook_HudMovePad_Get_MovePad_AxisValues(float *out, uintptr_t this_ptr) {
    out[0] = 0.0f;
    out[1] = 0.0f;

    // 1. Check virtual on-screen touch movepad first
    if (this_ptr && *(uint8_t *)(this_ptr + 25) != 0) { // 0x19
        uintptr_t child = *(uintptr_t *)(this_ptr + 352); // 0x160 (knob child)
        if (child && *(uint8_t *)(child + 25) != 0) {
            if (*(uint8_t *)(child + 28) == 0 || *(uint8_t *)(child + 26) != 0) {
                float child_x = *(float *)(child + 220); // 0xdc
                float child_y = *(float *)(child + 224); // 0xe0
                float len = sqrtf(child_x * child_x + child_y * child_y);
                float deadzone = *(float *)(this_ptr + 360); // 0x168
                float max_r = *(float *)(this_ptr + 364);    // 0x16c
                float diff = len - deadzone;
                if (diff < 0.0f) diff = 0.0f;
                if (diff > max_r) diff = max_r;
                float range = max_r - deadzone;
                if (range > 0.001f && len > 0.001f) {
                    out[0] = (diff * child_x) / (range * len);
                    out[1] = (diff * child_y) / (range * len);
                }
            }
        }
    }

    // 2. Physical Left Analog Stick
    float stick_x = (g_pad.lx - 128) / 128.0f;
    float stick_y = (g_pad.ly - 128) / 128.0f;
    float stick_len = sqrtf(stick_x * stick_x + stick_y * stick_y);
    const float deadzone = 0.18f;

    if (stick_len > deadzone) {
        float norm = (stick_len - deadzone) / (1.0f - deadzone);
        if (norm > 1.0f) norm = 1.0f;
        out[0] = (stick_x / stick_len) * norm;
        out[1] = (stick_y / stick_len) * norm;
    }

    // D-Pad override/fallback
    if (g_pad.buttons & SCE_CTRL_LEFT)  out[0] = -1.0f;
    if (g_pad.buttons & SCE_CTRL_RIGHT) out[0] =  1.0f;
    if (g_pad.buttons & SCE_CTRL_UP)    out[1] = -1.0f;
    if (g_pad.buttons & SCE_CTRL_DOWN)  out[1] =  1.0f;
}

// Right Analog Stick Camera Look (calibrated with NOVA 2 & Shadow Guardian)
//
// Root Cause of violent snap and freezing:
// 1. In CameraManager::Update, when the widget is not touched (*(char*)(widget + 0x1a) == 0),
//    the engine saves lastCamDir = currentCamDir and lastHeight = currentHeight.
// 2. Previously, s_vcam_cur_x/y was only reset in the else branch of this hook, but this hook
//    is ONLY called by the engine when *(char*)(widget + 0x1a) != 0! When the user centered
//    the stick, this hook was never called, leaving s_vcam_cur_x frozen at +/-500.0f.
//    The moment the user touched the stick again, out[0] was immediately +/-500.0f on frame 1,
//    causing the engine (which divides out[0] by 270.0f and multiplies by PI) to rotate lastCamDir
//    by 333° instantly -- creating a violent snap/jump!
// 3. Clamping s_vcam_cur_x to [-500, 500] also prevented continuous 360° rotation.
//
// Fix:
// - Camera accumulation and centering reset are moved to controls_update(), running unconditionally
//   every frame. In repose, s_vcam_cur_x/y are immediately 0.0f.
// - Quadratic deadzone curve (0.15f deadzone + 0.35*t + 0.65*t^2) matching Shadow Guardian / NOVA 2
//   for ultra-smooth, analog camera control.
// - Horizontal accumulation wraps at +/-1080.0f (exact 4*PI period of quaternion fromAngleAxis),
//   enabling continuous 360° rotation forever with zero discontinuity.
#define CAM_LOOK_DZ       0.15f
#define CAM_SPEED_X       4.5f
#define CAM_SPEED_Y       1.8f
#define CAM_LEVEL_DEFAULT 5
#define CAM_LEVEL_MAX     10
#define CAM_CFG           DATA_PATH "camera_sens.txt"

static int s_cam_level = CAM_LEVEL_DEFAULT;
static float s_vcam_cur_x = 0.0f;
static float s_vcam_cur_y = 0.0f;

static void cam_load_config(void) {
    SceUID fd = sceIoOpen(CAM_CFG, SCE_O_RDONLY, 0);
    if (fd < 0) return;
    char b[8] = { 0 };
    sceIoRead(fd, b, sizeof(b) - 1);
    sceIoClose(fd);
    int v = atoi(b);
    if (v >= 1 && v <= CAM_LEVEL_MAX) {
        s_cam_level = v;
        l_info("[cam] Loaded camera sensitivity %d/%d from %s", s_cam_level, CAM_LEVEL_MAX, CAM_CFG);
    }
}

static inline float cam_look_curve(float v) {
    float a = v < 0.0f ? -v : v;
    if (a <= CAM_LOOK_DZ)
        return 0.0f;
    float t = (a - CAM_LOOK_DZ) / (1.0f - CAM_LOOK_DZ);
    if (t > 1.0f)
        t = 1.0f;
    t = 0.35f * t + 0.65f * t * t;
    return v < 0.0f ? -t : t;
}

static void hook_CameraRotatePad_Get_MovePad_AxisValues(float *out, uintptr_t this_ptr) {
    out[0] = 0.0f;
    out[1] = 0.0f;

    // 1. Right analog stick (takes priority when deflected)
    if (s_cam_stick_deflected) {
        out[0] = s_vcam_cur_x;
        out[1] = s_vcam_cur_y;
        return;
    }

    // 2. Real touch camera rotation fallback (screen dragging)
    if (this_ptr && *(uint8_t *)(this_ptr + 25) != 0) { // 0x19
        uintptr_t touch_info = *(uintptr_t *)(this_ptr + 296); // 0x128
        if (touch_info) {
            short cur_x = *(short *)(touch_info + 4);
            short cur_y = *(short *)(touch_info + 6);
            float start_x = *(float *)(this_ptr + 252); // 0xfc
            float start_y = *(float *)(this_ptr + 256); // 0x100
            out[0] = (float)cur_x - start_x;
            out[1] = (float)cur_y - start_y;
        }
    }
}

// Hook for CameraRotatePad::UpdateTouchInfo(int) -- ver el comentario largo
// junto a s_hudwidget_update_touch_info mas arriba para la causa raiz. Llama
// PRIMERO a la implementacion real (mismo orden que el codigo original:
// acumular el timer de doble-tap propio de CameraRotatePad y despues
// delegar a HudWidget::UpdateTouchInfo, la clase base, que es quien de
// verdad calcula +0x1a a partir del touch real), y SOLO DESPUES pisa +0x1a
// a 1 si el stick fisico esta deflectado -- nunca antes, para no pisar ni
// competir con el resultado real del touch en el mismo frame.
static void hook_CameraRotatePad_UpdateTouchInfo(uintptr_t this_ptr, int dt) {
    if (!this_ptr || !is_plausible_ptr(this_ptr)) return;

    *(int *)(this_ptr + 0x174) += dt; // double-tap timer, igual que el original

    if (s_hudwidget_update_touch_info) {
        s_hudwidget_update_touch_info(this_ptr, dt);
    }

    if (s_cam_stick_deflected) {
        *(uint8_t *)(this_ptr + 0x1a) = 1;
    }
}

void controls_init(so_touch_fn touch_fn, so_key_fn key_down_fn, so_key_fn key_up_fn, JNIEnv *jni_env) {
    s_nativeTouch   = touch_fn;
    s_nativeKeyDown = key_down_fn;
    s_nativeKeyUp   = key_up_fn;
    s_jniEnv        = jni_env;

    memset(&g_pad, 0, sizeof(g_pad));
    g_pad.lx = 128;
    g_pad.ly = 128;
    g_pad.rx = 128;
    g_pad.ry = 128;

    cam_load_config();

    // Hook Left Analog Stick / Movement Pad
    uintptr_t movepad_sym = so_symbol(&so_mod, "_ZN10HudMovePad22Get_MovePad_AxisValuesEv");
    if (movepad_sym) {
        hook_addr(movepad_sym, (uintptr_t)&hook_HudMovePad_Get_MovePad_AxisValues);
        l_info("Hooked HudMovePad::Get_MovePad_AxisValues successfully");
    } else {
        l_warn("Could not find HudMovePad::Get_MovePad_AxisValues to hook");
    }

    // Hook Right Analog Stick / Camera Rotation Pad
    uintptr_t camerapad_sym = so_symbol(&so_mod, "_ZN15CameraRotatePad22Get_MovePad_AxisValuesEv");
    if (camerapad_sym) {
        hook_addr(camerapad_sym, (uintptr_t)&hook_CameraRotatePad_Get_MovePad_AxisValues);
        l_info("Hooked CameraRotatePad::Get_MovePad_AxisValues successfully");
    } else {
        l_warn("Could not find CameraRotatePad::Get_MovePad_AxisValues to hook");
    }

    // 2026-09-19: sin esto el stick derecho fisico no hacia NADA -- ver el
    // comentario largo junto a s_hudwidget_update_touch_info. Resolver la
    // base class (llamada, no hookeada) y hookear el override de
    // CameraRotatePad para forzar el gate de "touch activo" que el update
    // de la camara chequea antes de llamar a Get_MovePad_AxisValues.
    uintptr_t hudwidget_touch_sym = so_symbol(&so_mod, "_ZN9HudWidget15UpdateTouchInfoEi");
    if (hudwidget_touch_sym) {
        s_hudwidget_update_touch_info = (void (*)(uintptr_t, int))hudwidget_touch_sym;
        l_info("Resolved HudWidget::UpdateTouchInfo successfully");
    } else {
        l_warn("Could not resolve HudWidget::UpdateTouchInfo -- right stick camera fix disabled");
    }

    uintptr_t camerapad_touch_sym = so_symbol(&so_mod, "_ZN15CameraRotatePad15UpdateTouchInfoEi");
    if (camerapad_touch_sym) {
        hook_addr(camerapad_touch_sym, (uintptr_t)&hook_CameraRotatePad_UpdateTouchInfo);
        l_info("Hooked CameraRotatePad::UpdateTouchInfo successfully");
    } else {
        l_warn("Could not find CameraRotatePad::UpdateTouchInfo to hook -- right stick camera fix disabled");
    }

    // Resolve pointer to Hud::s_pInstance
    s_hud_s_pInstance_ptr = (uintptr_t *)so_symbol(&so_mod, "_ZN3Hud11s_pInstanceE");
    if (s_hud_s_pInstance_ptr) {
        l_info("Resolved Hud::s_pInstance symbol successfully");
    } else {
        l_warn("Could not resolve Hud::s_pInstance");
    }

    // Resolve HudWidget::FindWidgetByName(const char*) for force_named_child_shown().
    uintptr_t find_child_sym = so_symbol(&so_mod, "_ZN9HudWidget16FindWidgetByNameEPKc");
    if (find_child_sym) {
        s_hudwidget_find_widget_by_name = (uintptr_t (*)(uintptr_t, const char *))find_child_sym;
        l_info("Resolved HudWidget::FindWidgetByName successfully");
    } else {
        l_warn("Could not resolve HudWidget::FindWidgetByName -- health portrait fix disabled");
    }

    // Resolve AnimObject::SetAlpha(int) for dim_widget() (see its comment).
    uintptr_t set_alpha_sym = so_symbol(&so_mod, "_ZN10AnimObject8SetAlphaEi");
    if (set_alpha_sym) {
        s_animobject_set_alpha = (void (*)(uintptr_t, int))set_alpha_sym;
        l_info("Resolved AnimObject::SetAlpha successfully");
    } else {
        l_warn("Could not resolve AnimObject::SetAlpha -- virtual buttons will stay fully opaque");
    }
}

void controls_update(void) {
    if (sceCtrlPeekBufferPositive(0, &g_pad, 1) <= 0) {
        return;
    }

    s_old_buttons = s_current_buttons;
    s_current_buttons = g_pad.buttons;
    uint32_t pressed = s_current_buttons & ~s_old_buttons;
    uint32_t released = ~s_current_buttons & s_old_buttons;

    // Right Analog Stick Camera Look (calibrated with NOVA 2 & Shadow Guardian):
    // Controls camera rotation smoothly without snapping or freezing.
    // When deflected, integrates angular velocity into virtual coordinates.
    // When released, resets to 0 so the next movement begins smoothly from the current camera view.
    {
        float rx = (g_pad.rx - 128) / 128.0f;
        float ry = (g_pad.ry - 128) / 128.0f;
        float k = (float)s_cam_level / (float)CAM_LEVEL_DEFAULT;
        float vx = cam_look_curve(rx) * CAM_SPEED_X * k;
        float vy = cam_look_curve(ry) * CAM_SPEED_Y * k;

        if (vx != 0.0f || vy != 0.0f) {
            s_cam_stick_deflected = true;
            s_vcam_cur_x += vx;
            s_vcam_cur_y += vy;

            // Continuous horizontal wrap at 4*PI (1080.0f units in Sacred Odyssey engine space)
            while (s_vcam_cur_x > 1080.0f)  s_vcam_cur_x -= 1080.0f;
            while (s_vcam_cur_x < -1080.0f) s_vcam_cur_x += 1080.0f;

            // Vertical pitch/height clamp
            if (s_vcam_cur_y > 150.0f)  s_vcam_cur_y = 150.0f;
            if (s_vcam_cur_y < -150.0f) s_vcam_cur_y = -150.0f;
        } else {
            s_cam_stick_deflected = false;
            s_vcam_cur_x = 0.0f;
            s_vcam_cur_y = 0.0f;
        }
    }

    // Volcado diagnostico de la tabla de widgets (una vez por instancia
    // de Hud -- ver dump_hud_table()): con que instancia correr no importa,
    // la funcion misma filtra repetidos.
    if (s_hud_s_pInstance_ptr && *s_hud_s_pInstance_ptr) {
        dump_hud_table(*s_hud_s_pInstance_ptr);
    }

    // L+R combo (both bumpers held together, edge-triggered so holding them
    // doesn't flicker every frame) toggles the dimmed virtual HUD back to
    // full opacity -- user-requested way to see (or touch-tap directly) the
    // joystick and the remaining dimmed action buttons (attack/shield/horse/
    // switch-weapon/bomb/box/npc/treasure/mirror/Iron*/tutorial). Does NOT
    // affect status_healthGroup/Mini_Map/button_toSysIGM/button_sword (menu
    // icon + top-right sword) -- those are forced to full opacity every
    // frame regardless of this toggle (2026-09-19, ver mas abajo). Also
    // fires the L-Trigger/R-Trigger shield+horse taps simultaneously (both
    // are still physical buttons in their own right) -- accepted trade-off
    // of reusing L+R for this secondary function.
    {
        bool lr_now = (g_pad.buttons & SCE_CTRL_LTRIGGER) && (g_pad.buttons & SCE_CTRL_RTRIGGER);
        bool lr_before = (s_old_buttons & SCE_CTRL_LTRIGGER) && (s_old_buttons & SCE_CTRL_RTRIGGER);
        if (lr_now && !lr_before) {
            s_hud_full_opacity = !s_hud_full_opacity;
            l_info("[controls] L+R combo -> virtual HUD %s", s_hud_full_opacity ? "revealed (full opacity)" : "dimmed again");
        }
    }
    int hud_alpha = s_hud_full_opacity ? 255 : DIM_ALPHA;

    // Physical controls now duplicate every on-screen virtual button
    // (movement joystick + the action icons tapped synthetically below), so
    // dim them every frame to declutter the HUD -- via alpha (dim_widget),
    // NOT via the "visible" flag (hide_widget), so real touch and our own
    // synthetic taps below both keep working (see dim_widget()'s comment).
    dim_movepad(hud_alpha);
    dim_widget(HUD_OFFSET_CAMERAPAD, hud_alpha);
    dim_widget(HUD_OFFSET_ATTACK, hud_alpha);
    dim_widget(HUD_OFFSET_DEFENSE, hud_alpha);
    dim_widget(HUD_OFFSET_BLOCK, hud_alpha);
    dim_widget(HUD_OFFSET_CHANGE_HORSE, hud_alpha);
    dim_widget(HUD_OFFSET_CHANGE_RUN, hud_alpha);
    dim_widget(HUD_OFFSET_TARGET_CROSS, hud_alpha);
    // 2026-09-20: HUD_OFFSET_IGM (offset 116, button_toIGM) removido de esta
    // lista atenuada -- ver explicacion detallada mas abajo junto a
    // force_widget_shown(HUD_OFFSET_IGM). Era el motivo exacto por el cual
    // el retrato de Ayden quedaba a ~1% de opacidad (invisible) en reposo.
    // 2026-09-20 (usuario, log_20260920_000922.txt): "agregaste una espada...
    // no era agregarla sino ponerle la opacidad completa" -- HUD_OFFSET_SWORD
    // (offset 56, "button_sword") resultó ser un widget que el motor NO
    // muestra en gameplay normal; forzarlo visible+active lo hacía aparecer
    // como un ícono ajeno/nuevo, no como el ícono de espada tenue que el
    // usuario ya veía. Ese ícono real es button_switchWeapon (offset 52, el
    // que de verdad se renderiza -- ya se usa para el tap sintético de
    // SELECT más abajo), agrupado con SWORD como "cluster derecho" en
    // comentarios de sesiones viejas sin verificar cuál de los dos era. Se
    // revierte: SWORD vuelve a la lista atenuada de abajo (el motor
    // igualmente no lo muestra, así que atenuarlo no cambia nada visible) y
    // SWITCH_WEAPON pasa a opacidad completa (ver más abajo) -- solo alpha,
    // sin tocar visible/active, tal como pidió el usuario.
    dim_widget(HUD_OFFSET_SWORD, hud_alpha);
    // 2026-09-19 (usuario): iconos contextuales inferiores (bomba/caja/NPC/
    // tesoro/espejo/especiales Iron*/tutorial) nunca estaban en esta lista --
    // se mostraban a la opacidad default del motor, inconsistente con "el
    // resto" que el usuario pide mantener discreto. Se atenuan igual que el
    // resto del HUD inferior (el toque real sigue funcionando, dim_widget
    // solo tocá alpha, no el flag visible/active).
    dim_widget(HUD_OFFSET_PICK_BOMB, hud_alpha);
    dim_widget(HUD_OFFSET_PUSH_BOX, hud_alpha);
    dim_widget(HUD_OFFSET_TALK_NPC, hud_alpha);
    dim_widget(HUD_OFFSET_OPEN_TREASURE, hud_alpha);
    dim_widget(HUD_OFFSET_ROTATE_MIRROR, hud_alpha);
    dim_widget(HUD_OFFSET_IRON_EAGLE, hud_alpha);
    dim_widget(HUD_OFFSET_IRON_FIST, hud_alpha);
    dim_widget(HUD_OFFSET_IRON_CHAIN, hud_alpha);
    dim_widget(HUD_OFFSET_TUTORIAL_DLG, hud_alpha);
    // 2026-09-18 (usuario): los elementos SUPERIORES -- retrato/vida del
    // personaje (status_healthGroup, arriba-izquierda) y Mini_Map
    // (arriba-derecha) -- deben mostrarse siempre a opacidad completa, no
    // atenuarse con el resto del HUD. Se restauran a 255 cada frame (tambien
    // pisa cualquier dimming propio del motor).
    // 2026-09-19 (usuario): "mostrar el icono del menu ... y la espada
    // superior derecha" -- se suman button_toSysIGM (icono de menu, cluster
    // izquierdo) y button_sword (espada, cluster derecho) a la lista de
    // opacidad completa; salen de la lista dim de arriba.
    // 2026-09-19 (segunda vuelta, usuario): "veo el menu, pero falta el
    // personaje... el mini mapa" -- forzar solo alpha (255) no bastaba
    // porque el motor tenia estos widgets con visible=0 (el menu arrancaba
    // visible=1 de por si, por eso ese SI se veia). Ahora se fuerza
    // visible+active+alpha (ver force_widget_shown()).
    uintptr_t health_group_widget = force_widget_shown(HUD_OFFSET_HEALTH_GROUP);
    // 2026-09-20 (usuario): "el personaje al lado de la vida... sigue
    // oculto" -- status_healthGroup es un widget GRUPO; su propio alpha no
    // alcanza a los hijos reales "health_bg" (el retrato/marco -- candidato
    // al "personaje") y "health" (la barra), resueltos por nombre dentro de
    // este grupo especifico (ver force_named_child_shown()).
    force_named_child_shown(health_group_widget, "health_bg");
    force_named_child_shown(health_group_widget, "health");
    force_widget_shown(HUD_OFFSET_MINI_MAP);
    force_widget_shown(HUD_OFFSET_SYS_IGM);
    // 2026-09-20 (usuario, log_20260920_172216.txt): "el icono al lado de la
    // salud sigue invisible" -- el volcado diagnostico de hud_table en ese
    // log (instancia 0x89e5d5f8) probo definitivamente que en mision real:
    //   - status_healthGroup (offset 96) esta en x=112, y=24 (barra de vida).
    //   - button_toSysIGM (offset 112) esta en x=20, y=77 (boton de pausa).
    //   - button_toIGM (offset 116) esta en x=25, y=22 con AnimObject 0x8a5aff60.
    // La posicion (25, 22) es exactamente el retrato circular de Ayden al
    // lado de la vida (112, 24) y sobre el boton de pausa (20, 77). En
    // versiones previas estaba en la lista de dim_widget(..., hud_alpha),
    // por lo que cada frame se atenuaba a DIM_ALPHA (3, ~1% de opacidad),
    // haciendolo invisible a menos que se presionara L+R. Forzarlo visible,
    // activo y a alpha=255 aqui resuelve definitivamente su visibilidad en
    // reposo, tal como los otros widgets permanentes del HUD.
    force_widget_shown(HUD_OFFSET_IGM);
    // 2026-09-20 (usuario): la espada real (button_switchWeapon, ver el
    // comentario junto a HUD_OFFSET_SWORD mas arriba) SI se renderiza en
    // gameplay normal (por eso alpha=255 solo alcanza, sin necesitar
    // force_widget_shown/visible/active como los de arriba).
    dim_widget(HUD_OFFSET_SWITCH_WEAPON, 255);

    // Menu Key Events:
    // START -> KEYCODE_BACK (4) / Menu
    if (pressed & SCE_CTRL_START) {
        if (s_nativeKeyDown) s_nativeKeyDown(s_jniEnv, NULL, 4);
    }
    if (released & SCE_CTRL_START) {
        if (s_nativeKeyUp) s_nativeKeyUp(s_jniEnv, NULL, 4);
    }

    // Touch collection
    struct InternalTouchReport reports[MAX_REPORTS];
    int num_reports = 0;

    // 1. Read real front touch panel
    SceTouchData touch;
    if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1) > 0) {
        for (int i = 0; i < touch.reportNum && num_reports < MAX_REPORTS; i++) {
            int x = (int)(touch.report[i].x * SCREEN_W / 1920.0f);
            int y = (int)(touch.report[i].y * SCREEN_H / 1088.0f);
            if (x < 0) x = 0; if (x >= SCREEN_W) x = SCREEN_W - 1;
            if (y < 0) y = 0; if (y >= SCREEN_H) y = SCREEN_H - 1;
            reports[num_reports].id = touch.report[i].id; // 0..255
            reports[num_reports].x = x;
            reports[num_reports].y = y;
            num_reports++;
        }
    }

    // 2. Physical Button to Virtual Widget mappings (using stable negative IDs):
    // Scheme:
    // CROSS (X)    = Sword Attack (fixed coordinate, no dynamic override)
    // CIRCLE       = Shield / Defense (fixed coordinate, no dynamic override)
    // SQUARE       = Minimap (lower down on right side, never top-right sword)
    // TRIANGLE     = Horse (fixed coordinate, no dynamic override)
    // L-TRIGGER    = Shield / Defense or Target Lock
    // R-TRIGGER    = Horse
    // SELECT       = Weapon Switch / In-Game Menu (top-right corner)

    // CROSS/CIRCLE/TRIANGLE fixed engine-space (800x480) coordinates,
    // remeasured directly off screenshots/hj/2026-09-16/2026-09-16-204503.jpg
    // (960x544 real framebuffer) with a 40px reference grid overlay + a
    // gold-ring color-mask centroid pass, then converted with the SAME
    // linear 800/960, 480/544 stretch glViewport_soloader/glScissor_soloader
    // already apply engine-side (see source/utils/glutil.c) -- not a guess:
    //   Attack (flame sword, bottom-right): screenshot (890,400) -> (742,353)
    //   Shield (wave shield, bottom-center-right): screenshot (760,480) -> (633,424)
    //   Horse (bottom-right corner): screenshot (925,505) -> (771,446)
    // These match (within a few px) the previous session's fallback values,
    // confirming the COORDINATES were never the bug. The actual, previously
    // untested bug: get_widget_pos()'s dynamic override could silently steal
    // the tap away from these confirmed-correct spots whenever some OTHER
    // HudWidget slot in the same Hud table (button_action/button_attack/
    // button_defense/button_ChangeToHorse/...) reads back "visible"/"active"
    // stale-true from a leftover HUD-transition write -- the exact same class
    // of dangling-widget-table bug already confirmed THREE times over in this
    // file's own history (is_plausible_ptr's own comment trail). Since these
    // three icons are static HUD chrome (they never move or disappear during
    // normal gameplay, confirmed by this same screenshot), the dynamic lookup
    // buys nothing here but inherits all of that fragility -- removed
    // entirely for these three; SQUARE/L-TRIGGER/R-TRIGGER/SELECT keep it
    // (not reported broken, out of scope for this pass).
    #define BTN_ATTACK_X 742.0f
    #define BTN_ATTACK_Y 353.0f
    #define BTN_SHIELD_X 633.0f
    #define BTN_SHIELD_Y 424.0f
    #define BTN_HORSE_X  771.0f
    #define BTN_HORSE_Y  446.0f

    // CROSS (X): Sword Attack.
    if (g_pad.buttons & SCE_CTRL_CROSS) {
        if (pressed & SCE_CTRL_CROSS) {
            l_info("[controls] Cross -> Attack tap (%.0f, %.0f)", BTN_ATTACK_X, BTN_ATTACK_Y);
        }
        if (num_reports < MAX_REPORTS) {
            reports[num_reports].id = -2; // Virtual ID for Cross
            reports[num_reports].x = (int)BTN_ATTACK_X;
            reports[num_reports].y = (int)BTN_ATTACK_Y;
            num_reports++;
        }
    }

    // CIRCLE: Defense / Shield (never opens menu).
    if (g_pad.buttons & SCE_CTRL_CIRCLE) {
        if (pressed & SCE_CTRL_CIRCLE) {
            l_info("[controls] Circle -> Shield tap (%.0f, %.0f)", BTN_SHIELD_X, BTN_SHIELD_Y);
        }
        if (num_reports < MAX_REPORTS) {
            reports[num_reports].id = -5; // Virtual ID for Circle Shield
            reports[num_reports].x = (int)BTN_SHIELD_X;
            reports[num_reports].y = (int)BTN_SHIELD_Y;
            num_reports++;
        }
    }

    // SQUARE: Map / Minimap (lower down on right side, never top-right sword)
    if (g_pad.buttons & SCE_CTRL_SQUARE) {
        float x = 725.0f, y = 120.0f;
        if (!get_widget_pos(HUD_OFFSET_MINI_MAP, &x, &y) || y < 60.0f) {
            x = 725.0f; y = 120.0f;
        }
        if (num_reports < MAX_REPORTS) {
            reports[num_reports].id = -3; // Virtual ID for Square
            reports[num_reports].x = (int)x;
            reports[num_reports].y = (int)y;
            num_reports++;
        }
    }

    // TRIANGLE: Mount / Dismount Horse.
    if (g_pad.buttons & SCE_CTRL_TRIANGLE) {
        if (pressed & SCE_CTRL_TRIANGLE) {
            l_info("[controls] Triangle -> Horse tap (%.0f, %.0f)", BTN_HORSE_X, BTN_HORSE_Y);
        }
        if (num_reports < MAX_REPORTS) {
            reports[num_reports].id = -4; // Virtual ID for Triangle
            reports[num_reports].x = (int)BTN_HORSE_X;
            reports[num_reports].y = (int)BTN_HORSE_Y;
            num_reports++;
        }
    }

    // L TRIGGER: Defense / Shield or Target Lock
    if (g_pad.buttons & SCE_CTRL_LTRIGGER) {
        float x = 630.0f, y = 425.0f;
        if (!get_widget_pos(HUD_OFFSET_DEFENSE, &x, &y) &&
            !get_widget_pos(HUD_OFFSET_BLOCK, &x, &y) &&
            !get_widget_pos(HUD_OFFSET_TARGET_CROSS, &x, &y)) {
            x = 630.0f; y = 425.0f;
        }
        if (num_reports < MAX_REPORTS) {
            reports[num_reports].id = -6; // Virtual ID for L Trigger
            reports[num_reports].x = (int)x;
            reports[num_reports].y = (int)y;
            num_reports++;
        }
    }

    // R TRIGGER: Mount / Dismount Horse
    if (g_pad.buttons & SCE_CTRL_RTRIGGER) {
        float x = 775.0f, y = 452.0f;
        if (!get_widget_pos(HUD_OFFSET_CHANGE_HORSE, &x, &y) &&
            !get_widget_pos(HUD_OFFSET_CHANGE_RUN, &x, &y)) {
            x = 775.0f; y = 452.0f;
        }
        if (num_reports < MAX_REPORTS) {
            reports[num_reports].id = -7; // Virtual ID for R Trigger
            reports[num_reports].x = (int)x;
            reports[num_reports].y = (int)y;
            num_reports++;
        }
    }

    // SELECT: In-Game Menu / Bag / Weapon Switch (top-right corner)
    if (g_pad.buttons & SCE_CTRL_SELECT) {
        float x = 765.0f, y = 35.0f;
        if (!get_widget_pos(HUD_OFFSET_IGM, &x, &y) &&
            !get_widget_pos(HUD_OFFSET_SYS_IGM, &x, &y) &&
            !get_widget_pos(HUD_OFFSET_SWITCH_WEAPON, &x, &y) &&
            !get_widget_pos(HUD_OFFSET_SWORD, &x, &y)) {
            x = 765.0f; y = 35.0f;
        }
        if (num_reports < MAX_REPORTS) {
            reports[num_reports].id = -8; // Virtual ID for Select
            reports[num_reports].x = (int)x;
            reports[num_reports].y = (int)y;
            num_reports++;
        }
    }

    // 3. Multi-touch slot allocation loop (strictly 0..4 slots)
    int seen[MAX_TOUCH_SLOTS] = {0};

    for (int r = 0; r < num_reports; r++) {
        int hwId = reports[r].id;
        int x = reports[r].x;
        int y = reports[r].y;

        int slot = -1;
        for (int s = 0; s < MAX_TOUCH_SLOTS; s++) {
            if (s_slotHwId[s] == hwId) {
                slot = s;
                break;
            }
        }
        if (slot == -1) {
            for (int s = 0; s < MAX_TOUCH_SLOTS; s++) {
                if (s_slotHwId[s] == -1) {
                    slot = s;
                    break;
                }
            }
            if (slot == -1) continue; // All 5 slots full
            s_slotHwId[slot] = hwId;
            s_lastX[slot] = -1;
            s_lastY[slot] = -1;
        }
        seen[slot] = 1;

        if (s_nativeTouch) {
            if (s_lastX[slot] == -1) {
                s_nativeTouch(s_jniEnv, NULL, 1, x, y, slot); // ACTION_DOWN
            } else if (s_lastX[slot] != x || s_lastY[slot] != y) {
                s_nativeTouch(s_jniEnv, NULL, 2, x, y, slot); // ACTION_MOVE
            }
        }
        s_lastX[slot] = x;
        s_lastY[slot] = y;
    }

    // 4. Release slots that are no longer active
    for (int s = 0; s < MAX_TOUCH_SLOTS; s++) {
        if (s_slotHwId[s] != -1 && !seen[s]) {
            if (s_nativeTouch) {
                s_nativeTouch(s_jniEnv, NULL, 0, s_lastX[s], s_lastY[s], s); // ACTION_UP
            }
            s_slotHwId[s] = -1;
            s_lastX[s] = -1;
            s_lastY[s] = -1;
        }
    }
}
