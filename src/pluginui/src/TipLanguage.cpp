// STYM2149 — pluginui: tooltip languages
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "pluginui/TipLanguage.h"

#include <cctype>
#include <unordered_map>
#include <vector>

namespace stym::pluginui {

namespace {

// An English line with placeholders, cut into its literal parts: lits[0] {slots[0]} lits[1] ... lits[n].
struct Pattern {
    size_t entry;
    std::vector<std::string_view> lits;
    std::vector<int> slots;
    size_t literalSize = 0;  // the most specific pattern wins when several match
};

// "{1}" to "{9}" at index i.
bool placeholderAt(std::string_view s, size_t i) noexcept {
    return i + 2 < s.size() && s[i] == '{' && s[i + 1] >= '1' && s[i + 1] <= '9' && s[i + 2] == '}';
}

// Captured parts of `text` for the placeholders of `p`, each non-empty; false when the line does not match.
// The first and last literals must be the start and the end of the text. Each placeholder but the last takes the
// shortest non-empty run up to the next occurrence of the literal that follows it; the last one takes what is left
// before the final literal.
bool match(const Pattern& p, std::string_view text, std::vector<std::string_view>& parts) {
    const std::string_view head = p.lits.front(), tail = p.lits.back();
    if (text.size() < head.size() + tail.size() + p.slots.size()) return false;
    if (text.substr(0, head.size()) != head || text.substr(text.size() - tail.size()) != tail) return false;
    parts.clear();
    size_t pos = head.size();
    const size_t end = text.size() - tail.size();
    for (size_t k = 0; k < p.slots.size(); ++k) {
        if (k + 1 == p.slots.size()) {
            if (pos >= end) return false;
            parts.push_back(text.substr(pos, end - pos));
            break;
        }
        const std::string_view next = p.lits[k + 1];
        const size_t at = text.find(next, pos + 1);
        if (at == std::string_view::npos || at + next.size() > end) return false;
        parts.push_back(text.substr(pos, at - pos));
        pos = at + next.size();
    }
    return true;
}

// The translation `target` with each {n} replaced by what placeholder n captured in the English line; an {n} the
// English line does not have is left out.
std::string fill(std::string_view target, const Pattern& p, const std::vector<std::string_view>& parts) {
    std::string out;
    out.reserve(target.size() + 16);
    for (size_t i = 0; i < target.size(); ++i) {
        if (placeholderAt(target, i)) {
            const int slot = target[i + 1] - '0';
            for (size_t k = 0; k < p.slots.size(); ++k)
                if (p.slots[k] == slot) {
                    out += parts[k];
                    break;
                }
            i += 2;
            continue;
        }
        out += target[i];
    }
    return out;
}

}  // namespace

// Lines of one file (English, translation), indexed by English line, the lines with placeholders apart. The views
// point into the catalogue or into `owned`, which is filled before and never changed after.
struct TipBook::Table {
    std::vector<std::pair<std::string, std::string>> owned;
    std::vector<std::pair<std::string_view, std::string_view>> lines;
    std::unordered_map<std::string_view, size_t> exact;
    std::vector<Pattern> patterns;

    // Lines without a placeholder go into `exact`; the others are cut into their literals and placeholders.
    void index() {
        for (size_t e = 0; e < lines.size(); ++e) {
            const std::string_view en = lines[e].first;
            Pattern p{e, {}, {}};
            size_t from = 0;
            for (size_t i = 0; i < en.size(); ++i)
                if (placeholderAt(en, i)) {
                    p.lits.push_back(en.substr(from, i - from));
                    p.slots.push_back(en[i + 1] - '0');
                    from = i + 3;
                    i += 2;
                }
            p.lits.push_back(en.substr(from));
            if (p.slots.empty()) {
                exact.emplace(en, e);  // the first entry of a line wins
                continue;
            }
            for (const std::string_view l : p.lits) p.literalSize += l.size();
            patterns.push_back(std::move(p));
        }
    }

