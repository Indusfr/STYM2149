// STYM2149 — pluginui: the demo of the Information box, after the title screen of ST BLIX
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "pluginui/InfoDemo.h"

#include <algorithm>
#include <array>
#include <string_view>

#include "pluginui/DemoData.h"

namespace stym::pluginui {

namespace {

using gemui::Color;

// Indus's text (29/10/2026 04:03; SECTOR ONE and EQUINOX on 29/10/2026 10:54; the features of the plugin
// brought up to date on 01/11/2026), two typos corrected (EXPECIALLY, PRSIM) and the curly quotes drawn with
// the font's (notes/demo-scroll.txt in the project's files).
constexpr std::string_view kText =
    "YO! INDUS ...EX MEGADETH... OF PRISM PROUDLY PRESENTS... STYM2149 !!! THE YM2149 SOUNDCHIP REBORN AS A "
    "PLUGIN! VST3 AU CLAP LV2 AND STANDALONE... 3, 2X3, 16 OR 32 CHANNELS. BUZZER. SYNC BUZZER. SID VOICES. FM "
    "VOICES IN TTRAK RATIOS OR MAXYMISER SEMITONES. CHIPDRUMS. DIGIDRUMS AND STE DMA. ALL CYCLE EXACT AS THE GOOD "
    "OLD ST INTENDED. STEP SEQUENCER. ECHO AND DELAY. PLAYS WITH MIDI CONTROLLERS, AFTERTOUCH AND MPE. GEM DESKTOP "
    "IN SIX LANGUAGES. LOADS AND SAVES MAXYMISER, TTRAK, YM TRACKER, MIDI, YM AND SNDH... "
    "AND EATS XM, MOD, S3M AND OKT MODULES TOO! CODED BY PASCAL \"INDUS\" NOWAK WITH THE HELP OF CLAUDE. RESPECT "
    "TO LEONARD OF OXYGENE. GWEM. SLIPPYEX AND THE EMUTOS TEAM. GREETINGS TO ALL ATARI ST MUSICIANS AND SCENERS "
    "AROUND THE WORLD AND ESPECIALLY MY FRIENDS FROM OVERLANDERS, NOEXTRA, THE REPLICANTS, HEMOROIDS, SECTOR ONE, "
    "MJJ PROD, YES CREW, MPS, DUNE, ELITE ... AND ALL MY RETIRED SCENERS FROM PRISM, THE PENDRAGONS, MAD, THE "
    "FIREHAWKS, ST CONNEXION, THE LEGACY, EQUINOX, PARADIZE, AND EVERYBODY I FORGOT ... KEEP THE 8 MHZ SPIRIT "
    "ALIVE!";

constexpr int kGlyph = 32, kSpace = 16;  // advance of a glyph and of a space
constexpr int kShadow = 5;               // shadow of the logo and of the letters: right and down (ombtit_shft)
// Logo (AFF_TITL.S): 29 lines, hidden above the screen at -35, 50 steps down (a line each) to 15; 2 s hidden, 20 s
// shown, at 25 steps a second.
constexpr int kMove = 50, kHide = 50, kShow = 500;
// Text: the glyphs' top line at kTextTop, ±kWave along the sine (160 pixels a period, moving right 2° a VBL); each
// line moved sideways by at most kWobble pixels (8° a line, turning 6° a VBL).
constexpr int kTextTop = 72, kWave = 12, kWobble = 2, kScroll = 2;

// One sine of the table (0..32768 around 16384) times amp, rounded: -amp to amp.
int sine(int degrees, int amp) noexcept {
    const int a = ((degrees % 360) + 360) % 360;
    return ((int(demo::kSine[size_t(a)]) - 16384) * amp + 8192) >> 14;
}

// Glyph of a character of the text (-1: a space, or a character the font has not).
int glyphOf(char c) noexcept {
    const size_t at = demo::kFontChars.find(c);
    return at == std::string_view::npos ? -1 : int(at);
}

// Each character's left edge in the text and its glyph; the width of the whole text.
struct Layout {
    std::vector<int> x, glyph;
    int width = 0;
};
const Layout& layout() {
    static const Layout l = [] {
        Layout t;
        for (const char c : kText) {
            const int g = glyphOf(c);
            t.x.push_back(t.width);
            t.glyph.push_back(g);
            t.width += g < 0 ? kSpace : kGlyph;
        }
        return t;
    }();
    return l;
}

// Pixels of the crate in its colours, and the rasters.
struct Colours {
    std::array<Color, 32 * 32> crate{};
    std::array<Color, 512> rasters{};
    std::array<std::array<Color, 16>, 8> logo{};
    std::array<Color, 16> font{};
};
const Colours& colours() {
    static const Colours c = [] {
        Colours k;
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x)
                k.crate[size_t(y * 32 + x)] = InfoDemo::colour(demo::kWoodPalette[size_t(demo::kCrate.at(x, y))]);
        for (size_t i = 0; i < k.rasters.size(); ++i) k.rasters[i] = InfoDemo::colour(demo::kRasters[i]);
        for (size_t p = 0; p < k.logo.size(); ++p)
            for (size_t i = 0; i < 16; ++i) k.logo[p][i] = InfoDemo::colour(demo::kLogoPalettes[p][i]);
        for (size_t i = 0; i < 16; ++i) k.font[i] = InfoDemo::colour(demo::kFontPalette[i]);
        return k;
    }();
    return c;
}

const demo::Picture& logoPicture(int logo) noexcept { return logo == 0 ? demo::kStymLogo : demo::kPrismLogo; }

// Layers of colour indices (-1 transparent) laid over the picture: they reach kShadow pixels beyond its top and left
// edges, whose shadow falls inside.
constexpr int kLayerW = InfoDemo::kW + kShadow, kLayerH = InfoDemo::kH + kShadow;
using Layer = std::vector<int8_t>;
size_t cell(int x, int y) noexcept { return size_t((y + kShadow) * kLayerW + x + kShadow); }

// A layer laid over the picture: its shadow in black, then its pixels, colour 15 taking the line's raster.
void lay(std::vector<Color>& px, const Layer& layer, const std::array<Color, 16>& palette, const InfoDemo& demo) {
    const Color black = InfoDemo::colour(0);
    for (int y = 0; y < InfoDemo::kH; ++y)
        for (int x = 0; x < InfoDemo::kW; ++x)
            if (layer[cell(x - kShadow, y - kShadow)] >= 0) px[size_t(y * InfoDemo::kW + x)] = black;
    for (int y = 0; y < InfoDemo::kH; ++y) {
        const Color raster = demo.raster(y);
        for (int x = 0; x < InfoDemo::kW; ++x)
            if (const int c = layer[cell(x, y)]; c >= 0)
                px[size_t(y * InfoDemo::kW + x)] = c == 15 ? raster : palette[size_t(c)];
    }
}

}  // namespace

