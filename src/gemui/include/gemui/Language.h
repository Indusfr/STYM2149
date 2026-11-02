// STYM2149 — gemui: interface language
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// The interface is written in English in the code. Every text the font measures (Font::width, length, lineBox) and
// the painter draws (Painter::text, textAt) goes through uiText, which gives it in the interface language chosen in
// Settings: the layouts measure the text that is drawn, whatever the language. A text is translated line by line
// (split at '\n') by the function set with setUiLanguage, without the spaces around the line, which stay; a line it
// does not know stays in English. A text without an ASCII letter (a number, a value, a symbol) is never looked up.
//
// What must stay as it is goes inside a RawText scope: what the user types or names (text fields, file and patch
// names) and what is already translated (the tooltips, which have their own language, the lines of a wrapped text,
// translated whole before being cut).
//
// One language for the process, on the message thread: the plugin's editors and the standalone share the preference.
#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace stym::gemui {

// Translation of one line of interface text, nullopt when it has none.
using UiLine = std::function<std::optional<std::string>(std::string_view)>;

// Interface language of the process; an empty function: English (nothing translated). Forgets the texts translated so
// far.
void setUiLanguage(UiLine line);
bool uiTranslated() noexcept;  // a language other than English is set
// Counts the calls of setUiLanguage: a window laid out at another count measures its texts again.
uint64_t uiLanguageGeneration() noexcept;

// `english` in the interface language (the text itself when nothing translates it). The view stays valid until the
// next call outside a RawText scope or the next setUiLanguage.
std::string_view uiText(std::string_view english);
// Same, as a string to keep.
inline std::string uiString(std::string_view english) { return std::string(uiText(english)); }

// While one lives, texts are measured and drawn as they are; RawText(false) does nothing (a widget whose text is raw
// or not).
class RawText {
public:
    explicit RawText(bool on = true) noexcept;
    ~RawText();
    RawText(const RawText&) = delete;
    RawText& operator=(const RawText&) = delete;

private:
    bool on_;
};
bool uiRaw() noexcept;  // a RawText scope is open

// Every text that reaches uiText outside a RawText scope and has an ASCII letter is passed to `sink` (empty: none),
// translated or not: the tests collect the interface's texts this way (tools/tipcatalog/README.md). Returns the sink
// set before, to be passed the texts too or set back.
std::function<void(std::string_view)> recordUiTexts(std::function<void(std::string_view)> sink);

}  // namespace stym::gemui
