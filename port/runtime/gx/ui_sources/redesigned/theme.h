// Design system, palette, typography helpers, vector GameCube glyphs, and canonical Melee
// fighter/stage/menu tables for the optional Redesigned UI refresh.
// Clean-room implementation inspired by Dave Abarra's "Melee Redesigned" (XbPBV71g1_s).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace gx::redesigned::theme {

// ---- Core palette ----
inline ImU32 rgba(int r, int g, int b, float a = 1.0f) {
  return IM_COL32(r, g, b, (int)(255.0f * std::clamp(a, 0.0f, 1.0f)));
}

inline ImU32 bg_deep(float a = 1.0f)    { return rgba(7, 11, 18, a); }
inline ImU32 bg_mid(float a = 1.0f)     { return rgba(14, 22, 36, a); }
inline ImU32 bg_glow(float a = 1.0f)    { return rgba(26, 44, 72, a); }
inline ImU32 glass(float a = 0.78f)     { return rgba(10, 15, 24, a); }
inline ImU32 glass_hi(float a = 0.90f)  { return rgba(22, 31, 46, a); }
inline ImU32 white(float a = 1.0f)      { return rgba(246, 249, 253, a); }
inline ImU32 silver(float a = 1.0f)     { return rgba(164, 178, 198, a); }
inline ImU32 muted(float a = 1.0f)      { return rgba(106, 120, 140, a); }
inline ImU32 hairline(float a = 0.45f)  { return rgba(235, 242, 252, a); }
inline ImU32 crimson(float a = 0.88f)   { return rgba(198, 24, 46, a); }
inline ImU32 crimson_hi(float a = 1.0f) { return rgba(232, 46, 66, a); }
inline ImU32 emerald(float a = 1.0f)    { return rgba(44, 206, 140, a); }

inline ImU32 port_color(int port, int team = -1, bool is_cpu = false, float a = 1.0f) {
  if (team >= 0) {
    switch (team) {
      case 0: return rgba(230, 52, 68, a);   // Red team
      case 1: return rgba(52, 130, 246, a);  // Blue team
      case 2: return rgba(44, 196, 118, a);  // Green team
      default: break;
    }
  }
  if (is_cpu) return rgba(142, 154, 172, a);
  switch (port) {
    case 0: return rgba(230, 52, 68, a);   // P1 Crimson
    case 1: return rgba(52, 130, 246, a);  // P2 Cobalt
    case 2: return rgba(242, 184, 36, a);  // P3 Amber
    case 3: return rgba(44, 196, 118, a);  // P4 Emerald
    default: return rgba(180, 192, 210, a);
  }
}

// Smooth damage color ramp matching Dave's HUD (0% white -> 45% gold -> 95% orange -> 145%+ crimson)
inline ImU32 damage_color(float percent, float a = 1.0f) {
  const float p = std::clamp(percent, 0.0f, 220.0f);
  float r = 248.0f, g = 250.0f, b = 253.0f;
  if (p < 45.0f) {
    const float t = p / 45.0f;
    r = 248.0f + (255.0f - 248.0f) * t;
    g = 250.0f + (224.0f - 250.0f) * t;
    b = 253.0f + (116.0f - 253.0f) * t;
  } else if (p < 95.0f) {
    const float t = (p - 45.0f) / 50.0f;
    r = 255.0f;
    g = 224.0f + (132.0f - 224.0f) * t;
    b = 116.0f + (38.0f - 116.0f) * t;
  } else {
    const float t = std::clamp((p - 95.0f) / 55.0f, 0.0f, 1.0f);
    r = 255.0f + (236.0f - 255.0f) * t;
    g = 132.0f + (40.0f - 132.0f) * t;
    b = 38.0f + (54.0f - 38.0f) * t;
  }
  return rgba((int)r, (int)g, (int)b, a);
}

inline float ease_out_cubic(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  const float inv = 1.0f - t;
  return 1.0f - inv * inv * inv;
}

// ---- Typography helpers ----
struct Fonts {
  ImFont* display = nullptr;  // large condensed display (titles, MELEE, MAIN MENU, percents)
  ImFont* heading = nullptr;  // uppercase section & menu rows
  ImFont* body = nullptr;     // clean descriptions, prompts, tags
  ImFont* mono = nullptr;     // tabular numbers / timer
};

inline ImFont* pick_font(ImFont* preferred) {
  return preferred ? preferred : ImGui::GetFont();
}

