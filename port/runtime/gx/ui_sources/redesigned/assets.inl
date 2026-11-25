// Runtime Melee disc asset decoder for the optional Redesigned UI refresh.
// Decodes fighter portraits (MnSlChr.usd), stock icons (IfAll.usd), and stage previews
// (MnSlMap.usd) directly from the player's own Melee NTSC 1.02 ISO via HAL's HSD_Archive
// symbol & TexAnim tables — zero copyrighted Nintendo data is stored in this repository.
// When no disc is loaded (e.g. headless tests), clean procedural vector fallbacks render instead.
// SPDX-License-Identifier: GPL-2.0-or-later

namespace gx::redesigned::assets {

struct CachedTex {
  std::unique_ptr<ImTextureData> tex;
  int w = 0, h = 0;
  bool attempted = false;
};

struct DatView {
  const uint8_t* data = nullptr;
  uint32_t size = 0;
  bool ok(uint32_t at, uint32_t n) const { return data && at <= size && n <= size - at; }
  uint32_t u32(uint32_t at) const {
    return ok(at, 4) ? ((uint32_t)data[at] << 24 | (uint32_t)data[at + 1] << 16 |
                        (uint32_t)data[at + 2] << 8 | data[at + 3]) : 0;
  }
  uint16_t u16(uint32_t at) const {
    return ok(at, 2) ? (uint16_t)((uint16_t)data[at] << 8 | data[at + 1]) : 0;
  }
  uint8_t u8(uint32_t at) const { return ok(at, 1) ? data[at] : 0; }
};

struct DecodedImage {
  int w = 0, h = 0;
  std::vector<uint8_t> rgba;
};

// Reads a single file from a GameCube ISO/GCM by FST filename (case-insensitive).
inline std::vector<uint8_t> read_disc_file_from_iso(const std::string& iso_path, const char* wanted) {
  if (iso_path.empty() || !wanted) return {};
  std::ifstream f(std::filesystem::u8path(iso_path), std::ios::binary);
  if (!f) return {};
  auto read_at = [&](uint64_t at, void* out, size_t n) -> bool {
    f.clear();
    f.seekg((std::streamoff)at);
    f.read((char*)out, (std::streamsize)n);
    return (size_t)f.gcount() == n;
  };
  auto be32 = [](const uint8_t* p) -> uint32_t {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
  };
  char disc_id[6] = {};
  if (!read_at(0, disc_id, 6) || std::memcmp(disc_id, "GALE01", 6) != 0) return {};
  uint8_t h[8] = {};
  if (!read_at(0x424, h, 8)) return {};
  const uint32_t fst_off = be32(h), fst_size = be32(h + 4);
  if (fst_size < 12 || fst_size > 32u * 1024 * 1024) return {};
  std::vector<uint8_t> fst(fst_size);
  if (!read_at(fst_off, fst.data(), fst_size)) return {};
  const uint32_t count = be32(fst.data() + 8);
  if (!count || (uint64_t)count * 12 > fst_size) return {};
  for (uint32_t i = 1; i < count; ++i) {
    const uint8_t* e = fst.data() + (size_t)i * 12;
    if (e[0]) continue;
    const uint32_t name_at = count * 12 + (be32(e) & 0xFFFFFFu);
    if (name_at >= fst_size) continue;
    const char* name = (const char*)fst.data() + name_at;
    bool eq = true;
    for (size_t k = 0;; ++k) {
      const char a = (char)std::tolower((unsigned char)name[k]);
      const char b = (char)std::tolower((unsigned char)wanted[k]);
      if (a != b) { eq = false; break; }
      if (!a) break;
    }
    if (!eq) continue;
    const uint32_t off = be32(e + 4), sz = be32(e + 8);
    if (sz < 0x20 || sz > 64u * 1024 * 1024) return {};
    std::vector<uint8_t> out(sz);
    if (!read_at(off, out.data(), sz)) return {};
    return out;
  }
  return {};
}

// Finds a root symbol's data-block offset in a HAL .dat/.usd archive.
inline bool find_dat_root(const std::vector<uint8_t>& file, const char* symbol,
                          DatView* out_view, uint32_t* out_root_off) {
  if (file.size() < 0x20 || !symbol) return false;
  auto be32 = [&](size_t at) -> uint32_t {
    return ((uint32_t)file[at] << 24) | ((uint32_t)file[at + 1] << 16) |
           ((uint32_t)file[at + 2] << 8) | file[at + 3];
  };
  const uint32_t data_size = be32(4);
  const uint32_t reloc_cnt = be32(8);
  const uint32_t root_cnt = be32(12);
  const uint32_t ref_cnt = be32(16);
  const uint64_t reloc_off = 0x20ull + data_size;
  const uint64_t roots_off = reloc_off + (uint64_t)reloc_cnt * 4ull;
  const uint64_t str_off = roots_off + (uint64_t)(root_cnt + ref_cnt) * 8ull;
  if (str_off > file.size() || data_size > file.size() - 0x20) return false;
  for (uint32_t i = 0; i < root_cnt; ++i) {
    const size_t entry = (size_t)roots_off + (size_t)i * 8;
    const uint32_t node_off = be32(entry);
    const uint32_t name_off = be32(entry + 4);
    if (str_off + name_off >= file.size()) continue;
    const char* name = (const char*)file.data() + str_off + name_off;
    if (std::strcmp(name, symbol) == 0) {
      out_view->data = file.data() + 0x20;
      out_view->size = data_size;
      *out_root_off = node_off;
      return true;
    }
  }
  return false;
}

// Decodes one HSD_ImageDesc + optional HSD_TlutDesc pair from a HAL archive data block.
inline bool decode_hsd_image(const DatView& d, uint32_t img_off, uint32_t tlut_off,
                             DecodedImage* out) {
  if (!img_off || !d.ok(img_off, 0x18) || !out) return false;
  const uint32_t pixels_off = d.u32(img_off + 0);
  const uint32_t w = d.u16(img_off + 4);
  const uint32_t h = d.u16(img_off + 6);
  const uint32_t fmt = d.u32(img_off + 8);
  if (!w || !h || w > 512 || h > 512) return false;
  const bool fmt_ok = fmt <= 6 || fmt == 8 || fmt == 9 || fmt == 10 || fmt == 14;
  if (!fmt_ok) return false;
  const uint32_t bytes = gx::texture_level_bytes(w, h, fmt);
  if (!bytes || !d.ok(pixels_off, bytes)) return false;

  const uint8_t* tlut_ptr = nullptr;
  uint32_t tlut_fmt = 0;
  const bool paletted = (fmt == 8 || fmt == 9 || fmt == 10);
  if (paletted) {
    if (!tlut_off || !d.ok(tlut_off, 0x10)) return false;
    const uint32_t lut_off = d.u32(tlut_off + 0);
    tlut_fmt = d.u32(tlut_off + 4);
    const uint32_t entries = fmt == 8 ? 16u : fmt == 9 ? 256u : 16384u;
    if (tlut_fmt > 2 || !d.ok(lut_off, entries * 2u)) return false;
    tlut_ptr = d.data + lut_off;
  }

  out->w = (int)w;
  out->h = (int)h;
  gx::decode_texture(d.data + pixels_off, w, h, fmt, tlut_ptr, tlut_fmt, out->rgba);
  return out->rgba.size() == (size_t)w * h * 4;
}

// Walks a MatAnimJoint tree collecting every TexAnim image table whose entry count >= min_entries.
struct TexAnimTable {
  uint32_t imagetbl_off = 0;
  uint32_t tluttbl_off = 0;
  uint16_t n_images = 0;
  uint16_t n_tluts = 0;
};

inline void collect_texanims(const DatView& d, uint32_t maj_off, int depth,
                             std::vector<TexAnimTable>& out,
                             std::unordered_set<uint32_t>& visited) {
  if (!maj_off || depth > 48 || !d.ok(maj_off, 0x10) || !visited.insert(maj_off).second) return;
  const uint32_t child = d.u32(maj_off + 0);
  const uint32_t next = d.u32(maj_off + 4);
  uint32_t matanim = d.u32(maj_off + 8);
  int guard = 0;
  while (matanim && d.ok(matanim, 0x10) && guard++ < 64) {
    uint32_t texanim = d.u32(matanim + 8);
    int tguard = 0;
    while (texanim && d.ok(texanim, 0x18) && tguard++ < 32) {
      const uint32_t img_tbl = d.u32(texanim + 0x0C);
      const uint32_t tlut_tbl = d.u32(texanim + 0x10);
      const uint16_t n_img = d.u16(texanim + 0x14);
      const uint16_t n_tlut = d.u16(texanim + 0x16);
      if (img_tbl && n_img > 0 && n_img <= 512 && d.ok(img_tbl, (uint32_t)n_img * 4u)) {
        out.push_back(TexAnimTable{img_tbl, tlut_tbl, n_img, n_tlut});
      }
      texanim = d.u32(texanim + 0);
    }
    matanim = d.u32(matanim + 0);
  }
  collect_texanims(d, child, depth + 1, out, visited);
  collect_texanims(d, next, depth + 1, out, visited);
}

inline std::unique_ptr<ImTextureData> upload_rgba_texture(const DecodedImage& img) {
  if (img.w <= 0 || img.h <= 0 || img.rgba.size() != (size_t)img.w * img.h * 4) return nullptr;
  auto tex = std::make_unique<ImTextureData>();
  tex->Create(ImTextureFormat_RGBA32, img.w, img.h);
  std::memcpy(tex->GetPixels(), img.rgba.data(), img.rgba.size());
  tex->SetStatus(ImTextureStatus_WantCreate);
  if (ImGui::GetCurrentContext()) {
    ImGui::GetPlatformIO().Textures.push_back(tex.get());
  }
  return tex;
}

class DiscAssetBank {
 public:
  void set_iso(const std::string& iso) {
    if (iso == iso_path_) return;
    clear();
    iso_path_ = iso;
  }

