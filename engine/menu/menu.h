// castle.exe's native menus: FlashController / BaseMenu (vtable 0x49bd04),
// its pages (Menu 0x49c9f4 / DefaultMenu 0x49ca28, 0x200 bytes each) and the
// menu items (MenuItem 0x49cc1c and subclasses). The menu draws its text
// itself, over the movie, and attaches Flash clips (backgrounds, button
// prompts) into the movie's root. See docs/engine/menus.md.
//
// Field comments give castle.exe's offsets.
#pragma once

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

namespace render { class Renderer; }
namespace input { class Input; }
namespace player { class Game; class Player; class MovieClip; }

namespace menu {

class BaseMenu;
class Page;

using Callback = void (*)(BaseMenu&);

// ---- Items

class Item {
public:
    enum Type { kItem = 0, kText = 1, kTitle = 2, kToggle = 3, kGraphic = 4, kInteractive = 5, kButton = 6 };

    virtual ~Item() = default;
    virtual void render(render::Renderer&) {}      // +0x04
    virtual void on_highlight() {}                 // +0x08
    virtual void on_unhighlight() {}               // +0x0c
    virtual void on_press() {}                     // +0x10
    virtual void on_14() {}                        // +0x14
    virtual bool active() const { return flag2b; }  // +0x18
    virtual bool selectable() const { return false; }  // +0x1c
    virtual void set_shown(bool on) { shown = on; }    // +0x20
    virtual int type() const { return kItem; }         // +0x24
    virtual void measure(int align) {}                 // +0x28
    // Interactive items (type 5).
    virtual void on_focus() {}                     // +0x2c
    virtual void on_blur() {}                      // +0x30
    virtual bool on_key(int, char) { return false; }  // +0x34
    virtual void on_activate() {}                  // +0x38

    float x = 424.0f;           // +0x04
    float y = 0.0f;             // +0x08
    float alpha = 0.0f;         // +0x0c
    uint32_t color = 0x777777ff;  // +0x10 RRGGBBAA
    float box[4] = {};          // +0x14 hit box: left, top, right, bottom
    bool flag24 = false;        // +0x24
    bool auto_layout = true;    // +0x28
    bool disabled = false;      // +0x29
    bool hittable = false;      // +0x2a
    bool flag2b = true;         // +0x2b
    bool shown = true;          // +0x2c
};

// TextMenuItem (0x4c bytes): a line of the menu's own text.
class TextItem : public Item {
public:
    TextItem();
    // text from string `id` (0 = none), fitted into `max_width`.
    TextItem(BaseMenu* ctx, Callback cb, int id, int max_width, bool hittable);

    void render(render::Renderer& r) override;
    bool selectable() const override { return !disabled && callback; }
    int type() const override { return kText; }
    void measure(int align) override;

    void set_text_id(int id, int max_width = 0);
    void set_text(const std::u16string& s, int max_width = 0);

    BaseMenu* ctx = nullptr;      // +0x30
    Callback callback = nullptr;  // +0x34
    std::u16string text;          // +0x38
    float scale = 1.0f;           // +0x3c
    float fit = 1.0f;             // +0x40
    uint16_t align = 2;           // +0x44: 0 left, 1 right, 2 centre
    uint16_t width = 0;           // +0x48 (estimate: 11 per wide char, 8 per ASCII, / 8)
    uint16_t max_width = 0;       // +0x4a
};

// TitleMenuItem: a page's heading.
class TitleItem : public TextItem {
public:
    explicit TitleItem(int id);
    int type() const override { return kTitle; }
};

// ToggleMenuItem (0x68 bytes): a text item cycling through options.
class ToggleItem : public TextItem {
public:
    ToggleItem(BaseMenu* ctx, Callback on_change, int id);
    void render(render::Renderer& r) override;
    bool selectable() const override { return !disabled; }
    int type() const override { return kToggle; }
    void measure(int align) override;
    void add_option(int id);
    void add_option(const std::string& s);
    void clear_options();

