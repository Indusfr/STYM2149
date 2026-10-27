// STYM2149 — gemshot: scenes of the main window, same states as the captures of the prototype
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Each scene repeats the one in tools/proto_capture/capture.mjs: same starting state (default settings, zoom
// 100 %, mouse in the bottom-left corner of the desktop), same action. The gemshot tool renders them to PNG; the
// visual regression test (tests/visual) compares them with the captures of the prototype.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "Png.h"
#include "pluginui/Host.h"

namespace stym::gemshot {

// Names of the scenes, in the order of capture.mjs.
const std::vector<std::string>& sceneNames();
// Scenes of the windows specific to STYM2149, with no capture of the prototype: compared with their reference
// rendering (tests/data/visual/stym), checked by eye when it is created or changed.
const std::vector<std::string>& stymSceneNames();

// Zone that the comparison ignores: an intended deviation that covers the whole zone.
struct Mask {
    int x = 0, y = 0, w = 0, h = 0;
    std::string decision;
    bool contains(int px, int py) const noexcept { return px >= x && py >= y && px < x + w && py < y + h; }
};

// Inserted row (intended deviation): the band [y − dy, y) is new, absent from the capture,
// and is not compared (the STYM2149 scenes check it to the pixel); in the zone [y, y + h), the rows moved down by
// `dy`: a pixel matches if it equals the capture at the same place (what covers the zone: window, menu) or `dy`
// pixels higher. A negative `dy` is a removed row (two entries of the prototype made one): the rows of the zone
// moved up, a pixel matches the capture at the same place or −dy pixels lower; no band.
struct Shift {
    int x = 0, y = 0, w = 0, h = 0, dy = 0;
    std::string decision;
    int band = -1;  // height of the ignored band above the zone; -1: dy (the zone moved down by one inserted row)
    int dx = 0;     // horizontal move of a row: the zone is compared with the capture dx px to its left
    // Compared with the capture of Shot::alt instead: the former place of a moved frame that a menu or an
    // alert covers in the scene's capture, seen in the capture of the same window without it.
    bool alt = false;
    bool contains(int px, int py) const noexcept { return px >= x && py >= y && px < x + w && py < y + h; }
    bool inBand(int px, int py) const noexcept {
        const int b = band >= 0 ? band : dy;
        return b > 0 && px >= x && px < x + w && py >= y - b && py < y;
    }
};

// Zone dimmed where the prototype left it as it is (intended deviation): a pixel matches if
// it equals the capture, or the capture under the same veil, blended at `alpha` over `under` (the CSS opacity of the
// prototype's .inert class over the window's white). Alpha 0: the zone shows `under` alone, like the place that the
// centred window buttons left. `desktopRow` >= 0: the zone shows the desktop, like the place of the
// prototype's help line under the shorter main window; a pixel matches the capture's desktop in the same
// column below that row, at the same phase of the checkerboard (4 rows), veiled as the capture is there. `dx`, `dy`:
// the zone moved with its frame, compared with the capture that far up and to the left.
struct Dim {
    int x = 0, y = 0, w = 0, h = 0;
    double alpha = 1;
    uint32_t under = 0xFFFFFFFFu;
    std::string decision;
    int desktopRow = -1;
    int dx = 0, dy = 0;
    bool contains(int px, int py) const noexcept { return px >= x && py >= y && px < x + w && py < y + h; }
};

// The tooltip languages installed with STYM2149: the PO files of languages/ in the sources, by name, as the
// plugin reads them from the folder the installers fill. Every scene has them.
std::vector<pluginui::Host::TextFile> installedLanguages();

// Rendering of a scene (1344 × 984 desktop, empty image if the scene is unknown), its ignored, shifted and dimmed
// zones.
struct Shot {
    tools::Image image;
    std::vector<Mask> masks;
    std::vector<Shift> shifts;
    std::vector<Dim> dims;
    // Allowed deviation, in % of the original desktop: 1, or 2 for a floating window (small GEM texts).
    int budget = 1;
    // Scene of the prototype whose capture shows the same window without the menu or the alert of this one
    // (Shift::alt), and the opacity of the black veil the alert lays over it (25 %).
    std::string alt;
    double altVeil = 0;
};
Shot renderScene(const std::string& name);

// Capture of Shot::alt as the scene shows that window: under the veil of its alert.
tools::Image altCapture(const Shot& shot, const tools::Image& capture);

// Comparison with a capture, outside the ignored zones: pixels with a channel that deviates by more than
// `tolerance`, over the number of pixels compared. `alt`: the capture of Shot::alt, for the shifts that use it.
struct Diff {
    long differing = 0;
    long total = 0;
    double percent() const noexcept { return total ? 100.0 * double(differing) / double(total) : 100.0; }
};
Diff compare(const tools::Image& a, const tools::Image& b, int tolerance, const std::vector<Mask>& masks = {},
             const std::vector<Shift>& shifts = {}, const std::vector<Dim>& dims = {},
             const tools::Image* alt = nullptr);
// Image of the deviations: identical pixels paled, different pixels in red, ignored zones in pale blue.
tools::Image diffImage(const tools::Image& a, const tools::Image& b, int tolerance, const std::vector<Mask>& masks = {},
                       const std::vector<Shift>& shifts = {}, const std::vector<Dim>& dims = {},
                       const tools::Image* alt = nullptr);

}  // namespace stym::gemshot
