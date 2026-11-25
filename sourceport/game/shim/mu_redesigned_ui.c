/* Display-only shim for the optional Redesigned UI refresh (mu_redesigned_ui.h).
 *
 * Publishes read-only snapshots of live Melee menus, select screens, match HUD, and pause state
 * once per drawn frame (never during rollback resimulation), and answers lightweight draw-skip
 * predicates so vanilla 2D UI draws are omitted only when the player has enabled the matching
 * Redesigned UI layer. All game logic, physics, input timing, RNG, and online sync stay identical.
 *
 * All mutable state here is UI-only and explicitly excluded from rollback snapshots via
 * MU_EXCLUSIONS(redesigned_ui, ...).
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "mu_native.h"

#include <melee/gm/gm_1601.h>
#include <melee/gm/gm_unsplit.h>
#include <melee/gm/gmscene.h>
#include <melee/gm/gmvs.h>
#include <melee/gm/types.h>
#include <melee/if/ifall.h>
#include <melee/if/ifstatus.h>
#include <melee/if/iftime.h>
#include <melee/if/types.h>
#include <melee/mn/mnmain.h>
#include <melee/mn/types.h>
#include <melee/pl/player.h>
#include <melee/ft/types.h>
#include <melee/ft/forward.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjplink.h>

typedef u8 uint8_t;
typedef s8 int8_t;
typedef u16 uint16_t;
typedef s16 int16_t;
typedef u32 uint32_t;
typedef s32 int32_t;
#define MU_RUI_NO_STDINT 1
#include "mu_redesigned_ui.h"

unsigned int mu_rui_flags_abi(void);
void mu_rui_snapshot_abi(const void* snapshot);

extern struct GameSceneInfo* gm_804D6720;
extern MenuFlow mn_804A04F0;
extern Element_803F9628 ifStatus_803F9628[8];

int mu_nametag_position(int slot, float* x, float* y);
int mu_fighter_screen_position(int slot, float* x, float* y);

/* Display-only scratch state, excluded from rollback snapshots. */
typedef struct MuRuiSavedBounds {
    float bound_l, bound_r, bound_u, bound_d;
} MuRuiSavedBounds;

typedef struct MuRuiState {
    uint32_t frame_counter;
    /* Title */
    uint32_t title_frame;
    uint8_t title_ready;
    uint8_t title_intro_active;
    /* Main menu */
    uint32_t menu_frame;
    uint8_t menu_kind;
    uint8_t menu_prev;
    uint8_t menu_selection;
    uint8_t menu_item_count;
    uint8_t menu_entering;
    uint8_t menu_online_page;
    uint8_t menu_online_sub;
    uint32_t menu_item_mask;
    /* CSS */
    uint32_t css_frame;
    uint8_t css_ready_to_fight;
    uint8_t css_is_teams;
    uint8_t css_match_type;
    uint8_t css_mode;
    uint8_t css_stocks;
    uint8_t css_time_minutes;
    uint8_t css_door_count;
    uint8_t css_is_mex;
    uint8_t css_is_online;
    uint8_t css_online_mode;
    uint8_t css_ready_timer;
    uint32_t css_unlocked_mask;
    MuRuiCssPort css_ports[4];
    int css_bounds_saved;
    int css_bounds_applied;
    MuRuiSavedBounds css_vanilla_bounds[MU_RUI_CSS_SLOTS];
    /* SSS */
    uint32_t sss_frame;
    uint8_t sss_is_mex;
    uint8_t sss_confirmed;
    uint8_t sss_loading;
    uint8_t sss_hovered_index;
    uint8_t sss_hovered_stkind;
    uint8_t sss_frozen_stadium;
    uint8_t sss_is_online;
    uint32_t sss_unlocked_mask;
    uint32_t sss_struck_mask;
    float sss_cursor_x;
    float sss_cursor_y;
    /* Match banner & pause */
    uint8_t banner_kind;
    uint8_t banner_frames;
    uint8_t pause_active;
    int8_t pause_slot;
    uint8_t pause_flags;
    uint8_t pause_cursor;
} MuRuiState;

