// STYM2149 — pluginui: Song window (transport, Add, banks, blocks, menus, drag and drop, keyboard)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Behaviours taken from the prototype functions quoted in comments (openSongWin, closeSongWin, refreshSong, songKey,
// buildSongPat, songAdd, songStep, gestures of #songList).
//
// The transport, banks and patterns are those of the Step Sequencer (SeqWindow), shown again here. Each change of the
// Song first pushes the whole Song as one undo step (pushUndo, UndoEntry::Kind::Song), most of them through edit().
#include "pluginui/SongWindow.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <memory>

#include "gemui/Alert.h"
#include "pluginui/IconData.h"
#include "pluginui/MainWindow.h"

namespace stym::pluginui {

using gemui::Box;
using gemui::Button;
using gemui::Key;
using gemui::MenuItem;
using gemui::Rect;

namespace {

int mod(int a, int n) noexcept { return n > 0 ? ((a % n) + n) % n : 0; }

void smallButton(Button& b, const gemui::Bitmap& icon, const char* tip) {
    b.setIcon(&icon);
    b.setPadding(2, 6);  // .tbar .btn
    b.setMinWidth(26);
    b.setTip(tip);
}

// Label of a block (songLabel): own tempo after a quaver sign.
std::string chipLabel(const ymseq::SongEntry& e) {
    return ymseq::songLabel(e) + (e.bpm ? " ♪" + std::to_string(e.bpm) : "");
}

// Browser parseInt: leading spaces, sign, digits; nothing readable: nullopt.
std::optional<int> parseInt(const std::string& v) {
    size_t i = 0;
    while (i < v.size() && std::isspace(static_cast<unsigned char>(v[i]))) ++i;
    bool neg = false;
    if (i < v.size() && (v[i] == '+' || v[i] == '-')) neg = v[i++] == '-';
    long n = 0;
    size_t digits = 0;
    for (; i < v.size() && std::isdigit(static_cast<unsigned char>(v[i])); ++i, ++digits)
        n = std::min(n * 10 + (v[i] - '0'), 1000000L);
    if (!digits) return std::nullopt;
    return int(neg ? -n : n);
}

bool blank(const std::string& v) {
    return std::all_of(v.begin(), v.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; });
}

}  // namespace

// ------------------------------------------------------------------------------------------- blocks

double SongChip::opacity() const noexcept {
    // .dragsrc: the grabbed block, once the drag has started.
    return w_.dragMoved_ && w_.dragFrom_ == index_ ? 0.45 : Button::opacity();
}

void SongChip::mouseDown(const gemui::MouseEvent& e) {
    Button::mouseDown(e);
    if (e.button == gemui::MouseButton::Left) w_.dragStart(index_, e.pos);
}

void SongChip::mouseDrag(const gemui::MouseEvent& e) {
    w_.dragMove(e.pos);
}

void SongChip::mouseUp(const gemui::MouseEvent& e) {
    // pointerup of the page first, then the click of the block (no selection after a drag). The drop rebuilds the list:
    // the blocks are never destroyed, only hidden, so this block stays valid.
    const bool moved = w_.dragEnd();
    if (moved) {
        gemui::MouseEvent away = e;
        away.pos = {bounds().x - 1, bounds().y - 1};  // released "elsewhere": no click
        Button::mouseUp(away);
        return;
    }
    ShiftButton::mouseUp(e);
}

void SongChip::mouseCancel() {
    w_.dragCancel();
    ShiftButton::mouseCancel();
}

// ------------------------------------------------------------------------------------------- construction

SongWindow::SongWindow(MainWindow& main) : m_(main) {
    build();
    wire();
    place({double(kX), double(kY), 670, 169});
}

const ymseq::Song& SongWindow::song() const noexcept { return m_.extras_.seq.song; }
EditorSession& SongWindow::ses() const noexcept { return m_.host_.session(); }

