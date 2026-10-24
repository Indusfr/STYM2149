// STYM2149 — pluginui: Patches window (#libWin of the prototype), library of track sounds
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Save current (the sound of the focused track, under a name asked for), one button per patch (click: loaded into
// the focused track; right click: Load, Rename, Overwrite, Delete), Import and Export (files: M7, D-141). Positions
// and sizes: the prototype's CSS boxes at 100 % (window at 160, 160, 360 px wide).
//
// The library is shared by every project (Host::patchLibrary, plugincore/Patches.h): read again when the window
// opens, rewritten at each change. Loading a patch is one undo step of the track.
#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gemui/Controls.h"
#include "plugincore/Patches.h"
#include "pluginui/Widgets.h"

namespace stym::pluginui {

class MainWindow;

// Owned by MainWindow, used on the message thread. It keeps the library as last read (lib_) and writes it back whole
// after each change; Save current, Rename, Overwrite and Delete change the library, not the project (no undo step).
class PatchesWindow : public gemui::FloatWindow {
public:
    static constexpr int kX = 160, kY = 160, kWidth = 360;  // #libWin: left 160, top 160, width 360

    explicit PatchesWindow(MainWindow& main);

    // Opens the window in front of the others and reads the library again (openLib).
    void show();
    // One button per patch of lib_ (rebuilt when their number changes), then the layout; nothing while closed.
    void refresh();

    const std::vector<plugin::PatchEntry>& library() const noexcept { return lib_; }
    // For the Instruments window (D-147): library read again from the file; entry added at the end and file
    // rewritten (an unreadable file is not overwritten, an alert says so).
    const std::vector<plugin::PatchEntry>& reload();
    void append(plugin::PatchEntry e);
    // Import button (M7): patches of a library file added at the end; the prototype's alert if it is refused.
    void importText(const std::string& text);

    // Rectangle of an element by the prototype's id (captures, tests): "libWin", "libSave", "libImport",
    // "libExport", "libHint", and "libPatch0", "libPatch1"... for the patch buttons.
    std::optional<gemui::Rect> rectOf(std::string_view id) const;

private:
    void build();
    void wire();
    void layout();
    void load();   // reads the library file again
    void store();  // writes it (saveLib)
    bool stillAt(int k, const std::string& name);  // library read again: patch k still named `name`
    void loadInto(int k);                       // libApply on the focused track
    void patchMenu(int k);                      // right click on a patch
    // gemPrompt: a name prompt (Cancel, OK), `text` proposed; `ok` gets the text on OK.
    void prompt(const std::string& message, const std::string& text, std::function<void(const std::string&)> ok);

    MainWindow& m_;
    gemui::Button save_{"Save current"}, import_{"Import"}, export_{"Export"};
    Paragraph hint_{"Click = load into focused channel. Right click for actions."};
    Rule sep_;
    Paragraph empty_{"Library is empty. Save current stores the focused channel sound."};
    // One button per entry of lib_, in the same order. Each on the heap: the window holds its children by address.
    std::vector<std::unique_ptr<gemui::Button>> buttons_;
    std::vector<plugin::PatchEntry> lib_;
    bool unreadable_ = false;  // file present but unreadable: never rewritten
};

}  // namespace stym::pluginui
