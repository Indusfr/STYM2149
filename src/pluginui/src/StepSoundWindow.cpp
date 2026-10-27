// STYM2149 — pluginui: settings of the sound a step chooses (right click on GEN, SRC or ENV, D-392)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "pluginui/StepSoundWindow.h"

#include <algorithm>
#include <cmath>

#include "gemui/Font.h"
#include "pluginui/IconData.h"
#include "pluginui/MainWindow.h"
#include "ymengine/Params.h"

namespace stym::pluginui {

using gemui::Rect;
using ymengine::Generator;

namespace {

constexpr double kFrameW = 204.5;  // the usable width of a main window frame: the narrowest row
constexpr double kPad = 12, kGap = 8, kRowMargin = 6;
constexpr double kStepGap = 4;  // between the Sync Buzzer step buttons

}  // namespace

StepSoundWindow::StepSoundWindow(MainWindow& main) : m_(main) {
    hint_.setColor(gemui::colors::kHint);
    hint_.setFace(gemui::Face::kSmall);
    none_.setFace(gemui::Face::kSmall);
    place({double(kX), double(kY), kFrameW + 2 * kPad + 2 * kInset, 120});
}

StepSoundWindow::~StepSoundWindow() { clear(); }

void StepSoundWindow::clear() {
    lines_.clear();
    // The controls are owned here: detach them before they go.
    for (auto& r : rows_)
        for (gemui::Widget* w :
             std::initializer_list<gemui::Widget*>{&r->label, &r->slider, &r->pre, &r->field, &r->unit})
            if (w->parent() == this) remove(*w);
    for (auto& b : buttons_)
        if (b->button.parent() == this) remove(b->button);
    for (auto& h : headings_)
        if (h->parent() == this) remove(*h);
    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&wave_, &none_, &hint_})
        if (w->parent() == this) remove(*w);
    rows_.clear();
    buttons_.clear();
    headings_.clear();
}

void StepSoundWindow::openFor(Kind kind, int step, int value) {
    kind_ = kind;
    track_ = m_.cur_;
    step_ = step;
    value_ = value;
    clear();
    build();
    open();
    refresh();
}

StepSoundWindow::SliderRow& StepSoundWindow::addSlider(std::string id, const char* label, gemui::Slider& src,
                                                       gemui::ValueField& srcField, gemui::Label* srcLabel,
                                                       FieldSpec spec, bool above) {
    auto row = std::make_unique<SliderRow>();
    SliderRow& r = *row;
    r.id = std::move(id);
    r.label.setText(label);
    r.src = &src;
    r.srcField = &srcField;
    r.srcLabel = srcLabel;
    r.above = above;
    r.slider.setRange(src.min(), src.max(), 1);
    r.slider.setTip(src.tip());
    r.label.setTip(src.tip());
    r.field.setMinChars(spec.minChars);
    if (spec.pad) r.field.setPadChars(spec.pad);
    if (!spec.mask.empty()) r.field.setMask(spec.mask);
    r.field.setUnit(srcField.unit());  // out of the reverse video, as in the main window (D-464)
    r.field.setTip(srcField.tip());
    // The main window's callbacks: its parameter, its gesture, its undo step.
    gemui::Slider* s = &src;
    r.slider.onGestureStart = [&r, s] {
        r.held = true;
        if (s->onGestureStart) s->onGestureStart();
    };
    r.slider.onChange = [s](double v) {
        if (s->onChange) s->onChange(v);
    };
    r.slider.onGestureEnd = [&r, s] {
        r.held = false;
        if (s->onGestureEnd) s->onGestureEnd();
    };
    gemui::ValueField* f = &srcField;
    r.field.onCommit = [f](const std::string& t) { return f->onCommit && f->onCommit(t); };
    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&r.label, &r.slider, &r.pre, &r.field, &r.unit})
        add(*w);
    rows_.push_back(std::move(row));
    return r;
}

StepSoundWindow::ButtonItem& StepSoundWindow::addButton(std::string id, gemui::Button& src,
                                                        std::function<void(gemui::Button&)> click) {
    auto item = std::make_unique<ButtonItem>();
    ButtonItem& b = *item;
    b.id = std::move(id);
    b.src = &src;
    b.button.setTip(src.tip());
    if (click) b.button.onClick = [&b, click] { click(b.button); };
    else b.button.onClick = [s = &src] { if (s->onClick) s->onClick(); };
    b.button.onContext = [s = &src](gemui::Point p) { if (s->onContext) s->onContext(p); };
    add(b.button);
    buttons_.push_back(std::move(item));
    return b;
}

