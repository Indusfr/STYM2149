// STYM2149 — pluginui: Digidrum editor
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "pluginui/DigidrumWindow.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "gemui/Alert.h"
#include "plugincore/DmaSample.h"
#include "pluginui/Layout.h"
#include "pluginui/MainWindow.h"
#include "ymseq/Tables.h"

namespace stym::pluginui {

using gemui::Button;
using gemui::Face;
using gemui::Font;
using gemui::MenuItem;
using gemui::Rect;
using plugin::DigiConvert;
using plugin::DigidrumSlotState;
namespace colors = gemui::colors;

namespace {

constexpr double kPad = 8, kGap = 8;  // padding of the content and gap between the items of a row (px)
constexpr double kGainDb = 3;         // step of the -3 dB and +3 dB tools
// Rate presets in Hz; the first is the prototype's rate (its tooltip says so).
constexpr std::array<double, 3> kPresetHz = {7800, 8000, 11025};

std::string signedText(int v) { return v > 0 ? "+" + std::to_string(v) : std::to_string(v); }

}  // namespace

DigidrumWindow::DigidrumWindow(MainWindow& main) : m_(main) {
    build();
    wire();
    place({double(kX), double(kY), 300, 90});  // provisional size: layout() sets the real one at the first refresh
}

void DigidrumWindow::build() {
    setTitle("DIGIDRUMS");
    for (gemui::Label* l : {&hint_, &hint2_}) {
        l->setFace(Face::kSmall);
        l->setColor(colors::kHint);
    }
    for (int k = 0; k < ymengine::kDigidrumSlots; ++k) {
        Button& b = slots_[size_t(k)];
        b.setText(std::to_string(k + 1));
        b.setPadding(2, 6);
    }
    name_.setTip("Name of this slot, 16 characters (shown in its tip and kept in banks)");
    banksBtn_.setTip("Save the 8 slots as a named bank, load or delete a bank (library shared by all projects)");
    rate_.setRange(ymengine::kDigidrumRateMin, ymengine::kDigidrumRateMax, 1);
    rate_.setTip("Timer rate at the root note (the values are kept: the pitch moves). A file loads at this rate.");
    rate_.setId("digRate");
    for (size_t k = 0; k < presets_.size(); ++k) {
        presets_[k].setText(std::to_string(int(kPresetHz[k])));
        presets_[k].setTip("Rate " + std::to_string(int(kPresetHz[k])) + " Hz" +
                           (k == 0 ? " (the prototype's rate)" : ""));
    }
    root_.setRange(0, 127, 1);
    root_.setTip("Root note: the note that plays this slot at its rate");
    root_.setId("digRoot");
    fine_.setRange(-ymengine::kDigidrumCentsMax, ymengine::kDigidrumCentsMax, 1);
    fine_.setTip("Fine tuning of this slot in cents");
    fine_.setId("digFine");
    // Typed values (D-264): as many dashes as the longest value ("20000 Hz", "Sol#-1", "+100 ct").
    for (gemui::ValueField* f : {&ratev_, &rootv_, &finev_, &startv_, &endv_}) f->setPx(14);
    rootv_.setPadChars(2);
    rootv_.setWholeText(true);  // a note name: C#4, Sol3 or a MIDI number
    rootv_.setTip("Click to type a note (C#4, Sol3) or a MIDI note number");
    finev_.setPadChars(1);
    ratev_.setUnit(" Hz");  // the units stay in normal video under the pointer (D-451)
    finev_.setUnit(" ct");
    load_.setTip("Load an audio file into this slot (first channel, resampled to the rate, 3 s at most)");
    play_.setTip("Play this slot's part at its rate, through the chip's volume table: once, or with Loop on in a loop "
                 "until Play or Loop is clicked again");
    play_.setId("digPlay");
    clear_.setTip("Empty this slot (name, sample, Start, End and Loop)");
    loop_.setTip("Repeat the part from Start to End while the note is held; at note off it plays on to End");
    start_.setTip("First value played (and of the loop)");
    end_.setTip("End of the part played (the value after its last one)");
    start_.setId("digStart");
    end_.setId("digEnd");
    linear_.setTip("Linear 4-bit values, as the prototype: round(v x 7.5 + 7.5)");
    ym_.setTip("4-bit values matched to the chip's measured output levels (the sample keeps its shape)");
    normalize_.setTip("Raise the 8-bit source to full scale");
    reverse_.setTip("Reverse the sample: it plays from its end");
    fade_.setTip("Fade the sample out to its end");
    sign_.setTip("Flip the top bit of each byte (signed <-> unsigned)");
    gainDown_.setTip("Lower the 8-bit source by 3 dB");
    gainUp_.setTip("Raise the 8-bit source by 3 dB (clipped)");
    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{
             &hint_,   &hint2_,  &sep_,     &labSlot_,  &labName_, &name_,      &labRate_,  &rate_,   &ratev_,
             &labRoot_, &root_,  &rootv_,   &labFine_,  &fine_,    &finev_,     &banksBtn_, &labSample_, &wave_,
             &load_,   &play_,   &clear_,   &loop_,     &labStart_, &start_,    &startv_,   &labEnd_, &end_,
             &endv_,   &labConvert_, &linear_, &ym_,    &labTools_, &normalize_, &reverse_, &fade_,   &sign_,
             &gainDown_, &gainUp_})
        add(*w);
    for (auto& b : slots_) add(b);
    for (auto& b : presets_) add(b);
}

