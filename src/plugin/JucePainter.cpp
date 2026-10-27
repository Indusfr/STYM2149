// STYM2149 — plugin: GEM drawing with juce::Graphics at the screen's resolution (D-201)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "JucePainter.h"

#include <algorithm>
#include <cmath>

#include "gemui/Font.h"

namespace stym::plugin {

using gemui::Color;
using gemui::Rect;

namespace {

// gemui colours are 0xAARRGGBB like juce::Colour; the alpha is forced to FF (tileCell skips the transparent pattern
// pixels before calling it).
juce::Colour colour(Color c) noexcept { return juce::Colour(juce::uint32(c | 0xFF000000u)); }

// Mask of the contours of pixel squares (one rectangle per run of lit pixels), filled with anti-aliasing: the
// squares fall on whole pixels at a whole scale, so the mask is then fully opaque or empty. With `snap` (sharp pixels,
// D-331) each edge is rounded to a whole pixel, at least one pixel apart: the mask is opaque or empty at any scale.
struct Squares {
    juce::Path path;
    bool snap = false;
    void run(double x, double y, double w, double h) {
        if (snap) {
            const double x0 = std::round(x), y0 = std::round(y);
            const double x1 = std::max(x0 + 1, std::round(x + w)), y1 = std::max(y0 + 1, std::round(y + h));
            path.addRectangle(float(x0), float(y0), float(x1 - x0), float(y1 - y0));
            return;
        }
        path.addRectangle(float(x), float(y), float(w), float(h));
    }
};

}  // namespace

void JucePainter::Cache::setScale(double d) {
    if (d == d_) return;
    d_ = d;
    glyphs_.clear();
    bitmaps_.clear();
    tiles_.clear();
}

void JucePainter::Cache::setSharp(bool sharp) {
    if (sharp == sharp_) return;
    sharp_ = sharp;
    glyphs_.clear();
    bitmaps_.clear();
}

JucePainter::JucePainter(juce::Image& image, const Rect& area, Cache& cache, const juce::Rectangle<int>& limit)
    : Painter(area), image_(image), g_(image), cache_(cache), limit_(limit.getIntersection(image.getBounds())),
      d_(cache.scale()), whole_(std::floor(d_) == d_ && d_ >= 1 ? int(d_) : 0), sharp_(cache.sharp() && !whole_) {
    g_.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
}

juce::Rectangle<int> JucePainter::onScreen(const Rect& r, double d) noexcept {
    if (r.empty()) return {};
    const auto at = [d](int v) { return int(std::floor(v * d + 0.5)); };
    const int x = at(r.x), y = at(r.y);
    return {x, y, std::max(1, at(r.right()) - x), std::max(1, at(r.bottom()) - y)};
}

int JucePainter::at(double v) const noexcept { return int(std::floor(v * d_ + 0.5)); }

juce::Rectangle<int> JucePainter::screenClip() const noexcept {
    const Rect& c = clip();
    if (c.empty()) return {};
    return juce::Rectangle<int>::leftTopRightBottom(at(c.x), at(c.y), at(c.right()), at(c.bottom()))
        .getIntersection(limit_);
}

void JucePainter::fill(const Rect& r, Color c) noexcept {
    const juce::Rectangle<int> s = onScreen(r.intersect(clip()), d_).getIntersection(screenClip());
    if (s.isEmpty()) return;
    g_.setColour(colour(c));
    g_.fillRect(s);
}

void JucePainter::frame(const Rect& r, int t, Color c) noexcept {
    if (r.empty() || t <= 0) return;
    const juce::Rectangle<int> o = onScreen(r, d_), clip = screenClip();
    // Border of k screen pixels, t × d rounded down; 1e-9 keeps a product meant to be whole (floating-point error)
    // from dropping one pixel. Top and bottom span the width, left and right fill the height between them.
    const int k = std::max(1, int(std::floor(t * d_ + 1e-9)));
    const int kx = std::min(k, o.getWidth()), ky = std::min(k, o.getHeight());
    g_.setColour(colour(c));
    const juce::Rectangle<int> sides[] = {{o.getX(), o.getY(), o.getWidth(), ky},
                                          {o.getX(), o.getBottom() - ky, o.getWidth(), ky},
                                          {o.getX(), o.getY() + ky, kx, o.getHeight() - 2 * ky},
                                          {o.getRight() - kx, o.getY() + ky, kx, o.getHeight() - 2 * ky}};
    for (const auto& s : sides)
        if (const auto v = s.getIntersection(clip); !v.isEmpty()) g_.fillRect(v);
}

void JucePainter::line(gemui::Point a, gemui::Point b, Color c, int thickness) noexcept {
    if (a.x == b.x || a.y == b.y) {  // pixels of the framebuffer
        Painter::line(a, b, c, thickness);
        return;
    }
    const juce::Rectangle<int> clip = screenClip();
    if (clip.isEmpty()) return;
    // From the centre of the first end pixel to the centre of the last, `thickness` desktop pixels wide (D-214).
    const float k = float(d_), w = float(std::max(1.0, std::max(1, thickness) * d_));
    juce::Path path;
    path.startNewSubPath((float(a.x) + 0.5f) * k, (float(a.y) + 0.5f) * k);
    path.lineTo((float(b.x) + 0.5f) * k, (float(b.y) + 0.5f) * k);
    juce::Graphics::ScopedSaveState state(g_);
    g_.reduceClipRegion(clip);
    g_.setColour(colour(c));
    g_.strokePath(path, juce::PathStrokeType(w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

const juce::Image& JucePainter::tileCell(const gemui::Tile& t) {
    uint64_t key = 1469598103934665603ull;  // FNV-1a of the cell (64-bit offset basis)
    const auto mixIn = [&key](uint64_t v) { key = (key ^ v) * 1099511628211ull; };  // one 64-bit word, FNV prime
    mixIn(uint64_t(t.w));
    mixIn(uint64_t(t.h));
    for (int i = 0; i < t.w * t.h; ++i) mixIn(t.px[size_t(i)]);
    auto [it, fresh] = cache_.tiles_.try_emplace(key);
    if (!fresh) return it->second;
    // At least one screen pixel per pixel of the pattern, so that a halftone stays visible below one pixel per
    // desktop pixel.
    const int w = std::max(t.w, int(std::lround(t.w * d_))), h = std::max(t.h, int(std::lround(t.h * d_)));
    juce::Image cell(juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
    {
        juce::Graphics g(cell);
        for (int y = 0; y < t.h; ++y)
            for (int x = 0; x < t.w; ++x) {
                const Color c = t.at(x, y);
                if (!gemui::opaque(c)) continue;
                // Pattern pixel x covers the cell columns round(x·w/t.w) to round((x+1)·w/t.w): whole screen pixels,
                // the widths differing by one at most. Same for the rows.
                const int x0 = int(std::lround(double(x) * w / t.w)), x1 = int(std::lround(double(x + 1) * w / t.w));
                const int y0 = int(std::lround(double(y) * h / t.h)), y1 = int(std::lround(double(y + 1) * h / t.h));
                g.setColour(colour(c));
                g.fillRect(x0, y0, x1 - x0, y1 - y0);
            }
    }
    it->second = cell;
    return it->second;
}

void JucePainter::tile(const Rect& r, const gemui::Tile& t, gemui::Point anchor) noexcept {
    const juce::Rectangle<int> s = onScreen(r.intersect(clip()), d_).getIntersection(screenClip());
    if (s.isEmpty()) return;
    // The cell repeated from the anchor's screen pixel; its transparent pixels let what is below show through.
    const juce::Image& cell = tileCell(t);
    const int w = cell.getWidth(), h = cell.getHeight();
    // Place in the cell of the area's first screen pixel: a redrawn part stays aligned with the rest of the pattern.
    const int x0 = ((s.getX() - at(anchor.x)) % w + w) % w, y0 = ((s.getY() - at(anchor.y)) % h + h) % h;
    const juce::Image::BitmapData src(cell, juce::Image::BitmapData::readOnly);
    const juce::Image::BitmapData bd(image_, s.getX(), s.getY(), s.getWidth(), s.getHeight(),
                                     juce::Image::BitmapData::readWrite);
    for (int k = 0, cy = y0; k < s.getHeight(); ++k, cy = cy + 1 == h ? 0 : cy + 1) {
        const auto* line = reinterpret_cast<const uint32_t*>(src.getLinePointer(cy));
        auto* row = reinterpret_cast<uint32_t*>(bd.getLinePointer(k));
        for (int i = 0, cx = x0; i < s.getWidth(); ++i, cx = cx + 1 == w ? 0 : cx + 1)
            if (gemui::opaque(line[cx])) row[i] = line[cx];
    }
}

void JucePainter::drawMask(const Cache::Mask& m, int x, int y, Color c) noexcept {
    if (!m.image.isValid()) return;
    const juce::Rectangle<int> dest(x + m.x, y + m.y, m.image.getWidth(), m.image.getHeight());
    const juce::Rectangle<int> s = dest.getIntersection(screenClip());
    if (s.isEmpty()) return;
    // Colour laid over the image with the coverage of the mask, rounded to nearest per channel.
    const juce::Image::BitmapData mask(m.image, s.getX() - dest.getX(), s.getY() - dest.getY(), s.getWidth(),
                                       s.getHeight());
    const juce::Image::BitmapData bd(image_, s.getX(), s.getY(), s.getWidth(), s.getHeight(),
                                     juce::Image::BitmapData::readWrite);
    const uint32_t r = gemui::red(c), gr = gemui::green(c), b = gemui::blue(c);
    for (int k = 0; k < s.getHeight(); ++k) {
        const uint8_t* a = mask.getLinePointer(k);
        auto* row = reinterpret_cast<uint32_t*>(bd.getLinePointer(k));
        for (int i = 0; i < s.getWidth(); ++i) {
            const uint32_t al = a[size_t(i) * size_t(mask.pixelStride)];
            if (al == 0) continue;
            if (al == 255) {
                row[i] = c | 0xFF000000u;
                continue;
            }
            const uint32_t d = row[i], na = 255 - al;
            const auto ch = [al, na](uint32_t src, uint32_t dst) { return (src * al + dst * na + 127) / 255; };
            row[i] = gemui::rgb(ch(r, gemui::red(d)), ch(gr, gemui::green(d)), ch(b, gemui::blue(d)));
        }
    }
}

namespace {

// The path filled with anti-aliasing into a single-channel image that just covers its bounds; the mask keeps where
// its top-left corner falls.
JucePainter::Cache::Mask maskOf(const juce::Path& path) {
    if (path.isEmpty()) return {};
    const juce::Rectangle<float> b = path.getBounds();
    const int x0 = int(std::floor(b.getX())), y0 = int(std::floor(b.getY()));
    const int x1 = int(std::ceil(b.getRight())), y1 = int(std::ceil(b.getBottom()));
    juce::Image image(juce::Image::SingleChannel, x1 - x0, y1 - y0, true, juce::SoftwareImageType());
    juce::Graphics g(image);
    g.setColour(juce::Colours::white);
    g.fillPath(path, juce::AffineTransform::translation(float(-x0), float(-y0)));
    return {image, x0, y0};
}

}  // namespace

const JucePainter::Cache::Mask& JucePainter::glyphMask(int index, int px, int quarter, gemui::Face face) {
    using gemui::Font;
    // A whole scale keeps the framebuffer's raster of each quarter; otherwise one mask per glyph, its origin on a
    // whole screen pixel.
    const int q = whole_ ? quarter : 0;
    // Key: glyph index in bits 0-15, size in px from bit 16, face from bit 24, quarter (0-3) from bit 32.
    const uint64_t key = uint64_t(index) | uint64_t(px) << 16 | uint64_t(face) << 24 | uint64_t(q) << 32;
    auto [it, fresh] = cache_.glyphs_.try_emplace(key);
    if (!fresh) return it->second;
    // Rows of the glyph and size of one of its pixels in desktop pixels: the raster itself at a whole scale or in the
    // 8×8 and 6×6 fonts (cells on the pixel grid); the 16 px squares of the 8×16 font scaled to the size otherwise.
    const bool squares16 = !whole_ && Font::cell(px, face) == 0;
    const Font::Raster g = squares16 ? Font::raster(16, index, 0) : Font::raster(px, index, q, face);
    const double unit = (squares16 ? px / 16.0 : 1.0) * d_;
    // Each raster row is 16 bits, bit 15 = leftmost column; each run of lit bits becomes one rectangle.
    Squares s;
    s.snap = sharp_;
    for (int r = 0; r < g.count; ++r) {
        const uint16_t bits = g.bits(r);
        for (int b = 0; b < 16;) {
            if (!(bits & (0x8000 >> b))) {
                ++b;
                continue;
            }
            const int b0 = b;
            while (b < 16 && (bits & (0x8000 >> b))) ++b;
            s.run((g.shift + b0) * unit, (g.top + r) * unit, (b - b0) * unit, unit);
        }
    }
    it->second = maskOf(s.path);
    return it->second;
}

void JucePainter::glyph(int index, int x, int baseline, int px, Color c, int quarter, gemui::Face face) noexcept {
    // The 8×8 and 6×6 fonts ignore the quarter (cells on the pixel grid), as in the framebuffer.
    const double origin = whole_ || gemui::Font::cell(px, face) ? x : x + quarter / 4.0;
    drawMask(glyphMask(index, px, quarter, face), at(origin), at(baseline), c);
}

namespace {

// Mask of a bitmap's outline at d screen pixels per pixel (D-404): the polygons filled with anti-aliasing, or with
// `sharp` the screen pixels whose centre lies inside, opaque.
JucePainter::Cache::Mask outlineMask(const gemui::Bitmap& b, double d, bool sharp) {
    const gemui::Outline& o = *b.outline;
    if (!sharp) {
        juce::Path path;
        size_t first = 0;
        for (const uint16_t end : o.ends) {
            for (size_t k = first; k < end; ++k) {
                const float x = float(o.xy[2 * k] * d), y = float(o.xy[2 * k + 1] * d);
                if (k == first)
                    path.startNewSubPath(x, y);
                else
                    path.lineTo(x, y);
            }
            path.closeSubPath();
            first = end;
        }
        return maskOf(path);
    }
    const int w = int(std::ceil(b.w * d)), h = int(std::ceil(b.h * d));
    juce::Image image(juce::Image::SingleChannel, w, h, true, juce::SoftwareImageType());
    {
        const juce::Image::BitmapData bd(image, juce::Image::BitmapData::writeOnly);
        for (int r = 0; r < h; ++r)
            for (int k = 0; k < w; ++k)
                if (o.inside((k + 0.5) / d, (r + 0.5) / d)) *bd.getPixelPointer(k, r) = 255;
    }
    return {image, 0, 0};
}

}  // namespace

void JucePainter::bitmap(const gemui::Bitmap& b, int x, int y, Color c) noexcept {
    // Cached by the address of its bits: the icons are static data. One rectangle per run of lit pixels of a row; a
    // bitmap with an outline (the Atari logo, D-404) is its outline filled at any d but 1, whole or not, where the
    // squares would show steps; without anti-aliasing in sharp pixels at any d.
    auto [it, fresh] = cache_.bitmaps_.try_emplace(b.bits);
    if (fresh && b.outline && d_ != 1) {
        it->second = outlineMask(b, d_, cache_.sharp());
    } else if (fresh) {
        Squares s;
        s.snap = sharp_;
        for (int r = 0; r < b.h; ++r)
            for (int k = 0; k < b.w;) {
                if (!b.at(k, r)) {
                    ++k;
                    continue;
                }
                const int k0 = k;
                while (k < b.w && b.at(k, r)) ++k;
                s.run(k0 * d_, r * d_, (k - k0) * d_, d_);
            }
        it->second = maskOf(s.path);
    }
    drawMask(it->second, at(x), at(y), c);
}

void JucePainter::image(const Rect& r, const Color* px, int w, int h) noexcept {
    if (r.empty() || w <= 0 || h <= 0) return;
    const juce::Rectangle<int> s = onScreen(r.intersect(clip()), d_).getIntersection(screenClip());
    if (s.isEmpty()) return;
    // Pixel of the image shown at screen pixel v: at a whole scale, the one of its desktop pixel (the framebuffer
    // enlarged); otherwise the one under the centre of the screen pixel.
    const auto source = [this](int v, int origin, int span, int size) {
        const double pos = whole_ ? std::floor(double(v) / whole_) + 0.5 : (v + 0.5) / d_;
        return std::clamp(int(std::floor((pos - origin) * size / span)), 0, size - 1);
    };
    std::vector<int> cols(size_t(s.getWidth()));
    for (int x = 0; x < s.getWidth(); ++x) cols[size_t(x)] = source(s.getX() + x, r.x, r.w, w);
    const juce::Image::BitmapData bd(image_, s.getX(), s.getY(), s.getWidth(), s.getHeight(),
                                     juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < s.getHeight(); ++y) {
        const Color* src = px + size_t(source(s.getY() + y, r.y, r.h, h)) * size_t(w);
        auto* dst = reinterpret_cast<uint32_t*>(bd.getLinePointer(y));
        for (int x = 0; x < s.getWidth(); ++x) dst[x] = src[cols[size_t(x)]] | 0xFF000000u;
    }
}

// Colour c laid over the area at opacity alpha (gemui::mix, as in the framebuffer).
void JucePainter::shade(const Rect& r, Color c, double alpha) noexcept {
    const juce::Rectangle<int> s = onScreen(r.intersect(clip()), d_).getIntersection(screenClip());
    if (s.isEmpty()) return;
    const juce::Image::BitmapData bd(image_, s.getX(), s.getY(), s.getWidth(), s.getHeight(),
                                     juce::Image::BitmapData::readWrite);
    for (int y = 0; y < s.getHeight(); ++y) {
        auto* row = reinterpret_cast<uint32_t*>(bd.getLinePointer(y));
        for (int x = 0; x < s.getWidth(); ++x) row[x] = gemui::mix(c, row[x], alpha);
    }
}

// Opacity group as in the framebuffer: the screen pixels changed between begin and end are blended with alpha over
// those that were below.
void JucePainter::beginOpacity(const Rect& r, double alpha) {
    Group g;
    g.r = onScreen(r.intersect(clip()), d_).getIntersection(screenClip());
    g.alpha = alpha;
    g.under.resize(size_t(g.r.getWidth()) * size_t(g.r.getHeight()));
    if (!g.r.isEmpty()) {
        const juce::Image::BitmapData bd(image_, g.r.getX(), g.r.getY(), g.r.getWidth(), g.r.getHeight(),
                                         juce::Image::BitmapData::readOnly);
        for (int y = 0; y < g.r.getHeight(); ++y)
            std::copy_n(reinterpret_cast<const uint32_t*>(bd.getLinePointer(y)), g.r.getWidth(),
                        g.under.data() + size_t(y) * size_t(g.r.getWidth()));
    }
    groups_.push_back(std::move(g));
}

void JucePainter::endOpacity() {
    if (groups_.empty()) return;
    const Group g = std::move(groups_.back());
    groups_.pop_back();
    if (g.r.isEmpty()) return;
    const juce::Image::BitmapData bd(image_, g.r.getX(), g.r.getY(), g.r.getWidth(), g.r.getHeight(),
                                     juce::Image::BitmapData::readWrite);
    for (int y = 0; y < g.r.getHeight(); ++y) {
        auto* p = reinterpret_cast<uint32_t*>(bd.getLinePointer(y));
        const Color* u = g.under.data() + size_t(y) * size_t(g.r.getWidth());
        for (int x = 0; x < g.r.getWidth(); ++x)
            if (p[x] != u[x]) p[x] = gemui::mix(p[x], u[x], g.alpha);
    }
}

}  // namespace stym::plugin
