#include "render/offscreen.h"

#include <algorithm>

#include "render/gl.h"

namespace render {

using namespace gl;

bool Offscreen::begin(int width, int height, int samples) {
    release();
    width_ = width;
    height_ = height;

    glGenRenderbuffers(1, &msaa_rb_);
    glBindRenderbuffer(GL_RENDERBUFFER, msaa_rb_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, width, height);
    glGenFramebuffers(1, &msaa_fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msaa_rb_);
    glGenRenderbuffers(1, &stencil_rb_);
    glBindRenderbuffer(GL_RENDERBUFFER, stencil_rb_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, stencil_rb_);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;

    glGenRenderbuffers(1, &rb_);
    glBindRenderbuffer(GL_RENDERBUFFER, rb_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, 0, GL_RGBA8, width, height);
    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb_);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;

    glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo_);
    return true;
}

std::vector<uint8_t> Offscreen::finish() {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, msaa_fbo_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_);
    glDisable(GL_SCISSOR_TEST);
    glBlitFramebuffer(0, 0, width_, height_, 0, 0, width_, height_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);

    size_t row = size_t(width_) * 4;
    std::vector<uint8_t> pixels(row * size_t(height_)), out(pixels.size());
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    for (int y = 0; y < height_; y++)
        std::copy_n(&pixels[size_t(height_ - 1 - y) * row], row, &out[size_t(y) * row]);
    for (size_t i = 0; i < out.size(); i += 4) {
        int a = out[i + 3];
        if (a == 0 || a == 255) continue;
        for (int k = 0; k < 3; k++) out[i + k] = uint8_t(std::min(255, out[i + k] * 255 / a));
    }
    return out;
}

void Offscreen::release() {
    if (fbo_) glDeleteFramebuffers(1, &fbo_);
    if (msaa_fbo_) glDeleteFramebuffers(1, &msaa_fbo_);
    if (rb_) glDeleteRenderbuffers(1, &rb_);
    if (msaa_rb_) glDeleteRenderbuffers(1, &msaa_rb_);
    if (stencil_rb_) glDeleteRenderbuffers(1, &stencil_rb_);
    fbo_ = msaa_fbo_ = rb_ = msaa_rb_ = stencil_rb_ = 0;
}

}  // namespace render
