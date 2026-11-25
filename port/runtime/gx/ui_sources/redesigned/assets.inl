// Runtime Melee disc asset decoder for the optional Redesigned UI refresh.
// Decodes full fighter CSPs (136x188) and square CSS head icons (64x56) from MnSlChr.usd,
// costume-matched stock icons (24x24) from IfAll.usd, and full-color stage thumbnails (64x56)
// from MnSlMap.usd directly from the player's own Melee NTSC 1.02 ISO via HAL's HSD_Archive,
// JObj, TexAnim, and FObj keyframe tables — zero copyrighted Nintendo data is stored in git.
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
  float f32(uint32_t at) const {
    const uint32_t bits = u32(at);
    float v = 0.0f;
    std::memcpy(&v, &bits, sizeof v);
    return v;
  }
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

// Decodes an HSD_FObjDesc track into exact (frame -> integer value) keyframes.
inline void decode_fobj_keyframes(const DatView& d, uint32_t fobj_off,
                                  uint8_t wanted_type,
                                  std::unordered_map<int, int>& out_kf) {
  int guard = 0;
  while (fobj_off && d.ok(fobj_off, 0x14) && guard++ < 32) {
    const uint32_t next = d.u32(fobj_off + 0);
    const uint32_t length = d.u32(fobj_off + 4);
    const float start_frame = d.f32(fobj_off + 8);
    const uint8_t type = d.u8(fobj_off + 12);
    const uint8_t frac_val = d.u8(fobj_off + 13);
    const uint32_t ad = d.u32(fobj_off + 16);
    if (type == wanted_type && length > 0 && d.ok(ad, length)) {
      const uint8_t* raw = d.data + ad;
      size_t pos = 0;
      int t = (int)std::lround(start_frame);
      const int fmt = (frac_val >> 5) & 7;
      const float scale = (float)(1u << (frac_val & 0x1Fu));
      auto read_var = [&](size_t& p) -> int {
        int val = 0, shift = 0;
        while (p < length && shift < 28) {
          const uint8_t b = raw[p++];
          val |= (int)(b & 0x7Fu) << shift;
          if (!(b & 0x80u)) break;
          shift += 7;
        }
        return val;
      };
      auto read_val = [&](size_t& p) -> float {
        if (fmt == 0 && p + 4 <= length) {
          const uint32_t bits = ((uint32_t)raw[p] << 24) | ((uint32_t)raw[p + 1] << 16) |
                                ((uint32_t)raw[p + 2] << 8) | raw[p + 3];
          p += 4;
          float v = 0.0f;
          std::memcpy(&v, &bits, sizeof v);
          return v;
        }
        if (fmt == 1 && p + 2 <= length) {
          const int16_t s = (int16_t)((uint16_t)raw[p] << 8 | raw[p + 1]);
          p += 2;
          return (float)s / scale;
        }
        if (fmt == 2 && p + 2 <= length) {
          const uint16_t u = (uint16_t)((uint16_t)raw[p] << 8 | raw[p + 1]);
          p += 2;
          return (float)u / scale;
        }
        if (fmt == 3 && p + 1 <= length) {
          const int8_t s = (int8_t)raw[p++];
          return (float)s / scale;
        }
        if (p + 1 <= length) {
          return (float)raw[p++] / scale;
        }
        return 0.0f;
      };
      while (pos < length) {
        const uint8_t hdr = raw[pos++];
        const int op = hdr & 0x0F;
        int cnt = ((hdr >> 4) & 7) + 1;
        if (hdr & 0x80u) {
          const int hi = read_var(pos);
          cnt = ((hdr >> 4) & 7) + (hi << 3) + 1;
        }
        for (int i = 0; i < cnt && pos <= length; ++i) {
          if (op == 1 || op == 2 || op == 3) {
            const float v = read_val(pos);
            const int dt = read_var(pos);
            out_kf[t] = (int)std::lround(v);
            t += dt;
          } else if (op == 4) {
            const float v = read_val(pos);
            (void)read_val(pos);
            const int dt = read_var(pos);
            out_kf[t] = (int)std::lround(v);
            t += dt;
          } else if (op == 5) {
            (void)read_val(pos);
          } else if (op == 6) {
            const float v = read_val(pos);
            out_kf[t] = (int)std::lround(v);
          } else {
            pos = length;
            break;
          }
        }
      }
      break;
    }
    fobj_off = next;
  }
}