void SongWindow::build() {
    // Prototype ids of the sliders and buttons that MIDI learn can learn (D-146).
    for (const auto& [w, id] : std::initializer_list<std::pair<gemui::Widget*, const char*>>{
            {&add_, "songAdd"}, {&play_, "trPlay"}, {&rec_, "trRec"}, {&loop_, "trLoop"}})
        w->setId(id);
    setTitle("SONG");
    for (Button* b : {&play_, &rec_, &loop_, &start_, &prev_, &next_}) add(*b);
    add(tbarSep_);
    smallButton(play_, icons::kSmallPlay, "Play");
    smallButton(rec_, icons::kSmallRec, "Record while playing");
    smallButton(loop_, icons::kSmallLoop, "Loop");
    smallButton(start_, icons::kSmallStart, "Play from start");
    smallButton(prev_, icons::kSmallPrev, "Previous song entry");
    smallButton(next_, icons::kSmallNext, "Next song entry");
    add(add_);
    add_.setTip("Append the current pattern to the song (again = one more repeat, Shift = new entry)");
    add(hint_);
    hint_.setText("Add appends the current pattern (again = +1 repeat, Shift+Add = new entry).\n"
                  "Click selects (Shift = multi). Right click for actions.");
    hint_.setLineHeight(16.8);  // #songHint: line-height 1.4
    add(labBank_);
    add(labPattern_);
    for (int i = 0; i < 4; ++i) {
        Button& b = banks_[size_t(i)];
        const std::string n(1, char('A' + i));
        b.setText(n);
        b.setPx(15);
        b.setPadding(6, 11);
        b.setMinWidth(30);
        b.setTip("Bank " + n + ": eight patterns (" + n + "1 to " + n + "8)");
        add(b);
    }
    for (int i = 0; i < 8; ++i) {
        Button& b = pats_[size_t(i)];
        b.setText(std::to_string(i + 1));
        b.setPx(15);
        b.setPadding(6, 11);
        b.setMinWidth(30);
        b.setTip("Pattern " + std::to_string(i + 1) +
                 " of the current bank\nRight click: copy, cut, paste, or append it to the song.");
        add(b);
    }
    add(sep_);
    add(keysHint_);
    keysHint_.setText("Keys: Left/Right move, Ins/A add, Del remove, D duplicate, +/- repeats, X/C/V "
                      "cut/copy/paste, Home/End jump");
}

void SongWindow::wire() {
    onClose = [this] {
        // closeSongWin: closing the window leaves Song mode.
        dragFrom_ = -1;
        if (m_.extras_.global.songMode)
            m_.seq_.edit([](plugin::NonParamState& s) { s.global.songMode = false; });
        m_.syncWindowButtons();
    };
    play_.onClick = [this] { m_.seq_.togglePlay(); };
    rec_.onClick = [this] { m_.seq_.toggleRecord(); };
    loop_.onClick = [this] { m_.seq_.toggleLoop(); };
    start_.onClick = [this] { m_.seq_.transportStart(); };
    prev_.onClick = [this] { m_.songStep(-1); };
    next_.onClick = [this] { m_.songStep(1); };
    add_.onClick = [this] {
        // songAdd: same pattern as the last block → one more repeat; Shift: a separate block.
        const int pat = m_.extras_.seq.current;
        const bool separate = add_.shift();
        edit([pat, separate](ymseq::Song& s) { s.add(pat, separate); });
    };
    for (int i = 0; i < 4; ++i) banks_[size_t(i)].onClick = [this, i] { m_.seq_.pickBank(i); };
    for (int i = 0; i < 8; ++i) {
        pats_[size_t(i)].onClick = [this, i] { m_.seq_.pickPattern(i); };
        pats_[size_t(i)].onContext = [this, i](gemui::Point) {
            const int idx = m_.extras_.seq.viewBank * ymseq::kBankSize + i;
            std::vector<MenuItem> items(5);
            items[0].label = "Copy";
            items[0].pick = [this, idx] { m_.seq_.patCopy(idx); };
            items[1].label = "Cut";
            items[1].pick = [this, idx] { m_.seq_.patCut(idx); };
            items[2].label = "Paste";
            items[2].pick = [this, idx] { m_.seq_.patPaste(idx); };
            items[3] = MenuItem::separatorItem();
            items[4].label = "Add to song";
            items[4].pick = [this, idx] { edit([idx](ymseq::Song& s) { s.append(idx); }); };
            gemui::MenuList::open(m_.desk_, std::move(items), -1, pats_[size_t(i)].box(), false);
        };
    }
}

// ------------------------------------------------------------------------------------------- state

// pushSongUndo: the Song as it is now. The history drops it when it equals the last step, and Undo skips a step equal
// to the current Song: a push followed by no change only empties Redo.
void SongWindow::pushUndo() {
    UndoEntry e;
    e.kind = UndoEntry::Kind::Song;
    e.song = song();
    ses().undo.push(std::move(e));
}

void SongWindow::edit(const std::function<void(ymseq::Song&)>& fn) {
    pushUndo();
    m_.seq_.edit([&fn](plugin::NonParamState& s) { fn(s.seq.song); });
}

void SongWindow::applyUndo(const UndoEntry& e) {
    // applyUndoEntry (song): the Song comes back, the selection is emptied.
    const ymseq::Song s = e.song;
    sel_.clear();
    m_.seq_.edit([&s](plugin::NonParamState& st) { st.seq.song = s; });
}

