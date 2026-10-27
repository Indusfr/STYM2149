// STYM2149 — plugin editor, with JUCE: zoom, image of the GEM desktop at the screen's pixels, on-screen playing down
// to the sound, setting of a parameter with a gesture, preferences, non-automatable state, engine status.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdlib>
#include <memory>
#include <vector>

#include "BuildId.h"
#include "KeyboardLayout.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "pluginui/IconData.h"

using namespace stym;
using namespace stym::plugin;

namespace {

std::unique_ptr<juce::PropertiesFile> preferencesIn(const juce::File& file) {
    return std::make_unique<juce::PropertiesFile>(file, juce::PropertiesFile::Options{});
}

juce::MouseEvent mouseAt(StymEditor& editor, juce::Point<float> pos) {
    const auto now = juce::Time::getCurrentTime();
    return {juce::Desktop::getInstance().getMainMouseSource(),
            pos,
            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
            juce::MouseInputSource::defaultPressure,
            juce::MouseInputSource::defaultOrientation,
            juce::MouseInputSource::defaultRotation,
            juce::MouseInputSource::defaultTiltX,
            juce::MouseInputSource::defaultTiltY,
            &editor,
            &editor,
            now,
            pos,
            now,
            1,
            false};
}

// Energy of the main left output over `blocks` blocks of 512 samples, without host MIDI.
double play(StymProcessor& p, int blocks) {
    juce::AudioBuffer<float> buffer(p.getTotalNumOutputChannels(), 512);
    juce::MidiBuffer midi;
    double e = 0;
    for (int b = 0; b < blocks; ++b) {
        p.processBlock(buffer, midi);
        for (int i = 0; i < 512; ++i) e += double(buffer.getSample(0, i)) * double(buffer.getSample(0, i));
    }
    return e;
}

// Screen of `scale` pixels per component pixel (the display scale): each logical pixel of the desktop becomes a k × k
// block of the screen (whole number of screen pixels per desktop pixel). The Atari logo of the menu bar aside
// when k > 1: its outline is filled at the screen's resolution, its box only has to hold ink.
bool sameAsDesktop(StymEditor& editor, float scale, int k) {
    const juce::Image shot = editor.createComponentSnapshot(editor.getLocalBounds(), true, scale);
    gemui::Desktop& desk = editor.window().desktop();
    desk.invalidateAll();  // the framebuffer, which the editor no longer paints
    desk.paint();
    const gemui::Surface& s = desk.surface();
    if (shot.getWidth() != s.width() * k || shot.getHeight() != s.height() * k) return false;
    const gemui::Rect title = *editor.window().rectOf("mFuji");
    const gemui::Rect logo{title.x + 10, title.y + 5, pluginui::icons::kFuji.w, pluginui::icons::kFuji.h};
    const juce::Image::BitmapData bd(shot, juce::Image::BitmapData::readOnly);
    int ink = 0;
    for (int y = 0; y < shot.getHeight(); ++y)
        for (int x = 0; x < shot.getWidth(); ++x) {
            const uint32_t v = bd.getPixelColour(x, y).getARGB() | 0xff000000u;
            if (k > 1 && logo.contains({x / k, y / k})) {
                ink += v != (s.row(logo.y)[logo.x] | 0xff000000u);
                continue;
            }
            if (v != (s.row(y / k)[x / k] | 0xff000000u)) return false;
        }
    return k == 1 || ink > 0;
}

struct GestureListener : juce::AudioProcessorListener {
    std::vector<int> changed;
    int begun = 0, ended = 0;
    void audioProcessorParameterChanged(juce::AudioProcessor*, int index, float) override { changed.push_back(index); }
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int) override { ++begun; }
    void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*, int) override { ++ended; }
};

}  // namespace