struct TexAnimTable {
  uint32_t imagetbl_off = 0;
  uint32_t tluttbl_off = 0;
  uint16_t n_images = 0;
  uint16_t n_tluts = 0;
  std::unordered_map<int, int> timg_kf;  // HSD_A_T_TIMG (1): frame -> image index
  std::unordered_map<int, int> tclt_kf;  // HSD_A_T_TCLT (10): frame -> tlut index

  bool valid() const { return imagetbl_off != 0 && n_images > 0; }

  bool resolve_frame(int frame, int* out_img_idx, int* out_tlut_idx) const {
    if (!valid()) return false;
    int img_idx = -1;
    if (!timg_kf.empty()) {
      auto it = timg_kf.find(frame);
      if (it == timg_kf.end()) return false;
      img_idx = it->second;
    } else {
      img_idx = frame;
    }
    if (img_idx < 0 || img_idx >= (int)n_images) return false;
    int tlut_idx = img_idx;
    if (!tclt_kf.empty()) {
      auto it = tclt_kf.find(frame);
      if (it != tclt_kf.end()) tlut_idx = it->second;
    }
    *out_img_idx = img_idx;
    *out_tlut_idx = (tlut_idx >= 0 && tlut_idx < (int)n_tluts) ? tlut_idx : -1;
    return true;
  }
};

inline bool parse_texanim_node(const DatView& d, uint32_t texanim, TexAnimTable* out) {
  if (!texanim || !d.ok(texanim, 0x18) || !out) return false;
  const uint32_t aobj = d.u32(texanim + 0x08);
  const uint32_t img_tbl = d.u32(texanim + 0x0C);
  const uint32_t tlut_tbl = d.u32(texanim + 0x10);
  const uint16_t n_img = d.u16(texanim + 0x14);
  const uint16_t n_tlut = d.u16(texanim + 0x16);
  if (!img_tbl || n_img == 0 || n_img > 512 || !d.ok(img_tbl, (uint32_t)n_img * 4u)) return false;
  out->imagetbl_off = img_tbl;
  out->tluttbl_off = tlut_tbl;
  out->n_images = n_img;
  out->n_tluts = n_tlut;
  out->timg_kf.clear();
  out->tclt_kf.clear();
  if (aobj && d.ok(aobj, 0x10)) {
    const uint32_t fobj = d.u32(aobj + 8);
    decode_fobj_keyframes(d, fobj, 1u, out->timg_kf);   // HSD_A_T_TIMG
    decode_fobj_keyframes(d, fobj, 10u, out->tclt_kf);  // HSD_A_T_TCLT
  }
  return true;
}

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
      TexAnimTable tbl;
      if (parse_texanim_node(d, texanim, &tbl)) out.push_back(std::move(tbl));
      texanim = d.u32(texanim + 0);
    }
    matanim = d.u32(matanim + 0);
  }
  collect_texanims(d, child, depth + 1, out, visited);
  collect_texanims(d, next, depth + 1, out, visited);
}

