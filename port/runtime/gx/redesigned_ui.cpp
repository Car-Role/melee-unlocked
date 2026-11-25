// Thin host adapter for the optional Redesigned UI refresh (ui_sources/redesigned/).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "redesigned_ui.h"
#include "gx_core.h"
#include "render_options.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ui_sources/redesigned/theme.h"
#include "ui_sources/redesigned/assets.inl"
#include "ui_sources/redesigned/hud.inl"
#include "ui_sources/redesigned/menus.inl"

namespace gx::redesigned_ui {
namespace {

std::atomic<uint32_t> g_flags{0};
std::mutex g_mutex;
MuRedesignedUiSnapshot g_live_snap{};
bool g_has_source_snap = false;
uint64_t g_snap_generation = 0;
std::string g_iso_path;
std::string g_online_names[4];

gx::redesigned::theme::Fonts g_fonts{};
gx::redesigned::assets::DiscAssetBank g_assets;
gx::redesigned::hud::HudAnimState g_hud_anim;
gx::redesigned::menus::MenuAnimState g_menu_anim;
float g_clock = 0.0f;

void render_snapshot_impl(const MuRedesignedUiSnapshot& snap, ImDrawList* dl,
                          ImVec2 pic_min, ImVec2 pic_max, float clock_t, float dt,
                          const std::string (&names)[4]) {
  if (!dl || !snap.valid) return;
  const float w = pic_max.x - pic_min.x;
  const float h = pic_max.y - pic_min.y;
  if (w < 64.0f || h < 64.0f) return;

  // Responsive unit scale anchored to 720p height (and clamped gracefully on 4:3 or ultrawide)
  const float u = std::clamp(std::min(h / 720.0f, w / 960.0f), 0.45f, 3.0f);

  dl->PushClipRect(pic_min, pic_max, true);
  switch (snap.screen) {
    case MU_RUI_SCREEN_TITLE:
      if (snap.active_flags & MU_RUI_FLAG_TITLE) {
        gx::redesigned::menus::draw_title(dl, g_fonts, snap, pic_min, pic_max, u, clock_t);
      }
      break;
    case MU_RUI_SCREEN_MAIN_MENU:
      if (snap.active_flags & MU_RUI_FLAG_MENUS) {
        gx::redesigned::menus::draw_main_menu(dl, g_fonts, g_menu_anim, snap, pic_min, pic_max, u, clock_t, dt);
      }
      break;
    case MU_RUI_SCREEN_CSS:
      if (snap.active_flags & MU_RUI_FLAG_CSS) {
        gx::redesigned::menus::draw_css(dl, g_fonts, g_assets, g_menu_anim, snap, pic_min, pic_max, u, clock_t, dt);
      }
      break;
    case MU_RUI_SCREEN_SSS:
      if (snap.active_flags & MU_RUI_FLAG_SSS) {
        gx::redesigned::menus::draw_sss(dl, g_fonts, g_assets, g_menu_anim, snap, pic_min, pic_max, u, clock_t, dt);
      }
      break;
    case MU_RUI_SCREEN_MATCH:
      if (snap.active_flags & (MU_RUI_FLAG_HUD | MU_RUI_FLAG_PAUSE)) {
        gx::redesigned::hud::draw_match_screen(dl, g_fonts, g_assets, g_hud_anim, snap, names,
                                               pic_min, pic_max, u, clock_t, dt);
      }
      break;
    default:
      break;
  }
  dl->PopClipRect();
}

}  // namespace

void set_flags(uint32_t mask) {
  g_flags.store(mask, std::memory_order_relaxed);
}

uint32_t flags() {
  return g_flags.load(std::memory_order_relaxed);
}

uint32_t effective_flags(const RenderOptions& options, bool clean_replay_active) {
  if (!options.redesigned_ui || clean_replay_active) return 0u;
  uint32_t mask = MU_RUI_FLAG_ENABLED;
  if (options.redesigned_ui_hud)    mask |= MU_RUI_FLAG_HUD;
  if (options.redesigned_ui_pause)  mask |= MU_RUI_FLAG_PAUSE;
  if (options.redesigned_ui_menus)  mask |= MU_RUI_FLAG_TITLE | MU_RUI_FLAG_MENUS;
  if (options.redesigned_ui_select) mask |= MU_RUI_FLAG_CSS | MU_RUI_FLAG_SSS;
  return mask;
}

void publish_snapshot(const MuRedesignedUiSnapshot* snapshot) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!snapshot) {
    g_live_snap.valid = 0;
    return;
  }
  g_live_snap = *snapshot;
  g_has_source_snap = true;
  ++g_snap_generation;
}

