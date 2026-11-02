// STYM2149 — pluginui: languages of the tooltips and of the interface
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Tooltips are written in English in the code, the only language built in. The desktop translates each one when it
// shows it (Desktop::translateTip), line by line, through the book of the chosen language: an entry gives one English
// line and its translation. An English line may hold placeholders {1} to {9}, each matching a non-empty part of the
// line (a number, a name); the translation puts each captured part where its placeholder stands. A line missing from
// the book stays in English.
//
// The texts of the interface (menus, buttons, labels, window titles, alerts) are translated the same way, by a book of
// their own (interfaceBook), in the interface language chosen in Settings apart from the tooltip language (Indus,
// 28/10/2026 03:06; menus and labels were English only): gemui's uiText (gemui/Language.h) asks it for
// each line it measures or draws. In that book, the parts a line with placeholders captured are translated too, each by
// an entry of its own ("{1} [{2}]" puts "Bass [Chip Voice]" together from "Bass" and "Chip Voice").
//
// Every other language is a file (seventh DAW test, item 82): a gettext PO file (the format of translation
// tools such as Poedit) per language, read when the Options menu opens. French, German, Spanish, Portuguese and
// Italian are the files of languages/ in the sources, installed with STYM2149 into a folder of the computer; the
// user's own files are in the Languages folder next to the preferences. The header names the language ("Language:
// nl", "X-Language-Name: Nederlands"); each entry gives an English line (msgid) and its translation (msgstr). The
// entries of the interface have the context "ui" (msgctxt "ui"), the others are the tooltips'.
#pragma once
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace stym::pluginui {

// What the placeholders of the catalogue line chosen for `english` captured, in placeholder order (empty for a
// line without placeholders); nullopt when no line matches. Used by the tests to catch a generic line such as
// "Track {1}" swallowing a whole tooltip that has no entry of its own.
std::optional<std::vector<std::string>> tipPlaceholderParts(std::string_view english);
// Same for the interface lines of the catalogue, without the names between quotes or brackets, which are not
// looked up.
std::optional<std::vector<std::string>> uiPlaceholderParts(std::string_view english);

// Language read from a file.
struct TipFile {
    std::string id;    // code of the "Language" header, otherwise of the file name: lower case, '-' ("nl", "pt-pt")
    std::string name;  // "X-Language-Name" header (the language's name in that language), otherwise the code
    std::vector<std::pair<std::string, std::string>> lines;  // English line, translation (translated entries only)
    std::vector<std::pair<std::string, std::string>> uiLines;  // the same for the interface (context "ui")
};
// Reads the text of a PO file; `fileId` (the file name without .po) gives the code when the header has none. nullopt
// when the code is not one (letters, digits and '-', 2 to 16 characters). Entries marked fuzzy are left out, as
// gettext does, and so are those of a context other than "ui".
std::optional<TipFile> parseTipFile(std::string_view text, std::string_view fileId);
// Template of a new language: every English line of the catalogue with an empty translation, empty code and name, the
// tooltips first, then the interface lines (context "ui"). `version` goes in the header.
std::string tipTemplate(std::string_view version);
// Files written into the user's Languages folder when the menu opens it: README.txt and template.pot (name, text).
// `installedFolder`, the folder of the languages installed with STYM2149, is named in the read-me when known.
std::vector<std::pair<std::string, std::string>> tipFolderFiles(std::string_view version,
                                                                std::string_view installedFolder);
// Code of a language as kept ("fr_CA" gives "fr-ca"); empty when it is not one (letters, digits, '-' or '_', 2 to
// 16 characters).
std::string tipLanguageCode(std::string_view id);

// Translations of one language: the lines of one or more files, the first file first (a user's corrections over
// the installed file, a regional variant over its language). The default one is English, which translates nothing.
// MainWindow builds one when the language changes and the desktop calls translate for each tooltip, on the message
// thread. Copies are cheap: they share the tables.
class TipBook {
public:
    TipBook() = default;
    explicit TipBook(const TipFile& file) : TipBook(std::vector<const TipFile*>{&file}) {}
    explicit TipBook(const std::vector<const TipFile*>& files);
    // The book of the interface texts: the "ui" lines of the files, a line taken by a pattern getting its
    // captured parts translated, each by an exact entry or a pattern (one level).
    static TipBook interfaceBook(const std::vector<const TipFile*>& files);
    static TipBook interfaceBook(const TipFile& file) { return interfaceBook(std::vector<const TipFile*>{&file}); }

    // One line translated (nullopt: no entry for it); the whole tooltip, line by line, the missing lines in English.
    std::optional<std::string> line(std::string_view english) const;
    std::string translate(std::string_view english) const;

    struct Table;  // lines of one file, by English line and by pattern

private:
    TipBook(const std::vector<const TipFile*>& files, bool interface);
    // A part captured by a pattern of the interface book, translated by itself, or as it is.
    std::string part(std::string_view english) const;

    // One per file, first file first. A table never moves once built: its views point into its own strings.
    std::vector<std::shared_ptr<const Table>> tables_;
    bool nested_ = false;  // the interface book: the captured parts translated too
};

namespace tipcatalog {
// Every English line of the tooltips, then of the interface, each sorted (TipCatalog.cpp, generated by
// tools/tipcatalog/tipcatalog.py).
extern const char* const kLines[];
extern const size_t kLineCount;
extern const char* const kUiLines[];
extern const size_t kUiLineCount;
}  // namespace tipcatalog

}  // namespace stym::pluginui
