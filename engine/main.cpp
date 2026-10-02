// castle: plays Castle Crashers from the SWFs in assets/swf.
//
//   castle                              boot like the real game: logo, legal,
//                                       then whatever the scripts go to
//   castle MOVIE                        start straight into one movie
//   castle [MOVIE] --shot TICK OUT.png  run TICK game ticks, save the frame at
//                                       the stage's own size (848x480)
//   castle [MOVIE] --layers TICK DIR    same, each top-level layer separately
//                                       on a transparent background
//   castle MOVIE --export DIR [SCALE]   save every shape, bitmap and sprite
//                                       (first frame) as a cropped transparent
//                                       PNG, SCALE times its size (default 1)
//   castle [MOVIE] --dump TICK          run TICK ticks and print the display tree
//   castle [MOVIE] --capture TICKS OUT  run TICKS ticks, writing every movie
//                                       update's tree to OUT, in the format of
//                                       the castle.exe capture hook
//   --replay CAPTURE                    play a real-game capture's recorded input
// instead of the keyboard, or
//                                       a session of this game's: playing, it
//                                       records one to sessions/ next to the
//                                       executable (F3 there: state_dump.txt)
//   castle --jump SESSION TICKS         play a session to TICKS unseen, then go
//                                       on playing from there (F5 while playing:
//                                       the jump jump.txt names, "SESSION TICKS")
//   castle --verify SESSION             replay a recorded session and compare
//                                       every update with the real game's
//   castle --selftest CASES OUT         run tools/selftest.py's cases
//   --mod NAME                          replace graphics with mods/NAME (see
//                                       player/mods.h); off by default
//
// The capture modes run without opening a window.

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "common/crash.h"
#include "audio/audio.h"
#include "common/png.h"
#include "player/clip.h"
#include "player/dump.h"
#include "player/game.h"
#include "player/mods.h"
#include "render/offscreen.h"
#include "render/renderer.h"
#include "swf/movie.h"
#include "text/fonts.h"
#include "verify.h"

namespace fs = std::filesystem;