inline bool decode_from_texanim(const DatView& d, const TexAnimTable& tbl, int frame,
                                DecodedImage* out) {
  int img_idx = -1, tlut_idx = -1;
  if (!tbl.resolve_frame(frame, &img_idx, &tlut_idx)) return false;
  const uint32_t img_off = d.u32(tbl.imagetbl_off + (uint32_t)img_idx * 4u);
  const uint32_t tlut_off = (tbl.tluttbl_off && tlut_idx >= 0)
                                ? d.u32(tbl.tluttbl_off + (uint32_t)tlut_idx * 4u)
                                : 0;
  return decode_hsd_image(d, img_off, tlut_off, out);
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
    release_map(csp_textures_);
    release_map(stage_textures_);
    ifall_bytes_.clear();
    mnslchr_bytes_.clear();
    mnslmap_bytes_.clear();
    ifall_loaded_ = mnslchr_loaded_ = mnslmap_loaded_ = false;
    stock_table_ = {};
    csp_table_ = {};
    css_joint_imgs_.clear();
    stage_stacked_odd_ = {};
    stage_stacked_even_ = {};
    stage_large_ = {};
    stage_special_ = {};
  }

  // Returns an ImTextureData* for a fighter's costume-matched 24x24 stock icon (IfAll.usd).
  ImTextureData* stock_icon(int ckind, int costume) {
    if (iso_path_.empty() || ckind < 0 || ckind >= 26) return nullptr;
    const int c = std::clamp(costume, 0, 5);
    const int key = ckind * 8 + c;
    auto& entry = stock_textures_[key];
    if (entry.attempted) return entry.tex.get();
    entry.attempted = true;
    ensure_ifall();
    if (!stock_table_.valid()) return nullptr;
    // Exact gm_80168B34 formula: Zelda=0x12, Sheik=0x19, ckind > 19 -> ckind - 1; stride = 30.
    int base = ckind;
    if (ckind == 18) base = 0x12;
    else if (ckind == 19) base = 0x19;
    else if (ckind > 19) base = ckind - 1;
    DecodedImage img;
    if (!decode_from_texanim(ifall_view_, stock_table_, base + c * 30, &img)) {
      if (c == 0 || !decode_from_texanim(ifall_view_, stock_table_, base, &img)) return nullptr;
    }
    entry.w = img.w;
    entry.h = img.h;
    entry.tex = upload_rgba_texture(img);
    return entry.tex.get();
  }

  // Returns an ImTextureData* for a fighter's 64x56 CSS square head icon (MnSlChr.usd menu.joint).
  ImTextureData* css_icon(int ckind) {
    if (iso_path_.empty() || ckind < 0 || ckind >= 26) return nullptr;
    auto& entry = css_icon_textures_[ckind];
    if (entry.attempted) return entry.tex.get();
    entry.attempted = true;
    ensure_mnslchr();
    // Exact CSSIconJointId table from mn/types.h (DFS index in MnSelectChrDataTable.models.menu.joint)
    static constexpr int kCkindToJoint[26] = {
      0x16, 0x15, 0x17, 0x21, 0x1A, 0x12, 0x1D, 0x11, 0x10, 0x22,
      0x20, 0x18, 0x13, 0x1E, 0x19, 0x1F, 0x1B, 0x14, 0x1C, 0x1C,
      0x08, 0x0A, 0x04, 0x0E, 0x0C, 0x06
    };
    auto it = css_joint_imgs_.find(kCkindToJoint[ckind]);
    if (it == css_joint_imgs_.end()) return nullptr;
    DecodedImage img;
    if (!decode_hsd_image(mnslchr_view_, it->second.first, it->second.second, &img)) return nullptr;
    entry.w = img.w;
    entry.h = img.h;
    entry.tex = upload_rgba_texture(img);
    return entry.tex.get();
  }

  // Returns an ImTextureData* for a fighter's full 136x188 Character Select Portrait (MnSlChr.usd).
  ImTextureData* csp_portrait(int ckind, int costume) {
    if (iso_path_.empty() || ckind < 0 || ckind >= 26) return nullptr;
    const int c = std::clamp(costume, 0, 5);
    const int key = ckind * 8 + c;
    auto& entry = csp_textures_[key];
    if (entry.attempted) return entry.tex.get();
    entry.attempted = true;
    ensure_mnslchr();
    if (!csp_table_.valid()) return nullptr;
    // Exact ICONHUD_* mapping from mn/forward.h + mnCharSel_8025DB34 (frame = hud_idx + color * 30).
    int hud_idx = ckind;
    if (ckind == 19) hud_idx = 0x12;       // Sheik shares Zelda's selectable slot on CSS
    else if (ckind > 19) hud_idx = ckind - 1;
    DecodedImage img;
    if (!decode_from_texanim(mnslchr_view_, csp_table_, hud_idx + c * 30, &img)) {
      if (c == 0 || !decode_from_texanim(mnslchr_view_, csp_table_, hud_idx, &img)) return nullptr;
    }
    entry.w = img.w;
    entry.h = img.h;
    entry.tex = upload_rgba_texture(img);
    return entry.tex.get();
  }

  // Returns an ImTextureData* for a full-color 64x56 stage thumbnail (MnSlMap.usd).
  ImTextureData* stage_preview(int stage_icon_idx) {
    if (iso_path_.empty() || stage_icon_idx < 0 || stage_icon_idx >= 29) return nullptr;
    auto& entry = stage_textures_[stage_icon_idx];
    if (entry.attempted) return entry.tex.get();
    entry.attempted = true;
    ensure_mnslmap();
    // Exact x9 values from mnStageSel_803F06D0[0..28] in mnstagesel.c
    static constexpr uint8_t kStageX9[29] = {
      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x08, 0x09, 0x0A, 0x0B,
      0x0C, 0x0D, 0x06, 0x07, 0x12, 0x13, 0x10, 0x11, 0x0E, 0x0F,
      0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C
    };
    const int x9 = (int)kStageX9[stage_icon_idx];
    const TexAnimTable* tbl = nullptr;
    int frame = 0;
    if (stage_icon_idx <= 21) {
      // Paired stacked stage icons: even = child->next, odd = child; frame = x9 / 2 + 2
      tbl = (stage_icon_idx & 1) ? &stage_stacked_odd_ : &stage_stacked_even_;
      frame = x9 / 2 + 2;
    } else if (stage_icon_idx <= 23) {
      // Large icons (Flat Zone / etc.): frame = x9 - 0x14
      tbl = &stage_large_;
      frame = x9 - 0x14;
    } else {
      // Special stages (Battlefield, Final Destination, Dream Land 64, YI64, KJ64): frame = x9 - 0x16
      tbl = &stage_special_;
      frame = x9 - 0x16;
    }
    if (!tbl || !tbl->valid()) return nullptr;
    DecodedImage img;
    if (!decode_from_texanim(mnslmap_view_, *tbl, frame, &img)) return nullptr;
    entry.w = img.w;
    entry.h = img.h;
    entry.tex = upload_rgba_texture(img);
    return entry.tex.get();
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
    std::vector<TexAnimTable> tables;
    std::unordered_set<uint32_t> seen;
    collect_texanims(ifall_view_, maj, 0, tables, seen);
    for (auto& t : tables) {
      if (t.n_images >= 100) {
        stock_table_ = std::move(t);
        break;
      }
    }
  }

  void collect_css_menu_joints(uint32_t jobj, int& dfs_idx, std::unordered_set<uint32_t>& seen) {
    if (!jobj || !mnslchr_view_.ok(jobj, 0x40) || !seen.insert(jobj).second) return;
    const int my_idx = dfs_idx++;
    uint32_t dobj = mnslchr_view_.u32(jobj + 0x10);
    int dg = 0;
    while (dobj && mnslchr_view_.ok(dobj, 0x10) && dg++ < 32) {
      const uint32_t mobj = mnslchr_view_.u32(dobj + 0x08);
      if (mobj && mnslchr_view_.ok(mobj, 0x14)) {
        uint32_t tobj = mnslchr_view_.u32(mobj + 0x08);
        int tg = 0;
        while (tobj && mnslchr_view_.ok(tobj, 0x54) && tg++ < 16) {
          const uint32_t img = mnslchr_view_.u32(tobj + 0x4C);
          const uint32_t tlut = mnslchr_view_.u32(tobj + 0x50);
          if (img && mnslchr_view_.ok(img, 0x18) &&
              mnslchr_view_.u16(img + 4) == 64 && mnslchr_view_.u16(img + 6) == 56 &&
              mnslchr_view_.u32(img + 8) == 9u) {
            css_joint_imgs_[my_idx] = {img, tlut};
          }
          tobj = mnslchr_view_.u32(tobj + 0x04);
        }
      }
      dobj = mnslchr_view_.u32(dobj + 0x04);
    }
    collect_css_menu_joints(mnslchr_view_.u32(jobj + 0x08), dfs_idx, seen);
    collect_css_menu_joints(mnslchr_view_.u32(jobj + 0x0C), dfs_idx, seen);
  }

  void ensure_mnslchr() {
    if (mnslchr_loaded_) return;
    mnslchr_loaded_ = true;
    mnslchr_bytes_ = read_disc_file_from_iso(iso_path_, "MnSlChr.usd");
    uint32_t root = 0;
    if (!find_dat_root(mnslchr_bytes_, "MnSelectChrDataTable", &mnslchr_view_, &root)) return;

    // 1. Collect all 25 square 64x56 CI8 CSS icons from models.menu.joint (root + 0x40)
    if (mnslchr_view_.ok(root + 0x40, 0x10)) {
      const uint32_t menu_jobj = mnslchr_view_.u32(root + 0x40);
      int dfs_idx = 0;
      std::unordered_set<uint32_t> jseen;
      collect_css_menu_joints(menu_jobj, dfs_idx, jseen);
    }

    // 2. Collect the 118-entry 136x188 CI8 CSP portrait TexAnim from models.door (root + 0x90)
    for (uint32_t off : {0x90u, 0x40u, 0x70u}) {
      if (!mnslchr_view_.ok(root + off, 0x10)) continue;
      std::vector<TexAnimTable> tables;
      std::unordered_set<uint32_t> seen;
      collect_texanims(mnslchr_view_, mnslchr_view_.u32(root + off + 8), 0, tables, seen);
      for (auto& t : tables) {
        if (t.n_images >= 100 && t.n_tluts >= 100) {
          // Verify first image is a 136x188 CSP (not 24x24 stock icons)
          const uint32_t img0 = mnslchr_view_.u32(t.imagetbl_off);
          if (img0 && mnslchr_view_.ok(img0, 0x18) && mnslchr_view_.u16(img0 + 4) >= 100) {
            csp_table_ = std::move(t);
            return;
          }
        }
      }
    }
  }

  void ensure_mnslmap() {
    if (mnslmap_loaded_) return;
    mnslmap_loaded_ = true;
    mnslmap_bytes_ = read_disc_file_from_iso(iso_path_, "MnSlMap.usd");
    uint32_t root = 0;
    if (!find_dat_root(mnslmap_bytes_, "MnSelectStageDataTable", &mnslmap_view_, &root)) return;

    auto first_ta_from_maj = [&](uint32_t maj, TexAnimTable* dst) {
      std::vector<TexAnimTable> tables;
      std::unordered_set<uint32_t> seen;
      collect_texanims(mnslmap_view_, maj, 0, tables, seen);
      if (!tables.empty()) *dst = std::move(tables[0]);
    };

    // icon_large (+0x10): 4 images
    if (mnslmap_view_.ok(root + 0x10, 0x10)) {
      first_ta_from_maj(mnslmap_view_.u32(root + 0x10 + 8), &stage_large_);
    }
    // icon_special (+0x30): 7 images (Battlefield, Final Destination, Dream Land 64, YI64, KJ64)
    if (mnslmap_view_.ok(root + 0x30, 0x10)) {
      first_ta_from_maj(mnslmap_view_.u32(root + 0x30 + 8), &stage_special_);
    }
    // icon_stacked (+0x50): child = odd (2*i+1), child->next = even (2*i)
    if (mnslmap_view_.ok(root + 0x50, 0x10)) {
      const uint32_t maj = mnslmap_view_.u32(root + 0x50 + 8);
      if (maj && mnslmap_view_.ok(maj, 0x10)) {
        const uint32_t child = mnslmap_view_.u32(maj + 0);
        const uint32_t child_next = (child && mnslmap_view_.ok(child, 0x10))
                                        ? mnslmap_view_.u32(child + 4)
                                        : 0;
        auto pick_stacked = [&](uint32_t node, TexAnimTable* dst) {
          if (!node || !mnslmap_view_.ok(node, 0x10)) return;
          uint32_t ma = mnslmap_view_.u32(node + 8);
          int mg = 0;
          while (ma && mnslmap_view_.ok(ma, 0x10) && mg++ < 64) {
            uint32_t ta = mnslmap_view_.u32(ma + 8);
            int tg = 0;
            while (ta && mnslmap_view_.ok(ta, 0x18) && tg++ < 32) {
              TexAnimTable cand;
              if (parse_texanim_node(mnslmap_view_, ta, &cand) && cand.n_images >= 10) {
                *dst = std::move(cand);
                return;
              }
              ta = mnslmap_view_.u32(ta + 0);
            }
            ma = mnslmap_view_.u32(ma + 0);
          }
        };
        pick_stacked(child, &stage_stacked_odd_);
        pick_stacked(child_next, &stage_stacked_even_);
      }
    }
  }

  std::string iso_path_;
  bool ifall_loaded_ = false, mnslchr_loaded_ = false, mnslmap_loaded_ = false;
  std::vector<uint8_t> ifall_bytes_, mnslchr_bytes_, mnslmap_bytes_;
  DatView ifall_view_, mnslchr_view_, mnslmap_view_;
  TexAnimTable stock_table_, csp_table_;
  std::unordered_map<int, std::pair<uint32_t, uint32_t>> css_joint_imgs_;
  TexAnimTable stage_stacked_odd_, stage_stacked_even_, stage_large_, stage_special_;
  std::unordered_map<int, CachedTex> stock_textures_, css_icon_textures_, csp_textures_, stage_textures_;
};