void DigidrumWindow::wire() {
    onClose = [this] {
        stopPreview();  // a loop never plays on behind a closed window (D-449)
        m_.syncWindowButtons();
    };
    for (int k = 0; k < ymengine::kDigidrumSlots; ++k) slots_[size_t(k)].onClick = [this, k] { selectSlot(k); };
    name_.onClick = [this] { rename(); };
    banksBtn_.onClick = [this] { banksMenu(); };

    bindSlider(rate_, rateArmed_, [](DigidrumSlotState& s, double v) { s.play.rateHz = std::round(v); });
    for (size_t k = 0; k < presets_.size(); ++k)
        presets_[k].onClick = [this, k] { edit([k](DigidrumSlotState& s) { s.play.rateHz = kPresetHz[k]; }); };
    bindSlider(root_, rootArmed_,
               [](DigidrumSlotState& s, double v) { s.play.root = uint8_t(std::clamp(int(v), 0, 127)); });
    bindSlider(fine_, fineArmed_, [](DigidrumSlotState& s, double v) { s.play.cents = std::round(v); });
    auto typeInto = [](gemui::Slider& sl) {
        return [&sl](const std::string& t) {
            const auto v = gemui::typedNumber(t);
            if (!v) return false;
            sl.input(*v);
            return true;
        };
    };
    ratev_.onCommit = typeInto(rate_);
    rootv_.onCommit = [this](const std::string& t) {
        const auto n = ymseq::parseAbsNote(t);
        if (!n) return false;
        root_.input(*n);
        return true;
    };
    finev_.onCommit = typeInto(fine_);
    startv_.onCommit = typeInto(start_);
    endv_.onCommit = typeInto(end_);
    // The part played (D-443), with or without Loop: Start stays before End (at least one value). End 0 (never set) or
    // past a shorter sample is its end (sampleSpan): a moved Start writes End too.
    bindSlider(start_, startArmed_, [this](DigidrumSlotState& s, double v) {
        const ymengine::SampleSpan part = ymengine::sampleSpan(s.play.start, s.play.end, uint32_t(nibbles().size()));
        s.play.end = part.end;
        s.play.start = uint32_t(std::clamp(v, 0.0, part.end ? double(part.end - 1) : 0.0));
    });
    bindSlider(end_, endArmed_, [this](DigidrumSlotState& s, double v) {
        const double len = double(nibbles().size());
        const ymengine::SampleSpan part = ymengine::sampleSpan(s.play.start, s.play.end, uint32_t(len));
        s.play.start = part.start;
        s.play.end = uint32_t(std::clamp(v, std::min(len, double(part.start) + 1), len));
    });
    loop_.onClick = [this] { edit([](DigidrumSlotState& s) { s.play.loop = !s.play.loop; }); };

    // Loading goes through the host (file dialog, audio decoding), asynchronous and without an undo step; the preview
    // is a command to the engine on the audio thread.
    wave_.onClick = [this] { m_.host_.loadDigidrum(slot()); };
    load_.onClick = [this] { m_.host_.loadDigidrum(slot()); };
    // With Loop on, Play starts the part in a loop and a click while it loops stops it, as does Loop switched off
    // (the engine stops a looping preview whose Loop goes off); otherwise each click plays the part once (D-449).
    play_.onClick = [this] {
        if (looping()) return stopPreview();
        m_.host_.seqCommand(Host::SeqCommand::DigiPreview, slot());
        previewing_ = slot();
        play_.setOn(looping());
    };
    clear_.onClick = [this] {
        editBytes([](DigidrumSlotState& s, std::vector<int8_t>& src) {
            src.clear();
            s.name.clear();
            s.play.loop = false;
        });
    };
    // The conversion and the tools work on the 8-bit source; editBytes converts it again into the 4-bit values.
    linear_.onClick = [this] {
        if (state().convert == DigiConvert::Linear) return;
        editBytes([](DigidrumSlotState& s, std::vector<int8_t>&) { s.convert = DigiConvert::Linear; });
    };
    ym_.onClick = [this] {
        if (state().convert == DigiConvert::YmLevels) return;
        editBytes([](DigidrumSlotState& s, std::vector<int8_t>&) { s.convert = DigiConvert::YmLevels; });
    };
    normalize_.onClick = [this] {
        editBytes([](DigidrumSlotState&, std::vector<int8_t>& src) { plugin::digiNormalize(src); });
    };
    // Reverse mirrors Start and End, so that the part played stays the same sound read backwards (D-450).
    reverse_.onClick = [this] {
        editBytes([](DigidrumSlotState& s, std::vector<int8_t>& src) {
            plugin::digiReverse(src);
            ymengine::mirrorSpan(s.play.start, s.play.end, uint32_t(src.size()));
        });
    };
    fade_.onClick = [this] {
        editBytes([](DigidrumSlotState&, std::vector<int8_t>& src) { plugin::digiFadeOut(src); });
    };
    sign_.onClick = [this] {
        editBytes([](DigidrumSlotState&, std::vector<int8_t>& src) { plugin::dmaSign(src); });
    };
    gainDown_.onClick = [this] { gain(-kGainDb); };
    gainUp_.onClick = [this] { gain(kGainDb); };
}

