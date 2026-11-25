// Automated unit + headless ImGui rendering + PNG visual preview generator for the optional
// Redesigned UI refresh (mu_redesigned_ui.h / redesigned_ui.h / ui_sources/redesigned/).
// Run without args to execute all assertions (CTest).
// Run with `--render-previews <out_dir>` to rasterize 1280x720 previews of every screen plus
// the 320x240 `redesigned.png` F1 Settings gallery thumbnail.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "redesigned_ui.h"
#include "render_options.h"
#include "mu_host.h"
#include "ui_sources/redesigned/theme.h"
#include "ui_sources/redesigned/settings_theme.inl"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void expect(bool cond, const char* msg) {
  if (!cond) {
    std::fprintf(stderr, "FAIL: %s\n", msg);
    ++g_failures;
  }
}

// ---- Minimal software rasterizer for ImDrawData (RGBA32 / Alpha8 ImGui 1.92 font atlas) ----
struct Canvas {
  int w = 0, h = 0;
  std::vector<uint8_t> rgba;

  Canvas(int width, int height) : w(width), h(height), rgba((size_t)width * height * 4, 0) {}

  void clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    for (int i = 0; i < w * h; ++i) {
      rgba[i * 4 + 0] = r;
      rgba[i * 4 + 1] = g;
      rgba[i * 4 + 2] = b;
      rgba[i * 4 + 3] = a;
    }
  }

  void blend_pixel(int x, int y, float sr, float sg, float sb, float sa) {
    if (x < 0 || y < 0 || x >= w || y >= h || sa <= 0.001f) return;
    uint8_t* dst = &rgba[((size_t)y * w + x) * 4];
    const float inv = 1.0f - sa;
    dst[0] = (uint8_t)std::clamp(sr * sa + (float)dst[0] * inv + 0.5f, 0.0f, 255.0f);
    dst[1] = (uint8_t)std::clamp(sg * sa + (float)dst[1] * inv + 0.5f, 0.0f, 255.0f);
    dst[2] = (uint8_t)std::clamp(sb * sa + (float)dst[2] * inv + 0.5f, 0.0f, 255.0f);
    dst[3] = 255;
  }
};

struct AtlasSampler {
  const uint8_t* pixels = nullptr;
  int w = 0, h = 0, bpp = 4;

  void sample(float u, float v, float& tr, float& tg, float& tb, float& ta) const {
    if (!pixels || w <= 0 || h <= 0) {
      tr = tg = tb = ta = 1.0f;
      return;
    }
    const float fx = std::clamp(u, 0.0f, 1.0f) * (float)(w - 1);
    const float fy = std::clamp(v, 0.0f, 1.0f) * (float)(h - 1);
    const int x0 = (int)fx, y0 = (int)fy;
    const int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
    const float dx = fx - (float)x0, dy = fy - (float)y0;
    auto at = [&](int x, int y, int ch) -> float {
      if (bpp == 1) return ch == 3 ? (float)pixels[(size_t)y * w + x] / 255.0f : 1.0f;
      return (float)pixels[((size_t)y * w + x) * 4 + ch] / 255.0f;
    };
    auto bilerp = [&](int ch) -> float {
      const float a = at(x0, y0, ch) * (1.0f - dx) + at(x1, y0, ch) * dx;
      const float b = at(x0, y1, ch) * (1.0f - dx) + at(x1, y1, ch) * dx;
      return a * (1.0f - dy) + b * dy;
    };
    tr = bilerp(0);
    tg = bilerp(1);
    tb = bilerp(2);
    ta = bilerp(3);
  }
};

