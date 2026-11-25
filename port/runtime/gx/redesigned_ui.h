// Thin host adapter for the optional Redesigned UI refresh (ui_sources/redesigned/).
// Pure presentation: never modifies game memory or simulation state. Safe online & in replays.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "../abi/mu_redesigned_ui.h"
#include "imgui.h"
#include <cstdint>
#include <string>

namespace gx {
struct Frame;
struct RenderOptions;

namespace redesigned_ui {

// Live flag bitmask (MU_RUI_FLAG_*), queried by the Source Port game DLL via MuHostApi.
void set_flags(uint32_t mask);
uint32_t flags();

// Computes the effective MU_RUI_FLAG_* mask from RenderOptions + Clean Replays suppression.
uint32_t effective_flags(const RenderOptions& options, bool clean_replay_active);

// Called from the game thread (Source Port MuHostApi.redesigned_ui_snapshot). Thread-safe.
void publish_snapshot(const MuRedesignedUiSnapshot* snapshot);

// Fallback HUD publisher for Static Recomp frames when no Source Port snapshot is active.
void publish_fallback_frame(const Frame& frame);

// Supplies optional player names (e.g. online tags) for the 4 match HUD ports.
void set_online_names(const std::string names[4]);

// Configures the Melee NTSC 1.02 ISO path so fighter portraits, stock icons, and stage
// previews can be decoded directly from the player's own disc at runtime.
void set_iso_path(const std::string& iso_path);

// Supplies condensed display/heading/body/mono fonts created by the settings ImGui context.
void set_fonts(ImFont* display, ImFont* heading, ImFont* body, ImFont* mono);

// Draws the active Redesigned UI layer into the background ImGui draw list inside
// [picture_min, picture_max]. Safe to call every frame (no-op when disabled or inactive).
void draw(ImVec2 picture_min, ImVec2 picture_max, float dt);

// Releases cached GPU textures and resets state (called when ImGui context shuts down).
void shutdown();

// Direct test helpers (used by port/tests/redesigned_ui_test.cpp).
void draw_snapshot_for_test(const MuRedesignedUiSnapshot& snapshot, ImDrawList* draw_list,
                            ImVec2 picture_min, ImVec2 picture_max, float time_sec, float dt);

}  // namespace redesigned_ui
}  // namespace gx
