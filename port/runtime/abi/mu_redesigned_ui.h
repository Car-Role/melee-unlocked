/* Shared plain-C ABI for the optional Redesigned UI refresh (inspired by Dave Abarra's
 * "Melee Redesigned" concept). Display only: the game publishes read-only snapshots of its live
 * menu, select-screen, match HUD, and pause state, and skips only the matching vanilla GX draws
 * when the player opts in. No game logic, physics, input timing, RNG, or online sync changes.
 *
 * Shared verbatim by the MSVC/Clang host and the GCC native game library.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef MU_REDESIGNED_UI_H
#define MU_REDESIGNED_UI_H

#if !defined(MU_RUI_NO_STDINT)
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Live feature flags returned by MuHostApi.redesigned_ui_flags(). Zero when off or in Clean Replays. */
#define MU_RUI_FLAG_ENABLED  0x00000001u /* master switch */
#define MU_RUI_FLAG_HUD      0x00000002u /* in-game match HUD, timer, and announcements */
#define MU_RUI_FLAG_PAUSE    0x00000004u /* minimalist pause overlay */
#define MU_RUI_FLAG_TITLE    0x00000008u /* title screen typography over live background */
#define MU_RUI_FLAG_MENUS    0x00000010u /* main menu and supported list submenus */
#define MU_RUI_FLAG_CSS      0x00000020u /* character select screen (2x13 compact roster) */
#define MU_RUI_FLAG_SSS      0x00000040u /* stage select screen (horizontal stage strip) */
#define MU_RUI_FLAG_ALL      (MU_RUI_FLAG_ENABLED | MU_RUI_FLAG_HUD | MU_RUI_FLAG_PAUSE | \
                              MU_RUI_FLAG_TITLE | MU_RUI_FLAG_MENUS | MU_RUI_FLAG_CSS |   \
                              MU_RUI_FLAG_SSS)

/* Active high-level screen identified by the game. */
enum MuRuiScreen {
    MU_RUI_SCREEN_NONE      = 0,
    MU_RUI_SCREEN_TITLE     = 1,
    MU_RUI_SCREEN_MAIN_MENU = 2,
    MU_RUI_SCREEN_CSS       = 3,
    MU_RUI_SCREEN_SSS       = 4,
    MU_RUI_SCREEN_MATCH     = 5
};

/* Match banner ids (matching Melee's ifStatus_803F9628 element indices + 1). */
enum MuRuiBanner {
    MU_RUI_BANNER_NONE         = 0,
    MU_RUI_BANNER_TIME         = 1, /* element 0 */
    MU_RUI_BANNER_SUDDEN_DEATH = 2, /* element 1 */
    MU_RUI_BANNER_READY        = 3, /* element 3 */
    MU_RUI_BANNER_GO           = 4, /* element 4 */
    MU_RUI_BANNER_GAME         = 5, /* element 5 */
    MU_RUI_BANNER_COMPLETE     = 6, /* element 6 */
    MU_RUI_BANNER_FAILURE      = 7  /* element 7 */
};

/* Canonical 2-row x 13-column Melee Redesigned CSS order (indices into Melee's 26-entry CSSIcon
 * table: 0..24 characters, 25 = Random). Shared by the game's 2x13 hit-test mapping and the host's
 * Redesigned CSS renderer so controller cursor movement and visuals match 1:1.
 *
 * Vanilla CSSIcon indices in mncharsel.c (mnCharSel_803F0A48.icons[0..25]):
 *   Row 0 (0..8):  Dr.Mario(0), Mario(1), Luigi(2), Bowser(3), Peach(4), Yoshi(5), DK(6), C.Falcon(7), Ganon(8)
 *   Row 1 (9..17): Falco(9), Fox(10), Ness(11), IceClimbers(12), Kirby(13), Samus(14), Zelda(15), Link(16), YLink(17)
 *   Row 2 (18..25): Pichu(18), Pikachu(19), Jigglypuff(20), Mewtwo(21), G&W(22), Marth(23), Roy(24), Random(25)
 *
 * Dave's 2x13 Redesigned roster layout (matching demo_thumb_1 / main_sb_06):
 *   Top row (13):    Dr.Mario(0), Mario(1), Luigi(2), Bowser(3), Peach(4), Yoshi(5), DK(6),
 *                    C.Falcon(7), Pikachu(19), Pichu(18), Jigglypuff(20), Mewtwo(21), G&W(22)
 *   Bottom row (13): Falco(9), Fox(10), Ness(11), IceClimbers(12), Kirby(13), Samus(14),
 *                    Link(16), Zelda(15), YLink(17), Ganon(8), Marth(23), Roy(24), Random(25)
 */
