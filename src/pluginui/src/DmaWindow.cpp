// STYM2149 — pluginui: DMA window (sample bank of the STE DMA sound)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "pluginui/DmaWindow.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "gemui/Alert.h"
#include "plugincore/Digidrum.h"
#include "plugincore/DmaBanks.h"
#include "plugincore/DmaSample.h"
#include "plugincore/Parameters.h"
#include "pluginui/Layout.h"
#include "pluginui/MainWindow.h"
#include "ymengine/Params.h"
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
    useSte_.setTip("Set Computer to STE (SYSTEM panel, or Options > Settings)");
    play_.setId("dmaPlay");
    for (int k = 0; k < ymengine::kDmaSlots; ++k) {
        Button& b = slots_[size_t(k)];
        b.setText(std::to_string(k + 1));
        b.setPadding(2, 6);
    }
    load_.setTip("Load an audio file (resampled to the rate) or raw 8-bit bytes (.raw, .spl, .sam) into this slot");
    play_.setTip("Play this sample at its rate, from Start to End: once, or with Loop on in a loop until Play or "
                 "Loop is clicked again");
    clear_.setTip("Empty this slot (name, sample, Start and End)");
    name_.setTip("Name of this sample, 16 characters (taken from the file loaded)");
    name_.setId("dmaName");
    start_.setTip("First frame played (and of the loop)");
    end_.setTip("End of the part played (the frame after its last one)");
    start_.setId("dmaStart");
    end_.setId("dmaEnd");
    for (gemui::ValueField* f : {&startv_, &endv_}) f->setPx(14);
    for (int k = 0; k < ymengine::kDmaRates; ++k) {
        Button& b = rates_[size_t(k)];
        b.setText(kRateText[size_t(k)]);
        char tip[128];
        std::snprintf(tip, sizeof tip,
                      "Play this sample at %.0f Hz (Resample off: the bytes are kept, the pitch moves)",
                      ymengine::kDmaRateHz[size_t(k)]);
        b.setTip(tip);
    }
    resample_.setTip("Resample on: a new rate resamples the sample, which keeps its length and its pitch (32 KB at "
                     "most)");
    mono_.setTip("Mono: one byte per frame (a stereo sample is mixed down: left and right averaged)");
    stereo_.setTip("Stereo: bytes interleaved left, right (a mono sample is doubled, 16 KB per side at most)");
    loop_.setTip("Repeat the part from Start to End while the note sounds; otherwise the part plays once");
    root_.setRange(0, 127, 1);
    root_.setTip("Root note: the note that plays the sample at its rate");
    root_.setId("dmaRoot");
    rootv_.setPx(14);
    rootv_.setWholeText(true);  // a note name: C#4, Sol3 or a MIDI number
    rootv_.setTip("Click to type a note (C#4, Sol3) or a MIDI note number");
    // Fine tune, as in the Digidrums window: its unit stays in normal video under the pointer.
    fine_.setRange(-ymengine::kDmaCentsMax, ymengine::kDmaCentsMax, 1);
    fine_.setTip("Fine tuning of this slot in cents");
    fine_.setId("dmaFine");
    finev_.setPx(14);
    finev_.setPadChars(1);
    finev_.setUnit(" ct");
    rootv_.onCommit = [this](const std::string& t) {
        const auto n = ymseq::parseAbsNote(t);
        if (!n) return false;
        root_.input(*n);
        return true;
    };
    normalize_.setTip("Raise the sample to full scale");
    reverse_.setTip("Reverse the sample: it plays from its end");
    fade_.setTip("Fade the sample out to its end");
    sign_.setTip("Flip the top bit of each byte (signed <-> unsigned)");
    gainDown_.setTip("Lower the sample by 3 dB");
    gainUp_.setTip("Raise the sample by 3 dB (clipped)");
    chanL_.setTip("Stereo: the tools work on the left channel");
    chanR_.setTip("Stereo: the tools work on the right channel");
    banksBtn_.setTip("Save the 8 samples as a named bank, load or delete a bank (library shared by all projects)");
    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{
             &status_, &useSte_, &hint_, &hint2_, &sep_, &labSlot_, &labName_, &name_, &labRate_, &labRoot_, &root_,
             &rootv_, &labFine_, &fine_, &finev_, &labSample_, &wave_, &load_, &play_, &clear_, &loop_, &labStart_,
             &start_, &startv_, &labEnd_, &end_, &endv_, &labFormat_, &mono_, &stereo_, &resample_, &labTools_,
             &normalize_, &reverse_, &fade_, &sign_, &gainDown_, &gainUp_, &chanL_, &chanR_, &banksBtn_})
        add(*w);
    for (auto& b : slots_) add(b);
    for (auto& b : rates_) add(b);
}

