// STYM2149 — plugin: JUCE editor, GEM window
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "PluginEditor.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>
#if JucePlugin_Build_Standalone
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#include "BuildId.h"
#include "KeyboardLayout.h"
#include "PluginProcessor.h"
#include "StandaloneSlot.h"
#include "gemui/Alert.h"
#include "gemui/Language.h"
#include "plugincore/Digidrum.h"
#include "plugincore/DmaSample.h"
#include "plugincore/State.h"
#include "plugincore/SteMemory.h"
#include "pluginui/IconData.h"

namespace stym::plugin {
namespace {

using pluginui::MainWindow;

juce::String str(std::string_view s) { return juce::String::fromUTF8(s.data(), int(s.size())); }

constexpr int kZoomMin = 50, kZoomMax = 200;
// Size of the editor at a zoom: the 100 % zoom shows the desktop at 5/8 of its logical size, half of it (first DAW
// test, item 5) made 25 % larger (second DAW test, item 15); 160 % shows it at its logical size.
constexpr int kZoomFull = 160;
constexpr int zoomed(int logicalSize, int zoom) noexcept { return logicalSize * zoom / kZoomFull; }

// Largest zoom: the window grows until it fills the user area of the largest display (an 8K screen, for
// one), 200 % at least, as before.
int maxZoom() {
    int best = kZoomMax;
    for (const auto& d : juce::Desktop::getInstance().getDisplays().displays) {
        const auto a = d.userBounds;
        const double z = std::min(a.getWidth() * kZoomFull / MainWindow::kWidth,
                                  a.getHeight() * kZoomFull / MainWindow::kHeight);
        best = std::max(best, int(std::floor(z)));
    }
    return best;
}

// Starting size when the window was never resized (the prototype's autoZoom, from the main display). The thresholds
// are the prototype's, compared here with the display's user area in logical pixels instead of the browser window.
int autoZoom() {
    const auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    if (d == nullptr) return 100;
    const auto a = d->userBounds;
    if (a.getWidth() < 1100.f || a.getHeight() < 700.f) return 50;
    if (a.getWidth() < 1400.f || a.getHeight() < 850.f) return 75;
    if (a.getWidth() > 2400.f) return 125;
    return 100;
}

// Drawn cursor of the prototype (pencil, scissors, bee) as an ARGB image: black, white or transparent pixels.
juce::MouseCursor drawnCursor(const pluginui::icons::Cursor& c) {
    juce::Image img(juce::Image::ARGB, c.mask.w, c.mask.h, true);
    for (int y = 0; y < c.mask.h; ++y)
        for (int x = 0; x < c.mask.w; ++x)
            if (c.mask.at(x, y)) img.setPixelAt(x, y, c.ink.at(x, y) ? juce::Colours::black : juce::Colours::white);
    return {img, c.hotX, c.hotY};
}

// System cursor for a gemui pointer. JUCE has no "cell" (thick cross), "not-allowed" or "context-menu": the thin
// cross and the arrow replace them.
juce::MouseCursor systemCursor(gemui::Pointer p) {
    switch (p) {
    case gemui::Pointer::Hand: return juce::MouseCursor::PointingHandCursor;
    case gemui::Pointer::Move: return juce::MouseCursor::DraggingHandCursor;
    case gemui::Pointer::ResizeH: return juce::MouseCursor::LeftRightResizeCursor;
    case gemui::Pointer::Crosshair:
    case gemui::Pointer::Cell: return juce::MouseCursor::CrosshairCursor;
    case gemui::Pointer::Pencil: {
        static const juce::MouseCursor pencil = drawnCursor(pluginui::icons::kCursorPencil);
        return pencil;
    }
    case gemui::Pointer::Scissors: {
        static const juce::MouseCursor scissors = drawnCursor(pluginui::icons::kCursorScissors);
        return scissors;
    }
    case gemui::Pointer::Busy: {
        static const juce::MouseCursor bee = drawnCursor(pluginui::icons::kCursorBee);
        return bee;
    }
    case gemui::Pointer::Arrow:
    case gemui::Pointer::NotAllowed:
    case gemui::Pointer::ContextMenu: return juce::MouseCursor::NormalCursor;
    }
    return juce::MouseCursor::NormalCursor;
}

gemui::Modifiers mods(const juce::ModifierKeys& m) {
    gemui::Modifiers r;
    r.shift = m.isShiftDown();
    r.ctrl = m.isCommandDown();  // Ctrl on Windows and Linux, Cmd on macOS
    r.alt = m.isAltDown();
    return r;
}

#if JucePlugin_Build_Standalone
juce::StandalonePluginHolder* standaloneHolder() { return juce::StandalonePluginHolder::getInstance(); }
#endif

}  // namespace

// File specific to the interface: Preferences.settings in the STYM2149 folder (~/.config/STYM2149 on Linux,
// Application Support/STYM2149 on macOS, %APPDATA%\STYM2149 on Windows). The JUCE standalone application keeps its
// audio and MIDI devices in STYM2149.settings: on Windows, that file is in this same folder, and up to 0.9.0 the
// preferences were in it too; each JUCE save then put back the preferences read at its launch. The new file takes
// over the preferences of the old one once.
std::unique_ptr<juce::PropertiesFile> StymEditor::userPreferences() {
    juce::PropertiesFile::Options o;
    o.applicationName = "Preferences";
#if JUCE_LINUX || JUCE_BSD
    o.folderName = "~/.config/STYM2149";
#else
    o.folderName = "STYM2149";
#endif
    o.filenameSuffix = ".settings";
    o.osxLibrarySubFolder = "Application Support";
    o.millisecondsBeforeSaving = -1;  // written by the editor itself, merged with the file (writePreferences)
    return openPreferences(o.getDefaultFile(), o);
}

std::unique_ptr<juce::PropertiesFile> StymEditor::openPreferences(const juce::File& file,
                                                                   const juce::PropertiesFile::Options& options) {
    const bool fresh = !file.existsAsFile();
    auto prefs = std::make_unique<juce::PropertiesFile>(file, options);
    const juce::File old = file.getSiblingFile("STYM2149.settings");
    if (fresh && old.existsAsFile()) {
        // Keys of the JUCE standalone application (StandalonePluginHolder, StandaloneFilterWindow): left out.
        static const juce::StringArray kStandaloneKeys{"audioSetup", "shouldMuteInput", "filterState",
                                                       "lastStateFile", "windowX", "windowY"};
        juce::PropertiesFile previous(old, options);
        const juce::StringPairArray& all = previous.getAllProperties();
        for (int i = 0; i < all.size(); ++i)
            if (!kStandaloneKeys.contains(all.getAllKeys()[i]))
                prefs->setValue(all.getAllKeys()[i], all.getAllValues()[i]);
        prefs->saveIfNeeded();
    }
    return prefs;
}

// Every copy of the standalone application and every instance of the plugin in hosts keep their preferences
// in memory: the file is read again just before writing, so that only the keys changed here replace those in the
// file. Two copies writing within the same instant could still lose one change; a person changes one copy at a time.
void StymEditor::writePreferences(juce::PropertiesFile& file, const juce::StringPairArray& changed) {
    file.reload();  // values read over the ones in memory (JUCE keeps keys missing from the file)
    for (int i = 0; i < changed.size(); ++i) file.setValue(changed.getAllKeys()[i], changed.getAllValues()[i]);
    file.saveIfNeeded();
}

void StymEditor::storePreference(const juce::String& key, const juce::String& value) {
    if (!prefs_) return;
    prefs_->setValue(key, value);
    if (unsavedPrefs_.size() == 0) prefsDueAt_ = juce::Time::getMillisecondCounterHiRes() + 500.0;
    unsavedPrefs_.set(key, value);
}

void StymEditor::savePreferences() {
    if (!prefs_ || unsavedPrefs_.size() == 0) return;
    writePreferences(*prefs_, unsavedPrefs_);
    unsavedPrefs_.clear();
}

StymEditor::StymEditor(StymProcessor& owner) : StymEditor(owner, userPreferences()) {}

StymEditor::StymEditor(StymProcessor& owner, std::unique_ptr<juce::PropertiesFile> preferences)
    : AudioProcessorEditor(owner), processor_(owner), prefs_(std::move(preferences)) {
    const int stored = prefs_->getIntValue("zoom", 0);
    zoomMax_ = maxZoom();
    // Size the window was last given, otherwise one from the display, not remembered (prototype); no larger than
    // the largest display now.
    zoom_ = stored >= kZoomMin ? std::min(stored, zoomMax_) : autoZoom();
    // Smooth pixels by default (Settings > Pixels; default again since the eleventh DAW test, item 130).
    cache_.setSharp(prefs_->getValue("pixels", "smooth") == "sharp");
    processor_.dropHostNotes();  // notes received while the window was closed: stale
    processor_.dropMonitorMidi();
    processor_.dropLearnMidi();  // likewise for learned messages (the window reads the map again and gives it back)
    window_ = std::make_unique<MainWindow>(static_cast<pluginui::Host&>(*this));
    setOpaque(true);
    setWantsKeyboardFocus(true);
    // Resizable by the host (or the standalone application's window), in the desktop's proportions, from 50 % up to
    // the largest display; without a handle drawn by JUCE in the GEM desktop.
    const juce::ScopedValueSetter<bool> applying(applyingZoom_, true);  // provisional sizes: nothing to remember
    setResizable(true, false);
    setResizeLimits(zoomed(MainWindow::kWidth, kZoomMin), zoomed(MainWindow::kHeight, kZoomMin),
                    zoomed(MainWindow::kWidth, zoomMax_), zoomed(MainWindow::kHeight, zoomMax_));
    getConstrainer()->setFixedAspectRatio(double(MainWindow::kWidth) / double(MainWindow::kHeight));
    applyZoom(zoom_);
    window_->tick(juce::Time::getMillisecondCounterHiRes());
    flush();
    startTimerHz(frameHz_);
#if JucePlugin_Build_Standalone
    // A MIDI input unplugged while a key is held, then plugged back: told by the device manager.
    if (auto* holder = standalone() ? standaloneHolder() : nullptr) {
        juce::StringArray available, enabled;
        scanMidiInputs(available, enabled);
        midiInputsSeen(available, enabled);
        holder->deviceManager.addChangeListener(this);
    }
#endif
}

StymEditor::~StymEditor() {
    stopTimer();
#if JucePlugin_Build_Standalone
    if (auto* holder = standalone() ? standaloneHolder() : nullptr) holder->deviceManager.removeChangeListener(this);
#endif
    // Notes held with the mouse must not stay stuck: on-screen playing is cut (CC 120).
    processor_.pushScreenMidi(0xB0, 120, 0);
    window_.reset();
    processor_.disarmMidiLearn();  // learn mode ends with the window
    savePreferences();
    if (prefs_) prefs_->saveIfNeeded();
}

void StymEditor::applyZoom(int percent) {
    const juce::ScopedValueSetter<bool> applying(applyingZoom_, true);
    zoom_ = std::clamp(percent, kZoomMin, zoomMax_);
    setSize(zoomed(MainWindow::kWidth, zoom_), zoomed(MainWindow::kHeight, zoom_));
    repaint();
}

void StymEditor::resized() {
    if (applyingZoom_) return;
    // Size given by the host or by the user: the zoom follows the width and becomes the remembered choice.
    zoom_ = std::clamp(int(std::lround(getWidth() * double(kZoomFull) / MainWindow::kWidth)), kZoomMin, zoomMax_);
    storePreference("zoom", juce::String(zoom_));
    repaint();
}

void StymEditor::parentHierarchyChanged() { applyRenderer(); }

// Windows: JUCE 8 shows a window through Direct2D by default, which uploads the whole image of the desktop to
// the graphics card at each paint, the image being drawn by the CPU (render). JUCE's software renderer copies only the
// repainted areas; it is the default. With Direct2D, Indus saw a copy of the standalone application's window flicker
// at the top right of the Windows desktop for a few seconds (24/10/2026 22:27). In a host, the window is the
// plugin's own (JUCE's wrapper), never the host's.
void StymEditor::applyRenderer() {
#if JUCE_WINDOWS
    juce::ComponentPeer* peer = getPeer();
    if (peer == nullptr) return;
    const bool direct2d = prefs_->getValue("renderer", "software") == "direct2d";
    const int engine = peer->getAvailableRenderingEngines().indexOf(direct2d ? "Direct2D" : "Software Renderer");
    if (engine >= 0 && engine != peer->getCurrentRenderingEngine()) {
        peer->setCurrentRenderingEngine(engine);
        image_ = {};  // drawn again whole at the next paint
        repaint();
    }
#endif
}

bool StymEditor::rendererChoice() const {
#if JUCE_WINDOWS
    return true;
#else
    return false;
#endif
}

juce::AffineTransform StymEditor::toScreen() const noexcept {
    return juce::AffineTransform::scale(float(getWidth()) / float(MainWindow::kWidth),
                                        float(getHeight()) / float(MainWindow::kHeight));
}

void StymEditor::paint(juce::Graphics& g) {
    // The image has the screen pixels of the component (display scale included): drawn without scaling, one
    // image pixel per screen pixel.
    const float s = g.getInternalContext().getPhysicalPixelScaleFactor();
    const int w = std::max(1, int(std::lround(float(getWidth()) * s)));
    const int h = std::max(1, int(std::lround(float(getHeight()) * s)));
    if (image_.getWidth() != w || image_.getHeight() != h) render(w, h);
    // A component spanning part of a screen pixel (1150 px at 125 %: 1437.5) leaves the image's last column or row
    // partly covered: black below it, as below a desktop of other proportions, rather than what lay there.
    if (std::abs(float(w) - float(getWidth()) * s) > 1e-3f || std::abs(float(h) - float(getHeight()) * s) > 1e-3f)
        g.fillAll(juce::Colours::black);
    g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
    g.drawImageTransformed(image_, juce::AffineTransform::scale(float(getWidth()) / float(w),
                                                                float(getHeight()) / float(h)));
}

void StymEditor::render(int w, int h) {
    image_ = juce::Image(juce::Image::ARGB, w, h, false, juce::SoftwareImageType());
    image_.clear(image_.getBounds(), juce::Colours::black);  // below the desktop if the proportions differ
    cache_.setScale(double(w) / MainWindow::kWidth);  // d: screen pixels per desktop pixel, from the width
    draw(window_->desktop().bounds());
}

juce::Rectangle<int> StymEditor::draw(const gemui::Rect& r) {
    if (!image_.isValid() || r.empty()) return {};
    gemui::Desktop& desk = window_->desktop();
    const double d = cache_.scale();
    // Only the screen pixels of r change, but the widgets around it are painted too: the anti-aliased edges of their
    // glyphs and icons can reach up to two screen pixels outside their boxes.
    const int margin = 2 + int(std::ceil(2 / d));  // desktop pixels: two screen pixels (2 / d), plus 2
    const gemui::Rect around = gemui::Rect{r.x - margin, r.y - margin, r.w + 2 * margin, r.h + 2 * margin}.intersect(
        desk.bounds());
    // The painter covers the whole desktop but may only change `area`, the screen pixels of r. A desktop pixel
    // narrower than a screen pixel still paints one (onScreen): the last pixel of r can land on the screen pixel after
    // r's, which is painted again too, or a dragged outline leaves traces (tenth DAW test, item 129).
    juce::Rectangle<int> area = JucePainter::onScreen(r.intersect(desk.bounds()), d);
    area = area.withSize(area.getWidth() + 1, area.getHeight() + 1).getIntersection(image_.getBounds());
    {
        JucePainter p(image_, desk.bounds(), cache_, area);
        desk.draw(p, around);
    }
    // Component area of these screen pixels.
    const double kx = double(getWidth()) / image_.getWidth(), ky = double(getHeight()) / image_.getHeight();
    const int x0 = int(std::floor(area.getX() * kx)), y0 = int(std::floor(area.getY() * ky));
    return juce::Rectangle<int>::leftTopRightBottom(x0, y0, int(std::ceil(area.getRight() * kx)),
                                                    int(std::ceil(area.getBottom() * ky)));
}

void StymEditor::flush() {
    const gemui::Pointer pointer = window_->desktop().pointer();
    if (pointer != pointer_) {
        pointer_ = pointer;
        setMouseCursor(systemCursor(pointer));
    }
    const std::vector<gemui::Rect>& zones = window_->desktop().takePending();
    if (zones.empty()) return;
    if (!image_.isValid()) {
        repaint();  // the first paint draws the whole desktop
        return;
    }
    for (const gemui::Rect& r : zones)
        if (const juce::Rectangle<int> area = draw(r); !area.isEmpty()) repaint(area);
}

void StymEditor::timerCallback() { frame(); }

void StymEditor::frame() { frame(juce::Time::getMillisecondCounterHiRes()); }

void StymEditor::frame(double now) {
    window_->tick(now);
    flush();
    if (unsavedPrefs_.size() > 0 && now >= prefsDueAt_) savePreferences();
    // 60 frames a second while the demo of the Information box plays.
    if (const int hz = window_->frameRate(); hz != frameHz_) {
        frameHz_ = hz;
        startTimerHz(hz);
    }
}

gemui::Point StymEditor::logical(const juce::MouseEvent& e) const noexcept {
    const auto p = e.position.transformedBy(toScreen().inverted());
    return {std::clamp(int(std::floor(p.x)), 0, MainWindow::kWidth - 1),
            std::clamp(int(std::floor(p.y)), 0, MainWindow::kHeight - 1)};
}

void StymEditor::mouseMove(const juce::MouseEvent& e) {
    window_->desktop().mouseMove(logical(e), mods(e.mods));
    flush();
}

void StymEditor::mouseDown(const juce::MouseEvent& e) {
    if (!hasKeyboardFocus(true)) grabKeyboardFocus();
    const gemui::MouseButton b = e.mods.isPopupMenu()          ? gemui::MouseButton::Right
                                 : e.mods.isMiddleButtonDown() ? gemui::MouseButton::Middle
                                                               : gemui::MouseButton::Left;
    window_->desktop().mouseDown(logical(e), b, mods(e.mods));
    flush();
}

bool StymEditor::isInterestedInFileDrag(const juce::StringArray& files) {
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    for (const juce::String& f : files) {
        const juce::File file(f);
        if (pluginui::MainWindow::isMidiFile(f.toStdString()) || file.hasFileExtension("raw;spl;sam") ||
            formats.findFormatForFileExtension(file.getFileExtension()) != nullptr)
            return true;
    }
    return false;
}

void StymEditor::filesDropped(const juce::StringArray& files, int x, int y) {
    std::vector<std::string> paths;
    for (const juce::String& f : files) paths.push_back(f.toStdString());
    // The point of the drop in logical pixels, as for the mouse: the drawing of a sample under it takes the file.
    const auto p = juce::Point<float>(float(x), float(y)).transformedBy(toScreen().inverted());
    const gemui::Point at{std::clamp(int(std::floor(p.x)), 0, MainWindow::kWidth - 1),
                          std::clamp(int(std::floor(p.y)), 0, MainWindow::kHeight - 1)};
    window_->dropFiles(paths, at);
    flush();
}

void StymEditor::mouseDrag(const juce::MouseEvent& e) {
    window_->desktop().mouseDrag(logical(e), mods(e.mods));
    flush();
}

void StymEditor::mouseUp(const juce::MouseEvent& e) {
    window_->desktop().mouseUp(logical(e), mods(e.mods));
    flush();
}

void StymEditor::mouseDoubleClick(const juce::MouseEvent& e) {
    window_->desktop().mouseDoubleClick(logical(e), mods(e.mods));
    flush();
}

void StymEditor::mouseExit(const juce::MouseEvent&) {
    window_->desktop().mouseExit();
    flush();
}

void StymEditor::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    // Scroll step of juce::Viewport (14 × 16 px per wheel unit); deltaY > 0: wheel up.
    constexpr double kPixelsPerUnit = 14.0 * 16.0;
    window_->desktop().mouseWheel(logical(e), -wheel.deltaX * kPixelsPerUnit, -wheel.deltaY * kPixelsPerUnit,
                                  mods(e.mods));
    flush();
}

