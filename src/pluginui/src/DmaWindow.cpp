// STYM2149 — pluginui: DMA window (sample bank of the STE DMA sound)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "pluginui/DmaWindow.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "plugincore/DmaSample.h"
#include "plugincore/Parameters.h"
#include "pluginui/Layout.h"
#include "pluginui/MainWindow.h"
#include "ymseq/Tables.h"

namespace stym::pluginui {

using gemui::Button;
using gemui::Face;
using gemui::Font;
using gemui::MenuItem;
using gemui::Rect;
using plugin::Global;
using ymengine::DmaParams;
using ymengine::DmaSlot;
namespace colors = gemui::colors;

namespace {

constexpr double kPad = 8, kGap = 8;  // padding of the content and gap between the items of a row (px)
constexpr double kGainDb = 3;         // step of the -3 dB and +3 dB tools
// Labels of the rate buttons, ymengine::kDmaRateHz rounded (6258, 12517, 25033 and 50066 Hz).
constexpr std::array<const char*, ymengine::kDmaRates> kRateText = {"6.3k", "12.5k", "25k", "50k"};

}  // namespace

std::string DmaWindow::rateName(int k) { return kRateText[size_t(std::clamp(k, 0, ymengine::kDmaRates - 1))]; }

DmaWindow::DmaWindow(MainWindow& main) : m_(main) {
    build();
    wire();
    place({double(kX), double(kY), 300, 90});  // provisional size: layout() sets the real one at the first refresh
}

void DmaWindow::build() {
    setTitle("DMA - STE");
    for (gemui::Label* l : {&hint_, &hint2_}) {
        l->setFace(Face::kSmall);
        l->setColor(colors::kHint);
    }
    useSte_.setTip("Set Computer to STE (SYSTEM panel, or Options > Sound engine > Computer)");
    play_.setId("dmaPlay");
    for (int k = 0; k < ymengine::kDmaSlots; ++k) {
        Button& b = slots_[size_t(k)];
        b.setText(std::to_string(k + 1));
        b.setPadding(2, 6);
    }
    load_.setTip("Load an audio file (resampled to the rate) or raw 8-bit bytes (.raw, .spl, .sam) into this slot");
    play_.setTip("Play this sample once at its rate");
    clear_.setTip("Empty this slot");
    for (int k = 0; k < ymengine::kDmaRates; ++k) {
        Button& b = rates_[size_t(k)];
        b.setText(kRateText[size_t(k)]);
        char tip[96];
        std::snprintf(tip, sizeof tip, "Play this sample at %.0f Hz (the bytes are kept: the pitch moves)",
                      ymengine::kDmaRateHz[size_t(k)]);
        b.setTip(tip);
    }
    mono_.setTip("Mono: one byte per frame (a stereo sample is mixed down: left and right averaged)");
    stereo_.setTip("Stereo: bytes interleaved left, right (a mono sample is doubled, 16 KB per side at most)");
    loop_.setTip("Loop while the note is held; otherwise the sample plays to its end");
    root_.setRange(0, 127, 1);
    root_.setTip("Root note: the note that plays the sample at its rate");
    root_.setId("dmaRoot");
    rootv_.setPx(14);
    rootv_.setWholeText(true);  // a note name: C#4, Sol3 or a MIDI number (D-264)
    rootv_.setTip("Click to type a note (C#4, Sol3) or a MIDI note number");
    rootv_.onCommit = [this](const std::string& t) {
        const auto n = ymseq::parseAbsNote(t);
        if (!n) return false;
        root_.input(*n);
        return true;
    };
    sign_.setTip("Flip the top bit of each byte (signed <-> unsigned)");
    gainDown_.setTip("Lower the sample by 3 dB");
    gainUp_.setTip("Raise the sample by 3 dB (clipped)");
    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{
             &status_, &useSte_, &hint_, &hint2_, &sep_, &labSample_, &wave_, &load_, &play_, &clear_, &labRate_,
             &labFormat_, &mono_, &stereo_, &loop_, &labRoot_, &root_, &rootv_, &labTools_, &sign_, &gainDown_,
             &gainUp_})
        add(*w);
    for (auto& b : slots_) add(b);
    for (auto& b : rates_) add(b);
}