namespace {

// Finds a folder of the repository (such as assets/swf) by walking up from
// the executable's folder.
fs::path find_repo_dir(const fs::path& sub) {
    fs::path dir = SDL_GetBasePath() ? fs::path(SDL_GetBasePath()) : fs::current_path();
    for (int i = 0; i < 6 && !dir.empty(); i++, dir = dir.parent_path()) {
        if (fs::exists(dir / sub)) return dir / sub;
        if (dir == dir.parent_path()) break;
    }
    return sub;
}

fs::path movie_path(const fs::path& assets, const std::string& name) {
    for (const char* folder : {"game", "levels"}) {
        fs::path p = assets / folder / (name + ".swf");
        if (fs::exists(p)) return p;
    }
    return assets / "game" / (name + ".swf");
}

// The stage every movie shares: 848x480.
const swf::Rect kStage{0, 16960, 0, 9600};

// Renders the game (or one top-level layer of the current movie) offscreen.
bool save_stage(render::Renderer& renderer, player::Game& game, const std::string& out,
                player::Character* only = nullptr) {
    int w = kStage.xmax / 20, h = kStage.ymax / 20;
    render::Offscreen target;
    if (!target.begin(w, h)) return false;
    renderer.begin_frame(w, h, kStage, swf::Rgba{}, only != nullptr);
    if (only) {
        player::MovieClip* root = game.current()->root();
        root->render_child(renderer, only, root->to_swf());
    } else {
        game.render(renderer);
    }
    std::vector<uint8_t> pixels = target.finish();
    return png::write_rgba(out, w, h, pixels.data());
}

// Crops transparent borders (keeping a 1 px margin) and saves.
bool save_cropped(const std::string& out, int w, int h, const std::vector<uint8_t>& px) {
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (px[(size_t(y) * w + x) * 4 + 3]) {
                x0 = std::min(x0, x); x1 = std::max(x1, x);
                y0 = std::min(y0, y); y1 = std::max(y1, y);
            }
    if (x1 < 0) return false;  // nothing drawn
    x0 = std::max(0, x0 - 1); y0 = std::max(0, y0 - 1);
    x1 = std::min(w - 1, x1 + 1); y1 = std::min(h - 1, y1 + 1);
    int cw = x1 - x0 + 1, ch = y1 - y0 + 1;
    std::vector<uint8_t> crop(size_t(cw) * ch * 4);
    for (int y = 0; y < ch; y++)
        std::copy_n(&px[(size_t(y + y0) * w + x0) * 4], size_t(cw) * 4, &crop[size_t(y) * cw * 4]);
    return png::write_rgba(out, cw, ch, crop.data());
}

// Saves one character over its bounds (a sprite's first frame, over the
// bounds of what that frame draws).
bool export_character(render::Renderer& renderer, swf::Movie& movie, swf::Character& ch, float scale,
                      const std::string& out) {
    swf::Rect area;
    std::unique_ptr<player::Clip> clip;
    switch (ch.type) {
    case swf::CharacterType::Shape: area = static_cast<swf::ShapeCharacter&>(ch).bounds; break;
    case swf::CharacterType::Bitmap: area = static_cast<swf::BitmapCharacter&>(ch).bounds; break;
    case swf::CharacterType::Sprite: {
        clip = std::make_unique<player::Clip>(&movie, &static_cast<swf::SpriteCharacter&>(ch).timeline, nullptr);
        std::vector<player::QueuedAction> actions;
        clip->tick(actions);
        if (!clip->bounds(swf::Matrix{}, area)) return false;
        break;
    }
    default: return false;
    }
    const int pad = 2;
    int w = int(std::ceil((area.xmax - area.xmin) / 20.0f * scale)) + pad * 2;
    int h = int(std::ceil((area.ymax - area.ymin) / 20.0f * scale)) + pad * 2;
    if (w > 4096 || h > 4096 || w <= pad * 2 || h <= pad * 2) return false;
    swf::Rect view;  // the twips shown, so that one pixel is 20/scale twips
    view.xmin = area.xmin - int(pad * 20 / scale);
    view.ymin = area.ymin - int(pad * 20 / scale);
    view.xmax = view.xmin + int(std::lround(w * 20 / scale));
    view.ymax = view.ymin + int(std::lround(h * 20 / scale));

    render::Offscreen target;
    if (!target.begin(w, h)) return false;
    renderer.begin_frame(w, h, view, movie.background, true);
    swf::Matrix identity;
    swf::CXform none;
    if (ch.type == swf::CharacterType::Shape) {
        renderer.draw_shape(static_cast<swf::ShapeCharacter&>(ch).shape, identity, none);
    } else if (ch.type == swf::CharacterType::Bitmap) {
        renderer.draw_bitmap(static_cast<swf::BitmapCharacter&>(ch), identity, none);
    } else {
        clip->render(renderer, identity, none);
    }
    return save_cropped(out, w, h, target.finish());
}

const char* type_name(swf::CharacterType t) {
    switch (t) {
    case swf::CharacterType::Shape: return "shape";
    case swf::CharacterType::Bitmap: return "bitmap";
    case swf::CharacterType::Sprite: return "sprite";
    default: return "other";
    }
}

// Keyboard replay from a capture's "!! keys VK..." lines (the key state the
// real game sampled). Each change is tied to the last update before it of a
// movie other than the loading screen and pause movies, identified by its
// frame count, frame and how many times that (count, frame) was seen; once
// the engine's run makes the same update, the keys change.
struct KeyReplay {
    struct Change {
        int total = 0, frame = 0, occurrence = 0;  // total 0: before any update
        int pad = -1;  // a controller's reading ("!! pad"), or the keys
        int focus = -1;  // the window's focus ("!! focus"), when not -1
        int suspend = -1;  // the game suspended ("!! suspend"), when not -1
        int focus_suspend = -1;  // older captures: suspended while unfocused
        int mouse = -1;    // a mouse message ("!! mouse"), when not -1
        float mouse_x = 0, mouse_y = 0;
        int64_t dt_bits = -1;  // the tick length ("!! dt", float bits), when not -1
        std::array<uint8_t, 256> keys{};
        input::PadReading reading;
    };
    std::vector<Change> changes;
    size_t next = 0;
    bool has_suspend = false;  // the capture records "!! suspend"
    std::map<std::pair<int, int>, int> seen;

    void apply(player::Game& game, int total, int frame, int occurrence) {
        while (next < changes.size()) {
            const Change& c = changes[next];
            if (c.total != 0 && !(c.total == total && c.frame == frame && c.occurrence == occurrence)) break;
            if (c.dt_bits >= 0) {
                uint32_t bits = uint32_t(c.dt_bits);
                std::memcpy(&game.dt, &bits, 4);
            } else if (c.mouse >= 0) {
                game.mouse_event(c.mouse, c.mouse_x, c.mouse_y);
            } else if (c.suspend >= 0) {
                game.suspend_request = c.suspend;
            } else if (c.focus_suspend >= 0) {
                // Captures from before "!! suspend" was recorded: the game
                // was suspended while it didn't have the focus.
                if (!has_suspend) game.suspend_request = c.focus_suspend;
            } else if (c.focus >= 0) {
                game.window_active = c.focus != 0;
            }
            else if (c.pad >= 0) game.input.pads[c.pad] = c.reading;
            else game.input.keyboard.keys = c.keys;
            next++;
        }
    }
    unsigned updates = 0;  // every movie's, as a session of this game's counts them
    void on_update(player::Game& game, player::Player& p) {
        updates++;
        if (p.name() == "loading" || p.name() == "pause") return;
        int total = p.root()->total_frames, frame = p.root()->frame;
        apply(game, total, frame, ++seen[{total, frame}]);
    }
};
KeyReplay g_key_replay;

// Playing (not replaying): the session recorded in the capture's format, to
// sessions/session-DATE-TIME.txt next to the executable, so `castle --replay
// FILE` plays it again exactly (a bug seen while playing, reproduced): the
// save it began from, every update, and each change of input after the
// update it follows.
struct SessionRecorder {
    FILE* out = nullptr;
    unsigned updates = 0;
    std::array<uint8_t, 256> keys{};
    input::PadReading pads[4];
    bool pad_known[4] = {};
    int focus = -1, suspended = -1;

