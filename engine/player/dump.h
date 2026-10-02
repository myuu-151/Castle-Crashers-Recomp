// Text dumps of a movie's display tree, in the format castle.exe's capture
// hook writes, so the two can be compared line by line
// or by hash (castle --verify).
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

#include "player/player.h"

namespace player {

// One line per node: depth, character, kind, name, frame and flags (clips),
// matrix in castle.exe's units, colour transform; children indented.
// `with_text` adds text fields' contents (the hook can't read those yet).
void dump_tree(FILE* out, MovieClip* clip, int indent, bool with_text);

// The tree as the hook writes it, and the variable lines of the clips named
// in CASTLE_VARS.
std::string tree_text(MovieClip* root, std::string* vars = nullptr);

// FNV-1a of a tree's text with "-0.000" read as "0.000", as the hook hashes.
uint64_t tree_hash(const std::string& text);

// "== <movie> update <n> frame <f>/<total>", "!! hash <h>", then the tree, as
// the hook writes after each Player::Update.
void dump_update(FILE* out, Player& p, unsigned update);

// Everything about a movie a script could be waiting on: the root's
// variables, then the tree with text and, after every named clip, its
// variables (F3 in the game writes this, for a screen it never leaves).
void dump_state(FILE* out, MovieClip* root);

// Run after a --dump's state (a profiling build sets it: the heap as it is
// then).
inline void (*after_dump)() = nullptr;

}  // namespace player