void SongWindow::show() {
    // openSongWin: Song mode switches on, the window comes to the front.
    if (!m_.extras_.global.songMode) m_.seq_.edit([](plugin::NonParamState& s) { s.global.songMode = true; });
    open();
    refresh();
    m_.syncWindowButtons();
}

void SongWindow::syncMode() {
    // The state read back (host project) decides: Song mode on = window open.
    const bool on = m_.extras_.global.songMode;
    if (on && !isOpen()) {
        open();
        refresh();
    } else if (!on && isOpen()) {
        close();
    }
}

void SongWindow::refresh() {
    // refreshSong: keyboard cursor always inside the Song, selection bounded.
    const int n = song().size();
    cur_ = n ? std::clamp(cur_, 0, n - 1) : 0;
    sel_ = song().normalize(sel_);
    if (dragFrom_ >= n) dragFrom_ = -1;
    if (!isOpen()) return;
    rebuildChips();
    refreshTransport();
    refreshPatterns();
    layout();
}

void SongWindow::refreshEngineStatus(const plugin::EngineStatus& t) {
    // The block being played is lit (window open or not, the state is kept for the next refresh).
    if (t.seq.songPos == songPos_ && t.seq.playing == playing_) return;
    songPos_ = t.seq.songPos;
    playing_ = t.seq.playing;
    if (!isOpen()) return;
    for (size_t i = 0; i < chips_.size(); ++i) chips_[i]->setOn(playing_ && int(i) == songPos_);
}

void SongWindow::rebuildChips() {
    // One button per block; the extra buttons are hidden (never destroyed: a block may be in the middle of a gesture).
    const auto& entries = song().entries;
    while (chips_.size() < entries.size()) {
        const int i = int(chips_.size());
        auto c = std::make_unique<SongChip>(*this, i);
        c->onClick = [this, i, chip = c.get()] { clickChip(i, chip->shift()); };
        c->onContext = [this, i](gemui::Point) { chipMenu(i); };
        add(*c);
        chips_.push_back(std::move(c));
    }
    for (size_t i = 0; i < chips_.size(); ++i) {
        SongChip& c = *chips_[i];
        const bool shown = i < entries.size();
        c.setVisible(shown);
        if (!shown) continue;
        c.setText(chipLabel(entries[i]));
        c.setOn(playing_ && int(i) == songPos_);
    }
}

void SongWindow::refreshTransport() {
    // refreshTbars: like the bar of the Step Sequencer; the Song arrows exist only with blocks.
    if (!isOpen()) return;
    const ymengine::GlobalParams& g = m_.extras_.global;
    const bool host = m_.seqClock() == ymengine::SeqClock::Host;
    const bool playing = m_.seq_.playing();
    play_.setIcon(playing ? &icons::kSmallStop : &icons::kSmallPlay);
    play_.setOn(playing);
    play_.setEnabled(!host);
    play_.setTip(host ? "Host sync: the host transport starts and stops the sequencer" : playing ? "Stop" : "Play");
    rec_.setOn(ses().record);
    loop_.setOn(g.loop);
    const bool nav = !song().empty();
    for (Button* b : {&start_, &prev_, &next_}) {
        b->setVisible(nav);
        b->setEnabled(nav && !host);
    }
    tbarSep_.setVisible(nav);
}

void SongWindow::refreshPatterns() {
    // refreshPatBtns: bank shown, bank of the current pattern (dotted), current and pending pattern.
    const ymseq::Project& p = m_.extras_.seq;
    for (int i = 0; i < 4; ++i) {
        banks_[size_t(i)].setOn(i == p.viewBank);
        banks_[size_t(i)].setPending(p.current / ymseq::kBankSize == i && i != p.viewBank);
    }
    for (int i = 0; i < 8; ++i) {
        const int idx = p.viewBank * ymseq::kBankSize + i;
        pats_[size_t(i)].setOn(idx == p.current);
        pats_[size_t(i)].setPending(idx == p.pending);
    }
}

// ------------------------------------------------------------------------------------------- layout