static MuRuiState g_rui;

MU_EXCLUSIONS(redesigned_ui, { &g_rui, sizeof(g_rui) })

static uint32_t rui_flags_raw(void)
{
    return (uint32_t) mu_rui_flags_abi();
}

unsigned int mu_rui_flags(void)
{
    return (unsigned int) rui_flags_raw();
}

static int rui_is_supported_menu(int kind)
{
    switch (kind) {
    case MENU_KIND_MAIN:
    case MENU_KIND_1P:
    case MENU_KIND_VS:
    case MENU_KIND_TOY:
    case MENU_KIND_SETTINGS:
    case MENU_KIND_DATA:
    case MENU_KIND_REG:
    case MENU_KIND_8:
    case MENU_KIND_STADIUM:
    case MENU_KIND_SPECIAL:
        return 1;
    default:
        return 0;
    }
}

int mu_rui_hud_active(void)
{
    uint32_t f = rui_flags_raw();
    return (f & (MU_RUI_FLAG_ENABLED | MU_RUI_FLAG_HUD)) == (MU_RUI_FLAG_ENABLED | MU_RUI_FLAG_HUD);
}

int mu_rui_pause_active(void)
{
    uint32_t f = rui_flags_raw();
    return (f & (MU_RUI_FLAG_ENABLED | MU_RUI_FLAG_PAUSE)) == (MU_RUI_FLAG_ENABLED | MU_RUI_FLAG_PAUSE);
}

int mu_rui_title_active(void)
{
    uint32_t f = rui_flags_raw();
    return (f & (MU_RUI_FLAG_ENABLED | MU_RUI_FLAG_TITLE)) == (MU_RUI_FLAG_ENABLED | MU_RUI_FLAG_TITLE);
}

int mu_rui_menus_active(void)
{
    uint32_t f = rui_flags_raw();
    if ((f & (MU_RUI_FLAG_ENABLED | MU_RUI_FLAG_MENUS)) != (MU_RUI_FLAG_ENABLED | MU_RUI_FLAG_MENUS)) {
        return 0;
    }
    return rui_is_supported_menu((int) mn_804A04F0.cur_menu);
}

int mu_rui_css_active(void)
{
    uint32_t f = rui_flags_raw();
    if ((f & (MU_RUI_FLAG_ENABLED | MU_RUI_FLAG_CSS)) != (MU_RUI_FLAG_ENABLED | MU_RUI_FLAG_CSS)) {
        return 0;
    }
    if (g_rui.css_is_mex) {
        return 0;
    }
    return 1;
}

int mu_rui_sss_active(void)
{
    uint32_t f = rui_flags_raw();
    if ((f & (MU_RUI_FLAG_ENABLED | MU_RUI_FLAG_SSS)) != (MU_RUI_FLAG_ENABLED | MU_RUI_FLAG_SSS)) {
        return 0;
    }
    if (g_rui.sss_is_mex) {
        return 0;
    }
    return 1;
}

void mu_rui_note_title(int ready, int intro_active)
{
    g_rui.title_frame = g_rui.frame_counter + 1;
    g_rui.title_ready = (uint8_t) (ready ? 1 : 0);
    g_rui.title_intro_active = (uint8_t) (intro_active ? 1 : 0);
}

void mu_rui_note_menu(int menu_kind, int prev_menu, int selection, int item_count, int entering,
                      unsigned int item_mask, int online_page, int online_sub)
{
    g_rui.menu_frame = g_rui.frame_counter + 1;
    g_rui.menu_kind = (uint8_t) menu_kind;
    g_rui.menu_prev = (uint8_t) prev_menu;
    g_rui.menu_selection = (uint8_t) selection;
    g_rui.menu_item_count = (uint8_t) item_count;
    g_rui.menu_entering = (uint8_t) (entering ? 1 : 0);
    g_rui.menu_item_mask = (uint32_t) item_mask;
    g_rui.menu_online_page = (uint8_t) online_page;
    g_rui.menu_online_sub = (uint8_t) online_sub;
}