bool StymEditor::keyPressed(const juce::KeyPress& key) {
    gemui::KeyEvent ev;
    ev.mods = mods(key.getModifiers());
    const int kc = key.getKeyCode();
    if (kc == juce::KeyPress::returnKey) ev.key = gemui::Key::Enter;
    else if (kc == juce::KeyPress::escapeKey) ev.key = gemui::Key::Escape;
    else if (kc == juce::KeyPress::backspaceKey) ev.key = gemui::Key::Backspace;
    else if (kc == juce::KeyPress::deleteKey) ev.key = gemui::Key::Delete;
    else if (kc == juce::KeyPress::tabKey) ev.key = gemui::Key::Tab;
    else if (kc == juce::KeyPress::leftKey) ev.key = gemui::Key::Left;
    else if (kc == juce::KeyPress::rightKey) ev.key = gemui::Key::Right;
    else if (kc == juce::KeyPress::upKey) ev.key = gemui::Key::Up;
    else if (kc == juce::KeyPress::downKey) ev.key = gemui::Key::Down;
    else if (kc == juce::KeyPress::homeKey) ev.key = gemui::Key::Home;
    else if (kc == juce::KeyPress::endKey) ev.key = gemui::Key::End;
    else if (kc == juce::KeyPress::pageUpKey) ev.key = gemui::Key::PageUp;
    else if (kc == juce::KeyPress::pageDownKey) ev.key = gemui::Key::PageDown;
    else if (kc == juce::KeyPress::insertKey) ev.key = gemui::Key::Insert;
    else {
        ev.key = gemui::Key::Character;
        ev.ch = char32_t(key.getTextCharacter());
        // With Ctrl, some systems give no character: the key replaces it.
        if (ev.ch < 0x20 && kc > 0x20 && kc < 0x7F) ev.ch = char32_t(kc);
        // Key: numeric keypad digit, otherwise the character without Shift (letters in upper case, Latin-1 included).
        if (kc >= juce::KeyPress::numberPad0 && kc <= juce::KeyPress::numberPad9)
            ev.code = gemui::KeyEvent::kNumpad0 + (kc - juce::KeyPress::numberPad0);
        else if (kc >= 'a' && kc <= 'z') ev.code = kc - 'a' + 'A';
        else if (kc >= 0xE0 && kc <= 0xFE && kc != 0xF7) ev.code = kc - 0x20;  // à-þ → À-Þ
        else if (kc > 0x20 && kc != 0x7F && kc < 0x100) ev.code = kc;
    }
    // Auto-repeat: the key is still held since it was pressed.
    ev.repeat = !keysDown_.try_emplace(kc, ev).second;
    const bool used = window_->desktop().keyDown(ev);
    flush();
    return used;
}

