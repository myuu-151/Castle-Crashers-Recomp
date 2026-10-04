#include "swf/shape.h"

#include <algorithm>
#include <csetjmp>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <utility>

#include "tesselator.h"

namespace swf {

namespace {

// The maximum distance between a curve and its flattened polyline, in
// twips, is Shape::quality.curve_tolerance (castle.exe's own isn't known
// yet).

void read_gradient(Reader& r, FillStyle& fill, int shape_version) {
    fill.matrix = r.matrix();
    r.align();
    r.ub(2);  // spread mode
    r.ub(2);  // interpolation mode
    int count = int(r.ub(4));
    for (int i = 0; i < count; i++) {
        GradientStop stop;
        stop.ratio = r.u8();
        stop.color = shape_version >= 3 ? r.rgba() : r.rgb();
        fill.stops.push_back(stop);
    }
    if (fill.type == 0x13) r.u16();  // focal point
}

FillStyle read_fill(Reader& r, int shape_version) {
    FillStyle fill;
    fill.type = r.u8();
    if (fill.type == 0x00) {
        fill.color = shape_version >= 3 ? r.rgba() : r.rgb();
    } else if (fill.type == 0x10 || fill.type == 0x12 || fill.type == 0x13) {
        read_gradient(r, fill, shape_version);
        if (!fill.stops.empty()) fill.color = fill.stops.front().color;
    } else if (fill.type >= 0x40 && fill.type <= 0x43) {
        fill.bitmap_id = r.u16();
        fill.matrix = r.matrix();
    }
    return fill;
}

void read_styles(Reader& r, int shape_version, std::vector<FillStyle>& fills,
                 std::vector<LineStyle>& lines) {
    int count = r.u8();
    if (count == 0xFF && shape_version >= 2) count = r.u16();
    for (int i = 0; i < count; i++) fills.push_back(read_fill(r, shape_version));

    count = r.u8();
    if (count == 0xFF) count = r.u16();
    for (int i = 0; i < count; i++) {
        LineStyle line;
        line.width = r.u16();
        if (shape_version >= 4) {
            r.ub(2);  // start cap
            uint32_t join = r.ub(2);
            bool has_fill = r.ub(1);
            r.ub(3);  // no h scale, no v scale, pixel hinting
            r.ub(5);  // reserved
            r.ub(1);  // no close
            r.ub(2);  // end cap
            if (join == 2) r.u16();  // miter limit
            if (has_fill) line.color = read_fill(r, shape_version).color;
            else line.color = r.rgba();
        } else {
            line.color = shape_version >= 3 ? r.rgba() : r.rgb();
        }
        lines.push_back(line);
    }
}

struct Point {
    float x, y;
};

// Appends the edge's points after its start point.
void flatten(const Edge& e, bool reversed, std::vector<Point>& out) {
    float x0 = float(reversed ? e.x1 : e.x0), y0 = float(reversed ? e.y1 : e.y0);
    float x1 = float(reversed ? e.x0 : e.x1), y1 = float(reversed ? e.y0 : e.y1);
    if (!e.curve) {
        out.push_back({x1, y1});
        return;
    }
    float cx = float(e.cx), cy = float(e.cy);
    float dx = x0 - 2 * cx + x1, dy = y0 - 2 * cy + y1;
    float deviation = std::sqrt(dx * dx + dy * dy);
    int steps = std::max(1, int(std::ceil(std::sqrt(deviation / (8 * Shape::quality.curve_tolerance)))));
    for (int i = 1; i <= steps; i++) {
        float t = float(i) / float(steps), u = 1 - t;
        out.push_back({u * u * x0 + 2 * u * t * cx + t * t * x1,
                       u * u * y0 + 2 * u * t * cy + t * t * y1});
    }
}

Rgba lerp(Rgba a, Rgba b, float t) {
    auto mix = [t](uint8_t x, uint8_t y) { return uint8_t(std::lround(x + (y - x) * t)); };
    return {mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), mix(a.a, b.a)};
}

Rgba fill_color(const FillStyle& fill, float x, float y) {
    if (fill.stops.empty()) return fill.color;
    // Gradients live in a -16384..16384 square in gradient space.
    const Matrix& m = fill.matrix;
    float det = m.a * m.d - m.b * m.c;
    if (det == 0) return fill.color;
    float px = x - m.tx, py = y - m.ty;
    float gx = (m.d * px - m.c * py) / det;
    float gy = (-m.b * px + m.a * py) / det;
    float t = fill.type == 0x10 ? (gx + 16384) / 32768 : std::sqrt(gx * gx + gy * gy) / 16384;
    float ratio = std::clamp(t, 0.0f, 1.0f) * 255;
    const auto& stops = fill.stops;
    if (ratio <= stops.front().ratio) return stops.front().color;
    for (size_t i = 1; i < stops.size(); i++) {
        if (ratio <= stops[i].ratio) {
            float span = float(stops[i].ratio - stops[i - 1].ratio);
            return lerp(stops[i - 1].color, stops[i].color, span > 0 ? (ratio - stops[i - 1].ratio) / span : 0);
        }
    }
    return stops.back().color;
}

// Directed edges of one fill, linked end to start into closed contours.
std::vector<std::vector<Point>> build_contours(const std::vector<std::pair<const Edge*, bool>>& edges) {
    using Key = std::pair<int32_t, int32_t>;
    std::multimap<Key, size_t> by_start;
    for (size_t i = 0; i < edges.size(); i++) {
        const Edge& e = *edges[i].first;
        Key start = edges[i].second ? Key{e.x1, e.y1} : Key{e.x0, e.y0};
        by_start.emplace(start, i);
    }
    std::vector<bool> used(edges.size(), false);
    std::vector<std::vector<Point>> contours;
    for (size_t first = 0; first < edges.size(); first++) {
        if (used[first]) continue;
        std::vector<Point> contour;
        const Edge& e0 = *edges[first].first;
        Key start = edges[first].second ? Key{e0.x1, e0.y1} : Key{e0.x0, e0.y0};
        contour.push_back({float(start.first), float(start.second)});
        size_t current = first;
        while (true) {
            used[current] = true;
            auto [edge, reversed] = edges[current];
            flatten(*edge, reversed, contour);
            Key end = reversed ? Key{edge->x0, edge->y0} : Key{edge->x1, edge->y1};
            if (end == start) break;
            size_t next = SIZE_MAX;
            auto range = by_start.equal_range(end);
            for (auto it = range.first; it != range.second; ++it)
                if (!used[it->second]) { next = it->second; break; }
            if (next == SIZE_MAX) break;  // open contour; the tessellator closes it
            current = next;
        }
        if (contour.size() >= 3) contours.push_back(std::move(contour));
    }
    return contours;
}

// libtess2's memory. It doesn't check every allocation, so running out of
// memory in the middle of a fill (on a console) would crash it: every block
// is tracked, and one that can't be had jumps back to add_fill, which frees
// them all and leaves the fill out. It comes from operator new, as the rest
// of a shape's memory does (a console build keeps what tessellating needs
// apart from the game's own memory).
// (16-byte aligned: libtess2 keeps a jmp_buf in its blocks, which x64's
// setjmp saves SSE registers into with aligned stores.)
struct alignas(16) TessBlock {
    TessBlock* prev;
    TessBlock* next;
    size_t size;
};
TessBlock* g_tess_blocks = nullptr;
std::jmp_buf g_tess_out_of_memory;

void tess_link(TessBlock* b) {
    b->prev = nullptr;
    b->next = g_tess_blocks;
    if (g_tess_blocks) g_tess_blocks->prev = b;
    g_tess_blocks = b;
}

void tess_unlink(TessBlock* b) {
    if (b->prev) b->prev->next = b->next;
    else g_tess_blocks = b->next;
    if (b->next) b->next->prev = b->prev;
}

void* tess_alloc(void*, unsigned int size) {
    auto* b = static_cast<TessBlock*>(::operator new(sizeof(TessBlock) + size, std::nothrow));
    if (!b) std::longjmp(g_tess_out_of_memory, 1);
    b->size = size;
    tess_link(b);
    return b + 1;
}

void tess_free(void*, void* p) {
    if (!p) return;
    TessBlock* b = static_cast<TessBlock*>(p) - 1;
    tess_unlink(b);
    ::operator delete(b);
}

void* tess_realloc(void*, void* p, unsigned int size) {
    if (!p) return tess_alloc(nullptr, size);
    TessBlock* old = static_cast<TessBlock*>(p) - 1;
    void* moved = tess_alloc(nullptr, size);
    std::memcpy(moved, p, std::min<size_t>(old->size, size));
    tess_free(nullptr, p);
    return moved;
}

// A gradient is coloured at the mesh's vertices, the colours blended between
// them. A shape with few vertices across its gradient loses it: the cave's
// darkness (level 37, character 151) is a rectangle with a radial gradient,
// clear in the middle and black at the edges, and with only its four (black)
// corners it drew the whole screen black. So a gradient fill whose colours,
// blended, are off by more than kGradientError (of 255) at a triangle's middle
// or an edge's -- only those -- has every triangle split in four, the new
// points shared between neighbours (no seams), until it isn't (at most
// kGradientSplits times).
constexpr int kGradientError = 6, kGradientSplits = 5;

int color_error(Rgba blended, Rgba exact) {
    return std::max({std::abs(int(blended.r) - int(exact.r)), std::abs(int(blended.g) - int(exact.g)),
                     std::abs(int(blended.b) - int(exact.b)), std::abs(int(blended.a) - int(exact.a))});
}

bool gradient_off(const Mesh& mesh, const FillStyle& fill, size_t first_index) {
    auto at = [&](uint32_t i) -> const Vertex& { return mesh.vertices[i]; };
    for (size_t t = first_index; t + 2 < mesh.indices.size(); t += 3) {
        const Vertex& a = at(mesh.indices[t]);
        const Vertex& b = at(mesh.indices[t + 1]);
        const Vertex& c = at(mesh.indices[t + 2]);
        auto check = [&](const Vertex& p, const Vertex& q) {
            Rgba blended = lerp(p.color, q.color, 0.5f);
            return color_error(blended, fill_color(fill, (p.x + q.x) / 2, (p.y + q.y) / 2)) > kGradientError;
        };
        if (check(a, b) || check(b, c) || check(c, a)) return true;
        Rgba ab = lerp(a.color, b.color, 0.5f);
        Rgba middle = lerp(ab, c.color, 1.0f / 3.0f);  // (a + b + c) / 3
        if (color_error(middle, fill_color(fill, (a.x + b.x + c.x) / 3, (a.y + b.y + c.y) / 3)) > kGradientError)
            return true;
    }
    return false;
}

void refine_gradient(Mesh& mesh, const FillStyle& fill, size_t first_index) {
    if (fill.stops.empty()) return;
    for (int split = 0; split < kGradientSplits && gradient_off(mesh, fill, first_index); split++) {
        std::map<std::pair<uint32_t, uint32_t>, uint32_t> middles;
        auto middle = [&](uint32_t p, uint32_t q) {
            auto key = std::minmax(p, q);
            auto it = middles.find(key);
            if (it != middles.end()) return it->second;
            float x = (mesh.vertices[p].x + mesh.vertices[q].x) / 2, y = (mesh.vertices[p].y + mesh.vertices[q].y) / 2;
            uint32_t m = uint32_t(mesh.vertices.size());
            mesh.vertices.push_back({x, y, fill_color(fill, x, y)});
            middles.emplace(key, m);
            return m;
        };
        std::vector<uint32_t> tris(mesh.indices.begin() + ptrdiff_t(first_index), mesh.indices.end());
        mesh.indices.resize(first_index);
        for (size_t t = 0; t + 2 < tris.size(); t += 3) {
            uint32_t a = tris[t], b = tris[t + 1], c = tris[t + 2];
            uint32_t ab = middle(a, b), bc = middle(b, c), ca = middle(c, a);
            for (uint32_t i : {a, ab, ca, ab, b, bc, ca, bc, c, ab, bc, ca}) mesh.indices.push_back(i);
        }
    }
}

// False if memory ran out (the fill is left out).
bool add_fill(Mesh& mesh, const FillStyle& fill, const std::vector<std::vector<Point>>& contours) {
    static TESSalloc alloc = {tess_alloc, tess_realloc, tess_free, nullptr, 0, 0, 0, 0, 0, 0};
    TESStesselator* tess = nullptr;
    if (setjmp(g_tess_out_of_memory)) {
        while (g_tess_blocks) {
            TessBlock* b = g_tess_blocks;
            g_tess_blocks = b->next;
            ::operator delete(b);
        }
        return false;
    }
    tess = tessNewTess(&alloc);
    if (!tess) return false;
    for (const auto& c : contours)
        tessAddContour(tess, 2, c.data(), sizeof(Point), int(c.size()));
    if (tessTesselate(tess, TESS_WINDING_ODD, TESS_POLYGONS, 3, 2, nullptr)) {
        uint32_t base = uint32_t(mesh.vertices.size());
        const float* v = tessGetVertices(tess);
        int nv = tessGetVertexCount(tess);
        for (int i = 0; i < nv; i++)
            mesh.vertices.push_back({v[i * 2], v[i * 2 + 1], fill_color(fill, v[i * 2], v[i * 2 + 1])});
        const TESSindex* el = tessGetElements(tess);
        int ne = tessGetElementCount(tess);
        const size_t first_index = mesh.indices.size();
        for (int i = 0; i < ne; i++) {
            const TESSindex* tri = el + i * 3;
            if (tri[0] == TESS_UNDEF || tri[1] == TESS_UNDEF || tri[2] == TESS_UNDEF) continue;
            for (int k = 0; k < 3; k++) mesh.indices.push_back(base + uint32_t(tri[k]));
        }
        refine_gradient(mesh, fill, first_index);
    }
    tessDeleteTess(tess);
    return true;
}

void add_disc(Mesh& mesh, Point p, float radius, Rgba color) {
    const int kSides = std::max(3, Shape::quality.join_sides);
    uint32_t center = uint32_t(mesh.vertices.size());
    mesh.vertices.push_back({p.x, p.y, color});
    for (int i = 0; i < kSides; i++) {
        float a = float(i) * 6.2831853f / kSides;
        mesh.vertices.push_back({p.x + radius * std::cos(a), p.y + radius * std::sin(a), color});
    }
    for (int i = 0; i < kSides; i++)
        mesh.indices.insert(mesh.indices.end(), {center, center + 1 + uint32_t(i), center + 1 + uint32_t((i + 1) % kSides)});
}

// Strokes a polyline with round joins and caps (Flash's default line style).
void add_stroke(Mesh& mesh, const LineStyle& line, const std::vector<Point>& pts) {
    float half = std::max(float(line.width), 20.0f) / 2;  // hairlines are at least 1 px
    for (size_t i = 0; i + 1 < pts.size(); i++) {
        Point a = pts[i], b = pts[i + 1];
        float dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx * dx + dy * dy);
        if (len == 0) continue;
        float nx = -dy / len * half, ny = dx / len * half;
        uint32_t base = uint32_t(mesh.vertices.size());
        mesh.vertices.push_back({a.x + nx, a.y + ny, line.color});
        mesh.vertices.push_back({a.x - nx, a.y - ny, line.color});
        mesh.vertices.push_back({b.x + nx, b.y + ny, line.color});
        mesh.vertices.push_back({b.x - nx, b.y - ny, line.color});
        mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base + 1, base + 3, base + 2});
    }
    // Round caps at the ends, round joins between; a join where the outline
    // hardly turns can go (Shape::quality.join_min_turn, 0 on the PC).
    float min_turn = Shape::quality.join_min_turn;
    for (size_t i = 0; i < pts.size(); i++) {
        if (min_turn > 0 && i > 0 && i + 1 < pts.size()) {
            float ax = pts[i].x - pts[i - 1].x, ay = pts[i].y - pts[i - 1].y;
            float bx = pts[i + 1].x - pts[i].x, by = pts[i + 1].y - pts[i].y;
            float turn = std::atan2(std::fabs(ax * by - ay * bx), ax * bx + ay * by);
            if (turn < min_turn) continue;
        }
        add_disc(mesh, pts[i], half, line.color);
    }
}

}  // namespace