void mu_rui_note_css_header(int ready_to_fight, int ready_timer, int is_teams, int match_type,
                            int mode, int stocks, int time_minutes, int door_count,
                            int is_mex, int is_online, int online_mode, unsigned int unlocked_mask)
{
    g_rui.css_frame = g_rui.frame_counter + 1;
    g_rui.css_ready_to_fight = (uint8_t) (ready_to_fight ? 1 : 0);
    g_rui.css_ready_timer = (uint8_t) (ready_timer > 255 ? 255 : (ready_timer < 0 ? 0 : ready_timer));
    g_rui.css_is_teams = (uint8_t) (is_teams ? 1 : 0);
    g_rui.css_match_type = (uint8_t) match_type;
    g_rui.css_mode = (uint8_t) mode;
    g_rui.css_stocks = (uint8_t) stocks;
    g_rui.css_time_minutes = (uint8_t) time_minutes;
    g_rui.css_door_count = (uint8_t) door_count;
    g_rui.css_is_mex = (uint8_t) (is_mex ? 1 : 0);
    g_rui.css_is_online = (uint8_t) (is_online ? 1 : 0);
    g_rui.css_online_mode = (uint8_t) online_mode;
    g_rui.css_unlocked_mask = (uint32_t) unlocked_mask;
}

static void rui_decode_sjis_name(char* out, unsigned int cap, const char* src)
{
    unsigned int n = 0;
    const unsigned char* s = (const unsigned char*) src;
    if (out == NULL || cap == 0) return;
    out[0] = '\0';
    if (s == NULL) return;
    while (*s && n + 1 < cap) {
        unsigned c = *s++;
        if (c == 0x82 && *s >= 0x4F && *s <= 0x58) c = '0' + (*s++ - 0x4F);
        else if (c == 0x82 && *s >= 0x60 && *s <= 0x79) c = 'A' + (*s++ - 0x60);
        else if (c == 0x82 && *s >= 0x81 && *s <= 0x9A) c = 'a' + (*s++ - 0x81);
        else if (c == 0x81) {
            unsigned ascii = 0;
            switch (*s) {
            case 0x40: ascii = ' '; break;
            case 0x43: ascii = ','; break;
            case 0x44: ascii = '.'; break;
            case 0x48: ascii = '?'; break;
            case 0x49: ascii = '!'; break;
            case 0x7C: ascii = '-'; break;
            case 0x95: ascii = '&'; break;
            default: break;
            }
            if (ascii) { c = ascii; ++s; }
        }
        if (c >= 0x20 && c < 0x7F) {
            out[n++] = (char) c;
        }
    }
    out[n] = '\0';
}

void mu_rui_note_css_port(int port, int kind, int sel_icon, int char_kind, int costume,
                          int team, int cpu_level, int locked, int holding_puck,
                          float hand_x, float hand_y, float puck_x, float puck_y,
                          const char* nametag)
{
    MuRuiCssPort* p;
    if (port < 0 || port >= 4) return;
    p = &g_rui.css_ports[port];
    p->kind = (uint8_t) kind;
    p->sel_icon = (uint8_t) (sel_icon < 0 ? 0xFF : sel_icon);
    p->char_kind = (uint8_t) (char_kind < 0 ? 0xFF : char_kind);
    p->costume = (uint8_t) costume;
    p->team = (uint8_t) team;
    p->cpu_level = (uint8_t) cpu_level;
    p->locked = (uint8_t) (locked ? 1 : 0);
    p->holding_puck = (uint8_t) (holding_puck ? 1 : 0);
    p->hand_x = hand_x;
    p->hand_y = hand_y;
    p->puck_x = puck_x;
    p->puck_y = puck_y;
    rui_decode_sjis_name(p->nametag, sizeof(p->nametag), nametag);
}

