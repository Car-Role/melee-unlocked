// Matching "Redesigned" F1 PC Settings appearance (overlay_style == 8) inspired by
// Dave Abarra's "Melee Redesigned" visual system: dark obsidian-slate glass, condensed uppercase
// typography, crisp white hairlines, and a clean left triangle-cursor category rail.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "theme.h"

namespace gx::redesigned::settings_theme {

// 4 curated accents for overlay_style == 8:
// 0: Tournament Crimson, 1: Battlefield Cobalt, 2: Emerald Stock, 3: Obsidian Platinum
inline ImU32 accent_for(int variant) {
  static constexpr ImU32 kAccents[4] = {
    IM_COL32(230, 52, 68, 255),
    IM_COL32(52, 130, 246, 255),
    IM_COL32(44, 206, 140, 255),
    IM_COL32(226, 234, 246, 255),
  };
  return kAccents[std::clamp(variant, 0, 3)];
}

inline void draw_panel_backdrop(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float transparency_scale,
                                ImU32 accent) {
  const float a = std::clamp(transparency_scale, 0.25f, 1.0f);
  dl->AddRectFilledMultiColor(p0, p1,
                              theme::rgba(8, 13, 22, 0.94f * a),
                              theme::rgba(13, 21, 35, 0.94f * a),
                              theme::rgba(9, 15, 25, 0.96f * a),
                              theme::rgba(6, 10, 17, 0.96f * a));
  // Subtle top accent hairline + clean glass outer border
  dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + 2.5f), accent);
  dl->AddRect(p0, p1, theme::hairline(0.26f * a), 2.0f, 0, 1.2f);
}

inline void draw_header(ImDrawList* dl, ImFont* display_font, ImFont* body_font,
                        ImVec2 p0, float width, const char* active_category, ImU32 accent) {
  ImFont* f_disp = theme::pick_font(display_font);
  ImFont* f_body = theme::pick_font(body_font);
  theme::draw_text_tracked(dl, f_body, 11.5f, ImVec2(p0.x + 22.0f, p0.y + 12.0f),
                           theme::silver(0.75f), "MELEE UNLOCKED  |  REDESIGNED", 2.2f, false);
  theme::draw_text_tracked(dl, f_disp, 30.0f, ImVec2(p0.x + 20.0f, p0.y + 26.0f),
                           theme::white(1.0f), "PC SETTINGS", 2.8f, true);
  if (active_category && *active_category) {
    char upper[64];
    size_t n = 0;
    for (; active_category[n] && n + 1 < sizeof upper; ++n) {
      upper[n] = (char)std::toupper((unsigned char)active_category[n]);
    }
    upper[n] = '\0';
    const float cw = theme::measure_tracked(f_body, 13.0f, upper, 2.2f);
    theme::draw_text_tracked(dl, f_body, 13.0f, ImVec2(p0.x + width - 64.0f - cw, p0.y + 38.0f),
                             accent, upper, 2.2f, false);
  }
  dl->AddLine(ImVec2(p0.x + 20.0f, p0.y + 62.0f), ImVec2(p0.x + width - 20.0f, p0.y + 62.0f),
              theme::hairline(0.36f), 1.2f);
}

inline void draw_nav_item(ImDrawList* dl, ImFont* head_font, ImVec2 a, ImVec2 b,
                          const char* label, bool selected, bool hovered, ImU32 accent) {
  if (selected) {
    dl->AddRectFilledMultiColor(a, b,
                                theme::rgba(255, 255, 255, 0.11f),
                                theme::rgba(255, 255, 255, 0.02f),
                                theme::rgba(255, 255, 255, 0.02f),
                                theme::rgba(255, 255, 255, 0.09f));
    dl->AddRectFilled(a, ImVec2(a.x + 3.0f, b.y), accent);
    const float cy = (a.y + b.y) * 0.5f;
    dl->AddTriangleFilled(ImVec2(a.x + 11.0f, cy - 4.5f), ImVec2(a.x + 11.0f, cy + 4.5f),
                          ImVec2(a.x + 18.0f, cy), theme::white(1.0f));
  } else if (hovered) {
    dl->AddRectFilled(a, b, theme::rgba(255, 255, 255, 0.05f), 2.0f);
  }
  char upper[48];
  size_t n = 0;
  for (; label && label[n] && n + 1 < sizeof upper; ++n) {
    upper[n] = (char)std::toupper((unsigned char)label[n]);
  }
  upper[n] = '\0';
  ImFont* f = theme::pick_font(head_font);
  const float sz = selected ? 15.5f : 14.0f;
  const float tx = a.x + (selected ? 26.0f : 16.0f);
  const float ty = (a.y + b.y - sz) * 0.5f;
  theme::draw_text_tracked(dl, f, sz, ImVec2(tx, ty),
                           selected ? theme::white(1.0f) : theme::silver(hovered ? 0.94f : 0.72f),
                           upper, 1.5f, false);
}

}  // namespace gx::redesigned::settings_theme