TEST_CASE("Editor: GEM desktop in blocks of screen pixels at a whole scale (zoom 80 and 160 %)",
          "[editor]") {
    juce::ScopedJuceInitialiser_GUI juce;
    juce::TemporaryFile prefs(".settings");
    StymProcessor p;
    {
        // 80 %: half the logical size; on a 200 % display, one screen pixel per desktop pixel.
        auto f = preferencesIn(prefs.getFile());
        f->setValue("zoom", 80);
        StymEditor editor(p, std::move(f));
        CHECK(editor.getWidth() == pluginui::MainWindow::kWidth / 2);
        CHECK(editor.getHeight() == pluginui::MainWindow::kHeight / 2);
        CHECK(sameAsDesktop(editor, 2.0f, 1));
        CHECK(sameAsDesktop(editor, 4.0f, 2));
    }
    {
        auto f = preferencesIn(prefs.getFile());
        f->setValue("zoom", 160);
        StymEditor editor(p, std::move(f));
        CHECK(editor.getWidth() == pluginui::MainWindow::kWidth);
        CHECK(editor.getHeight() == pluginui::MainWindow::kHeight);
        CHECK(sameAsDesktop(editor, 1.0f, 1));
        CHECK(sameAsDesktop(editor, 2.0f, 2));
        editor.window().sequencer().show();  // floating window, greyed-out controls (opacity groups)
        editor.window().mixer().show();
        editor.frame();
        CHECK(sameAsDesktop(editor, 3.0f, 3));
    }
}

TEST_CASE("Editor: a dragged desktop icon leaves no trace at any size (tenth DAW test, item 129)", "[editor]") {
    juce::ScopedJuceInitialiser_GUI juce;
    juce::TemporaryFile prefs(".settings");
    StymProcessor p;
    // Sizes below and above the desktop's logical size, on displays at 100 and 125 %: a desktop pixel of the dotted
    // outline can fall on the screen pixel after the area that erases it.
    for (const int size : {100, 137, 200})
        for (const float scale : {1.0f, 1.25f}) {
            CAPTURE(size, scale);
            auto f = preferencesIn(prefs.getFile());
            f->setValue("zoom", size);
            StymEditor editor(p, std::move(f));
            editor.createComponentSnapshot(editor.getLocalBounds(), true, scale);  // whole image at this scale
            gemui::Desktop& desk = editor.window().desktop();
            const gemui::Rect b = editor.window().deskIcons()[1].bounds();
            const gemui::Point at{b.x + gemui::DeskIcon::kW / 2, b.y + 16};
            desk.mouseDown(at, gemui::MouseButton::Left, {});
            for (int k = 1; k <= 60; ++k) {
                const int out = k < 30 ? k : 60 - k;  // away from the icon, then back
                desk.mouseDrag({at.x + 9 * out, at.y + 7 * out + 3}, {});
                editor.frame();
            }
            desk.mouseUp({at.x, at.y + 3}, {});
            editor.frame();
            const juce::Image drawn = editor.createComponentSnapshot(editor.getLocalBounds(), true, scale);
            editor.createComponentSnapshot(editor.getLocalBounds(), true, scale * 2);  // drawn again from scratch
            const juce::Image whole = editor.createComponentSnapshot(editor.getLocalBounds(), true, scale);
            REQUIRE(drawn.getBounds() == whole.getBounds());
            int diff = 0;
            for (int y = 0; y < drawn.getHeight(); ++y)
                for (int x = 0; x < drawn.getWidth(); ++x) diff += drawn.getPixelAt(x, y) != whole.getPixelAt(x, y);
            CHECK(diff == 0);
        }
}

