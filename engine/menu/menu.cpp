#include "menu/menu.h"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cmath>

#include "audio/audio.h"
#include "input/input.h"
#include "player/game.h"
#include "player/movieclip.h"
#include "player/player.h"
#include "render/renderer.h"
#include "text/fonts.h"
#include "text/layout.h"

namespace menu {

namespace {

constexpr float kStageWidth = 848.0f;   // the original's +0xc
constexpr float kStageHeight = 480.0f;  // the original's +0x10

std::u16string to_u16(const std::string& utf8) {
    std::u16string out;
    for (char32_t c : text::utf8_to_utf32(utf8)) out.push_back(char16_t(c));
    return out;
}

// The width castle.exe estimates for fitting text: 11 per
// character from U+0080 up, 8 below, divided by 8.
uint16_t estimate_width(const std::u16string& s) {
    unsigned w = 0;
    for (char16_t c : s) w += c < 0x80 ? 8 : 11;
    return uint16_t(w >> 3);
}

}  // namespace

// ---- TextItem

TextItem::TextItem() {
    hittable = true;
}

TextItem::TextItem(BaseMenu* c, Callback cb, int id, int max, bool hit) {
    hittable = hit;
    ctx = c;
    callback = cb;
    scale = 0.8421052694320679f;
    set_text_id(id, max);
    if (!selectable()) color = 0xaaaaaaff;
}

void TextItem::set_text(const std::u16string& s, int max) {
    text = s;
    width = estimate_width(text);
    if (max) max_width = uint16_t(max);
    fit = 1.0f;
    if (max_width) {
        fit = float(max_width) / float(width);
        if (fit > 1.0f) fit = 1.0f;
    }
}

void TextItem::set_text_id(int id, int max) {
    set_text(to_u16(text::strings().get(id)), max);
}

// the size of `s` in the game font, widths scaled by `sx` and
// the line height by `sy` (plus one pixel per line).
static void measure_text(const std::u16string& s, float sx, float sy, float& w, float& h) {
    w = h = 0;
    const text::Font* font = text::game_font();
    if (!font || s.empty()) return;
    float line = float(font->line_height) * sy + 1.0f;
    float x = 0, bottom = line;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == u'\n') {
            if (x > w) w = x;
            h = bottom;
            bottom += line;
            x = 0;
            continue;
        }
        const text::Glyph* g = font->glyph(s[i]);
        if (!g) continue;
        x += float(g->xadvance) * sx;
        if (i + 1 >= s.size()) break;
        x += float(font->kerning(s[i], s[i + 1])) * sx;
    }
    if (x > w) w = x;
    h = bottom;
}

// draws `s` at (x, y) in stage pixels, vertically centred;
// align 0 ends it at x, 1 starts it there, 2 centres it.
static void draw_text(render::Renderer& r, const std::u16string& s, float x, float y, uint32_t rgba, float sx,
                      float sy, int align) {
    text::Font* font = text::game_font();
    if (!font || s.empty()) return;
    float w, h;
    measure_text(s, 1.0f, 1.0f, w, h);
    float pen_y = std::round(h * -0.5f);
    float pen_x = 0;
    bool line_start = true;
    std::vector<text::GlyphQuad> quads;
    for (size_t i = 0; i < s.size(); i++) {
        if (line_start) {
            if (align == 0 || align == 2) {
                float lw, lh;
                measure_text(s.substr(i), 1.0f, 1.0f, lw, lh);
                pen_x = align == 0 ? -lw : std::round(lw * -0.5f);
            } else {
                pen_x = 0;
            }
            line_start = false;
        }
        if (s[i] == u'\n') {
            pen_y += float(font->line_height);
            line_start = true;
            continue;
        }
        const text::Glyph* g = font->glyph(s[i]);
        if (!g) continue;
        if (s[i] != u' ' && g->width) {
            float gx0 = pen_x - 0.5f + g->xoffset, gx1 = pen_x + 0.5f + g->xoffset + g->width;
            float gy0 = pen_y + 0.5f + g->yoffset, gy1 = pen_y - 0.5f + g->yoffset + g->height;
            text::GlyphQuad q;
            q.x0 = (x + gx0 * sx) * 20;
            q.x1 = (x + gx1 * sx) * 20;
            q.y0 = (y + gy0 * sy) * 20;
            q.y1 = (y + gy1 * sy) * 20;
            q.u0 = (g->x + 0.5f) / float(font->page_width);
            q.u1 = q.u0 + float(g->width) / float(font->page_width);
            q.v0 = (g->y + 0.5f) / float(font->page_height);
            q.v1 = q.v0 + float(g->height) / float(font->page_height);
            quads.push_back(q);
        }
        pen_x += float(g->xadvance);
    }
    swf::Rgba c{uint8_t(rgba >> 24), uint8_t(rgba >> 16), uint8_t(rgba >> 8), uint8_t(rgba)};
    r.draw_text(*font, quads, c, swf::Matrix{}, swf::CXform{});
}