    void open(const fs::path& dir, const player::Game& game) {
        std::error_code ec;
        fs::create_directories(dir, ec);
        std::time_t t = std::time(nullptr);
        char name[64];
        std::strftime(name, sizeof(name), "session-%Y%m%d-%H%M%S.txt", std::localtime(&t));
        out = std::fopen((dir / name).string().c_str(), "w");
        if (!out) return;
        std::fprintf(out, "!! capture session (the reimplementation)\n!! format 2\n!! storage bytes");
        for (uint8_t b : game.storage.bytes()) std::fprintf(out, " %02x", b);
        std::fprintf(out, "\n!! gamertag %s\n", game.gamer_tag.c_str());
        SDL_Log("recording this session to %s", (dir / name).string().c_str());
    }
    // After a jump: the session jumped through, up to the update it reached,
    // then this one's own, so the file replays straight through to here.
    void open_after_jump(const fs::path& dir, const std::string& from, unsigned reached, const player::Game& game) {
        FILE* in = std::fopen(from.c_str(), "r");
        if (!in) return;
        open(dir, game);
        if (!out) {
            std::fclose(in);
            return;
        }
        std::fseek(out, 0, SEEK_SET);  // the jump's own header instead
        char line[8192];
        while (std::fgets(line, sizeof(line), in)) {
            unsigned n = 0;
            if (const char* u = std::strstr(line, " update "); u && line[0] == '=' && std::sscanf(u, " update %u", &n) == 1 &&
                                                               n > reached)
                break;
            std::fputs(line, out);
        }
        std::fclose(in);
        updates = reached;
        keys = game.input.keyboard.keys;
        for (int i = 0; i < 4; i++) {
            pads[i] = game.input.pads[i];
            pad_known[i] = true;
        }
        std::fflush(out);
    }
    void update(player::Player& p) {
        if (!out) return;
        std::fprintf(out, "== %s update %u frame %d/%d\n", p.name().c_str(), ++updates, p.root()->frame,
                     p.root()->total_frames);
    }
    // Before a tick: what changed since the last one.
    void input(const player::Game& game) {
        if (!out) return;
        if (game.input.keyboard.keys != keys) {
            keys = game.input.keyboard.keys;
            std::fprintf(out, "!! keys");
            for (int vk = 0; vk < 256; vk++)
                if (keys[size_t(vk)]) std::fprintf(out, " %d", vk);
            std::fprintf(out, "\n");
        }
        for (int i = 0; i < 4; i++) {
            const input::PadReading& r = game.input.pads[i];
            const input::PadReading& l = pads[i];
            if (pad_known[i] && r.connected == l.connected && r.buttons == l.buttons &&
                r.left_trigger == l.left_trigger && r.right_trigger == l.right_trigger && r.thumb_lx == l.thumb_lx &&
                r.thumb_ly == l.thumb_ly)
                continue;
            pads[i] = r;
            pad_known[i] = true;
            std::fprintf(out, "!! pad %d %d %u %u %u %d %d\n", i, r.connected ? 1 : 0, unsigned(r.buttons),
                         unsigned(r.left_trigger), unsigned(r.right_trigger), int(r.thumb_lx), int(r.thumb_ly));
        }
        if (int(game.window_active) != focus) {
            focus = game.window_active;
            std::fprintf(out, "!! focus %d\n", focus);
        }
        if (int(game.suspended) != suspended) {
            suspended = game.suspended;
            std::fprintf(out, "!! suspend %d\n", suspended);
        }
        std::fflush(out);
    }
    void mouse(int msg, float x, float y) {
        if (!out) return;
        uint32_t xb, yb;
        std::memcpy(&xb, &x, 4);
        std::memcpy(&yb, &y, 4);
        std::fprintf(out, "!! mouse %x %x %x\n", unsigned(msg), unsigned(xb), unsigned(yb));
    }
    ~SessionRecorder() {
        if (out) std::fclose(out);
    }
};

// Reads a capture's input: "!! keys" lines if it has them (replayed through
// the input system), else the "!! key CODE RESULT" answers (Key.isDown is
// answered from them directly). Plain text only: decompress .gz first.
bool load_replay(player::Game& game, const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return false;
    char line[16384];  // "!! storage bytes" lines are ~4.7 KB
    std::deque<std::pair<int, bool>> answers;
    std::map<std::pair<int, int>, int> seen;
    KeyReplay::Change last;
    // Format 1 captures wrote focus and suspend changes after the update
    // that first saw them: those belong before it, after the one before.
    KeyReplay::Change before_last;
    int format = 1;
    while (std::fgets(line, sizeof(line), f)) {
        int code = 0, result = 0;
        char movie[64];
        unsigned update = 0;
        int frame = 0, total = 0;
        if (std::sscanf(line, "== %63s update %u frame %d/%d", movie, &update, &frame, &total) == 4) {
            std::string m = movie;
            if (m == "loading" || m == "pause") continue;
            before_last = last;
            last.total = total;
            last.frame = frame;
            last.occurrence = ++seen[{total, frame}];
        } else if (std::strncmp(line, "!! keys", 7) == 0) {
            KeyReplay::Change c = last;
            c.keys.fill(0);
            for (char* p = line + 7;;) {
                char* end = nullptr;
                long vk = std::strtol(p, &end, 10);
                if (end == p) break;
                if (vk >= 0 && vk < 256) c.keys[size_t(vk)] = 1;
                p = end;
            }
            g_key_replay.changes.push_back(c);
        } else if (std::strncmp(line, "!! pad", 6) == 0) {
            KeyReplay::Change c = last;
            int index = 0, connected = 0, lx = 0, ly = 0;
            unsigned buttons = 0, lt = 0, rt = 0;
            if (std::sscanf(line, "!! pad %d %d %u %u %u %d %d", &index, &connected, &buttons, &lt, &rt, &lx, &ly) == 7 &&
                index >= 0 && index < 4) {
                c.pad = index;
                c.reading.connected = connected != 0;
                c.reading.buttons = uint16_t(buttons);
                c.reading.left_trigger = uint8_t(lt);
                c.reading.right_trigger = uint8_t(rt);
                c.reading.thumb_lx = int16_t(lx);
                c.reading.thumb_ly = int16_t(ly);
                g_key_replay.changes.push_back(c);
            }
        } else if (std::strncmp(line, "!! dt ", 6) == 0) {
            KeyReplay::Change c = last;
            c.dt_bits = int64_t(std::strtoul(line + 6, nullptr, 16));
            g_key_replay.changes.push_back(c);
        } else if (std::strncmp(line, "!! mouse ", 9) == 0) {
            KeyReplay::Change c = last;
            unsigned msg = 0, xb = 0, yb = 0;
            if (std::sscanf(line + 9, "%x %x %x", &msg, &xb, &yb) == 3) {
                c.mouse = int(msg);
                std::memcpy(&c.mouse_x, &xb, 4);
                std::memcpy(&c.mouse_y, &yb, 4);
                g_key_replay.changes.push_back(c);
            }
        } else if (std::strncmp(line, "!! format ", 10) == 0) {
            format = std::atoi(line + 10);
        } else if (std::strncmp(line, "!! suspend ", 11) == 0) {
            KeyReplay::Change c = format >= 2 ? last : before_last;
            c.suspend = std::atoi(line + 11) != 0 ? 1 : 0;
            g_key_replay.changes.push_back(c);
            g_key_replay.has_suspend = true;
        } else if (std::strncmp(line, "!! focus ", 9) == 0) {
            KeyReplay::Change c = format >= 2 ? last : before_last;
            c.focus = std::atoi(line + 9) != 0 ? 1 : 0;
            g_key_replay.changes.push_back(c);
            // The suspension it stands for when none is recorded: seen with
            // the update the line follows (the real flag changes later than
            // the focus).
            KeyReplay::Change s = last;
            s.focus_suspend = c.focus == 0 ? 1 : 0;
            g_key_replay.changes.push_back(s);
        } else if (std::strncmp(line, "!! storage bytes", 16) == 0 && game.captured_storage.empty()) {
            for (char* p = line + 16;;) {
                char* end = nullptr;
                long v = std::strtol(p, &end, 16);
                if (end == p) break;
                game.captured_storage.push_back(uint8_t(v));
                p = end;
            }
            // The DLC packs owned when the capture was made.
            if (game.captured_storage.size() > 0x5b0)
                game.dlc = (game.captured_storage[0x580] & 0x80 ? 1u : 0u) | (game.captured_storage[0x5b0] & 0x80 ? 2u : 0u);
        } else if (std::strncmp(line, "!! gamertag ", 12) == 0) {
            std::string tag = line + 12;
            while (!tag.empty() && (tag.back() == '\n' || tag.back() == '\r')) tag.pop_back();
            game.gamer_tag = tag;
        } else if (std::sscanf(line, "!! key %d %d", &code, &result) == 2) {
            answers.emplace_back(code, result != 0);
        }
    }
    std::fclose(f);
    // A capture that records its suspension needs no stand-in for it.
    if (g_key_replay.has_suspend) {
        auto& ch = g_key_replay.changes;
        ch.erase(std::remove_if(ch.begin(), ch.end(), [](const KeyReplay::Change& c) { return c.focus_suspend >= 0; }),
                 ch.end());
    }
    if (!g_key_replay.changes.empty()) {
        g_key_replay.apply(game, 0, 0, 0);
        SDL_Log("replaying %zu key changes from %s", g_key_replay.changes.size(), path.c_str());
    } else {
        game.replay = std::move(answers);
        game.replaying = true;
        SDL_Log("replaying %zu Key.isDown answers from %s", game.replay.size(), path.c_str());
    }
    return true;
}

std::string window_title(player::Game& game) {
    player::Player* p = game.current();
    if (!p) return "Castle Crashers";
    return "Castle Crashers - " + p->name() + " frame " + std::to_string(p->root()->current_frame()) + "/" +
           std::to_string(p->root()->total_frames);
}

// The Windows virtual-key code castle.exe sees for a key.
int virtual_key(SDL_Scancode s) {
    if (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z) return 'A' + (s - SDL_SCANCODE_A);
    if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_9) return '1' + (s - SDL_SCANCODE_1);
    if (s >= SDL_SCANCODE_F1 && s <= SDL_SCANCODE_F12) return 0x70 + (s - SDL_SCANCODE_F1);
    if (s >= SDL_SCANCODE_KP_1 && s <= SDL_SCANCODE_KP_9) return 0x61 + (s - SDL_SCANCODE_KP_1);
    switch (s) {
    case SDL_SCANCODE_0: return '0';
    case SDL_SCANCODE_KP_0: return 0x60;
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: return 0x0d;
    case SDL_SCANCODE_ESCAPE: return 0x1b;
    case SDL_SCANCODE_BACKSPACE: return 0x08;
    case SDL_SCANCODE_TAB: return 0x09;
    case SDL_SCANCODE_SPACE: return 0x20;
    case SDL_SCANCODE_PAGEUP: return 0x21;
    case SDL_SCANCODE_PAGEDOWN: return 0x22;
    case SDL_SCANCODE_END: return 0x23;
    case SDL_SCANCODE_HOME: return 0x24;
    case SDL_SCANCODE_LEFT: return 0x25;
    case SDL_SCANCODE_UP: return 0x26;
    case SDL_SCANCODE_RIGHT: return 0x27;
    case SDL_SCANCODE_DOWN: return 0x28;
    case SDL_SCANCODE_INSERT: return 0x2d;
    case SDL_SCANCODE_DELETE: return 0x2e;
    case SDL_SCANCODE_LSHIFT: case SDL_SCANCODE_RSHIFT: return 0x10;
    case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: return 0x11;
    case SDL_SCANCODE_LALT: case SDL_SCANCODE_RALT: return 0x12;
    case SDL_SCANCODE_SEMICOLON: return 0xba;
    case SDL_SCANCODE_EQUALS: return 0xbb;
    case SDL_SCANCODE_COMMA: return 0xbc;
    case SDL_SCANCODE_MINUS: return 0xbd;
    case SDL_SCANCODE_PERIOD: return 0xbe;
    case SDL_SCANCODE_SLASH: return 0xbf;
    case SDL_SCANCODE_GRAVE: return 0xc0;
    case SDL_SCANCODE_LEFTBRACKET: return 0xdb;
    case SDL_SCANCODE_BACKSLASH: return 0xdc;
    case SDL_SCANCODE_RIGHTBRACKET: return 0xdd;
    case SDL_SCANCODE_APOSTROPHE: return 0xde;
    default: return 0;
    }
}

