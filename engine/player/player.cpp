#include "player/player.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <tuple>

#include <SDL3/SDL_log.h>

#include "as/builtins.h"
#include "audio/audio.h"
#include "player/game.h"
#include "render/renderer.h"

namespace player {

namespace {

constexpr size_t kClipPool = 10500;  // 0x2904

// castle.exe's pool is one array of 10500 clips, made up front; so is this
// one: one block, so no heap overhead per clip, and the heap doesn't
// fragment or shrink as the pool fills. A slot not yet handed out holds a
// fresh clip, as a slot made on demand would.
std::vector<MovieClip*>& clip_pool() {
    static std::unique_ptr<MovieClip[]> clips(new MovieClip[kClipPool]);
    static std::vector<MovieClip*> pool = [] {
        std::vector<MovieClip*> p(kClipPool);
        for (size_t i = 0; i < kClipPool; i++) p[i] = &clips[i];
        return p;
    }();
    return pool;
}

// Movie roots are separate objects, outside the pool.
std::vector<std::unique_ptr<MovieClip>>& scenes() {
    static std::vector<std::unique_ptr<MovieClip>> list;
    return list;
}
// Roots of movies gone that something still referred to then.
std::vector<MovieClip*>& orphaned_roots() {
    static std::vector<MovieClip*> list;
    return list;
}
size_t g_clip_cursor = 0;
uint32_t g_serial = 0;

// Puts a pooled clip back to what the original's hands out.
void reinit(MovieClip* c, Player* player, uint32_t type) {
    c->props.clear();
    c->runner.release_functions();
    c->runner = as::Runner{};
    c->runner.owner = c;
    c->first_child = nullptr;
    decltype(c->labels)().swap(c->labels);  // (clear() would keep its memory)
    c->next = nullptr;
    c->parent = nullptr;
    c->definition = nullptr;
    c->timeline = nullptr;
    c->text.clear();
    c->matrix = Character::Matrix{};
    c->cached_xscale = c->cached_yscale = c->cached_rotation = 0;
    c->xscale_valid = c->yscale_valid = c->rotation_valid = false;
    c->cxform = nullptr;
    c->character_id = 0;
    c->name = 0;
    c->touched_frame = 0;
    c->birth_frame = 1;
    c->depth = 0;
    c->clip_depth = 0;
    c->flags5c = Character::kHasMatrix | Character::kContent;
    c->flags = 0;
    c->frame_before_goto = 0;
    c->action_frame = 0;
    c->frame = 0;
    c->total_frames = 0;
    c->shader = 0;
    c->player = player;
    c->serial = ++g_serial;
    (void)type;
}

}  // namespace

void print_clip_stats() {
    // Live clips by movie, character, whether they're a prototype, and
    // whether a parent still holds them.
    std::map<std::tuple<std::string, int, bool, bool>, int> counts;
    for (auto& c : clip_pool()) {
        if (!c->refcount) continue;
        bool proto = c->player && c->player->is_prototype(c);
        counts[{c->player ? c->player->name() : "?", c->character_id, proto, c->parent != nullptr}]++;
    }
    std::vector<std::pair<int, std::tuple<std::string, int, bool, bool>>> sorted;
    for (auto& [k, n] : counts) sorted.push_back({n, k});
    std::sort(sorted.rbegin(), sorted.rend());
    for (size_t i = 0; i < sorted.size() && i < 15; i++) {
        auto& [n, k] = sorted[i];
        std::printf("  %6d  %s id %d%s%s\n", n, std::get<0>(k).c_str(), std::get<1>(k),
                    std::get<2>(k) ? " prototype" : "", std::get<3>(k) ? "" : " (no parent)");
    }
}

size_t live_roots() {
    return scenes().size();
}

size_t live_clips() {
    size_t n = 0;
    for (auto& c : clip_pool()) n += c->refcount != 0;
    return n;
}

MovieClip* allocate_clip(Player* player, uint32_t type) {
    auto& pool = clip_pool();
    if (type == as::kScene) {
        // The root of a movie: its own object, not from the pool.
        scenes().push_back(std::make_unique<MovieClip>(as::kScene));
        MovieClip* c = scenes().back().get();
        reinit(c, player, type);
        return c;
    }
    for (size_t n = 0; n < kClipPool; n++) {
        size_t i = (g_clip_cursor + n) % kClipPool;
        MovieClip* c = pool[i];
        if (c->refcount == 0 && c->type() == as::kMovieClip) {
            g_clip_cursor = (i + 1) % kClipPool;
            reinit(c, player, type);
            return c;
        }
    }
    return nullptr;
}

Player::Player(Game& game, std::unique_ptr<swf::Movie> movie, const std::string& name)
    : game_(game), movie_(std::move(movie)), name_(name), interp_(game) {
    root_ = allocate_clip(this, as::kScene);
    root_->refcount = 1;
    root_->timeline = &movie_->root;
    root_->total_frames = uint16_t(movie_->root.frames.size());
    for (const auto& [label, frame] : movie_->root.labels)
        root_->labels.emplace_back(as::names().intern(label), uint16_t(frame + 1));
    std::sort(root_->labels.begin(), root_->labels.end());
    root_->name = as::name::k_level0;
    root_->matrix.tx = -float(movie_->stage.xmin) / 20.0f;
    root_->matrix.ty = -float(movie_->stage.ymin) / 20.0f;
    root_->flags = MovieClip::kActive | MovieClip::kPlaying | MovieClip::kVisible;
    root_->flags5c |= Character::kSprite;
    interp_.root = root_;
    // The root starts empty on frame 0; its first advance places frame 1 (the
    // real game's capture of a movie's ready update and of loadMovie clones).
}

Player::~Player() {
    // The movie's clips go back to the pool with it (castle.exe frees a
    // movie's whole display list). Its loaded movies are
    // destroyed at the same time, so no living tree still holds them.
    root_->flags = 0;
    audio::manager().unload(this);  // its sounds go, cut if playing
    for (auto& c : clip_pool()) {
        if (c->player != this) continue;
        // Its scripts' state goes, held or let go: a clip let go keeps the
        // constant pools it last ran with until its slot is handed out
        // again, and slot by slot the pool kept every level's (the
        // console lost memory level after level). None of the movie's
        // scripts runs now, and a slot handed out gets a fresh runner.
        c->runner.release_functions();
        c->runner = as::Runner{};
        c->runner.owner = c;
        if (c->refcount == 0) continue;
        c->props.clear();
        // The references the movie held go with it: its display lists' (one
        // for a clip in one) and its own on a prototype. What is left is
        // held from outside it, a variable of the main movie's (a hud's
        // player_pt keeps the level's player clip): the clip stays, emptied,
        // until that lets it go. Handed out again at once, the reference let
        // go later counted down whatever clip had the slot by then and reset
        // it in the middle of its display list, cutting off the rest (the
        // second visit to a level lost its players, shadows and bottom edge).
        int outside = int(c->refcount) - (c->parent ? 1 : 0) - (is_prototype(c) ? 1 : 0);
        c->first_child = nullptr;
        c->parent = nullptr;
        c->next = nullptr;
        c->flags = 0;
        c->refcount = uint16_t(std::max(outside, 0));
        if (c->refcount) c->player = nullptr;  // its movie is gone: it does nothing
    }
    // Its root goes too: kept, it held on to everything the movie's scripts
    // left in it (a level's worth of memory, every level), and to its
    // children, which went with the movie's nodes (a sweep marked them in
    // memory freed). One something still refers to stays, emptied.
    root_->props.clear();
    root_->first_child = nullptr;
    root_->next = nullptr;
    root_->parent = nullptr;
    root_->runner.release_functions();
    root_->runner = as::Runner{};
    root_->runner.owner = root_;
    // It goes now, or, still referred to, once nothing does (looked at as
    // each movie goes).
    orphaned_roots().push_back(root_);
    auto& roots = scenes();
    auto& orphans = orphaned_roots();
    std::erase_if(orphans, [&](MovieClip* r) {
        if (r->refcount != 0) return false;
        std::erase_if(roots, [&](const auto& s) { return s.get() == r; });
        return true;
    });
}

MovieClip* Player::build_prototype(swf::SpriteCharacter* sprite) {
    MovieClip* p = allocate_clip(this);
    if (!p) return nullptr;
    p->refcount = 1;  // prototypes live as long as the movie
    p->character_id = sprite->id;
    p->definition = sprite;
    p->timeline = &sprite->timeline;
    p->total_frames = uint16_t(sprite->timeline.frames.size());
    for (const auto& [label, frame] : sprite->timeline.labels)
        p->labels.emplace_back(as::names().intern(label), uint16_t(frame + 1));
    std::sort(p->labels.begin(), p->labels.end());
    p->flags = MovieClip::kActive | MovieClip::kPlaying | MovieClip::kVisible;
    p->flags5c |= Character::kSprite;
    prototypes_[sprite->id] = p;
    p->execute_frame_tags(1, 1);
    return p;
}

MovieClip* Player::instantiate(uint16_t id) {
    auto it = prototypes_.find(id);
    MovieClip* proto = nullptr;
    if (it != prototypes_.end()) {
        proto = it->second;
    } else {
        swf::Character* ch = movie_->character(id);
        if (!ch || ch->type != swf::CharacterType::Sprite) return nullptr;
        proto = build_prototype(static_cast<swf::SpriteCharacter*>(ch));
    }
    if (!proto) return nullptr;
    MovieClip* clip = allocate_clip(this);
    if (!clip) return nullptr;
    proto->copy_to(clip);
    clip->parent = nullptr;
    return clip;
}

Character* Player::instantiate_node(uint16_t id) {
    swf::Character* ch = movie_->character(id);
    if (!ch) return nullptr;
    std::unique_ptr<Character> node;
    if (ch->type == swf::CharacterType::EditText) {
        auto* field = static_cast<swf::EditTextCharacter*>(ch);
        auto text = std::make_unique<TextField>();
        text->text = field->initial_text;
        // castle.exe keeps a text field's colour as its colour transform:
        // the multipliers are the colour (seen in the capture: "Online
        // Interactions Not Rated", #993333, has mul 153 51 51 255).
        text->cxform = new_color();
        if (text->cxform) {
            text->cxform->mul[0] = field->color.r;
            text->cxform->mul[1] = field->color.g;
            text->cxform->mul[2] = field->color.b;
            text->cxform->mul[3] = field->color.a;
        }
        node = std::move(text);
    } else if (ch->type == swf::CharacterType::Shape || ch->type == swf::CharacterType::Bitmap) {
        node = std::make_unique<Character>(as::kGraphic);
    } else {
        return nullptr;
    }
    node->definition = ch;
    node->character_id = id;
    nodes_.push_back(std::move(node));
    return nodes_.back().get();
}

void Player::sweep_nodes() {
    if (nodes_.size() < sweep_at_) return;
    // Every pooled clip counts, in use or not: a clip let go keeps its
    // children until its slot is handed out again. Marked in the nodes
    // themselves: a sweep needs no memory (on a console there may be none
    // in one piece).
    for (auto& n : nodes_) n->reached = false;
    auto mark = [](const MovieClip& c) {
        for (Character* n = c.first_child; n; n = n->next) n->reached = true;
    };
    for (auto& c : clip_pool()) mark(*c);
    for (auto& c : scenes()) mark(*c);
    std::erase_if(nodes_, [](const std::unique_ptr<Character>& n) { return n->refcount == 0 && !n->reached; });
    sweep_at_ = nodes_.size() + kSweepEvery;
}

MovieClip* Player::new_empty_clip() {
    MovieClip* clip = allocate_clip(this);
    if (clip) clip->flags = MovieClip::kActive | MovieClip::kPlaying | MovieClip::kVisible;
    return clip;
}

as::Color* Player::new_color() {
    as::Color* c = interp_.colors.allocate();
    if (c) {
        c->reset_values();
        c->refcount++;
    }
    return c;
}

void Player::update(float dt) {
    if (std::FILE* t = as::call_trace()) std::fprintf(t, "== %s\n", name_.c_str());
    if (std::FILE* t = as::set_trace()) std::fprintf(t, "== %s\n", name_.c_str());
    // Sounds attached or started now belong to this movie.
    audio::manager().push(this);
    struct Pop {
        ~Pop() { audio::manager().pop(); }
    } pop;
    // time always goes on; the frame counter stops while the
    // game is paused; a paused movie does nothing else (its update still
    // happens, and the capture hook records it).
    interp_.time += dt;
    if (!game_.frames_stopped()) frame_counter++;
    if (paused) {
        if (on_updated) on_updated(*this);
        return;
    }
    root_->advance_frame();
    root_->run_frame(0);
    // Gotos: the target frame's actions run now (2.7).
    for (size_t i = 0; i < goto_queue.size(); i++) {
        GotoEntry e = goto_queue[i];
        if (e.clip->serial != e.serial || e.clip->refcount == 0) continue;
        e.clip->flags |= MovieClip::kPlaying;
        e.clip->run_frame(e.target);
    }
    goto_queue.clear();
    // loadMovie: each target still alive gets a clone of the loaded movie's
    // root, in its place; a movie not loaded yet stops the queue until the
    // next frame (docs/engine/movieclips-and-frames.md 3.4).
    size_t done = 0;
    for (; done < load_queue.size(); done++) {
        LoadEntry e = load_queue[done];
        // (a load dropped is logged: a level that never comes shows as a
        // black screen, and this is where it would go)
        if (e.target->refcount == 0 || e.target->serial != e.serial) {
            SDL_Log("loadMovie %s: its target is gone; dropped", as::names().str(e.name).c_str());
            continue;
        }
        Player* sub = game_.sub_movie(e.name);
        if (!sub) break;
        MovieClip* target = e.target;
        // Cloned like any clip (vtable +0x2c): a pooled
        // MovieClip, not a Scene.
        MovieClip* clone = allocate_clip(sub);
        if (!clone) {
            SDL_Log("loadMovie %s: no clip free in the pool; dropped", as::names().str(e.name).c_str());
            continue;
        }
        sub->root_->copy_to(clone);
        clone->flags |= MovieClip::kLoadedMovieRoot;
        clone->name = target->name;
        clone->depth = target->depth;
        clone->parent = target->parent;
        clone->character_id = target->character_id;
        clone->flags5c &= uint8_t(~Character::kHasMatrix);
        if (target->flags5c & Character::kHasMatrix) {
            clone->matrix = target->matrix;
            clone->flags5c |= Character::kHasMatrix;
        }
        clone->clip_depth = target->clip_depth;
        if (MovieClip* parent = target->parent) {
            parent->remove_child(target);
            parent->insert_child(clone, true);
        }
        sub->interp_.root = interp_.root;  // its _root is the host's
        sub->context_ = context_;          // and its scripts run in the host's context
    }
    load_queue.erase(load_queue.begin(), load_queue.begin() + ptrdiff_t(done));
    root_->resort_children();
    if (on_updated) on_updated(*this);
}

void Player::ready_update() {
    if (on_updated) on_updated(*this);
}

void Player::render(render::Renderer& r) {
    root_->render(r, root_->to_swf(), swf::CXform{});
}

}  // namespace player