void publish_fallback_frame(const Frame& frame) {
  // Fallback path for Static Recomp (which does not push MuRedesignedUiSnapshot directly).
  const uint32_t f = g_flags.load(std::memory_order_relaxed);
  if (!(f & MU_RUI_FLAG_ENABLED)) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_has_source_snap) return;

  MuRedesignedUiSnapshot snap{};
  snap.active_flags = f;
  snap.frame_counter = (uint32_t)frame.sequence;
  snap.scene_major = frame.scene_major;
  snap.scene_minor = frame.scene_minor;
  snap.match_pauser = -1;

  bool any_player = false;
  for (int i = 0; i < 4; ++i) {
    const auto& hp = frame.hud_players[i];
    if (!hp.present) continue;
    any_player = true;
    auto& dst = snap.hud_players[i];
    dst.present = 1;
    dst.alive = 1;
    dst.char_kind = 0xFF;
    dst.team = (int8_t)hp.team;
    dst.stocks = (uint8_t)std::clamp(hp.stocks, 0, 99);
    dst.damage = (int16_t)std::max(0, hp.damage);
    dst.damage_exact = hp.percent > 0.0f ? hp.percent : (float)dst.damage;
    dst.hitstun = hp.stunned ? 1 : 0;
    dst.nana_damage = (int16_t)hp.nana_damage;
    dst.tag_x = hp.tag_x;
    dst.tag_y = hp.tag_y;
    dst.tag_visible = (hp.tag_visible && std::isfinite(hp.tag_x) && std::isfinite(hp.tag_y)) ? 1 : 0;
  }
  if (any_player) {
    snap.valid = 1;
    snap.screen = MU_RUI_SCREEN_MATCH;
    snap.match_active = 1;
    snap.match_is_stock = 1;
    snap.match_is_teams = (frame.hud_players[0].team >= 0) ? 1 : 0;
    snap.timer_hidden = 1;
  }
  g_live_snap = snap;
}

void set_online_names(const std::string names[4]) {
  std::lock_guard<std::mutex> lock(g_mutex);
  for (int i = 0; i < 4; ++i) {
    g_online_names[i] = names ? names[i] : std::string();
  }
}

void set_iso_path(const std::string& iso_path) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_iso_path = iso_path;
}

void set_fonts(ImFont* display, ImFont* heading, ImFont* body, ImFont* mono) {
  g_fonts.display = display;
  g_fonts.heading = heading;
  g_fonts.body = body;
  g_fonts.mono = mono;
}

void draw(ImVec2 picture_min, ImVec2 picture_max, float dt) {
  const uint32_t f = g_flags.load(std::memory_order_relaxed);
  if (!(f & MU_RUI_FLAG_ENABLED)) return;

  MuRedesignedUiSnapshot snap{};
  std::string iso;
  std::string names[4];
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    snap = g_live_snap;
    iso = g_iso_path;
    for (int i = 0; i < 4; ++i) names[i] = g_online_names[i];
  }
  if (!snap.valid) return;
  snap.active_flags = f;

  g_assets.set_iso(iso);
  g_clock += std::clamp(dt, 0.0f, 0.1f);

  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  render_snapshot_impl(snap, dl, picture_min, picture_max, g_clock, dt, names);
}

void shutdown() {
  g_assets.clear();
  std::lock_guard<std::mutex> lock(g_mutex);
  g_live_snap = {};
  g_has_source_snap = false;
  g_fonts = {};
}

void draw_snapshot_for_test(const MuRedesignedUiSnapshot& snapshot, ImDrawList* draw_list,
                            ImVec2 picture_min, ImVec2 picture_max, float time_sec, float dt) {
  std::string empty_names[4];
  render_snapshot_impl(snapshot, draw_list, picture_min, picture_max, time_sec, dt, empty_names);
}

}  // namespace gx::redesigned_ui
