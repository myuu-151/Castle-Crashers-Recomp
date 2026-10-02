// Level collision: castle.exe's BSP of wall segments and the camera
// waypoints, loaded from assets/bsp/NAME.pdag by f_BSPLoadLevel and queried by
// the f_BSP* and waypoint natives. docs/engine/bsp.md.
#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace player {

class Bsp {
public:
    // the nodes, then 200 waypoints {x, y, hit}.
    bool load(const std::filesystem::path& file);

    // f_BSPHitTest: the fraction along (x1, y1)-(x2, y2) of
    // the first wall hit, or 0; the hit wall's type, index and slope are kept.
    float hit_test(float x1, float y1, float x2, float y2);

    int closest_waypoint(float x) const;
    float& waypoint(int i, int field);      // 0: x, 1: y, 2: hit

    int last_type = 0;     // 0x651fac
    int last_index = 0;    // 0x651fb0
    float last_slope = 0;  // 0x651fb4

private:
    struct Node {
        float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        int type = 0;
        int front = -1, back = -1;  // node indices, -1 for none
        int index = 0;
    };
    float intersect(const Node& n) const;
    float traverse(int node);

    std::vector<Node> nodes_;
    float waypoints_[600] = {};
    float x1_ = 0, y1_ = 0, x2_ = 0, y2_ = 0;  // the tested segment (0x651f9c..)
};

}  // namespace player