  void clear() {
    auto release_map = [](auto& m) {
      for (auto& kv : m) {
        if (kv.second.tex) {
          kv.second.tex->SetStatus(ImTextureStatus_WantDestroy);
          if (ImGui::GetCurrentContext()) {
            auto& list = ImGui::GetPlatformIO().Textures;
            for (int i = list.Size - 1; i >= 0; --i) {
              if (list[i] == kv.second.tex.get()) { list.erase(list.Data + i); break; }
            }
          }
        }
      }
      m.clear();
    };
    release_map(stock_textures_);
    release_map(css_icon_textures_);
    release_map(stage_textures_);
    ifall_bytes_.clear();
    mnslchr_bytes_.clear();
    mnslmap_bytes_.clear();
    ifall_loaded_ = mnslchr_loaded_ = mnslmap_loaded_ = false;
    stock_tables_.clear();
    css_tables_.clear();
    stage_tables_.clear();
  }

  // Returns an ImTextureData* for a fighter's stock icon (nullptr when no disc is loaded).
  ImTextureData* stock_icon(int ckind, int costume) {
    if (iso_path_.empty() || ckind < 0 || ckind >= 26) return nullptr;
    const int key = ckind * 8 + std::clamp(costume, 0, 5);
    auto& entry = stock_textures_[key];
    if (entry.attempted) return entry.tex.get();
    entry.attempted = true;
    ensure_ifall();
    if (stock_tables_.empty()) return nullptr;
    // IfAll.usd Stc_scemdls stock icon table stores icons ordered by fighter/costume frames.
    // Pick the first table with >= 26 small square textures (24x24).
    for (const auto& tbl : stock_tables_) {
      if (tbl.n_images < 26) continue;
      const int idx = std::clamp(ckind, 0, (int)tbl.n_images - 1);
      const uint32_t img_off = ifall_view_.u32(tbl.imagetbl_off + (uint32_t)idx * 4u);
      const uint32_t tlut_off = (tbl.tluttbl_off && idx < tbl.n_tluts)
                                    ? ifall_view_.u32(tbl.tluttbl_off + (uint32_t)idx * 4u)
                                    : 0;
      DecodedImage img;
      if (decode_hsd_image(ifall_view_, img_off, tlut_off, &img) && img.w <= 48 && img.h <= 48) {
        entry.w = img.w;
        entry.h = img.h;
        entry.tex = upload_rgba_texture(img);
        return entry.tex.get();
      }
    }
    return nullptr;
  }

