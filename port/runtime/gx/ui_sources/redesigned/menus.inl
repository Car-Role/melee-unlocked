// Title Screen, Main Menu & Submenus, Character Select Screen (2x13), and Stage Select Screen
// (Horizontal Competitive Strip + Secondary All-Stages Grid) for the optional Redesigned UI.
// Clean-room ImGui implementation inspired by Dave Abarra's "Melee Redesigned" (XbPBV71g1_s & ZC3DofH1h7Q).
// SPDX-License-Identifier: GPL-2.0-or-later

namespace gx::redesigned::menus {

struct MenuAnimState {
  int last_menu_kind = -1;
  float menu_enter_t = 1.0f;
  float cursor_y_smooth = -1.0f;
  float ready_banner_alpha = 0.0f;
  float sss_hover_lift[30] = {};
};

inline float smooth_step(float cur, float target, float speed, float dt) {
  const float k = 1.0f - std::exp(-speed * std::clamp(dt, 0.001f, 0.1f));
  return cur + (target - cur) * k;
}

// ---- Subtle Smash emblem watermark drawn softly on right side of dark-slate menus ----
inline void draw_smash_watermark(ImDrawList* dl, ImVec2 center, float rad, float alpha) {
  dl->AddCircle(center, rad, theme::white(alpha), 64, std::max(2.0f, rad * 0.035f));
  // Horizontal cut
  dl->AddLine(ImVec2(center.x - rad * 1.08f, center.y + rad * 0.28f),
              ImVec2(center.x + rad * 1.08f, center.y + rad * 0.28f),
              theme::white(alpha * 1.1f), std::max(3.0f, rad * 0.09f));
  // Vertical cut
  dl->AddLine(ImVec2(center.x - rad * 0.28f, center.y - rad * 1.08f),
              ImVec2(center.x - rad * 0.28f, center.y + rad * 1.08f),
              theme::white(alpha * 1.1f), std::max(5.0f, rad * 0.16f));
}

// ============================================================================================
// 1. TITLE SCREEN (translucent scrim over Melee's live animated background!)
// ============================================================================================
inline void draw_title(ImDrawList* dl, const theme::Fonts& fonts, const MuRedesignedUiSnapshot& snap,
                       ImVec2 pic_min, ImVec2 pic_max, float u, float t) {
  const float w = pic_max.x - pic_min.x, h = pic_max.y - pic_min.y;
  const float cx = (pic_min.x + pic_max.x) * 0.5f;
  const float cy = pic_min.y + h * 0.44f;

  // Translucent dark-slate atmospheric vignette so Melee's live animated title background glows through
  dl->AddRectFilledMultiColor(pic_min, ImVec2(pic_max.x, pic_min.y + h * 0.50f),
                              theme::rgba(6, 10, 18, 0.72f), theme::rgba(8, 14, 24, 0.72f),
                              theme::rgba(10, 16, 28, 0.44f), theme::rgba(10, 16, 28, 0.44f));
  dl->AddRectFilledMultiColor(ImVec2(pic_min.x, pic_min.y + h * 0.50f), pic_max,
                              theme::rgba(10, 16, 28, 0.44f), theme::rgba(10, 16, 28, 0.44f),
                              theme::rgba(5, 8, 15, 0.84f), theme::rgba(5, 8, 15, 0.84f));

  // Subtle ambient halo behind the logo
  for (int i = 4; i >= 1; --i) {
    dl->AddCircleFilled(ImVec2(cx, cy), h * (0.14f + i * 0.08f),
                        theme::rgba(42, 76, 128, 0.055f * (5 - i) * 0.45f), 48);
  }

  ImFont* f_disp = theme::pick_font(fonts.display ? fonts.display : fonts.heading);
  ImFont* f_head = theme::pick_font(fonts.heading ? fonts.heading : fonts.body);
  ImFont* f_body = theme::pick_font(fonts.body ? fonts.body : fonts.heading);

  // "SUPER SMASH BROS." eyebrow
  const char* eyebrow = "SUPER SMASH BROS.";
  const float eye_sz = 19.0f * u;
  const float eye_tr = 7.5f * u;
  const float eye_w = theme::measure_tracked(f_head, eye_sz, eyebrow, eye_tr);
  theme::draw_text_tracked(dl, f_head, eye_sz, ImVec2(cx - eye_w * 0.5f, cy - 82.0f * u),
                           theme::white(0.90f), eyebrow, eye_tr, true);

  // Giant crisp uppercase "MELEE"
  const char* melee = "MELEE";
  const float m_sz = 98.0f * u;
  const float m_tr = 15.0f * u;
  const float m_w = theme::measure_tracked(f_disp, m_sz, melee, m_tr);
  theme::draw_text_tracked(dl, f_disp, m_sz, ImVec2(cx - m_w * 0.5f, cy - 58.0f * u),
                           theme::white(1.0f), melee, m_tr, true);

  // Thin crisp white horizontal hairline
  const float hl_y = cy + 48.0f * u;
  const float hl_half = 176.0f * u;
  dl->AddLine(ImVec2(cx - hl_half, hl_y), ImVec2(cx + hl_half, hl_y),
              theme::white(0.75f), std::max(1.2f, 1.5f * u));

  // Tracked subtitle
  const char* sub = "MELEE UNLOCKED";
  const float sub_sz = 14.5f * u;
  const float sub_tr = 7.0f * u;
  const float sub_w = theme::measure_tracked(f_head, sub_sz, sub, sub_tr);
  theme::draw_text_tracked(dl, f_head, sub_sz, ImVec2(cx - sub_w * 0.5f, hl_y + 14.0f * u),
                           theme::silver(0.88f), sub, sub_tr, true);

  // Pulsing "PRESS START" (never drops below 58% alpha so it always remains readable)
  const char* prompt = snap.title_intro_active ? "INITIALIZING..." : "PRESS START";
  const float pulse = snap.title_intro_active ? 0.65f : (0.79f + 0.21f * std::sin(t * 3.0f));
  const float pr_sz = 18.0f * u;
  const float pr_tr = 5.5f * u;
  const float pr_w = theme::measure_tracked(f_head, pr_sz, prompt, pr_tr);
  theme::draw_text_tracked(dl, f_head, pr_sz, ImVec2(cx - pr_w * 0.5f, pic_min.y + h * 0.76f),
                           theme::white(pulse), prompt, pr_tr, true);

  // Bottom minimal version / hint line
  const char* foot = "REDESIGNED UI  |  F1 PC SETTINGS";
  const float ft_sz = 11.5f * u;
  const float ft_tr = 2.6f * u;
  const float ft_w = theme::measure_tracked(f_body, ft_sz, foot, ft_tr);
  theme::draw_text_tracked(dl, f_body, ft_sz, ImVec2(cx - ft_w * 0.5f, pic_max.y - 30.0f * u),
                           theme::muted(0.85f), foot, ft_tr, true);
}

// ============================================================================================
// 2. MAIN MENU & SUBMENUS
// ============================================================================================
inline void draw_main_menu(ImDrawList* dl, const theme::Fonts& fonts, MenuAnimState& anim,
                           const MuRedesignedUiSnapshot& snap, ImVec2 pic_min, ImVec2 pic_max,
                           float u, float t, float dt) {
  if (!snap.menu_supported) return;
  const auto* def = theme::menu_def(snap.menu_kind, (int)snap.menu_online_page);
  if (!def) return;

  const int anim_key = (int)snap.menu_kind * 4 + (int)snap.menu_online_page;
  if (anim.last_menu_kind != anim_key) {
    anim.last_menu_kind = anim_key;
    anim.menu_enter_t = 0.0f;
    anim.cursor_y_smooth = -1.0f;
  }
  anim.menu_enter_t = std::min(1.0f, anim.menu_enter_t + dt * 4.2f);
  const float ep = theme::ease_out_cubic(anim.menu_enter_t);

  const float w = pic_max.x - pic_min.x, h = pic_max.y - pic_min.y;
  theme::draw_atmospheric_backdrop(dl, pic_min, pic_max, t);
  draw_smash_watermark(dl, ImVec2(pic_min.x + w * 0.78f, pic_min.y + h * 0.48f), h * 0.34f, 0.055f);

  ImFont* f_disp = theme::pick_font(fonts.display ? fonts.display : fonts.heading);
  ImFont* f_head = theme::pick_font(fonts.heading ? fonts.heading : fonts.body);
  ImFont* f_body = theme::pick_font(fonts.body ? fonts.body : fonts.heading);

  const float lx = pic_min.x + w * 0.085f - (1.0f - ep) * 18.0f * u;
  const float top_y = pic_min.y + h * 0.13f;

  // Submenu breadcrumb eyebrow
  if (snap.menu_kind != 0) {
    char crumb[64];
    std::snprintf(crumb, sizeof crumb, "MAIN MENU  /  %s", def->heading);
    theme::draw_text_tracked(dl, f_body, 12.5f * u, ImVec2(lx, top_y - 22.0f * u),
                             theme::silver(0.72f * ep), crumb, 2.4f * u, true);
  } else {
    theme::draw_text_tracked(dl, f_body, 12.5f * u, ImVec2(lx, top_y - 22.0f * u),
                             theme::silver(0.72f * ep), "SUPER SMASH BROS. MELEE", 2.8f * u, true);
  }

  // Huge condensed uppercase heading
  theme::draw_text_tracked(dl, f_disp, 56.0f * u, ImVec2(lx - 2.0f * u, top_y),
                           theme::white(ep), def->heading, 3.6f * u, true);

  // Crisp hairline under heading
  const float hl_y = top_y + 68.0f * u;
  dl->AddLine(ImVec2(lx, hl_y), ImVec2(lx + 420.0f * u, hl_y), theme::hairline(0.55f * ep), std::max(1.0f, 1.4f * u));

  // Online page sub-badge on MENU_KIND_8
  if (snap.menu_kind == 8) {
    const char* obuf = (snap.menu_online_page == 1)
                           ? "SLIPPI PAGE  |  Z FOR YOUGAME"
                           : "YOUGAME PAGE  |  Z FOR SLIPPI";
    theme::draw_text_tracked(dl, f_body, 12.0f * u, ImVec2(lx + 440.0f * u, top_y + 42.0f * u),
                             theme::emerald(0.90f * ep), obuf, 2.0f * u, true);
  }

  // Collect visible rows, skipping Melee's hidden placeholder slots (e.g. SEL_1P_2 offline, SEL_TOY_2)
  int max_items = def->count;
  if (snap.menu_item_count > 0 && snap.menu_item_count < max_items) {
    max_items = (int)snap.menu_item_count;
  }
  int shown_idx[8];
  int visible_count = 0;
  for (int i = 0; i < max_items && i < 8; ++i) {
    if (!def->items[i] || !def->items[i][0]) continue;
    if (snap.menu_item_mask != 0u && (((snap.menu_item_mask >> i) & 1u) == 0u) &&
        i != (int)snap.menu_selection) {
      continue;
    }
    shown_idx[visible_count++] = i;
  }
  if (visible_count == 0) {
    shown_idx[0] = 0;
    visible_count = 1;
  }

  int sel_row = 0;
  for (int r = 0; r < visible_count; ++r) {
    if (shown_idx[r] == (int)snap.menu_selection) {
      sel_row = r;
      break;
    }
  }
  const int sel_item = shown_idx[sel_row];

  const float row_h = (visible_count >= 7) ? 38.0f * u : 46.0f * u;
  const float list_top = hl_y + 24.0f * u;
  const float target_cy = list_top + (float)sel_row * row_h + row_h * 0.5f;
  if (anim.cursor_y_smooth < 0.0f) anim.cursor_y_smooth = target_cy;
  else anim.cursor_y_smooth = smooth_step(anim.cursor_y_smooth, target_cy, 24.0f, dt);

  // Subtle horizontal highlight bar tracking the selected row smoothly
  const ImVec2 h0(lx - 10.0f * u, anim.cursor_y_smooth - row_h * 0.44f);
  const ImVec2 h1(lx + 420.0f * u, anim.cursor_y_smooth + row_h * 0.44f);
  dl->AddRectFilledMultiColor(h0, h1,
                              theme::rgba(255, 255, 255, 0.10f * ep),
                              theme::rgba(255, 255, 255, 0.0f),
                              theme::rgba(255, 255, 255, 0.0f),
                              theme::rgba(255, 255, 255, 0.08f * ep));

  // Crisp white triangle cursor
  const float tri_y = anim.cursor_y_smooth;
  dl->AddTriangleFilled(ImVec2(lx, tri_y - 5.5f * u), ImVec2(lx, tri_y + 5.5f * u),
                        ImVec2(lx + 9.0f * u, tri_y), theme::white(ep));

  for (int r = 0; r < visible_count; ++r) {
    const int idx = shown_idx[r];
    const char* label = def->items[idx];
    const bool selected = (r == sel_row);
    const bool enabled = (snap.menu_item_mask == 0u) || (((snap.menu_item_mask >> idx) & 1u) != 0u);
    const float ry = list_top + (float)r * row_h;
    const float fsz = (selected ? 25.0f : 21.0f) * u * (visible_count >= 7 ? 0.90f : 1.0f);
    const float tx = lx + (selected ? 22.0f * u : 16.0f * u);
    const ImU32 col = !enabled ? theme::muted(0.45f * ep)
                               : (selected ? theme::white(ep) : theme::silver(0.74f * ep));
    theme::draw_text_tracked(dl, f_head, fsz, ImVec2(tx, ry + (row_h - fsz) * 0.5f),
                             col, label, selected ? 2.4f * u : 1.8f * u, true);
  }

  // Hairline + description block underneath the menu list
  const float desc_hl_y = list_top + (float)visible_count * row_h + 18.0f * u;
  dl->AddLine(ImVec2(lx, desc_hl_y), ImVec2(lx + 420.0f * u, desc_hl_y),
              theme::hairline(0.32f * ep), std::max(1.0f, 1.2f * u));
  const char* desc = (sel_item < def->count && def->descs[sel_item]) ? def->descs[sel_item] : "";
  if (*desc) {
    dl->AddText(f_body, 14.5f * u, ImVec2(lx, desc_hl_y + 12.0f * u),
                theme::silver(0.86f * ep), desc, nullptr, 460.0f * u);
  }

  // Bottom vector GameCube prompts
  const float by = pic_max.y - 46.0f * u;
  float px = lx;
  px += theme::draw_gc_prompt(dl, f_body, ImVec2(px, by), u, theme::GcButton::A, "Select", ep);
  if (snap.menu_kind != 0) {
    px += theme::draw_gc_prompt(dl, f_body, ImVec2(px, by), u, theme::GcButton::B, "Back", ep);
  }
  if (snap.menu_kind == 8 && snap.menu_online_sub) {
    px += theme::draw_gc_prompt(dl, f_body, ImVec2(px, by), u, theme::GcButton::Z,
                                snap.menu_online_page == 1 ? "YouGame" : "Slippi", ep);
  }
  const char* f1 = "F1  PC SETTINGS";
  const float f1_w = theme::measure_tracked(f_body, 12.5f * u, f1, 1.8f * u);
  theme::draw_text_tracked(dl, f_body, 12.5f * u, ImVec2(pic_max.x - w * 0.065f - f1_w, by + 4.0f * u),
                           theme::muted(0.90f * ep), f1, 1.8f * u, true);
}

// ============================================================================================
// 3. CHARACTER SELECT SCREEN (CSS — Compact 2x13 Grid + Hero Portraits + Ready To Fight)
// ============================================================================================
inline void draw_css(ImDrawList* dl, const theme::Fonts& fonts, assets::DiscAssetBank& bank,
                     MenuAnimState& anim, const MuRedesignedUiSnapshot& snap,
                     ImVec2 pic_min, ImVec2 pic_max, float u, float t, float dt) {
  const float w = pic_max.x - pic_min.x, h = pic_max.y - pic_min.y;
  theme::draw_atmospheric_backdrop(dl, pic_min, pic_max, t);

  ImFont* f_disp = theme::pick_font(fonts.display ? fonts.display : fonts.heading);
  ImFont* f_head = theme::pick_font(fonts.heading ? fonts.heading : fonts.body);
  ImFont* f_body = theme::pick_font(fonts.body ? fonts.body : fonts.heading);

  const float margin_x = w * 0.045f;

  // ---- Top Header Bar ----
  const float hdr_y = pic_min.y + 18.0f * u;
  const char* mode_title = "MELEE";
  if (snap.is_online) {
    switch (snap.online_mode) {
      case 0: mode_title = "ONLINE  |  RANKED"; break;
      case 1: mode_title = "ONLINE  |  UNRANKED"; break;
      case 2: mode_title = "ONLINE  |  DIRECT"; break;
      case 3: mode_title = "ONLINE  |  TEAMS"; break;
      default: mode_title = "ONLINE PLAY"; break;
    }
  } else if (snap.is_training || snap.css_match_type == 1) {
    mode_title = "TRAINING";
  } else if (snap.css_is_teams) {
    mode_title = "TEAM MELEE";
  }
  theme::draw_text_tracked(dl, f_disp, 30.0f * u, ImVec2(pic_min.x + margin_x, hdr_y),
                           theme::white(1.0f), mode_title, 3.0f * u, true);

  // Rules strip right-aligned on header bar (STOCK N | TIME | SINGLES/TEAMS)
  char rules_buf[96];
  if (snap.is_training || snap.css_match_type == 1) {
    std::snprintf(rules_buf, sizeof rules_buf, "FREE PRACTICE");
  } else if (snap.css_mode == 0) {
    std::snprintf(rules_buf, sizeof rules_buf, "TIME  %u:00   |   %s",
                  (unsigned)(snap.css_time_minutes ? snap.css_time_minutes : 2),
                  snap.css_is_teams ? "TEAMS" : "FREE FOR ALL");
  } else {
    std::snprintf(rules_buf, sizeof rules_buf, "STOCK  %u   |   %u:00   |   %s",
                  (unsigned)(snap.css_stocks ? snap.css_stocks : 4),
                  (unsigned)(snap.css_time_minutes ? snap.css_time_minutes : 8),
                  snap.css_is_teams ? "TEAMS" : "SINGLES");
  }
  const float rsz = 15.0f * u;
  const float rw = theme::measure_tracked(f_head, rsz, rules_buf, 2.2f * u);
  theme::draw_text_tracked(dl, f_head, rsz, ImVec2(pic_max.x - margin_x - rw, hdr_y + 8.0f * u),
                           theme::silver(0.92f), rules_buf, 2.2f * u, true);

  // Full-width crisp hairline below header
  const float hdr_hl_y = hdr_y + 38.0f * u;
  dl->AddLine(ImVec2(pic_min.x + margin_x, hdr_hl_y), ImVec2(pic_max.x - margin_x, hdr_hl_y),
              theme::hairline(0.55f), std::max(1.0f, 1.3f * u));

  // ---- Upper Hero Portraits Area (up to 4 player slots) ----
  const int doors = (snap.css_door_count == 2) ? 2 : 4;
  const float hero_top = hdr_hl_y + 14.0f * u;
  const float hero_bot = pic_min.y + h * 0.565f;
  const float hero_h = hero_bot - hero_top;
  const float hero_gap = 14.0f * u;
  const float hero_total_w = w - 2.0f * margin_x;
  const float slot_w = (hero_total_w - hero_gap * (doors - 1)) / (float)doors;
  const float bar_h = 34.0f * u;

  for (int i = 0; i < doors; ++i) {
    const auto& cp = snap.css_ports[i];
    const float sx = pic_min.x + margin_x + (float)i * (slot_w + hero_gap);
    const ImVec2 sa(sx, hero_top);
    const ImVec2 sb(sx + slot_w, hero_bot);
    const ImVec2 bar_a(sx, hero_bot - bar_h);
    const ImVec2 bar_b(sx + slot_w, hero_bot);

    const bool absent = (cp.kind == 3);
    if (absent) {
      // Minimalist open port slot
      dl->AddRectFilled(sa, sb, theme::rgba(10, 15, 25, 0.34f), 2.0f * u);
      dl->AddRect(sa, sb, theme::hairline(0.14f), 2.0f * u, 0, 1.0f);
      dl->AddRectFilled(bar_a, bar_b, theme::rgba(20, 28, 42, 0.60f));
      char pempty[24];
      std::snprintf(pempty, sizeof pempty, "PORT %d  -  OPEN", i + 1);
      theme::draw_text_tracked(dl, f_body, 12.5f * u, ImVec2(sx + 12.0f * u, bar_a.y + 9.0f * u),
                               theme::muted(0.75f), pempty, 1.8f * u, false);
      continue;
    }

    const bool is_cpu = (cp.kind == 1);
    const int ckind = (cp.char_kind < 26) ? (int)cp.char_kind : -1;
    const auto& fi = theme::fighter_info(ckind);
    const ImU32 pcol = theme::port_color(i, snap.css_is_teams ? (int)cp.team : -1, is_cpu, 1.0f);

    // Portrait area above nameplate bar
    const ImVec2 pa(sx, hero_top), pb(sx + slot_w, bar_a.y);
    dl->AddRectFilled(pa, pb, theme::rgba(12, 18, 30, 0.72f), 2.0f * u);

    if (ckind >= 0) {
      ImTextureData* ctex = bank.csp_portrait(ckind, cp.costume);
      if (!ctex) ctex = bank.css_icon(ckind);
      assets::draw_fighter_portrait_card(dl, f_disp, pa, pb, ckind, cp.costume, ctex,
                                         cp.locked ? 1.0f : 0.78f, true);
      // Subtle lower gradient so bold 2-line fighter name pops cleanly
      dl->AddRectFilledMultiColor(ImVec2(pa.x, pb.y - hero_h * 0.48f), pb,
                                  theme::rgba(6, 10, 18, 0.0f), theme::rgba(6, 10, 18, 0.0f),
                                  theme::rgba(6, 10, 18, 0.88f), theme::rgba(6, 10, 18, 0.88f));

      // Costume indicator pips top-right
      const float pip_y = pa.y + 14.0f * u;
      for (int c = 0; c < 5; ++c) {
        const float px = pb.x - 14.0f * u - (float)(4 - c) * 10.0f * u;
        const bool cur = (c == (int)cp.costume);
        dl->AddCircleFilled(ImVec2(px, pip_y), cur ? 3.5f * u : 2.2f * u,
                            cur ? theme::white(0.95f) : theme::white(0.28f), 12);
      }

      // Bold 2-line uppercase fighter name on bottom-left of portrait
      const float name_sz = (doors == 2 ? 36.0f : 28.0f) * u;
      const bool two_lines = (fi.line2 && fi.line2[0] != '\0');
      const float ny = two_lines ? (pb.y - name_sz * 2.02f - 8.0f * u) : (pb.y - name_sz - 10.0f * u);
      theme::draw_text_tracked(dl, f_disp, name_sz, ImVec2(sx + 12.0f * u, ny),
                               theme::white(cp.locked ? 1.0f : 0.86f), fi.line1, 1.8f * u, true);
      if (two_lines) {
        theme::draw_text_tracked(dl, f_disp, name_sz, ImVec2(sx + 12.0f * u, ny + name_sz * 0.96f),
                                 theme::white(cp.locked ? 1.0f : 0.86f), fi.line2, 1.8f * u, true);
      }
    } else {
      // Hovering empty / Random
      const char* lbl = (cp.sel_icon == 25) ? "RANDOM" : "SELECT FIGHTER";
      const float lsz = 18.0f * u;
      const float lw = theme::measure_tracked(f_head, lsz, lbl, 2.5f * u);
      theme::draw_text_tracked(dl, f_head, lsz, ImVec2(sx + (slot_w - lw) * 0.5f, (pa.y + pb.y - lsz) * 0.5f),
                               theme::silver(0.75f), lbl, 2.5f * u, true);
    }

    // Crisp rim highlight when fighter is locked in
    dl->AddRect(sa, sb, cp.locked ? pcol : theme::hairline(0.24f), 2.0f * u, 0,
                cp.locked ? std::max(1.8f, 2.2f * u) : 1.0f);

    // Port-colored rectangular nameplate bar with signature 3-bar accent stripe on right
    dl->AddRectFilled(bar_a, bar_b,
                      theme::port_color(i, snap.css_is_teams ? (int)cp.team : -1, is_cpu, cp.locked ? 0.96f : 0.72f));
    // 3-bar right accent stripe
    for (int b = 0; b < 3; ++b) {
      const float bx = bar_b.x - 14.0f * u - (float)b * 7.5f * u;
      dl->AddRectFilled(ImVec2(bx, bar_a.y + 6.0f * u), ImVec2(bx + 3.5f * u, bar_b.y - 6.0f * u),
                        theme::rgba(255, 255, 255, cp.locked ? 0.85f : 0.42f));
    }

    char bar_lbl[32];
    if (cp.nametag[0]) {
      std::snprintf(bar_lbl, sizeof bar_lbl, "%s", cp.nametag);
    } else if (is_cpu) {
      std::snprintf(bar_lbl, sizeof bar_lbl, "CPU LV.%u", (unsigned)std::max<uint8_t>(1, cp.cpu_level));
    } else {
      std::snprintf(bar_lbl, sizeof bar_lbl, "PLAYER %d", i + 1);
    }
    theme::draw_text_tracked(dl, f_head, 15.0f * u, ImVec2(sx + 12.0f * u, bar_a.y + 8.5f * u),
                             theme::white(1.0f), bar_lbl, 2.0f * u, true);
  }

  // ---- Bottom Compact 2-Row x 13-Col Character Grid (exact 1:1 with MU_RUI_CSS_GRID_*) ----
  const float grid_l = pic_min.x + margin_x;
  const float grid_r = pic_max.x - margin_x;
  const float grid_t = pic_min.y + h * 0.605f;
  const float grid_b = pic_min.y + h * 0.875f;
  const float cell_gap = 4.5f * u;
  const float cell_w = (grid_r - grid_l - cell_gap * (MU_RUI_CSS_COLS - 1)) / (float)MU_RUI_CSS_COLS;
  const float cell_h = (grid_b - grid_t - cell_gap * (MU_RUI_CSS_ROWS - 1)) / (float)MU_RUI_CSS_ROWS;

  for (int slot = 0; slot < MU_RUI_CSS_SLOTS; ++slot) {
    const int row = slot / MU_RUI_CSS_COLS;
    const int col = slot % MU_RUI_CSS_COLS;
    const int icon_idx = (int)mu_rui_css_slot_to_icon(slot);
    const int ckind = theme::css_icon_to_ckind(icon_idx);
    const bool unlocked = (icon_idx == 25) || (snap.css_unlocked_mask == 0u) ||
                          (((snap.css_unlocked_mask >> icon_idx) & 1u) != 0u);

    const ImVec2 ca(grid_l + (float)col * (cell_w + cell_gap),
                    grid_t + (float)row * (cell_h + cell_gap));
    const ImVec2 cb(ca.x + cell_w, ca.y + cell_h);

    // Check whether any port hovers or locks this icon
    int hovered_port = -1;
    int locked_port = -1;
    for (int p = 0; p < 4; ++p) {
      if (snap.css_ports[p].kind == 3) continue;
      if ((int)snap.css_ports[p].sel_icon == icon_idx) {
        if (snap.css_ports[p].locked) locked_port = p;
        else hovered_port = p;
      }
    }

    if (icon_idx == 25) {
      // Random slot
      dl->AddRectFilled(ca, cb, theme::rgba(22, 32, 48, 0.92f), 2.0f * u);
      const float qsz = cell_h * 0.48f;
      const ImVec2 qs = f_disp->CalcTextSizeA(qsz, FLT_MAX, 0.0f, "?");
      theme::draw_text_tracked(dl, f_disp, qsz, ImVec2((ca.x + cb.x - qs.x) * 0.5f, (ca.y + cb.y - qs.y) * 0.5f - 4.0f * u),
                               theme::white(0.92f), "?", 0.0f, true);
      const float rlsz = 9.0f * u;
      const float rlw = theme::measure_tracked(f_body, rlsz, "RANDOM", 0.8f * u);
      theme::draw_text_tracked(dl, f_body, rlsz, ImVec2((ca.x + cb.x - rlw) * 0.5f, cb.y - 13.0f * u),
                               theme::silver(0.82f), "RANDOM", 0.8f * u, false);
    } else {
      ImTextureData* ctex = bank.css_icon(ckind);
      assets::draw_fighter_portrait_card(dl, f_head, ca, cb, ckind, 0, ctex, unlocked ? 0.95f : 0.25f, false);
      // Tiny clean fighter name strip at bottom of tile
      const auto& fi = theme::fighter_info(ckind);
      dl->AddRectFilled(ImVec2(ca.x, cb.y - 14.0f * u), cb, theme::rgba(5, 8, 15, 0.72f));
      const float nsz = 8.8f * u;
      const char* short_lbl = (fi.line2 && fi.line2[0]) ? fi.abbr : fi.line1;
      const ImVec2 ns = f_body->CalcTextSizeA(nsz, FLT_MAX, 0.0f, short_lbl);
      dl->AddText(f_body, nsz, ImVec2((ca.x + cb.x - ns.x) * 0.5f, cb.y - 12.5f * u),
                  theme::white(unlocked ? 0.90f : 0.35f), short_lbl);
    }

    ImU32 border = theme::hairline(0.24f);
    float thick = 1.0f;
    if (locked_port >= 0) {
      border = theme::port_color(locked_port, snap.css_is_teams ? (int)snap.css_ports[locked_port].team : -1,
                                 snap.css_ports[locked_port].kind == 1, 1.0f);
      thick = std::max(2.0f, 2.4f * u);
    } else if (hovered_port >= 0) {
      border = theme::white(0.95f);
      thick = std::max(1.8f, 2.2f * u);
    }
    dl->AddRect(ca, cb, border, 2.0f * u, 0, thick);
  }

  // ---- Circular Player Tokens (P1-P4) & Analog Cursors ----
  auto world_to_css_screen = [&](float wx, float wy) -> ImVec2 {
    const float nx = (wx - MU_RUI_CSS_GRID_LEFT) / (MU_RUI_CSS_GRID_RIGHT - MU_RUI_CSS_GRID_LEFT);
    const float ny = (MU_RUI_CSS_GRID_TOP - wy) / (MU_RUI_CSS_GRID_TOP - MU_RUI_CSS_GRID_BOTTOM);
    const float sx = grid_l + std::clamp(nx, -0.04f, 1.04f) * (grid_r - grid_l);
    const float sy = grid_t + std::clamp(ny, -0.80f, 1.12f) * (grid_b - grid_t);
    return ImVec2(sx, sy);
  };

  for (int i = 0; i < 4; ++i) {
    const auto& cp = snap.css_ports[i];
    if (cp.kind == 3) continue;
    const bool is_cpu = (cp.kind == 1);
    const ImU32 pcol = theme::port_color(i, snap.css_is_teams ? (int)cp.team : -1, is_cpu, 1.0f);
    char tok[8];
    if (is_cpu) std::snprintf(tok, sizeof tok, "CP");
    else std::snprintf(tok, sizeof tok, "P%d", i + 1);

    // Draw placed or carried puck token
    const ImVec2 ppos = world_to_css_screen(cp.puck_x, cp.puck_y);
    const float pr = 13.5f * u;
    dl->AddCircleFilled(ImVec2(ppos.x + 1.0f, ppos.y + 2.0f), pr, theme::rgba(0, 0, 0, 0.55f), 24);
    dl->AddCircleFilled(ppos, pr, pcol, 24);
    dl->AddCircle(ppos, pr, theme::white(0.95f), 24, std::max(1.5f, 1.9f * u));
    const float tsz = 12.0f * u;
    const ImVec2 ts = f_head->CalcTextSizeA(tsz, FLT_MAX, 0.0f, tok);
    dl->AddText(f_head, tsz, ImVec2(ppos.x - ts.x * 0.5f, ppos.y - ts.y * 0.5f), theme::white(1.0f), tok);

    // Draw sleek circular reticle cursor for human ports
    if (!is_cpu) {
      const ImVec2 hpos = world_to_css_screen(cp.hand_x, cp.hand_y);
      const float hr = cp.holding_puck ? 18.0f * u : 11.0f * u;
      dl->AddCircle(hpos, hr, theme::white(0.92f), 28, std::max(1.6f, 2.0f * u));
      dl->AddCircle(hpos, hr + 2.0f * u, pcol, 28, std::max(1.0f, 1.4f * u));
    }
  }

  // ---- Full-Width Crimson "READY TO FIGHT" Sweep Banner ----
  anim.ready_banner_alpha = smooth_step(anim.ready_banner_alpha, snap.css_ready_to_fight ? 1.0f : 0.0f, 18.0f, dt);
  if (anim.ready_banner_alpha > 0.02f) {
    const float a = anim.ready_banner_alpha;
    const float by_mid = hero_top + hero_h * 0.42f;
    const float bh = 58.0f * u;
    const ImVec2 r0(pic_min.x, by_mid - bh * 0.5f);
    const ImVec2 r1(pic_max.x, by_mid + bh * 0.5f);
    dl->AddRectFilledMultiColor(r0, r1,
                                theme::crimson(0.84f * a), theme::crimson_hi(0.94f * a),
                                theme::crimson_hi(0.94f * a), theme::crimson(0.84f * a));
    dl->AddLine(r0, ImVec2(r1.x, r0.y), theme::white(0.85f * a), std::max(1.2f, 1.5f * u));
    dl->AddLine(ImVec2(r0.x, r1.y), r1, theme::white(0.85f * a), std::max(1.2f, 1.5f * u));

    const float cx = (pic_min.x + pic_max.x) * 0.5f;
    const char* rtf = "READY TO FIGHT";
    const float rsz_main = 34.0f * u;
    const float rtr = 8.0f * u;
    const float rtw = theme::measure_tracked(f_disp, rsz_main, rtf, rtr);
    theme::draw_text_tracked(dl, f_disp, rsz_main, ImVec2(cx - rtw * 0.5f, by_mid - 24.0f * u),
                             theme::white(a), rtf, rtr, true);

    const float pulse = 0.65f + 0.35f * std::sin(t * 6.0f);
    const char* ps = ">>   PRESS START   <<";
    const float psz = 12.5f * u;
    const float ptr = 4.0f * u;
    const float pw_s = theme::measure_tracked(f_head, psz, ps, ptr);
    theme::draw_text_tracked(dl, f_head, psz, ImVec2(cx - pw_s * 0.5f, by_mid + 12.0f * u),
                             theme::white(pulse * a), ps, ptr, true);
  }

  // ---- Bottom GameCube Prompts ----
  const float pby = pic_max.y - 38.0f * u;
  float px = grid_l;
  px += theme::draw_gc_prompt(dl, f_body, ImVec2(px, pby), u, theme::GcButton::A, "Select", 1.0f);
  px += theme::draw_gc_prompt(dl, f_body, ImVec2(px, pby), u, theme::GcButton::B, "Back (Hold)", 1.0f);
  px += theme::draw_gc_prompt(dl, f_body, ImVec2(px, pby), u, theme::GcButton::X, nullptr, 1.0f);
  px += theme::draw_gc_prompt(dl, f_body, ImVec2(px, pby), u, theme::GcButton::Y, "Color Change", 1.0f);
  if (snap.css_is_teams) {
    theme::draw_gc_prompt(dl, f_body, ImVec2(px, pby), u, theme::GcButton::Z, "Team Color", 1.0f);
  }
}

// ============================================================================================
// 4. STAGE SELECT SCREEN (SSS — Horizontal Competitive Strip + Secondary All-Stages Grid)
// ============================================================================================
inline void draw_sss(ImDrawList* dl, const theme::Fonts& fonts, assets::DiscAssetBank& bank,
                     MenuAnimState& anim, const MuRedesignedUiSnapshot& snap,
                     ImVec2 pic_min, ImVec2 pic_max, float u, float t, float dt) {
  const float w = pic_max.x - pic_min.x, h = pic_max.y - pic_min.y;
  const int hovered = std::clamp((int)snap.sss_hovered_index, 0, 29);
  const auto& hstage = theme::stage_info_by_icon(hovered);

  // Atmospheric dark-slate base + dimmed hovered stage backdrop art
  theme::draw_atmospheric_backdrop(dl, pic_min, pic_max, t);
  assets::draw_stage_card_art(dl, pic_min, pic_max, hovered, bank.stage_preview(hovered), 0.72f, true);
  // Deep reading vignette over stage backdrop so typography & stage cards stay ultra-sharp
  dl->AddRectFilledMultiColor(pic_min, pic_max,
                              theme::rgba(6, 10, 18, 0.74f), theme::rgba(6, 10, 18, 0.42f),
                              theme::rgba(5, 8, 15, 0.88f), theme::rgba(5, 8, 15, 0.90f));

  ImFont* f_disp = theme::pick_font(fonts.display ? fonts.display : fonts.heading);
  ImFont* f_head = theme::pick_font(fonts.heading ? fonts.heading : fonts.body);
  ImFont* f_body = theme::pick_font(fonts.body ? fonts.body : fonts.heading);

  const float margin_x = w * 0.055f;

  // ---- Top Header: "STAGE SELECT" left, HUGE Stage Name top-right ----
  const float hdr_y = pic_min.y + 28.0f * u;
  theme::draw_text_tracked(dl, f_body, 12.5f * u, ImVec2(pic_min.x + margin_x, hdr_y),
                           theme::silver(0.75f), snap.is_online ? "ONLINE MATCHMAKING" : "COMPETITIVE STAGE LIST",
                           2.6f * u, true);
  theme::draw_text_tracked(dl, f_head, 28.0f * u, ImVec2(pic_min.x + margin_x, hdr_y + 18.0f * u),
                           theme::white(0.95f), "STAGE SELECT", 3.2f * u, true);

  // Huge hovered stage name top-right
  const float st_sz = 46.0f * u;
  const float st_tr = 3.2f * u;
  const float st_w = theme::measure_tracked(f_disp, st_sz, hstage.name, st_tr);
  theme::draw_text_tracked(dl, f_disp, st_sz, ImVec2(pic_max.x - margin_x - st_w, hdr_y + 4.0f * u),
                           theme::white(1.0f), hstage.name, st_tr, true);

  // Frozen Stadium badge when hovering Pokemon Stadium with Frozen PS enabled
  if (hovered == 18 && snap.sss_frozen_stadium) {
    const char* fz = "FROZEN STADIUM";
    const float fz_sz = 12.0f * u;
    const float fzw = theme::measure_tracked(f_head, fz_sz, fz, 2.0f * u);
    const ImVec2 fa(pic_max.x - margin_x - fzw - 16.0f * u, hdr_y + 56.0f * u);
    const ImVec2 fb(pic_max.x - margin_x, fa.y + 20.0f * u);
    dl->AddRectFilled(fa, fb, theme::rgba(36, 168, 212, 0.28f), 3.0f * u);
    dl->AddRect(fa, fb, theme::rgba(92, 212, 248, 0.85f), 3.0f * u, 0, 1.2f);
    theme::draw_text_tracked(dl, f_head, fz_sz, ImVec2(fa.x + 8.0f * u, fa.y + 3.5f * u),
                             theme::white(0.96f), fz, 2.0f * u, false);
  }

  const float hl_y = hdr_y + 62.0f * u;
  dl->AddLine(ImVec2(pic_min.x + margin_x, hl_y), ImVec2(pic_max.x - margin_x, hl_y),
              theme::hairline(0.50f), std::max(1.0f, 1.4f * u));

  // Coordinate mapping matching mu_rui_sss_hit_test (x in [-25.5, +25.5], y in [-16.5, +7.5])
  const float strip_l = pic_min.x + margin_x;
  const float strip_r = pic_max.x - margin_x;
  const float strip_w = strip_r - strip_l;

  // Map Melee SSS y (-19..+19) to screen y so primary [-2.5..+7.5] and secondary [-16.5..-4.5] align 1:1
  const float pri_t = pic_min.y + h * 0.295f;
  const float pri_b = pic_min.y + h * 0.565f;
  const float sec_t = pic_min.y + h * 0.620f;
  const float sec_b = pic_min.y + h * 0.865f;

  // ---- Primary Strip Group Hairline Labels (RANDOM | NEUTRAL STAGES | COUNTERPICK) ----
  const float p_gap = 10.0f * u;
  const float p_card_w = (strip_w - p_gap * (MU_RUI_SSS_PRIMARY_COUNT - 1)) / (float)MU_RUI_SSS_PRIMARY_COUNT;
  auto draw_group_hdr = [&](int col_start, int col_end, const char* title) {
    const float gx0 = strip_l + (float)col_start * (p_card_w + p_gap);
    const float gx1 = strip_l + (float)(col_end + 1) * (p_card_w + p_gap) - p_gap;
    const float gy = pri_t - 24.0f * u;
    theme::draw_text_tracked(dl, f_head, 12.0f * u, ImVec2(gx0, gy),
                             theme::silver(0.88f), title, 2.2f * u, true);
    dl->AddLine(ImVec2(gx0, gy + 16.0f * u), ImVec2(gx1, gy + 16.0f * u),
                theme::hairline(0.45f), std::max(1.0f, 1.2f * u));
  };
  draw_group_hdr(0, 0, "RANDOM");
  draw_group_hdr(1, 5, "NEUTRAL STAGES");
  draw_group_hdr(6, 6, "COUNTERPICK");

  // ---- Primary 7 Wide Competitive Cards ----
  for (int slot = 0; slot < MU_RUI_SSS_PRIMARY_COUNT; ++slot) {
    const int icon = (int)mu_rui_sss_primary_icon(slot);
    const auto& st = theme::stage_info_by_icon(icon);
    const bool is_hov = (icon == hovered);
    const bool unlocked = (icon == 29) || (snap.sss_unlocked_mask == 0u) ||
                          (((snap.sss_unlocked_mask >> icon) & 1u) != 0u);
    const bool struck = ((snap.sss_struck_mask >> icon) & 1u) != 0u;

    anim.sss_hover_lift[icon] = smooth_step(anim.sss_hover_lift[icon], is_hov ? 1.0f : 0.0f, 18.0f, dt);
    const float lift = anim.sss_hover_lift[icon] * 7.0f * u;

    const ImVec2 ca(strip_l + (float)slot * (p_card_w + p_gap), pri_t - lift);
    const ImVec2 cb(ca.x + p_card_w, pri_b - lift);

    // Subtle drop shadow
    dl->AddRectFilled(ImVec2(ca.x + 2.0f * u, ca.y + 6.0f * u), ImVec2(cb.x + 2.0f * u, cb.y + 6.0f * u),
                      theme::rgba(0, 0, 0, 0.45f), 3.0f * u);

    const float card_a = (!unlocked ? 0.25f : (struck ? 0.35f : 1.0f));
    assets::draw_stage_card_art(dl, ca, cb, icon, bank.stage_preview(icon), card_a, false);

    if (icon == 29) {
      const float qsz = 56.0f * u;
      const ImVec2 qs = f_disp->CalcTextSizeA(qsz, FLT_MAX, 0.0f, "?");
      theme::draw_text_tracked(dl, f_disp, qsz, ImVec2((ca.x + cb.x - qs.x) * 0.5f, (ca.y + cb.y - qs.y) * 0.5f - 10.0f * u),
                               theme::white(0.95f), "?", 0.0f, true);
    }

    // Bottom dark gradient & crisp stage label inside card
    dl->AddRectFilledMultiColor(ImVec2(ca.x, cb.y - 46.0f * u), cb,
                                theme::rgba(5, 8, 15, 0.0f), theme::rgba(5, 8, 15, 0.0f),
                                theme::rgba(5, 8, 15, 0.90f), theme::rgba(5, 8, 15, 0.90f));
    const float lsz = 12.5f * u;
    const float lw = theme::measure_tracked(f_head, lsz, st.short_name, 1.0f * u);
    theme::draw_text_tracked(dl, f_head, lsz, ImVec2((ca.x + cb.x - lw) * 0.5f, cb.y - 22.0f * u),
                             theme::white(card_a), st.short_name, 1.0f * u, true);

    // Red ban slash overlay when stage is struck
    if (struck) {
      dl->AddLine(ImVec2(ca.x + 12.0f * u, ca.y + 12.0f * u), ImVec2(cb.x - 12.0f * u, cb.y - 12.0f * u),
                  theme::crimson_hi(0.92f), std::max(2.5f, 3.2f * u));
      dl->AddLine(ImVec2(cb.x - 12.0f * u, ca.y + 12.0f * u), ImVec2(ca.x + 12.0f * u, cb.y - 12.0f * u),
                  theme::crimson_hi(0.92f), std::max(2.5f, 3.2f * u));
    }

    // Crisp outer border
    dl->AddRect(ca, cb, is_hov ? theme::white(1.0f) : theme::hairline(0.32f), 2.5f * u, 0,
                is_hov ? std::max(2.2f, 2.6f * u) : 1.0f);
  }

  // ---- Secondary All-Stages Strip (23 Stages in 2 Compact Rows: 12 + 11) ----
  const float sec_lbl_y = sec_t - 20.0f * u;
  theme::draw_text_tracked(dl, f_head, 11.5f * u, ImVec2(strip_l, sec_lbl_y),
                           theme::silver(0.75f), "ALL OTHER STAGES", 2.2f * u, true);
  dl->AddLine(ImVec2(strip_l, sec_lbl_y + 14.0f * u), ImVec2(strip_r, sec_lbl_y + 14.0f * u),
              theme::hairline(0.28f), 1.0f);

  const float s_gap = 5.0f * u;
  const float s_row_h = (sec_b - sec_t - s_gap) * 0.5f;
  for (int row = 0; row < 2; ++row) {
    const int cols = (row == 0) ? 12 : 11;
    const float cw = (strip_w - s_gap * (cols - 1)) / (float)cols;
    for (int col = 0; col < cols; ++col) {
      const int idx = row * 12 + col;
      if (idx >= MU_RUI_SSS_EXTRA_COUNT) continue;
      const int icon = (int)mu_rui_sss_extra_icon(idx);
      const auto& st = theme::stage_info_by_icon(icon);
      const bool is_hov = (icon == hovered);
      const bool unlocked = (snap.sss_unlocked_mask == 0u) || (((snap.sss_unlocked_mask >> icon) & 1u) != 0u);
      const bool struck = ((snap.sss_struck_mask >> icon) & 1u) != 0u;

      const ImVec2 ca(strip_l + (float)col * (cw + s_gap), sec_t + (float)row * (s_row_h + s_gap));
      const ImVec2 cb(ca.x + cw, ca.y + s_row_h);
      const float ca_alpha = !unlocked ? 0.22f : (struck ? 0.35f : 0.90f);

      assets::draw_stage_card_art(dl, ca, cb, icon, bank.stage_preview(icon), ca_alpha, false);
      dl->AddRectFilled(ImVec2(ca.x, cb.y - 16.0f * u), cb, theme::rgba(5, 8, 15, 0.78f));
      const float nsz = 8.5f * u;
      const ImVec2 ns = f_body->CalcTextSizeA(nsz, FLT_MAX, 0.0f, st.short_name);
      dl->AddText(f_body, nsz, ImVec2(ca.x + std::max(3.0f * u, (cw - ns.x) * 0.5f), cb.y - 13.5f * u),
                  theme::white(ca_alpha), st.short_name, nullptr, cw - 4.0f * u);
      if (struck) {
        dl->AddLine(ca, cb, theme::crimson_hi(0.90f), 2.0f * u);
      }
      dl->AddRect(ca, cb, is_hov ? theme::white(1.0f) : theme::hairline(0.24f), 2.0f * u, 0,
                  is_hov ? std::max(2.0f, 2.2f * u) : 1.0f);
    }
  }

  // ---- Crisp White Target Cursor Tracking sss_cursor_x/y ----
  if (!snap.sss_confirmed && !snap.sss_loading) {
    const float nx = (snap.sss_cursor_x + 25.5f) / 51.0f;
    // Map world y directly across the primary [+7.5..-2.5] and secondary [-4.5..-16.5] spans
    float sy = (pri_t + pri_b) * 0.5f;
    if (snap.sss_cursor_y >= -3.5f) {
      const float ty = (7.5f - snap.sss_cursor_y) / 10.0f;
      sy = pri_t + std::clamp(ty, -0.35f, 1.15f) * (pri_b - pri_t);
    } else {
      const float ty = (-4.5f - snap.sss_cursor_y) / 12.0f;
      sy = sec_t + std::clamp(ty, -0.15f, 1.15f) * (sec_b - sec_t);
    }
    const ImVec2 cpos(strip_l + std::clamp(nx, -0.02f, 1.02f) * strip_w, sy);
    const float r = 15.0f * u;
    dl->AddCircle(cpos, r, theme::white(0.96f), 32, std::max(1.8f, 2.2f * u));
    dl->AddCircleFilled(cpos, 2.5f * u, theme::white(0.96f), 12);
    // 4 crisp target ticks
    const float t0 = r - 4.0f * u, t1 = r + 5.0f * u;
    dl->AddLine(ImVec2(cpos.x - t1, cpos.y), ImVec2(cpos.x - t0, cpos.y), theme::white(0.95f), 1.8f * u);
    dl->AddLine(ImVec2(cpos.x + t0, cpos.y), ImVec2(cpos.x + t1, cpos.y), theme::white(0.95f), 1.8f * u);
    dl->AddLine(ImVec2(cpos.x, cpos.y - t1), ImVec2(cpos.x, cpos.y - t0), theme::white(0.95f), 1.8f * u);
    dl->AddLine(ImVec2(cpos.x, cpos.y + t0), ImVec2(cpos.x, cpos.y + t1), theme::white(0.95f), 1.8f * u);
  }

  // ---- "LOADING..." Confirmation Banner ----
  if (snap.sss_confirmed || snap.sss_loading) {
    const float cx = (pic_min.x + pic_max.x) * 0.5f;
    const float cy = (pic_min.y + pic_max.y) * 0.5f;
    const ImVec2 la(pic_min.x, cy - 42.0f * u), lb(pic_max.x, cy + 42.0f * u);
    dl->AddRectFilled(la, lb, theme::rgba(6, 10, 18, 0.88f));
    dl->AddLine(la, ImVec2(lb.x, la.y), theme::white(0.75f), 1.5f * u);
    dl->AddLine(ImVec2(la.x, lb.y), lb, theme::white(0.75f), 1.5f * u);
    const char* ltxt = "LOADING STAGE...";
    const float lsz = 28.0f * u;
    const float ltr = 7.0f * u;
    const float lw = theme::measure_tracked(f_disp, lsz, ltxt, ltr);
    theme::draw_text_tracked(dl, f_disp, lsz, ImVec2(cx - lw * 0.5f, cy - 22.0f * u),
                             theme::white(1.0f), ltxt, ltr, true);
    const float sw = theme::measure_tracked(f_head, 13.5f * u, hstage.name, 3.2f * u);
    theme::draw_text_tracked(dl, f_head, 13.5f * u, ImVec2(cx - sw * 0.5f, cy + 12.0f * u),
                             theme::silver(0.90f), hstage.name, 3.2f * u, true);
  }

  // ---- Bottom GameCube Prompts ----
  const float pby = pic_max.y - 38.0f * u;
  float px = strip_l;
  px += theme::draw_gc_prompt(dl, f_body, ImVec2(px, pby), u, theme::GcButton::A, "Select Stage", 1.0f);
  px += theme::draw_gc_prompt(dl, f_body, ImVec2(px, pby), u, theme::GcButton::B, "Back", 1.0f);
  px += theme::draw_gc_prompt(dl, f_body, ImVec2(px, pby), u, theme::GcButton::X, "Ban Stage", 1.0f);
  theme::draw_gc_prompt(dl, f_body, ImVec2(px, pby), u, theme::GcButton::Start, "Random", 1.0f);
}

}  // namespace gx::redesigned::menus