// Open gamepads, in order, as XInput pads 0-3.
void read_gamepads(input::Input& in) {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    for (int i = 0; i < 4; i++) {
        input::PadReading r;
        SDL_Gamepad* g = ids && i < count ? SDL_GetGamepadFromID(ids[i]) : nullptr;
        if (g) {
            r.connected = true;
            struct { SDL_GamepadButton b; uint16_t x; } map[] = {
                {SDL_GAMEPAD_BUTTON_DPAD_UP, 0x1}, {SDL_GAMEPAD_BUTTON_DPAD_DOWN, 0x2},
                {SDL_GAMEPAD_BUTTON_DPAD_LEFT, 0x4}, {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, 0x8},
                {SDL_GAMEPAD_BUTTON_START, 0x10}, {SDL_GAMEPAD_BUTTON_BACK, 0x20},
                {SDL_GAMEPAD_BUTTON_LEFT_STICK, 0x40}, {SDL_GAMEPAD_BUTTON_RIGHT_STICK, 0x80},
                {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, 0x100}, {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, 0x200},
                {SDL_GAMEPAD_BUTTON_SOUTH, 0x1000}, {SDL_GAMEPAD_BUTTON_EAST, 0x2000},
                {SDL_GAMEPAD_BUTTON_WEST, 0x4000}, {SDL_GAMEPAD_BUTTON_NORTH, 0x8000},
            };
            for (auto& m : map) {
                if (!SDL_GetGamepadButton(g, m.b)) continue;
                // Face buttons by the letter printed on them where there is
                // one (a Switch pad's A is on the right, not the bottom), by
                // place otherwise (a PlayStation pad's cross is A).
                uint16_t bit = m.x;
                switch (SDL_GetGamepadButtonLabel(g, m.b)) {
                case SDL_GAMEPAD_BUTTON_LABEL_A: bit = 0x1000; break;
                case SDL_GAMEPAD_BUTTON_LABEL_B: bit = 0x2000; break;
                case SDL_GAMEPAD_BUTTON_LABEL_X: bit = 0x4000; break;
                case SDL_GAMEPAD_BUTTON_LABEL_Y: bit = 0x8000; break;
                default: break;
                }
                r.buttons |= bit;
            }
            r.left_trigger = uint8_t(SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) >> 7);
            r.right_trigger = uint8_t(SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) >> 7);
            r.thumb_lx = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX);
            r.thumb_ly = int16_t(std::clamp(-int(SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY)) - 1, -32768, 32767));
        }
        in.pads[i] = r;
    }
    SDL_free(ids);
}

}  // namespace