InfoDemo::InfoDemo(uint32_t seed) : seed_(seed ? seed : 0x2149u), px_(size_t(kW * kH)) {
    makeCurves();
    newLogo();
    render();
}

Color InfoDemo::colour(uint16_t word) noexcept {
    const auto level = [word](int shift) {
        const uint32_t n = (word >> shift) & 15u;
        return ((n & 7u) << 1 | n >> 3) * 17u;
    };
    return gemui::rgb(level(8), level(4), level(0));
}

uint32_t InfoDemo::random() noexcept {  // xorshift32: the same draws on every platform
    seed_ ^= seed_ << 13;
    seed_ ^= seed_ >> 17;
    seed_ ^= seed_ << 5;
    return seed_;
}

// GENCURVE.S: v(i) = (base + A1 sin(h1 i + p1) + A2 sin(h2 i + p2) + A3 sin(h3 i + p3)) / 2, whole harmonics, so
// that the 360 points loop; the amplitudes and harmonics drawn in the ranges of ST BLIX's own curves.
void InfoDemo::makeCurves() {
    struct Term {
        int amp, harmonic, phase;
    };
    const auto draw = [this](int base, int mask) { return base + int(random() & uint32_t(mask)); };
    const auto curve = [](int base, std::array<Term, 3> t) {
        std::vector<int16_t> v(360);
        for (int i = 0; i < 360; ++i) {
            int sum = base;
            for (const Term& k : t)
                sum += ((int(demo::kSine[size_t((k.phase + k.harmonic * i) % 360)]) - 16384) * k.amp) >> 14;
            v[size_t(i)] = int16_t(sum >> 1);
        }
        return v;
    };
    const auto phase = [this] { return int(random() % 360u); };
    // Braced lists: the draws are made from left to right.
    curveX_ = curve(86, {Term{draw(48, 31), draw(5, 3), phase()}, Term{draw(12, 15), draw(7, 3), phase()},
                         Term{draw(4, 7), draw(2, 3), phase()}});
    curveY_ = curve(77, {Term{draw(20, 15), draw(2, 3), phase()}, Term{draw(20, 15), draw(4, 3), phase()},
                         Term{draw(30, 15), draw(1, 1), phase()}});
}

int InfoDemo::backgroundX() const noexcept { return curveX_[size_t(curve_)] & 31; }
int InfoDemo::backgroundY() const noexcept { return curveY_[size_t(curve_)] & 31; }

Color InfoDemo::raster(int y) const noexcept { return colours().rasters[size_t((vbl_ + uint64_t(y)) % 512)]; }

void InfoDemo::newLogo() {
    logo_ ^= 1;
    palette_ = int(random() & 7u);
    speed_ = int(random() >> 8 & 15u);
    // ST BLIX's s from 0 to 4; the STYM2149 logo, wider, up to 3 so that it stays on the screen.
    const int widest = logoPicture(logo_).w + 6 * 16 <= kW ? 4 : 3;
    shift_ = int(random() % uint32_t(widest + 1));
    angle_ = 0;
}

