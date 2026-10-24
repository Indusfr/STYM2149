// STYM2149 — pluginui: Instruments window (slots, Store, Load, Rename, Clear, exchange with Patches)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "pluginui/InstrumentsWindow.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "gemui/Alert.h"
#include "pluginui/MainWindow.h"

namespace stym::pluginui {

using gemui::Button;
using gemui::MenuItem;
using gemui::Rect;

namespace {
// Padding of the content, gap between the two columns, gap between the slot buttons of a column (px).
constexpr double kPad = 8, kGap = 8, kRowGap = 4;
constexpr int kRows = plugin::kInstruments / 2;  // two columns: $01-$10, $11-$20

// Two upper-case hexadecimal digits: the slot numbers $01 to $20.
std::string hex2(int v) {
    static const char* const kHex = "0123456789ABCDEF";
    return {kHex[(v >> 4) & 15], kHex[v & 15]};
}
}  // namespace

InstrumentsWindow::InstrumentsWindow(MainWindow& main) : m_(main) {
    build();
    wire();
    place({double(kX), double(kY), double(kWidth), 90});  // provisional height: layout() sets the real one
}

void InstrumentsWindow::build() {
    setTitle("INSTRUMENTS");
    add(hint_);
    add(sep_);
    for (Button& b : slots_) add(b);
}

void InstrumentsWindow::wire() {
    onClose = [this] { m_.syncWindowButtons(); };
    for (int k = 0; k < plugin::kInstruments; ++k) {
        Button& b = slots_[size_t(k)];
        b.onClick = [this, k] {
            if (at(k).used) loadIntoChannel(k);
            else storeChannel(k);
        };
        b.onContext = [this, k](gemui::Point) { slotMenu(k); };
    }
}

const plugin::Instrument& InstrumentsWindow::at(int k) const { return m_.extras_.instruments[size_t(k)]; }

void InstrumentsWindow::put(int k, plugin::Instrument ins) {
    m_.host_.editExtras([&](plugin::NonParamState& s) { s.instruments[size_t(k)] = std::move(ins); });
    m_.pullExtras();  // windows refreshed (INS row of the Step Sequencer included)
}

// ------------------------------------------------------------------------------------------- actions

void InstrumentsWindow::storeChannel(int k) {
    // Name asked as for a patch; an empty name stores nothing. The slot keeps its proposed name.
    const std::string name = at(k).used ? at(k).name : "Instrument " + std::to_string(k + 1);
    prompt("Instrument " + hex2(k + 1) + " name:", name, [this, k](const std::string& n) {
        const std::string clean = plugin::cleanInstrumentName(n);
        if (clean.empty()) return;
        put(k, plugin::captureInstrument(clean, m_.values_, m_.extras_, m_.cur_));
    });
}

void InstrumentsWindow::loadIntoChannel(int k) {
    // Like loading a patch (libApply): one undo step of the focused track.
    if (!at(k).used) return;
    const int t = m_.cur_;
    const plugin::PatchChange c = plugin::instrumentChange(at(k), m_.extras_, t);
    // armCur takes the track as it is; pushCur at the end pushes it once. Each host parameter that differs is set in
    // a host gesture of its own, and values_ is read back from the host.
    m_.armCur();
    for (const auto& [i, v] : c.params) {
        if (m_.host_.param(i) == v) continue;
        m_.host_.beginGesture(i);
        m_.host_.setParam(i, v);
        m_.host_.endGesture(i);
        m_.values_[size_t(i)] = m_.host_.param(i);
    }
    m_.host_.editExtras([&](plugin::NonParamState& s) { s.tracks[size_t(t)] = c.track; });
    m_.pullExtras();
    m_.pushCur();
}

void InstrumentsWindow::rename(int k) {
    if (!at(k).used) return;
    prompt("Rename instrument " + hex2(k + 1) + ":", plugin::instrumentLabel(at(k), k), [this, k](const std::string& n) {
        const std::string clean = plugin::cleanInstrumentName(n);
        if (clean.empty() || !at(k).used) return;
        plugin::Instrument ins = at(k);
        ins.name = clean;
        put(k, std::move(ins));
    });
}

void InstrumentsWindow::clear(int k) {
    // The steps that point to it play the sound of their track; their INS row keeps the number.
    if (at(k).used) put(k, plugin::Instrument{});
}

void InstrumentsWindow::saveToPatches(int k) {
    if (!at(k).used) return;
    plugin::Instrument ins = at(k);
    ins.name = plugin::instrumentLabel(ins, k);
    m_.patches().append(plugin::instrumentEntry(ins));
}

void InstrumentsWindow::loadFromPatches(int k) {
    const std::vector<plugin::PatchEntry> lib = m_.patches().reload();
    std::vector<MenuItem> items;
    for (const plugin::PatchEntry& e : lib) {
        MenuItem m;
        m.label = plugin::patchName(e);
        m.pick = [this, k, e] { put(k, plugin::instrumentFromEntry(e)); };
        items.push_back(std::move(m));
    }
    if (items.empty()) {
        MenuItem m;
        m.label = "Library is empty";
        m.disabled = true;
        m.why = "Patches: Save current stores the focused channel sound";
        items.push_back(std::move(m));
    }
    m_.popup(slots_[size_t(k)], std::move(items), -1);
}

void InstrumentsWindow::slotMenu(int k) {
    const bool used = at(k).used;
    std::vector<MenuItem> items;
    auto item = [&](const char* label, bool needs, std::function<void()> pick) {
        MenuItem m;
        m.label = label;
        m.pick = std::move(pick);
        if (needs && !used) {
            m.disabled = true;
            m.why = "Empty slot";
        }
        items.push_back(std::move(m));
    };
    item("Store channel", false, [this, k] { storeChannel(k); });
    item("Load into channel", true, [this, k] { loadIntoChannel(k); });
    item("Rename", true, [this, k] { rename(k); });
    items.push_back(MenuItem::separatorItem());
    item("Save to Patches", true, [this, k] { saveToPatches(k); });
    item("Load from Patches", false, [this, k] { loadFromPatches(k); });
    items.push_back(MenuItem::separatorItem());
    item("Clear", true, [this, k] { clear(k); });
    m_.trackPopup(slots_[size_t(k)], std::move(items), -1);
}

void InstrumentsWindow::prompt(const std::string& message, const std::string& text,
                               std::function<void(const std::string&)> ok) {
    gemui::Alert::Spec spec;
    spec.lines = {{message}};
    spec.promptButtons();
    spec.inputText = text;
    spec.inputChars = int(plugin::kInstrumentNameMax);
    gemui::Alert::open(m_.desk_, std::move(spec), [ok = std::move(ok)](int button, const std::string& t) {
        if (button == gemui::Alert::kPromptOk) ok(t);
    });
}

// ------------------------------------------------------------------------------------------- display

void InstrumentsWindow::show() {
    open();
    refresh();
    m_.syncWindowButtons();
}

void InstrumentsWindow::refresh() {
    if (!isOpen()) return;
    for (int k = 0; k < plugin::kInstruments; ++k) {
        const plugin::Instrument& ins = at(k);
        Button& b = slots_[size_t(k)];
        b.setText(hex2(k + 1) + (ins.used ? " " + plugin::instrumentLabel(ins, k) : " —"));
        b.setTip(ins.used ? "Click: load into the focused channel" : "Click: store the focused channel here");
    }
    layout();
}

void InstrumentsWindow::layout() {
    const double ox = box().x, oy = box().y;
    const double x = ox + 14;  // content like that of the Patches window (border 2, margin 4, padding 8)
    const double contentW = kWidth - 12 - 2 * kPad;

    // The note wraps within the width of its box: given the content width first, it then counts its lines.
    double y = oy + 32 + kPad;  // below the frame (6) and the title bar (26)
    hint_.setBox({x, y, contentW, 12});
    const double hintH = 12.0 * hint_.lineCount();
    hint_.setBox({x, y, std::min(hint_.naturalWidth(), contentW), hintH});
    y += hintH + kPad;
    sep_.setBox({ox + 6, y, contentW + 2 * kPad, 2});
    y += 2 + kPad;

    // Slots column by column: k / kRows is the column, k % kRows the row; each button as wide as its column.
    const double colW = (contentW - kGap) / 2;
    const double h = slots_[0].naturalHeight();
    for (int k = 0; k < plugin::kInstruments; ++k) {
        Button& b = slots_[size_t(k)];
        b.setMinWidth(colW);
        b.setBox({x + (k / kRows) * (colW + kGap), y + (k % kRows) * (h + kRowGap), colW, h});
    }
    y += kRows * h + (kRows - 1) * kRowGap;

    // Height: content, padding of 8, border and margin (6); the width stays kWidth.
    const double wh = y + kPad + 6 - oy;
    if (std::abs(wh - box().h) > 0.01 || std::abs(kWidth - box().w) > 0.01) place({ox, oy, double(kWidth), wh});
    repaint();
}

std::optional<Rect> InstrumentsWindow::rectOf(std::string_view id) const {
    if (!isOpen()) return std::nullopt;
    if (id == "insWin") return bounds();
    if (id == "insHint") return hint_.bounds();
    if (id.substr(0, 7) == "insSlot") {
        const std::string num(id.substr(7));
        const int k = num.empty() || num.size() > 2 ? -1 : std::atoi(num.c_str());
        if (k >= 0 && k < plugin::kInstruments) return slots_[size_t(k)].bounds();
    }
    return std::nullopt;
}

}  // namespace stym::pluginui
