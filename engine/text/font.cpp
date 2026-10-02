#include "text/font.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>

#include "common/files.h"

namespace text {

namespace {

std::vector<uint8_t> read_file(const std::filesystem::path& path) {
    std::vector<uint8_t> d;
    files::read(path.string(), d);
    return d;
}

// A little-endian value, 0 past the end.
template <typename T>
T get(const std::vector<uint8_t>& d, size_t pos) {
    if (pos + sizeof(T) > d.size()) return T{};
    return sizeof(T) == 4 ? T(files::le32(&d[pos])) : T(files::le16(&d[pos]));
}

// An uncompressed 32-bit DDS (A8R8G8B8, stored B, G, R, A) -> RGBA.
bool load_dds(const std::filesystem::path& path, int& width, int& height, std::vector<uint8_t>& rgba) {
    auto d = read_file(path);
    if (d.size() < 128 || std::memcmp(d.data(), "DDS ", 4) != 0) return false;
    height = int(get<uint32_t>(d, 12));
    width = int(get<uint32_t>(d, 16));
    uint32_t bits = get<uint32_t>(d, 88);
    if (bits != 32 || d.size() < 128 + size_t(width) * size_t(height) * 4) return false;
    rgba.resize(size_t(width) * size_t(height) * 4);
    for (size_t i = 0; i < rgba.size(); i += 4) {
        rgba[i + 0] = d[128 + i + 2];
        rgba[i + 1] = d[128 + i + 1];
        rgba[i + 2] = d[128 + i + 0];
        rgba[i + 3] = d[128 + i + 3];
    }
    return true;
}

}  // namespace

bool Font::load(const std::string& fnt_path) {
    auto d = read_file(fnt_path);
    if (d.size() < 4 || std::memcmp(d.data(), "BMF\x03", 4) != 0) return false;
    std::string page_file;
    size_t pos = 4;
    while (pos + 5 <= d.size()) {
        uint8_t type = d[pos];
        uint32_t length = get<uint32_t>(d, pos + 1);
        size_t body = pos + 5;
        if (body + length > d.size()) break;
        switch (type) {
        case 1:  // info
            size = std::abs(int(get<int16_t>(d, body)));
            name = reinterpret_cast<const char*>(&d[body + 14]);
            break;
        case 2:  // common
            line_height = get<uint16_t>(d, body);
            base = get<uint16_t>(d, body + 2);
            break;
        case 3:  // pages: the first page's file name
            page_file = reinterpret_cast<const char*>(&d[body]);
            break;
        case 4:  // chars, 20 bytes each
            for (size_t c = body; c + 20 <= body + length; c += 20) {
                Glyph g;
                uint32_t id = get<uint32_t>(d, c);
                g.x = get<uint16_t>(d, c + 4);
                g.y = get<uint16_t>(d, c + 6);
                g.width = get<uint16_t>(d, c + 8);
                g.height = get<uint16_t>(d, c + 10);
                g.xoffset = get<int16_t>(d, c + 12);
                g.yoffset = get<int16_t>(d, c + 14);
                g.xadvance = get<int16_t>(d, c + 16);
                glyphs_[id] = g;
            }
            break;
        case 5:  // kerning pairs, 10 bytes each
            for (size_t k = body; k + 10 <= body + length; k += 10)
                kerning_[(uint64_t(get<uint32_t>(d, k)) << 32) | get<uint32_t>(d, k + 4)] = get<int16_t>(d, k + 8);
            break;
        }
        pos = body + length;
    }
    auto dir = std::filesystem::path(fnt_path).parent_path();
    return !page_file.empty() && load_dds(dir / page_file, page_width, page_height, rgba);
}

const Glyph* Font::glyph(uint32_t codepoint) const {
    auto it = glyphs_.find(codepoint);
    return it == glyphs_.end() ? nullptr : &it->second;
}

int Font::kerning(uint32_t first, uint32_t second) const {
    auto it = kerning_.find((uint64_t(first) << 32) | second);
    return it == kerning_.end() ? 0 : it->second;
}

std::u32string utf8_to_utf32(const std::string& s) {
    std::u32string out;
    for (size_t i = 0; i < s.size();) {
        uint8_t c = uint8_t(s[i]);
        int extra = c < 0x80 ? 0 : c < 0xE0 ? 1 : c < 0xF0 ? 2 : 3;
        uint32_t cp = extra == 0 ? c : extra == 1 ? (c & 0x1F) : extra == 2 ? (c & 0x0F) : (c & 0x07);
        for (int k = 1; k <= extra && i + size_t(k) < s.size(); k++) cp = (cp << 6) | (uint8_t(s[i + size_t(k)]) & 0x3F);
        out.push_back(cp);
        i += size_t(extra) + 1;
    }
    return out;
}

}  // namespace text