inline float measure_tracked(ImFont* font, float size, const char* text, float tracking) {
  font = pick_font(font);
  if (!text || !*text) return 0.0f;
  if (tracking <= 0.01f) return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x;
  float total = 0.0f;
  char ch[2] = {0, 0};
  for (const char* p = text; *p; ++p) {
    ch[0] = *p;
    total += font->CalcTextSizeA(size, FLT_MAX, 0.0f, ch).x;
    if (p[1]) total += tracking;
  }
  return total;
}

inline void draw_text_tracked(ImDrawList* dl, ImFont* font, float size, ImVec2 pos,
                              ImU32 color, const char* text, float tracking = 0.0f,
                              bool shadow = true) {
  font = pick_font(font);
  if (!text || !*text) return;
  const ImU32 sh = IM_COL32(0, 4, 10, (int)(((color >> 24) & 0xFF) * 0.65f));
  if (tracking <= 0.01f) {
    if (shadow) dl->AddText(font, size, ImVec2(pos.x + 1.2f, pos.y + 1.5f), sh, text);
    dl->AddText(font, size, pos, color, text);
    return;
  }
  float x = pos.x;
  char ch[2] = {0, 0};
  for (const char* p = text; *p; ++p) {
    ch[0] = *p;
    if (shadow) dl->AddText(font, size, ImVec2(x + 1.2f, pos.y + 1.5f), sh, ch);
    dl->AddText(font, size, ImVec2(x, pos.y), color, ch);
    x += font->CalcTextSizeA(size, FLT_MAX, 0.0f, ch).x + tracking;
  }
}

// ---- Atmospheric dark-slate backdrop (Battlefield-inspired mood without copying art) ----
inline void draw_atmospheric_backdrop(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float t,
                                      ImU32 tint_top = 0, ImU32 tint_bot = 0) {
  const float w = p1.x - p0.x, h = p1.y - p0.y;
  const ImU32 tl = tint_top ? tint_top : rgba(8, 13, 22, 1.0f);
  const ImU32 tr = rgba(16, 28, 46, 1.0f);
  const ImU32 bl = tint_bot ? tint_bot : rgba(6, 9, 15, 1.0f);
  const ImU32 br = rgba(18, 26, 42, 1.0f);
  dl->AddRectFilledMultiColor(p0, p1, tl, tr, br, bl);

  // Diagonal nebula sheen across upper right (clean minimalist atmospheric depth)
  const ImVec2 c1(p0.x + w * (0.68f + 0.02f * std::sin(t * 0.25f)), p0.y + h * 0.34f);
  for (int i = 5; i >= 1; --i) {
    const float rad = h * (0.18f + i * 0.12f);
    dl->AddCircleFilled(c1, rad, rgba(44, 86, 146, 0.045f * (6 - i) * 0.38f), 48);
  }
  const ImVec2 c2(p0.x + w * 0.24f, p0.y + h * 0.78f);
  for (int i = 4; i >= 1; --i) {
    const float rad = h * (0.14f + i * 0.10f);
    dl->AddCircleFilled(c2, rad, rgba(28, 52, 88, 0.038f * (5 - i) * 0.35f), 40);
  }

  // Subtle floating dust / starlight motes
  for (int i = 0; i < 28; ++i) {
    const float seed = (float)i * 1.6180339f;
    const float fx = std::fmod(seed * 3.71f + t * (0.004f + (i % 5) * 0.0012f), 1.0f);
    const float fy = std::fmod(seed * 7.13f - t * (0.002f + (i % 4) * 0.0008f) + 10.0f, 1.0f);
    const float px = p0.x + fx * w;
    const float py = p0.y + fy * h;
    const float rad = (1.0f + (float)(i % 3) * 0.65f) * (h / 720.0f);
    const float pulse = 0.35f + 0.65f * (0.5f + 0.5f * std::sin(t * 1.3f + seed * 5.0f));
    dl->AddCircleFilled(ImVec2(px, py), rad, rgba(196, 218, 248, 0.16f * pulse), 8);
  }

  // Soft left-side reading vignette so white left-aligned typography pops effortlessly
  dl->AddRectFilledMultiColor(p0, ImVec2(p0.x + w * 0.45f, p1.y),
                              rgba(3, 5, 10, 0.55f), rgba(3, 5, 10, 0.0f),
                              rgba(3, 5, 10, 0.0f), rgba(3, 5, 10, 0.65f));
  // Bottom vignette
  dl->AddRectFilledMultiColor(ImVec2(p0.x, p1.y - h * 0.24f), p1,
                              rgba(3, 5, 10, 0.0f), rgba(3, 5, 10, 0.0f),
                              rgba(3, 5, 10, 0.72f), rgba(3, 5, 10, 0.72f));
}