int main(int argc, char** argv) {
    crash::install();
    std::string name, mode, out, mod, replay;
    int ticks = 1;
    int jump_ticks = 0;  // --jump FILE TICKS
    float scale = 1;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--capture" && i + 2 < argc) {
            mode = arg;
            ticks = std::atoi(argv[++i]);
            out = argv[++i];
        } else if (arg == "--dump" && i + 1 < argc) {
            mode = arg;
            ticks = std::atoi(argv[++i]);
        } else if ((arg == "--shot" || arg == "--layers") && i + 2 < argc) {
            mode = arg;
            ticks = std::atoi(argv[++i]);
            out = argv[++i];
        } else if (arg == "--verify" && i + 1 < argc) {
            mode = arg;
            replay = argv[++i];
        } else if (arg == "--selftest" && i + 2 < argc) {
            mode = arg;
            replay.clear();
            name = argv[++i];  // the cases file
            out = argv[++i];
        } else if (arg == "--replay" && i + 1 < argc) {
            replay = argv[++i];
        } else if (arg == "--jump" && i + 2 < argc) {
            replay = argv[++i];
            jump_ticks = std::atoi(argv[++i]);
        } else if (arg == "--mod" && i + 1 < argc) {
            mod = argv[++i];
        } else if (arg == "--export" && i + 1 < argc) {
            mode = arg;
            out = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') scale = float(std::atof(argv[++i]));
        } else {
            name = arg;
        }
    }
    fs::path assets = find_repo_dir("assets");
    if (!text::load(assets.string(), "en")) SDL_Log("fonts or strings missing in %s", assets.string().c_str());

    // The window uses the executable's icon (engine/resources/castle.rc).
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON, "1");
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
    if (!mode.empty()) flags |= SDL_WINDOW_HIDDEN;
    SDL_Window* window = SDL_CreateWindow("Castle Crashers", 1280, 725, flags);
    SDL_GLContext context = window ? SDL_GL_CreateContext(window) : nullptr;
    if (!context) {
        SDL_Log("window: %s", SDL_GetError());
        return 1;
    }
    SDL_GL_SetSwapInterval(1);

    render::Renderer renderer;
    if (!renderer.init()) {
        SDL_Log("OpenGL 3.3 is required");
        return 1;
    }

    if (mode == "--export") {
        auto movie = swf::Movie::load(movie_path(assets / "swf", name.empty() ? "logo" : name).string());
        if (!movie) return 1;
        if (!mod.empty()) player::apply_mod(*movie, find_repo_dir(fs::path("mods") / mod));
        fs::create_directories(out);
        int saved = 0;
        for (auto& [id, ch] : movie->characters) {
            std::string file = (fs::path(out) / (movie->name + "_" + std::to_string(id) + "_" +
                                                 type_name(ch->type) + ".png")).string();
            if (export_character(renderer, *movie, *ch, scale, file)) saved++;
        }
        SDL_Log("exported %d characters to %s", saved, out.c_str());
        SDL_Quit();
        return 0;
    }

    player::Game game(assets / "swf");
    if (!mod.empty()) game.mod_dir = find_repo_dir(fs::path("mods") / mod);
    // TESTING: CASTLE_LEVEL=N -- the first level the game loads is level N
    if (const char* level = std::getenv("CASTLE_LEVEL")) game.boot_level = std::atoi(level);
    // TESTING: CASTLE_MAX=N -- character N maxed (2: the red knight)
    if (const char* max = std::getenv("CASTLE_MAX")) game.max_character = std::atoi(max);
    if (mode == "--selftest") {
        int result = verify::selftest(game, name, out);
        SDL_Quit();
        return result;
    }
    if (!replay.empty() && !load_replay(game, replay)) SDL_Log("can't read replay %s", replay.c_str());
    // Playing (not capturing or replaying): progress is kept in a save file
    // next to the executable.
    if (mode.empty() && replay.empty()) {
        fs::path base = SDL_GetBasePath() ? fs::path(SDL_GetBasePath()) : fs::current_path();
        game.save_file = base / "castle_save.dat";
    }
    game.start(name);
    if (!game.captured_storage.empty()) {
        const auto& ours = game.storage.bytes();
        size_t differ = 0;
        for (size_t i = 0; i < ours.size(); i++)
            if (i >= game.captured_storage.size() || ours[i] != game.captured_storage[i]) {
                if (differ++ < 8)
                    SDL_Log("  0x%zx: ours %02x, captured %02x", i, ours[i],
                            i < game.captured_storage.size() ? game.captured_storage[i] : 0);
            }
        SDL_Log("save storage: %zu of %zu bytes differ from a fresh save", differ, ours.size());
        // The real game had loaded a save file: the replay
        // starts from it, checked like a loaded save.
        if (differ && game.captured_storage.size() >= save::Storage::kSize) {
            game.saved = game.captured_storage;
            game.storage.assign(game.captured_storage);
            game.storage.sanitize();
        }
    }
    if (!g_key_replay.changes.empty())
        player::Player::on_updated = [&](player::Player& p) { g_key_replay.on_update(game, p); };
    // Playing: the session recorded (SessionRecorder), from the save loaded.
    SessionRecorder recorder;
    if (mode.empty() && replay.empty()) {
        fs::path base = SDL_GetBasePath() ? fs::path(SDL_GetBasePath()) : fs::current_path();
        recorder.open(base / "sessions", game);
        player::Player::on_updated = [&](player::Player& p) { recorder.update(p); };
    }

    if (mode == "--verify") {
        std::setvbuf(stdout, nullptr, _IONBF, 0);  // progress survives a crash
        verify::Session session;
        if (!session.load(replay)) {
            SDL_Log("can't read session %s", replay.c_str());
            return 1;
        }
        player::Player::on_updated = [&](player::Player& p) {
            session.on_update(p);
            g_key_replay.on_update(game, p);
        };
        unsigned limit = session.last_update() + 2000;
        for (unsigned t = 0; t < limit && !session.done(); t++) game.tick();
        int result = session.report();
        if (g_key_replay.next < g_key_replay.changes.size()) {
            const auto& c = g_key_replay.changes[g_key_replay.next];
            std::printf("input replay: %zu of %zu changes applied; stuck at the one after the update %d/%d #%d\n",
                        g_key_replay.next, g_key_replay.changes.size(), c.frame, c.total, c.occurrence);
        }
        SDL_Quit();
        return result;
    }

    if (mode == "--capture") {
        FILE* f = std::fopen(out.c_str(), "w");
        if (!f) return 1;
        unsigned updates = 0;
        player::Player::on_updated = [&](player::Player& p) {
            player::dump_update(f, p, ++updates);
            g_key_replay.on_update(game, p);
        };
        for (int t = 0; t < ticks; t++) game.tick();
        std::fclose(f);
        SDL_Log("captured %u updates in %d ticks to %s", updates, ticks, out.c_str());
        SDL_Quit();
        return 0;
    }

    if (mode == "--dump") {
        for (int t = 0; t < ticks; t++) game.tick();
        if (!game.current()) return 1;
        std::printf("%s\n", window_title(game).c_str());
        std::printf("!! input: %s\n!! loading: %s\n!! updates %u\n", game.input_state().c_str(),
                    game.loading_state().c_str(), g_key_replay.updates);
        player::dump_state(stdout, game.current()->root());  // (the tree, then every named clip's variables)
        if (player::after_dump) player::after_dump();
        SDL_Quit();
        return 0;
    }

    if (mode == "--shot" || mode == "--layers") {
        for (int t = 0; t < ticks; t++) game.tick();
        bool ok = game.current() != nullptr;
        if (ok && mode == "--shot") {
            ok = save_stage(renderer, game, out);
        } else if (ok) {
            fs::create_directories(out);
            for (player::Character* c = game.current()->root()->first_child; c; c = c->next) {
                std::string file = (fs::path(out) / (game.current()->name() + "_t" + std::to_string(ticks) + "_depth" +
                                                     std::to_string(c->depth) + "_id" +
                                                     std::to_string(c->character_id) + ".png")).string();
                ok = save_stage(renderer, game, file, c) && ok;
            }
        }
        SDL_Log("%s %s after %d ticks -> %s", ok ? "saved" : "failed", window_title(game).c_str(), ticks, out.c_str());
        SDL_Quit();
        return ok ? 0 : 1;
    }

    // The game runs at a fixed 30 ticks per second (castle.exe slows down
    // rather than skipping frames).
    const uint64_t tick_ns = uint64_t(1e9 / 30);
    uint64_t next_tick = SDL_GetTicksNS();
    if (!std::getenv("CASTLE_SILENT")) audio::manager().init(assets / "audio");  // (silent: tests)
    fs::path base =SDL_GetBasePath() ? fs::path(SDL_GetBasePath()) : fs::current_path();
    // --jump (F5): the session played to that tick, unseen and silent, then
    // the game is yours from there; recorded, with the part jumped through,
    // as a session that replays the same way. Progress isn't saved (the save
    // it goes from is the session's, older than yours).
    if (jump_ticks > 0) {
        SDL_SetWindowTitle(window, "Castle Crashers - jumping...");
        audio::manager().hold_silent(true);
        for (int t = 0; t < jump_ticks; t++) game.tick();
        audio::manager().hold_silent(false);
        recorder.open_after_jump(base / "sessions", replay, g_key_replay.updates, game);
        g_key_replay.changes.clear();
        g_key_replay.next = 0;
        game.input.keyboard.keys = {};
        game.window_active = true;
        game.suspended = false;
        player::Player::on_updated = [&](player::Player& p) { recorder.update(p); };
        SDL_Log("jumped %d ticks into %s", jump_ticks, replay.c_str());
    }
    uint64_t last_iteration = SDL_GetTicksNS();
    bool running = true, paused = false;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) running = false;
            if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
                bool down = e.type == SDL_EVENT_KEY_DOWN;
                if (down && e.key.key == SDLK_F1) paused = !paused;
                if (down && e.key.key == SDLK_F2 && paused) game.tick();
                // F3: the current movie's state (tree, every named clip's
                // variables) and what holds input, to state_dump.txt next
                // to the executable, for a screen the game never leaves.
                if (down && e.key.key == SDLK_F3 && game.current()) {
                    fs::path base = SDL_GetBasePath() ? fs::path(SDL_GetBasePath()) : fs::current_path();
                    if (FILE* f = std::fopen((base / "state_dump.txt").string().c_str(), "w")) {
                        std::fprintf(f, "%s\ninput: %s\nloading: %s\n", window_title(game).c_str(),
                                     game.input_state().c_str(), game.loading_state().c_str());
                        player::dump_state(f, game.current()->root());
                        std::fclose(f);
                        SDL_Log("state dumped to %s", (base / "state_dump.txt").string().c_str());
                    }
                }
                // F5: jump to the point jump.txt next to the executable names
                // ("SESSION TICKS", the session relative to it): the game
                // starts again with --jump and this one closes.
                if (down && e.key.key == SDLK_F5 && !e.key.repeat) {
                    std::string file;
                    int at = 0;
                    if (FILE* f = std::fopen((base / "jump.txt").string().c_str(), "r")) {
                        char path[1024] = {};
                        if (std::fscanf(f, "%1023s %d", path, &at) == 2) file = (base / path).string();
                        std::fclose(f);
                    }
                    if (file.empty() || at <= 0 || !fs::exists(file)) {
                        SDL_Log("F5: no jump (jump.txt next to the executable: SESSION TICKS)");
                    } else {
                        std::string self = (base / fs::path(argv[0]).filename()).string();
                        if (fs::path(self).extension().empty()) self += ".exe";
                        std::string ticks_text = std::to_string(at);
                        const char* args[] = {self.c_str(), "--jump", file.c_str(), ticks_text.c_str(), nullptr};
                        if (SDL_Process* p = SDL_CreateProcess(args, false)) {
                            SDL_DestroyProcess(p);  // (it runs on)
                            running = false;
                        } else {
                            SDL_Log("F5: can't start %s: %s", self.c_str(), SDL_GetError());
                        }
                    }
                }
                // Like castle.exe's WM_KEYDOWN/WM_KEYUP handling: key-down
                // (and its auto-repeat) sets the key, key-up clears it.
                if (int vk = virtual_key(e.key.scancode)) game.input.keyboard.keys[vk] = down ? 1 : 0;
            }
            if (e.type == SDL_EVENT_GAMEPAD_ADDED) SDL_OpenGamepad(e.gdevice.which);
            // The mouse, for the menus, while the game shows its cursor.
            if (g_key_replay.changes.empty() && game.cursor_shown && game.window_active) {
                int msg = 0;
                float mx = 0, my = 0;
                if (e.type == SDL_EVENT_MOUSE_MOTION) {
                    msg = 0x200;
                    mx = e.motion.x;
                    my = e.motion.y;
                } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
                    msg = 0x201;
                } else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
                    msg = 0x202;
                } else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_RIGHT) {
                    msg = 0x205;
                }
                if (msg) {
                    if (msg != 0x200) {
                        mx = e.button.x;
                        my = e.button.y;
                    }
                    // Window to stage, through the letterbox the renderer draws in.
                    int ww = 1, wh = 1;
                    SDL_GetWindowSize(window, &ww, &wh);
                    float sw = kStage.xmax / 20.0f, sh = kStage.ymax / 20.0f;
                    float scale = std::min(float(ww) / sw, float(wh) / sh);
                    float ox = (float(ww) - sw * scale) * 0.5f, oy = (float(wh) - sh * scale) * 0.5f;
                    game.mouse_event(msg, (mx - ox) / scale, (my - oy) / scale);
                    recorder.mouse(msg, (mx - ox) / scale, (my - oy) / scale);
                }
            }
            // Like castle.exe, losing the window pauses the game and ignores
            // the pads (a replay has its own focus changes).
            if (g_key_replay.changes.empty()) {
                if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) game.window_active = false;
                if (e.type == SDL_EVENT_WINDOW_FOCUS_GAINED) game.window_active = true;
                if (e.type == SDL_EVENT_WINDOW_MINIMIZED) game.suspended = true;
                if (e.type == SDL_EVENT_WINDOW_RESTORED) game.suspended = false;
            }
        }
        uint64_t now = SDL_GetTicksNS();
        // The sound's fades run on the loop's own time.
        audio::manager().update(std::clamp(float(now - last_iteration) * 1e-9f, 1.0f / 60.0f, 0.1f));
        last_iteration = now;
        if (now >= next_tick) {
            if (g_key_replay.changes.empty()) read_gamepads(game.input);
            if (!paused) {
                recorder.input(game);
                game.tick();
            }
            if (game.quitting()) running = false;
            next_tick += tick_ns;
            if (now > next_tick + tick_ns) next_tick = now + tick_ns;  // slow down, don't skip
        }

        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        renderer.begin_frame(w, h, kStage, swf::Rgba{});
        game.render(renderer);
        SDL_GL_SwapWindow(window);
        SDL_SetWindowTitle(window, window_title(game).c_str());
        if (game.cursor_shown) SDL_ShowCursor();
        else SDL_HideCursor();
    }

    audio::manager().shutdown();
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
