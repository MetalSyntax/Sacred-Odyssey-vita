#include "controls.h"
#include "utils/logger.h"

#include <so_util/so_util.h>
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
static uintptr_t **s_hud_s_pInstance_ptr = NULL;

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
    HUD_OFFSET_HEALTH_GROUP  = 96,  // status_healthGroup
    HUD_OFFSET_TUTORIAL_DLG  = 104, // button_HudTurtorialDialogInGame
    HUD_OFFSET_MINI_MAP      = 108, // Mini_Map
    HUD_OFFSET_SYS_IGM       = 112, // button_toSysIGM
    HUD_OFFSET_IGM           = 116, // button_toIGM
};

// Guard against dangling/garbage HudWidget*/Hud* left in the Hud widget
// table. Three independent real crashes confirmed this is a real, recurring
// class of bug, not a single one-off:
//   - 0x4620656e (not 4-byte aligned) dereferencing widget+0x18 while
//     pressing Cross with HUD_OFFSET_TUTORIAL_DLG stale.
//   - 0x100 (4-byte aligned, but far too low to be any real object -- the
//     loader is at 0x81000000, the .so at 0x98000000, and every real heap
//     pointer observed in this port's logs sits well above 0x81000000) while
//     pressing Triangle with HUD_OFFSET_SWITCH_MENU stale, right after the
//     in-game menu closed.
//   - 0x00010118 while pressing Triangle with HUD_OFFSET_SWITCH_MENU stale
//     (confirmed via .psp2dmp: is_widget_active() read widget+0x18 fine --
//     that low page happened to be mapped -- then faulted on widget+0xdc).
//     This slipped through the previous MIN_PLAUSIBLE_PTR (0x00010000): being
//     merely above the null guard page is not the same as being a real
//     object. Every real heap/stack pointer this port has ever logged sits
//     above 0x80000000, so the floor is raised to comfortably reject any
//     small-int/near-NULL garbage while still never rejecting a live object.
// A real HudWidget*/Hud* is always 4-byte aligned (it has a vtable) AND well
// above the low guard-page region every OS reserves unmapped specifically so
// small-integer/NULL-like bugs fault immediately -- so this can never reject
// a genuinely live object, only the garbage the engine occasionally leaves
// behind in its own widget table during HUD transitions.
#define MIN_PLAUSIBLE_PTR 0x10000000u
static bool is_plausible_ptr(uintptr_t p) {
    return p >= MIN_PLAUSIBLE_PTR && (p & 0x3) == 0;
}

static bool is_widget_active(uintptr_t widget, float *out_x, float *out_y) {
    if (!widget || !is_plausible_ptr(widget)) return false;
    // widget + 0x18 is the visible/active flag in HudWidget
    uint8_t visible = *(uint8_t *)(widget + 0x18);
    if (!visible) return false;
    if (out_x) *out_x = *(float *)(widget + 0xdc);
    if (out_y) *out_y = *(float *)(widget + 0xe0);
    return true;
}

static bool get_widget_pos(enum HudWidgetOffset offset, float *out_x, float *out_y) {
    if (!s_hud_s_pInstance_ptr || !*s_hud_s_pInstance_ptr) return false;
    uintptr_t hud = **s_hud_s_pInstance_ptr;
    if (!hud || !is_plausible_ptr(hud)) return false;
    uintptr_t widget = *(uintptr_t *)(hud + offset);
    return is_widget_active(widget, out_x, out_y);
}

