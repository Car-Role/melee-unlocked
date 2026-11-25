# Redesigned UI import boundary

**Status: optional in-game UI refresh and matching F1 Settings appearance.**

## Visual inspiration

The layout, typography hierarchy, dark-slate minimalist aesthetic, compact 2x13
Character Select strip, horizontal Stage Select strip, streamlined Match HUD,
and minimalist Pause overlay are inspired by Dave Abarra's motion-graphics
concept videos:

- *Melee Redesigned*: <https://www.youtube.com/watch?v=XbPBV71g1_s>
- *Melee Redesigned: Demo*: <https://www.youtube.com/watch?v=ZC3DofH1h7Q>

## Clean-room implementation and assets

All C++ rendering code, procedural vector GameCube glyphs, animations, and
fallback art in this directory are original clean-room code authored for
Melee Unlocked under GNU GPL version 2 or later.

No copyrighted Nintendo art, fonts, or third-party artwork from the concept
videos is stored in this repository. When the player supplies their own Melee
NTSC 1.02 disc image, character portraits, stock icons, and stage preview
thumbnails are decoded at runtime from that disc image (`MnSlChr.usd`,
`IfAll.usd`, `MnSlMap.usd`) using the existing GX texture decoder. Without a
disc image (or in headless tests), crisp geometric vector emblems are rendered
instead.

Typography uses the host's installed condensed sans-serif font (`Bahnschrift`
on Windows, or `SourceSans3` under SIL OFL already included in
`ui_sources/gd_melee/assets/kit/` on Linux/macOS/fallback).

## Removal boundary

All Redesigned UI drawing logic lives in `port/runtime/gx/ui_sources/redesigned/`
and is invoked through the single thin adapter `port/runtime/gx/redesigned_ui.{h,cpp}`.
Game-side display hooks live in `sourceport/game/shim/mu_redesigned_ui.c` behind
`MU_RUI_FLAG_*`. Deleting this directory, its adapter, and the `redesigned_ui`
settings toggle restores vanilla presentation without affecting any other UI
appearance or gameplay path.