void InfoDemo::step() {
    curve_ = (curve_ + 1) % 360;
    switch (phase_) {
    case Phase::Hidden:
        if (timer_ == 0) newLogo();
        if (++timer_ == kHide) phase_ = Phase::Down, timer_ = 0;
        break;
    case Phase::Down:
        ++logoY_;
        if (++timer_ == kMove) phase_ = Phase::Shown, timer_ = 0;
        break;
    case Phase::Shown:
        if (++timer_ == kShow) phase_ = Phase::Up, timer_ = 0;
        break;
    case Phase::Up:
        --logoY_;
        if (++timer_ == kMove) phase_ = Phase::Hidden, timer_ = 0;
        break;
    }
    angle_ = (angle_ + speed_) % 360;
}

bool InfoDemo::advanceTo(uint64_t vbl) {
    if (vbl <= vbl_) return false;
    // A long pause (the computer asleep): the last minutes are enough to go on.
    if (vbl - vbl_ > 50 * 60) vbl_ = vbl - 50 * 60;
    for (uint64_t v = vbl_ + 1; v <= vbl; ++v)
        if (v % 2 == 0) step();
    vbl_ = vbl;
    scroll_ = vbl * kScroll;
    render();
    return true;
}

void InfoDemo::render() {
    // Background: the crate, its point (x, y) of the curves at the screen's top-left corner, moving right and up.
    const Colours& k = colours();
    const int bx = backgroundX(), by = backgroundY();
    for (int y = 0; y < kH; ++y) {
        const Color* row = &k.crate[size_t(((y + by) & 31) * 32)];
        Color* out = &px_[size_t(y * kW)];
        for (int x = 0; x < kW; ++x) out[x] = row[(x - bx) & 31];
    }
    drawLogo();
    drawText();
}

void InfoDemo::drawLogo() {
    if (logoY_ + 29 + kShadow <= 0) return;
    const demo::Picture& pic = logoPicture(logo_);
    Layer layer(size_t(kLayerW * kLayerH), -1);
    const int base = (kW - pic.w) / 2 - 3 * (1 << shift_);
    for (int i = 0; i < pic.h; ++i) {
        const int y = logoY_ + i;
        if (y < -kShadow || y >= kH) continue;
        // AFF_TITL.S: the table's sine × 12 << s >> 16, from 0 to 6 × 2^s pixels.
        const int a = (angle_ + i * speed_) % 360;
        const int x0 = base + ((int(demo::kSine[size_t(a)]) * 12) << shift_ >> 16);
        for (int c = 0; c < pic.w; ++c)
            if (const int x = x0 + c; x >= -kShadow && x < kW)
                if (const int idx = pic.at(c, i); idx >= 0) layer[cell(x, y)] = int8_t(idx);
    }
    lay(px_, layer, colours().logo[size_t(palette_)], *this);
}

void InfoDemo::drawText() {
    const Layout& t = layout();
    const int period = t.width + kW;  // the text enters from the right, leaves on the left, and comes again
    const int at = int(scroll_ % uint64_t(period)) - kW;  // text column at the screen's left edge
    // Glyph and column of the text under each column u of the layer, before the lines' wobble (-1: none).
    constexpr int kLeft = kShadow + kWobble;
    std::array<int, kLeft + kW + kWobble> glyph{}, column{};
    for (int u = -kLeft; u < kW + kWobble; ++u) {
        const int tx = at + u;
        int& g = glyph[size_t(u + kLeft)];
        g = -1;
        if (tx < 0 || tx >= t.width) continue;
        const size_t i = size_t(std::upper_bound(t.x.begin(), t.x.end(), tx) - t.x.begin()) - 1;
        g = t.glyph[i];
        column[size_t(u + kLeft)] = tx - t.x[i];
    }
    const int wave = int((vbl_ * 2) % 360), wobble = int((vbl_ * 6) % 360);
    Layer layer(size_t(kLayerW * kLayerH), -1);
    for (int y = kTextTop - kWave; y < kTextTop + kGlyph + kWave && y < kH; ++y) {
        const int dx = sine(y * 8 + wobble, kWobble);
        for (int x = -kShadow; x < kW; ++x) {
            const int u = x - dx;
            const int g = glyph[size_t(u + kLeft)];
            if (g < 0) continue;
            const int row = y - kTextTop - sine(u * 9 / 4 - wave, kWave);
            if (row < 0 || row >= kGlyph) continue;
            if (const int idx = demo::kFont[size_t(g)].at(column[size_t(u + kLeft)], row); idx >= 0)
                layer[cell(x, y)] = int8_t(idx);
        }
    }
    lay(px_, layer, colours().font, *this);
}

}  // namespace stym::pluginui