// Gain steps in a row (D-453): the run's total from the source before its first step, so that a step down gives back
// the peaks a step up clipped.
void DigidrumWindow::gain(double db) {
    const DigidrumSlotState& s0 = state();
    const std::vector<int8_t> now = s0.source.empty() ? plugin::digidrumSourceOf(nibbles()) : s0.source;
    const std::vector<int8_t> from = gainRun_.step(slot(), now, db);
    const double total = gainRun_.total();
    editBytes([&](DigidrumSlotState&, std::vector<int8_t>& src) {
        src = from;
        plugin::dmaGain(src, total);
    });
    gainRun_.gave(state().source);
}

// While a slider is held, MainWindow::dragging_ points to it so that refresh() does not move it under the pointer;
// its release reads the state back.
void DigidrumWindow::bindSlider(gemui::Slider& s, bool& armed,
                                std::function<void(DigidrumSlotState&, double)> set) {
    s.onGestureStart = [this, &s, &armed] {
        m_.dragging_ = &s;
        armed = true;
    };
    s.onGestureEnd = [this] {
        m_.dragging_ = nullptr;
        refresh();
    };
    s.onChange = [this, &s, &armed, set = std::move(set)](double v) {
        // One undo step at the first change of a gesture; a value coming from elsewhere (without a gesture) makes one
        // too.
        const bool push = armed || m_.dragging_ != &s;
        DigidrumSlotState next = state();
        set(next, v);
        if (next == state()) return;
        armed = false;
        edit([&next](DigidrumSlotState& st) { st = next; }, push);
    };
}

