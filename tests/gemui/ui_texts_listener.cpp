// STYM2149 — tests: collects the interface texts for the language catalogue
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// With STYM_UI_TEXTS=<file>, every text the tests have the font measure or the painter draw in the interface language
// (gemui::recordUiTexts) is written to <file> at the end of the run, one per line, sorted, a line break written "\n":
// the texts to compare with the catalogue of the interface, tools/tipcatalog/ui.txt (tools/tipcatalog/README.md).
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>
#include <cstdlib>
#include <fstream>
#include <set>
#include <string>

#include "gemui/Language.h"

namespace {

class UiTexts : public Catch::EventListenerBase {
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testRunStarting(const Catch::TestRunInfo&) override {
        const char* path = std::getenv("STYM_UI_TEXTS");
        if (!path || !*path) return;
        path_ = path;
        stym::gemui::recordUiTexts([this](std::string_view s) {
            if (seen_.size() < 200000) seen_.emplace(s);
        });
    }

    void testRunEnded(const Catch::TestRunStats&) override {
        if (path_.empty()) return;
        stym::gemui::recordUiTexts(nullptr);
        std::ofstream out(path_);
        for (const std::string& s : seen_) {
            for (const char c : s) {
                if (c == '\n') out << "\\n";
                else if (c == '\\') out << "\\\\";
                else out << c;
            }
            out << '\n';
        }
    }

private:
    std::string path_;
    std::set<std::string, std::less<>> seen_;
};

}  // namespace

CATCH_REGISTER_LISTENER(UiTexts)