void SongWindow::layout() {
    const double ox = box().x, oy = box().y;
    const double x = ox + 14;  // .mixbody content (border 2, margin 4, margin 8)
    double y = oy + 40;        // title bar (6 + 26), margin of .mixbody (8)

    // Row 1 (flex, gap 10, centred): transport, empty span.sep, Add, note (margin-top 6, two lines).
    const bool nav = !song().empty();
    const double tbarW = nav ? 6 * 27 + 6 * 3 + 2 * 4 + 2 : 3 * 27 + 2 * 3;  // 3 px gap, 2 px rule with margins of 4
    const double hintW = hint_.naturalWidth(), hintH = hint_.lineHeight() * 2;
    const double addW = add_.naturalWidth(), addH = add_.naturalHeight();
    const double h1 = std::max({19.0, addH, 6 + hintH});
    {
        double bx = x;
        const double by = y + (h1 - 19) / 2;
        for (Button* b : {&play_, &rec_, &loop_}) {
            b->setBox({bx, by, 27, 19});
            bx += 27 + 3;
        }
        if (nav) {
            tbarSep_.setBox({bx + 4, by, 2, 19});  // .tbar .sep: 2 px, 4 px margins
            bx += 4 + 2 + 4 + 3;
            for (Button* b : {&start_, &prev_, &next_}) {
                b->setBox({bx, by, 27, 19});
                bx += 27 + 3;
            }
        }
    }
    const double addX = x + tbarW + 10 + 10;  // gap, the empty span.sep, gap
    add_.setBox({addX, y + (h1 - addH) / 2, addW, addH});
    const double hintX = addX + addW + 10;
    hint_.setBox({hintX, y + (h1 - 6 - hintH) / 2 + 6, hintW, hintH});
    double contentW = hintX + hintW - x;
    y += h1;

    // Row 2 (margin-top 8, gap 6): banks and patterns.
    y += 8;
    const double h2 = banks_[0].naturalHeight();
    double bx = x;
    labBank_.setBox({bx, y + (h2 - 16) / 2, labBank_.naturalWidth(), 16});
    bx += labBank_.naturalWidth() + 6;
    for (Button& b : banks_) {
        b.setBox({bx, y, b.naturalWidth(), h2});
        bx += b.naturalWidth() + 6;
    }
    bx += 8;  // label margin-left 8
    labPattern_.setBox({bx, y + (h2 - 16) / 2, labPattern_.naturalWidth(), 16});
    bx += labPattern_.naturalWidth() + 6;
    for (size_t i = 0; i < pats_.size(); ++i) {
        pats_[i].setBox({bx, y, pats_[i].naturalWidth(), h2});
        bx += pats_[i].naturalWidth() + (i + 1 < pats_.size() ? 6 : 0);
    }
    contentW = std::max(contentW, bx - x);
    y += h2;

    // Rule (margin 8px -8px), placed when the width is known.
    y += 8;
    const double sepY = y;
    y += 2 + 8;

    // #songList: 12 columns at the width of their longest block, rows at the height of their tallest block, 2 px gap;
    // the blocks fill their cell. Block i is in column i % 12 of row i / 12.
    const int n = song().size();
    std::array<double, kColumns> colW{};
    std::vector<double> rowH(size_t((n + kColumns - 1) / kColumns), 0);
    for (int i = 0; i < n; ++i) {
        const SongChip& c = *chips_[size_t(i)];
        colW[size_t(i % kColumns)] = std::max(colW[size_t(i % kColumns)], c.naturalWidth());
        rowH[size_t(i / kColumns)] = std::max(rowH[size_t(i / kColumns)], double(c.naturalHeight()));
    }
    double listW = 2 * (kColumns - 1);
    for (double w : colW) listW += w;
    contentW = std::max(contentW, listW);
    double ry = y;
    for (size_t r = 0; r < rowH.size(); ++r) {
        double cx = x;
        for (int k = 0; k < kColumns; ++k) {
            const int i = int(r) * kColumns + k;
            if (i < n) chips_[size_t(i)]->setBox({cx, ry, colW[size_t(k)], rowH[r]});
            cx += colW[size_t(k)] + 2;
        }
        ry += rowH[r] + (r + 1 < rowH.size() ? 2 : 0);
    }
    y = ry;

    // Keys (div.hint: margin-top 6, one 12 px line).
    const double keysW = keysHint_.naturalWidth();
    contentW = std::max(contentW, keysW);
    keysHint_.setBox({x, y + 6, contentW, 12});
    y += 6 + 12;

    // Width of the window: that of its longest content (width:max-content), 340 px at least.
    const double w = std::max(double(kMinW), contentW + 2 * 14);
    sep_.setBox({ox + 6, sepY, w - 12, 2});
    const double h = y + 8 + 4 + 2 - oy;  // margin of .mixbody (8), margin and border of .gwin (4 + 2)
    if (std::abs(h - box().h) > 0.01 || std::abs(w - box().w) > 0.01) place({ox, oy, w, h});
    repaint();
}

