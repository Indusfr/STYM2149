// STYM2149 — pluginui: Patches window (Save current, list, Load, Rename, Overwrite, Delete)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Behaviours taken from the prototype: saveLib, libSnap, libApply, refreshLib, openLib, closeLib, the handlers of
// #libSave, #libImport and #libExport, the context menu of a patch.
#include "pluginui/PatchesWindow.h"

#include <algorithm>
#include <cmath>

#include "gemui/Alert.h"
#include "pluginui/Layout.h"
#include "pluginui/MainWindow.h"

namespace stym::pluginui {

using gemui::Button;
using gemui::MenuItem;
using gemui::Rect;

namespace {
// Padding of the content, gap in the row of buttons, gap between the patch buttons (#libList) (px).
constexpr double kPad = 8, kGap = 8, kListGap = 6;
constexpr double kHintLine = 16;  // #libHint: inline text in the 16 px lines of .mixbody
}  // namespace

PatchesWindow::PatchesWindow(MainWindow& main) : m_(main) {
    build();
    wire();
    place({double(kX), double(kY), double(kWidth), 90});  // provisional height: layout() sets the real one
}

void PatchesWindow::build() {
    // Prototype ids of the sliders and buttons that MIDI learn can learn (D-146).
    for (const auto& [w, id] : std::initializer_list<std::pair<gemui::Widget*, const char*>>{
            {&save_, "libSave"}, {&import_, "libImport"}, {&export_, "libExport"}})
        w->setId(id);
    setTitle("PATCHES");
    save_.setTip("Save the current patch to the library");
    import_.setTip("Load a patch library from a file");
    export_.setTip("Write the patch library to a file");
    empty_.setLineHeight(kHintLine);
    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&save_, &import_, &export_, &hint_, &sep_, &empty_})
        add(*w);
}

void PatchesWindow::wire() {
    onClose = [this] { m_.syncWindowButtons(); };  // closeLib
    save_.onClick = [this] {
        // gemPrompt "Patch name:"; an empty name saves nothing.
        prompt("Patch name:", "Patch " + std::to_string(lib_.size() + 1), [this](const std::string& n) {
            if (n.empty()) return;
            append(plugin::snapPatch(n, m_.values_, m_.extras_, m_.cur_));  // the file read again first
        });
    };
    // Library files (M7, D-141): Import appends the patches of the file at the end (patchLib.push), Export writes the
    // library in the prototype's format.
    import_.onClick = [this] {
        m_.openFile(plugin::FileKind::Patches, [this](const std::string& text) { importText(text); });
    };
    export_.onClick = [this] {
        load();
        if (unreadable_) {
            store();  // the alert for the unreadable file
            return;
        }
        refresh();  // the buttons follow the library read again: their indices are those of lib_
        m_.saveFileAs(plugin::FileKind::Patches, plugin::dumpPatchLibrary(lib_));
    };
}

// ------------------------------------------------------------------------------------------- library

// An empty text is an empty library; a text that does not parse marks the file unreadable (store then refuses to
// overwrite it).
void PatchesWindow::load() {
    const std::string text = m_.host_.patchLibrary();
    unreadable_ = false;
    lib_.clear();
    if (text.empty()) return;
    if (auto lib = plugin::parsePatchLibrary(text)) {
        lib_ = std::move(*lib);
    } else {
        unreadable_ = true;
    }
}

const std::vector<plugin::PatchEntry>& PatchesWindow::reload() {
    load();
    refresh();
    return lib_;
}

void PatchesWindow::append(plugin::PatchEntry e) {
    load();
    if (!unreadable_) lib_.push_back(std::move(e));
    store();
}

// The imported file is checked before the library is read again; a file written by a newer version is taken for what
// is known, and an alert says so (D-174).
void PatchesWindow::importText(const std::string& text) {
    bool newer = false;
    std::optional<std::vector<plugin::PatchEntry>> read = plugin::parsePatchLibrary(text, nullptr, &newer);
    if (!read) {
        m_.showMessage("Not a valid patch library file.");
        return;
    }
    load();
    if (!unreadable_) lib_.insert(lib_.end(), read->begin(), read->end());
    store();
    if (newer && !unreadable_) m_.newerFileAlert();
}

