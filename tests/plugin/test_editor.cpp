// STYM2149 — plugin editor, with JUCE: zoom, image of the GEM desktop at the screen's pixels, on-screen playing down
// to the sound, setting of a parameter with a gesture, preferences, non-automatable state, engine status.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "BuildId.h"
#include "KeyboardLayout.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "StandaloneSlot.h"
#include "plugincore/State.h"
#include "pluginui/IconData.h"
#include "pluginui/TipLanguage.h"

using namespace stym;
using namespace stym::plugin;

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();  // the plugin's factory (PluginProcessor.cpp)

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
    // In the GEM look, the logo 8 px after the title's left edge, centred in its height (Menu.cpp).
    const int fujiH = pluginui::icons::kFuji.h;
    const gemui::Rect logo{title.x + 8, title.y + (title.h - fujiH) / 2, pluginui::icons::kFuji.w, fujiH};
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
        // Resized by the host, in the desktop's proportions, from 50 % up to the size that fills the largest display,
        // 200 % at least: the size is remembered.
        const auto* c = editor.getConstrainer();
        CHECK(c->getMinimumWidth() == pluginui::MainWindow::kWidth * 50 / 160);
        int fill = 200;
        for (const auto& d : juce::Desktop::getInstance().getDisplays().displays)
            fill = std::max(fill, int(std::floor(std::min(d.userBounds.getWidth() * 160 / pluginui::MainWindow::kWidth,
                                                          d.userBounds.getHeight() * 160 /
                                                              pluginui::MainWindow::kHeight))));
        CAPTURE(fill);
        CHECK(c->getMaximumWidth() == pluginui::MainWindow::kWidth * fill / 160);
        CHECK(c->getMaximumHeight() == pluginui::MainWindow::kHeight * fill / 160);
        CHECK(c->getFixedAspectRatio() == double(pluginui::MainWindow::kWidth) / pluginui::MainWindow::kHeight);
        editor.setSize(pluginui::MainWindow::kWidth * 125 / 160, pluginui::MainWindow::kHeight * 125 / 160);
        CHECK(host.preference("zoom", "") == "125");
        host.setPreference("noteNames", "1");
    }
    {
        // A size remembered beyond the largest display (another screen, unplugged since) comes back to it.
        juce::TemporaryFile other(".settings");
        auto f = preferencesIn(other.getFile());
        f->setValue("zoom", 5000);
        StymEditor big(p, std::move(f));
        CHECK(big.getWidth() == big.getConstrainer()->getMaximumWidth());
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

TEST_CASE("Editor: two copies writing Preferences.settings keep each other's changes", "[editor]") {
    juce::ScopedJuceInitialiser_GUI juce;
    juce::TemporaryFile temp(".settings");
    juce::PropertiesFile::Options o;
    o.millisecondsBeforeSaving = -1;
    {
        juce::PropertiesFile seed(temp.getFile(), o);
        seed.setValue("zoom", 100);
        seed.setValue("tipLanguage", "en");
        REQUIRE(seed.save());
    }
    // Both copies read the file at their start; each changes one preference, then writes.
    juce::PropertiesFile one(temp.getFile(), o), two(temp.getFile(), o);
    juce::StringPairArray changedByOne(false), changedByTwo(false);
    one.setValue("tipLanguage", "fr");
    changedByOne.set("tipLanguage", "fr");
    StymEditor::writePreferences(one, changedByOne);
    two.setValue("zoom", 125);
    changedByTwo.set("zoom", "125");
    StymEditor::writePreferences(two, changedByTwo);
    const juce::PropertiesFile after(temp.getFile(), o);
    CHECK(after.getValue("tipLanguage") == "fr");  // a plain save of copy two would have put "en" back
    CHECK(after.getIntValue("zoom") == 125);
    CHECK(two.getValue("tipLanguage") == "fr");  // copy two now holds what copy one wrote
}

TEST_CASE("Editor: preferences of two editors on the same file both kept when they close", "[editor]") {
    juce::ScopedJuceInitialiser_GUI gui;
    juce::TemporaryFile prefs(".settings");
    juce::PropertiesFile::Options o;
    o.millisecondsBeforeSaving = -1;
    {
        StymProcessor p1, p2;
        auto e1 = std::make_unique<StymEditor>(p1, std::make_unique<juce::PropertiesFile>(prefs.getFile(), o));
        auto e2 = std::make_unique<StymEditor>(p2, std::make_unique<juce::PropertiesFile>(prefs.getFile(), o));
        static_cast<pluginui::Host&>(*e1).setPreference("tipLanguage", "de");
        static_cast<pluginui::Host&>(*e2).setPreference("noteNames", "1");
        e1.reset();
        e2.reset();
    }
    const juce::PropertiesFile after(prefs.getFile(), o);
    CHECK(after.getValue("tipLanguage") == "de");
    CHECK(after.getValue("noteNames") == "1");
}

TEST_CASE("Standalone copies: lowest free number, names, settings of a new copy", "[editor][standalone]") {
    std::set<int> taken{1, 2, 4};
    CHECK(lowestFreeSlot([&](int n) { return taken.insert(n).second; }) == 3);
    CHECK(lowestFreeSlot([&](int n) { return taken.insert(n).second; }) == 5);
    CHECK(lowestFreeSlot([](int) { return false; }) == 0);
    CHECK(slotName("STYM2149", 1) == "STYM2149");
    CHECK(slotName("STYM2149", 2) == "STYM2149 (2)");
    CHECK(slotName("STYM2149", 10) == "STYM2149 (10)");

    juce::PropertySet first;
    first.setValue("audioSetup", juce::String("<DEVICESETUP deviceType=\"ALSA\" audioOutputDeviceName=\"hw:1\">"
                                              "<MIDIINPUT name=\"Launchkey\" identifier=\"a\"/>"
                                              "<MIDIINPUT name=\"Exquis\" identifier=\"b\"/></DEVICESETUP>"));
    first.setValue("shouldMuteInput", true);
    first.setValue("windowX", 100);
    first.setValue("windowY", 80);
    first.setValue("filterState", "AAAA");
    first.setValue("lastStateFile", "/home/indus/song.stym");
    juce::PropertySet second;
    seedSlotSettings(first, second, 2);
    const std::unique_ptr<juce::XmlElement> setup = second.getXmlValue("audioSetup");
    REQUIRE(setup != nullptr);
    CHECK(setup->getStringAttribute("audioOutputDeviceName") == "hw:1");  // the same audio output
    CHECK(setup->getChildByName("MIDIINPUT") == nullptr);                 // each copy chooses its controller
    CHECK(second.getBoolValue("shouldMuteInput"));
    CHECK(second.getIntValue("windowX") == 132);
    CHECK(second.getIntValue("windowY") == 112);
    CHECK_FALSE(second.containsKey("filterState"));  // a new project
    CHECK_FALSE(second.containsKey("lastStateFile"));
    CHECK(slotWindowOffset(1) == 0);
    CHECK(slotWindowOffset(3) == 64);
    CHECK(slotWindowOffset(17) == 0);  // back to the first place every 16 copies

    juce::PropertySet fresh, third;  // the first copy never closed: no place, no device
    seedSlotSettings(fresh, third, 3);
    CHECK(third.getAllProperties().size() == 0);
}

TEST_CASE("Standalone copies: a number held while the copy lives", "[editor][standalone]") {
    const juce::String app = "STYM2149 slot test";  // its own lock, apart from the application's
    CHECK(standaloneCopyNumber() == 1);
    {
        const StandaloneSlot slot(app);
        CHECK(slot.number() == 1);
        CHECK(standaloneCopyNumber() == slot.number());
    }
    CHECK(standaloneCopyNumber() == 1);
}

TEST_CASE("Standalone copies: each copy's MIDI learn map, the ASIO alert of a new copy",
          "[editor][standalone]") {
    juce::PropertySet prefs, first, second;
    prefs.setValue("midiMap", R"({"CC21":{"id":"np","kind":"range"}})");
    adoptMidiMap(prefs, first, 1);
    CHECK(first.getValue("midiMap") == prefs.getValue("midiMap"));  // the first copy keeps the map learned before
    adoptMidiMap(prefs, second, 2);
    CHECK_FALSE(second.containsKey("midiMap"));  // another copy starts without a map: its controller is another
    first.setValue("midiMap", "{}");
    adoptMidiMap(prefs, first, 1);
    CHECK(first.getValue("midiMap") == "{}");  // taken once
    juce::PropertySet none, fresh;
    adoptMidiMap(none, fresh, 1);
    CHECK(fresh.getValue("midiMap") == "{}");  // a map learned later in a host stays apart

    juce::PropertySet asio, wasapi;
    asio.setValue("audioSetup", juce::String(R"(<DEVICESETUP deviceType="ASIO" audioOutputDeviceName="USB ASIO"/>)"));
    wasapi.setValue("audioSetup", juce::String(R"(<DEVICESETUP deviceType="Windows Audio"/>)"));
    CHECK(usesAsio(asio));
    CHECK_FALSE(usesAsio(wasapi));
    CHECK_FALSE(usesAsio(none));
    // Every line of the alert is a line of the interface catalogue, translated in the PO files.
    const std::set<std::string> catalogue(pluginui::tipcatalog::kUiLines,
                                          pluginui::tipcatalog::kUiLines + pluginui::tipcatalog::kUiLineCount);
    for (const bool asioOpen : {true, false}) {
        for (const bool anyOpen : {true, false}) {
            const std::vector<std::string> lines = asioNotice(asioOpen, anyOpen);
            CHECK(lines.size() == 2);
            for (const std::string& line : lines) CHECK(catalogue.count(line) == 1);
        }
    }
    CHECK(asioNotice(false, true)[1] != asioNotice(false, false)[1]);
}

TEST_CASE("Editor: in a copy of the standalone application, the MIDI learn map is the copy's own",
          "[editor][learn][standalone]") {
    juce::ScopedJuceInitialiser_GUI gui;
    juce::TemporaryFile prefs(".settings");
    const juce::String shared = R"({"CC22":{"id":"np","kind":"range"}})";
    {
        auto f = preferencesIn(prefs.getFile());
        f->setValue("midiMap", shared);
        f->saveIfNeeded();
    }
    juce::PropertySet copy;
    copy.setValue("midiMap", R"({"CC21":{"id":"np","kind":"range"}})");
    setStandaloneSettings(&copy);
    {
        // The processor of the standalone application takes the copy's map at its creation: CC21 is caught.
        const std::unique_ptr<juce::AudioProcessor> created(createPluginFilter());
        auto& p = dynamic_cast<StymProcessor&>(*created);
        p.prepareToPlay(48000, 512);
        juce::AudioBuffer<float> buffer(p.getTotalNumOutputChannels(), 512);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::controllerEvent(1, 21, 64), 0);
        p.processBlock(buffer, midi);
        std::array<uint8_t, 3> m{};
        CHECK(p.popLearnMidi(m));
        CHECK(m == std::array<uint8_t, 3>{0xB0, 21, 64});
    }
    juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Standalone);
    StymProcessor p;  // as the standalone application makes it
    juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Undefined);
    {
        StymEditor editor(p, preferencesIn(prefs.getFile()));
        REQUIRE(editor.window().midiMap().size() == 1);
        CHECK(editor.window().midiMap()[0].key == "CC21");
        editor.window().clearMidiMap();
        CHECK(copy.getValue("midiMap") == "{}");
    }
    CHECK(preferencesIn(prefs.getFile())->getValue("midiMap") == shared);  // the map of hosts left as it was
    setStandaloneSettings(nullptr);
}