void DmaWindow::wire() {
    onClose = [this] {
        stopPreview();  // a loop never plays on behind a closed window
        m_.syncWindowButtons();
    };
    useSte_.onClick = [this] { m_.setComputer(1); };  // 1: STE
    // Loading goes through the host (file dialog, decoding or raw bytes), asynchronous and without an undo step; the
    // preview is a command to the engine on the audio thread.
    for (int k = 0; k < ymengine::kDmaSlots; ++k)
        slots_[size_t(k)].onClick = [this, k] {
            sel_ = k;
            refresh();
        };
    name_.onClick = [this] { rename(); };
    wave_.onClick = [this] { m_.host_.loadDmaSample(sel_); };
    load_.onClick = [this] { m_.host_.loadDmaSample(sel_); };
    // With Loop on, Play starts the part in a loop and a click while it loops stops it, as does Loop switched off
    // (the engine stops a looping preview whose Loop goes off); otherwise each click plays the part once.
    play_.onClick = [this] {
        if (looping()) return stopPreview();
        m_.host_.seqCommand(Host::SeqCommand::DmaPreview, sel_);
        previewing_ = sel_;
        play_.setOn(looping());
    };
    clear_.onClick = [this] { editBytes([](DmaSlot&, std::vector<int8_t>& b) { b.clear(); }); };
    // The part played, in frames, with or without Loop: Start stays before End (at least one frame). End 0
    // (never set) or past a shorter sample is its end (sampleSpan): a moved Start writes End too.
    bindSlider(start_, startArmed_, [this](DmaSlot& s, double v) {
        const ymengine::SampleSpan part = ymengine::sampleSpan(s.start, s.end, frames());
        s.end = part.end;
        s.start = uint32_t(std::clamp(v, 0.0, part.end ? double(part.end - 1) : 0.0));
    });
    bindSlider(end_, endArmed_, [this](DmaSlot& s, double v) {
        const double n = double(frames());
        const ymengine::SampleSpan part = ymengine::sampleSpan(s.start, s.end, frames());
        s.start = part.start;
        s.end = uint32_t(std::clamp(v, std::min(n, double(part.start) + 1), n));
    });
    auto typeInto = [](gemui::Slider& sl) {
        return [&sl](const std::string& t) {
            const auto v = gemui::typedNumber(t);
            if (!v) return false;
            sl.input(*v);
            return true;
        };
    };
    bindSlider(fine_, fineArmed_, [](DmaSlot& s, double v) { s.cents = std::round(v); });
    startv_.onCommit = typeInto(start_);
    finev_.onCommit = typeInto(fine_);
    endv_.onCommit = typeInto(end_);
    for (int k = 0; k < ymengine::kDmaRates; ++k)
        rates_[size_t(k)].onClick = [this, k] { setRate(k); };
    // An option of the window, kept in the preferences like the other settings of the interface.
    banksBtn_.onClick = [this] { banksMenu(); };
    resample_.onClick = [this] {
        m_.host_.setPreference("dmaResample", resample_.on() ? "0" : "1");
        refresh();
    };
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

    // The tools of the Digidrums window and Sign and Gain, each one undo step; L and R choose the channels of a
    // stereo sample they work on, one of them always lit.
    normalize_.onClick = [this] { tool(plugin::DmaTool::Normalize); };
    reverse_.onClick = [this] { tool(plugin::DmaTool::Reverse); };
    fade_.onClick = [this] { tool(plugin::DmaTool::FadeOut); };
    sign_.onClick = [this] { tool(plugin::DmaTool::Sign); };
    gainDown_.onClick = [this] { tool(plugin::DmaTool::Gain, -kGainDb); };
    gainUp_.onClick = [this] { tool(plugin::DmaTool::Gain, kGainDb); };
    auto channel = [this](unsigned bit) {
        return [this, bit] {
            if (toolCh_ != bit) toolCh_ ^= bit;
            refresh();
        };
    };
    chanL_.onClick = channel(1);
    chanR_.onClick = channel(2);
}