TEST_CASE("Editor: size set by resizing the window, kept in the preferences (tenth DAW test, item 127)",
          "[editor]") {
    juce::ScopedJuceInitialiser_GUI juce;
    juce::TemporaryFile prefs(".settings");
    StymProcessor p;
    {
        auto f = preferencesIn(prefs.getFile());
        f->setValue("zoom", 100);
        StymEditor editor(p, std::move(f));
        pluginui::Host& host = editor;
        // 100 %: 5/8 of the logical size, 25 % more than half of it (second DAW test, item 15).
        CHECK(editor.getWidth() == pluginui::MainWindow::kWidth * 5 / 8);
        CHECK(editor.getHeight() == pluginui::MainWindow::kHeight * 5 / 8);
        // No Zoom entry in the Options menu.
        editor.window().openMenu(3);
        const auto* menu = dynamic_cast<const gemui::MenuList*>(editor.window().desktop().topPopup());
        REQUIRE(menu);
        CHECK(menu->items().front().label == "Audio/MIDI settings");
        for (const gemui::MenuItem& m : menu->items()) CHECK(m.label != "Zoom");
        editor.window().desktop().closePopups();
        // Resized by the host, in the desktop's proportions, from 50 to 200 %: the size is remembered.
        const auto* c = editor.getConstrainer();
        CHECK(c->getMinimumWidth() == pluginui::MainWindow::kWidth * 50 / 160);
        CHECK(c->getMaximumWidth() == pluginui::MainWindow::kWidth * 200 / 160);
        CHECK(c->getFixedAspectRatio() == double(pluginui::MainWindow::kWidth) / pluginui::MainWindow::kHeight);
        editor.setSize(pluginui::MainWindow::kWidth * 125 / 160, pluginui::MainWindow::kHeight * 125 / 160);
        CHECK(host.preference("zoom", "") == "125");
        host.setPreference("noteNames", "1");
    }
    StymEditor editor(p, preferencesIn(prefs.getFile()));
    const pluginui::Host& host = editor;
    CHECK(editor.getWidth() == pluginui::MainWindow::kWidth * 125 / 160);
    CHECK(host.preference("noteNames", "0") == "1");
    CHECK(host.preference("absent", "x") == "x");
}

TEST_CASE("Editor: Settings > Renderer offered on Windows only, the preference kept", "[editor]") {
    juce::ScopedJuceInitialiser_GUI juce;
    juce::TemporaryFile prefs(".settings");
    StymProcessor p;
    {
        StymEditor editor(p, preferencesIn(prefs.getFile()));
        pluginui::Host& host = editor;
#if JUCE_WINDOWS
        CHECK(host.rendererChoice());
#else
        CHECK_FALSE(host.rendererChoice());
#endif
        CHECK(host.preference("renderer", "software") == "software");
        host.setPreference("renderer", "direct2d");  // not on screen here: no window to change
        CHECK(host.preference("renderer", "software") == "direct2d");
    }
    StymEditor editor(p, preferencesIn(prefs.getFile()));
    CHECK(static_cast<const pluginui::Host&>(editor).preference("renderer", "software") == "direct2d");
}

TEST_CASE("Editor: Fuji > Information names the build, the CI run number and its commit", "[editor]") {
    juce::ScopedJuceInitialiser_GUI gui;
    StymProcessor p;
    juce::TemporaryFile prefs(".settings");
    StymEditor editor(p, preferencesIn(prefs.getFile()));
    const pluginui::Host& host = editor;
    const std::string build = std::string(JucePlugin_VersionString) + " build " + kBuildNumber;
    CHECK(std::string(kBuildNumber).size() > 0);
    CHECK(std::string(kBuildCommit).size() <= 7);
    if (*kBuildCommit) CHECK(host.version() == build + " (" + kBuildCommit + ")");
    else CHECK(host.version() == build);
}

TEST_CASE("Editor: Patches library in Patches.json, next to the preferences", "[editor]") {
    juce::ScopedJuceInitialiser_GUI gui;
    StymProcessor p;
    juce::TemporaryFile prefs(".settings");
    const juce::File lib = prefs.getFile().getSiblingFile("Patches.json");
    lib.deleteFile();
    {
        StymEditor editor(p, preferencesIn(prefs.getFile()));
        pluginui::Host& host = editor;
        CHECK(host.patchLibrary().empty());
        host.setPatchLibrary("{\"kind\": \"patchlib\", \"patches\": [{\"name\": \"\xC3\xA9\", \"patch\": {}}]}");
    }
    CHECK(lib.existsAsFile());
    StymEditor editor(p, preferencesIn(prefs.getFile()));
    const pluginui::Host& host = editor;
    CHECK(host.patchLibrary() == "{\"kind\": \"patchlib\", \"patches\": [{\"name\": \"\xC3\xA9\", \"patch\": {}}]}");
    lib.deleteFile();
}