void TextItem::measure(int a) {
    // the hit box, by alignment.
    float w, h;
    measure_text(text, fit * scale, scale, w, h);
    float left, top;
    if (a == 0) {
        left = x - w;
        top = y + h * 0.5f - h;
    } else if (a == 1) {
        left = x;
        top = y - h * 0.5f;
    } else {
        left = x - w * 0.5f;
        top = y - h * 0.5f;
    }
    box[0] = left;
    box[1] = top;
    box[2] = left + w;
    box[3] = top + h;
}

void TextItem::render(render::Renderer& r) {
    // a black copy one pixel left and down, then the text in
    // its colour, both at the page's alpha.
    if (text.empty()) return;
    uint32_t a = uint32_t(std::lround(alpha * 255.0f)) & 0xff;
    draw_text(r, text, std::round(x) - 1.0f, std::round(y) + 1.0f, a, fit * scale, scale, align);
    draw_text(r, text, std::round(x), std::round(y), (color & 0xffffff00u) | a, fit * scale, scale, align);
    if (hittable) measure(align);
}

// ---- TitleItem

TitleItem::TitleItem(int id) {
    auto_layout = false;
    scale = 1.25f;
    set_text_id(id, 0);
    color = 0xddddddff;
    y = 70.0f;
    hittable = false;
    x = kStageWidth * 0.5f;
}

// ---- ToggleItem

ToggleItem::ToggleItem(BaseMenu* c, Callback change, int id) {
    on_change = change;
    ctx = c;
    callback = nullptr;
    set_text_id(id, 0);
    if (!selectable()) color = 0xaaaaaaff;
    align = 0;
}

void ToggleItem::add_option(int id) {
    if (text == u"*") option_align = 2;
    std::u16string s = to_u16(text::strings().get(id));
    if (!s.empty()) options.push_back(s);
}

void ToggleItem::add_option(const std::string& s) {
    if (s == "*") option_align = 2;
    if (!s.empty()) options.push_back(to_u16(s));
}

void ToggleItem::clear_options() {
    options.clear();
    selected = 0;
}

void ToggleItem::render(render::Renderer& r) { TextItem::render(r); }
void ToggleItem::measure(int a) { TextItem::measure(a); }

// ---- GraphicItem

GraphicItem::GraphicItem(BaseMenu& m) {
    player = &m.movie;
    menu = &m;
}

void GraphicItem::attach(const std::string& name, int depth, float px, float py) {
    player::MovieClip* root = player->root();
    clip = nullptr;
    for (player::Character* c = root->first_child; c; c = c->next) {
        if (c->depth == depth) {
            clip = player::MovieClip::from(c);
            break;
        }
    }
    if (!clip) {
        auto it = player->movie().exports.find(name);
        if (it != player->movie().exports.end()) {
            if (player::MovieClip* made = player->instantiate(it->second)) {
                made->depth = depth;
                made->birth_frame = 1;
                root->insert_child(made, false);
                clip = made;
            }
        }
    }
    if (clip) {
        clip->flags &= uint8_t(~player::MovieClip::kVisible);
        clip->matrix.tx = px;
        clip->flags5c |= player::Character::kScriptMoved;
        clip->matrix.ty = py;
    }
}

void GraphicItem::set_shown(bool on) {
    shown = on;
    if (clip) clip->flags = uint8_t((clip->flags & 0x7f) | (on ? 0x80 : 0));
}

// ---- ButtonItem

