// Whole-file reads. Every asset (SWFs, fonts, strings, collision) is read
// through files::read, so a platform whose files live elsewhere (inside a
// disc image) links its own files.cpp instead of this one.
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace files {

// The whole file at `path` into `out`; false if it can't be read.
bool read(const std::string& path, std::vector<uint8_t>& out);
bool exists(const std::string& path);

// A part of a file a read can leave out: the pixels of a large bitmap in a
// SWF, which go to a texture and aren't kept. Read whole, a SWF with two
// 1 MB skies needed 2.35 MB in one piece, more than a console had.
struct Hole {
    uint32_t offset = 0, size = 0;  // in the file
};
// The holes a platform knows for a file, in file order (none: read it whole).
std::vector<Hole> holes(const std::string& path);
// A hole's bytes read a part at a time (`offset` and `size` within the hole),
// for a platform that can't hold a big one whole: a GameCube, where a sky's
// 1 MB of pixels in one piece wasn't to be had late in a session.
struct HoleStream {
    bool (*read)(void* self, uint32_t offset, uint32_t size, uint8_t* out) = nullptr;
    void* self = nullptr;
    bool get(uint32_t offset, uint32_t size, uint8_t* out) const { return read && read(self, offset, size, out); }
};
// The file without its holes into `out`. Each hole goes to `take`, in order,
// once all of the file before it is in `out` (up to `at`, the hole's place
// there): its bytes, or (bytes null) a stream to read them from; either lasts
// only for the call. False if it can't be read or `take` says no.
using TakeHole = bool (*)(void* context, size_t index, size_t at, const uint8_t* bytes, const HoleStream* stream);
bool read_holed(const std::string& path, const std::vector<Hole>& holes, std::vector<uint8_t>& out, TakeHole take,
                void* context);

// Little-endian values in a file, whatever the host's byte order.
inline uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
inline float le_f32(const uint8_t* p) {
    uint32_t bits = le32(p);
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

}  // namespace files