    std::vector<std::u16string> options;  // +0x4c
    Callback on_change = nullptr;         // +0x5c
    uint16_t selected = 0;                // +0x62
    uint16_t option_align = 1;            // +0x64
    bool wraps = true;                    // +0x66
};

// GraphicMenuItem (0x54 bytes): a clip attached to the movie's root.
class GraphicItem : public TextItem {
public:
    explicit GraphicItem(BaseMenu& menu);
    bool selectable() const override { return false; }
    void set_shown(bool on) override;
    int type() const override { return kGraphic; }
    // the root's child at `depth`, else a new instance of export
    // `name` inserted there; hidden, moved to (x, y).
    void attach(const std::string& name, int depth, float x, float y);

    player::Player* player = nullptr;  // +0x4c
    player::MovieClip* clip = nullptr;  // +0x50
    BaseMenu* menu = nullptr;
};

// ButtonMenuItem (0x58 bytes): a controller-button prompt with its label.
class ButtonItem : public GraphicItem {
public:
    ButtonItem(BaseMenu& menu, int button);  // + the original's
    void on_highlight() override { color = 0x777777ff; }
    void on_unhighlight() override { color = 0xffff00ff; }
    void on_press() override;                                // 0x4716d0
    bool active() const override { return flag2b; }
    int type() const override { return kButton; }

    int button = 0;  // +0x54
};

// ---- Pages

// A mouse region of a page (0x34 bytes, list at +0x1c): the pointer over it
// or released on it calls back, with where in it (0 to 1) it was.
struct Region {
    float box[4] = {};           // +0x00: left, top, right, bottom
    float rel_x = 0, rel_y = 0;  // +0x14, +0x18
    BaseMenu* ctx = nullptr;     // +0x1c
    Callback on_hover = nullptr;  // +0x20 (moved or pressed over it)
    Callback on_up = nullptr;     // +0x28 (released over it)
    bool enabled = true;          // +0x30 bit 0
};

// Menu / DefaultMenu (0x200 bytes).
class Page {
public:
    Page();
    Page(Page&&) = default;
    Page& operator=(Page&&) = default;

    void add(Item* item, float x = 0.0f, float y = 0.0f);
    void set_items_shown(bool on);
    void update(BaseMenu& menu, float dt);
    void render(render::Renderer& r);

    // Input actions (vtable +0x10..+0x2c).
    void accept(BaseMenu& menu);
    void back(BaseMenu& menu);
    void action_x(BaseMenu& menu);
    void action(int id, Callback cb);  // the original's / 230 / 250 (Y and the triggers)
    void navigate(BaseMenu& menu, int direction);  // 0 up, 1 down, 2 left, 3 right
    void faded_out();
    void select(BaseMenu& menu, uint16_t index);  // vtable +0x2c, the original's
    // The mouse (vtable +0x0c): moves and left button presses
    // over an item select it, a release on the selected one accepts it; the
    // button prompts light up and are pressed; a right click is Back.
    // Coordinates are the stage's.
    void mouse(BaseMenu& menu, int message, float x, float y);
    void run_accept();
    void flash_selected();

