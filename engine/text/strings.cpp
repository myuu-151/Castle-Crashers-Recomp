#include "text/strings.h"

#include <cstdint>
#include <vector>

#include "common/files.h"

namespace text {

bool Strings::load(const std::string& dir, const std::string& language) {
    std::vector<uint8_t> file;
    if (!files::read(dir + "/" + language + ".txt", file)) return false;
    std::string all(file.begin(), file.end()), line;
    for (size_t start = 0; start < all.size();) {
        size_t end = all.find('\n', start);
        if (end == std::string::npos) end = all.size();
        line.assign(all, start, end - start);
        start = end + 1;
        auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        std::string text;
        for (size_t i = tab + 1; i < line.size(); i++) {
            if (line[i] == '\\' && i + 1 < line.size()) {
                char e = line[++i];
                text += e == 'n' ? '\n' : e == 't' ? '\t' : e;
            } else if (line[i] != '\r') {
                text += line[i];
            }
        }
        strings_[std::stoi(line.substr(0, tab))] = text;
    }
    return true;
}

const std::string& Strings::get(int number) const {
    static const std::string none;
    auto it = strings_.find(number);
    return it == strings_.end() ? none : it->second;
}

}  // namespace text
