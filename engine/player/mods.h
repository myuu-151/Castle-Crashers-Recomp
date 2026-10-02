// Mods: optional replacement graphics, off unless asked for, so the default
// game stays 1:1 with the original.
//
// A mod is a folder mods/NAME/ with one folder per movie. Each movie folder
// holds pictures and a mod.txt of commands, one per line (# starts a comment):
//
//   replace ID FILE [scale S] [nearest]
//                               draw FILE (PNG) instead of character ID, fitted
//                               and centred into its bounds, S times larger;
//                               with nearest, its pixels drawn unsmoothed
//   hide ID...                  don't draw these characters
//   clip IDS to ID [boxes]      draw IDS (such as 21-31, or a list) only inside
//                               the silhouette of ID's instance in the same
//                               clip; if ID was replaced, IDS are stretched from
//                               its old bounds to the picture's. With boxes,
//                               each shape is drawn as its bounding box in its
//                               first fill colour, so it follows ID's outline
//   flash SPRITE shine IDS face ID over ID [grow PX]
//                               measure the white flash that the shine shapes
//                               IDS make in SPRITE, frame by frame (the face
//                               shape hides the shine beneath it), widen it by
//                               PX pixels and play it over ID's instance,
//                               clipped to it. Put it before hide and replace.
//
// Without a mod.txt, every ID.png in the folder replaces character ID.
// Placements, movement and animation of the characters are kept.
#pragma once

#include <filesystem>

#include "swf/movie.h"

namespace player {

// Applies mod_dir/<movie name>/ to the movie; returns how many characters
// were changed.
int apply_mod(swf::Movie& movie, const std::filesystem::path& mod_dir);

}  // namespace player
