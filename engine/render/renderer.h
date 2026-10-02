// Draws shapes and bitmaps with OpenGL, in the way castle.exe draws them with
// Direct3D 9: vertex-coloured triangles for vector fills and textured quads
// for bitmaps, each with a matrix and a colour transform.
#pragma once

#include <cstdint>
#include <vector>

#include "swf/movie.h"
#include "text/font.h"
#include "text/layout.h"

namespace render {

class Renderer {
public:
    bool init();

    // Starts a frame: letterboxes the stage into the window and clears it,
    // to the background colour or, if transparent, to nothing.
    void begin_frame(int window_width, int window_height, const swf::Rect& stage, swf::Rgba background,
                     bool transparent = false);

    void draw_shape(swf::Shape& shape, const swf::Matrix& matrix, const swf::CXform& cxform);
    void draw_bitmap(swf::BitmapCharacter& bitmap, const swf::Matrix& matrix, const swf::CXform& cxform);
    // Glyphs laid out by text::layout, in `color`.
    void draw_text(text::Font& font, const std::vector<text::GlyphQuad>& quads, swf::Rgba color,
                   const swf::Matrix& matrix, const swf::CXform& cxform);
    // A solid rectangle (twips, character space).
    void draw_rect(const swf::Rect& rect, swf::Rgba color, const swf::Matrix& matrix, const swf::CXform& cxform);

    // Stencil masks. In Write mode draws only mark the pixels they cover
    // (bitmaps where at least half opaque) without drawing colour; in Test
    // mode draws only reach marked pixels.
    enum class Stencil { Off, Write, Test };
    void set_stencil(Stencil mode);
    void clear_stencil();

    // SWF masks (a layer's clip depth), nested, as levels in the stencil's
    // low 7 bits (the mods' marks use bit 0x80). A mask's shapes are drawn
    // twice: after mask_begin() they raise the level where they cover, then
    // what they mask is drawn after mask_apply(); after mask_end_begin()
    // the same shapes lower it back, and mask_end() closes it.
    void mask_begin();
    void mask_apply();
    void mask_end_begin();
    void mask_end();

private:
    // Drawing inside the current masks only (or everywhere at level 0).
    void mask_content();
    int mask_level_ = 0;

    struct GpuMesh {
        uint32_t vao = 0, vbo = 0, ibo = 0;
        int index_count = 0;
    };

    void set_transform(const swf::Matrix& matrix, const swf::CXform& cxform, bool textured);

    uint32_t program_ = 0;
    int u_row0_ = -1, u_row1_ = -1, u_stage_ = -1, u_tint_ = -1, u_textured_ = -1, u_tex_ = -1;
    int u_alpha_test_ = -1;
    uint32_t quad_vao_ = 0, quad_vbo_ = 0;
    std::vector<GpuMesh> meshes_;
    swf::BitmapCharacter white_;  // 1x1 white, for draw_rect
    float stage_x_ = 0, stage_y_ = 0;  // stage origin, twips
};

}  // namespace render
