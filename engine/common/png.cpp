#include "common/png.h"

#include <algorithm>
#include <fstream>

namespace png {

namespace {

uint32_t crc32(const uint8_t* data, size_t size, uint32_t crc = 0) {
    crc = ~crc;
    for (size_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

void put32(std::vector<uint8_t>& out, uint32_t v) {
    for (int shift = 24; shift >= 0; shift -= 8) out.push_back(uint8_t(v >> shift));
}

void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& body) {
    put32(out, uint32_t(body.size()));
    size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), body.begin(), body.end());
    put32(out, crc32(&out[start], out.size() - start));
}

}  // namespace

bool write_rgba(const std::string& path, int width, int height, const uint8_t* rgba) {
    // Scanlines with filter byte 0, stored in uncompressed deflate blocks.
    std::vector<uint8_t> raw;
    for (int y = 0; y < height; y++) {
        raw.push_back(0);
        raw.insert(raw.end(), rgba + size_t(y) * width * 4, rgba + size_t(y + 1) * width * 4);
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    uint32_t a = 1, b = 0;
    for (uint8_t v : raw) {
        a = (a + v) % 65521;
        b = (b + a) % 65521;
    }
    for (size_t pos = 0; pos < raw.size() || pos == 0;) {
        size_t n = std::min<size_t>(65535, raw.size() - pos);
        bool last = pos + n >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(uint8_t(n));
        z.push_back(uint8_t(n >> 8));
        z.push_back(uint8_t(~n));
        z.push_back(uint8_t(~n >> 8));
        z.insert(z.end(), raw.begin() + ptrdiff_t(pos), raw.begin() + ptrdiff_t(pos + n));
        pos += n;
        if (last) break;
    }
    put32(z, (b << 16) | a);

    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<uint8_t> ihdr;
    put32(ihdr, uint32_t(width));
    put32(ihdr, uint32_t(height));
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});  // 8-bit RGBA
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});

    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
    return bool(f);
}

}  // namespace png