void SongWindow::paintOver(gemui::Painter& p) {
    // Outlines above the neighbouring blocks, like in the browser.
    const int n = song().size();
    auto frame = [&](Rect r, int t) {
        p.fill({r.x, r.y, r.w, t}, gemui::colors::kBlack);
        p.fill({r.x, r.bottom() - t, r.w, t}, gemui::colors::kBlack);
        p.fill({r.x, r.y + t, t, r.h - 2 * t}, gemui::colors::kBlack);
        p.fill({r.right() - t, r.y + t, t, r.h - 2 * t}, gemui::colors::kBlack);
    };
    auto grow = [](Rect r, int d) { return Rect{r.x - d, r.y - d, r.w + 2 * d, r.h + 2 * d}; };
    // .cur: 4 px inside the block.
    if (n > 0 && cur_ < n) frame(chips_[size_t(cur_)]->bounds(), 4);
    // .ssel: 4 px at 1 px from the block.
    for (int i : sel_)
        if (i < n) frame(grow(chips_[size_t(i)]->bounds(), 5), 4);
    // .drop: 2 px dashes at 1 px from the block passed over during a drag.
    if (dragMoved_ && dragFrom_ >= 0 && dragDrop_ >= 0 && dragDrop_ < n && dragDrop_ != dragFrom_) {
        const Rect r = grow(chips_[size_t(dragDrop_)]->bounds(), 3);
        for (int dx = 0; dx < r.w; dx += 12) {
            const int len = std::min(6, r.w - dx);
            p.fill({r.x + dx, r.y, len, 2}, gemui::colors::kBlack);
            p.fill({r.x + dx, r.bottom() - 2, len, 2}, gemui::colors::kBlack);
        }
        for (int dy = 0; dy < r.h; dy += 12) {
            const int len = std::min(6, r.h - dy);
            p.fill({r.x, r.y + dy, 2, len}, gemui::colors::kBlack);
            p.fill({r.right() - 2, r.y + dy, 2, len}, gemui::colors::kBlack);
        }
    }
}

std::optional<Rect> SongWindow::rectOf(std::string_view id) const {
    if (!isOpen()) return std::nullopt;
    const std::pair<const char*, const gemui::Widget*> ids[] = {
        {"songWin", this},          {"songAdd", &add_},         {"songTrPlay", &play_}, {"songTrRec", &rec_},
        {"songTrLoop", &loop_},     {"songTrStart", &start_},   {"songTrPrev", &prev_}, {"songTrNext", &next_},
        {"songHint", &hint_},
    };
    for (const auto& [name, w] : ids)
        if (id == name) return w->visible() ? std::optional<Rect>(w->bounds()) : std::nullopt;
    if (id.substr(0, 8) == "songChip") {
        const int i = std::atoi(std::string(id.substr(8)).c_str());
        if (i >= 0 && i < song().size()) return chips_[size_t(i)]->bounds();
    }
    return std::nullopt;
}

// ------------------------------------------------------------------------------------------- block gestures

void SongWindow::setSelection(ymseq::SongSel sel, int cursor) {
    sel_ = std::move(sel);
    cur_ = cursor;
    refresh();
}

void SongWindow::selectOnly(int i) {
    sel_ = {i};
}

void SongWindow::clickChip(int i, bool shift) {
    // Click: selects; Shift: adds or removes the block; the keyboard cursor follows it.
    if (i >= song().size()) return;
    if (shift) {
        const auto it = std::find(sel_.begin(), sel_.end(), i);
        if (it != sel_.end()) sel_.erase(it);
        else sel_ = song().normalize([&] {
            auto s = sel_;
            s.push_back(i);
            return s;
        }());
    } else {
        selectOnly(i);
    }
    cur_ = i;
    repaint();
}

