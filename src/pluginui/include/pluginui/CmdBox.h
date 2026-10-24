// STYM2149 — pluginui: box of the step commands (CMD row of the Step Sequencer)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Tenth DAW test, items 121, 122 and 128 (D-329, D-330, D-336): every command of the step in a list, one row each with
// its own value and an On/Off button: Envelope Restart (Rxx: tick of the line), Ticks per Line (Sxx), Sync Mark (Zxx),
// then the effects of item 130 (D-338): Volume Slide (Axy), Buzzer Slide (Bxy), Noise Slide (Nxy), Sample Offset
// (Oxx) and Fine Tune (Fxx, shown in cents). Eleventh DAW test: Arpeggio (Xyz) and Pitch Slide (Pxy) are retired
// (items 131 and 135, D-340, D-344: the ARP and SLD rows do it); Vibrato, BPM, Envelope Shape, Note Delay and Note Cut
// have rows of their own in the Step Sequencer (VIB, BPM, ENV, DLY, CUT: items 132, 133 and 138 to 140, D-341, D-342,
// D-347 to D-349). Twelfth DAW test: Envelope Restart goes to the ENV row, Sample Offset and Fine Tune to rows of their
// own, SMP and TUN (items 146, 152 and 153, D-355, D-361, D-362). The slides have two half sliders (up x and down y)
// and a field typed in hexadecimal. Changing a value turns its row on. Cancel is the default button (bold border, Enter
// and Escape); OK writes every row. A GEM box like the alerts: double frame, centred at 38 % of the desktop height,
// over the veil of the modal boxes.
#pragma once
#include <array>
#include <functional>
#include <memory>
#include <string>

#include "gemui/Controls.h"
#include "gemui/Desktop.h"
#include "ymengine/Sequence.h"

namespace stym::pluginui {

class CmdBox : public gemui::WindowFrame {
public:
    // Command of one kind: present at the step, and its parameter xx (kept when the row is off).
    struct Entry {
        bool on = false;
        int value = 0;
    };
    using Entries = std::array<Entry, ymengine::kSeqCmdKinds>;  // index: kind - 1
    struct Spec {
        int step = 0;  // shown from 1
        Entries entries{};
        std::function<void(const Entries&)> ok;
    };
    // Rows on screen, in the order of the tenth DAW test (items 122 and 130): the kinds of the CMD row
    // (ymengine::seqCmdInBox).
    static constexpr std::array<ymengine::SeqCmd, 5> kOrder = {ymengine::SeqCmd::Speed, ymengine::SeqCmd::Sync,
                                                               ymengine::SeqCmd::VolSlide, ymengine::SeqCmd::EnvSlide,
                                                               ymengine::SeqCmd::NoiseSlide};
    static constexpr int kCancel = 0, kOk = 1;

    static CmdBox& open(gemui::Desktop& d, Spec spec);
    explicit CmdBox(Spec spec);
    ~CmdBox() override;

    // Controls (tests, screenshots). part: 0 or 1 for the two sliders of the slides (up x, down y), 0 elsewhere.
    const Entry& entry(ymengine::SeqCmd k) const noexcept { return entries_[slot(k)]; }
    gemui::Button& onOff(ymengine::SeqCmd k) noexcept { return rows_[row(k)].onOff; }
    gemui::Slider& slider(ymengine::SeqCmd k, int part = 0) noexcept { return rows_[row(k)].sliders[size_t(part)]; }
    gemui::ValueField& field(ymengine::SeqCmd k) noexcept { return rows_[row(k)].field; }
    gemui::Label& label(ymengine::SeqCmd k) noexcept { return rows_[row(k)].label; }
    gemui::Button& button(int i) noexcept { return i == kOk ? ok_ : cancel_; }

    void paint(gemui::Painter& p) override;
    bool acceptsMouse() const noexcept override { return true; }
    bool keyDown(const gemui::KeyEvent& e) override;

private:
    struct Row {
        gemui::Label label;
        std::array<gemui::Slider, 2> sliders;
        gemui::ValueField field;
        gemui::Button onOff;
    };
    static size_t slot(ymengine::SeqCmd k) noexcept { return size_t(int(k) - 1); }
    static size_t row(ymengine::SeqCmd k) noexcept;
    void layout(const gemui::Desktop& d);
    // Shows entry k: its sliders and field, and its On/Off button.
    void show(ymengine::SeqCmd k);
    void change(ymengine::SeqCmd k, int value);
    void finish(int button);

    Spec spec_;
    Entries entries_;
    std::array<Row, kOrder.size()> rows_;
    gemui::Button cancel_{"Cancel", gemui::Button::Style::Default}, ok_{"OK"};
    std::string title_;
    double titleTop_ = 0;
};

}  // namespace stym::pluginui