// ------------------------------------------------------------------------------------------- state

int DigidrumWindow::slot() const noexcept {
    return std::clamp(m_.host_.session().digiSlot, 0, ymengine::kDigidrumSlots - 1);
}

void DigidrumWindow::selectSlot(int k) {
    m_.host_.session().digiSlot = std::clamp(k, 0, ymengine::kDigidrumSlots - 1);
    refresh();
}

bool DigidrumWindow::looping() const noexcept { return previewing_ == slot() && state().play.loop; }

void DigidrumWindow::stopPreview() {
    if (previewing_ < 0) return;
    m_.host_.seqCommand(Host::SeqCommand::DigiPreview, previewing_, 1);
    previewing_ = -1;
    play_.setOn(false);
}

void DigidrumWindow::refreshEngineStatus(const plugin::EngineStatus& t) {
    if (t.digiPreview == previewing_) return;
    previewing_ = t.digiPreview;
    if (isOpen()) play_.setOn(looping());
}

const DigidrumSlotState& DigidrumWindow::state() const noexcept { return m_.extras_.digiSlots[size_t(slot())]; }
const std::vector<uint8_t>& DigidrumWindow::nibbles() const noexcept { return m_.extras_.digidrums[size_t(slot())]; }

// The step records the slot as it is now, before the change (undoEntryLike reads the current state); with `bytes`,
// its source and 4-bit values too, otherwise its settings only.
void DigidrumWindow::pushUndo(bool bytes) {
    UndoEntry e;
    e.kind = UndoEntry::Kind::Digi;
    e.index = slot();
    e.digiBytes = bytes;
    m_.host_.session().undo.push(m_.undoEntryLike(e));
}

void DigidrumWindow::edit(const std::function<void(DigidrumSlotState&)>& fn, bool push) {
    // Nothing changes (button already chosen): no undo step.
    DigidrumSlotState s = state();
    fn(s);
    if (s == state()) return;
    if (push) pushUndo(false);
    // The host publishes the new state to the audio thread; pullExtras copies it back and refreshes every window.
    const size_t k = size_t(slot());
    m_.host_.editExtras([&s, k](plugin::NonParamState& st) { st.digiSlots[k] = std::move(s); });
    m_.pullExtras();
}

void DigidrumWindow::editBytes(const std::function<void(DigidrumSlotState&, std::vector<int8_t>&)>& fn) {
    // Slot without a source (state from before M6): source inferred from the values; kept as soon as it is used.
    const DigidrumSlotState s0 = state();
    const std::vector<int8_t> src0 = s0.source.empty() ? plugin::digidrumSourceOf(nibbles()) : s0.source;
    DigidrumSlotState s = s0;
    std::vector<int8_t> src = src0;
    fn(s, src);
    std::vector<uint8_t> n = plugin::digidrumNibbles(src, s.convert);
    // Start and End within the new length: End at most the length, Start before End; an empty slot has no loop.
    const auto len = uint32_t(n.size());
    s.play.end = std::min(s.play.end, len);
    s.play.start = std::min(s.play.start, s.play.end ? s.play.end - 1 : 0);
    if (len == 0) {
        s.play.loop = false;
        s.play.start = s.play.end = 0;
    }
    if (s == s0 && src == src0 && n == nibbles()) return;
    s.source = std::move(src);
    pushUndo(true);
    const size_t k = size_t(slot());
    m_.host_.editExtras([&](plugin::NonParamState& st) {
        st.digiSlots[k] = std::move(s);
        st.digidrums[k] = std::move(n);
    });
    m_.pullExtras();
}