TEST_CASE("Standalone: the session an earlier version saved at Quit is dropped, a configuration kept",
          "[editor][standalone]") {
    juce::PropertySet old;
    old.setValue("filterState", "AAAA");
    forgetUnsavedSession(old);
    CHECK_FALSE(old.containsKey("filterState"));  // the work not saved is lost, as the Quit box says
    juce::PropertySet kept;
    kept.setValue("filterState", "AAAA");
    kept.setValue("configFile", "/home/indus/live.stymcfg");
    forgetUnsavedSession(kept);
    CHECK(kept.getValue("filterState") == "AAAA");  // the configuration last saved or loaded reopens
}

TEST_CASE("Editor: configuration of the standalone application saved, loaded and reopened at the next start",
          "[editor][standalone]") {
    juce::ScopedJuceInitialiser_GUI gui;
    juce::TemporaryFile prefs(".settings");
    juce::PropertySet copy;
    setStandaloneSettings(&copy);
    juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Standalone);
    StymProcessor p;  // as the standalone application makes it
    juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Undefined);
    {
        StymEditor editor(p, preferencesIn(prefs.getFile()));
        pluginui::Host& host = editor;
        CHECK(host.configurationFile().empty());
        const int level = findParam("t01_level");
        REQUIRE(level >= 0);
        host.setParam(level, 42.0f);
        const std::string saved = host.configuration();
        juce::MemoryBlock state;
        p.getStateInformation(state);
        CHECK(saved == std::string(static_cast<const char*>(state.getData()), state.getSize()));  // as a host saves it

        host.setParam(level, 10.0f);
        CHECK(host.loadConfiguration(saved).empty());
        CHECK(host.param(level) == 42.0f);
        CHECK_FALSE(host.loadConfiguration("not a STYM2149 file").empty());
        PluginState pattern;
        pattern.values[size_t(level)] = 7.0f;
        const std::vector<uint8_t> bytes = encodeState(pattern, StateKind::Pattern);
        CHECK_FALSE(host.loadConfiguration({reinterpret_cast<const char*>(bytes.data()), bytes.size()}).empty());
        CHECK(host.param(level) == 42.0f);  // a pattern file is not a configuration: nothing changed

        host.keepConfiguration("/home/indus/live.stymcfg", saved);
        CHECK(host.configurationFile() == "/home/indus/live.stymcfg");
        juce::MemoryBlock reopened;
        REQUIRE(reopened.fromBase64Encoding(copy.getValue("filterState")));  // what JUCE's holder reloads
        CHECK(std::string(static_cast<const char*>(reopened.getData()), reopened.getSize()) == saved);
    }
    setStandaloneSettings(nullptr);
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