bool StymEditor::keyStateChanged(bool isKeyDown) {
    if (!isKeyDown) releaseKeys(false);
    return false;
}

void StymEditor::releaseKeys(bool all) {
    if (!window_) return;
    for (auto it = keysDown_.begin(); it != keysDown_.end();) {
        if (!all && juce::KeyPress::isKeyCurrentlyDown(it->first)) {
            ++it;
            continue;
        }
        const gemui::KeyEvent ev = it->second;
        it = keysDown_.erase(it);
        window_->desktop().keyUp(ev);
    }
    flush();
}

void StymEditor::modifierKeysChanged(const juce::ModifierKeys& m) {
    if (!window_) return;
    window_->setModifiers(mods(m));  // Alt held: scissors on the grid's notes
    flush();
}

void StymEditor::focusLost(FocusChangeType) {
    // No more releases to wait for: nothing must stay held (keyup missed outside the window).
    if (!window_) return;
    releaseKeys(true);
    window_->setModifiers({});
}

// ------------------------------------------------------------------------------------------------- Host

float StymEditor::param(int index) const {
    const auto& p = processor_.parameter(index);
    return clampToSpec(paramSpec(index), p.convertFrom0to1(p.getValue()));
}

void StymEditor::beginGesture(int index) { processor_.parameter(index).beginChangeGesture(); }

