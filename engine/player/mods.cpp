#include "player/mods.h"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push, 0)
#pragma warning(disable : 4505)
#endif
// Without thread-local storage (devkitPPC has none that works), stb_image
// reads its uninitialised "flip vertically" setting and turns pictures upside down.
#define STBI_NO_THREAD_LOCALS
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "common/files.h"
#include "player/clip.h"

namespace player {

namespace fs = std::filesystem;

namespace {

// What the original character covers, in twips.
bool original_bounds(swf::Movie& movie, swf::Character& ch, swf::Rect& out) {
    switch (ch.type) {
    case swf::CharacterType::Shape: out = static_cast<swf::ShapeCharacter&>(ch).bounds; return true;
    case swf::CharacterType::Bitmap: out = static_cast<swf::BitmapCharacter&>(ch).bounds; return true;
    case swf::CharacterType::Sprite: {
        Clip clip(&movie, &static_cast<swf::SpriteCharacter&>(ch).timeline, nullptr);
        std::vector<QueuedAction> actions;
        clip.tick(actions);
        return clip.bounds(swf::Matrix{}, out);
    }
    default: return false;
    }
}

struct Replaced {
    swf::Rect before, after;
};

bool replace(swf::Movie& movie, uint16_t id, const fs::path& file, float scale, bool nearest,
             std::map<uint16_t, Replaced>& done) {
    swf::Character* original = movie.character(id);
    swf::Rect area;
    if (!original || !original_bounds(movie, *original, area)) {
        SDL_Log("mod: %s has no character %d to replace", movie.name.c_str(), id);
        return false;
    }
    int w = 0, h = 0, channels = 0;
    std::vector<uint8_t> png;
    uint8_t* px = files::read(file.string(), png)
                      ? stbi_load_from_memory(png.data(), int(png.size()), &w, &h, &channels, 4)
                      : nullptr;
    if (!px) {
        SDL_Log("mod: can't read %s", file.string().c_str());
        return false;
    }

    // Fit the picture inside the original's bounds, keeping its shape.
    float aw = float(area.xmax - area.xmin), ah = float(area.ymax - area.ymin);
    float fit = std::min(aw / float(w), ah / float(h)) * scale;
    float cx = (area.xmin + area.xmax) / 2.0f, cy = (area.ymin + area.ymax) / 2.0f;
    auto bitmap = std::make_unique<swf::BitmapCharacter>();
    bitmap->type = swf::CharacterType::Bitmap;
    bitmap->id = id;
    bitmap->width = w;
    bitmap->height = h;
    bitmap->mipmaps = !nearest;
    bitmap->nearest = nearest;
    bitmap->bounds.xmin = int32_t(cx - w * fit / 2);
    bitmap->bounds.xmax = int32_t(cx + w * fit / 2);
    bitmap->bounds.ymin = int32_t(cy - h * fit / 2);
    bitmap->bounds.ymax = int32_t(cy + h * fit / 2);
    bitmap->rgba.assign(px, px + size_t(w) * size_t(h) * 4);
    stbi_image_free(px);
    done[id] = {area, bitmap->bounds};
    movie.characters[id] = std::move(bitmap);
    return true;
}

void hide(swf::Movie& movie, uint16_t id) {
    auto empty = std::make_unique<swf::Character>();
    empty->type = swf::CharacterType::Other;  // never drawn
    empty->id = id;
    movie.characters[id] = std::move(empty);
}

// "21-31" or "21" -> ids.
void parse_ids(const std::string& word, std::vector<uint16_t>& out) {
    auto dash = word.find('-');
    int first = std::stoi(word.substr(0, dash));
    int last = dash == std::string::npos ? first : std::stoi(word.substr(dash + 1));
    for (int id = first; id <= last; id++) out.push_back(uint16_t(id));
}

// The matrix mapping rectangle `from` onto rectangle `to`.
swf::Matrix stretch(const swf::Rect& from, const swf::Rect& to) {
    float sx = float(to.xmax - to.xmin) / float(std::max(1, from.xmax - from.xmin));
    float sy = float(to.ymax - to.ymin) / float(std::max(1, from.ymax - from.ymin));
    swf::Matrix m;
    m.a = sx;
    m.d = sy;
    m.tx = (to.xmin + to.xmax) / 2.0f - sx * (from.xmin + from.xmax) / 2.0f;
    m.ty = (to.ymin + to.ymax) / 2.0f - sy * (from.ymin + from.ymax) / 2.0f;
    return m;
}

// A grid of values over a rectangle of twips, `res` twips per cell.
struct Grid {
    swf::Rect box;
    float res = 10;
    int w = 0, h = 0;
    std::vector<float> v;