TEST_CASE("Editor: a MIDI input gone releases the keys it held, sound and keyboard", "[editor]") {
    juce::ScopedJuceInitialiser_GUI juce;
    juce::TemporaryFile prefs(".settings");
    StymProcessor p;
    p.prepareToPlay(48000, 512);
    StymEditor editor(p, preferencesIn(prefs.getFile()));
    // Every input available is enabled here.
    auto seen = [&editor](const juce::StringArray& inputs) { return editor.midiInputsSeen(inputs, inputs); };
    seen({"exquis", "keys"});
    // A key held on the controller: it sounds, lit on the on-screen keyboard.
    juce::AudioBuffer<float> buffer(p.getTotalNumOutputChannels(), 512);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(2, 60, uint8_t(100)), 0);
    p.processBlock(buffer, midi);
    CHECK(play(p, 8) > 0.0);
    editor.frame();
    CHECK(editor.window().piano().held(60));
    // Another input switched on: nothing changes.
    seen({"exquis", "keys", "pads"});
    play(p, 2);
    editor.frame();
    CHECK(editor.window().piano().held(60));
    // The controller unplugged: no note off will come; the key is released and goes dark.
    seen({"keys", "pads"});
    play(p, 200);
    CHECK(play(p, 4) == 0.0);
    editor.frame();
    CHECK_FALSE(editor.window().piano().held(60));
}