TEST_CASE("Editor: Preferences.settings takes over the old STYM2149.settings once, without the JUCE keys",
          "[editor]") {
    juce::ScopedJuceInitialiser_GUI juce;
    const juce::File dir = juce::File::createTempFile("prefs");
    REQUIRE(dir.createDirectory().wasOk());
    juce::PropertiesFile::Options o;
    o.millisecondsBeforeSaving = 0;
    const juce::File oldFile = dir.getChildFile("STYM2149.settings"), file = dir.getChildFile("Preferences.settings");
    {
        // Before 0.9.0 on Windows: preferences and settings of the standalone application in the same file.
        juce::PropertiesFile old(oldFile, o);
        old.setValue("zoom", 150);
        old.setValue("tipLanguage", "fr");
        old.setValue("audioSetup", "<DEVICESETUP/>");
        old.setValue("filterState", "AAAA");
        REQUIRE(old.saveIfNeeded());
    }
    {
        auto prefs = StymEditor::openPreferences(file, o);
        CHECK(prefs->getIntValue("zoom") == 150);
        CHECK(prefs->getValue("tipLanguage") == "fr");
        CHECK_FALSE(prefs->containsKey("audioSetup"));
        CHECK_FALSE(prefs->containsKey("filterState"));
        prefs->setValue("zoom", 75);
    }
    CHECK(file.existsAsFile());
    {
        // The old file stays as it is (settings of the standalone application); the takeover is done only once.
        juce::PropertiesFile old(oldFile, o);
        CHECK(old.getIntValue("zoom") == 150);
        CHECK(old.getValue("audioSetup") == "<DEVICESETUP/>");
        old.setValue("zoom", 200);
    }
    CHECK(StymEditor::openPreferences(file, o)->getIntValue("zoom") == 75);
    CHECK(dir.deleteRecursively());
}

TEST_CASE("Editor: MIDI learn, map from the preferences given to the processor, learned CC up to the slider",
          "[editor][learn]") {
    juce::ScopedJuceInitialiser_GUI gui;
    StymProcessor p;
    p.prepareToPlay(48000, 512);
    juce::AudioBuffer<float> buffer(p.getTotalNumOutputChannels(), 512);
    juce::TemporaryFile prefs(".settings");
    {
        auto f = preferencesIn(prefs.getFile());
        f->setValue("midiMap", R"({"CC21":{"id":"np","kind":"range"}})");
        f->saveIfNeeded();
    }
    const int np = paramIndex(0, Track::NoisePeriod);
    {
        StymEditor editor(p, preferencesIn(prefs.getFile()));
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::controllerEvent(1, 21, 127), 0);
        p.processBlock(buffer, midi);
        editor.window().tick(0);
        CHECK(p.currentValues()[size_t(np)] == 31.0f);
        editor.window().setMidiLearn(true);  // ends with the window
    }
    // Window closed: the learned CC is still caught (without effect), the other CCs go through.
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 21, 0), 0);
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 22, 5), 1);
    p.processBlock(buffer, midi);
    std::array<uint8_t, 3> m{};
    CHECK(p.popLearnMidi(m));
    CHECK(m == std::array<uint8_t, 3>{0xB0, 21, 0});
    CHECK_FALSE(p.popLearnMidi(m));
    CHECK(p.currentValues()[size_t(np)] == 31.0f);
    // Reopened: stale messages emptied, learn mode ended, map read again.
    StymEditor editor(p, preferencesIn(prefs.getFile()));
    CHECK_FALSE(editor.window().midiLearn());
    CHECK(editor.window().midiMap().size() == 1);
}

