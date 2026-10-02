#include "render/renderer.h"

#include <SDL3/SDL_log.h>

#include <algorithm>

#include "render/gl.h"

namespace render {

using namespace gl;

namespace {

const char* kVertexShader = R"(#version 330 core
layout(location = 0) in vec2 a_pos;     // twips, character space
layout(location = 1) in vec4 a_color;
layout(location = 2) in vec2 a_uv;
uniform vec3 u_row0;                    // character -> stage transform, twips
uniform vec3 u_row1;
uniform vec4 u_stage;                   // stage origin and size, twips
out vec4 v_color;
out vec2 v_uv;
void main() {
    vec2 p = vec2(dot(u_row0, vec3(a_pos, 1.0)), dot(u_row1, vec3(a_pos, 1.0)));
    vec2 ndc = (p - u_stage.xy) / u_stage.zw * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
    v_color = a_color;
    v_uv = a_uv;
}
)";

const char* kFragmentShader = R"(#version 330 core
in vec4 v_color;
in vec2 v_uv;
uniform vec4 u_tint;                    // colour transform as a multiply, 1.0 = unchanged
uniform int u_textured;
uniform sampler2D u_tex;
uniform float u_alpha_test;             // discard below this alpha (stencil writes)
out vec4 o_color;
void main() {
    vec4 c = u_textured != 0 ? texture(u_tex, v_uv) : v_color;
    if (c.a < u_alpha_test) discard;
    o_color = clamp(c * u_tint, 0.0, 1.0);
}
)";

uint32_t compile(GLenum type, const char* source) {
    uint32_t shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        SDL_Log("shader: %s", log);
    }
    return shader;
}

}  // namespace

bool Renderer::init() {
    if (!gl::load()) return false;
    program_ = glCreateProgram();
    glAttachShader(program_, compile(GL_VERTEX_SHADER, kVertexShader));
    glAttachShader(program_, compile(GL_FRAGMENT_SHADER, kFragmentShader));
    glLinkProgram(program_);
    GLint ok = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(program_, sizeof(log), nullptr, log);
        SDL_Log("program: %s", log);
        return false;
    }
    u_row0_ = glGetUniformLocation(program_, "u_row0");
    u_row1_ = glGetUniformLocation(program_, "u_row1");
    u_stage_ = glGetUniformLocation(program_, "u_stage");
    u_tint_ = glGetUniformLocation(program_, "u_tint");
    u_textured_ = glGetUniformLocation(program_, "u_textured");
    u_tex_ = glGetUniformLocation(program_, "u_tex");
    u_alpha_test_ = glGetUniformLocation(program_, "u_alpha_test");

    // One reusable quad for bitmaps: position (twips) and uv per corner.
    glGenVertexArrays(1, &quad_vao_);
    glGenBuffers(1, &quad_vbo_);
    glBindVertexArray(quad_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 16, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, nullptr);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, reinterpret_cast<void*>(sizeof(float) * 2));

    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_MULTISAMPLE);
    return true;
}

void Renderer::begin_frame(int window_width, int window_height, const swf::Rect& stage, swf::Rgba background,
                           bool transparent) {
    float stage_w = float(stage.xmax - stage.xmin), stage_h = float(stage.ymax - stage.ymin);
    float scale = std::min(float(window_width) / stage_w, float(window_height) / stage_h);
    int w = int(stage_w * scale), h = int(stage_h * scale);

    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, window_width, window_height);
    glClearColor(0, 0, 0, transparent ? 0.0f : 1.0f);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    glViewport((window_width - w) / 2, (window_height - h) / 2, w, h);
    glScissor((window_width - w) / 2, (window_height - h) / 2, w, h);
    glEnable(GL_SCISSOR_TEST);
    // castle.exe ignores SetBackgroundColor and clears to black: the logo
    // screen (#993333 in its SWF) is black in the real game.
    (void)background;

    mask_level_ = 0;
    glStencilMask(0xFF);
    glUseProgram(program_);
    glUniform4f(u_stage_, float(stage.xmin), float(stage.ymin), stage_w, stage_h);
    glUniform1i(u_tex_, 0);
    set_stencil(Stencil::Off);
}

void Renderer::set_stencil(Stencil mode) {
    switch (mode) {
    case Stencil::Off:
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glUniform1f(u_alpha_test_, 0.0f);
        mask_content();
        break;
    case Stencil::Write:
        glEnable(GL_STENCIL_TEST);
        glStencilMask(0x80);
        glStencilFunc(GL_ALWAYS, 0x80, 0x80);
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glUniform1f(u_alpha_test_, 0.5f);
        break;
    case Stencil::Test:
        glEnable(GL_STENCIL_TEST);
        glStencilMask(0x00);
        glStencilFunc(GL_EQUAL, 0x80, 0x80);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glUniform1f(u_alpha_test_, 0.0f);
        break;
    }
}

void Renderer::clear_stencil() {
    // The mods' marks only; any masks being drawn keep their levels.
    glStencilMask(0x80);
    glClearStencil(0);
    glClear(GL_STENCIL_BUFFER_BIT);
    glStencilMask(0xFF);
    mask_content();
}

void Renderer::mask_content() {
    if (mask_level_ == 0) {
        glDisable(GL_STENCIL_TEST);
        return;
    }
    glEnable(GL_STENCIL_TEST);
    glStencilMask(0x00);
    glStencilFunc(GL_EQUAL, mask_level_, 0x7F);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
}