/* Syncs Melee's 26-entry CSSIcon hit boxes between vanilla 3x9 and Redesigned 2x13 so analog
 * stick selection lands directly on each visual portrait tile without breaking vanilla CSS when
 * the player switches the option off. Local menu UI only — never runs inside a match. */
void mu_rui_sync_css_bounds(void* icons_void, int count, int is_mex)
{
    CSSIcon* icons = (CSSIcon*) icons_void;
    int i, slot;
    const int want = !is_mex && mu_rui_css_active() && count >= MU_RUI_CSS_SLOTS;

    if (icons == NULL || count < MU_RUI_CSS_SLOTS) return;

    if (!g_rui.css_bounds_saved) {
        for (i = 0; i < MU_RUI_CSS_SLOTS; ++i) {
            g_rui.css_vanilla_bounds[i].bound_l = icons[i].bound_l;
            g_rui.css_vanilla_bounds[i].bound_r = icons[i].bound_r;
            g_rui.css_vanilla_bounds[i].bound_u = icons[i].bound_u;
            g_rui.css_vanilla_bounds[i].bound_d = icons[i].bound_d;
        }
        g_rui.css_bounds_saved = 1;
    }

    if (want && !g_rui.css_bounds_applied) {
        const float col_w = (MU_RUI_CSS_GRID_RIGHT - MU_RUI_CSS_GRID_LEFT) / (float) MU_RUI_CSS_COLS;
        const float row_h = (MU_RUI_CSS_GRID_TOP - MU_RUI_CSS_GRID_BOTTOM) / (float) MU_RUI_CSS_ROWS;
        for (slot = 0; slot < MU_RUI_CSS_SLOTS; ++slot) {
            int icon_idx = (int) mu_rui_css_slot_to_icon(slot);
            int row = slot / MU_RUI_CSS_COLS;
            int col = slot % MU_RUI_CSS_COLS;
            if (icon_idx < 0 || icon_idx >= MU_RUI_CSS_SLOTS) continue;
            icons[icon_idx].bound_l = MU_RUI_CSS_GRID_LEFT + (float) col * col_w;
            icons[icon_idx].bound_r = MU_RUI_CSS_GRID_LEFT + (float) (col + 1) * col_w;
            icons[icon_idx].bound_u = MU_RUI_CSS_GRID_TOP - (float) row * row_h;
            icons[icon_idx].bound_d = MU_RUI_CSS_GRID_TOP - (float) (row + 1) * row_h;
        }
        g_rui.css_bounds_applied = 1;
    } else if (!want && g_rui.css_bounds_applied) {
        for (i = 0; i < MU_RUI_CSS_SLOTS; ++i) {
            icons[i].bound_l = g_rui.css_vanilla_bounds[i].bound_l;
            icons[i].bound_r = g_rui.css_vanilla_bounds[i].bound_r;
            icons[i].bound_u = g_rui.css_vanilla_bounds[i].bound_u;
            icons[i].bound_d = g_rui.css_vanilla_bounds[i].bound_d;
        }
        g_rui.css_bounds_applied = 0;
    }
}

void mu_rui_note_sss(int is_mex, int confirmed, int loading, int hovered_index, int hovered_stkind,
                     int frozen_stadium, int is_online, unsigned int unlocked_mask,
                     unsigned int struck_mask, float cursor_x, float cursor_y)
{
    g_rui.sss_frame = g_rui.frame_counter + 1;
    g_rui.sss_is_mex = (uint8_t) (is_mex ? 1 : 0);
    g_rui.sss_confirmed = (uint8_t) (confirmed ? 1 : 0);
    g_rui.sss_loading = (uint8_t) (loading ? 1 : 0);
    g_rui.sss_hovered_index = (uint8_t) hovered_index;
    g_rui.sss_hovered_stkind = (uint8_t) hovered_stkind;
    g_rui.sss_frozen_stadium = (uint8_t) (frozen_stadium ? 1 : 0);
    g_rui.sss_is_online = (uint8_t) (is_online ? 1 : 0);
    g_rui.sss_unlocked_mask = (uint32_t) unlocked_mask;
    g_rui.sss_struck_mask = (uint32_t) struck_mask;
    g_rui.sss_cursor_x = cursor_x;
    g_rui.sss_cursor_y = cursor_y;
}