    // Line with placeholders that matches `english` with the most fixed text, and what its placeholders captured.
    // Linear in the number of patterns; on equal fixed text, the first entry wins.
    const Pattern* bestPattern(std::string_view english, std::vector<std::string_view>& bestParts) const {
        const Pattern* best = nullptr;
        std::vector<std::string_view> parts;
        for (const Pattern& p : patterns)
            if ((!best || p.literalSize > best->literalSize) && match(p, english, parts)) {
                best = &p;
                bestParts = parts;
            }
        return best;
    }

    std::optional<std::string> line(std::string_view english) const {
        if (const auto it = exact.find(english); it != exact.end()) return std::string(lines[it->second].second);
        std::vector<std::string_view> parts;
        const Pattern* best = bestPattern(english, parts);
        if (!best) return std::nullopt;
        return fill(lines[best->entry].second, *best, parts);
    }
};

namespace {

// The catalogue, each line translated by itself (placeholders of the tests), built once.
const TipBook::Table& catalogue() {
    static const TipBook::Table table = [] {
        TipBook::Table t;
        t.lines.reserve(tipcatalog::kLineCount);
        for (size_t e = 0; e < tipcatalog::kLineCount; ++e)
            t.lines.emplace_back(tipcatalog::kLines[e], tipcatalog::kLines[e]);
        t.index();
        return t;
    }();
    return table;
}

}  // namespace

std::optional<std::vector<std::string>> tipPlaceholderParts(std::string_view english) {
    const TipBook::Table& c = catalogue();
    if (c.exact.count(english)) return std::vector<std::string>{};
    std::vector<std::string_view> parts;
    if (!c.bestPattern(english, parts)) return std::nullopt;
    return std::vector<std::string>(parts.begin(), parts.end());
}

// ------------------------------------------------------------------------------------------- books

// Each table copies its file's lines into `owned`, then indexes views of them.
TipBook::TipBook(const std::vector<const TipFile*>& files) {
    for (const TipFile* file : files) {
        auto t = std::make_shared<Table>();
        t->owned = file->lines;
        t->lines.reserve(t->owned.size());
        for (const auto& [en, tr] : t->owned) t->lines.emplace_back(en, tr);
        t->index();
        tables_.push_back(std::move(t));
    }
}

std::optional<std::string> TipBook::line(std::string_view english) const {
    for (const auto& t : tables_)
        if (std::optional<std::string> tr = t->line(english)) return tr;
    return std::nullopt;
}

std::string TipBook::translate(std::string_view english) const {
    if (tables_.empty()) return std::string(english);
    std::string out;
    out.reserve(english.size() + english.size() / 4);
    for (size_t from = 0;;) {
        const size_t nl = english.find('\n', from);
        const std::string_view one = english.substr(from, nl == std::string_view::npos ? nl : nl - from);
        const std::optional<std::string> t = line(one);
        out += t ? *t : std::string(one);
        if (nl == std::string_view::npos) break;
        out += '\n';
        from = nl + 1;
    }
    return out;
}

// ------------------------------------------------------------------------------------------- PO files

// Spaces around are dropped; letters lower-cased, '_' turned into '-'.
std::string tipLanguageCode(std::string_view id) {
    while (!id.empty() && (id.front() == ' ' || id.front() == '\t')) id.remove_prefix(1);
    while (!id.empty() && (id.back() == ' ' || id.back() == '\t' || id.back() == '\r')) id.remove_suffix(1);
    if (id.size() < 2 || id.size() > 16) return {};
    std::string out;
    for (const char ch : id) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (std::isalnum(c)) out += char(std::tolower(c));
        else if (ch == '-' || ch == '_') out += '-';
        else return {};
    }
    return out;
}