void StymEditor::setParam(int index, float plain) {
    auto& p = processor_.parameter(index);
    p.setValueNotifyingHost(p.convertTo0to1(clampToSpec(paramSpec(index), plain)));
}

void StymEditor::endGesture(int index) { processor_.parameter(index).endChangeGesture(); }

uint64_t StymEditor::extrasVersion() const { return processor_.extrasVersion(); }

NonParamState StymEditor::extras() const { return processor_.extras(); }

void StymEditor::editExtras(const std::function<void(NonParamState&)>& edit) { processor_.editExtras(edit); }

void StymEditor::screenMidi(uint8_t status, uint8_t data1, uint8_t data2) {
    processor_.pushScreenMidi(status, data1, data2);
}

bool StymEditor::hostNote(std::array<uint8_t, 3>& message) { return processor_.popHostNote(message); }

bool StymEditor::monitorMidi(plugin::MonitorMessage& message) { return processor_.popMonitorMidi(message); }

void StymEditor::setMidiCapture(const MidiKeys& keys, bool armed) { processor_.setMidiCapture(keys, armed); }

bool StymEditor::learnMidi(std::array<uint8_t, 3>& message) { return processor_.popLearnMidi(message); }

// Interface command → UiCommand on the on-screen playing queue, its arguments as bytes (0-255).
void StymEditor::seqCommand(SeqCommand command, int a, int b, int c) {
    // Indexed in the order of Host::SeqCommand.
    static constexpr std::array<UiCommand, 8> kCommands = {UiCommand::SeqPlay,     UiCommand::SeqStop,
                                                           UiCommand::SeqSelect,   UiCommand::SeqPreview,
                                                           UiCommand::SeqSongJump, UiCommand::DrumPreview,
                                                           UiCommand::DmaPreview,  UiCommand::DigiPreview};
    auto byte = [](int v) { return uint8_t(std::clamp(v, 0, 255)); };
    if (command == SeqCommand::SongJump) {  // entry on two bytes
        const int entry = std::clamp(a, 0, 65535);
        processor_.pushSeqCommand(UiCommand::SeqSongJump, uint8_t(entry & 0xff), uint8_t(entry >> 8), 0);
        return;
    }
    processor_.pushSeqCommand(kCommands[size_t(command)], byte(a), byte(b), byte(c));
}

