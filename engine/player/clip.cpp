#include "player/clip.h"

#include <algorithm>

#include "render/renderer.h"
#include "text/fonts.h"

namespace player {

namespace {

enum TagCode : uint16_t {
    kPlaceObject = 4,
    kRemoveObject = 5,
    kDoAction = 12,
    kPlaceObject2 = 26,
    kRemoveObject2 = 28,
    kPlaceObject3 = 70,
};

}  // namespace

Clip::Clip(swf::Movie* movie, const swf::Timeline* timeline, Clip* parent)
    : movie_(movie), timeline_(timeline), parent_(parent) {}

void Clip::tick(std::vector<QueuedAction>& actions) {
    int total = total_frames();
    if (playing_ && total > 0 && (frame_ < 0 || total > 1)) {
        int next = frame_ + 1;
        if (next >= total) goto_frame(0, actions);
        else run_frame(next, true, actions);
    }
    for (auto& [depth, obj] : children_) {
        if (obj.clip && !obj.placed_this_frame) obj.clip->tick(actions);
        obj.placed_this_frame = false;
    }
}

void Clip::goto_frame(int frame, std::vector<QueuedAction>& actions) {
    frame = std::clamp(frame, 0, std::max(0, total_frames() - 1));
    if (frame == frame_) return;
    if (frame > frame_) {
        for (int f = frame_ + 1; f <= frame; f++) run_frame(f, f == frame, actions);
        return;
    }
    // Backwards: rebuild the display list from the first frame.
    std::map<int, DisplayObject> previous = std::move(children_);
    children_.clear();
    reuse_ = &previous;
    frame_ = -1;
    for (int f = 0; f <= frame; f++) run_frame(f, f == frame, actions);
    reuse_ = nullptr;
}

void Clip::run_frame(int frame, bool run_actions, std::vector<QueuedAction>& actions) {
    frame_ = frame;
    if (frame < 0 || frame >= total_frames()) return;
    for (const swf::Tag& tag : timeline_->frames[size_t(frame)].tags) {
        swf::Reader r(movie_->data.data(), tag.offset + tag.length, tag.offset);
        switch (tag.code) {
        case kPlaceObject:
        case kPlaceObject2:
        case kPlaceObject3:
            place(tag, actions);
            break;
        case kRemoveObject:
            r.u16();
            remove(r.u16());
            break;
        case kRemoveObject2:
            remove(r.u16());
            break;
        case kDoAction:
            if (run_actions) actions.push_back({this, tag});
            break;
        }
    }
}

void Clip::place(const swf::Tag& tag, std::vector<QueuedAction>& actions) {
    swf::Reader r(movie_->data.data(), tag.offset + tag.length, tag.offset);
    uint8_t flags = 0, flags2 = 0;
    bool has_character = true, move = false;
    int depth = 0;
    uint16_t character_id = 0;

    if (tag.code == kPlaceObject) {
        character_id = r.u16();
        depth = r.u16();
        flags = 0x04;  // matrix follows; a colour transform may too
    } else {
        flags = r.u8();
        if (tag.code == kPlaceObject3) flags2 = r.u8();
        depth = r.u16();
        if (flags2 & 0x08) r.cstring();  // class name
        has_character = flags & 0x02;
        move = flags & 0x01;
        if (has_character) character_id = r.u16();
    }

    auto it = children_.find(depth);
    DisplayObject* obj = it == children_.end() ? nullptr : &it->second;
    bool fresh = !move || !obj;
    if (fresh && !has_character) return;  // modifying an empty depth

    DisplayObject created;
    if (fresh) obj = &created;

    if (flags & 0x04) obj->matrix = r.matrix();
    if (tag.code == kPlaceObject) {
        if (r.pos() < tag.offset + tag.length) obj->cxform = r.cxform(false);
    } else {
        if (flags & 0x08) obj->cxform = r.cxform(true);
        if (flags & 0x10) obj->ratio = r.u16();
        if (flags & 0x20) obj->name = r.cstring();
        if (flags & 0x40) obj->clip_depth = r.u16();
        // 0x80: onClipEvent handlers, run by the interpreter (not yet).
    }

    if (has_character && (fresh || obj->character_id != character_id)) {
        obj->character_id = character_id;
        obj->character = movie_->character(character_id);
        obj->clip.reset();
        if (obj->character && obj->character->type == swf::CharacterType::EditText)
            obj->text = static_cast<swf::EditTextCharacter*>(obj->character)->initial_text;
        if (obj->character && obj->character->type == swf::CharacterType::Sprite) {
            // Keep the instance across a backwards goto if nothing changed.
            if (reuse_) {
                auto old = reuse_->find(depth);
                if (old != reuse_->end() && old->second.character_id == character_id && old->second.clip) {
                    obj->clip = std::move(old->second.clip);
                    obj->placed_this_frame = true;
                }
            }
            if (!obj->clip) {
                auto* sprite = static_cast<swf::SpriteCharacter*>(obj->character);
                obj->clip = std::make_unique<Clip>(movie_, &sprite->timeline, this);
                obj->clip->run_frame(0, true, actions);
                obj->placed_this_frame = true;
            }
        }
    }
    if (fresh) children_[depth] = std::move(created);
}

bool Clip::bounds(const swf::Matrix& m, swf::Rect& out) const {
    bool any = false;
    auto grow = [&](const swf::Matrix& mm, const swf::Rect& r) {
        const float xs[2] = {float(r.xmin), float(r.xmax)}, ys[2] = {float(r.ymin), float(r.ymax)};
        for (float x : xs)
            for (float y : ys) {
                int32_t px = int32_t(mm.a * x + mm.c * y + mm.tx), py = int32_t(mm.b * x + mm.d * y + mm.ty);
                if (!any) out = {px, px, py, py};
                out.xmin = std::min(out.xmin, px); out.xmax = std::max(out.xmax, px);
                out.ymin = std::min(out.ymin, py); out.ymax = std::max(out.ymax, py);
                any = true;
            }
    };
    for (const auto& [depth, obj] : children_) {
        if (!obj.character || obj.clip_depth) continue;
        swf::Matrix mm = m * obj.matrix;
        switch (obj.character->type) {
        case swf::CharacterType::Shape: grow(mm, static_cast<swf::ShapeCharacter*>(obj.character)->bounds); break;
        case swf::CharacterType::Bitmap: grow(mm, static_cast<swf::BitmapCharacter*>(obj.character)->bounds); break;
        case swf::CharacterType::Sprite:
            if (obj.clip) {
                swf::Rect inner;
                if (obj.clip->bounds(mm, inner)) {
                    grow(swf::Matrix{}, inner);
                }
            }
            break;
        default: break;
        }
    }
    return any;
}

void Clip::render(render::Renderer& renderer, const swf::Matrix& parent_matrix,
                  const swf::CXform& parent_cxform) const {
    if (movie_->clipped.empty() && movie_->flashes.empty()) {
        for (const auto& [depth, obj] : children_) render_depth(depth, renderer, parent_matrix, parent_cxform);
        return;
    }
    // With a mod's clipping overrides: mask characters also mark the stencil,
    // and clipped characters draw only inside it.
    using Stencil = render::Renderer::Stencil;
    bool marked = false;
    for (const auto& [depth, obj] : children_) {
        auto clipped = movie_->clipped.find(obj.character_id);
        if (clipped != movie_->clipped.end()) {
            renderer.set_stencil(Stencil::Test);
            swf::Matrix stretched = parent_matrix * clipped->second.stretch;
            if (clipped->second.boxes && obj.character && obj.character->type == swf::CharacterType::Shape) {
                auto* shape = static_cast<swf::ShapeCharacter*>(obj.character);
                shape->shape.load();
                swf::Rgba color = shape->shape.fills.size() > 1 ? shape->shape.fills[1].color : swf::Rgba{};
                renderer.draw_rect(shape->bounds, color, stretched * obj.matrix, parent_cxform * obj.cxform);
            } else {
                render_depth(depth, renderer, stretched, parent_cxform);
            }
            renderer.set_stencil(Stencil::Off);
            continue;
        }
        render_depth(depth, renderer, parent_matrix, parent_cxform);
        if (movie_->masks.count(obj.character_id)) {
            renderer.set_stencil(Stencil::Write);
            render_depth(depth, renderer, parent_matrix, parent_cxform);
            renderer.set_stencil(Stencil::Off);
            marked = true;
        }
    }
    // A mod's flash over this clip, on its current frame.
    for (const auto& flash : movie_->flashes) {
        if (flash.timeline != timeline_ || frame_ < 0 || size_t(frame_) >= flash.frames.size()) continue;
        if (!flash.frames[size_t(frame_)]) continue;
        renderer.set_stencil(marked ? Stencil::Test : Stencil::Off);
        renderer.draw_bitmap(*flash.frames[size_t(frame_)], parent_matrix * flash.stretch, parent_cxform);
        renderer.set_stencil(Stencil::Off);
    }
    if (marked) renderer.clear_stencil();
}

void Clip::render_depth(int depth, render::Renderer& renderer, const swf::Matrix& parent_matrix,
                        const swf::CXform& parent_cxform) const {
    auto it = children_.find(depth);
    if (it == children_.end()) return;
    const DisplayObject& obj = it->second;
    if (!obj.character) return;
    if (obj.clip_depth) return;  // masks: not drawn yet (stencil to come)
    swf::Matrix m = parent_matrix * obj.matrix;
    swf::CXform c = parent_cxform * obj.cxform;
    switch (obj.character->type) {
    case swf::CharacterType::Shape:
        renderer.draw_shape(static_cast<swf::ShapeCharacter*>(obj.character)->shape, m, c);
        break;
    case swf::CharacterType::Bitmap:
        renderer.draw_bitmap(*static_cast<swf::BitmapCharacter*>(obj.character), m, c);
        break;
    case swf::CharacterType::Sprite:
        if (obj.clip) obj.clip->render(renderer, m, c);
        break;
    case swf::CharacterType::EditText: {
        auto* field = static_cast<swf::EditTextCharacter*>(obj.character);
        if (text::Font* font = text::game_font(); font && !obj.text.empty())
            renderer.draw_text(*font, text::layout(*font, *field, obj.text), field->color, m, c);
        break;
    }
    default:
        break;
    }
}

}  // namespace player