void DmaWindow::wire() {
    onClose = [this] { m_.syncWindowButtons(); };
    useSte_.onClick = [this] { m_.setComputer(1); };  // 1: STE
    // Loading goes through the host (file dialog, decoding or raw bytes), asynchronous and without an undo step; the
    // preview is a command to the engine on the audio thread.
    for (int k = 0; k < ymengine::kDmaSlots; ++k)
        slots_[size_t(k)].onClick = [this, k] {
            sel_ = k;
            refresh();
        };
    wave_.onClick = [this] { m_.host_.loadDmaSample(sel_); };
    load_.onClick = [this] { m_.host_.loadDmaSample(sel_); };
    play_.onClick = [this] { m_.host_.seqCommand(Host::SeqCommand::DmaPreview, sel_); };
    clear_.onClick = [this] { editBytes([](DmaSlot&, std::vector<int8_t>& b) { b.clear(); }); };
    for (int k = 0; k < ymengine::kDmaRates; ++k)
        rates_[size_t(k)].onClick = [this, k] { edit([k](DmaSlot& s) { s.rate = uint8_t(k); }); };
    mono_.onClick = [this] { setStereo(false); };
    stereo_.onClick = [this] { setStereo(true); };
    loop_.onClick = [this] { edit([](DmaSlot& s) { s.loop = !s.loop; }); };

    // Root note: one undo step per gesture (armed on press). While it is held, MainWindow::dragging_ points to it so
    // that refresh() does not move it under the pointer; a change without a gesture (typed value) makes its own step.
    root_.onGestureStart = [this] {
        m_.dragging_ = &root_;
        rootArmed_ = true;
    };
    root_.onGestureEnd = [this] {
        m_.dragging_ = nullptr;
        refresh();
    };
    root_.onChange = [this](double v) {
        DmaParams p = params();
        DmaSlot& s = p.slots[size_t(sel_)];
        const auto root = uint8_t(std::clamp(int(v), 0, 127));
        if (s.root == root) return;
        s.root = root;
        if (rootArmed_ || m_.dragging_ != &root_) pushUndo(-1);
        rootArmed_ = false;
        m_.host_.editExtras([&p](plugin::NonParamState& st) { st.dma = p; });
        m_.pullExtras();
    };

    sign_.onClick = [this] { editBytes([](DmaSlot&, std::vector<int8_t>& b) { plugin::dmaSign(b); }); };
    gainDown_.onClick = [this] { editBytes([](DmaSlot&, std::vector<int8_t>& b) { plugin::dmaGain(b, -kGainDb); }); };
    gainUp_.onClick = [this] { editBytes([](DmaSlot&, std::vector<int8_t>& b) { plugin::dmaGain(b, kGainDb); }); };
}

// ------------------------------------------------------------------------------------------- state

bool DmaWindow::ste() const noexcept { return m_.ste(); }
const DmaParams& DmaWindow::params() const noexcept { return m_.extras_.dma; }
const std::vector<int8_t>& DmaWindow::bytes() const noexcept { return m_.extras_.dmaSamples[size_t(sel_)]; }

// The step records the bank as it is now, before the change (undoEntryLike reads the current state), and the bytes of
// `slot` when it is one.
void DmaWindow::pushUndo(int slot) {
    UndoEntry e;
    e.kind = UndoEntry::Kind::Dma;
    e.index = slot;
    m_.host_.session().undo.push(m_.undoEntryLike(e));
}

void DmaWindow::edit(const std::function<void(DmaSlot&)>& fn) {
    // Nothing changes (button already chosen): no undo step.
    DmaParams p = params();
    fn(p.slots[size_t(sel_)]);
    if (p == params()) return;
    pushUndo(-1);
    // The host publishes the new state to the audio thread; pullExtras copies it back and refreshes every window.
    m_.host_.editExtras([&p](plugin::NonParamState& s) { s.dma = p; });
    m_.pullExtras();
}

void DmaWindow::editBytes(const std::function<void(DmaSlot&, std::vector<int8_t>&)>& fn) {
    DmaParams p = params();
    const int slot = sel_;
    std::vector<int8_t> b = bytes();
    fn(p.slots[size_t(slot)], b);
    if (p == params() && b == bytes()) return;
    pushUndo(slot);
    m_.host_.editExtras([&](plugin::NonParamState& s) {
        s.dma = p;
        s.dmaSamples[size_t(slot)] = std::move(b);
    });
    m_.pullExtras();
}