// The latest engine status published by the audio thread, or nullptr when nothing new arrived since the last call.
const EngineStatus* StymEditor::engineStatus() {
    return processor_.updateEngineStatus() ? &processor_.engineStatus() : nullptr;
}

pluginui::EditorSession& StymEditor::session() { return processor_.editorSession(); }

bool StymEditor::standalone() const {
    return processor_.wrapperType == juce::AudioProcessor::wrapperType_Standalone;
}

void StymEditor::showAlert(const std::vector<std::string>& lines) {
    gemui::Alert::Spec spec;
    for (const std::string& line : lines) spec.lines.push_back({line});
    gemui::Alert::open(window_->desktop(), std::move(spec));
    flush();
}

int StymEditor::copyNumber() const { return standalone() ? standaloneCopyNumber() : 1; }

std::string StymEditor::midiStatus() const {
#if JucePlugin_Build_Standalone
    if (auto* holder = standalone() ? standaloneHolder() : nullptr) {
        // The prototype's hookMidiInputs: no input, the name of the only input, or their number.
        int n = 0;
        juce::String name;
        for (const auto& d : juce::MidiInput::getAvailableDevices())
            if (holder->deviceManager.isMidiInputDeviceEnabled(d.identifier)) {
                ++n;
                name = d.name;
            }
        if (n == 0) return "no inputs";
        if (n == 1) return name.toStdString();
        return std::to_string(n) + " input(s)";
    }
#endif
    return "host";  // in a host, MIDI comes through the track
}

void StymEditor::scanMidiInputs(juce::StringArray& available, juce::StringArray& enabled) const {
    available.clear();
    enabled.clear();
#if JucePlugin_Build_Standalone
    if (auto* holder = standalone() ? standaloneHolder() : nullptr)
        for (const auto& d : juce::MidiInput::getAvailableDevices()) {
            available.add(d.identifier);
            if (holder->deviceManager.isMidiInputDeviceEnabled(d.identifier)) enabled.add(d.identifier);
        }
#endif
}

void StymEditor::changeListenerCallback(juce::ChangeBroadcaster*) {
#if JucePlugin_Build_Standalone
    auto* holder = standalone() ? standaloneHolder() : nullptr;
    if (!holder) return;
    juce::StringArray available, enabled;
    scanMidiInputs(available, enabled);
    // Closed, then opened on the device that came back; each change calls here again, and finds nothing more to do.
    for (const juce::String& id : midiInputsSeen(available, enabled)) {
        holder->deviceManager.setMidiInputDeviceEnabled(id, false);
        holder->deviceManager.setMidiInputDeviceEnabled(id, true);
    }
#endif
}

juce::StringArray StymEditor::midiInputsSeen(const juce::StringArray& available, const juce::StringArray& enabled) {
    juce::StringArray present, back;
    for (const juce::String& id : available)
        if (enabled.contains(id)) present.add(id);
    // Inputs are merged: which keys came from the one gone (unplugged or switched off) is not known, so every key held
    // is released.
    for (const juce::String& id : midiInputs_)
        if (!present.contains(id)) {
            processor_.releaseMidiKeys();
            break;
        }
    // Unplugged: JUCE keeps the input it had opened, closed by the system, and counts it as enabled; when the device
    // comes back under the same identifier, it does not open it again.
    for (const juce::String& id : midiInputs_)
        if (!available.contains(id)) midiGone_.addIfNotAlreadyThere(id);
    for (int i = midiGone_.size(); --i >= 0;)
        if (available.contains(midiGone_[i])) {
            if (enabled.contains(midiGone_[i])) back.add(midiGone_[i]);  // switched off meanwhile: stays off
            midiGone_.remove(i);
        }
    midiInputs_ = present;
    return back;
}

// Version and build: "0.9.0 build 412 (fe71c0d)" from the CI, "0.9.0 build dev" from a local build.
std::string StymEditor::version() const {
    std::string v = std::string(JucePlugin_VersionString) + " build " + kBuildNumber;
    if (*kBuildCommit) v += std::string(" (") + kBuildCommit + ")";
    return v;
}

void StymEditor::quit() {
    if (!standalone()) return;
    // The state is no longer kept at Quit, unlike JUCE's window and the prototype: the work not saved is lost,
    // as the Quit box says, and the next start reopens the configuration last saved or loaded (Indus, 01/11/2026
    // 18:25).
    juce::JUCEApplicationBase::quit();
}

bool StymEditor::askQuit() {
    if (!standalone()) return false;
    window_->requestQuit();
    return true;
}

bool StymEditor::hasAudioSettings() const {
#if JucePlugin_Build_Standalone
    return standalone() && standaloneHolder() != nullptr;
#else
    return false;
#endif
}

void StymEditor::openAudioSettings() {
#if JucePlugin_Build_Standalone
    if (auto* holder = standalone() ? standaloneHolder() : nullptr) holder->showAudioSettingsDialog();
#endif
}

std::string StymEditor::audioFilePatterns() const {
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    return formats.getWildcardForAllFormats().toStdString();
}