TEST_CASE("Editor: click on the on-screen keyboard, note played then released by the processor", "[editor]") {
    juce::ScopedJuceInitialiser_GUI juce;
    juce::TemporaryFile prefs(".settings");
    StymProcessor p;
    p.prepareToPlay(48000, 512);
    auto f = preferencesIn(prefs.getFile());
    f->setValue("zoom", 160);
    auto editor = std::make_unique<StymEditor>(p, std::move(f));
    CHECK(play(p, 4) == 0.0);
    // White key of the keyboard (main window, box {200, 716, 944, 110}): (214, 810) in logical pixels, the same in
    // the editor at 160 %.
    const juce::Point<float> key(214.0f, 810.0f);
    editor->mouseDown(mouseAt(*editor, key));
    CHECK(play(p, 8) > 0.0);
    editor->mouseUp(mouseAt(*editor, key));
    play(p, 200);
    CHECK(play(p, 4) == 0.0);
    // Note held with the mouse when the editor closes: cut (CC 120 from the screen).
    editor->mouseDown(mouseAt(*editor, key));
    CHECK(play(p, 8) > 0.0);
    editor.reset();
    play(p, 200);
    CHECK(play(p, 4) == 0.0);
}

TEST_CASE("Editor: parameter set by the window with a host gesture", "[editor]") {
    juce::ScopedJuceInitialiser_GUI juce;
    juce::TemporaryFile prefs(".settings");
    StymProcessor p;
    GestureListener listener;
    p.addListener(&listener);
    {
        StymEditor editor(p, preferencesIn(prefs.getFile()));
        pluginui::Host& host = editor;
        const int level = findParam("t01_level");
        REQUIRE(level >= 0);
        host.beginGesture(level);
        host.setParam(level, 42.0f);
        host.setParam(level, 1000.0f);  // brought back into the range
        host.endGesture(level);
        CHECK(host.param(level) == 100.0f);
        CHECK(listener.begun == 1);
        CHECK(listener.ended == 1);
        CHECK(listener.changed == std::vector<int>{level, level});
    }
    p.removeListener(&listener);
}

TEST_CASE("Editor: YM Player, dump loaded then played in place of the engine", "[editor][ymplayer]") {
    juce::ScopedJuceInitialiser_GUI juce;
    juce::TemporaryFile prefs(".settings");
    StymProcessor p;
    p.prepareToPlay(48000, 512);
    StymEditor editor(p, preferencesIn(prefs.getFile()));
    pluginui::Host& host = editor;
    CHECK_FALSE(host.ymPlayer().loaded);
    CHECK(host.loadYm("junk", "x.bin") == "Unsupported file");
    // YM3 of 100 frames: channel A at period 256, volume 15.
    std::string ym3 = "YM3!";
    ym3.resize(4 + 14 * 100, '\0');
    for (size_t f = 0; f < 100; ++f) {
        ym3[4 + 1 * 100 + f] = 1;
        ym3[4 + 7 * 100 + f] = 0x3e;
        ym3[4 + 8 * 100 + f] = 15;
    }
    CHECK(host.loadYm(ym3, "song.ym").empty());
    pluginui::Host::YmPlayer y = host.ymPlayer();
    CHECK(y.loaded);
    CHECK_FALSE(y.playing);
    CHECK(y.name == "song.ym  -  YM3!");
    CHECK(y.frames == 100);
    CHECK(y.rate == 50);
    CHECK(play(p, 4) == 0.0);
    host.ymPlay(true);
    CHECK(play(p, 8) > 0.0);
    y = host.ymPlayer();
    CHECK(y.playing);
    CHECK(y.position > 0);
    CHECK(y.regs[8] == 15);
    host.ymClose();
    CHECK_FALSE(host.ymPlayer().loaded);
    play(p, 4);
    CHECK(play(p, 4) == 0.0);
}

