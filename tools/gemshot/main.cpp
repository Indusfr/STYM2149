// STYM2149 — gemshot: renders the scenes of the main window to PNG and compares them with the prototype's captures
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Usage: gemshot output_folder [--ref captures_folder] [scene...]
// With --ref, also writes <scene>-diff.png (deviations in red) and prints the percentage of different pixels.
// Without a named scene, renders every scene; those of the windows specific to STYM2149 are compared with
// <captures>/stym.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Png.h"
#include "Scenes.h"

int main(int argc, char** argv) {
    using namespace stym;
    std::string out, ref;
    std::vector<std::string> only;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--ref") && i + 1 < argc) ref = argv[++i];
        else if (out.empty()) out = argv[i];
        else only.push_back(argv[i]);
    }
    if (out.empty()) {
        std::fprintf(stderr, "usage: gemshot output_folder [--ref captures_folder] [scene...]\n");
        return 2;
    }
    std::vector<std::string> names = only;
    if (names.empty()) {
        names = gemshot::sceneNames();
        names.insert(names.end(), gemshot::stymSceneNames().begin(), gemshot::stymSceneNames().end());
    }
    const auto& stym = gemshot::stymSceneNames();
    int failed = 0;
    for (const std::string& name : names) {
        const gemshot::Shot shot = gemshot::renderScene(name);
        const tools::Image& img = shot.image;
        if (!img.width) {
            std::fprintf(stderr, "unknown scene: %s\n", name.c_str());
            ++failed;
            continue;
        }
        const std::string path = out + "/" + name + ".png";
        if (!tools::writePng(path, img)) {
            std::fprintf(stderr, "cannot write: %s\n", path.c_str());
            ++failed;
            continue;
        }
        if (ref.empty()) {
            std::printf("%s\n", path.c_str());
            continue;
        }
        tools::Image proto;
        std::string error;
        const bool own = std::find(stym.begin(), stym.end(), name) != stym.end();
        if (!tools::readPng(ref + (own ? "/stym/" : "/") + name + ".png", proto, &error)) {
            std::fprintf(stderr, "%s: %s\n", name.c_str(), error.c_str());
            ++failed;
            continue;
        }
        tools::Image alt;
        if (!shot.alt.empty()) {
            if (!tools::readPng(ref + "/" + shot.alt + ".png", alt, &error)) {
                std::fprintf(stderr, "%s: %s\n", shot.alt.c_str(), error.c_str());
                ++failed;
                continue;
            }
            alt = gemshot::altCapture(shot, alt);
        }
        const tools::Image* seen = shot.alt.empty() ? nullptr : &alt;
        const gemshot::Diff d = gemshot::compare(img, proto, 4, shot.masks, shot.shifts, shot.dims, seen);
        tools::writePng(out + "/" + name + "-diff.png",
                        gemshot::diffImage(img, proto, 4, shot.masks, shot.shifts, shot.dims, seen));
        std::printf("%-16s %7ld px  %6.3f %%", name.c_str(), d.differing, d.percent());
        if (!shot.masks.empty()) {
            long area = 0;
            for (const gemshot::Mask& m : shot.masks) area += long(m.w) * long(m.h);
            std::printf("  (%zu zones ignored, %ld px, %s)", shot.masks.size(), area,
                        shot.masks.front().decision.c_str());
        }
        std::printf("\n");
    }
    return failed ? 1 : 0;
}