  // Returns an ImTextureData* for a fighter's CSS square portrait icon (nullptr without disc).
  ImTextureData* css_icon(int ckind) {
    if (iso_path_.empty() || ckind < 0 || ckind >= 26) return nullptr;
    auto& entry = css_icon_textures_[ckind];
    if (entry.attempted) return entry.tex.get();
    entry.attempted = true;
    ensure_mnslchr();
    if (css_tables_.empty()) return nullptr;
    for (const auto& tbl : css_tables_) {
      if (tbl.n_images < 25) continue;
      const int idx = std::clamp(ckind, 0, (int)tbl.n_images - 1);
      const uint32_t img_off = mnslchr_view_.u32(tbl.imagetbl_off + (uint32_t)idx * 4u);
      const uint32_t tlut_off = (tbl.tluttbl_off && idx < tbl.n_tluts)
                                    ? mnslchr_view_.u32(tbl.tluttbl_off + (uint32_t)idx * 4u)
                                    : 0;
      DecodedImage img;
      if (decode_hsd_image(mnslchr_view_, img_off, tlut_off, &img) && img.w >= 48 && img.h >= 48) {
        entry.w = img.w;
        entry.h = img.h;
        entry.tex = upload_rgba_texture(img);
        return entry.tex.get();
      }
    }
    return nullptr;
  }