namespace {

// Value of a header field ("Language: nl\n..."), nullopt when absent.
std::optional<std::string> headerField(std::string_view header, std::string_view key) {
    for (size_t from = 0; from < header.size();) {
        size_t nl = header.find('\n', from);
        if (nl == std::string_view::npos) nl = header.size();
        const std::string_view line = header.substr(from, nl - from);
        if (line.size() > key.size() && line.substr(0, key.size()) == key && line[key.size()] == ':') {
            std::string_view v = line.substr(key.size() + 1);
            while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.remove_prefix(1);
            while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '\r')) v.remove_suffix(1);
            return std::string(v);
        }
        from = nl + 1;
    }
    return std::nullopt;
}

// Content of a quoted PO string ("..." with C escapes) at the start of `s`; false when it is not one.
bool unquote(std::string_view s, std::string& out) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    if (s.size() < 2 || s.front() != '"' || s.back() != '"') return false;
    s = s.substr(1, s.size() - 2);
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 == s.size()) {
            out += s[i];
            continue;
        }
        switch (s[++i]) {
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': break;
        default: out += s[i]; break;  // \" \\ and any other character as it is
        }
    }
    return true;
}

// The reverse of unquote, for the template: quotes, backslashes, line feeds and tabs escaped.
std::string quoted(std::string_view s) {
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        if (c == '\n') {
            out += "\\n";
            continue;
        }
        if (c == '\t') {
            out += "\\t";
            continue;
        }
        out += c;
    }
    return out + '"';
}

}  // namespace

// Line by line: a keyword starts a field, a line that is only a quoted string continues the current one. An entry is
// complete once its msgstr has started; the next comment, msgctxt or msgid closes it (flush). The msgstr of the first
// empty msgid is the header. msgctxt, msgid_plural and msgstr[1] onwards are read into `ignored`.
std::optional<TipFile> parseTipFile(std::string_view text, std::string_view fileId) {
    if (text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);  // UTF-8 byte order mark
    TipFile file;
    std::optional<std::string> header;
    // Entry being read: its msgid and msgstr (msgstr[0] for a plural entry), the field the next quoted lines continue.
    std::string id, str;
    bool hasId = false, hasStr = false, fuzzy = false;
    std::string* field = nullptr;
    auto flush = [&] {
        if (hasId && hasStr) {
            if (id.empty()) {
                if (!header) header = str;
            } else if (!str.empty() && !fuzzy) {
                file.lines.emplace_back(std::move(id), std::move(str));
            }
        }
        id.clear();
        str.clear();
        hasId = hasStr = fuzzy = false;
        field = nullptr;
    };
    std::string ignored;
    for (size_t from = 0; from <= text.size();) {
        size_t nl = text.find('\n', from);
        if (nl == std::string_view::npos) nl = text.size();
        std::string_view line = text.substr(from, nl - from);
        from = nl + 1;
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
        if (line.empty() || line == "\r") continue;
        if (line.front() == '#') {
            if (hasStr) flush();
            if (line.substr(0, 2) == "#," && line.find("fuzzy") != std::string_view::npos) fuzzy = true;
            continue;
        }
        auto keyword = [&line](std::string_view k) {
            return line.size() > k.size() && line.substr(0, k.size()) == k &&
                   (line[k.size()] == ' ' || line[k.size()] == '\t');
        };
        if (keyword("msgctxt")) {
            if (hasStr) flush();
            ignored.clear();
            field = &ignored;
            unquote(line.substr(7), ignored);
        } else if (keyword("msgid")) {
            if (hasStr) flush();
            hasId = true;
            field = &id;
            unquote(line.substr(5), id);
        } else if (keyword("msgid_plural")) {
            ignored.clear();
            field = &ignored;
        } else if (keyword("msgstr") || keyword("msgstr[0]")) {
            hasStr = true;
            field = &str;
            unquote(line.substr(line[6] == '[' ? 9 : 6), str);
        } else if (line.substr(0, 7) == "msgstr[") {
            ignored.clear();
            field = &ignored;
        } else if (line.front() == '"' && field) {
            unquote(line, *field);
        }
    }
    flush();
    // Code: the Language header, else the file name; name: X-Language-Name, else the code.
    std::optional<std::string> code = header ? headerField(*header, "Language") : std::nullopt;
    file.id = tipLanguageCode(code && !code->empty() ? *code : fileId);
    if (file.id.empty()) return std::nullopt;
    const std::optional<std::string> name = header ? headerField(*header, "X-Language-Name") : std::nullopt;
    file.name = name && !name->empty() ? *name : file.id;
    return file;
}

