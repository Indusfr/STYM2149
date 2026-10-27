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
// own, SMP and TUN (items 146, 152 and 153, D-355, D-361, D-362). A slide has a speed from 0 (stop) to 15 and an
// Up/Down button, which write xy: x0 up, 0y down (D-391; one slider over xy from 0 to 255 before, D-385). In 16
// channels, the button of the Buzzer and Noise slides is their Curve: a menu of Up, Down and the shapes of the CRV row,
// written as Kxx and Qxx with the slide (D-394); it shows the direction and the shape (Up, Down EXP). Changing a value
// turns its row on. Cancel is the default button (bold border, Enter and Escape); OK writes every row. A GEM box like
// the alerts: double frame, centred at 38 % of the desktop height, over the veil of the modal boxes.
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
    // Rows on screen, in the order of the tenth DAW test (items 122 and 130): the kinds of the CMD row
    // (ymengine::seqCmdInBox).
    static constexpr std::array<ymengine::SeqCmd, 5> kOrder = {ymengine::SeqCmd::Speed, ymengine::SeqCmd::Sync,
                                                               ymengine::SeqCmd::VolSlide, ymengine::SeqCmd::EnvSlide,
                                                               ymengine::SeqCmd::NoiseSlide};
    struct Spec {
        int step = 0;  // shown from 1
        Entries entries{};
        // Rows of kOrder in the box: a slide that cannot act from the step is left out (D-386); its entry stays as
        // the step has it.
        std::array<bool, kOrder.size()> shown = {true, true, true, true, true};
        // 16 channels: the Buzzer and Noise slides take a curve (D-394); OK then sets their EnvCurve and NoiseCurve
        // entries (on with a shape other than Linear and the slide on).
        bool curves = false;
        std::function<void(const Entries&)> ok;
    };
    static constexpr int kCancel = 0, kOk = 1;

    static CmdBox& open(gemui::Desktop& d, Spec spec);
    explicit CmdBox(Spec spec);
    ~CmdBox() override;

    // Controls (tests, screenshots). shown: the row of k is in the box.
    const Entry& entry(ymengine::SeqCmd k) const noexcept { return entries_[slot(k)]; }
    gemui::Button& onOff(ymengine::SeqCmd k) noexcept { return rows_[row(k)].onOff; }
    gemui::Slider& slider(ymengine::SeqCmd k) noexcept { return rows_[row(k)].slider; }
    gemui::Button& direction(ymengine::SeqCmd k) noexcept { return rows_[row(k)].dir; }  // a slide's Up/Down or Curve
    int curve(ymengine::SeqCmd k) const noexcept { return rows_[row(k)].curve; }       // ymengine::SlideCurve
    bool shown(ymengine::SeqCmd k) const noexcept { return spec_.shown[row(k)]; }
    gemui::ValueField& field(ymengine::SeqCmd k) noexcept { return rows_[row(k)].field; }
    gemui::Label& label(ymengine::SeqCmd k) noexcept { return rows_[row(k)].label; }
    gemui::Button& button(int i) noexcept { return i == kOk ? ok_ : cancel_; }

    void paint(gemui::Painter& p) override;
    bool acceptsMouse() const noexcept override { return true; }
    bool keyDown(const gemui::KeyEvent& e) override;

private:
    struct Row {
        gemui::Label label;
        gemui::Slider slider;
        gemui::ValueField field;
        gemui::Button onOff;
        gemui::Button dir;  // a slide's Up/Down, or its Curve menu
        bool up = true;     // its direction, kept at speed 0
        int curve = 0;      // its shape (Linear)
    };
    static size_t slot(ymengine::SeqCmd k) noexcept { return size_t(int(k) - 1); }
    static size_t row(ymengine::SeqCmd k) noexcept;
    void layout(const gemui::Desktop& d);
    // Shows entry k: its slider and field, and its On/Off button.
    void show(ymengine::SeqCmd k);
    void change(ymengine::SeqCmd k, int value);
    // A slide's speed (0-15) in its row's direction.
    void changeSpeed(ymengine::SeqCmd k, int speed);
    // The Curve menu of a slide (16 channels, D-394), under its button.
    void curveMenu(ymengine::SeqCmd k);
    bool hasCurve(ymengine::SeqCmd k) const noexcept {
        return spec_.curves && ymengine::seqSlideCurve(k) != ymengine::SeqCmd::None;
    }
    void finish(int button);

    Spec spec_;
    Entries entries_;
    std::array<Row, kOrder.size()> rows_;
    gemui::Button cancel_{"Cancel", gemui::Button::Style::Default}, ok_{"OK"};
    std::string title_;
    double titleTop_ = 0;
};

}  // namespace stym::pluginui