/* Maps Melee's SSS cursor coordinates (-27..+27, -19..+19) directly onto the Redesigned SSS
 * horizontal stage strips (top primary row: Random + 5 Neutral + 1 Counterpick; bottom compact
 * 2-row grid: remaining 23 stages). Returns 1 and writes *out_icon (0..29) on hit. */
int mu_rui_sss_hit_test(float cx, float cy, unsigned int unlocked_mask, int* out_icon)
{
    if (!mu_rui_sss_active() || out_icon == NULL) return 0;

    /* Primary competitive strip: x in [-25, +25], y in [-2, +7] (7 wide cards) */
    if (cx >= -25.5f && cx <= 25.5f && cy >= -2.5f && cy <= 7.5f) {
        int slot = (int) ((cx + 25.5f) * (float) MU_RUI_SSS_PRIMARY_COUNT / 51.0f);
        if (slot < 0) slot = 0;
        if (slot >= MU_RUI_SSS_PRIMARY_COUNT) slot = MU_RUI_SSS_PRIMARY_COUNT - 1;
        {
            int icon = (int) mu_rui_sss_primary_icon(slot);
            if (icon == 29 || ((unlocked_mask >> icon) & 1u)) {
                *out_icon = icon;
                return 1;
            }
        }
    }

    /* Secondary all-stages strip below: 2 rows of 12 + 11 cards in x [-25.5, +25.5], y [-16.5, -4.5] */
    if (cx >= -25.5f && cx <= 25.5f && cy >= -16.5f && cy <= -4.5f) {
        int row = cy > -10.5f ? 0 : 1;
        int cols = row == 0 ? 12 : 11;
        int col = (int) ((cx + 25.5f) * (float) cols / 51.0f);
        int idx;
        if (col < 0) col = 0;
        if (col >= cols) col = cols - 1;
        idx = row * 12 + col;
        if (idx >= 0 && idx < MU_RUI_SSS_EXTRA_COUNT) {
            int icon = (int) mu_rui_sss_extra_icon(idx);
            if ((unlocked_mask >> icon) & 1u) {
                *out_icon = icon;
                return 1;
            }
        }
    }
    return 0;
}

void mu_rui_note_banner(int elem_idx)
{
    uint8_t kind = MU_RUI_BANNER_NONE;
    switch (elem_idx) {
    case 0: kind = MU_RUI_BANNER_TIME; break;
    case 1: kind = MU_RUI_BANNER_SUDDEN_DEATH; break;
    case 3: kind = MU_RUI_BANNER_READY; break;
    case 4: kind = MU_RUI_BANNER_GO; break;
    case 5: kind = MU_RUI_BANNER_GAME; break;
    case 6: kind = MU_RUI_BANNER_COMPLETE; break;
    case 7: kind = MU_RUI_BANNER_FAILURE; break;
    default: kind = MU_RUI_BANNER_NONE; break;
    }
    g_rui.banner_kind = kind;
    g_rui.banner_frames = 0;
}

void mu_rui_note_pause(int slot, int flag)
{
    if (slot >= 0 && slot < 4 && flag != 0) {
        g_rui.pause_active = 1;
        g_rui.pause_slot = (int8_t) slot;
        g_rui.pause_flags = (uint8_t) flag;
        g_rui.pause_cursor = 0;
    } else {
        g_rui.pause_active = 0;
        g_rui.pause_slot = -1;
        g_rui.pause_flags = 0;
    }
}

