// Offscreen rendering for screenshots and exports: a multisampled target that
// is resolved and read back as straight-alpha RGBA, top row first.
#pragma once

#include <cstdint>
#include <vector>

namespace render {

class Offscreen {
public:
    ~Offscreen() { release(); }

    // Creates and binds a width x height target. Draw after this.
    bool begin(int width, int height, int samples = 8);
    // Resolves the samples and returns the pixels. Blending onto a transparent
    // target leaves colours premultiplied, so they are divided back out.
    std::vector<uint8_t> finish();

private:
    void release();

    int width_ = 0, height_ = 0;
    uint32_t msaa_fbo_ = 0, msaa_rb_ = 0, stencil_rb_ = 0, fbo_ = 0, rb_ = 0;
};

}  // namespace render