void DmaWindow::tool(plugin::DmaTool t, double db) {
    const unsigned ch = toolCh_;
    if (t == plugin::DmaTool::Gain) {
        // The run's total from the bytes before its first step: a step down gives back the peaks a step up clipped.
        const std::vector<int8_t> from = gainRun_.step(sel_ * 4 + int(ch), bytes(), db);
        const double total = gainRun_.total();
        editBytes([&](DmaSlot& s, std::vector<int8_t>& b) {
            b = from;
            plugin::dmaTool(b, s.stereo, ch, t, total);
        });
        gainRun_.gave(bytes());
        return;
    }
    editBytes([t, db, ch](DmaSlot& s, std::vector<int8_t>& b) {
        plugin::dmaTool(b, s.stereo, ch, t, db);
        // The part played stays the same sound, now read backwards, when every frame turns round.
        if (t == plugin::DmaTool::Reverse && (!s.stereo || ch == 3))
            ymengine::mirrorSpan(s.start, s.end, uint32_t(b.size() / (s.stereo ? 2 : 1)));
    });
}

// While a slider is held, MainWindow::dragging_ points to it so that refresh() does not move it under the pointer;
// its release reads the state back.
void DmaWindow::bindSlider(gemui::Slider& s, bool& armed, std::function<void(DmaSlot&, double)> set) {
    s.onGestureStart = [this, &s, &armed] {
        m_.dragging_ = &s;
        armed = true;
    };
    s.onGestureEnd = [this] {
        m_.dragging_ = nullptr;
        refresh();
    };
    s.onChange = [this, &s, &armed, set = std::move(set)](double v) {
        // One undo step at the first change of a gesture; a value coming from elsewhere (typed) makes one too.
        const bool push = armed || m_.dragging_ != &s;
        DmaSlot next = params().slots[size_t(sel_)];
        set(next, v);
        if (next == params().slots[size_t(sel_)]) return;
        armed = false;
        edit([&next](DmaSlot& st) { st = next; }, push);
    };
}

// ------------------------------------------------------------------------------------------- state

bool DmaWindow::ste() const noexcept { return m_.ste(); }
const DmaParams& DmaWindow::params() const noexcept { return m_.extras_.dma; }
const std::vector<int8_t>& DmaWindow::bytes() const noexcept { return m_.extras_.dmaSamples[size_t(sel_)]; }
uint32_t DmaWindow::frames() const noexcept {
    return uint32_t(bytes().size() / (params().slots[size_t(sel_)].stereo ? 2 : 1));
}

// The step records the bank as it is now, before the change (undoEntryLike reads the current state), and the bytes of
// `slot` when it is one.
void DmaWindow::pushUndo(int slot) {
    UndoEntry e;
    e.kind = UndoEntry::Kind::Dma;
    e.index = slot;
    m_.host_.session().undo.push(m_.undoEntryLike(e));
}

void DmaWindow::edit(const std::function<void(DmaSlot&)>& fn, bool push) {
    // Nothing changes (button already chosen): no undo step.
    DmaParams p = params();
    fn(p.slots[size_t(sel_)]);
    if (p == params()) return;
    if (push) pushUndo(-1);
    // The host publishes the new state to the audio thread; pullExtras copies it back and refreshes every window.
    m_.host_.editExtras([&p](plugin::NonParamState& s) { s.dma = p; });
    m_.pullExtras();
}