static int rui_live_fighter(const HSD_GObj* gobj)
{
    const HSD_GObj* cur;
    if (gobj == NULL || HSD_GObjPLinkHead == NULL) return 0;
    for (cur = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_FIGHTER]; cur != NULL; cur = cur->next) {
        if (cur == gobj) return 1;
    }
    return 0;
}

static int rui_combo_stun_action(unsigned int a)
{
    return a == 38 || (a >= 75 && a <= 91) || (a >= 183 && a <= 204) || (a >= 223 && a <= 232) ||
           (a >= 239 && a <= 243) || (a >= 266 && a <= 296);
}

static int rui_nana_damage(int slot)
{
    HSD_GObj* gobj = Player_GetEntityAtIndex(slot, 1);
    Fighter* fp = rui_live_fighter(gobj) ? (Fighter*) gobj->user_data : NULL;
    return fp != NULL && fp->kind == Ft_Kind_Nana && fp->is_sub_fighter && fp->player_idx == slot
               ? (int) fp->dmg.x1830_percent : -1;
}

static int rui_active_banner_elem(void)
{
    int i;
    /* Check live banner GObjs in ifStatus_803F9628[0..7] so the banner disappears automatically
     * the exact frame Melee frees its GObj. */
    static const int order[7] = { 4, 3, 5, 0, 1, 6, 7 };
    for (i = 0; i < 7; ++i) {
        int idx = order[i];
        if (ifStatus_803F9628[idx].x0 != NULL) {
            return idx;
        }
    }
    return -1;
}

