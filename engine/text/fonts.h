// The loaded fonts and localized strings, shared by everything that draws text.
#pragma once

#include <string>

#include "text/font.h"
#include "text/strings.h"

namespace text {

// Loads assets/fonts/synjUnicode_20.fnt (the game's lettering) and the
// strings for `language` from assets/text.
bool load(const std::string& assets_dir, const std::string& language);

// The game font, or null if it isn't loaded.
Font* game_font();
const Strings& strings();
// Replaces or adds a string (a port's own wording).
void set_string(int number, const std::string& utf8);

}  // namespace text