void DmaWindow::editBytes(const std::function<void(DmaSlot&, std::vector<int8_t>&)>& fn) {
    DmaParams p = params();
    const int slot = sel_;
    std::vector<int8_t> b = bytes();
    std::string name = m_.extras_.dmaNames[size_t(slot)];
    DmaSlot& s = p.slots[size_t(slot)];
    fn(s, b);
    // Start and End within the new length: End at most the frames, Start before the end; an empty slot plays
    // the whole of its next sample.
    const auto n = uint32_t(b.size() / (s.stereo ? 2 : 1));
    s.end = std::min(s.end, n);
    const uint32_t last = s.end ? s.end : n;
    s.start = std::min(s.start, last ? last - 1 : 0);
    if (n == 0) {
        s.start = s.end = 0;
        name.clear();  // the name goes with the sample
    }
    if (p == params() && b == bytes() && name == m_.extras_.dmaNames[size_t(slot)]) return;
    pushUndo(slot);
    m_.host_.editExtras([&](plugin::NonParamState& st) {
        st.dma = p;
        st.dmaSamples[size_t(slot)] = std::move(b);
        st.dmaNames[size_t(slot)] = name;
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

void DmaWindow::setRate(int k) {
    const DmaSlot& now = params().slots[size_t(sel_)];
    if (k == now.rate) return;
    if (!resample_.on() || bytes().empty()) return edit([k](DmaSlot& s) { s.rate = uint8_t(k); });
    // One undo step with the bytes; Start and End stay on the same moments of the sample (End 0 stays at the end).
    editBytes([k](DmaSlot& s, std::vector<int8_t>& b) {
        const double from = ymengine::kDmaRateHz[s.rate], to = ymengine::kDmaRateHz[size_t(k)];
        b = plugin::dmaResample(b, s.stereo, from, to);
        auto scale = [&](uint32_t v) { return uint32_t(std::lround(double(v) * to / from)); };
        s.start = scale(s.start);
        s.end = scale(s.end);
        s.rate = uint8_t(k);
    });
}

void DmaWindow::rename() {
    gemui::Alert::Spec spec;
    spec.lines = {{"Slot " + std::to_string(sel_ + 1) + " name, 16 chars:"}};
    spec.promptButtons();
    spec.inputText = m_.extras_.dmaNames[size_t(sel_)];
    spec.inputChars = int(plugin::kDigidrumNameMax);
    const int slot = sel_;
    gemui::Alert::open(m_.desk_, std::move(spec), [this, slot](int button, const std::string& text) {
        if (button != gemui::Alert::kPromptOk) return;
        const std::string name = plugin::cleanDigidrumName(text);
        if (name == m_.extras_.dmaNames[size_t(slot)]) return;
        pushUndo(-1);  // the step keeps the names
        m_.host_.editExtras([&](plugin::NonParamState& s) { s.dmaNames[size_t(slot)] = name; });
        m_.pullExtras();
    });
}

// ------------------------------------------------------------------------------------------- banks

// As in the Digidrums window: the library is a file shared by every project and every instance (Host::dmaLibrary), so
// each bank action reads it again first. An empty text is an empty library; a text that does not parse marks it
// unreadable.
void DmaWindow::loadLibrary() {
    const std::string text = m_.host_.dmaLibrary();
    unreadable_ = false;
    banks_.clear();
    if (text.empty()) return;
    if (auto lib = plugin::parseDmaBanks(text)) banks_ = std::move(*lib);
    else unreadable_ = true;
}

void DmaWindow::storeLibrary() {
    if (unreadable_) {
        // The existing file could not be read: it is not overwritten.
        gemui::Alert::Spec spec;
        spec.lines = {{"The DMA bank file could not be read."}, {"It was left unchanged."}};
        gemui::Alert::open(m_.desk_, std::move(spec));
        loadLibrary();
        return;
    }
    m_.host_.setDmaLibrary(plugin::dumpDmaBanks(banks_));
}

const std::vector<plugin::DmaBank>& DmaWindow::banks() {
    loadLibrary();
    return banks_;
}

void DmaWindow::saveBank(const std::string& name) {
    const std::string clean = plugin::cleanDigidrumName(name);
    if (clean.empty()) return;
    loadLibrary();
    if (!unreadable_) {
        plugin::DmaBank b = plugin::captureDmaBank(clean, m_.extras_);
        const auto it = std::find_if(banks_.begin(), banks_.end(),
                                     [&clean](const plugin::DmaBank& x) { return x.name == clean; });
        if (it != banks_.end()) *it = std::move(b);
        else banks_.push_back(std::move(b));
    }
    storeLibrary();
}

void DmaWindow::loadBank(const std::string& name) {
    // The eight samples replaced: not an undo step (like loading a file); a looping preview stops with its sample.
    loadLibrary();
    const auto it = std::find_if(banks_.begin(), banks_.end(),
                                 [&name](const plugin::DmaBank& x) { return x.name == name; });
    if (it == banks_.end()) return;
    const plugin::DmaBank bank = *it;
    stopPreview();
    m_.host_.editExtras([&bank](plugin::NonParamState& s) {
        plugin::applyDmaBank(bank, s);
        plugin::sanitize(s);
    });
    m_.pullExtras();
}

void DmaWindow::deleteBank(const std::string& name) {
    loadLibrary();
    const auto it = std::find_if(banks_.begin(), banks_.end(),
                                 [&name](const plugin::DmaBank& x) { return x.name == name; });
    if (unreadable_ || it == banks_.end()) return;
    banks_.erase(it);
    storeLibrary();
}

void DmaWindow::confirm(const std::string& line, const std::string& button, std::function<void()> ok) {
    gemui::Alert::Spec spec;
    spec.lines = {{line}};
    spec.confirmButtons(button);
    gemui::Alert::open(m_.desk_, std::move(spec), [ok = std::move(ok)](int b, const std::string&) {
        if (b == gemui::Alert::kConfirmOk) ok();
    });
}

// Save bank..., Load bank and Delete bank, the last two with a submenu of the banks, as in the Digidrums window.
void DmaWindow::banksMenu() {
    loadLibrary();
    std::vector<MenuItem> items;
    MenuItem save;
    save.label = "Save bank...";
    save.pick = [this] {
        gemui::Alert::Spec spec;
        spec.lines = {{"Bank name, 16 chars (same name: replaced):"}};
        spec.promptButtons();
        spec.inputChars = int(plugin::kDigidrumNameMax);
        gemui::Alert::open(m_.desk_, std::move(spec), [this](int button, const std::string& text) {
            if (button == gemui::Alert::kPromptOk) saveBank(text);
        });
    };
    if (unreadable_) {
        save.disabled = true;
        save.why = "The bank file could not be read";
    }
    items.push_back(std::move(save));
    MenuItem load, del;
    load.label = "Load bank";
    del.label = "Delete bank";
    for (const plugin::DmaBank& b : banks_) {
        const std::string nm = b.name;
        MenuItem l;
        l.label = nm;
        l.pick = [this, nm] {
            confirm("Replace the 8 samples with bank \"" + nm + "\"?", "Load", [this, nm] { loadBank(nm); });
        };
        load.sub.push_back(std::move(l));
        MenuItem d;
        d.label = nm;
        d.pick = [this, nm] {
            confirm("Delete bank \"" + nm + "\" from the library?", "Delete", [this, nm] { deleteBank(nm); });
        };
        del.sub.push_back(std::move(d));
    }
    for (MenuItem* m : {&load, &del})
        if (m->sub.empty()) {
            m->disabled = true;
            m->why = unreadable_ ? "The bank file could not be read" : "No bank saved yet";
        }
    items.push_back(std::move(load));
    items.push_back(MenuItem::separatorItem());
    items.push_back(std::move(del));
    m_.popup(banksBtn_, std::move(items), gemui::kNoMarks);
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

bool DmaWindow::looping() const noexcept { return previewing_ == sel_ && params().slots[size_t(sel_)].loop; }

void DmaWindow::stopPreview() {
    if (previewing_ < 0) return;
    m_.host_.seqCommand(Host::SeqCommand::DmaPreview, previewing_, 1);
    previewing_ = -1;
    play_.setOn(false);
}

void DmaWindow::refreshEngineStatus(const plugin::EngineStatus& t) {
    if (t.dmaPreview == previewing_) return;
    previewing_ = t.dmaPreview;
    if (isOpen()) play_.setOn(looping());
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
        const std::string& nm = m_.extras_.dmaNames[size_t(k)];
        sb.setTip("Slot " + std::to_string(k + 1) + (nm.empty() ? "" : " \"" + nm + "\"") + ": " +
                  (n ? std::to_string(n) + " bytes" : "empty"));  // the name too
    }
    const std::string& name = m_.extras_.dmaNames[size_t(sel_)];
    name_.setText(name.empty() ? std::string("(none)") : name);
    const uint32_t n = frames();
    const ymengine::SampleSpan part = ymengine::sampleSpan(s.start, s.end, n);
    wave_.setSample(sel_, b, ymengine::kDmaRateHz[s.rate], s.stereo, s.loop, part.start, part.end, name);
    // Start runs over the frames (0 to frames - 1), End over the positions after them (1 to frames); the ranges keep
    // at least one step for an empty slot. They set the part played with or without Loop (Indus, 26/10/2026).
    const bool has = n > 0;
    start_.setRange(0, std::max(1.0, double(n) - 1), 1);
    end_.setRange(std::min(1.0, double(n)), std::max(1.0, double(n)), 1);
    if (m_.dragging_ != &start_) start_.setValue(part.start);
    if (m_.dragging_ != &end_) end_.setValue(part.end);
    if (!startv_.editing()) startv_.setText(std::to_string(part.start));
    if (!endv_.editing()) endv_.setText(std::to_string(part.end));
    for (gemui::Slider* sl : {&start_, &end_}) sl->setEnabled(has);
    for (gemui::Label* l : {&labStart_, &labEnd_}) l->setDim(!has);
    for (gemui::ValueField* f : {&startv_, &endv_}) f->setDim(!has);
    for (int k = 0; k < ymengine::kDmaRates; ++k) rates_[size_t(k)].setOn(k == s.rate);
    resample_.setOn(m_.host_.preference("dmaResample", "0") == "1");
    mono_.setOn(!s.stereo);
    stereo_.setOn(s.stereo);
    loop_.setOn(s.loop);
    play_.setOn(looping());
    if (m_.dragging_ != &root_) root_.setValue(s.root);
    if (!rootv_.editing()) rootv_.setText(ymseq::absNoteName(s.root, names));
    if (m_.dragging_ != &fine_) fine_.setValue(s.cents);
    if (!finev_.editing()) {
        const int c = int(std::lround(s.cents));
        finev_.setText((c > 0 ? "+" : "") + std::to_string(c) + " ct");
    }
    for (Button* t : {&play_, &clear_, &normalize_, &reverse_, &fade_, &sign_, &gainDown_, &gainUp_})
        t->setEnabled(!b.empty());
    chanL_.setOn(toolCh_ & 1u);
    chanR_.setOn(toolCh_ & 2u);
    for (Button* c : {&chanL_, &chanR_}) c->setEnabled(s.stereo && !b.empty());
    layout();
}

// ------------------------------------------------------------------------------------------- layout

// The status line, the notes and a rule across the window, then two columns side by side as in the Digidrums window:
// the slot settings (col1 px wide) and the sample (col2); the window takes the height of the taller column. Rows are
// flexRow lines without wrapping.
void DmaWindow::layout() {
    const double ox = box().x, oy = box().y;
    const double x = ox + 14;  // content (border 2, margin 4, padding 8)
    auto btn = [](Button& b) { return FlexItem{&b, b.naturalWidth(), double(b.naturalHeight())}; };
    auto lab = [](gemui::Label& l, double w = 0) { return FlexItem{&l, w > 0 ? w : l.naturalWidth(), 16}; };
    auto sld = [](gemui::Slider& s, double w) { return FlexItem{&s, w, 16}; };

    const double col1 = 262, col2 = 360;
    const double x2 = x + col1 + 24;
    std::vector<FlexItem> head = {lab(status_)};
    if (useSte_.visible()) head.push_back(btn(useSte_));
    double headW = 0;
    for (const FlexItem& f : head) headW += f.width;
    headW += kGap * double(head.size() - 1);
    const double contentW = std::max({col1 + 24 + col2, headW, hint_.naturalWidth(), hint2_.naturalWidth()});

    double y = oy + 32 + kPad;  // below the frame (6) and the title bar (26)
    y += flexRow(x, y, contentW, head, kGap, false) + 6;
    hint_.setBox({x, y, hint_.naturalWidth(), 12});
    y += 12;
    hint2_.setBox({x, y, hint2_.naturalWidth(), 12});
    y += 12 + 8;
    sep_.setBox({x - kPad, y, contentW + 2 * kPad, 2});
    y += 2 + 8;

    // On the model of the Digidrums window (Indus, 26/10/2026). Slot column: labels as wide as "Name", the
    // eight slots, the name, the four rates and the root note. Sample column: its label, with Load, Play and Clear
    // flush right on the same line; the 360 × 72 drawing; Loop, Start and End, End's slider under Start's; format and
    // tools.
    const double y0 = y;
    double y1 = y0;
    const double lw = Font::width("Name", 14);
    std::vector<FlexItem> sl = {lab(labSlot_, lw)};
    for (auto& b : slots_) sl.push_back(btn(b));
    y1 += flexRow(x, y1, col1, sl, 4, false) + 6;
    std::vector<FlexItem> nm = {lab(labName_, lw), btn(name_)};
    y1 += flexRow(x, y1, col1, nm, kGap, false) + 10;
    std::vector<FlexItem> rt = {lab(labRate_, lw)};
    for (auto& b : rates_) rt.push_back(btn(b));
    y1 += flexRow(x, y1, col1, rt, 4, false) + 4;
    std::vector<FlexItem> rs = {btn(resample_)};  // under the rates
    rs.front().marginLeft = lw + 4;
    y1 += flexRow(x, y1, col1, rs, 4, false) + 6;
    std::vector<FlexItem> rr = {lab(labRoot_, lw), sld(root_, 134), {&rootv_, Font::width("C#10", 14), 16}};
    y1 += flexRow(x, y1, col1, rr, kGap, false) + 6;
    std::vector<FlexItem> fn = {lab(labFine_, lw), sld(fine_, 134), {&finev_, Font::width("+100 ct", 14), 16}};
    y1 += flexRow(x, y1, col1, fn, kGap, false) + 10;
    std::vector<FlexItem> bk = {btn(banksBtn_)};  // as in the Digidrums window
    y1 += flexRow(x, y1, col1, bk, kGap, false);

    double y2 = y0;
    labSample_.setBox({x2, y2 + 2, labSample_.naturalWidth(), 16});
    std::vector<FlexItem> io = {btn(load_), btn(play_), btn(clear_)};
    const double ioW = load_.naturalWidth() + play_.naturalWidth() + clear_.naturalWidth() + 8;
    y2 += flexRow(x2 + col2 - ioW, y2, ioW, io, 4, false) + 6;
    wave_.setBox({x2, y2, col2, 72});
    y2 += 72 + 8;
    const double lw2 = Font::width("Format", 14);
    const double vw = Font::width("00000", 14);  // typed frames: 16384 stereo or 32768 mono at most
    std::vector<FlexItem> lp = {btn(loop_), lab(labStart_), sld(start_, 104), {&startv_, vw, 16}};
    y2 += flexRow(x2, y2, col2, lp, kGap, false) + 6;
    std::vector<FlexItem> le = {lab(labEnd_), sld(end_, 104), {&endv_, vw, 16}};
    le.front().marginLeft = loop_.naturalWidth() + kGap + labStart_.naturalWidth() - labEnd_.naturalWidth();
    y2 += flexRow(x2, y2, col2, le, kGap, false) + 10;
    std::vector<FlexItem> fm = {lab(labFormat_, lw2), btn(mono_), btn(stereo_)};
    y2 += flexRow(x2, y2, col2, fm, 4, false) + 6;
    std::vector<FlexItem> tl = {lab(labTools_, lw2), btn(normalize_), btn(reverse_), btn(fade_)};
    y2 += flexRow(x2, y2, col2, tl, 4, false) + 6;
    // Sign and Gain under them; L and R flush right.
    std::vector<FlexItem> gn = {btn(sign_), btn(gainDown_), btn(gainUp_)};
    gn.front().marginLeft = lw2 + 4;
    flexRow(x2, y2, col2, gn, 4, false);
    std::vector<FlexItem> ch = {btn(chanL_), btn(chanR_)};
    const double chW = chanL_.naturalWidth() + chanR_.naturalWidth() + 4;
    y2 += flexRow(x2 + col2 - chW, y2, chW, ch, 4, false);
    y = std::max(y1, y2);

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
                           {"dmaName", &name_},
                           {"dmaPlay", &play_},         {"dmaClear", &clear_},   {"dmaMono", &mono_},
                           {"dmaStereo", &stereo_},     {"dmaLoop", &loop_},     {"dmaRoot", &root_},
                           {"dmaRootV", &rootv_},       {"dmaSign", &sign_},     {"dmaGainDown", &gainDown_},
                           {"dmaGainUp", &gainUp_},     {"dmaStart", &start_},   {"dmaEnd", &end_},
                           {"dmaStartV", &startv_},     {"dmaEndV", &endv_},     {"dmaNormalize", &normalize_},
                           {"dmaReverse", &reverse_},   {"dmaFade", &fade_},     {"dmaChanL", &chanL_},
                           {"dmaChanR", &chanR_},       {"dmaResample", &resample_},
                           {"dmaFine", &fine_},         {"dmaFineV", &finev_},   {"dmaBanks", &banksBtn_}};
    for (const Named& n : named)
        if (id == n.id) return n.w->visible() ? std::optional<Rect>(n.w->bounds()) : std::nullopt;
    return std::nullopt;
}

const gemui::Slider* DmaWindow::slider(std::string_view id) const noexcept {
    if (id == "dmaRoot") return &root_;
    if (id == "dmaFine") return &fine_;
    if (id == "dmaStart") return &start_;
    if (id == "dmaEnd") return &end_;
    return nullptr;
}

}  // namespace stym::pluginui