namespace {

struct ButtonKind {
    const char* clip;
    int label;
    int max_width;
    int x;  // 0 left, 1 right
    float y;
};

// the original's's table: the prompt's clip, label, and place.
constexpr float kY = 425.0f;
const ButtonKind kButtons[24] = {
    {"Bbutton", 0x5f, 0, 1, kY},        {"Ybutton", 0x1c5, 0, 1, 360.0f}, {"Xbutton", 0x272, 0, 0, 360.0f},
    {"Abutton", 0x5a, 0, 0, kY},        {"Bbutton", 0x62, 0, 1, kY},      {"Xbutton", 0x24f, 0, 0, 390.0f},
    {"Xbutton", 0x1bd, 0, 0, 390.0f},   {"Xbutton", 0x1bd, 0, 0, 395.0f}, {"Bbutton", 0x1c1, 0, 1, kY},
    {"Xbutton", 0x1dd, 0, 1, kY},       {"Rtrigger", 0x1c2, 0, 1, 430.0f}, {"Ltrigger", 0x1c3, 0, 0, 430.0f},
    {"Bbutton", 0x5f, 0, 1, 395.0f},    {"Abutton", 0x1bb, 0, 0, 395.0f}, {"Ybutton", 0x197, 0x19, 0, kY},
    {"Xbutton", 0x18e, 0, 0, 390.0f},   {"Bbutton", 0x214, 0, 1, kY},     {"Abutton", 0x60, 0, 0, kY},
    {"Abutton", 0x1bb, 0, 0, kY},       {"Ybutton", 0x1bb, 0, 0, 390.0f}, {"Ybutton", 0x1c4, 0, 1, 390.0f},
    {"Xbutton", 0x258, 0, 0, 390.0f},   {"Xbutton", 0x198, 0, 0, 395.0f}, {"Abutton", 0x2df, 0, 0, kY},
};

}  // namespace

ButtonItem::ButtonItem(BaseMenu& menu, int kind) : GraphicItem(menu), button(kind) {
    const ButtonKind& k = kButtons[kind];
    int max = k.max_width;
    if (kind == 20) max = 0x10;  // 0 when the game is online-capable (the original's +9)
    set_text_id(k.label, max);
    float left = kStageWidth * 0.1428571492433548f;
    float x0 = k.x ? kStageWidth * 0.5f + left : left;
    attach(k.clip, 3000 + kind, x0, k.y);
    scale = 0.7f;
    align = 1;
    // The label goes right of the clip, vertically on its middle.
    float b[4] = {};
    if (clip) clip->bounds(player::Character::Matrix{}, b);
    float tw, th;
    measure_text(text, 0.56f, 0.7f, tw, th);
    x = (b[2] - b[0]) * 0.5f + x0 + 5.0f;
    y = (b[3] - b[1]) * 0.5f + k.y - th * 0.5f;
    color = 0xaaaaaaff;
    if (kind == 0x11) flag2b = false;  // the "Select" prompt can't be clicked
}

void ButtonItem::on_press() {
    // 0x4716d0: clicked, the prompt does its action on the active menu's
    // current page (0x47174c maps the kinds to the page's actions).
    static const uint8_t kAction[24] = {0, 1, 2, 3, 0, 2, 2, 2, 0, 2, 4, 5, 0, 3, 1, 2, 3, 3, 1, 1, 1, 2, 2, 3};
    BaseMenu* active = menu ? menu->game.active_controller() : nullptr;
    if (!active || !active->current || button < 0 || button > 23) return;
    Page& p = *active->current;
    switch (kAction[button]) {
    case 0: p.back(*active); break;                           // vtable +0x14
    case 1: if (p.on_y) p.action(4, p.on_y); break;            // +0x1c
    case 2: p.action_x(*active); break;                       // +0x18
    case 3: p.accept(*active); break;                         // +0x10
    case 4: if (p.on_lb) p.action(5, p.on_lb); break;          // +0x20
    case 5: if (p.on_rb) p.action(6, p.on_rb); break;          // +0x24
    default: break;
    }
}

// ---- Page

Page::Page() {
    pulse_rate = 10.0f;
}