    std::vector<std::unique_ptr<Item>> owned;
    std::vector<Item*> items;    // +0x3c (count +0x04)
    std::vector<Item*> clips;    // +0x13c (count +0x06)
    std::vector<Item*> buttons;  // +0x17c (count +0x08)
    std::vector<Region> regions;  // +0x1c (count +0x0a)
    uint16_t selected = 0;       // +0x0c
    int16_t port = -1;           // +0x0e: the port that last pressed a button here
    int16_t input_port = -2;     // +0x10: whose buttons count: a port, -2 any, -3 any signed in
    uint16_t flags = 0;          // +0x12: requirements checked on entry
    bool active = false;         // +0x14
    bool input_on = false;       // +0x15
    int last_action = 0;         // +0x18
    BaseMenu* ctx = nullptr;     // +0x1a4
    Callback on_accept = nullptr;  // +0x1a8
    Callback on_back = nullptr;    // +0x1ac
    Callback on_x = nullptr;       // +0x1b0
    Callback on_y = nullptr;       // +0x1b4
    Callback on_lb = nullptr;      // +0x1b8
    Callback on_rb = nullptr;      // +0x1bc
    Callback on_update = nullptr;  // +0x1c0
    Callback on_done = nullptr;    // +0x1c4
    Callback on_enter = nullptr;   // +0x1c8
    uint16_t next_page = 0;        // +0x1cc
    uint16_t return_page = 0;      // +0x1ce
    // Grid layout (+0x1d0..+0x1e0).
    bool grid = false;
    uint16_t grid_columns = 0, grid_rows = 0, grid_column = 0, grid_row = 0;
    float grid_x = 0, grid_y = 0;
    float pulse_rate = 0;          // +0x1e8
    float flash_time = 0;          // +0x1ec
    uint32_t flash_color = 0;      // +0x1f0
    uint8_t flags1f4 = 0;          // +0x1f4
    int8_t fade = 1;               // +0x1f5: 1 fading in, -1 out, 0 idle
    float fade_alpha = 0;          // +0x1f8
    float time = 0;                // +0x1fc
    bool input_blocked = false;    // +0x1e4
};

// The online manager's state the menus read (*(0x651f70)).
struct Online {
    uint32_t signed_in = 0xf;  // +0x28: ports signed in (all four, at sign-in)
    uint32_t can_play_online = 1;  // +0x4c
    uint32_t has_privileges = 1;   // +0x50
};

// ---- Controllers

class BaseMenu {
public:
    BaseMenu(player::Game& game, player::Player& movie);
    virtual ~BaseMenu() = default;
    virtual void update(float dt);  // vtable +0x04
    void render(render::Renderer& r);  // +0x08
    virtual int kind() const { return 0; }  // +0x0c: 1 main menu, 2 pause menu, 3 results

    void set_page(int index);
    // A mouse message for the current page (vtable +0x00):
    // 0x200 move, 0x201 left down, 0x202 left up, 0x205 right up.
    void pointer(int message, float x, float y);
    Page& page(int index) { return pages[size_t(index)]; }
    bool port_button(Page& p, int button, bool held);
    bool signed_in(int port) const;
    void play_sound(int which);
    void check_requirements(int prev_port, uint16_t flags);
    void check_each_tick(uint16_t flags);

    player::Game& game;
    player::Player& movie;              // +0x04
    Page* current = nullptr;            // +0x38
    uint16_t current_index = 0;         // +0x3c
    std::vector<Page> pages;            // +0x40
    uint16_t error = 0;                 // +0x34
    uint16_t message = 0;               // +0x36
    Online online;

protected:
    // Building blocks: an item made, owned by the page and added to it.
    TextItem* text(Page& p, int id, Callback cb = nullptr, int max = 0, bool hit = true, float x = 0, float y = 0);
    TitleItem* title(Page& p, int id, float x = 0, float y = 0);
    ButtonItem* button(Page& p, int kind);
    GraphicItem* graphic(Page& p, const char* name, int depth, float x, float y);
    ToggleItem* toggle(Page& p, Callback cb, int id, std::initializer_list<int> options, float x = 0, float y = 0);

    // Pages 0-17, the same in every controller: messages,
    // "are you sure", Help & Options and what it leads to, leaderboards.
    // Their clips go into this controller's movie.
    void build_0_17();
    void build_5(Page& p);
    void build_6(Page& p);
    void build_7(Page& p);
    void build_8(Page& p);
    void build_9(Page& p);
    void build_10(Page& p);
    void build_11(Page& p);
    void build_15(Page& p);
    void build_16(Page& p);
    void build_17(Page& p);
    void build_leaderboard(Page& p, int heading, std::initializer_list<int> columns, int grid_columns);
};

// Creates the controller SetFlashController(name) asks for, or null.
std::unique_ptr<BaseMenu> create(const std::string& name, player::Game& game, player::Player& movie);

}  // namespace menu
