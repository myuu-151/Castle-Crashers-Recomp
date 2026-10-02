// Interned names and strings. castle.exe turns every identifier and string
// into a u16 ID, case-sensitively (the hash sums raw bytes,
// unlike Adobe's player for SWF 6), with 262 built-in names at fixed IDs.
#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>

namespace as {

using NameId = uint32_t;

class Names {
public:
    Names();

    NameId intern(std::string_view s);
    // 0 if the string was never interned.
    NameId find(std::string_view s) const;
    const std::string& str(NameId id) const;

private:
    // A deque: it grows a piece at a time and never moves a string, so the
    // map's keys can point into it. A vector copied itself into one block
    // twice the size as it filled (at 4096 names, 96 KB to 192 KB, and next
    // 384 KB in one piece: late in a session on the console, not to be had),
    // and the map kept a second copy of every string.
    std::deque<std::string> strings_;
    std::unordered_map<std::string_view, NameId> ids_;
};

// The one table, shared by everything that runs scripts.
Names& names();

}  // namespace as