void DigidrumWindow::rename() {
    gemui::Alert::Spec spec;
    spec.lines = {{"Slot " + std::to_string(slot() + 1) + " name, 16 chars:"}};
    spec.promptButtons();
    spec.inputText = state().name;
    spec.inputChars = int(plugin::kDigidrumNameMax);
    gemui::Alert::open(m_.desk_, std::move(spec), [this](int button, const std::string& text) {
        if (button != gemui::Alert::kPromptOk) return;
        const std::string name = plugin::cleanDigidrumName(text);
        edit([&name](DigidrumSlotState& s) { s.name = name; });
    });
}

// ------------------------------------------------------------------------------------------- banks

// The library is a file shared by every project and every instance (Host::digidrumLibrary), so each bank action
// reads it again first. An empty text is an empty library; a text that does not parse marks it unreadable.
void DigidrumWindow::loadLibrary() {
    const std::string text = m_.host_.digidrumLibrary();
    unreadable_ = false;
    banks_.clear();
    if (text.empty()) return;
    if (auto lib = plugin::parseDigidrumBanks(text)) banks_ = std::move(*lib);
    else unreadable_ = true;
}

void DigidrumWindow::storeLibrary() {
    if (unreadable_) {
        // The existing file could not be read: it is not overwritten (like the Patches library).
        gemui::Alert::Spec spec;
        spec.lines = {{"The Digidrum bank file could not be read."}, {"It was left unchanged."}};
        gemui::Alert::open(m_.desk_, std::move(spec));
        loadLibrary();
        return;
    }
    m_.host_.setDigidrumLibrary(plugin::dumpDigidrumBanks(banks_));
}

const std::vector<plugin::DigidrumBank>& DigidrumWindow::banks() {
    loadLibrary();
    return banks_;
}

void DigidrumWindow::saveBank(const std::string& name) {
    const std::string clean = plugin::cleanDigidrumName(name);
    if (clean.empty()) return;
    loadLibrary();
    if (!unreadable_) {
        plugin::DigidrumBank b = plugin::captureBank(clean, m_.extras_);
        const auto it = std::find_if(banks_.begin(), banks_.end(),
                                     [&clean](const plugin::DigidrumBank& x) { return x.name == clean; });
        if (it != banks_.end()) *it = std::move(b);
        else banks_.push_back(std::move(b));
    }
    storeLibrary();
}

void DigidrumWindow::loadBank(const std::string& name) {
    // The eight slots replaced: not an undo step (like loading a sample).
    loadLibrary();
    const auto it = std::find_if(banks_.begin(), banks_.end(),
                                 [&name](const plugin::DigidrumBank& x) { return x.name == name; });
    if (it == banks_.end()) return;
    const plugin::DigidrumBank bank = *it;
    m_.host_.editExtras([&bank](plugin::NonParamState& s) {
        plugin::applyBank(bank, s);
        plugin::sanitize(s);
    });
    m_.pullExtras();
}

void DigidrumWindow::deleteBank(const std::string& name) {
    loadLibrary();
    const auto it = std::find_if(banks_.begin(), banks_.end(),
                                 [&name](const plugin::DigidrumBank& x) { return x.name == name; });
    if (unreadable_ || it == banks_.end()) return;
    banks_.erase(it);
    storeLibrary();
}

void DigidrumWindow::confirm(const std::string& line, const std::string& button, std::function<void()> ok) {
    gemui::Alert::Spec spec;
    spec.lines = {{line}};
    // gemConfirm rule for a destructive action: Cancel first, default and with the focus.
    spec.confirmButtons(button);
    gemui::Alert::open(m_.desk_, std::move(spec), [ok = std::move(ok)](int b, const std::string&) {
        if (b == gemui::Alert::kConfirmOk) ok();
    });
}