void Shape::load() {
    if (parsed) return;
    if (record) {
        Reader r(record, record_size);
        parse(r, version);
    } else if (stored && fetch) {
        std::vector<uint8_t> copy(record_size);
        if (!fetch(stored, copy.data(), copy.size())) return;
        Reader r(copy.data(), copy.size());
        parse(r, version);
    }
}

void Shape::parse(Reader& r, int shape_version) {
    parsed = true;
    fills.assign(1, {});
    lines.assign(1, {});
    paths.clear();
    groups = 1;

    size_t fill_base = 0, line_base = 0;
    read_styles(r, shape_version, fills, lines);
    int fill_bits = int(r.ub(4)), line_bits = int(r.ub(4));

    int32_t x = 0, y = 0;
    Path current;
    auto flush = [&] {
        if (!current.edges.empty()) paths.push_back(current);
        current.edges.clear();
    };

    while (true) {
        if (r.ub(1) == 0) {
            uint32_t flags = r.ub(5);
            if (flags == 0) break;
            flush();
            if (flags & 0x01) {
                int n = int(r.ub(5));
                x = r.sb(n);
                y = r.sb(n);
            }
            int64_t f0 = -1, f1 = -1, ln = -1;
            if (flags & 0x02) f0 = r.ub(fill_bits);
            if (flags & 0x04) f1 = r.ub(fill_bits);
            if (flags & 0x08) ln = r.ub(line_bits);
            if (flags & 0x10) {
                fill_base = fills.size() - 1;
                line_base = lines.size() - 1;
                read_styles(r, shape_version, fills, lines);
                fill_bits = int(r.ub(4));
                line_bits = int(r.ub(4));
                current.group = groups++;
                current.fill0 = current.fill1 = current.line = 0;
            }
            if (f0 >= 0) current.fill0 = f0 ? uint32_t(fill_base + f0) : 0;
            if (f1 >= 0) current.fill1 = f1 ? uint32_t(fill_base + f1) : 0;
            if (ln >= 0) current.line = ln ? uint32_t(line_base + ln) : 0;
        } else if (r.ub(1)) {  // straight edge
            int n = int(r.ub(4)) + 2;
            int32_t dx = 0, dy = 0;
            if (r.ub(1)) {
                dx = r.sb(n);
                dy = r.sb(n);
            } else if (r.ub(1)) {
                dy = r.sb(n);
            } else {
                dx = r.sb(n);
            }
            current.edges.push_back({x, y, 0, 0, x + dx, y + dy, false});
            x += dx;
            y += dy;
        } else {  // curved edge
            int n = int(r.ub(4)) + 2;
            int32_t cx = x + r.sb(n), cy = y + r.sb(n);
            int32_t ax = cx + r.sb(n), ay = cy + r.sb(n);
            current.edges.push_back({x, y, cx, cy, ax, ay, true});
            x = ax;
            y = ay;
        }
    }
    flush();
}