TEST_CASE("Editor: a MIDI input plugged back is opened again", "[editor]") {
    juce::ScopedJuceInitialiser_GUI juce;
    juce::TemporaryFile prefs(".settings");
    StymProcessor p;
    p.prepareToPlay(48000, 512);
    StymEditor editor(p, preferencesIn(prefs.getFile()));
    CHECK(editor.midiInputsSeen({"exquis", "keys"}, {"exquis", "keys"}).isEmpty());
    // Unplugged: JUCE still counts it as enabled, but it is no longer available.
    CHECK(editor.midiInputsSeen({"keys"}, {"keys"}).isEmpty());
    CHECK(editor.midiInputsSeen({"keys", "pads"}, {"keys"}).isEmpty());  // another device plugged in, not enabled
    // Plugged back, still enabled: to be closed and opened again, once.
    CHECK(editor.midiInputsSeen({"exquis", "keys", "pads"}, {"exquis", "keys"}) == juce::StringArray{"exquis"});
    CHECK(editor.midiInputsSeen({"exquis", "keys", "pads"}, {"exquis", "keys"}).isEmpty());
    // Unplugged, then switched off in the settings before it comes back: it stays off.
    CHECK(editor.midiInputsSeen({"exquis", "pads"}, {"exquis"}).isEmpty());
    CHECK(editor.midiInputsSeen({"exquis", "keys", "pads"}, {"exquis"}).isEmpty());
    // An input never seen enabled is not opened by STYM2149: the device manager opens those it remembers.
    CHECK(editor.midiInputsSeen({"exquis", "keys", "pads", "new"}, {"exquis", "new"}).isEmpty());
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