    Grid(const swf::Rect& area, float twips_per_cell) : box(area), res(twips_per_cell) {
        w = int(std::ceil((box.xmax - box.xmin) / res));
        h = int(std::ceil((box.ymax - box.ymin) / res));
        v.assign(size_t(w) * size_t(h), 0.0f);
    }
    float& at(int x, int y) { return v[size_t(y) * size_t(w) + size_t(x)]; }
};

// Rasterizes a placed shape into `out`: its fill alpha, or 1 where it covers
// anything if `coverage`.
void raster(swf::Shape& shape, const swf::Matrix& m, bool coverage, Grid& out) {
    shape.tessellate();
    const auto& vs = shape.mesh.vertices;
    const auto& idx = shape.mesh.indices;
    for (size_t t = 0; t + 2 < idx.size(); t += 3) {
        float px[3], py[3];
        for (int k = 0; k < 3; k++) {
            const swf::Vertex& v = vs[idx[t + size_t(k)]];
            px[k] = ((m.a * v.x + m.c * v.y + m.tx) - float(out.box.xmin)) / out.res;
            py[k] = ((m.b * v.x + m.d * v.y + m.ty) - float(out.box.ymin)) / out.res;
        }
        float value = coverage ? 1.0f : vs[idx[t]].color.a / 255.0f;
        int x0 = std::max(0, int(std::floor(std::min({px[0], px[1], px[2]}))));
        int x1 = std::min(out.w - 1, int(std::ceil(std::max({px[0], px[1], px[2]}))));
        int y0 = std::max(0, int(std::floor(std::min({py[0], py[1], py[2]}))));
        int y1 = std::min(out.h - 1, int(std::ceil(std::max({py[0], py[1], py[2]}))));
        float area = (px[1] - px[0]) * (py[2] - py[0]) - (py[1] - py[0]) * (px[2] - px[0]);
        if (area == 0) continue;
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                float cx = float(x) + 0.5f, cy = float(y) + 0.5f;
                float e0 = (px[1] - px[0]) * (cy - py[0]) - (py[1] - py[0]) * (cx - px[0]);
                float e1 = (px[2] - px[1]) * (cy - py[1]) - (py[2] - py[1]) * (cx - px[1]);
                float e2 = (px[0] - px[2]) * (cy - py[2]) - (py[0] - py[2]) * (cx - px[2]);
                bool inside = area > 0 ? (e0 >= 0 && e1 >= 0 && e2 >= 0) : (e0 <= 0 && e1 <= 0 && e2 <= 0);
                if (inside) out.at(x, y) = std::max(out.at(x, y), value);
            }
    }
}

// Widens bright areas by about r cells with a max filter whose footprint is
// an octagon (a square plus a diamond), so curves stay rounded, then softens
// the result with a box blur of radius b, twice.
void grow_and_soften(Grid& g, int r, int b) {
    std::vector<float> tmp(g.v.size());
    // dx, dy: the direction of a line of 2*radius+1 cells.
    auto pass = [&](int dx, int dy, int radius, bool max) {
        for (int y = 0; y < g.h; y++)
            for (int x = 0; x < g.w; x++) {
                float acc = 0;
                int n = 0;
                for (int k = -radius; k <= radius; k++) {
                    int sx = x + k * dx, sy = y + k * dy;
                    if (sx < 0 || sy < 0 || sx >= g.w || sy >= g.h) continue;
                    float s = g.at(sx, sy);
                    acc = max ? std::max(acc, s) : acc + s;
                    n++;
                }
                tmp[size_t(y) * size_t(g.w) + size_t(x)] = max ? acc : acc / float(std::max(1, n));
            }
        g.v.swap(tmp);
    };
    int square = int(std::lround(r * 0.4)), diamond = int(std::lround(r * 0.3));
    pass(1, 0, square, true);
    pass(0, 1, square, true);
    pass(1, 1, diamond, true);
    pass(1, -1, diamond, true);
    for (int i = 0; i < 2; i++) {
        pass(1, 0, b, false);
        pass(0, 1, b, false);
    }
}

// Measures the white flash that `shine` shapes make over a sprite, frame by
// frame: shapes composite over each other in depth order, and the `face`
// shape hides the flash beneath it, as in the original.
bool make_flash(swf::Movie& movie, uint16_t sprite_id, const std::set<uint16_t>& shine, uint16_t face_id,
                uint16_t mask_id, float grow_px) {
    swf::Character* ch = movie.character(sprite_id);
    if (!ch || ch->type != swf::CharacterType::Sprite) {
        SDL_Log("mod: %d is not a sprite", sprite_id);
        return false;
    }
    const swf::Timeline* timeline = &static_cast<swf::SpriteCharacter*>(ch)->timeline;
    swf::Rect box;
    if (!original_bounds(movie, *ch, box)) return false;
    // Room for the widening, and for the stretch onto the new picture, which
    // can squash the overlay.
    int margin = int(grow_px * 20) * 2 + 600;
    box = {box.xmin - margin, box.xmax + margin, box.ymin - margin, box.ymax + margin};
    const float res = 10;  // 2 cells per pixel of the original

    swf::Movie::Flash flash{timeline, mask_id, swf::Matrix{}, {}};
    Clip stepper(&movie, timeline, nullptr);
    std::vector<QueuedAction> actions;
    for (size_t f = 0; f < timeline->frames.size(); f++) {
        stepper.tick(actions);
        Grid white(box, res);
        bool any = false;
        for (const auto& [depth, obj] : stepper.children()) {
            if (!obj.character || obj.character->type != swf::CharacterType::Shape) continue;
            auto& shape = static_cast<swf::ShapeCharacter*>(obj.character)->shape;
            bool is_shine = shine.count(obj.character_id) > 0;
            if (!is_shine && obj.character_id != face_id) continue;
            Grid layer(box, res);
            raster(shape, obj.matrix, !is_shine, layer);
            for (size_t i = 0; i < white.v.size(); i++) {
                float a = layer.v[i];
                white.v[i] = is_shine ? a + (1 - a) * white.v[i] : white.v[i] * (1 - a);
            }
            any = any || is_shine;
        }
        if (!any) {
            flash.frames.push_back(nullptr);
            continue;
        }
        grow_and_soften(white, int(grow_px * 20 / res), 2);
        auto bitmap = std::make_unique<swf::BitmapCharacter>();
        bitmap->type = swf::CharacterType::Bitmap;
        bitmap->width = white.w;
        bitmap->height = white.h;
        bitmap->bounds = box;
        bitmap->rgba.resize(white.v.size() * 4);
        for (size_t i = 0; i < white.v.size(); i++) {
            bitmap->rgba[i * 4 + 0] = bitmap->rgba[i * 4 + 1] = bitmap->rgba[i * 4 + 2] = 255;
            bitmap->rgba[i * 4 + 3] = uint8_t(std::lround(std::clamp(white.v[i], 0.0f, 1.0f) * 255));
        }
        flash.frames.push_back(std::move(bitmap));
    }
    movie.flashes.push_back(std::move(flash));
    movie.masks.insert(mask_id);
    return true;
}

int run_script(swf::Movie& movie, const fs::path& dir, const std::vector<uint8_t>& script) {
    std::istringstream in(std::string(script.begin(), script.end()));
    std::map<uint16_t, Replaced> replaced;
    int changed = 0;
    std::string line;
    while (std::getline(in, line)) {
        line = line.substr(0, line.find('#'));
        std::istringstream words(line);
        std::string command;
        if (!(words >> command)) continue;
        try {
            if (command == "replace") {
                std::string id, file, key;
                float scale = 1;
                bool nearest = false;
                words >> id >> file;
                while (words >> key) {
                    if (key == "scale") words >> scale;
                    else if (key == "nearest") nearest = true;
                }
                if (replace(movie, uint16_t(std::stoi(id)), dir / file, scale, nearest, replaced)) changed++;
            } else if (command == "hide") {
                std::string word;
                std::vector<uint16_t> ids;
                while (words >> word) parse_ids(word, ids);
                for (uint16_t id : ids) hide(movie, id);
                changed += int(ids.size());
            } else if (command == "clip") {
                std::string word, mask, option;
                std::vector<uint16_t> ids;
                while (words >> word && word != "to") parse_ids(word, ids);
                words >> mask;
                bool boxes = (words >> option) && option == "boxes";
                uint16_t mask_id = uint16_t(std::stoi(mask));
                swf::Matrix m;
                auto r = replaced.find(mask_id);
                if (r != replaced.end()) m = stretch(r->second.before, r->second.after);
                for (uint16_t id : ids) movie.clipped[id] = {mask_id, m, boxes};
                movie.masks.insert(mask_id);
                changed += int(ids.size());
            } else if (command == "flash") {
                // flash SPRITE shine IDS face ID over ID [grow PX]
                std::string sprite, word, face, over, key;
                float grow = 0;
                std::set<uint16_t> shine;
                std::vector<uint16_t> ids;
                words >> sprite >> word;  // "shine"
                while (words >> word && word != "face") parse_ids(word, ids);
                shine.insert(ids.begin(), ids.end());
                words >> face >> word >> over;  // word is "over"
                if (words >> key && key == "grow") words >> grow;
                if (make_flash(movie, uint16_t(std::stoi(sprite)), shine, uint16_t(std::stoi(face)),
                               uint16_t(std::stoi(over)), grow))
                    changed++;
            } else {
                SDL_Log("mod: unknown command '%s'", command.c_str());
            }
        } catch (const std::exception&) {
            SDL_Log("mod: can't read line '%s'", line.c_str());
        }
    }
    // Flashes follow their mask character onto its replacement picture.
    for (auto& flash : movie.flashes) {
        auto r = replaced.find(flash.mask_id);
        if (r != replaced.end()) flash.stretch = stretch(r->second.before, r->second.after);
    }
    return changed;
}

}  // namespace

int apply_mod(swf::Movie& movie, const fs::path& mod_dir) {
    fs::path dir = mod_dir / movie.name;
    // Read through files:: (so a mod can be inside a disc image, with its
    // mod.txt); a folder of ID.png pictures only on a real file system.
    std::vector<uint8_t> script;
    if (files::read((dir / "mod.txt").string(), script)) return run_script(movie, dir, script);
    std::error_code error;  // (a platform without a file system throws otherwise)
    if (!fs::is_directory(dir, error)) return 0;

    std::map<uint16_t, Replaced> replaced;
    int changed = 0;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() != ".png") continue;
        try {
            if (replace(movie, uint16_t(std::stoi(entry.path().stem().string())), entry.path(), 1, false, replaced)) changed++;
        } catch (const std::exception&) {
        }
    }
    return changed;
}

}  // namespace player