void SongWindow::chipMenu(int i) {
    if (i >= song().size()) return;
    // Right click outside the selection: the block alone.
    if (std::find(sel_.begin(), sel_.end(), i) == sel_.end()) {
        selectOnly(i);
        repaint();
    }
    const ymseq::SongSel S = song().normalize(sel_);
    const std::string nSel = S.size() > 1 ? " (" + std::to_string(S.size()) + ")" : "";
    std::vector<MenuItem> items;
    auto item = [&](std::string label, std::function<void()> fn) {
        MenuItem m;
        m.label = std::move(label);
        m.pick = std::move(fn);
        items.push_back(std::move(m));
    };
    // One undo step per action (pushSongUndo, de-duplicated for Copy).
    // Jump (D-158): the number of jumps before continuing.
    for (int rep : ymseq::kSongRepeats)
        item("Repeat x" + std::to_string(rep), [this, S, rep] {
            edit([&](ymseq::Song& s) { s.setRepeat(S, rep); });
        });
    items.push_back(MenuItem::separatorItem());
    item("Duplicate" + nSel, [this, S] {
        ymseq::SongSel out;
        edit([&](ymseq::Song& s) { out = s.duplicate(S); });
        sel_ = out;
        refresh();
    });
    item("Cut" + nSel, [this, S] {
        ses().songClip = song().copy(S);
        edit([&](ymseq::Song& s) { s.erase(S); });
        sel_.clear();
        refresh();
    });
    item("Copy" + nSel, [this, S] {
        pushUndo();
        ses().songClip = song().copy(S);
    });
    item("Paste", [this, i] {
        if (!ses().songClip || ses().songClip->empty()) {
            pushUndo();
            return;
        }
        const std::vector<ymseq::SongEntry> clip = *ses().songClip;
        ymseq::SongSel out;
        edit([&](ymseq::Song& s) { out = s.pasteAfter(i, clip); });
        sel_ = out;
        refresh();
    });
    items.push_back(MenuItem::separatorItem());
    item("Move left", [this, S] {
        ymseq::SongSel s2 = S;
        bool moved = false;
        edit([&](ymseq::Song& s) { moved = s.moveLeft(s2); });
        if (moved) sel_ = s2;
        refresh();
    });
    item("Move right", [this, S] {
        ymseq::SongSel s2 = S;
        bool moved = false;
        edit([&](ymseq::Song& s) { moved = s.moveRight(s2); });
        if (moved) sel_ = s2;
        refresh();
    });
    item("Remove" + nSel, [this, S] {
        edit([&](ymseq::Song& s) { s.erase(S); });
        sel_.clear();
        refresh();
    });
    items.push_back(MenuItem::separatorItem());
    auto any = [&](auto pred) {
        return std::any_of(S.begin(), S.end(), [&](int x) { return pred(song().entries[size_t(x)]); });
    };
    const bool plays = any([](const ymseq::SongEntry& e) { return e.kind != ymseq::SongKind::Loop; });
    const bool rests = any([](const ymseq::SongEntry& e) {
        return e.kind == ymseq::SongKind::Rest || e.kind == ymseq::SongKind::NoteOff;
    });
    const bool loops = any([](const ymseq::SongEntry& e) { return e.kind == ymseq::SongKind::Loop; });
    auto unless = [&](bool ok, const char* why) {
        if (ok) return;
        items.back().disabled = true;
        items.back().why = why;
    };
    item("Tempo...", [this, S] { tempoPrompt(S); });
    unless(plays, "A loop plays nothing: it has no tempo");
    // Extended Song (D-158): special entries, their settings, loop point.
    items.push_back(MenuItem::separatorItem());
    item("Insert rest", [this, i] { insertSpecial(i, ymseq::SongEntry::rest(ymseq::SongKind::Rest)); });
    item("Insert note-off", [this, i] { insertSpecial(i, ymseq::SongEntry::rest(ymseq::SongKind::NoteOff)); });
    item("Insert loop", [this, i] {
        // The jump replays the blocks from the previous jump up to this one.
        if (i >= song().size()) return;
        int back = 1;
        while (i - back >= 0 && song().entries[size_t(i - back)].kind != ymseq::SongKind::Loop) ++back;
        insertSpecial(i, ymseq::SongEntry::loop(std::min(back, ymseq::kSongLoopMax)));
    });
    item("Length...", [this, S] { lengthPrompt(S); });
    unless(rests, "Only rest and note-off entries have a length");
    item("Loop back...", [this, S] { loopPrompt(S); });
    unless(loops, "Only loop entries jump back");
    const bool restart = song().entries[size_t(i)].restart;
    item(restart ? "Restart from start" : "Restart here", [this, i] {
        edit([i](ymseq::Song& s) { s.toggleRestart(i); });
        refresh();
    });
    if (song().entries[size_t(i)].kind == ymseq::SongKind::Loop) {
        items.back().disabled = true;
        items.back().why = "The song cannot restart on a loop";
    }
    // Ctrl+Z, Ctrl+Y and a state set by the host leave the menu open: its block indices would then name other blocks,
    // so the entries act only on the Song the menu was opened on.
    gemui::pickOnlyWhile(items, [this, snap = std::make_shared<const ymseq::Song>(song())] { return song() == *snap; });
    // refreshSong rebuilds the list: the menu anchors on the updated block.
    gemui::MenuList::open(m_.desk_, std::move(items), -1, chips_[size_t(i)]->box(), false);
}

void SongWindow::tempoPrompt(const ymseq::SongSel& sel) {
    // gemPrompt "Tempo for this block": empty (or unreadable) = tempo of the song, otherwise 20 to 300 BPM. The
    // selection comes from a menu: the Song may have shrunk since it opened (host state, undo).
    const ymseq::SongSel S = song().normalize(sel);
    if (S.empty()) return;
    const int cur0 = song().entries[size_t(S[0])].bpm;
    gemui::Alert::Spec spec;
    spec.lines = {{"Tempo for this block (blank = song tempo):"}};
    spec.promptButtons();
    spec.inputText = cur0 ? std::to_string(cur0) : "";
    spec.inputChars = 30;
    // The prompt writes only into the Song it was opened on (a state set by the host while it is open).
    gemui::Alert::open(m_.desk_, std::move(spec), [this, S, snap = song()](int button, const std::string& text) {
        if (button != gemui::Alert::kPromptOk || song() != snap) return;
        const std::optional<int> n = blank(text) ? std::nullopt : parseInt(text);
        edit([&](ymseq::Song& s) { s.setTempo(S, n); });
    });
}