void rasterize_draw_data(Canvas& canvas, ImDrawData* dd) {
  if (!dd) return;
  auto& io = ImGui::GetIO();
  auto sampler_for = [&](const ImDrawCmd& cmd) -> AtlasSampler {
    AtlasSampler s{};
    ImTextureData* td = cmd.TexRef._TexData ? cmd.TexRef._TexData : (io.Fonts ? io.Fonts->TexData : nullptr);
    if (td && td->GetPixels()) {
      s.pixels = (const uint8_t*)td->GetPixels();
      s.w = td->Width;
      s.h = td->Height;
      s.bpp = td->BytesPerPixel;
    }
    return s;
  };

  auto edge = [](float ax, float ay, float bx, float by, float cx, float cy) -> float {
    return (cx - ax) * (by - ay) - (cy - ay) * (bx - ax);
  };

  for (int li = 0; li < dd->CmdListsCount; ++li) {
    const ImDrawList* dl = dd->CmdLists[li];
    for (int ci = 0; ci < dl->CmdBuffer.Size; ++ci) {
      const ImDrawCmd& cmd = dl->CmdBuffer[ci];
      const AtlasSampler atlas = sampler_for(cmd);
      const int clip_x0 = std::max(0, (int)std::floor(cmd.ClipRect.x));
      const int clip_y0 = std::max(0, (int)std::floor(cmd.ClipRect.y));
      const int clip_x1 = std::min(canvas.w, (int)std::ceil(cmd.ClipRect.z));
      const int clip_y1 = std::min(canvas.h, (int)std::ceil(cmd.ClipRect.w));
      if (clip_x1 <= clip_x0 || clip_y1 <= clip_y0) continue;

      for (unsigned int ei = 0; ei + 2 < cmd.ElemCount; ei += 3) {
        const ImDrawVert& v0 = dl->VtxBuffer[cmd.VtxOffset + dl->IdxBuffer[cmd.IdxOffset + ei + 0]];
        const ImDrawVert& v1 = dl->VtxBuffer[cmd.VtxOffset + dl->IdxBuffer[cmd.IdxOffset + ei + 1]];
        const ImDrawVert& v2 = dl->VtxBuffer[cmd.VtxOffset + dl->IdxBuffer[cmd.IdxOffset + ei + 2]];

        float area = edge(v0.pos.x, v0.pos.y, v1.pos.x, v1.pos.y, v2.pos.x, v2.pos.y);
        if (std::fabs(area) < 1e-4f) continue;

        const ImDrawVert *a = &v0, *b = &v1, *c = &v2;
        if (area < 0.0f) {
          std::swap(b, c);
          area = -area;
        }
        const float inv_area = 1.0f / area;

        const int min_x = std::max(clip_x0, (int)std::floor(std::min({a->pos.x, b->pos.x, c->pos.x})));
        const int max_x = std::min(clip_x1 - 1, (int)std::ceil(std::max({a->pos.x, b->pos.x, c->pos.x})));
        const int min_y = std::max(clip_y0, (int)std::floor(std::min({a->pos.y, b->pos.y, c->pos.y})));
        const int max_y = std::min(clip_y1 - 1, (int)std::ceil(std::max({a->pos.y, b->pos.y, c->pos.y})));
        if (min_x > max_x || min_y > max_y) continue;

        const float r0 = (float)((a->col >> 0) & 0xFF), g0 = (float)((a->col >> 8) & 0xFF),
                    b0 = (float)((a->col >> 16) & 0xFF), a0 = (float)((a->col >> 24) & 0xFF) / 255.0f;
        const float r1 = (float)((b->col >> 0) & 0xFF), g1 = (float)((b->col >> 8) & 0xFF),
                    b1 = (float)((b->col >> 16) & 0xFF), a1 = (float)((b->col >> 24) & 0xFF) / 255.0f;
        const float r2 = (float)((c->col >> 0) & 0xFF), g2 = (float)((c->col >> 8) & 0xFF),
                    b2 = (float)((c->col >> 16) & 0xFF), a2 = (float)((c->col >> 24) & 0xFF) / 255.0f;

        for (int y = min_y; y <= max_y; ++y) {
          const float py = (float)y + 0.5f;
          for (int x = min_x; x <= max_x; ++x) {
            const float px = (float)x + 0.5f;
            const float w0 = edge(b->pos.x, b->pos.y, c->pos.x, c->pos.y, px, py);
            if (w0 < 0.0f) continue;
            const float w1 = edge(c->pos.x, c->pos.y, a->pos.x, a->pos.y, px, py);
            if (w1 < 0.0f) continue;
            const float w2 = edge(a->pos.x, a->pos.y, b->pos.x, b->pos.y, px, py);
            if (w2 < 0.0f) continue;

            const float l0 = w0 * inv_area, l1 = w1 * inv_area, l2 = w2 * inv_area;
            const float u = a->uv.x * l0 + b->uv.x * l1 + c->uv.x * l2;
            const float v = a->uv.y * l0 + b->uv.y * l1 + c->uv.y * l2;
            float tr, tg, tb, ta;
            atlas.sample(u, v, tr, tg, tb, ta);
            const float sr = (r0 * l0 + r1 * l1 + r2 * l2) * tr;
            const float sg = (g0 * l0 + g1 * l1 + g2 * l2) * tg;
            const float sb = (b0 * l0 + b1 * l1 + b2 * l2) * tb;
            const float sa = (a0 * l0 + a1 * l1 + a2 * l2) * ta;
            canvas.blend_pixel(x, y, sr, sg, sb, sa);
          }
        }
      }
    }
  }
}

