// The game's localized text, shown by number (`field.ntext = 742`). The
// strings come from castle.exe (tools/extract_strings.py) and live in
// assets/text/<language>.txt as `number<TAB>text` lines.
#pragma once

#include <string>
#include <unordered_map>

namespace text {

class Strings {
public:
    // language: en, de, fr, es, it, ja, ko, zh-Hant, pt or zh-Hans.
    bool load(const std::string& dir, const std::string& language);
    // UTF-8, or empty if there is no such string.
    const std::string& get(int number) const;
    // Replaces or adds a string (a port's own wording).
    void set(int number, const std::string& utf8) { strings_[number] = utf8; }

private:
    std::unordered_map<int, std::string> strings_;
};

}  // namespace text
