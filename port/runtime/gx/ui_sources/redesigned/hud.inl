// Match HUD, Match Banners, Over-Fighter Tags, and Pause Menu for the optional Redesigned UI.
// Clean-room ImGui implementation inspired by Dave Abarra's "Melee Redesigned" (XbPBV71g1_s).
// Pure presentation — reads read-only MuRedesignedUiSnapshot and never mutates game state.
// SPDX-License-Identifier: GPL-2.0-or-later

namespace gx::redesigned::hud {

inline float anim_lerp(float cur, float target, float speed, float dt) {
  const float k = 1.0f - std::exp(-speed * std::clamp(dt, 0.001f, 0.1f));
  return cur + (target - cur) * k;
}

struct HudAnimState {
  bool seen_present[4] = {false, false, false, false};
  float prev_damage[4] = {0, 0, 0, 0};
  float shake_timer[4] = {0, 0, 0, 0};
  float flash_timer[4] = {0, 0, 0, 0};
  float pause_alpha = 0.0f;
  uint32_t last_frame = 0;
};

inline void draw_timer(ImDrawList* dl, const theme::Fonts& fonts, const MuRedesignedUiSnapshot& snap,
                       ImVec2 pic_min, ImVec2 pic_max, float u, float t) {
  if (snap.timer_hidden || !snap.timer_enabled) return;

  const uint32_t total_sec = snap.timer_seconds;
  const uint32_t hrs = total_sec / 3600u;
  const uint32_t mins = (total_sec % 3600u) / 60u;
  const uint32_t secs = total_sec % 60u;
  const uint32_t cs = snap.timer_centiseconds % 100u;

  char main_buf[24];
  char cs_buf[8];
  if (snap.timer_shows_hours && hrs > 0) {
    std::snprintf(main_buf, sizeof main_buf, "%u:%02u:%02u", hrs, mins, secs);
  } else {
    std::snprintf(main_buf, sizeof main_buf, "%02u:%02u", mins, secs);
  }
  std::snprintf(cs_buf, sizeof cs_buf, ".%02u", cs);

  const bool urgent = !snap.timer_counts_up && total_sec <= 10u && (total_sec > 0u || cs > 0u);
  const float pulse = urgent ? (0.5f + 0.5f * std::sin(t * 14.0f)) : 0.0f;
  const float scale_bump = 1.0f + 0.05f * pulse;

  ImFont* f_disp = theme::pick_font(fonts.display ? fonts.display : fonts.mono);
  const float main_sz = 34.0f * u * scale_bump;
  const float cs_sz = 19.0f * u * scale_bump;
  const ImVec2 m_ext = f_disp->CalcTextSizeA(main_sz, FLT_MAX, 0.0f, main_buf);
  const ImVec2 c_ext = f_disp->CalcTextSizeA(cs_sz, FLT_MAX, 0.0f, cs_buf);
  const float total_w = m_ext.x + c_ext.x + 2.0f * u;
  const float cx = (pic_min.x + pic_max.x) * 0.5f;
  const float top_y = pic_min.y + 18.0f * u;

  // Subtle translucent dark pill backing so timer stays legible against bright stage skies
  const float pad_x = 18.0f * u, pad_y = 6.0f * u;
  const ImVec2 p0(cx - total_w * 0.5f - pad_x, top_y - pad_y);
  const ImVec2 p1(cx + total_w * 0.5f + pad_x, top_y + m_ext.y + pad_y);
  dl->AddRectFilled(p0, p1, theme::rgba(6, 10, 18, 0.46f), 4.0f * u);
  dl->AddLine(ImVec2(p0.x + 8.0f * u, p1.y), ImVec2(p1.x - 8.0f * u, p1.y),
              urgent ? theme::crimson_hi(0.90f) : theme::hairline(0.42f), std::max(1.0f, 1.4f * u));

  const ImU32 main_col = urgent ? theme::rgba(255, (int)(92 + 120 * (1.0f - pulse)), (int)(96 + 110 * (1.0f - pulse)), 1.0f)
                                : theme::white(0.98f);
  const ImU32 cs_col = urgent ? theme::crimson_hi(0.92f) : theme::silver(0.86f);

  const float x0 = cx - total_w * 0.5f;
  theme::draw_text_tracked(dl, f_disp, main_sz, ImVec2(x0, top_y), main_col, main_buf, 0.6f * u, true);
  theme::draw_text_tracked(dl, f_disp, cs_sz, ImVec2(x0 + m_ext.x + 2.0f * u, top_y + m_ext.y - c_ext.y - 3.0f * u),
                           cs_col, cs_buf, 0.2f * u, true);
}

inline void draw_match_banner(ImDrawList* dl, const theme::Fonts& fonts, const MuRedesignedUiSnapshot& snap,
                              ImVec2 pic_min, ImVec2 pic_max, float u) {
  if (snap.match_banner == MU_RUI_BANNER_NONE) return;

  const char* title = nullptr;
  const char* sub = nullptr;
  ImU32 accent = theme::crimson(0.88f);
  switch (snap.match_banner) {
    case MU_RUI_BANNER_READY:        title = "READY";        sub = "PREPARE FOR BATTLE"; accent = theme::rgba(24, 52, 96, 0.84f); break;
    case MU_RUI_BANNER_GO:           title = "GO!";          sub = nullptr;              accent = theme::rgba(28, 168, 118, 0.88f); break;
    case MU_RUI_BANNER_TIME:         title = "TIME!";        sub = "MATCH OVER";         accent = theme::crimson(0.88f); break;
    case MU_RUI_BANNER_GAME:         title = "GAME!";        sub = "MATCH COMPLETE";     accent = theme::crimson(0.88f); break;
    case MU_RUI_BANNER_SUDDEN_DEATH: title = "SUDDEN DEATH"; sub = "300% DAMAGE";        accent = theme::crimson(0.92f); break;
    case MU_RUI_BANNER_COMPLETE:     title = "COMPLETE";     sub = nullptr;              accent = theme::rgba(36, 128, 224, 0.88f); break;
    case MU_RUI_BANNER_FAILURE:      title = "FAILURE";      sub = nullptr;              accent = theme::crimson(0.88f); break;
    default: return;
  }

  const float prog = theme::ease_out_cubic((float)snap.match_banner_frame / 10.0f);
  const float w = pic_max.x - pic_min.x;
  const float cy = (pic_min.y + pic_max.y) * 0.45f;
  const float band_h = (sub ? 104.0f : 86.0f) * u * (0.75f + 0.25f * prog);

  const ImVec2 b0(pic_min.x, cy - band_h * 0.5f);
  const ImVec2 b1(pic_max.x, cy + band_h * 0.5f);

  // Full-width horizontal sweep ribbon with soft darker outer edges
  const ImU32 edge_col = theme::rgba(6, 10, 18, 0.62f * prog);
  dl->AddRectFilledMultiColor(b0, ImVec2(pic_min.x + w * 0.35f, b1.y), edge_col, accent, accent, edge_col);
  dl->AddRectFilled(ImVec2(pic_min.x + w * 0.35f, b0.y), ImVec2(pic_min.x + w * 0.65f, b1.y), accent);
  dl->AddRectFilledMultiColor(ImVec2(pic_min.x + w * 0.65f, b0.y), b1, accent, edge_col, edge_col, accent);

  // Crisp top & bottom white hairlines
  const float hl_w = w * (0.25f + 0.75f * prog);
  const float cx = (pic_min.x + pic_max.x) * 0.5f;
  dl->AddLine(ImVec2(cx - hl_w * 0.5f, b0.y), ImVec2(cx + hl_w * 0.5f, b0.y),
              theme::white(0.82f * prog), std::max(1.2f, 1.8f * u));
  dl->AddLine(ImVec2(cx - hl_w * 0.5f, b1.y), ImVec2(cx + hl_w * 0.5f, b1.y),
              theme::white(0.82f * prog), std::max(1.2f, 1.8f * u));

  ImFont* f_disp = theme::pick_font(fonts.display ? fonts.display : fonts.heading);
  const float title_sz = 64.0f * u * (1.16f - 0.16f * prog);
  const float tracking = (16.0f - 8.0f * prog) * u;
  const float tw = theme::measure_tracked(f_disp, title_sz, title, tracking);
  const float ty = sub ? (cy - title_sz * 0.62f) : (cy - title_sz * 0.50f);
  theme::draw_text_tracked(dl, f_disp, title_sz, ImVec2(cx - tw * 0.5f, ty),
                           theme::white(prog), title, tracking, true);

  if (sub) {
    ImFont* f_body = theme::pick_font(fonts.body ? fonts.body : fonts.heading);
    const float sub_sz = 14.0f * u;
    const float st = 5.0f * u;
    const float sw = theme::measure_tracked(f_body, sub_sz, sub, st);
    theme::draw_text_tracked(dl, f_body, sub_sz, ImVec2(cx - sw * 0.5f, cy + title_sz * 0.36f),
                             theme::white(0.84f * prog), sub, st, true);
  }
}

inline void draw_fighter_tags(ImDrawList* dl, const theme::Fonts& fonts, const MuRedesignedUiSnapshot& snap,
                              ImVec2 pic_min, ImVec2 pic_max, float u) {
  const float pw = pic_max.x - pic_min.x, ph = pic_max.y - pic_min.y;
  ImFont* f_body = theme::pick_font(fonts.heading ? fonts.heading : fonts.body);
  for (int i = 0; i < 4; ++i) {
    const auto& hp = snap.hud_players[i];
    if (!hp.present || !hp.alive || !hp.tag_visible) continue;
    if (!std::isfinite(hp.tag_x) || !std::isfinite(hp.tag_y)) continue;
    // Melee's nametag/fighter coordinates are in 640x480 screen units (or 0..1 normalized in synthetic tests).
    const float nx = (hp.tag_x > 1.5f || hp.tag_y > 1.5f) ? (hp.tag_x / 640.0f) : hp.tag_x;
    const float ny = (hp.tag_x > 1.5f || hp.tag_y > 1.5f) ? (hp.tag_y / 480.0f) : hp.tag_y;
    if (nx < 0.0f || nx > 1.0f || ny < 0.0f || ny > 1.0f) continue;

    const float sx = pic_min.x + nx * pw;
    const float sy = pic_min.y + ny * ph - 18.0f * u;
    const ImU32 pcol = theme::port_color(i, snap.match_is_teams ? hp.team : -1, hp.is_cpu != 0, 1.0f);
    char label[12];
    if (hp.is_cpu) std::snprintf(label, sizeof label, "CPU");
    else std::snprintf(label, sizeof label, "P%d", i + 1);

    const float sz = 12.5f * u;
    const ImVec2 ts = f_body->CalcTextSizeA(sz, FLT_MAX, 0.0f, label);
    const float pad_x = 7.0f * u, pad_y = 2.5f * u;
    const ImVec2 a(sx - ts.x * 0.5f - pad_x, sy - ts.y - pad_y * 2.0f);
    const ImVec2 b(sx + ts.x * 0.5f + pad_x, sy);
    dl->AddRectFilled(a, b, theme::rgba(8, 12, 20, 0.80f), 3.0f * u);
    dl->AddRectFilled(a, ImVec2(b.x, a.y + 2.2f * u), pcol, 1.5f * u);
    dl->AddText(f_body, sz, ImVec2(sx - ts.x * 0.5f, a.y + pad_y + 1.0f * u), theme::white(0.96f), label);
    // Crisp downward chevron pointing to fighter
    dl->AddTriangleFilled(ImVec2(sx - 4.0f * u, b.y), ImVec2(sx + 4.0f * u, b.y),
                          ImVec2(sx, b.y + 5.0f * u), pcol);
  }
}

inline void draw_player_cards(ImDrawList* dl, const theme::Fonts& fonts, assets::DiscAssetBank& bank,
                              HudAnimState& anim, const MuRedesignedUiSnapshot& snap,
                              const std::string (&online_names)[4],
                              ImVec2 pic_min, ImVec2 pic_max, float u, float dt) {
  int active_ports[4];
  int count = 0;
  for (int i = 0; i < 4; ++i) {
    if (snap.hud_players[i].present) active_ports[count++] = i;
  }
  if (count == 0) return;

  // Detect fresh damage hits to trigger crisp shake & flash feedback
  const bool new_sim_frame = (snap.frame_counter != anim.last_frame);
  if (new_sim_frame) anim.last_frame = snap.frame_counter;
  for (int i = 0; i < 4; ++i) {
    const auto& hp = snap.hud_players[i];
    if (!hp.present) {
      anim.seen_present[i] = false;
      anim.prev_damage[i] = 0.0f;
      anim.shake_timer[i] = 0.0f;
      anim.flash_timer[i] = 0.0f;
      continue;
    }
    if (!anim.seen_present[i]) {
      anim.seen_present[i] = true;
      anim.prev_damage[i] = hp.damage_exact;
      anim.shake_timer[i] = 0.0f;
      anim.flash_timer[i] = 0.0f;
    } else if (hp.damage_exact > anim.prev_damage[i] + 0.4f) {
      const float delta = hp.damage_exact - anim.prev_damage[i];
      anim.shake_timer[i] = std::clamp(0.14f + delta * 0.008f, 0.14f, 0.34f);
      anim.flash_timer[i] = 0.16f;
    }
    anim.prev_damage[i] = hp.damage_exact;
    anim.shake_timer[i] = std::max(0.0f, anim.shake_timer[i] - dt);
    anim.flash_timer[i] = std::max(0.0f, anim.flash_timer[i] - dt);
  }

  const float pw = pic_max.x - pic_min.x;
  const float card_scale = (count <= 2) ? 1.0f : (count == 3 ? 0.90f : 0.82f);
  const float cu = u * card_scale;
  const float card_w = 248.0f * cu;
  const float card_h = 84.0f * cu;
  const float gap = (count <= 2) ? std::min(156.0f * u, pw * 0.16f)
                                 : std::max(18.0f * u, (pw * 0.88f - card_w * count) / (float)(count - 1));
  const float total_w = card_w * count + gap * (count - 1);
  const float start_x = (pic_min.x + pic_max.x - total_w) * 0.5f;
  const float base_y = pic_max.y - 26.0f * u - card_h;

  ImFont* f_disp = theme::pick_font(fonts.display ? fonts.display : fonts.heading);
  ImFont* f_head = theme::pick_font(fonts.heading ? fonts.heading : fonts.body);
  ImFont* f_body = theme::pick_font(fonts.body ? fonts.body : fonts.heading);

  for (int idx = 0; idx < count; ++idx) {
    const int port = active_ports[idx];
    const auto& hp = snap.hud_players[port];
    const auto& fi = theme::fighter_info(hp.char_kind);
    const ImU32 pcol = theme::port_color(port, snap.match_is_teams ? hp.team : -1, hp.is_cpu != 0, 1.0f);

    // Subtle hit-shake offset when taking damage or in hitstun
    float ox = 0.0f, oy = 0.0f;
    const float shake_amp = (anim.shake_timer[port] > 0.0f ? (anim.shake_timer[port] * 26.0f) : 0.0f) +
                            (hp.hitstun ? 2.5f : 0.0f);
    if (shake_amp > 0.1f) {
      const uint32_t h = (snap.frame_counter * 2654435761u) ^ ((uint32_t)port * 0x9E3779B9u);
      const float sx = ((float)((h & 0xFFu)) / 127.5f) - 1.0f;
      const float sy = ((float)(((h >> 8) & 0xFFu)) / 127.5f) - 1.0f;
      ox = sx * std::min(8.5f, shake_amp) * cu;
      oy = sy * std::min(6.0f, shake_amp * 0.7f) * cu;
    }

    const float x = start_x + (float)idx * (card_w + gap);
    const float y = base_y;
    const bool defeated = (snap.match_is_stock && hp.stocks == 0 && !hp.alive);
    const float card_alpha = defeated ? 0.42f : 1.0f;

    // Subtle dark gradient backdrop behind the card (keeps it airy like Dave's minimal HUD)
    const ImVec2 bg0(x - 6.0f * cu, y - 4.0f * cu);
    const ImVec2 bg1(x + card_w + 6.0f * cu, y + card_h + 20.0f * cu);
    dl->AddRectFilledMultiColor(bg0, bg1,
                                theme::rgba(6, 10, 18, 0.24f * card_alpha),
                                theme::rgba(6, 10, 18, 0.14f * card_alpha),
                                theme::rgba(6, 10, 18, 0.62f * card_alpha),
                                theme::rgba(6, 10, 18, 0.68f * card_alpha));

    // Left framed square character portrait
    const float port_sz = 64.0f * cu;
    const ImVec2 pa(x, y + (card_h - port_sz) * 0.5f - 4.0f * cu);
    const ImVec2 pb(pa.x + port_sz, pa.y + port_sz);
    dl->AddRectFilled(ImVec2(pa.x - 2.0f * cu, pa.y - 2.0f * cu),
                      ImVec2(pb.x + 2.0f * cu, pb.y + 2.0f * cu),
                      theme::rgba(4, 7, 13, 0.78f * card_alpha), 2.0f * cu);
    ImTextureData* ctex = bank.css_icon(hp.char_kind);
    assets::draw_fighter_portrait_card(dl, f_head, pa, pb, hp.char_kind, hp.costume, ctex,
                                       (hp.alive ? 1.0f : 0.45f) * card_alpha, false);
    dl->AddRect(pa, pb, theme::rgba(235, 242, 252, 0.55f * card_alpha), 1.0f * cu, 0, std::max(1.0f, 1.4f * cu));

    // Right column: Stocks row on top, bold Damage % underneath
    const float rx = pb.x + 14.0f * cu;
    const float right_edge = x + card_w;

    if (snap.match_is_stamina) {
      // Stamina HP bar above readout
      const float frac = std::clamp((float)hp.damage / 150.0f, 0.0f, 1.0f);
      const ImVec2 h0(rx, pa.y + 4.0f * cu), h1(right_edge, pa.y + 12.0f * cu);
      dl->AddRectFilled(h0, h1, theme::rgba(255, 255, 255, 0.14f * card_alpha), 2.0f * cu);
      dl->AddRectFilled(h0, ImVec2(h0.x + (h1.x - h0.x) * frac, h1.y), pcol, 2.0f * cu);
    } else if (snap.match_is_stock) {
      const float pip_r = 7.5f * cu;
      const float pip_step = 20.0f * cu;
      const float pip_y = pa.y + 8.0f * cu;
      ImTextureData* stex = bank.stock_icon(hp.char_kind, hp.costume);
      if (hp.stocks <= 5) {
        for (int s = 0; s < hp.stocks; ++s) {
          assets::draw_stock_pip(dl, ImVec2(rx + pip_r + (float)s * pip_step, pip_y), pip_r,
                                 hp.char_kind, hp.costume, pcol, stex, card_alpha);
        }
      } else {
        assets::draw_stock_pip(dl, ImVec2(rx + pip_r, pip_y), pip_r, hp.char_kind, hp.costume, pcol, stex, card_alpha);
        char sbuf[16];
        std::snprintf(sbuf, sizeof sbuf, "x%u", (unsigned)hp.stocks);
        theme::draw_text_tracked(dl, f_head, 15.0f * cu, ImVec2(rx + pip_r * 2.0f + 6.0f * cu, pip_y - 7.5f * cu),
                                 theme::white(0.95f * card_alpha), sbuf, 0.4f * cu, true);
      }
    }

    // Nana secondary percent pill (Ice Climbers)
    if (hp.nana_damage >= 0) {
      char nbuf[20];
      std::snprintf(nbuf, sizeof nbuf, "NANA %d%%", (int)hp.nana_damage);
      const float nsz = 11.5f * cu;
      const ImVec2 ns = f_body->CalcTextSizeA(nsz, FLT_MAX, 0.0f, nbuf);
      const ImVec2 na(right_edge - ns.x - 10.0f * cu, pa.y + 1.0f * cu);
      const ImVec2 nb(right_edge, pa.y + ns.y + 5.0f * cu);
      dl->AddRectFilled(na, nb, theme::rgba(22, 34, 54, 0.85f * card_alpha), 3.0f * cu);
      dl->AddText(f_body, nsz, ImVec2(na.x + 5.0f * cu, na.y + 2.0f * cu),
                  theme::damage_color((float)hp.nana_damage, card_alpha), nbuf);
    }

    // Bold damage percentage with smooth color ramp + hit flash
    char dmg_buf[16];
    const int shown_dmg = std::max(0, (int)hp.damage);
    std::snprintf(dmg_buf, sizeof dmg_buf, "%d", shown_dmg);
    const char* unit = snap.match_is_stamina ? "HP" : "%";

    const float dmg_sz = 52.0f * cu;
    const float unit_sz = 22.0f * cu;
    ImU32 dmg_col = snap.match_is_stamina ? theme::white(card_alpha)
                                          : theme::damage_color(hp.damage_exact, card_alpha);
    if (anim.flash_timer[port] > 0.0f) {
      const float fl = std::clamp(anim.flash_timer[port] / 0.22f, 0.0f, 1.0f);
      if (fl > 0.35f) dmg_col = theme::white(card_alpha);
    }

    const ImVec2 d_ext = f_disp->CalcTextSizeA(dmg_sz, FLT_MAX, 0.0f, dmg_buf);
    const ImVec2 u_ext = f_disp->CalcTextSizeA(unit_sz, FLT_MAX, 0.0f, unit);
    const float dmg_y = pa.y + 18.0f * cu;
    const float dmg_x = std::min(rx + ox, right_edge - d_ext.x - u_ext.x - 2.0f * cu);
    theme::draw_text_tracked(dl, f_disp, dmg_sz, ImVec2(dmg_x, dmg_y + oy), dmg_col, dmg_buf, -0.5f * cu, true);
    theme::draw_text_tracked(dl, f_disp, unit_sz,
                             ImVec2(dmg_x + d_ext.x + 2.0f * cu, dmg_y + d_ext.y - u_ext.y - 6.0f * cu + oy),
                             theme::rgba((dmg_col >> 0) & 0xFF, (dmg_col >> 8) & 0xFF, (dmg_col >> 16) & 0xFF, 0.85f * card_alpha),
                             unit, 0.0f, true);

    // Port-colored horizontal underline bar spanning the full card width
    const float bar_y = y + card_h - 4.0f * cu;
    const float bar_h = 3.5f * cu;
    dl->AddRectFilled(ImVec2(x, bar_y), ImVec2(right_edge, bar_y + bar_h),
                      theme::port_color(port, snap.match_is_teams ? hp.team : -1, hp.is_cpu != 0, 0.96f * card_alpha),
                      1.0f * cu);

    // Label below bar: PLAYER N (or online tag) left, FIGHTER NAME right
    char plabel[32];
    if (!online_names[port].empty()) {
      std::snprintf(plabel, sizeof plabel, "%s", online_names[port].c_str());
    } else if (hp.is_cpu) {
      std::snprintf(plabel, sizeof plabel, "CPU %d", port + 1);
    } else {
      std::snprintf(plabel, sizeof plabel, "PLAYER %d", port + 1);
    }
    const float lbl_sz = 12.5f * cu;
    const float lbl_y = bar_y + bar_h + 5.0f * cu;
    theme::draw_text_tracked(dl, f_head, lbl_sz, ImVec2(x, lbl_y),
                             theme::white(0.92f * card_alpha), plabel, 1.4f * cu, true);

    const float fn_w = theme::measure_tracked(f_body, lbl_sz * 0.92f, fi.upper_name, 0.9f * cu);
    theme::draw_text_tracked(dl, f_body, lbl_sz * 0.92f, ImVec2(right_edge - fn_w, lbl_y + 0.5f * cu),
                             theme::silver(0.76f * card_alpha), fi.upper_name, 0.9f * cu, true);
  }
}

inline void draw_pause_overlay(ImDrawList* dl, const theme::Fonts& fonts, HudAnimState& anim,
                               const MuRedesignedUiSnapshot& snap, ImVec2 pic_min, ImVec2 pic_max,
                               float u, float dt) {
  const bool want = (snap.match_paused != 0) && ((snap.active_flags & MU_RUI_FLAG_PAUSE) != 0);
  anim.pause_alpha = anim_lerp(anim.pause_alpha, want ? 1.0f : 0.0f, 18.0f, dt);
  if (anim.pause_alpha < 0.02f) return;

  const float a = anim.pause_alpha;
  const float w = pic_max.x - pic_min.x, h = pic_max.y - pic_min.y;

  // Atmospheric dark scrim over frozen match frame
  dl->AddRectFilled(pic_min, pic_max, theme::rgba(5, 8, 15, 0.64f * a));
  dl->AddRectFilledMultiColor(pic_min, ImVec2(pic_min.x + w * 0.55f, pic_max.y),
                              theme::rgba(4, 7, 13, 0.72f * a), theme::rgba(4, 7, 13, 0.0f),
                              theme::rgba(4, 7, 13, 0.0f), theme::rgba(4, 7, 13, 0.78f * a));

  const float slide = (1.0f - a) * 18.0f * u;
  const float lx = pic_min.x + w * 0.085f - slide;
  const float ty = pic_min.y + h * 0.19f;

  ImFont* f_disp = theme::pick_font(fonts.display ? fonts.display : fonts.heading);
  ImFont* f_head = theme::pick_font(fonts.heading ? fonts.heading : fonts.body);
  ImFont* f_body = theme::pick_font(fonts.body ? fonts.body : fonts.heading);

  // Port eyebrow badge
  const int pslot = std::clamp((int)snap.match_pauser, 0, 3);
  const ImU32 pcol = theme::port_color(pslot, -1, false, a);
  char peye[24];
  std::snprintf(peye, sizeof peye, "PLAYER %d", pslot + 1);
  dl->AddRectFilled(ImVec2(lx, ty + 2.0f * u), ImVec2(lx + 4.0f * u, ty + 16.0f * u), pcol);
  theme::draw_text_tracked(dl, f_head, 15.0f * u, ImVec2(lx + 12.0f * u, ty),
                           theme::silver(0.92f * a), peye, 3.2f * u, true);

  // Giant condensed PAUSE title
  theme::draw_text_tracked(dl, f_disp, 64.0f * u, ImVec2(lx - 2.0f * u, ty + 22.0f * u),
                           theme::white(a), "PAUSE", 4.5f * u, true);

  // Crisp hairline
  const float hl_y = ty + 96.0f * u;
  dl->AddLine(ImVec2(lx, hl_y), ImVec2(lx + 340.0f * u, hl_y), theme::hairline(0.55f * a), std::max(1.0f, 1.4f * u));

  // Action rows reflecting live Melee pause controls accurately
  struct PauseRow { const char* title; const char* detail; bool primary; };
  const bool cam_allowed = (snap.match_pause_flags & 1u) != 0;
  const PauseRow rows[4] = {
    {"RESUME MATCH", "Press START to continue playing", true},
    {cam_allowed ? "FREE CAMERA" : "CAMERA LOCKED",
     cam_allowed ? "Move Control Stick / C-Stick to inspect stage" : "Camera control disabled in this mode", false},
    {"TOGGLE HUD", "Hold Z while paused to preview cleanly", false},
    {"QUIT MATCH", "Hold L + R + A + START to return to select", false},
  };

  float ry = hl_y + 24.0f * u;
  for (int i = 0; i < 4; ++i) {
    if (rows[i].primary) {
      // Crisp white triangle cursor matching Dave's pause screen
      const float cy = ry + 11.0f * u;
      dl->AddTriangleFilled(ImVec2(lx, cy - 5.5f * u), ImVec2(lx, cy + 5.5f * u),
                            ImVec2(lx + 9.0f * u, cy), theme::white(a));
    }
    const float tx = lx + (rows[i].primary ? 20.0f * u : 16.0f * u);
    theme::draw_text_tracked(dl, f_head, (rows[i].primary ? 23.0f : 19.0f) * u, ImVec2(tx, ry),
                             rows[i].primary ? theme::white(a) : theme::silver(0.84f * a),
                             rows[i].title, 2.2f * u, true);
    theme::draw_text_tracked(dl, f_body, 13.0f * u, ImVec2(tx, ry + 26.0f * u),
                             theme::muted(0.95f * a), rows[i].detail, 0.3f * u, true);
    ry += 54.0f * u;
  }

  // Bottom bar: left Resume/HUD prompts, right iconic QUIT GAME (L)(R)(A)(START) chord
  const float by = pic_max.y - 46.0f * u;
  float px = lx;
  px += theme::draw_gc_prompt(dl, f_body, ImVec2(px, by), u, theme::GcButton::Start, "Resume", a);
  px += theme::draw_gc_prompt(dl, f_body, ImVec2(px, by), u, theme::GcButton::Z, "Hide HUD", a);

  const float quit_lbl_sz = 13.5f * u;
  const char* qlbl = "QUIT MATCH";
  const float qlw = theme::measure_tracked(f_head, quit_lbl_sz, qlbl, 2.2f * u);
  const float chord_w = qlw + 16.0f * u + (21.0f * 3 + 42.0f + 8.0f * 4) * u;
  float qx = pic_max.x - w * 0.065f - chord_w;
  theme::draw_text_tracked(dl, f_head, quit_lbl_sz, ImVec2(qx, by + 3.5f * u),
                           theme::crimson_hi(0.95f * a), qlbl, 2.2f * u, true);
  qx += qlw + 14.0f * u;
  qx += theme::draw_gc_prompt(dl, f_body, ImVec2(qx, by), u, theme::GcButton::L, nullptr, a);
  qx += theme::draw_gc_prompt(dl, f_body, ImVec2(qx, by), u, theme::GcButton::R, nullptr, a);
  qx += theme::draw_gc_prompt(dl, f_body, ImVec2(qx, by), u, theme::GcButton::A, nullptr, a);
  theme::draw_gc_prompt(dl, f_body, ImVec2(qx, by), u, theme::GcButton::Start, nullptr, a);
}

inline void draw_match_screen(ImDrawList* dl, const theme::Fonts& fonts, assets::DiscAssetBank& bank,
                              HudAnimState& anim, const MuRedesignedUiSnapshot& snap,
                              const std::string (&online_names)[4],
                              ImVec2 pic_min, ImVec2 pic_max, float u, float t, float dt) {
  const bool hud_on = (snap.active_flags & MU_RUI_FLAG_HUD) != 0;
  const bool pause_owned = (snap.match_paused != 0) && ((snap.active_flags & MU_RUI_FLAG_PAUSE) != 0);
  if (hud_on) {
    draw_timer(dl, fonts, snap, pic_min, pic_max, u, t);
    if (!pause_owned) {
      draw_fighter_tags(dl, fonts, snap, pic_min, pic_max, u);
      draw_player_cards(dl, fonts, bank, anim, snap, online_names, pic_min, pic_max, u, dt);
      draw_match_banner(dl, fonts, snap, pic_min, pic_max, u);
    }
  }
  draw_pause_overlay(dl, fonts, anim, snap, pic_min, pic_max, u, dt);
}

}  // namespace gx::redesigned::hud