void Page::add(Item* item, float px, float py) {
    // the original's (item in EAX, page in ESI, x on the stack, y in XMM0).
    float w = kStageWidth, h = kStageHeight;
    if (grid) {
        // Rows of `grid_columns`, left to right, 0.6 size.
        if (auto* t = dynamic_cast<TextItem*>(item)) t->scale = 0.6f;
        float row_offset = (float(grid_row) - float(grid_rows) * 0.5f) * 30.0f;
        item->auto_layout = false;
        item->x = (w * 0.84f / float(grid_columns * 2)) * float(grid_column * 2 - 1) + grid_x + w * 0.08f;
        item->y = row_offset * 0.6f + grid_y;
        grid_column++;
        if (grid_columns < grid_column) {
            grid_row++;
            grid_column = 1;
            if (grid_rows < grid_row) grid = false;
        }
    } else if (py != 0.0f) {
        item->auto_layout = false;
        item->x = px;
        item->y = py;
    }
    int t = item->type();
    if (t == Item::kGraphic || t == 7) {
        clips.push_back(item);
        return;
    }
    if (t == Item::kButton) {
        buttons.push_back(item);
        return;
    }
    items.push_back(item);
    // Lay the list out: a column in the middle, 30 apart, centred on 60 below
    // the middle of the stage.
    float half_h = h * 0.5f;
    float span = float(items.size()) * 0.5f * 30.0f;
    for (size_t i = 0, row = 0; i < items.size(); i++, row += 30) {
        if (!items[selected]->selectable()) selected = uint16_t(i);
        Item* it = items[i];
        if (it->auto_layout) {
            it->x = w * 0.5f;
            it->y = float(row) + (half_h - span) + 60.0f;
        }
    }
}

void Page::set_items_shown(bool on) {
    for (Item* i : items) i->set_shown(on);
    for (Item* i : clips) i->set_shown(on);
    for (Item* i : buttons) i->set_shown(on);
}

void Page::render(render::Renderer& r) {
    for (Item* i : items)
        if (i->shown) i->render(r);
    for (Item* i : clips)
        if (i->shown) i->render(r);
    for (Item* i : buttons)
        if (i->shown) i->render(r);
}

// the original's...was a button pressed (or held, for the
// shoulder ones) by the page's player? -2 takes any port and adopts it; -3
// any port that is signed in.
bool BaseMenu::port_button(Page& p, int button, bool held) {
    auto test = [&](int port) {
        const input::Button& b = game.input.ports[port].buttons[button];
        return (b.config & 1) && (held ? (b.state & 1) : (b.state & 8));
    };
    if (p.input_port == -2) {
        for (int port = 0; port < 4; port++) {
            if (test(port)) {
                p.port = int16_t(port);
                return true;
            }
        }
    } else if (p.input_port == -3) {
        for (int port = 0; port < 4; port++) {
            if (signed_in(port) && test(port)) {
                p.port = p.input_port = int16_t(port);
                return true;
            }
        }
    } else if (p.input_port >= 0 && p.input_port < 4 && test(p.input_port)) {
        p.port = p.input_port;
        return true;
    }
    return false;
}

void Page::update(BaseMenu& menu, float dt) {
    // input.
    if (input_on && !input_blocked) {
        using F = input::FlashPad;
        if (menu.port_button(*this, F::kAccept, false) || menu.port_button(*this, F::kStartButton, false)) {
            accept(menu);
        } else if (on_back && menu.port_button(*this, F::kCancel, false)) {
            back(menu);
        } else if (on_x && menu.port_button(*this, F::kButtonX, false)) {
            action_x(menu);
        } else if (on_y && menu.port_button(*this, F::kButtonY, false)) {
            action(4, on_y);
        } else if (on_lb && menu.port_button(*this, 17, true)) {
            action(5, on_lb);
        } else if (on_rb && menu.port_button(*this, 16, true)) {
            action(6, on_rb);
        } else if (menu.port_button(*this, F::kUp, false)) {
            navigate(menu, 0);
        } else if (menu.port_button(*this, F::kDown, false)) {
            navigate(menu, 1);
        } else if (menu.port_button(*this, F::kLeft, false)) {
            navigate(menu, 2);
        } else if (menu.port_button(*this, F::kRight, false)) {
            navigate(menu, 3);
        }
    }
    if (on_update && ctx) on_update(*ctx);
    // the selected item pulses yellow, or flashes after a press.
    if (flash_time == 0.0f) {
        time += dt;
        if (!items.empty()) {
            float s = std::sin(time * pulse_rate);
            uint32_t v = uint32_t(int(s * 34.0f + 221.0f)) & 0xff;
            items[selected]->color = ((v << 8 | v) << 16) | 0xff;
        }
    } else {
        if (!items.empty()) {
            items[selected]->color = flash_color;
            flash_color = flash_color == 0xffff00ff ? 0x777777ff : 0xffff00ff;
        }
        flash_time -= dt;
        if (flash_time <= 0.0f) {
            flash_time = 0.0f;
            fade = -1;
        }
    }
    // fading in or out.
    if (fade != 0) {
        fade_alpha = float(fade) * dt * 5.0f + fade_alpha;
        if (fade_alpha > 1.0f) {
            fade_alpha = 1.0f;
            input_on = true;
            fade = 0;
        } else if (fade_alpha < 0.0f) {
            faded_out();
        }
        for (Item* i : items) i->alpha = fade_alpha;
        for (Item* i : clips) i->alpha = fade_alpha;
        for (Item* i : buttons) i->alpha = fade_alpha;
    }
}