void StymEditor::loadDigidrum(int slot, const std::string& path) {
    // The prototype's loading at the slot's rate (7800 Hz by default); 8-bit source kept for the Digidrums editor,
    // name taken from the file, the whole sample played. The whole file (once 3 s at most), after an
    // alert when the samples would then go past the memory of the Atari. The file chosen by Load or dropped on the
    // drawing, after the alert that asks whether to replace the slot's sample.
    if (slot < 0 || slot >= ymengine::kDigidrumSlotsMax || !juce::File::isAbsolutePath(str(path))) return;
    const juce::File file(str(path));
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->sampleRate <= 0 || reader->lengthInSamples <= 0) {
        window_->showMessage("Could not decode this audio file.");
        return;
    }
    const DigidrumSlotState settings = processor_.extras().digiSlots[size_t(slot)];
    const double rate = settings.play.rateHz;
    // Too long: more than kSampleLoadMax read or once converted.
    if (reader->lengthInSamples > juce::int64(kSampleLoadMax) ||
        double(reader->lengthInSamples) * rate / reader->sampleRate > double(kSampleLoadMax)) {
        window_->showMessage("This file is too long for a sample.");
        return;
    }
    const int len = int(reader->lengthInSamples);
    // A stereo file: the channel that the Stereo menu of the Digidrums window chooses, or both mixed.
    const int ch = reader->numChannels > 1 ? 2 : 1;
    juce::AudioBuffer<float> buf(ch, len);
    reader->read(&buf, 0, len, 0, true, ch > 1);
    const DigiStereo stereo = digiStereoOf(preference(kDigiStereoPref, "mix"));
    const std::vector<float> mono = digidrumChannel(
        {buf.getReadPointer(0), size_t(len)},
        ch > 1 ? std::span<const float>{buf.getReadPointer(1), size_t(len)} : std::span<const float>{}, stereo);
    const std::span<const float> channel{mono};
    std::vector<int8_t> source = toDigidrumSource(channel, reader->sampleRate, rate);
    std::vector<uint8_t> data = settings.convert == DigiConvert::Linear ? toDigidrum(channel, reader->sampleRate, rate)
                                                                        : digidrumNibbles(source, settings.convert);
    const std::string name = cleanDigidrumName(file.getFileNameWithoutExtension().toStdString());
    // Shared bytes: the alert and the edit copy the change without copying the sample.
    const SharedSamples<int8_t> src = std::move(source);
    const SharedSamples<uint8_t> values = std::move(data);
    window_->applySampleLoad([slot, name, src, values](NonParamState& s) {
        DigidrumSlotState& d = s.digiSlots[size_t(slot)];
        d.name = name;
        d.play.start = 0;
        d.play.end = uint32_t(values.size());
        d.source = src;
        s.digidrums[size_t(slot)] = values;
    });
}

pluginui::Host::Recorder StymEditor::recorder() const {
    Recorder r;
    r.state = Recorder::State(uint8_t(processor_.driver().recorderState()));
    r.chip = processor_.driver().recorderChip();
    r.frames = processor_.driver().recordedFrames();
    const StymProcessor::TakeInfo t = processor_.takeInfo();
    r.takeFrames = t.frames;
    r.takeHz = t.frameHz;
    r.takeChip = t.chip;
    return r;
}

std::string StymEditor::loadYm(std::string_view bytes, const std::string& fileName) {
    return processor_.loadYm({reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()}, fileName);
}

std::string StymEditor::playProject(const plugin::ParamValues& values, const plugin::NonParamState& extras, bool song,
                                    const std::string& name) {
    processor_.playProject(values, extras, song, name);
    return {};
}

pluginui::Host::YmPlayer StymEditor::ymPlayer() const {
    YmPlayer y;
    const Driver& d = processor_.driver();
    if (const std::shared_ptr<const Driver::PlayerProject> p = d.ymProject()) {  // a project played
        const Driver::YmStatus st = d.ymStatus();
        y.loaded = true;
        y.playing = d.ymPlaying();
        y.name = p->name;
        y.position = st.position;
        y.regs = st.regs;
        return y;
    }
    const std::shared_ptr<const YmTune> tune = d.ymTune();
    if (!tune) return y;
    const Driver::YmStatus st = d.ymStatus();
    y.loaded = true;
    y.playing = d.ymPlaying();
    y.sndh = tune->sndh != nullptr;
    y.name = tune->displayName();
    y.subsong = d.ymSubsong();
    y.subsongs = tune->subsongs();
    y.position = st.position;
    y.frames = tune->frames(y.subsong);
    y.rate = tune->frameRate();
    y.regs = st.regs;
    return y;
}

void StymEditor::ymPlay(bool on) { processor_.ymPlay(on); }
void StymEditor::ymRewind() { processor_.ymRewind(); }
void StymEditor::ymSubsong(int subsong) { processor_.ymSubsong(subsong); }
void StymEditor::ymClose() { processor_.ymClose(); }

void StymEditor::armRecorder(int chip) { processor_.armRecorder(chip); }
void StymEditor::disarmRecorder() { processor_.disarmRecorder(); }
void StymEditor::clearRecording() { processor_.clearTake(); }
RegisterTake StymEditor::recordedTake() const { return processor_.recordedTake(); }

void StymEditor::loadDmaSample(int slot, const std::string& path) {
    // DMA track: audio file resampled at the slot's rate, mono or stereo depending on its format; .raw, .spl
    // or .sam file: signed bytes taken as they are. The whole file (once 32 KB at most), after an alert when
    // the samples would then go past the memory of the Atari; chosen by Load or dropped on the drawing.
    if (slot < 0 || slot >= ymengine::kDmaSlotsMax || !juce::File::isAbsolutePath(str(path))) return;
    const juce::File file(str(path));
    const ymengine::DmaSlot settings = processor_.extras().dma.slots[size_t(slot)];
    std::vector<int8_t> data;
    if (file.hasFileExtension("raw;spl;sam")) {
        if (uint64_t(file.getSize()) > kSampleLoadMax) {
            window_->showMessage("This file is too long for a sample.");
            return;
        }
        juce::MemoryBlock block;
        if (!file.loadFileAsData(block) || block.getSize() == 0) {
            window_->showMessage("Could not read this file.");
            return;
        }
        data = dmaRaw({static_cast<const uint8_t*>(block.getData()), block.getSize()}, settings.stereo);
    } else {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if (reader == nullptr || reader->sampleRate <= 0 || reader->lengthInSamples <= 0) {
            window_->showMessage("Could not decode this audio file.");
            return;
        }
        const double rate = ymengine::kDmaRateHz[settings.rate];
        if (reader->lengthInSamples > juce::int64(kSampleLoadMax) ||
            double(reader->lengthInSamples) * rate / reader->sampleRate * (settings.stereo ? 2 : 1) >
                double(kSampleLoadMax)) {
            window_->showMessage("This file is too long for a sample.");
            return;
        }
        const int len = int(reader->lengthInSamples);
        const int ch = settings.stereo && reader->numChannels > 1 ? 2 : 1;
        juce::AudioBuffer<float> buf(ch, len);
        reader->read(&buf, 0, len, 0, true, ch > 1);
        const std::span<const float> left{buf.getReadPointer(0), size_t(len)};
        // Stereo from a mono file: the channel copied.
        const std::span<const float> right =
            settings.stereo ? std::span<const float>{buf.getReadPointer(ch - 1), size_t(len)} : std::span<const float>{};
        data = toDmaSample(left, right, reader->sampleRate, rate);
    }
    // Name taken from the file.
    const std::string name = cleanDigidrumName(file.getFileNameWithoutExtension().toStdString());
    const SharedSamples<int8_t> bytes = std::move(data);  // shared: the alert and the edit do not copy it
    window_->applySampleLoad([slot, name, bytes](NonParamState& s) {
        s.dmaSamples[size_t(slot)] = bytes;
        s.dma.slots[size_t(slot)].start = s.dma.slots[size_t(slot)].end = 0;  // the whole sample
        s.dmaNames[size_t(slot)] = name;
    });
}