void Shape::tessellate() {
    if (tessellated) return;
    tessellated = true;
    out_of_memory = false;
    mesh = {};
    load();

    // Flash draws each style group's fills, then its lines.
    for (int group = 0; group < groups; group++) {
        for (uint32_t f = 1; f < fills.size(); f++) {
            std::vector<std::pair<const Edge*, bool>> edges;
            for (const Path& p : paths) {
                if (p.group != group) continue;
                for (const Edge& e : p.edges) {
                    if (p.fill1 == f) edges.push_back({&e, false});  // fill on the right
                    if (p.fill0 == f) edges.push_back({&e, true});   // fill on the left
                }
            }
            if (!edges.empty() && !add_fill(mesh, fills[f], build_contours(edges))) out_of_memory = true;
        }
        for (const Path& p : paths) {
            if (p.group != group || p.line == 0) continue;
            std::vector<Point> pts;
            for (const Edge& e : p.edges) {
                if (pts.empty() || pts.back().x != float(e.x0) || pts.back().y != float(e.y0)) {
                    if (pts.size() > 1) add_stroke(mesh, lines[p.line], pts);
                    pts.assign(1, {float(e.x0), float(e.y0)});
                }
                flatten(e, false, pts);
            }
            if (pts.size() > 1) add_stroke(mesh, lines[p.line], pts);
        }
    }
    // The mesh is all a renderer needs; the rest is parsed again if wanted.
    // (Swapped out: `v = {}` would keep the vectors' memory.)
    if (record || stored) {
        std::vector<Path>().swap(paths);
        std::vector<FillStyle>().swap(fills);
        std::vector<LineStyle>().swap(lines);
        parsed = false;
    }
}

}  // namespace swf