// Draws a sub-region [src_u0..src_u1, src_v0..src_v1] of a texture into [a, b] with cover crop.
inline void draw_image_cover_sub(ImDrawList* dl, ImTextureData* tex, ImVec2 a, ImVec2 b,
                                 ImU32 tint, float src_v0, float src_v1, float anchor_y = 0.5f) {
  if (!tex || tex->Width <= 0 || tex->Height <= 0) return;
  const float rw = b.x - a.x, rh = b.y - a.y;
  if (rw <= 1.0f || rh <= 1.0f) return;
  const float sub_h = (float)tex->Height * std::max(0.1f, src_v1 - src_v0);
  const float ta = (float)tex->Width / sub_h;
  const float ra = rw / rh;
  ImVec2 uv0(0.0f, src_v0), uv1(1.0f, src_v1);
  if (ta > ra) {
    const float keep = ra / ta;
    const float pad = (1.0f - keep) * 0.5f;
    uv0.x = pad;
    uv1.x = 1.0f - pad;
  } else if (ta < ra) {
    const float keep = (ta / ra) * (src_v1 - src_v0);
    const float excess = (src_v1 - src_v0) - keep;
    uv0.y = src_v0 + excess * anchor_y;
    uv1.y = uv0.y + keep;
  }
  dl->AddImage(tex->GetTexRef(), a, b, uv0, uv1, tint);
}