  // Returns an ImTextureData* for a stage preview thumbnail (nullptr without disc).
  ImTextureData* stage_preview(int stage_icon_idx) {
    if (iso_path_.empty() || stage_icon_idx < 0 || stage_icon_idx >= 29) return nullptr;
    auto& entry = stage_textures_[stage_icon_idx];
    if (entry.attempted) return entry.tex.get();
    entry.attempted = true;
    ensure_mnslmap();
    if (stage_tables_.empty()) return nullptr;
    for (const auto& tbl : stage_tables_) {
      if (tbl.n_images < 29) continue;
      const int idx = std::clamp(stage_icon_idx, 0, (int)tbl.n_images - 1);
      const uint32_t img_off = mnslmap_view_.u32(tbl.imagetbl_off + (uint32_t)idx * 4u);
      const uint32_t tlut_off = (tbl.tluttbl_off && idx < tbl.n_tluts)
                                    ? mnslmap_view_.u32(tbl.tluttbl_off + (uint32_t)idx * 4u)
                                    : 0;
      DecodedImage img;
      if (decode_hsd_image(mnslmap_view_, img_off, tlut_off, &img) && img.w >= 64) {
        entry.w = img.w;
        entry.h = img.h;
        entry.tex = upload_rgba_texture(img);
        return entry.tex.get();
      }
    }
    return nullptr;
  }

 private:
  void ensure_ifall() {
    if (ifall_loaded_) return;
    ifall_loaded_ = true;
    ifall_bytes_ = read_disc_file_from_iso(iso_path_, "IfAll.usd");
    uint32_t root = 0;
    if (!find_dat_root(ifall_bytes_, "Stc_scemdls", &ifall_view_, &root)) return;
    const uint32_t md = ifall_view_.u32(root);
    if (!md || !ifall_view_.ok(md, 0x10)) return;
    const uint32_t maj = ifall_view_.u32(md + 8);
    std::unordered_set<uint32_t> seen;
    collect_texanims(ifall_view_, maj, 0, stock_tables_, seen);
  }