gemui::Label& StepSoundWindow::addHeading(const char* text) {
    headings_.push_back(std::make_unique<gemui::Label>(text));
    add(*headings_.back());
    return *headings_.back();
}

void StepSoundWindow::build() {
    MainWindow& m = m_;
    // A slider row placed at (x, y): the label before it, or on a line of its own above it.
    const auto placeSlider = [this](SliderRow& r, double x, double y) {
        double h = 0;
        if (r.above) {
            r.label.setBox({x, y, r.label.naturalWidth(), 16});
            y += 16 + kRowMargin;
            h += 16 + kRowMargin;
        }
        double cx = x;
        const double lw = r.above ? 0 : std::max(r.labelW, r.label.naturalWidth());
        if (!r.above) {
            r.label.setBox({cx, y, lw, 16});
            cx += lw + kGap;
        }
        const auto tail = [](const SliderRow& t) {  // what follows the slider: "x", the field, the unit
            return (t.pre.text().empty() ? 0 : t.pre.naturalWidth() + kGap) + t.field.naturalWidth() +
                   (t.unit.text().empty() ? 0 : t.unit.naturalWidth());
        };
        const double tailW = std::max(tail(r), r.alignTo ? tail(*r.alignTo) : 0);
        const double sw = std::max(0.0, x + rowW_ - cx - tailW - kGap);
        r.slider.setBox({cx, y, sw, 16});
        if (!r.pre.text().empty()) r.pre.setBox({cx + sw + kGap, y, r.pre.naturalWidth(), 16});
        // The field and its unit at the end of the row.
        const double unitW = r.unit.text().empty() ? 0 : r.unit.naturalWidth();
        const double fw = r.field.naturalWidth(), fx = x + rowW_ - unitW - fw;
        r.field.setBox({fx, y, fw, 16});
        r.unit.setBox({fx + fw, y, unitW, 16});
        return h + 16;
    };
    // A line with one slider row.
    const auto sliderLine = [this, placeSlider](SliderRow& r) {
        lines_.push_back([&r, placeSlider](double x, double y) { return placeSlider(r, x, y); });
    };
    const auto headingLine = [this](gemui::Label& l) {
        lines_.push_back([&l](double x, double y) {
            l.setBox({x, y, l.naturalWidth(), 16});
            return 16.0;
        });
    };
    // Buttons side by side, centred in the row.
    const auto buttonsLine = [this](std::vector<ButtonItem*> bs) {
        lines_.push_back([this, bs](double x, double y) {
            double w = 0, h = 0;
            for (ButtonItem* b : bs) {
                w += b->button.naturalWidth() + (w > 0 ? kGap : 0);
                h = std::max(h, double(b->button.naturalHeight()));
            }
            double cx = x + std::round((rowW_ - w) / 2);
            for (ButtonItem* b : bs) {
                b->button.setBox({cx, y, b->button.naturalWidth(), h});
                cx += b->button.naturalWidth() + kGap;
            }
            return h;
        });
    };

    if (kind_ == Kind::Generator) {
        gen_ = value_ >= 0 ? value_ : m.seq_.stepSound(step_).gen;
        if (gen_ == int(Generator::Sid)) {
            SliderRow& ratio =
                addSlider("ratio", "Timer ratio", m.ratio_, m.ratiov_, &m.labRatio_, {4, 0, "9.99"}, true);
            ratio.pre.setText("x");
            sliderLine(ratio);
            SliderRow& vmin = addSlider("vmin", "SID min vol", m.vmin_, m.vminv_, &m.labVmin_, {2, 3, ""}, true);
            vmin.alignTo = &ratio;
            sliderLine(vmin);
            // The menu of its forms after its name (24/10/2026, D-411), as in the main window.
            gemui::Label& waveHead = addHeading("SID wave");
            ButtonItem& form = addButton("waveForm", m.waveForm_, [&m](gemui::Button& b) { m.waveFormMenu(b); });
            form.button.setMinWidth(m.waveForm_.naturalWidth());  // as wide as the widest form
            lines_.push_back([&waveHead, &form](double x, double y) {
                const double h = form.button.naturalHeight();
                waveHead.setBox({x, y + (h - 16) / 2, waveHead.naturalWidth(), 16});
                form.button.setBox({x + waveHead.naturalWidth() + kGap, y, form.button.naturalWidth(), h});
                return h;
            });
            wave_.onSet = m.sidWave_.onSet;
            wave_.onWheel = m.sidWave_.onWheel;
            wave_.onMenu = m.sidWave_.onMenu;
            wave_.setTip(m.sidWave_.tip());
            add(wave_);
            lines_.push_back([this](double x, double y) {
                wave_.setBox({x + std::round((rowW_ - SidWaveGrid::kWidth) / 2), y, double(SidWaveGrid::kWidth),
                              double(SidWaveGrid::kHeight)});
                return double(SidWaveGrid::kHeight) + 2;
            });
            // Steps on the line of its label, then right under it, in the same place, the period of the sine with
            // Sine or Bounce, or the width of the square with Square, the three sliders lined up after the widest
            // label, as in the main window (Indus, 24/10/2026 15:50 to 18:41, D-406, D-408, D-411, D-412, D-414). The
            // line keeps its place when neither shows: the window does not change size. With Base, the bars and Steps
            // are hidden too, in their places (20:30, D-417).
            SliderRow& steps =
                addSlider("waveSteps", "Steps", m.waveSteps_, m.waveStepsv_, &m.labWaveSteps_, {2, 3, "99"}, false);
            SliderRow& period = addSlider("wavePeriod", "Period", m.wavePeriod_, m.wavePeriodv_, &m.labWavePeriod_,
                                          {2, 3, "99"}, false);
            SliderRow& width =
                addSlider("waveWidth", "Width", m.waveWidth_, m.waveWidthv_, &m.labWaveWidth_, {2, 3, "99"}, false);
            steps.showWith = [&m] { return m.waveForm() != MainWindow::WaveForm::Base; };
            period.showWith = [&m] { return ymengine::sineForm(m.waveForm()); };
            width.showWith = [&m] { return m.waveForm() == MainWindow::WaveForm::Square; };
            for (SliderRow* r : {&steps, &period, &width}) {
                r->labelW =
                    std::max({steps.label.naturalWidth(), period.label.naturalWidth(), width.label.naturalWidth()});
                r->alignTo = &ratio;
            }
            sliderLine(steps);
            lines_.push_back([&period, &width, placeSlider](double x, double y) {
                placeSlider(period, x, y);
                return placeSlider(width, x, y);
            });
        } else if (gen_ == int(Generator::SyncBuzzer)) {
            gemui::Label& tune = addHeading("Tuning");
            ButtonItem& tuned = addButton("buzTuned", m.buzTuned_);
            const double lw = tune.naturalWidth();
            lines_.push_back([&tune, &tuned](double x, double y) {
                const double h = tuned.button.naturalHeight();
                tune.setBox({x, y + (h - 16) / 2, tune.naturalWidth(), 16});
                tuned.button.setBox({x + tune.naturalWidth() + kGap, y, tuned.button.naturalWidth(), h});
                return h;
            });
            const struct {
                const char* id;
                const char* label;
                gemui::Slider& s;
                gemui::ValueField& f;
                gemui::Label& l;
                FieldSpec spec;
            } rows[] = {{"buzN", "N", m.buzN_, m.buzNv_, m.labBuzN_, {2, 0, "99"}},
                        {"buzD", "D", m.buzD_, m.buzDv_, m.labBuzD_, {2, 0, "99"}},
                        {"buzFine", "Fine", m.buzFine_, m.buzFinev_, m.labBuzFine_, {4, 0, ""}}};
            for (const auto& d : rows) {
                SliderRow& r = addSlider(d.id, d.label, d.s, d.f, &d.l, d.spec, false);
                r.labelW = lw;
                sliderLine(r);
            }
            buttonsLine({&addButton("buzPad", m.buzPad_),
                         &addButton("buzPresets", m.buzPresets_, [&m](gemui::Button& b) { m.buzPresetsMenu(b); })});
            headingLine(addHeading("Sync Buzzer steps"));
            std::vector<ButtonItem*> steps;
            for (int k = 0; k < ymengine::kBuzzSteps; ++k)
                steps.push_back(&addButton("buzzStep" + std::to_string(k), m.buzzStep_[size_t(k)],
                                           [&m, k](gemui::Button& b) { m.buzzStepMenu(k, b); }));
            lines_.push_back([this, steps](double x, double y) {
                // Four buttons of the GENERATOR frame's size, centred.
                const double bw = std::floor((kFrameW - 3 * kStepGap) / 4), bh = icons::kShapes[0].h + 8;
                const double bx = x + std::round((rowW_ - (4 * bw + 3 * kStepGap)) / 2);
                for (size_t k = 0; k < steps.size(); ++k)
                    steps[k]->button.setBox({bx + double(k % 4) * (bw + kStepGap), y + double(k / 4) * (bh + kStepGap),
                                             bw, bh});
                return 2 * bh + kStepGap;
            });
            SliderRow& len = addSlider("buzzLen", "Steps", m.buzzLen_, m.buzzLenv_, &m.labBuzzLen_, {1, 0, "9"}, false);
            len.unit.setText(" steps");  // the widest text: the row keeps its place (refresh follows the main one)
            sliderLine(len);
        } else {
            add(none_);
            lines_.push_back([this](double x, double y) {
                none_.setBox({x, y, rowW_, 12});
                return 12.0;
            });
        }
    } else if (kind_ == Kind::Source) {
        gen_ = m.seq_.stepSound(step_).gen;
        // The cell's source, or the track's (0): what it uses (SRC menu: Tone, Noise, Tone+Noise, then the same with
        // the envelope, D-277).
        bool noise = false, env = false;
        if (value_ > 0) {
            const int bits = value_ < 4 ? value_ : value_ - 4;
            noise = (bits & 2) != 0;
            env = value_ >= 4;
        } else {
            noise = m.iv(plugin::Track::Noise) != 0;
            env = m.iv(plugin::Track::VolMode) == 1;
        }
        const bool buzzer = gen_ == int(Generator::SyncBuzzer);  // the envelope alone (restoreMix)
        SliderRow& np = addSlider("np", "Noise period", m.np_, m.npv_, nullptr, {2, 1, ""}, true);
        np.ownDim = true;
        np.ownDimmed = !noise || buzzer;
        sliderLine(np);
        SliderRow& vol = addSlider("vol", "Volume", m.vol_, m.volv_, nullptr, {2, 1, ""}, false);
        vol.ownDim = true;
        vol.ownDimmed = (env && gen_ != int(Generator::Sid)) || buzzer;
        vol.labelW = 58;
        sliderLine(vol);
        SliderRow& level = addSlider("level", "Level", m.level_, m.levelv_, nullptr, {3, 0, ""}, false);
        level.labelW = 58;
        sliderLine(level);
    } else {
        gen_ = m.seq_.stepSound(step_).gen;
        SliderRow& ef = addSlider("ef", "Env freq", m.ef_, m.efv_, &m.labEf_, {4, 0, "999.9"}, true);
        ef.unit.setText("Hz");
        sliderLine(ef);
        gemui::Label& ep = addHeading("EP =");
        auto epRow = std::make_unique<SliderRow>();
        SliderRow& r = *epRow;
        r.id = "ep";
        r.srcField = &m.epv_;
        r.srcLabel = &m.labEp_;
        r.field.setMinChars(8);
        r.field.setTip(m.epv_.tip());
        r.field.onCommit = [f = &m.epv_](const std::string& t) { return f->onCommit && f->onCommit(t); };
        add(r.field);
        rows_.push_back(std::move(epRow));
        lines_.push_back([&ep, &r](double x, double y) {
            ep.setBox({x, y, ep.naturalWidth(), 16});
            r.field.setBox({x + ep.naturalWidth() + kGap, y, r.field.naturalWidth(), 16});
            return 16.0;
        });
    }
    add(hint_);
    lines_.push_back([this](double x, double y) {
        hint_.setBox({x, y + 4, rowW_, 12});
        return 16.0;
    });
}