#define MU_RUI_CSS_COLS 13
#define MU_RUI_CSS_ROWS 2
#define MU_RUI_CSS_SLOTS 26

static inline uint8_t mu_rui_css_slot_to_icon(int slot)
{
    static const uint8_t map[MU_RUI_CSS_SLOTS] = {
        /* Top row (0..12) */
        0, 1, 2, 3, 4, 5, 6, 7, 19, 18, 20, 21, 22,
        /* Bottom row (13..25) */
        9, 10, 11, 12, 13, 14, 16, 15, 17, 8, 23, 24, 25
    };
    return (slot >= 0 && slot < MU_RUI_CSS_SLOTS) ? map[slot] : 25;
}

static inline int mu_rui_css_icon_to_slot(int icon_idx)
{
    int s;
    for (s = 0; s < MU_RUI_CSS_SLOTS; ++s) {
        if (mu_rui_css_slot_to_icon(s) == icon_idx) return s;
    }
    return -1;
}

/* Melee CSS world-space bounds for the 2x13 Redesigned roster strip (matches host screen placement
 * in menus.inl so the player's analog stick cursor lands directly on each tile). */
#define MU_RUI_CSS_GRID_LEFT   (-26.0f)
#define MU_RUI_CSS_GRID_RIGHT  (26.0f)
#define MU_RUI_CSS_GRID_TOP    (-4.5f)
#define MU_RUI_CSS_GRID_BOTTOM (-13.5f)

/* Canonical SSS primary competitive strip order (indices into mnStageSel_803F06D0[0..29]):
 *   29: Random
 *   25: Final Destination (stkind 0x20)
 *   24: Battlefield (stkind 0x1F)
 *   8:  Yoshi's Story (stkind 0x02)
 *   26: Dream Land 64 (stkind 0x1C)
 *   6:  Fountain of Dreams (stkind 0x08)
 *   18: Pokemon Stadium (stkind 0x03) — Counterpick
 */
#define MU_RUI_SSS_PRIMARY_COUNT 7
static inline uint8_t mu_rui_sss_primary_icon(int slot)
{
    static const uint8_t map[MU_RUI_SSS_PRIMARY_COUNT] = { 29, 25, 24, 8, 26, 6, 18 };
    return (slot >= 0 && slot < MU_RUI_SSS_PRIMARY_COUNT) ? map[slot] : 29;
}

/* Remaining 23 Melee stages shown on the secondary strip below so every vanilla stage stays
 * accessible with the stick in casual/offline play. */
#define MU_RUI_SSS_EXTRA_COUNT 23
static inline uint8_t mu_rui_sss_extra_icon(int slot)
{
    static const uint8_t map[MU_RUI_SSS_EXTRA_COUNT] = {
        0, 1, 2, 3, 4, 5, 7, 9, 10, 11, 12, 13, 14, 15, 16, 17, 19, 20, 21, 22, 23, 27, 28
    };
    return (slot >= 0 && slot < MU_RUI_SSS_EXTRA_COUNT) ? map[slot] : 0;
}

typedef struct MuRuiCssPort {
    uint8_t kind;         /* 0=HMN, 1=CPU, 3=Closed */
    uint8_t sel_icon;     /* 0..25 icon index (25=Random), 0xFF=none */
    uint8_t char_kind;    /* external CharacterKind (0..25), 0xFF=none */
    uint8_t costume;      /* 0..5 */
    uint8_t team;         /* 0=Red, 1=Blue, 2=Green */
    uint8_t cpu_level;    /* 1..9 */
    uint8_t locked;       /* 1 if coin placed / character confirmed */
    uint8_t holding_puck; /* 1 if cursor currently holds coin */
    float hand_x;         /* Melee CSS world X (-32..+32) */
    float hand_y;         /* Melee CSS world Y (-24..+24) */
    float puck_x;         /* coin world X */
    float puck_y;         /* coin world Y */
    char nametag[24];     /* ASCII nametag or online tag */
} MuRuiCssPort;

