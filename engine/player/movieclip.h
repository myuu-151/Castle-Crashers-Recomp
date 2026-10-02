// Display nodes and movie clips, following castle.exe's MovieClip (vtable
// 0x49b34c). See docs/engine/movieclips-and-frames.md: the field comments
// give castle.exe's offsets.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "as/objects.h"
#include "as/runner.h"
#include "swf/movie.h"

namespace render { class Renderer; }

namespace player {

class Player;
class MovieClip;

// Anything in a display list: a movie clip, a shape or bitmap (Graphic, type
// 0x10000) or a text field (type 0x40000).
class Character : public as::Object {
public:
    explicit Character(uint32_t type) : as::Object(type) {}

    // The matrix, in castle.exe's units: a and d in percent (100 = 1.0), b and
    // c scaled by 20 (20 = 1.0), tx and ty in pixels.
    struct Matrix {
        float a = 100, d = 100, b = 0, c = 0, tx = 0, ty = 0;
    };
    // To a renderer matrix (unit scale, twips).
    swf::Matrix to_swf() const;
    // From a SWF matrix: scales /65535*100, rotate/skew /65535*20, and the
    // translation quantised to 1/256 pixel.
    void set_matrix_from_swf(const swf::Matrix& m);
    // Resets what castle.exe's Reset (vtable +4) clears; MovieClip adds more.
    void reset_node();
    // A shape, bitmap or text leaving its display list is reset too, its
    // colour transform let go: kept, every one taken off kept its slot in
    // the movie's pool of 2200 until the pool was full, and the effects
    // made after lost their transforms (fx.swf's hit box, drawn at 6%
    // alpha, became a white block over a level's chest).
    void on_last_release() override { reset_node(); }

    int depth = 0;                  // +0x20: SWF depth - 0x4000 for timeline objects
    int clip_depth = 0;             // +0x24: masks siblings up to this depth
    float cached_xscale = 0, cached_yscale = 0, cached_rotation = 0;  // +0x28..+0x30
    bool xscale_valid = false, yscale_valid = false, rotation_valid = false;  // +0x34..+0x36
    bool reached = false;           // (ours, in the padding: Player::sweep_nodes)
    Matrix matrix;                  // +0x38..+0x4c
    as::Color* cxform = nullptr;    // +0x50: own colour transform, or null
    uint16_t character_id = 0;      // +0x54
    as::NameId name = 0;            // +0x56: instance name
    uint16_t touched_frame = 0;     // +0x58
    uint16_t birth_frame = 1;       // +0x5a
    uint8_t flags5c = 0;            // +0x5c
    Character* next = nullptr;      // +0x60: next sibling
    MovieClip* parent = nullptr;    // +0x68
    swf::Character* definition = nullptr;
    std::string text;               // text fields: what they show (UTF-8)

    enum Flags5c : uint8_t { kHasMatrix = 0x01, kScriptMoved = 0x04, kContent = 0x08, kSprite = 0x10 };
};

// A text field (DynamicText, type 0x40000).
class TextField : public Character {
public:
    TextField() : Character(as::kText) {}
    bool get_member(as::Interpreter& in, as::NameId name, as::Value& out) override;
    void set_member(as::Interpreter& in, as::NameId name, const as::Value& v) override;
    int ntext = -1;
};

class MovieClip : public Character {
public:
    // State flags (+0x90).
    enum Flags : uint8_t {
        kActive = 0x01,
        kGotoThisPass = 0x02,
        kResort = 0x04,
        kLoadedMovieRoot = 0x08,
        kLoadEventDone = 0x10,
        kPlaying = 0x20,
        kStopRequested = 0x40,
        kVisible = 0x80,
    };

    explicit MovieClip(uint32_t type = as::kMovieClip) : Character(type) {}

    static MovieClip* from(as::Object* o) {
        return o && (o->type() == as::kMovieClip || o->type() == as::kScene) ? static_cast<MovieClip*>(o) : nullptr;
    }

    // Script object slots.
    void on_last_release() override;  // Reset (vtable +4)
    bool get_member(as::Interpreter& in, as::NameId name, as::Value& out) override;
    void set_member(as::Interpreter& in, as::NameId name, const as::Value& v) override;
    void call_method(as::Interpreter& in, as::NameId name, as::Args args, as::Value& result, bool& abort) override;
    as::NameId path_name() override;

    // Properties (vtable +0x58 / +0x5c): false if `id` isn't one.
    bool get_property(as::NameId id, as::Value& out);
    bool set_property(as::NameId id, const as::Value& v);
    // Bounds in the space `m` maps to, pixels: xmin, ymin, xmax, ymax.
    bool bounds(const Matrix& m, float out[4]);

    // The frame loop (docs 2.3-2.7).
    void advance_frame();          // vtable +0x3c, pass 1
    void run_frame(int frame);     // vtable +0x40, pass 2
    void execute_frame_tags(int mode, int frame);  // vtable +0x44
    void goto_frame(int target);
    int label_frame(as::NameId label) const;
    int current_frame() const { return frame ? frame : 1; }

    // The display list, a singly linked list sorted by ascending depth.
    Character* first_child = nullptr;  // +0x6c
    void insert_child(Character* child, bool replace);
    void remove_child(Character* child);  // unlink, Reset, release
    Character* child_at_depth(int d) const;
    MovieClip* child_named(as::NameId n) const;
    void resort_children();

    // Copies this clip into `dst` (CopyFrom, 0x444fe0): children cloned,
    // variables not.
    void copy_to(MovieClip* dst);
    // unloadMovie: empties the clip in place, a stopped
    // one-frame clip with no children, scripts or colour transform.
    void unload();

    void render(render::Renderer& r, const swf::Matrix& parent, const swf::CXform& parent_cxform);
    // Draws one child only (for exporting layers).
    void render_child(render::Renderer& r, Character* child, const swf::Matrix& parent);

    Player* player = nullptr;        // +0xa0
    as::Runner runner;               // +0xa4
    const swf::Timeline* timeline = nullptr;
    std::vector<std::pair<as::NameId, uint16_t>> labels;  // +0xf8, sorted
    uint32_t serial = 0;             // +0x8c
    uint8_t flags = 0;               // +0x90
    uint16_t frame_before_goto = 0;  // +0x92
    uint16_t action_frame = 0;       // +0x94
    uint16_t frame = 0;              // +0x96: current frame, 1-based, 0 = not started
    uint16_t total_frames = 0;       // +0x98
    uint32_t shader = 0;             // +0x9c
    // (Ours) setMask: the child that masks this clip, drawn only as the
    // mask, or null. The game only ever does _parent.setMask(this) (a
    // character's water box, a fish's), so it is always a child; one no
    // longer among the children is ignored.
    Character* script_mask = nullptr;

private:
    void place_object(const swf::Tag& tag, int frame);
    void update_placed(const swf::Tag& tag, bool birth);
    void remove_depth(int depth);
    float xscale();
    float yscale();
    float rotation_degrees();
    void rebuild_matrix(float xs, float ys, float degrees);
};

}  // namespace player