// The MIDI learn map of a copy of the standalone application is its own, in the copy's settings file (Indus,
// 31/10/2026 11:03): each copy plays its own controller. In a host, it stays one of the shared preferences.
juce::PropertySet* StymEditor::copySettings() const { return standalone() ? standaloneSettings() : nullptr; }

std::string StymEditor::preference(std::string_view key, std::string_view fallback) const {
    if (juce::PropertySet* copy = key == "midiMap" ? copySettings() : nullptr)
        return copy->getValue(str(key), str(fallback)).toStdString();
    return prefs_->getValue(str(key), str(fallback)).toStdString();
}

void StymEditor::setPreference(std::string_view key, std::string_view value) {
    if (juce::PropertySet* copy = key == "midiMap" ? copySettings() : nullptr) {
        copy->setValue(str(key), str(value));
        return;
    }
    storePreference(str(key), str(value));
    if (key == "pixels") {  // Settings > Pixels: the whole desktop drawn again
        cache_.setSharp(value == "sharp");
        image_ = {};
        repaint();
    } else if (key == "renderer") {  // Settings > Renderer (Windows)
        applyRenderer();
    }
}

std::string StymEditor::systemLanguage() const { return juce::SystemStats::getUserLanguage().toStdString(); }
std::string StymEditor::keyboardLayout() const {
    // Asked again at most once a second: the user can switch layouts while the editor is open.
    const juce::uint32 now = juce::Time::getMillisecondCounter();
    if (layout_.empty() || now - layoutAt_ >= 1000) {
        layout_ = systemKeyboardLayout();
        layoutAt_ = now;
    }
    return layout_;
}

// Folders of the tooltip languages installed with STYM2149, the first one first: on Windows,
// C:\ProgramData\STYM2149\Languages (Inno Setup installer); on macOS, /Library/Application Support/STYM2149/Languages;
// on Linux, STYM2149/Languages in the XDG data folders, ~/.local/share first (install.sh), then /usr/local/share and
// /usr/share. More exactly: $XDG_DATA_HOME first when it is an absolute path (~/.local/share otherwise), then each
// absolute folder of $XDG_DATA_DIRS (/usr/local/share:/usr/share when it is empty).
static std::vector<juce::File> installedLanguageFolders() {
#if JUCE_WINDOWS
    return {juce::File::getSpecialLocation(juce::File::commonApplicationDataDirectory).getChildFile("STYM2149")
                .getChildFile("Languages")};
#elif JUCE_MAC
    return {juce::File("/Library/Application Support/STYM2149/Languages")};
#else
    std::vector<juce::File> folders;
    const juce::String home = juce::SystemStats::getEnvironmentVariable("XDG_DATA_HOME", {});
    const juce::File data(home.startsWithChar('/') ? home : "~/.local/share");
    folders.push_back(data.getChildFile("STYM2149/Languages"));
    juce::String dirs = juce::SystemStats::getEnvironmentVariable("XDG_DATA_DIRS", {});
    if (dirs.isEmpty()) dirs = "/usr/local/share:/usr/share";
    for (const juce::String& d : juce::StringArray::fromTokens(dirs, ":", {}))
        if (d.startsWithChar('/')) folders.push_back(juce::File(d).getChildFile("STYM2149/Languages"));
    return folders;
#endif
}

// Tooltip languages: the .po files of the user's Languages folder, next to the preferences, then those of the
// installed folders (a name already read in an earlier installed folder is left out; a user's file and the installed
// file of the same name are both returned, and MainWindow::scanTipLanguages puts the user's lines first); at most 64
// files of 1 MB each, by name.
std::vector<pluginui::Host::TextFile> StymEditor::languageFiles() const {
    std::vector<TextFile> files;
    std::vector<juce::File> folders{prefs_->getFile().getSiblingFile("Languages")};
    for (juce::File& f : installedLanguageFolders()) folders.push_back(std::move(f));
    juce::StringArray installedNames;
    for (size_t k = 0; k < folders.size(); ++k) {
        if (!folders[k].isDirectory()) continue;
        juce::Array<juce::File> found = folders[k].findChildFiles(juce::File::findFiles, false, "*.po");
        found.sort();
        for (const juce::File& f : found) {
            if (files.size() >= 64) return files;
            if (k > 0 && installedNames.contains(f.getFileName())) continue;
            juce::MemoryBlock bytes;
            if (f.getSize() > (1 << 20) || !f.loadFileAsData(bytes)) continue;
            if (k > 0) installedNames.add(f.getFileName());
            files.push_back({f.getFileName().toStdString(),
                             std::string(static_cast<const char*>(bytes.getData()), bytes.getSize()), k > 0});
        }
    }
    return files;
}

std::string StymEditor::installedLanguagesFolder() const {
    return installedLanguageFolders().front().getFullPathName().toStdString();
}

// Writes the given files (read-me, template) into the user's Languages folder, then shows it in the system's file
// browser.
bool StymEditor::openLanguagesFolder(const std::vector<TextFile>& files) {
    const juce::File folder = prefs_->getFile().getSiblingFile("Languages");
    if (!folder.createDirectory()) return false;
    for (const TextFile& f : files) {
        const juce::File target = folder.getChildFile(str(f.name));
        target.getParentDirectory().createDirectory();
        target.replaceWithData(f.text.data(), f.text.size());
    }
    return folder.startAsProcess();
}

std::string StymEditor::patchLibrary() const {
    const juce::File f = prefs_->getFile().getSiblingFile("Patches.json");
    return f.existsAsFile() ? f.loadFileAsString().toStdString() : std::string();
}

void StymEditor::setPatchLibrary(const std::string& json) {
    // Shared by every instance: the last write wins, each window reads it again when it opens.
    const juce::File f = prefs_->getFile().getSiblingFile("Patches.json");
    f.getParentDirectory().createDirectory();
    f.replaceWithText(str(json));
}

std::string StymEditor::digidrumLibrary() const {
    const juce::File f = prefs_->getFile().getSiblingFile("Digidrums.json");
    return f.existsAsFile() ? f.loadFileAsString().toStdString() : std::string();
}

void StymEditor::setDigidrumLibrary(const std::string& json) {
    const juce::File f = prefs_->getFile().getSiblingFile("Digidrums.json");
    f.getParentDirectory().createDirectory();
    f.replaceWithText(str(json));
}

std::string StymEditor::dmaLibrary() const {
    const juce::File f = prefs_->getFile().getSiblingFile("DMA.json");
    return f.existsAsFile() ? f.loadFileAsString().toStdString() : std::string();
}

