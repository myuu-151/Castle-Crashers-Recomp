// Minimal PNG writer for screenshots (uncompressed).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace png {

// rgba: width * height * 4 bytes, top row first.
bool write_rgba(const std::string& path, int width, int height, const uint8_t* rgba);

}  // namespace png