std::string StepSoundWindow::titleText() const {
    static constexpr const char* kKinds[] = {"GENERATOR", "SOURCE", "ENVELOPE"};
    std::string t = std::string(kKinds[int(kind_)]) + " - " + std::to_string(m_.cur_ + 1);
    if (kind_ == Kind::Generator) {
        static constexpr const char* kGens[] = {"Chip Voice", "SID Voice", "Digidrum",
                                                "Sync Buzzer", "Chipdrum", "DMA"};
        t += std::string(" [") + kGens[std::clamp(gen_, 0, 5)] + "]";
    }
    return t;
}

void StepSoundWindow::refresh() {
    if (!visible()) return;
    if (m_.cur_ != track_) {  // the cell was the other track's
        close();
        return;
    }
    setTitle(titleText());
    for (auto& rp : rows_) {
        SliderRow& r = *rp;
        const bool dimmed = r.ownDim && r.ownDimmed;
        if (r.src) {
            // The range too: Width goes up to Steps - 1 (D-411).
            if (r.slider.min() != r.src->min() || r.slider.max() != r.src->max())
                r.slider.setRange(r.src->min(), r.src->max(), 1);
            if (!r.held) r.slider.setValue(r.src->value());
            r.slider.setEnabled(r.ownDim ? !dimmed : r.src->enabled());  // the cell's source, not the track's
        }
        if (r.srcField) {
            if (!r.field.editing()) r.field.setText(r.srcField->text());
            r.field.setDim(r.ownDim ? dimmed : r.srcField->opacity() < 1);
        }
        r.label.setDim((!r.ownDim && r.srcLabel && r.srcLabel->dim()) || dimmed);
        r.pre.setDim(r.label.dim());
        r.unit.setDim(r.label.dim());
        // Steps hidden with Base (D-417), Period with Sine or Bounce, Width with Square (D-408, D-411, D-414): the
        // form of the track's SID wave, whatever its generator, unlike the visibility of the main window's rows.
        const bool shown = !r.showWith || r.showWith();
        for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&r.label, &r.slider, &r.pre, &r.field, &r.unit})
            w->setVisible(shown);
        if (r.id == "buzzLen") r.unit.setText(m_.buzzUnit_.text());
    }
    for (auto& bp : buttons_) {
        ButtonItem& b = *bp;
        b.button.setText(b.src->text());
        b.button.setIcon(b.src->icon());
        b.button.setOn(b.src->on());
        b.button.setEnabled(b.src->enabled());
    }
    if (wave_.parent() == this) {
        wave_.setState(m_.sidWave_.state());
        wave_.setVisible(m_.waveForm() != MainWindow::WaveForm::Base);
    }
    hint_.setText("Shared by every step of track " + std::to_string(m_.cur_ + 1));
    layout();
}