void StymEditor::setDmaLibrary(const std::string& json) {
    const juce::File f = prefs_->getFile().getSiblingFile("DMA.json");
    f.getParentDirectory().createDirectory();
    f.replaceWithText(str(json));
}

// ---- Files ----

void StymEditor::loadProject(const ParamValues& values, const NonParamState& extras) {
    processor_.loadProject(values, extras);
}

NonParamState StymEditor::newExtras() const {
    NonParamState s;
    s.global.seqClock = processor_.defaultSeqClock();
    return s;
}

// The configuration of the standalone application (Indus, 01/11/2026 18:25): the state a host would save, so
// that JUCE's holder reopens it at the next start as it did with the state it saved at each Quit.
std::string StymEditor::configuration() const {
    juce::MemoryBlock block;
    processor_.getStateInformation(block);
    return {static_cast<const char*>(block.getData()), block.getSize()};
}

std::string StymEditor::loadConfiguration(std::string_view bytes) {
    const DecodeResult r = decodeState({reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()});
    if (!r.ok) return r.error.empty() ? "unreadable" : r.error;
    if (r.kind != StateKind::State) return "not a configuration";
    processor_.setStateInformation(bytes.data(), int(bytes.size()));
    return {};
}

void StymEditor::keepConfiguration(const std::string& path, std::string_view bytes) {
    juce::PropertySet* copy = copySettings();
    if (copy == nullptr) return;
    copy->setValue("configFile", str(path));
    copy->setValue("filterState", juce::MemoryBlock(bytes.data(), bytes.size()).toBase64Encoding());
}

std::string StymEditor::configurationFile() const {
    const juce::PropertySet* copy = copySettings();
    return copy != nullptr ? copy->getValue("configFile").toStdString() : std::string{};
}

void StymEditor::launchChooser(const FileDialog& d, bool save, std::function<void(const std::string& path)> done) {
    // Suggested file: the last one of this kind, otherwise the suggested name in the last chosen folder (at first,
    // the user's documents).
    juce::File initial;
    if (juce::File::isAbsolutePath(str(d.path))) {
        initial = juce::File(str(d.path));
    } else {
        const juce::String last = prefs_->getValue("lastFolder");
        juce::File dir = juce::File::isAbsolutePath(last) ? juce::File(last) : juce::File();
        if (!dir.isDirectory()) dir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
        initial = d.name.empty() ? dir : dir.getChildFile(str(d.name));
    }
    // The title in the interface language, as the GEM texts.
    chooser_ = std::make_unique<juce::FileChooser>(str(gemui::uiString(d.title)), initial, str(d.patterns));
    using Browser = juce::FileBrowserComponent;
    const int chooserFlags = Browser::canSelectFiles |
                             (save ? Browser::saveMode | Browser::warnAboutOverwriting : Browser::openMode);
    const juce::Component::SafePointer<StymEditor> self(this);
    chooser_->launchAsync(chooserFlags, [self, done = std::move(done)](const juce::FileChooser& fc) {
        if (self == nullptr) return;
        const juce::File file = fc.getResult();
        if (file == juce::File()) return;
        self->storePreference("lastFolder", file.getParentDirectory().getFullPathName());
        done(file.getFullPathName().toStdString());
        self->flush();
    });
}

void StymEditor::chooseFileToOpen(const FileDialog& dialog, std::function<void(const std::string& path)> done) {
    launchChooser(dialog, false, std::move(done));
}

void StymEditor::chooseFileToSave(const FileDialog& dialog, std::function<void(const std::string& path)> done) {
    launchChooser(dialog, true, std::move(done));
}

std::optional<std::string> StymEditor::readFile(const std::string& path, size_t maxBytes) const {
    if (!juce::File::isAbsolutePath(str(path))) return std::nullopt;
    const juce::File f(str(path));
    if (!f.existsAsFile() || f.getSize() < 0 || juce::uint64(f.getSize()) > maxBytes) return std::nullopt;
    juce::MemoryBlock data;
    if (!f.loadFileAsData(data)) return std::nullopt;
    return std::string(static_cast<const char*>(data.getData()), data.getSize());
}

bool StymEditor::writeFile(const std::string& path, std::string_view bytes) {
    if (!juce::File::isAbsolutePath(str(path))) return false;
    return juce::File(str(path)).replaceWithData(bytes.data(), bytes.size());
}

namespace {

// File of a long export: written to a provisional copy next to the target file (juce::TemporaryFile), which replaces
// it only at commit; destroyed without a commit, the copy is deleted (export cancelled or failed). Created on the
// message thread (createFile), then filled by the export worker.
class TemporaryFileSink final : public ymio::ByteSink {
public:
    explicit TemporaryFileSink(const juce::File& target)
        : temp_(target, juce::TemporaryFile::useHiddenFile), stream_(temp_.getFile().createOutputStream()) {
        ok_ = stream_ != nullptr && stream_->openedOk();
    }
    bool write(std::span<const uint8_t> bytes) override {
        if (!ok_) return false;
        ok_ = stream_->write(bytes.data(), bytes.size());
        pos_ += bytes.size();
        size_ = std::max(size_, pos_);
        return ok_;
    }
    bool seek(uint64_t position) override {
        if (!ok_ || position > size_) return ok_ = false;
        ok_ = stream_->setPosition(juce::int64(position));
        pos_ = position;
        return ok_;
    }
    uint64_t position() const override { return pos_; }
    uint64_t size() const override { return size_; }
    bool ok() const override { return ok_; }
    bool commit() override {
        if (!ok_) return false;
        stream_->flush();
        ok_ = stream_->getStatus().wasOk();
        stream_.reset();  // closed before the replacement
        return ok_ && temp_.overwriteTargetFileWithTemporary();
    }

private:
    juce::TemporaryFile temp_;
    std::unique_ptr<juce::FileOutputStream> stream_;
    uint64_t pos_ = 0, size_ = 0;  // write position; end of what was written (a seek may go back, not beyond it)
    bool ok_ = false;
};

}  // namespace

std::unique_ptr<ymio::ByteSink> StymEditor::createFile(const std::string& path) {
    if (!juce::File::isAbsolutePath(str(path))) return nullptr;
    auto sink = std::make_unique<TemporaryFileSink>(juce::File(str(path)));
    if (!sink->ok()) return nullptr;
    return sink;
}

uint32_t StymEditor::hostSampleRate() const {
    const double rate = processor_.getSampleRate();
    return rate > 0 ? uint32_t(std::lround(rate)) : 0;
}

}  // namespace stym::plugin
