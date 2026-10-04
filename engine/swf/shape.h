// Vector shapes: parsing DefineShape records and tessellating them into
// vertex-coloured triangles, the form castle.exe draws them in
// (VertexColorFillPS).
#pragma once

#include <cstdint>
#include <vector>

#include "swf/types.h"

namespace swf {

struct GradientStop {
    uint8_t ratio;
    Rgba color;
};

struct FillStyle {
    uint8_t type = 0;  // 0x00 solid, 0x10 linear, 0x12 radial, 0x13 focal, 0x4x bitmap
    Rgba color;
    Matrix matrix;     // gradient or bitmap space -> shape space
    std::vector<GradientStop> stops;
    uint16_t bitmap_id = 0;
};

struct LineStyle {
    uint16_t width = 0;  // twips
    Rgba color;
};

struct Edge {
    int32_t x0, y0;      // start
    int32_t cx, cy;      // control point (curves only)
    int32_t x1, y1;      // end
    bool curve;
};

// Edges between two style changes that share the same styles. Style indices
// are global (into Shape::fills / Shape::lines), 0 meaning none.
struct Path {
    uint32_t fill0 = 0, fill1 = 0, line = 0;
    int group = 0;  // bumped by each NewStyles record
    std::vector<Edge> edges;
};

struct Vertex {
    float x, y;  // twips, shape space
    Rgba color;
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

// How finely shapes are tessellated, for every shape (Shape::quality; a
// console sets it coarser: its GPU is the bottleneck in the busiest
// screens). The defaults are the PC's.
struct TessellationQuality {
    float curve_tolerance = 2.0f;  // twips between a curve and its polyline
    int join_sides = 12;           // of the disc rounding each point of an outline
    // A point where an outline turns less than this (radians) gets no disc:
    // the segments' ends meet with no gap to speak of.
    float join_min_turn = 0.0f;
};

struct Shape {
    static inline TessellationQuality quality;

    std::vector<FillStyle> fills;  // index 0 unused
    std::vector<LineStyle> lines;  // index 0 unused
    std::vector<Path> paths;
    int groups = 1;

    // The SHAPEWITHSTYLE record, in the movie's data. It is parsed when first
    // needed: most shapes are never drawn, and parsed edges take about ten
    // times the record's size.
    const uint8_t* record = nullptr;
    size_t record_size = 0;
    int version = 0;
    bool parsed = false;
    // The record kept out of main memory by a console (stash: the GameCube
    // puts it in ARAM, where its 1.5 MB of player and effects shapes don't
    // take the levels' memory): `record` is null and this is its handle, and
    // load() copies it back to parse it. 0: the record is in the movie's data.
    uint32_t stored = 0;

    // A console's store for records (none on the PC): stash copies a record
    // out and returns its handle (0: not stored, it stays in the data), fetch
    // copies it back into `out`, release frees it.
    static inline uint32_t (*stash)(const uint8_t* record, size_t size) = nullptr;
    static inline bool (*fetch)(uint32_t handle, uint8_t* out, size_t size) = nullptr;
    static inline void (*release)(uint32_t handle) = nullptr;

    bool tessellated = false;
    bool out_of_memory = false;  // memory ran out tessellating: fills are missing
    Mesh mesh;
    uint32_t gpu_mesh = 0;  // renderer handle

    // Parses SHAPEWITHSTYLE for DefineShape version 1-4.
    void parse(Reader& r, int shape_version);
    // Parses the record, if it isn't parsed: styles and paths.
    void load();
    // Triangulates the shape into `mesh`. The paths are dropped afterwards
    // (load() parses them again); the styles stay.
    void tessellate();
};

}  // namespace swf