// ---- Procedural vector GameCube button glyph ----
enum class GcButton { A, B, X, Y, Z, L, R, Start };

inline float draw_gc_prompt(ImDrawList* dl, ImFont* font, ImVec2 pos, float u,
                            GcButton btn, const char* label, float alpha = 1.0f) {
  font = pick_font(font);
  const float r = 10.5f * u;
  const bool pill = (btn == GcButton::Start);
  const float glyph_w = pill ? 42.0f * u : 2.0f * r;
  const ImVec2 center(pos.x + glyph_w * 0.5f, pos.y + r);

  ImU32 ring = rgba(190, 200, 216, 0.85f * alpha);
  ImU32 fill = rgba(14, 20, 30, 0.72f * alpha);
  const char* letter = "A";
  switch (btn) {
    case GcButton::A: ring = rgba(46, 204, 142, 0.95f * alpha); letter = "A"; break;
    case GcButton::B: ring = rgba(226, 56, 70, 0.95f * alpha);  letter = "B"; break;
    case GcButton::X: letter = "X"; break;
    case GcButton::Y: letter = "Y"; break;
    case GcButton::Z: ring = rgba(132, 118, 224, 0.90f * alpha); letter = "Z"; break;
    case GcButton::L: letter = "L"; break;
    case GcButton::R: letter = "R"; break;
    case GcButton::Start: letter = "START"; break;
  }

  if (pill) {
    const ImVec2 a(pos.x, center.y - r * 0.82f), b(pos.x + glyph_w, center.y + r * 0.82f);
    dl->AddRectFilled(a, b, fill, r * 0.82f);
    dl->AddRect(a, b, ring, r * 0.82f, 0, std::max(1.2f, 1.6f * u));
  } else {
    dl->AddCircleFilled(center, r, fill, 24);
    dl->AddCircle(center, r, ring, 24, std::max(1.3f, 1.8f * u));
  }

  const float lsz = (pill ? 10.5f : 12.5f) * u;
  const ImVec2 lsize = font->CalcTextSizeA(lsz, FLT_MAX, 0.0f, letter);
  dl->AddText(font, lsz, ImVec2(center.x - lsize.x * 0.5f, center.y - lsize.y * 0.5f - 0.5f * u),
              white(alpha), letter);

  float advance = glyph_w;
  if (label && *label) {
    const float tsz = 14.0f * u;
    const float tx = pos.x + glyph_w + 8.0f * u;
    const ImVec2 tsize = font->CalcTextSizeA(tsz, FLT_MAX, 0.0f, label);
    draw_text_tracked(dl, font, tsz, ImVec2(tx, center.y - tsize.y * 0.5f),
                      rgba(218, 226, 238, 0.92f * alpha), label, 0.4f * u, true);
    advance = glyph_w + 8.0f * u + tsize.x + 22.0f * u;
  } else {
    advance += 8.0f * u;
  }
  return advance;
}

// ---- Canonical Melee fighter metadata (indexed by External CharacterKind 0..25) ----
struct FighterInfo {
  const char* upper_name;
  const char* line1;
  const char* line2;
  const char* abbr;
  uint8_t r, g, b;   // signature emblem hue for fallback portraits
  uint8_t css_icon;  // vanilla CSSIcon index (0..25)
};