void StepSoundWindow::layout() {
    // Wide enough for the hint and for the title between the boxes of the bar.
    const double titleW = gemui::Font::width(titleText(), 16) + 2 * gemui::TitleBar::kBox + 16;
    rowW_ = std::max({kFrameW, std::ceil(hint_.naturalWidth()), std::ceil(titleW - 2 * kPad)});
    const Rect b = bounds();
    const gemui::Point o = contentOrigin();
    double y = o.y + kPad;
    for (size_t i = 0; i < lines_.size(); ++i)
        y += lines_[i](o.x + kPad, y) + (i + 1 < lines_.size() ? kRowMargin : 0);
    const double w = rowW_ + 2 * kPad + 2 * kInset, h = y + kPad - b.y + kInset;
    if (std::abs(h - b.h) > 0.5 || std::abs(w - b.w) > 0.5) {
        place({double(b.x), double(b.y), w, h});
        repaint();
    }
}

gemui::Slider* StepSoundWindow::slider(std::string_view id) noexcept {
    for (auto& r : rows_)
        if (r->id == id && r->src) return &r->slider;
    return nullptr;
}

gemui::ValueField* StepSoundWindow::field(std::string_view id) noexcept {
    for (auto& r : rows_)
        if ((r->src ? r->id + "v" : r->id) == id) return &r->field;
    return nullptr;
}

gemui::Button* StepSoundWindow::button(std::string_view id) noexcept {
    for (auto& b : buttons_)
        if (b->id == id) return &b->button;
    return nullptr;
}

const gemui::Label* StepSoundWindow::label(std::string_view id) const noexcept {
    for (auto& r : rows_)
        if (r->id == id) return &r->label;
    return nullptr;
}

std::optional<Rect> StepSoundWindow::rectOf(std::string_view id) const {
    if (!visible()) return std::nullopt;
    if (id == "stepWin") return bounds();
    if (id == "stepWave" && wave_.parent() == this && wave_.visible()) return wave_.bounds();
    if (id == "stepNone" && none_.parent() == this) return none_.bounds();
    auto* self = const_cast<StepSoundWindow*>(this);
    const auto shown = [](const gemui::Widget& w) {
        return w.showing() ? std::optional<Rect>(w.bounds()) : std::nullopt;
    };
    if (gemui::Slider* s = self->slider(id)) return shown(*s);
    if (gemui::ValueField* f = self->field(id)) return shown(*f);
    if (gemui::Button* b = self->button(id)) return shown(*b);
    return std::nullopt;
}

}  // namespace stym::pluginui