void Renderer::mask_begin() {
    // Only inside the masks already applied; each pixel is raised once
    // (a second shape over it no longer passes the test).
    glEnable(GL_STENCIL_TEST);
    glStencilMask(0x7F);
    glStencilFunc(GL_EQUAL, mask_level_, 0x7F);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glUniform1f(u_alpha_test_, 0.5f);
}

void Renderer::mask_apply() {
    mask_level_++;
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glUniform1f(u_alpha_test_, 0.0f);
    mask_content();
}

void Renderer::mask_end_begin() {
    glEnable(GL_STENCIL_TEST);
    glStencilMask(0x7F);
    glStencilFunc(GL_EQUAL, mask_level_, 0x7F);
    glStencilOp(GL_KEEP, GL_KEEP, GL_DECR);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glUniform1f(u_alpha_test_, 0.5f);
}

void Renderer::mask_end() {
    if (mask_level_ > 0) mask_level_--;
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glUniform1f(u_alpha_test_, 0.0f);
    mask_content();
}

void Renderer::set_transform(const swf::Matrix& m, const swf::CXform& c, bool textured) {
    glUniform3f(u_row0_, m.a, m.c, m.tx);
    glUniform3f(u_row1_, m.b, m.d, m.ty);
    glUniform4f(u_tint_, c.tint(0), c.tint(1), c.tint(2), c.tint(3));
    glUniform1i(u_textured_, textured ? 1 : 0);
}

void Renderer::draw_shape(swf::Shape& shape, const swf::Matrix& matrix, const swf::CXform& cxform) {
    if (shape.gpu_mesh == 0) {
        shape.tessellate();
        GpuMesh g;
        g.index_count = int(shape.mesh.indices.size());
        glGenVertexArrays(1, &g.vao);
        glGenBuffers(1, &g.vbo);
        glGenBuffers(1, &g.ibo);
        glBindVertexArray(g.vao);
        glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(shape.mesh.vertices.size() * sizeof(swf::Vertex)),
                     shape.mesh.vertices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(shape.mesh.indices.size() * sizeof(uint32_t)),
                     shape.mesh.indices.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(swf::Vertex), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(swf::Vertex),
                              reinterpret_cast<void*>(offsetof(swf::Vertex, color)));
        meshes_.push_back(g);
        shape.gpu_mesh = uint32_t(meshes_.size());
        shape.mesh = {};  // the GPU copy is all that's needed now
    }
    const GpuMesh& g = meshes_[shape.gpu_mesh - 1];
    if (g.index_count == 0) return;
    set_transform(matrix, cxform, false);
    glBindVertexArray(g.vao);
    glDrawElements(GL_TRIANGLES, g.index_count, GL_UNSIGNED_INT, nullptr);
}

void Renderer::draw_bitmap(swf::BitmapCharacter& bitmap, const swf::Matrix& matrix, const swf::CXform& cxform) {
    if (bitmap.texture == 0) {
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, bitmap.width, bitmap.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     bitmap.rgba.data());
        if (bitmap.mipmaps) gl::glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                        bitmap.nearest ? GL_NEAREST : bitmap.mipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, bitmap.nearest ? GL_NEAREST : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        bitmap.texture = tex;
    }
    const swf::Rect& b = bitmap.bounds;
    float quad[16] = {
        float(b.xmin), float(b.ymin), 0, 0,
        float(b.xmax), float(b.ymin), 1, 0,
        float(b.xmin), float(b.ymax), 0, 1,
        float(b.xmax), float(b.ymax), 1, 1,
    };
    set_transform(matrix, cxform, true);
    gl::glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, bitmap.texture);
    glBindVertexArray(quad_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void Renderer::draw_text(text::Font& font, const std::vector<text::GlyphQuad>& quads, swf::Rgba color,
                         const swf::Matrix& matrix, const swf::CXform& cxform) {
    if (quads.empty() || font.rgba.empty()) return;
    if (font.texture == 0) {
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, font.page_width, font.page_height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     font.rgba.data());
        gl::glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        font.texture = tex;
    }
    std::vector<float> v;
    v.reserve(quads.size() * 24);
    for (const auto& q : quads) {
        const float corners[6][4] = {{q.x0, q.y0, q.u0, q.v0}, {q.x1, q.y0, q.u1, q.v0}, {q.x0, q.y1, q.u0, q.v1},
                                     {q.x1, q.y0, q.u1, q.v0}, {q.x1, q.y1, q.u1, q.v1}, {q.x0, q.y1, q.u0, q.v1}};
        for (const auto& c : corners) v.insert(v.end(), c, c + 4);
    }
    const uint8_t channels[4] = {color.r, color.g, color.b, color.a};
    swf::CXform tinted;
    for (int i = 0; i < 4; i++) tinted.mul[i] = cxform.tint(i) * channels[i] / 255.0f * 256;
    set_transform(matrix, tinted, true);
    gl::glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, font.texture);
    glBindVertexArray(quad_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo_);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(v.size() * sizeof(float)), v.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, GLsizei(quads.size() * 6));
}

void Renderer::draw_rect(const swf::Rect& rect, swf::Rgba color, const swf::Matrix& matrix,
                         const swf::CXform& cxform) {
    if (white_.rgba.empty()) {
        white_.width = white_.height = 1;
        white_.rgba = {255, 255, 255, 255};
    }
    white_.bounds = rect;
    const uint8_t channels[4] = {color.r, color.g, color.b, color.a};
    swf::CXform tinted;
    for (int i = 0; i < 4; i++) tinted.mul[i] = cxform.tint(i) * channels[i] / 255.0f * 256;
    draw_bitmap(white_, matrix, tinted);
}

}  // namespace render