void Page::select(BaseMenu& menu, uint16_t i) {

    if (items.empty() || !items[selected]->active() || i >= items.size() || i == selected || !items[i]->active())
        return;
    items[selected]->color = 0x777777ff;
    menu.play_sound(0);
    uint16_t old = selected;
    selected = i;
    if (items[old]->type() == Item::kInteractive) items[old]->on_blur();
    if (items[i]->type() == Item::kInteractive) items[i]->on_focus();
}

void Page::mouse(BaseMenu& menu, int msg, float x, float y) {
    // while the page takes input from any port (or port 0).
    if (!input_on) return;
    bool any = input_port == 0 || input_port == -2 || (input_port == -3 && menu.game.registered(0));
    if (!any || (msg != 0x200 && msg != 0x201 && msg != 0x202 && msg != 0x205)) return;

    if (msg == 0x205) {
        port = 0;
        if (on_back) back(menu);
        return;
    }
    auto inside = [&](const float b[4]) { return x <= b[2] && b[0] <= x && y <= b[3] && b[1] <= y; };
    // The hit boxes are the text's as last drawn (the original's measures
    // them while drawing); measured here too, drawn or not.
    auto measure = [](Item* it) {
        auto* t = dynamic_cast<TextItem*>(it);
        if (t && t->hittable && t->shown && !t->text.empty()) t->measure(t->align);
    };
    for (size_t i = 0; i < items.size(); i++) {
        Item* it = items[i];
        measure(it);
        if (!it->hittable || !it->shown || !inside(it->box)) continue;
        port = 0;
        if (msg == 0x200 || msg == 0x201) select(menu, uint16_t(i));
        else if (msg == 0x202 && selected == i) accept(menu);
        return;
    }
    for (Item* b : buttons) b->on_highlight();
    for (Item* b : buttons) {
        measure(b);
        if (!b->active() || !b->hittable || !b->shown || !inside(b->box)) continue;
        port = 0;
        if (msg == 0x200 || msg == 0x201) b->on_unhighlight();
        else if (msg == 0x202) b->on_press();
        return;
    }
    for (Region& r : regions) {
        if (!r.enabled || x > r.box[2] || x < r.box[0] || y > r.box[3] || y < r.box[1]) continue;
        float w = r.box[2] - r.box[0], h = r.box[3] - r.box[1];
        float rx = std::clamp(x - r.box[0], 0.0f, w), ry = std::clamp(y - r.box[1], 0.0f, h);
        r.rel_x = rx / w;
        r.rel_y = ry / h;
        port = 0;
        Callback cb = msg == 0x202 ? r.on_up : (msg == 0x200 || msg == 0x201) ? r.on_hover : nullptr;
        if (cb && r.ctx) cb(*r.ctx);
        return;
    }
}

// the page has faded out; do what was asked.
void Page::faded_out() {
    fade_alpha = 0.0f;
    fade = 1;
    switch (last_action) {
    case 1:
        run_accept();
        break;
    case 2:
        if (on_back) {
            on_back(*ctx);
        }
        break;
    case 3:
        if (on_x) {
            on_x(*ctx);
        }
        break;
    case 7:
        if (on_done) on_done(*ctx);
        break;
    default:
        return;
    }
    last_action = 0;
}


void Page::run_accept() {
    last_action = 1;
    if (on_accept) {
        on_accept(*ctx);
        return;
    }
    auto* t = dynamic_cast<TextItem*>(items[selected]);
    if (t && t->callback && t->ctx) t->callback(*t->ctx);
}

void Page::flash_selected() {

    if (items[selected]->selectable()) {
        flash_time = 0.2f;
        flash_color = 0xffff00ff;
        input_on = false;
    }
}