  void ensure_mnslchr() {
    if (mnslchr_loaded_) return;
    mnslchr_loaded_ = true;
    mnslchr_bytes_ = read_disc_file_from_iso(iso_path_, "MnSlChr.usd");
    uint32_t root = 0;
    if (!find_dat_root(mnslchr_bytes_, "MnSelectChrDataTable", &mnslchr_view_, &root)) return;
    std::unordered_set<uint32_t> seen;
    // Walk StaticModelDesc slots inside MnSelectChrDataTable.models (+0x10..+0x90)
    for (uint32_t off = 0x10; off <= 0x90; off += 0x10) {
      if (!mnslchr_view_.ok(root + off, 0x10)) break;
      const uint32_t maj = mnslchr_view_.u32(root + off + 8);
      collect_texanims(mnslchr_view_, maj, 0, css_tables_, seen);
    }
  }

  void ensure_mnslmap() {
    if (mnslmap_loaded_) return;
    mnslmap_loaded_ = true;
    mnslmap_bytes_ = read_disc_file_from_iso(iso_path_, "MnSlMap.usd");
    uint32_t root = 0;
    if (!find_dat_root(mnslmap_bytes_, "MnSelectStageDataTable", &mnslmap_view_, &root)) return;
    std::unordered_set<uint32_t> seen;
    // stage_preview is at models +0x60 (+0x70 from table root), icon_large at +0x10
    for (uint32_t off : {0x70u, 0x10u, 0x50u}) {
      if (!mnslmap_view_.ok(root + off, 0x10)) continue;
      const uint32_t maj = mnslmap_view_.u32(root + off + 8);
      collect_texanims(mnslmap_view_, maj, 0, stage_tables_, seen);
    }
  }

