#include "swf/movie.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <new>

#include "common/files.h"

namespace swf {

namespace {

enum TagCode : uint16_t {
    kEnd = 0,
    kShowFrame = 1,
    kDefineShape = 2,
    kSetBackgroundColor = 9,
    kDefineShape2 = 22,
    kDefineShape3 = 32,
    kDefineEditText = 37,
    kDefineSprite = 39,
    kFrameLabel = 43,
    kExportAssets = 56,
    kDefineShape4 = 83,
    kBehemothBitmap = 148,
};

}  // namespace

Movie::~Movie() {
    if (on_destroy) on_destroy(*this);
    if (Shape::release) {
        for (auto& [id, ch] : characters) {
            if (ch->type != CharacterType::Shape) continue;
            Shape& shape = static_cast<ShapeCharacter&>(*ch).shape;
            if (shape.stored) Shape::release(shape.stored);
            shape.stored = 0;
        }
    }
}

namespace {

// Where a SWF's top-level tags start: after the header's rect, rate and count.
size_t first_tag(const std::vector<uint8_t>& d) { return 8 + (5 + 4 * size_t(d[8] >> 3) + 7) / 8 + 4; }

// A bitmap whose bottom row is all transparent under an all-opaque row (the
// plain skies, sky1 and sky3, of the game's 79): the row above copied into
// it. Drawn filtered, that row let what's behind the sky show through as a
// dark line above the treetops; castle.exe shows none.
void fill_clear_bottom_row(uint8_t* rgba, int width, int height) {
    if (width <= 0 || height < 2) return;
    uint8_t* last = rgba + size_t(height - 1) * size_t(width) * 4;
    const uint8_t* above = last - size_t(width) * 4;
    if (Movie::fills_bottom_row(above, last, width)) std::memcpy(last, above, size_t(width) * 4);
}

// The tiled skies' softened side columns (Movie::feathered_sides): each
// row's edge columns drawn as the columns next to them.
void fill_feathered_sides(uint8_t* rgba, int width, int height) {
    Movie::SideCount c;
    const size_t row = size_t(width) * 4;
    for (int y = 0; y < height; y++) Movie::side_rows(c, rgba + size_t(y) * row, width);
    if (!Movie::feathered_sides(c, width, height)) return;
    for (int y = 0; y < height; y++) Movie::fill_sides(rgba + size_t(y) * row, width);
}

// CASTLE_HOLES (the PC, to try the console's way): every bitmap's pixels as
// holes, as tools/copy_data.py lists the large ones for the GameCube.
std::vector<files::Hole> find_holes(const std::string& path) {
    std::vector<files::Hole> holes;
    std::vector<uint8_t> d;
    if (!files::read(path, d) || d.size() < 21 || d[0] != 'F') return holes;
    for (size_t pos = first_tag(d); pos + 2 <= d.size();) {
        uint16_t code_len = files::le16(&d[pos]);
        size_t header = 2, length = code_len & 0x3F;
        if (length == 0x3F && pos + 6 <= d.size()) {
            length = files::le32(&d[pos + 2]);
            header = 6;
        }
        size_t body = pos + header;
        if (body + length > d.size()) break;
        if ((code_len >> 6) == kBehemothBitmap) {
            size_t pixl = body + 20;
            while (pixl + 64 <= body + length && std::memcmp(&d[pixl], "LXIP", 4) != 0) pixl++;
            if (pixl + 64 <= body + length) {
                uint32_t from = files::le32(&d[pixl + 36]), to = files::le32(&d[pixl + 48]);
                if (from < to && pixl + to <= body + length)
                    holes.push_back({uint32_t(pixl + from), to - from});
            }
        }
        if ((code_len >> 6) == kEnd) break;
        pos = body + length;
    }
    return holes;
}

}  // namespace

void Movie::side_rows(SideCount& c, const uint8_t* row, int width) {
    if (width < 3) return;
    c.rows++;
    if (row[3] < row[7] && row[size_t(width - 1) * 4 + 3] < row[size_t(width - 2) * 4 + 3]) c.soft++;
    for (int x = 0; x < width; x += 8, c.pixels++)
        if (row[size_t(x) * 4 + 3] == 255) c.opaque++;
}

bool Movie::feathered_sides(const SideCount& c, int width, int height) {
    // big, nearly all opaque, and softened on both sides on every row: the
    // skies, and none of the game's other 76 bitmaps (64 x 64 glows and the
    // like, soft by design, are a fifth transparent or more)
    return width >= 256 && height >= 256 && c.rows == height && c.soft == height && c.pixels > 0 &&
           c.opaque * 100 >= c.pixels * 95;
}

void Movie::fill_sides(uint8_t* row, int width) {
    if (width < 3) return;
    std::memcpy(row, row + 4, 4);
    std::memcpy(row + size_t(width - 1) * 4, row + size_t(width - 2) * 4, 4);
}

bool Movie::fills_bottom_row(const uint8_t* above, const uint8_t* last, int width) {
    for (int x = 0; x < width; x++)
        if (last[x * 4 + 3] != 0 || above[x * 4 + 3] != 255) return false;
    return width > 0;
}

bool Movie::take_hole(void* self, size_t index, size_t at, const uint8_t* bytes, const files::HoleStream* stream) {
    // The hole is a bitmap's pixels, and its "PIXL" header, which says where
    // they start and how many there are, is just before them: in `data`.
    Movie& m = *static_cast<Movie*>(self);
    const uint8_t* d = m.data.data();
    uint32_t size = m.hole_sizes_[index];
    for (size_t p = at >= 64 ? at - 64 : 0, lo = at > 4096 ? at - 4096 : 0;; p--) {
        if (p + 64 <= at && std::memcmp(d + p, "LXIP", 4) == 0) {
            uint32_t words[16];
            for (int i = 0; i < 16; i++) words[i] = files::le32(d + p + 4 * size_t(i));
            if (p + words[9] == at && words[12] - words[9] == size &&
                size_t(words[4]) * size_t(words[5]) * 4 == size) {
                auto bitmap = std::make_unique<BitmapCharacter>();
                bitmap->type = CharacterType::Bitmap;
                bitmap->width = int(words[4]);
                bitmap->height = int(words[5]);
                if (!bytes) {
                    // A stream: to the renderer a part at a time if it can
                    // (it applies fills_bottom_row itself), else read whole.
                    if (!stream) return false;
                    if (!take_pixel_stream || !take_pixel_stream(*bitmap, *stream)) {
                        bitmap->rgba.resize(size);
                        if (!stream->get(0, size, bitmap->rgba.data())) return false;
                        fill_clear_bottom_row(bitmap->rgba.data(), bitmap->width, bitmap->height);
                        fill_feathered_sides(bitmap->rgba.data(), bitmap->width, bitmap->height);
                    }
                    m.holed_.emplace_back(at, std::move(bitmap));
                    return true;
                }
                // (The hole's buffer is files::read_holed's own, freed after this.)
                fill_clear_bottom_row(const_cast<uint8_t*>(bytes), bitmap->width, bitmap->height);
                fill_feathered_sides(const_cast<uint8_t*>(bytes), bitmap->width, bitmap->height);
                if (!take_pixels || !take_pixels(*bitmap, bytes)) bitmap->rgba.assign(bytes, bytes + size);
                m.holed_.emplace_back(at, std::move(bitmap));
                return true;
            }
        }
        if (p == lo) return false;
    }
}

bool Movie::read_holed(const std::string& path, const std::vector<files::Hole>& holes) {
    hole_sizes_.clear();
    for (const files::Hole& h : holes) hole_sizes_.push_back(h.size);
    if (!files::read_holed(path, holes, data, take_hole, this) || data.size() < 21) return false;
    // Each hole shortened the top-level tag it was in: its length, patched
    // (a hole is inside one, never across a header).
    size_t next = 0;
    for (size_t pos = first_tag(data), file_pos = pos; pos + 2 <= data.size() && next < holes.size();) {
        uint16_t code_len = files::le16(&data[pos]);
        size_t header = 2;
        uint32_t length = code_len & 0x3F;
        if (length == 0x3F) {
            if (pos + 6 > data.size()) return false;
            length = files::le32(&data[pos + 2]);
            header = 6;
        }
        size_t body = file_pos + header;
        uint32_t cut = 0;
        while (next < holes.size() && holes[next].offset >= body && holes[next].offset + holes[next].size <= body + length)
            cut += holes[next++].size;
        if (next < holes.size() && holes[next].offset < body + length) return false;
        if (cut) {
            if (header != 6) return false;
            uint32_t shorter = length - cut;
            for (int i = 0; i < 4; i++) data[pos + 2 + size_t(i)] = uint8_t(shorter >> (8 * i));
        }
        if ((code_len >> 6) == kEnd) break;
        pos += header + length - cut;
        file_pos += header + length;
    }
    return next == holes.size();
}

std::unique_ptr<Movie> Movie::load(const std::string& path) {
    auto movie = std::make_unique<Movie>();
    std::vector<files::Hole> holes = files::holes(path);
    if (holes.empty() && std::getenv("CASTLE_HOLES")) holes = find_holes(path);
    bool read = holes.empty() ? files::read(path, movie->data) : movie->read_holed(path, holes);
    if (!read || movie->data.size() < 21) return nullptr;
    const auto& d = movie->data;
    // The files are uncompressed (FWS) once unwrapped from COK6.
    if (d[0] != 'F' || d[1] != 'W' || d[2] != 'S') return nullptr;

    movie->name = std::filesystem::path(path).stem().string();
    movie->version = d[3];
    Reader r(d.data(), d.size(), 8);
    movie->stage = r.rect();
    uint16_t rate = r.u16();
    movie->frame_rate = float(rate >> 8) + float(rate & 0xFF) / 256.0f;
    r.u16();  // frame count; the tags are authoritative
    movie->read_tags(r.pos(), d.size(), movie->root, false);
    movie->stash_records();
    movie->drop_pixels();
    movie->holed_.clear();
    movie->hole_sizes_.clear();
    return movie;
}

void Movie::stash_records() {
    if (!Shape::stash) return;
    for (auto& [id, ch] : characters) {
        if (ch->type != CharacterType::Shape) continue;
        Shape& shape = static_cast<ShapeCharacter&>(*ch).shape;
        if (!shape.record || shape.record_size == 0) continue;
        uint32_t handle = Shape::stash(shape.record, shape.record_size);
        if (!handle) continue;  // (no room: it stays)
        size_t at = size_t(shape.record - data.data());
        pixels_.push_back({at, at + shape.record_size});
        shape.record = nullptr;
        shape.stored = handle;
    }
}

void Movie::drop_pixels() {
    if (pixels_.empty()) return;
    std::sort(pixels_.begin(), pixels_.end());
    // Where an offset moves to: down by the pieces cut before it (a binary
    // search: with a movie's shape records cut too, there are thousands, and
    // as many tags).
    std::vector<size_t> ends, cut_before{0};
    for (auto& [start, end] : pixels_) {
        ends.push_back(end);
        cut_before.push_back(cut_before.back() + (end - start));
    }
    auto moved = [&](size_t at) {
        size_t before = size_t(std::upper_bound(ends.begin(), ends.end(), at) - ends.begin());
        return at - cut_before[before];
    };
    auto remap = [&](Timeline& timeline) {
        for (Frame& frame : timeline.frames)
            for (Tag& tag : frame.tags) tag.offset = uint32_t(moved(tag.offset));
    };
    remap(root);
    for (auto& [id, ch] : characters) {
        if (ch->type == CharacterType::Sprite) remap(static_cast<SpriteCharacter&>(*ch).timeline);
        if (ch->type == CharacterType::Shape) {
            Shape& shape = static_cast<ShapeCharacter&>(*ch).shape;
            if (shape.record) shape.record = data.data() + moved(size_t(shape.record - data.data()));
        }
    }
    // Shape records now point into the compacted layout. Compacted in place:
    // the kept bytes move down over the pixels. (A second buffer the file's
    // size, as this once made, was more than a console could have for the
    // biggest level, 1.42 MB: the level never loaded.)
    uint8_t* d = data.data();
    size_t to = 0, from = 0;
    for (auto& [start, end] : pixels_) {
        std::memmove(d + to, d + from, start - from);
        to += start - from;
        from = end;
    }
    std::memmove(d + to, d + from, data.size() - from);
    to += data.size() - from;
    data.resize(to);
    // The memory the pixels took, given back: a buffer the new size, if one
    // can be had (the records move with it).
    const uint8_t* before = data.data();
    try {
        data.shrink_to_fit();
    } catch (const std::bad_alloc&) {
    }
    if (data.data() != before) {
        for (auto& [id, ch] : characters) {
            if (ch->type != CharacterType::Shape) continue;
            Shape& shape = static_cast<ShapeCharacter&>(*ch).shape;
            if (shape.record) shape.record = data.data() + (shape.record - before);
        }
    }
    pixels_.clear();
    pixels_.shrink_to_fit();
}

void Movie::read_tags(size_t pos, size_t end, Timeline& timeline, bool nested) {
    Reader r(data.data(), end, pos);
    Frame frame;
    while (!r.done()) {
        uint16_t code_len = r.u16();
        uint16_t code = code_len >> 6;
        uint32_t length = code_len & 0x3F;
        if (length == 0x3F) length = r.u32();
        size_t body = r.pos();
        if (body + length > end) break;
        Reader tag(data.data(), body + length, body);

        switch (code) {
        case kEnd:
            if (!frame.tags.empty()) timeline.frames.push_back(frame);
            return;
        case kShowFrame:
            timeline.frames.push_back(frame);
            frame = {};
            break;
        case kFrameLabel:
            timeline.labels[tag.cstring()] = int(timeline.frames.size());
            break;
        case kSetBackgroundColor:
            if (!nested) background = tag.rgb();
            break;
        case kExportAssets: {
            int count = tag.u16();
            for (int i = 0; i < count; i++) {
                uint16_t id = tag.u16();
                exports[tag.cstring()] = id;
            }
            break;
        }
        case kDefineSprite: {
            auto sprite = std::make_unique<SpriteCharacter>();
            sprite->type = CharacterType::Sprite;
            sprite->id = tag.u16();
            tag.u16();  // frame count
            uint16_t id = sprite->id;
            characters[id] = std::move(sprite);
            read_tags(tag.pos(), body + length,
                      static_cast<SpriteCharacter*>(characters[id].get())->timeline, true);
            break;
        }
        case kDefineShape:
        case kDefineShape2:
        case kDefineShape3:
        case kDefineShape4:
        case kDefineEditText:
        case kBehemothBitmap:
            define(code, tag, body + length);
            break;
        default:
            // Control tags (PlaceObject, RemoveObject, DoAction, sounds...) run
            // when their frame is reached.
            frame.tags.push_back({code, uint32_t(body), length});
            break;
        }
        r.seek(body + length);
    }
    if (!frame.tags.empty()) timeline.frames.push_back(frame);
}

void Movie::define(uint16_t code, Reader& r, size_t end) {
    switch (code) {
    case kDefineShape:
    case kDefineShape2:
    case kDefineShape3:
    case kDefineShape4: {
        auto shape = std::make_unique<ShapeCharacter>();
        shape->type = CharacterType::Shape;
        shape->id = r.u16();
        shape->bounds = r.rect();
        int shape_version = code == kDefineShape ? 1 : code == kDefineShape2 ? 2 : code == kDefineShape3 ? 3 : 4;
        if (shape_version == 4) {
            r.rect();  // edge bounds
            r.u8();    // flags
        }
        shape->shape.record = r.data() + r.pos();
        shape->shape.record_size = r.size() - r.pos();  // to the tag's end
        shape->shape.version = shape_version;
        characters[shape->id] = std::move(shape);
        break;
    }
    case kBehemothBitmap: {
        // u32 id, bounds as four s32 twips (xmin, xmax, ymin, ymax), 0xCD
        // padding, then a "PIXL" texture header whose words 4/5 are the size
        // and 9/12 the start and end of the RGBA pixels, relative to it.
        auto bitmap = std::make_unique<BitmapCharacter>();
        bitmap->type = CharacterType::Bitmap;
        size_t start = r.pos();
        bitmap->id = uint16_t(r.u32());
        bitmap->bounds.xmin = r.s32();
        bitmap->bounds.xmax = r.s32();
        bitmap->bounds.ymin = r.s32();
        bitmap->bounds.ymax = r.s32();
        size_t pixl = start + 20;
        while (pixl + 64 <= end && std::memcmp(&data[pixl], "LXIP", 4) != 0) pixl++;
        if (pixl + 64 > end) break;
        Reader h(data.data(), end, pixl);
        uint32_t words[16];
        for (auto& w : words) w = h.u32();
        bitmap->width = int(words[4]);
        bitmap->height = int(words[5]);
        size_t px_start = pixl + words[9], px_end = pixl + words[12];
        // Its pixels left out of the read: the bitmap made then (read_holed).
        for (auto& [at, made] : holed_) {
            if (at != px_start || !made) continue;
            made->id = bitmap->id;
            made->bounds = bitmap->bounds;
            characters[made->id] = std::move(made);
            return;
        }
        if (px_end > end || px_end - px_start != size_t(bitmap->width) * size_t(bitmap->height) * 4) break;
        fill_clear_bottom_row(data.data() + px_start, bitmap->width, bitmap->height);
        fill_feathered_sides(data.data() + px_start, bitmap->width, bitmap->height);
        if (!take_pixels || !take_pixels(*bitmap, data.data() + px_start))
            bitmap->rgba.assign(data.begin() + ptrdiff_t(px_start), data.begin() + ptrdiff_t(px_end));
        pixels_.push_back({px_start, px_end});
        characters[bitmap->id] = std::move(bitmap);
        break;
    }
    case kDefineEditText: {
        auto text = std::make_unique<EditTextCharacter>();
        text->type = CharacterType::EditText;
        text->id = r.u16();
        text->bounds = r.rect();
        text->flags = r.u16();
        if (text->flags & 0x0001) {  // HasFont
            text->font_id = r.u16();
            text->font_height = r.u16();
        }
        if (text->flags & 0x0004) text->color = r.rgba();  // HasTextColor
        if (text->flags & 0x0002) r.u16();                  // HasMaxLength
        if (text->flags & 0x2000) {                         // HasLayout
            text->align = r.u8();
            r.u16(); r.u16(); r.u16(); r.u16();             // margins, indent, leading
        }
        text->variable = r.cstring();
        if (text->flags & 0x0080) text->initial_text = r.cstring();  // HasText
        characters[text->id] = std::move(text);
        break;
    }
    }
}

}  // namespace swf