void DigidrumWindow::banksMenu() {
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
    for (size_t k = 0; k < banks_.size(); ++k) {
        const std::string nm = banks_[k].name;
        MenuItem l;
        l.label = nm;
        l.pick = [this, nm] {
            confirm("Replace the 8 slots with bank \"" + nm + "\"?", "Load", [this, nm] { loadBank(nm); });
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

// ------------------------------------------------------------------------------------------- display

void DigidrumWindow::show() {
    open();
    refresh();
    m_.syncWindowButtons();
}

// The held slider (MainWindow::dragging_) and a field being typed into keep what the user is doing; everything else
// shows the state.
void DigidrumWindow::refresh() {
    if (!isOpen()) return;
    const int k = slot();
    const DigidrumSlotState& s = state();
    const std::vector<uint8_t>& n = nibbles();
    const bool latin = m_.host_.preference("noteNames", "0") == "1";
    const auto names = latin ? ymseq::NoteNames::Latin : ymseq::NoteNames::English;

    for (int i = 0; i < ymengine::kDigidrumSlots; ++i) {
        Button& b = slots_[size_t(i)];
        b.setOn(i == k);
        const DigidrumSlotState& o = m_.extras_.digiSlots[size_t(i)];
        const size_t len = m_.extras_.digidrums[size_t(i)].size();
        std::string tip = "Slot " + std::to_string(i + 1);
        if (!o.name.empty()) tip += " \"" + o.name + "\"";
        tip += len ? ": " + std::to_string(len) + " values" : std::string(": empty");
        b.setTip(tip);
    }
    name_.setText(s.name.empty() ? std::string("(none)") : s.name);
    const ymengine::SampleSpan part = ymengine::sampleSpan(s.play.start, s.play.end, uint32_t(n.size()));
    wave_.setSample(k, n, s.play.rateHz, s.play.loop, part.start, part.end, s.name);

    if (m_.dragging_ != &rate_) rate_.setValue(s.play.rateHz);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.0f Hz", s.play.rateHz);
    if (!ratev_.editing()) ratev_.setText(buf);
    for (size_t i = 0; i < presets_.size(); ++i) presets_[i].setOn(s.play.rateHz == kPresetHz[i]);
    if (m_.dragging_ != &root_) root_.setValue(s.play.root);
    if (!rootv_.editing()) rootv_.setText(ymseq::absNoteName(s.play.root, names));
    if (m_.dragging_ != &fine_) fine_.setValue(s.play.cents);
    if (!finev_.editing()) finev_.setText(signedText(int(std::lround(s.play.cents))) + " ct");

    // Start runs over the values (0 to length - 1), End over the positions after them (1 to length); the ranges keep
    // at least one step for an empty slot. They set the part played with or without Loop (Indus, 26/10/2026, D-443).
    const bool has = !n.empty();
    const double len = double(n.size());
    start_.setRange(0, std::max(1.0, len - 1), 1);
    end_.setRange(std::min(1.0, len), std::max(1.0, len), 1);
    if (m_.dragging_ != &start_) start_.setValue(part.start);
    if (m_.dragging_ != &end_) end_.setValue(part.end);
    if (!startv_.editing()) startv_.setText(std::to_string(part.start));
    if (!endv_.editing()) endv_.setText(std::to_string(part.end));
    loop_.setOn(s.play.loop);
    play_.setOn(looping());
    loop_.setEnabled(has);
    for (gemui::Slider* sl : {&start_, &end_}) sl->setEnabled(has);
    for (gemui::Label* l : {&labStart_, &labEnd_}) l->setDim(!has);
    for (gemui::ValueField* f : {&startv_, &endv_}) f->setDim(!has);
    linear_.setOn(s.convert == DigiConvert::Linear);
    ym_.setOn(s.convert == DigiConvert::YmLevels);
    for (Button* b : {&play_, &clear_, &linear_, &ym_, &normalize_, &reverse_, &fade_, &sign_, &gainDown_, &gainUp_})
        b->setEnabled(has);
    labTools_.setDim(!has);
    labConvert_.setDim(!has);
    layout();
}

// ------------------------------------------------------------------------------------------- layout

// The two notes and a rule across the window, then two columns side by side: the slot settings (col1 px wide) and the
// sample (col2); the window takes the height of the taller column. Rows are flexRow lines without wrapping.
void DigidrumWindow::layout() {
    const double ox = box().x, oy = box().y;
    const double x = ox + 14;  // content (border 2, margin 4, padding 8)
    auto btn = [](Button& b) { return FlexItem{&b, b.naturalWidth(), double(b.naturalHeight())}; };
    auto lab = [](gemui::Label& l, double w = 0) { return FlexItem{&l, w > 0 ? w : l.naturalWidth(), 16}; };
    auto sld = [](gemui::Slider& s, double w) { return FlexItem{&s, w, 16}; };
    auto val = [](gemui::ValueField& v, double w) { return FlexItem{&v, w, 16}; };

    const double col1 = 262, col2 = 360;
    const double x2 = x + col1 + 24;
    const double contentW = std::max({col1 + 24 + col2, hint_.naturalWidth(), hint2_.naturalWidth()});

    double y = oy + 32 + kPad;  // below the frame (6) and the title bar (26)
    hint_.setBox({x, y, hint_.naturalWidth(), 12});
    y += 12;
    hint2_.setBox({x, y, hint2_.naturalWidth(), 12});
    y += 12 + 8;
    sep_.setBox({x - kPad, y, contentW + 2 * kPad, 2});
    y += 2 + 8;

    // Slot column: labels as wide as "Name", typed values as wide as the longest rate; the presets line up under the
    // Rate slider.
    const double y0 = y;
    double y1 = y0;
    const double lw = Font::width("Name", 14), vw = Font::width("20000 Hz", 14);
    std::vector<FlexItem> sl = {lab(labSlot_, lw)};
    for (auto& b : slots_) sl.push_back(btn(b));
    y1 += flexRow(x, y1, col1, sl, 4, false) + 6;
    std::vector<FlexItem> nm = {lab(labName_, lw), btn(name_)};
    y1 += flexRow(x, y1, col1, nm, kGap, false) + 10;
    std::vector<FlexItem> rt = {lab(labRate_, lw), sld(rate_, 134), val(ratev_, vw)};
    y1 += flexRow(x, y1, col1, rt, kGap, false) + 6;
    std::vector<FlexItem> pr;
    for (auto& b : presets_) pr.push_back(btn(b));
    pr.front().marginLeft = lw + kGap;
    y1 += flexRow(x, y1, col1, pr, 4, false) + 6;
    std::vector<FlexItem> rr = {lab(labRoot_, lw), sld(root_, 134), val(rootv_, vw)};
    y1 += flexRow(x, y1, col1, rr, kGap, false) + 6;
    std::vector<FlexItem> fn = {lab(labFine_, lw), sld(fine_, 134), val(finev_, vw)};
    y1 += flexRow(x, y1, col1, fn, kGap, false) + 10;
    std::vector<FlexItem> bk = {btn(banksBtn_)};
    y1 += flexRow(x, y1, col1, bk, kGap, false);

    // Sample column: its label, with Load, Play and Clear flush right on the same line; the 360 × 72 drawing; Loop,
    // Start and End, End's slider under Start's; conversion and tools.
    double y2 = y0;
    labSample_.setBox({x2, y2 + 2, labSample_.naturalWidth(), 16});
    std::vector<FlexItem> io = {btn(load_), btn(play_), btn(clear_)};
    const double ioW = load_.naturalWidth() + play_.naturalWidth() + clear_.naturalWidth() + 8;
    y2 += flexRow(x2 + col2 - ioW, y2, ioW, io, 4, false) + 6;
    wave_.setBox({x2, y2, 360, 72});
    y2 += 72 + 8;
    const double lw2 = Font::width("Tools", 14);
    std::vector<FlexItem> lp = {btn(loop_), lab(labStart_), sld(start_, 104), val(startv_, Font::width("00000", 14))};
    y2 += flexRow(x2, y2, col2, lp, kGap, false) + 6;
    std::vector<FlexItem> le = {lab(labEnd_), sld(end_, 104), val(endv_, Font::width("00000", 14))};
    le.front().marginLeft = loop_.naturalWidth() + kGap + labStart_.naturalWidth() - labEnd_.naturalWidth();
    y2 += flexRow(x2, y2, col2, le, kGap, false) + 10;
    std::vector<FlexItem> cv = {lab(labConvert_, lw2), btn(linear_), btn(ym_)};
    y2 += flexRow(x2, y2, col2, cv, 4, false) + 6;
    std::vector<FlexItem> tl = {lab(labTools_, lw2), btn(normalize_), btn(reverse_), btn(fade_)};
    y2 += flexRow(x2, y2, col2, tl, 4, false) + 6;
    std::vector<FlexItem> gn = {btn(sign_), btn(gainDown_), btn(gainUp_)};
    gn.front().marginLeft = lw2 + 4;
    y2 += flexRow(x2, y2, col2, gn, 4, false);
    y = std::max(y1, y2);

    // Window: content, padding of 8, border and margin (6 on each side); placed again only when its size changes.
    const double w = contentW + 2 * kPad + 12;
    const double h = y + kPad + 6 - oy;
    if (std::abs(h - box().h) > 0.01 || std::abs(w - box().w) > 0.01) place({ox, oy, w, h});
    repaint();
}

std::optional<Rect> DigidrumWindow::rectOf(std::string_view id) const {
    if (!isOpen()) return std::nullopt;
    if (id == "digWin") return bounds();
    // Index k of an id written `prefix` then one digit k below n; -1 otherwise.
    auto digit = [&](std::string_view prefix, int n) -> int {
        if (id.size() != prefix.size() + 1 || id.substr(0, prefix.size()) != prefix) return -1;
        const int k = id.back() - '0';
        return k >= 0 && k < n ? k : -1;
    };
    if (const int k = digit("digSlot", ymengine::kDigidrumSlots); k >= 0) return slots_[size_t(k)].bounds();
    if (const int k = digit("digPreset", int(presets_.size())); k >= 0) return presets_[size_t(k)].bounds();
    struct Named {
        std::string_view id;
        const gemui::Widget* w;
    };
    const Named named[] = {{"digName", &name_},         {"digBanks", &banksBtn_},   {"digWave", &wave_},
                           {"digLoad", &load_},         {"digPlay", &play_},        {"digClear", &clear_},
                           {"digRate", &rate_},         {"digRoot", &root_},        {"digFine", &fine_},
                           {"digLoop", &loop_},         {"digStart", &start_},      {"digEnd", &end_},
                           {"digLinear", &linear_},     {"digYm", &ym_},            {"digNormalize", &normalize_},
                           {"digReverse", &reverse_},   {"digFade", &fade_},        {"digSign", &sign_},
                           {"digGainDown", &gainDown_}, {"digGainUp", &gainUp_},    {"digRateV", &ratev_},
                           {"digRootV", &rootv_},       {"digFineV", &finev_},      {"digStartV", &startv_},
                           {"digEndV", &endv_}};
    for (const Named& n : named)
        if (id == n.id) return n.w->visible() ? std::optional<Rect>(n.w->bounds()) : std::nullopt;
    return std::nullopt;
}

const gemui::Slider* DigidrumWindow::slider(std::string_view id) const noexcept {
    if (id == "digRate") return &rate_;
    if (id == "digRoot") return &root_;
    if (id == "digFine") return &fine_;
    if (id == "digStart") return &start_;
    if (id == "digEnd") return &end_;
    return nullptr;
}

std::string DigidrumWindow::valueText(std::string_view id) const {
    if (id == "digRate") return ratev_.text();
    if (id == "digRoot") return rootv_.text();
    if (id == "digFine") return finev_.text();
    if (id == "digStart") return startv_.text();
    if (id == "digEnd") return endv_.text();
    return {};
}

}  // namespace stym::pluginui