inline const FighterInfo& fighter_info(int ckind) {
  static const FighterInfo kUnknown = {"RANDOM", "RANDOM", "", "?", 120, 134, 156, 25};
  // Indexed by Melee external CharacterKind (CKind_*: 0..25)
  static const FighterInfo kTable[26] = {
    {"CAPTAIN FALCON", "CAPTAIN",  "FALCON",   "CF", 184,  52,  64,  7}, // 0
    {"DONKEY KONG",    "DONKEY",   "KONG",     "DK", 156,  92,  46,  6}, // 1
    {"FOX",            "FOX",      "",         "FX", 214, 146,  52, 10}, // 2
    {"MR. GAME & WATCH","MR. GAME","& WATCH",  "GW",  92, 102, 118, 22}, // 3
    {"KIRBY",          "KIRBY",    "",         "KB", 232, 128, 168, 13}, // 4
    {"BOWSER",         "BOWSER",   "",         "BW",  94, 152,  76,  3}, // 5
    {"LINK",           "LINK",     "",         "LK",  64, 162,  92, 16}, // 6
    {"LUIGI",          "LUIGI",    "",         "LG",  58, 178,  86,  2}, // 7
    {"MARIO",          "MARIO",    "",         "MR", 218,  56,  62,  1}, // 8
    {"MARTH",          "MARTH",    "",         "MT",  66, 118, 218, 23}, // 9
    {"MEWTWO",         "MEWTWO",   "",         "M2", 154, 104, 212, 21}, // 10
    {"NESS",           "NESS",     "",         "NS", 214,  72,  82, 11}, // 11
    {"PEACH",          "PEACH",    "",         "PC", 234, 138, 186,  4}, // 12
    {"PIKACHU",        "PIKACHU",  "",         "PK", 242, 198,  48, 19}, // 13
    {"ICE CLIMBERS",   "ICE",      "CLIMBERS", "IC",  98, 178, 232, 12}, // 14
    {"JIGGLYPUFF",     "JIGGLY",   "PUFF",     "JP", 240, 154, 196, 20}, // 15
    {"SAMUS",          "SAMUS",    "",         "SM", 224, 104,  48, 14}, // 16
    {"YOSHI",          "YOSHI",    "",         "YS",  92, 194,  78,  5}, // 17
    {"ZELDA",          "ZELDA",    "",         "ZD", 182,  92, 196, 15}, // 18
    {"SHEIK",          "SHEIK",    "",         "SK",  94, 124, 212, 15}, // 19
    {"FALCO",          "FALCO",    "",         "FL",  72, 138, 224,  9}, // 20
    {"YOUNG LINK",     "YOUNG",    "LINK",     "YL",  82, 182,  98, 17}, // 21
    {"DR. MARIO",      "DR.",      "MARIO",    "DM", 184, 196, 212,  0}, // 22
    {"ROY",            "ROY",      "",         "RY", 218,  72,  52, 24}, // 23
    {"PICHU",          "PICHU",    "",         "PU", 244, 214,  88, 18}, // 24
    {"GANONDORF",      "GANON",    "DORF",     "GN", 136,  72, 152,  8}, // 25
  };
  if (ckind >= 0 && ckind < 26) return kTable[ckind];
  return kUnknown;
}

// Map vanilla CSSIcon index (0..25) to external CharacterKind (0..25, or -1 for Random)
inline int css_icon_to_ckind(int icon_idx) {
  static const int8_t map[26] = {
    22, 8, 7, 5, 12, 17, 1, 0, 25,
    20, 2, 11, 14, 4, 16, 18, 6, 21,
    24, 13, 15, 10, 3, 9, 23, -1
  };
  return (icon_idx >= 0 && icon_idx < 26) ? map[icon_idx] : -1;
}

// ---- Canonical Melee stage metadata (indexed by mnStageSel_803F06D0[0..29]) ----
struct StageInfo {
  const char* name;
  const char* short_name;
  uint8_t r1, g1, b1;
  uint8_t r2, g2, b2;
  uint8_t stkind;
};