  std::string iso_path_;
  bool ifall_loaded_ = false, mnslchr_loaded_ = false, mnslmap_loaded_ = false;
  std::vector<uint8_t> ifall_bytes_, mnslchr_bytes_, mnslmap_bytes_;
  DatView ifall_view_, mnslchr_view_, mnslmap_view_;
  std::vector<TexAnimTable> stock_tables_, css_tables_, stage_tables_;
  std::unordered_map<int, CachedTex> stock_textures_, css_icon_textures_, stage_textures_;
};

// Crisp procedural vector fighter emblem / portrait fallback when no ISO is loaded.
inline void draw_fighter_portrait_card(ImDrawList* dl, ImFont* font, ImVec2 a, ImVec2 b,
                                       int ckind, int costume, ImTextureData* tex,
                                       float alpha = 1.0f, bool hero_mode = false) {
  const auto& fi = theme::fighter_info(ckind);
  if (tex) {
    dl->AddImage(tex->GetTexRef(), a, b, ImVec2(0, 0), ImVec2(1, 1),
                 IM_COL32(255, 255, 255, (int)(255 * alpha)));
    return;
  }
  // Procedural dark-tinted portrait tile with signature character hue & subtle costume accent
  const float shift = (float)(costume % 6) * 0.08f;
  const int r = std::clamp((int)(fi.r * (0.95f - shift * 0.3f)), 24, 240);
  const int g = std::clamp((int)(fi.g * (0.95f + shift * 0.2f)), 24, 240);
  const int bl = std::clamp((int)(fi.b * (0.95f + shift * 0.1f)), 24, 240);
  const ImU32 top_col = theme::rgba(r / 2 + 12, g / 2 + 14, bl / 2 + 20, (hero_mode ? 0.55f : 0.92f) * alpha);
  const ImU32 bot_col = theme::rgba(10, 14, 22, (hero_mode ? 0.0f : 0.96f) * alpha);
  dl->AddRectFilledMultiColor(a, b, top_col, top_col, bot_col, bot_col);

  // Subtle geometric silhouette / diagonal crest
  const float w = b.x - a.x, h = b.y - a.y;
  const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
  if (hero_mode) {
    // Atmospheric hero emblem silhouette standing behind nameplate
    const float rad = std::min(w, h) * 0.36f;
    dl->AddCircleFilled(ImVec2(c.x, c.y - h * 0.06f), rad * 1.15f,
                        theme::rgba(r, g, bl, 0.14f * alpha), 48);
    dl->AddCircle(ImVec2(c.x, c.y - h * 0.06f), rad,
                  theme::rgba(r, g, bl, 0.34f * alpha), 48, 2.0f);
    // Iconic Smash cross lines subtly behind the emblem
    dl->AddLine(ImVec2(c.x - rad * 1.1f, c.y - h * 0.06f + rad * 0.22f),
                ImVec2(c.x + rad * 1.1f, c.y - h * 0.06f + rad * 0.22f),
                theme::rgba(255, 255, 255, 0.14f * alpha), 3.0f);
    dl->AddLine(ImVec2(c.x - rad * 0.26f, c.y - h * 0.06f - rad * 1.1f),
                ImVec2(c.x - rad * 0.26f, c.y - h * 0.06f + rad * 1.1f),
                theme::rgba(255, 255, 255, 0.14f * alpha), 5.0f);
    const float fsz = std::clamp(rad * 0.95f, 28.0f, 96.0f);
    const ImVec2 ts = theme::pick_font(font)->CalcTextSizeA(fsz, FLT_MAX, 0.0f, fi.abbr);
    theme::draw_text_tracked(dl, font, fsz, ImVec2(c.x - ts.x * 0.5f, c.y - h * 0.06f - ts.y * 0.5f),
                             theme::rgba(246, 249, 253, 0.24f * alpha), fi.abbr, 2.0f, false);
  } else {
    // Compact square tile emblem (CSS grid & Match HUD portrait box)
    dl->AddTriangleFilled(ImVec2(a.x, b.y), ImVec2(b.x, a.y + h * 0.35f), b,
                          theme::rgba(r, g, bl, 0.28f * alpha));
    const float fsz = std::clamp(h * 0.44f, 11.0f, 34.0f);
    const ImVec2 ts = theme::pick_font(font)->CalcTextSizeA(fsz, FLT_MAX, 0.0f, fi.abbr);
    theme::draw_text_tracked(dl, font, fsz, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f),
                             theme::white(0.95f * alpha), fi.abbr, 0.0f, true);
  }
}

// Crisp stock icon (ISO texture or sleek glowing Smash pip fallback).
inline void draw_stock_pip(ImDrawList* dl, ImVec2 center, float radius, int ckind, int costume,
                           ImU32 port_col, ImTextureData* tex, float alpha = 1.0f) {
  (void)ckind; (void)costume;
  if (tex) {
    const ImVec2 a(center.x - radius, center.y - radius), b(center.x + radius, center.y + radius);
    dl->AddImage(tex->GetTexRef(), a, b, ImVec2(0, 0), ImVec2(1, 1),
                 IM_COL32(255, 255, 255, (int)(255 * alpha)));
    return;
  }
  // Sleek circular stock pip with subtle inner Smash cross
  dl->AddCircleFilled(ImVec2(center.x + 0.8f, center.y + 1.0f), radius,
                      theme::rgba(0, 0, 0, 0.55f * alpha), 18);
  dl->AddCircleFilled(center, radius, port_col, 18);
  dl->AddCircle(center, radius, theme::white(0.88f * alpha), 18, std::max(1.0f, radius * 0.16f));
  dl->AddLine(ImVec2(center.x - radius * 0.72f, center.y + radius * 0.24f),
              ImVec2(center.x + radius * 0.72f, center.y + radius * 0.24f),
              theme::rgba(10, 14, 22, 0.72f * alpha), std::max(1.0f, radius * 0.18f));
  dl->AddLine(ImVec2(center.x - radius * 0.24f, center.y - radius * 0.72f),
              ImVec2(center.x - radius * 0.24f, center.y + radius * 0.72f),
              theme::rgba(10, 14, 22, 0.72f * alpha), std::max(1.2f, radius * 0.26f));
}