void PatchesWindow::store() {
    if (unreadable_) {
        // The existing file could not be read: it is not overwritten (the prototype started again from an empty list).
        gemui::Alert::Spec spec;
        spec.lines = {{"The patch library file could not be read."}, {"It was left unchanged."}};
        gemui::Alert::open(m_.desk_, std::move(spec));
        load();
    } else {
        m_.host_.setPatchLibrary(plugin::dumpPatchLibrary(lib_));
    }
    refresh();
}

bool PatchesWindow::stillAt(int k, const std::string& name) {
    // The library is one file shared by every instance of the plugin: it is read again before a change, and patch k is
    // changed only if it is still the one the menu was opened on; otherwise the list is shown as it now is.
    load();
    if (unreadable_) {
        store();  // the alert for the unreadable file
        return false;
    }
    if (k >= 0 && k < int(lib_.size()) && plugin::patchName(lib_[size_t(k)]) == name) return true;
    refresh();
    return false;
}

void PatchesWindow::loadInto(int k) {
    // libApply on the focused track: one undo step of the track (the prototype made none).
    if (k < 0 || k >= int(lib_.size())) return;
    const int t = m_.cur_;
    const plugin::PatchChange c = plugin::patchChange(lib_[size_t(k)], m_.extras_, t);
    // armCur takes the track as it is; pushCur at the end pushes it once. Each host parameter that differs is set in
    // a host gesture of its own, and values_ is read back from the host; the rest of the patch goes into the
    // non-automatable state of the track.
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

void PatchesWindow::patchMenu(int k) {
    if (k < 0 || k >= int(lib_.size())) return;
    const std::string name = plugin::patchName(lib_[size_t(k)]);
    std::vector<MenuItem> items;
    auto item = [&](const char* label, std::function<void()> pick) {
        MenuItem m;
        m.label = label;
        m.pick = std::move(pick);
        items.push_back(std::move(m));
    };
    item("Load", [this, k] { loadInto(k); });
    item("Rename", [this, k, name] {
        prompt("Rename patch:", name, [this, k, name](const std::string& n) {
            if (n.empty() || !stillAt(k, name)) return;
            plugin::renamePatch(lib_[size_t(k)], n);
            store();
        });
    });
    item("Overwrite", [this, k, name] {
        if (!stillAt(k, name)) return;
        lib_[size_t(k)] = plugin::snapPatch(name, m_.values_, m_.extras_, m_.cur_);
        store();
    });
    items.push_back(MenuItem::separatorItem());
    item("Delete", [this, k, name] {
        if (!stillAt(k, name)) return;
        lib_.erase(lib_.begin() + k);
        store();
    });
    m_.trackPopup(*buttons_[size_t(k)], std::move(items), -1);
}

void PatchesWindow::prompt(const std::string& message, const std::string& text,
                           std::function<void(const std::string&)> ok) {
    gemui::Alert::Spec spec;
    spec.lines = {{message}};
    spec.promptButtons();
    spec.inputText = text;
    spec.inputChars = 30;  // width of the field in characters, as the prototype's prompt field (size 30)
    spec.inputMax = -1;  // no limit, as the prototype's gemPrompt without maxLen: the field shows the end of the name
    gemui::Alert::open(m_.desk_, std::move(spec), [ok = std::move(ok)](int button, const std::string& t) {
        if (button == gemui::Alert::kPromptOk) ok(t);
    });
}

// ------------------------------------------------------------------------------------------- display

void PatchesWindow::show() {
    // openLib: the window comes to the front, the library is read again (shared by all projects).
    load();
    open();
    refresh();
    m_.syncWindowButtons();
}

void PatchesWindow::refresh() {
    // refreshLib: one button per patch, the note when the library is empty.
    if (!isOpen()) return;
    // The buttons are made again only when the number of patches changes; their callbacks hold their index.
    if (buttons_.size() != lib_.size()) {
        for (auto& b : buttons_) remove(*b);
        buttons_.clear();
        for (size_t k = 0; k < lib_.size(); ++k) {
            auto b = std::make_unique<Button>();
            b->onClick = [this, k] { loadInto(int(k)); };
            b->onContext = [this, k](gemui::Point) { patchMenu(int(k)); };
            add(*b);
            buttons_.push_back(std::move(b));
        }
    }
    // A name wider than the window is cut with "..." on its button, the tooltip giving it whole.
    const double maxW = kWidth - 12 - 2 * kPad;
    for (size_t k = 0; k < lib_.size(); ++k) {
        Button& b = *buttons_[k];
        std::string name = plugin::patchName(lib_[k]);
        b.setText(name);
        b.setTip(std::string());
        if (b.naturalWidth() <= maxW) continue;
        b.setTip(name);
        while (!name.empty()) {
            size_t n = name.size() - 1;  // last UTF-8 character
            while (n > 0 && (uint8_t(name[n]) & 0xC0) == 0x80) --n;
            name.resize(n);
            b.setText(name + "...");
            if (b.naturalWidth() <= maxW) break;
        }
    }
    layout();
}

void PatchesWindow::layout() {
    const double ox = box().x, oy = box().y;
    const double x = ox + 14;  // .mixbody content (border 2, margin 4, padding 8)
    const double contentW = kWidth - 12 - 2 * kPad;

    // Row of buttons (flex, wrap, gap 8); the note (.hint, margin-top 6) wraps to the next line if it does not fit and
    // folds there across the width of the content.
    // The note counts its lines in the width of its box (its own width, at most the content's); its item in the row
    // includes its 6 px top margin.
    double y = oy + 32 + kPad;  // below the frame (6) and the title bar (26)
    std::vector<FlexItem> row;
    for (Button* b : {&save_, &import_, &export_}) row.push_back({b, b->naturalWidth(), double(b->naturalHeight())});
    hint_.setBox({x, y, contentW, 12});
    const double hintW = std::min(hint_.naturalWidth(), contentW);
    hint_.setBox({x, y, hintW, 12});
    const double hintH = 12.0 * hint_.lineCount();
    row.push_back({&hint_, hintW, hintH + 6});
    y += flexRow(x, y, contentW, row, kGap);
    const FlexItem& h = row.back();
    hint_.setBox({h.box.x, h.box.y + 6, hintW, hintH});

    // 2 px rule up to the edges of the window (margin 8px -8px).
    y += kPad;
    sep_.setBox({ox + 6, y, contentW + 2 * kPad, 2});
    y += 2 + kPad;

    // #libList: buttons 6 px apart, wrapping when they no longer fit.
    std::vector<FlexItem> list;
    for (auto& b : buttons_) list.push_back({b.get(), b->naturalWidth(), double(b->naturalHeight())});
    if (!list.empty()) y += flexRow(x, y, contentW, list, kListGap);

    // #libHint: inline, in 16 px lines (empty library only).
    empty_.setVisible(lib_.empty());
    if (lib_.empty()) {
        empty_.setBox({x, y, contentW, kHintLine});
        const double eh = kHintLine * empty_.lineCount();
        empty_.setBox({x, y, std::min(empty_.naturalWidth(), contentW), eh});
        y += eh;
    }

    // Height: content, padding of 8, border and margin (6); the width stays kWidth.
    const double bottom = y + kPad;
    const double wh = bottom + 6 - oy;
    if (std::abs(wh - box().h) > 0.01 || std::abs(kWidth - box().w) > 0.01) place({ox, oy, double(kWidth), wh});
    repaint();
}

std::optional<Rect> PatchesWindow::rectOf(std::string_view id) const {
    if (!isOpen()) return std::nullopt;
    if (id == "libWin") return bounds();
    if (id.substr(0, 8) == "libPatch") {
        const std::string num(id.substr(8));
        const int k = num.empty() || num.size() > 3 ? -1 : std::atoi(num.c_str());
        if (k >= 0 && k < int(buttons_.size())) return buttons_[size_t(k)]->bounds();
        return std::nullopt;
    }
    struct Named {
        std::string_view id;
        const gemui::Widget* w;
    };
    const Named named[] = {{"libSave", &save_}, {"libImport", &import_}, {"libExport", &export_}, {"libHint", &empty_}};
    for (const Named& n : named)
        if (id == n.id) return n.w->visible() ? std::optional<Rect>(n.w->bounds()) : std::nullopt;
    return std::nullopt;
}

}  // namespace stym::pluginui