inline void draw_image_cover(ImDrawList* dl, ImTextureData* tex, ImVec2 a, ImVec2 b,
                             ImU32 tint, float anchor_y = 0.5f) {
  draw_image_cover_sub(dl, tex, a, b, tint, 0.0f, 1.0f, anchor_y);
}

// Draws a texture fitted inside [a, b] preserving exact aspect ratio (ideal for transparent CSP cutouts).
inline void draw_image_contain(ImDrawList* dl, ImTextureData* tex, ImVec2 a, ImVec2 b,
                               ImU32 tint, float anchor_y = 0.35f) {
  if (!tex || tex->Width <= 0 || tex->Height <= 0) return;
  const float rw = b.x - a.x, rh = b.y - a.y;
  if (rw <= 1.0f || rh <= 1.0f) return;
  const float ta = (float)tex->Width / (float)tex->Height;
  const float ra = rw / rh;
  float dw = rw, dh = rh;
  if (ta > ra) dh = rw / ta;
  else dw = rh * ta;
  const float ox = a.x + (rw - dw) * 0.5f;
  const float oy = a.y + (rh - dh) * anchor_y;
  dl->AddImage(tex->GetTexRef(), ImVec2(ox, oy), ImVec2(ox + dw, oy + dh),
               ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), tint);
}