void verify_draw_data(const char* name, ImDrawData* dd) {
  expect(dd != nullptr, name);
  if (!dd) return;
  int total_vtx = 0, total_idx = 0;
  bool all_finite = true;
  bool all_in_bounds = true;
  for (int i = 0; i < dd->CmdListsCount; ++i) {
    const ImDrawList* dl = dd->CmdLists[i];
    total_vtx += dl->VtxBuffer.Size;
    total_idx += dl->IdxBuffer.Size;
    for (int v = 0; v < dl->VtxBuffer.Size; ++v) {
      if (!std::isfinite(dl->VtxBuffer[v].pos.x) || !std::isfinite(dl->VtxBuffer[v].pos.y)) {
        all_finite = false;
      }
    }
    for (int idx = 0; idx < dl->IdxBuffer.Size; ++idx) {
      if ((int)dl->IdxBuffer[idx] >= dl->VtxBuffer.Size) {
        all_in_bounds = false;
      }
    }
  }
  expect(total_vtx > 0 && total_idx > 0, name);
  expect(all_finite, name);
  expect(all_in_bounds, name);
}

// Simulated stylized Battlefield stage scene drawn behind HUD/Title/Pause previews so visual
// inspection evaluates contrast & legibility over an actual game-like backdrop.
void draw_simulated_stage_backdrop(ImDrawList* dl, float w, float h) {
  dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(w, h),
                              IM_COL32(22, 34, 64, 255), IM_COL32(42, 62, 108, 255),
                              IM_COL32(18, 26, 48, 255), IM_COL32(12, 18, 34, 255));
  // Distant glowing horizon nebula
  dl->AddCircleFilled(ImVec2(w * 0.50f, h * 0.42f), h * 0.38f, IM_COL32(84, 126, 204, 42), 64);
  dl->AddCircleFilled(ImVec2(w * 0.50f, h * 0.44f), h * 0.22f, IM_COL32(138, 178, 244, 38), 48);
  // Main Battlefield stage platform
  const float cx = w * 0.5f, fy = h * 0.62f, sw = w * 0.48f;
  const ImVec2 base[4] = {
    ImVec2(cx - sw * 0.5f, fy), ImVec2(cx + sw * 0.5f, fy),
    ImVec2(cx + sw * 0.30f, fy + h * 0.22f), ImVec2(cx - sw * 0.30f, fy + h * 0.22f)
  };
  dl->AddConvexPolyFilled(base, 4, IM_COL32(34, 44, 68, 255));
  dl->AddRectFilled(ImVec2(cx - sw * 0.5f, fy - 6), ImVec2(cx + sw * 0.5f, fy + 6), IM_COL32(136, 166, 212, 255), 2.0f);
  // Three floating platforms
  const float pw = w * 0.13f;
  dl->AddRectFilled(ImVec2(cx - sw * 0.40f, h * 0.46f), ImVec2(cx - sw * 0.40f + pw, h * 0.46f + 7), IM_COL32(146, 176, 224, 240));
  dl->AddRectFilled(ImVec2(cx + sw * 0.40f - pw, h * 0.46f), ImVec2(cx + sw * 0.40f, h * 0.46f + 7), IM_COL32(146, 176, 224, 240));
  dl->AddRectFilled(ImVec2(cx - pw * 0.5f, h * 0.31f), ImVec2(cx + pw * 0.5f, h * 0.31f + 7), IM_COL32(146, 176, 224, 240));
  // Stylized fighter silhouettes on stage
  dl->AddCircleFilled(ImVec2(w * 0.37f, fy - 34), 22, IM_COL32(236, 158, 72, 220), 24);
  dl->AddCircleFilled(ImVec2(w * 0.61f, h * 0.46f - 28), 22, IM_COL32(88, 152, 240, 220), 24);
}