// Force-hide a HUD widget (currently only the on-screen movement joystick):
// physical controls fully replace it (hook_HudMovePad_Get_MovePad_AxisValues
// reads the real stick/D-Pad directly, see below) and touching its on-screen
// graphic does nothing in this port, so leaving it drawn is pure visual
// clutter on top of the real control scheme. Reuses the same "visible" byte
// (widget + 0x18) that is_widget_active() already reads -- every HUD widget
// in this engine gates both its own touch handling AND its own draw call on
// this single flag, so clearing it hides the graphic too, not just input.
static void hide_widget(enum HudWidgetOffset offset) {
    if (!s_hud_s_pInstance_ptr || !*s_hud_s_pInstance_ptr) return;
    uintptr_t hud = **s_hud_s_pInstance_ptr;
    if (!hud || !is_plausible_ptr(hud)) return;
    uintptr_t widget = *(uintptr_t *)(hud + offset);
    if (!widget || !is_plausible_ptr(widget)) return;
    *(uint8_t *)(widget + 0x18) = 0;
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

// Hook for CameraRotatePad::Get_MovePad_AxisValues (Right Analog Stick)
static void hook_CameraRotatePad_Get_MovePad_AxisValues(float *out, uintptr_t this_ptr) {
    out[0] = 0.0f;
    out[1] = 0.0f;

    // 1. Check touch camera rotation
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

    // 2. Physical Right Analog Stick
    float rx = (g_pad.rx - 128) / 128.0f;
    float ry = (g_pad.ry - 128) / 128.0f;
    float r_len = sqrtf(rx * rx + ry * ry);
    const float deadzone = 0.18f;

    if (r_len > deadzone) {
        float norm = (r_len - deadzone) / (1.0f - deadzone);
        if (norm > 1.0f) norm = 1.0f;
        // Sensitivity tuned for smooth, responsive console camera control
        const float camera_sensitivity = 8.5f;
        out[0] += (rx / r_len) * norm * camera_sensitivity;
        out[1] += (ry / r_len) * norm * camera_sensitivity;
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

    // Resolve pointer to Hud::s_pInstance
    s_hud_s_pInstance_ptr = (uintptr_t **)so_symbol(&so_mod, "_ZN3Hud11s_pInstanceE");
    if (s_hud_s_pInstance_ptr) {
        l_info("Resolved Hud::s_pInstance symbol successfully");
    } else {
        l_warn("Could not resolve Hud::s_pInstance");
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

    // Check if defense / shield widget is active on screen
    float def_x = 735.0f, def_y = 310.0f;
    bool has_defense = get_widget_pos(HUD_OFFSET_DEFENSE, &def_x, &def_y) ||
                       get_widget_pos(HUD_OFFSET_BLOCK, &def_x, &def_y);

    // Physical controls fully replace the on-screen virtual movement
    // joystick (see hook_HudMovePad_Get_MovePad_AxisValues); its touch
    // graphic is non-functional noise on a physical-control HUD, so keep it
    // force-hidden every frame.
    hide_widget(HUD_OFFSET_MOVEPAD);

    // Menu Key Events:
    // START -> KEYCODE_MENU (82) / KEYCODE_BACK (4)
    if (pressed & SCE_CTRL_START) {
        if (s_nativeKeyDown) s_nativeKeyDown(s_jniEnv, NULL, 4);
    }
    if (released & SCE_CTRL_START) {
        if (s_nativeKeyUp) s_nativeKeyUp(s_jniEnv, NULL, 4);
    }

    // CIRCLE -> KEYCODE_BACK (4) in menus / dialogs (when defense is not active)
    if (!has_defense) {
        if (pressed & SCE_CTRL_CIRCLE) {
            if (s_nativeKeyDown) s_nativeKeyDown(s_jniEnv, NULL, 4);
        }
        if (released & SCE_CTRL_CIRCLE) {
            if (s_nativeKeyUp) s_nativeKeyUp(s_jniEnv, NULL, 4);
        }
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
    // User-requested face-button scheme (2026-09-16): X = sword, Circle =
    // shield (already handled below via has_defense), Triangle = horse,
    // Square = map. Start stays as menu (unchanged).
    //
    // CROSS (X): Sword Attack, falling back to contextual Action/Interact
    // (dialog advance, cutscene skip, talk to NPC, treasure, bomb, push box,
    // mirror) when no attack widget is active -- these HUD buttons are never
    // shown at the same time in the game's own UI (combat vs. exploration),
    // so merging them costs nothing and keeps interaction accessible on X.
    if (g_pad.buttons & SCE_CTRL_CROSS) {
        float x = 635.0f, y = 385.0f;
        if (!get_widget_pos(HUD_OFFSET_ATTACK, &x, &y) &&
            !get_widget_pos(HUD_OFFSET_SWORD, &x, &y) &&
            !get_widget_pos(HUD_OFFSET_TUTORIAL_DLG, &x, &y) &&
            !get_widget_pos(HUD_OFFSET_CUTSCENE, &x, &y) &&
            !get_widget_pos(HUD_OFFSET_TALK_NPC, &x, &y) &&
            !get_widget_pos(HUD_OFFSET_OPEN_TREASURE, &x, &y) &&
            !get_widget_pos(HUD_OFFSET_PICK_BOMB, &x, &y) &&
            !get_widget_pos(HUD_OFFSET_PUSH_BOX, &x, &y) &&
            !get_widget_pos(HUD_OFFSET_ROTATE_MIRROR, &x, &y) &&
            !get_widget_pos(HUD_OFFSET_ACTION, &x, &y)) {
            x = 635.0f; y = 385.0f;
        }
        if (num_reports < MAX_REPORTS) {
            reports[num_reports].id = -2; // Virtual ID for Cross
            reports[num_reports].x = (int)x;
            reports[num_reports].y = (int)y;
            num_reports++;
        }
    }

    // SQUARE: Map / Minimap
    if (g_pad.buttons & SCE_CTRL_SQUARE) {
        float x = 765.0f, y = 35.0f;
        if (!get_widget_pos(HUD_OFFSET_MINI_MAP, &x, &y)) {
            x = 765.0f; y = 35.0f;
        }
        if (num_reports < MAX_REPORTS) {
            reports[num_reports].id = -3; // Virtual ID for Square
            reports[num_reports].x = (int)x;
            reports[num_reports].y = (int)y;
            num_reports++;
        }
    }

    // TRIANGLE: Mount / Dismount Horse
    if (g_pad.buttons & SCE_CTRL_TRIANGLE) {
        float x = 745.0f, y = 205.0f;
        if (!get_widget_pos(HUD_OFFSET_CHANGE_HORSE, &x, &y) &&
            !get_widget_pos(HUD_OFFSET_CHANGE_RUN, &x, &y)) {
            x = 745.0f; y = 205.0f;
        }
        if (num_reports < MAX_REPORTS) {
            reports[num_reports].id = -4; // Virtual ID for Triangle
            reports[num_reports].x = (int)x;
            reports[num_reports].y = (int)y;
            num_reports++;
        }
    }

    // CIRCLE: Defense / Guard in gameplay
    if ((g_pad.buttons & SCE_CTRL_CIRCLE) && has_defense) {
        if (num_reports < MAX_REPORTS) {
            reports[num_reports].id = -5; // Virtual ID for Circle Shield
            reports[num_reports].x = (int)def_x;
            reports[num_reports].y = (int)def_y;
            num_reports++;
        }
    }

    // L TRIGGER: Defense / Shield or Target Lock
    if (g_pad.buttons & SCE_CTRL_LTRIGGER) {
        float x = 735.0f, y = 310.0f;
        if (!get_widget_pos(HUD_OFFSET_DEFENSE, &x, &y) &&
            !get_widget_pos(HUD_OFFSET_BLOCK, &x, &y) &&
            !get_widget_pos(HUD_OFFSET_TARGET_CROSS, &x, &y)) {
            x = 735.0f; y = 310.0f;
        }
        if (num_reports < MAX_REPORTS) {
            reports[num_reports].id = -6; // Virtual ID for L Trigger
            reports[num_reports].x = (int)x;
            reports[num_reports].y = (int)y;
            num_reports++;
        }
    }

    // R TRIGGER: Mount / Dismount Horse or Secondary Attack
    if (g_pad.buttons & SCE_CTRL_RTRIGGER) {
        float x = 745.0f, y = 205.0f;
        if (!get_widget_pos(HUD_OFFSET_CHANGE_HORSE, &x, &y) &&
            !get_widget_pos(HUD_OFFSET_CHANGE_RUN, &x, &y) &&
            !get_widget_pos(HUD_OFFSET_ATTACK, &x, &y)) {
            x = 745.0f; y = 205.0f;
        }
        if (num_reports < MAX_REPORTS) {
            reports[num_reports].id = -7; // Virtual ID for R Trigger
            reports[num_reports].x = (int)x;
            reports[num_reports].y = (int)y;
            num_reports++;
        }
    }

    // SELECT: In-Game Menu / Bag / Minimap
    if (g_pad.buttons & SCE_CTRL_SELECT) {
        float x = 765.0f, y = 35.0f;
        if (!get_widget_pos(HUD_OFFSET_IGM, &x, &y) &&
            !get_widget_pos(HUD_OFFSET_MINI_MAP, &x, &y)) {
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