void mu_rui_publish_frame(void)
{
    MuRedesignedUiSnapshot snap;
    uint32_t flags;
    int major, minor, scene_kind, in_match;
    int slot;

    g_rui.frame_counter++;
    flags = rui_flags_raw();
    if (!(flags & MU_RUI_FLAG_ENABLED)) {
        return;
    }

    __builtin_memset(&snap, 0, sizeof(snap));
    snap.valid = 1;
    snap.frame_counter = g_rui.frame_counter;
    snap.active_flags = flags;
    major = gm_GetCurrentGameMode();
    minor = gm_GetCurrentSceneIndex();
    scene_kind = gm_804D6720 != NULL ? (int) gm_804D6720->scene_kind : -1;
    snap.scene_major = (uint8_t) major;
    snap.scene_minor = (uint8_t) minor;
    snap.is_online = (uint8_t) (major == GM_HANYU_CSS ? 1 : 0);
    snap.is_training = (uint8_t) (major == GM_TRAINING ? 1 : 0);
    snap.match_pauser = -1;

    in_match = (scene_kind == GS_VS || scene_kind == GS_SUDDEN_DEATH ||
                scene_kind == GS_TRAINING || scene_kind == GS_CAMERA_VS);

    if (in_match) {
        VsSceneController* vs = gmVs_GetSceneController();
        StartMeleeRules* rules = gm_GetStartMeleeRules();
        int live_banner = rui_active_banner_elem();

        snap.screen = MU_RUI_SCREEN_MATCH;
        snap.match_active = 1;
        snap.vanilla_suppressed = (uint8_t) (mu_rui_hud_active() ? 1 : 0);

        if (live_banner >= 0) {
            uint8_t want = MU_RUI_BANNER_NONE;
            switch (live_banner) {
            case 0: want = MU_RUI_BANNER_TIME; break;
            case 1: want = MU_RUI_BANNER_SUDDEN_DEATH; break;
            case 3: want = MU_RUI_BANNER_READY; break;
            case 4: want = MU_RUI_BANNER_GO; break;
            case 5: want = MU_RUI_BANNER_GAME; break;
            case 6: want = MU_RUI_BANNER_COMPLETE; break;
            case 7: want = MU_RUI_BANNER_FAILURE; break;
            default: break;
            }
            if (g_rui.banner_kind != want) {
                g_rui.banner_kind = want;
                g_rui.banner_frames = 0;
            } else if (g_rui.banner_frames < 255) {
                g_rui.banner_frames++;
            }
            snap.match_banner = g_rui.banner_kind;
            snap.match_banner_frame = g_rui.banner_frames;
        } else {
            g_rui.banner_kind = MU_RUI_BANNER_NONE;
            g_rui.banner_frames = 0;
        }

        if (rules != NULL) {
            snap.match_is_teams = (uint8_t) (rules->is_teams ? 1 : 0);
            snap.match_is_stock = (uint8_t) (rules->is_stock ? 1 : 0);
            snap.match_is_stamina = (uint8_t) (rules->match_kind == 3 ? 1 : 0);
            snap.stage_kind = (uint8_t) rules->stkind;
            snap.timer_enabled = (uint8_t) (rules->timer_enabled ? 1 : 0);
            snap.timer_counts_up = (uint8_t) (rules->timer_counts_up ? 1 : 0);
            snap.timer_shows_hours = (uint8_t) (rules->timer_shows_hours ? 1 : 0);
        }
        snap.timer_hidden = (uint8_t) ((ifTime_IsTimerHidden() || ifAll_IsHUDHidden()) ? 1 : 0);
        snap.timer_seconds = gm_8016AEEC();
        snap.timer_centiseconds = gm_8016AF0C();

        /* Pause state: verify against Melee's real DB pause flags so unpause always clears it. */
        if (gm_GetDbPauseFlag(1) || gm_GetDbPauseFlag(2)) {
            snap.match_paused = 1;
            snap.match_pauser = g_rui.pause_active ? g_rui.pause_slot
                                                   : (vs != NULL ? vs->state.pauser : (int8_t) 0);
            snap.match_pause_flags = g_rui.pause_flags ? g_rui.pause_flags : 5u;
            snap.match_pause_cursor = g_rui.pause_cursor;
        } else {
            g_rui.pause_active = 0;
            snap.match_paused = 0;
            snap.match_pauser = -1;
        }

        for (slot = 0; slot < 4; ++slot) {
            MuRuiHudPlayer* hp = &snap.hud_players[slot];
            int slot_type = Player_GetPlayerSlotType(slot);
            int present = (slot_type != Gm_PKind_NA);
            float tx = 0.0f, ty = 0.0f;
            int vis = 0;

            hp->present = (uint8_t) (present ? 1 : 0);
            hp->is_cpu = (uint8_t) (slot_type == Gm_PKind_Cpu ? 1 : 0);
            hp->char_kind = 0xFF;
            hp->team = -1;
            hp->nana_damage = -1;
            if (!present) continue;

            hp->char_kind = (uint8_t) Player_GetPlayerCharacter(slot);
            hp->costume = (uint8_t) Player_GetCostumeId(slot);
            hp->team = (int8_t) (gm_8016B168() ? Player_GetTeam(slot) : -1);
            hp->stocks = (uint8_t) (Player_GetStocks(slot) < 0 ? 0 : Player_GetStocks(slot));
            hp->damage = (int16_t) ifStatus_HudInfo.players[slot].damage_percent;
            hp->nana_damage = (int16_t) rui_nana_damage(slot);

            vis = mu_nametag_position(slot, &tx, &ty);
            if (!vis && !mu_fighter_screen_position(slot, &tx, &ty)) {
                tx = ty = __builtin_nanf("");
            }
            hp->tag_x = tx;
            hp->tag_y = ty;
            hp->tag_visible = (uint8_t) (vis ? 1 : 0);

            {
                HSD_GObj* gobj = Player_GetEntity(slot);
                Fighter* fp = rui_live_fighter(gobj) ? (Fighter*) gobj->user_data : NULL;
                if (fp != NULL) {
                    hp->alive = 1;
                    hp->damage_exact = fp->dmg.x1830_percent;
                    hp->hitstun = (uint8_t) ((fp->dmg.x195c_hitlag_frames > 0.0f ||
                                              rui_combo_stun_action(fp->motion_id)) ? 1 : 0);
                } else {
                    hp->alive = 0;
                    hp->damage_exact = (float) hp->damage;
                }
            }
        }
    } else if (g_rui.css_frame == g_rui.frame_counter) {
        snap.screen = MU_RUI_SCREEN_CSS;
        snap.css_active = 1;
        snap.css_ready_to_fight = g_rui.css_ready_to_fight;
        snap.css_ready_timer = g_rui.css_ready_timer;
        snap.css_is_teams = g_rui.css_is_teams;
        snap.css_match_type = g_rui.css_match_type;
        snap.css_mode = g_rui.css_mode;
        snap.css_stocks = g_rui.css_stocks;
        snap.css_time_minutes = g_rui.css_time_minutes;
        snap.css_door_count = g_rui.css_door_count;
        snap.css_is_mex = g_rui.css_is_mex;
        snap.is_online = g_rui.css_is_online;
        snap.online_mode = g_rui.css_online_mode;
        snap.css_unlocked_mask = g_rui.css_unlocked_mask;
        __builtin_memcpy(snap.css_ports, g_rui.css_ports, sizeof(snap.css_ports));
        snap.vanilla_suppressed = (uint8_t) (mu_rui_css_active() ? 1 : 0);
    } else if (g_rui.sss_frame == g_rui.frame_counter) {
        snap.screen = MU_RUI_SCREEN_SSS;
        snap.sss_active = 1;
        snap.sss_is_mex = g_rui.sss_is_mex;
        snap.sss_confirmed = g_rui.sss_confirmed;
        snap.sss_loading = g_rui.sss_loading;
        snap.sss_hovered_index = g_rui.sss_hovered_index;
        snap.sss_hovered_stkind = g_rui.sss_hovered_stkind;
        snap.sss_frozen_stadium = g_rui.sss_frozen_stadium;
        snap.is_online = g_rui.sss_is_online;
        snap.sss_unlocked_mask = g_rui.sss_unlocked_mask;
        snap.sss_struck_mask = g_rui.sss_struck_mask;
        snap.sss_cursor_x = g_rui.sss_cursor_x;
        snap.sss_cursor_y = g_rui.sss_cursor_y;
        snap.vanilla_suppressed = (uint8_t) (mu_rui_sss_active() ? 1 : 0);
    } else if (major == GM_TITLE || g_rui.title_frame == g_rui.frame_counter) {
        snap.screen = MU_RUI_SCREEN_TITLE;
        snap.title_ready = g_rui.title_ready;
        snap.title_intro_active = g_rui.title_intro_active;
        snap.vanilla_suppressed = (uint8_t) (mu_rui_title_active() ? 1 : 0);
    } else if (major == GM_MENU || g_rui.menu_frame == g_rui.frame_counter) {
        snap.screen = MU_RUI_SCREEN_MAIN_MENU;
        snap.menu_kind = (uint8_t) mn_804A04F0.cur_menu;
        snap.menu_prev = (uint8_t) mn_804A04F0.prev_menu;
        snap.menu_selection = (uint8_t) mn_804A04F0.hovered_selection;
        snap.menu_entering = (uint8_t) mn_804A04F0.entering_menu;
        snap.menu_item_count = g_rui.menu_item_count;
        snap.menu_item_mask = g_rui.menu_item_mask;
        snap.menu_online_page = g_rui.menu_online_page;
        snap.menu_online_sub = g_rui.menu_online_sub;
        snap.menu_supported = (uint8_t) rui_is_supported_menu((int) snap.menu_kind);
        snap.vanilla_suppressed = (uint8_t) (mu_rui_menus_active() ? 1 : 0);
    }

    mu_rui_snapshot_abi(&snap);
}