void SongWindow::insertSpecial(int i, const ymseq::SongEntry& e) {
    int at = i;
    edit([&](ymseq::Song& s) { at = s.insertEntryAfter(i, e); });
    cur_ = at;
    selectOnly(at);
    refresh();
}

void SongWindow::lengthPrompt(const ymseq::SongSel& sel) {
    // Length of the Rest and Note-off entries: 1 to 256 steps; empty or unreadable: nothing changes.
    const ymseq::SongSel S = song().normalize(sel);  // as in tempoPrompt
    const auto it = std::find_if(S.begin(), S.end(), [this](int x) {
        const ymseq::SongKind k = song().entries[size_t(x)].kind;
        return k == ymseq::SongKind::Rest || k == ymseq::SongKind::NoteOff;
    });
    if (it == S.end()) return;
    gemui::Alert::Spec spec;
    spec.lines = {{"Length of this entry, in steps (1-256):"}};
    spec.promptButtons();
    spec.inputText = std::to_string(song().entries[size_t(*it)].arg);
    spec.inputChars = 30;
    gemui::Alert::open(m_.desk_, std::move(spec), [this, S, snap = song()](int button, const std::string& text) {
        const std::optional<int> n = parseInt(text);
        if (button != gemui::Alert::kPromptOk || !n || song() != snap) return;  // as in tempoPrompt
        edit([&](ymseq::Song& s) { s.setLength(S, *n); });
    });
}

void SongWindow::loopPrompt(const ymseq::SongSel& sel) {
    // Entries replayed by the jump, those that precede it: 1 to 255; the number of jumps is in Repeat.
    const ymseq::SongSel S = song().normalize(sel);  // as in tempoPrompt
    const auto it = std::find_if(S.begin(), S.end(),
                                 [this](int x) { return song().entries[size_t(x)].kind == ymseq::SongKind::Loop; });
    if (it == S.end()) return;
    gemui::Alert::Spec spec;
    spec.lines = {{"Entries to play again before this loop (1-255):"}};
    spec.promptButtons();
    spec.inputText = std::to_string(song().entries[size_t(*it)].arg);
    spec.inputChars = 30;
    gemui::Alert::open(m_.desk_, std::move(spec), [this, S, snap = song()](int button, const std::string& text) {
        const std::optional<int> n = parseInt(text);
        if (button != gemui::Alert::kPromptOk || !n || song() != snap) return;  // as in tempoPrompt
        edit([&](ymseq::Song& s) { s.setLoopBack(S, *n); });
    });
}

// Drag of a block: dragStart on the press, dragMove while the button is held, dragEnd on the release (before the click
// of the block, which a drag cancels).
void SongWindow::dragStart(int i, gemui::Point at) {
    dragFrom_ = i;
    dragDrop_ = -1;
    dragMoved_ = false;
    dragAt_ = at;
}

int SongWindow::chipAt(gemui::Point p) const {
    for (int i = 0; i < song().size(); ++i)
        if (chips_[size_t(i)]->bounds().contains(p)) return i;
    return -1;
}

void SongWindow::dragMove(gemui::Point at) {
    if (dragFrom_ < 0) return;
    if (!dragMoved_) {
        // The drag starts beyond 5 px (Manhattan distance).
        if (std::abs(at.x - dragAt_.x) + std::abs(at.y - dragAt_.y) <= 5) return;
        dragMoved_ = true;
        repaint();
    }
    // pointerover: the block passed over becomes the target; the grabbed block and empty space do not change it.
    const int i = chipAt(at);
    if (i >= 0 && i != dragFrom_ && i != dragDrop_) {
        dragDrop_ = i;
        repaint();
    }
}

bool SongWindow::dragEnd() {
    const bool moved = dragMoved_;
    const int from = dragFrom_, drop = dragDrop_;
    dragFrom_ = dragDrop_ = -1;
    dragMoved_ = false;
    repaint();
    if (moved && from >= 0 && drop >= 0 && drop != from) {
        // The block (or the whole selection it belongs to) moves to the place of the target block.
        ymseq::Song next = song();
        ymseq::SongSel s = sel_;
        if (next.dragMove(s, from, drop)) {
            pushUndo();
            m_.seq_.edit([&next](plugin::NonParamState& st) { st.seq.song = next; });
            sel_ = s;
            refresh();
        }
    }
    return moved;
}

