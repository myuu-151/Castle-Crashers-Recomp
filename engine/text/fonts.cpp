#include "text/fonts.h"

namespace text {

namespace {

Font g_font;
bool g_font_loaded = false;
Strings g_strings;

}  // namespace

bool load(const std::string& assets_dir, const std::string& language) {
    g_font_loaded = g_font.load(assets_dir + "/fonts/synjUnicode_20.fnt");
    bool strings = g_strings.load(assets_dir + "/text", language);
    return g_font_loaded && strings;
}

Font* game_font() { return g_font_loaded ? &g_font : nullptr; }

const Strings& strings() { return g_strings; }

void set_string(int number, const std::string& utf8) { g_strings.set(number, utf8); }

}  // namespace text