void run_abi_and_options_tests() {
  expect(MU_HOST_API_VERSION == 34u, "MU_HOST_API_VERSION bumped to 34");
  expect(MU_RUI_CSS_SLOTS == 26, "CSS has 26 slots (2x13)");
  expect(MU_RUI_SSS_PRIMARY_COUNT + MU_RUI_SSS_EXTRA_COUNT == 30, "SSS covers all 30 icons");

  // Verify 2x13 CSS bijective mapping (0..25)
  std::array<int, 26> css_seen{};
  for (int s = 0; s < MU_RUI_CSS_SLOTS; ++s) {
    const uint8_t icon = mu_rui_css_slot_to_icon(s);
    expect(icon < 26, "CSS slot maps to valid icon 0..25");
    if (icon < 26) css_seen[icon]++;
  }
  for (int i = 0; i < 26; ++i) expect(css_seen[i] == 1, "Every CSS icon mapped exactly once");
  expect(mu_rui_css_slot_to_icon(25) == 25, "Bottom-right slot 25 is Random");

  // Verify SSS primary (7) + extra (23) cover 0..29 bijectively
  std::array<int, 30> sss_seen{};
  for (int i = 0; i < MU_RUI_SSS_PRIMARY_COUNT; ++i) {
    const uint8_t icon = mu_rui_sss_primary_icon(i);
    expect(icon < 30, "Primary SSS icon in range");
    if (icon < 30) sss_seen[icon]++;
  }
  for (int i = 0; i < MU_RUI_SSS_EXTRA_COUNT; ++i) {
    const uint8_t icon = mu_rui_sss_extra_icon(i);
    expect(icon < 30, "Extra SSS icon in range");
    if (icon < 30) sss_seen[icon]++;
  }
  for (int i = 0; i < 30; ++i) expect(sss_seen[i] == 1, "Every Melee stage icon 0..29 mapped exactly once");
  expect(mu_rui_sss_primary_icon(0) == 29, "SSS slot 0 is Random");
  expect(mu_rui_sss_primary_icon(1) == 25, "SSS slot 1 is Final Destination");
  expect(mu_rui_sss_primary_icon(2) == 24, "SSS slot 2 is Battlefield");
  expect(mu_rui_sss_primary_icon(6) == 18, "SSS slot 6 is Counterpick Pokemon Stadium");

  // Verify RenderOptions defaults & Clean Replays suppression
  gx::RenderOptions opts{};
  expect(opts.redesigned_ui == false, "Redesigned UI is OFF by default");
  expect(gx::redesigned_ui::effective_flags(opts, false) == 0u, "Default flags are 0");
  opts.redesigned_ui = true;
  expect(gx::redesigned_ui::effective_flags(opts, false) == MU_RUI_FLAG_ALL, "All flags active when enabled");
  expect(gx::redesigned_ui::effective_flags(opts, true) == 0u, "Clean Replays suppresses Redesigned UI completely");
  opts.redesigned_ui_select = false;
  expect((gx::redesigned_ui::effective_flags(opts, false) & (MU_RUI_FLAG_CSS | MU_RUI_FLAG_SSS)) == 0u,
         "Granular select toggle disables CSS/SSS bits");
  expect((gx::redesigned_ui::effective_flags(opts, false) & MU_RUI_FLAG_HUD) != 0u,
         "HUD bit stays active when select toggle is off");
}

void run_headless_render_tests(const std::string& out_dir) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

  ImFont* f_disp = nullptr;
  ImFont* f_head = nullptr;
  ImFont* f_body = nullptr;
#ifdef _WIN32
  if (std::filesystem::exists("C:\\Windows\\Fonts\\bahnschrift.ttf")) {
    f_disp = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\bahnschrift.ttf", 64.0f);
    f_head = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\bahnschrift.ttf", 28.0f);
    f_body = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\bahnschrift.ttf", 18.0f);
  }