inline const StageInfo& stage_info_by_icon(int icon_idx) {
  static const StageInfo kRandom = {"RANDOM STAGE", "RANDOM", 34, 44, 62, 14, 20, 32, 0};
  static const StageInfo kTable[30] = {
    {"PRINCESS PEACH'S CASTLE", "PEACH'S CASTLE",   78, 132, 198,  32,  58,  98, 0x04}, // 0
    {"RAINBOW CRUISE",          "RAINBOW CRUISE",  112, 152, 224,  48,  76, 136, 0x0B}, // 1
    {"KONGO JUNGLE",            "KONGO JUNGLE",     56, 138,  86,  24,  62,  42, 0x05}, // 2
    {"JUNGLE JAPES",            "JUNGLE JAPES",     48, 124,  78,  20,  54,  38, 0x0C}, // 3
    {"GREAT BAY",               "GREAT BAY",        46, 134, 186,  20,  58,  92, 0x0D}, // 4
    {"HYRULE TEMPLE",           "TEMPLE",          116, 142, 188,  44,  62,  86, 0x0E}, // 5
    {"YOSHI'S STORY",           "YOSHI'S STORY",   106, 178,  92,  44,  92,  52, 0x08}, // 6
    {"GREEN GREENS",            "GREEN GREENS",     72, 168,  96,  28,  76,  44, 0x10}, // 7
    {"FOUNTAIN OF DREAMS",      "FOUNTAIN",         86,  72, 178,  24,  28,  76, 0x02}, // 8
    {"YOSHI'S ISLAND",          "YOSHI'S ISLAND",   92, 164, 118,  38,  84,  58, 0x11}, // 9
    {"CORNERIA",                "CORNERIA",         66,  94, 148,  26,  38,  68, 0x07}, // 10
    {"VENOM",                   "VENOM",           148,  98,  58,  62,  38,  24, 0x16}, // 11
    {"BRINSTAR",                "BRINSTAR",        162,  76,  52,  66,  28,  22, 0x06}, // 12
    {"BRINSTAR DEPTHS",         "BRINSTAR DEPTHS", 128,  54,  68,  48,  22,  30, 0x0F}, // 13
    {"ONETT",                   "ONETT",            94, 128, 176,  38,  56,  86, 0x09}, // 14
    {"FOURSIDE",                "FOURSIDE",         52,  66, 124,  18,  24,  52, 0x12}, // 15
    {"MUTE CITY",               "MUTE CITY",        68,  84, 168,  28,  32,  76, 0x0A}, // 16
    {"BIG BLUE",                "BIG BLUE",         48, 118, 198,  18,  48,  96, 0x18}, // 17
    {"POKEMON STADIUM",         "POKEMON STADIUM",  82, 116, 152,  32,  48,  72, 0x03}, // 18
    {"POKE FLOATS",             "POKE FLOATS",     116, 144, 196,  48,  66, 108, 0x17}, // 19
    {"MUSHROOM KINGDOM",        "MUSHROOM K. I",    86, 148, 198,  38,  72, 108, 0x13}, // 20
    {"MUSHROOM KINGDOM II",     "MUSHROOM K. II",   98, 154, 174,  44,  74,  92, 0x14}, // 21
    {"ICICLE MOUNTAIN",         "ICICLE MTN",      106, 162, 208,  42,  76, 112, 0x19}, // 22
    {"FLAT ZONE",               "FLAT ZONE",       112, 124, 108,  48,  54,  46, 0x1B}, // 23
    {"BATTLEFIELD",             "BATTLEFIELD",      52,  72, 128,  16,  24,  48, 0x1F}, // 24
    {"FINAL DESTINATION",       "FINAL DEST.",     134,  42, 158,  26,  14,  54, 0x20}, // 25
    {"DREAM LAND N64",          "DREAM LAND",       84, 158, 124,  34,  78,  66, 0x1C}, // 26
    {"YOSHI'S ISLAND N64",      "YOSHI N64",        92, 166,  98,  36,  82,  48, 0x1D}, // 27
    {"KONGO JUNGLE N64",        "KONGO N64",        64, 132,  82,  26,  58,  38, 0x1E}, // 28
    {"RANDOM STAGE",            "RANDOM",           44,  58,  82,  16,  22,  34, 0x00}, // 29
  };
  if (icon_idx >= 0 && icon_idx < 30) return kTable[icon_idx];
  return kRandom;
}

// ---- Canonical Melee Menu Definitions (indexed by Melee MenuKind) ----
struct MenuDef {
  const char* heading;
  int count;
  const char* items[8];
  const char* descs[8];
};