// Crisp fighter portrait card (real ISO texture or procedural vector emblem fallback).
inline void draw_fighter_portrait_card(ImDrawList* dl, ImFont* font, ImVec2 a, ImVec2 b,
                                       int ckind, int costume, ImTextureData* tex,
                                       float alpha = 1.0f, bool hero_mode = false) {
  const auto& fi = theme::fighter_info(ckind);
  const float shift = (float)(costume % 6) * 0.08f;
  const int r = std::clamp((int)(fi.r * (0.95f - shift * 0.3f)), 24, 240);
  const int g = std::clamp((int)(fi.g * (0.95f + shift * 0.2f)), 24, 240);
  const int bl = std::clamp((int)(fi.b * (0.95f + shift * 0.1f)), 24, 240);

  if (tex) {
    if (hero_mode) {
      // Subtle character-tinted atmospheric glow behind transparent 136x188 CSP cutout
      const ImU32 top_col = theme::rgba(r / 2 + 10, g / 2 + 12, bl / 2 + 18, 0.36f * alpha);
      const ImU32 bot_col = theme::rgba(8, 12, 20, 0.0f);
      dl->AddRectFilledMultiColor(a, b, top_col, top_col, bot_col, bot_col);
      draw_image_contain(dl, tex, ImVec2(a.x + 6.0f, a.y + 4.0f), ImVec2(b.x - 6.0f, b.y - 2.0f),
                         IM_COL32(255, 255, 255, (int)(255 * alpha)), 0.30f);
    } else {
      // Compact 64x56 CSS head icon: trim bottom baked retro name bar (v > 0.74) for clean portrait crop
      draw_image_cover_sub(dl, tex, a, b, IM_COL32(255, 255, 255, (int)(255 * alpha)), 0.02f, 0.74f, 0.35f);
    }
    return;
  }

  // Procedural dark-tinted portrait tile with signature character hue & subtle costume accent
  const ImU32 top_col = theme::rgba(r / 2 + 12, g / 2 + 14, bl / 2 + 20, (hero_mode ? 0.55f : 0.92f) * alpha);
  const ImU32 bot_col = theme::rgba(10, 14, 22, (hero_mode ? 0.0f : 0.96f) * alpha);
  dl->AddRectFilledMultiColor(a, b, top_col, top_col, bot_col, bot_col);

  const float w = b.x - a.x, h = b.y - a.y;
  const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
  if (hero_mode) {
    const float rad = std::min(w, h) * 0.36f;
    dl->AddCircleFilled(ImVec2(c.x, c.y - h * 0.06f), rad * 1.15f,
                        theme::rgba(r, g, bl, 0.14f * alpha), 48);
    dl->AddCircle(ImVec2(c.x, c.y - h * 0.06f), rad,
                  theme::rgba(r, g, bl, 0.34f * alpha), 48, 2.0f);
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
    dl->AddTriangleFilled(ImVec2(a.x, b.y), ImVec2(b.x, a.y + h * 0.35f), b,
                          theme::rgba(r, g, bl, 0.28f * alpha));
    const float fsz = std::clamp(h * 0.44f, 11.0f, 34.0f);
    const ImVec2 ts = theme::pick_font(font)->CalcTextSizeA(fsz, FLT_MAX, 0.0f, fi.abbr);
    theme::draw_text_tracked(dl, font, fsz, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f),
                             theme::white(0.95f * alpha), fi.abbr, 0.0f, true);
  }
}