std::string tipTemplate(std::string_view version) {
    std::string out =
        "# STYM2149 tooltips: template of a new language.\n"
        "# Save a copy in the Languages folder as <code>.po (nl.po for Dutch), give the code and the name\n"
        "# of the language below, then write each translation in the msgstr under its English line (a text\n"
        "# editor or Poedit will do). Lines left empty stay in English. {1} to {9} stand for parts of the\n"
        "# line that vary (a number, a name): keep each one, in any order. Only the characters of the GEM\n"
        "# font (the Atari ST character set) can be drawn; the others appear as \"?\".\n";
    out += "msgid \"\"\nmsgstr \"\"\n";
    out += "\"Project-Id-Version: STYM2149 " + std::string(version) + "\\n\"\n";
    out += "\"Language: \\n\"\n\"X-Language-Name: \\n\"\n";
    out += "\"MIME-Version: 1.0\\n\"\n\"Content-Type: text/plain; charset=UTF-8\\n\"\n"
           "\"Content-Transfer-Encoding: 8bit\\n\"\n";
    for (size_t e = 0; e < tipcatalog::kLineCount; ++e)
        out += "\nmsgid " + quoted(tipcatalog::kLines[e]) + "\nmsgstr \"\"\n";
    return out;
}

std::vector<std::pair<std::string, std::string>> tipFolderFiles(std::string_view version,
                                                                std::string_view installedFolder) {
    std::string readme =
        "STYM2149 tooltip languages\n"
        "\n"
        "Each .po file of this folder adds a language to the Language menu of Options > Settings,\n"
        "or corrects one of those installed with STYM2149. English is built in. The menu reads\n"
        "the folder each time it opens.\n"
        "\n"
        "The languages installed with STYM2149 (French, German, Spanish, Portuguese, Italian) are\n"
        "PO files of the same kind, in a folder of the computer";
    readme += installedFolder.empty() ? std::string(".\n") : ":\n" + std::string(installedFolder) + "\n";
    readme +=
        "\n"
        "New language: copy template.pot here as <code>.po (nl.po for Dutch), write the code and\n"
        "the name of the language in its header (Language: nl, X-Language-Name: Nederlands), then\n"
        "each translation in the msgstr under its English line. A text editor or a translation\n"
        "tool such as Poedit will do. Lines left empty stay in English.\n"
        "\n"
        "Correction of an installed language: copy its file here (fr.po) and change its lines; it\n"
        "may keep only the lines it changes, the installed file gives the others. A regional\n"
        "variant (Language: fr_CA) takes the lines it leaves out from its language (fr).\n"
        "\n"
        "{1} to {9} stand for parts of a line that vary (a number, a name): keep each one, in any\n"
        "order. Only the characters of the GEM font (the Atari ST character set) can be drawn; the\n"
        "others appear as \"?\". Save the files in UTF-8.\n"
        "\n"
        "README.txt and template.pot are written again, for the current version, each time\n"
        "Languages folder is chosen in the menu; the other files are left as they are.\n";
    std::vector<std::pair<std::string, std::string>> files;
    files.emplace_back("README.txt", std::move(readme));
    files.emplace_back("template.pot", tipTemplate(version));
    return files;
}

}  // namespace stym::pluginui
