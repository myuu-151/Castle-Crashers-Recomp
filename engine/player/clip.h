// Movie clips: a timeline playing over a display list of placed characters.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "swf/movie.h"

namespace render { class Renderer; }

namespace player {

class Clip;

// One placed character at a depth of a clip's display list.
struct DisplayObject {
    uint16_t character_id = 0;
    swf::Character* character = nullptr;
    swf::Matrix matrix;
    swf::CXform cxform;
    uint16_t ratio = 0;
    uint16_t clip_depth = 0;  // non-zero: a mask over depths up to this one
    std::string name;
    std::string text;            // for text fields, UTF-8
    std::unique_ptr<Clip> clip;  // for sprites
    bool placed_this_frame = false;
};

// A DoAction waiting to run at the end of the frame.
struct QueuedAction {
    Clip* clip;
    swf::Tag tag;
};

class Clip {
public:
    Clip(swf::Movie* movie, const swf::Timeline* timeline, Clip* parent);

    // Advances one frame if playing, then advances the children.
    void tick(std::vector<QueuedAction>& actions);
    // Moves the playhead as gotoAndStop/gotoAndPlay do (0-based frame).
    void goto_frame(int frame, std::vector<QueuedAction>& actions);

    void play() { playing_ = true; }
    void stop() { playing_ = false; }
    int current_frame() const { return frame_; }
    int total_frames() const { return int(timeline_->frames.size()); }

    void render(render::Renderer& renderer, const swf::Matrix& parent_matrix,
                const swf::CXform& parent_cxform) const;
    // Grows `out` (twips) by everything drawn, under matrix m. Returns false if
    // nothing is drawn.
    bool bounds(const swf::Matrix& m, swf::Rect& out) const;
    // Draws only the object at one depth (for exporting layers).
    void render_depth(int depth, render::Renderer& renderer, const swf::Matrix& parent_matrix,
                      const swf::CXform& parent_cxform) const;

    swf::Movie* movie() const { return movie_; }
    Clip* parent() const { return parent_; }
    const std::map<int, DisplayObject>& children() const { return children_; }

private:
    // Runs the control tags of one frame. Actions are queued only when
    // run_actions is set (skipped frames of a goto don't run theirs).
    void run_frame(int frame, bool run_actions, std::vector<QueuedAction>& actions);
    void place(const swf::Tag& tag, std::vector<QueuedAction>& actions);
    void remove(int depth) { children_.erase(depth); }

    swf::Movie* movie_;
    const swf::Timeline* timeline_;
    Clip* parent_;
    int frame_ = -1;
    bool playing_ = true;
    std::map<int, DisplayObject> children_;
    // While rebuilding for a backwards goto: the previous display list, whose
    // instances are kept when the same character is placed at the same depth.
    std::map<int, DisplayObject>* reuse_ = nullptr;
};

}  // namespace player