#endif
  if (!f_disp) {
    f_disp = io.Fonts->AddFontDefaultVector();
    f_head = f_disp;
    f_body = f_disp;
  }
  gx::redesigned_ui::set_fonts(f_disp, f_head, f_body, f_disp);

  auto render_scene = [&](const char* name, const char* png_name, int w, int h,
                          auto&& draw_fn) {
    // Run a 1-frame pre-warm + ImGui 1.92 texture status ack so dynamic font atlas growth
    // completes before rasterizing the final frame (matching real GPU backend behavior).
    for (int pass = 0; pass < 2; ++pass) {
      io.DisplaySize = ImVec2((float)w, (float)h);
      io.DeltaTime = 1.0f / 60.0f;
      ImGui::NewFrame();
      draw_fn(ImGui::GetBackgroundDrawList(), (float)w, (float)h);
      ImGui::Render();
      ImDrawData* dd = ImGui::GetDrawData();
      if (dd && dd->Textures) {
        for (int ti = 0; ti < dd->Textures->Size; ++ti) {
          ImTextureData* td = (*dd->Textures)[ti];
          if (td && (td->Status == ImTextureStatus_WantCreate || td->Status == ImTextureStatus_WantUpdates)) {
            td->SetTexID((ImTextureID)(intptr_t)(td->UniqueID + 1));
            td->SetStatus(ImTextureStatus_OK);
          } else if (td && td->Status == ImTextureStatus_WantDestroy) {
            td->SetTexID(ImTextureID_Invalid);
            td->SetStatus(ImTextureStatus_Destroyed);
          }
        }
      }
      if (pass == 1) {
        verify_draw_data(name, dd);
        if (!out_dir.empty() && png_name) {
          std::filesystem::create_directories(out_dir);
          Canvas canvas(w, h);
          canvas.clear(7, 11, 18, 255);
          rasterize_draw_data(canvas, dd);
          const std::string path = (std::filesystem::path(out_dir) / png_name).string();
          expect(stbi_write_png(path.c_str(), w, h, 4, canvas.rgba.data(), w * 4) != 0, path.c_str());
        }
      }
    }
  };

  // 1. Title Screen
  {
    MuRedesignedUiSnapshot s{};
    s.valid = 1; s.screen = MU_RUI_SCREEN_TITLE; s.active_flags = MU_RUI_FLAG_ALL; s.title_ready = 1;
    render_scene("title", "01_title.png", 1280, 720, [&](ImDrawList* dl, float w, float h) {
      draw_simulated_stage_backdrop(dl, w, h);
      gx::redesigned_ui::draw_snapshot_for_test(s, dl, ImVec2(0, 0), ImVec2(w, h), 1.4f, 1.0f / 60.0f);
    });
  }

  // 2. Main Menu
  {
    MuRedesignedUiSnapshot s{};
    s.valid = 1; s.screen = MU_RUI_SCREEN_MAIN_MENU; s.active_flags = MU_RUI_FLAG_ALL;
    s.menu_kind = 0; s.menu_selection = 1; s.menu_item_count = 6; s.menu_supported = 1;
    render_scene("main_menu", "02_main_menu.png", 1280, 720, [&](ImDrawList* dl, float w, float h) {
      gx::redesigned_ui::draw_snapshot_for_test(s, dl, ImVec2(0, 0), ImVec2(w, h), 2.0f, 1.0f);
    });
  }

  // 3. Online Play Submenu
  {
    MuRedesignedUiSnapshot s{};
    s.valid = 1; s.screen = MU_RUI_SCREEN_MAIN_MENU; s.active_flags = MU_RUI_FLAG_ALL;
    s.menu_kind = 8; s.menu_selection = 1; s.menu_item_count = 6; s.menu_supported = 1;
    render_scene("online_menu", "03_online_menu.png", 1280, 720, [&](ImDrawList* dl, float w, float h) {
      gx::redesigned_ui::draw_snapshot_for_test(s, dl, ImVec2(0, 0), ImVec2(w, h), 2.5f, 1.0f);
    });
  }

  // 4. Character Select Screen (2x13 compact grid + Ready To Fight crimson banner)
  {
    MuRedesignedUiSnapshot s{};
    s.valid = 1; s.screen = MU_RUI_SCREEN_CSS; s.active_flags = MU_RUI_FLAG_ALL;
    s.css_active = 1; s.css_ready_to_fight = 1; s.css_stocks = 4; s.css_time_minutes = 8;
    s.css_mode = 1; s.css_door_count = 4; s.css_unlocked_mask = 0xFFFFFFFFu;
    // P1 Fox (locked on bottom row col 1 -> x=-20.0, y=-11.25)
    s.css_ports[0].kind = 0; s.css_ports[0].sel_icon = 10; s.css_ports[0].char_kind = 2;
    s.css_ports[0].costume = 0; s.css_ports[0].locked = 1;
    s.css_ports[0].puck_x = -20.0f; s.css_ports[0].puck_y = -11.25f;
    s.css_ports[0].hand_x = -18.5f; s.css_ports[0].hand_y = -10.0f;
    std::snprintf(s.css_ports[0].nametag, sizeof s.css_ports[0].nametag, "MANG0");
    // P2 Captain Falcon (locked on top row col 7 -> x=+4.0, y=-6.75)
    s.css_ports[1].kind = 0; s.css_ports[1].sel_icon = 7; s.css_ports[1].char_kind = 0;
    s.css_ports[1].costume = 2; s.css_ports[1].locked = 1;
    s.css_ports[1].puck_x = 4.0f; s.css_ports[1].puck_y = -6.75f;
    s.css_ports[1].hand_x = 5.5f; s.css_ports[1].hand_y = -5.8f;
    // P3 Marth (hovering on bottom row col 10 -> x=+16.0, y=-11.25)
    s.css_ports[2].kind = 0; s.css_ports[2].sel_icon = 23; s.css_ports[2].char_kind = 9;
    s.css_ports[2].costume = 1; s.css_ports[2].locked = 0; s.css_ports[2].holding_puck = 1;
    s.css_ports[2].puck_x = 16.0f; s.css_ports[2].puck_y = -11.25f;
    s.css_ports[2].hand_x = 16.0f; s.css_ports[2].hand_y = -11.25f;
    // P4 Open
    s.css_ports[3].kind = 3; s.css_ports[3].sel_icon = 0xFF; s.css_ports[3].char_kind = 0xFF;
    render_scene("css", "04_css.png", 1280, 720, [&](ImDrawList* dl, float w, float h) {
      // Run 2 steps so Ready To Fight ease-in alpha settles at 1.0
      gx::redesigned_ui::draw_snapshot_for_test(s, dl, ImVec2(0, 0), ImVec2(w, h), 3.0f, 1.0f);
    });
  }

  // 5. Stage Select Screen (Horizontal strip + Ban slash + Frozen Stadium badge)
  {
    MuRedesignedUiSnapshot s{};
    s.valid = 1; s.screen = MU_RUI_SCREEN_SSS; s.active_flags = MU_RUI_FLAG_ALL;
    s.sss_active = 1; s.sss_hovered_index = 25; s.sss_hovered_stkind = 0x20;
    s.sss_frozen_stadium = 1; s.sss_unlocked_mask = 0x3FFFFFFFu;
    s.sss_struck_mask = (1u << 24) | (1u << 6); // Battlefield & FoD struck
    s.sss_cursor_x = -7.2f; s.sss_cursor_y = 2.5f;
    render_scene("sss", "05_sss.png", 1280, 720, [&](ImDrawList* dl, float w, float h) {
      gx::redesigned_ui::draw_snapshot_for_test(s, dl, ImVec2(0, 0), ImVec2(w, h), 4.0f, 1.0f);
    });
  }

  // 6. Match HUD 2P (Fox vs Falco over Battlefield backdrop)
  {
    auto make_hp = [](uint8_t ckind, uint8_t costume, int8_t team, uint8_t stocks, int16_t dmg,
                      int16_t nana, uint8_t hitstun, uint8_t is_cpu, uint8_t alive,
                      float tx, float ty, uint8_t tvis) {
      MuRuiHudPlayer p{};
      p.present = 1; p.is_cpu = is_cpu; p.alive = alive; p.hitstun = hitstun;
      p.char_kind = ckind; p.costume = costume; p.team = team; p.stocks = stocks;
      p.damage = dmg; p.nana_damage = nana; p.damage_exact = (float)dmg;
      p.tag_x = tx; p.tag_y = ty; p.tag_visible = tvis;
      return p;
    };
    MuRedesignedUiSnapshot s{};
    s.valid = 1; s.screen = MU_RUI_SCREEN_MATCH; s.active_flags = MU_RUI_FLAG_ALL;
    s.match_active = 1; s.match_is_stock = 1; s.timer_enabled = 1;
    s.timer_seconds = 402; s.timer_centiseconds = 18; s.match_pauser = -1;
    s.hud_players[0] = make_hp(2, 0, -1, 4, 28, -1, 0, 0, 1, 236.0f, 260.0f, 1);
    s.hud_players[1] = make_hp(20, 1, -1, 3, 124, -1, 1, 0, 1, 392.0f, 190.0f, 1);
    render_scene("match_hud_2p", "06_match_hud_2p.png", 1280, 720, [&](ImDrawList* dl, float w, float h) {
      draw_simulated_stage_backdrop(dl, w, h);
      gx::redesigned_ui::draw_snapshot_for_test(s, dl, ImVec2(0, 0), ImVec2(w, h), 5.0f, 1.0f / 60.0f);
    });
  }

  // 7. Match HUD 4P Teams + Nana + GAME! Banner
  {
    auto make_hp = [](uint8_t ckind, uint8_t costume, int8_t team, uint8_t stocks, int16_t dmg,
                      int16_t nana, uint8_t hitstun, uint8_t is_cpu, uint8_t alive) {
      MuRuiHudPlayer p{};
      p.present = 1; p.is_cpu = is_cpu; p.alive = alive; p.hitstun = hitstun;
      p.char_kind = ckind; p.costume = costume; p.team = team; p.stocks = stocks;
      p.damage = dmg; p.nana_damage = nana; p.damage_exact = (float)dmg;
      return p;
    };
    MuRedesignedUiSnapshot s{};
    s.valid = 1; s.screen = MU_RUI_SCREEN_MATCH; s.active_flags = MU_RUI_FLAG_ALL;
    s.match_active = 1; s.match_is_stock = 1; s.match_is_teams = 1; s.timer_enabled = 1;
    s.timer_seconds = 7; s.timer_centiseconds = 45; s.match_pauser = -1;
    s.match_banner = MU_RUI_BANNER_GAME; s.match_banner_frame = 12;
    s.hud_players[0] = make_hp(2, 0, 0, 2, 64, -1, 0, 0, 1);
    s.hud_players[1] = make_hp(14, 0, 0, 3, 18, 47, 0, 0, 1);
    s.hud_players[2] = make_hp(9, 1, 1, 1, 152, -1, 1, 0, 1);
    s.hud_players[3] = make_hp(15, 2, 1, 0, 0, -1, 0, 1, 0);
    render_scene("match_hud_4p_banner", "07_match_hud_4p_banner.png", 1280, 720, [&](ImDrawList* dl, float w, float h) {
      draw_simulated_stage_backdrop(dl, w, h);
      gx::redesigned_ui::draw_snapshot_for_test(s, dl, ImVec2(0, 0), ImVec2(w, h), 6.0f, 1.0f / 60.0f);
    });
  }

  // 8. Pause Overlay
  {
    auto make_hp = [](uint8_t ckind, uint8_t stocks, int16_t dmg) {
      MuRuiHudPlayer p{};
      p.present = 1; p.alive = 1; p.char_kind = ckind; p.team = -1;
      p.stocks = stocks; p.damage = dmg; p.nana_damage = -1; p.damage_exact = (float)dmg;
      return p;
    };
    MuRedesignedUiSnapshot s{};
    s.valid = 1; s.screen = MU_RUI_SCREEN_MATCH; s.active_flags = MU_RUI_FLAG_ALL;
    s.match_active = 1; s.match_is_stock = 1; s.timer_enabled = 1;
    s.timer_seconds = 315; s.timer_centiseconds = 50;
    s.match_paused = 1; s.match_pauser = 0; s.match_pause_flags = 5;
    s.hud_players[0] = make_hp(2, 4, 42);
    s.hud_players[1] = make_hp(0, 3, 98);
    render_scene("pause", "08_pause.png", 1280, 720, [&](ImDrawList* dl, float w, float h) {
      draw_simulated_stage_backdrop(dl, w, h);
      gx::redesigned_ui::draw_snapshot_for_test(s, dl, ImVec2(0, 0), ImVec2(w, h), 7.0f, 1.0f);
    });
  }

  // 9. F1 Settings "Redesigned" Style Gallery Preview Card (320x240)
  render_scene("settings_preview", "redesigned.png", 320, 240, [&](ImDrawList* dl, float w, float h) {
    draw_simulated_stage_backdrop(dl, w, h);
    const ImVec2 p0(16, 12), p1(w - 16, h - 12);
    const ImU32 accent = gx::redesigned::settings_theme::accent_for(0);
    gx::redesigned::settings_theme::draw_panel_backdrop(dl, p0, p1, 0.96f, accent);
    gx::redesigned::theme::draw_text_tracked(dl, f_body, 7.5f, ImVec2(p0.x + 12, p0.y + 8),
                                             gx::redesigned::theme::silver(0.75f), "MELEE UNLOCKED  |  REDESIGNED", 1.1f, false);
    gx::redesigned::theme::draw_text_tracked(dl, f_head, 16.0f, ImVec2(p0.x + 11, p0.y + 18),
                                             gx::redesigned::theme::white(1.0f), "PC SETTINGS", 1.5f, false);
    dl->AddLine(ImVec2(p0.x + 12, p0.y + 38), ImVec2(p1.x - 12, p0.y + 38), gx::redesigned::theme::hairline(0.45f), 1.0f);
    static const char* nav[6] = {"VIDEO", "AUDIO", "GAME", "CONTROLS", "OVERLAYS", "CUSTOMIZE"};
    for (int i = 0; i < 6; ++i) {
      const ImVec2 na(p0.x + 10, p0.y + 44 + i * 24.0f), nb(p0.x + 96, na.y + 21.0f);
      const bool sel = (i == 5);
      if (sel) {
        dl->AddRectFilled(na, nb, gx::redesigned::theme::rgba(255, 255, 255, 0.11f));
        dl->AddRectFilled(na, ImVec2(na.x + 2.5f, nb.y), accent);
        dl->AddTriangleFilled(ImVec2(na.x + 6, (na.y + nb.y) * 0.5f - 3),
                              ImVec2(na.x + 6, (na.y + nb.y) * 0.5f + 3),
                              ImVec2(na.x + 11, (na.y + nb.y) * 0.5f), gx::redesigned::theme::white(1.0f));
      }
      dl->AddText(f_body, 9.5f, ImVec2(na.x + (sel ? 16.0f : 10.0f), na.y + 5.0f),
                  sel ? gx::redesigned::theme::white(1.0f) : gx::redesigned::theme::silver(0.72f), nav[i]);
    }
    // Right preview rows
    const float rx = p0.x + 106.0f, rw = p1.x - 12.0f - rx;
    static const char* rows[4] = {"Redesigned UI", "Match HUD & Timer", "Pause Menu", "Character & Stage Select"};
    for (int i = 0; i < 4; ++i) {
      const ImVec2 ra(rx, p0.y + 48 + i * 32.0f), rb(rx + rw, ra.y + 25.0f);
      dl->AddRectFilled(ra, rb, gx::redesigned::theme::rgba(14, 22, 35, 0.92f), 2.0f);
      dl->AddLine(ImVec2(ra.x, rb.y), rb, gx::redesigned::theme::hairline(0.18f), 1.0f);
      dl->AddText(f_body, 9.5f, ImVec2(ra.x + 7, ra.y + 7), gx::redesigned::theme::white(0.94f), rows[i]);
      const ImVec2 ta(rb.x - 32, ra.y + 6), tb(rb.x - 6, rb.y - 6);
      dl->AddRectFilled(ta, tb, accent, 6.5f);
      dl->AddCircleFilled(ImVec2(tb.x - 6.5f, (ta.y + tb.y) * 0.5f), 4.5f, gx::redesigned::theme::white(1.0f), 16);
    }
    dl->AddLine(ImVec2(p0.x + 12, p1.y - 24), ImVec2(p1.x - 12, p1.y - 24), gx::redesigned::theme::hairline(0.32f), 1.0f);
    dl->AddText(f_body, 8.5f, ImVec2(p0.x + 12, p1.y - 18), gx::redesigned::theme::silver(0.85f), "F1 RETURN");
    dl->AddRectFilled(ImVec2(p1.x - 64, p1.y - 20), ImVec2(p1.x - 12, p1.y - 6), gx::redesigned::theme::crimson(0.90f), 2.0f);
    dl->AddText(f_body, 8.0f, ImVec2(p1.x - 56, p1.y - 17), gx::redesigned::theme::white(1.0f), "QUIT GAME");
  });

  gx::redesigned_ui::shutdown();
  ImGui::DestroyContext();
}

}  // namespace

int main(int argc, char** argv) {
  std::string out_dir;
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::strcmp(argv[i], "--render-previews") == 0) out_dir = argv[i + 1];
  }
  run_abi_and_options_tests();
  run_headless_render_tests(out_dir);
  if (g_failures > 0) {
    std::fprintf(stderr, "redesigned_ui_test: %d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("redesigned_ui_test: all checks passed\n");
  return 0;
}