void DmaWindow::setStereo(bool stereo) {
    // Bytes kept: mono → stereo doubles each byte (16 KB per channel at most); stereo → mono averages the two channels.
    editBytes([stereo](DmaSlot& s, std::vector<int8_t>& b) {
        if (s.stereo == stereo) return;
        s.stereo = stereo;
        std::vector<int8_t> out;
        if (stereo) {
            const size_t n = std::min(b.size(), size_t(ymengine::kDmaMaxBytes / 2));
            out.reserve(2 * n);
            for (size_t i = 0; i < n; ++i) {
                out.push_back(b[i]);
                out.push_back(b[i]);
            }
        } else {
            // Mean of each left/right pair rounded down (an odd last byte is dropped).
            out.reserve(b.size() / 2);
            for (size_t i = 0; i + 1 < b.size(); i += 2)
                out.push_back(int8_t(std::floor((int(b[i]) + int(b[i + 1])) / 2.0)));
        }
        b = std::move(out);
    });
}

void DmaWindow::showSlot(int slot) {
    sel_ = std::clamp(slot, 0, ymengine::kDmaSlots - 1);
    show();
}

void DmaWindow::show() {
    open();
    refresh();
    m_.syncWindowButtons();
}

void DmaWindow::refresh() {
    if (!isOpen()) return;
    const bool steOn = ste();
    const DmaSlot& s = params().slots[size_t(sel_)];
    const std::vector<int8_t>& b = bytes();
    std::string where = "any track can use the DMA type";
    if (m_.mode3()) {
        // 3 channels: four tracks per chip (A, B, C and the Digidrum track), then the eight DMA tracks.
        const int first = 4 * m_.chips() + 1;
        where = "DMA tracks " + std::to_string(first) + " to " + std::to_string(first + ymengine::kDmaTracks - 1);
    }
    status_.setText(steOn ? "Computer: STE, " + where + "."
                          : std::string("Computer: ST. The DMA sound needs the STE."));
    useSte_.setVisible(!steOn);

    const bool latin = m_.host_.preference("noteNames", "0") == "1";
    const auto names = latin ? ymseq::NoteNames::Latin : ymseq::NoteNames::English;
    for (int k = 0; k < ymengine::kDmaSlots; ++k) {
        Button& sb = slots_[size_t(k)];
        sb.setOn(k == sel_);
        const size_t n = m_.extras_.dmaSamples[size_t(k)].size();
        sb.setTip("Sample " + std::to_string(k + 1) + ": " + (n ? std::to_string(n) + " bytes" : "empty"));
    }
    wave_.setSample(sel_, b, ymengine::kDmaRateHz[s.rate], s.stereo);
    for (int k = 0; k < ymengine::kDmaRates; ++k) rates_[size_t(k)].setOn(k == s.rate);
    mono_.setOn(!s.stereo);
    stereo_.setOn(s.stereo);
    loop_.setOn(s.loop);
    if (m_.dragging_ != &root_) root_.setValue(s.root);
    if (!rootv_.editing()) rootv_.setText(ymseq::absNoteName(s.root, names));
    for (Button* t : {&play_, &clear_, &sign_, &gainDown_, &gainUp_}) t->setEnabled(!b.empty());
    layout();
}

// ------------------------------------------------------------------------------------------- layout

