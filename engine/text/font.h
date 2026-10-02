// Bitmap fonts. castle.exe draws text fields with AngelCode BMFont fonts
// (assets/fonts/*.fnt, binary version 3) whose glyphs are in an
// uncompressed 32-bit DDS page, not with the SWF's own font outlines.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace text {

struct Glyph {
    uint16_t x = 0, y = 0, width = 0, height = 0;  // in the page, pixels
    int16_t xoffset = 0, yoffset = 0, xadvance = 0;
};

class Font {
public:
    bool load(const std::string& fnt_path);

    const Glyph* glyph(uint32_t codepoint) const;
    int kerning(uint32_t first, uint32_t second) const;

    std::string name;
    int size = 0;         // the size the glyphs were rendered at, pixels
    int line_height = 0;
    int base = 0;         // baseline, from the top of a line
    int page_width = 0, page_height = 0;
    std::vector<uint8_t> rgba;  // the page
    uint32_t texture = 0;       // renderer handle, created on first use

private:
    std::unordered_map<uint32_t, Glyph> glyphs_;
    std::unordered_map<uint64_t, int> kerning_;
};

// Decodes UTF-8 into code points.
std::u32string utf8_to_utf32(const std::string& s);

}  // namespace text
