// counter.hpp - shared line-counting logic used by both the CLI and the web
// server. Header-only so both translation units can include it without a
// separate build step.

#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

// Windows stores paths as wide strings (UTF-16) and interprets narrow strings
// using the system code page (often GBK on Chinese systems). The web UI, on the
// other hand, works in UTF-8, so we convert at the boundary.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
inline std::string wideToUtf8(const std::wstring& w) {
    if (w.empty())
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                                      nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    if (n > 0)
        WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                            &s[0], n, nullptr, nullptr);
    return s;
}
inline std::wstring utf8ToWide(const std::string& s) {
    if (s.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                      nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    if (n > 0)
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                            &w[0], n);
    return w;
}
inline fs::path u8path(const std::string& s) { return fs::path(utf8ToWide(s)); }
inline std::string pathToUtf8(const fs::path& p) { return wideToUtf8(p.native()); }
#else
inline fs::path u8path(const std::string& s) { return fs::path(s); }
inline std::string pathToUtf8(const fs::path& p) { return p.string(); }
#endif

// Recognition rules and comment/string syntax for one language.
struct Language {
    std::string name;                                        // display name
    std::vector<std::string> extensions;                     // extensions (with dot), lower-case
    std::vector<std::string> lineComments;                   // line-comment prefixes, e.g. "//", "#"
    std::vector<std::pair<std::string, std::string>> blocks; // block comments (start, end)
    std::vector<std::string> strings;                        // string delimiters, e.g. "\"", "'"
};

// Supported languages (table order == output order). Add one row to support more.
inline const std::vector<Language>& languages() {
    static const std::vector<Language> L = {
        {"C",      {".c", ".h"},
         {"//"},
         {{"/*", "*/"}},
         {"\"", "'"}},
        {"C++",    {".cpp", ".cc", ".cxx", ".hpp", ".hh", ".hxx"},
         {"//"},
         {{"/*", "*/"}},
         {"\"", "'"}},
        {"Java",   {".java"},
         {"//"},
         {{"/*", "*/"}},
         {"\"", "'"}},
        {"Python", {".py"},
         {"#"},
         {{"\"\"\"", "\"\"\""}, {"'''", "'''"}},
         {"\"", "'"}},
    };
    return L;
}

struct Counts {
    long blank = 0;
    long comment = 0;
    long code = 0;
};

struct LangResult {
    std::string name;
    long files = 0;
    long blank = 0;
    long comment = 0;
    long code = 0;
};

struct CountResult {
    bool ok = false;
    std::string error;
    std::vector<LangResult> languages;
    long totalFiles = 0;
    long totalBlank = 0;
    long totalComment = 0;
    long totalCode = 0;
};

// Return the language index for a file extension, or -1 if unsupported.
inline int languageIndexFor(const std::string& ext) {
    std::string e = ext;
    std::transform(e.begin(), e.end(), e.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto& L = languages();
    for (std::size_t k = 0; k < L.size(); ++k)
        for (const std::string& ex : L[k].extensions)
            if (ex == e)
                return static_cast<int>(k);
    return -1;
}

// Count blank / comment / code lines in a single file.
inline Counts countFile(const fs::path& path, const Language& lang) {
    Counts c;
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return c;

    std::string line;
    bool inBlock = false;
    std::string blockEnd; // the closing marker we look for while inside a block comment
    bool first = true;

    while (std::getline(in, line)) {
        // A NUL byte means the file is binary, not text: count nothing for it.
        if (line.find('\0') != std::string::npos)
            return Counts{};

        // Strip a UTF-8 BOM on the first line and a trailing '\r' (CRLF).
        if (first) {
            first = false;
            if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
                static_cast<unsigned char>(line[1]) == 0xBB &&
                static_cast<unsigned char>(line[2]) == 0xBF)
                line.erase(0, 3);
        }
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        const std::size_t n = line.size();
        std::size_t i = 0;
        bool sawCode = false;
        bool sawComment = inBlock; // line already starts inside a block comment

        while (i < n) {
            if (inBlock) {
                std::size_t p = line.find(blockEnd, i);
                if (p == std::string::npos) {
                    i = n; // rest of the line is comment
                } else {
                    i = p + blockEnd.size();
                    inBlock = false;
                    blockEnd.clear();
                }
                continue;
            }

            const char ch = line[i];
            if (ch == ' ' || ch == '\t') {
                ++i;
                continue;
            }

            bool matched = false;

            // 1) block-comment start
            for (const auto& b : lang.blocks) {
                if (line.compare(i, b.first.size(), b.first) == 0) {
                    inBlock = true;
                    blockEnd = b.second;
                    sawComment = true;
                    i += b.first.size();
                    matched = true;
                    break;
                }
            }
            if (matched)
                continue;

            // 2) line-comment start (rest of the line is comment)
            for (const std::string& lc : lang.lineComments) {
                if (line.compare(i, lc.size(), lc) == 0) {
                    sawComment = true;
                    i = n;
                    matched = true;
                    break;
                }
            }
            if (matched)
                break;

            // 3) string literal (skip it so "//" or "#" inside is not counted as a comment)
            for (const std::string& s : lang.strings) {
                if (line.compare(i, s.size(), s) == 0) {
                    const char quote = s[0];
                    i += s.size();
                    while (i < n) {
                        if (line[i] == '\\') { i += 2; continue; }
                        if (line[i] == quote) { ++i; break; }
                        ++i;
                    }
                    sawCode = true;
                    matched = true;
                    break;
                }
            }
            if (matched)
                continue;

            // 4) ordinary code character
            sawCode = true;
            ++i;
        }

        if (sawCode)
            ++c.code;
        else if (sawComment)
            ++c.comment;
        else
            ++c.blank;
    }
    return c;
}

