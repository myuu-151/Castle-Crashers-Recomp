#include "player/movieclip.h"

#include <algorithm>
#include <cmath>

#include "as/builtins.h"
#include "as/interpreter.h"
#include "player/game.h"
#include "player/player.h"
#include "render/renderer.h"
#include "text/fonts.h"
#include "text/layout.h"

namespace player {

using namespace as;

namespace {


enum TagCode : uint16_t {
    kRemoveObject = 5,
    kDoAction = 12,
    kPlaceObject2 = 26,
    kRemoveObject2 = 28,
};

// Unit-scale 2x3 matrix product helpers for bounds.
struct M {
    float a, b, c, d, tx, ty;  // x' = a x + c y + tx; y' = b x + d y + ty (pixels)
};
M unit(const Character::Matrix& m) { return {m.a / 100, m.b / 20, m.c / 20, m.d / 100, m.tx, m.ty}; }
M mul(const M& p, const M& q) {
    return {p.a * q.a + p.c * q.b, p.b * q.a + p.d * q.b, p.a * q.c + p.c * q.d,
            p.b * q.c + p.d * q.d, p.a * q.tx + p.c * q.ty + p.tx, p.b * q.tx + p.d * q.ty + p.ty};
}

void grow(float out[4], bool& any, const M& m, const swf::Rect& r) {
    float xs[2] = {r.xmin / 20.0f, r.xmax / 20.0f}, ys[2] = {r.ymin / 20.0f, r.ymax / 20.0f};
    for (float x : xs)
        for (float y : ys) {
            float px = m.a * x + m.c * y + m.tx, py = m.b * x + m.d * y + m.ty;
            if (!any) {
                out[0] = out[2] = px;
                out[1] = out[3] = py;
                any = true;
            }
            out[0] = std::min(out[0], px);
            out[1] = std::min(out[1], py);
            out[2] = std::max(out[2], px);
            out[3] = std::max(out[3], py);
        }
}

bool bounds_of(MovieClip* clip, const M& m, float out[4], bool& any) {
    for (Character* c = clip->first_child; c; c = c->next) {
        M cm = (c->flags5c & Character::kHasMatrix) ? mul(m, unit(c->matrix)) : m;
        if (c->type() == kGraphic && c->definition) {
            if (c->definition->type == swf::CharacterType::Shape)
                grow(out, any, cm, static_cast<swf::ShapeCharacter*>(c->definition)->bounds);
            else if (c->definition->type == swf::CharacterType::Bitmap)
                grow(out, any, cm, static_cast<swf::BitmapCharacter*>(c->definition)->bounds);
        } else if (MovieClip* mc = MovieClip::from(c)) {
            bounds_of(mc, cm, out, any);
        }
    }
    return any;
}

}  // namespace

// ---- Character

swf::Matrix Character::to_swf() const {
    swf::Matrix m;
    m.a = matrix.a / 100;
    m.d = matrix.d / 100;
    m.b = matrix.b / 20;
    m.c = matrix.c / 20;
    m.tx = matrix.tx * 20;
    m.ty = matrix.ty * 20;
    return m;
}

void Character::set_matrix_from_swf(const swf::Matrix& m) {
    // swf::Matrix holds raw/65536; castle.exe divides the raw value by 65535,
    // and only for the terms present: absent scales are
    // exactly 100, absent rotate/skew exactly 0.
    matrix.a = m.has_scale ? m.a * 65536.0f / 65535.0f * 100.0f : 100.0f;
    matrix.d = m.has_scale ? m.d * 65536.0f / 65535.0f * 100.0f : 100.0f;
    matrix.b = m.has_rotate ? m.b * 65536.0f / 65535.0f * 20.0f : 0.0f;
    matrix.c = m.has_rotate ? m.c * 65536.0f / 65535.0f * 20.0f : 0.0f;
    matrix.tx = std::floor(m.tx * 256.0f * 0.05f + 0.5f) / 256.0f;
    matrix.ty = std::floor(m.ty * 256.0f * 0.05f + 0.5f) / 256.0f;
    xscale_valid = yscale_valid = rotation_valid = false;
    flags5c |= kHasMatrix;
}

void Character::reset_node() {
    props.clear();
    if (cxform) {
        cxform->clip = nullptr;
        release(kColor, cxform);
        cxform = nullptr;
    }
    name = 0;
    touched_frame = 0;
    birth_frame = 1;
    depth = 0;
    next = nullptr;
    clip_depth = 0;
    flags5c &= 0xF0;
}

// ---- Text fields

bool TextField::get_member(Interpreter&, NameId n, Value& out) {
    if (n == name::kntext) {
        out.set_int(ntext);
        return true;
    }
    return false;
}

void TextField::set_member(Interpreter&, NameId n, const Value& v) {
    if (n == name::kntext) {
        ntext = to_int(v);
        text = text::strings().get(ntext);
    } else if (n == name::ktext) {
        text = names().str(to_name(v));
    }
}

// ---- MovieClip: script slots

void MovieClip::on_last_release() {
    // Reset (0x444e90), when the clip leaves a display list. First the
    // clip's own part: every child is reset in turn and let go
    //, so a removed clip's whole subtree returns to the pool;
    // the timeline, labels and scripts go; flags back to active, playing and
    // visible; a loaded movie's root lets its movie go.
    for (Character* c = first_child; c;) {
        Character* next_child = c->next;
        c->parent = nullptr;
        c->on_last_release();
        if (c->refcount > 0) c->refcount--;
        c = next_child;
    }
    first_child = nullptr;
    definition = nullptr;
    timeline = nullptr;
    decltype(labels)().swap(labels);  // (clear() would keep its memory)
    bool loaded_root = flags & kLoadedMovieRoot;
    flags = uint8_t((flags & kLoadedMovieRoot) | 0xa1);
    if (loaded_root && player) {
        flags &= uint8_t(~kLoadedMovieRoot);
        player->game().release_loaded_movie(player);
    }
    parent = nullptr;
    shader = 0;
    script_mask = nullptr;
    frame_before_goto = 0;
    action_frame = 0;
    frame = 0;
    total_frames = 1;
    runner.release_functions();
    runner.load_code = {};
    runner.enter_code = {};
    runner.frame_actions.clear();
    runner.frame_pools.clear();
    // Then the node's part: variables, colour transform, name, depth, flags.
    reset_node();
}

NameId MovieClip::path_name() {
    std::string path;
    for (MovieClip* c = this; c; c = c->parent) {
        if (c->name == 0) continue;
        path = path.empty() ? names().str(c->name) : names().str(c->name) + "." + path;
    }
    return names().intern(path);
}

bool MovieClip::get_member(Interpreter& in, NameId n, Value& out) {
    if (get_property(n, out)) return true;
    if (Cell* c = props.find(n)) {
        out = c->load();
        return true;
    }
    for (Character* c = first_child; c; c = c->next)
        if (c->name == n) {
            out.set_object(c);
            return true;
        }
    auto f = runner.functions.find(n);
    if (f != runner.functions.end()) {
        out.set_object(f->second);
        return true;
    }
    (void)in;
    return false;
}

void MovieClip::set_member(Interpreter&, NameId n, const Value& v) {
    if (!v.is_undefined() && set_property(n, v)) return;
    props.get_or_add(n).store(v);
}

// ---- Properties (1.3, 1.4)

float MovieClip::xscale() { return xscale_valid ? cached_xscale : std::sqrt(matrix.a * matrix.a + matrix.b * matrix.b); }
float MovieClip::yscale() { return yscale_valid ? cached_yscale : std::sqrt(matrix.d * matrix.d + matrix.c * matrix.c); }
float MovieClip::rotation_degrees() {
    if (rotation_valid) return cached_rotation;
    // the original's, operation for operation (the rounding shows in _rotation).
    float t = std::atan(matrix.b / (matrix.a * 0.2f));
    t = t * 180.0f;
    return t * 0.31830987f;
}

void MovieClip::rebuild_matrix(float xs, float ys, float degrees) {
    // the original's (and the _xscale / _yscale setters around it), with its
    // operations in the same order: the products are rounded differently
    // otherwise, and a rotated clip's matrix ends up a unit off in the last
    // place (seen as a scale of 12.156 where the game has 12.155).
    float r = degrees * 0.0055555557f;
    r = r * 3.14159274f;
    float cos_r = std::cos(r);
    matrix.d = cos_r * ys;
    matrix.a = cos_r * xs;
    float sin_r = std::sin(r);
    float sin_b = sin_r * 20.0f;
    float sin_c = sin_r * -20.0f;
    matrix.b = (xs * 0.01f) * sin_b;
    matrix.c = (ys * 0.01f) * sin_c;
    cached_xscale = xs;
    cached_yscale = ys;
    cached_rotation = degrees;
    xscale_valid = yscale_valid = rotation_valid = true;
    flags5c |= kScriptMoved;
}

bool MovieClip::bounds(const Matrix& m, float out[4]) {
    bool any = false;
    M own = (flags5c & kHasMatrix) ? mul(unit(m), unit(matrix)) : unit(m);
    return bounds_of(this, own, out, any);
}

bool MovieClip::get_property(NameId id, Value& out) {
    float b[4];
    switch (id) {
    case name::k_x: out.set_float(matrix.tx); return true;
    case name::k_y: out.set_float(matrix.ty); return true;
    case name::k_xscale: out.set_float(xscale()); return true;
    case name::k_yscale: out.set_float(yscale()); return true;
    case name::k_currentframe: out.set_int(current_frame()); return true;
    case name::k_alpha: out.set_int(cxform ? cxform->mul[3] : 255); return true;
    case name::k_visible: out.set_bool((flags & kVisible) != 0); return true;
    case name::k_width:
        out.set_float(bounds(Matrix{}, b) ? b[2] - b[0] : 0.0f);
        return true;
    case name::k_height:
        out.set_float(bounds(Matrix{}, b) ? b[3] - b[1] : 0.0f);
        return true;
    case name::k_rotation: out.set_float(rotation_degrees()); return true;
    case name::k_name: out.set_string(name); return true;
    case name::kthis: out.set_object(this); return true;
    case name::k_parent:
        if (parent) out.set_object(parent);
        else out.set_undefined();
        return true;
    default: return false;
    }
}

bool MovieClip::set_property(NameId id, const Value& v) {
    if (v.is_undefined()) return false;
    switch (id) {
    case name::k_x:
        matrix.tx = to_float(v);
        flags5c |= kScriptMoved;
        return true;
    case name::k_y:
        matrix.ty = to_float(v);
        flags5c |= kScriptMoved;
        return true;
    case name::k_xscale: rebuild_matrix(to_float(v), yscale(), rotation_degrees()); return true;
    case name::k_yscale: rebuild_matrix(xscale(), to_float(v), rotation_degrees()); return true;
    case name::k_rotation: {
        int32_t q = int32_t(to_float(v) * 128.0f);
        q %= 46080;
        if (q > 23040) q -= 46080;
        else if (q < -23040) q += 46080;
        rebuild_matrix(xscale(), yscale(), float(q) / 128.0f);
        return true;
    }
    case name::k_alpha: {
        if (!cxform && player) cxform = player->new_color();
        if (cxform) {
            int16_t a = int16_t(to_int(v));
            cxform->mul[3] = int16_t(int32_t(float(a) * 2.55f));
            if (cxform->clip) cxform->apply_to_clip();
        }
        return true;
    }
    case name::k_visible:
        if (to_bool(v)) flags |= kVisible;
        else flags &= uint8_t(~kVisible);
        return true;
    case name::k_height: {
        float b[4];
        float h = bounds(Matrix{}, b) ? b[3] - b[1] : 0.0f;
        rebuild_matrix(xscale(), h != 0 ? to_float(v) / h * 100.0f : yscale(), rotation_degrees());
        return true;
    }
    case name::k_name: name = to_name(v); return true;
    case name::k_quality: return true;
    default: return false;
    }
}

// ---- Display list

void MovieClip::insert_child(Character* child, bool replace) {
    child->parent = this;
    Character** link = &first_child;
    while (*link && (*link)->depth < child->depth) link = &(*link)->next;
    if (*link && (*link)->depth == child->depth) {
        if (!replace) return;
        Character* old = *link;
        *link = old->next;
        old->next = nullptr;
        old->parent = nullptr;
        old->on_last_release();
        if (old->refcount > 0) old->refcount--;
    }
    child->next = *link;
    *link = child;
    child->refcount++;
}

void MovieClip::remove_child(Character* child) {
    for (Character** link = &first_child; *link; link = &(*link)->next) {
        if (*link != child) continue;
        *link = child->next;
        child->next = nullptr;
        child->parent = nullptr;
        child->on_last_release();
        if (child->refcount > 0) child->refcount--;
        return;
    }
}

Character* MovieClip::child_at_depth(int d) const {
    for (Character* c = first_child; c; c = c->next)
        if (c->depth == d) return c;
    return nullptr;
}

MovieClip* MovieClip::child_named(NameId n) const {
    for (Character* c = first_child; c; c = c->next)
        if (c->name == n) return MovieClip::from(c);
    return nullptr;
}

void MovieClip::resort_children() {
    if (flags & kResort) {
        std::vector<Character*> list;
        for (Character* c = first_child; c; c = c->next) list.push_back(c);
        std::stable_sort(list.begin(), list.end(), [](Character* x, Character* y) { return x->depth < y->depth; });
        first_child = nullptr;
        for (auto it = list.rbegin(); it != list.rend(); ++it) {
            (*it)->next = first_child;
            first_child = *it;
        }
        flags &= uint8_t(~kResort);
    }
    for (Character* c = first_child; c; c = c->next)
        if (MovieClip* mc = MovieClip::from(c)) mc->resort_children();
}

void MovieClip::unload() {
    props.clear();
    if (cxform) {
        cxform->clip = nullptr;
        as::release(as::kColor, cxform);
        cxform = nullptr;
    }
    while (first_child) remove_child(first_child);
    timeline = nullptr;
    decltype(labels)().swap(labels);  // (clear() would keep its memory)
    flags = uint8_t((flags & 0xd8) | kStopRequested);
    frame_before_goto = 1;
    action_frame = 1;
    frame = 1;
    total_frames = 1;
    shader = 0;
    script_mask = nullptr;
    runner.release_functions();
    runner.load_code = {};
    runner.enter_code = {};
    runner.frame_actions.clear();
    runner.frame_pools.clear();
}

void MovieClip::copy_to(MovieClip* dst) {
    dst->timeline = timeline;
    dst->definition = definition;
    dst->character_id = character_id;
    dst->runner.load_code = runner.load_code;
    dst->runner.enter_code = runner.enter_code;
    dst->labels = labels;
    dst->total_frames = total_frames;
    dst->name = name;
    dst->player = player;
    dst->parent = parent;
    dst->matrix = matrix;
    dst->cached_xscale = cached_xscale;
    dst->cached_yscale = cached_yscale;
    dst->cached_rotation = cached_rotation;
    dst->xscale_valid = xscale_valid;
    dst->yscale_valid = yscale_valid;
    dst->rotation_valid = rotation_valid;
    dst->flags5c = uint8_t((dst->flags5c & ~kHasMatrix) | (flags5c & kHasMatrix) | kContent | (flags5c & kSprite));
    if (cxform) {
        dst->cxform = player->new_color();
        if (dst->cxform) {
            for (int i = 0; i < 4; i++) {
                dst->cxform->mul[i] = cxform->mul[i];
                dst->cxform->add[i] = cxform->add[i];
            }
        }
    }
    dst->frame = 0;
    dst->action_frame = 0;
    dst->touched_frame = 0;
    dst->shader = 0;
    dst->script_mask = nullptr;
    dst->flags = uint8_t((flags & 0xad) | 0xa0);
    // Children, cloned recursively.
    Character* tail = nullptr;
    for (Character* c = first_child; c; c = c->next) {
        Character* copy = nullptr;
        if (MovieClip* mc = MovieClip::from(c)) {
            MovieClip* clone = allocate_clip(player);
            mc->copy_to(clone);
            copy = clone;
        } else {
            copy = player->instantiate_node(c->character_id);
            if (!copy) continue;
            copy->matrix = c->matrix;
            copy->flags5c = c->flags5c;
            copy->text = c->text;
            if (c->cxform) {
                copy->cxform = player->new_color();
                if (copy->cxform)
                    for (int i = 0; i < 4; i++) {
                        copy->cxform->mul[i] = c->cxform->mul[i];
                        copy->cxform->add[i] = c->cxform->add[i];
                    }
            }
        }
        copy->depth = c->depth;
        copy->clip_depth = c->clip_depth;
        copy->name = c->name;
        copy->birth_frame = c->birth_frame;
        copy->parent = dst;
        copy->refcount++;
        copy->next = nullptr;
        if (tail) tail->next = copy;
        else dst->first_child = copy;
        tail = copy;
    }
}

// ---- Frame loop (2.4-2.7)

int MovieClip::label_frame(NameId label) const {
    auto it = std::lower_bound(labels.begin(), labels.end(), label,
                               [](const std::pair<NameId, uint16_t>& e, NameId n) { return e.first < n; });
    return it != labels.end() && it->first == label ? it->second : 0;
}

void MovieClip::remove_depth(int d) {
    if (Character* c = child_at_depth(d)) remove_child(c);
}

void MovieClip::place_object(const swf::Tag& tag, int frame_number) {
    swf::Movie& movie = player->movie();
    swf::Reader r(movie.data.data(), tag.offset + tag.length, tag.offset);
    uint8_t f = r.u8();
    int d = int(r.u16()) - 0x4000;
    bool has_character = f & 0x02;
    uint16_t id = has_character ? r.u16() : 0;
    Character* node = child_at_depth(d);
    bool in_place = false;
    // A Move that brings a new character without a matrix keeps the old
    // node's matrix (the original's copies it with the original's).
    bool inherit_matrix = has_character && (f & 0x01) && !(f & 0x04) && node && node->character_id != id;
    Character::Matrix inherited = node ? node->matrix : Character::Matrix{};
    uint8_t inherited_flags = node ? uint8_t(node->flags5c & Character::kHasMatrix) : 0;

    if (!has_character) {
        if (!node) return;
    } else if (node && node->character_id == id) {
        // Same character: updated in place (vtable +0x64), which differs by
        // class. A MovieClip (0x444c10) drops its colour transform, is made
        // active and loses its name; a Graphic (0x44a6f0) drops its colour
        // transform and its matrix flag; text (0x40ba80) keeps everything.
        in_place = true;
        bool clip = MovieClip::from(node) != nullptr;
        if (node->cxform && node->type() != kText) {
            node->cxform->clip = nullptr;
            release(kColor, node->cxform);
            node->cxform = nullptr;
        }
        if (MovieClip* mc = MovieClip::from(node)) {
            mc->flags |= kActive;
            mc->name = 0;
        } else if (!clip && node->type() != kText) {
            node->flags5c &= uint8_t(~Character::kHasMatrix);
        }
    } else {
        node = movie.character(id) && movie.character(id)->type == swf::CharacterType::Sprite
                   ? static_cast<Character*>(player->instantiate(id))
                   : player->instantiate_node(id);
        if (!node) return;
        node->birth_frame = uint16_t(current_frame());
        node->depth = d;
        if (inherit_matrix) {
            node->matrix = inherited;
            node->flags5c = uint8_t((node->flags5c & ~Character::kHasMatrix) | inherited_flags);
        }
        insert_child(node, true);
    }

    swf::Matrix m;
    bool has_matrix = f & 0x04;
    if (has_matrix) m = r.matrix();
    // CXFORMWITHALPHA, parsed into a fresh transform (multipliers
    // 255, adds 0; the original's writes the present terms and clamps negative
    // multipliers to 0), which is then copied whole into the node's own
    //: absent terms reset to those defaults.
    bool has_cxform = f & 0x08;
    int16_t cx_mul[4] = {255, 255, 255, 255}, cx_add[4] = {0, 0, 0, 0};
    if (has_cxform) {
        r.align();
        bool has_add = r.ub(1), has_mul = r.ub(1);
        int n = int(r.ub(4));
        if (has_mul)
            for (auto& mv : cx_mul) mv = int16_t(std::max(0, r.sb(n)));
        if (has_add)
            for (auto& a : cx_add) a = int16_t(r.sb(n));
        r.align();
    }
    if (f & 0x10) r.u16();  // ratio
    NameId new_name = 0;
    bool has_name = f & 0x20;
    if (has_name) new_name = names().intern(r.cstring());
    int new_clip_depth = (f & 0x40) ? int(r.u16()) - 0x4000 : 0;

    node->depth = d;
    node->character_id = has_character ? id : node->character_id;
    if (frame_number) node->touched_frame = uint16_t(frame_number);
    node->parent = this;
    // A clip moved by script keeps its matrix while the tag keeps its name.
    // A node updated in place that a script moved keeps its matrix only if the
    // tag names it with the name it has (a clip's was just cleared, so never);
    // otherwise the timeline takes it back (0x44e804).
    bool keep = false;
    if (in_place && (node->flags5c & kScriptMoved)) {
        if (has_name && new_name == node->name) keep = true;
        else node->flags5c &= uint8_t(~kScriptMoved);
    }
    if (has_matrix && !keep) node->set_matrix_from_swf(m);
    if (has_cxform) {
        if (!node->cxform) node->cxform = player->new_color();
        if (node->cxform) {
            for (int i = 0; i < 4; i++) {
                node->cxform->mul[i] = cx_mul[i];
                node->cxform->add[i] = cx_add[i];
            }
        }
    }
    if (has_name) node->name = new_name;
    if (f & 0x40) node->clip_depth = new_clip_depth;

    // Clip actions: only onClipEvent(load) and onClipEvent(enterFrame).
    if ((f & 0x80) && MovieClip::from(node)) {
        MovieClip* mc = MovieClip::from(node);
        r.u16();  // reserved
        r.u32();  // all event flags
        while (r.pos() + 4 <= tag.offset + tag.length) {
            uint32_t events = r.u32();
            if (events == 0) break;
            uint32_t size = r.u32();
            size_t start = r.pos();
            if (events & 0x00020000) start += 1;  // key code
            Code code{movie.data.data() + start, size - (start - r.pos())};
            if (events & 0x01) mc->runner.load_code = code;
            if (events & 0x02) mc->runner.enter_code = code;
            r.seek(r.pos() + size);
        }
    }
}

void MovieClip::execute_frame_tags(int mode, int frame_number) {
    if (!timeline || frame_number < 1 || size_t(frame_number) > timeline->frames.size()) return;
    for (const swf::Tag& tag : timeline->frames[size_t(frame_number - 1)].tags) {
        bool placement = tag.code == kPlaceObject2 || tag.code == kRemoveObject || tag.code == kRemoveObject2;
        if ((mode == 1 && !placement) || (mode == 2 && placement)) continue;
        swf::Reader r(player->movie().data.data(), tag.offset + tag.length, tag.offset);
        switch (tag.code) {
        case kPlaceObject2: place_object(tag, mode == 1 ? frame_number : 0); break;
        case kRemoveObject:
            r.u16();
            remove_depth(int(r.u16()) - 0x4000);
            break;
        case kRemoveObject2: remove_depth(int(r.u16()) - 0x4000); break;
        case kDoAction: {
            // Registered for the frame once; run by run_frame.
            auto& blocks = runner.frame_actions[frame_number];
            const uint8_t* code = player->movie().data.data() + tag.offset;
            bool known = std::any_of(blocks.begin(), blocks.end(), [&](const Code& c) { return c.data == code; });
            if (!known) blocks.push_back({code, tag.length});
            break;
        }
        default: break;  // sounds, init actions: ignored by castle.exe
        }
    }
}

void MovieClip::advance_frame() {
    if (!(flags & kActive)) return;
    bool looped = false;
    if (flags & kPlaying) {
        uint16_t old_action = action_frame;
        frame++;
        if (frame > total_frames) {
            frame = 1;
            looped = true;
            for (Character* c = first_child; c;) {
                Character* next_child = c->next;
                if (c->birth_frame != 1) remove_child(c);
                else c->touched_frame = old_action;
                c = next_child;
            }
        }
        action_frame = frame;
        execute_frame_tags(1, frame);
    }
    for (Character* c = first_child; c;) {
        Character* next_child = c->next;
        if (looped && c->touched_frame != frame) {
            remove_child(c);
        } else {
            c->touched_frame = 0;
            if (MovieClip* mc = MovieClip::from(c); mc && (mc->flags5c & kSprite)) {
                mc->parent = this;
                mc->advance_frame();
            }
        }
        c = next_child;
    }
}

void MovieClip::run_frame(int frame_number) {
    if (!(flags & kActive)) return;
    flags5c |= kContent;
    if (frame_number != 0) {
        flags |= kGotoThisPass;
        action_frame = uint16_t(frame_number);
    }
    if (action_frame == 0) action_frame = 1;
    if (frame == 0) frame = 1;
    Interpreter& in = player->context().interp();
    in.push_target(this);
    if (!(flags & kLoadEventDone)) {
        in.run_clip_event(runner, true);
        flags |= kLoadEventDone;
    } else {
        in.run_clip_event(runner, false);
    }
    if (flags & kPlaying) {
        execute_frame_tags(2, action_frame);
        in.run_frame_actions(runner, action_frame);
    }
    if ((flags & kStopRequested) || total_frames <= 1) flags &= uint8_t(~kPlaying);
    // The next child is read after this one ran (0x4489a2), so children
    // inserted after it during the pass run too (the native menu's clips),
    // and a child removed during its own run ends the loop (Reset clears
    // its next).
    for (Character* c = first_child; c; c = c->next) {
        if (MovieClip* mc = MovieClip::from(c); mc && (mc->flags5c & kSprite)) {
            // The goto flag is checked live: once a script in this pass does a
            // goto, the remaining children run only if placed on its frame
            // (verified against the real game: the menu's fader).
            if (!(flags & kGotoThisPass) || mc->touched_frame == action_frame) mc->run_frame(0);
        }
    }
    flags &= uint8_t(~kGotoThisPass);
    in.pop_target();
}

void MovieClip::goto_frame(int target) {
    int cur = current_frame();
    if (target == cur || target < 1) return;
    frame_before_goto = frame;
    // While the display list is rebuilt, the clip's frame is its action frame:
    // the surviving children are marked with it, so a goto made by a frame
    // script still runs them in this pass (RunFrame compares the mark with
    // +0x94), while the children it places are marked with the target.
    frame = action_frame ? action_frame : 1;
    flags |= kGotoThisPass;
    player->context().goto_queue.push_back({this, serial, target});

    // The replay (the original's forward, the original's backward) first lists the
    // placements, then runs those no later RemoveObject dropped.
    struct Entry {
        const swf::Tag* tag;  // null: a surviving child
        bool dropped;
        uint16_t frame;
        int depth;
    };
    std::vector<Entry> entries;
    auto tag_depth = [&](const swf::Tag& tag) {
        swf::Reader r(player->movie().data.data(), tag.offset + tag.length, tag.offset);
        if (tag.code == kPlaceObject2) r.u8();
        else if (tag.code == kRemoveObject) r.u16();
        return int(r.u16()) - 0x4000;
    };
    auto drop = [&](int d) {
        for (Entry& e : entries)
            if (e.depth == d) e.dropped = true;
    };
    int frames = timeline ? int(timeline->frames.size()) : 0;
    size_t survivors = 0;
    if (frame_before_goto < target) {
        for (Character* c = first_child; c; c = c->next) {
            c->touched_frame = frame;
            entries.push_back({nullptr, false, 0, c->depth});
        }
        survivors = entries.size();
        for (int f = frame_before_goto + 1; f <= target && f <= frames; f++) {
            for (const swf::Tag& tag : timeline->frames[size_t(f - 1)].tags) {
                if (tag.code == kPlaceObject2) {
                    entries.push_back({&tag, false, uint16_t(f), tag_depth(tag)});
                } else if (tag.code == kRemoveObject || tag.code == kRemoveObject2) {
                    int d = tag_depth(tag);
                    remove_depth(d);
                    drop(d);
                }
            }
        }
    } else {
        for (Character* c = first_child; c;) {
            Character* next_child = c->next;
            if (c->birth_frame > target) {
                remove_child(c);
            } else {
                c->touched_frame = frame;
                entries.push_back({nullptr, false, c->birth_frame, c->depth});
            }
            c = next_child;
        }
        survivors = entries.size();
        for (int f = 1; f <= target && f <= frames; f++) {
            for (const swf::Tag& tag : timeline->frames[size_t(f - 1)].tags) {
                if (tag.code == kPlaceObject2) {
                    int d = tag_depth(tag);
                    bool survivor = false;
                    for (size_t i = 0; i < survivors; i++) {
                        if (entries[i].depth != d) continue;
                        survivor = true;
                        if (entries[i].frame <= f) update_placed(tag, f == entries[i].frame);
                    }
                    if (!survivor) entries.push_back({&tag, false, uint16_t(f), d});
                } else if (tag.code == kRemoveObject || tag.code == kRemoveObject2) {
                    int d = tag_depth(tag);
                    bool survivor = false;
                    for (size_t i = 0; i < survivors; i++) survivor |= entries[i].depth == d;
                    if (survivor) continue;
                    remove_depth(d);
                    drop(d);
                }
            }
        }
    }
    for (size_t i = survivors; i < entries.size(); i++) {
        if (entries[i].dropped) continue;
        frame = entries[i].frame;
        place_object(*entries[i].tag, 0);
    }
    frame = uint16_t(target);
    for (Character* c = first_child; c; c = c->next)
        if (c->touched_frame == 0) c->touched_frame = frame;
}

// A backward goto re-applies the replayed PlaceObject2 of a surviving child
// in place: its matrix unless a script moved it, its colour
// transform (dropped on the frame it was placed if the tag has none) and its
// name. The character id is not checked.
void MovieClip::update_placed(const swf::Tag& tag, bool birth) {
    swf::Movie& movie = player->movie();
    swf::Reader r(movie.data.data(), tag.offset + tag.length, tag.offset);
    uint8_t f = r.u8();
    int d = int(r.u16()) - 0x4000;
    if (f & 0x02) r.u16();
    Character* node = child_at_depth(d);
    if (!node) return;
    if (f & 0x04) {
        swf::Matrix m = r.matrix();
        if (!(node->flags5c & kScriptMoved)) node->set_matrix_from_swf(m);
    }
    if (f & 0x08) {
        int16_t cx_mul[4] = {255, 255, 255, 255}, cx_add[4] = {0, 0, 0, 0};
        r.align();
        bool has_add = r.ub(1), has_mul = r.ub(1);
        int n = int(r.ub(4));
        if (has_mul)
            for (auto& mv : cx_mul) mv = int16_t(std::max(0, r.sb(n)));
        if (has_add)
            for (auto& a : cx_add) a = int16_t(r.sb(n));
        r.align();
        if (!node->cxform) node->cxform = player->new_color();
        if (node->cxform) {
            for (int i = 0; i < 4; i++) {
                node->cxform->mul[i] = cx_mul[i];
                node->cxform->add[i] = cx_add[i];
            }
        }
    } else if (birth && node->cxform) {
        node->cxform->clip = nullptr;
        release(kColor, node->cxform);
        node->cxform = nullptr;
    }
    if (f & 0x10) r.u16();
    if (f & 0x20) node->name = names().intern(r.cstring());
}

// ---- Rendering

namespace {

void draw_node(render::Renderer& r, Character* c, const swf::Matrix& m, const swf::CXform& cx) {
    if (MovieClip* mc = MovieClip::from(c)) {
        mc->render(r, m, cx);
        return;
    }
    if (!c->definition) return;
    switch (c->definition->type) {
    case swf::CharacterType::Shape:
        r.draw_shape(static_cast<swf::ShapeCharacter*>(c->definition)->shape, m, cx);
        break;
    case swf::CharacterType::Bitmap:
        r.draw_bitmap(*static_cast<swf::BitmapCharacter*>(c->definition), m, cx);
        break;
    case swf::CharacterType::EditText:
        if (text::Font* font = text::game_font(); font && !c->text.empty()) {
            auto* field = static_cast<swf::EditTextCharacter*>(c->definition);
            r.draw_text(*font, text::layout(*font, *field, c->text), field->color, m, cx);
        }
        break;
    default:
        break;
    }
}

}  // namespace

void MovieClip::render(render::Renderer& r, const swf::Matrix& parent_matrix, const swf::CXform& parent_cxform) {
    if (!(flags & kActive) || !(flags & kVisible)) return;
    swf::Movie& movie = player->movie();
    bool mods = !movie.clipped.empty() || !movie.flashes.empty();
    using Stencil = render::Renderer::Stencil;
    bool marked = false;
    // Masks: a layer with a clip depth hides itself and shows the layers
    // above it, up to that depth, only where it covers.
    struct Mask {
        Character* node;
        swf::Matrix matrix;
        int clip_depth;
    };
    std::vector<Mask> masks;
    // setMask: everything in this clip drawn only where its mask child
    // covers, the mask itself only as the mask (as a clip-depth mask is).
    Character* script = nullptr;
    swf::Matrix script_matrix;
    if (script_mask) {
        for (Character* c = first_child; c; c = c->next)
            if (c == script_mask) script = c;
        if (script) {
            script_matrix = (script->flags5c & kHasMatrix) ? parent_matrix * script->to_swf() : parent_matrix;
            r.mask_begin();
            draw_node(r, script, script_matrix, swf::CXform{});
            r.mask_apply();
        }
    }
    auto close_mask = [&] {
        const Mask& m = masks.back();
        r.mask_end_begin();
        draw_node(r, m.node, m.matrix, swf::CXform{});
        r.mask_end();
        masks.pop_back();
    };
    for (Character* c = first_child; c; c = c->next) {
        while (!masks.empty() && c->depth > masks.back().clip_depth) close_mask();
        if (c == script) continue;
        if (c->clip_depth) {
            swf::Matrix m = (c->flags5c & kHasMatrix) ? parent_matrix * c->to_swf() : parent_matrix;
            r.mask_begin();
            draw_node(r, c, m, swf::CXform{});
            r.mask_apply();
            masks.push_back({c, m, c->clip_depth});
            continue;
        }
        swf::CXform cx = parent_cxform;
        if (c->cxform) {
            swf::CXform own;
            for (int i = 0; i < 4; i++) {
                own.mul[i] = float(c->cxform->mul[i]);
                own.add[i] = float(c->cxform->add[i]);
            }
            cx = parent_cxform * own;
        }
        auto placed = [&](const swf::Matrix& base) { return (c->flags5c & kHasMatrix) ? base * c->to_swf() : base; };
        if (mods) {
            // A mod's clipped characters draw only inside their mask's silhouette.
            auto clipped = movie.clipped.find(c->character_id);
            if (clipped != movie.clipped.end()) {
                r.set_stencil(Stencil::Test);
                swf::Matrix m = placed(parent_matrix * clipped->second.stretch);
                if (clipped->second.boxes && c->definition && c->definition->type == swf::CharacterType::Shape) {
                    auto* shape = static_cast<swf::ShapeCharacter*>(c->definition);
                    shape->shape.load();
                    swf::Rgba color = shape->shape.fills.size() > 1 ? shape->shape.fills[1].color : swf::Rgba{};
                    r.draw_rect(shape->bounds, color, m, cx);
                } else {
                    draw_node(r, c, m, cx);
                }
                r.set_stencil(Stencil::Off);
                continue;
            }
        }
        draw_node(r, c, placed(parent_matrix), cx);
        if (mods && movie.masks.count(c->character_id)) {
            r.set_stencil(Stencil::Write);
            draw_node(r, c, placed(parent_matrix), cx);
            r.set_stencil(Stencil::Off);
            marked = true;
        }
    }
    while (!masks.empty()) close_mask();
    if (mods) {
        // A mod's flash over this clip, on its current frame.
        for (const auto& flash : movie.flashes) {
            if (flash.timeline != timeline || frame < 1 || size_t(frame) > flash.frames.size()) continue;
            if (!flash.frames[size_t(frame - 1)]) continue;
            r.set_stencil(marked ? Stencil::Test : Stencil::Off);
            r.draw_bitmap(*flash.frames[size_t(frame - 1)], parent_matrix * flash.stretch, parent_cxform);
            r.set_stencil(Stencil::Off);
        }
    }
    if (script) {
        r.mask_end_begin();
        draw_node(r, script, script_matrix, swf::CXform{});
        r.mask_end();
    }
    if (marked) r.clear_stencil();
}

void MovieClip::render_child(render::Renderer& r, Character* c, const swf::Matrix& parent_matrix) {
    swf::CXform cx;
    if (c->cxform)
        for (int i = 0; i < 4; i++) {
            cx.mul[i] = float(c->cxform->mul[i]);
            cx.add[i] = float(c->cxform->add[i]);
        }
    draw_node(r, c, (c->flags5c & kHasMatrix) ? parent_matrix * c->to_swf() : parent_matrix, cx);
}

}  // namespace player