// Crisp stock icon (costume-matched ISO 24x24 texture or glowing Smash pip fallback).
inline void draw_stock_pip(ImDrawList* dl, ImVec2 center, float radius, int ckind, int costume,
                           ImU32 port_col, ImTextureData* tex, float alpha = 1.0f) {
  (void)ckind; (void)costume;
  if (tex) {
    const float r = radius * 1.18f;
    const ImVec2 a(center.x - r, center.y - r), b(center.x + r, center.y + r);
    dl->AddImage(tex->GetTexRef(), a, b, ImVec2(0, 0), ImVec2(1, 1),
                 IM_COL32(255, 255, 255, (int)(255 * alpha)));
    return;
  }
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

// Crisp stage preview card (aspect-preserving ISO 64x56 thumbnail or stylized platform fallback).
inline void draw_stage_card_art(ImDrawList* dl, ImVec2 a, ImVec2 b, int stage_icon_idx,
                                ImTextureData* tex, float alpha = 1.0f, bool full_bg = false) {
  const auto& st = theme::stage_info_by_icon(stage_icon_idx);
  if (tex) {
    draw_image_cover(dl, tex, a, b,
                     IM_COL32(255, 255, 255, (int)(255 * (full_bg ? 0.34f : alpha))), 0.5f);
    return;
  }
  const ImU32 c1 = theme::rgba(st.r1, st.g1, st.b1, (full_bg ? 0.38f : 0.92f) * alpha);
  const ImU32 c2 = theme::rgba(st.r2, st.g2, st.b2, (full_bg ? 0.65f : 0.96f) * alpha);
  dl->AddRectFilledMultiColor(a, b, c1, c1, c2, c2);

  if (stage_icon_idx == 29) return;
  const float w = b.x - a.x, h = b.y - a.y;
  const float cx = (a.x + b.x) * 0.5f;
  const float floor_y = a.y + h * 0.68f;
  const float main_w = w * 0.56f;
  const float deck_h = std::max(2.0f, h * 0.08f);
  const ImU32 plat = theme::rgba(235, 244, 255, (full_bg ? 0.22f : 0.78f) * alpha);
  const ImU32 base = theme::rgba(14, 20, 34, (full_bg ? 0.30f : 0.62f) * alpha);
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