// One column of rows (flexRow lines without wrapping) under the status line, the notes and a rule across the window;
// the content is as wide as the widest of the column (364 px), the status row and the notes.
void DmaWindow::layout() {
    const double ox = box().x, oy = box().y;
    const double x = ox + 14;  // content (border 2, margin 4, padding 8)
    auto btn = [](Button& b) { return FlexItem{&b, b.naturalWidth(), double(b.naturalHeight())}; };
    auto lab = [](gemui::Label& l, double w = 0) { return FlexItem{&l, w > 0 ? w : l.naturalWidth(), 16}; };
    auto sld = [](gemui::Slider& s, double w) { return FlexItem{&s, w, 16}; };

    const double col = 364;
    std::vector<FlexItem> head = {lab(status_)};
    if (useSte_.visible()) head.push_back(btn(useSte_));
    double headW = 0;
    for (const FlexItem& f : head) headW += f.width;
    headW += kGap * double(head.size() - 1);
    const double contentW = std::max({col, headW, hint_.naturalWidth(), hint2_.naturalWidth()});

    double y = oy + 32 + kPad;  // below the frame (6) and the title bar (26)
    y += flexRow(x, y, contentW, head, kGap, false) + 6;
    hint_.setBox({x, y, hint_.naturalWidth(), 12});
    y += 12;
    hint2_.setBox({x, y, hint2_.naturalWidth(), 12});
    y += 12 + 8;
    sep_.setBox({x - kPad, y, contentW + 2 * kPad, 2});
    y += 2 + 8;

    std::vector<FlexItem> sl = {lab(labSample_)};
    for (auto& b : slots_) sl.push_back(btn(b));
    y += flexRow(x, y, col, sl, 4, false) + 6;
    wave_.setBox({x, y, col, 60});
    y += 60 + 6;
    std::vector<FlexItem> io = {btn(load_), btn(play_), btn(clear_)};
    y += flexRow(x, y, col, io, 4, false) + 6;
    // Labels as wide as the widest, "Format", so that the buttons of the rows line up.
    const double lw = Font::width("Format", 14);
    std::vector<FlexItem> rt = {lab(labRate_, lw)};
    for (auto& b : rates_) rt.push_back(btn(b));
    y += flexRow(x, y, col, rt, 4, false) + 6;
    std::vector<FlexItem> fm = {lab(labFormat_, lw), btn(mono_), btn(stereo_), btn(loop_)};
    fm.back().marginLeft = 12;
    y += flexRow(x, y, col, fm, 4, false) + 6;
    std::vector<FlexItem> rr = {lab(labRoot_, lw), sld(root_, 200), {&rootv_, Font::width("C#10", 14), 16}};
    y += flexRow(x, y, col, rr, kGap, false) + 6;
    std::vector<FlexItem> tl = {lab(labTools_, lw), btn(sign_), btn(gainDown_), btn(gainUp_)};
    y += flexRow(x, y, col, tl, 4, false);

    // Window: content, padding of 8, border and margin (6 on each side); placed again only when its size changes.
    const double w = contentW + 2 * kPad + 12;
    const double h = y + kPad + 6 - oy;
    if (std::abs(h - box().h) > 0.01 || std::abs(w - box().w) > 0.01) place({ox, oy, w, h});
    repaint();
}

std::optional<Rect> DmaWindow::rectOf(std::string_view id) const {
    if (!isOpen()) return std::nullopt;
    if (id == "dmaWin") return bounds();
    // Index k of an id written `prefix` then one digit k below n; -1 otherwise.
    auto digit = [&](std::string_view prefix, int n) -> int {
        if (id.size() != prefix.size() + 1 || id.substr(0, prefix.size()) != prefix) return -1;
        const int k = id.back() - '0';
        return k >= 0 && k < n ? k : -1;
    };
    if (const int k = digit("dmaSlot", ymengine::kDmaSlots); k >= 0) return slots_[size_t(k)].bounds();
    if (const int k = digit("dmaRate", ymengine::kDmaRates); k >= 0) return rates_[size_t(k)].bounds();
    struct Named {
        std::string_view id;
        const gemui::Widget* w;
    };
    const Named named[] = {{"dmaUseSte", &useSte_},     {"dmaWave", &wave_},     {"dmaLoad", &load_},
                           {"dmaPlay", &play_},         {"dmaClear", &clear_},   {"dmaMono", &mono_},
                           {"dmaStereo", &stereo_},     {"dmaLoop", &loop_},     {"dmaRoot", &root_},
                           {"dmaRootV", &rootv_},       {"dmaSign", &sign_},     {"dmaGainDown", &gainDown_},
                           {"dmaGainUp", &gainUp_}};
    for (const Named& n : named)
        if (id == n.id) return n.w->visible() ? std::optional<Rect>(n.w->bounds()) : std::nullopt;
    return std::nullopt;
}

const gemui::Slider* DmaWindow::slider(std::string_view id) const noexcept {
    return id == "dmaRoot" ? &root_ : nullptr;
}

}  // namespace stym::pluginui
