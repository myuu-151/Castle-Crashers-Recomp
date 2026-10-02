// One running movie: castle.exe's Player, the runtime context
// of one loaded .cok6. It owns the interpreter, the root timeline (the
// Scene), the sprite prototypes and the per-frame queues, and runs a frame
// as castle.exe's Player::Update does (docs/engine/movieclips-and-frames.md
// 2.3): advance the whole tree, run the whole tree, then the goto queue and
// the loadMovie queue.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "as/interpreter.h"
#include "player/movieclip.h"
#include "swf/movie.h"

namespace player {

class Game;

// Pooled clips in use (at most 10500, as in castle.exe).
size_t live_clips();
// Movie roots in being (one a movie, and any still referred to).
size_t live_roots();
void print_clip_stats();

// A movie's nodes nothing reaches go once it has made this many more since
// the last sweep. It changes only when they're freed: at 1024, each movie
// held up to a thousand dead nodes (128 bytes each on the console) at a time.
constexpr size_t kSweepEvery = 256;

class Player {
public:
    Player(Game& game, std::unique_ptr<swf::Movie> movie, const std::string& name);
    ~Player();

    // One frame of the movie (a 30 Hz tick).
    void update(float dt);
    void render(render::Renderer& r);

    const std::string& name() const { return name_; }
    swf::Movie& movie() { return *movie_; }
    MovieClip* root() { return root_; }
    as::Interpreter& interp() { return interp_; }
    // The movie whose interpreter and goto queue this one's clips use
    // (*(player+0x4c)+0xa0): itself, or the host once it is loaded into a
    // clip (3.4).
    Player& context() { return *context_; }
    Game& game() { return game_; }

    // A new instance of sprite `id`: a clone of its prototype (frame-1
    // children included), not yet in any display list.
    MovieClip* instantiate(uint16_t id);
    // A new node for a non-sprite character (shape, bitmap, text).
    Character* instantiate_node(uint16_t id);
    // An empty clip (createEmptyMovieClip).
    MovieClip* new_empty_clip();
    // Frees the nodes that no clip's display list reaches any more (removed,
    // or their clip's slot reused) and nothing holds; run between ticks.
    void sweep_nodes();
    // A colour transform for a clip, from the interpreter's pool.
    as::Color* new_color();

    struct GotoEntry {
        MovieClip* clip;
        uint32_t serial;
        int target;
    };
    std::vector<GotoEntry> goto_queue;  // player+0x1340
    struct LoadEntry {
        MovieClip* target;
        uint32_t serial;
        as::NameId name;
    };
    std::vector<LoadEntry> load_queue;  // player+0x78 (400-entry ring)
    bool sub_movie = false;             // player+0x74: loaded by loadMovie
    // The update in which a movie finishes loading: it only becomes ready
    // (castle.exe's Player::Update returns early), so nothing advances.
    void ready_update();
    // Called after every update (captures).
    static inline std::function<void(Player&)> on_updated;
    bool paused = false;                // player+0x2861
    uint32_t frame_counter = 0;         // player+0x2864

private:
    MovieClip* build_prototype(swf::SpriteCharacter* sprite);

    Game& game_;
    std::unique_ptr<swf::Movie> movie_;
    std::string name_;
    as::Interpreter interp_;
    Player* context_ = this;
    MovieClip* root_ = nullptr;
    std::map<uint16_t, MovieClip*> prototypes_;

public:
    bool is_prototype(const MovieClip* c) const {
        for (auto& [id, p] : prototypes_)
            if (p == c) return true;
        return false;
    }

private:
    std::vector<std::unique_ptr<Character>> nodes_;  // shapes, bitmaps, text
    size_t sweep_at_ = kSweepEvery;                  // nodes_ size for the next sweep
};

// The global MovieClip pool: 10,500 clips, handed out
// round-robin skipping those still referenced, each with a new serial.
MovieClip* allocate_clip(Player* player, uint32_t type = as::kMovieClip);

}  // namespace player