typedef struct MuRuiHudPlayer {
    uint8_t present;
    uint8_t is_cpu;
    uint8_t char_kind;    /* external CharacterKind 0..25, 0xFF=unknown */
    uint8_t costume;      /* 0..5 */
    int8_t team;          /* -1 not teams, 0=Red, 1=Blue, 2=Green */
    uint8_t stocks;       /* remaining stocks (0..99) */
    uint8_t alive;        /* 1 if active fighter exists */
    uint8_t hitstun;      /* 1 if in hitlag or damage state (drives subtle hit-shake) */
    int16_t damage;       /* displayed integer percent */
    int16_t nana_damage;  /* -1 if no Nana in play */
    float damage_exact;   /* exact float percent */
    float tag_x;          /* 640x480 screen X (NaN if none) */
    float tag_y;          /* 640x480 screen Y (NaN if none) */
    uint8_t tag_visible;  /* 1 if over-fighter P1/P2 marker is visible */
    uint8_t pad[3];
    char name[24];        /* nametag or online tag */
} MuRuiHudPlayer;

typedef struct MuRedesignedUiSnapshot {
    uint32_t valid;
    uint32_t frame_counter;
    uint32_t active_flags;     /* MU_RUI_FLAG_* currently active on this frame */
    uint8_t screen;            /* enum MuRuiScreen */
    uint8_t scene_major;
    uint8_t scene_minor;
    uint8_t is_online;
    uint8_t online_mode;       /* Slippi mode: 0 Ranked, 1 Unranked, 2 Direct, 3 Teams, 4 Party */
    uint8_t is_training;
    uint8_t is_replay;
    uint8_t vanilla_suppressed;/* 1 when the game skipped drawing vanilla UI for this screen */

    /* Title screen */
    uint8_t title_ready;
    uint8_t title_intro_active;
    uint8_t title_pad[2];

    /* Main menu & submenus */
    uint8_t menu_kind;         /* Melee MenuKind */
    uint8_t menu_prev;
    uint8_t menu_selection;    /* currently highlighted row */
    uint8_t menu_item_count;
    uint8_t menu_entering;
    uint8_t menu_supported;    /* 1 if Redesigned UI draws this MenuKind cleanly */
    uint8_t menu_online_page;  /* 0=YouGame/Primary, 1=Slippi */
    uint8_t menu_online_sub;   /* packed online slider hints */
    uint32_t menu_item_mask;   /* bit i set if row i is unlocked / visible */
    char menu_online_tag[32];

    /* Character Select Screen (CSS) */
    uint8_t css_active;
    uint8_t css_ready_to_fight;
    uint8_t css_is_teams;
    uint8_t css_match_type;
    uint8_t css_mode;          /* 0=Time, 1=Stock, 2=Coin, 3=Bonus */
    uint8_t css_stocks;
    uint8_t css_time_minutes;
    uint8_t css_door_count;
    uint8_t css_is_mex;        /* 1 -> fall back to vanilla CSS */
    uint8_t css_ready_timer;
    uint8_t css_pad[2];
    uint32_t css_unlocked_mask;/* bit i = CSSIcon i unlocked */
    MuRuiCssPort css_ports[4];

    /* Stage Select Screen (SSS) */
    uint8_t sss_active;
    uint8_t sss_is_mex;        /* 1 -> fall back to vanilla SSS */
    uint8_t sss_confirmed;
    uint8_t sss_loading;
    uint8_t sss_hovered_index; /* 0..28 stage, 29=Random, 30=None */
    uint8_t sss_hovered_stkind;
    uint8_t sss_frozen_stadium;
    uint8_t sss_pad;
    uint32_t sss_unlocked_mask;/* bit i = stage icon i unlocked */
    uint32_t sss_struck_mask;  /* bit i = stage icon i struck/banned */
    float sss_cursor_x;        /* Melee SSS world X (-27..+27) */
    float sss_cursor_y;        /* Melee SSS world Y (-19..+19) */

    /* Match HUD & Pause */
    uint8_t match_active;
    uint8_t match_paused;
    int8_t match_pauser;       /* 0..3 or -1 */
    uint8_t match_pause_flags; /* Melee pause flag bits (1=LRAS, 2=Z, 4=Stick) */
    uint8_t match_pause_cursor;/* 0=Continue, 1=Restart, 2=Camera, 3=Quit */
    uint8_t match_banner;      /* enum MuRuiBanner */
    uint8_t match_banner_frame;
    uint8_t match_is_teams;
    uint8_t match_is_stock;
    uint8_t match_is_stamina;
    uint8_t stage_kind;
    uint8_t timer_enabled;
    uint8_t timer_hidden;
    uint8_t timer_counts_up;
    uint8_t timer_shows_hours;
    uint8_t match_pad;
    int32_t timer_seconds;
    int32_t timer_centiseconds;
    MuRuiHudPlayer hud_players[4];
} MuRedesignedUiSnapshot;

#ifdef __cplusplus
}
#endif
#endif
