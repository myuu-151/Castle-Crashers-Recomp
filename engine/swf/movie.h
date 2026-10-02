// A loaded SWF: header, character dictionary and timelines.
#pragma once

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "common/files.h"
#include "swf/shape.h"
#include "swf/types.h"

namespace swf {

// A control tag inside a frame, pointing into the movie's data.
struct Tag {
    uint16_t code;
    uint32_t offset;
    uint32_t length;
};

struct Frame {
    std::vector<Tag> tags;  // everything before the frame's ShowFrame
};

struct Timeline {
    std::vector<Frame> frames;
    std::map<std::string, int> labels;  // label -> frame index (0-based)
};

enum class CharacterType { Shape, Sprite, Bitmap, EditText, Font, Other };

struct Character {
    CharacterType type = CharacterType::Other;
    uint16_t id = 0;
    virtual ~Character() = default;
};

struct ShapeCharacter : Character {
    Rect bounds;
    Shape shape;
};

struct SpriteCharacter : Character {
    Timeline timeline;
};

// Tag 148: a pre-rendered RGBA bitmap placed like a shape over its bounds.
struct BitmapCharacter : Character {
    Rect bounds;
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;
    bool mipmaps = false;  // smooth downscaling, for large mod pictures only
    bool nearest = false;  // drawn with nearest-neighbour sampling (a mod's choice)
    uint32_t texture = 0;  // renderer handle, created on first use
};

struct EditTextCharacter : Character {
    Rect bounds;
    uint16_t flags = 0;
    uint16_t font_id = 0;
    uint16_t font_height = 0;
    Rgba color;
    uint8_t align = 0;
    std::string variable;
    std::string initial_text;
};

class Movie {
public:
    static std::unique_ptr<Movie> load(const std::string& path);
    ~Movie();

    // Called as a movie is destroyed, so a renderer can free what it made
    // for the movie's characters (their gpu_mesh and texture handles).
    static inline void (*on_destroy)(Movie&) = nullptr;
    // A bitmap's pixels (RGBA, rows top to bottom), still in the file's data
    // as it loads: a renderer that makes its texture from them now returns
    // true, and they aren't copied into the bitmap's `rgba` (a console
    // hasn't the memory for two copies of a sky).
    static inline bool (*take_pixels)(BitmapCharacter&, const uint8_t* rgba) = nullptr;
    // The same for a bitmap read apart whose pixels come as a stream
    // (files::read_holed on a GameCube): a renderer that makes its texture
    // from it a part at a time returns true; if not, they are read whole.
    static inline bool (*take_pixel_stream)(BitmapCharacter&, const files::HoleStream&) = nullptr;
    // A bitmap whose bottom row is all transparent under an all-opaque row
    // (the plain skies) draws that row as the one above: true if these two
    // rows (RGBA, `width` pixels) are so.
    static bool fills_bottom_row(const uint8_t* above, const uint8_t* last, int width);
    // The tiled skies (sky2, sky5, sky6: 512 x 512, almost all opaque) have
    // their left and right columns softened, more transparent than the next
    // column in on every row. Two side by side, the sky behind showed through
    // as a thin line where they meet. side_rows counts a row toward that
    // (call it on every row, top to bottom); feathered_sides says whether the
    // bitmap is such a one, and fill_sides draws a row's edge columns as the
    // columns next to them.
    struct SideCount {
        int rows = 0, soft = 0;
        long long opaque = 0, pixels = 0;
    };
    static void side_rows(SideCount& c, const uint8_t* row, int width);
    static bool feathered_sides(const SideCount& c, int width, int height);
    static void fill_sides(uint8_t* row, int width);

    std::string name;
    int version = 0;
    Rect stage;
    float frame_rate = 30;
    Rgba background;
    Timeline root;
    std::map<uint16_t, std::unique_ptr<Character>> characters;
    std::map<std::string, uint16_t> exports;  // linkage name -> character id
    std::vector<uint8_t> data;

    // Mod overrides (player/mods.h), empty unless a mod is loaded: characters
    // drawn only inside the silhouette of another character's instance in the
    // same clip, stretched by a matrix, and the characters used as such masks.
    // With `boxes`, a shape is drawn as its bounding box filled with its first
    // fill colour, so a sweep follows the mask's silhouette, not the shape's.
    struct Clipped {
        uint16_t mask_id;
        Matrix stretch;
        bool boxes = false;
    };
    std::map<uint16_t, Clipped> clipped;
    std::set<uint16_t> masks;
    // A white flash measured from the original shine shapes of a sprite, one
    // overlay per frame of it (null when there's no flash), drawn over the
    // mask character's instance, clipped to it, stretched like `Clipped`.
    struct Flash {
        const Timeline* timeline;
        uint16_t mask_id;
        Matrix stretch;
        std::vector<std::unique_ptr<BitmapCharacter>> frames;
    };
    std::vector<Flash> flashes;

    Character* character(uint16_t id) const {
        auto it = characters.find(id);
        return it == characters.end() ? nullptr : it->second.get();
    }

private:
    void read_tags(size_t pos, size_t end, Timeline& timeline, bool nested);
    void define(uint16_t code, Reader& r, size_t end);
    // The bitmaps' pixels, copied out, are dropped from `data`, and every
    // offset and pointer into it after them moves down.
    void drop_pixels();
    std::vector<std::pair<size_t, size_t>> pixels_;  // [start, end) in data, while loading
    // Read with its large bitmaps' pixels left out (files::holes): each
    // bitmap made as its pixels go by, then the tags they were in shortened.
    bool read_holed(const std::string& path, const std::vector<files::Hole>& holes);
    static bool take_hole(void* movie, size_t index, size_t at, const uint8_t* bytes, const files::HoleStream* stream);
    // Those bitmaps, by where their pixels were in `data`, while loading.
    std::vector<std::pair<size_t, std::unique_ptr<BitmapCharacter>>> holed_;
    std::vector<uint32_t> hole_sizes_;
};

}  // namespace swf