inline const MenuDef* menu_def(int menu_kind, int online_page) {
  static const MenuDef kMain = {
    "MAIN MENU", 5,
    {"1-P MODE", "VS. MODE", "TROPHIES", "OPTIONS", "DATA"},
    {
      "Play Single-Player modes, Training, or Online Play.",
      "Battle locally in Melee, Tournament, or Special Melee.",
      "Browse your trophy collection and lottery.",
      "Adjust audio, screen, rumble, language, and PC Settings.",
      "View snapshots, archives, sound test, and VS. records."
    }
  };
  static const MenuDef k1P = {
    "1-P MODE", 5,
    {"REGULAR MATCH", "EVENT MATCH", "ONLINE PLAY", "STADIUM", "TRAINING"},
    {
      "Play Classic, Adventure, or All-Star single-player campaigns.",
      "Take on themed challenges and event battles.",
      "Play online with YouGame and Slippi rollback netplay.",
      "Compete in Target Test, Home-Run Contest, and Multi-Man Melee.",
      "Practice combos, movement, and setups freely."
    }
  };
  static const MenuDef kReg = {
    "REGULAR MATCH", 3,
    {"CLASSIC", "ADVENTURE", "ALL-STAR"},
    {
      "Fight through randomized stages and face Master Hand.",
      "Journey across themed Nintendo worlds.",
      "Battle every fighter in the roster on one life."
    }
  };
  static const MenuDef kVs = {
    "VS. MODE", 5,
    {"MELEE", "TOURNAMENT MELEE", "SPECIAL MELEE", "CUSTOM RULES", "NAME ENTRY"},
    {
      "Standard multiplayer Smash battle with custom rules.",
      "Bracket tournament play for up to 64 entrants.",
      "Play Camera, Stamina, Super Sudden Death, Giant, and more.",
      "Configure stock count, time limit, items, and stage select.",
      "Create and manage player name tags."
    }
  };
  static const MenuDef kOnlineYouGame = {
    "ONLINE PLAY", 6,
    {"YOUGAME RANKED", "YOUGAME UNRANKED", "YOUGAME DOUBLES", "YOUGAME FRIENDS", "SQUAD STRIKE", "TOURNAMENT"},
    {
      "Competitive ranked sets with matchmaking rating.",
      "Quick rollback matchmaking (Left/Right switches service).",
      "2v2 doubles rollback netplay with YouGame matchmaking.",
      "Join or host a lobby with friends.",
      "4-fighter elimination sets against online opponents.",
      "Enter scheduled bracket tournaments."
    }
  };
  static const MenuDef kOnlineSlippi = {
    "SLIPPI ONLINE", 4,
    {"SLIPPI DIRECT", "SLIPPI UNRANKED", "SLIPPI TEAMS", "SLIPPI PARTY"},
    {
      "Connect directly to a specific opponent by connect code.",
      "Play Slippi unranked rollback matches (Left/Right switches service).",
      "2v2 doubles rollback netplay on Slippi.",
      "Create or join a Slippi party lobby."
    }
  };
  static const MenuDef kStadium = {
    "STADIUM", 3,
    {"TARGET TEST", "HOME-RUN CONTEST", "MULTI-MAN MELEE"},
    {
      "Break all 10 targets as fast as possible.",
      "Launch the Sandbag as far as you can in 10 seconds.",
      "Survive waves of Wire Frames in Endless, 100-Man, or Cruel Melee."
    }
  };
  static const MenuDef kSpecial = {
    "SPECIAL MELEE", 8,
    {"CAMERA MODE", "STAMINA MODE", "SUPER SUDDEN DEATH", "GIANT MELEE",
     "TINY MELEE", "INVISIBLE MELEE", "FIXED-CAMERA", "SINGLE-BUTTON"},
    {
      "Pause freely and take snapshots during battles.",
      "Fight with HP bars instead of knockback percents.",
      "Every fighter starts at 300% damage.",
      "All fighters are permanently giant sized.",
      "All fighters are permanently tiny sized.",
      "All fighters turn completely invisible.",
      "Battle with a locked wide stage camera.",
      "Play using only the A button and control stick."
    }
  };
  static const MenuDef kToy = {
    "TROPHIES", 4,
    {"GALLERY", "LOTTERY", "", "COLLECTION"},
    {
      "Inspect unlocked trophies up close.",
      "Spend coins to unlock new trophies.",
      "",
      "Browse trophies sorted by series and category."
    }
  };
  static const MenuDef kSettings = {
    "OPTIONS", 6,
    {"RUMBLE", "SOUND", "SCREEN DISPLAY", "PC SETTINGS", "LANGUAGE", "ERASE DATA"},
    {
      "Toggle controller vibration per port.",
      "Configure music/SFX balance and mono/stereo audio.",
      "Adjust screen offset and deflicker.",
      "Open Melee Unlocked graphics, audio, controller, and UI settings.",
      "Switch between English and Japanese text.",
      "Reset save data on Memory Card Slot A."
    }
  };
  static const MenuDef kData = {
    "DATA", 5,
    {"SNAPSHOTS", "ARCHIVES", "SOUND TEST", "VS. RECORDS", "SPECIAL MOVIE"},
    {
      "Browse saved camera mode photographs.",
      "View notices, milestones, and unlocked fighters.",
      "Listen to Melee music tracks and sound effects.",
      "Inspect match history, KO counts, and fighter stats.",
      "Watch unlocked movies and credits."
    }
  };

  switch (menu_kind) {
    case 0: return &kMain;
    case 1: return &k1P;
    case 2: return &kVs;
    case 3: return &kToy;
    case 4: return &kSettings;
    case 5: return &kData;
    case 6: return &kReg;
    case 8: return (online_page == 1) ? &kOnlineSlippi : &kOnlineYouGame;
    case 9: return &kStadium;
    case 12: return &kSpecial;
    default: return nullptr;
  }
}

}  // namespace gx::redesigned::theme
