#include "player/bsp.h"

#include <cmath>
#include <cstring>

#include "common/files.h"

namespace player {

namespace {

// comiss sets the carry flag for "less than" and for unordered operands; the
// branches below test it, so NaN takes the "less" path.
bool below(float a, float b) { return a < b || std::isnan(a) || std::isnan(b); }

float f32(const std::vector<uint8_t>& d, size_t at) { return at + 4 <= d.size() ? files::le_f32(&d[at]) : 0.0f; }

}  // namespace

bool Bsp::load(const std::filesystem::path& file) {
    std::vector<uint8_t> d;
    if (!files::read(file.string(), d) || d.size() < 0x14) return false;
    uint32_t words = files::le32(&d[0x10]);
    size_t count = words >> 3;
    // A child is a float offset in words (8 per node); negative for none.
    auto child = [](float v) { return v >= 0.0f ? -int(v * -0.125f) : -1; };
    nodes_.assign(count, Node{});
    size_t at = 0x14;
    for (size_t i = 0; i < count; i++, at += 0x20) {
        Node& n = nodes_[i];
        n.x1 = f32(d, at + 4);
        n.y1 = f32(d, at + 8);
        n.x2 = f32(d, at + 0xc);
        n.y2 = f32(d, at + 0x10);
        n.type = int(f32(d, at + 0x14));
        n.front = child(f32(d, at + 0x18));
        n.back = child(f32(d, at + 0x1c));
        n.index = int(i);
    }
    for (int i = 0; i < 600; i++) waypoints_[i] = f32(d, at + size_t(i) * 4);
    return true;
}

// the moving segment against one wall.
float Bsp::intersect(const Node& n) const {
    float ex = n.x2 - n.x1, ey = n.y2 - n.y1;
    float dy = y2_ - y1_, dx = x2_ - x1_;
    float p = ex * dy, q = ey * dx;
    float denom = p - q;
    if (denom == 0.0f) return 0.0f;
    float t = ((n.y1 - y1_) * dx - (n.x1 - x1_) * dy) / denom;
    if (below(t, 0.0f) || below(1.0f, t)) return 0.0f;
    float denom2 = q - p;
    if (denom2 == 0.0f) return 0.0f;
    float u = ((y1_ - n.y1) * ex - (x1_ - n.x1) * ey) / denom2;
    if (below(u, 0.0f) || below(1.0f, u)) return 0.0f;
    const_cast<Bsp*>(this)->last_type = n.type;
    const_cast<Bsp*>(this)->last_index = n.index;
    const_cast<Bsp*>(this)->last_slope = ey / ex;
    return u;
}

// the side of each node the segment's ends are on decides the
// order: one side only, that child; crossing, the near child, the node's own
// wall, then the far child, stopping at the first hit.
float Bsp::traverse(int index) {
    if (index < 0 || size_t(index) >= nodes_.size()) return 0.0f;
    const Node& n = nodes_[size_t(index)];
    if (n.front < 0 && n.back < 0) return intersect(n);
    float ex = n.x2 - n.x1, ey = n.y2 - n.y1;
    float a = (y1_ - n.y1) * ex - (x1_ - n.x1) * ey;
    float b = (y2_ - n.y1) * ex - (x2_ - n.x1) * ey;
    if (!below(a, 0.0f) && !below(b, 0.0f)) return n.front >= 0 ? traverse(n.front) : 0.0f;
    if (!below(0.0f, a) && !below(0.0f, b)) return n.back >= 0 ? traverse(n.back) : 0.0f;
    auto crossing = [&](int near_child, int far_child) {
        if (near_child >= 0) {
            float r = traverse(near_child);
            if (r != 0.0f) return r;
        }
        float r = intersect(n);
        if (r != 0.0f || std::isnan(r)) return r;
        return far_child >= 0 ? traverse(far_child) : r;
    };
    if (!below(a, 0.0f) && !below(0.0f, b)) return crossing(n.front, n.back);
    if (!below(0.0f, a) && !below(b, 0.0f)) return crossing(n.back, n.front);
    return 0.0f;
}

float Bsp::hit_test(float x1, float y1, float x2, float y2) {
    x1_ = x1;
    y1_ = y1;
    x2_ = x2;
    y2_ = y2;
    last_type = 0;
    last_index = 0;
    last_slope = 0.0f;
    return nodes_.empty() ? 0.0f : traverse(0);
}

int Bsp::closest_waypoint(float x) const {
    int result = 0;
    for (int i = 0; i < 200; i++) {
        if (waypoints_[i * 3] > x) {
            result = i - 1 < 0 ? 0 : i - 1;
            break;
        }
    }
    return result;
}

float& Bsp::waypoint(int i, int field) {
    static float none = 0.0f;
    if (i < 0 || i >= 200) return none = 0.0f;
    return waypoints_[i * 3 + field];
}

}  // namespace player
