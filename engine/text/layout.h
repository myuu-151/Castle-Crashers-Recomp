// Lays out a text field's text in a bitmap font. castle.exe's exact rules
// (gutters, line spacing, alignment rounding, GetTextLocalizationScale) are
// still to confirm against the real game; this follows Flash's layout: a 2 px
// gutter, the font height as the em size, and per-line alignment.
#pragma once

#include <string>
#include <vector>

#include "swf/movie.h"
#include "text/font.h"

namespace text {

struct GlyphQuad {
    float x0, y0, x1, y1;  // twips, the field's space
    float u0, v0, u1, v1;
};

std::vector<GlyphQuad> layout(const Font& font, const swf::EditTextCharacter& field, const std::string& utf8);

}  // namespace text