void Page::accept(BaseMenu& menu) {

    if (!items.empty()) {
        Item* it = items[selected];
        if (it->type() == Item::kToggle) {
            auto* t = static_cast<ToggleItem*>(it);
            if (!t->disabled) {
                menu.play_sound(0);
                uint16_t old = t->selected;
                t->selected = uint16_t(t->selected == t->options.size() - 1 ? 0 : t->selected + 1);
                if (old != t->selected && t->on_change && t->ctx) t->on_change(*t->ctx);
            }
            return;
        }
        if (it->type() == Item::kInteractive) {
            menu.play_sound(0);
            it->on_activate();
            return;
        }
        auto* t = dynamic_cast<TextItem*>(it);
        if (t && !t->disabled && t->callback) {
            menu.play_sound(1);
            if (it->selectable()) {
                flash_time = 0.2f;
                flash_color = 0xffff00ff;
                input_on = false;
            }
            last_action = 1;
            return;
        }
    }
    if (on_accept && ctx) {
        last_action = 1;
        on_accept(*ctx);
    }
}

void Page::back(BaseMenu& menu) {

    if (on_back) {
        menu.play_sound(1);
        fade = -1;
        input_on = false;
        last_action = 2;
    }
}

void Page::action_x(BaseMenu& menu) {

    if ((flags1f4 & 1) && on_x && !items.empty()) {
        auto* t = dynamic_cast<TextItem*>(items[selected]);
        if (t && !t->disabled && t->callback) {
            menu.play_sound(1);
            last_action = 3;
            flash_selected();
            return;
        }
    }
    if (on_x) {
        last_action = 3;
        on_x(*ctx);
    }
}

void Page::action(int id, Callback cb) {

    last_action = id;
    cb(*ctx);
}

void Page::navigate(BaseMenu& menu, int direction) {

    if (items.empty() || !items[selected]->selectable()) return;
    uint16_t old = selected;
    items[old]->color = 0x777777ff;
    do {
        if (direction == 0) {
            selected = uint16_t(selected == 0 ? items.size() - 1 : selected - 1);
        } else if (direction == 1) {
            selected = uint16_t(selected == items.size() - 1 ? 0 : selected + 1);
        } else if (items[selected]->type() == Item::kToggle) {
            auto* t = static_cast<ToggleItem*>(items[selected]);
            menu.play_sound(0);
            uint16_t was = t->selected;
            if (direction == 2) {
                if (was == 0) {
                    if (t->wraps) t->selected = uint16_t(t->options.size() - 1);
                } else {
                    t->selected = uint16_t(was - 1);
                }
            } else {
                if (was == t->options.size() - 1) {
                    if (t->wraps) t->selected = 0;
                } else {
                    t->selected = uint16_t(was + 1);
                }
            }
            if (was != t->selected && t->on_change && t->ctx) t->on_change(*t->ctx);
            return;
        }
    } while (!items[selected]->selectable());
    if (direction == 0 || direction == 1) menu.play_sound(0);
    if (selected != old && items[old]->type() == Item::kInteractive) items[old]->on_blur();
    if (items[selected]->type() == Item::kInteractive) items[selected]->on_focus();
}

// ---- BaseMenu

BaseMenu::BaseMenu(player::Game& g, player::Player& m) : game(g), movie(m) {}

void BaseMenu::set_page(int index) {

    int prev_port = -2;
    if (current) {
        current->set_items_shown(false);
        prev_port = current->port;
    }
    current_index = uint16_t(index);
    current = &pages[size_t(index)];
    if (current->active) {
        current->input_port = int16_t(prev_port);
        current->port = int16_t(prev_port);
    }
    current->set_items_shown(true);
    if (current->on_enter) current->on_enter(*current->ctx);
    check_requirements(prev_port, current->flags);
}

void BaseMenu::pointer(int message, float x, float y) {
    if (current) current->mouse(*this, message, x, y);
}

void BaseMenu::update(float dt) {

    check_each_tick(current->flags);
    current->update(*this, dt);
}

void BaseMenu::render(render::Renderer& r) {
    if (current) current->render(r);
}

bool BaseMenu::signed_in(int port) const {
    // the original's without Steam users: the signed-in mask.
    return port >= 0 && port < 5 && (online.signed_in & (1u << port));
}

void BaseMenu::play_sound(int which) {
    // 0 the toggle, 1 the select sound, once, at full volume.
    audio::manager().play(uint32_t(which), 1.0f, 0.0f, false);
}

void BaseMenu::check_requirements(int, uint16_t) {
    // the page's requirements (Steam, sign-in, online) are met
    // offline with Steam running.
}

void BaseMenu::check_each_tick(uint16_t) {

}

}  // namespace menu
