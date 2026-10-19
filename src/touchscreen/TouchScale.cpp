#if BLUESQUID_TOUCHSCREEN_10IN
#include "TouchScale.h"

#include <math.h>

namespace TouchScale {
namespace {

// The icon fonts hold at most a few glyphs; their base descriptors are kept
// so that the bitmap callback can fetch the source glyph.
constexpr size_t kMaximumFonts = 4;
constexpr size_t kMaximumGlyphs = 8;

struct ScaledFont {
  const lv_font_t* base = nullptr;
  lv_font_t font{};
  lv_font_glyph_dsc_t glyphs[kMaximumGlyphs]{};
};
ScaledFont fonts[kMaximumFonts];

ScaledFont* entryFor(const lv_font_t* font) {
  for (ScaledFont& entry : fonts)
    if (&entry.font == font) return &entry;
  return nullptr;
}

bool glyphDsc(const lv_font_t* font, lv_font_glyph_dsc_t* dsc, uint32_t letter,
              uint32_t next) {
  ScaledFont* scaled = entryFor(font);
  lv_font_glyph_dsc_t base{};
  if (scaled == nullptr) return false;
  if (!scaled->base->get_glyph_dsc(scaled->base, &base, letter, next)) return false;
  if (base.gid.index >= kMaximumGlyphs) return false;
  scaled->glyphs[base.gid.index] = base;
  *dsc = base;
  dsc->adv_w = up(base.adv_w);
  dsc->box_w = up(base.box_w);
  dsc->box_h = up(base.box_h);
  dsc->ofs_x = up(base.ofs_x);
  dsc->ofs_y = up(base.ofs_y);
  dsc->stride = dsc->box_w;
  dsc->format = LV_FONT_GLYPH_FORMAT_A8;
  return true;
}

// 4-bit alpha at (x, y) of a source glyph, or 0 outside it.
float sourceAlpha(const uint8_t* source, const lv_font_glyph_dsc_t& base, int x, int y) {
  if (x < 0 || y < 0 || x >= base.box_w || y >= base.box_h) return 0.0F;
  const uint8_t packed = source[y * base.stride + x / 2];
  return float((x & 1) ? (packed & 0x0F) : (packed >> 4));
}

const void* glyphBitmap(lv_font_glyph_dsc_t* dsc, lv_draw_buf_t* drawBuffer) {
  const ScaledFont* scaled = entryFor(dsc->resolved_font);
  if (scaled == nullptr || dsc->req_raw_bitmap || drawBuffer == nullptr ||
      dsc->gid.index >= kMaximumGlyphs)
    return nullptr;
  lv_font_glyph_dsc_t base = scaled->glyphs[dsc->gid.index];
  base.req_raw_bitmap = 1;
  const auto* source =
      static_cast<const uint8_t*>(scaled->base->get_glyph_bitmap(&base, nullptr));
  if (source == nullptr || base.format != LV_FONT_GLYPH_FORMAT_A4) return nullptr;
  // Bilinear resampling of the source glyph.
  const float step = float(kDenominator) / kNumerator;
  for (int y = 0; y < dsc->box_h; ++y) {
    uint8_t* output = drawBuffer->data + y * drawBuffer->header.stride;
    const float sy = (y + 0.5F) * step - 0.5F;
    const int y0 = int(floorf(sy));
    const float fy = sy - y0;
    for (int x = 0; x < dsc->box_w; ++x) {
      const float sx = (x + 0.5F) * step - 0.5F;
      const int x0 = int(floorf(sx));
      const float fx = sx - x0;
      const float top = sourceAlpha(source, base, x0, y0) * (1 - fx) +
                        sourceAlpha(source, base, x0 + 1, y0) * fx;
      const float bottom = sourceAlpha(source, base, x0, y0 + 1) * (1 - fx) +
                           sourceAlpha(source, base, x0 + 1, y0 + 1) * fx;
      output[x] = uint8_t(lroundf((top * (1 - fy) + bottom * fy) * 17.0F));
    }
  }
  lv_draw_buf_flush_cache(drawBuffer, nullptr);
  return drawBuffer;
}

}  // namespace

const lv_font_t* scaledFont(const lv_font_t* base) {
  for (ScaledFont& entry : fonts) {
    if (entry.base == base) return &entry.font;
    if (entry.base != nullptr) continue;
    entry.base = base;
    entry.font = *base;
    entry.font.get_glyph_dsc = glyphDsc;
    entry.font.get_glyph_bitmap = glyphBitmap;
    entry.font.line_height = up(base->line_height);
    entry.font.base_line = up(base->base_line);
    entry.font.underline_position = up(base->underline_position);
    entry.font.static_bitmap = 0;
    entry.font.fallback = nullptr;
    return &entry.font;
  }
  return base;  // more fonts than expected: unscaled but usable
}

}  // namespace TouchScale
#endif
