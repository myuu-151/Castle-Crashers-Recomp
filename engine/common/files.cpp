#include "common/files.h"

#include <filesystem>
#include <fstream>
#include <iterator>

namespace files {

bool read(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

bool exists(const std::string& path) { return std::filesystem::exists(path); }

// The PC has the memory to read a file whole: no holes of its own (swf::Movie
// finds some itself under CASTLE_HOLES, to try the path the console takes).
std::vector<Hole> holes(const std::string&) { return {}; }

bool read_holed(const std::string& path, const std::vector<Hole>& holes, std::vector<uint8_t>& out, TakeHole take,
                void* context) {
    std::vector<uint8_t> whole;
    if (!read(path, whole)) return false;
    out.clear();
    size_t from = 0;
    for (size_t i = 0; i < holes.size(); i++) {
        const Hole& h = holes[i];
        if (h.offset < from || size_t(h.offset) + h.size > whole.size()) return false;
        out.insert(out.end(), whole.begin() + ptrdiff_t(from), whole.begin() + ptrdiff_t(h.offset));
        if (!take(context, i, out.size(), whole.data() + h.offset, nullptr)) return false;
        from = size_t(h.offset) + h.size;
    }
    out.insert(out.end(), whole.begin() + ptrdiff_t(from), whole.end());
    return true;
}

}  // namespace files