// Crisp stage preview card (ISO texture or stylized minimalist platform graphic fallback).
inline void draw_stage_card_art(ImDrawList* dl, ImVec2 a, ImVec2 b, int stage_icon_idx,
                                ImTextureData* tex, float alpha = 1.0f, bool full_bg = false) {
  const auto& st = theme::stage_info_by_icon(stage_icon_idx);
  if (tex) {
    dl->AddImage(tex->GetTexRef(), a, b, ImVec2(0, 0), ImVec2(1, 1),
                 IM_COL32(255, 255, 255, (int)(255 * (full_bg ? 0.35f : alpha))));
    return;
  }
  const ImU32 c1 = theme::rgba(st.r1, st.g1, st.b1, (full_bg ? 0.38f : 0.92f) * alpha);
  const ImU32 c2 = theme::rgba(st.r2, st.g2, st.b2, (full_bg ? 0.65f : 0.96f) * alpha);
  dl->AddRectFilledMultiColor(a, b, c1, c1, c2, c2);

  if (stage_icon_idx == 29) return;  // Random uses crisp '?' symbol drawn by caller
  const float w = b.x - a.x, h = b.y - a.y;
  // Minimalist geometric stage silhouette (main stage + floating platforms for BF/YS/DL/FoD/PS)
  const float cx = (a.x + b.x) * 0.5f;
  const float floor_y = a.y + h * 0.68f;
  const float main_w = w * 0.56f;
  const float deck_h = std::max(2.0f, h * 0.08f);
  const ImU32 plat = theme::rgba(235, 244, 255, (full_bg ? 0.22f : 0.78f) * alpha);
  const ImU32 base = theme::rgba(14, 20, 34, (full_bg ? 0.30f : 0.62f) * alpha);
  // Main stage body trapezoid
  const ImVec2 body[4] = {
    ImVec2(cx - main_w * 0.5f, floor_y),
    ImVec2(cx + main_w * 0.5f, floor_y),
    ImVec2(cx + main_w * 0.34f, floor_y + h * 0.22f),
    ImVec2(cx - main_w * 0.34f, floor_y + h * 0.22f)
  };
  dl->AddConvexPolyFilled(body, 4, base);
  dl->AddRectFilled(ImVec2(cx - main_w * 0.5f, floor_y - deck_h * 0.5f),
                    ImVec2(cx + main_w * 0.5f, floor_y + deck_h * 0.5f), plat, 1.0f);

  const bool tri_plats = (stage_icon_idx == 24 || stage_icon_idx == 8 || stage_icon_idx == 26 || stage_icon_idx == 6);
  const bool bi_plats = (stage_icon_idx == 18);
  if (tri_plats || bi_plats) {
    const float pw = w * 0.16f;
    const float py1 = a.y + h * 0.48f;
    dl->AddRectFilled(ImVec2(cx - main_w * 0.42f, py1), ImVec2(cx - main_w * 0.42f + pw, py1 + deck_h * 0.7f), plat);
    dl->AddRectFilled(ImVec2(cx + main_w * 0.42f - pw, py1), ImVec2(cx + main_w * 0.42f, py1 + deck_h * 0.7f), plat);
    if (tri_plats) {
      const float py2 = a.y + h * 0.30f;
      dl->AddRectFilled(ImVec2(cx - pw * 0.5f, py2), ImVec2(cx + pw * 0.5f, py2 + deck_h * 0.7f), plat);
    }
  }
}

}  // namespace gx::redesigned::assets