TEST_CASE("Processor: non-automatable state changed by the editor, versioned and saved", "[processor][editor]") {
    juce::ScopedJuceInitialiser_GUI juce;
    StymProcessor p;
    const uint64_t v0 = p.extrasVersion();
    p.editExtras([](NonParamState& s) {
        s.trackNames[2] = "  Bass line  ";
        s.digidrums[1] = {0, 8, 15, 20};
    });
    CHECK(p.extrasVersion() > v0);
    const NonParamState e = p.extras();
    CHECK(e.trackNames[2] == "Bass lin");                       // naming rule: 8 characters
    CHECK(e.digidrums[1] == std::vector<uint8_t>{0, 8, 15, 15});  // brought back to 4 bits

    juce::MemoryBlock state;
    p.getStateInformation(state);
    StymProcessor q;
    const uint64_t w0 = q.extrasVersion();
    q.setStateInformation(state.getData(), int(state.getSize()));
    CHECK(q.extrasVersion() > w0);  // the open editor reads everything again
    CHECK(q.extras().trackNames[2] == "Bass lin");
    CHECK(q.extras().digidrums[1] == e.digidrums[1]);
}

TEST_CASE("Processor: engine status published for the editor at 30 Hz", "[processor][editor]") {
    juce::ScopedJuceInitialiser_GUI juce;
    StymProcessor p;
    p.prepareToPlay(48000, 512);
    CHECK_FALSE(p.updateEngineStatus());
    p.pushScreenMidi(0x90, 60, 100);
    play(p, 4);  // 2048 samples: one publication (48000 / 30 = 1600)
    REQUIRE(p.updateEngineStatus());
    CHECK(p.engineStatus().active[0]);
    CHECK(p.engineStatus().notePeriod > 0);
    CHECK_FALSE(p.updateEngineStatus());  // nothing new since
}

// Images, not a test: `STYM_SHOT_DIR=dir stym_plugin_tests "[shot]"` writes the editor with the demo pattern and the
TEST_CASE("Editor: layout of the system keyboard from the keys at the places of Q and Y", "[editor]") {
    CHECK(layoutOfKeys(U'q', U'y') == "qwerty");
    CHECK(layoutOfKeys(U'a', U'y') == "azerty");  // French, Belgian
    CHECK(layoutOfKeys(U'A', U'Y') == "azerty");
    CHECK(layoutOfKeys(U'q', U'z') == "qwertz");  // German, Swiss
    CHECK(layoutOfKeys(U'Q', U'Z') == "qwertz");
    CHECK(layoutOfKeys(U'\'', U'f') == "qwerty");  // Dvorak: no layout of the Computer Keyboard window
    CHECK(layoutOfKeys(0, 0) == "qwerty");
    // On this machine (no display, or an X server without a French or German layout): one of the three.
    const std::string here = systemKeyboardLayout();
    CHECK((here == "qwerty" || here == "azerty" || here == "qwertz"));
    juce::ScopedJuceInitialiser_GUI juce;
    juce::TemporaryFile prefs(".settings");
    StymProcessor p;
    StymEditor editor(p, preferencesIn(prefs.getFile()));
    CHECK(static_cast<const pluginui::Host&>(editor).keyboardLayout() == here);
}

// Step Sequencer at the zooms 100 and 200 % on displays at 100, 125 and 150 %, to look at them.
TEST_CASE("Editor: images at the display scales", "[.][shot][editor]") {
    const char* dir = std::getenv("STYM_SHOT_DIR");
    if (dir == nullptr) SKIP("STYM_SHOT_DIR not set");
    juce::ScopedJuceInitialiser_GUI juce;
    juce::TemporaryFile prefs(".settings");
    StymProcessor p;
    for (const int zoom : {100, 200}) {
        auto f = preferencesIn(prefs.getFile());
        f->setValue("zoom", zoom);
        StymEditor editor(p, std::move(f));
        editor.window().doDemo();
        editor.window().sequencer().show();
        editor.frame();
        for (const float scale : {1.0f, 1.25f, 1.5f}) {
            const juce::Image shot = editor.createComponentSnapshot(editor.getLocalBounds(), true, scale);
            const juce::File out = juce::File(dir).getChildFile("editor-zoom" + juce::String(zoom) + "-display" +
                                                                juce::String(juce::roundToInt(scale * 100)) + ".png");
            out.deleteFile();
            juce::FileOutputStream stream(out);
            REQUIRE(juce::PNGImageFormat().writeImageToStream(shot, stream));
        }
    }
}