void SongWindow::dragCancel() {
    dragFrom_ = dragDrop_ = -1;
    dragMoved_ = false;
    repaint();
}

// ------------------------------------------------------------------------------------------- keyboard

void SongWindow::jump(int i) {
    // songStep, Enter: this block becomes the played block, its pattern the current pattern (the player takes it at
    // once, keeping the step).
    const int n = song().size();
    if (!n) return;
    i = std::clamp(i, 0, n - 1);
    songPos_ = i;
    // Special entry (D-158): no pattern to show; the player goes through a jump.
    if (const ymseq::SongEntry& e = song().entries[size_t(i)]; !e.special()) {
        const int pat = e.pat;
        m_.seq_.edit([pat](plugin::NonParamState& s) { s.seq.applied(pat); });
    }
    m_.host_.seqCommand(Host::SeqCommand::SongJump, i);
    refresh();
}

bool SongWindow::songKey(const gemui::KeyEvent& e) {
    // songKey: only when the window is the frontmost, without Ctrl or Alt.
    if (!isOpen() || e.mods.ctrl || e.mods.alt) return false;
    if (!parent() || gemui::FloatWindow::topmost(*parent()) != this) return false;
    const int n = song().size();
    // Letter of the key: its code when the host gives one, else the character typed, in capitals.
    const int c = e.code ? e.code : (e.ch < 128 ? std::toupper(int(e.ch)) : 0);
    const bool chr = e.key == Key::Character;
    auto sel = [this] { selectOnly(cur_); };
    auto move = [&](int d) {
        cur_ = n ? mod(cur_ + d, n) : 0;
        sel();
        refresh();
    };
    if (e.key == Key::Right) {
        move(1);
        return true;
    }
    if (e.key == Key::Left) {
        move(-1);
        return true;
    }
    if (e.key == Key::Home) {
        cur_ = 0;
        sel();
        refresh();
        return true;
    }
    if (e.key == Key::End) {
        cur_ = std::max(0, n - 1);
        sel();
        refresh();
        return true;
    }
    if (e.key == Key::Insert || (chr && c == 'A')) {  // the current pattern after the cursor
        const int pat = m_.extras_.seq.current;
        int at = 0;
        edit([&](ymseq::Song& s) { at = s.insertAfter(n ? cur_ : -1, pat); });
        cur_ = at;
        sel();
        refresh();
        return true;
    }
    if (e.key == Key::Delete || e.key == Key::Backspace) {
        if (!n) return true;
        const ymseq::SongSel S = sel_;
        int next = cur_;
        edit([&](ymseq::Song& s) { next = s.remove(S, cur_); });
        cur_ = next;
        sel_.clear();
        refresh();
        return true;
    }
    if (chr && c == 'D') {  // duplicate
        if (!n) return true;
        int next = cur_;
        edit([&](ymseq::Song& s) { next = s.duplicateAt(cur_); });
        cur_ = next;
        sel();
        refresh();
        return true;
    }
    // Keys = and - (Equal, Minus, and those of the numeric keypad), given by their character.
    if (chr && (e.ch == U'=' || e.ch == U'+')) {
        if (!n) return true;
        edit([this](ymseq::Song& s) { s.incRepeat(cur_); });
        return true;
    }
    if (chr && (e.ch == U'-' || e.ch == U'_')) {
        if (!n) return true;
        edit([this](ymseq::Song& s) { s.decRepeat(cur_); });
        return true;
    }
    if (chr && (c == 'X' || c == 'C')) {  // cut, copy: the selection, otherwise the block under the cursor
        if (!n) return true;
        ymseq::SongSel L = song().normalize(sel_);
        if (L.empty()) L = {cur_};
        ses().songClip = song().copy(L);
        if (c == 'X') {
            edit([&L](ymseq::Song& s) { s.erase(L); });
            cur_ = std::max(0, std::min(cur_, song().size() - 1));
            sel_.clear();
        }
        refresh();
        return true;
    }
    if (chr && c == 'V') {  // paste after the cursor
        if (!ses().songClip || ses().songClip->empty()) return true;
        const std::vector<ymseq::SongEntry> clip = *ses().songClip;
        ymseq::SongSel out;
        edit([&](ymseq::Song& s) { out = s.pasteAfter(n ? cur_ : -1, clip); });
        cur_ = out.empty() ? cur_ : out.front();
        sel();
        refresh();
        return true;
    }
    if (e.key == Key::Enter) {  // jump to this block
        if (!n) return true;
        jump(cur_);
        return true;
    }
    return false;
}

}  // namespace stym::pluginui
