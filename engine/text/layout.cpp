#include "text/layout.h"

namespace text {

namespace {

constexpr float kGutter = 2 * 20;  // twips

enum EditTextFlags : uint16_t {
    kWordWrap = 0x0040,
    kMultiline = 0x0020,
};

struct Line {
    std::u32string chars;
    float width = 0;  // twips
};

float advance(const Font& font, char32_t c, char32_t next, float scale) {
    const Glyph* g = font.glyph(c);
    float a = g ? float(g->xadvance) : 0.0f;
    if (next) a += float(font.kerning(c, next));
    return a * scale;
}

float measure(const Font& font, const std::u32string& s, float scale) {
    float w = 0;
    for (size_t i = 0; i < s.size(); i++) w += advance(font, s[i], i + 1 < s.size() ? s[i + 1] : 0, scale);
    return w;
}

}  // namespace

std::vector<GlyphQuad> layout(const Font& font, const swf::EditTextCharacter& field, const std::string& utf8) {
    std::vector<GlyphQuad> quads;
    if (font.size == 0 || font.page_width == 0) return quads;
    // Glyph metrics are pixels at font.size; the field's height is the em
    // size in twips.
    float height = field.font_height ? float(field.font_height) : 240.0f;
    float scale = height / float(font.size);  // twips per glyph pixel
    const swf::Rect& b = field.bounds;
    float max_width = float(b.xmax - b.xmin) - 2 * kGutter;

    // Break into lines: at newlines, and at spaces when word-wrapping.
    std::vector<Line> lines(1);
    bool wrap = (field.flags & kWordWrap) != 0;
    std::u32string word;
    auto flush_word = [&] {
        Line& line = lines.back();
        std::u32string candidate = line.chars + word;
        if (wrap && !line.chars.empty() && measure(font, candidate, scale) > max_width) {
            while (!line.chars.empty() && line.chars.back() == U' ') line.chars.pop_back();
            lines.push_back({word, 0});
        } else {
            line.chars = candidate;
        }
        word.clear();
    };
    for (char32_t c : utf8_to_utf32(utf8)) {
        if (c == U'\r' || c == U'\n') {
            flush_word();
            lines.push_back({});
        } else {
            word.push_back(c);
            if (c == U' ') flush_word();
        }
    }
    flush_word();

    float y = float(b.ymin) + kGutter;
    for (Line& line : lines) {
        line.width = measure(font, line.chars, scale);
        float x = float(b.xmin) + kGutter;
        if (field.align == 1) x = float(b.xmax) - kGutter - line.width;             // right
        else if (field.align == 2) x = (float(b.xmin + b.xmax) - line.width) / 2;  // centre
        for (size_t i = 0; i < line.chars.size(); i++) {
            char32_t c = line.chars[i];
            const Glyph* g = font.glyph(c);
            if (g && g->width && g->height) {
                GlyphQuad q;
                q.x0 = x + float(g->xoffset) * scale;
                q.y0 = y + float(g->yoffset) * scale;
                q.x1 = q.x0 + float(g->width) * scale;
                q.y1 = q.y0 + float(g->height) * scale;
                q.u0 = float(g->x) / float(font.page_width);
                q.v0 = float(g->y) / float(font.page_height);
                q.u1 = float(g->x + g->width) / float(font.page_width);
                q.v1 = float(g->y + g->height) / float(font.page_height);
                quads.push_back(q);
            }
            x += advance(font, c, i + 1 < line.chars.size() ? line.chars[i + 1] : 0, scale);
        }
        y += float(font.line_height) * scale;
    }
    return quads;
}

}  // namespace text