// Recursively count all supported source files under a directory.
inline CountResult countDirectory(const fs::path& root) {
    CountResult res;
    const auto& L = languages();
    for (const auto& lang : L)
        res.languages.push_back({lang.name, 0, 0, 0, 0});

    const fs::path dir = root.empty() ? fs::path(".") : root;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        res.error = "'" + pathToUtf8(dir) + "' is not a valid directory";
        return res;
    }

    fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }

        std::error_code fec;
        if (!it->is_regular_file(fec) || fec)
            continue;

        const int idx = languageIndexFor(it->path().extension().string());
        if (idx < 0)
            continue;

        Counts c = countFile(it->path(), L[idx]);
        ++res.languages[idx].files;
        res.languages[idx].blank += c.blank;
        res.languages[idx].comment += c.comment;
        res.languages[idx].code += c.code;
    }

    for (const auto& r : res.languages) {
        res.totalFiles += r.files;
        res.totalBlank += r.blank;
        res.totalComment += r.comment;
        res.totalCode += r.code;
    }
    res.ok = true;
    return res;
}

// Count one or more directories, merging the results into a single table.
// Invalid directories are skipped and reported in `error`; the result is `ok`
// as long as at least one directory was counted.
inline CountResult countDirectories(const std::vector<fs::path>& dirs) {
    CountResult merged;
    const auto& L = languages();
    for (const auto& lang : L)
        merged.languages.push_back({lang.name, 0, 0, 0, 0});

    std::vector<std::string> errors;
    for (const fs::path& d : dirs) {
        CountResult r = countDirectory(d);
        if (!r.ok) {
            errors.push_back(r.error);
            continue;
        }
        for (std::size_t i = 0; i < merged.languages.size(); ++i) {
            merged.languages[i].files += r.languages[i].files;
            merged.languages[i].blank += r.languages[i].blank;
            merged.languages[i].comment += r.languages[i].comment;
            merged.languages[i].code += r.languages[i].code;
        }
    }

    for (const auto& r : merged.languages) {
        merged.totalFiles += r.files;
        merged.totalBlank += r.blank;
        merged.totalComment += r.comment;
        merged.totalCode += r.code;
    }
    merged.ok = merged.totalFiles > 0 || errors.empty();
    for (std::size_t i = 0; i < errors.size(); ++i) {
        if (i)
            merged.error += "; ";
        merged.error += errors[i];
    }
    return merged;
}

// Minimal JSON string escaping (backslash, quote, control chars).
inline std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                out += c;
        }
    }
    return out;
}

// Serialize a CountResult to JSON.
inline std::string toJson(const CountResult& res) {
    std::string out = "{\"ok\":" + std::string(res.ok ? "true" : "false");
    out += ",\"error\":\"" + jsonEscape(res.error) + "\"";
    out += ",\"languages\":[";
    for (std::size_t i = 0; i < res.languages.size(); ++i) {
        const auto& r = res.languages[i];
        if (i)
            out += ",";
        out += "{\"name\":\"" + r.name + "\",\"files\":" + std::to_string(r.files)
             + ",\"blank\":" + std::to_string(r.blank)
             + ",\"comment\":" + std::to_string(r.comment)
             + ",\"code\":" + std::to_string(r.code) + "}";
    }
    out += "],\"total\":{\"files\":" + std::to_string(res.totalFiles)
         + ",\"blank\":" + std::to_string(res.totalBlank)
         + ",\"comment\":" + std::to_string(res.totalComment)
         + ",\"code\":" + std::to_string(res.totalCode) + "}}";
    return out;
}
